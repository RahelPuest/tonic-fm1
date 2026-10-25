/* SPDX-License-Identifier: MPL-2.0
 * TONIC user interface: the FM-1's controls mapped to the Digitone's workflow. Part of app.c.
 *
 *   keys      KEYBOARD: play the active track. GRID (REC lit): the 16 white keys are the 16 trig keys of the
 *             page; the black keys are the page and step functions (below). STEP (SEL+REC): the keys enter notes
 *             and chords at the active step, which then advances.
 *   K1-K4     the four parameters of the active row; with a step held: its p-locks
 *   SELECT    the next / previous row (the home screen: tempo)
 *   ALGORITHM the active track
 *   PRESETS   the track's sound (sound pages), the pattern (home), the active step (step recording), a sound lock
 *             (a step held)
 *   SEQ TRIG page | EDIT SYN1, SYN2 | ENV FLTR, AMP | LFO LFO1, LFO2 | FX chorus, delay, reverb, mixer
 *   ARP arpeggiator | GLO tempo, scale, voices, sound setup, modulation, keyboard, MIDI | HOME | SAVE
 *             pressed again: the next row (on release); with SEL: the previous one. SEQ EDIT ENV LFO held: a knob edits all
 *             tracks (CONTROL ALL), REC randomizes the row, SEL reverts either
 *   PLAY      start / stop (SEL+PLAY: stop and cut the effects); REC+PLAY: live recording (PLAY twice: quantize)
 *   REC       grid recording; SEL+REC: step recording
 *   OCT- OCT+ the keyboard's octave; SEL: transpose the track; a step held: its note (SEL: by octaves)
 *   SEL       the Digitone's FUNC: SEL+white key in GRID places a lock trig; in KEYBOARD, SEL+keys 1-4 pick tracks
 *   HOME held the performance layer: white keys pick patterns (several in a row: a chain), black keys 1-4 mute the
 *             tracks, 5 FILL while held, 6 FILL for a pass, 9 temporary save, 10 reload, 11 restart
 * GRID black keys: 1-4 step pages (with a white key: the track's length), 5 LOCK (with a white key: a lock trig),
 * 6 / 7 micro timing of the held steps, 9 COPY, 10 PASTE, 11 CLEAR (steps held: their locks; else the page). */

static const uint8_t WK[16] = PLAT_WHITE_KEYS, BK[11] = PLAT_BLACK_KEYS;
static int8_t key_white[NKEYS], key_black[NKEYS];   /* key -> white index / black index, -1 */

static struct {
    uint8_t trk, grp;
    uint8_t row[NG];
    uint8_t grid, steprec, step_cur, spage;
    int8_t oct;
    uint32_t btn, keys;
    uint32_t btn_t[NB];           /* when each button went down */
    uint64_t held;                /* grid: steps held (absolute) */
    uint64_t held_new;            /* placed by this press (not to be removed on release) */
    uint32_t held_t[NSTEP];
    uint8_t held_edit;            /* something was edited while steps were held */
    uint8_t key_note[NKEYS], key_trk[NKEYS];
    uint8_t key_how[NKEYS];       /* how a key went down (its release does the same): KH_* [0.9.2] */
    uint8_t key_chord[NKEYS];     /* the keyboard CHORD it played with */
    uint8_t arp_step, sound_i, song_row;
    uint8_t help_btn, help_block; /* a button held alone: its help after a second (help_block: it was used) */
    uint32_t taps[4];             /* tap tempo: the last taps */
    uint32_t plist_t;             /* PRESETS turned: when (the list window closes 6 s after the last turn) */
    uint8_t plist_on, plist_i;    /* the list window is open; its cursor (OCT+ loads it, OCT- closes) */
    uint8_t perf_lock;            /* HOME tapped twice: the performance layer stays (a tap on HOME leaves) */
    uint32_t home_tap;            /* the last short tap on HOME (ms) */
    uint8_t ntap;
    int8_t roll_lo;               /* the piano roll: the lowest note shown (-1 AUTO), the zoom (16 << z steps) */
    uint16_t pfx_keys;            /* the white keys holding a momentary effect (bit: key) [TONIC 0.7] */
    uint8_t euc[NTRK][NPAD + 1][NEU_];   /* the euclidean generator [TONIC 0.6, 0.8]: EU_* a track's (its last slot) or
                                          * a kit pad's (not saved: the trigs it wrote are) */
    uint32_t euc_seed;                /* its dice (a kit's ghosts and chances: rolled at each write) */
    uint8_t roll_zoom;
    int8_t save_row;
    uint8_t save_slot, confirm;
    uint32_t confirm_t;
    char msg[2][24];
    uint32_t msg_until;
    uint8_t redraw, perf, perf_chain_n, perf_chain[16];
    uint32_t enc_t[NE];
    uint32_t touch_t;             /* when a knob K1-K4 last moved a value (the screen shows numbers then) */
    uint8_t touch_k;
    uint8_t clear_held, live_quant_tap;
    uint32_t rec_play_t;
    /* step recording: the notes of the chord being entered */
    uint8_t sr_n, sr_notes[NNOTE], sr_vel, sr_down;
    uint64_t rec_pass[NTRK];      /* live recording: steps written in this pass */
    uint32_t rec_loop[NTRK];
    /* clipboard */
    trig_t clip[NSTEP];
    uint8_t clip_n, clip_ok;
    plock_t clip_lock[NLOCK];      /* (a whole track's: up to the pattern's pool) [0.9.2] */
    uint16_t clip_nlock;
    /* CONTROL ALL and page randomize (a page button held): the sounds before, for SEL to revert */
    uint8_t pg_btn, pg_back, pg_used, pg_snap_ok;
    int16_t pg_snap[NTRK][NSP];
    int16_t pg_tsnap[NTRK][NTP_END - NSP];
    uint32_t rnd;
} ui;

static void lock_compact_safe(void);
static int page_held(void);
static void control_all(const item_t *it, int v);
static int step_held_any(void) { return ui.held != 0 || ui.steprec; }
static const row_t *cur_row(void) { return grp_n(ui.grp) ? &grp_rows(ui.grp)[ui.row[ui.grp] % grp_n(ui.grp)] : 0; }
static int cur_step_base(void) { return ui.spage * 16; }

