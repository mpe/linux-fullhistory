/* $Id: isdnl1.c,v 1.15 1997/05/27 15:17:55 fritz Exp $

 * isdnl1.c     common low level stuff for Siemens Chipsetbased isdn cards
 *              based on the teles driver from Jan den Ouden
 *
 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *
 * Thanks to    Jan den Ouden
 *              Fritz Elfert
 *              Beat Doebeli
 *
 *
 * $Log: isdnl1.c,v $
 * Revision 1.15  1997/05/27 15:17:55  fritz
 * Added changes for recent 2.1.x kernels:
 *   changed return type of isdn_close
 *   queue_task_* -> queue_task
 *   clear/set_bit -> test_and_... where apropriate.
 *   changed type of hard_header_cache parameter.
 *
 * Revision 1.14  1997/04/07 23:00:08  keil
 * GFP_KERNEL ---> GFP_ATOMIC
 *
 * Revision 1.13  1997/04/06 22:55:50  keil
 * Using SKB's
 *
 * Revision 1.12  1997/03/26 13:43:57  keil
 * small cosmetics
 *
 * Revision 1.11  1997/03/25 23:11:23  keil
 * US NI-1 protocol
 *
 * Revision 1.10  1997/03/13 14:45:05  keil
 * using IRQ proof queue_task
 *
 * Revision 1.9  1997/03/12 21:44:21  keil
 * change Interrupt routine from atomic quick to normal
 *
 * Revision 1.8  1997/02/09 00:24:31  keil
 * new interface handling, one interface per card
 *
 * Revision 1.7  1997/01/27 15:56:03  keil
 * PCMCIA Teles card and ITK ix1 micro added
 *
 * Revision 1.6  1997/01/21 22:20:00  keil
 * changes for D-channel log; Elsa Quickstep support
 *
 * Revision 1.5  1997/01/10 12:51:19  keil
 * cleanup; set newversion
 *
 * Revision 1.4  1996/12/08 19:44:53  keil
 * L2FRAME_DEBUG and other changes from Pekka Sarnila
 *
 * Revision 1.3  1996/11/18 15:34:47  keil
 * fix HSCX version code
 *
 * Revision 1.2  1996/10/27 22:16:54  keil
 * ISAC/HSCX version lookup
 *
 * Revision 1.1  1996/10/13 20:04:53  keil
 * Initial revision
 *
 *
 *
 */

const char *l1_revision = "$Revision: 1.15 $";

#define __NO_VERSION__
#include <linux/config.h>
#include "hisax.h"
#include "isdnl1.h"

#if CARD_TELES0
#include "teles0.h"
#endif

#if CARD_TELES3
#include "teles3.h"
#endif

#if CARD_AVM_A1
#include "avm_a1.h"
#endif

#if CARD_ELSA
#include "elsa.h"
#endif

#if CARD_IX1MICROR2
#include "ix1_micro.h"
#endif

/* #define I4L_IRQ_FLAG SA_INTERRUPT */
#define I4L_IRQ_FLAG    0

#define HISAX_STATUS_BUFSIZE 4096

#define INCLUDE_INLINE_FUNCS
#include <linux/tqueue.h>
#include <linux/interrupt.h>

const char *CardType[] =
{"No Card", "Teles 16.0", "Teles 8.0", "Teles 16.3",
 "Creatix/Teles PnP", "AVM A1", "Elsa ML",
#ifdef CONFIG_HISAX_ELSA_PCMCIA
 "Elsa PCMCIA",
#else
 "Elsa Quickstep",
#endif
 "Teles PCMCIA", "ITK ix1-micro Rev.2"};

static char *HSCXVer[] =
{"A1", "?1", "A2", "?3", "A3", "V2.1", "?6", "?7",
 "?8", "?9", "?10", "?11", "?12", "?13", "?14", "???"};

static char *ISACVer[] =
{"2086/2186 V1.1", "2085 B1", "2085 B2",
 "2085 V2.3"};

extern struct IsdnCard cards[];
extern int nrcards;
extern char *HiSax_id;

/*
 * Find card with given driverId
 */
static inline struct IsdnCardState
*
hisax_findcard(int driverid)
{
	int i;

	for (i = 0; i < nrcards; i++)
		if (cards[i].sp)
			if (cards[i].sp->myid == driverid)
				return (cards[i].sp);
	return (struct IsdnCardState *) 0;
}

int
HiSax_readstatus(u_char * buf, int len, int user, int id, int channel)
{
	int count;
	u_char *p;
	struct IsdnCardState *csta = hisax_findcard(id);

	if (csta) {
		for (p = buf, count = 0; count < len; p++, count++) {
			if (user)
				put_user(*csta->status_read++, p);
			else
				*p++ = *csta->status_read++;
			if (csta->status_read > csta->status_end)
				csta->status_read = csta->status_buf;
		}
		return count;
	} else {
		printk(KERN_ERR
		 "HiSax: if_readstatus called with invalid driverId!\n");
		return -ENODEV;
	}
}

