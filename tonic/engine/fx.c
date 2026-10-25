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
static float ch_buf[CH_P];
static uint32_t ch_w;
static int16_t *dl_buf;           /* delay line, int16 (headroom: x 1/4) */
static uint32_t dl_n, dl_w;
/* reverb at SR / 2: Dattorro's plate, every line in one ring (the delay-memory style of the reverb chips): the
 * write point moves down one place a sample and each line is a fixed span of the ring from it, so a read or a
 * write is an add, a mask and a load. Lengths: Dattorro's (at 29761 Hz), scaled to fit 16384 samples. */
#define RV_N 16384u
#define RV_M (RV_N - 1u)
#define RV_SC 0.715f
enum { RV_PRE = 6000 };           /* pre-delay up to 272 ms at 22.05 kHz */
static const uint16_t RV_LEN0[12] = {142, 107, 379, 277,           /* input diffusers */
                                     672, 4453, 1800, 3720,        /* tank left: allpass, delay, allpass, delay */
                                     908, 4217, 2656, 3163};       /* tank right */
static uint16_t rv_len[12], rv_off[12];   /* each line's span: [off, off + len) from the write point */
static float *rv_ring;
static int16_t rv_pre[6000] ENG_LOWRAM;     /* pre-delay, x 8192 (headroom 4) */
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

static float fz_g = 1.0f, fz_tgt = 1.0f;   /* FREEZE: the reverb's input gain, its target */
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
    rv_ring = (float *)(p + *used);
    *used += RV_N * sizeof(float);
    for (i = 0; i < 14; i++) {
        int b = TAPS[i][1] < 0 ? -TAPS[i][1] : TAPS[i][1];
        tap_back[i] = (uint16_t)((uint32_t)((float)b * RV_SC) % rv_len[TAPS[i][0]]);
    }
    /* the delay takes what is left (in whole KB) */
    dl_n = (pool_bytes - *used) / sizeof(int16_t);
    dl_n &= ~1023u;
    dl_buf = (int16_t *)(p + *used);
    *used += dl_n * sizeof(int16_t);
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

static inline float onepole_g(float hz) { float g = fm_tanf(FM_PI * fm_minf(hz, 0.45f * SR) * INV_SR); return g / (1 + g); }
/* [APPROX] the effects' filter knobs 0..127: 20 Hz .. 20 kHz, exponential */
static inline float knob_hz(float v) { return 20.0f * fm_exp2f(fm_clampf(v, 0, 127) * (9.966f / 127.0f)); }

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
    f.c_hp_g = onepole_g(knob_hz(FX(P_CHPF) * 0.7f));
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
    f.d_hp_g = onepole_g(knob_hz(FX(P_DHPF) * 0.75f));
    f.d_lp_g = onepole_g(knob_hz(FX(P_DLPF)));
    f.d_rev = FX(P_DREV) * (1.0f / 127.0f);
    f.d_vol = FX(P_DVOL) * (1.0f / 100.0f);
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
        f.r_dec = dv >= 127.0f ? 1.0f : 0.35f + 0.645f * (1.0f - fm_exp2f(-dv * (4.6f / 127.0f)));
    }
    /* the loop's shelving damper: above FREQ the treble is cut by (1 - GAIN); GAIN 127 keeps it all */
    f.r_damp_g = onepole_g(knob_hz(FX(P_RFRQ)) * 0.5f) ;
    f.r_shelf = FX(P_RGAI) * (1.0f / 127.0f);
    f.r_hp_g = onepole_g(knob_hz(FX(P_RHPF)) * 0.5f);
    f.r_lp_g = onepole_g(knob_hz(FX(P_RLPF)) * 0.5f);
    f.r_vol = FX(P_RVOL) * (1.0f / 100.0f);
    /* master overdrive [MANUAL 13.5.1; APPROX curve], pattern volume */
    f.movd = FX(P_MOVD) * (1.0f / 127.0f);
    f.pvol = 1.6384f * vol_gain(FX(P_PVOL));        /* [FIRMWARE] 1.6384 (PVOL / 128)^2: unity at 100 */
djf:
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
static inline float dl_read(float d)                  /* the delay line, d samples back (fractional) */
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
    return ((float)dl_buf[a] + fr * (float)(dl_buf[b] - dl_buf[a])) * (4.0f / 32767.0f);
}
/* a soft limit without a divide or an exp: cubic on [-1.5, 1.5], flat beyond (its slope 1 at 0) */
static inline float soft(float x)
{
    if (x > 1.5f) return 1.0f;
    if (x < -1.5f) return -1.0f;
    return x - (4.0f / 27.0f) * x * x * x;
}

