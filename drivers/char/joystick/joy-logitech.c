/*
 *  joy-logitech.c  Version 1.2
 *
 *  Copyright (c) 1998 Vojtech Pavlik
 */

/*
 * This is a module for the Linux joystick driver, supporting
 * Logitech Digital joystick family.
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

#define JS_LT_MAX_START		250
#define JS_LT_MAX_STROBE	25
#define JS_LT_MAX_LENGTH	72

#define JS_LT_MAX_DELAY		12000

#define JS_LT_MODE_WMED		1
#define JS_LT_MODE_CM2		2
#define JS_LT_MODE_TPD		3

static int js_lt_seq_init[] __initdata = { 6000, 11000, 7000, 9000, 6000, 11000, 7000, 9000, 0 };
static int js_lt_seq_reset[] __initdata = { 2000, 3000, 2000, 3000, 0 };

static int js_lt_port_list[] __initdata = {0x201, 0};
static struct js_port* js_lt_port __initdata = NULL;

static struct {
	int x;
	int y;
} js_lt_hat_to_axis[] = {{ 0, 0}, { 0,-1}, { 1,-1}, { 1, 0}, { 1, 1}, { 0, 1}, {-1, 1}, {-1, 0}, {-1,-1}};

struct js_lt_info {
	int io;
	unsigned char mode;
};

/*
 * js_lt_read_packet() reads a Logitech packet.
 */

static int js_lt_read_packet(int io, __u64 *data)
{

	static unsigned char buf[JS_LT_MAX_LENGTH];
	unsigned char u, v, w, mask = 0;
	int i;
	unsigned long flags;
	unsigned int t, t1;

	int start = (js_time_speed * JS_LT_MAX_START) >> 10;
	int strobe = (js_time_speed * JS_LT_MAX_STROBE) >> 10;

	u = inb(io) >> 4;

	if (u == 0xc) mask = 0x10;
	if (u == 0x0) mask = 0x50;
	if (!mask) return 0;

	i = 0;

	__save_flags(flags);
	__cli();

	outb(0xff,io);

	u = inb(io);
	t = js_get_time();

	if ((u & 0xc) != 0xc) mask = 0x10;

	do {
		u = inb(io);
		t1 = js_get_time();
	} while ((((u >> 1) ^ u) & mask) != mask && js_delta(t1,t) < start);

	t = t1;

	do {
		v = inb(io);
		t1 = js_get_time();
		w = u ^ v;
		if ((((w >> 1) ^ w) & mask) == mask) {
			buf[i++] = w;
			t = t1;
			u = v;
		}
	} while (i < JS_LT_MAX_LENGTH && js_delta(t1,t) < strobe);

	__restore_flags(flags);

	t = i;
	*data = 0;

	if (mask == 0x10) {
		for (i = 0; i < t; i++)
			*data = ((buf[i] >> 5) & 1) | (*data << 1);
		return t;
	}
	if (mask == 0x50) {
		for (i = 0; i < t; i++)
			*data = ((__u64)(buf[i] & 0x20) << (t - 5)) | (buf[i] >> 7) | (*data << 1);
		return t << 1;
	}
	return 0;
}

/*
 * js_lt_reverse() reverses the order of bits in a byte.
 */

static unsigned char js_lt_reverse(unsigned char u)
{
	u = ((u & 0x0f) << 4) | ((u >> 4) & 0x0f);
	u = ((u & 0x33) << 2) | ((u >> 2) & 0x33);
	u = ((u & 0x55) << 1) | ((u >> 1) & 0x55);
	return u;
}

/*
 * js_lt_read() reads and analyzes Logitech joystick data.
 */

static int js_lt_read(void *xinfo, int **axes, int **buttons)
{
	struct js_lt_info *info = xinfo;
	__u64 data;
	int hat;

	switch (info->mode) {

		case JS_LT_MODE_TPD:

			if (js_lt_read_packet(info->io, &data) != 20) return -1;

			axes[0][0] = ((data >> 6) & 1) - ((data >> 4) & 1);
			axes[0][1] = ((data >> 5) & 1) - ((data >> 7) & 1);

			buttons[0][0] = js_lt_reverse((data & 0x0f) | ((data >> 4) & 0xf0));

			return 0;

		case JS_LT_MODE_WMED:

			if (js_lt_read_packet(info->io, &data) != 42) return -1;
			if ((hat = data & 0xf) > 8) return -1;

			axes[0][0] = (data >> 26) & 0xff;
			axes[0][1] = (data >> 18) & 0xff;
			axes[0][2] = (data >> 10) & 0xff;
			axes[0][3] = js_lt_hat_to_axis[hat].x;
			axes[0][4] = js_lt_hat_to_axis[hat].y;

			buttons[0][0] = js_lt_reverse((data >> 2) & 0xfc);

			return 0;

		case JS_LT_MODE_CM2:

			if (js_lt_read_packet(info->io, &data) != 64) return -1;

			axes[0][0] = (data >> 48) & 0xff;
			axes[0][1] = (data >> 40) & 0xff;
			axes[0][2] = (data >> 32) & 0xff;
			axes[0][3] = (data >> 24) & 0xff;
			axes[0][4] = (data >> 16) & 0xff;
			axes[0][5] = (data >>  8) & 0xff;

			buttons[0][0] = js_lt_reverse(data & 0xff);

			return 0;
	}

	return -1;
}

