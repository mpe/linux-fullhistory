/*
 *  joy-assasin.c  Version 1.2
 *
 *  Copyright (c) 1998 Vojtech Pavlik
 */

/*
 * This is a module for the Linux joystick driver, supporting
 * joysticks using FP-Gaming's Assasin 3D protocol.
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
#include <asm/system.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/joystick.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>

#define JS_AS_MAX_START		250
#define JS_AS_MAX_STROBE	50
#define JS_AS_MAX_TIME		2400
#define JS_AS_MAX_LENGTH	40

#define JS_AS_MODE_A3D		1	/* Assasin 3D */
#define JS_AS_MODE_PAN		2	/* Panther */
#define JS_AS_MODE_OEM		3	/* Panther OEM version */
#define JS_AS_MODE_PXL		4	/* Panther XL */

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_PARM(js_as, "2-24i");

static int js_as[]={-1,0,0,-1,0,0,-1,0,0,-1,0,0,-1,0,0,-1,0,0,-1,0,0,-1,0,0,-1,0,0};

static int js_as_port_list[] __initdata = {0x201, 0};
static struct js_port* js_as_port __initdata = NULL;

#include "joy-analog.h"

struct js_as_info {
	int io;
	char mode;
	char rudder;
	struct js_an_info an;
};

/*
 * js_as_read_packet() reads an Assasin 3D packet.
 */

static int js_as_read_packet(int io, int length, char *data)
{
	unsigned char u, v;
	int i;
	unsigned int t, t1;
	unsigned long flags;

	int start = (js_time_speed * JS_AS_MAX_START) >> 10;
	int strobe = (js_time_speed * JS_AS_MAX_STROBE) >> 10;

	i = 0;

	__save_flags(flags);
	__cli();
	outb(0xff,io);

	u = inb(io);
	t = js_get_time();

	do {
		v = inb(io);
		t1 = js_get_time();
	} while (u == v && js_delta(t1, t) < start);

	t = t1;

	do {
		v = inb(io);
		t1 = js_get_time();
		if ((u ^ v) & u & 0x10) {
			data[i++] = v >> 5;
			t = t1;
		}
		u = v;
	} while (i < length && js_delta(t1,t) < strobe);

	__restore_flags(flags);

	return i;
}

/*
 * js_as_csum() computes checksum of triplet packet
 */

static int js_as_csum(char *data, int count)
{
	int i, csum = 0;
	for (i = 0; i < count - 2; i++) csum += data[i];
	return (csum & 0x3f) != ((data[count - 2] << 3) | data[count - 1]);
}

/*
 * js_as_read() reads and analyzes A3D joystick data.
 */

static int js_as_read(void *xinfo, int **axes, int **buttons)
{
	struct js_as_info *info = xinfo;
	char data[JS_AS_MAX_LENGTH];

	switch (info->mode) {

		case JS_AS_MODE_A3D:
		case JS_AS_MODE_OEM:
		case JS_AS_MODE_PAN:

			if (js_as_read_packet(info->io, 29, data) != 29) return -1;
			if (data[0] != info->mode) return -1;
			if (js_as_csum(data, 29)) return -1;

			axes[0][0] = ((data[5] << 6) | (data[6] << 3) | data[ 7]) - ((data[5] & 4) << 7);
			axes[0][1] = ((data[8] << 6) | (data[9] << 3) | data[10]) - ((data[8] & 4) << 7);

			buttons[0][0] = (data[2] << 2) | (data[3] >> 1);

			info->an.axes[0] = ((char)((data[11] << 6) | (data[12] << 3) | (data[13]))) + 128;
			info->an.axes[1] = ((char)((data[14] << 6) | (data[15] << 3) | (data[16]))) + 128;
			info->an.axes[2] = ((char)((data[17] << 6) | (data[18] << 3) | (data[19]))) + 128;
			info->an.axes[3] = ((char)((data[20] << 6) | (data[21] << 3) | (data[22]))) + 128;

			info->an.buttons = ((data[3] << 3) | data[4]) & 0xf;

			js_an_decode(&info->an, axes + 1, buttons + 1);

			return 0;

		case JS_AS_MODE_PXL:

			if (js_as_read_packet(info->io, 33, data) != 33) return -1;
			if (data[0] != info->mode) return -1;
			if (js_as_csum(data, 33)) return -1;

			axes[0][0] = ((char)((data[15] << 6) | (data[16] << 3) | (data[17]))) + 128;
			axes[0][1] = ((char)((data[18] << 6) | (data[19] << 3) | (data[20]))) + 128;
			info->an.axes[0] = ((char)((data[21] << 6) | (data[22] << 3) | (data[23]))) + 128;
			axes[0][2] = ((char)((data[24] << 6) | (data[25] << 3) | (data[26]))) + 128;

			axes[0][3] = ( data[5]       & 1) - ((data[5] >> 2) & 1);
			axes[0][4] = ((data[5] >> 1) & 1) - ((data[6] >> 2) & 1);
			axes[0][5] = ((data[4] >> 1) & 1) - ( data[3]       & 1);
			axes[0][6] = ((data[4] >> 2) & 1) - ( data[4]       & 1);

			axes[0][7] = ((data[ 9] << 6) | (data[10] << 3) | data[11]) - ((data[ 9] & 4) << 7);
			axes[0][8] = ((data[12] << 6) | (data[13] << 3) | data[14]) - ((data[12] & 4) << 7);

			buttons[0][0] = (data[2] << 8) | ((data[3] & 6) << 5) | (data[7] << 3) | data[8];

			if (info->rudder) axes[1][0] = info->an.axes[0];

			return 0;

		default:
			printk("Error.\n");
			return -1;
	}
}

