/* SPDX-License-Identifier: MPL-2.0
 * TONIC storage: the project in the flash store (plat_store_*: A/B sectors, CRC, torn-write safe).
 *   OBJ_SETTINGS  settings and the project header (tempo, chain, MIDI)
 *   OBJ_POOL      the project's sound pool: 16 sounds (sound locks)
 *   OBJ_LIB0/1    the user sound library: 32 sounds
 *   OBJ_PAT0+n    pattern n (A01..B16), packed (core/pattern.c)
 * A flash write stops the CPU for a moment (the code runs from the flash, the sound stops), so nothing is written
 * while the sequencer plays: an edited pattern that is left stays in RAM, packed, and everything unsaved is written
 * once playback has stopped and the effects have died away, or at SAVE. Part of app.c's unity build. */

#define SET_MAGIC 0x53544E54u     /* "TNTS" */
#define SET_VERSION 1
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t cur;                  /* the pattern last played */
    uint8_t tempo_ptn;            /* 1: each pattern's own tempo; 0: the project's */
    uint16_t bpm10;               /* the project's tempo */
    uint8_t midi_out[NTRK];       /* MIDI out channel per track, 16 = off */
    uint8_t midi_in;              /* the auto channel (keys from MIDI play the active track), 16 = omni */
    uint8_t midi_trk_ch;          /* 1: channels 1-4 play tracks 1-4 */
    uint8_t clock_in, clock_out;
    uint8_t mute;
    uint8_t vel_sense;            /* keys: 1 velocity sensed, 0 fixed */
    uint8_t vel_fixed;
    uint8_t quant;                /* live recording quantized */
    uint8_t met;                  /* metronome */
    uint8_t chain_n;
    uint8_t chain[64];
    uint8_t demo;                 /* the demo project was written */
    uint8_t midi_only;            /* bit t: track t is a MIDI track */
    uint8_t song_on;              /* SONG mode [MANUAL 10.11] */
    uint8_t song_n;               /* rows */
    uint8_t song_end;             /* 0 LOOP, 1 STOP */
    uint8_t song_pat[32], song_rep[32], song_mute[32];
    uint8_t theme;                /* the screen's colours (draw.c THEMES) [TONIC] */
    uint8_t rsv[27];
} settings_t;
static settings_t set;

#define SND_MAGIC 0x44534E54u     /* "TNSD" */
/* version 4 (0.12): as 3, with 0.12's parameters (an older firmware, which reads 3 by its own NSP, refuses it);
 * version 3 (0.5): the sounds in their compact form (sound_to_bits: 16 of them fit an object), the layout named;
 * version 2 names the layout it was written with (sound_t whole; it grows at its ends: sound_from_raw reads any);
 * version 1 did not: its layout is told from its size (and, for the two of 224 bytes, which one reads as sounds) */
typedef struct {
    uint32_t magic;
    uint16_t version, count;
    uint8_t nsp, nss;
    uint16_t size;                /* bytes a sound */
} sndbank_t;
#define BANK_HDR 12

static sound_t pool[NPOOL > 16 ? 16 : NPOOL];
static sound_t lib[32] PLAT_POOL;
static uint8_t pool_dirty, lib_dirty, set_dirty;

/* the patterns in RAM: the one playing (and edited), the next one (cued), the temporary save */
static pattern_t pat_a PLAT_POOL, pat_b PLAT_POOL, pat_snap PLAT_POOL;
static pattern_t *pcur = &pat_a, *pnext = &pat_b;
static uint8_t copy_buf[PLAT_STORE_MAX] PLAT_POOL;  /* COPY PATTERN, packed (its own: not the temporary save's) [0.9.2] */
static uint16_t copy_len;
static int8_t cur_idx = 0, next_idx = -1, snap_idx = -1;
static uint8_t cur_dirty;
/* edited patterns that were left while playing: packed, written later */
#define NDIRTY 4
static struct { int8_t idx; uint16_t len; } dirty[NDIRTY];
static uint8_t dirty_buf[NDIRTY][PLAT_STORE_MAX] PLAT_POOL;
static uint32_t pack_buf_w[(PLAT_STORE_MAX + 64) / 4];
#define pack_buf ((uint8_t *)pack_buf_w)
static uint8_t store_hold;        /* a USB backup streams: no saving */
static uint32_t stop_ms;          /* when the sequencer stopped (the flush waits for the effects) */
static uint8_t flush_wanted;

