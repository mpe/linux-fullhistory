/* $Id: entry.S,v 1.91 1998/10/07 01:27:08 davem Exp $
 * arch/sparc64/kernel/entry.S:  Sparc64 trap low-level entry points.
 *
 * Copyright (C) 1995,1997 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996 Eddie C. Dost        (ecd@skynet.be)
 * Copyright (C) 1996 Miguel de Icaza      (miguel@nuclecu.unam.mx)
 * Copyright (C) 1996,1998 Jakub Jelinek   (jj@sunsite.mff.cuni.cz)
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
#include <asm/processor.h>
#include <asm/visasm.h>

/* #define SYSCALL_TRACING */

#define curptr      g6

#define NR_SYSCALLS 256      /* Each OS is different... */

	.text
	.align		32

	/* This is trivial with the new code... */
	.align		32
	.globl		do_fpdis
do_fpdis:
	ldub		[%g6 + AOFF_task_tss + AOFF_thread_fpsaved], %g5	! Load	Group
	sethi		%hi(TSTATE_PEF), %g4					! IEU0
	wr		%g0, FPRS_FEF, %fprs					! LSU	Group+4bubbles
	andcc		%g5, FPRS_FEF, %g0					! IEU1	Group
	be,a,pt		%icc, 1f						! CTI
	 clr		%g7							! IEU0
	ldub		[%g6 + AOFF_task_tss + AOFF_thread_gsr], %g7		! Load	Group
1:	andcc		%g5, FPRS_DL, %g0					! IEU1
	bne,pn		%icc, 2f						! CTI
	 fzero		%f0							! FPA
	andcc		%g5, FPRS_DU, %g0					! IEU1  Group
	bne,pn		%icc, 1f						! CTI
	 fzero		%f2							! FPA
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
	faddd		%f0, %f2, %f48
	fmuld		%f0, %f2, %f50
	faddd		%f0, %f2, %f52
	fmuld		%f0, %f2, %f54
	faddd		%f0, %f2, %f56
	fmuld		%f0, %f2, %f58
	b,pt		%xcc, fpdis_exit2
	 faddd		%f0, %f2, %f60
