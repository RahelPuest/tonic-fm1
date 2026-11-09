/* SPDX-License-Identifier: MPL-2.0
 * TONIC effects [MANUAL 4.2, 13]: send effects chorus -> (delay, reverb), delay -> reverb, reverb; their returns
 * into the mix; master overdrive at the very end; pattern volume. Pattern-level parameters (eng_pat->fx).
 *   chorus  two modulated taps (L / R in quadrature), input highpass, stereo width
 *   delay   one line, tempo-relative time (1/128 notes), ping-pong or a pan position, HPF and LPF in the feedback
 *           path with a soft limit (FDBK past unity swells but stays bounded); time changes glide (tape-like)
 *   reverb  a plate (Dattorro's figure-eight tank) at half the sample rate, pre-delay, decay, a shelving damper
 *           in the loop, input HPF / LPF
 *   [TONIC] a DJ filter on the master (after the overdrive) and sidechain ducking of the returns
 * Tunings marked [APPROX] are by design here: the manual gives the parameters, not their curves. */
#include "eng_int.h"

/* ---------------------------------------------------------------------- buffers --- */
#define CH_N 2048                 /* chorus line (46 ms): the index ring (its arithmetic as it was) */
#define CH_P 1024                 /* its storage: the taps reach at most 12 + 6 ms (794 samples) back [TONIC 0.10.1] */
static float *ch_buf;              /* (0.17: in the engine's pool, RAM is for code and state) */
static uint32_t ch_w;
static uint16_t *dl_buf;          /* delay line (0.17: half floats, as the reverb's lines: the repeats' grain falls with them) */
static uint32_t dl_n, dl_w;
/* reverb at SR / 2: Dattorro's plate, every line in one ring (the delay-memory style of the reverb chips): the
 * write point moves down one place a sample and each line is a fixed span of the ring from it, so a read or a
 * write is an add, a mask and a load. Lengths: Dattorro's (at 29761 Hz), scaled to fit 16384 samples. */
#define RV_N 16384u
#define RV_M (RV_N - 1u)
#define RV_SC 0.715f
enum { RV_PRE = 5520 };           /* pre-delay up to 250 ms at 22.05 kHz (the longest read: 5512) [0.13: 5520] */
static const uint16_t RV_LEN0[12] = {142, 107, 379, 277,           /* input diffusers */
                                     672, 4453, 1800, 3720,        /* tank left: allpass, delay, allpass, delay */
                                     908, 4217, 2656, 3163};       /* tank right */
static uint16_t rv_len[12], rv_off[12];   /* each line's span: [off, off + len) from the write point */
static uint16_t *rv_ring;         /* the plate's lines, as half floats [0.14: 16 bits, 32 KB to the BRAIDS voices] */
static int16_t *rv_pre;          /* pre-delay, x 8192 (headroom 4) (0.17: in the engine's pool) */
static uint32_t rv_w, rv_prew;
/* Dattorro's output taps (line, samples back at 29.8 kHz, sign), scaled at init */
static const int16_t TAPS[14][2] = {{9, 197}, {9, 2213}, {10, -1393}, {11, 1478}, {5, -1478}, {6, -138}, {7, -793},
                                    {5, 262}, {5, 2652}, {6, -930}, {7, 2110}, {9, -2209}, {10, -140}, {11, -79}};
static uint16_t tap_back[14];

static struct {
    /* chorus */
    float c_ph, c_inc, c_depth, c_hp_g, c_hp_s, c_wid, c_del, c_rev, c_vol;
    /* delay */
    float d_time, d_time_tgt, d_fb, d_hp_g, d_lp_g, d_hp_s, d_lp_s, d_rev, d_vol, d_wid, d_gl, d_gr;
    float d_rev0, d_vol0;         /* (0.23) as set; d_rev, d_vol: times fx_dg (the line lent to PERFORM) */
    int d_pp;
    /* reverb */
    float r_pre, r_dec, r_damp_g, r_shelf, r_hp_g, r_lp_g, r_hp_s, r_lp_s, r_vol;
    float r_damp_s[2], r_tank[2], r_lfo, r_in_prev, r_out_prev[2], r_out_l, r_out_r;
    int r_half;
    /* master */
    float movd, pvol;
    /* the DJ filter [TONIC]: a state-variable filter (TPT), lowpass below 0, highpass above; its smoothed knob */
    float mf_tgt, mf_s, mf_a1, mf_a2, mf_a3, mf_ic1[2], mf_ic2[2];
    int mf_mode;                  /* 0 bypass, -1 lowpass, 1 highpass */
    /* sidechain ducking of the returns (engine.c sets the gain each block) */
    float duck_now, duck_tgt;
} f;

void fx_duck(float g) { f.duck_tgt = g; }
static float fx_thr;
void fx_throw(float t) { fx_thr = t; }   /* (0.18) THROW: the dry mix into the delay and the reverb (PERFORM K3) */

static float fz_g = 1.0f, fz_tgt = 1.0f;   /* FREEZE: the reverb's input gain, its target */
static float fx_dg = 1.0f;          /* (0.23) the delay's return and reverb send while its line is lent (0 lent) */
static uint8_t fx_lend;             /* (0.23) the line lent to PERFORM's second bank: the delay neither reads nor writes */
void fx_freeze(int on) { fz_tgt = on ? 0.0f : 1.0f; }
static int16_t fxp_last[NFXP];      /* the values the coefficients were made from (fx_params) */
static float fxp_bpm = -1.0f;       /* -1: none yet */
void fx_init(void *pool, uint32_t *used, uint32_t pool_bytes)
{
    int i;
    fxp_bpm = -1.0f;
    uint8_t *p = (uint8_t *)pool;
    {
        uint32_t o = 1;
        for (i = 0; i < 12; i++) {
            rv_len[i] = (uint16_t)((float)RV_LEN0[i] * RV_SC) + 1;
            rv_off[i] = (uint16_t)o;
            o += rv_len[i];
        }
    }
    rv_ring = (uint16_t *)(p + *used);
    *used += RV_N * sizeof(uint16_t);
    ch_buf = (float *)(p + *used);                   /* (0.17: the chorus line and the pre-delay from the pool) */
    *used += CH_P * sizeof(float);
    rv_pre = (int16_t *)(p + *used);
    *used += (RV_PRE * sizeof(int16_t) + 3u) & ~3u;
    for (i = 0; i < 14; i++) {
        int b = TAPS[i][1] < 0 ? -TAPS[i][1] : TAPS[i][1];
        tap_back[i] = (uint16_t)((uint32_t)((float)b * RV_SC) % rv_len[TAPS[i][0]]);
    }
    /* the delay takes what is left (in whole KB) */
    dl_n = (pool_bytes - *used) / sizeof(uint16_t);
    dl_n &= ~1023u;
    dl_buf = (uint16_t *)(p + *used);
    *used += dl_n * sizeof(uint16_t);
    fx_clear();
}

