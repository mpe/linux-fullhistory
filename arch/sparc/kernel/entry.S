/* arch/sparc/kernel/entry.S:  Sparc trap low-level entry points.
 *
 * Sparc traps are so ugly, this code is going to go through a lot
 * of changes as I find out more interesting things. See head.S for
 * the trap table and how it works, this will show you how we get
 * to these routines.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */


#include <asm/head.h>
#include <asm/asi.h>
#include <asm/contregs.h>
#include <asm/psr.h>
#include <asm/cprefix.h>
#include <asm/vaddrs.h>
#include <asm/memreg.h>
#include <asm/page.h>

#define NR_SYSCALLS 255      /* Each OS is different... */

/* A debugging macro, it just prints a dot on the screen.  For now
 * it is only used in the context switch code since it is so delicate
 * I need to use the prom putchar() routines and reload the pointers
 * every time.  This clobbers %l7 and %o0.
 */
#define DO_SPARC_DOT \
	sethi	%hi(C_LABEL(romvec)), %l7; \
	ld	[%l7 + %lo(C_LABEL(romvec))], %l7; \
	ld	[%l7 + 0x54], %l7; \
	or	%g0, '.', %o0; \
	call	%l7; \
	nop; \
	nop; nop; nop; nop; nop; nop; nop; nop; nop; nop; nop; nop; \

/* Another macro, the name speaks for itself. */
#define DROP_THE_BOMB \
	sethi	%hi(C_LABEL(romvec)), %l7;  \
	ld	[%l7 + %lo(C_LABEL(romvec))], %l7;  \
	ld	[%l7 + 0x74], %l7;  \
	call	%l7;  \
	nop;  \

/* Turn off interrupts while we change contexts. Clobbers %l7 */
#define TRAPS_OFF \
	rd	%psr, %l7; \
	andn	%l7, (PSR_ET), %l7; \
	wr	%l7, 0x0, %psr; \

/* Here are macros for routines we do often, this allows me to inline this
 * without making the code look real ugly.  Well, the macro looks ugly too
 * but makes the trap entry code easier to understand.
 */

/* I really don't like synthetic instructions. So I avoid them like the
 * plague.
 */

/* Note that when I have to write a window out, and it is a user's window, I
 * have to check that the pages of memory that I am going to throw the window(s)
 * onto are valid and are writable by the user (this is %sp to %sp + 64) before
 * I start dumping stuff there. We always assume that kernels stack is ok.
 * XXX Will change on MP's XXX
 *
 * If we have to save a kernel window, only one branch is taken. This should
 * make trap handlers quicker in this scenario.
 *
 * Once 'current' is loaded into %g6, it stays there until we leave
 * this macro.
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
	sethi	%hi( C_LABEL(current) ), %g6; \
	ld	[%g6 + %lo( C_LABEL(current) )], %g6; \
	st	%g7, [%g6 + THREAD_WIM];        /* save 'this' threads mask */ \
	restore	%g0, %g0, %g0; \
	or	%g0, %l5, %g5;                  /* restore the globals we used */ \
	or	%g0, %l6, %g6; \
	b	8f;                             /* we are done */ \
	or	%g0, %l7, %g7; \
2:	sub	%g7, 0x1, %g7; \
	sethi	%hi( C_LABEL(current) ), %g6; \
	ld	[%g6 + %lo( C_LABEL(current) )], %g6; \
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
 *
 * See asm-sparc/cprefix.h to see how the CONCAT macros work.
 */
/* NOTE: The system call entry code ASSUMES that the ENTER_TRAP macro
 *       does NOT touch register %l7 and leaves the same contents after
 *       the macro is done. Keep in mind if you change this code.
 */

#define ENTER_TRAP(label) \
	rd	%wim, %l4; \
	or	%g0, 0x1, %l5; \
	sll	%l5, %l0, %l5; \
	andcc	%l0, PSR_PS, %g0; \
	bz	CONCAT1(label, 1); \
	andcc	%l4, %l5, %g0; \
	bz,a	CONCAT1(label, 3); \
	sub	%fp, 0xb0, %sp; \
	TRAP_WIN_CLEAN \
	b	CONCAT1(label, 3); \
	sub	%fp, 0xb0, %sp; \
CONCAT1(label, 1): \
	sethi	%hi( C_LABEL(current) ), %l6; \
	ld	[%l6 + %lo( C_LABEL(current) )], %l6; \
	ld	[%l6 + THREAD_WIM], %l5; \
	and	%l0, 0x1f, %l4; \
	cmp	%l5, %l3; \
	ble,a	CONCAT1(label, 4); \
	sethi	%hi( C_LABEL(nwindowsm1) ), %l4; \
	sub	%l5, %l3, %l3; \
	b	CONCAT1(label, 5); \
	sub	%l3, 0x1, %l5; \
CONCAT1(label, 4): \
	ld	[%l4 + %lo( C_LABEL(nwindowsm1) )], %l4; \
	sub	%l4, %l3, %l4; \
	add	%l5, %l4, %l5; \
CONCAT1(label, 5): \
	st	%l5, [%l6 + THREAD_UWINDOWS]; \
	bz,a	CONCAT1(label, 2); \
	sethi	%hi(C_LABEL(current)), %l6; \
	TRAP_WIN_CLEAN \
	sethi	%hi(C_LABEL(current)), %l6; \
CONCAT1(label, 2): \
	ld	[%l6 + %lo(C_LABEL(current))], %l6; \
	ld	[%l6 + TASK_KSTACK_PG], %l6; \
	add	%l6, (PAGE_SIZE - 96 - 80), %l6; \
	andn	%l6, 0x7, %l6; \
	or	%g0, %l6, %sp; \
CONCAT1(label, 3):

/* What needs to be executed upon entry to an interrupt.
 *
 * See asm-sparc/cprefix.h to see how the CONCAT macros work.
 */

#define ENTER_IRQ(label) \
	rd	%wim, %l4; \
	or	%g0, 0x1, %l5; \
	sll	%l5, %l0, %l5; \
	andcc	%l0, PSR_PS, %g0; \
	bz	CONCAT1(label, 1); \
	andcc	%l4, %l5, %g0; \
	bz,a	CONCAT1(label, 0); \
	sethi	%hi( C_LABEL(eintstack) ), %l7; \
	TRAP_WIN_CLEAN \
	sethi	%hi( C_LABEL(eintstack) ), %l7; \
CONCAT1(label, 0): \
	subcc	%fp, %l7, %g0; \
	bge,a	CONCAT1(label, 3); \
	sub	%l7, 0xb0, %sp; \
	b	CONCAT1(label, 3); \
	sub	%fp, 0xb0, %sp; \
CONCAT1(label, 1): \
	sethi	%hi( C_LABEL(current) ), %l6; \
	ld	[%l6 + %lo( C_LABEL(current) )], %l6; \
	ld	[%l6 + THREAD_WIM], %l5; \
	and	%l0, 0x1f, %l7; \
	cmp	%l5, %l7; \
	ble,a	CONCAT1(label, 4); \
	sethi	%hi( C_LABEL(nwindowsm1) ), %l4; \
	sub	%l5, %l7, %l7; \
	b	CONCAT1(label, 5); \
	sub	%l7, 0x1, %l5; \
