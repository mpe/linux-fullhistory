/*
 *  linux/mm/swapfile.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *  Swap reorganised 29.12.95, Stephen Tweedie
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
#include <linux/blkdev.h> /* for blk_size */

#include <asm/dma.h>
#include <asm/system.h> /* for cli()/sti() */
#include <asm/segment.h> /* for memcpy_to/fromfs */
#include <asm/bitops.h>
#include <asm/pgtable.h>

int nr_swapfiles = 0;
static struct {
	int head;	/* head of priority-ordered swapfile list */
	int next;	/* swapfile to be used next */
} swap_list = {-1, -1};

struct swap_info_struct swap_info[MAX_SWAPFILES];


static inline int scan_swap_map(struct swap_info_struct *si)
{
	int offset;
	/* 
	 * We try to cluster swap pages by allocating them
	 * sequentially in swap.  Once we've allocated
	 * SWAP_CLUSTER_MAX pages this way, however, we resort to
	 * first-free allocation, starting a new cluster.  This
	 * prevents us from scattering swap pages all over the entire
	 * swap partition, so that we reduce overall disk seek times
	 * between swap pages.  -- sct */
	if (si->cluster_nr) {
		while (si->cluster_next <= si->highest_bit) {
			offset = si->cluster_next++;
			if (si->swap_map[offset])
				continue;
			if (test_bit(offset, si->swap_lockmap))
				continue;
			si->cluster_nr--;
			goto got_page;
		}
	}
	si->cluster_nr = SWAP_CLUSTER_MAX;
	for (offset = si->lowest_bit; offset <= si->highest_bit ; offset++) {
		if (si->swap_map[offset])
			continue;
		if (test_bit(offset, si->swap_lockmap))
			continue;
		si->lowest_bit = offset;
got_page:
		si->swap_map[offset] = 1;
		nr_swap_pages--;
		if (offset == si->highest_bit)
			si->highest_bit--;
		si->cluster_next = offset;
		return offset;
	}
	return 0;
}

unsigned long get_swap_page(void)
{
	struct swap_info_struct * p;
	unsigned long offset, entry;
	int type, wrapped = 0;

	type = swap_list.next;
	if (type < 0)
	  return 0;

	while (1) {
		p = &swap_info[type];
		if ((p->flags & SWP_WRITEOK) == SWP_WRITEOK) {
			offset = scan_swap_map(p);
			if (offset) {
				entry = SWP_ENTRY(type,offset);
				type = swap_info[type].next;
				if (type < 0 ||
					p->prio != swap_info[type].prio) 
				{
						swap_list.next = swap_list.head;
				}
				else
				{
					swap_list.next = type;
				}
				return entry;
			}
		}
		type = p->next;
		if (!wrapped) {
			if (type < 0 || p->prio != swap_info[type].prio) {
				type = swap_list.head;
				wrapped = 1;
			}
		} else if (type < 0) {
			return 0;	/* out of swap space */
		}
	}
}

void swap_free(unsigned long entry)
{
	struct swap_info_struct * p;
	unsigned long offset, type;

	if (!entry)
		return;
	type = SWP_TYPE(entry);
	if (type & SHM_SWP_TYPE)
		return;
	if (type >= nr_swapfiles) {
		printk("Trying to free nonexistent swap-page\n");
		return;
	}
	p = & swap_info[type];
	offset = SWP_OFFSET(entry);
	if (offset >= p->max) {
		printk("swap_free: weirdness\n");
		return;
	}
	if (!(p->flags & SWP_USED)) {
		printk("Trying to free swap from unused swap-device\n");
		return;
	}
	if (offset < p->lowest_bit)
		p->lowest_bit = offset;
	if (offset > p->highest_bit)
		p->highest_bit = offset;
	if (!p->swap_map[offset])
		printk("swap_free: swap-space map bad (entry %08lx)\n",entry);
	else
		if (!--p->swap_map[offset])
			nr_swap_pages++;
	if (p->prio > swap_info[swap_list.next].prio) {
	    swap_list.next = swap_list.head;
	}
}

/*
 * Trying to stop swapping from a file is fraught with races, so
 * we repeat quite a bit here when we have to pause. swapoff()
 * isn't exactly timing-critical, so who cares (but this is /really/
 * inefficient, ugh).
 *
 * We return 1 after having slept, which makes the process start over
 * from the beginning for this process..
 */
