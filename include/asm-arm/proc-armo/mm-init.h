/*
 * linux/include/asm-arm/proc-armo/mm-init.h
 *
 * Copyright (C) 1996 Russell King
 *
 * This contains the code to setup the memory map on an ARM2/ARM250/ARM3
 * machine. This is both processor & architecture specific, and requires
 * some more work to get it to fit into our separate processor and
 * architecture structure.
 */

static unsigned long phys_screen_end;
int page_nr;

#define setup_processor_functions()
#define PTE_SIZE	(PTRS_PER_PTE * BYTES_PER_PTR)

static inline void setup_swapper_dir (int index, pte_t *ptep)
{
	set_pmd (pmd_offset (swapper_pg_dir + index, 0), mk_pmd (ptep));
}

/*
 * This routine needs more work to make it dynamically release/allocate mem!
 */
unsigned long map_screen_mem(unsigned long log_start, unsigned long kmem, int update)
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
		update_mm_cache_all ();
	}
	return kmem;
}

static inline unsigned long setup_pagetables(unsigned long start_mem, unsigned long end_mem)
{
	unsigned int i;
	union {unsigned long l; pte_t *pte; } u;

	page_nr = MAP_NR(end_mem);

	/* map in pages for (0x0000 - 0x8000) */
	u.l = ((start_mem + (PTE_SIZE-1)) & ~(PTE_SIZE-1));
	start_mem = u.l + PTE_SIZE;
	memzero (u.pte, PTE_SIZE);
	u.pte[0] = mk_pte(PAGE_OFFSET + 491520, PAGE_READONLY);
	setup_swapper_dir (0, u.pte);

	for (i = 1; i < PTRS_PER_PGD; i++)
		pgd_val(swapper_pg_dir[i]) = 0;

	/* now map screen mem in */
	phys_screen_end = SCREEN2_END;
	map_screen_mem (SCREEN1_END - 480*1024, 0, 0);

	return start_mem;
}

static inline void mark_usable_memory_areas(unsigned long *start_mem, unsigned long end_mem)
{
	unsigned long smem;

	*start_mem = smem = PAGE_ALIGN(*start_mem);

	while (smem < end_mem) {
		clear_bit(PG_reserved, &mem_map[MAP_NR(smem)].flags);
		smem += PAGE_SIZE;
	}

	for (smem = phys_screen_end; smem < SCREEN2_END; smem += PAGE_SIZE)
		clear_bit(PG_reserved, &mem_map[MAP_NR(smem)].flags);
}
