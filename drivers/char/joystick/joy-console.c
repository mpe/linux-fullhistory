/*
 * joy-console.c  Version 0.11V
 *
 * Copyright (c) 1998 Andree Borrmann
 */

/*
 * This is a module for the Linux joystick driver, supporting
 * console (NES, SNES, Multi1, Multi2, PSX) gamepads connected
 * via parallel port. Up to five such controllers can be
 * connected to one parallel port.
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
 */

#include <asm/io.h>
#include <asm/system.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/joystick.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/delay.h>


MODULE_AUTHOR("Andree Borrmann <A.Borrmann@tu-bs.de>");
MODULE_PARM(js_console, "2-6i");
MODULE_PARM(js_console2,"2-6i");
MODULE_PARM(js_console3,"2-6i");


#define JS_NO_PAD	0
#define JS_SNES_PAD	1
#define JS_NES_PAD	2
#define JS_NES4_PAD	3
#define JS_MULTI_STICK	4
#define JS_MULTI2_STICK	5
#define JS_PSX_PAD	6

#define JS_MAX_PAD	JS_PSX_PAD

struct js_console_info {
#ifdef USE_PARPORT
	struct pardevice *port;	/* parport device */
#else
	int port;		/* hw port */
#endif
	int pads;		/* total number of pads */
	int snes;		/* SNES pads */
	int nes;		/* NES pads */
	int multi;		/* Multi joysticks */
	int multi2;		/* Multi joysticks with 2 buttons */
	int psx;		/* Normal PSX controllers */
	int negcon;		/* PSX NEGCON controllers */
};

static struct js_port* js_console_port = NULL;

static int js_console[] __initdata = { -1, 0, 0, 0, 0, 0 };
static int js_console2[] __initdata = { -1, 0, 0, 0, 0, 0 };
static int js_console3[] __initdata = { -1, 0, 0, 0, 0, 0 };

static int status_bit[] = { 0x40, 0x80, 0x20, 0x10, 0x08 };

/*
 * NES/SNES support.
 */

#define JS_NES_DELAY	6	/* Delay between bits - 6us */

#define JS_NES_LENGTH	8	/* The NES pads use 8 bits of data */

#define JS_NES_A	0
#define JS_NES_B	1
#define JS_NES_START	2
#define JS_NES_SELECT	3
#define JS_NES_UP	4
#define JS_NES_DOWN	5
#define JS_NES_LEFT	6
#define JS_NES_RIGHT	7

#define JS_SNES_LENGTH	12	/* The SNES true length is 16, but the last 4 bits are unused */

#define JS_SNES_B	0
#define JS_SNES_Y	1
#define JS_SNES_START	2
#define JS_SNES_SELECT	3
#define JS_SNES_UP	4
#define JS_SNES_DOWN	5
#define JS_SNES_LEFT	6
#define JS_SNES_RIGHT	7
#define JS_SNES_A	8
#define JS_SNES_X	9
#define JS_SNES_L	10
#define JS_SNES_R	11

#define JS_NES_POWER	0xf8
#define JS_NES_CLOCK	0x01
#define JS_NES_LATCH	0x02

/*
 * js_nes_read_packet() reads a NES/SNES packet.
 * Each pad uses one bit per byte. So all pads connected to
 * this port are read in parallel.
 */

static void js_nes_read_packet(struct js_console_info *info, int length, unsigned char *data)
{
	int i;

	JS_PAR_DATA_OUT(JS_NES_POWER + JS_NES_CLOCK + JS_NES_LATCH, info->port);
	udelay(JS_NES_DELAY * 2);
	JS_PAR_DATA_OUT(JS_NES_POWER + JS_NES_CLOCK, info->port);

	for (i = 0; i < length; i++) {
		udelay(JS_NES_DELAY);
		JS_PAR_DATA_OUT(JS_NES_POWER, info->port);
		data[i] = JS_PAR_STATUS(info->port) ^ ~JS_PAR_STATUS_INVERT;
		udelay(JS_NES_DELAY);
		JS_PAR_DATA_OUT(JS_NES_POWER + JS_NES_CLOCK, info->port);
	}
}

/*
 * Multisystem joystick support
 */

#define JS_MULTI_LENGTH		5	/* Multi system joystick packet lenght is 5 */
#define JS_MULTI2_LENGTH	6	/* One more bit for one more button */

#define JS_MULTI_UP		0
#define JS_MULTI_DOWN		1
#define JS_MULTI_LEFT		2
#define JS_MULTI_RIGHT		3
#define JS_MULTI_BUTTON		4
#define JS_MULTI_BUTTON2	5

/*
 * js_multi_read_packet() reads a Multisystem joystick packet.
 */

