/* spinlock.h: 64-bit Sparc spinlock support.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __SPARC64_SPINLOCK_H
#define __SPARC64_SPINLOCK_H

#ifndef __ASSEMBLY__

#ifndef __SMP__

typedef unsigned char spinlock_t;
#define SPIN_LOCK_UNLOCKED 0

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
typedef unsigned long rwlock_t;
#define RW_LOCK_UNLOCKED (rwlock_t) { 0 }

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

/* To get debugging spinlocks which detect and catch
 * deadlock situations, set DEBUG_SPINLOCKS in the sparc64
 * specific makefile and rebuild your kernel.
 */

/* All of these locking primitives are expected to work properly
 * even in an RMO memory model, which currently is what the kernel
 * runs in.
 *
 * There is another issue.  Because we play games to save cycles
 * in the non-contention case, we need to be extra careful about
 * branch targets into the "spinning" code.  They live in their
 * own section, but the newer V9 branches have a shorter range
 * than the traditional 32-bit sparc branch variants.  The rule
 * is that the branches that go into and out of the spinner sections
 * must be pre-V9 branches.
 */

#ifndef SPIN_LOCK_DEBUG

typedef unsigned char spinlock_t;
#define SPIN_LOCK_UNLOCKED	0

#define spin_lock_init(lock)	(*((unsigned char *)(lock)) = 0)
#define spin_is_locked(lock)	(*((volatile unsigned char *)(lock)) != 0)

#define spin_unlock_wait(lock)	\
do {	membar("#LoadLoad");	\
} while(*((volatile unsigned char *)lock))

extern __inline__ void spin_lock(spinlock_t *lock)
{
	__asm__ __volatile__("
1:	ldstub		[%0], %%g7
	brnz,pn		%%g7, 2f
	 membar		#StoreLoad | #StoreStore
	.subsection	2
2:	ldub		[%0], %%g7
	brnz,pt		%%g7, 2b
	 membar		#LoadLoad
	b,a,pt		%%xcc, 1b
	.previous
"	: /* no outputs */
	: "r" (lock)
	: "g7", "memory");
}

extern __inline__ int spin_trylock(spinlock_t *lock)
{
	unsigned int result;
	__asm__ __volatile__("ldstub [%1], %0\n\t"
			     "membar #StoreLoad | #StoreStore"
			     : "=r" (result)
			     : "r" (lock)
			     : "memory");
	return (result == 0);
}

extern __inline__ void spin_unlock(spinlock_t *lock)
{
	__asm__ __volatile__("membar	#StoreStore | #LoadStore\n\t"
			     "stb	%%g0, [%0]\n\t"
			     : /* No outputs */
			     : "r" (lock)
			     : "memory");
}

extern __inline__ void spin_lock_irq(spinlock_t *lock)
{
	__asm__ __volatile__("
	wrpr		%%g0, 15, %%pil
1:	ldstub		[%0], %%g7
	brnz,pn		%%g7, 2f
	 membar		#StoreLoad | #StoreStore
	.subsection	2
2:	ldub		[%0], %%g7
	brnz,pt		%%g7, 2b
	 membar		#LoadLoad
	b,a,pt		%%xcc, 1b
	.previous
"	: /* no outputs */
	: "r" (lock)
	: "g7", "memory");
}

extern __inline__ void spin_unlock_irq(spinlock_t *lock)
{
	__asm__ __volatile__("
	membar		#StoreStore | #LoadStore
	stb		%%g0, [%0]
	wrpr		%%g0, 0x0, %%pil
"	: /* no outputs */
	: "r" (lock)
	: "memory");
}

#define spin_lock_irqsave(lock, flags)				\
do {	register spinlock_t *lp asm("g1");			\
	lp = lock;						\
	__asm__ __volatile__(					\
	"\n	rdpr		%%pil, %0\n"			\
	"	wrpr		%%g0, 15, %%pil\n"		\
	"1:	ldstub		[%1], %%g7\n"			\
	"	brnz,pn		%%g7, 2f\n"			\
	"	 membar		#StoreLoad | #StoreStore\n"	\
	"	.subsection	2\n"				\
	"2:	ldub		[%1], %%g7\n"			\
	"	brnz,pt		%%g7, 2b\n"			\
	"	 membar		#LoadLoad\n"			\
	"	b,a,pt		%%xcc, 1b\n"			\
	"	.previous\n"					\
	: "=&r" (flags)						\
	: "r" (lp)						\
	: "g7", "memory");					\
} while(0)

