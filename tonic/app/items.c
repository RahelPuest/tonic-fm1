/* SPDX-License-Identifier: MPL-2.0
 * TONIC pages: what the four knobs edit. A page is the Digitone's 8-parameter page, shown as two rows of four;
 * the knobs K1-K4 edit the active row. Items name a sound parameter, a trig-page parameter, an effect, or one of
 * the settings in the menus. Part of app.c. */

enum { IK_NONE, IK_SND, IK_TRK, IK_FX, IK_LVL, IK_SET, IK_TRKF, IK_PAT, IK_GLB, IK_MOD, IK_ARPS, IK_VIEW, IK_EUC };
typedef struct { uint8_t kind, id; } item_t;
static int euc_get(int k);              /* ui.c: the euclidean generator's settings for the track (its pad) */
static int euc_pad_len(int pad);        /* ui.c: a kit pad's euclidean cycle */
typedef struct { const char *title; item_t it[4]; } row_t;

#define S_(p) {IK_SND, p}
#define T_(p) {IK_TRK, p}
#define F_(p) {IK_FX, p}
#define L_(t) {IK_LVL, t}
#define ST(s) {IK_SET, s}
#define TF(f) {IK_TRKF, f}
#define PF(f) {IK_PAT, f}
#define GL(f) {IK_GLB, f}
#define MD(slot, w) {IK_MOD, (slot) << 1 | (w)}
#define AS(f) {IK_ARPS, f}
#define VW(f) {IK_VIEW, f}
#define EU(f) {IK_EUC, f}
#define NO {IK_NONE, 0}

enum { TF_LEN_, TF_SCALE_, TF_TRANSP_, TF_VOICES_, TF_UNI_, TF_SPREAD_, TF_LAYER_, TF_KSCALE_, TF_KROOT_, TF_KFOLD_,
       TF_KCHORD_, TF_QUANT_ };
enum { PF_BPM_, PF_SWING_, PF_LMODE_, PF_MLEN_, PF_CHLEN_, PF_STEAL_, PF_REUSE_ };
enum { GL_TMODE_, GL_CLKIN_, GL_CLKOUT_, GL_QUANT_, GL_VELS_, GL_VELF_, GL_MET_, GL_MIDI0_, GL_MIDI1_, GL_MIDI2_,
       GL_MIDI3_, GL_MIDIIN_, GL_TRKCH_, GL_MTRK_, GL_SROW_, GL_SPAT_, GL_SREP_, GL_SMUTE_, GL_SON_, GL_SLEN_,
       GL_SEND_, GL_THEME_ };
enum { AS_STEP_, AS_OFS_, AS_ON_ };
enum { VW_LO_, VW_ZOOM_ };              /* the piano roll's view (not saved) */
/* the euclidean generator [TONIC 0.6, 0.8]: cycle, hits, rotation, a named rhythm; accents' hits and rotation, ghost
 * notes' and the hits' chance (%); the notes' mode and range (a melodic track); a kit's pads' hits (not saved) */
enum { EU_LEN_, EU_PULS_, EU_ROT_, EU_PAT_, EU_AH_, EU_AR_, EU_GHOST_, EU_PROB_, EU_NMODE_, EU_NRANGE_, NEU_,
       EU_KIT0_ = 16 };                 /* EU_KIT0_ + pad: that pad's hits */
enum { EN_FIXED, EN_UP, EN_UPDN, EN_RANDOM, EN_CHORD, NEN };

/* the groups and their rows, in the order the group button steps through them */
static const char T_EUC[] = "EUCLID", T_EUC2[] = "EUC ACC", T_EUCK[] = "KIT 1-4", T_EUCK2[] = "KIT 5-8";
static const row_t R_TRIG[] = {
    {"TRIG", {T_(P_NOTE), T_(P_TVEL), T_(P_TLEN), T_(P_PROB)}},
    {"TRIG", {T_(P_FLTT), T_(P_LFOT), T_(P_PTIM), T_(P_PORT)}},
    {"ROLL", {VW(VW_LO_), VW(VW_ZOOM_), T_(P_NOTE), T_(P_TLEN)}},   /* the piano roll: the view, the held step */
    {"RTRG", {T_(P_RTRG), T_(P_RTIM), T_(P_RVEL), NO}},
    {T_EUC, {EU(EU_LEN_), EU(EU_PULS_), EU(EU_ROT_), EU(EU_PAT_)}},   /* [TONIC 0.6, 0.8] */
    {T_EUC2, {EU(EU_AH_), EU(EU_AR_), EU(EU_GHOST_), EU(EU_PROB_)}},
    {"EUC NOTE", {EU(EU_NMODE_), EU(EU_NRANGE_), NO, NO}},
    {"TRACK", {TF(TF_LEN_), TF(TF_SCALE_), PF(PF_MLEN_), PF(PF_CHLEN_)}},   /* [TONIC 0.12] the track's length, its scale */
};
/* a kit's SEQ rows: the same, with the pads' hits at a glance in place of EUC NOTE [TONIC 0.8] */
static const row_t R_TRIG_DR[] = {
    {"TRIG", {T_(P_NOTE), T_(P_TVEL), T_(P_TLEN), T_(P_PROB)}},
    {"TRIG", {T_(P_FLTT), T_(P_LFOT), T_(P_PTIM), T_(P_PORT)}},
    {"ROLL", {VW(VW_LO_), VW(VW_ZOOM_), T_(P_NOTE), T_(P_TLEN)}},
    {"RTRG", {T_(P_RTRG), T_(P_RTIM), T_(P_RVEL), NO}},
    {T_EUC, {EU(EU_LEN_), EU(EU_PULS_), EU(EU_ROT_), EU(EU_PAT_)}},
    {T_EUC2, {EU(EU_AH_), EU(EU_AR_), EU(EU_GHOST_), EU(EU_PROB_)}},
    {T_EUCK, {EU(EU_KIT0_), EU(EU_KIT0_ + 1), EU(EU_KIT0_ + 2), EU(EU_KIT0_ + 3)}},
    {T_EUCK2, {EU(EU_KIT0_ + 4), EU(EU_KIT0_ + 5), EU(EU_KIT0_ + 6), EU(EU_KIT0_ + 7)}},
    {"TRACK", {TF(TF_LEN_), TF(TF_SCALE_), PF(PF_MLEN_), PF(PF_CHLEN_)}},
};
static const row_t R_SYN[] = {
    {"SYN1", {S_(P_ALGO), S_(P_RATC), S_(P_RATA), S_(P_RATB)}},
    {"SYN1", {S_(P_HARM), S_(P_DTUN), S_(P_FDBK), S_(P_MIX)}},
    {"SYN1 2", {S_(P_OFSC), S_(P_OFSA), S_(P_OFSB1), S_(P_OFSB2)}},
    {"SYN2", {S_(P_AATK), S_(P_ADEC), S_(P_AEND), S_(P_ALEV)}},
    {"SYN2", {S_(P_BATK), S_(P_BDEC), S_(P_BEND), S_(P_BLEV)}},
    {"SYN2 2", {S_(P_ADEL), S_(P_ATRG), S_(P_ARST), S_(P_PHRT)}},
    {"SYN2 2", {S_(P_BDEL), S_(P_BTRG), S_(P_BRST), ST(S_ENGINE)}},
};
/* the DRUM engine's SYN pages [TONIC]: a row a pad (MODEL TUNE DEC TONE), the pads' levels, the engine */
/* the DRUM engine's SYN pages [TONIC; 0.12.1: a page a pad]: a pad's MODEL TUNE DEC TONE, its two controls of the model,
 * PAN and LEV; the pads' levels together; the engine */
