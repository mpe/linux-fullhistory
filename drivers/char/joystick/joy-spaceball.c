/*
 *  joy-spaceball.c  Version 0.1
 *
 *  Copyright (c) 1998 David Thompson
 *  Copyright (c) 1999 Vojtech Pavlik
 *  Copyright (c) 1999 Joseph Krahn
 *
 *  Sponsored by SuSE
 */

/*
 * This is a module for the Linux joystick driver, supporting
 * the SpaceTec SpaceBall 4000 FLX.
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

#define	N_JOYSTICK_SBALL	12
#define JS_SBALL_MAX_LENGTH	128

/*
 * List of SpaceBalls.
 */

static struct js_port* js_sball_port = NULL;

/*
 * Per-Ball data.
 */

struct js_sball_info {
	struct tty_struct* tty;
	struct js_port* port;
	int idx;
	unsigned char data[JS_SBALL_MAX_LENGTH];
	int js;
	char used;
};

/*
 * js_sball_process_packet() decodes packets the driver receives from the
 * SpaceBall.
 */

static void js_sball_process_packet(struct js_sball_info* info)
{
	int i,b;
	int **axes = info->port->axes;
	int **buttons = info->port->buttons;
	unsigned char *data = info->data;

	if (info->idx < 2) return;

	switch (info->data[0]) {

		case '@':					/* Reset packet */
			info->data[info->idx - 1] = 0;
			for (i = 1; i < info->idx && info->data[i] == ' '; i++);

			printk(KERN_INFO "js%d: SpaceBall 4000FLX [%s] on %s%d\n",
				info->js, info->data + i, info->tty->driver.name,
				MINOR(info->tty->device) - info->tty->driver.minor_start);

			memset(axes[0], 0, sizeof(int) * 6);	/* All axes, buttons should be zero */
			buttons[0][0] = 0;
			break;

		case 'D':					/* Ball data */
			if (info->idx != 16) return;
			if (!info->port->devs[0]) return;
			axes[0][0] = ((data[3] << 8) | data[4] );
			axes[0][1] = ((data[5] << 8) | data[6] );
			axes[0][2] = ((data[7] << 8) | data[8] );
			axes[0][3] = ((data[9] << 8) | data[10]);
			axes[0][4] = ((data[11]<< 8) | data[12]);
			axes[0][5] = ((data[13]<< 8) | data[14]);
			for(i = 0; i < 6; i ++) if (axes[0][i] & 0x8000) axes[0][i] -= 0x10000;
			break;

		case 'K':				/* Button data, part1 */
			/* We can ignore this packet for the SB 4000FLX. */
			break;

		case '.':				/* Button data, part2 */
			if (info->idx != 4) return;
			if (!info->port->devs[0]) return;
			b = (data[1] & 0xbf) << 8 | (data[2] & 0xbf);
			buttons[0][0] = ((b & 0x1f80) >> 1 | (b & 0x3f));
			break;

		case '?':				/* Error packet */
			info->data[info->idx - 1] = 0;
			printk(KERN_ERR "joy-spaceball: Device error. [%s]\n",info->data+1);
			break;

		case 'A':				/* reply to A command (ID# report) */
		case 'B':				/* reply to B command (beep) */
		case 'H':				/* reply to H command (firmware report) */
		case 'S':				/* reply to S command (single beep) */
		case 'Y':				/* reply to Y command (scale flag) */
		case '"':				/* reply to "n command (report info, part n) */
			break;

		case 'P':				/* Pulse (update) speed */
			if (info->idx != 3) return;     /* data[2],data[3] = hex digits for speed 00-FF */
			break;

		default:
			printk("joy-spaceball: Unknown packet %d length %d:", data[0], info->idx);
			for (i = 0; i < info->idx; i++) printk(" %02x", data[i]);
			printk("\n");
			return;
	}
}

/*
 * js_sball_open() is a callback from the joystick device open routine.
 */

static int js_sball_open(struct js_dev *jd)
{
	struct js_sball_info *info = jd->port->info;
	info->used++;
	MOD_INC_USE_COUNT;
	return 0;
}

/*
 * js_sball_close() is a callback from the joystick device release routine.
 */

static int js_sball_close(struct js_dev *jd)
{
	struct js_sball_info *info = jd->port->info;
	if (!--info->used) {
		js_unregister_device(jd->port->devs[0]);
		js_sball_port = js_unregister_port(jd->port);
	}
	MOD_DEC_USE_COUNT;
	return 0;
}

/*
 * js_sball_init_corr() initializes the correction values for the SpaceBall.
 */

