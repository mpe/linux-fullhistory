/*
 *  linux/fs/hpfs/inode.c
 *
 *  Mikulas Patocka (mikulas@artax.karlin.mff.cuni.cz), 1998-1999
 *
 *  inode VFS functions
 */

#include "hpfs_fn.h"

static const struct file_operations hpfs_file_ops =
{
	NULL,				/* lseek - default */
	generic_file_read,		/* read */
	hpfs_file_write,		/* write */
	NULL,				/* readdir - bad */
	NULL,				/* poll - default */
	NULL,				/* ioctl - default */
	generic_file_mmap,		/* mmap */
	hpfs_open,			/* open */
	NULL,				/* flush */
	hpfs_file_release,		/* release */
	hpfs_file_fsync,		/* fsync */
	NULL,				/* fasync */
	NULL,				/* check_media_change */
	NULL,				/* revalidate */
	NULL,				/* lock */
};

static const struct inode_operations hpfs_file_iops =
{
	(nonconst *) & hpfs_file_ops,	/* default file operations */
	NULL,				/* create */
	NULL,				/* lookup */
	NULL,				/* link */
	NULL,				/* unlink */
	NULL,				/* symlink */
	NULL,				/* mkdir */
	NULL,				/* rmdir */
	NULL,				/* mknod */
	NULL,				/* rename */
	NULL,				/* readlink */
	NULL,				/* follow_link */
	&hpfs_get_block,		/* get_block */
	block_read_full_page,		/* readpage */
	block_write_full_page,		/* writepage */
	hpfs_truncate,			/* truncate */
	NULL,				/* permission */
	NULL,				/* revalidate */
};

static const struct file_operations hpfs_dir_ops =
{
	hpfs_dir_lseek,			/* lseek */
	hpfs_dir_read,			/* read */
	NULL,				/* write - bad */
	hpfs_readdir,			/* readdir */
	NULL,				/* poll - default */
	NULL,				/* ioctl - default */
	NULL,				/* mmap */
	hpfs_open,			/* open */
	NULL,				/* flush */
	hpfs_dir_release,		/* no special release code */
	hpfs_file_fsync,		/* fsync */
	NULL,				/* fasync */
	NULL,				/* check_media_change */
	NULL,				/* revalidate */
	NULL,				/* lock */
};

static const struct inode_operations hpfs_dir_iops =
{
	(nonconst *) & hpfs_dir_ops,	/* default directory file ops */
	hpfs_create,			/* create */
	hpfs_lookup,			/* lookup */
	NULL,				/* link */
	hpfs_unlink,			/* unlink */
	hpfs_symlink,			/* symlink */
	hpfs_mkdir,			/* mkdir */
	hpfs_rmdir,			/* rmdir */
	hpfs_mknod,			/* mknod */
	hpfs_rename,			/* rename */
	NULL,				/* readlink */
	NULL,				/* follow_link */
	NULL,				/* get_block */
	NULL,				/* readpage */
	NULL,				/* writepage */
	NULL,				/* truncate */
	NULL,				/* permission */
	NULL				/* revalidate */
};

const struct inode_operations hpfs_symlink_iops =
{
	readlink:	page_readlink,
	follow_link:	page_follow_link,
	readpage:	hpfs_symlink_readpage
};

