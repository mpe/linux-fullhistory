/*
 *  linux/kernel/chr_drv/mem.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/mouse.h>

#include <asm/segment.h>
#include <asm/io.h>

static int read_ram(struct inode * inode, struct file * file,char * buf, int count)
{
	return -EIO;
}

static int write_ram(struct inode * inode, struct file * file,char * buf, int count)
{
	return -EIO;
}

static int read_mem(struct inode * inode, struct file * file,char * buf, int count)
{
	unsigned long addr;
	char *tmp;
	unsigned long pde, pte, page;
	int i;

	if (count < 0)
		return -EINVAL;
	addr = file->f_pos;
	tmp = buf;
	while (count > 0) {
		if (current->signal & ~current->blocked)
			break;
		pde = (unsigned long) pg_dir + (addr >> 20 & 0xffc);
		pte = *(unsigned long *) pde;
		if (!(pte & PAGE_PRESENT))
			break;
		pte &= 0xfffff000;
		pte += (addr >> 10) & 0xffc;
		page = *(unsigned long *) pte;
		if (!(page & 1))
			break;
		page &= 0xfffff000;
		page += addr & 0xfff;
		i = 4096-(addr & 0xfff);
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

static int write_mem(struct inode * inode, struct file * file,char * buf, int count)
{
	unsigned long addr;
	char *tmp;
	unsigned long pde, pte, page;
	int i;

	if (count < 0)
		return -EINVAL;
	addr = file->f_pos;
	tmp = buf;
	while (count > 0) {
		if (current->signal & ~current->blocked)
			break;
		pde = (unsigned long) pg_dir + (addr >> 20 & 0xffc);
		pte = *(unsigned long *) pde;
		if (!(pte & PAGE_PRESENT))
			break;
		pte &= 0xfffff000;
		pte += (addr >> 10) & 0xffc;
		page = *(unsigned long *) pte;
		if (!(page & PAGE_PRESENT))
			break;
		if (!(page & 2)) {
			do_wp_page(0,addr,current,0);
			continue;
		}
		page &= 0xfffff000;
		page += addr & 0xfff;
		i = 4096-(addr & 0xfff);
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

static int read_kmem(struct inode * inode, struct file * file,char * buf, int count)
{
	unsigned long p = file->f_pos;

	if (count < 0)
		return -EINVAL;
	if (p >= high_memory)
		return 0;
	if (count > high_memory - p)
		count = high_memory - p;
	memcpy_tofs(buf,(void *) p,count);
	file->f_pos += count;
	return count;
}

static int write_kmem(struct inode * inode, struct file * file,char * buf, int count)
{
	unsigned long p = file->f_pos;

	if (count < 0)
		return -EINVAL;
	if (p >= high_memory)
		return 0;
	if (count > high_memory - p)
		count = high_memory - p;
	memcpy_fromfs((void *) p,buf,count);
	file->f_pos += count;
	return count;
}

static int read_port(struct inode * inode,struct file * file,char * buf, int count)
{
	unsigned int i = file->f_pos;
	char * tmp = buf;

	while (count-- > 0 && i < 65536) {
		put_fs_byte(inb(i),tmp);
		i++;
		tmp++;
	}
	file->f_pos = i;
	return tmp-buf;
}

static int write_port(struct inode * inode,struct file * file,char * buf, int count)
{
	unsigned int i = file->f_pos;
	char * tmp = buf;

	while (count-- > 0 && i < 65536) {
		outb(get_fs_byte(tmp),i);
		i++;
		tmp++;
	}
	file->f_pos = i;
	return tmp-buf;
}

static int read_zero(struct inode *node,struct file *file,char *buf,int count)
{
	int left;

	for (left = count; left > 0; left--) {
		put_fs_byte(0,buf);
		buf++;
	}
	return count;
}

/*
 * The memory devices use the full 32 bits of the offset, and so we cannot
 * check against negative addresses: they are ok. The return value is weird,
 * though, in that case (0).
 *
 * also note that seeking relative to the "end of file" isn't supported:
 * it has no meaning, so it returns -EINVAL.
 */
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
	if (file->f_pos < 0)
		return 0;
	return file->f_pos;
}

static int mem_read(struct inode * inode, struct file * file, char * buf, int count)
{
	switch (MINOR(inode->i_rdev)) {
		case 0:
			return read_ram(inode,file,buf,count);
		case 1:
			return read_mem(inode,file,buf,count);
		case 2:
			return read_kmem(inode,file,buf,count);
		case 3:
			return 0;	/* /dev/null */
		case 4:
			return read_port(inode,file,buf,count);
		case 5:
			return read_zero(inode,file,buf,count);
		default:
			return -ENODEV;
	}
}

static int mem_write(struct inode * inode, struct file * file, char * buf, int count)
{
	switch (MINOR(inode->i_rdev)) {
		case 0:
			return write_ram(inode,file,buf,count);
		case 1:
			return write_mem(inode,file,buf,count);
		case 2:
			return write_kmem(inode,file,buf,count);
		case 3:
			return count;	/* /dev/null */
		case 4:
			return write_port(inode,file,buf,count);
		case 5:
			return count; /* /dev/zero */
		default:
			return -ENODEV;
	}
}

static struct file_operations mem_fops = {
	mem_lseek,
	mem_read,
	mem_write,
	NULL,		/* mem_readdir */
	NULL,		/* mem_select */
	NULL,		/* mem_ioctl */
	NULL,		/* no special open code */
	NULL		/* no special release code */
};

long chr_dev_init(long mem_start, long mem_end)
{
	chrdev_fops[1] = &mem_fops;
	mem_start = tty_init(mem_start);
	mem_start = lp_init(mem_start);
	mem_start = mouse_init(mem_start);
	return mem_start;
}
