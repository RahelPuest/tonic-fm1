/* SPDX-License-Identifier: MPL-2.0
 * TONIC's CPU1 test firmware [0.15.x] (tools/cpu1test_build.sh): at the start it tests the second core thoroughly
 * on the device, shows each result as it comes, and then (HOME) goes on into TONIC with core 1 held again. Every
 * step has a time limit; a core 1 that does not answer is held and the rest is skipped. The project is not saved
 * while it runs.
 *
 *   START    core 1 starts and reports in (20 ms at most), its start time
 *   MAILBOX  20000 empty jobs: none lost, none read wrong; the hand-over's average and longest time
 *   EXACT    2000 DSP blocks (fastmath, the sine table) on both cores at once: the same bits
 *   SPEED    the same block on core 0 and on core 1 alone: how fast core 1 is
 *   PARALLEL both cores at once against one: the gain
 *   RAM RD   core 1 sums 16 KB of RAM 500 times while core 0 works: always core 0's sum
 *   FLASH RD the same over 32 KB of the flash (the code): core 1's known weak spot on some units
 *   RAM WR   core 1 writes 8 KB, core 0 checks every word, 200 times
 *   AUDIO    10 s with the demo playing: in each audio block core 1 computes a block and core 0 checks it, as a
 *            dual-core engine would (late, wrong, the longest wait)
 *   STR DSP / STR FLASH / STR RAM   7 s each of one kind of job under load (0.15.x: apart, to tell which one fails)
 *   RESTART  core 1 held and started again 10 times (shown, not part of the verdict: a dual-core engine starts it
 *            once; fm1-emu does not model the hold, so there it fails) */

enum { C1T_START, C1T_MAIL, C1T_EXACT, C1T_SPEED, C1T_PAR, C1T_RAMRD, C1T_ROMRD, C1T_RAMWR, C1T_AUDIO, C1T_STRD,
       C1T_STRF, C1T_STRR, C1T_RESTART, C1T_N };
static const char *const C1T_NAME[C1T_N] = {"START", "MAILBOX", "EXACT", "SPEED", "PARALLEL", "RAM RD", "FLASH RD",
                                            "RAM WR", "AUDIO", "STR DSP", "STR FLSH", "STR RAM", "RESTART"};
enum { R_WAIT, R_RUN, R_OK, R_FAIL, R_SKIP };
#define C1T_WORDS 4096u                               /* the test buffer: 16 KB */
#define C1T_ROM ((const uint32_t *)(((uintptr_t)_etext - 32768u) & ~(uintptr_t)3u))   /* the flash: the code's last 32 KB */
#define C1T_ROMW 8192u
static uint32_t c1t_buf[C1T_WORDS] PLAT_POOL;
static volatile uint32_t c1t_sink;                    /* (core 0's load: its results kept, so the work is done) */
static struct {
    volatile uint32_t r0, r1;                         /* core 1's results: a line of their own */
    uint32_t pad[14];
} c1t_out __attribute__((aligned(64)));
static struct {
    uint8_t active, step, state[C1T_N], done, playing;
    char info[C1T_N][20];
    uint32_t n, err, bad0, t0, sum0, maxw, tw, nlate, naudio, aerr, seed;
    uint32_t v[19];                                   /* the measurements for the result code (C1V_*) */
    char code[64];                                    /* the result code, in groups of five */
} c1t PLAT_POOL;                                      /* (0.16: in the pool, the RAM is full; zeroed in c1t_init) */
/* the record that outlives a reset (.noinit): where the test was, what it had found, a crash's place. A start that
 * finds a test it was running did not end: it shows what it had, that step as CRASH (or RESET: a hang), and runs
 * nothing again (no loop) [0.15.x] */
#define C1R_MAGIC 0x43315452u                         /* "C1TR" */
static struct {
    uint32_t magic, running, crashed, vec, pc, sp, emu, up;
    uint8_t state[C1T_N], shown;
    uint32_t v[19];
    char info[C1T_N][20];
} c1rec __attribute__((section(".noinit")));
enum { C1V_START, C1V_MNS, C1V_MMAX, C1V_MBAD, C1V_EXACT, C1V_SPEED, C1V_PAR, C1V_RAMRD, C1V_ROMRD, C1V_RAMWR,
       C1V_ABAD, C1V_AMAX, C1V_AN, C1V_SBAD, C1V_SN, C1V_BAD, C1V_RESTART, C1V_STOP, C1V_STEP };

