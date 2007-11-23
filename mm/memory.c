/*
 *  linux/mm/memory.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * demand-loading started 01.12.91 - seems it is high on the list of
 * things wanted, and it should be easy to implement. - Linus
 */

/*
 * Ok, demand-loading was easy, shared pages a little bit tricker. Shared
 * pages started 02.12.91, seems to work. - Linus.
 *
 * Tested sharing by executing about 30 /bin/sh: under the old kernel it
 * would have taken more than the 6M I have free, but it worked well as
 * far as I could see.
 *
 * Also corrected some "invalidate()"s - I wasn't doing enough of them.
 */

/*
 * Real VM (paging to/from disk) started 18.12.91. Much more work and
 * thought has to go into this. Oh, well..
 * 19.12.91  -  works, somewhat. Sometimes I get faults, don't know why.
 *		Found it. Everything seems to work now.
 * 20.12.91  -  Ok, making the swap-device changeable like the root.
 */

#include <signal.h>

#include <asm/system.h>

#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>

#define CODE_SPACE(addr) ((((addr)+4095)&~4095) < \
current->start_code + current->end_code)

unsigned long HIGH_MEMORY = 0;

#define copy_page(from,to) \
__asm__("cld ; rep ; movsl"::"S" (from),"D" (to),"c" (1024):"cx","di","si")

#define CHECK_LAST_NR	16

static unsigned long last_pages[CHECK_LAST_NR] = { 0, };

unsigned char mem_map [ PAGING_PAGES ] = {0,};

/*
 * Free a page of memory at physical address 'addr'. Used by
 * 'free_page_tables()'
 */
void free_page(unsigned long addr)
{
	if (addr < LOW_MEM) return;
	if (addr < HIGH_MEMORY) {
		addr -= LOW_MEM;
		addr >>= 12;
		if (mem_map[addr]--)
			return;
		mem_map[addr]=0;
	}
	printk("trying to free free page: memory probably corrupted");
}

/*
 * This function frees a continuos block of page tables, as needed
 * by 'exit()'. As does copy_page_tables(), this handles only 4Mb blocks.
 */
int free_page_tables(unsigned long from,unsigned long size)
{
	unsigned long page;
	unsigned long page_dir;
	unsigned long *pg_table;
	unsigned long * dir, nr;

	if (from & 0x3fffff)
		panic("free_page_tables called with wrong alignment");
	if (!from)
		panic("Trying to free up swapper memory space");
	size = (size + 0x3fffff) >> 22;
	dir = (unsigned long *) ((from>>20) & 0xffc); /* _pg_dir = 0 */
	for ( ; size-->0 ; dir++) {
		if (!(page_dir = *dir))
			continue;
		*dir = 0;
		if (!(page_dir & 1)) {
			printk("free_page_tables: bad page directory.");
			continue;
		}
		pg_table = (unsigned long *) (0xfffff000 & page_dir);
		for (nr=0 ; nr<1024 ; nr++,pg_table++) {
			if (!(page = *pg_table))
				continue;
			*pg_table = 0;
			if (1 & page)
				free_page(0xfffff000 & page);
			else
				swap_free(page >> 1);
		}
		free_page(0xfffff000 & page_dir);
	}
	invalidate();
	for (page = 0; page < CHECK_LAST_NR ; page++)
		last_pages[page] = 0;
	return 0;
}

/*
 *  Well, here is one of the most complicated functions in mm. It
 * copies a range of linerar addresses by copying only the pages.
 * Let's hope this is bug-free, 'cause this one I don't want to debug :-)
 *
 * Note! We don't copy just any chunks of memory - addresses have to
 * be divisible by 4Mb (one page-directory entry), as this makes the
 * function easier. It's used only by fork anyway.
 *
 * NOTE 2!! When from==0 we are copying kernel space for the first
 * fork(). Then we DONT want to copy a full page-directory entry, as
 * that would lead to some serious memory waste - we just copy the
 * first 160 pages - 640kB. Even that is more than we need, but it
 * doesn't take any more memory - we don't copy-on-write in the low
 * 1 Mb-range, so the pages can be shared with the kernel. Thus the
 * special case for nr=xxxx.
 */
