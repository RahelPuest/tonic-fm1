/* SPDX-License-Identifier: MPL-2.0
 * TONIC voices: the Digitone's FM engine (4 operators C A B1 B2, 8 algorithms with X/Y outputs, HARM, DTUN,
 * FDBK, MIX, operator envelopes A and B), overdrive, base-width filter, multimode filter, amp envelope, pan,
 * sends, and two LFOs per voice. Control rate CR samples; per-sample work only in voice_block. */
#include "eng_int.h"
#include <stddef.h>

voice_t vc[NVOICE];
float sin_tab[SIN_N + 1];
#define sin_small sin_tab
static int16_t harm_q[NHARM][HARM_N + 1] ENG_LOWRAM;    /* the HARM shapes, x 32767 */
static float (*wt_voice)[HARM_N + 1];        /* each voice's HARM shape (the blend of two tables), in the pool */
/* each voice table's blend: the two shapes and their weights; pend: not written to the table yet (1: set this
 * block, 2: a block old) */
typedef struct { const int16_t *t0; float a, b; uint8_t pend; } harm_t;   /* (t1: t0 + HARM_N + 1) */
static harm_t harm_w[NVOICE];

enum { OC, OA, OB1, OB2 };
static float P_INV[NSP], P_LO[NSP], P_HI[NSP];   /* display units: 1 / q, the range */
static uint8_t P_SMOOTH[NSP];     /* the parameter glides (not a list) */

/* the destinations: names (screen) and the parameter each one moves */
const char *const DEST_NAME[ND] = {
    "NONE",
    "ALGO", "RAT C", "RAT A", "RAT B", "HARM", "DTUN", "FDBK", "MIX",
    "A ATK", "A DEC", "A END", "A LEV", "B ATK", "B DEC", "B END", "B LEV", "A DEL", "B DEL",
    "PITCH", "PIT AB2", "RAT ALL", "AB LEV", "AB ATK", "AB DEC", "AB END", "AB DEL",
    "OFS C", "OFS A", "OFS B1", "OFS B2",
    "FREQ", "RESO", "F ENV", "F ATK", "F DEC", "F SUS", "F REL", "BASE", "WIDTH", "F DEL",
    "ATK", "DEC", "SUS", "REL", "DRIVE", "PAN", "VOL", "REV", "DEL", "CHR",
    "L1 SPD", "L1 MUL", "L1 FADE", "L1 WAVE", "L1 SPH", "L1 MODE", "L1 DEP",
    "SRR", "BRR",
    "---", "---", "---", "---", "---", "---", "---", "---", "---", "---", "---", "---", "---", "---", "---", "---",
    "---", "---", "---", "---",
    "I AMT", "I TONE", "I MIX",
};
const uint8_t DEST_PARAM[ND] = {
    P_NONE,
    P_ALGO, P_RATC, P_RATA, P_RATB, P_HARM, P_DTUN, P_FDBK, P_MIX,
    P_AATK, P_ADEC, P_AEND, P_ALEV, P_BATK, P_BDEC, P_BEND, P_BLEV, P_ADEL, P_BDEL,
    P_NONE, P_NONE, P_NONE, P_NONE, P_NONE, P_NONE, P_NONE, P_NONE,
    P_OFSC, P_OFSA, P_OFSB1, P_OFSB2,
    P_FREQ, P_RESO, P_FENV, P_FATK, P_FDEC, P_FSUS, P_FREL, P_BASE, P_WDTH, P_FDEL,
    P_EATK, P_EDEC, P_ESUS, P_EREL, P_DRV, P_PAN, P_VOL, P_REV, P_DEL, P_CHR,
    P_L1SPD, P_L1MUL, P_L1FAD, P_L1WAV, P_L1SPH, P_L1MOD, P_L1DEP,
    P_SRR, P_BRR,
    P_D5TUN, P_D5DEC, P_D5TON, P_D5LEV, P_D6TUN, P_D6DEC, P_D6TON, P_D6LEV,
    P_D7TUN, P_D7DEC, P_D7TON, P_D7LEV, P_D8TUN, P_D8DEC, P_D8TON, P_D8LEV,
    P_NONE, P_NONE, P_NONE, P_NONE,
    P_IAMT, P_ITON, P_IMIX,
};

/* the VA engine's names for the destinations whose parameters it uses otherwise [TONIC] */
const char *dest_name(int engine, int d)
{
    static const struct { uint8_t d; const char *n; } VAN[] = {
        {D_ALGO, "WAV1"}, {D_RATC, "WAV2"}, {D_RATA, "FOLD"}, {D_RATB, "SUB"}, {D_HARM, "TUNE 2"}, {D_DTUN, "DETN"},
        {D_FDBK, "PW"}, {D_MIX, "MIX"}, {D_ALEV, "PENV"}, {D_BLEV, "MENV"}, {D_PITCHAB2, "PITCH 2"},
        {D_RATALL, "RING"}, {D_ABLEV, "P+M ENV"}, {D_OFSC, "SYNC"}, {D_OFSA, "NOIS"}, {D_OFSB1, "XMOD"},
        {D_OFSB2, "DRFT"}};
    unsigned i;
    if (d < 0 || d >= ND)
        return "?";
    if (engine == ENG_VA)
        for (i = 0; i < sizeof VAN / sizeof VAN[0]; i++)
            if (VAN[i].d == d)
                return VAN[i].n;
    if (engine == ENG_DR && d >= D_ALGO && d <= D_OFSB2 && d != D_PITCH) {   /* DRUM: pads 1-4, else unused */
        static const struct { uint8_t d; const char *n; } DRN[16] = {
            {D_MIX, "1 TUNE"}, {D_AATK, "1 DEC"}, {D_ADEC, "1 TONE"}, {D_AEND, "1 LEV"},
            {D_OFSC, "2 TUNE"}, {D_ALEV, "2 DEC"}, {D_BATK, "2 TONE"}, {D_BDEC, "2 LEV"},
            {D_OFSA, "3 TUNE"}, {D_BEND, "3 DEC"}, {D_BLEV, "3 TONE"}, {D_ADEL, "3 LEV"},
            {D_OFSB1, "4 TUNE"}, {D_BDEL, "4 DEC"}, {D_RATB, "4 TONE"}, {D_OFSB2, "4 LEV"}};
        for (i = 0; i < 16; i++)
            if (DRN[i].d == d)
                return DRN[i].n;
        return "---";
    }
    if (engine == ENG_DR && d >= D_P5TUN && d <= D_KLEV) {   /* DRUM [TONIC 0.9]: pads 5-8, the whole kit */
        static const char *const KN[4] = {"TUNE", "DEC", "TONE", "LEV"};
        static char b[12];
        int k = (d - D_P5TUN) & 3;
        if (d >= D_KTUN) {
            b[0] = 'A'; b[1] = 'L'; b[2] = 'L'; b[3] = ' ';
            for (i = 0; KN[d - D_KTUN][i]; i++) b[4 + i] = KN[d - D_KTUN][i];
            b[4 + i] = 0;
        } else {
            b[0] = (char)('5' + (d - D_P5TUN) / 4); b[1] = ' ';
            for (i = 0; KN[k][i]; i++) b[2 + i] = KN[k][i];
            b[2 + i] = 0;
        }
        return b;
    }
    return DEST_NAME[d];
}

/* ------------------------------------------------------------------------- tables --- */
static void build_harm(int16_t *q, const float *spec, float *t)   /* t: HARM_N + 1 floats of scratch */
{
    int i, k;
    float peak = 0;
    for (i = 0; i <= HARM_N; i++) {
        float x = (float)i / HARM_N, s = 0;
        for (k = 0; k < HARM_PARTIALS; k++)
            if (spec[k] != 0.0f)
                s += spec[k] * fm_sin_turns(x * (float)(k + 1));
        t[i] = s;
        if (fm_fabsf(s) > peak)
            peak = fm_fabsf(s);
    }
    /* [APPROX] each shape at the sine's peak: the timbre changes, the loudness stays about the same */
    for (i = 0; i <= HARM_N; i++)
        q[i] = (int16_t)(t[i] / peak * 32767.0f);
}

void synth_init(void *pool, uint32_t *used)
{
    int i;
    for (i = 0; i <= SIN_N; i++)
        sin_tab[i] = fm_sin_turns((float)i / SIN_N);
    for (i = 0; i < NSP; i++) {
        P_SMOOTH[i] = PDESC[i].fmt != F_LIST && i != P_ALGO && i != P_L1DST && i != P_L2DST && i != P_L1MUL &&
                      i != P_L2MUL && i != P_MDST;
        P_INV[i] = 1.0f / (float)PDESC[i].q;
        P_LO[i] = (float)PDESC[i].lo * P_INV[i];
        P_HI[i] = (float)PDESC[i].hi * P_INV[i];
    }
    wt_voice = (float (*)[HARM_N + 1])((uint8_t *)pool + *used);
    *used += NVOICE * (HARM_N + 1) * sizeof(float);
    for (i = 0; i < NHARM; i++)
        build_harm(harm_q[i], HARM_SPECTRUM[i], wt_voice[0]);   /* (the voices' tables are free until a note) */
    for (i = 0; i < NVOICE; i++) {
        vc[i].wti = (uint8_t)i;
        vc[i].lfo[0].seed = 0x9E3779B9u * (uint32_t)(i + 1);
        vc[i].lfo[1].seed = 0x7F4A7C15u * (uint32_t)(i + 1);
        vc[i].va_ns = 0x2545F491u * (uint32_t)(i + 1);
    }
}

/* --------------------------------------------------------------------- envelopes --- */
/* per-block coefficients of an exponential stage that has fallen by `db` dB after `time` s */
static inline float k_db(float time, float db)
{
    if (time <= 0.0f)
        return 0.0f;
    if (time > 1e8f)
        return 1.0f;
    return fm_exp2f(-db * 0.16609640f * CRF * INV_SR / time);   /* 10^(-db/20 x block/time) */
}
#define CACHE(in, val, out, expr) do { if ((val) != (in)) { (in) = (val); (out) = (expr); } } while (0)

/* operator envelope [FIRMWARE, the DSP's envelope 0x40000d94]: ATK, DEC and the delay are latched when the note
 * starts (op_latch); attack linear to full, then (TRIG) decay or (GATE) full until the key goes up, then decay;
 * decay exponential to END (DEC's time is to -30 dB of the way); END is read live; an idle operator sits at END */
static void op_latch(env_t *e, float del_s, float atk, float dec)
{
    float at = openv_atk(atk);
    e->k_a = at <= 0.0007f ? 1.0f : CRF * INV_SR / at;
    e->k_d = k_db(openv_dec(dec), 30.0f);
    e->t_l = del_s;
}
static void openv_step(env_t *e, float end, int trigged, int gate)
{
    switch (e->stage) {
    case 1:                                       /* delay */
        e->t += CRF * INV_SR;
        if (e->t >= e->t_l) {
            e->stage = 2;
            e->t = 0;
        }
        break;
    case 2:                                       /* attack */
        e->lvl += e->k_a;
        if (e->lvl >= 1.0f) {
            e->lvl = 1.0f;
            e->stage = trigged ? 3 : 4;
        }
        break;
    case 4:                                       /* gated: full while held */
        if (!gate)
            e->stage = 3;
        break;
    case 3:                                       /* decay to END */
        e->lvl = end + (e->lvl - end) * e->k_d;
        break;
    default:                                      /* idle: at END */
        e->lvl = end;
        break;
    }
}

/* the amp and filter envelopes [FIRMWARE, main CPU]: attack linear, decay to SUS and release to 0 exponential
 * (amp: times to -30 dB; filter: to -60 dB); 127 is infinite. The filter's has a delay. */
static void adsr_step(env_t *e, float del, float atk, float dec, float sus, float rel, int gate, int filt)
{
    if (!gate && e->stage >= 1 && e->stage <= 4)
        e->stage = 5;
    switch (e->stage) {
    case 1:
        CACHE(e->in_l, del, e->t_l, flt_del(del));
        e->t += CRF * INV_SR;
        if (e->t >= e->t_l) {
            e->stage = 2;
            e->t = 0;
        }
        break;
    case 2: {
        float at;
        CACHE(e->in_a, atk, e->k_a, (at = filt ? flt_atk(atk) : amp_atk(atk), at <= 0.0007f ? 1.0f : CRF * INV_SR / at));
        e->lvl += e->k_a;
        if (e->lvl >= 1.0f) {
            e->lvl = 1.0f;
            e->stage = 3;
        }
        break;
    }
    case 3:
        CACHE(e->in_d, dec, e->k_d, k_db(filt ? flt_dec(dec) : amp_dec(dec), 60.0f));
        e->lvl = sus + (e->lvl - sus) * e->k_d;
        if (fm_fabsf(e->lvl - sus) < 0.0002f) {
            e->lvl = sus;
            e->stage = 4;
        }
        break;
    case 4:
        e->lvl = sus;
        break;
    case 5:
        CACHE(e->in_r, rel, e->k_r, k_db(filt ? flt_dec(rel) : amp_dec(rel), 60.0f));
        e->lvl *= e->k_r;
        if (e->lvl < 0.00005f) {
            e->lvl = 0;
            e->stage = 0;
        }
        break;
    default:
        break;
    }
}

/* -------------------------------------------------------------------------- LFOs --- */
static inline uint32_t rnd32(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s; }
static inline float rndf(uint32_t *s) { return (float)(int32_t)rnd32(s) * (1.0f / 2147483648.0f); }

/* the waves [FIRMWARE: the OS's LFO wave functions]: TRI SIN SQR SAW bipolar; EXP 2^(-12 ph), unipolar; RMP 0 -> 1
 * over the first half cycle, then 0 */
static float lfo_wave(lfo_t *l, int wave, float ph)
{
    switch (wave) {
    case 0: return ph < 0.25f ? 4 * ph : ph < 0.75f ? 2 - 4 * ph : 4 * ph - 4;   /* TRI, from 0 rising */
    case 1: return lks(ph);                                                        /* SIN */
    case 2: return ph < 0.5f ? 1.0f : -1.0f;                                       /* SQR */
    case 3: return 1.0f - 2.0f * ph;                                               /* SAW, falling */
    case 4: return fm_exp2f(-12.0f * ph);                                         /* EXP */
    case 5: return ph < 0.5f ? 2.0f * ph : 0.0f;                                   /* RMP */
    default: return l->rnd;                                                        /* RND (held per cycle) */
    }
}

