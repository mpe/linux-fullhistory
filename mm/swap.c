/*
 *  linux/mm/swap.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * This file should contain most things doing the swapping from/to disk.
 * Started 18.12.91
 */

#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/stat.h>

static int lowest_bit = 0;
static int highest_bit = 0;

/*
 * The following are used to make sure we don't thrash too much...
 */
#define NR_LAST_FREE_PAGES 32
static unsigned long last_free_pages[NR_LAST_FREE_PAGES] = {0,};

#define SWAP_BITS (4096<<3)

#define bitop(name,op) \
static inline int name(char * addr,unsigned int nr) \
{ \
int __res; \
__asm__ __volatile__("bt" op " %1,%2; adcl $0,%0" \
:"=g" (__res) \
:"r" (nr),"m" (*(addr)),"0" (0)); \
return __res; \
}

bitop(bit,"")
bitop(setbit,"s")
bitop(clrbit,"r")

static char * swap_bitmap = NULL;
static char * swap_lockmap = NULL;
unsigned int swap_device = 0;
struct inode * swap_file = NULL;

void rw_swap_page(int rw, unsigned int nr, char * buf)
{
	static struct wait_queue * lock_queue = NULL;

	if (!swap_lockmap) {
		printk("No swap lock-map\n");
		return;
	}
	while (setbit(swap_lockmap,nr))
		sleep_on(&lock_queue);
	if (swap_device) {
		ll_rw_page(rw,swap_device,nr,buf);
	} else if (swap_file) {
		unsigned int zones[4];
		unsigned int block = nr << 2;
		int i;

		for (i = 0; i < 4; i++)
			if (!(zones[i] = bmap(swap_file,block++))) {
				printk("rw_swap_page: bad swap file\n");
				return;
			}
		ll_rw_swap_file(rw,swap_file->i_dev, zones,4,buf);
	} else
		printk("re_swap_page: no swap file or device\n");
	if (!clrbit(swap_lockmap,nr))
		printk("rw_swap_page: lock already cleared\n");
	wake_up(&lock_queue);
}

static unsigned int get_swap_page(void)
{
	unsigned int nr;

	if (!swap_bitmap)
		return 0;
	for (nr = lowest_bit; nr <= highest_bit ; nr++)
		if (clrbit(swap_bitmap,nr)) {
			if (nr == highest_bit)
				highest_bit--;
			return lowest_bit = nr;
		}
	return 0;
}

void swap_free(unsigned int swap_nr)
{
	if (!swap_nr)
		return;
	if (swap_bitmap && swap_nr < SWAP_BITS) {
		if (swap_nr < lowest_bit)
			lowest_bit = swap_nr;
		if (swap_nr > highest_bit)
			highest_bit = swap_nr;
		if (!setbit(swap_bitmap,swap_nr))
			return;
	}
	printk("swap_free: swap-space bitmap bad (bit %d)\n",swap_nr);
	return;
}

void swap_in(unsigned long *table_ptr)
{
	unsigned long swap_nr;
	unsigned long page;

	swap_nr = *table_ptr;
	if (1 & swap_nr) {
		printk("trying to swap in present page\n\r");
		return;
	}
	if (!swap_nr) {
		printk("No swap page in swap_in\n\r");
		return;
	}
	if (!swap_bitmap) {
		printk("Trying to swap in without swap bit-map");
		*table_ptr = BAD_PAGE;
		return;
	}
	page = get_free_page(GFP_KERNEL);
	if (!page) {
		oom(current);
		page = BAD_PAGE;
	} else	
		read_swap_page(swap_nr>>1, (char *) page);
	if (*table_ptr != swap_nr) {
		free_page(page);
		return;
	}
	swap_free(swap_nr>>1);
	*table_ptr = page | (PAGE_DIRTY | 7);
}

int try_to_swap_out(unsigned long * table_ptr)
{
	int i;
	unsigned long page;
	unsigned long swap_nr;

	page = *table_ptr;
	if (!(PAGE_PRESENT & page))
		return 0;
	if (page < low_memory || page >= high_memory)
		return 0;
	for (i = 0; i < NR_LAST_FREE_PAGES; i++)
		if (last_free_pages[i] == (page & 0xfffff000))
			return 0;
	if (PAGE_DIRTY & page) {
		page &= 0xfffff000;
		if (mem_map[MAP_NR(page)] != 1)
			return 0;
		if (!(swap_nr = get_swap_page()))
			return 0;
		*table_ptr = swap_nr<<1;
		invalidate();
		write_swap_page(swap_nr, (char *) page);
		free_page(page);
		return 1;
	}
	page &= 0xfffff000;
	*table_ptr = 0;
	invalidate();
	free_page(page);
	return 1;
}

/*
 * We never page the pages in task[0] - kernel memory.
 * We page all other pages.
 */
#define FIRST_VM_PAGE (TASK_SIZE>>12)
#define LAST_VM_PAGE (1024*1024)
#define VM_PAGES (LAST_VM_PAGE - FIRST_VM_PAGE)

/*
 * Go through the page tables, searching for a user page that
 * we can swap out.
 *
 * We now check that the process is swappable (normally only 'init'
 * is un-swappable), allowing high-priority processes which cannot be
 * swapped out (things like user-level device drivers (Not implemented)).
 */
