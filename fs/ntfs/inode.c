/**
 * inode.c - NTFS kernel inode handling. Part of the Linux-NTFS project.
 *
 * Copyright (c) 2001,2002 Anton Altaparmakov.
 *
 * This program/include file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program/include file is distributed in the hope that it will be 
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty 
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the Linux-NTFS 
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation,Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/pagemap.h>
#include <linux/buffer_head.h>

#include "ntfs.h"
#include "dir.h"

struct inode *ntfs_alloc_big_inode(struct super_block *sb)
{
	ntfs_inode *ni;

	ntfs_debug("Entering.");
	ni = (ntfs_inode *)kmem_cache_alloc(ntfs_big_inode_cache,
			SLAB_NOFS);
	if (!ni) {
		ntfs_error(sb, "Allocation of NTFS big inode structure "
				"failed.");
		return NULL;
	}
	return VFS_I(ni);
}

void ntfs_destroy_big_inode(struct inode *inode)
{
	ntfs_inode *ni = NTFS_I(inode);

	ntfs_debug("Entering.");
	BUG_ON(atomic_read(&ni->mft_count) || !atomic_dec_and_test(&ni->count));
	kmem_cache_free(ntfs_big_inode_cache, NTFS_I(inode));
}

ntfs_inode *ntfs_alloc_inode(void)
{
	ntfs_inode *ni = (ntfs_inode *)kmem_cache_alloc(ntfs_inode_cache,
			SLAB_NOFS);
	ntfs_debug("Entering.");
	if (unlikely(!ni))
		ntfs_error(NULL, "Allocation of NTFS inode structure failed.");
	return ni;
}

void ntfs_destroy_inode(ntfs_inode *ni)
{
	ntfs_debug("Entering.");
	BUG_ON(atomic_read(&ni->mft_count) || !atomic_dec_and_test(&ni->count));
	kmem_cache_free(ntfs_inode_cache, ni);
}

/**
 * __ntfs_init_inode - initialize ntfs specific part of an inode
 *
 * Initialize an ntfs inode to defaults.
 *
 * Return zero on success and -ENOMEM on error.
 */
static void __ntfs_init_inode(struct super_block *sb, ntfs_inode *ni)
{
	ntfs_debug("Entering.");
	memset(ni, 0, sizeof(ntfs_inode));
	atomic_set(&ni->count, 1);
	ni->vol = NULL;
	init_run_list(&ni->run_list);
	init_rwsem(&ni->mrec_lock);
	atomic_set(&ni->mft_count, 0);
	ni->page = NULL;
	ni->attr_list = NULL;
	init_run_list(&ni->attr_list_rl);
	init_run_list(&ni->_IDM(bmp_rl));
	init_MUTEX(&ni->extent_lock);
	ni->_INE(base_ntfs_ino) = NULL;
	ni->vol = NTFS_SB(sb);
	return;
}

static void ntfs_init_big_inode(struct inode *vi)
{
	ntfs_inode *ni = NTFS_I(vi);

	ntfs_debug("Entering.");
	__ntfs_init_inode(vi->i_sb, ni);
	ni->mft_no = vi->i_ino;
	return;
}

ntfs_inode *ntfs_new_inode(struct super_block *sb)
{
	ntfs_inode *ni = ntfs_alloc_inode();

	ntfs_debug("Entering.");
	if (ni)
		__ntfs_init_inode(sb, ni);
	return ni;
}

/**
 * ntfs_is_extended_system_file - check if a file is in the $Extend directory
 * @ctx:	initialized attribute search context
 *
 * Search all file name attributes in the inode described by the attribute
 * search context @ctx and check if any of the names are in the $Extend system
 * directory.
 * 
 * Return values:
 *	   1: file is in $Extend directory
 *	   0: file is not in $Extend directory
 *	-EIO: file is corrupt
 */
static int ntfs_is_extended_system_file(attr_search_context *ctx)
{
	int nr_links;

	/* Restart search. */
	reinit_attr_search_ctx(ctx);

	/* Get number of hard links. */
	nr_links = le16_to_cpu(ctx->mrec->link_count);

	/* Loop through all hard links. */
	while (lookup_attr(AT_FILE_NAME, NULL, 0, 0, 0, NULL, 0, ctx)) {
		FILE_NAME_ATTR *file_name_attr;
		ATTR_RECORD *attr = ctx->attr;
		u8 *p, *p2;

		nr_links--;
		/*
		 * Maximum sanity checking as we are called on an inode that
		 * we suspect might be corrupt.
		 */
		p = (u8*)attr + le32_to_cpu(attr->length);
		if (p < (u8*)ctx->mrec || (u8*)p > (u8*)ctx->mrec +
				le32_to_cpu(ctx->mrec->bytes_in_use)) {
err_corrupt_attr:
			ntfs_error(ctx->ntfs_ino->vol->sb, "Corrupt file name "
					"attribute. You should run chkdsk.");
			return -EIO;
		}
		if (attr->non_resident) {
			ntfs_error(ctx->ntfs_ino->vol->sb, "Non-resident file "
					"name. You should run chkdsk.");
			return -EIO;
		}
		if (attr->flags) {
			ntfs_error(ctx->ntfs_ino->vol->sb, "File name with "
					"invalid flags. You should run "
					"chkdsk.");
			return -EIO;
		}
		if (!(attr->_ARA(resident_flags) & RESIDENT_ATTR_IS_INDEXED)) {
			ntfs_error(ctx->ntfs_ino->vol->sb, "Unindexed file "
					"name. You should run chkdsk.");
			return -EIO;
		}
		file_name_attr = (FILE_NAME_ATTR*)((u8*)attr +
				le16_to_cpu(attr->_ARA(value_offset)));
		p2 = (u8*)attr + le32_to_cpu(attr->_ARA(value_length));
		if (p2 < (u8*)attr || p2 > p)
			goto err_corrupt_attr;
		/* This attribute is ok, but is it in the $Extend directory? */
		if (MREF_LE(file_name_attr->parent_directory) == FILE_Extend)
			return 1;	/* YES, it's an extended system file. */
	}
	if (nr_links) {
		ntfs_error(ctx->ntfs_ino->vol->sb, "Inode hard link count "
				"doesn't match number of name attributes. You "
				"should run chkdsk.");
		return -EIO;
	}
	return 0;	/* NO, it is not an extended system file. */
}

/**
 * ntfs_read_inode - read an inode from its device
 * @vi:		inode to read
 *
 * ntfs_read_inode() is called from the VFS iget() function to read the inode
 * described by @vi into memory from the device.
 *
 * The only fields in @vi that we need to/can look at when the function is
 * called are i_sb, pointing to the mounted device's super block, and i_ino,
 * the number of the inode to load.
 *
 * ntfs_read_inode() maps, pins and locks the mft record number i_ino for
 * reading and sets up the necessary @vi fields as well as initializing
 * the ntfs inode.
 *
 * Q: What locks are held when the function is called?
 * A: i_state has I_LOCK set, hence the inode is locked, also
 *    i_count is set to 1, so it is not going to go away
 *    i_flags is set to 0 and we have no business touching it. Only an ioctl()
 *    is allowed to write to them. We should of course be honouring them but
 *    we need to do that using the IS_* macros defined in include/linux/fs.h.
 *    In any case ntfs_read_inode() has nothing to do with i_flags at all.
 */
