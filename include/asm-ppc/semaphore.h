#ifndef _PPC_SEMAPHORE_H
#define _PPC_SEMAPHORE_H

#include <asm/atomic.h>

struct semaphore {
	atomic_t count;
	atomic_t waiting;
	struct wait_queue * wait;
};

#define MUTEX ((struct semaphore) { 1, 0, NULL })
#define MUTEX_LOCKED ((struct semaphore) { 0, 0, NULL })

extern void __down(struct semaphore * sem);
extern void __up(struct semaphore * sem);

extern void atomic_add(int c, int *v);
extern void atomic_sub(int c, int *v);

#define sema_init(sem, val)	atomic_set(&((sem)->count), val)

static inline int waking_non_zero(struct semaphore *sem)
{
	unsigned long flags;
	int ret = 0;

	save_flags(flags);
	cli();
	if (atomic_read(&sem->waking) > 0) {
		atomic_dec(&sem->waking);
		ret = 1;
	}
	restore_flags(flags);
	return ret;
}

extern inline void down(struct semaphore * sem)
{
  for (;;)
  {
    atomic_dec_return(&sem->count);
    if ( sem->count >= 0)
      break;
    __down(sem);
  }
}

extern inline void up(struct semaphore * sem)
{
  atomic_inc_return(&sem->count);
  if ( sem->count <= 0)
    __up(sem);
}	

#endif /* !(_PPC_SEMAPHORE_H) */
