/*
 * truncate.c
 *
 * PURPOSE
 *	Truncate handling routines for the OSTA-UDF(tm) filesystem.
 *
 * CONTACTS
 *	E-mail regarding any portion of the Linux UDF file system should be
 *	directed to the development team mailing list (run by majordomo):
 *		linux_udf@hootie.lvld.hp.com
 *
 * COPYRIGHT
 *	This file is distributed under the terms of the GNU General Public
 *	License (GPL). Copies of the GPL can be obtained from:
 *		ftp://prep.ai.mit.edu/pub/gnu/GPL
 *	Each contributing author retains all rights to their own work.
 *
 *  (C) 1999 Ben Fennema
 *  (C) 1999 Stelias Computing Inc
 *
 * HISTORY
 *
 *  02/24/99 blf  Created.
 *
 */

#include "udfdecl.h"
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/udf_fs.h>

#include "udf_i.h"
#include "udf_sb.h"

static void extent_trunc(struct inode * inode, lb_addr bloc, int *extoffset,
	lb_addr eloc, Uint32 elen, struct buffer_head **bh, Uint32 offset)
{
	lb_addr neloc = { 0, 0 };
	int nelen = 0;
	int blocks = inode->i_sb->s_blocksize / 512;
	int last_block = (elen + inode->i_sb->s_blocksize - 1) >> inode->i_sb->s_blocksize_bits;

	if (offset)
	{
		nelen = ((offset - 1) << inode->i_sb->s_blocksize_bits) +
			(inode->i_size & (inode->i_sb->s_blocksize - 1));
		neloc = eloc;
	}

	inode->i_blocks -= (blocks * (last_block - offset));
	udf_write_aext(inode, bloc, extoffset, neloc, nelen, bh, 1);
	if (!memcmp(&UDF_I_EXT0LOC(inode), &eloc, sizeof(lb_addr)))
	{
		UDF_I_EXT0LOC(inode) = neloc;
		UDF_I_EXT0LEN(inode) = nelen;
	}
	mark_inode_dirty(inode);
	udf_free_blocks(inode, eloc, offset, last_block - offset);
}

