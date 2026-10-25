/* SPDX-License-Identifier: MPL-2.0
 * TONIC: every parameter's name, display, range and initial value. Ranges are the Digitone's (manual OS 1.44,
 * research/manual/DIGITONE_REFERENCE.md); a parameter with q 4 has quarter-step resolution (the Digitone shows
 * those with two decimals). Initial values make the init sound: a plain sine carrier, filter open. */
#include "tonic.h"

static const char *const L_ONOFF[] = {"OFF", "ON"};
static const char *const L_PHRT[] = {"OFF", "ALL", "C", "A+B", "A+B2"};
static const char *const L_FTYP[] = {"OFF", "LP2", "HP2", "LP4"};
static const char *const L_LWAV[] = {"TRI", "SIN", "SQR", "SAW", "EXP", "RMP", "RND"};
static const char *const L_LMOD[] = {"FRE", "TRG", "HLD", "ONE", "HLF"};
static const char *const L_RTIM[] = {"1/1", "1/2", "1/3", "1/4", "1/5", "1/6", "1/8", "1/10", "1/12", "1/16",
                                     "1/20", "1/24", "1/32", "1/40", "1/48", "1/64", "1/80"};   /* RT_DIV */
static const char *const L_SCSRC[] = {"OFF", "T1", "T2", "T3", "T4", "1/4", "1/8", "1/16", "1/2", "1/1"};
static const char *const L_INS[] = {"OFF", "OVER", "HARD", "FUZZ", "RECT", "FOLD", "CRUSH"};
static const char *const L_SCPAD[] = {"ALL", "1", "2", "3", "4", "5", "6", "7", "8"};


