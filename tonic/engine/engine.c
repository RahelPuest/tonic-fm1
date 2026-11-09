/* SPDX-License-Identifier: MPL-2.0
 * TONIC engine: the render (control blocks: notes in, the sequencer, the voices, the effects), voice allocation
 * [MANUAL 10.5 VOICE MENU, 9.5.6 PLAY MODE], the notes from the keys and MIDI, live recording, transport. */
#include "eng_int.h"

eng_state_t eng = {.bpm = 120.0f};
pattern_t *volatile eng_pat;
sound_t *eng_pool;
trackstate_t ts[NTRK];
void (*eng_midi_out)(uint8_t st, uint8_t d1, uint8_t d2);
uint8_t eng_midi_chan[NTRK] = {16, 16, 16, 16};
uint8_t eng_midi_only;
void (*eng_clock_cb)(int what);
volatile uint32_t eng_ofs;

static voice_t fader[NFADE];                         /* (0.17) stolen voices fading out, three at once */
static void midi_pend_add(int trk, int note, uint32_t nid);             /* a stolen voice fades out here while its voice starts the new note */
static uint32_t age_ctr, nid_ctr;
static uint16_t strum_wait;           /* (voice_on1 gives it to the voices it starts) */

static uint32_t cpu_samples;
/* (0.16) the second core: its voices (a run of the sounding ones, the last in the voices' order) and their buffers */
static const eng_dual_t *dual;
static voice_t *c1_v[NVOICE];
static float *c1_buf;                                /* ENG_DUAL_MAXV x 5 x CR: dry L, R, the three sends */
static uint8_t c1_snd[ENG_DUAL_MAXV];                /* (0.17) the voice has sends: its send buffers are used */
static uint32_t dual_lost;
uintptr_t eng_c1_stack_lo, eng_c1_stack_hi;
static float meters[NTRK];
static const sound_t init_snd;    /* zeros: used only before a pattern is set */
/* sidechain ducking [TONIC]: the source track's notes (DUCK T1..T4) set the envelope, which falls back with REL */
static int duck_src;              /* 0 off, else the track + 1; NTRK+1.. a division of the tempo (0.10) */
static float duck_env, duck_k = 0.9f;
/* [0.10] PUMP: a kit source's pad (0 all, else the pad + 1); the dip's attack (a block's rise, 0: at once) and
 * whether it is rising; the last division's index; the gain now and the division's phase (for the screen) */
static int duck_pad, duck_keep;
static float duck_rise, duck_g = 1.0f, duck_ph = -1.0f, duck_len;
static uint8_t duck_up;
static int32_t duck_bi = -1;
static const float DUCK_DIV[5] = {96.0f, 48.0f, 24.0f, 192.0f, 384.0f};   /* 1/4 1/8 1/16 1/2 1/1, in ticks */
static void duck_trig(void)
{
    if (duck_rise > 0.0f)
        duck_up = 1;
    else
        duck_env = 1.0f;
}
float engine_duck(float *ph, float *len)
{
    if (ph)
        *ph = duck_ph;
    if (len)
        *len = duck_len;
    return duck_g;
}
/* the master compressor [TONIC 0.6]: its settings this block (from the FX values), the gain reduction (dB) and the
 * gain now */
static int16_t cp_amt, cp_atk, cp_rel, cp_mix;
static float cp_gr, cp_g = 1.0f;
float engine_comp_gr(void) { return cp_amt ? cp_gr : 0.0f; }
/* the momentary performance effects [TONIC 0.7]: TAPE's speed (1 .. 0), the filter sweep's offset (-64 .. 63) */
float eng_pitch;
static float tape_s = 1.0f, sweep;
static int16_t duck_rel = -1;

/* ------------------------------------------------------------------- the note queue --- */
/* the main loop (keys, MIDI in the main loop) to the render: single producer, single consumer */
enum { Q_ON, Q_OFF, Q_MOD, Q_ALLOFF, Q_PARAM, Q_PANIC, Q_SOUND, Q_START, Q_STOP };
typedef struct { uint8_t type, trk, a; int16_t b; } qev_t;
#define QN 128                    /* (0.17: 64 -> 128, a MIDI burst and the keys in one pass) */
static qev_t q[QN];
static volatile uint32_t q_w, q_r;
static void qput(int type, int trk, int a, int b)
{
    uint32_t w = q_w;
    if (w - q_r >= QN)
        return;
    q[w % QN].type = (uint8_t)type;
    q[w % QN].trk = (uint8_t)trk;
    q[w % QN].a = (uint8_t)a;
    q[w % QN].b = (int16_t)b;
    __asm__ volatile("" ::: "memory");
    q_w = w + 1;
}
void engine_note_on(int trk, int note, int vel) { qput(Q_ON, trk, note, vel); }
void engine_note_off(int trk, int note) { qput(Q_OFF, trk, note, 0); }
void engine_mod(int trk, int src, int val) { qput(Q_MOD, trk, src, val); }
void engine_all_off(int trk) { qput(Q_ALLOFF, trk < 0 ? 255 : trk, 0, 0); }
void engine_sound_changed(int trk) { qput(Q_SOUND, trk, 0, 0); }
void engine_param_changed(int trk, int param)
{
    pattern_t *p = eng_pat;
    if (p && param < NSP)
        qput(Q_PARAM, trk, param, p->snd[trk].p[param]);
}

/* --------------------------------------------------------------- voice allocation --- */
static int track_range(int trk, int *lo, int *hi)   /* the voices a track may use: [lo, hi) and its pool */
{
    pattern_t *p = eng_pat;
    int t, base = 0, locked = 0;
    for (t = 0; t < NTRK; t++)
        locked += p ? p->trk[t].voices : 0;
    if (locked > NVOICE)
        locked = NVOICE;
    for (t = 0; t < trk; t++)
        base += p ? p->trk[t].voices : 0;
    if (p && p->trk[trk].voices) {                   /* locked voices: its own, and only those (0.17: none past
                                                      * the twelve) */
        *lo = base < NVOICE ? base : NVOICE;
        *hi = base + p->trk[trk].voices;
        if (*hi > NVOICE)
            *hi = NVOICE;
        return 1;
    }
    *lo = locked < NVOICE ? locked : NVOICE;         /* dynamic: the unlocked ones (0.17: all locked, none: no longer
                                                      * the last locked one shared) */
    *hi = NVOICE;
    return 0;
}

static int pick_voice(int trk, int note)
{
    pattern_t *p = eng_pat;
    int lo, hi, i, best = -1, mode = p ? p->steal : 0;
    uint32_t oldest = 0xFFFFFFFFu;
    track_range(trk, &lo, &hi);
    if (p && p->reuse)                               /* REUSE: the same note takes its voice again */
        for (i = lo; i < hi; i++)
            if (vc[i].on && vc[i].trk == trk && vc[i].note == note)
                return i;
    for (i = lo; i < hi; i++)                        /* a free voice */
        if (!vc[i].on)
            return i;
    for (i = lo; i < hi; i++)                        /* the oldest released one */
        if (!vc[i].gate && vc[i].age < oldest) {
            oldest = vc[i].age;
            best = i;
        }
    if (best >= 0)
        return best;
    /* steal [MANUAL 10.5]: CYCLE the first played, TRACK the lowest-priority track (T4 before T1), LO, HI */
    for (i = lo; i < hi; i++) {
        int better;
        if (best < 0) {
            best = i;
            continue;
        }
        switch (mode) {
        case 1: better = vc[i].trk > vc[best].trk || (vc[i].trk == vc[best].trk && vc[i].age < vc[best].age); break;
        case 2: better = vc[i].note < vc[best].note; break;
        case 3: better = vc[i].note > vc[best].note; break;
        default: better = vc[i].age < vc[best].age; break;
        }
        if (better)
            best = i;
    }
    return best;
}