void fx_clear(void)
{
    uint32_t i, k;
    for (i = 0; i < CH_P; i++)
        ch_buf[i] = 0;
    for (i = 0; i < dl_n; i++)
        dl_buf[i] = 0;
    for (i = 0; i < RV_N; i++)
        rv_ring[i] = 0;
    (void)k;
    for (i = 0; i < RV_PRE; i++)
        rv_pre[i] = 0;
    f.c_hp_s = f.d_hp_s = f.d_lp_s = f.r_hp_s = f.r_lp_s = 0;
    f.r_damp_s[0] = f.r_damp_s[1] = f.r_tank[0] = f.r_tank[1] = 0;
    f.r_out_prev[0] = f.r_out_prev[1] = 0;
    f.mf_ic1[0] = f.mf_ic1[1] = f.mf_ic2[0] = f.mf_ic2[1] = 0;
    f.duck_now = f.duck_tgt = 1.0f;
}

/* (0.17) the coefficient for s += a (x - s) at rate fs: its corner where the knob says (the TPT form's g / (1 + g) here
 * put it an octave low; the reverb's damper, at half the rate, two octaves) */
static inline __attribute__((always_inline)) float onepole_a(float hz, float fs) { return 1.0f - fm_expf(-FM_TWO_PI * fm_minf(hz, 0.45f * fs) / fs); }
/* [APPROX] the effects' filter knobs 0..127: 20 Hz .. 20 kHz, exponential */
static inline __attribute__((always_inline)) float knob_hz(float v) { return 20.0f * fm_exp2f(fm_clampf(v, 0, 127) * (9.966f / 127.0f)); }

/* the pattern's FX parameters (each control block; fx: P_CDEP.. in display units) */
void fx_params(const int16_t *fx, float bpm)
{
#define FX(p) ((float)fx[(p) - P_CDEP] / (float)PDESC[p].q)
    float dep = fm_clampf(FX(P_CDEP), 0.0f, 127.0f);   /* (the chorus line's reach: within 127) */
    int i, same = bpm == fxp_bpm;
    for (i = 0; i < NFXP && same; i++)               /* nothing moved since the last block: the coefficients stand */
        same = fx[i] == fxp_last[i];
    if (same)
        goto djf;
    for (i = 0; i < NFXP; i++)
        fxp_last[i] = fx[i];
    fxp_bpm = bpm;
    /* chorus [APPROX]: rate 0.03..6 Hz, depth up to 6 ms around a 12 ms centre */
    f.c_inc = 0.03f * fm_exp2f(FX(P_CSPD) * (7.6f / 127.0f)) * CRF * INV_SR;
    f.c_depth = dep * (1.0f / 127.0f) * 0.006f * SR;
    f.c_hp_g = onepole_a(knob_hz(FX(P_CHPF) * 0.7f), SR);
    f.c_wid = FX(P_CWID) * (1.0f / 127.0f);
    f.c_del = FX(P_CDEL) * (1.0f / 127.0f);
    f.c_rev = FX(P_CREV) * (1.0f / 127.0f);
    f.c_vol = FX(P_CVOL) * (1.0f / 100.0f);
    /* delay [MANUAL: TIME in 128th notes of the tempo; FDBK 0..198; APPROX: 127 = unity] */
    {
        float t = FX(P_DTIM) * (240.0f / fm_maxf(bpm, 20.0f)) * (1.0f / 128.0f) * (float)SR;
        f.d_time_tgt = fm_clampf(t, 16.0f, (float)dl_n - 4.0f);
    }
    f.d_fb = FX(P_DFB) * (1.0f / 127.0f);
    f.d_hp_g = onepole_a(knob_hz(FX(P_DHPF) * 0.75f), SR);
    f.d_lp_g = onepole_a(knob_hz(FX(P_DLPF)), SR);
    f.d_rev0 = FX(P_DREV) * (1.0f / 127.0f);
    f.d_vol0 = FX(P_DVOL) * (1.0f / 100.0f);
    f.d_wid = FX(P_DWID) * (1.0f / 64.0f);
    f.d_pp = fx[P_DPP - P_CDEP] != 0;
    if (f.d_pp)                                      /* ping-pong reads twice TIME back: within the line [0.9.2] */
        f.d_time_tgt = fm_minf(f.d_time_tgt, ((float)dl_n - 4.0f) * 0.5f);
    if (f.d_pp) {                                    /* ping-pong: WID sets how far the repeats swing */
        float w = fm_fabsf(f.d_wid);
        f.d_gl = 0.5f + 0.5f * w;
        f.d_gr = 0.5f - 0.5f * w;
    } else {                                         /* a pan position, constant power */
        float pan = 0.5f + 0.5f * f.d_wid;
        f.d_gl = lks(0.25f * (1.0f - pan));
        f.d_gr = lks(0.25f * pan);
    }
    /* reverb [APPROX]: pre-delay 0..250 ms; DEC 1..126 -> the tank's decay gain (RT60 ~0.3 .. 25 s), 127 freeze */
    f.r_pre = FX(P_RPRE) * (1.0f / 127.0f) * 0.25f * 22050.0f;
    {
        float dv = FX(P_RDEC);
        f.r_dec = dv >= 127.0f ? 0.99999994f : 0.35f + 0.645f * (1.0f - fm_exp2f(-dv * (4.6f / 127.0f)));
    }
    /* the loop's shelving damper: above FREQ the treble is cut by (1 - GAIN); GAIN 127 keeps it all */
    f.r_damp_g = onepole_a(knob_hz(FX(P_RFRQ)), 0.5f * SR);   /* (the tank runs at half the rate) */
    f.r_shelf = FX(P_RGAI) * (1.0f / 127.0f);
    f.r_hp_g = onepole_a(knob_hz(FX(P_RHPF)), SR);
    f.r_lp_g = onepole_a(knob_hz(FX(P_RLPF)), SR);
    f.r_vol = FX(P_RVOL) * (1.0f / 100.0f);
    /* master overdrive [MANUAL 13.5.1; APPROX curve], pattern volume */
    f.movd = FX(P_MOVD) * (1.0f / 127.0f);
    f.pvol = 1.6384f * vol_gain(FX(P_PVOL));        /* [FIRMWARE] 1.6384 (PVOL / 128)^2: unity at 100 */
djf:
    f.d_rev = f.d_rev0 * fx_dg;                      /* (0.23: 1 but while PERFORM borrows the line) */
    f.d_vol = f.d_vol0 * fx_dg;
    /* the DJ filter [TONIC]: FILT -64 lowpass at 78 Hz .. 0 off .. +63 highpass at 7.7 kHz, Q 1; the knob glides
     * (~7 ms) and the coefficients follow it a block at a time */
    f.mf_tgt = FX(P_MFLT);
    f.mf_s += (f.mf_tgt - f.mf_s) * 0.1f;
    if (fm_fabsf(f.mf_s) < 0.25f && f.mf_tgt == 0.0f) {
        f.mf_s = 0;
        f.mf_mode = 0;
    } else {
        float fc = f.mf_s < 0 ? 20000.0f * fm_exp2f(f.mf_s * (8.0f / 64.0f)) : 20.0f * fm_exp2f(f.mf_s * (8.6f / 63.0f));
        float g = fm_tanf(FM_PI * fm_minf(fc, 0.45f * SR) * INV_SR), k = 1.0f;
        f.mf_mode = f.mf_s < 0 ? -1 : 1;
        f.mf_a1 = 1.0f / (1.0f + g * (g + k));
        f.mf_a2 = g * f.mf_a1;
        f.mf_a3 = g * f.mf_a2;
    }
#undef FX
}

