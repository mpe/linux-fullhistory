/*
 *  joy-db9.c  Version 0.6V
 *
 *  Copyright (c) 1998 Andree Borrmann
 *  Copyright (c) 1999 Vojtech Pavlik
 *
 *  Sponsored by SuSE
 */

/*
 * This is a module for the Linux joystick driver, supporting
 * console (Atari, Amstrad, Commodore, Amiga, Sega) joysticks
 * and gamepads connected to the parallel port.
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
#include <linux/delay.h>
#include <linux/init.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@suse.cz>");
MODULE_PARM(js_db9, "2i");
MODULE_PARM(js_db9_2, "2i");
MODULE_PARM(js_db9_3, "2i");

#define JS_MULTI_STICK	0x01
#define JS_MULTI2_STICK 0x02
#define JS_GENESIS_PAD  0x03
#define JS_GENESIS5_PAD 0x05
#define JS_GENESIS6_PAD	0x06
#define JS_SATURN_PAD	0x07
#define JS_MULTI_0802	0x08
#define JS_MULTI_0802_2	0x09
#define JS_MAX_PAD	0x0A

#define JS_DB9_UP	0x01
#define JS_DB9_DOWN	0x02
#define JS_DB9_LEFT	0x04
#define JS_DB9_RIGHT	0x08
#define JS_DB9_FIRE1	0x10
#define JS_DB9_FIRE2	0x20
#define JS_DB9_FIRE3	0x40
#define JS_DB9_FIRE4	0x80

#define JS_DB9_NORMAL	0x2a
#define JS_DB9_NOSELECT	0x28

#define JS_DB9_SATURN0	0x20
#define JS_DB9_SATURN1	0x22
#define JS_DB9_SATURN2	0x24
#define JS_DB9_SATURN3	0x26

#define JS_GENESIS6_DELAY	14

static struct js_port* js_db9_port = NULL;

static int js_db9[] __initdata = { -1, 0 };
static int js_db9_2[] __initdata = { -1, 0 };
static int js_db9_3[] __initdata = { -1, 0 };

struct js_db9_info {
	struct pardevice *port;	/* parport device */
	int mode;		/* pad mode */
};

/*
 * js_db9_read() reads and analyzes db9 joystick data.
 */

