/*
 *  linux/fs/ext2/super.c
 *
 *  Copyright (C) 1992, 1993  Remy Card (card@masi.ibp.fr)
 *
 *  from
 *
 *  linux/fs/minix/inode.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <stdarg.h>

#include <asm/segment.h>
#include <asm/system.h>

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ext2_fs.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/locks.h>

extern int vsprintf (char *, const char *, va_list);

void ext2_error (struct super_block * sb, const char * function,
		 const char * fmt, ...)
{
	char buf[1024];
	va_list args;

	if (!(sb->s_flags & MS_RDONLY)) {
		sb->u.ext2_sb.s_mount_state |= EXT2_ERROR_FS;
		sb->u.ext2_sb.s_es->s_state |= EXT2_ERROR_FS;
		sb->u.ext2_sb.s_sbh->b_dirt = 1;
		sb->s_dirt = 1;
	}
	va_start (args, fmt);
	vsprintf (buf, fmt, args);
	va_end (args);
	printk (
#ifdef KERN_ERR
		KERN_ERR
#endif
		"EXT2-fs error (device %d/%d): %s: %s\n",
		MAJOR(sb->s_dev), MINOR(sb->s_dev), function, buf);
}

volatile void ext2_panic (struct super_block * sb, const char * function,
			  const char * fmt, ...)
{
	char buf[1024];
	va_list args;

	if (!(sb->s_flags & MS_RDONLY)) {
		sb->u.ext2_sb.s_mount_state |= EXT2_ERROR_FS;
		sb->u.ext2_sb.s_es->s_state |= EXT2_ERROR_FS;
		sb->u.ext2_sb.s_sbh->b_dirt = 1;
		sb->s_dirt = 1;
	}
	va_start (args, fmt);
	vsprintf (buf, fmt, args);
	va_end (args);
	panic ("EXT2-fs panic (device %d/%d): %s: %s\n",
	       MAJOR(sb->s_dev), MINOR(sb->s_dev), function, buf);
}

void ext2_warning (struct super_block * sb, const char * function,
		   const char * fmt, ...)
{
	char buf[1024];
	va_list args;

	va_start (args, fmt);
	vsprintf (buf, fmt, args);
	va_end (args);
	printk (
#ifdef KERN_WARNING
		KERN_WARNING
#endif
		"EXT2-fs warning (device %d/%d): %s: %s\n",
		MAJOR(sb->s_dev), MINOR(sb->s_dev), function, buf);
}

void ext2_put_super (struct super_block * sb)
{
	int i;

	lock_super (sb);
	if (!(sb->s_flags & MS_RDONLY)) {
		sb->u.ext2_sb.s_es->s_state = sb->u.ext2_sb.s_mount_state;
		sb->u.ext2_sb.s_sbh->b_dirt = 1;
	}
#ifndef DONT_USE_DCACHE
	ext2_dcache_invalidate (sb->s_dev);
#endif
	sb->s_dev = 0;
	for (i = 0; i < EXT2_MAX_GROUP_DESC; i++)
		if (sb->u.ext2_sb.s_group_desc[i])
			brelse (sb->u.ext2_sb.s_group_desc[i]);
	for (i = 0; i < EXT2_MAX_GROUP_LOADED; i++)
		if (sb->u.ext2_sb.s_inode_bitmap[i])
			brelse (sb->u.ext2_sb.s_inode_bitmap[i]);
	for (i = 0; i < EXT2_MAX_GROUP_LOADED; i++)
		if (sb->u.ext2_sb.s_block_bitmap[i])
			brelse (sb->u.ext2_sb.s_block_bitmap[i]);
	brelse (sb->u.ext2_sb.s_sbh);
	unlock_super (sb);
	return;
}

static struct super_operations ext2_sops = { 
	ext2_read_inode,
	NULL,
	ext2_write_inode,
	ext2_put_inode,
	ext2_put_super,
	ext2_write_super,
	ext2_statfs,
	ext2_remount
};

#ifdef EXT2FS_PRE_02B_COMPAT

static int convert_pre_02b_fs (struct super_block * sb,
			       struct buffer_head * bh)
{
	struct ext2_super_block * es;
	struct ext2_old_group_desc old_group_desc [BLOCK_SIZE / sizeof (struct ext2_old_group_desc)];
	struct ext2_group_desc * gdp;
	struct buffer_head * bh2;
	int groups_count;
	int i;

	es = (struct ext2_super_block *) bh->b_data;
	bh2 = bread (sb->s_dev, 2, BLOCK_SIZE);
	if (!bh2) {
		printk ("Cannot read descriptor blocks while converting !\n");
		return 0;
	}
	memcpy (old_group_desc, bh2->b_data, BLOCK_SIZE);
	groups_count = (sb->u.ext2_sb.s_blocks_count - 
			sb->u.ext2_sb.s_first_data_block +
			(EXT2_BLOCK_SIZE(sb) * 8) - 1) /
				(EXT2_BLOCK_SIZE(sb) * 8);
	memset (bh2->b_data, 0, BLOCK_SIZE);
	gdp = (struct ext2_group_desc *) bh2->b_data;
	for (i = 0; i < groups_count; i++) {
		gdp[i].bg_block_bitmap = old_group_desc[i].bg_block_bitmap;
		gdp[i].bg_inode_bitmap = old_group_desc[i].bg_inode_bitmap;
		gdp[i].bg_inode_table = old_group_desc[i].bg_inode_table;
		gdp[i].bg_free_blocks_count = old_group_desc[i].bg_free_blocks_count;
		gdp[i].bg_free_inodes_count = old_group_desc[i].bg_free_inodes_count;
	}
	bh2->b_dirt = 1;
	brelse (bh2);
	es->s_magic = EXT2_SUPER_MAGIC;
	bh->b_dirt = 1;
	sb->s_magic = EXT2_SUPER_MAGIC;
	return 1;
}

#endif

/*
 * This function has been shamelessly adapted from the msdos fs
 */
