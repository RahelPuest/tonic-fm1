/* SPDX-License-Identifier: MPL-2.0
 * TONIC engine internals, shared by synth.c, fx.c, seq.c and engine.c. */
#pragma once
#include "engine.h"
#include "tables.h"
#include "fastmath.h"

/* buffers in the RAM that .ram_text leaves (base/device/app.ld .lowram): only the device has it [TONIC 0.10.1] */
#if defined(PLAT_HOST) || defined(__APPLE__) || defined(__EMSCRIPTEN__) || defined(__linux__) || defined(_WIN32)
#define ENG_LOWRAM
#else
#define ENG_LOWRAM __attribute__((section(".lowram")))
#endif
static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
#define CR 32                     /* control rate: parameters, envelopes and the sequencer every 32 samples */
#define CRF ((float)CR)
#define INV_SR (1.0f / (float)SR)

/* the LFO destinations (Appendix C), in the order of the DEST list */
enum {
    D_NONE,
    D_ALGO, D_RATC, D_RATA, D_RATB, D_HARM, D_DTUN, D_FDBK, D_MIX,
    D_AATK, D_ADEC, D_AEND, D_ALEV, D_BATK, D_BDEC, D_BEND, D_BLEV, D_ADEL, D_BDEL,
    D_PITCH, D_PITCHAB2, D_RATALL, D_ABLEV, D_ABATK, D_ABDEC, D_ABEND, D_ABDEL,
    D_OFSC, D_OFSA, D_OFSB1, D_OFSB2,
    D_FREQ, D_RESO, D_FENV, D_FATK, D_FDEC, D_FSUS, D_FREL, D_BASE, D_WDTH, D_FDEL,
    D_EATK, D_EDEC, D_ESUS, D_EREL, D_DRV, D_PAN, D_VOL, D_REV, D_DEL, D_CHR,
    /* LFO 2 only: LFO 1's parameters */
    D_L1SPD, D_L1MUL, D_L1FAD, D_L1WAV, D_L1SPH, D_L1MOD, D_L1DEP,
    /* [TONIC 0.9] LO-FI; the DRUM pads 5-8 (TUNE DEC TONE LEV each); the whole kit's TUNE DEC TONE LEV */
    D_SRR, D_BRR,
    D_P5TUN, D_P5DEC, D_P5TON, D_P5LEV, D_P6TUN, D_P6DEC, D_P6TON, D_P6LEV,
    D_P7TUN, D_P7DEC, D_P7TON, D_P7LEV, D_P8TUN, D_P8DEC, D_P8TON, D_P8LEV,
    D_KTUN, D_KDEC, D_KTON, D_KLEV,
    /* [TONIC 0.12] the insert effect */
    D_IAMT, D_ITON, D_IMIX,
    ND
};
typedef char nd_is_ndest[ND == NDEST ? 1 : -1] __attribute__((unused));   /* (tonic.h NDEST: the destinations' count) */
extern const char *const DEST_NAME[ND];
extern const uint8_t DEST_PARAM[ND];      /* the parameter a destination moves (P_NONE: a macro) */
const char *dest_name(int engine, int d);  /* a destination's name as the engine shows it */

/* ------------------------------------------------------------------------- voices --- */
typedef struct {
    float lvl;                    /* 0..1 */
    float t;                      /* time in the stage, s */
    uint8_t stage;                /* 0 idle, 1 delay, 2 attack, 3 decay, 4 sustain, 5 release / end decay, 6 end */
    /* the stage constants, recomputed only when their parameter moves */
    float in_a, in_d, in_r, in_l;
    float k_a, k_d, k_r, t_d, t_l;
} env_t;

typedef struct {
    float ph;                     /* phase 0..1 */
    float val, held, slew;        /* value now; HLD's latched value; RND's slew state */
    float fade;                   /* 0..1: the fade envelope */
    float rnd, rnd_prev;
    uint8_t running;              /* ONE / HLF stop */
    uint32_t seed;
} lfo_t;

