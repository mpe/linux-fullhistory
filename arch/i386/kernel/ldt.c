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
#include <asm/desc.h>

static int read_ldt(void * ptr, unsigned long bytecount)
{
	void * address = current->mm->segments;
	unsigned long size;

	if (!ptr)
		return -EINVAL;
	if (!address)
		return 0;
	size = LDT_ENTRIES*LDT_ENTRY_SIZE;
	if (size > bytecount)
		size = bytecount;
	return copy_to_user(ptr, address, size) ? -EFAULT : size;
}

static int write_ldt(void * ptr, unsigned long bytecount, int oldmode)
{
	struct mm_struct * mm = current->mm;
	__u32 entry_1, entry_2, *lp;
	int error;
	struct modify_ldt_ldt_s ldt_info;

	error = -EINVAL;
	if (bytecount != sizeof(ldt_info))
		goto out;
	error = -EFAULT; 	
	if (copy_from_user(&ldt_info, ptr, sizeof(ldt_info)))
		goto out;

	error = -EINVAL;
	if (ldt_info.entry_number >= LDT_ENTRIES)
		goto out;
	if (ldt_info.contents == 3) {
		if (oldmode)
			goto out;
		if (ldt_info.seg_not_present == 0)
			goto out;
	}

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
		void * ldt;
		error = -ENOMEM;
		ldt = vmalloc(LDT_ENTRIES*LDT_ENTRY_SIZE);
		if (!ldt)
			goto out;
		memset(ldt, 0, LDT_ENTRIES*LDT_ENTRY_SIZE);
		/*
		 * Make sure someone else hasn't allocated it for us ...
		 */
		if (!mm->segments) {
			int i = current->tarray_ptr - &task[0];
			mm->segments = ldt;
			set_ldt_desc(i, ldt, LDT_ENTRIES);
			current->tss.ldt = _LDT(i);
			load_ldt(i);
			if (atomic_read(&mm->count) > 1)
				printk(KERN_WARNING
					"LDT allocated for cloned task!\n");
		} else {
			vfree(ldt);
		}
	}

	lp = (__u32 *) ((ldt_info.entry_number << 3) + (char *) mm->segments);

   	/* Allow LDTs to be cleared by the user. */
   	if (ldt_info.base_addr == 0 && ldt_info.limit == 0) {
		if (oldmode ||
		    (ldt_info.contents == 0		&&
		     ldt_info.read_exec_only == 1	&&
		     ldt_info.seg_32bit == 0		&&
		     ldt_info.limit_in_pages == 0	&&
		     ldt_info.seg_not_present == 1	&&
		     ldt_info.useable == 0 )) {
			entry_1 = 0;
			entry_2 = 0;
			goto install;
		}
	}

	entry_1 = ((ldt_info.base_addr & 0x0000ffff) << 16) |
		  (ldt_info.limit & 0x0ffff);
	entry_2 = (ldt_info.base_addr & 0xff000000) |
		  ((ldt_info.base_addr & 0x00ff0000) >> 16) |
		  (ldt_info.limit & 0xf0000) |
		  ((ldt_info.read_exec_only ^ 1) << 9) |
		  (ldt_info.contents << 10) |
		  ((ldt_info.seg_not_present ^ 1) << 15) |
		  (ldt_info.seg_32bit << 22) |
		  (ldt_info.limit_in_pages << 23) |
		  0x7000;
	if (!oldmode)
		entry_2 |= (ldt_info.useable << 20);

	/* Install the new entry ...  */
install:
	*lp	= entry_1;
	*(lp+1)	= entry_2;
	error = 0;
out:
	return error;
}

asmlinkage int sys_modify_ldt(int func, void *ptr, unsigned long bytecount)
{
	int ret = -ENOSYS;

	lock_kernel();
	switch (func) {
	case 0:
		ret = read_ldt(ptr, bytecount);
		break;
	case 1:
		ret = write_ldt(ptr, bytecount, 1);
		break;
	case 0x11:
		ret = write_ldt(ptr, bytecount, 0);
		break;
	}
	unlock_kernel();
	return ret;
}
