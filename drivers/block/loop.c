/*
 *  linux/drivers/block/loop.c
 *
 *  Written by Theodore Ts'o, 3/29/93
 * 
 * Copyright 1993 by Theodore Ts'o.  Redistribution of this file is
 * permitted under the GNU Public License.
 *
 * DES encryption plus some minor changes by Werner Almesberger, 30-MAY-1993
 *
 * Modularized and updated for 1.1.16 kernel - Mitch Dsouza 28th May 1994
 *
 * Adapted for 1.3.59 kernel - Andries Brouwer, 1 Feb 1996
 */

#include <linux/module.h>

#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <asm/segment.h>
#include "loop.h"

#ifdef DES_AVAILABLE
#include "des.h"
#endif

#define DEFAULT_MAJOR_NR 28
int loop_major = DEFAULT_MAJOR_NR;
#define MAJOR_NR loop_major	/* not necessarily constant */

#define DEVICE_NAME "loop"
#define DEVICE_REQUEST do_lo_request
#define DEVICE_NR(device) (MINOR(device))
#define DEVICE_ON(device)
#define DEVICE_OFF(device)
#define DEVICE_NO_RANDOM
#define TIMEOUT_VALUE (6 * HZ)
#include <linux/blk.h>

#define MAX_LOOP 8
static struct loop_device loop_dev[MAX_LOOP];
static int loop_sizes[MAX_LOOP];

/*
 * Transfer functions
 */
static int transfer_none(struct loop_device *lo, int cmd, char *raw_buf,
		  char *loop_buf, int size)
{
	if (cmd == READ)
		memcpy(loop_buf, raw_buf, size);
	else
		memcpy(raw_buf, loop_buf, size);
	return 0;
}

static int transfer_xor(struct loop_device *lo, int cmd, char *raw_buf,
		 char *loop_buf, int size)
{
	char	*in, *out, *key;
	int	i, keysize;

	if (cmd == READ) {
		in = raw_buf;
		out = loop_buf;
	} else {
		in = loop_buf;
		out = raw_buf;
	}
	key = lo->lo_encrypt_key;
	keysize = lo->lo_encrypt_key_size;
	for (i=0; i < size; i++)
		*out++ = *in++ ^ key[(i & 511) % keysize];
	return 0;
}

#ifdef DES_AVAILABLE
static int transfer_des(struct loop_device *lo, int cmd, char *raw_buf,
		  char *loop_buf, int size)
{
	unsigned long tmp[2];
	unsigned long x0,x1,p0,p1;

	if (size & 7)
		return -EINVAL;
	x0 = lo->lo_des_init[0];
	x1 = lo->lo_des_init[1];
	while (size) {
		if (cmd == READ) {
			tmp[0] = (p0 = ((unsigned long *) raw_buf)[0])^x0;
			tmp[1] = (p1 = ((unsigned long *) raw_buf)[1])^x1;
			des_ecb_encrypt((des_cblock *) tmp,(des_cblock *)
			    loop_buf,lo->lo_des_key,DES_ENCRYPT);
			x0 = p0^((unsigned long *) loop_buf)[0];
			x1 = p1^((unsigned long *) loop_buf)[1];
		}
		else {
			p0 = ((unsigned long *) loop_buf)[0];
			p1 = ((unsigned long *) loop_buf)[1];
			des_ecb_encrypt((des_cblock *) loop_buf,(des_cblock *)
			    raw_buf,lo->lo_des_key,DES_DECRYPT);
			((unsigned long *) raw_buf)[0] ^= x0;
			((unsigned long *) raw_buf)[1] ^= x1;
			x0 = p0^((unsigned long *) raw_buf)[0];
			x1 = p1^((unsigned long *) raw_buf)[1];
		}
		size -= 8;
		raw_buf += 8;
		loop_buf += 8;
	}
	return 0;
}
#endif

static transfer_proc_t xfer_funcs[MAX_LOOP] = {
	transfer_none,		/* LO_CRYPT_NONE */
	transfer_xor,		/* LO_CRYPT_XOR */
#ifdef DES_AVAILABLE
	transfer_des,		/* LO_CRYPT_DES */
#else
	NULL,			/* LO_CRYPT_DES */
#endif
	0			/* LO_CRYPT_IDEA */
};


