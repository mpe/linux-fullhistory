/*
 *	linux/mm/mprotect.c
 *
 *  (C) Copyright 1994 Linus Torvalds
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

static void change_protection(unsigned long start, unsigned long end, int prot)
{
	unsigned long *page_table, *dir;
	unsigned long page, offset;
	int nr;

	dir = PAGE_DIR_OFFSET(current, start);
	offset = (start >> PAGE_SHIFT) & (PTRS_PER_PAGE-1);
	nr = (end - start) >> PAGE_SHIFT;
	while (nr > 0) {
		page = *dir;
		dir++;
		if (!(page & PAGE_PRESENT)) {
			nr = nr - PTRS_PER_PAGE + offset;
			offset = 0;
			continue;
		}
		page_table = offset + (unsigned long *) (page & PAGE_MASK);
		offset = PTRS_PER_PAGE - offset;
		if (offset > nr)
			offset = nr;
		nr = nr - offset;
		do {
			page = *page_table;
			if (page & PAGE_PRESENT)
				*page_table = (page & PAGE_CHG_MASK) | prot;
			++page_table;
		} while (--offset);
	}
	return;
}

static inline int mprotect_fixup_all(struct vm_area_struct * vma,
	int newflags, int prot)
{
	vma->vm_flags = newflags;
	vma->vm_page_prot = prot;
	return 0;
}

static inline int mprotect_fixup_start(struct vm_area_struct * vma,
	unsigned long end,
	int newflags, int prot)
{
	struct vm_area_struct * n;

	n = (struct vm_area_struct *) kmalloc(sizeof(struct vm_area_struct), GFP_KERNEL);
	if (!n)
		return -ENOMEM;
	*n = *vma;
	vma->vm_start = end;
	n->vm_end = end;
	vma->vm_offset += vma->vm_start - n->vm_start;
	n->vm_flags = newflags;
	n->vm_page_prot = prot;
	if (n->vm_inode)
		n->vm_inode->i_count++;
	if (n->vm_ops && n->vm_ops->open)
		n->vm_ops->open(n);
	insert_vm_struct(current, n);
	return 0;
}

static inline int mprotect_fixup_end(struct vm_area_struct * vma,
	unsigned long start,
	int newflags, int prot)
{
	struct vm_area_struct * n;

	n = (struct vm_area_struct *) kmalloc(sizeof(struct vm_area_struct), GFP_KERNEL);
	if (!n)
		return -ENOMEM;
	*n = *vma;
	vma->vm_end = start;
	n->vm_start = start;
	n->vm_offset += n->vm_start - vma->vm_start;
	n->vm_flags = newflags;
	n->vm_page_prot = prot;
	if (n->vm_inode)
		n->vm_inode->i_count++;
	if (n->vm_ops && n->vm_ops->open)
		n->vm_ops->open(n);
	insert_vm_struct(current, n);
	return 0;
}

static inline int mprotect_fixup_middle(struct vm_area_struct * vma,
	unsigned long start, unsigned long end,
	int newflags, int prot)
{
	struct vm_area_struct * left, * right;

	left = (struct vm_area_struct *) kmalloc(sizeof(struct vm_area_struct), GFP_KERNEL);
	if (!left)
		return -ENOMEM;
	right = (struct vm_area_struct *) kmalloc(sizeof(struct vm_area_struct), GFP_KERNEL);
	if (!right) {
		kfree(left);
		return -ENOMEM;
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
	vma->vm_page_prot = prot;
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

static int mprotect_fixup(struct vm_area_struct * vma, 
	unsigned long start, unsigned long end, unsigned int newflags)
{
	int prot, error;

	if (newflags == vma->vm_flags)
		return 0;
	prot = PAGE_PRESENT;
	if (newflags & (VM_READ | VM_EXEC))
		prot |= PAGE_READONLY;
	if (newflags & VM_WRITE)
		if (newflags & VM_SHARED)
			prot |= PAGE_SHARED;
		else
			prot |= PAGE_COPY;

	if (start == vma->vm_start)
		if (end == vma->vm_end)
			error = mprotect_fixup_all(vma, newflags, prot);
		else
			error = mprotect_fixup_start(vma, end, newflags, prot);
	else if (end == vma->vm_end)
		error = mprotect_fixup_end(vma, start, newflags, prot);
	else
		error = mprotect_fixup_middle(vma, start, end, newflags, prot);

	if (error)
		return error;

	change_protection(start, end, prot);
	return 0;
}

asmlinkage int sys_mprotect(unsigned long start, size_t len, unsigned long prot)
{
	unsigned long nstart, end, tmp;
	struct vm_area_struct * vma, * next;
	int error;

	if (start & ~PAGE_MASK)
		return -EINVAL;
	len = (len + ~PAGE_MASK) & PAGE_MASK;
	end = start + len;
	if (end < start)
		return -EINVAL;
	if (prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC))
		return -EINVAL;
	if (end == start)
		return 0;
	vma = find_vma(current, start);
	if (!vma || vma->vm_start > start)
		return -EFAULT;

	for (nstart = start ; ; ) {
		unsigned int newflags;

		/* Here we know that  vma->vm_start <= nstart < vma->vm_end. */

		newflags = prot | (vma->vm_flags & ~(PROT_READ | PROT_WRITE | PROT_EXEC));
		if ((newflags & ~(newflags >> 4)) & 0xf) {
			error = -EACCES;
			break;
		}

		if (vma->vm_end >= end) {
			error = mprotect_fixup(vma, nstart, end, newflags);
			break;
		}

		tmp = vma->vm_end;
		next = vma->vm_next;
		error = mprotect_fixup(vma, nstart, tmp, newflags);
		if (error)
			break;
		nstart = tmp;
		vma = next;
		if (!vma || vma->vm_start != nstart) {
			error = -EFAULT;
			break;
		}
	}
	merge_segments(current, start, end);
	return error;
}
