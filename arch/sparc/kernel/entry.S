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
	sethi	%hi(_current), %g6; \
	ld	[%g6 + %lo(_current)], %g6; \
	ld	[%g6 + THREAD_UWINDOWS], %g7; /* how many user wins are active? */ \
	subcc	%g7, 0x0, %g0
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
8:                                              /* We are done when we get here. */ \


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
1:	sethi	%hi(_current), %l6; \
	ld	[%l6 + %lo(_current)], %l6; \
	ld	[%l6 + THREAD_WIM], %l5; \
	and	%l0, 0x1f, %l4; \
	cmp	%l5, %l3; \
	ble,a	4f; \
	sethi	%hi(_nwindowsm1), %l4; \
	sub	%l5, %l3, %l3; \
	b	5f; \
	sub	%l3, 0x1, %l5; \
4:	ld	[%l4 + %lo(_nwindowsm1)], %l4; \
	sub	%l4, %l3, %l4; \
	add	%l5, %l4, %l5; \
5:	st	%l5, [%l6 + THREAD_UWINDOWS]; \
	bz,a	2f; \
	sethi	%hi(TASK_SIZE-176), %l5; \
	TRAP_WIN_CLEAN; \
	sethi	%hi(_current), %l6; \
	ld	[%l6 + %lo(_current)], %l6; \
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
	bz,a	0f; \
	sethi	%hi(_eintstack), %l7; \
	TRAP_WIN_CLEAN \
	sethi	%hi(_eintstack), %l7; \
0:	cmp	%fp, %l7; \
	bge,a	3f; \
	sub	%l7, 0xb0, %sp; \
	b	3f; \
	sub	%fp, 0xb0, %sp; \
1: 	sethi	%hi(_current), %l6; \
	ld	[%l6 + %lo(_current)], %l6; \
	ld	[%l6 + PCB_WIM], %l5; \
	and	%l0, 0x1f, %l7; \
	cmp	%l5, %l7; \
	ble,a	4f; \
	sethi	%hi(_nwindowsm1), %l4; \
	sub	%l5, %l7, %l7; \
	b	5f; \
	sub	%l7, 0x1, %l5; \
4:	ld	[%l4 + %lo(_nwindowsm1)], %l4; \
	sub	%l4, %l7, %l4; \
	add	%l5, %l4, %l5; \
5:	st	%l5, [%l6 + THREAD_UWINDOWS]; \
	bz,a	2f; \
	sethi	%hi(_eintstack), %l7; \
	TRAP_WIN_CLEAN; \
	sethi	%hi(_eintstack), %l7; \
2: \
	sub	%l7, 0xb0, %sp; \
3: \

	.text
	.align 4

/* Default trap handler */
	.globl my_trap_handler
my_trap_handler:
		rd %wim, %l4
		or %g0, 0x1, %l5
		sll %l5, %l0, %l5
		cmp %l4, %l5        ! are we in the invalid window?
	
		TRAP_WIN_CLEAN

		nop
		or %g0, %l3, %o0
		call _do_hw_interrupt
		or %g0, %g0, %o1
		wr %l0, 0x20, %psr  ! re-enable traps and reset the condition codes
		nop
		nop
		nop                 ! click our heels three times, "no place like home"
		jmp %l1
		rett %l2
	
/* This routine is optimized for kernel window fills. User fills take about two
 * or three extra jumps on the average. We'll see how this works out.
 */

/* Don't use local labels, or if you do be REAL CAREFUL. TRAP_WIN_CLEAN is
 * full of them! If you think this routine is hairy, window spills are worse,
 * see below.
 */

	.globl fill_window_entry
fill_window_entry:
	andcc	%l0, 0x40, %g0		! see if this is a user window fill
	bz,a	fill_from_user
	nop

	TRAP_WIN_CLEAN			/* danger, danger... */
	wr	%l0, 0x0, %psr	
	nop
	jmp	%l1
	rett	%l2

fill_from_user:
	sethi	%hi(_current), %l6
	ld	[%l6 + %lo(_current)], %l6
	ld	[%l6 + THREAD_WIM], %l5
	and	%l0, 0x1f, %l3

