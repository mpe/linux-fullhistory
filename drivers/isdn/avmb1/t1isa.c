/*
 * $Id: t1isa.c,v 1.5 1999/08/22 20:26:28 calle Exp $
 * 
 * Module for AVM T1 HEMA-card.
 * 
 * (c) Copyright 1999 by Carsten Paeth (calle@calle.in-berlin.de)
 * 
 * $Log: t1isa.c,v $
 * Revision 1.5  1999/08/22 20:26:28  calle
 * backported changes from kernel 2.3.14:
 * - several #include "config.h" gone, others come.
 * - "struct device" changed to "struct net_device" in 2.3.14, added a
 *   define in isdn_compat.h for older kernel versions.
 *
 * Revision 1.4  1999/07/09 15:05:50  keil
 * compat.h is now isdn_compat.h
 *
 * Revision 1.3  1999/07/06 07:42:04  calle
 * - changes in /proc interface
 * - check and changed calls to [dev_]kfree_skb and [dev_]alloc_skb.
 *
 * Revision 1.2  1999/07/05 15:09:54  calle
 * - renamed "appl_release" to "appl_released".
 * - version und profile data now cleared on controller reset
 * - extended /proc interface, to allow driver and controller specific
 *   informations to include by driver hackers.
 *
 * Revision 1.1  1999/07/01 15:26:44  calle
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

static char *revision = "$Revision: 1.5 $";

/* ------------------------------------------------------------- */

MODULE_AUTHOR("Carsten Paeth <calle@calle.in-berlin.de>");

/* ------------------------------------------------------------- */

static struct capi_driver_interface *di;

/* ------------------------------------------------------------- */

static int hema_irq_table[16] =
{0,
 0,
 0,
 0x80,				/* irq 3 */
 0,
 0x90,				/* irq 5 */
 0,
 0xA0,				/* irq 7 */
 0,
 0xB0,				/* irq 9 */
 0xC0,				/* irq 10 */
 0xD0,				/* irq 11 */
 0xE0,				/* irq 12 */
 0,
 0,
 0xF0,				/* irq 15 */
};

static int t1_detectandinit(unsigned int base, unsigned irq, int cardnr)
{
	unsigned char cregs[8];
	unsigned char reverse_cardnr;
	unsigned long flags;
	unsigned char dummy;
	int i;

	reverse_cardnr =   ((cardnr & 0x01) << 3) | ((cardnr & 0x02) << 1)
		         | ((cardnr & 0x04) >> 1) | ((cardnr & 0x08) >> 3);
	cregs[0] = (HEMA_VERSION_ID << 4) | (reverse_cardnr & 0xf);
	cregs[1] = 0x00; /* fast & slow link connected to CON1 */
	cregs[2] = 0x05; /* fast link 20MBit, slow link 20 MBit */
	cregs[3] = 0;
	cregs[4] = 0x11; /* zero wait state */
	cregs[5] = hema_irq_table[irq & 0xf];
	cregs[6] = 0;
	cregs[7] = 0;

	save_flags(flags);
	cli();
	/* board reset */
	t1outp(base, T1_RESETBOARD, 0xf);
	udelay(100 * 1000);
	dummy = t1inp(base, T1_FASTLINK+T1_OUTSTAT); /* first read */

	/* write config */
	dummy = (base >> 4) & 0xff;
	for (i=1;i<=0xf;i++) t1outp(base, i, dummy);
	t1outp(base, HEMA_PAL_ID & 0xf, dummy);
	t1outp(base, HEMA_PAL_ID >> 4, cregs[0]);
	for(i=1;i<7;i++) t1outp(base, 0, cregs[i]);
	t1outp(base, ((base >> 4)) & 0x3, cregs[7]);
	restore_flags(flags);

	udelay(100 * 1000);
	t1outp(base, T1_FASTLINK+T1_RESETLINK, 0);
	t1outp(base, T1_SLOWLINK+T1_RESETLINK, 0);
	udelay(10 * 1000);
	t1outp(base, T1_FASTLINK+T1_RESETLINK, 1);
	t1outp(base, T1_SLOWLINK+T1_RESETLINK, 1);
	udelay(100 * 1000);
	t1outp(base, T1_FASTLINK+T1_RESETLINK, 0);
	t1outp(base, T1_SLOWLINK+T1_RESETLINK, 0);
	udelay(10 * 1000);
	t1outp(base, T1_FASTLINK+T1_ANALYSE, 0);
	udelay(5 * 1000);
	t1outp(base, T1_SLOWLINK+T1_ANALYSE, 0);

	if (t1inp(base, T1_FASTLINK+T1_OUTSTAT) != 0x1) /* tx empty */
		return 1;
	if (t1inp(base, T1_FASTLINK+T1_INSTAT) != 0x0) /* rx empty */
		return 2;
	if (t1inp(base, T1_FASTLINK+T1_IRQENABLE) != 0x0)
		return 3;
	if ((t1inp(base, T1_FASTLINK+T1_FIFOSTAT) & 0xf0) != 0x70)
		return 4;
	if ((t1inp(base, T1_FASTLINK+T1_IRQMASTER) & 0x0e) != 0)
		return 5;
	if ((t1inp(base, T1_FASTLINK+T1_IDENT) & 0x7d) != 1)
		return 6;
	if (t1inp(base, T1_SLOWLINK+T1_OUTSTAT) != 0x1) /* tx empty */
		return 7;
	if ((t1inp(base, T1_SLOWLINK+T1_IRQMASTER) & 0x0e) != 0)
		return 8;
	if ((t1inp(base, T1_SLOWLINK+T1_IDENT) & 0x7d) != 0)
		return 9;
        return 0;
}

