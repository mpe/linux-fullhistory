/*
 *  wacom.c  Version 0.4
 *
 *  Copyright (c) 2000 Vojtech Pavlik		<vojtech@suse.cz>
 *  Copyright (c) 2000 Andreas Bach Aaen	<abach@stofanet.dk>
 *  Copyright (c) 2000 Clifford Wolf		<clifford@clifford.at>
 *  Copyright (c) 2000 Sam Mosel		<sam.mosel@computer.org>
 *
 *  USB Wacom Graphire and Wacom Intuos tablet support
 *
 *  Sponsored by SuSE
 *
 *  ChangeLog:
 *      v0.1 (vp)  - Initial release
 *      v0.2 (aba) - Support for all buttons / combinations
 *      v0.3 (vp)  - Support for Intuos added
 *      v0.4 (sm)  - Support for more Intuos models, menustrip,
 *                   relative mode, proximity.
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
 * Wacom Intuos Status packet:
 *
 *  byte 0: report ID (2)
 *  byte 1: bit7	1 (Sync Byte)
 *          bit6	Pointer Near
 *          bit5	0 - first proximity report
 *          bit4 	0 ?
 *          bit3	0 ?
 *          bit2	pen button2
 *          bit1	pen button1
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
 *  byte 9: bits 4-7:  Proximity
 */

#define USB_VENDOR_ID_WACOM		0x056a
#define USB_DEVICE_ID_WACOM_GRAPHIRE	0x0010
#define USB_DEVICE_ID_WACOM_INTUOS45	0x0020 /* Guess */
#define USB_DEVICE_ID_WACOM_INTUOS68	0x0021
#define USB_DEVICE_ID_WACOM_INTUOS912	0x0022 /* Guess */
#define USB_DEVICE_ID_WACOM_INTUOS1212	0x0023
#define USB_DEVICE_ID_WACOM_INTUOS1218	0x0024 /* Guess */

#define USB_TOOL_ID_WACOM_PEN	        0x0022
#define USB_TOOL_ID_WACOM_ERASER        0x00fa
#define USB_TOOL_ID_WACOM_STROKE_PEN    0x0032
#define USB_TOOL_ID_WACOM_INKING_PEN    0x0012
#define USB_TOOL_ID_WACOM_AIRBRUSH      0x0112
#define USB_TOOL_ID_WACOM_MOUSE4D       0x0094
#define USB_TOOL_ID_WACOM_LENS_CURSOR   0x0096

#define INTUOS_PEN_MODE_ABS             0x00
#define INTUOS_PEN_MODE_REL             0x01
#define INTUOS_PEN_MODE_QUICKPOINT      0x02

#define INTUOS_PRESSURE_MODE_SOFT       0x00
#define INTUOS_PRESSURE_MODE_MED        0x01
#define INTUOS_PRESSURE_MODE_FIRM       0x02

#define INTUOS_MENUSTRIP_Y_ZONE         1400
#define INTUOS_MENUSTRIP_BTN_YMIN        270
#define INTUOS_MENUSTRIP_BTN_YMAX       1070
#define INTUOS_MENUSTRIP_F1_XMIN          40
#define INTUOS_MENUSTRIP_F7_XMIN        8340
#define INTUOS_MENUSTRIP_F12_XMIN      15300

#define INTUOS_MENUSTRIP_BTN_WIDTH      1300

#define INTUOS_MENUSTRIP_F7_IX_OFFSET      6 /* offset into wacom_fkeys */
#define INTUOS_MENUSTRIP_F12_IX_OFFSET    11

struct wacom {
        signed char data[10];
	struct input_dev dev;
	struct urb irq;

        int last_x, last_y;
	unsigned int tool, device;
	unsigned int ymax, menustrip_touch;
	unsigned int pen_mode;
	unsigned int pressure_mode;
};

static int wacom_fkeys[16] = { KEY_F1,  KEY_F2,  KEY_F3,  KEY_F4,
			       KEY_F5,  KEY_F6,  KEY_F7,  KEY_F8,
			       KEY_F9,  KEY_F10, KEY_F11, KEY_F12,
			       KEY_F13, KEY_F14, KEY_F15, KEY_F16};

#define INTUOS_EXTENTS_MAX_X              0x00
#define INTUOS_EXTENTS_MAX_Y              0x01
#define INTUOS_EXTENTS_HAS_F7             0x02
#define INTUOS_EXTENTS_HAS_F12            0x03
#define INTUOS_EXTENTS_PEN_MODE           0x04
#define INTUOS_EXTENTS_HAS_QUICKPOINT     0x05
#define INTUOS_EXTENTS_HAS_PRESSURE_MODE  0x06
#define INTUOS_EXTENTS_PRESSURE_MODE      0x07

