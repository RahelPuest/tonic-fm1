/* SPDX-License-Identifier: MPL-2.0
 * TONIC: a Digitone-style FM groovebox for the M-VAVE FM-1. The app: part of fm1-base's unity build
 * (base/device/unity.c, base/host/plat_host.c), after the platform (gfx.c, the panel, the store are in scope).
 * The engine (engine/, core/tables.c, core/params.c) is compiled apart at -O2. */
#include "fastmath.h"
#include "tables.h"
#include "eng_int.h"
#include "pattern.h"
#ifndef PLAT_HOST
#include <stdint.h>
#endif

/* the engine's buffers: HARM tables, chorus, reverb, delay (the delay takes what is left) */
#define ENGINE_POOL (210u * 1024u + (NVOICE - 8u) * 1028u)   /* (of the delay line: 12 KB went to undo, 6 KB to the
                                         * full-resolution screen, 2 KB to the drum kits' sounds; 0.11: each voice past
                                         * eight brings its HARM table's 1028 B, so the delay line stays as long) */
static uint8_t engine_pool[ENGINE_POOL] PLAT_POOL;

static void demo_check(void);
static int step_held_any(void);
static sound_t *tsnd(void);
static uint8_t ob_valid;                             /* draw.c: the screen as sent is valid */
static void mark_dirty(void);

#include "pattern.c"
#include "store.c"
#include "presets.c"
#include "items.c"
#include "ui.c"
#include "draw.c"
#include "midi.c"

#if defined(__has_include)
#if __has_include("tonic_bench.h")
#include "tonic_bench.h"                             /* tools/bench_build.sh: the CPU benchmark build */
#endif
#endif
#ifdef TONIC_BENCH
/* the heaviest normal load: four tracks of two held notes (8 voices; from 2 s, when tracks 2-4 loop and trig again,
 * all 12), 4-pole filters with resonance, drive,
 * base-width, HARM, feedback, LO-FI, both LFOs moving, every send, the DJ filter, ducking; the home screen shows the render's load */
static void bench_setup(void)
{
    int t;
    pattern_init(pcur);
    for (t = 0; t < NTRK; t++) {
        sound_t *s = &pcur->snd[t];
        s->p[P_ALGO] = (int16_t)(t * 2 + 1);
        s->p[P_ALEV] = 90; s->p[P_AEND] = 127; s->p[P_BLEV] = 90; s->p[P_BEND] = 127;
        s->p[P_FDBK] = 200; s->p[P_HARM] = (int16_t)(t & 1 ? 40 : -40); s->p[P_DTUN] = 200;
        s->p[P_FTYP] = 3; s->p[P_FREQ] = 300; s->p[P_RESO] = 200; s->p[P_DRV] = 100;
        s->p[P_BASE] = 10; s->p[P_WDTH] = 100; s->p[P_CHR] = 200; s->p[P_DEL] = 200; s->p[P_REV] = 200;
        s->p[P_L1DST] = D_FREQ; s->p[P_L1DEP] = 100; s->p[P_L2DST] = D_HARM; s->p[P_L2DEP] = 60;
        s->p[P_L1SPD] = 160; s->p[P_L2SPD] = 100;
        s->p[P_ESUS] = 127;
        s->p[P_SRR] = 40; s->p[P_BRR] = 40;          /* LO-FI on every voice */
        s->p[P_ITYP] = INS_FUZZ; s->p[P_IAMT] = 90; s->p[P_ITON] = 80;   /* (0.12) the insert, its tone, and the */
        s->p[P_MDST] = D_RESO; s->p[P_MDEP] = 400; s->p[P_MDEC] = 50;    /* modulation envelope on every voice */
#if TONIC_BENCH == 2                                 /* the VA engine at its heaviest instead */
        sound_set_engine(s, ENG_VA);
        s->p[V_WAV1] = VW_SQR; s->p[V_WAV2] = VW_SQR; s->p[V_PW] = 200; s->p[V_SYNC] = 1; s->p[V_XMOD] = 60;
        s->p[V_SUB] = 80; s->p[V_NOIS] = 40; s->p[V_DRFT] = 60; s->p[V_TUNE2] = 28;
        s->p[P_L2DST] = D_FDBK;
#elif TONIC_BENCH == 3                               /* the DRUM engine: cymbals (six squares, two filters) on every pad */
        sound_set_engine(s, ENG_DR);
        { int k; for (k = 0; k < NPAD; k++) s->s[S_DM1 + k] = DM_CYMB; }
        s->p[DR_DEC(1)] = 127; s->p[DR_DEC(3)] = 127;
        s->p[P_L2DST] = D_AATK;
#endif
        pcur->trig[t][0].flags = TF_NOTE;
        pcur->trig[t][0].note = (uint8_t)(TONIC_BENCH == 3 ? PAD_NOTE[0] : 48 + t * 7);
#if TONIC_BENCH == 3
        pcur->trig[t][0].chord[0] = (int8_t)(PAD_NOTE[2] - PAD_NOTE[0]);   /* pads 1 and 3: two voices a track */
        pcur->trig[t][0].nnote = 2;
        pcur->trig[t][0].len = LEN_INF;
        continue;
#endif
        pcur->trig[t][0].nnote = 2;
        pcur->trig[t][0].chord[0] = 7;
        pcur->trig[t][0].len = LEN_INF;
    }
    pcur->trk[0].len = 64;
    pcur->mlen = 64;
    pcur->fx[P_MFLT - P_CDEP] = -20;                 /* the DJ filter and ducking (from T1) on */
    pcur->fx[P_SCSRC - P_CDEP] = 1;
    tx_cpy(pcur->name, "CPU BENCH");
    engine_play();
}
#endif