/*
 * js_as_open() is a callback from the file open routine.
 */

static int js_as_open(struct js_dev *jd)
{
	MOD_INC_USE_COUNT;
	return 0;
}

/*
 * js_as_close() is a callback from the file release routine.
 */

static int js_as_close(struct js_dev *jd)
{
	MOD_DEC_USE_COUNT;
	return 0;
}

/*
 * js_as_pxl_init_corr() initializes the correction values for
 * the Panther XL.
 */

static void __init js_as_pxl_init_corr(struct js_corr **corr, int **axes)
{
	int i;

	for (i = 0; i < 2; i++) {
		corr[0][i].type = JS_CORR_BROKEN;
		corr[0][i].prec = 0;
		corr[0][i].coef[0] = axes[0][i] - 4;
		corr[0][i].coef[1] = axes[0][i] + 4;
		corr[0][i].coef[2] = (1 << 29) / (127 - 32);
		corr[0][i].coef[3] = (1 << 29) / (127 - 32);
	}

	corr[0][2].type = JS_CORR_BROKEN;
	corr[0][2].prec = 0;
	corr[0][2].coef[0] = 127 - 4;
	corr[0][2].coef[1] = 128 + 4;
	corr[0][2].coef[2] = (1 << 29) / (127 - 6);
	corr[0][2].coef[3] = (1 << 29) / (127 - 6);

	for (i = 3; i < 7; i++) {
		corr[0][i].type = JS_CORR_BROKEN;
		corr[0][i].prec = 0;
		corr[0][i].coef[0] = 0;
		corr[0][i].coef[1] = 0;
		corr[0][i].coef[2] = (1 << 29);
		corr[0][i].coef[3] = (1 << 29);
	}

	for (i = 7; i < 9; i++) {
		corr[0][i].type = JS_CORR_BROKEN;
		corr[0][i].prec = -1;
		corr[0][i].coef[0] = 0;
		corr[0][i].coef[1] = 0;
		corr[0][i].coef[2] = (104 << 14);
		corr[0][i].coef[3] = (104 << 14);
	}
}

/*
 * js_as_as_init_corr() initializes the correction values for
 * the Panther and Assasin.
 */

static void __init js_as_as_init_corr(struct js_corr **corr)
{
	int i;

	for (i = 0; i < 2; i++) {
		corr[0][i].type = JS_CORR_BROKEN;
		corr[0][i].prec = -1;
		corr[0][i].coef[0] = 0;
		corr[0][i].coef[1] = 0;
		corr[0][i].coef[2] = (104 << 14);
		corr[0][i].coef[3] = (104 << 14);
	}
}

/*
 * js_as_rudder_init_corr() initializes the correction values for
 * the Panther XL connected rudder.
 */

static void __init js_as_rudder_init_corr(struct js_corr **corr, int **axes)
{
	corr[1][0].type = JS_CORR_BROKEN;
	corr[1][0].prec = 0;
	corr[1][0].coef[0] = axes[1][0] - (axes[1][0] >> 3);
	corr[1][0].coef[1] = axes[1][0] + (axes[1][0] >> 3);
	corr[1][0].coef[2] = (1 << 29) / (axes[1][0] - (axes[1][0] >> 2) + 1);
	corr[1][0].coef[3] = (1 << 29) / (axes[1][0] - (axes[1][0] >> 2) + 1);
}

/*
 * js_as_probe() probes for A3D joysticks.
 */

