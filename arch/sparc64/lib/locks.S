/* $Id: locks.S,v 1.3 1997/07/22 05:51:42 davem Exp $
 * locks.S: SMP low-level lock primitives on Sparc64.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/asm_offsets.h>
#include <asm/ptrace.h>
#include <asm/smp.h>

	.text
	.align		32

___lk_busy_spin:
	orcc		%g2, 0, %g0
	bne,pt		%icc, ___lk_busy_spin
	 ldub		[%g1 + 0], %g2
	b,pt		%xcc, 1f
	 ldstub		[%g1 + 0], %g2

	.globl		___lock_kernel
___lock_kernel:
	addcc		%g2, -1, %g2
	rdpr		%pil, %g3
	bcs,a,pn	%icc, 9f
	 st		%g2, [%g6 + AOFF_task_lock_depth]
	wrpr		%g0, 15, %pil
	ldstub		[%g1 + 0], %g2
1:	brnz,a,pn	%g2, ___lk_busy_spin
	 ldub		[%g1 + 0], %g2
	lduw		[%g6 + AOFF_task_processor], %g2
	membar		#LoadLoad | #LoadStore
	stb		%g2, [%g1 + 1]
2:	mov		-1, %g2
	st		%g2, [%g6 + AOFF_task_lock_depth]
	wrpr		%g3, 0, %pil
9:	jmpl		%o7 + 0x8, %g0
	 mov		%g5, %o7

	.globl		___lock_reacquire_kernel
___lock_reacquire_kernel:
	rdpr		%pil, %g3
	wrpr		%g0, 15, %pil
	st		%g2, [%g6 + AOFF_task_lock_depth]
	ldstub		[%g1 + 0], %g2
1:	brz,pt		%g2, 3f
	 ldub		[%g1 + 0], %g2
2:	brnz,a,pt	%g2, 2b
	 ldub		[%g1 + 0], %g2
	b,pt		%xcc, 1b
	 ldstub		[%g1 + 0], %g2
3:	lduw		[%g6 + AOFF_task_processor], %g2
	membar		#LoadLoad | #LoadStore
	stb		%g2, [%g1 + 1]
	wrpr		%g3, 0, %pil
	jmpl		%o7 + 0x8, %g0
	 mov		%g5, %o7

#undef NO_PROC_ID
#define NO_PROC_ID	0xff

	.globl		___unlock_kernel
___unlock_kernel:
	addcc		%g2, 1, %g2
	rdpr		%pil, %g3
	bne,a,pn	%icc, 1f
	 stw		%g2, [%g6 + AOFF_task_lock_depth]
	wrpr		15, %pil
	mov		NO_PROC_ID, %g2
	stb		%g2, [%g1 + 1]
	membar		#StoreStore | #LoadStore
	stb		%g0, [%g1 + 0]
	stw		%g0, [%g6 + AOFF_task_lock_depth]
	wrpr		%g3, 0, %pil
1:	jmpl		%o7 + 0x8, %g0
	 mov		%g5, %o7
	
