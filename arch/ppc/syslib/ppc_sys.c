/*
 * arch/ppc/syslib/ppc_sys.c
 *
 * PPC System library functions
 *
 * Maintainer: Kumar Gala <kumar.gala@freescale.com>
 *
 * Copyright 2005 Freescale Semiconductor Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <asm/ppc_sys.h>

int (*ppc_sys_device_fixup) (struct platform_device * pdev);

static int ppc_sys_inited;

void __init identify_ppc_sys_by_id(u32 id)
{
	unsigned int i = 0;
	while (1) {
		if ((ppc_sys_specs[i].mask & id) == ppc_sys_specs[i].value)
			break;
		i++;
	}

	cur_ppc_sys_spec = &ppc_sys_specs[i];

	return;
}

void __init identify_ppc_sys_by_name(char *name)
{
	/* TODO */
	return;
}

/* Update all memory resources by paddr, call before platform_device_register */
void __init
ppc_sys_fixup_mem_resource(struct platform_device *pdev, phys_addr_t paddr)
{
	int i;
	for (i = 0; i < pdev->num_resources; i++) {
		struct resource *r = &pdev->resource[i];
		if ((r->flags & IORESOURCE_MEM) == IORESOURCE_MEM) {
			r->start += paddr;
			r->end += paddr;
		}
	}
}

/* Get platform_data pointer out of platform device, call before platform_device_register */
void *__init ppc_sys_get_pdata(enum ppc_sys_devices dev)
{
	return ppc_sys_platform_devices[dev].dev.platform_data;
}

void ppc_sys_device_remove(enum ppc_sys_devices dev)
{
	unsigned int i;

	if (ppc_sys_inited) {
		platform_device_unregister(&ppc_sys_platform_devices[dev]);
	} else {
		if (cur_ppc_sys_spec == NULL)
			return;
		for (i = 0; i < cur_ppc_sys_spec->num_devices; i++)
			if (cur_ppc_sys_spec->device_list[i] == dev)
				cur_ppc_sys_spec->device_list[i] = -1;
	}
}

static int __init ppc_sys_init(void)
{
	unsigned int i, dev_id, ret = 0;

	BUG_ON(cur_ppc_sys_spec == NULL);

	for (i = 0; i < cur_ppc_sys_spec->num_devices; i++) {
		dev_id = cur_ppc_sys_spec->device_list[i];
		if (dev_id != -1) {
			if (ppc_sys_device_fixup != NULL)
				ppc_sys_device_fixup(&ppc_sys_platform_devices
						     [dev_id]);
			if (platform_device_register
			    (&ppc_sys_platform_devices[dev_id])) {
				ret = 1;
				printk(KERN_ERR
				       "unable to register device %d\n",
				       dev_id);
			}
		}
	}

	ppc_sys_inited = 1;
	return ret;
}

subsys_initcall(ppc_sys_init);