void app_say(const char *a, const char *b)
{
    int i;
    for (i = 0; i < 23 && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23 && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1200u;
    ui.redraw = 1;
}

/* ------------------------------------------------------------------ item values --- */
static sound_t *tsnd(void) { return &pcur->snd[ui.trk]; }
/* a kit's trig: where it plays pad `pad` (the note's index in its chord), or -1 [TONIC] */
static int trig_pad(const trig_t *g, int pad)
{
    int i;
    if (!(g->flags & TF_NOTE))
        return -1;
    for (i = 0; i < (g->nnote ? g->nnote : 1) && i < NNOTE; i++)
        if (pad_of(g->note + (i ? g->chord[i - 1] : 0)) == pad)
            return i;
    return -1;
}
static int cur_pad(void);
static void euc_set(int k, int v);
static void pad_pick(int p);
static track_t *ttrk(void) { return &pcur->trk[ui.trk]; }

/* the first held step (grid) or the active step (step recording): its trig, or 0 */
static int first_held(void)
{
    int s;
    if (ui.steprec)
        return ui.step_cur;
    for (s = 0; s < NSTEP; s++)
        if (ui.held >> s & 1u)
            return s;
    return -1;
}


/* quantize after recording: MICRO shows the track's largest micro timing; turning it down scales every offset of
 * the track (and, with SEL, of all tracks) by new / old, so 0 puts the trigs on the grid [MANUAL: QUANTIZE] */
static int micro_max(int t)
{
    int s, m = 0;
    for (s = 0; s < NSTEP; s++) {
        int u = pcur->trig[t][s].micro;
        if (pcur->trig[t][s].flags & (TF_NOTE | TF_LOCK))
            m = u < 0 ? (-u > m ? -u : m) : (u > m ? u : m);
    }
    return m;
}
static void micro_scale(int t, int v, int cur)
{
    int s;
    if (cur <= 0 || v >= cur)
        return;
    for (s = 0; s < NSTEP; s++) {
        trig_t *g = &pcur->trig[t][s];
        g->micro = (int8_t)(g->micro * v / cur);
    }
}

static int item_get(const item_t *it, int *locked)
{
    int s = first_held(), lp = item_lock_param(it);
    *locked = 0;
    if (s >= 0 && (it->kind == IK_SND || it->kind == IK_TRK || it->kind == IK_FX)) {
        trig_t *g = &pcur->trig[ui.trk][s];
        int16_t v;
        if (it->id == P_NOTE && (g->flags & TF_NOTE)) { *locked = 1; return g->note; }
        if (it->id == P_TVEL && g->vel) { *locked = 1; return g->vel; }
        if (it->id == P_TLEN && g->len != 0xFF) { *locked = 1; return g->len; }
        if (it->id == P_PROB) { *locked = g->cond != 0; return cond_pos(g->cond); }
        if (lp >= 0 && lock_get(pcur, ui.trk, s, lp, &v)) { *locked = 1; return v; }
    }
    switch (it->kind) {
    case IK_SND: return tsnd()->p[it->id];
    case IK_TRK: return ttrk()->tp[it->id - NSP];
    case IK_FX: return pcur->fx[it->id - P_CDEP];
    case IK_LVL: return pcur->trk[it->id & 3].tp[P_TLVL - NSP];
    case IK_SET: return tsnd()->s[it->id == S_ARPCHD && tsnd()->s[S_SCALE] ? S_SCHD : it->id];   /* (0.12) */
    case IK_TRKF:
        switch (it->id) {
        case TF_LEN_: return ttrk()->len;
        case TF_SCALE_: return ttrk()->scale;
        case TF_TRANSP_: return ttrk()->transpose;
        case TF_VOICES_: return ttrk()->voices;
        case TF_UNI_: return ttrk()->unison;
        case TF_SPREAD_: return ttrk()->uspread;
        case TF_LAYER_: return ttrk()->layer;
        case TF_KSCALE_: return ttrk()->kscale;
        case TF_KROOT_: return ttrk()->kroot;
        case TF_KFOLD_: return ttrk()->kfold;
        case TF_KCHORD_: return ttrk()->kchord;
        case TF_QUANT_: return micro_max(ui.trk);
        }
        return 0;
    case IK_PAT:
        switch (it->id) {
        case PF_BPM_: return (int)(eng.bpm * 10.0f + 0.5f);
        case PF_SWING_: return pcur->swing;
        case PF_LMODE_: return pcur->lenmode;
        case PF_MLEN_: return pcur->mlen;
        case PF_CHLEN_: return pcur->chlen;
        case PF_STEAL_: return pcur->steal;
        case PF_REUSE_: return pcur->reuse;
        }
        return 0;
    case IK_GLB:
        switch (it->id) {
        case GL_TMODE_: return set.tempo_ptn;
        case GL_CLKIN_: return set.clock_in;
        case GL_CLKOUT_: return set.clock_out;
        case GL_QUANT_: return set.quant;
        case GL_VELS_: return set.vel_sense;
        case GL_VELF_: return set.vel_fixed;
        case GL_MET_: return set.met;
        case GL_MIDIIN_: return set.midi_in;
        case GL_TRKCH_: return set.midi_trk_ch;
        case GL_MTRK_: return set.midi_only >> ui.trk & 1u;
        case GL_SROW_: return ui.song_row + 1;
        case GL_SPAT_: return set.song_pat[ui.song_row];
        case GL_SREP_: return set.song_rep[ui.song_row] ? set.song_rep[ui.song_row] : 1;
        case GL_SMUTE_: return set.song_mute[ui.song_row];
        case GL_SON_: return set.song_on;
        case GL_SLEN_: return set.song_n ? set.song_n : 1;
        case GL_SEND_: return set.song_end;
        case GL_THEME_: return set.theme;
        default: return set.midi_out[it->id - GL_MIDI0_];
        }
    case IK_MOD: {
        int slot = it->id >> 1;
        return (it->id & 1) ? tsnd()->mdep[slot / NMSLOT][slot % NMSLOT] : tsnd()->mdst[slot / NMSLOT][slot % NMSLOT];
    }
    case IK_ARPS:
        if (it->id == AS_STEP_) return ui.arp_step + 1;
        if (it->id == AS_OFS_) return tsnd()->arp_ofs[ui.arp_step];
        return tsnd()->arp_on >> ui.arp_step & 1u;
    case IK_VIEW: return it->id == VW_LO_ ? ui.roll_lo : ui.roll_zoom;
    case IK_EUC: return euc_get(it->id);
    default: return 0;
    }
}

/* a lock on every held step that has a trig (the pattern must still fit its flash object) */
static int lock_held(int param, int val)
{
    int s, done = 0;
    for (s = 0; s < NSTEP; s++) {
        trig_t *g = &pcur->trig[ui.trk][s];
        int on = ui.steprec ? s == ui.step_cur : (int)(ui.held >> s & 1u);
        if (!on || !(g->flags & (TF_NOTE | TF_LOCK)))
            continue;
        if (param == P_NOTE) g->note = (uint8_t)val;
        else if (param == P_TVEL) g->vel = (uint8_t)val;
        else if (param == P_TLEN) g->len = (uint8_t)val;
        else if (param == P_PROB) g->cond = (uint8_t)pos_cond(val);
        else if (lock_set(pcur, ui.trk, s, param, val) < 0 || !pattern_fits()) {
            lock_clear(pcur, ui.trk, s, param);
            app_say("PATTERN", "FULL");
            return done;
        }
        done = 1;
    }
    return done;
}

static void item_set_raw(const item_t *it, int v);
/* an edit of the sound that would make the pattern too big for its flash object is refused [0.9.2] */
static sound_t snd_bak PLAT_POOL;
static void item_set(const item_t *it, int v)
{
    int snd = it->kind == IK_SND || it->kind == IK_SET || it->kind == IK_MOD || it->kind == IK_ARPS;
    if (snd)
        snd_bak = *tsnd();
    item_set_raw(it, v);
    if (snd && !pattern_fits()) {
        *tsnd() = snd_bak;
        engine_sound_changed(ui.trk);
        app_say("PATTERN", "FULL");
    }
}
static void item_set_raw(const item_t *it, int v)
{
    int lp = item_lock_param(it);
    range_t r = item_range(it);
    v = clamp(v, r.lo, r.hi);
    if (step_held_any() && lp >= 0 && (ui.held || (ui.steprec && (pcur->trig[ui.trk][ui.step_cur].flags & (TF_NOTE | TF_LOCK))))) {
        if (lock_held(lp, v)) {
            ui.held_edit = 1;
            mark_dirty();
        }
        return;
    }
    switch (it->kind) {
    case IK_SND:
        tsnd()->p[it->id] = (int16_t)v;
        engine_param_changed(ui.trk, it->id);
        if (eng.rec_live)
            engine_rec_param(ui.trk, it->id, v);
        break;
    case IK_TRK:
        ttrk()->tp[it->id - NSP] = (int16_t)v;
        if (eng.rec_live && it->id != P_NOTE)
            engine_rec_param(ui.trk, it->id, v);
        break;
    case IK_FX:
        pcur->fx[it->id - P_CDEP] = (int16_t)v;
        if (eng.rec_live)
            engine_rec_param(ui.trk, it->id, v);
        break;
    case IK_LVL: pcur->trk[it->id & 3].tp[P_TLVL - NSP] = (int16_t)v; break;
    case IK_SET:
        if (it->id == S_ENGINE) {                    /* the SYN pages go to the engine's defaults [TONIC] */
            int p;
            (void)p;
            sound_set_engine(tsnd(), v);
            engine_sound_changed(ui.trk);
            ui.row[G_SYN] = (uint8_t)(v == ENG_DR ? DRM_ENGINE_ROW : 6);   /* (the ENGINE knob stays in reach) */
        } else if (it->id == S_ARPCHD && tsnd()->s[S_SCALE])
            tsnd()->s[S_SCHD] = (uint8_t)v;          /* (0.12) with a scale: the chord by degrees */
        else
            tsnd()->s[it->id] = (uint8_t)v;
        break;
    case IK_TRKF: {
        int t, all = 0;
        if (!pcur->lenmode && (it->id == TF_LEN_ || it->id == TF_SCALE_)) {
            /* (0.12) LEN and SCALE are each track's: a pattern of one length for all goes per track, as it played
             * (M.LEN INF, no CH.LEN: it changes at T1's end, as it did) */
            pcur->lenmode = 1;
            pcur->mlen = 0;
            pcur->chlen = 0;
        }
        for (t = 0; t < NTRK; t++) {
            track_t *k = &pcur->trk[t];
            if (!all && t != ui.trk)
                continue;
            switch (it->id) {
            case TF_LEN_:                            /* longer: the new steps copy the old ones [MANUAL 10.8.1] */
                if (v > k->len) {
                    int s;
                    for (s = k->len; s < v; s++)
                        if (!(pcur->trig[t][s].flags & (TF_NOTE | TF_LOCK)))
                            pcur->trig[t][s] = pcur->trig[t][s % k->len];
                }
                k->len = (uint8_t)v;
                if (!pcur->lenmode)
                    pcur->mlen = (uint8_t)v;
                break;
            case TF_SCALE_: k->scale = (uint8_t)v; break;
            case TF_TRANSP_: k->transpose = (int8_t)v; break;
            case TF_VOICES_: k->voices = (uint8_t)v; break;
            case TF_UNI_: k->unison = (uint8_t)v; break;
            case TF_SPREAD_: k->uspread = (uint8_t)v; break;
            case TF_LAYER_: k->layer = (uint8_t)(v & ~(1 << t)); break;
            case TF_KSCALE_: k->kscale = (uint8_t)v; break;
            case TF_KROOT_: k->kroot = (uint8_t)v; break;
            case TF_KFOLD_: k->kfold = (uint8_t)v; break;
            case TF_KCHORD_: k->kchord = (uint8_t)v; break;
            case TF_QUANT_: micro_scale(t, v, micro_max(ui.trk)); break;
            }
        }
        break;
    }
    case IK_PAT:
        switch (it->id) {
        case PF_BPM_:
            eng.bpm = (float)v * 0.1f;
            pcur->bpm10 = (uint16_t)v;
            if (!set.tempo_ptn) {
                set.bpm10 = (uint16_t)v;
                set_dirty = 1;
            }
            break;
        case PF_SWING_: pcur->swing = (uint8_t)v; break;
        case PF_LMODE_: pcur->lenmode = (uint8_t)v; if (!v) pcur->mlen = pcur->trk[0].len; break;
        case PF_MLEN_: pcur->mlen = (uint8_t)v; break;
        case PF_CHLEN_: pcur->chlen = (uint8_t)v; break;
        case PF_STEAL_: pcur->steal = (uint8_t)v; break;
        case PF_REUSE_: pcur->reuse = (uint8_t)v; break;
        }
        break;
    case IK_GLB:
        switch (it->id) {
        case GL_TMODE_: set.tempo_ptn = (uint8_t)v; break;
        case GL_CLKIN_: set.clock_in = (uint8_t)v; eng.ext_clock = (uint8_t)v; break;
        case GL_CLKOUT_: set.clock_out = (uint8_t)v; break;
        case GL_QUANT_: set.quant = (uint8_t)v; eng.rec_quant = (uint8_t)v; break;
        case GL_VELS_: set.vel_sense = (uint8_t)v; break;
        case GL_VELF_: set.vel_fixed = (uint8_t)v; break;
        case GL_MET_: set.met = (uint8_t)v; eng.metronome = (uint8_t)v; break;
        case GL_MIDIIN_: set.midi_in = (uint8_t)v; break;
        case GL_TRKCH_: set.midi_trk_ch = (uint8_t)v; break;
        case GL_SROW_: ui.song_row = (uint8_t)(v - 1); if (ui.song_row >= set.song_n) set.song_n = (uint8_t)(ui.song_row + 1); break;
        case GL_SPAT_: set.song_pat[ui.song_row] = (uint8_t)v; break;
        case GL_SREP_: set.song_rep[ui.song_row] = (uint8_t)v; break;
        case GL_SMUTE_: set.song_mute[ui.song_row] = (uint8_t)v; break;
        case GL_SON_: set.song_on = (uint8_t)v; if (!set.song_n) set.song_n = 1; break;
        case GL_SLEN_: set.song_n = (uint8_t)v; break;
        case GL_SEND_: set.song_end = (uint8_t)v; break;
        case GL_THEME_: set.theme = (uint8_t)v; set_dirty = 1; ob_valid = 0; break;
        case GL_MTRK_:
            set.midi_only = (uint8_t)(v ? set.midi_only | 1u << ui.trk : set.midi_only & ~(1u << ui.trk));
            eng_midi_only = set.midi_only;
            if (v && set.midi_out[ui.trk] >= 16) {   /* a MIDI track needs a channel: its track number */
                set.midi_out[ui.trk] = ui.trk;
                eng_midi_chan[ui.trk] = ui.trk;
            }
            break;
        default:
            set.midi_out[it->id - GL_MIDI0_] = (uint8_t)v;
            eng_midi_chan[it->id - GL_MIDI0_] = (uint8_t)v;
            break;
        }
        set_dirty = 1;
        return;
    case IK_MOD: {
        int slot = it->id >> 1;
        if (it->id & 1)
            tsnd()->mdep[slot / NMSLOT][slot % NMSLOT] = (int8_t)v;
        else
            tsnd()->mdst[slot / NMSLOT][slot % NMSLOT] = (uint8_t)v;
        break;
    }
    case IK_ARPS:
        if (it->id == AS_STEP_) { ui.arp_step = (uint8_t)(v - 1); ui.redraw = 1; return; }
        if (it->id == AS_OFS_) tsnd()->arp_ofs[ui.arp_step] = (int8_t)v;
        else if (v) tsnd()->arp_on |= (uint16_t)(1u << ui.arp_step);
        else tsnd()->arp_on &= (uint16_t)~(1u << ui.arp_step);
        break;
    case IK_VIEW:
        if (it->id == VW_LO_) ui.roll_lo = (int8_t)v;
        else ui.roll_zoom = (uint8_t)v;
        ui.redraw = 1;
        return;
    case IK_EUC:
        euc_set(it->id, v);
        ui.redraw = 1;
        return;
    default: return;
    }
    mark_dirty();
}

/* a knob turned d detents (dt ms since its last detent): slow turns move one unit (a quarter step on the fine
 * parameters), fast turns a whole step or more */
static void knob(int k, int d, uint32_t dt)
{
    const row_t *row = cur_row();
    const item_t *it;
    range_t r;
    int v, lk, step = 1;
    if (!row)
        return;
    it = &row->it[k];
    if (it->kind == IK_NONE)
        return;
    ui.touch_t = plat_ms();
    ui.touch_k = (uint8_t)k;
    r = item_range(it);
    v = item_get(it, &lk);
    if (r.fmt == F_DTIME) {                          /* delay time: through its musical values and the 128ths */
        static const int16_t TRIP[] = {64, 128, 256, 512, 1024, 2048};
        int i, n = d > 0 ? d : -d;
        for (i = 0; i < n; i++) {
            int best = d > 0 ? 3072 : 24, c, j;
            for (c = 24; c <= 3072; c += 24)
                if (d > 0 ? (c > v && c < best) : (c < v && c > best))
                    best = c;
            for (j = 0; j < 6; j++)
                if (d > 0 ? (TRIP[j] > v && TRIP[j] < best) : (TRIP[j] < v && TRIP[j] > best))
                    best = TRIP[j];
            v = best;
        }
        item_set(it, v);
        ui.redraw = 1;
        return;
    }
    if (r.fmt == F_DEST) {                           /* a destination: past LFO 1's own (on LFO 1) and past those
                                                      * the engine does not have ("---") [TONIC 0.9, 0.9.1] */
        int eng = tsnd()->s[S_ENGINE], dir = d > 0 ? 1 : -1, n = d > 0 ? d : -d, nv = v;
        while (n-- > 0) {
            int c = nv + dir;
            while (c >= r.lo && c <= r.hi && !dest_ok(it, eng, c))
                c += dir;
            if (c < r.lo || c > r.hi)
                break;
            nv = c;
        }
        item_set(it, nv);
        ui.redraw = 1;
        return;
    }
    if (it->kind == IK_TRK && it->id == P_NOTE && tsnd()->s[S_ENGINE] == ENG_DR) {   /* a kit: NOTE by pads */
        item_set(it, PAD_NOTE[clamp(pad_of(v) + d, 0, NPAD - 1)]);
        ui.redraw = 1;
        return;
    }
    if (r.hi - r.lo > 40) {
        if (dt < 25) step = r.q > 1 ? r.q * 2 : 4;
        else if (dt < 60) step = r.q > 1 ? r.q : 2;
    }
    if (it->kind == IK_PAT && it->id == PF_BPM_)
        step = dt < 60 ? 10 : 1;
    if (r.fmt == F_X16) {                            /* LFO DEP: 1/16 near zero (fine vibrato), coarser further out */
        int a = v < 0 ? -v : v;
        step = a < 32 ? 1 : a < 128 ? 4 : 16;
        if (dt < 25) step *= 4;
        else if (dt < 60) step *= 2;
    }
    if (plat_buttons() >> B_SEL & 1u)                /* SCL held: the finest steps, no acceleration [TONIC] */
        step = 1;
#ifdef PLAT_HOST
    if (plat_host_exact)                             /* the simulator's "spin": exactly the detents given */
        step = 1;
#endif
    if ((it->kind == IK_SND || it->kind == IK_TRK) && !ui.held && page_held())
        control_all(it, clamp(v + d * step, r.lo, r.hi));
    else
        item_set(it, v + d * step);
    ui.redraw = 1;
}

/* --------------------------------------------------------------------- keyboard --- */
/* the keyboard's note of key k: chromatic from F3, the octave, the track's keyboard scale (FOLD: the keys walk the
 * scale's notes) */
static int key_note(int k)
{
    const track_t *t = ttrk();
    int base = 53 + 12 * ui.oct, note;
    if (tsnd()->s[S_ENGINE] == ENG_DR) {             /* a kit: the white keys are its pads, 1-8 and again; a black
                                                      * key the white one before it [TONIC] */
        int w = key_white[k] >= 0 ? key_white[k] : k > 0 ? key_white[k - 1] : 0;
        return PAD_NOTE[(w < 0 ? 0 : w) % NPAD];
    }
    if (t->kscale && t->kfold) {
        uint16_t m = KSCALE_MASK[t->kscale];
        int deg = 0, n = t->kroot + 48 + 12 * ui.oct, i;
        while (deg < k) {
            n++;
            if (m >> ((n - t->kroot) % 12) & 1u)
                deg++;
        }
        (void)i;
        note = n;
    } else {
        note = base + k;
        if (t->kscale) {                             /* not in the scale: the scale note below */
            uint16_t m = KSCALE_MASK[t->kscale];
            while (!(m >> (((note - t->kroot) % 12 + 12) % 12) & 1u))
                note--;
        }
    }
    return clamp(note, 0, 127);
}

static int key_vel(int k)
{
    if (!set.vel_sense)
        return set.vel_fixed;
    return clamp((int)touch_vel((uint32_t)k), 1, 127);
}

static void play_note_on(int k)
{
    int note = key_note(k), vel = key_vel(k), c;
    static const int8_t CH[9][3] = {{0, 0, 0}, {0, 0, 0}, {4, 7, 0}, {3, 7, 0}, {4, 7, 10}, {3, 7, 10}, {4, 7, 11},
                                    {3, 6, 0}, {3, 6, 9}};
    ui.key_note[k] = (uint8_t)note;
    ui.key_trk[k] = ui.trk;
    ui.key_chord[k] = drum_trk() ? 0 : ttrk()->kchord;
    engine_note_on(ui.trk, note, vel);
    if (tsnd()->s[S_ENGINE] == ENG_DR) {             /* a kit: the pad played is the one the grid places, its row
                                                      * the one the knobs edit [TONIC] */
        ttrk()->tp[P_NOTE - NSP] = (int16_t)note;
        if (ui.grp == G_SYN && ui.row[G_SYN] < NPAD)
            ui.row[G_SYN] = (uint8_t)pad_of(note);
        ui.redraw = 1;
    } else if (ttrk()->kchord >= 2)                  /* CHORD [MANUAL 10.4]: a three or four note chord */
        for (c = 0; c < 3; c++)
            if (CH[ttrk()->kchord][c])
                engine_note_on(ui.trk, note + CH[ttrk()->kchord][c], vel);
    if (ui.steprec) {                                /* step recording: the chord being entered (KCHRD's notes too) */
        int n2[NNOTE], m = 0;
        n2[m++] = note;
        if (ui.key_chord[k] >= 2)
            for (c = 0; c < 3 && m < NNOTE; c++)
                if (CH[ui.key_chord[k]][c])
                    n2[m++] = note + CH[ui.key_chord[k]][c];
        for (c = 0; c < m && ui.sr_n < NNOTE; c++) {
            ui.sr_notes[ui.sr_n++] = (uint8_t)clamp(n2[c], 0, 127);
            if (ui.sr_n == 1)
                ui.sr_vel = (uint8_t)vel;
        }
        ui.sr_down++;
    }
}
static void play_note_off(int k)
{
    int c;
    static const int8_t CH[9][3] = {{0, 0, 0}, {0, 0, 0}, {4, 7, 0}, {3, 7, 0}, {4, 7, 10}, {3, 7, 10}, {4, 7, 11},
                                    {3, 6, 0}, {3, 6, 9}};
    engine_note_off(ui.key_trk[k], ui.key_note[k]);
    if (ui.key_chord[k] >= 2)                        /* the chord it played, not the one set now [0.9.2] */
        for (c = 0; c < 3; c++)
            if (CH[ui.key_chord[k]][c])
                engine_note_off(ui.key_trk[k], ui.key_note[k] + CH[ui.key_chord[k]][c]);
    if (ui.steprec && ui.sr_down && !--ui.sr_down && ui.sr_n) {   /* all keys up: the chord goes in */
        trig_t *g = &pcur->trig[ui.trk][ui.step_cur];
        int i, keep = g->flags & (TF_NOTE | TF_LOCK);
        g->flags = (uint8_t)((g->flags & (TF_PLOCK | TF_SLOCK)) | TF_NOTE);
        g->note = ui.sr_notes[0];
        g->nnote = ui.sr_n;
        for (i = 1; i < ui.sr_n; i++)
            g->chord[i - 1] = (int8_t)(ui.sr_notes[i] - ui.sr_notes[0]);
        g->vel = ui.sr_vel;
        if (!keep) {
            g->micro = 0;
            g->cond = 0;
            g->len = 0xFF;
        }
        if (!pattern_fits()) {
            g->flags = 0;
            app_say("PATTERN", "FULL");
        }
        ui.sr_n = 0;
        ui.step_cur = (uint8_t)((ui.step_cur + 1) % ttrk()->len);
        ui.spage = (uint8_t)(ui.step_cur / 16);
        mark_dirty();
    }
}

/* ------------------------------------------------------------------------- grid --- */
static int page_count(void) { return (ttrk()->len + 15) / 16; }

static void grid_white(int w, int down)
{
    int s = cur_step_base() + w;
    trig_t *g = &pcur->trig[ui.trk][s];
    uint32_t now = plat_ms();
    if (s >= NSTEP)
        return;
    if (down) {
        if ((plat_buttons() >> B_SEL & 1u) || (ui.keys >> BK[4] & 1u)) {   /* SEL / LOCK + key: a lock trig */
            if (!(g->flags & (TF_NOTE | TF_LOCK))) {
                g->flags = (uint8_t)((g->flags & (TF_PLOCK | TF_SLOCK)) | TF_LOCK);
                g->micro = 0;
                g->cond = 0;
                ui.held_new |= 1ull << s;
                mark_dirty();
            }
        } else if (tsnd()->s[S_ENGINE] == ENG_DR && (g->flags & TF_NOTE) && trig_pad(g, cur_pad()) < 0) {
            if (g->nnote < NNOTE) {                  /* a kit: another pad joins the step [TONIC] */
                int n = g->nnote ? g->nnote : 1;
                g->chord[n - 1] = (int8_t)(PAD_NOTE[cur_pad()] - g->note);
                g->nnote = (uint8_t)(n + 1);
                if (!pattern_fits()) {
                    g->nnote = (uint8_t)n;
                    app_say("PATTERN", "FULL");
                }
                mark_dirty();
            } else
                app_say("STEP", "4 PADS FULL");
            ui.held_new |= 1ull << s;
        } else if (!(g->flags & (TF_NOTE | TF_LOCK))) {   /* a note trig, at the track's NOTE */
            g->flags = (uint8_t)((g->flags & (TF_PLOCK | TF_SLOCK)) | TF_NOTE);
            g->note = (uint8_t)ttrk()->tp[P_NOTE - NSP];
            g->nnote = 1;
            g->vel = 0;
            g->len = 0xFF;
            g->micro = 0;
            g->cond = 0;
            if (!pattern_fits())
                g->flags = 0;
            ui.held_new |= 1ull << s;
            mark_dirty();
        }
        ui.held |= 1ull << s;
        ui.held_t[s] = now;
        ui.held_edit = 0;
    } else {
        /* released: a trig that was there, touched briefly and not edited, is removed [MANUAL 10.2.2] */
        if ((ui.held >> s & 1u) && !(ui.held_new >> s & 1u) && !ui.held_edit && now - ui.held_t[s] < 300u) {
            int pi = tsnd()->s[S_ENGINE] == ENG_DR && g->nnote > 1 ? trig_pad(g, cur_pad()) : -1;
            if (pi >= 0) {                           /* a kit: only the pad leaves the step [TONIC] (its level lock
                                                      * with it [0.9.2]) */
                int notes[NNOTE], n = 0, i;
                lock_clear(pcur, ui.trk, s, DRP[cur_pad()][DK_LEV]);
                for (i = 0; i < g->nnote; i++)
                    if (i != pi)
                        notes[n++] = g->note + (i ? g->chord[i - 1] : 0);
                g->note = (uint8_t)notes[0];
                for (i = 1; i < n; i++)
                    g->chord[i - 1] = (int8_t)(notes[i] - notes[0]);
                g->nnote = (uint8_t)n;
            } else {
                g->flags = 0;
                lock_clear(pcur, ui.trk, s, P_NONE);
            }
            mark_dirty();
        }
        ui.held &= ~(1ull << s);
        ui.held_new &= ~(1ull << s);
    }
    ui.redraw = 1;
}

/* one step's trig and locks kept, to put back an edit that would not fit the pattern's flash object [0.9.2] */
static struct { trig_t g; uint8_t n; plock_t l[128]; } step_bak;
static void step_save(int t, int s)
{
    int i;
    uint8_t key = LOCK_KEY(t, s);
    step_bak.g = pcur->trig[t][s];
    step_bak.n = 0;
    for (i = 0; i < pcur->nlock && step_bak.n < 128; i++)
        if (pcur->lock[i].f.param != P_NONE && pcur->lock[i].f.step == key)
            step_bak.l[step_bak.n++] = pcur->lock[i];
}
static void step_restore(int t, int s)
{
    int i;
    lock_clear(pcur, t, s, P_NONE);
    pcur->trig[t][s] = step_bak.g;
    for (i = 0; i < step_bak.n; i++)
        lock_set(pcur, t, s, step_bak.l[i].f.param, step_bak.l[i].f.val);
    trig_flags_update(pcur, t, s);
}

static void clip_copy(int from, int n)
{
    int s, i;
    ui.clip_nlock = 0;
    for (s = 0; s < n; s++) {
        ui.clip[s] = pcur->trig[ui.trk][from + s];
        for (i = 0; i < pcur->nlock && ui.clip_nlock < NLOCK; i++) {
            plock_t l = pcur->lock[i];
            if (l.f.param != P_NONE && l.f.step == LOCK_KEY(ui.trk, from + s)) {
                l.f.step = (uint8_t)s;
                ui.clip_lock[ui.clip_nlock++] = l;
            }
        }
    }
    ui.clip_n = (uint8_t)n;
    ui.clip_ok = 1;
}
static int clip_paste(int to)                  /* (0: pasted; -1: nothing, or stopped on a full pattern) */
{
    int s, i;
    if (!ui.clip_ok)
        return -1;
    for (s = 0; s < ui.clip_n && to + s < NSTEP; s++) {   /* a step at a time: one that would not fit stays as it
                                                           * was, and the paste stops there [0.9.2] */
        int ok = 1;
        step_save(ui.trk, to + s);
        lock_clear(pcur, ui.trk, to + s, P_NONE);
        pcur->trig[ui.trk][to + s] = ui.clip[s];
        pcur->trig[ui.trk][to + s].flags &= (uint8_t)~(TF_PLOCK | TF_SLOCK);
        for (i = 0; i < ui.clip_nlock && ok; i++)
            if (ui.clip_lock[i].f.step == s)
                ok = lock_set(pcur, ui.trk, to + s, ui.clip_lock[i].f.param, ui.clip_lock[i].f.val) == 0;
        if (!ok || !pattern_fits()) {
            step_restore(ui.trk, to + s);
            app_say("PATTERN", "FULL");
            mark_dirty();
            return -1;
        }
    }
    mark_dirty();
    return 0;
}

/* the active track's trigs and their locks rotated by d steps within its length [TONIC] */
static void track_shift(int d)
{
    trig_t *tmp = (trig_t *)(void *)pack_buf_w;     /* (the store's buffer as scratch: 640 B of its 3904) */
    int s, i, L = ttrk()->len ? ttrk()->len : 16, t = ui.trk;
    for (s = 0; s < L; s++)
        tmp[(s + d + L) % L] = pcur->trig[t][s];
    for (s = 0; s < L; s++)
        pcur->trig[t][s] = tmp[s];
    for (i = 0; i < pcur->nlock; i++) {              /* the locks move with their trigs */
        plock_t l = pcur->lock[i];
        if (l.f.param != P_NONE && (l.f.step >> 6) == t && (l.f.step & 63) < L) {
            l.f.step = (uint8_t)LOCK_KEY(t, ((l.f.step & 63) + d + L) % L);
            pcur->lock[i].w = l.w;
        }
    }
    mark_dirty();
    ui.redraw = 1;
}

static void grid_black(int b)
{
    int s, n = page_count();
    if (b < 4) {                                     /* step pages */
        if (b < n) {
            ui.spage = (uint8_t)b;
            ui.redraw = 1;
        }
        return;
    }
    if ((b == 5 || b == 6) && !ui.held) {             /* no step held: the track's trigs one step left / right */
        track_shift(b == 5 ? -1 : 1);
        app_say("SHIFT", b == 5 ? "< ONE STEP" : "ONE STEP >");
        return;
    }
    if (b == 5 || b == 6) {                          /* micro timing of the held steps */
        int moved = 0;
        for (s = 0; s < NSTEP; s++)
            if (ui.held >> s & 1u) {
                trig_t *g = &pcur->trig[ui.trk][s];
                g->micro = (int8_t)clamp(g->micro + (b == 5 ? -1 : 1), -23, 23);
                moved = 1;
                ui.held_edit = 1;
            }
        if (moved) {
            char t[12];
            int m = pcur->trig[ui.trk][first_held()].micro;
            tx_num(t, m);
            tx_cat(t, "/384");
            app_say("MICRO", t);
            mark_dirty();
        }
        return;
    }
    if (b >= 8 && (plat_buttons() >> B_SEL & 1u)) {  /* SEL + COPY / PASTE / CLEAR: the whole track */
        if (b == 8) {
            clip_copy(0, ttrk()->len);
            app_say("COPY", "TRACK");
        } else if (b == 9) {
            if (clip_paste(0) == 0)                  /* (else its own message stays) */
                app_say("PASTE", "TRACK");
        } else {
            for (s = 0; s < NSTEP; s++) {
                lock_clear(pcur, ui.trk, s, P_NONE);
                pcur->trig[ui.trk][s].flags = 0;
            }
            lock_compact_safe();
            mark_dirty();
            app_say("CLEAR", "TRACK");
        }
        return;
    }
    if (b == 8) {                                    /* COPY: the held steps or the page */
        int lo = -1, hi = -1;
        for (s = 0; s < NSTEP; s++)
            if (ui.held >> s & 1u) {
                if (lo < 0) lo = s;
                hi = s;
            }
        if (lo >= 0) {
            clip_copy(lo, hi - lo + 1);
            ui.held_edit = 1;
            app_say("COPY", "TRIGS");
        } else {
            clip_copy(cur_step_base(), 16);
            app_say("COPY", "PAGE");
        }
        return;
    }
    if (b == 9) {                                    /* PASTE: at the first held step, or the page */
        int at = first_held();
        ui.held_edit = 1;
        if (clip_paste(at >= 0 && !ui.steprec ? at : cur_step_base()) == 0)
            app_say("PASTE", "");
        return;
    }
    if (b == 10) {                                   /* CLEAR: the held steps' locks, or the page */
        if (ui.held) {
            for (s = 0; s < NSTEP; s++)
                if (ui.held >> s & 1u) {
                    lock_clear(pcur, ui.trk, s, P_NONE);
                    pcur->trig[ui.trk][s].cond = 0;
                    pcur->trig[ui.trk][s].micro = 0;
                    pcur->trig[ui.trk][s].vel = 0;
                    pcur->trig[ui.trk][s].len = 0xFF;
                }
            ui.held_edit = 1;
            app_say("CLEAR", "LOCKS");
        } else {
            for (s = cur_step_base(); s < cur_step_base() + 16; s++) {
                lock_clear(pcur, ui.trk, s, P_NONE);
                pcur->trig[ui.trk][s].flags = 0;
            }
            app_say("CLEAR", "PAGE");
        }
        lock_compact_safe();
        mark_dirty();
    }
}

/* the lock pool's free entries go when nothing plays (the render reads the pool) */
static void lock_compact_safe(void)
{
    if (!eng.playing)
        lock_compact(pcur);
}

/* live recording: what the engine heard, written to the pattern [MANUAL 10.2.5] */
static void rec_poll(void)
{
    rec_ev_t e;
    while (engine_rec_get(&e)) {
        trig_t *g;
        uint32_t lp;
        if (e.trk >= NTRK || e.step >= NSTEP)
            continue;
        lp = e.pass;                                 /* the track's own pass, when it was played [0.9.2] */
        if (lp != ui.rec_loop[e.trk]) {
            ui.rec_loop[e.trk] = lp;
            ui.rec_pass[e.trk] = 0;
        }
        g = &pcur->trig[e.trk][e.step];
        if (e.kind == 0) {
            if ((ui.rec_pass[e.trk] >> e.step & 1u) && (g->flags & TF_NOTE) && g->nnote < NNOTE) {
                g->chord[g->nnote - 1] = (int8_t)(e.note - g->note);   /* the same step this pass: a chord */
                g->nnote++;
            } else {
                g->flags = (uint8_t)((g->flags & (TF_PLOCK | TF_SLOCK)) | TF_NOTE);
                g->note = e.note;
                g->nnote = 1;
                g->vel = e.vel;
                g->micro = e.micro;
                g->len = e.len;
                g->cond = 0;
            }
            ui.rec_pass[e.trk] |= 1ull << e.step;
        } else {
            if (!(g->flags & (TF_NOTE | TF_LOCK)))
                g->flags = (uint8_t)((g->flags & (TF_PLOCK | TF_SLOCK)) | TF_LOCK);
            if (lock_set(pcur, e.trk, e.step, e.note, e.val) < 0 || !pattern_fits())
                lock_clear(pcur, e.trk, e.step, e.note);
        }
        if (!pattern_fits()) {
            g->flags = 0;
            app_say("PATTERN", "FULL");
        }
        mark_dirty();
        ui.redraw = 1;
    }
}

/* ----------------------------------------------------------------- the buttons --- */
static void group_button(int g, int back)
{
    ui.touch_t = 0;                                  /* the value pop-up belongs to the page it was turned on */
    if (ui.grp == g && grp_n(g)) {
        int n = grp_n(g);
        ui.row[g] = (uint8_t)((ui.row[g] + (back ? n - 1 : 1)) % n);
    } else
        ui.grp = (uint8_t)g;
    ui.confirm = 0;
    ui.redraw = 1;
}

/* the page button of the active group, while it is held (CONTROL ALL, randomize) */
static const uint8_t PG_BTN[NG] = {B_SEQ, B_EDIT, B_ENV, B_LFO, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static int page_held(void)
{
    uint8_t b = PG_BTN[ui.grp];
    return b != 0xFF && ui.pg_btn == b && (plat_buttons() >> b & 1u);
}
static void page_snap(void)
{
    int t;
    if (ui.pg_snap_ok)
        return;
    for (t = 0; t < NTRK; t++) {
        memcpy(ui.pg_snap[t], pcur->snd[t].p, sizeof ui.pg_snap[t]);
        memcpy(ui.pg_tsnap[t], pcur->trk[t].tp, sizeof ui.pg_tsnap[t]);
    }
    ui.pg_snap_ok = 1;
}
static void page_revert(void)
{
    int t, p;
    for (t = 0; t < NTRK; t++) {
        memcpy(pcur->snd[t].p, ui.pg_snap[t], sizeof ui.pg_snap[t]);
        memcpy(pcur->trk[t].tp, ui.pg_tsnap[t], sizeof ui.pg_tsnap[t]);
        for (p = 0; p < NSP; p++)
            engine_param_changed(t, p);
    }
    ui.pg_snap_ok = 0;
    mark_dirty();
    app_say("REVERTED", "");
}
/* [MANUAL 6.2.2] CONTROL ALL: the same value on every track */
static void control_all(const item_t *it, int v)
{
    uint8_t t0 = ui.trk;
    int e0 = pcur->snd[t0].s[S_ENGINE];
    /* (a parameter whose meaning is the engine's: only on the tracks of the same engine) [0.9.2] */
    int own = it->kind == IK_MOD || (it->kind == IK_SND && (it->id <= P_BRST || it->id >= P_D5TUN ||
                                                            it->id == P_L1DST || it->id == P_L2DST));
    page_snap();
    for (ui.trk = 0; ui.trk < NTRK; ui.trk++)
        if (!own || pcur->snd[ui.trk].s[S_ENGINE] == e0)
            item_set(it, v);
    ui.trk = t0;
    ui.pg_used = 1;
}
/* [MANUAL 11.1] page randomize: the sound parameters of the active row, a new draw each time */
static void page_randomize(void)
{
    const row_t *row = cur_row();
    int k;
    if (!row)
        return;
    page_snap();
    for (k = 0; k < 4; k++) {
        const item_t *it = &row->it[k];
        range_t r;
        if (it->kind != IK_SND)
            continue;
        r = item_range(it);
        ui.rnd = ui.rnd * 1664525u + 1013904223u + plat_ms();
        {
            int v = r.lo + (int)((ui.rnd >> 8) % (uint32_t)(r.hi - r.lo + 1)), n = 0;
            while (r.fmt == F_DEST && !dest_ok(it, tsnd()->s[S_ENGINE], v) && n++ < 64) {   /* (a live one) [0.9.2] */
                ui.rnd = ui.rnd * 1664525u + 1013904223u;
                v = r.lo + (int)((ui.rnd >> 8) % (uint32_t)(r.hi - r.lo + 1));
            }
            if (r.q > 1)
                v -= (v - r.lo) % r.q;
            item_set(it, v);
        }
    }
    ui.pg_used = 1;
    app_say("RANDOMIZED", "SCL: REVERT");
}

/* page button + OCT-: the active row's sound and trig parameters back to their defaults (SCL reverts) [TONIC] */
static void page_default(void)
{
    const row_t *row = cur_row();
    int k;
    if (!row)
        return;
    page_snap();
    for (k = 0; k < 4; k++)
        if (row->it[k].kind == IK_SND || row->it[k].kind == IK_TRK)
            item_set(&row->it[k], row->it[k].kind == IK_SND ? param_desc(tsnd()->s[S_ENGINE], row->it[k].id)->def
                                                            : PDESC[row->it[k].id].def);
    ui.pg_used = 1;
    app_say("DEFAULTS", "SCL: REVERT");
}

static void transport_play(int sel)
{
    uint32_t b = plat_buttons();
    if (b >> B_REC & 1u) {                           /* REC + PLAY: live recording; PLAY again: quantize */
        if (eng.rec_live && plat_ms() - ui.rec_play_t < 1500u) {
            set.quant ^= 1u;
            eng.rec_quant = set.quant;
            set_dirty = 1;
            app_say("QUANTIZE", set.quant ? "ON" : "OFF");
        } else {
            eng.rec_live = 1;
            ui.grid = 0;
            ui.steprec = 0;
            if (!eng.playing)
                engine_play();
            app_say("LIVE REC", set.quant ? "QUANTIZED" : "");
        }
        ui.rec_play_t = plat_ms();
        return;
    }
    if (sel) {                                       /* SEL + PLAY: stop, the effects cut */
        engine_stop_hard();
        app_say("STOP", "");
        return;
    }
    if (eng.rec_live) {                              /* PLAY in live recording: recording ends, playing goes on */
        eng.rec_live = 0;
        app_say("LIVE REC", "OFF");
        return;
    }
    if (eng.playing)
        engine_stop();
    else {
        song_start();                                /* SONG mode: from its first row */
        engine_play();
    }
}

static void save_action(void);
static void perf_key(int k, int down);
static void lock_compact_safe(void);
static int page_held(void);
static void control_all(const item_t *it, int v);

static void buttons(uint32_t b)
{
    uint32_t press = b & ~ui.btn, rel = ~b & ui.btn, now = plat_ms();
    int sel = (b >> B_SEL) & 1u, i;
    if (ui.plist_on && (press & ~(1u << B_SEL | 1u << B_OCTDN | 1u << B_OCTUP))) {
        ui.plist_on = 0;                             /* another button: the preset list closes */
        ui.redraw = 1;
    }
    for (i = 0; i < NB; i++)
        if (press >> i & 1u)
            ui.btn_t[i] = now;
    if (press) {                                     /* the help: a button pressed alone, nothing else since */
        if ((b & ~press) == 0 && !ui.keys && !(press & (press - 1u))) {
            for (i = 0; i < NB; i++)
                if (press >> i & 1u)
                    ui.help_btn = (uint8_t)i;
            ui.help_block = 0;
        } else
            ui.help_block = 1;
    }
    if (ui.help_btn != 0xFF && ui.help_btn < NB && !(b >> ui.help_btn & 1u)) {
        if (!ui.help_block && now - ui.btn_t[ui.help_btn] > 900u)
            ui.redraw = 1;                           /* the help goes */
        ui.help_btn = 0xFF;
    }
    if (page_held() && (press & (1u << B_REC | 1u << B_SEL | 1u << B_OCTDN))) {
        if (press >> B_REC & 1u)
            page_randomize();
        else if (press >> B_OCTDN & 1u)
            page_default();
        else if (ui.pg_snap_ok)
            page_revert(), ui.pg_used = 1;
        ui.btn = b;
        ui.redraw = 1;
        return;
    }
    if (press >> B_PLAY & 1u)
        transport_play(sel);
    if (press >> B_REC & 1u) {
        if (sel) {                                   /* step recording */
            ui.steprec ^= 1u;
            ui.grid = 0;
            ui.sr_n = ui.sr_down = 0;
            app_say("STEP REC", ui.steprec ? "ON" : "OFF");
        } else if (eng.rec_live) {                   /* from live to grid recording */
            eng.rec_live = 0;
            ui.grid = 1;
        } else if (!(b >> B_PLAY & 1u)) {
            ui.grid ^= 1u;
            ui.steprec = 0;
            ui.held = 0;
        }
        ui.redraw = 1;
    }
    {                                                /* SEQ EDIT ENV LFO: the next row on release, unless the
                                                      * held button was used for CONTROL ALL or randomize */
        static const uint8_t PB[4] = {B_SEQ, B_EDIT, B_ENV, B_LFO}, PG[4] = {G_TRIG, G_SYN, G_ENV, G_LFO};
        int j;
        for (j = 0; j < 4; j++) {
            if (press >> PB[j] & 1u) {
                ui.pg_btn = PB[j];
                ui.pg_back = (uint8_t)sel;
                ui.pg_used = 0;
                ui.pg_snap_ok = 0;
                if (ui.grp != PG[j])
                    group_button(PG[j], sel), ui.pg_used = 1;
            }
            if ((rel >> PB[j] & 1u) && ui.pg_btn == PB[j]) {
                if (!ui.pg_used && ui.grp == PG[j])
                    group_button(PG[j], ui.pg_back);
                ui.pg_btn = 0xFF;
            }
        }
    }
    if (press >> B_FX & 1u) group_button(G_FX, sel);
    if (press >> B_ARP & 1u) group_button(G_ARP, sel);
    if (press >> B_GLO & 1u) group_button(G_GLO, sel);
    if (rel >> B_HOME & 1u) {                        /* HOME: a tap the home screen; held, the performance layer */
        if (!ui.perf_chain_n && now - ui.btn_t[B_HOME] < 350u) {
            if (sel) {                               /* SCL + HOME: undo (again: redo) [TONIC] */
                int u2 = undo_do();
                app_say(u2 > 0 ? "UNDO" : u2 < 0 ? "UNDO" : "NOTHING", u2 > 0 ? "SCL+HOME AGAIN: REDO" : u2 < 0 ? "AFTER THE CHANGE" : "TO UNDO");
            } else if (ui.perf_lock) {               /* locked: a tap leaves the performance layer [TONIC] */
                ui.perf_lock = 0;
                ui.home_tap = 0;
                app_say("PERFORM", "UNLOCKED");
            } else if (ui.home_tap && now - ui.home_tap < 450u) {   /* a second tap: lock it [TONIC] */
                ui.perf_lock = 1;
                ui.home_tap = 0;
                app_say("PERFORM LOCKED", "HOME: LEAVE");
            } else {
                group_button(G_HOME, 0);
                ui.home_tap = now;
            }
        } else
            ui.home_tap = 0;
        if (ui.perf_chain_n > 1) {                   /* several patterns pressed in a row: a chain */
            int k;
            set.chain_n = ui.perf_chain_n;
            for (k = 0; k < ui.perf_chain_n; k++)
                set.chain[k] = ui.perf_chain[k];
            set_dirty = 1;
            app_say("CHAIN", "");
        }
        ui.perf = ui.perf_lock;
        ui.perf_chain_n = 0;
        if (eng.fill == 1 && !ui.perf_lock)
            eng.fill = 0;
        ui.redraw = 1;
    }
    if (ui.perf_lock)
        ui.perf = 1;
    if ((b >> B_HOME & 1u) && now - ui.btn_t[B_HOME] > 250u && !ui.perf) {
        ui.perf = 1;
        ui.redraw = 1;
    }
    if (press >> B_SAVE & 1u) {
        if (sel) {                                   /* SEL + SAVE: save everything now */
            if (eng.playing)
                app_say("STOP", "TO SAVE");
            else
                app_say(store_flush() == 0 ? "SAVED" : "SAVE FAILED", "");
        } else if (ui.grp == G_SAVE)
            save_action();
        else
            group_button(G_SAVE, 0);
    }
    if (press & (1u << B_OCTDN | 1u << B_OCTUP)) {
        int d = (press >> B_OCTUP & 1u) ? 1 : -1, s = first_held();
        if (ui.plist_on) {                           /* the preset list: OCT+ loads, OCT- closes [TONIC] */
            ui.plist_on = 0;
            if (d > 0) {
                int i = ui.plist_i;
                ui.sound_i = (uint8_t)i;
                snd_bak = *tsnd();
                if (i < NFACTORY)
                    preset_apply(tsnd(), &FACTORY[i]);
                else
                    *tsnd() = lib[i - NFACTORY];
                if (!pattern_fits()) {               /* (too big for the pattern: the sound as it was) [0.9.2] */
                    *tsnd() = snd_bak;
                    app_say("PATTERN", "FULL");
                } else
                    app_say("LOADED", tsnd()->name);
                engine_sound_changed(ui.trk);
                mark_dirty();
            } else
                app_say("PRESETS", "CANCELLED");
            ui.redraw = 1;
        } else if (ui.grp == G_SAVE && ui.confirm) {
            ui.confirm = 0;
            app_say("CANCELLED", "");
        } else if (ui.perf) {                        /* HOME held: transpose the pattern [MANUAL 10.9.9] */
            int t;
            char tx[8];
            for (t = 0; t < NTRK; t++)
                pcur->trk[t].transpose = (int8_t)clamp(pcur->trk[t].transpose + d, -36, 36);
            tx_num(tx, ttrk()->transpose);
            app_say("PTN TRANSPOSE", tx);
            mark_dirty();
        } else if (ui.held && s >= 0) {              /* a held step: its notes, semitones (SEL: octaves) */
            int t;
            for (t = 0; t < NSTEP; t++)
                if (ui.held >> t & 1u) {
                    trig_t *g = &pcur->trig[ui.trk][t];
                    if (g->flags & TF_NOTE)
                        g->note = (uint8_t)clamp(g->note + d * (sel ? 12 : 1), 0, 127);
                }
            ui.held_edit = 1;
            mark_dirty();
        } else if (drum_trk() && (sel || !ui.grid)) {   /* a kit: SEL + OCT (and OCT alone, playing) pick the pad;
                                                      * a kit has no octave or transpose [TONIC 0.6.1] */
            pad_pick((cur_pad() + d + NPAD) % NPAD);
        } else if (sel) {                            /* SEL: transpose the track */
            ttrk()->transpose = (int8_t)clamp(ttrk()->transpose + d, -36, 36);
            {
                char t[8];
                tx_num(t, ttrk()->transpose);
                app_say("TRANSPOSE", t);
            }
            mark_dirty();
        } else if (ui.grid) {                        /* the step page */
            ui.spage = (uint8_t)clamp(ui.spage + d, 0, page_count() - 1);
        } else {
            int i2;
            for (i2 = 0; i2 < NKEYS; i2++)           /* the held keys end before the octave moves */
                if (ui.keys >> i2 & 1u)
                    play_note_off(i2);
            ui.keys = 0;
            ui.oct = (int8_t)clamp(ui.oct + d, -3, 3);
            {
                char t[8];
                t[0] = ui.oct < 0 ? '-' : '+';
                t[1] = (char)('0' + (ui.oct < 0 ? -ui.oct : ui.oct));
                t[2] = 0;
                app_say("OCTAVE", t);
            }
        }
        ui.redraw = 1;
    }
    if (rel >> B_SEL & 1u || press >> B_SEL & 1u)
        ui.redraw = 1;
    ui.btn = b;
}

/* -------------------------------------------------------------------- the keys --- */
/* tap tempo: the mean of the last taps' spacing (a gap over 2 s starts again) */
static void tap_tempo(void)
{
    uint32_t now = plat_ms();
    if (ui.ntap && now - ui.taps[(ui.ntap - 1) & 3] > 2000u)
        ui.ntap = 0;
    ui.taps[ui.ntap & 3] = now;
    ui.ntap++;
    if (ui.ntap >= 2) {
        int n = ui.ntap > 4 ? 4 : ui.ntap;
        uint32_t span = ui.taps[(ui.ntap - 1) & 3] - ui.taps[(ui.ntap - n) & 3];
        float bpm = 60000.0f * (float)(n - 1) / (float)(span ? span : 1);
        item_t it = PF(PF_BPM_);
        char t[8];
        item_set(&it, (int)(bpm * 10.0f + 0.5f));
        tx_fix(t, (int)(eng.bpm * 10.0f + 0.5f), 10, 1);
        app_say("TAP TEMPO", t);
    } else
        app_say("TAP TEMPO", "TAP AGAIN");
}

/* the momentary effects on the performance layer [TONIC 0.7]: SCL + white key 1-9 while held (the key's release ends
 * it, SCL may go first) */
static const char *const PFX_NAME[NPFX] = {"ROLL 1/4", "ROLL 1/8", "ROLL 1/16", "ROLL 1/32", "HALFTIME", "TAPE STOP",
                                           "LP SWEEP", "HP SWEEP", "FREEZE"};
static void pfx_key(int w, int down)
{
    if (down) {
        ui.pfx_keys |= (uint16_t)(1u << w);
        eng.pfx |= (uint16_t)(1u << w);
        app_say("EFFECT", PFX_NAME[w]);
    } else {
        ui.pfx_keys &= (uint16_t)~(1u << w);
        eng.pfx &= (uint16_t)~(1u << w);
    }
    ui.redraw = 1;
}
static void perf_key(int k, int down)
{
    int w = key_white[k], bl = key_black[k];
    if (w >= 0 && w < NPFX && down && (plat_buttons() >> B_SEL & 1u)) {
        pfx_key(w, 1);
        return;
    }
    if (w >= 0 && down) {                            /* a pattern of the bank */
        int bank = cur_idx / 16, idx = bank * 16 + w;
        if (!(plat_buttons() >> B_HOME & 1u))        /* locked, HOME up: no chain, each key a pattern */
            ui.perf_chain_n = 0;
        if (ui.perf_chain_n < 16)
            ui.perf_chain[ui.perf_chain_n++] = (uint8_t)idx;
        if (ui.perf_chain_n == 1) {
            set.chain_n = 0;
            pattern_select(idx);
        }
        if (!(plat_buttons() >> B_HOME & 1u))        /* (locked, HOME up: not a chain in the making) */
            ui.perf_chain_n = 0;
    } else if (bl >= 0) {
        if (bl < 4 && down) {                        /* mutes */
            eng.mute ^= (uint8_t)(1u << bl);
            set.mute = eng.mute;
            set_dirty = 1;
        } else if (bl == 4)
            eng.fill = down ? 1 : 0;                 /* FILL while held */
        else if (bl == 5 && down) {                  /* FILL for one pass, from the pattern's next start */
            eng.fill_cue = 1;
            app_say("FILL", "NEXT PASS");
        }
        else if (bl == 6 && down)                    /* bank A / B */
            pattern_select((cur_idx + 16) % NPAT);
        else if (bl == 7 && down)                    /* tap tempo [TONIC] */
            tap_tempo();
        else if (bl == 8 && down) {
            pattern_temp_save();
            app_say("TEMP SAVE", "");
        } else if (bl == 9 && down) {
            if (pattern_temp_reload() == 0)
                app_say("RELOAD", "");
        } else if (bl == 10 && down)
            engine_restart();
    }
    ui.redraw = 1;
}

enum { KH_NONE, KH_NOTE, KH_PERF, KH_GRID, KH_SEL };
static void keys(uint32_t k)
{
    uint32_t press = k & ~ui.keys, rel = ~k & ui.keys;
    int i, sel = (plat_buttons() >> B_SEL) & 1u;
    if (press)
        ui.help_block = 1;
    for (i = 0; i < NKEYS; i++) {
        uint32_t bit = 1u << i;
        int w = key_white[i], bl = key_black[i];
        if (!((press | rel) & bit))
            continue;
        if ((rel & bit) && w >= 0 && w < NPFX && (ui.pfx_keys >> w & 1u)) {   /* a momentary effect ends, wherever */
            pfx_key(w, 0);
            ui.key_how[i] = KH_NONE;
            continue;
        }
        if (rel & bit) {                             /* a key goes up as it went down, whatever the mode now [0.9.2] */
            int how = ui.key_how[i];
            ui.key_how[i] = KH_NONE;
            if (how == KH_NOTE) {
                play_note_off(i);
                continue;
            }
            if (how == KH_SEL)
                continue;
            if (how == KH_PERF) {
                if (ui.perf)
                    perf_key(i, 0);
                continue;
            }
            if (how == KH_GRID) {
                if (w >= 0)
                    grid_white(w, 0);
                continue;
            }
        }
        if (press & bit)
            ui.key_how[i] = (uint8_t)(ui.perf ? KH_PERF : ui.grid ? KH_GRID : KH_NOTE);
        if (ui.perf) {
            perf_key(i, (press & bit) != 0);
            continue;
        }
        if (ui.grid) {
            if (w >= 0 && (press & bit) && drum_trk() && (k >> BK[7] & 1u)) {   /* a kit: black 8 + a white key picks
                                                                              * the pad [TONIC 0.6.1] */
                pad_pick(w % NPAD);
                continue;
            }
            if (w >= 0) {
                int p, lenset = 0;
                if (press & bit)                     /* a page key held + a white key: the track's length */
                    for (p = 0; p < 4; p++)
                        if (k >> BK[p] & 1u) {
                            item_t it = TF(TF_LEN_);
                            char t[8];
                            item_set(&it, p * 16 + w + 1);
                            tx_num(t, ttrk()->len);
                            app_say("LENGTH", t);
                            lenset = 1;
                            break;
                        }
                if (!lenset)
                    grid_white(w, (press & bit) != 0);
            } else if (bl >= 0 && (press & bit))
                grid_black(bl);
            continue;
        }
        if (sel && !ui.steprec && w >= 0 && w < 4 && (press & bit)) {   /* SEL + keys 1-4: the track */
            ui.key_how[i] = KH_SEL;
            ui.trk = (uint8_t)w;
            ui.redraw = 1;
            continue;
        }
        if (press & bit)
            play_note_on(i);
        else
            play_note_off(i);
    }
    ui.keys = k;
}

/* ------------------------------------------------------------------- encoders --- */
/* where the track's sound is in the PRESETS list: a factory sound or a library slot of its name, else the last one */
static int sound_index(void)
{
    const char *nm = tsnd()->name;
    int i, j;
    for (i = 0; i < NFACTORY + 32; i++) {
        const char *c = i < NFACTORY ? FACTORY[i].name : lib[i - NFACTORY].name;
        for (j = 0; j < NAMELEN - 1 && c[j] && c[j] == nm[j]; j++)
            ;
        if (c[0] && (j == NAMELEN - 1 || (!c[j] && !nm[j])))
            return i;
    }
    return ui.sound_i % (NFACTORY + 32);
}

static void preset_turn(int d)
{
    int s = first_held();
    if (ui.held && !ui.steprec)
        d = d > 0 ? 1 : -1;                          /* sound locks: one a turn */
    if (ui.held && s >= 0) {                         /* a sound lock from the pool [MANUAL 10.9.2] */
        int16_t v = -1;
        int t, nv;
        lock_get(pcur, ui.trk, s, P_SOUND, &v);
        nv = clamp(v + d, -1, 15);
        for (t = 0; t < NSTEP; t++)
            if ((ui.held >> t & 1u) && (pcur->trig[ui.trk][t].flags & TF_NOTE)) {
                if (nv < 0)
                    lock_clear(pcur, ui.trk, t, P_SOUND);
                else if (lock_set(pcur, ui.trk, t, P_SOUND, nv) < 0 || !pattern_fits())
                    lock_clear(pcur, ui.trk, t, P_SOUND);
            }
        ui.held_edit = 1;
        app_say("SOUND LOCK", nv < 0 ? "OFF" : pool[nv].name[0] ? pool[nv].name : "EMPTY");
        mark_dirty();
        return;
    }
    if (ui.steprec) {
        ui.step_cur = (uint8_t)((ui.step_cur + d + 64 * ttrk()->len) % ttrk()->len);
        ui.spage = (uint8_t)(ui.step_cur / 16);
        ui.redraw = 1;
        return;
    }
    if (ui.grp == G_HOME) {                          /* the pattern */
        int idx = ((next_idx >= 0 ? next_idx : cur_idx) + d + NPAT) % NPAT;
        set.chain_n = 0;
        pattern_select(idx);
        ui.redraw = 1;
        return;
    }
    if (ui.grp == G_SAVE) {
        ui.save_slot = (uint8_t)((ui.save_slot + d + 64) % 64);
        ui.redraw = 1;
        return;
    }
    /* the track's sound: a list window opens at the track's sound (factory sounds, then the user library); every
     * detent moves its cursor a sound, SCL a group; OCT+ loads the sound under it, OCT- closes [TONIC] */
    {
        int n = NFACTORY + 32, i, steps = d < 0 ? -d : d, dir = d < 0 ? -1 : 1, k;
        if (!ui.plist_on) {
            ui.plist_on = 1;
            ui.plist_i = (uint8_t)sound_index();
            steps = 0;                               /* the first detent opens it where the sound is */
        }
        i = ui.plist_i;
        if (steps > 8)
            steps = 8;
        if (steps && (plat_buttons() >> B_SEL & 1u) && i < NFACTORY) {
            int f, f2;
            preset_group(i, &f);
            if (dir > 0) {                           /* the next group (after the last: the user library) */
                for (k = i; k < NFACTORY && (preset_group(k, &f2), f2 == f); k++)
                    ;
                i = k;
            } else if (i > f)                        /* back: this group's first, then the one before */
                i = f;
            else if (f > 0)
                preset_group(f - 1, &i);
            else
                i = NFACTORY;
        } else if (steps && (plat_buttons() >> B_SEL & 1u)) {   /* in the library: on to the first group, back to
                                                                 * the last [0.9.2] */
            if (dir > 0)
                i = 0;
            else
                preset_group(NFACTORY - 1, &i);
        } else
            for (k = 0; k < steps; k++)
                i = (i + dir + n) % n;
        for (k = 0; i >= NFACTORY && !lib[i - NFACTORY].name[0] && k < n; k++)   /* empty library slots: past */
            i = (i + dir + n) % n;
        ui.plist_i = (uint8_t)i;
        ui.plist_t = plat_ms();
        ui.msg_until = 0;
        ui.redraw = 1;
    }
}

static void encoders(void)
{
    int e;
    uint32_t now = plat_ms();
    for (e = 0; e < NE; e++) {
        int d = plat_enc(e);
        uint32_t dt;
        if (!d)
            continue;
        dt = now - ui.enc_t[e];
        ui.enc_t[e] = now;
        ui.help_block = 1;
        switch (e) {
        case EN_K1: case EN_K2: case EN_K3: case EN_K4:
            if (ui.perf && e <= EN_K2) {             /* HOME held: K1 the DJ filter, K2 the ducking depth [TONIC] */
                item_t it = F_(e == EN_K1 ? P_MFLT : P_SCDEP);
                int lk;
                item_set(&it, item_get(&it, &lk) + d * (dt < 40 ? 4 : 1));
                ui.redraw = 1;
            } else if (ui.grp == G_HOME) {           /* home: the track levels */
                item_t it = L_(e - EN_K1);
                int lk;
                item_set(&it, item_get(&it, &lk) + d * (dt < 40 ? 4 : 1));
                ui.redraw = 1;
            } else
                knob(e - EN_K1, d, dt);
            break;
        case EN_SELECT:
            if (ui.grp == G_HOME) {
                item_t it = PF(PF_BPM_);
                int lk;
                item_set(&it, item_get(&it, &lk) + d * (dt < 60 ? 10 : 1));
            } else if (ui.grp == G_SAVE)
                ui.save_row = (int8_t)clamp(ui.save_row + d, 0, 9), ui.confirm = 0;
            else if (grp_n(ui.grp))
                ui.row[ui.grp] = (uint8_t)((ui.row[ui.grp] + d + 16 * grp_n(ui.grp)) % grp_n(ui.grp));
            ui.redraw = 1;
            break;
        case EN_ALGO:
            ui.trk = (uint8_t)((ui.trk + d + 4 * NTRK) % NTRK);
            ui.held = 0;
            app_say("TRACK", ui.trk == 0 ? "T1" : ui.trk == 1 ? "T2" : ui.trk == 2 ? "T3" : "T4");
            break;
        case EN_PRESET:
            preset_turn(d);
            break;
        }
    }
}

/* --------------------------------------------------------------------- the LEDs --- */
static void leds(void)
{
    uint32_t lit = 0, keys = 0, mid_k = 0, dim_k = 0, mid_b = 0, now = plat_ms();
    int s, i, blink = (now / 125u) & 1u, step = engine_step(ui.trk);
    static const uint8_t GB[NG] = {B_SEQ, B_EDIT, B_ENV, B_LFO, B_FX, B_ARP, B_GLO, B_HOME, B_SAVE};
    lit |= 1u << GB[ui.grp];
    if (eng.playing)
        lit |= LED_PLAY_GREEN | (eng.rec_live ? 1u << B_PLAY : 0);
    if (ui.grid || (eng.rec_live && blink) || (ui.steprec && (now / 90u) % 4u < 2u && (now / 90u) % 4u != 1u))
        lit |= 1u << B_REC;
    if (plat_buttons() >> B_SEL & 1u)
        lit |= 1u << B_SEL;
    if (store_unsaved() && !eng.playing)
        mid_b |= 1u << B_SAVE;
    if (ui.perf && ((plat_buttons() >> B_SEL & 1u) || ui.pfx_keys)) {   /* the effects' keys [TONIC 0.7] */
        for (i = 0; i < NPFX; i++) {
            if (ui.pfx_keys >> i & 1u)
                keys |= 1u << WK[i];
            else
                dim_k |= 1u << WK[i];
        }
    } else if (ui.perf) {
        for (i = 0; i < 16; i++) {
            int idx = (cur_idx / 16) * 16 + i;
            if (idx == cur_idx)
                keys |= 1u << WK[i];
            else if (idx == next_idx && blink)
                keys |= 1u << WK[i];
            else
                dim_k |= 1u << WK[i];
        }
        for (i = 0; i < 4; i++)
            if (!(eng.mute >> i & 1u))
                keys |= 1u << BK[i];
        if (eng.fill)
            keys |= 1u << BK[4];
    } else if (ui.grid || ui.steprec) {
        for (i = 0; i < 16; i++) {
            s = cur_step_base() + i;
            if (s >= ttrk()->len)
                continue;
            {
                const trig_t *g = &pcur->trig[ui.trk][s];
                int on = (g->flags & TF_NOTE) != 0, lk = (g->flags & TF_LOCK) != 0;
                if (on && tsnd()->s[S_ENGINE] == ENG_DR && trig_pad(g, cur_pad()) < 0)
                    on = 0, lk = 1;                  /* a kit: the other pads' steps half lit */
                if ((ui.held >> s & 1u) || (ui.steprec && s == ui.step_cur && blink))
                    on = 1;
                if (s == step)                       /* the playhead inverts its key */
                    on = !on, lk = 0;
                if (on)
                    keys |= 1u << WK[i];
                else if (lk)
                    mid_k |= 1u << WK[i];
                else
                    dim_k |= 1u << WK[i];
            }
        }
        for (i = 0; i < 4; i++)
            if (i < page_count()) {
                if (i == ui.spage || (step >= 0 && step / 16 == i && blink))
                    keys |= 1u << BK[i];
                else
                    mid_k |= 1u << BK[i];
            }
    } else {
        keys = ui.keys;
        for (i = 0; i < 16; i++)                     /* the playhead walks the white keys */
            if (step >= 0 && step % 16 == i)
                mid_k |= 1u << WK[i];
    }
    plat_lights(lit, keys, 0, dim_k, 1);
    plat_lights_mid(mid_b, mid_k);
}

/* ------------------------------------------------------------------- the input --- */
static void ui_input(void)
{
    if (ui.plist_on && (plat_ms() - ui.plist_t > 6000u || ui.perf || ui.grp == G_HOME || ui.grp == G_SAVE)) {
        ui.plist_on = 0;                             /* the preset list: closed after 6 s untouched */
        ui.redraw = 1;
    }
    touch_poll();
    buttons(plat_buttons());
    keys(touch_keys());
    encoders();
    rec_poll();
}

/* a kit's pad the grid places: the track's NOTE */
static int cur_pad(void) { return pad_of(ttrk()->tp[P_NOTE - NSP]); }

/* --------------------------------------------------------- the euclidean generator --- */
/* [TONIC 0.6, 0.8] HITS spread as evenly as they go over LEN steps (Bjorklund's rhythm: step i hits when (i x HITS) mod
 * LEN < HITS) or a named RHYTHM, turned ROT steps later, repeated over the track's length. ACC: a second such rhythm
 * of accents (the other hits softer); GHOST: soft notes on the empty steps, by chance; CHANCE: the hits' own. On a kit
 * it writes the picked pad (the other pads stay; accents and ghosts as the pad's LEV locked on its steps, ghosts and
 * chances rolled at each write); on another track note trigs (velocity, conditions), their notes FIXED (the track's
 * NOTE; a hit on a note trig keeps it), UP, UP DN, RANDOM or the CHORD's notes through the keyboard scale over RANGE
 * octaves. Each turn writes the track again: undo takes it back */
static uint8_t *euc_at(int pad)
{
    uint8_t *e = ui.euc[ui.trk][pad];
    if (!e[EU_LEN_]) {                               /* first use: the track's length (at most 16), no hits */
        e[EU_LEN_] = (uint8_t)(ttrk()->len < 16 ? ttrk()->len : 16);
        e[EU_PROB_] = 100;
        e[EU_NRANGE_] = 1;
    }
    return e;
}
static uint8_t *euc_of(void) { return euc_at(drum_trk() ? cur_pad() : NPAD); }
static int euc_bj(int n, int k, int r, int s)        /* Bjorklund: step s of k in n, turned r */
{
    int i = ((s % n) - r % n + n) % n;
    return (i * k) % n < k;
}
static int euc_named(const uint8_t *e) { return e[EU_PAT_] && e[EU_PAT_] < NRHY; }
/* the cycle in force: a named rhythm's own length, else LEN (LEN and HITS stay as set under a named rhythm) [0.9.2] */
static int euc_len(const uint8_t *e) { return euc_named(e) ? RHY[e[EU_PAT_]].len : e[EU_LEN_] ? e[EU_LEN_] : 16; }
static int euc_hits(const uint8_t *e)
{
    int i, n = 0;
    if (!euc_named(e))
        return e[EU_PULS_];
    for (i = 0; i < RHY[e[EU_PAT_]].len; i++)
        n += RHY[e[EU_PAT_]].mask >> i & 1u;
    return n;
}
static int euc_get(int k)
{
    const uint8_t *e;
    if (k >= EU_KIT0_)
        return euc_at((k - EU_KIT0_) & 7)[EU_PULS_];
    e = euc_of();
    if (k == EU_LEN_) return euc_len(e);             /* (what plays: a named rhythm's length and hits) */
    if (k == EU_PULS_) return euc_hits(e);
    return e[k];
}
static int euc_pad_len(int pad) { return euc_len(euc_at(pad & 7)); }
static int euc_hit(const uint8_t *e, int s)
{
    int n = euc_len(e);
    if (euc_named(e))                                /* a named rhythm */
        return RHY[e[EU_PAT_]].mask >> ((s % n - e[EU_ROT_] % n + n) % n) & 1u;
    return euc_bj(n, e[EU_PULS_], e[EU_ROT_], s);
}
/* ACC: ACC of the cycle's hits accented, spread over the hits (A.ROT turns them along the hits), so accents always
 * fall on hits [0.9.2] */
static int euc_acc(const uint8_t *e, int s)
{
    int n = euc_len(e), k = euc_hits(e), i, h = 0, c = ((s % n) + n) % n;
    if (!e[EU_AH_] || !k || !euc_hit(e, s))
        return 0;
    for (i = 0; i < c; i++)                          /* the hit's index in the cycle */
        h += euc_hit(e, i);
    return euc_bj(k, e[EU_AH_] < k ? e[EU_AH_] : k, e[EU_AR_], h);
}
static uint32_t euc_rnd(void) { ui.euc_seed = ui.euc_seed * 1664525u + 1013904223u; return ui.euc_seed >> 8; }
static int euc_cond(int pct)                         /* the PROB condition nearest pct (none: 100) */
{
    int i, best = 0;
    if (pct >= 100)
        return C_NONE;
    for (i = 1; i < C_AB0 - C_PROB0; i++)
        if ((PROB_PCT[i] > pct ? PROB_PCT[i] - pct : pct - PROB_PCT[i]) <
            (PROB_PCT[best] > pct ? PROB_PCT[best] - pct : pct - PROB_PCT[best]))
            best = i;
    return C_PROB0 + best;
}
/* the notes a melodic track's hits walk (NOTES not FIXED): the scale's notes from NOTE over RANGE octaves, or the
 * CHORD's notes (SCALE 3 if the sound has none) stacked the same way */
static int euc_notes(const uint8_t *e, uint8_t *out)
{
    const track_t *k = ttrk();
    int base = k->tp[P_NOTE - NSP], top = base + 12 * (e[EU_NRANGE_] ? e[EU_NRANGE_] : 1), n = 0, x;
    uint16_t m = k->kscale > 0 && k->kscale < NKSCALE ? KSCALE_MASK[k->kscale] : 0xAB5;
    if (e[EU_NMODE_] == EN_CHORD) {
        sound_t c = *tsnd();
        uint8_t ch[NCHORD];
        int o, i, cn;
        if (c.s[S_ARPCHD] == ACH_OFF)
            c.s[S_ARPCHD] = ACH_SCL3;
        c.s[S_CHBASS] = 0;
        cn = chord_notes(&c, base, k->kscale, k->kroot, ch);
        for (o = 0; o < (e[EU_NRANGE_] ? e[EU_NRANGE_] : 1); o++)
            for (i = 0; i < cn && n < 32; i++)
                if (ch[i] + 12 * o < 128)
                    out[n++] = (uint8_t)(ch[i] + 12 * o);
        return n;
    }
    for (x = base; x < top && x < 128 && n < 32; x++)
        if (m >> (((x - k->kroot) % 12 + 12) % 12) & 1u)
            out[n++] = (uint8_t)x;
    if (!n)
        out[n++] = (uint8_t)base;
    return n;
}
/* a note trig the generator takes away: its own locks stay, as a lock trig (it was one, or the user locked it) */
static void euc_unnote(int t, int s)
{
    trig_t *g = &pcur->trig[t][s];
    trig_flags_update(pcur, t, s);
    g->flags = (uint8_t)((g->flags & (TF_PLOCK | TF_SLOCK)) ? (g->flags & (TF_PLOCK | TF_SLOCK)) | TF_LOCK : 0);
}
static void euc_write_pad(int pad)
{
    const uint8_t *e = euc_at(pad);
    int t = ui.trk, s, L = ttrk()->len, kit = drum_trk(), h = 0, nn = 0, lastn, full = 0, crowd = 0;
    int note = kit ? PAD_NOTE[pad] : ttrk()->tp[P_NOTE - NSP], lev = kit ? tsnd()->p[DRP[pad][DK_LEV]] : 0;
    uint8_t notes[32];
    if (!kit && e[EU_NMODE_] != EN_FIXED)
        nn = euc_notes(e, notes);
    lastn = note;
    for (s = 0; s < L && !full; s++) {
        trig_t *g = &pcur->trig[t][s];
        int hit = euc_hit(e, s), acc = euc_acc(e, s), ghost = 0;
        step_save(t, s);
        if (hit && e[EU_PROB_] < 100 && kit)         /* a kit: the chance rolled now */
            hit = (int)(euc_rnd() % 100u) < e[EU_PROB_];
        if (!hit && e[EU_GHOST_] && !euc_hit(e, s))
            ghost = kit ? (int)(euc_rnd() % 100u) < e[EU_GHOST_] : 1;
        if (kit) {
            int pi = trig_pad(g, pad), want = hit || ghost;
            if (g->flags & TF_NOTE) {
                if (want && pi < 0 && g->nnote < NNOTE) {        /* the pad joins the step */
                    int n = g->nnote ? g->nnote : 1;
                    g->chord[n - 1] = (int8_t)(note - g->note);
                    g->nnote = (uint8_t)(n + 1);
                } else if (want && pi < 0)
                    crowd = 1;                                  /* four pads there already */
                else if (!want && pi >= 0) {                    /* the pad leaves it (the step with it, if alone) */
                    lock_clear(pcur, t, s, DRP[pad][DK_LEV]);
                    if (g->nnote > 1) {
                        int ns[NNOTE], n = 0, i;
                        for (i = 0; i < g->nnote; i++)
                            if (i != pi)
                                ns[n++] = g->note + (i ? g->chord[i - 1] : 0);
                        g->note = (uint8_t)ns[0];
                        for (i = 1; i < n; i++)
                            g->chord[i - 1] = (int8_t)(ns[i] - ns[0]);
                        g->nnote = (uint8_t)n;
                    } else
                        euc_unnote(t, s);
                }
            } else if (want) {                       /* a new trig (a lock trig becomes one, its locks kept) */
                g->flags = (uint8_t)((g->flags & (TF_PLOCK | TF_SLOCK)) | TF_NOTE);
                g->note = (uint8_t)note;
                g->nnote = 1;
                g->vel = 0;
                g->len = 0xFF;
                g->micro = 0;
                g->cond = 0;
            }
            /* the pad's level on its step: an accent full, the other hits 70 % (with ACC), a ghost 35 % */
            if (want && trig_pad(g, pad) >= 0 && (ghost || (e[EU_AH_] && !acc))) {
                if (lock_set(pcur, t, s, DRP[pad][DK_LEV], ghost ? lev * 35 / 100 : lev * 70 / 100) < 0)
                    full = 1;
            } else if (trig_pad(g, pad) >= 0 || !(g->flags & TF_NOTE))
                lock_clear(pcur, t, s, DRP[pad][DK_LEV]);
        } else if (hit || ghost) {
            int n = note;
            if (hit && nn) {                                 /* the hits walk their notes */
                int m = e[EU_NMODE_];
                n = m == EN_RANDOM ? notes[euc_rnd() % (uint32_t)nn] :
                    m == EN_UPDN && nn > 1 ? notes[(h / (nn - 1)) & 1 ? nn - 1 - h % (nn - 1) : h % (nn - 1)] :
                    notes[h % nn];
                h++;
            } else if (ghost)
                n = lastn;
            if (!(g->flags & TF_NOTE)) {                     /* a new note trig (a lock trig becomes one) */
                g->flags = (uint8_t)((g->flags & (TF_PLOCK | TF_SLOCK)) | TF_NOTE);
                g->note = (uint8_t)n;
                g->nnote = 1;
                g->len = 0xFF;
                g->micro = 0;
            } else if (nn || ghost)
                g->note = (uint8_t)n, g->nnote = 1;
            if (hit) lastn = g->note;
            g->vel = (uint8_t)(ghost ? 30 : e[EU_AH_] ? (acc ? 120 : 72) : 0);
            g->cond = (uint8_t)(ghost ? euc_cond(e[EU_GHOST_]) : euc_cond(e[EU_PROB_]));
        } else if (g->flags & TF_NOTE)
            euc_unnote(t, s);
        if (full || !pattern_fits()) {               /* it would not fit: the step as it was, and no further [0.9.2] */
            step_restore(t, s);
            full = 1;
        }
    }
    if (full)
        app_say("PATTERN", "FULL");
    else if (crowd)
        app_say("STEP", "4 PADS FULL");
    mark_dirty();
}
static void euc_write(void) { euc_write_pad(drum_trk() ? cur_pad() : NPAD); }
static void euc_set(int k, int v)
{
    uint8_t *e;
    int pad = drum_trk() ? cur_pad() : NPAD, n;
    if (k >= EU_KIT0_) {                             /* a kit's overview: that pad's hits */
        pad = (k - EU_KIT0_) & 7;
        k = EU_PULS_;
    }
    e = euc_at(pad);
    if ((k == EU_LEN_ || k == EU_PULS_) && euc_named(e)) {   /* LEN, HITS: the euclidean rhythm again, from what
                                                              * the named one showed */
        e[EU_LEN_] = (uint8_t)euc_len(e);
        e[EU_PULS_] = (uint8_t)euc_hits(e);
        e[EU_PAT_] = 0;
    }
    e[k] = (uint8_t)v;
    if (k != EU_PAT_ && !euc_named(e)) {
        if (e[EU_LEN_] < 1) e[EU_LEN_] = 1;
        if (e[EU_PULS_] > e[EU_LEN_]) e[EU_PULS_] = e[EU_LEN_];
    }
    n = euc_len(e);
    if (e[EU_AH_] > n) e[EU_AH_] = (uint8_t)n;
    if (e[EU_ROT_] >= n) e[EU_ROT_] = (uint8_t)(n - 1);
    if (e[EU_AR_] >= n) e[EU_AR_] = (uint8_t)(n - 1);
    if (e[EU_PULS_] || euc_named(e) || k == EU_PULS_ || k == EU_PAT_)   /* (no hits, no rhythm: only HITS and
                                                                         * STYLE write) [0.9.2: STYLE too] */
        euc_write_pad(pad);
}
/* the settings follow the pattern's undo [0.9.2]: 0 a new pattern, 1 an edit settled, 2 / 3 undo from the edit not
 * yet settled / from the one before */
static uint8_t euc_sh[NTRK][NPAD + 1][NEU_], euc_un[NTRK][NPAD + 1][NEU_];
static void euc_hist(int op)
{
    uint8_t tmp[NTRK][NPAD + 1][NEU_];
    switch (op) {
    case 0: memcpy(euc_sh, ui.euc, sizeof euc_sh); memcpy(euc_un, ui.euc, sizeof euc_un); break;
    case 1: memcpy(euc_un, euc_sh, sizeof euc_un); memcpy(euc_sh, ui.euc, sizeof euc_sh); break;
    default:
        memcpy(tmp, ui.euc, sizeof tmp);
        memcpy(ui.euc, op == 2 ? euc_sh : euc_un, sizeof tmp);
        memcpy(euc_un, tmp, sizeof tmp);
        memcpy(euc_sh, ui.euc, sizeof euc_sh);
        break;
    }
}

/* a kit's pad picked [TONIC 0.6.1]: the one the grid places (the track's NOTE), its row on the SYN pages */
static void pad_pick(int p)
{
    ttrk()->tp[P_NOTE - NSP] = PAD_NOTE[p];
    if (ui.grp == G_SYN && ui.row[G_SYN] < NPAD)
        ui.row[G_SYN] = (uint8_t)p;
    app_say("PAD", pad_label(p));
    mark_dirty();
    ui.redraw = 1;
}
