/* SPDX-License-Identifier: MPL-2.0
 * TONIC sequencer [MANUAL 10]: four tracks of up to 64 steps, each with its own length and scale; swing; micro
 * timing; trig conditions; p-locks and sound locks; note length; master length and change length; pattern change at
 * the change point; the arpeggiator. It runs in the render, once a control block (CR samples): events land on the
 * control-block grid (0.73 ms), well inside the micro-timing unit (1/384 note: 5.2 ms at 120 BPM).
 *
 * Time is in ticks of 1/384 note (24 a step at 1X). A track's position runs at its scale; the master counter at
 * 1X. A step's event is at s*24 + swing (odd steps) + micro, and may fall in the previous step (negative micro) or
 * the next (late micro + swing), across the loop's ends too: each block checks the steps around its window on the
 * unwrapped timeline, so every event fires exactly once and counts for the loop it belongs to. */
#include "eng_int.h"

#define NSN 16                    /* sequencer notes held per track (for their note-offs) */
#define TPS 24.0f                 /* ticks per step */

typedef struct {
    float pos;                    /* ticks into the track's loop */
    uint32_t loop;                /* the track's loops since start (A:B, 1ST) */
    int8_t last;                  /* the last evaluated trig condition on this track: 1 true, 0 false, -1 none */
    int8_t step;                  /* the step playing (screen) */
    struct { uint32_t nid; uint8_t on; float left; } sn[NSN];   /* the notes' ids (engine.c) */
    /* locks on the pattern-level parameters (FX, track level): they hold until the track's next trig */
    int16_t fxl[NFXP];
    uint64_t fxl_on;              /* bit: fxl[i] in force (NFXP <= 64: CPMIX is the 33rd) [0.9.2] */
    int16_t lvl;                  /* track level lock, -1 none */
    uint64_t skip;                /* steps not to play this pass (written by live recording just now) */
    /* retrig [TONIC]: the last note trig repeats every ival ticks over its length */
    struct {
        uint8_t on, step, nn, vel0;
        uint8_t note[NNOTE];
        int8_t ft, lt;
        int16_t port, rvel;
        const sound_t *snd;
        float el, at, ival, total;   /* ticks since the trig; the next repeat's time; the rate; the length */
    } rt;
    /* the arpeggiator */
    struct { uint8_t note, vel; float left; } ah[8];   /* held notes, in the order they came (left < 0: a key) */
    uint8_t nah;
    uint8_t a_idx, a_oct, a_step, a_up;
    float a_next;                 /* global tick of its next note */
    float r0, rpos;               /* ROLL [TONIC 0.7]: the repeated stretch's start, the position in it */
    uint32_t npass;               /* the track's passes, never reset (live recording) [TONIC 0.9.2] */
} strk_t;

static strk_t st[NTRK];
static float mpos, cpos;          /* the master and change counters, ticks at 1X */
static float gtick;               /* ticks since boot (the arpeggiator's clock) */
static uint8_t starting;          /* the first block after PLAY */
static pattern_t *cued;
static volatile uint8_t took_cue;
static uint32_t pat_loops;
static uint32_t pat_ends;         /* the pattern's passes since PLAY, across changes (song mode) [TONIC 0.9.2] */
volatile uint8_t seq_click;        /* a beat: 1, a bar: 2 (engine.c plays the metronome) */
uint32_t eng_now;

void seq_init(void)
{
    int t;
    for (t = 0; t < NTRK; t++) {
        st[t].last = -1;
        st[t].step = -1;
        st[t].lvl = -1;
    }
}

/* --------------------------------------------------------------------------- locks --- */
#define MAXL 64
static int gather(const pattern_t *p, int t, int s, plock_t *out)   /* the locks of a trig */
{
    int i, n = 0;
    uint8_t key = LOCK_KEY(t, s);
    for (i = 0; i < p->nlock && n < MAXL; i++) {
        plock_t l = p->lock[i];
        if (l.f.param != P_NONE && l.f.step == key)
            out[n++] = l;
    }
    return n;
}
static int lock_of(const plock_t *l, int n, int param, int def)
{
    int i;
    for (i = 0; i < n; i++)
        if (l[i].f.param == param)
            return l[i].f.val;
    return def;
}