void app_init(void)
{
    int k;
    for (k = 0; k < (int)sizeof ui; k++)              /* (the host's reboot keeps the RAM: start clean) */
        ((uint8_t *)&ui)[k] = 0;
    for (k = 0; k < NKEYS; k++)
        key_white[k] = key_black[k] = -1;
    for (k = 0; k < 16; k++)
        key_white[WK[k]] = (int8_t)k;
    for (k = 0; k < 11; k++)
        key_black[BK[k]] = (int8_t)k;
    cc_map_init();
    engine_init(engine_pool, ENGINE_POOL);
    store_boot();
    demo_check();
    eng_midi_out = midi_out_cb;
    eng_clock_cb = clock_out_cb;
    ui.grp = G_HOME;
    ui.row[G_ENV] = 1;                               /* FLTR: FREQ RESO TYPE ENV first */
    ui.roll_lo = -1;                                 /* the piano roll fits the notes */
    ui.help_btn = 0xFF;
#ifdef TONIC_BENCH
    bench_setup();
#endif
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12) { engine_render(lr, n, master_q12); }
void app_panic(void) { engine_panic(); }
void app_overload(void) { engine_shed(); }

void app_ui_init(void)
{
    touch_on(1);
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.redraw = 1;
}

void app_frame(void)
{
    ui_input();
    midi_poll();
    pattern_cue_poll();
    song_poll();
    store_poll();
    undo_poll();
    leds();
    ui_draw();
}

void app_input_only(void)
{
    ui_input();
    midi_poll();
}

#ifdef PLAT_HOST
/* the host simulator's own commands: sound N loads factory sound N on the active track */
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d)
{
    (void)b; (void)c; (void)d;
    if (!strcmp(cmd, "sound") && atoi(a) >= 0 && atoi(a) < NFACTORY) {
        preset_apply(tsnd(), &FACTORY[atoi(a)]);
        ui.redraw = 1;
        printf("  sound %d: %s\n", atoi(a), FACTORY[atoi(a)].name);
        return 1;
    }
    return 0;
}

