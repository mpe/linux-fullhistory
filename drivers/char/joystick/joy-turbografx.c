/*
 *  joy-turbografx.c  Version 1.2
 *
 *  Copyright (c) 1998-1999 Vojtech Pavlik
 *
 *  Sponsored by SuSE
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
MODULE_PARM(js_tg, "2-8i");
MODULE_PARM(js_tg_2, "2-8i");
MODULE_PARM(js_tg_3, "2-8i");

#define JS_TG_BUTTON1	0x08
#define JS_TG_UP	0x10
#define JS_TG_DOWN	0x20	
#define JS_TG_LEFT	0x40
#define JS_TG_RIGHT	0x80

#define JS_TG_BUTTON2	0x02
#define JS_TG_BUTTON3	0x04
#define JS_TG_BUTTON4	0x01
#define JS_TG_BUTTON5	0x08

static struct js_port* js_tg_port __initdata = NULL;

static int js_tg[] __initdata = { -1, 0, 0, 0, 0, 0, 0, 0 };
static int js_tg_2[] __initdata = { -1, 0, 0, 0, 0, 0, 0, 0 };
static int js_tg_3[] __initdata = { -1, 0, 0, 0, 0, 0, 0, 0 };

struct js_tg_info {
	struct pardevice *port;	/* parport device */
	int sticks;		/* joysticks connected */
};

/*
 * js_tg_read() reads and analyzes tg joystick data.
 */

static int js_tg_read(void *xinfo, int **axes, int **buttons)
{
	struct js_tg_info *info = xinfo;
	int data1, data2, i;

	for (i = 0; i < 7; i++)
		if ((info->sticks >> i) & 1) {

		JS_PAR_DATA_OUT(~(1 << i), info->port);
		data1 = JS_PAR_STATUS(info->port) ^ ~JS_PAR_STATUS_INVERT;
		data2 = JS_PAR_CTRL_IN(info->port) ^ JS_PAR_CTRL_INVERT;

		axes[i][0] = ((data1 & JS_TG_RIGHT) ? 1 : 0) - ((data1 & JS_TG_LEFT) ? 1 : 0);
		axes[i][1] = ((data1 & JS_TG_DOWN ) ? 1 : 0) - ((data1 & JS_TG_UP  ) ? 1 : 0);

		buttons[i][0] = ((data1 & JS_TG_BUTTON1) ? 0x01 : 0) | ((data2 & JS_TG_BUTTON2) ? 0x02 : 0)
			      | ((data2 & JS_TG_BUTTON3) ? 0x04 : 0) | ((data2 & JS_TG_BUTTON4) ? 0x08 : 0)
			      | ((data2 & JS_TG_BUTTON5) ? 0x10 : 0);

	}

	return 0;
}

/*
 * open callback: claim parport.
 * FIXME: race possible here.
 */

int js_tg_open(struct js_dev *dev)
{
	struct js_tg_info *info = dev->port->info;

	if (!MOD_IN_USE) {
		if (parport_claim(info->port)) return -EBUSY; 
		JS_PAR_CTRL_OUT(0x04, info->port);
	}
	MOD_INC_USE_COUNT;
	return 0;
}

/*
 * close callback: release parport
 */

int js_tg_close(struct js_dev *dev)
{
        struct js_tg_info *info = dev->port->info;

        MOD_DEC_USE_COUNT;
	if (!MOD_IN_USE) {
		JS_PAR_CTRL_OUT(0x00, info->port);
        	parport_release(info->port);
	}
        return 0;
}

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
		parport_unregister_device(info->port);
		js_tg_port = js_unregister_port(js_tg_port);
	}
}
#endif

/*
 * js_tg_init_corr() initializes correction values of
 * tg gamepads.
 */

static void __init js_tg_init_corr(int sticks, struct js_corr **corr)
{
	int i, j;

	for (i = 0; i < 7; i++)
		if ((sticks >> i) & 1)
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
	struct js_tg_info iniinfo;
	struct js_tg_info *info = &iniinfo;
	struct parport *pp;
	int i;

	if (config[0] < 0) return port;


	if (config[0] > 0x10)
		for (pp=parport_enumerate(); pp && (pp->base!=config[0]); pp=pp->next);
	else
		for (pp=parport_enumerate(); pp && (config[0]>0); pp=pp->next) config[0]--;

	if (!pp) {
		printk(KERN_ERR "joy-tg: no such parport\n");
		return port;
	}

	info->port = parport_register_device(pp, "joystick (turbografx)", NULL, NULL, NULL, PARPORT_DEV_EXCL, NULL);
	if (!info->port)
		return port;

	port = js_register_port(port, info, 7, sizeof(struct js_tg_info), js_tg_read);
	info = port->info;

	info->sticks = 0;

	for (i = 0; i < 7; i++)
		if (config[i+1] > 0 && config[i+1] < 6) {
			printk(KERN_INFO "js%d: Multisystem joystick on %s\n",
				js_register_device(port, i, 2, config[i+1], "Multisystem joystick", NULL, js_tg_open, js_tg_close),
				info->port->port->name);
			info->sticks |= (1 << i);
		}

        if (!info->sticks) {
		parport_unregister_device(info->port);
		return port;
        }
		
	js_tg_init_corr(info->sticks, port->corr);

	return port;
}

#ifndef MODULE
int __init js_tg_setup(SETUP_PARAM)
{
	int i;
	SETUP_PARSE(2);
	for (i = 0; i <= ints[0] && i < 2; i++) js_tg[i] = ints[i+1];
	return 1;
}
int __init js_tg_setup_2(SETUP_PARAM)
{
	int i;
	SETUP_PARSE(2);
	for (i = 0; i <= ints[0] && i < 2; i++) js_tg_2[i] = ints[i+1];
	return 1;
}
int __init js_tg_setup_3(SETUP_PARAM)
{
	int i;
	SETUP_PARSE(2);
	for (i = 0; i <= ints[0] && i < 2; i++) js_tg_3[i] = ints[i+1];
	return 1;
}
__setup("js_tg=", js_tg_setup);
__setup("js_tg_2=", js_tg_setup_2);
__setup("js_tg_3=", js_tg_setup_3);
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
