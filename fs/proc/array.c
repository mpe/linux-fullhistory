/*
 *  linux/fs/proc/array.c
 *
 *  Copyright (C) 1992  by Linus Torvalds
 *  based on ideas by Darren Senn
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>

#include <asm/segment.h>
#include <asm/io.h>

static int get_loadavg(char * buffer)
{
	return sprintf(buffer,"%d.%02d %d.%02d %d.%02d\n",
		avenrun[0] >> FSHIFT,
		(FIXED_1/2 + (avenrun[0] & (FIXED_1-1))*100) >> FSHIFT,
		avenrun[1] >> FSHIFT,
		(FIXED_1/2 + (avenrun[1] & (FIXED_1-1))*100) >> FSHIFT,
		avenrun[2] >> FSHIFT,
		(FIXED_1/2 + (avenrun[2] & (FIXED_1-1))*100) >> FSHIFT);
}

static int get_uptime(char * buffer)
{
	return sprintf(buffer,"%d\n",(jiffies+jiffies_offset)/HZ);
}

static int get_meminfo(char * buffer)
{
	struct sysinfo i;

	si_meminfo(&i);
	si_swapinfo(&i);
	return sprintf(buffer, "        total:   used:    free:   shared:  buffers:\n"
		"Mem:  %8d %8d %8d %8d %8d\n"
		"Swap: %8d %8d %8d\n",
		i.totalram, i.totalram-i.freeram, i.freeram, i.sharedram, i.bufferram,
		i.totalswap, i.totalswap-i.freeswap, i.freeswap);
}

static struct task_struct ** get_task(pid_t pid)
{
	struct task_struct ** p;

	p = task;
	while (++p < task+NR_TASKS) {
		if (*p && (*p)->pid == pid)
			return p;
	}
	return NULL;
}

static unsigned long get_phys_addr(struct task_struct ** p, unsigned long ptr)
{
	unsigned long page;

	if (!p || !*p || ptr >= TASK_SIZE)
		return 0;
	page = (*p)->tss.cr3;
	page += (ptr >> 20) & 0xffc;
	page = *(unsigned long *) page;
	if (!(page & 1))
		return 0;
	page &= 0xfffff000;
	page += (ptr >> 10) & 0xffc;
	page = *(unsigned long *) page;
	if (!(page & 1))
		return 0;
	page &= 0xfffff000;
	page += ptr & 0xfff;
	return page;
}

static unsigned long get_long(struct task_struct ** p, unsigned long ptr)
{
	unsigned long addr;

	if (ptr & 3)
		return 0;
	addr = get_phys_addr(p,ptr);
	if (!addr)
		return 0;
	return *(unsigned long *) addr;
}

static int get_char(struct task_struct ** p, unsigned long ptr)
{
	unsigned long addr;

	addr = get_phys_addr(p,ptr);
	if (!addr)
		return -1;
	return *(unsigned char *) addr;
}

static int get_array(struct task_struct ** p, unsigned long ptr, char * buffer)
{
	unsigned long tmp;
	int size = 0, result = 0;
	unsigned long array;
	char c;

	array = get_long(p,ptr);
	if (!ptr)
		return 0;
	for (;;) {
		tmp = get_long(p,array);
		if (!tmp)
			return result;
		array += 4;
		while ((c = get_char(p,tmp++)) > 0) {
			if (size < PAGE_SIZE)
				buffer[size++] = c;
			else
				return result;
		}
		if (c < 0)
			return result;
		result = size;
		if (size < PAGE_SIZE)
			buffer[size++] = '\0';
	}
}

static int get_env(int pid, char * buffer)
{
	struct task_struct ** p = get_task(pid);

	if (!p || !*p)
		return 0;
	return get_array(p, (*p)->start_stack+8, buffer);
}

static int get_arg(int pid, char * buffer)
{
	struct task_struct ** p = get_task(pid);

	if (!p || !*p)
		return 0;
	return get_array(p, (*p)->start_stack+4, buffer);
}

static int get_stat(int pid, char * buffer)
{
	struct task_struct ** p = get_task(pid);
	char state;

	if (!p || !*p)
		return 0;
	if ((*p)->state < 0)
		state = '.';
	else
		state = "RSDZTD"[(*p)->state];
	return sprintf(buffer,"%d (%s) %c %d %d %d %d\n",
		pid,
		(*p)->comm,
		state,
		(*p)->p_pptr->pid,
		(*p)->pgrp,
		(*p)->session,
		(*p)->tty);
}

static int array_read(struct inode * inode, struct file * file,char * buf, int count)
{
	char * page;
	int length;
	int end;
	int type, pid;

	if (count < 0)
		return -EINVAL;
	page = (char *) get_free_page(GFP_KERNEL);
	*page = 0;
	if (!page)
		return -ENOMEM;
	type = inode->i_ino;
	pid = type >> 16;
	type &= 0x0000ffff;
	switch (type) {
		case 2:
			length = get_loadavg(page);
			break;
		case 3:
			length = get_uptime(page);
			break;
		case 4:
			length = get_meminfo(page);
			break;
		case 9:
			length = get_env(pid, page);
			break;
		case 10:
			length = get_arg(pid, page);
			break;
		case 11:
			length = get_stat(pid, page);
			break;
		default:
			return -EBADF;
	}
	if (file->f_pos >= length)
		return 0;
	if (count + file->f_pos > length)
		count = length - file->f_pos;
	end = count + file->f_pos;
	memcpy_tofs(buf, page + file->f_pos, count);
	free_page((unsigned long) page);
	file->f_pos = end;
	return count;
}

static struct file_operations proc_array_operations = {
	NULL,		/* array_lseek */
	array_read,
	NULL,		/* array_write */
	NULL,		/* array_readdir */
	NULL,		/* array_select */
	NULL,		/* array_ioctl */
	NULL,		/* mmap */
	NULL,		/* no special open code */
	NULL		/* no special release code */
};

struct inode_operations proc_array_inode_operations = {
	&proc_array_operations,	/* default base directory file-ops */
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
	NULL			/* truncate */
};
