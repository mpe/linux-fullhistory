/* $Id: scall_o32.S,v 1.5 1998/08/19 21:53:50 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1997, 1998 by Ralf Baechle
 */
#include <asm/asm.h>
#include <linux/errno.h>
#include <asm/current.h>
#include <asm/mipsregs.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>
#include <asm/unistd.h>

/* This duplicates the definition from <linux/sched.h> */
#define PF_TRACESYS	0x00000020	/* tracing system calls */

/* This duplicates the definition from <asm/signal.h> */
#define SIGILL		4		/* Illegal instruction (ANSI).  */

/* Highest syscall used of any syscall flavour */
#define MAX_SYSCALL_NO	__NR_Linux + __NR_Linux_syscalls

	.align  5
NESTED(handle_sys, PT_SIZE, sp)
	.set	noat
	SAVE_SOME
	STI
	.set	at

	lw	t1, PT_EPC(sp)		# skip syscall on return

	sltiu	t0, v0, MAX_SYSCALL_NO + 1 # check syscall number
	addiu	t1, 4			# skip to next instruction
	beqz	t0, illegal_syscall
	sw	t1, PT_EPC(sp)

	/* XXX Put both in one cacheline, should save a bit. */
	sll	t0, v0, 2
	lw	t2, sys_call_table(t0)	# syscall routine
	lbu	t3, sys_narg_table(v0)	# number of arguments
	beqz	t2, illegal_syscall;

	subu	t0, t3, 5		# 5 or more arguments?
	bgez	t0, stackargs

stack_done:
	lw	t0, TASK_FLAGS($28)	# syscall tracing enabled?
	andi	t0, PF_TRACESYS
	bnez	t0, trace_a_syscall

	jalr	t2			# Do The Real Thing (TM)

	li	t0, -EMAXERRNO - 1	# error?
	sltu	t0, t0, v0
	sw	t0, PT_R7(sp)		# set error flag
	beqz	t0, 1f

	negu	v0			# error
	sw	v0, PT_R0(sp)		# set flag for syscall restarting
1:	sw	v0, PT_R2(sp)		# result

EXPORT(o32_ret_from_sys_call)
	lw	t0,bh_mask
	lw	t1,bh_active		# unused delay slot
	and	t0,t1
	bnez	t0,o32_handle_bottom_half

9:	lw	t0,PT_STATUS(sp)	# returning to kernel mode?
	andi	t1, t0, 0x10
	lw	t2, TASK_NEED_RESCHED($28)
	beqz	t1, o32_return		# -> yes
	bnez	t2, o32_reschedule
	lw	v0, TASK_SIGPENDING($28)
	move	a0, zero
	beqz	v0, o32_return
	move	a1, sp
	SAVE_STATIC
	jal	do_signal

o32_return:
	RESTORE_SOME
	RESTORE_SP
	.set	mips3
	eret
	.set	mips0

o32_handle_bottom_half:
	jal	do_bottom_half
	b	9b
o32_reschedule:
	SAVE_STATIC
	jal	schedule
	b	o32_ret_from_sys_call

/* ------------------------------------------------------------------------ */

trace_a_syscall:
	SAVE_STATIC
	sw	t2,PT_R1(sp)
	jal	syscall_trace
	lw	t2,PT_R1(sp)

	lw	a0, PT_R4(sp)		# Restore argument registers
	lw	a1, PT_R5(sp)
	lw	a2, PT_R6(sp)
	lw	a3, PT_R7(sp)
	jalr	t2

	li	t0, -EMAXERRNO - 1	# error?
	sltu	t0, t0, v0
	sw	t0, PT_R7(sp)		# set error flag
	beqz	t0, 1f

	negu	v0			# error
	sw	v0, PT_R0(sp)		# set flag for syscall restarting
1:	sw	v0, PT_R2(sp)		# result

	jal	syscall_trace
	j	ret_from_sys_call

/* ------------------------------------------------------------------------ */

	/*
	 * More than four arguments.  Try to deal with it by copying the
	 * stack arguments from the user stack to the kernel stack.
	 * This Sucks (TM).
	 */
stackargs:
	lw	t0, PT_R29(sp)		# get old user stack pointer
	subu	t3, 4
	sll	t1, t3, 2		# stack valid?

	addu	t1, t0			# end address
	or	t0, t1
	bltz	t0, bad_stack		# -> sp is bad

	lw	t0, PT_R29(sp)		# get old user stack pointer
	la	t1, 3f			# copy 1 to 2 arguments
	sll	t3, t3, 3
	subu	t1, t3
	jr	t1

	/* Ok, copy the args from the luser stack to the kernel stack */
1:	lw	t1, 20(t0)		# argument #6 from usp
	sw	t1, 20(sp)
2:	lw	t1, 16(t0)		# argument #5 from usp
	sw	t1, 16(sp)

3:	j	stack_done		# go back

	.section __ex_table,"a"
	PTR	1b,bad_stack
	PTR	2b,bad_stack
	.previous

	/*
	 * The stackpointer for a call with more than 4 arguments is bad.
	 */
bad_stack:
	negu	v0				# error
	sw	v0, PT_R0(sp)
	sw	v0, PT_R2(sp)
	li	t0, 1				# set error flag
	sw	t0, PT_R7(sp)
	j	ret_from_sys_call

	/*
	 * The system call does not exist in this kernel
	 */
illegal_syscall:
	li	v0, ENOSYS			# error
	sw	v0, PT_R2(sp)
	li	t0, 1				# set error flag
	sw	t0, PT_R7(sp)
	j	ret_from_sys_call
