/*
 *  linux/fs/proc/mem.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>

#include <asm/page.h>
#include <asm/segment.h>
#include <asm/io.h>
#include <asm/pgtable.h>

/*
 * mem_write isn't really a good idea right now. It needs
 * to check a lot more: if the process we try to write to 
 * dies in the middle right now, mem_write will overwrite
 * kernel memory.. This disables it altogether.
 */
#define mem_write NULL

static int check_range(struct task_struct * tsk, unsigned long addr, int count)
{
	struct vm_area_struct *vma;
	int retval;

	vma = find_vma(tsk, addr);
	if (!vma)
		return -EACCES;
	if (vma->vm_start > addr)
		return -EACCES;
	if (!(vma->vm_flags & VM_READ))
		return -EACCES;
	while ((retval = vma->vm_end - addr) < count) {
		struct vm_area_struct *next = vma->vm_next;
		if (!next)
			break;
		if (vma->vm_end != next->vm_start)
			break;
		if (!(next->vm_flags & VM_READ))
			break;
		vma = next;
	}
	if (retval > count)
		retval = count;
	return retval;
}

static struct task_struct * get_task(int pid)
{
	struct task_struct * tsk = current;

	if (pid != tsk->pid) {
		int i;
		tsk = NULL;
		for (i = 1 ; i < NR_TASKS ; i++)
			if (task[i] && task[i]->pid == pid) {
				tsk = task[i];
				break;
			}
		/*
		 * allow accesses only under the same circumstances
		 * that we would allow ptrace to work
		 */
		if (tsk) {
			if (!(tsk->flags & PF_PTRACED)
			    || tsk->state != TASK_STOPPED
			    || tsk->p_pptr != current)
				tsk = NULL;
		}
	}
	return tsk;
}

static int mem_read(struct inode * inode, struct file * file,char * buf, int count)
{
	pgd_t *page_dir;
	pmd_t *page_middle;
	pte_t pte;
	char * page;
	struct task_struct * tsk;
	unsigned long addr;
	char *tmp;
	int i;

	if (count < 0)
		return -EINVAL;
	tsk = get_task(inode->i_ino >> 16);
	if (!tsk)
		return -ESRCH;
	addr = file->f_pos;
	count = check_range(tsk, addr, count);
	if (count < 0)
		return count;
	tmp = buf;
	while (count > 0) {
		if (current->signal & ~current->blocked)
			break;
		page_dir = pgd_offset(tsk->mm,addr);
		if (pgd_none(*page_dir))
			break;
		if (pgd_bad(*page_dir)) {
			printk("Bad page dir entry %08lx\n", pgd_val(*page_dir));
			pgd_clear(page_dir);
			break;
		}
		page_middle = pmd_offset(page_dir,addr);
		if (pmd_none(*page_middle))
			break;
		if (pmd_bad(*page_middle)) {
			printk("Bad page middle entry %08lx\n", pmd_val(*page_middle));
			pmd_clear(page_middle);
			break;
		}
		pte = *pte_offset(page_middle,addr);
		if (!pte_present(pte))
			break;
		page = (char *) pte_page(pte) + (addr & ~PAGE_MASK);
		i = PAGE_SIZE-(addr & ~PAGE_MASK);
		if (i > count)
			i = count;
		memcpy_tofs(tmp, page, i);
		addr += i;
		tmp += i;
		count -= i;
	}
	file->f_pos = addr;
	return tmp-buf;
}

#ifndef mem_write

static int mem_write(struct inode * inode, struct file * file,char * buf, int count)
{
	pgd_t *page_dir;
	pmd_t *page_middle;
	pte_t pte;
	char * page;
	struct task_struct * tsk;
	unsigned long addr;
	char *tmp;
	int i;

	if (count < 0)
		return -EINVAL;
	addr = file->f_pos;
	tsk = get_task(inode->i_ino >> 16);
	if (!tsk)
		return -ESRCH;
	tmp = buf;
	while (count > 0) {
		if (current->signal & ~current->blocked)
			break;
		page_dir = pgd_offset(tsk,addr);
		if (pgd_none(*page_dir))
			break;
		if (pgd_bad(*page_dir)) {
			printk("Bad page dir entry %08lx\n", pgd_val(*page_dir));
			pgd_clear(page_dir);
			break;
		}
		page_middle = pmd_offset(page_dir,addr);
		if (pmd_none(*page_middle))
			break;
		if (pmd_bad(*page_middle)) {
			printk("Bad page middle entry %08lx\n", pmd_val(*page_middle));
			pmd_clear(page_middle);
			break;
		}
		pte = *pte_offset(page_middle,addr);
		if (!pte_present(pte))
			break;
		if (!pte_write(pte))
			break;
		page = (char *) pte_page(pte) + (addr & ~PAGE_MASK);
		i = PAGE_SIZE-(addr & ~PAGE_MASK);
		if (i > count)
			i = count;
		memcpy_fromfs(page, tmp, i);
		addr += i;
		tmp += i;
		count -= i;
	}
	file->f_pos = addr;
	if (tmp != buf)
		return tmp-buf;
	if (current->signal & ~current->blocked)
		return -ERESTARTSYS;
	return 0;
}

