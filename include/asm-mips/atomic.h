/*
 * Atomic operations that C can't guarantee us.  Useful for
 * resource counting etc..
 *
 * But use these as seldom as possible since they are much more slower
 * than regular operations.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 97, 99, 2000, 03 by Ralf Baechle
 */
#ifndef _ASM_ATOMIC_H
#define _ASM_ATOMIC_H

#include <linux/config.h>

#include <asm/system.h>

typedef struct { volatile int counter; } atomic_t;
typedef struct { volatile __s64 counter; } atomic64_t;

#ifdef __KERNEL__
#define ATOMIC_INIT(i)    { (i) }
#define ATOMIC64_INIT(i)    { (i) }

/*
 * atomic_read - read atomic variable
 * @v: pointer of type atomic_t
 *
 * Atomically reads the value of @v.  Note that the guaranteed
 * useful range of an atomic_t is only 24 bits.
 */
#define atomic_read(v)		((v)->counter)

/*
 * atomic64_read - read atomic variable
 * @v: pointer of type atomic64_t
 *
 */
#define atomic64_read(v)	((v)->counter)

/*
 * atomic_set - set atomic variable
 * @v: pointer of type atomic_t
 * @i: required value
 *
 * Atomically sets the value of @v to @i.  Note that the guaranteed
 * useful range of an atomic_t is only 24 bits.
 */
#define atomic_set(v,i)		((v)->counter = (i))

/*
 * atomic64_set - set atomic variable
 * @v: pointer of type atomic64_t
 * @i: required value
 */
#define atomic64_set(v,i)	((v)->counter = (i))

#ifndef CONFIG_CPU_HAS_LLSC

/*
 * The MIPS I implementation is only atomic with respect to
 * interrupts.  R3000 based multiprocessor machines are rare anyway ...
 *
 * atomic_add - add integer to atomic variable
 * @i: integer value to add
 * @v: pointer of type atomic_t
 *
 * Atomically adds @i to @v.  Note that the guaranteed useful range
 * of an atomic_t is only 24 bits.
 */
static __inline__ void atomic_add(int i, atomic_t * v)
{
	unsigned long flags;

	local_irq_save(flags);
	v->counter += i;
	local_irq_restore(flags);
}

/*
 * atomic_sub - subtract the atomic variable
 * @i: integer value to subtract
 * @v: pointer of type atomic_t
 *
 * Atomically subtracts @i from @v.  Note that the guaranteed
 * useful range of an atomic_t is only 24 bits.
 */
static __inline__ void atomic_sub(int i, atomic_t * v)
{
	unsigned long flags;

	local_irq_save(flags);
	v->counter -= i;
	local_irq_restore(flags);
}

static __inline__ int atomic_add_return(int i, atomic_t * v)
{
	unsigned long flags;
	int temp;

	local_irq_save(flags);
	temp = v->counter;
	temp += i;
	v->counter = temp;
	local_irq_restore(flags);

	return temp;
}

static __inline__ int atomic_sub_return(int i, atomic_t * v)
{
	unsigned long flags;
	int temp;

	local_irq_save(flags);
	temp = v->counter;
	temp -= i;
	v->counter = temp;
	local_irq_restore(flags);

	return temp;
}

#else

/*
 * ... while for MIPS II and better we can use ll/sc instruction.  This
 * implementation is SMP safe ...
 */

/*
 * atomic_add - add integer to atomic variable
 * @i: integer value to add
 * @v: pointer of type atomic_t
 *
 * Atomically adds @i to @v.  Note that the guaranteed useful range
 * of an atomic_t is only 24 bits.
 */
static __inline__ void atomic_add(int i, atomic_t * v)
{
	unsigned long temp;

	__asm__ __volatile__(
	"1:	ll	%0, %1		# atomic_add		\n"
	"	addu	%0, %2					\n"
	"	sc	%0, %1					\n"
	"	beqz	%0, 1b					\n"
	: "=&r" (temp), "=m" (v->counter)
	: "Ir" (i), "m" (v->counter));
}

/*
 * atomic_sub - subtract the atomic variable
 * @i: integer value to subtract
 * @v: pointer of type atomic_t
 *
 * Atomically subtracts @i from @v.  Note that the guaranteed
 * useful range of an atomic_t is only 24 bits.
 */
static __inline__ void atomic_sub(int i, atomic_t * v)
{
	unsigned long temp;

	__asm__ __volatile__(
	"	.set	noreorder	# atomic_sub		\n"
	"1:	ll	%0, %1					\n"
	"	subu	%0, %2					\n"
	"	sc	%0, %1					\n"
	"	beqz	%0, 1b					\n"
	"	.set	reorder					\n"
	: "=&r" (temp), "=m" (v->counter)
	: "Ir" (i), "m" (v->counter));
}

/*
 * Same as above, but return the result value
 */
