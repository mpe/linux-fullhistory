/* spinlock.h: 64-bit Sparc spinlock support.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __SPARC64_SPINLOCK_H
#define __SPARC64_SPINLOCK_H

#ifndef __ASSEMBLY__

#ifndef __SMP__

typedef struct { } spinlock_t;
#define SPIN_LOCK_UNLOCKED { }

#define spin_lock_init(lock)	do { } while(0)
#define spin_lock(lock)		do { } while(0)
#define spin_trylock(lock)	do { } while(0)
#define spin_unlock_wait(lock)	do { } while(0)
#define spin_unlock(lock)	do { } while(0)
#define spin_lock_irq(lock)	cli()
#define spin_unlock_irq(lock)	sti()

#define spin_lock_irqsave(lock, flags)		save_and_cli(flags)
#define spin_unlock_irqrestore(lock, flags)	restore_flags(flags)

/*
 * Read-write spinlocks, allowing multiple readers
 * but only one writer.
 *
 * NOTE! it is quite common to have readers in interrupts
 * but no interrupt writers. For those circumstances we
 * can "mix" irq-safe locks - any writer needs to get a
 * irq-safe write-lock, but readers can get non-irqsafe
 * read-locks.
 */
typedef struct { } rwlock_t;
#define RW_LOCK_UNLOCKED { }

#define read_lock(lock)		do { } while(0)
#define read_unlock(lock)	do { } while(0)
#define write_lock(lock)	do { } while(0)
#define write_unlock(lock)	do { } while(0)
#define read_lock_irq(lock)	cli()
#define read_unlock_irq(lock)	sti()
#define write_lock_irq(lock)	cli()
#define write_unlock_irq(lock)	sti()

#define read_lock_irqsave(lock, flags)		save_and_cli(flags)
#define read_unlock_irqrestore(lock, flags)	restore_flags(flags)
#define write_lock_irqsave(lock, flags)		save_and_cli(flags)
#define write_unlock_irqrestore(lock, flags)	restore_flags(flags)

#else /* !(__SMP__) */

typedef unsigned char spinlock_t;
#define SPIN_LOCK_UNLOCKED	0
#define spin_lock_init(lock)	(*(lock) = 0)
#define spin_unlock_wait(lock)	do { barrier(); } while(*(volatile spinlock_t *)lock)

extern __inline__ void spin_lock(spinlock_t *lock)
{
	__asm__ __volatile__("
1:	ldstub		[%0], %%g2
	brnz,a,pn	%%g2, 2f
	 ldub		[%0], %%g2
	.text		2
2:	brnz,a,pt	2b
	 ldub		[%0], %%g2
	b,a,pt		%%xcc, 1b
	.previous
"	: /* no outputs */
	: "r" (lock)
	: "g2", "memory");
}

extern __inline__ int spin_trylock(spinlock_t *lock)
{
	unsigned int result;
	__asm__ __volatile__("ldstub [%1], %0"
			     : "=r" (result)
			     : "r" (lock)
			     : "memory");
	return (result == 0);
}

extern __inline__ void spin_unlock(spinlock_t *lock)
{
	__asm__ __volatile__("stb %%g0, [%0]" : : "r" (lock) : "memory");
}

extern __inline__ void spin_lock_irq(spinlock_t *lock)
{
	__asm__ __volatile__("
	wrpr		%%g0, 15, %%pil
	ldstub		[%0], %%g2
	brnz,a,pn	%%g2, 2f
	 ldub		[%0], %%g2
	.text		2
2:	brnz,a,pt	2b
	 ldub		[%0], %%g2
	b,a,pt		%%xcc, 1b
	.previous
"	: /* no outputs */
	: "r" (lock)
	: "g2", "memory");
}

extern __inline__ void spin_unlock_irq(spinlock_t *lock)
{
	__asm__ __volatile__("
	stb		%%g0, [%0]
	wrpr		%%g0, 0x0, %%pil
"	: /* no outputs */
	: "r" (lock)
	: "memory");
}

#define spin_lock_irqsave(lock, flags)			\
do {	register spinlock_t *lp asm("g1");		\
	lp = lock;					\
	__asm__ __volatile__(				\
	"	rdpr		%%pil, %0\n\t"		\
	"	wrpr		%%g0, 15, %%pil\n\t"	\
	"1:	ldstub		[%1], %%g2\n\t"		\
	"	brnz,a,pnt	%%g2, 2f\n\t"		\
	"	 ldub		[%1], %%g2\n\t"		\
	"	.text		2\n\t"			\
	"2:	brnz,a,pt	%%g2, 2b\n\t"		\
	"	 ldub		[%1], %%g2\n\t"		\
	"	b,a,pt		%%xcc, 1b\n\t"		\
	"	.previous\n"				\
	: "=r" (flags)					\
	: "r" (lp)					\
	: "g2", "memory");				\
} while(0)