static void js_multi_read_packet(struct js_console_info *info, int length, unsigned char *data)
{
	int i;

	for (i = 0; i < length; i++) {
		JS_PAR_DATA_OUT(~(1 << i), info->port);
		data[i] = JS_PAR_STATUS(info->port) ^ ~JS_PAR_STATUS_INVERT;
	}
}

/*
 * PSX support
 */

#define JS_PSX_DELAY	10

#define JS_PSX_LENGTH	8

#define JS_PSX_NORMAL	0x41
#define JS_PSX_NEGCON	0x23
#define JS_PSX_MOUSE	0x12

#define JS_PSX_SELBUT	0x01
#define JS_PSX_START	0x08
#define JS_PSX_UP	0x10
#define JS_PSX_RIGHT	0x20
#define JS_PSX_DOWN	0x40
#define JS_PSX_LEFT	0x80

#define JS_PSX_CLOCK	0x01
#define JS_PSX_COMMAND	0x02
#define JS_PSX_POWER	0xf8
#define JS_PSX_NOPOWER	0x04
#define JS_PSX_SELECT	0x08

#define JS_PSX_CTRL_OUT(X,Y)	JS_PAR_CTRL_OUT((X)^0x0f, Y)

/*
 * js_psx_command() writes 8bit command and reads 8bit data from
 * the psx pad.
 */

static int js_psx_command(struct js_console_info *info, int b)
{
	int i, cmd, ret=0;

	cmd = (b&1)?JS_PSX_COMMAND:0;
	for (i=0; i<8; i++) {
		JS_PSX_CTRL_OUT(cmd, info->port);
		udelay(JS_PSX_DELAY);
		ret |= ((JS_PAR_STATUS(info->port) ^ JS_PAR_STATUS_INVERT ) & info->psx) ? (1<<i) : 0;
		cmd = (b&1)?JS_PSX_COMMAND:0;
		JS_PSX_CTRL_OUT(JS_PSX_CLOCK | cmd, info->port);
		udelay(JS_PSX_DELAY);
		b >>= 1;
	}
	return ret;
}

/*
 * js_psx_read_packet() reads a whole psx packet and returns
 * device identifier code.
 */

static int js_psx_read_packet(struct js_console_info *info, int length, unsigned char *data)
{
	int i, ret;
	unsigned long flags;

	__save_flags(flags);
	__cli();

	JS_PAR_DATA_OUT(JS_PSX_POWER, info->port);

	JS_PSX_CTRL_OUT(JS_PSX_CLOCK | JS_PSX_SELECT, info->port);	/* Select pad */
	udelay(JS_PSX_DELAY*2);
	js_psx_command(info, 0x01);					/* Access pad */
	ret = js_psx_command(info, 0x42);				/* Get device id */
	if (js_psx_command(info, 0)=='Z')				/* okay? */
		for (i=0; i<length; i++)
			data[i]=js_psx_command(info, 0);
	else ret = -1;

	JS_PSX_CTRL_OUT(JS_PSX_SELECT | JS_PSX_CLOCK, info->port);
	__restore_flags(flags);

	return ret;
}


/*
 * js_console_read() reads and analyzes console pads data.
 */

#define JS_MAX_LENGTH JS_SNES_LENGTH

