/*
 *  joy-sidewinder.c  Version 1.2
 *
 *  Copyright (c) 1998-1999 Vojtech Pavlik
 *
 *  Sponsored by SuSE
 */

/*
 * This is a module for the Linux joystick driver, supporting
 * Microsoft SideWinder digital joystick family.
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
#include <linux/init.h>

/*
 * These are really magic values. Changing them can make a problem go away,
 * as well as break everything.
 */

#undef JS_SW_DEBUG

#define JS_SW_START		400	/* The time we wait for the first bit [400 us] */
#define JS_SW_STROBE		45	/* Max time per bit [45 us] */
#define JS_SW_TIMEOUT		4000	/* Wait for everything to settle [4 ms] */
#define JS_SW_KICK		45	/* Wait after A0 fall till kick [45 us] */
#define JS_SW_END		8	/* Number of bits before end of packet to kick */
#define JS_SW_FAIL		16	/* Number of packet read errors to fail and reinitialize */
#define JS_SW_BAD		2	/* Number of packet read errors to switch off 3d Pro optimization */
#define JS_SW_OK		64	/* Number of packet read successes to switch optimization back on */
#define JS_SW_LENGTH		512	/* Max number of bits in a packet */

/*
 * SideWinder joystick types ...
 */

#define JS_SW_TYPE_3DP		1
#define JS_SW_TYPE_F23		2
#define JS_SW_TYPE_GP		3
#define JS_SW_TYPE_PP		4
#define JS_SW_TYPE_FFP		5
#define JS_SW_TYPE_FSP		6
#define JS_SW_TYPE_FFW		7

static int js_sw_port_list[] __initdata = {0x201, 0};
static struct js_port* js_sw_port __initdata = NULL;

static struct {
	int x;
	int y;
} js_sw_hat_to_axis[] = {{ 0, 0}, { 0,-1}, { 1,-1}, { 1, 0}, { 1, 1}, { 0, 1}, {-1, 1}, {-1, 0}, {-1,-1}};

struct js_sw_info {
	int io;
	int length;
	int speed;
	unsigned char type;
	unsigned char bits;
	unsigned char number;
	unsigned char fail;
	unsigned char ok;
};

/*
 * Gameport speed.
 */

unsigned int js_sw_io_speed = 0;

/*
 * js_sw_measure_speed() measures the gameport i/o speed.
 */

static int __init js_sw_measure_speed(int io)
{
#ifdef __i386__

#define GET_TIME(x)     do { outb(0, 0x43); x = inb(0x40); x |= inb(0x40) << 8; } while (0)
#define DELTA(x,y)      ((y)-(x)+((y)<(x)?1193180L/HZ:0))

	unsigned int i, t, t1, t2, t3, tx;
	unsigned long flags;

	tx = 1 << 30;

	for(i = 0; i < 50; i++) {
		save_flags(flags);	/* Yes, all CPUs */
		cli();
		GET_TIME(t1);
		for(t = 0; t < 50; t++) inb(io);
		GET_TIME(t2);
		GET_TIME(t3);
		restore_flags(flags);
		udelay(i * 10);
		if ((t = DELTA(t2,t1) - DELTA(t3,t2)) < tx) tx = t;
	}

	return 59659 / t;

#else

	unsigned int j, t = 0;

	j = jiffies; while (j == jiffies);
	j = jiffies; while (j == jiffies) { t++; inb(0x201); }

	return t * HZ / 1000;

#endif
}

/*
 * js_sw_read_packet() is a function which reads either a data packet, or an
 * identification packet from a SideWinder joystick. Better don't try to
 * understand this, since all the ugliness of the Microsoft Digital
 * Overdrive protocol is concentrated in this function. If you really want
 * to know how this works, first go watch a couple horror movies, so that
 * you are well prepared, read US patent #5628686 and then e-mail me,
 * and I'll send you an explanation.
 *					Vojtech <vojtech@suse.cz>
 */

