/* arch/sparc/kernel/entry.S:  Sparc trap low-level entry points.
 *
 * Sparc traps are so ugly, this code is going to go through a lot
 * of changes as I find out more interesting things. See head.S for
 * the trap table and how it works, this will show you how we get
 * to these routines.
 *
 * Copyright (C) 1994 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/head.h>
#include <asm/asi.h>
#include <asm/psr.h>
#include <asm/cprefix.h>
#include <asm/vaddrs.h>

/* Here are macros for routines we do often, this allows me to inline this
 * without making the code look real ugly. Well, the macro looks ugly too but
 * makes the trap entry code easier to understand.
 */

/* I really don't like synthetic instructions. So I avoid them like the
 * plague.
 */

/* Note that when I have to write a window out, and it is a user's window, I
 * have to check that the pages of memory that I am going to throw the window(s)
 * onto are valid and are writable by the user (this is %sp to %sp + 64) before
 * I start dumping stuff there. We always assume that kernels stack is ok.
 *
 * If we have to save a kernel window, only one branch is taken. This should
 * make trap handlers quicker in this scenario.
 *
 * Once 'current' is loaded into %g6, it stays there until we leave
 * this macro.
 *
 * XXX must do some checking on the assumption that kernel stack is always ok
 */

/* I will document how this works real soon. TODO */

#define TRAP_WIN_CLEAN \
	or	%g0, %g5, %l5;    /* we need the globals to do our work */ \
	or	%g0, %g6, %l6;    /* and %l0 to %l4 are loaded with important */ \
	or	%g0, %g7, %l7;	  /* information like the psr and pc's to return to */ \
	sethi	%hi( C_LABEL(current) ), %g6; \
	ld	[%g6 + %lo( C_LABEL(current) )], %g6; \
	ld	[%g6 + THREAD_UWINDOWS], %g7; /* how many user wins are active? */ \
	subcc	%g7, 0x0, %g0; \
	bne	2f;                            /* If there are any, branch. */ \
	save	%g0, %g0, %g0;                 /* Save into that window either way. */ \
	std	%l0, [%sp];                    /* If above shows only kernel windows */ \
1:	std	%l2, [%sp + 0x8];              /* then we get here. */ \
	std	%l4, [%sp + 0x10]; \
	std	%l6, [%sp + 0x18]; \
	std	%i0, [%sp + 0x20]; \
	std	%i2, [%sp + 0x28]; \
	std	%i4, [%sp + 0x30]; \
	std	%i6, [%sp + 0x38]; \
	or	%g0, 0x1, %g5; \
	rd	%psr, %g7; \
	sll	%g5, %g7, %g5; \
	wr	%g5, 0x0, %wim;                 /* update %wim to 'now' invalid */ \
	and	%g7, 0x1f, %g7; \
	st	%g7, [%g6 + THREAD_WIM];        /* save 'this' threads mask */ \
	restore	%g0, %g0, %g0; \
	or	%g0, %l5, %g5;                  /* restore the globals we used */ \
	or	%g0, %l6, %g6; \
	b	8f;                             /* we are done */ \
	or	%g0, %l7, %g7; \
2:	sub	%g7, 0x1, %g7; \
	st	%g7, [%g6 + THREAD_UWINDOWS];   /* There are user windows if we */ \
	andcc	%sp, 0x7, %g0;                  /* get here. Check for stack alignment. */ \
	bne	5f;                             /* Stack is unaligned, yuck. */ \
	sra	%sp, 0x1e, %g7;                 /* This stuff checks to see if top 3-bits */ \
	subcc	%g7, 0x0, %g0;                  /* of stack pointer address are ok. */ \
	be,a    3f; \
	andn	%sp, 0xfff, %g7; \
	subcc	%g7, -1, %g0; \
	bne	5f; 				/* bad stack pointer, ugh */ \
	andn	%sp, 0xfff, %g7; \
3:	lda	[%g7] ASI_PTE, %g7;             /* Ok, user stack is a valid address */ \
	srl	%g7, 0x1d, %g7; \
	subcc	%g7, 0x6, %g0;                  /* Can the user write to it? */ \
	bne	5f; \
	and	%sp, 0xfff, %g7; \
	subcc	%g7, 0xfc1, %g0;                /* Is our save area on one page? */ \
	bl,a	1b; \
	std	%l0, [%sp]; \
	add	%sp, 0x38, %g5;                 /* Nope, have to check both pages */ \
	sra	%g5, 0x1e, %g7; \
	subcc	%g7, 0x0, %g0; \
	be,a	4f; \
	andn	%g5, 0xfff, %g7; \
	subcc	%g7, -1, %g0; \
	bne	5f; \
	andn	%g5, 0xfff, %g7; \
4:	lda	[%g7] ASI_PTE, %g7;             /* Stack space in 2nd page is valid */ \
	srl	%g7, 0x1d, %g7; \
	subcc	%g7, 0x6, %g0;                  /* Can user write here too? */ \
	be,a	1b; \
	std	%l0, [%sp]; \
