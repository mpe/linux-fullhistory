#include <linux/config.h>
#ifdef CONFIG_BLK_DEV_SD
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include "scsi.h"
#include "sd.h"

extern int scsi_ioctl (Scsi_Device *dev, int cmd, void *arg);

int sd_ioctl(struct inode * inode, struct file * file, unsigned long cmd, unsigned long arg)
{
	int dev = inode->i_rdev;
	
	switch (cmd) {
		default:
			return scsi_ioctl(rscsi_disks[MINOR(dev) >> 4].device, cmd, (void *) arg);
	}
}
#endif