#define RV(o) rv_ring[(rv_w + (o)) & RV_M]
static inline float tap(int i)                          /* tap i, signed: tap_back[i] samples into its line */
{
    float v = RV(rv_off[TAPS[i][0]] + tap_back[i]);
    return TAPS[i][1] < 0 ? -v : v;
}
/* line k: an allpass (lattice) through it, or a plain delay; the write point is its span's start */
static inline float ap(int k, float x, float g)
{
    float d = RV(rv_off[k] + rv_len[k] - 1), y = d - g * x;
    RV(rv_off[k]) = x + g * y;
    return y;
}
static inline float dly(int k, float x)
{
    float y = RV(rv_off[k] + rv_len[k] - 1);
    RV(rv_off[k]) = x;
    return y;
}
/* one reverb sample at 22.05 kHz: in mono, out L / R. The spans are Dattorro's lengths x 0.715 (generated) */
static void plate(float in, float *ol, float *orr)
{
    uint32_t w = rv_w, pr, back;
    float x, l, r, d, y, lp;
#define R(o) rv_ring[(w + (o)) & RV_M]
    rv_pre[rv_prew] = (int16_t)fm_clampf(in * 8192.0f, -32767.0f, 32767.0f);   /* pre-delay */
    back = (uint32_t)f.r_pre;
    pr = rv_prew >= back ? rv_prew - back : rv_prew + RV_PRE - back;
    x = (float)rv_pre[pr] * (1.0f / 8192.0f);
    rv_prew = rv_prew + 1 >= RV_PRE ? 0 : rv_prew + 1;
    x = (d = R(102), y = d - (0.75f) * (x), R(1) = (x) + (0.75f) * y, y);                       /* input diffusion */
    x = (d = R(179), y = d - (0.75f) * (x), R(103) = (x) + (0.75f) * y, y);
    x = (d = R(450), y = d - (0.625f) * (x), R(180) = (x) + (0.625f) * y, y);
    x = (d = R(649), y = d - (0.625f) * (x), R(451) = (x) + (0.625f) * y, y);
    l = x + f.r_tank[1] * f.r_dec;               /* the tank: two branches crossing */
    r = x + f.r_tank[0] * f.r_dec;
    l = (d = R(1130), y = d - (-0.7f) * (l), R(650) = (l) + (-0.7f) * y, y);
    l = (d = R(4314), R(1131) = (l), d);
    lp = f.r_damp_s[0] + f.r_damp_g * (l - f.r_damp_s[0]);   /* damping shelf: the treble back in by GAIN */
    f.r_damp_s[0] = fm_flush(lp);
    l = (lp + f.r_shelf * (l - lp)) * f.r_dec;
    l = (d = R(5602), y = d - (0.5f) * (l), R(4315) = (l) + (0.5f) * y, y);
    f.r_tank[0] = (d = R(8262), R(5603) = (l), d);
    r = (d = R(8912), y = d - (-0.7f) * (r), R(8263) = (r) + (-0.7f) * y, y);
    r = (d = R(11928), R(8913) = (r), d);
    lp = f.r_damp_s[1] + f.r_damp_g * (r - f.r_damp_s[1]);
    f.r_damp_s[1] = fm_flush(lp);
    r = (lp + f.r_shelf * (r - lp)) * f.r_dec;
    r = (d = R(13828), y = d - (0.5f) * (r), R(11929) = (r) + (0.5f) * y, y);
    f.r_tank[1] = (d = R(16090), R(13829) = (r), d);
    *ol = 0.6f * (+ R(9053) + R(10495) - R(12924) + R(14885) - R(2187) - R(4413) - R(6169));
    *orr = 0.6f * (+ R(1318) + R(3027) - R(4979) + R(7111) - R(10492) - R(12029) - R(13885));
#undef R
    rv_w = (w - 1u) & RV_M;                      /* every line moves on one sample */
}

void fx_block(float *dl, float *dr, const float *sc, const float *sd, const float *sr, int n)
{
#ifdef PROF_NO_FX
    return;
#endif
    int i;
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
        /* delay: input from the sends and the chorus */
        din = sd[i] + 0.5f * (cl + cr) * f.c_del;
        f.d_time += (f.d_time_tgt - f.d_time) * 0.0004f;   /* time changes glide (~50 ms) */
        {
            float y = dl_read(f.d_time), fbk;
            /* the feedback path: HPF, LPF, a soft limit */
            f.d_lp_s += f.d_lp_g * (y - f.d_lp_s);
            fbk = f.d_lp_s;
            f.d_hp_s += f.d_hp_g * (fbk - f.d_hp_s);
            fbk -= f.d_hp_s;
            fbk = soft((din + fbk * f.d_fb) * 0.5f) * 2.0f;
            {
                float s = fbk * (32767.0f / 4.0f);
                dl_buf[dl_w] = (int16_t)fm_clampf(s, -32767.0f, 32767.0f);
            }
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
        /* the mix: dry + returns, master overdrive, pattern volume */
        {
            float l = dl[i] + (cl * f.c_vol + dout_l * f.d_vol + rl * f.r_vol) * dk;
            float r = dr[i] + (cr * f.c_vol + dout_r * f.d_vol + rr * f.r_vol) * dk;
            dk += dkd;
            if (f.movd > 0.0f) {                     /* [APPROX] soft clip with up to +24 dB drive */
                float g = 1.0f + 15.0f * f.movd * f.movd, comp = 1.0f / (1.0f + 1.5f * f.movd);
                l = soft(l * g) * comp * 1.2f;
                r = soft(r * g) * comp * 1.2f;
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
    f.mf_ic1[0] = fm_flush(f.mf_ic1[0]);
    f.mf_ic1[1] = fm_flush(f.mf_ic1[1]);
    f.mf_ic2[0] = fm_flush(f.mf_ic2[0]);
    f.mf_ic2[1] = fm_flush(f.mf_ic2[1]);
}
