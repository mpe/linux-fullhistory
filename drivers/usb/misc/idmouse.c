/* Siemens ID Mouse driver v0.5

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of
  the License, or (at your option) any later version.

  Copyright (C) 2004-5 by Florian 'Floe' Echtler  <echtler@fs.tum.de>
                      and Andreas  'ad'  Deresch <aderesch@fs.tum.de>

  Derived from the USB Skeleton driver 1.1,
  Copyright (C) 2003 Greg Kroah-Hartman (greg@kroah.com)

*/

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/smp_lock.h>
#include <linux/completion.h>
#include <asm/uaccess.h>
#include <linux/usb.h>

#define WIDTH 225
#define HEIGHT 288
#define HEADER "P5 225 288 255 "
#define IMGSIZE ((WIDTH * HEIGHT) + sizeof(HEADER)-1)

/* Version Information */
#define DRIVER_VERSION "0.5"
#define DRIVER_SHORT   "idmouse"
#define DRIVER_AUTHOR  "Florian 'Floe' Echtler <echtler@fs.tum.de>"
#define DRIVER_DESC    "Siemens ID Mouse FingerTIP Sensor Driver"

/* Siemens ID Mouse */
#define USB_IDMOUSE_VENDOR_ID  0x0681
#define USB_IDMOUSE_PRODUCT_ID 0x0005

/* we still need a minor number */
#define USB_IDMOUSE_MINOR_BASE 132

static struct usb_device_id idmouse_table[] = {
	{USB_DEVICE(USB_IDMOUSE_VENDOR_ID, USB_IDMOUSE_PRODUCT_ID)},
	{} /* null entry at the end */
};

MODULE_DEVICE_TABLE(usb, idmouse_table);

/* structure to hold all of our device specific stuff */
struct usb_idmouse {

	struct usb_device *udev; /* save off the usb device pointer */
	struct usb_interface *interface; /* the interface for this device */

	unsigned char *bulk_in_buffer; /* the buffer to receive data */
	size_t bulk_in_size; /* the size of the receive buffer */
	__u8 bulk_in_endpointAddr; /* the address of the bulk in endpoint */

	int open; /* if the port is open or not */
	int present; /* if the device is not disconnected */
	struct semaphore sem; /* locks this structure */

};

/* local function prototypes */
static ssize_t idmouse_read(struct file *file, char __user *buffer,
				size_t count, loff_t * ppos);

static int idmouse_open(struct inode *inode, struct file *file);
static int idmouse_release(struct inode *inode, struct file *file);

static int idmouse_probe(struct usb_interface *interface,
				const struct usb_device_id *id);

static void idmouse_disconnect(struct usb_interface *interface);

/* file operation pointers */
static struct file_operations idmouse_fops = {
	.owner = THIS_MODULE,
	.read = idmouse_read,
	.open = idmouse_open,
	.release = idmouse_release,
};

/* class driver information for devfs */
static struct usb_class_driver idmouse_class = {
	.name = "usb/idmouse%d",
	.fops = &idmouse_fops,
	.mode = S_IFCHR | S_IRUSR | S_IRGRP | S_IROTH, /* filemode (char, 444) */
	.minor_base = USB_IDMOUSE_MINOR_BASE,
};

/* usb specific object needed to register this driver with the usb subsystem */
static struct usb_driver idmouse_driver = {
	.owner = THIS_MODULE,
	.name = DRIVER_SHORT,
	.probe = idmouse_probe,
	.disconnect = idmouse_disconnect,
	.id_table = idmouse_table,
};

// prevent races between open() and disconnect()
static DECLARE_MUTEX(disconnect_sem);