static int parse_options (char * options, unsigned long * sb_block,
			  unsigned long * mount_options)
{
	char * this_char;
	char * value;

	if (!options)
		return 1;
	for (this_char = strtok (options, ",");
	     this_char != NULL;
	     this_char = strtok (NULL, ",")) {
		if ((value = strchr (this_char, '=')) != NULL)
			*value++ = 0;
		if (!strcmp (this_char, "check"))
			*mount_options |= EXT2_MOUNT_CHECK;
		else if (!strcmp (this_char, "sb")) {
			if (!value || !*value)
				return 0;
			*sb_block = simple_strtoul (value, &value, 0);
			if (*value)
				return 0;
		}
		else {
			printk ("EXT2-fs: Unrecognized mount option %s\n", this_char);
			return 0;
		}
	}
	return 1;
}

struct super_block * ext2_read_super (struct super_block * s, void * data,
				      int silent)
{
	struct buffer_head * bh;
	struct ext2_super_block * es;
	unsigned long sb_block = 1;
	unsigned long logic_sb_block = 1;
	int dev = s->s_dev;
	int bh_count;
	int i, j;
#ifdef EXT2FS_PRE_02B_COMPAT
	int fs_converted = 0;
#endif

	s->u.ext2_sb.s_mount_opt = 0;
	if (!parse_options ((char *) data, &sb_block,
	    &s->u.ext2_sb.s_mount_opt)) {
		s->s_dev = 0;
		return NULL;
	}

	lock_super (s);
	set_blocksize (dev, BLOCK_SIZE);
	if (!(bh = bread (dev, sb_block, BLOCK_SIZE))) {
		s->s_dev = 0;
		unlock_super (s);
		printk ("EXT2-fs: unable to read superblock\n");
		return NULL;
	}
	es = (struct ext2_super_block *) bh->b_data;
	/* Note: s_es must be initialized s_es as soon as possible because
	   some ext2 macro-instructions depend on its value */
	s->u.ext2_sb.s_es = es;
	s->s_magic = es->s_magic;
	if (s->s_magic != EXT2_SUPER_MAGIC
#ifdef EXT2FS_PRE_02B_COMPAT
	   && s->s_magic != EXT2_PRE_02B_MAGIC
#endif
	   ) {
		s->s_dev = 0;
		unlock_super (s);
		brelse (bh);
		if (!silent)
			printk ("VFS: Can't find an ext2 filesystem on dev 0x%04x.\n",
				dev);
		return NULL;
	}
	s->s_blocksize = EXT2_MIN_BLOCK_SIZE << es->s_log_block_size;
	s->s_blocksize_bits = EXT2_BLOCK_SIZE_BITS(s);
	if (s->s_blocksize != BLOCK_SIZE && 
	    (s->s_blocksize == 1024 || s->s_blocksize == 2048 ||  
	     s->s_blocksize == 4096)) {
		unsigned long offset;

		brelse (bh);
		set_blocksize (dev, s->s_blocksize);
		logic_sb_block = sb_block / s->s_blocksize;
		offset = sb_block % s->s_blocksize;
		bh = bread (dev, logic_sb_block, s->s_blocksize);
		if(!bh)
			return NULL;
		es = (struct ext2_super_block *) (((char *)bh->b_data) + offset);
		s->u.ext2_sb.s_es = es;
		if (es->s_magic != EXT2_SUPER_MAGIC) {
			s->s_dev = 0;
			unlock_super (s);
			brelse (bh);
			printk ("EXT2-fs: Magic mismatch, very weird !\n");
			return NULL;
		}
	}
	s->u.ext2_sb.s_frag_size = EXT2_MIN_FRAG_SIZE <<
				   es->s_log_frag_size;
	if (s->u.ext2_sb.s_frag_size)
		s->u.ext2_sb.s_frags_per_block = s->s_blocksize /
						   s->u.ext2_sb.s_frag_size;
	else
		s->s_magic = 0;
	s->u.ext2_sb.s_blocks_per_group = es->s_blocks_per_group;
	s->u.ext2_sb.s_frags_per_group = es->s_frags_per_group;
	s->u.ext2_sb.s_inodes_per_group = es->s_inodes_per_group;
	s->u.ext2_sb.s_inodes_per_block = s->s_blocksize /
					  sizeof (struct ext2_inode);
	s->u.ext2_sb.s_desc_per_block = s->s_blocksize /
					sizeof (struct ext2_group_desc);
	s->u.ext2_sb.s_sbh = bh;
	s->u.ext2_sb.s_es = es;
	s->u.ext2_sb.s_mount_state = es->s_state;
	s->u.ext2_sb.s_rename_lock = 0;
	s->u.ext2_sb.s_rename_wait = NULL;
#ifdef EXT2FS_PRE_02B_COMPAT
	if (s->s_magic == EXT2_PRE_02B_MAGIC) {
		if (es->s_blocks_count > 262144) {
			 /* fs > 256 MB can't be converted */ 
			s->s_dev = 0;
			unlock_super (s);
			brelse (bh);
			printk ("EXT2-fs: trying to mount a pre-0.2b file"
				"system which cannot be converted\n");
			return NULL;
		}
		printk ("EXT2-fs: mounting a pre 0.2b file system, "
			"will try to convert the structure\n");
		if (!(s->s_flags & MS_RDONLY)) {
			s->s_dev = 0;
			unlock_super (s);
			brelse (bh);
			printk ("EXT2-fs: cannot convert a read-only fs\n");
			return NULL;
		}
		if (!convert_pre_02b_fs (s, bh)) {
			s->s_dev = 0;
			unlock_super (s);
			brelse (bh);
			printk ("EXT2-fs: conversion failed !!!\n");
			return NULL;
		}
		printk ("EXT2-fs: conversion succeeded !!!\n");
		fs_converted = 1;
	}
#endif
	if (s->s_magic != EXT2_SUPER_MAGIC) {
		s->s_dev = 0;
		unlock_super (s);
		brelse (bh);
		if (!silent)
			printk ("VFS: Can't find an ext2 filesystem on dev 0x%04x.\n",
				dev);
		return NULL;
	}
	if (s->s_blocksize != bh->b_size) {
		s->s_dev = 0;
		unlock_super (s);
		brelse (bh);
		if (!silent)
			printk ("VFS: Unsupported blocksize on dev 0x%04x.\n",
				dev);
		return NULL;
	}

	if (s->s_blocksize != s->u.ext2_sb.s_frag_size) {
		s->s_dev = 0;
		unlock_super (s);
		brelse (bh);
		printk ("EXT2-fs: fragsize %lu != blocksize %lu (not supported yet)\n",
			s->u.ext2_sb.s_frag_size, s->s_blocksize);
		return NULL;
	}

	s->u.ext2_sb.s_groups_count = (es->s_blocks_count -
				       es->s_first_data_block +
				       EXT2_BLOCKS_PER_GROUP(s) - 1) /
				       EXT2_BLOCKS_PER_GROUP(s);
	for (i = 0; i < EXT2_MAX_GROUP_DESC; i++)
		s->u.ext2_sb.s_group_desc[i] = NULL;
	bh_count = (s->u.ext2_sb.s_groups_count + EXT2_DESC_PER_BLOCK(s) - 1) /
		   EXT2_DESC_PER_BLOCK(s);
	if (bh_count > EXT2_MAX_GROUP_DESC) {
		s->s_dev = 0;
		unlock_super (s);
		brelse (bh);
		printk ("EXT2-fs: file system is too big\n");
		return NULL;
	}
	for (i = 0; i < bh_count; i++) {
		s->u.ext2_sb.s_group_desc[i] = bread (dev, logic_sb_block + i + 1,
						      s->s_blocksize);
		if (!s->u.ext2_sb.s_group_desc[i]) {
			s->s_dev = 0;
			unlock_super (s);
			for (j = 0; j < i; j++)
				brelse (s->u.ext2_sb.s_group_desc[i]);
			brelse (bh);
			printk ("EXT2-fs: unable to read group descriptors\n");
			return NULL;
		}
	}
	for (i = 0; i < EXT2_MAX_GROUP_LOADED; i++) {
		s->u.ext2_sb.s_inode_bitmap_number[i] = 0;
		s->u.ext2_sb.s_inode_bitmap[i] = NULL;
		s->u.ext2_sb.s_block_bitmap_number[i] = 0;
		s->u.ext2_sb.s_block_bitmap[i] = NULL;
	}
	s->u.ext2_sb.s_loaded_inode_bitmaps = 0;
	s->u.ext2_sb.s_loaded_block_bitmaps = 0;
	unlock_super (s);
	/* set up enough so that it can read an inode */
	s->s_dev = dev;
	s->s_op = &ext2_sops;
	if (!(s->s_mounted = iget (s, EXT2_ROOT_INO))) {
		s->s_dev = 0;
		for (i = 0; i < EXT2_MAX_GROUP_DESC; i++)
			if (s->u.ext2_sb.s_group_desc[i])
				brelse (s->u.ext2_sb.s_group_desc[i]);
		brelse (bh);
		printk ("EXT2-fs: get root inode failed\n");
		return NULL;
	}
	if (!(s->s_flags & MS_RDONLY)) {
		es->s_state &= ~EXT2_VALID_FS;
		if (!es->s_max_mnt_count)
			es->s_max_mnt_count = EXT2_DFL_MAX_MNT_COUNT;
		es->s_mnt_count++;
		es->s_mtime = CURRENT_TIME;
		bh->b_dirt = 1;
		s->s_dirt = 1;
	}
#ifdef EXT2FS_PRE_02B_COMPAT
	if (fs_converted) {
		for (i = 0; i < bh_count; i++)
			s->u.ext2_sb.s_group_desc[i]->b_dirt = 1;
		s->s_dirt = 1;
	}
#endif
	if (!(s->u.ext2_sb.s_mount_state & EXT2_VALID_FS))
		printk ("EXT2-fs warning: mounting unchecked file system, "
			"running e2fsck is recommended\n");
 	else if (s->u.ext2_sb.s_mount_state & EXT2_ERROR_FS)
		printk ("EXT2-fs warning: mounting file system with errors, "
			"running e2fsck is recommended\n");
	else if (es->s_mnt_count >= es->s_max_mnt_count)
		printk ("EXT2-fs warning: maximal mount count reached, "
                        "running e2fsck is recommended\n");
	if (s->u.ext2_sb.s_mount_opt & EXT2_MOUNT_CHECK) {
		printk ("[EXT II FS %s, %s, bs=%lu, fs=%lu, gc=%lu, bpg=%lu, ipg=%lu]\n",
			EXT2FS_VERSION, EXT2FS_DATE, s->s_blocksize,
			s->u.ext2_sb.s_frag_size, s->u.ext2_sb.s_groups_count,
			EXT2_BLOCKS_PER_GROUP(s), EXT2_INODES_PER_GROUP(s));
		ext2_check_blocks_bitmap (s);
		ext2_check_inodes_bitmap (s);
	}
	return s;
}

