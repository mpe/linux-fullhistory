/*
 *  joy-logitech.c  Version 1.2
 *
 *  Copyright (c) 1998-1999 Vojtech Pavlik
 *
 *  Sponsored by SuSE
 */

/*
 * This is a module for the Linux joystick driver, supporting
 * Logitech ADI joystick family.
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
#include <linux/malloc.h>
#include <linux/init.h>

/*
 * Times array sizes, flags, ids.
 */

#undef JS_LT_DEBUG

#define JS_LT_MAX_START		400	/* Trigger to packet timeout [400us] */

#define JS_LT_MAX_LENGTH	256
#define JS_LT_MIN_LENGTH	8
#define JS_LT_MIN_LEN_LENGTH	10
#define JS_LT_MIN_ID_LENGTH	66
#define JS_LT_MAX_NAME_LENGTH	16

#define JS_LT_INIT_DELAY	10	/* Delay after init packet [10ms] */
#define JS_LT_DATA_DELAY	4	/* Delay after data packet [4ms] */

#define JS_LT_FLAG_HAT		0x04
#define JS_LT_FLAG_10BIT	0x08

#define JS_LT_ID_WMED		0x00
#define JS_LT_ID_TPD		0x01
#define JS_LT_ID_WMI		0x04
#define JS_LT_ID_WGP		0x06
#define JS_LT_ID_WM3D		0x07
#define JS_LT_ID_WGPE		0x08

#define JS_LT_BUG_BUTTONS	0x01
#define JS_LT_BUG_LONGID	0x02
#define JS_LT_BUG_LONGDATA	0x04
#define JS_LT_BUG_IGNTRIG	0x08

/*
 * Port probing variables.
 */

static int js_lt_port_list[] __initdata = { 0x201, 0 };
static struct js_port* js_lt_port __initdata = NULL;

/*
 * Device names.
 */

#define JS_LT_MAX_ID		10

static char *js_lt_names[] = {	"WingMan Extreme Digital", "ThunderPad Digital", "SideCar", "CyberMan 2",
				"WingMan Interceptor", "WingMan Formula", "WingMan GamePad", 
				"WingMan Extreme Digital 3D", "WingMan GamePad Extreme", 
				"WingMan GamePad USB", "Unknown Device %#x"};

/*
 * Hat to axis conversion arrays.
 */

static struct {
	int x;
	int y;
} js_lt_hat_to_axis[] = {{ 0, 0}, { 0,-1}, { 1,-1}, { 1, 0}, { 1, 1}, { 0, 1}, {-1, 1}, {-1, 0}, {-1,-1}};

/*
 * Per-port information.
 */

struct js_lt_info {
	int  io;
	int  length[2];
	int  ret[2];
	int  idx[2];
	unsigned char id[2];
	char buttons[2];
	char axes10[2];
	char axes8[2];
	char pad[2];
	char hats[2];
	char name[2][JS_LT_MAX_NAME_LENGTH];
	unsigned char data[2][JS_LT_MAX_LENGTH];
	char bugs[2];
};

/*
 * js_lt_read_packet() reads a Logitech ADI packet.
 */

static void js_lt_read_packet(struct js_lt_info *info)
{
	unsigned char u, v, w, x, z;
	int t[2], s[2], p[2], i;
	unsigned long flags;

	for (i = 0; i < 2; i++) {
		info->ret[i] = -1;
		p[i] = t[i] = JS_LT_MAX_START;
		s[i] = 0;
	}

	__save_flags(flags);
	__cli();

	outb(0xff, info->io);
	v = z = inb(info->io);

	do {
		u = v;
		w = u ^ (v = x = inb(info->io));
		for (i = 0; i < 2; i++, w >>= 2, x >>= 2) {
			t[i]--;
			if ((w & 0x30) && s[i]) {
				if ((w & 0x30) < 0x30 && info->ret[i] < JS_LT_MAX_LENGTH && t[i] > 0) {
					info->data[i][++info->ret[i]] = w;
					p[i] = t[i] = (p[i] - t[i]) << 1;
				} else t[i] = 0;
			} else if (!(x & 0x30)) s[i] = 1;
		}
	} while (t[0] > 0 || t[1] > 0);

	__restore_flags(flags);

	for (i = 0; i < 2; i++, z >>= 2)
		if ((z & 0x30) && info->ret[i] > 0)
			info->bugs[i] |= JS_LT_BUG_BUTTONS;

#ifdef JS_LT_DEBUG
	printk(KERN_DEBUG "joy-logitech: read %d %d bits\n", info->ret[0], info->ret[1]);
	printk(KERN_DEBUG "joy-logitech: stream0:");
	for (i = 0; i <= info->ret[0]; i++) printk("%d", (info->data[0][i] >> 5) & 1);
	printk("\n");
	printk(KERN_DEBUG "joy-logitech: stream1:");
	for (i = 0; i <= info->ret[1]; i++) printk("%d", (info->data[1][i] >> 5) & 1);
	printk("\n");
#endif

	return;
}