static const char T_PAD1[] = "PAD 1", T_PAD2[] = "PAD 2", T_PAD3[] = "PAD 3", T_PAD4[] = "PAD 4", T_PAD5[] = "PAD 5",
                  T_PAD6[] = "PAD 6", T_PAD7[] = "PAD 7", T_PAD8[] = "PAD 8", T_LEVEL[] = "LEVEL";
static const row_t R_DRM[] = {
    {T_PAD1, {ST(S_DM1), S_(P_MIX), S_(P_AATK), S_(P_ADEC)}},
    {T_PAD1, {S_(P_DA1), S_(P_DB1), S_(P_DPN1), S_(P_AEND)}},
    {T_PAD2, {ST(S_DM2), S_(P_OFSC), S_(P_ALEV), S_(P_BATK)}},
    {T_PAD2, {S_(P_DA2), S_(P_DB2), S_(P_DPN2), S_(P_BDEC)}},
    {T_PAD3, {ST(S_DM3), S_(P_OFSA), S_(P_BEND), S_(P_BLEV)}},
    {T_PAD3, {S_(P_DA3), S_(P_DB3), S_(P_DPN3), S_(P_ADEL)}},
    {T_PAD4, {ST(S_DM4), S_(P_OFSB1), S_(P_BDEL), S_(P_RATB)}},
    {T_PAD4, {S_(P_DA4), S_(P_DB4), S_(P_DPN4), S_(P_OFSB2)}},
    {T_PAD5, {ST(S_DM5), S_(P_D5TUN), S_(P_D5DEC), S_(P_D5TON)}},
    {T_PAD5, {S_(P_DA5), S_(P_DB5), S_(P_DPN5), S_(P_D5LEV)}},
    {T_PAD6, {ST(S_DM6), S_(P_D6TUN), S_(P_D6DEC), S_(P_D6TON)}},
    {T_PAD6, {S_(P_DA6), S_(P_DB6), S_(P_DPN6), S_(P_D6LEV)}},
    {T_PAD7, {ST(S_DM7), S_(P_D7TUN), S_(P_D7DEC), S_(P_D7TON)}},
    {T_PAD7, {S_(P_DA7), S_(P_DB7), S_(P_DPN7), S_(P_D7LEV)}},
    {T_PAD8, {ST(S_DM8), S_(P_D8TUN), S_(P_D8DEC), S_(P_D8TON)}},
    {T_PAD8, {S_(P_DA8), S_(P_DB8), S_(P_DPN8), S_(P_D8LEV)}},
    {T_LEVEL, {S_(P_AEND), S_(P_BDEC), S_(P_ADEL), S_(P_OFSB2)}},
    {T_LEVEL, {S_(P_D5LEV), S_(P_D6LEV), S_(P_D7LEV), S_(P_D8LEV)}},
    {"KIT", {NO, NO, NO, ST(S_ENGINE)}},       /* (K4, as on the FM and VA pages: the knob stays the same) */
};
#define NDRM_ROWS 19
#define NDRM_PAD_ROWS 16                       /* (a pad's page: rows 2 x pad, 2 x pad + 1) */
/* the VA engine's SYN pages [TONIC]: the FM pages' places under their VA names, and SHAPE (0.6) */
static const char T_OSC[] = "OSC", T_OSC2[] = "OSC 2", T_VENV[] = "ENV", T_VENV2[] = "ENV 2", T_SHAPE[] = "SHAPE";
static const row_t R_VA[] = {
    {T_OSC, {S_(P_ALGO), S_(P_RATC), S_(P_RATA), S_(P_RATB)}},
    {T_OSC, {S_(P_HARM), S_(P_DTUN), S_(P_FDBK), S_(P_MIX)}},
    {T_OSC2, {S_(P_OFSC), S_(P_OFSA), S_(P_OFSB1), S_(P_OFSB2)}},
    {T_VENV, {S_(P_AATK), S_(P_ADEC), S_(P_AEND), S_(P_ALEV)}},
    {T_VENV, {S_(P_BATK), S_(P_BDEC), S_(P_BEND), S_(P_BLEV)}},
    {T_VENV2, {S_(P_ADEL), S_(P_ATRG), S_(P_ARST), S_(P_PHRT)}},
    {T_VENV2, {S_(P_BDEL), S_(P_BTRG), S_(P_BRST), ST(S_ENGINE)}},
    {T_SHAPE, {S_(V_FOLD), S_(V_RING), NO, NO}},
};
#define NVA_ROWS 8
#define DRM_ENGINE_ROW 18
static const row_t R_ENV[] = {
    {"FLTR", {S_(P_FATK), S_(P_FDEC), S_(P_FSUS), S_(P_FREL)}},
    {"FLTR", {S_(P_FREQ), S_(P_RESO), S_(P_FTYP), S_(P_FENV)}},
    {"FLTR 2", {S_(P_FDEL), S_(P_BASE), S_(P_WDTH), ST(S_FKTRK)}},
    {"AMP", {S_(P_EATK), S_(P_EDEC), S_(P_ESUS), S_(P_EREL)}},
    {"AMP", {S_(P_DRV), S_(P_PAN), S_(P_VOL), S_(P_AENR)}},
    {"AMP 2", {S_(P_CHR), S_(P_DEL), S_(P_REV), NO}},
    {"LOFI", {S_(P_SRR), S_(P_BRR), NO, NO}},
    {"INSRT", {S_(P_ITYP), S_(P_IAMT), S_(P_ITON), S_(P_IMIX)}},   /* [TONIC 0.12] the voice's insert effect */
};
static const row_t R_LFO[] = {
    {"LFO1", {S_(P_L1SPD), S_(P_L1MUL), S_(P_L1FAD), S_(P_L1DST)}},
    {"LFO1", {S_(P_L1WAV), S_(P_L1SPH), S_(P_L1MOD), S_(P_L1DEP)}},
    {"LFO2", {S_(P_L2SPD), S_(P_L2MUL), S_(P_L2FAD), S_(P_L2DST)}},
    {"LFO2", {S_(P_L2WAV), S_(P_L2SPH), S_(P_L2MOD), S_(P_L2DEP)}},
    {"MENV", {S_(P_MATK), S_(P_MDEC), S_(P_MSUS), S_(P_MREL)}},      /* [TONIC 0.12] the modulation envelope */
    {"MENV", {S_(P_MDEL), S_(P_MDST), S_(P_MDEP), NO}},
};
static const row_t R_FX[] = {
    {"CHORUS", {F_(P_CDEP), F_(P_CSPD), F_(P_CHPF), F_(P_CWID)}},
    {"CHORUS", {F_(P_CDEL), F_(P_CREV), F_(P_CVOL), NO}},
    {"DELAY", {F_(P_DTIM), F_(P_DPP), F_(P_DWID), F_(P_DFB)}},
    {"DELAY", {F_(P_DHPF), F_(P_DLPF), F_(P_DREV), F_(P_DVOL)}},
    {"REVERB", {F_(P_RPRE), F_(P_RDEC), F_(P_RFRQ), F_(P_RGAI)}},
    {"REVERB", {F_(P_RHPF), F_(P_RLPF), F_(P_RVOL), NO}},
    {"MIXER", {L_(0), L_(1), L_(2), L_(3)}},
    {"MIXER", {F_(P_MOVD), F_(P_PVOL), F_(P_MFLT), NO}},
    {"DUCK", {F_(P_SCSRC), F_(P_SCDEP), F_(P_SCREL), F_(P_SCFX)}},
    {"PUMP", {F_(P_SCPAD), F_(P_SCATK), F_(P_SCKEEP), NO}},              /* [TONIC 0.10] */
    {"COMP", {F_(P_CPAMT), F_(P_CPATK), F_(P_CPREL), F_(P_CPMIX)}},   /* [TONIC 0.6] */
};
static const char T_CHD[] = "CHORD";
static const row_t R_ARP[] = {
    {"ARP", {ST(S_ARPMODE), ST(S_ARPSPD), ST(S_ARPRNG), ST(S_ARPLEN)}},
    {"ARP", {ST(S_ARPSTEPS), AS(AS_STEP_), AS(AS_OFS_), AS(AS_ON_)}},
    {T_CHD, {ST(S_ARPCHD), ST(S_ARPINV), ST(S_CHVOIC), ST(S_CHBASS)}},   /* [TONIC 0.6, 0.8] a chord on each note, */
    {T_CHD, {ST(S_CHSTRM), ST(S_CHSDIR), NO, NO}},                      /* with or without the arp */
    {"SCALE", {ST(S_SCALE), ST(S_SCROOT), NO, NO}},                     /* (0.12) the sound's scale: notes snap to it,
                                                                         * its chords by degrees */
};
static const row_t R_GLO[] = {
    {"TEMPO", {PF(PF_BPM_), PF(PF_SWING_), GL(GL_TMODE_), GL(GL_MET_)}},
    {"TEMPO", {GL(GL_CLKIN_), GL(GL_CLKOUT_), GL(GL_QUANT_), GL(GL_VELS_)}},
    {"SCALE", {NO, TF(TF_LEN_), TF(TF_SCALE_), TF(TF_TRANSP_)}},   /* (0.12: always per track) */
    {"SCALE", {PF(PF_MLEN_), PF(PF_CHLEN_), GL(GL_VELF_), TF(TF_QUANT_)}},
    {"VOICE", {PF(PF_STEAL_), PF(PF_REUSE_), TF(TF_VOICES_), TF(TF_UNI_)}},
    {"VOICE", {TF(TF_SPREAD_), TF(TF_LAYER_), NO, NO}},
    {"SETUP", {ST(S_KSA), ST(S_KSB1), ST(S_KSB2), ST(S_OCT)}},
    {"SETUP", {ST(S_PLAYMODE), ST(S_MONOPRIO), ST(S_VEL2VOL), ST(S_PBDEP)}},
    {"PORTA", {ST(S_PTYPE), ST(S_PSLOPE), ST(S_PAMT), ST(S_PSTYLE)}},
    {"PORTA", {ST(S_PGATE), NO, NO, NO}},
    {"VEL MOD", {MD(0, 0), MD(0, 1), MD(1, 0), MD(1, 1)}},
    {"VEL MOD", {MD(2, 0), MD(2, 1), MD(3, 0), MD(3, 1)}},
    {"MODWHEEL", {MD(4, 0), MD(4, 1), MD(5, 0), MD(5, 1)}},
    {"MODWHEEL", {MD(6, 0), MD(6, 1), MD(7, 0), MD(7, 1)}},
    {"AFTERTCH", {MD(8, 0), MD(8, 1), MD(9, 0), MD(9, 1)}},
    {"AFTERTCH", {MD(10, 0), MD(10, 1), MD(11, 0), MD(11, 1)}},
    {"BREATH", {MD(12, 0), MD(12, 1), MD(13, 0), MD(13, 1)}},
    {"BREATH", {MD(14, 0), MD(14, 1), MD(15, 0), MD(15, 1)}},
    {"PITCHBND", {MD(16, 0), MD(16, 1), MD(17, 0), MD(17, 1)}},
    {"PITCHBND", {MD(18, 0), MD(18, 1), MD(19, 0), MD(19, 1)}},
    {"SONG", {GL(GL_SROW_), GL(GL_SPAT_), GL(GL_SREP_), GL(GL_SMUTE_)}},
    {"SONG", {GL(GL_SON_), GL(GL_SLEN_), GL(GL_SEND_), NO}},
    {"KEYBOARD", {TF(TF_KSCALE_), TF(TF_KROOT_), TF(TF_KFOLD_), TF(TF_KCHORD_)}},
    {"MIDI", {GL(GL_MIDI0_), GL(GL_MIDI1_), GL(GL_MIDI2_), GL(GL_MIDI3_)}},
    {"MIDI", {GL(GL_MIDIIN_), GL(GL_TRKCH_), GL(GL_MTRK_), NO}},
    {"DISPLAY", {GL(GL_THEME_), NO, NO, NO}},
};

