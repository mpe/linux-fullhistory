/*
 *  joy-thrustmaster.c  Version 1.2
 *
 *  Copyright (c) 1998 Vojtech Pavlik
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

#define JS_TM_MAX_START		400
#define JS_TM_MAX_STROBE	25
#define JS_TM_MAX_LENGTH	13

#define JS_TM_MODE_M3DI		1
#define JS_TM_MODE_3DRP		3
#define JS_TM_MODE_WCS3		4

#define JS_TM_MODE_MAX		5	/* Last mode + 1 */

#define JS_TM_BYTE_A0		0
#define JS_TM_BYTE_A1		1
#define JS_TM_BYTE_A2		3
#define JS_TM_BYTE_A3		4
#define JS_TM_BYTE_A4		6
#define JS_TM_BYTE_A5		7

#define JS_TM_BYTE_D0		2
#define JS_TM_BYTE_D1		5
#define JS_TM_BYTE_D2		8
#define JS_TM_BYTE_D3		9

#define JS_TM_BYTE_ID		10
#define JS_TM_BYTE_REV		11
#define JS_TM_BYTE_DEF		12

static int js_tm_port_list[] __initdata = {0x201, 0};
static struct js_port* js_tm_port __initdata = NULL;

struct js_tm_info {
	int io;
	unsigned char mode;
};

static int js_tm_id_to_def[JS_TM_MODE_MAX] = {0x00, 0x42, 0x00, 0x22, 0x00};

/*
 * js_tm_read_packet() reads a ThrustMaster packet.
 */

static int js_tm_read_packet(int io, unsigned char *data)
{
	unsigned int t, t1;
	unsigned char u, v, error;
	int i, j;
	unsigned long flags;

	int start = (js_time_speed * JS_TM_MAX_START) >> 10;
	int strobe = (js_time_speed * JS_TM_MAX_STROBE) >> 10;

	error = 0;
	i = j = 0;

	__save_flags(flags);
	__cli();
	outb(0xff,io);

	t = js_get_time();

	do {
		u = inb(io);
		t1 = js_get_time();
	} while ((u & 1) && js_delta(t1, t) < start);

	t = t1;
	u >>= 4;

	do {
		v = inb(io) >> 4;
		t1 = js_get_time();
		if ((u ^ v) & u & 2) {
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
			t = t1;
		}
		u = v;
	} while (!error && i < JS_TM_MAX_LENGTH && js_delta(t1,t) < strobe);

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

	if (js_tm_read_packet(info->io, data)) {
		printk(KERN_WARNING "joy-thrustmaster: failed to read data packet\n");
		return -1;
	}
	if (data[JS_TM_BYTE_ID] != info->mode) {
		printk(KERN_WARNING "joy-thrustmaster: ID (%d) != mode (%d)\n",
			data[JS_TM_BYTE_ID], info->mode);
		return -1;
	}
	if (data[JS_TM_BYTE_DEF] != js_tm_id_to_def[info->mode]) {
		printk(KERN_WARNING "joy-thrustmaster: DEF (%d) != def(mode) (%d)\n",
			data[JS_TM_BYTE_DEF], js_tm_id_to_def[info->mode]);
		return -1;
	}

	switch (info->mode) {

		case JS_TM_MODE_M3DI:

			axes[0][0] = data[JS_TM_BYTE_A0];
			axes[0][1] = data[JS_TM_BYTE_A1];
			axes[0][2] = data[JS_TM_BYTE_A2];
			axes[0][3] = data[JS_TM_BYTE_A3];

			axes[0][4] = ((data[JS_TM_BYTE_D0] >> 3) & 1) - ((data[JS_TM_BYTE_D0] >> 1) & 1);
			axes[0][5] = ((data[JS_TM_BYTE_D0] >> 2) & 1) - ( data[JS_TM_BYTE_D0]       & 1);

			buttons[0][0] = ((data[JS_TM_BYTE_D0] >> 6) & 0x01) | ((data[JS_TM_BYTE_D0] >> 3) & 0x06)
				      | ((data[JS_TM_BYTE_D0] >> 4) & 0x08) | ((data[JS_TM_BYTE_D1] >> 2) & 0x30);

			return 0;

		case JS_TM_MODE_3DRP:

			axes[0][0] = data[JS_TM_BYTE_A0];
			axes[0][1] = data[JS_TM_BYTE_A1];

			buttons[0][0] = ( data[JS_TM_BYTE_D0]       & 0x3f) | ((data[JS_TM_BYTE_D1] << 6) & 0xc0)
				      | (( ((int) data[JS_TM_BYTE_D0]) << 2) & 0x300);

			return 0;

	}

	return -1;
}