static int idmouse_create_image(struct usb_idmouse *dev)
{
	int bytes_read = 0;
	int bulk_read = 0;
	int result = 0;

	if (dev->bulk_in_size < sizeof(HEADER))
		return -ENOMEM;

	memcpy(dev->bulk_in_buffer,HEADER,sizeof(HEADER)-1);
	bytes_read += sizeof(HEADER)-1;

	/* Dump the setup packets. Yes, they are uncommented, simply 
	   because they were sniffed under Windows using SnoopyPro.
	   I _guess_ that 0x22 is a kind of reset command and 0x21 
	   means init..
	*/
	result = usb_control_msg (dev->udev, usb_sndctrlpipe (dev->udev, 0),
				0x21, 0x42, 0x0001, 0x0002, NULL, 0, HZ);
	if (result < 0)
		return result;
	result = usb_control_msg (dev->udev, usb_sndctrlpipe (dev->udev, 0),
				0x20, 0x42, 0x0001, 0x0002, NULL, 0, HZ);
	if (result < 0)
		return result;
	result = usb_control_msg (dev->udev, usb_sndctrlpipe (dev->udev, 0),
				0x22, 0x42, 0x0000, 0x0002, NULL, 0, HZ);
	if (result < 0)
		return result;

	result = usb_control_msg (dev->udev, usb_sndctrlpipe (dev->udev, 0),
				0x21, 0x42, 0x0001, 0x0002, NULL, 0, HZ);
	if (result < 0)
		return result;
	result = usb_control_msg (dev->udev, usb_sndctrlpipe (dev->udev, 0),
				0x20, 0x42, 0x0001, 0x0002, NULL, 0, HZ);
	if (result < 0)
		return result;
	result = usb_control_msg (dev->udev, usb_sndctrlpipe (dev->udev, 0),
				0x20, 0x42, 0x0000, 0x0002, NULL, 0, HZ);
	if (result < 0)
		return result;

	/* loop over a blocking bulk read to get data from the device */
	while (bytes_read < IMGSIZE) {
		result = usb_bulk_msg (dev->udev,
				usb_rcvbulkpipe (dev->udev, dev->bulk_in_endpointAddr),
				dev->bulk_in_buffer + bytes_read,
				dev->bulk_in_size, &bulk_read, HZ * 5);
		if (result < 0)
			return result;
		if (signal_pending(current))
			return -EINTR;
		bytes_read += bulk_read;
	}

	/* reset the device */
	result = usb_control_msg (dev->udev, usb_sndctrlpipe (dev->udev, 0),
				0x22, 0x42, 0x0000, 0x0002, NULL, 0, HZ);
	if (result < 0)
		return result;

	/* should be IMGSIZE == 64815 */
	dbg("read %d bytes fingerprint data", bytes_read);
	return 0;
}

static inline void idmouse_delete(struct usb_idmouse *dev)
{
	kfree(dev->bulk_in_buffer);
	kfree(dev);
}

static int idmouse_open(struct inode *inode, struct file *file)
{
	struct usb_idmouse *dev = NULL;
	struct usb_interface *interface;
	int result = 0;

	/* prevent disconnects */
	down(&disconnect_sem);

	/* get the interface from minor number and driver information */
	interface = usb_find_interface (&idmouse_driver, iminor (inode));
	if (!interface) {
		up(&disconnect_sem);
		return -ENODEV;
	}
	/* get the device information block from the interface */
	dev = usb_get_intfdata(interface);
	if (!dev) {
		up(&disconnect_sem);
		return -ENODEV;
	}

	/* lock this device */
	down(&dev->sem);

	/* check if already open */
	if (dev->open) {

		/* already open, so fail */
		result = -EBUSY;

	} else {

		/* create a new image and check for success */
		result = idmouse_create_image (dev);
		if (result)
			goto error;

		/* increment our usage count for the driver */
		++dev->open;

		/* save our object in the file's private structure */
		file->private_data = dev;

	} 

error:

	/* unlock this device */
	up(&dev->sem);

	/* unlock the disconnect semaphore */
	up(&disconnect_sem);
	return result;
}

static int idmouse_release(struct inode *inode, struct file *file)
{
	struct usb_idmouse *dev;

	/* prevent a race condition with open() */
	down(&disconnect_sem);

	dev = (struct usb_idmouse *) file->private_data;

	if (dev == NULL) {
		up(&disconnect_sem);
		return -ENODEV;
	}

	/* lock our device */
	down(&dev->sem);

	/* are we really open? */
	if (dev->open <= 0) {
		up(&dev->sem);
		up(&disconnect_sem);
		return -ENODEV;
	}

	--dev->open;

	if (!dev->present) {
		/* the device was unplugged before the file was released */
		up(&dev->sem);
		idmouse_delete(dev);
		up(&disconnect_sem);
		return 0;
	}

	up(&dev->sem);
	up(&disconnect_sem);
	return 0;
}

