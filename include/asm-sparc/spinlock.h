/* spinlock.h: 32-bit Sparc spinlock support.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __SPARC_SPINLOCK_H
#define __SPARC_SPINLOCK_H

#ifndef __ASSEMBLY__

#ifndef __SMP__

typedef struct { } spinlock_t;
#define SPIN_LOCK_UNLOCKED { }

#define spin_lock_init(lock)	do { } while(0)
#define spin_lock(lock)		do { } while(0)
#define spin_trylock(lock)	do { } while(0)
#define spin_unlock(lock)	do { } while(0)
#define spin_lock_irq(lock)	cli()
#define spin_unlock_irq(lock)	sti()

#define spin_lock_irqsave(lock, flags)		save_and_cli(flags)
#define spin_unlock_irqrestore(lock, flags)	restore_flags(flags)

#else /* !(__SMP__) */

#include <asm/psr.h>

typedef unsigned char spinlock_t;
#define SPIN_LOCK_UNLOCKED	0

extern __inline__ void spin_lock(spinlock_t *lock)
{
	register spinlock_t *lp asm("g1");
	lp = lock;
	__asm__ __volatile__("
	ldstub	[%%g1], %%g2
	orcc	%%g2, 0x0, %%g0
	be	1f
	 mov	%%o7, %%g4
	call	___spinlock_waitfor
	 ldub	[%%g1], %%g2
1:"	: /* no outputs */
	: "r" (lp)
	: "g2", "g4", "memory", "cc");
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
	register spinlock_t *lp asm("g1");
	lp = lock;
	__asm__ __volatile__("
	rd	%%psr, %%g2
	or	%%g2, %0, %%g2
	wr	%%g2, 0x0, %%psr
	nop; nop; nop;
	ldstub	[%%g1], %%g2
	orcc	%%g2, 0x0, %%g0
	be	1f
	 mov	%%o7, %%g4
	call	___spinlock_waitfor
	 ldub	[%%g1], %%g2
1:"	: /* No outputs */
	: "i" (PSR_PIL), "r" (lp)
	: "g2", "g4", "memory", "cc");
}

extern __inline__ void spin_unlock_irq(spinlock_t *lock)
{
	__asm__ __volatile__("
	rd	%%psr, %%g2
	andn	%%g2, %1, %%g2
	stb	%%g0, [%0]
	wr	%%g2, 0x0, %%psr
	nop; nop; nop;
"	: /* No outputs. */
	: "r" (lock), "i" (PSR_PIL)
	: "g2", "memory");
}

#define spin_lock_irqsave(lock, flags)		\
do {						\
	register spinlock_t *lp asm("g1");	\
	lp = lock;				\
	__asm__ __volatile__(			\
	"rd	%%psr, %0\n\t"			\
	"or	%0, %1, %%g2\n\t"		\
	"wr	%%g2, 0x0, %%psr\n\t"		\
	"nop; nop; nop;\n\t"			\
	"ldstub	[%%g1], %%g2\n\t"		\
	"orcc	%%g2, 0x0, %%g0\n\t"		\
	"be	1f\n\t"				\
	" mov	%%o7, %%g4\n\t"			\
	"call	___spinlock_waitfor\n\t"	\
	" ldub	[%%g1], %%g2\n\t"		\
"1:"	: "=r" (flags)				\
	: "i" (PSR_PIL), "r" (lp)		\
	: "g2", "g4", "memory", "cc");		\
} while(0)

extern __inline__ void spin_unlock_irqrestore(spinlock_t *lock, unsigned long flags)
{
	__asm__ __volatile__("
	stb	%%g0, [%0]
	wr	%1, 0x0, %%psr
	nop; nop; nop;
"	: /* No outputs. */
	: "r" (lock), "r" (flags)
	: "memory", "cc");
}

#endif /* __SMP__ */

#endif /* !(__ASSEMBLY__) */

#endif /* __SPARC_SPINLOCK_H */
