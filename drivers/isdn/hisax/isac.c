/* $Id: isac.c,v 1.12 1998/02/12 23:07:40 keil Exp $

 * isac.c   ISAC specific routines
 *
 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *
 *
 * $Log: isac.c,v $
 * Revision 1.12  1998/02/12 23:07:40  keil
 * change for 2.1.86 (removing FREE_READ/FREE_WRITE from [dev]_kfree_skb()
 *
 * Revision 1.11  1998/02/09 10:54:49  keil
 * fixes for leased mode
 *
 * Revision 1.10  1998/02/02 13:37:37  keil
 * new init
 *
 * Revision 1.9  1997/11/06 17:09:07  keil
 * New 2.1 init code
 *
 * Revision 1.8  1997/10/29 19:00:03  keil
 * new layer1,changes for 2.1
 *
 * Revision 1.7  1997/10/01 09:21:37  fritz
 * Removed old compatibility stuff for 2.0.X kernels.
 * From now on, this code is for 2.1.X ONLY!
 * Old stuff is still in the separate branch.
 *
 * Revision 1.6  1997/08/15 17:47:08  keil
 * avoid oops because a uninitialised timer
 *
 * Revision 1.5  1997/08/07 17:48:49  keil
 * fix wrong parenthesis
 *
 * Revision 1.4  1997/07/30 17:11:59  keil
 * fixed Timer3
 *
 * Revision 1.3  1997/07/27 21:37:40  keil
 * T3 implemented; supervisor l1timer; B-channel TEST_LOOP
 *
 * Revision 1.2  1997/06/26 11:16:15  keil
 * first version
 *
 *
 */

#define __NO_VERSION__
#include "hisax.h"
#include "isac.h"
#include "isdnl1.h"
#include <linux/interrupt.h>

#define DBUSY_TIMER_VALUE 80
#define ARCOFI_USE 1

static char *ISACVer[] HISAX_INITDATA =
{"2086/2186 V1.1", "2085 B1", "2085 B2",
 "2085 V2.3"};

void
ISACVersion(struct IsdnCardState *cs, char *s)
{
	int val;

	val = cs->readisac(cs, ISAC_RBCH);
	printk(KERN_INFO "%s ISAC version : %s\n", s, ISACVer[(val >> 5) & 3]);
}

static void
ph_command(struct IsdnCardState *cs, unsigned int command)
{
	if (cs->debug & L1_DEB_ISAC) {
		char tmp[32];
		sprintf(tmp, "ph_command %x", command);
		debugl1(cs, tmp);
	}
	cs->writeisac(cs, ISAC_CIX0, (command << 2) | 3);
}

static void
manl1_msg(struct IsdnCardState *cs, int msg, void *arg) {
	struct PStack *st;

	st = cs->stlist;
	while (st) {
		st->ma.manl1(st, msg, arg);
		st = st->next;
	}
}

static void
isac_new_ph(struct IsdnCardState *cs)
{
	switch (cs->ph_state) {
		case (ISAC_IND_RS):
		case (ISAC_IND_EI):
			ph_command(cs, ISAC_CMD_DUI);
			manl1_msg(cs, PH_RESET_IND, NULL);
			break;
		case (ISAC_IND_DID):
			manl1_msg(cs, PH_DEACT_CNF, NULL);
			break;
		case (ISAC_IND_DR):
			manl1_msg(cs, PH_DEACT_IND, NULL);
			break;
		case (ISAC_IND_PU):
			manl1_msg(cs, PH_POWERUP_CNF, NULL);
			break;
		case (ISAC_IND_RSY):
			manl1_msg(cs, PH_RSYNC_IND, NULL);
			break;
		case (ISAC_IND_ARD):
			manl1_msg(cs, PH_INFO2_IND, NULL);
			break;
		case (ISAC_IND_AI8):
			manl1_msg(cs, PH_I4_P8_IND, NULL);
			break;
		case (ISAC_IND_AI10):
			manl1_msg(cs, PH_I4_P10_IND, NULL);
			break;
		default:
			break;
	}
}

