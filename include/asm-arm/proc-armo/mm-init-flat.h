/*
 * linux/include/asm-arm/proc-armo/mmap.h
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

/*
 * This routine needs more work to make it dynamically release/allocate mem!
 */
unsigned long map_screen_mem(unsigned long log_start, unsigned long kmem, int update)
{
	static int updated = 0;
	unsigned long address = SCREEN_START, i;
	pgd_t *pg_dir;
	pmd_t *pm_dir;
	pte_t *pt_entry;

	if (updated)
		return 0;
	updated = update;

	pg_dir = swapper_pg_dir + (SCREEN1_BASE >> PGDIR_SHIFT);
	pm_dir = pmd_offset(pg_dir, SCREEN1_BASE);
	pt_entry = pte_offset(pm_dir, SCREEN1_BASE);

	for (i = SCREEN1_BASE; i < SCREEN1_END; i += PAGE_SIZE) {
		if (i >= log_start) {
			*pt_entry = mk_pte(address, __pgprot(_PAGE_PRESENT));
			address += PAGE_SIZE;
		} else
			*pt_entry = mk_pte(0, __pgprot(0));
		pt_entry++;
	}
	phys_screen_end = address;
	if (update)
		flush_tlb_all ();
	return kmem;
}

static inline unsigned long setup_pagetables(unsigned long start_mem, unsigned long end_mem)
{
	unsigned long address;
	unsigned int spi;

	page_nr = MAP_NR(end_mem);

	/* Allocate zero page */
	address = PAGE_OFFSET + 480*1024;
	for (spi = 0; spi < 32768 >> PAGE_SHIFT; spi++) {
		pgd_val(swapper_pg_dir[spi]) = pte_val(mk_pte(address, PAGE_READONLY));
		address += PAGE_SIZE;
	}

	while (spi < (PAGE_OFFSET >> PGDIR_SHIFT))
		pgd_val(swapper_pg_dir[spi++]) = 0;

	map_screen_mem (SCREEN1_END - 480*1024, 0, 0);
	return start_mem;
}

static inline void mark_usable_memory_areas(unsigned long *start_mem, unsigned long end_mem)
{
	unsigned long smem = PAGE_ALIGN(*start_mem);

	while (smem < end_mem) {
		clear_bit(PG_reserved, &mem_map[MAP_NR(smem)].flags);
		smem += PAGE_SIZE;
	}

	for (smem = phys_screen_end; smem < SCREEN2_END; smem += PAGE_SIZE)
		clear_bit(PG_reserved, &mem_map[MAP_NR(smem)].flags);
}
