/*
 * $Id: b1pci.c,v 1.18 1999/11/05 16:38:01 calle Exp $
 * 
 * Module for AVM B1 PCI-card.
 * 
 * (c) Copyright 1999 by Carsten Paeth (calle@calle.in-berlin.de)
 * 
 * $Log: b1pci.c,v $
 * Revision 1.18  1999/11/05 16:38:01  calle
 * Cleanups before kernel 2.4:
 * - Changed all messages to use card->name or driver->name instead of
 *   constant string.
 * - Moved some data from struct avmcard into new struct avmctrl_info.
 *   Changed all lowlevel capi driver to match the new structur.
 *
 * Revision 1.17  1999/10/05 06:50:07  calle
 * Forgot SA_SHIRQ as argument to request_irq.
 *
 * Revision 1.16  1999/08/11 21:01:07  keil
 * new PCI codefix
 *
 * Revision 1.15  1999/08/10 16:02:27  calle
 * struct pci_dev changed in 2.3.13. Made the necessary changes.
 *
 * Revision 1.14  1999/07/09 15:05:41  keil
 * compat.h is now isdn_compat.h
 *
 * Revision 1.13  1999/07/05 15:09:50  calle
 * - renamed "appl_release" to "appl_released".
 * - version und profile data now cleared on controller reset
 * - extended /proc interface, to allow driver and controller specific
 *   informations to include by driver hackers.
 *
 * Revision 1.12  1999/07/01 15:26:29  calle
 * complete new version (I love it):
 * + new hardware independed "capi_driver" interface that will make it easy to:
 *   - support other controllers with CAPI-2.0 (i.e. USB Controller)
 *   - write a CAPI-2.0 for the passive cards
 *   - support serial link CAPI-2.0 boxes.
 * + wrote "capi_driver" for all supported cards.
 * + "capi_driver" (supported cards) now have to be configured with
 *   make menuconfig, in the past all supported cards where included
 *   at once.
 * + new and better informations in /proc/capi/
 * + new ioctl to switch trace of capi messages per controller
 *   using "avmcapictrl trace [contr] on|off|...."
 * + complete testcircle with all supported cards and also the
 *   PCMCIA cards (now patch for pcmcia-cs-3.0.13 needed) done.
 *
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/capi.h>
#include <asm/io.h>
#include "capicmd.h"
#include "capiutil.h"
#include "capilli.h"
#include "avmcard.h"

static char *revision = "$Revision: 1.18 $";

/* ------------------------------------------------------------- */

#ifndef PCI_VENDOR_ID_AVM
#define PCI_VENDOR_ID_AVM	0x1244
#endif

#ifndef PCI_DEVICE_ID_AVM_B1
#define PCI_DEVICE_ID_AVM_B1	0x700
#endif

/* ------------------------------------------------------------- */

MODULE_AUTHOR("Carsten Paeth <calle@calle.in-berlin.de>");

/* ------------------------------------------------------------- */

static struct capi_driver_interface *di;

/* ------------------------------------------------------------- */

static void b1pci_interrupt(int interrupt, void *devptr, struct pt_regs *regs)
{
	avmcard *card;

	card = (avmcard *) devptr;

	if (!card) {
		printk(KERN_WARNING "b1pci: interrupt: wrong device\n");
		return;
	}
	if (card->interrupt) {
		printk(KERN_ERR "%s: reentering interrupt hander.\n", card->name);
		return;
	}

	card->interrupt = 1;

	b1_handle_interrupt(card);

	card->interrupt = 0;
}
/* ------------------------------------------------------------- */

static void b1pci_remove_ctr(struct capi_ctr *ctrl)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);
	avmcard *card = cinfo->card;
	unsigned int port = card->port;

	b1_reset(port);
	b1_reset(port);

	di->detach_ctr(ctrl);
	free_irq(card->irq, card);
	release_region(card->port, AVMB1_PORTLEN);
	ctrl->driverdata = 0;
	kfree(card->ctrlinfo);
	kfree(card);

	MOD_DEC_USE_COUNT;
}

/* ------------------------------------------------------------- */

static char *b1pci_procinfo(struct capi_ctr *ctrl)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);

	if (!cinfo)
		return "";
	sprintf(cinfo->infobuf, "%s %s 0x%x %d",
		cinfo->cardname[0] ? cinfo->cardname : "-",
		cinfo->version[VER_DRIVER] ? cinfo->version[VER_DRIVER] : "-",
		cinfo->card ? cinfo->card->port : 0x0,
		cinfo->card ? cinfo->card->irq : 0
		);
	return cinfo->infobuf;
}

/* ------------------------------------------------------------- */