CONCAT1(label, 4): \
	ld	[%l4 + %lo( C_LABEL(nwindowsm1) )], %l4; \
	sub	%l4, %l7, %l4; \
	add	%l5, %l4, %l5; \
CONCAT1(label, 5): \
	st	%l5, [%l6 + THREAD_UWINDOWS]; \
	bz,a	CONCAT1(label, 2); \
	sethi	%hi( C_LABEL(eintstack) ), %l7; \
	TRAP_WIN_CLEAN \
	sethi	%hi( C_LABEL(eintstack) ), %l7; \
CONCAT1(label, 2): \
	sub	%l7, 0xb0, %sp; \
CONCAT1(label, 3):


	.text
	.align 4

/* Bad trap handler */
	.globl bad_trap_handler
bad_trap_handler:
	ENTER_TRAP(bad_trap_handler)

	or	%g0, %l3, %o0
	or	%g0, %l0, %o1
	call	C_LABEL(do_hw_interrupt)
	or	%g0, %l1, %o2

	jmp	%l1
	rett %	l2
	
	.align 4
	.globl sparc_timer
sparc_timer:
	ENTER_IRQ(sparc_timer)

	sethi	%hi(C_LABEL(master_l10_limit)), %l4
	ld	[%l4 + %lo(C_LABEL(master_l10_limit))], %l4
	ld	[%l4], %g0			! read the limit register

	std	%g2, [%sp + C_STACK + PT_G2]
	or	%g0, %g1, %l7
	rd	%y, %l6
	std	%g4, [%sp + C_STACK + PT_G4]
	andn	%l0, PSR_PIL, %l4
/*	sll	%l3, 0x8, %l5 */
	std	%g6, [%sp + C_STACK + PT_G6]
/*	or	%l5, %l4, %l4 */

	/* Magic, we can't increase PIL and set ET at the same
	 * time or the chip calls prom_panic().
	 */
/*	wr	%l4, 0x0, %psr */
	wr	%l4, PSR_ET, %psr

	or	%g0, 10, %o0
	add	%sp, C_STACK, %o1
	call	C_LABEL(do_IRQ)
	nop

	or	%g0, %l7, %g1
	wr	%l6, 0x0, %y
	ldd	[%sp + C_STACK + PT_G2], %g2
	ldd	[%sp + C_STACK + PT_G4], %g4
	ldd	[%sp + C_STACK + PT_G6], %g6

	b	ret_trap_entry
	wr	%l0, 0x0, %psr

/* For now all IRQ's not registered get sent here. handler_irq() will
 * see if a routine is registered to handle this interrupt and if not
 * it will say so on the console.
 */

	.align 4
	.globl real_irq_entry
real_irq_entry:
	ENTER_IRQ(real_irq_entry)
	std	%g2, [%sp + C_STACK + PT_G2]
	or	%g0, %g1, %l7
	rd	%y, %l6
	std	%g4, [%sp + C_STACK + PT_G4]
	andn	%l0, PSR_PIL, %l4
	sll	%l3, 0x8, %l5
	std	%g6, [%sp + C_STACK + PT_G6]
	or	%l5, %l4, %l4

	wr	%l4, 0x0, %psr
	wr	%l4, PSR_ET, %psr

	std	%l0, [%sp + C_STACK + PT_PSR]
	std	%l2, [%sp + C_STACK + PT_NPC]
	
	or	%g0, %l3, %o0	
	add	%sp, C_STACK, %o1
	call	C_LABEL(handler_irq)
	nop

	or	%g0, %l7, %g1
	wr	%l6, 0x0, %y
	ldd	[%sp + C_STACK + PT_G2], %g2
	ldd	[%sp + C_STACK + PT_G4], %g4
	ldd	[%sp + C_STACK + PT_G6], %g6
	b	ret_trap_entry
	wr	%l0, 0x0, %psr


/* This routine is optimized for kernel window fills. User fills take about two
 * or three extra jumps on the average. We'll see how this works out.
 */

/* Don't use local labels, or if you do be REAL CAREFUL. TRAP_WIN_CLEAN is
 * full of them! If you think this routine is hairy, window fills are worse,
 * see below.
 */

	.align 4
	.globl spill_window_entry
spill_window_entry:
	andcc	%l0, PSR_PS, %g0	! see if this is a user window fill
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
1:
	ld	[%l4 + %lo( C_LABEL(nwindowsm1) )], %l4
	sub	%l4, %l3, %l4
	add	%l5, %l4, %l5
2:
	st	%l5, [%l6 + THREAD_UWINDOWS]

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
1:
	lda	[%l7] ASI_PTE, %l7
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
1:
	lda	[%l7] ASI_PTE, %l7
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
	wr	%l0, PSR_ET, %psr		! turn off traps
	std	%l0, [%sp + C_STACK]		! set up thread_frame XXX
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
	nop					/* SHould trap here */
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
	/* Should trap here */

1:
	or	%g0, %g6, %l3
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

	/* This routine handles illegal isntructions and privileged
	 * instruction attempts from user code.
	 */
	.align 4
	.globl bad_instruction
bad_instruction:
	ENTER_TRAP(bad_instruction)
	st	%g1, [%sp + C_STACK + PT_G1]
	rd	%y, %l4
	std	%g2, [%sp + C_STACK + PT_G2]
	std	%g4, [%sp + C_STACK + PT_G4]
	std	%g6, [%sp + C_STACK + PT_G6]
	wr	%l0, PSR_ET, %psr		! re-enable traps
	add	%sp, C_STACK, %o0
	mov	%l1, %o1
	mov	%l2, %o2
	cmp	%l3, 0x2
	bne	1f
	call	C_LABEL(do_illegal_instruction)
	mov	%l0, %o3
	b	2f
	ld	[%sp + C_STACK + PT_G1], %g1

1:
	call	C_LABEL(do_priv_instruction)
	mov	%l0, %o3
	ld	[%sp + C_STACK + PT_G1], %g1
2:
	wr	%l0, 0x0, %psr
	ldd	[%sp + C_STACK + PT_G2], %g2
	ldd	[%sp + C_STACK + PT_G4], %g4
	ldd	[%sp + C_STACK + PT_G6], %g6
	b	ret_trap_entry
	wr	%l4, 0, %y

	/* This routine handles unaligned data accesses.
	 */
	.align 4
	.globl mna_handler
mna_handler:
	ENTER_TRAP(mna_handler)
	st	%g1, [%sp + C_STACK + PT_G1]
	rd	%y, %l4
	std	%g2, [%sp + C_STACK + PT_G2]
	std	%g4, [%sp + C_STACK + PT_G4]
	std	%g6, [%sp + C_STACK + PT_G6]
	wr	%l0, PSR_ET, %psr		! re-enable traps
	add	%sp, C_STACK, %o0
	mov	%l1, %o1
	mov	%l2, %o2
	call	C_LABEL(do_memaccess_unaligned)
	mov	%l0, %o3

	ld	[%sp + C_STACK + PT_G1], %g1
	wr	%l0, 0x0, %psr
	ldd	[%sp + C_STACK + PT_G2], %g2
	ldd	[%sp + C_STACK + PT_G4], %g4
	ldd	[%sp + C_STACK + PT_G6], %g6
	b	ret_trap_entry
	wr	%l4, 0, %y

	/* This routine handles floating point disabled traps.
	 */
	.align 4
	.globl fpd_trap_handler
