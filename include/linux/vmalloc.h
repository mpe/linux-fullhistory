#ifndef __LINUX_VMALLOC_H
#define __LINUX_VMALLOC_H

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/spinlock.h>

#include <asm/pgtable.h>

/* bits in vm_struct->flags */
#define VM_IOREMAP	0x00000001	/* ioremap() and friends */
#define VM_ALLOC	0x00000002	/* vmalloc() */

struct vm_struct {
	unsigned long flags;
	void * addr;
	unsigned long size;
	struct vm_struct * next;
};

struct vm_struct * get_vm_area(unsigned long size, unsigned long flags);
void vfree(void * addr);
void * vmalloc(unsigned long size);
long vread(char *buf, char *addr, unsigned long count);
void vmfree_area_pages(unsigned long address, unsigned long size);
int vmalloc_area_pages(unsigned long address, unsigned long size);

/* vmlist_lock is a read-write spinlock that protects vmlist 
 * Used in mm/vmalloc.c (get_vm_area() and vfree()) and fs/proc/kcore.c.
 */
extern rwlock_t vmlist_lock;
 
extern struct vm_struct * vmlist;
#endif