/* one control step of an LFO: spd -64..63, mult index (0..23: >= 12 fixed at 120 BPM), returns -1..1 x depth */
static float lfo_step(voice_t *v, int i, float spd, int mult, float fade, int wave, float sph, int mode,
                      float dep)
{
    lfo_t *l = &v->lfo[i];
    float bpm = mult >= NMULT ? 120.0f : eng.bpm, hz, out, w;
    int m = mult >= NMULT ? mult - NMULT : mult;
    /* [MANUAL p.56 table] a cycle is 128 / (SPD x MULT) whole notes */
    hz = spd * (float)LFO_MULT[m < 0 ? 0 : m > NMULT - 1 ? NMULT - 1 : m] * (1.0f / 128.0f) * (bpm / 240.0f);
    if (l->running) {
        float d = hz * CRF * INV_SR, nph = l->ph + d;
        if (mode == 3 || mode == 4) {                /* ONE: one cycle, then it stops; HLF: half a cycle */
            l->held += fm_fabsf(d);
            if (l->held >= (mode == 3 ? 1.0f : 0.5f)) {
                nph = l->ph + (d >= 0 ? 1.0f : -1.0f) * (fm_fabsf(d) - (l->held - (mode == 3 ? 1.0f : 0.5f)));
                if (mode == 3)
                    nph = d >= 0 ? 0.99999f : 0.0f;
                l->running = 0;
            }
        }
        if (nph >= 1.0f || nph < 0.0f) {
            nph -= fm_floorf(nph);
            l->rnd_prev = l->rnd;
            l->rnd = rndf(&l->seed);                 /* RND: a new value each cycle */
        }
        l->ph = nph;
    }
    w = lfo_wave(l, wave, l->ph);
    if (wave == 6 && sph > 0) {                      /* RND: SPH is slew (OS 1.43) */
        float k = 1.0f - sph * (1.0f / 128.0f);
        l->slew += (w - l->slew) * k * k;
        w = l->slew;
    }
    out = mode == 2 ? l->val : w;                    /* HLD: latched at the trig */
    if (mode != 2)
        l->val = w;
    /* [MANUAL] FADE: positive fades out, negative fades in */
    if (fade != 0) {
        float ft = lfo_fade_time(fade), d = CRF * INV_SR / ft;
        l->fade = fm_clampf(l->fade + (fade > 0 ? -d : d), 0, 1);
        out *= l->fade;
    }
    return out * dep * (1.0f / 64.0f);
}

static void lfo_restart(voice_t *v, int i, int mode, float sph, float fade, int wave)
{
    lfo_t *l = &v->lfo[i];
    l->fade = fade > 0 ? 1.0f : fade < 0 ? 0.0f : 1.0f;
    if (mode == 0) {                                 /* FRE: keeps running (and starts, after ONE / HLF or on a
                                                      * new voice) [0.9.2] */
        l->running = 1;
        return;
    }
    if (mode == 2) {                                 /* HLD: runs free, latches its value at the trig */
        l->running = 1;
        l->val = lfo_wave(l, wave, l->ph);
        return;
    }
    l->ph = sph * (1.0f / 128.0f);
    l->held = 0;                                     /* ONE / HLF: the distance travelled */
    l->running = 1;
    if (wave == 6)
        l->rnd = rndf(&l->seed);
}

/* ------------------------------------------------------------ the voice's controls --- */
static inline float pv(const voice_t *v, int p) { return (float)v->p[p] * P_INV[p]; }
static inline float spanof(int p) { return P_HI[p] - P_LO[p]; }
/* [FIRMWARE] an LFO at full depth (DEP 64) moves its destination +-128 of the OS's parameter units: display units,
 * except pitch (semitones), the ratio offsets (1/64 ratio = 15.625 thousandths) and RATIO B (half an index) */
static inline float lfo_unit(int d, int va)
{
    if (va) return 128.0f;                           /* [TONIC] VA: display units (TUNE2: semitones, as PITCH) */
    if (d >= D_OFSC && d <= D_OFSB2) return 2000.0f;
    return d == D_RATB ? 64.0f : 128.0f;
}

/* the value of a parameter with its modulation, clamped to the parameter's range (display units) */
static inline float eff(const voice_t *v, const float *m, int p, int d)
{
    return fm_clampf(v->ps[p] + m[d], P_LO[p], P_HI[p]);
}

/* the VA engine's control step [TONIC]: the oscillators' increments and levels from the SYN pages (display
 * units, with their modulation m), f0 the note's frequency. Pitch: PENV x envelope A on both oscillators, MENV x
 * envelope B and PITCH 2 on OSC2 (up to +48 semitones each, square law); DETN pulls them apart up to 50 cents;
 * DRFT a slow random wander up to 20 cents */
static inline float vclamp(const voice_t *v, const float *m, int p, int d)
{
    const pdesc_t *pd = param_desc(ENG_VA, p);
    return fm_clampf(v->ps[p] + m[d], (float)pd->lo / (float)pd->q, (float)pd->hi / (float)pd->q);
}
static void va_control(voice_t *v, const float *m, float f0)
{
    float pe = vclamp(v, m, V_PENV, D_ALEV) * (1.0f / 127.0f), me = vclamp(v, m, V_MENV, D_BLEV) * (1.0f / 127.0f);
    float dn = vclamp(v, m, V_DETN, D_DTUN) * (1.0f / 127.0f), dr = vclamp(v, m, V_DRFT, D_OFSB2) * (1.0f / 127.0f);
    float x = (vclamp(v, m, V_MIX, D_MIX) + 64.0f) * (1.0f / 127.0f), sub = vclamp(v, m, V_SUB, D_RATB) * (1.0f / 127.0f);
    float ns = vclamp(v, m, V_NOIS, D_OFSA) * (1.0f / 127.0f), s1, s2, lim = 0.45f;
    v->va_w1 = (uint8_t)fm_clampf((float)v->p[V_WAV1] + m[D_ALGO] + 0.5f, 0, NVW - 0.01f);
    v->va_w2 = (uint8_t)fm_clampf((float)v->p[V_WAV2] + m[D_RATC] + 0.5f, 0, NVW - 0.01f);
    v->va_sync = (float)v->p[V_SYNC] + m[D_OFSC] >= 0.5f;   /* (SYNC as a destination) [0.9.2] */
    v->va_pw = 0.5f - 0.45f * vclamp(v, m, V_PW, D_FDBK) * (1.0f / 120.0f);
    v->va_xm = vclamp(v, m, V_XMOD, D_OFSB1) * (1.0f / 127.0f) * 0.5f;
    /* drift: a new target every ~90 ms, glided to */
    if (dr > 0.0f) {
        if (++v->va_dn >= 128u) {
            v->va_ns = v->va_ns * 1664525u + 1013904223u;
            v->va_dtgt = (float)(int32_t)v->va_ns * (1.0f / 2147483648.0f);
            v->va_dn = 0;
        }
        v->va_drift += (v->va_dtgt - v->va_drift) * 0.01f;
    }
    s1 = 48.0f * pe * pe * v->ea.lvl - 0.25f * dn * dn + 0.2f * dr * v->va_drift;
    s2 = s1 + 0.5f * dn * dn - 0.4f * dr * v->va_drift + vclamp(v, m, V_TUNE2, D_HARM) + 48.0f * me * me * v->eb.lvl +
         m[D_PITCHAB2];
    v->va_inc[0] = fm_minf(f0 * fm_exp2f(s1 * (1.0f / 12.0f)) * INV_SR, lim);
    v->va_inc[1] = fm_minf(f0 * fm_exp2f(s2 * (1.0f / 12.0f)) * INV_SR, lim);
    v->va_inc[2] = 0.5f * v->va_inc[0];
    /* levels: a crossfade of the two, the sub and the noise (square law); x 0.3 sits near the FM engine's level */
    v->va_g1 = 0.3f * (1.0f - x);
    v->va_g2 = 0.3f * x;
    v->va_gs = 0.3f * sub * sub;
    v->va_gn = 0.3f * ns * ns;
    {   /* (0.6) FOLD: the mix driven up to 9x into a triangle folder (LFO: the FOLD destination, RAT A's place);
         * RING: OSC1 x OSC2 added (LFO: RING, RAT ALL's place). 0: off, the samples as before */
        float fo = fm_clampf(v->ps[V_FOLD] + m[D_RATA], 0.0f, 63.0f) * (1.0f / 63.0f);
        float rg = fm_clampf(v->ps[V_RING] + m[D_RATALL], 0.0f, 63.0f) * (1.0f / 63.0f);
        v->va_fold = fo > 0.0f ? 1.0f + 8.0f * fo * fo : 0.0f;
        v->va_ring = 0.3f * rg;
    }
}

/* the DRUM engine's control step [TONIC]: the pad's TUNE DEC TONE LEV (pads 1-4 with their modulation), the
 * pitch offset po (semitones: OCT, PITCH, bend), then the model's frequencies, envelope factors and filters.
 * TUNE: half semitones; DEC scales the model's decay 1/9 .. 9 x (2^((DEC - 64) / 20)); TONE is the model's own */
static const uint8_t DRD[4][4] = {
    {D_MIX, D_AATK, D_ADEC, D_AEND}, {D_OFSC, D_ALEV, D_BATK, D_BDEC}, {D_OFSA, D_BEND, D_BLEV, D_ADEL},
    {D_OFSB1, D_BDEL, D_RATB, D_OFSB2}};
/* per model: base frequency (Hz), decay to -60 dB (s), output gain */
static const float DM_HZ[NDM] = {49, 55, 52, 180, 185, 480, 1100, 205.3f, 205.3f, 262, 115, 230, 540, 2400, 4000,
                                 80, 1, 1600, 1220, 6500, 140, 41, 50, 62, 160, 220, 2200, 340, 340, 3100, 7000};
static const float DM_T60[NDM] = {0.9f, 0.45f, 0.5f, 0.2f, 0.18f, 0.045f, 0.3f, 0.07f, 0.6f, 1.8f, 0.5f, 0.22f,
                                  0.35f, 0.05f, 0.12f, 0.25f, 0.4f, 0.22f, 0.25f, 0.045f, 0.55f, 1.3f, 0.5f, 0.3f, 0.3f,
                                  0.16f, 0.3f, 0.09f, 0.75f, 0.14f, 0.06f};
static const float DM_GAIN[NDM] = {0.5f, 0.5f, 0.5f, 0.55f, 0.55f, 0.55f, 1.1f, 2.0f, 2.0f, 1.4f, 0.5f, 0.45f, 0.6f,
                                   0.5f, 1.0f, 0.4f, 0.6f, 1.0f, 1.5f, 1.0f, 0.5f, 0.5f, 0.6f, 0.5f, 0.55f, 0.5f, 1.9f,
                                   1.1f, 1.1f, 0.55f, 0.6f};