5:	ld	[%g6 + THREAD_UWINDOWS], %g7;   /* This is due to either bad page perms */ \
	add	%g6, THREAD_REG_WINDOW, %g5;    /* for the users stack area, or the stack */ \
6:	std	%l0, [%g5];                     /* pointer is misaligned. See above. */ \
	std	%l2, [%g5 + 0x8]; \
	std	%l4, [%g5 + 0x10]; \
	std	%l6, [%g5 + 0x18]; \
	std	%i0, [%g5 + 0x20]; \
	std	%i2, [%g5 + 0x28]; \
	std	%i4, [%g5 + 0x30]; \
	std	%i6, [%g5 + 0x38]; \
	subcc	%g7, 0x1, %g7; \
	bge,a	6b;                             /* while(uwindows>=0) { write_win(); */ \
	save	%g5, 0x40, %g5;                 /*     uwindows--; } */ \
	st	%sp, [%g6 + THREAD_USP]; \
	or	%g0, 0x1, %g5; \
	rd	%psr, %g7; \
	sll	%g5, %g7, %g5; \
	wr	%g5, 0x0, %wim; \
	and	%g7, 0x1f, %g7; \
	st	%g7, [%g6 + THREAD_WIM];        /* Update thread_struct fields */ \
	ld	[%g6 + THREAD_UWINDOWS], %g7; \
	add	%g7, 0x1, %g5; \
	st	%g5, [%g6 + THREAD_W_SAVED]; \
	st	%g0, [%g6 + THREAD_UWINDOWS]; \
7:	subcc	%g7, 0x1, %g7;                  /* Restore back to where we started. */ \
	bge	7b; \
	restore	%g0, %g0, %g0; \
	or	%g0, %l5, %g5;                  /* Restore the globals. */ \
	or	%g0, %l6, %g6; \
	or	%g0, %l7, %g7; \
8:      nop;                                     /* We are done when we get here. */ \

/* As if the last macro wasn't enough, we have to go through a very similar routine
 * upon entry to most traps and interrupts. This is save away the current window
 * if it is the trap window, clean it, and adjust the stack for the handler c-code
 * to work.
 */

#define ENTER_TRAP \
	rd	%wim, %l4; \
	or	%g0, 0x1, %l5; \
	sll	%l5, %l0, %l5; \
	andcc	%l0, 0x40, %g0; \
	bz	1f; \
	andcc	%l4, %l5, %g0; \
	bz,a	3f; \
	sub	%fp, 0xb0, %sp; \
	TRAP_WIN_CLEAN \
	b	3f; \
	sub	%fp, 0xb0, %sp; \
1:	sethi	%hi( C_LABEL(current) ), %l6; \
	ld	[%l6 + %lo( C_LABEL(current) )], %l6; \
	ld	[%l6 + THREAD_WIM], %l5; \
	and	%l0, 0x1f, %l4; \
	cmp	%l5, %l3; \
	ble,a	4f; \
	sethi	%hi( C_LABEL(nwindowsm1) ), %l4; \
	sub	%l5, %l3, %l3; \
	b	5f; \
	sub	%l3, 0x1, %l5; \
4:	ld	[%l4 + %lo( C_LABEL(nwindowsm1) )], %l4; \
	sub	%l4, %l3, %l4; \
	add	%l5, %l4, %l5; \
5:	st	%l5, [%l6 + THREAD_UWINDOWS]; \
	bz,a	2f; \
	sethi	%hi(TASK_SIZE-176), %l5; \
	TRAP_WIN_CLEAN; \
	sethi	%hi( C_LABEL(current) ), %l6; \
	ld	[%l6 + %lo( C_LABEL(current) )], %l6; \
	sethi	%hi(TASK_SIZE-176), %l5; \
2:	or	%l5, %lo(TASK_SIZE-176), %l5; \
	add	%l6, %l5, %sp; \
3: \

#define ENTER_IRQ \
	rd	%wim, %l4; \
	or	%g0, 0x1, %l5; \
	sll	%l5, %l0, %l5; \
	andcc	%l0, 0x40, %g0; \
	bz	1f; \
	andcc	%l4, %l5, %g0; \
	sethi	%hi( C_LABEL(eintstack) ), %l7; \
	or	%l7, %lo( C_LABEL(eintstack) ), %l7; \
	bz	0f; \
	nop; \
	TRAP_WIN_CLEAN \
	sethi	%hi( C_LABEL(eintstack) ), %l7; \
	or	%l7, %lo( C_LABEL(eintstack) ), %l7; \
0:	subcc	%fp, %l7, %g0; \
	bg,a	3f; \
	sub	%l7, 0xb0, %sp; \
	b	3f; \
	sub	%fp, 0xb0, %sp; \