#define P(n, f, q, lo, hi, d) {n, f, q, lo, hi, d, 0}
#define PL(n, lst, hi, d) {n, F_LIST, 1, 0, hi, d, lst}
/* defaults: the Digitone OS 1.44's own (its parameter descriptors) [FIRMWARE] */
const pdesc_t PDESC[NPARAM] = {
    /* SYN1 page 1 */
    [P_ALGO] = P("ALGO", F_INT1, 1, 0, 7, 0),
    [P_RATC] = P("C", F_RATC, 1, 0, 18, 3),               /* 1.00 */
    [P_RATA] = P("A", F_RATA, 1, 0, 35, 3),               /* 1.00 */
    [P_RATB] = P("B", F_RATB, 1, 0, 360, 3 * 19 + 3),     /* B1 1.00, B2 1.00 */
    [P_HARM] = P("HARM", F_X4, 4, -104, 104, 0),
    [P_DTUN] = P("DTUN", F_X4, 4, 0, 508, 0),
    [P_FDBK] = P("FDBK", F_X4, 4, 0, 480, 0),
    [P_MIX] = P("MIX", F_BIP, 1, -64, 63, 0),
    /* SYN1 page 2 */
    [P_OFSC] = P("OFSC", F_OFS, 1, -999, 999, 0),
    [P_OFSA] = P("OFSA", F_OFS, 1, -999, 999, 0),
    [P_OFSB1] = P("OFB1", F_OFS, 1, -999, 999, 0),
    [P_OFSB2] = P("OFB2", F_OFS, 1, -999, 999, 0),
    /* SYN2 page 1 */
    [P_AATK] = P("ATK", F_INT, 1, 0, 127, 0),
    [P_ADEC] = P("DEC", F_INT, 1, 0, 127, 32),
    [P_AEND] = P("END", F_INT, 1, 0, 127, 127),
    [P_ALEV] = P("LEV", F_INT, 1, 0, 127, 0),
    [P_BATK] = P("ATK", F_INT, 1, 0, 127, 0),
    [P_BDEC] = P("DEC", F_INT, 1, 0, 127, 32),
    [P_BEND] = P("END", F_INT, 1, 0, 127, 127),
    [P_BLEV] = P("LEV", F_INT, 1, 0, 127, 0),
    /* SYN2 page 2 */
    [P_ADEL] = P("ADEL", F_INT, 1, 0, 127, 0),
    [P_ATRG] = PL("ATRG", L_ONOFF, 1, 1),
    [P_ARST] = PL("ARST", L_ONOFF, 1, 1),
    [P_PHRT] = PL("PHRT", L_PHRT, 4, 1),
    [P_BDEL] = P("BDEL", F_INT, 1, 0, 127, 0),
    [P_BTRG] = PL("BTRG", L_ONOFF, 1, 1),
    [P_BRST] = PL("BRST", L_ONOFF, 1, 1),
    /* FLTR page 1 */
    [P_FATK] = P("ATK", F_INT, 1, 0, 127, 0),
    [P_FDEC] = P("DEC", F_INT, 1, 0, 127, 64),
    [P_FSUS] = P("SUS", F_INT, 1, 0, 127, 0),
    [P_FREL] = P("REL", F_REL, 1, 0, 127, 64),
    [P_FREQ] = P("FREQ", F_X4, 4, 0, 508, 508),
    [P_RESO] = P("RESO", F_X4, 4, 0, 508, 0),
    [P_FTYP] = PL("TYPE", L_FTYP, 3, 1),
    [P_FENV] = P("ENV", F_X4, 4, -256, 252, 0),
    /* FLTR page 2 */
    [P_FDEL] = P("DEL", F_INT, 1, 0, 127, 0),
    [P_BASE] = P("BASE", F_INT, 1, 0, 127, 0),
    [P_WDTH] = P("WIDTH", F_INT, 1, 0, 127, 127),
    /* AMP page 1 */
    [P_EATK] = P("ATK", F_INT, 1, 0, 127, 8),
    [P_EDEC] = P("DEC", F_INT, 1, 0, 127, 32),
    [P_ESUS] = P("SUS", F_INT, 1, 0, 127, 96),
    [P_EREL] = P("REL", F_REL, 1, 0, 127, 24),
    [P_DRV] = P("DRV", F_X4, 4, 0, 508, 0),
    [P_PAN] = P("PAN", F_PAN, 1, -64, 63, 0),
    [P_VOL] = P("VOL", F_X4, 4, 0, 508, 440),
    /* AMP page 2 */
    [P_CHR] = P("CHR", F_OFFV, 4, 0, 508, 0),
    [P_DEL] = P("DEL", F_OFFV, 4, 0, 508, 0),
    [P_REV] = P("REV", F_OFFV, 4, 0, 508, 0),
    [P_AENR] = PL("AENR", L_ONOFF, 1, 1),
    /* LFO 1 */
    [P_L1SPD] = P("SPD", F_X4, 4, -256, 252, 128),
    [P_L1MUL] = P("MULT", F_MULT, 1, 0, 23, 5),
    [P_L1FAD] = P("FADE", F_BIP, 1, -64, 63, 0),
    [P_L1DST] = P("DEST", F_DEST, 1, 0, NDEST - 1, 0),   /* (0.9: past LFO 1's own, which it skips) */
    [P_L1WAV] = PL("WAVE", L_LWAV, 6, 0),
    [P_L1SPH] = P("SPH", F_INT, 1, 0, 127, 0),
    [P_L1MOD] = PL("MODE", L_LMOD, 4, 0),
    [P_L1DEP] = P("DEP", F_X16, 16, -1024, 1023, 0),
    /* LFO 2 (can also reach LFO 1's parameters) */
    [P_L2SPD] = P("SPD", F_X4, 4, -256, 252, 128),
    [P_L2MUL] = P("MULT", F_MULT, 1, 0, 23, 5),
    [P_L2FAD] = P("FADE", F_BIP, 1, -64, 63, 0),
    [P_L2DST] = P("DEST", F_DEST, 1, 0, NDEST - 1, 0),
    [P_L2WAV] = PL("WAVE", L_LWAV, 6, 0),
    [P_L2SPH] = P("SPH", F_INT, 1, 0, 127, 0),
    [P_L2MOD] = PL("MODE", L_LMOD, 4, 0),
    [P_L2DEP] = P("DEP", F_X16, 16, -1024, 1023, 0),
    /* LO-FI (TONIC's): 0 off */
    [P_SRR] = P("SRR", F_OFFV, 1, 0, 127, 0),
    [P_BRR] = P("BRR", F_OFFV, 1, 0, 127, 0),
    /* the DRUM engine's pads 5-8 (TONIC's) */
#define DPAD(n) [P_D##n##TUN] = P("TUNE", F_BIP, 1, -64, 63, 0), [P_D##n##DEC] = P("DEC", F_INT, 1, 0, 127, 64), \
                [P_D##n##TON] = P("TONE", F_INT, 1, 0, 127, 64), [P_D##n##LEV] = P("LEV", F_INT, 1, 0, 127, 100)
    DPAD(5), DPAD(6), DPAD(7), DPAD(8),
#undef DPAD
    /* (0.12) the pads' two controls of their model (0: the model as it was) and their pans */
#define DPX(n) [P_DA##n] = P("CTL A", F_BIP, 1, -64, 63, 0), [P_DB##n] = P("CTL B", F_BIP, 1, -64, 63, 0), \
               [P_DPN##n] = P("PAN", F_BIP, 1, -64, 63, 0)
    DPX(1), DPX(2), DPX(3), DPX(4), DPX(5), DPX(6), DPX(7), DPX(8),
#undef DPX
    /* (0.12) the insert effect; the modulation envelope (times as the filter envelope's) */
    [P_ITYP] = PL("INSRT", L_INS, NINS - 1, 0),
    [P_IAMT] = P("AMT", F_INT, 1, 0, 127, 64),
    [P_ITON] = P("TONE", F_INT, 1, 0, 127, 127),
    [P_IMIX] = P("MIX", F_INT, 1, 0, 127, 127),
    [P_MDEL] = P("DEL", F_INT, 1, 0, 127, 0),
    [P_MATK] = P("ATK", F_INT, 1, 0, 127, 0),
    [P_MDEC] = P("DEC", F_INT, 1, 0, 127, 64),
    [P_MSUS] = P("SUS", F_INT, 1, 0, 127, 0),
    [P_MREL] = P("REL", F_REL, 1, 0, 127, 64),
    [P_MDST] = P("DEST", F_DEST, 1, 0, NDEST - 1, 0),
    [P_MDEP] = P("DEP", F_X16, 16, -1024, 1023, 0),
    /* TRIG page (track) */
    [P_NOTE] = P("NOTE", F_NOTE, 1, 0, 127, 60),
    [P_TVEL] = P("VEL", F_INT, 1, 1, 127, 100),
    [P_TLEN] = P("LEN", F_LEN, 1, 0, 127, 14),            /* 1/16 */
    [P_PROB] = P("PROB", F_PCT, 1, 0, 100, 100),
    [P_FLTT] = PL("FLT.T", L_ONOFF, 1, 1),
    [P_LFOT] = PL("LFO.T", L_ONOFF, 1, 1),
    [P_PTIM] = P("PTIM", F_INT, 1, 0, 127, 39),
    [P_PORT] = PL("PORT", L_ONOFF, 1, 0),
    [P_TLVL] = P("LEVEL", F_INT, 1, 0, 127, 100),
    /* RETRIG (TONIC's): repeats over the note's length at RTIM; RVEL fades them out (-) or in (+) */
    [P_RTRG] = PL("RTRG", L_ONOFF, 1, 0),
    [P_RTIM] = PL("RTIM", L_RTIM, 16, 9),                 /* 1/16 */
    [P_RVEL] = P("RVEL", F_BIP, 1, -128, 127, 0),
    /* CHORUS */
    [P_CDEP] = P("DPTH", F_INT, 1, 0, 127, 127),
    [P_CSPD] = P("SPD", F_INT, 1, 0, 127, 32),
    [P_CHPF] = P("HPF", F_INT, 1, 0, 127, 0),
    [P_CWID] = P("WDTH", F_INT, 1, 0, 127, 127),
    [P_CDEL] = P("DEL", F_INT, 1, 0, 127, 0),
    [P_CREV] = P("REV", F_INT, 1, 0, 127, 0),
    [P_CVOL] = P("VOL", F_INT, 1, 0, 127, 100),
    /* DELAY: TIME in 1/24 of a 128th note (24 = 1/128 .. 3072 = 1 bar) */
    [P_DTIM] = P("TIME", F_DTIME, 24, 24, 3072, 24 * 24),
    [P_DPP] = PL("X", L_ONOFF, 1, 0),
    [P_DWID] = P("WID", F_BIP, 1, -64, 63, 0),
    [P_DFB] = P("FDBK", F_INT, 1, 0, 198, 50),
    [P_DHPF] = P("HPF", F_INT, 1, 0, 127, 32),
    [P_DLPF] = P("LPF", F_INT, 1, 0, 127, 96),
    [P_DREV] = P("REV", F_INT, 1, 0, 127, 0),
    [P_DVOL] = P("VOL", F_INT, 1, 0, 127, 110),
    /* REVERB */
    [P_RPRE] = P("PRE", F_INT, 1, 0, 127, 16),
    [P_RDEC] = P("DEC", F_DEC, 1, 1, 127, 48),
    [P_RFRQ] = P("FREQ", F_INT, 1, 0, 127, 64),
    [P_RGAI] = P("GAIN", F_INT, 1, 0, 127, 60),
    [P_RHPF] = P("HPF", F_INT, 1, 0, 127, 32),
    [P_RLPF] = P("LPF", F_INT, 1, 0, 127, 96),
    [P_RVOL] = P("VOL", F_INT, 1, 0, 127, 110),
    /* MASTER */
    [P_MOVD] = P("MOVD", F_INT, 1, 0, 127, 0),
    [P_PVOL] = P("PVOL", F_INT, 1, 0, 127, 100),
    /* TONIC's: the DJ filter (- lowpass, + highpass), sidechain ducking from a track's notes */
    [P_MFLT] = P("FILT", F_BIP, 1, -64, 63, 0),
    [P_SCSRC] = PL("DUCK", L_SCSRC, 9, 0),        /* (0.10: 1/4 .. 1/1 the tempo's divisions: volume shaping) */
    [P_SCDEP] = P("DPTH", F_INT, 1, 0, 127, 80),
    [P_SCREL] = P("REL", F_INT, 1, 0, 127, 48),
    [P_SCFX] = PL("FX", L_ONOFF, 1, 1),
    /* TONIC's (0.6): the master compressor (AMT 0 off) */
    [P_CPAMT] = P("AMT", F_OFFV, 1, 0, 127, 0),
    [P_CPATK] = P("ATK", F_INT, 1, 0, 127, 24),
    [P_CPREL] = P("REL", F_INT, 1, 0, 127, 56),
    [P_CPMIX] = P("MIX", F_INT, 1, 0, 127, 127),
    /* TONIC's (0.10): PUMP */
    [P_SCPAD] = PL("PAD", L_SCPAD, 8, 0),
    [P_SCATK] = P("ATK", F_INT, 1, 0, 127, 0),
    [P_SCKEEP] = PL("KEEP", L_SCSRC, 4, 0),
};

/* ------------------------------------------------------------------ the VA engine --- */
/* [TONIC] the SYN pages under ENGINE VA: two oscillators (SAW, SQR with PW, TRI, SIN, OFF), OSC2 tuning and sync, a
 * sub-oscillator an octave below OSC1, noise, cross modulation 2 > 1, drift; envelope A moves the pitch (PENV),
 * envelope B OSC2's pitch (MENV). P_RATA is not used. Ranges sit inside the parameters' own (pattern.c checks) */
static const char *const L_VWAV[] = {"SAW", "SQR", "TRI", "SIN", "OFF"};
static const char *const L_VRST[] = {"OFF", "ON"};
static const pdesc_t VADESC[P_PHRT + 1] = {
    [V_WAV1] = PL("WAV1", L_VWAV, 4, VW_SAW),
    [V_WAV2] = PL("WAV2", L_VWAV, 4, VW_SAW),
    [P_RATA] = P("---", F_INT, 1, 0, 0, 0),
    [V_SUB] = P("SUB", F_INT, 1, 0, 127, 0),
    [V_TUNE2] = P("TUNE", F_X4, 4, -96, 96, 0),           /* OSC2, semitones, quarter steps */
    [V_DETN] = P("DETN", F_X4, 4, 0, 508, 40),
    [V_PW] = P("PW", F_X4, 4, 0, 480, 0),
    [V_MIX] = P("MIX", F_BIP, 1, -64, 63, 0),
    [V_SYNC] = PL("SYNC", L_VRST, 1, 0),
    [V_NOIS] = P("NOIS", F_INT, 1, 0, 127, 0),
    [V_XMOD] = P("XMOD", F_INT, 1, 0, 127, 0),
    [V_DRFT] = P("DRFT", F_INT, 1, 0, 127, 12),
    [P_AATK] = P("ATK", F_INT, 1, 0, 127, 0),
    [P_ADEC] = P("DEC", F_INT, 1, 0, 127, 32),
    [P_AEND] = P("END", F_INT, 1, 0, 127, 0),
    [V_PENV] = P("PENV", F_INT, 1, 0, 127, 0),
    [P_BATK] = P("ATK", F_INT, 1, 0, 127, 0),
    [P_BDEC] = P("DEC", F_INT, 1, 0, 127, 32),
    [P_BEND] = P("END", F_INT, 1, 0, 127, 0),
    [V_MENV] = P("MENV", F_INT, 1, 0, 127, 0),
    [P_PHRT] = PL("RST", L_VRST, 1, 0),
};

/* ---------------------------------------------------------------- the DRUM engine --- */
/* [TONIC] a kit of 8 pads. Pads 1-4 keep their TUNE DEC TONE LEV in SYN slots (each range inside the slot's own,
 * q 1; they keep LFO destinations), pads 5-8 in their own parameters. The other SYN slots are unused ("---") */
#define DRPAD(n) {DR_TUN(n), DR_DEC(n), DR_TON(n), DR_LEV(n)}
const uint8_t DRP[NPAD][4] = {DRPAD(1), DRPAD(2), DRPAD(3), DRPAD(4), DRPAD(5), DRPAD(6), DRPAD(7), DRPAD(8)};
#undef DRPAD
const char *const DM_NAME[NDM] = {"BD 808", "BD 909", "BD FM", "SD 808", "SD 909", "RIM", "CLAP", "HH CL", "HH OP",
                                  "CYMBAL", "TOM", "CONGA", "COWBEL", "CLAVE", "SHAKER", "ZAP", "NOISE",
                                  "CLAP 2", "TAMB", "MARACA", "LASER", "BD SUB", "BD DIST", "BD PNCH", "SD FAT",
                                  "SD 606", "SD BRSH", "HH 9 CL", "HH 9 OP", "HH FM", "HH 606"};
const char *const DM_SHORT[NDM] = {"BD", "BD", "BD", "SD", "SD", "RS", "CP", "CH", "OH", "CY", "TO", "CO", "CB",
                                   "CL", "SH", "ZP", "NZ", "CP", "TB", "MA", "LT", "BD", "BD", "BD", "SD", "SD", "SD",
                                   "CH", "OH", "HH", "CH"};
const uint16_t KSCALE_MASK[NKSCALE] = {0xFFF, 0xAB5, 0x5AD, 0x6AD, 0x5AB, 0xAD5, 0x6B5, 0x56B, 0x295,
                                              0x4A9, 0x9AD, 0x555};
/* (the keyboard scales, above: CHROM MAJOR MINOR DORIAN PHRYG LYDIAN MIXO LOCRIAN PENT MA PENT MI HARM MI WHOLE) */
const uint8_t PAD_NOTE[NPAD] = {60, 62, 64, 65, 67, 69, 71, 72};
static const pdesc_t DR_TUN = P("TUNE", F_BIP, 1, -64, 63, 0), DR_DEC = P("DEC", F_INT, 1, 0, 127, 64),
                     DR_TON = P("TONE", F_INT, 1, 0, 127, 64), DR_LEV = P("LEV", F_INT, 1, 0, 127, 100),
                     DR_NONE = P("---", F_INT, 1, 0, 0, 0), DR_NONE4 = P("---", F_INT, 4, 0, 0, 0);
/* a kit to start from: models, and TUNE / DEC / TONE where a pad wants its own */
static const uint8_t DR_KIT[NPAD] = {DM_BD808, DM_SD808, DM_CLAP, DM_HHC, DM_HHO, DM_TOM, DM_TOM, DM_COWB};
static const int8_t DR_KIT_TUN[NPAD] = {0, 0, 0, 0, 0, -10, 12, 0};

static const pdesc_t VA_FOLD = P("FOLD", F_INT, 1, 0, 63, 0), VA_RING = P("RING", F_INT, 1, 0, 63, 0);

const pdesc_t *param_desc(int engine, int p)
{
    if (engine == ENG_VA && p >= 0 && p <= P_PHRT && VADESC[p].name[0])
        return &VADESC[p];
    if (engine == ENG_VA && (p == V_FOLD || p == V_RING))
        return p == V_FOLD ? &VA_FOLD : &VA_RING;
    if (engine == ENG_DR && p >= 0 && p <= P_BRST) {
        int i, k;
        for (i = 0; i < 4; i++)
            for (k = 0; k < 4; k++)
                if (DRP[i][k] == p)
                    return k == DK_TUN ? &DR_TUN : k == DK_DEC ? &DR_DEC : k == DK_TON ? &DR_TON : &DR_LEV;
        return PDESC[p].q == 4 ? &DR_NONE4 : &DR_NONE;
    }
    return &PDESC[p];
}

/* the SYN pages' parameters that change meaning: SYN1, SYN1 2, the levels and PHRT (the envelopes' times keep it).
 * To or from DRUM: every SYN slot, the pads, and the amp envelope (DRUM: open, the pads decay by themselves) */
void sound_set_engine(sound_t *s, int engine)
{
    static const uint8_t SYNP[] = {P_ALGO, P_RATC, P_RATA, P_RATB, P_HARM, P_DTUN, P_FDBK, P_MIX, P_OFSC, P_OFSA,
                                   P_OFSB1, P_OFSB2, P_ALEV, P_BLEV, P_AEND, P_BEND, P_PHRT, V_FOLD, V_RING};
    unsigned i;
    int was = s->s[S_ENGINE];
    if (was == engine)
        return;
    s->s[S_ENGINE] = (uint8_t)engine;
    if (was == ENG_DR || engine == ENG_DR) {
        for (i = P_ALGO; i <= P_BRST; i++)
            s->p[i] = param_desc(engine, (int)i)->def;
        for (i = P_D5TUN; i <= P_D8LEV; i++)
            s->p[i] = PDESC[i].def;
        for (i = P_EATK; i <= P_EREL; i++)
            s->p[i] = PDESC[i].def;
        if (engine == ENG_DR) {
            for (i = 0; i < NPAD; i++) {
                s->s[S_DM1 + i] = DR_KIT[i];
                s->p[DRP[i][DK_TUN]] = DR_KIT_TUN[i];
            }
            s->p[P_EATK] = 0;                        /* the amp envelope open: ATK 0, SUS full, REL INF */
            s->p[P_EDEC] = 0;
            s->p[P_ESUS] = 127;
            s->p[P_EREL] = 127;
            s->s[S_PLAYMODE] = PM_POLY;
            s->s[S_ARPMODE] = ARP_OFF;
        }
        return;
    }
    for (i = 0; i < sizeof SYNP; i++)
        s->p[SYNP[i]] = param_desc(engine, SYNP[i])->def;
}

/* ------------------------------------------------------------------------ chords --- */
/* [TONIC 0.6, 0.8] a note's chord (the sound's CHORD, INV, VOICING, BASS), up to NCHORD notes; scale / root: the
 * track's keyboard scale, for SCALE 3 / 4 (CHROM: major). CHORD OFF: the note alone */
static const int8_t ACH_IV[NACH][5] = {
    {0}, {0, 4, 7, -1}, {0, 3, 7, -1}, {0, 2, 7, -1}, {0, 5, 7, -1}, {0, 4, 7, 10, -1}, {0, 4, 7, 11, -1},
    {0, 3, 7, 10, -1}, {0, 3, 6, -1}, {0, 4, 8, -1}, {0, 7, 12, -1}, {0, 12, -1}, {0}, {0},
    {0, 4, 7, 9, -1}, {0, 3, 7, 9, -1}, {0, 4, 7, 14, -1}, {0, 4, 7, 11, 14}, {0, 3, 7, 10, 14}, {0, 5, 7, 10, -1},
    {0, 4, 7, 10, 14}, {0, 5, 10, 15, -1}, {0, 1, 2, -1}};
static void sort_up(uint8_t *a, int n)
{
    int i, j;
    for (i = 1; i < n; i++)
        for (j = i; j > 0 && a[j] < a[j - 1]; j--) {
            uint8_t x = a[j];
            a[j] = a[j - 1];
            a[j - 1] = x;
        }
}
/* (0.12) the sound's scale: a note snaps to the nearest of its notes (below on a tie); no scale, or a kit: as it is */
static int in_scale(uint16_t m, int root, int x) { return x >= 0 && x <= 127 && (m >> (((x - root) % 12 + 12) % 12) & 1u); }
int scale_snap(const sound_t *s, int note)
{
    int d, sc = s ? s->s[S_SCALE] : 0, root;
    uint16_t m;
    if (!sc || sc >= NKSCALE || s->s[S_ENGINE] == ENG_DR)
        return note;
    m = KSCALE_MASK[sc];
    root = s->s[S_SCROOT] % 12;
    for (d = 0; d < 12; d++) {
        if (in_scale(m, root, note - d))
            return note - d;
        if (in_scale(m, root, note + d))
            return note + d;
    }
    return note;
}
/* the chords by degrees: steps up the scale from the root (OCTAVE: 12 semitones exactly), -1 the end */
#define DOCT 99
static const int8_t DCH_DEG[NDCH][5] = {
    {0, -1}, {0, 1, -1}, {0, 2, -1}, {0, 3, -1}, {0, 4, -1}, {0, 5, -1}, {0, 6, -1}, {0, DOCT, -1},
    {0, 2, 4, -1}, {0, 1, 4, -1}, {0, 3, 4, -1}, {0, 2, 4, 5, -1}, {0, 2, 4, 6, -1}, {0, 2, 4, 6, 8},
    {0, 2, 4, 8, -1}, {0, 3, 6, -1}, {0, 4, DOCT, -1}};
const char *const DCH_NAME[NDCH] = {"OFF", "2ND", "3RD", "4TH", "5TH", "6TH", "7TH", "OCT", "TRIAD", "SUS2", "SUS4",
                                     "6TH+", "7TH+", "9TH+", "ADD9", "QUART", "POWER"};
static int scale_up(uint16_t m, int root, int x, int steps)   /* the scale note steps above x (x in the scale) */
{
    while (steps > 0 && x < 127) {
        x++;
        if (in_scale(m, root, x))
            steps--;
    }
    return x;
}

int chord_notes(const sound_t *s, int note, int scale, int root, uint8_t *out)
{
    int c = s->s[S_ARPCHD] < NACH ? s->s[S_ARPCHD] : 0, n = 0, i, inv = s->s[S_ARPINV] & 3;
    int voic = s->s[S_CHVOIC] < NCV ? s->s[S_CHVOIC] : 0;
    if (s->s[S_SCALE] && s->s[S_SCALE] < NKSCALE && s->s[S_ENGINE] != ENG_DR) {   /* (0.12) by the sound's scale */
        uint16_t m = KSCALE_MASK[s->s[S_SCALE]];
        int r = s->s[S_SCROOT] % 12, d = s->s[S_SCHD] < NDCH ? s->s[S_SCHD] : 0;
        note = scale_snap(s, note);
        for (i = 0; i < 5 && DCH_DEG[d][i] >= 0; i++) {
            int x = DCH_DEG[d][i] == DOCT ? note + 12 : scale_up(m, r, note, DCH_DEG[d][i]);
            if (x <= 127)
                out[n++] = (uint8_t)x;
        }
        if (d == DCH_OFF)
            return n;
    } else if (c == ACH_OFF) {
        out[0] = (uint8_t)note;
        return 1;
    } else if (c == ACH_SCL3 || c == ACH_SCL4) {            /* stacked thirds of the scale: every other scale note */
        int x = note, deg = 0;
        uint16_t m = scale > 0 && scale < NKSCALE ? KSCALE_MASK[scale] : 0xAB5;
        while (x > 0 && !(m >> (((x - root) % 12 + 12) % 12) & 1u))   /* not in the scale: the scale note below */
            x--;
        out[n++] = (uint8_t)x;
        while (n < (c == ACH_SCL3 ? 3 : 4) && x < 127) {
            x++;
            if (m >> (((x - root) % 12 + 12) % 12) & 1u && ++deg % 2 == 0)
                out[n++] = (uint8_t)x;
        }
    } else
        for (i = 0; i < 5 && ACH_IV[c][i] >= 0 && (i == 0 || ACH_IV[c][i] > 0); i++)
            if (note + ACH_IV[c][i] < 128)
                out[n++] = (uint8_t)(note + ACH_IV[c][i]);
    for (i = 0; i < inv && i < n - 1; i++) {         /* inversions: the lowest up an octave */
        int lo = 0, j;
        for (j = 1; j < n; j++)
            if (out[j] < out[lo]) lo = j;
        if (out[lo] + 12 < 128)
            out[lo] = (uint8_t)(out[lo] + 12);
    }
    if (voic != CV_CLOSE && n >= 3) {                /* the voicing, on the notes low to high */
        sort_up(out, n);
        if (voic == CV_OPEN && out[1] + 12 < 128)
            out[1] = (uint8_t)(out[1] + 12);
        else if (voic == CV_DROP2 && out[n - 2] >= 12)
            out[n - 2] = (uint8_t)(out[n - 2] - 12);
        else if (voic == CV_SPREAD)
            for (i = 1; i < n; i += 2)
                if (out[i] + 12 < 128)
                    out[i] = (uint8_t)(out[i] + 12);
        sort_up(out, n);
    }
    if (s->s[S_CHBASS] && note >= 12 && n < NCHORD) {   /* BASS: the root an octave below, first */
        for (i = n; i > 0; i--)
            out[i] = out[i - 1];
        out[0] = (uint8_t)(note - 12);
        n++;
    }
    return n;
}
