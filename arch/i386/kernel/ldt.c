/*
 * linux/kernel/ldt.c
 *
 * Copyright (C) 1992 Krishna Balasubramanian and Linus Torvalds
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/vmalloc.h>

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/ldt.h>

static int read_ldt(void * ptr, unsigned long bytecount)
{
	void * address = current->mm->segments;
	unsigned long size;

	if (!ptr)
		return -EINVAL;
	size = LDT_ENTRIES*LDT_ENTRY_SIZE;
	if (!address) {
		address = &default_ldt;
		size = sizeof(default_ldt);
	}
	if (size > bytecount)
		size = bytecount;
	return copy_to_user(ptr, address, size) ? -EFAULT : size;
}

static int write_ldt(void * ptr, unsigned long bytecount, int oldmode)
{
	struct modify_ldt_ldt_s ldt_info;
	unsigned long *lp;
	struct mm_struct * mm;
	int error, i;

	if (bytecount != sizeof(ldt_info))
		return -EINVAL;
	error = copy_from_user(&ldt_info, ptr, sizeof(ldt_info));
	if (error)
		return -EFAULT; 	

	if ((ldt_info.contents == 3 && (oldmode || ldt_info.seg_not_present == 0)) || ldt_info.entry_number >= LDT_ENTRIES)
		return -EINVAL;

	mm = current->mm;

	/*
	 * Horrible dependencies! Try to get rid of this. This is wrong,
	 * as it only reloads the ldt for the first process with this
	 * mm. The implications are that you should really make sure that
	 * you have a ldt before you do the first clone(), otherwise
	 * you get strange behaviour (the kernel is safe, it's just user
	 * space strangeness).
	 *
	 * For no good reason except historical, the GDT index of the LDT
	 * is chosen to follow the index number in the task[] array.
	 */
	if (!mm->segments) {
		for (i=1 ; i<NR_TASKS ; i++) {
			if (task[i] == current) {
				if (!(mm->segments = (void *) vmalloc(LDT_ENTRIES*LDT_ENTRY_SIZE)))
					return -ENOMEM;
				memset(mm->segments, 0, LDT_ENTRIES*LDT_ENTRY_SIZE);
				set_ldt_desc(gdt+(i<<1)+FIRST_LDT_ENTRY, mm->segments, LDT_ENTRIES);
				load_ldt(i);
			}
		}
	}
	
	lp = (unsigned long *) (LDT_ENTRY_SIZE * ldt_info.entry_number + (unsigned long) mm->segments);
   	/* Allow LDTs to be cleared by the user. */
   	if (ldt_info.base_addr == 0 && ldt_info.limit == 0
		&& (oldmode ||
			(  ldt_info.contents == 0
			&& ldt_info.read_exec_only == 1
			&& ldt_info.seg_32bit == 0
			&& ldt_info.limit_in_pages == 0
			&& ldt_info.seg_not_present == 1
			&& ldt_info.useable == 0 )) ) {
		*lp = 0;
		*(lp+1) = 0;
		return 0;
	}
	*lp = ((ldt_info.base_addr & 0x0000ffff) << 16) |
		  (ldt_info.limit & 0x0ffff);
	*(lp+1) = (ldt_info.base_addr & 0xff000000) |
		  ((ldt_info.base_addr & 0x00ff0000)>>16) |
		  (ldt_info.limit & 0xf0000) |
		  (ldt_info.contents << 10) |
		  ((ldt_info.read_exec_only ^ 1) << 9) |
		  (ldt_info.seg_32bit << 22) |
		  (ldt_info.limit_in_pages << 23) |
		  ((ldt_info.seg_not_present ^1) << 15) |
		  0x7000;
	if (!oldmode) *(lp+1) |= (ldt_info.useable << 20);
	return 0;
}

asmlinkage int sys_modify_ldt(int func, void *ptr, unsigned long bytecount)
{
	int ret;

	lock_kernel();
	if (func == 0)
		ret = read_ldt(ptr, bytecount);
	else if (func == 1)
		ret = write_ldt(ptr, bytecount, 1);
	else  if (func == 0x11)
		ret = write_ldt(ptr, bytecount, 0);
	else
		ret = -ENOSYS;
	unlock_kernel();
	return ret;
}
