/* SPDX-License-Identifier: MPL-2.0
 * TONIC engine tests on the host: the engine units built natively (cc -O2 -ffp-contract=off, as the device's
 * floats), driven directly. Each test prints PASS / FAIL with its numbers; renders go to OUTDIR as WAV.
 *   eng_test OUTDIR [test ...]       (no test: all)
 * The CPU proxy counts retired instructions (macOS proc_pid_rusage), the measure Felucca's budget files use. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#include <unistd.h>
#endif
#include "eng_int.h"
#include "pattern.h"

static const char *outdir = ".";
static int fails;
static uint8_t pool[336 * 1024 + (NVOICE - 8) * 1028];   /* (as the app: the voices past eight bring their HARM tables) */
static pattern_t pat;
static sound_t spool[NPOOL];

/* the notes the engine plays, with their sample time (from the MIDI-out hook) */
typedef struct { uint32_t t; uint8_t st, d1, d2; } ev_t;
static ev_t evs[8192];
static int nev;
static uint32_t now_samples, t0_samples;
static void midi_hook(uint8_t st, uint8_t d1, uint8_t d2)
{
    if (nev < 8192) {
        evs[nev].t = eng_now * CR - t0_samples;
        evs[nev].st = st;
        evs[nev].d1 = d1;
        evs[nev].d2 = d2;
        nev++;
    }
}

