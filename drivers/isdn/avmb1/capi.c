/*
 * $Id: capi.c,v 1.22 1999/11/13 21:27:16 keil Exp $
 *
 * CAPI 2.0 Interface for Linux
 *
 * Copyright 1996 by Carsten Paeth (calle@calle.in-berlin.de)
 *
 * $Log: capi.c,v $
 * Revision 1.22  1999/11/13 21:27:16  keil
 * remove KERNELVERSION
 *
 * Revision 1.21  1999/09/10 17:24:18  calle
 * Changes for proposed standard for CAPI2.0:
 * - AK148 "Linux Exention"
 *
 * Revision 1.20  1999/09/07 09:02:53  calle
 * SETDATA removed. Now inside the kernel the datapart of DATA_B3_REQ and
 * DATA_B3_IND is always directly after the CAPI message. The "Data" member
 * ist never used inside the kernel.
 *
 * Revision 1.19  1999/07/09 15:05:42  keil
 * compat.h is now isdn_compat.h
 *
 * Revision 1.18  1999/07/06 07:42:01  calle
 * - changes in /proc interface
 * - check and changed calls to [dev_]kfree_skb and [dev_]alloc_skb.
 *
 * Revision 1.17  1999/07/01 15:26:30  calle
 * complete new version (I love it):
 * + new hardware independed "capi_driver" interface that will make it easy to:
 *   - support other controllers with CAPI-2.0 (i.e. USB Controller)
 *   - write a CAPI-2.0 for the passive cards
 *   - support serial link CAPI-2.0 boxes.
 * + wrote "capi_driver" for all supported cards.
 * + "capi_driver" (supported cards) now have to be configured with
 *   make menuconfig, in the past all supported cards where included
 *   at once.
 * + new and better informations in /proc/capi/
 * + new ioctl to switch trace of capi messages per controller
 *   using "avmcapictrl trace [contr] on|off|...."
 * + complete testcircle with all supported cards and also the
 *   PCMCIA cards (now patch for pcmcia-cs-3.0.13 needed) done.
 *
 * Revision 1.16  1999/07/01 08:22:57  keil
 * compatibility macros now in <linux/isdn_compat.h>
 *
 * Revision 1.15  1999/06/21 15:24:11  calle
 * extend information in /proc.
 *
 * Revision 1.14  1999/06/10 16:51:03  calle
 * Bugfix: open/release of control device was not handled correct.
 *
 * Revision 1.13  1998/08/28 04:32:25  calle
 * Added patch send by Michael.Mueller4@post.rwth-aachen.de, to get AVM B1
 * driver running with 2.1.118.
 *
 * Revision 1.12  1998/05/26 22:39:34  he
 * sync'ed with 2.1.102 where appropriate (CAPABILITY changes)
 * concap typo
 * cleared dev.tbusy in isdn_net BCONN status callback
 *
 * Revision 1.11  1998/03/09 17:46:37  he
 * merged in 2.1.89 changes
 *
 * Revision 1.10  1998/02/13 07:09:13  calle
 * change for 2.1.86 (removing FREE_READ/FREE_WRITE from [dev]_kfree_skb()
 *
 * Revision 1.9  1998/01/31 11:14:44  calle
 * merged changes to 2.0 tree, prepare 2.1.82 to work.
 *
 * Revision 1.8  1997/11/04 06:12:08  calle
 * capi.c: new read/write in file_ops since 2.1.60
 * capidrv.c: prepared isdnlog interface for d2-trace in newer firmware.
 * capiutil.c: needs config.h (CONFIG_ISDN_DRV_AVMB1_VERBOSE_REASON)
 * compat.h: added #define LinuxVersionCode
 *
 * Revision 1.7  1997/10/11 10:29:34  calle
 * llseek() parameters changed in 2.1.56.
 *
 * Revision 1.6  1997/10/01 09:21:15  fritz
 * Removed old compatibility stuff for 2.0.X kernels.
 * From now on, this code is for 2.1.X ONLY!
 * Old stuff is still in the separate branch.
 *
 * Revision 1.5  1997/08/21 23:11:55  fritz
 * Added changes for kernels >= 2.1.45
 *
 * Revision 1.4  1997/05/27 15:17:50  fritz
 * Added changes for recent 2.1.x kernels:
 *   changed return type of isdn_close
 *   queue_task_* -> queue_task
 *   clear/set_bit -> test_and_... where apropriate.
 *   changed type of hard_header_cache parameter.
 *
 * Revision 1.3  1997/05/18 09:24:14  calle
 * added verbose disconnect reason reporting to avmb1.
 * some fixes in capi20 interface.
 * changed info messages for B1-PCI
 *
 * Revision 1.2  1997/03/05 21:17:59  fritz
 * Added capi_poll for compiling under 2.1.27
 *
 * Revision 1.1  1997/03/04 21:50:29  calle
 * Frirst version in isdn4linux
 *
 * Revision 2.2  1997/02/12 09:31:39  calle
 * new version
 *
 * Revision 1.1  1997/01/31 10:32:20  calle
 * Initial revision
 *
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/malloc.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/signal.h>
#include <linux/mm.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/skbuff.h>
#include <linux/proc_fs.h>
#include <linux/poll.h>
#include <linux/capi.h>
#include <linux/kernelcapi.h>

#include "capiutil.h"
#include "capicmd.h"
#include "capidev.h"

MODULE_AUTHOR("Carsten Paeth (calle@calle.in-berlin.de)");

/* -------- driver information -------------------------------------- */

