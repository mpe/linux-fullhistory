/*****************************************************************************/

/*
 *	ezusb.c  --  Firmware download miscdevice for Anchorchips EZUSB microcontrollers.
 *
 *	Copyright (C) 1999
 *          Thomas Sailer (sailer@ife.ee.ethz.ch)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  History:
 *   0.1  26.05.99  Created
 *
 */

/*****************************************************************************/

#include <linux/config.h>
#include <linux/module.h>
#include <linux/socket.h>
#include <asm/uaccess.h>
#include <linux/miscdevice.h>

#include "usb.h"
#include "ezusb.h"

/* --------------------------------------------------------------------- */

#define NREZUSB 1

static struct ezusb {
	struct semaphore mutex;
	struct usb_device *usbdev;
	unsigned int irqep;
	unsigned int intlen;
	unsigned char intdata[64];
} ezusb[NREZUSB];

/* --------------------------------------------------------------------- */

static int ezusb_irq(int state, void *__buffer, int len, void *dev_id)
{
	struct ezusb *ez = (struct ezusb *)dev_id;
	
	if (len > sizeof(ez->intdata))
		len = sizeof(ez->intdata);
	ez->intlen = len;
	memcpy(ez->intdata, __buffer, len);
	return 1;
}

/* --------------------------------------------------------------------- */

static loff_t ezusb_llseek(struct file *file, loff_t offset, int origin)
{
	struct ezusb *ez = (struct ezusb *)file->private_data;

	switch(origin) {
	case 1:
		offset += file->f_pos;
		break;
	case 2:
		offset += 0x10000;
		break;
	}
	if (offset < 0 || offset >= 0x10000)
		return -EINVAL;
	return (file->f_pos = offset);
}

static ssize_t ezusb_read(struct file *file, char *buf, size_t sz, loff_t *ppos)
{
	struct ezusb *ez = (struct ezusb *)file->private_data;
	unsigned pos = *ppos;
	unsigned ret = 0;
	unsigned len;
	unsigned char b[64];
        devrequest dr;
	int i;

	if (*ppos < 0 || *ppos >= 0x10000)
		return -EINVAL;
	down(&ez->mutex);
	if (!ez->usbdev) {
		up(&ez->mutex);
		return -EIO;
	}
	while (sz > 0 && pos < 0x10000) {
		len = sz;
		if (len > sizeof(b))
			len = sizeof(b);
		if (pos + len > 0x10000)
			len = 0x10000 - pos;
		dr.requesttype = 0xc0;
		dr.request = 0xa0;
		dr.value = pos;
		dr.index = 0;
		dr.length = len;
		i = ez->usbdev->bus->op->control_msg(ez->usbdev, usb_rcvctrlpipe(ez->usbdev, 0), &dr, b, len);
		if (i) {
			up(&ez->mutex);
			printk(KERN_WARNING "ezusb: upload failed pos %u len %u ret %d\n", dr.value, dr.length, i);
			*ppos = pos;
			if (ret)
				return ret;
			return -ENXIO;
		}
		if (copy_to_user(buf, b, len)) {
			up(&ez->mutex);
			*ppos = pos;
			if (ret)
				return ret;
			return -EFAULT;
		}
		pos += len;
		buf += len;
		sz -= len;
		ret += len;
	}
	up(&ez->mutex);
	*ppos = pos;
	return ret;
}

static ssize_t ezusb_write(struct file *file, const char *buf, size_t sz, loff_t *ppos)
{
	struct ezusb *ez = (struct ezusb *)file->private_data;
	unsigned pos = *ppos;
	unsigned ret = 0;
	unsigned len;
	unsigned char b[64];
        devrequest dr;
	int i;

	if (*ppos < 0 || *ppos >= 0x10000)
		return -EINVAL;
	down(&ez->mutex);
	if (!ez->usbdev) {
		up(&ez->mutex);
		return -EIO;
	}
	while (sz > 0 && pos < 0x10000) {
		len = sz;
		if (len > sizeof(b))
			len = sizeof(b);
		if (pos + len > 0x10000)
			len = 0x10000 - pos;
		if (copy_from_user(b, buf, len)) {
			up(&ez->mutex);
			*ppos = pos;
			if (ret)
				return ret;
			return -EFAULT;
		}
		dr.requesttype = 0x40;
		dr.request = 0xa0;
		dr.value = pos;
		dr.index = 0;
		dr.length = len;
		i = ez->usbdev->bus->op->control_msg(ez->usbdev, usb_sndctrlpipe(ez->usbdev, 0), &dr, b, len);
		if (i) {
			up(&ez->mutex);
			printk(KERN_WARNING "ezusb: download failed pos %u len %u ret %d\n", dr.value, dr.length, i);
			*ppos = pos;
			if (ret)
				return ret;
			return -ENXIO;
		}
		pos += len;
		buf += len;
		sz -= len;
		ret += len;
	}
	up(&ez->mutex);
	*ppos = pos;
	return ret;
}

