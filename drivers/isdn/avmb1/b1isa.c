/*
 * $Id: b1isa.c,v 1.3 1999/07/09 15:05:40 keil Exp $
 * 
 * Module for AVM B1 ISA-card.
 * 
 * (c) Copyright 1999 by Carsten Paeth (calle@calle.in-berlin.de)
 * 
 * $Log: b1isa.c,v $
 * Revision 1.3  1999/07/09 15:05:40  keil
 * compat.h is now isdn_compat.h
 *
 * Revision 1.2  1999/07/05 15:09:49  calle
 * - renamed "appl_release" to "appl_released".
 * - version und profile data now cleared on controller reset
 * - extended /proc interface, to allow driver and controller specific
 *   informations to include by driver hackers.
 *
 * Revision 1.1  1999/07/01 15:26:27  calle
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
#include <linux/capi.h>
#include <asm/io.h>
#include <linux/isdn_compat.h>
#include "capicmd.h"
#include "capiutil.h"
#include "capilli.h"
#include "avmcard.h"

static char *revision = "$Revision: 1.3 $";

/* ------------------------------------------------------------- */

MODULE_AUTHOR("Carsten Paeth <calle@calle.in-berlin.de>");

/* ------------------------------------------------------------- */

static struct capi_driver_interface *di;

/* ------------------------------------------------------------- */

static void b1isa_interrupt(int interrupt, void *devptr, struct pt_regs *regs)
{
	avmcard *card;

	card = (avmcard *) devptr;

	if (!card) {
		printk(KERN_WARNING "b1_interrupt: wrong device\n");
		return;
	}
	if (card->interrupt) {
		printk(KERN_ERR "b1_interrupt: reentering interrupt hander (%s)\n", card->name);
		return;
	}

	card->interrupt = 1;

	b1_handle_interrupt(card);

	card->interrupt = 0;
}
/* ------------------------------------------------------------- */

static void b1isa_remove_ctr(struct capi_ctr *ctrl)
{
	avmcard *card = (avmcard *)(ctrl->driverdata);
	unsigned int port = card->port;

	b1_reset(port);
	b1_reset(port);

	di->detach_ctr(ctrl);
	free_irq(card->irq, card);
	release_region(card->port, AVMB1_PORTLEN);
	kfree(card);

	MOD_DEC_USE_COUNT;
}

/* ------------------------------------------------------------- */

static int b1isa_add_card(struct capi_driver *driver, struct capicardparams *p)
{
	avmcard *card;
	int retval;

	card = (avmcard *) kmalloc(sizeof(avmcard), GFP_ATOMIC);

	if (!card) {
		printk(KERN_WARNING "b1isa: no memory.\n");
		return -ENOMEM;
	}
	memset(card, 0, sizeof(avmcard));
	sprintf(card->name, "b1isa-%x", p->port);
	card->port = p->port;
	card->irq = p->irq;
	card->cardtype = avm_b1isa;

	if (check_region(card->port, AVMB1_PORTLEN)) {
		printk(KERN_WARNING
		       "b1isa: ports 0x%03x-0x%03x in use.\n",
		       card->port, card->port + AVMB1_PORTLEN);
		kfree(card);
		return -EBUSY;
	}
	if (b1_irq_table[card->irq & 0xf] == 0) {
		printk(KERN_WARNING "b1isa: irq %d not valid.\n", card->irq);
		kfree(card);
		return -EINVAL;
	}
	if (   card->port != 0x150 && card->port != 0x250
	    && card->port != 0x300 && card->port != 0x340) {
		printk(KERN_WARNING "b1isa: illegal port 0x%x.\n", card->port);
		kfree(card);
		return -EINVAL;
	}
	b1_reset(card->port);
	if ((retval = b1_detect(card->port, card->cardtype)) != 0) {
		printk(KERN_NOTICE "b1isa: NO card at 0x%x (%d)\n",
					card->port, retval);
		kfree(card);
		return -EIO;
	}
	b1_reset(card->port);

	request_region(p->port, AVMB1_PORTLEN, card->name);

	retval = request_irq(card->irq, b1isa_interrupt, 0, card->name, card);
	if (retval) {
		printk(KERN_ERR "b1isa: unable to get IRQ %d.\n", card->irq);
		release_region(card->port, AVMB1_PORTLEN);
		kfree(card);
		return -EBUSY;
	}

	card->ctrl = di->attach_ctr(driver, card->name, card);
	if (!card->ctrl) {
		printk(KERN_ERR "b1isa: attach controller failed.\n");
		free_irq(card->irq, card);
		release_region(card->port, AVMB1_PORTLEN);
		kfree(card);
		return -EBUSY;
	}

	MOD_INC_USE_COUNT;
	return 0;
}

static char *b1isa_procinfo(struct capi_ctr *ctrl)
{
	avmcard *card = (avmcard *)(ctrl->driverdata);
	if (!card)
		return "";
	sprintf(card->infobuf, "%s %s 0x%x %d",
		card->cardname[0] ? card->cardname : "-",
		card->version[VER_DRIVER] ? card->version[VER_DRIVER] : "-",
		card->port, card->irq
		);
	return card->infobuf;
}

/* ------------------------------------------------------------- */

static struct capi_driver b1isa_driver = {
    "b1isa",
    "0.0",
    b1_load_firmware,
    b1_reset_ctr,
    b1isa_remove_ctr,
    b1_register_appl,
    b1_release_appl,
    b1_send_message,

    b1isa_procinfo,
    b1ctl_read_proc,
    0,	/* use standard driver_read_proc */

    b1isa_add_card,
};

#ifdef MODULE
#define b1isa_init init_module
void cleanup_module(void);
#endif

int b1isa_init(void)
{
	struct capi_driver *driver = &b1isa_driver;
	char *p;

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
	return 0;
}

#ifdef MODULE
void cleanup_module(void)
{
    detach_capi_driver(&b1isa_driver);
}
#endif
