/* $Id: isdnl1.c,v 2.18 1998/02/12 23:07:42 keil Exp $

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
 * Revision 2.18  1998/02/12 23:07:42  keil
 * change for 2.1.86 (removing FREE_READ/FREE_WRITE from [dev]_kfree_skb()
 *
 * Revision 2.17  1998/02/11 17:28:07  keil
 * Niccy PnP/PCI support
 *
 * Revision 2.16  1998/02/09 18:46:08  keil
 * Support for Sedlbauer PCMCIA (Marcus Niemann)
 *
 * Revision 2.15  1998/02/09 10:54:51  keil
 * fixes for leased mode
 *
 * Revision 2.14  1998/02/03 23:31:31  keil
 * add AMD7930 support
 *
 * Revision 2.13  1998/02/02 13:33:02  keil
 * New card support
 *
 * Revision 2.12  1998/01/31 21:41:48  keil
 * changes for newer 2.1 kernels
 *
 * Revision 2.11  1997/11/12 15:01:23  keil
 * COMPAQ_ISA changes
 *
 * Revision 2.10  1997/11/08 21:35:48  keil
 * new l1 init
 *
 * Revision 2.9  1997/11/06 17:09:18  keil
 * New 2.1 init code
 *
 * Revision 2.8  1997/10/29 19:00:05  keil
 * new layer1,changes for 2.1
 *
 * Revision 2.7  1997/10/10 20:56:50  fritz
 * New HL interface.
 *
 * Revision 2.6  1997/09/12 10:05:16  keil
 * ISDN_CTRL_DEBUG define
 *
 * Revision 2.5  1997/09/11 17:24:45  keil
 * Add new cards
 *
 * Revision 2.4  1997/08/15 17:47:09  keil
 * avoid oops because a uninitialised timer
 *
 * Revision 2.3  1997/08/01 11:16:40  keil
 * cosmetics
 *
 * Revision 2.2  1997/07/30 17:11:08  keil
 * L1deactivated exported
 *
 * Revision 2.1  1997/07/27 21:35:38  keil
 * new layer1 interface
 *
 * Revision 2.0  1997/06/26 11:02:53  keil
 * New Layer and card interface
 *
 * Revision 1.15  1997/05/27 15:17:55  fritz
 * Added changes for recent 2.1.x kernels:
 *   changed return type of isdn_close
 *   queue_task_* -> queue_task
 *   clear/set_bit -> test_and_... where apropriate.
 *   changed type of hard_header_cache parameter.
 *
 * old changes removed KKe
 *
 */

const char *l1_revision = "$Revision: 2.18 $";

#define __NO_VERSION__
#include <linux/config.h>
#include "hisax.h"
#include "isdnl1.h"
#include <linux/kernel_stat.h>
#if (LINUX_VERSION_CODE < 0x020150) /* 2.1.80 */
#define kstat_irqs( PAR ) kstat.interrupts( (PAR) )
#endif



#if CARD_TELES0
extern int setup_teles0(struct IsdnCard *card);
#endif

#if CARD_TELES3
extern int setup_teles3(struct IsdnCard *card);
#endif

#if CARD_AVM_A1
extern int setup_avm_a1(struct IsdnCard *card);
#endif

#if CARD_ELSA
extern int setup_elsa(struct IsdnCard *card);
#endif

#if CARD_IX1MICROR2
extern int setup_ix1micro(struct IsdnCard *card);
#endif

#if CARD_DIEHLDIVA
extern	int  setup_diva(struct IsdnCard *card);
#endif

#if CARD_ASUSCOM
extern int setup_asuscom(struct IsdnCard *card);
#endif

#if CARD_TELEINT
extern int setup_TeleInt(struct IsdnCard *card);
#endif

#if CARD_SEDLBAUER
extern int setup_sedlbauer(struct IsdnCard *card);
#endif

#if CARD_SPORTSTER
extern int setup_sportster(struct IsdnCard *card);
#endif

#if CARD_MIC
extern int setup_mic(struct IsdnCard *card);
#endif

#if CARD_NETJET
extern int setup_netjet(struct IsdnCard *card);
#endif

#if CARD_TELES3C
extern int setup_t163c(struct IsdnCard *card);
#endif

#if CARD_AMD7930
extern int setup_amd7930(struct IsdnCard *card);
#endif

#if CARD_NICCY
extern int setup_niccy(struct IsdnCard *card);
#endif

#define HISAX_STATUS_BUFSIZE 4096
#define ISDN_CTRL_DEBUG 1
#define INCLUDE_INLINE_FUNCS
#include <linux/tqueue.h>
#include <linux/interrupt.h>
const char *CardType[] =
{"No Card", "Teles 16.0", "Teles 8.0", "Teles 16.3", "Creatix/Teles PnP",
 "AVM A1", "Elsa ML", "Elsa Quickstep", "Teles PCMCIA", "ITK ix1-micro Rev.2",
 "Elsa PCMCIA", "Eicon.Diehl Diva", "ISDNLink", "TeleInt", "Teles 16.3c", 
 "Sedlbauer Speed Card", "USR Sportster", "ith mic Linux", "Elsa PCI",
 "Compaq ISA", "NETjet", "Teles PCI", "Sedlbauer Speed Star (PCMCIA)",
 "AMD 7930", "NICCY"
};

