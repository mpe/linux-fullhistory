/*
 *  drivers/s390/cio/requestirq.c
 *   S/390 common I/O routines -- enabling and disabling of devices
 *   $Revision: 1.42 $
 *
 *    Copyright (C) 1999-2002 IBM Deutschland Entwicklung GmbH,
 *			      IBM Corporation
 *    Author(s): Ingo Adlung (adlung@de.ibm.com)
 *		 Cornelia Huck (cohuck@de.ibm.com)
 *		 Arnd Bergmann (arndb@de.ibm.com)
 */

#include <linux/module.h>
#include <linux/config.h>
#include <linux/device.h>
#include <linux/init.h>

#include "css.h"

/* for compatiblity only... */
int
request_irq (unsigned int irq,
	     void (*handler) (int, void *, struct pt_regs *),
	     unsigned long irqflags, const char *devname, void *dev_id)
{
	return -EINVAL;
}

/* for compatiblity only... */
void
free_irq (unsigned int irq, void *dev_id)
{
}

struct pgid global_pgid;
EXPORT_SYMBOL_GPL(global_pgid);

/*
 * init_IRQ is now only used to set the pgid as early as possible
 */
void __init
init_IRQ(void)
{
	/*
	 * Let's build our path group ID here.
	 */
	global_pgid.cpu_addr = *(__u16 *) __LC_CPUADDR;
	global_pgid.cpu_id = ((cpuid_t *) __LC_CPUID)->ident;
	global_pgid.cpu_model = ((cpuid_t *) __LC_CPUID)->machine;
	global_pgid.tod_high = (__u32) (get_clock() >> 32);
}
