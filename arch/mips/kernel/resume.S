/*
 *  linux/kernel/mips/sys_call.S
 *
 *  Copyright (C) 1994, 1995 Waldorf Electronics
 *  written by Ralf Baechle
 */

/*
 * sys_call.S  contains the system-call and fault low-level handling routines.
 * This also contains the timer-interrupt handler, as well as all interrupts
 * and faults that can result in a task-switch.
 */

#include <asm/regdef.h>
#include <asm/processor.h>
#include <asm/mipsregs.h>
#include <asm/mipsconfig.h>

/*
 * These are offsets into the task-struct.
 */
state		=  0
counter		=  4
priority	=  8
signal		= 12
blocked		= 16
flags		= 20
errno		= 24			#/* MIPS OK */
exec_domain	= 60			#/* ??? */

ENOSYS		= 38

/*
 * Code necessary to switch tasks on an Linux/MIPS machine.
 */
MODE_ALIAS	=	0x0016			# uncachable

		.text
		.set	noreorder
		.set	at

		.globl	_resume
_resume:
		/*
		 * current task's task_struct
		 */
		lui	t5,%hi(_current)
		lw	t0,%lo(_current)(t5)

		/*
		 * Save status register
		 */
		mfc0	t1,CP0_STATUS
		addu	t0,a1			# Add tss offset
		sw	t1,TOFF_CP0_STATUS(t0)

		/*
		 * Disable interrupts
		 */
		ori	t2,t1,0x1f
		xori	t2,0x1e
		mtc0	t2,CP0_STATUS

		/*
		 * Save non-scratch registers
		 * All other registers have been saved on the kernel stack
		 */
		sw	s0,TOFF_REG16(t0)
		sw	s1,TOFF_REG17(t0)
		sw	s2,TOFF_REG18(t0)
		sw	s3,TOFF_REG19(t0)
		sw	s4,TOFF_REG20(t0)
		sw	s5,TOFF_REG21(t0)
		sw	s6,TOFF_REG22(t0)
		sw	s7,TOFF_REG23(t0)
		sw	gp,TOFF_REG28(t0)
		sw	sp,TOFF_REG29(t0)
		sw	fp,TOFF_REG30(t0)
		sw	ra,TOFF_REG31(t0)

		/*
		 * Save floating point state
		 */
		srl	t2,t1,29
		andi	t2,1
		beqz	t2,2f
		srl	t2,t1,26
		andi	t2,1
		beqz	t2,1f
		sdc1	$f0,(TOFF_FPU+0)(t0)	# delay slot
		/*
		 * Store the 16 odd double precision registers
		 */
		sdc1	$f1,(TOFF_FPU+8)(t0)
		sdc1	$f3,(TOFF_FPU+24)(t0)
		sdc1	$f5,(TOFF_FPU+40)(t0)
		sdc1	$f7,(TOFF_FPU+56)(t0)
		sdc1	$f9,(TOFF_FPU+72)(t0)
		sdc1	$f11,(TOFF_FPU+88)(t0)
		sdc1	$f13,(TOFF_FPU+104)(t0)
		sdc1	$f15,(TOFF_FPU+120)(t0)
		sdc1	$f17,(TOFF_FPU+136)(t0)
		sdc1	$f19,(TOFF_FPU+152)(t0)
		sdc1	$f21,(TOFF_FPU+168)(t0)
		sdc1	$f23,(TOFF_FPU+184)(t0)
		sdc1	$f25,(TOFF_FPU+200)(t0)
		sdc1	$f27,(TOFF_FPU+216)(t0)
		sdc1	$f29,(TOFF_FPU+232)(t0)
		sdc1	$f31,(TOFF_FPU+248)(t0)

		/*
		 * Store the 16 even double precision registers
		 */
1:		cfc1	t1,$31
		sdc1	$f2,(TOFF_FPU+16)(t0)
		sdc1	$f4,(TOFF_FPU+32)(t0)
		sdc1	$f6,(TOFF_FPU+48)(t0)
		sdc1	$f8,(TOFF_FPU+64)(t0)
		sdc1	$f10,(TOFF_FPU+80)(t0)
		sdc1	$f12,(TOFF_FPU+96)(t0)
		sdc1	$f14,(TOFF_FPU+112)(t0)
		sdc1	$f16,(TOFF_FPU+128)(t0)
		sdc1	$f18,(TOFF_FPU+144)(t0)
		sdc1	$f20,(TOFF_FPU+160)(t0)
		sdc1	$f22,(TOFF_FPU+176)(t0)
		sdc1	$f24,(TOFF_FPU+192)(t0)
		sdc1	$f26,(TOFF_FPU+208)(t0)
		sdc1	$f28,(TOFF_FPU+224)(t0)
		sdc1	$f30,(TOFF_FPU+240)(t0)
		sw	t1,(TOFF_FPU+256)(t0)

		/*
		 * Switch current task
		 */
