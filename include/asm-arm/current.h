#ifndef _ASMARM_CURRENT_H
#define _ASMARM_CURRENT_H

/* Old compilers seem to generate bad code if we allow `current' to be
   non volatile.  */
#if (__GNUC__ > 2) || (__GNUC__ == 2 && __GNUC_MINOR__ > 90)
static inline struct task_struct *get_current(void) __attribute__ (( __const__ ));
#endif

static inline struct task_struct *get_current(void)
{
	register unsigned long sp asm ("sp");
	return (struct task_struct *)(sp & ~0x1fff);
}

#define current (get_current())

#endif /* _ASMARM_CURRENT_H */
