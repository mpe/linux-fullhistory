/*
 * memory.c: memory initialisation code.
 *
 * Copyright (C) 1998 Harald Koerfgen, Frieder Streffer and Paul M. Antoine
 *
 * $Id: $
 */
#include <asm/addrspace.h>
#include <linux/init.h>
#include <linux/string.h>
#include "prom.h"

typedef struct {
	int pagesize;
	unsigned char bitmap[0];
} memmap;

extern int (*rex_getbitmap)(memmap *);

#undef PROM_DEBUG

#ifdef PROM_DEBUG
extern int (*prom_printf)(char *, ...);
#endif

extern unsigned long mips_memory_upper;

volatile unsigned long mem_err = 0;	/* So we know an error occured */

/*
 * Probe memory in 4MB chunks, waiting for an error to tell us we've fallen
 * off the end of real memory.  Only suitable for the 2100/3100's (PMAX).
 */

#define CHUNK_SIZE 0x400000

unsigned long __init pmax_get_memory_size(void)
{
	volatile unsigned char *memory_page, dummy;
	char	old_handler[0x80];
	extern char genexcept_early;

	/* Install exception handler */
	memcpy(&old_handler, (void *)(KSEG0 + 0x80), 0x80);
	memcpy((void *)(KSEG0 + 0x80), &genexcept_early, 0x80);

	/* read unmapped and uncached (KSEG1)
	 * DECstations have at least 4MB RAM
	 * Assume less than 480MB of RAM, as this is max for 5000/2xx
	 * FIXME this should be replaced by the first free page!
	 */
	for (memory_page = (unsigned char *) KSEG1 + CHUNK_SIZE;
	     (mem_err== 0) && (memory_page < ((unsigned char *) KSEG1+0x1E000000));
  	     memory_page += CHUNK_SIZE) {
		dummy = *memory_page;
	}
	memcpy((void *)(KSEG0 + 0x80), &old_handler, 0x80);
	return (unsigned long)memory_page - KSEG1 - CHUNK_SIZE;
}

/*
 * Use the REX prom calls to get hold of the memory bitmap, and thence
 * determine memory size.
 */
unsigned long __init rex_get_memory_size(void)
{
	int i, bitmap_size;
	unsigned long mem_size = 0;
	memmap *bm;

	/* some free 64k */
	bm = (memmap *) 0x80028000;

	bitmap_size = rex_getbitmap(bm);

	for (i = 0; i < bitmap_size; i++) {
		/* FIXME: very simplistically only add full sets of pages */
		if (bm->bitmap[i] == 0xff)
			mem_size += (8 * bm->pagesize);
	}
	return (mem_size);
}

void __init prom_meminit(unsigned int magic)
{
	if (magic != REX_PROM_MAGIC)
		mips_memory_upper = KSEG0 + pmax_get_memory_size();
	else
		mips_memory_upper = KSEG0 + rex_get_memory_size();

#ifdef PROM_DEBUG
	prom_printf("mips_memory_upper: 0x%08x\n", mips_memory_upper);
#endif
}

/* Called from mem_init() to fixup the mem_map page settings. */
void __init prom_fixup_mem_map(unsigned long start, unsigned long end)
{
}

void prom_free_prom_memory (void)
{
}
