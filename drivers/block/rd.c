/*
 * ramdisk.c - Multiple RAM disk driver - gzip-loading version - v. 0.8 beta.
 * 
 * (C) Chad Page, Theodore Ts'o, et. al, 1995. 
 *
 * This RAM disk is designed to have filesystems created on it and mounted
 * just like a regular floppy disk.  
 *  
 * It also does something suggested by Linus: use the buffer cache as the
 * RAM disk data.  This makes it possible to dynamically allocate the RAM disk
 * buffer - with some consequences I have to deal with as I write this. 
 * 
 * This code is based on the original ramdisk.c, written mostly by
 * Theodore Ts'o (TYT) in 1991.  The code was largely rewritten by
 * Chad Page to use the buffer cache to store the RAM disk data in
 * 1995; Theodore then took over the driver again, and cleaned it up
 * for inclusion in the mainline kernel.
 *
 * The original CRAMDISK code was written by Richard Lyons, and
 * adapted by Chad Page to use the new RAM disk interface.  Theodore
 * Ts'o rewrote it so that both the compressed RAM disk loader and the
 * kernel decompressor uses the same inflate.c codebase.  The RAM disk
 * loader now also loads into a dynamic (buffer cache based) RAM disk,
 * not the old static RAM disk.  Support for the old static RAM disk has
 * been completely removed.
 *
 * Loadable module support added by Tom Dyas.
 *
 * Further cleanups by Chad Page (page0588@sundance.sjsu.edu):
 *	Cosmetic changes in #ifdef MODULE, code movement, etc.
 * 	When the RAM disk module is removed, free the protected buffers
 * 	Default RAM disk size changed to 2.88 MB
 *
 *  Added initrd: Werner Almesberger & Hans Lermen, Feb '96
 *
* 4/25/96 : Made RAM disk size a parameter (default is now 4 MB) 
 *		- Chad Page
 *
 * Add support for fs images split across >1 disk, Paul Gortmaker, Mar '98
 *
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/minix_fs.h>
#include <linux/ext2_fs.h>
#include <linux/romfs_fs.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/malloc.h>
#include <linux/ioctl.h>
#include <linux/fd.h>
#include <linux/module.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/byteorder.h>

extern void wait_for_keypress(void);

/*
 * 35 has been officially registered as the RAMDISK major number, but
 * so is the original MAJOR number of 1.  We're using 1 in
 * include/linux/major.h for now
 */
#define MAJOR_NR RAMDISK_MAJOR
#include <linux/blk.h>

/* The RAM disk size is now a parameter */
#define NUM_RAMDISKS 16		/* This cannot be overridden (yet) */ 

#ifndef MODULE
/* We don't have to load RAM disks or gunzip them in a module. */
#define RD_LOADER
#define BUILD_CRAMDISK

void rd_load(void);
static int crd_load(struct file *fp, struct file *outfp);

#ifdef CONFIG_BLK_DEV_INITRD
static int initrd_users = 0;
#endif
#endif

/* Various static variables go here.  Most are used only in the RAM disk code.
 */

static int rd_length[NUM_RAMDISKS];
static int rd_blocksizes[NUM_RAMDISKS];

/*
 * Parameters for the boot-loading of the RAM disk.  These are set by
 * init/main.c (from arguments to the kernel command line) or from the
 * architecture-specific setup routine (from the stored boot sector
 * information). 
 */
int rd_size = 4096;		/* Size of the RAM disks */

#ifndef MODULE
int rd_doload = 0;		/* 1 = load RAM disk, 0 = don't load */
int rd_prompt = 1;		/* 1 = prompt for RAM disk, 0 = don't prompt */
int rd_image_start = 0;		/* starting block # of image */
#ifdef CONFIG_BLK_DEV_INITRD
unsigned long initrd_start,initrd_end;
int mount_initrd = 1;		/* zero if initrd should not be mounted */
int initrd_below_start_ok = 0;
#endif
#endif

/*
 *  Basically, my strategy here is to set up a buffer-head which can't be
 *  deleted, and make that my Ramdisk.  If the request is outside of the
 *  allocated size, we must get rid of it...
 *
 */
