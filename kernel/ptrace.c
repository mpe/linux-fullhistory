/*
 * linux/kernel/ptrace.c
 *
 * (C) Copyright 1999 Linus Torvalds
 *
 * Common interfaces for "ptrace()" which we do not want
 * to continually duplicate across every architecture.
 */

#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/highmem.h>

#include <asm/pgtable.h>
#include <asm/uaccess.h>

/*
 * Access another process' address space, one page at a time.
 */
static int access_one_page(struct task_struct * tsk, struct vm_area_struct * vma, unsigned long addr, void *buf, int len, int write)
{
	pgd_t * pgdir;
	pmd_t * pgmiddle;
	pte_t * pgtable;
	unsigned long mapnr;
	unsigned long maddr; 
	struct page *page;

repeat:
	pgdir = pgd_offset(vma->vm_mm, addr);
	if (pgd_none(*pgdir))
		goto fault_in_page;
	if (pgd_bad(*pgdir))
		goto bad_pgd;
	pgmiddle = pmd_offset(pgdir, addr);
	if (pmd_none(*pgmiddle))
		goto fault_in_page;
	if (pmd_bad(*pgmiddle))
		goto bad_pmd;
	pgtable = pte_offset(pgmiddle, addr);
	if (!pte_present(*pgtable))
		goto fault_in_page;
	mapnr = pte_pagenr(*pgtable);
	if (write && (!pte_write(*pgtable) || !pte_dirty(*pgtable)))
		goto fault_in_page;
	if (mapnr >= max_mapnr)
		return 0;
	page = mem_map + mapnr;
	flush_cache_page(vma, addr);

	if (write) {
		maddr = kmap(page);
		memcpy((char *)maddr + (addr & ~PAGE_MASK), buf, len);
		flush_page_to_ram(page);
		kunmap(page);
	} else {
		maddr = kmap(page);
		memcpy(buf, (char *)maddr + (addr & ~PAGE_MASK), len);
		flush_page_to_ram(page);
		kunmap(page);
	}
	return len;

fault_in_page:
	/* -1: out of memory. 0 - unmapped page */
	if (handle_mm_fault(tsk, vma, addr, write) > 0)
		goto repeat;
	return 0;

bad_pgd:
	pgd_ERROR(*pgdir);
	return 0;

bad_pmd:
	pmd_ERROR(*pgmiddle);
	return 0;
}

int access_process_vm(struct task_struct *tsk, unsigned long addr, void *buf, int len, int write)
{
	int copied;
	struct vm_area_struct * vma;

	down(&tsk->mm->mmap_sem);
	vma = find_extend_vma(tsk, addr);
	if (!vma) {
		up(&tsk->mm->mmap_sem);
		return 0;
	}
	copied = 0;
	for (;;) {
		unsigned long offset = addr & ~PAGE_MASK;
		int this_len = PAGE_SIZE - offset;
		int retval;

		if (this_len > len)
			this_len = len;
		retval = access_one_page(tsk, vma, addr, buf, this_len, write);
		copied += retval;
		if (retval != this_len)
			break;

		len -= retval;
		if (!len)
			break;

		addr += retval;
		buf += retval;

		if (addr < vma->vm_end)
			continue;	
		if (!vma->vm_next)
			break;
		if (vma->vm_next->vm_start != vma->vm_end)
			break;
	
		vma = vma->vm_next;
	}
	up(&tsk->mm->mmap_sem);
	return copied;
}

int ptrace_readdata(struct task_struct *tsk, unsigned long src, char *dst, int len)
{
	int copied = 0;

	while (len > 0) {
		char buf[128];
		int this_len, retval;

		this_len = (len > sizeof(buf)) ? sizeof(buf) : len;
		retval = access_process_vm(tsk, src, buf, this_len, 0);
		if (!retval) {
			if (copied)
				break;
			return -EIO;
		}
		if (copy_to_user(dst, buf, retval))
			return -EFAULT;
		copied += retval;
		src += retval;
		dst += retval;
		len -= retval;			
	}
	return copied;
}

int ptrace_writedata(struct task_struct *tsk, char * src, unsigned long dst, int len)
{
	int copied = 0;

	while (len > 0) {
		char buf[128];
		int this_len, retval;

		this_len = (len > sizeof(buf)) ? sizeof(buf) : len;
		if (copy_from_user(buf, src, this_len))
			return -EFAULT;
		retval = access_process_vm(tsk, dst, buf, this_len, 1);
		if (!retval) {
			if (copied)
				break;
			return -EIO;
		}
		copied += retval;
		src += retval;
		dst += retval;
		len -= retval;			
	}
	return copied;
}