extern struct IsdnCard cards[];
extern int nrcards;
extern char *HiSax_id;
extern struct IsdnBuffers *tracebuf;

#define TIMER3_VALUE 7

static
struct Fsm l1fsm =
{NULL, 0, 0, NULL, NULL};

enum {
	ST_L1_F2,
	ST_L1_F3,
	ST_L1_F4,
	ST_L1_F5,
	ST_L1_F6,
	ST_L1_F7,
	ST_L1_F8,
};

#define L1_STATE_COUNT (ST_L1_F8+1)

static char *strL1State[] =
{
	"ST_L1_F2",
	"ST_L1_F3",
	"ST_L1_F4",
	"ST_L1_F5",
	"ST_L1_F6",
	"ST_L1_F7",
	"ST_L1_F8",
};

enum {
	EV_PH_ACTIVATE,
	EV_RESET_IND,
	EV_DEACT_CNF,
	EV_DEACT_IND,
	EV_POWER_UP,
	EV_RSYNC_IND, 
	EV_INFO2_IND,
	EV_INFO4_IND,
	EV_TIMER_DEACT,
	EV_TIMER_ACT,
	EV_TIMER3,
};

#define L1_EVENT_COUNT (EV_TIMER3 + 1)

static char *strL1Event[] =
{
	"EV_PH_ACTIVATE",
	"EV_RESET_IND",
	"EV_DEACT_CNF",
	"EV_DEACT_IND",
	"EV_POWER_UP",
	"EV_RSYNC_IND", 
	"EV_INFO2_IND",
	"EV_INFO4_IND",
	"EV_TIMER_DEACT",
	"EV_TIMER_ACT",
	"EV_TIMER3",
};

/*
 * Find card with given driverId
 */
static inline struct IsdnCardState
*hisax_findcard(int driverid)
{
	int i;

	for (i = 0; i < nrcards; i++)
		if (cards[i].cs)
			if (cards[i].cs->myid == driverid)
				return (cards[i].cs);
	return (NULL);
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

#if ISDN_CTRL_DEBUG
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
#else
#define KDEBUG_DEF
#include "../kdebug.h"

static int DbgLineNr=0,DbgSequenzNr=1;

void
HiSax_putstatus(struct IsdnCardState *csta, char *buf)
{
	char tmp[512];
	
	if (DbgLineNr==23)
		DbgLineNr=0;
	sprintf(tmp, "%5d %s",DbgSequenzNr++,buf);
	gput_str(tmp,0,DbgLineNr++);
}	
#endif

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
debugl1(struct IsdnCardState *cs, char *msg)
{
	char tmp[256], tm[32];

	jiftime(tm, jiffies);
	sprintf(tmp, "%s Card %d %s\n", tm, cs->cardnr + 1, msg);
	HiSax_putstatus(cs, tmp);
}

static void
l1m_debug(struct FsmInst *fi, char *s)
{
	struct PStack *st = fi->userdata;
	
	debugl1(st->l1.hardware, s);
}

void
L1activated(struct IsdnCardState *cs)
{
	struct PStack *st;

	st = cs->stlist;
	while (st) {
		if (test_and_clear_bit(FLG_L1_ACTIVATING, &st->l1.Flags))
			st->l1.l1man(st, PH_ACTIVATE_CNF, NULL);
		else
			st->l1.l1man(st, PH_ACTIVATE_IND, NULL);
		st = st->next;
	}
}

void
L1deactivated(struct IsdnCardState *cs)
{
	struct PStack *st;

	st = cs->stlist;
	while (st) {
		if (test_bit(FLG_L1_DBUSY, &cs->HW_Flags))
			st->l1.l1l2(st, PH_PAUSE_CNF, NULL);
		st->l1.l1man(st, PH_DEACTIVATE_IND, NULL);
		st = st->next;
	}
	test_and_clear_bit(FLG_L1_DBUSY, &cs->HW_Flags);
}

void
DChannel_proc_xmt(struct IsdnCardState *cs)
{
	struct PStack *stptr;

	if (cs->tx_skb)
		return;

	stptr = cs->stlist;
	while (stptr != NULL)
		if (test_and_clear_bit(FLG_L1_PULL_REQ, &stptr->l1.Flags)) {
			stptr->l1.l1l2(stptr, PH_PULL_CNF, NULL);
			break;
		} else
			stptr = stptr->next;
}

void
DChannel_proc_rcv(struct IsdnCardState *cs)
{
	struct sk_buff *skb, *nskb;
	struct PStack *stptr = cs->stlist;
	int found, tei, sapi;
	char tmp[64];

	if (stptr)
		if (test_bit(FLG_L1_ACTTIMER, &stptr->l1.Flags))
			FsmEvent(&stptr->l1.l1m, EV_TIMER_ACT, NULL);	
	while ((skb = skb_dequeue(&cs->rq))) {
#ifdef L2FRAME_DEBUG		/* psa */
		if (cs->debug & L1_DEB_LAPD)
			Logl2Frame(cs, skb, "PH_DATA", 1);
#endif
		stptr = cs->stlist;
		sapi = skb->data[0] >> 2;
		tei = skb->data[1] >> 1;

		if (tei == GROUP_TEI) {
			if (sapi == CTRL_SAPI) {	/* sapi 0 */
				if (cs->dlogflag) {
					LogFrame(cs, skb->data, skb->len);
					dlogframe(cs, skb->data + 3, skb->len - 3,
						  "Q.931 frame network->user broadcast");
				}
				while (stptr != NULL) {
					if ((nskb = skb_clone(skb, GFP_ATOMIC)))
						stptr->l1.l1l2(stptr, PH_DATA_IND, nskb);
					else
						printk(KERN_WARNING "HiSax: isdn broadcast buffer shortage\n");
					stptr = stptr->next;
				}
			} else if (sapi == TEI_SAPI) {
				while (stptr != NULL) {
					if ((nskb = skb_clone(skb, GFP_ATOMIC)))
						stptr->l1.l1tei(stptr, PH_DATA_IND, nskb);
					else
						printk(KERN_WARNING "HiSax: tei broadcast buffer shortage\n");
					stptr = stptr->next;
				}
			}
			dev_kfree_skb(skb);
		} else if (sapi == CTRL_SAPI) {
			found = 0;
			while (stptr != NULL)
				if (tei == stptr->l2.tei) {
					stptr->l1.l1l2(stptr, PH_DATA_IND, skb);
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
					LogFrame(cs, skb->data, skb->len);
					dlogframe(cs, skb->data + 4, skb->len - 4, tmp);
				}
				dev_kfree_skb(skb);
			}
		}
	}
}

