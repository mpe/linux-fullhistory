/*
 * File...........: linux/drivers/s390/block/dasd_ioctl.c
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 *		    Horst Hummel <Horst.Hummel@de.ibm.com>
 *		    Carsten Otte <Cotte@de.ibm.com>
 *		    Martin Schwidefsky <schwidefsky@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 1999-2001
 *
 * i/o controls for the dasd driver.
 *
 * 05/04/02 split from dasd.c, code restructuring.
 */
#include <linux/config.h>
#include <linux/version.h>
#include <linux/interrupt.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/blkpg.h>
#include <linux/blk.h>

#include <asm/irq.h>
#include <asm/uaccess.h>

/* This is ugly... */
#define PRINTK_HEADER "dasd_ioctl:"

#include "dasd_int.h"

/*
 * SECTION: ioctl functions.
 */
static struct list_head dasd_ioctl_list = LIST_HEAD_INIT(dasd_ioctl_list);

/*
 * Find the ioctl with number no.
 */
static dasd_ioctl_list_t *
dasd_find_ioctl(int no)
{
	struct list_head *curr;
	list_for_each (curr, &dasd_ioctl_list) {
		if (list_entry (curr, dasd_ioctl_list_t, list)->no == no) {
			return list_entry (curr, dasd_ioctl_list_t, list);
		}
	}
	return NULL;
}

/*
 * Register ioctl with number no.
 */
int
dasd_ioctl_no_register(struct module *owner, int no, dasd_ioctl_fn_t handler)
{
	dasd_ioctl_list_t *new;
	if (dasd_find_ioctl(no))
		return -EBUSY;
	new = kmalloc(sizeof (dasd_ioctl_list_t), GFP_KERNEL);
	if (new == NULL)
		return -ENOMEM;
	new->owner = owner;
	new->no = no;
	new->handler = handler;
	list_add(&new->list, &dasd_ioctl_list);
	MOD_INC_USE_COUNT;
	return 0;
}

/*
 * Deregister ioctl with number no.
 */
int
dasd_ioctl_no_unregister(struct module *owner, int no, dasd_ioctl_fn_t handler)
{
	dasd_ioctl_list_t *old = dasd_find_ioctl(no);
	if (old == NULL)
		return -ENOENT;
	if (old->no != no || old->handler != handler || owner != old->owner)
		return -EINVAL;
	list_del(&old->list);
	kfree(old);
	MOD_DEC_USE_COUNT;
	return 0;
}

int
dasd_ioctl(struct inode *inp, struct file *filp,
	   unsigned int no, unsigned long data)
{
	dasd_devmap_t *devmap;
	dasd_device_t *device;
	dasd_ioctl_list_t *ioctl;
	struct block_device *bdev;
	struct list_head *l;
	const char *dir;
	int rc;

	if ((!inp) || kdev_none(inp->i_rdev))
		return -EINVAL;
	if ((_IOC_DIR(no) != _IOC_NONE) && (data == 0)) {
		PRINT_DEBUG("empty data ptr");
		return -EINVAL;
	}
	bdev = bdget(kdev_t_to_nr(inp->i_rdev));
	if (!bdev)
		return -EINVAL;

	devmap = dasd_devmap_from_bdev(bdev);
	device = (devmap != NULL) ?
		dasd_get_device(devmap) : ERR_PTR(-ENODEV);
	if (IS_ERR(device)) {
		MESSAGE(KERN_WARNING,
			"No device registered as device %s", bdevname(bdev));
		bdput(bdev);
		return -EINVAL;
	}
	dir = _IOC_DIR (no) == _IOC_NONE ? "0" :
		_IOC_DIR (no) == _IOC_READ ? "r" :
		_IOC_DIR (no) == _IOC_WRITE ? "w" : 
		_IOC_DIR (no) == (_IOC_READ | _IOC_WRITE) ? "rw" : "u";
	DBF_DEV_EVENT(DBF_DEBUG, device,
		      "ioctl 0x%08x %s'0x%x'%d(%d) with data %8lx", no,
		      dir, _IOC_TYPE(no), _IOC_NR(no), _IOC_SIZE(no), data);
	/* Search for ioctl no in the ioctl list. */
	list_for_each(l, &dasd_ioctl_list) {
		ioctl = list_entry(l, dasd_ioctl_list_t, list);
		if (ioctl->no == no) {
			/* Found a matching ioctl. Call it. */
			if (ioctl->owner) {
				if (try_inc_mod_count(ioctl->owner) != 0)
					continue;
				rc = ioctl->handler(bdev, no, data);
				__MOD_DEC_USE_COUNT(ioctl->owner);
			} else
				rc = ioctl->handler(bdev, no, data);
			dasd_put_device(devmap);
			bdput(bdev);
			return rc;
		}
	}
	/* No ioctl with number no. */
	DBF_DEV_EVENT(DBF_INFO, device,
		      "unknown ioctl 0x%08x=%s'0x%x'%d(%d) data %8lx", no,
		      dir, _IOC_TYPE(no), _IOC_NR(no), _IOC_SIZE(no), data);
	dasd_put_device(devmap);
	bdput(bdev);
	return -ENOTTY;
}