void hpfs_read_inode(struct inode *i)
{
	struct buffer_head *bh;
	struct fnode *fnode;
	struct super_block *sb = i->i_sb;
	unsigned char *ea;
	int ea_size;
	i->i_op = 0;
	init_MUTEX(&i->i_hpfs_sem);
	i->i_uid = sb->s_hpfs_uid;
	i->i_gid = sb->s_hpfs_gid;
	i->i_mode = sb->s_hpfs_mode;
	i->i_hpfs_conv = sb->s_hpfs_conv;
	i->i_blksize = 512;
	i->i_size = -1;
	i->i_blocks = -1;
	
	i->i_hpfs_dno = 0;
	i->i_hpfs_n_secs = 0;
	i->i_hpfs_file_sec = 0;
	i->i_hpfs_disk_sec = 0;
	i->i_hpfs_dpos = 0;
	i->i_hpfs_dsubdno = 0;
	i->i_hpfs_ea_mode = 0;
	i->i_hpfs_ea_uid = 0;
	i->i_hpfs_ea_gid = 0;
	i->i_hpfs_ea_size = 0;
	i->i_version = ++event;

	i->i_hpfs_rddir_off = NULL;
	i->i_hpfs_dirty = 0;

	i->i_atime = 0;
	i->i_mtime = 0;
	i->i_ctime = 0;

	if (!i->i_sb->s_hpfs_rd_inode)
		hpfs_error(i->i_sb, "read_inode: s_hpfs_rd_inode == 0");
	if (i->i_sb->s_hpfs_rd_inode == 2) {
		i->i_mode |= S_IFREG;
		i->i_mode &= ~0111;
		i->i_op = (struct inode_operations *) &hpfs_file_iops;
		i->i_nlink = 1;
		return;
	}
	if (!(fnode = hpfs_map_fnode(sb, i->i_ino, &bh))) {
		/*i->i_mode |= S_IFREG;
		i->i_mode &= ~0111;
		i->i_op = (struct inode_operations *) &hpfs_file_iops;
		i->i_nlink = 0;*/
		make_bad_inode(i);
		return;
	}
	if (i->i_sb->s_hpfs_eas) {
		if ((ea = hpfs_get_ea(i->i_sb, fnode, "UID", &ea_size))) {
			if (ea_size == 2) {
				i->i_uid = ea[0] + (ea[1] << 8);
				i->i_hpfs_ea_uid = 1;
			}
			kfree(ea);
		}
		if ((ea = hpfs_get_ea(i->i_sb, fnode, "GID", &ea_size))) {
			if (ea_size == 2) {
				i->i_gid = ea[0] + (ea[1] << 8);
				i->i_hpfs_ea_gid = 1;
			}
			kfree(ea);
		}
		if ((ea = hpfs_get_ea(i->i_sb, fnode, "SYMLINK", &ea_size))) {
			kfree(ea);
			i->i_mode = S_IFLNK | 0777;
			i->i_op = (struct inode_operations *) &hpfs_symlink_iops;
			i->i_nlink = 1;
			i->i_size = ea_size;
			i->i_blocks = 1;
			brelse(bh);
			return;
		}
		if ((ea = hpfs_get_ea(i->i_sb, fnode, "MODE", &ea_size))) {
			int rdev = 0;
			umode_t mode = sb->s_hpfs_mode;
			if (ea_size == 2) {
				mode = ea[0] + (ea[1] << 8);
				i->i_hpfs_ea_mode = 1;
			}
			kfree(ea);
			i->i_mode = mode;
			if (S_ISBLK(mode) || S_ISCHR(mode)) {
				if ((ea = hpfs_get_ea(i->i_sb, fnode, "DEV", &ea_size))) {
					if (ea_size == 4)
						rdev = ea[0] + (ea[1] << 8) + (ea[2] << 16) + (ea[3] << 24);
					kfree(ea);
				}
			}
			if (S_ISBLK(mode) || S_ISCHR(mode) || S_ISFIFO(mode) || S_ISSOCK(mode)) {
				brelse(bh);
				i->i_nlink = 1;
				i->i_size = 0;
				i->i_blocks = 1;
				init_special_inode(i, mode, rdev);
				return;
			}
		}
	}
	if (fnode->dirflag) {
		unsigned n_dnodes, n_subdirs;
		i->i_mode |= S_IFDIR;
		i->i_op = (struct inode_operations *) &hpfs_dir_iops;
		i->i_hpfs_parent_dir = fnode->up;
		i->i_hpfs_dno = fnode->u.external[0].disk_secno;
		if (sb->s_hpfs_chk >= 2) {
			struct buffer_head *bh0;
			if (hpfs_map_fnode(sb, i->i_hpfs_parent_dir, &bh0)) brelse(bh0);
		}
		n_dnodes = 0; n_subdirs = 0;
		hpfs_count_dnodes(i->i_sb, i->i_hpfs_dno, &n_dnodes, &n_subdirs, NULL);
		i->i_blocks = 4 * n_dnodes;
		i->i_size = 2048 * n_dnodes;
		i->i_nlink = 2 + n_subdirs;
	} else {
		i->i_mode |= S_IFREG;
		if (!i->i_hpfs_ea_mode) i->i_mode &= ~0111;
		i->i_op = (struct inode_operations *) &hpfs_file_iops;
		i->i_nlink = 1;
		i->i_size = fnode->file_size;
		i->i_blocks = ((i->i_size + 511) >> 9) + 1;
	}
	brelse(bh);
}

