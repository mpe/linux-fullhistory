/*
 *  usbkbd.c  Version 0.1
 *
 *  Copyright (c) 1999 Vojtech Pavlik
 *
 *  USB HIDBP Keyboard support
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
#include <linux/module.h>
#include <linux/input.h>
#include "usb.h"

MODULE_AUTHOR("Vojtech Pavlik <vojtech@suse.cz>");

static unsigned char usb_kbd_keycode[256] = {
	  0,  0,  0,  0, 30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38,
	 50, 49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44,  2,  3,
	  4,  5,  6,  7,  8,  9, 10, 11, 28,  1, 14, 15, 57, 12, 13, 26,
	 27, 43,  0, 39, 40, 41, 51, 52, 53, 58, 59, 60, 61, 62, 63, 64,
	 65, 66, 67, 68, 87, 88, 99, 70,119,110,102,104,111,107,109,106,
	105,108,103, 69, 98, 55, 74, 78, 96, 79, 80, 81, 75, 76, 77, 71,
	 72, 73, 82, 83, 86,127,116,117, 85, 89, 90, 91, 92, 93, 94, 95,
	120,121,122,123,  0,138,  0,  0,128,129,131,137,133,135,136,113,
	115,114,  0,  0,  0,  0,  0,124,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	 29, 42, 56,125, 97, 54,100,126
};

struct usb_kbd {
	struct input_dev dev;
	unsigned char new[8];
	unsigned char old[8];
	struct urb irq, led;
	devrequest dr;
	unsigned char leds;
};

static void usb_kbd_irq(struct urb *urb)
{
	struct usb_kbd *kbd = urb->context;
	int i;

	if (urb->status) return;

	for (i = 0; i < 8; i++)
		input_report_key(&kbd->dev, usb_kbd_keycode[i + 224], (kbd->new[0] >> i) & 1);

	for (i = 2; i < 8; i++) {

		if (kbd->old[i] > 3 && memscan(kbd->new + 2, kbd->old[i], 6) == kbd->new + 8) {
			if (usb_kbd_keycode[kbd->old[i]])
				input_report_key(&kbd->dev, usb_kbd_keycode[kbd->old[i]], 0);
			else
				info("Unknown key (scancode %#x) released.", kbd->old[i]);
		}

		if (kbd->new[i] > 3 && memscan(kbd->old + 2, kbd->new[i], 6) == kbd->old + 8) {
			if (usb_kbd_keycode[kbd->new[i]])
				input_report_key(&kbd->dev, usb_kbd_keycode[kbd->new[i]], 1);
			else
				info("Unknown key (scancode %#x) pressed.", kbd->new[i]);
		}
	}

	memcpy(kbd->old, kbd->new, 8);
}

int usb_kbd_event(struct input_dev *dev, unsigned int type, unsigned int code, int value)
{
	struct usb_kbd *kbd = dev->private;

	if (type != EV_LED) return -1;

	kbd->leds = (!!test_bit(LED_KANA,    dev->led) << 3) | (!!test_bit(LED_COMPOSE, dev->led) << 3) |
		    (!!test_bit(LED_SCROLLL, dev->led) << 2) | (!!test_bit(LED_CAPSL,   dev->led) << 1) |
		    (!!test_bit(LED_NUML,    dev->led));
		
	usb_submit_urb(&kbd->led);

	return -1;
}

static void usb_kbd_led(struct urb *urb)
{
	if (urb->status)
		printk(KERN_DEBUG "nonzero control status received: %d\n", urb->status);
}

static void *usb_kbd_probe(struct usb_device *dev, unsigned int ifnum)
{
	struct usb_interface_descriptor *interface;
	struct usb_endpoint_descriptor *endpoint;
	struct usb_kbd *kbd;
	int i;

	if (dev->descriptor.bNumConfigurations != 1) return NULL;
	interface = dev->config[0].interface[ifnum].altsetting + 0;

	if (interface->bInterfaceClass != 3) return NULL;
	if (interface->bInterfaceSubClass != 1) return NULL;
	if (interface->bInterfaceProtocol != 1) return NULL;
	if (interface->bNumEndpoints != 1) return NULL;

	endpoint = interface->endpoint + 0;
	if (!(endpoint->bEndpointAddress & 0x80)) return NULL;
	if ((endpoint->bmAttributes & 3) != 3) return NULL;

	usb_set_protocol(dev, 0);

	if (!(kbd = kmalloc(sizeof(struct usb_kbd), GFP_KERNEL))) return NULL;
	memset(kbd, 0, sizeof(struct usb_kbd));

	kbd->dev.evbit[0] = BIT(EV_KEY) | BIT(EV_LED) | BIT(EV_REP);
	kbd->dev.ledbit[0] = BIT(LED_NUML) | BIT(LED_CAPSL) | BIT(LED_SCROLLL) | BIT(LED_COMPOSE) | BIT(LED_KANA);

	for (i = 0; i < 255; i++)
		set_bit(usb_kbd_keycode[i], kbd->dev.keybit);
	clear_bit(0, kbd->dev.keybit);
	
	kbd->dev.private = kbd;
	kbd->dev.event = usb_kbd_event;

	kbd->dr.requesttype = USB_RT_HIDD;
	kbd->dr.request = USB_REQ_SET_REPORT;
	kbd->dr.value = 0x200;
	kbd->dr.index = 1;
	kbd->dr.length = 1;

	{
		int pipe = usb_rcvintpipe(dev, endpoint->bEndpointAddress);
		int maxp = usb_maxpacket(dev, pipe, usb_pipeout(pipe));

		FILL_INT_URB(&kbd->irq, dev, pipe, kbd->new, maxp > 8 ? 8 : maxp,
			usb_kbd_irq, kbd, endpoint->bInterval);
	}

	FILL_CONTROL_URB(&kbd->led, dev, usb_sndctrlpipe(dev, endpoint->bEndpointAddress),
		(void*) &kbd->dr, &kbd->leds, 1, usb_kbd_led, kbd);
			
	if (usb_submit_urb(&kbd->irq)) {
		kfree(kbd);
		return NULL;
	}

	input_register_device(&kbd->dev);

	printk(KERN_INFO "input%d: USB HIDBP keyboard\n", kbd->dev.number);


	return kbd;
}

static void usb_kbd_disconnect(struct usb_device *dev, void *ptr)
{
	struct usb_kbd *kbd = ptr;
	usb_unlink_urb(&kbd->irq);
	input_unregister_device(&kbd->dev);
	kfree(kbd);
}

static struct usb_driver usb_kbd_driver = {
	name:		"keyboard",
	probe:		usb_kbd_probe,
	disconnect:	usb_kbd_disconnect
};

#ifdef MODULE
void cleanup_module(void)
{
	usb_deregister(&usb_kbd_driver);
}

int init_module(void)
#else
int usb_kbd_init(void)
#endif
{
	usb_register(&usb_kbd_driver);
	return 0;
}
