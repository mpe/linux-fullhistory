/*
 *  joy-lightning.c  Version 1.2
 *
 *  Copyright (c) 1998 Vojtech Pavlik
 */

/*
 * This is a module for the Linux joystick driver, supporting
 * PDPI Lightning 4 gamecards and analog joysticks connected
 * to them.
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
 * e-mail - mail your message to <vojtech@ucw.cz>, or by paper mail:
 * Vojtech Pavlik, Ucitelska 1576, Prague 8, 182 00 Czech Republic
 */

#include <asm/io.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/joystick.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>

#define JS_L4_PORT		0x201
#define JS_L4_SELECT_ANALOG	0xa4
#define JS_L4_SELECT_DIGITAL	0xa5
#define JS_L4_SELECT_SECONDARY	0xa6
#define JS_L4_CMD_ID		0x80
#define JS_L4_CMD_GETCAL	0x92
#define JS_L4_CMD_SETCAL	0x93
#define JS_L4_ID		0x04
#define JS_L4_BUSY		0x01
#define JS_L4_TIMEOUT		80	/* 80 us */

static struct js_port* __initdata js_l4_port = NULL;

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_PARM(js_l4, "2-24i");

static int js_l4[]={-1,0,0,-1,0,0,-1,0,0,-1,0,0,-1,0,0,-1,0,0,-1,0,0,-1,0,0,-1,0,0};

#include "joy-analog.h"

struct js_l4_info {
	int port;
	struct js_an_info an;
};

/*
 * js_l4_wait_ready() waits for the L4 to become ready.
 */

static int js_l4_wait_ready(void)
{
	unsigned int t, t1, timeout;
	timeout = (JS_L4_TIMEOUT * js_time_speed) >> 10;
	t = t1 = js_get_time();
	while ((inb(JS_L4_PORT) & JS_L4_BUSY) && (js_delta(t1 = js_get_time(), t) < timeout));
	return -(js_delta(t1, t) >= timeout);
}

/*
 * js_l4_read() reads data from the Lightning 4.
 */

static int js_l4_read(void *xinfo, int **axes, int **buttons)
{
	struct js_l4_info *info = xinfo;
	int i;
	unsigned char status;

	outb(JS_L4_SELECT_ANALOG, JS_L4_PORT);
	outb(JS_L4_SELECT_DIGITAL + (info->port >> 2), JS_L4_PORT);

	if (inb(JS_L4_PORT) & JS_L4_BUSY) return -1;
	outb(info->port & 3, JS_L4_PORT);

	if (js_l4_wait_ready()) return -1;
	status = inb(JS_L4_PORT);

	for (i = 0; i < 4; i++)
		if (status & (1 << i)) {
			if (js_l4_wait_ready()) return -1;
			info->an.axes[i] = inb(JS_L4_PORT);
		}

	if (status & 0x10) {
		if (js_l4_wait_ready()) return -1;
		info->an.buttons = inb(JS_L4_PORT);
	}

	js_an_decode(&info->an, axes, buttons);

	outb(JS_L4_SELECT_ANALOG, JS_L4_PORT);

	return 0;
}

#if 0

/*
 * js_l4_setcal() programs the L4 with calibration values.
 */

static int js_l4_setcal(int port, int *calib)
{
	int i;

	outb(JS_L4_SELECT_ANALOG, JS_L4_PORT);
	outb(JS_L4_SELECT_DIGITAL + (port >> 2), JS_L4_PORT);

	if (inb(JS_L4_PORT) & JS_L4_BUSY) return -1;

	outb(JS_L4_CMD_SETCAL, JS_L4_PORT);
	if (js_l4_wait_ready()) return -1;

        outb(port & 3, JS_L4_PORT);
	if (js_l4_wait_ready()) return -1;

	for (i = 0; i < 4; i++) {
		outb(calib[0], JS_L4_PORT);
		if (js_l4_wait_ready()) return -1;
	}

	outb(JS_L4_SELECT_ANALOG, JS_L4_PORT);

	return 0;
}


static void js_l4_calibrate(struct js_l4_info *info)
{
	int calib[4];
	int i;

	if ((info->an.extensions & JS_AN_BUTTON_PXY_X) && !(info->an.extensions & JS_AN_BUTTON_PXY_U))
		info->an.axes[2] >>= 2;						/* Pad button X */

	if ((info->an.extensions & JS_AN_BUTTON_PXY_Y) && !(info->an.extensions & JS_AN_BUTTON_PXY_V))
		info->an.axes[3] >>= 2;						/* Pad button Y */

	if (info->an.extensions & JS_AN_HAT_FCS) 
		info->an.axes[3] >>= 2;						/* FCS hat */

	if (((info->an.mask[0] & 0xb) == 0xb) ||
	    ((info->an.mask[1] & 0xb) == 0xb))
		info->an.axes[3] = (info->an.axes[0] + info->an.axes[1]) >> 1;	/* Throttle */

	for (i = 0; i < 4; i++)
		calib[i] = (info->an.axes[i] * 255) / 100;
	
	for (i = 0; i < 4; i++)
		if (calib[i] > 255) calib[i] = 255;

	js_l4_setcal(info->port, calib);

}
	
