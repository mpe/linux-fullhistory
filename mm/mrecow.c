/*
 *	linux/mm/recow.c
 *
 * Copyright (C) 1998 Linus Torvalds
 */

#include <linux/mm.h>
#include <linux/smp_lock.h>

#include <asm/pgtable.h>

/*
 * This implements the "re-COW" system call.
 *
 * "re-cow" - synchronize two page-aligned areas with each other,
 * returning the first offset at which they differ. This is useful
 * for doing large memory compares. One example is simulating a
 * VGA device under DOSEMU, where you cache the old state of the
 * screen, and let DOSEMU have a private copy that it can change.
 *
 * When you want to synchronize the real screen with the private
 * simulation, you "re-cow()" the areas and see where the changes
 * have been. It will not only tell you what pages have been modified,
 * it will also share pages if they are the same..
 *
 * Doing this in user mode is prohibitive, as we need to access
 * the page tables in order to do this efficiently.
 */

#define verify_recow(mm, vma, addr, len) do { \
	vma = find_vma(mm, addr); \
	if (!vma || (vma->vm_flags & VM_SHARED) || addr < vma->vm_start || vma->vm_end - addr < len) \
		goto badvma; \
} while (0)

static int do_compare(struct mm_struct *mm, pte_t * firstpte, unsigned long addr)
{
	pgd_t * pgd;

	pgd = pgd_offset(mm, addr);
	if (!pgd_none(*pgd)) {
		pmd_t * pmd = pmd_offset(pgd, addr);
		if (!pmd_none(*pmd)) {
			pte_t * pte = pte_offset(pmd, addr);
			pte_t entry = *pte;
			if (pte_present(entry)) {
				unsigned long page1 = pte_page(*firstpte);
				unsigned long page2 = pte_page(entry);

				if (page1 == page2)
					return 1;

				if (!memcmp((void *) page1, (void *) page2, PAGE_SIZE)) {
					entry = pte_wrprotect(entry);
					*firstpte = entry;
					*pte = entry;
					atomic_inc(&mem_map[MAP_NR(page2)].count);
					free_page(page1);
					return 2;
				}
			}
		}
	}
	return 0;
}

/*
 * NOTE! We return zero for all "special" cases, 
 * so that if something is not present for any
 * reason the user has to compare that by hand.
 */
static int same_page(struct mm_struct *mm, unsigned long addr1, unsigned long addr2)
{
	pgd_t * pgd;

	pgd = pgd_offset(mm, addr1);
	if (!pgd_none(*pgd)) {
		pmd_t * pmd = pmd_offset(pgd, addr1);
		if (!pmd_none(*pmd)) {
			pte_t * pte = pte_offset(pmd, addr1);
			if (pte_present(*pte))
				return do_compare(mm, pte, addr2);
		}
	}
	return 0;
}

asmlinkage long sys_mrecow(unsigned long addr1, unsigned long addr2, size_t len)
{
	int shared = 0;
	long retval;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma1, *vma2;

	/*
	 * Everything has to be page-aligned..
	 */
	if ((addr1 | addr2 | len) & ~PAGE_MASK)
		return -EINVAL;

	/* Make sure we're not aliased (trivially the same for the whole length) */
	if (addr1 == addr2)
		return len;

	down(&mm->mmap_sem);

	/*
	 * We require that the comparison areas are fully contained
	 * within the vm_area_structs, and that they are both private
	 * mappings..
	 */
	retval = -EFAULT;
	verify_recow(mm, vma1, addr1, len);
	verify_recow(mm, vma2, addr2, len);

	/*
	 * We need to have the same page protections on both
	 * areas, otherwise we'll mess up when we combine pages.
	 *
	 * Right now we also only allow this on anonymous areas,
	 * I don't know if we'd want to expand it to other things
	 * at some point.
	 */
	retval = -EINVAL;
	if (pgprot_val(vma1->vm_page_prot) != pgprot_val(vma2->vm_page_prot))
		goto badvma;
	if (vma1->vm_file || vma2->vm_file)
		goto badvma;

	/*
	 * Ok, start the page walk..
	 */
	lock_kernel();
	retval = 0;
	while (len) {
		int same = same_page(mm, addr1, addr2);
		if (!same)
			break;

		shared |= same;
		len -= PAGE_SIZE;
		retval += PAGE_SIZE;
		addr1 += PAGE_SIZE;
		addr2 += PAGE_SIZE;
	}
	unlock_kernel();
	if (shared)
		flush_tlb_mm(mm);

badvma:
	up(&mm->mmap_sem);
	return retval;
}


