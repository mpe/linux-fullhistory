/*
 * ELF definitions for 26-bit CPUs
 */

#define ELF_EXEC_PAGESIZE	32768

#if 0		/* not yet */
#define ELF_PROC_OK(x)		\
	((x)->e_flags & EF_ARM_APCS26)
#else
#define ELF_PROC_OK(x)		(1)
#endif

#ifdef __KERNEL__

#define SET_PERSONALITY(ex,ibcs2) \
	current->personality = PER_LINUX

#endif
