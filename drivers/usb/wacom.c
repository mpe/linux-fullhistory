/*
 *  wacom.c  Version 0.3
 *
 *  Copyright (c) 2000 Vojtech Pavlik		<vojtech@suse.cz>
 *  Copyright (c) 2000 Andreas Bach Aaen	<abach@stofanet.dk>
 *  Copyright (c) 2000 Clifford Wolf		<clifford@clifford.at>
 *
 *  USB Wacom Graphire and Wacom Intuos tablet support
 *
 *  Sponsored by SuSE
 *
 *  ChangeLog:
 *      v0.1 (vp)  - Initial release
 *      v0.2 (aba) - Support for all buttons / combinations
 *      v0.3 (vp)  - Support for Intuos added
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
#include "usb.h"

MODULE_AUTHOR("Vojtech Pavlik <vojtech@suse.cz>");

/*
 * Wacom Graphire packet:
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
 *
 * Wacom Intuos packet:
 *
 *  byte 0: report ID (2)
 *  byte 1: bit7	1 ?
 *          bit6	tilt (pressure?) data valid
 *          bit5	near
 *          bit4 	0 ?
 *          bit3	0 ?
 *          bit2	pen button
 *          bit1	first packet (contains other infos)
 *          bit0	0 ?
 *  byte 2: X high bits
 *  byte 3: X low bits
 *  byte 4: Y high bits
 *  byte 5: Y low bits
 *  byte 6: bits 0-7:  pressure (bits 2-9)
 *  byte 7: bits 6-7:  pressure (bits 0-1)
 *  byte 7: bits 0-5:  X tilt  (bits 1-6)
 *  byte 8: bit    7:  X tilt  (bit  0)
 *  byte 8: bits 0-6:  Y tilt  (bits 0-6)
 *  byte 9: ?
 */

#define USB_VENDOR_ID_WACOM		0x056a
#define USB_DEVICE_ID_WACOM_GRAPHIRE	0x0010
#define USB_DEVICE_ID_WACOM_INTUOS	0x0021

struct wacom {
	signed char data[12];
	struct input_dev dev;
	struct urb irq;
};

static void wacom_graphire_irq(struct urb *urb)
{
	struct wacom *wacom = urb->context;
	unsigned char *data = wacom->data;
	struct input_dev *dev = &wacom->dev;

	if (urb->status) return;

	if (data[0] != 2)
		dbg("received unknown report #%d", data[0]);

        if ( data[1] & 0x80 ) {
	        input_report_abs(dev, ABS_X, data[2] | ((__u32)data[3] << 8));
	        input_report_abs(dev, ABS_Y, 7422 - (data[4] | ((__u32)data[5] << 8)));
	}

	switch ((data[1] >> 5) & 3) {

		case 0:	/* Pen */
			input_report_btn(dev, BTN_TOOL_PEN, data[1] & 0x80);
			input_report_btn(dev, BTN_TOUCH, data[1] & 0x01);
			input_report_btn(dev, BTN_STYLUS, data[1] & 0x02);
			input_report_btn(dev, BTN_STYLUS2, data[1] & 0x04);
			input_report_abs(dev, ABS_PRESSURE, data[6] | ((__u32)data[7] << 8));
			break;

		case 1: /* Rubber */
	                input_report_btn(dev, BTN_TOOL_RUBBER, data[1] & 0x80);
			input_report_btn(dev, BTN_TOUCH, data[1] & 0x01);
			input_report_btn(dev, BTN_STYLUS, data[1] & 0x02);
			input_report_btn(dev, BTN_STYLUS2, data[1] & 0x04);
			input_report_abs(dev, ABS_PRESSURE, data[6] | ((__u32)data[7] << 8));
			break;

		case 2: /* Mouse */
	                input_report_btn(dev, BTN_TOOL_MOUSE, data[7] > 24);
			input_report_btn(dev, BTN_LEFT, data[1] & 0x01);
			input_report_btn(dev, BTN_RIGHT, data[1] & 0x02);
			input_report_btn(dev, BTN_MIDDLE, data[1] & 0x04);
			input_report_abs(dev, ABS_DISTANCE, data[7]);
			input_report_rel(dev, REL_WHEEL, (signed char) data[6]);
			break;
	}
}

static void wacom_intuos_irq(struct urb *urb)
{
	struct wacom *wacom = urb->context;
	unsigned char *data = wacom->data;
	struct input_dev *dev = &wacom->dev;
	unsigned int t;

	if (urb->status) return;

	if (data[0] != 2)
		dbg("received unknown report #%d", data[0]);

	if (data[1] & 0x02) /* First record, weird data */
		return;
	
	if (data[1] & 0x20) { /* Near */
		input_report_abs(dev, ABS_X, (((unsigned int) data[2]) << 8) | data[3]);
		input_report_abs(dev, ABS_Y, 16240 - ((((unsigned int) data[4]) << 8) | data[5]));
	}

	input_report_btn(dev, BTN_TOOL_PEN, data[1] & 0x20);
	input_report_btn(dev, BTN_STYLUS, data[1] & 0x04);

	t = (((unsigned int) data[6]) << 2) | ((data[7] & 0xC0) >> 6);

	input_report_btn(dev, BTN_TOUCH, t > 10);
	input_report_abs(dev, ABS_PRESSURE, t);

	if (data[1] & 0x40) { /* Tilt data */
		input_report_abs(dev, ABS_TILT_X, ((((unsigned int) data[7]) & 0x3f) << 1) | ((data[8] & 0x80) >> 7));
		input_report_abs(dev, ABS_TILT_Y, data[8] & 0x7f);
	}
}