static void steal(voice_t *v)          /* the voice's sound fades out in the fader while the voice is reused */
{
    if (!v->on)
        return;
    int f, best = 0;
    for (f = 0; f < NFADE; f++) {                    /* (0.17) a free fader, else the quietest's */
        if (!fader[f].on) {
            best = f;
            break;
        }
        if (fader[f].amp_now < fader[best].amp_now)
            best = f;
    }
    fader[best] = *v;
    fader[best].stolen = 1;
    synth_fader(&fader[best], v, best);              /* (0.14: a BRAIDS voice's oscillator copied) */
}

/* mono modes: the track's one voice */
static int mono_voice(int trk)
{
    int lo, hi, i;
    track_range(trk, &lo, &hi);
    for (i = lo; i < hi; i++)
        if (vc[i].on && vc[i].trk == trk)
            return i;
    return -1;
}

static uint32_t voice_on1(int trk, int note, int vel, const plock_t *lk, int nl, int key, int ft, int lt, int porta,
                          const sound_t *s, uint32_t nid)
{
    pattern_t *p = eng_pat;
    int pm = s->s[S_PLAYMODE], uni = p && p->trk[trk].unison > 1 ? p->trk[trk].unison : 1, u;
    if ((pm == PM_MONO || pm == PM_MONO_LEG) && s->s[S_ENGINE] != ENG_DR) {
        int i = mono_voice(trk), legato = 0;
        voice_t *v;
        if (i >= 0 && vc[i].eng != s->s[S_ENGINE]) {   /* (0.17) another engine: the old note fades in a fader */
            steal(&vc[i]);
            vc[i].on = 0;
        } else if (i >= 0)
            legato = pm == PM_MONO_LEG && vc[i].gate;
        if (i < 0) {
            i = pick_voice(trk, note);
            if (i < 0)
                return 0;
            steal(&vc[i]);
            vc[i].on = 0;
        }
        v = &vc[i];
        voice_start(v, trk, note, vel, s, lk, nl, key, v->on ? (legato ? 2 : 1) : 0, ft, lt, porta);
        v->wait = strum_wait;
        v->age = ++age_ctr;
        v->nid = nid;
        v->uni_cents = 0;
        v->uni_pan = 0;
        return nid;
    }
    if (s->s[S_ENGINE] == ENG_DR) {                  /* DRUM [TONIC]: a pad takes its own voice again; a closed hat
                                                      * chokes the open ones */
        int pad = pad_of(note), i = -1, k, md = s->s[S_DM1 + pad], lo, hi;
        voice_t *v;
        track_range(trk, &lo, &hi);
        for (k = lo; k < hi; k++)
            if (vc[k].on && !vc[k].stolen && vc[k].trk == trk && vc[k].eng == ENG_DR) {
                if (vc[k].dr_pad == pad)
                    i = k;
                else if ((md == DM_HHC || md == DM_H909C || md == DM_H606) &&
                         (vc[k].dr_model == DM_HHO || vc[k].dr_model == DM_H909O))
                    voice_kill(&vc[k]);
            }
        if (i < 0)
            i = pick_voice(trk, note);
        if (i < 0)
            return 0;
        v = &vc[i];
        if (v->on)
            steal(v);
        v->on = 0;
        voice_start(v, trk, note, vel, s, lk, nl, key, 0, ft, lt, 0);
        v->wait = strum_wait;
        v->age = ++age_ctr;
        v->nid = nid;
        v->uni_cents = 0;
        v->uni_pan = 0;
        return nid;
    }
    for (u = 0; u < uni; u++) {                      /* unison: voices a note, detuned and spread [MANUAL 10.5] */
        int i = pick_voice(trk, note), retrig;
        voice_t *v;
        float spread = p ? (float)p->trk[trk].uspread * (1.0f / 127.0f) : 0, x;
        if (i < 0)
            return 0;
        v = &vc[i];
        retrig = v->on && v->trk == trk && v->note == note && p && p->reuse && uni == 1;
        if (v->on && !retrig)
            steal(v);
        if (!retrig)
            v->on = 0;
        voice_start(v, trk, note, vel, s, lk, nl, key, retrig, ft, lt, porta);
        v->wait = strum_wait;
        v->age = ++age_ctr;
        v->nid = nid;
        x = uni > 1 ? (float)u / (float)(uni - 1) * 2.0f - 1.0f : 0.0f;
        v->uni_cents = x * spread * 25.0f;           /* [APPROX] up to +-25 cents, */
        v->uni_pan = x * spread * 60.0f;             /* and across the stereo field */
    }
    return nid;
}

uint32_t eng_voice_on(int trk, int note, int vel, const plock_t *lk, int nl, int key, int ft, int lt, int porta,
                      const sound_t *snd)
{
    uint8_t n1 = (uint8_t)note;
    return eng_notes_on(trk, &n1, 1, vel, lk, nl, key, ft, lt, porta, snd);
}

/* notes started as one [TONIC 0.8]: one id; STRUM spreads their starts (low to high, high to low, or each time the
 * other way), the voices waiting their turn */
static uint8_t strum_alt[NTRK];
uint32_t eng_notes_on(int trk, const uint8_t *notes, int n, int vel, const plock_t *lk, int nl, int key, int ft, int lt,
                      int porta, const sound_t *snd)
{
    pattern_t *p = eng_pat;
    uint32_t nid = ++nid_ctr ? nid_ctr : ++nid_ctr;
    int t, i, dir = 0;
    float step = 0.0f;
    if (!p)
        return 0;
    if (!snd)
        snd = &p->snd[trk];
    ts[trk].snd = snd;
    if (duck_src == trk + 1) {                       /* (a kit: its PAD, or every pad) */
        int hit = !duck_pad || snd->s[S_ENGINE] != ENG_DR;
        for (i = 0; i < n && !hit; i++)
            hit = pad_of(notes[i]) == duck_pad - 1;
        if (hit)
            duck_trig();
    }
    if (n > 1 && snd->s[S_CHSTRM]) {                 /* STRUM: 0..100 ms a note, in control blocks */
        step = (float)snd->s[S_CHSTRM] * 0.8f * 0.001f * SR / CRF;
        dir = snd->s[S_CHSDIR] == SD_DOWN ? 1 : snd->s[S_CHSDIR] == SD_ALT ? (strum_alt[trk] ^= 1) : 0;
    }
    for (i = 0; i < n; i++) {
        int note = scale_snap(snd, notes[i]);       /* (0.12: in the sound's scale, if it has one) */
        if (step > 0.0f) {                           /* the notes' order by pitch: their turn */
            int j, r = 0;
            for (j = 0; j < n; j++)
                r += dir ? notes[j] > note || (notes[j] == note && j < i) : notes[j] < note || (notes[j] == note && j < i);
            strum_wait = (uint16_t)((float)r * step + 0.5f);
        }
        if (eng_midi_only >> trk & 1u) {             /* a MIDI track: the note goes out, no voice; the note-off at
                                                      * its length, as a voice's would */
            if (eng_midi_out && eng_midi_chan[trk] < 16)
                eng_midi_out((uint8_t)(0x90 | eng_midi_chan[trk]), (uint8_t)note, (uint8_t)vel);
            midi_pend_add(trk, note, nid);
            continue;
        }
        voice_on1(trk, note, vel, lk, nl, key, ft, lt, porta, snd, nid);
        if (eng_midi_out && eng_midi_chan[trk] < 16) {   /* (0.17) its note-off by the note's id, as a MIDI track's:
                                                      * a mono retrig or a stolen voice no longer loses it */
            eng_midi_out((uint8_t)(0x90 | eng_midi_chan[trk]), (uint8_t)note, (uint8_t)vel);
            midi_pend_add(trk, note, nid);
        }
        for (t = 0; t < NTRK; t++)                   /* LAYER: the same note on the layered tracks, their sounds */
            if (t != trk && (p->trk[trk].layer >> t & 1u))
                voice_on1(t, note, vel, 0, 0, 0, ft, lt, porta, &p->snd[t], nid);
    }
    strum_wait = 0;
    return nid;
}