static int js_console_read(void *xinfo, int **axes, int **buttons)
{
	struct js_console_info *info = xinfo;
	unsigned char data[JS_MAX_LENGTH];

	int i, s;
	int n = 0;

/*
 * NES and SNES pads
 */

	if (info->nes || info->snes) {

		js_nes_read_packet(info, info->snes ? JS_SNES_LENGTH : JS_NES_LENGTH, data);

		for (i = 0; i < 5; i++) {
			s = status_bit[i];
			if (info->nes & s) {
				axes[n][0] = (data[JS_SNES_RIGHT]&s?1:0) - (data[JS_SNES_LEFT]&s?1:0);
				axes[n][1] = (data[JS_SNES_DOWN] &s?1:0) - (data[JS_SNES_UP]  &s?1:0);

				buttons[n][0] = ((data[JS_NES_A]    &s)?1:0) | ((data[JS_NES_B]     &s)?2:0)
					      | ((data[JS_NES_START]&s)?4:0) | ((data[JS_NES_SELECT]&s)?8:0);

				n++;
			} else
			if (info->snes & s) {
				axes[n][0] = (data[JS_SNES_RIGHT]&s?1:0) - (data[JS_SNES_LEFT]&s?1:0);
				axes[n][1] = (data[JS_SNES_DOWN] &s?1:0) - (data[JS_SNES_UP]  &s?1:0);

				buttons[n][0] = ((data[JS_SNES_A]    &s)?0x01:0) | ((data[JS_SNES_B]     &s)?0x02:0)
					      | ((data[JS_SNES_X]    &s)?0x04:0) | ((data[JS_SNES_Y]     &s)?0x08:0)
					      | ((data[JS_SNES_L]    &s)?0x10:0) | ((data[JS_SNES_R]     &s)?0x20:0)
					      | ((data[JS_SNES_START]&s)?0x40:0) | ((data[JS_SNES_SELECT]&s)?0x80:0);
				n++;
			}
		}
	}

/*
 * Multi and Multi2 joysticks
 */

	if (info->multi || info->multi2) {

		js_multi_read_packet(info, info->multi2 ? JS_MULTI2_LENGTH : JS_MULTI_LENGTH, data);

		for (i = 0; i < 5; i++) {
			s = status_bit[i];
			if (info->multi & s) {
				axes[n][0] = (data[JS_MULTI_RIGHT]&s?1:0) - (data[JS_MULTI_LEFT]&s?1:0);
				axes[n][1] = (data[JS_MULTI_DOWN] &s?1:0) - (data[JS_MULTI_UP]  &s?1:0);

				buttons[n][0] = (data[JS_MULTI_BUTTON]&s)?1:0;

				n++;
			} else
			if (info->multi2 & s) {
				axes[n][0] = (data[JS_MULTI_RIGHT]&s?1:0) - (data[JS_MULTI_LEFT]&s?1:0);
				axes[n][1] = (data[JS_MULTI_DOWN] &s?1:0) - (data[JS_MULTI_UP]  &s?1:0);

				buttons[n][0] = (data[JS_MULTI_BUTTON]&s)?1:0 | (data[JS_MULTI_BUTTON2]&s)?2:0;

				n++;
			}
		}
	}

/*
 * PSX controllers
 */

	if (info->psx && (js_psx_read_packet(info, 2, data) == JS_PSX_NORMAL)) {	/* FIXME? >1 PSX pads? */

		axes[n][0] = (data[0]&JS_PSX_RIGHT?0:1) - (data[0]&JS_PSX_LEFT?0:1);
		axes[n][1] = (data[0]&JS_PSX_DOWN ?0:1) - (data[0]&JS_PSX_UP  ?0:1);

		buttons[n][0] = ((~data[1]&0xf)<<4) | ((~data[1]&0xf0)>>4) |
				(data[0]&JS_PSX_START?0:0x200) | (data[0]&JS_PSX_SELBUT?0:0x100);

		n++;
	}

	return -(n != info->pads);
}

/*
 * open callback: claim parport.
 */

int js_console_open(struct js_dev *dev)
{
#ifdef USE_PARPORT
	struct js_console_info *info = dev->port->info;
	if (!MOD_IN_USE && parport_claim(info->port)) return -EBUSY;
#endif
	MOD_INC_USE_COUNT;
	return 0;
}

/*
 * close callback: release parport
 */

int js_console_close(struct js_dev *dev)
{
#ifdef USE_PARPORT
	struct js_console_info *info = dev->port->info;
#endif
	MOD_DEC_USE_COUNT;
#ifdef USE_PARPORT
	if (!MOD_IN_USE) parport_release(info->port);
#endif
	return 0;
}

#ifdef MODULE
void cleanup_module(void)
{
	struct js_console_info *info;
	int i;

	while (js_console_port != NULL) {
		for (i = 0; i < js_console_port->ndevs; i++)
			if (js_console_port->devs[i] != NULL)
				js_unregister_device(js_console_port->devs[i]);
		info = js_console_port->info;
#ifdef USE_PARPORT
		parport_unregister_device(info->port);
#else
		release_region(info->port, 3);
#endif
		js_console_port = js_unregister_port(js_console_port);
	}
}
#endif

/*
 * js_console_init_corr() initializes correction values of
 * console gamepads.
 */

static void __init js_console_init_corr(int num_axes, struct js_corr *corr)
{
	int i;

	for (i = 0; i < num_axes; i++) {
		corr[i].type = JS_CORR_BROKEN;
		corr[i].prec = 0;
		corr[i].coef[0] = 0;
		corr[i].coef[1] = 0;
		corr[i].coef[2] = (1 << 29);
		corr[i].coef[3] = (1 << 29);
	}
}

/*
 * js_console_probe() probes for console gamepads.
 * Only PSX pads can really be probed for.
 */

static struct js_port __init *js_console_probe(int *config, struct js_port *port)
{
	char *name[5];
	int i, psx, axes[5], buttons[5];
	unsigned char data[2];			/* used for PSX probe */
	struct js_console_info info;

	memset(&info, 0, sizeof(struct js_console_info));

