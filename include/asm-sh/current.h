#ifndef __ASM_SH_CURRENT_H
#define __ASM_SH_CURRENT_H

/*
 * Copyright (C) 1999 Niibe Yutaka
 *
 */

struct task_struct;

static __inline__ struct task_struct * get_current(void)
{
	struct task_struct *current;

	__asm__("stc	r4_bank,%0\n\t"
		"add	%1,%0"
		:"=&r" (current)
		:"r" (-8192));
	return current;
}

#define current get_current()

#endif /* __ASM_SH_CURRENT_H */