/* ------------------------------------------------------------------ the conditions --- */
static uint32_t rng_s = 0x1234567u;
static inline uint32_t rng(void) { rng_s ^= rng_s << 13; rng_s ^= rng_s >> 17; rng_s ^= rng_s << 5; return rng_s; }
static inline int chance(int pct) { return pct >= 100 || (int)(rng() % 100u) < pct; }

/* [MANUAL 10.9.3] 1 = the trig plays. loop: the loop the trig belongs to (0 = the first) */
static int cond_true(int t, int c, uint32_t loop, int prob)
{
    int r;
    switch (c) {
    case C_NONE: return chance(prob);              /* the track's PROB (a conditional lock overrides it) */
    case C_FILL: r = eng.fill != 0; break;
    case C_NFILL: r = eng.fill == 0; break;
    case C_PRE: return st[t].last == 1;            /* PRE / !PRE are not evaluated themselves */
    case C_NPRE: return st[t].last == 0;
    case C_NEI: return t > 0 && st[t - 1].last == 1;
    case C_NNEI: return t > 0 && st[t - 1].last == 0;
    case C_1ST: r = loop == 0; break;
    case C_N1ST: r = loop != 0; break;
    default:
        if (c >= C_PROB0 && c < C_AB0)
            r = chance(PROB_PCT[c - C_PROB0]);
        else if (c >= C_AB0 && c < C_END) {
            const uint8_t *ab = COND_AB[c - C_AB0];
            r = (int)(loop % ab[1]) == ab[0] - 1;      /* 1:2 the 1st, 3rd...; 2:4 the 2nd, 6th... */
        } else
            r = 1;
        break;
    }
    st[t].last = (int8_t)r;
    return r;
}

/* -------------------------------------------------------------- sequencer notes --- */
static void sn_add(int t, uint32_t nid, float len)
{
    strk_t *k = &st[t];
    int i, j = -1;
    if (!nid)
        return;
    for (i = 0; i < NSN; i++)
        if (!k->sn[i].on) {
            j = i;
            break;
        }
    if (j < 0) {                                    /* full: end the shortest */
        j = 0;
        for (i = 1; i < NSN; i++)
            if (k->sn[i].left < k->sn[j].left)
                j = i;
        eng_note_end(k->sn[j].nid);
    }
    k->sn[j].nid = nid;
    k->sn[j].on = 1;
    k->sn[j].left = len;
}
static void sn_tick(int t, float dt)
{
    strk_t *k = &st[t];
    int i;
    for (i = 0; i < NSN; i++)
        if (k->sn[i].on && k->sn[i].left < 1e30f) {
            k->sn[i].left -= dt;
            if (k->sn[i].left <= 0) {
                k->sn[i].on = 0;
                eng_note_end(k->sn[i].nid);
            }
        }
}
void seq_release_all(void)
{
    int t, i;
    for (t = 0; t < NTRK; t++) {
        for (i = 0; i < NSN; i++)
            if (st[t].sn[i].on) {
                st[t].sn[i].on = 0;
                eng_note_end(st[t].sn[i].nid);
            }
        st[t].nah = 0;
        st[t].rt.on = 0;
    }
}

/* -------------------------------------------------------------------- arpeggiator --- */
/* [MANUAL 9.6] SPD as note divisions: ticks of 1/384 */
static const uint16_t ARP_DIV[11] = {384, 192, 96, 48, 32, 24, 16, 12, 8, 6, 4};
const char *const ARP_DIV_NAME[11] = {"1/1", "1/2", "1/4", "1/8", "1/12", "1/16", "1/24", "1/32", "1/48", "1/64", "1/96"};

int arp_on(int t) { const sound_t *s = ts[t].snd; return s && s->s[S_ARPMODE] != ARP_OFF; }

void arp_hold(int t, int note, int vel, float len)    /* len < 0: held by a key until arp_free */
{
    strk_t *k = &st[t];
    int i;
    for (i = 0; i < k->nah; i++)
        if (k->ah[i].note == note) {
            k->ah[i].left = len;
            k->ah[i].vel = (uint8_t)vel;
            return;
        }
    if (k->nah >= 8)
        return;
    if (!k->nah) {                                   /* a new phrase: from its start, on the next division */
        const sound_t *s = ts[t].snd;
        float div = ARP_DIV[s->s[S_ARPSPD] < 11 ? s->s[S_ARPSPD] : 5];
        k->a_idx = k->a_oct = k->a_step = 0;
        k->a_up = 1;
        k->a_next = eng.playing ? fm_floorf(gtick / div) * div + div : gtick;
        if (eng.playing && gtick - (k->a_next - div) < 2.0f)   /* right on a division: now */
            k->a_next -= div;
    }
    k->ah[k->nah].note = (uint8_t)note;
    k->ah[k->nah].vel = (uint8_t)vel;
    k->ah[k->nah].left = len;
    k->nah++;
}
void arp_free(int t, int note)
{
    strk_t *k = &st[t];
    int i;
    for (i = 0; i < k->nah; i++)
        if (k->ah[i].note == note && k->ah[i].left < 0) {
            for (; i + 1 < k->nah; i++)
                k->ah[i] = k->ah[i + 1];
            k->nah--;
            return;
        }
}

