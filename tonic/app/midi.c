/* SPDX-License-Identifier: MPL-2.0
 * TONIC MIDI: notes in (channels 1-4 play tracks 1-4, the auto channel the active track), the Digitone's CC
 * numbers for the sound parameters [MANUAL App. B, CC MSB only], program change (patterns), mod wheel, breath,
 * aftertouch, pitch bend, clock in; out: the tracks' notes on their channels (engine.c), clock (when set).
 * Part of app.c. */

/* [MANUAL App. B] CC number -> parameter (P_*), 0 = none; 94 mute and 95 track level are handled apart */
static uint8_t CC_PARAM[128];
static void cc_map_init(void)
{
    static const uint8_t M[][2] = {
        {90, P_ALGO}, {91, P_RATC}, {92, P_RATA}, {16, P_RATB}, {17, P_HARM}, {18, P_DTUN}, {19, P_FDBK},
        {20, P_MIX}, {75, P_AATK}, {76, P_ADEC}, {77, P_AEND}, {78, P_ALEV}, {79, P_BATK}, {80, P_BDEC},
        {81, P_BEND}, {82, P_BLEV}, {83, P_ADEL}, {84, P_ATRG}, {85, P_ARST}, {86, P_BDEL}, {87, P_BTRG},
        {88, P_BRST}, {89, P_PHRT}, {23, P_FREQ}, {24, P_RESO}, {74, P_FTYP}, {70, P_FATK}, {71, P_FDEC},
        {72, P_FSUS}, {73, P_FREL}, {25, P_FENV}, {43, P_FDEL}, {26, P_BASE}, {27, P_WDTH}, {104, P_EATK},
        {105, P_EDEC}, {106, P_ESUS}, {107, P_EREL}, {9, P_DRV}, {10, P_PAN}, {7, P_VOL}, {12, P_CHR},
        {13, P_DEL}, {14, P_REV}, {102, P_AENR}, {28, P_L1SPD}, {108, P_L1MUL}, {109, P_L1FAD}, {110, P_L1DST},
        {111, P_L1WAV}, {112, P_L1SPH}, {113, P_L1MOD}, {29, P_L1DEP}, {30, P_L2SPD}, {114, P_L2MUL},
        {115, P_L2FAD}, {116, P_L2DST}, {117, P_L2WAV}, {118, P_L2SPH}, {119, P_L2MOD}, {31, P_L2DEP},
    };
    unsigned i;
    for (i = 0; i < 128; i++)
        CC_PARAM[i] = P_NONE;
    for (i = 0; i < sizeof M / sizeof M[0]; i++)
        CC_PARAM[M[i][0]] = M[i][1];
}

/* a CC value to a parameter: the enumerations and lists take the value as it is, the ranges are scaled */
static int cc_value(int t, int p, int v)
{
    const pdesc_t *d = param_desc(pcur->snd[t].s[S_ENGINE], p);
    int span = d->hi - d->lo;
    if (span <= 127 && d->q == 1 && d->lo >= -64 && d->hi <= 127 && (d->fmt == F_LIST || span < 64))
        return clamp(d->lo < 0 ? v - 64 : v, d->lo, d->hi);
    if (d->lo < 0)
        return clamp(d->lo + v * span / 127, d->lo, d->hi);
    return d->lo + v * span / 127;
}

static uint8_t midi_on_trk[16][64];    /* a MIDI note held: the track + 1 it plays on, a nibble a note [0.9.2] */
static int on_trk(int ch, int n) { return midi_on_trk[ch][n >> 1] >> ((n & 1) * 4) & 15; }
static void on_trk_set(int ch, int n, int v)
{
    uint8_t *b = &midi_on_trk[ch][n >> 1];
    *b = (uint8_t)((*b & ~(15 << ((n & 1) * 4))) | v << ((n & 1) * 4));
}
static int midi_track(int ch)
{
    if (set.midi_trk_ch && ch < NTRK)
        return ch;
    if (set.midi_in == 16 || ch == set.midi_in)
        return ui.trk;
    return -1;
}

static void midi_poll(void)
{
    uint32_t pkt;
    int port, n = 0;
    while (n++ < 64 && plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
        int ch = (int)(st & 15u), t;
        switch (st) {
        case 0xF8: engine_clock_tick(); continue;
        case 0xFA: engine_clock_start(0); continue;
        case 0xFB: engine_clock_start(1); continue;
        case 0xFC: engine_clock_stop(); continue;
        default: break;
        }
        if (st < 0x80 || st >= 0xF0)
            continue;
        t = midi_track(ch);
        if (t < 0)
            continue;
        switch (st & 0xF0u) {
        case 0x90:
            if (d2) {
                on_trk_set(ch, d1, t + 1);   /* (its note-off goes where it went) */
                engine_note_on(t, (int)d1, (int)d2);
                break;
            }
            /* fall through: velocity 0 is a note-off */
        case 0x80:
            if (on_trk(ch, d1)) {               /* the track the note-on went to, though the active track
                                                      * changed since [0.9.2] */
                t = on_trk(ch, d1) - 1;
                on_trk_set(ch, d1, 0);
            }
            engine_note_off(t, (int)d1);
            break;
        case 0xB0:
            if (d1 == 1) engine_mod(t, MS_MW, (int)d2);
            else if (d1 == 2) engine_mod(t, MS_BC, (int)d2);
            else if (d1 == 94) { eng.mute = (uint8_t)(d2 ? eng.mute | 1u << t : eng.mute & ~(1u << t)); }
            else if (d1 == 95) { pcur->trk[t].tp[P_TLVL - NSP] = (int16_t)d2; mark_dirty(); }
            else if (d1 == 123) engine_all_off(t);
            else if (CC_PARAM[d1] != P_NONE) {
                int p = CC_PARAM[d1], was = pcur->snd[t].p[p];
                pcur->snd[t].p[p] = (int16_t)cc_value(t, p, (int)d2);
                if (!pattern_fits()) {               /* (too big for the pattern: as it was) [0.9.2] */
                    pcur->snd[t].p[p] = (int16_t)was;
                    break;
                }
                engine_param_changed(t, p);
                if (eng.rec_live)
                    engine_rec_param(t, p, pcur->snd[t].p[p]);
                mark_dirty();
                ui.redraw = 1;
            }
            break;
        case 0xC0: pattern_select((int)d1 % NPAT); ui.redraw = 1; break;
        case 0xD0: engine_mod(t, MS_AT, (int)d1); break;
        case 0xE0: engine_mod(t, MS_PB, (int)((d2 << 7 | d1)) - 8192); break;
        default: break;
        }
    }
}

/* the engine's notes out (in the render) */
static void midi_out_cb(uint8_t st, uint8_t d1, uint8_t d2)
{
    uint32_t cin = (uint32_t)(st >> 4);
    plat_midi_send(cin | (uint32_t)st << 8 | (uint32_t)d1 << 16 | (uint32_t)d2 << 24, TO_USB);
}
static void clock_out_cb(int what)
{
    static const uint8_t B[3] = {0xF8, 0xFA, 0xFC};
    if (set.clock_out)
        plat_midi_send(0x0Fu | (uint32_t)B[what] << 8, TO_USB);
}
