/* SPDX-License-Identifier: MPL-2.0
 * TONIC: a Digitone-style FM groovebox for the M-VAVE FM-1.
 * Shared definitions: parameters, sounds, patterns. Used by the engine units (engine/, -O2) and the app (-Os). */
#pragma once
#include <stdint.h>

#define NTRK 4                    /* synth tracks */
#define NVOICE 12                 /* voices shared by the tracks (Digitone: 8; TONIC 0.11: 12) */
#define NSTEP 64                  /* steps per track */
#define NNOTE 4                   /* notes per trig (a chord) */
#define NPAT 32                   /* patterns: 2 banks of 16 */
#define NPOOL 32                  /* sounds in the project's sound pool (sound locks) */
#define NLOCK 512                 /* p-lock entries per pattern (RAM); the flash object limits the total */
#define SR 44100

/* ------------------------------------------------------------------ sound parameters --- */
/* Each is an int16 in display units (what the screen shows, times the parameter's scale for the fine ones).
 * The order is the Digitone's page order; the numbers are part of the saved format: append only. */
enum {
    /* SYN1 page 1 */
    P_ALGO, P_RATC, P_RATA, P_RATB, P_HARM, P_DTUN, P_FDBK, P_MIX,
    /* SYN1 page 2: ratio offsets */
    P_OFSC, P_OFSA, P_OFSB1, P_OFSB2,
    /* SYN2 page 1: operator envelopes and levels */
    P_AATK, P_ADEC, P_AEND, P_ALEV, P_BATK, P_BDEC, P_BEND, P_BLEV,
    /* SYN2 page 2 */
    P_ADEL, P_ATRG, P_ARST, P_PHRT, P_BDEL, P_BTRG, P_BRST,
    /* FLTR page 1 */
    P_FATK, P_FDEC, P_FSUS, P_FREL, P_FREQ, P_RESO, P_FTYP, P_FENV,
    /* FLTR page 2 */
    P_FDEL, P_BASE, P_WDTH,
    /* AMP page 1 */
    P_EATK, P_EDEC, P_ESUS, P_EREL, P_DRV, P_PAN, P_VOL,
    /* AMP page 2 */
    P_CHR, P_DEL, P_REV, P_AENR,
    /* LFO 1, LFO 2 */
    P_L1SPD, P_L1MUL, P_L1FAD, P_L1DST, P_L1WAV, P_L1SPH, P_L1MOD, P_L1DEP,
    P_L2SPD, P_L2MUL, P_L2FAD, P_L2DST, P_L2WAV, P_L2SPH, P_L2MOD, P_L2DEP,
    /* LO-FI (TONIC's): sample rate and bit reduction, after the overdrive (pack version 2) */
    P_SRR, P_BRR,
    /* the DRUM engine's pads 5-8: TUNE DEC TONE LEV each (pads 1-4 use SYN slots: DRP) (pack version 3) */
    P_D5TUN, P_D5DEC, P_D5TON, P_D5LEV, P_D6TUN, P_D6DEC, P_D6TON, P_D6LEV,
    P_D7TUN, P_D7DEC, P_D7TON, P_D7LEV, P_D8TUN, P_D8DEC, P_D8TON, P_D8LEV,
    /* (0.12, pack version 4) DRUM: each pad's two controls of its model (DA: SWEEP, SNAP, ...; DB: CLICK, BODY, ...)
     * and its pan */
    P_DA1, P_DA2, P_DA3, P_DA4, P_DA5, P_DA6, P_DA7, P_DA8,
    P_DB1, P_DB2, P_DB3, P_DB4, P_DB5, P_DB6, P_DB7, P_DB8,
    P_DPN1, P_DPN2, P_DPN3, P_DPN4, P_DPN5, P_DPN6, P_DPN7, P_DPN8,
    /* (0.12) the voice's insert effect after the filter (INS_*): amount, tone (a lowpass on it; 127 open), dry / wet */
    P_ITYP, P_IAMT, P_ITON, P_IMIX,
    /* (0.12) the modulation envelope: delay, attack, decay, sustain, release, its destination (as an LFO's) and depth */
    P_MDEL, P_MATK, P_MDEC, P_MSUS, P_MREL, P_MDST, P_MDEP,
    NSP                           /* sound parameters (lockable) */
};
/* trig page (pattern level, per track), lockable per step: ids after the sound's */
enum { P_NOTE = NSP, P_TVEL, P_TLEN, P_PROB, P_FLTT, P_LFOT, P_PTIM, P_PORT, P_TLVL,
       P_RTRG, P_RTIM, P_RVEL,    /* retrig (TONIC's, pack version 2): on, rate, velocity fade */
       NTP_END };
