#ifndef _ASMARM_PAGE_H
#define _ASMARM_PAGE_H

#include <asm/arch/mmu.h>
#include <asm/proc/page.h>

#ifdef __KERNEL__

#define get_user_page(vaddr)		__get_free_page(GFP_KERNEL)
#define free_user_page(page, addr)	free_page(addr)
#define clear_page(page)	memzero((void *)(page), PAGE_SIZE)
#define copy_page(to,from)	memcpy((void *)(to), (void *)(from), PAGE_SIZE)

#endif

/* unsigned long __pa(void *x) */
#define __pa(x)			__virt_to_phys((unsigned long)(x))

/* void *__va(unsigned long x) */
#define __va(x)			((void *)(__phys_to_virt((unsigned long)(x))))

#endif
