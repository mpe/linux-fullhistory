/*
 * BK Id: SCCS/s.init.c 1.36 09/22/01 14:03:09 paulus
 */
/*
 *  PowerPC version 
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Modifications by Paul Mackerras (PowerMac) (paulus@cs.anu.edu.au)
 *  and Cort Dougan (PReP) (cort@cs.nmt.edu)
 *    Copyright (C) 1996 Paul Mackerras
 *  Amiga/APUS changes by Jesper Skov (jskov@cygnus.co.uk).
 *
 *  Derived from "arch/i386/mm/init.c"
 *    Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/highmem.h>
#ifdef CONFIG_BLK_DEV_INITRD
#include <linux/blk.h>		/* for initrd_* */
#endif

#include <asm/pgalloc.h>
#include <asm/prom.h>
#include <asm/io.h>
#include <asm/mmu_context.h>
#include <asm/pgtable.h>
#include <asm/mmu.h>
#include <asm/smp.h>
#include <asm/machdep.h>
#include <asm/btext.h>
#include <asm/tlb.h>

#include "mem_pieces.h"
#include "mmu_decl.h"

#define MAX_LOW_MEM	(0xF0000000UL - KERNELBASE)

mmu_gather_t mmu_gathers[NR_CPUS];

void *end_of_DRAM;
unsigned long total_memory;
unsigned long total_lowmem;

int mem_init_done;
int init_bootmem_done;
int boot_mapsize;
unsigned long totalram_pages;
unsigned long totalhigh_pages;

extern char _end[];
extern char etext[], _stext[];
extern char __init_begin, __init_end;
extern char __prep_begin, __prep_end;
extern char __chrp_begin, __chrp_end;
extern char __pmac_begin, __pmac_end;
extern char __openfirmware_begin, __openfirmware_end;

#ifdef CONFIG_HIGHMEM
pte_t *kmap_pte;
pgprot_t kmap_prot;
#endif

void MMU_init(void);
void set_phys_avail(unsigned long total_ram);

/* XXX should be in current.h  -- paulus */
extern struct task_struct *current_set[NR_CPUS];

char *klimit = _end;
struct mem_pieces phys_avail;

extern char *sysmap; 
extern unsigned long sysmap_size;

/*
 * this tells the system to map all of ram with the segregs
 * (i.e. page tables) instead of the bats.
 * -- Cort
 */
int __map_without_bats;

/* max amount of RAM to use */
unsigned long __max_memory;
/* max amount of low RAM to map in */
unsigned long __max_low_memory = MAX_LOW_MEM;

int do_check_pgt_cache(int low, int high)
{
	int freed = 0;
	if (pgtable_cache_size > high) {
		do {
                        if (pgd_quicklist) {
				free_pgd_slow(get_pgd_fast());
				freed++;
			}
			if (pte_quicklist) {
				pte_free_slow(pte_alloc_one_fast(NULL, 0));
				freed++;
			}
		} while (pgtable_cache_size > low);
	}
	return freed;
}

void show_mem(void)
{
	int i,free = 0,total = 0,reserved = 0;
	int shared = 0, cached = 0;
	struct task_struct *p;
	int highmem = 0;

	printk("Mem-info:\n");
	show_free_areas();
	printk("Free swap:       %6dkB\n",nr_swap_pages<<(PAGE_SHIFT-10));
	i = max_mapnr;
	while (i-- > 0) {
		total++;
		if (PageHighMem(mem_map+i))
			highmem++;
		if (PageReserved(mem_map+i))
			reserved++;
		else if (PageSwapCache(mem_map+i))
			cached++;
		else if (!page_count(mem_map+i))
			free++;
		else
			shared += atomic_read(&mem_map[i].count) - 1;
	}
	printk("%d pages of RAM\n",total);
	printk("%d pages of HIGHMEM\n", highmem);
	printk("%d free pages\n",free);
	printk("%d reserved pages\n",reserved);
	printk("%d pages shared\n",shared);
	printk("%d pages swap cached\n",cached);
	printk("%d pages in page table cache\n",(int)pgtable_cache_size);
	show_buffers();
	printk("%-8s %3s %8s %8s %8s %9s %8s", "Process", "Pid",
	       "Ctx", "Ctx<<4", "Last Sys", "pc", "task");
#ifdef CONFIG_SMP
	printk(" %3s", "CPU");
#endif /* CONFIG_SMP */
	printk("\n");
	for_each_task(p)
	{
		printk("%-8.8s %3d %8ld %8ld %8ld %c%08lx %08lx ",
		       p->comm,p->pid,
		       (p->mm)?p->mm->context:0,
		       (p->mm)?(p->mm->context<<4):0,
		       p->thread.last_syscall,
		       (p->thread.regs)?user_mode(p->thread.regs) ? 'u' : 'k' : '?',
		       (p->thread.regs)?p->thread.regs->nip:0,
		       (ulong)p);
		{
			int iscur = 0;
#ifdef CONFIG_SMP
			printk("%3d ", p->processor);
			if ( (p->processor != NO_PROC_ID) &&
			     (p == current_set[p->processor]) )
			{
				iscur = 1;
				printk("current");
			}
#else
			if ( p == current )
			{
				iscur = 1;
				printk("current");
			}
			
			if ( p == last_task_used_math )
			{
				if ( iscur )
					printk(",");
				printk("last math");
			}			
#endif /* CONFIG_SMP */
			printk("\n");
		}
	}
}

