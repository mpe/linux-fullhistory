#ifndef _ALPHA_CURRENT_H
#define _ALPHA_CURRENT_H

/* Some architectures may want to do something "clever" here since
 * this is the most frequently accessed piece of data in the entire
 * kernel.
 */
extern struct task_struct *current_set[NR_CPUS];

register struct task_struct *current __asm__("$8");

#endif /* !(_ALPHA_CURRENT_H) */