	if (config[0] < 0) return port;

#ifdef USE_PARPORT
	{
		struct parport *pp;

		if (config[0] > 0x10)
			for (pp=parport_enumerate(); pp != NULL && (pp->base!=config[0]); pp=pp->next);
		else
			for (pp=parport_enumerate(); pp != NULL && (config[0]>0); pp=pp->next) config[0]--;

		if (pp == NULL) {
			printk(KERN_ERR "joy-console: no such parport\n");
			return port;
		}

		info.port = parport_register_device(pp, "joystick (console)", NULL, NULL, NULL, PARPORT_DEV_EXCL, NULL);
		if (!info.port)
			return port;
	}

	if (parport_claim(info.port))
	{
		parport_unregister_device(info.port);	/* port currently not available ... */
		return port;
	}
#else
	info.port = config[0];
	if (check_region(info.port, 3)) return port;
	request_region(info.port, 3, "joystick (console pad)");
#endif

	for (i = 0; i < 5; i++)
		switch(config[i+1]) {

			case JS_NO_PAD:

				break;

			case JS_SNES_PAD:

				axes[info.pads]    = 2;
				buttons[info.pads] = 8;
				name[info.pads]    = "SNES pad";
				info.snes |= status_bit[i];
				info.pads++;
				break;

			case JS_NES_PAD:

				axes[info.pads]    = 2;
				buttons[info.pads] = 4;
				name[info.pads]    = "NES pad";
				info.nes |= status_bit[i];
				info.pads++;
				break;

			case JS_MULTI_STICK:

				axes[info.pads]    = 2;
				buttons[info.pads] = 1;
				name[info.pads]    = "Multisystem joystick";
				info.multi |= status_bit[i];
				info.pads++;
				break;

			case JS_MULTI2_STICK:

				axes[info.pads]    = 2;
				buttons[info.pads] = 2;
				name[info.pads]    = "Multisystem joystick (2 fire)";
				info.multi |= status_bit[i];
				info.pads++;
				break;

			case JS_PSX_PAD:

				info.psx |= status_bit[i];
				psx = js_psx_read_packet(&info, 2, data);
				psx = js_psx_read_packet(&info, 2, data);
				info.psx &= ~status_bit[i];

				switch(psx) {
					case JS_PSX_NORMAL:
						axes[info.pads]    = 2;
						buttons[info.pads] = 10;
						name[info.pads]    = "PSX controller";
						info.psx |= status_bit[i];
						info.pads++;
						break;
					case JS_PSX_NEGCON:
						printk(KERN_WARNING "joy-console: NegCon not yet supported...\n");
						break;
					case JS_PSX_MOUSE:
						printk(KERN_WARNING "joy-console: PSX mouse not supported...\n");
						break;
					case -1:
						printk(KERN_ERR "joy-console: no PSX controller found...\n");
						break;
					default:
						printk(KERN_WARNING "joy-console: unknown PSX controller 0x%x\n", psx);
				}
				break;

			default:

				printk(KERN_WARNING "joy-console: pad type %d unknown\n", config[i+1]);
		}

	if (!info.pads) {
#ifdef USE_PARPORT
		parport_release(info.port);
		parport_unregister_device(info.port);
#else
		release_region(info.port, 3);
#endif
		return port;
	}

	port = js_register_port(port, &info, info.pads, sizeof(struct js_console_info), js_console_read);

	for (i = 0; i < info.pads; i++) {
#ifdef USE_PARPORT
		printk(KERN_INFO "js%d: %s on %s\n",
			js_register_device(port, i, axes[i], buttons[i], name[i], js_console_open, js_console_close),
			name[i], info.port->port->name);
#else
		printk(KERN_INFO "js%d: %s at %#x\n",
			js_register_device(port, i, axes[i], buttons[i], name[i], js_console_open, js_console_close),
			name[i], info.port);
#endif

		js_console_init_corr(axes[i], port->corr[i]);
	}

#ifdef USE_PARPORT
	parport_release(info.port);
#endif
	return port;
}

#ifndef MODULE
void __init js_console_setup(char *str, int *ints)
{
	int i;

	if (!strcmp(str,"js_console"))
		for (i = 0; i <= ints[0] && i < 6; i++) js_console[i] = ints[i+1];
	if (!strcmp(str,"js_console2"))
		for (i = 0; i <= ints[0] && i < 6; i++) js_console2[i] = ints[i+1];
	if (!strcmp(str,"js_console3"))
		for (i = 0; i <= ints[0] && i < 6; i++) js_console3[i] = ints[i+1];

}
#endif

#ifdef MODULE
int init_module(void)
#else
int __init js_console_init(void)
#endif
{
	js_console_port = js_console_probe(js_console, js_console_port);
	js_console_port = js_console_probe(js_console2, js_console_port);
	js_console_port = js_console_probe(js_console3, js_console_port);

	if (js_console_port) return 0;

#ifdef MODULE
	printk(KERN_WARNING "joy-console: no joysticks specified\n");
#endif
	return -ENODEV;
}
