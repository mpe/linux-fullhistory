/*
 *  file.c
 *
 *  Copyright (C) 1995 by Paal-Kr. Engstad and Volker Lendecke
 *
 */

#include <linux/config.h>
#ifdef MODULE
#include <linux/module.h>
#include <linux/version.h>
#endif

#include <asm/segment.h>
#include <asm/system.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/mm.h>
#include <linux/smb_fs.h>
#include <linux/malloc.h>

static int 
smb_fsync(struct inode *inode, struct file *file)
{
	return 0;
}

int
smb_make_open(struct inode *i, int right)
{
        struct smb_dirent *dirent;
        int open_result;

        if (i == NULL) {
                printk("smb_make_open: got NULL inode\n");
                return -EINVAL;
        }

        dirent = &(SMB_INOP(i)->finfo);

        DDPRINTK("smb_make_open: dirent->opened = %d\n", dirent->opened);

        if ((dirent->opened) == 0) {
                /* tries max. rights */
                open_result = smb_proc_open(SMB_SERVER(i),
                                            dirent->path, dirent->len,
                                            dirent);
                if (open_result) 
                        return open_result;

                dirent->opened = 1;
        }

        if (   ((right == O_RDONLY) && (   (dirent->access == O_RDONLY)
                                        || (dirent->access == O_RDWR)))
            || ((right == O_WRONLY) && (   (dirent->access == O_WRONLY)
                                        || (dirent->access == O_RDWR)))
            || ((right == O_RDWR)   && (dirent->access == O_RDWR)))
                return 0;

        return -EACCES;
}

static int 
smb_file_read(struct inode *inode, struct file *file, char *buf, int count)
{
	int result, bufsize, to_read, already_read;
	off_t pos;
        int errno;

        DPRINTK("smb_file_read: enter %s\n", SMB_FINFO(inode)->path);
        
	if (!inode) {
		DPRINTK("smb_file_read: inode = NULL\n");
		return -EINVAL;
	}

	if (!S_ISREG(inode->i_mode)) {
		DPRINTK("smb_file_read: read from non-file, mode %07o\n",
                        inode->i_mode);
		return -EINVAL;
	}

        if ((errno = smb_make_open(inode, O_RDONLY)) != 0)
                return errno;
        
	pos = file->f_pos;

	if (pos + count > inode->i_size)
		count = inode->i_size - pos;

	if (count <= 0)
		return 0;
	bufsize = SMB_SERVER(inode)->max_xmit - SMB_HEADER_LEN - 5 * 2 - 5;

        already_read = 0;

	/* First read in as much as possible for each bufsize. */
        while (already_read < count) {

                to_read = min(bufsize, count - already_read);

		result = smb_proc_read(SMB_SERVER(inode), SMB_FINFO(inode),
                                       pos, to_read, buf, 1);
		if (result < 0)
			return result;
		pos += result;
		buf += result;
                already_read += result;

		if (result < to_read) {
                        break;
		}
	}

        file->f_pos = pos;

	if (!IS_RDONLY(inode)) inode->i_atime = CURRENT_TIME;
	inode->i_dirt = 1;

        DPRINTK("smb_file_read: exit %s\n", SMB_FINFO(inode)->path);

        return already_read;
}

static int 
smb_file_write(struct inode *inode, struct file *file, char *buf, int count)
{
	int result, bufsize, to_write, already_written;
        off_t pos;
        int errno;
			  
	if (!inode) {
		DPRINTK("smb_file_write: inode = NULL\n");
		return -EINVAL;
	}

	if (!S_ISREG(inode->i_mode)) {
		DPRINTK("smb_file_write: write to non-file, mode %07o\n",
                       inode->i_mode);
		return -EINVAL;
	}

        DPRINTK("smb_file_write: enter %s\n", SMB_FINFO(inode)->path);

	if (count <= 0)
		return 0;

        if ((errno = smb_make_open(inode, O_RDWR)) != 0)
                return errno;
        
	pos = file->f_pos;

	if (file->f_flags & O_APPEND)
		pos = inode->i_size;

	bufsize = SMB_SERVER(inode)->max_xmit - SMB_HEADER_LEN - 5 * 2 - 5;

        already_written = 0;

        while (already_written < count) {

                to_write = min(bufsize, count - already_written);

		result = smb_proc_write(SMB_SERVER(inode), SMB_FINFO(inode), 
					pos, to_write, buf);

		if (result < 0)
			return result;

		pos += result;
		buf += result;
                already_written += result;

		if (result < to_write) {
			break;
		}
	}

	inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	inode->i_dirt = 1;

	file->f_pos = pos;

        if (pos > inode->i_size) {
                inode->i_size = pos;
        }

        DPRINTK("smb_file_write: exit %s\n", SMB_FINFO(inode)->path);

	return already_written;
}

static struct file_operations smb_file_operations = {
	NULL,			/* lseek - default */
	smb_file_read,		/* read */
	smb_file_write,		/* write */
	NULL,			/* readdir - bad */
	NULL,			/* select - default */
	smb_ioctl,		/* ioctl */
	smb_mmap,               /* mmap */
	NULL,                   /* open */
	NULL,                   /* release */
	smb_fsync,		/* fsync */
};

struct inode_operations smb_file_inode_operations = {
	&smb_file_operations,	/* default file operations */
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

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 8
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -8
 * c-argdecl-indent: 8
 * c-label-offset: -8
 * c-continued-statement-offset: 8
 * c-continued-brace-offset: 0
 * End:
 */