/* ------------------------------------------------------------------ the work --- */
/* a DSP block: fastmath, the RAM sine table, a feedback sine, an exponential: the same bits on either core */
static uint32_t c1t_kernel(uint32_t seed, uint32_t n)
{
    float ph = (float)(seed & 1023u) * (1.0f / 1024.0f), inc = 0.0137f + (float)(seed % 97u) * 1e-4f, acc = 0, fb = 0;
    uint32_t i, h = seed;
    fm_bits_t b;
    for (i = 0; i < n; i++) {
        float s = fm_sin_turns(ph + fb * 0.3f);
        fb = s;
        acc = acc * 0.999f + s * fm_exp2f((float)(i & 15u) * -0.25f) + sin_tab[(h >> 3) & 255u] * 0.01f;
        ph += inc;
        if (ph >= 1.0f)
            ph -= 1.0f;
        h = h * 1664525u + 1013904223u;
    }
    b.f = acc;
    return b.u ^ h;
}
static uint32_t c1t_sum(const uint32_t *p, uint32_t n)   /* a checksum that sees order and every bit */
{
    uint32_t i, s = 0x811C9DC5u;
    for (i = 0; i < n; i++)
        s = (s ^ p[i]) * 16777619u;
    return s;
}
static void c1j_nop(uint32_t a) { (void)a; }
static void c1j_kernel(uint32_t seed) { c1t_out.r0 = c1t_kernel(seed, 512u); }
static void c1j_kernel_long(uint32_t n) { c1t_out.r0 = c1t_kernel(12345u, n); }
static void c1j_ramrd(uint32_t a) { (void)a; c1t_out.r0 = c1t_sum(c1t_buf, C1T_WORDS); }
static void c1j_romrd(uint32_t a) { (void)a; c1t_out.r0 = c1t_sum(C1T_ROM, C1T_ROMW); }
static void c1j_ramwr(uint32_t seed)
{
    uint32_t i, *p = c1t_buf + C1T_WORDS / 2u;
    for (i = 0; i < C1T_WORDS / 2u; i++)
        p[i] = (i * 2654435761u) ^ seed;
}
static void c1j_audio(uint32_t seed) { c1t_out.r1 = c1t_kernel(seed, 128u); }

/* -------------------------------------------------------------- the result code --- */
/* everything in 45 characters to type (nine groups, three lines): each test's state (2 bits: OK FAIL SKIP other), 16 measurements in a byte
 * each (scaled, at most 255), RESTART (4 bits), the code's version (4 bits), a CRC-16 over them; Crockford's
 * base 32 (no I L O U), in groups of five. tools/cpu1code.py reads it */
static void c1t_make_code(void)
{
    static const char A[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    static const uint8_t SC[16] = {1, 2, 3, 0, 0, 0, 1, 0, 0, 0, 0, 1, 6, 0, 8, 0};   /* (right shifts: the scales) */
    uint8_t b[32] = {0};
    uint32_t pos = 0, i, crc = 0xFFFFu;
#define PUT(val, nb) do { uint32_t v_ = (val), k_; for (k_ = 0; k_ < (nb); k_++, pos++) \
        if (v_ >> k_ & 1u) b[pos >> 3] |= (uint8_t)(1u << (pos & 7)); } while (0)
    for (i = 0; i < C1T_N; i++)
        PUT(c1t.state[i] == R_OK ? 0u : c1t.state[i] == R_FAIL ? 1u : c1t.state[i] == R_SKIP ? 2u : 3u, 2);
    {   /* (code version 2) a crash: whether, in which step, its vector, where (the PC), on
         * core 1 (its stack), how long after the start */
#ifndef PLAT_HOST
        uint32_t on1 = c1rec.sp >= (uint32_t)(uintptr_t)tonic_c1_ustack && c1rec.sp < (uint32_t)(uintptr_t)tonic_c1_sstack_top;
#else
        uint32_t on1 = 0;
#endif
        PUT(c1t.v[C1V_STOP] ? 1u : 0u, 1);   /* (stopped by a crash or a reset) */
        PUT(c1t.v[C1V_STEP] & 15u, 4);       /* (the step it stopped in) */
        PUT(c1rec.crashed ? c1rec.vec & 255u : 0u, 8);
        PUT(c1rec.crashed ? 1u : 0u, 1);
        PUT(c1rec.crashed ? c1rec.pc >> 1 & 0x1FFFFFFu : 0u, 25);   /* (even; the flash and the RAM below 64 MB) */
        PUT(c1rec.crashed && on1, 1);
        PUT(c1rec.up / 1000u > 127u ? 127u : c1rec.up / 1000u, 7);
    }
    for (i = 0; i < 16; i++) {
        uint32_t v = c1t.v[i] >> SC[i];
        PUT(v > 255u ? 255u : v, 8);
    }
    PUT(c1t.v[C1V_RESTART] > 15u ? 15u : c1t.v[C1V_RESTART], 4);
    PUT(2u, 4);                                       /* (the code's version) */
    for (i = 0; i < pos; i++) {                       /* CRC-16 (CCITT) over the bits so far */
        uint32_t bit = (uint32_t)(b[i >> 3] >> (i & 7)) & 1u;
        crc = ((crc << 1) ^ (((crc >> 15) ^ bit) & 1u ? 0x1021u : 0u)) & 0xFFFFu;
    }
    PUT(crc, 16);
#undef PUT
    {
        uint32_t c = 0, n = (pos + 4u) / 5u;
        for (i = 0; i < n; i++) {
            uint32_t k, v = 0;
            for (k = 0; k < 5u; k++) {
                uint32_t p = i * 5u + k;
                v |= (p < pos ? (uint32_t)(b[p >> 3] >> (p & 7)) & 1u : 0u) << k;
            }
            if (i && i % 5u == 0)
                c1t.code[c++] = '-';
            c1t.code[c++] = A[v];
        }
        c1t.code[c] = 0;
    }
}

