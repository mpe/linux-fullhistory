/*
 * $Id: b1capi.c,v 1.4 1997/05/27 15:17:45 fritz Exp $
 * 
 * CAPI 2.0 Module for AVM B1-card.
 * 
 * (c) Copyright 1997 by Carsten Paeth (calle@calle.in-berlin.de)
 * 
 * $Log: b1capi.c,v $
 * Revision 1.4  1997/05/27 15:17:45  fritz
 * Added changes for recent 2.1.x kernels:
 *   changed return type of isdn_close
 *   queue_task_* -> queue_task
 *   clear/set_bit -> test_and_... where apropriate.
 *   changed type of hard_header_cache parameter.
 *
 * Revision 1.3  1997/05/18 09:24:09  calle
 * added verbose disconnect reason reporting to avmb1.
 * some fixes in capi20 interface.
 * changed info messages for B1-PCI
 *
 * Revision 1.2  1997/03/05 21:20:41  fritz
 * Removed include of config.h (mkdep stated this is unneded).
 *
 * Revision 1.1  1997/03/04 21:50:27  calle
 * Frirst version in isdn4linux
 *
 * Revision 2.2  1997/02/12 09:31:39  calle
 * new version
 *
 * Revision 1.1  1997/01/31 10:32:20  calle
 * Initial revision
 *
 * 
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <asm/segment.h>
#include <linux/skbuff.h>
#include <linux/tqueue.h>
#include <linux/capi.h>
#include <linux/b1lli.h>
#include <linux/kernelcapi.h>
#include "compat.h"
#include "capicmd.h"
#include "capiutil.h"

static char *revision = "$Revision: 1.4 $";

/* ------------------------------------------------------------- */

int showcapimsgs = 0;		/* used in lli.c */
int loaddebug = 0;
static int portbase = 0x150;
#ifdef MODULE
static int irq = 15;

#ifdef HAS_NEW_SYMTAB
MODULE_AUTHOR("Carsten Paeth <calle@calle.in-berlin.de>");
MODULE_PARM(portbase, "i");
MODULE_PARM(irq, "2-15i");
MODULE_PARM(showcapimsgs, "0-3i");
MODULE_PARM(loaddebug, "0-1i");
#endif
#endif

/* ------------------------------------------------------------- */

struct msgidqueue {
	struct msgidqueue *next;
	__u16 msgid;
};

typedef struct avmb1_ncci {
	struct avmb1_ncci *next;
	__u16 applid;
	__u32 ncci;
	__u32 winsize;
	struct msgidqueue *msgidqueue;
	struct msgidqueue *msgidlast;
	struct msgidqueue *msgidfree;
	struct msgidqueue msgidpool[CAPI_MAXDATAWINDOW];
} avmb1_ncci;

typedef struct avmb1_appl {
	__u16 applid;
	capi_register_params rparam;
	int releasing;
	__u32 param;
	void (*signal) (__u16 applid, __u32 param);
	struct sk_buff_head recv_queue;
	struct avmb1_ncci *nccilist;
} avmb1_appl;

/* ------------------------------------------------------------- */

static struct capi_version driver_version = {2, 0, 0, 9};
static char driver_serial[CAPI_SERIAL_LEN] = "4711";
static char capi_manufakturer[64] = "AVM Berlin";

#define APPL(a)		   (&applications[(a)-1])
#define	VALID_APPLID(a)	   ((a) && (a) <= CAPI_MAXAPPL && APPL(a)->applid == a)
#define APPL_IS_FREE(a)    (APPL(a)->applid == 0)
#define APPL_MARK_FREE(a)  do{ APPL(a)->applid=0; MOD_DEC_USE_COUNT; }while(0);
#define APPL_MARK_USED(a)  do{ APPL(a)->applid=(a); MOD_INC_USE_COUNT; }while(0);

#define NCCI2CTRL(ncci)    (((ncci) >> 24) & 0x7f)

