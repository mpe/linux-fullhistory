/*
 * proc/fs/generic.c --- generic routines for the proc-fs
 *
 * This file contains generic proc-fs routines for handling
 * directories and files.
 * 
 * Copyright (C) 1991, 1992 Linus Torvalds.
 * Copyright (C) 1997 Theodore Ts'o
 */

#include <asm/uaccess.h>

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <asm/bitops.h>

extern struct inode_operations proc_dyna_dir_inode_operations;

static ssize_t proc_file_read(struct file * file, char * buf,
			      size_t nbytes, loff_t *ppos);
static ssize_t proc_file_write(struct file * file, const char * buffer,
			       size_t count, loff_t *ppos);
static long long proc_file_lseek(struct file *, long long, int);

int proc_match(int len, const char *name,struct proc_dir_entry * de)
{
	if (!de || !de->low_ino)
		return 0;
	if (de->namelen != len)
		return 0;
	return !memcmp(name, de->name, len);
}

static struct file_operations proc_file_operations = {
    proc_file_lseek,	/* lseek   */
    proc_file_read,	/* read	   */
    proc_file_write,	/* write   */
    NULL,		/* readdir */
    NULL,		/* poll    */
    NULL,		/* ioctl   */
    NULL,		/* mmap	   */
    NULL,		/* no special open code	   */
    NULL,		/* flush */
    NULL,		/* no special release code */
    NULL		/* can't fsync */
};

struct inode_operations proc_file_inode_operations = {
	&proc_file_operations,  /* default proc file-ops */
	NULL,		/* create	*/
	NULL,		/* lookup	*/
    NULL,		/* link		*/
    NULL,		/* unlink	*/
    NULL,		/* symlink	*/
    NULL,		/* mkdir	*/
    NULL,	    /* rmdir	   */
    NULL,	    /* mknod	   */
    NULL,	    /* rename	   */
    NULL,	    /* readlink	   */
    NULL,	    /* follow_link */
    NULL,	    /* readpage	   */
    NULL,	    /* writepage   */
    NULL,	    /* bmap	   */
    NULL,	    /* truncate	   */
    NULL	    /* permission  */
};

/*
 * compatibility to replace fs/proc/net.c
 */
struct inode_operations proc_net_inode_operations = {
	&proc_file_operations,	/* default net file-ops */
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


#ifndef MIN
#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#endif

/* 4K page size but our output routines use some slack for overruns */
#define PROC_BLOCK_SIZE	(3*1024)

static ssize_t
proc_file_read(struct file * file, char * buf, size_t nbytes, loff_t *ppos)
{
	struct inode * inode = file->f_dentry->d_inode;
	char 	*page;
	ssize_t	retval=0;
	int	eof=0;
	ssize_t	n, count;
	char	*start;
	struct proc_dir_entry * dp;

	dp = (struct proc_dir_entry *) inode->u.generic_ip;
	if (!(page = (char*) __get_free_page(GFP_KERNEL)))
		return -ENOMEM;

	while ((nbytes > 0) && !eof)
	{
		count = MIN(PROC_BLOCK_SIZE, nbytes);

		start = NULL;
		if (dp->get_info) {
			/*
			 * Handle backwards compatibility with the old net
			 * routines.
			 * 
			 * XXX What gives with the file->f_flags & O_ACCMODE
			 * test?  Seems stupid to me....
			 */
			n = dp->get_info(page, &start, *ppos, count,
				 (file->f_flags & O_ACCMODE) == O_RDWR);
			if (n < count)
				eof = 1;
		} else if (dp->read_proc) {
			n = dp->read_proc(page, &start, *ppos,
					  count, &eof, dp->data);
		} else
			break;
			
		if (!start) {
			/*
			 * For proc files that are less than 4k
			 */
			start = page + *ppos;
			n -= *ppos;
			if (n <= 0)
				break;
			if (n > count)
				n = count;
		}
		if (n == 0)
			break;	/* End of file */
		if (n < 0) {
			if (retval == 0)
				retval = n;
			break;
		}
		
		/* This is a hack to allow mangling of file pos independent
 		 * of actual bytes read.  Simply place the data at page,
 		 * return the bytes, and set `start' to the desired offset
 		 * as an unsigned int. - Paul.Russell@rustcorp.com.au
		 */
 		n -= copy_to_user(buf, start < page ? page : start, n);
		if (n == 0) {
			if (retval == 0)
				retval = -EFAULT;
			break;
		}

		*ppos += start < page ? (long)start : n; /* Move down the file */
		nbytes -= n;
		buf += n;
		retval += n;
	}
	free_page((unsigned long) page);
	return retval;
}

static ssize_t
proc_file_write(struct file * file, const char * buffer,
		size_t count, loff_t *ppos)
{
	struct inode *inode = file->f_dentry->d_inode;
	struct proc_dir_entry * dp;
	
	dp = (struct proc_dir_entry *) inode->u.generic_ip;

	if (!dp->write_proc)
		return -EIO;

	/* FIXME: does this routine need ppos?  probably... */
	return dp->write_proc(file, buffer, count, dp->data);
}


static long long
proc_file_lseek(struct file * file, long long offset, int orig)
{
    switch (orig) {
    case 0:
	file->f_pos = offset;
	return(file->f_pos);
    case 1:
	file->f_pos += offset;
	return(file->f_pos);
    case 2:
	return(-EINVAL);
    default:
	return(-EINVAL);
    }
}

/*
 * This function parses a name such as "tty/driver/serial", and
 * returns the struct proc_dir_entry for "/proc/tty/driver", and
 * returns "serial" in residual.
 */
static int xlate_proc_name(const char *name,
			   struct proc_dir_entry **ret, const char **residual)
{
	const char     		*cp = name, *next;
	struct proc_dir_entry	*de;
	int			len;