fpd_trap_handler:
	ENTER_TRAP(fpd_trap_handler)
	st	%g1, [%sp + C_STACK + PT_G1]
	rd	%y, %l4
	std	%g2, [%sp + C_STACK + PT_G2]
	std	%g4, [%sp + C_STACK + PT_G4]
	std	%g6, [%sp + C_STACK + PT_G6]
	wr	%l0, PSR_ET, %psr		! re-enable traps
	add	%sp, C_STACK, %o0
	mov	%l1, %o1
	mov	%l2, %o2
	call	C_LABEL(do_fpd_trap)
	mov	%l0, %o3

	ld	[%sp + C_STACK + PT_G1], %g1
	wr	%l0, 0x0, %psr
	ldd	[%sp + C_STACK + PT_G2], %g2
	ldd	[%sp + C_STACK + PT_G4], %g4
	ldd	[%sp + C_STACK + PT_G6], %g6
	b	ret_trap_entry
	wr	%l4, 0, %y

	/* This routine handles Floating Point Exceptions.
	 */
	.align 4
	.globl fpe_trap_handler
fpe_trap_handler:
	ENTER_TRAP(fpe_trap_handler)
	st	%g1, [%sp + C_STACK + PT_G1]
	rd	%y, %l4
	std	%g2, [%sp + C_STACK + PT_G2]
	std	%g4, [%sp + C_STACK + PT_G4]
	std	%g6, [%sp + C_STACK + PT_G6]
	wr	%l0, PSR_ET, %psr		! re-enable traps
	add	%sp, C_STACK, %o0
	mov	%l1, %o1
	mov	%l2, %o2
	call	C_LABEL(do_fpe_trap)
	mov	%l0, %o3

	ld	[%sp + C_STACK + PT_G1], %g1
	wr	%l0, 0x0, %psr
	ldd	[%sp + C_STACK + PT_G2], %g2
	ldd	[%sp + C_STACK + PT_G4], %g4
	ldd	[%sp + C_STACK + PT_G6], %g6
	b	ret_trap_entry
	wr	%l4, 0, %y

	/* This routine handles Tag Overflow Exceptions.
	 */
	.align 4
	.globl do_tag_overflow
do_tag_overflow:
	ENTER_TRAP(do_tag_overflow)
	st	%g1, [%sp + C_STACK + PT_G1]
	rd	%y, %l4
	std	%g2, [%sp + C_STACK + PT_G2]
	std	%g4, [%sp + C_STACK + PT_G4]
	std	%g6, [%sp + C_STACK + PT_G6]
	wr	%l0, PSR_ET, %psr		! re-enable traps
	add	%sp, C_STACK, %o0
	mov	%l1, %o1
	mov	%l2, %o2
	call	C_LABEL(handle_tag_overflow)
	mov	%l0, %o3

	ld	[%sp + C_STACK + PT_G1], %g1
	wr	%l0, 0x0, %psr
	ldd	[%sp + C_STACK + PT_G2], %g2
	ldd	[%sp + C_STACK + PT_G4], %g4
	ldd	[%sp + C_STACK + PT_G6], %g6
	b	ret_trap_entry
	wr	%l4, 0, %y

	/* This routine handles Watchpoint Exceptions.
	 */
	.align 4
	.globl do_watchpoint
do_watchpoint:
	ENTER_TRAP(do_watchpoint)
	st	%g1, [%sp + C_STACK + PT_G1]
	rd	%y, %l4
	std	%g2, [%sp + C_STACK + PT_G2]
	std	%g4, [%sp + C_STACK + PT_G4]
	std	%g6, [%sp + C_STACK + PT_G6]
	wr	%l0, PSR_ET, %psr		! re-enable traps
	add	%sp, C_STACK, %o0
	mov	%l1, %o1
	mov	%l2, %o2
	call	C_LABEL(handle_watchpoint)
	mov	%l0, %o3

	ld	[%sp + C_STACK + PT_G1], %g1
	wr	%l0, 0x0, %psr
	ldd	[%sp + C_STACK + PT_G2], %g2
	ldd	[%sp + C_STACK + PT_G4], %g4
	ldd	[%sp + C_STACK + PT_G6], %g6
	b	ret_trap_entry
	wr	%l4, 0, %y

	/* This routine handles Register Access Exceptions.
	 */
	.align 4
	.globl do_reg_access
do_reg_access:
	ENTER_TRAP(do_reg_access)
	st	%g1, [%sp + C_STACK + PT_G1]
	rd	%y, %l4
	std	%g2, [%sp + C_STACK + PT_G2]
	std	%g4, [%sp + C_STACK + PT_G4]
	std	%g6, [%sp + C_STACK + PT_G6]
	wr	%l0, PSR_ET, %psr		! re-enable traps
	add	%sp, C_STACK, %o0
	mov	%l1, %o1
	mov	%l2, %o2
	call	C_LABEL(handle_reg_access)
	mov	%l0, %o3

	ld	[%sp + C_STACK + PT_G1], %g1
	wr	%l0, 0x0, %psr
	ldd	[%sp + C_STACK + PT_G2], %g2
	ldd	[%sp + C_STACK + PT_G4], %g4
	ldd	[%sp + C_STACK + PT_G6], %g6
	b	ret_trap_entry
	wr	%l4, 0, %y

	/* This routine handles Instruction Access Errors.
	 */
	.align 4
	.globl do_iacc_error
do_iacc_error:
	ENTER_TRAP(do_iacc_error)
	st	%g1, [%sp + C_STACK + PT_G1]
	rd	%y, %l4
	std	%g2, [%sp + C_STACK + PT_G2]
	std	%g4, [%sp + C_STACK + PT_G4]
	std	%g6, [%sp + C_STACK + PT_G6]
	wr	%l0, PSR_ET, %psr		! re-enable traps
	add	%sp, C_STACK, %o0
	mov	%l1, %o1
	mov	%l2, %o2
	call	C_LABEL(handle_iacc_error)
	mov	%l0, %o3

	ld	[%sp + C_STACK + PT_G1], %g1
	wr	%l0, 0x0, %psr
	ldd	[%sp + C_STACK + PT_G2], %g2
	ldd	[%sp + C_STACK + PT_G4], %g4
	ldd	[%sp + C_STACK + PT_G6], %g6
	b	ret_trap_entry
	wr	%l4, 0, %y

	/* This routine handles Co-Processor Disabled Exceptions.
	 */
	.align 4
	.globl do_cp_disabled
do_cp_disabled:
	ENTER_TRAP(do_cp_disabled)
	st	%g1, [%sp + C_STACK + PT_G1]
	rd	%y, %l4
	std	%g2, [%sp + C_STACK + PT_G2]
	std	%g4, [%sp + C_STACK + PT_G4]
	std	%g6, [%sp + C_STACK + PT_G6]
	wr	%l0, PSR_ET, %psr		! re-enable traps
	add	%sp, C_STACK, %o0
	mov	%l1, %o1
	mov	%l2, %o2
	call	C_LABEL(handle_cp_disabled)
	mov	%l0, %o3

	ld	[%sp + C_STACK + PT_G1], %g1
	wr	%l0, 0x0, %psr
	ldd	[%sp + C_STACK + PT_G2], %g2
	ldd	[%sp + C_STACK + PT_G4], %g4
	ldd	[%sp + C_STACK + PT_G6], %g6
	b	ret_trap_entry
	wr	%l4, 0, %y

	/* This routine handles Unimplemented FLUSH Exceptions.
	 */
	.align 4
	.globl do_bad_flush