/* a note as its sound's chord (CHORD set; not a kit, whose notes are pads) [TONIC 0.8] */
static int chordful(const sound_t *s)            /* (0.12: with a scale, its own chord by degrees) */
{
    return s && s->s[S_ENGINE] != ENG_DR && (s->s[S_SCALE] ? s->s[S_SCHD] != DCH_OFF : s->s[S_ARPCHD] != ACH_OFF);
}
uint32_t eng_chord_on(int trk, int note, int vel, const plock_t *lk, int nl, int key, int ft, int lt, int porta,
                      const sound_t *snd)
{
    pattern_t *p = eng_pat;
    const sound_t *s = snd ? snd : p ? &p->snd[trk] : 0;
    uint8_t ch[NCHORD];
    int n;
    if (!p || !chordful(s))
        return eng_voice_on(trk, note, vel, lk, nl, key, ft, lt, porta, snd);
    n = chord_notes(s, note, p->trk[trk].kscale, p->trk[trk].kroot, ch);
    return eng_notes_on(trk, ch, n, vel, lk, nl, key, ft, lt, porta, snd);
}

/* MIDI tracks' notes sounding outside (their note-offs by id, as voices') */
static struct { uint32_t nid; uint8_t trk, note; } mpend[24];
static void midi_pend_add(int trk, int note, uint32_t nid)
{
    int i, j = 0;
    for (i = 0; i < 24; i++)
        if (!mpend[i].nid) {
            j = i;
            break;
        }
    if (mpend[j].nid && eng_midi_out && eng_midi_chan[mpend[j].trk] < 16)   /* full: the oldest ends */
        eng_midi_out((uint8_t)(0x80 | eng_midi_chan[mpend[j].trk]), mpend[j].note, 0);
    mpend[j].nid = nid;
    mpend[j].trk = (uint8_t)trk;
    mpend[j].note = (uint8_t)note;
}

void eng_note_end(uint32_t nid)
{
    int i;
    for (i = 0; i < 24; i++)
        if (mpend[i].nid == nid) {
            if (eng_midi_out && eng_midi_chan[mpend[i].trk] < 16)
                eng_midi_out((uint8_t)(0x80 | eng_midi_chan[mpend[i].trk]), mpend[i].note, 0);
            mpend[i].nid = 0;
        }
    for (i = 0; i < NVOICE; i++)
        if (vc[i].on && vc[i].nid == nid && vc[i].gate)
            voice_release(&vc[i]);
}
/* (0.17) STOP: every note still out ends, and All Notes Off (CC 123) on each channel in use */
static void midi_all_off(void)
{
    int i, t;
    uint32_t sent = 0;
    for (i = 0; i < 24; i++)
        if (mpend[i].nid) {
            if (eng_midi_out && eng_midi_chan[mpend[i].trk] < 16)
                eng_midi_out((uint8_t)(0x80 | eng_midi_chan[mpend[i].trk]), mpend[i].note, 0);
            mpend[i].nid = 0;
        }
    for (t = 0; t < NTRK; t++)
        if (eng_midi_out && eng_midi_chan[t] < 16 && !(sent >> eng_midi_chan[t] & 1u)) {
            sent |= 1u << eng_midi_chan[t];
            eng_midi_out((uint8_t)(0xB0 | eng_midi_chan[t]), 123, 0);
        }
}

void eng_voice_off(int trk, int note)
{
    int i;
    for (i = 0; i < 24; i++)
        if (mpend[i].nid && mpend[i].trk == trk && mpend[i].note == note) {
            if (eng_midi_out && eng_midi_chan[trk] < 16)
                eng_midi_out((uint8_t)(0x80 | eng_midi_chan[trk]), (uint8_t)note, 0);
            mpend[i].nid = 0;
        }
    for (i = 0; i < NVOICE; i++)
        if (vc[i].on && vc[i].gate && vc[i].note == note && (vc[i].trk == trk || (eng_pat && (eng_pat->trk[trk].layer >> vc[i].trk & 1u))))
            voice_release(&vc[i]);
}

void eng_lock_trig(int trk, const plock_t *lk, int nl, int key) { synth_lock(trk, lk, nl, key); }

/* ------------------------------------------------------------- keys and MIDI notes --- */
/* the keys held per track (mono note priority, legato), in the order pressed */
static struct { uint8_t n[16]; uint8_t c; uint32_t nid[16]; uint8_t how[16]; } held[NTRK];
enum { HK_VOICE, HK_ARP, HK_CHORD };   /* how a held key sounded when pressed: its release does the same [0.9.2] */

/* live recording: a note held now, written when released */
typedef struct { uint8_t on, trk, note, vel, step; int8_t micro; float start; uint32_t pass; } recnote_t;
static recnote_t recn[16];
#define RQN 32
static rec_ev_t rq[RQN];
static volatile uint32_t rq_w, rq_r;
static void rec_put(const rec_ev_t *e)
{
    if (rq_w - rq_r < RQN) {
        rq[rq_w % RQN] = *e;
        __asm__ volatile("" ::: "memory");
        rq_w++;
    }
}
int engine_rec_get(rec_ev_t *e)
{
    if (rq_r == rq_w)
        return 0;
    *e = rq[rq_r % RQN];
    rq_r++;
    return 1;
}

/* where a note played now lands: its step and micro timing on the track */
static void rec_where(int trk, int *step, int *micro)
{
    pattern_t *p = eng_pat;
    float pos = seq_pos(trk), sw = (float)p->swing * 0.48f - 24.0f, best = 1e9f;
    int L = p->trk[trk].len ? p->trk[trk].len : 16, s, bs = 0;
    for (s = (int)(pos / 24.0f) - 1; s <= (int)(pos / 24.0f) + 1; s++) {
        int sm = ((s % L) + L) % L;
        float e = (float)s * 24.0f + ((sm & 1) ? sw : 0.0f), d = pos - e;
        if (fm_fabsf(d) < fm_fabsf(best)) {
            best = d;
            bs = s;
        }
    }
    *step = ((bs % L) + L) % L;
    *micro = eng.rec_quant ? 0 : (int)clampi((int)(best + (best >= 0 ? 0.5f : -0.5f)), -23, 23);
    /* a trig placed ahead of the playhead in this pass would sound twice: skip it once */
    if (best < 0)
        seq_skip(trk, *step);
}

static void key_on(int trk, int note, int vel)
{
    pattern_t *p = eng_pat;
    const sound_t *s;
    int i, pm;
    if (!p)
        return;
    s = &p->snd[trk];
    ts[trk].snd = s;
    pm = s->s[S_PLAYMODE];
    if (held[trk].c >= 16)                           /* (all 16 held: the key is not followed) */
        return;
    held[trk].n[held[trk].c] = (uint8_t)note;
    held[trk].nid[held[trk].c] = 0;
    held[trk].how[held[trk].c] = (uint8_t)(arp_on(trk) ? HK_ARP : chordful(s) ? HK_CHORD : HK_VOICE);
    held[trk].c++;
    if (eng.rec_live && eng.playing) {               /* live recording [MANUAL 10.2.5] */
        for (i = 0; i < 16; i++)
            if (!recn[i].on) {
                int st, mi;
                rec_where(trk, &st, &mi);
                recn[i].on = 1;
                recn[i].trk = (uint8_t)trk;
                recn[i].note = (uint8_t)note;
                recn[i].vel = (uint8_t)vel;
                recn[i].step = (uint8_t)st;
                recn[i].micro = (int8_t)mi;
                recn[i].start = seq_pos(trk);
                recn[i].pass = seq_pass(trk);
                break;
            }
    }
    if (arp_on(trk)) {
        arp_hold(trk, note, vel, -1.0f);
        return;
    }
    if ((pm == PM_MONO || pm == PM_MONO_LEG) && s->s[S_MONOPRIO] && held[trk].c > 1) {
        /* LOW / HIGH priority: a new key plays only if it wins */
        int win = note;
        for (i = 0; i < held[trk].c; i++)
            if (s->s[S_MONOPRIO] == 1 ? held[trk].n[i] < win : held[trk].n[i] > win)
                win = held[trk].n[i];
        if (win != note)
            return;
    }
    {
        int porta = p->trk[trk].tp[P_PORT - NSP] ? p->trk[trk].tp[P_PTIM - NSP] : 0;
        held[trk].nid[held[trk].c - 1] = eng_chord_on(trk, note, vel, 0, 0, 0, p->trk[trk].tp[P_FLTT - NSP],
                                                      p->trk[trk].tp[P_LFOT - NSP], porta, s);
    }
}

