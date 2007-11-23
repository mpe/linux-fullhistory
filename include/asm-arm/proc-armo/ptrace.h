/*
 * linux/include/asm-arm/proc-armo/ptrace.h
 *
 * Copyright (C) 1996 Russell King
 */

#ifndef __ASM_PROC_PTRACE_H
#define __ASM_PROC_PTRACE_H

/* this struct defines the way the registers are stored on the
   stack during a system call. */

struct pt_regs {
	long uregs[17];
};

#define ARM_pc		uregs[15]
#define ARM_lr		uregs[14]
#define ARM_sp		uregs[13]
#define ARM_ip		uregs[12]
#define ARM_fp		uregs[11]
#define ARM_r10		uregs[10]
#define ARM_r9		uregs[9]
#define ARM_r8		uregs[8]
#define ARM_r7		uregs[7]
#define ARM_r6		uregs[6]
#define ARM_r5		uregs[5]
#define ARM_r4		uregs[4]
#define ARM_r3		uregs[3]
#define ARM_r2		uregs[2]
#define ARM_r1		uregs[1]
#define ARM_r0		uregs[0]
#define ARM_ORIG_r0	uregs[16] /* -1 */

#define USR26_MODE	0x00
#define FIQ26_MODE	0x01
#define IRQ26_MODE	0x02
#define SVC26_MODE	0x03
#define MODE_MASK	0x03
#define F_BIT		(1 << 26)
#define I_BIT		(1 << 27)
#define CC_V_BIT	(1 << 28)
#define CC_C_BIT	(1 << 29)
#define CC_Z_BIT	(1 << 30)
#define CC_N_BIT	(1 << 31)

#define processor_mode(regs) \
	((regs)->ARM_pc & MODE_MASK)

#define user_mode(regs) \
	(processor_mode(regs) == USR26_MODE)

#define interrupts_enabled(regs) \
	(!((regs)->ARM_pc & I_BIT))

#define fast_interrupts_enabled(regs) \
	(!((regs)->ARM_pc & F_BIT))

#define condition_codes(regs) \
	((regs)->ARM_pc & (CC_V_BIT|CC_C_BIT|CC_Z_BIT|CC_N_BIT))

#define pc_pointer(v) \
	((v) & 0x03fffffc)

#define instruction_pointer(regs) \
	(pc_pointer((regs)->ARM_pc))

/* Are the current registers suitable for user mode?
 * (used to maintain security in signal handlers)
 */
static inline int valid_user_regs(struct pt_regs *regs)
{
	if (!user_mode(regs) || regs->ARM_pc & (F_BIT | I_BIT))
		return 1;

	return 0;
}

#endif