do_bad_flush:
	ENTER_TRAP(do_bad_flush)
	st	%g1, [%sp + C_STACK + PT_G1]
	rd	%y, %l4
	std	%g2, [%sp + C_STACK + PT_G2]
	std	%g4, [%sp + C_STACK + PT_G4]
	std	%g6, [%sp + C_STACK + PT_G6]
	wr	%l0, PSR_ET, %psr		! re-enable traps
	add	%sp, C_STACK, %o0
	mov	%l1, %o1
	mov	%l2, %o2
	call	C_LABEL(handle_bad_flush)
	mov	%l0, %o3

	ld	[%sp + C_STACK + PT_G1], %g1
	wr	%l0, 0x0, %psr
	ldd	[%sp + C_STACK + PT_G2], %g2
	ldd	[%sp + C_STACK + PT_G4], %g4
	ldd	[%sp + C_STACK + PT_G6], %g6
	b	ret_trap_entry
	wr	%l4, 0, %y

	/* This routine handles Co-Processor Exceptions.
	 */
	.align 4
	.globl do_cp_exception
do_cp_exception:
	ENTER_TRAP(do_cp_exception)
	st	%g1, [%sp + C_STACK + PT_G1]
	rd	%y, %l4
	std	%g2, [%sp + C_STACK + PT_G2]
	std	%g4, [%sp + C_STACK + PT_G4]
	std	%g6, [%sp + C_STACK + PT_G6]
	wr	%l0, PSR_ET, %psr		! re-enable traps
	add	%sp, C_STACK, %o0
	mov	%l1, %o1
	mov	%l2, %o2
	call	C_LABEL(handle_cp_exception)
	mov	%l0, %o3

	ld	[%sp + C_STACK + PT_G1], %g1
	wr	%l0, 0x0, %psr
	ldd	[%sp + C_STACK + PT_G2], %g2
	ldd	[%sp + C_STACK + PT_G4], %g4
	ldd	[%sp + C_STACK + PT_G6], %g6
	b	ret_trap_entry
	wr	%l4, 0, %y

	/* This routine handles Data Access Errors.
	 */
	.align 4
	.globl do_dacc_error
do_dacc_error:
	ENTER_TRAP(do_dacc_error)
	st	%g1, [%sp + C_STACK + PT_G1]
	rd	%y, %l4
	std	%g2, [%sp + C_STACK + PT_G2]
	std	%g4, [%sp + C_STACK + PT_G4]
	std	%g6, [%sp + C_STACK + PT_G6]
	wr	%l0, PSR_ET, %psr		! re-enable traps
	add	%sp, C_STACK, %o0
	mov	%l1, %o1
	mov	%l2, %o2
	call	C_LABEL(handle_dacc_error)
	mov	%l0, %o3

	ld	[%sp + C_STACK + PT_G1], %g1
	wr	%l0, 0x0, %psr
	ldd	[%sp + C_STACK + PT_G2], %g2
	ldd	[%sp + C_STACK + PT_G4], %g4
	ldd	[%sp + C_STACK + PT_G6], %g6
	b	ret_trap_entry
	wr	%l4, 0, %y

	/* This routine handles Hardware Divide By Zero Exceptions.
	 */
	.align 4
	.globl do_hw_divzero
do_hw_divzero:
	ENTER_TRAP(do_hw_divzero)
	st	%g1, [%sp + C_STACK + PT_G1]
	rd	%y, %l4
	std	%g2, [%sp + C_STACK + PT_G2]
	std	%g4, [%sp + C_STACK + PT_G4]
	std	%g6, [%sp + C_STACK + PT_G6]
	wr	%l0, PSR_ET, %psr		! re-enable traps
	add	%sp, C_STACK, %o0
	mov	%l1, %o1
	mov	%l2, %o2
	call	C_LABEL(handle_hw_divzero)
	mov	%l0, %o3

	ld	[%sp + C_STACK + PT_G1], %g1
	wr	%l0, 0x0, %psr
	ldd	[%sp + C_STACK + PT_G2], %g2
	ldd	[%sp + C_STACK + PT_G4], %g4
	ldd	[%sp + C_STACK + PT_G6], %g6
	b	ret_trap_entry
	wr	%l4, 0, %y

	/* This routine handles Data Store Errors.
	 */
	.align 4
	.globl do_dstore_err
do_dstore_err:
	ENTER_TRAP(do_dstore_err)
	st	%g1, [%sp + C_STACK + PT_G1]
	rd	%y, %l4
	std	%g2, [%sp + C_STACK + PT_G2]
	std	%g4, [%sp + C_STACK + PT_G4]
	std	%g6, [%sp + C_STACK + PT_G6]
	wr	%l0, PSR_ET, %psr		! re-enable traps
	add	%sp, C_STACK, %o0
	mov	%l1, %o1
	mov	%l2, %o2
	call	C_LABEL(handle_dstore_error)
	mov	%l0, %o3

	ld	[%sp + C_STACK + PT_G1], %g1
	wr	%l0, 0x0, %psr
	ldd	[%sp + C_STACK + PT_G2], %g2
	ldd	[%sp + C_STACK + PT_G4], %g4
	ldd	[%sp + C_STACK + PT_G6], %g6
	b	ret_trap_entry
	wr	%l4, 0, %y

	/* This routine handles Data Access MMU-Miss Exceptions.
	 */
	.align 4
	.globl do_dacc_mmu_miss
do_dacc_mmu_miss:
	ENTER_TRAP(do_dacc_mmu_miss)
	st	%g1, [%sp + C_STACK + PT_G1]
	rd	%y, %l4
	std	%g2, [%sp + C_STACK + PT_G2]
	std	%g4, [%sp + C_STACK + PT_G4]
	std	%g6, [%sp + C_STACK + PT_G6]
	wr	%l0, PSR_ET, %psr		! re-enable traps
	add	%sp, C_STACK, %o0
	mov	%l1, %o1
	mov	%l2, %o2
	call	C_LABEL(handle_dacc_mmu_miss)
	mov	%l0, %o3

	ld	[%sp + C_STACK + PT_G1], %g1
	wr	%l0, 0x0, %psr
	ldd	[%sp + C_STACK + PT_G2], %g2
	ldd	[%sp + C_STACK + PT_G4], %g4
	ldd	[%sp + C_STACK + PT_G6], %g6
	b	ret_trap_entry
	wr	%l4, 0, %y

	/* This routine handles Instruction Access MMU-Miss Exceptions.
	 */
	.align 4
	.globl do_iacc_mmu_miss
