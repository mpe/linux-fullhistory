#ifndef __ASM_SH_BUGS_H
#define __ASM_SH_BUGS_H

/*
 * This is included by init/main.c to check for architecture-dependent bugs.
 *
 * Needs:
 *	void check_bugs(void);
 */

/*
 * I don't know of any Super-H bugs yet.
 */

#include <asm/processor.h>

static void __init check_bugs(void)
{
	extern unsigned long loops_per_sec;

	cpu_data->loops_per_sec = loops_per_sec;
	
	switch (cpu_data->type) {
	case CPU_SH7708:
		printk("CPU: SH7708/SH7709\n");
		break;
	case CPU_SH7729:
		printk("CPU: SH7709A/SH7729\n");
		break;
	case CPU_SH7750:
		printk("CPU: SH7750\n");
		break;
	default:
		printk("CPU: ??????\n");
		break;
	}
}
#endif /* __ASM_SH_BUGS_H */
