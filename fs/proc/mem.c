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
#include <linux/page.h>

#include <asm/segment.h>
#include <asm/io.h>

/*
 * mem_write isn't really a good idea right now. It needs
 * to check a lot more: if the process we try to write to 
 * dies in the middle right now, mem_write will overwrite
 * kernel memory.. This disables it altogether.
 */
#define mem_write NULL

static int mem_read(struct inode * inode, struct file * file,char * buf, int count)
{
	unsigned long addr, pid, cr3;
	char *tmp;
	unsigned long pte, page;
	int i;

	if (count < 0)
		return -EINVAL;
	pid = inode->i_ino;
	pid >>= 16;
	cr3 = 0;
	for (i = 1 ; i < NR_TASKS ; i++)
		if (task[i] && task[i]->pid == pid) {
			cr3 = task[i]->tss.cr3;
			break;
		}
	if (!cr3)
		return -EACCES;
	addr = file->f_pos;
	tmp = buf;
	while (count > 0) {
		if (current->signal & ~current->blocked)
			break;
		pte = *PAGE_DIR_OFFSET(cr3,addr);
		if (!(pte & PAGE_PRESENT))
			break;
		pte &= PAGE_MASK;
		pte += PAGE_PTR(addr);
		page = *(unsigned long *) pte;
		if (!(page & 1))
			break;
		page &= PAGE_MASK;
		page += addr & ~PAGE_MASK;
		i = PAGE_SIZE-(addr & ~PAGE_MASK);
		if (i > count)
			i = count;
		memcpy_tofs(tmp,(void *) page,i);
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
	unsigned long addr, pid, cr3;
	char *tmp;
	unsigned long pte, page;
	int i;

	if (count < 0)
		return -EINVAL;
	addr = file->f_pos;
	pid = inode->i_ino;
	pid >>= 16;
	cr3 = 0;
	for (i = 1 ; i < NR_TASKS ; i++)
		if (task[i] && task[i]->pid == pid) {
			cr3 = task[i]->tss.cr3;
			break;
		}
	if (!cr3)
		return -EACCES;
	tmp = buf;
	while (count > 0) {
		if (current->signal & ~current->blocked)
			break;
		pte = *PAGE_DIR_OFFSET(cr3,addr);
		if (!(pte & PAGE_PRESENT))
			break;
		pte &= PAGE_MASK;
		pte += PAGE_PTR(addr);
		page = *(unsigned long *) pte;
		if (!(page & PAGE_PRESENT))
			break;
		if (!(page & 2)) {
			do_wp_page(0,addr,current,0);
			continue;
		}
		page &= PAGE_MASK;
		page += addr & ~PAGE_MASK;
		i = PAGE_SIZE-(addr & ~PAGE_MASK);
		if (i > count)
			i = count;
		memcpy_fromfs((void *) page,tmp,i);
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

int
mem_mmap(struct inode * inode, struct file * file,
	     struct vm_area_struct * vma)
{
  unsigned long *src_table, *dest_table, stmp, dtmp, cr3;
  struct vm_area_struct *src_vma = 0;
  int i;

  /* Get the source's task information */

  cr3 = 0;
  for (i = 1 ; i < NR_TASKS ; i++)
    if (task[i] && task[i]->pid == (inode->i_ino >> 16)) {
      cr3     = task[i]->tss.cr3;
      src_vma = task[i]->mm->mmap;
      break;
    }

  if (!cr3)
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

    src_table = PAGE_DIR_OFFSET(cr3, stmp);
    if (!*src_table)
      return -EINVAL;
    src_table = (unsigned long *)((*src_table & PAGE_MASK) + PAGE_PTR(stmp));
    if (!*src_table)
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

    src_table = PAGE_DIR_OFFSET(cr3, stmp);
    src_table = (unsigned long *)((*src_table & PAGE_MASK) + PAGE_PTR(stmp));

    dest_table = PAGE_DIR_OFFSET(current->tss.cr3, dtmp);

    if (!*dest_table) {
      *dest_table = get_free_page(GFP_KERNEL);
      if (!*dest_table) { oom(current); *dest_table=BAD_PAGE; }
      else *dest_table |= PAGE_TABLE;
    }
    
    dest_table = (unsigned long *)((*dest_table & PAGE_MASK) + PAGE_PTR(dtmp));

    if (!(*src_table & PAGE_PRESENT)) 
      do_no_page(src_vma, stmp, PAGE_PRESENT);

    if ((vma->vm_flags & VM_WRITE) && !(*src_table & PAGE_RW))
      do_wp_page(src_vma, stmp, PAGE_RW | PAGE_PRESENT);

    *src_table |= PAGE_DIRTY;
    *dest_table = *src_table;
    mem_map[MAP_NR(*src_table)]++;

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