static __inline__ int atomic_add_return(int i, atomic_t * v)
{
	unsigned long temp, result;

	__asm__ __volatile__(
	"	.set	noreorder	# atomic_add_return	\n"
	"1:	ll	%1, %2					\n"
	"	addu	%0, %1, %3				\n"
	"	sc	%0, %2					\n"
	"	beqz	%0, 1b					\n"
	"	addu	%0, %1, %3				\n"
	"	sync						\n"
	"	.set	reorder					\n"
	: "=&r" (result), "=&r" (temp), "=m" (v->counter)
	: "Ir" (i), "m" (v->counter)
	: "memory");

	return result;
}

static __inline__ int atomic_sub_return(int i, atomic_t * v)
{
	unsigned long temp, result;

	__asm__ __volatile__(
	"	.set	noreorder	# atomic_sub_return	\n"
	"1:	ll	%1, %2					\n"
	"	subu	%0, %1, %3				\n"
	"	sc	%0, %2					\n"
	"	beqz	%0, 1b					\n"
	"	subu	%0, %1, %3				\n"
	"	sync						\n"
	"	.set	reorder					\n"
	: "=&r" (result), "=&r" (temp), "=m" (v->counter)
	: "Ir" (i), "m" (v->counter)
	: "memory");

	return result;
}
#endif

#ifndef CONFIG_CPU_HAS_LLDSCD

/*
 * This implementation is only atomic with respect to interrupts.  It can't
 * be used on SMP
 *
 * atomic64_add - add integer to atomic variable
 * @i: integer value to add
 * @v: pointer of type atomic64_t
 *
 * Atomically adds @i to @v.
 */
static __inline__ void atomic64_add(int i, atomic64_t * v)
{
	unsigned long flags;

	local_irq_save(flags);
	v->counter += i;
	local_irq_restore(flags);
}

/*
 * atomic64_sub - subtract the atomic variable
 * @i: integer value to subtract
 * @v: pointer of type atomic64_t
 *
 * Atomically subtracts @i from @v.
 */
static __inline__ void atomic64_sub(int i, atomic64_t * v)
{
	unsigned long flags;

	local_irq_save(flags);
	v->counter -= i;
	local_irq_restore(flags);
}

static __inline__ int atomic64_add_return(int i, atomic64_t * v)
{
	unsigned long flags;
	int temp;

	local_irq_save(flags);
	temp = v->counter;
	temp += i;
	v->counter = temp;
	local_irq_restore(flags);

	return temp;
}

static __inline__ int atomic64_sub_return(int i, atomic64_t * v)
{
	unsigned long flags;
	int temp;

	local_irq_save(flags);
	temp = v->counter;
	temp -= i;
	v->counter = temp;
	local_irq_restore(flags);

	return temp;
}

#else

/*
 * ... while for MIPS III and better we can use ll/sc instruction.  This
 * implementation is SMP safe ...
 */

/*
 * atomic64_add - add integer to atomic variable
 * @i: integer value to add
 * @v: pointer of type atomic64_t
 *
 * Atomically adds @i to @v.
 */
static __inline__ void atomic64_add(int i, atomic64_t * v)
{
	unsigned long temp;

	__asm__ __volatile__(
	"1:	ll	%0, %1		# atomic64_add		\n"
	"	addu	%0, %2					\n"
	"	sc	%0, %1					\n"
	"	beqz	%0, 1b					\n"
	: "=&r" (temp), "=m" (v->counter)
	: "Ir" (i), "m" (v->counter));
}

/*
 * atomic64_sub - subtract the atomic variable
 * @i: integer value to subtract
 * @v: pointer of type atomic64_t
 *
 * Atomically subtracts @i from @v.
 */
static __inline__ void atomic64_sub(int i, atomic64_t * v)
{
	unsigned long temp;

	__asm__ __volatile__(
	"	.set	noreorder	# atomic64_sub		\n"
	"1:	ll	%0, %1					\n"
	"	subu	%0, %2					\n"
	"	sc	%0, %1					\n"
	"	beqz	%0, 1b					\n"
	"	.set	reorder					\n"
	: "=&r" (temp), "=m" (v->counter)
	: "Ir" (i), "m" (v->counter));
}

/*
 * Same as above, but return the result value
 */
static __inline__ int atomic64_add_return(int i, atomic64_t * v)
{
	unsigned long temp, result;

	__asm__ __volatile__(
	"	.set	noreorder	# atomic64_add_return	\n"
	"1:	ll	%1, %2					\n"
	"	addu	%0, %1, %3				\n"
	"	sc	%0, %2					\n"
	"	beqz	%0, 1b					\n"
	"	addu	%0, %1, %3				\n"
	"	sync						\n"
	"	.set	reorder					\n"
	: "=&r" (result), "=&r" (temp), "=m" (v->counter)
	: "Ir" (i), "m" (v->counter)
	: "memory");

	return result;
}

