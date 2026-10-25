/* SPDX-License-Identifier: MPL-2.0
 * TONIC factory sounds and the demo project: original sounds (not the Digitone's factory content). Each sound is
 * the init sound plus a list of (parameter, value) in stored units (q 4 parameters: display x 4). Part of app.c. */

#define X4(v) ((int16_t)((v) * 4))
#define X16(v) ((int16_t)((v) * 16))
/* ratios as constants: x 4 (every ratio is a multiple of 0.25); B as B1 x 400 + B2 x 4. preset_apply finds the
 * list indices */
#define RA(r) ((int16_t)((r) * 4))
#define RC(r) ((int16_t)((r) * 4))
#define RB(b1, b2) ((int16_t)((b1) * 4) * 100 + (int16_t)((b2) * 4))
#define END -1
#define SETUP 200                 /* then (S_* index, value) pairs */
#define MOD 201                   /* then (source, slot, destination, depth) */
#define ARP 202                   /* then mode, speed, range, length */
#define NOBASE 203                /* first, then 0: the defaults alone, not the factory base */
/* a DRUM kit: the engine, then its 8 pads' models; pad n's TUNE DEC TONE LEV */
#define KIT(a, b, c, d, e, f, g, h) SETUP, S_ENGINE, ENG_DR, SETUP, S_DM1, a, SETUP, S_DM2, b, SETUP, S_DM3, c, \
    SETUP, S_DM4, d, SETUP, S_DM5, e, SETUP, S_DM6, f, SETUP, S_DM7, g, SETUP, S_DM8, h
#define PAD(n, tun, dec, ton, lev) DR_TUN(n), tun, DR_DEC(n), dec, DR_TON(n), ton, DR_LEV(n), lev

static int rc_idx(float r)
{
    int i;
    for (i = 0; i < NRATC; i++)
        if (RATIO_C[i] >= r - 0.01f)
            return i;
    return NRATC - 1;
}
static int ra_idx(float r)
{
    int i;
    for (i = 0; i < NRATA; i++)
        if (RATIO_A[i] >= r - 0.01f)
            return i;
    return NRATA - 1;
}

typedef struct {
    const char *name;
    const int16_t *kv;
} preset_t;

#define GROUP(n)
#define PRESET(id, nm, ...) static const int16_t KV_##id[] = {__VA_ARGS__, END};
#include "presets.inc"
#undef PRESET
#define PRESET(id, nm, ...) {nm, KV_##id},
static const preset_t FACTORY[] = {
#include "presets.inc"
};
#undef PRESET
#undef GROUP
/* the groups in the factory list's order: a name, then 0 for each sound in it */
#define GROUP(n) n,
#define PRESET(id, nm, ...) 0,
static const char *const PSEQ[] = {
#include "presets.inc"
};
#undef PRESET
#undef GROUP
#define NFACTORY ((int)(sizeof FACTORY / sizeof FACTORY[0]))

static void preset_apply(sound_t *s, const preset_t *pr)
{
    /* the factory sounds were made on TONIC's earlier defaults (before INIT took the OS's): those, on the current
     * curves, are their base */
    static const int16_t BASE[][2] = {
        {P_ADEC, 9}, {P_AEND, 0}, {P_BDEC, 9}, {P_BEND, 0}, {P_PHRT, 0}, {P_FDEC, 20}, {P_FSUS, 127},
        {P_FREL, 20}, {P_EATK, 5}, {P_EDEC, 15}, {P_ESUS, 127}, {P_EREL, 7}, {P_VOL, 508},
        {P_L1SPD, 192}, {P_L1MUL, 2}, {P_L2SPD, 192}, {P_L2MUL, 2},
    };
    const int16_t *k = pr->kv;
    int i;
    sound_init(s);
    if (*k == NOBASE)
        k += 2;
    else
        for (i = 0; i < (int)(sizeof BASE / sizeof BASE[0]); i++)
            s->p[BASE[i][0]] = BASE[i][1];
    while (*k != END) {
        if (*k == SETUP) {
            if (k[1] == S_ENGINE)                    /* the VA engine: its defaults first (put it first) */
                sound_set_engine(s, k[2]);
            else
                s->s[k[1]] = (uint8_t)k[2];
            k += 3;
        } else if (*k == MOD) {
            s->mdst[k[1]][k[2]] = (uint8_t)k[3];
            s->mdep[k[1]][k[2]] = (int8_t)k[4];
            k += 5;
        } else if (*k == ARP) {
            s->s[S_ARPMODE] = (uint8_t)k[1];
            s->s[S_ARPSPD] = (uint8_t)k[2];
            s->s[S_ARPRNG] = (uint8_t)k[3];
            s->s[S_ARPLEN] = (uint8_t)k[4];
            k += 5;
        } else {
            int v = k[1];
            if (s->s[S_ENGINE] != ENG_FM)
                ;                                    /* VA, DRUM: the values as they are */
            else if (*k == P_RATC)
                v = rc_idx((float)v * 0.25f);
            else if (*k == P_RATA)
                v = ra_idx((float)v * 0.25f);
            else if (*k == P_RATB)
                v = rc_idx((float)(v / 100) * 0.25f) * NRATB + rc_idx((float)(v % 100) * 0.25f);
            if (*k < NSP)
                s->p[*k] = (int16_t)clamp(v, param_desc(s->s[S_ENGINE], *k)->lo, param_desc(s->s[S_ENGINE], *k)->hi);
            k += 2;
        }
    }
    for (i = 0; i < NAMELEN - 1 && pr->name[i]; i++)
        s->name[i] = pr->name[i];
    s->name[i] = 0;
}

