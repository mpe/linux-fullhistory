/*
 * linux/include/asm-arm/semaphore.h
 */
#ifndef __ASM_ARM_SEMAPHORE_H
#define __ASM_ARM_SEMAPHORE_H

#include <linux/linkage.h>
#include <asm/system.h>
#include <asm/atomic.h>

struct semaphore {
	atomic_t count;
	int waking;
	struct wait_queue * wait;
};

#define MUTEX ((struct semaphore) { ATOMIC_INIT(1), 0, NULL })
#define MUTEX_LOCKED ((struct semaphore) { ATOMIC_INIT(0), 0, NULL })

asmlinkage void __down_failed (void /* special register calling convention */);
asmlinkage int  __down_interruptible_failed (void /* special register calling convention */);
asmlinkage int  __down_failed_trylock(void  /* params in registers */);
asmlinkage void __up_wakeup (void /* special register calling convention */);

extern void __down(struct semaphore * sem);
extern int  __down_interruptible(struct semaphore * sem);
extern int  __down_trylock(struct semaphore * sem);
extern void __up(struct semaphore * sem);

#define sema_init(sem, val)	atomic_set(&((sem)->count), (val))

#include <asm/proc/semaphore.h>

#endif
