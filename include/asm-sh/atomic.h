#ifndef __ASM_SH_ATOMIC_H
#define __ASM_SH_ATOMIC_H

/*
 * Atomic operations that C can't guarantee us.  Useful for
 * resource counting etc..
 *
 */

#include <linux/config.h>

#ifdef CONFIG_SMP
typedef struct { volatile int counter; } atomic_t;
#else
typedef struct { int counter; } atomic_t;
#endif

#define ATOMIC_INIT(i)	( (atomic_t) { (i) } )

#define atomic_read(v)		((v)->counter)
#define atomic_set(v,i)		((v)->counter = (i))

#include <asm/system.h>

/*
 * Make sure gcc doesn't try to be clever and move things around
 * on us. We need to use _exactly_ the address the user gave us,
 * not some alias that contains the same information.
 */
#define __atomic_fool_gcc(x) (*(volatile struct { int a[100]; } *)x)

/*
 * To get proper branch prediction for the main line, we must branch
 * forward to code at the end of this object's .text section, then
 * branch back to restart the operation.
 */

extern __inline__ void atomic_add(int i, atomic_t * v)
{
	unsigned long flags;

	save_and_cli(flags);
	*(long *)v += i;
	restore_flags(flags);
}

extern __inline__ void atomic_sub(int i, atomic_t *v)
{
	unsigned long flags;

	save_and_cli(flags);
	*(long *)v -= i;
	restore_flags(flags);
}

extern __inline__ int atomic_add_return(int i, atomic_t * v)
{
	unsigned long temp, flags;

	save_and_cli(flags);
	temp = *(long *)v;
	temp += i;
	*(long *)v = temp;
	restore_flags(flags);

	return temp;
}

extern __inline__ int atomic_sub_return(int i, atomic_t * v)
{
	unsigned long temp, flags;

	save_and_cli(flags);
	temp = *(long *)v;
	temp -= i;
	*(long *)v = temp;
	restore_flags(flags);

	return temp;
}

#define atomic_dec_return(v) atomic_sub_return(1,(v))
#define atomic_inc_return(v) atomic_add_return(1,(v))

#define atomic_sub_and_test(i,v) (atomic_sub_return((i), (v)) == 0)
#define atomic_dec_and_test(v) (atomic_sub_return(1, (v)) == 0)

#define atomic_inc(v) atomic_add(1,(v))
#define atomic_dec(v) atomic_sub(1,(v))

extern __inline__ void atomic_clear_mask(unsigned int mask, atomic_t *v)
{
	unsigned long flags;

	save_and_cli(flags);
	*(long *)v &= ~mask;
	restore_flags(flags);
}

extern __inline__ void atomic_set_mask(unsigned int mask, atomic_t *v)
{
	unsigned long flags;

	save_and_cli(flags);
	*(long *)v |= mask;
	restore_flags(flags);
}

#endif /* __ASM_SH_ATOMIC_H */