/* --------------------------------------------------------------- the screen --- */
static void c1t_info(int t, const char *a, uint32_t v, const char *b)
{
    char *s = c1t.info[t];
    tx_cpy(s, a);
    if (v != 0xFFFFFFFFu)
        tx_num(s + tx_len(s), (int)v);
    tx_cat(s, b);
}
static void c1t_draw(void)
{
    int t, ok = 1, any = 0;
    static const char *const ST[] = {"", "...", "OK", "FAIL", "SKIP"};
    static const char *const OUT[] = {"CPU1 NOT TESTED", "CPU1 NOT RELIABLE", "CPU1 RELIABLE"};
    o_clear();
    o_header("CPU1 TEST", c1t.done ? "DONE" : "RUNNING");
    for (t = 0; t < C1T_N; t++) {
        int y = 11 + t * 6;
        o_ink = c1t.state[t] == R_FAIL ? K_ACC : c1t.state[t] == R_OK ? K_FG : K_DIM;
        o_ts(1, y, C1T_NAME[t], 1);
        o_ts(36, y, ST[c1t.state[t]], 1);
        o_ink = K_DIM;
        o_ts(54, y, c1t.info[t], 1);
        if (t != C1T_RESTART) {
            ok = ok && (c1t.state[t] == R_OK);
            any = any || c1t.state[t] == R_FAIL;
        }
    }
    o_ink = K_FG;
    if (c1t.done) {
        char l[3][20];
        int k, j, c = 0;
        o_ts(1, 89, c1t.v[C1V_STOP] ? (c1rec.crashed ? "STOPPED BY A CRASH" : "STOPPED BY A RESET") : OUT[ok ? 2 : any ? 1 : 0], 1);
        for (j = 0; j < 3; j++) {                     /* the code: three groups a line */
            for (k = 0; k < 17 && c1t.code[c]; k++)
                l[j][k] = c1t.code[c++];
            l[j][k] = 0;
            if (c1t.code[c] == '-')
                c++;
        }
        o_ink = K_ACC;
        for (j = 0; j < 3; j++)
            o_ts(1, 95 + j * 6, l[j], 1);
        o_ink = K_DIM;
        o_ts(1, 114, "HOME: TONIC  PLAY: TEST AGAIN", 1);
    } else {
        o_ink = K_DIM;
        o_ts(1, 114, "PLEASE WAIT (ABOUT 55 S)", 1);
    }
    o_send();
}

