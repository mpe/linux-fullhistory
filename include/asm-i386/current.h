#ifndef _I386_CURRENT_H
#define _I386_CURRENT_H

static inline unsigned long get_esp(void)
{
	unsigned long esp;
	__asm__("movl %%esp,%0":"=r" (esp));
	return esp;
}

#define current ((struct task_struct *)(get_esp() & ~8191UL))


#endif /* !(_I386_CURRENT_H) */