/* the notes the arpeggiator walks: the held notes, each as its chord (up to 32) */
static int arp_list(int t, strk_t *k, uint8_t *nt, uint8_t *vl)
{
    const sound_t *s = ts[t].snd;
    int n = 0, i, j, c;
    uint8_t ch[NCHORD];
    for (i = 0; i < k->nah; i++) {
        c = s ? chord_notes(s, k->ah[i].note, eng_pat ? eng_pat->trk[t].kscale : 0, eng_pat ? eng_pat->trk[t].kroot : 0,
                            ch) : (ch[0] = k->ah[i].note, 1);
        for (j = 0; j < c && n < 32; j++) {
            nt[n] = ch[j];
            vl[n++] = k->ah[i].vel;
        }
    }
    return n;
}

static int arp_pick(int t, strk_t *k, int mode, int *vel, int *count)
{
    uint8_t ord[32], nt[32], vl[32];
    int n = arp_list(t, k, nt, vl), i, j, idx;
    *count = n;
    for (i = 0; i < n; i++)
        ord[i] = (uint8_t)i;
    if (mode != ARP_TRUE)                            /* sorted by pitch */
        for (i = 1; i < n; i++)
            for (j = i; j > 0 && nt[ord[j]] < nt[ord[j - 1]]; j--) {
                uint8_t x = ord[j];
                ord[j] = ord[j - 1];
                ord[j - 1] = x;
            }
    if (k->a_idx >= n)
        k->a_idx = 0;
    idx = k->a_idx;
    if (mode == ARP_RANDOM && n)                     /* RANDOM [TONIC 0.8]: any of them */
        idx = (int)(rng() % (uint32_t)n);
    else if (mode == ARP_DOWN)
        idx = n - 1 - idx;
    else if (mode == ARP_CYCLE && !k->a_up)
        idx = n - 1 - idx;
    *vel = vl[ord[idx]];
    return nt[ord[idx]];
}

static void arp_block(int t, float dt1x)
{
    strk_t *k = &st[t];
    const sound_t *s = ts[t].snd;
    int i;
    for (i = 0; i < k->nah; i++)                     /* notes from trigs end with their length */
        if (k->ah[i].left >= 0) {
            k->ah[i].left -= dt1x;
            if (k->ah[i].left <= 0) {
                int j;
                for (j = i; j + 1 < k->nah; j++)
                    k->ah[j] = k->ah[j + 1];
                k->nah--;
                i--;
            }
        }
    if (!k->nah || !s || s->s[S_ARPMODE] == ARP_OFF)
        return;
    if (gtick >= k->a_next) {
        float div = ARP_DIV[s->s[S_ARPSPD] < 11 ? s->s[S_ARPSPD] : 5];
        int vel, cnt, note = arp_pick(t, k, s->s[S_ARPMODE], &vel, &cnt), alen = s->s[S_ARPSTEPS] ? s->s[S_ARPSTEPS] : 16;
        int step = k->a_step % alen;
        k->a_next += div;
        if (k->a_next < gtick)
            k->a_next = gtick + div;
        if ((s->arp_on >> step) & 1u) {             /* an active arp step: the note with its offset */
            int n = note + 12 * k->a_oct + s->arp_ofs[step];
            float len = (float)LEN_TICKS[s->s[S_ARPLEN] < NLEN ? s->s[S_ARPLEN] : 14] * (1.0f / 4.0f);
            if (s->s[S_ARPMODE] == ARP_CHORD) {      /* CHORD [TONIC 0.8]: all the notes at once, each step */
                uint8_t nt[32], vl[32], ch[32];
                int c = arp_list(t, k, nt, vl), m = 0, j;
                for (j = 0; j < c; j++) {
                    int x = nt[j] + 12 * k->a_oct + s->arp_ofs[step];
                    if (x >= 0 && x < 128 && m < 8)
                        ch[m++] = (uint8_t)x;
                }
                if (m)
                    sn_add(t, eng_notes_on(t, ch, m, vel, 0, 0, 0, 1, 1, 0, 0), len < 1.0f ? 1.0f : len);
                cnt = 1;                             /* (each step a whole chord: the octave moves every step) */
            } else if (n >= 0 && n < 128)
                sn_add(t, eng_voice_on(t, n, vel, 0, 0, 0, 1, 1, 0, 0), len < 1.0f ? 1.0f : len);
        }
        k->a_step = (uint8_t)((k->a_step + 1) % alen);
        /* the next note; after a cycle the octave goes up, to RNG octaves, then back [MANUAL 9.6.3] */
        if (++k->a_idx >= cnt) {
            k->a_idx = 0;
            if (s->s[S_ARPMODE] == ARP_CYCLE && cnt > 1) {
                k->a_up ^= 1;
                if (k->a_up && ++k->a_oct >= (s->s[S_ARPRNG] ? s->s[S_ARPRNG] : 1))
                    k->a_oct = 0;
            } else if (++k->a_oct >= (s->s[S_ARPRNG] ? s->s[S_ARPRNG] : 1))
                k->a_oct = 0;
        }
    }
}

