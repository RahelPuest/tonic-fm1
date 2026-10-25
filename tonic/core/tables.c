/* SPDX-License-Identifier: MPL-2.0
 * TONIC: parameter mappings (see tables.h for the source tags). */
#include "tables.h"
#include "fastmath.h"
#include "curves.h"

/* a knot table of curves.h at x (0..127, fractional): linear, or in the log domain between positive knots */
static float knot_at(const uint8_t *ix, const float *v, int n, int lg, float x)
{
    int j;
    if (x <= 0)
        return v[0];
    for (j = 0; j < n - 1; j++)
        if (x <= (float)ix[j + 1]) {
            float t = (x - (float)ix[j]) / (float)(ix[j + 1] - ix[j]), a = v[j], b = v[j + 1];
            if (lg && a > 0 && b > 0)
                return a * fm_exp2f(t * fm_log2f(b / a));
            return a + (b - a) * t;
        }
    return v[n - 1];
}

/* [ELEKTRON] the ratio lists, from Elektron's engine developer (research/manual/DIGITONE_REFERENCE.md §3.4) */
const float RATIO_C[NRATC] = {0.25f, 0.5f, 0.75f, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
const float RATIO_B[NRATB] = {0.25f, 0.5f, 0.75f, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
const float RATIO_A[NRATA] = {0.25f, 0.5f, 0.75f, 1, 1.25f, 1.5f, 1.75f, 2, 2.25f, 2.5f, 2.75f, 3, 3.25f, 3.5f,
                              3.75f, 4, 4.25f, 4.5f, 4.75f, 5, 5.5f, 6, 6.5f, 7, 7.5f, 8, 8.5f, 9, 9.5f, 10,
                              11, 12, 13, 14, 15, 16};

/* HARM [MANUAL A.6: 26 additive shapes in six families; FIRMWARE: the partials of the 26 tables in the OS image,
 * research/firmware (each table 257 points, normalised to a peak of 1, as build_harm does)]. Amplitudes relative
 * to the fundamental. */
const float HARM_SPECTRUM[NHARM][HARM_PARTIALS] = {
    {1},
    /* saw, built up: partials 1..2 .. 1..8 at 1/n */
    {1, 1.f / 2},
    {1, 1.f / 2, 1.f / 3},
    {1, 1.f / 2, 1.f / 3, 1.f / 4},
    {1, 1.f / 2, 1.f / 3, 1.f / 4, 1.f / 5},
    {1, 1.f / 2, 1.f / 3, 1.f / 4, 1.f / 5, 1.f / 6},
    {1, 1.f / 2, 1.f / 3, 1.f / 4, 1.f / 5, 1.f / 6, 1.f / 7},
    {1, 1.f / 2, 1.f / 3, 1.f / 4, 1.f / 5, 1.f / 6, 1.f / 7, 1.f / 8},
    /* saw, reduced: the 8-partial saw without 2, 2..3, .. 2..7 */
    {1, 0, 1.f / 3, 1.f / 4, 1.f / 5, 1.f / 6, 1.f / 7, 1.f / 8},
    {1, 0, 0, 1.f / 4, 1.f / 5, 1.f / 6, 1.f / 7, 1.f / 8},
    {1, 0, 0, 0, 1.f / 5, 1.f / 6, 1.f / 7, 1.f / 8},
    {1, 0, 0, 0, 0, 1.f / 6, 1.f / 7, 1.f / 8},
    {1, 0, 0, 0, 0, 0, 1.f / 7, 1.f / 8},
    {1, 0, 0, 0, 0, 0, 0, 1.f / 8},
    /* odd / even mix: 1, 3, 8 */
    {1, 0, 1.f / 3, 0, 0, 0, 0, 1.f / 8},
    /* square, built up: odd partials 1..3 .. 1..11 */
    {1, 0, 1.f / 3},
    {1, 0, 1.f / 3, 0, 1.f / 5},
    {1, 0, 1.f / 3, 0, 1.f / 5, 0, 1.f / 7},
    {1, 0, 1.f / 3, 0, 1.f / 5, 0, 1.f / 7, 0, 1.f / 9},
    {1, 0, 1.f / 3, 0, 1.f / 5, 0, 1.f / 7, 0, 1.f / 9, 0, 1.f / 11},
    /* square, reduced: 1 5 7 9 11; 1 5; 1 5 9; 1 5 9 11 */
    {1, 0, 0, 0, 1.f / 5, 0, 1.f / 7, 0, 1.f / 9, 0, 1.f / 11},
    {1, 0, 0, 0, 1.f / 5},
    {1, 0, 0, 0, 1.f / 5, 0, 0, 0, 1.f / 9},
    {1, 0, 0, 0, 1.f / 5, 0, 0, 0, 1.f / 9, 0, 1.f / 11},
    /* bell: 1 4; 1 4 7; 1 4 7 10 */
    {1, 0, 0, 1.f / 4},
    {1, 0, 0, 1.f / 4, 0, 0, 1.f / 7},
    {1, 0, 0, 1.f / 4, 0, 0, 1.f / 7, 0, 0, 1.f / 10},
};

/* [FIRMWARE: the OS's LEN list, 0x402421f0] the note lengths in 1/96 of a step: 0.125 .. 1 step in 1/16, then
 * 16 values per doubling (1/16, 1/8, 1/4, 1/2, 1, 2, 4 steps apart) up to 128 steps; 127 = INF */
const uint16_t LEN_TICKS[NLEN] = {
#define L(x) ((uint16_t)((x) * 96.0 + 0.5))
#define SEG(a, d) L(a + d), L(a + 2 * d), L(a + 3 * d), L(a + 4 * d), L(a + 5 * d), L(a + 6 * d), L(a + 7 * d), \
    L(a + 8 * d), L(a + 9 * d), L(a + 10 * d), L(a + 11 * d), L(a + 12 * d), L(a + 13 * d), L(a + 14 * d),     \
    L(a + 15 * d), L(a + 16 * d)
    L(0.125), L(0.1875), L(0.25), L(0.3125), L(0.375), L(0.4375), L(0.5), L(0.5625), L(0.625), L(0.6875),
    L(0.75), L(0.8125), L(0.875), L(0.9375), L(1.0),
    SEG(1.0, 0.0625), SEG(2.0, 0.125), SEG(4.0, 0.25), SEG(8.0, 0.5), SEG(16.0, 1.0), SEG(32.0, 2.0),
    SEG(64.0, 4.0),
    0xFFFFu,
#undef SEG
#undef L
};

/* [FIRMWARE: the OS's condition list, 0x401a75f0] the probability steps; then 100 %, FILL, PRE, NEI, 1ST (each
 * with its negation) and A:B */
const uint8_t PROB_PCT[NPROB] = {1, 2, 4, 6, 9, 13, 19, 25, 33, 41, 50, 59, 67, 75, 81, 87, 91, 94, 96, 98, 99};
/* [MANUAL] A:B, B = 2..8, A = 1..B */
const uint8_t COND_AB[35][2] = {
    {1, 2}, {2, 2}, {1, 3}, {2, 3}, {3, 3}, {1, 4}, {2, 4}, {3, 4}, {4, 4}, {1, 5}, {2, 5}, {3, 5}, {4, 5}, {5, 5},
    {1, 6}, {2, 6}, {3, 6}, {4, 6}, {5, 6}, {6, 6}, {1, 7}, {2, 7}, {3, 7}, {4, 7}, {5, 7}, {6, 7}, {7, 7},
    {1, 8}, {2, 8}, {3, 8}, {4, 8}, {5, 8}, {6, 8}, {7, 8}, {8, 8}};

/* [MANUAL] the LFO multipliers (p.56 table) */
const uint16_t LFO_MULT[NMULT] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048};
/* [MANUAL] track / pattern scale */
const float SCALE_MUL[7] = {0.125f, 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f};
const char *const SCALE_NAME[7] = {"1/8X", "1/4X", "1/2X", "3/4X", "1X", "3/2X", "2X"};

/* [FIRMWARE, matches the manual's graph] the LEV B macro, x = LEV B / 128: B1 rises to full at 0.375 (48), falls
 * to 0.125 at 0.75 (96) while B2 rises to full, then B1 rises to full again */
void lev_b_split(float lev, float *b1, float *b2)
{
    float x = fm_clampf(lev, 0, 127) * (1.0f / 128.0f);
    if (x <= 0.375f) {
        *b1 = (8.0f / 3.0f) * x;
        *b2 = 0;
    } else if (x <= 0.75f) {
        *b1 = 1.875f - (7.0f / 3.0f) * x;
        *b2 = (8.0f / 3.0f) * x - 1.0f;
    } else {
        *b1 = 3.5f * x - 2.5f;
        *b2 = 1.0f;
    }
}

/* ------------------------------------------------------------------ times and levels --- */
/* knots of a curve given by points of the firmware's tables (log-interpolated between them) */
static float points(const float (*pt)[2], int n, float x)
{
    int j;
    if (x <= pt[0][0])
        return pt[0][1];
    for (j = 0; j < n - 1; j++)
        if (x <= pt[j + 1][0]) {
            float t = (x - pt[j][0]) / (pt[j + 1][0] - pt[j][0]), a = pt[j][1], b = pt[j + 1][1];
            if (a > 0 && b > 0)
                return a * fm_exp2f(t * fm_log2f(b / a));
            return a + (b - a) * t;
        }
    return pt[n - 1][1];
}

/* [FIRMWARE] operator envelopes (the DSP, 1500 blocks a second): attack linear over ATK's time, decay
 * exponential, DEC's time to -30 dB of the way to END */
float openv_atk(float v) { return knot_at(CV_OPATK_I, CV_OPATK_V, CV_OPATK_N, CV_OPATK_LOG, v); }
float openv_dec(float v) { return knot_at(CV_OPDEC_I, CV_OPDEC_V, CV_OPDEC_N, CV_OPDEC_LOG, v); }

/* [FIRMWARE: the points the analysis measured in the main CPU's tables; between them INFERRED log-smooth] the amp
 * envelope's attack (the table counts samples at 48 kHz); the filter envelope's attack uses the same counts as
 * blocks of 32, so it is 32 times longer (as the firmware does) */
static const float AMP_ATK_PT[][2] = {
    {0, 1.0f / 48000}, {1, 3.0f / 48000}, {2, 6.0f / 48000}, {3, 12.0f / 48000}, {4, 21.0f / 48000},
    {5, 33.0f / 48000}, {6, 48.0f / 48000}, {7, 67.0f / 48000}, {8, 90.0f / 48000}, {9, 117.0f / 48000},
    {10, 149.0f / 48000}, {16, 0.0092f}, {32, 0.048f}, {64, 0.262f}, {96, 0.714f}, {112, 1.50f}, {120, 9.0f},
    {127, 30.0f}};
float amp_atk(float v) { return points(AMP_ATK_PT, (int)(sizeof AMP_ATK_PT / sizeof AMP_ATK_PT[0]), v); }
float flt_atk(float v) { return 32.0f * amp_atk(v); }
/* [FIRMWARE] the amp envelope's decay / release, time to -60 dB: the operator decay's table values (which are its
 * -30 dB times); 127 = infinite (the release's INF) */
float amp_dec(float v) { return v >= 126.5f ? 1e9f : openv_dec(v); }
/* [FIRMWARE points] the filter envelope's decay / release (per block), time to -60 dB; 127 infinite */
static const float FLT_DEC_PT[][2] = {{0, 0.0007f}, {16, 0.118f}, {32, 0.357f}, {64, 1.80f}, {96, 7.66f},
                                      {126, 28.0f}};
float flt_dec(float v) { return v >= 126.5f ? 1e9f : points(FLT_DEC_PT, 6, v); }
/* [FIRMWARE points] the filter envelope's delay */
static const float FLT_DEL_PT[][2] = {{0, 0}, {16, 0.0107f}, {64, 0.383f}, {127, 3.0f}};
float flt_del(float v) { return points(FLT_DEL_PT, 4, v); }

/* [FIRMWARE] the modulation depth of a level 0..127 (already x the operator's envelope): T_lev x 2.2054 cycles,
 * from a dense table made once (the firmware also interpolates its 128 steps linearly) */
static float lev_tab[129];
float fm_index(float lev)
{
    int i;
    float f;
    if (lev_tab[127] == 0.0f)
        for (i = 0; i <= 128; i++)
            lev_tab[i] = level_curve((float)(i > 127 ? 127 : i)) * 13.857f;
    lev = fm_clampf(lev, 0, 127);
    i = (int)lev;
    f = lev - (float)i;
    return lev_tab[i] + f * (lev_tab[i + 1] - lev_tab[i]);
}
/* [FIRMWARE] feedback: (FDBK / 128) x 2/pi cycles a unit of the feedback operator's output (4 rad at 128) */
float fb_amount(float fdbk) { return fm_clampf(fdbk, 0, 120) * (4.0f / 128.0f); }
/* [FIRMWARE: exact for LP4 (its coefficient table), LP2 / HP within 3.4 cents] the cutoff, FREQ 0..127 */
float cutoff_hz(float f) { return 5.0f * fm_exp2f(fm_clampf(f, 0, 127) * (1.0f / 10.5756f)); }
/* [FIRMWARE] base-width corners */
float bw_hz(float v) { return knot_at(CV_BWHZ_I, CV_BWHZ_V, CV_BWHZ_N, CV_BWHZ_LOG, fm_clampf(v, 0, 127)); }
/* [FIRMWARE points: RESO 16 Q 0.63, 32 1.19, 64 3.29, 96 8.65, 127 21.7; this fit INFERRED, within ~10 %] */
float reso_q(float r) { return 0.45f * fm_exp2f(fm_clampf(r, 0, 127) * (1.0f / 22.7f)); }
/* [FIRMWARE] the drive's gain g (DRV 0 bypasses) */
float drive_gain(float d) { float x = fm_clampf(d, 0, 127) * (1.0f / 128.0f); return 0.1535f + 0.8465f * fm_minf(2.0f * x * x, 0.992f); }
/* [FIRMWARE] VOL: (VOL / 128)^2; [APPROX] the track level the same law */
float vol_gain(float v) { float x = fm_clampf(v, 0, 127) * (1.0f / 128.0f); return x * x; }
/* [FIRMWARE, INFERRED identification] the track level: (TL / 127)^2 */
float trk_gain(float v) { float x = fm_clampf(v, 0, 127) * (1.0f / 127.0f); return x * x; }
float porta_time(float p) { return 0.002f * fm_exp2f(fm_clampf(p, 0, 127) * (11.3f / 127.0f)); }
/* [FIRMWARE] LFO FADE, the full-scale time of the linear fade: in (FADE < 0) 0.003034 s x 2^(-FADE / 4) (-1 3.6 ms,
 * -32 0.78 s, -64 199 s); out (FADE > 0) 198.9 s x 2^(-FADE / 4) (+1 167 s, +32 0.78 s, +63 3.6 ms) */
float lfo_fade_time(float f)
{
    f = fm_clampf(f, -64, 63);
    return (f < 0 ? 0.003034f : 198.9f) * fm_exp2f(-f * 0.25f);
}
/* [FIRMWARE] the operator envelope delay: in 128th notes of the tempo */
float op_delay(float v, float bpm) { return fm_clampf(v, 0, 127) * (240.0f / fm_maxf(bpm, 30.0f)) * (1.0f / 128.0f); }

/* [FIRMWARE] DTUN: an offset added to the ratios of A and B2, each with its own curve: exponential to about 116
 * (A: +0.0035 at 64, B2: +0.0047), then large steps at the top (A to +0.5, B2 to +0.75 at 127) */
float dtun_a(float d) { return knot_at(CV_DTUN_A_I, CV_DTUN_A_V, CV_DTUN_A_N, CV_DTUN_A_LOG, d); }
float dtun_b2(float d) { return knot_at(CV_DTUN_B2_I, CV_DTUN_B2_V, CV_DTUN_B2_N, CV_DTUN_B2_LOG, d); }
/* [FIRMWARE] the modulation level curve */
float level_curve(float l) { return knot_at(CV_LEVEL_I, CV_LEVEL_V, CV_LEVEL_N, CV_LEVEL_LOG, l); }

/* [FIRMWARE] VELOCITY TO VOL: OFF 1, LIN v / 127, EXP (e^(k x) - 1) / (e^k - 1), LOG its mirror 1 - EXP(1 - x),
 * k = 4.86 (the OS's 128-entry tables, within 1.4e-5) */
float vel_curve(float x) { return (fm_expf(4.86f * x) - 1.0f) * (1.0f / 128.02f); }