static int pattern_load_into(int idx, pattern_t *p);
static void pattern_cue_poll(void);

/* ------------------------------------------------------------------- the settings --- */
static void settings_defaults(void)
{
    int t;
    for (t = 0; t < (int)sizeof set; t++)
        ((uint8_t *)&set)[t] = 0;
    set.magic = SET_MAGIC;
    set.version = SET_VERSION;
    set.bpm10 = 1200;
    for (t = 0; t < NTRK; t++)
        set.midi_out[t] = 16;
    set.midi_in = 16;
    set.midi_trk_ch = 1;
    set.vel_sense = 1;
    set.vel_fixed = 100;
    set.quant = 1;
}

static int bank_load(uint32_t obj, sound_t *dst, int n)
{
    sndbank_t *bp = (sndbank_t *)(void *)pack_buf;   /* (pack_buf: big enough, word aligned) */
#define b (*bp)
    int i, got = plat_store_load(obj, pack_buf, sizeof pack_buf_w), nsp, nss, size, hdr, cnt;
    const uint8_t *raw = pack_buf;
    for (i = 0; i < n; i++) {
        sound_init(&dst[i]);
        dst[i].name[0] = 0;                          /* an empty slot */
    }
    if (got < 8 || b.magic != SND_MAGIC || !b.count)
        return -1;
    cnt = b.count;
    if (b.version == 4 || b.version == 3 || b.version == 2) {
        nsp = b.nsp;
        nss = b.nss;
        size = b.size;
        hdr = BANK_HDR;
        if (size != (b.version >= 3 ? sound_bits_size(nsp, nss) : sound_raw_size(nsp, nss)))
            return -1;
    } else if (b.version == 1) {                     /* 220: before LO-FI; 224: with it (0.1), or with ENGINE too */
        hdr = 8;
        size = (got - 8) / cnt;
        if (size == sound_raw_size(65, 19)) { nsp = 65; nss = 19; }
        else if (size == sound_raw_size(67, 20)) { nsp = 67; nss = 20; }
        else return -1;
    } else
        return -1;
    for (i = 0; i < n && i < cnt && hdr + (i + 1) * size <= got; i++) {
        const uint8_t *e = raw + hdr + i * size;
        if (b.version >= 3)
            sound_from_bits(&dst[i], e, nsp, nss);
        else if (!sound_from_raw(&dst[i], e, nsp, nss) && b.version == 1 && nss == 20)
            sound_from_raw(&dst[i], e, 67, 19);      /* 0.1's layout, without ENGINE */
    }
    return 0;
}
static int bank_save(uint32_t obj, const sound_t *src, int n)
{
    sndbank_t *bp = (sndbank_t *)(void *)pack_buf;
    int i;
    int size = sound_bits_size(NSP, NSS);
    b.magic = SND_MAGIC;
    b.version = 4;
    b.count = (uint16_t)n;
    b.nsp = NSP;
    b.nss = NSS;
    b.size = (uint16_t)size;
    for (i = 0; i < n; i++)
        sound_to_bits(&src[i], pack_buf + BANK_HDR + i * size);
    return plat_store_save(obj, &b, (uint32_t)(BANK_HDR + n * size));
#undef b
}

/* --------------------------------------------------------------------- patterns --- */
static int dirty_find(int idx)
{
    int i;
    for (i = 0; i < NDIRTY; i++)
        if (dirty[i].idx == idx && dirty[i].len)
            return i;
    return -1;
}

static int pattern_load_into(int idx, pattern_t *p)
{
    int k = dirty_find(idx), n;
    if (k >= 0 && pattern_unpack(p, dirty_buf[k], dirty[k].len) == 0)
        return 0;
    n = plat_store_load(OBJ_PAT0 + (uint32_t)idx, pack_buf, sizeof pack_buf_w);
    if (n > 0 && pattern_unpack(p, pack_buf, n) == 0)
        return 0;
    pattern_init(p);                                 /* never saved: an empty pattern */
    return 1;
}

/* the playing pattern, edited and left: packed into the dirty list (or saved at once when stopped) */
/* a pattern too big for its flash object (no edit should get it there): its last locks dropped until it fits, rather
 * than the whole pattern lost [0.9.2]. Returns the packed size (pack_buf), -1 if even that fails */
