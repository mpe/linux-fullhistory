/*
 *  linux/fs/nfs/file.c
 *
 *  Copyright (C) 1992  Rick Sladkey
 *
 *  Changes Copyright (C) 1994 by Florian La Roche
 *   - Do not copy data too often around in the kernel.
 *   - In nfs_file_read the return value of kmalloc wasn't checked.
 *   - Put in a better version of read look-ahead buffering. Original idea
 *     and implementation by Wai S Kok elekokws@ee.nus.sg.
 *
 *  Expire cache on write to a file by Wai S Kok (Oct 1994).
 *
 *  nfs regular file handling functions
 */

#ifdef MODULE
#include <linux/module.h>
#endif

#include <asm/segment.h>
#include <asm/system.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/mm.h>
#include <linux/nfs_fs.h>
#include <linux/malloc.h>

static int nfs_file_read(struct inode *, struct file *, char *, int);
static int nfs_file_write(struct inode *, struct file *, char *, int);
static int nfs_fsync(struct inode *, struct file *);

static struct file_operations nfs_file_operations = {
	NULL,			/* lseek - default */
	nfs_file_read,		/* read */
	nfs_file_write,		/* write */
	NULL,			/* readdir - bad */
	NULL,			/* select - default */
	NULL,			/* ioctl - default */
	nfs_mmap,		/* mmap */
	NULL,			/* no special open is needed */
	NULL,			/* release */
	nfs_fsync,		/* fsync */
};

struct inode_operations nfs_file_inode_operations = {
	&nfs_file_operations,	/* default file operations */
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

/* Once data is inserted, it can only be deleted, if (in_use==0). */
struct read_cache {
	int		in_use;		/* currently in use? */
	unsigned long	inode_num;	/* inode number */
	off_t		file_pos;	/* file position */
	int		len;		/* size of data */
	unsigned long	time;		/* time, this entry was inserted */
	char *		buf;		/* data */
	int		buf_size;	/* size of buffer */
};

#define READ_CACHE_SIZE 5
#define EXPIRE_CACHE (HZ * 3)		/* keep no longer than 3 seconds */

unsigned long num_requests = 0;
unsigned long num_cache_hits = 0;

static int tail = 0;	/* next cache slot to replace */

static struct read_cache cache[READ_CACHE_SIZE] = {
	{ 0, 0, -1, 0, 0, NULL, 0 },
	{ 0, 0, -1, 0, 0, NULL, 0 },
	{ 0, 0, -1, 0, 0, NULL, 0 },
	{ 0, 0, -1, 0, 0, NULL, 0 },
	{ 0, 0, -1, 0, 0, NULL, 0 } };

static int nfs_fsync(struct inode *inode, struct file *file)
{
	return 0;
}

static int nfs_file_read(struct inode *inode, struct file *file, char *buf,
			 int count)
{
	int result, hunk, i, n, fs;
	struct nfs_fattr fattr;
	char *data;
	off_t pos;

	if (!inode) {
		printk("nfs_file_read: inode = NULL\n");
		return -EINVAL;
	}
	if (!S_ISREG(inode->i_mode)) {
		printk("nfs_file_read: read from non-file, mode %07o\n",
			inode->i_mode);
		return -EINVAL;
	}
	pos = file->f_pos;
	if (pos + count > inode->i_size)
		count = inode->i_size - pos;
	if (count <= 0)
		return 0;
	++num_requests;
	cli();
	for (i = 0; i < READ_CACHE_SIZE; i++)
		if ((cache[i].inode_num == inode->i_ino)
			&& (cache[i].file_pos <= pos)
			&& (cache[i].file_pos + cache[i].len >= pos + count)
			&& (abs(jiffies - cache[i].time) < EXPIRE_CACHE))
			break;
	if (i < READ_CACHE_SIZE) {
		++cache[i].in_use;
		sti();
		++num_cache_hits;
		memcpy_tofs(buf, cache[i].buf + pos - cache[i].file_pos, count);
		--cache[i].in_use;
		file->f_pos += count;
		return count;
	}
	sti();
	n = NFS_SERVER(inode)->rsize;
	for (i = 0; i < count - n; i += n) {
		result = nfs_proc_read(NFS_SERVER(inode), NFS_FH(inode), 
			pos, n, buf, &fattr, 1);
		if (result < 0)
			return result;
		pos += result;
		buf += result;
		if (result < n) {
			file->f_pos = pos;
			nfs_refresh_inode(inode, &fattr);
			return i + result;
		}
	}
	fs = 0;
	if (!(data = (char *)kmalloc(n, GFP_KERNEL))) {
		data = buf;
		fs = 1;
	}
	result = nfs_proc_read(NFS_SERVER(inode), NFS_FH(inode),
		pos, n, data, &fattr, fs);
	if (result < 0) {
		if (!fs)
			kfree_s(data, n);
		return result;
	}
	hunk = count - i;
	if (result < hunk)
		hunk = result;
	if (fs) {
		file->f_pos = pos + hunk;
		nfs_refresh_inode(inode, &fattr);
		return i + hunk;
	}
	memcpy_tofs(buf, data, hunk);
	file->f_pos = pos + hunk;
	nfs_refresh_inode(inode, &fattr);
	cli();
	if (cache[tail].in_use == 0) {
		if (cache[tail].buf)
			kfree_s(cache[tail].buf, cache[tail].buf_size);
		cache[tail].buf = data;
		cache[tail].buf_size = n;
		cache[tail].inode_num = inode->i_ino;
		cache[tail].file_pos = pos;
		cache[tail].len = result;
		cache[tail].time = jiffies;
		if (++tail >= READ_CACHE_SIZE)
			tail = 0;
	} else
		kfree_s(data, n);
	sti();
	return i + hunk;
}

static int nfs_file_write(struct inode *inode, struct file *file, char *buf,
			  int count)
{
	int result, hunk, i, n, pos;
	struct nfs_fattr fattr;

	if (!inode) {
		printk("nfs_file_write: inode = NULL\n");
		return -EINVAL;
	}
	if (!S_ISREG(inode->i_mode)) {
		printk("nfs_file_write: write to non-file, mode %07o\n",
			inode->i_mode);
		return -EINVAL;
	}
	if (count <= 0)
		return 0;

	cli();
	/* If hit, cache is dirty and must be expired. */
	for (i = 0; i < READ_CACHE_SIZE; i++)
		if(cache[i].inode_num == inode->i_ino)
			cache[i].time -= EXPIRE_CACHE;
        sti();

	pos = file->f_pos;
	if (file->f_flags & O_APPEND)
		pos = inode->i_size;
	n = NFS_SERVER(inode)->wsize;
	for (i = 0; i < count; i += n) {
		hunk = count - i;
		if (hunk >= n)
			hunk = n;
		result = nfs_proc_write(NFS_SERVER(inode), NFS_FH(inode), 
			pos, hunk, buf, &fattr);
		if (result < 0)
			return result;
		pos += hunk;
		buf += hunk;
		if (hunk < n) {
			i += hunk;
			break;
		}
	}
	file->f_pos = pos;
	nfs_refresh_inode(inode, &fattr);
	return i;
}

