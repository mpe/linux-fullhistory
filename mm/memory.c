/*
 *  linux/mm/memory.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
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

#include <asm/system.h>

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>
#include <linux/string.h>

#define CODE_SPACE(addr) ((((addr)+4095)&~4095) < \
current->start_code + current->end_code)

unsigned long low_memory = 0;
unsigned long high_memory = 0;
unsigned long paging_pages = 0;

#define copy_page(from,to) \
__asm__("cld ; rep ; movsl"::"S" (from),"D" (to),"c" (1024):"cx","di","si")

unsigned char * mem_map = NULL;

/*
 * oom() prints a message (so that the user knows why the process died),
 * and gives the process an untrappable SIGSEGV.
 */
void oom(struct task_struct * task)
{
	printk("\nout of memory\n");
	task->sigaction[SIGSEGV-1].sa_handler = NULL;
	task->blocked &= ~(1<<(SIGSEGV-1));
	send_sig(SIGSEGV,task,1);
}

int nr_free_pages = 0;
/*
 * Free a page of memory at physical address 'addr'. Used by
 * 'free_page_tables()'
 */
void free_page(unsigned long addr)
{
	unsigned long i;

	if (addr < low_memory)
		return;
	if (addr < high_memory) {
		i = addr - low_memory;
		i >>= 12;
		if (mem_map[i] == 1)
			++nr_free_pages;
		if (mem_map[i]--)
			return;
		mem_map[i] = 0;
	}
	printk("trying to free free page (%08x): memory probably corrupted\n",addr);
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
		if (!(to_page_table = (unsigned long *) get_free_page(GFP_KERNEL)))
			return -1;	/* Out of memory, see freeing */
		*to_dir = ((unsigned long) to_page_table) | 7;
		nr = (from==0)?0xA0:1024;
		for ( ; nr-- > 0 ; from_page_table++,to_page_table++) {
repeat:
			this_page = *from_page_table;
			if (!this_page)
				continue;
			if (!(1 & this_page)) {
				if (!(new_page = get_free_page(GFP_KERNEL)))
					return -1;
				++current->rss;
				read_swap_page(this_page>>1, (char *) new_page);
				if (*from_page_table != this_page) {
					free_page(new_page);
					goto repeat;
				}
				*to_page_table = this_page;
				*from_page_table = new_page | (PAGE_DIRTY | 7);
				continue;
			}
			this_page &= ~2;
			*to_page_table = this_page;
			if (this_page > low_memory) {
				*from_page_table = this_page;
				this_page -= low_memory;
				this_page >>= 12;
				if (!mem_map[this_page]++)
					--nr_free_pages;
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
int unmap_page_range(unsigned long from, unsigned long size)
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
int remap_page_range(unsigned long from, unsigned long to, unsigned long size,
		 int permiss)
{
	unsigned long *page_table, *dir;
	unsigned long poff, pcnt;
	unsigned long page;

	if ((from & 0xfff) || (to & 0xfff))
		panic("remap_page_range called with wrong alignment");
	dir = (unsigned long *) ((from >> 20) & 0xffc); /* _pg_dir = 0 */
	size = (size + 0xfff) >> 12;
	poff = (from >> 12) & 0x3ff;
	if ((pcnt = 1024 - poff) > size)
		pcnt = size;

	while (size > 0) {
		if (!(1 & *dir)) {
			if (!(page_table = (unsigned long *)get_free_page(GFP_KERNEL))) {
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

			if (page = *page_table) {
				*page_table = 0;
				--current->rss;
				if (1 & page)
					free_page(0xfffff000 & page);
				else
					swap_free(page >> 1);
			}

			/*
			 * i'm not sure of the second cond here. should we
			 * report failure?
			 * the first condition should return an invalid access
			 * when the page is referenced. current assumptions
			 * cause it to be treated as demand allocation.
			 */
			if (mask == 4 || to >= high_memory)
				*page_table++ = 0;	/* not present */
			else {
				++current->rss;
				*page_table++ = (to | mask);
				if (to > low_memory) {
					unsigned long frame;
					frame = to - low_memory;
					frame >>= 12;
					if (!mem_map[frame]++)
						--nr_free_pages;
				}
			}
			to += PAGE_SIZE;
		}
		pcnt = (size > 1024 ? 1024 : size);
	}
	invalidate();
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

	if (page >= high_memory) {
		printk("put_page: trying to put page %p at %p\n",page,address);
		return 0;
	}
	if (page >= low_memory && mem_map[(page-low_memory)>>12] != 1) {
		printk("put_page: mem_map disagrees with %p at %p\n",page,address);
		return 0;
	}
	page_table = (unsigned long *) ((address>>20) & 0xffc);
	if ((*page_table)&1)
		page_table = (unsigned long *) (0xfffff000 & *page_table);
	else {
		tmp = get_free_page(GFP_KERNEL);
		if (!tmp) {
			oom(current);
			tmp = BAD_PAGETABLE;
		}
		*page_table = tmp | 7;
		return 0;
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

	if (page < low_memory || page >= high_memory)
		printk("put_dirty_page: trying to put page %p at %p\n",page,address);
	if (mem_map[(page-low_memory)>>12] != 1)
		printk("mem_map disagrees with %p at %p\n",page,address);
	page_table = (unsigned long *) ((address>>20) & 0xffc);
	if ((*page_table)&1)
		page_table = (unsigned long *) (0xfffff000 & *page_table);
	else {
		if (!(tmp=get_free_page(GFP_KERNEL)))
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

static void un_wp_page(unsigned long * table_entry, struct task_struct * task)
{
	unsigned long old_page;
	unsigned long new_page = 0;
	unsigned long dirty;

repeat:
	old_page = *table_entry;
	if (!(old_page & 1)) {
		if (new_page)
			free_page(new_page);
		return;
	}
	dirty = old_page & PAGE_DIRTY;
	old_page &= 0xfffff000;
	if (old_page >= high_memory) {
		if (new_page)
			free_page(new_page);
		printk("bad page address\n\r");
		send_sig(SIGSEGV, task, 1);
		*table_entry = BAD_PAGE | 7;
		return;
	}
	if (old_page >= low_memory && mem_map[MAP_NR(old_page)]==1) {
		*table_entry |= 2;
		invalidate();
		if (new_page)
			free_page(new_page);
		return;
	}
	if (!new_page && (new_page=get_free_page(GFP_KERNEL)))
		goto repeat;
	if (new_page)
		copy_page(old_page,new_page);
	else {
		new_page = BAD_PAGE;
		send_sig(SIGSEGV,task,1);
	}
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
void do_wp_page(unsigned long error_code, unsigned long address,
	struct task_struct * tsk, unsigned long user_esp)
{
	unsigned long pde, pte, page;

	pde = (address>>20) & 0xffc;
	pte = *(unsigned long *) pde;
	if ((pte & 3) != 3) {
		printk("do_wp_page: bogus page-table at address %08x (%08x)\n",address,pte);
		*(unsigned long *) pde = BAD_PAGETABLE | 7;
		send_sig(SIGSEGV, tsk, 1);
		return;
	}
	if (address < TASK_SIZE) {
		printk("do_wp_page: kernel WP error at address %08x (%08x)\n",address,pte);
		*(unsigned long *) pde = BAD_PAGETABLE | 7;
		send_sig(SIGSEGV, tsk, 1);
		return;
	}
	pte &= 0xfffff000;
	pte += (address>>10) & 0xffc;
	page = *(unsigned long *) pte;
	if ((page & 3) != 1) {
		printk("do_wp_page: bogus page at address %08x (%08x)\n",address,page);
		*(unsigned long *) pte = BAD_PAGE | 7;
		send_sig(SIGSEGV, tsk, 1);
		return;
	}
	++current->min_flt;
	un_wp_page((unsigned long *) pte, tsk);
}

void write_verify(unsigned long address)
{
	unsigned long page;

	page = *(unsigned long *) ((address>>20) & 0xffc);
	if (!(page & PAGE_PRESENT))
		return;
	page &= 0xfffff000;
	page += ((address>>10) & 0xffc);
	if ((3 & *(unsigned long *) page) == 1)  /* non-writeable, present */
		un_wp_page((unsigned long *) page, current);
	return;
}

static void get_empty_page(unsigned long address)
{
	unsigned long tmp;

	tmp = get_free_page(GFP_KERNEL);
	if (!tmp) {
		oom(current);
		tmp = BAD_PAGE;
	}
	if (!put_page(tmp,address))
		free_page(tmp);
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
	if (phys_addr >= high_memory || phys_addr < low_memory)
		return 0;
	to = *(unsigned long *) to_page;
	if (!(to & 1)) {
		to = get_free_page(GFP_KERNEL);
		if (!to)
			return 0;
		*(unsigned long *) to_page = to | 7;
	}
	to &= 0xfffff000;
	to_page = to + ((address>>10) & 0xffc);
	if (1 & *(unsigned long *) to_page)
		panic("try_to_share: to_page already exists");
/* share them: write-protect */
	*(unsigned long *) from_page &= ~2;
	*(unsigned long *) to_page = *(unsigned long *) from_page;
	invalidate();
	phys_addr -= low_memory;
	phys_addr >>= 12;
	if (!mem_map[phys_addr]++)
		--nr_free_pages;
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

	if (!inode || inode->i_count < 2)
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
 * fill in an empty page-table if none exists
 */
static unsigned long get_empty_pgtable(unsigned long * p)
{
	unsigned long page = 0;

repeat:
	if (1 & *p) {
		free_page(page);
		return *p;
	}
	if (*p) {
		printk("get_empty_pgtable: bad page-directory entry \n");
		*p = 0;
	}
	if (page) {
		*p = page | 7;
		return *p;
	}
	if (page = get_free_page(GFP_KERNEL))
		goto repeat;
	oom(current);
	*p = BAD_PAGETABLE | 7;
	return 0;
}

void do_no_page(unsigned long error_code, unsigned long address,
	struct task_struct *tsk, unsigned long user_esp)
{
	int nr[4];
	unsigned long tmp;
	unsigned long page;
	unsigned int block,i;
	struct inode * inode;

	if (address < TASK_SIZE) {
		printk("\n\rBAD!! KERNEL PAGE MISSING\n\r");
		do_exit(SIGSEGV);
	}
	if (address - tsk->start_code >= TASK_SIZE) {
		printk("Bad things happen: nonexistent page error in do_no_page\n\r");
		do_exit(SIGSEGV);
	}
	page = get_empty_pgtable((unsigned long *) ((address >> 20) & 0xffc));
	if (!page)
		return;
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
	page = get_free_page(GFP_KERNEL);
	if (!page) {
		oom(current);
		put_page(BAD_PAGE,address);
		return;
	}
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
	oom(current);
}

void show_mem(void)
{
	int i,j,k,free=0,total=0;
	int shared = 0;
	unsigned long * pg_tbl;

	printk("Mem-info:\n\r");
	printk("Free pages:    %6d\n",nr_free_pages);
	printk("Buffer heads:  %6d\n",nr_buffer_heads);
	printk("Buffer blocks: %6d\n",nr_buffers);
	for (i = 0 ; i < paging_pages ; i++) {
		total++;
		if (!mem_map[i])
			free++;
		else
			shared += mem_map[i]-1;
	}
	printk("%d free pages of %d\n\r",free,total);
	printk("%d pages shared\n\r",shared);
	printk("%d free pages via nr_free_pages\n\r", nr_free_pages);
	k = 0;
	for(i=4 ; i<1024 ;) {
		if (1&pg_dir[i]) {
			if (pg_dir[i]>high_memory) {
				printk("page directory[%d]: %08X\n\r",
					i,pg_dir[i]);
				i++;
				continue;
			}
			if (pg_dir[i]>low_memory)
				free++,k++;
			pg_tbl=(unsigned long *) (0xfffff000 & pg_dir[i]);
			for(j=0 ; j<1024 ; j++)
				if ((pg_tbl[j]&1) && pg_tbl[j]>low_memory)
					if (pg_tbl[j]>high_memory)
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
		do_wp_page(error_code, address, current, user_esp);
		return;
	}
}

unsigned long mem_init(unsigned long start_mem, unsigned long end_mem)
{
	end_mem &= 0xfffff000;
	high_memory = end_mem;
	mem_map = (char *) start_mem;
	paging_pages = (end_mem - start_mem) >> 12;
	start_mem += paging_pages;
	start_mem += 0xfff;
	start_mem &= 0xfffff000;
	low_memory = start_mem;
	paging_pages = (high_memory - low_memory) >> 12;
	swap_device = 0;
	swap_file = NULL;
	memset(mem_map,0,paging_pages);
	nr_free_pages = paging_pages;
	return start_mem;
}
