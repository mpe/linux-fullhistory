/*
 * linux/arch/arm/lib/fp_support.c
 *
 * Copyright (C) 1995, 1996 Russell King
 */

#include <linux/sched.h>
#include <linux/linkage.h>

extern void (*fp_save)(struct fp_soft_struct *);

asmlinkage void fp_setup(void)
{
	struct task_struct *p;

	p = &init_task;
	do {
		fp_save(&p->tss.fpstate.soft);
		p = p->next_task;
	}
	while (p != &init_task);
}
