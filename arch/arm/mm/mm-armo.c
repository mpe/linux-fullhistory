/*
 * arch/arm/mm/mm-armo.c
 *
 * Page table sludge for older ARM processor architectures.
 *
 * Copyright (C) 1998-1999 Russell King
 */
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/init.h>

#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/arch/memory.h>

#include "map.h"

#define MEMC_TABLE_SIZE (256*sizeof(unsigned long))
#define PGD_TABLE_SIZE	(PTRS_PER_PGD * BYTES_PER_PTR)

/*
 * FIXME: the following over-allocates by 6400%
 */
static inline void *alloc_table(int size, int prio)
{
	if (size != 128)
		printk("invalid table size\n");
	return (void *)get_page_8k(prio);
}

/*
 * Allocate a page table.  Note that we place the MEMC
 * table before the page directory.  This means we can
 * easily get to both tightly-associated data structures
 * with a single pointer.  This function is slightly
 * better - it over-allocates by only 711%
 */
static inline void *alloc_pgd_table(int priority)
{
	unsigned long pg8k;

	pg8k = get_page_8k(priority);
	if (pg8k)
		pg8k += MEMC_TABLE_SIZE;

	return (void *)pg8k;
}

void free_table(void *table)
{
	unsigned long tbl = (unsigned long)table;

	tbl &= ~8191;
	free_page_8k(tbl);
}

pgd_t *get_pgd_slow(void)
{
	pgd_t *pgd = (pgd_t *)alloc_pgd_table(GFP_KERNEL);
	pmd_t *new_pmd;

	if (pgd) {
		pgd_t *init = pgd_offset(&init_mm, 0);
		
		memzero(pgd, USER_PTRS_PER_PGD * BYTES_PER_PTR);
		memcpy(pgd + USER_PTRS_PER_PGD, init + USER_PTRS_PER_PGD,
			(PTRS_PER_PGD - USER_PTRS_PER_PGD) * BYTES_PER_PTR);

		/*
		 * On ARM, first page must always be allocated
		 */
		if (!pmd_alloc(pgd, 0))
			goto nomem;
		else {
			pmd_t *old_pmd = pmd_offset(init, 0);
			new_pmd = pmd_offset(pgd, 0);

			if (!pte_alloc(new_pmd, 0))
				goto nomem_pmd;
			else {
				pte_t *new_pte = pte_offset(new_pmd, 0);
				pte_t *old_pte = pte_offset(old_pmd, 0);

				set_pte (new_pte, *old_pte);
			}
		}
		/* update MEMC tables */
		cpu_memc_update_all(pgd);
	}
	return pgd;

nomem_pmd:
	pmd_free(new_pmd);
nomem:
	free_table(pgd);
	return NULL;
}

pte_t *get_pte_slow(pmd_t *pmd, unsigned long offset)
{
	pte_t *pte;

	pte = (pte_t *)alloc_table(PTRS_PER_PTE * BYTES_PER_PTR, GFP_KERNEL);
	if (pmd_none(*pmd)) {
		if (pte) {
			memzero(pte, PTRS_PER_PTE * BYTES_PER_PTR);
			set_pmd(pmd, mk_pmd(pte));
			return pte + offset;
		}
		set_pmd(pmd, mk_pmd(BAD_PAGETABLE));
		return NULL;
	}
	free_table((void *)pte);
	if (pmd_bad(*pmd)) {
		__bad_pmd(pmd);
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + offset;
}

/*
 * This contains the code to setup the memory map on an ARM2/ARM250/ARM3
 * machine. This is both processor & architecture specific, and requires
 * some more work to get it to fit into our separate processor and
 * architecture structure.
 */
int page_nr;

#define PTE_SIZE	(PTRS_PER_PTE * BYTES_PER_PTR)

static inline void setup_swapper_dir (int index, pte_t *ptep)
{
	set_pmd (pmd_offset (swapper_pg_dir + index, 0), mk_pmd (ptep));
}

unsigned long __init
setup_page_tables(unsigned long start_mem, unsigned long end_mem)
{
	unsigned int i;
	union { unsigned long l; pte_t *pte; } u;

	page_nr = MAP_NR(end_mem);

	/* map in pages for (0x0000 - 0x8000) */
	u.l = ((start_mem + (PTE_SIZE-1)) & ~(PTE_SIZE-1));
	start_mem = u.l + PTE_SIZE;
	memzero (u.pte, PTE_SIZE);
	u.pte[0] = mk_pte(PAGE_OFFSET + 491520, PAGE_READONLY);
	setup_swapper_dir (0, u.pte);

	for (i = 1; i < PTRS_PER_PGD; i++)
		pgd_val(swapper_pg_dir[i]) = 0;

	return start_mem;
}

unsigned long __init
create_mem_holes(unsigned long start, unsigned long end)
{
	return start;
}

void __init
mark_usable_memory_areas(unsigned long start_mem, unsigned long end_mem)
{
	while (start_mem < end_mem) {
		clear_bit(PG_reserved, &mem_map[MAP_NR(start_mem)].flags);
		start_mem += PAGE_SIZE;
	}
}