static void key_off(int trk, int note)
{
    pattern_t *p = eng_pat;
    const sound_t *s = p ? &p->snd[trk] : &init_snd;
    int i, j, pm = s->s[S_PLAYMODE], was_last = held[trk].c && held[trk].n[held[trk].c - 1] == note;
    for (i = 0; i < 16; i++)                         /* live recording: the note is complete */
        if (recn[i].on && recn[i].trk == trk && recn[i].note == note) {
            rec_ev_t e;
            float len = seq_pos(trk) - recn[i].start;
            int li = 0;
            if (len < 0)
                len += (float)(p->trk[trk].len ? p->trk[trk].len : 16) * 24.0f;
            while (li < LEN_INF - 1 && (float)LEN_TICKS[li + 1] * 0.25f <= len)
                li++;
            e.trk = (uint8_t)trk;
            e.note = (uint8_t)note;
            e.vel = recn[i].vel;
            e.step = recn[i].step;
            e.micro = recn[i].micro;
            e.len = (uint8_t)li;
            e.kind = 0;
            e.val = 0;
            e.pass = recn[i].pass;
            rec_put(&e);
            recn[i].on = 0;
        }
    {   /* the key as it was pressed [0.9.2]: an arp's note, a chord (all its notes), or a voice */
        int how = -1;
        uint32_t nid = 0;
        for (i = 0; i < held[trk].c; i++)
            if (held[trk].n[i] == note) {
                how = held[trk].how[i];
                nid = held[trk].nid[i];
                for (j = i; j + 1 < held[trk].c; j++) {
                    held[trk].n[j] = held[trk].n[j + 1];
                    held[trk].nid[j] = held[trk].nid[j + 1];
                    held[trk].how[j] = held[trk].how[j + 1];
                }
                held[trk].c--;
                break;
            }
        if (how == HK_ARP) {
            arp_free(trk, note);
            return;
        }
        if (how == HK_CHORD) {
            if (nid)
                eng_note_end(nid);                   /* all its notes, and their MIDI note-offs */
            return;
        }
        if (how < 0 && arp_on(trk))                  /* (not followed: as before) */
            arp_free(trk, note);
    }
    if ((pm == PM_MONO || pm == PM_MONO_LEG) && held[trk].c && (was_last || s->s[S_MONOPRIO])) {
        /* mono: back to the key that now has priority (legato in MONO LEG.) */
        int back = held[trk].n[held[trk].c - 1];
        if (s->s[S_MONOPRIO])
            for (i = 0; i < held[trk].c; i++)
                if (s->s[S_MONOPRIO] == 1 ? held[trk].n[i] < back : held[trk].n[i] > back)
                    back = held[trk].n[i];
        if (back != note) {
            int vi = mono_voice(trk);
            if (vi >= 0 && vc[vi].note == note) {
                if (eng_midi_out && eng_midi_chan[trk] < 16)   /* the released note ends outside too [0.9.2] */
                    eng_midi_out((uint8_t)(0x80 | eng_midi_chan[trk]), (uint8_t)note, 0);
                eng_voice_on(trk, back, vc[vi].vel, 0, 0, 0, p->trk[trk].tp[P_FLTT - NSP],
                             p->trk[trk].tp[P_LFOT - NSP], p->trk[trk].tp[P_PORT - NSP] ? p->trk[trk].tp[P_PTIM - NSP] : 0, s);
                return;
            }
        }
    }
    eng_voice_off(trk, note);
    if (eng_midi_out && eng_midi_chan[trk] < 16)
        eng_midi_out((uint8_t)(0x80 | eng_midi_chan[trk]), (uint8_t)note, 0);
}

void engine_rec_param(int trk, int param, int val)
{
    rec_ev_t e;
    int st, mi;
    if (!eng.rec_live || !eng.playing || !eng_pat)
        return;
    rec_where(trk, &st, &mi);
    e.trk = (uint8_t)trk;
    e.note = (uint8_t)param;
    e.step = (uint8_t)st;
    e.kind = 1;
    e.val = (int16_t)val;
    e.vel = 0;
    e.micro = 0;
    e.len = 0;
    e.pass = seq_pass(trk);
    rec_put(&e);
}

static void q_drain(void)
{
    int i;
    while (q_r != q_w) {
        qev_t e = q[q_r % QN];
        q_r++;
        switch (e.type) {
        case Q_ON: key_on(e.trk, e.a, e.b); break;
        case Q_OFF: key_off(e.trk, e.a); break;
        case Q_MOD:
            if (e.a == MS_PB) ts[e.trk].pb = (float)e.b * (1.0f / 8192.0f);   /* (0.24: only the bend, by PBDEP) */
            break;
        case Q_ALLOFF: {
            for (i = 0; i < NVOICE; i++)
                if (vc[i].on && (e.trk == 255 || vc[i].trk == e.trk))
                    voice_release(&vc[i]);
            if (e.trk == 255)
                for (i = 0; i < NTRK; i++)
                    held[i].c = 0;
            break;
        }
        case Q_PARAM: synth_param(e.trk, e.a, e.b); break;
        case Q_SOUND: synth_sound(e.trk); break;
        case Q_START:                                /* (0.17) the transport in the render, not under its feet */
            seq_start();
            break;
        case Q_STOP:
            seq_release_all();
            midi_all_off();
            for (i = 0; i < NVOICE; i++)
                if (vc[i].on)
                    voice_release(&vc[i]);
            for (i = 0; i < NTRK; i++)
                held[i].c = 0;
            break;
        case Q_PANIC: {
            for (i = 0; i < NVOICE; i++)
                vc[i].on = 0;
            for (i = 0; i < NFADE; i++)
                fader[i].on = 0;
            break;
        }
        }
    }
}

/* ----------------------------------------------------------------------- transport --- */
/* (0.17) the sequencer's state is the render's: PLAY and STOP go through the queue (PLAY's first, so the render
 * never runs a block playing without the start) */