do_iacc_mmu_miss:
	ENTER_TRAP(do_iacc_mmu_miss)
	st	%g1, [%sp + C_STACK + PT_G1]
	rd	%y, %l4
	std	%g2, [%sp + C_STACK + PT_G2]
	std	%g4, [%sp + C_STACK + PT_G4]
	std	%g6, [%sp + C_STACK + PT_G6]
	wr	%l0, PSR_ET, %psr		! re-enable traps
	add	%sp, C_STACK, %o0
	mov	%l1, %o1
	mov	%l2, %o2
	call	C_LABEL(handle_iacc_mmu_miss)
	mov	%l0, %o3

	ld	[%sp + C_STACK + PT_G1], %g1
	wr	%l0, 0x0, %psr
	ldd	[%sp + C_STACK + PT_G2], %g2
	ldd	[%sp + C_STACK + PT_G4], %g4
	ldd	[%sp + C_STACK + PT_G6], %g6
	b	ret_trap_entry
	wr	%l4, 0, %y

	/* The getcc software trap.  The user wants the condition codes from
	 * the %psr in register %g1.
	 */

	.align 4
	.globl getcc_trap_handler
getcc_trap_handler:
	/* Shit, one more instruction and I could do this inline. */
	sll	%l0, 0x8, %g1
	srl	%g1, 28, %g1
	jmp	%l2
	rett	%l2+0x4

	/* The setcc software trap.  The user has condition codes in %g1
	 * that it would like placed in the %psr.  Be careful not to flip
	 * any unintention bits!
	 */

	.align 4
	.globl setcc_trap_handler
setcc_trap_handler:
	sll	%g1, 0x14, %l4
	set	PSR_ICC, %l5
	andn	%l0, %l5, %l0
	or	%l4, %l0, %l4
	wr	%l4, 0x0, %psr
	WRITE_PAUSE
	jmp	%l2
	rett	%l2+0x4

	.align 4
NMI_STRING:	.asciz	"NMI received, dazed and confused, halting...\n"

	.align 4
	.globl linux_trap_nmi
	.globl C_LABEL(interrupt_enable)
linux_trap_nmi:
	sethi	%hi(C_LABEL(prom_vector_p)), %o0
	ld	[%o0 + %lo(C_LABEL(prom_vector_p))], %o0
	ld	[%o0 + 0x74], %o0
	/* Ugh, until I figure out how to clear the IRQ line ;( */
	call	%o0
	nop

	.align 4
	.globl sparc_text_fault
sparc_text_fault:
	ENTER_TRAP(sparc_text_fault)
	st	%g1, [%sp + C_STACK + PT_G1]
	std	%g2, [%sp + C_STACK + PT_G2]
	std	%g4, [%sp + C_STACK + PT_G4]
	std	%g6, [%sp + C_STACK + PT_G6]
	rd	%y, %l4
	sethi	%hi(AC_SYNC_ERR), %o0
	lda	[%o0] ASI_CONTROL, %o1
	add	%o0, 0x4, %o0           ! go to sync vaddr
	lda	[%o0] ASI_CONTROL, %o2
	andcc	%o1, SUN4C_SYNC_NOMEM, %g0
	bz,a	normal_page_fault
	wr	%l0, PSR_ET, %psr	! re-enable traps

	add	%o0, 0x4, %o0           ! go to async error register
	lda	[%o0] ASI_CONTROL, %o3
	add	%o0, 0x4, %o0		! go to async vaddr
	subcc	%o4, %o2, %g0
	be,a	is_sync_fault		! not an async fault
	wr	%l0, PSR_ET, %psr

	/* crap, an asynchronous error has occurred */
	sethi	%hi(C_LABEL(interrupt_enable)), %l5
	ldub	[%l5 + %lo(C_LABEL(interrupt_enable))], %o0
	andn	%o0, INTS_ENAB, %o0
	stb	%o0, [%l5 + %lo(C_LABEL(interrupt_enable))]
	wr	%l0, PSR_ET, %psr		! enable traps
	call	C_LABEL(sparc_txtmem_error)	! call high level c-code
	or	%g0, FAULT_ASYNC, %o0

	ld	[%sp + C_STACK + PT_G1], %g1
	wr	%l0, 0x0, %psr
	ldd	[%sp + C_STACK + PT_G2], %g2
	ldd	[%sp + C_STACK + PT_G4], %g4
	ldd	[%sp + C_STACK + PT_G6], %g6
	ldub	[%l5 + %lo(C_LABEL(interrupt_enable))], %o1
	or	%o1, INTS_ENAB, %o1
	stb	%o1, [%l5 + %lo(C_LABEL(interrupt_enable))]
	b	ret_trap_entry
	wr	%l4, 0, %y

is_sync_fault:
	call	C_LABEL(sparc_txtmem_error)	! call high level c-code
	or	%g0, FAULT_SYNC, %o0

	ld	[%sp + C_STACK + PT_G1], %g1
	ld	[%sp + C_STACK + PT_G2], %g2
	ld	[%sp + C_STACK + PT_G4], %g4
	ld	[%sp + C_STACK + PT_G6], %g6
	wr	%l4, 0x0, %y
	b	ret_trap_entry
	wr	%l0, 0x0, %psr

normal_page_fault:
	std	%l0, [%sp + C_STACK + PT_PSR]
	or	%g0, %l3, %o0
	st	%l2, [%sp + C_STACK + PT_NPC]
	st	%l4, [%sp + C_STACK + PT_Y]
	or	%g0, %l1, %o3
	std	%i0, [%sp + C_STACK + PT_I0]
	std	%i2, [%sp + C_STACK + PT_I2]
	or	%g0, %l0, %o4
	std	%i4, [%sp + C_STACK + PT_I4]
	std	%i6, [%sp + C_STACK + PT_I6]
	call	C_LABEL(sparc_text_access_fault)
	add	%sp, C_STACK, %o5

	ldd	[%sp + C_STACK + PT_PSR], %l0
	ldd	[%sp + C_STACK + PT_NPC], %l2
	wr	%l3, 0x0, %y
	ld	[%sp + C_STACK + PT_G1], %g1
	ldd	[%sp + C_STACK + PT_G2], %g2
	ldd	[%sp + C_STACK + PT_G4], %g4
	ldd	[%sp + C_STACK + PT_G6], %g6
	ldd	[%sp + C_STACK + PT_I0], %i0
	ldd	[%sp + C_STACK + PT_I2], %i2
	ldd	[%sp + C_STACK + PT_I4], %i4
	ldd	[%sp + C_STACK + PT_I6], %i6

	b	ret_trap_entry
	wr	%l0, 0x0, %psr

	.align 4
	.globl sparc_data_fault
sparc_data_fault:
	ENTER_TRAP(sparc_data_fault)
	st	%g1, [%sp + C_STACK + PT_G1]
	std	%g2, [%sp + C_STACK + PT_G2]
	std	%g4, [%sp + C_STACK + PT_G4]
	std	%g6, [%sp + C_STACK + PT_G6]
	rd	%y, %l4
	sethi	%hi(AC_SYNC_ERR), %o0
	lda	[%o0] ASI_CONTROL, %o1
	add	%o0, 0x4, %o0           ! go to sync vaddr
	lda	[%o0] ASI_CONTROL, %o2
	andcc	%o1, SUN4C_SYNC_NOMEM, %g0
	bz,a	normal_data_page_fault
	wr	%l0, PSR_ET, %psr

	add	%o0, 0x4, %o0           ! go to async error register
	lda	[%o0] ASI_CONTROL, %o3
	add	%o0, 0x4, %o0		! go to async vaddr
	subcc	%o4, %o2, %g0
	be,a	is_data_sync_fault	! not an async fault
	wr	%l0, PSR_ET, %psr

	/* crap, an asynchronous error has occurred */
	sethi	%hi(C_LABEL(interrupt_enable)), %l5
	ldub	[%l5 + %lo(C_LABEL(interrupt_enable))], %o0
	andn	%o0, INTS_ENAB, %o0
	stb	%o0, [%l5 + %lo(C_LABEL(interrupt_enable))]
	wr	%l0, PSR_ET, %psr
	call	C_LABEL(sparc_datamem_error)	! call high level c-code
	or	%g0, FAULT_ASYNC, %o0

	ld	[%sp + C_STACK + PT_G1], %g1
	wr	%l0, 0x0, %psr
	ldd	[%sp + C_STACK + PT_G2], %g2
	ldd	[%sp + C_STACK + PT_G4], %g4
	ldd	[%sp + C_STACK + PT_G6], %g6
	ldub	[%l5 + %lo(C_LABEL(interrupt_enable))], %o1
	or	%o1, INTS_ENAB, %o1
	stb	%o1, [%l5 + %lo(C_LABEL(interrupt_enable))]
	b	ret_trap_entry
	wr	%l4, 0, %y

