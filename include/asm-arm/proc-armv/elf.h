/*
 * ELF definitions for 32-bit CPUs
 */

#define ELF_EXEC_PAGESIZE	4096

/* We can execute both 32-bit and 26-bit code. */
#define ELF_PROC_OK(x)		(1)

#ifdef __KERNEL__

#if 0		/* not yet */
#define SET_PERSONALITY(ex,ibcs2) \
	current_personality = (ex->e_flags & EF_ARM_APCS26) ? \
	PER_LINUX : PER_LINUX_32BIT
#else
#define SET_PERSONALITY(ex,ibcs2) \
	current->personality = PER_LINUX_32BIT
#endif

#endif
