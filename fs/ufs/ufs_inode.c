/*
 *  linux/fs/ufs/ufs_inode.c
 *
 * Copyright (C) 1996
 * Adrian Rodriguez (adrian@franklins-tower.rutgers.edu)
 * Laboratory for Computer Science Research Computing Facility
 * Rutgers, The State University of New Jersey
 *
 * $Id: ufs_inode.c,v 1.8 1997/06/04 08:28:28 davem Exp $
 *
 */

#include <linux/fs.h>
#include <linux/ufs_fs.h>
#include <linux/sched.h>

void ufs_print_inode(struct inode * inode)
{
	printk("ino %lu  mode 0%6.6o  lk %d  uid %d  gid %d"
	       "  sz %lu  blks %lu  cnt %u\n",
	       inode->i_ino, inode->i_mode, inode->i_nlink, inode->i_uid,
	       inode->i_gid, inode->i_size, inode->i_blocks, inode->i_count);
	printk("  db <0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x"
	       " 0x%x 0x%x 0x%x 0x%x>\n",
		inode->u.ufs_i.i_data[0], inode->u.ufs_i.i_data[1],
		inode->u.ufs_i.i_data[2], inode->u.ufs_i.i_data[3],
		inode->u.ufs_i.i_data[4], inode->u.ufs_i.i_data[5],
		inode->u.ufs_i.i_data[6], inode->u.ufs_i.i_data[7],
		inode->u.ufs_i.i_data[8], inode->u.ufs_i.i_data[9],
		inode->u.ufs_i.i_data[10], inode->u.ufs_i.i_data[11]);
	printk("  gen 0x%8.8x ib <0x%x 0x%x 0x%x>\n",
		inode->u.ufs_i.i_gen,
		inode->u.ufs_i.i_data[UFS_IND_BLOCK],
		inode->u.ufs_i.i_data[UFS_DIND_BLOCK],
		inode->u.ufs_i.i_data[UFS_TIND_BLOCK]);
}

#define inode_bmap(inode, nr) ((inode)->u.ufs_i.i_data[(nr)])

static inline int block_bmap (struct inode *inode, int block, int nr)
{
	struct buffer_head *bh;
	int tmp;

	/* XXX Split in fsize big blocks (Can't bread 8Kb). */ 
	tmp = nr >> (inode->i_sb->u.ufs_sb.s_fshift - 2);
	bh = bread (inode->i_dev, block + tmp, inode->i_sb->u.ufs_sb.s_fsize);
	if (!bh)
		return 0;
	nr &= ~(inode->i_sb->u.ufs_sb.s_fmask) >> 2;
	tmp = ufs_swab32(((__u32 *)bh->b_data)[nr]);
	brelse (bh);
	return tmp;
}