int capi_major = 68;		/* allocated */

MODULE_PARM(capi_major, "i");

/* -------- global variables ---------------------------------------- */

static struct capidev capidevs[CAPI_MAXMINOR + 1];
struct capi_interface *capifuncs;

/* -------- function called by lower level -------------------------- */

static void capi_signal(__u16 applid, __u32 minor)
{
	struct capidev *cdev;
	struct sk_buff *skb = 0;

	if (minor > CAPI_MAXMINOR || !capidevs[minor].is_registered) {
		printk(KERN_ERR "BUG: capi_signal: illegal minor %d\n", minor);
		return;
	}
	cdev = &capidevs[minor];
	(void) (*capifuncs->capi_get_message) (applid, &skb);
	if (skb) {
		skb_queue_tail(&cdev->recv_queue, skb);
		wake_up_interruptible(&cdev->recv_wait);
	} else {
		printk(KERN_ERR "BUG: capi_signal: no skb\n");
	}
}

/* -------- file_operations ----------------------------------------- */

static long long capi_llseek(struct file *file,
			     long long offset, int origin)
{
	return -ESPIPE;
}

static ssize_t capi_read(struct file *file, char *buf,
		      size_t count, loff_t *ppos)
{
        struct inode *inode = file->f_dentry->d_inode;
	unsigned int minor = MINOR(inode->i_rdev);
	struct capidev *cdev;
	struct sk_buff *skb;
	int retval;
	size_t copied;

       if (ppos != &file->f_pos)
		return -ESPIPE;

	if (!minor || minor > CAPI_MAXMINOR || !capidevs[minor].is_registered)
		return -ENODEV;

	cdev = &capidevs[minor];

	if ((skb = skb_dequeue(&cdev->recv_queue)) == 0) {

		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		for (;;) {
			interruptible_sleep_on(&cdev->recv_wait);
			if ((skb = skb_dequeue(&cdev->recv_queue)) != 0)
				break;
			if (signal_pending(current))
				break;
		}
		if (skb == 0)
			return -ERESTARTNOHAND;
	}
	if (skb->len > count) {
		skb_queue_head(&cdev->recv_queue, skb);
		return -EMSGSIZE;
	}
	retval = copy_to_user(buf, skb->data, skb->len);
	if (retval) {
		skb_queue_head(&cdev->recv_queue, skb);
		return retval;
	}
	copied = skb->len;

	if (CAPIMSG_COMMAND(skb->data) == CAPI_DATA_B3
	    && CAPIMSG_SUBCOMMAND(skb->data) == CAPI_IND)
		cdev->nrecvdatapkt++;
        else cdev->nrecvctlpkt++;
	kfree_skb(skb);

	return copied;
}

