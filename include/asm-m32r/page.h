#ifndef _ASM_M32R_PAGE_H
#define _ASM_M32R_PAGE_H

#include <linux/config.h>

/* PAGE_SHIFT determines the page size */
#define PAGE_SHIFT	12
#define PAGE_SIZE	(1UL << PAGE_SHIFT)
#define PAGE_MASK	(~(PAGE_SIZE-1))

#ifdef __KERNEL__
#ifndef __ASSEMBLY__

extern void clear_page(void *to);
extern void copy_page(void *to, void *from);

#define clear_user_page(page, vaddr, pg)	clear_page(page)
#define copy_user_page(to, from, vaddr, pg)	copy_page(to, from)

/*
 * These are used to make use of C type-checking..
 */
typedef struct { unsigned long pte; } pte_t;
typedef struct { unsigned long pmd; } pmd_t;
typedef struct { unsigned long pgd; } pgd_t;
#define pte_val(x)	((x).pte)
#define PTE_MASK	PAGE_MASK

typedef struct { unsigned long pgprot; } pgprot_t;

#define pmd_val(x)	((x).pmd)
#define pgd_val(x)	((x).pgd)
#define pgprot_val(x)	((x).pgprot)

#define __pte(x) ((pte_t) { (x) } )
#define __pmd(x) ((pmd_t) { (x) } )
#define __pgd(x) ((pgd_t) { (x) } )
#define __pgprot(x)	((pgprot_t) { (x) } )

#endif /* !__ASSEMBLY__ */

/* to align the pointer to the (next) page boundary */
#define PAGE_ALIGN(addr)	(((addr) + PAGE_SIZE - 1) & PAGE_MASK)

/*
 * This handles the memory map.. We could make this a config
 * option, but too many people screw it up, and too few need
 * it.
 *
 * A __PAGE_OFFSET of 0xC0000000 means that the kernel has
 * a virtual address space of one gigabyte, which limits the
 * amount of physical memory you can use to about 950MB.
 *
 * If you want more physical memory than this then see the CONFIG_HIGHMEM4G
 * and CONFIG_HIGHMEM64G options in the kernel configuration.
 */


/* This handles the memory map.. */

#ifndef __ASSEMBLY__

/* Pure 2^n version of get_order */
static __inline__ int get_order(unsigned long size)
{
	int order;

	size = (size - 1) >> (PAGE_SHIFT - 1);
	order = -1;
	do {
		size >>= 1;
		order++;
	} while (size);

	return order;
}

#endif /* __ASSEMBLY__ */

#define __MEMORY_START  CONFIG_MEMORY_START
#define __MEMORY_SIZE   CONFIG_MEMORY_SIZE

#ifdef CONFIG_MMU
#define __PAGE_OFFSET  (0x80000000)
#else
#define __PAGE_OFFSET  (0x00000000)
#endif

#define PAGE_OFFSET		((unsigned long)__PAGE_OFFSET)
#define __pa(x)			((unsigned long)(x) - PAGE_OFFSET)
#define __va(x)			((void *)((unsigned long)(x) + PAGE_OFFSET))

#ifndef CONFIG_DISCONTIGMEM
#define PFN_BASE		(CONFIG_MEMORY_START >> PAGE_SHIFT)
#define pfn_to_page(pfn)	(mem_map + ((pfn) - PFN_BASE))
#define page_to_pfn(page)	\
	((unsigned long)((page) - mem_map) + PFN_BASE)
#define pfn_valid(pfn)		(((pfn) - PFN_BASE) < max_mapnr)
#endif  /* !CONFIG_DISCONTIGMEM */

#define virt_to_page(kaddr)	pfn_to_page(__pa(kaddr) >> PAGE_SHIFT)
#define virt_addr_valid(kaddr)	pfn_valid(__pa(kaddr) >> PAGE_SHIFT)

#define VM_DATA_DEFAULT_FLAGS	(VM_READ | VM_WRITE | VM_EXEC | \
				 VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC )

#define devmem_is_allowed(x) 1

#endif /* __KERNEL__ */

#endif /* _ASM_M32R_PAGE_H */

