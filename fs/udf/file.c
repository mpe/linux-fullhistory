/*
 * file.c
 *
 * PURPOSE
 *  File handling routines for the OSTA-UDF(tm) filesystem.
 *
 * CONTACTS
 *  E-mail regarding any portion of the Linux UDF file system should be
 *  directed to the development team mailing list (run by majordomo):
 *    linux_udf@hootie.lvld.hp.com
 *
 * COPYRIGHT
 *  This file is distributed under the terms of the GNU General Public
 *  License (GPL). Copies of the GPL can be obtained from:
 *    ftp://prep.ai.mit.edu/pub/gnu/GPL
 *  Each contributing author retains all rights to their own work.
 *
 *  (C) 1998-1999 Dave Boynton
 *  (C) 1998-1999 Ben Fennema
 *  (C) 1999 Stelias Computing Inc
 *
 * HISTORY
 *
 *  10/02/98 dgb  Attempt to integrate into udf.o
 *  10/07/98      Switched to using generic_readpage, etc., like isofs
 *                And it works!
 *  12/06/98 blf  Added udf_file_read. uses generic_file_read for all cases but
 *                ICB_FLAG_AD_IN_ICB.
 *  04/06/99      64 bit file handling on 32 bit systems taken from ext2 file.c
 *  05/12/99      Preliminary file write support
 */

#include "udfdecl.h"
#include <linux/fs.h>
#include <linux/udf_fs.h>
#include <asm/uaccess.h>
#include <linux/kernel.h>
#include <linux/string.h> /* memset */
#include <linux/errno.h>
#include <linux/locks.h>

#include "udf_i.h"
#include "udf_sb.h"

#define NBUF	32

typedef void * poll_table; 

static long long udf_file_llseek(struct file *, long long, int);
static ssize_t udf_file_read_adinicb (struct file *, char *, size_t, loff_t *);
static ssize_t udf_file_write (struct file *, const char *, size_t, loff_t *);
#if BITS_PER_LONG < 64
static int udf_open_file(struct inode *, struct file *);
#endif
static int udf_release_file(struct inode *, struct file *);

static struct file_operations udf_file_operations = {
	udf_file_llseek,	/* llseek */
	generic_file_read,	/* read */
	udf_file_write,		/* write */
	NULL,				/* readdir */
	NULL,				/* poll */
	udf_ioctl,			/* ioctl */
	generic_file_mmap,	/* mmap */
#if BITS_PER_LONG == 64
	NULL, 				/* open */
#else
	udf_open_file,		/* open */
#endif
	NULL,				/* flush */
	udf_release_file,	/* release */
	udf_sync_file,		/* fsync */
	NULL,				/* fasync */
	NULL,				/* check_media_change */
	NULL,				/* revalidate */
	NULL				/* lock */
};

struct inode_operations udf_file_inode_operations = {
	&udf_file_operations,
	NULL,					/* create */
	NULL,					/* lookup */
	NULL,					/* link */
	NULL,					/* unlink */
	NULL,					/* symlink */
	NULL,					/* mkdir */
	NULL,					/* rmdir */
	NULL,					/* mknod */
	NULL,					/* rename */
	NULL,					/* readlink */
	NULL,					/* follow_link */
	udf_get_block,			/* get_block */
	block_read_full_page,	/* readpage */
	block_write_full_page,	/* writepage */
	block_flushpage,		/* flushpage */
#ifdef CONFIG_UDF_RW
	udf_truncate,			/* truncate */
#else
	NULL,					/* truncate */
#endif
	NULL,					/* permission */
	NULL,					/* smap */
	NULL					/* revalidate */
};

static struct file_operations udf_file_operations_adinicb = {
	udf_file_llseek,	/* llseek */
	udf_file_read_adinicb,/* read */
	udf_file_write,		/* write */
	NULL,				/* readdir */
	NULL,				/* poll */
	udf_ioctl,			/* ioctl */
	NULL,				/* mmap */
	NULL, 				/* open */
	NULL,				/* flush */
	udf_release_file,	/* release */
	udf_sync_file,		/* fsync */
	NULL,				/* fasync */
	NULL,				/* check_media_change */
	NULL,				/* revalidate */
	NULL				/* lock */
};