static int js_db9_read(void *xinfo, int **axes, int **buttons)
{
	struct js_db9_info *info = xinfo;
	int data;

	switch(info->mode)
	{
	  case JS_MULTI_0802_2:

		data = JS_PAR_DATA_IN(info->port) >> 3;

		axes[1][1] = (data&JS_DB9_DOWN ?0:1) - (data&JS_DB9_UP  ?0:1);
		axes[1][0] = (data&JS_DB9_RIGHT?0:1) - (data&JS_DB9_LEFT?0:1);

		buttons[1][0] = (data&JS_DB9_FIRE1?0:1);

	  case JS_MULTI_0802:

		data = JS_PAR_STATUS(info->port) >> 3;

		axes[0][1] = (data&JS_DB9_DOWN ?0:1) - (data&JS_DB9_UP  ?0:1);
		axes[0][0] = (data&JS_DB9_RIGHT?0:1) - (data&JS_DB9_LEFT?0:1);

		buttons[0][0] = (data&JS_DB9_FIRE1?1:0);

		break;

	  case JS_MULTI_STICK:

		data = JS_PAR_DATA_IN(info->port);

		axes[0][1] = (data&JS_DB9_DOWN ?0:1) - (data&JS_DB9_UP  ?0:1);
		axes[0][0] = (data&JS_DB9_RIGHT?0:1) - (data&JS_DB9_LEFT?0:1);

		buttons[0][0] = (data&JS_DB9_FIRE1?0:1);

		break;

	  case JS_MULTI2_STICK:

		data=JS_PAR_DATA_IN(info->port);

		axes[0][1] = (data&JS_DB9_DOWN ?0:1) - (data&JS_DB9_UP  ?0:1);
		axes[0][0] = (data&JS_DB9_RIGHT?0:1) - (data&JS_DB9_LEFT?0:1);

		buttons[0][0] = (data&JS_DB9_FIRE1?0:1) | (data&JS_DB9_FIRE2?0:2);

		break;

	  case JS_GENESIS_PAD:

		JS_PAR_CTRL_OUT(JS_DB9_NOSELECT, info->port);
		data = JS_PAR_DATA_IN(info->port);

		axes[0][1] = (data&JS_DB9_DOWN ?0:1) - (data&JS_DB9_UP  ?0:1);
		axes[0][0] = (data&JS_DB9_RIGHT?0:1) - (data&JS_DB9_LEFT?0:1);

		buttons[0][0] = (data&JS_DB9_FIRE1?0:2) | (data&JS_DB9_FIRE2?0:4);

		JS_PAR_CTRL_OUT(JS_DB9_NORMAL,info->port);
		data=JS_PAR_DATA_IN(info->port);

		buttons[0][0] |= (data&JS_DB9_FIRE1?0:1) | (data&JS_DB9_FIRE2?0:8);

		break;

	  case JS_GENESIS5_PAD:

		JS_PAR_CTRL_OUT(JS_DB9_NOSELECT,info->port);
		data=JS_PAR_DATA_IN(info->port);

		axes[0][1] = (data&JS_DB9_DOWN ?0:1) - (data&JS_DB9_UP  ?0:1);
		axes[0][0] = (data&JS_DB9_RIGHT?0:1) - (data&JS_DB9_LEFT?0:1);

		buttons[0][0] = (data&JS_DB9_FIRE1?0:0x02) | (data&JS_DB9_FIRE2?0:0x04);

		JS_PAR_CTRL_OUT(JS_DB9_NORMAL, info->port);
		data=JS_PAR_DATA_IN(info->port);

		buttons[0][0] |= (data&JS_DB9_FIRE1?0:0x01) | (data&JS_DB9_FIRE2?0:0x08) |
				 (data&JS_DB9_LEFT ?0:0x10) | (data&JS_DB9_RIGHT?0:0x20);
		break;

	  case JS_GENESIS6_PAD:

		JS_PAR_CTRL_OUT(JS_DB9_NOSELECT,info->port); /* 1 */
		udelay(JS_GENESIS6_DELAY);
		data=JS_PAR_DATA_IN(info->port);

		axes[0][1] = (data&JS_DB9_DOWN ?0:1) - (data&JS_DB9_UP  ?0:1);
		axes[0][0] = (data&JS_DB9_RIGHT?0:1) - (data&JS_DB9_LEFT?0:1);

		buttons[0][0] = (data&JS_DB9_FIRE1?0:0x02) | (data&JS_DB9_FIRE2?0:0x04);

		JS_PAR_CTRL_OUT(JS_DB9_NORMAL, info->port);
		udelay(JS_GENESIS6_DELAY);
		data=JS_PAR_DATA_IN(info->port);

		buttons[0][0] |= (data&JS_DB9_FIRE1?0:0x01) | (data&JS_DB9_FIRE2?0:0x08);

		JS_PAR_CTRL_OUT(JS_DB9_NOSELECT, info->port); /* 2 */
		udelay(JS_GENESIS6_DELAY);
		JS_PAR_CTRL_OUT(JS_DB9_NORMAL, info->port);
		udelay(JS_GENESIS6_DELAY);
		JS_PAR_CTRL_OUT(JS_DB9_NOSELECT, info->port); /* 3 */
		udelay(JS_GENESIS6_DELAY);
		data=JS_PAR_DATA_IN(info->port);

		buttons[0][0] |= (data&JS_DB9_LEFT?0:0x10) | (data&JS_DB9_DOWN ?0:0x20) |
				 (data&JS_DB9_UP  ?0:0x40) | (data&JS_DB9_RIGHT?0:0x80);

		JS_PAR_CTRL_OUT(JS_DB9_NORMAL, info->port);
		udelay(JS_GENESIS6_DELAY);
		JS_PAR_CTRL_OUT(JS_DB9_NOSELECT, info->port); /* 4 */
		udelay(JS_GENESIS6_DELAY);
		JS_PAR_CTRL_OUT(JS_DB9_NORMAL, info->port);

		break;

	  case JS_SATURN_PAD:

		JS_PAR_CTRL_OUT(JS_DB9_SATURN0, info->port);
		data = JS_PAR_DATA_IN(info->port);

		buttons[0][0] = (data&JS_DB9_UP  ?0:0x20) | (data&JS_DB9_DOWN ?0:0x10) |
				(data&JS_DB9_LEFT?0:0x08) | (data&JS_DB9_RIGHT?0:0x40);

		JS_PAR_CTRL_OUT(JS_DB9_SATURN2, info->port);
		data = JS_PAR_DATA_IN(info->port);

		axes[0][1] = (data&JS_DB9_DOWN ?0:1) - (data&JS_DB9_UP  ?0:1);
		axes[0][0] = (data&JS_DB9_RIGHT?0:1) - (data&JS_DB9_LEFT?0:1);

		JS_PAR_CTRL_OUT(JS_DB9_NORMAL, info->port);
		data = JS_PAR_DATA_IN(info->port);

		buttons[0][0] |= (data&JS_DB9_UP  ?0:0x02) | (data&JS_DB9_DOWN ?0:0x04) |
				 (data&JS_DB9_LEFT?0:0x01) | (data&JS_DB9_RIGHT?0:0x80);


		break;

	  default:
		return -1;
	}

	return 0;
}

/*
 * open callback: claim parport.
 */

int js_db9_open(struct js_dev *dev)
{
	struct js_db9_info *info = dev->port->info;

	if (!MOD_IN_USE) {
		if (parport_claim(info->port)) return -EBUSY;

		JS_PAR_DATA_OUT(0xff, info->port);
		if (info->mode != JS_MULTI_0802)
			JS_PAR_ECTRL_OUT(0x35,info->port);	/* enable PS/2 mode: */
		JS_PAR_CTRL_OUT(JS_DB9_NORMAL,info->port);	/* reverse direction, enable Select signal */
	}
		
	MOD_INC_USE_COUNT;
	return 0;
}