static int b1pci_add_card(struct capi_driver *driver, struct capicardparams *p)
{
	avmcard *card;
	avmctrl_info *cinfo;
	int retval;

	card = (avmcard *) kmalloc(sizeof(avmcard), GFP_ATOMIC);

	if (!card) {
		printk(KERN_WARNING "%s: no memory.\n", driver->name);
		return -ENOMEM;
	}
	memset(card, 0, sizeof(avmcard));
        cinfo = (avmctrl_info *) kmalloc(sizeof(avmctrl_info), GFP_ATOMIC);
	if (!cinfo) {
		printk(KERN_WARNING "%s: no memory.\n", driver->name);
		kfree(card);
		return -ENOMEM;
	}
	memset(cinfo, 0, sizeof(avmctrl_info));
	card->ctrlinfo = cinfo;
	cinfo->card = card;
	sprintf(card->name, "b1pci-%x", p->port);
	card->port = p->port;
	card->irq = p->irq;
	card->cardtype = avm_b1pci;

	if (check_region(card->port, AVMB1_PORTLEN)) {
		printk(KERN_WARNING
		       "%s: ports 0x%03x-0x%03x in use.\n",
		       driver->name, card->port, card->port + AVMB1_PORTLEN);
	        kfree(card->ctrlinfo);
		kfree(card);
		return -EBUSY;
	}
	b1_reset(card->port);
	if ((retval = b1_detect(card->port, card->cardtype)) != 0) {
		printk(KERN_NOTICE "%s: NO card at 0x%x (%d)\n",
					driver->name, card->port, retval);
	        kfree(card->ctrlinfo);
		kfree(card);
		return -EIO;
	}
	b1_reset(card->port);

	request_region(p->port, AVMB1_PORTLEN, card->name);

	retval = request_irq(card->irq, b1pci_interrupt, SA_SHIRQ, card->name, card);
	if (retval) {
		printk(KERN_ERR "%s: unable to get IRQ %d.\n",
				driver->name, card->irq);
		release_region(card->port, AVMB1_PORTLEN);
	        kfree(card->ctrlinfo);
		kfree(card);
		return -EBUSY;
	}

	cinfo->capi_ctrl = di->attach_ctr(driver, card->name, cinfo);
	if (!cinfo->capi_ctrl) {
		printk(KERN_ERR "%s: attach controller failed.\n",
				driver->name);
		free_irq(card->irq, card);
		release_region(card->port, AVMB1_PORTLEN);
	        kfree(card->ctrlinfo);
		kfree(card);
		return -EBUSY;
	}

	MOD_INC_USE_COUNT;

	return 0;
}

/* ------------------------------------------------------------- */

static struct capi_driver b1pci_driver = {
    "b1pci",
    "0.0",
    b1_load_firmware,
    b1_reset_ctr,
    b1pci_remove_ctr,
    b1_register_appl,
    b1_release_appl,
    b1_send_message,

    b1pci_procinfo,
    b1ctl_read_proc,
    0,	/* use standard driver_read_proc */

    0, /* no add_card function */
};

#ifdef MODULE
#define b1pci_init init_module
void cleanup_module(void);
#endif

static int ncards = 0;

int b1pci_init(void)
{
	struct capi_driver *driver = &b1pci_driver;
	struct pci_dev *dev = NULL;
	char *p;
	int retval;

	if ((p = strchr(revision, ':'))) {
		strncpy(driver->revision, p + 1, sizeof(driver->revision));
		p = strchr(driver->revision, '$');
		*p = 0;
	}

	printk(KERN_INFO "%s: revision %s\n", driver->name, driver->revision);

        di = attach_capi_driver(driver);

	if (!di) {
		printk(KERN_ERR "%s: failed to attach capi_driver\n",
				driver->name);
		return -EIO;
	}

#ifdef CONFIG_PCI
	if (!pci_present()) {
		printk(KERN_ERR "%s: no PCI bus present\n", driver->name);
    		detach_capi_driver(driver);
		return -EIO;
	}

	while ((dev = pci_find_device(PCI_VENDOR_ID_AVM, PCI_DEVICE_ID_AVM_B1, dev))) {
		struct capicardparams param;

		param.port = dev->resource[ 1].start & PCI_BASE_ADDRESS_IO_MASK;
		param.irq = dev->irq;
		printk(KERN_INFO
			"%s: PCI BIOS reports AVM-B1 at i/o %#x, irq %d\n",
			driver->name, param.port, param.irq);
		retval = b1pci_add_card(driver, &param);
		if (retval != 0) {
		        printk(KERN_ERR
			"%s: no AVM-B1 at i/o %#x, irq %d detected\n",
			driver->name, param.port, param.irq);
#ifdef MODULE
			cleanup_module();
#endif
			return retval;
		}
		ncards++;
	}
	if (ncards) {
		printk(KERN_INFO "%s: %d B1-PCI card(s) detected\n",
				driver->name, ncards);
		return 0;
	}
	printk(KERN_ERR "%s: NO B1-PCI card detected\n", driver->name);
	return -ESRCH;
#else
	printk(KERN_ERR "%s: kernel not compiled with PCI.\n", driver->name);
	return -EIO;
#endif
}

#ifdef MODULE
void cleanup_module(void)
{
    detach_capi_driver(&b1pci_driver);
}
#endif
