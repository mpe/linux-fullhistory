/*
 *  joy-analog.c  Version 1.2
 *
 *  Copyright (c) 1996-1999 Vojtech Pavlik
 *
 *  Sponsored by SuSE
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
 * e-mail - mail your message to <vojtech@suse.cz>, or by paper mail:
 * Vojtech Pavlik, Ucitelska 1576, Prague 8, 182 00 Czech Republic
 */

#include <asm/io.h>
#include <asm/param.h>
#include <asm/system.h>
#include <linux/config.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/joystick.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/init.h>

#define JS_AN_MAX_TIME		3000	/* 3 ms */
#define JS_AN_LOOP_TIME		2000	/* 2 t */

static int js_an_port_list[] __initdata = {0x201, 0};
static struct js_port* js_an_port __initdata = NULL;

MODULE_AUTHOR("Vojtech Pavlik <vojtech@suse.cz>");
MODULE_PARM(js_an, "2-24i");

static int __initdata js_an[] = { -1,0,0,-1,0,0,-1,0,0,-1,0,0,-1,0,0,-1,0,0,-1,0,0,-1,0,0 };

#include "joy-analog.h"

struct js_ax_info {
        int io;
	int speed;
	int loop;
	int timeout;
        struct js_an_info an;
};

/*
 * Time macros.
 */

#ifdef __i386__
#ifdef CONFIG_X86_TSC
#define GET_TIME(x)	__asm__ __volatile__ ( "rdtsc" : "=a" (x) : : "dx" )
#define DELTA(x,y)	((x)-(y))
#define TIME_NAME "TSC"
#else
#define GET_TIME(x)	do { outb(0, 0x43); x = inb(0x40); x |= inb(0x40) << 8; } while (0)
#define DELTA(x,y)	((y)-(x)+((y)<(x)?1193180L/HZ:0))
#define TIME_NAME "PIT"
#endif
#elif __alpha__
#define GET_TIME(x)	__asm__ __volatile__ ( "rpcc %0" : "=r" (x) )
#define DELTA(x,y)	((x)-(y))
#define TIME_NAME "PCC"
#endif

#ifndef GET_TIME
#define FAKE_TIME
static unsigned long js_an_faketime = 0;
#define GET_TIME(x)     do { x = js_an_faketime++; } while(0)
#define DELTA(x,y)	((x)-(y))
#define TIME_NAME "Unreliable"
#endif

/*
 * js_an_read() reads analog joystick data.
 */

static int js_an_read(void *xinfo, int **axes, int **buttons)
{
	struct js_ax_info *info = xinfo;
	struct js_an_info *an = &info->an;
	int io = info->io;
	unsigned long flags;
	unsigned char buf[4];
	unsigned int time[4];
	unsigned char u, v, w;
	unsigned int p, q, r, s, t;
	int i, j;

	an->buttons = ~inb(io) >> 4;

	i = 0;
	w = ((an->mask[0] | an->mask[1]) & JS_AN_AXES_STD) | (an->extensions & JS_AN_HAT_FCS)
	  | ((an->extensions & JS_AN_BUTTONS_PXY_XY) >> 2) | ((an->extensions & JS_AN_BUTTONS_PXY_UV) >> 4);
	p = info->loop;
	q = info->timeout;
	
	__save_flags(flags);
	__cli();
	outb(0xff,io);
	GET_TIME(r);
	__restore_flags(flags);
	t = r;
	v = w;
	do {
		s = t;
		u = v;
		__cli();
		v = inb(io) & w;
		GET_TIME(t);
		__restore_flags(flags);
		if ((u ^ v) && (DELTA(t,s) < p)) {
			time[i] = t;
			buf[i] = u ^ v;
			i++;
		}
	} while (v && (i < 4) && (DELTA(t,r) < q));

	v <<= 4;

	for (--i; i >= 0; i--) {
		v |= buf[i];
		for (j = 0; j < 4; j++)
			if (buf[i] & (1 << j)) an->axes[j] = (DELTA(time[i],r) << 10) / info->speed;
	}

	js_an_decode(an, axes, buttons);

	return -(v != w);
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
 * js_an_calibrate_timer() calibrates the timer and computes loop
 * and timeout values for a joystick port.
 */

static void __init js_an_calibrate_timer(struct js_ax_info *info)
{
	unsigned int i, t, tx, t1, t2, t3;
	unsigned long flags;
	int io = info->io;

	save_flags(flags);
	cli();
	GET_TIME(t1);
#ifdef FAKE_TIME
	js_an_faketime += 830;
#endif
	udelay(1000);
	GET_TIME(t2);
	GET_TIME(t3);
	restore_flags(flags);

	info->speed = DELTA(t2, t1) - DELTA(t3, t2);

	tx = 1 << 30;

	for(i = 0; i < 50; i++) {
		save_flags(flags);
		cli();
		GET_TIME(t1);
		for(t = 0; t < 50; t++) { inb(io); GET_TIME(t2); }
		GET_TIME(t3);
		restore_flags(flags);
		udelay(i);
		if ((t = DELTA(t2,t1) - DELTA(t3,t2)) < tx) tx = t;
	}

        info->loop = (JS_AN_LOOP_TIME * t) / 50000;
	info->timeout = (JS_AN_MAX_TIME * info->speed) / 1000;
}

/*
 * js_an_probe() probes for analog joysticks.
 */

static struct js_port __init *js_an_probe(int io, int mask0, int mask1, struct js_port *port)
{
	struct js_ax_info info, *ax;
	int i, numdev;
	unsigned char u;

	if (io < 0) return port;

	if (check_region(io, 1)) return port;

	outb(0xff,io);
	u = inb(io);
	udelay(JS_AN_MAX_TIME);
	u = (inb(io) ^ u) & u;

	if (!u) return port;
	if (u & 0xf0) return port;

	if ((numdev = js_an_probe_devs(&info.an, u, mask0, mask1, port)) <= 0)
		return port;

	info.io = io;
	js_an_calibrate_timer(&info);

	request_region(info.io, 1, "joystick (analog)");
	port = js_register_port(port, &info, numdev, sizeof(struct js_ax_info), js_an_read);
	ax = port->info;	

	for (i = 0; i < numdev; i++)
		printk(KERN_INFO "js%d: %s at %#x ["TIME_NAME" timer, %d %sHz clock, %d ns res]\n",
			js_register_device(port, i, js_an_axes(i, &ax->an), js_an_buttons(i, &ax->an),
				js_an_name(i, &ax->an), js_an_open, js_an_close),
			js_an_name(i, &ax->an),
			ax->io,
			ax->speed > 10000 ? (ax->speed + 800) / 1000 : ax->speed,
			ax->speed > 10000 ? "M" : "k",
			ax->loop * 1000000000 / JS_AN_LOOP_TIME / ax->speed);

	js_an_read(ax, port->axes, port->buttons);
	js_an_init_corr(&ax->an, port->axes, port->corr, 8);

	return port;
}

#ifndef MODULE
int __init js_an_setup(SETUP_PARAM)
{
	int i;
	SETUP_PARSE(24);
	for (i = 0; i <= ints[0] && i < 24; i++) js_an[i] = ints[i+1];
	return 1;
}
__setup("js_an=", js_an_setup);
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
	struct js_ax_info *info;

	while (js_an_port) {
		for (i = 0; i < js_an_port->ndevs; i++)
			if (js_an_port->devs[i])
				js_unregister_device(js_an_port->devs[i]);
		info = js_an_port->info;
		release_region(info->io, 1);
		js_an_port = js_unregister_port(js_an_port);
	}

}
#endif
