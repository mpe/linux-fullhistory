#ifndef __ASM_ARM_PTRACE_H
#define __ASM_ARM_PTRACE_H

#include <asm/proc/ptrace.h>

#ifdef __KERNEL__
extern void show_regs(struct pt_regs *);
#endif

#endif

