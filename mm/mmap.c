/*
 *	linux/mm/mmap.c
 *
 * Written by obz.
 */
#include <linux/stat.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/shm.h>
#include <linux/errno.h>
#include <linux/mman.h>
#include <linux/string.h>

#include <asm/segment.h>
#include <asm/system.h>

/*
 * description of effects of mapping type and prot in current implementation.
 * this is due to the current handling of page faults in memory.c. the expected
 * behavior is in parens:
 *
 * map_type	prot
 *		PROT_NONE	PROT_READ	PROT_WRITE	PROT_EXEC
 * MAP_SHARED	r: (no) yes	r: (yes) yes	r: (no) yes	r: (no) no
 *		w: (no) yes	w: (no) copy	w: (yes) yes	w: (no) no
 *		x: (no) no	x: (no) no	x: (no) no	x: (yes) no
 *		
 * MAP_PRIVATE	r: (no) yes	r: (yes) yes	r: (no) yes	r: (no) no
 *		w: (no) copy	w: (no) copy	w: (copy) copy	w: (no) no
 *		x: (no) no	x: (no) no	x: (no) no	x: (yes) no
 *
 */

#define CODE_SPACE(addr)	\
 (PAGE_ALIGN(addr) < current->start_code + current->end_code)

int do_mmap(struct file * file, unsigned long addr, unsigned long len,
	unsigned long prot, unsigned long flags, unsigned long off)
{
	int mask, error;

	if (addr > TASK_SIZE || len > TASK_SIZE || addr > TASK_SIZE-len)
		return -EINVAL;

	/*
	 * do simple checking here so the lower-level routines won't have
	 * to. we assume access permissions have been handled by the open
	 * of the memory object, so we don't do any here.
	 */

	switch (flags & MAP_TYPE) {
	case MAP_SHARED:
		if ((prot & PROT_WRITE) && !(file->f_mode & 2))
			return -EACCES;
		/* fall through */
	case MAP_PRIVATE:
		if (!(file->f_mode & 1))
			return -EACCES;
		break;

	default:
		return -EINVAL;
	}

	/*
	 * obtain the address to map to. we verify (or select) it and ensure
	 * that it represents a valid section of the address space.
	 */

	if (flags & MAP_FIXED) {
		if (addr & ~PAGE_MASK)
			return -EINVAL;
		if (len > TASK_SIZE || addr > TASK_SIZE - len)
			return -ENOMEM;
	} else {
		struct vm_area_struct * vmm;

		/* Maybe this works.. Ugly it is. */
		addr = SHM_RANGE_START;
		while (addr+len < SHM_RANGE_END) {
			for (vmm = current->mmap ; vmm ; vmm = vmm->vm_next) {
				if (addr >= vmm->vm_end)
					continue;
				if (addr + len <= vmm->vm_start)
					continue;
				addr = PAGE_ALIGN(vmm->vm_end);
				break;
			}
			if (!vmm)
				break;
		}
		if (addr+len >= SHM_RANGE_END)
			return -ENOMEM;
	}

	/*
	 * determine the object being mapped and call the appropriate
	 * specific mapper. the address has already been validated, but
	 * not unmapped
	 */
	if (!file->f_op || !file->f_op->mmap)
		return -ENODEV;
	mask = 0;
	if (prot & (PROT_READ | PROT_EXEC))
		mask |= PAGE_READONLY;
	if (prot & PROT_WRITE)
		if ((flags & MAP_TYPE) == MAP_PRIVATE)
			mask |= PAGE_COW;
		else
			mask |= PAGE_RW;
	if (!mask)
		return -EINVAL;

	error = file->f_op->mmap(file->f_inode, file, addr, len, mask, off);
	if (!error)
		return addr;

	if (!current->errno)
		current->errno = -error;
	return -1;
}

asmlinkage int sys_mmap(unsigned long *buffer)
{
	unsigned long fd;
	struct file * file;

	fd = get_fs_long(buffer+4);
	if (fd >= NR_OPEN || !(file = current->filp[fd]))
		return -EBADF;
	return do_mmap(file, get_fs_long(buffer), get_fs_long(buffer+1),
		get_fs_long(buffer+2), get_fs_long(buffer+3), get_fs_long(buffer+5));
}

asmlinkage int sys_munmap(unsigned long addr, size_t len)
{
	struct vm_area_struct *mpnt, **p, *free;

	if ((addr & ~PAGE_MASK) || addr > LONG_MAX || addr == 0 || addr + len > TASK_SIZE)
		return -EINVAL;

	/* This needs a bit of work - we need to figure out how to
	   deal with areas that overlap with something that we are using */

	p = &current->mmap;
	free = NULL;
	/*
	 * Check if this memory area is ok - put it on the temporary
	 * list if so..
	 */
	while ((mpnt = *p) != NULL) {
		if (addr > mpnt->vm_start && addr < mpnt->vm_end)
			goto bad_munmap;
		if (addr+len > mpnt->vm_start && addr + len < mpnt->vm_end)
			goto bad_munmap;
		if (addr <= mpnt->vm_start && addr + len >= mpnt->vm_end) {
			*p = mpnt->vm_next;
			mpnt->vm_next = free;
			free = mpnt;
			continue;
		}
		p = &mpnt->vm_next;
	}
	/*
	 * Ok - we have the memory areas we should free on the 'free' list,
	 * so release them, and unmap the page range..
	 */
	while (free) {
		mpnt = free;
		free = free->vm_next;
		if (mpnt->vm_ops && mpnt->vm_ops->close)
			mpnt->vm_ops->close(mpnt);
		kfree(mpnt);
	}

	unmap_page_range(addr, len);
	return 0;
bad_munmap:
/*
 * the arguments we got were bad: put the temporary list back into the mmap list
 */
	while (free) {
		mpnt = free;
		free = free->vm_next;
		mpnt->vm_next = current->mmap;
		current->mmap = mpnt;
	}
	return -EINVAL;
}

/* This is used for a general mmap of a disk file */
int generic_mmap(struct inode * inode, struct file * file,
	unsigned long addr, size_t len, int prot, unsigned long off)
{
  	struct vm_area_struct * mpnt;
	extern struct vm_operations_struct file_mmap;
	struct buffer_head * bh;

	if (prot & PAGE_RW)	/* only PAGE_COW or read-only supported right now */
		return -EINVAL;
	if (off & (inode->i_sb->s_blocksize - 1))
		return -EINVAL;
	if (!inode->i_sb || !S_ISREG(inode->i_mode))
		return -EACCES;
	if (!inode->i_op || !inode->i_op->bmap)
		return -ENOEXEC;
	if (!(bh = bread(inode->i_dev,bmap(inode,0),inode->i_sb->s_blocksize)))
		return -EACCES;
	if (!IS_RDONLY(inode)) {
		inode->i_atime = CURRENT_TIME;
		inode->i_dirt = 1;
	}
	brelse(bh);

	mpnt = (struct vm_area_struct * ) kmalloc(sizeof(struct vm_area_struct), GFP_KERNEL);
	if (!mpnt)
		return -ENOMEM;

	unmap_page_range(addr, len);	
	mpnt->vm_task = current;
	mpnt->vm_start = addr;
	mpnt->vm_end = addr + len;
	mpnt->vm_page_prot = prot;
	mpnt->vm_share = NULL;
	mpnt->vm_inode = inode;
	inode->i_count++;
	mpnt->vm_offset = off;
	mpnt->vm_ops = &file_mmap;
	mpnt->vm_next = current->mmap;
	current->mmap = mpnt;
	return 0;
}