static __inline__ int atomic64_sub_return(int i, atomic64_t * v)
{
	unsigned long temp, result;

	__asm__ __volatile__(
	"	.set	noreorder	# atomic64_sub_return	\n"
	"1:	ll	%1, %2					\n"
	"	subu	%0, %1, %3				\n"
	"	sc	%0, %2					\n"
	"	beqz	%0, 1b					\n"
	"	subu	%0, %1, %3				\n"
	"	sync						\n"
	"	.set	reorder					\n"
	: "=&r" (result), "=&r" (temp), "=m" (v->counter)
	: "Ir" (i), "m" (v->counter)
	: "memory");

	return result;
}
#endif

#define atomic_dec_return(v) atomic_sub_return(1,(v))
#define atomic64_dec_return(v) atomic64_sub_return(1,(v))
#define atomic_inc_return(v) atomic_add_return(1,(v))
#define atomic64_inc_return(v) atomic64_add_return(1,(v))

/*
 * atomic_sub_and_test - subtract value from variable and test result
 * @i: integer value to subtract
 * @v: pointer of type atomic_t
 *
 * Atomically subtracts @i from @v and returns
 * true if the result is zero, or false for all
 * other cases.  Note that the guaranteed
 * useful range of an atomic_t is only 24 bits.
 */
#define atomic_sub_and_test(i,v) (atomic_sub_return((i), (v)) == 0)

/*
 * atomic64_sub_and_test - subtract value from variable and test result
 * @i: integer value to subtract
 * @v: pointer of type atomic64_t
 *
 * Atomically subtracts @i from @v and returns
 * true if the result is zero, or false for all
 * other cases.
 */
#define atomic64_sub_and_test(i,v) (atomic64_sub_return((i), (v)) == 0)

/*
 * atomic_inc_and_test - increment and test
 * @v: pointer of type atomic_t
 *
 * Atomically increments @v by 1
 * and returns true if the result is zero, or false for all
 * other cases.  Note that the guaranteed
 * useful range of an atomic_t is only 24 bits.
 */
#define atomic_inc_and_test(v) (atomic_inc_return(v) == 0)

/*
 * atomic64_inc_and_test - increment and test
 * @v: pointer of type atomic64_t
 *
 * Atomically increments @v by 1
 * and returns true if the result is zero, or false for all
 * other cases.
 */
#define atomic64_inc_and_test(v) (atomic64_inc_return(v) == 0)

/*
 * atomic_dec_and_test - decrement by 1 and test
 * @v: pointer of type atomic_t
 *
 * Atomically decrements @v by 1 and
 * returns true if the result is 0, or false for all other
 * cases.  Note that the guaranteed
 * useful range of an atomic_t is only 24 bits.
 */
#define atomic_dec_and_test(v) (atomic_sub_return(1, (v)) == 0)

/*
 * atomic64_dec_and_test - decrement by 1 and test
 * @v: pointer of type atomic64_t
 *
 * Atomically decrements @v by 1 and
 * returns true if the result is 0, or false for all other
 * cases.
 */
#define atomic64_dec_and_test(v) (atomic64_sub_return(1, (v)) == 0)

/*
 * atomic_inc - increment atomic variable
 * @v: pointer of type atomic_t
 *
 * Atomically increments @v by 1.  Note that the guaranteed
 * useful range of an atomic_t is only 24 bits.
 */
#define atomic_inc(v) atomic_add(1,(v))

/*
 * atomic64_inc - increment atomic variable
 * @v: pointer of type atomic64_t
 *
 * Atomically increments @v by 1.
 */
#define atomic64_inc(v) atomic64_add(1,(v))

/*
 * atomic_dec - decrement and test
 * @v: pointer of type atomic_t
 *
 * Atomically decrements @v by 1.  Note that the guaranteed
 * useful range of an atomic_t is only 24 bits.
 */
#define atomic_dec(v) atomic_sub(1,(v))

/*
 * atomic64_dec - decrement and test
 * @v: pointer of type atomic64_t
 *
 * Atomically decrements @v by 1.
 */
#define atomic64_dec(v) atomic64_sub(1,(v))

/*
 * atomic_add_negative - add and test if negative
 * @v: pointer of type atomic_t
 * @i: integer value to add
 *
 * Atomically adds @i to @v and returns true
 * if the result is negative, or false when
 * result is greater than or equal to zero.  Note that the guaranteed
 * useful range of an atomic_t is only 24 bits.
 */
#define atomic_add_negative(i,v) (atomic_add_return(i, (v)) < 0)

/*
 * atomic64_add_negative - add and test if negative
 * @v: pointer of type atomic64_t
 * @i: integer value to add
 *
 * Atomically adds @i to @v and returns true
 * if the result is negative, or false when
 * result is greater than or equal to zero.
 */
#define atomic64_add_negative(i,v) (atomic64_add_return(i, (v)) < 0)

/* Atomic operations are already serializing */
#define smp_mb__before_atomic_dec()	smp_mb()
#define smp_mb__after_atomic_dec()	smp_mb()
#define smp_mb__before_atomic_inc()	smp_mb()
#define smp_mb__after_atomic_inc()	smp_mb()

#endif /* defined(__KERNEL__) */

#endif /* _ASM_ATOMIC_H */