void ntfs_read_inode(struct inode *vi)
{
	ntfs_volume *vol = NTFS_SB(vi->i_sb);
	ntfs_inode *ni;
	MFT_RECORD *m;
	STANDARD_INFORMATION *si;
	attr_search_context *ctx;
	int err;

	ntfs_debug("Entering for i_ino 0x%lx.", vi->i_ino);

	/* Setup the generic vfs inode parts now. */

	/* This is the optimal IO size (for stat), not the fs block size. */
	vi->i_blksize = PAGE_CACHE_SIZE;
	/*
	 * This is for checking whether an inode has changed w.r.t. a file so
	 * that the file can be updated if necessary (compare with f_version).
	 */
	vi->i_version = ++event;
	/* Set uid and gid from the mount options. */
	vi->i_uid = vol->uid;
	vi->i_gid = vol->gid;
	/* Set to zero so we can use logical operations on it from here on. */
	vi->i_mode = 0;

	/*
	 * Initialize the ntfs specific part of @vi special casing
	 * FILE_MFT which we need to do at mount time.
	 */
	if (vi->i_ino != FILE_MFT)
		ntfs_init_big_inode(vi);

	ni = NTFS_I(vi);

	/* Map, pin and lock the mft record for reading. */
	m = map_mft_record(READ, ni);
	if (IS_ERR(m)) {
		err = PTR_ERR(m);
		goto err_out;
	}

	/* Is the record in use? */
	if (!(m->flags & MFT_RECORD_IN_USE)) {
		ntfs_error(vi->i_sb, "Inode is not in use! You should "
				"run chkdsk.");
		goto unm_err_out;
	}

	/* Is this an extent mft record / inode? Treat same as if not in use. */
	if (m->base_mft_record) {
		ntfs_error(vi->i_sb, "Inode is an extent inode! iget() "
				"not possible. You should run chkdsk.");
		goto unm_err_out;
	}

	/* Transfer information from mft record into vfs and ntfs inodes. */

	/* Cache the sequence number in the ntfs inode. */
	ni->seq_no = le16_to_cpu(m->sequence_number);

	/*
	 * FIXME: Keep in mind that link_count is two for files which have both
	 * a long file name and a short file name as separate entries, so if
	 * we are hiding short file names this will be too high. Either we need
	 * to account for the short file names by subtracting them or we need
	 * to make sure we delete files even though i_nlink is not zero which
	 * might be tricky due to vfs interactions. Need to think about this
	 * some more when implementing the unlink command.
	 */
	vi->i_nlink = le16_to_cpu(m->link_count);
	/*
	 * FIXME: Reparse points can have the directory bit set even though
	 * they would be S_IFLNK. Need to deal with this further below when we
	 * implement reparse points / symbolic links but it will do for now.
	 * Also if not a directory, it could be something else, rather than
	 * a regular file. But again, will do for now.
	 */
	if (m->flags & MFT_RECORD_IS_DIRECTORY) {
		vi->i_mode |= S_IFDIR;
		/*
		 * Linux/Unix do not support directory hard links and things
		 * break without this kludge.
		 */
		if (vi->i_nlink > 1)
			vi->i_nlink = 1;
	} else
		vi->i_mode |= S_IFREG;

	ctx = get_attr_search_ctx(ni, m);
	if (!ctx) {
		err = -ENOMEM;
		goto unm_err_out;
	}

	/*
	 * Find the standard information attribute in the mft record. At this
	 * stage we haven't setup the attribute list stuff yet, so this could
	 * in fact fail if the standard information is in an extent record, but
	 * I don't think this actually ever happens.
	 */
	if (!lookup_attr(AT_STANDARD_INFORMATION, NULL, 0, 0, 0, NULL, 0,
			ctx)) {
		/*
		 * TODO: We should be performing a hot fix here (if the recover
		 * mount option is set) by creating a new attribute.
		 */
		ntfs_error(vi->i_sb, "$STANDARD_INFORMATION attribute is "
				"missing.");
		goto put_unm_err_out;
	}
	/* Get the standard information attribute value. */
	si = (STANDARD_INFORMATION*)((char*)ctx->attr +
			le16_to_cpu(ctx->attr->_ARA(value_offset)));

	/* Transfer information from the standard information into vfs_ino. */
	/*
	 * Note: The i_?times do not quite map perfectly onto the NTFS times,
	 * but they are close enough, and in the end it doesn't really matter
	 * that much...
	 */
	/*
	 * mtime is the last change of the data within the file. Not changed
	 * when only metadata is changed, e.g. a rename doesn't affect mtime.
	 */
	vi->i_mtime = ntfs2utc(si->last_data_change_time);
	/*
	 * ctime is the last change of the metadata of the file. This obviously
	 * always changes, when mtime is changed. ctime can be changed on its
	 * own, mtime is then not changed, e.g. when a file is renamed.
	 */
	vi->i_ctime = ntfs2utc(si->last_mft_change_time);
	/*
	 * Last access to the data within the file. Not changed during a rename
	 * for example but changed whenever the file is written to.
	 */
	vi->i_atime = ntfs2utc(si->last_access_time);

	/*
	 * Find the attribute list attribute and set the corresponding bit in
	 * ntfs_ino->state.
	 */
	reinit_attr_search_ctx(ctx);
	if (lookup_attr(AT_ATTRIBUTE_LIST, NULL, 0, 0, 0, NULL, 0, ctx)) {
		if (vi->i_ino == FILE_MFT)
			goto skip_attr_list_load;
		ntfs_debug("Attribute list found in inode 0x%lx.", vi->i_ino);
		ni->state |= 1 << NI_AttrList;
		if (ctx->attr->flags & ATTR_IS_ENCRYPTED ||
				ctx->attr->flags & ATTR_COMPRESSION_MASK) {
			ntfs_error(vi->i_sb, "Attribute list attribute is "
					"compressed/encrypted. Not allowed. "
					"Corrupt inode. You should run "
					"chkdsk.");
			goto put_unm_err_out;
		}
		/* Now allocate memory for the attribute list. */
		ni->attr_list_size = (u32)attribute_value_length(ctx->attr);
		ni->attr_list = ntfs_malloc_nofs(ni->attr_list_size);
		if (!ni->attr_list) {
			ntfs_error(vi->i_sb, "Not enough memory to allocate "
					"buffer for attribute list.");
			err = -ENOMEM;
			goto ec_put_unm_err_out;
		}
		if (ctx->attr->non_resident) {
			ni->state |= 1 << NI_AttrListNonResident;
			if (ctx->attr->_ANR(lowest_vcn)) {
				ntfs_error(vi->i_sb, "Attribute list has non "
						"zero lowest_vcn. Inode is "
						"corrupt. You should run "
						"chkdsk.");
				goto put_unm_err_out;
			}
			/*
			 * Setup the run list. No need for locking as we have
			 * exclusive access to the inode at this time.
			 */
			ni->attr_list_rl.rl = decompress_mapping_pairs(vol,
					ctx->attr, NULL);
			if (IS_ERR(ni->attr_list_rl.rl)) {
				err = PTR_ERR(ni->attr_list_rl.rl);
				ni->attr_list_rl.rl = NULL;
				ntfs_error(vi->i_sb, "Mapping pairs "
						"decompression failed with "
						"error code %i. Corrupt "
						"attribute list in inode.",
						-err);
				goto ec_put_unm_err_out;
			}
			/* Now load the attribute list. */
			if ((err = load_attribute_list(vol, &ni->attr_list_rl,
					ni->attr_list, ni->attr_list_size,
					sle64_to_cpu(
					ctx->attr->_ANR(initialized_size))))) {
				ntfs_error(vi->i_sb, "Failed to load "
						"attribute list attribute.");
				goto ec_put_unm_err_out;
			}
		} else /* if (!ctx.attr->non_resident) */ {
			if ((u8*)ctx->attr + le16_to_cpu(
					ctx->attr->_ARA(value_offset)) +
					le32_to_cpu(
					ctx->attr->_ARA(value_length)) >
					(u8*)ctx->mrec + vol->mft_record_size) {
				ntfs_error(vi->i_sb, "Corrupt attribute list "
						"in inode.");
				goto put_unm_err_out;
			}
			/* Now copy the attribute list. */
			memcpy(ni->attr_list, (u8*)ctx->attr + le16_to_cpu(
					ctx->attr->_ARA(value_offset)),
					le32_to_cpu(
					ctx->attr->_ARA(value_length)));
		}
	}
skip_attr_list_load:
	/*
	 * If an attribute list is present we now have the attribute list value
	 * in ntfs_ino->attr_list and it is ntfs_ino->attr_list_size bytes.
	 */
	if (S_ISDIR(vi->i_mode)) {
		INDEX_ROOT *ir;
		char *ir_end, *index_end;

		/* It is a directory, find index root attribute. */
		reinit_attr_search_ctx(ctx);
		if (!lookup_attr(AT_INDEX_ROOT, I30, 4, CASE_SENSITIVE, 0,
				NULL, 0, ctx)) {
			// FIXME: File is corrupt! Hot-fix with empty index
			// root attribute if recovery option is set.
			ntfs_error(vi->i_sb, "$INDEX_ROOT attribute is "
					"missing.");
			goto put_unm_err_out;
		}
		/* Set up the state. */
		if (ctx->attr->non_resident) {
			ntfs_error(vi->i_sb, "$INDEX_ROOT attribute is "
					"not resident. Not allowed.");
			goto put_unm_err_out;
		}
		/*
		 * Compressed/encrypted index root just means that the newly
		 * created files in that directory should be created compressed/
		 * encrypted. However index root cannot be both compressed and
		 * encrypted.
		 */
		if (ctx->attr->flags & ATTR_COMPRESSION_MASK)
			ni->state |= 1 << NI_Compressed;
		if (ctx->attr->flags & ATTR_IS_ENCRYPTED) {
			if (ctx->attr->flags & ATTR_COMPRESSION_MASK) {
				ntfs_error(vi->i_sb, "Found encrypted and "
						"compressed attribute. Not "
						"allowed.");
				goto put_unm_err_out;
			}
			ni->state |= 1 << NI_Encrypted;
		}
		ir = (INDEX_ROOT*)((char*)ctx->attr +
				le16_to_cpu(ctx->attr->_ARA(value_offset)));
		ir_end = (char*)ir + le32_to_cpu(ctx->attr->_ARA(value_length));
		if (ir_end > (char*)ctx->mrec + vol->mft_record_size) {
			ntfs_error(vi->i_sb, "$INDEX_ROOT attribute is "
					"corrupt.");
			goto put_unm_err_out;
		}
		index_end = (char*)&ir->index +
				le32_to_cpu(ir->index.index_length);
		if (index_end > ir_end) {
			ntfs_error(vi->i_sb, "Directory index is corrupt.");
			goto put_unm_err_out;
		}
		if (ir->type != AT_FILE_NAME) {
			ntfs_error(vi->i_sb, __FUNCTION__ "(): Indexed "
					"attribute is not $FILE_NAME. Not "
					"allowed.");
			goto put_unm_err_out;
		}
		if (ir->collation_rule != COLLATION_FILE_NAME) {
			ntfs_error(vi->i_sb, "Index collation rule is not "
					"COLLATION_FILE_NAME. Not allowed.");
			goto put_unm_err_out;
		}
		ni->_IDM(index_block_size) = le32_to_cpu(ir->index_block_size);
		if (ni->_IDM(index_block_size) &
				(ni->_IDM(index_block_size) - 1)) {
			ntfs_error(vi->i_sb, "Index block size (%u) is not a "
					"power of two.",
					ni->_IDM(index_block_size));
			goto put_unm_err_out;
		}
		if (ni->_IDM(index_block_size) > PAGE_CACHE_SIZE) {
			ntfs_error(vi->i_sb, "Index block size (%u) > "
					"PAGE_CACHE_SIZE (%ld) is not "
					"supported. Sorry.",
					ni->_IDM(index_block_size),
					PAGE_CACHE_SIZE);
			err = -EOPNOTSUPP;
			goto ec_put_unm_err_out;
		}
		if (ni->_IDM(index_block_size) < NTFS_BLOCK_SIZE) {
			ntfs_error(vi->i_sb, "Index block size (%u) < "
					"NTFS_BLOCK_SIZE (%i) is not "
					"supported. Sorry.",
					ni->_IDM(index_block_size),
					NTFS_BLOCK_SIZE);
			err = -EOPNOTSUPP;
			goto ec_put_unm_err_out;
		}
		ni->_IDM(index_block_size_bits) =
				ffs(ni->_IDM(index_block_size)) - 1;
		/* Determine the size of a vcn in the directory index. */
		if (vol->cluster_size <= ni->_IDM(index_block_size)) {
			ni->_IDM(index_vcn_size) = vol->cluster_size;
			ni->_IDM(index_vcn_size_bits) = vol->cluster_size_bits;
		} else {
			ni->_IDM(index_vcn_size) = vol->sector_size;
			ni->_IDM(index_vcn_size_bits) = vol->sector_size_bits;
		}
		if (!(ir->index.flags & LARGE_INDEX)) {
			/* No index allocation. */
			vi->i_size = ni->initialized_size = 0;
			goto skip_large_dir_stuff;
		} /* LARGE_INDEX: Index allocation present. Setup state. */
		ni->state |= 1 << NI_NonResident;
		/* Find index allocation attribute. */
		reinit_attr_search_ctx(ctx);
		if (!lookup_attr(AT_INDEX_ALLOCATION, I30, 4, CASE_SENSITIVE,
				0, NULL, 0, ctx)) {
			ntfs_error(vi->i_sb, "$INDEX_ALLOCATION attribute "
					"is not present but $INDEX_ROOT "
					"indicated it is.");
			goto put_unm_err_out;
		}
		if (!ctx->attr->non_resident) {
			ntfs_error(vi->i_sb, "$INDEX_ALLOCATION attribute "
					"is resident.");
			goto put_unm_err_out;
		}
		if (ctx->attr->flags & ATTR_IS_ENCRYPTED) {
			ntfs_error(vi->i_sb, "$INDEX_ALLOCATION attribute "
					"is encrypted.");
			goto put_unm_err_out;
		}
		if (ctx->attr->flags & ATTR_COMPRESSION_MASK) {
			ntfs_error(vi->i_sb, "$INDEX_ALLOCATION attribute "
					"is compressed.");
			goto put_unm_err_out;
		}
		if (ctx->attr->_ANR(lowest_vcn)) {
			ntfs_error(vi->i_sb, "First extent of "
					"$INDEX_ALLOCATION attribute has non "
					"zero lowest_vcn. Inode is corrupt. "
					"You should run chkdsk.");
			goto put_unm_err_out;
		}
		vi->i_size = sle64_to_cpu(ctx->attr->_ANR(data_size));
		ni->initialized_size = sle64_to_cpu(
				ctx->attr->_ANR(initialized_size));
		ni->allocated_size = sle64_to_cpu(
				ctx->attr->_ANR(allocated_size));
		/* Find bitmap attribute. */
		reinit_attr_search_ctx(ctx);
		if (!lookup_attr(AT_BITMAP, I30, 4, CASE_SENSITIVE, 0, NULL, 0,
				ctx)) {
			ntfs_error(vi->i_sb, "$BITMAP attribute is not "
					"present but it must be.");
			goto put_unm_err_out;
		}
		if (ctx->attr->flags & (ATTR_COMPRESSION_MASK |
				ATTR_IS_ENCRYPTED)) {
			ntfs_error(vi->i_sb, "$BITMAP attribute is compressed "
					"and/or encrypted.");
			goto put_unm_err_out;
		}
		if (ctx->attr->non_resident) {
			ni->state |= 1 << NI_BmpNonResident;
			if (ctx->attr->_ANR(lowest_vcn)) {
				ntfs_error(vi->i_sb, "First extent of $BITMAP "
						"attribute has non zero "
						"lowest_vcn. Inode is corrupt. "
						"You should run chkdsk.");
				goto put_unm_err_out;
			}
			ni->_IDM(bmp_size) = sle64_to_cpu(
					ctx->attr->_ANR(data_size));
			ni->_IDM(bmp_initialized_size) = sle64_to_cpu(
					ctx->attr->_ANR(initialized_size));
			ni->_IDM(bmp_allocated_size) = sle64_to_cpu(
					ctx->attr->_ANR(allocated_size));
			/*
			 * Setup the run list. No need for locking as we have
			 * exclusive access to the inode at this time.
			 */
			ni->_IDM(bmp_rl).rl = decompress_mapping_pairs(vol,
					ctx->attr, NULL);
			if (IS_ERR(ni->_IDM(bmp_rl).rl)) {
				err = PTR_ERR(ni->_IDM(bmp_rl).rl);
				ni->_IDM(bmp_rl).rl = NULL;
				ntfs_error(vi->i_sb, "Mapping pairs "
						"decompression failed with "
						"error code %i.", -err);
				goto ec_put_unm_err_out;
			}
		} else
			ni->_IDM(bmp_size) = ni->_IDM(bmp_initialized_size) =
					ni->_IDM(bmp_allocated_size) =
					le32_to_cpu(
					ctx->attr->_ARA(value_length));
		/* Consistency check bitmap size vs. index allocation size. */
		if (ni->_IDM(bmp_size) << 3 < vi->i_size >>
				ni->_IDM(index_block_size_bits)) {
			ntfs_error(vi->i_sb, "$I30 bitmap too small (0x%Lx) "
					"for index allocation (0x%Lx).",
					(long long)ni->_IDM(bmp_size) << 3,
					vi->i_size);
			goto put_unm_err_out;
		}
skip_large_dir_stuff:
		/* Everyone gets read and scan permissions. */
		vi->i_mode |= S_IRUGO | S_IXUGO;
		/* If not read-only, set write permissions. */
		if (!IS_RDONLY(vi))
			vi->i_mode |= S_IWUGO;
		/*
		 * Apply the directory permissions mask set in the mount
		 * options.
		 */
		vi->i_mode &= ~vol->dmask;
		/* Setup the operations for this inode. */
		vi->i_op = &ntfs_dir_inode_ops;
		vi->i_fop = &ntfs_dir_ops;
		vi->i_mapping->a_ops = &ntfs_dir_aops;
	} else {
		/* It is a file: find first extent of unnamed data attribute. */
		reinit_attr_search_ctx(ctx);
		if (!lookup_attr(AT_DATA, NULL, 0, 0, 0, NULL, 0, ctx)) {
			vi->i_size = ni->initialized_size =
					ni->allocated_size = 0LL;
			/*
			 * FILE_Secure does not have an unnamed $DATA
			 * attribute, so we special case it here.
			 */
			if (vi->i_ino == FILE_Secure)
				goto no_data_attr_special_case;
			/*
			 * Most if not all the system files in the $Extend
			 * system directory do not have unnamed data
			 * attributes so we need to check if the parent
			 * directory of the file is FILE_Extend and if it is
			 * ignore this error. To do this we need to get the
			 * name of this inode from the mft record as the name
			 * contains the back reference to the parent directory.
			 */
			if (ntfs_is_extended_system_file(ctx) > 0)
				goto no_data_attr_special_case;
			// FIXME: File is corrupt! Hot-fix with empty data
			// attribute if recovery option is set.
			ntfs_error(vi->i_sb, "$DATA attribute is "
					"missing.");
			goto put_unm_err_out;
		}
		/* Setup the state. */
		if (ctx->attr->non_resident) {
			ni->state |= 1 << NI_NonResident;
			if (ctx->attr->flags & ATTR_COMPRESSION_MASK) {
				ni->state |= 1 << NI_Compressed;
				if (vol->cluster_size > 4096) {
					ntfs_error(vi->i_sb, "Found "
						"compressed data but "
						"compression is disabled due "
						"to cluster size (%i) > 4kiB.",
						vol->cluster_size);
					goto put_unm_err_out;
				}
				if ((ctx->attr->flags & ATTR_COMPRESSION_MASK)
						!= ATTR_IS_COMPRESSED) {
					ntfs_error(vi->i_sb, "Found "
						"unknown compression method or "
						"corrupt file.");
					goto put_unm_err_out;
				}
				ni->_ICF(compression_block_clusters) = 1U <<
					ctx->attr->_ANR(compression_unit);
				if (ctx->attr->_ANR(compression_unit) != 4) {
					ntfs_error(vi->i_sb, "Found "
						"nonstandard compression unit "
						"(%u instead of 4). Cannot "
						"handle this. This might "
						"indicate corruption so you "
						"should run chkdsk.",
					     ctx->attr->_ANR(compression_unit));
					err = -EOPNOTSUPP;
					goto ec_put_unm_err_out;
				}
				ni->_ICF(compression_block_size) = 1U << (
					       ctx->attr->_ANR(compression_unit)
						+ vol->cluster_size_bits);
				ni->_ICF(compression_block_size_bits) = ffs(
					ni->_ICF(compression_block_size)) - 1;
			}
			if (ctx->attr->flags & ATTR_IS_ENCRYPTED) {
				if (ctx->attr->flags & ATTR_COMPRESSION_MASK) {
					ntfs_error(vi->i_sb, "Found encrypted "
							"and compressed data.");
					goto put_unm_err_out;
				}
				ni->state |= 1 << NI_Encrypted;
			}
			if (ctx->attr->_ANR(lowest_vcn)) {
				ntfs_error(vi->i_sb, "First extent of $DATA "
						"attribute has non zero "
						"lowest_vcn. Inode is corrupt. "
						"You should run chkdsk.");
				goto put_unm_err_out;
			}
			/* Setup all the sizes. */
			vi->i_size = sle64_to_cpu(ctx->attr->_ANR(data_size));
			ni->initialized_size = sle64_to_cpu(
					ctx->attr->_ANR(initialized_size));
			ni->allocated_size = sle64_to_cpu(
					ctx->attr->_ANR(allocated_size));
			if (NInoCompressed(ni)) {
				ni->_ICF(compressed_size) = sle64_to_cpu(
					ctx->attr->_ANR(compressed_size));
				if (vi->i_size != ni->initialized_size)
					ntfs_warning(vi->i_sb, "Compressed "
							"file with data_size "
							"unequal to "
							"initialized size "
							"found. This will "
							"probably cause "
							"problems when trying "
							"to access the file. "
							"Please notify "
							"linux-ntfs-dev@"
							"lists.sf.net that you"
							"saw this message."
							"Thanks!");
			}
		} else { /* Resident attribute. */
			/*
			 * Make all sizes equal for simplicity in read code
			 * paths. FIXME: Need to keep this in mind when
			 * converting to non-resident attribute in write code
			 * path. (Probably only affects truncate().)
			 */
			vi->i_size = ni->initialized_size = ni->allocated_size =
				le32_to_cpu(ctx->attr->_ARA(value_length));
		}
no_data_attr_special_case:
		/* Everyone gets all permissions. */
		vi->i_mode |= S_IRWXUGO;
		/* If read-only, noone gets write permissions. */
		if (IS_RDONLY(vi))
			vi->i_mode &= ~S_IWUGO;
		/* Apply the file permissions mask set in the mount options. */
		vi->i_mode &= ~vol->fmask;
		/* Setup the operations for this inode. */
		vi->i_op = &ntfs_file_inode_ops;
		vi->i_fop = &ntfs_file_ops;
		vi->i_mapping->a_ops = &ntfs_file_aops;
	}
	/*
	 * The number of 512-byte blocks used on disk (for stat). This is in so
	 * far inaccurate as it doesn't account for any named streams or other
	 * special non-resident attributes, but that is how Windows works, too,
	 * so we are at least consistent with Windows, if not entirely
	 * consistent with the Linux Way. Doing it the Linux Way would cause a
	 * significant slowdown as it would involve iterating over all
	 * attributes in the mft record and adding the allocated/compressed
	 * sizes of all non-resident attributes present to give us the Linux
	 * correct size that should go into i_blocks (after division by 512).
	 */
	if (!NInoCompressed(ni))
		vi->i_blocks = ni->allocated_size >> 9;
	else
		vi->i_blocks = ni->_ICF(compressed_size) >> 9;
	/* Done. */
	put_attr_search_ctx(ctx);
	unmap_mft_record(READ, ni);
	ntfs_debug("Done.");
	return;
ec_put_unm_err_out:
	put_attr_search_ctx(ctx);
	goto ec_unm_err_out;
put_unm_err_out:
	put_attr_search_ctx(ctx);
unm_err_out:
	err = -EIO;
ec_unm_err_out:
	unmap_mft_record(READ, ni);
err_out:
	ntfs_error(vi->i_sb, "Failed with error code %i. Marking inode 0x%lx "
			"as bad.", -err, vi->i_ino);
	make_bad_inode(vi);
	return;
}