static void
BChannel_proc_xmt(struct BCState *bcs)
{
	struct PStack *st = bcs->st;

	if (test_bit(BC_FLG_BUSY, &bcs->Flag))
		return;

	if (test_and_clear_bit(FLG_L1_PULL_REQ, &st->l1.Flags))
		st->l1.l1l2(st, PH_PULL_CNF, NULL);
	if (!test_bit(BC_FLG_ACTIV, &bcs->Flag))
		if (!test_bit(BC_FLG_BUSY, &bcs->Flag) && (!skb_queue_len(&bcs->squeue)))
			st->ma.manl1(st, PH_DEACTIVATE_CNF, 0);
}

static void
BChannel_proc_rcv(struct BCState *bcs)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&bcs->rqueue))) {
		bcs->st->l1.l1l2(bcs->st, PH_DATA_IND, skb);
	}
}

static void
BChannel_bh(struct BCState *bcs)
{
	if (!bcs)
		return;
	if (test_and_clear_bit(B_RCVBUFREADY, &bcs->event))
		BChannel_proc_rcv(bcs);
	if (test_and_clear_bit(B_XMTBUFREADY, &bcs->event))
		BChannel_proc_xmt(bcs);
}

void
HiSax_addlist(struct IsdnCardState *cs,
	      struct PStack *st)
{
	st->next = cs->stlist;
	cs->stlist = st;
}

void
HiSax_rmlist(struct IsdnCardState *cs,
	     struct PStack *st)
{
	struct PStack *p;

	FsmDelTimer(&st->l1.timer, 0);
	if (cs->stlist == st)
		cs->stlist = st->next;
	else {
		p = cs->stlist;
		while (p)
			if (p->next == st) {
				p->next = st->next;
				return;
			} else
				p = p->next;
	}
}

void
init_bcstate(struct IsdnCardState *cs,
	     int bc)
{
	struct BCState *bcs = cs->bcs + bc;

	bcs->cs = cs;
	bcs->channel = bc;
	bcs->tqueue.next = 0;
	bcs->tqueue.sync = 0;
	bcs->tqueue.routine = (void *) (void *) BChannel_bh;
	bcs->tqueue.data = bcs;
	bcs->BC_SetStack = NULL;
	bcs->BC_Close = NULL;
	bcs->Flag = 0;
}

