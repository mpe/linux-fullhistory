#ifndef _ASMARM_PAGE_H
#define _ASMARM_PAGE_H

#include <asm/arch/memory.h>
#include <asm/proc/page.h>

#ifdef __KERNEL__

#ifndef __ASSEMBLY__

#define BUG() do { \
	printk("kernel BUG at %s:%d!\n", __FILE__, __LINE__); \
	*(int *)0 = 0; \
} while (0)

#define PAGE_BUG(page) do { \
	BUG(); \
} while (0)

#endif /* __ASSEMBLY__ */

#define get_user_page(vaddr)		__get_free_page(GFP_KERNEL)
#define free_user_page(page, addr)	free_page(addr)
#define clear_page(page)		memzero((void *)(page), PAGE_SIZE)
#define copy_page(to,from)		memcpy((void *)(to), (void *)(from), PAGE_SIZE)

#endif

/* unsigned long __pa(void *x) */
#define __pa(x)			__virt_to_phys((unsigned long)(x))

/* void *__va(unsigned long x) */
#define __va(x)			((void *)(__phys_to_virt((unsigned long)(x))))

#endif
