/*
 *	linux/mm/remap.c
 *
 *	(C) Copyright 1996 Linus Torvalds
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
#include <linux/swap.h>

#include <asm/segment.h>
#include <asm/system.h>
#include <asm/pgtable.h>

static inline pte_t *get_one_pte(struct mm_struct *mm, unsigned long addr)
{
	pgd_t * pgd;
	pmd_t * pmd;
	pte_t * pte = NULL;

	pgd = pgd_offset(mm, addr);
	if (pgd_none(*pgd))
		goto end;
	if (pgd_bad(*pgd)) {
		printk("move_one_page: bad source pgd (%08lx)\n", pgd_val(*pgd));
		pgd_clear(pgd);
		goto end;
	}

	pmd = pmd_offset(pgd, addr);
	if (pmd_none(*pmd))
		goto end;
	if (pmd_bad(*pmd)) {
		printk("move_one_page: bad source pmd (%08lx)\n", pmd_val(*pmd));
		pmd_clear(pmd);
		goto end;
	}

	pte = pte_offset(pmd, addr);
	if (pte_none(*pte))
		pte = NULL;
end:
	return pte;
}

static inline pte_t *alloc_one_pte(struct mm_struct *mm, unsigned long addr)
{
	pmd_t * pmd;
	pte_t * pte = NULL;

	pmd = pmd_alloc(pgd_offset(mm, addr), addr);
	if (pmd)
		pte = pte_alloc(pmd, addr);
	return pte;
}

static inline int copy_one_pte(pte_t * src, pte_t * dst)
{
	int error = 0;
	pte_t pte = *src;

	if (!pte_none(pte)) {
		error++;
		if (dst) {
			pte_clear(src);
			set_pte(dst, pte);
			error--;
		}
	}
	return error;
}

static int move_one_page(struct mm_struct *mm, unsigned long old_addr, unsigned long new_addr)
{
	int error = 0;
	pte_t * src;

	src = get_one_pte(mm, old_addr);
	if (src)
		error = copy_one_pte(src, alloc_one_pte(mm, new_addr));
	return error;
}

static int move_page_tables(struct mm_struct * mm,
	unsigned long new_addr, unsigned long old_addr, unsigned long len)
{
	unsigned long offset = len;

	flush_cache_range(mm, old_addr, old_addr + len);
	flush_tlb_range(mm, old_addr, old_addr + len);

	/*
	 * This is not the clever way to do this, but we're taking the
	 * easy way out on the assumption that most remappings will be
	 * only a few pages.. This also makes error recovery easier.
	 */
	while (offset) {
		offset -= PAGE_SIZE;
		if (move_one_page(mm, old_addr + offset, new_addr + offset))
			goto oops_we_failed;
	}
	return 0;

	/*
	 * Ok, the move failed because we didn't have enough pages for
	 * the new page table tree. This is unlikely, but we have to
	 * take the possibility into account. In that case we just move
	 * all the pages back (this will work, because we still have
	 * the old page tables)
	 */
oops_we_failed:
	flush_cache_range(mm, new_addr, new_addr + len);
	while ((offset += PAGE_SIZE) < len)
		move_one_page(mm, new_addr + offset, old_addr + offset);
	flush_tlb_range(mm, new_addr, new_addr + len);
	zap_page_range(mm, new_addr, new_addr + len);
	return -1;
}

static inline unsigned long move_vma(struct vm_area_struct * vma,
	unsigned long addr, unsigned long old_len, unsigned long new_len)
{
	struct vm_area_struct * new_vma;

	new_vma = (struct vm_area_struct *)
		kmalloc(sizeof(struct vm_area_struct), GFP_KERNEL);
	if (new_vma) {
		unsigned long new_addr = get_unmapped_area(addr, new_len);

		if (new_addr && !move_page_tables(current->mm, new_addr, addr, old_len)) {
			*new_vma = *vma;
			new_vma->vm_start = new_addr;
			new_vma->vm_end = new_addr+new_len;
			new_vma->vm_offset = vma->vm_offset + (addr - vma->vm_start);
			if (new_vma->vm_inode)
				new_vma->vm_inode->i_count++;
			if (new_vma->vm_ops && new_vma->vm_ops->open)
				new_vma->vm_ops->open(new_vma);
			insert_vm_struct(current, new_vma);
			merge_segments(current, new_vma->vm_start, new_vma->vm_end);
			do_munmap(addr, old_len);
			return new_addr;
		}
		kfree(new_vma);
	}
	return -ENOMEM;
}

/*
 * Expand (or shrink) an existing mapping, potentially moving it at the
 * same time (controlled by the MREMAP_MAYMOVE flag and available VM space)
 */
asmlinkage unsigned long sys_mremap(unsigned long addr,
	unsigned long old_len, unsigned long new_len,
	unsigned long flags)
{
	struct vm_area_struct *vma;

	if (addr & ~PAGE_MASK)
		return -EINVAL;
	old_len = PAGE_ALIGN(old_len);
	new_len = PAGE_ALIGN(new_len);

	/*
	 * Always allow a shrinking remap: that just unmaps
	 * the unnecessary pages..
	 */
	if (old_len > new_len) {
		do_munmap(addr+new_len, old_len - new_len);
		return addr;
	}

	/*
	 * Ok, we need to grow..
	 */
	vma = find_vma(current, addr);
	if (!vma || vma->vm_start > addr)
		return -EFAULT;
	/* We can't remap across vm area boundaries */
	if (old_len > vma->vm_end - addr)
		return -EFAULT;
	if (vma->vm_flags & VM_LOCKED) {
		unsigned long locked = current->mm->locked_vm << PAGE_SHIFT;
		locked += new_len - old_len;
		if (locked > current->rlim[RLIMIT_MEMLOCK].rlim_cur)
			return -EAGAIN;
	}

	/* old_len exactly to the end of the area.. */
	if (old_len == vma->vm_end - addr &&
	    (old_len != new_len || !(flags & MREMAP_MAYMOVE))) {
		unsigned long max_addr = TASK_SIZE;
		if (vma->vm_next)
			max_addr = vma->vm_next->vm_start;
		/* can we just expand the current mapping? */
		if (max_addr - addr >= new_len) {
			int pages = (new_len - old_len) >> PAGE_SHIFT;
			vma->vm_end = addr + new_len;
			current->mm->total_vm += pages;
			if (vma->vm_flags & VM_LOCKED)
				current->mm->locked_vm += pages;
			return addr;
		}
	}

	/*
	 * We weren't able to just expand or shrink the area,
	 * we need to create a new one and move it..
	 */
	if (flags & MREMAP_MAYMOVE)
		return move_vma(vma, addr, old_len, new_len);
	return -ENOMEM;
}
