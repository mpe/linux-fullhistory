/* $Id: entry.S,v 1.50 1997/07/15 16:53:00 davem Exp $
 * arch/sparc64/kernel/entry.S:  Sparc64 trap low-level entry points.
 *
 * Copyright (C) 1995,1997 David S. Miller (davem@caip.rutgers.edu)
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

/* #define SYSCALL_TRACING */

#define curptr      g6

#define NR_SYSCALLS 256      /* Each OS is different... */

	.text
	.globl	sparc64_dtlb_prot_catch, sparc64_dtlb_refbit_catch
	.globl	sparc64_itlb_refbit_catch

	/* Note, DMMU SFAR not updated for fast tlb data access miss
	 * traps, so we must use tag access to find the right page.
	 * However for DMMU fast protection traps it is updated so
	 * we use, but we must also clear it _before_ we enable interrupts
	 * and save state because there is a race where we can push a user
	 * window right now in etrap, a protection fault happens (for example
	 * to update the dirty bit) and since we left crap in the sfsr
	 * it will not get updated properly.
	 */
	.align		32
sparc64_dtlb_prot_catch:
	wr		%g0, ASI_DMMU, %asi
	rdpr		%pstate, %g1
	wrpr		%g1, PSTATE_AG|PSTATE_MG, %pstate
	rdpr		%tl, %g3
	ldxa		[%g0 + TLB_TAG_ACCESS] %asi, %g5
	stxa		%g0, [%g0 + TLB_SFSR] %asi
	membar		#Sync
	cmp		%g3, 1

	bgu,a,pn	%icc, winfix_trampoline
	 rdpr		%tpc, %g3
	ba,pt		%xcc, etrap
	 rd		%pc, %g7
	b,pt		%xcc, 1f
	 mov		1, %o2
sparc64_dtlb_refbit_catch:
	srlx		%g5, 9, %g4
	and		%g4, ((_PAGE_PRESENT | _PAGE_READ) >> 9), %g4

	cmp		%g4, ((_PAGE_PRESENT | _PAGE_READ) >> 9)
	be,a,pt		%xcc, 2f
	 mov		1, %g4
	wr		%g0, ASI_DMMU, %asi
	rdpr		%pstate, %g1
	wrpr		%g1, PSTATE_AG|PSTATE_MG, %pstate
	rdpr		%tl, %g3
	ldxa		[%g0 + TLB_TAG_ACCESS] %asi, %g5

	cmp		%g3, 1
	bgu,pn		%icc, winfix_trampoline
	 rdpr		%tpc, %g3
	b,pt		%xcc, etrap
	 rd		%pc, %g7
	clr		%o2
1:	srlx		%l5, PAGE_SHIFT, %o1
	add		%sp, STACK_BIAS + REGWIN_SZ, %o0

	call		do_sparc64_fault
	 sllx		%o1, PAGE_SHIFT, %o1
	b,pt		%xcc, rtrap
	 clr		%l6
	nop
	nop
	nop
	nop

sparc64_itlb_refbit_catch:
	srlx		%g5, 9, %g4
	and		%g4, ((_PAGE_PRESENT | _PAGE_READ) >> 9), %g4
	cmp		%g4, ((_PAGE_PRESENT | _PAGE_READ) >> 9)
	be,a,pt		%xcc, 3f
	 mov		1, %g4
	rdpr		%pstate, %g1
	wrpr		%g1, PSTATE_AG|PSTATE_MG, %pstate
	rdpr		%tpc, %g5

	b,pt		%xcc, etrap
	 rd		%pc, %g7
	b,pt		%xcc, 1b
	 clr		%o2
2:	sllx		%g4, 63, %g4				! _PAGE_VALID
	or		%g5, _PAGE_ACCESSED, %g5
	or		%g5, %g4, %g5
	stxa		%g5, [%g3 + %g1] ASI_PHYS_USE_EC	! store new PTE

	stxa		%g5, [%g0] ASI_DTLB_DATA_IN		! TLB load
	retry
3:	sllx		%g4, 63, %g4				! _PAGE_VALID
	or		%g5, _PAGE_ACCESSED, %g5
	or		%g5, %g4, %g5
	stxa		%g5, [%g3 + %g1] ASI_PHYS_USE_EC	! store new PTE
	stxa		%g5, [%g0] ASI_ITLB_DATA_IN		! TLB load
	retry

	/* This is trivial with the new code... */
	.align		32
	.globl		do_fpdis
