/* $Id: isdnl1.c,v 1.8 1997/02/09 00:24:31 keil Exp $

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

const char *l1_revision = "$Revision: 1.8 $";

#define __NO_VERSION__
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

#define HISAX_STATUS_BUFSIZE 4096

#define INCLUDE_INLINE_FUNCS
#include <linux/tqueue.h>
#include <linux/interrupt.h>

const char *CardType[] =
{"No Card", "Teles 16.0", "Teles 8.0", "Teles 16.3",
 "Creatix PnP", "AVM A1", "Elsa ML", "Elsa Quickstep",
 "Teles PCMCIA", "ITK ix1-micro Rev.2"};

static char *HSCXVer[] =
{"A1", "?1", "A2", "?3", "A3", "V2.1", "?6", "?7",
 "?8", "?9", "?10", "?11", "?12", "?13", "?14", "???"};

static char *ISACVer[] =
{"2086/2186 V1.1", "2085 B1", "2085 B2",
 "2085 V2.3"};

extern void tei_handler(struct PStack *st, byte pr,
			struct BufHeader *ibh);
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
HiSax_readstatus(byte * buf, int len, int user, int id, int channel)
{
	int count;
	byte *p;
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
	byte *p;
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
		Sfree(csta->status_buf);
	csta->status_read = NULL;
	csta->status_write = NULL;
	csta->status_end = NULL;
	Sfree(csta->dlogspace);
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
HscxVersion(byte v)
{
	return (HSCXVer[v & 0xf]);
}

void
hscx_sched_event(struct HscxState *hsp, int event)
{
	hsp->event |= 1 << event;
	queue_task_irq_off(&hsp->tqueue, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
}

/*
 * ISAC stuff goes here
 */

char *
ISACVersion(byte v)
{
	return (ISACVer[(v >> 5) & 3]);
}

