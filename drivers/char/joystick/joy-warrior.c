/*
 *  joy-warrior.c  Version 0.1
 *
 *  Copyright (c) 1998 David Thompson
 *  Copyright (c) 1999 Vojtech Pavlik
 *
 *  Sponsored by SuSE
 */

/*
 * This is a module for the Linux joystick driver, supporting
 * the Logitech WingMan Warrior joystick.
 */

/*
 * This program is free warftware; you can redistribute it and/or modify
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
 *  Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@suse.cz>, or by paper mail:
 * Vojtech Pavlik, Ucitelska 1576, Prague 8, 182 00 Czech Republic
 */

#include <asm/io.h>
#include <asm/system.h>
#include <linux/errno.h>
#include <linux/joystick.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/tty.h>
#include <linux/init.h>

/*
 * Constants.
 */

#define	N_JOYSTICK_WAR		13
#define JS_WAR_MAX_LENGTH	16

/*
 * List of Warriors.
 */

static struct js_port* js_war_port = NULL;

static char js_war_lengths[] = { 0, 4, 12, 3, 4, 4, 0, 0 }; 

/*
 * Per-Warrior data.
 */

struct js_war_info {
	struct tty_struct* tty;
	struct js_port* port;
	int idx;
	int len;
	unsigned char data[JS_WAR_MAX_LENGTH];
	char used;
};

/*
 * js_war_process_packet() decodes packets the driver receives from the
 * Warrior. It updates the data accordingly.
 */

static void js_war_process_packet(struct js_war_info* info)
{
	int **axes = info->port->axes;
	int **buttons = info->port->buttons;
	unsigned char *data = info->data;
	int i;

	if (!info->idx) return;

	switch ((data[0] >> 4) & 7) {

		case 1:					/* Button data */
			if (!info->port->devs[0]) return;
			buttons[0][0] = ((data[3] & 0xa) >> 1) | ((data[3] & 0x5) << 1);
			return;
		case 3:					/* XY-axis info->data */
			if (!info->port->devs[0]) return;
			axes[0][0] = ((data[0] & 8) << 5) - (data[2] | ((data[0] & 4) << 5));
			axes[0][1] = (data[1] | ((data[0] & 1) << 7)) - ((data[0] & 2) << 7);
			return;
			break;
		case 5:					/* Throttle, spinner, hat info->data */
			if (!info->port->devs[0]) return;
			axes[0][2] = (data[1] | ((data[0] & 1) << 7)) - ((data[0] & 2) << 7);
			axes[0][3] = (data[3] & 2 ? 1 : 0) - (info->data[3] & 1 ? 1 : 0);
			axes[0][4] = (data[3] & 8 ? 1 : 0) - (info->data[3] & 4 ? 1 : 0);
			axes[0][5] = (data[2] | ((data[0] & 4) << 5)) - ((data[0] & 8) << 5);
			return;
		case 2:					/* Static status (Send !S to get one) */
		case 4:					/* Dynamic status */
			return;
		default:
			printk("joy-warrior: Unknown packet %d length %d:", (data[0] >> 4) & 7, info->idx);
			for (i = 0; i < info->idx; i++)
				printk(" %02x", data[i]);
			printk("\n");
			return;
	}
}

/*
 * js_war_open() is a callback from the joystick device open routine.
 */

static int js_war_open(struct js_dev *jd)
{
	struct js_war_info *info = jd->port->info;
	info->used++;
	return 0;
}

/*
 * js_war_close() is a callback from the joystick device release routine.
 */

static int js_war_close(struct js_dev *jd)
{
	struct js_war_info *info = jd->port->info;
	if (!--info->used) {
		js_unregister_device(jd->port->devs[0]);
		js_war_port = js_unregister_port(jd->port);
	}
	return 0;
}

/*
 * js_war_init_corr() initializes the correction values for the Warrior.
 */