static void
isac_bh(struct IsdnCardState *cs)
{
	struct PStack *stptr;
	
	if (!cs)
		return;

	if (test_and_clear_bit(D_CLEARBUSY, &cs->event)) {
		if (cs->debug)
			debugl1(cs, "D-Channel Busy cleared");
		stptr = cs->stlist;
		while (stptr != NULL) {
			stptr->l1.l1l2(stptr, PH_PAUSE_CNF, NULL);
			stptr = stptr->next;
		}
	}
	if (test_and_clear_bit(D_L1STATECHANGE, &cs->event))
		isac_new_ph(cs);		
	if (test_and_clear_bit(D_RCVBUFREADY, &cs->event))
		DChannel_proc_rcv(cs);
	if (test_and_clear_bit(D_XMTBUFREADY, &cs->event))
		DChannel_proc_xmt(cs);
	if (test_and_clear_bit(D_RX_MON0, &cs->event))
		test_and_set_bit(HW_MON0_TX_END, &cs->HW_Flags);
	if (test_and_clear_bit(D_RX_MON1, &cs->event))
		test_and_set_bit(HW_MON1_TX_END, &cs->HW_Flags);
	if (test_and_clear_bit(D_TX_MON0, &cs->event))
		test_and_set_bit(HW_MON0_RX_END, &cs->HW_Flags);
	if (test_and_clear_bit(D_TX_MON1, &cs->event))
		test_and_set_bit(HW_MON1_RX_END, &cs->HW_Flags);
}

void
isac_empty_fifo(struct IsdnCardState *cs, int count)
{
	u_char *ptr;
	long flags;

	if ((cs->debug & L1_DEB_ISAC) && !(cs->debug & L1_DEB_ISAC_FIFO))
		debugl1(cs, "isac_empty_fifo");

	if ((cs->rcvidx + count) >= MAX_DFRAME_LEN) {
		if (cs->debug & L1_DEB_WARN) {
			char tmp[40];
			sprintf(tmp, "isac_empty_fifo overrun %d",
				cs->rcvidx + count);
			debugl1(cs, tmp);
		}
		cs->writeisac(cs, ISAC_CMDR, 0x80);
		cs->rcvidx = 0;
		return;
	}
	ptr = cs->rcvbuf + cs->rcvidx;
	cs->rcvidx += count;
	save_flags(flags);
	cli();
	cs->readisacfifo(cs, ptr, count);
	cs->writeisac(cs, ISAC_CMDR, 0x80);
	restore_flags(flags);
	if (cs->debug & L1_DEB_ISAC_FIFO) {
		char tmp[128];
		char *t = tmp;

		t += sprintf(t, "isac_empty_fifo cnt %d", count);
		QuickHex(t, ptr, count);
		debugl1(cs, tmp);
	}
}

static void
isac_fill_fifo(struct IsdnCardState *cs)
{
	int count, more;
	u_char *ptr;
	long flags;

	if ((cs->debug & L1_DEB_ISAC) && !(cs->debug & L1_DEB_ISAC_FIFO))
		debugl1(cs, "isac_fill_fifo");

	if (!cs->tx_skb)
		return;

	count = cs->tx_skb->len;
	if (count <= 0)
		return;

	more = 0;
	if (count > 32) {
		more = !0;
		count = 32;
	}
	save_flags(flags);
	cli();
	ptr = cs->tx_skb->data;
	skb_pull(cs->tx_skb, count);
	cs->tx_cnt += count;
	cs->writeisacfifo(cs, ptr, count);
	cs->writeisac(cs, ISAC_CMDR, more ? 0x8 : 0xa);
	restore_flags(flags);
	if (test_and_set_bit(FLG_DBUSY_TIMER, &cs->HW_Flags)) {
		debugl1(cs, "isac_fill_fifo dbusytimer running");
		del_timer(&cs->dbusytimer);
	}
	init_timer(&cs->dbusytimer);
	cs->dbusytimer.expires = jiffies + ((DBUSY_TIMER_VALUE * HZ)/1000);
	add_timer(&cs->dbusytimer);
	if (cs->debug & L1_DEB_ISAC_FIFO) {
		char tmp[128];
		char *t = tmp;

		t += sprintf(t, "isac_fill_fifo cnt %d", count);
		QuickHex(t, ptr, count);
		debugl1(cs, tmp);
	}
}