int copy_page_tables(unsigned long from,unsigned long to,long size)
{
	unsigned long * from_page_table;
	unsigned long * to_page_table;
	unsigned long this_page;
	unsigned long * from_dir, * to_dir;
	unsigned long new_page;
	unsigned long nr;

	if ((from&0x3fffff) || (to&0x3fffff))
		panic("copy_page_tables called with wrong alignment");
	from_dir = (unsigned long *) ((from>>20) & 0xffc); /* _pg_dir = 0 */
	to_dir = (unsigned long *) ((to>>20) & 0xffc);
	size = ((unsigned) (size+0x3fffff)) >> 22;
	for( ; size-->0 ; from_dir++,to_dir++) {
		if (*to_dir)
			printk("copy_page_tables: already exist, "
				"probable memory corruption\n");
		if (!*from_dir)
			continue;
		if (!(1 & *from_dir)) {
			printk("copy_page_tables: page table swapped out, "
				"probable memory corruption");
			*from_dir = 0;
			continue;
		}
		from_page_table = (unsigned long *) (0xfffff000 & *from_dir);
		if (!(to_page_table = (unsigned long *) get_free_page()))
			return -1;	/* Out of memory, see freeing */
		*to_dir = ((unsigned long) to_page_table) | 7;
		nr = (from==0)?0xA0:1024;
		for ( ; nr-- > 0 ; from_page_table++,to_page_table++) {
			this_page = *from_page_table;
			if (!this_page)
				continue;
			if (!(1 & this_page)) {
				if (!(new_page = get_free_page()))
					return -1;
				++current->rss;
				read_swap_page(this_page>>1, (char *) new_page);
				*to_page_table = this_page;
				*from_page_table = new_page | (PAGE_DIRTY | 7);
				continue;
			}
			this_page &= ~2;
			*to_page_table = this_page;
			if (this_page > LOW_MEM) {
				*from_page_table = this_page;
				this_page -= LOW_MEM;
				this_page >>= 12;
				mem_map[this_page]++;
			}
		}
	}
	invalidate();
	return 0;
}

/*
 * a more complete version of free_page_tables which performs with page
 * granularity.
 */
int
unmap_page_range(unsigned long from, unsigned long size)
{
	unsigned long page, page_dir;
	unsigned long *page_table, *dir;
	unsigned long poff, pcnt, pc;

	if (from & 0xfff)
		panic("unmap_page_range called with wrong alignment");
	if (!from)
		panic("unmap_page_range trying to free swapper memory space");
	size = (size + 0xfff) >> 12;
	dir = (unsigned long *) ((from >> 20) & 0xffc); /* _pg_dir = 0 */
	poff = (from >> 12) & 0x3ff;
	if ((pcnt = 1024 - poff) > size)
		pcnt = size;

	for ( ; size > 0; ++dir, size -= pcnt,
	     pcnt = (size > 1024 ? 1024 : size)) {
		if (!(page_dir = *dir))	{
			poff = 0;
			continue;
		}
		if (!(page_dir & 1)) {
			printk("unmap_page_range: bad page directory.");
			continue;
		}
		page_table = (unsigned long *)(0xfffff000 & page_dir);
		if (poff) {
			page_table += poff;
			poff = 0;
		}
		for (pc = pcnt; pc--; page_table++) {
			if (page = *page_table) {
				--current->rss;
				*page_table = 0;
				if (1 & page)
					free_page(0xfffff000 & page);
				else
					swap_free(page >> 1);
			}
		}
		if (pcnt == 1024) {
			free_page(0xfffff000 & page_dir);
			*dir = 0;
		}
	}
	invalidate();
	for (page = 0; page < CHECK_LAST_NR ; page++)
		last_pages[page] = 0;
	return 0;
}

/*
 * maps a range of physical memory into the requested pages. the old
 * mappings are removed. any references to nonexistent pages results
 * in null mappings (currently treated as "copy-on-access")
 *
 * permiss is encoded as cxwr (copy,exec,write,read) where copy modifies
 * the behavior of write to be copy-on-write.
 *
 * due to current limitations, we actually have the following
 *		on		off
 * read:	yes		yes
 * write/copy:	yes/copy	copy/copy
 * exec:	yes		yes
 */