static void ext2_commit_super (struct super_block * sb,
			       struct ext2_super_block * es)
{
	es->s_wtime = CURRENT_TIME;
	sb->u.ext2_sb.s_sbh->b_dirt = 1;
	sb->s_dirt = 0;
}

/*
 * In the second extended file system, it is not necessary to
 * write the super block since we use a mapping of the
 * disk super block in a buffer.
 *
 * However, this function is still used to set the fs valid
 * flags to 0.  We need to set this flag to 0 since the fs
 * may have been checked while mounted and e2fsck may have
 * set s_state to EXT2_VALID_FS after some corrections.
 */

void ext2_write_super (struct super_block * sb)
{
	struct ext2_super_block * es;

	if (!(sb->s_flags & MS_RDONLY)) {
		es = sb->u.ext2_sb.s_es;

		ext2_debug ("setting valid to 0\n");

		if (es->s_state & EXT2_VALID_FS) {
			es->s_state &= ~EXT2_VALID_FS;
			es->s_mtime = CURRENT_TIME;
		}
		ext2_commit_super (sb, es);
	}
	sb->s_dirt = 0;
}

int ext2_remount (struct super_block * sb, int * flags)
{
	struct ext2_super_block * es;

	es = sb->u.ext2_sb.s_es;
	if ((*flags & MS_RDONLY) == (sb->s_flags & MS_RDONLY))
		return 0;
	if (*flags & MS_RDONLY) {
		if (es->s_state & EXT2_VALID_FS ||
		    !(sb->u.ext2_sb.s_mount_state & EXT2_VALID_FS))
			return 0;
		/* OK, we are remounting a valid rw partition rdonly, so set
		   the rdonly flag and then mark the partition as valid
		   again. */
		es->s_state = sb->u.ext2_sb.s_mount_state;
		es->s_mtime = CURRENT_TIME;
		sb->u.ext2_sb.s_sbh->b_dirt = 1;
		sb->s_dirt = 1;
		ext2_commit_super (sb, es);
	}
	else {
		/* Mounting a RDONLY partition read-write, so reread and
		   store the current valid flag.  (It may have been changed 
		   by e2fsck since we originally mounted the partition.)  */
		sb->u.ext2_sb.s_mount_state = es->s_state;
		es->s_state &= ~EXT2_VALID_FS;
		if (!es->s_max_mnt_count)
			es->s_max_mnt_count = EXT2_DFL_MAX_MNT_COUNT;
		es->s_mnt_count++;
		es->s_mtime = CURRENT_TIME;
		sb->u.ext2_sb.s_sbh->b_dirt = 1;
		sb->s_dirt = 1;
		if (!(sb->u.ext2_sb.s_mount_state & EXT2_VALID_FS))
			printk ("EXT2-fs warning: remounting unchecked fs, "
				"running e2fsck is recommended\n");
		else if ((sb->u.ext2_sb.s_mount_state & EXT2_ERROR_FS))
			printk ("EXT2-fs warning: remounting fs with errors, "
				"running e2fsck is recommended\n");
		else if (es->s_mnt_count >= es->s_max_mnt_count)
			printk ("EXT2-fs warning: maximal mount count reached, "
                        	"running e2fsck is recommended\n");
	}
	return 0;
}

void ext2_statfs (struct super_block * sb, struct statfs * buf)
{
	long tmp;

	put_fs_long (EXT2_SUPER_MAGIC, &buf->f_type);
	put_fs_long (sb->s_blocksize, &buf->f_bsize);
	put_fs_long (sb->u.ext2_sb.s_es->s_blocks_count, &buf->f_blocks);
	tmp = ext2_count_free_blocks (sb);
	put_fs_long (tmp, &buf->f_bfree);
	if (tmp >= sb->u.ext2_sb.s_es->s_r_blocks_count)
		put_fs_long (tmp - sb->u.ext2_sb.s_es->s_r_blocks_count,
			     &buf->f_bavail);
	else
		put_fs_long (0, &buf->f_bavail);
	put_fs_long (sb->u.ext2_sb.s_es->s_inodes_count, &buf->f_files);
	put_fs_long (ext2_count_free_inodes (sb), &buf->f_ffree);
	put_fs_long (EXT2_NAME_LEN, &buf->f_namelen);
	/* Don't know what value to put in buf->f_fsid */
}