static int js_sw_read_packet(int io, int speed, unsigned char *buf, int length, int id)
{
	unsigned long flags;
	int timeout, bitout, sched, i, kick, start, strobe;
	unsigned char pending, u, v;

	i = -id;						/* Don't care about data, only want ID */
	timeout = id ? (JS_SW_TIMEOUT * speed) >> 10 : 0;	/* Set up global timeout for ID packet */
	kick = id ? (JS_SW_KICK * speed) >> 10 : 0;		/* Set up kick timeout for ID packet */
	start = (JS_SW_START * speed) >> 10;
	strobe = (JS_SW_STROBE * speed) >> 10;
	bitout = start;
	pending = 0;
	sched = 0;

        __save_flags(flags);					/* Quiet, please */
        __cli();

	outb(0xff, io);						/* Trigger */
	v = inb(io);

	do {
		bitout--;
		u = v;
		v = inb(io);
	} while (!(~v & u & 0x10) && (bitout > 0));		/* Wait for first falling edge on clock */

	if (bitout > 0) bitout = strobe;			/* Extend time if not timed out */

	while ((timeout > 0 || bitout > 0) && (i < length)) {

		timeout--;
		bitout--;					/* Decrement timers */
		sched--;

		u = v;
		v = inb(io);

		if ((~u & v & 0x10) && (bitout > 0)) {		/* Rising edge on clock - data bit */
			if (i >= 0)				/* Want this data */
				buf[i] = v >> 5;		/* Store it */
			i++;					/* Advance index */
			bitout = strobe;			/* Extend timeout for next bit */
		} 

		if (kick && (~v & u & 0x01)) {			/* Falling edge on axis 0 */
			sched = kick;				/* Schedule second trigger */
			kick = 0;				/* Don't schedule next time on falling edge */
			pending = 1;				/* Mark schedule */
		} 

		if (pending && sched < 0 && (i > -JS_SW_END)) {	/* Second trigger time */
			outb(0xff, io);				/* Trigger */
			bitout = start;				/* Long bit timeout */
			pending = 0;				/* Unmark schedule */
			timeout = 0;				/* Switch from global to bit timeouts */ 
		}
	}

	__restore_flags(flags);					/* Done - relax */

#ifdef JS_SW_DEBUG
	{
		int j;
		printk(KERN_DEBUG "joy-sidewinder: Read %d triplets. [", i);
		for (j = 0; j < i; j++) printk("%d", buf[j]);
		printk("]\n");
	}
#endif

	return i;
}

/*
 * js_sw_get_bits() and GB() compose bits from the triplet buffer into a __u64.
 * Parameter 'pos' is bit number inside packet where to start at, 'num' is number
 * of bits to be read, 'shift' is offset in the resulting __u64 to start at, bits
 * is number of bits per triplet.
 */

#define GB(pos,num,shift) js_sw_get_bits(buf, pos, num, shift, info->bits)

static __u64 js_sw_get_bits(unsigned char *buf, int pos, int num, char shift, char bits)
{
	__u64 data = 0;
	int tri = pos % bits;						/* Start position */
	int i   = pos / bits;
	int bit = shift;

	while (num--) {
		data |= (__u64)((buf[i] >> tri++) & 1) << bit++;	/* Transfer bit */
		if (tri == bits) {
			i++;						/* Next triplet */
			tri = 0;
		}
	}

	return data;
}

/*
 * js_sw_init_digital() initializes a SideWinder 3D Pro joystick
 * into digital mode.
 */

static void js_sw_init_digital(int io, int speed)
{
	int seq[] = { 140, 140+725, 140+300, 0 };
	unsigned long flags;
	int i, t;

        __save_flags(flags);
        __cli();

	i = 0;
        do {
                outb(0xff, io);					/* Trigger */
		t = (JS_SW_TIMEOUT * speed) >> 10;
		while ((inb(io) & 1) && t) t--;			/* Wait for axis to fall back to 0 */
                udelay(seq[i]);					/* Delay magic time */
        } while (seq[++i]);

	outb(0xff, io);						/* Last trigger */

	__restore_flags(flags);
}

