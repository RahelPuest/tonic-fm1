/* SPDX-License-Identifier: MPL-2.0
 * TONIC engine: voices (FM, filters, envelopes, LFOs), effects, the sequencer. Compiled at -O2, called from the
 * audio render (engine_render) and from the main loop (the rest). */
#pragma once
#include "tonic.h"

/* ---------------------------------------------------------------- the engine's state --- */
typedef struct {
    float bpm;                    /* tempo now (the pattern's, or MIDI clock's) */
    uint8_t playing;
    uint8_t fill;                 /* FILL mode active (1 held, 3 for this pass) */
    uint8_t fill_cue;             /* FILL for one pass, from the pattern's next start [MANUAL 10.9.4] */
    uint8_t mute;                 /* bit t: track t muted */
    uint8_t rec_live;             /* live recording: notes and turns are written */
    uint8_t rec_quant;            /* live recording quantized to steps */
    uint8_t ext_clock;            /* follow MIDI clock */
    uint8_t metronome;
    uint8_t click_vol;
    uint8_t ins_lq;               /* (0.17) the inserts at the base rate (GLO > SYSTEM > INSHQ OFF: less CPU) */
    uint8_t solo;                 /* (0.18) bit t: track t soloed (any set: the others silent; not saved) */
    uint8_t mute_next;            /* (0.18) the mutes to take at the next bar (mute_q set) */
    volatile uint8_t mute_q;      /* (0.18) mute_next waits for the bar's first step (MUTE screen, NEXT BAR) */
    float thr;                    /* (0.18) THROW: the dry mix into the delay and the reverb, 0..1 (not saved) */
    volatile uint16_t pfx;        /* the momentary performance effects held (PFX_*) [TONIC 0.7] */
} eng_state_t;
/* the momentary performance effects [TONIC 0.7]: while held. ROLL: the sequencer repeats the current 1/4 .. 1/32 (the
 * shortest held wins) and goes on in time when it ends; HALF: the sequencer at half speed; TAPE: tempo and pitch slow to
 * a stop (and come back up when it ends); LP / HP: the DJ filter sweeps closed; FREEZE: the reverb holds its tail, no
 * new input */
enum { PFX_ROLL4, PFX_ROLL8, PFX_ROLL16, PFX_ROLL32, PFX_HALF, PFX_TAPE, PFX_LP, PFX_HP, PFX_FREEZE, NPFX };
extern float eng_speed;           /* the sequencer's speed (1: normal; HALF, TAPE) */
extern float eng_pitch;           /* semitones added to every voice (TAPE) */
extern eng_state_t eng;

/* the pattern playing (the render reads it; the main loop edits it). Pattern changes: engine_cue */
extern pattern_t *volatile eng_pat;

void engine_init(void *pool, uint32_t pool_bytes);   /* buffers from the pool */
void engine_render(int32_t *lr, uint32_t n, uint32_t gain_q12);   /* audio interrupt */

/* (0.16) the second core: the app hands the engine a job runner, and part of the voices render there (each into its
 * own buffers, then added in the voices' order: the same bits as on one core). give: fn(arg) to the other core;
 * wait: 0 once it is done, -1 if not within us microseconds (the engine then renders alone from there on); now: a
 * tick counter both cores read (for the split by each voice's cost). 0: one core. The voices' buffers come from the
 * engine's pool (ENG_DUAL_BYTES of it) */
typedef struct {
    void (*give)(void (*fn)(uint32_t), uint32_t arg);
    int (*wait)(uint32_t us);
    uint32_t (*now)(void);
    uint32_t (*bad)(void);                      /* (0.17) jobs the other core read wrong so far: a rise ends it */
    void (*gave_up)(void);                      /* (0.17) the engine gave it up (the app keeps one core from then on) */
} eng_dual_t;
void engine_dual(const eng_dual_t *d);
int engine_dual_on(void);                       /* 1 while the second core renders (0: off, or given up) */
uint32_t engine_dual_lost(void);                /* the times it did not finish in time (and was given up) */
extern uintptr_t eng_c1_stack_lo, eng_c1_stack_hi;   /* its stack: Braids' noise keeps a state per core */
#define ENG_DUAL_MAXV 6                          /* voices on the second core at most */
#define ENG_DUAL_BYTES (ENG_DUAL_MAXV * 5u * 32u * 4u)
#define ENG_FXRAM_BYTES (1024u * 4u + 5520u * 2u)   /* (0.17) the chorus line and the reverb's pre-delay, from the pool */
void engine_panic(void);                             /* all sound off now */

