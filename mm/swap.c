/*
 *  linux/mm/swap.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 */

/*
 * This file should contain most things doing the swapping from/to disk.
 * Started 18.12.91
 *
 * Swap aging added 23.2.95, Stephen Tweedie.
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

#include <asm/dma.h>
#include <asm/system.h> /* for cli()/sti() */
#include <asm/segment.h> /* for memcpy_to/fromfs */
#include <asm/bitops.h>
#include <asm/pgtable.h>

/*
 * We identify three levels of free memory.  We never let free mem
 * fall below the min_free_pages except for atomic allocations.  We
 * start background swapping if we fall below free_pages_high free
 * pages, and we begin intensive swapping below free_pages_low.
 *
 * Keep these three variables contiguous for sysctl(2).  
 */
int min_free_pages = 20;
int free_pages_low = 30;
int free_pages_high = 40;

/* We track the number of pages currently being asynchronously swapped
   out, so that we don't try to swap TOO many pages out at once */
atomic_t nr_async_pages = 0;

/*
 * Constants for the page aging mechanism: the maximum age (actually,
 * the maximum "youthfulness"); the quanta by which pages rejuvenate
 * and age; and the initial age for new pages. 
 */

swap_control_t swap_control = {
	20, 3, 1, 3,		/* Page aging */
	10, 2, 2, 4,		/* Buffer aging */
	32, 4,			/* Aging cluster */
	8192, 8192,		/* Pageout and bufferout weights */
	-200,			/* Buffer grace */
	1, 1,			/* Buffs/pages to free */
	RCL_ROUND_ROBIN		/* Balancing policy */
};

swapstat_t swapstats = {0};

/* General swap control */

/* Parse the kernel command line "swap=" option at load time: */
void swap_setup(char *str, int *ints)
{
	int * swap_vars[8] = {
		&MAX_PAGE_AGE,
		&PAGE_ADVANCE,
		&PAGE_DECLINE,
		&PAGE_INITIAL_AGE,
		&AGE_CLUSTER_FRACT,
		&AGE_CLUSTER_MIN,
		&PAGEOUT_WEIGHT,
		&BUFFEROUT_WEIGHT
	};
	int i;
	for (i=0; i < ints[0] && i < 8; i++) {
		if (ints[i+1])
			*(swap_vars[i]) = ints[i+1];
	}
}

/* Parse the kernel command line "buff=" option at load time: */
void buff_setup(char *str, int *ints)
{
	int * buff_vars[6] = {
		&MAX_BUFF_AGE,
		&BUFF_ADVANCE,
		&BUFF_DECLINE,
		&BUFF_INITIAL_AGE,
		&BUFFEROUT_WEIGHT,
		&BUFFERMEM_GRACE
	};
	int i;
	for (i=0; i < ints[0] && i < 6; i++) {
		if (ints[i+1])
			*(buff_vars[i]) = ints[i+1];
	}
}

