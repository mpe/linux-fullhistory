/*
 *  joy-thrustmaster.c  Version 1.2
 *
 *  Copyright (c) 1998-1999 Vojtech Pavlik
 *
 *  Sponsored by SuSE
 */

/*
 * This is a module for the Linux joystick driver, supporting
 * ThrustMaster DirectConnect (BSP) joystick family.
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
#include <asm/system.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/joystick.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/init.h>

#define JS_TM_MAX_START		400
#define JS_TM_MAX_STROBE	45
#define JS_TM_MAX_LENGTH	13

#define JS_TM_MODE_M3DI		1
#define JS_TM_MODE_3DRP		3
#define JS_TM_MODE_FGP		163

#define JS_TM_BYTE_ID		10
#define JS_TM_BYTE_REV		11
#define JS_TM_BYTE_DEF		12

static int js_tm_port_list[] __initdata = {0x201, 0};
static struct js_port* js_tm_port __initdata = NULL;

static unsigned char js_tm_byte_a[16] = { 0, 1, 3, 4, 6, 7 };
static unsigned char js_tm_byte_d[16] = { 2, 5, 8, 9 };

struct js_tm_info {
	int io;
	unsigned char mode;
};

/*
 * js_tm_read_packet() reads a ThrustMaster packet.
 */

static int js_tm_read_packet(int io, unsigned char *data)
{
	unsigned int t, p;
	unsigned char u, v, error;
	int i, j;
	unsigned long flags;

	error = 0;
	i = j = 0;
	p = t = JS_TM_MAX_START;

	__save_flags(flags);
	__cli();
	outb(0xff,io);
	
	v = inb(io) >> 4;

	do {
		t--;
		u = v; v = inb(io) >> 4;
		if (~v & u & 2) {
			if (j) {
				if (j < 9) {				/* Data bit */
					data[i] |= (~v & 1) << (j - 1);
					j++;
				} else {				/* Stop bit */
					error |= v & 1;
					j = 0;
					i++;
				}
			} else {					/* Start bit */
				data[i] = 0;
				error |= ~v & 1;
				j++;
			}
			p = t = (p - t) << 1;
		}
	} while (!error && i < JS_TM_MAX_LENGTH && t > 0);

	__restore_flags(flags);

	return -(i != JS_TM_MAX_LENGTH);
}

/*
 * js_tm_read() reads and analyzes ThrustMaster joystick data.
 */

static int js_tm_read(void *xinfo, int **axes, int **buttons)
{
	struct js_tm_info *info = xinfo;
	unsigned char data[JS_TM_MAX_LENGTH];
	int i;

	if (js_tm_read_packet(info->io, data)) return -1;
	if (data[JS_TM_BYTE_ID] != info->mode) return -1;

	for (i = 0; i < data[JS_TM_BYTE_DEF] >> 4; i++) axes[0][i] = data[js_tm_byte_a[i]];

	switch (info->mode) {

		case JS_TM_MODE_M3DI:

			axes[0][4] = ((data[js_tm_byte_d[0]] >> 3) & 1) - ((data[js_tm_byte_d[0]] >> 1) & 1);
			axes[0][5] = ((data[js_tm_byte_d[0]] >> 2) & 1) - ( data[js_tm_byte_d[0]]       & 1);

			buttons[0][0] = ((data[js_tm_byte_d[0]] >> 6) & 0x01) | ((data[js_tm_byte_d[0]] >> 3) & 0x06)
				      | ((data[js_tm_byte_d[0]] >> 4) & 0x08) | ((data[js_tm_byte_d[1]] >> 2) & 0x30);

			return 0;

		case JS_TM_MODE_3DRP:
		case JS_TM_MODE_FGP:

			buttons[0][0] = (data[js_tm_byte_d[0]] & 0x3f) | ((data[js_tm_byte_d[1]] << 6) & 0xc0)
				      | (( ((int) data[js_tm_byte_d[0]]) << 2) & 0x300);

			return 0;

		default:

			buttons[0][0] = 0;

			for (i = 0; i < (data[JS_TM_BYTE_DEF] & 0xf); i++)
				buttons[0][0] |= ((int) data[js_tm_byte_d[i]]) << (i << 3);

			return 0;

	}

	return -1;
}