static void
closecard(int cardnr)
{
	struct IsdnCardState *csta = cards[cardnr].cs;
	struct sk_buff *skb;
	
	if (csta->bcs->BC_Close != NULL) { 
		csta->bcs->BC_Close(csta->bcs + 1);
		csta->bcs->BC_Close(csta->bcs);
	}

	if (csta->rcvbuf) {
		kfree(csta->rcvbuf);
		csta->rcvbuf = NULL;
	}
	while ((skb = skb_dequeue(&csta->rq))) {
		dev_kfree_skb(skb);
	}
	while ((skb = skb_dequeue(&csta->sq))) {
		dev_kfree_skb(skb);
	}
	if (csta->tx_skb) {
		dev_kfree_skb(csta->tx_skb);
		csta->tx_skb = NULL;
	}
	if (csta->mon_rx) {
		kfree(csta->mon_rx);
		csta->mon_rx = NULL;
	}
	if (csta->mon_tx) {
		kfree(csta->mon_tx);
		csta->mon_tx = NULL;
	}
	csta->cardmsg(csta, CARD_RELEASE, NULL);
	del_timer(&csta->dbusytimer);
	ll_unload(csta);
}

HISAX_INITFUNC(static int init_card(struct IsdnCardState *cs))
{
	int irq_cnt, cnt = 3;
	long flags;

	save_flags(flags);
	cli();
	irq_cnt = kstat_irqs(cs->irq);
	printk(KERN_INFO "%s: IRQ %d count %d\n", CardType[cs->typ], cs->irq,
		irq_cnt);
	if (cs->cardmsg(cs, CARD_SETIRQ, NULL)) {
		printk(KERN_WARNING "HiSax: couldn't get interrupt %d\n",
			cs->irq);
		return(1);
	}
	while (cnt) {
		cs->cardmsg(cs, CARD_INIT, NULL);
		sti();
		current->state = TASK_INTERRUPTIBLE;
		/* Timeout 10ms */
		schedule_timeout((10 * HZ) / 1000);
		restore_flags(flags);
		printk(KERN_INFO "%s: IRQ %d count %d\n", CardType[cs->typ],
			cs->irq, kstat_irqs(cs->irq));
		if (kstat_irqs(cs->irq) == irq_cnt) {
			printk(KERN_WARNING
			       "%s: IRQ(%d) getting no interrupts during init %d\n",
			       CardType[cs->typ], cs->irq, 4 - cnt);
			if (cnt == 1) {
				free_irq(cs->irq, cs);
				return (2);
			} else {
				cs->cardmsg(cs, CARD_RESET, NULL);
				cnt--;
			}
		} else {
			cs->cardmsg(cs, CARD_TEST, NULL);
			return(0);
		}
	}
	restore_flags(flags);
	return(3);
}

