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
#include <linux/config.h>
#include <linux/fs.h>
#include <linux/udf_fs.h>
#include <asm/uaccess.h>
#include <linux/kernel.h>
#include <linux/string.h> /* memset */
#include <linux/errno.h>
#include <linux/locks.h>

#include "udf_i.h"
#include "udf_sb.h"

/*
 * Make sure the offset never goes beyond the 32-bit mark..
 */
static loff_t udf_file_llseek(struct file * file, loff_t offset, int origin)
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

	retval = generic_file_write(file, buf, count, ppos, block_write_partial_page);

	if (retval > 0)
	{
		remove_suid(inode);
		inode->i_ctime = inode->i_mtime = CURRENT_TIME;
		UDF_I_UCTIME(inode) = UDF_I_UMTIME(inode) = CURRENT_UTIME;
		mark_inode_dirty(inode);
	}
	return retval;
}

int udf_write_partial_page_adinicb(struct file *file, struct page *page, unsigned long offset, unsigned long bytes, const char * buf)
{
	struct inode *inode = file->f_dentry->d_inode;
	int err = 0, block;
	struct buffer_head *bh;
	unsigned long kaddr = 0;

	if (!PageLocked(page))
		BUG();
	if (offset < 0 || offset >= (inode->i_sb->s_blocksize - udf_file_entry_alloc_offset(inode)))
		BUG();
	if (bytes+offset < 0 || bytes+offset > (inode->i_sb->s_blocksize - udf_file_entry_alloc_offset(inode)))
		BUG();

	kaddr = kmap(page);
	block = udf_get_lb_pblock(inode->i_sb, UDF_I_LOCATION(inode), 0);
	bh = getblk (inode->i_dev, block, inode->i_sb->s_blocksize);
	if (!buffer_uptodate(bh))
	{
		ll_rw_block (READ, 1, &bh);
		wait_on_buffer(bh);
	}
	err = copy_from_user((char *)kaddr + offset, buf, bytes);
	memcpy(bh->b_data + udf_file_entry_alloc_offset(inode) + offset,
		(char *)kaddr + offset, bytes);
	mark_buffer_dirty(bh, 0);
	brelse(bh);
	kunmap(page);
	SetPageUptodate(page);
	return bytes;
}

static ssize_t udf_file_write_adinicb(struct file * file, const char * buf,
	size_t count, loff_t *ppos)
{
	ssize_t retval;
	struct inode *inode = file->f_dentry->d_inode;
	int err, pos;

	if (file->f_flags & O_APPEND)
		pos = inode->i_size;
	else
		pos = *ppos;

	if (inode->i_sb->s_blocksize < (udf_file_entry_alloc_offset(inode) +
		pos + count))
	{
		udf_expand_file_adinicb(file, pos + count, &err);
		if (UDF_I_ALLOCTYPE(inode) == ICB_FLAG_AD_IN_ICB)
		{
			udf_debug("udf_expand_adinicb: err=%d\n", err);
			return err;
		}
		else
			return udf_file_write(file, buf, count, ppos);
	}
	else
	{
		if (pos + count > inode->i_size)
			UDF_I_LENALLOC(inode) = pos + count;
		else
			UDF_I_LENALLOC(inode) = inode->i_size;
	}

	retval = generic_file_write(file, buf, count, ppos, udf_write_partial_page_adinicb);

	if (retval > 0)
	{
		remove_suid(inode);
		inode->i_ctime = inode->i_mtime = CURRENT_TIME;
		UDF_I_UCTIME(inode) = UDF_I_UMTIME(inode) = CURRENT_UTIME;
		mark_inode_dirty(inode);
	}
	return retval;
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
	int result = -1;
	struct buffer_head *bh = NULL;
	Uint16 ident;
	long_ad eaicb;
	Uint8 *ea = NULL;

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

	if (!bh || (ident != TID_FILE_ENTRY && ident != TID_EXTENDED_FILE_ENTRY))
	{
		udf_debug("bread failed (ino=%ld) or ident (%d) != TID_(EXTENDED_)FILE_ENTRY",
			inode->i_ino, ident);
		return -EFAULT;
	}

	if (UDF_I_EXTENDED_FE(inode) == 0)
	{
		struct FileEntry *fe;

		fe = (struct FileEntry *)bh->b_data;
		eaicb = fe->extendedAttrICB;
		if (UDF_I_LENEATTR(inode))
			ea = fe->extendedAttr;
	}
	else
	{
		struct ExtendedFileEntry *efe;

		efe = (struct ExtendedFileEntry *)bh->b_data;
		eaicb = efe->extendedAttrICB;
		if (UDF_I_LENEATTR(inode))
			ea = efe->extendedAttr;
	}

	switch (cmd) 
	{
		case UDF_GETEASIZE:
			if ( (result = verify_area(VERIFY_WRITE, (char *)arg, 4)) == 0) 
				result = put_user(UDF_I_LENEATTR(inode), (int *)arg);
			break;

		case UDF_GETEABLOCK:
			if ( (result = verify_area(VERIFY_WRITE, (char *)arg, UDF_I_LENEATTR(inode))) == 0) 
				result = copy_to_user((char *)arg, ea, UDF_I_LENEATTR(inode));
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

/*
 * udf_open_file
 *
 * PURPOSE
 *  Called when an inode is about to be open.
 *
 * DESCRIPTION
 *  Use this to disallow opening RW large files on 32 bit systems.
 *  On 64 bit systems we force on O_LARGEFILE in sys_open.
 *
 * HISTORY
 *
 */
static int udf_open_file(struct inode * inode, struct file * filp)
{
	if ((inode->i_size & 0xFFFFFFFF00000000UL) && !(filp->f_flags & O_LARGEFILE))
		return -EFBIG;
	return 0;
}

static struct file_operations udf_file_operations = {
	llseek:		udf_file_llseek,
	read:		generic_file_read,
	write:		udf_file_write,
	ioctl:		udf_ioctl,
	mmap:		generic_file_mmap,
	open:		udf_open_file,
	release:	udf_release_file,
	fsync:		udf_sync_file,
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
#if CONFIG_UDF_RW == 1
	udf_truncate,			/* truncate */
#else
	NULL,					/* truncate */
#endif
	NULL,					/* permission */
	NULL					/* revalidate */
};

static struct file_operations udf_file_operations_adinicb = {
	llseek:		udf_file_llseek,
	read:		generic_file_read,
	write:		udf_file_write_adinicb,
	ioctl:		udf_ioctl,
	release:	udf_release_file,
	fsync:		udf_sync_file_adinicb,
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
	udf_readpage_adinicb,	/* readpage */
	udf_writepage_adinicb,	/* writepage */
#if CONFIG_UDF_RW == 1
	udf_truncate_adinicb,	/* truncate */
#else
	NULL,					/* truncate */
#endif
	NULL,					/* permission */
	NULL					/* revalidate */
};