/*
 * js_lt_move_bits() detects a possible 2-stream mode, and moves
 * the bits accordingly. 
 */

static void js_lt_move_bits(struct js_lt_info *info, int length)
{
	int i;

 	info->idx[0] = info->idx[1] = 0;

	if (info->ret[0] <= 0 || info->ret[1] <= 0) return;
	if (info->data[0][0] & 0x20 || ~info->data[1][0] & 0x20) return;

	for (i = 1; i <= info->ret[1]; i++)
		info->data[0][((length - 1) >> 1) + i + 1] = info->data[1][i];

	info->ret[0] += info->ret[1];
	info->ret[1] = -1;
}

/*
 * js_lt_get_bits() gathers bits from the data packet.
 */

static inline int js_lt_get_bits(struct js_lt_info *info, int device, int count)
{
	int bits = 0;
	int i;
	if ((info->idx[device] += count) > info->ret[device]) return 0;
	for (i = 0; i < count; i++) bits |= ((info->data[device][info->idx[device] - i] >> 5) & 1) << i; 
	return bits;
}

/*
 * js_lt_read() reads and analyzes Logitech joystick data.
 */

static int js_lt_read(void *xinfo, int **axes, int **buttons)
{
	struct js_lt_info *info = xinfo;
	int i, j, k, l, t;
	int ret = 0;

	js_lt_read_packet(info);
	js_lt_move_bits(info, info->length[0]);

	for (i = 0; i < 2; i++) {

		if (!info->length[i]) continue;
		
		if (info->length[i] > info->ret[i] ||
		    info->id[i] != (js_lt_get_bits(info, i, 4) | (js_lt_get_bits(info, i, 4) << 4))) {
			ret = -1;
			continue;
		}

		if (info->length[i] < info->ret[i])
			info->bugs[i] |= JS_LT_BUG_LONGDATA;
			
		k = l = 0;

		for (j = 0; j < info->axes10[i]; j++) 
			axes[i][k++] = js_lt_get_bits(info, i, 10);

		for (j = 0; j < info->axes8[i]; j++) 
			axes[i][k++] = js_lt_get_bits(info, i, 8);

		for (j = 0; j <= (info->buttons[i] - 1) >> 5; j++) buttons[i][j] = 0;

		for (j = 0; j < info->buttons[i] && j < 63; j++) {
			if (j == info->pad[i]) {
				t = js_lt_get_bits(info, i, 4);
				axes[i][k++] = ((t >> 2) & 1) - ( t       & 1);
				axes[i][k++] = ((t >> 1) & 1) - ((t >> 3) & 1);
			}
			buttons[i][l >> 5] |= js_lt_get_bits(info, i, 1) << (l & 0x1f);
			l++;
		}

		for (j = 0; j < info->hats[i]; j++) {
			if((t = js_lt_get_bits(info, i, 4)) > 8) {
				if (t != 15) ret = -1; /* Hat press */
				t = 0;
			}
			axes[i][k++] = js_lt_hat_to_axis[t].x;
			axes[i][k++] = js_lt_hat_to_axis[t].y;
		}

		for (j = 63; j < info->buttons[i]; j++) {
			buttons[i][l >> 5] |= js_lt_get_bits(info, i, 1) << (l & 0x1f);
			l++;
		}
	}

	return ret;
}

/*
 * js_lt_init_digital() sends a trigger & delay sequence
 * to reset and initialize a Logitech joystick into digital mode.
 */

static void __init js_lt_init_digital(int io)
{
	int seq[] = { 3, 2, 3, 10, 6, 11, 7, 9, 11, 0 };
	int i;

	for (i = 0; seq[i]; i++) {
		outb(0xff,io);
		mdelay(seq[i]);
	}
}