/* ------------------------------------------------------------------------ retrig --- */
/* RTIM [TONIC]: note divisions (1/16 = one step at 1X = 24 ticks), as Elektron's retrig rates */
static const uint8_t RT_DIV[17] = {1, 2, 3, 4, 5, 6, 8, 10, 12, 16, 20, 24, 32, 40, 48, 64, 80};

/* the repeats due on track t (dt: its ticks this block) */
static void rt_block(pattern_t *p, int t, float dt)
{
    strk_t *k = &st[t];
    plock_t lk[MAXL];
    if (!k->rt.on)
        return;
    k->rt.el += dt;
    while (k->rt.on && k->rt.el >= k->rt.at) {
        float fr, g;
        int n, nl, vel;
        if (k->rt.at >= k->rt.total - 0.01f) {
            k->rt.on = 0;
            break;
        }
        /* RVEL: - fades the repeats out (to silence at -128), + fades them in (from silence at +127) */
        fr = k->rt.total < 1e30f ? k->rt.at / k->rt.total : 0.0f;
        g = k->rt.rvel < 0 ? 1.0f + (float)k->rt.rvel * (1.0f / 128.0f) * fr
                           : 1.0f - (float)k->rt.rvel * (1.0f / 128.0f) * (1.0f - fr);
        vel = clampi((int)((float)k->rt.vel0 * g + 0.5f), 1, 127);
        k->rt.at += k->rt.ival;
        if (eng.mute >> t & 1u)
            continue;
        nl = (p->trig[t][k->rt.step].flags & (TF_PLOCK | TF_SLOCK)) ? gather(p, t, k->rt.step, lk) : 0;
        for (n = 0; n < k->rt.nn; n++)
            sn_add(t, eng_chord_on(t, k->rt.note[n], vel, lk, nl, LOCK_KEY(t, k->rt.step), k->rt.ft, k->rt.lt,
                                   k->rt.port, k->rt.snd), fm_maxf(0.5f, k->rt.ival * 0.9f));
    }
}

/* ----------------------------------------------------------------------- the trigs --- */
static float swing_ticks(const pattern_t *p) { return (float)p->swing * 0.48f - TPS; }