static ssize_t capi_write(struct file *file, const char *buf,
		       size_t count, loff_t *ppos)
{
        struct inode *inode = file->f_dentry->d_inode;
	unsigned int minor = MINOR(inode->i_rdev);
	struct capidev *cdev;
	struct sk_buff *skb;
	int retval;
	__u8 cmd;
	__u8 subcmd;
	__u16 mlen;

       if (ppos != &file->f_pos)
		return -ESPIPE;

	if (!minor || minor > CAPI_MAXMINOR || !capidevs[minor].is_registered)
		return -ENODEV;

	cdev = &capidevs[minor];

	skb = alloc_skb(count, GFP_USER);

	if ((retval = copy_from_user(skb_put(skb, count), buf, count))) {
		kfree_skb(skb);
		return retval;
	}
	cmd = CAPIMSG_COMMAND(skb->data);
	subcmd = CAPIMSG_SUBCOMMAND(skb->data);
	mlen = CAPIMSG_LEN(skb->data);
	if (cmd == CAPI_DATA_B3 && subcmd == CAPI_REQ) {
		__u16 dlen = CAPIMSG_DATALEN(skb->data);
		if (mlen + dlen != count) {
			kfree_skb(skb);
			return -EINVAL;
		}
	} else if (mlen != count) {
		kfree_skb(skb);
		return -EINVAL;
	}
	CAPIMSG_SETAPPID(skb->data, cdev->applid);

	cdev->errcode = (*capifuncs->capi_put_message) (cdev->applid, skb);

	if (cdev->errcode) {
		kfree_skb(skb);
		return -EIO;
	}
	if (cmd == CAPI_DATA_B3 && subcmd == CAPI_REQ)
		cdev->nsentdatapkt++;
	else cdev->nsentctlpkt++;
	return count;
}

static unsigned int
capi_poll(struct file *file, poll_table * wait)
{
	unsigned int mask = 0;
	unsigned int minor = MINOR(file->f_dentry->d_inode->i_rdev);
	struct capidev *cdev;

	if (!minor || minor > CAPI_MAXMINOR || !capidevs[minor].is_registered)
		return POLLERR;

	cdev = &capidevs[minor];
	poll_wait(file, &(cdev->recv_wait), wait);
	mask = POLLOUT | POLLWRNORM;
	if (!skb_queue_empty(&cdev->recv_queue))
		mask |= POLLIN | POLLRDNORM;
	return mask;
}

