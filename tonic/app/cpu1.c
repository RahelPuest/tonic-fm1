/* SPDX-License-Identifier: MPL-2.0
 * TONIC: the AC79's second core (core 1): it renders part of the voices [0.16] and the effects [0.17] (engine_dual).
 * (0.27) fm1-base's second core (fm1.toml cpu2 = true, plat_cpu2_* in plat.h, felucca-bsp's chip/fm1_cpu1.*): its
 * start, the jobs with their check words, the time limit, its faults and the bar until power-off are the platform's
 * (cpu2_fallback: one core after a failure, no restart). 0.16-0.26 had their own here (and a CPU1 test firmware). */

#ifndef PLAT_HOST
#include "fm1_time.h"                                 /* fm1_ticks: the jobs' times (24 MHz) */
static void c1_give(void (*fn)(uint32_t), uint32_t arg)
{
    if (!plat_cpu2_run(fn, arg))                      /* (not taken: done here, so the wait finds it done) */
        fn(arg);
}
static int c1_wait(uint32_t us)                       /* 0 done; -1 given up, the job dropped (the engine redoes it) */
{
    (void)us;                                         /* (the platform's own limit: 8 ms) */
    return plat_cpu2_wait();
}
static uint32_t c1_now(void) { return fm1_ticks(); }
static uint32_t c1_bad(void) { return plat_cpu2_bad(); }
static void c1_gave_up(void) {}                       /* (the engine stops giving it jobs; the platform holds it) */
static const eng_dual_t c1_dual = {c1_give, c1_wait, c1_now, c1_bad, c1_gave_up};
static void c1_engine_start(void)                     /* app_init: the one time it may start */
{
    if (plat_cpu2_set(1) == 0)
        engine_dual(&c1_dual);
}
#else                                                 /* the host: no second core */
static void c1_engine_start(void) {}
#endif