/* the trig at step s of track t fires; loop: the loop it belongs to */
static void fire(pattern_t *p, int t, int s, uint32_t loop)
{
    trig_t *tr = &p->trig[t][s];
    track_t *tk = &p->trk[t];
    strk_t *k = &st[t];
    plock_t lk[MAXL];
    int nl, prob, i;
    if (!(tr->flags & (TF_NOTE | TF_LOCK)))
        return;
    if ((k->skip >> s) & 1u) {                       /* recorded in this pass: it already sounded */
        k->skip &= ~(1ull << s);
        return;
    }
    nl = (tr->flags & (TF_PLOCK | TF_SLOCK)) ? gather(p, t, s, lk) : 0;
    prob = lock_of(lk, nl, P_PROB, tk->tp[P_PROB - NSP]);
    if (!cond_true(t, tr->cond, loop, prob))
        return;
    /* pattern-level locks hold until this track's next trig: the last ones end here */
    k->fxl_on = 0;
    k->lvl = -1;
    for (i = 0; i < nl; i++) {
        int pp = lk[i].f.param;
        if (pp >= P_CDEP && pp < NPARAM) {
            k->fxl[pp - P_CDEP] = lk[i].f.val;
            k->fxl_on |= (uint64_t)1 << (pp - P_CDEP);
        } else if (pp == P_TLVL)
            k->lvl = lk[i].f.val;
    }
    if (tr->flags & TF_NOTE) {
        const sound_t *snd = &p->snd[t];
        int slock = lock_of(lk, nl, P_SOUND, -1);
        int vel = tr->vel ? tr->vel : lock_of(lk, nl, P_TVEL, tk->tp[P_TVEL - NSP]);
        int li = tr->len != 0xFF ? tr->len : lock_of(lk, nl, P_TLEN, tk->tp[P_TLEN - NSP]);
        int ft = lock_of(lk, nl, P_FLTT, tk->tp[P_FLTT - NSP]);
        int lt = lock_of(lk, nl, P_LFOT, tk->tp[P_LFOT - NSP]);
        int port = lock_of(lk, nl, P_PORT, tk->tp[P_PORT - NSP]) ? lock_of(lk, nl, P_PTIM, tk->tp[P_PTIM - NSP]) : 0;
        int root = tr->note;
        float len = li >= LEN_INF ? 1e31f : (float)LEN_TICKS[li < NLEN ? li : 14] * (1.0f / 4.0f);
        int n;
        root = lock_of(lk, nl, P_NOTE, root);
        if (slock >= 0 && slock < NPOOL && eng_pool && eng_pool[slock].name[0])
            snd = &eng_pool[slock];
        if (snd->s[S_ENGINE] != ENG_DR)              /* (a kit's notes are its pads: not transposed) */
            root += tk->transpose;
        k->rt.on = 0;                                /* a note trig ends the last one's repeats */
        if (eng.mute >> t & 1u)
            return;
        if (lock_of(lk, nl, P_RTRG, tk->tp[P_RTRG - NSP]) && !arp_on(t)) {   /* retrig [TONIC] */
            int ri = clampi(lock_of(lk, nl, P_RTIM, tk->tp[P_RTIM - NSP]), 0, 16);
            k->rt.on = 1;
            k->rt.step = (uint8_t)s;
            k->rt.nn = 0;
            k->rt.vel0 = (uint8_t)vel;
            k->rt.ft = (int8_t)ft;
            k->rt.lt = (int8_t)lt;
            k->rt.port = (int16_t)port;
            k->rt.rvel = (int16_t)lock_of(lk, nl, P_RVEL, tk->tp[P_RVEL - NSP]);
            k->rt.snd = snd;
            k->rt.el = 0;
            k->rt.ival = 384.0f / (float)RT_DIV[ri];
            k->rt.at = k->rt.ival;
            k->rt.total = len;
            if (len > k->rt.ival * 0.9f)             /* each repeat is a short note */
                len = k->rt.ival * 0.9f;
        }
        for (n = 0; n < (tr->nnote ? tr->nnote : 1); n++) {
            int note = root + (n ? tr->chord[n - 1] : 0);
            if (note < 0 || note > 127)
                continue;
            if (k->rt.on)
                k->rt.note[k->rt.nn++] = (uint8_t)note;
            if (arp_on(t))
                arp_hold(t, note, vel, len);
            else
                sn_add(t, eng_chord_on(t, note, vel, lk, nl, LOCK_KEY(t, s), ft, lt, port, snd), len < 0.5f ? 0.5f : len);
        }
    } else
        eng_lock_trig(t, lk, nl, LOCK_KEY(t, s));
}

/* the events of track t with unwrapped times in [a, b) of loop `loop` */
static void window(pattern_t *p, int t, float a, float b, uint32_t loop, int first)
{
    int L = p->trk[t].len ? p->trk[t].len : 16, s, s0 = (int)fm_floorf(a / TPS) - 2, s1 = (int)fm_floorf(b / TPS) + 1;
    float sw = swing_ticks(p);
    for (s = s0; s <= s1; s++) {
        int sm = ((s % L) + L) % L;
        const trig_t *tr = &p->trig[t][sm];
        float e;
        if (!(tr->flags & (TF_NOTE | TF_LOCK)))
            continue;
        e = (float)s * TPS + (float)tr->micro + ((sm & 1) ? sw : 0.0f);
        if (first && s == 0 && e < a)                /* PLAY: an early first step plays at once */
            e = a;
        if (e >= a && e < b) {
            uint32_t lp = s < 0 ? loop - 1 : s >= L ? loop + 1 : loop;
            fire(p, t, sm, s < 0 && loop == 0 ? 0 : lp);
        }
    }
}