#define WACOM_TRUE   1
#define WACOM_FALSE  0

static int intuos_extents[5][8] = {
	{ 12700, 10360, WACOM_FALSE, WACOM_FALSE,  8340, WACOM_FALSE, WACOM_FALSE,     0},  /* Intuos 4x5   */
	{ 20320, 15040,  WACOM_TRUE, WACOM_FALSE, 15300, WACOM_FALSE,  WACOM_TRUE, 18360},  /* Intuos 6x8   */
	{ 30480, 23060,  WACOM_TRUE,  WACOM_TRUE, 22280,  WACOM_TRUE,  WACOM_TRUE, 26640},  /* Intuos 9x12  */
	{ 30480, 30480,  WACOM_TRUE,  WACOM_TRUE, 22280,  WACOM_TRUE,  WACOM_TRUE, 26640},  /* Intuos 12x12 */
	{ 47720, 30480,  WACOM_TRUE,  WACOM_TRUE, 29260,  WACOM_TRUE,  WACOM_TRUE, 33620}}; /* Intuos 12x18 */

static char intuos_names[5][12] = {
	{"Intuos 4x5  "}, {"Intuos 6x8  "},
	{"Intuos 9x12 "}, {"Intuos 12x12"},
	{"Intuos 12x18"}};

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

static void intuos_menustrip( unsigned int x, unsigned int y, struct wacom *wacom )
{
	struct input_dev *dev = &wacom->dev;
	unsigned int local_x = x;
	unsigned int local_y = y - ( wacom->ymax - INTUOS_MENUSTRIP_Y_ZONE );
	unsigned int fkey_index ;

	/* Ensure we are in the vertical strip for the buttons */
	if ( (local_y > INTUOS_MENUSTRIP_BTN_YMIN) && (local_y < INTUOS_MENUSTRIP_BTN_YMAX) ) {

		/* Handle Pressure Mode */
		if ( intuos_extents[wacom->device][INTUOS_EXTENTS_HAS_PRESSURE_MODE] ) {
			int pressure_mode = ( (local_x - intuos_extents[wacom->device][INTUOS_EXTENTS_PRESSURE_MODE])
					      / INTUOS_MENUSTRIP_BTN_WIDTH );
			if ( ( pressure_mode >= INTUOS_PRESSURE_MODE_SOFT ) &&
			     ( pressure_mode <= INTUOS_PRESSURE_MODE_FIRM ) ) {
				wacom->pressure_mode = pressure_mode;
				return;
			}
		}

		/* Handle Pen Mode */
		{
			int pen_mode = ( (local_x - intuos_extents[wacom->device][INTUOS_EXTENTS_PEN_MODE])
					 / INTUOS_MENUSTRIP_BTN_WIDTH );
			if ( ( pen_mode == INTUOS_PEN_MODE_ABS ) ||
			     ( pen_mode == INTUOS_PEN_MODE_REL ) ||
			     ( ( pen_mode == INTUOS_PEN_MODE_QUICKPOINT ) &&
			       ( intuos_extents[wacom->device][INTUOS_EXTENTS_HAS_QUICKPOINT] ) ) ) {
				wacom->pen_mode = pen_mode;
				return;
			}
		}

		/* Handle Function Keys */
		if ( local_x > INTUOS_MENUSTRIP_F12_XMIN ) {
			fkey_index = INTUOS_MENUSTRIP_F12_IX_OFFSET
				+ ( (local_x - INTUOS_MENUSTRIP_F12_XMIN) / INTUOS_MENUSTRIP_BTN_WIDTH );
			fkey_index = ( fkey_index > 16 ) ? 16 : fkey_index; /* Ensure in range */
		}
		else if ( local_x > INTUOS_MENUSTRIP_F7_XMIN ) {
			fkey_index = INTUOS_MENUSTRIP_F7_IX_OFFSET
				+ ( (local_x - INTUOS_MENUSTRIP_F7_XMIN) / INTUOS_MENUSTRIP_BTN_WIDTH );
			fkey_index = ( fkey_index > 11 ) ? 11 : fkey_index;
		}
		else {
			fkey_index = ( (local_x - INTUOS_MENUSTRIP_F1_XMIN) / INTUOS_MENUSTRIP_BTN_WIDTH );
			fkey_index = ( fkey_index > 6 ) ? 6 : fkey_index;
		}
		input_report_key(dev, wacom_fkeys[fkey_index], 1);
		input_report_key(dev, wacom_fkeys[fkey_index], 0);

		return;
	}
}
		