/**
 * ntfs_read_inode_mount - special read_inode for mount time use only
 * @vi:		inode to read
 *
 * Read inode FILE_MFT at mount time, only called with super_block lock
 * held from within the read_super() code path.
 *
 * This function exists because when it is called the page cache for $MFT/$DATA
 * is not initialized and hence we cannot get at the contents of mft records
 * by calling map_mft_record*().
 *
 * Further it needs to cope with the circular references problem, i.e. can't
 * load any attributes other than $ATTRIBUTE_LIST until $DATA is loaded, because
 * we don't know where the other extent mft records are yet and again, because
 * we cannot call map_mft_record*() yet. Obviously this applies only when an
 * attribute list is actually present in $MFT inode.
 *
 * We solve these problems by starting with the $DATA attribute before anything
 * else and iterating using lookup_attr($DATA) over all extents. As each extent
 * is found, we decompress_mapping_pairs() including the implied
 * merge_run_lists(). Each step of the iteration necessarily provides
 * sufficient information for the next step to complete.
 *
 * This should work but there are two possible pit falls (see inline comments
 * below), but only time will tell if they are real pits or just smoke...
 */
void ntfs_read_inode_mount(struct inode *vi)
{
	VCN next_vcn, last_vcn, highest_vcn;
	s64 block;
	struct super_block *sb = vi->i_sb;
	ntfs_volume *vol = NTFS_SB(sb);
	struct buffer_head *bh;
	ntfs_inode *ni;
	MFT_RECORD *m = NULL;
	ATTR_RECORD *attr;
	attr_search_context *ctx;
	unsigned int i, nr_blocks;
	int err;

	ntfs_debug("Entering.");

	/* Initialize the ntfs specific part of @vi. */
	ntfs_init_big_inode(vi);
	ni = NTFS_I(vi);
	if (vi->i_ino != FILE_MFT) {
		ntfs_error(sb, "Called for inode 0x%lx but only inode %d "
				"allowed.", vi->i_ino, FILE_MFT);
		goto err_out;
	}

	/*
	 * This sets up our little cheat allowing us to reuse the async io
	 * completion handler for directories.
	 */
	ni->_IDM(index_block_size) = vol->mft_record_size;
	ni->_IDM(index_block_size_bits) = vol->mft_record_size_bits;

	/* Very important! Needed to be able to call map_mft_record*(). */
	vol->mft_ino = vi;

	/* Allocate enough memory to read the first mft record. */
	if (vol->mft_record_size > 64 * 1024) {
		ntfs_error(sb, "Unsupported mft record size %i (max 64kiB).",
				vol->mft_record_size);
		goto err_out;
	}
	i = vol->mft_record_size;
	if (i < sb->s_blocksize)
		i = sb->s_blocksize;
	m = (MFT_RECORD*)ntfs_malloc_nofs(i);
	if (!m) {
		ntfs_error(sb, "Failed to allocate buffer for $MFT record 0.");
		goto err_out;
	}

	/* Determine the first block of the $MFT/$DATA attribute. */
	block = vol->mft_lcn << vol->cluster_size_bits >>
			sb->s_blocksize_bits;
	nr_blocks = vol->mft_record_size >> sb->s_blocksize_bits;
	if (!nr_blocks)
		nr_blocks = 1;

	/* Load $MFT/$DATA's first mft record. */
	for (i = 0; i < nr_blocks; i++) {
		bh = sb_bread(sb, block++);
		if (!bh) {
			ntfs_error(sb, "Device read failed.");
			goto err_out;
		}
		memcpy((char*)m + (i << sb->s_blocksize_bits), bh->b_data,
				sb->s_blocksize);
		brelse(bh);
	}

	/* Apply the mst fixups. */
	if (post_read_mst_fixup((NTFS_RECORD*)m, vol->mft_record_size)) {
		/* FIXME: Try to use the $MFTMirr now. */
		ntfs_error(sb, "MST fixup failed. $MFT is corrupt.");
		goto err_out;
	}

	/* Need this to sanity check attribute list references to $MFT. */
	ni->seq_no = le16_to_cpu(m->sequence_number);

	/* Provides readpage() and sync_page() for map_mft_record(READ). */
	vi->i_mapping->a_ops = &ntfs_mft_aops;

	ctx = get_attr_search_ctx(ni, m);
	if (!ctx) {
		err = -ENOMEM;
		goto err_out;
	}

	/* Find the attribute list attribute if present. */
	if (lookup_attr(AT_ATTRIBUTE_LIST, NULL, 0, 0, 0, NULL, 0, ctx)) {
		ATTR_LIST_ENTRY *al_entry, *next_al_entry;
		u8 *al_end;

		ntfs_debug("Attribute list attribute found in $MFT.");
		ni->state |= 1 << NI_AttrList;
		if (ctx->attr->flags & ATTR_IS_ENCRYPTED ||
				ctx->attr->flags & ATTR_COMPRESSION_MASK) {
			ntfs_error(sb, "Attribute list attribute is "
					"compressed/encrypted. Not allowed. "
					"$MFT is corrupt. You should run "
					"chkdsk.");
			goto put_err_out;
		}
		/* Now allocate memory for the attribute list. */
		ni->attr_list_size = (u32)attribute_value_length(ctx->attr);
		ni->attr_list = ntfs_malloc_nofs(ni->attr_list_size);
		if (!ni->attr_list) {
			ntfs_error(sb, "Not enough memory to allocate buffer "
					"for attribute list.");
			goto put_err_out;
		}
		if (ctx->attr->non_resident) {
			ni->state |= 1 << NI_AttrListNonResident;
			if (ctx->attr->_ANR(lowest_vcn)) {
				ntfs_error(sb, "Attribute list has non zero "
						"lowest_vcn. $MFT is corrupt. "
						"You should run chkdsk.");
				goto put_err_out;
			}
			/* Setup the run list. */
			ni->attr_list_rl.rl = decompress_mapping_pairs(vol,
					ctx->attr, NULL);
			if (IS_ERR(ni->attr_list_rl.rl)) {
				err = PTR_ERR(ni->attr_list_rl.rl);
				ni->attr_list_rl.rl = NULL;
				ntfs_error(sb, "Mapping pairs decompression "
						"failed with error code %i.",
						-err);
				goto put_err_out;
			}
			/* Now load the attribute list. */
			if ((err = load_attribute_list(vol, &ni->attr_list_rl,
					ni->attr_list, ni->attr_list_size,
					sle64_to_cpu(
					ctx->attr->_ANR(initialized_size))))) {
				ntfs_error(sb, "Failed to load attribute list "
						"attribute with error code %i.",
						-err);
				goto put_err_out;
			}
		} else /* if (!ctx.attr->non_resident) */ {
			if ((u8*)ctx->attr + le16_to_cpu(
					ctx->attr->_ARA(value_offset)) +
					le32_to_cpu(
					ctx->attr->_ARA(value_length)) >
					(u8*)ctx->mrec + vol->mft_record_size) {
				ntfs_error(sb, "Corrupt attribute list "
						"attribute.");
				goto put_err_out;
			}
			/* Now copy the attribute list. */
			memcpy(ni->attr_list, (u8*)ctx->attr + le16_to_cpu(
					ctx->attr->_ARA(value_offset)),
					le32_to_cpu(
					ctx->attr->_ARA(value_length)));
		}
		/* The attribute list is now setup in memory. */
		/*
		 * FIXME: I don't know if this case is actually possible.
		 * According to logic it is not possible but I have seen too
		 * many weird things in MS software to rely on logic... Thus we
		 * perform a manual search and make sure the first $MFT/$DATA
		 * extent is in the base inode. If it is not we abort with an
		 * error and if we ever see a report of this error we will need
		 * to do some magic in order to have the necessary mft record
		 * loaded and in the right place in the page cache. But
		 * hopefully logic will prevail and this never happens...
		 */
		al_entry = (ATTR_LIST_ENTRY*)ni->attr_list;
		al_end = (u8*)al_entry + ni->attr_list_size;
		for (;; al_entry = next_al_entry) {
			/* Out of bounds check. */
			if ((u8*)al_entry < ni->attr_list ||
					(u8*)al_entry > al_end)
				goto em_put_err_out;
			/* Catch the end of the attribute list. */
			if ((u8*)al_entry == al_end)
				goto em_put_err_out;
			if (!al_entry->length)
				goto em_put_err_out;
			if ((u8*)al_entry + 6 > al_end || (u8*)al_entry +
					le16_to_cpu(al_entry->length) > al_end)
				goto em_put_err_out;
			next_al_entry = (ATTR_LIST_ENTRY*)((u8*)al_entry +
					le16_to_cpu(al_entry->length));
			if (le32_to_cpu(al_entry->type) >
					const_le32_to_cpu(AT_DATA))
				goto em_put_err_out;
			if (AT_DATA != al_entry->type)
				continue;
			/* We want an unnamed attribute. */
			if (al_entry->name_length)
				goto em_put_err_out;
			/* Want the first entry, i.e. lowest_vcn == 0. */
			if (al_entry->lowest_vcn)
				goto em_put_err_out;
			/* First entry has to be in the base mft record. */
			if (MREF_LE(al_entry->mft_reference) != vi->i_ino) {
				/* MFT references do not match, logic fails. */
				ntfs_error(sb, "BUG: The first $DATA extent "
						"of $MFT is not in the base "
						"mft record. Please report "
						"you saw this message to "
						"linux-ntfs-dev@lists.sf.net");
				goto put_err_out;
			} else {
				/* Sequence numbers must match. */
				if (MSEQNO_LE(al_entry->mft_reference) !=
						ni->seq_no)
					goto em_put_err_out;
				/* Got it. All is ok. We can stop now. */
				break;
			}
		}
	}

	reinit_attr_search_ctx(ctx);

	/* Now load all attribute extents. */
	attr = NULL;
	next_vcn = last_vcn = highest_vcn = 0;
	while (lookup_attr(AT_DATA, NULL, 0, 0, next_vcn, NULL, 0, ctx)) {
		run_list_element *nrl;

		/* Cache the current attribute. */
		attr = ctx->attr;
		/* $MFT must be non-resident. */
		if (!attr->non_resident) {
			ntfs_error(sb, "$MFT must be non-resident but a "
					"resident extent was found. $MFT is "
					"corrupt. Run chkdsk.");
			goto put_err_out;
		}
		/* $MFT must be uncompressed and unencrypted. */
		if (attr->flags & ATTR_COMPRESSION_MASK ||
				attr->flags & ATTR_IS_ENCRYPTED) {
			ntfs_error(sb, "$MFT must be uncompressed and "
					"unencrypted but a compressed/"
					"encrypted extent was found. "
					"$MFT is corrupt. Run chkdsk.");
			goto put_err_out;
		}
		/*
		 * Decompress the mapping pairs array of this extent and merge
		 * the result into the existing run list. No need for locking
		 * as we have exclusive access to the inode at this time and we
		 * are a mount in progress task, too.
		 */
		nrl = decompress_mapping_pairs(vol, attr, ni->run_list.rl);
		if (IS_ERR(nrl)) {
			ntfs_error(sb, "decompress_mapping_pairs() failed with "
					"error code %ld. $MFT is corrupt.",
					PTR_ERR(nrl));
			goto put_err_out;
		}
		ni->run_list.rl = nrl;

		/* Are we in the first extent? */
		if (!next_vcn) {
			u64 ll;

			if (attr->_ANR(lowest_vcn)) {
				ntfs_error(sb, "First extent of $DATA "
						"attribute has non zero "
						"lowest_vcn. $MFT is corrupt. "
						"You should run chkdsk.");
				goto put_err_out;
			}
			/* Get the last vcn in the $DATA attribute. */
			last_vcn = sle64_to_cpu(attr->_ANR(allocated_size)) >>
					vol->cluster_size_bits;
			/* Fill in the inode size. */
			vi->i_size = sle64_to_cpu(attr->_ANR(data_size));
			ni->initialized_size = sle64_to_cpu(
					attr->_ANR(initialized_size));
			ni->allocated_size = sle64_to_cpu(
					attr->_ANR(allocated_size));
			/* Set the number of mft records. */
			ll = vi->i_size >> vol->mft_record_size_bits;
			/*
			 * Verify the number of mft records does not exceed
			 * 2^32 - 1.
			 */
			if (ll >= (1ULL << 32)) {
				ntfs_error(sb, "$MFT is too big! Aborting.");
				goto put_err_out;
			}
			vol->_VMM(nr_mft_records) = ll;
			/*
			 * We have got the first extent of the run_list for
			 * $MFT which means it is now relatively safe to call
			 * the normal ntfs_read_inode() function. Thus, take
			 * us out of the calling chain. Also we need to do this
			 * now because we need ntfs_read_inode() in place to
			 * get at subsequent extents.
			 */
			sb->s_op = &ntfs_sops;
			/*
			 * Complete reading the inode, this will actually
			 * re-read the mft record for $MFT, this time entering
			 * it into the page cache with which we complete the
			 * kick start of the volume. It should be safe to do
			 * this now as the first extent of $MFT/$DATA is
			 * already known and we would hope that we don't need
			 * further extents in order to find the other
			 * attributes belonging to $MFT. Only time will tell if
			 * this is really the case. If not we will have to play
			 * magic at this point, possibly duplicating a lot of
			 * ntfs_read_inode() at this point. We will need to
			 * ensure we do enough of its work to be able to call
			 * ntfs_read_inode() on extents of $MFT/$DATA. But lets
			 * hope this never happens...
			 */
			ntfs_read_inode(vi);
			if (is_bad_inode(vi)) {
				ntfs_error(sb, "ntfs_read_inode() of $MFT "
						"failed. BUG or corrupt $MFT. "
						"Run chkdsk and if no errors "
						"are found, please report you "
						"saw this message to "
						"linux-ntfs-dev@lists.sf.net");
				put_attr_search_ctx(ctx);
				/* Revert to the safe super operations. */
				sb->s_op = &ntfs_mount_sops;
				goto out_now;
			}
			/*
			 * Re-initialize some specifics about $MFT's inode as
			 * ntfs_read_inode() will have set up the default ones.
			 */
			/* Set uid and gid to root. */
			vi->i_uid = vi->i_gid = 0;
			/* Regular file. No access for anyone. */
			vi->i_mode = S_IFREG;
			/* No VFS initiated operations allowed for $MFT. */
			vi->i_op = &ntfs_empty_inode_ops;
			vi->i_fop = &ntfs_empty_file_ops;
			/* Put back our special address space operations. */
			vi->i_mapping->a_ops = &ntfs_mft_aops;
		}

		/* Get the lowest vcn for the next extent. */
		highest_vcn = sle64_to_cpu(attr->_ANR(highest_vcn));
		next_vcn = highest_vcn + 1;

		/* Only one extent or error, which we catch below. */
		if (next_vcn <= 0)
			break;

		/* Avoid endless loops due to corruption. */
		if (next_vcn < sle64_to_cpu(attr->_ANR(lowest_vcn))) {
			ntfs_error(sb, "$MFT has corrupt attribute list "
					"attribute. Run chkdsk.");
			goto put_err_out;
		}
	}
	if (!attr) {
		ntfs_error(sb, "$MFT/$DATA attribute not found. $MFT is "
				"corrupt. Run chkdsk.");
		goto put_err_out;
	}
	if (highest_vcn && highest_vcn != last_vcn - 1) {
		ntfs_error(sb, "Failed to load the complete run list "
				"for $MFT/$DATA. Driver bug or "
				"corrupt $MFT. Run chkdsk.");
		ntfs_debug("highest_vcn = 0x%Lx, last_vcn - 1 = 0x%Lx",
				(long long)highest_vcn,
				(long long)last_vcn - 1);
		goto put_err_out;
	}
	put_attr_search_ctx(ctx);
	ntfs_debug("Done.");
out_now:
	ntfs_free(m);
	return;
em_put_err_out:
	ntfs_error(sb, "Couldn't find first extent of $DATA attribute in "
			"attribute list. $MFT is corrupt. Run chkdsk.");
put_err_out:
	put_attr_search_ctx(ctx);
err_out:
	/* Make sure we revert to the safe super operations. */
	sb->s_op = &ntfs_mount_sops;
	ntfs_error(sb, "Failed. Marking inode as bad.");
	make_bad_inode(vi);
	goto out_now;
}