static inline int unuse_pte(struct vm_area_struct * vma, unsigned long address,
	pte_t *dir, unsigned int type, unsigned long page)
{
	pte_t pte = *dir;

	if (pte_none(pte))
		return 0;
	if (pte_present(pte)) {
		unsigned long page_nr = MAP_NR(pte_page(pte));
		if (page_nr >= MAP_NR(high_memory))
			return 0;
		if (!in_swap_cache(page_nr))
			return 0;
		if (SWP_TYPE(in_swap_cache(page_nr)) != type)
			return 0;
		delete_from_swap_cache(page_nr);
		set_pte(dir, pte_mkdirty(pte));
		return 0;
	}
	if (SWP_TYPE(pte_val(pte)) != type)
		return 0;
	read_swap_page(pte_val(pte), (char *) page);
	if (pte_val(*dir) != pte_val(pte)) {
		free_page(page);
		return 1;
	}
	set_pte(dir, pte_mkwrite(pte_mkdirty(mk_pte(page, vma->vm_page_prot))));
	flush_tlb_page(vma, address);
	++vma->vm_mm->rss;
	swap_free(pte_val(pte));
	return 1;
}

static inline int unuse_pmd(struct vm_area_struct * vma, pmd_t *dir,
	unsigned long address, unsigned long size, unsigned long offset,
	unsigned int type, unsigned long page)
{
	pte_t * pte;
	unsigned long end;

	if (pmd_none(*dir))
		return 0;
	if (pmd_bad(*dir)) {
		printk("unuse_pmd: bad pmd (%08lx)\n", pmd_val(*dir));
		pmd_clear(dir);
		return 0;
	}
	pte = pte_offset(dir, address);
	offset += address & PMD_MASK;
	address &= ~PMD_MASK;
	end = address + size;
	if (end > PMD_SIZE)
		end = PMD_SIZE;
	do {
		if (unuse_pte(vma, offset+address-vma->vm_start, pte, type, page))
			return 1;
		address += PAGE_SIZE;
		pte++;
	} while (address < end);
	return 0;
}

static inline int unuse_pgd(struct vm_area_struct * vma, pgd_t *dir,
	unsigned long address, unsigned long size,
	unsigned int type, unsigned long page)
{
	pmd_t * pmd;
	unsigned long offset, end;

	if (pgd_none(*dir))
		return 0;
	if (pgd_bad(*dir)) {
		printk("unuse_pgd: bad pgd (%08lx)\n", pgd_val(*dir));
		pgd_clear(dir);
		return 0;
	}
	pmd = pmd_offset(dir, address);
	offset = address & PGDIR_MASK;
	address &= ~PGDIR_MASK;
	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;
	do {
		if (unuse_pmd(vma, pmd, address, end - address, offset, type, page))
			return 1;
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address < end);
	return 0;
}

static int unuse_vma(struct vm_area_struct * vma, pgd_t *pgdir,
	unsigned long start, unsigned long end,
	unsigned int type, unsigned long page)
{
	while (start < end) {
		if (unuse_pgd(vma, pgdir, start, end - start, type, page))
			return 1;
		start = (start + PGDIR_SIZE) & PGDIR_MASK;
		pgdir++;
	}
	return 0;
}

static int unuse_process(struct mm_struct * mm, unsigned int type, unsigned long page)
{
	struct vm_area_struct* vma;

	/*
	 * Go through process' page directory.
	 */
	if (!mm || mm == &init_mm)
		return 0;
	vma = mm->mmap;
	while (vma) {
		pgd_t * pgd = pgd_offset(mm, vma->vm_start);
		if (unuse_vma(vma, pgd, vma->vm_start, vma->vm_end, type, page))
			return 1;
		vma = vma->vm_next;
	}
	return 0;
}

/*
 * To avoid races, we repeat for each process after having
 * swapped something in. That gets rid of a few pesky races,
 * and "swapoff" isn't exactly timing critical.
 */
