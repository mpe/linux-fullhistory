/*
 *  linux/fs/ufs/ufs_super.c
 *
 * Copyright (C) 1996
 * Adrian Rodriguez (adrian@franklins-tower.rutgers.edu)
 * Laboratory for Computer Science Research Computing Facility
 * Rutgers, The State University of New Jersey
 *
 * Copyright (C) 1996  Eddie C. Dost  (ecd@skynet.be)
 *
 */

/*
 * Kernel module support added on 96/04/26 by
 * Stefan Reinauer <stepan@home.culture.mipt.ru>
 *
 * Module usage counts added on 96/04/29 by
 * Gertjan van Wingerde <gertjan@cs.vu.nl>
 *
 * Clean swab support on 19970406 by
 * Francois-Rene Rideau <rideau@ens.fr>
 *
 * 4.4BSD (FreeBSD) support added on February 1st 1998 by
 * Niels Kristian Bech Jensen <nkbj@image.dk> partially based
 * on code by Martin von Loewis <martin@mira.isdn.cs.tu-berlin.de>.
 *
 * NeXTstep support added on February 5th 1998 by
 * Niels Kristian Bech Jensen <nkbj@image.dk>.
 */

#undef DEBUG_UFS_SUPER
/*#define DEBUG_UFS_SUPER 1*/
/* Uncomment the line above when hacking ufs superblock code */

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/ufs_fs.h>
#include <linux/locks.h>
#include <linux/init.h>
#include <asm/uaccess.h>

#include "ufs_swab.h"

struct super_block * ufs_read_super(struct super_block * sb, void * data, int silent);
void ufs_put_super (struct super_block * sb);
int ufs_statfs(struct super_block * sb, struct statfs * buf, int bufsize);

static struct super_operations ufs_super_ops = {
	ufs_read_inode,
	NULL,			/* XXX - ufs_write_inode() */
	ufs_put_inode,
	NULL,			/* XXX - ufs_delete_inode() */
	NULL,			/* XXX - notify_change() */
	ufs_put_super,
	NULL,			/* XXX - ufs_write_super() */
	ufs_statfs,
	NULL,			/* XXX - ufs_remount() */
};

static struct file_system_type ufs_fs_type = {
	"ufs",
	FS_REQUIRES_DEV,
	ufs_read_super,
	NULL
};

__initfunc(int init_ufs_fs(void))
{
	return(register_filesystem(&ufs_fs_type));
}

#ifdef MODULE
EXPORT_NO_SYMBOLS;

int init_module(void)
{
	return init_ufs_fs();
}

void cleanup_module(void)
{
	unregister_filesystem(&ufs_fs_type);
}
#endif

static char error_buf[1024];

void ufs_warning (struct super_block * sb, const char * function,
		  const char * fmt, ...)
{
	va_list args;

	va_start (args, fmt);
	vsprintf (error_buf, fmt, args);
	va_end (args);
	printk (KERN_WARNING "UFS warning (device %s): %s: %s\n",
		kdevname(sb->s_dev), function, error_buf);
}

#ifdef DEBUG_UFS_SUPER
static void
ufs_print_super_stuff(struct super_block * sb, struct ufs_superblock * usb)
{
	__u32 flags = sb->u.ufs_sb.s_flags;

	printk("fs_sblkno: 0x%8.8x\n", usb->fs_sblkno);
	printk("fs_size:   0x%8.8x\n", usb->fs_size);
	printk("fs_ncg:    0x%8.8x\n", usb->fs_ncg);
	printk("fs_bsize:  0x%8.8x\n", usb->fs_bsize);
	printk("fs_fsize:  0x%8.8x\n", usb->fs_fsize);
	printk("fs_frag:   0x%8.8x\n", usb->fs_frag);
	printk("fs_nindir: 0x%8.8x\n", usb->fs_nindir);
	printk("fs_inopb:  0x%8.8x\n", usb->fs_inopb);
	printk("fs_optim:  0x%8.8x\n", usb->fs_optim);
	printk("fs_ncyl:   0x%8.8x\n", usb->fs_ncyl);
	printk("fs_clean:  0x%8.8x\n", usb->fs_clean);
	printk("fs_state:  0x%8.8x\n", UFS_STATE(usb));
	printk("fs_magic:  0x%8.8x\n", usb->fs_magic);
	printk("fs_fsmnt:  `%s'\n", usb->fs_fsmnt);

	return;
}
#endif