1: 	sethi	%hi( C_LABEL(current) ), %l6; \
	ld	[%l6 + %lo( C_LABEL(current) )], %l6; \
	ld	[%l6 + THREAD_WIM], %l5; \
	and	%l0, 0x1f, %l7; \
	cmp	%l5, %l7; \
	ble,a	4f; \
	sethi	%hi( C_LABEL(nwindowsm1) ), %l4; \
	sub	%l5, %l7, %l7; \
	b	5f; \
	sub	%l7, 0x1, %l5; \
4:	ld	[%l4 + %lo( C_LABEL(nwindowsm1) )], %l4; \
	sub	%l4, %l7, %l4; \
	add	%l5, %l4, %l5; \
5:	st	%l5, [%l6 + THREAD_UWINDOWS]; \
	bz,a	2f; \
	sethi	%hi( C_LABEL(eintstack) ), %l7; \
	TRAP_WIN_CLEAN \
	sethi	%hi( C_LABEL(eintstack) ), %l7; \
2: \
	sub	%l7, 0xb0, %sp; \
3:


	.text
	.align 4

/* Default trap handler */
	.globl my_trap_handler
my_trap_handler:
#if 1
		jmp	%l1
		rett	%l2
		nop
#else
		rd %wim, %l4
		or %g0, 0x1, %l5
		sll %l5, %l0, %l5
		cmp %l4, %l5        ! are we in the invalid window?
	
		TRAP_WIN_CLEAN

		nop
		or %g0, %l3, %o0
		call C_LABEL(do_hw_interrupt)
		or %g0, %g0, %o1
		wr %l0, 0x20, %psr  ! re-enable traps and reset the condition codes
		nop
		nop
		nop                 ! click our heels three times, "no place like home"
		jmp %l1
		rett %l2
#endif /* bogon */
	
	.align 4
	.globl sparc_timer
sparc_timer:
	sethi	%hi(TIMER_VADDR), %l4
	or	%l4, %lo(TIMER_VADDR), %l4	! read the limit register
	ld	[%l4 + 0xc], %l4		! to clear the interrupt
	rd	%wim, %l4
	or	%g0, 0x1, %l5
	sll	%l5, %l0, %l5
	andcc	%l0, 0x40, %g0
	bz	st1
	sethi	%hi( C_LABEL(eintstack) ), %l7
	andcc	%l4, %l5, %g0
	bz	st0
	or	%l7, %lo( C_LABEL(eintstack) ), %l7
	TRAP_WIN_CLEAN
	sethi	%hi( C_LABEL(eintstack) ), %l7
	or	%l7, %lo( C_LABEL(eintstack) ), %l7
st0:	subcc	%fp, %l7, %g0
	bg,a	st3
	sub	%l7, 0xb0, %sp
	b	st3
	sub	%fp, 0xb0, %sp
st1: 	sethi	%hi( C_LABEL(current) ), %l6
	ld	[%l6 + %lo( C_LABEL(current) )], %l6
	ld	[%l6 + THREAD_WIM], %l5
	and	%l0, 0x1f, %l7
	cmp	%l5, %l7
	ble,a	st4
	sethi	%hi( C_LABEL(nwindowsm1) ), %l4
	sub	%l5, %l7, %l7
	b	st5
	sub	%l7, 0x1, %l5
st4:	ld	[%l4 + %lo( C_LABEL(nwindowsm1) )], %l4
	sub	%l4, %l7, %l4
	add	%l5, %l4, %l5
st5:	st	%l5, [%l6 + THREAD_UWINDOWS]
	sethi	%hi( C_LABEL(eintstack) ), %l7
	bz,a	st2
	or	%l7, %lo( C_LABEL(eintstack) ), %l7
	TRAP_WIN_CLEAN
	sethi	%hi( C_LABEL(eintstack) ), %l7
	or	%l7, %lo( C_LABEL(eintstack) ), %l7
st2:	sub	%l7, 0xb0, %sp

st3:	std	%g2, [%sp + 96 + 24]
	or	%g0, %g1, %l7
	rd	%y, %l6
	std	%g4, [%sp + 96 + 32]
	andn	%l0, PSR_PIL, %l4
	sll	%l3, 0x8, %l5
	std	%g6, [%sp + 96 + 40]
	or	%l5, %l4, %l4

	wr	%l4, 0x0, %psr
	wr	%l4, PSR_ET, %psr

	std	%l0, [%sp + 96 + 0]
	std	%l2, [%sp + 96 + 8]
	st	%fp, [%sp + 96 + 16]
	
	or	%g0, 14, %o0	
	or	%g0, %g0, %o1
	call	C_LABEL(do_sparc_timer)
	nop

	or	%g0, %l7, %g1
	wr	%l6, 0x0, %y
	ldd	[%sp + 96 + 24], %g2
	ldd	[%sp + 96 + 32], %g4
	ldd	[%sp + 96 + 40], %g6
	wr	%l0, 0x0, %psr
	nop
	nop
	nop

	and	%l0, 31, %l5
	sethi	%hi(lnx_winmask), %l6
	or	%l6, %lo(lnx_winmask), %l6
	ldub	[%l6 + %l5], %l5
	andcc	%l0, PSR_PS, %g0
	bnz	1f
	rd	%wim, %l4