void engine_play(void)
{
    if (eng.playing)
        return;
    qput(Q_START, 0, 0, 0);
    eng.playing = 1;
    if (eng_clock_cb)
        eng_clock_cb(1);
}
void engine_stop(void)
{
    if (eng.playing && eng_clock_cb)
        eng_clock_cb(2);
    eng.playing = 0;
    eng.rec_live = 0;
    qput(Q_STOP, 0, 0, 0);
}
void engine_stop_hard(void)
{
    engine_stop();
    qput(Q_PANIC, 0, 0, 0);
    fx_clear();
}
void engine_restart(void) { qput(Q_START, 0, 0, 0); }
void engine_cue(pattern_t *next)
{
    if (!eng.playing) {                              /* stopped: at once */
        eng_pat = next;
        seq_cue(0);
        return;
    }
    seq_cue(next);
}
pattern_t *engine_cued(void) { return seq_cued(); }
int engine_took_cue(void) { return seq_took_cue(); }
void engine_cue_how(int how) { seq_cue_how(how); }
void engine_at_change(int mute, int stop, int bpm10) { seq_at_change(mute, stop, bpm10); }
int engine_step(int trk) { return seq_step(trk); }
uint32_t engine_loops(void) { return seq_loops(); }
uint32_t engine_passes(void) { return seq_ends(); }
uint32_t engine_cpu_samples(void) { return cpu_samples; }
const sound_t *engine_sound(int trk) { return eng_pat ? &eng_pat->snd[trk] : &init_snd; }
int engine_voices_busy(int trk)
{
    int i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += vc[i].on && (trk < 0 || vc[i].trk == trk);
    return n;
}
float engine_level(int trk) { return meters[trk]; }
static int newest(int trk)
{
    int i, best = -1;
    for (i = 0; i < NVOICE; i++)
        if (vc[i].on && vc[i].trk == trk && (best < 0 || vc[i].age > vc[best].age))
            best = i;
    return best;
}
static float master_peak;
float engine_master_level(void) { return master_peak; }
int engine_env_view(int trk, int which, float *lvl)
{
    int v = newest(trk);
    const env_t *e;
    if (v < 0) { *lvl = 0; return 0; }
    e = which == 0 ? &vc[v].ea : which == 1 ? &vc[v].eb : which == 2 ? &vc[v].ef : which == 4 ? &vc[v].em : &vc[v].eamp;
    *lvl = e->lvl;
    return e->stage;
}
float engine_pad_level(int trk, int pad)
{
    int i;
    float m = 0;
    for (i = 0; i < NVOICE; i++)
        if (vc[i].on && !vc[i].stolen && vc[i].trk == trk && vc[i].eng == ENG_DR && vc[i].dr_pad == pad &&
            vc[i].dr_env > m)
            m = vc[i].dr_env;
    return m;
}
float engine_lfo_phase(int trk, int lfo) { int v = newest(trk); return v < 0 ? -1.0f : vc[v].lfo[lfo].ph; }
int engine_filter_view(int trk, float *fc, float *res)
{
    int v = newest(trk);
    if (v < 0 || vc[v].c_fc < -1e8f) return 0;
    *fc = vc[v].c_fc;
    *res = vc[v].c_res;
    return 1;
}
float engine_lfo_view(int trk, int lfo)
{
    int i, best = -1;
    for (i = 0; i < NVOICE; i++)
        if (vc[i].on && vc[i].trk == trk && (best < 0 || vc[i].age > vc[best].age))
            best = i;
    return best < 0 ? 0 : vc[best].lfo[lfo].val;
}

/* MIDI clock: the tempo from the clocks' spacing (24 a quarter), start / stop */
static uint32_t clk_last, clk_n;
static float clk_avg;
static volatile uint8_t rt_start, rt_stop;            /* (0.19) Start / Continue (1 / 2), Stop: for the render */
static void clock_in_tick(void)
{
    uint32_t now = eng_now;
    if (clk_n && now > clk_last) {                   /* (0.17) the intervals averaged, not the tempos */
        float iv = (float)(now - clk_last) * CRF * INV_SR;
        if (clk_avg <= 0.0f || iv < clk_avg * 4.0f)  /* (0.19: a gap, a stop between: not a tempo) */
            clk_avg = clk_avg > 0.0f ? clk_avg + (iv - clk_avg) * 0.08f : iv;
        {
            float bpm = 60.0f / (clk_avg * 24.0f);
            if (bpm > 20.0f && bpm < 400.0f)
                eng.bpm = fm_floorf(bpm * 10.0f + 0.5f) * 0.1f;
        }
    }
    clk_last = now;
    clk_n++;
    if (eng.playing || rt_start)                     /* (a clock right after Start counts, before the render took it) */
        seq_clk_rx++;
}
void engine_clock_rt(int what)
{
    if (!eng.ext_clock)
        return;
    switch (what) {
    case 0: clock_in_tick(); break;
    case 1: seq_clk_rx = 0; rt_stop = 0; rt_start = 1; break;   /* (the next clock is the first step's) */
    case 2: if (!eng.playing) rt_start = 2; break;
    default: rt_start = 0; rt_stop = 1; break;
    }
}
/* (0.19) the clock's Start / Continue / Stop, at the render's block (the sequencer is the render's) */
static void clock_in_transport(void)
{
    if (rt_stop) {
        int k;
        rt_stop = 0;
        if (eng.playing) {
            eng.playing = 0;
            eng.rec_live = 0;
            seq_release_all();
            midi_all_off();
            for (k = 0; k < NVOICE; k++)
                if (vc[k].on)
                    voice_release(&vc[k]);
            if (eng_clock_cb)
                eng_clock_cb(2);
        }
    }
    if (rt_start) {
        int c = rt_start;
        rt_start = 0;
        if (c == 1)
            seq_start();
        eng.playing = 1;
        if (eng_clock_cb && c == 1)
            eng_clock_cb(1);
    }
}

/* ------------------------------------------------------------------------ init --- */
void engine_init(void *pool, uint32_t pool_bytes)
{
    uint32_t used = 0;
    int t;
    synth_init(pool, &used);
    used = (used + 3u) & ~3u;
    c1_buf = (float *)((uint8_t *)pool + used);      /* (0.16) the second core's voices' buffers */
    used += ENG_DUAL_BYTES;
    fx_init(pool, &used, pool_bytes);
    seq_init();
    for (t = 0; t < NTRK; t++) {
        ts[t].snd = &init_snd;
        ts[t].level = 1.0f;
        ts[t].last_note = -1;
    }
}

void engine_panic(void) { qput(Q_PANIC, 0, 0, 0); }

/* overload: (0.23, after mono2k's CPU guard) the voice heard least goes, faded in ~3 ms: its amp (envelope x level) x
 * its track's level, a released one counted at half (a muted track's voices first); among equals the oldest. The
 * screen names the dearest track (its voices' measured render time), the one to lighten */
/* (0.21, after Hortator's plan to degrade before shedding) an overload first puts the inserts at the base rate for
 * three seconds (eng.ins_lq bit 1, as INSHQ OFF does); only an overload while they are there costs a voice */
static uint32_t ins_shed_n;
void engine_shed(void)
{
    int i, best = -1;
    float hb = 0.0f;
    uint32_t tc[NTRK] = {0};
    if (!eng.ins_lq) {
        for (i = 0; i < NVOICE; i++)
            if (vc[i].on && vc[i].in_typ && vc[i].in_typ != INS_CRUSH && vc[i].in_typ != INS_OVER)
                break;
        if (i < NVOICE) {                            /* (an insert at 2x is playing: that first) */
            eng.ins_lq |= 2u;
            ins_shed_n = 3u * SR;
            return;
        }
    }
    for (i = 0; i < NVOICE; i++)
        if (vc[i].on && !vc[i].stolen) {
            int t = vc[i].trk & (NTRK - 1);
            float h = fm_fabsf(vc[i].amp_now) * ts[t].level * (vc[i].gate ? 1.0f : 0.5f);
            tc[t] += vc[i].cost ? vc[i].cost : 400u;
            if (best < 0 || h < hb || (h == hb && vc[i].age < vc[best].age)) {
                best = i;
                hb = h;
            }
        }
    if (best < 0)
        return;
    voice_kill(&vc[best]);
    for (i = 1, eng.shed_trk = 0; i < NTRK; i++)
        if (tc[i] > tc[eng.shed_trk])
            eng.shed_trk = (uint8_t)i;
    eng.shed_n++;
}