int
remap_page_range(unsigned long from, unsigned long to, unsigned long size,
		 int permiss)
{
	unsigned long *page_table, *dir;
	unsigned long poff, pcnt;

	if ((from & 0xfff) || (to & 0xfff))
		panic("remap_page_range called with wrong alignment");
	dir = (unsigned long *) ((from >> 20) & 0xffc); /* _pg_dir = 0 */
	size = (size + 0xfff) >> 12;
	poff = (from >> 12) & 0x3ff;
	if ((pcnt = 1024 - poff) > size)
		pcnt = size;

	while (size > 0) {
		if (!(1 & *dir)) {
			if (!(page_table = (unsigned long *)get_free_page())) {
				invalidate();
				return -1;
			}
			*dir++ = ((unsigned long) page_table) | 7;
		}
		else
			page_table = (unsigned long *)(0xfffff000 & *dir++);
		if (poff) {
			page_table += poff;
			poff = 0;
		}

		for (size -= pcnt; pcnt-- ;) {
			int mask;

			mask = 4;
			if (permiss & 1)
				mask |= 1;
			if (permiss & 2) {
				if (permiss & 8)
					mask |= 1;
				else
					mask |= 3;
			}
			if (permiss & 4)
				mask |= 1;

			if (*page_table) {
				--current->rss;
				if (1 & *page_table)
					free_page(0xfffff000 & *page_table);
				else
					swap_free(*page_table >> 1);
			}

			/*
			 * i'm not sure of the second cond here. should we
			 * report failure?
			 * the first condition should return an invalid access
			 * when the page is referenced. current assumptions
			 * cause it to be treated as demand allocation.
			 */
			if (mask == 4 || to >= HIGH_MEMORY)
				*page_table++ = 0;	/* not present */
			else {
				++current->rss;
				*page_table++ = (to | mask);
				if (to > LOW_MEM) {
					unsigned long frame;
					frame = to - LOW_MEM;
					frame >>= 12;
					mem_map[frame]++;
				}
			}
			to += PAGE_SIZE;
		}
		pcnt = (size > 1024 ? 1024 : size);
	}
	invalidate();
	for (to = 0; to < CHECK_LAST_NR ; to++)
		last_pages[to] = 0;
	return 0;
}

/*
 * This function puts a page in memory at the wanted address.
 * It returns the physical address of the page gotten, 0 if
 * out of memory (either when trying to access page-table or
 * page.)
 */
static unsigned long put_page(unsigned long page,unsigned long address)
{
	unsigned long tmp, *page_table;

/* NOTE !!! This uses the fact that _pg_dir=0 */

	if (page < LOW_MEM || page >= HIGH_MEMORY) {
		printk("put_page: trying to put page %p at %p\n",page,address);
		return 0;
	}
	if (mem_map[(page-LOW_MEM)>>12] != 1) {
		printk("put_page: mem_map disagrees with %p at %p\n",page,address);
		return 0;
	}
	page_table = (unsigned long *) ((address>>20) & 0xffc);
	if ((*page_table)&1)
		page_table = (unsigned long *) (0xfffff000 & *page_table);
	else {
		if (!(tmp=get_free_page()))
			return 0;
		*page_table = tmp | 7;
		page_table = (unsigned long *) tmp;
	}
	page_table += (address>>12) & 0x3ff;
	if (*page_table) {
		printk("put_page: page already exists\n");
		*page_table = 0;
		invalidate();
	}
	*page_table = page | 7;
/* no need for invalidate */
	return page;
}

/*
 * The previous function doesn't work very well if you also want to mark
 * the page dirty: exec.c wants this, as it has earlier changed the page,
 * and we want the dirty-status to be correct (for VM). Thus the same
 * routine, but this time we mark it dirty too.
 */