static ssize_t idmouse_read(struct file *file, char __user *buffer, size_t count,
				loff_t * ppos)
{
	struct usb_idmouse *dev;
	int result = 0;

	dev = (struct usb_idmouse *) file->private_data;

	// lock this object
	down (&dev->sem);

	// verify that the device wasn't unplugged
	if (!dev->present) {
		up (&dev->sem);
		return -ENODEV;
	}

	if (*ppos >= IMGSIZE) {
		up (&dev->sem);
		return 0;
	}

	if (count > IMGSIZE - *ppos)
		count = IMGSIZE - *ppos;

	if (copy_to_user (buffer, dev->bulk_in_buffer + *ppos, count)) {
		result = -EFAULT;
	} else {
		result = count;
		*ppos += count;
	}

	// unlock the device 
	up(&dev->sem);
	return result;
}

static int idmouse_probe(struct usb_interface *interface,
				const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(interface);
	struct usb_idmouse *dev = NULL;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	size_t buffer_size;
	int result;

	/* check if we have gotten the data or the hid interface */
	iface_desc = &interface->altsetting[0];
	if (iface_desc->desc.bInterfaceClass != 0x0A)
		return -ENODEV;

	/* allocate memory for our device state and initialize it */
	dev = kmalloc(sizeof(*dev), GFP_KERNEL);
	if (dev == NULL)
		return -ENOMEM;
	memset(dev, 0x00, sizeof(*dev));

	init_MUTEX(&dev->sem);
	dev->udev = udev;
	dev->interface = interface;

	/* set up the endpoint information - use only the first bulk-in endpoint */
	endpoint = &iface_desc->endpoint[0].desc;
	if (!dev->bulk_in_endpointAddr
		&& (endpoint->bEndpointAddress & USB_DIR_IN)
		&& ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) ==
		USB_ENDPOINT_XFER_BULK)) {

		/* we found a bulk in endpoint */
		buffer_size = le16_to_cpu(endpoint->wMaxPacketSize);
		dev->bulk_in_size = buffer_size;
		dev->bulk_in_endpointAddr = endpoint->bEndpointAddress;
		dev->bulk_in_buffer =
			kmalloc(IMGSIZE + buffer_size, GFP_KERNEL);

		if (!dev->bulk_in_buffer) {
			err("Unable to allocate input buffer.");
			idmouse_delete(dev);
			return -ENOMEM;
		}
	}

	if (!(dev->bulk_in_endpointAddr)) {
		err("Unable to find bulk-in endpoint.");
		idmouse_delete(dev);
		return -ENODEV;
	}
	/* allow device read, write and ioctl */
	dev->present = 1;

	/* we can register the device now, as it is ready */
	usb_set_intfdata(interface, dev);
	result = usb_register_dev(interface, &idmouse_class);
	if (result) {
		/* something prevented us from registering this device */
		err("Unble to allocate minor number.");
		usb_set_intfdata(interface, NULL);
		idmouse_delete(dev);
		return result;
	}

	/* be noisy */
	dev_info(&interface->dev,"%s now attached\n",DRIVER_DESC);

	return 0;
}

static void idmouse_disconnect(struct usb_interface *interface)
{
	struct usb_idmouse *dev;

	/* prevent races with open() */
	down(&disconnect_sem);

	/* get device structure */
	dev = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);

	/* lock it */
	down(&dev->sem);

	/* give back our minor */
	usb_deregister_dev(interface, &idmouse_class);

	/* prevent device read, write and ioctl */
	dev->present = 0;

	/* unlock */
	up(&dev->sem);

	/* if the device is opened, idmouse_release will clean this up */
	if (!dev->open)
		idmouse_delete(dev);

	up(&disconnect_sem);

	info("%s disconnected", DRIVER_DESC);
}

static int __init usb_idmouse_init(void)
{
	int result;

	info(DRIVER_DESC " " DRIVER_VERSION);

	/* register this driver with the USB subsystem */
	result = usb_register(&idmouse_driver);
	if (result)
		err("Unable to register device (error %d).", result);

	return result;
}

static void __exit usb_idmouse_exit(void)
{
	/* deregister this driver with the USB subsystem */
	usb_deregister(&idmouse_driver);
}

module_init(usb_idmouse_init);
module_exit(usb_idmouse_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