/* ------------------------------------------------------------------------ render --- */
static uint32_t dual_bad0;
void engine_dual(const eng_dual_t *d) { dual = d && c1_buf ? d : 0; dual_bad0 = dual && dual->bad ? dual->bad() : 0; }
/* (0.17) the second core given up: one core from here on, and the app told */
static void dual_off(void)
{
    const eng_dual_t *d = dual;
    dual_lost++;
    dual = 0;
    if (d && d->gave_up)
        d->gave_up();
}
/* (0.17) a job's voice as the second core reads it: one of the twelve, else none (a word read wrong) */
static inline int dual_ok(const voice_t *v) { return v >= vc && v < vc + NVOICE; }
typedef char eng_dual_cr_check[CR == 32 ? 1 : -1];   /* (ENG_DUAL_BYTES counts 32 samples a block) */
int engine_dual_on(void) { return dual != 0; }
uint32_t engine_dual_lost(void) { return dual_lost; }
static inline uint32_t vcost(const voice_t *v) { return v->cost ? v->cost : 400u; }   /* (a new one: a guess) */
/* the second core's job: its n voices, each into its own five buffers from zero, timed */
static void dual_job(uint32_t n)
{
    uint32_t k, i;
    for (k = 0; k < n; k++) {
        float *b = c1_buf + k * 5u * CR;
        voice_t *v = c1_v[k];
        uint32_t t0 = dual->now(), dt, nz;
        if (!dual_ok(v)) {
            c1_snd[k] = 0;
            for (i = 0; i < 2u * CR; i++)
                b[i] = 0.0f;
            continue;
        }
        /* (0.17) without sends the voice adds only to L and R (voice_block): its send buffers stay out, neither
         * cleared nor added (adding +0 changes no sum: the same bits) */
        c1_snd[k] = v->snd_c * 2.0f + v->snd_d * 2.0f + v->snd_r * 2.0f > 0.0f;
        nz = c1_snd[k] ? 5u * CR : 2u * CR;
        for (i = 0; i < nz; i++)
            b[i] = 0.0f;
        ENG_FAR(voice_block)(v, b, b + CR, b + 2 * CR, b + 3 * CR, b + 4 * CR);
        dt = dual->now() - t0;
        v->cost = (uint16_t)(dt > 65535u ? 65535u : dt ? dt : 1u);
    }
}
/* the control step: the sounding voices' second half on the second core (each voice's own state, the scratch per
 * core; the BRAIDS delay buffers after it, on core 0, in the voices' order: the same with one core or two) */
static void dual_ctl_job(uint32_t n)
{
    uint32_t k;
    for (k = 0; k < n; k++)
        if (dual_ok(c1_v[k]))
            synth_control_voice(c1_v[k], 1);
}
static void control_block(int c1_free)
{
    int on[NVOICE], non = 0, split, k, i;
    if (!dual || !c1_free) {
        synth_control();
        return;
    }
    for (i = 0; i < NVOICE; i++)
        if (vc[i].on)
            on[non++] = i;
    split = non >= 2 ? (non + 1) / 2 : non;
    for (k = split; k < non; k++)
        c1_v[k - split] = &vc[on[k]];
    if (split < non)
        dual->give(dual_ctl_job, (uint32_t)(non - split));
    for (k = 0; k < split; k++)
        synth_control_voice(&vc[on[k]], 0);
    if (split < non && dual->wait(5000u) < 0) {      /* not in time: held, one core from here on */
        dual_off();
        for (k = split; k < non; k++)
            synth_control_voice(&vc[on[k]], 0);
    }
    synth_control_end();
}
/* the voices of a block: on one core in their order; with the second, a run at the end goes there (as much of the
 * cost as leaves both about even), core 0 renders the rest meanwhile, then adds the second core's buffers in the
 * voices' order: each voice's samples added once, after the ones before it, as on one core */
static void voices_block(float *dl, float *dr, float *sc, float *sd, float *sr, float *m)
{
    int on[NVOICE], non = 0, split, k, i;
    for (i = 0; i < NVOICE; i++)
        if (vc[i].on)
            on[non++] = i;
    split = non;
    if (dual && non >= 2) {
        uint32_t tot = 0, suf = 0;
        for (k = 0; k < non; k++)
            tot += vcost(&vc[on[k]]);
        while (split > 1 && non - split < ENG_DUAL_MAXV) {
            uint32_t c = vcost(&vc[on[split - 1]]);
            if (2u * suf + c > tot)
                break;
            suf += c;
            split--;
        }
        if (split < non) {
            for (k = split; k < non; k++)
                c1_v[k - split] = &vc[on[k]];
            dual->give(dual_job, (uint32_t)(non - split));
        }
    }
    for (k = 0; k < split; k++) {
        voice_t *v = &vc[on[k]];
        if (dual) {
            uint32_t t0 = dual->now(), dt;
            ENG_FAR(voice_block)(v, dl, dr, sc, sd, sr);   /* (0.13: in RAM) */
            dt = dual->now() - t0;
            v->cost = (uint16_t)(dt > 65535u ? 65535u : dt ? dt : 1u);
        } else
            ENG_FAR(voice_block)(v, dl, dr, sc, sd, sr);
    }
    if (split < non) {
        if (dual->wait(5000u) == 0) {
            for (k = 0; k < non - split; k++) {
                const float *__restrict bl = c1_buf + (uint32_t)k * 5u * CR, *__restrict br = bl + CR;
                float *__restrict ol = dl, *__restrict orr = dr;
                for (i = 0; i < CR; i++) {
                    ol[i] += bl[i];
                    orr[i] += br[i];
                }
                if (c1_snd[k]) {
                    const float *__restrict bc = bl + 2 * CR, *__restrict bd = bl + 3 * CR, *__restrict bv = bl + 4 * CR;
                    float *__restrict oc = sc, *__restrict od = sd, *__restrict ov = sr;
                    for (i = 0; i < CR; i++) {
                        oc[i] += bc[i];
                        od[i] += bd[i];
                        ov[i] += bv[i];
                    }
                }
            }
        } else {                                     /* not in time: held, and from here on one core */
            dual_off();
            for (k = split; k < non; k++)
                ENG_FAR(voice_block)(&vc[on[k]], dl, dr, sc, sd, sr);
        }
        if (dual && dual->bad && dual->bad() != dual_bad0)   /* (0.17) it read a job wrong: one core from the next */
            dual_off();
    }
    for (k = 0; k < non; k++)
        if (vc[on[k]].meter > m[vc[on[k]].trk])
            m[vc[on[k]].trk] = vc[on[k]].meter;
}

/* (0.17) a block's second half: its effect settings, the effects, the compressor, the metronome, the output. With
 * the second core it runs there while core 0 already makes the next block's first half (the sequencer and the
 * control step): each block's own settings (taken in its first half) and buffers, the same arithmetic in the same
 * order, so the same bits; the last block of a call is finished before the call returns */
