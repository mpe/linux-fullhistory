/* $Id: isdnl1.c,v 2.36 1999/08/25 16:50:57 keil Exp $

 * isdnl1.c     common low level stuff for Siemens Chipsetbased isdn cards
 *              based on the teles driver from Jan den Ouden
 *
 * Author       Karsten Keil (keil@isdn4linux.de)
 *
 *		This file is (c) under GNU PUBLIC LICENSE
 *		For changes and modifications please read
 *		../../../Documentation/isdn/HiSax.cert
 *
 * Thanks to    Jan den Ouden
 *              Fritz Elfert
 *              Beat Doebeli
 *
 *
 * $Log: isdnl1.c,v $
 * Revision 2.36  1999/08/25 16:50:57  keil
 * Fix bugs which cause 2.3.14 hangs (waitqueue init)
 *
 * Revision 2.35  1999/08/22 20:27:07  calle
 * backported changes from kernel 2.3.14:
 * - several #include "config.h" gone, others come.
 * - "struct device" changed to "struct net_device" in 2.3.14, added a
 *   define in isdn_compat.h for older kernel versions.
 *
 * Revision 2.34  1999/07/09 13:50:15  keil
 * remove unused variable
 *
 * Revision 2.33  1999/07/09 13:34:33  keil
 * remove debug code
 *
 * Revision 2.32  1999/07/01 08:11:47  keil
 * Common HiSax version for 2.0, 2.1, 2.2 and 2.3 kernel
 *
 * Revision 2.31  1998/11/15 23:54:56  keil
 * changes from 2.0
 *
 * Revision 2.30  1998/09/30 22:27:00  keil
 * Add init of l1.Flags
 *
 * Revision 2.29  1998/09/27 23:54:43  keil
 * cosmetics
 *
 * Revision 2.28  1998/09/27 12:52:23  keil
 * Fix against segfault, if the driver cannot allocate an IRQ channel
 *
 * Revision 2.27  1998/08/13 23:36:39  keil
 * HiSax 3.1 - don't work stable with current LinkLevel
 *
 * Revision 2.26  1998/07/15 15:01:31  calle
 * Support for AVM passive PCMCIA cards:
 *    A1 PCMCIA, FRITZ!Card PCMCIA and FRITZ!Card PCMCIA 2.0
 *
 * Revision 2.25  1998/05/25 14:10:09  keil
 * HiSax 3.0
 * X.75 and leased are working again.
 *
 * Revision 2.24  1998/05/25 12:58:04  keil
 * HiSax golden code from certification, Don't use !!!
 * No leased lines, no X75, but many changes.
 *
 * Revision 2.22  1998/04/15 16:40:13  keil
 * Add S0Box and Teles PCI support
 * Fix cardnr overwrite bug
 *
 * Revision 2.21  1998/04/10 10:35:28  paul
 * fixed (silly?) warnings from egcs on Alpha.
 *
 * Revision 2.20  1998/03/09 23:19:27  keil
 * Changes for PCMCIA
 *
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

const char *l1_revision = "$Revision: 2.36 $";

#define __NO_VERSION__
#include "hisax.h"
#include "isdnl1.h"

#define TIMER3_VALUE 7000

static
struct Fsm l1fsm_b =
{NULL, 0, 0, NULL, NULL};

static
struct Fsm l1fsm_d =
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

#define L1D_STATE_COUNT (ST_L1_F8+1)

static char *strL1DState[] =
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
	ST_L1_NULL,
	ST_L1_WAIT_ACT,
	ST_L1_WAIT_DEACT,
	ST_L1_ACTIV,
};

#define L1B_STATE_COUNT (ST_L1_ACTIV+1)

static char *strL1BState[] =
{
	"ST_L1_NULL",
	"ST_L1_WAIT_ACT",
	"ST_L1_WAIT_DEACT",
	"ST_L1_ACTIV",
};

enum {
	EV_PH_ACTIVATE,
	EV_PH_DEACTIVATE,
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
	"EV_PH_DEACTIVATE",
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

void
debugl1(struct IsdnCardState *cs, char *fmt, ...)
{
	va_list args;
	char tmp[8];
	
	va_start(args, fmt);
	sprintf(tmp, "Card%d ", cs->cardnr + 1);
	VHiSax_putstatus(cs, tmp, fmt, args);
	va_end(args);
}

static void
l1m_debug(struct FsmInst *fi, char *fmt, ...)
{
	va_list args;
	struct PStack *st = fi->userdata;
	struct IsdnCardState *cs = st->l1.hardware;
	char tmp[8];
	
	va_start(args, fmt);
	sprintf(tmp, "Card%d ", cs->cardnr + 1);
	VHiSax_putstatus(cs, tmp, fmt, args);
	va_end(args);
}

void
L1activated(struct IsdnCardState *cs)
{
	struct PStack *st;

	st = cs->stlist;
	while (st) {
		if (test_and_clear_bit(FLG_L1_ACTIVATING, &st->l1.Flags))
			st->l1.l1l2(st, PH_ACTIVATE | CONFIRM, NULL);
		else
			st->l1.l1l2(st, PH_ACTIVATE | INDICATION, NULL);
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
			st->l1.l1l2(st, PH_PAUSE | CONFIRM, NULL);
		st->l1.l1l2(st, PH_DEACTIVATE | INDICATION, NULL);
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
			stptr->l1.l1l2(stptr, PH_PULL | CONFIRM, NULL);
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

	if (stptr)
		if (test_bit(FLG_L1_ACTTIMER, &stptr->l1.Flags))
			FsmEvent(&stptr->l1.l1m, EV_TIMER_ACT, NULL);	
	while ((skb = skb_dequeue(&cs->rq))) {
#ifdef L2FRAME_DEBUG		/* psa */
		if (cs->debug & L1_DEB_LAPD)
			Logl2Frame(cs, skb, "PH_DATA", 1);
