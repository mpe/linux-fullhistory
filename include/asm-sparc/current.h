#ifndef _SPARC_CURRENT_H
#define _SPARC_CURRENT_H

/* Some architectures may want to do something "clever" here since
 * this is the most frequently accessed piece of data in the entire
 * kernel.
 */
extern struct task_struct *current_set[NR_CPUS];

/* Sparc rules... */
register struct task_struct *current asm("g6");

#endif /* !(_SPARC_CURRENT_H) */
