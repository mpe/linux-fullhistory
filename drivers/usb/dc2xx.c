/*
 * Copyright (C) 1999 by David Brownell <david-b@pacbell.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
 
 
/*
 * USB driver for Kodak DC-2XX series digital still cameras
 *
 * The protocol here is the same as the one going over a serial line, but
 * it uses USB for speed.  Set up /dev/kodak, get gphoto (www.gphoto.org),
 * and have fun!
 *
 * This should also work for a number of other digital (non-Kodak) cameras,
 * by adding the vendor and product IDs to the table below.
 */

/*
 * HISTORY
 *
 * 26 August, 1999 -- first release (0.1), works with my DC-240.
 * 	The DC-280 (2Mpixel) should also work, but isn't tested.
 *	If you use gphoto, make sure you have the USB updates.
 *	Lives in a 2.3.14 or so Linux kernel, in drivers/usb.
 * 31 August, 1999 -- minor update to recognize DC-260 and handle
 *	its endpoints being in a different order.  Note that as
 *	of gPhoto 0.36pre, the USB updates are integrated.
 * 12 Oct, 1999 -- handle DC-280 interface class (0xff not 0x0);
 *	added timeouts to bulk_msg calls.  Minor updates, docs.
 * 03 Nov, 1999 -- update for 2.3.25 kernel API changes.
 *
 * Thanks to:  the folk who've provided USB product IDs, sent in
 * patches, and shared their sucesses!
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/random.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/malloc.h>
#include <linux/module.h>

#include "usb.h"


// #define	CAMERA_DEBUG


/* XXX need to get registered minor number, cdev 10/MINOR */
/* XXX or: cdev USB_MAJOR(180)/USB_CAMERA_MINOR */
#define	USB_CAMERA_MINOR	170


/* Application protocol limit is 0x8002; USB has disliked that limit! */
#define	MAX_PACKET_SIZE		0x2000		/* e.g. image downloading */

#define	MAX_READ_RETRY		5		/* times to retry reads */
#define	MAX_WRITE_RETRY		5		/* times to retry writes */
#define	RETRY_TIMEOUT		(HZ)		/* sleep between retries */


/* table of cameras that work through this driver */
static const struct camera {
	short		idVendor;
	short		idProduct;
	/* plus hooks for camera-specific info if needed */
} cameras [] = {
    { 0x040a, 0x0120 },		// Kodak DC-240
    { 0x040a, 0x0130 },		// Kodak DC-280

	/* Kodak has several other USB-enabled devices, which (along with
	 * models from other vendors) all use the Flashpoint "Digita
	 * OS" and its wire protocol.  These use a different application
	 * level protocol from the DC-240/280 models.  Note that Digita
	 * isn't just for cameras -- Epson has a non-USB Digita printer.
	 */
//  { 0x040a, 0x0100 },		// Kodak DC-220
    { 0x040a, 0x0110 },		// Kodak DC-260
    { 0x040a, 0x0111 },		// Kodak DC-265
    { 0x040a, 0x0112 },		// Kodak DC-290

//  { 0x03f0, 0xffff },		// HP PhotoSmart C500

	/* Other USB cameras may well work here too, so long as they
	 * just stick to half duplex packet exchanges and bulk messages.
	 * Some non-camera devices have also been shown to work.
	 */
};


struct camera_state {
	/* these fields valid (dev != 0) iff camera connected */
	struct usb_device	*dev;		/* USB device handle */
	char			inEP;		/* read endpoint */
	char			outEP;		/* write endpoint */
	const struct camera	*info;		/* DC-240, etc */

	/* valid iff isOpen */
	int			isOpen;		/* device opened? */
	int			isActive;	/* I/O taking place? */
	char			*buf;		/* buffer for I/O */

	/* always valid */
	wait_queue_head_t	wait;		/* for timed waits */
};


/* For now, we only support one camera at a time: there's one
 * application-visible device (e.g. /dev/kodak) and the second
 * (to Nth) camera detected on the bus is ignored.
 */
static struct camera_state static_camera_state;


static ssize_t camera_read (struct file *file,
	char *buf, size_t len, loff_t *ppos)
{
	struct camera_state	*camera;
	int			retries;

	camera = (struct camera_state *) file->private_data;
	if (len > MAX_PACKET_SIZE)
		return -EINVAL;
	if (camera->isActive++)
		return -EBUSY;

	/* Big reads are common, for image downloading.  Smaller ones
	 * are also common (even "directory listing" commands don't
	 * send very much data).  We preserve packet boundaries here,
	 * they matter in the application protocol.
	 */
	for (retries = 0; retries < MAX_READ_RETRY; retries++) {
		unsigned long		count;
		int			result;

		if (signal_pending (current)) {
			camera->isActive = 0;
			return -EINTR;
		}
		if (!camera->dev) {
			camera->isActive = 0;
			return -ENODEV;
		}

		result = usb_bulk_msg (camera->dev,
			  usb_rcvbulkpipe (camera->dev, camera->inEP),
			  camera->buf, len, &count, HZ*10);

#ifdef	CAMERA_DEBUG
		printk ("camera.r (%d) - 0x%x %ld\n", len, result, count);
#endif
		if (!result) {
			if (copy_to_user (buf, camera->buf, count))
				return -EFAULT;
			camera->isActive = 0;
			return count;
		}
		if (result != USB_ST_TIMEOUT)
			break;
		interruptible_sleep_on_timeout (&camera->wait, RETRY_TIMEOUT);
#ifdef	CAMERA_DEBUG
		printk ("camera.r (%d) - retry\n", len);
#endif
	}
	camera->isActive = 0;
	return -EIO;
}