static int pack_squeeze(pattern_t *p)
{
    int n = pattern_pack(p, pack_buf, PLAT_STORE_MAX), i, dropped = 0;
    while (n < 0 && p->nlock) {
        for (i = p->nlock - 1; i >= 0 && p->lock[i].f.param == P_NONE; i--)
            ;
        if (i < 0)
            break;
        {
            plock_t l = p->lock[i];
            l.f.param = P_NONE;
            p->lock[i].w = l.w;
        }
        dropped = 1;
        n = pattern_pack(p, pack_buf, PLAT_STORE_MAX);
    }
    if (dropped) {
        int t, st;
        for (t = 0; t < NTRK; t++)
            for (st = 0; st < NSTEP; st++)
                trig_flags_update(p, t, st);
        app_say("PATTERN FULL", "LOCKS DROPPED");
    }
    return n;
}
static int pattern_keep(int idx, pattern_t *p)
{
    int n = pack_squeeze(p), k, i;
    if (n < 0)
        return -1;
    k = dirty_find(idx);
    if (k < 0)
        for (i = 0; i < NDIRTY; i++)
            if (!dirty[i].len) {
                k = i;
                break;
            }
    if (k < 0) {                                     /* full: write one now (a short gap in the sound) */
        if (plat_store_save(OBJ_PAT0 + (uint32_t)dirty[0].idx, dirty_buf[0], dirty[0].len) == 0)
            dirty[0].len = 0;
        k = 0;
    }
    for (i = 0; i < n; i++)
        dirty_buf[k][i] = pack_buf[i];
    dirty[k].idx = (int8_t)idx;
    dirty[k].len = (uint16_t)n;
    return 0;
}

/* everything unsaved to the flash (main loop, never while playing): 0 ok */
static int store_flush(void)
{
    int i, rc = 0;
    if (store_hold)
        return -1;
    if (cur_dirty) {
        int n = pack_squeeze(pcur);
        if (n > 0 && plat_store_save(OBJ_PAT0 + (uint32_t)cur_idx, pack_buf, (uint32_t)n) == 0)
            cur_dirty = 0;
        else
            rc = -1;
        i = dirty_find(cur_idx);
        if (i >= 0)
            dirty[i].len = 0;
    }
    for (i = 0; i < NDIRTY; i++)
        if (dirty[i].len) {
            if (plat_store_save(OBJ_PAT0 + (uint32_t)dirty[i].idx, dirty_buf[i], dirty[i].len) == 0)
                dirty[i].len = 0;
            else
                rc = -1;
        }
    if (pool_dirty && bank_save(OBJ_POOL, pool, 16) == 0)
        pool_dirty = 0;
    if (lib_dirty && bank_save(OBJ_LIB0, lib, 16) == 0 && bank_save(OBJ_LIB1, lib + 16, 16) == 0)
        lib_dirty = 0;
    if (set.cur != (uint8_t)cur_idx) {
        set.cur = (uint8_t)cur_idx;
        set_dirty = 1;
    }
    if (set_dirty && plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0)
        set_dirty = 0;
    flush_wanted = 0;
    return rc;
}

static int store_unsaved(void)
{
    int i;
    for (i = 0; i < NDIRTY; i++)
        if (dirty[i].len)
            return 1;
    return cur_dirty || pool_dirty || lib_dirty || set_dirty || set.cur != (uint8_t)cur_idx;
}

/* the main loop's part: flush once stopped and quiet */
static void store_poll(void)
{
    uint32_t now = plat_ms();
    if (eng.playing) {
        stop_ms = 0;
        return;
    }
    if (!stop_ms)
        stop_ms = now ? now : 1;
    if ((store_unsaved() || flush_wanted) && now - stop_ms > 2500u && engine_level(0) + engine_level(1) +
        engine_level(2) + engine_level(3) < 0.002f && !store_hold)
        store_flush();
}

/* undo [TONIC]: pat_shadow is the pattern as it last settled (no edit for 0.7 s), pat_undo the state before that;
 * an undo swaps the pattern with the one before the latest edits, a second undo swaps back (redo) */
static pattern_t pat_shadow, pat_undo;   /* (both in RAM: the pool took the new voices' HARM tables (0.11) and the
                                          * larger sounds (0.12)) */