1:	andcc	%l5, %l4, %g0
	bnz	2f
	wr	%l0, 0x0, %psr
	nop
	nop
	nop

	jmp	%l1
	rett	%l2

2:	wr	%g0, 0x0, %wim
	nop
	nop
	nop

	restore
	restore	%g0, 0x1, %l1
	rd	%psr, %l0
	and	%l0, 31, %l0
	sll	%l1, %l0, %l1
	wr	%l1, 0x0, %wim
	sethi	%hi( C_LABEL(current) ), %l1
	ld	[%l1 + %lo( C_LABEL(current) ) ], %l1
	st	%l0, [%l1 + THREAD_WIM]
	save	%g0, %g0, %g0
	
	ldd	[%sp], %l0
	ldd	[%sp + 0x8], %l2
	ldd	[%sp + 0x10], %l4
	ldd	[%sp + 0x18], %l6
	ldd	[%sp + 0x20], %i0
	ldd	[%sp + 0x28], %i2
	ldd	[%sp + 0x30], %i4
	ldd	[%sp + 0x38], %i6
	
	save	%g0, %g0, %g0
	
	jmp	%l1
	rett	%l2


/* For now all IRQ's not registered get sent here so I can see
 * what is poking the chip.
 */

	.align 4
	.globl stray_irq_entry
stray_irq_entry:
	rd	%wim, %l4
	or	%g0, 0x1, %l5
	sll	%l5, %l0, %l5
	andcc	%l0, 0x40, %g0
	bz	tt1
	sethi	%hi( C_LABEL(eintstack) ), %l7
	andcc	%l4, %l5, %g0
	bz	tt0
	or	%l7, %lo( C_LABEL(eintstack) ), %l7
	TRAP_WIN_CLEAN
	sethi	%hi( C_LABEL(eintstack) ), %l7
	or	%l7, %lo( C_LABEL(eintstack) ), %l7
tt0:	subcc	%fp, %l7, %g0
	bg,a	tt3
	sub	%l7, 0xb0, %sp
	b	tt3
	sub	%fp, 0xb0, %sp
tt1: 	sethi	%hi( C_LABEL(current) ), %l6
	ld	[%l6 + %lo( C_LABEL(current) )], %l6
	ld	[%l6 + THREAD_WIM], %l5
	and	%l0, 0x1f, %l7
	cmp	%l5, %l7
	ble,a	tt4
	sethi	%hi( C_LABEL(nwindowsm1) ), %l4
	sub	%l5, %l7, %l7
	b	tt5
	sub	%l7, 0x1, %l5
tt4:	ld	[%l4 + %lo( C_LABEL(nwindowsm1) )], %l4
	sub	%l4, %l7, %l4
	add	%l5, %l4, %l5
tt5:	st	%l5, [%l6 + THREAD_UWINDOWS]
	sethi	%hi( C_LABEL(eintstack) ), %l7
	bz,a	tt2
	or	%l7, %lo( C_LABEL(eintstack) ), %l7
	TRAP_WIN_CLEAN
	sethi	%hi( C_LABEL(eintstack) ), %l7
	or	%l7, %lo( C_LABEL(eintstack) ), %l7
tt2:	sub	%l7, 0xb0, %sp

tt3:	std	%g2, [%sp + 96 + 24]
	or	%g0, %g1, %l7
	rd	%y, %l6
	std	%g4, [%sp + 96 + 32]
	andn	%l0, PSR_PIL, %l4
	sll	%l3, 0x8, %l5
	std	%g6, [%sp + 96 + 40]
	or	%l5, %l4, %l4

	wr	%l4, 0x0, %psr
	wr	%l4, PSR_ET, %psr

	std	%l0, [%sp + 96 + 0]
	std	%l2, [%sp + 96 + 8]
	st	%fp, [%sp + 96 + 16]
	
	or	%g0, %l3, %o0	
	or	%g0, %g0, %o1
	call	C_LABEL(unexpected_irq)
	nop

	or	%g0, %l7, %g1
	wr	%l6, 0x0, %y
	ldd	[%sp + 96 + 24], %g2
	ldd	[%sp + 96 + 32], %g4
	ldd	[%sp + 96 + 40], %g6
	wr	%l0, 0x0, %psr
	nop
	nop
	nop

	and	%l0, 31, %l5
	sethi	%hi(lnx_winmask), %l6
	or	%l6, %lo(lnx_winmask), %l6
	ldub	[%l6 + %l5], %l5
	andcc	%l0, PSR_PS, %g0
	bnz	1f
	rd	%wim, %l4