static void check(int ok, const char *name, const char *fmt, ...)
{
    char b[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    printf("%s %-28s %s\n", ok ? "PASS" : "FAIL", name, b);
    if (!ok)
        fails++;
}

/* ------------------------------------------------------------------------ audio out --- */
static float *wav_l, *wav_r;
static uint32_t wav_n, wav_cap;
static void render(float seconds, int keep)
{
    int32_t blk[2 * 256];
    uint32_t n = (uint32_t)(seconds * SR), i;
    while (n) {
        uint32_t k = n > 256 ? 256 : n;
        engine_render(blk, 256, 4096);
        now_samples += 256;
        if (keep)
            for (i = 0; i < k && wav_n < wav_cap; i++) {
                wav_l[wav_n] = (float)blk[2 * i] / 8388607.0f;
                wav_r[wav_n] = (float)blk[2 * i + 1] / 8388607.0f;
                wav_n++;
            }
        n -= k;
    }
}
static void wav_start(float max_s)
{
    wav_cap = (uint32_t)(max_s * SR);
    wav_l = realloc(wav_l, wav_cap * sizeof(float));
    wav_r = realloc(wav_r, wav_cap * sizeof(float));
    wav_n = 0;
}
static void wav_save(const char *name)
{
    char path[512];
    FILE *f;
    uint32_t i, bytes = wav_n * 4;
    snprintf(path, sizeof path, "%s/%s.wav", outdir, name);
    f = fopen(path, "wb");
    if (!f)
        return;
    fwrite("RIFF", 1, 4, f);
    { uint32_t v = 36 + bytes; fwrite(&v, 4, 1, f); }
    fwrite("WAVEfmt ", 1, 8, f);
    { uint32_t v = 16; fwrite(&v, 4, 1, f); }
    { uint16_t v = 1; fwrite(&v, 2, 1, f); v = 2; fwrite(&v, 2, 1, f); }
    { uint32_t v = SR; fwrite(&v, 4, 1, f); v = SR * 4; fwrite(&v, 4, 1, f); }
    { uint16_t v = 4; fwrite(&v, 2, 1, f); v = 16; fwrite(&v, 2, 1, f); }
    fwrite("data", 1, 4, f);
    fwrite(&bytes, 4, 1, f);
    for (i = 0; i < wav_n; i++) {
        int16_t l = (int16_t)(fmaxf(-1, fminf(1, wav_l[i])) * 32767), r = (int16_t)(fmaxf(-1, fminf(1, wav_r[i])) * 32767);
        fwrite(&l, 2, 1, f);
        fwrite(&r, 2, 1, f);
    }
    fclose(f);
}

/* the fundamental of a stretch of the output: zero crossings (for pure-ish tones) */
static float pitch_of(uint32_t from, uint32_t len)
{
    uint32_t i, c = 0, first = 0, last = 0;
    for (i = from + 1; i < from + len && i < wav_n; i++)
        if (wav_l[i - 1] <= 0 && wav_l[i] > 0) {
            if (!c)
                first = i;
            last = i;
            c++;
        }
    return c > 1 ? (float)(c - 1) * SR / (float)(last - first) : 0;
}
static float rms_of(uint32_t from, uint32_t len)
{
    double s = 0;
    uint32_t i;
    for (i = from; i < from + len && i < wav_n; i++)
        s += wav_l[i] * wav_l[i];
    return (float)sqrt(s / len);
}
/* the share of energy above the fundamental's band: a brightness measure */
static float bright_of(uint32_t from, uint32_t len)
{
    double s = 0, d = 0;
    uint32_t i;
    for (i = from + 1; i < from + len && i < wav_n; i++) {
        float x = wav_l[i] - wav_l[i - 1];
        d += x * x;
        s += wav_l[i] * wav_l[i];
    }
    return s > 0 ? (float)sqrt(d / s) : 0;
}

static void reset(void)
{
    int i;
    engine_stop_hard();
    render(0.05f, 0);
    pattern_init(&pat);
    for (i = 0; i < NPOOL; i++)
        sound_init(&spool[i]), spool[i].name[0] = 0;
    eng_pool = spool;
    eng_pat = &pat;
    eng.bpm = 120.0f;
    eng.mute = 0;
    eng.fill = 0;
    nev = 0;
    t0_samples = eng_now * CR;
}

static void put(int t, int s, int note)
{
    trig_t *g = &pat.trig[t][s];
    g->flags = TF_NOTE;
    g->note = (uint8_t)note;
    g->nnote = 1;
    g->len = 0xFF;
}

/* --------------------------------------------------------------------------- tests --- */
static void t_pitch(void)
{
    float f;
    reset();
    wav_start(2);
    engine_note_on(0, 69, 100);
    render(0.5f, 1);
    f = pitch_of(SR / 10, SR / 4);
    check(fabsf(f - 440.0f) < 0.5f, "pitch A4 sine", "%.2f Hz", f);
    engine_note_off(0, 69);
    render(0.5f, 1);
    /* ratio C 2: an octave up */
    reset();
    pat.snd[0].p[P_RATC] = 4;
    wav_start(1);
    engine_note_on(0, 57, 100);
    render(0.4f, 1);
    f = pitch_of(SR / 10, SR / 4);
    check(fabsf(f - 440.0f) < 0.5f, "ratio C 2.00 on A3", "%.2f Hz", f);
    wav_save("pitch");
}

static void t_fm_levels(void)
{
    /* LEV A sweeps the brightness up, monotonically */
    float b[5];
    int i, mono = 1;
    char txt[128] = "";
    for (i = 0; i < 5; i++) {
        reset();
        pat.snd[0].p[P_ALEV] = (int16_t)(i * 32 > 127 ? 127 : i * 32);
        pat.snd[0].p[P_AEND] = 127;
        wav_start(1);
        engine_note_on(0, 48, 100);
        render(0.3f, 1);
        b[i] = bright_of(SR / 10, SR / 10);
        /* rising up to 96; at 127 the energy above the 20 kHz filter stops the measure growing */
        if (i && i < 4 && b[i] <= b[i - 1])
            mono = 0;
        if (i == 4 && b[i] < 0.9f * b[i - 1])
            mono = 0;
        snprintf(txt + strlen(txt), sizeof txt - strlen(txt), "%.4f ", b[i]);
    }
    check(mono, "LEV A brightness rises", "%s", txt);
}

static void t_algorithms(void)
{
    int a, ok = 1;
    char txt[160] = "";
    for (a = 0; a < 8; a++) {
        float r;
        reset();
        pat.snd[0].p[P_ALGO] = (int16_t)a;
        pat.snd[0].p[P_ALEV] = 80;
        pat.snd[0].p[P_BLEV] = 80;
        pat.snd[0].p[P_FDBK] = 120;
        wav_start(1);
        engine_note_on(0, 48, 100);
        render(0.3f, 1);
        r = rms_of(SR / 10, SR / 10);
        if (!(r > 0.01f && r < 1.0f) || isnan(r))
            ok = 0;
        snprintf(txt + strlen(txt), sizeof txt - strlen(txt), "%d:%.2f ", a + 1, r);
        if (a == 0) wav_save("algo1");
    }
    check(ok, "8 algorithms sound", "%s", txt);
}

/* trigs on every step at 120 BPM: a 16th is 5512.5 samples; micro timing moves a trig by 1/24 step */
static void t_timing(void)
{
    int i, ok = 1, n = 0;
    double maxerr = 0;
    reset();
    eng_midi_out = midi_hook;
    eng_midi_chan[0] = 0;
    for (i = 0; i < 16; i++)
        put(0, i, 60);
    pat.trig[0][4].micro = 12;               /* half a step late */
    pat.trig[0][8].micro = -23;
    engine_play();
    render(2.1f, 0);
    for (i = 0; i < nev && n < 16; i++)
        if ((evs[i].st & 0xF0) == 0x90) {
            double want = n * 5512.5 + (n == 4 ? 12 * 5512.5 / 24 : n == 8 ? -23 * 5512.5 / 24 : 0);
            double err = fabs((double)evs[i].t - want);
            if (err > maxerr)
                maxerr = err;
            if (err > CR + 1)
                ok = 0;
            n++;
        }
    check(ok && n == 16, "step + micro timing", "%d trigs, max error %.0f samples (%.2f ms)", n, maxerr, maxerr * 1000.0 / SR);
    eng_midi_chan[0] = 16;
}

static void t_swing(void)
{
    int i, n = 0;
    uint32_t t[4] = {0};
    reset();
    eng_midi_out = midi_hook;
    eng_midi_chan[0] = 0;
    pat.swing = 66;                          /* nearly triplet */
    for (i = 0; i < 4; i++)
        put(0, i, 60);
    engine_play();
    render(0.7f, 0);
    for (i = 0; i < nev && n < 4; i++)
        if ((evs[i].st & 0xF0) == 0x90)
            t[n++] = evs[i].t;
    {
        double off = (double)(t[1] - t[0]) / 5512.5, want = 66 * 0.48 / 24.0;
        check(n == 4 && fabs(off - want) < 0.02, "swing 66 %", "2nd 16th at %.3f steps (want %.3f)", off, want);
    }
    eng_midi_chan[0] = 16;
}

static void t_conditions(void)
{
    int i, plays = 0, fill = 0;
    reset();
    eng_midi_out = midi_hook;
    eng_midi_chan[0] = 0;
    pat.trk[0].len = 4;
    put(0, 0, 60);
    pat.trig[0][0].cond = C_AB0 + 6;          /* 2:4 */
    put(0, 2, 62);
    pat.trig[0][2].cond = C_FILL;
    engine_play();
    render(4 * 4 * 0.125f * 4 + 0.05f, 0);    /* 16 loops of 4 steps */
    for (i = 0; i < nev; i++)
        if ((evs[i].st & 0xF0) == 0x90) {
            if (evs[i].d1 == 60) {
                uint32_t loop = (uint32_t)(evs[i].t / (4 * 5512.5));
                if (loop % 4 != 1)
                    plays = -100;
                plays++;
            } else
                fill++;
        }
    check(plays == 4 && fill == 0, "condition 2:4, FILL off", "2:4 played %d of 16 loops, FILL trig %d", plays, fill);
    /* FILL on */
    nev = 0;
    eng.fill = 1;
    render(0.6f, 0);
    fill = 0;
    for (i = 0; i < nev; i++)
        fill += (evs[i].st & 0xF0) == 0x90 && evs[i].d1 == 62;
    check(fill >= 1, "condition FILL", "%d in fill mode", fill);
    eng.fill = 0;
    eng_midi_chan[0] = 16;
}

static void t_plock(void)
{
    /* a filter lock on step 2 only: the step is darker, the next unlocked step back to the base */
    float b0, b1, b2;
    reset();
    pat.snd[0].p[P_ALEV] = 100;
    pat.snd[0].p[P_AEND] = 127;
    pat.snd[0].p[P_EREL] = 0;
    pat.snd[0].p[P_FTYP] = 3;
    pat.snd[0].p[P_FREQ] = 127 * 4;
    put(0, 0, 48);
    put(0, 2, 48);
    put(0, 4, 48);
    lock_set(&pat, 0, 2, P_FREQ, 50 * 4);
    wav_start(2);
    engine_play();
    render(0.8f, 1);
    b0 = bright_of(1000, 3000);
    b1 = bright_of((uint32_t)(2 * 5512.5) + 1000, 3000);
    b2 = bright_of((uint32_t)(4 * 5512.5) + 1000, 3000);
    check(b1 < b0 * 0.6f && fabsf(b2 - b0) < b0 * 0.1f, "p-lock FREQ on one step", "bright %.3f / locked %.3f / after %.3f", b0, b1, b2);
    wav_save("plock");
}


/* [TONIC] retrig: a 1/16 note at 1/64 repeats 4 times, RVEL -128 fades them 100 75 50 25 */
static void t_retrig(void)
{
    int i, n = 0, v[8] = {0}, ok;
    uint32_t t[8] = {0};
    reset();
    eng_midi_out = midi_hook;
    eng_midi_chan[0] = 0;
    put(0, 0, 60);
    pat.trig[0][0].len = 14;                 /* 1/16 */
    pat.trig[0][0].vel = 100;
    pat.trk[0].tp[P_RTRG - NSP] = 1;
    pat.trk[0].tp[P_RTIM - NSP] = 15;        /* 1/64 */
    pat.trk[0].tp[P_RVEL - NSP] = -128;
    engine_play();
    render(0.12f, 0);
    for (i = 0; i < nev; i++)
        if ((evs[i].st & 0xF0) == 0x90 && n < 8) {
            t[n] = evs[i].t;
            v[n++] = evs[i].d2;
        }
    ok = n == 4 && v[0] == 100 && v[1] == 75 && v[2] == 50 && v[3] == 25;
    for (i = 1; i < n; i++)                  /* 1/64 at 120 BPM: 1378 samples apart */
        ok = ok && fabs((double)(t[i] - t[i - 1]) - 1378.1) < CR + 1;
    check(ok, "retrig 1/64, RVEL -128", "%d notes, vel %d %d %d %d, gap %u", n, v[0], v[1], v[2], v[3], n > 1 ? t[1] - t[0] : 0);
    eng_midi_chan[0] = 16;
}

/* [TONIC] LO-FI: SRR and BRR add the high partials of a held, filtered sine */
static float lofi_bright(int srr, int brr)
{
    reset();
    pat.snd[0].p[P_FTYP] = 0;
    pat.snd[0].p[P_SRR] = (int16_t)srr;
    pat.snd[0].p[P_BRR] = (int16_t)brr;
    put(0, 0, 57);
    pat.trig[0][0].len = LEN_INF;
    wav_start(1);
    engine_play();
    render(0.4f, 1);
    return bright_of(8000, 8000);
}
static void t_lofi(void)
{
    float b0 = lofi_bright(0, 0), b1 = lofi_bright(100, 0), b2 = lofi_bright(0, 110);
    check(b1 > b0 * 3.0f && b2 > b0 * 3.0f, "LO-FI SRR / BRR", "bright %.3f / SRR 100 %.3f / BRR 110 %.3f", b0, b1, b2);
}

static void dr_kit(sound_t *s);
/* [0.12] a kit pad's CTL A / B move every model they name (0: the model as before), its PAN pans it */
static uint32_t dr_hash_of(int md, int a, int b, int pan, float *l, float *r)
{
    uint32_t h = 2166136261u, i;
    reset();
    dr_kit(&pat.snd[0]);
    pat.snd[0].s[S_DM1] = (uint8_t)md;
    pat.snd[0].p[P_DA1] = (int16_t)a;
    pat.snd[0].p[P_DB1] = (int16_t)b;
    pat.snd[0].p[P_DPN1] = (int16_t)pan;
    put(0, 0, PAD_NOTE[0]);
    wav_start(1);
    engine_play();
    render(0.5f, 1);
    *l = *r = 0;
    for (i = 0; i < wav_n; i++) {
        uint32_t q = (uint32_t)(int32_t)(wav_l[i] * 1e6f);
        h = (h ^ q) * 16777619u;
        *l += wav_l[i] * wav_l[i];
        *r += wav_r[i] * wav_r[i];
    }
    return h;
}
static void t_drum_ctl(void)
{
    static const uint8_t NOB[] = {DM_CLAVE, DM_NOISE, DM_H606};   /* (no CTL B) */
    int md, bad = 0, nb, k;
    float l, r, l2, r2;
    for (md = 0; md < NDM; md++) {
        uint32_t h0 = dr_hash_of(md, 0, 0, 0, &l, &r), ha = dr_hash_of(md, 40, 0, 0, &l, &r), hb = dr_hash_of(md, 0, 40, 0, &l, &r);
        for (nb = 0, k = 0; k < 3; k++)
            nb |= NOB[k] == md;
        if (ha == h0 || (!nb && hb == h0)) {
            printf("     model %d: A %s, B %s\n", md, ha == h0 ? "no effect" : "ok", hb == h0 ? "no effect" : "ok");
            bad++;
        }
    }
    dr_hash_of(DM_SD808, 0, 0, -64, &l, &r);
    dr_hash_of(DM_SD808, 0, 0, 63, &l2, &r2);
    check(!bad && l > r * 4.0f && r2 > l2 * 4.0f, "DRUM CTL A / B, PAD PAN", "%d models without an effect; pan left %.1f:1, right %.1f:1",
          bad, l / (r > 1e-9f ? r : 1e-9f), r2 / (l2 > 1e-9f ? l2 : 1e-9f));
}

/* [0.12] the insert effect: every type changes a held note and stays finite; OFF the note as it was */
static float ins_run(int ty, int amt, int mix, uint32_t *hash)
{
    uint32_t h = 2166136261u, i;
    memset(vc, 0, sizeof vc);                        /* (a cold engine: each run from the same state) */
    engine_init(pool, sizeof pool);
    reset();
    pat.snd[0].p[P_ITYP] = (int16_t)ty;
    pat.snd[0].p[P_IAMT] = (int16_t)amt;
    pat.snd[0].p[P_IMIX] = (int16_t)mix;
    put(0, 0, 45);
    pat.trig[0][0].len = LEN_INF;
    wav_start(1);
    engine_play();
    render(0.4f, 1);
    for (i = 0; i < wav_n; i++)
        h = (h ^ (uint32_t)(int32_t)(wav_l[i] * 1e6f)) * 16777619u;
    *hash = h;
    for (i = 0; i < wav_n; i++)
        if (!isfinite(wav_l[i]) || fabsf(wav_l[i]) > 4.0f)
            return -1.0f;
    return bright_of(8000, 8000);
}
static void t_insert(void)
{
    uint32_t h0, h, hm;
    int ty, ok = 1;
    float b0 = ins_run(0, 64, 127, &h0), b;
    b0 = ins_run(0, 64, 127, &h0);                   /* (again: the first run after the others starts elsewhere) */
    ins_run(INS_OVER, 100, 0, &hm);                  /* MIX 0: dry */
    for (ty = 1; ty < NINS; ty++) {
        b = ins_run(ty, 110, 127, &h);
        if (b < 0.0f || h == h0)
            ok = 0;
        printf("     insert %d: brightness %.3f (off %.3f)\n", ty, b, b0);
    }
    check(ok && hm == h0, "insert effects", "every type moves the note and stays finite; MIX 0 the dry note");
}

/* [0.12] the modulation envelope: on VOL with DEP -64, ATK 0, DEC 60, SUS 0: the note starts down and comes up */
static void t_menv(void)
{
    float a, b, a0, b0;
    reset();
    put(0, 0, 57);
    pat.trig[0][0].len = LEN_INF;
    pat.snd[0].p[P_EATK] = 0;
    wav_start(2);
    engine_play();
    render(1.5f, 1);
    a0 = rms_of(2000, 2000);
    b0 = rms_of(60000, 2000);
    reset();
    put(0, 0, 57);
    pat.trig[0][0].len = LEN_INF;
    pat.snd[0].p[P_EATK] = 0;
    pat.snd[0].p[P_MDST] = D_VOL;
    pat.snd[0].p[P_MDEP] = -1024;
    pat.snd[0].p[P_MATK] = 0;
    pat.snd[0].p[P_MDEC] = 60;
    pat.snd[0].p[P_MSUS] = 0;
    pat.snd[0].p[P_MDEL] = 0;
    wav_start(2);
    engine_play();
    render(1.5f, 1);
    a = rms_of(2000, 2000);
    b = rms_of(60000, 2000);
    check(a < a0 * 0.5f && b > b0 * 0.8f, "modulation envelope", "start %.3f (without %.3f), later %.3f (%.3f)", a, a0, b, b0);
}

/* [0.12] the sound's scale: notes snap (below on a tie), chords by degrees */
static void t_scale(void)
{
    sound_t s;
    uint8_t o[NCHORD];
    int n, ok = 1;
    sound_init(&s);
    s.s[S_SCALE] = 1;                                /* MAJOR */
    s.s[S_SCROOT] = 0;                               /* C */
    ok = ok && scale_snap(&s, 61) == 60 && scale_snap(&s, 63) == 62 && scale_snap(&s, 64) == 64 && scale_snap(&s, 66) == 65;
    s.s[S_SCHD] = DCH_TRIAD;
    n = chord_notes(&s, 62, 0, 0, o);                /* D minor in C major */
    ok = ok && n == 3 && o[0] == 62 && o[1] == 65 && o[2] == 69;
    s.s[S_SCHD] = DCH_SEVEN;
    n = chord_notes(&s, 67, 0, 0, o);                /* G7 */
    ok = ok && n == 4 && o[0] == 67 && o[1] == 71 && o[2] == 74 && o[3] == 77;
    s.s[S_SCHD] = DCH_3RD;
    n = chord_notes(&s, 64, 0, 0, o);                /* E + its third: G */
    ok = ok && n == 2 && o[1] == 67;
    s.s[S_SCHD] = DCH_POWER;
    n = chord_notes(&s, 63, 0, 0, o);                /* D# snaps to D: D A D' */
    ok = ok && n == 3 && o[0] == 62 && o[1] == 69 && o[2] == 74;
    s.s[S_SCALE] = 2;                                /* A minor */
    s.s[S_SCROOT] = 9;
    s.s[S_SCHD] = DCH_TRIAD;
    n = chord_notes(&s, 57, 0, 0, o);
    ok = ok && n == 3 && o[0] == 57 && o[1] == 60 && o[2] == 64;
    s.s[S_SCALE] = 0;                                /* no scale: the named chords, as before */
    s.s[S_ARPCHD] = ACH_MAJ;
    n = chord_notes(&s, 61, 0, 0, o);
    ok = ok && n == 3 && o[0] == 61 && o[1] == 65 && o[2] == 68;
    s.s[S_ENGINE] = ENG_DR;                          /* a kit: no scale */
    s.s[S_SCALE] = 1;
    ok = ok && scale_snap(&s, 61) == 61;
    check(ok, "scale snap, chords by degrees", "C major: C# -> C, Dm, G7, E+3rd, D power; A minor Am; no scale MAJ; a kit as it is");
}

/* [0.9.2] an LFO on VOL in MODE FRE (the default) and HLD moves a held note (they used to stand still) */
static float lfo_swing(int mode)
{
    int k;
    float lo = 1e9f, hi = 0;
    reset();
    put(0, 0, 57);
    pat.trig[0][0].len = LEN_INF;
    pat.snd[0].p[P_EATK] = 0;
    pat.snd[0].p[P_L1DST] = D_VOL;
    pat.snd[0].p[P_L1MOD] = (int16_t)mode;
    pat.snd[0].p[P_L1SPD] = 64 * 4;
    pat.snd[0].p[P_L1MUL] = 8;
    pat.snd[0].p[P_L1DEP] = 1023;
    wav_start(2.5f);
    engine_play();
    render(2.0f, 1);
    for (k = 2; k < 20; k++) {
        float r = rms_of((uint32_t)(k * 0.1f * SR), 1000);
        lo = r < lo ? r : lo;
        hi = r > hi ? r : hi;
    }
    return hi / (lo > 1e-6f ? lo : 1e-6f);
}
static void t_lfo(void)
{
    float fre = lfo_swing(0), trg = lfo_swing(1);
    check(fre > 1.2f && trg > 1.2f, "LFO FRE / TRG move", "level swing x%.2f (FRE) / x%.2f (TRG)", fre, trg);
}

/* [TONIC] ducking: T1's trigs (a MIDI track: no sound of its own) duck T2's held note by DPTH, back with REL */
static void duck_run(int src, float *before, float *in, float *after)
{
    int s;
    reset();
    eng_midi_only = 1;
    for (s = 0; s < 16; s += 4)
        put(0, s, 36);
    put(1, 0, 57);
    pat.trig[1][0].len = LEN_INF;
    pat.snd[1].p[P_EATK] = 0;
    pat.fx[P_SCSRC - P_CDEP] = (int16_t)src;
    pat.fx[P_SCDEP - P_CDEP] = 127;
    pat.fx[P_SCREL - P_CDEP] = 40;           /* ~90 ms */
    wav_start(2.5f);
    engine_play();
    render(1.2f, 1);
    *before = rms_of((uint32_t)(4 * 5512.5) - 1500, 800);
    *in = rms_of((uint32_t)(4 * 5512.5) + 300, 400);
    *after = rms_of((uint32_t)(4 * 5512.5) + 16000, 800);
    eng_midi_only = 0;
}
/* [0.10] PUMP: a kit source ducks only on its PAD; the tempo's 1/4 pumps a held note with no source; KEEP spares it */
static float pump_dip(float at, float *base)
{
    float b = rms_of((uint32_t)(at * SR) - 1500, 800), d = rms_of((uint32_t)(at * SR) + 400, 400);
    if (base)
        *base = b;
    return d / (b > 1e-6f ? b : 1e-6f);
}
static void pump_run(int src, int pad, int keep)
{
    reset();
    eng_midi_only = 0;
    dr_kit(&pat.snd[0]);
    put(0, 4, PAD_NOTE[0]);                  /* pad 1 at 0.5 s, pad 2 at 1.5 s */
    put(0, 12, PAD_NOTE[1]);
    pat.snd[0].p[P_VOL] = 0;
    put(1, 0, 57);
    pat.trig[1][0].len = LEN_INF;
    pat.snd[1].p[P_EATK] = 0;
    pat.fx[P_SCSRC - P_CDEP] = (int16_t)src;
    pat.fx[P_SCPAD - P_CDEP] = (int16_t)pad;
    pat.fx[P_SCKEEP - P_CDEP] = (int16_t)keep;
    pat.fx[P_SCDEP - P_CDEP] = 127;
    pat.fx[P_SCREL - P_CDEP] = 40;
    wav_start(2.5f);
    engine_play();
    render(2.0f, 1);
}
static void t_pump(void)
{
    float p1, p2, q1, q2, k1;
    pump_run(1, 2, 0);                       /* T1 a kit, PAD 2 */
    p1 = pump_dip(0.5f, 0);
    p2 = pump_dip(1.5f, 0);
    pump_run(5, 0, 0);                       /* 1/4: a dip every 0.5 s at 120 BPM */
    q1 = pump_dip(1.0f, 0);
    q2 = pump_dip(1.5f, 0);
    pump_run(5, 0, 2);                       /* KEEP T2 */
    k1 = pump_dip(1.0f, 0);
    check(p1 > 0.8f && p2 < 0.25f && q1 < 0.25f && q2 < 0.25f && k1 > 0.8f, "PUMP pad / 1/4 / KEEP",
          "pad 1 %.2f, pad 2 %.2f; 1/4 %.2f %.2f; kept %.2f", p1, p2, q1, q2, k1);
}
static void t_duck(void)
{
    float b, i, a, b0, i0, a0;
    duck_run(1, &b, &i, &a);
    wav_save("duck");
    duck_run(0, &b0, &i0, &a0);
    check(i < b * 0.25f && a > b * 0.8f && fabsf(i0 / b0 - 1) < 0.1f, "ducking T1 > T2",
          "before %.3f, ducked %.3f, back %.3f; off: %.3f / %.3f", b, i, a, b0, i0);
}

/* [TONIC] the DJ filter: below 0 a lowpass (darker), above 0 a highpass (the fundamental goes) */
static void djf_run(int v, float *rms, float *br)
{
    reset();
    pat.snd[0].p[P_FTYP] = 0;
    pat.snd[0].p[P_ALEV] = 110;              /* a bright tone */
    pat.snd[0].p[P_AEND] = 127;
    put(0, 0, 45);
    pat.trig[0][0].len = LEN_INF;
    pat.fx[P_MFLT - P_CDEP] = (int16_t)v;
    wav_start(1);
    engine_play();
    render(0.5f, 1);
    *rms = rms_of(8000, 8000);
    *br = bright_of(8000, 8000);
}
static void t_djfilter(void)
{
    float r0, b0, r1, b1, r2, b2;
    djf_run(0, &r0, &b0);
    djf_run(-48, &r1, &b1);
    djf_run(48, &r2, &b2);
    check(b1 < b0 * 0.5f && r2 < r0 * 0.5f && b2 > b0, "DJ filter LP / HP",
          "off rms %.3f bright %.3f; -48 bright %.3f; +48 rms %.3f bright %.3f", r0, b0, b1, r2, b2);
}

/* [TONIC] the VA engine: a plain saw at A4, the sub an octave below, the pitch envelope, PW, the CPU at its worst */
#ifdef __APPLE__
static double measure(void);
#endif
static void va_sound(sound_t *s)
{
    sound_set_engine(s, ENG_VA);
    s->p[V_DRFT] = 0;
    s->p[V_DETN] = 0;
    s->p[P_FTYP] = 0;
    s->p[P_EATK] = 0;
    s->p[P_ESUS] = 127;
}
static float va_run(void (*set)(sound_t *), int note, float secs, uint32_t from, uint32_t len, float *rms)
{
    reset();
    va_sound(&pat.snd[0]);
    set(&pat.snd[0]);
    wav_start(secs + 0.2f);
    engine_note_on(0, note, 100);
    render(secs, 1);
    if (rms)
        *rms = rms_of(from, len);
    return pitch_of(from, len);
}
static void va_saw(sound_t *s) { s->p[V_WAV1] = VW_SAW; s->p[V_WAV2] = VW_OFF; s->p[V_MIX] = -64; }
static void va_sub(sound_t *s) { s->p[V_WAV1] = VW_OFF; s->p[V_WAV2] = VW_OFF; s->p[V_SUB] = 127; }
static void va_penv(sound_t *s) { va_saw(s); s->p[V_PENV] = 90; s->p[P_ADEC] = 30; s->p[P_AEND] = 0; }
static void va_off(sound_t *s) { s->p[V_WAV1] = VW_OFF; s->p[V_WAV2] = VW_OFF; }
static void t_va(void)
{
    {   /* every VA range inside its parameter's own, the same q; a VA sound survives pack / unpack */
        int p, ok = 1;
        static uint8_t b[512];
        sound_t a, c;
        for (p = 0; p < NSP; p++) {
            const pdesc_t *d = param_desc(ENG_VA, p);
            ok = ok && d->q == PDESC[p].q && d->lo >= PDESC[p].lo && d->hi <= PDESC[p].hi && d->def >= d->lo && d->def <= d->hi;
        }
        sound_init(&a);
        sound_set_engine(&a, ENG_VA);
        a.p[V_TUNE2] = -40;
        ok = ok && sound_unpack(&c, b, sound_pack(&a, b, sizeof b)) == 0 && c.s[S_ENGINE] == ENG_VA && c.p[V_TUNE2] == -40;
        check(ok, "VA ranges, saved form", "");
    }
    {   /* the sound banks' layouts: this build's, and the older ones (before LO-FI: 65 + 19; 0.1: 67 + 19) read back */
        static uint8_t raw[512];
        sound_t a, c;
        int ok = sound_raw_size(NSP, NSS) == (int)sizeof(sound_t), k;
        sound_init(&a);
        a.p[P_FREQ] = 300; a.s[S_ENGINE] = ENG_VA; a.mdst[1][2] = D_FREQ; a.mdep[1][2] = -20; a.arp_ofs[3] = 7;
        a.arp_on = 0x1234;
        memcpy(raw, &a, sizeof a);
        ok = ok && sound_from_raw(&c, raw, NSP, NSS) && !memcmp(&a, &c, sizeof a);
        for (k = 0; k < 2; k++) {            /* an old layout, written by hand */
            int nsp = k ? 67 : 65, nss = 19, o = 0, i;
            memset(raw, 0, sizeof raw);
            for (i = 0; i < nsp; i++) { raw[2 * i] = (uint8_t)a.p[i]; raw[2 * i + 1] = (uint8_t)(a.p[i] >> 8); }
            o = 2 * nsp;
            for (i = 0; i < nss; i++) raw[o + i] = a.s[i];
            raw[o + S_ENGINE] = 0;
            o += nss;
            memcpy(raw + o, a.mdst, 20); memcpy(raw + o + 20, a.mdep, 20); memcpy(raw + o + 40, a.arp_ofs, 16);
            o = (o + 56 + 1) & ~1;
            raw[o] = 0x34; raw[o + 1] = 0x12;
            memcpy(raw + o + 2, "OLD SOUND", 10);
            ok = ok && sound_from_raw(&c, raw, nsp, nss) && c.p[P_FREQ] == 300 && c.mdst[1][2] == D_FREQ &&
                 c.mdep[1][2] == -20 && c.arp_ofs[3] == 7 && c.arp_on == 0x1234 && !strcmp(c.name, "OLD SOUND") &&
                 c.s[S_ENGINE] == ENG_FM;
        }
        memset(raw, 0xA5, sizeof raw);       /* garbage: an empty slot, not a sound */
        ok = ok && !sound_from_raw(&c, raw, NSP, NSS) && !c.name[0];
        check(ok, "sound banks: old layouts", "raw %d B; 65+19 and 67+19 read back; garbage refused", (int)sizeof(sound_t));
    }
    float r0, rs, f1 = va_run(va_saw, 69, 0.4f, 4000, 8000, &r0), f2 = va_run(va_sub, 69, 0.4f, 4000, 8000, &rs);
    float early = va_run(va_penv, 57, 0.1f, 300, 1500, 0), late = va_run(va_penv, 57, 1.6f, 60000, 8000, 0), z;
    va_run(va_off, 69, 0.3f, 2000, 4000, &z);
    check(fabsf(f1 - 440.0f) < 1.0f && fabsf(f2 - 220.0f) < 1.0f && r0 > 0.02f && z < 1e-5f, "VA saw, sub, OFF",
          "saw %.2f Hz (rms %.3f), sub %.2f Hz, OFF rms %.6f", f1, r0, f2, z);
    check(early > 330.0f && fabsf(late - 220.0f) < 1.5f, "VA pitch envelope", "%.0f Hz at the start, %.1f Hz later",
          early, late);
    {   /* PolyBLEP: a saw at C8 keeps most of its energy below 12 kHz harmonics, no folded tone under the fundamental:
         * a crude check, the output's mean absolute slope stays near the ideal saw's */
        float f = va_run(va_saw, 108, 0.3f, 4000, 6000, 0);
        check(fabsf(f - 4186.0f) < 20.0f, "VA saw at C8", "%.0f Hz", f);
    }
}
/* ------------------------------------------------------------------ the DRUM engine --- */
static void dr_kit(sound_t *s) { sound_init(s); sound_set_engine(s, ENG_DR); }
static float peak_of(uint32_t from, uint32_t len)
{
    float p = 0;
    uint32_t i;
    for (i = from; i < from + len && i < wav_n; i++)
        p = fmaxf(p, fmaxf(fabsf(wav_l[i]), fabsf(wav_r[i])));
    return p;
}
/* one hit of model md on pad 1 (TUNE tune), rendered secs; peak, rms of the first 300 ms */
static void dr_hit(int md, int tune, float secs, float *pk, float *rms)
{
    reset();
    dr_kit(&pat.snd[0]);
    pat.snd[0].s[S_DM1] = (uint8_t)md;
    pat.snd[0].p[DRP[0][DK_TUN]] = (int16_t)tune;
    wav_start(secs + 0.1f);
    engine_note_on(0, PAD_NOTE[0], 127);
    render(0.01f, 1);
    engine_note_off(0, PAD_NOTE[0]);
    render(secs, 1);
    if (pk) *pk = peak_of(0, wav_n);
    if (rms) *rms = rms_of(0, (uint32_t)(0.3f * SR));
}
static void t_drum(void)
{
    {   /* every DRUM range inside its slot's own, q 1; a kit survives pack / unpack; the pads' notes */
        int p, ok = 1, i;
        static uint8_t b[512];
        sound_t a, c;
        for (p = 0; p < NSP; p++) {
            const pdesc_t *d = param_desc(ENG_DR, p);
            ok = ok && d->q == PDESC[p].q && d->lo >= PDESC[p].lo && d->hi <= PDESC[p].hi && d->def >= d->lo && d->def <= d->hi;
        }
        for (i = 0; i < NPAD; i++)
            ok = ok && pad_of(PAD_NOTE[i]) == i && pad_of(PAD_NOTE[i] + 12 * 0) == i;
        ok = ok && pad_of(61) == 0 && pad_of(74) == 0 && pad_of(48) == 1 && pad_of(0) >= 0;
        dr_kit(&a);
        a.s[S_DM3] = DM_ZAP;
        a.p[P_D7DEC] = 12;
        ok = ok && sound_unpack(&c, b, sound_pack(&a, b, sizeof b)) == 0 && c.s[S_ENGINE] == ENG_DR &&
             c.s[S_DM3] == DM_ZAP && c.p[P_D7DEC] == 12 && c.p[P_EREL] == 127;
        sound_set_engine(&a, ENG_FM);
        ok = ok && a.p[P_EREL] == PDESC[P_EREL].def && a.p[P_ADEC] == PDESC[P_ADEC].def && a.p[P_MIX] == 0;
        check(ok, "DRUM ranges, pads, saved form", "");
        {   /* the banks' compact form: 16 sounds fit an object; a kit and an FM sound come back whole */
            static uint8_t bb[512];
            sound_t f;
            int n = sound_bits_size(NSP, NSS);
            sound_init(&f);
            f.p[P_OFSC] = -999; f.p[P_HARM] = 104; f.p[P_L1DEP] = -1024; f.p[P_FREQ] = 300; f.mdst[4][3] = D_FREQ;
            f.mdep[4][3] = -77; f.arp_ofs[15] = -24; f.arp_on = 0xA5C3; memcpy(f.name, "FM BITS", 8);
            dr_kit(&a);
            a.p[P_D8TUN] = -64; a.p[P_D6LEV] = 127; a.s[S_DM8] = DM_NOISE; memcpy(a.name, "KIT BITS", 9);
            sound_to_bits(&a, bb);
            ok = 12 + 16 * n <= 3840 && sound_from_bits(&c, bb, NSP, NSS) && !memcmp(&a, &c, sizeof a);
            sound_to_bits(&f, bb);
            ok = ok && sound_from_bits(&c, bb, NSP, NSS) && !memcmp(&f, &c, sizeof f);
            memset(bb, 0xA5, sizeof bb);
            ok = ok && !sound_from_bits(&c, bb, NSP, NSS);
            check(ok, "sound banks: compact form", "%d B a sound, a bank %d B (an object: 3840)", n, 12 + 16 * n);
        }
    }
    {   /* every model sounds, does not clip, and dies away (the voice ends) */
        int md, ok = 1;
        char line[1024] = "";
        for (md = 0; md < NDM; md++) {
            float pk, rms;
            int busy;
            dr_hit(md, 0, 3.0f, &pk, &rms);
            busy = engine_voices_busy(-1);
            ok = ok && pk > 0.05f && pk < 0.99f && !busy;
            snprintf(line + strlen(line), sizeof line - strlen(line), "%s %.2f/%.3f%s ", DM_SHORT[md], pk, rms, busy ? "!" : "");
        }
        check(ok, "DRUM models (peak/rms)", "%d models", NDM);
        printf("     %s\n", line);
    }
    {   /* BD 808: its tail at 49 Hz; TUNE +24 (12 semitones) an octave up */
        float f1, f2;
        dr_hit(DM_BD808, 0, 0.6f, 0, 0);
        f1 = pitch_of((uint32_t)(0.25f * SR), (uint32_t)(0.3f * SR));
        dr_hit(DM_BD808, 24, 0.6f, 0, 0);
        f2 = pitch_of((uint32_t)(0.25f * SR), (uint32_t)(0.3f * SR));
        check(fabsf(f1 - 49.0f) < 1.5f && fabsf(f2 - 98.0f) < 2.5f, "DRUM BD tune", "%.1f Hz, TUNE +24 %.1f Hz", f1, f2);
    }
    {   /* a closed hat chokes the open one; a pad hit again takes its own voice; transpose leaves the pads */
        int a, b2, c2;
        reset();
        dr_kit(&pat.snd[0]);
        engine_note_on(0, PAD_NOTE[4], 100);         /* OH */
        render(0.05f, 0);
        engine_note_on(0, PAD_NOTE[3], 100);         /* CH */
        render(0.03f, 0);
        a = engine_voices_busy(0);
        engine_note_on(0, PAD_NOTE[0], 100);
        render(0.01f, 0);
        engine_note_on(0, PAD_NOTE[0], 100);
        render(0.01f, 0);
        engine_note_on(0, PAD_NOTE[0], 100);
        render(0.01f, 0);
        b2 = engine_voices_busy(0);
        reset();
        dr_kit(&pat.snd[0]);
        pat.trk[0].transpose = 5;
        put(0, 0, PAD_NOTE[1]);
        engine_play();
        render(0.05f, 0);
        c2 = -1;
        for (int i = 0; i < NVOICE; i++)
            if (vc[i].on)
                c2 = vc[i].dr_pad;
        engine_stop_hard();
        check(a == 1 && b2 == 2 && c2 == 1, "DRUM choke, one voice a pad", "after CH %d voice, BD x3 %d voices, "
              "transposed SD on pad %d", a, b2, c2 + 1);
    }
    {   /* the kit all at once, to listen to: drum_kit.wav */
        int k;
        reset();
        dr_kit(&pat.snd[0]);
        wav_start(0.3f * NDM + 0.5f);
        for (k = 0; k < NDM; k++) {
            pat.snd[0].s[S_DM1] = (uint8_t)k;
            engine_note_on(0, PAD_NOTE[0], 110);
            render(0.01f, 1);
            engine_note_off(0, PAD_NOTE[0]);
            render(0.27f, 1);
        }
        wav_save("drum_models");
    }
}
static void t_drum_cpu(void)
{
#ifdef __APPLE__
    int i;
    reset();
    for (i = 0; i < NTRK; i++) {             /* 8 cymbals (the heaviest model: six squares, two filters) + LP4, drive,
                                              * LFOs, sends */
        sound_t *s = &pat.snd[i];
        int k;
        dr_kit(s);
        for (k = 0; k < NPAD; k++)
            s->s[S_DM1 + k] = DM_CYMB;
        s->p[P_FTYP] = 3; s->p[P_FREQ] = 300; s->p[P_RESO] = 200; s->p[P_DRV] = 100; s->p[P_BASE] = 10;
        s->p[P_WDTH] = 100; s->p[P_CHR] = 200; s->p[P_DEL] = 200; s->p[P_REV] = 200; s->p[P_SRR] = 40;
        s->p[P_L1DST] = D_FREQ; s->p[P_L1DEP] = 100; s->p[P_L2DST] = D_AATK; s->p[P_L2DEP] = 60;
        s->p[DRP[0][DK_DEC]] = s->p[DRP[1][DK_DEC]] = 127;
    }
    for (i = 0; i < 8; i++)
        engine_note_on(i / 2, PAD_NOTE[i & 1], 100);
    {
        double ips = measure();
        check(ips < 2800.0, "DRUM CPU (8 cymbals)", "%.0f host instr / sample (FM worst case ~2450)", ips);
    }
#endif
}
static void t_va_cpu(void)
{
#ifdef __APPLE__
    int i;
    reset();
    for (i = 0; i < NTRK; i++) {             /* VA at its heaviest: both SQR (two BLEPs), sync, XMOD, sub, noise,
                                              * drift, LP4, drive, LFOs, sends (as the FM guard) */
        sound_t *s = &pat.snd[i];
        sound_set_engine(s, ENG_VA);
        s->p[V_WAV1] = VW_SQR; s->p[V_WAV2] = VW_SQR; s->p[V_PW] = 200; s->p[V_SYNC] = 1; s->p[V_XMOD] = 60;
        s->p[V_SUB] = 80; s->p[V_NOIS] = 40; s->p[V_DRFT] = 60; s->p[V_TUNE2] = 28;
        s->p[P_FTYP] = 3; s->p[P_FREQ] = 300; s->p[P_RESO] = 200; s->p[P_DRV] = 100; s->p[P_BASE] = 10;
        s->p[P_WDTH] = 100; s->p[P_CHR] = 200; s->p[P_DEL] = 200; s->p[P_REV] = 200;
        s->p[P_L1DST] = D_FREQ; s->p[P_L1DEP] = 100; s->p[P_L2DST] = D_FDBK; s->p[P_L2DEP] = 60; s->p[P_ESUS] = 127;
    }
    for (i = 0; i < 8; i++)
        engine_note_on(i / 2, 48 + i * 5, 100);
    {
        double ips = measure();
        check(ips < 2800.0, "VA CPU (8 heavy voices)", "%.0f host instr / sample (FM worst case ~2450)", ips);
    }
    reset();                                 /* a usual VA sound: two saws, LP4 */
    for (i = 0; i < NTRK; i++) {
        sound_t *s = &pat.snd[i];
        sound_set_engine(s, ENG_VA);
        s->p[P_FTYP] = 3; s->p[P_FREQ] = 300; s->p[P_RESO] = 200; s->p[P_ESUS] = 127;
    }
    for (i = 0; i < 8; i++)
        engine_note_on(i / 2, 48 + i * 5, 100);
    {
        double ips = measure();
        printf("     VA, two saws + LP4, 8 voices  %.0f instr/sample\n", ips);
    }
#endif
}

static void t_cpu(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 a, b;
    int i;
    double ips;
    reset();
    /* the heaviest normal load: 8 voices held, 4-pole filters, drive, all effects on */
    for (i = 0; i < NTRK; i++) {
        sound_t *s = &pat.snd[i];
        s->p[P_ALGO] = (int16_t)(i * 2);
        s->p[P_ALEV] = 90;
        s->p[P_BLEV] = 90;
        s->p[P_FDBK] = 200;
        s->p[P_HARM] = (int16_t)(i & 1 ? 40 : -40);
        s->p[P_FTYP] = 3;
        s->p[P_FREQ] = 300;
        s->p[P_RESO] = 200;
        s->p[P_DRV] = 100;
        s->p[P_BASE] = 10;
        s->p[P_WDTH] = 100;
        s->p[P_CHR] = 200;
        s->p[P_DEL] = 200;
        s->p[P_REV] = 200;
        s->p[P_L1DST] = D_FREQ;
        s->p[P_L1DEP] = 100;
        s->p[P_L2DST] = D_HARM;
        s->p[P_L2DEP] = 60;
    }
    for (i = 0; i < 8; i++)
        engine_note_on(i / 2, 48 + i * 5, 100);
    render(0.2f, 0);
    proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&a);
    render(2.0f, 0);
    proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&b);
    ips = (double)(b.ri_instructions - a.ri_instructions) / (2.0 * SR);
    /* a regression guard: the device binary at this load measured 45 % of a 240 MHz core in fm1-emu (2026-10-23,
     * docs/VALIDATION.md); this host figure was 2103 then */
    check(ips < 2650.0, "CPU regression guard", "%.0f host instr / sample (worst case; 2414 = 48 %% in the ISS)", ips);
    printf("     voices busy %d\n", engine_voices_busy(-1));
