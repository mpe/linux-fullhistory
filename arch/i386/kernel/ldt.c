/*
 * linux/kernel/ldt.c
 *
 * Copyright (C) 1992 Krishna Balasubramanian and Linus Torvalds
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <linux/ldt.h>

static int read_ldt(void * ptr, unsigned long bytecount)
{
	int error;
	void * address = current->ldt;
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
	error = verify_area(VERIFY_WRITE, ptr, size);
	if (error)
		return error;
	memcpy_tofs(ptr, address, size);
	return size;
}

static inline int limits_ok(struct modify_ldt_ldt_s *ldt_info)
{
	unsigned long base, limit;
	/* linear address of first and last accessible byte */
	unsigned long first, last;

	base = ldt_info->base_addr;
	limit = ldt_info->limit;
	if (ldt_info->limit_in_pages)
		limit = limit * PAGE_SIZE + PAGE_SIZE - 1;

	first = base;
	last = limit + base;

	/* segment grows down? */
	if (ldt_info->contents == 1) {
		/* data segment grows down */
		first = base+limit+1;
		last = base+65535;
		if (ldt_info->seg_32bit)
			last = base-1;
	}
	return (last >= first && last < TASK_SIZE);
}

static int write_ldt(void * ptr, unsigned long bytecount)
{
	struct modify_ldt_ldt_s ldt_info;
	unsigned long *lp;
	int error, i;

	if (bytecount != sizeof(ldt_info))
		return -EINVAL;
	error = verify_area(VERIFY_READ, ptr, sizeof(ldt_info));
	if (error)
		return error;

	memcpy_fromfs(&ldt_info, ptr, sizeof(ldt_info));

	if (ldt_info.contents == 3 || ldt_info.entry_number >= LDT_ENTRIES)
		return -EINVAL;

	if (!limits_ok(&ldt_info))
		return -EINVAL;

	if (!current->ldt) {
		for (i=1 ; i<NR_TASKS ; i++) {
			if (task[i] == current) {
				if (!(current->ldt = (struct desc_struct*) vmalloc(LDT_ENTRIES*LDT_ENTRY_SIZE)))
					return -ENOMEM;
				memset(current->ldt, 0, LDT_ENTRIES*LDT_ENTRY_SIZE);
				set_ldt_desc(gdt+(i<<1)+FIRST_LDT_ENTRY, current->ldt, LDT_ENTRIES);
				load_ldt(i);
			}
		}
	}
	
	lp = (unsigned long *) &current->ldt[ldt_info.entry_number];
   	/* Allow LDTs to be cleared by the user. */
   	if (ldt_info.base_addr == 0 && ldt_info.limit == 0) {
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
	return 0;
}

asmlinkage int sys_modify_ldt(int func, void *ptr, unsigned long bytecount)
{
	if (func == 0)
		return read_ldt(ptr, bytecount);
	if (func == 1)
		return write_ldt(ptr, bytecount);
	return -ENOSYS;
}
