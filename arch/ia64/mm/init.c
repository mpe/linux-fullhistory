/*
 * Initialize MMU support.
 *
 * Copyright (C) 1998, 1999 Hewlett-Packard Co
 * Copyright (C) 1998, 1999 David Mosberger-Tang <davidm@hpl.hp.com>
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <linux/bootmem.h>
#include <linux/mm.h>
#include <linux/reboot.h>
#include <linux/slab.h>
#include <linux/swap.h>

#include <asm/bitops.h>
#include <asm/dma.h>
#include <asm/efi.h>
#include <asm/ia32.h>
#include <asm/io.h>
#include <asm/pgalloc.h>
#include <asm/sal.h>
#include <asm/system.h>

/* References to section boundaries: */
extern char _stext, _etext, _edata, __init_begin, __init_end;

/*
 * These are allocated in head.S so that we get proper page alignment.
 * If you change the size of these then change head.S as well.
 */
extern char empty_bad_page[PAGE_SIZE];
extern pmd_t empty_bad_pmd_table[PTRS_PER_PMD];
extern pte_t empty_bad_pte_table[PTRS_PER_PTE];

extern void ia64_tlb_init (void);

static unsigned long totalram_pages;

/*
 * Fill in empty_bad_pmd_table with entries pointing to
 * empty_bad_pte_table and return the address of this PMD table.
 */
static pmd_t *
get_bad_pmd_table (void)
{
	pmd_t v;
	int i;

	pmd_set(&v, empty_bad_pte_table);

	for (i = 0; i < PTRS_PER_PMD; ++i)
		empty_bad_pmd_table[i] = v;

	return empty_bad_pmd_table;
}

/*
 * Fill in empty_bad_pte_table with PTEs pointing to empty_bad_page
 * and return the address of this PTE table.
 */
static pte_t *
get_bad_pte_table (void)
{
	pte_t v;
	int i;

	set_pte(&v, pte_mkdirty(mk_pte_phys(__pa(empty_bad_page), PAGE_SHARED)));

	for (i = 0; i < PTRS_PER_PTE; ++i)
		empty_bad_pte_table[i] = v;

	return empty_bad_pte_table;
}

void
__handle_bad_pgd (pgd_t *pgd)
{
	pgd_ERROR(*pgd);
	pgd_set(pgd, get_bad_pmd_table());
}

void
__handle_bad_pmd (pmd_t *pmd)
{
	pmd_ERROR(*pmd);
	pmd_set(pmd, get_bad_pte_table());
}

/*
 * Allocate and initialize an L3 directory page and set
 * the L2 directory entry PMD to the newly allocated page.
 */
