/*
 * kernel/traps.c
 *
 * (C) Copyright 1994 Linus Torvalds
 */

/*
 * This file initializes the trap entry points
 */

#include <linux/sched.h>

#include <asm/system.h>
#include <asm/io.h>

extern asmlinkage void entInt(void);
void keyboard_interrupt(void);

void do_hw_interrupt(unsigned long type, unsigned long vector)
{
	if (type == 1) {
		jiffies++;
		return;
	}
	/* keyboard or mouse */
	if (type == 3) {
		if (vector == 0x980) {
			keyboard_interrupt();
			return;
		} else {
		unsigned char c = inb_local(0x64);
		printk("IO device interrupt, vector = %lx\n", vector);
		if (!(c & 1)) {
			int i;
			printk("Hmm. Keyboard interrupt, status = %02x\n", c);
			for (i = 0; i < 10000000 ; i++)
				/* nothing */;
			printk("Serial line interrupt status: %02x\n", inb_local(0x3fa));
		} else {
			c = inb_local(0x60);
			printk("#%02x# ", c);
		}
		return;
		}
	}
	printk("Hardware intr %ld %ld\n", type, vector);
}

void trap_init(void)
{
	unsigned long gptr;

	__asm__("br %0,___tmp\n"
		"___tmp:\tldgp %0,0(%0)"
		: "=r" (gptr));
	wrkgp(gptr);
	wrent(entInt, 0);
}
