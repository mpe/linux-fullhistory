#ifndef _SPARC_SEMAPHORE_H
#define _SPARC_SEMAPHORE_H

/* Dinky, good for nothing, just barely irq safe, Sparc semaphores. */

#ifdef __KERNEL__

#include <asm/atomic.h>

struct semaphore {
	atomic_t count;
	atomic_t waking;
	struct wait_queue * wait;
};

#define MUTEX ((struct semaphore) { ATOMIC_INIT(1), ATOMIC_INIT(0), NULL })
#define MUTEX_LOCKED ((struct semaphore) { ATOMIC_INIT(0), ATOMIC_INIT(0), NULL })

extern void __down(struct semaphore * sem);
extern int __down_interruptible(struct semaphore * sem);
extern int __down_trylock(struct semaphore * sem);
extern void __up(struct semaphore * sem);

#define sema_init(sem, val)	atomic_set(&((sem)->count), val)

extern inline void down(struct semaphore * sem)
{
	register atomic_t *ptr asm("g1");
	register int increment asm("g2");

	ptr = (atomic_t *) __atomic_fool_gcc(sem);
	increment = 1;

	__asm__ __volatile__("
	mov	%%o7, %%g4
	call	___atomic_sub
	 add	%%o7, 8, %%o7
	tst	%%g2
	bl	2f
	 nop
1:
	.subsection 2
2:	save	%%sp, -64, %%sp
	mov	%%g1, %%l1
	mov	%%g5, %%l5
	call	%3
	 mov	%%g1, %%o0
	mov	%%l1, %%g1
	ba	1b
	 restore %%l5, %%g0, %%g5
	.previous\n"
	: "=&r" (increment)
	: "0" (increment), "r" (ptr), "i" (__down)
	: "g3", "g4", "g7", "memory", "cc");
}

extern inline int down_interruptible(struct semaphore * sem)
{
	register atomic_t *ptr asm("g1");
	register int increment asm("g2");

	ptr = (atomic_t *) __atomic_fool_gcc(sem);
	increment = 1;

	__asm__ __volatile__("
	mov	%%o7, %%g4
	call	___atomic_sub
	 add	%%o7, 8, %%o7
	tst	%%g2
	bl	2f
	 clr	%%g2
1:
	.subsection 2
2:	save	%%sp, -64, %%sp
	mov	%%g1, %%l1
	mov	%%g5, %%l5
	call	%3
	 mov	%%g1, %%o0
	mov	%%l1, %%g1
	mov	%%l5, %%g5
	ba	1b
	 restore %%o0, %%g0, %%g2
	.previous\n"
	: "=&r" (increment)
	: "0" (increment), "r" (ptr), "i" (__down_interruptible)
	: "g3", "g4", "g7", "memory", "cc");

	return increment;
}

extern inline int down_trylock(struct semaphore * sem)
{
	register atomic_t *ptr asm("g1");
	register int increment asm("g2");

	ptr = (atomic_t *) __atomic_fool_gcc(sem);
	increment = 1;

	__asm__ __volatile__("
	mov	%%o7, %%g4
	call	___atomic_sub
	 add	%%o7, 8, %%o7
	tst	%%g2
	bl	2f
	 clr	%%g2
1:
	.subsection 2
2:	save	%%sp, -64, %%sp
	mov	%%g1, %%l1
	mov	%%g5, %%l5
	call	%3
	 mov	%%g1, %%o0
	mov	%%l1, %%g1
	mov	%%l5, %%g5
	ba	1b
	 restore %%o0, %%g0, %%g2
	.previous\n"
	: "=&r" (increment)
	: "0" (increment), "r" (ptr), "i" (__down_trylock)
	: "g3", "g4", "g7", "memory", "cc");

	return increment;
}

extern inline void up(struct semaphore * sem)
{
	register atomic_t *ptr asm("g1");
	register int increment asm("g2");

	ptr = (atomic_t *) __atomic_fool_gcc(sem);
	increment = 1;

	__asm__ __volatile__("
	mov	%%o7, %%g4
	call	___atomic_add
	 add	%%o7, 8, %%o7
	tst	%%g2
	ble	2f
	 nop
1:
	.subsection 2
2:	save	%%sp, -64, %%sp
	mov	%%g1, %%l1
	mov	%%g5, %%l5
	call	%3
	 mov	%%g1, %%o0
	mov	%%l1, %%g1
	ba	1b
	 restore %%l5, %%g0, %%g5
	.previous\n"
	: "=&r" (increment)
	: "0" (increment), "r" (ptr), "i" (__up)
	: "g3", "g4", "g7", "memory", "cc");
}	

#endif /* __KERNEL__ */

#endif /* !(_SPARC_SEMAPHORE_H) */