extern __inline__ void spin_unlock_irqrestore(spinlock_t *lock, unsigned long flags)
{
	__asm__ __volatile__("
	membar		#StoreStore | #LoadStore
	stb		%%g0, [%0]
	wrpr		%1, 0x0, %%pil
"	: /* no outputs */
	: "r" (lock), "r" (flags)
	: "memory");
}

#else /* !(SPIN_LOCK_DEBUG) */

typedef struct {
	unsigned char lock;
	unsigned int owner_pc, owner_cpu;
} spinlock_t;
#define SPIN_LOCK_UNLOCKED (spinlock_t) { 0, 0, NO_PROC_ID }
#define spin_lock_init(__lock)	\
do {	(__lock)->lock = 0; \
	(__lock)->owner_pc = 0; \
	(__lock)->owner_cpu = NO_PROC_ID; \
} while(0)
#define spin_is_locked(__lock)	(*((volatile unsigned char *)(&((__lock)->lock))) != 0)
#define spin_unlock_wait(__lock)	\
do { \
	membar("#LoadLoad"); \
} while(*((volatile unsigned char *)(&((__lock)->lock))))

extern void _do_spin_lock (spinlock_t *lock, char *str);
extern void _do_spin_unlock (spinlock_t *lock);
extern int _spin_trylock (spinlock_t *lock);

#define spin_trylock(lp)	_spin_trylock(lp)
#define spin_lock(lock)		_do_spin_lock(lock, "spin_lock")
#define spin_lock_irq(lock)	do { __cli(); _do_spin_lock(lock, "spin_lock_irq"); } while(0)
#define spin_lock_irqsave(lock, flags) do { __save_and_cli(flags); _do_spin_lock(lock, "spin_lock_irqsave"); } while(0)
#define spin_unlock(lock)	_do_spin_unlock(lock)
#define spin_unlock_irq(lock)	do { _do_spin_unlock(lock); __sti(); } while(0)
#define spin_unlock_irqrestore(lock, flags) do { _do_spin_unlock(lock); __restore_flags(flags); } while(0)

#endif /* SPIN_LOCK_DEBUG */

/* Multi-reader locks, these are much saner than the 32-bit Sparc ones... */

#ifndef SPIN_LOCK_DEBUG

typedef unsigned long rwlock_t;
#define RW_LOCK_UNLOCKED	0

extern __inline__ void read_lock(rwlock_t *rw)
{
	__asm__ __volatile__("
1:	ldx		[%0], %%g5
	brlz,pn		%%g5, 2f
4:	 add		%%g5, 1, %%g7
	casx		[%0], %%g5, %%g7
	cmp		%%g5, %%g7
	bne,pn		%%xcc, 1b
	 membar		#StoreLoad | #StoreStore
	.subsection	2
2:	ldx		[%0], %%g5
	brlz,pt		%%g5, 2b
	 membar		#LoadLoad
	b,a,pt		%%xcc, 4b
	.previous
"	: /* no outputs */
	: "r" (rw)
	: "g5", "g7", "cc", "memory");
}

extern __inline__ void read_unlock(rwlock_t *rw)
{
	__asm__ __volatile__("
1:	ldx		[%0], %%g5
	sub		%%g5, 1, %%g7
	casx		[%0], %%g5, %%g7
	cmp		%%g5, %%g7
	bne,pn		%%xcc, 1b
	 membar		#StoreLoad | #StoreStore
"	: /* no outputs */
	: "r" (rw)
	: "g5", "g7", "cc", "memory");
}

