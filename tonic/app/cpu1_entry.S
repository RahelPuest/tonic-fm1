/* SPDX-License-Identifier: MPL-2.0
 * TONIC: the second core's entry (app/cpu1.c: the engine's second core [0.16], the CPU1 test): it starts in the chip's ROM, which jumps
 * here. Interrupts off, no interrupt configuration, its own stacks (in the pool: the jobs' user stack, a small one
 * for exceptions), then out into tonic_c1_main, which never returns. */
	.weak tonic_c1_main
	.section .text.tonic_c1,"ax",@progbits
	.balign 4
	.globl tonic_c1_entry
tonic_c1_entry:
	cli
	icfg = 0
	usp = tonic_c1_ustack_top
	sp = tonic_c1_sstack_top
	ssp = tonic_c1_sstack_top
	reti = tonic_c1_main
	rti

	.section .pool.tonic_c1stack,"aw",@nobits
	.balign 8
	.globl tonic_c1_ustack
tonic_c1_ustack:
	.space 4256				/* (the FM engine's deepest path: ~3 KB; 256 B of it the guard's) */
	.globl tonic_c1_ustack_top
tonic_c1_ustack_top:
	.space 256
	.globl tonic_c1_sstack_top
tonic_c1_sstack_top:
