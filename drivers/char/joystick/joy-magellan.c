/*
 *  joy-magellan.c  Version 0.1
 *
 *  Copyright (c) 1999 Vojtech Pavlik
 *
 *  Sponsored by SuSE
 */

/*
 * This is a module for the Linux joystick driver, supporting
 * the Magellan and Space Mouse 6dof controllers.
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
#include <linux/delay.h>
#include <linux/tty.h>
#include <linux/init.h>

/*
 * Constants.
 */

#define	N_JOYSTICK_MAG		14
#define JS_MAG_MAX_LENGTH	64

/*
 * List of Magellans.
 */

static struct js_port* js_mag_port = NULL;

/*
 * Per-Magellan data.
 */

struct js_mag_info {
	struct tty_struct* tty;
	struct js_port* port;
	int idx;
	unsigned char data[JS_MAG_MAX_LENGTH];
	unsigned char name[JS_MAG_MAX_LENGTH];
	char ack;
	char used;
};

/*
 * js_mag_crunch_nibbles() verifies that the bytes sent from the Magellan
 * have correct upper nibbles for the lower ones, if not, the packet will
 * be thrown away. It also strips these upper halves to simplify further
 * processing.
 */

static int js_mag_crunch_nibbles(unsigned char *data, int count)
{
	static unsigned char nibbles[16] = "0AB3D56GH9:K<MN?";

	do {
		if (data[count] == nibbles[data[count] & 0xf])
			data[count] = data[count] & 0xf;
		else
			return -1;
	} while (--count);

	return 0;
}

/*
 * js_mag_process_packet() decodes packets the driver receives from the
 * Magellan. It updates the data accordingly, and sets an ACK flag
 * to the type of last packet received, if received OK.
 */

static void js_mag_process_packet(struct js_mag_info* info)
{
	int i;

	if (!info->idx) return;

	switch (info->data[0]) {

		case 'd':				/* Axis data */
			if (info->idx != 25) return;
			if (js_mag_crunch_nibbles(info->data, 24)) return;
			if (!info->port->devs[0]) return;
			for (i = 0; i < 6; i++) {
				info->port->axes[0][i] = 
					( info->data[(i << 2) + 1] << 12 | info->data[(i << 2) + 2] << 8 |
					  info->data[(i << 2) + 3] <<  4 | info->data[(i << 2) + 4] )
					 - 32768;
			}
			break;

		case 'e':				/* Error packet */
			if (info->idx != 4) return;
			if (js_mag_crunch_nibbles(info->data, 3)) return;
			switch (info->data[1]) {
				case 1:
					printk(KERN_ERR "joy-magellan: Received command error packet. Failing command byte: %c\n",
						info->data[2] | (info->data[3] << 4));
					break;
				case 2:
					printk(KERN_ERR "joy-magellan: Received framing error packet.\n");
					break;
				default:
					printk(KERN_ERR "joy-magellan: Received unknown error packet.\n");
			}
			break;

		case 'k':				/* Button data */
			if (info->idx != 4) return;
			if (js_mag_crunch_nibbles(info->data, 3)) return;
			if (!info->port->devs[0]) return;
			info->port->buttons[0][0] = (info->data[1] << 1) | (info->data[2] << 5) | info->data[3];
			break;

		case 'm':				/* Mode */
			if (info->idx != 2) return;
			if (js_mag_crunch_nibbles(info->data, 1)) return; 
			break;

		case 'n':				/* Null radius */
			if (info->idx != 2) return;
			if (js_mag_crunch_nibbles(info->data, 1)) return; 
			break;

		case 'p':				/* Data rate */
			if (info->idx != 3) return;
			if (js_mag_crunch_nibbles(info->data, 2)) return;
			break;

		case 'q':				/* Sensitivity */
			if (info->idx != 3) return;
			if (js_mag_crunch_nibbles(info->data, 2)) return; 
			break;

		case 'v':				/* Version string */
			info->data[info->idx] = 0;
			for (i = 1; i < info->idx && info->data[i] == ' '; i++);
			memcpy(info->name, info->data + i, info->idx - i);
			break;

		case 'z':				/* Zero position */
			break;

		default:
			printk("joy-magellan: Unknown packet %d length %d:", info->data[0], info->idx);
			for (i = 0; i < info->idx; i++) printk(" %02x", info->data[i]);
			printk("\n");
			return;
	}

	info->ack = info->data[0];
}

/*
 * js_mag_command() sends a command to the Magellan, and waits for
 * acknowledge.
 */

static int js_mag_command(struct js_mag_info *info, char *command, int timeout)
{
	info->ack = 0;
	if (info->tty->driver.write(info->tty, 0, command, strlen(command)) != strlen(command)) return -1;
	while (!info->ack && timeout--) mdelay(1);
	return -(info->ack != command[0]);
}

/*
 * js_mag_setup() initializes the Magellan to sane state. Also works as
 * a probe for Magellan existence.
 */

static int js_mag_setup(struct js_mag_info *info)
{

	if (js_mag_command(info, "vQ\r", 800))	/* Read version */
		return -1;
	if (js_mag_command(info, "m3\r", 50))	/* Set full 3d mode */
		return -1;
	if (js_mag_command(info, "pBB\r", 50))	/* Set 16 reports/second (max) */
		return -1;
	if (js_mag_command(info, "z\r", 50))	/* Set zero position */
		return -1;

	return 0;
}