/**
 * ntfs_dirty_inode - mark the inode's metadata dirty
 * @vi:		inode to mark dirty
 *
 * This is called from fs/inode.c::__mark_inode_dirty(), when the inode itself
 * is being marked dirty. An example is when UPDATE_ATIME() is invoked.
 *
 * We mark the inode dirty by setting both the page in which the mft record
 * resides and the buffer heads in that page which correspond to the mft record
 * dirty. This ensures that the changes will eventually be propagated to disk
 * when the inode is set dirty.
 *
 * FIXME: Can we do that with the buffer heads? I am not too sure. Because if we
 * do that we need to make sure that the kernel will not write out those buffer
 * heads or we are screwed as it will write corrupt data to disk. The only way
 * a mft record can be written correctly is by mst protecting it, writting it
 * synchronously and fast mst deprotecting it. During this period, obviously,
 * the mft record must be marked as not uptodate, be locked for writing or
 * whatever, so that nobody attempts anything stupid.
 *
 * FIXME: Do we need to check that the fs is not mounted read only? And what
 * about the inode? Anything else?
 *
 * FIXME: As we are only a read only driver it is safe to just return here for
 * the moment.
 */
void ntfs_dirty_inode(struct inode *vi)
{
	ntfs_debug("Entering for inode 0x%lx.", vi->i_ino);
	NInoSetDirty(NTFS_I(vi));
	return;
}

