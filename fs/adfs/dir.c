/*
 *  linux/fs/adfs/dir.c
 *
 * Copyright (C) 1997 Russell King
 */

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/adfs_fs.h>
#include <linux/sched.h>
#include <linux/stat.h>

static ssize_t adfs_dirread (struct file *filp, char *buf,
			     size_t siz, loff_t *ppos)
{
	return -EISDIR;
}

static int adfs_readdir (struct file *, void *, filldir_t);

static struct file_operations adfs_dir_operations = {
	NULL,			/* lseek - default */
	adfs_dirread,		/* read */
	NULL,			/* write - bad */
	adfs_readdir,		/* readdir */
	NULL,			/* select - default */
	NULL,			/* ioctl */
	NULL,			/* mmap */
	NULL,			/* no special open code */
	NULL,			/* flush */
	NULL,			/* no special release code */
	file_fsync,		/* fsync */
	NULL,			/* fasync */
	NULL,			/* check_media_change */
	NULL			/* revalidate */
};

/*
 * directories can handle most operations...
 */
struct inode_operations adfs_dir_inode_operations = {
	&adfs_dir_operations,	/* default directory file-ops */
	NULL,			/* create */
	adfs_lookup,		/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* read link */
	NULL,			/* follow link */
	NULL,			/* read page */
	NULL,			/* write page */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL			/* smap */
};

unsigned int adfs_val (unsigned char *p, int len)
{
	unsigned int val = 0;

	switch (len) {
	case 4:
		val |= p[3] << 24;
	case 3:
		val |= p[2] << 16;
	case 2:
		val |= p[1] << 8;
	default:
		val |= p[0];
	}
	return val;
}

static unsigned int adfs_time (unsigned int load, unsigned int exec)
{
	unsigned int high, low;

	high = ((load << 24) | (exec >> 8)) - 0x336e996a;
	low  =  exec & 255;

	/* 65537 = h256,l1
	 * (h256 % 100) = 56         h256 / 100 = 2
	 *    56 << 8 = 14336           2 * 256 = 512
	 *      + l1 = 14337
	 *        / 100 = 143
	 *          + 512 = 655
	 */
	return (((high % 100) << 8) + low) / 100 + (high / 100 << 8);
}

int adfs_readname (char *buf, char *ptr, int maxlen)
{
	int size = 0;
	while (*ptr >= ' ' && maxlen--) {
		switch (*ptr) {
		case '/':
			*buf++ = '.';
			break;
		default:
			*buf++ = *ptr;
			break;
		}
		ptr++;
		size ++;
	}
	*buf = '\0';
	return size;
}

int adfs_dir_read_parent (struct inode *inode, struct buffer_head **bhp)
{
	struct super_block *sb;
	int i, size;

	if (!inode)
		return 0;

	sb = inode->i_sb;

	size = 2048 >> sb->s_blocksize_bits;

	for (i = 0; i < size; i++) {
		int block;

		block = adfs_parent_bmap (inode, i);
		if (block)
			bhp[i] = bread (sb->s_dev, block, sb->s_blocksize);
		else
			adfs_error (sb, "adfs_dir_read_parent",
				    "directory %lu with a hole at offset %d", inode->i_ino, i);
		if (!block || !bhp[i]) {
			int j;
			for (j = i - 1; j >= 0; j--)
				brelse (bhp[j]);
			return 0;
		}
	}
	return i;
}

int adfs_dir_read (struct inode *inode, struct buffer_head **bhp)
{
	struct super_block *sb;
	int i, size;

	if (!inode || !S_ISDIR(inode->i_mode))
		return 0;

	sb = inode->i_sb;

	size = inode->i_size >> sb->s_blocksize_bits;

	for (i = 0; i < size; i++) {
		int block;

		block = adfs_bmap (inode, i);
		if (block)
			bhp[i] = bread (sb->s_dev, block, sb->s_blocksize);
		else
			adfs_error (sb, "adfs_dir_read",
				    "directory %lX,%lX with a hole at offset %d",
				    inode->i_ino, inode->u.adfs_i.file_id, i);
		if (!block || !bhp[i]) {
			int j;
			for (j = i - 1; j >= 0; j--)
				brelse (bhp[j]);
			return 0;
		}
	}
	return i;
}

int adfs_dir_check (struct inode *inode, struct buffer_head **bhp, int buffers, union adfs_dirtail *dtp)
{
	struct adfs_dirheader dh;
	union adfs_dirtail dt;

	memcpy (&dh, bhp[0]->b_data, sizeof (dh));
	memcpy (&dt, bhp[3]->b_data + 471, sizeof(dt));

	if (memcmp (&dh.startmasseq, &dt.new.endmasseq, 5) ||
	    (memcmp (&dh.startname, "Nick", 4) &&
	     memcmp (&dh.startname, "Hugo", 4))) {
		adfs_error (inode->i_sb, "adfs_check_dir",
			    "corrupted directory inode %lX,%lX",
			    inode->i_ino, inode->u.adfs_i.file_id);
		return 1;
	}
	if (dtp)
		*dtp = dt;
	return 0;
}