#endif
		stptr = cs->stlist;
		if (skb->len<3) {
			debugl1(cs, "D-channel frame too short(%d)",skb->len);
			idev_kfree_skb(skb, FREE_READ);
			return;
		}
		if ((skb->data[0] & 1) || !(skb->data[1] &1)) {
			debugl1(cs, "D-channel frame wrong EA0/EA1");
			idev_kfree_skb(skb, FREE_READ);
			return;
		}
		sapi = skb->data[0] >> 2;
		tei = skb->data[1] >> 1;
		if (cs->debug & DEB_DLOG_HEX)
			LogFrame(cs, skb->data, skb->len);
		if (cs->debug & DEB_DLOG_VERBOSE)
			dlogframe(cs, skb, 1);
		if (tei == GROUP_TEI) {
			if (sapi == CTRL_SAPI) { /* sapi 0 */
				while (stptr != NULL) {
					if ((nskb = skb_clone(skb, GFP_ATOMIC)))
						stptr->l1.l1l2(stptr, PH_DATA | INDICATION, nskb);
					else
						printk(KERN_WARNING "HiSax: isdn broadcast buffer shortage\n");
					stptr = stptr->next;
				}
			} else if (sapi == TEI_SAPI) {
				while (stptr != NULL) {
					if ((nskb = skb_clone(skb, GFP_ATOMIC)))
						stptr->l1.l1tei(stptr, PH_DATA | INDICATION, nskb);
					else
						printk(KERN_WARNING "HiSax: tei broadcast buffer shortage\n");
					stptr = stptr->next;
				}
			}
			idev_kfree_skb(skb, FREE_READ);
		} else if (sapi == CTRL_SAPI) { /* sapi 0 */
			found = 0;
			while (stptr != NULL)
				if (tei == stptr->l2.tei) {
					stptr->l1.l1l2(stptr, PH_DATA | INDICATION, skb);
					found = !0;
					break;
				} else
					stptr = stptr->next;
			if (!found)
				idev_kfree_skb(skb, FREE_READ);
		}
	}
}

static void
BChannel_proc_xmt(struct BCState *bcs)
{
	struct PStack *st = bcs->st;

	if (test_bit(BC_FLG_BUSY, &bcs->Flag)) {
		debugl1(bcs->cs, "BC_BUSY Error");
		return;
	}

	if (test_and_clear_bit(FLG_L1_PULL_REQ, &st->l1.Flags))
		st->l1.l1l2(st, PH_PULL | CONFIRM, NULL);
	if (!test_bit(BC_FLG_ACTIV, &bcs->Flag)) {
		if (!test_bit(BC_FLG_BUSY, &bcs->Flag) && (!skb_queue_len(&bcs->squeue))) {
			st->l2.l2l1(st, PH_DEACTIVATE | CONFIRM, NULL);
		}
	}
}

