/*
 * Acorn specific net device driver probe routine
 *
 * Copyright (C) 1998 Russell King
 */
#include <linux/config.h>
#include <linux/netdevice.h>
#include <linux/errno.h>
#include <linux/init.h>

extern int ether1_probe (struct device *dev);
extern int ether3_probe (struct device *dev);
extern int etherh_probe (struct device *dev);

__initfunc(int acorn_ethif_probe(struct device *dev))
{
	if (1
#ifdef CONFIG_ARM_ETHERH
        && etherh_probe (dev)
#endif
#ifdef CONFIG_ARM_ETHER3
        && ether3_probe (dev)
#endif
#ifdef CONFIG_ARM_ETHER1
        && ether1_probe (dev)
#endif
	&& 1) {
		return 1;
	}
	return 0;
}