struct super_block *
ufs_read_super(struct super_block * sb, void * data, int silent)
{
	struct ufs_superblock * usb;	/* normalized to local byteorder */
	struct buffer_head * bh1, *bh2;
	__u32 flags = UFS_DEBUG_INITIAL; /* for sb->u.ufs_sb.s_flags */
	static int offsets[] = { 0, 96, 160 };	/* different superblock locations */
	int i;

	/* sb->s_dev and sb->s_flags are set by our caller
	 * data is the mystery argument to sys_mount()
	 *
	 * Our caller also sets s_dev, s_covered, s_rd_only, s_dirt,
	 *   and s_type when we return.
	 */

	MOD_INC_USE_COUNT;
	lock_super (sb);
	set_blocksize (sb->s_dev, BLOCK_SIZE);

	/* XXX - make everything read only for testing */
	sb->s_flags |= MS_RDONLY;

	for (i = 0; i < sizeof(offsets)/sizeof(offsets[0]); i++) {
		if (!(bh1 = bread(sb->s_dev, offsets[i] + UFS_SBLOCK/BLOCK_SIZE,
		            BLOCK_SIZE)) ||
		    !(bh2 = bread(sb->s_dev, offsets[i] +
		            UFS_SBLOCK/BLOCK_SIZE + 1, BLOCK_SIZE))) {
			brelse(bh1);
		        printk ("ufs_read_super: unable to read superblock\n");
			goto ufs_read_super_lose;
		}
		/* XXX - redo this so we can free it later... */
		usb = (struct ufs_superblock *)__get_free_page(GFP_KERNEL);
		if (usb == NULL) {
			brelse(bh1);
			brelse(bh2);
		        printk ("ufs_read_super: get_free_page() failed\n");
			goto ufs_read_super_lose;
		}

		memcpy((char *)usb, bh1->b_data, BLOCK_SIZE);
		memcpy((char *)usb + BLOCK_SIZE, bh2->b_data,
		       sizeof(struct ufs_superblock) - BLOCK_SIZE);

		brelse(bh1);
		brelse(bh2);

		switch (le32_to_cpup(&usb->fs_magic)) {
			case UFS_MAGIC:
				flags |= UFS_LITTLE_ENDIAN;
				ufs_superblock_le_to_cpus(usb);
				goto found;
			case UFS_CIGAM:
				flags |= UFS_BIG_ENDIAN;
				ufs_superblock_be_to_cpus(usb);
				goto found;
				/* usb is now normalized to local byteorder */
			default:
		}
	}
	printk ("ufs_read_super: bad magic number 0x%8.8x "
		"on dev %d/%d\n", usb->fs_magic,
		MAJOR(sb->s_dev), MINOR(sb->s_dev));
	goto ufs_read_super_lose;
found:
#ifdef DEBUG_UFS_SUPER
	printk("ufs_read_super: superblock offset 0x%2.2x\n", offsets[i]);
#endif
	/* We found a UFS filesystem on this device. */

	/* XXX - parse args */

	if ((usb->fs_bsize != 4096) && (usb->fs_bsize != 8192)) {
	        printk("ufs_read_super: invalid fs_bsize = %d\n",
	               usb->fs_bsize);
	        goto ufs_read_super_lose;
	}

	if ((usb->fs_fsize != 512) && (usb->fs_fsize != 1024)) {
	        printk("ufs_read_super: invalid fs_fsize = %d\n",
	               usb->fs_fsize);
	        goto ufs_read_super_lose;
	}
	if (usb->fs_fsize != BLOCK_SIZE) {
		set_blocksize (sb->s_dev, usb->fs_fsize);
	}

	flags |= UFS_VANILLA;
	/* XXX more consistency check */
#ifdef DEBUG_UFS_SUPER
	printk("ufs_read_super: maxsymlinklen 0x%8.8x\n",
	        usb->fs_u.fs_44.fs_maxsymlinklen);
#endif
	if (usb->fs_u.fs_44.fs_maxsymlinklen >= 0) {
        	if (usb->fs_u.fs_44.fs_inodefmt >= UFS_44INODEFMT) {
                	flags |= UFS_44BSD;
                } else {
                	flags |= UFS_OLD;	/* 4.2BSD */
                }
	} else if (offsets[i] > 0) {
		flags |= UFS_NEXT;
	} else {
		flags |= UFS_SUN;
	}

#ifdef DEBUG_UFS_SUPER
	ufs_print_super_stuff(sb, usb);
#endif
	if (    ((flags&UFS_ST_MASK)==UFS_ST_44BSD)
             || ((flags&UFS_ST_MASK)==UFS_ST_OLD)
             || ((flags&UFS_ST_MASK)==UFS_ST_NEXT)
             || ( ((flags&UFS_ST_MASK)==UFS_ST_SUN)
                  && UFS_STATE(usb) == UFS_FSOK - usb->fs_time)) {
		switch(usb->fs_clean) {
			case UFS_FSACTIVE:	/* 0x00 */
				printk("ufs_read_super: fs is active\n");
				sb->s_flags |= MS_RDONLY;
				break;
			case UFS_FSCLEAN:	/* 0x01 */
#ifdef DEBUG_UFS_SUPER
				printk("ufs_read_super: fs is clean\n");
#endif
				break;
			case UFS_FSSTABLE:	/* 0x02 */
#ifdef DEBUG_UFS_SUPER
				printk("ufs_read_super: fs is stable\n");
#endif
				break;
			case UFS_FSOSF1:	/* 0x03 */
				/* XXX is this correct for DEC OSF/1? */
#ifdef DEBUG_UFS_SUPER
				printk("ufs_read_super: fs is clean and stable (OSF/1)\n");
#endif
				break;
			case UFS_FSBAD:		/* 0xFF */
				printk("ufs_read_super: fs is bad\n");
				sb->s_flags |= MS_RDONLY;
				break;
			default:
				printk("ufs_read_super: can't grok fs_clean 0x%x\n",
					usb->fs_clean);
				sb->s_flags |= MS_RDONLY;
				break;
		}
	} else {
		printk("ufs_read_super: fs needs fsck\n");
		sb->s_flags |= MS_RDONLY;
		/* XXX - make it read only or barf if it's not (/, /usr) */
	}

	/* XXX - sanity check sb fields */

	/* KRR - Why are we not using fs_bsize for blocksize? */
	sb->s_blocksize = usb->fs_fsize;
	sb->s_blocksize_bits = usb->fs_fshift;
	/* XXX - sb->s_lock */
	sb->s_op = &ufs_super_ops;
	sb->dq_op = 0; /* XXX */
	sb->s_magic = usb->fs_magic;
	/* sb->s_time */
	/* sb->s_wait */
	/* XXX - sb->u.ufs_sb */
	sb->u.ufs_sb.s_raw_sb = usb; /* XXX - maybe move this to the top */
	sb->u.ufs_sb.s_flags = flags ;
	sb->u.ufs_sb.s_ncg = usb->fs_ncg;
	sb->u.ufs_sb.s_ipg = usb->fs_ipg;
	sb->u.ufs_sb.s_fpg = usb->fs_fpg;
	sb->u.ufs_sb.s_fsize = usb->fs_fsize;
	sb->u.ufs_sb.s_fmask = usb->fs_fmask;
	sb->u.ufs_sb.s_fshift = usb->fs_fshift;
	sb->u.ufs_sb.s_bsize = usb->fs_bsize;
	sb->u.ufs_sb.s_bmask = usb->fs_bmask;
	sb->u.ufs_sb.s_bshift = usb->fs_bshift;
	sb->u.ufs_sb.s_iblkno = usb->fs_iblkno;
	sb->u.ufs_sb.s_dblkno = usb->fs_dblkno;
	sb->u.ufs_sb.s_cgoffset = usb->fs_cgoffset;
	sb->u.ufs_sb.s_cgmask = usb->fs_cgmask;
	sb->u.ufs_sb.s_inopb = usb->fs_inopb;
	sb->u.ufs_sb.s_lshift = usb->fs_bshift - usb->fs_fshift;
	sb->u.ufs_sb.s_lmask = ~((usb->fs_fmask - usb->fs_bmask)
					>> usb->fs_fshift);
	sb->u.ufs_sb.s_fsfrag = usb->fs_frag; /* XXX - rename this later */
	sb->u.ufs_sb.s_blockbase = offsets[i];
	sb->s_root = d_alloc_root(iget(sb, UFS_ROOTINO), NULL);

#ifdef DEBUG_UFS_SUPER
	printk("ufs_read_super: inopb %u\n", sb->u.ufs_sb.s_inopb);
#endif
	/*
	 * XXX - read cg structs?
	 */

	unlock_super(sb);
	return(sb);

ufs_read_super_lose:
	/* XXX - clean up */
	set_blocksize (sb->s_dev, BLOCK_SIZE);
	sb->s_dev = 0;
	unlock_super (sb);
	MOD_DEC_USE_COUNT;
	return(NULL);
}