static void *wacom_probe(struct usb_device *dev, unsigned int ifnum)
{
	struct usb_endpoint_descriptor *endpoint;
	struct wacom *wacom;
	char *name;

	if (dev->descriptor.idVendor != USB_VENDOR_ID_WACOM ||
	   (dev->descriptor.idProduct != USB_DEVICE_ID_WACOM_GRAPHIRE &&
	    dev->descriptor.idProduct != USB_DEVICE_ID_WACOM_INTUOS))
		return NULL;

	endpoint = dev->config[0].interface[ifnum].altsetting[0].endpoint + 0;

	if (!(wacom = kmalloc(sizeof(struct wacom), GFP_KERNEL))) return NULL;
	memset(wacom, 0, sizeof(struct wacom));

	switch (dev->descriptor.idProduct) {

		case USB_DEVICE_ID_WACOM_GRAPHIRE:

			wacom->dev.evbit[0] |= BIT(EV_KEY) | BIT(EV_REL) | BIT(EV_ABS);
			wacom->dev.keybit[LONG(BTN_MOUSE)] |= BIT(BTN_LEFT) | BIT(BTN_RIGHT) | BIT(BTN_MIDDLE);
			wacom->dev.keybit[LONG(BTN_DIGI)] |= BIT(BTN_TOOL_PEN) | BIT(BTN_TOOL_RUBBER) | BIT(BTN_TOOL_MOUSE);
			wacom->dev.keybit[LONG(BTN_DIGI)] |= BIT(BTN_TOUCH) | BIT(BTN_STYLUS) | BIT(BTN_STYLUS2);
			wacom->dev.relbit[0] |= BIT(REL_WHEEL);
			wacom->dev.absbit[0] |= BIT(ABS_X) | BIT(ABS_Y) | BIT(ABS_PRESSURE) | BIT(ABS_DISTANCE);

			wacom->dev.absmax[ABS_X] = 10206;
			wacom->dev.absmax[ABS_Y] = 7422;
			wacom->dev.absmax[ABS_PRESSURE] = 511;
			wacom->dev.absmax[ABS_DISTANCE] = 32;

			FILL_INT_URB(&wacom->irq, dev, usb_rcvintpipe(dev, endpoint->bEndpointAddress),
					wacom->data, 8, wacom_graphire_irq, wacom, endpoint->bInterval);

			name = "Graphire";
			break;

		case USB_DEVICE_ID_WACOM_INTUOS:

			wacom->dev.evbit[0] |= BIT(EV_KEY) | BIT(EV_ABS);
			wacom->dev.keybit[LONG(BTN_DIGI)] |= BIT(BTN_TOOL_PEN) | BIT(BTN_TOUCH) | BIT(BTN_STYLUS);
			wacom->dev.absbit[0] |= BIT(ABS_X) | BIT(ABS_Y) | BIT(ABS_PRESSURE);
			wacom->dev.absbit[0] |= BIT(ABS_TILT_X) | BIT(ABS_TILT_Y);

			wacom->dev.absmax[ABS_X] = 20320;
			wacom->dev.absmax[ABS_Y] = 16240;
			wacom->dev.absmax[ABS_PRESSURE] = 1024;
			wacom->dev.absmax[ABS_TILT_X] = 127;
			wacom->dev.absmax[ABS_TILT_Y] = 127;

			FILL_INT_URB(&wacom->irq, dev, usb_rcvintpipe(dev, endpoint->bEndpointAddress),
					wacom->data, 8, wacom_intuos_irq, wacom, endpoint->bInterval);

			name = "Intuos";
			break;

		default:
			return NULL;
	}

	if (usb_submit_urb(&wacom->irq)) {
		kfree(wacom);
		return NULL;
	}

	input_register_device(&wacom->dev);

	printk(KERN_INFO "input%d: Wacom %s\n", wacom->dev.number, name);

	return wacom;
}

static void wacom_disconnect(struct usb_device *dev, void *ptr)
{
	struct wacom *wacom = ptr;
	usb_unlink_urb(&wacom->irq);
	input_unregister_device(&wacom->dev);
	kfree(wacom);
}

static struct usb_driver wacom_driver = {
	name:		"wacom",
	probe:		wacom_probe,
	disconnect:	wacom_disconnect,
};

static int __init wacom_init(void)
{
	usb_register(&wacom_driver);
	return 0;
}

static void __exit wacom_exit(void)
{
	usb_deregister(&wacom_driver);
}

module_init(wacom_init);
module_exit(wacom_exit);
