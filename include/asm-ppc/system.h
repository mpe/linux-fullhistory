#ifndef __PPC_SYSTEM_H
#define __PPC_SYSTEM_H

#if 0
#define mb() \
__asm__ __volatile__("mb": : :"memory")
#endif
#define mb()  __asm__ __volatile__ (""   : : :"memory")


extern void __save_flags(long *flags);
extern void __restore_flags(long flags);
extern void sti(void);
extern void cli(void);
extern int _disable_interrupts(void);
extern void _enable_interrupts(int);

/*extern void memcpy(void *, void *, int);*/
extern void bzero(void *, int);

struct task_struct;
extern void switch_to(struct task_struct *prev, struct task_struct *next);

#define save_flags(flags) __save_flags(&(flags))
#define restore_flags(flags) __restore_flags(flags)

#define xchg(ptr,x) ((__typeof__(*(ptr)))__xchg((unsigned long)(x),(ptr),sizeof(*(ptr))))

/* this guy lives in arch/ppc/kernel */
extern inline unsigned long *xchg_u32(void *m, unsigned long val);

/*
 *  these guys don't exist.
 *  someone should create them.
 *              -- Cort
 */
extern void *xchg_u64(void *ptr, unsigned long val);
extern int xchg_u8(char *m, char val);

/*
 * This function doesn't exist, so you'll get a linker error
 * if something tries to do an invalid xchg().
 *
 * This only works if the compiler isn't horribly bad at optimizing.
 * gcc-2.5.8 reportedly can't handle this, but as that doesn't work
 * too well on the alpha anyway..
 */
extern void __xchg_called_with_bad_pointer(void);

static inline unsigned long __xchg(unsigned long x, void * ptr, int size)
{
	switch (size) {
		case 4:
			return (unsigned long )xchg_u32(ptr, x);
		case 8:
			return (unsigned long )xchg_u64(ptr, x);
	}
	__xchg_called_with_bad_pointer();
	return x;


}



extern inline int tas(char * m)
{
	return xchg_u8(m,1);
}

extern inline void * xchg_ptr(void * m, void * val)
{
	return (void *) xchg_u32(m, (unsigned long) val);
}

#endif
