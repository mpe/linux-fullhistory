/* $Id: entry.S,v 1.14 1997/04/14 06:56:54 davem Exp $
 * arch/sparc64/kernel/entry.S:  Sparc64 trap low-level entry points.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996 Eddie C. Dost   (ecd@skynet.be)
 * Copyright (C) 1996 Miguel de Icaza (miguel@nuclecu.unam.mx)
 * Copyright (C) 1996,1997 Jakub Jelinek   (jj@sunsite.mff.cuni.cz)
 */

#include <linux/config.h>
#include <linux/errno.h>

#include <asm/head.h>
#include <asm/asi.h>
#include <asm/smp.h>
#include <asm/ptrace.h>
#include <asm/page.h>
#include <asm/signal.h>
#include <asm/pgtable.h>

#define curptr      g6

#define NR_SYSCALLS 256      /* Each OS is different... */

	.text
        .align  4
/* FIXME: This is still debugging hack */
	.globl	sparc64_dtlb_fault, sparc64_dtlb_refbit_catch, sparc64_itlb_refbit_catch
sparc64_dtlb_fault:
	rdpr	%pstate, %g1
	wrpr	%g1, PSTATE_AG|PSTATE_MG, %pstate
	ba,pt	%xcc, etrap
	 rd	%pc, %g7
	call	sparc64_dtlb_fault_handler
	 nop

sparc64_dtlb_refbit_catch:
	rdpr	%pstate, %g1
	wrpr	%g1, PSTATE_AG|PSTATE_MG, %pstate
	ba,pt	%xcc, etrap
	 rd	%pc, %g7
	call	sparc64_dtlb_refbit_handler
	 add	%sp, STACK_BIAS + REGWIN_SZ, %o0

sparc64_itlb_refbit_catch:
	rdpr	%pstate, %g1
	wrpr	%g1, PSTATE_AG|PSTATE_MG, %pstate
	ba,pt	%xcc, etrap
	 rd	%pc, %g7
	call	sparc64_dtlb_refbit_handler
	 nop

	/* Note check out head.h, this code isn't even used for UP,
	 * for SMP things will be different.  In particular the data
	 * registers for cross calls will be:
	 *
	 * DATA 0: Address of function to call
	 * DATA 1: Argument 1, place in %g6
	 * DATA 2: Argument 2, place in %g7
	 *
	 * With this method we can do most of the cross-call tlb/cache
	 * flushing in very quickly.
	 */
	.align	4
	.globl	do_ivec
do_ivec:
	ldxa	[%g0] ASI_INTR_RECEIVE, %g1
	andcc	%g1, 0x20, %g0
	be,pn	%xcc, do_ivec_return
	 mov	0x40, %g2

	/* Load up Interrupt Vector Data 0 register. */
	sethi	%uhi(ivector_to_mask), %g4
	ldxa	[%g2] ASI_UDB_INTR_R, %g3
	or	%g4, %ulo(ivector_to_mask), %g4
	and	%g3, 0x7ff, %g3
	sllx	%g4, 32, %g4
	sethi	%hi(ivector_to_mask), %g5
	sllx	%g3, 3, %g3
	or	%g5, %lo(ivector_to_mask), %g5
	add	%g5, %g4, %g4
	ldx	[%g4 + %g3], %g2
	brz,pn	%g2, do_ivec_spurious
	 nop

	/* No branches, worse case we don't know about this interrupt
	 * yet, so we would just write a zero into the softint register
	 * which is completely harmless.
	 */
	wr	%g2, 0x0, %set_softint

do_ivec_return:
	/* Acknowledge the UPA */
	stxa	%g0, [%g0] ASI_INTR_RECEIVE
	membar	#Sync
	retry

do_ivec_spurious:
	stxa	%g0, [%g0] ASI_INTR_RECEIVE
	rdpr	%pstate, %g1
	wrpr	%g1, PSTATE_IG | PSTATE_AG, %pstate
	ba,pt	%xcc, etrap
	 rd	%pc, %g7
	call	report_spurious_ivec
	 add	%sp, STACK_BIAS + REGWIN_SZ, %o0
	ba,pt	%xcc, rtrap
	 nop

breakpoint_t:
	.asciz	"Breakpoint Trap %lx\n"
	.align	4
	.globl	breakpoint_trap
breakpoint_trap:
	mov	%o0, %o1
	sethi	%hi(breakpoint_t), %o0
	or	%o0, %lo(breakpoint_t), %o0
	call	prom_printf
	 add	%o0, %g4, %o0
	call	prom_cmdline
	 nop
	ba,a,pt	%xcc, rtrap

	.globl	sys_pipe, sys_execve, sys_sigpause, sys_nis_syscall

sys_pipe:
	sethi	%hi(sparc_pipe), %g1
	add	%g1, %g4, %g1
	jmpl	%g1 + %lo(sparc_pipe), %g0
	 add	%sp, STACK_BIAS + REGWIN_SZ, %o0

sys_nis_syscall:
	sethi	%hi(c_sys_nis_syscall), %g1
	add	%g1, %g4, %g1
	jmpl	%g1 + %lo(c_sys_nis_syscall), %g0
	 add	%sp, STACK_BIAS + REGWIN_SZ, %o0

sys_execve:
	sethi	%hi(sparc_execve), %g1
	add	%g1, %g4, %g1
	jmpl	%g1 + %lo(sparc_execve), %g0
	 add	%sp, STACK_BIAS + REGWIN_SZ, %o0