/* the pattern's change point has come: play the cued pattern from its start */
static void take_cue(void)
{
    int t;
    if (!cued)
        return;
    eng_pat = cued;
    cued = 0;
    took_cue = 1;
    mpos = cpos = 0;
    pat_loops = 0;
    for (t = 0; t < NTRK; t++) {
        st[t].pos = 0;
        st[t].loop = 0;
        st[t].npass++;
        st[t].last = -1;
        st[t].fxl_on = 0;
        st[t].lvl = -1;
        st[t].skip = 0;
        st[t].rt.on = 0;
    }
    starting = 1;
}

void seq_cue(pattern_t *p) { cued = p; }
pattern_t *seq_cued(void) { return cued; }
int seq_took_cue(void) { int r = took_cue; took_cue = 0; return r; }

void seq_start(void)
{
    int t;
    mpos = cpos = 0;
    pat_loops = 0;
    pat_ends = 0;
    gtick = 0;                                       /* the arpeggiator's divisions from PLAY [TONIC 0.9.2] */
    for (t = 0; t < NTRK; t++) {
        st[t].pos = 0;
        st[t].loop = 0;
        st[t].npass++;
        st[t].a_next = 0;
        st[t].last = -1;
        st[t].skip = 0;
        st[t].fxl_on = 0;
        st[t].lvl = -1;
        st[t].rt.on = 0;
    }
    starting = 1;
}

/* one control block */
float eng_speed = 1.0f;
static float roll_prev;           /* the ROLL length in force (ticks at 1X), 0 none */
/* the ROLL held: the shortest of the four (ticks at 1X: 1/4 96 .. 1/32 12), 0 none */
static float roll_len(void)
{
    uint16_t f = eng.pfx;
    return f >> PFX_ROLL32 & 1u ? 12.0f : f >> PFX_ROLL16 & 1u ? 24.0f : f >> PFX_ROLL8 & 1u ? 48.0f :
           f >> PFX_ROLL4 & 1u ? 96.0f : 0.0f;
}

