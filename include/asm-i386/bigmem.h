/*
 * bigmem.h:	virtual kernel memory mappings for big memory
 *
 * Used in CONFIG_BIGMEM systems for memory pages which	are not
 * addressable by direct kernel virtual adresses.
 *
 * Copyright (C) 1999 Gerhard Wichert, Siemens AG
 *		      Gerhard.Wichert@pdb.siemens.de
 */

#ifndef _ASM_BIGMEM_H
#define _ASM_BIGMEM_H

#include <linux/init.h>

#define BIGMEM_DEBUG /* undef for production */

/* declarations for bigmem.c */
extern unsigned long bigmem_start, bigmem_end;
extern int nr_free_bigpages;

extern pte_t *kmap_pte;
extern pgprot_t kmap_prot;

extern void kmap_init(void) __init;

/* kmap helper functions necessary to access the bigmem pages in kernel */
#include <asm/pgtable.h>
#include <asm/kmap_types.h>

extern inline unsigned long kmap(unsigned long kaddr, enum km_type type)
{
	if (__pa(kaddr) < bigmem_start)
		return kaddr;
	{
		enum fixed_addresses idx = type+KM_TYPE_NR*smp_processor_id();
		unsigned long vaddr = __fix_to_virt(FIX_KMAP_BEGIN+idx);

#ifdef BIGMEM_DEBUG
		if (!pte_none(*(kmap_pte-idx)))
		{
			__label__ here;
		here:
			printk(KERN_ERR "not null pte on CPU %d from %p\n",
			       smp_processor_id(), &&here);
		}
#endif
		set_pte(kmap_pte-idx, mk_pte(kaddr & PAGE_MASK, kmap_prot));
		__flush_tlb_one(vaddr);

		return vaddr | (kaddr & ~PAGE_MASK);
	}
}

extern inline void kunmap(unsigned long vaddr, enum km_type type)
{
#ifdef BIGMEM_DEBUG
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

#endif /* _ASM_BIGMEM_H */
