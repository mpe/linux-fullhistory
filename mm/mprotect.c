/*
 *  mm/mprotect.c
 *
 *  (C) Copyright 1994 Linus Torvalds
 *  (C) Copyright 2002 Christoph Hellwig
 *
 *  Address space accounting code	<alan@redhat.com>
 *  (C) Copyright 2002 Red Hat Inc, All Rights Reserved
 */

#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/slab.h>
#include <linux/shm.h>
#include <linux/mman.h>
#include <linux/fs.h>
#include <linux/highmem.h>
#include <linux/security.h>

#include <asm/uaccess.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

static inline void
change_pte_range(pmd_t *pmd, unsigned long address,
		unsigned long size, pgprot_t newprot)
{
	pte_t * pte;
	unsigned long end;

	if (pmd_none(*pmd))
		return;
	if (pmd_bad(*pmd)) {
		pmd_ERROR(*pmd);
		pmd_clear(pmd);
		return;
	}
	pte = pte_offset_map(pmd, address);
	address &= ~PMD_MASK;
	end = address + size;
	if (end > PMD_SIZE)
		end = PMD_SIZE;
	do {
		if (pte_present(*pte)) {
			pte_t entry;

			/* Avoid an SMP race with hardware updated dirty/clean
			 * bits by wiping the pte and then setting the new pte
			 * into place.
			 */
			entry = ptep_get_and_clear(pte);
			set_pte(pte, pte_modify(entry, newprot));
		}
		address += PAGE_SIZE;
		pte++;
	} while (address && (address < end));
	pte_unmap(pte - 1);
}

static inline void
change_pmd_range(pgd_t *pgd, unsigned long address,
		unsigned long size, pgprot_t newprot)
{
	pmd_t * pmd;
	unsigned long end;

	if (pgd_none(*pgd))
		return;
	if (pgd_bad(*pgd)) {
		pgd_ERROR(*pgd);
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
	} while (address && (address < end));
}

static void
change_protection(struct vm_area_struct *vma, unsigned long start,
		unsigned long end, pgprot_t newprot)
{
	pgd_t *dir;
	unsigned long beg = start;

	dir = pgd_offset(current->mm, start);
	flush_cache_range(vma, beg, end);
	if (start >= end)
		BUG();
	spin_lock(&current->mm->page_table_lock);
	do {
		change_pmd_range(dir, start, end - start, newprot);
		start = (start + PGDIR_SIZE) & PGDIR_MASK;
		dir++;
	} while (start && (start < end));
	flush_tlb_range(vma, beg, end);
	spin_unlock(&current->mm->page_table_lock);
	return;
}
/*
 * Try to merge a vma with the previous flag, return 1 if successful or 0 if it
 * was impossible.
 */
static int
mprotect_attempt_merge(struct vm_area_struct *vma, struct vm_area_struct *prev,
		unsigned long end, int newflags)
{
	struct mm_struct * mm = vma->vm_mm;

	if (!prev || !vma)
		return 0;
	if (prev->vm_end != vma->vm_start)
		return 0;
	if (!can_vma_merge(prev, newflags))
		return 0;
	if (vma->vm_file || (vma->vm_flags & VM_SHARED))
		return 0;

	/*
	 * If the whole area changes to the protection of the previous one
	 * we can just get rid of it.
	 */
	if (end == vma->vm_end) {
		spin_lock(&mm->page_table_lock);
		prev->vm_end = end;
		__vma_unlink(mm, vma, prev);
		spin_unlock(&mm->page_table_lock);

		kmem_cache_free(vm_area_cachep, vma);
		mm->map_count--;
		return 1;
	} 

	/*
	 * Otherwise extend it.
	 */
	spin_lock(&mm->page_table_lock);
	prev->vm_end = end;
	vma->vm_start = end;
	spin_unlock(&mm->page_table_lock);
	return 1;
}