static inline float drv(const voice_t *v, const float *m, int pad, int k)
{
    float x = v->ps[DRP[pad][k]] + (pad < 4 ? m[DRD[pad][k]] : m[D_P5TUN + (pad - 4) * 4 + k]) + m[D_KTUN + k];
    return k == DK_TUN ? fm_clampf(x, -64.0f, 63.0f) : fm_clampf(x, 0.0f, 127.0f);
}
/* a TPT state-variable filter's coefficients: a1 a2 a3 k */
static void svf_c(float *c, float hz, float q)
{
    float g = fm_tanf(FM_PI * fm_clampf(hz, 10.0f, 0.45f * SR) * INV_SR), k = 1.0f / q, a1 = 1.0f / (1.0f + g * (g + k));
    c[0] = a1;
    c[1] = g * a1;
    c[2] = g * c[1];
    c[3] = k;
}
static inline float fpow2(float x) { return fm_exp2f(x); }
static inline float kdec(float t60) { return fm_expf(-6.9078f * INV_SR / fm_maxf(t60, 0.001f)); }   /* per sample */
static inline float ktau(float tau) { return fm_expf(-INV_SR / fm_maxf(tau, 0.0002f)); }
static void dr_control(voice_t *v, const float *m, float po)
{
    int pad = v->dr_pad, md = v->dr_model;
    float tune = drv(v, m, pad, DK_TUN), dec = drv(v, m, pad, DK_DEC), x = drv(v, m, pad, DK_TON) * (1.0f / 127.0f);
    float lev = drv(v, m, pad, DK_LEV) * (1.0f / 127.0f);
    /* (0.12) CTL A / B: x 1/4 .. 4 (A ratios: x 1/2 .. 2), 1 at 0: the model exactly as it was */
    float ca = v->ps[P_DA1 + pad], cb = v->ps[P_DB1 + pad];
    float fa = ca != 0.0f ? fpow2(ca * (1.0f / 32.0f)) : 1.0f, fb = cb != 0.0f ? fpow2(cb * (1.0f / 32.0f)) : 1.0f;
    float ra = ca != 0.0f ? fpow2(ca * (1.0f / 64.0f)) : 1.0f;
    float r = fpow2((tune * 0.5f + po) * (1.0f / 12.0f)), dsc = fpow2((dec - 64.0f) * (1.0f / 20.0f));
    float t60 = DM_T60[md] * dsc, f0 = DM_HZ[md] * r, rs = fm_sqrtf(r), tc = fpow2((x - 0.5f) * 2.0f);
    v->dr_x = x;
    v->dr_g = lev * lev * DM_GAIN[md];
    v->dr_ek = kdec(t60);
    v->dr_f0 = fm_minf(f0 * INV_SR, 0.45f);
    v->dr_xa = fa;
    v->dr_xb = fb;
    v->dr_xr = ra;
    v->dr_xc = 0.0f;
    switch (md) {
    case DM_BD808: v->dr_sw = 0.2f + 2.5f * x; v->dr_pk = ktau(0.02f); break;
    case DM_BD909: v->dr_sw = 3.0f; v->dr_pk = ktau(0.012f); v->dr_nk = ktau(0.002f); break;
    case DM_BDFM: v->dr_sw = 1.5f; v->dr_pk = ktau(0.01f); v->dr_nk = ktau(0.04f * dsc); break;
    case DM_TOM: v->dr_sw = 0.3f + 1.2f * x; v->dr_pk = ktau(0.05f); v->dr_nk = ktau(0.01f); break;
    case DM_CONGA: v->dr_sw = 0.25f + x; v->dr_pk = ktau(0.006f); v->dr_nk = ktau(0.004f); break;
    case DM_ZAP: v->dr_sw = 6.0f + 30.0f * x; v->dr_pk = ktau(0.025f * dsc); break;
    case DM_LASER: v->dr_sw = 1.5f + 6.0f * x; v->dr_pk = ktau(0.07f * dsc); v->dr_nk = ktau(0.003f); break;
    /* (0.9) SUB: a deep sine, a slight sweep (TONE), warmed; DIST: a hard-clipped kick (TONE the drive); PUNCH: a fast
     * sweep and a click (TONE) */
    case DM_BDSUB: v->dr_sw = 0.1f + 1.0f * x; v->dr_pk = ktau(0.03f); break;
    case DM_BDDIST: v->dr_sw = 4.0f; v->dr_pk = ktau(0.015f); break;
    case DM_BDPNCH: v->dr_sw = 5.0f; v->dr_pk = ktau(0.006f); v->dr_nk = ktau(0.002f); break;
    /* FAT: two low tones and lowpassed noise; 606: a short tone under bright noise; BRUSH: noise swept in, a band */
    case DM_SDFAT:
        v->dr_sw = 0.25f; v->dr_pk = ktau(0.015f); v->dr_nk = kdec(0.35f * dsc);
        svf_c(v->dr_ca, 3500.0f * rs, 0.7f);
        break;
    case DM_SD606:
        v->dr_sw = 0.3f; v->dr_pk = ktau(0.006f); v->dr_nk = kdec(0.18f * dsc);
        svf_c(v->dr_ca, 2500.0f * rs, 0.7f);
        break;
    case DM_SDBRSH: svf_c(v->dr_ca, 2200.0f * r * tc, 0.7f * fb); v->dr_nk = ktau(0.015f * fa); break;
    /* 909 hats: the metal higher and noisier through a bright band; FM: an inharmonic FM pair; 606: bright noise */
    case DM_H909C: case DM_H909O:
        svf_c(v->dr_ca, 10000.0f * rs * tc, 1.4f * fb);
        svf_c(v->dr_cb, 7500.0f * rs, 0.7f);
        break;
    case DM_HHFM: svf_c(v->dr_cb, 5000.0f * rs, 0.7f); v->dr_nk = kdec(0.05f * dsc); break;
    case DM_H606: svf_c(v->dr_ca, 7000.0f * r * tc, 0.8f * fa); break;
    case DM_SD808:
        v->dr_sw = 0.15f; v->dr_pk = ktau(0.01f); v->dr_nk = kdec(0.25f * dsc);
        svf_c(v->dr_ca, 1800.0f * rs, 0.7f);
        break;
    case DM_SD909:
        v->dr_sw = 0.8f; v->dr_pk = ktau(0.008f); v->dr_nk = kdec(0.28f * dsc);
        svf_c(v->dr_ca, 5000.0f * rs, 0.6f);
        break;
    case DM_RIM: break;
    case DM_CLAP:
        svf_c(v->dr_ca, 1100.0f * r * tc, 1.6f);
        v->dr_bk = ktau(0.0045f);
        v->dr_sp = (uint32_t)(0.0095f * SR * fa);
        break;
    case DM_CLAP2:                                   /* (909-like: four bursts, closer, brighter) */
        svf_c(v->dr_ca, 1600.0f * r * tc, 1.2f);
        v->dr_bk = ktau(0.0035f);
        v->dr_sp = (uint32_t)(0.0068f * SR * fa);
        break;
    case DM_HHC: case DM_HHO:
        svf_c(v->dr_ca, 8000.0f * rs, 1.1f * fb);
        svf_c(v->dr_cb, 6000.0f * rs * tc, 0.7f);
        break;
    case DM_CYMB:
        svf_c(v->dr_ca, 3800.0f * rs * tc, 0.8f * fb);
        svf_c(v->dr_cb, 2500.0f * rs, 0.7f);
        break;
    case DM_TAMB:                                    /* jingles: the metal higher, a bright band, a second shake */
        svf_c(v->dr_ca, 7500.0f * rs * tc, 1.6f * fb);
        svf_c(v->dr_cb, 5000.0f * rs, 0.7f);
        v->dr_bk = ktau(0.012f);
        v->dr_sp = (uint32_t)(0.03f * SR);
        break;
    case DM_COWB: svf_c(v->dr_ca, 1100.0f * rs * tc, 2.0f * fb); v->dr_nk = ktau(0.012f); break;
    case DM_CLAVE: break;
    case DM_SHAKR: svf_c(v->dr_ca, 4000.0f * r * fpow2((x - 0.5f) * 3.0f), 0.8f * fb); v->dr_nk = ktau(0.006f * fa); break;
    case DM_MARAC: svf_c(v->dr_ca, 6500.0f * r * fpow2((x - 0.5f) * 2.0f), 0.7f * fb); v->dr_nk = ktau(0.0006f * fa); break;
    default:                                         /* NOISE: TONE under the middle lowpass, over it highpass */
        svf_c(v->dr_ca, x < 0.5f ? 150.0f * r * fpow2(x * 15.0f) : 40.0f * r * fpow2((x - 0.5f) * 18.0f), 1.5f * fa);
        break;
    }
    switch (md) {                                    /* (0.12) the kicks, toms and zaps: A BEND (the sweep), B TICK (the click) */
    case DM_BD808: case DM_BD909: case DM_BDFM: case DM_TOM: case DM_CONGA: case DM_ZAP: case DM_LASER:
    case DM_BDSUB: case DM_BDDIST: case DM_BDPNCH:
        v->dr_sw *= fa;
        v->dr_xb = cb < 0.0f ? 1.0f + cb * (1.0f / 64.0f) : 1.0f;   /* (less of the click it has) */
        v->dr_xc = cb > 0.0f ? cb * (0.5f / 63.0f) : 0.0f;         /* (a click where it has none) */
        break;
    case DM_HHC: case DM_HHO: case DM_CYMB: case DM_TAMB: case DM_H909C: case DM_H909O:
        v->dr_xa = fa;                               /* NOIS: more noise in the mix (- more metal) */
        break;
    case DM_RIM:                                     /* B: the second tone's ratio, x 1/2 .. 2 */
        v->dr_xb = cb != 0.0f ? fpow2(cb * (1.0f / 64.0f)) : 1.0f;
        break;
    default: break;
    }
}

/* a hit: the drum's state from the start */
static void dr_start(voice_t *v)
{
    const sound_t *s = v->snd;
    int i;
    v->dr_pad = (uint8_t)pad_of(v->note);
    v->dr_model = (uint8_t)(s->s[S_DM1 + v->dr_pad] < NDM ? s->s[S_DM1 + v->dr_pad] : 0);
    v->dr_n = 0;
    v->dr_env = v->dr_ne = v->dr_pe = v->dr_be = 1.0f;
    v->dr_nk = v->dr_pk = v->dr_bk = 0.0f;
    v->dr_sp = 1;
    for (i = 0; i < 6; i++)
        v->dr_ph[i] = 0.0f;
    v->dr_sa[0] = v->dr_sa[1] = v->dr_sb[0] = v->dr_sb[1] = 0.0f;
}

/* a voice's engine changes: the union's state for the new one starts at zero [TONIC] */
static void voice_engine(voice_t *v, int eng)
{
    uint8_t *b = (uint8_t *)v + offsetof(voice_t, ph);   /* (the union: from its first member to amp_now) */
    unsigned i;
    for (i = 0; i < offsetof(voice_t, amp_now) - offsetof(voice_t, ph); i++)
        b[i] = 0;
    v->eng = (uint8_t)eng;
    if (eng == ENG_FM)
        v->harm_side = 9;
}