void
isac_sched_event(struct IsdnCardState *sp, int event)
{
	sp->event |= 1 << event;
	queue_task_irq_off(&sp->tqueue, &tq_immediate);
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
			if (!sp->xmtibh)
				if (!BufQueueUnlink(&sp->xmtibh, &sp->sq))
					sp->sendptr = 0;
			if (sp->xmtibh)
				sp->isac_fill_fifo(sp);
			break;
		case (13):
			sp->ph_command(sp, 9);
			sp->ph_active = 5;
			isac_sched_event(sp, ISAC_PHCHANGE);
			if (!sp->xmtibh)
				if (!BufQueueUnlink(&sp->xmtibh, &sp->sq))
					sp->sendptr = 0;
			if (sp->xmtibh)
				sp->isac_fill_fifo(sp);
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

	if (sp->xmtibh)
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
	struct BufHeader *ibh, *cibh;
	struct PStack *stptr;
	byte *ptr;
	int found, broadc;
	char tmp[64];

	while (!BufQueueUnlink(&ibh, &sp->rq)) {
#ifdef L2FRAME_DEBUG		/* psa */
		if (sp->debug & L1_DEB_LAPD)
			Logl2Frame(sp, ibh, "PH_DATA", 1);
#endif
		stptr = sp->stlist;
		ptr = DATAPTR(ibh);
		broadc = (ptr[1] >> 1) == 127;

		if (broadc) {
			if (!(ptr[0] >> 2)) {	/* sapi 0 */
				sp->CallFlags = 3;
				if (sp->dlogflag) {
					LogFrame(sp, ptr, ibh->datasize);
					dlogframe(sp, ptr + 3, ibh->datasize - 3,
						  "Q.931 frame network->user broadcast");
				}
			}
			while (stptr != NULL) {
				if ((ptr[0] >> 2) == stptr->l2.sap)
					if (!BufPoolGet(&cibh, &sp->rbufpool, GFP_ATOMIC,
							(void *) 1, 5)) {
						memcpy(DATAPTR(cibh), DATAPTR(ibh), ibh->datasize);
						cibh->datasize = ibh->datasize;
						stptr->l1.l1l2(stptr, PH_DATA, cibh);
					} else
						printk(KERN_WARNING "HiSax: isdn broadcast buffer shortage\n");
				stptr = stptr->next;
			}
			BufPoolRelease(ibh);
		} else {
			found = 0;
			while (stptr != NULL)
				if (((ptr[0] >> 2) == stptr->l2.sap) &&
				    ((ptr[1] >> 1) == stptr->l2.tei)) {
					stptr->l1.l1l2(stptr, PH_DATA, ibh);
					found = !0;
					break;
				} else
					stptr = stptr->next;
			if (!found) {
				/* BD 10.10.95
				 * Print out D-Channel msg not processed
				 * by isdn4linux
				 */

				if ((!(ptr[0] >> 2)) && (!(ptr[2] & 0x01))) {
					sprintf(tmp,
						"Q.931 frame network->user with tei %d (not for us)",
						ptr[1] >> 1);
					LogFrame(sp, ptr, ibh->datasize);
					dlogframe(sp, ptr + 4, ibh->datasize - 4, tmp);
				}
				BufPoolRelease(ibh);
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
l2l1(struct PStack *st, int pr,
     struct BufHeader *ibh)
{
	struct IsdnCardState *sp = (struct IsdnCardState *) st->l1.hardware;
	byte *ptr = DATAPTR(ibh);
	char str[64];

	switch (pr) {
		case (PH_DATA):
			if (sp->xmtibh) {
				BufQueueLink(&sp->sq, ibh);
#ifdef L2FRAME_DEBUG		/* psa */
				if (sp->debug & L1_DEB_LAPD)
					Logl2Frame(sp, ibh, "PH_DATA Queued", 0);
#endif
			} else {
				if ((sp->dlogflag) && (!(ptr[2] & 1))) {	/* I-FRAME */
					LogFrame(sp, ptr, ibh->datasize);
					sprintf(str, "Q.931 frame user->network tei %d", st->l2.tei);
					dlogframe(sp, ptr + st->l2.ihsize, ibh->datasize - st->l2.ihsize,
						  str);
				}
				sp->xmtibh = ibh;
				sp->sendptr = 0;
				sp->releasebuf = !0;
				sp->isac_fill_fifo(sp);
#ifdef L2FRAME_DEBUG		/* psa */
				if (sp->debug & L1_DEB_LAPD)
					Logl2Frame(sp, ibh, "PH_DATA", 0);
#endif
			}
			break;
		case (PH_DATA_PULLED):
			if (sp->xmtibh) {
				if (sp->debug & L1_DEB_WARN)
					debugl1(sp, " l2l1 xmtibh exist this shouldn't happen");
				break;
			}
			if ((sp->dlogflag) && (!(ptr[2] & 1))) {	/* I-FRAME */
				LogFrame(sp, ptr, ibh->datasize);
				sprintf(str, "Q.931 frame user->network tei %d", st->l2.tei);
				dlogframe(sp, ptr + st->l2.ihsize, ibh->datasize - st->l2.ihsize,
					  str);
			}
			sp->xmtibh = ibh;
			sp->sendptr = 0;
			sp->releasebuf = 0;
			sp->isac_fill_fifo(sp);
#ifdef L2FRAME_DEBUG		/* psa */
			if (sp->debug & L1_DEB_LAPD)
				Logl2Frame(sp, ibh, "PH_DATA_PULLED", 0);
#endif
			break;
		case (PH_REQUEST_PULL):
#ifdef L2FRAME_DEBUG		/* psa */
			if (sp->debug & L1_DEB_LAPD)
				debugl1(sp, "-> PH_REQUEST_PULL");
#endif
			if (!sp->xmtibh) {
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

	if (hsp->xmtibh)
		return;

	if (st->l1.requestpull) {
		st->l1.requestpull = 0;
		st->l1.l1l2(st, PH_PULL_ACK, NULL);
	}
	if (!hsp->active)
		if ((!hsp->xmtibh) && (!hsp->sq.head))
			hsp->sp->modehscx(hsp, 0, 0);
}

static void
hscx_process_rcv(struct HscxState *hsp)
{
	struct BufHeader *ibh;

#ifdef DEBUG_MAGIC
	if (hsp->magic != 301270) {
		printk(KERN_DEBUG "hscx_process_rcv magic not 301270\n");
		return;
	}
#endif
	while (!BufQueueUnlink(&ibh, &hsp->rq)) {
		hsp->st->l1.l1l2(hsp->st, PH_DATA, ibh);
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

#ifdef DEBUG_MAGIC
	if (sp->magic != 301271) {
		printk(KERN_DEBUG "isac_discardq magic not 301271\n");
		return;
	}
#endif

	BufQueueDiscard(&sp->sq, pr, heldby, releasetoo);
}

void
setstack_HiSax(struct PStack *st, struct IsdnCardState *sp)
{
	st->l1.hardware = sp;
	st->l1.sbufpool = &(sp->sbufpool);
	st->l1.rbufpool = &(sp->rbufpool);
	st->l1.smallpool = &(sp->smallpool);
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
			SA_INTERRUPT, "HiSax", NULL)) {
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
	hs->sp->modehscx(hs, 0, 0);
	hs->inuse = 0;

	if (hs->init) {
		BufPoolFree(&hs->smallpool);
		BufPoolFree(&hs->rbufpool);
		BufPoolFree(&hs->sbufpool);
	}
	hs->init = 0;
}

static void
closecard(int cardnr)
{
	struct IsdnCardState *csta = cards[cardnr].sp;

	BufPoolFree(&csta->smallpool);
	BufPoolFree(&csta->rbufpool);
	BufPoolFree(&csta->sbufpool);

	close_hscxstate(csta->hs + 1);
	close_hscxstate(csta->hs);

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
	      Smalloc(sizeof(struct IsdnCardState), GFP_KERNEL,
		      "struct IsdnCardState"))) {
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
	if (!(sp->dlogspace = Smalloc(4096, GFP_KERNEL, "dlogspace"))) {
		printk(KERN_WARNING
		       "HiSax: No memory for dlogspace(card %d)\n",
		       cardnr + 1);
		restore_flags(flags);
		return (0);
	}
	if (!(sp->status_buf = Smalloc(HISAX_STATUS_BUFSIZE, GFP_KERNEL, "status_buf"))) {
		printk(KERN_WARNING
		       "HiSax: No memory for status_buf(card %d)\n",
		       cardnr + 1);
		Sfree(sp->dlogspace);
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
	sp->iif.maxbufsize = BUFFER_SIZE(HSCX_SBUF_ORDER, HSCX_SBUF_BPPS);
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
	    0;

	sp->iif.command = HiSax_command;
	sp->iif.writebuf = HiSax_writebuf;
	sp->iif.writecmd = NULL;
	sp->iif.writebuf_skb = NULL;
	sp->iif.readstat = HiSax_readstatus;
	register_isdn(&sp->iif);
	sp->myid = sp->iif.channels;
	restore_flags(flags);
	printk(KERN_NOTICE
	       "HiSax: Card %d Protocol %s Id=%s (%d)\n", cardnr + 1,
	       (card->protocol == ISDN_PTYPE_1TR6) ? "1TR6" :
	       (card->protocol == ISDN_PTYPE_EURO) ? "EDSS1" :
	       (card->protocol == ISDN_PTYPE_LEASED) ? "LEASED" :
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
	BufPoolInit(&sp->sbufpool, ISAC_SBUF_ORDER, ISAC_SBUF_BPPS,
		    ISAC_SBUF_MAXPAGES);
	BufPoolInit(&sp->rbufpool, ISAC_RBUF_ORDER, ISAC_RBUF_BPPS,
		    ISAC_RBUF_MAXPAGES);
	BufPoolInit(&sp->smallpool, ISAC_SMALLBUF_ORDER, ISAC_SMALLBUF_BPPS,
		    ISAC_SMALLBUF_MAXPAGES);
	sp->rcvibh = NULL;
	sp->rcvptr = 0;
	sp->xmtibh = NULL;
	sp->sendptr = 0;
	sp->mon_rx = NULL;
	sp->mon_rxp = 0;
	sp->mon_tx = NULL;
	sp->mon_txp = 0;
	sp->mon_flg = 0;
	sp->event = 0;
	sp->tqueue.next = 0;
	sp->tqueue.sync = 0;
	sp->tqueue.routine = (void *) (void *) isac_bh;
	sp->tqueue.data = sp;

	BufQueueInit(&sp->rq);
	BufQueueInit(&sp->sq);

	sp->stlist = NULL;
	sp->ph_active = 0;
	sp->dlogflag = 0;
	sp->debug = L1_DEB_WARN;
	sp->releasebuf = 0;
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
				Sfree((void *) cards[i].sp);
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
			Sfree((void *) cards[i].sp);
			cards[i].sp = NULL;
		}
	Isdnl2Free();
	CallcFree();
	restore_flags(flags);
}

static void
hscx_l2l1(struct PStack *st, int pr,
	  struct BufHeader *ibh)
{
	struct IsdnCardState *sp = (struct IsdnCardState *)
	st->l1.hardware;
	struct HscxState *hsp = sp->hs + st->l1.hscx;
	long flags;

	switch (pr) {
		case (PH_DATA):
			save_flags(flags);
			cli();
			if (hsp->xmtibh) {
				BufQueueLink(&hsp->sq, ibh);
				restore_flags(flags);
			} else {
				restore_flags(flags);
				hsp->xmtibh = ibh;
				hsp->sendptr = 0;
				hsp->releasebuf = !0;
				sp->hscx_fill_fifo(hsp);
			}
			break;
		case (PH_DATA_PULLED):
			if (hsp->xmtibh) {
				printk(KERN_WARNING "hscx_l2l1: this shouldn't happen\n");
				break;
			}
			hsp->xmtibh = ibh;
			hsp->sendptr = 0;
			hsp->releasebuf = 0;
			sp->hscx_fill_fifo(hsp);
			break;
		case (PH_REQUEST_PULL):
			if (!hsp->xmtibh) {
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

#ifdef DEBUG_MAGIC
	if (hsp->magic != 301270) {
		printk(KERN_DEBUG "hscx_discardq magic not 301270\n");
		return;
	}
#endif

	BufQueueDiscard(&hsp->sq, pr, heldby, releasetoo);
}

static int
open_hscxstate(struct IsdnCardState *sp,
	       int hscx)
{
	struct HscxState *hsp = sp->hs + hscx;

	if (!hsp->init) {
		BufPoolInit(&hsp->sbufpool, HSCX_SBUF_ORDER, HSCX_SBUF_BPPS,
			    HSCX_SBUF_MAXPAGES);
		BufPoolInit(&hsp->rbufpool, HSCX_RBUF_ORDER, HSCX_RBUF_BPPS,
			    HSCX_RBUF_MAXPAGES);
		BufPoolInit(&hsp->smallpool, HSCX_SMALLBUF_ORDER, HSCX_SMALLBUF_BPPS,
			    HSCX_SMALLBUF_MAXPAGES);
	}
	hsp->init = !0;

	BufQueueInit(&hsp->rq);
	BufQueueInit(&hsp->sq);

	hsp->releasebuf = 0;
	hsp->rcvibh = NULL;
	hsp->xmtibh = NULL;
	hsp->rcvptr = 0;
	hsp->sendptr = 0;
	hsp->event = 0;
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
			if (!hsp->xmtibh)
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

	st->l1.sbufpool = &hs->sbufpool;
	st->l1.rbufpool = &hs->rbufpool;
	st->l1.smallpool = &hs->smallpool;
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
l2cmd(byte cmd)
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
l2frames(byte * ptr)
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
Logl2Frame(struct IsdnCardState *sp, struct BufHeader *ibh, char *buf, int dir)
{
	char tmp[132];
	byte *ptr;

	ptr = DATAPTR(ibh);

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