static ssize_t camera_write (struct file *file,
	const char *buf, size_t len, loff_t *ppos)
{
	struct camera_state	*camera;
	ssize_t			bytes_written = 0;

	camera = (struct camera_state *) file->private_data;
	if (len > MAX_PACKET_SIZE)
		return -EINVAL;
	if (camera->isActive++)
		return -EBUSY;
	
	/* most writes will be small: simple commands, sometimes with
	 * parameters.  putting images (like borders) into the camera
	 * would be the main use of big writes.
	 */
	while (len > 0) {
		char		*obuf = camera->buf;
		int		maxretry = MAX_WRITE_RETRY;
		unsigned long	copy_size, thistime;

		/* it's not clear that retrying can do any good ... or that
		 * fragmenting application packets into N writes is correct.
		 */
		thistime = copy_size = len;
		if (copy_from_user (obuf, buf, copy_size)) {
			bytes_written = -EFAULT;
			break;
		}
		while (thistime) {
			int		result;
			unsigned long	count;

			if (signal_pending (current)) {
				if (!bytes_written)
					bytes_written = -EINTR;
				goto done;
			}
			if (!camera->dev) {
				if (!bytes_written)
					bytes_written = -ENODEV;
				goto done;
			}

			result = usb_bulk_msg (camera->dev,
				 usb_sndbulkpipe (camera->dev, camera->outEP),
				 obuf, thistime, &count, HZ*10);
#ifdef	CAMERA_DEBUG
			if (result)
				printk ("camera.w USB err - %x\n", result);
#endif
			if (count) {
				obuf += count;
				thistime -= count;
				maxretry = MAX_WRITE_RETRY;
				continue;
			} else if (!result)
				break;
				
			if (result == USB_ST_TIMEOUT) {	/* NAK - delay a bit */
				if (!maxretry--) {
					if (!bytes_written)
						bytes_written = -ETIME;
					goto done;
				}
                                interruptible_sleep_on_timeout (&camera->wait,
					RETRY_TIMEOUT);
				continue;
			} 
			if (!bytes_written)
				bytes_written = -EIO;
			goto done;
		}
		bytes_written += copy_size;
		len -= copy_size;
		buf += copy_size;
	}
done:
	camera->isActive = 0;
#ifdef	CAMERA_DEBUG
	printk ("camera.w %d\n", bytes_written); 
#endif
	return bytes_written;
}

static int camera_open (struct inode *inode, struct file *file)
{
	struct camera_state *camera = &static_camera_state;

	/* ignore camera->dev so it can be turned on "late" */

	if (camera->isOpen++)
		return -EBUSY;
	if (!(camera->buf = (char *) kmalloc (MAX_PACKET_SIZE, GFP_KERNEL))) {
		camera->isOpen = 0;
		return -ENOMEM;
	}
#ifdef	CAMERA_DEBUG
	printk ("camera.open\n"); 
#endif
	
	/* Keep driver from being unloaded while it's in use */
	MOD_INC_USE_COUNT;

	camera->isActive = 0;
	file->private_data = camera;
	return 0;
}

static int camera_release (struct inode *inode, struct file *file)
{
	struct camera_state *camera;

	camera = (struct camera_state *) file->private_data;
	kfree (camera->buf);
	camera->isOpen = 0;
	MOD_DEC_USE_COUNT;
#ifdef	CAMERA_DEBUG
	printk ("camera.close\n"); 
#endif

	return 0;
}

	/* XXX should define some ioctls to expose camera type
	 * to applications ... what USB exposes should suffice.
	 * apps should be able to see the camera type.
	 */
static /* const */ struct file_operations usb_camera_fops = {
	NULL,		/* llseek */
	camera_read,
	camera_write,
	NULL, 		/* readdir */
	NULL,		/* poll */
	NULL,		/* ioctl */
	NULL,		/* mmap */
	camera_open,
	NULL,		/* flush */
	camera_release,
	NULL,		/* async */
	NULL,		/* fasync */
	NULL,		/* check_media_change */
	NULL,		/* revalidate */
	NULL,		/* lock */
};

static struct miscdevice usb_camera = {
	USB_CAMERA_MINOR,
	"USB camera (Kodak DC-2xx)",
	&usb_camera_fops
	// next, prev
};



