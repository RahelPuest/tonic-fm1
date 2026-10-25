/* SPDX-License-Identifier: MPL-2.0
 * TONIC: the mappings from parameter values to what the engine does (ratios, times, levels, frequencies).
 * Every mapping says where it comes from (docs/RESEARCH.md has the evidence):
 *   [MANUAL]    the Digitone manual (OS 1.44) states it
 *   [ELEKTRON]  Elektron staff stated it (forum), cited in research/manual/DIGITONE_REFERENCE.md
 *   [FIRMWARE]  recovered from the Digitone OS image (research/firmware)
 *   [APPROX]    chosen by ear / by design here, to be checked against a Digitone */
#pragma once
#include <stdint.h>

#define NRATC 19
#define NRATA 36
#define NRATB 19
extern const float RATIO_C[NRATC], RATIO_A[NRATA], RATIO_B[NRATB];

#define NHARM 27                  /* sine + 26 shapes */
#define HARM_PARTIALS 16
extern const float HARM_SPECTRUM[NHARM][HARM_PARTIALS];

#define NLEN 128
extern const uint16_t LEN_TICKS[NLEN];   /* note length index -> ticks of 1/384 note; 0xFFFF = INF */
#define LEN_INF 127
#define NPROB 21
extern const uint8_t PROB_PCT[NPROB];
extern const uint8_t COND_AB[35][2];     /* the A:B conditions in their order */
#define NMULT 12
extern const uint16_t LFO_MULT[NMULT];   /* x1 .. x2048 (the dotted, fixed-120 BPM set follows at +NMULT) */
extern const float SCALE_MUL[7];         /* track scale: 1/8x .. 2x */
extern const char *const SCALE_NAME[7];

/* [MANUAL] the LEV B macro: B1 and B2 levels (0..1) from LEV B (0..127) */
void lev_b_split(float lev, float *b1, float *b2);

/* times in seconds of the envelope stages */
float openv_atk(float v);                /* operator: attack (linear) */
float openv_dec(float v);                /* operator: decay to -30 dB of the way to END */
float amp_atk(float v), amp_dec(float v);   /* amp: attack; decay / release to -30 dB (127: infinite) */
float flt_atk(float v), flt_dec(float v), flt_del(float v);   /* filter: attack; decay / release to -60 dB; delay */
float op_delay(float v, float bpm);      /* ADEL / BDEL: 128th notes */

float fm_index(float lev);               /* modulation depth (radians) of level x envelope, 0..127 */
float fb_amount(float fdbk);             /* feedback 0..120 -> radians per unit of output */
float dtun_a(float dtun);                /* the ratio offset DTUN 0..127 adds to A */
float dtun_b2(float dtun);               /* ... and to B2 */
float level_curve(float lev);            /* the level curve 0..127 -> 0..1 */
float cutoff_hz(float freq);             /* multimode FREQ 0..127 -> Hz */
float bw_hz(float v);                    /* base-width 0..127 -> Hz */
float reso_q(float reso);                /* RESO 0..127 -> Q */
float drive_gain(float drv);             /* DRV -> the waveshaper's gain g */
float vol_gain(float v);                 /* VOL / level 0..127 -> gain */
float porta_time(float ptim);
float lfo_fade_time(float fade);
float vel_curve(float x);
float trk_gain(float v);
