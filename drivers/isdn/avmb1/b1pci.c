/*
 * $Id: b1pci.c,v 1.9 1999/04/15 19:49:32 calle Exp $
 * 
 * Module for AVM B1 PCI-card.
 * 
 * (c) Copyright 1997 by Carsten Paeth (calle@calle.in-berlin.de)
 * 
 * $Log: b1pci.c,v $
 * Revision 1.9  1999/04/15 19:49:32  calle
 * fix fuer die B1-PCI. Jetzt geht z.B. auch IRQ 17 ...
 *
 * Revision 1.8  1998/06/17 19:51:16  he
 * merged with 2.1.10[34] (cosmetics and udelay() -> mdelay())
 * brute force fix to avoid Ugh's in isdn_tty_write()
 * cleaned up some dead code
 *
 * Revision 1.7  1998/03/29 16:06:02  calle
 * changes from 2.0 tree merged.
 *
 * Revision 1.2.2.2  1998/01/23 16:49:30  calle
 * added functions for pcmcia cards,
 * avmb1_addcard returns now the controller number.
 *
 * Revision 1.6  1998/02/25 09:15:36  fritz
 * apply Martin's pci driver patch to isdn drivers (vgerCVS)
 *
 * Revision 1.5  1998/01/31 11:14:43  calle
 * merged changes to 2.0 tree, prepare 2.1.82 to work.
 *
 * Revision 1.4  1997/12/10 20:00:50  calle
 * get changes from 2.0 version
 *
 * Revision 1.3  1997/10/01 09:21:14  fritz
 * Removed old compatibility stuff for 2.0.X kernels.
 * From now on, this code is for 2.1.X ONLY!
 * Old stuff is still in the separate branch.
 *
 * Revision 1.2  1997/05/18 09:24:13  calle
 * added verbose disconnect reason reporting to avmb1.
 * some fixes in capi20 interface.
 * changed info messages for B1-PCI
 *
 * Revision 1.1  1997/03/30 17:10:42  calle
 * added support for AVM-B1-PCI card.
 *
 */

#include <linux/config.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/skbuff.h>
#include "compat.h"
#include <linux/capi.h>
#include <linux/b1lli.h>

#ifndef PCI_VENDOR_ID_AVM
#define PCI_VENDOR_ID_AVM	0x1244
#endif

#ifndef PCI_DEVICE_ID_AVM_B1
#define PCI_DEVICE_ID_AVM_B1	0x700
#endif

static char *revision = "$Revision: 1.9 $";

/* ------------------------------------------------------------- */

MODULE_AUTHOR("Carsten Paeth <calle@calle.in-berlin.de>");

/* ------------------------------------------------------------- */

/* ------------------------------------------------------------- */
/* -------- Init & Cleanup ------------------------------------- */
/* ------------------------------------------------------------- */

/*
 * init / exit functions
 */

#ifdef MODULE
#define b1pci_init init_module
#endif

int b1pci_init(void)
{
	char *p;
	char rev[10];
	int rc;
	struct pci_dev *dev = NULL;

	if ((p = strchr(revision, ':'))) {
		strcpy(rev, p + 1);
		p = strchr(rev, '$');
		*p = 0;
	} else
		strcpy(rev, " ??? ");


#ifdef CONFIG_PCI
	if (!pci_present()) {
		printk(KERN_ERR "b1pci: no PCI bus present\n");
		return -EIO;
	}

	printk(KERN_INFO "b1pci: revision %s\n", rev);

	while ((dev = pci_find_device(PCI_VENDOR_ID_AVM, PCI_DEVICE_ID_AVM_B1, dev))) {
		unsigned int ioaddr = dev->base_address[1] & PCI_BASE_ADDRESS_IO_MASK;
		unsigned int irq = dev->irq;
		printk(KERN_INFO
			"b1pci: PCI BIOS reports AVM-B1 at i/o %#x, irq %d\n",
			ioaddr, irq);
		if ((rc = avmb1_probecard(ioaddr, irq, AVM_CARDTYPE_B1PCI)) != 0) {
		        printk(KERN_ERR
			"b1pci: no AVM-B1 at i/o %#x, irq %d detected\n",
			ioaddr, irq);
			return rc;
		}
		if ((rc = avmb1_addcard(ioaddr, irq, AVM_CARDTYPE_B1PCI)) < 0)
			return rc;
	}
	return 0;
#else
	printk(KERN_ERR "b1pci: kernel not compiled with PCI.\n");
	return -EIO;
#endif
}

#ifdef MODULE
void cleanup_module(void)
{
}
#endif