/**
 * ntfs_commit_inode - write out a dirty inode
 * @ni:		inode to write out
 *
 */
int ntfs_commit_inode(ntfs_inode *ni)
{
	ntfs_debug("Entering for inode 0x%lx.", ni->mft_no);
	NInoClearDirty(ni);
	return 0;
}

void __ntfs_clear_inode(ntfs_inode *ni)
{
	int err;

	ntfs_debug("Entering for inode 0x%lx.", ni->mft_no);
	if (NInoDirty(ni)) {
		err = ntfs_commit_inode(ni);
		if (err) {
			ntfs_error(ni->vol->sb, "Failed to commit dirty "
					"inode synchronously.");
			// FIXME: Do something!!!
		}
	}
	/* Synchronize with ntfs_commit_inode(). */
	down_write(&ni->mrec_lock);
	up_write(&ni->mrec_lock);
	if (NInoDirty(ni)) {
		ntfs_error(ni->vol->sb, "Failed to commit dirty inode "
				"asynchronously.");
		// FIXME: Do something!!!
	}
	/* No need to lock at this stage as no one else has a reference. */
	if (ni->nr_extents > 0) {
		int i;

		// FIXME: Handle dirty case for each extent inode!
		for (i = 0; i < ni->nr_extents; i++)
			ntfs_destroy_inode(ni->_INE(extent_ntfs_inos)[i]);
		kfree(ni->_INE(extent_ntfs_inos));
	}
	/* Free all alocated memory. */
	down_write(&ni->run_list.lock);
	ntfs_free(ni->run_list.rl);
	ni->run_list.rl = NULL;
	up_write(&ni->run_list.lock);

	ntfs_free(ni->attr_list);

	down_write(&ni->attr_list_rl.lock);
	ntfs_free(ni->attr_list_rl.rl);
	ni->attr_list_rl.rl = NULL;
	up_write(&ni->attr_list_rl.lock);
}

