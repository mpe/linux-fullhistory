/*
 *  linux/arch/arm/mm/init.c
 *
 *  Copyright (C) 1995, 1996  Russell King
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

#include <asm/system.h>
#include <asm/segment.h>
#include <asm/pgtable.h>
#include <asm/dma.h>
#include <asm/hardware.h>
#include <asm/proc/mm-init.h>

pgd_t swapper_pg_dir[PTRS_PER_PGD];

extern char _etext, _stext, _edata, __bss_start, _end;
extern char __init_begin, __init_end;

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
 * for a process dying in kernel mode, possibly leaving a inode
 * unused etc..
 *
 * BAD_PAGETABLE is the accompanying page-table: it is initialized
 * to point to BAD_PAGE entries.
 *
 * ZERO_PAGE is a special page that is used for zero-initialized
 * data and COW.
 */
#if PTRS_PER_PTE != 1
unsigned long *empty_bad_page_table;

pte_t *__bad_pagetable(void)
{
	int i;
	pte_t bad_page;

	bad_page = BAD_PAGE;
	for (i = 0; i < PTRS_PER_PTE; i++)
		empty_bad_page_table[i] = (unsigned long)pte_val(bad_page);
	return (pte_t *) empty_bad_page_table;
}
#endif

unsigned long *empty_zero_page;
unsigned long *empty_bad_page;

pte_t __bad_page(void)
{
	memzero (empty_bad_page, PAGE_SIZE);
	return pte_nocache(pte_mkdirty(mk_pte((unsigned long) empty_bad_page, PAGE_SHARED)));
}

void show_mem(void)
{
	extern void show_net_buffers(void);
	int i,free = 0,total = 0,reserved = 0;
	int shared = 0;

	printk("Mem-info:\n");
	show_free_areas();
	printk("Free swap:       %6dkB\n",nr_swap_pages<<(PAGE_SHIFT-10));
	i = MAP_NR(high_memory);
	while (i-- > 0) {
		total++;
		if (PageReserved(mem_map+i))
			reserved++;
		else if (!atomic_read(&mem_map[i].count))
			free++;
		else
			shared += atomic_read(&mem_map[i].count) - 1;
	}
	printk("%d pages of RAM\n",total);
	printk("%d free pages\n",free);
	printk("%d reserved pages\n",reserved);
	printk("%d pages shared\n",shared);
	show_buffers();
#ifdef CONFIG_NET
	show_net_buffers();
#endif
}

/*
 * paging_init() sets up the page tables...
 */
__initfunc(unsigned long paging_init(unsigned long start_mem, unsigned long end_mem))
{
	extern unsigned long free_area_init(unsigned long, unsigned long);

	start_mem = PAGE_ALIGN(start_mem);
	empty_zero_page = (unsigned long *)start_mem;
	start_mem += PAGE_SIZE;
	empty_bad_page = (unsigned long *)start_mem;
	start_mem += PAGE_SIZE;
#if PTRS_PER_PTE != 1
	empty_bad_page_table = (unsigned long *)start_mem;
	start_mem += PTRS_PER_PTE * sizeof (void *);
#endif
	memzero (empty_zero_page, PAGE_SIZE);
	start_mem = setup_pagetables (start_mem, end_mem);

	flush_tlb_all();
	update_memc_all();

	return free_area_init(start_mem, end_mem);
}

/*
 * mem_init() marks the free areas in the mem_map and tells us how much
 * memory is free.  This is done after various parts of the system have
 * claimed their memory after the kernel image.
 */
__initfunc(void mem_init(unsigned long start_mem, unsigned long end_mem))
{
	extern void sound_init(void);
	int codepages = 0;
	int reservedpages = 0;
	int datapages = 0;
	int initpages = 0;
	unsigned long tmp;

	end_mem &= PAGE_MASK;
	high_memory = (void *)end_mem;
	max_mapnr = num_physpages = MAP_NR(end_mem);

	/* mark usable pages in the mem_map[] */
	mark_usable_memory_areas(&start_mem, end_mem);

	for (tmp = PAGE_OFFSET; tmp < end_mem ; tmp += PAGE_SIZE) {
		if (PageReserved(mem_map+MAP_NR(tmp))) {
			if (tmp >= KERNTOPHYS(_stext) &&
			    tmp < KERNTOPHYS(_edata)) {
				if (tmp < KERNTOPHYS(_etext))
					codepages++;
				else
					datapages++;
			} else if (tmp >= KERNTOPHYS(__init_begin)
				   && tmp < KERNTOPHYS(__init_end))
				initpages++;
			else if (tmp >= KERNTOPHYS(__bss_start)
				 && tmp < (unsigned long) start_mem)
				datapages++;
			else
				reservedpages++;
			continue;
		}
		atomic_set(&mem_map[MAP_NR(tmp)].count, 1);
#ifdef CONFIG_BLK_DEV_INITRD
		if (!initrd_start || (tmp < initrd_start || tmp >= initrd_end))
#endif
			free_page(tmp);
	}
	printk ("Memory: %luk/%luk available (%dk kernel code, %dk reserved, %dk data, %dk init)\n",
		 (unsigned long) nr_free_pages << (PAGE_SHIFT-10),
		 max_mapnr << (PAGE_SHIFT-10),
		 codepages << (PAGE_SHIFT-10),
		 reservedpages << (PAGE_SHIFT-10),
		 datapages << (PAGE_SHIFT-10),
		 initpages << (PAGE_SHIFT-10));

#ifdef CONFIG_CPU_26
	if (max_mapnr <= 128) {
		extern int sysctl_overcommit_memory;
		/* On a machine this small we won't get anywhere without
		   overcommit, so turn it on by default.  */
		sysctl_overcommit_memory = 1;
	}
#endif
}

void free_initmem (void)
{
	unsigned long addr;

	addr = (unsigned long)(&__init_begin);
	for (; addr < (unsigned long)(&__init_end); addr += PAGE_SIZE) {
		mem_map[MAP_NR(addr)].flags &= ~(1 << PG_reserved);
		atomic_set(&mem_map[MAP_NR(addr)].count, 1);
		free_page(addr);
	}
	printk ("Freeing unused kernel memory: %dk freed\n", (&__init_end - &__init_begin) >> 10);
}

void si_meminfo(struct sysinfo *val)
{
	int i;

	i = MAP_NR(high_memory);
	val->totalram = 0;
	val->sharedram = 0;
	val->freeram = nr_free_pages << PAGE_SHIFT;
	val->bufferram = buffermem;
	while (i-- > 0)  {
		if (PageReserved(mem_map+i))
			continue;
		val->totalram++;
		if (!atomic_read(&mem_map[i].count))
			continue;
		val->sharedram += atomic_read(&mem_map[i].count) - 1;
	}
	val->totalram <<= PAGE_SHIFT;
	val->sharedram <<= PAGE_SHIFT;
}