is_data_sync_fault:
	call	C_LABEL(sparc_datamem_error)	! call high level c-code
	or	%g0, FAULT_SYNC, %o0

	ld	[%sp + C_STACK + PT_G1], %g1
	ldd	[%sp + C_STACK + PT_G2], %g2
	ldd	[%sp + C_STACK + PT_G4], %g4
	ldd	[%sp + C_STACK + PT_G6], %g6
	wr	%l4, 0x0, %y
	b	ret_trap_entry
	wr	%l0, 0x0, %psr

normal_data_page_fault:
	std	%l0, [%sp + C_STACK + PT_PSR]	! store %psr and pc
	or	%g0, %l3, %o0
	st	%l2, [%sp + C_STACK + PT_NPC]	! store npc
	st	%l4, [%sp + C_STACK + PT_Y]	! store %y
	or	%g0, %l1, %o3

	/* The globals have already been placed on the stack */
	std	%i0, [%sp + C_STACK + PT_I0]	! store ins
	std	%i2, [%sp + C_STACK + PT_I2]
	or	%g0, %l0, %o4
	std	%i4, [%sp + C_STACK + PT_I4]
	std	%i6, [%sp + C_STACK + PT_I6]
	call	C_LABEL(sparc_data_access_fault)
	add	%sp, C_STACK, %o5

	ldd	[%sp + C_STACK + PT_PSR], %l0
	ldd	[%sp + C_STACK + PT_NPC], %l2
	wr	%l3, 0x0, %y
	ld	[%sp + C_STACK + PT_G1], %g1
	ldd	[%sp + C_STACK + PT_G2], %g2
	ldd	[%sp + C_STACK + PT_G4], %g4
	ldd	[%sp + C_STACK + PT_G6], %g6
	ldd	[%sp + C_STACK + PT_I0], %i0
	ldd	[%sp + C_STACK + PT_I2], %i2
	ldd	[%sp + C_STACK + PT_I4], %i4
	ldd	[%sp + C_STACK + PT_I6], %i6

	b	ret_trap_entry
	wr	%l0, 0x0, %psr


	.align 4
	.globl C_LABEL(srmmu_text_fault)
C_LABEL(srmmu_text_fault):
	ENTER_TRAP(srmmu_text_fault)
	st	%g1, [%sp + C_STACK + PT_G1]
	std	%g2, [%sp + C_STACK + PT_G2]
	std	%g4, [%sp + C_STACK + PT_G4]
	std	%g6, [%sp + C_STACK + PT_G6]
	rd	%y, %l4
	set	0x300, %o0
	lda	[%o0] ASI_M_MMUREGS, %o1	! fault status
	set	0x400, %o0
	lda	[%o0] ASI_M_MMUREGS, %o2	! fault address
	wr	%l0, PSR_ET, %psr		! traps back on
	WRITE_PAUSE
	std	%l0, [%sp + C_STACK + PT_PSR]
	or	%g0, %l3, %o0
	st	%l2, [%sp + C_STACK + PT_NPC]
	st	%l4, [%sp + C_STACK + PT_Y]
	or	%g0, %l1, %o3
	std	%i0, [%sp + C_STACK + PT_I0]
	std	%i2, [%sp + C_STACK + PT_I2]
	or	%g0, %l0, %o4
	std	%i4, [%sp + C_STACK + PT_I4]
	std	%i6, [%sp + C_STACK + PT_I6]
	call	C_LABEL(srmmu_text_access_fault)
	add	%sp, C_STACK, %o5

	ldd	[%sp + C_STACK + PT_PSR], %l0
	ldd	[%sp + C_STACK + PT_NPC], %l2
	wr	%l3, 0x0, %y
	ld	[%sp + C_STACK + PT_G1], %g1
	ldd	[%sp + C_STACK + PT_G2], %g2
	ldd	[%sp + C_STACK + PT_G4], %g4
	ldd	[%sp + C_STACK + PT_G6], %g6
	ldd	[%sp + C_STACK + PT_I0], %i0
	ldd	[%sp + C_STACK + PT_I2], %i2
	ldd	[%sp + C_STACK + PT_I4], %i4
	ldd	[%sp + C_STACK + PT_I6], %i6

	b	ret_trap_entry
	wr	%l0, 0x0, %psr

	.align 4
	.globl C_LABEL(srmmu_data_fault)
C_LABEL(srmmu_data_fault):
	ENTER_TRAP(srmmu_data_fault)
	st	%g1, [%sp + C_STACK + PT_G1]
	std	%g2, [%sp + C_STACK + PT_G2]
	std	%g4, [%sp + C_STACK + PT_G4]
	std	%g6, [%sp + C_STACK + PT_G6]
	rd	%y, %l4

	set	AC_M_SFSR, %o0
	lda	[%o0] ASI_M_MMUREGS, %o1	! fault status
	set	AC_M_SFAR, %o0
	lda	[%o0] ASI_M_MMUREGS, %o2	! fault address
	set	AC_M_AFSR, %o0
	lda	[%o0] ASI_M_MMUREGS, %o3
	set	AC_M_AFAR, %o0
	lda	[%o0] ASI_M_MMUREGS, %o4
	wr	%l0, PSR_ET, %psr		! traps back on
	WRITE_PAUSE
	std	%l0, [%sp + C_STACK + PT_PSR]
	or	%g0, %l3, %o0
	st	%l2, [%sp + C_STACK + PT_NPC]
	st	%l4, [%sp + C_STACK + PT_Y]
	std	%i0, [%sp + C_STACK + PT_I0]
	std	%i2, [%sp + C_STACK + PT_I2]
	std	%i4, [%sp + C_STACK + PT_I4]
	std	%i6, [%sp + C_STACK + PT_I6]
	call	C_LABEL(srmmu_data_access_fault)
	add	%sp, C_STACK, %o5

	ldd	[%sp + C_STACK + PT_PSR], %l0
	ldd	[%sp + C_STACK + PT_NPC], %l2
	wr	%l3, 0x0, %y
	ld	[%sp + C_STACK + PT_G1], %g1
	ldd	[%sp + C_STACK + PT_G2], %g2
	ldd	[%sp + C_STACK + PT_G4], %g4
	ldd	[%sp + C_STACK + PT_G6], %g6
	ldd	[%sp + C_STACK + PT_I0], %i0
	ldd	[%sp + C_STACK + PT_I2], %i2
	ldd	[%sp + C_STACK + PT_I4], %i4
	ldd	[%sp + C_STACK + PT_I6], %i6

	b	ret_trap_entry
	wr	%l0, 0x0, %psr

