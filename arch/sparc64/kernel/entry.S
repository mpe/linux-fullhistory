/* $Id: entry.S,v 1.1 1997/03/18 17:58:59 jj Exp $
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

#define curptr      g6

#define NR_SYSCALLS 256      /* Each OS is different... */

	.text
        .align  4
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