static void
BChannel_proc_rcv(struct BCState *bcs)
{
	struct sk_buff *skb;

	if (bcs->st->l1.l1m.state == ST_L1_WAIT_ACT) {
		FsmDelTimer(&bcs->st->l1.timer, 4);
		FsmEvent(&bcs->st->l1.l1m, EV_TIMER_ACT, NULL);
	}
	while ((skb = skb_dequeue(&bcs->rqueue))) {
		bcs->st->l1.l1l2(bcs->st, PH_DATA | INDICATION, skb);
	}
}

void
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

static char tmpdeb[32];

char *
l2frames(u_char * ptr)
{
	switch (ptr[2] & ~0x10) {
		case 1:
		case 5:
		case 9:
			sprintf(tmpdeb, "%s[%d](nr %d)", l2cmd(ptr[2]), ptr[3] & 1, ptr[3] >> 1);
			break;
		case 0x6f:
		case 0x0f:
		case 3:
		case 0x43:
		case 0x63:
		case 0x87:
		case 0xaf:
			sprintf(tmpdeb, "%s[%d]", l2cmd(ptr[2]), (ptr[2] & 0x10) >> 4);
			break;
		default:
			if (!(ptr[2] & 1)) {
				sprintf(tmpdeb, "I[%d](ns %d, nr %d)", ptr[3] & 1, ptr[2] >> 1, ptr[3] >> 1);
				break;
			} else
				return "invalid command";
	}


	return tmpdeb;
}

void
Logl2Frame(struct IsdnCardState *cs, struct sk_buff *skb, char *buf, int dir)
{
	u_char *ptr;

	ptr = skb->data;

	if (ptr[0] & 1 || !(ptr[1] & 1))
		debugl1(cs, "Address not LAPD");
	else
		debugl1(cs, "%s %s: %s%c (sapi %d, tei %d)",
			(dir ? "<-" : "->"), buf, l2frames(ptr),
			((ptr[0] & 2) >> 1) == dir ? 'C' : 'R', ptr[0] >> 2, ptr[1] >> 1);
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

	FsmChangeState(fi, ST_L1_F3);
	if (test_bit(FLG_L1_ACTIVATING, &st->l1.Flags))
		st->l1.l1hw(st, HW_ENABLE | REQUEST, NULL);
}

static void
l1_deact_req(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	FsmChangeState(fi, ST_L1_F3);
//	if (!test_bit(FLG_L1_T3RUN, &st->l1.Flags)) {
		FsmDelTimer(&st->l1.timer, 1);
		FsmAddTimer(&st->l1.timer, 550, EV_TIMER_DEACT, NULL, 2);
		test_and_set_bit(FLG_L1_DEACTTIMER, &st->l1.Flags);
//	}
}

