/*
 *  usbmouse.c  Version 0.1
 *
 *  Copyright (c) 1999 Vojtech Pavlik
 *
 *  USB HIDBP Mouse support
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
#include "usb.h"

MODULE_AUTHOR("Vojtech Pavlik <vojtech@suse.cz>");

#define USBMOUSE_EXTRA

struct usb_mouse {
	signed char data[8];
	struct input_dev dev;
	struct urb irq;
};

static void usb_mouse_irq(struct urb *urb)
{
	struct usb_mouse *mouse = urb->context;
	signed char *data = mouse->data;
	struct input_dev *dev = &mouse->dev;

	if (urb->status) return;

	input_report_key(dev, BTN_LEFT, !!(data[0] & 0x01));
	input_report_key(dev, BTN_RIGHT, !!(data[0] & 0x02));
	input_report_key(dev, BTN_MIDDLE, !!(data[0] & 0x04));
	input_report_rel(dev, REL_X, data[1]);
	input_report_rel(dev, REL_Y, data[2]);
#ifdef USBMOUSE_EXTRA
	input_report_key(dev, BTN_SIDE, !!(data[0] & 0x08));
	input_report_key(dev, BTN_EXTRA, !!(data[0] & 0x10));
	input_report_rel(dev, REL_WHEEL, data[3]);
#endif
}

static void *usb_mouse_probe(struct usb_device *dev, unsigned int ifnum)
{
	struct usb_interface_descriptor *interface;
	struct usb_endpoint_descriptor *endpoint;
	struct usb_mouse *mouse;

	if (dev->descriptor.bNumConfigurations != 1) return NULL;
	interface = dev->config[0].interface[ifnum].altsetting + 0;

	if (interface->bInterfaceClass != 3) return NULL;
	if (interface->bInterfaceSubClass != 1) return NULL;
	if (interface->bInterfaceProtocol != 2) return NULL;
	if (interface->bNumEndpoints != 1) return NULL;

	endpoint = interface->endpoint + 0;
	if (!(endpoint->bEndpointAddress & 0x80)) return NULL;
	if ((endpoint->bmAttributes & 3) != 3) return NULL;

#ifndef USBMOUSE_EXTRA
	usb_set_protocol(dev, 0);
#endif

	if (!(mouse = kmalloc(sizeof(struct usb_mouse), GFP_KERNEL))) return NULL;
	memset(mouse, 0, sizeof(struct usb_mouse));

	mouse->dev.evbit[0] = BIT(EV_KEY) | BIT(EV_REL);
	mouse->dev.keybit[LONG(BTN_MOUSE)] = BIT(BTN_LEFT) | BIT(BTN_RIGHT) | BIT(BTN_MIDDLE);
	mouse->dev.relbit[0] = BIT(REL_X) | BIT(REL_Y);
#ifdef USBMOUSE_EXTRA
	mouse->dev.keybit[LONG(BTN_MOUSE)] |= BIT(BTN_SIDE) | BIT(BTN_EXTRA);
	mouse->dev.relbit[0] |= BIT(REL_WHEEL);
#endif

	{
		int pipe = usb_rcvintpipe(dev, endpoint->bEndpointAddress);
		int maxp = usb_maxpacket(dev, pipe, usb_pipeout(pipe));

		FILL_INT_URB(&mouse->irq, dev, pipe, mouse->data, maxp > 8 ? 8 : maxp,
			usb_mouse_irq, mouse, endpoint->bInterval);
	}

	if (usb_submit_urb(&mouse->irq)) {
		kfree(mouse);
		return NULL;
	}

	input_register_device(&mouse->dev);

	printk(KERN_INFO "input%d: USB HIDBP mouse\n", mouse->dev.number);

	return mouse;
}

static void usb_mouse_disconnect(struct usb_device *dev, void *ptr)
{
	struct usb_mouse *mouse = ptr;
	usb_unlink_urb(&mouse->irq);
	input_unregister_device(&mouse->dev);
	kfree(mouse);
}

static struct usb_driver usb_mouse_driver = {
	name:		"usb_mouse",
	probe:		usb_mouse_probe,
	disconnect:	usb_mouse_disconnect,
};

#ifdef MODULE
void cleanup_module(void)
{
	usb_deregister(&usb_mouse_driver);
}

int init_module(void)
#else
int usb_mouse_init(void)
#endif
{
	usb_register(&usb_mouse_driver);
	return 0;
}