typedef struct {
    uint8_t on;                   /* sounding (until its release ends) */
    uint8_t gate;                 /* key or trig held */
    uint8_t trk, note;            /* track, MIDI note */
    uint8_t vel;
    uint8_t stolen;               /* fading out quickly before a new note */
    uint32_t age;                 /* note-on order (stealing) */
    uint32_t nid;                 /* the note it plays (eng_voice_on's id) */
    const sound_t *snd;           /* the sound it plays (a sound lock's, or its track's) */
    float uni_cents;              /* unison detune, cents */
    float vel01;
    float uni_pan;
    int16_t p[NSP];               /* the voice's parameters: the sound's plus its trig's locks */
    float ps[NSP];                /* the same, gliding (display units) */
    uint8_t gliding;              /* some ps[] still moving */
    uint32_t locked[(NSP + 31) / 32];   /* bit: the parameter is locked on this voice (knob turns leave it) */
    /* pitch */
    float pitch, pitch_from;      /* note in semitones (fractional while gliding) */
    float glide_t, glide_len;
    float ks[3];                  /* key scaling of A, B1, B2 (set at note-on) */
    /* caches: the inputs of the last transcendental work, and its results */
    float c_pitch, c_f0, c_dtun, c_dta, c_dtb2, c_fc, c_res, c_q, c_srr, c_brr, c_r, c_base, c_top, c_pan;
    uint8_t c_ftype;
    uint8_t ftype;
    uint8_t wti;                  /* its HARM shape table (a stolen voice's copy keeps its voice's) */
    uint8_t eng;                  /* the engine its state is for (voice_start clears the union below on a change) */
    uint16_t wait;                /* control blocks before it sounds (a strummed chord's later notes) [TONIC 0.8] */
    uint8_t latch;                /* the operator envelopes take their modulated ATK DEC DEL next control step [0.9.2] */
    env_t ea, eb, ef, eamp;       /* operator A / B envelopes, filter, amp */
    env_t em;                     /* (0.12) the modulation envelope */
    /* (0.12) the insert effect: its type (INS_*), drive, level, bias, the tone lowpass's coefficient (0 open), dry /
     * wet, and its state (the lowpass, a DC blocker, CRUSH's held sample and its count) */
    uint8_t in_typ, in_n, in_len;
    float in_g, in_c, in_b, in_tg, in_c2, in_mix, in_q, in_lp, in_dc, in_hold;   /* (in_c2: TONE's cache) */
    lfo_t lfo[2];
    /* filters */
    float bw_hp, bw_lp, bw_lpp;   /* base-width one-poles' states */
    float bw_ghp, bw_glp;         /* their coefficients */
    uint8_t bw_on;
    float s1, s2, s3, s4;         /* multimode states */
    float fg, fk, fr;             /* multimode coefficients */
    float drive_g;
    float lf_inc, lf_q, lf_iq;    /* LO-FI: sample-hold increment (0 off), quantum (0 off) and its inverse */
    float lf_ph, lf_hold;
    /* the VA engine's state that lives from note to note (free-running phases, drift) and its noise, which the
     * DRUM engine shares */
    float va_ph[3], va_drift, va_dtgt;
    uint32_t va_ns, va_dn;
    /* each engine's own state: a voice plays one engine at a time [TONIC: shared to save RAM] */
    union {
        struct {                  /* FM: operators C A B1 B2, phases and increments in 2^-32 cycles */
            uint32_t ph[4];
            uint32_t inc[4];
            float fb1;            /* the feedback operator's last output */
            float idx_now[4], idx_tgt[4];   /* modulation index per operator (or output level for enveloped outputs) */
            float ev_now[2], ev_tgt[2];     /* operator envelopes A, B (for the enveloped signals) */
            float wx_now, wx_tgt, wy_now, wy_tgt;
            float fb_now, fb_tgt; /* feedback, cycles */
            float harm_frac; int harm_i, harm_side;   /* HARM: table, blend and which operators */
            uint8_t algo;
        };
        struct {                  /* VA [TONIC]: the oscillators' settings, from each control block */
            uint8_t va_w1, va_w2, va_sync;
            float va_inc[3], va_pw, va_g1, va_g2, va_gs, va_gn, va_xm;
            float va_fold, va_ring;   /* (0.6) wavefolder drive (0 off), ring modulation level (0 off) */
        };
        struct {                  /* DRUM [TONIC]: the pad and its model (latched at the hit), samples since it, the
                                   * envelopes (amp, noise / second, pitch, the clap's bursts: levels and per-sample
                                   * factors), the oscillators, two SVFs */
            uint8_t dr_pad, dr_model;
            uint32_t dr_n, dr_sp;
            float dr_env, dr_ek, dr_ne, dr_nk, dr_pe, dr_pk, dr_be, dr_bk;
            float dr_f0, dr_sw, dr_x, dr_g;
            float dr_ph[6];
            float dr_ca[4], dr_cb[4], dr_sa[2], dr_sb[2];
            float dr_xa, dr_xb, dr_xr, dr_xc;   /* (0.12) the pad's CTL A / B as the model uses them: factors (1
                                                 * at 0), a ratio's factor, the kick's click added */
        };
    };
    float amp_now, amp_tgt;       /* amp: envelope x level, interpolated */
    float panl, panr;
    float snd_c, snd_d, snd_r;    /* sends */
    float steal_g;
    float meter;
} voice_t;

