/*
 *	linux/mm/mlock.c
 *
 *  (C) Copyright 1995 Linus Torvalds
 */
#include <linux/stat.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/shm.h>
#include <linux/errno.h>
#include <linux/mman.h>
#include <linux/string.h>
#include <linux/malloc.h>

#include <asm/segment.h>
#include <asm/system.h>
#include <asm/pgtable.h>

static inline int mlock_fixup_all(struct vm_area_struct * vma, int newflags)
{
	vma->vm_flags = newflags;
	return 0;
}

static inline int mlock_fixup_start(struct vm_area_struct * vma,
	unsigned long end, int newflags)
{
	struct vm_area_struct * n;

	n = (struct vm_area_struct *) kmalloc(sizeof(struct vm_area_struct), GFP_KERNEL);
	if (!n)
		return -EAGAIN;
	*n = *vma;
	vma->vm_start = end;
	n->vm_end = end;
	vma->vm_offset += vma->vm_start - n->vm_start;
	n->vm_flags = newflags;
	if (n->vm_inode)
		n->vm_inode->i_count++;
	if (n->vm_ops && n->vm_ops->open)
		n->vm_ops->open(n);
	insert_vm_struct(current, n);
	return 0;
}

static inline int mlock_fixup_end(struct vm_area_struct * vma,
	unsigned long start, int newflags)
{
	struct vm_area_struct * n;

	n = (struct vm_area_struct *) kmalloc(sizeof(struct vm_area_struct), GFP_KERNEL);
	if (!n)
		return -EAGAIN;
	*n = *vma;
	vma->vm_end = start;
	n->vm_start = start;
	n->vm_offset += n->vm_start - vma->vm_start;
	n->vm_flags = newflags;
	if (n->vm_inode)
		n->vm_inode->i_count++;
	if (n->vm_ops && n->vm_ops->open)
		n->vm_ops->open(n);
	insert_vm_struct(current, n);
	return 0;
}

static inline int mlock_fixup_middle(struct vm_area_struct * vma,
	unsigned long start, unsigned long end, int newflags)
{
	struct vm_area_struct * left, * right;

	left = (struct vm_area_struct *) kmalloc(sizeof(struct vm_area_struct), GFP_KERNEL);
	if (!left)
		return -EAGAIN;
	right = (struct vm_area_struct *) kmalloc(sizeof(struct vm_area_struct), GFP_KERNEL);
	if (!right) {
		kfree(left);
		return -EAGAIN;
	}
	*left = *vma;
	*right = *vma;
	left->vm_end = start;
	vma->vm_start = start;
	vma->vm_end = end;
	right->vm_start = end;
	vma->vm_offset += vma->vm_start - left->vm_start;
	right->vm_offset += right->vm_start - left->vm_start;
	vma->vm_flags = newflags;
	if (vma->vm_inode)
		vma->vm_inode->i_count += 2;
	if (vma->vm_ops && vma->vm_ops->open) {
		vma->vm_ops->open(left);
		vma->vm_ops->open(right);
	}
	insert_vm_struct(current, left);
	insert_vm_struct(current, right);
	return 0;
}

static int mlock_fixup(struct vm_area_struct * vma, 
	unsigned long start, unsigned long end, unsigned int newflags)
{
	int pages, retval;

	if (newflags == vma->vm_flags)
		return 0;

	if (start == vma->vm_start) {
		if (end == vma->vm_end)
			retval = mlock_fixup_all(vma, newflags);
		else
			retval = mlock_fixup_start(vma, end, newflags);
	} else {
		if (end == vma->vm_end)
			retval = mlock_fixup_end(vma, start, newflags);
		else
			retval = mlock_fixup_middle(vma, start, end, newflags);
	}
	if (!retval) {
		/* keep track of amount of locked VM */
		pages = (end - start) >> PAGE_SHIFT;
		if (!(newflags & VM_LOCKED))
			pages = -pages;
		vma->vm_mm->locked_vm += pages;

		if (newflags & VM_LOCKED)
			while (start < end) {
				char c = get_user((char *) start);
				__asm__ __volatile__("": :"r" (c));
				start += PAGE_SIZE;
			}
	}
	return retval;
}

