/*
 * scsi_sysfs.c
 *
 * SCSI sysfs interface routines.
 *
 * Created to pull SCSI mid layer sysfs routines into one file.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/device.h>
#include "scsi.h"
#include "hosts.h"

/**
 * scsi_host_class_name_show - copy out the SCSI host name
 * @dev:		device to check
 * @page:		copy data into this area
 * @count:		number of bytes to copy
 * @off:		start at this offset in page
 * Return:
 *     number of bytes written into page.
 **/
static ssize_t scsi_host_class_name_show(struct device *dev, char *page)
{
	struct Scsi_Host *shost;

	shost = to_scsi_host(dev);

	if (!shost)
		return 0;
	
	return snprintf(page, 20, "scsi%d\n", shost->host_no);
}

DEVICE_ATTR(class_name, S_IRUGO, scsi_host_class_name_show, NULL);

static int scsi_host_class_add_dev(struct device * dev)
{
	device_create_file(dev, &dev_attr_class_name);
	return 0;
}

static void scsi_host_class_rm_dev(struct device * dev)
{
	device_remove_file(dev, &dev_attr_class_name);
}

struct device_class shost_devclass = {
	.name		= "scsi-host",
	.add_device	= scsi_host_class_add_dev,
	.remove_device	= scsi_host_class_rm_dev,
};

/**
 * scsi_bus_match:
 * @dev:
 * @dev_driver:
 *
 * Return value:
 **/
static int scsi_bus_match(struct device *dev, 
                          struct device_driver *dev_driver)
{
        if (!strcmp("sg", dev_driver->name)) {
                if (strstr(dev->bus_id, ":gen"))
                        return 1;
        } else if (!strcmp("st",dev_driver->name)) {
                if (strstr(dev->bus_id,":mt"))
                        return 1;
        } else if (!strcmp("sd", dev_driver->name)) {
                if ((!strstr(dev->bus_id, ":gen")) && 
		    (!strstr(dev->bus_id, ":mt"))) { 
                        return 1;
                }
	}
        return 0;
}


static struct bus_type scsi_bus_type = {
        .name		= "scsi",
        .match		= scsi_bus_match,
};


int scsi_sysfs_register(void)
{
	bus_register(&scsi_bus_type);
	devclass_register(&shost_devclass);

	return 0;
}

void scsi_sysfs_unregister(void)
{
	devclass_unregister(&shost_devclass);
	bus_unregister(&scsi_bus_type);
}

/**
 * scsi_upper_driver_register - register upper level driver.
 * @sdev_tp:	Upper level driver to register with the scsi bus.
 *
 * Return value:
 * 	0 on Success / non-zero on Failure
 **/
int scsi_upper_driver_register(struct Scsi_Device_Template *sdev_tp)
{
	int error = 0;

	sdev_tp->scsi_driverfs_driver.bus = &scsi_bus_type;
	error = driver_register(&sdev_tp->scsi_driverfs_driver);

	return error;
}

/**
 * scsi_upper_driver_unregister - unregister upper level driver 
 * @sdev_tp:	Upper level driver to unregister with the scsi bus.
 *
 **/
void scsi_upper_driver_unregister(struct Scsi_Device_Template *sdev_tp)
{
	driver_unregister(&sdev_tp->scsi_driverfs_driver);
}


/*
 * show_function: macro to create an attr function that can be used to
 * show a non-bit field.
 */
#define show_function(field, format_string)				\
static ssize_t								\
show_##field (struct device *dev, char *buf)				\
{									\
	struct scsi_device *sdev;					\
	sdev = to_scsi_device(dev);					\
	return snprintf (buf, 20, format_string, sdev->field);		\
}									\

/*
 * sdev_rd_attr: macro to create a function and attribute variable for a
 * read only field.
 */
#define sdev_rd_attr(field, format_string)				\
	show_function(field, format_string)				\
