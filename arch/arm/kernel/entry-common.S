#include <linux/config.h>
/*============================================================================
 * All exits to user mode from the kernel go through this code.
 */
		.globl	ret_from_sys_call

		.align	5
fast_syscall_return:
		str	r0, [sp, #S_R0 + 4]		@ returned r0
slow_syscall_return:
		add	sp, sp, #4
ret_from_sys_call:
		adr	r0, bh_data
		ldmia	r0, {r0, r4}
		ldr	r0, [r0]
		ldr	r1, [r4]
		tst	r0, r1
		blne	SYMBOL_NAME(do_bottom_half)
ret_with_reschedule:
		get_current_task r1		@ check for scheduling
		ldr	r0, [r1, #TSK_NEED_RESCHED]
		teq	r0, #0
		bne	ret_reschedule
		ldr	r1, [r1, #TSK_SIGPENDING]
		teq	r1, #0			@ check for signals
		bne	ret_signal

ret_from_all:	restore_user_regs

ret_signal:	mov	r1, sp
		adrsvc	al, lr, ret_from_all
		mov	r2, r4
		b	SYMBOL_NAME(do_signal)

ret_reschedule:	adrsvc	al, lr, ret_with_reschedule
		b	SYMBOL_NAME(schedule)

		.globl	ret_from_exception
ret_from_exception:
		adr	r0, bh_data
		ldmia	r0, {r0, r1}
		ldr	r0, [r0]
		ldr	r1, [r1]
		mov	r4, #0
		tst	r0, r1
		blne	SYMBOL_NAME(do_bottom_half)
		ldr	r0, [sp, #S_PSR]
		tst	r0, #3			@ returning to user mode?
		beq	ret_with_reschedule
		b	ret_from_all

#include "calls.S"

/*=============================================================================
 * SWI handler
 *-----------------------------------------------------------------------------
 *
 * We now handle sys-call tracing, and the errno in the task structure.
 * Still have a problem with >4 arguments for functions.  Theres only
 * a couple of functions in the code that have 5 arguments, so Im not
 * too worried.
 */

		.align	5
vector_swi:	save_user_regs
		mask_pc	lr, lr
		mov	fp, #0
		ldr	r6, [lr, #-4]		@ get SWI instruction
		arm700_bug_check r6, r7
#ifdef CONFIG_ALIGNMENT_TRAP
		ldr	r7, .LCswi
		ldr	r7, [r7]
		mcr	p15, 0, r7, c1, c0
#endif
		enable_irqs r7

		str	r4, [sp, #-4]!		@ new style: (r0 = arg1, r4 = arg5)
		adrsvc	al, lr, fast_syscall_return

		bic	r6, r6, #0xff000000	@ mask off SWI op-code
		eor	r6, r6, #OS_NUMBER<<20	@ check OS number
		cmp	r6, #NR_syscalls	@ check upper syscall limit
		bcs	2f

		get_current_task r7
		ldr	ip, [r7, #TSK_FLAGS]	@ check for syscall tracing
		adr	r5, SYMBOL_NAME(sys_call_table)
		tst	ip, #PF_TRACESYS
		ldreq	pc, [r5, r6, lsl #2]	@ call sys routine

		ldr	r7, [sp, #S_IP + 4]	@ save old IP
		mov	r0, #0
		str	r0, [sp, #S_IP + 4]	@ trace entry [IP = 0]
		bl	SYMBOL_NAME(syscall_trace)
		str	r7, [sp, #S_IP + 4]

		ldmib	sp, {r0 - r3}		@ have to reload r0 - r3
		mov	lr, pc
		ldr	pc, [r5, r6, lsl #2]	@ call sys routine
		str	r0, [sp, #S_R0 + 4]	@ returned r0

		mov	r0, #1
		str	r0, [sp, #S_IP + 4]	@ trace exit [IP = 1]
		bl	SYMBOL_NAME(syscall_trace)
		str	r7, [sp, #S_IP + 4]
		b	slow_syscall_return

2:		add	r1, sp, #4
		tst	r6, #0x00f00000		@ is it a Unix SWI?
		bne	3f
		subs	r0, r6, #(KSWI_SYS_BASE - KSWI_BASE)
		bcs	SYMBOL_NAME(arm_syscall)
		b	SYMBOL_NAME(sys_ni_syscall) @ not private func

3:		eor	r0, r6, #OS_NUMBER <<20	@ Put OS number back
		adrsvc	al, lr, slow_syscall_return
		b	SYMBOL_NAME(deferred)

		.align	5

bh_data:	.word	SYMBOL_NAME(bh_mask)
		.word	SYMBOL_NAME(bh_active)

ENTRY(sys_call_table)
#include "calls.S"

/*============================================================================
 * Special system call wrappers
 */
@ r0 = syscall number
@ r5 = syscall table
SYMBOL_NAME(sys_syscall):
		eor	r6, r0, #OS_NUMBER << 20
		cmp	r6, #NR_syscalls	@ check range
		ldmleib	sp, {r0 - r4}		@ get our args
		strle	r4, [sp]		@ Put our arg on the stack
		ldrle	pc, [r5, r6, lsl #2]
		mov	r0, #-ENOSYS
		mov	pc, lr

sys_fork_wrapper:
		add	r0, sp, #4
		b	SYMBOL_NAME(sys_fork)

sys_vfork_wrapper:
		add	r0, sp, #4
		b	SYMBOL_NAME(sys_vfork)

sys_execve_wrapper:
		add	r3, sp, #4
		b	SYMBOL_NAME(sys_execve)

sys_mount_wrapper:
		mov	r6, lr
		add	r5, sp, #4
		str	r5, [sp]
		str	r4, [sp, #-4]!
		bl	SYMBOL_NAME(sys_compat_mount)
		add	sp, sp, #4
		RETINSTR(mov,pc,r6)

sys_clone_wapper:
		add	r2, sp, #4
		b	SYMBOL_NAME(sys_clone)

sys_llseek_wrapper:
		mov	r6, lr
		add	r5, sp, #4
		str	r5, [sp]
		str	r4, [sp, #-4]!
		bl	SYMBOL_NAME(sys_compat_llseek)
		add	sp, sp, #4
		RETINSTR(mov,pc,r6)

sys_sigsuspend_wrapper:
		add	r3, sp, #4
		b	SYMBOL_NAME(sys_sigsuspend)

sys_rt_sigsuspend_wrapper:
		add	r2, sp, #4
		b	SYMBOL_NAME(sys_rt_sigsuspend)

sys_sigreturn_wrapper:
		add	r0, sp, #4
		b	SYMBOL_NAME(sys_sigreturn)

sys_rt_sigreturn_wrapper:
		add	r0, sp, #4
		b	SYMBOL_NAME(sys_rt_sigreturn)

sys_sigaltstack_wrapper:
		ldr	r2, [sp, #4 + S_SP]
		b	do_sigaltstack

		.data

ENTRY(fp_enter)
		.word	fpe_not_present