static struct js_port __init *js_as_probe(int io, int mask0, int mask1, struct js_port *port)
{
	struct js_as_info iniinfo;
	struct js_as_info *info = &iniinfo;
	char *name;
	char data[JS_AS_MAX_LENGTH];
	unsigned char u;
	int i;
	int numdev;

	memset(info, 0, sizeof(struct js_as_info));

	if (io < 0) return port;

	if (check_region(io, 1)) return port;
	if (((u = inb(io)) & 3) == 3) return port;
	outb(0xff,io);
	if (!((inb(io) ^ u) & ~u & 0xf)) return port;

	if (js_as_read_packet(io, 1, data) != 1) return port;

	if (data[0] && data[0] <= 4) {
		info->mode = data[0];
		info->io = io;
		request_region(io, 1, "joystick (assasin)");
		port = js_register_port(port, info, 3, sizeof(struct js_as_info), js_as_read);
		info = port->info;
	} else {
		printk(KERN_WARNING "joy-assasin: unknown joystick device detected "
			"(io=%#x, id=%d), contact <vojtech@ucw.cz>\n", io, data[0]);
		return port;
	}

	udelay(JS_AS_MAX_TIME);

	if (info->mode == JS_AS_MODE_PXL) {
			printk(KERN_INFO "js%d: MadCatz Panther XL at %#x\n",
				js_register_device(port, 0, 9, 9, "MadCatz Panther XL", js_as_open, js_as_close),
				info->io);
			js_as_read(port->info, port->axes, port->buttons);
			js_as_pxl_init_corr(port->corr, port->axes);
			if (info->an.axes[0] < 254) {
			printk(KERN_INFO "js%d: Analog rudder on MadCatz Panther XL\n",
				js_register_device(port, 1, 1, 0, "Analog rudder", js_as_open, js_as_close));
				info->rudder = 1;
				port->axes[1][0] = info->an.axes[0];
				js_as_rudder_init_corr(port->corr, port->axes);
			}
			return port;
	}

	switch (info->mode) {
		case JS_AS_MODE_A3D: name = "FP-Gaming Assasin 3D"; break;
		case JS_AS_MODE_PAN: name = "MadCatz Panther"; break;
		case JS_AS_MODE_OEM: name = "OEM Assasin 3D"; break;
		default: name = "This cannot happen"; break;
	}

	printk(KERN_INFO "js%d: %s at %#x\n",
		js_register_device(port, 0, 2, 3, name, js_as_open, js_as_close),
		name, info->io);

	js_as_as_init_corr(port->corr);

	js_as_read(port->info, port->axes, port->buttons);

	for (i = u = 0; i < 4; i++) if (info->an.axes[i] < 254) u |= 1 << i;

	if ((numdev = js_an_probe_devs(&info->an, u, mask0, mask1, port)) <= 0)
		return port;

	for (i = 0; i < numdev; i++)
		printk(KERN_INFO "js%d: %s on %s\n",
			js_register_device(port, i + 1, js_an_axes(i, &info->an), js_an_buttons(i, &info->an),
				js_an_name(i, &info->an), js_as_open, js_as_close),
			js_an_name(i, &info->an), name);

	js_an_decode(&info->an, port->axes + 1, port->buttons + 1);
	js_an_init_corr(&info->an, port->axes + 1, port->corr + 1, 0);

	return port;
}

#ifndef MODULE
void __init js_as_setup(char *str, int *ints)
{
	int i;
	for (i = 0; i <= ints[0] && i < 24; i++) js_as[i] = ints[i+1];
}
#endif

#ifdef MODULE
int init_module(void)
#else
int __init js_as_init(void)
#endif
{
	int i;

	if (js_as[0] >= 0) {
		for (i = 0; (js_as[i*3] >= 0) && i < 8; i++)
			js_as_port = js_as_probe(js_as[i*3], js_as[i*3+1], js_as[i*3+2], js_as_port);
	} else {
		for (i = 0; js_as_port_list[i]; i++) js_as_port = js_as_probe(js_as_port_list[i], 0, 0, js_as_port);
	}
	if (js_as_port) return 0;

#ifdef MODULE
	printk(KERN_WARNING "joy-assasin: no joysticks found\n");
#endif

	return -ENODEV;
}

#ifdef MODULE
void cleanup_module(void)
{
	int i;
	struct js_as_info *info;

	while (js_as_port != NULL) {
		for (i = 0; i < js_as_port->ndevs; i++)
			if (js_as_port->devs[i] != NULL)
				js_unregister_device(js_as_port->devs[i]);
		info = js_as_port->info;
		release_region(info->io, 1);
		js_as_port = js_unregister_port(js_as_port);
	}

}
#endif