1:	andcc	%l5, %l4, %g0
	bnz	2f
	wr	%l0, 0x0, %psr
	nop
	nop
	nop

	jmp	%l1
	rett	%l2

2:	wr	%g0, 0x0, %wim
	nop
	nop
	nop

	restore
	restore	%g0, 0x1, %l1
	rd	%psr, %l0
	and	%l0, 31, %l0
	sll	%l1, %l0, %l1
	wr	%l1, 0x0, %wim
	sethi	%hi( C_LABEL(current) ), %l1
	ld	[%l1 + %lo( C_LABEL(current) ) ], %l1
	st	%l0, [%l1 + THREAD_WIM]
	save	%g0, %g0, %g0
	
	ldd	[%sp], %l0
	ldd	[%sp + 0x8], %l2
	ldd	[%sp + 0x10], %l4
	ldd	[%sp + 0x18], %l6
	ldd	[%sp + 0x20], %i0
	ldd	[%sp + 0x28], %i2
	ldd	[%sp + 0x30], %i4
	ldd	[%sp + 0x38], %i6
	
	save	%g0, %g0, %g0
	
	jmp	%l1
	rett	%l2



/* This routine is optimized for kernel window fills. User fills take about two
 * or three extra jumps on the average. We'll see how this works out.
 */

/* Don't use local labels, or if you do be REAL CAREFUL. TRAP_WIN_CLEAN is
 * full of them! If you think this routine is hairy, window spills are worse,
 * see below.
 */

	.align 4
	.globl spill_window_entry
spill_window_entry:
	andcc	%l0, 0x40, %g0		! see if this is a user window fill
	bz,a	spill_from_user
	nop

	TRAP_WIN_CLEAN			/* danger, danger... */
	wr	%l0, 0x0, %psr	
	nop
	jmp	%l1
	rett	%l2

spill_from_user:
	sethi	%hi( C_LABEL(current) ), %l6
	ld	[%l6 + %lo( C_LABEL(current) )], %l6
	ld	[%l6 + THREAD_WIM], %l5
	and	%l0, 0x1f, %l3

/* I don't know what's worse, the extra comparison here, or an extra load
 * from a lookup table, we'll see.
 */
	cmp	%l5, %l3
	ble,a	1f
	sethi	%hi( C_LABEL(nwindowsm1) ), %l4
	sub	%l5, %l3, %l3
	b	2f
	sub	%l3, 0x1, %l5
1:	ld	[%l4 + %lo( C_LABEL(nwindowsm1) )], %l4
	sub	%l4, %l3, %l4
	add	%l5, %l4, %l5
2:	st	%l5, [%l6 + THREAD_UWINDOWS]

	TRAP_WIN_CLEAN		   /* danger, danger... */
	sethi	%hi( C_LABEL(current) ), %l6
	ld	[%l6 + %lo( C_LABEL(current) )], %l6
	ld	[%l6 + THREAD_KSP], %sp
	and	%l0, 0x1f, %l3
	sethi	%hi(lnx_winmask), %l6
	or	%l6, %lo(lnx_winmask), %l6
	ldub	[%l6 + %l3], %l5
	rd	%wim, %l4
	jmp	%l1
	rett	%l2

/* A window spill has occurred.  This presents a weird situation, a restore
 * was attempted and a trap occurred. Therefore the restore attempt had no
 * effect on window movement and the trap saved, which means it went in the
 * other direction. :-( We are in a trap window which is two restores away
 * from the window we want to un-invalidate so to speak and three away from
 * the one which will become invalid after this routine. There are probably
 * bugs already this routine. Bugs suck.
 */

/* This is a very complicated and hairy routine, don't expect to understand
 * it the first time. :>
 */

	.align 4
	.globl fill_window_entry
fill_window_entry:
	wr	%g0, 0, %wim		! Can not enter invalid register without this.
	andcc	%l0, 0x40, %g0          ! From user?
	restore				! restore to where trap occurred
	bz	fill_from_user
	restore				! enter invalid register, whee...
	restore %g0, 0x1, %l1		! enter one-past invalid register
	rd	%psr, %l0		! this is the window we need to save
	and	%l0, 0x1f, %l0
	sll	%l1, %l0, %l1
	wr	%l1, 0x0, %wim
	sethi	%hi( C_LABEL(current) ), %l1
	ld	[%l1 + %lo( C_LABEL(current) )], %l1
	st	%l0, [%l1 + THREAD_WIM]
	save	%g0, %g0, %g0		! back to invalid register
	ldd	[%sp], %l0		! load the window from stack
	ldd	[%sp + 8], %l2
	ldd	[%sp + 16], %l4
	ldd	[%sp + 24], %l6
	ldd	[%sp + 32], %i0
	ldd	[%sp + 40], %i2
	ldd	[%sp + 48], %i4
	ldd	[%sp + 56], %i6
	save	%g0, %g0, %g0		! to window where trap happened
	save	%g0, %g0, %g0		! back to trap window, so rett works
	wr	%l0, 0x0, %psr		! load condition codes
	nop
	jmp	%l1
	rett	%l2			! are you as confused as I am?

