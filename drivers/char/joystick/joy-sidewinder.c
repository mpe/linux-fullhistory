/*
 *  joy-sidewinder.c  Version 1.2
 *
 *  Copyright (c) 1998 Vojtech Pavlik
 */

/*
 * This is a module for the Linux joystick driver, supporting
 * Microsoft SideWinder digital joystick family.
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

#define JS_SW_MAX_START		250
#define JS_SW_MIN_STROBE	25
#define JS_SW_EXT_STROBE	45
#define JS_SW_MIN_TIME		1500
#define JS_SW_MAX_TIME		4000

#define JS_SW_MAX_LENGTH	72

#define JS_SW_MODE_3DP		1
#define JS_SW_MODE_PP		2
#define JS_SW_MODE_GP		3

static int js_sw_port_list[] __initdata = {0x201, 0};
static struct js_port* js_sw_port __initdata = NULL;

static struct {
	int x;
	int y;
} js_sw_hat_to_axis[] = {{ 0, 0}, { 0,-1}, { 1,-1}, { 1, 0}, { 1, 1}, { 0, 1}, {-1, 1}, {-1, 0}, {-1,-1}};

struct js_sw_info {
	int io;
	unsigned char mode;
	unsigned char number;
	unsigned char optimize;
};

/*
 * js_sw_init_digital() switches a SideWinder into digital mode.
 */

static void __init js_sw_init_digital(int io)
{
	unsigned int t;
	unsigned int timeout = (js_time_speed * JS_SW_MAX_TIME) >> 10;
        int delays[] = {140, 140+726, 140+300, 0};
	int i = 0;
	unsigned long flags;

	__save_flags(flags);
	__cli();
	do {
		outb(0xff,io);
		t = js_get_time();
		while ((inb(io) & 1) && (js_delta(js_get_time(),t) < timeout));
		udelay(delays[i]);
	} while (delays[i++]);
	__restore_flags(flags);

	for (i = 0; i < 4; i++) {
		udelay(300);
		outb(0xff, io);
	}

	return;
}

/*
 * js_sw_read_packet() reads a SideWinder packet.
 */

static int js_sw_read_packet(int io, int l1, int l2, int strobe, __u64 *data)
{
	static unsigned char buf[JS_SW_MAX_LENGTH];
	unsigned char u, v;
	int i;
	unsigned long flags;
	unsigned int t, t1;

	int length = l1 < l2 ? l2 : l1;
	int start = (js_time_speed * JS_SW_MAX_START) >> 10;
	strobe = (js_time_speed * strobe) >> 10;

	i = 0;

	__save_flags(flags);
	__cli();
	outb(0xff,io);

	v = inb(io);
	t = js_get_time();

	do {
		u = v;
		v = inb(io);
		t1 = js_get_time();
	} while (!((u ^ v) & u & 0x10) && js_delta(t1, t) < start);

	t = t1;

	do {
		v = inb(io);
		t1 = js_get_time();
		if ((u ^ v) & v & 0x10) {
			buf[i++] = v >> 5;
			t = t1;
		}
		u = v;
	} while (i < length && js_delta(t1,t) < strobe);

	__restore_flags(flags);

	*data = 0;

	if (i == l1) {
		t = i > 64 ? 64 : i;
		for (i = 0; i < t; i++)
			*data |= (__u64) (buf[i] & 1) << i;
		return t;
	}
	if (i == l2) {
		t = i > 22 ? 22 : i;
		for (i = 0; i < t; i++)
			*data |= (__u64) buf[i] << (3 * i);
		return t * 3;
	}

	return i;
}

/*
 * js_sw_parity computes parity of __u64
 */

static int js_sw_parity(__u64 t)
{
	t ^= t >> 32;
	t ^= t >> 16;
	t ^= t >> 8;
	t ^= t >> 4;
	t ^= t >> 2;
	t ^= t >> 1;
	return t & 1;
}

/*
 * js_sw_csum() computes checksum of nibbles in __u64
 */

static int js_sw_csum(__u64 t)
{
	char sum = 0;
	while (t) {
		sum += t & 0xf;
		t >>= 4;
	}
	return sum & 0xf;
}

/*
 * js_sw_read() reads and analyzes SideWinder joystick data.
 */

