/*
 *  joy-spaceorb.c  Version 0.1
 *
 *  Copyright (c) 1998 David Thompson
 *  Copyright (c) 1999 Vojtech Pavlik
 *
 *  Sponsored by SuSE
 */

/*
 * This is a module for the Linux joystick driver, supporting
 * the SpaceTec SpaceOrb 360 and SpaceBall Avenger 6dof controllers.
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
#include <linux/tty.h>
#include <linux/init.h>

/*
 * Constants.
 */

#define	N_JOYSTICK_ORB		15
#define JS_ORB_MAX_LENGTH	64

/*
 * List of SpaceOrbs.
 */

static struct js_port* js_orb_port = NULL;

/*
 * Per-Orb data.
 */

struct js_orb_info {
	struct tty_struct* tty;
	struct js_port* port;
	int idx;
	unsigned char data[JS_ORB_MAX_LENGTH];
	int js;
	char used;
};

static unsigned char js_orb_xor[] = "SpaceWare";

static unsigned char *js_orb_errors[] = { "EEPROM storing 0 failed", "Receive queue overflow", "Transmit queue timeout",
		"Bad packet", "Power brown-out", "EEPROM checksum error", "Hardware fault" }; 

/*
 * js_orb_process_packet() decodes packets the driver receives from the
 * SpaceOrb.
 */

static void js_orb_process_packet(struct js_orb_info* info)
{
	int i;
	int **axes = info->port->axes;
	int **buttons = info->port->buttons;
	unsigned char *data = info->data;
	unsigned char c = 0;

	if (info->idx < 2) return;
	for (i = 0; i < info->idx; i++) c ^= data[i];
	if (c) return;

	switch (info->data[0]) {

		case 'R':				/* Reset packet */
			info->data[info->idx - 1] = 0;
			for (i = 1; i < info->idx && info->data[i] == ' '; i++);
			printk(KERN_INFO "js%d: SpaceOrb 360 [%s] on %s%d\n",
				info->js, info->data + i, info->tty->driver.name,
				MINOR(info->tty->device) - info->tty->driver.minor_start);
			break;

		case 'D':				/* Ball + button data */
			if (info->idx != 12) return;
			if (!info->port->devs[0]) return;
			for (i = 0; i < 9; i++) info->data[i+2] ^= js_orb_xor[i]; 
			axes[0][0] = ( data[2]         << 3) | (data[ 3] >> 4);
			axes[0][1] = ((data[3] & 0x0f) << 6) | (data[ 4] >> 1);
			axes[0][2] = ((data[4] & 0x01) << 9) | (data[ 5] << 2) | (data[4] >> 5);
			axes[0][3] = ((data[6] & 0x1f) << 5) | (data[ 7] >> 2);
			axes[0][4] = ((data[7] & 0x03) << 8) | (data[ 8] << 1) | (data[7] >> 6);
			axes[0][5] = ((data[9] & 0x3f) << 4) | (data[10] >> 3);
			for(i = 0; i < 6; i ++) if (axes[0][i] & 0x200) axes[0][i] -= 1024;
			buttons[0][0] = data[1];
			break;

		case 'K':				/* Button data */
			if (info->idx != 5) return;
			if (!info->port->devs[0]) return;
			buttons[0][0] = data[2];
			break;

		case 'E':				/* Error packet */
			if (info->idx != 4) return;
			printk(KERN_ERR "joy-spaceorb: Device error. [ ");
			for (i = 0; i < 7; i++)
				if (data[1] & (1 << i))
					printk("%s ", js_orb_errors[i]);
			printk("]\n");
			break;

		case 'N':				/* Null region */
			if (info->idx != 3) return;
			break;

		case 'P':				/* Pulse (update) speed */
			if (info->idx != 4) return;
			break;

		default:
			printk("joy-spaceorb: Unknown packet %d length %d:", data[0], info->idx);
			for (i = 0; i < info->idx; i++) printk(" %02x", data[i]);
			printk("\n");
			return;
	}
}

/*
 * js_orb_open() is a callback from the joystick device open routine.
 */

static int js_orb_open(struct js_dev *jd)
{
	struct js_orb_info *info = jd->port->info;
	info->used++;
	MOD_INC_USE_COUNT;
	return 0;
}