1:	mov		SECONDARY_CONTEXT, %g3
	add		%g6, AOFF_task_fpregs + 0x80, %g1
	faddd		%f0, %f2, %f4
	fmuld		%f0, %f2, %f6
	ldxa		[%g3] ASI_DMMU, %g5
	add		%g6, AOFF_task_fpregs + 0xc0, %g2
	stxa		%g0, [%g3] ASI_DMMU
	faddd		%f0, %f2, %f8
	fmuld		%f0, %f2, %f10
	flush		%g6
	membar		#StoreLoad | #LoadLoad
	ldda		[%g1] ASI_BLK_S, %f32	! grrr, where is ASI_BLK_NUCLEUS 8-(
	ldda		[%g2] ASI_BLK_S, %f48
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
	b,pt		%xcc, fpdis_exit
	 membar		#Sync
2:	andcc		%g5, FPRS_DU, %g0
	bne,pt		%icc, 3f
	 fzero		%f32
	mov		SECONDARY_CONTEXT, %g3
	fzero		%f34
	ldxa		[%g3] ASI_DMMU, %g5
	add		%g6, AOFF_task_fpregs, %g1
	stxa		%g0, [%g3] ASI_DMMU
	add		%g6, AOFF_task_fpregs + 0x40, %g2
	faddd		%f32, %f34, %f36
	fmuld		%f32, %f34, %f38
	flush		%g6
	membar		#StoreLoad | #LoadLoad
	ldda		[%g1] ASI_BLK_S, %f0	! grrr, where is ASI_BLK_NUCLEUS 8-(
	ldda		[%g2] ASI_BLK_S, %f16
	faddd		%f32, %f34, %f40
	fmuld		%f32, %f34, %f42
	faddd		%f32, %f34, %f44
	fmuld		%f32, %f34, %f46
	faddd		%f32, %f34, %f48
	fmuld		%f32, %f34, %f50
	faddd		%f32, %f34, %f52
	fmuld		%f32, %f34, %f54
	faddd		%f32, %f34, %f56
	fmuld		%f32, %f34, %f58
	faddd		%f32, %f34, %f60
	fmuld		%f32, %f34, %f62
	b,pt		%xcc, fpdis_exit
	 membar		#Sync
3:	mov		SECONDARY_CONTEXT, %g3
	add		%g6, AOFF_task_fpregs, %g1
	ldxa		[%g3] ASI_DMMU, %g5
	mov		0x40, %g2
	stxa		%g0, [%g3] ASI_DMMU
	flush		%g6
	membar		#StoreLoad | #LoadLoad
	ldda		[%g1] ASI_BLK_S, %f0		! grrr, where is ASI_BLK_NUCLEUS 8-(
	ldda		[%g1 + %g2] ASI_BLK_S, %f16
	add		%g1, 0x80, %g1
	ldda		[%g1] ASI_BLK_S, %f32
	ldda		[%g1 + %g2] ASI_BLK_S, %f48
	membar		#Sync
fpdis_exit:
	stxa		%g5, [%g3] ASI_DMMU
	flush		%g6
fpdis_exit2:
	wr		%g7, 0, %gsr
	ldx		[%g6 + AOFF_task_tss + AOFF_thread_xfsr], %fsr
	rdpr		%tstate, %g3
	or		%g3, %g4, %g3		! anal...
	wrpr		%g3, %tstate
	wr		%g0, FPRS_FEF, %fprs	! clean DU/DL bits
	retry

	/* The registers for cross calls will be:
	 *
	 * DATA 0: [low 32-bits]  Address of function to call, jmp to this
	 *         [high 32-bits] MMU Context Argument 0, place in %g5
	 * DATA 1: Address Argument 1, place in %g6
	 * DATA 2: Address Argument 2, place in %g7
	 *
	 * With this method we can do most of the cross-call tlb/cache
	 * flushing very quickly.
	 */
	.data
	.align  8
	.globl	ivec_spurious_cookie
ivec_spurious_cookie:	.xword	0

	.text
	.align	32
	.globl	do_ivec
do_ivec:
	ldxa		[%g0] ASI_INTR_RECEIVE, %g5
	andcc		%g5, 0x20, %g0
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
	 sethi		%hi(0x80000000), %g5

	or		%g2, %g5, %g2
	stx		%g2, [%g1 + %g3]

	/* No branches, worse case we don't know about this interrupt
	 * yet, so we would just write a zero into the softint register
	 * which is completely harmless.
	 */
	wr		%g2, 0x0, %set_softint
do_ivec_return:
	stxa		%g0, [%g0] ASI_INTR_RECEIVE
	membar		#Sync
	retry
do_ivec_xcall:
	srlx		%g3, 32, %g5
	add		%g2, 0x10, %g2
	srl		%g3, 0, %g3
	ldxa		[%g2] ASI_UDB_INTR_R, %g6
	add		%g2, 0x10, %g2
	ldxa		[%g2] ASI_UDB_INTR_R, %g7
	stxa		%g0, [%g0] ASI_INTR_RECEIVE
	membar		#Sync
	jmpl		%g3, %g0
	 nop

do_ivec_spurious:
	srl		%g3, 3, %g3
	sethi		%hi(ivec_spurious_cookie), %g2
	stx		%g3, [%g2 + %lo(ivec_spurious_cookie)]
	stxa		%g0, [%g0] ASI_INTR_RECEIVE
	membar		#Sync
	rdpr		%pstate, %g5
	wrpr		%g5, PSTATE_IG | PSTATE_AG, %pstate
	sethi		%hi(109f), %g7
	ba,pt		%xcc, etrap
109:	 or		%g7, %lo(109b), %g7
	call		report_spurious_ivec
	 add		%sp, STACK_BIAS + REGWIN_SZ, %o0
	ba,pt		%xcc, rtrap
	 clr		%l6

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

	.globl		utrap, utrap_ill
utrap:	brz,pn		%g1, etrap
	 nop
	save		%sp, -128, %sp
	rdpr		%tstate, %l6
	rdpr		%cwp, %l7
	andn		%l6, TSTATE_CWP, %l6
	wrpr		%l6, %l7, %tstate
	rdpr		%tpc, %l6
	rdpr		%tnpc, %l7
	wrpr		%g1, 0, %tnpc
	done
utrap_ill:
        call		bad_trap
	 add		%sp, STACK_BIAS + REGWIN_SZ, %o0
	ba,pt		%xcc, rtrap
	 clr		%l6

#ifdef CONFIG_BLK_DEV_FD
	.globl		floppy_hardint
floppy_hardint:
	wr		%g0, (1 << 11), %clear_softint
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
	ldx		[%g3 + 0x10], %g4		! action->mask == ino_bucket ptr
	ldx		[%g4 + 0x18], %g4		! bucket->iclr
	stw		%g0, [%g4]			! SYSIO_ICLR_IDLE
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
	sethi		%hi(109f), %g7
	b,pt		%xcc, etrap_irq
109:	 or		%g7, %lo(109b), %g7

	mov		11, %o0
	mov		0, %o1
	call		sparc_floppy_irq
	 add		%sp, STACK_BIAS + REGWIN_SZ, %o2

	b,pt		%xcc, rtrap
	 clr		%l6

#endif /* CONFIG_BLK_DEV_FD */

	/* XXX Here is stuff we still need to write... -DaveM XXX */
	.globl		netbsd_syscall
netbsd_syscall:
	retl
	 nop

	/* These next few routines must be sure to clear the
	 * SFSR FaultValid bit so that the fast tlb data protection
	 * handler does not flush the wrong context and lock up the
	 * box.
	 */
	.globl		__do_data_access_exception
	.globl		__do_data_access_exception_tl1
__do_data_access_exception_tl1:
	rdpr		%pstate, %g4
	wrpr		%g4, PSTATE_MG|PSTATE_AG, %pstate
	rdpr		%tl, %g3
	cmp		%g3, 1
	mov		TLB_SFSR, %g3
	mov		DMMU_SFAR, %g5
	ldxa		[%g3] ASI_DMMU, %g4	! Get SFSR
	ldxa		[%g5] ASI_DMMU, %g5	! Get SFAR
	stxa		%g0, [%g3] ASI_DMMU	! Clear SFSR.FaultValid bit
	membar		#Sync
	bgu,pn		%icc, winfix_dax
	 rdpr		%tpc, %g3
	sethi		%hi(109f), %g7
	ba,pt		%xcc, etraptl1
	 or		%g7, %lo(109f), %g7	! Merge in below
__do_data_access_exception:
	rdpr		%pstate, %g4
	wrpr		%g4, PSTATE_MG|PSTATE_AG, %pstate
	mov		TLB_SFSR, %g3
	mov		DMMU_SFAR, %g5
	ldxa		[%g3] ASI_DMMU, %g4	! Get SFSR
	ldxa		[%g5] ASI_DMMU, %g5	! Get SFAR
	stxa		%g0, [%g3] ASI_DMMU	! Clear SFSR.FaultValid bit
	membar		#Sync
	sethi		%hi(109f), %g7
	ba,pt		%xcc, etrap
109:	 or		%g7, %lo(109b), %g7
	mov		%l4, %o1
	mov		%l5, %o2
	call		data_access_exception
	 add		%sp, STACK_BIAS + REGWIN_SZ, %o0
	ba,pt		%xcc, rtrap
	 clr		%l6

	.globl		__do_instruction_access_exception
	.globl		__do_instruction_access_exception_tl1
__do_instruction_access_exception_tl1:
	rdpr		%pstate, %g4
	wrpr		%g4, PSTATE_MG|PSTATE_AG, %pstate
	mov		TLB_SFSR, %g3
	mov		DMMU_SFAR, %g5
	ldxa		[%g3] ASI_DMMU, %g4	! Get SFSR
	ldxa		[%g5] ASI_DMMU, %g5	! Get SFAR
	stxa		%g0, [%g3] ASI_IMMU	! Clear FaultValid bit
	membar		#Sync
	sethi		%hi(109f), %g7
	ba,pt		%xcc, etraptl1
	 or		%g7, %lo(109f), %g7	! Merge in below
__do_instruction_access_exception:
	rdpr		%pstate, %g4
	wrpr		%g4, PSTATE_MG|PSTATE_AG, %pstate
	mov		TLB_SFSR, %g3
	mov		DMMU_SFAR, %g5
	ldxa		[%g3] ASI_DMMU, %g4	! Get SFSR
	ldxa		[%g5] ASI_DMMU, %g5	! Get SFAR
	stxa		%g0, [%g3] ASI_IMMU	! Clear FaultValid bit
	membar		#Sync
	sethi		%hi(109f), %g7
	ba,pt		%xcc, etrap
109:	 or		%g7, %lo(109b), %g7
	mov		%l4, %o1
	mov		%l5, %o2
	call		instruction_access_exception
	 add		%sp, STACK_BIAS + REGWIN_SZ, %o0
	ba,pt		%xcc, rtrap
	 clr		%l6

	.globl		__do_privact
__do_privact:
	mov		TLB_SFSR, %g3
	stxa		%g0, [%g3] ASI_DMMU	! Clear FaultValid bit
	membar		#Sync
	sethi		%hi(109f), %g7
	ba,pt		%xcc, etrap
109:	or		%g7, %lo(109b), %g7
	call		do_privact
	 add		%sp, STACK_BIAS + REGWIN_SZ, %o0
	ba,pt		%xcc, rtrap
	 clr		%l6

	.globl		do_mna
do_mna:
	rdpr		%tl, %g3
	cmp		%g3, 1

	/* Setup %g4/%g5 now as they are used in the
	 * winfixup code.
	 */
	mov		TLB_SFSR, %g3
	mov		DMMU_SFAR, %g4
	ldxa		[%g4] ASI_DMMU, %g4
	ldxa		[%g3] ASI_DMMU, %g5
	stxa		%g0, [%g3] ASI_DMMU	! Clear FaultValid bit
	membar		#Sync
	bgu,pn		%icc, winfix_dax
	 rdpr		%tpc, %g3

1:	sethi		%hi(109f), %g7
	ba,pt		%xcc, etrap
109:	 or		%g7, %lo(109b), %g7
	mov		%l4, %o1
	mov		%l5, %o2
	call		mem_address_unaligned
	 add		%sp, STACK_BIAS + REGWIN_SZ, %o0
	ba,pt		%xcc, rtrap
	 clr		%l6

	.globl		do_lddfmna
do_lddfmna:
	sethi		%hi(109f), %g7
	mov		TLB_SFSR, %g4
	ldxa		[%g4] ASI_DMMU, %g5
	stxa		%g0, [%g4] ASI_DMMU	! Clear FaultValid bit
	membar		#Sync
	mov		DMMU_SFAR, %g4
	ldxa		[%g4] ASI_DMMU, %g4
	ba,pt		%xcc, etrap
109:	 or		%g7, %lo(109b), %g7
	mov		%l4, %o1
	mov		%l5, %o2
	call		handle_lddfmna
	 add		%sp, STACK_BIAS + REGWIN_SZ, %o0
	ba,pt		%xcc, rtrap
	 clr		%l6

	.globl		do_stdfmna
do_stdfmna:
	sethi		%hi(109f), %g7
	mov		TLB_SFSR, %g4
	ldxa		[%g4] ASI_DMMU, %g5
	stxa		%g0, [%g4] ASI_DMMU	! Clear FaultValid bit
	membar		#Sync
	mov		DMMU_SFAR, %g4
	ldxa		[%g4] ASI_DMMU, %g4
	ba,pt		%xcc, etrap
109:	 or		%g7, %lo(109b), %g7
	mov		%l4, %o1
	mov		%l5, %o2
	call		handle_stdfmna
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
	 sll		%o0, 0x2, %o0
	sethi		%hi(sunos_nosys), %l6
	b,pt		%xcc, 2f
	 or		%l6, %lo(sunos_nosys), %l6
1:	sethi		%hi(sunos_sys_table), %l7
	or		%l7, %lo(sunos_sys_table), %l7
	lduw		[%l7 + %o0], %l6
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
	.globl	sys_sigsuspend, sys_rt_sigsuspend, sys32_rt_sigsuspend
	.globl	sys_sigreturn, sys_rt_sigreturn
	.globl	sys32_sigreturn, sys32_rt_sigreturn
	.globl	sys32_execve, sys_ptrace
	.globl	sys_sigaltstack, sys32_sigaltstack
	.globl	sys32_sigstack
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
sys_memory_ordering:
		sethi		%hi(sparc_memory_ordering), %g1
		add		%sp, STACK_BIAS + REGWIN_SZ, %o1
		jmpl		%g1 + %lo(sparc_memory_ordering), %g0
		 nop
sys_sigaltstack:sethi		%hi(do_sigaltstack), %g1
		add		%i6, STACK_BIAS, %o2
		jmpl		%g1 + %lo(do_sigaltstack), %g1
		 nop
sys32_sigstack:	sethi		%hi(do_sys32_sigstack), %g1
		mov		%i6, %o2
		jmpl		%g1 + %lo(do_sys32_sigstack), %g1
		 nop
sys32_sigaltstack:
		sethi		%hi(do_sys32_sigaltstack), %g1
		mov		%i6, %o2
		jmpl		%g1 + %lo(do_sys32_sigaltstack), %g1
		 nop

		.align		32
sys_sigsuspend:	add		%sp, STACK_BIAS + REGWIN_SZ, %o0
		call		do_sigsuspend
		 add		%o7, 1f-.-4, %o7
		nop
sys_rt_sigsuspend: /* NOTE: %o0,%o1 have a correct value already */
		add		%sp, STACK_BIAS + REGWIN_SZ, %o2
		call		do_rt_sigsuspend
		 add		%o7, 1f-.-4, %o7
		nop
sys32_rt_sigsuspend: /* NOTE: %o0,%o1 have a correct value already */
		srl		%o0, 0, %o0
		add		%sp, STACK_BIAS + REGWIN_SZ, %o2
		call		do_rt_sigsuspend32
		 add		%o7, 1f-.-4, %o7
		/* NOTE: %o0 has a correct value already */
sys_sigpause:	add		%sp, STACK_BIAS + REGWIN_SZ, %o1
		call		do_sigpause
		 add		%o7, 1f-.-4, %o7
		nop
sys_sigreturn:	add		%sp, STACK_BIAS + REGWIN_SZ, %o0
		call		do_sigreturn
		 add		%o7, 1f-.-4, %o7
		nop
sys32_sigreturn:
		add		%sp, STACK_BIAS + REGWIN_SZ, %o0
		call		do_sigreturn32
		 add		%o7, 1f-.-4, %o7
		nop
sys_rt_sigreturn:
		add		%sp, STACK_BIAS + REGWIN_SZ, %o0
		call		do_rt_sigreturn
		 add		%o7, 1f-.-4, %o7
		nop
sys32_rt_sigreturn:
		add		%sp, STACK_BIAS + REGWIN_SZ, %o0
		call		do_rt_sigreturn32
		 add		%o7, 1f-.-4, %o7
		nop
sys_ptrace:	add		%sp, STACK_BIAS + REGWIN_SZ, %o0
		call		do_ptrace
		 add		%o7, 1f-.-4, %o7
		nop
		.align		32
1:		ldx		[%curptr + AOFF_task_flags], %l5
		andcc		%l5, 0x20, %g0
		be,pt		%icc, rtrap
		 clr		%l6
		call		syscall_trace
		 nop

		ba,pt		%xcc, rtrap
		 clr		%l6

	/* This is how fork() was meant to be done, 8 instruction entry.
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
	.globl	sys_fork, sys_vfork, sys_clone, sparc_exit
	.globl	ret_from_syscall
	.align	32
sys_fork:
sys_vfork:	mov		SIGCHLD, %o0
		clr		%o1
sys_clone:	flushw
		mov		%o7, %l5
		add		%sp, STACK_BIAS + REGWIN_SZ, %o2
		movrz		%o1, %fp, %o1
		call		do_fork
		 mov		%l5, %o7
ret_from_syscall:
		/* Clear SPARC_FLAG_NEWCHILD, switch_to leaves tss.flags in
		 * %o7 for us.  Check performance counter stuff too.
		 */
		andn		%o7, 0x100, %o7
		sth		%o7, [%g6 + AOFF_task_tss + AOFF_thread_flags]
#ifdef __SMP__
		sethi		%hi(scheduler_lock), %o4
		membar		#StoreStore | #LoadStore
		stb		%g0, [%o4 + %lo(scheduler_lock)]
#endif
		andcc		%o7, 0x200, %g0
		be,pt		%icc, 1f
		 nop
		ldx		[%g6 + AOFF_task_tss + AOFF_thread_pcr_reg], %o7
		wr		%g0, %o7, %pcr
		wr		%g0, %g0, %pic
1:		b,pt		%xcc, ret_sys_call
		 ldx		[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I0], %o0
sparc_exit:	rdpr		%otherwin, %g1
		rdpr		%pstate, %g2
		wrpr		%g2, PSTATE_IE, %pstate
		rdpr		%cansave, %g3
		add		%g3, %g1, %g3
		wrpr		%g3, 0x0, %cansave
		wrpr		%g0, 0x0, %otherwin
		wrpr		%g2, 0x0, %pstate
		mov		%o7, %l5
		sth		%g0, [%g6 + AOFF_task_tss + AOFF_thread_w_saved]
		call		sys_exit
		 mov		%l5, %o7

linux_sparc_ni_syscall:
	sethi		%hi(sys_ni_syscall), %l7
	b,pt		%xcc, 4f
	 or		%l7, %lo(sys_ni_syscall), %l7

linux_syscall_trace32:
	call		syscall_trace
	 nop
	srl		%i0, 0, %o0
	mov		%i4, %o4
	srl		%i1, 0, %o1
	srl		%i2, 0, %o2
	b,pt		%xcc, 2f
	 srl		%i3, 0, %o3

linux_syscall_trace:
	call		syscall_trace
	 nop
	mov		%i0, %o0
	mov		%i1, %o1
	mov		%i2, %o2
	mov		%i3, %o3
	b,pt		%xcc, 2f
	 mov		%i4, %o4


	/* Linux 32-bit and SunOS system calls enter here... */
	.align	32
	.globl	linux_sparc_syscall32
linux_sparc_syscall32:
	/* Direct access to user regs, must faster. */
	cmp		%g1, NR_SYSCALLS			! IEU1	Group
	bgeu,pn		%xcc, linux_sparc_ni_syscall		! CTI
	 srl		%i0, 0, %o0				! IEU0
	sll		%g1, 2, %l4				! IEU0	Group
#ifdef SYSCALL_TRACING
	add		%sp, STACK_BIAS + REGWIN_SZ, %o1
	call		syscall_trace_entry
	 mov		%g1, %o0
	srl		%i0, 0, %o0
#endif
	mov		%i4, %o4				! IEU1
	lduw		[%l7 + %l4], %l7			! Load
	srl		%i1, 0, %o1				! IEU0	Group
	ldx		[%curptr + AOFF_task_flags], %l0	! Load

	mov		%i5, %o5				! IEU1
	srl		%i2, 0, %o2				! IEU0	Group
	mov		%i0, %l5				! IEU1
	andcc		%l0, 0x20, %g0				! IEU1	Group
	bne,pn		%icc, linux_syscall_trace32		! CTI
	 srl		%i3, 0, %o3				! IEU0
	call		%l7					! CTI	Group brk forced
	 add		%o7, 3f-.-4, %o7			! IEU0

	/* Linux native and SunOS system calls enter here... */
	.align	32
	.globl	linux_sparc_syscall, ret_sys_call
linux_sparc_syscall:
	/* Direct access to user regs, must faster. */
	cmp		%g1, NR_SYSCALLS			! IEU1	Group
	bgeu,pn		%xcc, linux_sparc_ni_syscall		! CTI
	 mov		%i0, %o0				! IEU0
	sll		%g1, 2, %l4				! IEU0	Group
#ifdef SYSCALL_TRACING
	add		%sp, STACK_BIAS + REGWIN_SZ, %o1
	call		syscall_trace_entry
	 mov		%g1, %o0
	mov		%i0, %o0
#endif
	mov		%i1, %o1				! IEU1
	lduw		[%l7 + %l4], %l7			! Load
4:	mov		%i2, %o2				! IEU0	Group
	ldx		[%curptr + AOFF_task_flags], %l0	! Load

	mov		%i3, %o3				! IEU1
	mov		%i4, %o4				! IEU0	Group
	andcc		%l0, 0x20, %g0				! IEU1	Group+1 bubble
	bne,pn		%icc, linux_syscall_trace		! CTI	Group
	 mov		%i0, %l5				! IEU0
2:	call		%l7					! CTI	Group brk forced
	 mov		%i5, %o5				! IEU0
3:	stx		%o0, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I0]
ret_sys_call:
#ifdef SYSCALL_TRACING
	call		syscall_trace_exit
	 add		%sp, STACK_BIAS + REGWIN_SZ, %o1
#endif
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