enum { G_TRIG, G_SYN, G_ENV, G_LFO, G_FX, G_ARP, G_GLO, G_HOME, G_SAVE, NG };
static const struct { const row_t *rows; uint8_t n; } GROUP[NG] = {
#define NR(r) sizeof r / sizeof r[0]
    {R_TRIG, NR(R_TRIG)}, {R_SYN, 7}, {R_ENV, NR(R_ENV)}, {R_LFO, NR(R_LFO)}, {R_FX, NR(R_FX)}, {R_ARP, NR(R_ARP)},
    {R_GLO, NR(R_GLO)}, {0, 0}, {0, 0},
#undef NR
};

/* a group's rows as the track's engine has them: DRUM has its own SYN pages [TONIC] */
static int drum_trk(void) { return tsnd()->s[S_ENGINE] == ENG_DR; }
static int va_trk(void) { return tsnd()->s[S_ENGINE] == ENG_VA; }
static const row_t *grp_rows(int g)
{
    if (g == G_TRIG && drum_trk()) return R_TRIG_DR;
    return g == G_SYN && drum_trk() ? R_DRM : g == G_SYN && va_trk() ? R_VA : GROUP[g].rows;
}
static int grp_n(int g)
{
    if (g == G_TRIG && drum_trk()) return sizeof R_TRIG_DR / sizeof R_TRIG_DR[0];
    return g == G_SYN && drum_trk() ? NDRM_ROWS : g == G_SYN && va_trk() ? NVA_ROWS : GROUP[g].n;
}
static int is_roll(const row_t *r) { return r && r->it[0].kind == IK_VIEW; }   /* SEQ's piano roll row */

