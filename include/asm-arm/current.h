#ifndef _ASMARM_CURRENT_H
#define _ASMARM_CURRENT_H

static inline unsigned long get_sp(void)
{
	unsigned long sp;
	__asm__ ("mov %0,sp" : "=r" (sp));
	return sp;
}

static inline struct task_struct *get_current(void)
{
	struct task_struct *ts;
	__asm__ __volatile__("
	bic	%0, sp, #0x1f00
	bic	%0, %0, #0x00ff
	" : "=r" (ts));
	return ts;
}

#define current (get_current())

#endif /* _ASMARM_CURRENT_H */
