/*
 *  joy-analog.c  Version 1.2
 *
 *  Copyright (c) 1996-1998 Vojtech Pavlik
 */

/*
 * This is a module for the Linux joystick driver, supporting
 * up to two analog (or CHF/FCS) joysticks on a single joystick port.
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

#define JS_AN_MAX_TIME 3000

static int js_an_port_list[] __initdata = {0x201, 0};
static struct js_port* js_an_port __initdata = NULL;

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_PARM(js_an, "2-24i");

static int js_an[]={-1,0,0,-1,0,0,-1,0,0,-1,0,0,-1,0,0,-1,0,0,-1,0,0,-1,0,0,-1,0,0};

#include "joy-analog.h"

/*
 * js_an_read() reads analog joystick data.
 */

static int js_an_read(void *xinfo, int **axes, int **buttons)
{
	struct js_an_info *info = xinfo;
	unsigned char buf[4];
	int time[4];
	unsigned char u, v, a;
	unsigned int t, t1;
	int i, j;
	int timeout;
	int io = info->io;

	timeout = (JS_AN_MAX_TIME * js_time_speed_a) >> 10;

	info->buttons = (~inb(io) & JS_AN_BUTTONS_STD) >> 4;

	i = 0;
	u = a = ((info->mask[0] | info->mask[1]) & JS_AN_AXES_STD) | (info->extensions & JS_AN_HAT_FCS)
	      | ((info->extensions & JS_AN_BUTTONS_PXY_XY) >> 2) | ((info->extensions & JS_AN_BUTTONS_PXY_UV) >> 4);

	outb(0xff,io);
	t = js_get_time_a();
	do {
		v = inb(io) & a;
		t1 = js_get_time_a();
		if (u ^ v) {
			time[i] = js_delta_a(t1,t);
			buf[i] = u ^ v;
			u = v;
			i++;
		}
	} while (v && js_delta_a(t1,t) < timeout);

	for (--i; i >= 0; i--)
		for (j = 0; j < 4; j++)
			if (buf[i] & (1 << j)) info->axes[j] = (time[i] << 10) / js_time_speed_a;

	js_an_decode(info, axes, buttons);

	return 0;
}

/*
 * js_an_open() is a callback from the file open routine.
 */

static int js_an_open(struct js_dev *jd)
{
	MOD_INC_USE_COUNT;
	return 0;
}

/*
 * js_an_close() is a callback from the file release routine.
 */

static int js_an_close(struct js_dev *jd)
{
	MOD_DEC_USE_COUNT;
	return 0;
}

/*
 * js_an_probe() probes for analog joysticks.
 */

static struct js_port __init *js_an_probe(int io, int mask0, int mask1, struct js_port *port)
{
	struct js_an_info info;
	int i, numdev;
	unsigned char u;

	if (io < 0) return port;

	if (check_region(io, 1)) return port;

	if (((u = inb(io)) & 3) == 3) return port;
	outb(0xff,io);
	u = inb(io);
	udelay(JS_AN_MAX_TIME);
	u = (inb(io) ^ u) & u;

	if (!u) return port;
	if (u & 0xf0) return port;

	if ((numdev = js_an_probe_devs(&info, u, mask0, mask1, port)) <= 0)
		return port;

	info.io = io;
	request_region(info.io, 1, "joystick (analog)");
	port = js_register_port(port, &info, numdev, sizeof(struct js_an_info), js_an_read);

	for (i = 0; i < numdev; i++)
		printk(KERN_INFO "js%d: %s at %#x\n",
			js_register_device(port, i, js_an_axes(i, &info), js_an_buttons(i, &info),
				js_an_name(i, &info), js_an_open, js_an_close),
			js_an_name(i, &info), info.io);

	js_an_read(port->info, port->axes, port->buttons);
	js_an_init_corr(port->info, port->axes, port->corr, 8);

	return port;
}

#ifndef MODULE
void __init js_an_setup(char *str, int *ints)
{
	int i;
	for (i = 0; i <= ints[0] && i < 24; i++) js_an[i] = ints[i+1];
}
#endif

#ifdef MODULE
int init_module(void)
#else
int __init js_an_init(void)
#endif
{
	int i;

	if (js_an[0] >= 0) {
		for (i = 0; (js_an[i*3] >= 0) && i < 8; i++)
			js_an_port = js_an_probe(js_an[i*3], js_an[i*3+1], js_an[i*3+2], js_an_port);
	} else {
		for (i = 0; js_an_port_list[i]; i++)
			js_an_port = js_an_probe(js_an_port_list[i], 0, 0, js_an_port);
	}
	if (js_an_port) return 0;

#ifdef MODULE
	printk(KERN_WARNING "joy-analog: no joysticks found\n");
#endif

	return -ENODEV;
}

#ifdef MODULE
void cleanup_module(void)
{
	int i;
	struct js_an_info *info;

	while (js_an_port != NULL) {
		for (i = 0; i < js_an_port->ndevs; i++)
			if (js_an_port->devs[i] != NULL)
				js_unregister_device(js_an_port->devs[i]);
		info = js_an_port->info;
		release_region(info->io, 1);
		js_an_port = js_unregister_port(js_an_port);
	}

}
#endif
