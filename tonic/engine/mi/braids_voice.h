/* SPDX-License-Identifier: MPL-2.0
 * TONIC's BRAIDS engine [0.14]: Mutable Instruments' Braids macro oscillator (MIT, engine/mi/braids), one per voice,
 * behind a C interface. Its state is a few hundred bytes; the five models with delay lines (COMB, PLUCK, BOWED,
 * BLOWN, FLUTE) need one of the shared delay buffers too. Braids runs at 96 kHz on the module: here at the engine's
 * rate, its pitch moved by the ratio (its decays are about twice as long). */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define BR_NMODEL 47                              /* (the module's 47; the hidden 48th left out) */
unsigned br_state_size(void);                     /* bytes a voice's oscillator takes (4-aligned) */
unsigned br_delay_size(void);                     /* bytes a shared delay buffer takes */
void br_init(void *st);
void br_set_delay(void *st, void *delay);         /* 0: none (a model that needs one is then silent) */
int br_needs_delay(int model);
void br_strike(void *st);                         /* a note starts: the struck and plucked models excited */
/* n samples: model 0..BR_NMODEL-1, pitch in 1/128 semitones (MIDI note x 128, at the engine's rate), timbre and color
 * 0..32767 */
void br_render(void *st, int model, int pitch, int timbre, int color, const uint8_t *sync, int16_t *out, int n);
/* (0.15) sync: per sample 0, or 1 + 128ths of a sample past a reset (Braids' sync input; 0: none at all) */
/* (0.15) Braids' signature waveshaper: its table from a seed (one fixed signature), then out mixed with its shape by
 * amount (0..65535) */
unsigned br_shaper_size(void);
void br_shaper_init(void *ws, uint32_t seed);
void br_shape(const void *ws, int16_t *buf, int n, int amount);
#ifdef __cplusplus
}
#endif
