/*
 * $Id: wmforce.c,v 1.6 2000/05/29 09:01:52 vojtech Exp $
 *
 *  Copyright (c) 2000 Vojtech Pavlik
 *
 *  USB Logitech WingMan Force joystick support
 *
 *  Sponsored by SuSE
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or 
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 * 
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@suse.cz>, or by paper mail:
 * Vojtech Pavlik, Ucitelska 1576, Prague 8, 182 00 Czech Republic
 */

#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@suse.cz>");

#define USB_VENDOR_ID_LOGITECH		0x046d
#define USB_DEVICE_ID_LOGITECH_WMFORCE	0xc281

struct wmforce {
	signed char data[8];
	struct input_dev dev;
	struct urb irq;
	int open;
};

static struct {
        __s32 x;
        __s32 y;
} wmforce_hat_to_axis[16] = {{ 0,-1}, { 1,-1}, { 1, 0}, { 1, 1}, { 0, 1}, {-1, 1}, {-1, 0}, {-1,-1}};

static char *wmforce_name = "Logitech WingMan Force";

static void wmforce_irq(struct urb *urb)
{
	struct wmforce *wmforce = urb->context;
	unsigned char *data = wmforce->data;
	struct input_dev *dev = &wmforce->dev;

	if (urb->status) return;

	if (data[0] != 1) {
		if (data[0] != 2)
			warn("received unknown report #%d", data[0]);
		return;
	}

	input_report_abs(dev, ABS_X, (__s16) (((__s16)data[2] << 8) | data[1]));
	input_report_abs(dev, ABS_Y, (__s16) (((__s16)data[4] << 8) | data[3]));
	input_report_abs(dev, ABS_THROTTLE, data[5]);
	input_report_abs(dev, ABS_HAT0X, wmforce_hat_to_axis[data[7] >> 4].x);
	input_report_abs(dev, ABS_HAT0Y, wmforce_hat_to_axis[data[7] >> 4].y);

	input_report_key(dev, BTN_TRIGGER, data[6] & 0x01);
	input_report_key(dev, BTN_TOP,     data[6] & 0x02);
	input_report_key(dev, BTN_THUMB,   data[6] & 0x04);
	input_report_key(dev, BTN_TOP2,    data[6] & 0x08);
	input_report_key(dev, BTN_BASE,    data[6] & 0x10);
	input_report_key(dev, BTN_BASE2,   data[6] & 0x20);
	input_report_key(dev, BTN_BASE3,   data[6] & 0x40);
	input_report_key(dev, BTN_BASE4,   data[6] & 0x80);
	input_report_key(dev, BTN_BASE5,   data[7] & 0x01);
}

static int wmforce_open(struct input_dev *dev)
{
	struct wmforce *wmforce = dev->private;

	if (wmforce->open++)
		return 0;

	if (usb_submit_urb(&wmforce->irq))
		return -EIO;

	return 0;
}

static void wmforce_close(struct input_dev *dev)
{
	struct wmforce *wmforce = dev->private;

	if (!--wmforce->open)
		usb_unlink_urb(&wmforce->irq);
}

static void *wmforce_probe(struct usb_device *dev, unsigned int ifnum)
{
	struct usb_endpoint_descriptor *endpoint;
	struct wmforce *wmforce;
	int i;

	if (dev->descriptor.idVendor != USB_VENDOR_ID_LOGITECH ||
	    dev->descriptor.idProduct != USB_DEVICE_ID_LOGITECH_WMFORCE)
		return NULL;

	endpoint = dev->config[0].interface[ifnum].altsetting[0].endpoint + 0;

	if (!(wmforce = kmalloc(sizeof(struct wmforce), GFP_KERNEL))) return NULL;
	memset(wmforce, 0, sizeof(struct wmforce));

	wmforce->dev.evbit[0] = BIT(EV_KEY) | BIT(EV_ABS);
	wmforce->dev.keybit[LONG(BTN_JOYSTICK)] = BIT(BTN_TRIGGER) | BIT(BTN_TOP) | BIT(BTN_THUMB) | BIT(BTN_TOP2) |
					BIT(BTN_BASE) | BIT(BTN_BASE2) | BIT(BTN_BASE3) | BIT(BTN_BASE4) | BIT(BTN_BASE5);
	wmforce->dev.absbit[0] = BIT(ABS_X) | BIT(ABS_Y) | BIT(ABS_THROTTLE) | BIT(ABS_HAT0X) | BIT(ABS_HAT0Y);

	for (i = ABS_X; i <= ABS_Y; i++) {
		wmforce->dev.absmax[i] =  1920;
		wmforce->dev.absmin[i] = -1920;
		wmforce->dev.absflat[i] = 128;
	}

	wmforce->dev.absmax[ABS_THROTTLE] = 255;
	wmforce->dev.absmin[ABS_THROTTLE] = 0;

	for (i = ABS_HAT0X; i <= ABS_HAT0Y; i++) {
		wmforce->dev.absmax[i] =  1;
		wmforce->dev.absmin[i] = -1;
	}

	wmforce->dev.private = wmforce;
	wmforce->dev.open = wmforce_open;
	wmforce->dev.close = wmforce_close;

	wmforce->dev.name = wmforce_name;
	wmforce->dev.idbus = BUS_USB;
	wmforce->dev.idvendor = dev->descriptor.idVendor;
	wmforce->dev.idproduct = dev->descriptor.idProduct;
	wmforce->dev.idversion = dev->descriptor.bcdDevice;

	FILL_INT_URB(&wmforce->irq, dev, usb_rcvintpipe(dev, endpoint->bEndpointAddress),
			wmforce->data, 8, wmforce_irq, wmforce, endpoint->bInterval);

	input_register_device(&wmforce->dev);

	printk(KERN_INFO "input%d: %s on usb%d:%d.%d\n",
		 wmforce->dev.number, wmforce_name, dev->bus->busnum, dev->devnum, ifnum);

	return wmforce;
}

static void wmforce_disconnect(struct usb_device *dev, void *ptr)
{
	struct wmforce *wmforce = ptr;
	usb_unlink_urb(&wmforce->irq);
	input_unregister_device(&wmforce->dev);
	kfree(wmforce);
}

static struct usb_driver wmforce_driver = {
	name:		"wmforce",
	probe:		wmforce_probe,
	disconnect:	wmforce_disconnect,
};

static int __init wmforce_init(void)
{
	usb_register(&wmforce_driver);
	return 0;
}

static void __exit wmforce_exit(void)
{
	usb_deregister(&wmforce_driver);
}

module_init(wmforce_init);
module_exit(wmforce_exit);
