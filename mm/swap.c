/*
 *  linux/mm/swap.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 */

/*
 * This file contains the default values for the opereation of the
 * Linux VM subsystem. Finetuning documentation can be found in
 * linux/Documentation/sysctl/vm.txt.
 * Started 18.12.91
 * Swap aging added 23.2.95, Stephen Tweedie.
 * Buffermem limits added 12.3.98, Rik van Riel.
 */

#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/swap.h>
#include <linux/fs.h>
#include <linux/swapctl.h>
#include <linux/pagemap.h>
#include <linux/init.h>

#include <asm/dma.h>
#include <asm/system.h> /* for cli()/sti() */
#include <asm/uaccess.h> /* for copy_to/from_user */
#include <asm/bitops.h>
#include <asm/pgtable.h>

/*
 * We identify three levels of free memory.  We never let free mem
 * fall below the freepages.min except for atomic allocations.  We
 * start background swapping if we fall below freepages.high free
 * pages, and we begin intensive swapping below freepages.low.
 *
 * These values are there to keep GCC from complaining. Actual
 * initialization is done in mm/page_alloc.c or arch/sparc(64)/mm/init.c.
 */
freepages_t freepages = {
	48,	/* freepages.min */
	72,	/* freepages.low */
	96	/* freepages.high */
};

/* We track the number of pages currently being asynchronously swapped
   out, so that we don't try to swap TOO many pages out at once */
atomic_t nr_async_pages = ATOMIC_INIT(0);

/*
 * Constants for the page aging mechanism: the maximum age (actually,
 * the maximum "youthfulness"); the quanta by which pages rejuvenate
 * and age; and the initial age for new pages. 
 */

swap_control_t swap_control = {
	20, 3, 1, 3,		/* Page aging */
	32, 4,			/* Aging cluster */
	8192, 8192,		/* Pageout and bufferout weights */
};

swapstat_t swapstats = {0};

buffer_mem_t buffer_mem = {
	6,	/* minimum percent buffer + cache memory */
	20,	/* borrow percent buffer + cache memory */
	90	/* maximum percent buffer + cache memory */
};