/*
 * js_sw_parity() computes parity of __u64
 */

static int js_sw_parity(__u64 t)
{
	int x = t ^ (t >> 32);
	x ^= x >> 16;
	x ^= x >> 8;
	x ^= x >> 4;
	x ^= x >> 2;
	x ^= x >> 1;
	return x & 1;
}

/*
 * js_sw_ccheck() checks synchronization bits and computes checksum of nibbles.
 */

static int js_sw_check(__u64 t)
{
	char sum = 0;

	if ((t & 0x8080808080808080ULL) ^ 0x80)			/* Sync */
		return -1;

	while (t) {						/* Sum */
		sum += t & 0xf;
		t >>= 4;
	}

	return sum & 0xf;
}

/*
 * js_sw_parse() analyzes SideWinder joystick data, and writes the results into
 * the axes and buttons arrays.
 */

static int js_sw_parse(unsigned char *buf, struct js_sw_info *info, int **axes, int **buttons)
{
	int hat, i;

	switch (info->type) {

		case JS_SW_TYPE_3DP:
		case JS_SW_TYPE_F23:

			if (js_sw_check(GB(0,64,0)) || (hat = GB(6,1,3) | GB(60,3,0))  > 8) return -1;

			axes[0][0] = GB( 3,3,7) | GB(16,7,0);
			axes[0][1] = GB( 0,3,7) | GB(24,7,0);
			axes[0][2] = GB(35,2,7) | GB(40,7,0);
			axes[0][3] = GB(32,3,7) | GB(48,7,0);
			axes[0][4] = js_sw_hat_to_axis[hat].x;
			axes[0][5] = js_sw_hat_to_axis[hat].y;
			buttons[0][0] = ~(GB(37,1,8) | GB(38,1,7) | GB(8,7,0));

			return 0;

		case JS_SW_TYPE_GP:

			for (i = 0; i < info->number * 15; i += 15) {

				if (js_sw_parity(GB(i,15,0))) return -1;

				axes[i][0] = GB(i+3,1,0) - GB(i+2,1,0);
				axes[i][1] = GB(i+0,1,0) - GB(i+1,1,0);
				buttons[i][0] = ~GB(i+4,10,0);

			}

			return 0;

		case JS_SW_TYPE_PP:
		case JS_SW_TYPE_FFP:

			if (!js_sw_parity(GB(0,48,0)) || (hat = GB(42,4,0)) > 8) return -1;

			axes[0][0] = GB( 9,10,0);
			axes[0][1] = GB(19,10,0);
			axes[0][2] = GB(36, 6,0);
			axes[0][3] = GB(29, 7,0);
			axes[0][4] = js_sw_hat_to_axis[hat].x;
			axes[0][5] = js_sw_hat_to_axis[hat].y;
			buttons[0][0] = ~GB(0,9,0);

			return 0;

		case JS_SW_TYPE_FSP:

			if (!js_sw_parity(GB(0,43,0)) || (hat = GB(28,4,0)) > 8) return -1;

			axes[0][0] = GB( 0,10,0);
			axes[0][1] = GB(16,10,0);
			axes[0][2] = GB(32, 6,0);
			axes[0][3] = js_sw_hat_to_axis[hat].x;
			axes[0][4] = js_sw_hat_to_axis[hat].y;
			buttons[0][0] = ~(GB(10,6,0) | GB(26,2,6) | GB(38,2,8));

			return 0;

		case JS_SW_TYPE_FFW:

			if (!js_sw_parity(GB(0,33,0))) return -1;

			axes[0][0] = GB( 0,10,0);
			axes[0][1] = GB(10, 6,0);
			axes[0][2] = GB(16, 6,0);
			buttons[0][0] = ~GB(22,8,0);

			return 0;
	}

	return -1;
}

/*
 * js_sw_read() reads SideWinder joystick data, and reinitializes
 * the joystick in case of persistent problems. This is the function that is
 * called from the generic code to poll the joystick.
 */

