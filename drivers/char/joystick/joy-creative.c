/*
 *  joy-creative.c  Version 1.2
 *
 *  Copyright (c) 1999 Vojtech Pavlik
 *
 *  Sponsored by SuSE
 */

/*
 * This is a module for the Linux joystick driver, supporting
 * Creative Labs Blaster gamepad family.
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
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/joystick.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/init.h>

#define JS_CR_MAX_STROBE	100	/* 100 us max wait for first strobe */
#define JS_CR_LENGTH		36

#define JS_CR_MODE_BGPC		8

static int js_cr_port_list[] __initdata = {0x201, 0};
static struct js_port* js_cr_port __initdata = NULL;

struct js_cr_info {
	int io;
	unsigned char mode[2];
};

/*
 * js_cr_read_packet() reads a Blaster gamepad packet.
 */

static int js_cr_read_packet(int io, unsigned int *data)
{
	unsigned long flags;
	unsigned char u, v, w;
	__u64 buf[2];
	int r[2], t[2], p[2];
	int i, j, ret;

	for (i = 0; i < 2; i++); {
		r[i] = buf[i] = 0;
		p[i] = t[i] = JS_CR_MAX_STROBE;
		p[i] += JS_CR_MAX_STROBE;
	}
	
	__save_flags(flags);
	__cli();

	u = inb(io);

	do {
		t[0]--; t[1]--;
		v = inb(io);
		for (i = 0, w = u ^ v; i < 2 && w; i++, w >>= 2)
			if (w & 0x30) {
				if ((w & 0x30) < 0x30 && r[i] < JS_CR_LENGTH && t[i] > 0) {
					buf[i] |= (__u64)((w >> 5) & 1) << r[i]++;
					p[i] = t[i] = (p[i] - t[i]) << 1;
					u = v;
				} else t[i] = 0;
			}
	} while (t[0] > 0 || t[1] > 0);

	__restore_flags(flags);


	ret = 0;

	for (i = 0; i < 2; i++) {

		if (r[i] != JS_CR_LENGTH) continue;

		for (j = 0; j < JS_CR_LENGTH && (buf[i] & 0x04104107f) ^ 0x041041040; j++)
			buf[i] = (buf[i] >> 1) | ((__u64)(buf[i] & 1) << (JS_CR_LENGTH - 1));

		if (j < JS_CR_LENGTH) ret |= (1 << i);

		data[i] = ((buf[i] >>  7) & 0x000001f) | ((buf[i] >>  8) & 0x00003e0)
			| ((buf[i] >>  9) & 0x0007c00) | ((buf[i] >> 10) & 0x00f8000)
			| ((buf[i] >> 11) & 0x1f00000);

	}

	return ret;
}

/*
 * js_cr_read() reads and analyzes Blaster gamepad data.
 */

static int js_cr_read(void *xinfo, int **axes, int **buttons)
{
	struct js_cr_info *info = xinfo;
	unsigned int data[2];
	int i, r;

	if (!(r = js_cr_read_packet(info->io, data)))
		return -1;

	for (i = 0; i < 2; i++)
		if (r & (1 << i)) {
			switch (info->mode[i]) {

				case JS_CR_MODE_BGPC:

					axes[i][0] = ((data[i] >> 4) & 1) - ((data[i] >> 3) & 1);
					axes[i][1] = ((data[i] >> 2) & 1) - ((data[i] >> 1) & 1);

					buttons[i][0] = ((data[i] >> 12) & 0x007) | ((data[i] >> 6) & 0x038)
						      | ((data[i] >>  1) & 0x0c0) | ((data[i] >> 7) & 0x300)
						      | ((data[i] <<  5) & 0xc00);

					break;

				default:
					break;

			}
		}

	return 0;
}

/*
 * js_cr_open() is a callback from the file open routine.
 */

static int js_cr_open(struct js_dev *jd)
{
	MOD_INC_USE_COUNT;
	return 0;
}

/*
 * js_cr_close() is a callback from the file release routine.
 */

static int js_cr_close(struct js_dev *jd)
{
	MOD_DEC_USE_COUNT;
	return 0;
}

/*
 * js_cr_init_corr() initializes correction values of
 * Blaster gamepads.
 */

static void __init js_cr_init_corr(int mode, struct js_corr *corr)
{
	int i;

	switch (mode) {

		case JS_CR_MODE_BGPC:

			for (i = 0; i < 2; i++) {
				corr[i].type = JS_CORR_BROKEN;
				corr[i].prec = 0;
				corr[i].coef[0] = 0;
				corr[i].coef[1] = 0;
				corr[i].coef[2] = (1 << 29);
				corr[i].coef[3] = (1 << 29);
			}

			break;

	}
}

/*
 * js_cr_probe() probes for Blaster gamepads.
 */

static struct js_port __init *js_cr_probe(int io, struct js_port *port)
{
	struct js_cr_info info;
	char *names[] = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, "Blaster GamePad Cobra" };
	char axes[] = { 0, 0, 0, 0, 0, 0, 0, 0, 2 };
	char buttons[] = { 0, 0, 0, 0, 0, 0, 0, 0, 12 };
	unsigned int data[2];
	int i, r;

	if (check_region(io, 1)) return port;

	info.mode[0] = info.mode[1] = 0;

	if (!(r = js_cr_read_packet(io, data)))
		return port;

	for (i = 0; i < 2; i++) {
		if (r & (1 << i)) {
			if (~data[i] & 1) {
				info.mode[i] = JS_CR_MODE_BGPC;
			} else {
				info.mode[i] = (data[i] >> 2) & 7;
			}
			if (!names[info.mode[i]]) {
				printk(KERN_WARNING "joy-creative: Unknown Creative device %d at %#x\n",
					info.mode[i], io);
				info.mode[i] = 0;
			}
		}
	}

	if (!info.mode[0] && !info.mode[1]) return port;

	info.io = io;

	request_region(io, 1, "joystick (creative)");
	port = js_register_port(port, &info, 2, sizeof(struct js_cr_info), js_cr_read);

	for (i = 0; i < 2; i++)
		if (info.mode[i]) {
			printk(KERN_INFO "js%d: %s at %#x\n",
				js_register_device(port, i, axes[info.mode[i]], buttons[info.mode[i]],
					names[info.mode[i]], js_cr_open, js_cr_close),
				names[info.mode[i]], io);
			js_cr_init_corr(info.mode[i], port->corr[i]);
		}

	return port;
}

#ifdef MODULE
int init_module(void)
#else
int __init js_cr_init(void)
#endif
{
	int *p;

	for (p = js_cr_port_list; *p; p++) js_cr_port = js_cr_probe(*p, js_cr_port);
	if (js_cr_port) return 0;

#ifdef MODULE
	printk(KERN_WARNING "joy-creative: no joysticks found\n");
#endif

	return -ENODEV;
}

#ifdef MODULE
void cleanup_module(void)
{
	int i;
	struct js_cr_info *info;

	while (js_cr_port) {
		for (i = 0; i < js_cr_port->ndevs; i++)
			if (js_cr_port->devs[i])
				js_unregister_device(js_cr_port->devs[i]);
		info = js_cr_port->info;
		release_region(info->io, 1);
		js_cr_port = js_unregister_port(js_cr_port);
	}
}
#endif
