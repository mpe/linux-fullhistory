/*
 *  linux/mm/initmem.c
 *
 *  Copyright (C) 1999 Ingo Molnar
 *
 *  simple boot-time physical memory area allocator and
 *  free memory collector. It's used to deal with reserved
 *  system memory and memory holes as well.
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/kernel_stat.h>
#include <linux/swap.h>
#include <linux/swapctl.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <asm/dma.h>

/*
 * Pointer to a bitmap - the bits represent all physical memory pages
 * from physical address 0 to physical address end_mem.
 *
 * Access to this subsystem has to be serialized externally. (this is
 * true for the boot process anyway)
 */
unsigned long max_low_pfn;

static void * bootmem_map = NULL;

/*
 * Called once to set up the allocator itself.
 */
unsigned long __init init_bootmem (unsigned long start, unsigned long pages)
{
	unsigned long mapsize = (pages+7)/8;

	if (bootmem_map)
		BUG();
	bootmem_map = __va(start << PAGE_SHIFT);
	max_low_pfn = pages;

	/*
	 * Initially all pages are reserved - setup_arch() has to
	 * register free RAM areas explicitly.
	 */
	memset(bootmem_map, 0xff, mapsize);

	return mapsize;
}

/*
 * Marks a particular physical memory range as usable. Usable RAM
 * might be used for boot-time allocations - or it might get added
 * to the free page pool later on.
 */
void __init reserve_bootmem (unsigned long addr, unsigned long size)
{
	unsigned long i;
	/*
	 * round up, partially reserved pages are considered
	 * fully reserved.
	 */
	unsigned long end = (addr + size + PAGE_SIZE-1)/PAGE_SIZE;

	if (!bootmem_map) BUG();
	if (!size) BUG();

	if (end > max_low_pfn)
		BUG();
	for (i = addr/PAGE_SIZE; i < end; i++)
		if (test_and_set_bit(i, bootmem_map))
			BUG();
}

void __init free_bootmem (unsigned long addr, unsigned long size)
{
	unsigned long i;
	/*
	 * round down end of usable mem, partially free pages are
	 * considered reserved.
	 */
	unsigned long end = (addr + size)/PAGE_SIZE;

	if (!bootmem_map) BUG();
	if (!size) BUG();

	if (end > max_low_pfn)
		BUG();
	for (i = addr/PAGE_SIZE; i < end; i++) {
		if (!test_and_clear_bit(i, bootmem_map))
			BUG();
	}
}

/*
 * We 'merge' subsequent allocations to save space. We might 'lose'
 * some fraction of a page if allocations cannot be satisfied due to
 * size constraints on boxes where there is physical RAM space
 * fragmentation - in these cases * (mostly large memory boxes) this
 * is not a problem.
 *
 * On low memory boxes we get it right in 100% of the cases.
 */
static unsigned long last_pos = 0;
static unsigned long last_offset = 0;

/*
 * alignment has to be a power of 2 value.
 */
void * __init __alloc_bootmem (unsigned long size, unsigned long align, unsigned long goal)
{
	int area = 0;
	unsigned long i, start = 0, reserved;
	void *ret;
	unsigned long offset, remaining_size;
	unsigned long areasize, preferred;

	if (!bootmem_map) BUG();
	if (!size) BUG();

	/*
	 * We try to allocate bootmem pages above 'goal'
	 * first, then we try to allocate lower pages.
	 */
	if (goal) {
		preferred = goal >> PAGE_SHIFT;
		if (preferred >= max_low_pfn)
			preferred = 0;
	} else
		preferred = 0;

	areasize = (size+PAGE_SIZE-1)/PAGE_SIZE;

restart_scan:
	for (i = preferred; i < max_low_pfn; i++) {
		reserved = test_bit(i, bootmem_map);
		if (!reserved) {
			if (!area) {
				area = 1;
				start = i;
			}
			if (i - start + 1 == areasize)
				goto found;
		} else {
			area = 0;
			start = -1;
		}
	}
	if (preferred) {
		preferred = 0;
		goto restart_scan;
	}
	BUG();
found:
	if (start >= max_low_pfn)
		BUG();

	/*
	 * Is the next page of the previous allocation-end the start
	 * of this allocation's buffer? If yes then we can 'merge'
	 * the previous partial page with this allocation.
	 */
	if (last_offset && (last_pos+1 == start)) {
		offset = (last_offset+align-1) & ~(align-1);
		if (offset > PAGE_SIZE)
			BUG();
		remaining_size = PAGE_SIZE-offset;
		if (remaining_size > PAGE_SIZE)
			BUG();
		if (size < remaining_size) {
			areasize = 0;
			// last_pos unchanged
			last_offset = offset+size;
			ret = __va(last_pos*PAGE_SIZE + offset);
		} else {
			size -= remaining_size;
			areasize = (size+PAGE_SIZE-1)/PAGE_SIZE;
			ret = __va(last_pos*PAGE_SIZE + offset);
			last_pos = start+areasize-1;
			last_offset = size;
		}
 		last_offset &= ~PAGE_MASK;
	} else {
		last_pos = start + areasize - 1;
		last_offset = size & ~PAGE_MASK;
		ret = __va(start * PAGE_SIZE);
	}
	/*
	 * Reserve the area now:
	 */
	for (i = start; i < start+areasize; i++)
		if (test_and_set_bit(i, bootmem_map))
			BUG();

	return ret;
}

unsigned long __init free_all_bootmem (void)
{
	struct page * page;
	unsigned long i, count, total = 0;

	if (!bootmem_map) BUG();

	page = mem_map;
	count = 0;
	for (i = 0; i < max_low_pfn; i++, page++) {
		if (!test_bit(i, bootmem_map)) {
			count++;
			ClearPageReserved(page);
			set_page_count(page, 1);
			if (i >= (__pa(MAX_DMA_ADDRESS) >> PAGE_SHIFT))
				clear_bit(PG_DMA, &page->flags);
			__free_page(page);
		}
	}
	total += count;
	/*
	 * Now free the allocator bitmap itself, it's not
	 * needed anymore:
	 */
	page = mem_map + MAP_NR(bootmem_map);
	count = 0;
	for (i = 0; i < (max_low_pfn/8 + PAGE_SIZE-1)/PAGE_SIZE; i++,page++) {
		count++;
		ClearPageReserved(page);
		set_page_count(page, 1);
		__free_page(page);
	}
	total += count;
	bootmem_map = NULL;

	return total;
}