do_fpdis:
	wr		%g0, FPRS_FEF, %fprs
	ldx		[%g6 + AOFF_task_flags], %g2
	sethi		%hi(0x00100000), %g4		! XXX PF_USEDFPU
	andcc		%g2, %g4, %g0

	bne,a,pt	%xcc, fpload_fromkstk
	 sethi		%hi((((PAGE_SIZE<<1)-((64*4)+(2*8))) & ~(64 - 1))), %g2
	fzero		%f0
	fzero		%f2
	faddd		%f0, %f2, %f4
	fmuld		%f0, %f2, %f6
	faddd		%f0, %f2, %f8
	fmuld		%f0, %f2, %f10

	faddd		%f0, %f2, %f12
	fmuld		%f0, %f2, %f14
	faddd		%f0, %f2, %f16
	fmuld		%f0, %f2, %f18
	faddd		%f0, %f2, %f20
	fmuld		%f0, %f2, %f22
	faddd		%f0, %f2, %f24
	fmuld		%f0, %f2, %f26

	faddd		%f0, %f2, %f28
	fmuld		%f0, %f2, %f30
	faddd		%f0, %f2, %f32
	fmuld		%f0, %f2, %f34
	faddd		%f0, %f2, %f36
	fmuld		%f0, %f2, %f38
	faddd		%f0, %f2, %f40
	fmuld		%f0, %f2, %f42

	faddd		%f0, %f2, %f44
	fmuld		%f0, %f2, %f46
	ldx		[%g6 + AOFF_task_flags], %g2
	faddd		%f0, %f2, %f48
	fmuld		%f0, %f2, %f50
	or		%g2, %g4, %g2
	faddd		%f0, %f2, %f52
	fmuld		%f0, %f2, %f54

	stx		%g2, [%g6 + AOFF_task_flags]
	faddd		%f0, %f2, %f56
	sethi		%hi(empty_zero_page), %g3
	fmuld		%f0, %f2, %f58

	faddd		%f0, %f2, %f60
	ldx		[%g3], %fsr			! wheee, empty_zero_page
	b,pt		%xcc, fpdis_exit
	 wr		%g0, 0, %gsr

