/*
 *  linux/fs/inode.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/stat.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>

#include <asm/system.h>

struct inode inode_table[NR_INODE]={{0,},};

static inline void wait_on_inode(struct inode * inode)
{
	cli();
	while (inode->i_lock)
		sleep_on(&inode->i_wait);
	sti();
}

static inline void lock_inode(struct inode * inode)
{
	cli();
	while (inode->i_lock)
		sleep_on(&inode->i_wait);
	inode->i_lock=1;
	sti();
}

static inline void unlock_inode(struct inode * inode)
{
	inode->i_lock=0;
	wake_up(&inode->i_wait);
}

static void write_inode(struct inode * inode)
{
	if (!inode->i_dirt)
		return;
	inode->i_dirt = 0;
	lock_inode(inode);
	if (inode->i_dev && inode->i_sb &&
	    inode->i_sb->s_op && inode->i_sb->s_op->write_inode)
		inode->i_sb->s_op->write_inode(inode);
	unlock_inode(inode);
}

static void read_inode(struct inode * inode)
{
	lock_inode(inode);
	if (inode->i_sb && inode->i_sb->s_op && inode->i_sb->s_op->read_inode)
		inode->i_sb->s_op->read_inode(inode);
	unlock_inode(inode);
}

/*
 * bmap is needed for demand-loading and paging: if this function
 * doesn't exist for a filesystem, then those things are impossible:
 * executables cannot be run from the filesystem etc...
 *
 * This isn't as bad as it sounds: the read-routines might still work,
 * so the filesystem would be otherwise ok (for example, you might have
 * a DOS filesystem, which doesn't lend itself to bmap very well, but
 * you could still transfer files to/from the filesystem)
 */
int bmap(struct inode * inode, int block)
{
	if (inode->i_op && inode->i_op->bmap)
		return inode->i_op->bmap(inode,block);
	return 0;
}

void invalidate_inodes(int dev)
{
	int i;
	struct inode * inode;

	inode = 0+inode_table;
	for(i=0 ; i<NR_INODE ; i++,inode++) {
		wait_on_inode(inode);
		if (inode->i_dev == dev) {
			if (inode->i_count) {
				printk("inode in use on removed disk\n\r");
				continue;
			}
			inode->i_dev = inode->i_dirt = 0;
		}
	}
}

void sync_inodes(void)
{
	int i;
	struct inode * inode;

	inode = 0+inode_table;
	for(i=0 ; i<NR_INODE ; i++,inode++) {
		wait_on_inode(inode);
		if (inode->i_dirt)
			write_inode(inode);
	}
}

void iput(struct inode * inode)
{
	if (!inode)
		return;
	wait_on_inode(inode);
	if (!inode->i_count) {
		printk("iput: trying to free free inode\n");
		printk("device %04x, inode %d, mode=%07o\n",inode->i_rdev,
			inode->i_ino,inode->i_mode);
		return;
	}
	if (inode->i_pipe) {
		wake_up(&PIPE_READ_WAIT(*inode));
		wake_up(&PIPE_WRITE_WAIT(*inode));
	}
repeat:
	if (inode->i_count>1) {
		inode->i_count--;
		return;
	}
	if (inode->i_pipe) {
		unsigned long page = (unsigned long) PIPE_BASE(*inode);
		PIPE_BASE(*inode) = NULL;
		free_page(page);
	}
	if (!inode->i_dev) {
		inode->i_count--;
		return;
	}
	if (!inode->i_nlink) {
		if (inode->i_sb && inode->i_sb->s_op && inode->i_sb->s_op->put_inode) {
			inode->i_sb->s_op->put_inode(inode);
			return;
		}
	}
	if (inode->i_dirt) {
		write_inode(inode);	/* we can sleep - so do again */
		wait_on_inode(inode);
		goto repeat;
	}
	inode->i_count--;
	return;
}

struct inode * get_empty_inode(void)
{
	struct inode * inode;
	static struct inode * last_inode = inode_table;
	int i;

	do {
		inode = NULL;
		for (i = NR_INODE; i ; i--) {
			if (++last_inode >= inode_table + NR_INODE)
				last_inode = inode_table;
			if (!last_inode->i_count) {
				inode = last_inode;
				if (!inode->i_dirt && !inode->i_lock)
					break;
			}
		}
		if (!inode) {
			for (i=0 ; i<NR_INODE ; i++)
				printk("(%04x: %d (%o)) ",inode_table[i].i_dev,
					inode_table[i].i_ino,inode_table[i].i_mode);
			panic("No free inodes in mem");
		}
		wait_on_inode(inode);
		while (inode->i_dirt) {
			write_inode(inode);
			wait_on_inode(inode);
		}
	} while (inode->i_count);
	memset(inode,0,sizeof(*inode));
	inode->i_count = 1;
	return inode;
}

struct inode * get_pipe_inode(void)
{
	struct inode * inode;

	if (!(inode = get_empty_inode()))
		return NULL;
	if (!(PIPE_BASE(*inode) = (char *) get_free_page(GFP_USER))) {
		inode->i_count = 0;
		return NULL;
	}
	inode->i_count = 2;	/* sum of readers/writers */
	PIPE_READ_WAIT(*inode) = PIPE_WRITE_WAIT(*inode) = NULL;
	PIPE_HEAD(*inode) = PIPE_TAIL(*inode) = 0;
	PIPE_READERS(*inode) = PIPE_WRITERS(*inode) = 1;
	inode->i_pipe = 1;
	return inode;
}

struct inode * iget(int dev,int nr)
{
	struct inode * inode, * empty;

	if (!dev)
		panic("iget with dev==0");
	empty = get_empty_inode();
	inode = inode_table;
	while (inode < NR_INODE+inode_table) {
		if (inode->i_dev != dev || inode->i_ino != nr) {
			inode++;
			continue;
		}
		wait_on_inode(inode);
		if (inode->i_dev != dev || inode->i_ino != nr) {
			inode = inode_table;
			continue;
		}
		inode->i_count++;
		if (inode->i_mount) {
			int i;

			for (i = 0 ; i<NR_SUPER ; i++)
				if (super_block[i].s_covered==inode)
					break;
			if (i >= NR_SUPER) {
				printk("Mounted inode hasn't got sb\n");
				if (empty)
					iput(empty);
				return inode;
			}
			iput(inode);
			if (!(inode = super_block[i].s_mounted))
				printk("iget: mounted dev has no rootinode\n");
			else {
				inode->i_count++;
				wait_on_inode(inode);
			}
		}
		if (empty)
			iput(empty);
		return inode;
	}
	if (!empty)
		return (NULL);
	inode = empty;
	if (!(inode->i_sb = get_super(dev))) {
		printk("iget: gouldn't get super-block\n\t");
		iput(inode);
		return NULL;
	}
	inode->i_dev = dev;
	inode->i_ino = nr;
	inode->i_flags = inode->i_sb->s_flags;
	read_inode(inode);
	return inode;
}
