#ifndef _M68K_SEGMENT_H
#define _M68K_SEGMENT_H

/* define constants */
/* Address spaces (FC0-FC2) */
#define USER_DATA     (1)
#ifndef USER_DS
#define USER_DS       (USER_DATA)
#endif
#define USER_PROGRAM  (2)
#define SUPER_DATA    (5)
#ifndef KERNEL_DS
#define KERNEL_DS     (SUPER_DATA)
#endif
#define SUPER_PROGRAM (6)
#define CPU_SPACE     (7)

#ifndef __ASSEMBLY__

/*
 * Get/set the SFC/DFC registers for MOVES instructions
 */

static inline unsigned long get_fs(void)
{
	unsigned long _v;
	__asm__ ("movec %/dfc,%0":"=r" (_v):);

	return _v;
}

static inline unsigned long get_ds(void)
{
    /* return the supervisor data space code */
    return KERNEL_DS;
}

static inline void set_fs(unsigned long val)
{
	__asm__ __volatile__ ("movec %0,%/sfc\n\t"
			      "movec %0,%/dfc\n\t"
			      : /* no outputs */ : "r" (val) : "memory");
}

#endif /* __ASSEMBLY__ */

#endif /* _M68K_SEGMENT_H */