/* ----------------------------------------------------------------- the record --- */
static void c1r_save(void)
{
    uint32_t i;
    for (i = 0; i < C1T_N; i++)
        c1rec.state[i] = c1t.state[i];
    memcpy(c1rec.v, c1t.v, sizeof c1rec.v);
    memcpy(c1rec.info, c1t.info, sizeof c1rec.info);
}
static void c1r_load(void)
{
    uint32_t i;
    for (i = 0; i < C1T_N; i++)
        c1t.state[i] = c1rec.state[i] <= R_SKIP ? c1rec.state[i] : R_WAIT;
    memcpy(c1t.v, c1rec.v, sizeof c1t.v);
    memcpy(c1t.info, c1rec.info, sizeof c1t.info);
    for (i = 0; i < C1T_N; i++)
        c1t.info[i][19] = 0;
}
/* a crash (app/cpu1.c's app_fault, before the crash screen): where, for the next start */
static void c1t_note_fault(const fm1_crash_t *c)
{
    if (c1rec.magic == C1R_MAGIC && c1rec.running) {
        c1rec.crashed = 1;
        c1rec.vec = c->vec;
        c1rec.pc = c->pc;
        c1rec.sp = c->sp;
        c1rec.emu = c->emu;
        c1rec.up = c->uptime_ms;
    }
}

