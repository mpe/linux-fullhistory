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

	.text
	.align 4

/* Default trap handler */
	.globl my_trap_handler
my_trap_handler:
		rd %wim, %l4
		or %g0, 0x1, %l5
		sll %l5, %l0, %l5
		cmp %l4, %l5        ! are we in the invalid register?
	
#if 0 /* work in progress */
		be wash_trap_win
#endif

		nop
		or %%g0, %l3, %o0
		call _do_hw_interrupt
		or %%g0, %%g0, %o1
		wr %l0, 0x20, %psr  ! re-enable traps and reset the condition codes
		nop
		nop
		nop                 ! click our heels three times, "no place like home"
		jmp %l1
		rett %l2
	
/* This is cheese and only works reliably if not coming from userland. */
	.globl fill_window_entry
fill_window_entry:
	wr 	%g0, 0, %wim		! let us into the invalid window
					! without inducing another trap
	restore
	nop
	nop				! no guarentees until 3 insns later
	restore
	restore	%g0, 1, %l1
	rd	%psr, %l0
	sll	%l1, %l0, %l0
	wr	%l0, 0, %wim
	save	%g0, %g0, %g0

/* load up the window */
	ldd	[%sp], %l0
	ldd	[%sp + 8], %l2
	ldd	[%sp + 16], %l4
	ldd	[%sp + 24], %l6
	ldd	[%sp + 32], %i0
	ldd	[%sp + 40], %i2
	ldd	[%sp + 48], %i4
	ldd	[%sp + 56], %i6

	save	%g0, %g0, %g0
	save	%g0, %g0, %g0
	jmp	%l1
	rett	%l2

/* Cheese number two, give this a bad stack pointer and get scared. */
	.globl spill_window_entry
spill_window_entry:
	save	%g0, %g0, %g0  ! save into next 'valid' window
	std	%l0, [%sp]     ! acquire some scratch registers
	rd	%psr, %l0
	or	%g0, 0x1, %l1
	sll	%l1, %l0, %l0
	wr	%l0, 0, %wim   ! make window we are saving the 'invalid' one
	std	%l2, [%sp + 8]
	std	%l4, [%sp + 16]
	std	%l6, [%sp + 24]
	std	%i0, [%sp + 32]
	std	%i2, [%sp + 40]
	std	%i4, [%sp + 48]
	std	%i6, [%sp + 56]
	restore                ! restore back into the window we want to use
	jmp	%l1
	rett	%l2            ! return from spill handler


/* This is where most generic traps enter, the registers should be:
	%l0 == %psr
	%l1 == %pc
	%l2 == %npc
	%l4 == trap_type
	%l7 == trap_handler
*/

trap_entry:
	srl	%l0, 0x6, %l5	! shift over to previous priv bit
	andcc	%l5, 0x1, %g0   ! 1 == from kernel 0 == from user
	bz	2f
	nop			! we dont handle users yet ;-(
	
	or	%g0, 0x1, %l5
	sll	%l5, %l0, %l5	! a trick, only least 5 bits are
				! significant in a register shift
				! count.
	rd	%wim, %l6
	andcc	%l6, %l5, %g0   ! if (((1<<CWP)&(%wim))==1) we are
				! in an 'invalid' window
	bz,a	3f
	sub	%fp, C_STACK+80, %sp ! cool, just set stack and go
				     ! this window is valid to use
	
/* we aparently need to clean the trap window, inline this for speed */	
	or	%g0, %g7, %l7	! set up g6 and g7 for scratch
	or	%g0, %g6, %l6	
	
	save	%g0, %g0, %g0   ! enter next window after the invalid one
	std	%l0, [%sp]      ! and store it on the stack for later
	std	%l2, [%sp + 8]  ! retrieval
	std	%l4, [%sp + 16]
	std	%l6, [%sp + 24]
	std	%i0, [%sp + 32]
	std	%i2, [%sp + 40]
	std	%i4, [%sp + 48]
	std	%i6, [%sp + 56]

	rd	%psr, %g7       ! read the current window pointer
	or	%g0, 0x1, %g6   ! and mask it over into the %wim
	sll	%g6, %g7, %g6   ! value
	wr	%g6, 0, %wim    ! make this window the now 'invalid' one
	and	%g6, 31, %g6
	restore			! re-enter the trap window, it is ok now

	or	%g0, %l6, %g6	! bring back in the old temporaries
	or	%g0, %l7, %g7   ! to the locals they were in

	b	3f
	sub	%fp, C_STACK+80, %sp ! fix stack pointer

	
/* for now if we get a from-user trap you lose... */

	.data
	.align 4

tfu_youlose:	.asciz "trap from userland, you lose...\n"
	
	.align 4
	.text

2:	
	sethi	%hi(tfu_youlose), %o0
	sethi	%hi(prom_printf), %o1
	ld	[%o1 + %lo(prom_printf)], %o1
	call	%o1	
	or	%o0, %lo(tfu_youlose), %o0
	
	sethi	%hi(prom_halt), %o1
	ld	[%o1 + %lo(prom_halt)], %o1
	call	%o1				! its over
	nop
	
/* nop and just return, here is where I would jump to the
 * appropriate handler.
 */

	jmp %1
	rett %l2
	nop