void
isac_sched_event(struct IsdnCardState *cs, int event)
{
	test_and_set_bit(event, &cs->event);
	queue_task(&cs->tqueue, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
}

void
isac_interrupt(struct IsdnCardState *cs, u_char val)
{
	u_char exval, v1;
	struct sk_buff *skb;
	unsigned int count;
	long flags;
	char tmp[32];

	if (cs->debug & L1_DEB_ISAC) {
		sprintf(tmp, "ISAC interrupt %x", val);
		debugl1(cs, tmp);
	}
	if (val & 0x80) {	/* RME */
		exval = cs->readisac(cs, ISAC_RSTA);
		if ((exval & 0x70) != 0x20) {
			if (exval & 0x40)
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, "ISAC RDO");
			if (!(exval & 0x20))
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, "ISAC CRC error");
			cs->writeisac(cs, ISAC_CMDR, 0x80);
		} else {
			count = cs->readisac(cs, ISAC_RBCL) & 0x1f;
			if (count == 0)
				count = 32;
			isac_empty_fifo(cs, count);
			save_flags(flags);
			cli();
			if ((count = cs->rcvidx) > 0) {
				cs->rcvidx = 0;
				if (!(skb = alloc_skb(count, GFP_ATOMIC)))
					printk(KERN_WARNING "HiSax: D receive out of memory\n");
				else {
					memcpy(skb_put(skb, count), cs->rcvbuf, count);
					skb_queue_tail(&cs->rq, skb);
				}
			}
			restore_flags(flags);
		}
		cs->rcvidx = 0;
		isac_sched_event(cs, D_RCVBUFREADY);
	}
	if (val & 0x40) {	/* RPF */
		isac_empty_fifo(cs, 32);
	}
	if (val & 0x20) {	/* RSC */
		/* never */
		if (cs->debug & L1_DEB_WARN)
			debugl1(cs, "ISAC RSC interrupt");
	}
	if (val & 0x10) {	/* XPR */
		if (test_and_clear_bit(FLG_DBUSY_TIMER, &cs->HW_Flags))
			del_timer(&cs->dbusytimer);
		if (test_and_clear_bit(FLG_L1_DBUSY, &cs->HW_Flags))
			isac_sched_event(cs, D_CLEARBUSY);
		if (cs->tx_skb) {
			if (cs->tx_skb->len) {
				isac_fill_fifo(cs);
				goto afterXPR;
			} else {
				dev_kfree_skb(cs->tx_skb);
				cs->tx_cnt = 0;
				cs->tx_skb = NULL;
			}
		}
		if ((cs->tx_skb = skb_dequeue(&cs->sq))) {
			cs->tx_cnt = 0;
			isac_fill_fifo(cs);
		} else
			isac_sched_event(cs, D_XMTBUFREADY);
	}
      afterXPR:
	if (val & 0x04) {	/* CISQ */
		cs->ph_state = (cs->readisac(cs, ISAC_CIX0) >> 2) & 0xf;
		if (cs->debug & L1_DEB_ISAC) {
			sprintf(tmp, "ph_state change %x", cs->ph_state);
			debugl1(cs, tmp);
		}
		isac_sched_event(cs, D_L1STATECHANGE);
	}
	if (val & 0x02) {	/* SIN */
		/* never */
		if (cs->debug & L1_DEB_WARN)
			debugl1(cs, "ISAC SIN interrupt");
	}
	if (val & 0x01) {	/* EXI */
		exval = cs->readisac(cs, ISAC_EXIR);
		if (cs->debug & L1_DEB_WARN) {
			sprintf(tmp, "ISAC EXIR %02x", exval);
			debugl1(cs, tmp);
		}
		if (exval & 0x04) {
			v1 = cs->readisac(cs, ISAC_MOSR);
			if (cs->debug & L1_DEB_WARN) {
				sprintf(tmp, "ISAC MOSR %02x", v1);
				debugl1(cs, tmp);
			}
#if ARCOFI_USE
			if (v1 & 0x08) {
				if (!cs->mon_rx) {
					if (!(cs->mon_rx = kmalloc(MAX_MON_FRAME, GFP_ATOMIC))) {
						if (cs->debug & L1_DEB_WARN)
							debugl1(cs, "ISAC MON RX out of memory!");
						cs->mocr &= 0xf0;
						cs->mocr |= 0x0a;
						cs->writeisac(cs, ISAC_MOCR, cs->mocr);
						goto afterMONR0;
					} else
						cs->mon_rxp = 0;
				}
				if (cs->mon_rxp >= MAX_MON_FRAME) {
					cs->mocr &= 0xf0;
					cs->mocr |= 0x0a;
					cs->writeisac(cs, ISAC_MOCR, cs->mocr);
					cs->mon_rxp = 0;
					if (cs->debug & L1_DEB_WARN)
						debugl1(cs, "ISAC MON RX overflow!");
					goto afterMONR0;
				}
				cs->mon_rx[cs->mon_rxp++] = cs->readisac(cs, ISAC_MOR0);
				if (cs->debug & L1_DEB_WARN) {
					sprintf(tmp, "ISAC MOR0 %02x", cs->mon_rx[cs->mon_rxp -1]);
					debugl1(cs, tmp);
				}
				if (cs->mon_rxp == 1) {
					cs->mocr |= 0x04;
					cs->writeisac(cs, ISAC_MOCR, cs->mocr);
				}
			}
		      afterMONR0:
			if (v1 & 0x80) {
				if (!cs->mon_rx) {
					if (!(cs->mon_rx = kmalloc(MAX_MON_FRAME, GFP_ATOMIC))) {
						if (cs->debug & L1_DEB_WARN)
							debugl1(cs, "ISAC MON RX out of memory!");
						cs->mocr &= 0x0f;
						cs->mocr |= 0xa0;
						cs->writeisac(cs, ISAC_MOCR, cs->mocr);
						goto afterMONR1;
					} else
						cs->mon_rxp = 0;
				}
				if (cs->mon_rxp >= MAX_MON_FRAME) {
					cs->mocr &= 0x0f;
					cs->mocr |= 0xa0;
					cs->writeisac(cs, ISAC_MOCR, cs->mocr);
					cs->mon_rxp = 0;
					if (cs->debug & L1_DEB_WARN)
						debugl1(cs, "ISAC MON RX overflow!");
					goto afterMONR1;
				}
				cs->mon_rx[cs->mon_rxp++] = cs->readisac(cs, ISAC_MOR1);
				if (cs->debug & L1_DEB_WARN) {
					sprintf(tmp, "ISAC MOR1 %02x", cs->mon_rx[cs->mon_rxp -1]);
					debugl1(cs, tmp);
				}
				if (cs->mon_rxp == 1) {
					cs->mocr |= 0x40;
					cs->writeisac(cs, ISAC_MOCR, cs->mocr);
				}
			}
		      afterMONR1:
			if (v1 & 0x04) {
				cs->mocr &= 0xf0;
				cs->mocr |= 0x0a;
				cs->writeisac(cs, ISAC_MOCR, cs->mocr);
				isac_sched_event(cs, D_RX_MON0);
			}
			if (v1 & 0x40) {
				cs->mocr &= 0x0f;
				cs->mocr |= 0xa0;
				cs->writeisac(cs, ISAC_MOCR, cs->mocr);
				isac_sched_event(cs, D_RX_MON1);
			}
			if (v1 & 0x02) {
				if (!cs->mon_tx) {
					cs->mocr &= 0xf0;
					cs->mocr |= 0x0a;
					cs->writeisac(cs, ISAC_MOCR, cs->mocr);
					goto AfterMOX0;
				}
				if (cs->mon_txp >= cs->mon_txc) {
					if (cs->mon_txc)
						isac_sched_event(cs, D_TX_MON0);
					goto AfterMOX0;
				}
				cs->writeisac(cs, ISAC_MOX0,
					cs->mon_tx[cs->mon_txp++]);
				if (cs->debug & L1_DEB_WARN) {
					sprintf(tmp, "ISAC %02x -> MOX0", cs->mon_tx[cs->mon_txp -1]);
					debugl1(cs, tmp);
				}
			}
		      AfterMOX0:
			if (v1 & 0x20) {
				if (!cs->mon_tx) {
					cs->mocr &= 0x0f;
					cs->mocr |= 0xa0;
					cs->writeisac(cs, ISAC_MOCR, cs->mocr);
					goto AfterMOX1;
				}
				if (cs->mon_txp >= cs->mon_txc) {
					if (cs->mon_txc)
						isac_sched_event(cs, D_TX_MON1);
					goto AfterMOX1;
				}
				cs->writeisac(cs, ISAC_MOX1,
					cs->mon_tx[cs->mon_txp++]);
				if (cs->debug & L1_DEB_WARN) {
					sprintf(tmp, "ISAC %02x -> MOX1", cs->mon_tx[cs->mon_txp -1]);
					debugl1(cs, tmp);
				}
			}
		      AfterMOX1:
#endif
		}
	}
}