#endif
}

#ifdef __APPLE__
static double measure(void)
{
    struct rusage_info_v4 a, b;
    render(0.2f, 0);
    proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&a);
    render(1.0f, 0);
    proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&b);
    return (double)(b.ri_instructions - a.ri_instructions) / SR;
}
#endif
/* where the CPU goes: the same 8 voices with parts switched on one by one */
static void t_cpu_parts(void)
{
#ifdef __APPLE__
    int stage, i, t;
    static const char *const NAME[] = {"idle (FX only)", "8 voices, sine, no filter", "+ FM levels + fdbk + HARM",
                                       "+ LP4 filter", "+ drive", "+ base-width", "+ LFOs", "+ sends"};
    for (stage = 0; stage < 8; stage++) {
        reset();
        for (t = 0; t < NTRK; t++) {
            sound_t *s = &pat.snd[t];
            s->p[P_FTYP] = 0;
            s->p[P_AEND] = 127;
            s->p[P_BEND] = 127;
            if (stage >= 2) { s->p[P_ALEV] = 90; s->p[P_BLEV] = 90; s->p[P_FDBK] = 200; s->p[P_HARM] = (int16_t)(t & 1 ? 40 : -40); }
            if (stage >= 3) { s->p[P_FTYP] = 3; s->p[P_FREQ] = 300; s->p[P_RESO] = 200; }
            if (stage >= 4) s->p[P_DRV] = 100;
            if (stage >= 5) { s->p[P_BASE] = 10; s->p[P_WDTH] = 100; }
            if (stage >= 6) { s->p[P_L1DST] = D_FREQ; s->p[P_L1DEP] = 100; s->p[P_L2DST] = D_HARM; s->p[P_L2DEP] = 60; }
            if (stage >= 7) { s->p[P_CHR] = 200; s->p[P_DEL] = 200; s->p[P_REV] = 200; }
        }
        if (stage)
            for (i = 0; i < 8; i++)
                engine_note_on(i / 2, 48 + i * 5, 100);
        printf("     %-28s %6.0f instr/sample\n", NAME[stage], measure());
    }
#endif
}