void ufs_put_super (struct super_block * sb)
{
        if (sb->u.ufs_sb.s_flags & UFS_DEBUG) {
		printk("ufs_put_super\n"); /* XXX */
        }


	/* XXX - sync fs data, set state to ok, and flush buffers */
	set_blocksize (sb->s_dev, BLOCK_SIZE);

	/* XXX - free allocated kernel memory */
	/* includes freeing usb page */

	MOD_DEC_USE_COUNT;

	return;
}

int ufs_statfs(struct super_block * sb, struct statfs * buf, int bufsiz)
{
	struct statfs tmp;
	struct statfs *sp = &tmp;
	struct ufs_superblock *fsb = sb->u.ufs_sb.s_raw_sb;
	/* fsb was already normalized during mounting */
	unsigned long used, avail;

        if (sb->u.ufs_sb.s_flags & UFS_DEBUG) {
		printk("ufs_statfs\n"); /* XXX */
        }

	sp->f_type = sb->s_magic;
	sp->f_bsize = sb->s_blocksize;
	sp->f_blocks = fsb->fs_dsize;
	sp->f_bfree = fsb->fs_cstotal.cs_nbfree *
			fsb->fs_frag +
			fsb->fs_cstotal.cs_nffree;

	avail = sp->f_blocks - (sp->f_blocks / 100) *
			fsb->fs_minfree;
	used = sp->f_blocks - sp->f_bfree;
	if (avail > used)
		sp->f_bavail = avail - used;
	else
		sp->f_bavail = 0;

	sp->f_files = sb->u.ufs_sb.s_ncg * sb->u.ufs_sb.s_ipg;
	sp->f_ffree = fsb->fs_cstotal.cs_nifree;
	sp->f_fsid.val[0] = fsb->fs_id[0];
	sp->f_fsid.val[1] = fsb->fs_id[1];
	sp->f_namelen = UFS_MAXNAMLEN;

	return copy_to_user(buf, sp, bufsiz) ? -EFAULT : 0;
}
