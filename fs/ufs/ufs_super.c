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
 * $Id: ufs_super.c,v 1.21 1996/12/29 20:48:55 davem Exp $
 *
 */

/*
 * Kernel module support added on 96/04/26 by
 * Stefan Reinauer <stepan@home.culture.mipt.ru>
 *
 * Module usage counts added on 96/04/29 by
 * Gertjan van Wingerde <gertjan@cs.vu.nl>
 */

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/ufs_fs.h>
#include <linux/locks.h>

#include <asm/uaccess.h>

int ufs_need_swab = 0;

struct super_block * ufs_read_super(struct super_block * sb, void * data, int silent);
void ufs_put_super (struct super_block * sb);
void ufs_statfs(struct super_block * sb, struct statfs * buf, int bufsize);

static struct super_operations ufs_super_ops = {
	ufs_read_inode,
	NULL,			/* notify_change() */
	NULL,			/* XXX - ufs_write_inode() */
	ufs_put_inode,
	ufs_put_super,
	NULL,			/* XXX - ufs_write_super() */
	ufs_statfs,
	NULL,			/* XXX - ufs_remount() */
};

static struct file_system_type ufs_fs_type = {
	ufs_read_super, "ufs", 1, NULL
};

int
init_ufs_fs(void)
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

#if 0 /* unused */
static void
ufs_print_super_stuff(struct super_block * sb, struct ufs_superblock * usb)
{

	printk("fs_sblkno: 0x%8.8x\n", usb->fs_sblkno);
	printk("fs_size: 0x%8.8x\n", usb->fs_size);
	printk("fs_ncg: 0x%8.8x\n", usb->fs_ncg);
	printk("fs_bsize: 0x%8.8x\n", usb->fs_bsize);
	printk("fs_frag: 0x%8.8x\n", usb->fs_frag);
	printk("fs_nindir: 0x%8.8x\n", usb->fs_nindir);
	printk("fs_inopb: 0x%8.8x\n", usb->fs_inopb);
	printk("fs_optim: 0x%8.8x\n", usb->fs_optim);
	printk("fs_ncyl: 0x%8.8x\n", usb->fs_ncyl);
	printk("fs_state: 0x%8.8x\n", usb->fs_state);
	printk("fs_magic: 0x%8.8x\n", usb->fs_magic);
	printk("fs_fsmnt: `%s'\n", usb->fs_fsmnt);

	return;
}
#endif