static void t_pack(void)
{
    static uint8_t buf[8192];
    static pattern_t q;
    int n, ok;
    reset();
    put(0, 0, 60);
    put(1, 5, 67);
    pat.trig[1][5].nnote = 3;
    pat.trig[1][5].chord[0] = 4;
    pat.trig[1][5].chord[1] = 7;
    pat.trig[2][9].flags = TF_LOCK;
    lock_set(&pat, 2, 9, P_FREQ, 100);
    lock_set(&pat, 0, 0, P_SOUND, 3);
    pat.snd[3].p[P_ALGO] = 5;
    pat.bpm10 = 1337;
    n = pattern_pack(&pat, buf, sizeof buf);
    ok = n > 0 && pattern_unpack(&q, buf, n) == 0;
    ok = ok && q.trig[1][5].nnote == 3 && q.trig[1][5].chord[1] == 7 && q.snd[3].p[P_ALGO] == 5 && q.bpm10 == 1337;
    ok = ok && (q.trig[2][9].flags & TF_PLOCK) && (q.trig[0][0].flags & TF_SLOCK);
    {
        int16_t v = 0;
        ok = ok && lock_get(&q, 2, 9, P_FREQ, &v) && v == 100;
    }
    check(ok, "pattern pack / unpack", "%d bytes", n);
    {   /* a version 1 pattern (before LO-FI and retrig): its trig and FX locks move up to the new ids */
        reset();
        put(0, 0, 60);
        lock_set(&pat, 0, 0, P_FREQ, 100);
        lock_set(&pat, 0, 0, P_TLEN, 30);
        lock_set(&pat, 0, 0, P_DFB, 77);
        n = pattern_pack(&pat, buf, sizeof buf);
        buf[4] = 1;
        for (int k = 0; k < 3; k++) {        /* the locks: the last 3 x 4 bytes (step, param, value) */
            uint8_t *pp = &buf[n - 12 + 4 * k + 1];
            *pp = (uint8_t)(*pp == P_TLEN ? P_TLEN - 2 - NDRP - NSP12 : *pp == P_DFB ? P_DFB - 5 - NDRP - NSP12 : *pp);
        }
        {
            int16_t a = 0, b = 0, c = 0;
            ok = pattern_unpack(&q, buf, n) == 0 && lock_get(&q, 0, 0, P_FREQ, &a) && a == 100 &&
                 lock_get(&q, 0, 0, P_TLEN, &b) && b == 30 && lock_get(&q, 0, 0, P_DFB, &c) && c == 77;
        }
        check(ok, "version 1 locks migrate", "FREQ, LEN, DELAY FDBK");
        n = pattern_pack(&pat, buf, sizeof buf);   /* version 2 (before the drum pads 5-8): every id from P_D5TUN */
        buf[4] = 2;
        for (int k = 0; k < 3; k++) {
            uint8_t *pp = &buf[n - 12 + 4 * k + 1];
            *pp = (uint8_t)(*pp >= P_DA1 ? *pp - NDRP - NSP12 : *pp >= P_D5TUN ? *pp - NDRP : *pp);
        }
        {
            int16_t a = 0, b = 0, c = 0;
            ok = pattern_unpack(&q, buf, n) == 0 && lock_get(&q, 0, 0, P_FREQ, &a) && a == 100 &&
                 lock_get(&q, 0, 0, P_TLEN, &b) && b == 30 && lock_get(&q, 0, 0, P_DFB, &c) && c == 77;
        }
        check(ok, "version 2 locks migrate", "FREQ, LEN, DELAY FDBK");
        n = pattern_pack(&pat, buf, sizeof buf);   /* version 3 (before 0.12's sound parameters): every id from P_DA1 */
        buf[4] = 3;
        for (int k = 0; k < 3; k++) {
            uint8_t *pp = &buf[n - 12 + 4 * k + 1];
            *pp = (uint8_t)(*pp >= P_DA1 ? *pp - NSP12 : *pp);
        }
        {
            int16_t a = 0, b = 0, c = 0;
            ok = pattern_unpack(&q, buf, n) == 0 && lock_get(&q, 0, 0, P_FREQ, &a) && a == 100 &&
                 lock_get(&q, 0, 0, P_TLEN, &b) && b == 30 && lock_get(&q, 0, 0, P_DFB, &c) && c == 77;
        }
        check(ok, "version 3 locks migrate", "FREQ, LEN, DELAY FDBK");
    }
    pattern_init(&q);
    check(pattern_pack(&q, buf, sizeof buf) < 600, "empty pattern size", "%d bytes", pattern_pack(&q, buf, sizeof buf));
}

