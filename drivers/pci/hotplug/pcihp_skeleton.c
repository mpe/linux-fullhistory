/*
 * PCI Hot Plug Controller Skeleton Driver - 0.2
 *
 * Copyright (C) 2001,2003 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (C) 2001,2003 IBM Corp.
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * This driver is to be used as a skeleton driver to be show how to interface
 * with the pci hotplug core easily.
 *
 * Send feedback to <greg@kroah.com>
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/init.h>
#include "pci_hotplug.h"


#define SLOT_MAGIC	0x67267322
struct slot {
	u32 magic;
	u8 number;
	struct hotplug_slot *hotplug_slot;
	struct list_head slot_list;
};

static LIST_HEAD(slot_list);

#if !defined(CONFIG_HOTPLUG_PCI_SKELETON_MODULE)
	#define MY_NAME	"pcihp_skeleton"
#else
	#define MY_NAME	THIS_MODULE->name
#endif

#define dbg(format, arg...)					\
	do {							\
		if (debug)					\
			printk (KERN_DEBUG "%s: " format "\n",	\
				MY_NAME , ## arg); 		\
	} while (0)
#define err(format, arg...) printk(KERN_ERR "%s: " format "\n", MY_NAME , ## arg)
#define info(format, arg...) printk(KERN_INFO "%s: " format "\n", MY_NAME , ## arg)
#define warn(format, arg...) printk(KERN_WARNING "%s: " format "\n", MY_NAME , ## arg)



/* local variables */
static int debug;
static int num_slots;

#define DRIVER_VERSION	"0.2"
#define DRIVER_AUTHOR	"Greg Kroah-Hartman <greg@kroah.com>"
#define DRIVER_DESC	"Hot Plug PCI Controller Skeleton Driver"

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
MODULE_PARM(debug, "i");
MODULE_PARM_DESC(debug, "Debugging mode enabled or not");

static int enable_slot		(struct hotplug_slot *slot);
static int disable_slot		(struct hotplug_slot *slot);
static int set_attention_status (struct hotplug_slot *slot, u8 value);
static int hardware_test	(struct hotplug_slot *slot, u32 value);
static int get_power_status	(struct hotplug_slot *slot, u8 *value);
static int get_attention_status	(struct hotplug_slot *slot, u8 *value);
static int get_latch_status	(struct hotplug_slot *slot, u8 *value);
static int get_adapter_status	(struct hotplug_slot *slot, u8 *value);

static struct hotplug_slot_ops skel_hotplug_slot_ops = {
	.owner =		THIS_MODULE,
	.enable_slot =		enable_slot,
	.disable_slot =		disable_slot,
	.set_attention_status =	set_attention_status,
	.hardware_test =	hardware_test,
	.get_power_status =	get_power_status,
	.get_attention_status =	get_attention_status,
	.get_latch_status =	get_latch_status,
	.get_adapter_status =	get_adapter_status,
};


/* Inline functions to check the sanity of a pointer that is passed to us */
static inline int slot_paranoia_check (struct slot *slot, const char *function)
{
	if (!slot) {
		dbg("%s - slot == NULL", function);
		return -1;
	}
	if (slot->magic != SLOT_MAGIC) {
		dbg("%s - bad magic number for slot", function);
		return -1;
	}
	if (!slot->hotplug_slot) {
		dbg("%s - slot->hotplug_slot == NULL!", function);
		return -1;
	}
	return 0;
}

static inline struct slot *get_slot (struct hotplug_slot *hotplug_slot, const char *function)
{ 
	struct slot *slot;

	if (!hotplug_slot) {
		dbg("%s - hotplug_slot == NULL\n", function);
		return NULL;
	}

	slot = (struct slot *)hotplug_slot->private;
	if (slot_paranoia_check (slot, function))
                return NULL;
	return slot;
}               