static int try_to_unuse(unsigned int type)
{
	int nr;
	unsigned long page = get_free_page(GFP_KERNEL);

	if (!page)
		return -ENOMEM;
	nr = 0;
	while (nr < NR_TASKS) {
		struct task_struct * p = task[nr];
		if (p) {
			if (unuse_process(p->mm, type, page)) {
				page = get_free_page(GFP_KERNEL);
				if (!page)
					return -ENOMEM;
				continue;
			}
		}
		nr++;
	}
	free_page(page);
	return 0;
}

asmlinkage int sys_swapoff(const char * specialfile)
{
	struct swap_info_struct * p;
	struct inode * inode;
	struct file filp;
	int i, type, prev;
	int err;

	if (!suser())
		return -EPERM;
	err = namei(specialfile,&inode);
	if (err)
		return err;
	prev = -1;
	for (type = swap_list.head; type >= 0; type = swap_info[type].next) {
		p = swap_info + type;
		if ((p->flags & SWP_WRITEOK) == SWP_WRITEOK) {
			if (p->swap_file) {
				if (p->swap_file == inode)
				  break;
			} else {
				if (S_ISBLK(inode->i_mode)
				    && (p->swap_device == inode->i_rdev))
				  break;
			}
		}
		prev = type;
	}
	if (type < 0){
		iput(inode);
		return -EINVAL;
	}
	if (prev < 0) {
		swap_list.head = p->next;
	} else {
		swap_info[prev].next = p->next;
	}
	if (type == swap_list.next) {
		/* just pick something that's safe... */
		swap_list.next = swap_list.head;
	}
	p->flags = SWP_USED;
	err = try_to_unuse(type);
	if (err) {
		iput(inode);
		/* re-insert swap space back into swap_list */
		for (prev = -1, i = swap_list.head; i >= 0; prev = i, i = swap_info[i].next)
			if (p->prio >= swap_info[i].prio)
				break;
		p->next = i;
		if (prev < 0)
			swap_list.head = swap_list.next = p - swap_info;
		else
			swap_info[prev].next = p - swap_info;
		p->flags = SWP_WRITEOK;
		return err;
	}
	if(p->swap_device){
		memset(&filp, 0, sizeof(filp));		
		filp.f_inode = inode;
		filp.f_mode = 3; /* read write */
		/* open it again to get fops */
		if( !blkdev_open(inode, &filp) &&
		   filp.f_op && filp.f_op->release){
			filp.f_op->release(inode,&filp);
			filp.f_op->release(inode,&filp);
		}
	}
	iput(inode);

	nr_swap_pages -= p->pages;
	iput(p->swap_file);
	p->swap_file = NULL;
	p->swap_device = 0;
	vfree(p->swap_map);
	p->swap_map = NULL;
	free_page((long) p->swap_lockmap);
	p->swap_lockmap = NULL;
	p->flags = 0;
	return 0;
}

/*
 * Written 01/25/92 by Simmule Turner, heavily changed by Linus.
 *
 * The swapon system call
 */
