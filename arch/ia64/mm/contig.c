/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1998-2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *	Stephane Eranian <eranian@hpl.hp.com>
 * Copyright (C) 2000, Rohit Seth <rohit.seth@intel.com>
 * Copyright (C) 1999 VA Linux Systems
 * Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 * Copyright (C) 2003 Silicon Graphics, Inc. All rights reserved.
 *
 * Routines used by ia64 machines with contiguous (or virtually contiguous)
 * memory.
 */
#include <linux/config.h>
#include <linux/bootmem.h>
#include <linux/efi.h>
#include <linux/mm.h>
#include <linux/swap.h>

#include <asm/meminit.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/sections.h>

/**
 * show_mem - display a memory statistics summary
 *
 * Just walks the pages in the system and describes where they're allocated.
 */
void
show_mem (void)
{
	int i, total = 0, reserved = 0;
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
		else if (page_count(mem_map + i))
			shared += page_count(mem_map + i) - 1;
	}
	printk("%d pages of RAM\n", total);
	printk("%d reserved pages\n", reserved);
	printk("%d pages shared\n", shared);
	printk("%d pages swap cached\n", cached);
	printk("%ld pages in page table cache\n", pgtable_cache_size);
}

/* physical address where the bootmem map is located */
unsigned long bootmap_start;

/**
 * find_max_pfn - adjust the maximum page number callback
 * @start: start of range
 * @end: end of range
 * @arg: address of pointer to global max_pfn variable
 *
 * Passed as a callback function to efi_memmap_walk() to determine the highest
 * available page frame number in the system.
 */
int
find_max_pfn (unsigned long start, unsigned long end, void *arg)
{
	unsigned long *max_pfnp = arg, pfn;

	pfn = (PAGE_ALIGN(end - 1) - PAGE_OFFSET) >> PAGE_SHIFT;
	if (pfn > *max_pfnp)
		*max_pfnp = pfn;
	return 0;
}

/**
 * find_bootmap_location - callback to find a memory area for the bootmap
 * @start: start of region
 * @end: end of region
 * @arg: unused callback data
 *
 * Find a place to put the bootmap and return its starting address in
 * bootmap_start.  This address must be page-aligned.
 */
int
find_bootmap_location (unsigned long start, unsigned long end, void *arg)
{
	unsigned long needed = *(unsigned long *)arg;
	unsigned long range_start, range_end, free_start;
	int i;

#if IGNORE_PFN0
	if (start == PAGE_OFFSET) {
		start += PAGE_SIZE;
		if (start >= end)
			return 0;
	}
#endif

	free_start = PAGE_OFFSET;

	for (i = 0; i < num_rsvd_regions; i++) {
		range_start = max(start, free_start);
		range_end   = min(end, rsvd_region[i].start & PAGE_MASK);

		if (range_end <= range_start)
			continue; /* skip over empty range */

	       	if (range_end - range_start >= needed) {
			bootmap_start = __pa(range_start);
			return 1;	/* done */
		}

		/* nothing more available in this segment */
		if (range_end == end)
			return 0;

		free_start = PAGE_ALIGN(rsvd_region[i].end);
	}
	return 0;
}

/**
 * find_memory - setup memory map
 *
 * Walk the EFI memory map and find usable memory for the system, taking
 * into account reserved areas.
 */
void
find_memory (void)
{
	unsigned long bootmap_size;

	reserve_memory();

	/* first find highest page frame number */
	max_pfn = 0;
	efi_memmap_walk(find_max_pfn, &max_pfn);

	/* how many bytes to cover all the pages */
	bootmap_size = bootmem_bootmap_pages(max_pfn) << PAGE_SHIFT;

	/* look for a location to hold the bootmap */
	bootmap_start = ~0UL;
	efi_memmap_walk(find_bootmap_location, &bootmap_size);
	if (bootmap_start == ~0UL)
		panic("Cannot find %ld bytes for bootmap\n", bootmap_size);

	bootmap_size = init_bootmem(bootmap_start >> PAGE_SHIFT, max_pfn);

	/* Free all available memory, then mark bootmem-map as being in use. */
	efi_memmap_walk(filter_rsvd_memory, free_bootmem);
	reserve_bootmem(bootmap_start, bootmap_size);

	find_initrd();
}