#endif

/*
 * js_l4_open() is a callback from the file open routine.
 */

static int js_l4_open(struct js_dev *jd)
{
	MOD_INC_USE_COUNT;
	return 0;
}

/*
 * js_l4_close() is a callback from the file release routine.
 */

static int js_l4_close(struct js_dev *jd)
{
	MOD_DEC_USE_COUNT;
	return 0;
}

/*
 * js_l4_probe() probes for joysticks on the L4 cards.
 */

static struct js_port __init *js_l4_probe(int cards, int l4port, int mask0, int mask1, struct js_port *port)
{
	struct js_l4_info iniinfo;
	struct js_l4_info *info = &iniinfo;
#if 0
	int calib[4] = {255, 255, 255, 255};
#endif
	int i, numdev;
	unsigned char u;

	if (l4port < 0) return port;
	if (~cards & (1 << (l4port >> 2))) return port;

	memset(info, 0, sizeof(struct js_l4_info));
	info->port = l4port;

#if 0
	js_l4_setcal(info->port, calib);
#endif
	if (js_l4_read(info, NULL, NULL)) return port;

	for (i = u = 0; i < 4; i++) if (info->an.axes[i] < 253) u |= 1 << i;

	if ((numdev = js_an_probe_devs(&info->an, u, mask0, mask1, port)) <= 0)
		return port;

	port = js_register_port(port, info, numdev, sizeof(struct js_l4_info), js_l4_read);

	for (i = 0; i < numdev; i++)
		printk(KERN_INFO "js%d: %s on L4 port %d\n",
			js_register_device(port, i, js_an_axes(i, &info->an), js_an_buttons(i, &info->an),
				js_an_name(i, &info->an), js_l4_open, js_l4_close),
			js_an_name(i, &info->an), info->port);

	info = port->info;

#if 0
	js_l4_calibrate(info);
#endif
	js_l4_read(info, port->axes, port->buttons);
	js_an_init_corr(&info->an, port->axes, port->corr, 0);

	return port;
}

/*
 * js_l4_card_probe() probes for presence of the L4 card(s).
 */

static int __init js_l4_card_probe(void)
{
	int i, cards = 0;
	unsigned char rev = 0;

	if (check_region(JS_L4_PORT, 1)) return 0;

	for (i = 0; i < 2; i++) {

		outb(JS_L4_SELECT_ANALOG, JS_L4_PORT);
		outb(JS_L4_SELECT_DIGITAL + i, JS_L4_PORT);		/* Select card 0-1 */

		if (inb(JS_L4_PORT) & JS_L4_BUSY) continue;
		outb(JS_L4_CMD_ID, JS_L4_PORT);				/* Get card ID & rev */

		if (js_l4_wait_ready()) continue;
		if (inb(JS_L4_PORT) != JS_L4_SELECT_DIGITAL + i) continue;

		if (js_l4_wait_ready()) continue;
		if (inb(JS_L4_PORT) != JS_L4_ID) continue;

		if (js_l4_wait_ready()) continue;
		rev = inb(JS_L4_PORT);

		cards |= (1 << i);

		printk(KERN_INFO "js: PDPI Lightning 4 %s card (ports %d-%d) firmware v%d.%d found at %#x\n",
			i ? "secondary" : "primary", (i << 2), (i << 2) + 3, rev >> 4, rev & 0xf, JS_L4_PORT);
	}

	outb(JS_L4_SELECT_ANALOG, JS_L4_PORT);

	return cards;
}

#ifndef MODULE
void __init js_l4_setup(char *str, int *ints)
{
	int i;
	for (i = 0; i <= ints[0] && i < 24; i++) js_l4[i] = ints[i+1];
}
#endif

#ifdef MODULE
int init_module(void)
#else
int __init js_l4_init(void)
#endif
{
	int i, cards;

	cards = js_l4_card_probe();

	if (js_l4[0] >= 0) {
		for (i = 0; (js_l4[i*3] >= 0) && i < 8; i++)
			js_l4_port = js_l4_probe(cards, js_l4[i*3], js_l4[i*3+1], js_l4[i*3+2], js_l4_port);
	} else {
		for (i = 0; i < (cards << 2); i++)
			js_l4_port = js_l4_probe(cards, i, 0, 0, js_l4_port);
	}

	if (js_l4_port == NULL) {
#ifdef MODULE
		printk(KERN_WARNING "joy-lightning: no joysticks found\n");
#endif
		return -ENODEV;
	}

	request_region(JS_L4_PORT, 1, "joystick (lightning)");

	return 0;
}

#ifdef MODULE
void cleanup_module(void)
{
	int i;

	while (js_l4_port) {
		for (i = 0; i < js_l4_port->ndevs; i++)
			if (js_l4_port->devs[i])
				js_unregister_device(js_l4_port->devs[i]);
		js_l4_port = js_unregister_port(js_l4_port);
	}
	outb(JS_L4_SELECT_ANALOG, JS_L4_PORT);
	release_region(JS_L4_PORT, 1);

}
#endif
