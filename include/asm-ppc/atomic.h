/*
 * PowerPC atomic operations
 */

#ifndef _ASM_PPC_ATOMIC_H_ 
#define _ASM_PPC_ATOMIC_H_

typedef struct { int counter; } atomic_t;
#define ATOMIC_INIT	{ 0 }

/*
 * Make sure gcc doesn't try to be clever and move things around
 * on us. We need to use _exactly_ the address the user gave us,
 * not some alias that contains the same information.
 */
#define __atomic_fool_gcc(x) (*(struct { int a[100]; } *)x)

#define atomic_read(v)		((v)->counter)
#define atomic_set(v)		(((v)->counter) = i)

#define atomic_dec_return(v) ({atomic_sub(1,(v));(v);})
#define atomic_inc_return(v) ({atomic_add(1,(v));(v);})

#define atomic_inc(v) atomic_add(1,(v))
#define atomic_dec(v) atomic_sub(1,(v))
#endif