static int ezusb_open(struct inode *inode, struct file *file)
{
	struct ezusb *ez = &ezusb[0];

	down(&ez->mutex);
	while (!ez->usbdev) {
		up(&ez->mutex);
		if (!(file->f_flags & O_NONBLOCK)) {
			return -EIO;
		}
		schedule_timeout(HZ/2);
		if (signal_pending(current))
			return -EAGAIN;
		down(&ez->mutex);
	}
	up(&ez->mutex);
	file->f_pos = 0;
	file->private_data = ez;
	return 0;
}

static int ezusb_release(struct inode *inode, struct file *file)
{
	struct ezusb *ez = (struct ezusb *)file->private_data;
	return 0;
}

static int ezusb_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct ezusb *ez = (struct ezusb *)file->private_data;
	struct ezusb_ctrltransfer ctrl;
	struct ezusb_bulktransfer bulk;
	unsigned int len1, ep;
	unsigned long len2;
	unsigned int irqep;
	unsigned char tbuf[1024];
	int i;

	switch (cmd) {
	case EZUSB_CONTROL:
		copy_from_user_ret(&ctrl, (void *)arg, sizeof(ctrl), -EFAULT);
		if (ctrl.dlen > sizeof(tbuf) || ctrl.dlen > 1024)
			return -EINVAL;
		if (ctrl.requesttype & 0x80) {
			if (ctrl.dlen && !access_ok(VERIFY_WRITE, ctrl.data, ctrl.dlen))
				return -EINVAL;
			down(&ez->mutex);
			if (!ez->usbdev) {
				up(&ez->mutex);
				return -EIO;
			}
			i = ez->usbdev->bus->op->control_msg(ez->usbdev, usb_rcvctrlpipe(ez->usbdev, 0),
							     (devrequest *)&ctrl, tbuf, ctrl.dlen);
			up(&ez->mutex);
			if (!i && ctrl.dlen) {
				copy_to_user_ret(ctrl.data, tbuf, ctrl.dlen, -EFAULT);
			}
		} else {
			if (ctrl.dlen) {
				copy_from_user_ret(tbuf, ctrl.data, ctrl.dlen, -EFAULT);
			}
			down(&ez->mutex);
			if (!ez->usbdev) {
				up(&ez->mutex);
				return -EIO;
			}
			i = ez->usbdev->bus->op->control_msg(ez->usbdev, usb_sndctrlpipe(ez->usbdev, 0),
							     (devrequest *)&ctrl, tbuf, ctrl.dlen);
			up(&ez->mutex);
		}
		if (i) {
			printk(KERN_WARNING "ezusb: EZUSB_CONTROL failed rqt %u rq %u len %u ret %d\n", 
			       ctrl.requesttype, ctrl.request, ctrl.length, i);
			return -ENXIO;
		}
		return 0;

	case EZUSB_INTERRUPT:
		get_user_ret(irqep, (unsigned int *)arg, -EFAULT);
		if (irqep != ez->irqep) {
			if (ez->irqep)
				return -EIO;
			ez->irqep = irqep;
			usb_request_irq(ez->usbdev, usb_rcvctrlpipe(ez->usbdev, ez->irqep),
					ezusb_irq, 2 /* interval */, ez);
			ez->intlen = 0;
			return -EAGAIN;
		}
		copy_to_user_ret((&((struct ezusb_interrupttransfer *)0)->data) + arg, 
				 ez->intdata, 64, -EFAULT);
		return ez->intlen;

	case EZUSB_BULK:
		copy_from_user_ret(&bulk, (void *)arg, sizeof(bulk), -EFAULT);
		len1 = bulk.len;
		if (len1 > sizeof(tbuf))
			len1 = sizeof(tbuf);
		if (bulk.ep & 0x80) {
			if (len1 && !access_ok(VERIFY_WRITE, bulk.data, len1))
				return -EINVAL;
			down(&ez->mutex);
			if (!ez->usbdev) {
				up(&ez->mutex);
				return -EIO;
			}
			i = ez->usbdev->bus->op->bulk_msg(ez->usbdev, usb_rcvbulkpipe(ez->usbdev, bulk.ep & 0x7f),
							  tbuf, len1, &len2);
			up(&ez->mutex);
			if (!i && len2) {
				copy_to_user_ret(bulk.data, tbuf, len2, -EFAULT);
			}
		} else {
			if (len1) {
				copy_from_user_ret(tbuf, bulk.data, len1, -EFAULT);
			}
			down(&ez->mutex);
			if (!ez->usbdev) {
				up(&ez->mutex);
				return -EIO;
			}
			i = ez->usbdev->bus->op->bulk_msg(ez->usbdev, usb_sndbulkpipe(ez->usbdev, bulk.ep & 0x7f),
							  tbuf, len1, &len2);
			up(&ez->mutex);
		}
		if (i) {
			printk(KERN_WARNING "ezusb: EZUSB_BULK failed ep 0x%x len %u ret %d\n", 
			       bulk.ep, bulk.len, i);
			return -ENXIO;
		}
		return len2;

	case EZUSB_RESETEP:
		get_user_ret(ep, (unsigned int *)arg, -EFAULT);
		if ((ep & ~0x80) >= 16)
			return -EINVAL;
		usb_settoggle(ez->usbdev, ep & 0xf, !(ep & 0x80), 0);
		return 0;
	}
	return -ENOIOCTLCMD;
}