static int js_sw_read(void *xinfo, int **axes, int **buttons)
{
	struct js_sw_info *info = xinfo;
	unsigned char buf[JS_SW_LENGTH];
	int i;

	i = js_sw_read_packet(info->io, info->speed, buf, info->length, 0);

	if (info->type <= JS_SW_TYPE_F23 && info->length == 66 && i != 66) {	/* Broken packet, try to fix */

		if (i == 64 && !js_sw_check(js_sw_get_bits(buf,0,64,0,1))) {	/* Last init failed, 1 bit mode */
			printk(KERN_WARNING "joy-sidewinder: Joystick in wrong mode on %#x"
				" - going to reinitialize.\n", info->io);
			info->fail = JS_SW_FAIL;				/* Reinitialize */
			i = 128;						/* Bogus value */
		}

		if (i < 66 && GB(0,64,0) == GB(i*3-66,64,0))			/* 1 == 3 */
			i = 66;							/* Everything is fine */

		if (i < 66 && GB(0,64,0) == GB(66,64,0))			/* 1 == 2 */
			i = 66;							/* Everything is fine */

		if (i < 66 && GB(i*3-132,64,0) == GB(i*3-66,64,0)) {		/* 2 == 3 */
			memmove(buf, buf + i - 22, 22);				/* Move data */
			i = 66;							/* Carry on */
		}
	}

	if (i == info->length && !js_sw_parse(buf, info, axes, buttons)) {	/* Parse data */

		info->fail = 0;
		info->ok++;

		if (info->type <= JS_SW_TYPE_F23 && info->length == 66		/* Many packets OK */
			&& info->ok > JS_SW_OK) {

			printk(KERN_INFO "joy-sidewinder: No more trouble on %#x"
				" - enabling optimization again.\n", info->io);
			info->length = 22;
		}

		return 0;
	}

	info->ok = 0;
	info->fail++;

	if (info->type <= JS_SW_TYPE_F23 && info->length == 22			/* Consecutive bad packets */
			&& info->fail > JS_SW_BAD) {

		printk(KERN_INFO "joy-sidewinder: Many bit errors on %#x"
			" - disabling optimization.\n", info->io);
		info->length = 66;
	}

	if (info->fail < JS_SW_FAIL) return -1;					/* Not enough, don't reinitialize yet */

	printk(KERN_WARNING "joy-sidewinder: Too many bit errors on %#x"
		" - reinitializing joystick.\n", info->io);

	if (!i && info->type <= JS_SW_TYPE_F23) {				/* 3D Pro can be in analog mode */
		udelay(3 * JS_SW_TIMEOUT);
		js_sw_init_digital(info->io, info->speed);
	}

	udelay(JS_SW_TIMEOUT);
	i = js_sw_read_packet(info->io, info->speed, buf, JS_SW_LENGTH, 0);	/* Read normal data packet */
	udelay(JS_SW_TIMEOUT);
	js_sw_read_packet(info->io, info->speed, buf, JS_SW_LENGTH, i);		/* Read ID packet, this initializes the stick */

	info->fail = JS_SW_FAIL;
	
	return -1;
}

/*
 * js_sw_init_corr() initializes the correction values for
 * SideWinders.
 */