typedef struct {
    float dl[CR], dr[CR], sc[CR], sd[CR], sr[CR];
    int16_t fx[NFXP];
    float bpm, duck, tape, g, thr;
    float gt;                                    /* (0.23) the sequencer's tick (GATE in time), -1 stopped */
    uint16_t pfb;                                /* (0.23) PERFORM's second bank held */
    int16_t cp[4];
    uint8_t has_fx, freeze, click, met, click_vol;
    uint32_t cnt;
    int32_t *lr;
} fxjob_t;
static fxjob_t fj[2];
static uint8_t pfb_busy;                             /* (0.23) the second bank still sounding or giving the line back */
static void post_block(fxjob_t *j)
{
    float *dl = j->dl, *dr = j->dr;
    int i;
    if (j->has_fx) {                                 /* the block's effect settings, then the effects */
        fx_freeze(j->freeze);
        fx_params(j->fx, j->bpm);
        fx_duck(j->duck);
    }
    fx_throw(j->thr);                                /* (0.18) THROW */
    if (j->pfb || pfb_busy)                          /* (0.23) PERFORM's second bank: the shimmer's feed */
        pfb_pre(dl, dr, j->sr, CR, j->pfb);
    ENG_FAR(fx_block)(dl, dr, j->sc, j->sd, j->sr, CR);
    if (j->pfb || pfb_busy)                          /* and the captures, the gate */
        pfb_busy = (uint8_t)pfb_post(dl, dr, CR, j->pfb, j->bpm, j->gt);
    if (j->cp[0] > 0) {   /* the master compressor [TONIC 0.6]: a peak detector a block, the gain in dB; AMT moves
                         * the threshold (0 .. -30 dB) and the ratio (1:1 .. 6:1) together, half the reduction a
                         * 0 dB peak would get comes back as make-up; ATK 0.5 .. 32 ms, REL 20 ms .. 1.3 s; MIX
                         * blends in the compressed signal (parallel compression). The gain glides over the block */
        float a = (float)j->cp[0] * (1.0f / 127.0f), thr = -30.0f * a, ratio = 1.0f + 5.0f * a, pk = 0, lv, tgt, g, gd;
        float mix = (float)j->cp[3] * (1.0f / 127.0f), mk = -thr * (1.0f - 1.0f / ratio) * 0.5f;
        for (i = 0; i < CR; i++) {
            float x = fm_maxf(fm_fabsf(dl[i]), fm_fabsf(dr[i]));
            if (x > pk) pk = x;
        }
        lv = 6.0206f * fm_log2f(pk * 0.8545f + 1e-9f);   /* (at the output's level) */
        tgt = lv > thr ? (lv - thr) * (1.0f - 1.0f / ratio) : 0.0f;
        {
            float tm = tgt > cp_gr ? 0.0005f * fm_exp2f((float)j->cp[1] * (6.0f / 127.0f))
                                   : 0.02f * fm_exp2f((float)j->cp[2] * (6.0f / 127.0f));
            cp_gr += (tgt - cp_gr) * (1.0f - fm_expf(-CRF * INV_SR / tm));
        }
        g = fm_db2lin(mk - cp_gr);
        g = 1.0f + mix * (g - 1.0f);
        gd = (g - cp_g) * (1.0f / CRF);
        for (i = 0; i < CR; i++) {
            cp_g += gd;
            dl[i] *= cp_g;
            dr[i] *= cp_g;
        }
        cp_g = g;
    } else {                                     /* (0.17) off: the gain glides back to 1 over the block */
        cp_gr = 0.0f;
        if (cp_g != 1.0f) {
            float gd = (1.0f - cp_g) * (1.0f / CRF);
            for (i = 0; i < CR; i++) {
                cp_g += gd;
                dl[i] *= cp_g;
                dr[i] *= cp_g;
            }
            cp_g = 1.0f;
        }
    }
    {   /* the metronome [MANUAL 10.7]: a click on each beat, higher on the bar's first */
        static float cph, cenv, cinc;
        if (j->click) {
            if (j->met) {
                cenv = 0.5f * (float)(j->click_vol ? j->click_vol : 90) * (1.0f / 127.0f);
                cinc = (j->click == 2 ? 1760.0f : 1320.0f) * INV_SR;
                cph = 0;
            }
        }
        if (cenv > 0.0005f) {
            for (i = 0; i < CR; i++) {
                float c = lks(cph) * cenv;
                cph += cinc;
                if (cph >= 1.0f)
                    cph -= 1.0f;
                cenv *= 0.9985f;
                dl[i] += c;
                dr[i] += c;
            }
        }
    }
    for (i = 0; i < (int)j->cnt; i++) {
        /* TONIC's output gain (0.855: +2.9 dB before the OS's pattern volume law, which is unity at PVOL 100),
         * then a soft knee from -2 dBFS: dense chords round off instead of clipping */
        float l = dl[i] * 0.8545f, r = dr[i] * 0.8545f, a;
        a = fm_fabsf(l);
        if (a > 0.8f) {
            float t = (a - 0.8f) * 5.0f;
            a = 0.8f + 0.2f * t / (1.0f + t);
            l = l < 0 ? -a : a;
        }
        a = fm_fabsf(r);
        if (a > 0.8f) {
            float t = (a - 0.8f) * 5.0f;
            a = 0.8f + 0.2f * t / (1.0f + t);
            r = r < 0 ? -a : a;
        }
        if (j->tape < 0.25f) {                   /* TAPE: the last of it fades as the tape stops */
            l *= j->tape * 4.0f;
            r *= j->tape * 4.0f;
        }
        j->lr[2 * i] = (int32_t)(l * j->g);
        j->lr[2 * i + 1] = (int32_t)(r * j->g);
        if (fm_fabsf(l) > master_peak) master_peak = fm_fabsf(l);
        if (fm_fabsf(r) > master_peak) master_peak = fm_fabsf(r);
    }
    master_peak *= 0.985f;
}
static void dual_post_job(uint32_t k) { post_block(&fj[k & 1u]); }