/* the operator envelopes' ATK DEC DEL with their modulation m, at the note's first control step [0.9.2] */
static void op_relatch(voice_t *v, const float *m)
{
    float dla = op_delay(eff(v, m, P_ADEL, D_ADEL), eng.bpm), dlb = op_delay(eff(v, m, P_BDEL, D_BDEL), eng.bpm);
    op_latch(&v->ea, dla, eff(v, m, P_AATK, D_AATK), eff(v, m, P_ADEC, D_ADEC));
    op_latch(&v->eb, dlb, eff(v, m, P_BATK, D_BATK), eff(v, m, P_BDEC, D_BDEC));
    if (v->ea.stage == 2 && v->ea.t == 0 && dla > 0.0f)
        v->ea.stage = 1;
    if (v->eb.stage == 2 && v->eb.t == 0 && dlb > 0.0f)
        v->eb.stage = 1;
    v->latch = 0;
}
static void voice_control(voice_t *v)
{
#ifdef PROF_NO_CONTROL
    if (v->age) return;
#endif
    if (v->wait) {                                   /* a strummed note not yet due: it waits as it was set up */
        v->wait--;
        return;
    }
    /* the modulation of each destination: zero between calls; what a call writes it sets back to zero at its end
     * (cheaper than clearing all ND each block) */
    static float m[ND];
    uint8_t mw[NMS * NMSLOT + 3];
    int nmw = 0;
    int i, algo, ri, mult;
    float f0, pitch, rc, ra, rb1, rb2, dt, harm, lev, b1, b2;
    const sound_t *s = v->snd ? v->snd : ts[v->trk].snd;
    trackstate_t *t = &ts[v->trk];
    int eng = s->s[S_ENGINE], va = eng == ENG_VA, other = eng != ENG_FM;
    /* [FIRMWARE] every parameter glides to its value, one pole a block (tau ~22 ms: 0.03 a block at 1500 Hz) */
    if (v->gliding) {                                /* (until every parameter has arrived; FM skips the drum pads'
                                                      * places) */
        float far = 0;
        for (i = 0; i < NSP; i++) {                 /* (VA: FOLD and RING sit there too; 0.12: the insert and the
                                                      * modulation envelope after the pads, for every engine) */
            if (i == P_D5TUN && eng == ENG_FM)
                i = P_ITYP;
            float tv = (float)v->p[i] * P_INV[i], d = tv - v->ps[i];
            if (P_SMOOTH[i] && fm_fabsf(d) > 0.004f) {
                v->ps[i] += d * 0.0326f;
                far = 1;
            } else
                v->ps[i] = tv;
        }
        v->gliding = far != 0;
    }

    /* modulation sources with destinations (VELOCITY MOD, MOD WHEEL, AFTERTOUCH, BREATH, PITCH BEND): the depth
     * offsets the parameter (display units) by depth x source */
    {
        float src[NMS];
        int k, j;
        src[MS_VEL] = v->vel01;
        src[MS_MW] = t->mw;
        src[MS_AT] = t->at;
        src[MS_BC] = t->bc;
        src[MS_PB] = t->pb;
        for (k = 0; k < NMS; k++)
            if (src[k] != 0.0f)                      /* (a source at rest adds nothing) */
            for (j = 0; j < NMSLOT; j++)
                if (s->mdst[k][j] && s->mdst[k][j] < ND) {
                    int d = s->mdst[k][j];
                    float sp = DEST_PARAM[d] != P_NONE ? spanof(DEST_PARAM[d]) : d == D_PITCH || d == D_PITCHAB2 ? 24.0f : 127.0f;
                    if (other && DEST_PARAM[d] <= P_BRST) {   /* the VA / DRUM engine's range */
                        const pdesc_t *pd = param_desc(eng, DEST_PARAM[d]);
                        sp = (float)(pd->hi - pd->lo) / (float)pd->q;
                    }
                    if (va && (d == D_RATA || d == D_RATALL))   /* VA: FOLD, RING */
                        sp = 63.0f;
                    m[d] += (float)s->mdep[k][j] * (1.0f / 128.0f) * sp * src[k];
                    mw[nmw++] = (uint8_t)d;
                }
    }

    /* (0.12) the modulation envelope (before the LFOs: it may move LFO 1's parameters) */
    {
        int d3 = v->p[P_MDST];
        if (d3 > 0 && d3 < ND && v->p[P_MDEP] != 0) {
            adsr_step(&v->em, v->ps[P_MDEL], v->ps[P_MATK], v->ps[P_MDEC], v->ps[P_MSUS] * (1.0f / 127.0f),
                      v->ps[P_MREL], v->gate, 1);
            m[d3] += v->em.lvl * v->ps[P_MDEP] * (1.0f / 64.0f) * lfo_unit(d3, other);
            mw[nmw++] = (uint8_t)d3;
        }
    }

    /* LFO 2 first: it may move LFO 1's parameters */
    {
        float l2 = lfo_step(v, 1, pv(v, P_L2SPD), v->p[P_L2MUL], (float)v->p[P_L2FAD], v->p[P_L2WAV],
                            (float)v->p[P_L2SPH], v->p[P_L2MOD], pv(v, P_L2DEP));
        int d2 = v->p[P_L2DST], d1;
        float l1;
        if (d2 > 0 && d2 < ND) {
            m[d2] += l2 * lfo_unit(d2, other);
            mw[nmw++] = (uint8_t)d2;
        }
        mult = (int)(eff(v, m, P_L1MUL, D_L1MUL) + 0.5f);
        l1 = lfo_step(v, 0, eff(v, m, P_L1SPD, D_L1SPD), mult, eff(v, m, P_L1FAD, D_L1FAD),
                      (int)(eff(v, m, P_L1WAV, D_L1WAV) + 0.5f), eff(v, m, P_L1SPH, D_L1SPH),
                      (int)(eff(v, m, P_L1MOD, D_L1MOD) + 0.5f), eff(v, m, P_L1DEP, D_L1DEP));
        d1 = v->p[P_L1DST];
        if (d1 > 0 && d1 < ND && (d1 < D_L1SPD || d1 > D_L1DEP)) {   /* (not its own parameters) */
            m[d1] += l1 * lfo_unit(d1, other);
            mw[nmw++] = (uint8_t)d1;
        }
    }

    /* macros [MANUAL App. C]: AB LEV/ATK/DEC/END/DEL move both groups */
    m[D_ALEV] += m[D_ABLEV]; m[D_BLEV] += m[D_ABLEV];
    m[D_AATK] += m[D_ABATK]; m[D_BATK] += m[D_ABATK];
    m[D_ADEC] += m[D_ABDEC]; m[D_BDEC] += m[D_ABDEC];
    m[D_AEND] += m[D_ABEND]; m[D_BEND] += m[D_ABEND];
    m[D_ADEL] += m[D_ABDEL]; m[D_BDEL] += m[D_ABDEL];
    if (m[D_RATALL] != 0 && !other) {                          /* RATIO ALL: every ratio, as a share of its list */
        float r = m[D_RATALL] * (1.0f / 127.0f);
        m[D_RATC] += r * NRATC;
        m[D_RATA] += r * NRATA;
        m[D_RATB] += r * NRATB * NRATB;
    }

    /* the operator envelopes' ATK DEC DEL with their modulation, latched at the note's start [0.9.2] */
    if (v->latch)
        op_relatch(v, m);

    /* pitch: note, sound octave, glide, pitch bend (the sound's PB DEPTH, semitones), PITCH modulation */
    if (v->glide_len > 0 && v->glide_t < v->glide_len) {
        float g = v->glide_t / v->glide_len;
        pitch = v->pitch_from + (v->pitch - v->pitch_from) * g;
        if (s->s[S_PSTYLE])                          /* GLISSANDO: semitones */
            pitch = fm_floorf(pitch + 0.5f);
        if (!s->s[S_PGATE] || v->gate)
            v->glide_t += CRF * INV_SR;
    } else
        pitch = v->pitch;
    pitch += (float)((int)s->s[S_OCT] - 2) * 12.0f + m[D_PITCH] + v->uni_cents * 0.01f + t->pb * (float)s->s[S_PBDEP];
    if (eng_pitch != 0.0f)                           /* TAPE [TONIC 0.7]: the whole machine slowing */
        pitch += eng_pitch;
    CACHE(v->c_pitch, pitch, v->c_f0, 440.0f * fm_exp2f((pitch - 69.0f) * (1.0f / 12.0f)));
    f0 = v->c_f0;

    if (v->eng != eng) {                             /* the sound changed engine under the note: the note ends (a
                                                      * quick fade, not the new engine's tone) [0.9.2] */
        voice_engine(v, eng);
        v->stolen = 1;
    }
    if (eng == ENG_DR) {                             /* the DRUM engine: its pad's drum */
        dr_control(v, m, pitch - (float)v->note);
        goto filters;
    }
    if (va) {                                        /* the VA engine: its oscillators instead of the operators */
        openv_step(&v->ea, eff(v, m, P_AEND, D_AEND) * (1.0f / 128.0f), v->p[P_ATRG], v->gate);
        openv_step(&v->eb, eff(v, m, P_BEND, D_BEND) * (1.0f / 128.0f), v->p[P_BTRG], v->gate);
        va_control(v, m, f0);
        goto filters;
    }
    /* ratios [ELEKTRON lists] + offsets [-0.999, 0.999] + DTUN on A and B2 [MANUAL 11.3.6] */
    algo = (int)(eff(v, m, P_ALGO, D_ALGO) + 0.5f);
    ri = (int)(eff(v, m, P_RATC, D_RATC) + 0.5f);
    rc = RATIO_C[ri] + eff(v, m, P_OFSC, D_OFSC) * 0.001f;
    ri = (int)(eff(v, m, P_RATA, D_RATA) + 0.5f);
    ra = RATIO_A[ri] + eff(v, m, P_OFSA, D_OFSA) * 0.001f;
    ri = (int)(eff(v, m, P_RATB, D_RATB) + 0.5f);
    rb1 = RATIO_B[ri / NRATB] + eff(v, m, P_OFSB1, D_OFSB1) * 0.001f;
    rb2 = RATIO_B[ri % NRATB] + eff(v, m, P_OFSB2, D_OFSB2) * 0.001f;
    {   /* DTUN [FIRMWARE]: offsets added to the ratios of A and B2; PITCH A and B2 multiplies them */
        float dv = eff(v, m, P_DTUN, D_DTUN);
        if (dv != v->c_dtun) {
            v->c_dtun = dv;
            v->c_dta = dtun_a(dv);
            v->c_dtb2 = dtun_b2(dv);
        }
        ra += v->c_dta;
        rb2 += v->c_dtb2;
        dt = m[D_PITCHAB2] != 0.0f ? fm_exp2f(m[D_PITCHAB2] * (1.0f / 12.0f)) : 1.0f;
    }
    {   /* increments in 2^-32 cycles a sample (below Nyquist) */
        float k = f0 * INV_SR * 4294967296.0f, lim = 0.49f * 4294967296.0f;
        v->inc[OC] = (uint32_t)(int32_t)fm_minf(rc * k, lim);
        v->inc[OA] = (uint32_t)(int32_t)fm_minf(ra * dt * k, lim);
        v->inc[OB1] = (uint32_t)(int32_t)fm_minf(rb1 * k, lim);
        v->inc[OB2] = (uint32_t)(int32_t)fm_minf(rb2 * dt * k, lim);
    }

    /* operator envelopes (ATK / DEC / delay latched at the note's start) */
    openv_step(&v->ea, eff(v, m, P_AEND, D_AEND) * (1.0f / 128.0f), v->p[P_ATRG], v->gate);
    openv_step(&v->eb, eff(v, m, P_BEND, D_BEND) * (1.0f / 128.0f), v->p[P_BTRG], v->gate);

    /* modulation depth [FIRMWARE]: T_lev[level x key scaling x envelope] x 13.86 rad; LEV B through the macro */
    lev = eff(v, m, P_ALEV, D_ALEV) * (1.0f / 128.0f);
    lev_b_split(eff(v, m, P_BLEV, D_BLEV), &b1, &b2);
    v->ev_tgt[0] = v->ea.lvl;
    v->ev_tgt[1] = v->eb.lvl;
    v->idx_tgt[OA] = fm_index(128.0f * lev * v->ks[0] * v->ea.lvl) * (1.0f / FM_TWO_PI);
    v->idx_tgt[OB1] = fm_index(128.0f * b1 * v->ks[1] * v->eb.lvl) * (1.0f / FM_TWO_PI);
    v->idx_tgt[OB2] = fm_index(128.0f * b2 * v->ks[2] * v->eb.lvl) * (1.0f / FM_TWO_PI);
    v->algo = (uint8_t)algo;
    v->fb_tgt = fb_amount(eff(v, m, P_FDBK, D_FDBK)) * (1.0f / FM_TWO_PI);

    /* MIX [FIRMWARE]: (K - m) X + m Y, K = 0.248, m = K x (MIX + 64) / 127 */
    {
        float x = (eff(v, m, P_MIX, D_MIX) + 64.0f) * (1.0f / 127.0f);
        v->wx_tgt = 0.248f * (1.0f - x);
        v->wy_tgt = 0.248f * x;
    }

    /* HARM [MANUAL A.6]: negative shapes C, positive A and B1; between integers the tables blend */
    harm = eff(v, m, P_HARM, D_HARM);
    {
        int side = harm < 0 ? -1 : harm > 0 ? 1 : 0;
        float a = fm_fabsf(harm);
        int hi = (int)a;
        float fr = a - (float)hi;
        if (hi >= NHARM - 1) {
            hi = NHARM - 1;
            fr = 0;
        }
        if (side != v->harm_side || hi != v->harm_i || fm_fabsf(fr - v->harm_frac) > 0.002f) {
            /* (the blend is only noted here: voice_block reads it from the two tables while it moves, and writes
             * the voice's table once it has stood still a block: the same values, fewer of them) */
            harm_t *h = &harm_w[v->wti];
            h->a = (1.0f - fr) * (1.0f / 32767.0f);
            h->b = fr * (1.0f / 32767.0f);
            h->t0 = harm_q[hi];
            if (hi + 1 >= NHARM) {                   /* (the last shape: fr is 0, so t0 a + t1 0 is t0 a, and
                                                      * t' 0 + t0 a its bits too, with t' the shape before) */
                h->t0 = harm_q[hi - 1];
                h->b = h->a;
                h->a = 0.0f;
            }
            h->pend = 1;
            v->harm_side = side;
            v->harm_i = hi;
            v->harm_frac = fr;
        }
    }

filters:
    /* filter envelope; cutoff = FREQ + 2 ENV x env + key track [FIRMWARE: 0.8813 KT/100 a semitone from note 48],
     * f0 = 5 Hz x 2^(c / 10.5756), Q from RESO; LP2 and HP share one 2-pole, HP = input - LP */
    adsr_step(&v->ef, eff(v, m, P_FDEL, D_FDEL), eff(v, m, P_FATK, D_FATK), eff(v, m, P_FDEC, D_FDEC),
              eff(v, m, P_FSUS, D_FSUS) * (1.0f / 127.0f), eff(v, m, P_FREL, D_FREL), v->gate, 1);
    {
        float fc = fm_clampf(eff(v, m, P_FREQ, D_FREQ) + eff(v, m, P_FENV, D_FENV) * 2.0f * v->ef.lvl +
                             ((float)v->note - 48.0f) * (float)s->s[S_FKTRK] * 0.008813f, 0, 127);
        float rs = eff(v, m, P_RESO, D_RESO);
        v->ftype = (uint8_t)v->p[P_FTYP];
        if (fm_fabsf(fc - v->c_fc) > 0.01f || rs != v->c_res || v->ftype != v->c_ftype) {
            float hz = fm_minf(cutoff_hz(fc), 0.45f * SR), g = fm_tanf(FM_PI * hz * INV_SR), q;
            if (rs != v->c_res || v->c_q == 0.0f)    /* (Q only when RESO moves) */
                v->c_q = reso_q(rs);
            q = v->c_q;
            v->c_fc = fc;
            v->c_res = rs;
            v->c_ftype = v->ftype;
            v->fg = g;
            if (v->ftype == 3) {                     /* [FIRMWARE] 4 bilinear one-poles at fc, feedback k = 4r through a
                                                      * tanh-like curve, r = 0.875 (RESO / 128)^2 (k <= 3.44), input
                                                      * x (1 + k); the loop's exact solution INFERRED */
                float G = g / (1.0f + g), G4 = G * G * G * G, rr = rs * (1.0f / 128.0f);
                v->fk = 3.5f * rr * rr;
                v->fr = G;
                v->c_r = 1.0f / (1.0f + v->fk * G4);
            } else {                                 /* 2-pole TPT SVF, damping 1 / (2Q) */
                float d = 0.5f / q;
                v->fk = d;
                v->fr = 1.0f / (1.0f + 2.0f * d * g + g * g);
            }
        }
    }
    /* base-width [FIRMWARE]: a one-pole lowpass at BASE + WIDTH (open at 127) and a one-pole highpass at BASE,
     * corners 5 Hz x ~3.1 every 16 */
    {
        float base = eff(v, m, P_BASE, D_BASE), top = base + eff(v, m, P_WDTH, D_WDTH);
        v->bw_on = base > 0.5f || top < 126.5f;
        if (v->bw_on && (base != v->c_base || top != v->c_top)) {
            v->c_base = base;
            v->c_top = top;
            v->bw_ghp = fm_expf(-FM_TWO_PI * fm_minf(bw_hz(base), 0.45f * SR) * INV_SR);
            v->bw_glp = top >= 126.5f ? 0.0f : fm_expf(-FM_TWO_PI * fm_minf(bw_hz(top), 0.45f * SR) * INV_SR);
        }
    }
    {   /* (0: bypassed; modulation reaches it at DRV 0 too) [0.9.2] */
        float dv = eff(v, m, P_DRV, D_DRV);
        v->drive_g = dv > 0.0f ? drive_gain(dv) : 0.0f;
    }
    {   /* (0.12) the insert effect: AMT drives it (each type its own range), TONE a lowpass on it from 100 Hz up
         * (127: open), MIX dry / wet. OFF: nothing computed, the samples as before */
        int ty = v->p[P_ITYP];
        v->in_typ = (uint8_t)(ty > 0 && ty < NINS ? ty : 0);
        if (v->in_typ) {
            float a = eff(v, m, P_IAMT, D_IAMT) * (1.0f / 127.0f), tn = eff(v, m, P_ITON, D_ITON);
            v->in_mix = eff(v, m, P_IMIX, D_IMIX) * (1.0f / 127.0f);
            v->in_b = 0.0f;
            v->in_c = 1.0f;
            switch (v->in_typ) {
            case INS_OVER: v->in_g = 1.0f + 15.0f * a * a; v->in_c = 1.0f / fm_sqrtf(v->in_g); break;
            case INS_HARD: v->in_g = 1.0f + 31.0f * a * a; v->in_c = 1.0f / fm_sqrtf(v->in_g); break;
            case INS_FUZZ:
                v->in_g = 1.0f + 63.0f * a * a;
                v->in_b = 0.35f * a;
                v->in_c = 0.7f;
                break;
            case INS_RECT: v->in_g = 1.0f + 7.0f * a; v->in_c = 0.8f; break;
            case INS_FOLD: v->in_g = 1.0f + 7.0f * a; break;
            default:                                 /* CRUSH: 12 .. 2 bits, and holds up to 16 samples */
                v->in_q = fpow2(-(12.0f - 10.0f * a));
                v->in_len = (uint8_t)(1 + (int)(a * a * 15.0f));
                break;
            }
            CACHE(v->in_c2, tn, v->in_tg, tn >= 126.5f ? 0.0f :
                  1.0f - fm_expf(-FM_TWO_PI * 100.0f * fpow2(tn * (8.0f / 127.0f)) * INV_SR));
        }
    }
    {   /* LO-FI [TONIC]: SRR holds each sample for 1..64 samples (2^(SRR x 6 / 127), fractional: it sweeps);
         * BRR rounds to 16..2 bits (full scale +-1). LFOs and the modulation reach both (0.9) */
        float srr = fm_clampf(v->ps[P_SRR] + m[D_SRR], 0.0f, 127.0f), brr = fm_clampf(v->ps[P_BRR] + m[D_BRR], 0.0f, 127.0f);
        if (srr != v->c_srr) {                       /* (cached: a pure function of srr, as of brr below) */
            v->c_srr = srr;
            v->lf_inc = srr > 0.5f ? fm_exp2f(srr * (-6.0f / 127.0f)) : 0.0f;
        }
        if (brr != v->c_brr) {
            v->c_brr = brr;
            v->lf_q = brr > 0.5f ? fm_exp2f(brr * (14.0f / 127.0f) - 15.0f) : 0.0f;
            v->lf_iq = v->lf_q > 0.0f ? 1.0f / v->lf_q : 0.0f;
        }
    }

    /* amp: envelope x velocity (VELOCITY TO VOL) x VOL */
    adsr_step(&v->eamp, 0, eff(v, m, P_EATK, D_EATK), eff(v, m, P_EDEC, D_EDEC),
              eff(v, m, P_ESUS, D_ESUS) * (1.0f / 127.0f), eff(v, m, P_EREL, D_EREL), v->gate, 0);
    {
        float vg;
        switch (s->s[S_VEL2VOL]) {                   /* [FIRMWARE: the four curves; see vel_curve] */
        case VV_OFF: vg = 1.0f; break;
        case VV_LOG: vg = 1.0f - vel_curve(1.0f - v->vel01); break;
        case VV_EXP: vg = vel_curve(v->vel01); break;
        default: vg = v->vel01; break;
        }
        v->amp_tgt = v->eamp.lvl * vg * vol_gain(eff(v, m, P_VOL, D_VOL)) * v->steal_g * t->level;
    }
    {
        /* [FIRMWARE] the pan law: p = (PAN - 1) / 252 (PAN 0..127, 64 centre), gains 1 - 4p^2 and 4p(1 - p),
         * 0.75 each at the centre (which side is which: INFERRED) */
        float pan = fm_clampf(eff(v, m, P_PAN, D_PAN) + v->uni_pan + (eng == ENG_DR ? v->ps[P_DPN1 + v->dr_pad] : 0.0f) +
                              63.0f, 0, 126) * (1.0f / 252.0f);   /* (0.12: a kit's pads their own pan) */
        if (pan != v->c_pan) {
            v->c_pan = pan;
            v->panl = 1.0f - 4.0f * pan * pan;
            v->panr = 4.0f * pan * (1.0f - pan);
        }
    }
    {
        float c = eff(v, m, P_CHR, D_CHR), d = eff(v, m, P_DEL, D_DEL), r = eff(v, m, P_REV, D_REV);
        v->snd_c = c * c * (1.0f / 16384.0f);        /* [FIRMWARE] (send / 128)^2 */
        v->snd_d = d * d * (1.0f / 16384.0f);
        v->snd_r = r * r * (1.0f / 16384.0f);
    }
    if (v->stolen) {                                 /* a stolen voice fades out in ~3 ms */
        v->steal_g *= 0.5f;
        if (v->steal_g < 0.01f)
            v->on = 0;
    }
    if (v->eamp.stage == 0 && v->eamp.lvl == 0)
        v->on = 0;
    if (eng == ENG_DR && v->dr_env < 0.0001f && v->dr_ne < 0.0001f && v->dr_n > CR)
        v->on = 0;                                   /* the drum has died away */
    /* the modulation back to zero: what the sources wrote, and what the macros and RATIO ALL add to */
    while (nmw)
        m[mw[--nmw]] = 0;
    m[D_ALEV] = m[D_BLEV] = m[D_AATK] = m[D_BATK] = m[D_ADEC] = m[D_BDEC] = 0;
    m[D_AEND] = m[D_BEND] = m[D_ADEL] = m[D_BDEL] = 0;
    m[D_RATC] = m[D_RATA] = m[D_RATB] = 0;
}

