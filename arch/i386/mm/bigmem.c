/*
 * BIGMEM IA32 code and variables.
 *
 * (C) 1999 Andrea Arcangeli, SuSE GmbH, andrea@suse.de
 *          Gerhard Wichert, Siemens AG, Gerhard.Wichert@pdb.siemens.de
 */

#include <linux/mm.h>
#include <linux/bigmem.h>

unsigned long bigmem_start, bigmem_end;

/* NOTE: fixmap_init alloc all the fixmap pagetables contigous on the
   physical space so we can cache the place of the first one and move
   around without checking the pgd every time. */
pte_t *kmap_pte;
pgprot_t kmap_prot;

#define kmap_get_fixmap_pte(vaddr)					\
	pte_offset(pmd_offset(pgd_offset_k(vaddr), (vaddr)), (vaddr))

void __init kmap_init(void)
{
	unsigned long kmap_vstart;

	/* cache the first kmap pte */
	kmap_vstart = __fix_to_virt(FIX_KMAP_BEGIN);
	kmap_pte = kmap_get_fixmap_pte(kmap_vstart);

	kmap_prot = PAGE_KERNEL;
	if (boot_cpu_data.x86_capability & X86_FEATURE_PGE)
		pgprot_val(kmap_prot) |= _PAGE_GLOBAL;
}