/*
 * js_lt_open() is a callback from the file open routine.
 */

static int js_lt_open(struct js_dev *jd)
{
	MOD_INC_USE_COUNT;
	return 0;
}

/*
 * js_lt_close() is a callback from the file release routine.
 */

static int js_lt_close(struct js_dev *jd)
{
	MOD_DEC_USE_COUNT;
	return 0;
}

/*
 * js_lt_trigger_sequence() sends a trigger & delay sequence
 * to reset/initialize a Logitech joystick.
 */

static void __init js_lt_trigger_sequence(int io, int *seq)
{
	while (*seq) {
		outb(0xff,io);
		udelay(*seq++);
	}
}

/*
 * js_lt_init_corr() initializes the correction values for
 * Logitech joysticks.
 */

static void __init js_lt_init_corr(int num_axes, int mode, int **axes, struct js_corr **corr)
{
	int j;

	for (j = 0; j < num_axes; j++) {
		corr[0][j].type = JS_CORR_BROKEN;
		corr[0][j].prec = 2;
		corr[0][j].coef[0] = axes[0][j] - 8;
		corr[0][j].coef[1] = axes[0][j] + 8;
		corr[0][j].coef[2] = (1 << 29) / (127 - 32);
		corr[0][j].coef[3] = (1 << 29) / (127 - 32);
	}

	switch (mode) {
		case JS_LT_MODE_TPD:  j = 0; break;
		case JS_LT_MODE_WMED: j = 3; break;
		case JS_LT_MODE_CM2:  j = 6; break;
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
 * js_lt_probe() probes for Logitech type joysticks.
 */

static struct js_port __init *js_lt_probe(int io, struct js_port *port)
{
	struct js_lt_info info;
	char *name;
	int axes, buttons, i;
	__u64 data;
	unsigned char u;

	if (check_region(io, 1)) return port;

	if (((u = inb(io)) & 3) == 3) return port;
	outb(0xff,io);
	if (!((inb(io) ^ u) & ~u & 0xf)) return port;

	if (!(i = js_lt_read_packet(io, &data))) {
		udelay(JS_LT_MAX_DELAY);
		js_lt_trigger_sequence(io, js_lt_seq_reset);
		js_lt_trigger_sequence(io, js_lt_seq_init);
		i = js_lt_read_packet(io, &data);
	}

	switch (i) {
		case 0:
			return port;
		case 20:
			info.mode = JS_LT_MODE_TPD;
			axes = 2; buttons = 8; name = "Logitech ThunderPad Digital";
			break;
		case 42:
			info.mode = JS_LT_MODE_WMED;
			axes = 5; buttons = 6; name = "Logitech WingMan Extreme Digital";
			break;
		case 64:
			info.mode = JS_LT_MODE_CM2;
			axes = 6; buttons = 8; name = "Logitech CyberMan 2";
			break;
		case 72:
		case 144:
			return port;
		default:
			printk(KERN_WARNING "joy-logitech: unknown joystick device detected "
				"(io=%#x, count=%d, data=0x%08x%08x), contact <vojtech@ucw.cz>\n",
				io, i, (int)(data >> 32), (int)(data & 0xffffffff));
			return port;
	}

	info.io = io;

	request_region(io, 1, "joystick (logitech)");
	port = js_register_port(port, &info, 1, sizeof(struct js_lt_info), js_lt_read);
	printk(KERN_INFO "js%d: %s at %#x\n",
		js_register_device(port, 0, axes, buttons, name, js_lt_open, js_lt_close), name, io);

	udelay(JS_LT_MAX_DELAY);

	js_lt_read(port->info, port->axes, port->buttons);
	js_lt_init_corr(axes, info.mode, port->axes, port->corr);

	return port;
}

#ifdef MODULE
int init_module(void)
#else
int __init js_lt_init(void)
#endif
{
	int *p;

	for (p = js_lt_port_list; *p; p++) js_lt_port = js_lt_probe(*p, js_lt_port);
	if (js_lt_port) return 0;

#ifdef MODULE
	printk(KERN_WARNING "joy-logitech: no joysticks found\n");
#endif

	return -ENODEV;
}

#ifdef MODULE
void cleanup_module(void)
{
	struct js_lt_info *info;

	while (js_lt_port != NULL) {
		js_unregister_device(js_lt_port->devs[0]);
		info = js_lt_port->info;
		release_region(info->io, 1);
		js_lt_port = js_unregister_port(js_lt_port);
	}
}
#endif