fpload_fromkstk:
	or		%g2, %lo((((PAGE_SIZE<<1)-((64*4)+(2*8))) & ~(64 - 1))), %g2
	add		%g6, %g2, %g2
	mov		SECONDARY_CONTEXT, %g3
	stxa		%g0, [%g3] ASI_DMMU
	flush		%g2
	wr		%g0, ASI_BLK_S, %asi	! grrr, where is ASI_BLK_NUCLEUS 8-(
	membar		#StoreLoad | #LoadLoad

	ldda		[%g2 + 0x000] %asi, %f0
	ldda		[%g2 + 0x040] %asi, %f16
	ldda		[%g2 + 0x080] %asi, %f32
	ldda		[%g2 + 0x0c0] %asi, %f48
	ldx		[%g2 + 0x100], %fsr
	ldx		[%g2 + 0x108], %g2
	membar		#Sync
	wr		%g2, 0, %gsr
fpdis_exit:
	rdpr		%tstate, %g3
	sethi		%hi(TSTATE_PEF), %g4
	or		%g3, %g4, %g3		! anal...
	wrpr		%g3, %tstate
	retry

#ifdef __SMP__
	/* Note check out head.h, this code isn't even used for UP,
	 * for SMP things will be different.  In particular the data
	 * registers for cross calls will be:
	 *
	 * DATA 0: [low 32-bits]  Address of function to call, jmp to this
	 *         [high 32-bits] MMU Context Argument 0, place in %g5
	 * DATA 1: Address Argument 1, place in %g6
	 * DATA 2: Address Argument 2, place in %g7
	 *
	 * With this method we can do most of the cross-call tlb/cache
	 * flushing very quickly.
	 */
	.align	32
	.globl	do_ivec
do_ivec:
	ldxa		[%g0] ASI_INTR_RECEIVE, %g1
	andcc		%g1, 0x20, %g0
	be,pn		%xcc, do_ivec_return
	 mov		0x40, %g2

	/* Load up Interrupt Vector Data 0 register. */
	sethi		%hi(KERNBASE), %g4
	ldxa		[%g2] ASI_UDB_INTR_R, %g3
	cmp		%g3, %g4
	bgeu,pn		%xcc, do_ivec_xcall
	 nop
	and		%g3, 0x7ff, %g3
	sllx		%g3, 3, %g3
	ldx		[%g1 + %g3], %g2
	brz,pn		%g2, do_ivec_spurious
	 nop

	/* No branches, worse case we don't know about this interrupt
	 * yet, so we would just write a zero into the softint register
	 * which is completely harmless.
	 */
	wr		%g2, 0x0, %set_softint

do_ivec_return:
	/* Acknowledge the UPA */
	stxa		%g0, [%g0] ASI_INTR_RECEIVE
	membar		#Sync
	retry
do_ivec_xcall:
	srlx		%g3, 32, %g5
	add		%g2, 0x10, %g2
	sra		%g3, 0, %g3
	ldxa		[%g2] ASI_UDB_INTR_R, %g6
	add		%g2, 0x10, %g2
	jmpl		%g3, %g0
	 ldxa		[%g2] ASI_UDB_INTR_R, %g7
do_ivec_spurious:
	stxa		%g0, [%g0] ASI_INTR_RECEIVE
	membar		#Sync
	rdpr		%pstate, %g1
	wrpr		%g1, PSTATE_IG | PSTATE_AG, %pstate
	ba,pt		%xcc, etrap
	 rd		%pc, %g7
	call		report_spurious_ivec
	 add		%sp, STACK_BIAS + REGWIN_SZ, %o0
	ba,pt		%xcc, rtrap
	 clr		%l6
#endif /* __SMP__ */

	.globl		getcc, setcc
getcc:
	ldx		[%o0 + PT_V9_TSTATE], %o1
	srlx		%o1, 32, %o1
	and		%o1, 0xf, %o1
	retl
	 stx		%o1, [%o0 + PT_V9_G1]
setcc:
	ldx		[%o0 + PT_V9_TSTATE], %o1
	ldx		[%o0 + PT_V9_G1], %o2
	or		%g0, %ulo(TSTATE_ICC), %o3
	sllx		%o3, 32, %o3
	andn		%o1, %o3, %o1
	sllx		%o2, 32, %o2
	and		%o2, %o3, %o2
	or		%o1, %o2, %o1
	retl
	 stx		%o1, [%o0 + PT_V9_TSTATE]

#ifdef CONFIG_BLK_DEV_FD
	.globl		floppy_hardint
floppy_hardint:
	sethi		%hi(doing_pdma), %g1
	ld		[%g1 + %lo(doing_pdma)], %g2
	brz,pn		%g2, floppy_dosoftint
	 sethi		%hi(fdc_status), %g3
	ldx		[%g3 + %lo(fdc_status)], %g3
	sethi		%hi(pdma_vaddr), %g5
	ldx		[%g5 + %lo(pdma_vaddr)], %g4
	sethi		%hi(pdma_size), %g5
	ldx		[%g5 + %lo(pdma_size)], %g5

next_byte:
	ldub		[%g3], %g7
	andcc		%g7, 0x80, %g0
	be,pn		%icc, floppy_fifo_emptied
	 andcc		%g7, 0x20, %g0
	be,pn		%icc, floppy_overrun
	 andcc		%g7, 0x40, %g0
	be,pn		%icc, floppy_write
	 sub		%g5, 1, %g5

	ldub		[%g3 + 1], %g7
	orcc		%g0, %g5, %g0
	stb		%g7, [%g4]
	bne,pn		%xcc, next_byte
	 add		%g4, 1, %g4

	b,pt		%xcc, floppy_tdone
	 nop

floppy_write:
	ldub		[%g4], %g7
	orcc		%g0, %g5, %g0
	stb		%g7, [%g3 + 1]
	bne,pn		%xcc, next_byte
	 add		%g4, 1, %g4

floppy_tdone:
	sethi		%hi(pdma_vaddr), %g1
	stx		%g4, [%g1 + %lo(pdma_vaddr)]
	sethi		%hi(pdma_size), %g1
	stx		%g5, [%g1 + %lo(pdma_size)]
	sethi		%hi(auxio_register), %g1
	ldx		[%g1 + %lo(auxio_register)], %g7
	ldub		[%g7], %g5
	or		%g5, 0xc2, %g5
	stb		%g5, [%g7]
	andn		%g5, 0x02, %g5

	nop; nop;  nop; nop;  nop; nop;
	nop; nop;  nop; nop;  nop; nop;

	stb		%g5, [%g7]
	sethi		%hi(doing_pdma), %g1
	b,pt		%xcc, floppy_dosoftint
	 st		%g0, [%g1 + %lo(doing_pdma)]

floppy_fifo_emptied:
	sethi		%hi(pdma_vaddr), %g1
	stx		%g4, [%g1 + %lo(pdma_vaddr)]
	sethi		%hi(pdma_size), %g1
	stx		%g5, [%g1 + %lo(pdma_size)]
	sethi		%hi(irq_action), %g1
	or		%g1, %lo(irq_action), %g1
	ldx		[%g1 + (11 << 3)], %g3		! irqaction[floppy_irq]
	ldx		[%g3 + 0x10], %g4		! action->mask
	st		%g0, [%g4]			! SYSIO_ICLR_IDLE
	membar		#Sync				! probably not needed...
	retry

floppy_overrun:
	sethi		%hi(pdma_vaddr), %g1
	stx		%g4, [%g1 + %lo(pdma_vaddr)]
	sethi		%hi(pdma_size), %g1
	stx		%g5, [%g1 + %lo(pdma_size)]
	sethi		%hi(doing_pdma), %g1
	st		%g0, [%g1 + %lo(doing_pdma)]

floppy_dosoftint:
	rdpr		%pil, %g2
	wrpr		%g0, 15, %pil
	b,pt		%xcc, etrap_irq
	 rd		%pc, %g7

	mov		11, %o0
	mov		0, %o1
	call		sparc_floppy_irq
	 add		%sp, STACK_BIAS + REGWIN_SZ, %o2

	b,pt		%xcc, rtrap
	 clr		%l6

#endif /* CONFIG_BLK_DEV_FD */

	/* XXX Here is stuff we still need to write... -DaveM XXX */
	.globl		indirect_syscall, netbsd_syscall, solaris_syscall
indirect_syscall:
netbsd_syscall:
solaris_syscall:
	retl
	 nop

	.globl		do_mna
do_mna:
	rdpr		%tl, %g3
	cmp		%g3, 1
	bgu,a,pn	%icc, winfix_mna
	 rdpr		%tpc, %g3
	ba,pt		%xcc, etrap
	 rd		%pc, %g7
	call		mem_address_unaligned
	 add		%sp, STACK_BIAS + REGWIN_SZ, %o0
	ba,pt		%xcc, rtrap
	 clr		%l6

	.globl	breakpoint_trap
breakpoint_trap:
	call		sparc_breakpoint
	 add		%sp, STACK_BIAS + REGWIN_SZ, %o0
	ba,pt		%xcc, rtrap
	 nop

	/* SunOS uses syscall zero as the 'indirect syscall' it looks
	 * like indir_syscall(scall_num, arg0, arg1, arg2...);  etc.
	 * This is complete brain damage.
	 */
	.globl	sunos_indir
sunos_indir:
	srl		%o0, 0, %o0
	mov		%o7, %l4
	cmp		%o0, NR_SYSCALLS
	blu,a,pt	%icc, 1f
	 sll		%o0, 0x3, %o0
	sethi		%hi(sunos_nosys), %l6
	b,pt		%xcc, 2f
	 or		%l6, %lo(sunos_nosys), %l6
1:	sethi		%hi(sunos_sys_table), %l7
	or		%l7, %lo(sunos_sys_table), %l7
	ldx		[%l7 + %o0], %l6
2:	mov		%o1, %o0
	mov		%o2, %o1
	mov		%o3, %o2
	mov		%o4, %o3
	mov		%o5, %o4
	call		%l6
	 mov		%l4, %o7

	.globl	sunos_getpid
sunos_getpid:
	call	sys_getppid
	 nop
	call	sys_getpid
	 stx	%o0, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I1]
	b,pt	%xcc, ret_sys_call
	 stx	%o0, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I0]

	/* SunOS getuid() returns uid in %o0 and euid in %o1 */
	.globl	sunos_getuid
