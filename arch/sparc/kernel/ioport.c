/* ioport.c:  Simple io mapping allocator.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1995 Miguel de Icaza (miguel@nuclecu.unam.mx)
 *
 * The routines in this file should be changed for a memory allocator
 * that would be setup just like NetBSD does : you create regions that
 * are administered by a general purpose allocator, and then you call
 * that allocator with your handle and the block size instead of this
 * weak stuff.
 *
 * XXX No joke, this needs to be rewritten badly. XXX
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/ioport.h>

#include <asm/io.h>
#include <asm/vaddrs.h>
#include <asm/oplib.h>

/* This points to the next to use virtual memory for io mappings */
static long next_free_region = IOBASE_VADDR;
static long dvma_next_free   = DVMA_VADDR;

/*
 * sparc_alloc_dev:
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

void *sparc_alloc_io (void *address, void *virtual, int len, char *name,
		      int bus_type, int rdonly)
{
	unsigned long vaddr, base_address;
	unsigned long addr = (unsigned long) address;


	if (virtual){
		vaddr = (unsigned long) virtual;
	} else {
		vaddr = next_free_region;
	}
	
	if (((unsigned long) virtual + len) > (IOBASE_VADDR + IOBASE_LEN)){
		printk ("alloc_io: Mapping ouside IOBASE area\n");
		prom_halt ();
	}
	if (check_region (vaddr, len)){
		printk ("alloc_io: 0x%lx is already in use\n", vaddr);
		prom_halt ();
	}

	/* Tell Linux resource manager about the mapping */
	request_region (vaddr, len, name);

	base_address = vaddr;
	/* Do the actual mapping */
	for (; len > 0; len -= PAGE_SIZE){
		mapioaddr (addr, vaddr, bus_type, rdonly);
		vaddr += PAGE_SIZE;
		addr  += PAGE_SIZE;
		if (!virtual)
			next_free_region += PAGE_SIZE;
	}
	return (void *) base_address;
}

/* Does DVMA allocations with PAGE_SIZE granulatity */
void *sparc_dvma_malloc (int len, char *name)
{
	unsigned long vaddr, base_address;

	vaddr = dvma_next_free;
	if (check_region (vaddr, len)){
		printk ("alloc_dma: 0x%lx is already in use\n", vaddr);
		prom_halt ();
	}
	if (vaddr + len > (DVMA_VADDR + DVMA_LEN)){
		printk ("alloc_dvma: out of dvma memory\n");
		prom_halt ();
	}

	/* Tell Linux resource manager about the mapping */
	request_region (vaddr, len, name);

	base_address = vaddr;
	/* Assign the memory area and remove the cache bit */
	/* XXX EWWWWEE!!  Sun4c specific, fix now! XXX */
	for (; len > 0; len -= PAGE_SIZE){
		printk ("Len=%d\n", len);
		put_pte (vaddr, get_pte (vaddr) | PTE_NC);
		vaddr += PAGE_SIZE;
		dvma_next_free += PAGE_SIZE;
	}
	return (void *) base_address;
}