struct inode_operations udf_file_inode_operations_adinicb = {
	&udf_file_operations_adinicb,
	NULL,					/* create */
	NULL,					/* lookup */
	NULL,					/* link */
	NULL,					/* unlink */
	NULL,					/* symlink */
	NULL,					/* mkdir */
	NULL,					/* rmdir */
	NULL,					/* mknod */
	NULL,					/* rename */
	NULL,					/* readlink */
	NULL,					/* follow_link */
	udf_get_block,			/* get_block */
	block_read_full_page,	/* readpage */
	block_write_full_page,	/* writepage */
	block_flushpage,		/* flushpage */
#ifdef CONFIG_UDF_RW
	udf_truncate,			/* truncate */
#else
	NULL,					/* truncate */
#endif
	NULL,					/* permission */
	NULL,					/* smap */
	NULL					/* revalidate */
};

/*
 * Make sure the offset never goes beyond the 32-bit mark..
 */
static long long udf_file_llseek(struct file * file, long long offset, int origin)
{
	struct inode * inode = file->f_dentry->d_inode;

	switch (origin)
	{
		case 2:
		{
			offset += inode->i_size;
			break;
		}
		case 1:
		{
			offset += file->f_pos;
			break;
		}
	}
	if (((unsigned long long) offset >> 32) != 0)
	{
#if BITS_PER_LONG < 64
		return -EINVAL;
#else
		if (offset > ???)
			return -EINVAL;
#endif
	}
	if (offset != file->f_pos)
	{
		file->f_pos = offset;
		file->f_reada = 0;
		file->f_version = ++event;
	}
	return offset;
}

static inline void remove_suid(struct inode * inode)
{
	unsigned int mode;

	/* set S_IGID if S_IXGRP is set, and always set S_ISUID */
	mode = (inode->i_mode & S_IXGRP)*(S_ISGID/S_IXGRP) | S_ISUID;

	/* was any of the uid bits set? */
	mode &= inode->i_mode;
	if (mode && !capable(CAP_FSETID))
	{
		inode->i_mode &= ~mode;
		mark_inode_dirty(inode);
	}
}

static ssize_t udf_file_write(struct file * file, const char * buf,
	size_t count, loff_t *ppos)
{
	ssize_t retval;
	struct inode *inode = file->f_dentry->d_inode;
	remove_suid(inode);

	if (UDF_I_ALLOCTYPE(inode) == ICB_FLAG_AD_IN_ICB)
	{
		int i, err;
		struct buffer_head *bh;

		if ((bh = udf_expand_adinicb(inode, &i, 0, &err)))
			udf_release_data(bh);
	}

	retval = generic_file_write(file, buf, count, ppos, block_write_partial_page);

	if (retval > 0)
	{
		inode->i_ctime = inode->i_mtime = CURRENT_TIME;
		UDF_I_UCTIME(inode) = UDF_I_UMTIME(inode) = CURRENT_UTIME;
	}
	mark_inode_dirty(inode);
	return retval;
}

/*
 * udf_file_read
 *
 * PURPOSE
 *	Read from an open file.
 *
 * DESCRIPTION
 *	Optional - sys_read() will return -EINVAL if this routine is not
 *	available.
 *
 *	Refer to sys_read() in fs/read_write.c
 *	sys_read() -> .
 *
 *	Note that you can use generic_file_read() instead, which requires that
 *	udf_readpage() be available, but you can use generic_readpage(), which
 *	requires that udf_block_map() be available. Reading will then be done by
 *	memory-mapping the file a page at a time. This is not suitable for
 *	devices that don't handle read-ahead [example: CD-R/RW that may have
 *	blank sectors that shouldn't be read].
 *
 *	Refer to generic_file_read() in mm/filemap.c and to generic_readpage()
 *	in fs/buffer.c
 *
 *	Block devices can use block_read() instead. Refer to fs/block_dev.c
 *
 * PRE-CONDITIONS
 *	inode			Pointer to inode to read from (never NULL).
 *	filp			Pointer to file to read from (never NULL).
 *	buf			Point to read buffer (validated).
 *	bufsize			Size of read buffer.
 *
 * POST-CONDITIONS
 *	<return>		Bytes read (>=0) or an error code (<0) that
 *				sys_read() will return.
 *
 * HISTORY
 *	July 1, 1997 - Andrew E. Mileski
 *	Written, tested, and released.
 */
static ssize_t udf_file_read_adinicb(struct file * filp, char * buf,
	size_t bufsize, loff_t * loff)
{
	struct inode *inode = filp->f_dentry->d_inode;
	Uint32 size, left, pos, block;
	struct buffer_head *bh = NULL;

	size = inode->i_size;
	if (*loff > size)
		left = 0;
	else
		left = size - *loff;
	if (left > bufsize)
		left = bufsize;

	if (left <= 0)
		return 0;

	pos = *loff + UDF_I_EXT0OFFS(inode);
	block = udf_block_map(inode, 0);
	if (!(bh = udf_tread(inode->i_sb,
		udf_get_lb_pblock(inode->i_sb, UDF_I_LOCATION(inode), 0),
		inode->i_sb->s_blocksize)))
	{
		return 0;
	}
	if (!copy_to_user(buf, bh->b_data + pos, left))
		*loff += left;
	else
		return -EFAULT;

	return left;
}

