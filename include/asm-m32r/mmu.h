#ifndef _ASM_M32R_MMU_H
#define _ASM_M32R_MMU_H

#include <linux/config.h>

#if !defined(CONFIG_MMU)
typedef struct {
	struct vm_list_struct	*vmlist;
	unsigned long		end_brk;
} mm_context_t;
#else

/* Default "unsigned long" context */
#ifndef CONFIG_SMP
typedef unsigned long mm_context_t;
#else
typedef unsigned long mm_context_t[NR_CPUS];
#endif

#endif  /* CONFIG_MMU */
#endif  /* _ASM_M32R_MMU_H */
