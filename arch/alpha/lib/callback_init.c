#include <linux/init.h>
#include <linux/sched.h>

#include <asm/page.h>
#include <asm/hwrpb.h>
#include <asm/console.h>
#include <asm/pgtable.h>

#include "../kernel/proto.h"

extern struct hwrpb_struct *hwrpb;

/* This is the SRM version.  Maybe there will be a DBM version. */

int callback_init_done = 0;

void *  __init callback_init(void * kernel_end)
{
	int i, j;
	unsigned long vaddr = CONSOLE_REMAP_START;
	struct crb_struct * crb;
	pgd_t * pgd = pgd_offset_k(vaddr);
	pmd_t * pmd;
	unsigned long two_pte_pages;

	if (!alpha_using_srm) {
		switch_to_system_map();
		return kernel_end;
	}

	/* Allocate some memory for the pages. */
	two_pte_pages = ((unsigned long)kernel_end + ~PAGE_MASK) & PAGE_MASK;
	kernel_end = (void *)(two_pte_pages + 2*PAGE_SIZE);
	memset((void *)two_pte_pages, 0, 2*PAGE_SIZE);

	/* Starting at the HWRPB, locate the CRB. */
	crb = (struct crb_struct *)((char *)hwrpb + hwrpb->crb_offset);

	/* Tell the console whither the console is to be remapped. */
	if (srm_fixup(vaddr, (unsigned long)hwrpb))
		__halt();		/* "We're boned."  --Bender */

	/* Edit the procedure descriptors for DISPATCH and FIXUP. */
	crb->dispatch_va = (struct procdesc_struct *)
		(vaddr + (unsigned long)crb->dispatch_va - crb->map[0].va);
	crb->fixup_va = (struct procdesc_struct *)
		(vaddr + (unsigned long)crb->fixup_va - crb->map[0].va);

	switch_to_system_map();

	/*
	 * Set up the first and second level PTEs for console callbacks.
	 * There is an assumption here that only one of each is needed,
	 * and this allows for 8MB.  Currently (late 1999), big consoles
	 * are still under 4MB.
	 */
	pgd_set(pgd, (pmd_t *)two_pte_pages);
	pmd = pmd_offset(pgd, vaddr);
	pmd_set(pmd, (pte_t *)(two_pte_pages + PAGE_SIZE));

	/*
	 * Set up the third level PTEs and update the virtual addresses
	 * of the CRB entries.
	 */
	for (i = 0; i < crb->map_entries; ++i) {
		unsigned long paddr = crb->map[i].pa;
		crb->map[i].va = vaddr;
		for (j = 0; j < crb->map[i].count; ++j) {
			set_pte(pte_offset(pmd, vaddr),
				mk_pte_phys(paddr, PAGE_KERNEL));
			paddr += PAGE_SIZE;
			vaddr += PAGE_SIZE;
		}
	}

	callback_init_done = 1;
	return kernel_end;
}

