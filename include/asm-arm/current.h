#ifndef _ASMARM_CURRENT_H
#define _ASMARM_CURRENT_H

static inline unsigned long get_sp(void)
{
	unsigned long sp;
	__asm__ ("mov	%0,sp" : "=r" (sp));
	return sp;
}

/* Old compilers seem to generate bad code if we allow `current' to be
   non volatile.  */
#if (__GNUC__ > 2) || (__GNUC__ == 2 && __GNUC_MINOR__ > 90)
static inline struct task_struct *get_current(void) __attribute__ (( __const__ ));
#define __VOLATILE_CURRENT
#else
#define __VOLATILE_CURRENT volatile
#endif

static inline struct task_struct *get_current(void)
{
	struct task_struct *ts;
	__asm__ __VOLATILE_CURRENT (
	"bic	%0, sp, #0x1f00		@ get_current
	bic	%0, %0, #0x00ff" 
	: "=r" (ts));
	return ts;
}

#define current (get_current())

#endif /* _ASMARM_CURRENT_H */