static int do_mlock(unsigned long start, size_t len, int on)
{
	unsigned long nstart, end, tmp;
	struct vm_area_struct * vma, * next;
	int error;

	if (!suser())
		return -EPERM;
	len = (len + ~PAGE_MASK) & PAGE_MASK;
	end = start + len;
	if (end < start)
		return -EINVAL;
	if (end == start)
		return 0;
	vma = find_vma(current, start);
	if (!vma || vma->vm_start > start)
		return -ENOMEM;

	for (nstart = start ; ; ) {
		unsigned int newflags;

		/* Here we know that  vma->vm_start <= nstart < vma->vm_end. */

		newflags = vma->vm_flags | VM_LOCKED;
		if (!on)
			newflags &= ~VM_LOCKED;

		if (vma->vm_end >= end) {
			error = mlock_fixup(vma, nstart, end, newflags);
			break;
		}

		tmp = vma->vm_end;
		next = vma->vm_next;
		error = mlock_fixup(vma, nstart, tmp, newflags);
		if (error)
			break;
		nstart = tmp;
		vma = next;
		if (!vma || vma->vm_start != nstart) {
			error = -ENOMEM;
			break;
		}
	}
	merge_segments(current, start, end);
	return error;
}

asmlinkage int sys_mlock(unsigned long start, size_t len)
{
	unsigned long locked;
	unsigned long lock_limit;

	len = (len + (start & ~PAGE_MASK) + ~PAGE_MASK) & PAGE_MASK;
	start &= PAGE_MASK;

	locked = len >> PAGE_SHIFT;
	locked += current->mm->locked_vm;

	lock_limit = current->rlim[RLIMIT_MEMLOCK].rlim_cur;
	lock_limit >>= PAGE_SHIFT;

	/* check against resource limits */
	if (locked > lock_limit)
		return -ENOMEM;

	/* we may lock at most half of physical memory... */
	/* (this check is pretty bogus, but doesn't hurt) */
	if (locked > MAP_NR(high_memory)/2)
		return -ENOMEM;

	return do_mlock(start, len, 1);
}

asmlinkage int sys_munlock(unsigned long start, size_t len)
{
	len = (len + (start & ~PAGE_MASK) + ~PAGE_MASK) & PAGE_MASK;
	start &= PAGE_MASK;
	return do_mlock(start, len, 0);
}

static int do_mlockall(int flags)
{
	int error;
	unsigned int def_flags;
	struct vm_area_struct * vma;

	if (!suser())
		return -EPERM;

	def_flags = 0;
	if (flags & MCL_FUTURE)
		def_flags = VM_LOCKED;
	current->mm->def_flags = def_flags;

	error = 0;
	for (vma = current->mm->mmap; vma ; vma = vma->vm_next) {
		unsigned int newflags;

		newflags = vma->vm_flags | VM_LOCKED;
		if (!(flags & MCL_CURRENT))
			newflags &= ~VM_LOCKED;
		error = mlock_fixup(vma, vma->vm_start, vma->vm_end, newflags);
		if (error)
			break;
	}
	merge_segments(current, 0, TASK_SIZE);
	return error;
}

asmlinkage int sys_mlockall(int flags)
{
	unsigned long lock_limit;

	if (!flags || (flags & ~(MCL_CURRENT | MCL_FUTURE)))
		return -EINVAL;

	lock_limit = current->rlim[RLIMIT_MEMLOCK].rlim_cur;
	lock_limit >>= PAGE_SHIFT;

	if (current->mm->total_vm > lock_limit)
		return -ENOMEM;

	/* we may lock at most half of physical memory... */
	/* (this check is pretty bogus, but doesn't hurt) */
	if (current->mm->total_vm > MAP_NR(high_memory)/2)
		return -ENOMEM;

	return do_mlockall(flags);
}

asmlinkage int sys_munlockall(void)
{
	return do_mlockall(0);
}
