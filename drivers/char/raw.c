/*
 * linux/drivers/char/raw.c
 *
 * Front-end raw character devices.  These can be bound to any block
 * devices to provide genuine Unix raw character device semantics.
 *
 * We reserve minor number 0 for a control interface.  ioctl()s on this
 * device are used to bind the other minor numbers to block devices.
 */

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/major.h>
#include <linux/blkdev.h>
#include <linux/module.h>
#include <linux/raw.h>
#include <linux/capability.h>
#include <linux/uio.h>

#include <asm/uaccess.h>

struct raw_device_data {
	struct block_device *binding;
	int inuse;
};

static struct raw_device_data raw_devices[256];
static DECLARE_MUTEX(raw_mutex);
static struct file_operations raw_ctl_fops;

/*
 * Open/close code for raw IO.
 *
 * We just rewrite the i_mapping for the /dev/raw/rawN file descriptor to
 * point at the blockdev's address_space and set the file handle to use
 * O_DIRECT.
 *
 * Set the device's soft blocksize to the minimum possible.  This gives the
 * finest possible alignment and has no adverse impact on performance.
 */
static int raw_open(struct inode *inode, struct file *filp)
{
	const int minor = minor(inode->i_rdev);
	struct block_device *bdev;
	int err;

	if (minor == 0) {	/* It is the control device */
		filp->f_op = &raw_ctl_fops;
		return 0;
	}
	
	down(&raw_mutex);

	/*
	 * All we need to do on open is check that the device is bound.
	 */
	bdev = raw_devices[minor].binding;
	err = -ENODEV;
	if (bdev) {
		err = bd_claim(bdev, raw_open);
		if (err)
			goto out;
		atomic_inc(&bdev->bd_count);
		err = blkdev_get(bdev, filp->f_mode, 0, BDEV_RAW);
		if (err) {
			bd_release(bdev);
			goto out;
		} else {
			err = set_blocksize(bdev, bdev_hardsect_size(bdev));
			if (err == 0) {
				raw_devices[minor].inuse++;
				filp->f_dentry->d_inode->i_mapping =
					bdev->bd_inode->i_mapping;
				filp->f_flags |= O_DIRECT;
			}
		}
	}
	filp->private_data = bdev;
out:
	up(&raw_mutex);
	return err;
}

static int raw_release(struct inode *inode, struct file *filp)
{
	const int minor= minor(inode->i_rdev);
	struct block_device *bdev;
	
	down(&raw_mutex);
	bdev = raw_devices[minor].binding;
	raw_devices[minor].inuse--;
	up(&raw_mutex);
	bd_release(bdev);
	blkdev_put(bdev, BDEV_RAW);
	return 0;
}

/*
 * Forward ioctls to the underlying block device.
 */
static int
raw_ioctl(struct inode *inode, struct file *filp,
		  unsigned int command, unsigned long arg)
{
	struct block_device *bdev = filp->private_data;

	return ioctl_by_bdev(bdev, command, arg);
}

/*
 * Deal with ioctls against the raw-device control interface, to bind
 * and unbind other raw devices.
 */
static int
raw_ctl_ioctl(struct inode *inode, struct file *filp,
		unsigned int command, unsigned long arg)
{
	struct raw_config_request rq;
	struct raw_device_data *rawdev;
	int err;
	
	switch (command) {
	case RAW_SETBIND:
	case RAW_GETBIND:

		/* First, find out which raw minor we want */

		err = -EFAULT;
		if (copy_from_user(&rq, (void *) arg, sizeof(rq)))
			goto out;
		
		err = -EINVAL;
		if (rq.raw_minor < 0 || rq.raw_minor > MINORMASK)
			goto out;
		rawdev = &raw_devices[rq.raw_minor];

		if (command == RAW_SETBIND) {
			/*
			 * This is like making block devices, so demand the
			 * same capability
			 */
			err = -EPERM;
			if (!capable(CAP_SYS_ADMIN))
				goto out;

			/*
			 * For now, we don't need to check that the underlying
			 * block device is present or not: we can do that when
			 * the raw device is opened.  Just check that the
			 * major/minor numbers make sense.
			 */

			err = -EINVAL;
			if ((rq.block_major == 0 && rq.block_minor != 0) ||
					rq.block_major > MAX_BLKDEV ||
					rq.block_minor > MINORMASK)
				goto out;
			
			down(&raw_mutex);
			err = -EBUSY;
			if (rawdev->inuse) {
				up(&raw_mutex);
				goto out;
			}
			if (rawdev->binding) {
				bdput(rawdev->binding);
				MOD_DEC_USE_COUNT;
			}
			if (rq.block_major == 0 && rq.block_minor == 0) {
				/* unbind */
				rawdev->binding = NULL;
			} else {
				kdev_t kdev;

				kdev = mk_kdev(rq.block_major, rq.block_minor);
				rawdev->binding = bdget(kdev_t_to_nr(kdev));
				MOD_INC_USE_COUNT;
			}
			up(&raw_mutex);
		} else {
			struct block_device *bdev;

			down(&raw_mutex);
			bdev = rawdev->binding;
			if (bdev) {
				rq.block_major = MAJOR(bdev->bd_dev);
				rq.block_minor = MINOR(bdev->bd_dev);
			} else {
				rq.block_major = rq.block_minor = 0;
			}
			up(&raw_mutex);
			err = -EFAULT;
			if (copy_to_user((void *)arg, &rq, sizeof(rq)))
				goto out;
		}
		err = 0;
		break;
		
	default:
		err = -EINVAL;
		break;
	}
out:
	return err;
}

static ssize_t raw_file_write(struct file *file, const char *buf,
				   size_t count, loff_t *ppos)
{
	struct iovec local_iov = { .iov_base = (void *)buf, .iov_len = count };

	return generic_file_write_nolock(file, &local_iov, 1, ppos);
}

static ssize_t raw_file_aio_write(struct kiocb *iocb, const char *buf,
					size_t count, loff_t pos)
{
	struct iovec local_iov = { .iov_base = (void *)buf, .iov_len = count };

	return generic_file_aio_write_nolock(iocb, &local_iov, 1, &iocb->ki_pos);
}


static struct file_operations raw_fops = {
	.read	=	generic_file_read,
	.aio_read = 	generic_file_aio_read,
	.write	=	raw_file_write,
	.aio_write = 	raw_file_aio_write,
	.open	=	raw_open,
	.release=	raw_release,
	.ioctl	=	raw_ioctl,
	.readv	= 	generic_file_readv,
	.writev	= 	generic_file_writev,
	.owner	=	THIS_MODULE,
};

static struct file_operations raw_ctl_fops = {
	.ioctl	=	raw_ctl_ioctl,
	.open	=	raw_open,
	.owner	=	THIS_MODULE,
};

static int __init raw_init(void)
{
	register_chrdev(RAW_MAJOR, "raw", &raw_fops);
	return 0;
}

static void __exit raw_exit(void)
{
	unregister_chrdev(RAW_MAJOR, "raw");
}

module_init(raw_init);
module_exit(raw_exit);
MODULE_LICENSE("GPL");
