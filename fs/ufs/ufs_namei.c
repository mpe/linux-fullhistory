/*
 *  linux/fs/ufs/ufs_namei.c
 *
 * Copyright (C) 1996
 * Adrian Rodriguez (adrian@franklins-tower.rutgers.edu)
 * Laboratory for Computer Science Research Computing Facility
 * Rutgers, The State University of New Jersey
 *
 * Clean swab support by Francois-Rene Rideau <rideau@ens.fr> 19970406
 * Ported to 2.1.62 by Francois-Rene Rideau <rideau@ens.fr> 19971109
 *
 * 4.4BSD (FreeBSD) support added on February 1st 1998 by
 * Niels Kristian Bech Jensen <nkbj@image.dk> partially based
 * on code by Martin von Loewis <martin@mira.isdn.cs.tu-berlin.de>.
 */

#include <linux/fs.h>
#include <linux/ufs_fs.h>
#include <linux/string.h>
#include "ufs_swab.h"

/*
 * NOTE! unlike strncmp, ext2_match returns 1 for success, 0 for failure.
 * stolen from ext2fs
 * NOTE2: flags *is* used, through this is hidden by macros like SWAB16.
 */
static int ufs_match (int len, const char * const name, struct ufs_direct * d, __u32 flags)
{
	if (!d || len > UFS_MAXNAMLEN) /* XXX - name space */
		return 0;
	/*
	 * "" means "." ---> so paths like "/usr/lib//libc.a" work
	 */
	if (!len && (NAMLEN(d) == 1) && (d->d_name[0] == '.') &&
  	   (d->d_name[1] == '\0'))
  		return 1;
	if (len != NAMLEN(d))
		return 0;
	return !memcmp(name, d->d_name, len);
}