static void t1_handle_interrupt(avmcard * card)
{
	struct capi_ctr *ctrl = card->ctrl;
	unsigned char b1cmd;
	struct sk_buff *skb;

	unsigned ApplId;
	unsigned MsgLen;
	unsigned DataB3Len;
	unsigned NCCI;
	unsigned WindowSize;

	while (b1_rx_full(card->port)) {

		b1cmd = b1_get_byte(card->port);

		switch (b1cmd) {

		case RECEIVE_DATA_B3_IND:

			ApplId = (unsigned) b1_get_word(card->port);
			MsgLen = t1_get_slice(card->port, card->msgbuf);
			DataB3Len = t1_get_slice(card->port, card->databuf);

			if (!(skb = alloc_skb(DataB3Len+MsgLen, GFP_ATOMIC))) {
				printk(KERN_ERR "t1isa: incoming packet dropped\n");
			} else {
				memcpy(skb_put(skb, MsgLen), card->msgbuf, MsgLen);
				memcpy(skb_put(skb, DataB3Len), card->databuf, DataB3Len);
				CAPIMSG_SETDATA(skb->data, skb->data + MsgLen);
				ctrl->handle_capimsg(ctrl, ApplId, skb);
			}
			break;

		case RECEIVE_MESSAGE:

			ApplId = (unsigned) b1_get_word(card->port);
			MsgLen = t1_get_slice(card->port, card->msgbuf);
			if (!(skb = alloc_skb(MsgLen, GFP_ATOMIC))) {
				printk(KERN_ERR "t1isa: incoming packet dropped\n");
			} else {
				memcpy(skb_put(skb, MsgLen), card->msgbuf, MsgLen);
				ctrl->handle_capimsg(ctrl, ApplId, skb);
			}
			break;

		case RECEIVE_NEW_NCCI:

			ApplId = b1_get_word(card->port);
			NCCI = b1_get_word(card->port);
			WindowSize = b1_get_word(card->port);

			ctrl->new_ncci(ctrl, ApplId, NCCI, WindowSize);

			break;

		case RECEIVE_FREE_NCCI:

			ApplId = b1_get_word(card->port);
			NCCI = b1_get_word(card->port);

			if (NCCI != 0xffffffff)
				ctrl->free_ncci(ctrl, ApplId, NCCI);
			else ctrl->appl_released(ctrl, ApplId);
			break;

		case RECEIVE_START:
			b1_put_byte(card->port, SEND_POLLACK);
			ctrl->resume_output(ctrl);
			break;

		case RECEIVE_STOP:
			ctrl->suspend_output(ctrl);
			break;

		case RECEIVE_INIT:

			card->versionlen = t1_get_slice(card->port, card->versionbuf);
			b1_parse_version(card);
			printk(KERN_INFO "%s: %s-card (%s) now active\n",
			       card->name,
			       card->version[VER_CARDTYPE],
			       card->version[VER_DRIVER]);
			ctrl->ready(ctrl);
			break;

		case RECEIVE_TASK_READY:
			ApplId = (unsigned) b1_get_word(card->port);
			MsgLen = t1_get_slice(card->port, card->msgbuf);
			card->msgbuf[MsgLen--] = 0;
			while (    MsgLen >= 0
			       && (   card->msgbuf[MsgLen] == '\n'
				   || card->msgbuf[MsgLen] == '\r'))
				card->msgbuf[MsgLen--] = 0;
			printk(KERN_INFO "%s: task %d \"%s\" ready.\n",
					card->name, ApplId, card->msgbuf);
			break;

		case RECEIVE_DEBUGMSG:
			MsgLen = t1_get_slice(card->port, card->msgbuf);
			card->msgbuf[MsgLen--] = 0;
			while (    MsgLen >= 0
			       && (   card->msgbuf[MsgLen] == '\n'
				   || card->msgbuf[MsgLen] == '\r'))
				card->msgbuf[MsgLen--] = 0;
			printk(KERN_INFO "%s: DEBUG: %s\n", card->name, card->msgbuf);
			break;

		case 0xff:
			printk(KERN_ERR "%s: card reseted ?\n", card->name);
			return;
		default:
			printk(KERN_ERR "%s: b1_interrupt: 0x%x ???\n",
					card->name, b1cmd);
			return;
		}
	}
}

