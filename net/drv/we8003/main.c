/*
 * we8003.c	A generic WD8003 driver for LINUX.
 *
 * Version:	@(#)we8003.c	1.0.0	04/22/93
 *
 * Author:	Fred N. van Kempen, <waltje@uwalt.nl.mugnet.org>
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/tty.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/string.h>
#include <linux/ddi.h>
#include <asm/system.h>
#include <asm/segment.h>
#include <asm/io.h>
#include <errno.h>
#include <linux/fcntl.h>
#include <netinet/in.h>
#include "we8003.h"


#define VERSION		"1.0.0"		/* current version ID		*/


struct ddi_device *we_ptrs[NR_WE8003];	/* pointers to DDI blocks	*/


static int
we_getconf(struct ddi_device *dev, struct ddconf *cp)
{
  cp->ioaddr = dev->config.ioaddr;	/* I/O base address		*/
  cp->ioaux = 0;			/* not used			*/
  cp->irq = dev->config.irq;		/* IRQ channel			*/
  cp->dma = 0;				/* not used			*/
  cp->memaddr = dev->config.memaddr;	/* RAM base address		*/
  cp->memsize = dev->config.memsize;	/* RAM size			*/
  return(0);
}


static int
we_setconf(struct ddi_device *dev, struct ddconf *cp)
{
  dev->config.ioaddr = cp->ioaddr;	/* I/O base address		*/
  dev->config.irq = cp->irq;		/* IRQ channel			*/
  dev->config.memaddr = cp->memaddr;	/* RAM base address		*/
  dev->config.memsize = cp->memsize;	/* RAM size			*/
  PRINTK (("%s: IO=0x%X IRQ=%d MEM=0x%X(%d)\n",
	dev->name, dev->config.ioaddr, dev->config.irq,
	dev->config.memaddr, dev->config.memsize));

  /* FIXME: request the IRQ line and initialize HW here! */

  return(0);
}


static int
we_open(struct inode * inode, struct file * file)
{
  int minor;
  struct ddi_device *dev;

  minor = MINOR(inode->i_rdev);
  if (minor < 0 || minor >= NR_WE8003) return(-ENODEV);
  dev = we_ptrs[minor];
  if (dev == NULL || (dev->flags & DDI_FREADY) == 0) return(-ENODEV);

  return(0);
}


static void
we_close(struct inode * inode, struct file * file)
{
  int minor;
  struct ddi_device *dev;

  minor = MINOR(inode->i_rdev);
  if (minor < 0 || minor >= NR_WE8003) return;
  dev = we_ptrs[minor];
  if (dev == NULL || (dev->flags & DDI_FREADY) == 0) return;
}


static int
we_ioctl(struct inode *inode, struct file *file,
	 unsigned int cmd, unsigned long arg)
{
  int minor, ret;
  struct ddi_device *dev;
  struct ddconf conf;

  minor = MINOR(inode->i_rdev);
  if (minor < 0 || minor >= NR_WE8003) return(-ENODEV);
  dev = we_ptrs[minor];
  if (dev == NULL || (dev->flags & DDI_FREADY) == 0) return(-ENODEV);

  ret = -EINVAL;
  switch(cmd) {
	case DDIOCGNAME:
		memcpy_tofs((void *)arg, dev->name, DDI_MAXNAME);
		ret = 0;
		break;
	case DDIOCGCONF:
		ret = we_getconf(dev, &conf);
		memcpy_tofs((void *)arg, &conf, sizeof(conf));
		break;
	case DDIOCSCONF:
		memcpy_fromfs(&conf, (void *)arg, sizeof(conf));
		ret = we_setconf(dev, &conf);
		break;
	default:
		break;
  }
  return(ret);
}


static struct file_operations we_fops = {
  NULL,		/* LSEEK	*/
  NULL,		/* READ		*/
  NULL,		/* WRITE	*/
  NULL,		/* READDIR	*/
  NULL,		/* SELECT	*/
  we_ioctl,	/* IOCTL	*/
  NULL,		/* MMAP		*/
  we_open,	/* OPEN		*/
  we_close	/* CLOSE	*/
};


/* This is the main entry point of this driver. */
int
we8003_init(struct ddi_device *dev)
{
  static int unit_nr = 0;
  int i;

  /* Initialize the driver if this is the first call. */
  if (unit_nr == 0) {
	for(i = 0; i < NR_WE8003; i++) we_ptrs[i] = NULL;
  }

  /* Initialize the local control block pointer. */
  we_ptrs[unit_nr] = dev;
  dev->unit = unit_nr++;
  sprintf(dev->name, WE_NAME, dev->unit);
  dev->flags |= DDI_FREADY;

  /* Say hello to our viewers. */
  PRINTK (("%s: version %s: ", dev->title, VERSION));
  (void) we_setconf(dev, &dev->config);

  /* First of all, setup a VFS major device handler if needed. */
  if (dev->major != 0) {
	if (dev->flags & DDI_FBLKDEV) {
		if (register_blkdev(dev->major, "WE8003", &we_fops) < 0) {
			printk("%s: cannot register block device %d!\n",
					dev->name, dev->major);
			return(-EINVAL);
		}
	}
	if (dev->flags & DDI_FCHRDEV) {
		if (register_chrdev(dev->major, "WE8003", &we_fops) < 0) {
			printk("%s: cannot register character device %d!\n",
					dev->name, dev->major);
			return(-EINVAL);
		}
	}
  }

  /* All done... */
  return(0);
}
