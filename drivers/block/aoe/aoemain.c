/* Copyright (c) 2004 Coraid, Inc.  See COPYING for GPL terms. */
/*
 * aoemain.c
 * Module initialization routines, discover timer
 */

#include <linux/hdreg.h>
#include <linux/blkdev.h>
#include <linux/module.h>
#include "aoe.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sam Hopkins <sah@coraid.com>");
MODULE_DESCRIPTION("AoE block/char driver for 2.6.[0-9]+");
MODULE_VERSION(VERSION);

enum { TINIT, TRUN, TKILL };

static void
discover_timer(ulong vp)
{
	static struct timer_list t;
	static volatile ulong die;
	static spinlock_t lock;
	ulong flags;
	enum { DTIMERTICK = HZ * 60 }; /* one minute */

	switch (vp) {
	case TINIT:
		init_timer(&t);
		spin_lock_init(&lock);
		t.data = TRUN;
		t.function = discover_timer;
		die = 0;
	case TRUN:
		spin_lock_irqsave(&lock, flags);
		if (!die) {
			t.expires = jiffies + DTIMERTICK;
			add_timer(&t);
		}
		spin_unlock_irqrestore(&lock, flags);

		aoecmd_cfg(0xffff, 0xff);
		return;
	case TKILL:
		spin_lock_irqsave(&lock, flags);
		die = 1;
		spin_unlock_irqrestore(&lock, flags);

		del_timer_sync(&t);
	default:
		return;
	}
}

static void __exit
aoe_exit(void)
{
	discover_timer(TKILL);

	aoenet_exit();
	aoeblk_exit();
	aoechr_exit();
	aoedev_exit();
}

static int __init
aoe_init(void)
{
	int n, (**p)(void);
	int (*fns[])(void) = {
		aoedev_init, aoechr_init, aoeblk_init, aoenet_init, NULL
	};

	for (p=fns; *p != NULL; p++) {
		n = (*p)();
		if (n) {
			aoe_exit();
			printk(KERN_INFO "aoe: aoe_init: initialisation failure.\n");
			return n;
		}
	}
	printk(KERN_INFO
	       "aoe: aoe_init: AoE v2.6-%s initialised.\n",
	       VERSION);

	discover_timer(TINIT);
	return 0;
}

module_init(aoe_init);
module_exit(aoe_exit);

