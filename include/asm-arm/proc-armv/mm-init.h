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
#include <asm/pgtable.h>
 
#define PTE_SIZE (PTRS_PER_PTE * BYTES_PER_PTR)

extern unsigned long setup_io_pagetables(unsigned long start_mem);

/*
 * Add a SECTION mapping between VIRT and PHYS in domain DOMAIN with protection PROT
 */
static inline void
alloc_init_section(unsigned long *mem, unsigned long virt, unsigned long phys, int domain, int prot)
{
	pgd_t *pgdp;
	pmd_t *pmdp, pmd;

	pgdp = pgd_offset_k(virt);
	pmdp = pmd_offset(pgdp, virt);

	pmd_val(pmd) = phys | PMD_TYPE_SECT | PMD_DOMAIN(domain) | prot;
	set_pmd(pmdp, pmd);
}

/*
 * Clear any mapping
 */
static inline void
free_init_section(unsigned long virt)
{
	pgd_t *pgdp;
	pmd_t *pmdp;

	pgdp = pgd_offset_k(virt);
	pmdp = pmd_offset(pgdp, virt);

	pmd_clear(pmdp);
}

/*
 * Add a PAGE mapping between VIRT and PHYS in domain DOMAIN with protection PROT
 */
static inline void
alloc_init_page(unsigned long *mem, unsigned long virt, unsigned long phys, int domain, int prot)
{
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;

	pgdp = pgd_offset_k(virt);
	pmdp = pmd_offset(pgdp, virt);

	if (pmd_none(*pmdp)) {
		unsigned long memory = *mem;

		memory = (memory + PTE_SIZE - 1) & ~(PTE_SIZE - 1);

		ptep = (pte_t *)memory;
		memzero(ptep, PTE_SIZE);
		memory += PTE_SIZE;

		ptep = (pte_t *)memory;
		memzero(ptep, PTE_SIZE);

		set_pmd(pmdp, __mk_pmd(ptep, PMD_TYPE_TABLE | PMD_DOMAIN(domain)));

		*mem = memory + PTE_SIZE;
	}

	ptep = pte_offset(pmdp, virt);

	set_pte(ptep, mk_pte_phys(phys, __pgprot(prot)));
}

static inline unsigned long
setup_pagetables(unsigned long start_mem, unsigned long end_mem)
{
	unsigned long address = 0;

	do {
		if (address >= PAGE_OFFSET && address < end_mem)
			/*
			 * map in physical ram & kernel
			 */
			alloc_init_section(&start_mem, address, __virt_to_phys(address), DOMAIN_KERNEL,
					   PMD_SECT_CACHEABLE | PMD_SECT_BUFFERABLE | PMD_SECT_AP_WRITE);
		else
			/*
			 * unmap everything else
			 */
			free_init_section(address);

		address += PGDIR_SIZE;
	} while (address != 0);

	/*
	 * An area to invalidate the cache
	 */
	alloc_init_section(&start_mem, FLUSH_BASE, FLUSH_BASE_PHYS, DOMAIN_KERNEL,
			   PMD_SECT_CACHEABLE | PMD_SECT_AP_READ);

	/*
	 * Now set up our IO mappings
	 */
	start_mem = setup_io_pagetables(start_mem);

	/*
	 * map in zero page
	 */
	alloc_init_page(&start_mem, 0, __virt_to_phys(PAGE_OFFSET),
			DOMAIN_USER, L_PTE_CACHEABLE | L_PTE_YOUNG | L_PTE_PRESENT);

	flush_cache_all();

	return start_mem;
}

static inline
void mark_usable_memory_areas(unsigned long *start_mem, unsigned long end_mem)
{
	unsigned long smem;

	*start_mem = smem = PAGE_ALIGN(*start_mem);

	/*
	 * Mark all of memory from the end of kernel to end of memory
	 */
	while (smem < end_mem) {
		clear_bit(PG_reserved, &mem_map[MAP_NR(smem)].flags);
		smem += PAGE_SIZE;
	}

	/*
	 * Mark memory from page 1 to start of the swapper page directory
	 */
	smem = PAGE_OFFSET + PAGE_SIZE;
	while (smem < (unsigned long)&swapper_pg_dir) {
		clear_bit(PG_reserved, &mem_map[MAP_NR(smem)].flags);
		smem += PAGE_SIZE;
	}
}	

