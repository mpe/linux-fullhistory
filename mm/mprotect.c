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
#include <asm/pgtable.h>

static inline void change_pte_range(pmd_t * pmd, unsigned long address,
	unsigned long size, pgprot_t newprot)
{
	pte_t * pte;
	unsigned long end;

	if (pmd_none(*pmd))
		return;
	if (pmd_bad(*pmd)) {
		printk("change_pte_range: bad pmd (%08lx)\n", pmd_val(*pmd));
		pmd_clear(pmd);
		return;
	}
	pte = pte_offset(pmd, address);
	address &= ~PMD_MASK;
	end = address + size;
	if (end > PMD_SIZE)
		end = PMD_SIZE;
	do {
		pte_t entry = *pte;
		if (pte_present(entry))
			*pte = pte_modify(entry, newprot);
		address += PAGE_SIZE;
		pte++;
	} while (address < end);
}

static inline void change_pmd_range(pgd_t * pgd, unsigned long address,
	unsigned long size, pgprot_t newprot)
{
	pmd_t * pmd;
	unsigned long end;

	if (pgd_none(*pgd))
		return;
	if (pgd_bad(*pgd)) {
		printk("change_pmd_range: bad pgd (%08lx)\n", pgd_val(*pgd));
		pgd_clear(pgd);
		return;
	}
	pmd = pmd_offset(pgd, address);
	address &= ~PGDIR_MASK;
	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;
	do {
		change_pte_range(pmd, address, end - address, newprot);
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address < end);
}

static void change_protection(unsigned long start, unsigned long end, pgprot_t newprot)
{
	pgd_t *dir;

	dir = pgd_offset(current, start);
	while (start < end) {
		change_pmd_range(dir, start, end - start, newprot);
		start = (start + PGDIR_SIZE) & PGDIR_MASK;
		dir++;
	}
	invalidate();
	return;
}

static inline int mprotect_fixup_all(struct vm_area_struct * vma,
	int newflags, pgprot_t prot)
{
	vma->vm_flags = newflags;
	vma->vm_page_prot = prot;
	return 0;
}

static inline int mprotect_fixup_start(struct vm_area_struct * vma,
	unsigned long end,
	int newflags, pgprot_t prot)
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
	int newflags, pgprot_t prot)
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
	int newflags, pgprot_t prot)
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
	pgprot_t newprot;
	int error;

	if (newflags == vma->vm_flags)
		return 0;
	newprot = protection_map[newflags & 0xf];
	if (start == vma->vm_start)
		if (end == vma->vm_end)
			error = mprotect_fixup_all(vma, newflags, newprot);
		else
			error = mprotect_fixup_start(vma, end, newflags, newprot);
	else if (end == vma->vm_end)
		error = mprotect_fixup_end(vma, start, newflags, newprot);
	else
		error = mprotect_fixup_middle(vma, start, end, newflags, newprot);

	if (error)
		return error;

	change_protection(start, end, newprot);
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