static void __init js_sw_init_corr(int num_axes, int type, int number, struct js_corr **corr)
{
	int i, j;

	for (i = 0; i < number; i++) {

		for (j = 0; j < num_axes; j++) {
			corr[i][j].type = JS_CORR_BROKEN;
			corr[i][j].prec = 8;
			corr[i][j].coef[0] = 511 - 32;
			corr[i][j].coef[1] = 512 + 32;
			corr[i][j].coef[2] = (1 << 29) / (511 - 32);
			corr[i][j].coef[3] = (1 << 29) / (511 - 32);
		}

		switch (type) {

			case JS_SW_TYPE_3DP:
			case JS_SW_TYPE_F23:

				corr[i][2].type = JS_CORR_BROKEN;
				corr[i][2].prec = 4;
				corr[i][2].coef[0] = 255 - 16;
				corr[i][2].coef[1] = 256 + 16;
				corr[i][2].coef[2] = (1 << 29) / (255 - 16);
				corr[i][2].coef[3] = (1 << 29) / (255 - 16);

				j = 4;

			break;

			case JS_SW_TYPE_PP:
			case JS_SW_TYPE_FFP:

				corr[i][2].type = JS_CORR_BROKEN;
				corr[i][2].prec = 0;
				corr[i][2].coef[0] = 31 - 2;
				corr[i][2].coef[1] = 32 + 2;
				corr[i][2].coef[2] = (1 << 29) / (31 - 2);
				corr[i][2].coef[3] = (1 << 29) / (31 - 2);

				corr[i][3].type = JS_CORR_BROKEN;
				corr[i][3].prec = 1;
				corr[i][3].coef[0] = 63 - 4;
				corr[i][3].coef[1] = 64 + 4;
				corr[i][3].coef[2] = (1 << 29) / (63 - 4);
				corr[i][3].coef[3] = (1 << 29) / (63 - 4);

				j = 4;

			break;

			case JS_SW_TYPE_FFW:

				corr[i][0].type = JS_CORR_BROKEN;
				corr[i][0].prec = 2;
				corr[i][0].coef[0] = 511 - 8;
				corr[i][0].coef[1] = 512 + 8;
				corr[i][0].coef[2] = (1 << 29) / (511 - 8);
				corr[i][0].coef[3] = (1 << 29) / (511 - 8);

				corr[i][1].type = JS_CORR_BROKEN;
				corr[i][1].prec = 1;
				corr[i][1].coef[0] = 63;
				corr[i][1].coef[1] = 63;
				corr[i][1].coef[2] = (1 << 29) / -63;
				corr[i][1].coef[3] = (1 << 29) / -63;

				corr[i][2].type = JS_CORR_BROKEN;
				corr[i][2].prec = 1;
				corr[i][2].coef[0] = 63;
				corr[i][2].coef[1] = 63;
				corr[i][2].coef[2] = (1 << 29) / -63;
				corr[i][2].coef[3] = (1 << 29) / -63;

				j = 3;

			break;

			case JS_SW_TYPE_FSP:
				
				corr[i][2].type = JS_CORR_BROKEN;
				corr[i][2].prec = 0;
				corr[i][2].coef[0] = 31 - 2;
				corr[i][2].coef[1] = 32 + 2;
				corr[i][2].coef[2] = (1 << 29) / (31 - 2);
				corr[i][2].coef[3] = (1 << 29) / (31 - 2);

				j = 3;

			break;

			default:

				j = 0;
		}

		for (; j < num_axes; j++) {				/* Hats & other binary axes */
			corr[i][j].type = JS_CORR_BROKEN;
			corr[i][j].prec = 0;
			corr[i][j].coef[0] = 0;
			corr[i][j].coef[1] = 0;
			corr[i][j].coef[2] = (1 << 29);
			corr[i][j].coef[3] = (1 << 29);
		}
	}
}

/*
 * js_sw_print_packet() prints the contents of a SideWinder packet.
 */

static void js_sw_print_packet(char *name, int length, unsigned char *buf, char bits)
{
	int i;

	printk("joy-sidewinder: %s packet, %d bits. [", name, length);
	for (i = (((length + 3) >> 2) - 1); i >= 0; i--)
		printk("%x", (int)js_sw_get_bits(buf, i << 2, 4, 0, bits));
	printk("]\n");
}

/*
 * js_sw_3dp_id() translates the 3DP id into a human legible string.
 * Unfortunately I don't know how to do this for the other SW types.
 */

static void js_sw_3dp_id(unsigned char *buf, char *comment)
{
	int i;
	char pnp[8], rev[9];

	for (i = 0; i < 7; i++)						/* ASCII PnP ID */
		pnp[i] = js_sw_get_bits(buf, 24+8*i, 8, 0, 1);

	for (i = 0; i < 8; i++)						/* ASCII firmware revision */
		rev[i] = js_sw_get_bits(buf, 88+8*i, 8, 0, 1);

	pnp[7] = rev[8] = 0;

	sprintf(comment, " [PnP %d.%02d id %s rev %s]",
		(int) (js_sw_get_bits(buf, 8, 6, 6, 1) |		/* Two 6-bit values */
			js_sw_get_bits(buf, 16, 6, 0, 1)) / 100,
		(int) (js_sw_get_bits(buf, 8, 6, 6, 1) |
			js_sw_get_bits(buf, 16, 6, 0, 1)) % 100,
		 pnp, rev);
}