static void
ISAC_l2l1(struct PStack *st, int pr, void *arg)
{
	struct IsdnCardState *cs = (struct IsdnCardState *) st->l1.hardware;
	struct sk_buff *skb = arg;
	char str[64];

	switch (pr) {
		case (PH_DATA_REQ):
			if (cs->tx_skb) {
				skb_queue_tail(&cs->sq, skb);
#ifdef L2FRAME_DEBUG		/* psa */
				if (cs->debug & L1_DEB_LAPD)
					Logl2Frame(cs, skb, "PH_DATA Queued", 0);
#endif
			} else {
				if ((cs->dlogflag) && (!(skb->data[2] & 1))) {	/* I-FRAME */
					LogFrame(cs, skb->data, skb->len);
					sprintf(str, "Q.931 frame user->network tei %d", st->l2.tei);
					dlogframe(cs, skb->data + 4, skb->len - 4,
						  str);
				}
				cs->tx_skb = skb;
				cs->tx_cnt = 0;
#ifdef L2FRAME_DEBUG		/* psa */
				if (cs->debug & L1_DEB_LAPD)
					Logl2Frame(cs, skb, "PH_DATA", 0);
#endif
				isac_fill_fifo(cs);
			}
			break;
		case (PH_PULL_IND):
			if (cs->tx_skb) {
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, " l2l1 tx_skb exist this shouldn't happen");
				skb_queue_tail(&cs->sq, skb);
				break;
			}
			if ((cs->dlogflag) && (!(skb->data[2] & 1))) {	/* I-FRAME */
				LogFrame(cs, skb->data, skb->len);
				sprintf(str, "Q.931 frame user->network tei %d", st->l2.tei);
				dlogframe(cs, skb->data + 4, skb->len - 4,
					  str);
			}
			cs->tx_skb = skb;
			cs->tx_cnt = 0;
#ifdef L2FRAME_DEBUG		/* psa */
			if (cs->debug & L1_DEB_LAPD)
				Logl2Frame(cs, skb, "PH_DATA_PULLED", 0);
#endif
			isac_fill_fifo(cs);
			break;
		case (PH_PULL_REQ):
#ifdef L2FRAME_DEBUG		/* psa */
			if (cs->debug & L1_DEB_LAPD)
				debugl1(cs, "-> PH_REQUEST_PULL");
#endif
			if (!cs->tx_skb) {
				test_and_clear_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
				st->l1.l1l2(st, PH_PULL_CNF, NULL);
			} else
				test_and_set_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
			break;
	}
}