/* FX and mixer (pattern level), lockable from any track */
enum {
    P_CDEP = NTP_END, P_CSPD, P_CHPF, P_CWID, P_CDEL, P_CREV, P_CVOL,
    P_DTIM, P_DPP, P_DWID, P_DFB, P_DHPF, P_DLPF, P_DREV, P_DVOL,
    P_RPRE, P_RDEC, P_RFRQ, P_RGAI, P_RHPF, P_RLPF, P_RVOL,
    P_MOVD, P_PVOL,
    /* TONIC's: the master DJ filter; sidechain ducking (a track's notes duck the others) */
    P_MFLT, P_SCSRC, P_SCDEP, P_SCREL, P_SCFX,
    /* TONIC's (0.6): the master compressor: amount, attack, release, dry / compressed mix */
    P_CPAMT, P_CPATK, P_CPREL, P_CPMIX,
    /* TONIC's (0.10): the ducking's PUMP page: a kit source's pad, the dip's attack, a track never ducked */
    P_SCPAD, P_SCATK, P_SCKEEP,
    NPARAM
};
#define NFXP (NPARAM - P_CDEP)
#define P_SOUND 250               /* a sound lock: the value is the pool slot */
#define P_NONE 255

/* sound setup (not lockable; saved with the sound) */
enum {
    S_KSA, S_KSB1, S_KSB2, S_FKTRK,
    S_PTYPE, S_PSLOPE, S_PAMT, S_PSTYLE, S_PGATE,
    S_PLAYMODE, S_MONOPRIO, S_VEL2VOL, S_PBDEP, S_OCT,
    S_ARPMODE, S_ARPSPD, S_ARPRNG, S_ARPLEN, S_ARPSTEPS,
    S_ENGINE,                     /* TONIC's: ENG_FM, ENG_VA, ENG_DR (the SYN pages' parameters mean the engine's) */
    S_DM1, S_DM2, S_DM3, S_DM4, S_DM5, S_DM6, S_DM7, S_DM8,   /* DRUM: each pad's model (DM_*) */
    S_ARPCHD, S_ARPINV,           /* TONIC's (0.6): a chord on each note (ACH_*), inverted (0.8: with or without the arp) */
    S_CHVOIC, S_CHBASS, S_CHSTRM, S_CHSDIR,   /* (0.8) the chord's voicing (CV_*), the root an octave below, strum
                                               * time (0..127: 0..100 ms a note), its direction (SD_*) */
    S_SCALE, S_SCROOT, S_SCHD,    /* (0.12) the sound's scale (0 off, else KSCALE_MASK's 1..), its root (0 C .. 11 B):
                                   * every note snaps to it; with a scale the chord is S_SCHD's (DCH_*, by degrees) */
    NSS
};
enum { PM_POLY, PM_POLY_MLFO, PM_MONO, PM_MONO_LEG };
enum { ENG_FM, ENG_VA, ENG_DR, NENG };
enum { VV_OFF, VV_LOG, VV_LIN, VV_EXP };
enum { ARP_OFF, ARP_TRUE, ARP_UP, ARP_DOWN, ARP_CYCLE, ARP_RANDOM, ARP_CHORD, NARP };   /* (0.8: RANDOM, CHORD) */
enum { ACH_OFF, ACH_MAJ, ACH_MIN, ACH_SUS2, ACH_SUS4, ACH_7, ACH_MAJ7, ACH_MIN7, ACH_DIM, ACH_AUG, ACH_POW, ACH_OCT,
       ACH_SCL3, ACH_SCL4,              /* SCL3 / SCL4: three / four notes of the track's keyboard scale */
       ACH_6, ACH_MIN6, ACH_ADD9, ACH_MAJ9, ACH_MIN9, ACH_7SUS4, ACH_9, ACH_QUART, ACH_CLUST,   /* (0.8) */
       NACH };