int swap_out(void)
{
	static int dir_entry = 1024;
	static int page_entry = -1;
	int counter = VM_PAGES;
	int pg_table;
	struct task_struct * p;

check_dir:
	if (counter < 0)
		goto no_swap;
	if (dir_entry >= 1024)
		dir_entry = FIRST_VM_PAGE>>10;
	if (!(p = task[dir_entry >> 4])) {
		counter -= 1024;
		dir_entry++;
		goto check_dir;
	}
	if (!(1 & (pg_table = pg_dir[dir_entry]))) {
		if (pg_table) {
			printk("bad page-table at pg_dir[%d]: %08x\n\r",
				dir_entry,pg_table);
			pg_dir[dir_entry] = 0;
		}
		counter -= 1024;
		dir_entry++;
		goto check_dir;
	}
	pg_table &= 0xfffff000;
check_table:
	if (counter < 0)
		goto no_swap;
	counter--;
	page_entry++;
	if (page_entry >= 1024) {
		page_entry = -1;
		dir_entry++;
		goto check_dir;
	}
	if (p->swappable && try_to_swap_out(page_entry + (unsigned long *) pg_table)) {
		p->rss--;
		dir_entry++;
		return 1;
	}
	goto check_table;
no_swap:
	printk("Out of swap-memory\n\r");
	return 0;
}

/*
 * Get physical address of first (actually last :-) free page, and mark it
 * used. If no free pages left, return 0.
 */
unsigned long get_free_page(int priority)
{
	unsigned long result;
	static unsigned long index = 0;

repeat:
	__asm__("std ; repne ; scasb\n\t"
		"jne 1f\n\t"
		"movb $1,1(%%edi)\n\t"
		"sall $12,%%ecx\n\t"
		"addl %2,%%ecx\n\t"
		"movl %%ecx,%%edx\n\t"
		"movl $1024,%%ecx\n\t"
		"leal 4092(%%edx),%%edi\n\t"
		"rep ; stosl\n\t"
		"movl %%edx,%%eax\n"
		"1:\tcld"
		:"=a" (result)
		:"0" (0),"b" (low_memory),"c" (paging_pages),
		"D" (mem_map+paging_pages-1)
		:"di","cx","dx");
	if (result >= high_memory)
		goto repeat;
	if ((result && result < low_memory) || (result & 0xfff)) {
		printk("weird result: %08x\n",result);
		result = 0;
	}
	if (result) {
		--nr_free_pages;
		if (index >= NR_LAST_FREE_PAGES)
			index = 0;
		last_free_pages[index] = result;
		index++;
		return result;
	}
	if (nr_free_pages) {
		printk("Damn. mm_free_page count is off by %d\r\n",
			nr_free_pages);
		nr_free_pages = 0;
	}
	if (priority <= GFP_BUFFER)
		return 0;
	if (shrink_buffers()) {
		schedule();
		goto repeat;
	}
	if (swap_out()) {
		schedule();
		goto repeat;
	}
	return 0;
}

/*
 * Written 01/25/92 by Simmule Turner, heavily changed by Linus.
 *
 * The swapon system call
 */
int sys_swapon(const char * specialfile)
{
	struct inode * swap_inode;
	char * tmp;
	int i,j;

	if (!suser())
		return -EPERM;
	if (!(swap_inode  = namei(specialfile)))
		return -ENOENT;
	if (swap_file || swap_device || swap_bitmap || swap_lockmap) {
		iput(swap_inode);
		return -EBUSY;
	}
	if (S_ISBLK(swap_inode->i_mode)) {
		swap_device = swap_inode->i_rdev;
		iput(swap_inode);
	} else if (S_ISREG(swap_inode->i_mode))
		swap_file = swap_inode;
	else {
		iput(swap_inode);
		return -EINVAL;
	}
	tmp = (char *) get_free_page(GFP_USER);
	swap_lockmap = (char *) get_free_page(GFP_USER);
	if (!tmp || !swap_lockmap) {
		printk("Unable to start swapping: out of memory :-)\n");
		free_page((long) tmp);
		free_page((long) swap_lockmap);
		iput(swap_file);
		swap_device = 0;
		swap_file = NULL;
		swap_bitmap = NULL;
		swap_lockmap = NULL;
		return -ENOMEM;
	}
	read_swap_page(0,tmp);
	if (strncmp("SWAP-SPACE",tmp+4086,10)) {
		printk("Unable to find swap-space signature\n\r");
		free_page((long) tmp);
		free_page((long) swap_lockmap);
		iput(swap_file);
		swap_device = 0;
		swap_file = NULL;
		swap_bitmap = NULL;
		swap_lockmap = NULL;
		return -EINVAL;
	}
	memset(tmp+4086,0,10);
	j = 0;
	lowest_bit = 0;
	highest_bit = 0;
	for (i = 1 ; i < SWAP_BITS ; i++)
		if (bit(tmp,i)) {
			if (!lowest_bit)
				lowest_bit = i;
			highest_bit = i;
			j++;
		}
	if (!j) {
		printk("Empty swap-file\n");
		free_page((long) tmp);
		free_page((long) swap_lockmap);
		iput(swap_file);
		swap_device = 0;
		swap_file = NULL;
		swap_bitmap = NULL;
		swap_lockmap = NULL;
		return -EINVAL;
	}
	swap_bitmap = tmp;
	printk("Adding Swap: %d pages (%d bytes) swap-space\n\r",j,j*4096);
	return 0;
}