/*
 * udf_ioctl
 *
 * PURPOSE
 *	Issue an ioctl.
 *
 * DESCRIPTION
 *	Optional - sys_ioctl() will return -ENOTTY if this routine is not
 *	available, and the ioctl cannot be handled without filesystem help.
 *
 *	sys_ioctl() handles these ioctls that apply only to regular files:
 *		FIBMAP [requires udf_block_map()], FIGETBSZ, FIONREAD
 *	These ioctls are also handled by sys_ioctl():
 *		FIOCLEX, FIONCLEX, FIONBIO, FIOASYNC
 *	All other ioctls are passed to the filesystem.
 *
 *	Refer to sys_ioctl() in fs/ioctl.c
 *	sys_ioctl() -> .
 *
 * PRE-CONDITIONS
 *	inode			Pointer to inode that ioctl was issued on.
 *	filp			Pointer to file that ioctl was issued on.
 *	cmd			The ioctl command.
 *	arg			The ioctl argument [can be interpreted as a
 *				user-space pointer if desired].
 *
 * POST-CONDITIONS
 *	<return>		Success (>=0) or an error code (<=0) that
 *				sys_ioctl() will return.
 *
 * HISTORY
 *	July 1, 1997 - Andrew E. Mileski
 *	Written, tested, and released.
 */
int udf_ioctl(struct inode *inode, struct file *filp, unsigned int cmd,
	unsigned long arg)
{
	int result=-1;
	int size;
	struct buffer_head *bh = NULL;
	struct FileEntry *fe;
	Uint16 ident;

	if ( permission(inode, MAY_READ) != 0 )
	{
		udf_debug("no permission to access inode %lu\n",
						inode->i_ino);
		return -EPERM;
	}

	if ( !arg )
	{
		udf_debug("invalid argument to udf_ioctl\n");
		return -EINVAL;
	}

	/* first, do ioctls that don't need to udf_read */
	switch (cmd)
	{
		case UDF_GETVOLIDENT:
			if ( (result == verify_area(VERIFY_WRITE, (char *)arg, 32)) == 0)
				result = copy_to_user((char *)arg, UDF_SB_VOLIDENT(inode->i_sb), 32);
			return result;

	}

	/* ok, we need to read the inode */
	bh = udf_read_ptagged(inode->i_sb, UDF_I_LOCATION(inode), 0, &ident);

	if (!bh || ident != TID_FILE_ENTRY)
	{
		udf_debug("bread failed (ino=%ld) or ident (%d) != TID_FILE_ENTRY",
			inode->i_ino, ident);
		return -EFAULT;
	}

	fe = (struct FileEntry *)bh->b_data;
	size = le32_to_cpu(fe->lengthExtendedAttr);

	switch (cmd) 
	{
		case UDF_GETEASIZE:
			if ( (result = verify_area(VERIFY_WRITE, (char *)arg, 4)) == 0) 
				result= put_user(size, (int *)arg);
			break;

		case UDF_GETEABLOCK:
			if ( (result = verify_area(VERIFY_WRITE, (char *)arg, size)) == 0) 
				result= copy_to_user((char *)arg, fe->extendedAttr, size);
			break;

		default:
			udf_debug("ino=%ld, cmd=%d\n", inode->i_ino, cmd);
			break;
	}

	udf_release_data(bh);
	return result;
}

/*
 * udf_release_file
 *
 * PURPOSE
 *  Called when all references to the file are closed
 *
 * DESCRIPTION
 *  Discard prealloced blocks
 *
 * HISTORY
 *
 */
static int udf_release_file(struct inode * inode, struct file * filp)
{
	if (filp->f_mode & FMODE_WRITE)
		udf_discard_prealloc(inode);
	return 0;
}

#if BITS_PER_LONG < 64
/*
 * udf_open_file
 *
 * PURPOSE
 *  Called when an inode is about to be open.
 *
 * DESCRIPTION
 *  Use this to disallow opening RW large files on 32 bit systems.
 *
 * HISTORY
 *
 */
static int udf_open_file(struct inode * inode, struct file * filp)
{
	if (inode->i_size == (Uint32)-1 && (filp->f_mode & FMODE_WRITE))
		return -EFBIG;
	return 0;
}
#endif