static int capi_ioctl(struct inode *inode, struct file *file,
		      unsigned int cmd, unsigned long arg)
{
	unsigned int minor = MINOR(inode->i_rdev);
	struct capidev *cdev;
	capi_ioctl_struct data;
	int retval;


	if (minor >= CAPI_MAXMINOR || !capidevs[minor].is_open)
		return -ENODEV;

	cdev = &capidevs[minor];

	switch (cmd) {
	case CAPI_REGISTER:
		{
			if (!minor)
				return -EINVAL;
			retval = copy_from_user((void *) &data.rparams,
						(void *) arg, sizeof(struct capi_register_params));
			if (retval)
				return -EFAULT;
			if (cdev->is_registered)
				return -EEXIST;
			cdev->errcode = (*capifuncs->capi_register) (&data.rparams,
							  &cdev->applid);
			if (cdev->errcode)
				return -EIO;
			(void) (*capifuncs->capi_set_signal) (cdev->applid, capi_signal, minor);
			cdev->is_registered = 1;
		}
		return 0;

	case CAPI_GET_VERSION:
		{
			retval = copy_from_user((void *) &data.contr,
						(void *) arg,
						sizeof(data.contr));
			if (retval)
				return -EFAULT;
		        cdev->errcode = (*capifuncs->capi_get_version) (data.contr, &data.version);
			if (cdev->errcode)
				return -EIO;
			retval = copy_to_user((void *) arg,
					      (void *) &data.version,
					      sizeof(data.version));
			if (retval)
				return -EFAULT;
		}
		return 0;

	case CAPI_GET_SERIAL:
		{
			retval = copy_from_user((void *) &data.contr,
						(void *) arg,
						sizeof(data.contr));
			if (retval)
				return -EFAULT;
			cdev->errcode = (*capifuncs->capi_get_serial) (data.contr, data.serial);
			if (cdev->errcode)
				return -EIO;
			retval = copy_to_user((void *) arg,
					      (void *) data.serial,
					      sizeof(data.serial));
			if (retval)
				return -EFAULT;
		}
		return 0;
	case CAPI_GET_PROFILE:
		{
			retval = copy_from_user((void *) &data.contr,
						(void *) arg,
						sizeof(data.contr));
			if (retval)
				return -EFAULT;

			if (data.contr == 0) {
				cdev->errcode = (*capifuncs->capi_get_profile) (data.contr, &data.profile);
				if (cdev->errcode)
					return -EIO;

				retval = copy_to_user((void *) arg,
				      (void *) &data.profile.ncontroller,
				       sizeof(data.profile.ncontroller));

			} else {
				cdev->errcode = (*capifuncs->capi_get_profile) (data.contr, &data.profile);
				if (cdev->errcode)
					return -EIO;

				retval = copy_to_user((void *) arg,
						  (void *) &data.profile,
						   sizeof(data.profile));
			}
			if (retval)
				return -EFAULT;
		}
		return 0;

	case CAPI_GET_MANUFACTURER:
		{
			retval = copy_from_user((void *) &data.contr,
						(void *) arg,
						sizeof(data.contr));
			if (retval)
				return -EFAULT;
			cdev->errcode = (*capifuncs->capi_get_manufacturer) (data.contr, data.manufacturer);
			if (cdev->errcode)
				return -EIO;

			retval = copy_to_user((void *) arg, (void *) data.manufacturer,
					      sizeof(data.manufacturer));
			if (retval)
				return -EFAULT;

		}
		return 0;
	case CAPI_GET_ERRCODE:
		data.errcode = cdev->errcode;
		cdev->errcode = CAPI_NOERROR;
		if (arg) {
			retval = copy_to_user((void *) arg,
					      (void *) &data.errcode,
					      sizeof(data.errcode));
			if (retval)
				return -EFAULT;
		}
		return data.errcode;

	case CAPI_INSTALLED:
		if ((*capifuncs->capi_isinstalled)() == CAPI_NOERROR)
			return 0;
		return -ENXIO;

	case CAPI_MANUFACTURER_CMD:
		{
			struct capi_manufacturer_cmd mcmd;
			if (minor)
				return -EINVAL;
			if (!capable(CAP_SYS_ADMIN))
				return -EPERM;
			retval = copy_from_user((void *) &mcmd, (void *) arg,
						sizeof(mcmd));
			if (retval)
				return -EFAULT;
			return (*capifuncs->capi_manufacturer) (mcmd.cmd, mcmd.data);
		}
		return 0;
	}
	return -EINVAL;
}

static int capi_open(struct inode *inode, struct file *file)
{
	unsigned int minor = MINOR(inode->i_rdev);

	if (minor >= CAPI_MAXMINOR)
		return -ENXIO;

	if (minor) {
		if (capidevs[minor].is_open)
			return -EEXIST;

		capidevs[minor].is_open = 1;
		skb_queue_head_init(&capidevs[minor].recv_queue);
		MOD_INC_USE_COUNT;
		capidevs[minor].nopen++;

	} else {
		capidevs[minor].is_open++;
		MOD_INC_USE_COUNT;
	}

	return 0;
}

static int
capi_release(struct inode *inode, struct file *file)
{
	unsigned int minor = MINOR(inode->i_rdev);
	struct capidev *cdev;
	struct sk_buff *skb;

	if (minor >= CAPI_MAXMINOR || !capidevs[minor].is_open) {
		printk(KERN_ERR "capi20: release minor %d ???\n", minor);
		return 0;
	}
	cdev = &capidevs[minor];

	if (minor) {

		if (cdev->is_registered)
			(*capifuncs->capi_release) (cdev->applid);

		cdev->is_registered = 0;
		cdev->applid = 0;

		while ((skb = skb_dequeue(&cdev->recv_queue)) != 0) {
			kfree_skb(skb);
		}
		cdev->is_open = 0;
	} else {
		cdev->is_open--;
	}

	MOD_DEC_USE_COUNT;
	return 0;
}

