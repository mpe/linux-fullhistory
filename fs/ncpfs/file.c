/*
 *  file.c
 *
 *  Copyright (C) 1995, 1996 by Volker Lendecke
 *
 */

#include <asm/segment.h>
#include <asm/system.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/mm.h>
#include <linux/ncp_fs.h>
#include <linux/locks.h>
#include "ncplib_kernel.h"
#include <linux/malloc.h>

static inline int min(int a, int b)
{
	return a<b ? a : b;
}

static int 
ncp_fsync(struct inode *inode, struct file *file)
{
	return 0;
}

int
ncp_make_open(struct inode *i, int right)
{
        struct nw_file_info *finfo;

        if (i == NULL)
	{
                printk("ncp_make_open: got NULL inode\n");
                return -EINVAL;
        }

        finfo = NCP_FINFO(i);

        DPRINTK("ncp_make_open: dirent->opened = %d\n", finfo->opened);

	lock_super(i->i_sb);
        if (finfo->opened == 0)
	{
		finfo->access = -1;
                /* tries max. rights */
		if (ncp_open_create_file_or_subdir(NCP_SERVER(i),
						   NULL, NULL,
						   OC_MODE_OPEN, 0,
						   AR_READ | AR_WRITE,
						   finfo) == 0)
		{
			finfo->access = O_RDWR;
		}
		else if (ncp_open_create_file_or_subdir(NCP_SERVER(i),
							NULL, NULL,
							OC_MODE_OPEN, 0,
							AR_READ,
							finfo) == 0)
		{
			finfo->access = O_RDONLY;
		}
        }

	unlock_super(i->i_sb);

        if (   ((right == O_RDONLY) && (   (finfo->access == O_RDONLY)
                                        || (finfo->access == O_RDWR)))
            || ((right == O_WRONLY) && (   (finfo->access == O_WRONLY)
                                        || (finfo->access == O_RDWR)))
            || ((right == O_RDWR)   && (finfo->access == O_RDWR)))
                return 0;

        return -EACCES;
}

static int 
ncp_file_read(struct inode *inode, struct file *file, char *buf, int count)
{
	int bufsize, already_read;
	off_t pos;
        int errno;

        DPRINTK("ncp_file_read: enter %s\n", NCP_ISTRUCT(inode)->entryName);
        
	if (inode == NULL)
	{
		DPRINTK("ncp_file_read: inode = NULL\n");
		return -EINVAL;
	}
	if (!ncp_conn_valid(NCP_SERVER(inode)))
	{
		return -EIO;
	}

	if (!S_ISREG(inode->i_mode))
	{
		DPRINTK("ncp_file_read: read from non-file, mode %07o\n",
                        inode->i_mode);
		return -EINVAL;
	}

	pos = file->f_pos;

	if (pos + count > inode->i_size)
	{
		count = inode->i_size - pos;
	}

	if (count <= 0)
	{
		return 0;
	}

        if ((errno = ncp_make_open(inode, O_RDONLY)) != 0)
	{
                return errno;
	}
        
	bufsize = NCP_SERVER(inode)->buffer_size;

        already_read = 0;

	/* First read in as much as possible for each bufsize. */
        while (already_read < count)
	{
		int read_this_time;
		int to_read = min(bufsize - (pos % bufsize),
				  count - already_read);

		if (ncp_read(NCP_SERVER(inode), NCP_FINFO(inode)->file_handle,
			     pos, to_read, buf, &read_this_time) != 0)
		{
			return -EIO; /* This is not exact, i know.. */
		}

		pos += read_this_time;
		buf += read_this_time;
                already_read += read_this_time;

		if (read_this_time < to_read)
		{
                        break;
		}
	}

        file->f_pos = pos;

	if (!IS_RDONLY(inode))
	{
		inode->i_atime = CURRENT_TIME;
	}

	inode->i_dirt = 1;

        DPRINTK("ncp_file_read: exit %s\n", NCP_ISTRUCT(inode)->entryName);

        return already_read;
}

static int 
ncp_file_write(struct inode *inode, struct file *file, const char *buf,
	       int count)
{
	int bufsize, already_written;
        off_t pos;
        int errno;
			  
	if (inode == NULL)
	{
		DPRINTK("ncp_file_write: inode = NULL\n");
		return -EINVAL;
	}
	if (!ncp_conn_valid(NCP_SERVER(inode)))
	{
		return -EIO;
	}

	if (!S_ISREG(inode->i_mode))
	{
		DPRINTK("ncp_file_write: write to non-file, mode %07o\n",
                       inode->i_mode);
		return -EINVAL;
	}

        DPRINTK("ncp_file_write: enter %s\n", NCP_ISTRUCT(inode)->entryName);

	if (count <= 0)
	{
		return 0;
	}

        if ((errno = ncp_make_open(inode, O_RDWR)) != 0)
	{
                return errno;
	}
        
	pos = file->f_pos;

	if (file->f_flags & O_APPEND)
	{
		pos = inode->i_size;
	}

	bufsize = NCP_SERVER(inode)->buffer_size;

        already_written = 0;

        while (already_written < count)
	{
		int written_this_time;
		int to_write = min(bufsize - (pos % bufsize),
				   count - already_written);

		if (ncp_write(NCP_SERVER(inode), NCP_FINFO(inode)->file_handle,
			      pos, to_write, buf, &written_this_time) != 0)
		{
			return -EIO;
                }

		pos += written_this_time;
		buf += written_this_time;
		already_written += written_this_time;

		if (written_this_time < to_write)
		{
			break;
		}
	}

	inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	inode->i_dirt = 1;

	file->f_pos = pos;

        if (pos > inode->i_size)
	{
                inode->i_size = pos;
		ncp_invalid_dir_cache(NCP_INOP(inode)->dir->inode);
        }

        DPRINTK("ncp_file_write: exit %s\n", NCP_ISTRUCT(inode)->entryName);

	return already_written;
}

static struct file_operations ncp_file_operations = {
	NULL,			/* lseek - default */
	ncp_file_read,		/* read */
	ncp_file_write,		/* write */
	NULL,			/* readdir - bad */
	NULL,			/* select - default */
	ncp_ioctl,		/* ioctl */
	ncp_mmap,		/* mmap */
	NULL,                   /* open */
	NULL,                   /* release */
	ncp_fsync,		/* fsync */
};

struct inode_operations ncp_file_inode_operations = {
	&ncp_file_operations,	/* default file operations */
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
