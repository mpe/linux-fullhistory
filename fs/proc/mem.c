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

static int mem_read(struct inode * inode, struct file * file,char * buf, int count)
{
	pgd_t *pgdir;
	pte_t pte;
	char * page;
	struct task_struct * tsk;
	unsigned long addr, pid;
	char *tmp;
	int i;

	if (count < 0)
		return -EINVAL;
	pid = inode->i_ino;
	pid >>= 16;
	tsk = NULL;
	for (i = 1 ; i < NR_TASKS ; i++)
		if (task[i] && task[i]->pid == pid) {
			tsk = task[i];
			break;
		}
	if (!tsk)
		return -EACCES;
	addr = file->f_pos;
	tmp = buf;
	while (count > 0) {
		if (current->signal & ~current->blocked)
			break;
		pgdir = PAGE_DIR_OFFSET(tsk,addr);
		if (pgd_none(*pgdir))
			break;
		if (pgd_bad(*pgdir)) {
			printk("Bad page dir entry %08lx\n", pgd_val(*pgdir));
			pgd_clear(pgdir);
			break;
		}
		pte = *(pte_t *) (PAGE_PTR(addr) + pgd_page(*pgdir));
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
	pgd_t * pgdir;
	pte_t * pte;
	char * page;
	struct task_struct * tsk;
	unsigned long addr, pid;
	char *tmp;
	int i;

	if (count < 0)
		return -EINVAL;
	addr = file->f_pos;
	pid = inode->i_ino;
	pid >>= 16;
	tsk = NULL;
	for (i = 1 ; i < NR_TASKS ; i++)
		if (task[i] && task[i]->pid == pid) {
			tsk = task[i];
			break;
		}
	if (!tsk)
		return -EACCES;
	tmp = buf;
	while (count > 0) {
		if (current->signal & ~current->blocked)
			break;
		pgdir = PAGE_DIR_OFFSET(tsk,addr);
		if (pgd_none(*pgdir))
			break;
		if (pgd_bad(*pgdir)) {
			printk("Bad page dir entry %08lx\n", pgd_val(*pgdir));
			pgd_clear(pgdir);
			break;
		}
		pte = *(pte_t *) (PAGE_PTR(addr) + pgd_page(*pgdir));
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
	pte_t *src_table, *dest_table;
	unsigned long stmp, dtmp;
	struct vm_area_struct *src_vma = NULL;
	int i;

	/* Get the source's task information */

	tsk = NULL;
	for (i = 1 ; i < NR_TASKS ; i++)
		if (task[i] && task[i]->pid == (inode->i_ino >> 16)) {
			tsk = task[i];
			src_vma = task[i]->mm->mmap;
			break;
		}

	if (!tsk)
		return -EACCES;

	/* Ensure that we have a valid source area.  (Has to be mmap'ed and
	 have valid page information.)  We can't map shared memory at the
	 moment because working out the vm_area_struct & nattach stuff isn't
	 worth it. */

	stmp = vma->vm_offset;
	while (stmp < vma->vm_offset + (vma->vm_end - vma->vm_start)) {
		while (src_vma && stmp > src_vma->vm_end)
			src_vma = src_vma->vm_next;
		if (!src_vma || (src_vma->vm_flags & VM_SHM))
			return -EINVAL;

		src_dir = PAGE_DIR_OFFSET(tsk, stmp);
		if (pgd_none(*src_dir))
			return -EINVAL;
		if (pgd_bad(*src_dir)) {
			printk("Bad source page dir entry %08lx\n", pgd_val(*src_dir));
			return -EINVAL;
		}

		src_table = (pte_t *)(pgd_page(*src_dir) + PAGE_PTR(stmp));
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

	src_vma = task[i]->mm->mmap;
	stmp    = vma->vm_offset;
	dtmp    = vma->vm_start;

	while (dtmp < vma->vm_end) {
		while (src_vma && stmp > src_vma->vm_end)
			src_vma = src_vma->vm_next;

		src_dir = PAGE_DIR_OFFSET(tsk, stmp);
		src_table = (pte_t *) (pgd_page(*src_dir) + PAGE_PTR(stmp));

		dest_dir = PAGE_DIR_OFFSET(current, dtmp);

		if (pgd_none(*dest_dir)) {
			unsigned long page = get_free_page(GFP_KERNEL);
			if (!page)
				return -ENOMEM;
			if (pgd_none(*dest_dir)) {
				pgd_set(dest_dir, (pte_t *) page);
			} else {
				free_page(page);
			}
		}

		if (pgd_bad(*dest_dir)) {
			printk("Bad dest directory entry %08lx\n", pgd_val(*dest_dir));
			return -EINVAL;
		}

		dest_table = (pte_t *) (pgd_page(*dest_dir) + PAGE_PTR(dtmp));

		if (!pte_present(*src_table))
			do_no_page(src_vma, stmp, 1);

		if ((vma->vm_flags & VM_WRITE) && !pte_write(*src_table))
			do_wp_page(src_vma, stmp, 1);

		*src_table = pte_mkdirty(*src_table);
		*dest_table = *src_table;
		mem_map[MAP_NR(pte_page(*src_table))]++;

		stmp += PAGE_SIZE;
		dtmp += PAGE_SIZE;
	}

	invalidate();
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
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};
