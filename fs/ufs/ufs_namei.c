/*
 *  linux/fs/ufs/ufs_namei.c
 *
 * Copyright (C) 1996
 * Adrian Rodriguez (adrian@franklins-tower.rutgers.edu)
 * Laboratory for Computer Science Research Computing Facility
 * Rutgers, The State University of New Jersey
 *
 * $Id: ufs_namei.c,v 1.3 1996/04/25 09:12:07 davem Exp $
 *
 */

#include <linux/fs.h>

extern unsigned int ufs_bmap(struct inode * inode, int block); /* XXX */

/*
 * NOTE! unlike strncmp, ext2_match returns 1 for success, 0 for failure.
 * stolen from ext2fs
 */
static int ufs_match (int len, const char * const name, struct ufs_direct * d)
{
	if (!d || len > UFS_MAXNAMLEN)
		return 0;
	/*
	 * "" means "." ---> so paths like "/usr/lib//libc.a" work
	 */
	if (!len && (d->d_namlen == 1) && (d->d_name[0] == '.') &&
	   (d->d_name[1] == '\0'))
		return 1;
	if (len != d->d_namlen)
		return 0;
	return !memcmp(name, d->d_name, len);
}

/* XXX - this is a mess, especially for endianity */
int ufs_lookup (struct inode * dir, const char * name, int len,
	        struct inode ** result)
{
	unsigned long int lfragno, fragno;
	struct buffer_head * bh;
	struct ufs_direct * d;

	/*
	 * Touching /xyzzy in a filesystem toggles debugging messages.
	 */
	if ((len == 5) && !(memcmp(name, "xyzzy", len)) &&
	    (dir->i_ino == UFS_ROOTINO)) {
	        dir->i_sb->u.ufs_sb.s_flags ^= UFS_DEBUG;
	        printk("UFS debugging %s\n",
	               (dir->i_sb->u.ufs_sb.s_flags & UFS_DEBUG) ?
	               "on": "off");
	        return(-ENOENT);
	}

	/*
	 * Touching /xyzzy.i in a filesystem toggles debugging for ufs_inode.c.
	 */
	if ((len == 7) && !(memcmp(name, "xyzzy.i", len)) &&
	    (dir->i_ino == UFS_ROOTINO)) {
	        dir->i_sb->u.ufs_sb.s_flags ^= UFS_DEBUG_INODE;
	        printk("UFS inode debugging %s\n",
	               (dir->i_sb->u.ufs_sb.s_flags & UFS_DEBUG_INODE) ?
	               "on": "off");
	        return(-ENOENT);
	}

	if ((len == 7) && !(memcmp(name, "xyzzy.n", len)) &&
	    (dir->i_ino == UFS_ROOTINO)) {
	        dir->i_sb->u.ufs_sb.s_flags ^= UFS_DEBUG_NAMEI;
	        printk("UFS namei debugging %s\n",
	               (dir->i_sb->u.ufs_sb.s_flags & UFS_DEBUG_NAMEI) ?
	               "on": "off");
	        return(-ENOENT);
	}

	if ((len == 7) && !(memcmp(name, "xyzzy.l", len)) &&
	    (dir->i_ino == UFS_ROOTINO)) {
	        dir->i_sb->u.ufs_sb.s_flags ^= UFS_DEBUG_LINKS;
	        printk("UFS symlink debugging %s\n",
	               (dir->i_sb->u.ufs_sb.s_flags & UFS_DEBUG_LINKS) ?
	               "on": "off");
	        return(-ENOENT);
	}

	if (dir->i_sb->u.ufs_sb.s_flags & (UFS_DEBUG|UFS_DEBUG_NAMEI)) {
	        printk("ufs_lookup: called for ino %lu  name %s\n",
	               dir->i_ino, name);
	}

	/* XXX - do I want i_blocks in 512-blocks or 1024-blocks? */
	for (lfragno = 0; lfragno < (dir->i_blocks)>>1; lfragno++) {
	        fragno = ufs_bmap(dir, lfragno);
	        /* XXX - ufs_bmap() call needs error checking */
	        /* XXX - s_blocksize is actually the UFS frag size */
	        if (dir->i_sb->u.ufs_sb.s_flags & UFS_DEBUG) {
	                printk("ufs_lookup: ino %lu lfragno %lu  fragno %lu\n",
	                       dir->i_ino, lfragno, fragno);
	        }
	        if (fragno == 0) {
	                /* XXX - bug bug bug */
	                return(-ENOENT);
	        }
	        bh = bread(dir->i_dev, fragno, dir->i_sb->s_blocksize);
	        if (bh == NULL) {
	                printk("ufs_lookup: bread failed: ino %lu, lfragno %lu",
	                       dir->i_ino, lfragno);
	                return(-EIO);
	        }
	        d = (struct ufs_direct *)(bh->b_data);
	        while (((char *)d - bh->b_data + d->d_reclen) <=
	               dir->i_sb->s_blocksize) {
	                /* XXX - skip block if d_reclen or d_namlen is 0 */
	                if ((d->d_reclen == 0) || (d->d_namlen == 0)) {
	                        if (dir->i_sb->u.ufs_sb.s_flags & UFS_DEBUG) {
	                                printk("ufs_lookup: skipped space in directory, ino %lu\n",
	                                       dir->i_ino);
	                        }
	                        break;
	                }
	                if (dir->i_sb->u.ufs_sb.s_flags & UFS_DEBUG) {
	                        printk("lfragno 0x%lx  direct d 0x%x  d_ino %u  d_reclen %u  d_namlen %u  d_name `%s'\n",
	                               lfragno, (unsigned int)d, d->d_ino, d->d_reclen, d->d_namlen, d->d_name);
	                }
	                if ((d->d_namlen == len) &&
	                    /* XXX - don't use strncmp() - see ext2fs */
	                    (ufs_match(len, name, d))) {
	                        /* We have a match */
	                        *result = iget(dir->i_sb, d->d_ino);
	                        brelse(bh);
	                        return(0);
	                } else {
	                        /* XXX - bounds checking */
	                        if (dir->i_sb->u.ufs_sb.s_flags & UFS_DEBUG) {
	                                printk("ufs_lookup: wanted (%s,%d) got (%s,%d)\n",
	                                       name, len, d->d_name, d->d_namlen);
	                        }
	                }
	                d = (struct ufs_direct *)((char *)d + d->d_reclen);
	        }
	        brelse(bh);
	}
	return(-ENOENT);
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