static int js_sw_read(void *xinfo, int **axes, int **buttons)
{
	struct js_sw_info *info = xinfo;
	__u64 data;
	int hat, i;

	switch (info->mode) {

		case JS_SW_MODE_3DP:

			if (info->optimize) {
				i = js_sw_read_packet(info->io, -1, 22, JS_SW_EXT_STROBE, &data);
			} else {
				i = js_sw_read_packet(info->io, 64, 66, JS_SW_EXT_STROBE, &data);
				if (i == 198) info->optimize = 1;
			}

			if (i < 60) {
				js_sw_init_digital(info->io);
				info->optimize = 0;
				return -1;
			}

			if (((data & 0x8080808080808080ULL) ^ 0x80) || js_sw_csum(data) ||
				(hat = ((data >> 3) & 0x08) | ((data >> 60) & 0x07)) > 8) {
				info->optimize = 0;
				return -1;
			}
			axes[0][0] = ((data <<  4) & 0x380) | ((data >> 16) & 0x07f);
			axes[0][1] = ((data <<  7) & 0x380) | ((data >> 24) & 0x07f);
			axes[0][2] = ((data >> 28) & 0x180) | ((data >> 40) & 0x07f);
			axes[0][3] = ((data >> 25) & 0x380) | ((data >> 48) & 0x07f);
			axes[0][4] = js_sw_hat_to_axis[hat].x;
			axes[0][5] = js_sw_hat_to_axis[hat].y;
			buttons[0][0] = ((~data >> 31) & 0x80) | ((~data >> 8) & 0x7f);

			return 0;

		case JS_SW_MODE_PP:

			if (js_sw_read_packet(info->io, 48, 16, JS_SW_EXT_STROBE, &data) != 48) return -1;
			if (!js_sw_parity(data) || (hat = (data >> 42) & 0xf) > 8) return -1;

			axes[0][0] = (data >>  9) & 0x3ff;
			axes[0][1] = (data >> 19) & 0x3ff;
			axes[0][2] = (data >> 29) & 0x07f;
			axes[0][3] = (data >> 36) & 0x03f;
			axes[0][4] = js_sw_hat_to_axis[hat].x;
			axes[0][5] = js_sw_hat_to_axis[hat].y;
			buttons[0][0] = ~data & 0x1ff;

			return 0;

		case JS_SW_MODE_GP:

			if (js_sw_read_packet(info->io, 15 * info->number, 5 * info->number,
				JS_SW_EXT_STROBE, &data) != 15 * info->number) return -1;
			if (js_sw_parity(data)) return -1;

			for (i = 0; i < info->number; i++) {
				axes[i][0] = ((data >> 3) & 1) - ((data >> 2) & 1);
				axes[i][1] = ( data       & 1) - ((data >> 1) & 1);
				buttons[i][0] = (~data >> 4) & 0x3ff;
				data >>= 15;
			}

			return 0;

		default:
			return -1;
	}
}

/*
 * js_sw_open() is a callback from the file open routine.
 */

static int js_sw_open(struct js_dev *jd)
{
	MOD_INC_USE_COUNT;
	return 0;
}

/*
 * js_sw_close() is a callback from the file release routine.
 */

static int js_sw_close(struct js_dev *jd)
{
	MOD_DEC_USE_COUNT;
	return 0;
}

/*
 * js_sw_init_corr() initializes the correction values for
 * SideWinders.
 */

static void __init js_sw_init_corr(int num_axes, int mode, int number, struct js_corr **corr)
{
	int i, j;

	for (i = 0; i < number; i++) {

		for (j = 0; j < num_axes; j++) {
			corr[i][j].type = JS_CORR_BROKEN;
			corr[i][j].prec = 8;
			corr[i][j].coef[0] = 511 - 32;
			corr[i][j].coef[1] = 512 + 32;
			corr[i][j].coef[2] = (1 << 29) / (511 - 32);
			corr[i][j].coef[3] = (1 << 29) / (511 - 32);
		}

		switch (mode) {

			case JS_SW_MODE_3DP:

				corr[i][2].type = JS_CORR_BROKEN;
				corr[i][2].prec = 4;
				corr[i][2].coef[0] = 255 - 16;
				corr[i][2].coef[1] = 256 + 16;
				corr[i][2].coef[2] = (1 << 29) / (255 - 16);
				corr[i][2].coef[3] = (1 << 29) / (255 - 16);

				j = 4;

			break;

			case JS_SW_MODE_PP:

				corr[i][2].type = JS_CORR_BROKEN;
				corr[i][2].prec = 1;
				corr[i][2].coef[0] = 63 - 4;
				corr[i][2].coef[1] = 64 + 4;
				corr[i][2].coef[2] = (1 << 29) / (63 - 4);
				corr[i][2].coef[3] = (1 << 29) / (63 - 4);

				corr[i][3].type = JS_CORR_BROKEN;
				corr[i][3].prec = 0;
				corr[i][3].coef[0] = 31 - 2;
				corr[i][3].coef[1] = 32 + 2;
				corr[i][3].coef[2] = (1 << 29) / (31 - 2);
				corr[i][3].coef[3] = (1 << 29) / (31 - 2);

				j = 4;

			break;

			default:

				j = 0;

		}

		for (; j < num_axes; j++) {
			corr[i][j].type = JS_CORR_BROKEN;
			corr[i][j].prec = 0;
			corr[i][j].coef[0] = 0;
			corr[i][j].coef[1] = 0;
			corr[i][j].coef[2] = (1 << 29);
			corr[i][j].coef[3] = (1 << 29);
		}
	}
}

