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
