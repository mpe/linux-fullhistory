#ifndef _LINUX_SWAP_H
#define _LINUX_SWAP_H

#include <asm/page.h>

#define SWAP_FLAG_PREFER	0x8000	/* set if swap priority specified */
#define SWAP_FLAG_PRIO_MASK	0x7fff
#define SWAP_FLAG_PRIO_SHIFT	0

#define MAX_SWAPFILES 8

union swap_header {
	struct 
	{
		char reserved[PAGE_SIZE - 10];
		char magic[10];
	} magic;
	struct 
	{
		char	     bootbits[1024];	/* Space for disklabel etc. */
		unsigned int version;
		unsigned int last_page;
		unsigned int nr_badpages;
		unsigned int padding[125];
		unsigned int badpages[1];
	} info;
};

#ifdef __KERNEL__

/*
 * Max bad pages in the new format..
 */
#define __swapoffset(x) ((unsigned long)&((union swap_header *)0)->x)
#define MAX_SWAP_BADPAGES \
	((__swapoffset(magic.magic) - __swapoffset(info.badpages)) / sizeof(int))

#include <asm/atomic.h>

#define SWP_USED	1
#define SWP_WRITEOK	3

#define SWAP_CLUSTER_MAX 32

#define SWAP_MAP_MAX	0x7fff
#define SWAP_MAP_BAD	0x8000

struct swap_info_struct {
	unsigned int flags;
	kdev_t swap_device;
	struct dentry * swap_file;
	unsigned short * swap_map;
	unsigned int lowest_bit;
	unsigned int highest_bit;
	unsigned int cluster_next;
	unsigned int cluster_nr;
	int prio;			/* swap priority */
	int pages;
	unsigned long max;
	int next;			/* next entry on swap list */
};

extern int nr_swap_pages;
extern int nr_free_pages;
extern int nr_lru_pages;
extern struct list_head lru_cache;
extern atomic_t nr_async_pages;
extern struct address_space swapper_space;
extern atomic_t page_cache_size;
extern atomic_t buffermem_pages;

/* Incomplete types for prototype declarations: */
struct task_struct;
struct vm_area_struct;
struct sysinfo;

/* linux/ipc/shm.c */
extern int shm_swap (int, int);

/* linux/mm/swap.c */
extern void swap_setup (void);

/* linux/mm/vmscan.c */
extern int try_to_free_pages(unsigned int gfp_mask);

/* linux/mm/page_io.c */
extern void rw_swap_page(int, struct page *, int);
extern void rw_swap_page_nolock(int, swp_entry_t, char *, int);

/* linux/mm/page_alloc.c */

/* linux/mm/swap_state.c */
extern void show_swap_cache_info(void);
extern void add_to_swap_cache(struct page *, swp_entry_t);
extern int swap_duplicate(swp_entry_t);
extern int swap_check_entry(unsigned long);
extern struct page * lookup_swap_cache(swp_entry_t);
extern struct page * read_swap_cache_async(swp_entry_t, int);
#define read_swap_cache(entry) read_swap_cache_async(entry, 1);
extern int swap_count(struct page *);
extern swp_entry_t acquire_swap_entry(struct page *page);

/*
 * Make these inline later once they are working properly.
 */
extern void __delete_from_swap_cache(struct page *page);
extern void delete_from_swap_cache(struct page *page);
extern void free_page_and_swap_cache(struct page *page);

/* linux/mm/swapfile.c */
extern unsigned int nr_swapfiles;
extern struct swap_info_struct swap_info[];
extern int is_swap_partition(kdev_t);
extern void si_swapinfo(struct sysinfo *);
extern swp_entry_t get_swap_page(void);
extern void swap_free(swp_entry_t);
struct swap_list_t {
	int head;	/* head of priority-ordered swapfile list */
	int next;	/* swapfile to be used next */
};
extern struct swap_list_t swap_list;
asmlinkage long sys_swapoff(const char *);
asmlinkage long sys_swapon(const char *, int);

#define SWAP_CACHE_INFO

#ifdef SWAP_CACHE_INFO
extern unsigned long swap_cache_add_total;
extern unsigned long swap_cache_del_total;
extern unsigned long swap_cache_find_total;
extern unsigned long swap_cache_find_success;
#endif

/*
 * Work out if there are any other processes sharing this page, ignoring
 * any page reference coming from the swap cache, or from outstanding
 * swap IO on this page.  (The page cache _does_ count as another valid
 * reference to the page, however.)
 */
static inline int is_page_shared(struct page *page)
{
	unsigned int count;
	if (PageReserved(page))
		return 1;
	count = page_count(page);
	if (PageSwapCache(page))
		count += swap_count(page) - 2;
	return  count > 1;
}

extern spinlock_t pagemap_lru_lock;

/*
 * Helper macros for lru_pages handling.
 */
#define	lru_cache_add(page)			\
do {						\
	spin_lock(&pagemap_lru_lock);		\
	list_add(&(page)->lru, &lru_cache);	\
	nr_lru_pages++;				\
	spin_unlock(&pagemap_lru_lock);		\
} while (0)

#define	lru_cache_del(page)			\
do {						\
	spin_lock(&pagemap_lru_lock);		\
	list_del(&(page)->lru);			\
	nr_lru_pages--;				\
	spin_unlock(&pagemap_lru_lock);		\
} while (0)

#endif /* __KERNEL__*/

#endif /* _LINUX_SWAP_H */