void
HiSax_putstatus(struct IsdnCardState *csta, char *buf)
{
	long flags;
	int len, count, i;
	u_char *p;
	isdn_ctrl ic;

	save_flags(flags);
	cli();
	count = 0;
	len = strlen(buf);

	if (!csta) {
		printk(KERN_WARNING "HiSax: No CardStatus for message %s", buf);
		restore_flags(flags);
		return;
	}
	for (p = buf, i = len; i > 0; i--, p++) {
		*csta->status_write++ = *p;
		if (csta->status_write > csta->status_end)
			csta->status_write = csta->status_buf;
		count++;
	}
	restore_flags(flags);
	if (count) {
		ic.command = ISDN_STAT_STAVAIL;
		ic.driver = csta->myid;
		ic.arg = count;
		csta->iif.statcallb(&ic);
	}
}

int
ll_run(struct IsdnCardState *csta)
{
	long flags;
	isdn_ctrl ic;

	save_flags(flags);
	cli();
	ic.driver = csta->myid;
	ic.command = ISDN_STAT_RUN;
	csta->iif.statcallb(&ic);
	restore_flags(flags);
	return 0;
}

void
ll_stop(struct IsdnCardState *csta)
{
	isdn_ctrl ic;

	ic.command = ISDN_STAT_STOP;
	ic.driver = csta->myid;
	csta->iif.statcallb(&ic);
	CallcFreeChan(csta);
}

static void
ll_unload(struct IsdnCardState *csta)
{
	isdn_ctrl ic;

	ic.command = ISDN_STAT_UNLOAD;
	ic.driver = csta->myid;
	csta->iif.statcallb(&ic);
	if (csta->status_buf)
		kfree(csta->status_buf);
	csta->status_read = NULL;
	csta->status_write = NULL;
	csta->status_end = NULL;
	kfree(csta->dlogspace);
}

void
debugl1(struct IsdnCardState *sp, char *msg)
{
	char tmp[256], tm[32];

	jiftime(tm, jiffies);
	sprintf(tmp, "%s Card %d %s\n", tm, sp->cardnr + 1, msg);
	HiSax_putstatus(sp, tmp);
}

/*
 * HSCX stuff goes here
 */


char *
HscxVersion(u_char v)
{
	return (HSCXVer[v & 0xf]);
}