sys_sigpause:
	/* NOTE: %o0 has a correct value already */
	call	do_sigpause
	 add	%sp, STACK_BIAS + REGWIN_SZ, %o1
	
	ld	[%curptr + AOFF_task_flags], %l5
	andcc	%l5, 0x20, %g0
	be,pt	%icc, ret_sys_call
	 clr	%o0
	call	syscall_trace
	 nop
	ba,pt	%xcc, ret_sys_call
	 clr	%o0

	/* This is how fork() was meant to be done, 11 instruction entry. -DaveM */
	.globl	sys_fork, sys_vfork, sys_clone
sys_fork:
sys_vfork:
	mov	SIGCHLD, %o0
	clr	%o1
sys_clone:
	mov	%o7, %l5
	flushw
	rdpr	%cwp, %o4
	add	%sp, STACK_BIAS + REGWIN_SZ, %o2
	brz,a	%o1, 1f
	 mov	%fp, %o1
1:
	/* Don't try this at home. */
	stx	%o4, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_G0]
	call	do_fork
	 add	%l5, 8, %o7

linux_sparc_ni_syscall:
	sethi	%hi(sys_ni_syscall), %l7
	or	%l7, %lo(sys_ni_syscall), %l7
	ba,pt	%xcc,syscall_is_too_hard
	 add	%l7, %g4, %l7

linux_fast_syscall:
	andn	%l7, 3, %l7
	mov	%i0, %o0
	mov	%i1, %o1
	mov 	%i2, %o2
	jmpl	%l7 + %g0, %g0
	 mov	%i3, %o3

linux_syscall_trace:
	call	syscall_trace
	 nop
	mov	%i0, %o0
	mov	%i1, %o1
	mov	%i2, %o2
	mov	%i3, %o3
	ba,pt	%xcc, 2f
	 mov	%i4, %o4

	.globl	ret_from_syscall
ret_from_syscall:
	ba,pt	%xcc, ret_sys_call
	 ldx	[%sp + STACK_BIAS + REGWIN_SZ + PT_I0], %o0

	/* Linux native and SunOS system calls enter here... */
	.align	4
	.globl	linux_sparc_syscall
linux_sparc_syscall:
	/* Direct access to user regs, must faster. */
	cmp	%g1, NR_SYSCALLS
	add	%l7, %g4, %l7
	bgeu,pn	%xcc, linux_sparc_ni_syscall
	 sll	%g1, 3, %l4
	ldx	[%l7 + %l4], %l7
	andcc	%l7, 1, %g0
	bne,pn	%icc, linux_fast_syscall
	 /* Just do the next insn in the delay slot */

	.globl	syscall_is_too_hard
syscall_is_too_hard:
	mov	%i0, %o0
	mov	%i1, %o1
	mov	%i2, %o2

	ldx	[%curptr + AOFF_task_flags], %l5
	mov	%i3, %o3
	mov	%i4, %o4
	andcc	%l5, 0x20, %g0
	bne,pn	%icc, linux_syscall_trace
	 mov	%i0, %l5
2:
	call	%l7
	 mov	%i5, %o5

	stx	%o0, [%sp + STACK_BIAS + REGWIN_SZ + PT_I0]

	.globl	ret_sys_call
ret_sys_call:
	ldx	[%curptr + AOFF_task_flags], %l6
	mov	%ulo(TSTATE_XCARRY | TSTATE_ICARRY), %g2
	ldx	[%sp + STACK_BIAS + REGWIN_SZ + PT_TSTATE], %g3
	cmp	%o0, -ENOIOCTLCMD
	sllx	%g2, 32, %g2
	bgeu,pn	%xcc, 1f
	 andcc	%l6, 0x20, %l6	

	/* System call success, clear Carry condition code. */
	andn	%g3, %g2, %g3
	clr	%l6
	stx	%g3, [%sp + STACK_BIAS + REGWIN_SZ + PT_TSTATE]	
	bne,pn	%icc, linux_syscall_trace2
	 ldx	[%sp + STACK_BIAS + REGWIN_SZ + PT_TNPC], %l1 /* pc = npc */
	add	%l1, 0x4, %l2				      /* npc = npc+4 */
	stx	%l1, [%sp + STACK_BIAS + REGWIN_SZ + PT_TPC]
	ba,pt	%xcc, rtrap
	 stx	%l2, [%sp + STACK_BIAS + REGWIN_SZ + PT_TNPC]
1:
	/* System call failure, set Carry condition code.
	 * Also, get abs(errno) to return to the process.
	 */
	sub	%g0, %o0, %o0
	or	%g3, %g2, %g3
	stx	%o0, [%sp + STACK_BIAS + REGWIN_SZ + PT_I0]
	mov	1, %l6
	stx	%g3, [%sp + STACK_BIAS + REGWIN_SZ + PT_TSTATE]
	bne,pn	%icc, linux_syscall_trace2
	 ldx	[%sp + STACK_BIAS + REGWIN_SZ + PT_TNPC], %l1 /* pc = npc */
	add	%l1, 0x4, %l2				      /* npc = npc+4 */
	stx	%l1, [%sp + STACK_BIAS + REGWIN_SZ + PT_TPC]
	ba,pt	%xcc, rtrap
	 stx	%l2, [%sp + STACK_BIAS + REGWIN_SZ + PT_TNPC]

linux_syscall_trace2:
	call	syscall_trace
	 add	%l1, 0x4, %l2			/* npc = npc+4 */
	stx	%l1, [%sp + STACK_BIAS + REGWIN_SZ + PT_TPC]
	ba,pt	%xcc, rtrap
	 stx	%l2, [%sp + STACK_BIAS + REGWIN_SZ + PT_TNPC]

/* End of entry.S */