static void trunc(struct inode * inode)
{
	lb_addr bloc, eloc, neloc = { 0, 0 };
	Uint32 extoffset, elen, offset, nelen = 0, lelen = 0, lenalloc;
	int etype;
	int first_block = (inode->i_size + inode->i_sb->s_blocksize - 1) >> inode->i_sb->s_blocksize_bits;
	struct buffer_head *bh = NULL;
	int adsize;

	if (UDF_I_ALLOCTYPE(inode) == ICB_FLAG_AD_SHORT)
		adsize = sizeof(short_ad);
	else if (UDF_I_ALLOCTYPE(inode) == ICB_FLAG_AD_LONG)
		adsize = sizeof(long_ad);
	else
		adsize = 0;

	if ((etype = inode_bmap(inode, first_block, &bloc, &extoffset, &eloc, &elen, &offset, &bh)) != -1)
	{
		extoffset -= adsize;
		extent_trunc(inode, bloc, &extoffset, eloc, elen, &bh, offset);

		if (offset)
			lenalloc = extoffset;
		else
			lenalloc = extoffset - adsize;

		if (!memcmp(&UDF_I_LOCATION(inode), &bloc, sizeof(lb_addr)))
			lenalloc -= udf_file_entry_alloc_offset(inode);
		else
			lenalloc -= sizeof(struct AllocExtDesc);

		while ((etype = udf_current_aext(inode, &bloc, &extoffset, &eloc, &elen, &bh, 0)) != -1)
		{
			if (etype == EXTENT_NEXT_EXTENT_ALLOCDECS)
			{
				udf_write_aext(inode, bloc, &extoffset, neloc, nelen, &bh, 0);
				extoffset = 0;
				if (lelen)
				{
					if (!memcmp(&UDF_I_LOCATION(inode), &bloc, sizeof(lb_addr)))
						memset(bh->b_data, 0x00, udf_file_entry_alloc_offset(inode));
					else
						memset(bh->b_data, 0x00, sizeof(struct AllocExtDesc));
					udf_free_blocks(inode, bloc, 0, lelen);
				}
				else
				{
					if (!memcmp(&UDF_I_LOCATION(inode), &bloc, sizeof(lb_addr)))
						UDF_I_LENALLOC(inode) = lenalloc;
					else
					{
						struct AllocExtDesc *aed = (struct AllocExtDesc *)(bh->b_data);
						aed->lengthAllocDescs = cpu_to_le32(lenalloc);
					}
				}

				udf_release_data(bh);
				bh = NULL;

				bloc = eloc;
				if (elen)
					lelen = (elen + inode->i_sb->s_blocksize - 1) >>
						inode->i_sb->s_blocksize_bits;
				else
					lelen = 1;
			}
			else if (etype != EXTENT_NOT_RECORDED_NOT_ALLOCATED)
				extent_trunc(inode, bloc, &extoffset, eloc, elen, &bh, 0);
			else
				udf_write_aext(inode, bloc, &extoffset, neloc, nelen, &bh, 1);
		}

		if (lelen)
		{
			if (!memcmp(&UDF_I_LOCATION(inode), &bloc, sizeof(lb_addr)))
				memset(bh->b_data, 0x00, udf_file_entry_alloc_offset(inode));
			else
				memset(bh->b_data, 0x00, sizeof(struct AllocExtDesc));
			udf_free_blocks(inode, bloc, 0, lelen);
		}
		else
		{
			if (!memcmp(&UDF_I_LOCATION(inode), &bloc, sizeof(lb_addr)))
				UDF_I_LENALLOC(inode) = lenalloc;
			else
			{
				struct AllocExtDesc *aed = (struct AllocExtDesc *)(bh->b_data);
				aed->lengthAllocDescs = cpu_to_le32(lenalloc);
			}
		}
	}
	else if (inode->i_size)
	{
		lb_addr e0loc = UDF_I_LOCATION(inode);
		Uint32 ext0offset = udf_file_entry_alloc_offset(inode);
		char tetype;

		if (offset)
		{
			extoffset -= adsize;
			tetype = udf_next_aext(inode, &bloc, &extoffset, &eloc, &elen, &bh, 1);
			if (tetype == EXTENT_NOT_RECORDED_NOT_ALLOCATED)
			{
				extoffset -= adsize;
				elen = (EXTENT_NOT_RECORDED_NOT_ALLOCATED << 30) |
					(elen + (offset << inode->i_sb->s_blocksize_bits));
				if (ext0offset == extoffset && !memcmp(&e0loc, &bloc, sizeof(lb_addr)))
					UDF_I_EXT0LEN(inode) = elen;
				udf_write_aext(inode, bloc, &extoffset, eloc, elen, &bh, 0);
			}
			else
			{
				if (elen & (inode->i_sb->s_blocksize - 1))
				{
					extoffset -= adsize;
					elen = (EXTENT_RECORDED_ALLOCATED << 30) |
						((elen + inode->i_sb->s_blocksize - 1) &
						~(inode->i_sb->s_blocksize - 1));
					if (ext0offset == extoffset && !memcmp(&e0loc, &bloc, sizeof(lb_addr)))
						UDF_I_EXT0LEN(inode) = elen;
					udf_write_aext(inode, bloc, &extoffset, eloc, elen, &bh, 1);
				}
				memset(&eloc, 0x00, sizeof(lb_addr));
				elen = (EXTENT_NOT_RECORDED_NOT_ALLOCATED << 30) |
					(offset << inode->i_sb->s_blocksize_bits);
				if (ext0offset == extoffset && !memcmp(&e0loc, &bloc, sizeof(lb_addr)))
				{
					UDF_I_EXT0LOC(inode) = eloc;
					UDF_I_EXT0LEN(inode) = elen;
				}
				udf_add_aext(inode, &bloc, &extoffset, eloc, elen, &bh, 1);
			}
		}
	}

	udf_release_data(bh);
}

void udf_truncate(struct inode * inode)
{
	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
			S_ISLNK(inode->i_mode)))
		return;
	if (IS_APPEND(inode) || IS_IMMUTABLE(inode))
		return;

	if (!UDF_I_EXT0OFFS(inode))
	{
		udf_discard_prealloc(inode);

		trunc(inode);
	}

	inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	mark_inode_dirty(inode);
}

void udf_truncate_adinicb(struct inode * inode)
{
	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
			S_ISLNK(inode->i_mode)))
		return;
	if (IS_APPEND(inode) || IS_IMMUTABLE(inode))
		return;

	inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	mark_inode_dirty(inode);
}