/* [FIRMWARE] the amp envelope's decay reaches -30 dB in the OS's time (Digitone OS 1.44, the amp decay table
 * 0x401a9238: DEC 32 0.506 s, DEC 64 2.756 s to -30 dB); the test checks the engine follows it */
static float decay_time(int dec)
{
    uint32_t w = SR / 100, i, pk = 0;
    float top = 0, lim;
    reset();
    pat.snd[0].p[P_EATK] = 0;
    pat.snd[0].p[P_EDEC] = (int16_t)dec;
    pat.snd[0].p[P_ESUS] = 0;
    pat.snd[0].p[P_FTYP] = 0;
    pat.snd[0].p[P_BASE] = 0;
    pat.snd[0].p[P_WDTH] = 127;
    pat.snd[0].p[P_ALEV] = 0;
    pat.snd[0].p[P_BLEV] = 0;
    pat.snd[0].p[P_CHR] = pat.snd[0].p[P_DEL] = pat.snd[0].p[P_REV] = 0;
    wav_start(8);
    engine_note_on(0, 69, 127);
    render(7.5f, 1);
    for (i = 0; i + w < wav_n; i += w) {
        float r = rms_of(i, w);
        if (r > top)
            top = r, pk = i;
    }
    lim = top * powf(10.0f, -30.0f / 20.0f);
    for (i = pk; i + w < wav_n; i += w)
        if (rms_of(i, w) <= lim)
            return (float)(i - pk) / SR;
    return 99.0f;
}
static void t_firmware(void)
{
    float a = decay_time(32), b = decay_time(64), f;
    check(fabsf(a / 0.506f - 1) < 0.05f && fabsf(b / 2.756f - 1) < 0.05f, "amp DEC to -30 dB vs the OS table",
          "DEC 32 %.3f s (OS 0.506), DEC 64 %.3f s (OS 2.756)", a, b);
    /* [FIRMWARE] an LFO moves its destination 2 x DEP units; pitch units are semitones: SQR, DEP 1.00 = +2 */
    reset();
    pat.snd[0].p[P_L1DST] = D_PITCH;
    pat.snd[0].p[P_L1WAV] = 2;
    pat.snd[0].p[P_L1MOD] = 1;
    pat.snd[0].p[P_L1SPD] = 4;                       /* SPD 1, MULT 1: a cycle every 512 beats */
    pat.snd[0].p[P_L1MUL] = 0;
    pat.snd[0].p[P_L1DEP] = 16;
    pat.snd[0].p[P_ESUS] = 127;
    wav_start(1);
    engine_note_on(0, 69, 100);
    render(0.5f, 1);
    f = pitch_of(SR / 5, SR / 4);
    check(fabsf(f - 493.88f) < 1.0f, "LFO DEP 1.00 on PITCH = 2 semitones", "%.2f Hz (want 493.88)", f);
}