enum { CV_CLOSE, CV_OPEN, CV_DROP2, CV_SPREAD, NCV };   /* voicings: as stacked; the 2nd note up an octave; the 2nd
                                                         * highest down an octave; every other note up an octave */
enum { SD_UP, SD_DOWN, SD_ALT, NSD };
enum { INS_OFF, INS_OVER, INS_HARD, INS_FUZZ, INS_RECT, INS_FOLD, INS_CRUSH, NINS };   /* (0.12) insert effects */
/* (0.12) chords by the scale's degrees: two-note intervals, then stacks */
enum { DCH_OFF, DCH_2ND, DCH_3RD, DCH_4TH, DCH_5TH, DCH_6TH, DCH_7TH, DCH_OCT, DCH_TRIAD, DCH_SUS2, DCH_SUS4,
       DCH_SIX, DCH_SEVEN, DCH_NINE, DCH_ADD9, DCH_QUART, DCH_POWER, NDCH };
#define NDEST 83                  /* LFO / modulation destinations (engine/eng_int.h D_*) */
#define NCHORD 6                  /* notes a chord can have (5 and the BASS) */
#define NKSCALE 12
extern const uint16_t KSCALE_MASK[NKSCALE];   /* the keyboard scales: bit i, i semitones above the root */

/* modulation sources with assignable destinations (sound setup: VELOCITY MOD, MOD WHEEL, AFTERTOUCH, BREATH,
 * PITCH BEND): four slots each, a destination parameter and a depth (an offset of the parameter, -128..127) */
enum { MS_VEL, MS_MW, MS_AT, MS_BC, MS_PB, NMS };
#define NMSLOT 4

#define NAMELEN 12
typedef struct {
    int16_t p[NSP];
    uint8_t s[NSS];
    uint8_t mdst[NMS][NMSLOT];    /* P_* or P_NONE */
    int8_t mdep[NMS][NMSLOT];
    int8_t arp_ofs[16];           /* arpeggiator step offsets, semitones */
    uint16_t arp_on;              /* bit i: arpeggiator step i active */
    char name[NAMELEN];
} sound_t;

/* --------------------------------------------------------------------------- trigs --- */
enum {                            /* trig flags */
    TF_NOTE = 1,                  /* a note trig */
    TF_LOCK = 2,                  /* a lock trig (no note) */
    TF_SLOCK = 4,                 /* has a sound lock (in the lock pool) */
    TF_PLOCK = 8,                 /* has parameter locks (in the lock pool) */
};
/* trig conditions: 0 none; then the Digitone's list (tables.c COND_NAME) */
enum {
    C_NONE, C_FILL, C_NFILL, C_PRE, C_NPRE, C_NEI, C_NNEI, C_1ST, C_N1ST,
    C_PROB0,                      /* C_PROB0 + i: probability PROB_PCT[i] */
    C_AB0 = C_PROB0 + 21,         /* C_AB0 + k: the A:B conditions (1:2, 2:2, 1:3 ... 8:8) */
    C_END = C_AB0 + 35
};
typedef struct {
    uint8_t flags;
    int8_t micro;                 /* -23..+23 ticks of 1/384 */
    uint8_t cond;                 /* C_* */
    uint8_t nnote;                /* notes in the chord (1..NNOTE) for a note trig */
    uint8_t note;                 /* root note (MIDI) */
    int8_t chord[NNOTE - 1];      /* the other notes, as offsets from the root */
    uint8_t vel;                  /* 0 = the track's VEL */
    uint8_t len;                  /* LEN index, 0xFF = the track's LEN */
} trig_t;

