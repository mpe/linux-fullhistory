#ifndef _LINUX_SWAP_H
#define _LINUX_SWAP_H

#define SWAP_FLAG_PREFER	0x8000	/* set if swap priority specified */
#define SWAP_FLAG_PRIO_MASK	0x7fff
#define SWAP_FLAG_PRIO_SHIFT	0

#define MAX_SWAPFILES 8

#ifdef __KERNEL__

#include <asm/atomic.h>

#define SWP_USED	1
#define SWP_WRITEOK	3

#define SWAP_CLUSTER_MAX 32

struct swap_info_struct {
	unsigned int flags;
	kdev_t swap_device;
	struct inode * swap_file;
	unsigned char * swap_map;
	unsigned char * swap_lockmap;
	int lowest_bit;
	int highest_bit;
	int cluster_next;
	int cluster_nr;
	int prio;			/* swap priority */
	int pages;
	unsigned long max;
	int next;			/* next entry on swap list */
};

extern int nr_swap_pages;
extern int nr_free_pages;
extern atomic_t nr_async_pages;
extern int min_free_pages;
extern int free_pages_low;
extern int free_pages_high;

/* Incomplete types for prototype declarations: */
struct task_struct;
struct vm_area_struct;
struct sysinfo;

/* linux/ipc/shm.c */
extern int shm_swap (int, int);

/* linux/mm/vmscan.c */
extern int try_to_free_page(int, int, int);

/* linux/mm/page_io.c */
extern void rw_swap_page(int, unsigned long, char *, int);
#define read_swap_page(nr,buf) \
	rw_swap_page(READ,(nr),(buf),1)
#define write_swap_page(nr,buf) \
	rw_swap_page(WRITE,(nr),(buf),1)
extern void swap_after_unlock_page (unsigned long entry);

/* linux/mm/page_alloc.c */
extern void swap_in(struct task_struct *, struct vm_area_struct *,
		    pte_t *, unsigned long, int);


/* linux/mm/swap_state.c */
extern void show_swap_cache_info(void);
extern int add_to_swap_cache(unsigned long, unsigned long);
extern unsigned long init_swap_cache(unsigned long, unsigned long);
extern void swap_duplicate(unsigned long);

/* linux/mm/swapfile.c */
extern int nr_swapfiles;
extern struct swap_info_struct swap_info[];
void si_swapinfo(struct sysinfo *);
unsigned long get_swap_page(void);
extern void swap_free(unsigned long);

/*
 * vm_ops not present page codes for shared memory.
 *
 * Will go away eventually..
 */
#define SHM_SWP_TYPE 0x40

/*
 * swap cache stuff (in linux/mm/swap_state.c)
 */

#define SWAP_CACHE_INFO

extern unsigned long * swap_cache;

#ifdef SWAP_CACHE_INFO
extern unsigned long swap_cache_add_total;
extern unsigned long swap_cache_add_success;
extern unsigned long swap_cache_del_total;
extern unsigned long swap_cache_del_success;
extern unsigned long swap_cache_find_total;
extern unsigned long swap_cache_find_success;
#endif

extern inline unsigned long in_swap_cache(unsigned long index)
{
	return swap_cache[index]; 
}

extern inline long find_in_swap_cache(unsigned long index)
{
	unsigned long entry;

#ifdef SWAP_CACHE_INFO
	swap_cache_find_total++;
#endif
	entry = xchg(swap_cache + index, 0);
#ifdef SWAP_CACHE_INFO
	if (entry)
		swap_cache_find_success++;
#endif	
	return entry;
}

extern inline int delete_from_swap_cache(unsigned long index)
{
	unsigned long entry;
	
#ifdef SWAP_CACHE_INFO
	swap_cache_del_total++;
#endif	
	entry = xchg(swap_cache + index, 0);
	if (entry)  {
#ifdef SWAP_CACHE_INFO
		swap_cache_del_success++;
#endif
		swap_free(entry);
		return 1;
	}
	return 0;
}


#endif /* __KERNEL__*/

#endif /* _LINUX_SWAP_H */
