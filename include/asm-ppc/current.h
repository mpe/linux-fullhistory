#ifndef _PPC_CURRENT_H
#define _PPC_CURRENT_H

#include <linux/config.h>

extern struct task_struct *current_set[1];

register struct task_struct *current asm("r2");

#endif /* !(_PPC_CURRENT_H) */