/* --------------------------------------------------------------------- the work --- */
static inline __attribute__((always_inline)) uint16_t rv_q(float v)
{
    fm_bits_t b;
    b.f = v * 1.925929944e-34f;                      /* 2^-112 */
    return (uint16_t)(((b.u >> 16) & 0x8000u) | (((b.u & 0x7FFFFFFFu) + 0x1000u) >> 13));
}
static inline __attribute__((always_inline)) float rv_f(uint16_t h)
{
    fm_bits_t b;
    b.u = ((uint32_t)(h & 0x8000u) << 16) | ((uint32_t)(h & 0x7FFFu) << 13);
    return b.f * 5.192296859e33f;                    /* 2^112 */
}
static inline __attribute__((always_inline)) float dl_read(float d)                  /* the delay line, d samples back (fractional) */
{
    float pos = (float)dl_w - d, fr;
    int32_t i;
    uint32_t a, b;
    if (pos < 0)
        pos += (float)dl_n;
    i = (int32_t)pos;
    fr = pos - (float)i;
    a = (uint32_t)i;
    if (a >= dl_n)
        a -= dl_n;
    b = a + 1 >= dl_n ? 0 : a + 1;
    {
        float va = rv_f(dl_buf[a]);
        return va + fr * (rv_f(dl_buf[b]) - va);
    }
}
/* a soft limit without a divide or an exp: cubic on [-1.5, 1.5], flat beyond (its slope 1 at 0) */
static inline __attribute__((always_inline)) float soft(float x)
{
    if (x > 1.5f) return 1.0f;
    if (x < -1.5f) return -1.0f;
    return x - (4.0f / 27.0f) * x * x * x;
}

/* (0.14) a half float: 10 bits of mantissa, so its error stays 66 dB under the signal however quiet the tail gets.
 * Branch-free: the exponent moved by a multiply with 2^-112 (2^112 back), the mantissa rounded to nearest; under
 * the halves' normal range the multiply leaves a denormal (or 0), which comes back as about the same value */