/* Normal Linux system calls enter here... */
/* Trying to make this as generic and simple as possible. */

	.align 4
	.globl linux_sparc_syscall
linux_sparc_syscall:
	/* Don't dork with %l7, it holds the pointer to the
	 * system call vector table.  ENTER_TRAP does not
	 * modify its value.
	 */
	ENTER_TRAP(linux_sparc_syscall)

	/* setup pt_regs stack frame, leave ints off...
	 * First save all but the current window onto the stack.
	 * This means nwindows-2 saves and nwindows-2 restores.
	 */
	andn	%l0, PSR_PIL, %l5
	wr	%l5, 0xf00, %psr
	wr	%l5, 0xf20, %psr	! no ints, traps on
	WRITE_PAUSE
	
	.globl nop7
	/* Flush windows */
	save	%sp, -C_STACK, %sp
	save	%sp, -C_STACK, %sp
	save	%sp, -C_STACK, %sp
	save	%sp, -C_STACK, %sp
	save	%sp, -C_STACK, %sp
nop7:	save	%sp, -C_STACK, %sp
	restore
	restore
	restore
	restore
	restore
	restore

	rd	%psr, %l6
	and	%l6, PSR_CWP, %l6		! only care about CWP
	andn	%l0, PSR_CWP, %l0
	or	%l0, %l6, %l0			! %l0 is now the new %psr
	
	std	%l0, [%sp + C_STACK + PT_PSR]	! save it away
	rd	%y, %l3
	std	%l2, [%sp + C_STACK + PT_NPC]

	/* Put %wim in %g0 slot, a hack.  This way we ensure that %wim
	 * sits right behind the current window in %psr, which is what
	 * we want.
	 */
	rd	%wim, %l4
	st	%l4, [%sp + C_STACK + PT_G0]
	st	%g1, [%sp + C_STACK + PT_G1]
	std	%g2, [%sp + C_STACK + PT_G2]
	std	%g4, [%sp + C_STACK + PT_G4]
	std	%g6, [%sp + C_STACK + PT_G6]
	std	%i0, [%sp + C_STACK + PT_I0]
	std	%i2, [%sp + C_STACK + PT_I2]
	std	%i4, [%sp + C_STACK + PT_I4]
	std	%i6, [%sp + C_STACK + PT_I6]

	wr	%l0, PSR_ET, %psr /* Turn on traps + interrupts */
	WRITE_PAUSE

	cmp	%i0, NR_SYSCALLS
	bgu,a	C_LABEL(ret_sys_call)
	or	%g0, -1, %i0

	cmp	%i0, 0x2		! fork? Same number for all OSs
	bne	not_fork
	nop
	call	C_LABEL(sys_fork)	! yep, load pt_regs into first arg
	add	%sp, C_STACK, %o0
	b	C_LABEL(ret_sys_call)
	nop
not_fork:
	/* Remember, trap table entry loaded syscall table ptr in %l7 */
	sll	%i0, 0x2, %o0
	add	%l7, %o0, %l7
	ld	[%l7], %o5		! load up ptr to syscall handler

	mov	%i1, %o0		! load up arguments
	mov	%i2, %o1
	mov	%i3, %o2
	mov	%i4, %o3
	jmpl	%o5, %o7		! Make syscall
	mov	%i5, %o4

	.globl C_LABEL(ret_sys_call)  /* exported for copy_process() */
C_LABEL(ret_sys_call):	 /* Child of a fork returns here */
	/* dump the pt_regs back into their rightful places */
	ldd	[%sp + C_STACK + PT_PSR], %l0
	ldd	[%sp + C_STACK + PT_NPC], %l2
	wr	%l3, 0x0, %y

	ld	[%sp + C_STACK + PT_G1], %g1
	ldd	[%sp + C_STACK + PT_G2], %g2
	ldd	[%sp + C_STACK + PT_G4], %g4
	ldd	[%sp + C_STACK + PT_G6], %g6
	ldd	[%sp + C_STACK + PT_I0], %i0
	ldd	[%sp + C_STACK + PT_I2], %i2
	ldd	[%sp + C_STACK + PT_I4], %i4
	ldd	[%sp + C_STACK + PT_I6], %i6

	/* %i6 is our frame pointer, the restore done by the rett
	 * instruction will automatically put us back on the users
	 * stack.
	 * Advance the pc and npc past the trap instruction, the copy_process()
	 * code for fork() depends on this being done right before trap return.
	 */
	or	%l2, 0x0, %l5

	or	%l5, 0x0, %l1    /* pc = npc */
	add	%l5, 0x4, %l2    /* npc= npc+4 */

	wr	%l0, 0x0, %psr
	WRITE_PAUSE	

	/* Fall through to ret_trap_entry */

/* Return from trap code.  I realized that I was duplicating a lot
 * of logic in the various trap handlers. Traps are off upon entry.
 */

ret_trap_entry:
	and	%l0, 0x1f, %l5
	sethi	%hi(lnx_winmask), %l6
	or	%l6, %lo(lnx_winmask), %l6
	ldub	[%l6 + %l5], %l5
	andcc	%l0, PSR_PS, %g0
	bnz	ret_trap_kernel
	rd	%wim, %l4

	sethi	%hi(C_LABEL(current)), %l6
	ld	[%l6 + %lo(C_LABEL(current))], %l6
	ld	[%l6 + THREAD_W_SAVED], %l7
	subcc	%g0, %l7, %g0
	bz,a	ret_trap_user
	nop

	wr	%g0, 0, %wim
	or	%g0, %g6, %l3
	or	%g0, %l6, %g6
	st	%g0, [%g6 + THREAD_W_SAVED]
	restore
	restore %g0, 1, %l1
	rd	%psr, %l0
	sll	%l1, %l0, %l1
	wr	%l1, 0x0, %wim
	and	%l0, 0x1f, %l0
	st	%l0, [%g6 + THREAD_WIM]
	nop
	save	%g0, %g0, %g0
	add	%g6, THREAD_REG_WINDOW, %g6
	ldd	[%g6], %l0
	ldd	[%g6 + 0x8], %l2
	ldd	[%g6 + 0x10], %l4
	ldd	[%g6 + 0x18], %l6
	ldd	[%g6 + 0x20], %i0
	ldd	[%g6 + 0x28], %i2
	ldd	[%g6 + 0x30], %i4
	ldd	[%g6 + 0x38], %i6

	save	%g0, %g0, %g0
	wr	%l0, 0x0, %psr
	or	%g0, %l3, %g6
	jmp	%l1
	rett	%l2

ret_trap_kernel:
	andcc	%l4, %l5, %g0
	bnz	1f
	wr	%l0, 0x0, %psr       ! reset condition codes
	nop
	jmp	%l1
	rett	%l2

1:
	wr	%g0, 0x0, %wim
	WRITE_PAUSE
	restore
	restore %g0, 0x1, %l1
	rd	%psr, %l0
	and	%l0, 0x1f, %l0
	sll	%l1, %l0, %l1
	wr	%l1, 0x0, %wim
	sethi	%hi(C_LABEL(current)), %l1
	ld	[%l1 + %lo(C_LABEL(current))], %l1
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