void
isac_l1cmd(struct IsdnCardState *cs, int msg, void *arg)
{
	u_char val;
	char tmp[32];
	
	switch(msg) {
		case PH_RESET_REQ:
			if ((cs->ph_state == ISAC_IND_EI) ||
				(cs->ph_state == ISAC_IND_DR) ||
				(cs->ph_state == ISAC_IND_RS))
			        ph_command(cs, ISAC_CMD_TIM);
			else
				ph_command(cs, ISAC_CMD_RS);
			break;
		case PH_ENABLE_REQ:
			ph_command(cs, ISAC_CMD_TIM);
			break;
		case PH_INFO3_REQ:
			ph_command(cs, ISAC_CMD_AR8);
			break;
		case PH_TESTLOOP_REQ:
			val = 0;
			if (1 & (int) arg)
				val |= 0x0c;
			if (2 & (int) arg)
				val |= 0x3;
			if (test_bit(HW_IOM1, &cs->HW_Flags)) {
				/* IOM 1 Mode */
				if (!val) {
					cs->writeisac(cs, ISAC_SPCR, 0xa);
					cs->writeisac(cs, ISAC_ADF1, 0x2);
				} else {
					cs->writeisac(cs, ISAC_SPCR, val);
					cs->writeisac(cs, ISAC_ADF1, 0xa);
				}
			} else {
				/* IOM 2 Mode */
				cs->writeisac(cs, ISAC_SPCR, val);
				if (val)
					cs->writeisac(cs, ISAC_ADF1, 0x8);
				else
					cs->writeisac(cs, ISAC_ADF1, 0x0);
			}
			break;
		default:
			if (cs->debug & L1_DEB_WARN) {
				sprintf(tmp, "isac_l1cmd unknown %4x", msg);
				debugl1(cs, tmp);
			}
			break;
	}
}