static int enable_slot (struct hotplug_slot *hotplug_slot)
{
	struct slot *slot = get_slot (hotplug_slot, __FUNCTION__);
	int retval = 0;
	
	if (slot == NULL)
		return -ENODEV;
	
	dbg ("%s - physical_slot = %s\n", __FUNCTION__, hotplug_slot->name);

	/*
	 * Fill in code here to enable the specified slot
	 */

	return retval;
}


static int disable_slot (struct hotplug_slot *hotplug_slot)
{
	struct slot *slot = get_slot (hotplug_slot, __FUNCTION__);
	int retval = 0;
	
	if (slot == NULL)
		return -ENODEV;
	
	dbg ("%s - physical_slot = %s\n", __FUNCTION__, hotplug_slot->name);

	/*
	 * Fill in code here to disable the specified slot
	 */

	return retval;
}

static int set_attention_status (struct hotplug_slot *hotplug_slot, u8 status)
{
	struct slot *slot = get_slot (hotplug_slot, __FUNCTION__);
	int retval = 0;
	
	if (slot == NULL)
		return -ENODEV;
	
	dbg ("%s - physical_slot = %s\n", __FUNCTION__, hotplug_slot->name);

	switch (status) {
		case 0:
			/*
			 * Fill in code here to turn light off
			 */
			break;

		case 1:
		default:
			/*
			 * Fill in code here to turn light on
			 */
			break;
	}

	return retval;
}

static int hardware_test (struct hotplug_slot *hotplug_slot, u32 value)
{
	struct slot *slot = get_slot (hotplug_slot, __FUNCTION__);
	int retval = 0;
	
	if (slot == NULL)
		return -ENODEV;
	
	dbg ("%s - physical_slot = %s\n", __FUNCTION__, hotplug_slot->name);

	err ("No hardware tests are defined for this driver");
	retval = -ENODEV;

	/* Or you can specify a test if you want to */
	
	return retval;
}

static int get_power_status (struct hotplug_slot *hotplug_slot, u8 *value)
{
	struct slot *slot = get_slot (hotplug_slot, __FUNCTION__);
	int retval = 0;
	
	if (slot == NULL)
		return -ENODEV;
	
	dbg(__FUNCTION__" - physical_slot = %s\n", hotplug_slot->name);

	/*
	 * Fill in logic to get the current power status of the specific
	 * slot and store it in the *value location.
	 */

	return retval;
}

static int get_attention_status (struct hotplug_slot *hotplug_slot, u8 *value)
{
	struct slot *slot = get_slot (hotplug_slot, __FUNCTION__);
	int retval = 0;
	
	if (slot == NULL)
		return -ENODEV;
	
	dbg(__FUNCTION__" - physical_slot = %s\n", hotplug_slot->name);

	/*
	 * Fill in logic to get the current attention status of the specific
	 * slot and store it in the *value location.
	 */

	return retval;
}

static int get_latch_status (struct hotplug_slot *hotplug_slot, u8 *value)
{
	struct slot *slot = get_slot (hotplug_slot, __FUNCTION__);
	int retval = 0;
	
	if (slot == NULL)
		return -ENODEV;
	
	dbg(__FUNCTION__" - physical_slot = %s\n", hotplug_slot->name);

	/*
	 * Fill in logic to get the current latch status of the specific
	 * slot and store it in the *value location.
	 */

	return retval;
}

static int get_adapter_status (struct hotplug_slot *hotplug_slot, u8 *value)
{
	struct slot *slot = get_slot (hotplug_slot, __FUNCTION__);
	int retval = 0;
	
	if (slot == NULL)
		return -ENODEV;
	
	dbg(__FUNCTION__" - physical_slot = %s\n", hotplug_slot->name);

	/*
	 * Fill in logic to get the current adapter status of the specific
	 * slot and store it in the *value location.
	 */

	return retval;
}

static void release_slots(struct hotplug_slot *hotplug_slot)
{
	struct slot *slot = get_slot(hotplug_slot, __FUNCTION__);
	int retval = 0;

	if (slot == NULL)
		return -ENODEV;

	dbg(__FUNCTION__" - physical_slot = %s\n", hotplug_slot->name);
	kfree(slot->hotplug_slot->info);
	kfree(slot->hotplug_slot->name);
	kfree(slot->hotplug_slot);
	kfree(slot);
}