sunos_getuid:
	call	sys_geteuid
	 nop
	call	sys_getuid
	 stx	%o0, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I1]
	b,pt	%xcc, ret_sys_call
	 stx	%o0, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I0]

	/* SunOS getgid() returns gid in %o0 and egid in %o1 */
	.globl	sunos_getgid
sunos_getgid:
	call	sys_getegid
	 nop
	call	sys_getgid
	 stx	%o0, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I1]
	b,pt	%xcc, ret_sys_call
	 stx	%o0, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I0]

	/* SunOS's execv() call only specifies the argv argument, the
	 * environment settings are the same as the calling processes.
	 */
	.globl	sunos_execv
sunos_execv:
	sethi	%hi(sparc32_execve), %g1
	stx	%g0, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I2]
	jmpl	%g1 + %lo(sparc32_execve), %g0
	 add	%sp, STACK_BIAS + REGWIN_SZ, %o0

	.globl	sys_pipe, sys_execve, sys_sigpause, sys_nis_syscall
	.globl	sys_sigsuspend, sys_sigreturn
	.globl	sys32_execve, sys_ptrace
	.align	32
sys_pipe:	sethi		%hi(sparc_pipe), %g1
		add		%sp, STACK_BIAS + REGWIN_SZ, %o0
		jmpl		%g1 + %lo(sparc_pipe), %g0
		 nop