ret_trap_user:
	andcc	%l4, %l5, %g0
	bnz	1f
	wr	%l0, 0x0, %psr
	nop
	jmp	%l1
	rett	%l2

1:
	wr	%g0, 0x0, %wim
	wr	%l0, 0x0, %psr
	WRITE_PAUSE
	restore
	restore	%g0, 0x1, %l1
	rd	%psr, %l0
	sll	%l1, %l0, %l1
	wr	%l1, 0x0, %wim
	sethi	%hi(C_LABEL(current)), %l1
	ld	[%l1 + %lo(C_LABEL(current))], %l1
	and	%l0, 0x1f, %l0
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

/* Context switch code.  I don't feel like playing around with
 * inline gcc-assembly to do this right, so here it is.  The new
 * process's task_struct ptr is passed in %o0.
 *
 * This is still work in progress.
 * ONLY MAKE PROM CALLS FOR DIAGNOSTICS WHEN TRAPS ARE ON!!!!!
 *
 * First successful task switch 05/13/95 21:52:37
 *
 */
	.align 4
	.globl C_LABEL(sparc_switch_to)
C_LABEL(sparc_switch_to):
	or	%g0, %o0, %l5
	sethi	%hi(C_LABEL(current)), %l6
	ld	[%l6 + %lo(C_LABEL(current))], %l6
	rd	%psr, %l0

	or	%g0, %l0, %l4
	andn	%l0, PSR_PIL, %l0   /* turn off IRQ level bits leave PSR_ET on */

	wr	%l0, 0xf00, %psr    /* NO interrupts */
	WRITE_PAUSE

	/* Save state of old process */
	.globl rnop7
	save	%sp, -C_STACK, %sp
	save	%sp, -C_STACK, %sp
	save	%sp, -C_STACK, %sp
	save	%sp, -C_STACK, %sp
	save	%sp, -C_STACK, %sp
rnop7:	save	%sp, -C_STACK, %sp
	restore
	restore
	restore
	restore
	restore
	restore

	rd	%psr, %l3
	and	%l3, PSR_CWP, %l3		! only care about CWP bits now
	andn	%l0, PSR_CWP, %l0		! integrate with old %psr
	or	%l3, %l0, %l0
	
	st	%l0, [%sp + C_STACK + PT_PSR]	! save new %psr
	/* ??? We backtrack the PC two instructions due to retl's semantics ??? */
	/*sub	%o7, 0x8, %o7 */
	st	%o7, [%sp + C_STACK + PT_PC]	! save return PC
	add	%o7, 0x4, %l3
	st	%l3, [%sp + C_STACK + PT_NPC]   ! and NPC

	rd	%y, %l3
	st	%l3, [%sp + C_STACK + PT_Y]	! save %y

	/* Save the %wim into %g0 slot, ensures that it sits behind CWP */
	rd	%wim, %l3
	st	%l3, [%sp + C_STACK + PT_G0]	! save new %wim
	st	%g1, [%sp + C_STACK + PT_G1]
	std	%g2, [%sp + C_STACK + PT_G2]
	std	%g4, [%sp + C_STACK + PT_G4]
	std	%g6, [%sp + C_STACK + PT_G6]
	std	%i0, [%sp + C_STACK + PT_I0]
	std	%i2, [%sp + C_STACK + PT_I2]
	std	%i4, [%sp + C_STACK + PT_I4]
	std	%i6, [%sp + C_STACK + PT_I6]

	wr	%l0, (0xf20), %psr		! no traps, no intrs
	WRITE_PAUSE

	/* TRAPS ARE OFF, NO PROM CALLS WHATSOEVER FOR DEBUGGING!!! */
	/* SO what we do is we put an imperical constant in %g2 and
	 * a 'counter' in %g1 which we increment after every instruction
	 * so we can figure out where the thing prom panics.  Then at the
	 * prom prompt we print out the saved registers.  To drop into the
	 * prom and look at the registers just execute 7 saves since that
	 * will induce a window trap before the last one and traps are off,
	 * thus a watchdog reset will occur.
	 */

	/* Grrr, this is hairy... be careful, again NO PROM CALLS YET! */
	/* Load up the new 'current' */
	sethi	%hi(C_LABEL(current)), %g1
	st	%l5, [%g1 + %lo(C_LABEL(current))]
	
	/* Load up new processes stack, we always switch onto the kernel stack */
	/* Be CAREFUL, use globals for temporaries, because after we change the
	 * %psr the window could change and you will most likely be accessing
	 * different outs, ins, and locals than you origionally were.
	 */
	or	%g0, %l5, %g6
	ld	[%l5 + THREAD_KSP], %g3

	/* New procs %psr */
	ld	[%g3 + C_STACK + PT_PSR], %g4
	wr	%g4, 0xf00, %psr         /* No ints, no traps */
	WRITE_PAUSE

	/* We could be in a different window NOW. Assume nothing about the
	 * current set of in, out and local registers.
	 */

	/* New procs %wim */
	ld	[%g3 + C_STACK + PT_G0], %l4 /* %wim is here */
	st	%l4, [%g6 + THREAD_WIM]      /* Update tss */
	wr	%l4, 0x0, %wim               /* Use it */
	WRITE_PAUSE

	/* Finally, load the stack */
	or	%g0, %g3, %sp

	/* We are now sane, we have a good stack and our state is reflected
	 * properly in 'current'.  Let it rip.
	 */
	/* Remember, you can't increase PIL and turn on traps at the
	 * same time.
	 */
	wr	%g4, 0xf00, %psr  /* Traps on, no interrupts. */
	wr	%g4, 0xf20, %psr
	WRITE_PAUSE
	
	sethi	%hi(C_LABEL(current)), %o0
	ld	[%o0 + %lo(C_LABEL(current))], %o0
	ld	[%o0 + THREAD_PC], %o7 /* Setup return address */

	/* cross fingers */
	retl
	nop

/* The following two things point to window management tables. The first
 * one is used to quickly look up how many user windows there are from
 * trap-land. The second is used in a trap handler to determine if a rett
 * instruction will land us smack inside the invalid window that possibly
 * the trap was called to fix-up.
 */

/* For now these are static tables geared for a 7 window sparc.
 * But in head.S after we calculate this table based upon the
 * nwindows value.  This table is big enough for a 16 window sparc.
 */


		.data
		.align 4
		.globl lnx_winmask
lnx_winmask:
		.byte	2, 4, 8, 16, 32, 64, 1, 0
		.byte	0, 0, 0, 0, 0, 0, 0, 0
	
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
	.long C_LABEL(sys_sysfs)		/* 135 */
	.long C_LABEL(sys_personality)
	.long C_LABEL(sys_ni_syscall)		/* for afs_syscall */
	.long C_LABEL(sys_setfsuid)
	.long C_LABEL(sys_setfsgid)
	.long C_LABEL(sys_llseek)		/* 140 */
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)

	/* 150 */
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)

	/* 160 */
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)

	/* 170 */
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)

	/* 180 */
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)

	/* 190 */
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)

	/* 200 */
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)

	/* 210 */
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)

	/* 220 */
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)

	/* 230 */
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)

	/* 240 */
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)

	/* 250 */
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall), C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)		/* 255 */
	.align 4
