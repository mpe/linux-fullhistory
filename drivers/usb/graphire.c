/*
 *  graphire.c  Version 0.2
 *
 *  Copyright (c) 2000 Vojtech Pavlik		<vojtech@suse.cz>
 *  Copyright (c) 2000 Andreas Bach Aaen	<abach@stofanet.dk>
 *
 *  USB Wacom Graphire tablet support
 *
 *  Sponsored by SuSE
 *
 *  ChangeLog:
 *      v0.1 (vp)  - Initial release
 *      v0.2 (aba) - Support for all buttons / combinations
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

/*
 * Thanks for the following information to: Andreas Bach Aaen <abach@mail1.stofanet.dk>
 *
 * The input report:
 * 
 * byte 0: report ID (2)
 * byte 1: bit7 	mouse/pen/rubber near
 *         bit5-6	0 - pen, 1 - rubber, 2 - mouse
 * 	   bit4		1 ?
 *         bit3		0 ?
 *         bit2		mouse middle button / pen button2
 *         bit1		mouse right button / pen button1
 *         bit0		mouse left button / pen tip / rubber
 * byte 2: X low bits
 * byte 3: X high bits
 * byte 4: Y low bits
 * byte 5: Y high bits
 * byte 6: pen pressure low bits / mouse wheel
 * byte 7: pen presure high bits / mouse distance
 * 
 * There are also two single-byte feature reports (2 and 3).
 *
 * Resolution:
 * X: 0 - 10206
 * Y: 0 - 7422
 * 
 * (0,0) is upper left corner
 */

#define USB_VENDOR_ID_WACOM		0x056a
#define USB_DEVICE_ID_WACOM_GRAPHIRE	0x0010

struct graphire {
	signed char data[8];
	struct input_dev dev;
	struct urb irq;
};

static void graphire_irq(struct urb *urb)
{
	struct graphire *graphire = urb->context;
	unsigned char *data = graphire->data;
	struct input_dev *dev = &graphire->dev;

	if (urb->status) return;

	if (data[0] != 2)
		dbg("received unknown report #%d", data[0]);

        if ( data[1] & 0x80 ) {
	        input_report_abs(dev, ABS_X, data[2] | ((__u32)data[3] << 8));
	        input_report_abs(dev, ABS_Y, 7422 - (data[4] | ((__u32)data[5] << 8)));
	}

	switch ((data[1] >> 5) & 3) {

		case 0:	/* Pen */
			input_report_key(dev, BTN_TOOL_PEN, !!(data[1] & 0x80));
			input_report_key(dev, BTN_TOUCH, !!(data[1] & 0x01));
			input_report_key(dev, BTN_STYLUS, !!(data[1] & 0x02));
			input_report_key(dev, BTN_STYLUS2, !!(data[1] & 0x04));
			input_report_abs(dev, ABS_PRESSURE, data[6] | ((__u32)data[7] << 8));
			break;

		case 1: /* Rubber */
	                input_report_key(dev, BTN_TOOL_RUBBER, !!(data[1] & 0x80));
			input_report_key(dev, BTN_TOUCH, !!(data[1] & 0x01));
			input_report_key(dev, BTN_STYLUS, !!(data[1] & 0x02));
			input_report_key(dev, BTN_STYLUS2, !!(data[1] & 0x04));
			input_report_abs(dev, ABS_PRESSURE, data[6] | ((__u32)data[7] << 8));
			break;

		case 2: /* Mouse */
	                input_report_key(dev, BTN_TOOL_MOUSE, data[7] > 24);
			input_report_key(dev, BTN_LEFT, !!(data[1] & 0x01));
			input_report_key(dev, BTN_RIGHT, !!(data[1] & 0x02));
			input_report_key(dev, BTN_MIDDLE, !!(data[1] & 0x04));
			input_report_abs(dev, ABS_DISTANCE, data[7]);
			input_report_rel(dev, REL_WHEEL, (signed char) data[6]);
			break;
	}
}

static void *graphire_probe(struct usb_device *dev, unsigned int ifnum)
{
	struct usb_endpoint_descriptor *endpoint;
	struct graphire *graphire;

	if (dev->descriptor.idVendor != USB_VENDOR_ID_WACOM ||
	    dev->descriptor.idProduct != USB_DEVICE_ID_WACOM_GRAPHIRE)
		return NULL;

	endpoint = dev->config[0].interface[ifnum].altsetting[0].endpoint + 0;

	if (!(graphire = kmalloc(sizeof(struct graphire), GFP_KERNEL))) return NULL;
	memset(graphire, 0, sizeof(struct graphire));

	graphire->dev.evbit[0] |= BIT(EV_KEY) | BIT(EV_REL) | BIT(EV_ABS);
	graphire->dev.keybit[LONG(BTN_MOUSE)] |= BIT(BTN_LEFT) | BIT(BTN_RIGHT) | BIT(BTN_MIDDLE);
	graphire->dev.keybit[LONG(BTN_DIGI)] |= BIT(BTN_TOOL_PEN) | BIT(BTN_TOOL_RUBBER) | BIT(BTN_TOOL_MOUSE);
	graphire->dev.keybit[LONG(BTN_DIGI)] |= BIT(BTN_TOUCH) | BIT(BTN_STYLUS) | BIT(BTN_STYLUS2);
	graphire->dev.relbit[0] |= BIT(REL_WHEEL);
	graphire->dev.absbit[0] |= BIT(ABS_X) | BIT(ABS_Y) | BIT(ABS_PRESSURE) | BIT(ABS_DISTANCE);

	graphire->dev.absmax[ABS_X] = 10206;
	graphire->dev.absmax[ABS_Y] = 7422;
	graphire->dev.absmax[ABS_PRESSURE] = 511;
	graphire->dev.absmax[ABS_DISTANCE] = 32;

	FILL_INT_URB(&graphire->irq, dev, usb_rcvintpipe(dev, endpoint->bEndpointAddress),
			graphire->data, 8, graphire_irq, graphire, endpoint->bInterval);

	if (usb_submit_urb(&graphire->irq)) {
		kfree(graphire);
		return NULL;
	}

	input_register_device(&graphire->dev);

	printk(KERN_INFO "input%d: Wacom Graphire\n", graphire->dev.number);

	return graphire;
}

static void graphire_disconnect(struct usb_device *dev, void *ptr)
{
	struct graphire *graphire = ptr;
	usb_unlink_urb(&graphire->irq);
	input_unregister_device(&graphire->dev);
	kfree(graphire);
}

static struct usb_driver graphire_driver = {
	name:		"graphire",
	probe:		graphire_probe,
	disconnect:	graphire_disconnect,
};

#ifdef MODULE
void cleanup_module(void)
{
	usb_deregister(&graphire_driver);
}

int init_module(void)
#else
int graphire_init(void)
#endif
{
	usb_register(&graphire_driver);
	return 0;
}
