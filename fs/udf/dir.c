/*
 * dir.c
 *
 * PURPOSE
 *  Directory handling routines for the OSTA-UDF(tm) filesystem.
 *
 * CONTACTS
 *	E-mail regarding any portion of the Linux UDF file system should be
 *	directed to the development team mailing list (run by majordomo):
 *		linux_udf@hpesjro.fc.hp.com
 *
 * COPYRIGHT
 *	This file is distributed under the terms of the GNU General Public
 *	License (GPL). Copies of the GPL can be obtained from:
 *		ftp://prep.ai.mit.edu/pub/gnu/GPL
 *	Each contributing author retains all rights to their own work.
 *
 *  (C) 1998-2000 Ben Fennema
 *
 * HISTORY
 *
 *  10/05/98 dgb  Split directory operations into it's own file
 *                Implemented directory reads via do_udf_readdir
 *  10/06/98      Made directory operations work!
 *  11/17/98      Rewrote directory to support ICB_FLAG_AD_LONG
 *  11/25/98 blf  Rewrote directory handling (readdir+lookup) to support reading
 *                across blocks.
 *  12/12/98      Split out the lookup code to namei.c. bulk of directory
 *                code now in directory.c:udf_fileident_read.
 */

#include "udfdecl.h"

#if defined(__linux__) && defined(__KERNEL__)
#include <linux/version.h>
#include "udf_i.h"
#include "udf_sb.h"
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/udf_fs.h>
#endif

/* Prototypes for file operations */
static int udf_readdir(struct file *, void *, filldir_t);
static int do_udf_readdir(struct inode *, struct file *, filldir_t, void *);

/* readdir and lookup functions */

struct file_operations udf_dir_operations = {
	read:			generic_read_dir,
	readdir:		udf_readdir,
	ioctl:			udf_ioctl,
	fsync:			udf_fsync_file,
};

/*
 * udf_readdir
 *
 * PURPOSE
 *	Read a directory entry.
 *
 * DESCRIPTION
 *	Optional - sys_getdents() will return -ENOTDIR if this routine is not
 *	available.
 *
 *	Refer to sys_getdents() in fs/readdir.c
 *	sys_getdents() -> .
 *
 * PRE-CONDITIONS
 *	filp			Pointer to directory file.
 *	buf			Pointer to directory entry buffer.
 *	filldir			Pointer to filldir function.
 *
 * POST-CONDITIONS
 *	<return>		>=0 on success.
 *
 * HISTORY
 *	July 1, 1997 - Andrew E. Mileski
 *	Written, tested, and released.
 */

int udf_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct inode *dir = filp->f_dentry->d_inode;
	int result;

	if ( filp->f_pos == 0 ) 
	{
		if (filldir(dirent, ".", 1, filp->f_pos, dir->i_ino, DT_DIR) < 0)
			return 0;
		filp->f_pos ++;
	}
 
	result = do_udf_readdir(dir, filp, filldir, dirent);
	UPDATE_ATIME(dir);
 	return result;
}

