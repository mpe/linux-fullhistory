/*
 * This program is used to generate definitions needed by
 * assembly language modules.
 *
 * We use the technique used in the OSF Mach kernel code:
 * generate asm statements containing #defines,
 * compile this file to assembler, and then extract the
 * #defines from the assembly-language output.
 */

#include <stddef.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/processor.h>

#define DEFINE(sym, val) \
	asm volatile("\n#define\t" #sym "\t%0" : : "i" (val))

void
main(void)
{
	/*DEFINE(KERNELBASE, KERNELBASE);*/
	DEFINE(STATE, offsetof(struct task_struct, state));
	DEFINE(NEXT_TASK, offsetof(struct task_struct, next_task));
	DEFINE(COUNTER, offsetof(struct task_struct, counter));
	DEFINE(BLOCKED, offsetof(struct task_struct, blocked));
	DEFINE(SIGNAL, offsetof(struct task_struct, signal));
	DEFINE(TSS, offsetof(struct task_struct, tss));
	DEFINE(KSP, offsetof(struct thread_struct, ksp));
	DEFINE(PG_TABLES, offsetof(struct thread_struct, pg_tables));
#ifdef CONFIG_PMAC	
	DEFINE(LAST_PC, offsetof(struct thread_struct, last_pc));
	DEFINE(USER_STACK, offsetof(struct thread_struct, user_stack));
#endif	
	DEFINE(LAST_SYSCALL, offsetof(struct thread_struct, last_syscall));
	DEFINE(PT_REGS, offsetof(struct thread_struct, regs));
	DEFINE(PF_TRACESYS, PF_TRACESYS);
	DEFINE(TASK_FLAGS, offsetof(struct task_struct, flags));
	DEFINE(TSS_FPR0, offsetof(struct thread_struct, fpr[0]));
#if 0
	DEFINE(TSS_FPR1, offsetof(struct thread_struct, fpr[1]));
	DEFINE(TSS_FPR2, offsetof(struct thread_struct, fpr[2]));
	DEFINE(TSS_FPR3, offsetof(struct thread_struct, fpr[3]));
	DEFINE(TSS_FPR4, offsetof(struct thread_struct, fpr[4]));
	DEFINE(TSS_FPR5, offsetof(struct thread_struct, fpr[5]));
	DEFINE(TSS_FPR6, offsetof(struct thread_struct, fpr[6]));
	DEFINE(TSS_FPR7, offsetof(struct thread_struct, fpr[7]));
	DEFINE(TSS_FPR8, offsetof(struct thread_struct, fpr[8]));
	DEFINE(TSS_FPR9, offsetof(struct thread_struct, fpr[9]));
	DEFINE(TSS_FPR10, offsetof(struct thread_struct, fpr[10]));
	DEFINE(TSS_FPR11, offsetof(struct thread_struct, fpr[11]));
	DEFINE(TSS_FPR12, offsetof(struct thread_struct, fpr[12]));
	DEFINE(TSS_FPR13, offsetof(struct thread_struct, fpr[13]));
	DEFINE(TSS_FPR14, offsetof(struct thread_struct, fpr[14]));
	DEFINE(TSS_FPR15, offsetof(struct thread_struct, fpr[15]));
	DEFINE(TSS_FPR16, offsetof(struct thread_struct, fpr[16]));
	DEFINE(TSS_FPR17, offsetof(struct thread_struct, fpr[17]));
	DEFINE(TSS_FPR18, offsetof(struct thread_struct, fpr[18]));
	DEFINE(TSS_FPR19, offsetof(struct thread_struct, fpr[19]));
	DEFINE(TSS_FPR20, offsetof(struct thread_struct, fpr[20]));
	DEFINE(TSS_FPR21, offsetof(struct thread_struct, fpr[21]));
	DEFINE(TSS_FPR22, offsetof(struct thread_struct, fpr[22]));
	DEFINE(TSS_FPR23, offsetof(struct thread_struct, fpr[23]));
	DEFINE(TSS_FPR24, offsetof(struct thread_struct, fpr[24]));
	DEFINE(TSS_FPR25, offsetof(struct thread_struct, fpr[25]));
	DEFINE(TSS_FPR26, offsetof(struct thread_struct, fpr[26]));
	DEFINE(TSS_FPR27, offsetof(struct thread_struct, fpr[27]));
	DEFINE(TSS_FPR28, offsetof(struct thread_struct, fpr[28]));
	DEFINE(TSS_FPR29, offsetof(struct thread_struct, fpr[29]));
	DEFINE(TSS_FPR30, offsetof(struct thread_struct, fpr[30]));
	DEFINE(TSS_FPR31, offsetof(struct thread_struct, fpr[31]));
#endif
	DEFINE(TSS_FPSCR, offsetof(struct thread_struct, fpscr));
	/* Interrupt register frame */
	DEFINE(TASK_UNION_SIZE, sizeof(union task_union));
	DEFINE(STACK_FRAME_OVERHEAD, STACK_FRAME_OVERHEAD);
	DEFINE(INT_FRAME_SIZE, STACK_FRAME_OVERHEAD + sizeof(struct pt_regs));
	DEFINE(GPR0, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[0]));
	DEFINE(GPR1, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[1]));
	DEFINE(GPR2, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[2]));
	DEFINE(GPR3, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[3]));
	DEFINE(GPR4, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[4]));
	DEFINE(GPR5, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[5]));
	DEFINE(GPR6, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[6]));
	DEFINE(GPR7, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[7]));
	DEFINE(GPR8, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[8]));
	DEFINE(GPR9, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[9]));
	DEFINE(GPR10, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[10]));
	DEFINE(GPR11, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[11]));
	DEFINE(GPR12, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[12]));
	DEFINE(GPR13, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[13]));
	DEFINE(GPR14, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[14]));
	DEFINE(GPR15, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[15]));
	DEFINE(GPR16, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[16]));
	DEFINE(GPR17, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[17]));
	DEFINE(GPR18, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[18]));
	DEFINE(GPR19, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[19]));
	DEFINE(GPR20, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[20]));
	DEFINE(GPR21, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[21]));
	DEFINE(GPR22, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[22]));
	DEFINE(GPR23, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[23]));
	DEFINE(GPR24, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[24]));
	DEFINE(GPR25, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[25]));
	DEFINE(GPR26, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[26]));
	DEFINE(GPR27, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[27]));
	DEFINE(GPR28, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[28]));
	DEFINE(GPR29, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[29]));
	DEFINE(GPR30, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[30]));
	DEFINE(GPR31, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[31]));
	/* Note: these symbols include _ because they overlap with special register names */
	DEFINE(_NIP, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, nip));
	DEFINE(_MSR, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, msr));
	DEFINE(_CTR, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, ctr));
	DEFINE(_LINK, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, link));
	DEFINE(_CCR, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, ccr));
	DEFINE(_XER, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, xer));
	DEFINE(_DAR, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, dar));
	DEFINE(_DSISR, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, dsisr));
	DEFINE(ORIG_GPR3, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, orig_gpr3));
	DEFINE(RESULT, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, result));
	DEFINE(TRAP, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, trap));
}
