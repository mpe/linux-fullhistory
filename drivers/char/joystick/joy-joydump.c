/*
 *  joydump.c  Version 1.2
 *
 *  Copyright (c) 1996-1998 Vojtech Pavlik
 */

/*
 * This is just a very simple driver that can dump the data
 * out of the joystick port into the syslog ...
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
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/joystick.h>
#include <linux/kernel.h>
#include <linux/delay.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");

#define JS_PORT 0x201

#ifdef __i386__
static unsigned int get_time(void)
{
	unsigned int x;
	__asm__ __volatile__ ( "rdtsc" : "=A" (x) );
	return x;
}
#else

#ifdef	__alpha__
static unsigned int get_time(void)
{
	unsigned int x;
	__asm__ __volatile__ ( "rpcc %0" : "=r" (x) );
	return x;
}
#else

#error "Sorry, you need a precise timer for this - define some in joydump.c"

#endif
#endif

#define BUF_SIZE 128

struct datatype {
	unsigned int time;
	unsigned char data;
};

static struct datatype buf[BUF_SIZE];

int init_module(void)
{
	int i, j;
	unsigned int t, t1;
	int timeout, div;
	unsigned long flags;
	unsigned char u;

	printk(KERN_INFO "joydump: ,------------------- START ------------------.\n");

/*
 * Calibrate.
 */

	__save_flags(flags);
	__cli();
	t = get_time();
	udelay(1000);
	t1 = get_time();
	__restore_flags(flags);

	timeout = t1 - t;
	div = timeout / 1000;

	printk(KERN_INFO "joydump: | Timer ticks at %d.%02d MHz.                 |\n",
		timeout / 1000, timeout / 10 % 100);

	__save_flags(flags);
	__cli();
	t = get_time();
	inb(JS_PORT); inb(JS_PORT); inb(JS_PORT); inb(JS_PORT); inb(JS_PORT);
	inb(JS_PORT); inb(JS_PORT); inb(JS_PORT); inb(JS_PORT); inb(JS_PORT);
	t1 = get_time();
	__restore_flags(flags);

	printk(KERN_INFO "joydump: | I/O at %#x takes %d.%02d us.                |\n",
		JS_PORT, (t1 - t) / div / 10, (t1 - t) * 10 / div % 100);

	timeout *= 20;

/*
 * Gather data.
 */

	__save_flags(flags);
	__cli();

	u = inb(JS_PORT);
	t = t1 = get_time();
	buf[0].data = u;
	buf[0].time = t1;
	i = 1;

	outb(u,JS_PORT);

	while (i < BUF_SIZE && t1 - t < timeout) {

		t1 = get_time();
		buf[i].data = inb(JS_PORT);

		if (buf[i].data != u) {
			u = buf[i].data;
			buf[i].time = t1;
			i++;
		}
	}

	__restore_flags(flags);

/*
 * Dump data.
 */


	t = i;

	printk(KERN_INFO "joydump: >------------------- DATA -------------------<\n");
	printk(KERN_INFO "joydump: | index: %3d delta: %3d.%02d us data: ", 0, 0, 0);
	for (j = 7; j >= 0; j--)
		printk("%d",(buf[0].data >> j) & 1);
	printk(" |\n");
	for (i = 1; i < t; i++) {
		printk(KERN_INFO "joydump: | index: %3d delta: %3d.%02d us data: ", i,
			(buf[i].time - buf[i-1].time) / div, (buf[i].time - buf[i-1].time) * 100 / div % 100);
		for (j = 7; j >= 0; j--)
			printk("%d",(buf[i].data >> j) & 1);
		printk(" |\n");
	}

	printk(KERN_INFO "joydump: `-------------------- END -------------------'\n");

	return -1;
}

void cleanup_module(void)
{
}
