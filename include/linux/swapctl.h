#ifndef _LINUX_SWAPCTL_H
#define _LINUX_SWAPCTL_H

#include <asm/page.h>
#include <linux/fs.h>

/* Swap tuning control */

typedef struct swap_control_v6
{
	unsigned int	sc_max_page_age;
	unsigned int	sc_page_advance;
	unsigned int	sc_page_decline;
	unsigned int	sc_page_initial_age;
	unsigned int	sc_age_cluster_fract;
	unsigned int	sc_age_cluster_min;
	unsigned int	sc_pageout_weight;
	unsigned int	sc_bufferout_weight;
} swap_control_v6;
typedef struct swap_control_v6 swap_control_t;
extern swap_control_t swap_control;

typedef struct swapstat_v1
{
	unsigned long	wakeups;
	unsigned long	pages_reclaimed;
	unsigned long	pages_shm;
	unsigned long	pages_mmap;
	unsigned long	pages_swap;

	unsigned long	gfp_freepage_attempts;
	unsigned long	gfp_freepage_successes;
	unsigned long	gfp_shrink_attempts;
	unsigned long	gfp_shrink_successes;
	unsigned long	kswap_freepage_attempts;
	unsigned long	kswap_freepage_successes;
	unsigned long	kswap_wakeups[4];
} swapstat_v1;
typedef swapstat_v1 swapstat_t;
extern swapstat_t swapstats;

typedef struct buffer_mem_v1
{
	unsigned int	min_percent;
	unsigned int	borrow_percent;
	unsigned int	max_percent;
} buffer_mem_v1;
typedef buffer_mem_v1 buffer_mem_t;
extern buffer_mem_t buffer_mem;
extern buffer_mem_t page_cache;

typedef struct freepages_v1
{
	unsigned int	min;
	unsigned int	low;
	unsigned int	high;
} freepages_v1;
typedef freepages_v1 freepages_t;
extern freepages_t freepages;

typedef struct pager_daemon_v1
{
	unsigned int	tries_base;
	unsigned int	tries_min;
	unsigned int	swap_cluster;
} pager_daemon_v1;
typedef pager_daemon_v1 pager_daemon_t;
extern pager_daemon_t pager_daemon;

#define SC_VERSION	1
#define SC_MAX_VERSION	1

#ifdef __KERNEL__

/* Define the maximum (least urgent) priority for the page reclaim code */
#define RCL_MAXPRI 6
/* We use an extra priority in the swap accounting code to represent
   failure to free a resource at any priority */
#define RCL_FAILURE (RCL_MAXPRI + 1)

#define AGE_CLUSTER_FRACT	(swap_control.sc_age_cluster_fract)
#define AGE_CLUSTER_MIN		(swap_control.sc_age_cluster_min)
#define PAGEOUT_WEIGHT		(swap_control.sc_pageout_weight)
#define BUFFEROUT_WEIGHT	(swap_control.sc_bufferout_weight)

/* Page aging (see mm/swap.c) */

#define MAX_PAGE_AGE		(swap_control.sc_max_page_age)
#define PAGE_ADVANCE		(swap_control.sc_page_advance)
#define PAGE_DECLINE		(swap_control.sc_page_decline)
#define PAGE_INITIAL_AGE	(swap_control.sc_page_initial_age)

/* Given a resource of N units (pages or buffers etc), we only try to
 * age and reclaim AGE_CLUSTER_FRACT per 1024 resources each time we
 * scan the resource list. */
static inline int AGE_CLUSTER_SIZE(int resources)
{
	unsigned int n = (resources * AGE_CLUSTER_FRACT) >> 10;
	if (n < AGE_CLUSTER_MIN)
		return AGE_CLUSTER_MIN;
	else
		return n;
}

#endif /* __KERNEL */

#endif /* _LINUX_SWAPCTL_H */