/* ------------------------------------------------------------- */

static void t1isa_interrupt(int interrupt, void *devptr, struct pt_regs *regs)
{
	avmcard *card;

	card = (avmcard *) devptr;

	if (!card) {
		printk(KERN_WARNING "t1_interrupt: wrong device\n");
		return;
	}
	if (card->interrupt) {
		printk(KERN_ERR "t1_interrupt: reentering interrupt hander (%s)\n", card->name);
		return;
	}

	card->interrupt = 1;

	t1_handle_interrupt(card);

	card->interrupt = 0;
}
/* ------------------------------------------------------------- */

static int t1isa_load_firmware(struct capi_ctr *ctrl, capiloaddata *data)
{
	avmcard *card = (avmcard *)(ctrl->driverdata);
	unsigned int port = card->port;
	unsigned long flags;
	int retval;

	t1_disable_irq(port);
	b1_reset(port);

	if ((retval = b1_load_t4file(port, &data->firmware))) {
		b1_reset(port);
		printk(KERN_ERR "%s: failed to load t4file!!\n",
					card->name);
		return retval;
	}

	if (data->configuration.len > 0 && data->configuration.data) {
		if ((retval = b1_load_config(port, &data->configuration))) {
			b1_reset(port);
			printk(KERN_ERR "%s: failed to load config!!\n",
					card->name);
			return retval;
		}
	}

	if (!b1_loaded(port)) {
		printk(KERN_ERR "%s: failed to load t4file.\n", card->name);
		return -EIO;
	}

	save_flags(flags);
	cli();
	b1_setinterrupt(port, card->irq, card->cardtype);
	b1_put_byte(port, SEND_INIT);
	b1_put_word(port, AVM_NAPPS);
	b1_put_word(port, AVM_NCCI_PER_CHANNEL*30);
	b1_put_word(port, ctrl->cnr - 1);
	restore_flags(flags);

	return 0;
}

void t1isa_reset_ctr(struct capi_ctr *ctrl)
{
	avmcard *card = (avmcard *)(ctrl->driverdata);
	unsigned int port = card->port;

	t1_disable_irq(port);
	b1_reset(port);
	b1_reset(port);

	memset(card->version, 0, sizeof(card->version));
	ctrl->reseted(ctrl);
}

static void t1isa_remove_ctr(struct capi_ctr *ctrl)
{
	avmcard *card = (avmcard *)(ctrl->driverdata);
	unsigned int port = card->port;

	t1_disable_irq(port);
	b1_reset(port);
	b1_reset(port);
	t1_reset(port);

	di->detach_ctr(ctrl);
	free_irq(card->irq, card);
	release_region(card->port, AVMB1_PORTLEN);
	kfree(card);

	MOD_DEC_USE_COUNT;
}

/* ------------------------------------------------------------- */