/*
 * js_sw_guess_mode() checks the upper two button bits for toggling -
 * indication of that the joystick is in 3-bit mode. This is documented
 * behavior for 3DP ID packet, and for example the FSP does this in
 * normal packets instead. Fun ...
 */

static int js_sw_guess_mode(unsigned char *buf, int len)
{
	int i;
	unsigned char xor = 0;
	for (i = 1; i < len; i++) xor |= (buf[i - 1] ^ buf[i]) & 6;
	return !!xor * 2 + 1;
}

/*
 * js_sw_probe() probes for SideWinder type joysticks.
 */

static struct js_port __init *js_sw_probe(int io, struct js_port *port)
{
	struct js_sw_info info;
	char *names[] = {NULL, "SideWinder 3D Pro", "Flight2000 F-23", "SideWinder GamePad", "SideWinder Precision Pro",
			"SideWinder Force Feedback Pro", "SideWinder FreeStyle Pro", "SideWinder Force Feedback Wheel" };
	char axes[] = { 0, 6, 6, 2, 6, 6, 5, 3 };
	char buttons[] = { 0, 9, 9, 10, 9, 9, 10, 8 };
	int i, j, k, l, speed;
	unsigned char buf[JS_SW_LENGTH];
	unsigned char idbuf[JS_SW_LENGTH];
	unsigned char m = 1;
	char comment[40];

	comment[0] = 0;

	if (check_region(io, 1)) return port;

	speed = js_sw_measure_speed(io);

	i = js_sw_read_packet(io, speed, buf, JS_SW_LENGTH, 0);		/* Read normal packet */
	m |= js_sw_guess_mode(buf, i);					/* Data packet (1-bit) can carry mode info [FSP] */
	udelay(JS_SW_TIMEOUT);

#ifdef JS_SW_DEBUG
	printk(KERN_DEBUG "joy-sidewinder: Init 1: Mode %d. Length %d.\n", m , i);
#endif

	if (!i) {							/* No data. 3d Pro analog mode? */
		js_sw_init_digital(io, speed);				/* Switch to digital */
		udelay(JS_SW_TIMEOUT);
		i = js_sw_read_packet(io, speed, buf, JS_SW_LENGTH, 0);	/* Retry reading packet */
		udelay(JS_SW_TIMEOUT);
#ifdef JS_SW_DEBUG
	printk(KERN_DEBUG "joy-sidewinder: Init 1b: Length %d.\n", i);
#endif
		if (!i) return port;					/* No data -> FAIL */
	}

	j = js_sw_read_packet(io, speed, idbuf, JS_SW_LENGTH, i);	/* Read ID. This initializes the stick */
	m |= js_sw_guess_mode(idbuf, j);				/* ID packet should carry mode info [3DP] */

#ifdef JS_SW_DEBUG
	printk(KERN_DEBUG "joy-sidewinder: Init 2: Mode %d. ID Length %d.\n", m , j);
#endif

	if (!j) {							/* Read ID failed. Happens in 1-bit mode on PP */
		udelay(JS_SW_TIMEOUT);
		i = js_sw_read_packet(io, speed, buf, JS_SW_LENGTH, 0);	/* Retry reading packet */
#ifdef JS_SW_DEBUG
	printk(KERN_DEBUG "joy-sidewinder: Init 2b: Mode %d. Length %d.\n", m , i);
#endif
		if (!i) return port;
		udelay(JS_SW_TIMEOUT);
		j = js_sw_read_packet(io, speed, idbuf, JS_SW_LENGTH, i);/* Retry reading ID */
#ifdef JS_SW_DEBUG
	printk(KERN_DEBUG "joy-sidewinder: Init 2c: ID Length %d.\n", j);
#endif

	}