/* a row's title as the track's engine shows it: under VA the SYN pages are OSC and MOD ENV; a DRUM kit's pads
 * show their models (BD SD) [TONIC] */
static const char *pad_label(int k);
static int syn_row(void);                    /* ui.c: the SYN group's row now */
static const char *row_title(const row_t *r)
{
    if (r >= R_DRM && r < R_DRM + NDRM_PAD_ROWS)     /* a pad's page: its number and model, "1 BD" */
        return pad_label((int)(r - R_DRM) / 2);
    if (r >= R_SYN && r < R_SYN + 7 && tsnd()->s[S_ENGINE] == ENG_VA) {
        static const char *const VT[7] = {"OSC", "OSC", "OSC 2", "ENV", "ENV", "ENV 2", "ENV 2"};
        return VT[r - R_SYN];
    }
    return r->title;
}

/* ------------------------------------------------------------------- the lists --- */
static const char *const L_OFFON[] = {"OFF", "ON"};
static const char *const L_ENGINE[NENG] = {"FM", "VA", "DRUM"};
/* (0.9.1: list values short enough for a knob's box, at most 5 characters) */
static const char *const L_ACH[NACH] = {"OFF", "MAJ", "MIN", "SUS2", "SUS4", "7TH", "MAJ7", "MIN7", "DIM", "AUG", "POWER",
                                        "OCT", "SCL3", "SCL4", "6TH", "MIN6", "ADD9", "MAJ9", "MIN9",
                                        "7SUS4", "9TH", "QUART", "CLUST"};
static const char *const L_CV[NCV] = {"CLOSE", "OPEN", "DROP2", "WIDE"};
static const char *const L_SD[NSD] = {"UP", "DOWN", "ALT"};
static const char *const L_ENOTE[NEN] = {"FIXED", "UP", "UP DN", "RAND", "CHORD"};
/* the named rhythms [TONIC 0.8]: (0) EUCLID itself, then patterns a bit a step */
#define NRHY 11
static const char *const L_RHY[NRHY] = {"EUCL", "TRESL", "CINQL", "SON32", "SON23", "RUMBA", "BOSSA",
                                        "GAHU", "FOUR", "OFFBT", "BACKB"};
static const struct { uint8_t len; uint16_t mask; } RHY[NRHY] = {
    {0, 0}, {8, 1u | 1u << 3 | 1u << 6}, {8, 1u | 1u << 2 | 1u << 3 | 1u << 5 | 1u << 6},
    {16, 1u | 1u << 3 | 1u << 6 | 1u << 10 | 1u << 12}, {16, 1u << 2 | 1u << 4 | 1u << 8 | 1u << 11 | 1u << 14},
    {16, 1u | 1u << 3 | 1u << 7 | 1u << 10 | 1u << 12}, {16, 1u | 1u << 3 | 1u << 6 | 1u << 10 | 1u << 13},
    {16, 1u | 1u << 3 | 1u << 6 | 1u << 10 | 1u << 14}, {16, 1u | 1u << 4 | 1u << 8 | 1u << 12},
    {16, 1u << 2 | 1u << 6 | 1u << 10 | 1u << 14}, {16, 1u << 4 | 1u << 12}};
