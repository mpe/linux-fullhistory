/*
 * arch/arm/mm/mm-armv.c
 *
 * Page table sludge for ARM v3 and v4 processor architectures.
 *
 * Copyright (C) 1998-1999 Russell King
 */
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/init.h>

#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/io.h>

#include "map.h"

unsigned long *valid_addr_bitmap;

/*
 * need to get a 16k page for level 1
 */
pgd_t *get_pgd_slow(void)
{
	pgd_t *pgd = (pgd_t *)__get_free_pages(GFP_KERNEL,2);
	pmd_t *new_pmd;

	if (pgd) {
		pgd_t *init = pgd_offset(&init_mm, 0);
		
		memzero(pgd, USER_PTRS_PER_PGD * BYTES_PER_PTR);
		memcpy(pgd + USER_PTRS_PER_PGD, init + USER_PTRS_PER_PGD,
			(PTRS_PER_PGD - USER_PTRS_PER_PGD) * BYTES_PER_PTR);
		clean_cache_area(pgd, PTRS_PER_PGD * BYTES_PER_PTR);

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
	}
	return pgd;

nomem_pmd:
	pmd_free(new_pmd);
nomem:
	free_pages((unsigned long)pgd, 2);
	return NULL;
}

pte_t *get_pte_slow(pmd_t *pmd, unsigned long offset)
{
	pte_t *pte;

	pte = (pte_t *)get_page_2k(GFP_KERNEL);
	if (pmd_none(*pmd)) {
		if (pte) {
			memzero(pte, 2 * PTRS_PER_PTE * BYTES_PER_PTR);
			clean_cache_area(pte, PTRS_PER_PTE * BYTES_PER_PTR);
			pte += PTRS_PER_PTE;
			set_pmd(pmd, mk_user_pmd(pte));
			return pte + offset;
		}
		set_pmd(pmd, mk_user_pmd(BAD_PAGETABLE));
		return NULL;
	}
	free_page_2k((unsigned long)pte);
	if (pmd_bad(*pmd)) {
		__bad_pmd(pmd);
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + offset;
}

pte_t *get_pte_kernel_slow(pmd_t *pmd, unsigned long offset)
{
	pte_t *pte;

	pte = (pte_t *)get_page_2k(GFP_KERNEL);
	if (pmd_none(*pmd)) {
		if (pte) {
			memzero(pte, 2 * PTRS_PER_PTE * BYTES_PER_PTR);
			clean_cache_area(pte, PTRS_PER_PTE * BYTES_PER_PTR);
			pte += PTRS_PER_PTE;
			set_pmd(pmd, mk_kernel_pmd(pte));
			return pte + offset;
		}
		set_pmd(pmd, mk_kernel_pmd(BAD_PAGETABLE));
		return NULL;
	}
	free_page_2k((unsigned long)pte);
	if (pmd_bad(*pmd)) {
		__bad_pmd_kernel(pmd);
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + offset;
}

/*
 * Create a SECTION PGD between VIRT and PHYS in domain
 * DOMAIN with protection PROT
 */
static inline void
alloc_init_section(unsigned long virt, unsigned long phys, int prot)
{
	pmd_t pmd;

	pmd_val(pmd) = phys | prot;

	set_pmd(pmd_offset(pgd_offset_k(virt), virt), pmd);
}

/*
 * Add a PAGE mapping between VIRT and PHYS in domain
 * DOMAIN with protection PROT.  Note that due to the
 * way we map the PTEs, we must allocate two PTE_SIZE'd
 * blocks - one for the Linux pte table, and one for
 * the hardware pte table.
 */
static inline void
alloc_init_page(unsigned long *mem, unsigned long virt, unsigned long phys, int domain, int prot)
{
	pmd_t *pmdp;
	pte_t *ptep;

	pmdp = pmd_offset(pgd_offset_k(virt), virt);

#define PTE_SIZE (PTRS_PER_PTE * BYTES_PER_PTR)

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

#undef PTE_SIZE

	ptep = pte_offset(pmdp, virt);

	set_pte(ptep, mk_pte_phys(phys, __pgprot(prot)));
}

/*
 * Clear any PGD mapping.  On a two-level page table system,
 * the clearance is done by the middle-level functions (pmd)
 * rather than the top-level (pgd) functions.
 */
static inline void
free_init_section(unsigned long virt)
{
	pmd_clear(pmd_offset(pgd_offset_k(virt), virt));
}

/*
 * Create the page directory entries and any necessary
 * page tables for the mapping specified by `md'.  We
 * are able to cope here with varying sizes and address
 * offsets, and we take full advantage of sections.
 */
static void __init
create_mapping(unsigned long *mem_ptr, struct map_desc *md)
{
	unsigned long virt, length;
	int prot_sect, prot_pte;
	long off;

	prot_pte = L_PTE_PRESENT | L_PTE_YOUNG | L_PTE_DIRTY |
		   (md->prot_read  ? L_PTE_USER       : 0) |
		   (md->prot_write ? L_PTE_WRITE      : 0) |
		   (md->cacheable  ? L_PTE_CACHEABLE  : 0) |
		   (md->bufferable ? L_PTE_BUFFERABLE : 0);

	prot_sect = PMD_TYPE_SECT | PMD_DOMAIN(md->domain) |
		    (md->prot_read  ? PMD_SECT_AP_READ    : 0) |
		    (md->prot_write ? PMD_SECT_AP_WRITE   : 0) |
		    (md->cacheable  ? PMD_SECT_CACHEABLE  : 0) |
		    (md->bufferable ? PMD_SECT_BUFFERABLE : 0);

	virt   = md->virtual;
	off    = md->physical - virt;
	length = md->length;

	while ((virt & 1048575 || (virt + off) & 1048575) && length >= PAGE_SIZE) {
		alloc_init_page(mem_ptr, virt, virt + off, md->domain, prot_pte);

		virt   += PAGE_SIZE;
		length -= PAGE_SIZE;
	}

	while (length >= PGDIR_SIZE) {
		alloc_init_section(virt, virt + off, prot_sect);

		virt   += PGDIR_SIZE;
		length -= PGDIR_SIZE;
	}

	while (length >= PAGE_SIZE) {
		alloc_init_page(mem_ptr, virt, virt + off, md->domain, prot_pte);

		virt   += PAGE_SIZE;
		length -= PAGE_SIZE;
	}
}

/*
 * Initial boot-time mapping.  This covers just the
 * zero page, kernel and the flush area.  NB: it
 * must be sorted by virtual address, and no
 * virtual address overlaps.
 *  init_map[2..4] are for architectures with small
 *  amounts of banked memory.
 */
static struct map_desc init_map[] __initdata = {
	{ 0, 0, PAGE_SIZE,  DOMAIN_USER,   0, 0, 1, 0 }, /* zero page     */
	{ 0, 0, 0,          DOMAIN_KERNEL, 0, 1, 1, 1 }, /* kernel memory */
	{ 0, 0, 0,          DOMAIN_KERNEL, 0, 1, 1, 1 },
	{ 0, 0, 0,          DOMAIN_KERNEL, 0, 1, 1, 1 },
	{ 0, 0, 0,          DOMAIN_KERNEL, 0, 1, 1, 1 },
	{ 0, 0, PGDIR_SIZE, DOMAIN_KERNEL, 1, 0, 1, 1 }, /* cache flush 1 */
	{ 0, 0, 0,          DOMAIN_KERNEL, 1, 0, 1, 0 }  /* cache flush 2 */
};

#define NR_INIT_MAPS (sizeof(init_map) / sizeof(init_map[0]))

unsigned long __init
setup_page_tables(unsigned long start_mem, unsigned long end_mem)
{
	unsigned long address = 0;
	int idx = 0;

	/*
	 * Correct the above mappings
	 */
	init_map[0].physical =
	init_map[1].physical = __virt_to_phys(PAGE_OFFSET);
	init_map[1].virtual  = PAGE_OFFSET;
	init_map[1].length   = end_mem - PAGE_OFFSET;
	init_map[5].physical = FLUSH_BASE_PHYS;
	init_map[5].virtual  = FLUSH_BASE;
#ifdef FLUSH_BASE_MINICACHE
	init_map[6].physical = FLUSH_BASE_PHYS + PGDIR_SIZE;
	init_map[6].virtual  = FLUSH_BASE_MINICACHE;
	init_map[6].length   = PGDIR_SIZE;
#endif

	/*
	 * Firstly, go through the initial mappings,
	 * but clear out any pgdir entries that are
	 * not in the description.
	 */
	do {
		if (address < init_map[idx].virtual || idx == NR_INIT_MAPS) {
			free_init_section(address);
			address += PGDIR_SIZE;
		} else {
			create_mapping(&start_mem, init_map + idx);

			address = init_map[idx].virtual + init_map[idx].length;
			address = (address + PGDIR_SIZE - 1) & PGDIR_MASK;

			do {
				idx += 1;
			} while (init_map[idx].length == 0 && idx < NR_INIT_MAPS);
		}
	} while (address != 0);

	/*
	 * Now, create the architecture specific mappings
	 */
	for (idx = 0; idx < io_desc_size; idx++)
		create_mapping(&start_mem, io_desc + idx);

	flush_cache_all();

	return start_mem;
}

/*
 * The mem_map array can get very big.  Mark the end of the
 * valid mem_map banks with PG_skip, and setup the address
 * validity bitmap.
 */
unsigned long __init
create_mem_holes(unsigned long start_mem, unsigned long end_mem)
{
	struct page *pg = NULL;
	unsigned int sz, i;

	if (!machine_is_riscpc())
		return start_mem;

	sz = (end_mem - PAGE_OFFSET) >> 20;
	sz = (sz + 31) >> 3;

	valid_addr_bitmap = (unsigned long *)start_mem;
	start_mem += sz;

	memset(valid_addr_bitmap, 0, sz);

	if (start_mem > mem_desc[0].virt_end)
		printk(KERN_CRIT "*** Error: RAM bank 0 too small\n");

	for (i = 0; i < mem_desc_size; i++) {
		unsigned int idx, end;

		if (pg) {
			pg->next_hash = mem_map +
				 MAP_NR(mem_desc[i].virt_start);
			pg = NULL;
		}

		idx = __kern_valid_idx(mem_desc[i].virt_start);
		end = __kern_valid_idx(mem_desc[i].virt_end);

		do
			set_bit(idx, valid_addr_bitmap);
		while (++idx < end);

		if (mem_desc[i].virt_end < end_mem) {
			pg = mem_map + MAP_NR(mem_desc[i].virt_end);

			set_bit(PG_skip, &pg->flags);
		}
	}

	if (pg)
		pg->next_hash = NULL;

	return start_mem;
}

void __init
mark_usable_memory_areas(unsigned long start_mem, unsigned long end_mem)
{
	/*
	 * Mark all of memory from the end of kernel to end of memory
	 */
	while (start_mem < end_mem) {
		clear_bit(PG_reserved, &mem_map[MAP_NR(start_mem)].flags);
		start_mem += PAGE_SIZE;
	}

	/*
	 * Mark memory from page 1 to start of the swapper page directory
	 */
	start_mem = PAGE_OFFSET + PAGE_SIZE;
	while (start_mem < (unsigned long)&swapper_pg_dir) {
		clear_bit(PG_reserved, &mem_map[MAP_NR(start_mem)].flags);
		start_mem += PAGE_SIZE;
	}
}	
