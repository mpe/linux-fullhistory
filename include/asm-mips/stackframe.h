/*
 *  include/asm-mips/stackframe.h
 *
 *  Copyright (C) 1994, 1995, 1996 by Ralf Baechle and Paul M. Antoine.
 */
#ifndef __ASM_MIPS_STACKFRAME_H
#define __ASM_MIPS_STACKFRAME_H

#include <asm/asm.h>
#include <asm/offset.h>

#define SAVE_ALL                                         \
		mfc0	k0, CP0_STATUS;                  \
		sll	k0, 3;     /* extract cu0 bit */ \
		bltz	k0, 8f;                          \
		 move	k1, sp;                          \
		/* Called from user mode, new stack. */  \
		lui	k1, %hi(kernelsp);               \
		lw	k1, %lo(kernelsp)(k1);           \
8:                                                       \
		move	k0, sp;                          \
		subu	sp, k1, PT_SIZE;                 \
		sw	k0, PT_R29(sp);                  \
		sw	$2, PT_R2(sp);                   \
		sw	$1, PT_R1(sp);                   \
		sw	$2, PT_OR2(sp);                  \
		sw	$0, PT_R0(sp);			 \
		mfc0	v0, CP0_STATUS;                  \
		sw	$3, PT_R3(sp);                   \
		sw	v0, PT_STATUS(sp);               \
		sw	$4, PT_R4(sp);                   \
		mfc0	v0, CP0_CAUSE;                   \
		sw	$5, PT_R5(sp);                   \
		sw	v0, PT_CAUSE(sp);                \
		sw	$6, PT_R6(sp);                   \
		mfc0	v0, CP0_EPC;                     \
		sw	$7, PT_R7(sp);                   \
		sw	v0, PT_EPC(sp);                  \
		sw	$7, PT_OR7(sp);                  \
		sw	$8, PT_R8(sp);                   \
		mfhi	v0;                              \
		sw	$9, PT_R9(sp);                   \
		sw	v0, PT_HI(sp);                   \
		sw	$10,PT_R10(sp);                  \
		mflo	v0;                              \
		sw	$11, PT_R11(sp);                 \
		sw	v0,  PT_LO(sp);                  \
		sw	$12, PT_R12(sp);                 \
		sw	$13, PT_R13(sp);                 \
		sw	$14, PT_R14(sp);                 \
		sw	$15, PT_R15(sp);                 \
		sw	$16, PT_R16(sp);                 \
		sw	$17, PT_R17(sp);                 \
		sw	$18, PT_R18(sp);                 \
		sw	$19, PT_R19(sp);                 \
		sw	$20, PT_R20(sp);                 \
		sw	$21, PT_R21(sp);                 \
		sw	$22, PT_R22(sp);                 \
		sw	$23, PT_R23(sp);                 \
		sw	$24, PT_R24(sp);                 \
		sw	$25, PT_R25(sp);                 \
		sw	$28, PT_R28(sp);                 \
		sw	$30, PT_R30(sp);                 \
		sw	$31, PT_R31(sp);

/*
 * Note that we restore the IE flags from stack. This means
 * that a modified IE mask will be nullified.
 */
#define RESTORE_ALL                                      \
		mfc0	t0, CP0_STATUS;                  \
		ori	t0, 0x1f;                        \
		xori	t0, 0x1f;                        \
		mtc0	t0, CP0_STATUS;                  \
		lw	v0, PT_STATUS(sp);               \
		lw	v1, PT_LO(sp);                   \
		mtc0	v0, CP0_STATUS;                  \
		mtlo	v1;                              \
		lw	v0, PT_HI(sp);                   \
		lw	v1, PT_EPC(sp);                  \
		mthi	v0;                              \
		mtc0	v1, CP0_EPC;                     \
		lw	$31, PT_R31(sp);                 \
		lw	$30, PT_R30(sp);                 \
		lw	$28, PT_R28(sp);                 \
		lw	$25, PT_R25(sp);                 \
		lw	$24, PT_R24(sp);                 \
		lw	$23, PT_R23(sp);                 \
		lw	$22, PT_R22(sp);                 \
		lw	$21, PT_R21(sp);                 \
		lw	$20, PT_R20(sp);                 \
		lw	$19, PT_R19(sp);                 \
		lw	$18, PT_R18(sp);                 \
		lw	$17, PT_R17(sp);                 \
		lw	$16, PT_R16(sp);                 \
		lw	$15, PT_R15(sp);                 \
		lw	$14, PT_R14(sp);                 \
		lw	$13, PT_R13(sp);                 \
		lw	$12, PT_R12(sp);                 \
		lw	$11, PT_R11(sp);                 \
		lw	$10, PT_R10(sp);                 \
		lw	$9,  PT_R9(sp);                  \
		lw	$8,  PT_R8(sp);                  \
		lw	$7,  PT_R7(sp);                  \
		lw	$6,  PT_R6(sp);                  \
		lw	$5,  PT_R5(sp);                  \
		lw	$4,  PT_R4(sp);                  \
		lw	$3,  PT_R3(sp);                  \
		lw	$2,  PT_R2(sp);                  \
		lw	$1,  PT_R1(sp);                  \
		lw	sp,  PT_R29(sp);

/*
 * Move to kernel mode and disable interrupts.
 * Set cp0 enable bit as sign that we're running on the kernel stack
 */
#define CLI                                             \
		mfc0	t0,CP0_STATUS;                  \
		li	t1,ST0_CU0|0x1f;                \
		or	t0,t1;                          \
		xori	t0,0x1f;                        \
		mtc0	t0,CP0_STATUS

/*
 * Move to kernel mode and enable interrupts.
 * Set cp0 enable bit as sign that we're running on the kernel stack
 *
 * Note that the mtc0 will be effective on R4000 pipeline stage 7. This
 * means that another three instructions will be executed with interrupts
 * disabled.  Arch/mips/mips3/r4xx0.S makes use of this fact.
 */
#define STI                                             \
		mfc0	t0,CP0_STATUS;                  \
		li	t1,ST0_CU0|0x1f;                \
		or	t0,t1;                          \
		xori	t0,0x1e;                        \
		mtc0	t0,CP0_STATUS

#endif /* __ASM_MIPS_STACKFRAME_H */