/* factory sound i's group: its name, and its first sound */
static const char *preset_group(int i, int *first)
{
    int k, n = -1, g0 = 0;
    const char *g = "";
    for (k = 0; k < (int)(sizeof PSEQ / sizeof PSEQ[0]); k++) {
        if (PSEQ[k]) {
            g = PSEQ[k];
            g0 = n + 1;
        } else if (++n == i)
            break;
    }
    if (first)
        *first = g0;
    return g;
}

static int preset_find(const char *name)
{
    int i, j;
    for (i = 0; i < NFACTORY; i++) {
        for (j = 0; FACTORY[i].name[j] && FACTORY[i].name[j] == name[j]; j++)
            ;
        if (!FACTORY[i].name[j] && !name[j])
            return i;
    }
    return 0;
}
static void sound_factory(sound_t *s, const char *name) { preset_apply(s, &FACTORY[preset_find(name)]); }

/* ------------------------------------------------------------------- the demo --- */
static void tr_note(pattern_t *p, int t, int s, int note, int vel, int len)
{
    trig_t *g = &p->trig[t][s];
    g->flags = TF_NOTE;
    g->note = (uint8_t)note;
    g->vel = (uint8_t)vel;
    g->len = (uint8_t)(len < 0 ? 0xFF : len);
    g->nnote = 1;
}
static void tr_chord(pattern_t *p, int t, int s, int root, int a, int b, int c, int len)
{
    trig_t *g = &p->trig[t][s];
    tr_note(p, t, s, root, 0, len);
    g->nnote = (uint8_t)(c ? 4 : b ? 3 : 2);
    g->chord[0] = (int8_t)a;
    g->chord[1] = (int8_t)b;
    g->chord[2] = (int8_t)c;
}

/* LEN indices (core/tables.c LEN_TICKS): 0.5 step = 6, 1 = 14, 2 = 30, 3 = 38, 4 = 46, 8 = 62, 16 = 78 */
enum { L_HALF = 6, L_1 = 14, L_2 = 30, L_3 = 38, L_4 = 46, L_8 = 62, L_16 = 78 };

static void demo_pool(void)
{
    static const char *const P[16] = {"BD ROUND", "SD FM", "HH CLOSED", "HH OPEN", "CLAP NOISE", "TOM LOW",
                                      "RIM", "COWBELL", "BD HARD", "SD ROLL", "PERC BLIP", "ZAP",
                                      "BELL HIT", "STAB", "FM BASS", "CHORD STAB"};
    int i;
    for (i = 0; i < 16; i++)
        sound_factory(&pool[i], P[i]);
}

/* A01: four tracks: drums on T1 with sound locks, hats with conditions on T2, a locked bass on T3, chords on T4 */
/* the demo patterns were mixed on TONIC's earlier effect defaults (before they took the OS's) */
static void demo_init(pattern_t *p)
{
    static const int16_t FXB[][2] = {
        {P_CDEP, 48}, {P_CSPD, 24}, {P_CHPF, 20}, {P_CWID, 100}, {P_DWID, 63}, {P_DFB, 60}, {P_DHPF, 0},
        {P_DLPF, 127}, {P_DVOL, 100}, {P_RPRE, 8}, {P_RDEC, 60}, {P_RFRQ, 96}, {P_RGAI, 80}, {P_RHPF, 16},
        {P_RLPF, 112}, {P_RVOL, 100},
    };
    int i;
    pattern_init(p);
    for (i = 0; i < (int)(sizeof FXB / sizeof FXB[0]); i++)
        p->fx[FXB[i][0] - P_CDEP] = FXB[i][1];
}

