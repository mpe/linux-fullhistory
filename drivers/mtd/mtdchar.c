/*
 * $Id: mtdchar.c,v 1.7 2000/06/30 15:54:19 dwmw2 Exp $
 *
 * Character-device access to raw MTD devices.
 *
 */


#include <linux/mtd/compatmac.h>

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/malloc.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,2,0)
static loff_t mtd_lseek (struct file *file, loff_t offset, int orig)
#else
static int mtd_lseek (struct inode *inode, struct file *file, off_t offset, int orig)
#endif
{
	struct mtd_info *mtd=(struct mtd_info *)file->private_data;

	switch (orig) {
	case 0: 
		/* SEEK_SET */
		file->f_pos = offset;
		break;
	case 1: 
		/* SEEK_CUR */
		file->f_pos += offset;
		break;
	case 2:
		/* SEEK_END */
		file->f_pos =mtd->size + offset;
		break;
	default: 
		return -EINVAL;
	}

	if (file->f_pos < 0) 
		file->f_pos = 0;
	else if (file->f_pos >= mtd->size)
		file->f_pos = mtd->size - 1;

	return file->f_pos;
}



static int mtd_open(struct inode *inode, struct file *file)
{
	int minor = MINOR(inode->i_rdev);
	int devnum = minor >> 1;
	struct mtd_info *mtd;

	DEBUG(0, "MTD_open\n");

	if (devnum >= MAX_MTD_DEVICES)
		return -ENODEV;

	/* You can't open the RO devices RW */
	if ((file->f_mode & 2) && (minor & 1))
		return -EACCES;

	MOD_INC_USE_COUNT;

	mtd = get_mtd_device(NULL, devnum);
		
	if (!mtd) {
		MOD_DEC_USE_COUNT;
		return -ENODEV;
	}
	
	file->private_data = mtd;
		
	/* You can't open it RW if it's not a writeable device */
	if ((file->f_mode & 2) && !(mtd->flags & MTD_WRITEABLE)) {
		put_mtd_device(mtd);
		MOD_DEC_USE_COUNT;
		return -EACCES;
	}
		
	return 0;
} /* mtd_open */

/*====================================================================*/

static release_t mtd_close(struct inode *inode,
				 struct file *file)
{
	struct mtd_info *mtd;

	DEBUG(0, "MTD_close\n");

	mtd = (struct mtd_info *)file->private_data;
	
	if (mtd->sync)
		mtd->sync(mtd);
	
	put_mtd_device(mtd);

	MOD_DEC_USE_COUNT;
	release_return(0);
} /* mtd_close */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,2,0)
#define FILE_POS *ppos
#else
#define FILE_POS file->f_pos
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,2,0)
static ssize_t mtd_read(struct file *file, char *buf, size_t count,loff_t *ppos)
#else
static int mtd_read(struct inode *inode,struct file *file, char *buf, int count)
#endif
{
	struct mtd_info *mtd = (struct mtd_info *)file->private_data;
	size_t retlen=0;
	int ret=0;
	char *kbuf;
	
	DEBUG(0,"MTD_read\n");

	if (FILE_POS + count > mtd->size)
		count = mtd->size - FILE_POS;

	if (!count)
		return 0;
	
	/* FIXME: Use kiovec in 2.3 or 2.2+rawio, or at
	 * least split the IO into smaller chunks. 
	 */
	
	kbuf = vmalloc(count);
	if (!kbuf)
		return -ENOMEM;
	
	ret = MTD_READ(mtd, FILE_POS, count, &retlen, kbuf);
	if (!ret) {
		FILE_POS += retlen;
		if (copy_to_user(buf, kbuf, retlen))
			ret = -EFAULT;
		else
			ret = retlen;

	}
	
	vfree(kbuf);
	
	return ret;
} /* mtd_read */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,2,0)
static ssize_t mtd_write(struct file *file, const char *buf, size_t count,loff_t *ppos)
#else
static read_write_t mtd_write(struct inode *inode,struct file *file, const char *buf, count_t count)
#endif
{
	struct mtd_info *mtd = (struct mtd_info *)file->private_data;
	char *kbuf;
	size_t retlen;
	int ret=0;

	DEBUG(0,"MTD_write\n");
	
	if (FILE_POS == mtd->size)
		return -ENOSPC;
	
	if (FILE_POS + count > mtd->size)
		count = mtd->size - FILE_POS;

	if (!count)
		return 0;

	kbuf=vmalloc(count);

	if (!kbuf)
		return -ENOMEM;
	
	if (copy_from_user(kbuf, buf, count)) {
		vfree(kbuf);
		return -EFAULT;
	}
		

	ret = (*(mtd->write))(mtd, FILE_POS, count, &retlen, buf);
		
	if (!ret) {
		FILE_POS += retlen;
		ret = retlen;
	}

	vfree(kbuf);

	return ret;
} /* mtd_write */

/*======================================================================

    IOCTL calls for getting device parameters.

======================================================================*/
static void mtd_erase_callback (struct erase_info *instr)
{
	wake_up((wait_queue_head_t *)instr->priv);
}

