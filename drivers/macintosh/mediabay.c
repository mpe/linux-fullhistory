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
#define __KERNEL_SYSCALLS__

#include <linux/config.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/hdreg.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <asm/prom.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/feature.h>
#include <asm/mediabay.h>
#include <asm/init.h>


#undef MB_USE_INTERRUPTS

struct media_bay_hw {
	unsigned char	b0;
	unsigned char	contents;
	unsigned char	b2;
	unsigned char	b3;
};

struct media_bay_info {
	volatile struct media_bay_hw*	addr;
	int				content_id;
	int				previous_id;
	int				ready;
	int				last_value;
	int				value_count;
	int				reset_timer;
	struct device_node*		dev_node;
#ifdef CONFIG_BLK_DEV_IDE
	unsigned long			cd_base;
	int 				cd_index;
	int				cd_irq;
	int				cd_timer;
#endif
};

#define MAX_BAYS	2

static volatile struct media_bay_info media_bays[MAX_BAYS];
int media_bay_count = 0;

#define MB_CONTENTS(i)	((in_8(&media_bays[i].addr->contents) >> 4) & 7)

#ifdef CONFIG_BLK_DEV_IDE
/* check the busy bit in the media-bay ide interface
   (assumes the media-bay contains an ide device) */
#define MB_IDE_READY(i)	((in_8((volatile unsigned char *) \
			       (media_bays[i].cd_base + 0x70)) & 0x80) == 0)
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
#define MB_RESET_COUNT	20

/*
 * Wait this many ticks after an IDE device (e.g. CD-ROM) is inserted
 * (or until the device is ready) before registering the IDE interface.
 */
#define MB_IDE_WAIT	1000