	de = &proc_root;
	while (1) {
		next = strchr(cp, '/');
		if (!next)
			break;

		len = next - cp;
		for (de = de->subdir; de ; de = de->next) {
			if (proc_match(len, cp, de))
				break;
		}
		if (!de)
			return -ENOENT;
		cp += len + 1;
	}
	*residual = cp;
	*ret = de;
	return 0;
}

struct proc_dir_entry *create_proc_entry(const char *name, mode_t mode,
					 struct proc_dir_entry *parent)
{
	struct proc_dir_entry *ent = NULL;
	const char *fn = name;
	int len;

	if (!parent && xlate_proc_name(name, &parent, &fn) != 0)
		goto out;
	len = strlen(fn);

	ent = kmalloc(sizeof(struct proc_dir_entry) + len + 1, GFP_KERNEL);
	if (!ent)
		goto out;
	memset(ent, 0, sizeof(struct proc_dir_entry));
	memcpy(((char *) ent) + sizeof(*ent), fn, len + 1);
	ent->name = ((char *) ent) + sizeof(*ent);
	ent->namelen = len;

	if (S_ISDIR(mode)) {
		if ((mode & S_IALLUGO) == 0)
		mode |= S_IRUGO | S_IXUGO;
		ent->ops = &proc_dyna_dir_inode_operations;
		ent->nlink = 2;
	} else {
		if ((mode & S_IFMT) == 0)
			mode |= S_IFREG;
		if ((mode & S_IALLUGO) == 0)
			mode |= S_IRUGO;
		ent->nlink = 1;
	}
	ent->mode = mode;

	proc_register(parent, ent);
	
out:
	return ent;
}

extern void free_proc_entry(struct proc_dir_entry *);
void free_proc_entry(struct proc_dir_entry *de)
{
	int ino = de->low_ino;

	if (ino >= PROC_DYNAMIC_FIRST &&
	    ino < PROC_DYNAMIC_FIRST+PROC_NDYNAMIC)
		kfree(de);
}

/*
 * Remove a /proc entry and free it if it's not currently in use.
 * If it is in use, we set the 'deleted' flag.
 */
void remove_proc_entry(const char *name, struct proc_dir_entry *parent)
{
	struct proc_dir_entry *de;
	const char *fn = name;
	int len;

	if (!parent && xlate_proc_name(name, &parent, &fn) != 0)
		goto out;
	len = strlen(fn);

	for (de = parent->subdir; de ; de = de->next) {
		if (proc_match(len, fn, de))
			break;
	}

	if (de) {
		proc_unregister(parent, de->low_ino);
		de->nlink = 0;
		de->deleted = 1;
		if (!de->count)
			free_proc_entry(de);
		else {
			printk("remove_proc_entry: %s/%s busy, count=%d\n",
				parent->name, de->name, de->count);
		}
	}
out:
	return;
}