static int
mprotect_fixup(struct vm_area_struct *vma, struct vm_area_struct **pprev,
	unsigned long start, unsigned long end, unsigned int newflags)
{
	struct mm_struct * mm = vma->vm_mm;
	unsigned long charged = 0;
	pgprot_t newprot;
	int error;

	if (newflags == vma->vm_flags) {
		*pprev = vma;
		return 0;
	}

	/*
	 * If we make a private mapping writable we increase our commit;
	 * but (without finer accounting) cannot reduce our commit if we
	 * make it unwritable again.
	 *
	 * FIXME? We haven't defined a VM_NORESERVE flag, so mprotecting
	 * a MAP_NORESERVE private mapping to writable will now reserve.
	 */
	if (newflags & VM_WRITE) {
		if (!(vma->vm_flags & (VM_ACCOUNT|VM_WRITE|VM_SHARED))) {
			charged = (end - start) >> PAGE_SHIFT;
			if (security_vm_enough_memory(charged))
				return -ENOMEM;
			newflags |= VM_ACCOUNT;
		}
	}

	newprot = protection_map[newflags & 0xf];

	if (start == vma->vm_start) {
		/*
		 * Try to merge with the previous vma.
		 */
		if (mprotect_attempt_merge(vma, *pprev, end, newflags)) {
			vma = *pprev;
			goto success;
		}
	} else {
		error = split_vma(mm, vma, start, 1);
		if (error)
			goto fail;
	}
	/*
	 * Unless it returns an error, this function always sets *pprev to
	 * the first vma for which vma->vm_end >= end.
	 */
	*pprev = vma;

	if (end != vma->vm_end) {
		error = split_vma(mm, vma, end, 0);
		if (error)
			goto fail;
	}

	spin_lock(&mm->page_table_lock);
	vma->vm_flags = newflags;
	vma->vm_page_prot = newprot;
	spin_unlock(&mm->page_table_lock);
success:
	change_protection(vma, start, end, newprot);
	return 0;

fail:
	vm_unacct_memory(charged);
	return error;
}

asmlinkage long
sys_mprotect(unsigned long start, size_t len, unsigned long prot)
{
	unsigned long vm_flags, nstart, end, tmp;
	struct vm_area_struct * vma, * next, * prev;
	int error = -EINVAL;
	const int grows = prot & (PROT_GROWSDOWN|PROT_GROWSUP);
	prot &= ~(PROT_GROWSDOWN|PROT_GROWSUP);
	if (grows == (PROT_GROWSDOWN|PROT_GROWSUP)) /* can't be both */
		return -EINVAL;

	if (start & ~PAGE_MASK)
		return -EINVAL;
	len = PAGE_ALIGN(len);
	end = start + len;
	if (end < start)
		return -EINVAL;
	if (prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC | PROT_SEM))
		return -EINVAL;
	if (end == start)
		return 0;

	vm_flags = calc_vm_prot_bits(prot);

	down_write(&current->mm->mmap_sem);

	vma = find_vma_prev(current->mm, start, &prev);
	error = -ENOMEM;
	if (!vma)
		goto out;
	if (unlikely(grows & PROT_GROWSDOWN)) {
		if (vma->vm_start >= end)
			goto out;
		start = vma->vm_start;
		error = -EINVAL;
		if (!(vma->vm_flags & VM_GROWSDOWN))
			goto out;
	}
	else {
		if (vma->vm_start > start)
			goto out;
		if (unlikely(grows & PROT_GROWSUP)) {
			end = vma->vm_end;
			error = -EINVAL;
			if (!(vma->vm_flags & VM_GROWSUP))
				goto out;
		}
	}

	for (nstart = start ; ; ) {
		unsigned int newflags;
		int last = 0;

		/* Here we know that  vma->vm_start <= nstart < vma->vm_end. */

		if (is_vm_hugetlb_page(vma)) {
			error = -EACCES;
			goto out;
		}

		newflags = vm_flags | (vma->vm_flags & ~(VM_READ | VM_WRITE | VM_EXEC));

		if ((newflags & ~(newflags >> 4)) & 0xf) {
			error = -EACCES;
			goto out;
		}

		error = security_file_mprotect(vma, prot);
		if (error)
			goto out;

		if (vma->vm_end > end) {
			error = mprotect_fixup(vma, &prev, nstart, end, newflags);
			goto out;
		}
		if (vma->vm_end == end)
			last = 1;

		tmp = vma->vm_end;
		next = vma->vm_next;
		error = mprotect_fixup(vma, &prev, nstart, tmp, newflags);
		if (error)
			goto out;
		if (last)
			break;
		nstart = tmp;
		vma = next;
		if (!vma || vma->vm_start != nstart) {
			error = -ENOMEM;
			goto out;
		}
	}

	if (next && prev->vm_end == next->vm_start &&
			can_vma_merge(next, prev->vm_flags) &&
			!prev->vm_file && !(prev->vm_flags & VM_SHARED)) {
		spin_lock(&prev->vm_mm->page_table_lock);
		prev->vm_end = next->vm_end;
		__vma_unlink(prev->vm_mm, next, prev);
		spin_unlock(&prev->vm_mm->page_table_lock);

		kmem_cache_free(vm_area_cachep, next);
		prev->vm_mm->map_count--;
	}
out:
	up_write(&current->mm->mmap_sem);
	return error;
}