static int
dasd_ioctl_api_version(struct block_device *bdev, int no, long args)
{
	int ver = DASD_API_VERSION;
	return put_user(ver, (int *) args);
}

/*
 * Enable device.
 */
static int
dasd_ioctl_enable(struct block_device *bdev, int no, long args)
{
	dasd_devmap_t *devmap;
	dasd_device_t *device;
	int devno;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	devmap = dasd_devmap_from_bdev(bdev);
	device = (devmap != NULL) ?
		dasd_get_device(devmap) : ERR_PTR(-ENODEV);
	if (IS_ERR(device))
		return PTR_ERR(device);
	devno = device->devinfo.devno;
	dasd_enable_devices(devno, devno);
	dasd_put_device(devmap);
	return 0;
}

/*
 * Disable device.
 */
static int
dasd_ioctl_disable(struct block_device *bdev, int no, long args)
{
	dasd_devmap_t *devmap;
	dasd_device_t *device;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	devmap = dasd_devmap_from_bdev(bdev);
	device = (devmap != NULL) ?
		dasd_get_device(devmap) : ERR_PTR(-ENODEV);
	if (IS_ERR(device))
		return PTR_ERR(device);
	/*
	 * Man this is sick. We don't do a real disable but only downgrade
	 * the device to DASD_STATE_BASIC. The reason is that dasdfmt uses
	 * BIODASDDISABLE to disable accesses to the device via the block
	 * device layer but it still wants to do i/o on the device by
	 * using the BIODASDFMT ioctl. Therefore the correct state for the
	 * device is DASD_STATE_BASIC that allows to do basic i/o.
	 */
	dasd_set_target_state(device, DASD_STATE_BASIC);
	dasd_put_device(devmap);
	return 0;
}

/*
 * performs formatting of _device_ according to _fdata_
 * Note: The discipline's format_function is assumed to deliver formatting
 * commands to format a single unit of the device. In terms of the ECKD
 * devices this means CCWs are generated to format a single track.
 */
static int
dasd_format(dasd_device_t * device, format_data_t * fdata)
{
	dasd_ccw_req_t *cqr;
	int rc;

	if (device->discipline->format_device == NULL)
		return -EPERM;

	if (atomic_read(&device->open_count) > 1) {
		DEV_MESSAGE(KERN_WARNING, device, "%s",
			    "dasd_format: device is open! ");
		return -EBUSY;
	}

	DBF_DEV_EVENT(DBF_NOTICE, device,
		      "formatting units %d to %d (%d B blocks) flags %d",
		      fdata->start_unit,
		      fdata->stop_unit, fdata->blksize, fdata->intensity);

	while (fdata->start_unit <= fdata->stop_unit) {
		cqr = device->discipline->format_device(device, fdata);
		if (IS_ERR(cqr))
			return PTR_ERR(cqr);
		rc = dasd_sleep_on_interruptible(cqr);
		dasd_sfree_request(cqr, cqr->device);
		if (rc) {
			if (rc != -ERESTARTSYS)
				DEV_MESSAGE(KERN_ERR, device,
					    " Formatting of unit %d failed "
					    "with rc = %d",
					    fdata->start_unit, rc);
			return rc;
		}
		fdata->start_unit++;
	}
	return 0;
}

/*
 * Format device.
 */
