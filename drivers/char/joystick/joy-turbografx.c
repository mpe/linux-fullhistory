/*
 *  joy-turbografx.c  Version 1.2
 *
 *  Copyright (c) 1998 Vojtech Pavlik
 */

/*
 * This is a module for the Linux joystick driver, supporting
 * Steffen Schwenke's <schwenke@burg-halle.de> TurboGraFX parallel port
 * interface.
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
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/joystick.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/delay.h>


MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_PARM(js_tg, "2i");
MODULE_PARM(js_tg_2, "0-2i");
MODULE_PARM(js_tg_3, "0-2i");

#define JS_TG_BUTTON	3
#define JS_TG_UP	4
#define JS_TG_DOWN	5
#define JS_TG_LEFT	6
#define JS_TG_RIGHT	7

static struct js_port* js_tg_port = NULL;

static int js_tg[] __initdata = { -1, 0 };
static int js_tg_2[] __initdata = { -1, 0 };
static int js_tg_3[] __initdata = { -1, 0 };

struct js_tg_info {
#ifdef USE_PARPORT
	struct pardevice *port;	/* parport device */
	int use;		/* use count */
	int wanted;		/* parport wanted */
#else
	int port;		/* hw port */
#endif
	int count;		/* number of joysticks */
};

/*
 * js_tg_read() reads and analyzes tg joystick data.
 */

static int js_tg_read(void *xinfo, int **axes, int **buttons)
{
	struct js_tg_info *info = xinfo;
	int data, i;

	for (i = 0; i < info->count; i++) {

		JS_PAR_DATA_OUT(~(1 << i), info->port);
		data = JS_PAR_STATUS(info->port) ^ ~JS_PAR_STATUS_INVERT;

		axes[i][0] = ((data >> JS_TG_RIGHT) & 1) - ((data >> JS_TG_LEFT) & 1);
		axes[i][1] = ((data >> JS_TG_DOWN ) & 1) - ((data >> JS_TG_UP  ) & 1);

		buttons[i][0] = (data >> JS_TG_BUTTON) & 1;

	}

	return 0;
}

/*
 * open callback: claim parport.
 */

int js_tg_open(struct js_dev *dev)
{
	MOD_INC_USE_COUNT;

#ifdef USE_PARPORT
	{
		struct js_tg_info *info = dev->port->info;
		if (!info->use && parport_claim(info->port)) {
			printk(KERN_WARNING "joy-tg: parport busy\n");		/* port currently not available ... */
			info->wanted++;						/* we'll claim it on wakeup */
			return 0;
		}
		info->use++;
	}
#endif
	return 0;
}

/*
 * close callback: release parport
 */

int js_tg_close(struct js_dev *dev)
{
#ifdef USE_PARPORT
	struct js_tg_info *info = dev->port->info;

	if (!--info->use)
		parport_release(info->port);
#endif
	MOD_DEC_USE_COUNT;

	return 0;
}

/*
 * parport wakeup callback: claim the port!
 */

#ifdef USE_PARPORT
static void js_tg_wakeup(void *v)
{
	struct js_tg_info *info = js_tg_port->info;	/* FIXME! We can have more than 1 port! */

	if (!info->use && info->wanted)
	{
		parport_claim(info->port);
		info->use++;
		info->wanted--;
	}
}
#endif

#ifdef MODULE
void cleanup_module(void)
{
	struct js_tg_info *info;
	int i;

	while (js_tg_port) {
		for (i = 0; i < js_tg_port->ndevs; i++)
			if (js_tg_port->devs[i])
				js_unregister_device(js_tg_port->devs[i]);
		info = js_tg_port->info;
#ifdef USE_PARPORT
		parport_unregister_device(info->port);
#else
		release_region(info->port, 3);
#endif
		js_tg_port = js_unregister_port(js_tg_port);
	}
}
#endif

/*
 * js_tg_init_corr() initializes correction values of
 * tg gamepads.
 */

static void __init js_tg_init_corr(int count, struct js_corr **corr)
{
	int i, j;

	for (i = 0; i < count; i++)
		for (j = 0; j < 2; j++) {
			corr[i][j].type = JS_CORR_BROKEN;
			corr[i][j].prec = 0;
			corr[i][j].coef[0] = 0;
			corr[i][j].coef[1] = 0;
			corr[i][j].coef[2] = (1 << 29);
			corr[i][j].coef[3] = (1 << 29);
		}
}

/*
 * js_tg_probe() probes for tg gamepads.
 */

static struct js_port __init *js_tg_probe(int *config, struct js_port *port)
{
	struct js_tg_info info;
	int i;

	if (config[0] < 0) return port;
	if (config[1] < 1 || config[1] > 8) return port;

#ifdef USE_PARPORT
	{
		struct parport *pp;

		if (config[0] > 0x10)
			for (pp=parport_enumerate(); pp && (pp->base!=config[0]); pp=pp->next);
		else
			for (pp=parport_enumerate(); pp && (config[0]>0); pp=pp->next) config[0]--;

		if (!pp) {
			printk(KERN_ERR "joy-tg: no such parport\n");
			return port;
		}

		info.port = parport_register_device(pp, "joystick (turbografx)", NULL, js_tg_wakeup, NULL, 0, NULL);
		info.wanted = 0;
		info.use = 0;
	}
#else
	info.port = config[0];
	if (check_region(info.port, 3)) return port;
	request_region(info.port, 3, "joystick (turbografx)");
#endif

	info.count = config[1];

	port = js_register_port(port, &info, info.count, sizeof(struct js_tg_info), js_tg_read);


	for (i = 0; i < info.count; i++)
#ifdef USE_PARPORT
		printk(KERN_INFO "js%d: Multisystem joystick on %s\n",
			js_register_device(port, i, 2, 1, "Multisystem joystick", js_tg_open, js_tg_close),
			info.port->port->name);
#else
		printk(KERN_INFO "js%d: Multisystem joystick at %#x\n",
			js_register_device(port, i, 2, 1, "Multisystem joystick", js_tg_open, js_tg_close),
			info.port);
#endif

	js_tg_init_corr(info.count, port->corr);

	return port;
}

#ifndef MODULE
void __init js_tg_setup(char *str, int *ints)
{
	int i;

	if (!strcmp(str,"js_tg"))
		for (i = 0; i <= ints[0] && i < 2; i++) js_tg[i] = ints[i+1];
	if (!strcmp(str,"js_tg_2"))
		for (i = 0; i <= ints[0] && i < 2; i++) js_tg_2[i] = ints[i+1];
	if (!strcmp(str,"js_tg_3"))
		for (i = 0; i <= ints[0] && i < 2; i++) js_tg_3[i] = ints[i+1];

}
#endif

#ifdef MODULE
int init_module(void)
#else
int __init js_tg_init(void)
#endif
{
	js_tg_port = js_tg_probe(js_tg, js_tg_port);
	js_tg_port = js_tg_probe(js_tg_2, js_tg_port);
	js_tg_port = js_tg_probe(js_tg_3, js_tg_port);

	if (js_tg_port) return 0;

#ifdef MODULE
	printk(KERN_WARNING "joy-tg: no joysticks specified\n");
#endif
	return -ENODEV;
}