sys_nis_syscall:sethi		%hi(c_sys_nis_syscall), %g1
		add		%sp, STACK_BIAS + REGWIN_SZ, %o0
		jmpl		%g1 + %lo(c_sys_nis_syscall), %g0
		 nop

sys_execve:	sethi		%hi(sparc_execve), %g1
		add		%sp, STACK_BIAS + REGWIN_SZ, %o0
		jmpl		%g1 + %lo(sparc_execve), %g0
		 nop
sys32_execve:	sethi		%hi(sparc32_execve), %g1
		add		%sp, STACK_BIAS + REGWIN_SZ, %o0
		jmpl		%g1 + %lo(sparc32_execve), %g0
		 nop

		/* NOTE: %o0 has a correct value already */
sys_sigpause:	call		do_sigpause
		 add		%sp, STACK_BIAS + REGWIN_SZ, %o1
		ldx		[%curptr + AOFF_task_flags], %l5
		andcc		%l5, 0x20, %g0
		be,pt		%icc, rtrap
		 clr		%l6
		call		syscall_trace
		 nop

		ba,pt		%xcc, rtrap
		 clr		%l6
linux_sparc_ni_syscall:
		sethi		%hi(sys_ni_syscall), %l7
		b,pt		%xcc,syscall_is_too_hard
		 or		%l7, %lo(sys_ni_syscall), %l7
		nop

		.align		32
sys_sigsuspend:	call		do_sigsuspend
		 add		%sp, STACK_BIAS + REGWIN_SZ, %o0
		ldx		[%curptr + AOFF_task_flags], %l5
		andcc		%l5, 0x20, %g0
		be,pt		%icc, rtrap
		 clr		%l6
		call		syscall_trace
		 nop

		ba,pt		%xcc, rtrap
		 clr		%l6
	
		.align		32
sys_sigreturn:	call		do_sigreturn
		 add		%sp, STACK_BIAS + REGWIN_SZ, %o0
		ldx		[%curptr + AOFF_task_flags], %l5
		andcc		%l5, 0x20, %g0
		be,pt		%icc, rtrap
		 clr		%l6
		call		syscall_trace
		 nop

		ba,pt		%xcc, rtrap
		 clr		%l6

		.align		32
sys_ptrace:	call		do_ptrace
		 add		%sp, STACK_BIAS + REGWIN_SZ, %o0
		ldx		[%curptr + AOFF_task_flags], %l5
		andcc		%l5, 0x20, %g0
		be,pt		%icc, rtrap
		 clr		%l6
		call		syscall_trace
		 nop

		ba,pt		%xcc, rtrap
		 clr		%l6
	
	/* This is how fork() was meant to be done, 12 instruction entry.
	 *
	 * I questioned the following code briefly, let me clear things
	 * up so you must not reason on it like I did.
	 *
	 * Know the fork_kpsr etc. we use in the sparc32 port?  We don't
	 * need it here because the only piece of window state we copy to
	 * the child is the CWP register.  Even if the parent sleeps,
	 * we are safe because we stuck it into pt_regs of the parent
	 * so it will not change.
	 *
	 * XXX This raises the question, whether we can do the same on
	 * XXX sparc32 to get rid of fork_kpsr _and_ fork_kwim.  The
	 * XXX answer is yes.  We stick fork_kpsr in UREG_G0 and
	 * XXX fork_kwim in UREG_G1 (global registers are considered
	 * XXX volatile across a system call in the sparc ABI I think
	 * XXX if it isn't we can use regs->y instead, anyone who depends
	 * XXX upon the Y register being preserved across a fork deserves
	 * XXX to lose).
	 *
	 * In fact we should take advantage of that fact for other things
	 * during system calls...
	 */
	.globl	sys_fork, sys_vfork, sys_clone
	.globl	ret_from_syscall, ret_from_smpfork
	.align	32