/*
 * js_orb_close() is a callback from the joystick device release routine.
 */

static int js_orb_close(struct js_dev *jd)
{
	struct js_orb_info *info = jd->port->info;
	if (!--info->used) {
		js_unregister_device(jd->port->devs[0]);
		js_orb_port = js_unregister_port(jd->port);
	}
	MOD_DEC_USE_COUNT;
	return 0;
}

/*
 * js_orb_init_corr() initializes the correction values for the SpaceOrb.
 */

static void __init js_orb_init_corr(struct js_corr **corr)
{
	int j;

	for (j = 0; j < 6; j++) {
		corr[0][j].type = JS_CORR_BROKEN;
		corr[0][j].prec = 0;
		corr[0][j].coef[0] = 0 ;
		corr[0][j].coef[1] = 0 ;
		corr[0][j].coef[2] = (1 << 29) / 511;
		corr[0][j].coef[3] = (1 << 29) / 511;
	}
}

/*
 * js_orb_ldisc_open() is the routine that is called upon setting our line
 * discipline on a tty.
 */

static int js_orb_ldisc_open(struct tty_struct *tty)
{
	struct js_orb_info iniinfo;
	struct js_orb_info *info = &iniinfo;

	info->tty = tty;
	info->idx = 0;
	info->used = 1;

	js_orb_port = js_register_port(js_orb_port, info, 1, sizeof(struct js_orb_info), NULL);
	
	info = js_orb_port->info;
	info->port = js_orb_port;
	tty->disc_data = info;

	info->js = js_register_device(js_orb_port, 0, 6, 7, "SpaceOrb 360", js_orb_open, js_orb_close);

	js_orb_init_corr(js_orb_port->corr);

	MOD_INC_USE_COUNT;

	return 0;
}

/*
 * js_orb_ldisc_close() is the opposite of js_orb_ldisc_open()
 */

static void js_orb_ldisc_close(struct tty_struct *tty)
{
	struct js_orb_info* info = (struct js_orb_info*) tty->disc_data;
	if (!--info->used) {
		js_unregister_device(info->port->devs[0]);
		js_orb_port = js_unregister_port(info->port);
	}
	MOD_DEC_USE_COUNT;
}

/*
 * js_orb_ldisc_receive() is called by the low level driver when characters
 * are ready for us. We then buffer them for further processing, or call the
 * packet processing routine.
 */

static void js_orb_ldisc_receive(struct tty_struct *tty, const unsigned char *cp, char *fp, int count)
{
	struct js_orb_info* info = (struct js_orb_info*) tty->disc_data;
	int i;

	for (i = 0; i < count; i++) {
		if (~cp[i] & 0x80) {
			if (info->idx) js_orb_process_packet(info);
			info->idx = 0;
		}
		if (info->idx < JS_ORB_MAX_LENGTH)
			info->data[info->idx++] = cp[i] & 0x7f;
	}
}

/*
 * js_orb_ldisc_room() reports how much room we do have for receiving data.
 * Although we in fact have infinite room, we need to specify some value
 * here, so why not the size of our packet buffer. It's big anyway.
 */

static int js_orb_ldisc_room(struct tty_struct *tty)
{
	return JS_ORB_MAX_LENGTH;
}

/*
 * The line discipline structure.
 */

static struct tty_ldisc js_orb_ldisc = {
	magic:		TTY_LDISC_MAGIC,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,0)
	name:		"spaceorb",
#endif
	open:		js_orb_ldisc_open,
	close:		js_orb_ldisc_close,
	receive_buf:	js_orb_ldisc_receive,
	receive_room:	js_orb_ldisc_room,
};

/*
 * The functions for inserting/removing us as a module.
 */

#ifdef MODULE
int init_module(void)
#else
int __init js_orb_init(void)
#endif
{
        if (tty_register_ldisc(N_JOYSTICK_ORB, &js_orb_ldisc)) {
                printk(KERN_ERR "joy-spaceorb: Error registering line discipline.\n");
		return -ENODEV;
	}

	return  0;
}

#ifdef MODULE
void cleanup_module(void)
{
	tty_register_ldisc(N_JOYSTICK_ORB, NULL);
}
#endif