extern __inline__ void write_lock(rwlock_t *rw)
{
	__asm__ __volatile__("
	sethi		%%uhi(0x8000000000000000), %%g3
	sllx		%%g3, 32, %%g3
1:	ldx		[%0], %%g5
	brlz,pn		%%g5, 5f
4:	 or		%%g5, %%g3, %%g7
	casx		[%0], %%g5, %%g7
	cmp		%%g5, %%g7
	bne,pn		%%xcc, 1b
	 andncc		%%g7, %%g3, %%g0
	bne,pn		%%xcc, 7f
	 membar		#StoreLoad | #StoreStore
	.subsection	2
7:	ldx		[%0], %%g5
	andn		%%g5, %%g3, %%g7
	casx		[%0], %%g5, %%g7
	cmp		%%g5, %%g7
	bne,pn		%%xcc, 7b
	 membar		#StoreLoad | #StoreStore
5:	ldx		[%0], %%g5
	brnz,pt		%%g5, 5b
	 membar		#LoadLoad
	b,a,pt		%%xcc, 4b
	.previous
"	: /* no outputs */
	: "r" (rw)
	: "g3", "g5", "g7", "memory", "cc");
}

extern __inline__ void write_unlock(rwlock_t *rw)
{
	__asm__ __volatile__("
	sethi		%%uhi(0x8000000000000000), %%g3
	sllx		%%g3, 32, %%g3
1:	ldx		[%0], %%g5
	andn		%%g5, %%g3, %%g7
	casx		[%0], %%g5, %%g7
	cmp		%%g5, %%g7
	bne,pn		%%xcc, 1b
	 membar		#StoreLoad | #StoreStore
"	: /* no outputs */
	: "r" (rw)
	: "g3", "g5", "g7", "memory", "cc");
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

#else /* !(SPIN_LOCK_DEBUG) */

typedef struct {
	unsigned long lock;
	unsigned int writer_pc, writer_cpu;
	unsigned int reader_pc[4];
} rwlock_t;
#define RW_LOCK_UNLOCKED	(rwlock_t) { 0, 0, NO_PROC_ID, { 0, 0, 0, 0 } }

extern void _do_read_lock(rwlock_t *rw, char *str);
extern void _do_read_unlock(rwlock_t *rw, char *str);
extern void _do_write_lock(rwlock_t *rw, char *str);
extern void _do_write_unlock(rwlock_t *rw);

#define read_lock(lock)	\
do {	unsigned long flags; \
	__save_and_cli(flags); \
	_do_read_lock(lock, "read_lock"); \
	__restore_flags(flags); \
} while(0)
#define read_lock_irq(lock)	do { __cli(); _do_read_lock(lock, "read_lock_irq"); } while(0)
#define read_lock_irqsave(lock, flags) do { __save_and_cli(flags); _do_read_lock(lock, "read_lock_irqsave"); } while(0)

#define read_unlock(lock) \
do {	unsigned long flags; \
	__save_and_cli(flags); \
	_do_read_unlock(lock, "read_unlock"); \
	__restore_flags(flags); \
} while(0)
#define read_unlock_irq(lock)	do { _do_read_unlock(lock, "read_unlock_irq"); __sti() } while(0)
#define read_unlock_irqrestore(lock, flags) do { _do_read_unlock(lock, "read_unlock_irqrestore"); __restore_flags(flags); } while(0)

#define write_lock(lock) \
do {	unsigned long flags; \
	__save_and_cli(flags); \
	_do_write_lock(lock, "write_lock"); \
	__restore_flags(flags); \
} while(0)
#define write_lock_irq(lock)	do { __cli(); _do_write_lock(lock, "write_lock_irq"); } while(0)
#define write_lock_irqsave(lock, flags) do { __save_and_cli(flags); _do_write_lock(lock, "write_lock_irqsave"); } while(0)

#define write_unlock(lock) \
do {	unsigned long flags; \
	__save_and_cli(flags); \
	_do_write_unlock(lock); \
	__restore_flags(flags); \
} while(0)
#define write_unlock_irq(lock)	do { _do_write_unlock(lock); __sti(); } while(0)
#define write_unlock_irqrestore(lock, flags) do { _do_write_unlock(lock); __restore_flags(flags); } while(0)

#endif /* SPIN_LOCK_DEBUG */

#endif /* __SMP__ */

#endif /* !(__ASSEMBLY__) */

#endif /* !(__SPARC64_SPIN%0_H) */