#define RV(o) rv_f(rv_ring[(rv_w + (o)) & RV_M])
#define RVW(o, v) (rv_ring[(rv_w + (o)) & RV_M] = rv_q(v))
static inline __attribute__((always_inline)) float tap(int i)                          /* tap i, signed: tap_back[i] samples into its line */
{
    float v = RV(rv_off[TAPS[i][0]] + tap_back[i]);
    return TAPS[i][1] < 0 ? -v : v;
}
/* line k: an allpass (lattice) through it, or a plain delay; the write point is its span's start */
static inline __attribute__((always_inline)) float ap(int k, float x, float g)
{
    float d = RV(rv_off[k] + rv_len[k] - 1), y = d - g * x;
    RVW(rv_off[k], x + g * y);
    return y;
}
static inline __attribute__((always_inline)) float dly(int k, float x)
{
    float y = RV(rv_off[k] + rv_len[k] - 1);
    RVW(rv_off[k], x);
    return y;
}
/* one reverb sample at 22.05 kHz: in mono, out L / R. The spans are Dattorro's lengths x 0.715 (generated) */
static ENG_RAMCODE void plate(float in, float *ol, float *orr)
{
    uint32_t w = rv_w, pr, back;
    float x, l, r, d, y, lp;
#define R(o) rv_f(rv_ring[(w + (o)) & RV_M])
#define W(o, v) (rv_ring[(w + (o)) & RV_M] = rv_q(v))
    rv_pre[rv_prew] = (int16_t)fm_clampf(in * 8192.0f, -32767.0f, 32767.0f);   /* pre-delay */
    back = (uint32_t)f.r_pre;
    pr = rv_prew >= back ? rv_prew - back : rv_prew + RV_PRE - back;
    x = (float)rv_pre[pr] * (1.0f / 8192.0f);
    rv_prew = rv_prew + 1 >= RV_PRE ? 0 : rv_prew + 1;
    x = (d = R(102), y = d - (0.75f) * (x), W(1, (x) + (0.75f) * y), y);                       /* input diffusion */
    x = (d = R(179), y = d - (0.75f) * (x), W(103, (x) + (0.75f) * y), y);
    x = (d = R(450), y = d - (0.625f) * (x), W(180, (x) + (0.625f) * y), y);
    x = (d = R(649), y = d - (0.625f) * (x), W(451, (x) + (0.625f) * y), y);
    l = x + f.r_tank[1] * f.r_dec;               /* the tank: two branches crossing */
    r = x + f.r_tank[0] * f.r_dec;
    {   /* (0.17) the tank's first allpasses read 0 .. 8 samples short, swept slowly (0.9 Hz, the two in quadrature):
         * Dattorro's modulation, against the plate's metallic ringing */
        float ml, mr, fl, fr;
        int il, ir;
        f.r_lfo += 0.9f / 22050.0f;
        if (f.r_lfo >= 1.0f)
            f.r_lfo -= 1.0f;
        ml = 4.0f + 4.0f * lks(f.r_lfo);
        mr = 4.0f + 4.0f * lks(f.r_lfo + 0.25f);
        il = (int)ml;
        ir = (int)mr;
        fl = ml - (float)il;
        fr = mr - (float)ir;
        d = R(1130 - il);
        d += fl * (R(1129 - il) - d);
        y = d - (-0.7f) * (l);
        W(650, (l) + (-0.7f) * y);
        l = y;
        d = R(8912 - ir);
        d += fr * (R(8911 - ir) - d);
        y = d - (-0.7f) * (r);
        W(8263, (r) + (-0.7f) * y);
        r = y;
    }
    l = (d = R(4314), W(1131, (l)), d);
    lp = f.r_damp_s[0] + f.r_damp_g * (l - f.r_damp_s[0]);   /* damping shelf: the treble back in by GAIN */
    f.r_damp_s[0] = fm_flush(lp);
    l = (lp + f.r_shelf * (l - lp)) * f.r_dec;
    l = (d = R(5602), y = d - (0.5f) * (l), W(4315, (l) + (0.5f) * y), y);
    f.r_tank[0] = (d = R(8262), W(5603, (l)), d);
    r = (d = R(11928), W(8913, (r)), d);
    lp = f.r_damp_s[1] + f.r_damp_g * (r - f.r_damp_s[1]);
    f.r_damp_s[1] = fm_flush(lp);
    r = (lp + f.r_shelf * (r - lp)) * f.r_dec;
    r = (d = R(13828), y = d - (0.5f) * (r), W(11929, (r) + (0.5f) * y), y);
    f.r_tank[1] = (d = R(16090), W(13829, (r)), d);
    *ol = 0.6f * (+ R(9053) + R(10495) - R(12924) + R(14885) - R(2187) - R(4413) - R(6169));
    *orr = 0.6f * (+ R(1318) + R(3027) - R(4979) + R(7111) - R(10492) - R(12029) - R(13885));
#undef R
#undef W
    rv_w = (w - 1u) & RV_M;                      /* every line moves on one sample */
}

/* (0.21, an idea from Hortator's plans) the effects asleep: with no send and THROW and everything they wrote and gave
 * back under -140 dB for longer than the delay line (and half a second for the plate), chorus, delay and reverb are
 * skipped until a send comes in again; what they hold is that quiet */