static void wacom_intuos_irq(struct urb *urb)
{
        struct wacom *wacom = urb->context;
	unsigned char *data = wacom->data;
	struct input_dev *dev = &wacom->dev;
	unsigned int t;
	int x, y;

	if (urb->status) return;

	if (data[0] != 2)
		dbg("received unknown report #%d", data[0]);

	if ( ((data[1] >> 5) & 0x3) == 0x2 ) /* First record, feature report */
		{
			wacom->tool = (((char)data[2] << 4) | (char)data[3] >> 4) & 0xff ;
			/* Report tool type */
			switch ( wacom->tool )	{
			case USB_TOOL_ID_WACOM_PEN: 
				input_report_btn(dev, BTN_TOOL_PEN, 1);
				break;
			case USB_TOOL_ID_WACOM_ERASER: 
				input_report_btn(dev, BTN_TOOL_RUBBER, 1);
				break;
			case USB_TOOL_ID_WACOM_STROKE_PEN: 
				input_report_btn(dev, BTN_TOOL_BRUSH, 1);
				break;
			case USB_TOOL_ID_WACOM_INKING_PEN: 
				input_report_btn(dev, BTN_TOOL_PENCIL, 1);
				break;
			case USB_TOOL_ID_WACOM_AIRBRUSH: 
				input_report_btn(dev, BTN_TOOL_AIRBRUSH, 1);
				break;
			case USB_TOOL_ID_WACOM_MOUSE4D: 
			case USB_TOOL_ID_WACOM_LENS_CURSOR: 
				input_report_btn(dev, BTN_TOOL_MOUSE, 1);
				break;
			default:
				break;
			}			
			return;
		}

	if ( ( data[1] | data[2] | data[3] | data[4] | data[5] | data[6]
	       | data[7] | data[8] | data[9] ) == 0x80 ) { /* exit report */
		switch ( wacom->tool ) {
		case USB_TOOL_ID_WACOM_PEN: 
			input_report_btn(dev, BTN_TOOL_PEN, 0);
			break;
		case USB_TOOL_ID_WACOM_ERASER: 
			input_report_btn(dev, BTN_TOOL_RUBBER, 0);
			break;
		case USB_TOOL_ID_WACOM_STROKE_PEN: 
			input_report_btn(dev, BTN_TOOL_BRUSH, 0);
			break;
		case USB_TOOL_ID_WACOM_INKING_PEN: 
			input_report_btn(dev, BTN_TOOL_PENCIL, 0);
			break;
		case USB_TOOL_ID_WACOM_AIRBRUSH: 
			input_report_btn(dev, BTN_TOOL_AIRBRUSH, 0);
			break;
		case USB_TOOL_ID_WACOM_MOUSE4D: 
		case USB_TOOL_ID_WACOM_LENS_CURSOR: 
			input_report_btn(dev, BTN_TOOL_MOUSE, 0);
			break;
		default:
			break;
		}			
		return;
	}

	x = (((unsigned int) data[2]) << 8) | data[3] ;
	y = wacom->dev.absmax[ABS_Y] - ((((unsigned int) data[4]) << 8) | data[5]);

	t = (((unsigned int) data[6]) << 2) | ((data[7] & 0xC0) >> 6);

	/* Handle touch near menustrip */
	if ( y > ( wacom->dev.absmax[ABS_Y] - INTUOS_MENUSTRIP_Y_ZONE ) ) {
		if ( t > 10 ) /* Touch */
			wacom->menustrip_touch = 1;
		if ( (wacom->menustrip_touch) && (t <= 10) ) { /* Pen Up */
			intuos_menustrip( x, y, wacom );
			wacom->menustrip_touch = 0;
		}
		return;
	}
	else
		wacom->menustrip_touch = 0;

	switch ( wacom->pen_mode ) {
	case INTUOS_PEN_MODE_ABS:
	        input_report_abs(dev, ABS_X, x);
		input_report_abs(dev, ABS_Y, y);
		break;
	case INTUOS_PEN_MODE_REL:
	        input_report_rel(dev, REL_X, ( x - wacom->last_x) / 8 );
	        input_report_rel(dev, REL_Y, - ( y - wacom->last_y) / 8 );
		break;
	default: break;
	}

	wacom->last_x = x;
	wacom->last_y = y;

	input_report_btn(dev, BTN_STYLUS, data[1] & 0x02);
	input_report_btn(dev, BTN_STYLUS2, data[1] & 0x04);

	input_report_btn(dev, BTN_TOUCH, t > 10);
	input_report_abs(dev, ABS_PRESSURE, t);

	input_report_abs(dev, ABS_DISTANCE, ( data[9] & 0xf0 ) >> 4 );

	input_report_abs(dev, ABS_TILT_X, ((((unsigned int) data[7]) & 0x3f) << 1) | ((data[8] & 0x80) >> 7));
	input_report_abs(dev, ABS_TILT_Y, data[8] & 0x7f);

}