fill_from_user:
	andcc	%sp, 0x7, %g0		! check for alignment of user stack
	bne	fill_bad_stack
	sra	%sp, 0x1e, %l7
	cmp	%l7, 0x0
	be,a	1f
	andn	%sp, 0xfff, %l7
	cmp	%l7, -1
	bne	fill_bad_stack
	andn	%sp, 0xfff, %l7
1:	lda	[%l7] ASI_PTE, %l7
	srl	%l7, 0x1d, %l7
	andn	%l7, 0x2, %l7
	cmp	%l7, 0x4
	bne	fill_bad_stack
	and	%sp, 0xfff, %l7
	cmp	%l7, 0xfc1
	bl,a	fill_stack_ok
	restore	%g0, 1, %l1
	add	%sp, 0x38, %l5
	sra	%sp, 0x1e, %l7
	cmp	%l7, 0x0
	be,a	1f
	andn	%sp, 0xfff, %l7
	cmp	%l7, -1
	bne	fill_bad_stack
	andn	%sp, 0xfff, %l7
1:	lda	[%l7] ASI_PTE, %l7
	srl	%l7, 0x1d, %l7
	andn	%l7, 0x2, %l7
	cmp	%l7, 0x4
	be,a	fill_stack_ok
	restore	%g0, 0x1, %l1

fill_bad_stack:
	save	%g0, %g0, %g0			! save to where restore happened
	save	%g0, 0x1, %l4			! save is an add remember? to trap window
	sethi	%hi( C_LABEL(current) ), %l6
	ld	[%l6 + %lo( C_LABEL(current) )], %l6
	st	%l4, [%l6 + THREAD_UWINDOWS]	! update current->tss values
	ld	[%l6 + THREAD_WIM], %l5
	sll	%l4, %l5, %l4
	wr	%l4, 0x0, %wim
	ld	[%l6 + THREAD_KSP], %sp		! set to kernel stack pointer
	wr	%l0, 0x20, %psr			! turn off traps
	std	%l0, [%sp + C_STACK]		! set up thread_frame on stack
	rd	%y, %l3
	std	%l2, [%sp + C_STACK + 0x8]
	or	%g0, 0x6, %o0			! so _sparc_trap knows what to do
	st	%g1, [%sp + C_STACK + 0x14]	! no need to save %g0, always zero
	or	%g0, %l0, %o1
	std	%g2, [%sp + C_STACK + 0x18]
	or	%g0, %l1, %o2
	std	%g4, [%sp + C_STACK + 0x20]
	add	%sp, C_STACK, %o3
	std	%g6, [%sp + C_STACK + 0x28]
	std	%i0, [%sp + C_STACK + 0x30]
	std	%i2, [%sp + C_STACK + 0x38]
	std	%i4, [%sp + C_STACK + 0x40]
	call	sparc_trap
	std	%i6, [%sp + C_STACK + 0x48]
	
	ldd	[%sp + C_STACK], %l0
	ldd	[%sp + C_STACK + 0x8], %l2
	wr	%l3, 0, %y
	ld	[%sp + C_STACK + 0x14], %g1
	ldd	[%sp + C_STACK + 0x18], %g2
	ldd	[%sp + C_STACK + 0x20], %g4
	ldd	[%sp + C_STACK + 0x28], %g6
	ldd	[%sp + C_STACK + 0x30], %i0
	ldd	[%sp + C_STACK + 0x38], %i2
	ldd	[%sp + C_STACK + 0x40], %i4
	wr	%l0, 0, %psr			! disable traps again
	ldd	[%sp + C_STACK + 0x48], %i6
	sethi	%hi( C_LABEL(current) ), %l6
	ld	[%l6 + %lo( C_LABEL(current) )], %l6
	ld	[%l6 + THREAD_W_SAVED], %l7
	cmp	%l7, 0x0
	bl,a	1f
	wr	%g0, 0x0, %wim
	b,a	leave_trap

