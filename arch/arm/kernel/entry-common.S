/*============================================================================
 * All exits to user mode from the kernel go through this code.
 */

#include <linux/config.h>

		.globl	ret_from_sys_call

ret_from_exception:
		adr	r0, 1f
		ldmia	r0, {r0, r1}
		ldr	r0, [r0]
		ldr	r1, [r1]
		tst	r0, r1
		blne	SYMBOL_NAME(do_bottom_half)
ret_from_intr:	ldr	r0, [sp, #S_PSR]
		tst	r0, #3
		beq	ret_with_reschedule
		b	ret_from_all

ret_signal:	mov	r1, sp
		adrsvc	al, lr, ret_from_all
		b	SYMBOL_NAME(do_signal)

2:		bl	SYMBOL_NAME(schedule)

ret_from_sys_call:
		adr	r0, 1f
		ldmia	r0, {r0, r1}
		ldr	r0, [r0]
		ldr	r1, [r1]
		tst	r0, r1
		adrsvc	ne, lr, ret_from_intr
		bne	SYMBOL_NAME(do_bottom_half)

ret_with_reschedule:
		get_current_task r1
		ldr	r0, [r1, #TSK_NEED_RESCHED]
		teq	r0, #0
		bne	2b
		ldr	r1, [r1, #TSK_SIGPENDING]
		teq	r1, #0
		bne	ret_signal

ret_from_all:	restore_user_regs

1:		.word	SYMBOL_NAME(bh_mask)
		.word	SYMBOL_NAME(bh_active)

/*=============================================================================
 * SWI handler
 *-----------------------------------------------------------------------------
 *
 * We now handle sys-call tracing, and the errno in the task structure.
 * Still have a problem with >4 arguments for functions.  Theres only
 * a couple of functions in the code that have 5 arguments, so Im not
 * too worried.
 */

#include "calls.S"

vector_swi:	save_user_regs
		mov	fp, #0
		mask_pc	lr, lr
		ldr	r6, [lr, #-4]!		@ get SWI instruction
		arm700_bug_check r6, r7
		enable_irqs r7
		
		bic	r6, r6, #0xff000000	@ mask off SWI op-code
		eor	r6, r6, #OS_NUMBER<<20	@ check OS number
		cmp	r6, #NR_syscalls	@ check upper syscall limit
		bcs	2f

		get_current_task r5
		ldr	ip, [r5, #TSK_FLAGS]	@ check for syscall tracing
		tst	ip, #PF_TRACESYS
		bne	1f

		adr	ip, SYMBOL_NAME(sys_call_table)
		str	r4, [sp, #-4]!		@ new style: (r0 = arg1, r5 = arg5)
		mov	lr, pc
		ldr	pc, [ip, r6, lsl #2]	@ call sys routine
		add	sp, sp, #4
		str	r0, [sp, #S_R0]		@ returned r0
		b	ret_from_sys_call

1:		ldr	r7, [sp, #S_IP]		@ save old IP
		mov	r0, #0
		str	r0, [sp, #S_IP]		@ trace entry [IP = 0]
		bl	SYMBOL_NAME(syscall_trace)
		str	r7, [sp, #S_IP]
		ldmia	sp, {r0 - r3}		@ have to reload r0 - r3
		adr	ip, SYMBOL_NAME(sys_call_table)
		str	r4, [sp, #-4]!		@ new style: (r0 = arg1, r5 = arg5)
		mov	lr, pc
		ldr	pc, [ip, r6, lsl #2]	@ call sys routine
		add	sp, sp, #4
		str	r0, [sp, #S_R0]		@ returned r0
		mov	r0, #1
		str	r0, [sp, #S_IP]		@ trace exit [IP = 1]
		bl	SYMBOL_NAME(syscall_trace)
		str	r7, [sp, #S_IP]
		b	ret_from_sys_call

2:		tst	r6, #0x00f00000		@ is it a Unix SWI?
		bne	3f
		cmp	r6, #(KSWI_SYS_BASE - KSWI_BASE)
		bcc	4f			@ not private func
		bic	r0, r6, #0x000f0000
		mov	r1, sp
		bl	SYMBOL_NAME(arm_syscall)
		b	ret_from_sys_call

3:		eor	r0, r6, #OS_NUMBER<<20	@ Put OS number back
		mov	r1, sp
		bl	SYMBOL_NAME(deferred)
		ldmfd	sp, {r0 - r3}
		b	ret_from_sys_call

4:		bl	SYMBOL_NAME(sys_ni_syscall)
		str	r0, [sp, #0]		@ returned r0
		b	ret_from_sys_call

@ r0 = syscall number
@ r1 = syscall r0
@ r5 = syscall r4
@ ip = syscall table
SYMBOL_NAME(sys_syscall):
		mov	r6, r0
		eor	r6, r6, #OS_NUMBER << 20
		cmp	r6, #NR_syscalls		@ check range
		movgt	r0, #-ENOSYS
		movgt	pc, lr
		add	sp, sp, #4			@ take of the save of our r4
		ldmib	sp, {r0 - r4}			@ get our args
		str	r4, [sp, #-4]!			@ Put our arg on the stack
		ldr	pc, [ip, r6, lsl #2]

ENTRY(sys_call_table)
#include "calls.S"

/*============================================================================
 * Special system call wrappers
 */
sys_fork_wrapper:
		add	r0, sp, #4
		b	SYMBOL_NAME(sys_fork)

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

/*
 *=============================================================================
 *		Low-level interface code
 *-----------------------------------------------------------------------------
 *		Trap initialisation
 *-----------------------------------------------------------------------------
 *
 * Note - FIQ code has changed.  The default is a couple of words in 0x1c, 0x20
 * that call _unexp_fiq.  Nowever, we now copy the FIQ routine to 0x1c (removes
 * some excess cycles).
 *
 * What we need to put into 0-0x1c are ldrs to branch to 0xC0000000
 * (the kernel).
 * 0x1c onwards is reserved for FIQ, so I think that I will allocate 0xe0 onwards for
 * the actual address to jump to.
 */

		.section ".text.init",#alloc,#execinstr

#if defined(CONFIG_CPU_32)
/*
 * these go into 0x00
 */
.Lbranches:	swi	SYS_ERROR0
		ldr	pc, .Lbranches + 0xe4
		ldr	pc, .Lbranches + 0xe8
		ldr	pc, .Lbranches + 0xec
		ldr	pc, .Lbranches + 0xf0
		ldr	pc, .Lbranches + 0xf4
		ldr	pc, .Lbranches + 0xf8
		ldr	pc, .Lbranches + 0xfc
/*
 * this is put into 0xe4 and above
 */
.Ljump_addresses:
		.word	vector_undefinstr	@ 0xe4
		.word	vector_swi		@ 0xe8
		.word	vector_prefetch		@ 0xec
		.word	vector_data		@ 0xf0
		.word	vector_addrexcptn	@ 0xf4
		.word	vector_IRQ		@ 0xf8
		.word	_unexp_fiq		@ 0xfc
/*
 * initialise the trap system
 */
ENTRY(trap_init)
		stmfd	sp!, {r4 - r7, lr}
		initialise_traps_extra
		mov	r0, #0xe4
		adr	r1, .Ljump_addresses
		ldmia	r1, {r1 - r7}
		stmia	r0, {r1 - r7}
		mov	r0, #0
		adr	r1, .Lbranches
		ldmia	r1, {r1 - r7}
		stmia	r0, {r1 - r7}
		LOADREGS(fd, sp!, {r4 - r7, pc})
#elif defined(CONFIG_CPU_26)
.Ljump_addresses:
		swi	SYS_ERROR0
		.word	vector_undefinstr	- 12
		.word	vector_swi		- 16
		.word	vector_prefetch		- 20
		.word	vector_data		- 24
		.word	vector_addrexcptn	- 28
		.word	vector_IRQ		- 32
		.word	_unexp_fiq		- 36
		b	. + 8
/*
 * initialise the trap system
 */
ENTRY(trap_init)
		stmfd	sp!, {r4 - r7, lr}
		adr	r1, .Ljump_addresses
		ldmia	r1, {r1 - r7, ip, lr}
		orr	r2, lr, r2, lsr #2
		orr	r3, lr, r3, lsr #2
		orr	r4, lr, r4, lsr #2
		orr	r5, lr, r5, lsr #2
		orr	r6, lr, r6, lsr #2
		orr	r7, lr, r7, lsr #2
		orr	ip, lr, ip, lsr #2
		mov	r0, #0
		stmia	r0, {r1 - r7, ip}
		ldmfd	sp!, {r4 - r7, pc}^
#endif

		.previous

/*============================================================================
 * FP support
 */

		.data

ENTRY(fp_enter)
		.word	fpe_not_present
