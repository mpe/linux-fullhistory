/*
 *  arch/mips/mm/init.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *  Ported to MIPS by Ralf Baechle
 */
#include <linux/config.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>

#include <asm/cachectl.h>
#include <asm/jazzdma.h>
#include <asm/vector.h>
#include <asm/system.h>
#include <asm/segment.h>
#include <asm/pgtable.h>

extern void deskstation_tyne_dma_init(void);
extern void sound_mem_init(void);
extern void die_if_kernel(char *,struct pt_regs *,long);
extern void show_net_buffers(void);

extern char empty_zero_page[PAGE_SIZE];

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
pte_t * __bad_pagetable(void)
{
	extern char empty_bad_page_table[PAGE_SIZE];
	unsigned long page;
	unsigned long dummy1, dummy2;

	page = ((unsigned long)empty_bad_page_table) + (PT_OFFSET - PAGE_OFFSET);
#if __mips__ >= 3
        /*
         * Use 64bit code even for Linux/MIPS 32bit on R4000
         */
	__asm__ __volatile__(
		".set\tnoreorder\n"
		".set\tnoat\n\t"
		".set\tmips3\n\t"
		"dsll32\t$1,%2,0\n\t"
		"dsrl32\t%2,$1,0\n\t"
		"or\t%2,$1\n"
		"1:\tsd\t%2,(%0)\n\t"
		"subu\t%1,1\n\t"
		"bnez\t%1,1b\n\t"
		"addiu\t%0,8\n\t"
		".set\tmips0\n\t"
		".set\tat\n"
		".set\treorder"
		:"=r" (dummy1),
		 "=r" (dummy2)
		:"r" (pte_val(BAD_PAGE)),
		 "0" (page),
		 "1" (PAGE_SIZE/8));
#else
	__asm__ __volatile__(
		".set\tnoreorder\n"
		"1:\tsw\t%2,(%0)\n\t"
		"subu\t%1,1\n\t"
		"bnez\t%1,1b\n\t"
		"addiu\t%0,4\n\t"
		".set\treorder"
		:"=r" (dummy1),
		 "=r" (dummy2)
		:"r" (pte_val(BAD_PAGE)),
		 "0" (page),
		 "1" (PAGE_SIZE/4));
#endif

	return (pte_t *)page;
}

static inline void
__zeropage(unsigned long page)
{
	unsigned long dummy1, dummy2;

#ifdef __R4000__
        /*
         * Use 64bit code even for Linux/MIPS 32bit on R4000
         */
	__asm__ __volatile__(
		".set\tnoreorder\n"
		".set\tnoat\n\t"
		".set\tmips3\n"
		"1:\tsd\t$0,(%0)\n\t"
		"subu\t%1,1\n\t"
		"bnez\t%1,1b\n\t"
		"addiu\t%0,8\n\t"
		".set\tmips0\n\t"
		".set\tat\n"
		".set\treorder"
		:"=r" (dummy1),
		 "=r" (dummy2)
		:"0" (page),
		 "1" (PAGE_SIZE/8));
#else
	__asm__ __volatile__(
		".set\tnoreorder\n"
		"1:\tsw\t$0,(%0)\n\t"
		"subu\t%1,1\n\t"
		"bnez\t%1,1b\n\t"
		"addiu\t%0,4\n\t"
		".set\treorder"
		:"=r" (dummy1),
		 "=r" (dummy2)
		:"0" (page),
		 "1" (PAGE_SIZE/4));
#endif
}

static inline void
zeropage(unsigned long page)
{
	sys_cacheflush((void *)page, PAGE_SIZE, BCACHE);
	sync_mem();
	__zeropage(page + (PT_OFFSET - PAGE_OFFSET));
}

pte_t __bad_page(void)
{
	extern char empty_bad_page[PAGE_SIZE];
	unsigned long page = (unsigned long)empty_bad_page;

	zeropage(page);
	return pte_mkdirty(mk_pte(page, PAGE_SHARED));
}

unsigned long __zero_page(void)
{
	unsigned long page = (unsigned long) empty_zero_page;

	zeropage(page);
	return page;
}

/*
 * This is horribly inefficient ...
 */