static void rd_request(void)
{
	unsigned int minor;
	int offset, len;

repeat:
	INIT_REQUEST;
	
	minor = MINOR(CURRENT->rq_dev);

	if (minor >= NUM_RAMDISKS) {
		end_request(0);
		goto repeat;
	}
	
	offset = CURRENT->sector << 9;
	len = CURRENT->current_nr_sectors << 9;

	if ((offset + len) > rd_length[minor]) {
		end_request(0);
		goto repeat;
	}

	/*
	 * If we're reading, fill the buffer with 0's.  This is okay since
         * we're using protected buffers which should never get freed...
	 *
	 * If we're writing, we protect the buffer.
  	 */

	if (CURRENT->cmd == READ) 
		memset(CURRENT->buffer, 0, len); 
	else	
		set_bit(BH_Protected, &CURRENT->bh->b_state);

	end_request(1);
	goto repeat;
} 

static int rd_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	int err;
	
	if (!inode || !inode->i_rdev) 	
		return -EINVAL;

	switch (cmd) {
		case BLKFLSBUF:
			if (!capable(CAP_SYS_ADMIN)) return -EACCES;
			invalidate_buffers(inode->i_rdev);
			break;
         	case BLKGETSIZE:   /* Return device size */
			if (!arg)  return -EINVAL;
			err = verify_area(VERIFY_WRITE, (long *) arg,
					  sizeof(long));
			if (err)
				return err;
			put_user(rd_length[MINOR(inode->i_rdev)] / 512, 
				 (long *) arg);
			return 0;
			
		default:
			break;
	};

	return 0;
}


#ifdef CONFIG_BLK_DEV_INITRD

static ssize_t initrd_read(struct file *file, char *buf,
			   size_t count, loff_t *ppos)
{
	int left;

	left = initrd_end - initrd_start - *ppos;
	if (count > left) count = left;
	if (count == 0) return 0;
	copy_to_user(buf, (char *)initrd_start + *ppos, count);
	*ppos += count;
	return count;
}


static int initrd_release(struct inode *inode,struct file *file)
{
	unsigned long i;

	if (--initrd_users) return 0;
	for (i = initrd_start; i < initrd_end; i += PAGE_SIZE)
		free_page(i);
	initrd_start = 0;
	return 0;
}


static struct file_operations initrd_fops = {
	NULL,		/* lseek */
	initrd_read,	/* read */
	NULL,		/* write */
	NULL,		/* readdir */
	NULL,		/* poll */
	NULL, 		/* ioctl */
	NULL,		/* mmap */
	NULL,		/* open */
	NULL,		/* flush */
	initrd_release,	/* release */
	NULL		/* fsync */ 
};

#endif


static int rd_open(struct inode * inode, struct file * filp)
{
#ifdef CONFIG_BLK_DEV_INITRD
	if (DEVICE_NR(inode->i_rdev) == INITRD_MINOR) {
		if (!initrd_start) return -ENODEV;
		initrd_users++;
		filp->f_op = &initrd_fops;
		return 0;
	}
#endif

	if (DEVICE_NR(inode->i_rdev) >= NUM_RAMDISKS)
		return -ENXIO;

	MOD_INC_USE_COUNT;

	return 0;
}

static int rd_release(struct inode * inode, struct file * filp)
{
	MOD_DEC_USE_COUNT;
	return 0;
}

static struct file_operations fd_fops = {
	NULL,		/* lseek - default */
	block_read,	/* read - block dev read */
	block_write,	/* write - block dev write */
	NULL,		/* readdir - not here! */
	NULL,		/* poll */
	rd_ioctl, 	/* ioctl */
	NULL,		/* mmap */
	rd_open,	/* open */
	NULL,		/* flush */
	rd_release,	/* module needs to decrement use count */
	block_fsync		/* fsync */ 
};

/* This is the registration and initialization section of the RAM disk driver */
__initfunc(int rd_init(void))
{
	int		i;

	if (register_blkdev(MAJOR_NR, "ramdisk", &fd_fops)) {
		printk("RAMDISK: Could not get major %d", MAJOR_NR);
		return -EIO;
	}

	blk_dev[MAJOR_NR].request_fn = &rd_request;

	for (i = 0; i < NUM_RAMDISKS; i++) {
		rd_length[i] = (rd_size * 1024);
		rd_blocksizes[i] = 1024;
	}

	blksize_size[MAJOR_NR] = rd_blocksizes;

	printk("RAM disk driver initialized:  %d RAM disks of %dK size\n",
							NUM_RAMDISKS, rd_size);

	return 0;
}