/* ------------------------------------------------------------------- 0.6's additions --- */
static int arp_notes(int chord, int inv, int scale, int root, int note, uint8_t *out, int max)
{
    int i, n = 0;
    reset();
    eng_midi_out = midi_hook;
    eng_midi_chan[0] = 0;
    pat.snd[0].s[S_ARPMODE] = ARP_UP;
    pat.snd[0].s[S_ARPSPD] = 5;                      /* 1/16 */
    pat.snd[0].s[S_ARPCHD] = (uint8_t)chord;
    pat.snd[0].s[S_ARPINV] = (uint8_t)inv;
    pat.trk[0].kscale = (uint8_t)scale;
    pat.trk[0].kroot = (uint8_t)root;
    engine_play();
    engine_note_on(0, note, 100);
    render(1.2f, 0);
    engine_note_off(0, note);
    engine_stop_hard();
    for (i = 0; i < nev && n < max; i++)
        if ((evs[i].st & 0xF0) == 0x90 && evs[i].d2)
            out[n++] = evs[i].d1;
    eng_midi_chan[0] = 16;
    return n;
}
static void heavy_fm(sound_t *s, int i);
static void t_new06_cpu(void)
{
#ifdef __APPLE__
    double a, b, c, d;
    int i;
    reset();
    for (i = 0; i < NTRK; i++)
        heavy_fm(&pat.snd[i], i);
    for (i = 0; i < 8; i++)
        engine_note_on(i / 2, 48 + i * 5, 100);
    a = measure();
    pat.fx[P_CPAMT - P_CDEP] = 100;
    b = measure();
    reset();
    for (i = 0; i < NTRK; i++) {
        va_sound(&pat.snd[i]);
        pat.snd[i].p[V_WAV2] = VW_SAW;
    }
    for (i = 0; i < 8; i++)
        engine_note_on(i / 2, 48 + i * 5, 100);
    c = measure();
    for (i = 0; i < NTRK; i++) {
        pat.snd[i].p[V_FOLD] = 40;
        pat.snd[i].p[V_RING] = 40;
    }
    engine_sound_changed(0); engine_sound_changed(1); engine_sound_changed(2); engine_sound_changed(3);
    d = measure();
    printf("     compressor on: %+.0f host instr / sample (FM worst case %.0f); VA FOLD + RING on 8 voices: %+.0f (%.0f)\n",
           b - a, a, d - c, c);
#endif
}
static void t_new06(void)
{
    {   /* the arpeggiator's chords: MAJ7 on C4, first inversion (E G B C); SCALE 3 of C minor on D: D F Ab;
         * OFF: the note alone */
        uint8_t a[16], b[16], c[16];
        int na = arp_notes(ACH_MAJ7, 1, 0, 0, 60, a, 8), nb = arp_notes(ACH_SCL3, 0, 2, 0, 62, b, 6);
        int nc = arp_notes(ACH_OFF, 0, 0, 0, 60, c, 3), ok;
        ok = na == 8 && a[0] == 64 && a[1] == 67 && a[2] == 71 && a[3] == 72 && a[4] == 64;
        ok = ok && nb == 6 && b[0] == 62 && b[1] == 65 && b[2] == 68 && b[3] == 62;
        ok = ok && nc == 3 && c[0] == 60 && c[1] == 60;
        check(ok, "arp chords, inversion, scale", "MAJ7/1: %d %d %d %d; C minor on D: %d %d %d", a[0], a[1], a[2], a[3],
              b[0], b[1], b[2]);
    }
    {   /* the compressor: a loud chord, AMT 127 reduces its peaks (after make-up); AMT 0: as before */
        float p0, p1, gr;
        int i, k;
        for (k = 0; k < 2; k++) {
            reset();
            for (i = 0; i < 4; i++) {
                pat.snd[i].p[P_ALEV] = 60;
                pat.snd[i].p[P_VOL] = 508;
            }
            pat.fx[P_CPAMT - P_CDEP] = (int16_t)(k ? 127 : 0);
            pat.fx[P_CPATK - P_CDEP] = 0;
            wav_start(1.2f);
            for (i = 0; i < 8; i++)
                engine_note_on(i / 2, 36 + i * 5, 127);
            render(1.0f, 1);
            if (k) { p1 = peak_of((uint32_t)(0.3f * SR), (uint32_t)(0.6f * SR)); gr = engine_comp_gr(); }
            else p0 = peak_of((uint32_t)(0.3f * SR), (uint32_t)(0.6f * SR));
        }
        check(p1 < p0 * 0.8f && gr > 3.0f, "compressor", "peak %.3f -> %.3f, gain reduction %.1f dB", p0, p1, gr);
    }
    {   /* VA FOLD brightens a sine; RING adds the sum and difference of two sines (440 x 660: 220 and 1100 Hz) */
        float b0, b1, r0, r1;
        reset(); va_sound(&pat.snd[0]); pat.snd[0].p[V_WAV1] = VW_SIN; pat.snd[0].p[V_WAV2] = VW_OFF; pat.snd[0].p[V_MIX] = -64;
        wav_start(0.6f); engine_note_on(0, 69, 100); render(0.5f, 1); b0 = bright_of(4000, 8000);
        reset(); va_sound(&pat.snd[0]); pat.snd[0].p[V_WAV1] = VW_SIN; pat.snd[0].p[V_WAV2] = VW_OFF; pat.snd[0].p[V_MIX] = -64;
        pat.snd[0].p[V_FOLD] = 50;
        wav_start(0.6f); engine_note_on(0, 69, 100); render(0.5f, 1); b1 = bright_of(4000, 8000);
        reset(); va_sound(&pat.snd[0]); pat.snd[0].p[V_WAV1] = VW_SIN; pat.snd[0].p[V_WAV2] = VW_SIN; pat.snd[0].p[V_MIX] = -64;
        pat.snd[0].p[V_TUNE2] = 28;                  /* +7 semitones (quarter steps) */
        wav_start(0.6f); engine_note_on(0, 69, 100); render(0.5f, 1); r0 = pitch_of(4000, 16000);
        reset(); va_sound(&pat.snd[0]); pat.snd[0].p[V_WAV1] = VW_SIN; pat.snd[0].p[V_WAV2] = VW_SIN; pat.snd[0].p[V_MIX] = -64;
        pat.snd[0].p[V_TUNE2] = 28; pat.snd[0].p[V_RING] = 63;
        wav_start(0.6f); engine_note_on(0, 69, 100); render(0.5f, 1); r1 = rms_of(4000, 16000);
        check(b1 > b0 * 2.0f && r0 > 430.0f && r1 > 0.0f, "VA FOLD, RING", "brightness %.3f -> %.3f; ring rms %.3f",
              b0, b1, r1);
    }
}