/*
 * js_tm_open() is a callback from the file open routine.
 */

static int js_tm_open(struct js_dev *jd)
{
	MOD_INC_USE_COUNT;
	return 0;
}

/*
 * js_tm_close() is a callback from the file release routine.
 */

static int js_tm_close(struct js_dev *jd)
{
	MOD_DEC_USE_COUNT;
	return 0;
}

/*
 * js_tm_init_corr() initializes the correction values for
 * ThrustMaster joysticks.
 */

static void __init js_tm_init_corr(int num_axes, int mode, int **axes, struct js_corr **corr)
{
	int j;

	for (j = 0; j < num_axes; j++) {
		corr[0][j].type = JS_CORR_BROKEN;
		corr[0][j].prec = 0;
		corr[0][j].coef[0] = 127 - 2;
		corr[0][j].coef[1] = 128 + 2;
		corr[0][j].coef[2] = (1 << 29) / (127 - 4);
		corr[0][j].coef[3] = (1 << 29) / (127 - 4);
	}

	switch (mode) {
		case JS_TM_MODE_M3DI: j = 4; break;
		case JS_TM_MODE_3DRP: j = 2; break;
		default:	      j = 0; break;
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
	char *names[JS_TM_MODE_MAX] = { NULL, "ThrustMaster Millenium 3D Inceptor", NULL,
						"ThrustMaster Rage 3D Gamepad", "ThrustMaster WCS III" };
	char axes[JS_TM_MODE_MAX] = { 0, 6, 0, 2, 0 };
	char buttons[JS_TM_MODE_MAX] = { 0, 5, 0, 10, 0 };

	unsigned char data[JS_TM_MAX_LENGTH];
	unsigned char u;

	if (check_region(io, 1)) return port;

	if (((u = inb(io)) & 3) == 3) return port;
	outb(0xff,io);
	if (!((inb(io) ^ u) & ~u & 0xf)) return port;

	if(js_tm_read_packet(io, data)) {
		printk(KERN_WARNING "joy-thrustmaster: probe - can't read packet\n");
		return port;
	}

	info.io = io;
	info.mode = data[JS_TM_BYTE_ID];

	if (!info.mode) return port;

	if (info.mode >= JS_TM_MODE_MAX || !names[info.mode]) {
		printk(KERN_WARNING "joy-thrustmaster: unknown device detected "
				    "(io=%#x, id=%d), contact <vojtech@ucw.cz>\n",
					io, info.mode);
		return port;
	}

	if (data[JS_TM_BYTE_DEF] != js_tm_id_to_def[info.mode]) {
		printk(KERN_WARNING "joy-thrustmaster: wrong DEF (%d) for ID %d - should be %d\n",
				data[JS_TM_BYTE_DEF], info.mode, js_tm_id_to_def[info.mode]);
	}

	request_region(io, 1, "joystick (thrustmaster)");
	port = js_register_port(port, &info, 1, sizeof(struct js_tm_info), js_tm_read);
	printk(KERN_INFO "js%d: %s revision %d at %#x\n",
		js_register_device(port, 0, axes[info.mode], buttons[info.mode],
				names[info.mode], js_tm_open, js_tm_close), names[info.mode], data[JS_TM_BYTE_REV], io);
	js_tm_init_corr(axes[info.mode], info.mode, port->axes, port->corr);

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

	while (js_tm_port != NULL) {
		js_unregister_device(js_tm_port->devs[0]);
		info = js_tm_port->info;
		release_region(info->io, 1);
		js_tm_port = js_unregister_port(js_tm_port);
	}
}
#endif