/* loadable module support */

#ifdef MODULE

int init_module(void)
{
	int error = rd_init();
	if (!error)
		printk(KERN_INFO "RAMDISK: Loaded as module.\n");
	return error;
}

/* Before freeing the module, invalidate all of the protected buffers! */
void cleanup_module(void)
{
	int i;

	for (i = 0 ; i < NUM_RAMDISKS; i++)
		invalidate_buffers(MKDEV(MAJOR_NR, i));

	unregister_blkdev( MAJOR_NR, "ramdisk" );
	blk_dev[MAJOR_NR].request_fn = 0;
}

#endif  /* MODULE */

/* End of non-loading portions of the RAM disk driver */

#ifdef RD_LOADER 
/*
 * This routine tries to find a RAM disk image to load, and returns the
 * number of blocks to read for a non-compressed image, 0 if the image
 * is a compressed image, and -1 if an image with the right magic
 * numbers could not be found.
 *
 * We currently check for the following magic numbers:
 * 	minix
 * 	ext2
 *	romfs
 * 	gzip
 */
__initfunc(int
identify_ramdisk_image(kdev_t device, struct file *fp, int start_block))
{
	const int size = 512;
	struct minix_super_block *minixsb;
	struct ext2_super_block *ext2sb;
	struct romfs_super_block *romfsb;
	int nblocks = -1;
	unsigned char *buf;

	buf = kmalloc(size, GFP_KERNEL);
	if (buf == 0)
		return -1;

	minixsb = (struct minix_super_block *) buf;
	ext2sb = (struct ext2_super_block *) buf;
	romfsb = (struct romfs_super_block *) buf;
	memset(buf, 0xe5, size);

	/*
	 * Read block 0 to test for gzipped kernel
	 */
	if (fp->f_op->llseek)
		fp->f_op->llseek(fp, start_block * BLOCK_SIZE, 0);
	fp->f_pos = start_block * BLOCK_SIZE;
	
	fp->f_op->read(fp, buf, size, &fp->f_pos);

	/*
	 * If it matches the gzip magic numbers, return -1
	 */
	if (buf[0] == 037 && ((buf[1] == 0213) || (buf[1] == 0236))) {
		printk(KERN_NOTICE
		       "RAMDISK: Compressed image found at block %d\n",
		       start_block);
		nblocks = 0;
		goto done;
	}

	/* romfs is at block zero too */
	if (romfsb->word0 == ROMSB_WORD0 &&
	    romfsb->word1 == ROMSB_WORD1) {
		printk(KERN_NOTICE
		       "RAMDISK: romfs filesystem found at block %d\n",
		       start_block);
		nblocks = (ntohl(romfsb->size)+BLOCK_SIZE-1)>>BLOCK_SIZE_BITS;
		goto done;
	}

	/*
	 * Read block 1 to test for minix and ext2 superblock
	 */
	if (fp->f_op->llseek)
		fp->f_op->llseek(fp, (start_block+1) * BLOCK_SIZE, 0);
	fp->f_pos = (start_block+1) * BLOCK_SIZE;

	fp->f_op->read(fp, buf, size, &fp->f_pos);
		
	/* Try minix */
	if (minixsb->s_magic == MINIX_SUPER_MAGIC ||
	    minixsb->s_magic == MINIX_SUPER_MAGIC2) {
		printk(KERN_NOTICE
		       "RAMDISK: Minix filesystem found at block %d\n",
		       start_block);
		nblocks = minixsb->s_nzones << minixsb->s_log_zone_size;
		goto done;
	}

	/* Try ext2 */
	if (ext2sb->s_magic == cpu_to_le16(EXT2_SUPER_MAGIC)) {
		printk(KERN_NOTICE
		       "RAMDISK: ext2 filesystem found at block %d\n",
		       start_block);
		nblocks = le32_to_cpu(ext2sb->s_blocks_count);
		goto done;
	}

	printk(KERN_NOTICE
	       "RAMDISK: Couldn't find valid RAM disk image starting at %d.\n",
	       start_block);
	
done:
	if (fp->f_op->llseek)
		fp->f_op->llseek(fp, start_block * BLOCK_SIZE, 0);
	fp->f_pos = start_block * BLOCK_SIZE;	

	kfree(buf);
	return nblocks;
}

/*
 * This routine loads in the RAM disk image.
 */
