/*
 *  include/asm-mips/stackframe.h
 *
 *  Copyright (C) 1994, 1995 Waldorf Electronics
 *  written by Ralf Baechle
 */

#ifndef __ASM_MIPS_STACKFRAME_H
#define __ASM_MIPS_STACKFRAME_H

/*
 * Stack layout for all exceptions:
 *
 * ptrace needs to have all regs on the stack.
 * if the order here is changed, it needs to be 
 * updated in asm/mips/fork.c:copy_process, asm/mips/signal.c:do_signal,
 * asm-mips/ptrace.c, include/asm-mips/ptrace.h
 * and asm-mips/ptrace
 */

/*
 * Offsets into the Interrupt stackframe.
 * The first 20 bytes are reserved for the usual MIPS calling sequence
 */
#define FR_REG1		20
#define FR_REG2		((FR_REG1) + 4)
#define FR_REG3		((FR_REG2) + 4)
#define FR_REG4		((FR_REG3) + 4)
#define FR_REG5		((FR_REG4) + 4)
#define FR_REG6		((FR_REG5) + 4)
#define FR_REG7		((FR_REG6) + 4)
#define FR_REG8		((FR_REG7) + 4)
#define FR_REG9		((FR_REG8) + 4)
#define FR_REG10	((FR_REG9) + 4)
#define FR_REG11	((FR_REG10) + 4)
#define FR_REG12	((FR_REG11) + 4)
#define FR_REG13	((FR_REG12) + 4)
#define FR_REG14	((FR_REG13) + 4)
#define FR_REG15	((FR_REG14) + 4)
#define FR_REG16	((FR_REG15) + 4)
#define FR_REG17	((FR_REG16) + 4)
#define FR_REG18	((FR_REG17) + 4)
#define FR_REG19	((FR_REG18) + 4)
#define FR_REG20	((FR_REG19) + 4)
#define FR_REG21	((FR_REG20) + 4)
#define FR_REG22	((FR_REG21) + 4)
#define FR_REG23	((FR_REG22) + 4)
#define FR_REG24	((FR_REG23) + 4)
#define FR_REG25	((FR_REG24) + 4)

/*
 * $26 (k0) and $27 (k1) not saved
 */
#define FR_REG28	((FR_REG25) + 4)
#define FR_REG29	((FR_REG28) + 4)
#define FR_REG30	((FR_REG29) + 4)
#define FR_REG31	((FR_REG30) + 4)

/*
 * Saved special registers
 */
#define FR_LO		((FR_REG31) + 4)
#define FR_HI		((FR_LO) + 4)

/*
 * Saved cp0 registers follow
 */
#define FR_STATUS	((FR_HI) + 4)
#define FR_EPC		((FR_STATUS) + 4)
#define FR_CAUSE	((FR_EPC) + 4)

/*
 * Some goodies...
 */
#define FR_INTERRUPT	((FR_CAUSE) + 4)
#define FR_ORIG_REG2	((FR_INTERRUPT) + 4)

/*
 * Size of stack frame
 */
#define FR_SIZE		((FR_ORIG_REG2) + 4)

#define SAVE_ALL                                \
		mfc0	k0,CP0_STATUS;          \
		andi	k0,0x18;                /* extract KSU bits */ \
		beqz	k0,1f;                  \
		move	k1,sp;                  \
		/*                              \
		 * Called from user mode, new stack \
		 */                             \
		lui	k1,%hi(_kernelsp);      \
		lw	k1,%lo(_kernelsp)(k1);  \