/*
 * close callback: release parport
 */

int js_db9_close(struct js_dev *dev)
{
	struct js_db9_info *info = dev->port->info;

	MOD_DEC_USE_COUNT;

	if (!MOD_IN_USE) {

		JS_PAR_CTRL_OUT(0x00,info->port);		/* normal direction */
		if (info->mode != JS_MULTI_0802)
			JS_PAR_ECTRL_OUT(0x15,info->port);	/* enable normal mode */

		parport_release(info->port);
	}
	return 0;
}

#ifdef MODULE
void cleanup_module(void)
{
	struct js_db9_info *info;
	int i;

	while (js_db9_port) {
		info = js_db9_port->info;

		for (i = 0; i < js_db9_port->ndevs; i++)
			if (js_db9_port->devs[i])
				js_unregister_device(js_db9_port->devs[i]);
		parport_unregister_device(info->port);
		js_db9_port = js_unregister_port(js_db9_port);
	}

}
#endif

/*
 * js_db9_init_corr() initializes correction values of
 * db9 gamepads.
 */

static void __init js_db9_init_corr(struct js_corr *corr)
{
	int i;

	for (i = 0; i < 2; i++) {
		corr[i].type = JS_CORR_BROKEN;
		corr[i].prec = 0;
		corr[i].coef[0] = 0;
		corr[i].coef[1] = 0;
		corr[i].coef[2] = (1 << 29);
		corr[i].coef[3] = (1 << 29);
	}
}

/*
 * js_db9_probe() probes for db9 gamepads.
 */

static struct js_port __init *js_db9_probe(int *config, struct js_port *port)
{
	struct js_db9_info info;
	struct parport *pp;
	int i;
	char buttons[JS_MAX_PAD] = {0,1,2,4,0,6,8,8,1,1};
	char *name[JS_MAX_PAD] = {NULL, "Multisystem joystick", "Multisystem joystick (2 fire)", "Genesis pad",
					NULL, "Genesis 5 pad", "Genesis 6 pad", "Saturn pad", "Multisystem (0.8.0.2) joystick",
					"Multisystem (0.8.0.2-dual) joystick"};

	if (config[0] < 0) return port;
	if (config[1] < 0 || config[1] >= JS_MAX_PAD || !name[config[1]]) return port;

	info.mode = config[1];

	if (config[0] > 0x10)
		for (pp=parport_enumerate(); pp && (pp->base!=config[0]); pp=pp->next);
	else
		for (pp=parport_enumerate(); pp && (config[0]>0); pp=pp->next) config[0]--;

	if (!pp) {
		printk(KERN_ERR "joy-db9: no such parport\n");
		return port;
	}

	if (!(pp->modes & (PARPORT_MODE_PCPS2 | PARPORT_MODE_PCECPPS2)) && info.mode != JS_MULTI_0802) {
		printk(KERN_ERR "js-db9: specified parport is not bidirectional\n");
		return port;
	}

	info.port = parport_register_device(pp, "joystick (db9)", NULL, NULL, NULL, PARPORT_DEV_EXCL, NULL);
	if (!info.port)
		return port;

	port = js_register_port(port, &info, 1 + (info.mode == JS_MULTI_0802_2), sizeof(struct js_db9_info), js_db9_read);

	for (i = 0; i < 1 + (info.mode == JS_MULTI_0802_2); i++) {
		printk(KERN_INFO "js%d: %s on %s\n",
			js_register_device(port, i, 2, buttons[info.mode], name[info.mode], js_db9_open, js_db9_close),
			name[info.mode], info.port->port->name);

		js_db9_init_corr(port->corr[i]);
	}


	return port;
}

#ifndef MODULE
int __init js_db9_setup(SETUP_PARAM)
{
	int i;
	SETUP_PARSE(2);
	for (i = 0; i <= ints[0] && i < 2; i++) js_db9[i] = ints[i+1];
	return 1;
}
int __init js_db9_setup_2(SETUP_PARAM)
{
	int i;
	SETUP_PARSE(2);
	for (i = 0; i <= ints[0] && i < 2; i++) js_db9_2[i] = ints[i+1];
	return 1;
}
int __init js_db9_setup_3(SETUP_PARAM)
{
	int i;
	SETUP_PARSE(2);
	for (i = 0; i <= ints[0] && i < 2; i++) js_db9_3[i] = ints[i+1];
	return 1;
}
__setup("js_db9=", js_db9_setup);
__setup("js_db9_2=", js_db9_setup_2);
__setup("js_db9_3=", js_db9_setup_3);
#endif

#ifdef MODULE
int init_module(void)
#else
int __init js_db9_init(void)
#endif
{
	js_db9_port = js_db9_probe(js_db9, js_db9_port);
	js_db9_port = js_db9_probe(js_db9_2, js_db9_port);
	js_db9_port = js_db9_probe(js_db9_3, js_db9_port);

	if (js_db9_port) return 0;

#ifdef MODULE
	printk(KERN_WARNING "joy-db9: no joysticks specified\n");
#endif
	return -ENODEV;
}
