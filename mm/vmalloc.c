/*
 *  linux/mm/vmalloc.c
 *
 *  Copyright (C) 1993  Linus Torvalds
 */

#include <asm/system.h>

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/malloc.h>
#include <linux/mm.h>

#include <asm/segment.h>

struct vm_struct {
	unsigned long flags;
	void * addr;
	unsigned long size;
	struct vm_struct * next;
};

static struct vm_struct * vmlist = NULL;

static inline void set_pgdir(unsigned long dindex, pte_t * page_table)
{
	struct task_struct * p;

	p = &init_task;
	do {
		pgd_set(PAGE_DIR_OFFSET(p,0) + dindex, page_table);
		p = p->next_task;
	} while (p != &init_task);
}

static inline void clear_pgdir(unsigned long dindex)
{
	struct task_struct * p;

	p = &init_task;
	do {
		pgd_clear(PAGE_DIR_OFFSET(p,0) + dindex);
		p = p->next_task;
	} while (p != &init_task);
}

static int free_area_pages(unsigned long dindex, unsigned long index, unsigned long nr)
{
	pgd_t * dir;
	pte_t * page_table;
	unsigned long page;

	dir = swapper_pg_dir + dindex;
	if (pgd_none(*dir))
		return 0;
	if (pgd_bad(*dir)) {
		printk("bad page directory entry in free_area_pages: %08lx\n", pgd_val(*dir));
		pgd_clear(dir);
		return 0;
	}
	page = pgd_page(*dir);
	page_table = index + (pte_t *) page;
	do {
		pte_t pte = *page_table;
		pte_clear(page_table);
		if (pte_present(pte))
			free_page(pte_page(pte));
		page_table++;
	} while (--nr);
	page_table = (pte_t *) page;
	for (nr = 0 ; nr < PTRS_PER_PAGE ; nr++, page_table++)
		if (!pte_none(*page_table))
			return 0;
	clear_pgdir(dindex);
	mem_map[MAP_NR(page)] = 1;
	free_page(page);
	invalidate();
	return 0;
}

static int alloc_area_pages(unsigned long dindex, unsigned long index, unsigned long nr)
{
	pgd_t *dir;
	pte_t *page_table;

	dir = swapper_pg_dir + dindex;
	if (pgd_none(*dir)) {
		unsigned long page = get_free_page(GFP_KERNEL);
		if (!page)
			return -ENOMEM;
		if (!pgd_none(*dir)) {
			free_page(page);
		} else {
			mem_map[MAP_NR(page)] = MAP_PAGE_RESERVED;
			set_pgdir(dindex, (pte_t *) page);
		}
	}
	if (pgd_bad(*dir)) {
		printk("Bad page dir entry in alloc_area_pages (%08lx)\n", pgd_val(*dir));
		return -ENOMEM;
	}
	page_table = index + (pte_t *) pgd_page(*dir);
	/*
	 * use a tempotary page-table entry to remove a race with
	 * vfree(): it mustn't free the page table from under us
	 * if we sleep in get_free_page()
	 */
	*page_table = BAD_PAGE;
	do {
		unsigned long pg = get_free_page(GFP_KERNEL);

		if (!pg)
			return -ENOMEM;
		*page_table = mk_pte(pg, PAGE_KERNEL);
		page_table++;
	} while (--nr);
	invalidate();
	return 0;
}

static int do_area(void * addr, unsigned long size,
	int (*area_fn)(unsigned long,unsigned long,unsigned long))
{
	unsigned long nr, dindex, index;

	nr = size >> PAGE_SHIFT;
	dindex = VMALLOC_VMADDR(addr);
	index = (dindex >> PAGE_SHIFT) & (PTRS_PER_PAGE-1);
	dindex = (dindex >> PGDIR_SHIFT) & (PTRS_PER_PAGE-1);
	while (nr > 0) {
		unsigned long i = PTRS_PER_PAGE - index;

		if (i > nr)
			i = nr;
		nr -= i;
		if (area_fn(dindex, index, i))
			return -1;
		index = 0;
		dindex++;
	}
	return 0;
}

void vfree(void * addr)
{
	struct vm_struct **p, *tmp;

	if (!addr)
		return;
	if ((PAGE_SIZE-1) & (unsigned long) addr) {
		printk("Trying to vfree() bad address (%p)\n", addr);
		return;
	}
	for (p = &vmlist ; (tmp = *p) ; p = &tmp->next) {
		if (tmp->addr == addr) {
			*p = tmp->next;
			do_area(tmp->addr, tmp->size, free_area_pages);
			kfree(tmp);
			return;
		}
	}
	printk("Trying to vfree() nonexistent vm area (%p)\n", addr);
}

void * vmalloc(unsigned long size)
{
	void * addr;
	struct vm_struct **p, *tmp, *area;

	size = PAGE_ALIGN(size);
	if (!size || size > high_memory)
		return NULL;
	area = (struct vm_struct *) kmalloc(sizeof(*area), GFP_KERNEL);
	if (!area)
		return NULL;
	addr = (void *) VMALLOC_START;
	area->size = size + PAGE_SIZE;
	area->next = NULL;
	for (p = &vmlist; (tmp = *p) ; p = &tmp->next) {
		if (size + (unsigned long) addr < (unsigned long) tmp->addr)
			break;
		addr = (void *) (tmp->size + (unsigned long) tmp->addr);
	}
	area->addr = addr;
	area->next = *p;
	*p = area;
	if (do_area(addr, size, alloc_area_pages)) {
		vfree(addr);
		return NULL;
	}
	return addr;
}

int vread(char *buf, char *addr, int count)
{
	struct vm_struct **p, *tmp;
	char *vaddr, *buf_start = buf;
	int n;

	for (p = &vmlist; (tmp = *p) ; p = &tmp->next) {
		vaddr = (char *) tmp->addr;
		while (addr < vaddr) {
			if (count == 0)
				goto finished;
			put_fs_byte('\0', buf++), addr++, count--;
		}
		n = tmp->size - PAGE_SIZE;
		if (addr > vaddr)
			n -= addr - vaddr;
		while (--n >= 0) {
			if (count == 0)
				goto finished;
			put_fs_byte(*addr++, buf++), count--;
		}
	}
finished:
	return buf - buf_start;
}
