/*
 *    Copyright (c) 1996 Paul Mackerras <paulus@cs.anu.edu.au>
 *      Changes to accomodate Power Macintoshes.
 *    Cort Dougan <cort@cs.nmt.edu>
 *      Rewrites.
 *    Grant Erickson <grant@lcse.umn.edu>
 *      General rework and split from mm/init.c.
 *
 *    Module name: mem_pieces.c
 *
 *    Description:
 *      Routines and data structures for manipulating and representing
 *      phyiscal memory extents (i.e. address/length pairs).
 *
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/blk.h>

#include <asm/page.h>
#include <asm/prom.h>

#include "mem_pieces.h"

extern char _start[], _end[];
extern char _stext[], etext[];

char *klimit = _end;

struct mem_pieces phys_avail;

static void mem_pieces_print(struct mem_pieces *);

/*
 * Scan a region for a piece of a given size with the required alignment.
 */
void __init *
mem_pieces_find(unsigned int size, unsigned int align)
{
	int i;
	unsigned a, e;
	struct mem_pieces *mp = &phys_avail;

	for (i = 0; i < mp->n_regions; ++i) {
		a = mp->regions[i].address;
		e = a + mp->regions[i].size;
		a = (a + align - 1) & -align;
		if (a + size <= e) {
			mem_pieces_remove(mp, a, size, 1);
			return __va(a);
		}
	}
	panic("Couldn't find %u bytes at %u alignment\n", size, align);
	
	return NULL;
}

/*
 * Remove some memory from an array of pieces
 */
void __init 
mem_pieces_remove(struct mem_pieces *mp, unsigned int start, unsigned int size,
		  int must_exist)
{
	int i, j;
	unsigned int end, rs, re;
	struct reg_property *rp;

	end = start + size;
	for (i = 0, rp = mp->regions; i < mp->n_regions; ++i, ++rp) {
		if (end > rp->address && start < rp->address + rp->size)
			break;
	}
	if (i >= mp->n_regions) {
		if (must_exist)
			printk("mem_pieces_remove: [%x,%x) not in any region\n",
			       start, end);
		return;
	}
	for (; i < mp->n_regions && end > rp->address; ++i, ++rp) {
		rs = rp->address;
		re = rs + rp->size;
		if (must_exist && (start < rs || end > re)) {
			printk("mem_pieces_remove: bad overlap [%x,%x) with",
			       start, end);
			mem_pieces_print(mp);
			must_exist = 0;
		}
		if (start > rs) {
			rp->size = start - rs;
			if (end < re) {
				/* need to split this entry */
				if (mp->n_regions >= MEM_PIECES_MAX)
					panic("eek... mem_pieces overflow");
				for (j = mp->n_regions; j > i + 1; --j)
					mp->regions[j] = mp->regions[j-1];
				++mp->n_regions;
				rp[1].address = end;
				rp[1].size = re - end;
			}
		} else {
			if (end < re) {
				rp->address = end;
				rp->size = re - end;
			} else {
				/* need to delete this entry */
				for (j = i; j < mp->n_regions - 1; ++j)
					mp->regions[j] = mp->regions[j+1];
				--mp->n_regions;
				--i;
				--rp;
			}
		}
	}
}

static void __init
mem_pieces_print(struct mem_pieces *mp)
{
	int i;

	for (i = 0; i < mp->n_regions; ++i)
		printk(" [%x, %x)", mp->regions[i].address,
		       mp->regions[i].address + mp->regions[i].size);
	printk("\n");
}

#if defined(CONFIG_APUS) || defined(CONFIG_ALL_PPC)
/*
 * Add some memory to an array of pieces
 */
void __init
mem_pieces_append(struct mem_pieces *mp, unsigned int start, unsigned int size)
{
	struct reg_property *rp;

	if (mp->n_regions >= MEM_PIECES_MAX)
		return;
	rp = &mp->regions[mp->n_regions++];
	rp->address = start;
	rp->size = size;
}
#endif

void __init
mem_pieces_sort(struct mem_pieces *mp)
{
	unsigned long a, s;
	int i, j;

	for (i = 1; i < mp->n_regions; ++i) {
		a = mp->regions[i].address;
		s = mp->regions[i].size;
		for (j = i - 1; j >= 0; --j) {
			if (a >= mp->regions[j].address)
				break;
			mp->regions[j+1] = mp->regions[j];
		}
		mp->regions[j+1].address = a;
		mp->regions[j+1].size = s;
	}
}

void __init
mem_pieces_coalesce(struct mem_pieces *mp)
{
	unsigned long a, s, ns;
	int i, j, d;

	d = 0;
	for (i = 0; i < mp->n_regions; i = j) {
		a = mp->regions[i].address;
		s = mp->regions[i].size;
		for (j = i + 1; j < mp->n_regions
			     && mp->regions[j].address - a <= s; ++j) {
			ns = mp->regions[j].address + mp->regions[j].size - a;
			if (ns > s)
				s = ns;
		}
		mp->regions[d].address = a;
		mp->regions[d].size = s;
		++d;
	}
	mp->n_regions = d;
}

/*
 * Set phys_avail to phys_mem less the kernel text/data/bss.
 */
void __init
set_phys_avail(struct mem_pieces *mp)
{
	unsigned long kstart, ksize;

	/*
	 * Initially, available phyiscal memory is equivalent to all
	 * physical memory.
	 */

	phys_avail = *mp;

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
}