#define MAX_DISK_SIZE 1024*1024*1024


static void figure_loop_size(struct loop_device *lo)
{
	int	size;

	if (S_ISREG(lo->lo_inode->i_mode))
		size = (lo->lo_inode->i_size - lo->lo_offset) / 1024;
	else {
		if (blk_size[MAJOR(lo->lo_device)])
			size = ((blk_size[MAJOR(lo->lo_device)]
				 [MINOR(lo->lo_device)]) -
				(lo->lo_offset/1024));
		else
			size = MAX_DISK_SIZE;
	}
	loop_sizes[lo->lo_number] = size;
}

static void do_lo_request(void)
{
	int	real_block, block, offset, len, blksize, size;
	char	*dest_addr;
	struct loop_device *lo;
	struct buffer_head *bh;

repeat:
	INIT_REQUEST;
	if (MINOR(CURRENT->rq_dev) >= MAX_LOOP)
		goto error_out;
	lo = &loop_dev[MINOR(CURRENT->rq_dev)];
	if (!lo->lo_inode || !lo->transfer)
		goto error_out;

	blksize = BLOCK_SIZE;
	if (blksize_size[MAJOR(lo->lo_device)]) {
	    blksize = blksize_size[MAJOR(lo->lo_device)][MINOR(lo->lo_device)];
	    if (!blksize)
	      blksize = BLOCK_SIZE;
	}

	dest_addr = CURRENT->buffer;
	
	if (blksize < 512) {
		block = CURRENT->sector * (512/blksize);
		offset = 0;
	} else {
		block = CURRENT->sector / (blksize >> 9);
		offset = CURRENT->sector % (blksize >> 9);
	}
	block += lo->lo_offset / blksize;
	offset += lo->lo_offset % blksize;
	if (offset > blksize) {
		block++;
		offset -= blksize;
	}
	len = CURRENT->current_nr_sectors << 9;
	if ((CURRENT->cmd != WRITE) && (CURRENT->cmd != READ)) {
		printk("unknown loop device command (%d)?!?", CURRENT->cmd);
		goto error_out;
	}
	while (len > 0) {
		real_block = block;
		if (lo->lo_flags & LO_FLAGS_DO_BMAP) {
			real_block = bmap(lo->lo_inode, block);
			if (!real_block) {
				printk("loop: block %d not present\n", block);
				goto error_out;
			}
		}
		bh = getblk(lo->lo_device, real_block, blksize);
		if (!bh) {
			printk("loop: device %s: getblk(-, %d, %d) returned NULL",
			       kdevname(lo->lo_device),
			       block, blksize);
			goto error_out;
		}
		if (!buffer_uptodate(bh) && ((CURRENT->cmd == READ) ||
					(offset || (len < blksize)))) {
			ll_rw_block(READ, 1, &bh);
			wait_on_buffer(bh);
			if (!buffer_uptodate(bh)) {
				brelse(bh);
				goto error_out;
			}
		}
		size = blksize - offset;
		if (size > len)
			size = len;
		if ((lo->transfer)(lo, CURRENT->cmd, bh->b_data + offset,
				   dest_addr, size)) {
			printk("loop: transfer error block %d\n", block);
			brelse(bh);
			goto error_out;
		}
		if (CURRENT->cmd == WRITE) {
			set_bit(BH_Dirty, &bh->b_state);
			ll_rw_block(WRITE, 1, &bh);
			wait_on_buffer(bh);
			if (buffer_dirty(bh)) {
				brelse(bh);
				goto error_out;
			}
		}
		brelse(bh);
		dest_addr += size;
		len -= size;
		offset = 0;
		block++;
	}
	end_request(1);
	goto repeat;
error_out:
	end_request(0);
	goto repeat;
}