static uint32_t fx_quiet;
#define FX_QUIET_PK 1e-7f
ENG_RAMCODE void fx_block(float *dl, float *dr, const float *sc, const float *sd, const float *sr, int n)
{
#ifdef PROF_NO_FX
    return;
#endif
    int i, idle, any = fx_thr > 0.0f || fz_tgt != 1.0f || fz_g != 1.0f;
    float pk = 0.0f;
    for (i = 0; i < n && !any; i++)
        any = sc[i] != 0.0f || sd[i] != 0.0f || sr[i] != 0.0f;
    if (any)
        fx_quiet = 0;
    idle = !any && fx_quiet > (uint32_t)dl_n + SR / 2;
    float m1 = lks(f.c_ph) * f.c_depth, m2 = lks(f.c_ph + 0.25f) * f.c_depth, m1e, m2e, m1d, m2d;
    f.c_ph += f.c_inc;
    if (f.c_ph >= 1.0f)
        f.c_ph -= 1.0f;
    m1e = lks(f.c_ph) * f.c_depth;
    m2e = lks(f.c_ph + 0.25f) * f.c_depth;
    m1d = (m1e - m1) / (float)n;
    m2d = (m2e - m2) / (float)n;
    float dk = f.duck_now, dkd = (f.duck_tgt - dk) / (float)n;
    for (i = 0; i < n; i++) {
        float cin = sc[i], cl, cr, din, dout_l, dout_r, rin, rl, rr;
        if (idle) {                                  /* (0.21) asleep: no returns; the delay's TIME glides on */
            cl = cr = dout_l = dout_r = rl = rr = 0.0f;
            f.d_time += (f.d_time_tgt - f.d_time) * 0.0004f;
            f.r_half ^= 1;
            goto mix;
        }
        /* chorus */
        f.c_hp_s += f.c_hp_g * (cin - f.c_hp_s);
        cin -= f.c_hp_s;
        ch_buf[ch_w & (CH_P - 1)] = cin;
        {
            float d1 = 0.012f * SR + m1, d2 = 0.012f * SR + m2;
            float p1 = (float)ch_w - d1, p2 = (float)ch_w - d2;
            int32_t i1, i2;
            float a1, a2;
            if (p1 < 0) p1 += CH_N;
            if (p2 < 0) p2 += CH_N;
            i1 = (int32_t)p1; a1 = p1 - (float)i1;
            i2 = (int32_t)p2; a2 = p2 - (float)i2;
            cl = ch_buf[i1 & (CH_P - 1)] + a1 * (ch_buf[(i1 + 1) & (CH_P - 1)] - ch_buf[i1 & (CH_P - 1)]);
            cr = ch_buf[i2 & (CH_P - 1)] + a2 * (ch_buf[(i2 + 1) & (CH_P - 1)] - ch_buf[i2 & (CH_P - 1)]);
        }
        ch_w = (ch_w + 1) & (CH_N - 1);
        m1 += m1d;
        m2 += m2d;
        {   /* width: 0 mono .. 1 the taps apart */
            float mid = 0.5f * (cl + cr), side = 0.5f * (cl - cr) * f.c_wid;
            cl = mid + side;
            cr = mid - side;
        }
        /* delay: input from the sends and the chorus (0.18: and THROW's share of the dry mix) */
        din = sd[i] + 0.5f * (cl + cr) * f.c_del;
        if (fx_thr > 0.0f)
            din += fx_thr * 0.5f * (dl[i] + dr[i]);
        f.d_time += (f.d_time_tgt - f.d_time) * 0.0004f;   /* time changes glide (~50 ms) */
        if (fx_lend)                                 /* (0.23) the line lent to PERFORM */
            dout_l = dout_r = 0.0f;
        else {
            float y = dl_read(f.d_time), fbk;
            /* the feedback path: HPF, LPF, a soft limit */
            f.d_lp_s += f.d_lp_g * (y - f.d_lp_s);
            fbk = f.d_lp_s;
            f.d_hp_s += f.d_hp_g * (fbk - f.d_hp_s);
            fbk -= f.d_hp_s;
            fbk = soft((din + fbk * f.d_fb) * 0.5f) * 2.0f;
            if (!any)                                /* (0.21: measured only while nothing comes in) */
                pk = fm_maxf(pk, fm_fabsf(fbk));
            dl_buf[dl_w] = rv_q(fm_clampf(fbk, -4.0f, 4.0f));   /* (0.17: a half float) */
            dl_w = dl_w + 1 >= dl_n ? 0 : dl_w + 1;
            if (f.d_pp) {                            /* ping-pong: the repeats alternate */
                float y2 = dl_read(fm_minf(f.d_time * 2.0f, (float)dl_n - 4.0f));   /* (TIME still gliding down) */
                dout_l = y * f.d_gl + y2 * f.d_gr;
                dout_r = y2 * f.d_gl + y * f.d_gr;
                if (f.d_wid < 0) { float t = dout_l; dout_l = dout_r; dout_r = t; }
            } else {
                dout_l = y * f.d_gl;
                dout_r = y * f.d_gr;
            }
        }
        /* reverb, at half rate: in from the sends, the chorus and the delay */
        rin = sr[i] + 0.5f * (cl + cr) * f.c_rev + 0.5f * (dout_l + dout_r) * f.d_rev;
        if (fx_thr > 0.0f)
            rin += fx_thr * 0.5f * (dl[i] + dr[i]);
        if (fz_g != 1.0f) {                          /* FREEZE [TONIC 0.7]: the input fades out (~3 ms) */
            rin *= fz_g;
            fz_g += (fz_tgt - fz_g) * 0.01f;
            if (fm_fabsf(fz_g - fz_tgt) < 0.0005f) fz_g = fz_tgt;
        } else if (fz_tgt != 1.0f)
            fz_g = 0.999f;
        f.r_hp_s += f.r_hp_g * (rin - f.r_hp_s);
        rin -= f.r_hp_s;
        f.r_lp_s += f.r_lp_g * (rin - f.r_lp_s);
        rin = f.r_lp_s;
        if (f.r_half) {
            float l, r;
#ifdef PROF_NO_REV
            l = r = 0; if (0)
#endif
            plate(0.5f * (rin + f.r_in_prev), &l, &r);
            f.r_out_prev[0] = f.r_out_l;
            f.r_out_prev[1] = f.r_out_r;
            f.r_out_l = l;
            f.r_out_r = r;
            rl = 0.5f * (f.r_out_prev[0] + l);
            rr = 0.5f * (f.r_out_prev[1] + r);
        } else {
            f.r_in_prev = rin;
            rl = f.r_out_l;
            rr = f.r_out_r;
        }
        f.r_half ^= 1;
        if (!any)
            pk = fm_maxf(pk, fm_fabsf(cin) + fm_fabsf(rin) + fm_fabsf(rl) + fm_fabsf(rr));
        /* the mix: dry + returns, master overdrive, pattern volume */
    mix:
        {
            float l = dl[i] + (cl * f.c_vol + dout_l * f.d_vol + rl * f.r_vol) * dk;
            float r = dr[i] + (cr * f.c_vol + dout_r * f.d_vol + rr * f.r_vol) * dk;
            dk += dkd;
            if (f.movd > 0.0f) {                     /* [APPROX] soft clip with up to +24 dB drive */
                float g = 1.0f + 15.0f * f.movd * f.movd, comp = 1.0f / (1.0f + 1.5f * f.movd);
                float ld = soft(l * g) * comp * 1.2f, rd = soft(r * g) * comp * 1.2f;
                if (f.movd >= 0.0625f) {
                    l = ld;
                    r = rd;
                } else {                             /* (0.17) its first steps blended in: no level step at 0 */
                    float w = f.movd * 16.0f;
                    l = l + w * (ld - l);
                    r = r + w * (rd - r);
                }
            }
            if (f.mf_mode) {                         /* the DJ filter, both channels */
                float x2[2] = {l, r};
                int c;
                for (c = 0; c < 2; c++) {
                    float x = x2[c], v3 = x - f.mf_ic2[c], v1 = f.mf_a1 * f.mf_ic1[c] + f.mf_a2 * v3;
                    float v2 = f.mf_ic2[c] + f.mf_a2 * f.mf_ic1[c] + f.mf_a3 * v3;
                    f.mf_ic1[c] = 2.0f * v1 - f.mf_ic1[c];
                    f.mf_ic2[c] = 2.0f * v2 - f.mf_ic2[c];
                    x2[c] = f.mf_mode < 0 ? v2 : x - v1 - v2;
                }
                l = x2[0];
                r = x2[1];
            }
            dl[i] = l * f.pvol;
            dr[i] = r * f.pvol;
        }
    }
    f.duck_now = f.duck_tgt;
    if (!idle && !any)                               /* (0.21) how long the effects have been that quiet */
        fx_quiet = pk < FX_QUIET_PK ? fx_quiet + (uint32_t)n : 0;
    if (idle) {                                      /* (0.21) asleep, the lines and the plate's sweep move on as if
                                                      * they ran: woken, the effects are where they would have been */
        int k;
        ch_w = (ch_w + (uint32_t)n) & (CH_N - 1);
        dl_w = (dl_w + (uint32_t)n) % dl_n;
        for (k = 0; k < n / 2; k++) {                /* (the plate a sample in two; n even) */
            rv_w = (rv_w - 1u) & RV_M;
            rv_prew = rv_prew + 1 >= RV_PRE ? 0 : rv_prew + 1;
            f.r_lfo += 0.9f / 22050.0f;
            if (f.r_lfo >= 1.0f)
                f.r_lfo -= 1.0f;
        }
    }
    f.c_hp_s = fm_flush(f.c_hp_s);                   /* (0.17) the filters' states off denormals too */
    f.d_hp_s = fm_flush(f.d_hp_s);
    f.d_lp_s = fm_flush(f.d_lp_s);
    f.r_hp_s = fm_flush(f.r_hp_s);
    f.r_lp_s = fm_flush(f.r_lp_s);
    f.mf_ic1[0] = fm_flush(f.mf_ic1[0]);
    f.mf_ic1[1] = fm_flush(f.mf_ic1[1]);
    f.mf_ic2[0] = fm_flush(f.mf_ic2[0]);
    f.mf_ic2[1] = fm_flush(f.mf_ic2[1]);
}