#define VALID_CARD(c)	   ((c) > 0 && (c) <= ncards)
#define CARD(c)		   (&cards[(c)-1])
#define CARDNR(cp)	   ((cards-(cp))+1)

static avmb1_appl applications[CAPI_MAXAPPL];
static avmb1_card cards[CAPI_MAXCONTR];
static int ncards = 0;
static struct sk_buff_head recv_queue;
static struct capi_interface_user *capi_users = 0;
static long notify_up_set = 0;
static long notify_down_set = 0;

static struct tq_struct tq_state_notify;
static struct tq_struct tq_recv_notify;

/* -------- util functions ------------------------------------ */

static inline int capi_cmd_valid(__u8 cmd)
{
	switch (cmd) {
	case CAPI_ALERT:
	case CAPI_CONNECT:
	case CAPI_CONNECT_ACTIVE:
	case CAPI_CONNECT_B3_ACTIVE:
	case CAPI_CONNECT_B3:
	case CAPI_CONNECT_B3_T90_ACTIVE:
	case CAPI_DATA_B3:
	case CAPI_DISCONNECT_B3:
	case CAPI_DISCONNECT:
	case CAPI_FACILITY:
	case CAPI_INFO:
	case CAPI_LISTEN:
	case CAPI_MANUFACTURER:
	case CAPI_RESET_B3:
	case CAPI_SELECT_B_PROTOCOL:
		return 1;
	}
	return 0;
}

static inline int capi_subcmd_valid(__u8 subcmd)
{
	switch (subcmd) {
	case CAPI_REQ:
	case CAPI_CONF:
	case CAPI_IND:
	case CAPI_RESP:
		return 1;
	}
	return 0;
}

/* -------- NCCI Handling ------------------------------------- */

static inline void mq_init(avmb1_ncci * np)
{
	int i;
	np->msgidqueue = 0;
	np->msgidlast = 0;
	memset(np->msgidpool, 0, sizeof(np->msgidpool));
	np->msgidfree = &np->msgidpool[0];
	for (i = 1; i < np->winsize; i++) {
		np->msgidpool[i].next = np->msgidfree;
		np->msgidfree = &np->msgidpool[i];
	}
}

static inline int mq_enqueue(avmb1_ncci * np, __u16 msgid)
{
	struct msgidqueue *mq;
	if ((mq = np->msgidfree) == 0)
		return 0;
	np->msgidfree = mq->next;
	mq->msgid = msgid;
	mq->next = 0;
	if (np->msgidlast)
		np->msgidlast->next = mq;
	np->msgidlast = mq;
	if (!np->msgidqueue)
		np->msgidqueue = mq;
	return 1;
}

static inline int mq_dequeue(avmb1_ncci * np, __u16 msgid)
{
	struct msgidqueue **pp;
	for (pp = &np->msgidqueue; *pp; pp = &(*pp)->next) {
		if ((*pp)->msgid == msgid) {
			struct msgidqueue *mq = *pp;
			*pp = mq->next;
			if (mq == np->msgidlast)
				np->msgidlast = 0;
			mq->next = np->msgidfree;
			np->msgidfree = mq;
			return 1;
		}
	}
	return 0;
}

void avmb1_handle_new_ncci(avmb1_card * card,
			   __u16 appl, __u32 ncci, __u32 winsize)
{
	avmb1_ncci *np;
	if (!VALID_APPLID(appl)) {
		printk(KERN_ERR "avmb1_handle_new_ncci: illegal appl %d\n", appl);
		return;
	}
	if ((np = (avmb1_ncci *) kmalloc(sizeof(avmb1_ncci), GFP_ATOMIC)) == 0) {
		printk(KERN_ERR "avmb1_handle_new_ncci: alloc failed ncci 0x%x\n", ncci);
		return;
	}
	if (winsize > CAPI_MAXDATAWINDOW) {
		printk(KERN_ERR "avmb1_handle_new_ncci: winsize %d too big, set to %d\n",
		       winsize, CAPI_MAXDATAWINDOW);
		winsize = CAPI_MAXDATAWINDOW;
	}
	np->applid = appl;
	np->ncci = ncci;
	np->winsize = winsize;
	mq_init(np);
	np->next = APPL(appl)->nccilist;
	APPL(appl)->nccilist = np;
	printk(KERN_INFO "b1capi: appl %d ncci 0x%x up\n", appl, ncci);

}