#endif

static int mem_lseek(struct inode * inode, struct file * file, off_t offset, int orig)
{
	switch (orig) {
		case 0:
			file->f_pos = offset;
			return file->f_pos;
		case 1:
			file->f_pos += offset;
			return file->f_pos;
		default:
			return -EINVAL;
	}
}

/*
 * This isn't really reliable by any means..
 */
int mem_mmap(struct inode * inode, struct file * file,
	     struct vm_area_struct * vma)
{
	struct task_struct *tsk;
	pgd_t *src_dir, *dest_dir;
	pmd_t *src_middle, *dest_middle;
	pte_t *src_table, *dest_table;
	unsigned long stmp, dtmp;
	struct vm_area_struct *src_vma = NULL;

	/* Get the source's task information */

	tsk = get_task(inode->i_ino >> 16);

	if (!tsk)
		return -ESRCH;

	/* Ensure that we have a valid source area.  (Has to be mmap'ed and
	 have valid page information.)  We can't map shared memory at the
	 moment because working out the vm_area_struct & nattach stuff isn't
	 worth it. */

	src_vma = tsk->mm->mmap;
	stmp = vma->vm_offset;
	while (stmp < vma->vm_offset + (vma->vm_end - vma->vm_start)) {
		while (src_vma && stmp > src_vma->vm_end)
			src_vma = src_vma->vm_next;
		if (!src_vma || (src_vma->vm_flags & VM_SHM))
			return -EINVAL;

		src_dir = pgd_offset(tsk->mm, stmp);
		if (pgd_none(*src_dir))
			return -EINVAL;
		if (pgd_bad(*src_dir)) {
			printk("Bad source page dir entry %08lx\n", pgd_val(*src_dir));
			return -EINVAL;
		}
		src_middle = pmd_offset(src_dir, stmp);
		if (pmd_none(*src_middle))
			return -EINVAL;
		if (pmd_bad(*src_middle)) {
			printk("Bad source page middle entry %08lx\n", pmd_val(*src_middle));
			return -EINVAL;
		}
		src_table = pte_offset(src_middle, stmp);
		if (pte_none(*src_table))
			return -EINVAL;

		if (stmp < src_vma->vm_start) {
			if (!(src_vma->vm_flags & VM_GROWSDOWN))
				return -EINVAL;
			if (src_vma->vm_end - stmp > current->rlim[RLIMIT_STACK].rlim_cur)
				return -EINVAL;
		}
		stmp += PAGE_SIZE;
	}

	src_vma = tsk->mm->mmap;
	stmp    = vma->vm_offset;
	dtmp    = vma->vm_start;

	flush_cache_range(vma->vm_mm, vma->vm_start, vma->vm_end);
	flush_cache_range(src_vma->vm_mm, src_vma->vm_start, src_vma->vm_end);
	while (dtmp < vma->vm_end) {
		while (src_vma && stmp > src_vma->vm_end)
			src_vma = src_vma->vm_next;

		src_dir = pgd_offset(tsk->mm, stmp);
		src_middle = pmd_offset(src_dir, stmp);
		src_table = pte_offset(src_middle, stmp);

		dest_dir = pgd_offset(current->mm, dtmp);
		dest_middle = pmd_alloc(dest_dir, dtmp);
		if (!dest_middle)
			return -ENOMEM;
		dest_table = pte_alloc(dest_middle, dtmp);
		if (!dest_table)
			return -ENOMEM;

		if (!pte_present(*src_table))
			do_no_page(tsk, src_vma, stmp, 1);

		if ((vma->vm_flags & VM_WRITE) && !pte_write(*src_table))
			do_wp_page(tsk, src_vma, stmp, 1);

		set_pte(src_table, pte_mkdirty(*src_table));
		set_pte(dest_table, *src_table);
		mem_map[MAP_NR(pte_page(*src_table))].count++;

		stmp += PAGE_SIZE;
		dtmp += PAGE_SIZE;
	}

	flush_tlb_range(vma->vm_mm, vma->vm_start, vma->vm_end);
	flush_tlb_range(src_vma->vm_mm, src_vma->vm_start, src_vma->vm_end);
	return 0;
}

static struct file_operations proc_mem_operations = {
	mem_lseek,
	mem_read,
	mem_write,
	NULL,		/* mem_readdir */
	NULL,		/* mem_select */
	NULL,		/* mem_ioctl */
	mem_mmap,	/* mmap */
	NULL,		/* no special open code */
	NULL,		/* no special release code */
	NULL		/* can't fsync */
};

struct inode_operations proc_mem_inode_operations = {
	&proc_mem_operations,	/* default base directory file-ops */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};