static void __init js_war_init_corr(struct js_corr **corr)
{
	int i;

	for (i = 0; i < 6; i++) {
		corr[0][i].type = JS_CORR_BROKEN;
		corr[0][i].prec = 0;
		corr[0][i].coef[0] = -8;
		corr[0][i].coef[1] = 8;
		corr[0][i].coef[2] = (1 << 29) / (128 - 64);
		corr[0][i].coef[3] = (1 << 29) / (128 - 64);
	}

	corr[0][2].coef[2] = (1 << 29) / (128 - 16);
	corr[0][2].coef[3] = (1 << 29) / (128 - 16);

	for (i = 3; i < 5; i++) {
		corr[0][i].coef[0] = 0;
		corr[0][i].coef[1] = 0;
		corr[0][i].coef[2] = (1 << 29);
		corr[0][i].coef[3] = (1 << 29);
	}

	corr[0][5].prec = -1;
	corr[0][5].coef[0] = 0;
	corr[0][5].coef[1] = 0;
	corr[0][5].coef[2] = (1 << 29) / 128;
	corr[0][5].coef[3] = (1 << 29) / 128;
}

/*
 * js_war_ldisc_open() is the routine that is called upon setting our line
 * discipline on a tty.
 */

static int js_war_ldisc_open(struct tty_struct *tty)
{
	struct js_war_info iniinfo;
	struct js_war_info *info = &iniinfo;

	MOD_INC_USE_COUNT;

	info->tty = tty;
	info->idx = 0;
	info->len = 0;
	info->used = 1;

	js_war_port = js_register_port(js_war_port, info, 1, sizeof(struct js_war_info), NULL);

	info = js_war_port->info;
	info->port = js_war_port;
	tty->disc_data = info;

	printk(KERN_INFO "js%d: WingMan Warrior on %s%d\n",
		js_register_device(js_war_port, 0, 6, 4, "WingMan Warrior", THIS_MODULE, js_war_open, js_war_close),
		tty->driver.name, MINOR(tty->device) - tty->driver.minor_start);

	js_war_init_corr(js_war_port->corr);

	return 0;
}

/*
 * js_war_ldisc_close() is the opposite of js_war_ldisc_open()
 */

static void js_war_ldisc_close(struct tty_struct *tty)
{
	struct js_war_info* info = (struct js_war_info*) tty->disc_data;
	if (!--info->used) {
		js_unregister_device(info->port->devs[0]);
		js_war_port = js_unregister_port(info->port);
	}
	MOD_DEC_USE_COUNT;
}

/*
 * js_war_ldisc_receive() is called by the low level driver when characters
 * are ready for us. We then buffer them for further processing, or call the
 * packet processing routine.
 */

static void js_war_ldisc_receive(struct tty_struct *tty, const unsigned char *cp, char *fp, int count)
{
	struct js_war_info* info = (struct js_war_info*) tty->disc_data;
	int i;

	for (i = 0; i < count; i++) {
		if (cp[i] & 0x80) {
			if (info->idx)
				js_war_process_packet(info);
			info->idx = 0;
			info->len = js_war_lengths[(cp[i] >> 4) & 7];
		}

		if (info->idx < JS_WAR_MAX_LENGTH)
			info->data[info->idx++] = cp[i];

		if (info->idx == info->len) {
			if (info->idx)
				js_war_process_packet(info);	
			info->idx = 0;
			info->len = 0;
		}
	}
}

/*
 * js_war_ldisc_room() reports how much room we do have for receiving data.
 * Although we in fact have infinite room, we need to specify some value
 * here, so why not the size of our packet buffer. It's big anyway.
 */

static int js_war_ldisc_room(struct tty_struct *tty)
{
	return JS_WAR_MAX_LENGTH;
}

/*
 * The line discipline structure.
 */

static struct tty_ldisc js_war_ldisc = {
        magic:          TTY_LDISC_MAGIC,
        name:           "warrior",
	open:		js_war_ldisc_open,
	close:		js_war_ldisc_close,
	receive_buf:	js_war_ldisc_receive,
	receive_room:	js_war_ldisc_room,
};

/*
 * The functions for inserting/removing us as a module.
 */

#ifdef MODULE
int init_module(void)
#else
int __init js_war_init(void)
#endif
{
        if (tty_register_ldisc(N_JOYSTICK_WAR, &js_war_ldisc)) {
                printk(KERN_ERR "joy-warrior: Error registering line discipline.\n");
		return -ENODEV;
	}

	return  0;
}

#ifdef MODULE
void cleanup_module(void)
{
	tty_register_ldisc(N_JOYSTICK_WAR, NULL);
}
#endif