void avmb1_handle_free_ncci(avmb1_card * card,
			    __u16 appl, __u32 ncci)
{
	if (!VALID_APPLID(appl)) {
		printk(KERN_ERR "avmb1_handle_free_ncci: illegal appl %d\n", appl);
		return;
	}
	if (ncci != 0xffffffff) {
		avmb1_ncci **pp;
		for (pp = &APPL(appl)->nccilist; *pp; pp = &(*pp)->next) {
			if ((*pp)->ncci == ncci) {
				avmb1_ncci *np = *pp;
				*pp = np->next;
				kfree(np);
				printk(KERN_INFO "b1capi: appl %d ncci 0x%x down\n", appl, ncci);
				return;
			}
		}
		printk(KERN_ERR "avmb1_handle_free_ncci: ncci 0x%x not found\n", ncci);
	} else {
		avmb1_ncci **pp, **nextpp;
		for (pp = &APPL(appl)->nccilist; *pp; pp = nextpp) {
			if (NCCI2CTRL((*pp)->ncci) == card->cnr) {
				avmb1_ncci *np = *pp;
				*pp = np->next;
				printk(KERN_INFO "b1capi: appl %d ncci 0x%x down!\n", appl, np->ncci);
				kfree(np);
				nextpp = pp;
			} else {
				nextpp = &(*pp)->next;
			}
		}
		APPL(appl)->releasing--;
		if (APPL(appl)->releasing == 0) {
	                APPL(appl)->signal = 0;
			APPL_MARK_FREE(appl);
			printk(KERN_INFO "b1capi: appl %d down\n", appl);
		}
	}
}

static avmb1_ncci *find_ncci(avmb1_appl * app, __u32 ncci)
{
	avmb1_ncci *np;
	for (np = app->nccilist; np; np = np->next) {
		if (np->ncci == ncci)
			return np;
	}
	return 0;
}

/* -------- Receiver ------------------------------------------ */


static void recv_handler(void *dummy)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&recv_queue)) != 0) {
		__u16 appl = CAPIMSG_APPID(skb->data);
		struct avmb1_ncci *np;
		if (!VALID_APPLID(appl)) {
			printk(KERN_ERR "b1capi: recv_handler: applid %d ? (%s)\n",
			       appl, capi_message2str(skb->data));
			kfree_skb(skb, FREE_READ);
			continue;
		}
		if (APPL(appl)->signal == 0) {
			printk(KERN_ERR "b1capi: recv_handler: applid %d has no signal function\n",
			       appl);
			kfree_skb(skb, FREE_READ);
			continue;
		}
		if (   CAPIMSG_COMMAND(skb->data) == CAPI_DATA_B3
		    && CAPIMSG_SUBCOMMAND(skb->data) == CAPI_CONF
	            && (np = find_ncci(APPL(appl), CAPIMSG_NCCI(skb->data))) != 0
		    && mq_dequeue(np, CAPIMSG_MSGID(skb->data)) == 0) {
			printk(KERN_ERR "b1capi: msgid %hu ncci 0x%x not on queue\n",
				CAPIMSG_MSGID(skb->data), np->ncci);
		}
		skb_queue_tail(&APPL(appl)->recv_queue, skb);
		(APPL(appl)->signal) (APPL(appl)->applid, APPL(appl)->param);
	}
}