extern voice_t vc[NVOICE];
#define HARM_N 256
#define SIN_N 256
extern float sin_tab[SIN_N + 1];  /* the sine, 256 points (+1) */
static inline float lks(float x)                 /* sin(2 pi x), from the table */
{
    float fi;
    int32_t i;
    x -= fm_floorf(x);
    fi = x * (float)SIN_N;
    i = (int32_t)fi;
    fi -= (float)i;
    return sin_tab[i] + fi * (sin_tab[i + 1] - sin_tab[i]);
}

/* the per-track sources: modulation inputs (key / MIDI), the sound in use */
typedef struct {
    float mw, at, bc, pb;         /* 0..1 (pb -1..1) */
    const sound_t *snd;           /* the track's sound now */
    float level;                  /* track level gain */
    float meter;
    int8_t last_note;             /* portamento: the last note */
    float lfo_master_ph[2];       /* POLY M.LFO: the first voice's phase */
} trackstate_t;
extern trackstate_t ts[NTRK];

void synth_init(void *pool, uint32_t *used);
void voice_start(voice_t *v, int trk, int note, int vel, const sound_t *s, const plock_t *locks, int nlock,
                 int lock_key, int retrig, int filter_trig, int lfo_trig, int porta);
void voice_release(voice_t *v);
void voice_kill(voice_t *v);
/* render CR samples of a voice into the buses (added) */
void voice_block(voice_t *v, float *dl, float *dr, float *sc, float *sd, float *sr);

/* effects (fx.c) */
void fx_init(void *pool, uint32_t *used, uint32_t pool_bytes);
void fx_block(float *dl, float *dr, const float *sc, const float *sd, const float *sr, int n);
void fx_params(const int16_t *fx, float bpm);
void fx_clear(void);
void fx_duck(float g);             /* the returns' gain this block (sidechain ducking) */
void fx_freeze(int on);            /* FREEZE [TONIC 0.7]: the reverb takes no new input */

/* the sequencer (seq.c), at control rate */
void seq_block(void);
void seq_init(void);
extern uint32_t eng_now;          /* control blocks since start */

/* engine.c: notes into voices. eng_voice_on returns the note's id (0: no voice), eng_note_end releases it */
uint32_t eng_voice_on(int trk, int note, int vel, const plock_t *locks, int nlock, int lock_key,
                      int filter_trig, int lfo_trig, int porta, const sound_t *snd);
/* [TONIC 0.8] several notes as one (one id: they end together), strummed by the sound's STRUM; a note as its sound's
 * chord (CHORD set, not a kit), else alone */
uint32_t eng_notes_on(int trk, const uint8_t *notes, int n, int vel, const plock_t *locks, int nlock, int lock_key,
                      int filter_trig, int lfo_trig, int porta, const sound_t *snd);
uint32_t eng_chord_on(int trk, int note, int vel, const plock_t *locks, int nlock, int lock_key,
                      int filter_trig, int lfo_trig, int porta, const sound_t *snd);
void eng_note_end(uint32_t nid);
void eng_voice_off(int trk, int note);
void eng_lock_trig(int trk, const plock_t *locks, int nlock, int lock_key);
extern sound_t *eng_pool;         /* the project's sound pool (sound locks); a slot with an empty name is empty */

/* seq.c */
void seq_start(void);
void seq_release_all(void);
void seq_cue(pattern_t *p);
pattern_t *seq_cued(void);
int seq_took_cue(void);
int seq_step(int t);
uint32_t seq_loops(void);
uint32_t seq_ends(void);          /* the pattern's passes since PLAY, across pattern changes */
uint32_t seq_pass(int t);         /* track t's passes, never reset */
float seq_pos(int t);
float seq_gtick(void);            /* ticks (1/384 note) since PLAY, running while stopped too */
void seq_skip(int t, int s);
int16_t seq_fx(const pattern_t *p, int i);
void seq_fx_all(const pattern_t *p, int16_t *fx);   /* seq_fx for every i */
int seq_level(const pattern_t *p, int t);
extern volatile uint8_t seq_click;
int arp_on(int t);
void arp_hold(int t, int note, int vel, float len);
void arp_free(int t, int note);
extern const char *const ARP_DIV_NAME[11];

/* synth.c */
void synth_control(void);
void synth_param(int trk, int p, int val);
void synth_sound(int trk);        /* the track's sounding voices take its sound's parameters (not their locks) */
void synth_lock(int trk, const plock_t *locks, int nlock, int lock_key);
