#ifndef _LINUX_SWAPCTL_H
#define _LINUX_SWAPCTL_H

#include <asm/page.h>
#include <linux/fs.h>

/* Swap tuning control */

/* First, enumerate the different reclaim policies */
enum RCL_POLICY {RCL_ROUND_ROBIN, RCL_BUFF_FIRST, RCL_PERSIST};

typedef struct swap_control_v5
{
	int	sc_max_page_age;
	int	sc_page_advance;
	int	sc_page_decline;
	int	sc_page_initial_age;
	int	sc_max_buff_age;
	int	sc_buff_advance;
	int	sc_buff_decline;
	int	sc_buff_initial_age;
	int	sc_age_cluster_fract;
	int	sc_age_cluster_min;
	int	sc_pageout_weight;
	int	sc_bufferout_weight;
	int 	sc_buffer_grace;
	int 	sc_nr_buffs_to_free;
	int 	sc_nr_pages_to_free;
	enum RCL_POLICY	sc_policy;
} swap_control_v5;
typedef struct swap_control_v5 swap_control_t;
extern swap_control_t swap_control;

typedef struct kswapd_control_v1
{
	int	maxpages;
	int	pages_buff;
	int	pages_shm;
	int	pages_mmap;
	int	pages_swap;
} kswapd_control_v1;
typedef kswapd_control_v1 kswapd_control_t;
extern kswapd_control_t kswapd_ctl;

typedef struct swapstat_v1
{
	int	wakeups;
	int	pages_reclaimed;
	int	pages_shm;
	int	pages_mmap;
	int	pages_swap;
} swapstat_v1;
typedef swapstat_v1 swapstat_t;
extern swapstat_t swapstats;

extern int min_free_pages, free_pages_low, free_pages_high;

#define SC_VERSION	1
#define SC_MAX_VERSION	1

#ifdef __KERNEL__

/* Define the maximum (least urgent) priority for the page reclaim code */
#define RCL_MAXPRI 6
/* We use an extra priority in the swap accounting code to represent
   failure to free a resource at any priority */
#define RCL_FAILURE (RCL_MAXPRI + 1)

#define RCL_POLICY		(swap_control.sc_policy)
#define AGE_CLUSTER_FRACT	(swap_control.sc_age_cluster_fract)
#define AGE_CLUSTER_MIN		(swap_control.sc_age_cluster_min)
#define PAGEOUT_WEIGHT		(swap_control.sc_pageout_weight)
#define BUFFEROUT_WEIGHT	(swap_control.sc_bufferout_weight)

#define NR_BUFFS_TO_FREE	(swap_control.sc_nr_buffs_to_free)
#define NR_PAGES_TO_FREE	(swap_control.sc_nr_pages_to_free)

#define BUFFERMEM_GRACE		(swap_control.sc_buffer_grace)

/* Page aging (see mm/swap.c) */

#define MAX_PAGE_AGE		(swap_control.sc_max_page_age)
#define PAGE_ADVANCE		(swap_control.sc_page_advance)
#define PAGE_DECLINE		(swap_control.sc_page_decline)
#define PAGE_INITIAL_AGE	(swap_control.sc_page_initial_age)

#define MAX_BUFF_AGE		(swap_control.sc_max_buff_age)
#define BUFF_ADVANCE		(swap_control.sc_buff_advance)
#define BUFF_DECLINE		(swap_control.sc_buff_decline)
#define BUFF_INITIAL_AGE	(swap_control.sc_buff_initial_age)

/* Given a resource of N units (pages or buffers etc), we only try to
 * age and reclaim AGE_CLUSTER_FRACT per 1024 resources each time we
 * scan the resource list. */
static inline int AGE_CLUSTER_SIZE(int resources)
{
	int n = (resources * AGE_CLUSTER_FRACT) >> 10;
	if (n < AGE_CLUSTER_MIN)
		return AGE_CLUSTER_MIN;
	else
		return n;
}

static inline void touch_page(struct page *page)
{
	if (page->age < (MAX_PAGE_AGE - PAGE_ADVANCE))
		page->age += PAGE_ADVANCE;
	else
		page->age = MAX_PAGE_AGE;
}

static inline void age_page(struct page *page)
{
	if (page->age > PAGE_DECLINE)
		page->age -= PAGE_DECLINE;
	else
		page->age = 0;
}

static inline int age_of(unsigned long addr)
{
	return mem_map[MAP_NR(addr)].age;
}

static inline void set_page_new(unsigned long addr)
{
	mem_map[MAP_NR(addr)].age = PAGE_INITIAL_AGE;
}

#endif /* __KERNEL */

#endif /* _LINUX_SWAPCTL_H */