void avmb1_handle_capimsg(avmb1_card * card, __u16 appl, struct sk_buff *skb)
{
	if (card->cardstate != CARD_RUNNING) {
		printk(KERN_INFO "b1capi: controller %d not active, got: %s",
		       card->cnr, capi_message2str(skb->data));
		goto error;
		return;
	}
	skb_queue_tail(&recv_queue, skb);
	queue_task(&tq_recv_notify, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
	return;

      error:
	kfree_skb(skb, FREE_READ);
}

void avmb1_interrupt(int interrupt, void *devptr, struct pt_regs *regs)
{
	avmb1_card *card;

	card = (avmb1_card *) devptr;

	if (!card) {
		printk(KERN_WARNING "avmb1_interrupt: wrong device\n");
		return;
	}
	if (card->interrupt) {
		printk(KERN_ERR "avmb1_interrupt: reentering interrupt hander\n");
		return;
	}

	card->interrupt = 1;

	B1_handle_interrupt(card);

	card->interrupt = 0;
}

/* -------- Notifier ------------------------------------------ */

static void notify_up(__u16 contr)
{
	struct capi_interface_user *p;

	for (p = capi_users; p; p = p->next) {
		if (p->callback)
			(*p->callback) (KCI_CONTRUP, contr,
				(capi_profile *)
					CARD(contr)->version[VER_PROFILE]);
	}
}

static void notify_down(__u16 contr)
{
	struct capi_interface_user *p;
	for (p = capi_users; p; p = p->next) {
		if (p->callback)
			(*p->callback) (KCI_CONTRDOWN, contr, 0);
	}
}

static void notify_handler(void *dummy)
{
	__u16 contr;

	for (contr=1; VALID_CARD(contr); contr++)
		 if (test_and_clear_bit(contr, &notify_up_set))
			 notify_up(contr);
	for (contr=1; VALID_CARD(contr); contr++)
		 if (test_and_clear_bit(contr, &notify_down_set))
			 notify_down(contr);
}

/* -------- card ready callback ------------------------------- */

void avmb1_card_ready(avmb1_card * card)
{
	__u16 appl;

	card->cversion.majorversion = 2;
	card->cversion.minorversion = 0;
	card->cversion.majormanuversion = (card->version[VER_DRIVER][0] - '0') << 4;
	card->cversion.majormanuversion |= (card->version[VER_DRIVER][2] - '0');
	card->cversion.minormanuversion = (card->version[VER_DRIVER][3] - '0') << 4;
	card->cversion.minormanuversion |= (card->version[VER_DRIVER][5] - '0') * 10;
	card->cversion.minormanuversion |= (card->version[VER_DRIVER][6] - '0');
	card->cardstate = CARD_RUNNING;


	for (appl = 1; appl <= CAPI_MAXAPPL; appl++) {
		if (VALID_APPLID(appl) && !APPL(appl)->releasing) {
			B1_send_register(card->port, appl,
				1024 * (APPL(appl)->rparam.level3cnt+1),
				APPL(appl)->rparam.level3cnt,
				APPL(appl)->rparam.datablkcnt,
				APPL(appl)->rparam.datablklen);
		}
	}

        set_bit(CARDNR(card), &notify_up_set);
        queue_task(&tq_state_notify, &tq_scheduler);
}

/* ------------------------------------------------------------- */

int avmb1_addcard(int port, int irq)
{
	struct avmb1_card *card;
	int irqval;


	card = &cards[ncards];
	memset(card, 0, sizeof(avmb1_card));
	sprintf(card->name, "avmb1-%d", ncards + 1);

	request_region(port, AVMB1_PORTLEN, card->name);

	if ((irqval = request_irq(irq, avmb1_interrupt, SA_SHIRQ, card->name, card)) != 0) {
		printk(KERN_ERR "b1capi: unable to get IRQ %d (irqval=%d).\n",
		       irq, irqval);
		release_region((unsigned short) port, AVMB1_PORTLEN);
		return -EIO;
	}
	ncards++;
	card->cnr = ncards;
	card->port = port;
	card->irq = irq;
	card->cardstate = CARD_DETECTED;
	return 0;
}

int avmb1_probecard(int port, int irq)
{
	int rc;

	if (check_region((unsigned short) port, AVMB1_PORTLEN)) {
		printk(KERN_WARNING
		       "b1capi: ports 0x%03x-0x%03x in use.\n",
		       portbase, portbase + AVMB1_PORTLEN);
		return -EIO;
	}
	if (!B1_valid_irq(irq)) {
		printk(KERN_WARNING "b1capi: irq %d not valid.\n", irq);
		return -EIO;
	}
	if ((rc = B1_detect(port)) != 0) {
		printk(KERN_NOTICE "b1capi: NO card at 0x%x (%d)\n", port, rc);
		return -EIO;
	}
	B1_reset(port);
	printk(KERN_NOTICE "b1capi: AVM-B1-Controller detected at 0x%x\n", port);

	return 0;
}

/* ------------------------------------------------------------- */
/* -------- CAPI2.0 Interface ---------------------------------- */
/* ------------------------------------------------------------- */

static int capi_installed(void)
{
	int i;
	for (i = 0; i < ncards; i++) {
		if (cards[i].cardstate == CARD_RUNNING)
			return 1;
	}
	return 0;
}

static __u16 capi_register(capi_register_params * rparam, __u16 * applidp)
{
	int i;
	int appl;

	if (rparam->datablklen < 128)
		return CAPI_LOGBLKSIZETOSMALL;

	for (appl = 1; appl <= CAPI_MAXAPPL; appl++) {
		if (APPL_IS_FREE(appl))
			break;
	}
	if (appl > CAPI_MAXAPPL)
		return CAPI_TOOMANYAPPLS;

	APPL_MARK_USED(appl);
	skb_queue_head_init(&APPL(appl)->recv_queue);

	memcpy(&APPL(appl)->rparam, rparam, sizeof(capi_register_params));

	for (i = 0; i < ncards; i++) {
		if (cards[i].cardstate != CARD_RUNNING)
			continue;
		B1_send_register(cards[i].port, appl,
			       1024 * (APPL(appl)->rparam.level3cnt + 1),
				 APPL(appl)->rparam.level3cnt,
				 APPL(appl)->rparam.datablkcnt,
				 APPL(appl)->rparam.datablklen);
	}
	*applidp = appl;
	printk(KERN_INFO "b1capi: appl %d up\n", appl);

	return CAPI_NOERROR;
}

static __u16 capi_release(__u16 applid)
{
	struct sk_buff *skb;
	int i;

	if (ncards == 0)
		return CAPI_REGNOTINSTALLED;
	if (!VALID_APPLID(applid) || APPL(applid)->releasing)
		return CAPI_ILLAPPNR;
	while ((skb = skb_dequeue(&APPL(applid)->recv_queue)) != 0)
		kfree_skb(skb, FREE_READ);
	for (i = 0; i < ncards; i++) {
		if (cards[i].cardstate != CARD_RUNNING)
			continue;
		APPL(applid)->releasing++;
		B1_send_release(cards[i].port, applid);
	}
	if (APPL(applid)->releasing == 0) {
	        APPL(applid)->signal = 0;
		APPL_MARK_FREE(applid);
		printk(KERN_INFO "b1capi: appl %d down\n", applid);
	}
	return CAPI_NOERROR;
}

static __u16 capi_put_message(__u16 applid, struct sk_buff *skb)
{
	avmb1_ncci *np;
	int contr;
	if (ncards == 0)
		return CAPI_REGNOTINSTALLED;
	if (!VALID_APPLID(applid))
		return CAPI_ILLAPPNR;
	if (skb->len < 12
	    || !capi_cmd_valid(CAPIMSG_COMMAND(skb->data))
	    || !capi_subcmd_valid(CAPIMSG_SUBCOMMAND(skb->data)))
		return CAPI_ILLCMDORSUBCMDORMSGTOSMALL;
	contr = CAPIMSG_CONTROLLER(skb->data);
	if (!VALID_CARD(contr) || CARD(contr)->cardstate != CARD_RUNNING) {
		contr = 1;
	        if (CARD(contr)->cardstate != CARD_RUNNING) 
			return CAPI_REGNOTINSTALLED;
	}
	if (CARD(contr)->blocked)
		return CAPI_SENDQUEUEFULL;

	if (   CAPIMSG_COMMAND(skb->data) == CAPI_DATA_B3
	    && CAPIMSG_SUBCOMMAND(skb->data) == CAPI_REQ
	    && (np = find_ncci(APPL(applid), CAPIMSG_NCCI(skb->data))) != 0
	    && mq_enqueue(np, CAPIMSG_MSGID(skb->data)) == 0)
		return CAPI_SENDQUEUEFULL;

	B1_send_message(CARD(contr)->port, skb);
	return CAPI_NOERROR;
}

static __u16 capi_get_message(__u16 applid, struct sk_buff **msgp)
{
	struct sk_buff *skb;

	if (!VALID_APPLID(applid))
		return CAPI_ILLAPPNR;
	if ((skb = skb_dequeue(&APPL(applid)->recv_queue)) == 0)
		return CAPI_RECEIVEQUEUEEMPTY;
	*msgp = skb;
	return CAPI_NOERROR;
}

static __u16 capi_set_signal(__u16 applid,
			     void (*signal) (__u16 applid, __u32 param),
			     __u32 param)
{
	if (!VALID_APPLID(applid))
		return CAPI_ILLAPPNR;
	APPL(applid)->signal = signal;
	APPL(applid)->param = param;
	return CAPI_NOERROR;
}

static __u16 capi_get_manufacturer(__u16 contr, __u8 buf[CAPI_MANUFACTURER_LEN])
{
	if (contr == 0) {
		strncpy(buf, capi_manufakturer, CAPI_MANUFACTURER_LEN);
		return CAPI_NOERROR;
	}
	if (!VALID_CARD(contr) || CARD(contr)->cardstate != CARD_RUNNING) 
		return 0x2002;

	strncpy(buf, capi_manufakturer, CAPI_MANUFACTURER_LEN);
	return CAPI_NOERROR;
}

static __u16 capi_get_version(__u16 contr, struct capi_version *verp)
{
	if (contr == 0) {
		*verp = driver_version;
		return CAPI_NOERROR;
	}
	if (!VALID_CARD(contr) || CARD(contr)->cardstate != CARD_RUNNING) 
		return 0x2002;

	memcpy((void *) verp, CARD(contr)->version[VER_SERIAL],
	       sizeof(capi_version));
	return CAPI_NOERROR;
}

static __u16 capi_get_serial(__u16 contr, __u8 serial[CAPI_SERIAL_LEN])
{
	if (contr == 0) {
		strncpy(serial, driver_serial, 8);
		return CAPI_NOERROR;
	}
	if (!VALID_CARD(contr) || CARD(contr)->cardstate != CARD_RUNNING) 
		return 0x2002;

	memcpy((void *) serial, CARD(contr)->version[VER_SERIAL],
	       CAPI_SERIAL_LEN);
	serial[CAPI_SERIAL_LEN - 1] = 0;
	return CAPI_NOERROR;
}

static __u16 capi_get_profile(__u16 contr, struct capi_profile *profp)
{
	if (contr == 0) {
		profp->ncontroller = ncards;
		return CAPI_NOERROR;
	}
	if (!VALID_CARD(contr) || CARD(contr)->cardstate != CARD_RUNNING) 
		return 0x2002;

	memcpy((void *) profp, CARD(contr)->version[VER_PROFILE],
	       sizeof(struct capi_profile));
	return CAPI_NOERROR;
}

static int capi_manufacturer(unsigned int cmd, void *data)
{
	unsigned long flags;
	avmb1_loaddef ldef;
	avmb1_carddef cdef;
	avmb1_resetdef rdef;
	avmb1_card *card;
	int rc;

	switch (cmd) {
	case AVMB1_ADDCARD:
		if ((rc = copy_from_user((void *) &cdef, data,
					 sizeof(avmb1_carddef))))
			return rc;
		if (!B1_valid_irq(cdef.irq))
			return -EINVAL;

		if ((rc = avmb1_probecard(cdef.port, cdef.irq)) != 0)
			return rc;

		return avmb1_addcard(cdef.port, cdef.irq);

	case AVMB1_LOAD:

		if ((rc = copy_from_user((void *) &ldef, data,
					 sizeof(avmb1_loaddef))))
			return rc;
		if (!VALID_CARD(ldef.contr) || ldef.t4file.len <= 0) {
			if (loaddebug)
				printk(KERN_DEBUG "b1capi: load: invalid parameter contr=%d len=%d\n", ldef.contr, ldef.t4file.len);
			return -EINVAL;
		}

		card = CARD(ldef.contr);
		save_flags(flags);
		cli();
		if (card->cardstate != CARD_DETECTED) {
			restore_flags(flags);
			if (loaddebug)
				printk(KERN_DEBUG "b1capi: load: contr=%d not in detect state\n", ldef.contr);
			return -EBUSY;
		}
		card->cardstate = CARD_LOADING;
		restore_flags(flags);

		if (loaddebug) {
			printk(KERN_DEBUG "b1capi: load: reseting contr %d\n",
				ldef.contr);
		}

		B1_reset(card->port);
		if ((rc = B1_load_t4file(card->port, &ldef.t4file))) {
			B1_reset(card->port);
			printk(KERN_ERR "b1capi: failed to load t4file!!\n");
			card->cardstate = CARD_DETECTED;
			return rc;
		}
		B1_disable_irq(card->port);
		if (loaddebug) {
			printk(KERN_DEBUG "b1capi: load: ready contr %d: checking\n",
				ldef.contr);
		}

		if (!B1_loaded(card->port)) {
			card->cardstate = CARD_DETECTED;
			printk(KERN_ERR "b1capi: failed to load t4file.\n");
			return -EIO;
		}
		/*
		 * enable interrupt
		 */

		card->cardstate = CARD_INITSTATE;
		save_flags(flags);
		cli();
		B1_assign_irq(card->port, card->irq);
		B1_enable_irq(card->port);
		restore_flags(flags);

		if (loaddebug) {
			printk(KERN_DEBUG "b1capi: load: irq enabled contr %d\n",
				ldef.contr);
		}

		/*
		 * init card
		 */
		B1_send_init(card->port, AVM_NAPPS, AVM_NNCCI, card->cnr - 1);

		if (loaddebug) {
			printk(KERN_DEBUG "b1capi: load: waiting for init reply contr %d\n",
				ldef.contr);
		}

		while (card->cardstate != CARD_RUNNING) {

			current->timeout = jiffies + HZ / 10;	/* 0.1 sec */
			current->state = TASK_INTERRUPTIBLE;
			schedule();

			if (signal_pending(current))
				return -EINTR;
		}
		return 0;
	case AVMB1_RESETCARD:
		if ((rc = copy_from_user((void *) &rdef, data,
					 sizeof(avmb1_resetdef))))
			return rc;

		if (!VALID_CARD(rdef.contr))
			return -EINVAL;

		card = CARD(rdef.contr);

		if (card->cardstate == CARD_RUNNING)
			return -EBUSY;

		B1_reset(card->port);
		B1_reset(card->port);

		card->cardstate = CARD_DETECTED;
		return 0;
	}
	return -EINVAL;
}

struct capi_interface avmb1_interface =
{
	capi_installed,
	capi_register,
	capi_release,
	capi_put_message,
	capi_get_message,
	capi_set_signal,
	capi_get_manufacturer,
	capi_get_version,
	capi_get_serial,
	capi_get_profile,
	capi_manufacturer
};

/* ------------------------------------------------------------- */
/* -------- Exported Functions --------------------------------- */
/* ------------------------------------------------------------- */

struct capi_interface *attach_capi_interface(struct capi_interface_user *userp)
{
	struct capi_interface_user *p;

	for (p = capi_users; p; p = p->next) {
		if (p == userp) {
			printk(KERN_ERR "b1capi: double attach from %s\n",
			       userp->name);
			return 0;
		}
	}
	userp->next = capi_users;
	capi_users = userp;
	MOD_INC_USE_COUNT;

	return &avmb1_interface;
}

int detach_capi_interface(struct capi_interface_user *userp)
{
	struct capi_interface_user **pp;

	for (pp = &capi_users; *pp; pp = &(*pp)->next) {
		if (*pp == userp) {
			*pp = userp->next;
			userp->next = 0;
			MOD_DEC_USE_COUNT;
			return 0;
		}
	}
	printk(KERN_ERR "b1capi: double detach from %s\n", userp->name);
	return -1;
}

/* ------------------------------------------------------------- */
/* -------- Init & Cleanup ------------------------------------- */
/* ------------------------------------------------------------- */

#ifdef HAS_NEW_SYMTAB
EXPORT_SYMBOL(attach_capi_interface);
EXPORT_SYMBOL(detach_capi_interface);
EXPORT_SYMBOL(avmb1_addcard);
EXPORT_SYMBOL(avmb1_probecard);
#else
static struct symbol_table capidev_syms =
{
#include <linux/symtab_begin.h>
	X(attach_capi_interface),
	X(detach_capi_interface),
	X(avmb1_addcard),
	X(avmb1_probecard),
#include <linux/symtab_end.h>
};
#endif


/*
 * init / exit functions
 */

#ifdef MODULE
#define avmb1_init init_module
#endif

int avmb1_init(void)
{
	char *p;
	char rev[10];


#ifndef HAS_NEW_SYMTAB
	/* No symbols to export, hide all symbols */
	register_symtab(&capidev_syms);
#endif
	skb_queue_head_init(&recv_queue);
	/* init_bh(CAPI_BH, do_capi_bh); */

	tq_state_notify.routine = notify_handler;
	tq_state_notify.data = 0;

	tq_recv_notify.routine = recv_handler;
	tq_recv_notify.data = 0;


	if ((p = strchr(revision, ':'))) {
		strcpy(rev, p + 1);
		p = strchr(rev, '$');
		*p = 0;
	} else
		strcpy(rev, " ??? ");

#ifdef MODULE
	if (portbase) {
	        int rc;
		if ((rc = avmb1_probecard(portbase, irq)) != 0)
			return rc;
		if ((rc = avmb1_addcard(portbase, irq)) != 0)
			return rc;
	} else {
		printk(KERN_NOTICE "AVM-B1-CAPI-driver Rev%s: loaded\n", rev);
	}
#else
	printk(KERN_NOTICE "AVM-B1-CAPI-driver Rev%s: started\n", rev);
#endif
	return 0;
}

#ifdef MODULE
void cleanup_module(void)
{
	char rev[10];
	char *p;
	int i;

	if ((p = strchr(revision, ':'))) {
		strcpy(rev, p + 1);
		p = strchr(rev, '$');
		*p = 0;
	} else {
		strcpy(rev, " ??? ");
	}

	for (i = 0; i < ncards; i++) {
		/*
		 * disable card
		 */
		B1_disable_irq(cards[i].port);
		B1_reset(cards[i].port);
		B1_reset(cards[i].port);
		/*
		 * free kernel resources
		 */
		free_irq(cards[i].irq, &cards[i]);
		release_region(cards[i].port, AVMB1_PORTLEN);

	}
	printk(KERN_NOTICE "AVM-B1-CAPI-driver Rev%s: unloaded\n", rev);
}
#endif