#define NTHEME 5
static const char *const L_THEME[NTHEME] = {"OLED", "AMBER", "PHOSPHOR", "BLUE", "PAPER"};
static const char *const L_ZOOM[] = {"16", "32", "64"};
static const char *const L_ARPMODE[NARP] = {"OFF", "TRUE", "UP", "DOWN", "CYCL", "RAND", "CHRD"};
static const char *const L_PLAY[] = {"POLY", "POLY M", "MONO", "LEGATO"};
static const char *const L_PRIO[] = {"LAST", "LOW", "HIGH"};
static const char *const L_V2V[] = {"OFF", "LOG", "LIN", "EXP"};
static const char *const L_PTYPE[] = {"TRACK", "VOICE", "LEGATO"};
static const char *const L_PSLOPE[] = {"RATE", "TIME"};
static const char *const L_PSTYLE[] = {"GLIDE", "GLISS"};
static const char *const L_STEAL[] = {"CYCLE", "TRACK", "LO", "HI"};
static const char *const L_LMODE[] = {"PTRN", "TRACK"};
static const char *const L_TMODE[] = {"PROJ", "PTRN"};
static const char *const L_VELS[] = {"FIXED", "KEYS"};
static const char *const L_CLK[] = {"OFF", "ON"};
static const char *const L_SEND[] = {"LOOP", "STOP"};
static const char *const L_NOTES[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
/* (0.12) the sound's scales (KSCALE_MASK, CHROM as OFF) */
static const char *const L_SCALE12[NKSCALE] = {"OFF", "MAJOR", "MINOR", "DORIA", "PHRYG", "LYDIA", "MIXO", "LOCRI",
                                               "PENTA", "PENTm", "HARMm", "WHOLE"};
/* (0.12) a pad's two controls, by its model ("---": the model has no second one); 4 letters: the kit's pages have
 * the pads' picture beside them */
static const char *dr_ctl_name(int md, int b)
{
    static const char *const N[NDM][2] = {
        [DM_BD808] = {"BEND", "TICK"}, [DM_BD909] = {"BEND", "TICK"}, [DM_BDFM] = {"BEND", "TICK"},
        [DM_SD808] = {"SNAP", "BODY"}, [DM_SD909] = {"SNAP", "BODY"}, [DM_RIM] = {"HARD", "TUN2"},
        [DM_CLAP] = {"SPRD", "TAIL"}, [DM_HHC] = {"NOIS", "RESO"}, [DM_HHO] = {"NOIS", "RESO"},
        [DM_CYMB] = {"NOIS", "RESO"}, [DM_TOM] = {"BEND", "TICK"}, [DM_CONGA] = {"BEND", "TICK"},
        [DM_COWB] = {"TUN2", "RESO"}, [DM_CLAVE] = {"HARD", "---"}, [DM_SHAKR] = {"RISE", "RESO"},
        [DM_ZAP] = {"BEND", "TICK"}, [DM_NOISE] = {"RESO", "---"}, [DM_CLAP2] = {"SPRD", "TAIL"},
        [DM_TAMB] = {"NOIS", "RESO"}, [DM_MARAC] = {"RISE", "RESO"}, [DM_LASER] = {"BEND", "TICK"},
        [DM_BDSUB] = {"BEND", "TICK"}, [DM_BDDIST] = {"BEND", "TICK"}, [DM_BDPNCH] = {"BEND", "TICK"},
        [DM_SDFAT] = {"SNAP", "BODY"}, [DM_SD606] = {"SNAP", "BODY"}, [DM_SDBRSH] = {"RISE", "RESO"},
        [DM_H909C] = {"NOIS", "RESO"}, [DM_H909O] = {"NOIS", "RESO"}, [DM_HHFM] = {"TUN2", "FM"},
        [DM_H606] = {"RESO", "---"}};
    return N[md][b & 1] ? N[md][b & 1] : "---";
}
static const char *const L_KSCALE[NKSCALE] = {"CHROM", "MAJOR", "MINOR", "DORIA", "PHRYG", "LYDIA", "MIXO",
                                               "LOCRI", "PENT+", "PENT-", "HMIN", "WHOLE"};

static const char *const L_KCHORD[] = {"OFF", "ON", "MAJ", "MIN", "7TH", "MIN7", "MAJ7", "DIM", "DIM7"};

/* text without a libc */
static void tx_cpy(char *d, const char *s) { while ((*d++ = *s++)) ; }
static int tx_len(const char *s) { int n = 0; while (s[n]) n++; return n; }

/* a kit's pad as its knobs are named: its number and its model's letters, "1 BD" [TONIC] */
static const char *pad_label(int k)
{
    static char b[NPAD][6];                      /* (a buffer a pad: the tab bar names all eight at once) */
    char *t = b[k & 7];
    const char *m = DM_SHORT[tsnd()->s[S_DM1 + (k & 7)] % NDM];
    t[0] = (char)('1' + (k & 7)); t[1] = ' '; t[2] = m[0]; t[3] = m[1]; t[4] = 0;
    return t;
}
/* the models as the knob's box shows them (the name above it has the instrument): its flavour */
static const char *const DM_VAR[NDM] = {"808", "909", "FM", "808", "909", "RIM", "CLAP", "CL", "OP", "CYM", "TOM",
                                        "CONG", "BELL", "CLAV", "SHKR", "ZAP", "NOIS", "CLP2", "TAMB", "MARA", "LASR",
                                        "SUB", "DIST", "PNCH", "FAT", "606", "BRSH", "909", "909", "FM", "606"};

/* an item's name, range, quantum, display */
static const char *item_name(const item_t *it)
{
    /* (0.9.1: names that fit beside each other, at most 5 characters) */
    static const char *const TFN[] = {"LEN", "SCALE", "TRNSP", "VOICE", "UNISN", "SPRD", "LAYER", "SCALE",
                                      "ROOT", "FOLD", "KCHRD", "MICRO"};
    static const char *const PFN[] = {"BPM", "SWING", "MODE", "MLEN", "CHLEN", "STEAL", "REUSE"};
    static const char *const GLN[] = {"TEMPO", "CKIN", "CKOUT", "QUANT", "VEL", "FIXVL", "METRO",
                                      "CH T1", "CH T2", "CH T3", "CH T4", "AUTO", "TRKCH", "EXT", "ROW", "PTN", "REPT", "MUTE", "SONG", "ROWS", "END", "THEME"};   /* EXT: the active track plays out over MIDI only */
    static const char *const STN[NSS] = {"KS A", "KS B1", "KS B2", "KTRK", "TYPE", "SLOPE", "AMT", "STYLE",
                                         "GATE", "PLAY", "PRIO", "V>VOL", "PBDEP", "OCT", "MODE", "SPD", "RNG",
                                         "LEN", "STEPS", "ENG", "PAD 1", "PAD 2", "PAD 3", "PAD 4",
                                         "PAD 5", "PAD 6", "PAD 7", "PAD 8", "CHORD", "INV", "VOICE", "BASS", "STRUM",
                                         "DIR", "SCALE", "ROOT", "CHORD"};
    static const char *const ASN[] = {"STEP", "OFFSET", "ACTIVE"};
    static const char *const LVN[] = {"TRK1", "TRK2", "TRK3", "TRK4"};
    switch (it->kind) {
    case IK_SND:
        if (drum_trk()) {                            /* a kit's levels: the pad, "1 BD" */
            int k;
            for (k = 0; k < NPAD; k++)
                if (DRP[k][DK_LEV] == it->id)      /* (on a pad's own page: LEV) */
                    return grp_rows(G_SYN)[syn_row() % NDRM_ROWS].title == T_LEVEL ? pad_label(k) : "LEV";
            if (it->id >= P_DA1 && it->id <= P_DB8) {   /* (0.12) the controls as the pad's model names them */
                int b = it->id >= P_DB1, pd = it->id - (b ? P_DB1 : P_DA1);
                return dr_ctl_name(tsnd()->s[S_DM1 + pd] % NDM, b);
            }
        }
        return param_desc(tsnd()->s[S_ENGINE], it->id)->name;
    case IK_TRK: case IK_FX: return PDESC[it->id].name;
    case IK_LVL: return LVN[it->id & 3];
    case IK_SET: return it->id >= S_DM1 && it->id <= S_DM8 ? pad_label(it->id - S_DM1) : STN[it->id];
    case IK_TRKF: return TFN[it->id];
    case IK_PAT: return PFN[it->id];
    case IK_GLB: return GLN[it->id];
    case IK_MOD: return (it->id & 1) ? "DEPTH" : "DEST";
    case IK_ARPS: return ASN[it->id];
    case IK_VIEW: return it->id == VW_LO_ ? "VIEW" : "ZOOM";
    case IK_EUC: {
        static const char *const EN[NEU_] = {"LEN", "HITS", "ROT", "STYLE", "ACC", "AROT", "GHOST", "PROB", "NOTES",
                                             "RANGE"};
        if (it->id >= EU_KIT0_)                      /* a kit's pad: its label, "1 BD" */
            return pad_label(it->id - EU_KIT0_);
        return EN[it->id % NEU_];
    }
    default: return "";
    }
}

/* the COND knob runs in the OS's list order [FIRMWARE 0x401a75f0]: 1 % .. 99 %, then none (100 %), FILL !FILL PRE
 * !PRE NEI !NEI 1ST !1ST, 1:2 .. 8:8; the trig keeps its C_* value */
static int cond_pos(int c)
{
    if (c >= C_PROB0 && c < C_AB0) return c - C_PROB0;
    if (c == C_NONE) return 21;
    if (c < C_PROB0) return 21 + c;
    return c;
}
static int pos_cond(int p)
{
    if (p < 21) return C_PROB0 + p;
    if (p < 30) return p - 21;
    return p;
}

typedef struct { int lo, hi, q; uint8_t fmt; const char *const *list; } range_t;
/* a destination the item can take: not LFO 1's own (on LFO 1), not one the engine lacks ("---") */
static int dest_ok(const item_t *it, int eng, int c)
{
    if (c == D_NONE)
        return 1;
    if (it->kind == IK_SND && it->id == P_L1DST && c >= D_L1SPD && c <= D_L1DEP)
        return 0;
    return !(dest_name(eng, c)[0] == '-' && dest_name(eng, c)[1] == '-');
}
static range_t item_range(const item_t *it)
{
    range_t r = {0, 127, 1, F_INT, 0};
    switch (it->kind) {
    case IK_SND: case IK_TRK: case IK_FX: {
        const pdesc_t *d = it->kind == IK_SND ? param_desc(tsnd()->s[S_ENGINE], it->id) : &PDESC[it->id];
        r.lo = d->lo; r.hi = d->hi; r.q = d->q; r.fmt = d->fmt; r.list = d->list;
        if (it->kind == IK_FX && (it->id == P_CPATK || it->id == P_CPREL))   /* (in ms) */
            r.fmt = it->id == P_CPATK ? 245 : 244;
        if (it->id == P_PROB && step_held_any()) {   /* a held trig: COND instead of PROB [MANUAL 11.2.4] */
            r.lo = 0; r.hi = C_END - 1; r.fmt = 250;
        }
        break;
    }
    case IK_LVL: break;
    case IK_SET:
        switch (it->id) {
        case S_FKTRK: r.hi = 100; break;
        case S_PTYPE: r.hi = 2; r.fmt = F_LIST; r.list = L_PTYPE; break;
        case S_PSLOPE: r.hi = 1; r.fmt = F_LIST; r.list = L_PSLOPE; break;
        case S_PAMT: r.hi = 100; break;
        case S_PSTYLE: r.hi = 1; r.fmt = F_LIST; r.list = L_PSTYLE; break;
        case S_PGATE: r.hi = 1; r.fmt = F_LIST; r.list = L_OFFON; break;
        case S_PLAYMODE: r.hi = 3; r.fmt = F_LIST; r.list = L_PLAY; break;
        case S_MONOPRIO: r.hi = 2; r.fmt = F_LIST; r.list = L_PRIO; break;
        case S_VEL2VOL: r.hi = 3; r.fmt = F_LIST; r.list = L_V2V; break;
        case S_PBDEP: r.hi = 24; break;
        case S_OCT: r.hi = 4; r.fmt = 251; break;
        case S_ARPMODE: r.hi = NARP - 1; r.fmt = F_LIST; r.list = L_ARPMODE; break;
        case S_ARPSPD: r.hi = 10; r.fmt = F_LIST; r.list = ARP_DIV_NAME; break;
        case S_ARPRNG: r.lo = 1; r.hi = 4; break;
        case S_ARPLEN: r.hi = LEN_INF - 1; r.fmt = F_LEN; break;
        case S_ARPSTEPS: r.lo = 1; r.hi = 16; break;
        case S_ENGINE: r.hi = NENG - 1; r.fmt = F_LIST; r.list = L_ENGINE; break;
        case S_DM1: case S_DM2: case S_DM3: case S_DM4: case S_DM5: case S_DM6: case S_DM7: case S_DM8:
            r.hi = NDM - 1; r.fmt = F_LIST; r.list = DM_NAME; break;
        case S_ARPCHD:                           /* (0.12: with a scale, the chords by degrees) */
            if (tsnd()->s[S_SCALE]) { r.hi = NDCH - 1; r.fmt = F_LIST; r.list = DCH_NAME; }
            else { r.hi = NACH - 1; r.fmt = F_LIST; r.list = L_ACH; }
            break;
        case S_SCALE: r.hi = NKSCALE - 1; r.fmt = F_LIST; r.list = L_SCALE12; break;
        case S_SCROOT: r.hi = 11; r.fmt = F_LIST; r.list = L_NOTES; break;
        case S_ARPINV: r.hi = 3; break;
        case S_CHVOIC: r.hi = NCV - 1; r.fmt = F_LIST; r.list = L_CV; break;
        case S_CHBASS: r.hi = 1; r.fmt = F_LIST; r.list = L_OFFON; break;
        case S_CHSTRM: r.fmt = 246; break;           /* (ms) */
        case S_CHSDIR: r.hi = NSD - 1; r.fmt = F_LIST; r.list = L_SD; break;
        default: break;
        }
        break;
    case IK_TRKF:
        switch (it->id) {
        case TF_LEN_: r.lo = 2; r.hi = 64; break;
        case TF_SCALE_: r.hi = 6; r.fmt = F_LIST; r.list = SCALE_NAME; break;
        case TF_TRANSP_: r.lo = -36; r.hi = 36; r.fmt = F_BIP; break;
        case TF_VOICES_: r.hi = NVOICE; r.fmt = 252; break;
        case TF_UNI_: r.lo = 1; r.hi = 4; break;
        case TF_SPREAD_: break;
        case TF_LAYER_: r.hi = 15; r.fmt = 253; break;
        case TF_KSCALE_: r.hi = NKSCALE - 1; r.fmt = F_LIST; r.list = L_KSCALE; break;
        case TF_KROOT_: r.hi = 11; r.fmt = F_LIST; r.list = L_NOTES; break;
        case TF_KFOLD_: r.hi = 1; r.fmt = F_LIST; r.list = L_OFFON; break;
        case TF_KCHORD_: r.hi = 8; r.fmt = F_LIST; r.list = L_KCHORD; break;
        case TF_QUANT_: r.hi = 23; break;
        }
        break;
    case IK_PAT:
        switch (it->id) {
        case PF_BPM_: r.lo = 300; r.hi = 3000; r.q = 10; r.fmt = 254; break;
        case PF_SWING_: r.lo = 50; r.hi = 80; r.fmt = F_PCT; break;
        case PF_LMODE_: r.hi = 1; r.fmt = F_LIST; r.list = L_LMODE; break;
        case PF_MLEN_: case PF_CHLEN_: r.hi = 255; r.fmt = 255; break;
        case PF_STEAL_: r.hi = 3; r.fmt = F_LIST; r.list = L_STEAL; break;
        case PF_REUSE_: r.hi = 1; r.fmt = F_LIST; r.list = L_OFFON; break;
        }
        break;
    case IK_GLB:
        switch (it->id) {
        case GL_TMODE_: r.hi = 1; r.fmt = F_LIST; r.list = L_TMODE; break;
        case GL_CLKIN_: case GL_CLKOUT_: case GL_MET_: r.hi = 1; r.fmt = F_LIST; r.list = L_CLK; break;
        case GL_QUANT_: case GL_TRKCH_: r.hi = 1; r.fmt = F_LIST; r.list = L_OFFON; break;
        case GL_MTRK_: r.hi = 1; r.fmt = F_LIST; r.list = L_OFFON; break;
        case GL_SROW_: r.lo = 1; r.hi = 32; break;
        case GL_SPAT_: r.hi = NPAT - 1; r.fmt = 248; break;
        case GL_SREP_: r.lo = 1; r.hi = 32; break;
        case GL_SMUTE_: r.hi = 15; r.fmt = 253; break;
        case GL_SON_: r.hi = 1; r.fmt = F_LIST; r.list = L_OFFON; break;
        case GL_SLEN_: r.lo = 1; r.hi = 32; break;
        case GL_SEND_: r.hi = 1; r.fmt = F_LIST; r.list = L_SEND; break;
        case GL_THEME_: r.hi = NTHEME - 1; r.fmt = F_LIST; r.list = L_THEME; break;
        case GL_VELS_: r.hi = 1; r.fmt = F_LIST; r.list = L_VELS; break;
        case GL_VELF_: r.lo = 1; break;
        default: r.hi = 16; r.fmt = 249; break;       /* MIDI channels: 1..16, OFF */
        }
        break;
    case IK_MOD:
        if (it->id & 1) { r.lo = -128; r.hi = 127; r.fmt = F_BIP; }
        else { r.hi = NDEST - 1; r.fmt = F_DEST; }   /* (0.9: every destination, LFO 1's parameters too) */
        break;
    case IK_ARPS:
        if (it->id == AS_STEP_) { r.lo = 1; r.hi = 16; }
        else if (it->id == AS_OFS_) { r.lo = -24; r.hi = 24; r.fmt = F_BIP; }
        else { r.hi = 1; r.fmt = F_LIST; r.list = L_OFFON; }
        break;
    case IK_VIEW:
        if (it->id == VW_LO_) { r.lo = -1; r.hi = 108; r.fmt = 247; }   /* AUTO, or the lowest note shown */
        else { r.hi = 2; r.fmt = F_LIST; r.list = L_ZOOM; }
        break;
    case IK_EUC:
        switch (it->id) {
        case EU_LEN_: r.lo = 1; r.hi = 64; break;
        case EU_PULS_: case EU_AH_: r.hi = euc_get(EU_LEN_); break;
        case EU_ROT_: case EU_AR_: r.hi = euc_get(EU_LEN_) - 1; break;
        case EU_PAT_: r.hi = NRHY - 1; r.fmt = F_LIST; r.list = L_RHY; break;
        case EU_GHOST_: r.hi = 100; r.fmt = F_PCT; break;
        case EU_PROB_: r.lo = 1; r.hi = 100; r.fmt = F_PCT; break;
        case EU_NMODE_: r.hi = NEN - 1; r.fmt = F_LIST; r.list = L_ENOTE; break;
        case EU_NRANGE_: r.lo = 1; r.hi = 3; break;
        default: r.hi = euc_pad_len(it->id - EU_KIT0_); break;   /* a kit pad's hits */
        }
        break;
    default: r.hi = 0; break;
    }
    return r;
}

/* the value a lockable item would lock: the parameter id, or -1 */
static int item_lock_param(const item_t *it)
{
    if (it->kind == IK_SND || it->kind == IK_FX)
        return it->id;
    if (it->kind == IK_TRK && it->id != P_TLVL)
        return it->id;
    return -1;
}

static void tx_num(char *b, int v)
{
    char t[12];
    int n = 0, neg = v < 0;
    unsigned u = (unsigned)(neg ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (neg)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}
static void tx_fix(char *b, int v, int div, int dec)    /* v / div with dec decimals */
{
    int neg = v < 0, whole, fr, k, p = 1;
    for (k = 0; k < dec; k++)
        p *= 10;
    if (neg)
        v = -v;
    whole = v / div;
    fr = (v % div) * p / div;
    if (neg)
        *b++ = '-';
    tx_num(b, whole);
    while (*b)
        b++;
    if (dec) {
        *b++ = '.';
        for (k = p / 10; k; k /= 10) {
            *b++ = (char)('0' + (fr / k) % 10);
        }
        *b = 0;
    }
}
static void tx_ratio(char *b, float r)
{
    int x = (int)(r * 100.0f + 0.5f);
    if (x < 100) {                                   /* .25 .50 .75: the Digitone's way */
        b[0] = '.';
        b[1] = (char)('0' + x / 10);
        b[2] = (char)('0' + x % 10);
        b[3] = 0;
    } else
        tx_fix(b, x, 100, x >= 1000 ? 0 : 2);
}
static void tx_cat(char *d, const char *s) { while (*d) d++; while (*s) *d++ = *s++; *d = 0; }

static void tx_len_idx(char *b, int li)
{
    uint16_t t;
    if (li >= LEN_INF) {
        tx_cpy(b, "INF");
        return;
    }
    t = LEN_TICKS[li];                               /* 1/96 step */
    switch (t) {                                     /* [FIRMWARE: the OS's LEN strings] powers of two as notes */
    case 24: tx_cpy(b, "1/64"); return;
    case 48: tx_cpy(b, "1/32"); return;
    case 96: tx_cpy(b, "1/16"); return;
    case 192: tx_cpy(b, "1/8"); return;
    case 384: tx_cpy(b, "1/4"); return;
    case 768: tx_cpy(b, "1/2"); return;
    default: break;
    }
    if (t < 96) {                                    /* .125 .. .938 */
        int m = (t * 1000 + 48) / 96;
        b[0] = '.';
        b[1] = (char)('0' + m / 100);
        b[2] = (char)('0' + m / 10 % 10);
        b[3] = (char)('0' + m % 10);
        b[4] = 0;
    } else if (t < 960)                              /* 1.06 .. 9.50, rounded as the OS shows them */
        tx_fix(b, (t * 100 + 48) / 96, 100, 2);
    else if (t < 9600)                               /* 10.0 .. 96.0 */
        tx_fix(b, (t * 10 + 48) / 96, 10, 1);
    else                                             /* 100 .. 128 */
        tx_num(b, (t + 48) / 96);
}

static void item_fmt(const item_t *it, int v, char *b)
{
    range_t r = item_range(it);
    b[0] = 0;
    switch (r.fmt) {
    case F_INT: tx_num(b, v); break;
    case F_INT1: tx_num(b, v + 1); break;
    case F_BIP: if (v > 0) { b[0] = '+'; tx_num(b + 1, v); } else tx_num(b, v); break;
    case F_PAN:
        if (!v) tx_cpy(b, "C");
        else { b[0] = v < 0 ? 'L' : 'R'; tx_num(b + 1, v < 0 ? -v : v); }
        break;
    case F_LIST:
        if (it->kind == IK_FX && it->id == P_SCPAD && v > 0) {   /* a kit source's pad: its model [0.10] */
            int src = pcur->fx[P_SCSRC - P_CDEP] - 1;
            if (src >= 0 && src < NTRK && pcur->snd[src].s[S_ENGINE] == ENG_DR) {
                b[0] = (char)('0' + v); b[1] = ' ';
                tx_cpy(b + 2, DM_SHORT[pcur->snd[src].s[S_DM1 + v - 1] % NDM]);
                break;
            }
        }
        tx_cpy(b, r.list && v >= 0 && v <= r.hi ? r.list[v] : "?");
        break;
    case F_X4: tx_fix(b, v, 4, 2); break;
    case F_X16: tx_fix(b, v, 16, 2); break;
    case F_RATC: tx_ratio(b, RATIO_C[clamp(v, 0, NRATC - 1)]); break;
    case F_RATA: tx_ratio(b, RATIO_A[clamp(v, 0, NRATA - 1)]); break;
    case F_RATB: {
        char t[8];
        tx_ratio(b, RATIO_B[clamp(v, 0, NRATB * NRATB - 1) / NRATB]);
        tx_cat(b, "/");
        tx_ratio(t, RATIO_B[clamp(v, 0, NRATB * NRATB - 1) % NRATB]);
        tx_cat(b, t);
        break;
    }
    case F_OFS: if (v > 0) { b[0] = '+'; tx_fix(b + 1, v, 1000, 3); } else tx_fix(b, v, 1000, 3); break;
    case F_REL: if (v >= 127) tx_cpy(b, "INF"); else tx_num(b, v); break;
    case F_DEC: if (v >= 127) tx_cpy(b, "INF"); else tx_num(b, v); break;
    case F_NOTE:                                     /* a kit's notes: its pads [TONIC] */
        if (it->id == P_NOTE && drum_trk()) {
            int pd = pad_of(v);
            b[0] = (char)('1' + pd); b[1] = ' ';
            tx_cpy(b + 2, DM_SHORT[tsnd()->s[S_DM1 + pd] % NDM]);
        } else
            note_name(v, b);
        break;
    case F_LEN: tx_len_idx(b, v); break;
    case F_PCT: tx_num(b, v); tx_cat(b, "%"); break;
    case F_DEST: tx_cpy(b, dest_name(tsnd()->s[S_ENGINE], v)); break;
    case F_MULT: {
        static const char *const M[NMULT] = {"1", "2", "4", "8", "16", "32", "64", "128", "256", "512", "1K", "2K"};
        if (v >= NMULT) { b[0] = '.'; tx_cpy(b + 1, M[v - NMULT]); } else tx_cpy(b, M[v]);
        break;
    }
    case F_DTIME: {
        static const struct { int16_t v; const char *n; } DT[] = {
            {24, "1/128"}, {48, "1/64"}, {64, "1/32T"}, {96, "1/32"}, {128, "1/16T"}, {192, "1/16"}, {256, "1/8T"},
            {288, "1/16."}, {384, "1/8"}, {512, "1/4T"}, {576, "1/8."}, {768, "1/4"}, {1024, "1/2T"},
            {1152, "1/4."}, {1536, "1/2"}, {2304, "1/2."}, {3072, "1/1"}};
        unsigned k;
        for (k = 0; k < sizeof DT / sizeof DT[0]; k++)
            if (DT[k].v == v) {
                tx_cpy(b, DT[k].n);
                return;
            }
        tx_fix(b, v, 24, 2);
        break;
    }
    case F_OFFV: if (v <= 0) tx_cpy(b, "OFF"); else if (r.q > 1) tx_fix(b, v, r.q, 2); else tx_num(b, v); break;
    case F_SCALE: tx_cpy(b, SCALE_NAME[clamp(v, 0, 6)]); break;
    case 247: if (v < 0) tx_cpy(b, "AUTO"); else note_name(v, b); break;
    case 246: if (v <= 0) tx_cpy(b, "OFF"); else { tx_num(b, (v * 8 + 5) / 10); tx_cat(b, "MS"); } break;   /* STRUM */
    case 245: case 244: {                            /* COMP ATK 0.5 .. 32 ms, REL 20 ms .. 1.3 s */
        float ms = (r.fmt == 245 ? 0.5f : 20.0f) * fm_exp2f((float)v * (6.0f / 127.0f));
        if (ms >= 1000.0f) { tx_fix(b, (int)(ms / 10.0f + 0.5f), 100, 2); tx_cat(b, "S"); }
        else if (ms >= 10.0f) { tx_num(b, (int)(ms + 0.5f)); tx_cat(b, "MS"); }
        else { tx_fix(b, (int)(ms * 10.0f + 0.5f), 10, 1); tx_cat(b, "MS"); }
        break;
    }
    case 248: b[0] = (char)('A' + v / 16); b[1] = (char)('0' + (v % 16 + 1) / 10); b[2] = (char)('0' + (v % 16 + 1) % 10); b[3] = 0; break;
    case 249: if (v >= 16) tx_cpy(b, "OFF"); else tx_num(b, v + 1); break;
    case 250: {                                      /* trig conditions (v: the knob position) */
        v = pos_cond(v);
        if (v == C_NONE) tx_cpy(b, "---");
        else if (v == C_FILL) tx_cpy(b, "FILL");
        else if (v == C_NFILL) tx_cpy(b, "!FILL");
        else if (v == C_PRE) tx_cpy(b, "PRE");
        else if (v == C_NPRE) tx_cpy(b, "!PRE");
        else if (v == C_NEI) tx_cpy(b, "NEI");
        else if (v == C_NNEI) tx_cpy(b, "!NEI");
        else if (v == C_1ST) tx_cpy(b, "1ST");
        else if (v == C_N1ST) tx_cpy(b, "!1ST");
        else if (v < C_AB0) { tx_num(b, PROB_PCT[v - C_PROB0]); tx_cat(b, "%"); }
        else { tx_num(b, COND_AB[v - C_AB0][0]); tx_cat(b, ":"); tx_num(b + tx_len(b), COND_AB[v - C_AB0][1]); }
        break;
    }
    case 251: if (v == 2) tx_cpy(b, "0"); else { b[0] = v > 2 ? '+' : '-'; tx_num(b + 1, v > 2 ? v - 2 : 2 - v); } break;
    case 252: if (!v) tx_cpy(b, "DYN"); else tx_num(b, v); break;
    case 253: {
        int t;
        for (t = 0; t < NTRK; t++)
            b[t] = (v >> t & 1) ? (char)('1' + t) : '-';
        b[NTRK] = 0;
        break;
    }
    case 254: tx_fix(b, v, 10, 1); break;
    case 255: if (!v) tx_cpy(b, it->id == PF_MLEN_ ? "INF" : "OFF"); else tx_num(b, v); break;
    default: tx_num(b, v); break;
    }
}