void
hscx_sched_event(struct HscxState *hsp, int event)
{
	hsp->event |= 1 << event;
	queue_task(&hsp->tqueue, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
}

/*
 * ISAC stuff goes here
 */

char *
ISACVersion(u_char v)
{
	return (ISACVer[(v >> 5) & 3]);
}

void
isac_sched_event(struct IsdnCardState *sp, int event)
{
	sp->event |= 1 << event;
	queue_task(&sp->tqueue, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
}

int
act_wanted(struct IsdnCardState *sp)
{
	struct PStack *st;

	st = sp->stlist;
	while (st)
		if (st->l1.act_state)
			return (!0);
		else
			st = st->next;
	return (0);
}

void
isac_new_ph(struct IsdnCardState *sp)
{
	int enq;

	enq = act_wanted(sp);

	switch (sp->ph_state) {
		case (6):
			sp->ph_active = 0;
			sp->ph_command(sp, 15);
			break;
		case (15):
			sp->ph_active = 0;
			if (enq)
				sp->ph_command(sp, 0);
			break;
		case (0):
			sp->ph_active = 0;
			if (enq)
				sp->ph_command(sp, 0);
#if 0
			else
				sp->ph_command(sp, 15);
#endif
			break;
		case (7):
			sp->ph_active = 0;
			if (enq)
				sp->ph_command(sp, 9);
			break;
		case (12):
			sp->ph_command(sp, 8);
			sp->ph_active = 5;
			isac_sched_event(sp, ISAC_PHCHANGE);
			if (!sp->tx_skb)
				sp->tx_skb = skb_dequeue(&sp->sq);
			if (sp->tx_skb) {
				sp->tx_cnt = 0;
				sp->isac_fill_fifo(sp);
			}
			break;
		case (13):
			sp->ph_command(sp, 9);
			sp->ph_active = 5;
			isac_sched_event(sp, ISAC_PHCHANGE);
			if (!sp->tx_skb)
				sp->tx_skb = skb_dequeue(&sp->sq);
			if (sp->tx_skb) {
				sp->tx_cnt = 0;
				sp->isac_fill_fifo(sp);
			}
			break;
		case (4):
		case (8):
			sp->ph_active = 0;
			break;
		default:
			sp->ph_active = 0;
			break;
	}
}

static void
restart_ph(struct IsdnCardState *sp)
{
	if (!sp->ph_active) {
		if ((sp->ph_state == 6) || (sp->ph_state == 0)) {
			sp->ph_command(sp, 0);
			sp->ph_active = 2;
		} else {
			sp->ph_command(sp, 1);
			sp->ph_active = 1;
		}
	} else if (sp->ph_active == 2) {
		sp->ph_command(sp, 1);
		sp->ph_active = 1;
	}
}


static void
act_ivated(struct IsdnCardState *sp)
{
	struct PStack *st;

	st = sp->stlist;
	while (st) {
		if (st->l1.act_state == 1) {
			st->l1.act_state = 2;
			st->l1.l1man(st, PH_ACTIVATE, NULL);
		}
		st = st->next;
	}
}

static void
process_new_ph(struct IsdnCardState *sp)
{
	if (sp->ph_active == 5)
		act_ivated(sp);
}

static void
process_xmt(struct IsdnCardState *sp)
{
	struct PStack *stptr;

	if (sp->tx_skb)
		return;

	stptr = sp->stlist;
	while (stptr != NULL)
		if (stptr->l1.requestpull) {
			stptr->l1.requestpull = 0;
			stptr->l1.l1l2(stptr, PH_PULL_ACK, NULL);
			break;
		} else
			stptr = stptr->next;
}

static void
process_rcv(struct IsdnCardState *sp)
{
	struct sk_buff *skb, *nskb;
	struct PStack *stptr;
	int found, broadc;
	char tmp[64];

	while ((skb = skb_dequeue(&sp->rq))) {
#ifdef L2FRAME_DEBUG		/* psa */
		if (sp->debug & L1_DEB_LAPD)
			Logl2Frame(sp, skb, "PH_DATA", 1);
#endif
		stptr = sp->stlist;
		broadc = (skb->data[1] >> 1) == 127;

		if (broadc) {
			if (!(skb->data[0] >> 2)) {	/* sapi 0 */
				sp->CallFlags = 3;
				if (sp->dlogflag) {
					LogFrame(sp, skb->data, skb->len);
					dlogframe(sp, skb->data + 3, skb->len - 3,
						  "Q.931 frame network->user broadcast");
				}
			}
			while (stptr != NULL) {
				if ((skb->data[0] >> 2) == stptr->l2.sap)
					if ((nskb = skb_clone(skb, GFP_ATOMIC)))
						stptr->l1.l1l2(stptr, PH_DATA, nskb);
					else
						printk(KERN_WARNING "HiSax: isdn broadcast buffer shortage\n");
				stptr = stptr->next;
			}
			SET_SKB_FREE(skb);
			dev_kfree_skb(skb, FREE_READ);
		} else {
			found = 0;
			while (stptr != NULL)
				if (((skb->data[0] >> 2) == stptr->l2.sap) &&
				((skb->data[1] >> 1) == stptr->l2.tei)) {
					stptr->l1.l1l2(stptr, PH_DATA, skb);
					found = !0;
					break;
				} else
					stptr = stptr->next;
			if (!found) {
				/* BD 10.10.95
				 * Print out D-Channel msg not processed
				 * by isdn4linux
				 */

				if ((!(skb->data[0] >> 2)) && (!(skb->data[2] & 0x01))) {
					sprintf(tmp,
						"Q.931 frame network->user with tei %d (not for us)",
						skb->data[1] >> 1);
					LogFrame(sp, skb->data, skb->len);
					dlogframe(sp, skb->data + 4, skb->len - 4, tmp);
				}
				SET_SKB_FREE(skb);
				dev_kfree_skb(skb, FREE_READ);
			}
		}

	}

}

static void
isac_bh(struct IsdnCardState *sp)
{
	if (!sp)
		return;

	if (test_and_clear_bit(ISAC_PHCHANGE, &sp->event))
		process_new_ph(sp);
	if (test_and_clear_bit(ISAC_RCVBUFREADY, &sp->event))
		process_rcv(sp);
	if (test_and_clear_bit(ISAC_XMTBUFREADY, &sp->event))
		process_xmt(sp);
}

static void
l2l1(struct PStack *st, int pr, void *arg)
{
	struct IsdnCardState *sp = (struct IsdnCardState *) st->l1.hardware;
	struct sk_buff *skb = arg;
	char str[64];

	switch (pr) {
		case (PH_DATA):
			if (sp->tx_skb) {
				skb_queue_tail(&sp->sq, skb);
#ifdef L2FRAME_DEBUG		/* psa */
				if (sp->debug & L1_DEB_LAPD)
					Logl2Frame(sp, skb, "PH_DATA Queued", 0);
#endif
			} else {
				if ((sp->dlogflag) && (!(skb->data[2] & 1))) {	/* I-FRAME */
					LogFrame(sp, skb->data, skb->len);
					sprintf(str, "Q.931 frame user->network tei %d", st->l2.tei);
					dlogframe(sp, skb->data + st->l2.ihsize, skb->len - st->l2.ihsize,
						  str);
				}
				sp->tx_skb = skb;
				sp->tx_cnt = 0;
#ifdef L2FRAME_DEBUG		/* psa */
				if (sp->debug & L1_DEB_LAPD)
					Logl2Frame(sp, skb, "PH_DATA", 0);
#endif
				sp->isac_fill_fifo(sp);
			}
			break;
		case (PH_DATA_PULLED):
			if (sp->tx_skb) {
				if (sp->debug & L1_DEB_WARN)
					debugl1(sp, " l2l1 tx_skb exist this shouldn't happen");
				break;
			}
			if ((sp->dlogflag) && (!(skb->data[2] & 1))) {	/* I-FRAME */
				LogFrame(sp, skb->data, skb->len);
				sprintf(str, "Q.931 frame user->network tei %d", st->l2.tei);
				dlogframe(sp, skb->data + st->l2.ihsize, skb->len - st->l2.ihsize,
					  str);
			}
			sp->tx_skb = skb;
			sp->tx_cnt = 0;
#ifdef L2FRAME_DEBUG		/* psa */
			if (sp->debug & L1_DEB_LAPD)
				Logl2Frame(sp, skb, "PH_DATA_PULLED", 0);
#endif
			sp->isac_fill_fifo(sp);
			break;
		case (PH_REQUEST_PULL):
#ifdef L2FRAME_DEBUG		/* psa */
			if (sp->debug & L1_DEB_LAPD)
				debugl1(sp, "-> PH_REQUEST_PULL");
#endif
			if (!sp->tx_skb) {
				st->l1.requestpull = 0;
				st->l1.l1l2(st, PH_PULL_ACK, NULL);
			} else
				st->l1.requestpull = !0;
			break;
	}
}


static void
hscx_process_xmt(struct HscxState *hsp)
{
	struct PStack *st = hsp->st;

	if (hsp->tx_skb)
		return;

	if (st->l1.requestpull) {
		st->l1.requestpull = 0;
		st->l1.l1l2(st, PH_PULL_ACK, NULL);
	}
	if (!hsp->active)
		if ((!hsp->tx_skb) && (!skb_queue_len(&hsp->squeue)))
			hsp->sp->modehscx(hsp, 0, 0);
}

static void
hscx_process_rcv(struct HscxState *hsp)
{
	struct sk_buff *skb;

#ifdef DEBUG_MAGIC
	if (hsp->magic != 301270) {
		printk(KERN_DEBUG "hscx_process_rcv magic not 301270\n");
		return;
	}
#endif
	while ((skb = skb_dequeue(&hsp->rqueue))) {
		hsp->st->l1.l1l2(hsp->st, PH_DATA, skb);
	}
}

static void
hscx_bh(struct HscxState *hsp)
{

	if (!hsp)
		return;

	if (test_and_clear_bit(HSCX_RCVBUFREADY, &hsp->event))
		hscx_process_rcv(hsp);
	if (test_and_clear_bit(HSCX_XMTBUFREADY, &hsp->event))
		hscx_process_xmt(hsp);

}

/*
 * interrupt stuff ends here
 */

void
HiSax_addlist(struct IsdnCardState *sp,
	      struct PStack *st)
{
	st->next = sp->stlist;
	sp->stlist = st;
}

void
HiSax_rmlist(struct IsdnCardState *sp,
	     struct PStack *st)
{
	struct PStack *p;

	if (sp->stlist == st)
		sp->stlist = st->next;
	else {
		p = sp->stlist;
		while (p)
			if (p->next == st) {
				p->next = st->next;
				return;
			} else
				p = p->next;
	}
}

static void
check_ph_act(struct IsdnCardState *sp)
{
	struct PStack *st = sp->stlist;

	while (st) {
		if (st->l1.act_state)
			return;
		st = st->next;
	}
	if (sp->ph_active == 5)
		sp->ph_active = 4;
}

static void
HiSax_manl1(struct PStack *st, int pr,
	    void *arg)
{
	struct IsdnCardState *sp = (struct IsdnCardState *)
	st->l1.hardware;
	long flags;
	char tmp[32];

	switch (pr) {
		case (PH_ACTIVATE):
			if (sp->debug) {
				sprintf(tmp, "PH_ACT ph_active %d", sp->ph_active);
				debugl1(sp, tmp);
			}
			save_flags(flags);
			cli();
			if (sp->ph_active & 4) {
				sp->ph_active = 5;
				st->l1.act_state = 2;
				restore_flags(flags);
				st->l1.l1man(st, PH_ACTIVATE, NULL);
			} else {
				st->l1.act_state = 1;
				if (sp->ph_active == 0)
					restart_ph(sp);
				restore_flags(flags);
			}
			break;
		case (PH_DEACTIVATE):
			st->l1.act_state = 0;
			if (sp->debug) {
				sprintf(tmp, "PH_DEACT ph_active %d", sp->ph_active);
				debugl1(sp, tmp);
			}
			check_ph_act(sp);
			break;
	}
}

static void
HiSax_l2l1discardq(struct PStack *st, int pr,
		   void *heldby, int releasetoo)
{
	struct IsdnCardState *sp = (struct IsdnCardState *) st->l1.hardware;
	struct sk_buff *skb;

#ifdef DEBUG_MAGIC
	if (sp->magic != 301271) {
		printk(KERN_DEBUG "isac_discardq magic not 301271\n");
		return;
	}
#endif

	while ((skb = skb_dequeue(&sp->sq))) {
		SET_SKB_FREE(skb);
		dev_kfree_skb(skb, FREE_WRITE);
	}
}

void
setstack_HiSax(struct PStack *st, struct IsdnCardState *sp)
{
	st->l1.hardware = sp;
	st->protocol = sp->protocol;

	setstack_tei(st);

	st->l1.stlistp = &(sp->stlist);
	st->l1.act_state = 0;
	st->l2.l2l1 = l2l1;
	st->l2.l2l1discardq = HiSax_l2l1discardq;
	st->ma.manl1 = HiSax_manl1;
	st->l1.requestpull = 0;
}

void
init_hscxstate(struct IsdnCardState *sp,
	       int hscx)
{
	struct HscxState *hsp = sp->hs + hscx;

	hsp->sp = sp;
	hsp->hscx = hscx;

	hsp->tqueue.next = 0;
	hsp->tqueue.sync = 0;
	hsp->tqueue.routine = (void *) (void *) hscx_bh;
	hsp->tqueue.data = hsp;

	hsp->inuse = 0;
	hsp->init = 0;
	hsp->active = 0;

#ifdef DEBUG_MAGIC
	hsp->magic = 301270;
#endif
}

int
get_irq(int cardnr, void *routine)
{
	struct IsdnCard *card = cards + cardnr;
	long flags;

	save_flags(flags);
	cli();
	if (request_irq(card->sp->irq, routine,
			I4L_IRQ_FLAG, "HiSax", NULL)) {
		printk(KERN_WARNING "HiSax: couldn't get interrupt %d\n",
		       card->sp->irq);
		restore_flags(flags);
		return (0);
	}
	irq2dev_map[card->sp->irq] = (void *) card->sp;
	restore_flags(flags);
	return (1);
}

static void
release_irq(int cardnr)
{
	struct IsdnCard *card = cards + cardnr;

	irq2dev_map[card->sp->irq] = NULL;
	free_irq(card->sp->irq, NULL);
}

void
close_hscxstate(struct HscxState *hs)
{
	struct sk_buff *skb;

	hs->sp->modehscx(hs, 0, 0);
	hs->inuse = 0;
	if (hs->init) {
		if (hs->rcvbuf) {
			kfree(hs->rcvbuf);
			hs->rcvbuf = NULL;
		}
		while ((skb = skb_dequeue(&hs->rqueue))) {
			SET_SKB_FREE(skb);
			dev_kfree_skb(skb, FREE_READ);
		}
		while ((skb = skb_dequeue(&hs->squeue))) {
			SET_SKB_FREE(skb);
			dev_kfree_skb(skb, FREE_WRITE);
		}
		if (hs->tx_skb) {
			SET_SKB_FREE(hs->tx_skb);
			dev_kfree_skb(hs->tx_skb, FREE_WRITE);
			hs->tx_skb = NULL;
		}
	}
	hs->init = 0;
}

static void
closecard(int cardnr)
{
	struct IsdnCardState *csta = cards[cardnr].sp;
	struct sk_buff *skb;

	close_hscxstate(csta->hs + 1);
	close_hscxstate(csta->hs);

	if (csta->rcvbuf) {
		kfree(csta->rcvbuf);
		csta->rcvbuf = NULL;
	}
	while ((skb = skb_dequeue(&csta->rq))) {
		SET_SKB_FREE(skb);
		dev_kfree_skb(skb, FREE_READ);
	}
	while ((skb = skb_dequeue(&csta->sq))) {
		SET_SKB_FREE(skb);
		dev_kfree_skb(skb, FREE_WRITE);
	}
	if (csta->tx_skb) {
		SET_SKB_FREE(csta->tx_skb);
		dev_kfree_skb(csta->tx_skb, FREE_WRITE);
		csta->tx_skb = NULL;
	}
	switch (csta->typ) {
#if CARD_TELES0
		case ISDN_CTYPE_16_0:
		case ISDN_CTYPE_8_0:
			release_io_teles0(cards + cardnr);
			break;
#endif
#if CARD_TELES3
		case ISDN_CTYPE_PNP:
		case ISDN_CTYPE_16_3:
		case ISDN_CTYPE_TELESPCMCIA:
			release_io_teles3(cards + cardnr);
			break;
#endif
#if CARD_AVM_A1
		case ISDN_CTYPE_A1:
			release_io_avm_a1(cards + cardnr);
			break;
#endif
#if CARD_ELSA
		case ISDN_CTYPE_ELSA:
		case ISDN_CTYPE_ELSA_QS1000:
			release_io_elsa(cards + cardnr);
			break;
#endif
#if CARD_IX1MICROR2
		case ISDN_CTYPE_IX1MICROR2:
			release_io_ix1micro(cards + cardnr);
			break;
#endif
		default:
			break;
	}
	ll_unload(csta);
}

static int
checkcard(int cardnr, char *id)
{
	long flags;
	int ret = 0;
	struct IsdnCard *card = cards + cardnr;
	struct IsdnCardState *sp;

	save_flags(flags);
	cli();
	if (!(sp = (struct IsdnCardState *)
	      kmalloc(sizeof(struct IsdnCardState), GFP_ATOMIC))) {
		printk(KERN_WARNING
		       "HiSax: No memory for IsdnCardState(card %d)\n",
		       cardnr + 1);
		restore_flags(flags);
		return (0);
	}
	card->sp = sp;
	sp->cardnr = cardnr;
	sp->cfg_reg = 0;
	sp->protocol = card->protocol;

	if ((card->typ > 0) && (card->typ < 31)) {
		if (!((1 << card->typ) & SUPORTED_CARDS)) {
			printk(KERN_WARNING
			     "HiSax: Support for %s Card not selected\n",
			       CardType[card->typ]);
			restore_flags(flags);
			return (0);
		}
	} else {
		printk(KERN_WARNING
		       "HiSax: Card Type %d out of range\n",
		       card->typ);
		restore_flags(flags);
		return (0);
	}
	if (!(sp->dlogspace = kmalloc(4096, GFP_ATOMIC))) {
		printk(KERN_WARNING
		       "HiSax: No memory for dlogspace(card %d)\n",
		       cardnr + 1);
		restore_flags(flags);
		return (0);
	}
	if (!(sp->status_buf = kmalloc(HISAX_STATUS_BUFSIZE, GFP_ATOMIC))) {
		printk(KERN_WARNING
		       "HiSax: No memory for status_buf(card %d)\n",
		       cardnr + 1);
		kfree(sp->dlogspace);
		restore_flags(flags);
		return (0);
	}
	sp->status_read = sp->status_buf;
	sp->status_write = sp->status_buf;
	sp->status_end = sp->status_buf + HISAX_STATUS_BUFSIZE - 1;
	sp->typ = card->typ;
	sp->CallFlags = 0;
	strcpy(sp->iif.id, id);
	sp->iif.channels = 2;
	sp->iif.maxbufsize = MAX_DATA_SIZE;
	sp->iif.hl_hdrlen = MAX_HEADER_LEN;
	sp->iif.features =
	    ISDN_FEATURE_L2_X75I |
	    ISDN_FEATURE_L2_HDLC |
	    ISDN_FEATURE_L2_TRANS |
	    ISDN_FEATURE_L3_TRANS |
#ifdef	CONFIG_HISAX_1TR6
	    ISDN_FEATURE_P_1TR6 |
#endif
#ifdef	CONFIG_HISAX_EURO
	    ISDN_FEATURE_P_EURO |
#endif
#ifdef        CONFIG_HISAX_NI1
	    ISDN_FEATURE_P_NI1 |
#endif
	    0;

	sp->iif.command = HiSax_command;
	sp->iif.writebuf = NULL;
	sp->iif.writecmd = NULL;
	sp->iif.writebuf_skb = HiSax_writebuf_skb;
	sp->iif.readstat = HiSax_readstatus;
	register_isdn(&sp->iif);
	sp->myid = sp->iif.channels;
	restore_flags(flags);
	printk(KERN_NOTICE
	       "HiSax: Card %d Protocol %s Id=%s (%d)\n", cardnr + 1,
	       (card->protocol == ISDN_PTYPE_1TR6) ? "1TR6" :
	       (card->protocol == ISDN_PTYPE_EURO) ? "EDSS1" :
	       (card->protocol == ISDN_PTYPE_LEASED) ? "LEASED" :
	       (card->protocol == ISDN_PTYPE_NI1) ? "NI1" :
	       "NONE", sp->iif.id, sp->myid);
	switch (card->typ) {
#if CARD_TELES0
		case ISDN_CTYPE_16_0:
		case ISDN_CTYPE_8_0:
			ret = setup_teles0(card);
			break;
#endif
#if CARD_TELES3
		case ISDN_CTYPE_16_3:
		case ISDN_CTYPE_PNP:
		case ISDN_CTYPE_TELESPCMCIA:
			ret = setup_teles3(card);
			break;
#endif
#if CARD_AVM_A1
		case ISDN_CTYPE_A1:
			ret = setup_avm_a1(card);
			break;
#endif
#if CARD_ELSA
		case ISDN_CTYPE_ELSA:
		case ISDN_CTYPE_ELSA_QS1000:
			ret = setup_elsa(card);
			break;
#endif
#if CARD_IX1MICROR2
		case ISDN_CTYPE_IX1MICROR2:
			ret = setup_ix1micro(card);
			break;
#endif
		default:
			printk(KERN_WARNING "HiSax: Unknown Card Typ %d\n",
			       card->typ);
			ll_unload(sp);
			return (0);
	}
	if (!ret) {
		ll_unload(sp);
		return (0);
	}
	if (!(sp->rcvbuf = kmalloc(MAX_DFRAME_LEN, GFP_ATOMIC))) {
		printk(KERN_WARNING
		       "HiSax: No memory for isac rcvbuf\n");
		return (1);
	}
	sp->rcvidx = 0;
	sp->tx_skb = NULL;
	sp->tx_cnt = 0;
	sp->event = 0;
	sp->tqueue.next = 0;
	sp->tqueue.sync = 0;
	sp->tqueue.routine = (void *) (void *) isac_bh;
	sp->tqueue.data = sp;

	skb_queue_head_init(&sp->rq);
	skb_queue_head_init(&sp->sq);

	sp->stlist = NULL;
	sp->ph_active = 0;
	sp->dlogflag = 0;
	sp->debug = L1_DEB_WARN;
#ifdef DEBUG_MAGIC
	sp->magic = 301271;
#endif

	init_hscxstate(sp, 0);
	init_hscxstate(sp, 1);

	switch (card->typ) {
#if CARD_TELES0
		case ISDN_CTYPE_16_0:
		case ISDN_CTYPE_8_0:
			ret = initteles0(sp);
			break;
#endif
#if CARD_TELES3
		case ISDN_CTYPE_16_3:
		case ISDN_CTYPE_PNP:
		case ISDN_CTYPE_TELESPCMCIA:
			ret = initteles3(sp);
			break;
#endif
#if CARD_AVM_A1
		case ISDN_CTYPE_A1:
			ret = initavm_a1(sp);
			break;
#endif
#if CARD_ELSA
		case ISDN_CTYPE_ELSA:
		case ISDN_CTYPE_ELSA_QS1000:
			ret = initelsa(sp);
			break;
#endif
#if CARD_IX1MICROR2
		case ISDN_CTYPE_IX1MICROR2:
			ret = initix1micro(sp);
			break;
#endif
		default:
			ret = 0;
			break;
	}
	if (!ret) {
		closecard(cardnr);
		return (0);
	}
	init_tei(sp, sp->protocol);
	CallcNewChan(sp);
	ll_run(sp);
	return (1);
}

void
HiSax_shiftcards(int idx)
{
	int i;

	for (i = idx; i < 15; i++)
		memcpy(&cards[i], &cards[i + 1], sizeof(cards[i]));
}

int
HiSax_inithardware(void)
{
	int foundcards = 0;
	int i = 0;
	int t = ',';
	int flg = 0;
	char *id;
	char *next_id = HiSax_id;
	char ids[20];

	if (strchr(HiSax_id, ','))
		t = ',';
	else if (strchr(HiSax_id, '%'))
		t = '%';

	while (i < nrcards) {
		if (cards[i].typ < 1)
			break;
		id = next_id;
		if ((next_id = strchr(id, t))) {
			*next_id++ = 0;
			strcpy(ids, id);
			flg = i + 1;
		} else {
			next_id = id;
			if (flg >= i)
				strcpy(ids, id);
			else
				sprintf(ids, "%s%d", id, i);
		}
		if (checkcard(i, ids)) {
			foundcards++;
			i++;
		} else {
			printk(KERN_WARNING "HiSax: Card %s not installed !\n",
			       CardType[cards[i].typ]);
			if (cards[i].sp)
				kfree((void *) cards[i].sp);
			cards[i].sp = NULL;
			HiSax_shiftcards(i);
		}
	}
	return foundcards;
}

void
HiSax_closehardware(void)
{
	int i;
	long flags;

	save_flags(flags);
	cli();
	for (i = 0; i < nrcards; i++)
		if (cards[i].sp) {
			ll_stop(cards[i].sp);
			CallcFreeChan(cards[i].sp);
			release_tei(cards[i].sp);
			release_irq(i);
			closecard(i);
			kfree((void *) cards[i].sp);
			cards[i].sp = NULL;
		}
	Isdnl2Free();
	CallcFree();
	restore_flags(flags);
}

static void
hscx_l2l1(struct PStack *st, int pr, void *arg)
{
	struct sk_buff *skb = arg;
	struct IsdnCardState *sp = (struct IsdnCardState *) st->l1.hardware;
	struct HscxState *hsp = sp->hs + st->l1.hscx;
	long flags;

	switch (pr) {
		case (PH_DATA):
			save_flags(flags);
			cli();
			if (hsp->tx_skb) {
				skb_queue_tail(&hsp->squeue, skb);
				restore_flags(flags);
			} else {
				restore_flags(flags);
				hsp->tx_skb = skb;
				hsp->count = 0;
				sp->hscx_fill_fifo(hsp);
			}
			break;
		case (PH_DATA_PULLED):
			if (hsp->tx_skb) {
				printk(KERN_WARNING "hscx_l2l1: this shouldn't happen\n");
				break;
			}
			hsp->tx_skb = skb;
			hsp->count = 0;
			sp->hscx_fill_fifo(hsp);
			break;
		case (PH_REQUEST_PULL):
			if (!hsp->tx_skb) {
				st->l1.requestpull = 0;
				st->l1.l1l2(st, PH_PULL_ACK, NULL);
			} else
				st->l1.requestpull = !0;
			break;
	}

}
extern struct IsdnBuffers *tracebuf;

static void
hscx_l2l1discardq(struct PStack *st, int pr, void *heldby,
		  int releasetoo)
{
	struct IsdnCardState *sp = (struct IsdnCardState *)
	st->l1.hardware;
	struct HscxState *hsp = sp->hs + st->l1.hscx;
	struct sk_buff *skb;

#ifdef DEBUG_MAGIC
	if (hsp->magic != 301270) {
		printk(KERN_DEBUG "hscx_discardq magic not 301270\n");
		return;
	}
#endif

	while ((skb = skb_dequeue(&hsp->squeue))) {
		SET_SKB_FREE(skb);
		dev_kfree_skb(skb, FREE_WRITE);
	}
}

static int
open_hscxstate(struct IsdnCardState *sp,
	       int hscx)
{
	struct HscxState *hsp = sp->hs + hscx;

	if (!hsp->init) {
		if (!(hsp->rcvbuf = kmalloc(HSCX_BUFMAX, GFP_ATOMIC))) {
			printk(KERN_WARNING
			       "HiSax: No memory for hscx_rcvbuf\n");
			return (1);
		}
		skb_queue_head_init(&hsp->rqueue);
		skb_queue_head_init(&hsp->squeue);
	}
	hsp->init = !0;

	hsp->tx_skb = NULL;
	hsp->event = 0;
	hsp->rcvidx = 0;
	hsp->tx_cnt = 0;
	return (0);
}

static void
hscx_manl1(struct PStack *st, int pr,
	   void *arg)
{
	struct IsdnCardState *sp = (struct IsdnCardState *) st->l1.hardware;
	struct HscxState *hsp = sp->hs + st->l1.hscx;

	switch (pr) {
		case (PH_ACTIVATE):
			hsp->active = !0;
			sp->modehscx(hsp, st->l1.hscxmode, st->l1.hscxchannel);
			st->l1.l1man(st, PH_ACTIVATE, NULL);
			break;
		case (PH_DEACTIVATE):
			if (!hsp->tx_skb)
				sp->modehscx(hsp, 0, 0);

			hsp->active = 0;
			break;
	}
}

int
setstack_hscx(struct PStack *st, struct HscxState *hs)
{
	if (open_hscxstate(st->l1.hardware, hs->hscx))
		return (-1);

	st->l1.hscx = hs->hscx;
	st->l2.l2l1 = hscx_l2l1;
	st->ma.manl1 = hscx_manl1;
	st->l2.l2l1discardq = hscx_l2l1discardq;

	st->l1.act_state = 0;
	st->l1.requestpull = 0;

	hs->st = st;
	return (0);
}

void
HiSax_reportcard(int cardnr)
{
	struct IsdnCardState *sp = cards[cardnr].sp;

	printk(KERN_DEBUG "HiSax: reportcard No %d\n", cardnr + 1);
	printk(KERN_DEBUG "HiSax: Type %s\n", CardType[sp->typ]);
	printk(KERN_DEBUG "HiSax: debuglevel %x\n", sp->debug);
	printk(KERN_DEBUG "HiSax: HiSax_reportcard address 0x%lX\n",
	       (ulong) & HiSax_reportcard);
}

#ifdef L2FRAME_DEBUG		/* psa */

char *
l2cmd(u_char cmd)
{
	switch (cmd & ~0x10) {
		case 1:
			return "RR";
		case 5:
			return "RNR";
		case 9:
			return "REJ";
		case 0x6f:
			return "SABME";
		case 0x0f:
			return "DM";
		case 3:
			return "UI";
		case 0x43:
			return "DISC";
		case 0x63:
			return "UA";
		case 0x87:
			return "FRMR";
		case 0xaf:
			return "XID";
		default:
			if (!(cmd & 1))
				return "I";
			else
				return "invalid command";
	}
}

static char tmp[20];

char *
l2frames(u_char * ptr)
{
	switch (ptr[2] & ~0x10) {
		case 1:
		case 5:
		case 9:
			sprintf(tmp, "%s[%d](nr %d)", l2cmd(ptr[2]), ptr[3] & 1, ptr[3] >> 1);
			break;
		case 0x6f:
		case 0x0f:
		case 3:
		case 0x43:
		case 0x63:
		case 0x87:
		case 0xaf:
			sprintf(tmp, "%s[%d]", l2cmd(ptr[2]), (ptr[2] & 0x10) >> 4);
			break;
		default:
			if (!(ptr[2] & 1)) {
				sprintf(tmp, "I[%d](ns %d, nr %d)", ptr[3] & 1, ptr[2] >> 1, ptr[3] >> 1);
				break;
			} else
				return "invalid command";
	}


	return tmp;
}

void
Logl2Frame(struct IsdnCardState *sp, struct sk_buff *skb, char *buf, int dir)
{
	char tmp[132];
	u_char *ptr;

	ptr = skb->data;

	if (ptr[0] & 1 || !(ptr[1] & 1))
		debugl1(sp, "Addres not LAPD");
	else {
		sprintf(tmp, "%s %s: %s%c (sapi %d, tei %d)",
			(dir ? "<-" : "->"), buf, l2frames(ptr),
			((ptr[0] & 2) >> 1) == dir ? 'C' : 'R', ptr[0] >> 2, ptr[1] >> 1);
		debugl1(sp, tmp);
	}
}

#endif