/* ---------------------------------------------------------------------- the audio --- */
/* a lookup in a 256-point table at a phase in 2^-32 cycles, linear between points */
static inline float lku(const float *t, uint32_t ph)
{
    uint32_t i = ph >> 24;
    float fr = (float)(int32_t)(ph & 0xFFFFFFu) * (1.0f / 16777216.0f);
    return t[i] + fr * (t[i + 1] - t[i]);
}
/* a modulation in cycles (|m| < 128) as a phase offset */
static inline uint32_t pmod(float m) { return (uint32_t)(int32_t)(m * 16777216.0f) << 8; }
/* a soft clip without a divide or an exp: cubic on [-1.5, 1.5], flat beyond */
static inline float softc(float x)
{
    if (x > 1.5f) return 1.0f;
    if (x < -1.5f) return -1.0f;
    return x - (4.0f / 27.0f) * x * x * x;
}

/* the drive [FIRMWARE, DSP]: u = 2 g x; y = f(f(f(u))) / 2, f(u) = u (2 - |u|), +-1 beyond */
static inline float f_dr1(float u) { return u * (2.0f - fm_fabsf(u)); }   /* f_dr for |u| <= 1 */
static inline float f_dr(float u)
{
    if (u >= 1.0f) return 1.0f;
    if (u <= -1.0f) return -1.0f;
    return u * (2.0f - fm_fabsf(u));
}

/* the per-sample work of one voice: the algorithm, drive, filters, amp, pan, sends; added to the buses.
 * The routing is the firmware's (research/firmware/ENGINE_RE): each modulator reaches its target either as its
 * raw output or through its envelope (raw x env); its depth is always T_lev[level x env]; the feedback operator
 * feeds back its enveloped output, one sample late. */
/* PolyBLEP: the correction of a unit step at phase 0, t in 0..1, dt the increment (idt its inverse) */
static inline float blep(float t, float dt, float idt)
{
    if (t < dt) {
        t *= idt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) * idt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}
/* one oscillator over a block, the wave chosen once: phases ph[i] (0..1) given, out[i] = wave (added with gain g) */
static void va_osc(int w, const float *ph, float dt, float pw, float g, float *out, int add)
{
    float idt = 1.0f / fm_maxf(dt, 1e-6f), dc = 2.0f * pw - 1.0f;
    int i;
    switch (w) {
    case VW_SAW:
        for (i = 0; i < CR; i++) {
            float t = ph[i], y = 2.0f * t - 1.0f;
            if (t < dt || t > 1.0f - dt) y -= blep(t, dt, idt);
            out[i] = add ? out[i] + g * y : g * y;
        }
        break;
    case VW_SQR:
        for (i = 0; i < CR; i++) {
            float t = ph[i], u = t - pw + 1.0f, y;
            if (u >= 1.0f) u -= 1.0f;
            y = (t < pw ? 1.0f : -1.0f) - dc;
            if (t < dt || t > 1.0f - dt) y += blep(t, dt, idt);
            if (u < dt || u > 1.0f - dt) y -= blep(u, dt, idt);
            out[i] = add ? out[i] + g * y : g * y;
        }
        break;
    case VW_TRI:
        for (i = 0; i < CR; i++) {
            float y = 1.0f - 4.0f * fm_fabsf(ph[i] - 0.5f);
            out[i] = add ? out[i] + g * y : g * y;
        }
        break;
    case VW_SIN:
        for (i = 0; i < CR; i++)
            out[i] = add ? out[i] + g * lks(ph[i]) : g * lks(ph[i]);
        break;
    default:
        if (!add)
            for (i = 0; i < CR; i++)
                out[i] = 0;
        break;
    }
}
/* the VA engine's samples [TONIC]: OSC1's phases (its wraps restart OSC2 with SYNC), OSC2, OSC1 (phase-modulated
 * by OSC2 with XMOD), the sub (a square an octave below OSC1), noise */
static void va_block(voice_t *v, float *x)
{
    float p1[CR], p2[CR], o2[CR];
    float t1 = v->va_ph[0], t2 = v->va_ph[1], t3 = v->va_ph[2];
    const float d1 = v->va_inc[0], d2 = v->va_inc[1], d3 = v->va_inc[2], xm = v->va_xm, r21 = d2 / fm_maxf(d1, 1e-6f);
    int i, need2 = v->va_w2 != VW_OFF && (v->va_g2 > 0.0f || xm > 0.0f);
    if (v->va_sync)
        for (i = 0; i < CR; i++) {
            p1[i] = t1;
            p2[i] = t2;
            t1 += d1;
            t2 += d2;
            if (t2 >= 1.0f) t2 -= 1.0f;
            if (t1 >= 1.0f) {
                t1 -= 1.0f;
                t2 = t1 * r21;                       /* hard sync: OSC2 restarts with OSC1 */
            }
        }
    else
        for (i = 0; i < CR; i++) {
            p1[i] = t1;
            p2[i] = t2;
            t1 += d1;
            if (t1 >= 1.0f) t1 -= 1.0f;
            t2 += d2;
            if (t2 >= 1.0f) t2 -= 1.0f;
        }
    if (need2)
        va_osc(v->va_w2, p2, d2, v->va_pw, 1.0f, o2, 0);
    if (xm > 0.0f && need2)
        for (i = 0; i < CR; i++) {
            float t = p1[i] + xm * o2[i];
            if (t < 0.0f) t += 1.0f;
            else if (t >= 1.0f) t -= 1.0f;
            p1[i] = t;
        }
    if (v->va_ring > 0.0f && need2) {                /* (0.6) ring modulation: OSC1 alone first */
        float o1[CR], g1 = v->va_g1, gr = v->va_ring;
        va_osc(v->va_w1, p1, d1, v->va_pw, 1.0f, o1, 0);
        for (i = 0; i < CR; i++)
            x[i] = g1 * o1[i] + gr * o1[i] * o2[i];
    } else
        va_osc(v->va_w1, p1, d1, v->va_pw, v->va_g1, x, 0);
    if (need2 && v->va_g2 > 0.0f)
        for (i = 0; i < CR; i++)
            x[i] += v->va_g2 * o2[i];
    if (v->va_gs > 0.0f) {
        for (i = 0; i < CR; i++) {
            p2[i] = t3;
            t3 += d3;
            if (t3 >= 1.0f) t3 -= 1.0f;
        }
        va_osc(VW_SQR, p2, d3, 0.5f, v->va_gs, x, 1);
    }
    if (v->va_gn > 0.0f) {
        uint32_t ns = v->va_ns;
        for (i = 0; i < CR; i++) {
            ns = ns * 1664525u + 1013904223u;
            x[i] += v->va_gn * (float)(int32_t)ns * (1.0f / 2147483648.0f);
        }
        v->va_ns = ns;
    }
    if (v->va_fold > 0.0f) {                         /* (0.6) the folder: u in units of the mix's full level, a
                                                      * triangle that reflects at +-1 */
        float k = v->va_fold * (1.0f / 0.3f);
        for (i = 0; i < CR; i++) {
            float t = (x[i] * k + 1.0f) * 0.25f;
            t -= fm_floorf(t);
            x[i] = 0.3f * (1.0f - 4.0f * fm_fabsf(t - 0.5f));
        }
    }
    v->va_ph[0] = t1;
    v->va_ph[1] = t2;
    v->va_ph[2] = t3;
}

/* the DRUM engine's samples [TONIC]: per model, a sine with a falling pitch (kicks, toms, congas, the zap), tones
 * and filtered noise (snares), pings (rim, clave), noise bursts (clap), six detuned squares through a bandpass and a
 * highpass (the 808's metal: hats, cymbal), two squares through a bandpass (cowbell), filtered noise */