/* ------------------------------------------------------------------ the steps --- */
static void c1t_end(int t, int ok)
{
    c1t.state[t] = ok ? R_OK : R_FAIL;
    c1t.step = (uint8_t)(t + 1);
    c1t.n = 0;
    c1t.err = 0;
    c1t.maxw = 0;
    c1t.tw = 0;
    c1t.bad0 = c1_bad();
    if (!ok && !c1_up) {                              /* core 1 gone: the rest cannot run */
        int k;
        for (k = t + 1; k < C1T_N; k++)
            if (k != C1T_RESTART) {
                c1t.state[k] = R_SKIP;
            }
    }
    c1r_save();
}
/* the audio interrupt's part (AUDIO): core 1 computes a block, core 0 the same, then they are compared */
static void c1t_audio(void)
{
    uint32_t t0, w, want;
    if (!c1t.playing || !c1_up)
        return;
    t0 = c1_now();
    c1_give(c1j_audio, c1t.seed);
    want = c1t_kernel(c1t.seed, 128u);
    if (c1_wait(1500u) < 0) {
        c1t.nlate++;
        c1t.playing = 0;
        return;
    }
    w = (c1_now() - t0) / 24u;
    if (w > c1t.maxw)
        c1t.maxw = w;
    if (c1t_out.r1 != want)
        c1t.aerr++;
    c1t.naudio++;
    c1t.seed = c1t.seed * 1103515245u + 12345u;
}
static void c1t_run(void)
{
    int t = c1t.step;
    uint32_t i, us;
    if (t >= C1T_N) {
        c1t.v[C1V_BAD] = c1_bad();
        c1_hold();
        c1t_make_code();
        c1t.done = 1;
        c1rec.running = 0;
        c1rec.shown = 1;
        c1r_save();
        return;
    }
    if (c1t.state[t] == R_SKIP) {
        c1t.step++;
        return;
    }
    c1t.state[t] = R_RUN;
    c1rec.running = (uint32_t)t + 1u;
    switch (t) {
    case C1T_START:
        if (c1_start(&us) == 0) {
            c1t_info(t, "", us, " US");
            c1t.v[C1V_START] = us;
            c1t_end(t, 1);
        } else {
            c1t_info(t, "NO ANSWER", 0xFFFFFFFFu, "");
            c1t_end(t, 0);
        }
        break;
    case C1T_MAIL:                                    /* 2000 a frame, 20000 in all */
        for (i = 0; i < 2000u; i++) {
            uint32_t t0 = c1_now(), w;
            c1_give(c1j_nop, i);
            if (c1_wait(2000u) < 0) {
                c1t_info(t, "LOST AT ", c1t.n + i, "");
                c1t_end(t, 0);
                return;
            }
            w = c1_now() - t0;
            c1t.tw += w;
            if (w > c1t.maxw)
                c1t.maxw = w;
        }
        c1t.n += 2000u;
        if (c1t.n >= 20000u) {
            uint32_t bad = c1_bad() - c1t.bad0;
            c1t_info(t, "", c1t.tw / c1t.n * 1000u / 24000u, "");   /* (ns: ticks / 24 MHz) */
            c1t.v[C1V_MNS] = c1t.tw / c1t.n * 1000u / 24000u;
            c1t.v[C1V_MMAX] = c1t.maxw / 24u;
            c1t.v[C1V_MBAD] = bad;
            tx_cat(c1t.info[t], "NS MAX ");
            tx_num(c1t.info[t] + tx_len(c1t.info[t]), (int)(c1t.maxw / 24u));
            tx_cat(c1t.info[t], bad ? "US BAD" : "US");
            c1t_end(t, bad == 0);
        }
        break;
    case C1T_EXACT:                                   /* 200 a frame, 2000 in all, both cores at once */
        for (i = 0; i < 200u; i++) {
            uint32_t s = c1t.n + i + 1u, want;
            c1_give(c1j_kernel, s);
            want = c1t_kernel(s, 512u);
            if (c1_wait(20000u) < 0) {
                c1t_info(t, "LOST", 0xFFFFFFFFu, "");
                c1t_end(t, 0);
                return;
            }
            c1t.err += c1t_out.r0 != want;
        }
        c1t.n += 200u;
        if (c1t.n >= 2000u) {
            c1t_info(t, "", c1t.err, " WRONG OF 2000");
            c1t.v[C1V_EXACT] = c1t.err;
            c1t_end(t, c1t.err == 0);
        }
        break;
    case C1T_SPEED: {                                 /* the same block alone on each core */
        uint32_t a, b, k = 20000u;
        a = c1_now();
        c1t.sum0 = c1t_kernel(12345u, k);
        a = c1_now() - a;
        b = c1_now();
        c1_give(c1j_kernel_long, k);
        if (c1_wait(200000u) < 0) {
            c1t_info(t, "LOST", 0xFFFFFFFFu, "");
            c1t_end(t, 0);
            break;
        }
        b = c1_now() - b;
        c1t_info(t, "C1 ", b ? a * 100u / b : 0, "% OF C0");
        c1t.v[C1V_SPEED] = b ? a * 100u / b : 0;
        c1t_end(t, c1t_out.r0 == c1t.sum0);
        break;
    }
    case C1T_PAR: {                                   /* both at once against one */
        uint32_t a, b, k = 20000u;
        a = c1_now();
        c1t_sink ^= c1t_kernel(54321u, k);
        a = c1_now() - a;
        b = c1_now();
        c1_give(c1j_kernel_long, k);
        c1t_sink ^= c1t_kernel(54321u, k);
        if (c1_wait(200000u) < 0) {
            c1t_info(t, "LOST", 0xFFFFFFFFu, "");
            c1t_end(t, 0);
            break;
        }
        b = c1_now() - b;
        c1t_info(t, "X", b ? 2u * a * 100u / b : 0, "/100 SPEED");
        c1t.v[C1V_PAR] = b ? 2u * a * 100u / b : 0;
        c1t_end(t, b < 2u * a * 9u / 10u);            /* (both at once: at least 1.8 x one) */
        break;
    }
    case C1T_RAMRD:                                   /* 50 a frame, 500 in all, core 0 busy meanwhile */
        if (!c1t.n) {
            for (i = 0; i < C1T_WORDS; i++)
                c1t_buf[i] = i * 2246822519u ^ 0xA5A5A5A5u;
            c1t.sum0 = c1t_sum(c1t_buf, C1T_WORDS);
        }
        for (i = 0; i < 50u; i++) {
            c1_give(c1j_ramrd, i);
            c1t_sink ^= c1t_kernel(i, 256u);
            if (c1_wait(20000u) < 0) {
                c1t_info(t, "LOST", 0xFFFFFFFFu, "");
                c1t_end(t, 0);
                return;
            }
            c1t.err += c1t_out.r0 != c1t.sum0;
        }
        c1t.n += 50u;
        if (c1t.n >= 500u) {
            c1t_info(t, "", c1t.err, " WRONG OF 500");
            c1t.v[t == C1T_RAMRD ? C1V_RAMRD : C1V_ROMRD] = c1t.err;
            c1t_end(t, c1t.err == 0);
        }
        break;
    case C1T_ROMRD:
        if (!c1t.n)
            c1t.sum0 = c1t_sum(C1T_ROM, C1T_ROMW);
        for (i = 0; i < 50u; i++) {
            c1_give(c1j_romrd, i);
            c1t_sink ^= c1t_kernel(i, 256u);
            if (c1_wait(40000u) < 0) {
                c1t_info(t, "LOST", 0xFFFFFFFFu, "");
                c1t_end(t, 0);
                return;
            }
            c1t.err += c1t_out.r0 != c1t.sum0;
        }
        c1t.n += 50u;
        if (c1t.n >= 500u) {
            c1t_info(t, "", c1t.err, " WRONG OF 500");
            c1t.v[t == C1T_RAMRD ? C1V_RAMRD : C1V_ROMRD] = c1t.err;
            c1t_end(t, c1t.err == 0);
        }
        break;
    case C1T_RAMWR:                                   /* 20 a frame, 200 in all */
        for (i = 0; i < 20u; i++) {
            uint32_t s = c1t.n + i + 7u, k, *p = c1t_buf + C1T_WORDS / 2u;
            c1_give(c1j_ramwr, s);
            if (c1_wait(20000u) < 0) {
                c1t_info(t, "LOST", 0xFFFFFFFFu, "");
                c1t_end(t, 0);
                return;
            }
            for (k = 0; k < C1T_WORDS / 2u; k++)
                c1t.err += p[k] != ((k * 2654435761u) ^ s);
        }
        c1t.n += 20u;
        if (c1t.n >= 200u) {
            c1t_info(t, "", c1t.err, " WORDS WRONG");
            c1t.v[C1V_RAMWR] = c1t.err;
            c1t_end(t, c1t.err == 0);
        }
        break;
    case C1T_AUDIO:                                   /* 10 s of the demo, core 1 in every audio block */
        if (!c1t.n) {
            c1t.t0 = plat_ms();
            c1t.naudio = c1t.nlate = c1t.aerr = c1t.maxw = 0;
            c1t.seed = 99u;
            engine_play();
            c1t.playing = 1;
            c1t.n = 1;
        }
        if (plat_ms() - c1t.t0 >= 10000u || !c1t.playing) {
            c1t.playing = 0;
            engine_stop();
            c1t_info(t, "", c1t.nlate + c1t.aerr, " BAD MAX ");
            c1t.v[C1V_ABAD] = c1t.nlate + c1t.aerr;
            c1t.v[C1V_AMAX] = c1t.maxw;
            c1t.v[C1V_AN] = c1t.naudio;
            tx_num(c1t.info[t] + tx_len(c1t.info[t]), (int)c1t.maxw);
            tx_cat(c1t.info[t], "US");
            c1t_end(t, c1t.naudio > 1000u && c1t.nlate == 0 && c1t.aerr == 0);
        }
        break;
    case C1T_RESTART:
        for (i = 0; i < 10u; i++) {
            c1_hold();
            if (c1_start(0) < 0)
                c1t.err++;
        }
        c1t_info(t, "", 10u - c1t.err, " OF 10");
        c1t.v[C1V_RESTART] = 10u - c1t.err;
        c1t_end(t, c1t.err == 0);
        break;
    case C1T_STRD:                                    /* 7 s of one kind of job each, a frame at a time */
    case C1T_STRF:
    case C1T_STRR: {
        uint32_t r = (uint32_t)(t - C1T_STRD);
        if (!c1t.n) {
            c1t.t0 = plat_ms();
            for (i = 0; i < C1T_WORDS; i++)
                c1t_buf[i] = i * 2246822519u ^ 0xA5A5A5A5u;
            c1t.n = 1;
        }
#ifdef TONIC_C1T_CRASH                                 /* (the crash path's own test: 1 core 0, 2 core 1, in STR FLSH) */
        if (t == C1T_STRF && plat_ms() - c1t.t0 > 2000u) {
            static void (*volatile bad)(uint32_t);
            if (TONIC_C1T_CRASH == 1)
                bad(0);
            c1_give(bad, 0);
        }
#endif
        for (i = 0; i < 30u; i++) {
            uint32_t s = c1t.seed = c1t.seed * 1103515245u + 12345u, want = 0;
            if (r == 0) {
                c1_give(c1j_kernel, s);
                want = c1t_kernel(s, 512u);
            } else if (r == 1) {
                c1_give(c1j_romrd, s);
                c1t_sink ^= c1t_kernel(s, 256u);
            } else {
                c1_give(c1j_ramrd, s);
                c1t_sink ^= c1t_kernel(s, 256u);
            }
            if (c1_wait(40000u) < 0) {
                c1t_info(t, "LOST AFTER ", c1t.tw, "");
                c1t.v[C1V_SBAD] += 1u;
                c1t_end(t, 0);
                return;
            }
            if (r == 0)
                c1t.err += c1t_out.r0 != want;
            else if (r == 1)
                c1t.err += c1t_out.r0 != c1t_sum(C1T_ROM, C1T_ROMW);
            else
                c1t.err += c1t_out.r0 != c1t_sum(c1t_buf, C1T_WORDS);
            c1t.tw++;
        }
        if (plat_ms() - c1t.t0 >= 7000u) {
            uint32_t bad = c1_bad() - c1t.bad0;
            c1t_info(t, "", c1t.err + bad, " BAD OF ");
            c1t.v[C1V_SBAD] += c1t.err + bad;          /* (the three together) */
            c1t.v[C1V_SN] += c1t.tw;
            tx_num(c1t.info[t] + tx_len(c1t.info[t]), (int)c1t.tw);
            c1t_end(t, c1t.err == 0 && bad == 0);
        }
        break;
    }
    default: c1t.step++; break;
    }
}