HISAX_INITFUNC(static int
checkcard(int cardnr, char *id, int *busy_flag))
{
	long flags;
	int ret = 0;
	struct IsdnCard *card = cards + cardnr;
	struct IsdnCardState *cs;

	save_flags(flags);
	cli();
	if (!(cs = (struct IsdnCardState *)
	      kmalloc(sizeof(struct IsdnCardState), GFP_ATOMIC))) {
		printk(KERN_WARNING
		       "HiSax: No memory for IsdnCardState(card %d)\n",
		       cardnr + 1);
		restore_flags(flags);
		return (0);
	}
	card->cs = cs;
	cs->cardnr = cardnr;
	cs->debug = L1_DEB_WARN;
	cs->HW_Flags = 0;
	cs->busy_flag = busy_flag;
#if TEI_PER_CARD
#else
	test_and_set_bit(FLG_TWO_DCHAN, &cs->HW_Flags);
#endif
	cs->protocol = card->protocol;

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
	if (!(cs->dlogspace = kmalloc(4096, GFP_ATOMIC))) {
		printk(KERN_WARNING
		       "HiSax: No memory for dlogspace(card %d)\n",
		       cardnr + 1);
		restore_flags(flags);
		return (0);
	}
	if (!(cs->status_buf = kmalloc(HISAX_STATUS_BUFSIZE, GFP_ATOMIC))) {
		printk(KERN_WARNING
		       "HiSax: No memory for status_buf(card %d)\n",
		       cardnr + 1);
		kfree(cs->dlogspace);
		restore_flags(flags);
		return (0);
	}
	cs->stlist = NULL;
	cs->dlogflag = 0;
	cs->mon_tx = NULL;
	cs->mon_rx = NULL;
	cs->status_read = cs->status_buf;
	cs->status_write = cs->status_buf;
	cs->status_end = cs->status_buf + HISAX_STATUS_BUFSIZE - 1;
	cs->typ = card->typ;
	strcpy(cs->iif.id, id);
	cs->iif.channels = 2;
	cs->iif.maxbufsize = MAX_DATA_SIZE;
	cs->iif.hl_hdrlen = MAX_HEADER_LEN;
	cs->iif.features =
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

	cs->iif.command = HiSax_command;
	cs->iif.writecmd = NULL;
	cs->iif.writebuf_skb = HiSax_writebuf_skb;
	cs->iif.readstat = HiSax_readstatus;
	register_isdn(&cs->iif);
	cs->myid = cs->iif.channels;
	printk(KERN_INFO
	       "HiSax: Card %d Protocol %s Id=%s (%d)\n", cardnr + 1,
	       (card->protocol == ISDN_PTYPE_1TR6) ? "1TR6" :
	       (card->protocol == ISDN_PTYPE_EURO) ? "EDSS1" :
	       (card->protocol == ISDN_PTYPE_LEASED) ? "LEASED" :
	       (card->protocol == ISDN_PTYPE_NI1) ? "NI1" :
	       "NONE", cs->iif.id, cs->myid);
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
		case ISDN_CTYPE_COMPAQ_ISA:
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
		case ISDN_CTYPE_ELSA_PNP:
		case ISDN_CTYPE_ELSA_PCMCIA:
		case ISDN_CTYPE_ELSA_PCI:
			ret = setup_elsa(card);
			break;
#endif
#if CARD_IX1MICROR2
		case ISDN_CTYPE_IX1MICROR2:
			ret = setup_ix1micro(card);
			break;
#endif
#if CARD_DIEHLDIVA
		case ISDN_CTYPE_DIEHLDIVA:
			ret = setup_diva(card);
			break;
#endif
#if CARD_ASUSCOM
		case ISDN_CTYPE_ASUSCOM:
			ret = setup_asuscom(card);
			break;
#endif
#if CARD_TELEINT
		case ISDN_CTYPE_TELEINT:
			ret = setup_TeleInt(card);
			break;
#endif
#if CARD_SEDLBAUER
		case ISDN_CTYPE_SEDLBAUER:
		case ISDN_CTYPE_SEDLBAUER_PCMCIA:
			ret = setup_sedlbauer(card);
			break;
#endif
#if CARD_SPORTSTER
		case ISDN_CTYPE_SPORTSTER:
			ret = setup_sportster(card);
			break;
#endif
#if CARD_MIC
		case ISDN_CTYPE_MIC:
			ret = setup_mic(card);
			break;
#endif
#if CARD_NETJET
		case ISDN_CTYPE_NETJET:
			ret = setup_netjet(card);
			break;
#endif
#if CARD_TELES3C
		case ISDN_CTYPE_TELES3C:
			ret = setup_t163c(card);
			break;
#endif
#if CARD_NICCY
		case ISDN_CTYPE_NICCY:
			ret = setup_niccy(card);
			break;
#endif
#if CARD_AMD7930
		case ISDN_CTYPE_AMD7930:
			ret = setup_amd7930(card);
			break;
#endif
		default:
			printk(KERN_WARNING "HiSax: Unknown Card Typ %d\n",
			       card->typ);
			ll_unload(cs);
			restore_flags(flags);
			return (0);
	}
	if (!ret) {
		ll_unload(cs);
		restore_flags(flags);
		return (0);
	}
	if (!(cs->rcvbuf = kmalloc(MAX_DFRAME_LEN, GFP_ATOMIC))) {
		printk(KERN_WARNING
		       "HiSax: No memory for isac rcvbuf\n");
		return (1);
	}
	cs->rcvidx = 0;
	cs->tx_skb = NULL;
	cs->tx_cnt = 0;
	cs->event = 0;
	cs->tqueue.next = 0;
	cs->tqueue.sync = 0;
	cs->tqueue.data = cs;

	skb_queue_head_init(&cs->rq);
	skb_queue_head_init(&cs->sq);

	init_bcstate(cs, 0);
	init_bcstate(cs, 1);
	ret = init_card(cs);
	if (ret) {
		closecard(cardnr);
		restore_flags(flags);
		return (0);
	}
	init_tei(cs, cs->protocol);
	CallcNewChan(cs);
	ll_run(cs);
	cs->l1cmd(cs, PH_RESET_REQ, NULL);
	restore_flags(flags);
	return (1);
}

HISAX_INITFUNC(void
HiSax_shiftcards(int idx))
{
	int i;

	for (i = idx; i < 15; i++)
		memcpy(&cards[i], &cards[i + 1], sizeof(cards[i]));
}

HISAX_INITFUNC(int
HiSax_inithardware(int *busy_flag))
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
		if (checkcard(i, ids, busy_flag)) {
			foundcards++;
			i++;
		} else {
			printk(KERN_WARNING "HiSax: Card %s not installed !\n",
			       CardType[cards[i].typ]);
			if (cards[i].cs)
				kfree((void *) cards[i].cs);
			cards[i].cs = NULL;
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
		if (cards[i].cs) {
			ll_stop(cards[i].cs);
			release_tei(cards[i].cs);
			closecard(i);
			free_irq(cards[i].cs->irq, cards[i].cs);
			kfree((void *) cards[i].cs);
			cards[i].cs = NULL;
		}
	Isdnl1Free();
	TeiFree();
	Isdnl2Free();
	CallcFree();
	restore_flags(flags);
}