/* ------------------------------------------------- PERFORM's second bank (0.23) --- */
/* After mono2k's PERFORM (ideas only). The captures borrow the delay line while one is held: the delay's return fades
 * out over eight blocks, the line then holds the master as it sounds (stereo frames of two half floats; its last
 * PB_WS halves the shimmer's mono ring), the effect plays from it, crossfaded in and out in 5 ms; when none is held
 * and none sounds, the line is cleared a piece a block and the delay comes back, fading in. GATE and DUB need no line */
#define PB_WS 4096u                /* the shimmer's ring (mono halves) */
#define PB_WIN 2048u               /* OCT's window (frames): two taps half a window apart */
#define PB_F 88u                   /* the edges' fades (2 ms) */
#define PB_X (1.0f / 220.0f)       /* the crossfade's step (5 ms) */
static struct {
    uint8_t lend;                  /* 0 the delay's, 1 fading it out, 2 lent, 3 clearing, 4 fading it in */
    int8_t cur;                    /* the capture sounding (PFB_*), -1 none */
    float mix;                     /* its share of the output */
    uint32_t n, w, cap;            /* the ring's frames, the next written, frames written since it was lent */
    uint32_t t, s0, seg;           /* since the capture began, the frame it began at, its segment */
    uint32_t clr, rng;
    uint8_t sl[4], md[4];          /* SCAT: the beat's slices, where each comes from and how */
    float d, wph, fph;             /* OCT: the taps' delay; WOW: the wobble's and the flutter's phase */
    float gph, gg;                 /* GATE: its phase (stopped), its gain */
    float sg, sd;                  /* SHIM: the feed's gain, the shift's delay */
    uint32_t sw;                   /* the shimmer's ring, the next written */
    float shim[CR];                /* the last block's output an octave up: this block's feed */
} pb = {.cur = -1, .gg = 1.0f};