static void euc_hist(int op);                        /* ui.c: the euclidean settings follow the undo [0.9.2] */
static int8_t shadow_idx = -1;
static uint8_t undo_ok, edit_pend;
static uint32_t edit_t;
static void mark_dirty(void)
{
    cur_dirty = 1;
    edit_pend = 1;
    edit_t = plat_ms();
}
static void undo_poll(void)
{
    if (shadow_idx != cur_idx) {                     /* another pattern: its own history, from now */
        pat_shadow = *pcur;
        shadow_idx = cur_idx;
        undo_ok = 0;
        edit_pend = 0;
        euc_hist(0);
    } else if (edit_pend && plat_ms() - edit_t > 700u) {
        pat_undo = pat_shadow;
        pat_shadow = *pcur;
        undo_ok = 1;
        edit_pend = 0;
        euc_hist(1);
    }
}
static int undo_do(void)                             /* 1 done, 0 nothing to undo, -1 not now */
{
    const pattern_t *src = edit_pend ? &pat_shadow : undo_ok ? &pat_undo : 0;
    if (!src)
        return 0;
    if (next_idx >= 0 || shadow_idx != cur_idx)      /* a pattern change on its way */
        return -1;
    euc_hist(src == &pat_shadow ? 2 : 3);
    *pnext = *src;                                   /* (first: src may be pat_undo) */
    pat_undo = *pcur;                                /* what is undone, for the redo */
    engine_all_off(-1);
    eng_pat = pnext;
    {
        pattern_t *t = pcur;
        pcur = pnext;
        pnext = t;
    }
    pat_shadow = *pcur;
    undo_ok = 1;
    edit_pend = 0;
    cur_dirty = 1;
    return 1;
}

/* the size the current pattern would have in the flash: an edit that would pass PLAT_STORE_MAX is refused */
static int pattern_fits(void) { return pattern_pack(pcur, pack_buf, PLAT_STORE_MAX) > 0; }

/* change pattern: cued at the change point while playing, at once when stopped */
static void pattern_select(int idx)
{
    if (idx < 0 || idx >= NPAT)
        return;
    if (eng.playing) {
        /* the cue off first, and a cue the render took meanwhile handled, so that pnext is never written while it
         * could start playing */
        engine_cue(0);
        pattern_cue_poll();
        if (idx == cur_idx) {                        /* the same pattern: no change */
            next_idx = -1;
            return;
        }
        pattern_load_into(idx, pnext);
        next_idx = (int8_t)idx;
        engine_cue(pnext);
        return;
    }
    if (idx == cur_idx)
        return;
    if (cur_dirty)
        pattern_keep(cur_idx, pcur);
    cur_dirty = 0;
    pattern_load_into(idx, pnext);
    engine_cue(pnext);                               /* stopped: eng_pat = pnext now */
    {
        pattern_t *t = pcur;
        pcur = pnext;
        pnext = t;
    }
    cur_idx = (int8_t)idx;
    next_idx = -1;
    set.cur = (uint8_t)idx;
    set_dirty = 1;
    if (!set.tempo_ptn)
        pcur->bpm10 = set.bpm10;
    else
        eng.bpm = (float)pcur->bpm10 * 0.1f;
}

/* the render took the cued pattern (main loop, each frame) */
static void pattern_cue_poll(void)
{
    if (next_idx >= 0 && engine_took_cue()) {
        pattern_t *t = pcur;
        if (cur_dirty)
            pattern_keep(cur_idx, pcur);
        cur_dirty = 0;
        pcur = pnext;
        pnext = t;
        cur_idx = next_idx;
        next_idx = -1;
        set.cur = (uint8_t)cur_idx;
        set_dirty = 1;
        if (set.tempo_ptn)
            eng.bpm = (float)pcur->bpm10 * 0.1f;
        if (set.chain_n > 1 && !set.song_on) {       /* a chain: the next one cued at once */
            int i;
            for (i = 0; i < set.chain_n; i++)
                if (set.chain[i] == cur_idx)
                    break;
            pattern_select(set.chain[(i + 1) % set.chain_n]);
        }
    }
}

/* SONG mode [MANUAL 10.11]: rows of a pattern played REPEAT times with their mutes; at the end LOOP or STOP. The
 * next row's pattern is cued while the last pass of a row plays, so the change is seamless. */