static int
dasd_ioctl_format(struct block_device *bdev, int no, long args)
{
	dasd_devmap_t *devmap;
	dasd_device_t *device;
	format_data_t fdata;
	int partn, rc;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	if (!args)
		return -EINVAL;
	/* fdata == NULL is no longer a valid arg to dasd_format ! */
	partn = MINOR(bdev->bd_dev) & DASD_PARTN_MASK;
	devmap = dasd_devmap_from_bdev(bdev);
	device = (devmap != NULL) ?
		dasd_get_device(devmap) : ERR_PTR(-ENODEV);
	if (IS_ERR(device))
		return PTR_ERR(device);

	rc = 0;
	if (devmap->features & DASD_FEATURE_READONLY)
		rc = -EROFS;
	else if (copy_from_user(&fdata, (void *) args, sizeof (format_data_t)))
		rc = -EFAULT;
	else if (partn != 0) {
		DEV_MESSAGE(KERN_WARNING, device, "%s",
			    "Cannot low-level format a partition");
		rc = -EINVAL;
	} else
		rc = dasd_format(device, &fdata);
	dasd_put_device(devmap);
	return rc;
}

#ifdef CONFIG_DASD_PROFILE
/*
 * Reset device profile information
 */
static int
dasd_ioctl_reset_profile(struct block_device *bdev, int no, long args)
{
	dasd_devmap_t *devmap;
	dasd_device_t *device;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	devmap = dasd_devmap_from_bdev(bdev);
	device = (devmap != NULL) ?
		dasd_get_device(devmap) : ERR_PTR(-ENODEV);
	if (IS_ERR(device))
		return PTR_ERR(device);
	memset(&device->profile, 0, sizeof (dasd_profile_info_t));
	dasd_put_device(devmap);
	return 0;
}

/*
 * Return device profile information
 */
static int
dasd_ioctl_read_profile(struct block_device *bdev, int no, long args)
{
	dasd_devmap_t *devmap;
	dasd_device_t *device;
	int rc;

	devmap = dasd_devmap_from_bdev(bdev);
	device = (devmap != NULL) ?
		dasd_get_device(devmap) : ERR_PTR(-ENODEV);
	if (IS_ERR(device))
		return PTR_ERR(device);
	rc = 0;
	if (copy_to_user((long *) args, (long *) &device->profile,
			 sizeof (dasd_profile_info_t)))
		rc = -EFAULT;
	dasd_put_device(devmap);
	return rc;
}
#else
static int
dasd_ioctl_reset_profile(struct block_device *bdev, int no, long args)
{
	return -ENOSYS;
}

static int
dasd_ioctl_read_profile(struct block_device *bdev, int no, long args)
{
	return -ENOSYS;
}
#endif

/*
 * Return dasd information. Used for BIODASDINFO and BIODASDINFO2.
 */
static int
dasd_ioctl_information(struct block_device *bdev, int no, long args)
{
	dasd_devmap_t *devmap;
	dasd_device_t *device;
	dasd_information2_t *dasd_info;
	unsigned long flags;
	int rc;

	devmap = dasd_devmap_from_bdev(bdev);
	device = (devmap != NULL) ?
		dasd_get_device(devmap) : ERR_PTR(-ENODEV);
	if (IS_ERR(device))
		return PTR_ERR(device);
	if (!device->discipline->fill_info) {
		dasd_put_device(devmap);
		return -EINVAL;
	}

	dasd_info = kmalloc(sizeof(dasd_information2_t), GFP_KERNEL);
	if (dasd_info == NULL) {
		dasd_put_device(devmap);
		return -ENOMEM;
	}
	rc = device->discipline->fill_info(device, dasd_info);
	if (rc) {
		dasd_put_device(devmap);
		kfree(dasd_info);
		return rc;
	}

	dasd_info->devno = device->devinfo.devno;
	dasd_info->schid = device->devinfo.irq;
	dasd_info->cu_type = device->devinfo.sid_data.cu_type;
	dasd_info->cu_model = device->devinfo.sid_data.cu_model;
	dasd_info->dev_type = device->devinfo.sid_data.dev_type;
	dasd_info->dev_model = device->devinfo.sid_data.dev_model;
	dasd_info->open_count = atomic_read(&device->open_count);
	dasd_info->status = device->state;
	
	/*
	 * check if device is really formatted
	 * LDL / CDL was returned by 'fill_info'
	 */
	if ((device->state < DASD_STATE_READY) ||
	    (dasd_check_blocksize(device->bp_block)))
		dasd_info->format = DASD_FORMAT_NONE;
	
	dasd_info->features = devmap->features;
	
	if (device->discipline)
		memcpy(dasd_info->type, device->discipline->name, 4);
	else
		memcpy(dasd_info->type, "none", 4);
	dasd_info->req_queue_len = 0;
	dasd_info->chanq_len = 0;
	if (device->request_queue->request_fn) {
		struct list_head *l;
#ifdef DASD_EXTENDED_PROFILING
		{
			struct list_head *l;
			spin_lock_irqsave(&device->lock, flags);
			list_for_each(l, &device->request_queue->queue_head)
				dasd_info->req_queue_len++;
			spin_unlock_irqrestore(&device->lock, flags);
		}
#endif				/* DASD_EXTENDED_PROFILING */
		spin_lock_irqsave(get_irq_lock(device->devinfo.irq), flags);
		list_for_each(l, &device->ccw_queue)
			dasd_info->chanq_len++;
		spin_unlock_irqrestore(get_irq_lock(device->devinfo.irq),
				       flags);
	}
	
	rc = 0;
	if (copy_to_user((long *) args, (long *) dasd_info,
			 ((no == (unsigned int) BIODASDINFO2) ?
			  sizeof (dasd_information2_t) :
			  sizeof (dasd_information_t))))
		rc = -EFAULT;
	dasd_put_device(devmap);
	kfree(dasd_info);
	return rc;
}

