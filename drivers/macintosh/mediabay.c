/*
 * Driver for the media bay on the PowerBook 3400 and 2400.
 *
 * Copyright (C) 1998 Paul Mackerras.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */
#include <linux/config.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/hdreg.h>
#include <asm/prom.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/ohare.h>
#include <asm/mediabay.h>
#include <asm/init.h>

struct media_bay_hw {
	unsigned char	b0;
	unsigned char	contents;
	unsigned char	b2;
	unsigned char	b3;
	unsigned	feature;
};

static volatile struct media_bay_hw *mb_addr;

#define MB_CONTENTS()	((in_8(&mb_addr->contents) >> 4) & 7)
#define SET_FEATURES(set, clr) \
	out_le32(&mb_addr->feature, \
		 (in_le32(&mb_addr->feature) & ~(clr)) | (set));

static int media_bay_id = -1;
static int mb_ready;
static int mb_last_value;
static int mb_value_count;

int media_bay_present;

#ifdef CONFIG_BLK_DEV_IDE
unsigned long mb_cd_base;
int mb_cd_index = -1;
int mb_cd_irq;

/* check the busy bit in the media-bay ide interface
   (assumes the media-bay contains an ide device) */
#define MB_IDE_READY()	((in_8((volatile unsigned char *) \
			       (mb_cd_base + 0x70)) & 0x80) == 0)
#endif

/*
 * Consider the media-bay ID value stable if it is the same for
 * this many consecutive samples (at intervals of 1/HZ seconds).
 */
#define MB_STABLE_COUNT	4

/*
 * Hold the media-bay reset signal true for this many ticks
 * after a device is inserted before releasing it.
 */
#define MB_RESET_COUNT	10

/*
 * Wait this many ticks after an IDE device (e.g. CD-ROM) is inserted
 * (or until the device is ready) before registering the IDE interface.
 */
#define MB_IDE_WAIT	500

static void poll_media_bay(void);
static void set_media_bay(int id);

/*
 * It seems that the bit for the media-bay interrupt in the IRQ_LEVEL
 * register is always set when there is something in the media bay.
 * This causes problems for the interrupt code if we attach an interrupt
 * handler to the media-bay interrupt, because it tends to go into
 * an infinite loop calling the media bay interrupt handler.
 * Therefore we do it all by polling the media bay once each tick.
 */

__pmac /* I don't know of any chrp with a mediabay -- Cort */

void
media_bay_init(void)
{
	struct device_node *np;

	np = find_devices("media-bay");
	if (np == NULL || np->n_addrs == 0)
		return;
	mb_addr = (volatile struct media_bay_hw *)
		ioremap(np->addrs[0].address, sizeof(struct media_bay_hw));

#if 0
	if (np->n_intrs == 0) {
		printk(KERN_WARNING "No interrupt for media bay?\n");
	} else {
		if (request_irq(np->intrs[0].line, media_bay_intr, 0,
				"Media bay", NULL))
			printk(KERN_WARNING "Couldn't get IRQ %d for "
			       "media bay\n", np->intrs[0].line);
	}
#endif

	media_bay_present = 1;
	set_media_bay(MB_CONTENTS());
	if (media_bay_id != MB_NO) {
		SET_FEATURES(0, OH_BAY_RESET);
		mb_ready = 1;
	}
}

#if 0
static void
media_bay_intr(int irq, void *devid, struct pt_regs *regs)
{
	int id = MB_CONTENTS();

	if (id == MB_NO)
		set_media_bay(id);
}
#endif

int
check_media_bay(int what)
{
	return what == media_bay_id && mb_ready;
}

/*
 * This procedure runs as a kernel thread to poll the media bay
 * once each tick and register and unregister the IDE interface
 * with the IDE driver.  It needs to be a thread because
 * ide_register can't be called from interrupt context.
 */
int
media_bay_task(void *x)
{
	int prev = media_bay_id;
	int reset_timer = 0;
#ifdef CONFIG_BLK_DEV_IDE
	int cd_timer = 0;
#endif

	strcpy(current->comm, "media-bay");
	for (;;) {
		poll_media_bay();
		if (media_bay_id != prev) {
			reset_timer = (media_bay_id != MB_NO)?
				MB_RESET_COUNT: 0;
			mb_ready = 0;
#ifdef CONFIG_BLK_DEV_IDE
			cd_timer = 0;
			if (media_bay_id != MB_CD && mb_cd_index >= 0) {
				printk(KERN_DEBUG "Unregistering mb ide\n");
				ide_unregister(mb_cd_index);
				mb_cd_index = -1;
			}
#endif
		} else if (reset_timer) {
			if (--reset_timer == 0) {
				SET_FEATURES(0, OH_BAY_RESET);
				mb_ready = 1;
#ifdef CONFIG_BLK_DEV_IDE
				if (media_bay_id == MB_CD && mb_cd_base != 0)
					cd_timer = MB_IDE_WAIT;
#endif
			}
#ifdef CONFIG_BLK_DEV_IDE
		} else if (cd_timer && (--cd_timer == 0 || MB_IDE_READY())
			   && mb_cd_index < 0) {
			mb_cd_index = ide_register(mb_cd_base, 0, mb_cd_irq);
			printk(KERN_DEBUG "media-bay is ide %d\n", mb_cd_index);
#endif
		}

		prev = media_bay_id;
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(1);
		if (signal_pending(current))
			return 0;
	}
}

void
poll_media_bay(void)
{
	int id = MB_CONTENTS();

	if (id == mb_last_value) {
		if (id != media_bay_id
		    && ++mb_value_count >= MB_STABLE_COUNT)
			set_media_bay(id);
	} else {
		mb_last_value = id;
		mb_value_count = 0;
	}
}

static void
set_media_bay(int id)
{
	u32 clr, set;

	media_bay_id = id;
	mb_last_value = id;
	clr = OH_FLOPPY_ENABLE | OH_IDECD_POWER;
	set = 0;
	switch (id) {
	case MB_CD:
		set = OH_BAY_ENABLE | OH_IDECD_POWER | OH_BAY_IDE_ENABLE;
		printk(KERN_INFO "media bay contains a CD-ROM drive\n");
		break;
	case MB_FD:
		set = OH_BAY_ENABLE | OH_BAY_FLOPPY_ENABLE | OH_FLOPPY_ENABLE;
		printk(KERN_INFO "media bay contains a floppy disk drive\n");
		break;
	case MB_NO:
		printk(KERN_INFO "media bay is empty\n");
		break;
	default:
		set = OH_BAY_ENABLE;
		printk(KERN_INFO "media bay contains an unknown device (%d)\n",
		       id);
		break;
	}

	SET_FEATURES(set, clr);
	printk(KERN_DEBUG "feature reg now %x\n", in_le32(&mb_addr->feature));
}