void
HiSax_reportcard(int cardnr)
{
	struct IsdnCardState *cs = cards[cardnr].cs;
	struct PStack *stptr;
	struct l3_process *pc;
	int j, i = 1;

	printk(KERN_DEBUG "HiSax: reportcard No %d\n", cardnr + 1);
	printk(KERN_DEBUG "HiSax: Type %s\n", CardType[cs->typ]);
	printk(KERN_DEBUG "HiSax: debuglevel %x\n", cs->debug);
	printk(KERN_DEBUG "HiSax: HiSax_reportcard address 0x%lX\n",
	       (ulong) & HiSax_reportcard);
	printk(KERN_DEBUG "HiSax: cs 0x%lX\n", (ulong) cs);
	printk(KERN_DEBUG "HiSax: cs stl 0x%lX\n", (ulong) & (cs->stlist));
	stptr = cs->stlist;
	while (stptr != NULL) {
		printk(KERN_DEBUG "HiSax: dst%d 0x%lX\n", i, (ulong) stptr);
		printk(KERN_DEBUG "HiSax: dst%d stp 0x%lX\n", i, (ulong) stptr->l1.stlistp);
		printk(KERN_DEBUG "HiSax:   tei %d sapi %d\n",
		       stptr->l2.tei, stptr->l2.sap);
		printk(KERN_DEBUG "HiSax:      man 0x%lX\n", (ulong) stptr->ma.layer);
		pc = stptr->l3.proc;
		while (pc) {
			printk(KERN_DEBUG "HiSax: l3proc %x 0x%lX\n", pc->callref,
			       (ulong) pc);
			printk(KERN_DEBUG "HiSax:    state %d  st 0x%lX chan 0x%lX\n",
			    pc->state, (ulong) pc->st, (ulong) pc->chan);
			pc = pc->next;
		}
		stptr = stptr->next;
		i++;
	}
	for (j = 0; j < 2; j++) {
		printk(KERN_DEBUG "HiSax: ch%d 0x%lX\n", j,
		       (ulong) & cs->channel[j]);
		stptr = cs->channel[j].b_st;
		i = 1;
		while (stptr != NULL) {
			printk(KERN_DEBUG "HiSax:  b_st%d 0x%lX\n", i, (ulong) stptr);
			printk(KERN_DEBUG "HiSax:    man 0x%lX\n", (ulong) stptr->ma.layer);
			stptr = stptr->next;
			i++;
		}
	}
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
Logl2Frame(struct IsdnCardState *cs, struct sk_buff *skb, char *buf, int dir)
{
	char tmp[132];
	u_char *ptr;

	ptr = skb->data;

	if (ptr[0] & 1 || !(ptr[1] & 1))
		debugl1(cs, "Addres not LAPD");
	else {
		sprintf(tmp, "%s %s: %s%c (sapi %d, tei %d)",
			(dir ? "<-" : "->"), buf, l2frames(ptr),
			((ptr[0] & 2) >> 1) == dir ? 'C' : 'R', ptr[0] >> 2, ptr[1] >> 1);
		debugl1(cs, tmp);
	}
}
#endif

static void
l1_reset(struct FsmInst *fi, int event, void *arg)
{
	FsmChangeState(fi, ST_L1_F3);
}

static void
l1_deact_cnf(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct IsdnCardState *cs = st->l1.hardware;

	FsmChangeState(fi, ST_L1_F3);
	if (test_bit(FLG_L1_ACTIVATING, &st->l1.Flags))
		cs->l1cmd(cs, PH_ENABLE_REQ, NULL);
}

static void
l1_deact_req(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	FsmChangeState(fi, ST_L1_F3);
	if (!test_bit(FLG_L1_T3RUN, &st->l1.Flags)) {
		FsmDelTimer(&st->l1.timer, 1);
		FsmAddTimer(&st->l1.timer, 550, EV_TIMER_DEACT, NULL, 2);
		test_and_set_bit(FLG_L1_DEACTTIMER, &st->l1.Flags);
	}
}

static void
l1_power_up(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct IsdnCardState *cs = st->l1.hardware;

	if (test_bit(FLG_L1_ACTIVATING, &st->l1.Flags)) {
		FsmChangeState(fi, ST_L1_F4);
		cs->l1cmd(cs, PH_INFO3_REQ, NULL);
		FsmDelTimer(&st->l1.timer, 1);
		FsmAddTimer(&st->l1.timer, TIMER3_VALUE * HZ, EV_TIMER3, NULL, 2);
		test_and_set_bit(FLG_L1_T3RUN, &st->l1.Flags);
	} else
		FsmChangeState(fi, ST_L1_F3);
}

static void
l1_go_F5(struct FsmInst *fi, int event, void *arg)
{
	FsmChangeState(fi, ST_L1_F5);
}

static void
l1_go_F8(struct FsmInst *fi, int event, void *arg)
{
	FsmChangeState(fi, ST_L1_F8);
}

static void
l1_info2_ind(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct IsdnCardState *cs = st->l1.hardware;

	FsmChangeState(fi, ST_L1_F6);
	cs->l1cmd(cs, PH_INFO3_REQ, NULL);
}

static void
l1_info4_ind(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct IsdnCardState *cs = st->l1.hardware;

	FsmChangeState(fi, ST_L1_F7);
	cs->l1cmd(cs, PH_INFO3_REQ, NULL);
	if (test_and_clear_bit(FLG_L1_DEACTTIMER, &st->l1.Flags))
		FsmDelTimer(&st->l1.timer, 4);
	if (!test_bit(FLG_L1_ACTIVATED, &st->l1.Flags)) {
		if (test_and_clear_bit(FLG_L1_T3RUN, &st->l1.Flags))
			FsmDelTimer(&st->l1.timer, 3);
		FsmAddTimer(&st->l1.timer, 110, EV_TIMER_ACT, NULL, 2);
		test_and_set_bit(FLG_L1_ACTTIMER, &st->l1.Flags);
	}
}

static void
l1_timer3(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct IsdnCardState *cs = st->l1.hardware;
	
	test_and_clear_bit(FLG_L1_T3RUN, &st->l1.Flags);	
        if (test_and_clear_bit(FLG_L1_ACTIVATING, &st->l1.Flags))
        	L1deactivated(cs);
        if (st->l1.l1m.state != ST_L1_F6)
        	FsmChangeState(fi, ST_L1_F3);
}

static void
l1_timer_act(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct IsdnCardState *cs = st->l1.hardware;
	
	test_and_clear_bit(FLG_L1_ACTTIMER, &st->l1.Flags);
	test_and_set_bit(FLG_L1_ACTIVATED, &st->l1.Flags);
	L1activated(cs);
}

static void
l1_timer_deact(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct IsdnCardState *cs = st->l1.hardware;
	
	test_and_clear_bit(FLG_L1_DEACTTIMER, &st->l1.Flags);
	test_and_clear_bit(FLG_L1_ACTIVATED, &st->l1.Flags);
	L1deactivated(cs);
	cs->l1cmd(cs, PH_DEACT_ACK, NULL);
}

static void
l1_activate(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct IsdnCardState *cs = st->l1.hardware;
                
	cs->l1cmd(cs, PH_RESET_REQ, NULL);
}

static struct FsmNode L1FnList[] HISAX_INITDATA =
{
	{ST_L1_F3, EV_PH_ACTIVATE, l1_activate},
	{ST_L1_F3, EV_RESET_IND, l1_reset},
	{ST_L1_F4, EV_RESET_IND, l1_reset},
	{ST_L1_F5, EV_RESET_IND, l1_reset},
	{ST_L1_F6, EV_RESET_IND, l1_reset},
	{ST_L1_F7, EV_RESET_IND, l1_reset},
	{ST_L1_F8, EV_RESET_IND, l1_reset},
	{ST_L1_F3, EV_DEACT_CNF, l1_deact_cnf},
	{ST_L1_F4, EV_DEACT_CNF, l1_deact_cnf},
	{ST_L1_F5, EV_DEACT_CNF, l1_deact_cnf},
	{ST_L1_F6, EV_DEACT_CNF, l1_deact_cnf},
	{ST_L1_F7, EV_DEACT_CNF, l1_deact_cnf},
	{ST_L1_F8, EV_DEACT_CNF, l1_deact_cnf},
	{ST_L1_F6, EV_DEACT_IND, l1_deact_req},
	{ST_L1_F7, EV_DEACT_IND, l1_deact_req},
	{ST_L1_F8, EV_DEACT_IND, l1_deact_req},
	{ST_L1_F3, EV_POWER_UP, l1_power_up},
	{ST_L1_F4, EV_RSYNC_IND, l1_go_F5},
	{ST_L1_F6, EV_RSYNC_IND, l1_go_F8},
	{ST_L1_F7, EV_RSYNC_IND, l1_go_F8},
	{ST_L1_F3, EV_INFO2_IND, l1_info2_ind},
	{ST_L1_F4, EV_INFO2_IND, l1_info2_ind},
	{ST_L1_F5, EV_INFO2_IND, l1_info2_ind},
	{ST_L1_F7, EV_INFO2_IND, l1_info2_ind},
	{ST_L1_F8, EV_INFO2_IND, l1_info2_ind},
	{ST_L1_F3, EV_INFO4_IND, l1_info4_ind},
	{ST_L1_F4, EV_INFO4_IND, l1_info4_ind},
	{ST_L1_F5, EV_INFO4_IND, l1_info4_ind},
	{ST_L1_F6, EV_INFO4_IND, l1_info4_ind},
	{ST_L1_F8, EV_INFO4_IND, l1_info4_ind},
	{ST_L1_F3, EV_TIMER3, l1_timer3},
	{ST_L1_F4, EV_TIMER3, l1_timer3},
	{ST_L1_F5, EV_TIMER3, l1_timer3},
	{ST_L1_F6, EV_TIMER3, l1_timer3},
	{ST_L1_F8, EV_TIMER3, l1_timer3},
	{ST_L1_F7, EV_TIMER_ACT, l1_timer_act},
	{ST_L1_F3, EV_TIMER_DEACT, l1_timer_deact},
	{ST_L1_F4, EV_TIMER_DEACT, l1_timer_deact},
	{ST_L1_F5, EV_TIMER_DEACT, l1_timer_deact},
	{ST_L1_F6, EV_TIMER_DEACT, l1_timer_deact},
	{ST_L1_F7, EV_TIMER_DEACT, l1_timer_deact},
	{ST_L1_F8, EV_TIMER_DEACT, l1_timer_deact},
};

#define L1_FN_COUNT (sizeof(L1FnList)/sizeof(struct FsmNode))

HISAX_INITFUNC(void Isdnl1New(void))
{
	l1fsm.state_count = L1_STATE_COUNT;
	l1fsm.event_count = L1_EVENT_COUNT;
	l1fsm.strEvent = strL1Event;
	l1fsm.strState = strL1State;
	FsmNew(&l1fsm, L1FnList, L1_FN_COUNT);
}

void Isdnl1Free(void)
{
	FsmFree(&l1fsm);
}

static void
dch_manl1(struct PStack *st, int pr,
	   void *arg)
{
	struct IsdnCardState *cs = (struct IsdnCardState *) st->l1.hardware;
	char tmp[32];

	switch (pr) {
		case PH_ACTIVATE_REQ:
			if (cs->debug) {
				sprintf(tmp, "PH_ACTIVATE_REQ %s",
					strL1State[st->l1.l1m.state]);
				debugl1(cs, tmp);
			}
			if (test_bit(FLG_L1_ACTIVATED, &st->l1.Flags))
				st->l1.l1man(st, PH_ACTIVATE_CNF, NULL);
			else {
				test_and_set_bit(FLG_L1_ACTIVATING, &st->l1.Flags);
				FsmEvent(&st->l1.l1m, EV_PH_ACTIVATE, arg);
			}
			break;
		case PH_DEACTIVATE_REQ:
			if (cs->debug) {
				sprintf(tmp, "PH_DEACTIVATE_REQ %s",
					strL1State[st->l1.l1m.state]);
				debugl1(cs, tmp);
			}
			break;
		case PH_TESTLOOP_REQ:
			if (1 & (int) arg)
				debugl1(cs, "PH_TEST_LOOP B1");
			if (2 & (int) arg)
				debugl1(cs, "PH_TEST_LOOP B2");
			if (!(3 & (int) arg))
				debugl1(cs, "PH_TEST_LOOP DISABLED");
			cs->l1cmd(cs, PH_TESTLOOP_REQ, arg);
			break;
		case PH_RESET_IND:
			FsmEvent(&st->l1.l1m, EV_RESET_IND, arg);
			break;
		case PH_DEACT_CNF:
			FsmEvent(&st->l1.l1m, EV_DEACT_CNF, arg);
			break;
		case PH_DEACT_IND:
			FsmEvent(&st->l1.l1m, EV_DEACT_IND, arg);
			break;
		case PH_POWERUP_CNF:
			FsmEvent(&st->l1.l1m, EV_POWER_UP, arg);
			break;
		case PH_RSYNC_IND:
			FsmEvent(&st->l1.l1m, EV_RSYNC_IND, arg);
			break;
		case PH_INFO2_IND:
			FsmEvent(&st->l1.l1m, EV_INFO2_IND, arg);
			break;
		case PH_I4_P8_IND:
		case PH_I4_P10_IND:
			FsmEvent(&st->l1.l1m, EV_INFO4_IND, arg);
			break;
		default:
			if (cs->debug) {
				sprintf(tmp, "dch_manl1 msg %04X unhandled", pr);
				debugl1(cs, tmp);
			}
			break;
	}
}

void
setstack_HiSax(struct PStack *st, struct IsdnCardState *cs)
{
	st->l1.hardware = cs;
	st->protocol = cs->protocol;
	st->l1.l1m.fsm = &l1fsm;
	st->l1.l1m.state = ST_L1_F3;
	st->l1.l1m.debug = cs->debug;
	st->l1.l1m.userdata = st;
	st->l1.l1m.userint = 0;
	st->l1.l1m.printdebug = l1m_debug;
	FsmInitTimer(&st->l1.l1m, &st->l1.timer);
	setstack_tei(st);
	setstack_manager(st);
	st->l1.stlistp = &(cs->stlist);
	st->ma.manl1 = dch_manl1;
	st->l1.Flags = 0;
	cs->setstack_d(st, cs);
}