1:		move	k0,sp;                  \
		subu	sp,k1,FR_SIZE;          \
		sw	k0,FR_REG29(sp);        \
		sw	$2,FR_REG2(sp);         \
		sw	$2,FR_ORIG_REG2(sp);    \
		mfc0	v0,CP0_STATUS;          \
		sw	v0,FR_STATUS(sp);       \
		mfc0	v0,CP0_CAUSE;           \
		sw	v0,FR_CAUSE(sp);        \
		mfc0	v0,CP0_EPC;             \
		sw	v0,FR_EPC(sp);          \
		mfhi	v0;                     \
		sw	v0,FR_HI(sp);           \
		mflo	v0;                     \
		sw	v0,FR_LO(sp);           \
		sw	$1,FR_REG1(sp);         \
		sw	$3,FR_REG3(sp);         \
		sw	$4,FR_REG4(sp);         \
		sw	$5,FR_REG5(sp);         \
		sw	$6,FR_REG6(sp);         \
		sw	$7,FR_REG7(sp);         \
		sw	$8,FR_REG8(sp);         \
		sw	$9,FR_REG9(sp);         \
		sw	$10,FR_REG10(sp);       \
		sw	$11,FR_REG11(sp);       \
		sw	$12,FR_REG12(sp);       \
		sw	$13,FR_REG13(sp);       \
		sw	$14,FR_REG14(sp);       \
		sw	$15,FR_REG15(sp);       \
		sw	$16,FR_REG16(sp);       \
		sw	$17,FR_REG17(sp);       \
		sw	$18,FR_REG18(sp);       \
		sw	$19,FR_REG19(sp);       \
		sw	$20,FR_REG20(sp);       \
		sw	$21,FR_REG21(sp);       \
		sw	$22,FR_REG22(sp);       \
		sw	$23,FR_REG23(sp);       \
		sw	$24,FR_REG24(sp);       \
		sw	$25,FR_REG25(sp);       \
		sw	$28,FR_REG28(sp);       \
		sw	$30,FR_REG30(sp);       \
		sw	$31,FR_REG31(sp)

#define RESTORE_ALL                             \
		lw	v1,FR_EPC(sp);          \
		lw	v0,FR_HI(sp);           \
		mtc0	v1,CP0_EPC;             \
		lw	v1,FR_LO(sp);           \
		mthi	v0;                     \
		lw	v0,FR_STATUS(sp);       \
		mtlo	v1;                     \
		mtc0	v0,CP0_STATUS;          \
		lw	$31,FR_REG31(sp);       \
		lw	$30,FR_REG30(sp);       \
		lw	$28,FR_REG28(sp);       \
		lw	$25,FR_REG25(sp);       \
		lw	$24,FR_REG24(sp);       \
		lw	$23,FR_REG23(sp);       \
		lw	$22,FR_REG22(sp);       \
		lw	$21,FR_REG21(sp);       \
		lw	$20,FR_REG20(sp);       \
		lw	$19,FR_REG19(sp);       \
		lw	$18,FR_REG18(sp);       \
		lw	$17,FR_REG17(sp);       \
		lw	$16,FR_REG16(sp);       \
		lw	$15,FR_REG15(sp);       \
		lw	$14,FR_REG14(sp);       \
		lw	$13,FR_REG13(sp);       \
		lw	$12,FR_REG12(sp);       \
		lw	$11,FR_REG11(sp);       \
		lw	$10,FR_REG10(sp);       \
		lw	$9,FR_REG9(sp);         \
		lw	$8,FR_REG8(sp);         \
		lw	$7,FR_REG7(sp);         \
		lw	$6,FR_REG6(sp);         \
		lw	$5,FR_REG5(sp);         \
		lw	$4,FR_REG4(sp);         \
		lw	$3,FR_REG3(sp);         \
		lw	$2,FR_REG2(sp);         \
		lw	$1,FR_REG1(sp);         \
		lw	sp,FR_REG29(sp); /* Deallocate stack */ \
		eret

/*
 * Move to kernel mode and disable interrupts
 */
#define CLI                                     \
		mfc0	k0,CP0_STATUS;          \
		ori	k0,k0,0x1f;             \
		xori	k0,k0,0x1f;             \
		mtc0	k0,CP0_STATUS

/*
 * Move to kernel mode and enable interrupts
 */
#define STI                                     \
		mfc0	k0,CP0_STATUS;          \
		ori	k0,k0,0x1f;             \
		xori	k0,k0,0x1e;             \
		mtc0	k0,CP0_STATUS

#endif /* __ASM_MIPS_STACKFRAME_H */