int ufs_lookup (struct inode *dir, struct dentry *dentry)
{
  /* XXX - this is all fucked up! */
	unsigned long int lfragno, fragno;
	struct buffer_head * bh;
	struct ufs_direct * d;
	struct super_block * sb = dir->i_sb;
	const char *name = dentry->d_name.name;
	int len = dentry->d_name.len;
	__u32 flags;
        struct inode *inode;

	/* XXX - isn't that already done by the upper layer? */
        if (!dir || !S_ISDIR(dir->i_mode))
		return -EBADF;

	flags = sb->u.ufs_sb.s_flags;

	if (flags & UFS_DEBUG)
		printk("Passed name: %s\nPassed length: %d\n", name, len);

	/* debugging hacks:
	 * Touching /xyzzy in a filesystem toggles debugging messages.
	 */
	if ((len == 5) && !(memcmp(name, "xyzzy", len)) &&
	    (dir->i_ino == UFS_ROOTINO)) {
	        sb->u.ufs_sb.s_flags ^= UFS_DEBUG;
	        printk("UFS debugging %s\n",
	               (sb->u.ufs_sb.s_flags & UFS_DEBUG) ?
	               "on": "off");
		goto not_found;
	        /*return(-ENOENT);*/
	}

	/*
	 * Touching /xyzzy.i in a filesystem toggles debugging for ufs_inode.c
	 */
	if ((len == 7) && !(memcmp(name, "xyzzy.i", len)) &&
	    (dir->i_ino == UFS_ROOTINO)) {
	        sb->u.ufs_sb.s_flags ^= UFS_DEBUG_INODE;
	        printk("UFS inode debugging %s\n",
	               (sb->u.ufs_sb.s_flags & UFS_DEBUG_INODE) ?
	               "on": "off");
		goto not_found;
	        /*return(-ENOENT);*/
	}

	/*
	 * Touching /xyzzy.n in a filesystem toggles debugging for ufs_namei.c
	 */
	if ((len == 7) && !(memcmp(name, "xyzzy.n", len)) &&
	    (dir->i_ino == UFS_ROOTINO)) {
	        sb->u.ufs_sb.s_flags ^= UFS_DEBUG_NAMEI;
	        printk("UFS namei debugging %s\n",
	               (sb->u.ufs_sb.s_flags & UFS_DEBUG_NAMEI) ?
	               "on": "off");
		goto not_found;
	        /*return(-ENOENT);*/
	}

	/*
	 * Touching /xyzzy.l in a filesystem toggles debugging for ufs_symlink.c
	 */
	if ((len == 7) && !(memcmp(name, "xyzzy.l", len)) &&
	    (dir->i_ino == UFS_ROOTINO)) {
	        sb->u.ufs_sb.s_flags ^= UFS_DEBUG_LINKS;
	        printk("UFS symlink debugging %s\n",
	               (sb->u.ufs_sb.s_flags & UFS_DEBUG_LINKS) ?
	               "on": "off");
		goto not_found;
	        /*return(-ENOENT);*/
	}

	/* Now for the real thing */

 	if (flags & (UFS_DEBUG|UFS_DEBUG_NAMEI)) {
	        printk("ufs_lookup: called for ino %lu  name %s\n",
	               dir->i_ino, name);
	}

 	for (lfragno = 0; lfragno < dir->i_blocks; lfragno++) {
	        fragno = ufs_bmap(dir, lfragno);
 		/* ufs_bmap() reads the block (frag) size in s_blocksize */
	        /* XXX - ufs_bmap() call needs error checking */
 	        if (flags & UFS_DEBUG) {
	                printk("ufs_lookup: ino %lu lfragno %lu  fragno %lu\n",
	                       dir->i_ino, lfragno, fragno);
	        }
	        if (fragno == 0) {
	                /* XXX - bug bug bug */
			goto not_found;
			/*return(-ENOENT);*/
	        }
	        bh = bread(dir->i_dev, fragno, sb->s_blocksize);
	        if (bh == NULL) {
	                printk("ufs_lookup: bread failed: "
                               "ino %lu, lfragno %lu",
	                       dir->i_ino, lfragno);
	                return(-EIO);
	        }
	        d = (struct ufs_direct *)(bh->b_data);
	        while (((char *)d - bh->b_data + SWAB16(d->d_reclen)) <=
	               sb->s_blocksize) {
	                /* XXX - skip block if d_reclen or d_namlen is 0 */
	                if ((d->d_reclen == 0) || (NAMLEN(d) == 0)) {
			/* no need to SWAB16(): test against 0 */
	                        if (flags & UFS_DEBUG) {
	                                printk("ufs_lookup: skipped space in directory, ino %lu\n",
	                                       dir->i_ino);
	                        }
	                        break;
	                }
	                if (flags & UFS_DEBUG) {
	                        printk("lfragno 0x%lx  "
					"direct d 0x%x  "
					"d_ino %u  "
					"d_reclen %u  "
					"d_namlen %u  d_name `%s'\n",
	                        	lfragno,
				        (unsigned int)((unsigned long)d),
					SWAB32(d->d_ino),
					SWAB16(d->d_reclen),
					NAMLEN(d),d->d_name);
	                }
	                if ((NAMLEN(d) == len) &&
	                    /* XXX - don't use strncmp() - see ext2fs */
	                    (ufs_match(len, name, d, flags))) {
	                        /* We have a match */
/* XXX - I only superficially understand how things work,
 * so use at your own risk... -- Fare'
 */
				inode = iget(sb, SWAB32(d->d_ino));
				brelse(bh);
				if(!inode) { return -EACCES; }
                                d_add(dentry,inode);
	                        return(0);
	                } else {
	                        /* XXX - bounds checking */
	                        if (flags & UFS_DEBUG) {
	                                printk("ufs_lookup: "
						"wanted (%s,%d) got (%s,%d)\n",
	                                       name, len,
					       d->d_name, NAMLEN(d));
	                        }
	                }
			d = (struct ufs_direct *)((char *)d +
			     SWAB16(d->d_reclen));
	        }
	        brelse(bh);
	}
   not_found:
	d_add(dentry,NULL);
	return(0);
}