/*
 * Set read only
 */
static int
dasd_ioctl_set_ro(struct block_device *bdev, int no, long args)
{
	dasd_devmap_t *devmap;
	dasd_device_t *device;
	int intval, i;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	if (MINOR(bdev->bd_dev) & DASD_PARTN_MASK)
		// ro setting is not allowed for partitions
		return -EINVAL;
	if (get_user(intval, (int *) args))
		return -EFAULT;
	devmap = dasd_devmap_from_bdev(bdev);
	device = (devmap != NULL) ?
		dasd_get_device(devmap) : ERR_PTR(-ENODEV);
	if (IS_ERR(device))
		return PTR_ERR(device);
	if (intval)
		devmap->features |= DASD_FEATURE_READONLY;
	else
		devmap->features &= ~DASD_FEATURE_READONLY;
	for (i = 0; i < (1 << DASD_PARTN_BITS); i++)
		set_device_ro(to_kdev_t(bdev->bd_dev + i), intval);
	dasd_put_device(devmap);
	return 0;
}

/*
 * Return disk geometry.
 */
static int
dasd_ioctl_getgeo(struct block_device *bdev, int no, long args)
{
	struct hd_geometry geo = { 0, };
	dasd_devmap_t *devmap;
	dasd_device_t *device;
	int rc;

	devmap = dasd_devmap_from_bdev(bdev);
	device = (devmap != NULL) ?
		dasd_get_device(devmap) : ERR_PTR(-ENODEV);
	if (IS_ERR(device))
		return PTR_ERR(device);
	rc = 0;
	if (device != NULL && device->discipline != NULL &&
	    device->discipline->fill_geometry != NULL) {
		device->discipline->fill_geometry(device, &geo);
		geo.start = get_start_sect(bdev);
		if (copy_to_user((struct hd_geometry *) args, &geo,
				 sizeof (struct hd_geometry)))
			rc = -EFAULT;
	} else
		rc = -EINVAL;
	dasd_put_device(devmap);
	return rc;
}

/*
 * List of static ioctls.
 */
static struct { int no; dasd_ioctl_fn_t fn; } dasd_ioctls[] =
{
	{ BIODASDDISABLE, dasd_ioctl_disable },
	{ BIODASDENABLE, dasd_ioctl_enable },
	{ BIODASDFMT, dasd_ioctl_format },
	{ BIODASDINFO, dasd_ioctl_information },
	{ BIODASDINFO2, dasd_ioctl_information },
	{ BIODASDPRRD, dasd_ioctl_read_profile },
	{ BIODASDPRRST, dasd_ioctl_reset_profile },
	{ BLKROSET, dasd_ioctl_set_ro },
	{ DASDAPIVER, dasd_ioctl_api_version },
	{ HDIO_GETGEO, dasd_ioctl_getgeo },
	{ -1, NULL }
};

int
dasd_ioctl_init(void)
{
	int i;

	for (i = 0; dasd_ioctls[i].no != -1; i++)
		dasd_ioctl_no_register(NULL, dasd_ioctls[i].no,
				       dasd_ioctls[i].fn);
	return 0;

}

void
dasd_ioctl_exit(void)
{
	int i;

	for (i = 0; dasd_ioctls[i].no != -1; i++)
		dasd_ioctl_no_unregister(NULL, dasd_ioctls[i].no,
					 dasd_ioctls[i].fn);

}

EXPORT_SYMBOL(dasd_ioctl_no_register);
EXPORT_SYMBOL(dasd_ioctl_no_unregister);