/* a parameter lock: one 32-bit word, written whole (the render reads it while the main loop edits) */
typedef union {
    uint32_t w;
    struct {
        uint8_t step;             /* bits 0..5 step, 6..7 track */
        uint8_t param;            /* P_*, P_SOUND; P_NONE = free entry */
        int16_t val;
    } f;
} plock_t;
#define LOCK_KEY(t, s) ((uint8_t)(((t) << 6) | (s)))

/* per-track pattern settings */
typedef struct {
    int16_t tp[NTP_END - NSP];    /* trig page: NOTE VEL LEN PROB FLT.T LFO.T PTIM PORT, track level, retrig */
    uint8_t len;                  /* steps 2..64 */
    uint8_t scale;                /* SCALE index: 1/8 1/4 1/2 3/4 1 3/2 2 */
    int8_t transpose;
    uint8_t voices;               /* locked voices: 0 dynamic, 1..8 */
    uint8_t unison;               /* voices per note (1 = off) */
    uint8_t uspread;              /* unison spread 0..127 */
    uint8_t layer;                /* bit t: notes of this track also play track t */
    uint8_t kscale, kroot, kfold, kchord;   /* track note / keyboard setup: scale, root, fold, chord */
} track_t;

#define PAT_MAGIC 0x31544E54u     /* "TNT1" */
typedef struct {
    uint32_t magic;
    char name[NAMELEN];
    uint16_t bpm10;               /* tempo, tenths of BPM (300..3000) */
    uint8_t swing;                /* 50..80 % */
    uint8_t lenmode;              /* 0 length per pattern, 1 per track */
    uint8_t mlen;                 /* master length (0 = INF), steps */
    uint8_t chlen;                /* change length (0 = OFF: the pattern's end) */
    uint8_t steal;                /* voice stealing: 0 CYCLE 1 TRACK 2 LO 3 HI */
    uint8_t reuse;                /* same note reuses its voice */
    int16_t fx[NFXP];             /* chorus, delay, reverb, master */
    track_t trk[NTRK];
    sound_t snd[NTRK];
    trig_t trig[NTRK][NSTEP];
    uint16_t nlock;               /* entries used (some may be free: P_NONE) */
    plock_t lock[NLOCK];
} pattern_t;

/* ----------------------------------------------------------- parameter descriptions --- */
enum {                            /* how a value is shown */
    F_INT,                        /* the number */
    F_INT1,                       /* the number + 1 (ALGO 1..8) */
    F_BIP,                        /* signed (-64..63) */
    F_PAN,                        /* L64..R63 */
    F_ONOFF,
    F_X4,                         /* value / 4 with two decimals (HARM: quarter steps) */
    F_X16,                        /* value / 16 with two decimals (LFO DEP) */
    F_LIST,                       /* a name from a list */
    F_RATC, F_RATA, F_RATB,       /* ratio tables */
    F_OFS,                        /* ratio offset */
    F_TIME,                       /* envelope time 0..127, shown as the number */
    F_REL,                        /* 0..126, 127 = INF */
    F_NOTE,                       /* C-1 .. G9 */
    F_LEN,                        /* note length table */
    F_PCT,                        /* % */
    F_DEST,                       /* an LFO destination */
    F_MULT,                       /* LFO multiplier */
    F_DTIME,                      /* delay time */
    F_OFFV,                       /* OFF, then the value (sends) */
    F_DEC,                        /* reverb decay: 1..126, 127 = INF */
    F_SCALE,                      /* track scale */
};
typedef struct {
    char name[6];
    uint8_t fmt;
    uint8_t q;                    /* stored = display x q (fine resolution: a slow detent moves 1, a fast one q) */
    int16_t lo, hi, def;          /* stored units */
    const char *const *list;      /* F_LIST */
} pdesc_t;

extern const pdesc_t PDESC[NPARAM];

/* the VA engine [TONIC] gives the SYN pages' parameters (P_ALGO .. P_PHRT) its own meaning, name and range (each
 * range inside the parameter's own, the same scale q). param_desc: a sound parameter as an engine shows it */