static int loop_set_fd(struct loop_device *lo, unsigned int arg)
{
	struct inode	*inode;
	
	if (arg >= NR_OPEN || !current->files->fd[arg])
		return -EBADF;
	if (lo->lo_inode)
		return -EBUSY;
	inode = current->files->fd[arg]->f_inode;
	if (!inode) {
		printk("loop_set_fd: NULL inode?!?\n");
		return -EINVAL;
	}
	if (S_ISREG(inode->i_mode)) {
		lo->lo_device = inode->i_dev;
		lo->lo_flags |= LO_FLAGS_DO_BMAP;
	} else if (S_ISBLK(inode->i_mode))
			lo->lo_device = inode->i_rdev;
		else
			return -EINVAL;
	lo->lo_inode = inode;
	lo->lo_inode->i_count++;
	lo->transfer = NULL;
	figure_loop_size(lo);
	return 0;
}

static int loop_clr_fd(struct loop_device *lo, kdev_t dev)
{
	if (!lo->lo_inode)
		return -ENXIO;
	if (lo->lo_refcnt > 1)
		return -EBUSY;
	lo->lo_inode->i_count--;
	lo->lo_device = 0;
	lo->lo_inode = NULL;
	lo->lo_encrypt_type = 0;
	lo->lo_offset = 0;
	lo->lo_encrypt_key_size = 0;
	memset(lo->lo_encrypt_key, 0, LO_KEY_SIZE);
	memset(lo->lo_name, 0, LO_NAME_SIZE);
	invalidate_buffers(dev);
	return 0;
}

static int loop_set_status(struct loop_device *lo, struct loop_info *arg)
{
	struct loop_info info;

	if (!lo->lo_inode)
		return -ENXIO;
	if (!arg)
		return -EINVAL;
	memcpy_fromfs(&info, arg, sizeof(info));
	if (info.lo_encrypt_key_size > LO_KEY_SIZE)
		return -EINVAL;
	switch (info.lo_encrypt_type) {
	case LO_CRYPT_NONE:
		break;
	case LO_CRYPT_XOR:
		if (info.lo_encrypt_key_size < 0)
			return -EINVAL;
		break;
#ifdef DES_AVAILABLE
	case LO_CRYPT_DES:
		if (info.lo_encrypt_key_size != 8)
			return -EINVAL;
		des_set_key((des_cblock *) lo->lo_encrypt_key,
		   lo->lo_des_key);
		memcpy(lo->lo_des_init,info.lo_init,8);
		break;
#endif
	default:
		return -EINVAL;
	}
	lo->lo_offset = info.lo_offset;
	strncpy(lo->lo_name, info.lo_name, LO_NAME_SIZE);
	lo->lo_encrypt_type = info.lo_encrypt_type;
	lo->transfer = xfer_funcs[lo->lo_encrypt_type];
	lo->lo_encrypt_key_size = info.lo_encrypt_key_size;
	if (info.lo_encrypt_key_size)
		memcpy(lo->lo_encrypt_key, info.lo_encrypt_key,
		       info.lo_encrypt_key_size);
	figure_loop_size(lo);
	return 0;
}

static int loop_get_status(struct loop_device *lo, struct loop_info *arg)
{
	struct loop_info	info;
	
	if (!lo->lo_inode)
		return -ENXIO;
	if (!arg)
		return -EINVAL;
	memset(&info, 0, sizeof(info));
	info.lo_number = lo->lo_number;
	info.lo_device = kdev_t_to_nr(lo->lo_inode->i_dev);
	info.lo_inode = lo->lo_inode->i_ino;
	info.lo_rdevice = kdev_t_to_nr(lo->lo_device);
	info.lo_offset = lo->lo_offset;
	info.lo_flags = lo->lo_flags;
	strncpy(info.lo_name, lo->lo_name, LO_NAME_SIZE);
	info.lo_encrypt_type = lo->lo_encrypt_type;
	if (lo->lo_encrypt_key_size && suser()) {
		info.lo_encrypt_key_size = lo->lo_encrypt_key_size;
		memcpy(info.lo_encrypt_key, lo->lo_encrypt_key,
		       lo->lo_encrypt_key_size);
	}
	memcpy_tofs(arg, &info, sizeof(info));
	return 0;
}

