/* $Id: init.c,v 1.4 1999/10/23 01:37:02 gniibe Exp $
 *
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
#include <linux/bootmem.h>

#include <asm/processor.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/mmu_context.h>
#include <asm/io.h>

/*
 * Cache of MMU context last used.
 */
unsigned long mmu_context_cache;

static unsigned long totalram_pages = 0;
static unsigned long totalhigh_pages = 0;

extern void show_net_buffers(void);
extern unsigned long init_smp_mappings(unsigned long);

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

unsigned long empty_bad_page[1024];
pte_t empty_bad_pte_table[PTRS_PER_PTE];
extern unsigned long empty_zero_page[1024];

static pte_t * get_bad_pte_table(void)
{
	pte_t v;
	int i;

	v = pte_mkdirty(mk_pte_phys(__pa(empty_bad_page), PAGE_SHARED));

	for (i = 0; i < PAGE_SIZE/sizeof(pte_t); i++)
		empty_bad_pte_table[i] = v;

	return empty_bad_pte_table;
}

void __handle_bad_pmd(pmd_t *pmd)
{
	pmd_ERROR(*pmd);
	pmd_val(*pmd) = _PAGE_TABLE + __pa(get_bad_pte_table());
}

void __handle_bad_pmd_kernel(pmd_t *pmd)
{
	pmd_ERROR(*pmd);
	pmd_val(*pmd) = _KERNPG_TABLE + __pa(get_bad_pte_table());
}

pte_t *get_pte_kernel_slow(pmd_t *pmd, unsigned long offset)
{
	pte_t *pte;

	pte = (pte_t *) __get_free_page(GFP_KERNEL);
	if (pmd_none(*pmd)) {
		if (pte) {
			clear_page(pte);
			pmd_val(*pmd) = _KERNPG_TABLE + __pa(pte);
			return pte + offset;
		}
		pmd_val(*pmd) = _KERNPG_TABLE + __pa(get_bad_pte_table());
		return NULL;
	}
	free_page((unsigned long)pte);
	if (pmd_bad(*pmd)) {
		__handle_bad_pmd_kernel(pmd);
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
			clear_page((void *)pte);
			pmd_val(*pmd) = _PAGE_TABLE + __pa(pte);
			return (pte_t *)pte + offset;
		}
		pmd_val(*pmd) = _PAGE_TABLE + __pa(get_bad_pte_table());
		return NULL;
	}
	free_page(pte);
	if (pmd_bad(*pmd)) {
		__handle_bad_pmd(pmd);
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + offset;
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
	show_buffers();
#ifdef CONFIG_NET
	show_net_buffers();
#endif
}

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
void __init paging_init(void)
{
	pgd_t * pg_dir;

	/* We don't need kernel mapping as hardware support that. */
	pg_dir = swapper_pg_dir;

	/* Unmap the original low memory mappings to detect NULL reference */
	pgd_val(pg_dir[0]) = 0;

	/* Enable MMU */
	ctrl_outl(MMU_CONTROL_INIT, MMUCR);

	mmu_context_cache = MMU_CONTEXT_FIRST_VERSION;
	set_asid(mmu_context_cache & MMU_CONTEXT_ASID_MASK);

	free_area_init(max_low_pfn);
}

void __init mem_init(void)
{
	int codepages = 0;
	int reservedpages = 0;
	int datapages = 0;
	int initpages = 0;

	max_mapnr = num_physpages = max_low_pfn;
	high_memory = (void *) ((unsigned long)__va(max_low_pfn * PAGE_SIZE)+__MEMORY_START);

	/* clear the zero-page */
	memset(empty_zero_page, 0, PAGE_SIZE);

	/* this will put all low memory onto the freelists */
	totalram_pages += free_all_bootmem();

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
		ClearPageReserved(mem_map + MAP_NR(addr));
		set_page_count(mem_map+MAP_NR(addr), 1);
		free_page(addr);
		totalram_pages++;
	}
	printk ("Freeing unused kernel memory: %dk freed\n", (&__init_end - &__init_begin) >> 10);
}

void si_meminfo(struct sysinfo *val)
{
	val->totalram = totalram_pages;
	val->sharedram = 0;
	val->freeram = nr_free_pages;
	val->bufferram = atomic_read(&buffermem_pages);
	val->totalhigh = totalhigh_pages;
	val->freehigh = nr_free_highpages;
	val->mem_unit = PAGE_SIZE;
	return;
}
