/*
 * Radio Card Device Driver for Linux
 *
 * (c) 1997 Matthew Kirkwood <weejock@ferret.lmh.ox.ac.uk>
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/fs.h>

#include <linux/miscdevice.h>

#include <asm/uaccess.h>

#include <linux/config.h>

#include <linux/radio.h>
#ifdef CONFIG_RADIO_RTRACK
#include "rtrack.h"
#endif
#ifdef CONFIG_RADIO_WINRADIO
#include "winradio.h"
#endif

int radio_open(struct inode *inode, struct file *file);
int radio_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg);

/* /dev/radio interface */
static struct file_operations radio_fops = {
	NULL,			/* seek */
	NULL,			/* read */
	NULL,			/* write */
	NULL,			/* readdir */
	NULL,			/* select */
	&radio_ioctl,		/* ioctl */
	NULL,			/* mmap */
	&radio_open,		/* we're not allowed NULL, it seems... */
	NULL			/* release */
};

static struct miscdevice radio_miscdevice = {
	RADIO_MINOR,		/* minor device number */
	"radio",		/* title */
	&radio_fops,		/* file operations */
	NULL, NULL		/* previous and next (not our business) */
};


static struct radio_device *firstdevice, *lastdevice;
static int numdevices;

__initfunc(void radio_init(void))
{
	/* register the handler for the device number... */
	if(misc_register(&radio_miscdevice)) {
		printk("radio: couldn't register misc device\n");
		return;
	}

	/* do some general initialisation stuff */
	lastdevice = firstdevice = NULL;
	numdevices = 0;

#ifdef	CONFIG_RADIO_RTRACK
	radiotrack_init();
#endif
#ifdef	CONFIG_RADIO_WINRADIO
	printk("oooops.  no winradio support yet... :-(\n");
#endif
/* etc.... */

	printk("radio: registered %d devices\n", numdevices);
}


/* according to drivers/char/misc.c, the "open" call must not be NULL.
 * I'm not sure if I approve, but I didn't write it, so...
 */
int radio_open(struct inode *inode, struct file *file)
{
	return 0;
}


/* append a device to the linked list... */
int radio_add_device(struct radio_device *newdev)
{
	if(firstdevice == NULL) {
		firstdevice = newdev;
	} else {
		lastdevice->next = newdev;
	}
	lastdevice = newdev; numdevices++;
	newdev->cap->dev_num=numdevices;
	newdev->next = NULL;			/* don't need, but... */
	return(numdevices);
}

struct radio_device *getdev(int index)
{
struct radio_device *retval;

	if(index > numdevices)
		return NULL;		/* let's have a bit less of that */

	retval = firstdevice;
	for(;index;index--)
		retval = retval->next;

	return retval;
}


/* interface routine */
int radio_ioctl(struct inode *inode, struct file *file,
		unsigned int cmd, unsigned long arg)
{
struct radio_device *dev;
struct radio_ctl *ctl_arg = (struct radio_ctl*)arg;
int nowrite;
int val, ret;

	if((void*)arg == NULL)
		return -EFAULT;		/* XXX - check errnos are OK */


	switch(cmd) {
	case RADIO_NUMDEVS:
		return (put_user(numdevices,(int*)arg) ? -EFAULT : 0);


	case RADIO_GETCAPS:
	/* p'raps I should verify for read then write ?? */
		if(verify_area(VERIFY_WRITE, (void*)arg, sizeof(struct radio_cap)))
			return -EFAULT;
		if((dev = getdev(((struct radio_cap*)arg)->dev_num)) == NULL)
			return -EINVAL;
		copy_to_user((void*)arg, dev->cap, sizeof(struct radio_cap));
		return 0;


	case RADIO_GETBNDCAP:
		if(verify_area(VERIFY_WRITE, (void*)arg, sizeof(struct radio_band)))
			return -EFAULT;

		if((dev = getdev(((struct radio_band*)arg)->dev_num)) == NULL)
			return -EINVAL;

		val = ((struct radio_band*)arg)->index;
		if(val >= dev->cap->num_bwidths)
			return -EINVAL;			/* XXX errno */

		copy_to_user((void*)arg, (dev->bands)+(val*sizeof(struct radio_band)),
			sizeof(struct radio_band));
		return 0;
	}


/* now, we know that arg points to a struct radio_ctl */
	/* get the requested device */
	if(verify_area(VERIFY_READ, ctl_arg, sizeof(struct radio_ctl)))
		return -EFAULT;

	if((dev = getdev(ctl_arg->dev_num)) == NULL)
		return -EINVAL;

	nowrite = verify_area(VERIFY_WRITE, ctl_arg, sizeof(struct radio_ctl));

	val = ctl_arg->value;

	switch(cmd) {
	case RADIO_SETVOL:
		if((val < dev->cap->volmin) || (val > dev->cap->volmax))
			return -EINVAL;
		if((ret = (*(dev->setvol))(dev, val)))
			return ret;
		dev->curvol = val;
		return 0;

	case RADIO_GETVOL:
		if(nowrite)
			return -EFAULT;
		ctl_arg->value = dev->curvol;
		return 0;


	case RADIO_SETBAND:
		if(val >= dev->cap->num_bwidths)
			return -EINVAL;
		if((ret = (*(dev->setband))(dev, val)))
			return ret;
		dev->curband = val;
		return 0;

	case RADIO_GETBAND:
		if(nowrite)
			return -EFAULT;
		ctl_arg->value = dev->curband;
		return 0;


	case RADIO_SETFREQ: {
	struct radio_band *bp;

		bp = (dev->bands) + ((dev->curband) * sizeof(struct radio_band));
		if((val < bp->freqmin) || (val > bp->freqmax))
			return -EINVAL;
		if((ret = (*(dev->setfreq))(dev, val)))
			return ret;
		dev->curfreq = val;
		return 0;
	}

	case RADIO_GETFREQ:
		if(nowrite)
			return -EFAULT;
		ctl_arg->value = dev->curfreq;
		return 0;


	case RADIO_GETSIGSTR:
		if(nowrite)
			return -EFAULT;
		ctl_arg->value = (*(dev->getsigstr))(dev);
		return 0;


	default:
		return -ENOSYS;
	}
}