static struct file_operations capi_fops =
{
	capi_llseek,
	capi_read,
	capi_write,
	NULL,			/* capi_readdir */
	capi_poll,
	capi_ioctl,
	NULL,			/* capi_mmap */
	capi_open,
        NULL,                   /* capi_flush */
	capi_release,
	NULL,			/* capi_fsync */
	NULL,			/* capi_fasync */
};

/* -------- /proc functions ----------------------------------- */

/*
 * /proc/capi/capi20:
 *  minor opencount nrecvctlpkt nrecvdatapkt nsendctlpkt nsenddatapkt
 */
static int proc_capidev_read_proc(char *page, char **start, off_t off,
                                       int count, int *eof, void *data)
{
        struct capidev *cp;
	int i;
	int len = 0;
	off_t begin = 0;

	for (i=0; i < CAPI_MAXMINOR; i++) {
		cp = &capidevs[i+1];
		if (cp->nopen == 0) continue;
		len += sprintf(page+len, "%d %lu %lu %lu %lu %lu\n",
			i+1,
			cp->nopen,
			cp->nrecvctlpkt,
			cp->nrecvdatapkt,
			cp->nsentctlpkt,
			cp->nsentdatapkt);
		if (len+begin > off+count)
			goto endloop;
		if (len+begin < off) {
			begin += len;
			len = 0;
		}
	}
endloop:
	if (i >= CAPI_MAXMINOR)
		*eof = 1;
	if (off >= len+begin)
		return 0;
	*start = page + (begin-off);
	return ((count < begin+len-off) ? count : begin+len-off);
}

static struct procfsentries {
  char *name;
  mode_t mode;
  int (*read_proc)(char *page, char **start, off_t off,
                                       int count, int *eof, void *data);
  struct proc_dir_entry *procent;
} procfsentries[] = {
   /* { "capi",		  S_IFDIR, 0 }, */
   { "capi/capi20", 	  0	 , proc_capidev_read_proc },
};

static void proc_init(void)
{
    int nelem = sizeof(procfsentries)/sizeof(procfsentries[0]);
    int i;

    for (i=0; i < nelem; i++) {
        struct procfsentries *p = procfsentries + i;
	p->procent = create_proc_entry(p->name, p->mode, 0);
	if (p->procent) p->procent->read_proc = p->read_proc;
    }
}

static void proc_exit(void)
{
    int nelem = sizeof(procfsentries)/sizeof(procfsentries[0]);
    int i;

    for (i=nelem-1; i >= 0; i--) {
        struct procfsentries *p = procfsentries + i;
	if (p->procent) {
	   remove_proc_entry(p->name, 0);
	   p->procent = 0;
	}
    }
}
/* -------- init function and module interface ---------------------- */

#ifdef MODULE
#define	 capi_init	init_module
#endif

static struct capi_interface_user cuser = {
	"capi20",
	0,
};

int capi_init(void)
{
	int j;
	
	memset(capidevs, 0, sizeof(capidevs));
	for ( j = 0; j < CAPI_MAXMINOR+1; j++ ) {
		init_waitqueue_head(&capidevs[j].recv_wait);
	}

	if (register_chrdev(capi_major, "capi20", &capi_fops)) {
		printk(KERN_ERR "capi20: unable to get major %d\n", capi_major);
		return -EIO;
	}
	printk(KERN_NOTICE "capi20: started up with major %d\n", capi_major);

	if ((capifuncs = attach_capi_interface(&cuser)) == 0) {
		unregister_chrdev(capi_major, "capi20");
		return -EIO;
	}
	(void)proc_init();
	return 0;
}

#ifdef MODULE
void cleanup_module(void)
{
	(void)proc_exit();
	unregister_chrdev(capi_major, "capi20");
	(void) detach_capi_interface(&cuser);
}

#endif