static inline float pb_half(uint32_t i) { return rv_f(dl_buf[i]); }
static void pb_frame(uint32_t fr, float *l, float *r)  /* frame fr of the ring (any count: wrapped) */
{
    fr %= pb.n;
    *l = pb_half(2u * fr);
    *r = pb_half(2u * fr + 1u);
}
static void pb_back(float back, float *l, float *r)    /* back frames (fractional, >= 1) behind the write point */
{
    float pos = (float)pb.w - back, fr, l2, r2;
    uint32_t a;
    while (pos < 0.0f)
        pos += (float)pb.n;
    a = (uint32_t)pos;
    fr = pos - (float)a;
    pb_frame(a, l, r);
    pb_frame(a + 1u, &l2, &r2);
    *l += fr * (l2 - *l);
    *r += fr * (r2 - *r);
}
static float pb_edge(uint32_t p, uint32_t len)          /* a slice's fade in and out */
{
    uint32_t e = p < len - 1u - p ? p : len - 1u - p;
    return e >= PB_F ? 1.0f : (float)e * (1.0f / (float)PB_F);
}
static uint32_t pb_rand(void)
{
    pb.rng ^= pb.rng << 13;
    pb.rng ^= pb.rng >> 17;
    pb.rng ^= pb.rng << 5;
    return pb.rng;
}
/* frames of a note value (1/q of a whole note) at the tempo, halved until k of them fit the ring */
static uint32_t pb_len(float bpm, float q, uint32_t k)
{
    uint32_t n = (uint32_t)(240.0f / fm_maxf(bpm, 20.0f) / q * (float)SR);
    while (n > 64u && k * n + 64u > pb.n)
        n >>= 1;
    return n < 64u ? 64u : n;
}
static int pb_want(unsigned on)                         /* the capture to sound: the first of these held */
{
    static const int8_t ORD[6] = {PFB_STUT, PFB_SCAT, PFB_REV, PFB_OCTU, PFB_OCTD, PFB_WOW};
    int i;
    for (i = 0; i < 6; i++)
        if (on >> ORD[i] & 1u) {
            if ((ORD[i] == PFB_OCTU || ORD[i] == PFB_OCTD) && pb.cap < PB_WIN + 4u)
                return -1;                           /* (its window not yet full) */
            if (ORD[i] == PFB_WOW && pb.cap < 1200u)
                return -1;
            return ORD[i];
        }
    return -1;
}
/* one sample of the capture k: x the live sound (written already), t its time */
static void pb_wet(int k, float xl, float xr, float *ol, float *orr)
{
    uint32_t t = pb.t, s = pb.seg;
    float l = xl, r = xr, e = 1.0f;
    switch (k) {
    case PFB_REV: {                                  /* the last segment backwards (the first: as it is) */
        uint32_t q = t / s, p = t % s;
        if (q) {
            pb_frame(pb.s0 + q * s - 1u - p, &l, &r);
            e = pb_edge(p, s);
        }
        break;
    }
    case PFB_STUT: {                                 /* the first slice again, three quarters of each sounding */
        uint32_t q = t / s, p = t % s, on = s - s / 4u;
        if (q) {
            pb_frame(pb.s0 + p, &l, &r);
            e = p < on ? pb_edge(p, on) : 0.0f;
        }
        break;
    }
    case PFB_SCAT: {                                 /* the beat after the press (recorded, then kept), its
                                                      * sixteenths anywhere every beat, some backwards */
        uint32_t q = t / s, p = t % s, sl = s / 4u, i = p / sl, pp = p % sl, o;
        if (!p && q) {                               /* a new beat: its four slices' sources */
            int j;
            for (j = 0; j < 4; j++) {
                uint32_t r0 = pb_rand();
                pb.sl[j] = (uint8_t)(r0 & 3u);
                pb.md[j] = (uint8_t)((r0 >> 8) % 8u < 5u ? 0 : (r0 >> 8) % 8u < 7u ? 1 : 2);
            }
        }
        if (q && i < 4u) {
            o = pb.md[i] == 1 ? sl - 1u - pp : pb.md[i] == 2 ? pp % (sl / 2u) : pp;
            pb_frame(pb.s0 + pb.sl[i] * sl + o, &l, &r);
            e = pb.md[i] == 2 ? pb_edge(pp % (sl / 2u), sl / 2u) : pb_edge(pp, sl);
        }
        break;
    }
    case PFB_OCTU: case PFB_OCTD: {                  /* two taps sliding through a window, crossfaded */
        float d2, w1, la, ra, lb, rb;
        pb.d += k == PFB_OCTU ? -1.0f : 0.5f;
        if (pb.d < 0.0f) pb.d += (float)PB_WIN;
        if (pb.d >= (float)PB_WIN) pb.d -= (float)PB_WIN;
        d2 = pb.d + 0.5f * (float)PB_WIN;
        if (d2 >= (float)PB_WIN) d2 -= (float)PB_WIN;
        w1 = 1.0f - fm_fabsf(2.0f * pb.d * (1.0f / (float)PB_WIN) - 1.0f);
        pb_back(1.0f + pb.d, &la, &ra);
        pb_back(1.0f + d2, &lb, &rb);
        l = la * w1 + lb * (1.0f - w1);
        r = ra * w1 + rb * (1.0f - w1);
        break;
    }
    case PFB_WOW: {                                  /* 10 ms back, swaying 4 ms at 0.6 Hz and 0.3 ms at 6.5 Hz */
        pb.wph += 0.6f * INV_SR;
        if (pb.wph >= 1.0f) pb.wph -= 1.0f;
        pb.fph += 6.5f * INV_SR;
        if (pb.fph >= 1.0f) pb.fph -= 1.0f;
        pb_back(1.0f + 441.0f + 176.0f * lks(pb.wph) + 13.0f * lks(pb.fph), &l, &r);
        break;
    }
    default:
        break;
    }
    *ol = l * e;
    *orr = r * e;
}

void pfb_pre(const float *dl, const float *dr, float *sr, int n, unsigned on)
{
    int i;
    float step = 1.0f / 8.0f;
    (void)dl; (void)dr;
    if (on & PFB_BUF) {                              /* the line wanted */
        if (pb.lend == 0 || pb.lend == 4)
            pb.lend = 1;
        else if (pb.lend == 3)                       /* (still lent, being cleared: lent again) */
            pb.lend = 2;
    } else if (pb.lend == 2 && pb.cur < 0 && pb.mix <= 0.0f && pb.sg <= 0.0f) {
        pb.lend = 3;
        pb.clr = 0;
    }
    if (pb.lend == 1) {                              /* the delay's return out, then the line taken */
        fx_dg = fm_maxf(fx_dg - step, 0.0f);
        if (fx_dg <= 0.0f) {
            fx_lend = 1;
            pb.lend = 2;
            pb.n = (dl_n - PB_WS) / 2u;
            pb.w = pb.cap = pb.sw = 0;
            pb.d = pb.sd = 0.0f;
        }
    } else if (pb.lend == 3) {                       /* cleared a piece a block, then the delay's again */
        uint32_t e = pb.clr + 4096u < dl_n ? pb.clr + 4096u : dl_n;
        for (; pb.clr < e; pb.clr++)
            dl_buf[pb.clr] = 0;
        if (pb.clr >= dl_n) {
            fx_lend = 0;
            f.d_hp_s = f.d_lp_s = 0.0f;
            pb.lend = 4;
        }
    } else if (pb.lend == 4) {
        fx_dg = fm_minf(fx_dg + step, 1.0f);
        if (fx_dg >= 1.0f)
            pb.lend = 0;
    }
    /* SHIM: the output an octave up into the reverb's send (the last block's: a block late in a long tail) */
    if (pb.lend == 2 && (on >> PFB_SHIM & 1u))
        pb.sg = fm_minf(pb.sg + 1.0f / 16.0f, 1.0f);
    else
        pb.sg = fm_maxf(pb.sg - 1.0f / 16.0f, 0.0f);
    if (pb.sg > 0.0f)
        for (i = 0; i < n; i++)
            sr[i] += pb.sg * pb.shim[i];
}