/* the main loop's part: one slice of the tests, the screen; 1 while the test page is up */
static int c1t_frame(void)
{
    static uint32_t held;
    if (!c1t.active)
        return 0;
    if (!c1t.done)
        c1t_run();
    c1t_draw();
    if (c1t.done && !held && (plat_buttons() >> B_PLAY & 1u)) {   /* PLAY: the test again */
        memset(&c1t, 0, sizeof c1t);
        memset(&c1rec, 0, sizeof c1rec);
        c1rec.magic = C1R_MAGIC;
        c1t.active = 1;
        return 1;
    }
    if (c1t.done && (plat_buttons() >> B_HOME & 1u)) {   /* HOME: on into TONIC, once it is let go */
        held = 1;
        return 1;
    }
    if (held && !plat_buttons()) {                    /* (the press and its release go nowhere) */
        uint32_t now = plat_ms();
        int i;
        c1t.active = 0;
        ui.btn = 0;
        for (i = 0; i < NB; i++)
            ui.btn_t[i] = now;
        ui.redraw = 1;
        lcd_fill(0, 0, 240, 240, C_BLACK);
    }
    return 1;
}
/* the start: a test that did not end (a crash or a reset in step running - 1) is shown as it stood, that step
 * FAIL, the rest SKIP, and is not run again; one that ended is shown again; otherwise the test runs */
