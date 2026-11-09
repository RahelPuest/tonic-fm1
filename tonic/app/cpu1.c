/* SPDX-License-Identifier: MPL-2.0
 * TONIC: the AC79's second core (core 1): it renders part of the voices [0.16] (engine_dual), and the CPU1 test
 * firmware tests it (tools/cpu1test_build.sh, app/cpu1test.c). It is started once and never again: on the FM-1 a
 * core 1 held and started a second time crashes (the CPU1 test's RESTART, on the device).
 *
 * Written from the steps of the AC79 SDK's EnableOtherCpu as felucca-bsp's chip/fm1_cpu1.h (MPL-2.0) sets them out,
 * and from what the FM-1 projects found on devices and wrote down (X0X, Melodee, Jangada, Felucca's notes in
 * research/fm1/FM1_PLATFORM.md): its interrupt bank off (32 words from 0x1EEF300), its entry address in the word
 * at 0x01C7FFF8, bit 3 of 0x10008 while it starts, CORE_CON (0x1EEE004) bit 3 set and bit 1 cleared; it starts
 * through the chip's ROM, so the PC limits are open until it has reported in. Hold: bit 1 set, bit 3 cleared.
 * It takes no interrupts and waits in RAM (.c1_text) for one job at a time: a function and an argument with a
 * check word (on some FM-1s core 1 now and then reads words wrong), each core writing its own cache line, csync
 * around every hand-over. Nothing waits for it forever. The registers are the BSP's (chip/fm1_cpu1.h), the entry
 * app/cpu1_entry.S. */

#ifndef PLAT_HOST
#include "fm1_cpu1.h"                                 /* the BSP's helpers: hold, prepare, release, own guard, sync */
typedef struct {
    volatile uint32_t job, arg;                       /* core 0's line: job n hands fn(arg) over */
    void (*volatile fn)(uint32_t);
    volatile uint32_t check;
    uint32_t pad0[12];
    volatile uint32_t alive, done;                    /* core 1's line: it runs; the last job it finished */
    volatile uint32_t bad, bad_job;                   /* jobs it read wrong (the check did not match) */
    volatile uint32_t loops;                          /* its waiting loop's rounds (it is alive) */
    uint32_t pad1[11];
} c1mb_t;
static c1mb_t c1mb __attribute__((aligned(64)));
static uint8_t c1_up;
#define C1_CHECK(fn, arg, j) ((uint32_t)(uintptr_t)(fn) ^ (arg) ^ (j) * 0x9E3779B1u)

extern void tonic_c1_entry(void);                     /* app/cpu1_entry.S */
extern uint32_t tonic_c1_ustack[], tonic_c1_sstack_top[];

/* core 1's loop, in RAM: its own stack limits first, then the jobs */
__attribute__((section(".c1_text"), noreturn, used)) void tonic_c1_main(void)
{
    uint32_t done, last_bad = 0xFFFFFFFFu;
    fm1_cpu1_own_guard((uint32_t)(uintptr_t)tonic_c1_ustack + 256u, (uint32_t)(uintptr_t)tonic_c1_sstack_top - 1u);
    done = c1mb.done;
    c1mb.alive = 1;
    fm1_cpu1_sync();
    for (;;) {
        uint32_t j = c1mb.job;
        fm1_cpu1_sync();
        c1mb.loops++;
        if (j == done)
            continue;
        if (j == done + 1u) {
            void (*fn)(uint32_t) = c1mb.fn;
            uint32_t arg = c1mb.arg;
            if (fn && c1mb.check == C1_CHECK(fn, arg, j)) {
                fn(arg);
                done = j;
                fm1_cpu1_sync();
                c1mb.done = j;
                fm1_cpu1_sync();
                continue;
            }
        }
        if (j != last_bad) {                          /* a job read wrong: counted once, then read again */
            last_bad = j;
            c1mb.bad_job = j;
            fm1_cpu1_sync();
            c1mb.bad++;
            fm1_cpu1_sync();
        }
    }
}

static void c1_hold(void)
{
    fm1_cpu1_hold();
    c1_up = 0;
}

/* 0: core 1 runs and answered within 20 ms (*us: how long it took); -1: it is held again */
static int c1_start(uint32_t *us)
{
    uint32_t div, t0;
    c1_hold();
    {                                                 /* (its stack marked: how deep it went, c1_stack_used) */
        uint32_t *p;
        for (p = tonic_c1_ustack; p < tonic_c1_sstack_top; p++)
            *p = 0xC1C1C1C1u;
    }
    c1mb.alive = 0;
    c1mb.done = c1mb.job;
    c1mb.fn = 0;
    fm1_cpu1_prepare(tonic_c1_entry);
    div = fm1_cpu1_release();
    t0 = fm1_ticks();
    while (!c1mb.alive && fm1_ticks() - t0 < 24000u * 20u)
        fm1_cpu1_sync();
    if (us)
        *us = (fm1_ticks() - t0) / 24u;
    fm1_cpu1_started(div);
    if (!c1mb.alive) {
        c1_hold();
        return -1;
    }
    c1_up = 1;
    return 0;
}