static int t1isa_add_card(struct capi_driver *driver, struct capicardparams *p)
{
	struct capi_ctr *ctrl;
	avmcard *card;
	int retval;

	card = (avmcard *) kmalloc(sizeof(avmcard), GFP_ATOMIC);

	if (!card) {
		printk(KERN_WARNING "t1isa: no memory.\n");
		return -ENOMEM;
	}
	memset(card, 0, sizeof(avmcard));
	sprintf(card->name, "t1isa-%x", p->port);
	card->port = p->port;
	card->irq = p->irq;
	card->cardtype = avm_t1isa;
	card->cardnr = p->cardnr;

	if (!(((card->port & 0x7) == 0) && ((card->port & 0x30) != 0x30))) {
		printk(KERN_WARNING "t1isa: illegal port 0x%x.\n", card->port);
		kfree(card);
		return -EINVAL;
        }

	if (check_region(card->port, AVMB1_PORTLEN)) {
		printk(KERN_WARNING
		       "t1isa: ports 0x%03x-0x%03x in use.\n",
		       card->port, card->port + AVMB1_PORTLEN);
		kfree(card);
		return -EBUSY;
	}
	if (hema_irq_table[card->irq & 0xf] == 0) {
		printk(KERN_WARNING "t1isa: irq %d not valid.\n", card->irq);
		kfree(card);
		return -EINVAL;
	}
	for (ctrl = driver->controller; ctrl; ctrl = ctrl->next) {
		if (((avmcard *)(ctrl->driverdata))->cardnr == card->cardnr) {
			printk(KERN_WARNING "t1isa: card with number %d already installed.\n", card->cardnr);
			kfree(card);
			return -EBUSY;
		}
	}
        if ((retval = t1_detectandinit(card->port, card->irq, card->cardnr)) != 0) {
		printk(KERN_NOTICE "t1isa: NO card at 0x%x (%d)\n",
					card->port, retval);
		kfree(card);
		return -EIO;
	}
	t1_disable_irq(card->port);
	b1_reset(card->port);

	request_region(p->port, AVMB1_PORTLEN, card->name);

	retval = request_irq(card->irq, t1isa_interrupt, 0, card->name, card);
	if (retval) {
		printk(KERN_ERR "t1isa: unable to get IRQ %d.\n", card->irq);
		release_region(card->port, AVMB1_PORTLEN);
		kfree(card);
		return -EBUSY;
	}

	card->ctrl = di->attach_ctr(driver, card->name, card);
	if (!card->ctrl) {
		printk(KERN_ERR "t1isa: attach controller failed.\n");
		free_irq(card->irq, card);
		release_region(card->port, AVMB1_PORTLEN);
		kfree(card);
		return -EBUSY;
	}

	MOD_INC_USE_COUNT;
	return 0;
}

static void t1isa_send_message(struct capi_ctr *ctrl, struct sk_buff *skb)
{
	avmcard *card = (avmcard *)(ctrl->driverdata);
	unsigned int port = card->port;
	unsigned long flags;
	__u16 len = CAPIMSG_LEN(skb->data);
	__u8 cmd = CAPIMSG_COMMAND(skb->data);
	__u8 subcmd = CAPIMSG_SUBCOMMAND(skb->data);

	save_flags(flags);
	cli();
	if (CAPICMD(cmd, subcmd) == CAPI_DATA_B3_REQ) {
		__u16 dlen = CAPIMSG_DATALEN(skb->data);
		b1_put_byte(port, SEND_DATA_B3_REQ);
		t1_put_slice(port, skb->data, len);
		t1_put_slice(port, skb->data + len, dlen);
	} else {
		b1_put_byte(port, SEND_MESSAGE);
		t1_put_slice(port, skb->data, len);
	}
	restore_flags(flags);
	dev_kfree_skb(skb);
}
/* ------------------------------------------------------------- */

static char *t1isa_procinfo(struct capi_ctr *ctrl)
{
	avmcard *card = (avmcard *)(ctrl->driverdata);
	if (!card)
		return "";
	sprintf(card->infobuf, "%s %s 0x%x %d %d",
		card->cardname[0] ? card->cardname : "-",
		card->version[VER_DRIVER] ? card->version[VER_DRIVER] : "-",
		card->port, card->irq, card->cardnr
		);
	return card->infobuf;
}


/* ------------------------------------------------------------- */

static struct capi_driver t1isa_driver = {
    "t1isa",
    "0.0",
    t1isa_load_firmware,
    t1isa_reset_ctr,
    t1isa_remove_ctr,
    b1_register_appl,
    b1_release_appl,
    t1isa_send_message,

    t1isa_procinfo,
    b1ctl_read_proc,
    0,	/* use standard driver_read_proc */

    t1isa_add_card,
};

#ifdef MODULE
#define t1isa_init init_module
void cleanup_module(void);
#endif

int t1isa_init(void)
{
	struct capi_driver *driver = &t1isa_driver;
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
    detach_capi_driver(&t1isa_driver);
}
#endif