static void *wacom_probe(struct usb_device *dev, unsigned int ifnum)
{
	struct usb_endpoint_descriptor *endpoint;
	struct wacom *wacom;
	char *name;

	if (dev->descriptor.idVendor != USB_VENDOR_ID_WACOM ||
	    (dev->descriptor.idProduct != USB_DEVICE_ID_WACOM_GRAPHIRE &&
	     dev->descriptor.idProduct != USB_DEVICE_ID_WACOM_INTUOS45 &&
	     dev->descriptor.idProduct != USB_DEVICE_ID_WACOM_INTUOS68 &&
	     dev->descriptor.idProduct != USB_DEVICE_ID_WACOM_INTUOS912 &&
	     dev->descriptor.idProduct != USB_DEVICE_ID_WACOM_INTUOS1212 &&
	     dev->descriptor.idProduct != USB_DEVICE_ID_WACOM_INTUOS1218))
		return NULL;

	endpoint = dev->config[0].interface[ifnum].altsetting[0].endpoint + 0;

	if (!(wacom = kmalloc(sizeof(struct wacom), GFP_KERNEL))) return NULL;
	memset(wacom, 0, sizeof(struct wacom));

	if ( dev->descriptor.idProduct == USB_DEVICE_ID_WACOM_GRAPHIRE ) {
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
	}
	else { /* Intuos */

		wacom->dev.evbit[0] |= BIT(EV_KEY) | BIT(EV_ABS) | BIT(EV_REL);
		wacom->dev.keybit[LONG(KEY_F1)]  |= BIT(KEY_F1)  | BIT(KEY_F2)  | BIT(KEY_F3)  | BIT(KEY_F4) | BIT(KEY_F5);
		wacom->dev.keybit[LONG(KEY_F6)]  |= BIT(KEY_F6)  | BIT(KEY_F7)  | BIT(KEY_F8);
		wacom->dev.keybit[LONG(KEY_F9)]  |= BIT(KEY_F9)  | BIT(KEY_F10) | BIT(KEY_F11) | BIT(KEY_F12);
		wacom->dev.keybit[LONG(KEY_F13)] |= BIT(KEY_F13) | BIT(KEY_F14) | BIT(KEY_F15) | BIT(KEY_F16);
		wacom->dev.keybit[LONG(BTN_DIGI)] |= BIT(BTN_TOUCH) | BIT(BTN_STYLUS) | BIT(BTN_STYLUS2);
		wacom->dev.keybit[LONG(BTN_DIGI)] |= BIT(BTN_TOOL_PEN) | BIT(BTN_TOOL_RUBBER) | BIT(BTN_TOOL_BRUSH);
		wacom->dev.keybit[LONG(BTN_DIGI)] |= BIT(BTN_TOOL_PENCIL) | BIT(BTN_TOOL_AIRBRUSH) | BIT(BTN_TOOL_MOUSE);
		wacom->dev.absbit[0] |= BIT(ABS_X) | BIT(ABS_Y) | BIT(ABS_PRESSURE) | BIT(ABS_DISTANCE);
		wacom->dev.absbit[0] |= BIT(ABS_TILT_X) | BIT(ABS_TILT_Y);
		wacom->dev.relbit[0] |= BIT(REL_X) | BIT(REL_Y);

		wacom->dev.absmax[ABS_PRESSURE] = 1023;
		wacom->dev.absmax[ABS_DISTANCE] = 15;
		wacom->dev.absmax[ABS_TILT_X] = 127;
		wacom->dev.absmax[ABS_TILT_Y] = 127;

		wacom->device = dev->descriptor.idProduct - USB_DEVICE_ID_WACOM_INTUOS45;

		wacom->dev.absmax[ABS_X] = intuos_extents[wacom->device][INTUOS_EXTENTS_MAX_X];
		wacom->dev.absmax[ABS_Y] = intuos_extents[wacom->device][INTUOS_EXTENTS_MAX_Y];

		wacom->ymax = intuos_extents[wacom->device][INTUOS_EXTENTS_MAX_Y];
		wacom->pen_mode = INTUOS_PEN_MODE_ABS;
		wacom->pressure_mode = INTUOS_PRESSURE_MODE_SOFT;

		name = intuos_names[wacom->device];

		FILL_INT_URB(&wacom->irq, dev, usb_rcvintpipe(dev, endpoint->bEndpointAddress),
			     wacom->data, 10, wacom_intuos_irq, wacom, endpoint->bInterval);

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
