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

#define CHG_MASK (PAGE_MASK | PAGE_ACCESSED | PAGE_DIRTY | PAGE_PWT | PAGE_PCD)

static void change_protection(unsigned long start, unsigned long end, int prot)
{
	unsigned long *page_table, *dir;
	unsigned long page, offset;
	int nr;

	dir = PAGE_DIR_OFFSET(current->tss.cr3, start);
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
				*page_table = (page & CHG_MASK) | prot;
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
	merge_segments(current->mm->mmap);
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
	insert_vm_struct(current, n);
	merge_segments(current->mm->mmap);
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
	insert_vm_struct(current, n);
	merge_segments(current->mm->mmap);
	return 0;
}

static inline int mprotect_fixup_middle(struct vm_area_struct * vma,
	unsigned long start, unsigned long end,
	int newflags, int prot)
{
	int error;
	unsigned long tmpflags, tmpprot;

	tmpflags = vma->vm_flags;
	tmpprot = vma->vm_page_prot;
	vma->vm_flags = newflags;
	vma->vm_page_prot = prot;
	error = mprotect_fixup_end(vma, end, tmpflags, tmpprot);
	if (!error)
		error = mprotect_fixup_start(vma, start, tmpflags, tmpprot);
	return error;
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
	unsigned long end;
	struct vm_area_struct * vma;

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
	for (vma = current->mm->mmap ; ; vma = vma->vm_next) {
		if (!vma)
			return -EFAULT;
		if (vma->vm_end > start)
			break;
	}
	if (vma->vm_start > start)
		return -EFAULT;

	for ( ; ; ) {
		int error;
		unsigned int newflags;

		newflags = prot | (vma->vm_flags & ~(PROT_READ | PROT_WRITE | PROT_EXEC));
		if ((newflags & ~(newflags >> 4)) & 0xf)
			return -EACCES;

		if (vma->vm_end >= end)
			return mprotect_fixup(vma, start, end, newflags);

		error = mprotect_fixup(vma, start, vma->vm_end, newflags);
		if (error)
			return error;
		start = vma->vm_end;
		vma = vma->vm_next;
		if (!vma || vma->vm_start != start)
			return -EFAULT;
	}
}
