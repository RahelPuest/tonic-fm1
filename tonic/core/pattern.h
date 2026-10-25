/* SPDX-License-Identifier: MPL-2.0
 * TONIC patterns and sounds: initial values, the lock pool, the compact form saved to flash. */
#pragma once
#include "tonic.h"

void sound_init(sound_t *s);                      /* the init sound */
void pattern_init(pattern_t *p);                  /* an empty pattern with init sounds */
void track_init(track_t *t);

/* the lock pool: one value per (track, step, parameter) */
int lock_get(const pattern_t *p, int trk, int step, int param, int16_t *val);   /* 1 = locked */
int lock_set(pattern_t *p, int trk, int step, int param, int val);   /* 0 ok, -1 the pool is full */
void lock_clear(pattern_t *p, int trk, int step, int param);         /* param P_NONE: every lock of the step */
int lock_count(const pattern_t *p, int trk, int step);
void lock_compact(pattern_t *p);                  /* drop free entries (main loop, never while the render reads) */
void trig_flags_update(pattern_t *p, int trk, int step);   /* TF_PLOCK / TF_SLOCK from the pool */

/* the saved form: magic, version, then the fields that differ from an empty pattern. pack returns its size,
 * or -1 if it would not fit in max bytes. unpack: 0 ok, -1 not a pattern. */
#define PACK_VERSION 4             /* 2: P_SRR, P_BRR, the retrig parameters (version 1 locks are moved up);
                                    * 3: the drum pads 5-8 (P_D5TUN..P_D8LEV: later ids move up NDRP);
                                    * 4 (0.12): the pads' controls and pans, the insert effect, the modulation envelope
                                    * (P_DA1..P_MDEP: later ids move up NSP12) */
#define NDRP (P_D8LEV - P_D5TUN + 1)
#define NSP12 (NSP - P_DA1)
int pattern_pack(const pattern_t *p, uint8_t *out, int max);
int pattern_unpack(pattern_t *p, const uint8_t *in, int n);
int sound_pack(const sound_t *s, uint8_t *out, int max);
int sound_unpack(sound_t *s, const uint8_t *in, int n);
/* a sound stored whole (the sound banks), as laid out by a build with nsp parameters and nss setup bytes (the fields
 * only ever grew at their ends): 1 = it reads as a sound (name, routing and setup in range), 0 = not (s is then an
 * empty slot). sound_raw_size: that layout's size in bytes */
int sound_from_raw(sound_t *s, const uint8_t *b, int nsp, int nss);
int sound_raw_size(int nsp, int nss);
/* the banks' compact form (version 3): every parameter in the bits of its range. sound_bits_size: an entry's bytes
 * for a build of nsp parameters and nss setup bytes; to_bits writes this build's; from_bits as from_raw */
int sound_bits_size(int nsp, int nss);
void sound_to_bits(const sound_t *s, uint8_t *b);
int sound_from_bits(sound_t *s, const uint8_t *b, int nsp, int nss);
int pattern_empty(const pattern_t *p);            /* no trigs at all */

/* text */
void note_name(int note, char *out);              /* C0 .. G10 (Elektron's octave numbers) */