static void demo_a01(pattern_t *p)
{
    int s;
    static const uint8_t BASS[16] = {36, 0, 36, 48, 0, 36, 0, 39, 36, 0, 43, 36, 0, 41, 39, 0};
    demo_init(p);
    memcpy(p->name, "GROUND", 7);
    p->bpm10 = 1180;
    p->swing = 56;
    sound_factory(&p->snd[0], "BD ROUND");
    sound_factory(&p->snd[1], "HH CLOSED");
    sound_factory(&p->snd[2], "FM BASS");
    sound_factory(&p->snd[3], "SOFT KEYS");
    for (s = 0; s < 16; s += 4)
        tr_note(p, 0, s, 48, 112, L_1);
    tr_note(p, 0, 4, 48, 104, L_1);
    lock_set(p, 0, 4, P_SOUND, 1);                   /* the snare is a sound lock on the kick's track */
    tr_note(p, 0, 12, 48, 108, L_1);
    lock_set(p, 0, 12, P_SOUND, 1);
    tr_note(p, 0, 14, 48, 70, L_1);
    p->trig[0][14].cond = C_PROB0 + 10;              /* 50 %: a ghost kick */
    tr_note(p, 0, 15, 50, 60, L_1);
    lock_set(p, 0, 15, P_SOUND, 6);                  /* a rim */
    p->trig[0][15].cond = C_FILL;
    for (s = 0; s < 16; s++) {
        tr_note(p, 1, s, 60, (s & 1) ? 70 : 100, L_HALF);
        if (s % 4 == 2)
            p->trig[1][s].micro = 3;                 /* laid back */
    }
    p->trig[1][7].cond = C_AB0 + 0;                  /* 1:2 */
    p->trig[1][15].cond = C_N1ST;
    lock_set(p, 1, 6, P_SOUND, 3);                   /* an open hat */
    tr_note(p, 1, 6, 60, 90, L_2);
    for (s = 0; s < 16; s++)
        if (BASS[s]) {
            tr_note(p, 2, s, BASS[s], s % 4 ? 90 : 118, s == 3 ? L_HALF : L_1);
            if (s % 3 == 1)
                lock_set(p, 2, s, P_FREQ, X4(55 + s * 3));   /* the filter walks */
        }
    lock_set(p, 2, 10, P_ALEV, 100);
    tr_chord(p, 3, 0, 60, 3, 7, 10, L_3);
    tr_chord(p, 3, 6, 58, 4, 7, 11, L_2);
    p->trig[3][6].micro = -6;
    tr_chord(p, 3, 10, 56, 3, 7, 12, L_3);
    p->trk[3].tp[P_TLVL - NSP] = 84;
    p->fx[P_DTIM - P_CDEP] = 24 * 48;                /* a dotted 8th */
    p->fx[P_DFB - P_CDEP] = 70;
    p->snd[3].p[P_DEL] = X4(40);
    p->snd[3].p[P_REV] = X4(50);
}

/* A02: an arpeggio over held chords, a pad, a slow kick */
static void demo_a02(pattern_t *p)
{
    demo_init(p);
    memcpy(p->name, "ARPEGGIO", 9);
    p->bpm10 = 1240;
    sound_factory(&p->snd[0], "BD ROUND");
    sound_factory(&p->snd[1], "GLASS ARP");
    sound_factory(&p->snd[2], "SUB BASS");
    sound_factory(&p->snd[3], "WARM PAD");
    tr_note(p, 0, 0, 48, 115, L_1);
    tr_note(p, 0, 8, 48, 110, L_1);
    tr_note(p, 0, 11, 48, 70, L_1);
    p->trig[0][11].cond = C_AB0 + 1;                 /* 2:2 */
    tr_chord(p, 1, 0, 64, 3, 7, 12, L_8);
    tr_chord(p, 1, 8, 62, 5, 8, 12, L_8);
    tr_note(p, 2, 0, 40, 100, L_8);
    tr_note(p, 2, 8, 38, 100, L_8);
    tr_chord(p, 3, 0, 52, 7, 15, 0, L_16);
    p->trk[3].tp[P_TLVL - NSP] = 80;
}

