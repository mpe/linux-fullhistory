/*
 *  joy-amiga.c  Version 1.2
 *
 *  Copyright (c) 1998 Vojtech Pavlik
 */

/*
 * This is a module for the Linux joystick driver, supporting
 * microswitch based joystick connected to Amiga joystick port.
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

#include <asm/system.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/joystick.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/amigahw.h>

static struct js_port* js_am_port __initdata = NULL;

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_PARM(js_am, "1-2i");

static int js_am[]={0,0};

/*
 * js_am_read() reads and Amiga joystick data.
 */

static int js_am_read(void *info, int **axes, int **buttons)
{
	int data = 0;

	switch (*(int*)info) {
		case 0:
			data = ~custom.joy0dat;
			buttons[0][0] = (~ciaa.pra >> 6) & 1;
			break;

		case 1:
			data = ~custom.joy1dat;
			buttons[0][0] = (~ciaa.pra >> 7) & 1;
			break;

		default:
			return -1;
	}

	axes[0][0] = ((data >> 1) & 1) - ((data >> 9) & 1);
	data = ~(data ^ (data << 1));
	axes[0][0] = ((data >> 1) & 1) - ((data >> 9) & 1);

	return 0;
}

/*
 * js_am_open() is a callback from the file open routine.
 */

static int js_am_open(struct js_dev *jd)
{
	MOD_INC_USE_COUNT;
	return 0;
}

/*
 * js_am_close() is a callback from the file release routine.
 */

static int js_am_close(struct js_dev *jd)
{
	MOD_DEC_USE_COUNT;
	return 0;
}

/*
 * js_am_init_corr() initializes correction values of
 * Amiga joysticks.
 */

static void __init js_am_init_corr(struct js_corr **corr)
{
	int i;

	for (i = 0; i < 2; i++) {
		corr[0][i].type = JS_CORR_BROKEN;
		corr[0][i].prec = 0;
		corr[0][i].coef[0] = 0;
		corr[0][i].coef[1] = 0;
		corr[0][i].coef[2] = (1 << 29);
		corr[0][i].coef[3] = (1 << 29);
	}
}

#ifndef MODULE
void __init js_am_setup(char *str, int *ints)
{
	int i;
	for (i = 0; i <= ints[0] && i < 2; i++) js_am[i] = ints[i+1];
}
#endif

#ifdef MODULE
int init_module(void)
#else
int __init js_am_init(void)
#endif
{
	int i;

	for (i = 0; i < 2; i++)
		if (js_am[i]) {
			js_am_port = js_register_port(js_am_port, &i, 1, sizeof(int), js_am_read);
			printk(KERN_INFO "js%d: Amiga joystick at joy%ddat\n",
				js_register_device(js_am_port, 0, 2, 1, "Amiga joystick", js_am_open, js_am_close), i);
			js_am_init_corr(js_am_port->corr);
		}
	if (js_am_port) return 0;

#ifdef MODULE
	printk(KERN_WARNING "joy-amiga: no joysticks specified\n");
#endif

	return -ENODEV;
}

#ifdef MODULE
void cleanup_module(void)
{
	while (js_am_port != NULL) {
		if (js_am_port->devs[0] != NULL)
			js_unregister_device(js_am_port->devs[0]);
		js_am_port = js_unregister_port(js_am_port);
	}
}
#endif