	k = JS_SW_FAIL;							/* Try JS_SW_FAIL times */
	l = 0;

	do {
		k--;
		udelay(JS_SW_TIMEOUT);
		i = js_sw_read_packet(io, speed, buf, JS_SW_LENGTH, 0);	/* Read data packet */
#ifdef JS_SW_DEBUG
		printk(KERN_DEBUG "joy-sidewinder: Init 3: Length %d.\n", i);
#endif

		if (i > l) {						/* Longer? As we can only lose bits, it makes */
									/* no sense to try detection for a packet shorter */
			l = i;						/* than the previous one */

			info.number = 1;
			info.io = io;
			info.speed = speed;
			info.length = i;
			info.bits = m;
			info.fail = 0;
			info.ok = 0;
			info.type = 0;

			switch (i * m) {
				case 60:
					info.number++;
				case 45:				/* Ambiguous packet length */
					if (j <= 40) {			/* ID length less or eq 40 -> FSP */	
				case 43:
						info.type = JS_SW_TYPE_FSP;
						break;
					}
					info.number++;
				case 30:
					info.number++;
				case 15:
					info.type = JS_SW_TYPE_GP;
					break;
				case 33:
				case 31:
					info.type = JS_SW_TYPE_FFW;
					break;
				case 48:				/* Ambiguous */
					if (j == 14) {			/* ID lenght 14*3 -> FFP */
						info.type = JS_SW_TYPE_FFP;
						sprintf(comment, " [AC %s]", js_sw_get_bits(idbuf,38,1,0,3) ? "off" : "on");
					} else
					info.type = JS_SW_TYPE_PP;
					break;
				case 198:
					info.length = 22;
				case 64:
					info.type = JS_SW_TYPE_3DP;
					if (j == 160) js_sw_3dp_id(idbuf, comment);
					break;
			}
		}

	} while (k && !info.type);

	if (!info.type) {
		printk(KERN_WARNING "joy-sidewinder: unknown joystick device detected "
			"(io=%#x), contact <vojtech@suse.cz>\n", io);
		js_sw_print_packet("ID", j * 3, idbuf, 3);
		js_sw_print_packet("Data", i * m, buf, m);
		return port;
	}

#ifdef JS_SW_DEBUG
	js_sw_print_packet("ID", j * 3, idbuf, 3);
	js_sw_print_packet("Data", i * m, buf, m);
#endif

	k = i;

	request_region(io, 1, "joystick (sidewinder)");

	port = js_register_port(port, &info, info.number, sizeof(struct js_sw_info), js_sw_read);

	for (i = 0; i < info.number; i++)
		printk(KERN_INFO "js%d: %s%s at %#x [%d ns res %d-bit id %d data %d]\n",
			js_register_device(port, i, axes[info.type], buttons[info.type],
				names[info.type], THIS_MODULE, NULL, NULL), names[info.type], comment, io,
				1000000 / speed, m, j, k);

	js_sw_init_corr(axes[info.type], info.type, info.number, port->corr);

	return port;
}

#ifdef MODULE
int init_module(void)
#else
int __init js_sw_init(void)
#endif
{
	int *p;

	for (p = js_sw_port_list; *p; p++) js_sw_port = js_sw_probe(*p, js_sw_port);
	if (js_sw_port) return 0;

#ifdef MODULE
	printk(KERN_WARNING "joy-sidewinder: no joysticks found\n");
#endif

	return -ENODEV;
}

#ifdef MODULE
void cleanup_module(void)
{
	int i;
	struct js_sw_info *info;

	while (js_sw_port) {
		for (i = 0; i < js_sw_port->ndevs; i++)
			if (js_sw_port->devs[i])
				js_unregister_device(js_sw_port->devs[i]);
		info = js_sw_port->info;
		release_region(info->io, 1);
		js_sw_port = js_unregister_port(js_sw_port);
	}

}
#endif
