/*
 *	linux/mm/mlock.c
 *
 *  (C) Copyright 1995 Linus Torvalds
 */
#include <linux/slab.h>
#include <linux/shm.h>
#include <linux/mman.h>
#include <linux/smp_lock.h>

#include <asm/uaccess.h>
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

	n = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
	if (!n)
		return -EAGAIN;
	*n = *vma;
	vma->vm_start = end;
	n->vm_end = end;
	vma->vm_offset += vma->vm_start - n->vm_start;
	n->vm_flags = newflags;
	if (n->vm_file)
		n->vm_file->f_count++;
	if (n->vm_ops && n->vm_ops->open)
		n->vm_ops->open(n);
	insert_vm_struct(current->mm, n);
	return 0;
}

static inline int mlock_fixup_end(struct vm_area_struct * vma,
	unsigned long start, int newflags)
{
	struct vm_area_struct * n;

	n = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
	if (!n)
		return -EAGAIN;
	*n = *vma;
	vma->vm_end = start;
	n->vm_start = start;
	n->vm_offset += n->vm_start - vma->vm_start;
	n->vm_flags = newflags;
	if (n->vm_file)
		n->vm_file->f_count++;
	if (n->vm_ops && n->vm_ops->open)
		n->vm_ops->open(n);
	insert_vm_struct(current->mm, n);
	return 0;
}

static inline int mlock_fixup_middle(struct vm_area_struct * vma,
	unsigned long start, unsigned long end, int newflags)
{
	struct vm_area_struct * left, * right;

	left = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
	if (!left)
		return -EAGAIN;
	right = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
	if (!right) {
		kmem_cache_free(vm_area_cachep, left);
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
	if (vma->vm_file)
		vma->vm_file->f_count += 2;

	if (vma->vm_ops && vma->vm_ops->open) {
		vma->vm_ops->open(left);
		vma->vm_ops->open(right);
	}
	insert_vm_struct(current->mm, left);
	insert_vm_struct(current->mm, right);
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
		make_pages_present(start, end);
	}
	return retval;
}

static int do_mlock(unsigned long start, size_t len, int on)
{
	unsigned long nstart, end, tmp;
	struct vm_area_struct * vma, * next;
	int error;

	if (!capable(CAP_IPC_LOCK))
		return -EPERM;
	len = (len + ~PAGE_MASK) & PAGE_MASK;
	end = start + len;
	if (end < start)
		return -EINVAL;
	if (end == start)
		return 0;
	vma = find_vma(current->mm, start);
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
	merge_segments(current->mm, start, end);
	return error;
}

asmlinkage int sys_mlock(unsigned long start, size_t len)
{
	unsigned long locked;
	unsigned long lock_limit;
	int error = -ENOMEM;

	down(&current->mm->mmap_sem);
	lock_kernel();
	len = (len + (start & ~PAGE_MASK) + ~PAGE_MASK) & PAGE_MASK;
	start &= PAGE_MASK;

	locked = len >> PAGE_SHIFT;
	locked += current->mm->locked_vm;

	lock_limit = current->rlim[RLIMIT_MEMLOCK].rlim_cur;
	lock_limit >>= PAGE_SHIFT;

	/* check against resource limits */
	if (locked > lock_limit)
		goto out;

	/* we may lock at most half of physical memory... */
	/* (this check is pretty bogus, but doesn't hurt) */
	if (locked > num_physpages/2)
		goto out;

	error = do_mlock(start, len, 1);
out:
	unlock_kernel();
	up(&current->mm->mmap_sem);
	return error;
}

asmlinkage int sys_munlock(unsigned long start, size_t len)
{
	int ret;

	down(&current->mm->mmap_sem);
	lock_kernel();
	len = (len + (start & ~PAGE_MASK) + ~PAGE_MASK) & PAGE_MASK;
	start &= PAGE_MASK;
	ret = do_mlock(start, len, 0);
	unlock_kernel();
	up(&current->mm->mmap_sem);
	return ret;
}

static int do_mlockall(int flags)
{
	int error;
	unsigned int def_flags;
	struct vm_area_struct * vma;

	if (!capable(CAP_IPC_LOCK))
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
	merge_segments(current->mm, 0, TASK_SIZE);
	return error;
}

asmlinkage int sys_mlockall(int flags)
{
	unsigned long lock_limit;
	int ret = -EINVAL;

	down(&current->mm->mmap_sem);
	lock_kernel();
	if (!flags || (flags & ~(MCL_CURRENT | MCL_FUTURE)))
		goto out;

	lock_limit = current->rlim[RLIMIT_MEMLOCK].rlim_cur;
	lock_limit >>= PAGE_SHIFT;

	ret = -ENOMEM;
	if (current->mm->total_vm > lock_limit)
		goto out;

	/* we may lock at most half of physical memory... */
	/* (this check is pretty bogus, but doesn't hurt) */
	if (current->mm->total_vm > num_physpages/2)
		goto out;

	ret = do_mlockall(flags);
out:
	unlock_kernel();
	up(&current->mm->mmap_sem);
	return ret;
}

asmlinkage int sys_munlockall(void)
{
	int ret;

	down(&current->mm->mmap_sem);
	lock_kernel();
	ret = do_mlockall(0);
	unlock_kernel();
	up(&current->mm->mmap_sem);
	return ret;
}
