/*
 *  joy-gravis.c  Version 1.2
 *
 *  Copyright (c) 1998-1999 Vojtech Pavlik
 *
 *  Sponsored by SuSE
 */

/*
 * This is a module for the Linux joystick driver, supporting
 * Gravis GrIP digital joystick family.
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

#define JS_GR_MODE_GPP		1
#define JS_GR_LENGTH_GPP	24
#define JS_GR_STROBE_GPP	400

#define JS_GR_MODE_XT		2
#define JS_GR_MODE_BD		3
#define JS_GR_LENGTH_XT		4
#define JS_GR_STROBE_XT		200
#define JS_GR_MAX_CHUNKS_XT	10	
#define JS_GR_MAX_BITS_XT	30	

static int js_gr_port_list[] __initdata = {0x201, 0};
static struct js_port* js_gr_port __initdata = NULL;

struct js_gr_info {
	int io;
	unsigned char mode[2];
};

/*
 * js_gr_gpp_read_packet() reads a Gravis GamePad Pro packet.
 */

static int js_gr_gpp_read_packet(int io, int shift, unsigned int *data)
{
	unsigned long flags;
	unsigned char u, v;
	unsigned int t, p;
	int i;

	i = 0;
	data[0] = 0;
	p = t = JS_GR_STROBE_GPP;
	p += JS_GR_STROBE_GPP;

	__save_flags(flags);
	__cli();

	v = inb(io) >> shift;

	do {
		t--;
		u = v; v = (inb(io) >> shift) & 3;
		if (~v & u & 1) {
			data[0] |= (v >> 1) << i++;
			p = t = (p - t) << 1;
		}
	} while (i < JS_GR_LENGTH_GPP && t > 0);

	__restore_flags(flags);

	if (i < JS_GR_LENGTH_GPP) return -1;

	for (i = 0; i < JS_GR_LENGTH_GPP && (data[0] & 0xfe4210) ^ 0x7c0000; i++)
		data[0] = data[0] >> 1 | (data[0] & 1) << (JS_GR_LENGTH_GPP - 1);

	return -(i == JS_GR_LENGTH_GPP);
}

/*
 * js_gr_xt_read_packet() reads a Gravis Xterminator packet.
 */

static int js_gr_xt_read_packet(int io, int shift, unsigned int *data)
{
	unsigned int i, j, buf, crc;
	unsigned char u, v, w;
	unsigned long flags;
	unsigned int t, p;
	char status;

	data[0] = data[1] = data[2] = data[3] = 0;
	status = buf = i = j = 0;
	p = t = JS_GR_STROBE_XT;
	p += JS_GR_STROBE_XT;

	__save_flags(flags);
	__cli();

	v = w = (inb(io) >> shift) & 3;

	do {
		t--;
		u = (inb(io) >> shift) & 3;

		if (u ^ v) {

			if ((u ^ v) & 1) {
				p = t = (p - t) << 2;
				buf = (buf << 1) | (u >> 1);
				i++;
			} else 

			if ((((u ^ v) & (v ^ w)) >> 1) & ~(u | v | w) & 1) {
				p = t = (p - t) << 2;
				if (i == 20) {
					crc = buf ^ (buf >> 7) ^ (buf >> 14);
					if (!((crc ^ (0x25cb9e70 >> ((crc >> 2) & 0x1c))) & 0xf)) {
						data[buf >> 18] = buf >> 4;
						status |= 1 << (buf >> 18);
					}
					j++;
				}
				buf = 0;
				i = 0;
			}

			w = v;
			v = u;
		}

	} while (status != 0xf && i < JS_GR_MAX_BITS_XT && j < JS_GR_MAX_CHUNKS_XT && t > 0);

	__restore_flags(flags);

	return -(status != 0xf);
}

/*
 * js_gr_read() reads and analyzes GrIP joystick data.
 */

static int js_gr_read(void *xinfo, int **axes, int **buttons)
{
	struct js_gr_info *info = xinfo;
	unsigned int data[JS_GR_LENGTH_XT];
	int i;

	for (i = 0; i < 2; i++)
		switch (info->mode[i]) {

			case JS_GR_MODE_GPP:

				if (js_gr_gpp_read_packet(info->io, (i << 1) + 4, data)) return -1;

				axes[i][0] = ((data[0] >> 15) & 1) - ((data[0] >> 16) & 1);
				axes[i][1] = ((data[0] >> 13) & 1) - ((data[0] >> 12) & 1);

				data[0] = ((data[0] >> 6) & 0x37) | (data[0] & 0x08) | ((data[0] << 1) & 0x40) |
				       ((data[0] << 5) & 0x80) | ((data[0] << 8) & 0x300);

				buttons[i][0] = (data[0] & 0xfc) | ((data[0] >> 1) & 0x101) | ((data[0] << 1) & 0x202);

				break;

			case JS_GR_MODE_XT:

				if (js_gr_xt_read_packet(info->io, (i << 1) + 4, data)) return -1;

				axes[i][0] =       (data[0] >> 2) & 0x3f;
				axes[i][1] = 63 - ((data[0] >> 8) & 0x3f);
				axes[i][2] =       (data[1] >> 2) & 0x3f;
				axes[i][3] =       (data[1] >> 8) & 0x3f;
				axes[i][4] =       (data[2] >> 8) & 0x3f;

				axes[i][5] = ((data[2] >> 1) & 1) - ( data[2]       & 1);
				axes[i][6] = ((data[2] >> 2) & 1) - ((data[2] >> 3) & 1);
				axes[i][7] = ((data[2] >> 5) & 1) - ((data[2] >> 4) & 1);
				axes[i][8] = ((data[2] >> 6) & 1) - ((data[2] >> 7) & 1);

				buttons[i][0] = (data[3] >> 3) & 0x7ff;

				break;

			case JS_GR_MODE_BD:

				if (js_gr_xt_read_packet(info->io, (i << 1) + 4, data)) return -1;

				axes[i][0] =       (data[0] >> 2) & 0x3f;
				axes[i][1] = 63 - ((data[0] >> 8) & 0x3f);
				axes[i][2] =       (data[2] >> 8) & 0x3f;

				axes[i][3] = ((data[2] >> 1) & 1) - ( data[2]       & 1);
				axes[i][4] = ((data[2] >> 2) & 1) - ((data[2] >> 3) & 1);

				buttons[i][0] = ((data[3] >> 6) & 0x01) | ((data[3] >> 3) & 0x06)
					      | ((data[3] >> 4) & 0x18);

				break;

			default:
				break;

		}


	return 0;
}