/* A03: polymeter: 16, 12 and 7 steps, half-time bass, conditions */
static void demo_a03(pattern_t *p)
{
    int s;
    static const uint8_t MEL[12] = {72, 0, 75, 77, 0, 79, 0, 82, 79, 0, 77, 75};
    demo_init(p);
    memcpy(p->name, "POLYMETER", 10);
    p->bpm10 = 1260;
    p->lenmode = 1;
    p->mlen = 0;                                     /* INF */
    p->chlen = 16;
    sound_factory(&p->snd[0], "MARIMBA");
    sound_factory(&p->snd[1], "DX BELL");
    sound_factory(&p->snd[2], "ACID");
    sound_factory(&p->snd[3], "METAL HIT");
    p->trk[1].len = 12;
    p->trk[2].len = 7;
    p->trk[3].len = 16;
    p->trk[3].scale = 2;                             /* 1/2X */
    for (s = 0; s < 16; s++)
        if (s % 3 != 2)
            tr_note(p, 0, s, 60 + (s % 5) * 2, 90, L_HALF);
    for (s = 0; s < 12; s++)
        if (MEL[s])
            tr_note(p, 1, s, MEL[s], 80, L_2);
    p->trig[1][5].cond = C_AB0 + 3;                  /* 2:3 */
    for (s = 0; s < 7; s++)
        tr_note(p, 2, s, s == 3 ? 48 : 36, s == 0 ? 120 : 90, L_HALF);
    lock_set(p, 2, 3, P_FREQ, X4(96));
    lock_set(p, 2, 5, P_RESO, X4(110));
    tr_note(p, 3, 0, 60, 110, L_1);
    tr_note(p, 3, 6, 67, 90, L_1);
    p->trig[3][6].cond = C_PROB0 + 12;               /* 67 % */
}

/* A04: one sound, many locks: a filter and FM sweep written as p-locks over 32 steps */
static void demo_a04(pattern_t *p)
{
    int s;
    demo_init(p);
    memcpy(p->name, "LOCKS", 6);
    p->bpm10 = 1300;
    sound_factory(&p->snd[0], "BD HARD");
    sound_factory(&p->snd[1], "FM BASS");
    sound_factory(&p->snd[2], "HH CLOSED");
    sound_factory(&p->snd[3], "STAB");
    for (s = 0; s < 32; s += 4)
        tr_note(p, 0, s, 48, 120, L_1);
    for (s = 0; s < 32; s++) {
        tr_note(p, 1, s, s % 8 == 6 ? 43 : 36, 100, L_HALF);
        lock_set(p, 1, s, P_FREQ, X4(30 + (s * 3) % 70));
        if (s % 4 == 0)
            lock_set(p, 1, s, P_ALEV, 40 + (s * 5) % 80);
    }
    for (s = 2; s < 32; s += 4)
        tr_note(p, 2, s, 60, 90, L_HALF);
    tr_chord(p, 3, 6, 60, 7, 15, 0, L_1);
    tr_chord(p, 3, 22, 58, 7, 14, 0, L_1);
    p->trig[3][22].micro = 8;
    for (s = 0; s < NTRK; s++)
        p->trk[s].len = 32;
    p->mlen = 32;
}

/* the demo project (first: the first power-on, which also fills the library; SAVE > LOAD DEMO keeps the library and
 * replaces the rest of the project: the patterns after A04 empty, no chain, no song) [0.9.2] */
static void demo_write(int first)
{
    static void (*const DEMO[])(pattern_t *) = {demo_a01, demo_a02, demo_a03, demo_a04};
    unsigned i;
    int n;
    for (i = 0; i < NDIRTY; i++)                     /* nothing of the old project is written over the demo */
        dirty[i].len = 0;
    cur_dirty = 0;
    next_idx = -1;
    demo_pool();
    bank_save(OBJ_POOL, pool, 16);
    pool_dirty = 0;
    for (i = 0; i < NPAT; i++) {
        if (i < sizeof DEMO / sizeof DEMO[0])
            DEMO[i](pnext);
        else if (first)
            break;
        else
            pattern_init(pnext);
        n = pattern_pack(pnext, pack_buf, PLAT_STORE_MAX);
        if (n > 0)
            plat_store_save(OBJ_PAT0 + i, pack_buf, (uint32_t)n);
    }
    if (first) {                                     /* the user library starts as a copy of factory sounds */
        for (i = 0; i < 32; i++)
            preset_apply(&lib[i], &FACTORY[i % NFACTORY]), lib[i].name[0] = i < 8 ? lib[i].name[0] : 0;
        lib_dirty = 1;
    }
    set.demo = 1;
    set.cur = 0;
    set.chain_n = 0;
    set.song_on = 0;
    set.song_n = 0;
    set_dirty = 1;
    cur_idx = 0;
    store_flush();
}

/* first power-on (or an empty project): write the demo; demo_write() alone overwrites (SAVE > LOAD DEMO) */
static void demo_check(void)
{
    int i;
    for (i = 0; i < NPAT && !set.demo; i++)          /* patterns in the flash: a project is there, keep it */
        if (plat_store_size(OBJ_PAT0 + (uint32_t)i) > 0) {
            set.demo = 1;
            set_dirty = 1;
        }
    if (!set.demo) {
        demo_write(1);
        store_boot();
    }
}