int ufs_bmap (struct inode * inode, int block)
{
	int i;
	int addr_per_block = UFS_ADDR_PER_BLOCK(inode->i_sb);
	int addr_per_block_bits = UFS_ADDR_PER_BLOCK_BITS(inode->i_sb);
	int lbn = ufs_lbn (inode->i_sb, block);
	int boff = ufs_boff (inode->i_sb, block);

	if (lbn < 0) {
		ufs_warning (inode->i_sb, "ufs_bmap", "block < 0");
		return 0;
	}
	if (lbn >= UFS_NDADDR + addr_per_block +
		(1 << (addr_per_block_bits * 2)) +
		((1 << (addr_per_block_bits * 2)) << addr_per_block_bits)) {
		ufs_warning (inode->i_sb, "ufs_bmap", "block > big");
		return 0;
	}
	if (lbn < UFS_NDADDR)
		return ufs_dbn (inode->i_sb, inode_bmap (inode, lbn), boff);
	lbn -= UFS_NDADDR;
	if (lbn < addr_per_block) {
		i = inode_bmap (inode, UFS_IND_BLOCK);
		if (!i)
			return 0;
		return ufs_dbn (inode->i_sb, block_bmap (inode, i, lbn), boff);
	}
	lbn -= addr_per_block;
	if (lbn < (1 << (addr_per_block_bits * 2))) {
		i = inode_bmap (inode, UFS_DIND_BLOCK);
		if (!i)
			return 0;
		i = block_bmap (inode, i, lbn >> addr_per_block_bits);
		if (!i)
			return 0;
		return ufs_dbn (inode->i_sb,
				block_bmap (inode, i, lbn & (addr_per_block-1)),
				boff);
	}
	lbn -= (1 << (addr_per_block_bits * 2));
	i = inode_bmap (inode, UFS_TIND_BLOCK);
	if (!i)
		return 0;
	i = block_bmap (inode, i, lbn >> (addr_per_block_bits * 2));
	if (!i)
		return 0;
	i = block_bmap (inode, i,
			(lbn >> addr_per_block_bits) & (addr_per_block - 1));
	if (!i)
		return 0;
	return ufs_dbn (inode->i_sb,
			block_bmap (inode, i, lbn & (addr_per_block-1)), boff);
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
	inode->i_mode = ufs_swab16(ufsip->ui_mode);
	inode->i_nlink = ufs_swab16(ufsip->ui_nlink);
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
	if (ufs_swab32(ufsip->ui_gen) == 0) {
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
	if (ufs_swab16(ufsip->ui_suid) == UFS_USEEFT) {
	        /* EFT */
	        inode->i_uid = 0;
	        printk("ufs_read_inode: EFT uid %u ino %lu dev %u/%u, using %u\n",
	               ufs_swab32(ufsip->ui_uid), inode->i_ino, MAJOR(inode->i_dev), MINOR(inode->i_dev),
	               inode->i_uid);
	} else {
	        inode->i_uid = ufs_swab16(ufsip->ui_suid);
	}
	if (ufs_swab16(ufsip->ui_sgid) == UFS_USEEFT) {
	        /* EFT */
	        inode->i_gid = 0;
	        printk("ufs_read_inode: EFT gid %u ino %lu dev %u/%u, using %u\n",
	               ufs_swab32(ufsip->ui_gid), inode->i_ino, MAJOR(inode->i_dev), MINOR(inode->i_dev),
	               inode->i_gid);
	} else {
	        inode->i_gid = ufs_swab16(ufsip->ui_sgid);
	}

	/*
	 * Linux i_size is 32 bits, so some files on a UFS filesystem may not
	 * be readable.  I let people access the first 32 bits worth of them.
	 * for the rw code, we may want to mark these inodes as read-only.
	 * XXX - bug Linus to make i_size a __u64 instead of a __u32.
	 */
	inode->u.ufs_i.i_size = ((__u64)(ufs_swab32(ufsip->ui_size.val[0]))<<32) |
				 (__u64)(ufs_swab32(ufsip->ui_size.val[1]));
	/* KRR - Just type cast inode->u.ufs_i.i_size into off_t and
	 * worry about overflow later
         */
	inode->i_size = (off_t)inode->u.ufs_i.i_size;

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
	inode->i_blocks = ufs_swab32(ufsip->ui_blocks);
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
	                inode->u.ufs_i.i_data[i] = ufs_swab32(ufsip->ui_db[i]);
	        }
	        for (i = 0; i < UFS_NINDIR; i++) {
	                inode->u.ufs_i.i_data[UFS_IND_BLOCK + i] =
							ufs_swab32(ufsip->ui_ib[i]);
	        }
	}

	/* KRR - I need to check the SunOS header files, but for the time
	 * being, I'm going to tread ui_db[0] and [1] as a __u64 and swab
	 * them appropriately.  This should clean up any real endian problems,
	 * but we'll still need to add size checks in the write portion of
	 * the code.
	 */
	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode)) {
		inode->i_rdev = (kdev_t)((__u64)(ufs_swab32(ufsip->ui_db[0]))<<32) |
				 (__u64)(ufs_swab32(ufsip->ui_db[1]));
	}

	/* XXX - implement fast and slow symlinks */

	inode->u.ufs_i.i_flags = ufs_swab32(ufsip->ui_flags);
	inode->u.ufs_i.i_gen = ufs_swab32(ufsip->ui_gen); /* XXX - is this i_version? */
	inode->u.ufs_i.i_shadow = ufs_swab32(ufsip->ui_shadow); /* XXX */
	inode->u.ufs_i.i_uid = ufs_swab32(ufsip->ui_uid);
	inode->u.ufs_i.i_gid = ufs_swab32(ufsip->ui_gid);
	inode->u.ufs_i.i_oeftflag = ufs_swab32(ufsip->ui_oeftflag);

	brelse(bh);

	if (inode->i_sb->u.ufs_sb.s_flags & (UFS_DEBUG|UFS_DEBUG_INODE)) {
	        ufs_print_inode(inode);
	}

	return;
}

void ufs_put_inode (struct inode * inode)
{
	if (inode->i_sb->u.ufs_sb.s_flags & (UFS_DEBUG|UFS_DEBUG_INODE)) {
		printk("ufs_put_inode:\n");
	        ufs_print_inode(inode);
	}

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