void si_meminfo(struct sysinfo *val)
{
	val->totalram = totalram_pages;
	val->sharedram = 0;
	val->freeram = nr_free_pages();
	val->bufferram = atomic_read(&buffermem_pages);
	val->totalhigh = totalhigh_pages;
	val->freehigh = nr_free_highpages();
	val->mem_unit = PAGE_SIZE;
}

/* Free up now-unused memory */
static void free_sec(unsigned long start, unsigned long end, const char *name)
{
	unsigned long cnt = 0;

	while (start < end) {
		ClearPageReserved(virt_to_page(start));
		set_page_count(virt_to_page(start), 1);
		free_page(start);
		cnt++;
		start += PAGE_SIZE;
 	}
	if (cnt) {
		printk(" %ldk %s", cnt << (PAGE_SHIFT - 10), name);
		totalram_pages += cnt;
	}
}

void free_initmem(void)
{
#define FREESEC(TYPE) \
	free_sec((unsigned long)(&__ ## TYPE ## _begin), \
		 (unsigned long)(&__ ## TYPE ## _end), \
		 #TYPE);

	printk ("Freeing unused kernel memory:");
	FREESEC(init);
	if (_machine != _MACH_Pmac)
		FREESEC(pmac);
	if (_machine != _MACH_chrp)
		FREESEC(chrp);
	if (_machine != _MACH_prep)
		FREESEC(prep);
	if (!have_of)
		FREESEC(openfirmware);
 	printk("\n");
#undef FREESEC
}

#ifdef CONFIG_BLK_DEV_INITRD
void free_initrd_mem(unsigned long start, unsigned long end)
{
	printk ("Freeing initrd memory: %ldk freed\n", (end - start) >> 10);

	for (; start < end; start += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(start));
		set_page_count(virt_to_page(start), 1);
		free_page(start);
		totalram_pages++;
	}
}
#endif

/*
 * Check for command-line options that affect what MMU_init will do.
 */
void MMU_setup(void)
{
	/* Check for nobats option (used in mapin_ram). */
	if (strstr(cmd_line, "nobats")) {
		__map_without_bats = 1;
	}

	/* Look for mem= option on command line */
	if (strstr(cmd_line, "mem=")) {
		char *p, *q;
		unsigned long maxmem = 0;

		for (q = cmd_line; (p = strstr(q, "mem=")) != 0; ) {
			q = p + 4;
			if (p > cmd_line && p[-1] != ' ')
				continue;
			maxmem = simple_strtoul(q, &q, 0);
			if (*q == 'k' || *q == 'K') {
				maxmem <<= 10;
				++q;
			} else if (*q == 'm' || *q == 'M') {
				maxmem <<= 20;
				++q;
			}
		}
		__max_memory = maxmem;
	}
}

/*
 * MMU_init sets up the basic memory mappings for the kernel,
 * including both RAM and possibly some I/O regions,
 * and sets up the page tables and the MMU hardware ready to go.
 */
void __init MMU_init(void)
{
	if (ppc_md.progress)
		ppc_md.progress("MMU:enter", 0x111);

	/* parse args from command line */
	MMU_setup();

	/*
	 * Figure out how much memory we have, how much
	 * is lowmem, and how much is highmem.
	 */
	total_memory = ppc_md.find_end_of_memory();

	if (__max_memory && total_memory > __max_memory)
		total_memory = __max_memory;
	total_lowmem = total_memory;
	if (total_lowmem > __max_low_memory) {
		total_lowmem = __max_low_memory;
#ifndef CONFIG_HIGHMEM
		total_memory = total_lowmem;
#endif /* CONFIG_HIGHMEM */
	}
	end_of_DRAM = __va(total_lowmem);
	set_phys_avail(total_lowmem);

	/* Initialize the MMU hardware */
	if (ppc_md.progress)
		ppc_md.progress("MMU:hw init", 0x300);
	MMU_init_hw();

	/* Map in all of RAM starting at KERNELBASE */
	if (ppc_md.progress)
		ppc_md.progress("MMU:mapin", 0x301);
	mapin_ram();

#ifdef CONFIG_HIGHMEM
	ioremap_base = PKMAP_BASE;
#else
	ioremap_base = 0xfe000000UL;	/* for now, could be 0xfffff000 */
#endif /* CONFIG_HIGHMEM */
	ioremap_bot = ioremap_base;

	/* Map in I/O resources */
	if (ppc_md.progress)
		ppc_md.progress("MMU:setio", 0x302);
	if (ppc_md.setup_io_mappings)
		ppc_md.setup_io_mappings();

	/* Initialize the context management stuff */
	mmu_context_init();

	if (ppc_md.progress)
		ppc_md.progress("MMU:exit", 0x211);

#ifdef CONFIG_BOOTX_TEXT
	/* Must be done last, or ppc_md.progress will die */
	if (have_of)
		map_boot_text();
#endif
}

/* This is only called until mem_init is done. */
void __init *early_get_page(void)
{
	void *p;

	if (init_bootmem_done) {
		p = alloc_bootmem_pages(PAGE_SIZE);
	} else {
		p = mem_pieces_find(PAGE_SIZE, PAGE_SIZE);
	}
	return p;
}

/*
 * Initialize the bootmem system and give it all the memory we
 * have available.
 */
void __init do_init_bootmem(void)
{
	unsigned long start, size;
	int i;

	/*
	 * Find an area to use for the bootmem bitmap.
	 * We look for the first area which is at least
	 * 128kB in length (128kB is enough for a bitmap
	 * for 4GB of memory, using 4kB pages), plus 1 page
	 * (in case the address isn't page-aligned).
	 */
	start = 0;
	size = 0;
	for (i = 0; i < phys_avail.n_regions; ++i) {
		unsigned long a = phys_avail.regions[i].address;
		unsigned long s = phys_avail.regions[i].size;
		if (s <= size)
			continue;
		start = a;
		size = s;
		if (s >= 33 * PAGE_SIZE)
			break;
	}
	start = PAGE_ALIGN(start);

	boot_mapsize = init_bootmem(start >> PAGE_SHIFT,
				    total_lowmem >> PAGE_SHIFT);

	/* remove the bootmem bitmap from the available memory */
	mem_pieces_remove(&phys_avail, start, boot_mapsize, 1);

	/* add everything in phys_avail into the bootmem map */
	for (i = 0; i < phys_avail.n_regions; ++i)
		free_bootmem(phys_avail.regions[i].address,
			     phys_avail.regions[i].size);

	init_bootmem_done = 1;
}

/*
 * paging_init() sets up the page tables - in fact we've already done this.
 */
void __init paging_init(void)
{
	unsigned long zones_size[MAX_NR_ZONES], i;

#ifdef CONFIG_HIGHMEM
	map_page(PKMAP_BASE, 0, 0);	/* XXX gross */
	pkmap_page_table = pte_offset(pmd_offset(pgd_offset_k(PKMAP_BASE), PKMAP_BASE), PKMAP_BASE);
	map_page(KMAP_FIX_BEGIN, 0, 0);	/* XXX gross */
	kmap_pte = pte_offset(pmd_offset(pgd_offset_k(KMAP_FIX_BEGIN), KMAP_FIX_BEGIN), KMAP_FIX_BEGIN);
	kmap_prot = PAGE_KERNEL;
#endif /* CONFIG_HIGHMEM */

	/*
	 * All pages are DMA-able so we put them all in the DMA zone.
	 */
	zones_size[ZONE_DMA] = total_lowmem >> PAGE_SHIFT;
	for (i = 1; i < MAX_NR_ZONES; i++)
		zones_size[i] = 0;

#ifdef CONFIG_HIGHMEM
	zones_size[ZONE_HIGHMEM] = (total_memory - total_lowmem) >> PAGE_SHIFT;
#endif /* CONFIG_HIGHMEM */

	free_area_init(zones_size);
}

void __init mem_init(void)
{
	unsigned long addr;
	int codepages = 0;
	int datapages = 0;
	int initpages = 0;
#ifdef CONFIG_HIGHMEM
	unsigned long highmem_mapnr;

	highmem_mapnr = total_lowmem >> PAGE_SHIFT;
	highmem_start_page = mem_map + highmem_mapnr;
	max_mapnr = total_memory >> PAGE_SHIFT;
#else
	max_mapnr = max_low_pfn;
#endif /* CONFIG_HIGHMEM */

	high_memory = (void *) __va(max_low_pfn * PAGE_SIZE);
	num_physpages = max_mapnr;	/* RAM is assumed contiguous */

	totalram_pages += free_all_bootmem();

#ifdef CONFIG_BLK_DEV_INITRD
	/* if we are booted from BootX with an initial ramdisk,
	   make sure the ramdisk pages aren't reserved. */
	if (initrd_start) {
		for (addr = initrd_start; addr < initrd_end; addr += PAGE_SIZE)
			ClearPageReserved(virt_to_page(addr));
	}
#endif /* CONFIG_BLK_DEV_INITRD */

#if defined(CONFIG_ALL_PPC)	
	/* mark the RTAS pages as reserved */
	if ( rtas_data )
		for (addr = (ulong)__va(rtas_data);
		     addr < PAGE_ALIGN((ulong)__va(rtas_data)+rtas_size) ;
		     addr += PAGE_SIZE)
			SetPageReserved(virt_to_page(addr));
#endif /* defined(CONFIG_ALL_PPC) */
	if ( sysmap )
		for (addr = (unsigned long)sysmap;
		     addr < PAGE_ALIGN((unsigned long)sysmap+sysmap_size) ;
		     addr += PAGE_SIZE)
			SetPageReserved(virt_to_page(addr));
	
	for (addr = PAGE_OFFSET; addr < (unsigned long)end_of_DRAM;
	     addr += PAGE_SIZE) {
		if (!PageReserved(virt_to_page(addr)))
			continue;
		if (addr < (ulong) etext)
			codepages++;
		else if (addr >= (unsigned long)&__init_begin
			 && addr < (unsigned long)&__init_end)
			initpages++;
		else if (addr < (ulong) klimit)
			datapages++;
	}

#ifdef CONFIG_HIGHMEM
	{
		unsigned long pfn;

		for (pfn = highmem_mapnr; pfn < max_mapnr; ++pfn) {
			struct page *page = mem_map + pfn;

			ClearPageReserved(page);
			set_bit(PG_highmem, &page->flags);
			atomic_set(&page->count, 1);
			__free_page(page);
			totalhigh_pages++;
		}
		totalram_pages += totalhigh_pages;
	}
#endif /* CONFIG_HIGHMEM */

        printk("Memory: %luk available (%dk kernel code, %dk data, %dk init, %ldk highmem)\n",
	       (unsigned long)nr_free_pages()<< (PAGE_SHIFT-10),
	       codepages<< (PAGE_SHIFT-10), datapages<< (PAGE_SHIFT-10),
	       initpages<< (PAGE_SHIFT-10),
	       (unsigned long) (totalhigh_pages << (PAGE_SHIFT-10)));
	if (sysmap)
		printk("System.map loaded at 0x%08x for debugger, size: %ld bytes\n",
			(unsigned int)sysmap, sysmap_size);
	mem_init_done = 1;
}

/*
 * Set phys_avail to the amount of physical memory,
 * less the kernel text/data/bss.
 */
void __init
set_phys_avail(unsigned long total_memory)
{
	unsigned long kstart, ksize;

	/*
	 * Initially, available physical memory is equivalent to all
	 * physical memory.
	 */

	phys_avail.regions[0].address = 0;
	phys_avail.regions[0].size = total_memory;
	phys_avail.n_regions = 1;

	/*
	 * Map out the kernel text/data/bss from the available physical
	 * memory.
	 */

	kstart = __pa(_stext);	/* should be 0 */
	ksize = PAGE_ALIGN(klimit - _stext);

	mem_pieces_remove(&phys_avail, kstart, ksize, 0);
	mem_pieces_remove(&phys_avail, 0, 0x4000, 0);

#if defined(CONFIG_BLK_DEV_INITRD)
	/* Remove the init RAM disk from the available memory. */
	if (initrd_start) {
		mem_pieces_remove(&phys_avail, __pa(initrd_start),
				  initrd_end - initrd_start, 1);
	}
#endif /* CONFIG_BLK_DEV_INITRD */
#ifdef CONFIG_ALL_PPC
	/* remove the RTAS pages from the available memory */
	if (rtas_data)
		mem_pieces_remove(&phys_avail, rtas_data, rtas_size, 1);
	/* remove the sysmap pages from the available memory */
	if (sysmap)
		mem_pieces_remove(&phys_avail, __pa(sysmap), sysmap_size, 1);
#endif /* CONFIG_ALL_PPC */
}

/* Mark some memory as reserved by removing it from phys_avail. */
void __init reserve_phys_mem(unsigned long start, unsigned long size)
{
	mem_pieces_remove(&phys_avail, start, size, 1);
}

void flush_page_to_ram(struct page *page)
{
	unsigned long vaddr = (unsigned long) kmap(page);
	__flush_page_to_ram(vaddr);
	kunmap(page);
}

/*
 * set_pte stores a linux PTE into the linux page table.
 * On machines which use an MMU hash table we avoid changing the
 * _PAGE_HASHPTE bit.
 */
void set_pte(pte_t *ptep, pte_t pte)
{
#if _PAGE_HASHPTE != 0
	pte_update(ptep, ~_PAGE_HASHPTE, pte_val(pte) & ~_PAGE_HASHPTE);
#else
	*ptep = pte;
#endif
}
