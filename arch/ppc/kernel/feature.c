/*
 *  arch/ppc/kernel/feature.c
 *
 *  Copyright (C) 1996 Paul Mackerras (paulus@cs.anu.edu.au)
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */
#include <linux/types.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/errno.h>
#include <asm/ohare.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/feature.h>

#define MAX_FEATURE_REGS		2
#undef DEBUG_FEATURE

static u32 feature_bits_pbook[] = {
	0,			/* FEATURE_null */
	OH_SCC_RESET,		/* FEATURE_Serial_reset */
	OH_SCC_ENABLE,		/* FEATURE_Serial_enable */
	OH_SCCA_IO,		/* FEATURE_Serial_IO_A */
	OH_SCCB_IO,		/* FEATURE_Serial_IO_B */
	OH_FLOPPY_ENABLE,	/* FEATURE_SWIM3_enable */
	OH_MESH_ENABLE,		/* FEATURE_MESH_enable */
	OH_IDE_ENABLE,		/* FEATURE_IDE_enable */
	OH_VIA_ENABLE,		/* FEATURE_VIA_enable */
	OH_IDECD_POWER,		/* FEATURE_CD_power */
	OH_BAY_RESET,		/* FEATURE_Mediabay_reset */
	OH_BAY_ENABLE,		/* FEATURE_Mediabay_enable */
	OH_BAY_PCI_ENABLE,	/* FEATURE_Mediabay_PCI_enable */
	OH_BAY_IDE_ENABLE,	/* FEATURE_Mediabay_IDE_enable */
	OH_BAY_FLOPPY_ENABLE,	/* FEATURE_Mediabay_floppy_enable */
	0,			/* FEATURE_BMac_reset */
	0,			/* FEATURE_BMac_IO_enable */
	0,			/* FEATURE_Modem_PowerOn -> guess...*/
	0			/* FEATURE_Modem_Reset -> guess...*/
};

/* assume these are the same as the ohare until proven otherwise */
static u32 feature_bits_heathrow[] = {
	0,			/* FEATURE_null */
	OH_SCC_RESET,		/* FEATURE_Serial_reset */
	OH_SCC_ENABLE,		/* FEATURE_Serial_enable */
	OH_SCCA_IO,		/* FEATURE_Serial_IO_A */
	OH_SCCB_IO,		/* FEATURE_Serial_IO_B */
	OH_FLOPPY_ENABLE,	/* FEATURE_SWIM3_enable */
	OH_MESH_ENABLE,		/* FEATURE_MESH_enable */
	OH_IDE_ENABLE,		/* FEATURE_IDE_enable */
	OH_VIA_ENABLE,		/* FEATURE_VIA_enable */
	OH_IDECD_POWER,		/* FEATURE_CD_power */
	OH_BAY_RESET,		/* FEATURE_Mediabay_reset */
	OH_BAY_ENABLE,		/* FEATURE_Mediabay_enable */
	OH_BAY_PCI_ENABLE,	/* FEATURE_Mediabay_PCI_enable */
	OH_BAY_IDE_ENABLE,	/* FEATURE_Mediabay_IDE_enable */
	OH_BAY_FLOPPY_ENABLE,	/* FEATURE_Mediabay_floppy_enable */
	0x80000000,		/* FEATURE_BMac_reset */
	0x60000000,		/* FEATURE_BMac_IO_enable */
	0x02000000,		/* FEATURE_Modem_PowerOn -> guess...*/
	0x07000000		/* FEATURE_Modem_Reset -> guess...*/
};

/* definition of a feature controller object */
struct feature_controller
{
	u32*			bits;
	volatile u32*		reg;
	struct device_node*	device;
};

/* static functions */
static void
feature_add_controller(struct device_node *controller_device, u32* bits);

static int
feature_lookup_controller(struct device_node *device);

/* static varialbles */
static struct feature_controller	controllers[MAX_FEATURE_REGS];
static int				controller_count = 0;


void
feature_init(void)
{
	struct device_node *np;

	np = find_devices("mac-io");
	while (np != NULL)
	{
		feature_add_controller(np, feature_bits_heathrow);
		np = np->next;
	}
	if (controller_count == 0)
	{
		np = find_devices("ohare");
		if (np)
		{
			if (find_devices("via-pmu") != NULL)
				feature_add_controller(np, feature_bits_pbook);
			else
				/* else not sure; maybe this is a Starmax? */
				feature_add_controller(np, NULL);
		}
	}

	if (controller_count)
		printk(KERN_INFO "Registered %d feature controller(s)\n", controller_count);
}

static void
feature_add_controller(struct device_node *controller_device, u32* bits)
{
	struct feature_controller*	controller;
	
	if (controller_count >= MAX_FEATURE_REGS)
	{
		printk(KERN_INFO "Feature controller %s skipped(MAX:%d)\n",
			controller_device->full_name, MAX_FEATURE_REGS);
		return;
	}
	controller = &controllers[controller_count];

	controller->bits	= bits;
	controller->device	= controller_device;
	if (controller_device->n_addrs == 0) {
		printk(KERN_ERR "No addresses for %s\n",
			controller_device->full_name);
		return;
	}

	controller->reg		= (volatile u32 *)ioremap(
		controller_device->addrs[0].address + OHARE_FEATURE_REG, 4);

	if (bits == NULL) {
		printk(KERN_INFO "Twiddling the magic ohare bits\n");
		out_le32(controller->reg, STARMAX_FEATURES);
		return;
	}

	controller_count++;
}

static int
feature_lookup_controller(struct device_node *device)
{
	int	i;
	
	if (device == NULL)
		return -EINVAL;
		
	while(device)
	{
		for (i=0; i<controller_count; i++)
			if (device == controllers[i].device)
				return i;
		device = device->parent;
	}

#ifdef DEBUG_FEATURE
	printk("feature: <%s> not found on any controller\n",
		device->name);
#endif
	
	return -ENODEV;
}

int
feature_set(struct device_node* device, enum system_feature f)
{
	int		controller;
	unsigned long	flags;

	if (f >= FEATURE_last)
		return -EINVAL;	

	controller = feature_lookup_controller(device);
	if (controller < 0)
		return controller;
	
#ifdef DEBUG_FEATURE
	printk("feature: <%s> setting feature %d in controller @0x%x\n",
		device->name, (int)f, (unsigned int)controllers[controller].reg);
#endif

	save_flags(flags);
	cli();
	st_le32( controllers[controller].reg,
		 ld_le32(controllers[controller].reg) |
		 controllers[controller].bits[f]);
	restore_flags(flags);
	udelay(10);
	
	return 0;
}

int
feature_clear(struct device_node* device, enum system_feature f)
{
	int		controller;
	unsigned long	flags;

	if (f >= FEATURE_last)
		return -EINVAL;	

	controller = feature_lookup_controller(device);
	if (controller < 0)
		return controller;
	
#ifdef DEBUG_FEATURE
	printk("feature: <%s> clearing feature %d in controller @0x%x\n",
		device->name, (int)f, (unsigned int)controllers[controller].reg);
#endif

	save_flags(flags);
	cli();
	st_le32( controllers[controller].reg,
		 ld_le32(controllers[controller].reg) &
		 ~(controllers[controller].bits[f]));
	restore_flags(flags);
	udelay(10);
	
	return 0;
}

int
feature_test(struct device_node* device, enum system_feature f)
{
	int		controller;

	if (f >= FEATURE_last)
		return -EINVAL;	

	controller = feature_lookup_controller(device);
	if (controller < 0)
		return controller;
	
	return (ld_le32(controllers[controller].reg) &
		controllers[controller].bits[f]) != 0;
}

