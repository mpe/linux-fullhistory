/*
 *  linux/fs/ufs/ufs_inode.c
 *
 * Copyright (C) 1996
 * Adrian Rodriguez (adrian@franklins-tower.rutgers.edu)
 * Laboratory for Computer Science Research Computing Facility
 * Rutgers, The State University of New Jersey
 *
 * $Id: ufs_inode.c,v 1.3 1996/04/25 09:12:05 davem Exp $
 *
 */

#include <linux/fs.h>
#include <linux/ufs_fs.h>
#include <linux/sched.h>

extern struct inode_operations ufs_file_inode_operations;
extern struct inode_operations ufs_dir_inode_operations;
extern struct inode_operations ufs_symlink_inode_operations;
extern struct file_operations ufs_file_operations;
extern struct file_operations ufs_dir_operations;
extern struct file_operations ufs_symlink_operations;

void ufs_print_inode(struct inode * inode)
{
	printk("ino %lu  mode 0%6.6o  lk %d  uid %d  gid %d  sz %lu  blks %lu  cnt %u\n",
	       inode->i_ino, inode->i_mode, inode->i_nlink, inode->i_uid, inode->i_gid, inode->i_size, inode->i_blocks, inode->i_count);
	printk("  db <0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x>\n",
	       inode->u.ufs_i.ui_db[0], inode->u.ufs_i.ui_db[1],
	       inode->u.ufs_i.ui_db[2], inode->u.ufs_i.ui_db[3],
	       inode->u.ufs_i.ui_db[4], inode->u.ufs_i.ui_db[5],
	       inode->u.ufs_i.ui_db[6], inode->u.ufs_i.ui_db[7],
	       inode->u.ufs_i.ui_db[8], inode->u.ufs_i.ui_db[9],
	       inode->u.ufs_i.ui_db[10], inode->u.ufs_i.ui_db[11]);
	printk("  gen 0x%8.8x ib <0x%x 0x%x 0x%x>\n",
	       inode->u.ufs_i.ui_gen, inode->u.ufs_i.ui_ib[0],
	       inode->u.ufs_i.ui_ib[1], inode->u.ufs_i.ui_ib[2]);
}