static void __init js_sball_init_corr(struct js_corr **corr)
{
	int j;

	for (j = 0; j < 3; j++) {
		corr[0][j].type = JS_CORR_BROKEN;
		corr[0][j].prec = 0;
		corr[0][j].coef[0] = 0;
		corr[0][j].coef[1] = 0;
		corr[0][j].coef[2] = 50000;
		corr[0][j].coef[3] = 50000;
	}
	for (j = 3; j < 6; j++) {
		corr[0][j].type = JS_CORR_BROKEN;
		corr[0][j].prec = 0;
		corr[0][j].coef[0] = 0;
		corr[0][j].coef[1] = 0;
		corr[0][j].coef[2] = 300000;
		corr[0][j].coef[3] = 300000;
	}
}

/*
 * js_sball_ldisc_open() is the routine that is called upon setting our line
 * discipline on a tty.
 */

static int js_sball_ldisc_open(struct tty_struct *tty)
{
	struct js_sball_info iniinfo;
	struct js_sball_info *info = &iniinfo;

	info->tty = tty;
	info->idx = 0;
	info->used = 1;

	js_sball_port = js_register_port(js_sball_port, info, 1, sizeof(struct js_sball_info), NULL);
	
	info = js_sball_port->info;
	info->port = js_sball_port;
	tty->disc_data = info;

	info->js = js_register_device(js_sball_port, 0, 6, 12, "SpaceBall 4000 FLX", js_sball_open, js_sball_close);

	js_sball_init_corr(js_sball_port->corr);

	MOD_INC_USE_COUNT;

	return 0;
}

/*
 * js_sball_ldisc_close() is the opposite of js_sball_ldisc_open()
 */

static void js_sball_ldisc_close(struct tty_struct *tty)
{
	struct js_sball_info* info = (struct js_sball_info*) tty->disc_data;
	if (!--info->used) {
		js_unregister_device(info->port->devs[0]);
		js_sball_port = js_unregister_port(info->port);
	}
	MOD_DEC_USE_COUNT;
}

/*
 * js_sball_ldisc_receive() is called by the low level driver when characters
 * are ready for us. We then buffer them for further processing, or call the
 * packet processing routine.
 */

static void js_sball_ldisc_receive(struct tty_struct *tty, const unsigned char *cp, char *fp, int count)
{
	struct js_sball_info* info = (struct js_sball_info*) tty->disc_data;
	int i;
	int esc_flag = 0;

/*
 * Spaceball 4000 FLX packets all start with a one letter packet-type decriptor,
 * and end in 0x0d.  It uses '^' as an escape for 0x0d characters which can
 * occur in the axis values.  ^M, ^Q and ^S all mean 0x0d, depending (I think)
 * on whether the axis value is increasing, decreasing, or same as before.
 * (I don't see why this is useful).
 *
 * There may be a nicer whay to handle the escapes, but I wanted to be sure to
 * allow for an escape at the end of the buffer.
 */
	for (i = 0; i < count; i++) {
		if (esc_flag) {	 /* If the last char was an escape, overwrite it with the escaped value */

			switch (cp[i]){
				case 'M':
                        	case 'Q':
                        	case 'S':
                                	info->data[info->idx]=0x0d;
                                	break;
                        	case '^': /* escaped escape; leave as is */
                                	break;
                        	default:
					printk("joy-spaceball: Unknown escape character: %02x\n", cp[i]);
			}

			esc_flag = 0;

		} else {

			if (info->idx < JS_SBALL_MAX_LENGTH)
				info->data[info->idx++] = cp[i];

			if (cp[i] == 0x0D) {
				if (info->idx)
					js_sball_process_packet(info);
				info->idx = 0;
			} else
				if (cp[i] == '^') esc_flag = 1;

		}
	}
}

/*
 * js_sball_ldisc_room() reports how much room we do have for receiving data.
 * Although we in fact have infinite room, we need to specify some value
 * here, so why not the size of our packet buffer. It's big anyway.
 */

static int js_sball_ldisc_room(struct tty_struct *tty)
{
	return JS_SBALL_MAX_LENGTH;
}

/*
 * The line discipline structure.
 */

static struct tty_ldisc js_sball_ldisc = {
	magic:		TTY_LDISC_MAGIC,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,0)
	name:		"spaceball",
#endif
	open:		js_sball_ldisc_open,
	close:		js_sball_ldisc_close,
	receive_buf:	js_sball_ldisc_receive,
	receive_room:	js_sball_ldisc_room,
};

/*
 * The functions for inserting/removing us as a module.
 */

#ifdef MODULE
int init_module(void)
#else
int __init js_sball_init(void)
#endif
{
        if (tty_register_ldisc(N_JOYSTICK_SBALL, &js_sball_ldisc)) {
                printk(KERN_ERR "joy-spaceball: Error registering line discipline.\n");
		return -ENODEV;
	}

	return  0;
}

#ifdef MODULE
void cleanup_module(void)
{
	tty_register_ldisc(N_JOYSTICK_SBALL, NULL);
}
#endif