#define SLOT_NAME_SIZE	10
static void make_slot_name (struct slot *slot)
{
	/*
	 * Stupid way to make a filename out of the slot name.
	 * replace this if your hardware provides a better way to name slots.
	 */
	snprintf (slot->hotplug_slot->name, SLOT_NAME_SIZE, "%d", slot->number);
}

static int init_slots (void)
{
	struct slot *slot;
	struct hotplug_slot *hotplug_slot;
	struct hotplug_slot_info *info;
	char *name;
	int retval = 0;
	int i;

	/*
	 * Create a structure for each slot, and register that slot
	 * with the pci_hotplug subsystem.
	 */
	for (i = 0; i < num_slots; ++i) {
		slot = kmalloc (sizeof (struct slot), GFP_KERNEL);
		if (!slot)
			return -ENOMEM;
		memset(slot, 0, sizeof(struct slot));

		hotplug_slot = kmalloc (sizeof (struct hotplug_slot), GFP_KERNEL);
		if (!hotplug_slot) {
			kfree (slot);
			return -ENOMEM;
		}
		memset(hotplug_slot, 0, sizeof (struct hotplug_slot));
		slot->hotplug_slot = hotplug_slot;

		info = kmalloc (sizeof (struct hotplug_slot_info), GFP_KERNEL);
		if (!info) {
			kfree (hotplug_slot);
			kfree (slot);
			return -ENOMEM;
		}
		memset(info, 0, sizeof (struct hotplug_slot_info));
		hotplug_slot->info = info;

		name = kmalloc (SLOT_NAME_SIZE, GFP_KERNEL);
		if (!name) {
			kfree (info);
			kfree (hotplug_slot);
			kfree (slot);
			return -ENOMEM;
		}
		hotplug_slot->name = name;

		slot->magic = SLOT_MAGIC;
		slot->number = i;

		hotplug_slot->private = slot;
		hotplug_slot->release = &release_slot;
		make_slot_name (slot);
		hotplug_slot->ops = &skel_hotplug_slot_ops;
		
		/*
		 * Initilize the slot info structure with some known
		 * good values.
		 */
		info->power_status = get_power_status(slot);
		info->attention_status = get_attention_status(slot);
		info->latch_status = get_latch_status(slot);
		info->adapter_status = get_adapter_status(slot);
		
		dbg ("registering slot %d\n", i);
		retval = pci_hp_register (slot->hotplug_slot);
		if (retval) {
			err ("pci_hp_register failed with error %d\n", retval);
			kfree (info);
			kfree (name);
			kfree (hotplug_slot);
			kfree (slot);
			return retval;
		}

		/* add slot to our internal list */
		list_add (&slot->slot_list, &slot_list);
	}

	return retval;
}

static void cleanup_slots(void)
{
	struct list_head *tmp;
	struct list_head *next;
	struct slot *slot;

	/*
	 * Unregister all of our slots with the pci_hotplug subsystem.
	 * Memory will be freed in release_slot() callback after slot's
	 * lifespan is finished.
	 */
	list_for_each_safe (tmp, next, &slot_list) {
		slot = list_entry (tmp, struct slot, slot_list);
		list_del (&slot->slot_list);
		pci_hp_deregister (slot->hotplug_slot);
	}
}
		
static int __init pcihp_skel_init(void)
{
	int retval;

	/*
	 * Do specific initialization stuff for your driver here
	 * Like initilizing your controller hardware (if any) and
	 * determining the number of slots you have in the system
	 * right now.
	 */
	num_slots = 5;

	retval = init_slots();
	if (retval)
		return retval;

	info (DRIVER_DESC " version: " DRIVER_VERSION "\n");
	return 0;
}

static void __exit pcihp_skel_exit(void)
{
	/*
	 * Clean everything up.
	 */
	cleanup_slots();
}

module_init(pcihp_skel_init);
module_exit(pcihp_skel_exit);