unsigned long put_dirty_page(unsigned long page, unsigned long address)
{
	unsigned long tmp, *page_table;

/* NOTE !!! This uses the fact that _pg_dir=0 */

	if (page < LOW_MEM || page >= HIGH_MEMORY)
		printk("put_dirty_page: trying to put page %p at %p\n",page,address);
	if (mem_map[(page-LOW_MEM)>>12] != 1)
		printk("mem_map disagrees with %p at %p\n",page,address);
	page_table = (unsigned long *) ((address>>20) & 0xffc);
	if ((*page_table)&1)
		page_table = (unsigned long *) (0xfffff000 & *page_table);
	else {
		if (!(tmp=get_free_page()))
			return 0;
		*page_table = tmp|7;
		page_table = (unsigned long *) tmp;
	}
	page_table += (address>>12) & 0x3ff;
	if (*page_table) {
		printk("put_dirty_page: page already exists\n");
		*page_table = 0;
		invalidate();
	}
	*page_table = page | (PAGE_DIRTY | 7);
/* no need for invalidate */
	return page;
}

void un_wp_page(unsigned long * table_entry)
{
	unsigned long old_page;
	unsigned long new_page = 0;
	unsigned long dirty;

repeat:
	old_page = *table_entry;
	dirty = old_page & PAGE_DIRTY;
	if (!(old_page & 1)) {
		if (new_page)
			free_page(new_page);
		return;
	}
	old_page &= 0xfffff000;
	if (old_page >= HIGH_MEMORY) {
		if (new_page)
			free_page(new_page);
		printk("bad page address\n\r");
		do_exit(SIGSEGV);
	}
	if (old_page >= LOW_MEM && mem_map[MAP_NR(old_page)]==1) {
		*table_entry |= 2;
		invalidate();
		if (new_page)
			free_page(new_page);
		return;
	}
	if (!new_page) {
		if (!(new_page=get_free_page()))
			oom();
		goto repeat;
	}
	copy_page(old_page,new_page);
	*table_entry = new_page | dirty | 7;
	free_page(old_page);
	invalidate();
}	

/*
 * This routine handles present pages, when users try to write
 * to a shared page. It is done by copying the page to a new address
 * and decrementing the shared-page counter for the old page.
 *
 * If it's in code space we exit with a segment error.
 */
void do_wp_page(unsigned long error_code,unsigned long address)
{
	if (address < TASK_SIZE) {
		printk("\n\rBAD! KERNEL MEMORY WP-ERR!\n\r");
		do_exit(SIGSEGV);
	}
	if (address - current->start_code >= TASK_SIZE) {
		printk("Bad things happen: page error in do_wp_page\n\r");
		do_exit(SIGSEGV);
	}
	++current->min_flt;
	un_wp_page((unsigned long *)
		(((address>>10) & 0xffc) + (0xfffff000 &
		*((unsigned long *) ((address>>20) &0xffc)))));
}

void write_verify(unsigned long address)
{
	unsigned long page;

	if (!( (page = *((unsigned long *) ((address>>20) & 0xffc)) )&1))
		return;
	page &= 0xfffff000;
	page += ((address>>10) & 0xffc);
	if ((3 & *(unsigned long *) page) == 1)  /* non-writeable, present */
		un_wp_page((unsigned long *) page);
	return;
}

void get_empty_page(unsigned long address)
{
	unsigned long tmp;

	if (!(tmp=get_free_page()) || !put_page(tmp,address)) {
		free_page(tmp);		/* 0 is ok - ignored */
		oom();
	}
}

/*
 * try_to_share() checks the page at address "address" in the task "p",
 * to see if it exists, and if it is clean. If so, share it with the current
 * task.
 *
 * NOTE! This assumes we have checked that p != current, and that they
 * share the same executable or library.
 */
