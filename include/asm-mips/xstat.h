/* $Id: xstat.h,v 1.1 1998/02/06 12:51:41 jj Exp $
 * xstat.h: sys_xstat/xmknod architecture dependent stuff.
 *
 * Copyright (C) 1998  Jakub Jelinek  (jj@sunsite.mff.cuni.cz)
 */
 
extern __inline__ int cp_xstat(struct inode *inode, struct stat64 *s, unsigned long blocks, int blksize)
{
	struct stat64 tmp;
	memset (&tmp, 0, sizeof(tmp));
	tmp.st_dev = (((__u64)MAJOR(inode->i_dev)) << 32) | MINOR(inode->i_dev);
	tmp.st_ino = inode->i_ino;
	tmp.st_mode = inode->i_mode;
	tmp.st_nlink = inode->i_nlink;
	tmp.st_uid = inode->i_uid;
	tmp.st_gid = inode->i_gid;
	tmp.st_rdev = (((__u64)MAJOR(inode->i_rdev)) << 32) | MINOR(inode->i_rdev);
	tmp.st_size = inode->i_size;
	tmp.st_atim.tv_sec = inode->i_atime;
	tmp.st_mtim.tv_sec = inode->i_mtime;
	tmp.st_ctim.tv_sec = inode->i_ctime;
	tmp.st_blksize = blksize;
	tmp.st_blocks = blocks;
	/* Should I check if all fs names are < 16? All in the kernel tree are */
	if (inode->i_sb)
		strcpy(tmp.st_fstype, inode->i_sb->s_type->name);
	return copy_to_user(s,&tmp,sizeof(tmp));
}

extern __inline__ int get_user_new_dev_t(kdev_t *kdev, __new_dev_t *udev) {
	__new_dev_t ndev;
	if (copy_from_user (&ndev, udev, sizeof(__new_dev_t))) return -EFAULT;
	*kdev = MKDEV((ndev >> 32), (__u32)ndev);
	return 0;
}
