#ifndef __LINUX_VMALLOC_H
#define __LINUX_VMALLOC_H

#include <linux/sched.h>
#include <linux/mm.h>

#include <asm/pgtable.h>

struct vm_struct {
	unsigned long flags;
	void * addr;
	unsigned long size;
	struct vm_struct * next;
};

struct vm_struct * get_vm_area(unsigned long size);
void vfree(void * addr);
void * vmalloc(unsigned long size);
int vread(char *buf, char *addr, int count);
void vmfree_area_pages(unsigned long address, unsigned long size);
int vmalloc_area_pages(unsigned long address, unsigned long size);

extern inline void set_pgdir(unsigned long address, pgd_t entry)
{
#if !defined(__mc68000__) && !defined(__sparc_v9__)
	struct task_struct * p;

	read_lock(&tasklist_lock);
	for_each_task(p) {
		if (!p->mm)
			continue;
		*pgd_offset(p->mm,address) = entry;
	}
	read_unlock(&tasklist_lock);
#endif
}

#endif