__initfunc(static void rd_load_image(kdev_t device,int offset))
{
 	struct inode inode, out_inode;
	struct file infile, outfile;
	struct dentry in_dentry, out_dentry;
	mm_segment_t fs;
	kdev_t ram_device;
	int nblocks, i;
	char *buf;
	unsigned short rotate = 0;
	unsigned short devblocks = 0;
	char rotator[4] = { '|' , '/' , '-' , '\\' };

	ram_device = MKDEV(MAJOR_NR, 0);

	memset(&infile, 0, sizeof(infile));
	memset(&inode, 0, sizeof(inode));
	memset(&in_dentry, 0, sizeof(in_dentry));
	inode.i_rdev = device;
	infile.f_mode = 1; /* read only */
	infile.f_dentry = &in_dentry;
	in_dentry.d_inode = &inode;

	memset(&outfile, 0, sizeof(outfile));
	memset(&out_inode, 0, sizeof(out_inode));
	memset(&out_dentry, 0, sizeof(out_dentry));
	out_inode.i_rdev = ram_device;
	outfile.f_mode = 3; /* read/write */
	outfile.f_dentry = &out_dentry;
	out_dentry.d_inode = &out_inode;

	if (blkdev_open(&inode, &infile) != 0) return;
	if (blkdev_open(&out_inode, &outfile) != 0) return;

	fs = get_fs();
	set_fs(KERNEL_DS);
	
	nblocks = identify_ramdisk_image(device, &infile, offset);
	if (nblocks < 0)
		goto done;

	if (nblocks == 0) {
#ifdef BUILD_CRAMDISK
		if (crd_load(&infile, &outfile) == 0)
			goto successful_load;
#else
		printk(KERN_NOTICE
		       "RAMDISK: Kernel does not support compressed "
		       "RAM disk images\n");
#endif
		goto done;
	}

	if (nblocks > (rd_length[0] >> BLOCK_SIZE_BITS)) {
		printk("RAMDISK: image too big! (%d/%d blocks)\n",
		       nblocks, rd_length[0] >> BLOCK_SIZE_BITS);
		goto done;
	}
		
	/*
	 * OK, time to copy in the data
	 */
	buf = kmalloc(BLOCK_SIZE, GFP_KERNEL);
	if (buf == 0) {
		printk(KERN_ERR "RAMDISK: could not allocate buffer\n");
		goto done;
	}

	if (blk_size[MAJOR(device)])
		devblocks = blk_size[MAJOR(device)][MINOR(device)];

#ifdef CONFIG_BLK_DEV_INITRD
	if (MAJOR(device) == MAJOR_NR && MINOR(device) == INITRD_MINOR)
		devblocks = nblocks;
#endif

	if (devblocks == 0) {
		printk(KERN_ERR "RAMDISK: could not determine device size\n");
		goto done;
	}

	printk(KERN_NOTICE "RAMDISK: Loading %d blocks [%d disk%s] into ram disk... ", 
		nblocks, ((nblocks-1)/devblocks)+1, nblocks>devblocks ? "s" : "");
	for (i=0; i < nblocks; i++) {
		if (i && (i % devblocks == 0)) {
			printk("done disk #%d.\n", i/devblocks);
			rotate = 0;
			invalidate_buffers(device);
			if (infile.f_op->release)
				infile.f_op->release(&inode, &infile);
			printk("Please insert disk #%d and press ENTER\n", i/devblocks+1);
			wait_for_keypress();
			if (blkdev_open(&inode, &infile) != 0)  {
				printk("Error opening disk.\n");
				goto done;
			}
			infile.f_pos = 0;
			printk("Loading disk #%d... ", i/devblocks+1);
		}
		infile.f_op->read(&infile, buf, BLOCK_SIZE, &infile.f_pos);
		outfile.f_op->write(&outfile, buf, BLOCK_SIZE, &outfile.f_pos);
		if (!(i % 16)) {
			printk("%c\b", rotator[rotate & 0x3]);
			rotate++;
		}
	}
	printk("done.\n");
	kfree(buf);

successful_load:
	invalidate_buffers(device);
	ROOT_DEV = MKDEV(MAJOR_NR,0);

done:
	if (infile.f_op->release)
		infile.f_op->release(&inode, &infile);
	set_fs(fs);
}