static int try_to_share(unsigned long address, struct task_struct * p)
{
	unsigned long from;
	unsigned long to;
	unsigned long from_page;
	unsigned long to_page;
	unsigned long phys_addr;

	from_page = to_page = ((address>>20) & 0xffc);
	from_page += ((p->start_code>>20) & 0xffc);
	to_page += ((current->start_code>>20) & 0xffc);
/* is there a page-directory at from? */
	from = *(unsigned long *) from_page;
	if (!(from & 1))
		return 0;
	from &= 0xfffff000;
	from_page = from + ((address>>10) & 0xffc);
	phys_addr = *(unsigned long *) from_page;
/* is the page clean and present? */
	if ((phys_addr & 0x41) != 0x01)
		return 0;
	phys_addr &= 0xfffff000;
	if (phys_addr >= HIGH_MEMORY || phys_addr < LOW_MEM)
		return 0;
	to = *(unsigned long *) to_page;
	if (!(to & 1)) {
		if (to = get_free_page())
			*(unsigned long *) to_page = to | 7;
		else
			oom();
	}
	to &= 0xfffff000;
	to_page = to + ((address>>10) & 0xffc);
	if (1 & *(unsigned long *) to_page)
		panic("try_to_share: to_page already exists");
/* share them: write-protect */
	*(unsigned long *) from_page &= ~2;
	*(unsigned long *) to_page = *(unsigned long *) from_page;
	invalidate();
	phys_addr -= LOW_MEM;
	phys_addr >>= 12;
	mem_map[phys_addr]++;
	return 1;
}

/*
 * share_page() tries to find a process that could share a page with
 * the current one. Address is the address of the wanted page relative
 * to the current data space.
 *
 * We first check if it is at all feasible by checking executable->i_count.
 * It should be >1 if there are other tasks sharing this inode.
 */
static int share_page(struct inode * inode, unsigned long address)
{
	struct task_struct ** p;
	int i;

	if (inode->i_count < 2 || !inode)
		return 0;
	for (p = &LAST_TASK ; p > &FIRST_TASK ; --p) {
		if (!*p)
			continue;
		if (current == *p)
			continue;
		if (address < LIBRARY_OFFSET) {
			if (inode != (*p)->executable)
				continue;
		} else {
			for (i=0; i < (*p)->numlibraries; i++)
				if (inode == (*p)->libraries[i].library)
					break;
			if (i >= (*p)->numlibraries)
				continue;
		}
		if (try_to_share(address,*p))
			return 1;
	}
	return 0;
}

/*
 * fill in an empty page or directory if none exists
 */
static unsigned long get_empty(unsigned long * p)
{
	unsigned long page = 0;

repeat:
	if (1 & *p) {
		free_page(page);
		return *p;
	}
	if (*p) {
		printk("get_empty: bad page entry \n");
		*p = 0;
	}
	if (page) {
		*p = page | 7;
		return *p;
	}
	if (!(page = get_free_page()))
		oom();
	goto repeat;
}

void do_no_page(unsigned long error_code, unsigned long address,
	struct task_struct *tsk, unsigned long user_esp)
{
	static unsigned int last_checked = 0;
	int nr[4];
	unsigned long tmp;
	unsigned long page;
	unsigned int block,i;
	struct inode * inode;