void __copy_page(unsigned long from, unsigned long to)
{
	/*
	 * Now copy page from uncached KSEG1 to KSEG0.  The copy destination
	 * is in KSEG0 so that we keep stupid L2 caches happy.
	 */
	if(from == (unsigned long) empty_zero_page)
	{
		/*
		 * The page copied most is the COW empty_zero_page.  Since we
		 * know its contents we can avoid the writeback reading of
		 * the page.  Speeds up the standard case a lot.
		 */
		__zeropage(to);
	}
	else
	{
		/*
		 * Force writeback of old page to memory.  We don't know the
		 * virtual address, so we have to flush the entire cache ...
		 */
		sys_cacheflush(0, ~0, DCACHE);
		sync_mem();
		memcpy((void *) to,
		       (void *) (from + (PT_OFFSET - PAGE_OFFSET)), PAGE_SIZE);
	}
	/*
	 * Now writeback the page again if colour has changed.
	 * Actually this does a Hit_Writeback, but due to an artifact in
	 * the R4xx0 implementation this should be slightly faster.
	 * Then sweep chipset controlled secondary caches and the ICACHE.
	 */
	if (page_colour(from) != page_colour(to))
		sys_cacheflush(0, ~0, DCACHE);
	sys_cacheflush(0, ~0, ICACHE);
}

void show_mem(void)
{
	int i, free = 0, total = 0, reserved = 0;
	int shared = 0;

	printk("Mem-info:\n");
	show_free_areas();
	printk("Free swap:       %6dkB\n", nr_swap_pages<<(PAGE_SHIFT-10));
	i = (high_memory - PAGE_OFFSET) >> PAGE_SHIFT;
	while (i-- > 0) {
		total++;
		if (mem_map[i].reserved)
			reserved++;
		else if (!mem_map[i].count)
			free++;
		else
			shared += mem_map[i].count-1;
	}
	printk("%d pages of RAM\n", total);
	printk("%d free pages\n", free);
	printk("%d reserved pages\n", reserved);
	printk("%d pages shared\n", shared);
	show_buffers();
#ifdef CONFIG_NET
	show_net_buffers();
#endif
}

extern unsigned long free_area_init(unsigned long, unsigned long);

unsigned long paging_init(unsigned long start_mem, unsigned long end_mem)
{
	pgd_init((unsigned long)swapper_pg_dir - (PT_OFFSET - PAGE_OFFSET));
	return free_area_init(start_mem, end_mem);
}

void mem_init(unsigned long start_mem, unsigned long end_mem)
{
	int codepages = 0;
	int datapages = 0;
	unsigned long tmp;
	extern int _etext;

#ifdef CONFIG_MIPS_JAZZ
	start_mem = vdma_init(start_mem, end_mem);
#endif

	end_mem &= PAGE_MASK;
	high_memory = end_mem;

	/* mark usable pages in the mem_map[] */
	start_mem = PAGE_ALIGN(start_mem);

	tmp = start_mem;
	while (tmp < high_memory) {
		mem_map[MAP_NR(tmp)].reserved = 0;
		tmp += PAGE_SIZE;
	}

#ifdef CONFIG_DESKSTATION_TYNE
	deskstation_tyne_dma_init();
#endif
#ifdef CONFIG_SOUND
	sound_mem_init();
#endif
	for (tmp = PAGE_OFFSET ; tmp < high_memory ; tmp += PAGE_SIZE) {
		if (mem_map[MAP_NR(tmp)].reserved) {
			if (tmp < (unsigned long) &_etext)
				codepages++;
			else if (tmp < start_mem)
				datapages++;
			continue;
		}
		mem_map[MAP_NR(tmp)].count = 1;
		free_page(tmp);
	}
	tmp = nr_free_pages << PAGE_SHIFT;
	printk("Memory: %luk/%luk available (%dk kernel code, %dk data)\n",
		tmp >> 10,
		(high_memory - PAGE_OFFSET) >> 10,
		codepages << (PAGE_SHIFT-10),
		datapages << (PAGE_SHIFT-10));

	return;
}

void si_meminfo(struct sysinfo *val)
{
	int i;

	i = high_memory >> PAGE_SHIFT;
	val->totalram = 0;
	val->sharedram = 0;
	val->freeram = nr_free_pages << PAGE_SHIFT;
	val->bufferram = buffermem;
	while (i-- > 0)  {
		if (mem_map[i].reserved)
			continue;
		val->totalram++;
		if (!mem_map[i].count)
			continue;
		val->sharedram += mem_map[i].count-1;
	}
	val->totalram <<= PAGE_SHIFT;
	val->sharedram <<= PAGE_SHIFT;
	return;
}