static int mtd_ioctl(struct inode *inode, struct file *file,
		     u_int cmd, u_long arg)
{
	struct mtd_info *mtd = (struct mtd_info *)file->private_data;
	int ret = 0;
	u_long size;
	
	DEBUG(0, "MTD_ioctl\n");

	size = (cmd & IOCSIZE_MASK) >> IOCSIZE_SHIFT;
	if (cmd & IOC_IN) {
		ret = verify_area(VERIFY_READ, (char *)arg, size);
		if (ret) return ret;
	}
	if (cmd & IOC_OUT) {
		ret = verify_area(VERIFY_WRITE, (char *)arg, size);
		if (ret) return ret;
	}
	
	switch (cmd) {
	case MEMGETINFO:
		copy_to_user((struct mtd_info *)arg, mtd,
			     sizeof(struct mtd_info_user));
		break;

	case MEMERASE:
	{
		struct erase_info *erase=kmalloc(sizeof(struct erase_info),GFP_KERNEL);
		if (!erase)
			ret = -ENOMEM;
		else {
			wait_queue_head_t waitq;
			DECLARE_WAITQUEUE(wait, current);

			init_waitqueue_head(&waitq);

			memset (erase,0,sizeof(struct erase_info));
			copy_from_user(&erase->addr, (u_long *)arg,
				       2 * sizeof(u_long));
			erase->mtd = mtd;
			erase->callback = mtd_erase_callback;
			erase->priv = (unsigned long)&waitq;
			
			/* FIXME: Allow INTERRUPTIBLE. Which means
			   not having the wait_queue head on the stack
			   
			   Does it? Why? Who wrote this? Was it my alter 
			   ago - the intelligent one? Or was it the stupid 
			   one, and now I'm being clever I don't know what
			   it was on about?

			   dwmw2.

			   It was the intelligent one. If the wq_head is
			   on the stack, and we leave because we got 
			   interrupted, then the wq_head is no longer 
			   there when the callback routine tries to
			   wake us up --> BOOM!.

			*/
			current->state = TASK_UNINTERRUPTIBLE;
			add_wait_queue(&waitq, &wait);
			ret = mtd->erase(mtd, erase);
			if (!ret)
				schedule();
			remove_wait_queue(&waitq, &wait);
			current->state = TASK_RUNNING;
			if (!ret)
				ret = (erase->state == MTD_ERASE_FAILED);
			kfree(erase);
		}
		break;
	}

	case MEMWRITEOOB:
	{
		struct mtd_oob_buf buf;
		void *databuf;
		ssize_t retlen;
		
		copy_from_user(&buf, (struct mtd_oob_buf *)arg, sizeof(struct mtd_oob_buf));
		
		if (buf.length > 0x4096)
			return -EINVAL;

		if (!mtd->write_oob)
			ret = -EOPNOTSUPP;
		else
			ret = verify_area(VERIFY_READ, (char *)buf.ptr, buf.length);

		if (ret)
			return ret;

		databuf = kmalloc(buf.length, GFP_KERNEL);
		if (!databuf)
			return -ENOMEM;
		
		copy_from_user(databuf, buf.ptr, buf.length);

		ret = (mtd->write_oob)(mtd, buf.start, buf.length, &retlen, databuf);

		copy_to_user((void *)arg + sizeof(loff_t), &retlen, sizeof(ssize_t));

		kfree(databuf);
		break;

	}

	case MEMREADOOB:
	{
		struct mtd_oob_buf buf;
		void *databuf;
		ssize_t retlen;

		copy_from_user(&buf, (struct mtd_oob_buf *)arg, sizeof(struct mtd_oob_buf));
		
		if (buf.length > 0x4096)
			return -EINVAL;

		if (!mtd->read_oob)
			ret = -EOPNOTSUPP;
		else
			ret = verify_area(VERIFY_WRITE, (char *)buf.ptr, buf.length);

		if (ret)
			return ret;

		databuf = kmalloc(buf.length, GFP_KERNEL);
		if (!databuf)
			return -ENOMEM;
		
		ret = (mtd->read_oob)(mtd, buf.start, buf.length, &retlen, databuf);

		copy_to_user((void *)arg + sizeof(loff_t), &retlen, sizeof(ssize_t));

		if (retlen)
			copy_to_user(buf.ptr, databuf, retlen);

		kfree(databuf);
		break;
	}
			     
			     
		
		

	default:
	  printk("Invalid ioctl %x (MEMGETINFO = %x)\n",cmd, MEMGETINFO);
		ret = -EINVAL;
	}
	
	return ret;
} /* memory_ioctl */

static struct file_operations mtd_fops = {

	llseek:		mtd_lseek,     	/* lseek */
	read:		mtd_read,	/* read */
	write: 		mtd_write, 	/* write */
	ioctl:		mtd_ioctl,	/* ioctl */
	open:		mtd_open,	/* open */
	release:	mtd_close,	/* release */
};


#if LINUX_VERSION_CODE < 0x20300
#ifdef MODULE
#define init_mtdchar init_module
#define cleanup_mtdchar cleanup_module
#endif
#endif

mod_init_t init_mtdchar(void)
{
	
	if (register_chrdev(MTD_CHAR_MAJOR,"mtd",&mtd_fops)) {
		printk(KERN_NOTICE "Can't allocate major number %d for Memory Technology Devices.\n",
		       MTD_CHAR_MAJOR);
		return EAGAIN;
	}

	return 0;
}

mod_exit_t cleanup_mtdchar(void)
{
	unregister_chrdev(MTD_CHAR_MAJOR,"mtd");
}

#if LINUX_VERSION_CODE > 0x20300
module_init(init_mtdchar);
module_exit(cleanup_mtdchar);
#endif