/* ------------------------------------------------------------- 0.7: momentary effects --- */
static int notes_between(uint32_t t0, uint32_t t1, uint8_t *out, int max)
{
    int i, n = 0;
    for (i = 0; i < nev && n < max; i++)
        if ((evs[i].st & 0xF0) == 0x90 && evs[i].d2 && evs[i].t >= t0 && evs[i].t < t1)
            out[n++] = evs[i].d1;
    return n;
}
static void t_pfx(void)
{
    const uint32_t S = 5512;                         /* a step at 120 BPM, samples (5512.5) */
    {   /* ROLL 1/8 held from step 4.5 to 8.5: steps 4 5 4 5 ...; let go, the pattern goes on in time (step 9 at its
         * own time); without ROLL as before */
        uint8_t a[32], b[8];
        int i, na, nb, ok = 1;
        reset();
        eng_midi_out = midi_hook;
        eng_midi_chan[0] = 0;
        for (i = 0; i < 16; i++)
            put(0, i, 40 + i);
        engine_play();
        render(4.5f * S / SR, 0);
        eng.pfx = 1u << PFX_ROLL8;
        render(4.0f * S / SR, 0);
        eng.pfx = 0;
        render(4.0f * S / SR, 0);
        engine_stop_hard();
        na = notes_between((uint32_t)(4.5f * S), (uint32_t)(8.5f * S), a, 32);
        nb = notes_between((uint32_t)(8.5f * S), (uint32_t)(10.5f * S), b, 8);
        ok = na == 4 && a[0] == 45 && a[1] == 44 && a[2] == 45 && a[3] == 44 && nb == 2 && b[0] == 49 && b[1] == 50;
        (void)i;
        check(ok, "ROLL 1/8, then in time", "rolled %d: %d %d %d %d; after: %d %d", na, a[0], a[1], a[2], a[3], b[0], b[1]);
        eng_midi_chan[0] = 16;
    }
    {   /* HALF: half the notes in the same time */
        uint8_t a[32];
        int i, n0, n1;
        for (i = 0; i < 2; i++) {
            int k;
            reset();
            eng_midi_out = midi_hook;
            eng_midi_chan[0] = 0;
            for (k = 0; k < 16; k++)
                put(0, k, 60);
            eng.pfx = i ? 1u << PFX_HALF : 0;
            engine_play();
            render(8.0f * S / SR, 0);
            engine_stop_hard();
            eng.pfx = 0;
            if (i) n1 = notes_between(0, 8 * S, a, 32); else n0 = notes_between(0, 8 * S, a, 32);
        }
        render(0.05f, 0);
        check(n0 == 8 && n1 == 4, "HALF", "%d notes in 8 steps, held: %d", n0, n1);
        eng_midi_chan[0] = 16;
    }
    {   /* TAPE: a held note slows to silence, the pitch far down; let go, it comes back to its pitch */
        float r0, r1, r2, f2;
        reset();
        wav_start(3.0f);
        engine_note_on(0, 69, 100);
        render(0.3f, 1);
        r0 = rms_of((uint32_t)(0.1f * SR), (uint32_t)(0.2f * SR));
        eng.pfx = 1u << PFX_TAPE;
        render(1.5f, 1);
        r1 = rms_of((uint32_t)(1.6f * SR), (uint32_t)(0.2f * SR));
        eng.pfx = 0;
        render(0.8f, 1);
        r2 = rms_of((uint32_t)(2.3f * SR), (uint32_t)(0.2f * SR));
        f2 = pitch_of((uint32_t)(2.3f * SR), (uint32_t)(0.2f * SR));
        check(r1 < r0 * 0.01f && r2 > r0 * 0.7f && fabsf(f2 - 440.0f) < 2.0f, "TAPE stop and start",
              "rms %.3f -> %.5f -> %.3f, %.1f Hz after", r0, r1, r2, f2);
        engine_note_off(0, 69);
    }
    {   /* LP: a bright note darkens while held, back when let go; FREEZE: the reverb's tail stays after the notes */
        float b0, b1, b2, t0, t1;
        int k;
        reset();
        pat.snd[0].p[P_ALEV] = 100;
        wav_start(2.5f);
        engine_note_on(0, 57, 100);
        render(0.3f, 1);
        b0 = bright_of((uint32_t)(0.1f * SR), 8000);
        eng.pfx = 1u << PFX_LP;
        render(1.6f, 1);
        b1 = bright_of((uint32_t)(1.6f * SR), 8000);
        eng.pfx = 0;
        render(0.3f, 1);
        b2 = bright_of((uint32_t)(2.05f * SR), 8000);
        engine_note_off(0, 57);
        check(b1 < b0 * 0.5f && b2 > b0 * 0.8f, "LP SWEEP", "brightness %.3f -> %.3f -> %.3f", b0, b1, b2);
        for (k = 0; k < 2; k++) {
            reset();
            fx_clear();
            pat.snd[0].p[P_REV] = 508;
            pat.fx[P_RDEC - P_CDEP] = 40;
            wav_start(4.0f);
            engine_note_on(0, 60, 110);
            render(0.5f, 1);
            if (k) eng.pfx = 1u << PFX_FREEZE;
            engine_note_off(0, 60);
            render(3.0f, 1);
            if (k) t1 = rms_of((uint32_t)(3.0f * SR), (uint32_t)(0.4f * SR));
            else t0 = rms_of((uint32_t)(3.0f * SR), (uint32_t)(0.4f * SR));
            eng.pfx = 0;
        }
        check(t1 > t0 * 20.0f && t1 > 0.001f, "FREEZE", "the tail after 2.5 s: %.5f, frozen %.5f", t0, t1);
    }
}

/* ------------------------------------------------------------------ 0.8: chords --- */
static void t_chord08(void)
{
    {   /* chord_notes: types, voicings, BASS */
        sound_t c;
        uint8_t o[NCHORD];
        int n, ok = 1;
        sound_init(&c);
        c.s[S_ARPCHD] = ACH_MAJ9;
        n = chord_notes(&c, 60, 0, 0, o);
        ok = ok && n == 5 && o[4] == 74;
        c.s[S_ARPCHD] = ACH_MAJ7; c.s[S_CHVOIC] = CV_DROP2;            /* C E G B: G down -> G3 C E B */
        n = chord_notes(&c, 60, 0, 0, o);
        ok = ok && n == 4 && o[0] == 55 && o[1] == 60 && o[2] == 64 && o[3] == 71;
        c.s[S_CHVOIC] = CV_SPREAD; c.s[S_CHBASS] = 1;                 /* C (E+12) G (B+12), C2 below */
        n = chord_notes(&c, 60, 0, 0, o);
        ok = ok && n == 5 && o[0] == 48 && o[1] == 60 && o[2] == 67 && o[3] == 76 && o[4] == 83;
        c.s[S_CHVOIC] = CV_OPEN; c.s[S_CHBASS] = 0; c.s[S_ARPCHD] = ACH_MAJ;   /* C (E+12) G -> C G E5 */
        n = chord_notes(&c, 60, 0, 0, o);
        ok = ok && n == 3 && o[0] == 60 && o[1] == 67 && o[2] == 76;
        check(ok, "chords: types, voicings, BASS", "");
    }
    {   /* a key plays the chord without the arp; letting go ends all of it; STRUM spreads the starts */
        int i, nv, on0 = 0, nst = 0;
        uint32_t ts_[8];
        reset();
        eng_midi_out = midi_hook;
        eng_midi_chan[0] = 0;
        pat.snd[0].s[S_ARPCHD] = ACH_MIN7;
        engine_note_on(0, 57, 100);
        render(0.05f, 0);
        nv = engine_voices_busy(0);
        engine_note_off(0, 57);
        render(1.5f, 0);
        on0 = engine_voices_busy(0);
        pat.snd[0].s[S_CHSTRM] = 50;                 /* 40 ms a note */
        nev = 0;
        t0_samples = eng_now * CR;
        engine_note_on(0, 57, 100);
        render(0.3f, 0);
        engine_note_off(0, 57);
        render(1.5f, 0);
        for (i = 0; i < nev && nst < 8; i++)
            if ((evs[i].st & 0xF0) == 0x90 && evs[i].d2)
                ts_[nst++] = evs[i].t;
        /* (MIDI goes out at once; the voices wait: count the voices sounding over time) */
        check(nv == 4 && on0 == 0 && nst == 4, "chord without arp", "4 notes held: %d voices; after release %d", nv, on0);
        eng_midi_chan[0] = 16;
    }
    {   /* STRUM: the voices start one by one (the 4th 120 ms after the 1st) */
        int a, b;
        reset();
        pat.snd[0].s[S_ARPCHD] = ACH_MIN7;
        pat.snd[0].s[S_CHSTRM] = 50;
        pat.snd[0].p[P_EATK] = 0;
        engine_note_on(0, 57, 100);
        render(0.06f, 0);
        a = 0;
        for (int i = 0; i < NVOICE; i++) a += vc[i].on && !vc[i].wait;
        render(0.1f, 0);
        b = 0;
        for (int i = 0; i < NVOICE; i++) b += vc[i].on && !vc[i].wait;
        engine_note_off(0, 57);
        render(1.0f, 0);
        check(a == 2 && b == 4, "STRUM", "sounding after 60 ms: %d, after 160 ms: %d", a, b);
    }
    {   /* a trig plays the chord; the arp's CHORD mode plays it on each step; RANDOM picks among the notes */
        uint8_t o[32];
        int n1, n2, n3, i, distinct = 0, seen[128] = {0};
        reset();
        eng_midi_out = midi_hook;
        eng_midi_chan[0] = 0;
        pat.snd[0].s[S_ARPCHD] = ACH_MAJ;
        put(0, 0, 60);
        engine_play();
        render(0.1f, 0);
        engine_stop_hard();
        n1 = notes_between(0, 4000, o, 32);
        n2 = arp_notes(ACH_MAJ, 0, 0, 0, 60, o, 32);                /* UP: one note a 16th (1.2 s: ~9) */
        reset();
        eng_midi_out = midi_hook;
        eng_midi_chan[0] = 0;
        pat.snd[0].s[S_ARPMODE] = ARP_CHORD;
        pat.snd[0].s[S_ARPSPD] = 5;
        pat.snd[0].s[S_ARPCHD] = ACH_MAJ;
        engine_play();
        engine_note_on(0, 60, 100);
        render(0.6f, 0);
        engine_note_off(0, 60);
        engine_stop_hard();
        n3 = notes_between(0, (uint32_t)(0.6f * SR), o, 32);
        reset();
        eng_midi_out = midi_hook;
        eng_midi_chan[0] = 0;
        pat.snd[0].s[S_ARPMODE] = ARP_RANDOM;
        pat.snd[0].s[S_ARPSPD] = 5;
        pat.snd[0].s[S_ARPCHD] = ACH_MAJ7;
        engine_play();
        engine_note_on(0, 60, 100);
        render(2.0f, 0);
        engine_note_off(0, 60);
        engine_stop_hard();
        {
            int nr = notes_between(0, 2 * SR, o, 32);
            for (i = 0; i < nr; i++)
                if (!seen[o[i]]++) distinct++;
        }
        check(n1 == 3 && n3 >= 12 && n3 % 3 == 0 && distinct == 4, "chord trig, arp CHORD, RANDOM",
              "trig: %d notes; CHORD mode: %d in 0.6 s; RANDOM: %d distinct of 4", n1, n3, distinct);
        (void)n2;
        eng_midi_chan[0] = 16;
    }
}

/* --------------------------------------------------- 0.9: more modulation destinations --- */
static float dr_pad_pitch(int pad, int dst, float dep)
{
    reset();
    dr_kit(&pat.snd[0]);
    pat.snd[0].s[S_DM1 + pad] = DM_BD808;
    pat.snd[0].p[DRP[pad][DK_DEC]] = 127;
    pat.snd[0].p[P_L1DST] = (int16_t)dst;
    pat.snd[0].p[P_L1DEP] = (int16_t)(dep * 16.0f);
    pat.snd[0].p[P_L1WAV] = 2;                       /* SQR: a fixed offset for half a cycle */
    pat.snd[0].p[P_L1SPD] = 4;                       /* (slow) */
    pat.snd[0].p[P_L1MOD] = 1;
    wav_start(1.0f);
    engine_note_on(0, PAD_NOTE[pad], 100);
    render(0.8f, 1);
    engine_note_off(0, PAD_NOTE[pad]);
    return pitch_of((uint32_t)(0.4f * SR), (uint32_t)(0.3f * SR));
}
static void t_mod09(void)
{
    {   /* an LFO on pad 6's TUNE, on ALL TUNE; pad 6 untouched by pad 5's destination */
        float f0 = dr_pad_pitch(5, D_NONE, 0), f1 = dr_pad_pitch(5, D_P6TUN, 24), f2 = dr_pad_pitch(5, D_KTUN, 24),
              f3 = dr_pad_pitch(5, D_P5TUN, 24);
        check(f1 > f0 * 1.2f && f2 > f0 * 1.2f && fabsf(f3 - f0) < 1.0f, "LFO on pads 5-8, ALL TUNE",
              "pad 6: %.1f Hz -> %.1f (6 TUNE), %.1f (ALL TUNE), %.1f (5 TUNE)", f0, f1, f2, f3);
    }
    {   /* an LFO on SRR brightens a sine (LO-FI from 0) */
        float b0, b1;
        reset();
        wav_start(0.6f);
        engine_note_on(0, 57, 100);
        render(0.5f, 1);
        b0 = bright_of(8000, 8000);
        engine_note_off(0, 57);
        reset();
        pat.snd[0].p[P_L1DST] = D_SRR;
        pat.snd[0].p[P_L1DEP] = 60 * 16;
        pat.snd[0].p[P_L1WAV] = 2;
        pat.snd[0].p[P_L1SPD] = 4;
        pat.snd[0].p[P_L1MOD] = 1;
        wav_start(0.6f);
        engine_note_on(0, 57, 100);
        render(0.5f, 1);
        b1 = bright_of(8000, 8000);
        engine_note_off(0, 57);
        check(b1 > b0 * 2.0f, "LFO on SRR", "brightness %.3f -> %.3f", b0, b1);
    }
}