/* XXX - ufs_read_inode is a mess */
void ufs_read_inode(struct inode * inode)
{
	struct super_block * sb;
	struct ufs_inode * ufsip;
	struct buffer_head * bh;

	sb = inode->i_sb;

	if (ufs_ino_ok(inode)) {
	        printk("ufs_read_inode: bad inum %lu", inode->i_ino);

	        return;
	}

#if 0
	printk("ufs_read_inode: ino %lu  cg %u  cgino %u  ipg %u  inopb %u\n",
	       inode->i_ino, ufs_ino2cg(inode),
	       (inode->i_ino%sb->u.ufs_sb.s_inopb),
	       sb->u.ufs_sb.s_ipg, sb->u.ufs_sb.s_inopb);
#endif
	bh = bread(inode->i_dev,
	           ufs_cgimin(inode->i_sb, ufs_ino2cg(inode)) +
	           (inode->i_ino%sb->u.ufs_sb.s_ipg)/(sb->u.ufs_sb.s_inopb/sb->u.ufs_sb.s_fsfrag),
	           BLOCK_SIZE);
	if (!bh) {
	        printk("ufs_read_inode: can't read inode %lu from dev %d/%d",
	               inode->i_ino, MAJOR(inode->i_dev), MINOR(inode->i_dev));
	        return;
	}

	ufsip = (struct ufs_inode *)bh->b_data;
	ufsip += (inode->i_ino%(sb->u.ufs_sb.s_inopb/sb->u.ufs_sb.s_fsfrag));

	/*
	 * Copy data to the in-core inode.
	 */
	inode->i_mode = ufsip->ui_mode;
	inode->i_nlink = ufsip->ui_nlink;
	if (inode->i_nlink == 0) {
	        /* XXX */
	        printk("ufs_read_inode: zero nlink ino %lu  dev %u/%u\n",
	               inode->i_ino, MAJOR(inode->i_dev), MINOR(inode->i_dev));
	        inode->i_nlink = 1;
	        printk("ufs_read_inode: fishy ino %lu pblk %lu dev %u/%u\n",
	               inode->i_ino,
	               ufs_cgimin(inode->i_sb, ufs_ino2cg(inode)) +
	               (inode->i_ino%sb->u.ufs_sb.s_ipg)/sb->u.ufs_sb.s_inopb,
	               MAJOR(inode->i_dev), MINOR(inode->i_dev));
	}
	/* XXX - debugging */
	if (ufsip->ui_gen == 0) {
	        printk("ufs_read_inode: zero gen ino %lu pblk %lu dev %u/%u\n",
	               inode->i_ino,
	               ufs_cgimin(inode->i_sb, ufs_ino2cg(inode)) +
	               (inode->i_ino%sb->u.ufs_sb.s_ipg)/sb->u.ufs_sb.s_inopb,
	               MAJOR(inode->i_dev), MINOR(inode->i_dev));
	}
	/*
	 * Since Linux currently only has 16-bit uid_t and gid_t, we can't
	 * really support EFTs.  For the moment, we use 0 as the uid and gid
	 * if an inode has a uid or gid that won't fit in 16 bits.  This way
	 * random users can't get at these files, since they get dynamically
	 * "chown()ed" to root.
	 */
	if (ufsip->ui_suid == UFS_USEEFT) {
	        /* EFT */
	        inode->i_uid = 0;
	        printk("ufs_read_inode: EFT uid %u ino %lu dev %u/%u, using %u\n",
	               ufsip->ui_uid, inode->i_ino, MAJOR(inode->i_dev), MINOR(inode->i_dev),
	               inode->i_uid);
	} else {
	        inode->i_uid = ufsip->ui_suid;
	}
	if (ufsip->ui_suid == UFS_USEEFT) {
	        /* EFT */
	        inode->i_uid = 0;
	        printk("ufs_read_inode: EFT gid %u ino %lu dev %u/%u, using %u\n",
	               ufsip->ui_gid, inode->i_ino, MAJOR(inode->i_dev), MINOR(inode->i_dev),
	               inode->i_gid);
	} else {
	        inode->i_gid = ufsip->ui_sgid;
	}

	/*
	 * Linux i_size is 32 bits, so some files on a UFS filesystem may not
	 * be readable.  I let people access the first 32 bits worth of them.
	 * for the rw code, we may want to mark these inodes as read-only.
	 * XXX - bug Linus to make i_size a __u64 instead of a __u32.
	 */
	inode->u.ufs_i.ui_size = ((__u64)(ufsip->ui_size.val[0])<<32) | (__u64)(ufsip->ui_size.val[1]);
	inode->i_size = ufsip->ui_size.val[1]; /* XXX - endianity */
	if (ufsip->ui_size.val[0] != 0) {
	        inode->i_size = 0xffffffff;
	        printk("ufs_read_inode: file too big ino %lu dev %u/%u, faking size\n",
	               inode->i_ino, MAJOR(inode->i_dev), MINOR(inode->i_dev));
	}
	/*
	 * Linux doesn't keep tv_usec around in the kernel, so we discard it.
	 * XXX - I'm not sure what I should do about writing things.  I may
	 * want to keep this data, but for the moment I think I'll just write
	 * zeros for these fields when writing out inodes.
	 */
	inode->i_atime = ufsip->ui_atime.tv_sec;
	inode->i_mtime = ufsip->ui_mtime.tv_sec;
	inode->i_ctime = ufsip->ui_ctime.tv_sec;
	inode->i_blksize = sb->u.ufs_sb.s_fsize;
	inode->i_blocks = ufsip->ui_blocks;
	inode->i_version = ++event; /* see linux/kernel/sched.c */

	if (S_ISREG(inode->i_mode)) {
	        inode->i_op = &ufs_file_inode_operations;
	} else if (S_ISDIR(inode->i_mode)) {
	        inode->i_op = &ufs_dir_inode_operations;
	} else if (S_ISLNK(inode->i_mode)) {
	        inode->i_op = &ufs_symlink_inode_operations;
	} else if (S_ISCHR(inode->i_mode)) {
	        inode->i_op = &chrdev_inode_operations;
	} else if (S_ISBLK(inode->i_mode)) {
	        inode->i_op = &blkdev_inode_operations;
	} else if (S_ISFIFO(inode->i_mode)) {
	        init_fifo(inode);
	} else {
	        printk("ufs_read_inode: unknown file type 0%o ino %lu dev %d/%d\n",
	               inode->i_mode, inode->i_ino, MAJOR(inode->i_dev),
	               MINOR(inode->i_dev));
	        /* XXX - debugging */
	        ufs_print_inode(inode);
	        inode->i_op = &ufs_file_inode_operations;
	}

	/*
	 * ufs_read_super makes sure that UFS_NDADDR and UFS_NINDIR are sane.
	 */
	if (S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
	    S_ISLNK(inode->i_mode)) {
	        int i;

	        for (i = 0; i < UFS_NDADDR; i++) {
	                inode->u.ufs_i.ui_db[i] = ufsip->ui_db[i];
	        }
	        for (i = 0; i < UFS_NINDIR; i++) {
	                inode->u.ufs_i.ui_ib[i] = ufsip->ui_ib[i];
	        }
	}

	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode)) {
	        /* XXX - should be ui_db[1] on little endian ufs filesystems */
	        inode->i_rdev = to_kdev_t(ufsip->ui_db[0]);
	}

	/* XXX - implement fast and slow symlinks */

	inode->u.ufs_i.ui_flags = ufsip->ui_flags;
	inode->u.ufs_i.ui_gen = ufsip->ui_gen; /* XXX - is this i_version? */
	inode->u.ufs_i.ui_shadow = ufsip->ui_shadow; /* XXX */
	inode->u.ufs_i.ui_uid = ufsip->ui_uid;
	inode->u.ufs_i.ui_gid = ufsip->ui_gid;
	inode->u.ufs_i.ui_oeftflag = ufsip->ui_oeftflag;

	brelse(bh);

	if (inode->i_sb->u.ufs_sb.s_flags & (UFS_DEBUG|UFS_DEBUG_INODE)) {
	        ufs_print_inode(inode);
	}

	return;
}

void ufs_put_inode (struct inode * inode)
{
	if (inode->i_nlink)
	        return;

	printk("ufs_put_inode: nlink == 0 for inum %lu on dev %d/%d\n",
	       inode->i_ino, MAJOR(inode->i_dev), MINOR(inode->i_dev));
	ufs_print_inode(inode);
	panic("ufs_put_inode: fs is read only, and nlink == 0");

	/* XXX - this code goes here eventually
	inode->i_size = 0;
	if (inode->i_blocks)
	        ufs_truncate(inode);
	ufs_free_inode(inode);
	*/

	return;
}

/*
 * Local Variables: ***
 * c-indent-level: 8 ***
 * c-continued-statement-offset: 8 ***
 * c-brace-offset: -8 ***
 * c-argdecl-indent: 0 ***
 * c-label-offset: -8 ***
 * End: ***
 */