/*
 * js_mag_read() updates the axis and button data upon startup.
 */

static int js_mag_read(struct js_mag_info *info)
{
	memset(info->port->axes[0],0, sizeof(int) * 6);		/* Axes are 0 after zero postition cmd */ 

	if (js_mag_command(info, "kQ\r", 50))			/* Read buttons */
		return -1;

	return 0;
}

/*
 * js_mag_open() is a callback from the joystick device open routine.
 */

static int js_mag_open(struct js_dev *jd)
{
	struct js_mag_info *info = jd->port->info;
	info->used++;	
	MOD_INC_USE_COUNT;
	return 0;
}

/*
 * js_mag_close() is a callback from the joystick device release routine.
 */

static int js_mag_close(struct js_dev *jd)
{
	struct js_mag_info *info = jd->port->info;
	if (!--info->used) {
		js_unregister_device(jd->port->devs[0]);
		js_mag_port = js_unregister_port(jd->port);
	}
	MOD_DEC_USE_COUNT;
	return 0;
}

/*
 * js_mag_init_corr() initializes the correction values for the Magellan.
 * It asumes gain setting of 0, question is, what we should do for higher
 * gain settings ...
 */

static void js_mag_init_corr(struct js_corr **corr)
{
	int i;

	for (i = 0; i < 6; i++) {
		corr[0][i].type = JS_CORR_BROKEN;
		corr[0][i].prec = 0;
		corr[0][i].coef[0] = 0;
		corr[0][i].coef[1] = 0;
		corr[0][i].coef[2] = (1 << 29) / 256;
		corr[0][i].coef[3] = (1 << 29) / 256;
	}
}

/*
 * js_mag_ldisc_open() is the routine that is called upon setting our line
 * discipline on a tty. It looks for the Magellan, and if found, registers
 * it as a joystick device.
 */

static int js_mag_ldisc_open(struct tty_struct *tty)
{
	struct js_mag_info iniinfo;
	struct js_mag_info *info = &iniinfo;

	info->tty = tty;
	info->idx = 0;
	info->used = 1;

	js_mag_port = js_register_port(js_mag_port, info, 1, sizeof(struct js_mag_info), NULL);

	info = js_mag_port->info;
	info->port = js_mag_port;
	tty->disc_data = info;

	if (js_mag_setup(info)) {
		js_mag_port = js_unregister_port(info->port);
		return -ENODEV;
	}

	printk(KERN_INFO "js%d: Magellan [%s] on %s%d\n",
		js_register_device(js_mag_port, 0, 6, 9, "Magellan", js_mag_open, js_mag_close),
		info->name, tty->driver.name, MINOR(tty->device) - tty->driver.minor_start);


	js_mag_read(info);
	js_mag_init_corr(js_mag_port->corr);

	MOD_INC_USE_COUNT;

	return 0;
}

/*
 * js_mag_ldisc_close() is the opposite of js_mag_ldisc_open()
 */

static void js_mag_ldisc_close(struct tty_struct *tty)
{
	struct js_mag_info* info = (struct js_mag_info*) tty->disc_data;
	if (!--info->used) {
		js_unregister_device(info->port->devs[0]);
		js_mag_port = js_unregister_port(info->port);
	}
	MOD_DEC_USE_COUNT;
}

/*
 * js_mag_ldisc_receive() is called by the low level driver when characters
 * are ready for us. We then buffer them for further processing, or call the
 * packet processing routine.
 */

static void js_mag_ldisc_receive(struct tty_struct *tty, const unsigned char *cp, char *fp, int count)
{
	struct js_mag_info* info = (struct js_mag_info*) tty->disc_data;
	int i;

	for (i = 0; i < count; i++)
		if (cp[i] == '\r') {
			js_mag_process_packet(info);
			info->idx = 0;
		} else {
			if (info->idx < JS_MAG_MAX_LENGTH)
				info->data[info->idx++] = cp[i];
		} 
}

/*
 * js_mag_ldisc_room() reports how much room we do have for receiving data.
 * Although we in fact have infinite room, we need to specify some value
 * here, so why not the size of our packet buffer. It's big anyway.
 */

static int js_mag_ldisc_room(struct tty_struct *tty)
{
	return JS_MAG_MAX_LENGTH;
}

/*
 * The line discipline structure.
 */

static struct tty_ldisc js_mag_ldisc = {
        magic:          TTY_LDISC_MAGIC,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,0)
        name:           "magellan",
#endif
	open:		js_mag_ldisc_open,
	close:		js_mag_ldisc_close,
	receive_buf:	js_mag_ldisc_receive,
	receive_room:	js_mag_ldisc_room,
};

/*
 * The functions for inserting/removing us as a module.
 */

#ifdef MODULE
int init_module(void)
#else
int __init js_mag_init(void)
#endif
{
        if (tty_register_ldisc(N_JOYSTICK_MAG, &js_mag_ldisc)) {
                printk(KERN_ERR "joy-magellan: Error registering line discipline.\n");
		return -ENODEV;
	}

	return  0;
}

#ifdef MODULE
void cleanup_module(void)
{
	tty_register_ldisc(N_JOYSTICK_MAG, NULL);
}
#endif
