/*
 * Linux ARCnet driver - COM20020 PCI support (Contemporary Controls PCI20)
 * 
 * Written 1994-1999 by Avery Pennarun,
 *    based on an ISA version by David Woodhouse.
 * Written 1999 by Martin Mares <mj@suse.cz>.
 * Derived from skeleton.c by Donald Becker.
 *
 * Special thanks to Contemporary Controls, Inc. (www.ccontrols.com)
 *  for sponsoring the further development of this driver.
 *
 * **********************
 *
 * The original copyright of skeleton.c was as follows:
 *
 * skeleton.c Written 1993 by Donald Becker.
 * Copyright 1993 United States Government as represented by the
 * Director, National Security Agency.  This software may only be used
 * and distributed according to the terms of the GNU Public License as
 * modified by SRC, incorporated herein by reference.
 *
 * **********************
 *
 * For more details, see drivers/net/arcnet.c
 *
 * **********************
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/arcdevice.h>
#include <linux/com20020.h>

#include <asm/io.h>


#define VERSION "arcnet: COM20020 PCI support\n"

#ifdef MODULE
static struct net_device *cards[16];
static int numcards;
#endif

static void com20020pci_open_close(struct net_device *dev, bool open)
{
    if (open)
	MOD_INC_USE_COUNT;
    else
	MOD_DEC_USE_COUNT;
}

/*
 * No need to probe for PCI cards - just ask the PCI layer and launch all the
 * ones we find.
 */
static int __init com20020pci_probe(char *name_template, int node, int backplane, int clock, int timeout)
{
    struct net_device *dev;
    struct arcnet_local *lp;
    struct pci_dev *pdev = NULL;
    int ioaddr, gotone = 0, err;

    BUGLVL(D_NORMAL) printk(VERSION);

    while ((pdev = pci_find_device(0x1571, 0xa004, pdev)))
    {
	if (pci_enable_device(pdev))
	    continue;
	dev = dev_alloc(name_template ? : "arc%d", &err);
	if (!dev)
	    return err;
	lp = dev->priv = kmalloc(sizeof(struct arcnet_local), GFP_KERNEL);
	if (!lp)
	    return -ENOMEM;
	memset(lp, 0, sizeof(struct arcnet_local));
    
	ioaddr = pdev->resource[2].start;
	dev->base_addr = ioaddr;
	dev->irq = pdev->irq;
	dev->dev_addr[0] = node;
	lp->backplane = backplane;
	lp->clock = clock;
	lp->timeout = timeout;
	lp->hw.open_close_ll = com20020pci_open_close;
	
	BUGMSG(D_INIT, "PCI BIOS reports a device at %Xh, IRQ %d\n",
	       ioaddr, dev->irq);
	       
	if (check_region(ioaddr, ARCNET_TOTAL_SIZE))
	{
	    BUGMSG(D_INIT, "IO region %xh-%xh already allocated.\n",
		   ioaddr, ioaddr + ARCNET_TOTAL_SIZE - 1);
	    continue;
	}

	if (ASTATUS() == 0xFF)
	{
	    BUGMSG(D_NORMAL, "IO address %Xh was reported by PCI BIOS, "
		    "but seems empty!\n", ioaddr);
	    continue;
	}
	
	if (com20020_check(dev))
	    continue;

	if (!com20020_found(dev, SA_SHIRQ))
	{
#ifdef MODULE
	    cards[numcards++] = dev;
#endif
	    gotone++;
	}
    }
    
    return gotone ? 0 : -ENODEV;
}


#ifdef MODULE

/* Module parameters */

static int node = 0;
static char *device;		/* use eg. device="arc1" to change name */
static int timeout = 3;
static int backplane = 0;
static int clock = 0;

MODULE_PARM(node, "i");
MODULE_PARM(device, "s");
MODULE_PARM(timeout, "i");
MODULE_PARM(backplane, "i");
MODULE_PARM(clock, "i");

int init_module(void)
{
    return com20020pci_probe(device, node, backplane, clock & 7, timeout & 3);
}

void cleanup_module(void)
{
    struct net_device *dev;
    int count;
    
    for (count = 0; count < numcards; count++)
    {
	dev = cards[count];

	if (dev->start)
	    dev->stop(dev);

	free_irq(dev->irq, dev);
	release_region(dev->base_addr, ARCNET_TOTAL_SIZE);
	unregister_netdev(dev);
	kfree(dev->priv);
	kfree(dev);
    }
}

#else

void __init com20020pci_probe_all(void)
{
	com20020pci_probe(NULL, 0, 0, 0, 3);
}

#endif				/* MODULE */