static int 
do_udf_readdir(struct inode * dir, struct file *filp, filldir_t filldir, void *dirent)
{
	struct udf_fileident_bh fibh;
	struct FileIdentDesc *fi=NULL;
	struct FileIdentDesc cfi;
	int block, iblock;
	loff_t nf_pos = filp->f_pos - 1;
	int flen;
	char fname[255];
	char *nameptr;
	Uint16 liu;
	Uint8 lfi;
	loff_t size = (udf_ext0_offset(dir) + dir->i_size) >> 2;
	struct buffer_head * bh = NULL, * tmp, * bha[16];
	lb_addr bloc, eloc;
	Uint32 extoffset, elen, offset;
	int i, num;
	unsigned int dt_type;

	if (nf_pos >= size)
		return 0;

	if (nf_pos == 0)
		nf_pos = (udf_ext0_offset(dir) >> 2);

	fibh.soffset = fibh.eoffset = (nf_pos & ((dir->i_sb->s_blocksize - 1) >> 2)) << 2;
	if (inode_bmap(dir, nf_pos >> (dir->i_sb->s_blocksize_bits - 2),
		&bloc, &extoffset, &eloc, &elen, &offset, &bh) == EXTENT_RECORDED_ALLOCATED)
	{
		offset >>= dir->i_sb->s_blocksize_bits;
		block = udf_get_lb_pblock(dir->i_sb, eloc, offset);
		if ((++offset << dir->i_sb->s_blocksize_bits) < elen)
		{
			if (UDF_I_ALLOCTYPE(dir) == ICB_FLAG_AD_SHORT)
				extoffset -= sizeof(short_ad);
			else if (UDF_I_ALLOCTYPE(dir) == ICB_FLAG_AD_LONG)
				extoffset -= sizeof(long_ad);
		}
		else
			offset = 0;
	}
	else
	{
		udf_release_data(bh);
		return -ENOENT;
	}

	if (!(fibh.sbh = fibh.ebh = udf_tread(dir->i_sb, block)))
	{
		udf_release_data(bh);
		return -EIO;
	}

	if (!(offset & ((16 >> (dir->i_sb->s_blocksize_bits - 9))-1)))
	{
		i = 16 >> (dir->i_sb->s_blocksize_bits - 9);
		if (i+offset > (elen >> dir->i_sb->s_blocksize_bits))
			i = (elen >> dir->i_sb->s_blocksize_bits)-offset;
		for (num=0; i>0; i--)
		{
			block = udf_get_lb_pblock(dir->i_sb, eloc, offset+i);
			tmp = udf_tgetblk(dir->i_sb, block);
			if (tmp && !buffer_uptodate(tmp) && !buffer_locked(tmp))
				bha[num++] = tmp;
			else
				brelse(tmp);
		}
		if (num)
		{
			ll_rw_block(READA, num, bha);
			for (i=0; i<num; i++)
				brelse(bha[i]);
		}
	}

	while ( nf_pos < size )
	{
		filp->f_pos = nf_pos + 1;

		fi = udf_fileident_read(dir, &nf_pos, &fibh, &cfi, &bloc, &extoffset, &eloc, &elen, &offset, &bh);

		if (!fi)
		{
			if (fibh.sbh != fibh.ebh)
				udf_release_data(fibh.ebh);
			udf_release_data(fibh.sbh);
			udf_release_data(bh);
			return 0;
		}

		liu = le16_to_cpu(cfi.lengthOfImpUse);
		lfi = cfi.lengthFileIdent;

		if (fibh.sbh == fibh.ebh)
			nameptr = fi->fileIdent + liu;
		else
		{
			int poffset;	/* Unpaded ending offset */

			poffset = fibh.soffset + sizeof(struct FileIdentDesc) + liu + lfi;

			if (poffset >= lfi)
				nameptr = (char *)(fibh.ebh->b_data + poffset - lfi);
			else
			{
				nameptr = fname;
				memcpy(nameptr, fi->fileIdent + liu, lfi - poffset);
				memcpy(nameptr + lfi - poffset, fibh.ebh->b_data, poffset);
			}
		}

		if ( (cfi.fileCharacteristics & FILE_DELETED) != 0 )
		{
			if ( !UDF_QUERY_FLAG(dir->i_sb, UDF_FLAG_UNDELETE) )
				continue;
		}
		
		if ( (cfi.fileCharacteristics & FILE_HIDDEN) != 0 )
		{
			if ( !UDF_QUERY_FLAG(dir->i_sb, UDF_FLAG_UNHIDE) )
				continue;
		}

		if ( cfi.fileCharacteristics & FILE_PARENT )
		{
			iblock = udf_get_lb_pblock(dir->i_sb, UDF_I_LOCATION(filp->f_dentry->d_parent->d_inode), 0);
			flen = 2;
			memcpy(fname, "..", flen);
			dt_type = DT_DIR;
		}
		else
		{
			iblock = udf_get_lb_pblock(dir->i_sb, lelb_to_cpu(cfi.icb.extLocation), 0);
			flen = udf_get_filename(dir->i_sb, nameptr, fname, lfi);
			dt_type = DT_UNKNOWN;
		}

		if (flen)
		{
			if (filldir(dirent, fname, flen, filp->f_pos, iblock, dt_type) < 0)
			{
				if (fibh.sbh != fibh.ebh)
					udf_release_data(fibh.ebh);
				udf_release_data(fibh.sbh);
				udf_release_data(bh);
	 			return 0;
			}
		}
	} /* end while */

	filp->f_pos = nf_pos + 1;

	if (fibh.sbh != fibh.ebh)
		udf_release_data(fibh.ebh);
	udf_release_data(fibh.sbh);
	udf_release_data(bh);

	return 0;
}