/*
 * js_gr_open() is a callback from the file open routine.
 */

static int js_gr_open(struct js_dev *jd)
{
	MOD_INC_USE_COUNT;
	return 0;
}

/*
 * js_gr_close() is a callback from the file release routine.
 */

static int js_gr_close(struct js_dev *jd)
{
	MOD_DEC_USE_COUNT;
	return 0;
}

/*
 * js_gr_init_corr() initializes correction values of
 * GrIP joysticks.
 */

static void __init js_gr_init_corr(int mode, struct js_corr *corr)
{
	int i;

	switch (mode) {

		case JS_GR_MODE_GPP:

			for (i = 0; i < 2; i++) {
				corr[i].type = JS_CORR_BROKEN;
				corr[i].prec = 0;
				corr[i].coef[0] = 0;
				corr[i].coef[1] = 0;
				corr[i].coef[2] = (1 << 29);
				corr[i].coef[3] = (1 << 29);
			}

			break;

		case JS_GR_MODE_XT:

			for (i = 0; i < 5; i++) {
				corr[i].type = JS_CORR_BROKEN;
				corr[i].prec = 0;
				corr[i].coef[0] = 31 - 4;
				corr[i].coef[1] = 32 + 4;
				corr[i].coef[2] = (1 << 29) / (32 - 14);
				corr[i].coef[3] = (1 << 29) / (32 - 14);
			}

			for (i = 5; i < 9; i++) {
				corr[i].type = JS_CORR_BROKEN;
				corr[i].prec = 0;
				corr[i].coef[0] = 0;
				corr[i].coef[1] = 0;
				corr[i].coef[2] = (1 << 29);
				corr[i].coef[3] = (1 << 29);
			}

			break;

		case JS_GR_MODE_BD:

			for (i = 0; i < 3; i++) {
				corr[i].type = JS_CORR_BROKEN;
				corr[i].prec = 0;
				corr[i].coef[0] = 31 - 4;
				corr[i].coef[1] = 32 + 4;
				corr[i].coef[2] = (1 << 29) / (32 - 14);
				corr[i].coef[3] = (1 << 29) / (32 - 14);
			}

			for (i = 3; i < 5; i++) {
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
 * js_gr_probe() probes for GrIP joysticks.
 */

static struct js_port __init *js_gr_probe(int io, struct js_port *port)
{
	struct js_gr_info info;
	char *names[] = { NULL, "Gravis GamePad Pro", "Gravis Xterminator", "Gravis Blackhawk Digital"};
	char axes[] = { 0, 2, 9, 5};
	char buttons[] = { 0, 10, 11, 5};
	unsigned int data[JS_GR_LENGTH_XT];
	int i;

	if (check_region(io, 1)) return port;

	info.mode[0] = info.mode[1] = 0;

	for (i = 0; i < 2; i++) {
		if (!js_gr_gpp_read_packet(io, (i << 1) + 4, data)) info.mode[i] = JS_GR_MODE_GPP;
		if (!js_gr_xt_read_packet(io, (i << 1) + 4, data)) {
			if ((data[3] & 7) == 7)
				info.mode[i] = JS_GR_MODE_XT;
			if ((data[3] & 7) == 0)
				info.mode[i] = JS_GR_MODE_BD;
		}
	}

	if (!info.mode[0] && !info.mode[1]) return port;

	info.io = io;

	request_region(io, 1, "joystick (gravis)");
	port = js_register_port(port, &info, 2, sizeof(struct js_gr_info), js_gr_read);

	for (i = 0; i < 2; i++)
		if (info.mode[i]) {
			printk(KERN_INFO "js%d: %s at %#x\n",
				js_register_device(port, i, axes[info.mode[i]], buttons[info.mode[i]],
					names[info.mode[i]], js_gr_open, js_gr_close),
				names[info.mode[i]], io);
			js_gr_init_corr(info.mode[i], port->corr[i]);
		}

	return port;
}

#ifdef MODULE
int init_module(void)
#else
int __init js_gr_init(void)
#endif
{
	int *p;

	for (p = js_gr_port_list; *p; p++) js_gr_port = js_gr_probe(*p, js_gr_port);
	if (js_gr_port) return 0;

#ifdef MODULE
	printk(KERN_WARNING "joy-gravis: no joysticks found\n");
#endif

	return -ENODEV;
}

#ifdef MODULE
void cleanup_module(void)
{
	int i;
	struct js_gr_info *info;

	while (js_gr_port) {
		for (i = 0; i < js_gr_port->ndevs; i++)
			if (js_gr_port->devs[i])
				js_unregister_device(js_gr_port->devs[i]);
		info = js_gr_port->info;
		release_region(info->io, 1);
		js_gr_port = js_unregister_port(js_gr_port);
	}
}
#endif
