/*
 * arch/arm/mm/mm-arc.c
 *
 * Extra MM routines for the Archimedes architecture
 *
 * Copyright (C) 1998 Russell King
 */
#include <linux/init.h>
#include <asm/hardware.h>
#include <asm/pgtable.h>

unsigned long phys_screen_end;

/*
 * This routine needs more work to make it dynamically release/allocate mem!
 */
__initfunc(unsigned long map_screen_mem(unsigned long log_start, unsigned long kmem, int update))
{
	static int updated = 0;

	if (updated)
		return 0;

	updated = update;

	if (update) {
		unsigned long address = log_start, offset;
		pgd_t *pgdp;

		kmem = (kmem + 3) & ~3;

		pgdp = pgd_offset (&init_mm, address);				/* +31 */
		offset = SCREEN_START;
		while (address < SCREEN1_END) {
			unsigned long addr_pmd, end_pmd;
			pmd_t *pmdp;

			/* if (pgd_none (*pgdp)) alloc pmd */
			pmdp = pmd_offset (pgdp, address);			/* +0 */
			addr_pmd = address & ~PGDIR_MASK;			/* 088000 */
			end_pmd = addr_pmd + SCREEN1_END - address;		/* 100000 */
			if (end_pmd > PGDIR_SIZE)
				end_pmd = PGDIR_SIZE;

			do {
				unsigned long addr_pte, end_pte;
				pte_t *ptep;

				if (pmd_none (*pmdp)) {
					pte_t *new_pte = (pte_t *)kmem;
					kmem += PTRS_PER_PTE * BYTES_PER_PTR;
					memzero (new_pte, PTRS_PER_PTE * BYTES_PER_PTR);
					set_pmd (pmdp, mk_pmd(new_pte));
				}

				ptep = pte_offset (pmdp, addr_pmd);		/* +11 */
				addr_pte = addr_pmd & ~PMD_MASK;		/* 088000 */
				end_pte = addr_pte + end_pmd - addr_pmd;	/* 100000 */
				if (end_pte > PMD_SIZE)
					end_pte = PMD_SIZE;

				do {
					set_pte (ptep, mk_pte(offset, PAGE_KERNEL));
					addr_pte += PAGE_SIZE;
					offset += PAGE_SIZE;
					ptep++;
				} while (addr_pte < end_pte);

				pmdp++;
				addr_pmd = (addr_pmd + PMD_SIZE) & PMD_MASK;
			} while (addr_pmd < end_pmd);

			address = (address + PGDIR_SIZE) & PGDIR_MASK;
			pgdp ++;
		}

		phys_screen_end = offset;
		flush_tlb_all ();
		update_memc_all ();
	}
	return kmem;
}