2:		sw	a0,%lo(_current)(t5)
		addu	a0,a1			# Add tss offset

		/*
		 * Switch address space
		 */

		/*
		 * (Choose new ASID for process)
		 */

		/*
		 * Switch the root pointer
		 */
		lw	t0,TOFF_PG_DIR(a0)
		la	t1,TLB_ROOT
		mtc0	t1,CP0_ENTRYHI
		mtc0	zero,CP0_INDEX
		srl	t0,6
		ori	t0,MODE_ALIAS
		mtc0	t0,CP0_ENTRYLO0
		mtc0	zero,CP0_ENTRYLO1
		tlbwi

		/*
		 * Flush tlb (probably not needed)
		 * (Doesn't clobber a0-a3)
		 */
		jal	_tlbflush
		nop 					# delay slot

		lw	a2,TOFF_CP0_STATUS(a0)

		/*
		 * Restore fpu state:
		 *  - cp0 status register bits
		 *  - fp gp registers
		 *  - cp1 status/control register
		 */
		ori	t1,a2,1				# pipeline magic
		xori	t1,1
		mtc0	t1,CP0_STATUS
		srl	t0,a2,29
		andi	t0,1
		beqz	t0,2f
		srl	t0,a2,26
		andi	t0,1
		beqz	t0,1f
		ldc1	$f0,(TOFF_FPU+0)(a0)	# delay slot
		/*
		 * Restore the 16 odd double precision registers only
		 * when enabled in the cp0 status register.
		 */
		ldc1	$f1,(TOFF_FPU+8)(a0)
		ldc1	$f3,(TOFF_FPU+24)(a0)
		ldc1	$f5,(TOFF_FPU+40)(a0)
		ldc1	$f7,(TOFF_FPU+56)(a0)
		ldc1	$f9,(TOFF_FPU+72)(a0)
		ldc1	$f11,(TOFF_FPU+88)(a0)
		ldc1	$f13,(TOFF_FPU+104)(a0)
		ldc1	$f15,(TOFF_FPU+120)(a0)
		ldc1	$f17,(TOFF_FPU+136)(a0)
		ldc1	$f19,(TOFF_FPU+152)(a0)
		ldc1	$f21,(TOFF_FPU+168)(a0)
		ldc1	$f23,(TOFF_FPU+184)(a0)
		ldc1	$f25,(TOFF_FPU+200)(a0)
		ldc1	$f27,(TOFF_FPU+216)(a0)
		ldc1	$f29,(TOFF_FPU+232)(a0)
		ldc1	$f31,(TOFF_FPU+248)(a0)

		/*
		 * Restore the 16 even double precision registers always
		 * when cp1 was enabled in the cp0 status register.
		 */
1:		lw	t0,(TOFF_FPU+256)(a0)
		ldc1	$f2,(TOFF_FPU+16)(a0)
		ldc1	$f4,(TOFF_FPU+32)(a0)
		ldc1	$f6,(TOFF_FPU+48)(a0)
		ldc1	$f8,(TOFF_FPU+64)(a0)
		ldc1	$f10,(TOFF_FPU+80)(a0)
		ldc1	$f12,(TOFF_FPU+96)(a0)
		ldc1	$f14,(TOFF_FPU+112)(a0)
		ldc1	$f16,(TOFF_FPU+128)(a0)
		ldc1	$f18,(TOFF_FPU+144)(a0)
		ldc1	$f20,(TOFF_FPU+160)(a0)
		ldc1	$f22,(TOFF_FPU+176)(a0)
		ldc1	$f24,(TOFF_FPU+192)(a0)
		ldc1	$f26,(TOFF_FPU+208)(a0)
		ldc1	$f28,(TOFF_FPU+224)(a0)
		ldc1	$f30,(TOFF_FPU+240)(a0)
		ctc1	t0,$31

		/*
		 * Restore non-scratch registers
		 */
2:		lw	s0,TOFF_REG16(a0)
		lw	s1,TOFF_REG17(a0)
		lw	s2,TOFF_REG18(a0)
		lw	s3,TOFF_REG19(a0)
		lw	s4,TOFF_REG20(a0)
		lw	s5,TOFF_REG21(a0)
		lw	s6,TOFF_REG22(a0)
		lw	s7,TOFF_REG23(a0)
		lw	gp,TOFF_REG28(a0)
		lw	sp,TOFF_REG29(a0)
		lw	fp,TOFF_REG30(a0)
		lw	ra,TOFF_REG31(a0)

		/*
		 * Restore status register
		 */
		lw	t0,TOFF_KSP(a0)
		sw	t0,_kernelsp

		jr	ra
		mtc0	a2,CP0_STATUS			# delay slot
