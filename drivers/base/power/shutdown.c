/*
 * shutdown.c - power management functions for the device tree.
 * 
 * Copyright (c) 2002-3 Patrick Mochel
 *		 2002-3 Open Source Development Lab
 * 
 * This file is released under the GPLv2
 *
 */

#undef DEBUG

#include <linux/device.h>
#include <asm/semaphore.h>

#define to_dev(node) container_of(node,struct device,kobj.entry)

extern struct subsystem devices_subsys;

/**
 * We handle system devices differently - we suspend and shut them 
 * down first and resume them first. That way, we do anything stupid like
 * shutting down the interrupt controller before any devices..
 *
 * Note that there are not different stages for power management calls - 
 * they only get one called once when interrupts are disabled. 
 */

extern int sysdev_shutdown(void);

/**
 * device_shutdown - call ->remove() on each device to shutdown. 
 */
void device_shutdown(void)
{
	struct device * dev;
	
	down_write(&devices_subsys.rwsem);
	list_for_each_entry_reverse(dev,&devices_subsys.kset.list,kobj.entry) {
		pr_debug("shutting down %s: ",dev->name);
		if (dev->driver && dev->driver->shutdown) {
			pr_debug("Ok\n");
			dev->driver->shutdown(dev);
		} else
			pr_debug("Ignored.\n");
	}
	up_write(&devices_subsys.rwsem);

	sysdev_shutdown();
}