/* notes from the keys or MIDI (main loop or render; queued, played at the next block) */
void engine_note_on(int trk, int note, int vel);
void engine_note_off(int trk, int note);
void engine_mod(int trk, int src, int val);          /* MS_* source value 0..127 (PB: -8192..8191) */
void engine_all_off(int trk);                        /* release every note of a track (or -1: all) */

/* a parameter of a track's sound changed (main loop): the voices of the track follow unless locked */
void engine_param_changed(int trk, int param);
void engine_sound_changed(int trk);                  /* the whole sound (a preset, the engine): the voices follow */

/* transport */
void engine_play(void);
void engine_stop(void);                              /* stop: notes released, effects ring out */
void engine_stop_hard(void);                         /* stop twice: effects too */
void engine_cue(pattern_t *next);                    /* play `next` when the current pattern ends (CH.LEN) */
void engine_cue_how(int how);                        /* (0.17) after engine_cue: 0 at the end, 1 at once from step 1, 2 at once in place */
pattern_t *engine_cued(void);                        /* the cued pattern, 0 none */
int engine_took_cue(void);                           /* 1 once after the render switched to the cued pattern */
/* (0.17) at the pattern's next change point, in the render: these mutes (-1: as they are), a stop (song END STOP),
 * this tempo (0: as it is); so a song row's mutes and tempo come with its first step, not a frame after */
void engine_at_change(int mute, int stop, int bpm10);
void engine_restart(void);                           /* the pattern from its first step, now */

/* where the sequencer is (main loop: the playhead on the screen and the keys) */
int engine_step(int trk);                            /* the step playing on the track, -1 stopped */
uint32_t engine_loops(void);                         /* times the pattern has played since start */
uint32_t engine_passes(void);                        /* passes since PLAY, across pattern changes (song mode) */
uint32_t engine_cpu_samples(void);

/* live recording: notes played while recording, picked up by the main loop and written to the pattern */
typedef struct {
    uint8_t trk, note, vel, step;
    int8_t micro;
    uint8_t len;                  /* LEN index once released, 0xFF while held */
    uint8_t kind;                 /* 0 note, 1 parameter lock (note = param, value in val) */
    int16_t val;
    uint32_t pass;                /* the track's pass it was played in (a note on a step twice in one pass: a chord) */
} rec_ev_t;
int engine_rec_get(rec_ev_t *e);
void engine_rec_param(int trk, int param, int val);  /* a knob turned while live recording */

/* MIDI clock in */
/* (0.19) MIDI clock in, from the 1 kHz tick (app_tick: the moment it comes, not at the next screen frame): 0 a clock
 * (F8), 1 Start (FA), 2 Continue (FB), 3 Stop (FC); with ext_clock. Start and Stop take effect in the render's next
 * block, and go on to the clock out (as Hortator forwards them) */
void engine_clock_rt(int what);

/* the sound a track plays now (its pattern sound, the voice parameters: for the screen) */
const sound_t *engine_sound(int trk);
float engine_lfo_view(int trk, int lfo);             /* the newest voice's LFO value, -1..1 (screen) */
int engine_voices_busy(int trk);                     /* voices sounding on a track (-1: all) */
float engine_level(int trk);                         /* the track's output level, 0..1 (meters) */
float engine_master_level(void);                     /* the output's peak, 0..1, falling (meters) */
/* the newest sounding voice of a track, for the screen's moving pictures: an envelope (0 operator A, 1 operator B,
 * 2 filter, 3 amp): its stage (0 none: no voice) and level; an LFO's phase (-1: none); the filter's cutoff now
 * (FREQ units 0..127, with its envelope), its resonance and type (0: no voice) */
int engine_env_view(int trk, int which, float *lvl);
float engine_duck(float *ph, float *len);       /* the ducking's gain now (1: none); a division's phase 0..1 and its
                                                * length in s (-1: from a track) [0.10] */
float engine_comp_gr(void);                     /* the master compressor's gain reduction now, dB (0: off) */
float engine_pad_level(int trk, int pad);      /* a DRUM kit's pad: its loudest voice's envelope, 0 silent */
float engine_lfo_phase(int trk, int lfo);
int engine_filter_view(int trk, float *fc, float *res);

/* MIDI out of the notes the sequencer plays (USB): the app sets it */
extern void (*eng_midi_out)(uint8_t st, uint8_t d1, uint8_t d2);
extern uint8_t eng_midi_chan[NTRK];                  /* 0..15, 16 = off */
extern uint8_t eng_midi_only;                        /* bit t: track t is a MIDI track (no internal voices) */
extern void (*eng_clock_cb)(int what);               /* MIDI clock out: 0 a clock (24 a quarter), 1 start, 2 stop */
void engine_shed(void);                              /* the render ran late: end the oldest released voice */