static void
l1_power_up(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	if (test_bit(FLG_L1_ACTIVATING, &st->l1.Flags)) {
		FsmChangeState(fi, ST_L1_F4);
		st->l1.l1hw(st, HW_INFO3 | REQUEST, NULL);
		FsmDelTimer(&st->l1.timer, 1);
		FsmAddTimer(&st->l1.timer, TIMER3_VALUE, EV_TIMER3, NULL, 2);
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

	FsmChangeState(fi, ST_L1_F6);
	st->l1.l1hw(st, HW_INFO3 | REQUEST, NULL);
}

static void
l1_info4_ind(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	FsmChangeState(fi, ST_L1_F7);
	st->l1.l1hw(st, HW_INFO3 | REQUEST, NULL);
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

	test_and_clear_bit(FLG_L1_T3RUN, &st->l1.Flags);	
	if (test_and_clear_bit(FLG_L1_ACTIVATING, &st->l1.Flags))
		L1deactivated(st->l1.hardware);
	if (st->l1.l1m.state != ST_L1_F6) {
		FsmChangeState(fi, ST_L1_F3);
		st->l1.l1hw(st, HW_ENABLE | REQUEST, NULL);
	}
}

static void
l1_timer_act(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	
	test_and_clear_bit(FLG_L1_ACTTIMER, &st->l1.Flags);
	test_and_set_bit(FLG_L1_ACTIVATED, &st->l1.Flags);
	L1activated(st->l1.hardware);
}

static void
l1_timer_deact(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	
	test_and_clear_bit(FLG_L1_DEACTTIMER, &st->l1.Flags);
	test_and_clear_bit(FLG_L1_ACTIVATED, &st->l1.Flags);
	L1deactivated(st->l1.hardware);
	st->l1.l1hw(st, HW_DEACTIVATE | RESPONSE, NULL);
}

static void
l1_activate(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
                
	st->l1.l1hw(st, HW_RESET | REQUEST, NULL);
}

static void
l1_activate_no(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	if ((!test_bit(FLG_L1_DEACTTIMER, &st->l1.Flags)) && (!test_bit(FLG_L1_T3RUN, &st->l1.Flags))) {
		test_and_clear_bit(FLG_L1_ACTIVATING, &st->l1.Flags);
		L1deactivated(st->l1.hardware);
	}
}

static struct FsmNode L1DFnList[] HISAX_INITDATA =
{
	{ST_L1_F3, EV_PH_ACTIVATE, l1_activate},
	{ST_L1_F6, EV_PH_ACTIVATE, l1_activate_no},
	{ST_L1_F8, EV_PH_ACTIVATE, l1_activate_no},
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

#define L1D_FN_COUNT (sizeof(L1DFnList)/sizeof(struct FsmNode))

static void
l1b_activate(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	FsmChangeState(fi, ST_L1_WAIT_ACT);
	FsmAddTimer(&st->l1.timer, st->l1.delay, EV_TIMER_ACT, NULL, 2);
}

static void
l1b_deactivate(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	FsmChangeState(fi, ST_L1_WAIT_DEACT);
	FsmAddTimer(&st->l1.timer, 10, EV_TIMER_DEACT, NULL, 2);
}

static void
l1b_timer_act(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	FsmChangeState(fi, ST_L1_ACTIV);
	st->l1.l1l2(st, PH_ACTIVATE | CONFIRM, NULL);
}

static void
l1b_timer_deact(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	FsmChangeState(fi, ST_L1_NULL);
	st->l2.l2l1(st, PH_DEACTIVATE | CONFIRM, NULL);
}

static struct FsmNode L1BFnList[] HISAX_INITDATA =
{
	{ST_L1_NULL, EV_PH_ACTIVATE, l1b_activate},
	{ST_L1_WAIT_ACT, EV_TIMER_ACT, l1b_timer_act},
	{ST_L1_ACTIV, EV_PH_DEACTIVATE, l1b_deactivate},
	{ST_L1_WAIT_DEACT, EV_TIMER_DEACT, l1b_timer_deact},
};

#define L1B_FN_COUNT (sizeof(L1BFnList)/sizeof(struct FsmNode))

HISAX_INITFUNC(void Isdnl1New(void))
{
	l1fsm_d.state_count = L1D_STATE_COUNT;
	l1fsm_d.event_count = L1_EVENT_COUNT;
	l1fsm_d.strEvent = strL1Event;
	l1fsm_d.strState = strL1DState;
	FsmNew(&l1fsm_d, L1DFnList, L1D_FN_COUNT);
	l1fsm_b.state_count = L1B_STATE_COUNT;
	l1fsm_b.event_count = L1_EVENT_COUNT;
	l1fsm_b.strEvent = strL1Event;
	l1fsm_b.strState = strL1BState;
	FsmNew(&l1fsm_b, L1BFnList, L1B_FN_COUNT);
}

void Isdnl1Free(void)
{
	FsmFree(&l1fsm_d);
	FsmFree(&l1fsm_b);
}

static void
dch_l2l1(struct PStack *st, int pr, void *arg)
{
	struct IsdnCardState *cs = (struct IsdnCardState *) st->l1.hardware;

	switch (pr) {
		case (PH_DATA | REQUEST):
		case (PH_PULL | REQUEST):
		case (PH_PULL |INDICATION):
			st->l1.l1hw(st, pr, arg);
			break;
		case (PH_ACTIVATE | REQUEST):
			if (cs->debug)
				debugl1(cs, "PH_ACTIVATE_REQ %s",
					strL1DState[st->l1.l1m.state]);
			if (test_bit(FLG_L1_ACTIVATED, &st->l1.Flags))
				st->l1.l1l2(st, PH_ACTIVATE | CONFIRM, NULL);
			else {
				test_and_set_bit(FLG_L1_ACTIVATING, &st->l1.Flags);
				FsmEvent(&st->l1.l1m, EV_PH_ACTIVATE, arg);
			}
			break;
		case (PH_TESTLOOP | REQUEST):
			if (1 & (long) arg)
				debugl1(cs, "PH_TEST_LOOP B1");
			if (2 & (long) arg)
				debugl1(cs, "PH_TEST_LOOP B2");
			if (!(3 & (long) arg))
				debugl1(cs, "PH_TEST_LOOP DISABLED");
			st->l1.l1hw(st, HW_TESTLOOP | REQUEST, arg);
			break;
		default:
			if (cs->debug)
				debugl1(cs, "dch_l2l1 msg %04X unhandled", pr);
			break;
	}
}

void
l1_msg(struct IsdnCardState *cs, int pr, void *arg) {
	struct PStack *st;

	st = cs->stlist;
	
	while (st) {
		switch(pr) {
			case (HW_RESET | INDICATION):
				FsmEvent(&st->l1.l1m, EV_RESET_IND, arg);
				break;
			case (HW_DEACTIVATE | CONFIRM):
				FsmEvent(&st->l1.l1m, EV_DEACT_CNF, arg);
				break;
			case (HW_DEACTIVATE | INDICATION):
				FsmEvent(&st->l1.l1m, EV_DEACT_IND, arg);
				break;
			case (HW_POWERUP | CONFIRM):
				FsmEvent(&st->l1.l1m, EV_POWER_UP, arg);
				break;
			case (HW_RSYNC | INDICATION):
				FsmEvent(&st->l1.l1m, EV_RSYNC_IND, arg);
				break;
			case (HW_INFO2 | INDICATION):
				FsmEvent(&st->l1.l1m, EV_INFO2_IND, arg);
				break;
			case (HW_INFO4_P8 | INDICATION):
			case (HW_INFO4_P10 | INDICATION):
				FsmEvent(&st->l1.l1m, EV_INFO4_IND, arg);
				break;
			default:
				if (cs->debug)
					debugl1(cs, "l1msg %04X unhandled", pr);
				break;
		}
		st = st->next;
	}
}

void
l1_msg_b(struct PStack *st, int pr, void *arg) {
	switch(pr) {
		case (PH_ACTIVATE | REQUEST):
			FsmEvent(&st->l1.l1m, EV_PH_ACTIVATE, NULL);
			break;
		case (PH_DEACTIVATE | REQUEST):
			FsmEvent(&st->l1.l1m, EV_PH_DEACTIVATE, NULL);
			break;
	}
}

void
setstack_HiSax(struct PStack *st, struct IsdnCardState *cs)
{
	st->l1.hardware = cs;
	st->protocol = cs->protocol;
	st->l1.l1m.fsm = &l1fsm_d;
	st->l1.l1m.state = ST_L1_F3;
	st->l1.l1m.debug = cs->debug;
	st->l1.l1m.userdata = st;
	st->l1.l1m.userint = 0;
	st->l1.l1m.printdebug = l1m_debug;
	FsmInitTimer(&st->l1.l1m, &st->l1.timer);
	setstack_tei(st);
	setstack_manager(st);
	st->l1.stlistp = &(cs->stlist);
	st->l2.l2l1  = dch_l2l1;
	st->l1.Flags = 0;
	cs->setstack_d(st, cs);
}

void
setstack_l1_B(struct PStack *st)
{
	struct IsdnCardState *cs = st->l1.hardware;

	st->l1.l1m.fsm = &l1fsm_b;
	st->l1.l1m.state = ST_L1_NULL;
	st->l1.l1m.debug = cs->debug;
	st->l1.l1m.userdata = st;
	st->l1.l1m.userint = 0;
	st->l1.l1m.printdebug = l1m_debug;
	st->l1.Flags = 0;
	FsmInitTimer(&st->l1.l1m, &st->l1.timer);
}
