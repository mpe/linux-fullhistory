/*
 * linux/include/asm-arm/proc-armv/mm-init.h
 *
 * Copyright (C) 1996 Russell King
 *
 * This contains the code to setup the memory map on an ARM v3 or v4 machine.
 * This is both processor & architecture specific, and requires some
 * more work to get it to fit into our separate processor and architecture
 * structure.
 */

/*
 * On ebsa, we want the memory map set up so:
 *
 *   PHYS	  VIRT
 * 00000000	00000000	Zero page
 * 000003ff	000003ff	Zero page end
 * 00000000	c0000000	Kernel and all physical memory
 * 01ffffff	c1ffffff	End of physical (32MB)
 * e0000000	e0000000	IO start
 * ffffffff	ffffffff	IO end
 *
 * On rpc, we want:
 *
 *   PHYS	  VIRT
 * 10000000	00000000	Zero page
 * 100003ff	000003ff	Zero page end
 * 10000000	c0000000	Kernel and all physical memory
 * 1fffffff	cfffffff	End of physical (32MB)
 * 02000000	d?000000	Screen memory (first image)
 * 02000000	d8000000	Screen memory (second image)
 * 00000000	df000000	StrongARM cache invalidation area
 * 03000000	e0000000	IO start
 * 03ffffff	e0ffffff	IO end
 *
 * We set it up using the section page table entries.
 */

#include <asm/arch/mmap.h>
#include <asm/pgtable.h>
 
#define V2P(x)	virt_to_phys(x)
#define PTE_SIZE (PTRS_PER_PTE * 4)

#define PMD_SECT	(PMD_TYPE_SECT | PMD_DOMAIN(DOMAIN_KERNEL) | PMD_SECT_CACHEABLE)

static inline void setup_swapper_dir (int index, unsigned long entry)
{
	pmd_t pmd;

	pmd_val(pmd) = entry;
	set_pmd (pmd_offset (swapper_pg_dir + index, 0), pmd);
}

static inline unsigned long setup_pagetables(unsigned long start_mem, unsigned long end_mem)
{
	unsigned long address;
	unsigned int spi;
	union { unsigned long l; unsigned long *p; } u;

	/* map in zero page */
	u.l = ((start_mem + (PTE_SIZE-1)) & ~(PTE_SIZE-1));
	start_mem = u.l + PTE_SIZE;
	memzero (u.p, PTE_SIZE);
	*u.p = V2P(PAGE_OFFSET) | PTE_CACHEABLE | PTE_TYPE_SMALL;
	setup_swapper_dir (0, V2P(u.l) | PMD_TYPE_TABLE | PMD_DOMAIN(DOMAIN_USER));

	for (spi = 1; spi < (PAGE_OFFSET >> PGDIR_SHIFT); spi++)
		pgd_val(swapper_pg_dir[spi]) = 0;

	/* map in physical ram & kernel */
	address = PAGE_OFFSET;
	while (spi < end_mem >> PGDIR_SHIFT) {
		setup_swapper_dir (spi++,
				V2P(address) | PMD_SECT |
				PMD_SECT_BUFFERABLE | PMD_SECT_AP_WRITE);
		address += PGDIR_SIZE;
	}
	while (spi < PTRS_PER_PGD)
		pgd_val(swapper_pg_dir[spi++]) = 0;

	/*
	 * An area to invalidate the cache
	 */
	setup_swapper_dir (0xdf0, SAFE_ADDR | PMD_SECT | PMD_SECT_AP_READ);

	/* map in IO */
	address = IO_START;
	spi = IO_BASE >> PGDIR_SHIFT;
	pgd_val(swapper_pg_dir[spi-1]) = 0xc0000000 | PMD_TYPE_SECT |
					 PMD_DOMAIN(DOMAIN_KERNEL) | PMD_SECT_AP_WRITE;
	while (address < IO_START + IO_SIZE && address) {
		pgd_val(swapper_pg_dir[spi++]) = address |
						PMD_TYPE_SECT | PMD_DOMAIN(DOMAIN_IO) |
						PMD_SECT_AP_WRITE;
		address += PGDIR_SIZE;
	}

#ifdef HAVE_MAP_VID_MEM
	map_screen_mem(0, 0, 0);
#endif

	flush_cache_all();
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
}	