pte_t*
get_pte_slow (pmd_t *pmd, unsigned long offset)
{
	pte_t *pte;

	pte = (pte_t *) __get_free_page(GFP_KERNEL);
	if (pmd_none(*pmd)) {
		if (pte) {
			/* everything A-OK */
			clear_page(pte);
			pmd_set(pmd, pte);
			return pte + offset;
		}
		pmd_set(pmd, get_bad_pte_table());
		return NULL;
	}
	free_page((unsigned long) pte);
	if (pmd_bad(*pmd)) {
		__handle_bad_pmd(pmd);
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + offset;
}

int
do_check_pgt_cache (int low, int high)
{
	int freed = 0;

        if (pgtable_cache_size > high) {
                do {
                        if (pgd_quicklist)
                                free_page((unsigned long)get_pgd_fast()), ++freed;
                        if (pmd_quicklist)
                                free_page((unsigned long)get_pmd_fast()), ++freed;
                        if (pte_quicklist)
                                free_page((unsigned long)get_pte_fast()), ++freed;
                } while (pgtable_cache_size > low);
        }
        return freed;
}

/*
 * This performs some platform-dependent address space initialization.
 * On IA-64, we want to setup the VM area for the register backing
 * store (which grows upwards) and install the gateway page which is
 * used for signal trampolines, etc.
 */
void
ia64_init_addr_space (void)
{
	struct vm_area_struct *vma;

	/*
	 * If we're out of memory and kmem_cache_alloc() returns NULL,
	 * we simply ignore the problem.  When the process attempts to
	 * write to the register backing store for the first time, it
	 * will get a SEGFAULT in this case.
	 */
	vma = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
	if (vma) {
		vma->vm_mm = current->mm;
		vma->vm_start = IA64_RBS_BOT;
		vma->vm_end = vma->vm_start + PAGE_SIZE;
		vma->vm_page_prot = PAGE_COPY;
		vma->vm_flags = VM_READ|VM_WRITE|VM_MAYREAD|VM_MAYWRITE|VM_GROWSUP;
		vma->vm_ops = NULL;
		vma->vm_pgoff = 0;
		vma->vm_file = NULL;
		vma->vm_private_data = NULL;
		insert_vm_struct(current->mm, vma);
	}
}

void
free_initmem (void)
{
	unsigned long addr;

	addr = (unsigned long) &__init_begin;
	for (; addr < (unsigned long) &__init_end; addr += PAGE_SIZE) {
		clear_bit(PG_reserved, &virt_to_page(addr)->flags);
		set_page_count(virt_to_page(addr), 1);
		free_page(addr);
		++totalram_pages;
	}
	printk ("Freeing unused kernel memory: %ldkB freed\n",
		(&__init_end - &__init_begin) >> 10);
}

void
free_initrd_mem(unsigned long start, unsigned long end)
{
	if (start < end)
		printk ("Freeing initrd memory: %ldkB freed\n", (end - start) >> 10);
	for (; start < end; start += PAGE_SIZE) {
		clear_bit(PG_reserved, &virt_to_page(start)->flags);
		set_page_count(virt_to_page(start), 1);
		free_page(start);
		++totalram_pages;
	}
}

void
si_meminfo (struct sysinfo *val)
{
	val->totalram = totalram_pages;
	val->sharedram = 0;
	val->freeram = nr_free_pages();
	val->bufferram = atomic_read(&buffermem_pages);
	val->totalhigh = 0;
	val->freehigh = 0;
	val->mem_unit = PAGE_SIZE;
	return;
}

void
show_mem (void)
{
	int i,free = 0,total = 0,reserved = 0;
	int shared = 0, cached = 0;

	printk("Mem-info:\n");
	show_free_areas();
	printk("Free swap:       %6dkB\n", nr_swap_pages<<(PAGE_SHIFT-10));
	i = max_mapnr;
	while (i-- > 0) {
		total++;
		if (PageReserved(mem_map+i))
			reserved++;
		else if (PageSwapCache(mem_map+i))
			cached++;
		else if (!page_count(mem_map + i))
			free++;
		else
			shared += page_count(mem_map + i) - 1;
	}
	printk("%d pages of RAM\n", total);
	printk("%d reserved pages\n", reserved);
	printk("%d pages shared\n", shared);
	printk("%d pages swap cached\n", cached);
	printk("%ld pages in page table cache\n", pgtable_cache_size);
	show_buffers();
}

/*
 * This is like put_dirty_page() but installs a clean page with PAGE_GATE protection
 * (execute-only, typically).
 */
struct page *
put_gate_page (struct page *page, unsigned long address)
{
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;

	if (!PageReserved(page))
		printk("put_gate_page: gate page at 0x%p not in reserved memory\n",
		       page_address(page));

	pgd = pgd_offset_k(address);		/* note: this is NOT pgd_offset()! */
	pmd = pmd_alloc(pgd, address);
	if (!pmd) {
		__free_page(page);
		panic("Out of memory.");
		return 0;
	}
	pte = pte_alloc(pmd, address);
	if (!pte) {
		__free_page(page);
		panic("Out of memory.");
		return 0;
	}
	if (!pte_none(*pte)) {
		pte_ERROR(*pte);
		__free_page(page);
		return 0;
	}
	flush_page_to_ram(page);
	set_pte(pte, page_pte_prot(page, PAGE_GATE));
	/* no need for flush_tlb */
	return page;
}

void __init
ia64_rid_init (void)
{
	unsigned long flags, rid, pta, impl_va_msb;

	/* Set up the kernel identity mappings (regions 6 & 7) and the vmalloc area (region 5): */
	ia64_clear_ic(flags);

	rid = ia64_rid(IA64_REGION_ID_KERNEL, __IA64_UNCACHED_OFFSET);
	ia64_set_rr(__IA64_UNCACHED_OFFSET, (rid << 8) | (_PAGE_SIZE_256M << 2));

	rid = ia64_rid(IA64_REGION_ID_KERNEL, PAGE_OFFSET);
	ia64_set_rr(PAGE_OFFSET, (rid << 8) | (_PAGE_SIZE_256M << 2));

	rid = ia64_rid(IA64_REGION_ID_KERNEL, VMALLOC_START);
	ia64_set_rr(VMALLOC_START, (rid << 8) | (PAGE_SHIFT << 2) | 1);

	__restore_flags(flags);

	/*
	 * Check if the virtually mapped linear page table (VMLPT)
	 * overlaps with a mapped address space.  The IA-64
	 * architecture guarantees that at least 50 bits of virtual
	 * address space are implemented but if we pick a large enough
	 * page size (e.g., 64KB), the VMLPT is big enough that it
	 * will overlap with the upper half of the kernel mapped
	 * region.  I assume that once we run on machines big enough
	 * to warrant 64KB pages, IMPL_VA_MSB will be significantly
	 * bigger, so we can just adjust the number below to get
	 * things going.  Alternatively, we could truncate the upper
	 * half of each regions address space to not permit mappings
	 * that would overlap with the VMLPT.  --davidm 99/11/13
	 */
#	define ld_pte_size		3
#	define ld_max_addr_space_pages	3*(PAGE_SHIFT - ld_pte_size) /* max # of mappable pages */
#	define ld_max_addr_space_size	(ld_max_addr_space_pages + PAGE_SHIFT)
#	define ld_max_vpt_size		(ld_max_addr_space_pages + ld_pte_size)
#	define POW2(n)			(1ULL << (n))
	impl_va_msb = ffz(~my_cpu_data.unimpl_va_mask) - 1;

	if (impl_va_msb < 50 || impl_va_msb > 60)
		panic("Bogus impl_va_msb value of %lu!\n", impl_va_msb);

	if (POW2(ld_max_addr_space_size - 1) + POW2(ld_max_vpt_size) > POW2(impl_va_msb))
		panic("mm/init: overlap between virtually mapped linear page table and "
		      "mapped kernel space!");
	pta = POW2(61) - POW2(impl_va_msb);
	/*
	 * Set the (virtually mapped linear) page table address.  Bit
	 * 8 selects between the short and long format, bits 2-7 the
	 * size of the table, and bit 0 whether the VHPT walker is
	 * enabled.
	 */
	ia64_set_pta(pta | (0<<8) | ((3*(PAGE_SHIFT-3)+3)<<2) | 1);
}

/*
 * Set up the page tables.
 */
void
paging_init (void)
{
	unsigned long max_dma, zones_size[MAX_NR_ZONES];

	clear_page((void *) ZERO_PAGE_ADDR);

	/* initialize mem_map[] */

	memset(zones_size, 0, sizeof(zones_size));

	max_dma = (PAGE_ALIGN(MAX_DMA_ADDRESS) >> PAGE_SHIFT);
	if (max_low_pfn < max_dma)
		zones_size[ZONE_DMA] = max_low_pfn;
	else {
		zones_size[ZONE_DMA] = max_dma;
		zones_size[ZONE_NORMAL] = max_low_pfn - max_dma;
	}
	free_area_init(zones_size);
}

static int
count_pages (u64 start, u64 end, void *arg)
{
	unsigned long *count = arg;

	*count += (end - start) >> PAGE_SHIFT;
	return 0;
}

static int
count_reserved_pages (u64 start, u64 end, void *arg)
{
	unsigned long num_reserved = 0;
	unsigned long *count = arg;
	struct page *pg;

	for (pg = virt_to_page(start); pg < virt_to_page(end); ++pg)
		if (PageReserved(pg))
			++num_reserved;
	*count += num_reserved;
	return 0;
}

void
mem_init (void)
{
	extern char __start_gate_section[];
	long reserved_pages, codesize, datasize, initsize;

	if (!mem_map)
		BUG();

	num_physpages = 0;
	efi_memmap_walk(count_pages, &num_physpages);

	max_mapnr = max_low_pfn;
	high_memory = __va(max_low_pfn * PAGE_SIZE);

	totalram_pages += free_all_bootmem();

	reserved_pages = 0;
	efi_memmap_walk(count_reserved_pages, &reserved_pages);

	codesize =  (unsigned long) &_etext - (unsigned long) &_stext;
	datasize =  (unsigned long) &_edata - (unsigned long) &_etext;
	initsize =  (unsigned long) &__init_end - (unsigned long) &__init_begin;

	printk("Memory: %luk/%luk available (%luk code, %luk reserved, %luk data, %luk init)\n",
	       (unsigned long) nr_free_pages() << (PAGE_SHIFT - 10),
	       max_mapnr << (PAGE_SHIFT - 10), codesize >> 10, reserved_pages << (PAGE_SHIFT - 10),
	       datasize >> 10, initsize >> 10);

	/* install the gate page in the global page table: */
	put_gate_page(virt_to_page(__start_gate_section), GATE_ADDR);

#ifndef CONFIG_IA64_SOFTSDV_HACKS
	/*
	 * (Some) SoftSDVs seem to have a problem with this call.
	 * Since it's mostly a performance optimization, just don't do
	 * it for now...  --davidm 99/12/6
	 */
	efi_enter_virtual_mode();
#endif

#ifdef CONFIG_IA32_SUPPORT
	ia32_gdt_init();
#endif
	return;
}
