#ifndef _I386_CURRENT_H
#define _I386_CURRENT_H

/* Some architectures may want to do something "clever" here since
 * this is the most frequently accessed piece of data in the entire
 * kernel.  For an example, see the Sparc implementation where an
 * entire register is hard locked to contain the value of current.
 */
extern struct task_struct *current_set[NR_CPUS];

static inline unsigned long get_esp(void)
{
	unsigned long esp;
	__asm__("movl %%esp,%0":"=r" (esp));
	return esp;
}

#define current ((struct task_struct *)(get_esp() & ~8191UL))


#endif /* !(_I386_CURRENT_H) */