enum {                            /* VA: which parameter holds what */
    V_WAV1 = P_ALGO, V_WAV2 = P_RATC, V_SUB = P_RATB, V_TUNE2 = P_HARM, V_DETN = P_DTUN, V_PW = P_FDBK,
    V_MIX = P_MIX, V_SYNC = P_OFSC, V_NOIS = P_OFSA, V_XMOD = P_OFSB1, V_DRFT = P_OFSB2, V_PENV = P_ALEV,
    V_MENV = P_BLEV,
    V_FOLD = P_D5TUN, V_RING = P_D6TUN,   /* (0.6) wavefolder, ring modulation: in two of the drum pads' places
                                           * (their default 0: a VA sound from before has them off) */
};
enum { VW_SAW, VW_SQR, VW_TRI, VW_SIN, VW_OFF, NVW };
const pdesc_t *param_desc(int engine, int p);
void sound_set_engine(sound_t *s, int engine);   /* switch, the SYN pages to the engine's defaults */

/* the DRUM engine [TONIC]: a kit of 8 pads on one track. A note picks its pad by its white key: C4 D4 E4 F4 G4 A4 B4
 * C5 are pads 1-8 (each octave on, the white keys go on counting, mod 8; a black key plays the white one below).
 * Each pad has a model and TUNE (-64..63, half semitones), DEC, TONE, LEV (0..127): DRP[pad] = those parameters */
#define NPAD 8
enum {
    DM_BD808, DM_BD909, DM_BDFM, DM_SD808, DM_SD909, DM_RIM, DM_CLAP, DM_HHC, DM_HHO, DM_CYMB, DM_TOM, DM_CONGA,
    DM_COWB, DM_CLAVE, DM_SHAKR, DM_ZAP, DM_NOISE,
    DM_CLAP2, DM_TAMB, DM_MARAC, DM_LASER,   /* (0.6) */
    DM_BDSUB, DM_BDDIST, DM_BDPNCH, DM_SDFAT, DM_SD606, DM_SDBRSH, DM_H909C, DM_H909O, DM_HHFM, DM_H606,   /* (0.9) */
    NDM
};
enum { DK_TUN, DK_DEC, DK_TON, DK_LEV };
/* pad n's (1-8) parameters, as constants: DRP is made of these */
#define DR_TUN(n) ((n) == 1 ? P_MIX : (n) == 2 ? P_OFSC : (n) == 3 ? P_OFSA : (n) == 4 ? P_OFSB1 : P_D5TUN + 4 * ((n) - 5))
#define DR_DEC(n) ((n) == 1 ? P_AATK : (n) == 2 ? P_ALEV : (n) == 3 ? P_BEND : (n) == 4 ? P_BDEL : P_D5DEC + 4 * ((n) - 5))
#define DR_TON(n) ((n) == 1 ? P_ADEC : (n) == 2 ? P_BATK : (n) == 3 ? P_BLEV : (n) == 4 ? P_RATB : P_D5TON + 4 * ((n) - 5))
#define DR_LEV(n) ((n) == 1 ? P_AEND : (n) == 2 ? P_BDEC : (n) == 3 ? P_ADEL : (n) == 4 ? P_OFSB2 : P_D5LEV + 4 * ((n) - 5))
extern const uint8_t DRP[NPAD][4];
extern const char *const DM_NAME[NDM];
extern const char *const DM_SHORT[NDM];       /* two letters (the kit's picture, the piano roll) */
extern const uint8_t PAD_NOTE[NPAD];          /* the pads' own notes: 60 62 64 65 67 69 71 72 */
static inline int pad_of(int note)
{
    static const uint8_t W[12] = {0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6};
    int w = 7 * (note / 12) + W[note % 12] - 35;    /* white keys from C4 */
    return ((w % NPAD) + NPAD) % NPAD;
}

int chord_notes(const sound_t *s, int note, int scale, int root, uint8_t *out);   /* params.c: a note's chord (NCHORD at most) */
int scale_snap(const sound_t *s, int note);   /* params.c: the note in the sound's scale (the nearest; below on a tie) */
extern const char *const DCH_NAME[NDCH];     /* params.c: the chords by degrees, by name */