/*
 * js_lt_init_corr() initializes the correction values for
 * Logitech joysticks.
 */

static void __init js_lt_init_corr(int id, int naxes10, int naxes8, int naxes1, int *axes, struct js_corr *corr)
{
	int j;
	
	if (id == JS_LT_ID_WMED) axes[2] = 128;	/* Throttle fixup */
	if (id == JS_LT_ID_WMI)  axes[2] = 512;
	if (id == JS_LT_ID_WM3D) axes[3] = 128;	

	if (id == JS_LT_ID_WGPE) {		/* Tilt fixup */
		axes[0] = 512;
		axes[1] = 512;
	}

	for (j = 0; j < naxes10; j++) {
		corr[j].type = JS_CORR_BROKEN;
		corr[j].prec = 2;
		corr[j].coef[0] = axes[j] - 16;
		corr[j].coef[1] = axes[j] + 16;
		corr[j].coef[2] = (1 << 29) / (256 - 32);
		corr[j].coef[3] = (1 << 29) / (256 - 32);
	}

	for (; j < naxes8 + naxes10; j++) {
		corr[j].type = JS_CORR_BROKEN;
		corr[j].prec = 1;
		corr[j].coef[0] = axes[j] - 2;
		corr[j].coef[1] = axes[j] + 2;
		corr[j].coef[2] = (1 << 29) / (64 - 16);
		corr[j].coef[3] = (1 << 29) / (64 - 16);
	}

	for (; j < naxes1 + naxes8 + naxes10; j++) {
		corr[j].type = JS_CORR_BROKEN;
		corr[j].prec = 0;
		corr[j].coef[0] = 0;
		corr[j].coef[1] = 0;
		corr[j].coef[2] = (1 << 29);
		corr[j].coef[3] = (1 << 29);
	}
}

/*
 * js_lt_probe() probes for Logitech type joysticks.
 */

static struct js_port __init *js_lt_probe(int io, struct js_port *port)
{
	struct js_lt_info iniinfo;
	struct js_lt_info *info = &iniinfo;
	char name[32];
	int i, j, t;

	if (check_region(io, 1)) return port;

	js_lt_init_digital(io);

	memset(info, 0, sizeof(struct js_lt_info));

	info->length[0] = info->length[1] = JS_LT_MAX_LENGTH;

	info->io = io;
	js_lt_read_packet(info);
	
	if (info->ret[0] >= JS_LT_MIN_LEN_LENGTH)
		js_lt_move_bits(info, js_lt_get_bits(info, 0, 10));

	info->length[0] = info->length[1] = 0;

	for (i = 0; i < 2; i++) {

		if (info->ret[i] < JS_LT_MIN_ID_LENGTH) continue; /* Minimum ID packet length */

		if (info->ret[i] < (t = js_lt_get_bits(info, i, 10))) {
			printk(KERN_WARNING "joy-logitech: Short ID packet: reported: %d != read: %d\n",
				t, info->ret[i]); 
			continue;
		}

		if (info->ret[i] > t)
			info->bugs[i] |= JS_LT_BUG_LONGID;

#ifdef JS_LT_DEBUG
		printk(KERN_DEBUG "joy-logitech: id %d length %d", i, t);
#endif

		info->id[i] = js_lt_get_bits(info, i, 4) | (js_lt_get_bits(info, i, 4) << 4);

		if ((t = js_lt_get_bits(info, i, 4)) & JS_LT_FLAG_HAT) info->hats[i]++;

#ifdef JS_LT_DEBUG
		printk(KERN_DEBUG "joy-logitech: id %d flags %d", i, t);
#endif

		if ((info->length[i] = js_lt_get_bits(info, i, 10)) >= JS_LT_MAX_LENGTH) {
			printk(KERN_WARNING "joy-logitech: Expected packet length too long (%d).\n",
				info->length[i]);
			continue;
		}

		if (info->length[i] < JS_LT_MIN_LENGTH) {
			printk(KERN_WARNING "joy-logitech: Expected packet length too short (%d).\n",
				info->length[i]);
			continue;
		}

		info->axes8[i] = js_lt_get_bits(info, i, 4);
		info->buttons[i] = js_lt_get_bits(info, i, 6);

		if (js_lt_get_bits(info, i, 6) != 8 && info->hats[i]) {
			printk(KERN_WARNING "joy-logitech: Other than 8-dir POVs not supported yet.\n");
			continue;
		}

		info->buttons[i] += js_lt_get_bits(info, i, 6);
		info->hats[i] += js_lt_get_bits(info, i, 4);

		j = js_lt_get_bits(info, i, 4);

		if (t & JS_LT_FLAG_10BIT) {
			info->axes10[i] = info->axes8[i] - j;
			info->axes8[i] = j;
		}

		t = js_lt_get_bits(info, i, 4);

		for (j = 0; j < t; j++)
			info->name[i][j] = js_lt_get_bits(info, i, 8);
		info->name[i][j] = 0;

		switch (info->id[i]) {
			case JS_LT_ID_TPD:
				info->pad[i] = 4;
				info->buttons[i] -= 4;
				break;
			case JS_LT_ID_WGP:
				info->pad[i] = 0;
				info->buttons[i] -= 4;
				break;
			default:
				info->pad[i] = -1;
				break;
		}

		if (info->length[i] != 
			(t = 8 + info->buttons[i] + info->axes10[i] * 10 + info->axes8[i] * 8 +
			info->hats[i] * 4 + (info->pad[i] != -1) * 4)) {
			printk(KERN_WARNING "js%d: Expected lenght %d != data length %d\n", i, t, info->length[i]);
		}

	}

