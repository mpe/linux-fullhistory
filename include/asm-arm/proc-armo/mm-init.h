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
extern unsigned long phys_screen_end;
extern unsigned long map_screen_mem(unsigned long log_start, unsigned long kmem, int update);
int page_nr;

#define setup_processor_functions()
#define PTE_SIZE	(PTRS_PER_PTE * BYTES_PER_PTR)

static inline void setup_swapper_dir (int index, pte_t *ptep)
{
	set_pmd (pmd_offset (swapper_pg_dir + index, 0), mk_pmd (ptep));
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