static void c1t_init(void)
{
    memset(&c1t, 0, sizeof c1t);
    c1t.active = 1;
#if defined(TONIC_C1T_CRASH) && TONIC_C1T_CRASH == 3   /* (as after a crash in STR FLSH: the record's way back) */
    {
        int k;
        memset(&c1rec, 0, sizeof c1rec);
        c1rec.magic = C1R_MAGIC;
        for (k = 0; k < C1T_STRF; k++) {
            c1rec.state[k] = R_OK;
            tx_cpy(c1rec.info[k], "FROM BEFORE");
        }
        c1rec.v[C1V_START] = 136u;
        c1rec.running = C1T_STRF + 1u;
        c1rec.crashed = 1;
        c1rec.vec = 13u;
        c1rec.pc = (uint32_t)(uintptr_t)c1j_romrd + 6u;
        c1rec.sp = (uint32_t)(uintptr_t)tonic_c1_sstack_top - 40u;
        c1rec.up = 41234u;
    }
#endif
    if (c1rec.magic == C1R_MAGIC && c1rec.running >= 1u && c1rec.running <= C1T_N) {
        int t = (int)c1rec.running - 1, k;
        c1r_load();
        c1t.state[t] = R_FAIL;
        if (c1rec.crashed) {
            c1t_info(t, "CRASH V", c1rec.vec, "");
        } else {
            c1t_info(t, "RESET", 0xFFFFFFFFu, "");
            c1rec.pc = c1rec.sp = c1rec.vec = c1rec.up = 0;
        }
        for (k = t + 1; k < C1T_N; k++)
            c1t.state[k] = R_SKIP;
        c1t.v[C1V_STOP] = 1;
        c1t.v[C1V_STEP] = (uint32_t)t;
        c1t.step = C1T_N;
        c1t_make_code();
        c1t.done = 1;
        c1rec.running = 0;
        c1rec.shown = 1;
        c1r_save();
        return;
    }
    if (c1rec.magic == C1R_MAGIC && c1rec.shown) {   /* (shown before: the same again, PLAY runs it anew) */
        c1r_load();
        c1t.step = C1T_N;
        c1t_make_code();
        c1t.done = 1;
        return;
    }
    memset(&c1rec, 0, sizeof c1rec);
    c1rec.magic = C1R_MAGIC;
}