/* hand fn(arg) over (core 1 idle: c1_wait came back 0) */
static void c1_give(void (*fn)(uint32_t), uint32_t arg)
{
    uint32_t j = c1mb.job + 1u;
    c1mb.fn = fn;
    c1mb.arg = arg;
    c1mb.check = C1_CHECK(fn, arg, j);
    fm1_cpu1_sync();
    c1mb.job = j;
    fm1_cpu1_sync();
}

/* 0: the last job finished within us microseconds; -1: not (core 1 is then held: it is not trusted) */
static int c1_wait(uint32_t us)
{
    uint32_t t0 = fm1_ticks();
    for (;;) {
        fm1_cpu1_sync();
        if (c1mb.done == c1mb.job)
            return 0;
        if (fm1_ticks() - t0 > us * 24u) {
            c1_hold();
            return -1;
        }
    }
}
static uint32_t c1_stack_used(void)                  /* bytes of core 1's user stack it has used */
{
    const uint32_t *p = tonic_c1_ustack;
    while (p < tonic_c1_sstack_top && *p == 0xC1C1C1C1u)
        p++;
    return (uint32_t)((const uint8_t *)tonic_c1_sstack_top - (const uint8_t *)p) - 256u;
}
static uint32_t c1_bad(void) { fm1_cpu1_sync(); return c1mb.bad; }
static uint32_t c1_loops(void) { fm1_cpu1_sync(); return c1mb.loops; }
static uint32_t c1_now(void) { return fm1_ticks(); }

/* a crash: on core 1 (its stacks) it stops there for good, noted for the next start (which then stays on one core);
 * on core 0 core 1 is held first [APP_FAULT] */
static struct { uint32_t magic, crashed1; } c1keep __attribute__((section(".noinit")));
#define C1K_MAGIC 0x43314B50u                         /* "C1KP" */
#ifdef TONIC_CPU1TEST
static void c1t_note_fault(const fm1_crash_t *c);
#endif
void app_fault(const fm1_crash_t *c)
{
#ifdef TONIC_CPU1TEST
    c1t_note_fault(c);
#endif
    if (c->sp >= (uint32_t)(uintptr_t)tonic_c1_ustack && c->sp < (uint32_t)(uintptr_t)tonic_c1_sstack_top) {
        c1keep.magic = C1K_MAGIC;
        c1keep.crashed1 = 1;
        for (;;)                                      /* core 1's crash: it stops here; core 0's wait runs out */
            fm1_cpu1_sync();
    }
    fm1_cpu1_hold();                                  /* core 0's: core 1 given up */
}
/* every start: core 1 held before anything else runs (a reset may have left it running) [APP_CSTART] */
void app_cstart(void) { fm1_cpu1_hold(); }

/* (0.16) the engine's second core: started once at the start, unless it crashed since the power came on */
static void c1_gave_up(void)                          /* (0.17) the engine gave it up: one core until power-off */
{
    c1keep.magic = C1K_MAGIC;
    c1keep.crashed1 = 1;
}
static const eng_dual_t c1_dual = {c1_give, c1_wait, c1_now, c1_bad, c1_gave_up};
/* (0.17) the main loop: a fault on core 1 (the BSP's handler stopped it there) noted for the next start */
static void c1_poll(void)
{
    if (fm1_core1_faults && !(c1keep.magic == C1K_MAGIC && c1keep.crashed1))
        c1_gave_up();
}
static void c1_engine_start(void)
{
    if (c1keep.magic == C1K_MAGIC && c1keep.crashed1)
        return;
    eng_c1_stack_lo = (uintptr_t)tonic_c1_ustack;
    eng_c1_stack_hi = (uintptr_t)tonic_c1_sstack_top;
    fm1_core1_lo = (uint32_t)(uintptr_t)tonic_c1_ustack;   /* (0.17) the BSP's fault handler: a fault there is core 1's */
    fm1_core1_hi = (uint32_t)(uintptr_t)tonic_c1_sstack_top;
    if (c1_start(0) == 0)
        engine_dual(&c1_dual);
    fm1_guard_unlock_top();                           /* (0.25) the start locked the top of RAM (fm1_cpu1_prepare), but
                                                     * the platform's audio_init writes its vector there after app_init
                                                     * and locks it itself after that: on some FM-1s that write raised
                                                     * the exception at every start (pc 02017644, then safe mode) */
}
#else                                                 /* the host: no second core */
static uint8_t c1_up;
static int c1_start(uint32_t *us) { if (us) *us = 0; return -1; }
static void c1_hold(void) {}
static void c1_give(void (*fn)(uint32_t), uint32_t arg) { (void)fn; (void)arg; }
static int c1_wait(uint32_t us) { (void)us; return -1; }
static uint32_t c1_bad(void) { return 0; }
static uint32_t c1_loops(void) { return 0; }
static uint32_t c1_now(void) { return plat_ms() * 24000u; }
static uint32_t c1_stack_used(void) { return 0; }
static void c1_engine_start(void) {}
static void c1_poll(void) {}
#endif