/*
 * js_sw_probe() probes for SideWinder type joysticks.
 */

static struct js_port __init *js_sw_probe(int io, struct js_port *port)
{
	struct js_sw_info info;
	char *name;
	int i, j, axes, buttons;
	__u64 data;
	unsigned char u;


	if (check_region(io, 1)) return port;
	if (((u = inb(io)) & 3) == 3) return port;
	outb(0xff,io);
	if (!((inb(io) ^ u) & ~u & 0xf)) return port;

	i = js_sw_read_packet(io, JS_SW_MAX_LENGTH, -1, JS_SW_EXT_STROBE, &data);

	if (!i) {
		udelay(JS_SW_MIN_TIME);
		js_sw_init_digital(io);
		udelay(JS_SW_MAX_TIME);
		i = js_sw_read_packet(io, JS_SW_MAX_LENGTH, -1, JS_SW_EXT_STROBE, &data);
	}

	switch (i) {
		case 0:
			return port;
		case 5:
		case 10:
		case 15:
		case 20:
		case 30:
		case 45:
		case 60:
			info.mode = JS_SW_MODE_GP;
			outb(0xff,io);							/* Kick into 3-bit mode */
			udelay(JS_SW_MAX_TIME);
			i = js_sw_read_packet(io, 60, -1, JS_SW_EXT_STROBE, &data);	/* Get total length */
			udelay(JS_SW_MIN_TIME);
			j = js_sw_read_packet(io, 15, -1, JS_SW_MIN_STROBE, &data);	/* Get subpacket length */
			if (!i || !j) {
				printk(KERN_WARNING "joy-sidewinder: SideWinder GamePad detected (%d,%d),"
							" but not idenfitied.\n", i, j);
				return port;
			}
			info.number = i / j;
			axes = 2; buttons = 10; name = "SideWinder GamePad";
			break;
		case 16:
		case 48:
			info.mode = JS_SW_MODE_PP; info.number = 1;
			axes = 6; buttons = 9; name = "SideWinder Precision Pro";
			break;
		case 64:
		case 66:
			info.mode = JS_SW_MODE_3DP; info.number = 1; info.optimize = 0;
			axes = 6; buttons = 8; name = "SideWinder 3D Pro";
			break;
		case 72:
			return port;
		default:
			printk(KERN_WARNING "joy-sidewinder: unknown joystick device detected "
				"(io=%#x, count=%d, data=0x%08x%08x), contact <vojtech@ucw.cz>\n",
				io, i, (int)(data >> 32), (int)(data & 0xffffffff));
			return port;
	}

	info.io = io;

	request_region(io, 1, "joystick (sidewinder)");
	port = js_register_port(port, &info, info.number, sizeof(struct js_sw_info), js_sw_read);
	for (i = 0; i < info.number; i++)
		printk(KERN_INFO "js%d: %s at %#x\n",
			js_register_device(port, i, axes, buttons, name, js_sw_open, js_sw_close), name, io);
	js_sw_init_corr(axes, info.mode, info.number, port->corr);

	return port;
}

#ifdef MODULE
int init_module(void)
#else
int __init js_sw_init(void)
#endif
{
	int *p;

	for (p = js_sw_port_list; *p; p++) js_sw_port = js_sw_probe(*p, js_sw_port);
	if (js_sw_port) return 0;

#ifdef MODULE
	printk(KERN_WARNING "joy-sidewinder: no joysticks found\n");
#endif

	return -ENODEV;
}

#ifdef MODULE
void cleanup_module(void)
{
	int i;
	struct js_sw_info *info;

	while (js_sw_port != NULL) {
		for (i = 0; i < js_sw_port->ndevs; i++)
			if (js_sw_port->devs[i] != NULL)
				js_unregister_device(js_sw_port->devs[i]);
		info = js_sw_port->info;
		release_region(info->io, 1);
		js_sw_port = js_unregister_port(js_sw_port);
	}

}
#endif