/* ---------------------------------------------------------------- the golden renders --- */
/* scenes rendered sample for sample and hashed (FNV-1a over the int32 output): an optimization must leave every
 * hash as it was. `eng_test OUT golden` prints them; GOLDEN=file compares with a file of earlier ones */
static uint32_t ghash;
static int32_t gpeak;
static FILE *gdump;          /* GDUMP=file: the scenes' output, raw */
static void grender(float secs)
{
    int32_t blk[2 * 256];
    uint32_t n = (uint32_t)(secs * SR) / 256, i, k;
    for (i = 0; i < n; i++) {
        engine_render(blk, 256, 4096);
        if (gdump)
            fwrite(blk, 4, 512, gdump);
        for (k = 0; k < 512; k++) {
            uint32_t v = (uint32_t)blk[k];
            int b;
            if (blk[k] > gpeak) gpeak = blk[k];
            for (b = 0; b < 4; b++) {
                ghash ^= (v >> (8 * b)) & 0xFFu;
                ghash *= 16777619u;
            }
        }
    }
}
static void heavy_fm(sound_t *s, int i)
{
    s->p[P_ALGO] = (int16_t)(i * 2); s->p[P_ALEV] = 90; s->p[P_BLEV] = 90; s->p[P_FDBK] = 200;
    s->p[P_HARM] = (int16_t)(i & 1 ? 40 : -40); s->p[P_FTYP] = 3; s->p[P_FREQ] = 300; s->p[P_RESO] = 200;
    s->p[P_DRV] = 100; s->p[P_BASE] = 10; s->p[P_WDTH] = 100; s->p[P_CHR] = 200; s->p[P_DEL] = 200; s->p[P_REV] = 200;
    s->p[P_L1DST] = D_FREQ; s->p[P_L1DEP] = 100; s->p[P_L2DST] = D_HARM; s->p[P_L2DEP] = 60; s->p[P_SRR] = 30;
}
static void t_golden(void)
{
    static const char *const NAME[] = {"fm held", "va held", "drum pattern", "fm pattern", "steal + release"};
    uint32_t h[5];
    int sc, i;
    if (getenv("GDUMP"))
        gdump = fopen(getenv("GDUMP"), "wb");
    for (sc = 0; sc < 5; sc++) {
        memset(vc, 0, sizeof vc);                    /* each scene from a cold engine: nothing left by the tests before */
        engine_init(pool, sizeof pool);
        reset();
        ghash = 2166136261u;
        gpeak = 0;
        switch (sc) {
        case 0:
            for (i = 0; i < NTRK; i++)
                heavy_fm(&pat.snd[i], i);
            for (i = 0; i < 8; i++)
                engine_note_on(i / 2, 48 + i * 5, 100);
            grender(1.5f);
            for (i = 0; i < 8; i++)
                engine_note_off(i / 2, 48 + i * 5);
            grender(1.0f);
            break;
        case 1:
            for (i = 0; i < NTRK; i++) {
                sound_t *s = &pat.snd[i];
                sound_set_engine(s, ENG_VA);
                s->p[V_WAV1] = (int16_t)(i % 4); s->p[V_WAV2] = VW_SQR; s->p[V_PW] = 200; s->p[V_SYNC] = (int16_t)(i & 1);
                s->p[V_XMOD] = 60; s->p[V_SUB] = 80; s->p[V_NOIS] = 40; s->p[V_DRFT] = 60; s->p[V_TUNE2] = 28;
                s->p[V_PENV] = 40; s->p[P_FTYP] = (int16_t)(1 + i % 3); s->p[P_FREQ] = 300; s->p[P_RESO] = 200;
                s->p[P_L1DST] = D_FDBK; s->p[P_L1DEP] = 300; s->p[P_DEL] = 300;
            }
            for (i = 0; i < 8; i++)
                engine_note_on(i / 2, 40 + i * 7, 60 + i * 8);
            grender(1.5f);
            break;
        case 2: {
            int k;
            sound_t *s = &pat.snd[0];
            dr_kit(s);
            for (k = 0; k < NPAD; k++)
                s->s[S_DM1 + k] = (uint8_t)((k * 5 + 3) % 17);   /* (the 0.5 models) */
            s->p[DR_TON(3)] = 20; s->p[P_SRR] = 20; s->p[P_FTYP] = 1; s->p[P_FREQ] = 400;
            s->p[P_L1DST] = D_ADEC; s->p[P_L1DEP] = 400; s->p[P_REV] = 200;
            for (i = 0; i < 16; i++) {
                put(0, i, PAD_NOTE[i % NPAD]);
                pat.trig[0][i].nnote = 2;
                pat.trig[0][i].chord[0] = (int8_t)(PAD_NOTE[(i * 3 + 1) % NPAD] - PAD_NOTE[i % NPAD]);
            }
            lock_set(&pat, 0, 4, DR_TUN(2), 30);
            lock_set(&pat, 0, 6, P_RTRG, 1);
            dr_kit(&pat.snd[1]);
            put(1, 0, PAD_NOTE[0]);
            put(1, 8, PAD_NOTE[3]);
            pat.fx[P_SCSRC - P_CDEP] = 2;
            pat.fx[P_MFLT - P_CDEP] = 30;
            engine_play();
            grender(4.0f);
            break;
        }
        case 3:
            for (i = 0; i < NTRK; i++)
                heavy_fm(&pat.snd[i], i + 1);
            pat.snd[1].s[S_ARPMODE] = ARP_UP;
            pat.snd[2].s[S_PLAYMODE] = PM_MONO_LEG;
            pat.trk[2].tp[P_PORT - NSP] = 1;
            pat.trk[3].unison = 3;
            pat.snd[3].mdst[MS_VEL][0] = D_FREQ; pat.snd[3].mdep[MS_VEL][0] = 60;
            pat.snd[0].p[P_L1MOD] = 3; pat.snd[0].p[P_L2WAV] = 6; pat.snd[0].p[P_L2DEP] = 400;
            for (i = 0; i < 16; i++) {
                put(i % 4, i, 48 + (i * 7) % 24);
                pat.trig[i % 4][i].micro = (int8_t)((i % 5) - 2);
            }
            lock_set(&pat, 0, 4, P_FREQ, 100);
            lock_set(&pat, 1, 5, P_ALEV, 20);
            lock_set(&pat, 2, 6, P_RTRG, 1);
            pat.swing = 60;
            pat.fx[P_SCSRC - P_CDEP] = 1;
            engine_play();
            grender(4.0f);
            break;
        default:
            for (i = 0; i < NTRK; i++)
                heavy_fm(&pat.snd[i], i);
            sound_set_engine(&pat.snd[2], ENG_VA);
            dr_kit(&pat.snd[3]);
            for (i = 0; i < 20; i++) {
                engine_note_on(i % 4, i % 4 == 3 ? PAD_NOTE[i % 8] : 40 + i * 3, 100);
                grender(0.05f);
                if (i % 3 == 0)
                    engine_note_off(i % 4, 40 + i * 3);
            }
            grender(1.5f);
            break;
        }
        engine_stop_hard();
        h[sc] = ghash;
        if (gpeak < 100000)                          /* (a scene must sound: about -38 dBFS at least) */
            h[sc] = 0;
    }
    {
        const char *gf = getenv("GOLDEN");
        if (gdump)
            fclose(gdump), gdump = 0;
        uint32_t want[5] = {0};
        int have = 0, ok = 1;
        FILE *f = gf ? fopen(gf, "r") : 0;
        if (f) {
            have = fscanf(f, "%x %x %x %x %x", &want[0], &want[1], &want[2], &want[3], &want[4]) == 5;
            fclose(f);
        }
        for (sc = 0; sc < 5; sc++) {
            int same = !have || h[sc] == want[sc];
            ok = ok && same;
            printf("     golden %-16s %08x%s\n", NAME[sc], h[sc], have ? (same ? " same" : " DIFFERS") : "");
        }
        if (gf && !f) {                              /* no file yet: write it */
            f = fopen(gf, "w");
            if (f) {
                fprintf(f, "%08x %08x %08x %08x %08x\n", h[0], h[1], h[2], h[3], h[4]);
                fclose(f);
            }
        }
        check(ok, "golden renders", have ? "every scene sample for sample as before" : "hashes printed");
    }
}

int main(int argc, char **argv)
{
    static const struct { const char *name; void (*fn)(void); } T[] = {
        {"pitch", t_pitch}, {"fm", t_fm_levels}, {"algo", t_algorithms}, {"timing", t_timing},
        {"swing", t_swing}, {"cond", t_conditions}, {"plock", t_plock}, {"firmware", t_firmware}, {"pack", t_pack}, {"retrig", t_retrig}, {"lofi", t_lofi}, {"drumctl", t_drum_ctl}, {"insert", t_insert}, {"menv", t_menv}, {"scale", t_scale}, {"pump", t_pump}, {"lfo", t_lfo}, {"duck", t_duck},
        {"djf", t_djfilter}, {"va", t_va}, {"vacpu", t_va_cpu}, {"drum", t_drum}, {"drumcpu", t_drum_cpu}, {"golden", t_golden}, {"new06", t_new06}, {"pfx", t_pfx}, {"chord08", t_chord08}, {"mod09", t_mod09}, {"new06cpu", t_new06_cpu}, {"cpu", t_cpu}, {"cpuparts", t_cpu_parts},
    };
    unsigned i;
    int a;
    if (argc > 1)
        outdir = argv[1];
    engine_init(pool, sizeof pool);
    for (i = 0; i < sizeof T / sizeof T[0]; i++) {
        int run = argc <= 2;
        for (a = 2; a < argc; a++)
            run |= !strcmp(argv[a], T[i].name);
        if (run)
            T[i].fn();
    }
    printf("%s\n", fails ? "SOME TESTS FAILED" : "all passed");
    return fails != 0;
}
