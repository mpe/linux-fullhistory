/*
 * memory.c: PROM library functions for acquiring/using memory descriptors
 *           given to us from the ARCS firmware.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 *
 * $Id: memory.c,v 1.2 1998/03/27 08:53:47 ralf Exp $
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/swap.h>

#include <asm/sgialib.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/bootinfo.h>

/* #define DEBUG */

__initfunc(struct linux_mdesc *prom_getmdesc(struct linux_mdesc *curr))
{
	return romvec->get_mdesc(curr);
}

#ifdef DEBUG /* convenient for debugging */
static char *mtypes[8] = {
	"Exception Block",
	"ARCS Romvec Page",
	"Free/Contig RAM",
	"Generic Free RAM",
	"Bad Memory",
	"Standlong Program Pages",
	"ARCS Temp Storage Area",
	"ARCS Permanent Storage Area"
};
#endif

static struct prom_pmemblock prom_pblocks[PROM_MAX_PMEMBLOCKS];

__initfunc(struct prom_pmemblock *prom_getpblock_array(void))
{
	return &prom_pblocks[0];
}

__initfunc(static void prom_setup_memupper(void))
{
	struct prom_pmemblock *p, *highest;

	for(p = prom_getpblock_array(), highest = 0; p->size != 0; p++) {
		if(p->base == 0xdeadbeef)
			prom_printf("WHEEE, bogus pmemblock\n");
		if(!highest || p->base > highest->base)
			highest = p;
	}
	mips_memory_upper = highest->base + highest->size;
#ifdef DEBUG
	prom_printf("prom_setup_memupper: mips_memory_upper = %08lx\n",
		    mips_memory_upper);
#endif
}

__initfunc(void prom_meminit(void))
{
	struct linux_mdesc *p;
	int totram;
	int i = 0;

	p = prom_getmdesc(PROM_NULL_MDESC);
#ifdef DEBUG
	prom_printf("ARCS MEMORY DESCRIPTOR dump:\n");
	while(p) {
		prom_printf("[%d,%p]: base<%08lx> pages<%08lx> type<%s>\n",
			    i, p, p->base, p->pages, mtypes[p->type]);
		p = prom_getmdesc(p);
		i++;
	}
#endif
	p = prom_getmdesc(PROM_NULL_MDESC);
	totram = 0;
	i = 0;
	while(p) {
		if(p->type == free || p->type == fcontig) {
			prom_pblocks[i].base =
				((p->base<<PAGE_SHIFT) + 0x80000000);
			prom_pblocks[i].size = p->pages << PAGE_SHIFT;
			totram += prom_pblocks[i].size;
#ifdef DEBUG
			prom_printf("free_chunk[%d]: base=%08lx size=%d\n",
				    i, prom_pblocks[i].base,
				    prom_pblocks[i].size);
#endif
			i++;
		}
		p = prom_getmdesc(p);
	}
	prom_pblocks[i].base = 0xdeadbeef;
	prom_pblocks[i].size = 0; /* indicates last elem. of array */
	printk("PROMLIB: Total free ram %d bytes (%dK,%dMB)\n",
		    totram, (totram/1024), (totram/1024/1024));

	/* Setup upper physical memory bound. */
	prom_setup_memupper();
}

/* Called from mem_init() to fixup the mem_map page settings. */
__initfunc(void prom_fixup_mem_map(unsigned long start, unsigned long end))
{
	struct prom_pmemblock *p;
	int i, nents;

	/* Determine number of pblockarray entries. */
	p = prom_getpblock_array();
	for(i = 0; p[i].size; i++)
		;
	nents = i;
	while(start < end) {
		for(i = 0; i < nents; i++) {
			if((start >= (p[i].base)) &&
			   (start < (p[i].base + p[i].size))) {
				start = p[i].base + p[i].size;
				start &= PAGE_MASK;
				continue;
			}
		}
		set_bit(PG_reserved, &mem_map[MAP_NR(start)].flags);
		start += PAGE_SIZE;
	}
}
