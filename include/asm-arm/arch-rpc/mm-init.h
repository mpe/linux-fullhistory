/*
 * linux/include/asm-arm/arch-rpc/mmap.h
 *
 * Copyright (C) 1996 Russell King
 */

#define HAVE_MAP_VID_MEM

unsigned long map_screen_mem(unsigned long log_start, unsigned long kmem, int update)
{
	static int updated = 0;
	unsigned long address;
	pgd_t *pgd;

	if (updated)
		return 0;
	updated = update;

	address = SCREEN_START | PMD_TYPE_SECT | PMD_DOMAIN(DOMAIN_KERNEL) | PMD_SECT_AP_WRITE;
	pgd = swapper_pg_dir + (SCREEN2_BASE >> PGDIR_SHIFT);
	pgd_val(pgd[0]) = address;
	pgd_val(pgd[1]) = address + (1 << PGDIR_SHIFT);

	if (update) {
		unsigned long pgtable = PAGE_ALIGN(kmem), *p;
		int i;

		memzero ((void *)pgtable, 4096);
		
		pgd_val(pgd[-2]) = __virt_to_phys(pgtable) | PMD_TYPE_TABLE | PMD_DOMAIN(DOMAIN_KERNEL);
		pgd_val(pgd[-1]) = __virt_to_phys(pgtable + PTRS_PER_PTE*4) | PMD_TYPE_TABLE | PMD_DOMAIN(DOMAIN_KERNEL);
		p = (unsigned long *)pgtable;

		i = PTRS_PER_PTE * 2 - ((SCREEN1_END - log_start) >> PAGE_SHIFT);
		address = SCREEN_START | PTE_TYPE_SMALL | PTE_AP_WRITE;

		while (i < PTRS_PER_PTE * 2) {
			p[i++] = address;
			address += PAGE_SIZE;
		}

		flush_page_to_ram(pgtable);

		kmem = pgtable + PAGE_SIZE;
	}
	return kmem;
}