void hpfs_write_inode_ea(struct inode *i, struct fnode *fnode)
{
	if (fnode->acl_size_l || fnode->acl_size_s) {
		/* Some unknown structures like ACL may be in fnode,
		   we'd better not overwrite them */
		hpfs_error(i->i_sb, "fnode %08x has some unknown HPFS386 stuctures", i->i_ino);
	} else if (i->i_sb->s_hpfs_eas >= 2) {
		unsigned char ea[4];
		if ((i->i_uid != i->i_sb->s_hpfs_uid) || i->i_hpfs_ea_uid) {
			ea[0] = i->i_uid & 0xff;
			ea[1] = i->i_uid >> 8;
			hpfs_set_ea(i, fnode, "UID", ea, 2);
			i->i_hpfs_ea_uid = 1;
		}
		if ((i->i_gid != i->i_sb->s_hpfs_gid) || i->i_hpfs_ea_gid) {
			ea[0] = i->i_gid & 0xff;
			ea[1] = i->i_gid >> 8;
			hpfs_set_ea(i, fnode, "GID", ea, 2);
			i->i_hpfs_ea_gid = 1;
		}
		if (!S_ISLNK(i->i_mode))
			if ((i->i_mode != ((i->i_sb->s_hpfs_mode & ~(S_ISDIR(i->i_mode) ? 0 : 0111))
			  | (S_ISDIR(i->i_mode) ? S_IFDIR : S_IFREG))
			  && i->i_mode != ((i->i_sb->s_hpfs_mode & ~(S_ISDIR(i->i_mode) ? 0222 : 0333))
			  | (S_ISDIR(i->i_mode) ? S_IFDIR : S_IFREG))) || i->i_hpfs_ea_mode) {
				ea[0] = i->i_mode & 0xff;
				ea[1] = i->i_mode >> 8;
				hpfs_set_ea(i, fnode, "MODE", ea, 2);
				i->i_hpfs_ea_mode = 1;
			}
		if (S_ISBLK(i->i_mode) || S_ISCHR(i->i_mode)) {
			int d = kdev_t_to_nr(i->i_rdev);
			ea[0] = d & 0xff;
			ea[1] = (d >> 8) & 0xff;
			ea[2] = (d >> 16) & 0xff;
			ea[3] = d >> 24;
			hpfs_set_ea(i, fnode, "DEV", ea, 4);
		}
	}
}

void hpfs_write_inode(struct inode *i)
{
	struct inode *parent;
	if (!i->i_nlink) return;
	if (i->i_ino == i->i_sb->s_hpfs_root) return;
	if (i->i_hpfs_rddir_off && !i->i_count) {
		if (*i->i_hpfs_rddir_off) printk("HPFS: write_inode: some position still there\n");
		kfree(i->i_hpfs_rddir_off);
		i->i_hpfs_rddir_off = NULL;
	}
	i->i_hpfs_dirty = 0;
	hpfs_lock_iget(i->i_sb, 1);
	parent = iget(i->i_sb, i->i_hpfs_parent_dir);
	hpfs_unlock_iget(i->i_sb);
	hpfs_lock_inode(parent);
	hpfs_write_inode_nolock(i);
	hpfs_unlock_inode(parent);
	iput(parent);
}

void hpfs_write_inode_nolock(struct inode *i)
{
	struct buffer_head *bh;
	struct fnode *fnode;
	struct quad_buffer_head qbh;
	struct hpfs_dirent *de;
	if (i->i_ino == i->i_sb->s_hpfs_root) return;
	if (!(fnode = hpfs_map_fnode(i->i_sb, i->i_ino, &bh))) return;
	if (i->i_ino != i->i_sb->s_hpfs_root) {
		if (!(de = map_fnode_dirent(i->i_sb, i->i_ino, fnode, &qbh))) {
			brelse(bh);
			return;
		}
	} else de = NULL;
	if (S_ISREG(i->i_mode)) {
		fnode->file_size = de->file_size = i->i_size;
	} else if (S_ISDIR(i->i_mode)) {
		fnode->file_size = de->file_size = 0;
	}
	hpfs_write_inode_ea(i, fnode);
	if (de) {
		de->write_date = gmt_to_local(i->i_sb, i->i_mtime);
		de->read_date = gmt_to_local(i->i_sb, i->i_atime);
		de->creation_date = gmt_to_local(i->i_sb, i->i_ctime);
		de->read_only = !(i->i_mode & 0222);
		de->ea_size = i->i_hpfs_ea_size;
		hpfs_mark_4buffers_dirty(&qbh);
		hpfs_brelse4(&qbh);
	}
	if (S_ISDIR(i->i_mode)) {
		if ((de = map_dirent(i, i->i_hpfs_dno, "\001\001", 2, NULL, &qbh))) {
			de->write_date = gmt_to_local(i->i_sb, i->i_mtime);
			de->read_date = gmt_to_local(i->i_sb, i->i_atime);
			de->creation_date = gmt_to_local(i->i_sb, i->i_ctime);
			de->read_only = !(i->i_mode & 0222);
			de->ea_size = /*i->i_hpfs_ea_size*/0;
			de->file_size = 0;
			hpfs_mark_4buffers_dirty(&qbh);
			hpfs_brelse4(&qbh);
		} else hpfs_error(i->i_sb, "directory %08x doesn't have '.' entry", i->i_ino);
	}
	mark_buffer_dirty(bh, 1);
	brelse(bh);
}

int hpfs_notify_change(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;
	int error;
	if (inode->i_sb->s_hpfs_root == inode->i_ino) return -EINVAL;
	if ((error = inode_change_ok(inode, attr))) return error;
	inode_setattr(inode, attr);
	hpfs_write_inode(inode);
	return 0;
}

void hpfs_write_if_changed(struct inode *inode)
{
	if (inode->i_hpfs_dirty) {
		hpfs_write_inode(inode);
	}
}

void hpfs_delete_inode(struct inode *inode)
{
	hpfs_remove_fnode(inode->i_sb, inode->i_ino);
}
