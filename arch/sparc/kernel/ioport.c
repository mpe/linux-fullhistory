/* $Id: ioport.c,v 1.24 1997/04/10 03:02:32 davem Exp $
 * ioport.c:  Simple io mapping allocator.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1995 Miguel de Icaza (miguel@nuclecu.unam.mx)
 *
 * The routines in this file should be changed for a memory allocator
 * that would be setup just like NetBSD does : you create regions that
 * are administered by a general purpose allocator, and then you call
 * that allocator with your handle and the block size instead of this
 * weak stuff.
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/mm.h>

#include <asm/io.h>
#include <asm/vaddrs.h>
#include <asm/oplib.h>
#include <asm/page.h>
#include <asm/pgtable.h>

/* This points to the next to use virtual memory for io mappings */
static unsigned long dvma_next_free   = DVMA_VADDR;
unsigned long sparc_iobase_vaddr = IOBASE_VADDR;

/*
 * sparc_alloc_io:
 * Map and allocates an obio device.
 * Implements a simple linear allocator, you can force the function
 * to use your own mapping, but in practice this should not be used.
 *
 * Input:
 *  address: the obio address to map
 *  virtual: if non zero, specifies a fixed virtual address where
 *           the mapping should take place.
 *  len:     the length of the mapping
 *  bus_type: The bus on which this io area sits.
 *
 * Returns:
 *  The virtual address where the mapping actually took place.
 */

void *sparc_alloc_io (u32 address, void *virtual, int len, char *name,
		      u32 bus_type, int rdonly)
{
	unsigned long vaddr, base_address;
	unsigned long addr = (unsigned long) address;
	unsigned long offset = (addr & (~PAGE_MASK));

	if (virtual) {
		vaddr = (unsigned long) virtual;

		len += offset;
		if(((unsigned long) virtual + len) > (IOBASE_VADDR + IOBASE_LEN)) {
			prom_printf("alloc_io: Mapping outside IOBASE area\n");
			prom_halt();
		}
		if(check_region ((vaddr | offset), len)) {
			prom_printf("alloc_io: 0x%lx is already in use\n", vaddr);
			prom_halt();
		}

		/* Tell Linux resource manager about the mapping */
		request_region ((vaddr | offset), len, name);
	} else {
		vaddr = occupy_region(sparc_iobase_vaddr, IOBASE_END,
				(offset + len + PAGE_SIZE-1) & PAGE_MASK, PAGE_SIZE, name);
		if (vaddr == 0) {
			/* Usually we cannot see printks in this case. */
			prom_printf("alloc_io: cannot occupy %d region\n", len);
			prom_halt();
		}
	}

	base_address = vaddr;
	/* Do the actual mapping */
	for (; len > 0; len -= PAGE_SIZE) {
		mapioaddr(addr, vaddr, bus_type, rdonly);
		vaddr += PAGE_SIZE;
		addr += PAGE_SIZE;
	}

	return (void *) (base_address | offset);
}

void sparc_free_io (void *virtual, int len)
{
	unsigned long vaddr = (unsigned long) virtual & PAGE_MASK;
	unsigned long plen = (((unsigned long)virtual & ~PAGE_MASK) + len + PAGE_SIZE-1) & PAGE_MASK;

	release_region(vaddr, plen);

	for (; plen != 0;) {
		plen -= PAGE_SIZE;
		unmapioaddr(vaddr + plen);
	}
}

/* Does DVMA allocations with PAGE_SIZE granularity.  How this basically
 * works is that the ESP chip can do DVMA transfers at ANY address with
 * certain size and boundary restrictions.  But other devices that are
 * attached to it and would like to do DVMA have to set things up in
 * a special way, if the DVMA sees a device attached to it transfer data
 * at addresses above DVMA_VADDR it will grab them, this way it does not
 * now have to know the peculiarities of where to read the Lance data
 * from. (for example)
 */
void *_sparc_dvma_malloc (int len, char *name)
{
	unsigned long vaddr, base_address;

	vaddr = dvma_next_free;
	if(check_region (vaddr, len)) {
		prom_printf("alloc_dma: 0x%lx is already in use\n", vaddr);
		prom_halt();
	}
	if(vaddr + len > (DVMA_VADDR + DVMA_LEN)) {
		prom_printf("alloc_dvma: out of dvma memory\n");
		prom_halt();
	}

	/* Basically these can be mapped just like any old
	 * IO pages, cacheable bit off, etc.  The physical
	 * pages are now mapped dynamically to save space.
	 */
	base_address = vaddr;
	mmu_map_dma_area(base_address, len);
	/* Assign the memory area. */
	dvma_next_free = PAGE_ALIGN(dvma_next_free+len);

	request_region(base_address, len, name);

	return (void *) base_address;
}