sys_fork:
sys_vfork:	mov		SIGCHLD, %o0
		clr		%o1
sys_clone:	mov		%o7, %l5
/*???*/		save		%sp, -REGWIN_SZ, %sp
		flushw
/*???*/		restore		%g0, %g0, %g0
		rdpr		%cwp, %o4
		add		%sp, STACK_BIAS + REGWIN_SZ, %o2

		movrz		%o1, %fp, %o1
		stx		%o4, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_G0]
		call		do_fork
		 mov		%l5, %o7
#ifdef __SMP__
ret_from_smpfork:
		sethi		%hi(scheduler_lock), %o4
		membar		#StoreStore | #LoadStore
		stb		%g0, [%o4 + %lo(scheduler_lock)]
#endif
ret_from_syscall:
		b,pt		%xcc, ret_sys_call
		 ldx		[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I0], %o0

linux_syscall_trace:
		call		syscall_trace
		 nop
		mov		%i0, %o0
		mov		%i1, %o1
		mov		%i2, %o2
		mov		%i3, %o3
		b,pt		%xcc, 2f
		 mov		%i4, %o4

	/* Linux native and SunOS system calls enter here... */
	.align	32
	.globl	linux_sparc_syscall, syscall_is_too_hard, ret_sys_call
linux_sparc_syscall:
	/* Direct access to user regs, must faster. */
	cmp		%g1, NR_SYSCALLS			! IEU1	Group
	bgeu,pn		%xcc, linux_sparc_ni_syscall		! CTI
	 mov		%i0, %o0				! IEU0
	sll		%g1, 3, %l4				! IEU0	Group
	mov		%i1, %o1				! IEU1
	ldx		[%l7 + %l4], %l7			! Load
syscall_is_too_hard:
	mov		%i2, %o2				! IEU0	Group
	ldx		[%curptr + AOFF_task_flags], %l5	! Load

	st		%g0, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_FPRS]
	mov		%i3, %o3				! IEU1
	mov		%i4, %o4				! IEU0	Group
	andcc		%l5, 0x20, %g0				! IEU1	2 bubbles
	bne,pn		%icc, linux_syscall_trace		! CTI	Group
	 mov		%i0, %l5				! IEU0
2:	call		%l7					! CTI	Group brk forced
	 mov		%i5, %o5				! IEU0
	stx		%o0, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I0]

ret_sys_call:
	ldx		[%curptr + AOFF_task_flags], %l6
	sra		%o0, 0, %o0
	mov		%ulo(TSTATE_XCARRY | TSTATE_ICARRY), %g2
	ldx		[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_TSTATE], %g3
	cmp		%o0, -ENOIOCTLCMD
	sllx		%g2, 32, %g2
	bgeu,pn		%xcc, 1f
	 andcc		%l6, 0x20, %l6	

	/* System call success, clear Carry condition code. */
	andn		%g3, %g2, %g3
	stx		%g3, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_TSTATE]	
	bne,pn		%icc, linux_syscall_trace2
	 ldx		[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_TNPC], %l1 ! pc = npc
	add		%l1, 0x4, %l2				         !npc = npc+4
	stx		%l1, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_TPC]
	b,pt		%xcc, rtrap_clr_l6
	 stx		%l2, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_TNPC]
1:
	/* System call failure, set Carry condition code.
	 * Also, get abs(errno) to return to the process.
	 */
	sub		%g0, %o0, %o0
	or		%g3, %g2, %g3
	stx		%o0, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I0]
	mov		1, %l6
	stx		%g3, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_TSTATE]
	bne,pn		%icc, linux_syscall_trace2
	 ldx		[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_TNPC], %l1 ! pc = npc
	add		%l1, 0x4, %l2				         !npc = npc+4

	stx		%l1, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_TPC]
	b,pt		%xcc, rtrap
	 stx		%l2, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_TNPC]
linux_syscall_trace2:
	call		syscall_trace
	 add		%l1, 0x4, %l2			/* npc = npc+4 */
	stx		%l1, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_TPC]
	ba,pt		%xcc, rtrap
	 stx		%l2, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_TNPC]

	.align		32
	.globl		__flushw_user
__flushw_user:
1:	save		%sp, -128, %sp
	rdpr		%otherwin, %g1
	brnz,pt		%g1, 1b
	 add		%g2, 1, %g2
1:	sub		%g2, 1, %g2
	brnz,pt		%g2, 1b
	 restore	%g0, %g0, %g0
2:	retl
	 mov		%g3, %o7
