/* $Id: r2300_scall.S,v 1.1.1.1 1997/06/01 03:16:42 ralf Exp $
 * r2300_scall.S: R2000/R3000 specific code to handle system calls.
 *
 * Copyright (C) 1994, 1995, 1996 by Ralf Baechle and Andreas Busse
 *
 * Multi-arch abstraction and beautification:
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 */
#include <asm/asm.h>
#include <asm/bootinfo.h>
#include <asm/cachectl.h>
#include <asm/fpregdef.h>
#include <asm/mipsconfig.h>
#include <asm/mipsregs.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>

/*
 * do_syscall calls the function in a1 with upto 7 arguments.  If over
 * four arguments are being requested, the additional arguments will
 * be copied from the user stack pointed to by a0->reg29.
 *
 * a0 (struct pt_regs *)  pointer to user registers
 * a1 (syscall_t)         pointer to syscall to do
 * a2 (int)               number of arguments to syscall
 */
	.set	noreorder
NESTED(r2300_do_syscalls, 32, sp)
	subu	sp,32
	sw	ra,28(sp)
	sll	a2,a2,PTRLOG
	lw	t0,dst(a2)
	move	t2,a1
	jalr	t0
	 lw	t0,PT_R29(a0)		# get old user stack pointer

7:
	lw	t1,24(t0)		# parameter #7 from usp
	nop				# delay slot
	sw	t1,24(sp)
6:
	lw	t1,20(t0)		# parameter #6 from usp
	nop				# delay slot
	sw	t1,20(sp)
5:
	lw	t1,16(t0)		# parameter #5 from usp
	nop				# delay slot
	sw	t1,16(sp)
4:
	lw	a3,PT_R7(a0)		# 4 args
3:
	lw	a2,PT_R6(a0)		# 3 args
2:
	lw	a1,PT_R5(a0)		# 2 args
1:
	jalr	t2			# 1 args
	 lw	a0,PT_R4(a0)

	lw	ra,28(sp)
	addiu	sp,32
	jr	ra
	 nop

0:
	jalr	t2			# 0 args, just pass a0
	 lw	ra,28(sp)
	addiu	sp,32

	jr	ra
	 nop				# delay slot
	END(r2300_do_syscalls)

		.rdata
		.align	PTRLOG
dst:		PTR	0b, 1b, 2b, 3b, 4b, 5b, 6b, 7b

		.section	__ex_table,"a"
		PTR	7b,bad_stack
		PTR	6b,bad_stack
		PTR	5b,bad_stack
		.previous