extern __inline__ void spin_unlock_irqrestore(spinlock_t *lock, unsigned long flags)
{
	__asm__ __volatile__("
	stb		%%g0, [%0]
	wrpr		%1, 0x0, %%pil
"	: /* no outputs */
	: "r" (lock), "r" (flags)
	: "memory");
}

/* Multi-reader locks, these are much saner than the 32-bit Sparc ones... */

typedef unsigned long rwlock_t;
#define RW_LOCK_UNLOCKED	0

extern __inline__ void read_lock(rwlock_t *rw)
{
	__asm__ __volatile__("
	ldx		[%0], %%g2
1:
	brlz,pn		%%g2, 2f
4:	 add		%%g2, 1, %%g3
	casx		[%0], %%g2, %%g3
	cmp		%%g2, %%g3
	bne,a,pn	%%xcc, 1b
	 ldx		[%0],%%g2
	.text		2
2:	ldx		[%0], %%g2
3:	brlz,pt		%%g2, 3b
	 ldx		[%0], %%g2
	b,a,pt		%%xcc, 4b
	.previous
"	: /* no outputs */
	: "r" (rw)
	: "g2", "g3", "memory");
}

extern __inline__ void read_unlock(rwlock_t *rw)
{
	__asm__ __volatile__("
	ldx		[%0], %%g2
1:
	sub		%%g2, 1, %%g3
	casx		[%0], %%g2, %%g3
	cmp		%%g2, %%g3
	bne,a,pn	%%xcc, 1b
	 ldx		[%0], %%g2
"	: /* no outputs */
	: "r" (rw)
	: "g2", "g3", "memory");
}

extern __inline__ void write_lock(rwlock_t *rw)
{
	__asm__ __volatile__("
	sethi		%%uhi(0x8000000000000000), %%g5
	ldx		[%0] %%g2
	sllx		%%g5, 32, %%g5
1:
	brlz,pn		%%g2, 5f
4:	 or		%%g2, %%g5, %%g3
	casx		[%0], %%g2, %%g3
	cmp		%%g2, %%g3
	bne,a,pn	%%xcc, 1b
	 ldx		[%0], %%g2
	andncc		%%g3, %%g5, %%g0
	bne,a,pn	%%xcc, 3f
	 ldx		[%0], %%g2
	.text		2
3:
	andn		%%g2, %%g5, %%g3
	casx		[%0], %%g2, %%g3
	cmp		%%g2, %%g3
	bne,a,pn	%%xcc, 3b
	 ldx		[%0], %%g2
5:	ldx		[%0], %%g2
6:	brlz,pt		%%g2, 6b
	 ldx		[%0], %%g2
	b,a,pt		%%xcc, 4b
"	: /* no outputs */
	: "r" (rw)
	: "g2", "g3", "g5", "memory", "cc");
}

extern __inline__ void write_unlock(rwlock_t *rw)
{
	__asm__ __volatile__("
	sethi		%%uhi(0x8000000000000000), %%g5
	ldx		[%0], %%g2
	sllx		%%g5, 32, %%g5
1:
	andn		%%g2, %%g5, %%g3
	casx		[%0], %%g2, %%g3
	cmp		%%g2, %%g3
	bne,a,pn	%%xcc, 1b
	 ldx		[%0], %%g2
"	: /* no outputs */
	: "r" (rw)
	: "g2", "g3", "g5", "memory", "cc");
}

#define read_lock_irq(lock)	do { __cli(); read_lock(lock); } while (0)
#define read_unlock_irq(lock)	do { read_unlock(lock); __sti(); } while (0)
#define write_lock_irq(lock)	do { __cli(); write_lock(lock); } while (0)
#define write_unlock_irq(lock)	do { write_unlock(lock); __sti(); } while (0)

#define read_lock_irqsave(lock, flags)	\
	do { __save_and_cli(flags); read_lock(lock); } while (0)
#define read_unlock_irqrestore(lock, flags) \
	do { read_unlock(lock); __restore_flags(flags); } while (0)
#define write_lock_irqsave(lock, flags)	\
	do { __save_and_cli(flags); write_lock(lock); } while (0)
#define write_unlock_irqrestore(lock, flags) \
	do { write_unlock(lock); __restore_flags(flags); } while (0)

#endif /* __SMP__ */

#endif /* !(__ASSEMBLY__) */

#endif /* !(__SPARC64_SPIN%0_H) */
