/*
 * This program is used to generate definitions needed by
 * assembly language modules.
 *
 * We use the technique used in the OSF Mach kernel code:
 * generate asm statements containing #defines,
 * compile this file to assembler, and then extract the
 * #defines from the assembly-language output.
 */

#include <linux/stddef.h>
#include <linux/sched.h>

#define DEFINE(sym, val) \
	asm volatile("\n#define " #sym " %c0" : : "i" (val))

int main(void)
{
	DEFINE(TS_TSS, offsetof(struct task_struct, tss));
	DEFINE(TS_ESP0, offsetof(struct task_struct, tss.esp0));
	DEFINE(TS_FPU, offsetof(struct task_struct, tss.fp));
	return 0;
}