void seq_block(void)
{
    pattern_t *p = eng_pat;
    float d1x = CRF * INV_SR * eng.bpm * (96.0f / 60.0f);   /* ticks at 1X this block */
    float roll = roll_len();
    int t;
    if (eng_speed != 1.0f)                           /* HALF, TAPE [TONIC 0.7] */
        d1x *= eng_speed;
    eng_now++;
    gtick += d1x;
    if (!p)
        return;
    if (eng.playing && eng_clock_cb) {               /* MIDI clock out: one every 4 ticks */
        static float clk;
        clk += d1x;
        while (clk >= 4.0f) {
            clk -= 4.0f;
            eng_clock_cb(0);
        }
    }
    if (eng.playing) {                               /* the metronome's beats (1X quarters from PLAY) */
        static float beat;
        static uint32_t nbeat;
        if (starting)
            beat = 96.0f, nbeat = 0;
        beat += starting ? 0.0f : d1x;
        if (beat >= 96.0f) {
            beat -= 96.0f;
            seq_click = (nbeat++ % 4u) ? 1 : 2;
        }
    }
    if (eng.playing) {
        float ml;
        int change = 0;
        int roll_new = roll != roll_prev && roll > 0.0f;
        if (roll == 0.0f || starting)
            roll_prev = 0.0f;
        for (t = 0; t < NTRK; t++) {
            track_t *tk = &p->trk[t];
            strk_t *k = &st[t];
            float sm = SCALE_MUL[tk->scale < 7 ? tk->scale : 4];
            float dt = d1x * sm, L = (float)(tk->len ? tk->len : 16) * TPS;
            float a = k->pos, b = a + dt;
            int play = !(roll > 0.0f && !starting);  /* ROLL: the real position runs on silently */
            rt_block(p, t, dt);                      /* the repeats first: a trig in this block starts afresh */
            if (starting)
                window(p, t, a, b, k->loop, 1);
            else if (b < L) {
                if (play)
                    window(p, t, a, b, k->loop, 0);
            } else {                                 /* the track's end: the rest of this loop, then the next */
                if (play)
                    window(p, t, a, L, k->loop, 0);
                k->loop++;
                k->npass++;
                b -= L;
                if (play)
                    window(p, t, 0, b, k->loop, 0);
                if (t == 0 && (!p->lenmode || (p->mlen == 0 && p->chlen == 0)))   /* (0.12: per track with M.LEN
                                                      * INF and no CH.LEN too: at T1's end, not never) */
                    change = 1, pat_loops++, pat_ends++;
            }
            if (!play) {                             /* ROLL [TONIC 0.7]: the stretch around where it began, again */
                float lt = roll * sm, ra, rb, end;
                if (roll_new) {                      /* (a new length while rolling: around the rolled place) */
                    float ref = roll_prev > 0.0f ? k->rpos : a;
                    k->r0 = fm_floorf(ref / lt) * lt;
                    k->rpos = ref;
                }
                ra = k->rpos;
                rb = ra + dt;
                end = k->r0 + lt;
                if (rb < end)
                    window(p, t, ra, rb, k->loop, 0);
                else {
                    window(p, t, ra, end, k->loop, 0);
                    rb = k->r0 + (rb - end);
                    window(p, t, k->r0, rb, k->loop, 0);
                }
                k->rpos = rb;
                k->step = (int8_t)((int)(rb / TPS) % (tk->len ? tk->len : 16));
            } else
                k->step = (int8_t)(b / TPS);
            k->pos = b;
            sn_tick(t, dt);
        }
        if (roll > 0.0f && !starting)
            roll_prev = roll;
        starting = 0;
        /* per-track mode [MANUAL 10.8.2]: M.LEN restarts every track; the change point comes every CH.LEN, else
         * every M.LEN (INF and no CH.LEN: never) */
        if (p->lenmode) {
            float mlen = (float)p->mlen * TPS, clen = (float)p->chlen * TPS;
            ml = clen > 0 && (mlen <= 0 || clen < mlen) ? clen : mlen;
            mpos += d1x;
            cpos += d1x;
            if (mlen > 0 && mpos >= mlen) {
                mpos -= mlen;
                for (t = 0; t < NTRK; t++) {
                    st[t].pos = 0;
                    st[t].loop++;
                    st[t].npass++;
                }
            }
            if (ml > 0 && cpos >= ml) {
                cpos -= ml;
                change = 1;
                pat_loops++;
                pat_ends++;
            }
        }
        if (change) {                                /* the pattern's start: a cued FILL begins, one ends */
            if (eng.fill == 3)
                eng.fill = 0;
            if (eng.fill_cue) {
                eng.fill_cue = 0;
                eng.fill = 3;
            }
        }
        if (change && cued)
            take_cue();
    } else {
        for (t = 0; t < NTRK; t++)
            sn_tick(t, d1x);
    }
    for (t = 0; t < NTRK; t++)
        arp_block(t, d1x);
}

int seq_step(int t) { return eng.playing ? st[t].step : -1; }
uint32_t seq_loops(void) { return pat_loops; }
uint32_t seq_ends(void) { return pat_ends; }
uint32_t seq_pass(int t) { return st[t].npass; }
float seq_pos(int t) { return st[t].pos; }
float seq_gtick(void) { return gtick; }
void seq_skip(int t, int s) { st[t].skip |= 1ull << s; }
/* the pattern-level values in force (FX / level locks of the tracks' last trigs) */
int16_t seq_fx(const pattern_t *p, int i)
{
    int t;
    for (t = 0; t < NTRK; t++)
        if (st[t].fxl_on >> i & 1u)
            return st[t].fxl[i];
    return p->fx[i];
}
void seq_fx_all(const pattern_t *p, int16_t *fx)
{
    uint64_t on = 0;
    int t, i;
    for (t = 0; t < NTRK; t++)
        on |= st[t].fxl_on;
    if (!on) {                                       /* (no locks in force: the pattern's, without the search) */
        for (i = 0; i < NFXP; i++)
            fx[i] = p->fx[i];
        return;
    }
    for (i = 0; i < NFXP; i++)
        fx[i] = seq_fx(p, i);
}
int seq_level(const pattern_t *p, int t) { return st[t].lvl >= 0 ? st[t].lvl : p->trk[t].tp[P_TLVL - NSP]; }