void ntfs_clear_inode(ntfs_inode *ni)
{
	__ntfs_clear_inode(ni);

	/* Bye, bye... */
	ntfs_destroy_inode(ni);
}

/**
 * ntfs_clear_big_inode - clean up the ntfs specific part of an inode
 * @vi:		vfs inode pending annihilation
 *
 * When the VFS is going to remove an inode from memory, ntfs_clear_big_inode()
 * is called, which deallocates all memory belonging to the NTFS specific part
 * of the inode and returns.
 *
 * If the MFT record is dirty, we commit it before doing anything else.
 */
void ntfs_clear_big_inode(struct inode *vi)
{
	ntfs_inode *ni = NTFS_I(vi);

	__ntfs_clear_inode(ni);

	if (S_ISDIR(vi->i_mode)) {
		down_write(&ni->_IDM(bmp_rl).lock);
		ntfs_free(ni->_IDM(bmp_rl).rl);
		up_write(&ni->_IDM(bmp_rl).lock);
	}
	return;
}

/**
 * ntfs_show_options - show mount options in /proc/mounts
 * @sf:		seq_file in which to write our mount options
 * @mnt:	vfs mount whose mount options to display
 *
 * Called by the VFS once for each mounted ntfs volume when someone reads
 * /proc/mounts in order to display the NTFS specific mount options of each
 * mount. The mount options of the vfs mount @mnt are written to the seq file
 * @sf and success is returned.
 */
int ntfs_show_options(struct seq_file *sf, struct vfsmount *mnt)
{
	ntfs_volume *vol = NTFS_SB(mnt->mnt_sb);
	int i;

	seq_printf(sf, ",uid=%i", vol->uid);
	seq_printf(sf, ",gid=%i", vol->gid);
	if (vol->fmask == vol->dmask)
		seq_printf(sf, ",umask=0%o", vol->fmask);
	else {
		seq_printf(sf, ",fmask=0%o", vol->fmask);
		seq_printf(sf, ",dmask=0%o", vol->dmask);
	}
	seq_printf(sf, ",nls=%s", vol->nls_map->charset);
	if (NVolCaseSensitive(vol))
		seq_printf(sf, ",case_sensitive");
	if (NVolShowSystemFiles(vol))
		seq_printf(sf, ",show_sys_files");
	for (i = 0; on_errors_arr[i].val; i++) {
		if (on_errors_arr[i].val & vol->on_errors)
			seq_printf(sf, ",errors=%s", on_errors_arr[i].str);
	}
	seq_printf(sf, ",mft_zone_multiplier=%i", vol->mft_zone_multiplier);
	return 0;
}