static void * camera_probe(struct usb_device *dev, unsigned int ifnum)
{
	int				i;
	const struct camera		*camera_info = NULL;
	struct usb_interface_descriptor	*interface;
	struct usb_endpoint_descriptor	*endpoint;
	int				direction, ep;

	struct camera_state		*camera = &static_camera_state;

	/* Is it a supported camera? */
	for (i = 0; i < sizeof (cameras) / sizeof (struct camera); i++) {
		if (cameras [i].idVendor != dev->descriptor.idVendor)
			continue;
		if (cameras [i].idProduct != dev->descriptor.idProduct)
			continue;
		camera_info = &cameras [i];
		break;
	}
	if (camera_info == NULL)
		return NULL;

	/* these have one config, one interface */
	if (dev->descriptor.bNumConfigurations != 1
			|| dev->config[0].bNumInterfaces != 1) {
		printk (KERN_INFO "Bogus camera config info\n");
		return NULL;
	}

	/* models differ in how they report themselves */
	interface = &dev->actconfig->interface[ifnum].altsetting[0];
	if ((interface->bInterfaceClass != USB_CLASS_PER_INTERFACE
		&& interface->bInterfaceClass != USB_CLASS_VENDOR_SPEC)
			|| interface->bInterfaceSubClass != 0
			|| interface->bInterfaceProtocol != 0
			|| interface->bNumEndpoints != 2
			) {
		printk (KERN_INFO "Bogus camera interface info\n");
		return NULL;
	}

	/* can only show one camera at a time through /dev ... */
	if (!camera->dev) {
		camera->dev = dev;
		printk(KERN_INFO "USB Camera is connected\n");
	} else {
		printk(KERN_INFO "Ignoring additional USB Camera\n");
		return NULL;
	}

	/* get input and output endpoints (either order) */
	endpoint = interface->endpoint;
	camera->outEP = camera->inEP =  -1;

	ep = endpoint [0].bEndpointAddress & USB_ENDPOINT_NUMBER_MASK;
	direction = endpoint [0].bEndpointAddress & USB_ENDPOINT_DIR_MASK;
	if (direction == USB_DIR_IN)
		camera->inEP = ep;
	else
		camera->outEP = ep;

	ep = endpoint [1].bEndpointAddress & USB_ENDPOINT_NUMBER_MASK;
	direction = endpoint [1].bEndpointAddress & USB_ENDPOINT_DIR_MASK;
	if (direction == USB_DIR_IN)
		camera->inEP = ep;
	else
		camera->outEP = ep;

	if (camera->outEP == -1 || camera->inEP == -1
			|| endpoint [0].bmAttributes != USB_ENDPOINT_XFER_BULK
			|| endpoint [1].bmAttributes != USB_ENDPOINT_XFER_BULK
			) {
		printk (KERN_INFO "Bogus camera endpoints\n");
		camera->dev = NULL;
		return NULL;
	}


	if (usb_set_configuration (dev, dev->config[0].bConfigurationValue)) {
		printk (KERN_INFO "Failed usb_set_configuration: camera\n");
		camera->dev = NULL;
		return NULL;
	}

	camera->info = camera_info;
	return camera;
}

static void camera_disconnect(struct usb_device *dev, void *ptr)
{
	struct camera_state	*camera = (struct camera_state *) ptr;

	if (camera->dev != dev)
		return;

	/* Currently not reflecting this up to userland; at one point
	 * it got called on bus reconfig, which we clearly don't want.
	 * A good consequence is the ability to remove camera for
	 * a while without apps needing to do much more than ignore
	 * some particular error returns.  On the bad side, if one
	 * camera is swapped for another one, we won't be telling.
	 */
	camera->info = NULL;
	camera->dev = NULL;

	printk (KERN_INFO "USB Camera disconnected\n");
}

static /* const */ struct usb_driver camera_driver = {
	"dc2xx",
	camera_probe,
	camera_disconnect,
	{ NULL, NULL },

	NULL,	/* &usb_camera_fops, */
	0	/* USB_CAMERA_MINOR */
};


#ifdef MODULE
static __init
#endif
int usb_dc2xx_init(void)
{
	struct camera_state *camera = &static_camera_state;

	camera->dev = NULL;
	camera->isOpen = 0;
	camera->isActive = 0;
	init_waitqueue_head (&camera->wait);

	if (usb_register (&camera_driver) < 0)
		return -1;
	misc_register (&usb_camera);

	return 0;
}

#ifdef MODULE
static __exit
#endif
void usb_dc2xx_cleanup(void)
{
	usb_deregister (&camera_driver);
	misc_deregister (&usb_camera);
}


#ifdef MODULE

MODULE_AUTHOR("David Brownell, david-b@pacbell.net");
MODULE_DESCRIPTION("USB Camera Driver for Kodak DC-2xx series cameras");

module_init (usb_dc2xx_init);
module_exit (usb_dc2xx_cleanup);

#endif	/* MODULE */
