/*
 * highmem.h: virtual kernel memory mappings for high memory
 *
 * Used in CONFIG_HIGHMEM systems for memory pages which
 * are not addressable by direct kernel virtual adresses.
 *
 * Copyright (C) 1999 Gerhard Wichert, Siemens AG
 *		      Gerhard.Wichert@pdb.siemens.de
 *
 *
 * Redesigned the x86 32-bit VM architecture to deal with 
 * up to 16 Terrabyte physical memory. With current x86 CPUs
 * we now support up to 64 Gigabytes physical RAM.
 *
 * Copyright (C) 1999 Ingo Molnar <mingo@redhat.com>
 */

#ifndef _ASM_HIGHMEM_H
#define _ASM_HIGHMEM_H

#include <linux/init.h>

/* undef for production */
#define HIGHMEM_DEBUG 1

/* declarations for highmem.c */
extern unsigned long highstart_pfn, highend_pfn;

extern pte_t *kmap_pte;
extern pgprot_t kmap_prot;

extern void kmap_init(void) __init;

/* kmap helper functions necessary to access the highmem pages in kernel */
#include <asm/pgtable.h>
#include <asm/kmap_types.h>

extern inline unsigned long kmap(struct page *page, enum km_type type)
{
	if (page < highmem_start_page)
		return page_address(page);
	{
		enum fixed_addresses idx = type+KM_TYPE_NR*smp_processor_id();
		unsigned long vaddr = __fix_to_virt(FIX_KMAP_BEGIN+idx);

#if HIGHMEM_DEBUG
		if (!pte_none(*(kmap_pte-idx)))
		{
			__label__ here;
		here:
			printk(KERN_ERR "not null pte on CPU %d from %p\n",
			       smp_processor_id(), &&here);
		}
#endif
		set_pte(kmap_pte-idx, mk_pte(page, kmap_prot));
		__flush_tlb_one(vaddr);

		return vaddr;
	}
}

extern inline void kunmap(unsigned long vaddr, enum km_type type)
{
#if HIGHMEM_DEBUG
	enum fixed_addresses idx = type+KM_TYPE_NR*smp_processor_id();
	if ((vaddr & PAGE_MASK) == __fix_to_virt(FIX_KMAP_BEGIN+idx))
	{
		/* force other mappings to Oops if they'll try to access
		   this pte without first remap it */
		pte_clear(kmap_pte-idx);
		__flush_tlb_one(vaddr);
	}
#endif
}

extern inline void kmap_check(void)
{
#if HIGHMEM_DEBUG
	int idx_base = KM_TYPE_NR*smp_processor_id(), i;
	for (i = idx_base; i < idx_base+KM_TYPE_NR; i++)
		if (!pte_none(*(kmap_pte-i)))
			BUG();
#endif
}
#endif /* _ASM_HIGHMEM_H */