	if (!info->length[0] && !info->length[1])
		return port;

	request_region(io, 1, "joystick (logitech)");

	port = js_register_port(port, info, 2, sizeof(struct js_lt_info), js_lt_read);
	info = port->info;

	for (i = 0; i < 2; i++)
		if (info->length[i] > 0) {
			sprintf(name, info->id[i] < JS_LT_MAX_ID ?
				js_lt_names[info->id[i]] : js_lt_names[JS_LT_MAX_ID], info->id[i]); 
			printk(KERN_INFO "js%d: %s [%s] at %#x\n",
				js_register_device(port, i,
					info->axes10[i] + info->axes8[i] + ((info->hats[i] + (info->pad[i] >= 0)) << 1),
					info->buttons[i], name, THIS_MODULE, NULL, NULL), name, info->name[i], io);
		}

	mdelay(JS_LT_INIT_DELAY);
	if (js_lt_read(info, port->axes, port->buttons)) {
		if (info->ret[0] < 1) info->bugs[0] |= JS_LT_BUG_IGNTRIG;
		if (info->ret[1] < 1) info->bugs[1] |= JS_LT_BUG_IGNTRIG;
		mdelay(JS_LT_DATA_DELAY);
		js_lt_read(info, port->axes, port->buttons);
	}

	for (i = 0; i < 2; i++)
		if (info->length[i] > 0) {
#ifdef JS_LT_DEBUG
				printk(KERN_DEBUG "js%d: length %d ret %d id %d buttons %d axes10 %d axes8 %d "
						  "pad %d hats %d name %s explen %d\n",
					i, info->length[i], info->ret[i], info->id[i],
					info->buttons[i], info->axes10[i], info->axes8[i],
					info->pad[i], info->hats[i], info->name[i],
					8 + info->buttons[i] + info->axes10[i] * 10 + info->axes8[i] * 8 +
					info->hats[i] * 4 + (info->pad[i] != -1) * 4);
#endif
				if (info->bugs[i]) {
					printk(KERN_WARNING "js%d: Firmware bugs detected:%s%s%s%s\n", i,
						info->bugs[i] & JS_LT_BUG_BUTTONS ? " init_buttons" : "",
						info->bugs[i] & JS_LT_BUG_LONGID ? " long_id" : "",
						info->bugs[i] & JS_LT_BUG_LONGDATA ? " long_data" : "",
						info->bugs[i] & JS_LT_BUG_IGNTRIG ? " ignore_trigger" : "");
				}
				js_lt_init_corr(info->id[i], info->axes10[i], info->axes8[i],
					((info->pad[i] >= 0) + info->hats[i]) << 1, port->axes[i], port->corr[i]);
	}

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
	int i;
	struct js_lt_info *info;

	while (js_lt_port) {
		for (i = 0; i < js_lt_port->ndevs; i++)
			 if (js_lt_port->devs[i])
				js_unregister_device(js_lt_port->devs[i]);
		info = js_lt_port->info;
		release_region(info->io, 1);
		js_lt_port = js_unregister_port(js_lt_port);
	}
}
#endif