1:	or	%g0, %g6, %l3
	or	%g0, %l6, %g6
	st	%g0, [%g6 + THREAD_W_SAVED]
	restore	%g0, %g0, %g0
	restore	%g0, %g0, %g0
	restore	%g0, 0x1, %l1
	rd	%psr, %l0
	sll	%l1, %l0, %l1
	wr	%l1, 0x0, %wim
	and	%l0, 0x1f, %l0
	st	%l0, [%g6 + THREAD_WIM]
	nop
	save	%g0, %g0, %g0
	ldd	[%sp], %l0			! load number one
	ldd	[%sp + 0x8], %l2
	ldd	[%sp + 0x10], %l4
	ldd	[%sp + 0x18], %l6
	ldd	[%sp + 0x20], %i0
	ldd	[%sp + 0x28], %i2
	ldd	[%sp + 0x30], %i4
	ldd	[%sp + 0x38], %i6
	save	%g0, %g0, %g0
	ldd	[%sp], %l0			! load number two
	ldd	[%sp + 0x8], %l2	
	ldd	[%sp + 0x10], %l4
	ldd	[%sp + 0x18], %l6
	ldd	[%sp + 0x20], %i0
	ldd	[%sp + 0x28], %i2
	ldd	[%sp + 0x30], %i4
	ldd	[%sp + 0x38], %i6
	save	%g0, %g0, %g0			! re-enter trap window
	wr	%l0, 0x0, %psr			! restore condition codes
	or	%g0, %l3, %g6			! restore scratch register
	jmp	%l1
	rett	%l2

fill_stack_ok:
	rd	%psr, %l0
	sll	%l1, %l0, %l1
	wr	%l1, 0x0, %wim
	sethi	%hi( C_LABEL(current) ), %l2
	ld	[%l2 + %lo( C_LABEL(current) )], %l2
	and	%l0, 0x1f, %l0
	st	%l0, [%l2 + THREAD_WIM]
	save	%g0, %g0, %g0
	ldd	[%sp], %l0			! only one load necessary
	ldd	[%sp + 0x8], %l2
	ldd	[%sp + 0x10], %l4
	ldd	[%sp + 0x18], %l6
	ldd	[%sp + 0x20], %i0
	ldd	[%sp + 0x28], %i2
	ldd	[%sp + 0x30], %i4
	ldd	[%sp + 0x38], %i6
	save	%g0, %g0, %g0
	save	%g0, %g0, %g0			! save into trap window
	wr	%l0, 0x0, %psr			! local number 0 here has cond codes
	nop
	jmp	%l1
	rett	%l2

	.align 4
	.globl trap_entry
trap_entry:
	TRAP_WIN_CLEAN
	jmp	%l1
	rett	%l2

	.align 4
	.globl linux_trap_nmi
linux_trap_nmi:
	TRAP_WIN_CLEAN
	jmp	%l1
	rett	%l2

	.align 4
	.globl sparc_trap
sparc_trap:
	TRAP_WIN_CLEAN
	jmp	%l1
	rett	%l2

	.align 4
	.globl leave_trap
leave_trap:
	jmp	%l1
	rett	%l2

/* The following two things point to window management tables. The first
   one is used to quickly look up how many user windows there are from
   trap-land. The second is used in a trap handler to determine if a rett
   instruction will land us smack inside the invalid window that possibly
   the trap was called to fix-up.
*/

/* For now these are static tables geared for a 7 window sparc. */

		.data
		.align 4
lnx_winmask:	.byte	2, 4, 8, 16, 32, 64, 128, 1  ! lnx_winmask[0..7]

	
		.align 4
		.globl C_LABEL(sys_call_table)