void engine_render(int32_t *lr, uint32_t n, uint32_t gain_q12)
{
    float g = (float)gain_q12 * (1.0f / 4096.0f) * 8388607.0f;
    uint32_t done = 0;
    int cur = 0, pend = 0;                           /* pend: the other slot's second half runs on core 1 */
    pattern_t *p;
    if (ins_shed_n) {                                /* (0.21) the overload's three seconds at the base rate */
        ins_shed_n = ins_shed_n > n ? ins_shed_n - n : 0;
        if (!ins_shed_n)
            eng.ins_lq &= (uint8_t)~2u;
    }
    while (done < n) {
        fxjob_t *j = &fj[cur];
        int i, t;
        eng_ofs = done;                              /* (0.23) where in the render a MIDI message is due */
        j->has_fx = 0;
        clock_in_transport();                        /* (0.19) MIDI clock's Start / Stop */
        q_drain();
        seq_block();
        if (seq_take_stop()) {                       /* (0.17) song END STOP: at the change point itself */
            int k;
            if (eng_clock_cb)
                eng_clock_cb(2);
            eng.playing = 0;
            eng.rec_live = 0;
            seq_release_all();
            midi_all_off();
            for (k = 0; k < NVOICE; k++)
                if (vc[k].on)
                    voice_release(&vc[k]);
        }
        p = eng_pat;
        if (p) {
            int16_t fx[NFXP];
            seq_fx_all(p, fx);
            {   /* the performance effects [TONIC 0.7] */
                uint16_t pf = eng.pfx;
                float half = pf >> PFX_HALF & 1u ? 0.5f : 1.0f;
                /* TAPE: the speed falls to a stop in 0.8 s, evenly as a platter runs down (the pitch ever faster), and
                 * comes back in 0.2 s */
                if (pf >> PFX_TAPE & 1u)
                    tape_s = tape_s - CRF * INV_SR / 0.8f;
                else if (tape_s < 1.0f)
                    tape_s = fm_minf(tape_s + CRF * INV_SR / 0.2f, 1.0f);
                if (tape_s < 0.0f) tape_s = 0.0f;
                eng_speed = half * tape_s;
                eng_pitch = tape_s >= 1.0f ? 0.0f : 12.0f * fm_log2f(fm_maxf(tape_s, 0.0157f));   /* (down to -72) */
                /* LP / HP: the DJ filter sweeps to its end in ~1.5 s while held, and jumps back when let go */
                if (pf >> PFX_LP & 1u)
                    sweep = fm_maxf(sweep - 64.0f * CRF * INV_SR / 1.5f, -64.0f);
                else if (pf >> PFX_HP & 1u)
                    sweep = fm_minf(sweep + 63.0f * CRF * INV_SR / 1.5f, 63.0f);
                else
                    sweep = 0.0f;
                if (sweep != 0.0f)
                    fx[P_MFLT - P_CDEP] = (int16_t)clampi(fx[P_MFLT - P_CDEP] + (int)sweep, -64, 63);
                /* FREEZE: the reverb holds (DEC INF, the treble kept), no new input */
                if (pf >> PFX_FREEZE & 1u) {
                    fx[P_RDEC - P_CDEP] = 127;
                    fx[P_RGAI - P_CDEP] = 127;
                }
                j->freeze = (uint8_t)(pf >> PFX_FREEZE & 1u);
                /* (0.23) the second bank's DUB: a throw into the delay, its feedback up and its band narrowed; SHIM:
                 * the reverb long and heard */
                if (eng.pfxb >> PFB_DUB & 1u) {
                    fx[P_DFB - P_CDEP] = (int16_t)(fx[P_DFB - P_CDEP] > 165 ? fx[P_DFB - P_CDEP] : 165);
                    fx[P_DHPF - P_CDEP] = (int16_t)(fx[P_DHPF - P_CDEP] > 56 ? fx[P_DHPF - P_CDEP] : 56);
                    fx[P_DLPF - P_CDEP] = (int16_t)(fx[P_DLPF - P_CDEP] < 84 ? fx[P_DLPF - P_CDEP] : 84);
                    fx[P_DVOL - P_CDEP] = (int16_t)(fx[P_DVOL - P_CDEP] > 100 ? fx[P_DVOL - P_CDEP] : 100);
                }
                if (eng.pfxb >> PFB_SHIM & 1u) {
                    fx[P_RDEC - P_CDEP] = (int16_t)(fx[P_RDEC - P_CDEP] > 116 ? fx[P_RDEC - P_CDEP] : 116);
                    fx[P_RVOL - P_CDEP] = (int16_t)(fx[P_RVOL - P_CDEP] > 100 ? fx[P_RVOL - P_CDEP] : 100);
                    fx[P_RGAI - P_CDEP] = (int16_t)(fx[P_RGAI - P_CDEP] > 96 ? fx[P_RGAI - P_CDEP] : 96);
                }
            }
            for (i = 0; i < NFXP; i++)                   /* (0.17) the effects' settings for this block's effects */
                j->fx[i] = fx[i];
            j->bpm = eng.bpm;
            j->has_fx = 1;
            cp_amt = fx[P_CPAMT - P_CDEP];
            cp_atk = fx[P_CPATK - P_CDEP];
            cp_rel = fx[P_CPREL - P_CDEP];
            cp_mix = fx[P_CPMIX - P_CDEP];
            {   /* ducking: gain 1 - DPTH x env on the other tracks (and the returns with FX ON); REL is the time
                 * constant, 10 ms .. 1.3 s; the level ramp below gives it a ~3 ms attack */
                float dg;
                duck_src = clampi(fx[P_SCSRC - P_CDEP], 0, NTRK + 5);
                duck_pad = clampi(fx[P_SCPAD - P_CDEP], 0, NPAD);
                duck_keep = clampi(fx[P_SCKEEP - P_CDEP], 0, NTRK);
                duck_rise = fx[P_SCATK - P_CDEP] > 0 ?                  /* ATK: 2 .. 250 ms */
                            CRF * INV_SR / (0.002f + (float)fx[P_SCATK - P_CDEP] * (0.248f / 127.0f)) : 0.0f;
                if (fx[P_SCREL - P_CDEP] != duck_rel) {
                    duck_rel = fx[P_SCREL - P_CDEP];
                    duck_k = fm_expf(-CRF * INV_SR / (0.01f * fm_exp2f((float)duck_rel * (7.0f / 127.0f))));
                }
                if (duck_src > NTRK) {                   /* [0.10] the tempo's divisions, from PLAY (and on while
                                                          * stopped) */
                    float div = DUCK_DIV[duck_src - NTRK - 1], gt = seq_gtick();
                    int32_t bi = (int32_t)(gt / div);
                    if (bi != duck_bi) {
                        duck_bi = bi;
                        duck_trig();
                    }
                    duck_ph = (gt - (float)bi * div) / div;
                    duck_len = div * (1.0f / 384.0f) * (240.0f / fm_maxf(eng.bpm, 20.0f));
                } else {
                    duck_bi = -1;
                    duck_ph = -1.0f;
                }
                if (!duck_src)
                    duck_up = 0;
                if (duck_up) {                           /* ATK: the dip comes in */
                    duck_env += duck_rise;
                    if (duck_env >= 1.0f) {
                        duck_env = 1.0f;
                        duck_up = 0;
                    }
                }
                dg = duck_src ? 1.0f - (float)fx[P_SCDEP - P_CDEP] * (1.0f / 127.0f) * duck_env : 1.0f;
                duck_g = dg;
                if (!duck_up)
                    duck_env *= duck_k;
                j->duck = duck_src && fx[P_SCFX - P_CDEP] ? dg : 1.0f;
                for (t = 0; t < NTRK; t++) {
                    float lvl = trk_gain((float)seq_level(p, t));
                    float tgt = (eng.mute >> t & 1u) || (eng.solo && !(eng.solo >> t & 1u)) ? 0.0f : lvl;   /* (0.18: solo) */
                    if (duck_src && t != duck_src - 1 && t != duck_keep - 1)
                        tgt *= dg;
                    ts[t].level = fm_flush(ts[t].level + (tgt - ts[t].level) * 0.25f);   /* mutes and levels ramp (~3 ms) */
                }
            }
        }
        j->cp[0] = (int16_t)cp_amt;
        j->cp[1] = (int16_t)cp_atk;
        j->cp[2] = (int16_t)cp_rel;
        j->cp[3] = (int16_t)cp_mix;
        j->click = seq_click;                        /* (taken here: the next block's seq_block may set it again) */
        seq_click = 0;
        j->met = eng.metronome;
        j->click_vol = eng.click_vol;
        j->tape = tape_s;
        j->thr = eng.pfxb >> PFB_DUB & 1u ? fm_maxf(eng.thr, 0.8f) : eng.thr;   /* (0.18) THROW (0.23: DUB's) */
        j->pfb = eng.pfxb;
        j->gt = eng.playing ? seq_gtick() : -1.0f;
        j->g = g;
        j->lr = lr + 2u * done;
        j->cnt = n - done < CR ? n - done : CR;
        control_block(!pend);                        /* (core 1 busy with the last block's effects: on core 0) */
        if (pend) {                                  /* the last block's effects done before the voices */
            pend = 0;
            if (dual && dual->wait(5000u) < 0) {
                dual_off();
                post_block(&fj[cur ^ 1]);            /* (not in time: again here, one glitch, one core from now) */
            }
        }
        for (i = 0; i < CR; i++)
            j->dl[i] = j->dr[i] = j->sc[i] = j->sd[i] = j->sr[i] = 0;
        {
            float m[NTRK] = {0};
            voices_block(j->dl, j->dr, j->sc, j->sd, j->sr, m);
            for (t = 0; t < NTRK; t++)
                meters[t] = m[t] > meters[t] ? m[t] : meters[t] * 0.97f;
        }
        for (i = 0; i < NFADE; i++)                  /* the stolen voices' last few ms */
            if (fader[i].on) {
                fader[i].amp_tgt *= 0.4f;
                ENG_FAR(voice_block)(&fader[i], j->dl, j->dr, j->sc, j->sd, j->sr);
                if (fader[i].amp_now < 0.0005f)
                    fader[i].on = 0;
            }
        if (dual) {
            dual->give(dual_post_job, (uint32_t)cur);
            pend = 1;
        } else
            post_block(j);
        cur ^= 1;
        done += CR;
    }
    if (pend && dual->wait(5000u) < 0) {
        dual_off();
        post_block(&fj[cur ^ 1]);
    }
    cpu_samples += n;
}