static void poll_media_bay(int which);
static void set_media_bay(int which, int id);
static int media_bay_task(void *);

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
	int		n,i;
	
	for (i=0; i<MAX_BAYS; i++)
	{
		memset((char *)&media_bays[i], 0, sizeof(struct media_bay_info));
		media_bays[i].content_id	= -1;
#ifdef CONFIG_BLK_DEV_IDE
		media_bays[i].cd_index		= -1;
#endif
	}
	
	np = find_devices("media-bay");
	n = 0;
	while(np && (n<MAX_BAYS))
	{
		if (np->n_addrs == 0)
			continue;
		media_bays[n].addr = (volatile struct media_bay_hw *)
			ioremap(np->addrs[0].address, sizeof(struct media_bay_hw));

#ifdef MB_USE_INTERRUPTS
		if (np->n_intrs == 0)
		{
			printk(KERN_ERR "media bay %d has no irq\n",n);
			continue;
		}
		
		if (request_irq(np_intrs[0].line, media_bay_intr, 0, "Media bay", NULL))
		{
			printk(KERN_ERR "Couldn't get IRQ %d for media bay %d\n", irq, n);
			continue;
		}
#endif	
		media_bay_count++;
	
		set_media_bay(n, MB_CONTENTS(n));
		if (media_bays[n].content_id != MB_NO) {
			feature_clear(media_bays[n].dev_node, FEATURE_Mediabay_reset);
			udelay(500);
		}
		media_bays[n].ready		= 1;
		media_bays[n].previous_id	= media_bays[n].content_id;
		media_bays[n].reset_timer	= 0;
		media_bays[n].dev_node		= np;
#ifdef CONFIG_BLK_DEV_IDE
		media_bays[n].cd_timer		= 0;
#endif
		n++;
		np=np->next;
	}
	
	if (media_bay_count)
	{
		printk(KERN_INFO "Registered %d media-bay(s)\n", media_bay_count);

		kernel_thread(media_bay_task, NULL, 0);
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
check_media_bay(struct device_node *which_bay, int what)
{
#ifdef CONFIG_BLK_DEV_IDE
	int	i;

	for (i=0; i<media_bay_count; i++)
		if (which_bay == media_bays[i].dev_node)
		{
			if ((what == media_bays[i].content_id) && media_bays[i].ready)
				return 0;
			media_bays[i].cd_index = -1;
			return -EINVAL;
		}
#endif /* CONFIG_BLK_DEV_IDE */
	return -ENODEV;
}

int
check_media_bay_by_base(unsigned long base, int what)
{
#ifdef CONFIG_BLK_DEV_IDE
	int	i;

	for (i=0; i<media_bay_count; i++)
		if (base == media_bays[i].cd_base)
		{
			if ((what == media_bays[i].content_id) && media_bays[i].ready)
				return 0;
			media_bays[i].cd_index = -1;
			return -EINVAL;
		} 
#endif
	
	return -ENODEV;
}

int
media_bay_set_ide_infos(struct device_node* which_bay, unsigned long base,
	int irq, int index)
{
#ifdef CONFIG_BLK_DEV_IDE
	int	i;

	for (i=0; i<media_bay_count; i++)
		if (which_bay == media_bays[i].dev_node)
		{
 			media_bays[i].cd_base	= base;
			media_bays[i].cd_irq	= irq;
			media_bays[i].cd_index	= index;
			printk(KERN_DEBUG "Registered ide %d for media bay %d\n", index, i);			
			return 0;
		} 
#endif
	
	return -ENODEV;
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
	volatile struct media_bay_info* bay;
	int	i = 0;
	
	strcpy(current->comm, "media-bay");
	for (;;)
	{
		bay = &media_bays[i];
		poll_media_bay(i);
		if (bay->content_id != bay->previous_id) {
			bay->reset_timer = (bay->content_id != MB_NO) ?
				MB_RESET_COUNT: 0;
			bay->ready = 0;
#ifdef CONFIG_BLK_DEV_IDE
			bay->cd_timer = 0;
			if (bay->content_id != MB_CD && bay->cd_index >= 0) {
				printk(KERN_DEBUG "Unregistering mb %d ide, index:%d\n", i, bay->cd_index);
				ide_unregister(bay->cd_index);
				bay->cd_index = -1;
			}
#endif
		} else if (bay->reset_timer) {
			if (--bay->reset_timer == 0) {
 				feature_clear(bay->dev_node, FEATURE_Mediabay_reset);
				bay->ready = 1;
#ifdef CONFIG_BLK_DEV_IDE
				bay->cd_timer = 0;
				if (bay->content_id == MB_CD && bay->cd_base != 0)
					bay->cd_timer = MB_IDE_WAIT;
#endif
			}
#ifdef CONFIG_BLK_DEV_IDE
		} else if (bay->cd_timer && (--bay->cd_timer == 0 || MB_IDE_READY(i))
			   && bay->cd_index < 0) {
			bay->cd_timer = 0;
			printk(KERN_DEBUG "Registering IDE, base:0x%08lx, irq:%d\n", bay->cd_base, bay->cd_irq);
			printk("\n");
			bay->cd_index = ide_register(bay->cd_base, 0, bay->cd_irq);
			if (bay->cd_index == -1)
				printk("\nCD-ROM badly inserted. Remove it and try again !\n");
			else
				printk(KERN_DEBUG "media-bay %d is ide %d\n", i, bay->cd_index);
#endif
		}

		bay->previous_id = bay->content_id;
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(1);
		if (signal_pending(current))
			return 0;
		i = (i+1)%media_bay_count;
	}
}

void
poll_media_bay(int which)
{
	int id = MB_CONTENTS(which);

	if (id == media_bays[which].last_value) {
		if (id != media_bays[which].content_id
		    && ++media_bays[which].value_count >= MB_STABLE_COUNT)
 			set_media_bay(which, id);
	} else {
		media_bays[which].last_value = id;
		media_bays[which].value_count = 0;
	}
}

static void
set_media_bay(int which, int id)
{
	volatile struct media_bay_info* bay;

	bay = &media_bays[which];
	
	bay->content_id = id;
	bay->last_value = id;
	
	switch (id) {
	case MB_CD:
		feature_clear(bay->dev_node, FEATURE_Mediabay_floppy_enable);
		feature_set(bay->dev_node, FEATURE_Mediabay_enable);
		feature_set(bay->dev_node, FEATURE_CD_power);
		feature_set(bay->dev_node, FEATURE_Mediabay_IDE_enable);
		printk(KERN_INFO "media bay %d contains a CD-ROM drive\n", which);
		break;
	case MB_FD:
		feature_clear(bay->dev_node, FEATURE_CD_power);
		feature_set(bay->dev_node, FEATURE_Mediabay_enable);
		feature_set(bay->dev_node, FEATURE_Mediabay_floppy_enable);
		feature_set(bay->dev_node, FEATURE_SWIM3_enable);
		printk(KERN_INFO "media bay %d contains a floppy disk drive\n", which);
		break;
	case MB_NO:
		feature_clear(bay->dev_node, FEATURE_Mediabay_floppy_enable);
		feature_clear(bay->dev_node, FEATURE_CD_power);
		printk(KERN_INFO "media bay %d is empty\n", which);
		break;
	default:
		feature_clear(bay->dev_node, FEATURE_Mediabay_floppy_enable);
		feature_clear(bay->dev_node, FEATURE_CD_power);
		feature_set(bay->dev_node, FEATURE_Mediabay_enable);
		printk(KERN_INFO "media bay %d contains an unknown device (%d)\n",
		       which, id);
		break;
	}
	
	udelay(500);
}