/*
 * js_tm_init_corr() initializes the correction values for
 * ThrustMaster joysticks.
 */

static void __init js_tm_init_corr(int num_axes, int mode, int **axes, struct js_corr **corr)
{
	int j = 0;

	for (; j < num_axes; j++) {
		corr[0][j].type = JS_CORR_BROKEN;
		corr[0][j].prec = 0;
		corr[0][j].coef[0] = 127 - 2;
		corr[0][j].coef[1] = 128 + 2;
		corr[0][j].coef[2] = (1 << 29) / (127 - 4);
		corr[0][j].coef[3] = (1 << 29) / (127 - 4);
	}

	switch (mode) {
		case JS_TM_MODE_M3DI: j = 4; break;
		default: break;
	}

	for (; j < num_axes; j++) {
		corr[0][j].type = JS_CORR_BROKEN;
		corr[0][j].prec = 0;
		corr[0][j].coef[0] = 0;
		corr[0][j].coef[1] = 0;
		corr[0][j].coef[2] = (1 << 29);
		corr[0][j].coef[3] = (1 << 29);
	}

}

/*
 * js_tm_probe() probes for ThrustMaster type joysticks.
 */

static struct js_port __init *js_tm_probe(int io, struct js_port *port)
{
	struct js_tm_info info;
	struct js_rm_models {
		unsigned char id;
		char *name;
		char axes;
		char buttons;
	} models[] = {	{   1, "ThrustMaster Millenium 3D Inceptor", 6, 6 },
			{   3, "ThrustMaster Rage 3D Gamepad", 2, 10 },
			{ 163, "Thrustmaster Fusion GamePad", 2, 10 },
			{   0, NULL, 0, 0 }};
	char name[64];
	unsigned char data[JS_TM_MAX_LENGTH];
	unsigned char a, b;
	int i;

	if (check_region(io, 1)) return port;

	if (js_tm_read_packet(io, data)) return port;

	info.io = io;
	info.mode = data[JS_TM_BYTE_ID];

	if (!info.mode) return port;

	for (i = 0; models[i].id && models[i].id != info.mode; i++);

	if (models[i].id != info.mode) {
		a = data[JS_TM_BYTE_DEF] >> 4;
		b = (data[JS_TM_BYTE_DEF] & 0xf) << 3;
		sprintf(name, "Unknown %d-axis, %d-button TM device %d", a, b, info.mode);
	} else {
		sprintf(name, models[i].name);
		a = models[i].axes;
		b = models[i].buttons;
	}

	request_region(io, 1, "joystick (thrustmaster)");
	port = js_register_port(port, &info, 1, sizeof(struct js_tm_info), js_tm_read);
	printk(KERN_INFO "js%d: %s revision %d at %#x\n",
		js_register_device(port, 0, a, b, name, THIS_MODULE, NULL, NULL), name, data[JS_TM_BYTE_REV], io);
	js_tm_init_corr(a, info.mode, port->axes, port->corr);

	return port;
}

#ifdef MODULE
int init_module(void)
#else
int __init js_tm_init(void)
#endif
{
	int *p;

	for (p = js_tm_port_list; *p; p++) js_tm_port = js_tm_probe(*p, js_tm_port);
	if (js_tm_port) return 0;

#ifdef MODULE
	printk(KERN_WARNING "joy-thrustmaster: no joysticks found\n");
#endif

	return -ENODEV;
}

#ifdef MODULE
void cleanup_module(void)
{
	struct js_tm_info *info;

	while (js_tm_port) {
		js_unregister_device(js_tm_port->devs[0]);
		info = js_tm_port->info;
		release_region(info->io, 1);
		js_tm_port = js_unregister_port(js_tm_port);
	}
}
#endif