__initfunc(void rd_load(void))
{
	if (rd_doload == 0)
		return;
	
	if (MAJOR(ROOT_DEV) != FLOPPY_MAJOR) return;

	if (rd_prompt) {
#ifdef CONFIG_BLK_DEV_FD
		floppy_eject();
#endif
		printk(KERN_NOTICE
		       "VFS: Insert root floppy disk to be loaded into RAM disk and press ENTER\n");
		wait_for_keypress();
	}

	rd_load_image(ROOT_DEV,rd_image_start);

}


#ifdef CONFIG_BLK_DEV_INITRD
__initfunc(void initrd_load(void))
{
	rd_load_image(MKDEV(MAJOR_NR, INITRD_MINOR),0);
}
#endif

#endif /* RD_LOADER */

#ifdef BUILD_CRAMDISK

/*
 * gzip declarations
 */

#define OF(args)  args

#define memzero(s, n)     memset ((s), 0, (n))


typedef unsigned char  uch;
typedef unsigned short ush;
typedef unsigned long  ulg;

#define INBUFSIZ 4096
#define WSIZE 0x8000    /* window size--must be a power of two, and */
			/*  at least 32K for zip's deflate method */

static uch *inbuf;
static uch *window;

static unsigned insize = 0;  /* valid bytes in inbuf */
static unsigned inptr = 0;   /* index of next byte to be processed in inbuf */
static unsigned outcnt = 0;  /* bytes in output buffer */
static int exit_code = 0;
static long bytes_out = 0;
static struct file *crd_infp, *crd_outfp;

#define get_byte()  (inptr < insize ? inbuf[inptr++] : fill_inbuf())
		
/* Diagnostic functions (stubbed out) */
#define Assert(cond,msg)
#define Trace(x)
#define Tracev(x)
#define Tracevv(x)
#define Tracec(c,x)
#define Tracecv(c,x)

#define STATIC static

static int  fill_inbuf(void);
static void flush_window(void);
static void *malloc(int size);
static void free(void *where);
static void error(char *m);
static void gzip_mark(void **);
static void gzip_release(void **);

#include "../../lib/inflate.c"

__initfunc(static void *malloc(int size))
{
	return kmalloc(size, GFP_KERNEL);
}

__initfunc(static void free(void *where))
{
	kfree(where);
}

__initfunc(static void gzip_mark(void **ptr))
{
}

__initfunc(static void gzip_release(void **ptr))
{
}


/* ===========================================================================
 * Fill the input buffer. This is called only when the buffer is empty
 * and at least one byte is really needed.
 */
__initfunc(static int fill_inbuf(void))
{
	if (exit_code) return -1;
	
	insize = crd_infp->f_op->read(crd_infp, inbuf, INBUFSIZ,
				      &crd_infp->f_pos);
	if (insize == 0) return -1;

	inptr = 1;

	return inbuf[0];
}

/* ===========================================================================
 * Write the output window window[0..outcnt-1] and update crc and bytes_out.
 * (Used for the decompressed data only.)
 */
__initfunc(static void flush_window(void))
{
    ulg c = crc;         /* temporary variable */
    unsigned n;
    uch *in, ch;
    
    crd_outfp->f_op->write(crd_outfp, window, outcnt, &crd_outfp->f_pos);
    in = window;
    for (n = 0; n < outcnt; n++) {
	    ch = *in++;
	    c = crc_32_tab[((int)c ^ ch) & 0xff] ^ (c >> 8);
    }
    crc = c;
    bytes_out += (ulg)outcnt;
    outcnt = 0;
}

__initfunc(static void error(char *x))
{
	printk(KERN_ERR "%s", x);
	exit_code = 1;
}

__initfunc(static int
crd_load(struct file * fp, struct file *outfp))
{
	int result;
	
	crd_infp = fp;
	crd_outfp = outfp;
	inbuf = kmalloc(INBUFSIZ, GFP_KERNEL);
	if (inbuf == 0) {
		printk(KERN_ERR "RAMDISK: Couldn't allocate gzip buffer\n");
		return -1;
	}
	window = kmalloc(WSIZE, GFP_KERNEL);
	if (window == 0) {
		printk(KERN_ERR "RAMDISK: Couldn't allocate gzip window\n");
		kfree(inbuf);
		return -1;
	}
	makecrc();
	result = gunzip();
	kfree(inbuf);
	kfree(window);
	return result;
}

#endif  /* BUILD_CRAMDISK */

