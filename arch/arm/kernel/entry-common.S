#include <linux/config.h>
/*============================================================================
 * All exits to user mode from the kernel go through this code.
 */

#define S_OFF		8
#define SYSCALL_REGS	r4, r5

/*
 * Define to favour ARM8, ARM9 and StrongARM cpus.  This says that it is
 * cheaper to use two LDR instructions than a two-register LDM, if the
 * latter would entail calculating an address specially.
 */
#if defined(CONFIG_CPU_SA110)
#define HARVARD_CACHE
#endif

		.globl	ret_from_sys_call

		.align	5
fast_syscall_return:
		str	r0, [sp, #S_R0 + S_OFF]		@ returned r0
slow_syscall_return:
		add	sp, sp, #S_OFF
ret_from_sys_call:
#ifdef HARVARD_CACHE
		ldr	r0, bh_data
		ldr	r4, bh_data+4
#else
		adr	r0, bh_data
		ldmia	r0, {r0, r4}
#endif
		ldr	r0, [r0]
		ldr	r1, [r4]
		tst	r0, r1
		blne	SYMBOL_NAME(do_bottom_half)
ret_with_reschedule:
		get_current_task r5
		ldr	r0, [r5, #TSK_NEED_RESCHED]
		ldr	r1, [r5, #TSK_SIGPENDING]
		teq	r0, #0
		bne	ret_reschedule
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
#ifdef HARVARD_CACHE
		ldr	r0, bh_data
		ldr	r1, bh_data + 4
#else
		adr	r0, bh_data
		ldmia	r0, {r0, r1}
#endif
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
 */

/*
 * Create some aliases for some registers.  These should allow
 * us to have in theory up to 7 arguments to a function.
 */
scno		.req	r9			@ syscall number
tbl		.req	r8			@ syscall table pointer
tip		.req	r7			@ temporary IP

		.align	5
vector_swi:	save_user_regs
		mask_pc	lr, lr
		mov	fp, #0
		ldr	scno, [lr, #-4]		@ get SWI instruction
		arm700_bug_check scno, ip
#ifdef CONFIG_ALIGNMENT_TRAP
		ldr	ip, .LCswi
		ldr	ip, [ip]
		mcr	p15, 0, ip, c1, c0
#endif
		enable_irqs ip

		stmdb	sp!, {SYSCALL_REGS}	@ new style: (r0 = arg1, r4 = arg5, r5 = arg6)
						@ Note that we dont have to handle
						@ sys_syscalls arg7 here
		adrsvc	al, lr, fast_syscall_return

		bic	scno, scno, #0xff000000	@ mask off SWI op-code
		eor	scno, scno, #OS_NUMBER<<20	@ check OS number
		cmp	scno, #NR_syscalls	@ check upper syscall limit
		bcs	2f

		get_current_task ip
		ldr	ip, [ip, #TSK_FLAGS]	@ check for syscall tracing
		adr	tbl, SYMBOL_NAME(sys_call_table)
		tst	ip, #PF_TRACESYS
		ldreq	pc, [tbl, scno, lsl #2]	@ call sys routine

		ldr	tip, [sp, #S_IP + S_OFF]	@ save old IP
		mov	ip, #0
		str	ip, [sp, #S_IP + S_OFF]	@ trace entry [IP = 0]
		bl	SYMBOL_NAME(syscall_trace)
		str	tip, [sp, #S_IP + S_OFF]

		add	ip, sp, #S_OFF
		ldmia	ip, {r0 - r3}		@ have to reload r0 - r3
		mov	lr, pc
		ldr	pc, [tbl, scno, lsl #2]	@ call sys routine
		str	r0, [sp, #S_R0 + S_OFF]	@ returned r0

		mov	ip, #1
		str	ip, [sp, #S_IP + S_OFF]	@ trace exit [IP = 1]
		bl	SYMBOL_NAME(syscall_trace)
		str	tip, [sp, #S_IP + S_OFF]
		b	slow_syscall_return

2:		add	r1, sp, #S_OFF
		tst	scno, #0x00f00000	@ is it a Unix SWI?
		bne	3f
		subs	r0, scno, #(KSWI_SYS_BASE - KSWI_BASE)
		bcs	SYMBOL_NAME(arm_syscall)
		b	SYMBOL_NAME(sys_ni_syscall) @ not private func

3:		eor	r0, scno, #OS_NUMBER <<20	@ Put OS number back
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
		eor	scno, r0, #OS_NUMBER << 20
		cmp	scno, #NR_syscalls	@ check range
		add	ip, sp, #S_OFF
		ldmleib	ip, {r0 - r3, SYSCALL_REGS}	@ get our args
		stmleia	sp, {SYSCALL_REGS}	@ Put our arg on the stack
		ldrle	pc, [tbl, scno, lsl #2]
		mov	r0, #-ENOSYS
		RETINSTR(mov,pc,lr)

sys_fork_wrapper:
		add	r0, sp, #S_OFF
		b	SYMBOL_NAME(sys_fork)

sys_vfork_wrapper:
		add	r0, sp, #S_OFF
		b	SYMBOL_NAME(sys_vfork)

sys_execve_wrapper:
		add	r3, sp, #S_OFF
		b	SYMBOL_NAME(sys_execve)

sys_clone_wapper:
		add	r2, sp, #S_OFF
		b	SYMBOL_NAME(sys_clone)

sys_sigsuspend_wrapper:
		add	r3, sp, #S_OFF
		b	SYMBOL_NAME(sys_sigsuspend)

sys_rt_sigsuspend_wrapper:
		add	r2, sp, #S_OFF
		b	SYMBOL_NAME(sys_rt_sigsuspend)

sys_sigreturn_wrapper:
		add	r0, sp, #S_OFF
		b	SYMBOL_NAME(sys_sigreturn)

sys_rt_sigreturn_wrapper:
		add	r0, sp, #S_OFF
		b	SYMBOL_NAME(sys_rt_sigreturn)

sys_sigaltstack_wrapper:
		ldr	r2, [sp, #S_OFF + S_SP]
		b	do_sigaltstack

		.data

ENTRY(fp_enter)
		.word	fpe_not_present