/* I don't know what's worse, the extra comparison here, or an extra load
 * from a lookup table, we'll see.
 */
	cmp	%l5, %l3
	ble,a	1f
	sethi	%hi(_nwindowsm1), %l4
	sub	%l5, %l3, %l3
	b	2f
	sub	%l3, 0x1, %l5
1:	ld	[%l4 + %lo(_nwindowsm1)], %l4
	sub	%l4, %l3, %l4
	add	%l5, %l4, %l5
2:	st	%l5, [%l6 + THREAD_UWINDOWS]

	TRAP_WIN_CLEAN		   /* danger, danger... */
	sethi	%hi(_current), %l6
	ld	[%l6 + %lo(_current)], %l6
	ld	[%l6 + THREAD_KSP], %sp
	and	%l0, 0x1f, %l3
	sethi	%hi(lnx_winmask), %l6
	or	%l6, %lo(lnx_winmask), %l6
	ldub	[%l6 + %l3], %l5
	b	back_to_userland_safety		! may need a sched()
	rd	%wim, %l4

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

	.globl spill_window_entry
spill_window_entry:
	wr	%g0, 0, %wim		! Can not enter invalid register without this.
	andcc	%l0, 0x40, %g0          ! From user?
	restore				! restore to where trap occurred
	bz	spill_from_user
	restore				! enter invalid register, whee...
	restore %g0, 0x1, %l1		! enter one-past invalid register
	rd	%psr, %l0		! this is the window we need to save
	and	%l0, 0x1f, %l0
	sll	%l1, %l0, %l1
	wr	%l1, 0x0, %wim
	sethi	%hi(_current), %l1
	ld	[%l1 + %lo(_current)], %l1
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

spill_from_user:
	andcc	%sp, 0x7, %g0		! check for alignment of user stack
	bne	spill_bad_stack
	sra	%sp, 0x1e, %l7
	cmp	%l7, 0x0
	be,a	1f
	andn	%sp, 0xfff, %l7
	cmp	%l7, -1
	bne	spill_bad_stack
	andn	%sp, 0xfff, %l7
1:	lda	[%l7] ASI_PTE, %l7
	srl	%l7, 0x1d, %l7
	andn	%l7, 0x2, %l7
	cmp	%l7, 0x4
	bne	spill_bad_stack
	and	%sp, 0xfff, %l7
	cmp	%l7, 0xfc1
	bl,a	spill_stack_ok
	restore	%g0, 1, %l1
	add	%sp, 0x38, %l5
	sra	%sp, 0x1e, %l7
	cmp	%l7, 0x0
	be,a	1f
	andn	%sp, 0xfff, %l7
	cmp	%l7, -1
	bne	spill_bad_stack
	andn	%sp, 0xfff, %l7
1:	lda	[%l7] ASI_PTE, %l7
	srl	%l7, 0x1d, %l7
	andn	%l7, 0x2, %l7
	cmp	%l7, 0x4
	be,a	spill_stack_ok
	restore	%g0, 0x1, %l1

spill_bad_stack:
	save	%g0, %g0, %g0			! save to where restore happened
	save	%g0, 0x1, %l4			! save is an add remember? to trap window
	sethi	%hi(_current), %l6
	ld	[%l6 + %lo(_current)], %l6
	st	%l4, [%l6 + THREAD_UWINDOWS]	! update current->tss values
	ld	[%l6 + THREAD_WIN], %l5
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
	call	_sparc_trap
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
	sethi	%hi(_current), %l6
	ld	[%l6 + %lo(_current)], %l6
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

spill_stack_ok:
	rd	%psr, %l0
	sll	%l1, %l0, %l1
	wr	%l1, 0x0, %wim
	sethi	%hi(_current), %l2
	ld	[%l2 + %lo(_current)], %l2
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

/* The following two things point to window management tables. The first
   one is used to quickly look up how many user windows there are from
   trap-land. The second is used in a trap handler to determine if a rett
   instruction will land us smack inside the invalid window that possibly
   the trap was called to fix-up.
*/

/* For now these are static tables geared for a 7 window sparc. */

		.data
		.align 4
lnx_winmask:	.byte	2, 4, 8, 16, 32, 64, 128,1  ! lnx_winmask[0..7]

	