C_LABEL(sys_call_table):
	.long C_LABEL(sys_setup)		/* 0 */
	.long C_LABEL(sys_exit)
	.long C_LABEL(sys_fork)
	.long C_LABEL(sys_read)
	.long C_LABEL(sys_write)
	.long C_LABEL(sys_open)			/* 5 */
	.long C_LABEL(sys_close)
	.long C_LABEL(sys_waitpid)
	.long C_LABEL(sys_creat)
	.long C_LABEL(sys_link)
	.long C_LABEL(sys_unlink)		/* 10 */
	.long C_LABEL(sys_execve)
	.long C_LABEL(sys_chdir)
	.long C_LABEL(sys_time)
	.long C_LABEL(sys_mknod)
	.long C_LABEL(sys_chmod)		/* 15 */
	.long C_LABEL(sys_chown)
	.long C_LABEL(sys_break)
	.long C_LABEL(sys_stat)
	.long C_LABEL(sys_lseek)
	.long C_LABEL(sys_getpid)		/* 20 */
	.long C_LABEL(sys_mount)
	.long C_LABEL(sys_umount)
	.long C_LABEL(sys_setuid)
	.long C_LABEL(sys_getuid)
	.long C_LABEL(sys_stime)		/* 25 */
	.long C_LABEL(sys_ni_syscall)		/* this will be sys_ptrace() */
	.long C_LABEL(sys_alarm)
	.long C_LABEL(sys_fstat)
	.long C_LABEL(sys_pause)
	.long C_LABEL(sys_utime)		/* 30 */
	.long C_LABEL(sys_stty)
	.long C_LABEL(sys_gtty)
	.long C_LABEL(sys_access)
	.long C_LABEL(sys_nice)
	.long C_LABEL(sys_ftime)		/* 35 */
	.long C_LABEL(sys_sync)
	.long C_LABEL(sys_kill)
	.long C_LABEL(sys_rename)
	.long C_LABEL(sys_mkdir)
	.long C_LABEL(sys_rmdir)		/* 40 */
	.long C_LABEL(sys_dup)
	.long C_LABEL(sys_pipe)
	.long C_LABEL(sys_times)
	.long C_LABEL(sys_prof)
	.long C_LABEL(sys_brk)			/* 45 */
	.long C_LABEL(sys_setgid)
	.long C_LABEL(sys_getgid)
	.long C_LABEL(sys_signal)
	.long C_LABEL(sys_geteuid)
	.long C_LABEL(sys_getegid)		/* 50 */
	.long C_LABEL(sys_acct)
	.long C_LABEL(sys_phys)
	.long C_LABEL(sys_lock)
	.long C_LABEL(sys_ioctl)
	.long C_LABEL(sys_fcntl)		/* 55 */
	.long C_LABEL(sys_mpx)
	.long C_LABEL(sys_setpgid)
	.long C_LABEL(sys_ulimit)
	.long C_LABEL(sys_olduname)
	.long C_LABEL(sys_umask)	/* 60 */
	.long C_LABEL(sys_chroot)
	.long C_LABEL(sys_ustat)
	.long C_LABEL(sys_dup2)
	.long C_LABEL(sys_getppid)
	.long C_LABEL(sys_getpgrp)		/* 65 */
	.long C_LABEL(sys_setsid)
	.long C_LABEL(sys_sigaction)
	.long C_LABEL(sys_sgetmask)
	.long C_LABEL(sys_ssetmask)
	.long C_LABEL(sys_setreuid)		/* 70 */
	.long C_LABEL(sys_setregid)
	.long C_LABEL(sys_sigsuspend)
	.long C_LABEL(sys_sigpending)
	.long C_LABEL(sys_sethostname)
	.long C_LABEL(sys_setrlimit)		/* 75 */
	.long C_LABEL(sys_getrlimit)
	.long C_LABEL(sys_getrusage)
	.long C_LABEL(sys_gettimeofday)
	.long C_LABEL(sys_settimeofday)
	.long C_LABEL(sys_getgroups)		/* 80 */
	.long C_LABEL(sys_setgroups)
	.long C_LABEL(sys_select)
	.long C_LABEL(sys_symlink)
	.long C_LABEL(sys_lstat)
	.long C_LABEL(sys_readlink)		/* 85 */
	.long C_LABEL(sys_uselib)
	.long C_LABEL(sys_swapon)
	.long C_LABEL(sys_reboot)
	.long C_LABEL(sys_readdir)
	.long C_LABEL(sys_mmap)			/* 90 */
	.long C_LABEL(sys_munmap)
	.long C_LABEL(sys_truncate)
	.long C_LABEL(sys_ftruncate)
	.long C_LABEL(sys_fchmod)
	.long C_LABEL(sys_fchown)		/* 95 */
	.long C_LABEL(sys_getpriority)
	.long C_LABEL(sys_setpriority)
	.long C_LABEL(sys_profil)
	.long C_LABEL(sys_statfs)
	.long C_LABEL(sys_fstatfs)		/* 100 */
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_socketcall)
	.long C_LABEL(sys_syslog)
	.long C_LABEL(sys_setitimer)
	.long C_LABEL(sys_getitimer)		/* 105 */
	.long C_LABEL(sys_newstat)
	.long C_LABEL(sys_newlstat)
	.long C_LABEL(sys_newfstat)
	.long C_LABEL(sys_uname)
	.long C_LABEL(sys_ni_syscall)		/* 110 */
	.long C_LABEL(sys_vhangup)
	.long C_LABEL(sys_idle)
	.long C_LABEL(sys_ni_syscall)           /* was vm86, meaningless on Sparc */
	.long C_LABEL(sys_wait4)
	.long C_LABEL(sys_swapoff)		/* 115 */
	.long C_LABEL(sys_sysinfo)
	.long C_LABEL(sys_ipc)
	.long C_LABEL(sys_fsync)
	.long C_LABEL(sys_sigreturn)
	.long C_LABEL(sys_ni_syscall)		/* 120 */
	.long C_LABEL(sys_setdomainname)
	.long C_LABEL(sys_newuname)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_adjtimex)
	.long C_LABEL(sys_mprotect)		/* 125 */
	.long C_LABEL(sys_sigprocmask)
	.long C_LABEL(sys_create_module)
	.long C_LABEL(sys_init_module)
	.long C_LABEL(sys_delete_module)
	.long C_LABEL(sys_get_kernel_syms)	/* 130 */
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_getpgid)
	.long C_LABEL(sys_fchdir)
	.long C_LABEL(sys_bdflush)
	.long C_LABEL(sys_sysfs)	/* 135 */
	.long C_LABEL(sys_personality)
	.long 0				/* for afs_syscall */
	.long C_LABEL(sys_setfsuid)
	.long C_LABEL(sys_setfsgid)
	.long C_LABEL(sys_llseek)		/* 140 */
	.align 4