void
setstack_isac(struct PStack *st, struct IsdnCardState *cs)
{
	st->l2.l2l1 = ISAC_l2l1;
}

static void
dbusy_timer_handler(struct IsdnCardState *cs)
{
	struct PStack *stptr;

	if (test_bit(FLG_DBUSY_TIMER, &cs->HW_Flags)) {
		if (cs->debug)
			debugl1(cs, "D-Channel Busy");
		test_and_set_bit(FLG_L1_DBUSY, &cs->HW_Flags);
		stptr = cs->stlist;
		
		while (stptr != NULL) {
			stptr->l1.l1l2(stptr, PH_PAUSE_IND, NULL);
			stptr = stptr->next;
		}
	}
}

HISAX_INITFUNC(void
initisac(struct IsdnCardState *cs))
{
	cs->tqueue.routine = (void *) (void *) isac_bh;
	cs->l1cmd = isac_l1cmd;
	cs->setstack_d = setstack_isac;
	cs->dbusytimer.function = (void *) dbusy_timer_handler;
	cs->dbusytimer.data = (long) cs;
	init_timer(&cs->dbusytimer);
  	cs->writeisac(cs, ISAC_MASK, 0xff);
  	cs->mocr = 0xaa;
	if (test_bit(HW_IOM1, &cs->HW_Flags)) {
		/* IOM 1 Mode */
		cs->writeisac(cs, ISAC_ADF2, 0x0);
		cs->writeisac(cs, ISAC_SPCR, 0xa);
		cs->writeisac(cs, ISAC_ADF1, 0x2);
		cs->writeisac(cs, ISAC_STCR, 0x70);
		cs->writeisac(cs, ISAC_MODE, 0xc9);
	} else {
		/* IOM 2 Mode */
		cs->writeisac(cs, ISAC_ADF2, 0x80);
		cs->writeisac(cs, ISAC_SQXR, 0x2f);
		cs->writeisac(cs, ISAC_SPCR, 0x00);
		cs->writeisac(cs, ISAC_STCR, 0x70);
		cs->writeisac(cs, ISAC_MODE, 0xc9);
		cs->writeisac(cs, ISAC_TIMR, 0x00);
		cs->writeisac(cs, ISAC_ADF1, 0x00);
	}
	ph_command(cs, ISAC_CMD_RS);
	cs->writeisac(cs, ISAC_MASK, 0x0);
}

HISAX_INITFUNC(void
clear_pending_isac_ints(struct IsdnCardState *cs))
{
	int val;
	char tmp[64];

	val = cs->readisac(cs, ISAC_STAR);
	sprintf(tmp, "ISAC STAR %x", val);
	debugl1(cs, tmp);
	val = cs->readisac(cs, ISAC_MODE);
	sprintf(tmp, "ISAC MODE %x", val);
	debugl1(cs, tmp);
	val = cs->readisac(cs, ISAC_ADF2);
	sprintf(tmp, "ISAC ADF2 %x", val);
	debugl1(cs, tmp);
	val = cs->readisac(cs, ISAC_ISTA);
	sprintf(tmp, "ISAC ISTA %x", val);
	debugl1(cs, tmp);
	if (val & 0x01) {
		val = cs->readisac(cs, ISAC_EXIR);
		sprintf(tmp, "ISAC EXIR %x", val);
		debugl1(cs, tmp);
	} else if (val & 0x04) {
		val = cs->readisac(cs, ISAC_CIR0);
		sprintf(tmp, "ISAC CIR0 %x", val);
		debugl1(cs, tmp);
		cs->ph_state = (val >> 2) & 0xf;
	} else {
		cs->ph_state = (cs->readisac(cs, ISAC_CIX0) >> 2) & 0xf;
	}
	isac_sched_event(cs, D_L1STATECHANGE);
	cs->writeisac(cs, ISAC_MASK, 0xFF);
	cs->writeisac(cs, ISAC_MASK, 0);
	cs->writeisac(cs, ISAC_CMDR, 0x41);
}