static inline float dnoise(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return (float)(int32_t)*s * (1.0f / 2147483648.0f); }
/* one sample through a TPT SVF (c: a1 a2 a3 k, st: its two states): the lowpass; band and high through pointers */
static inline float svf(const float *c, float *st, float x, float *band, float *high)
{
    float v3 = x - st[1], v1 = c[0] * st[0] + c[1] * v3, v2 = st[1] + c[1] * st[0] + c[2] * v3;
    st[0] = 2.0f * v1 - st[0];
    st[1] = 2.0f * v2 - st[1];
    *band = v1;
    *high = x - c[3] * v1 - v2;
    return v2;
}
static inline float wrap1(float p) { return p >= 1.0f ? p - 1.0f : p; }
static inline float sq(float p) { return p < 0.5f ? 1.0f : -1.0f; }
static const float HH_R[6] = {1.0f, 1.4827f, 1.8003f, 2.5460f, 2.6303f, 3.8967f};   /* the 808's six: 205 .. 800 Hz */
static void dr_block(voice_t *v, float *x)
{
    float env = v->dr_env, ek = v->dr_ek, ne = v->dr_ne, nk = v->dr_nk, pe = v->dr_pe, pk = v->dr_pk;
    float f0 = v->dr_f0, sw = v->dr_sw, tx = v->dr_x, g = v->dr_g, p0 = v->dr_ph[0], p1 = v->dr_ph[1];
    float b, h, y;
    uint32_t ns = v->va_ns;
    int i, uses_n = 1;
    switch (v->dr_model) {
    case DM_BD808: case DM_BD909: case DM_BDFM: case DM_TOM: case DM_CONGA: case DM_ZAP: case DM_LASER:
    case DM_BDSUB: case DM_BDDIST: case DM_BDPNCH: {
        int md = v->dr_model;
        float cl = md == DM_BD909 ? 0.6f * tx : md == DM_TOM || md == DM_CONGA ? 0.08f : md == DM_LASER ? 0.15f :
                   md == DM_BDPNCH ? 0.2f + 0.6f * tx : 0.0f;
        cl = cl * v->dr_xb + v->dr_xc;               /* (0.12) CLICK */
        float dk = 2.0f + 10.0f * tx;                /* (BD DIST's drive) */
        float idx = 2.5f * tx;
        uses_n = md != DM_BD808 && md != DM_ZAP && md != DM_BDSUB && md != DM_BDDIST;
        for (i = 0; i < CR; i++) {
            float f = f0 * (1.0f + sw * pe);
            pe *= pk;
            p0 = wrap1(p0 + fm_minf(f, 0.45f));
            if (md == DM_BDFM) {
                p1 = wrap1(p1 + 2.0f * fm_minf(f, 0.22f));
                y = lks(p0 + idx * ne * lks(p1));
            } else
                y = lks(p0);
            if (cl > 0.0f)
                y += cl * ne * dnoise(&ns);
            if (md == DM_BD909)
                y = softc(1.6f * y);
            else if (md == DM_LASER)                 /* (the Simmons' edge) */
                y = softc(1.3f * y);
            else if (md == DM_BDSUB)                 /* (warmed) */
                y = softc(1.25f * y);
            else if (md == DM_BDDIST)                /* (clipped hard, then levelled) */
                y = 0.8f * softc(dk * y);
            else if (md == DM_BDPNCH)
                y = softc(1.4f * y);
            if (uses_n) ne *= nk;
            x[i] = g * env * y;
            env *= ek;
        }
        break;
    }
    case DM_SD808: case DM_SD909: case DM_SDFAT: case DM_SD606: {
        int md = v->dr_model, two = md == DM_SD808 || md == DM_SDFAT;
        float tg = (1.0f - 0.65f * tx) * (md == DM_SD606 ? 0.45f : 1.0f), ng = (0.25f + 0.9f * tx) * (md == DM_SD606 ? 1.4f : 1.0f);
        tg *= v->dr_xb;                              /* (0.12) BODY, SNAP */
        ng *= v->dr_xa;
        float r2 = md == DM_SDFAT ? 1.75f : 1.83f;
        for (i = 0; i < CR; i++) {
            float f = f0 * (1.0f + sw * pe), n = dnoise(&ns), lo;
            pe *= pk;
            p0 = wrap1(p0 + f);
            y = lks(p0);
            if (two) {
                p1 = wrap1(p1 + fm_minf(r2 * f, 0.45f));
                y = 0.6f * y + 0.4f * lks(p1);
            }
            lo = svf(v->dr_ca, v->dr_sa, n, &b, &h);
            x[i] = g * (tg * env * y + ng * ne * (md == DM_SDFAT ? lo : md == DM_SD909 ? b : h));
            env *= ek;
            ne *= nk;
        }
        break;
    }
    case DM_RIM: case DM_CLAVE: {
        int rim = v->dr_model == DM_RIM;
        float f1 = fm_minf(f0 * 3.54f * v->dr_xb, 0.45f), k = (1.0f + 3.0f * tx) * v->dr_xa, hd = 2.0f * v->dr_xa;
        uses_n = 0;                                  /* (0.12: A HARD; B the rim's second tone) */
        for (i = 0; i < CR; i++) {
            p0 = wrap1(p0 + f0);
            if (rim) {
                p1 = wrap1(p1 + f1);
                y = softc(hd * ((0.7f - 0.35f * tx) * lks(p0) + (0.3f + 0.4f * tx) * lks(p1)));
            } else
                y = softc(k * lks(p0)) / (k > 1.5f ? 1.0f : k);
            x[i] = g * env * y;
            env *= ek;
        }
        break;
    }
    case DM_CLAP: case DM_CLAP2: {                   /* three (four) bursts ~9.5 (6.8) ms apart, then the tail */
        float be = v->dr_be, bk = v->dr_bk;
        uint32_t n = v->dr_n, sp = v->dr_sp, nb = v->dr_model == DM_CLAP2 ? 4u : 3u;
        float tl = 0.7f * v->dr_xb;                  /* (0.12) A SPREAD (dr_sp), B TAIL */
        uses_n = 0;
        for (i = 0; i < CR; i++, n++) {
            if (n < nb * sp && n % sp == 0u)
                be = 1.0f;
            (void)svf(v->dr_ca, v->dr_sa, dnoise(&ns), &b, &h);
            x[i] = g * b * (be + (n >= (nb - 1u) * sp ? tl * env : 0.0f));
            be *= bk;
            if (n >= (nb - 1u) * sp)
                env *= ek;
        }
        v->dr_be = be;
        break;
    }
    case DM_HHC: case DM_HHO: case DM_CYMB: case DM_TAMB: case DM_H909C: case DM_H909O: {
        int tb = v->dr_model == DM_TAMB, h9 = v->dr_model == DM_H909C || v->dr_model == DM_H909O;
        float nm = fm_minf((v->dr_model == DM_CYMB || tb ? 0.25f : h9 ? 0.45f + 0.4f * tx : 0.8f * tx) * v->dr_xa, 1.0f);
        float mg = (1.0f - nm) * (1.0f / 6.0f);       /* (0.12: A NOIS, B the band's resonance) */
        float inc[6], ph[6], fb = v->dr_model == DM_CYMB ? 1.3f : 1.0f, be = v->dr_be, bk = v->dr_bk;
        uint32_t n = v->dr_n, sp = v->dr_sp;
        int k;
        uses_n = 0;
        for (k = 0; k < 6; k++) {
            inc[k] = fm_minf(f0 * fb * HH_R[k], 0.45f);
            ph[k] = v->dr_ph[k];
        }
        for (i = 0; i < CR; i++) {
            float mt = 0.0f, lo;
            for (k = 0; k < 6; k++) {
                ph[k] = wrap1(ph[k] + inc[k]);
                mt += sq(ph[k]);
            }
            lo = svf(v->dr_ca, v->dr_sa, mt * mg + nm * dnoise(&ns), &b, &h);
            lo = svf(v->dr_cb, v->dr_sb, b, &b, &h);
            (void)lo;
            if (tb) {                                /* the tambourine's second shake, 30 ms on */
                if (n == sp)
                    be = 1.0f;
                x[i] = g * (env + 0.8f * (n >= sp ? be : 0.0f)) * h;
                be *= bk;
                n++;
            } else
                x[i] = g * env * h;
            env *= ek;
        }
        if (tb)
            v->dr_be = be;
        for (k = 0; k < 6; k++)
            v->dr_ph[k] = ph[k];
        p0 = ph[0];
        p1 = ph[1];
        break;
    }
    case DM_COWB: {
        float f1 = fm_minf(f0 * 1.4815f * v->dr_xr, 0.45f);   /* (0.12: A RATIO, B RESO) */
        for (i = 0; i < CR; i++) {
            p0 = wrap1(p0 + f0);
            p1 = wrap1(p1 + f1);
            (void)svf(v->dr_ca, v->dr_sa, 0.5f * (sq(p0) + sq(p1)), &b, &h);
            x[i] = g * env * (0.55f + 0.45f * ne) * b;
            env *= ek;
            ne *= nk;
        }
        break;
    }
    case DM_SHAKR: case DM_MARAC: case DM_SDBRSH: {
        int br = v->dr_model == DM_SDBRSH;
        for (i = 0; i < CR; i++) {
            (void)svf(v->dr_ca, v->dr_sa, dnoise(&ns), &b, &h);
            x[i] = g * env * (1.0f - ne) * (br ? b : h);
            env *= ek;
            ne *= nk;
        }
        break;
    }
    case DM_H606:                                    /* (0.9) bright noise */
        uses_n = 0;
        for (i = 0; i < CR; i++) {
            (void)svf(v->dr_ca, v->dr_sa, dnoise(&ns), &b, &h);
            x[i] = g * env * h;
            env *= ek;
        }
        break;
    case DM_HHFM: {                                  /* (0.9) a carrier and a modulator 1.41 apart, the index falling */
        float f1 = fm_minf(f0 * 1.4142f * v->dr_xr, 0.45f), idx = (2.0f + 5.0f * tx) * v->dr_xb;   /* (0.12) */
        for (i = 0; i < CR; i++) {
            p0 = wrap1(p0 + f0);
            p1 = wrap1(p1 + f1);
            (void)svf(v->dr_cb, v->dr_sb, lks(p0 + idx * (0.3f + 0.7f * ne) * lks(p1)), &b, &h);
            x[i] = g * env * h;
            env *= ek;
            ne *= nk;
        }
        break;
    }
    default: {                                       /* NOISE */
        int hp = tx >= 0.5f;
        uses_n = 0;
        for (i = 0; i < CR; i++) {
            float lo = svf(v->dr_ca, v->dr_sa, dnoise(&ns), &b, &h);
            x[i] = g * env * (hp ? h : lo);
            env *= ek;
        }
        break;
    }
    }
    if (!uses_n)
        ne = 0.0f;
    v->dr_sa[0] = fm_flush(v->dr_sa[0]); v->dr_sa[1] = fm_flush(v->dr_sa[1]);
    v->dr_sb[0] = fm_flush(v->dr_sb[0]); v->dr_sb[1] = fm_flush(v->dr_sb[1]);
    v->dr_env = env;
    v->dr_ne = ne;
    v->dr_pe = pe;
    if (v->dr_model != DM_HHC && v->dr_model != DM_HHO && v->dr_model != DM_CYMB && v->dr_model != DM_TAMB &&
        v->dr_model != DM_H909C && v->dr_model != DM_H909O) {
        v->dr_ph[0] = p0;
        v->dr_ph[1] = p1;
    }
    v->dr_n += CR;
    v->va_ns = ns;
}

/* lku of a HARM blend that is not in its table yet: the table's two points as the blend writes them */
static inline float lkh(const int16_t *t0, float a, float b, uint32_t ph)
{
    uint32_t i = ph >> 24;
    float fr = (float)(int32_t)(ph & 0xFFFFFFu) * (1.0f / 16777216.0f);
    const int16_t *t = t0 + i;
    float w0 = (float)t[0] * a + (float)t[HARM_N + 1] * b, w1 = (float)t[1] * a + (float)t[HARM_N + 2] * b;
    return w0 + fr * (w1 - w0);
}
/* the operators' block. mode 0: the HARM shape from the voice's table; 1 / 2: the shape (on A and B1 / on C) blended
 * from its two tables at each lookup, the table's values exactly (while HARM moves: cheaper than the table) */
