/*
 *  linux/arch/sh/mm/init.c
 *
 *  Copyright (C) 1999  Niibe Yutaka
 *
 *  Based on linux/arch/i386/mm/init.c:
 *   Copyright (C) 1995  Linus Torvalds
 */

#include <linux/config.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/smp.h>
#include <linux/init.h>
#ifdef CONFIG_BLK_DEV_INITRD
#include <linux/blk.h>
#endif

#include <asm/processor.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/mmu_context.h>

/*
 * Cache of MMU context last used.
 */
unsigned long mmu_context_cache;

static unsigned long totalram = 0;

extern void show_net_buffers(void);
extern unsigned long init_smp_mappings(unsigned long);

void __bad_pte_kernel(pmd_t *pmd)
{
	printk("Bad pmd in pte_alloc: %08lx\n", pmd_val(*pmd));
	pmd_val(*pmd) = _KERNPG_TABLE + __pa(BAD_PAGETABLE);
}

void __bad_pte(pmd_t *pmd)
{
	printk("Bad pmd in pte_alloc: %08lx\n", pmd_val(*pmd));
	pmd_val(*pmd) = _PAGE_TABLE + __pa(BAD_PAGETABLE);
}

pte_t *get_pte_kernel_slow(pmd_t *pmd, unsigned long offset)
{
	pte_t *pte;

	pte = (pte_t *) __get_free_page(GFP_KERNEL);
	if (pmd_none(*pmd)) {
		if (pte) {
			clear_page((unsigned long)pte);
			pmd_val(*pmd) = _KERNPG_TABLE + __pa(pte);
			return pte + offset;
		}
		pmd_val(*pmd) = _KERNPG_TABLE + __pa(BAD_PAGETABLE);
		return NULL;
	}
	free_page((unsigned long)pte);
	if (pmd_bad(*pmd)) {
		__bad_pte_kernel(pmd);
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + offset;
}

pte_t *get_pte_slow(pmd_t *pmd, unsigned long offset)
{
	unsigned long pte;

	pte = (unsigned long) __get_free_page(GFP_KERNEL);
	if (pmd_none(*pmd)) {
		if (pte) {
			clear_page(pte);
			pmd_val(*pmd) = _PAGE_TABLE + __pa(pte);
			return (pte_t *)(pte + offset);
		}
		pmd_val(*pmd) = _PAGE_TABLE + __pa(BAD_PAGETABLE);
		return NULL;
	}
	free_page(pte);
	if (pmd_bad(*pmd)) {
		__bad_pte(pmd);
		return NULL;
	}
	return (pte_t *) (pmd_page(*pmd) + offset);
}

int do_check_pgt_cache(int low, int high)
{
	int freed = 0;
	if(pgtable_cache_size > high) {
		do {
			if(pgd_quicklist)
				free_pgd_slow(get_pgd_fast()), freed++;
			if(pmd_quicklist)
				free_pmd_slow(get_pmd_fast()), freed++;
			if(pte_quicklist)
				free_pte_slow(get_pte_fast()), freed++;
		} while(pgtable_cache_size > low);
	}
	return freed;
}

/*
 * BAD_PAGE is the page that is used for page faults when linux
 * is out-of-memory. Older versions of linux just did a
 * do_exit(), but using this instead means there is less risk
 * for a process dying in kernel mode, possibly leaving an inode
 * unused etc..
 *
 * BAD_PAGETABLE is the accompanying page-table: it is initialized
 * to point to BAD_PAGE entries.
 *
 * ZERO_PAGE is a special page that is used for zero-initialized
 * data and COW.
 */
pte_t * __bad_pagetable(void)
{
	extern char empty_bad_page_table[PAGE_SIZE];
	unsigned long page = (unsigned long)empty_bad_page_table;

	clear_page(page);
	return (pte_t *)empty_bad_page_table;
}

pte_t __bad_page(void)
{
	extern char empty_bad_page[PAGE_SIZE];
	unsigned long page = (unsigned long)empty_bad_page;

	clear_page(page);
	return pte_mkdirty(mk_pte(page, PAGE_SHARED));
}

void show_mem(void)
{
	int i,free = 0,total = 0,reserved = 0;
	int shared = 0, cached = 0;

	printk("Mem-info:\n");
	show_free_areas();
	printk("Free swap:       %6dkB\n",nr_swap_pages<<(PAGE_SHIFT-10));
	i = max_mapnr;
	while (i-- > 0) {
		total++;
		if (PageReserved(mem_map+i))
			reserved++;
		else if (PageSwapCache(mem_map+i))
			cached++;
		else if (!page_count(mem_map+i))
			free++;
		else
			shared += page_count(mem_map+i) - 1;
	}
	printk("%d pages of RAM\n",total);
	printk("%d reserved pages\n",reserved);
	printk("%d pages shared\n",shared);
	printk("%d pages swap cached\n",cached);
	printk("%ld pages in page table cache\n",pgtable_cache_size);
#ifdef CONFIG_NET
	show_net_buffers();
#endif
}

extern unsigned long free_area_init(unsigned long, unsigned long);

/* References to section boundaries */

extern char _text, _etext, _edata, __bss_start, _end;
extern char __init_begin, __init_end;

pgd_t swapper_pg_dir[1024];

/*
 * paging_init() sets up the page tables
 *
 * This routines also unmaps the page at virtual kernel address 0, so
 * that we can trap those pesky NULL-reference errors in the kernel.
 */
unsigned long __init
paging_init(unsigned long start_mem, unsigned long end_mem)
{
	pgd_t * pg_dir;

	start_mem = PAGE_ALIGN(start_mem);

	/* We don't need kernel mapping as hardware support that. */
	pg_dir = swapper_pg_dir;

	/* Unmap the original low memory mappings to detect NULL reference */
	pgd_val(pg_dir[0]) = 0;

	/* Enable MMU */
	__asm__ __volatile__ ("mov.l	%0,%1"
			      : /* no output */
			      : "r" (MMU_CONTROL_INIT), "m" (__m(MMUCR)));

	return free_area_init(start_mem, end_mem);
}

unsigned long empty_bad_page[1024];
unsigned long empty_bad_page_table[1024];
unsigned long empty_zero_page[1024];

void __init mem_init(unsigned long start_mem, unsigned long end_mem)
{
	int codepages = 0;
	int reservedpages = 0;
	int datapages = 0;
	int initpages = 0;
	unsigned long tmp;

	end_mem &= PAGE_MASK;
	high_memory = (void *) end_mem;
	max_mapnr = num_physpages = MAP_NR(end_mem);

	/* clear the zero-page */
	memset(empty_zero_page, 0, PAGE_SIZE);

	/* Mark (clear "reserved" bit) usable pages in the mem_map[] */
	/*    Note that all are marked reserved already. */
	tmp = start_mem = PAGE_ALIGN(start_mem);
	while (tmp < end_mem) {
		clear_bit(PG_reserved, &mem_map[MAP_NR(tmp)].flags);
		clear_bit(PG_DMA, &mem_map[MAP_NR(tmp)].flags);
		tmp += PAGE_SIZE;
	}

	for (tmp = PAGE_OFFSET; tmp < end_mem; tmp += PAGE_SIZE) {
		if (PageReserved(mem_map+MAP_NR(tmp))) {
			if (tmp >= (unsigned long) &_text && tmp < (unsigned long) &_edata) {
				if (tmp < (unsigned long) &_etext)
					codepages++;
				else
					datapages++;
			} else if (tmp >= (unsigned long) &__init_begin
				   && tmp < (unsigned long) &__init_end)
				initpages++;
			else if (tmp >= (unsigned long) &__bss_start
				 && tmp < (unsigned long) start_mem)
				datapages++;
			else
				reservedpages++;
			continue;
		}
		set_page_count(mem_map+MAP_NR(tmp), 1);
		totalram += PAGE_SIZE;
#ifdef CONFIG_BLK_DEV_INITRD
		if (!initrd_start || (tmp < initrd_start || tmp >= initrd_end))
#endif
			free_page(tmp);
	}
	printk("Memory: %luk/%luk available (%dk kernel code, %dk reserved, %dk data, %dk init)\n",
		(unsigned long) nr_free_pages << (PAGE_SHIFT-10),
		max_mapnr << (PAGE_SHIFT-10),
		codepages << (PAGE_SHIFT-10),
		reservedpages << (PAGE_SHIFT-10),
		datapages << (PAGE_SHIFT-10),
		initpages << (PAGE_SHIFT-10));
}

void free_initmem(void)
{
	unsigned long addr;
	
	addr = (unsigned long)(&__init_begin);
	for (; addr < (unsigned long)(&__init_end); addr += PAGE_SIZE) {
		mem_map[MAP_NR(addr)].flags &= ~(1 << PG_reserved);
		set_page_count(mem_map+MAP_NR(addr), 1);
		free_page(addr);
		totalram += PAGE_SIZE;
	}
	printk ("Freeing unused kernel memory: %dk freed\n", (&__init_end - &__init_begin) >> 10);
}

void si_meminfo(struct sysinfo *val)
{
	val->totalram = totalram;
	val->sharedram = 0;
	val->freeram = nr_free_pages << PAGE_SHIFT;
	val->bufferram = atomic_read(&buffermem);
	return;
}