asmlinkage int sys_swapon(const char * specialfile, int swap_flags)
{
	struct swap_info_struct * p;
	struct inode * swap_inode;
	unsigned int type;
	int i, j, prev;
	int error;
	struct file filp;
	static int least_priority = 0;

	memset(&filp, 0, sizeof(filp));
	if (!suser())
		return -EPERM;
	p = swap_info;
	for (type = 0 ; type < nr_swapfiles ; type++,p++)
		if (!(p->flags & SWP_USED))
			break;
	if (type >= MAX_SWAPFILES)
		return -EPERM;
	if (type >= nr_swapfiles)
		nr_swapfiles = type+1;
	p->flags = SWP_USED;
	p->swap_file = NULL;
	p->swap_device = 0;
	p->swap_map = NULL;
	p->swap_lockmap = NULL;
	p->lowest_bit = 0;
	p->highest_bit = 0;
	p->cluster_nr = 0;
	p->max = 1;
	p->next = -1;
	if (swap_flags & SWAP_FLAG_PREFER) {
		p->prio =
		  (swap_flags & SWAP_FLAG_PRIO_MASK)>>SWAP_FLAG_PRIO_SHIFT;
	} else {
		p->prio = --least_priority;
	}
	error = namei(specialfile,&swap_inode);
	if (error)
		goto bad_swap_2;
	p->swap_file = swap_inode;
	error = -EBUSY;
	if (swap_inode->i_count != 1)
		goto bad_swap_2;
	error = -EINVAL;

	if (S_ISBLK(swap_inode->i_mode)) {
		p->swap_device = swap_inode->i_rdev;
		set_blocksize(p->swap_device, PAGE_SIZE);
		
		filp.f_inode = swap_inode;
		filp.f_mode = 3; /* read write */
		error = blkdev_open(swap_inode, &filp);
		p->swap_file = NULL;
		iput(swap_inode);
		if(error)
			goto bad_swap_2;
		error = -ENODEV;
		if (!p->swap_device ||
		    (blk_size[MAJOR(p->swap_device)] &&
		     !blk_size[MAJOR(p->swap_device)][MINOR(p->swap_device)]))
			goto bad_swap;
		error = -EBUSY;
		for (i = 0 ; i < nr_swapfiles ; i++) {
			if (i == type)
				continue;
			if (p->swap_device == swap_info[i].swap_device)
				goto bad_swap;
		}
	} else if (!S_ISREG(swap_inode->i_mode))
		goto bad_swap;
	p->swap_lockmap = (unsigned char *) get_free_page(GFP_USER);
	if (!p->swap_lockmap) {
		printk("Unable to start swapping: out of memory :-)\n");
		error = -ENOMEM;
		goto bad_swap;
	}
	read_swap_page(SWP_ENTRY(type,0), (char *) p->swap_lockmap);
	if (memcmp("SWAP-SPACE",p->swap_lockmap+PAGE_SIZE-10,10)) {
		printk("Unable to find swap-space signature\n");
		error = -EINVAL;
		goto bad_swap;
	}
	memset(p->swap_lockmap+PAGE_SIZE-10,0,10);
	j = 0;
	p->lowest_bit = 0;
	p->highest_bit = 0;
	for (i = 1 ; i < 8*PAGE_SIZE ; i++) {
		if (test_bit(i,p->swap_lockmap)) {
			if (!p->lowest_bit)
				p->lowest_bit = i;
			p->highest_bit = i;
			p->max = i+1;
			j++;
		}
	}
	if (!j) {
		printk("Empty swap-file\n");
		error = -EINVAL;
		goto bad_swap;
	}
	p->swap_map = (unsigned char *) vmalloc(p->max);
	if (!p->swap_map) {
		error = -ENOMEM;
		goto bad_swap;
	}
	for (i = 1 ; i < p->max ; i++) {
		if (test_bit(i,p->swap_lockmap))
			p->swap_map[i] = 0;
		else
			p->swap_map[i] = 0x80;
	}
	p->swap_map[0] = 0x80;
	memset(p->swap_lockmap,0,PAGE_SIZE);
	p->flags = SWP_WRITEOK;
	p->pages = j;
	nr_swap_pages += j;
	printk("Adding Swap: %dk swap-space\n",j<<(PAGE_SHIFT-10));

	/* insert swap space into swap_list: */
	prev = -1;
	for (i = swap_list.head; i >= 0; i = swap_info[i].next) {
		if (p->prio >= swap_info[i].prio) {
			break;
		}
		prev = i;
	}
	p->next = i;
	if (prev < 0) {
		swap_list.head = swap_list.next = p - swap_info;
	} else {
		swap_info[prev].next = p - swap_info;
	}
	return 0;
bad_swap:
	if(filp.f_op && filp.f_op->release)
		filp.f_op->release(filp.f_inode,&filp);
bad_swap_2:
	free_page((long) p->swap_lockmap);
	vfree(p->swap_map);
	iput(p->swap_file);
	p->swap_device = 0;
	p->swap_file = NULL;
	p->swap_map = NULL;
	p->swap_lockmap = NULL;
	p->flags = 0;
	return error;
}

void si_swapinfo(struct sysinfo *val)
{
	unsigned int i, j;

	val->freeswap = val->totalswap = 0;
	for (i = 0; i < nr_swapfiles; i++) {
		if ((swap_info[i].flags & SWP_WRITEOK) != SWP_WRITEOK)
			continue;
		for (j = 0; j < swap_info[i].max; ++j)
			switch (swap_info[i].swap_map[j]) {
				case 128:
					continue;
				case 0:
					++val->freeswap;
				default:
					++val->totalswap;
			}
	}
	val->freeswap <<= PAGE_SHIFT;
	val->totalswap <<= PAGE_SHIFT;
	return;
}