static inline __attribute__((always_inline)) void fm_block(voice_t *v, float *x, const int mode)
{
    uint32_t phC = v->ph[OC], phA = v->ph[OA], phB1 = v->ph[OB1], phB2 = v->ph[OB2];
    const uint32_t iC = v->inc[OC], iA = v->inc[OA], iB1 = v->inc[OB1], iB2 = v->inc[OB2];
    float a_n = v->idx_now[OA], a_d = (v->idx_tgt[OA] - a_n) * (1.0f / CRF);
    float b1_n = v->idx_now[OB1], b1_d = (v->idx_tgt[OB1] - b1_n) * (1.0f / CRF);
    float b2_n = v->idx_now[OB2], b2_d = (v->idx_tgt[OB2] - b2_n) * (1.0f / CRF);
    float eA = v->ev_now[0], eAd = (v->ev_tgt[0] - eA) * (1.0f / CRF);
    float eB = v->ev_now[1], eBd = (v->ev_tgt[1] - eB) * (1.0f / CRF);
    float fb_n = v->fb_now, fb_d = (v->fb_tgt - fb_n) * (1.0f / CRF);
    float wx = v->wx_now, wxd = (v->wx_tgt - wx) * (1.0f / CRF);
    float wy = v->wy_now, wyd = (v->wy_tgt - wy) * (1.0f / CRF);
    float f1 = v->fb1;
    const float *tC = v->harm_side < 0 ? wt_voice[v->wti] : sin_small;
    const float *tAB = v->harm_side > 0 ? wt_voice[v->wti] : sin_small;
    const harm_t *h = &harm_w[v->wti];
    const int16_t *h0 = h->t0;
    const float ha = h->a, hb = h->b;
    const float *tS = sin_small;
    int i;
#define LKC(ph) (mode == 2 ? lkh(h0, ha, hb, ph) : lku(tC, ph))
#define LKAB(ph) (mode == 1 ? lkh(h0, ha, hb, ph) : lku(tAB, ph))
    /* each algorithm in passes, one or two operators each, in the order they feed one another; every pass keeps
     * its own copies of the ramps it reads (the same sums from the same start: the same values as one loop), so
     * fewer values are live at once */
    switch (v->algo) {
    case 0: { /* 1: A(fb); B2 -> B1 raw; A -> C raw; B1 -> C raw. X = C, Y = B1 */
        float oa[CR], o1[CR];
        {
            float e = eA, fb = fb_n;
            for (i = 0; i < CR; i++) {
                float a = LKAB(phA + pmod(fb * f1));
                f1 = a * e;
                oa[i] = a;
                phA += iA; e += eAd; fb += fb_d;
            }
        }
        {
            float n = b2_n;
            for (i = 0; i < CR; i++) {
                float b2 = lku(tS, phB2);
                o1[i] = LKAB(phB1 + pmod(b2 * n));
                phB2 += iB2; phB1 += iB1; n += b2_d;
            }
        }
        {
            float na = a_n, n1 = b1_n, X = wx, Y = wy;
            for (i = 0; i < CR; i++) {
                float c = LKC(phC + pmod(oa[i] * na + o1[i] * n1));
                x[i] = c * X + o1[i] * Y;
                phC += iC; na += a_d; n1 += b1_d; X += wxd; Y += wyd;
            }
        }
        break;
    }
    case 1: { /* 2: B2(fb); B2 -> B1 env; A -> C env. X = C, Y = B1 */
        float o1[CR];
        {
            float e = eB, fb = fb_n, n = b2_n;
            for (i = 0; i < CR; i++) {
                float b2 = lku(tS, phB2 + pmod(fb * f1));
                o1[i] = LKAB(phB1 + pmod(b2 * e * n));
                f1 = b2 * e;
                phB2 += iB2; phB1 += iB1; e += eBd; fb += fb_d; n += b2_d;
            }
        }
        {
            float e = eA, n = a_n, X = wx, Y = wy;
            for (i = 0; i < CR; i++) {
                float a = LKAB(phA), c = LKC(phC + pmod(a * e * n));
                x[i] = c * X + o1[i] * Y;
                phA += iA; phC += iC; e += eAd; n += a_d; X += wxd; Y += wyd;
            }
        }
        break;
    }
    case 2: { /* 3: A(fb); A -> C raw, A -> B2 raw, A -> B1 env. X = C + B2, Y = B1 */
        float oa[CR], os[CR];
        {
            float e = eA, fb = fb_n;
            for (i = 0; i < CR; i++) {
                float a = LKAB(phA + pmod(fb * f1));
                f1 = a * e;
                oa[i] = a;
                phA += iA; e += eAd; fb += fb_d;
            }
        }
        {
            float n = a_n;
            for (i = 0; i < CR; i++) {
                uint32_t ar = pmod(oa[i] * n);
                float c = LKC(phC + ar), b2 = lku(tS, phB2 + ar);
                os[i] = c + b2;
                phC += iC; phB2 += iB2; n += a_d;
            }
        }
        {
            float e = eA, n = a_n, X = wx, Y = wy;
            for (i = 0; i < CR; i++) {
                float b1 = LKAB(phB1 + pmod(oa[i] * e * n));
                x[i] = os[i] * X + b1 * Y;
                phB1 += iB1; e += eAd; n += a_d; X += wxd; Y += wyd;
            }
        }
        break;
    }
    case 3: { /* 4: B2(fb); B2 -> B1 -> A -> C, all env. X = C, Y = B1 */
        float o2[CR], o1[CR];
        {
            float e = eB, fb = fb_n;
            for (i = 0; i < CR; i++) {
                float b2 = lku(tS, phB2 + pmod(fb * f1));
                f1 = b2 * e;
                o2[i] = b2;
                phB2 += iB2; e += eBd; fb += fb_d;
            }
        }
        {
            float e = eB, n = b2_n;
            for (i = 0; i < CR; i++) {
                o1[i] = LKAB(phB1 + pmod(o2[i] * e * n));
                phB1 += iB1; e += eBd; n += b2_d;
            }
        }
        {
            float e = eB, n = b1_n;
            for (i = 0; i < CR; i++) {
                o2[i] = LKAB(phA + pmod(o1[i] * e * n));   /* (A's output, in B2's place) */
                phA += iA; e += eBd; n += b1_d;
            }
        }
        {
            float e = eA, n = a_n, X = wx, Y = wy;
            for (i = 0; i < CR; i++) {
                float c = LKC(phC + pmod(o2[i] * e * n));
                x[i] = c * X + o1[i] * Y;
                phC += iC; e += eAd; n += a_d; X += wxd; Y += wyd;
            }
        }
        break;
    }
    case 4: { /* 5: B1(fb); B1 -> B2, B1 -> A, B2 -> A, A -> C, all env. X = C, Y = A */
        float om[CR], oa[CR];
        {
            float e = eB, fb = fb_n, n = b1_n;
            for (i = 0; i < CR; i++) {
                float b1 = LKAB(phB1 + pmod(fb * f1));
                om[i] = b1 * e * n;
                f1 = b1 * e;
                phB1 += iB1; e += eBd; fb += fb_d; n += b1_d;
            }
        }
        {
            float e = eB, n = b2_n;
            for (i = 0; i < CR; i++) {
                float b2 = lku(tS, phB2 + pmod(om[i]));
                oa[i] = LKAB(phA + pmod(om[i] + b2 * e * n));
                phB2 += iB2; phA += iA; e += eBd; n += b2_d;
            }
        }
        {
            float e = eA, n = a_n, X = wx, Y = wy;
            for (i = 0; i < CR; i++) {
                float c = LKC(phC + pmod(oa[i] * e * n));
                x[i] = c * X + oa[i] * Y;
                phC += iC; e += eAd; n += a_d; X += wxd; Y += wyd;
            }
        }
        break;
    }
    case 5: { /* 6: A(fb); A and B2 -> C and B1, all env. X = C, Y = B1 */
        float ot[CR];
        uint32_t mm[CR];
        {
            float e = eA, fb = fb_n, n = a_n;
            for (i = 0; i < CR; i++) {
                float a = LKAB(phA + pmod(fb * f1));
                ot[i] = a * e * n;
                f1 = a * e;
                phA += iA; e += eAd; fb += fb_d; n += a_d;
            }
        }
        {
            float e = eB, n = b2_n;
            for (i = 0; i < CR; i++) {
                float b2 = lku(tS, phB2);
                mm[i] = pmod(ot[i] + b2 * e * n);
                phB2 += iB2; e += eBd; n += b2_d;
            }
        }
        {
            float X = wx, Y = wy;
            for (i = 0; i < CR; i++) {
                float c = LKC(phC + mm[i]), b1 = LKAB(phB1 + mm[i]);
                x[i] = c * X + b1 * Y;
                phC += iC; phB1 += iB1; X += wxd; Y += wyd;
            }
        }
        break;
    }
    case 6: { /* 7: A(fb); A -> C env, B2 -> B1 env. X = C + A env, Y = B1 env + B2 env */
        float os[CR];
        {
            float e = eA, fb = fb_n, n = a_n;
            for (i = 0; i < CR; i++) {
                float a = LKAB(phA + pmod(fb * f1)), c = LKC(phC + pmod(a * e * n));
                f1 = a * e;
                os[i] = c + a * e;
                phA += iA; phC += iC; e += eAd; fb += fb_d; n += a_d;
            }
        }
        {
            float e = eB, n = b2_n, X = wx, Y = wy;
            for (i = 0; i < CR; i++) {
                float b2 = lku(tS, phB2), b1 = LKAB(phB1 + pmod(b2 * e * n));
                x[i] = os[i] * X + (b1 + b2) * e * Y;
                phB2 += iB2; phB1 += iB1; e += eBd; n += b2_d; X += wxd; Y += wyd;
            }
        }
        break;
    }
    default: { /* 8: B1(fb); A -> C env. X = C + B2 env, Y = B1 env */
        float o1[CR], oc[CR];
        {
            float e = eB, fb = fb_n;
            for (i = 0; i < CR; i++) {
                float b1 = LKAB(phB1 + pmod(fb * f1));
                f1 = b1 * e;
                o1[i] = b1;
                phB1 += iB1; e += eBd; fb += fb_d;
            }
        }
        {
            float e = eA, n = a_n;
            for (i = 0; i < CR; i++) {
                float a = LKAB(phA);
                oc[i] = LKC(phC + pmod(a * e * n));
                phA += iA; phC += iC; e += eAd; n += a_d;
            }
        }
        {
            float e = eB, X = wx, Y = wy;
            for (i = 0; i < CR; i++) {
                float b2 = lku(tS, phB2);
                x[i] = (oc[i] + b2 * e) * X + o1[i] * e * Y;
                phB2 += iB2; e += eBd; X += wxd; Y += wyd;
            }
        }
        break;
    }
    }
#undef LKC
#undef LKAB
    v->ph[OC] = phC;
    v->ph[OA] = phA;
    v->ph[OB1] = phB1;
    v->ph[OB2] = phB2;
    v->fb1 = f1;
    v->idx_now[OA] = v->idx_tgt[OA];
    v->idx_now[OB1] = v->idx_tgt[OB1];
    v->idx_now[OB2] = v->idx_tgt[OB2];
    v->ev_now[0] = v->ev_tgt[0];
    v->ev_now[1] = v->ev_tgt[1];
    v->fb_now = v->fb_tgt;
    v->wx_now = v->wx_tgt;
    v->wy_now = v->wy_tgt;
}

/* (apart: voice_block's own frame and registers stay as the other engines need them) */
static __attribute__((noinline)) void fm_block0(voice_t *v, float *x) { fm_block(v, x, 0); }
static __attribute__((noinline)) void fm_block1(voice_t *v, float *x) { fm_block(v, x, 1); }
static __attribute__((noinline)) void fm_block2(voice_t *v, float *x) { fm_block(v, x, 2); }

void voice_block(voice_t *v, float *dl, float *dr, float *sc, float *sd, float *srv)
{
    float x[CR];
    if (v->wait)                                     /* (not sounding yet) */
        return;
#ifdef PROF_NO_BLOCK
    return;
#endif
    if (v->eng == ENG_VA)
        va_block(v, x);
    else if (v->eng == ENG_DR)
        dr_block(v, x);
    else {
        harm_t *h = &harm_w[v->wti];
        if (h->pend && v->harm_side != 0) {
            if (h->pend >= 2) {                      /* it stood still a block: into the table */
                float *w = wt_voice[v->wti], a = h->a, b = h->b;
                const int16_t *t0 = h->t0, *t1 = h->t0 + HARM_N + 1;
                int j;
                for (j = 0; j <= HARM_N; j++)
                    w[j] = (float)t0[j] * a + (float)t1[j] * b;
                h->pend = 0;
            } else
                h->pend = 2;
        }
        if (h->pend && v->harm_side > 0)
            fm_block1(v, x);
        else if (h->pend && v->harm_side < 0)
            fm_block2(v, x);
        else
            fm_block0(v, x);
    }

    /* drive [FIRMWARE] (bypassed at DRV 0); LO-FI [TONIC]: bit reduction, then sample-rate reduction (the filters
     * after it can tame its aliasing). With the sample-rate reduction on, drive and bit reduction (no state) work
     * only on the samples it holds: the same values, without the ones it drops */
    if (v->lf_inc > 0.0f) {
        int i;
        float ph = v->lf_ph, h = v->lf_hold, inc = v->lf_inc, g2 = 2.0f * v->drive_g, q = v->lf_q, iq = v->lf_iq;
        int drv = v->drive_g > 0.0f, brr = q > 0.0f;
        for (i = 0; i < CR; i++) {
            ph += inc;
            if (ph >= 1.0f) {
                float y = x[i];
                ph -= 1.0f;
                if (drv)
                    y = 0.5f * f_dr1(f_dr1(f_dr(y * g2)));
                if (brr)
                    y = fm_floorf(y * iq + 0.5f) * q;
                h = y;
            }
            x[i] = h;
        }
        v->lf_ph = ph;
        v->lf_hold = h;
    } else {
        if (v->drive_g > 0.0f) {
            int i;
            float g2 = 2.0f * v->drive_g;
            for (i = 0; i < CR; i++)                 /* (after the first stage |u| <= 1: no clamps needed, the same
                                                      * values, +-1 included) */
                x[i] = 0.5f * f_dr1(f_dr1(f_dr(x[i] * g2)));
        }
        if (v->lf_q > 0.0f) {
            int i;
            float q = v->lf_q, iq = v->lf_iq;
            for (i = 0; i < CR; i++)
                x[i] = fm_floorf(x[i] * iq + 0.5f) * q;
        }
    }
    /* base-width [FIRMWARE]: one-pole lowpass at BASE + WIDTH, then one-pole highpass at BASE */
    if (v->bw_on) {
        int i;
        float lp = v->bw_lp, hp = v->bw_hp, lpp = v->bw_lpp, c1 = v->bw_ghp, c2 = v->bw_glp, h = (1.0f + c1) * 0.5f;
        for (i = 0; i < CR; i++) {
            lp = c2 * lp + (1.0f - c2) * x[i];
            hp = c1 * hp + h * (lp - lpp);
            lpp = lp;
            x[i] = hp;
        }
        v->bw_lp = fm_flush(lp);
        v->bw_hp = fm_flush(hp);
        v->bw_lpp = lpp;
    }
    /* the multimode filter: LP2, HP (= input - LP2), LP4 */
    if (v->ftype == 1 || v->ftype == 2) {
        int i, lpo = v->ftype == 1;
        float s1 = v->s1, s2 = v->s2, fg = v->fg, fk2 = 2.0f * v->fk + v->fg, fr = v->fr;
        for (i = 0; i < CR; i++) {
            float h = (x[i] - fk2 * s1 - s2) * fr, b = fg * h + s1, l = fg * b + s2;
            s1 = fg * h + b;
            s2 = fg * b + l;
            x[i] = lpo ? l : x[i] - l;
        }
        v->s1 = fm_flush(s1);
        v->s2 = fm_flush(s2);
    } else if (v->ftype == 3) {
        int i;
        float s1 = v->s1, s2 = v->s2, s3 = v->s3, s4 = v->s4, G = v->fr, fk = v->fk, inv = v->c_r;
        float G2 = G * G, G3 = G2 * G, G4 = G2 * G2, om = 1.0f - G, comp = 1.0f + fk;
        for (i = 0; i < CR; i++) {
            float xin = x[i] * comp, S = (G3 * s1 + G2 * s2 + G * s3 + s4) * om;
            float y4 = (G4 * xin + S) * inv, u = xin - softc(fk * y4), vv;
            vv = (u - s1) * G; u = vv + s1; s1 = u + vv;
            vv = (u - s2) * G; u = vv + s2; s2 = u + vv;
            vv = (u - s3) * G; u = vv + s3; s3 = u + vv;
            vv = (u - s4) * G; u = vv + s4; s4 = u + vv;
            x[i] = u;
        }
        v->s1 = fm_flush(s1);
        v->s2 = fm_flush(s2);
        v->s3 = fm_flush(s3);
        v->s4 = fm_flush(s4);
    }
    /* (0.12) the insert effect: the wet signal a type at a time, then the DC blocker, TONE and MIX where they are on */
    if (v->in_typ) {
        int i, ty = v->in_typ;
        float g = v->in_g, c = v->in_c, tg = v->in_tg, mx = v->in_mix, w[CR];
        switch (ty) {
        case INS_OVER: for (i = 0; i < CR; i++) w[i] = softc(g * x[i]) * c; break;
        case INS_HARD:
            for (i = 0; i < CR; i++) {
                float u = g * x[i];
                w[i] = (u > 1.0f ? 1.0f : u < -1.0f ? -1.0f : u) * c;
            }
            break;
        case INS_FUZZ: {
            float bias = v->in_b, sb = softc(bias);
            for (i = 0; i < CR; i++)
                w[i] = (softc(g * x[i] + bias) - sb) * c;
            break;
        }
        case INS_RECT: for (i = 0; i < CR; i++) w[i] = softc(g * fm_fabsf(x[i])) * c; break;
        case INS_FOLD:
            for (i = 0; i < CR; i++) {
                float u = g * x[i] * 0.25f + 0.25f;
                u -= fm_floorf(u);
                w[i] = 4.0f * fm_fabsf(u - 0.5f) - 1.0f;
            }
            break;
        default: {                                   /* CRUSH */
            float h = v->in_hold, q = v->in_q, iq = 1.0f / q;
            int n = v->in_n, len = v->in_len;
            for (i = 0; i < CR; i++) {
                if (n <= 0) {
                    h = fm_floorf(x[i] * iq + 0.5f) * q;
                    n = len;
                }
                n--;
                w[i] = h;
            }
            v->in_hold = h;
            v->in_n = (uint8_t)(n < 0 ? 0 : n);
            break;
        }
        }
        if (ty == INS_FUZZ || ty == INS_RECT) {      /* (their DC away) */
            float dc = v->in_dc;
            for (i = 0; i < CR; i++) {
                dc += 0.003f * (w[i] - dc);
                w[i] -= dc;
            }
            v->in_dc = fm_flush(dc);
        }
        if (tg > 0.0f) {
            float lp = v->in_lp;
            for (i = 0; i < CR; i++) {
                lp += tg * (w[i] - lp);
                w[i] = lp;
            }
            v->in_lp = fm_flush(lp);
        }
        if (mx >= 1.0f)
            for (i = 0; i < CR; i++)
                x[i] = w[i];
        else
            for (i = 0; i < CR; i++)
                x[i] = x[i] + mx * (w[i] - x[i]);
    }
    /* amp, pan, sends */
    {
        int i;
        float an = v->amp_now, ad = (v->amp_tgt - an) * (1.0f / CRF), peak;
        float pl = v->panl * 2.0f, pr = v->panr * 2.0f, cs = v->snd_c * 2.0f, ds = v->snd_d * 2.0f, rs = v->snd_r * 2.0f;
        if (cs + ds + rs > 0.0f) {
            for (i = 0; i < CR; i++) {
                float y = x[i] * an;
                an += ad;
                dl[i] += y * pl;
                dr[i] += y * pr;
                sc[i] += y * cs;
                sd[i] += y * ds;
                srv[i] += y * rs;
            }
        } else {
            for (i = 0; i < CR; i++) {
                float y = x[i] * an;
                an += ad;
                dl[i] += y * pl;
                dr[i] += y * pr;
            }
        }
        peak = fm_fabsf(x[CR - 1] * an) + fm_fabsf(x[CR / 2] * an);
        v->amp_now = v->amp_tgt;
        v->meter = peak * 4.0f;
    }
}