struct super_block *
ufs_read_super(struct super_block * sb, void * data, int silent)
{
	struct ufs_superblock * usb;
	struct buffer_head * bh1, *bh2;

	/* sb->s_dev and sb->s_flags are set by our caller
	 * data is the mystery argument to sys_mount()
	 *
	 * Our caller also sets s_dev, s_covered, s_rd_only, s_dirt,
	 *   and s_type when we return.
	 */

	MOD_INC_USE_COUNT;
	lock_super (sb);

	/* XXX - make everything read only for testing */
	sb->s_flags |= MS_RDONLY;

	if (!(bh1 = bread(sb->s_dev, UFS_SBLOCK/BLOCK_SIZE, BLOCK_SIZE)) ||
	    !(bh2 = bread(sb->s_dev, (UFS_SBLOCK + BLOCK_SIZE)/BLOCK_SIZE,
	                  BLOCK_SIZE))) {
	        if (bh1) {
	                brelse(bh1);
	        }
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

	ufs_need_swab = 0;
	sb->s_magic = ufs_swab32(usb->fs_magic);
	if (sb->s_magic != UFS_MAGIC) {
		ufs_need_swab = 1;
		sb->s_magic = ufs_swab32(usb->fs_magic);
		if (sb->s_magic != UFS_MAGIC) {
	                printk ("ufs_read_super: bad magic number 0x%8.8lx "
				"on dev %d/%d\n", sb->s_magic,
				MAJOR(sb->s_dev), MINOR(sb->s_dev));

			goto ufs_read_super_lose;
		}
	}

	/* We found a UFS filesystem on this device. */

	/* XXX - parse args */

	if (ufs_swab32(usb->fs_bsize) != UFS_BSIZE) {
	        printk("ufs_read_super: fs_bsize %d != %d\n", ufs_swab32(usb->fs_bsize),
	               UFS_BSIZE);
	        goto ufs_read_super_lose;
	}

	if (ufs_swab32(usb->fs_fsize) != UFS_FSIZE) {
	        printk("ufs_read_super: fs_fsize %d != %d\n", ufs_swab32(usb->fs_fsize),
	               UFS_FSIZE);
	        goto ufs_read_super_lose;
	}

#ifdef DEBUG_UFS_SUPER
	printk("ufs_read_super: fs last mounted on \"%s\"\n", usb->fs_fsmnt);
#endif

	if (ufs_swab32(usb->fs_state) == UFS_FSOK - ufs_swab32(usb->fs_time)) {
	        switch(usb->fs_clean) {
	                case UFS_FSCLEAN:
#ifdef DEBUG_UFS_SUPER
	                  printk("ufs_read_super: fs is clean\n");
#endif
	                  break;
	                case UFS_FSSTABLE:
#ifdef DEBUG_UFS_SUPER
	                  printk("ufs_read_super: fs is stable\n");
#endif
	                  break;
	                case UFS_FSACTIVE:
	                  printk("ufs_read_super: fs is active\n");
	                  sb->s_flags |= MS_RDONLY;
	                  break;
	                case UFS_FSBAD:
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
	sb->s_blocksize = ufs_swab32(usb->fs_fsize);
	sb->s_blocksize_bits = ufs_swab32(usb->fs_fshift);
	/* XXX - sb->s_lock */
	sb->s_op = &ufs_super_ops;
	sb->dq_op = 0; /* XXX */
	/* KRR - defined above - sb->s_magic = usb->fs_magic; */
	/* sb->s_time */
	/* sb->s_wait */
	/* XXX - sb->u.ufs_sb */
	sb->u.ufs_sb.s_raw_sb = usb; /* XXX - maybe move this to the top */
	sb->u.ufs_sb.s_flags = 0;
	sb->u.ufs_sb.s_ncg = ufs_swab32(usb->fs_ncg);
	sb->u.ufs_sb.s_ipg = ufs_swab32(usb->fs_ipg);
	sb->u.ufs_sb.s_fpg = ufs_swab32(usb->fs_fpg);
	sb->u.ufs_sb.s_fsize = ufs_swab32(usb->fs_fsize);
	sb->u.ufs_sb.s_fmask = ufs_swab32(usb->fs_fmask);
	sb->u.ufs_sb.s_fshift = ufs_swab32(usb->fs_fshift);
	sb->u.ufs_sb.s_bsize = ufs_swab32(usb->fs_bsize);
	sb->u.ufs_sb.s_bmask = ufs_swab32(usb->fs_bmask);
	sb->u.ufs_sb.s_bshift = ufs_swab32(usb->fs_bshift);
	sb->u.ufs_sb.s_iblkno = ufs_swab32(usb->fs_iblkno);
	sb->u.ufs_sb.s_dblkno = ufs_swab32(usb->fs_dblkno);
	sb->u.ufs_sb.s_cgoffset = ufs_swab32(usb->fs_cgoffset);
	sb->u.ufs_sb.s_cgmask = ufs_swab32(usb->fs_cgmask);
	sb->u.ufs_sb.s_inopb = ufs_swab32(usb->fs_inopb);
	sb->u.ufs_sb.s_lshift = ufs_swab32(usb->fs_bshift) - ufs_swab32(usb->fs_fshift);
	sb->u.ufs_sb.s_lmask = ~((ufs_swab32(usb->fs_fmask) - ufs_swab32(usb->fs_bmask))
					>> ufs_swab32(usb->fs_fshift));
	sb->u.ufs_sb.s_fsfrag = ufs_swab32(usb->fs_frag); /* XXX - rename this later */
	sb->s_mounted = iget(sb, UFS_ROOTINO);

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

	lock_super (sb);
	/* XXX - sync fs data, set state to ok, and flush buffers */
	sb->s_dev = 0;

	/* XXX - free allocated kernel memory */

	unlock_super (sb);
	MOD_DEC_USE_COUNT;

	return;
}

void ufs_statfs(struct super_block * sb, struct statfs * buf, int bufsiz)
{
	struct statfs tmp;
	struct statfs *sp = &tmp;
	struct ufs_superblock *fsb = sb->u.ufs_sb.s_raw_sb;
	unsigned long used, avail;

        if (sb->u.ufs_sb.s_flags & UFS_DEBUG) {
		printk("ufs_statfs\n"); /* XXX */
        }

	sp->f_type = sb->s_magic;
	sp->f_bsize = sb->s_blocksize;
	sp->f_blocks = ufs_swab32(fsb->fs_dsize);
	sp->f_bfree = ufs_swab32(fsb->fs_cstotal.cs_nbfree) *
			ufs_swab32(fsb->fs_frag) +
			ufs_swab32(fsb->fs_cstotal.cs_nffree);

	avail = sp->f_blocks - (sp->f_blocks / 100) *
			ufs_swab32(fsb->fs_minfree);
	used = sp->f_blocks - sp->f_bfree;
	if (avail > used)
		sp->f_bavail = avail - used;
	else
		sp->f_bavail = 0;

	sp->f_files = sb->u.ufs_sb.s_ncg * sb->u.ufs_sb.s_ipg;
	sp->f_ffree = ufs_swab32(fsb->fs_cstotal.cs_nifree);
	sp->f_fsid.val[0] = ufs_swab32(fsb->fs_id[0]);
	sp->f_fsid.val[1] = ufs_swab32(fsb->fs_id[1]);
	sp->f_namelen = UFS_MAXNAMLEN;

	copy_to_user(buf, sp, bufsiz);
	return;
}
