/*
 *  linux/fs/ufs/ufs_super.c
 *
 * Copyright (C) 1996
 * Adrian Rodriguez (adrian@franklins-tower.rutgers.edu)
 * Laboratory for Computer Science Research Computing Facility
 * Rutgers, The State University of New Jersey
 *
 * $Id: ufs_super.c,v 1.3 1996/04/25 09:12:09 davem Exp $
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
#include <linux/module.h>
#include <linux/locks.h>

#include <asm/segment.h>

struct super_block * ufs_read_super(struct super_block * sb, void * data, int silent);
void ufs_put_super (struct super_block * sb);
void ufs_statfs(struct super_block * sb, struct statfs * buf, int bufsize);

extern void ufs_read_inode(struct inode * inode);
extern void ufs_put_inode(struct inode * inode);

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
int init_module(void)
{
	int status;

	if ((status = init_ufs_fs()) == 0)
		register_symtab(0);
	return status;
}

void cleanup_module(void)
{
	unregister_filesystem(&ufs_fs_type);
}
#endif

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
	        sb->s_dev = 0;
	        unlock_super (sb);
	        if (bh1) {
	                brelse(bh1);
	        }
	        printk ("ufs_read_super: unable to read superblock\n");

		MOD_DEC_USE_COUNT;
	        return(NULL);
	}
	/* XXX - redo this so we can free it later... */
	usb = (struct ufs_superblock *)__get_free_page(GFP_KERNEL); 
	if (usb == NULL) {
	        printk ("ufs_read_super: get_free_page() failed\n");
	}

	memcpy((char *)usb, bh1->b_data, BLOCK_SIZE);
	memcpy((char *)usb + BLOCK_SIZE, bh2->b_data,
	       sizeof(struct ufs_superblock) - BLOCK_SIZE);

	brelse(bh1);
	brelse(bh2);

	if (usb->fs_magic != UFS_MAGIC) {
	        /* XXX - replace hard-coded constant with a byte-swap macro */
	        if (usb->fs_magic == 0x54190100) {
	                printk ("ufs_read_super: can't grok byteswapped fs on dev %d/%d\n",
	                        MAJOR(sb->s_dev), MINOR(sb->s_dev));
	                silent = 1;
	        }
	        sb->s_dev = 0;
	        unlock_super (sb);
	        if (!silent)
	                printk ("ufs_read_super: bad magic number 0x%8.8x on dev %d/%d\n",
	                        usb->fs_magic, MAJOR(sb->s_dev),
	                        MINOR(sb->s_dev));
		MOD_DEC_USE_COUNT;
	        return(NULL);
	}

	/* We found a UFS filesystem on this device. */

	/* XXX - parse args */

	if (usb->fs_bsize != UFS_BSIZE) {
	        printk("ufs_read_super: fs_bsize %d != %d\n", usb->fs_bsize,
	               UFS_BSIZE);
	        goto ufs_read_super_lose;
	}

	if (usb->fs_fsize != UFS_FSIZE) {
	        printk("ufs_read_super: fs_fsize %d != %d\n", usb->fs_fsize,
	               UFS_FSIZE);
	        goto ufs_read_super_lose;
	}

	if (usb->fs_nindir != UFS_NINDIR) {
	        printk("ufs_read_super: fs_nindir %d != %d\n", usb->fs_nindir,
	               UFS_NINDIR);
	        printk("ufs_read_super: fucking Sun blows me\n");
	}

	printk("ufs_read_super: fs last mounted on \"%s\"\n", usb->fs_fsmnt);

	if (usb->fs_state == UFS_FSOK - usb->fs_time) {
	        switch(usb->fs_clean) {
	                case UFS_FSCLEAN:
	                  printk("ufs_read_super: fs is clean\n");
	                  break;
	                case UFS_FSSTABLE:
	                  printk("ufs_read_super: fs is stable\n");
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

	sb->s_blocksize = usb->fs_fsize;
	sb->s_blocksize_bits = 10;  /* XXX */
	/* XXX - sb->s_lock */
	sb->s_op = &ufs_super_ops;
	sb->dq_op = 0; /* XXX */
	sb->s_magic = usb->fs_magic;
	/* sb->s_time */
	/* sb->s_wait */
	/* XXX - sb->u.ufs_sb */
	sb->u.ufs_sb.s_raw_sb = usb; /* XXX - maybe move this to the top */
	sb->u.ufs_sb.s_flags = 0;
	sb->u.ufs_sb.s_ncg = usb->fs_ncg;
	sb->u.ufs_sb.s_ipg = usb->fs_ipg;
	sb->u.ufs_sb.s_fpg = usb->fs_fpg;
	sb->u.ufs_sb.s_fsize = usb->fs_fsize;
	sb->u.ufs_sb.s_bsize = usb->fs_bsize;
	sb->u.ufs_sb.s_iblkno = usb->fs_iblkno;
	sb->u.ufs_sb.s_dblkno = usb->fs_dblkno;
	sb->u.ufs_sb.s_cgoffset = usb->fs_cgoffset;
	sb->u.ufs_sb.s_cgmask = usb->fs_cgmask;
	sb->u.ufs_sb.s_inopb = usb->fs_inopb;
	sb->u.ufs_sb.s_fsfrag = usb->fs_frag; /* XXX - rename this later */
	sb->s_mounted = iget(sb, UFS_ROOTINO);

	printk("ufs_read_super: inopb %u\n", sb->u.ufs_sb.s_inopb);
	/*
	 * XXX - read cg structs?
	 */

	unlock_super(sb);
	return(sb);

       ufs_read_super_lose:
	/* XXX - clean up */
	MOD_DEC_USE_COUNT;
	return(NULL);
}

void ufs_put_super (struct super_block * sb)
{

	printk("ufs_put_super\n"); /* XXX */

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

	printk("ufs_statfs\n"); /* XXX */
	tmp.f_type = sb->s_magic;
	tmp.f_bsize = PAGE_SIZE;
	tmp.f_blocks = sb->u.ufs_sb.s_raw_sb->fs_dsize;
	tmp.f_bfree = sb->u.ufs_sb.s_raw_sb->fs_cstotal.cs_nbfree;
	tmp.f_bavail =  sb->u.ufs_sb.s_raw_sb->fs_cstotal.cs_nbfree; /* XXX */
	tmp.f_files = sb->u.ufs_sb.s_ncg * sb->u.ufs_sb.s_ipg;
	tmp.f_ffree = sb->u.ufs_sb.s_raw_sb->fs_cstotal.cs_nifree;
	tmp.f_fsid.val[0] = sb->u.ufs_sb.s_raw_sb->fs_id[0];
	tmp.f_fsid.val[1] = sb->u.ufs_sb.s_raw_sb->fs_id[1];
	tmp.f_namelen = UFS_MAXNAMLEN;
/*        tmp.f_spare[6] */

	memcpy_tofs(buf, &tmp, bufsiz);

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