int pfb_post(float *dl, float *dr, int n, unsigned on, float bpm, float gt)
{
    int i;
    if (pb.lend == 2) {
        int want = pb_want(on);
        for (i = 0; i < n; i++) {
            float xl = dl[i], xr = dr[i], wl, wr;
            if (!((pb.cur == PFB_SCAT || pb.cur == PFB_STUT) && pb.t >= pb.seg)) {   /* the live sound into the ring
                                                              * first (STUT's slice and SCAT's beat kept) */
                uint32_t a = 2u * pb.w;
                dl_buf[a] = rv_q(fm_clampf(xl, -4.0f, 4.0f));
                dl_buf[a + 1u] = rv_q(fm_clampf(xr, -4.0f, 4.0f));
                pb.w = pb.w + 1u >= pb.n ? 0 : pb.w + 1u;
                if (pb.cap < 0x7FFFFFFFu)
                    pb.cap++;
            }
            if (want != pb.cur) {                    /* another capture: the one sounding out first */
                if (pb.cur >= 0 && pb.mix > 0.0f)
                    pb.mix = fm_maxf(pb.mix - PB_X, 0.0f);
                if (pb.cur < 0 || pb.mix <= 0.0f) {
                    if (pb.cur == PFB_SCAT || pb.cur == PFB_STUT)   /* (the ring stood: the windows fill again) */
                        pb.cap = 0;
                    pb.cur = (int8_t)want;
                    pb.t = 0;
                    pb.s0 = pb.w ? pb.w - 1u : pb.n - 1u;
                    pb.d = 0.0f;
                    pb.wph = pb.fph = 0.0f;
                    pb.rng = 0x9E3779B9u ^ (uint32_t)pb.cap;
                    pb.seg = pb.cur == PFB_STUT ? pb_len(bpm, 32.0f, 1u) : pb.cur == PFB_SCAT ? pb_len(bpm, 4.0f, 1u)
                                                                                              : pb_len(bpm, 8.0f, 2u);
                }
            } else if (pb.cur >= 0)
                pb.mix = fm_minf(pb.mix + PB_X, 1.0f);
            if (pb.cur >= 0) {
                pb_wet(pb.cur, xl, xr, &wl, &wr);
                pb.t++;
                dl[i] = xl + pb.mix * (wl - xl);
                dr[i] = xr + pb.mix * (wr - xr);
            }
        }
        if (pb.cur >= 0 && want < 0 && pb.mix <= 0.0f) {
            if (pb.cur == PFB_SCAT || pb.cur == PFB_STUT)
                pb.cap = 0;
            pb.cur = -1;
        }
    } else {
        pb.cur = -1;
        pb.mix = 0.0f;
    }
    if ((on >> PFB_GATE & 1u) || pb.gg < 1.0f) {     /* GATE: a 1/16 chop, half of each open (in time when playing) */
        float tps = fm_maxf(bpm, 20.0f) * (96.0f / 60.0f) * INV_SR;   /* ticks a sample */
        for (i = 0; i < n; i++) {
            float ph, tgt;
            if (gt >= 0.0f)
                ph = (gt + (float)i * tps) * (1.0f / 24.0f);
            else {
                pb.gph += tps * (1.0f / 24.0f);
                ph = pb.gph;
            }
            ph -= fm_floorf(ph);
            tgt = !(on >> PFB_GATE & 1u) || ph < 0.5f ? 1.0f : 0.0f;
            pb.gg += (tgt - pb.gg) * 0.015f;         /* (~1.5 ms) */
            if (!(on >> PFB_GATE & 1u) && pb.gg > 0.999f)
                pb.gg = 1.0f;
            dl[i] *= pb.gg;
            dr[i] *= pb.gg;
        }
    } else
        pb.gph = 0.0f;
    if (pb.lend == 2 && ((on >> PFB_SHIM & 1u) || pb.sg > 0.0f)) {   /* SHIM: the output into its ring, up an octave */
        for (i = 0; i < n; i++) {
            uint32_t base = dl_n - PB_WS, d2i;
            float m = 0.5f * (dl[i] + dr[i]), d2, w1, a, b;
            dl_buf[base + pb.sw] = rv_q(fm_clampf(m, -4.0f, 4.0f));
            pb.sw = (pb.sw + 1u) & (PB_WS - 1u);
            pb.sd -= 1.0f;
            if (pb.sd < 0.0f) pb.sd += (float)PB_WS;
            d2 = pb.sd + 0.5f * (float)PB_WS;
            if (d2 >= (float)PB_WS) d2 -= (float)PB_WS;
            w1 = 1.0f - fm_fabsf(2.0f * pb.sd * (1.0f / (float)PB_WS) - 1.0f);
            a = pb_half(base + ((pb.sw - 2u - (uint32_t)pb.sd) & (PB_WS - 1u)));
            d2i = (uint32_t)d2;
            b = pb_half(base + ((pb.sw - 2u - d2i) & (PB_WS - 1u)));
            pb.shim[i] = soft(0.55f * (a * w1 + b * (1.0f - w1)));
        }
    }
    return pb.lend != 0 || pb.cur >= 0 || pb.gg < 1.0f || pb.sg > 0.0f;
}
