#ifndef __ASM_ARM_PTRACE_H
#define __ASM_ARM_PTRACE_H

#include <asm/proc/ptrace.h>

#ifndef __ASSEMBLY__
#define pc_pointer(v) \
	((v) & ~PCMASK)

#define instruction_pointer(regs) \
	(pc_pointer((regs)->ARM_pc))

#ifdef __KERNEL__
extern void show_regs(struct pt_regs *);
#endif

#endif /* __ASSEMBLY__ */

#endif

