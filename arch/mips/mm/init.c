/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 1995, 1996 by Ralf Baechle
 *
 * $Id: init.c,v 1.14 1998/05/01 01:34:53 ralf Exp $
 */
#include <linux/config.h>
#include <linux/init.h>
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
#include <linux/swap.h>
#ifdef CONFIG_BLK_DEV_INITRD
#include <linux/blk.h>
#endif

#include <asm/bootinfo.h>
#include <asm/cachectl.h>
#include <asm/dma.h>
#include <asm/jazzdma.h>
#include <asm/vector.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#ifdef CONFIG_SGI
#include <asm/sgialib.h>
#endif

extern void deskstation_tyne_dma_init(void);
extern void show_net_buffers(void);

const char bad_pmd_string[] = "Bad pmd in pte_alloc: %08lx\n";

asmlinkage int sys_cacheflush(void *addr, int bytes, int cache)
{
	/* XXX Just get it working for now... */
	flush_cache_all();
	return 0;
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
pte_t * __bad_pagetable(void)
{
	extern char empty_bad_page_table[PAGE_SIZE];
	unsigned long page;
	unsigned long dummy1, dummy2;
#if (_MIPS_ISA == _MIPS_ISA_MIPS3) || (_MIPS_ISA == _MIPS_ISA_MIPS4)
	unsigned long dummy3;
#endif

	page = (unsigned long) empty_bad_page_table;
	/*
	 * As long as we only save the low 32 bit of the 64 bit wide
	 * R4000 registers on interrupt we cannot use 64 bit memory accesses
	 * to the main memory.
	 */
#if (_MIPS_ISA == _MIPS_ISA_MIPS3) || (_MIPS_ISA == _MIPS_ISA_MIPS4)
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
		 "=r" (dummy2),
		 "=r" (dummy3)
		:"0" (page),
		 "1" (PAGE_SIZE/8),
		 "2" (pte_val(BAD_PAGE)));
#else /* (_MIPS_ISA == _MIPS_ISA_MIPS1) || (_MIPS_ISA == _MIPS_ISA_MIPS2) */
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

pte_t __bad_page(void)
{
	extern char empty_bad_page[PAGE_SIZE];
	unsigned long page = (unsigned long)empty_bad_page;

	clear_page(page);
	return pte_mkdirty(mk_pte(page, PAGE_SHARED));
}

void show_mem(void)
{
	int i, free = 0, total = 0, reserved = 0;
	int shared = 0;

	printk("Mem-info:\n");
	show_free_areas();
	printk("Free swap:       %6dkB\n", nr_swap_pages<<(PAGE_SHIFT-10));
	i = max_mapnr;
	while (i-- > 0) {
		total++;
		if (PageReserved(mem_map+i))
			reserved++;
		else if (!atomic_read(&mem_map[i].count))
			free++;
		else
			shared += atomic_read(&mem_map[i].count) - 1;
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

__initfunc(unsigned long paging_init(unsigned long start_mem, unsigned long end_mem))
{
	pgd_init((unsigned long)swapper_pg_dir);
	return free_area_init(start_mem, end_mem);
}

__initfunc(void mem_init(unsigned long start_mem, unsigned long end_mem))
{
	int codepages = 0;
	int datapages = 0;
	unsigned long tmp;
	extern int _etext, _ftext;

#ifdef CONFIG_MIPS_JAZZ
	if (mips_machgroup == MACH_GROUP_JAZZ)
		start_mem = vdma_init(start_mem, end_mem);
#endif

	end_mem &= PAGE_MASK;
	max_mapnr = num_physpages = MAP_NR(end_mem);
	high_memory = (void *)end_mem;

	/* clear the zero-page */
	clear_page((unsigned long)empty_zero_page);

	/* mark usable pages in the mem_map[] */
	start_mem = PAGE_ALIGN(start_mem);

	for(tmp = MAP_NR(start_mem);tmp < max_mapnr;tmp++)
		clear_bit(PG_reserved, &mem_map[tmp].flags);


#ifdef CONFIG_SGI
	prom_fixup_mem_map(start_mem, (unsigned long)high_memory);
#endif

	for (tmp = PAGE_OFFSET; tmp < end_mem; tmp += PAGE_SIZE) {
		/*
		 * This is only for PC-style DMA.  The onboard DMA
		 * of Jazz and Tyne machines is completly different and
		 * not handled via a flag in mem_map_t.
		 */
		if (tmp >= MAX_DMA_ADDRESS)
			clear_bit(PG_DMA, &mem_map[MAP_NR(tmp)].flags);
		if (PageReserved(mem_map+MAP_NR(tmp))) {
			if ((tmp < (unsigned long) &_etext) &&
			    (tmp >= (unsigned long) &_ftext))
				codepages++;
			else if ((tmp < start_mem) &&
				 (tmp > (unsigned long) &_etext))
				datapages++;
			continue;
		}
		atomic_set(&mem_map[MAP_NR(tmp)].count, 1);
#ifdef CONFIG_BLK_DEV_INITRD
		if (!initrd_start || (tmp < initrd_start || tmp >=
		    initrd_end))
#endif
			free_page(tmp);
	}
	tmp = nr_free_pages << PAGE_SHIFT;
	printk("Memory: %luk/%luk available (%dk kernel code, %dk data)\n",
		tmp >> 10,
		max_mapnr << (PAGE_SHIFT-10),
		codepages << (PAGE_SHIFT-10),
		datapages << (PAGE_SHIFT-10));

	return;
}

extern char __init_begin, __init_end;

void free_initmem(void)
{
	unsigned long addr;
        
	addr = (unsigned long)(&__init_begin);
	for (; addr < (unsigned long)(&__init_end); addr += PAGE_SIZE) {
		mem_map[MAP_NR(addr)].flags &= ~(1 << PG_reserved);
		atomic_set(&mem_map[MAP_NR(addr)].count, 1);
		free_page(addr);
	}
	printk("Freeing unused kernel memory: %dk freed\n",
	       (&__init_end - &__init_begin) >> 10);
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
	return;
}