static struct { uint8_t row, passes, cued; uint32_t loops; } song;
static void song_row_start(int r)
{
    song.row = (uint8_t)r;
    song.passes = 0;
    song.cued = 0;
    eng.mute = set.song_mute[r];
}
static void song_start(void)
{
    if (!set.song_on || !set.song_n)
        return;
    set.chain_n = 0;
    pattern_select(set.song_pat[0]);
    song_row_start(0);
    song.loops = 0;
}
static void song_poll(void)
{
    uint32_t lp;
    int last, next;
    if (!set.song_on || !set.song_n || !eng.playing)
        return;
    lp = engine_passes();                            /* (passes since PLAY, across the changes) [0.9.2] */
    if (lp < song.loops)                             /* PLAY again: counted from there */
        song.loops = lp;
    if (lp == song.loops)
        goto cue;
    song.loops = lp;                                 /* a pass of the row's pattern ended */
    song.passes++;
    if (song.passes >= (set.song_rep[song.row] ? set.song_rep[song.row] : 1)) {
        next = song.row + 1;
        if (next >= set.song_n) {
            if (set.song_end) {
                engine_stop();
                return;
            }
            next = 0;
        }
        song_row_start(next);
    }
cue:
    /* the last pass of this row starts now: cue the next row's pattern */
    last = song.passes + 1 >= (set.song_rep[song.row] ? set.song_rep[song.row] : 1);
    if (last && !song.cued) {
        next = song.row + 1 >= set.song_n ? (set.song_end ? -1 : 0) : song.row + 1;
        if (next >= 0 && set.song_pat[next] != cur_idx)
            pattern_select(set.song_pat[next]);
        song.cued = 1;
    }
}

/* temporary save / reload [MANUAL 10.9.7] */
static void pattern_temp_save(void) { pat_snap = *pcur; snap_idx = cur_idx; }
static int pattern_temp_reload(void)
{
    if (next_idx >= 0) {                             /* (pnext holds the next pattern) [0.9.2] */
        app_say("PATTERN CHANGE", "WAIT FOR IT");
        return -1;
    }
    if (snap_idx != cur_idx) {                       /* no temporary save: from what is saved */
        pattern_load_into(cur_idx, pnext);
        pat_snap = *pnext;
    }
    /* the render reads pcur: copy the trigs and sounds field by field is not atomic; release the notes first */
    engine_all_off(-1);
    *pcur = pat_snap;
    mark_dirty();
    return 0;
}

static void store_boot(void)
{
    int i;
    for (i = 0; i < NDIRTY; i++)
        dirty[i].len = 0;
    next_idx = -1;                                   /* (a restore, the demo: nothing of before is pending) [0.9.2] */
    snap_idx = -1;
    copy_len = 0;
    shadow_idx = -1;
    undo_ok = 0;
    edit_pend = 0;
    {   /* the settings are append-only: an older, shorter record loads, its missing fields zero */
        int n, k;
        settings_defaults();
        for (k = 0; k < (int)sizeof set; k++)
            ((uint8_t *)&set)[k] = 0;
        n = plat_store_load(OBJ_SETTINGS, &set, sizeof set);
        if (n < 64 || set.magic != SET_MAGIC || set.version > SET_VERSION)
            settings_defaults();
    }
    if (set.cur >= NPAT)
        set.cur = 0;
    bank_load(OBJ_POOL, pool, 16);
    bank_load(OBJ_LIB0, lib, 16);
    bank_load(OBJ_LIB1, lib + 16, 16);
    cur_idx = (int8_t)set.cur;
    pattern_load_into(cur_idx, pcur);
    cur_dirty = 0;
    eng_pool = pool;
    eng_pat = pcur;
    eng.bpm = set.tempo_ptn ? (float)pcur->bpm10 * 0.1f : (float)set.bpm10 * 0.1f;
    eng.mute = set.mute;
    eng.rec_quant = set.quant;
    eng.ext_clock = set.clock_in;
    for (i = 0; i < NTRK; i++)
        eng_midi_chan[i] = set.midi_out[i];
    eng_midi_only = set.midi_only;
}

/* backup / restore hooks (base/device/backup.c) */
int app_backup_playing(void) { return eng.playing; }
void app_backup_flush(void) { store_flush(); }
void app_backup_hold(int on) { store_hold = (uint8_t)(on != 0); }
void app_backup_restored(void)
{
    engine_stop_hard();
    store_boot();
    demo_check();
}