void adfs_dir_free (struct buffer_head **bhp, int buffers)
{
	int i;

	for (i = buffers - 1; i >= 0; i--)
		brelse (bhp[i]);
}

int adfs_dir_get (struct super_block *sb, struct buffer_head **bhp,
		  int buffers, int pos, unsigned long parent_object_id,
		  struct adfs_idir_entry *ide)
{
	struct adfs_direntry de;
	int thissize, buffer, offset;

	offset = pos & (sb->s_blocksize - 1);
	buffer = pos >> sb->s_blocksize_bits;

	if (buffer > buffers)
		return 0;

	thissize = sb->s_blocksize - offset;
	if (thissize > 26)
		thissize = 26;

	memcpy (&de, bhp[buffer]->b_data + offset, thissize);
	if (thissize != 26)
		memcpy (((char *)&de) + thissize, bhp[buffer + 1]->b_data, 26 - thissize);

	if (!de.dirobname[0])
		return 0;

	ide->name_len =	adfs_readname (ide->name, de.dirobname, ADFS_NAME_LEN);
	ide->inode_no = adfs_inode_generate (parent_object_id, pos);
	ide->file_id  = adfs_val (de.dirinddiscadd, 3);
	ide->size     = adfs_val (de.dirlen, 4);
	ide->mode     = de.newdiratts;
	ide->mtime    = adfs_time (adfs_val (de.dirload, 4), adfs_val (de.direxec, 4));
	ide->filetype = (adfs_val (de.dirload, 4) >> 8) & 0xfff;
	return 1;
}

int adfs_dir_find_entry (struct super_block *sb, struct buffer_head **bhp,
			 int buffers, unsigned int pos,
			 struct adfs_idir_entry *ide)
{
	struct adfs_direntry de;
	int offset, buffer, thissize;

	offset = pos & (sb->s_blocksize - 1);
	buffer = pos >> sb->s_blocksize_bits;

	if (buffer > buffers)
		return 0;

	thissize = sb->s_blocksize - offset;
	if (thissize > 26)
		thissize = 26;

	memcpy (&de, bhp[buffer]->b_data + offset, thissize);
	if (thissize != 26)
		memcpy (((char *)&de) + thissize, bhp[buffer + 1]->b_data, 26 - thissize);

	if (!de.dirobname[0])
		return 0;

	ide->name_len =	adfs_readname (ide->name, de.dirobname, ADFS_NAME_LEN);
	ide->size     = adfs_val (de.dirlen, 4);
	ide->mode     = de.newdiratts;
	ide->file_id  = adfs_val (de.dirinddiscadd, 3);
	ide->mtime    = adfs_time (adfs_val (de.dirload, 4), adfs_val (de.direxec, 4));
	ide->filetype = (adfs_val (de.dirload, 4) >> 8) & 0xfff;
	return 1;
}	

static int adfs_readdir (struct file *filp, void *dirent, filldir_t filldir)
{
	struct inode *inode = filp->f_dentry->d_inode;
	struct super_block *sb;
	struct buffer_head *bh[4];
	union  adfs_dirtail dt;
	unsigned long parent_object_id, dir_object_id;
	int buffers, pos;

	if (!inode || !S_ISDIR(inode->i_mode))
		return -EBADF;
	sb = inode->i_sb;

	if (filp->f_pos > ADFS_NUM_DIR_ENTRIES + 2)
		return -ENOENT;

	if (!(buffers = adfs_dir_read (inode, bh))) {
		adfs_error (sb, "adfs_readdir", "unable to read directory");
		return -EINVAL;
	}

	if (adfs_dir_check (inode, bh, buffers, &dt)) {
		adfs_dir_free (bh, buffers);
		return -ENOENT;
	}

	parent_object_id = adfs_val (dt.new.dirparent, 3);
	dir_object_id = adfs_inode_objid (inode);

	if (filp->f_pos < 2) {
		if (filp->f_pos < 1) {
			if (filldir (dirent, ".", 1, 0, inode->i_ino) < 0)
				return 0;
			filp->f_pos ++;
		}
		if (filldir (dirent, "..", 2, 1,
			     adfs_inode_generate (parent_object_id, 0)) < 0)
			return 0;
		filp->f_pos ++;
	}

	pos = 5 + (filp->f_pos - 2) * 26;
	while (filp->f_pos < 79) {
		struct adfs_idir_entry ide;

		if (!adfs_dir_get (sb, bh, buffers, pos, dir_object_id, &ide))
			break;

		if (filldir (dirent, ide.name, ide.name_len, filp->f_pos, ide.inode_no) < 0)
			return 0;
		filp->f_pos ++;
		pos += 26;
	}
	adfs_dir_free (bh, buffers);
	return 0;
}