static struct file_operations ezusb_fops = {
	ezusb_llseek,
	ezusb_read,
	ezusb_write,
	NULL,  /* readdir */
	NULL,  /* poll */
	ezusb_ioctl,
	NULL,  /* mmap */
	ezusb_open,
	NULL,  /* flush */
	ezusb_release,
	NULL,  /* fsync */
	NULL,  /* fasync */
	NULL,  /* check_media_change */
	NULL,  /* revalidate */
	NULL   /* lock */
};

static struct miscdevice ezusb_misc = {
	192, "ezusb", &ezusb_fops
};

/* --------------------------------------------------------------------- */

static int ezusb_probe(struct usb_device *usbdev)
{
	struct ezusb *ez = &ezusb[0];
        struct usb_interface_descriptor *interface;
        struct usb_endpoint_descriptor *endpoint;

#undef KERN_DEBUG
#define KERN_DEBUG ""
        printk(KERN_DEBUG "ezusb: probe: vendor id 0x%x, device id 0x%x\n",
               usbdev->descriptor.idVendor, usbdev->descriptor.idProduct);

	/* the 1234:5678 is just a self assigned test ID */
        if ((usbdev->descriptor.idVendor != 0x0547 || usbdev->descriptor.idProduct != 0x2131) &&
	    (usbdev->descriptor.idVendor != 0x1234 || usbdev->descriptor.idProduct != 0x5678))
                return -1;

        /* We don't handle multiple configurations */
        if (usbdev->descriptor.bNumConfigurations != 1)
                return -1;

        /* We don't handle multiple interfaces */
        if (usbdev->config[0].bNumInterfaces != 1)
                return -1;

	down(&ez->mutex);
	if (ez->usbdev) {
		up(&ez->mutex);
		printk(KERN_INFO "ezusb: device already used\n");
		return -1;
	}
	ez->usbdev = usbdev;
	usbdev->private = ez;
        if (usb_set_configuration(usbdev, usbdev->config[0].bConfigurationValue)) {
		printk(KERN_ERR "ezusb: set_configuration failed\n");
		goto err;
	}
        interface = &usbdev->config[0].altsetting[1].interface[0];
	if (usb_set_interface(usbdev, 0, 1)) {
		printk(KERN_ERR "ezusb: set_interface failed\n");
		goto err;
	}
	up(&ez->mutex);
	MOD_INC_USE_COUNT;
        return 0;

 err:
	up(&ez->mutex);
	ez->usbdev = NULL;
	usbdev->private = NULL;
	return -1;
}

static void ezusb_disconnect(struct usb_device *usbdev)
{
	struct ezusb *ez = (struct ezusb *)usbdev->private;

	down(&ez->mutex);
	ez->usbdev = NULL;
	up(&ez->mutex);
        usbdev->private = NULL;
	MOD_DEC_USE_COUNT;
}

static struct usb_driver ezusb_driver = {
        "ezusb",
        ezusb_probe,
        ezusb_disconnect,
        { NULL, NULL }
};

/* --------------------------------------------------------------------- */

int ezusb_init(void)
{
	unsigned u;

	/* initialize struct */
	for (u = 0; u < NREZUSB; u++) {
		init_MUTEX(&ezusb[u].mutex);
		ezusb[u].usbdev = NULL;
		ezusb[u].irqep = 0;
	}
	/* register misc device */
	if (misc_register(&ezusb_misc)) {
		printk(KERN_WARNING "ezusb: cannot register minor %d\n", ezusb_misc.minor);
		return -1;
	}
	usb_register(&ezusb_driver);
        printk(KERN_INFO "ezusb: Anchorchip firmware download driver registered\n");
	return 0;
}

void ezusb_cleanup(void)
{
	usb_deregister(&ezusb_driver);
	misc_deregister(&ezusb_misc);
}

/* --------------------------------------------------------------------- */

#ifdef MODULE

int minor = 192;

int init_module(void)
{
	ezusb_misc.minor = minor;
	return ezusb_init();
}

void cleanup_module(void)
{
	ezusb_cleanup();
}

#endif

/* --------------------------------------------------------------------- */