	/* Thrashing ? Make it interruptible, but don't penalize otherwise */
	for (i = 0; i < CHECK_LAST_NR; i++)
		if ((address & 0xfffff000) == last_pages[i]) {
			current->counter = 0;
			schedule();
		}
	last_checked++;
	if (last_checked >= CHECK_LAST_NR)
		last_checked = 0;
	last_pages[last_checked] = address & 0xfffff000;
	if (address < TASK_SIZE) {
		printk("\n\rBAD!! KERNEL PAGE MISSING\n\r");
		do_exit(SIGSEGV);
	}
	if (address - tsk->start_code >= TASK_SIZE) {
		printk("Bad things happen: nonexistent page error in do_no_page\n\r");
		do_exit(SIGSEGV);
	}
	page = get_empty((unsigned long *) ((address >> 20) & 0xffc));
	page &= 0xfffff000;
	page += (address >> 10) & 0xffc;
	tmp = *(unsigned long *) page;
	if (tmp & 1) {
		printk("bogus do_no_page\n");
		return;
	}
	++tsk->rss;
	if (tmp) {
		++tsk->maj_flt;
		swap_in((unsigned long *) page);
		return;
	}
	address &= 0xfffff000;
	tmp = address - tsk->start_code;
	inode = NULL;
	block = 0;
	if (tmp < tsk->end_data) {
		inode = tsk->executable;
		block = 1 + tmp / BLOCK_SIZE;
	} else {
		i = tsk->numlibraries;
		while (i-- > 0) {
			if (tmp < tsk->libraries[i].start)
				continue;
			block = tmp - tsk->libraries[i].start;
			if (block >= tsk->libraries[i].length)
				continue;
			inode = tsk->libraries[i].library;
			block = 1 + block / BLOCK_SIZE;
			break;
		}
	}
	if (!inode) {
		++tsk->min_flt;
		get_empty_page(address);
		if (tsk != current)
			return;
		if (tmp >= LIBRARY_OFFSET || tmp < tsk->brk)
			return;
		if (tmp+8192 >= (user_esp & 0xfffff000))
			return;
		send_sig(SIGSEGV,tsk,1);
		return;
	}
	if (tsk == current)
		if (share_page(inode,tmp)) {
			++tsk->min_flt;
			return;
		}
	++tsk->maj_flt;
	if (!(page = get_free_page()))
		oom();
	for (i=0 ; i<4 ; block++,i++)
		nr[i] = bmap(inode,block);
	bread_page(page,inode->i_dev,nr);
	i = tmp + 4096 - tsk->end_data;
	if (i>4095)
		i = 0;
	tmp = page + 4096;
	while (i--) {
		tmp--;
		*(char *)tmp = 0;
	}
	if (put_page(page,address))
		return;
	free_page(page);
	oom();
}

void mem_init(long start_mem, long end_mem)
{
	int i;

	swap_device = 0;
	swap_file = NULL;
	HIGH_MEMORY = end_mem;
	for (i=0 ; i<PAGING_PAGES ; i++)
		mem_map[i] = USED;
	i = MAP_NR(start_mem);
	end_mem -= start_mem;
	end_mem >>= 12;
	while (end_mem-->0)
		mem_map[i++]=0;
}

void show_mem(void)
{
	int i,j,k,free=0,total=0;
	int shared = 0;
	unsigned long * pg_tbl;

	printk("Mem-info:\n\r");
	for(i=0 ; i<PAGING_PAGES ; i++) {
		if (mem_map[i] == USED)
			continue;
		total++;
		if (!mem_map[i])
			free++;
		else
			shared += mem_map[i]-1;
	}
	printk("%d free pages of %d\n\r",free,total);
	printk("%d pages shared\n\r",shared);
	k = 0;
	for(i=4 ; i<1024 ;) {
		if (1&pg_dir[i]) {
			if (pg_dir[i]>HIGH_MEMORY) {
				printk("page directory[%d]: %08X\n\r",
					i,pg_dir[i]);
				i++;
				continue;
			}
			if (pg_dir[i]>LOW_MEM)
				free++,k++;
			pg_tbl=(unsigned long *) (0xfffff000 & pg_dir[i]);
			for(j=0 ; j<1024 ; j++)
				if ((pg_tbl[j]&1) && pg_tbl[j]>LOW_MEM)
					if (pg_tbl[j]>HIGH_MEMORY)
						printk("page_dir[%d][%d]: %08X\n\r",
							i,j, pg_tbl[j]);
					else
						k++,free++;
		}
		i++;
		if (!(i&15) && k) {
			k++,free++;	/* one page/process for task_struct */
			printk("Process %d: %d pages\n\r",(i>>4)-1,k);
			k = 0;
		}
	}
	printk("Memory found: %d (%d)\n\r",free-shared,total);
}


/* This routine handles page faults.  It determines the address,
   and the problem then passes it off to one of the appropriate
   routines. */
void do_page_fault(unsigned long *esp, unsigned long error_code)
{
	unsigned long address;
	unsigned long user_esp;

	if ((0xffff & esp[1]) == 0xf)
		user_esp = esp[3];
	else
		user_esp = 0;
	/* get the address */
	__asm__("movl %%cr2,%0":"=r" (address));
	if (!(error_code & 1)) {
		do_no_page(error_code, address, current, user_esp);
		return;
	} else {
		do_wp_page(error_code, address);
		return;
	}
}
