/*
 *  linux/mm/swap.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 */

/*
 * This file contains the default values for the opereation of the
 * Linux VM subsystem. Fine-tuning documentation can be found in
 * linux/Documentation/sysctl/vm.txt.
 * Started 18.12.91
 * Swap aging added 23.2.95, Stephen Tweedie.
 * Buffermem limits added 12.3.98, Rik van Riel.
 */

#include <linux/mm.h>
#include <linux/kernel_stat.h>
#include <linux/swap.h>
#include <linux/swapctl.h>
#include <linux/pagemap.h>
#include <linux/init.h>

#include <asm/dma.h>
#include <asm/uaccess.h> /* for copy_to/from_user */
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
	96,	/* freepages.low */
	144	/* freepages.high */
};

/* How many pages do we try to swap or page in/out together? */
int page_cluster = 4; /* Default value modified in swap_setup() */

/* We track the number of pages currently being asynchronously swapped
   out, so that we don't try to swap TOO many pages out at once */
atomic_t nr_async_pages = ATOMIC_INIT(0);

buffer_mem_t buffer_mem = {
	2,	/* minimum percent buffer */
	10,	/* borrow percent buffer */
	60	/* maximum percent buffer */
};

buffer_mem_t page_cache = {
	2,	/* minimum percent page cache */
	15,	/* borrow percent page cache */
	75	/* maximum */
};

pager_daemon_t pager_daemon = {
	512,	/* base number for calculating the number of tries */
	SWAP_CLUSTER_MAX,	/* minimum number of tries */
	SWAP_CLUSTER_MAX,	/* do swap I/O in clusters of this size */
};

/*
 * Perform any setup for the swap system
 */

void __init swap_setup(void)
{
	/* Use a smaller cluster for memory <16MB or <32MB */
	if (num_physpages < ((16 * 1024 * 1024) >> PAGE_SHIFT))
		page_cluster = 2;
	else if (num_physpages < ((32 * 1024 * 1024) >> PAGE_SHIFT))
		page_cluster = 3;
	else
		page_cluster = 4;
}