/* ------------------------------------------------------------------ note on / off --- */
static inline int locked(const voice_t *v, int p) { return (v->locked[p >> 5] >> (p & 31)) & 1u; }

/* start (or retrigger) a voice. retrig: the voice was sounding the same track (mono, reuse, legato): the
 * envelopes follow their RESET settings. */
void voice_start(voice_t *v, int trk, int note, int vel, const sound_t *s, const plock_t *locks, int nlock,
                 int lock_key, int retrig, int filter_trig, int lfo_trig, int porta)
{
    int i, legato = retrig == 2, same = v->on && v->snd == s;
    uint32_t was[(NSP + 31) / 32];
    if (v->eng != s->s[S_ENGINE])                    /* another engine: its state from zero */
        voice_engine(v, s->s[S_ENGINE]);
    for (i = 0; i < NSP; i++)
        v->p[i] = s->p[i];
    for (i = 0; i < (NSP + 31) / 32; i++)
        was[i] = v->locked[i], v->locked[i] = 0;
    for (i = 0; i < nlock; i++) {                    /* the trig's locks */
        plock_t l = locks[i];
        if (l.f.step == lock_key && l.f.param < NSP) {
            v->p[l.f.param] = l.f.val;
            v->locked[l.f.param >> 5] |= 1u << (l.f.param & 31);
        }
    }
    /* [FIRMWARE] the smoothing goes on through a note-on; it is set at once for another sound on the voice and for
     * the parameters locked on this trig or the one before (a voice that was silent starts on its values: INFERRED) */
    for (i = 0; i < NSP; i++)
        if (!same || ((v->locked[i >> 5] | was[i >> 5]) >> (i & 31) & 1u))
            v->ps[i] = (float)v->p[i] * P_INV[i];
    v->gliding = 1;
    if (!retrig) {
        v->on = 1;
        if (v->eng == ENG_FM) {
            v->ph[0] = v->ph[1] = v->ph[2] = v->ph[3] = 0;
            v->fb1 = 0;
            v->harm_side = 9;                        /* rebuild the HARM shape */
        }
        v->ea.lvl = v->eb.lvl = v->ef.lvl = v->eamp.lvl = v->em.lvl = 0;
        v->in_lp = v->in_dc = v->in_hold = 0;
        v->in_n = 0;
        v->s1 = v->s2 = v->s3 = v->s4 = v->bw_hp = v->bw_lp = v->bw_lpp = 0;
        v->lf_ph = 1.0f;                             /* LO-FI: the first sample is taken at once */
        v->lf_hold = 0;
        v->amp_now = 0;
    }
    v->trk = (uint8_t)trk;
    v->snd = s;
    v->gate = 1;
    v->stolen = 0;
    v->wait = 0;
    v->steal_g = 1.0f;
    v->vel = (uint8_t)vel;
    v->vel01 = (float)vel * (1.0f / 127.0f);
    /* portamento [MANUAL 9.5.5]: from the track's last note (TRACK, LEGATO ONLY) or the voice's own (VOICE) */
    {
        float from = s->s[S_PTYPE] == 1 ? (v->on && retrig ? v->pitch : (float)note) :
                     ts[trk].last_note >= 0 ? (float)ts[trk].last_note : (float)note;
        if (s->s[S_PTYPE] == 2 && !legato)
            porta = 0;
        v->pitch_from = from;
        v->pitch = (float)note;
        if (porta && from != (float)note) {
            float amt = (float)s->s[S_PAMT] * 0.01f;
            float tm = porta_time((float)porta);
            v->pitch_from = (float)note + (from - (float)note) * amt;
            /* CONSTANT RATE: the time grows with the interval (the PTIM time per octave) */
            v->glide_len = s->s[S_PSLOPE] ? tm : tm * fm_fabsf(v->pitch_from - (float)note) * (1.0f / 12.0f);
            v->glide_t = 0;
        } else
            v->glide_len = 0;
    }
    v->note = (uint8_t)note;
    {   /* key scaling [FIRMWARE]: k = 1 - t KS / 128, t from 0 (C1, note 24) to 1 (C7, note 96), linear in Hz */
        float hz = 440.0f * fm_exp2f(((float)note - 69.0f) * (1.0f / 12.0f));
        float tt = fm_clampf((hz - 32.703f) * (1.0f / (2093.0f - 32.703f)), 0, 1);
        v->ks[0] = (127.0f - tt * (float)s->s[S_KSA]) * (1.0f / 128.0f);   /* [FIRMWARE] */
        v->ks[1] = (127.0f - tt * (float)s->s[S_KSB1]) * (1.0f / 128.0f);
        v->ks[2] = (127.0f - tt * (float)s->s[S_KSB2]) * (1.0f / 128.0f);
    }
    v->c_pitch = v->c_dtun = v->c_fc = v->c_res = v->c_base = v->c_top = v->c_pan = v->c_srr = v->c_brr = -1e9f;
    v->c_ftype = 255;
    v->ea.in_a = v->ea.in_d = v->ea.in_r = v->ea.in_l = -1e9f;
    v->eb = v->eb; v->eb.in_a = v->eb.in_d = v->eb.in_r = v->eb.in_l = -1e9f;
    v->ef.in_a = v->ef.in_d = v->ef.in_r = v->ef.in_l = -1e9f;
    v->em.in_a = v->em.in_d = v->em.in_r = v->em.in_l = -1e9f;
    v->in_c2 = -1e9f;
    v->eamp.in_a = v->eamp.in_d = v->eamp.in_r = v->eamp.in_l = -1e9f;
    /* phase reset [MANUAL 11.6.4]: OFF, ALL, C, A+B, A+B2 */
    switch (v->eng == ENG_FM ? v->p[P_PHRT] : 0) {
    case 1: v->ph[0] = v->ph[1] = v->ph[2] = v->ph[3] = 0; break;
    case 2: v->ph[0] = 0; break;
    case 3: v->ph[1] = v->ph[2] = v->ph[3] = 0; break;
    case 4: v->ph[1] = v->ph[3] = 0; break;
    default: break;
    }
    if (s->s[S_ENGINE] == ENG_DR) {                  /* DRUM: every hit from the start, no glide */
        v->glide_len = 0;
        v->pitch_from = v->pitch;
        dr_start(v);
    }
    if (s->s[S_ENGINE] == ENG_VA && v->p[P_PHRT])    /* VA: RST ON starts the oscillators at 0 */
        v->va_ph[0] = v->va_ph[1] = v->va_ph[2] = 0;
    if (v->p[P_PHRT])                                /* [FIRMWARE] with a phase reset the filter starts clear too */
        v->s1 = v->s2 = v->s3 = v->s4 = 0;
    /* envelopes [FIRMWARE]: the operator envelopes latch ATK, DEC and the delay now; a retrigger restarts them from
     * zero with ARST / BRST ON (and always after a delay), else the attack goes on from where it is. The amp envelope
     * likewise with AENR; the filter envelope always from zero (when FLT.T triggers it). Legato: none. */
    if (!legato) {
        op_latch(&v->ea, op_delay(v->ps[P_ADEL], eng.bpm), v->ps[P_AATK], v->ps[P_ADEC]);
        op_latch(&v->eb, op_delay(v->ps[P_BDEL], eng.bpm), v->ps[P_BATK], v->ps[P_BDEC]);
        v->latch = 1;                                /* (again with their modulation, at the first control step) */
        if (!retrig || v->p[P_ARST] || v->p[P_ADEL])
            v->ea.lvl = 0;
        if (!retrig || v->p[P_BRST] || v->p[P_BDEL])
            v->eb.lvl = 0;
        v->ea.stage = v->p[P_ADEL] ? 1 : 2;
        v->eb.stage = v->p[P_BDEL] ? 1 : 2;
        v->ea.t = v->eb.t = 0;
        if (filter_trig) {
            v->ef.lvl = 0;
            v->ef.stage = v->p[P_FDEL] ? 1 : 2;
            v->ef.t = 0;
        } else if (!retrig)
            v->ef.stage = 0;                         /* FLT.T OFF: the filter envelope stays down */
        v->em.lvl = 0;                               /* (0.12) the modulation envelope: every note */
        v->em.stage = v->p[P_MDEL] ? 1 : 2;
        v->em.t = 0;
        if (!retrig || v->p[P_AENR])
            v->eamp.lvl = 0;
        v->eamp.stage = 2;
        v->eamp.t = 0;
    }
    if (s->s[S_PLAYMODE] == PM_POLY_MLFO) {          /* POLY M.LFO [MANUAL 9.5.6]: the track's first voice's LFOs */
        voice_t *first = 0;
        for (i = 0; i < NVOICE; i++)
            if (&vc[i] != v && vc[i].on && vc[i].trk == trk && (!first || vc[i].age < first->age))
                first = &vc[i];
        if (first) {
            v->lfo[0] = first->lfo[0];
            v->lfo[1] = first->lfo[1];
            lfo_trig = 0;
        }
    }
    if (lfo_trig) {
        for (i = 0; i < 2; i++) {
            int b = i ? P_L2SPD : P_L1SPD;
            if (1)
                lfo_restart(v, i, v->p[b + 6], (float)v->p[b + 5], (float)v->p[b + 2], v->p[b + 4]);
        }
    }
    ts[trk].last_note = (int8_t)note;
    voice_control(v);                                /* targets now: the first block starts on them */
    if (v->eng == ENG_FM) {
        v->idx_now[0] = v->idx_tgt[0];
        v->idx_now[1] = v->idx_tgt[1];
        v->idx_now[2] = v->idx_tgt[2];
        v->idx_now[3] = v->idx_tgt[3];
        v->ev_now[0] = v->ev_tgt[0];
        v->ev_now[1] = v->ev_tgt[1];
        v->fb_now = v->fb_tgt;
        v->wx_now = v->wx_tgt;
        v->wy_now = v->wy_tgt;
    }
}

void voice_release(voice_t *v) { v->gate = 0; }
void voice_kill(voice_t *v) { v->stolen = 1; }

/* a control step for every sounding voice (engine.c calls it each block) */
void synth_control(void)
{
    int i;
    for (i = 0; i < NVOICE; i++)
        if (vc[i].on)
            voice_control(&vc[i]);
}

/* a knob turned: the track's voices follow, except where the voice has the parameter locked */
void synth_param(int trk, int p, int val)
{
    int i;
    for (i = 0; i < NVOICE; i++)
        if (vc[i].on && vc[i].trk == trk && !locked(&vc[i], p)) {
            vc[i].p[p] = (int16_t)val;
            vc[i].gliding = 1;
        }
}

void synth_sound(int trk)
{
    pattern_t *pt = eng_pat;
    int i, p;
    if (!pt)
        return;
    for (i = 0; i < NVOICE; i++)
        if (vc[i].on && vc[i].trk == trk && vc[i].snd == &pt->snd[trk]) {
            for (p = 0; p < NSP; p++)
                if (!locked(&vc[i], p))
                    vc[i].p[p] = pt->snd[trk].p[p];
            vc[i].gliding = 1;
        }
}

/* a lock trig: the track's sounding voices take its locks, at once [FIRMWARE: a p-lock resets the smoothing] */
void synth_lock(int trk, const plock_t *locks, int nlock, int lock_key)
{
    int i, k;
    for (i = 0; i < NVOICE; i++)
        if (vc[i].on && vc[i].trk == trk)
            for (k = 0; k < nlock; k++) {
                plock_t l = locks[k];
                if (l.f.step == lock_key && l.f.param < NSP) {
                    vc[i].p[l.f.param] = l.f.val;
                    vc[i].ps[l.f.param] = (float)l.f.val * P_INV[l.f.param];
                    vc[i].locked[l.f.param >> 5] |= 1u << (l.f.param & 31);
                }
            }
}