/* the host simulator's checks: expect WHAT VALUE */
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "pattern")) *got = cur_idx;
    else if (!strcmp(what, "track")) *got = ui.trk;
    else if (!strcmp(what, "group")) *got = ui.grp;
    else if (!strcmp(what, "row")) *got = ui.row[ui.grp];
    else if (!strcmp(what, "rollzoom")) *got = ui.roll_zoom;
    else if (!strcmp(what, "help")) *got = ui.help_btn < NB && !ui.help_block && (plat_buttons() >> ui.help_btn & 1u) && plat_ms() - ui.btn_t[ui.help_btn] > 900u;
    else if (!strcmp(what, "rolllo")) *got = ui.roll_lo;
    else if (!strcmp(what, "grid")) *got = ui.grid;
    else if (!strcmp(what, "engine")) *got = tsnd()->s[S_ENGINE];
    else if (!strcmp(what, "soundi")) *got = ui.sound_i;
    else if (!strcmp(what, "plist")) *got = ui.plist_on;
    else if (!strcmp(what, "plisti")) *got = ui.plist_i;
    else if (!strcmp(what, "perf")) *got = ui.perf;
    else if (!strcmp(what, "theme")) *got = set.theme;
    else if (!strcmp(what, "perflock")) *got = ui.perf_lock;
    else if (!strcmp(what, "pfx")) *got = eng.pfx;
    else if (!strcmp(what, "playing")) *got = eng.playing;
    else if (!strcmp(what, "live")) *got = eng.rec_live;
    else if (!strcmp(what, "dirty")) *got = store_unsaved();
    else if (!strcmp(what, "voices")) *got = engine_voices_busy(-1);
    else if (!strcmp(what, "bpm10")) *got = (int)(eng.bpm * 10.0f + 0.5f);
    else if (!strcmp(what, "mute")) *got = eng.mute;
    else if (!strcmp(what, "fill")) *got = eng.fill;
    else if (!strcmp(what, "len")) *got = pcur->trk[ui.trk].len;
    else if (!strcmp(what, "tvoices")) *got = pcur->trk[ui.trk].voices;
    else if (!strcmp(what, "tscale")) *got = pcur->trk[ui.trk].scale;
    else if (!strcmp(what, "lenmode")) *got = pcur->lenmode;
    else if (!strcmp(what, "mlen")) *got = pcur->mlen;
    else if (!strcmp(what, "chlen")) *got = pcur->chlen;
    else if (!strncmp(what, "set", 3) && what[3] >= '0' && what[3] <= '9') *got = tsnd()->s[atoi(what + 3) % NSS];   /* setID */
    else if (!strcmp(what, "nlock")) { int i, n = 0; for (i = 0; i < pcur->nlock; i++) n += pcur->lock[i].f.param != P_NONE; *got = n; }
    else if (!strcmp(what, "ntrig")) { int s, n = 0; for (s = 0; s < NSTEP; s++) n += (pcur->trig[ui.trk][s].flags & (TF_NOTE | TF_LOCK)) != 0; *got = n; }
    else if (!strcmp(what, "steprec")) *got = ui.steprec;
    else if (!strcmp(what, "stepcur")) *got = ui.step_cur;
    else if (!strcmp(what, "spage")) *got = ui.spage;
    else if (!strcmp(what, "oct")) *got = ui.oct;
    else if (!strcmp(what, "transpose")) *got = pcur->trk[ui.trk].transpose;
    else if (!strcmp(what, "chain")) *got = set.chain_n;
    else if (!strncmp(what, "trig", 4)) *got = pcur->trig[ui.trk][atoi(what + 4) % NSTEP].flags & (TF_NOTE | TF_LOCK);
    else if (!strncmp(what, "note", 4)) *got = pcur->trig[ui.trk][atoi(what + 4) % NSTEP].note;
    else if (!strncmp(what, "micro", 5)) *got = pcur->trig[ui.trk][atoi(what + 5) % NSTEP].micro;
    else if (!strncmp(what, "nnote", 5)) *got = pcur->trig[ui.trk][atoi(what + 5) % NSTEP].nnote;
    else if (!strncmp(what, "cond", 4)) *got = pcur->trig[ui.trk][atoi(what + 4) % NSTEP].cond;
    else if (!strncmp(what, "vel", 3) && what[3] >= '0' && what[3] <= '9') *got = pcur->trig[ui.trk][atoi(what + 3) % NSTEP].vel;
    else if (!strncmp(what, "tp", 2) && what[2] >= '0' && what[2] <= '9')  /* tpTRACK_PARAM */
        *got = pcur->snd[(what[2] - '1') & 3].p[atoi(strchr(what, '_') ? strchr(what, '_') + 1 : "0") % NSP];
    else if (!strncmp(what, "tpar", 4)) *got = pcur->trk[ui.trk].tp[(atoi(what + 4) - NSP) % (NTP_END - NSP)];   /* tparID */
    else if (!strncmp(what, "fx", 2) && what[2] >= '0' && what[2] <= '9') *got = pcur->fx[(atoi(what + 2) - P_CDEP) % NFXP];   /* fxID */
    else if (!strncmp(what, "p", 1) && what[1] >= '0' && what[1] <= '9') *got = pcur->snd[ui.trk].p[atoi(what + 1) % NSP];
    else if (!strncmp(what, "lock", 4)) {           /* lockSTEP_PARAM */
        int16_t v = -1;
        int st = atoi(what + 4), pa = 0;
        const char *u = strchr(what, '_');
        if (u) pa = atoi(u + 1);
        *got = lock_get(pcur, ui.trk, st, pa, &v) ? v : -1;
    } else
        return 0;
    return 1;
}
#endif