static DEVICE_ATTR(field, S_IRUGO, show_##field, NULL)


/*
 * sdev_rd_attr: create a function and attribute variable for a
 * read/write field.
 */
#define sdev_rw_attr(field, format_string)				\
	show_function(field, format_string)				\
									\
static ssize_t								\
store_##field (struct device *dev, const char *buf, size_t count)	\
{									\
	struct scsi_device *sdev;					\
	sdev = to_scsi_device(dev);					\
	snscanf (buf, 20, format_string, &sdev->field);			\
	return count;							\
}									\
static DEVICE_ATTR(field, S_IRUGO | S_IWUSR, show_##field, store_##field)

/*
 * sdev_rd_attr: create a function and attribute variable for a
 * read/write bit field.
 */
#define sdev_rw_attr_bit(field)						\
	show_function(field, "%d\n")					\
									\
static ssize_t								\
store_##field (struct device *dev, const char *buf, size_t count)	\
{									\
	int ret;							\
	struct scsi_device *sdev;					\
	ret = scsi_sdev_check_buf_bit(buf);				\
	if (ret >= 0)	{						\
		sdev = to_scsi_device(dev);				\
		sdev->field = ret;					\
		ret = count;						\
	}								\
	return ret;							\
}									\
static DEVICE_ATTR(field, S_IRUGO | S_IWUSR, show_##field, store_##field)

/*
 * scsi_sdev_check_buf_bit: return 0 if buf is "0", return 1 if buf is "1",
 * else return -EINVAL.
 */
static int scsi_sdev_check_buf_bit(const char *buf)
{
	if ((buf[1] == '\0') || ((buf[1] == '\n') && (buf[2] == '\0'))) {
		if (buf[0] == '1')
			return 1;
		else if (buf[0] == '0')
			return 0;
		else 
			return -EINVAL;
	} else
		return -EINVAL;
}

/*
 * Create the actual show/store functions and data structures.
 */
sdev_rd_attr (device_blocked, "%d\n");
sdev_rd_attr (current_queue_depth, "%d\n");
sdev_rd_attr (new_queue_depth, "%d\n");
sdev_rd_attr (type, "%d\n");
sdev_rd_attr (access_count, "%d\n");
sdev_rd_attr (vendor, "%.8s\n");
sdev_rd_attr (model, "%.16s\n");
sdev_rd_attr (rev, "%.4s\n");
sdev_rw_attr_bit (online);

static struct device_attribute * const sdev_attrs[] = {
	&dev_attr_device_blocked,
	&dev_attr_current_queue_depth,
	&dev_attr_new_queue_depth,
	&dev_attr_type,
	&dev_attr_access_count,
	&dev_attr_vendor,
	&dev_attr_model,
	&dev_attr_rev,
	&dev_attr_online,
};

/**
 * scsi_device_register - register a scsi device with the scsi bus
 * @sdev:	scsi_device to register
 *
 * Return value:
 * 	0 on Success / non-zero on Failure
 **/
int scsi_device_register(struct scsi_device *sdev)
{
	int error = 0, i;

	sprintf(sdev->sdev_driverfs_dev.bus_id,"%d:%d:%d:%d",
		sdev->host->host_no, sdev->channel, sdev->id, sdev->lun);
	sdev->sdev_driverfs_dev.parent = sdev->host->host_gendev;
	sdev->sdev_driverfs_dev.bus = &scsi_bus_type;

	error = device_register(&sdev->sdev_driverfs_dev);
	if (error)
		return error;

	for (i = 0; !error && i < ARRAY_SIZE(sdev_attrs); i++)
		error = device_create_file(&sdev->sdev_driverfs_dev,
					   sdev_attrs[i]);

	if (error)
		scsi_device_unregister(sdev);

	return error;
}

/**
 * scsi_device_unregister - unregister a device from the scsi bus
 * @sdev:	scsi_device to unregister
 **/
void scsi_device_unregister(struct scsi_device *sdev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sdev_attrs); i++)
		device_remove_file(&sdev->sdev_driverfs_dev, sdev_attrs[i]);
	device_unregister(&sdev->sdev_driverfs_dev);
}