static int lo_ioctl(struct inode * inode, struct file * file,
	unsigned int cmd, unsigned long arg)
{
	struct loop_device *lo;
	int dev, err;

	if (!inode)
		return -EINVAL;
	if (MAJOR(inode->i_rdev) != loop_major) {
		printk("lo_ioctl: pseudo-major != %d\n", loop_major);
		return -ENODEV;
	}
	dev = MINOR(inode->i_rdev);
	if (dev >= MAX_LOOP)
		return -ENODEV;
	lo = &loop_dev[dev];
	switch (cmd) {
	case LOOP_SET_FD:
		return loop_set_fd(lo, arg);
	case LOOP_CLR_FD:
		return loop_clr_fd(lo, inode->i_rdev);
	case LOOP_SET_STATUS:
		return loop_set_status(lo, (struct loop_info *) arg);
	case LOOP_GET_STATUS:
		return loop_get_status(lo, (struct loop_info *) arg);
	case BLKGETSIZE:   /* Return device size */
		if (!lo->lo_inode)
			return -ENXIO;
		if (!arg)  return -EINVAL;
		err = verify_area(VERIFY_WRITE, (long *) arg, sizeof(long));
		if (err)
			return err;
		put_fs_long(loop_sizes[lo->lo_number] << 1, (long *) arg);
		return 0;
		default:
			return -EINVAL;
	}
	return 0;
}

static int lo_open(struct inode *inode, struct file *file)
{
	struct loop_device *lo;
	int	dev;

	if (!inode)
		return -EINVAL;
	if (MAJOR(inode->i_rdev) != loop_major) {
		printk("lo_open: pseudo-major != %d\n", loop_major);
		return -ENODEV;
	}
	dev = MINOR(inode->i_rdev);
	if (dev >= MAX_LOOP)
		return -ENODEV;
	lo = &loop_dev[dev];
	lo->lo_refcnt++;
	MOD_INC_USE_COUNT;
	return 0;
}

static void lo_release(struct inode *inode, struct file *file)
{
	struct loop_device *lo;
	int	dev;

	if (!inode)
		return;
	if (MAJOR(inode->i_rdev) != loop_major) {
		printk("lo_release: pseudo-major != %d\n", loop_major);
		return;
	}
	dev = MINOR(inode->i_rdev);
	if (dev >= MAX_LOOP)
		return;
	sync_dev(inode->i_rdev);
	lo = &loop_dev[dev];
	if (lo->lo_refcnt <= 0)
		printk("lo_release: refcount(%d) <= 0\n", lo->lo_refcnt);
	else  {
		lo->lo_refcnt--;
		MOD_DEC_USE_COUNT;
	}

	return;
}

static struct file_operations lo_fops = {
	NULL,			/* lseek - default */
	block_read,		/* read - general block-dev read */
	block_write,		/* write - general block-dev write */
	NULL,			/* readdir - bad */
	NULL,			/* select */
	lo_ioctl,		/* ioctl */
	NULL,			/* mmap */
	lo_open,		/* open */
	lo_release		/* release */
};

/*
 * And now the modules code and kernel interface.
 */
#ifdef MODULE
#define loop_init init_module
#endif

int
loop_init( void ) {
	int	i = (loop_major ? loop_major : DEFAULT_MAJOR_NR);

	if (register_blkdev(i, "loop", &lo_fops) &&
	    (i = register_blkdev(0, "loop", &lo_fops)) <= 0) {
		printk("Unable to get major number for loop device\n");
		return -EIO;
	}
	loop_major = i;
#ifdef MODULE
	if (i != DEFAULT_MAJOR_NR)
#endif
	  printk("loop: registered device at major %d\n",loop_major);

	blk_dev[loop_major].request_fn = DEVICE_REQUEST;
	for (i=0; i < MAX_LOOP; i++) {
		memset(&loop_dev[i], 0, sizeof(struct loop_device));
		loop_dev[i].lo_number = i;
	}
	memset(&loop_sizes, 0, sizeof(loop_sizes));
	blk_size[loop_major] = loop_sizes;

	return 0;
}

#ifdef MODULE
void
cleanup_module( void ) {
  if (unregister_blkdev(loop_major, "loop") != 0)
    printk("loop: cleanup_module failed\n");
}
#endif
