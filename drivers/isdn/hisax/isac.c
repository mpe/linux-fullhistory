/* $Id: isac.c,v 1.22 1999/08/09 19:04:40 keil Exp $

 * isac.c   ISAC specific routines
 *
 * Author       Karsten Keil (keil@isdn4linux.de)
 *
 *		This file is (c) under GNU PUBLIC LICENSE
 *		For changes and modifications please read
 *		../../../Documentation/isdn/HiSax.cert
 *
 * $Log: isac.c,v $
 * Revision 1.22  1999/08/09 19:04:40  keil
 * Fix race condition - Thanks to Christer Weinigel
 *
 * Revision 1.21  1999/07/12 21:05:17  keil
 * fix race in IRQ handling
 * added watchdog for lost IRQs
 *
 * Revision 1.20  1999/07/09 08:23:06  keil
 * Fix ISAC lost TX IRQ handling
 *
 * Revision 1.19  1999/07/01 08:11:43  keil
 * Common HiSax version for 2.0, 2.1, 2.2 and 2.3 kernel
 *
 * Revision 1.18  1998/11/15 23:54:51  keil
 * changes from 2.0
 *
 * Revision 1.17  1998/08/13 23:36:37  keil
 * HiSax 3.1 - don't work stable with current LinkLevel
 *
 * Revision 1.16  1998/05/25 12:58:01  keil
 * HiSax golden code from certification, Don't use !!!
 * No leased lines, no X75, but many changes.
 *
 * Revision 1.15  1998/04/15 16:45:32  keil
 * new init code
 *
 * Revision 1.14  1998/04/10 10:35:26  paul
 * fixed (silly?) warnings from egcs on Alpha.
 *
 * Revision 1.13  1998/03/07 22:57:01  tsbogend
 * made HiSax working on Linux/Alpha
 *
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
#include "arcofi.h"
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
	printk(KERN_INFO "%s ISAC version (%x): %s\n", s, val, ISACVer[(val >> 5) & 3]);
}

static void
ph_command(struct IsdnCardState *cs, unsigned int command)
{
	if (cs->debug & L1_DEB_ISAC)
		debugl1(cs, "ph_command %x", command);
	cs->writeisac(cs, ISAC_CIX0, (command << 2) | 3);
}


static void
isac_new_ph(struct IsdnCardState *cs)
{
	switch (cs->dc.isac.ph_state) {
		case (ISAC_IND_RS):
		case (ISAC_IND_EI):
			ph_command(cs, ISAC_CMD_DUI);
			l1_msg(cs, HW_RESET | INDICATION, NULL);
			break;
		case (ISAC_IND_DID):
			l1_msg(cs, HW_DEACTIVATE | CONFIRM, NULL);
			break;
		case (ISAC_IND_DR):
			l1_msg(cs, HW_DEACTIVATE | INDICATION, NULL);
			break;
		case (ISAC_IND_PU):
			l1_msg(cs, HW_POWERUP | CONFIRM, NULL);
			break;
		case (ISAC_IND_RSY):
			l1_msg(cs, HW_RSYNC | INDICATION, NULL);
			break;
		case (ISAC_IND_ARD):
			l1_msg(cs, HW_INFO2 | INDICATION, NULL);
			break;
		case (ISAC_IND_AI8):
			l1_msg(cs, HW_INFO4_P8 | INDICATION, NULL);
			break;
		case (ISAC_IND_AI10):
			l1_msg(cs, HW_INFO4_P10 | INDICATION, NULL);
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
			stptr->l1.l1l2(stptr, PH_PAUSE | CONFIRM, NULL);
			stptr = stptr->next;
		}
	}
	if (test_and_clear_bit(D_L1STATECHANGE, &cs->event))
		isac_new_ph(cs);		
	if (test_and_clear_bit(D_RCVBUFREADY, &cs->event))
		DChannel_proc_rcv(cs);
	if (test_and_clear_bit(D_XMTBUFREADY, &cs->event))
		DChannel_proc_xmt(cs);
	if (test_and_clear_bit(D_RX_MON1, &cs->event))
		arcofi_fsm(cs, ARCOFI_RX_END, NULL);
	if (test_and_clear_bit(D_TX_MON1, &cs->event))
		arcofi_fsm(cs, ARCOFI_TX_END, NULL);
}

void
isac_empty_fifo(struct IsdnCardState *cs, int count)
{
	u_char *ptr;
	long flags;

	if ((cs->debug & L1_DEB_ISAC) && !(cs->debug & L1_DEB_ISAC_FIFO))
		debugl1(cs, "isac_empty_fifo");

	if ((cs->rcvidx + count) >= MAX_DFRAME_LEN_L1) {
		if (cs->debug & L1_DEB_WARN)
			debugl1(cs, "isac_empty_fifo overrun %d",
				cs->rcvidx + count);
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
		char *t = cs->dlog;

		t += sprintf(t, "isac_empty_fifo cnt %d", count);
		QuickHex(t, ptr, count);
		debugl1(cs, cs->dlog);
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
	if (test_and_set_bit(FLG_DBUSY_TIMER, &cs->HW_Flags)) {
		debugl1(cs, "isac_fill_fifo dbusytimer running");
		del_timer(&cs->dbusytimer);
	}
	init_timer(&cs->dbusytimer);
	cs->dbusytimer.expires = jiffies + ((DBUSY_TIMER_VALUE * HZ)/1000);
	add_timer(&cs->dbusytimer);
	restore_flags(flags);
	if (cs->debug & L1_DEB_ISAC_FIFO) {
		char *t = cs->dlog;

		t += sprintf(t, "isac_fill_fifo cnt %d", count);
		QuickHex(t, ptr, count);
		debugl1(cs, cs->dlog);
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

	if (cs->debug & L1_DEB_ISAC)
		debugl1(cs, "ISAC interrupt %x", val);
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
					SET_SKB_FREE(skb);
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
				idev_kfree_skb(cs->tx_skb, FREE_WRITE);
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
		exval = cs->readisac(cs, ISAC_CIR0);
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "ISAC CIR0 %02X", exval );
		if (exval & 2) {
			cs->dc.isac.ph_state = (exval >> 2) & 0xf;
			if (cs->debug & L1_DEB_ISAC)
				debugl1(cs, "ph_state change %x", cs->dc.isac.ph_state);
			isac_sched_event(cs, D_L1STATECHANGE);
		}
		if (exval & 1) {
			exval = cs->readisac(cs, ISAC_CIR1);
			if (cs->debug & L1_DEB_ISAC)
				debugl1(cs, "ISAC CIR1 %02X", exval );
		}
	}
	if (val & 0x02) {	/* SIN */
		/* never */
		if (cs->debug & L1_DEB_WARN)
			debugl1(cs, "ISAC SIN interrupt");
	}
	if (val & 0x01) {	/* EXI */
		exval = cs->readisac(cs, ISAC_EXIR);
		if (cs->debug & L1_DEB_WARN)
			debugl1(cs, "ISAC EXIR %02x", exval);
		if (exval & 0x80) {  /* XMR */
			debugl1(cs, "ISAC XMR");
			printk(KERN_WARNING "HiSax: ISAC XMR\n");
		}
		if (exval & 0x40) {  /* XDU */
			debugl1(cs, "ISAC XDU");
			printk(KERN_WARNING "HiSax: ISAC XDU\n");
			if (test_and_clear_bit(FLG_DBUSY_TIMER, &cs->HW_Flags))
				del_timer(&cs->dbusytimer);
			if (test_and_clear_bit(FLG_L1_DBUSY, &cs->HW_Flags))
				isac_sched_event(cs, D_CLEARBUSY);
			if (cs->tx_skb) { /* Restart frame */
				skb_push(cs->tx_skb, cs->tx_cnt);
				cs->tx_cnt = 0;
				isac_fill_fifo(cs);
			} else {
				printk(KERN_WARNING "HiSax: ISAC XDU no skb\n");
				debugl1(cs, "ISAC XDU no skb");
			}
		}
		if (exval & 0x04) {  /* MOS */
			v1 = cs->readisac(cs, ISAC_MOSR);
			if (cs->debug & L1_DEB_MONITOR)
				debugl1(cs, "ISAC MOSR %02x", v1);
#if ARCOFI_USE
			if (v1 & 0x08) {
				if (!cs->dc.isac.mon_rx) {
					if (!(cs->dc.isac.mon_rx = kmalloc(MAX_MON_FRAME, GFP_ATOMIC))) {
						if (cs->debug & L1_DEB_WARN)
							debugl1(cs, "ISAC MON RX out of memory!");
						cs->dc.isac.mocr &= 0xf0;
						cs->dc.isac.mocr |= 0x0a;
						cs->writeisac(cs, ISAC_MOCR, cs->dc.isac.mocr);
						goto afterMONR0;
					} else
						cs->dc.isac.mon_rxp = 0;
				}
				if (cs->dc.isac.mon_rxp >= MAX_MON_FRAME) {
					cs->dc.isac.mocr &= 0xf0;
					cs->dc.isac.mocr |= 0x0a;
					cs->writeisac(cs, ISAC_MOCR, cs->dc.isac.mocr);
					cs->dc.isac.mon_rxp = 0;
					if (cs->debug & L1_DEB_WARN)
						debugl1(cs, "ISAC MON RX overflow!");
					goto afterMONR0;
				}
				cs->dc.isac.mon_rx[cs->dc.isac.mon_rxp++] = cs->readisac(cs, ISAC_MOR0);
				if (cs->debug & L1_DEB_MONITOR)
					debugl1(cs, "ISAC MOR0 %02x", cs->dc.isac.mon_rx[cs->dc.isac.mon_rxp -1]);
				if (cs->dc.isac.mon_rxp == 1) {
					cs->dc.isac.mocr |= 0x04;
					cs->writeisac(cs, ISAC_MOCR, cs->dc.isac.mocr);
				}
			}
		      afterMONR0:
			if (v1 & 0x80) {
				if (!cs->dc.isac.mon_rx) {
					if (!(cs->dc.isac.mon_rx = kmalloc(MAX_MON_FRAME, GFP_ATOMIC))) {
						if (cs->debug & L1_DEB_WARN)
							debugl1(cs, "ISAC MON RX out of memory!");
						cs->dc.isac.mocr &= 0x0f;
						cs->dc.isac.mocr |= 0xa0;
						cs->writeisac(cs, ISAC_MOCR, cs->dc.isac.mocr);
						goto afterMONR1;
					} else
						cs->dc.isac.mon_rxp = 0;
				}
				if (cs->dc.isac.mon_rxp >= MAX_MON_FRAME) {
					cs->dc.isac.mocr &= 0x0f;
					cs->dc.isac.mocr |= 0xa0;
					cs->writeisac(cs, ISAC_MOCR, cs->dc.isac.mocr);
					cs->dc.isac.mon_rxp = 0;
					if (cs->debug & L1_DEB_WARN)
						debugl1(cs, "ISAC MON RX overflow!");
					goto afterMONR1;
				}
				cs->dc.isac.mon_rx[cs->dc.isac.mon_rxp++] = cs->readisac(cs, ISAC_MOR1);
				if (cs->debug & L1_DEB_MONITOR)
					debugl1(cs, "ISAC MOR1 %02x", cs->dc.isac.mon_rx[cs->dc.isac.mon_rxp -1]);
				cs->dc.isac.mocr |= 0x40;
				cs->writeisac(cs, ISAC_MOCR, cs->dc.isac.mocr);
			}
		      afterMONR1:
			if (v1 & 0x04) {
				cs->dc.isac.mocr &= 0xf0;
				cs->writeisac(cs, ISAC_MOCR, cs->dc.isac.mocr);
				cs->dc.isac.mocr |= 0x0a;
				cs->writeisac(cs, ISAC_MOCR, cs->dc.isac.mocr);
				isac_sched_event(cs, D_RX_MON0);
			}
			if (v1 & 0x40) {
				cs->dc.isac.mocr &= 0x0f;
				cs->writeisac(cs, ISAC_MOCR, cs->dc.isac.mocr);
				cs->dc.isac.mocr |= 0xa0;
				cs->writeisac(cs, ISAC_MOCR, cs->dc.isac.mocr);
				isac_sched_event(cs, D_RX_MON1);
			}
			if (v1 & 0x02) {
				if ((!cs->dc.isac.mon_tx) || (cs->dc.isac.mon_txc && 
					(cs->dc.isac.mon_txp >= cs->dc.isac.mon_txc) && 
					!(v1 & 0x08))) {
					cs->dc.isac.mocr &= 0xf0;
					cs->writeisac(cs, ISAC_MOCR, cs->dc.isac.mocr);
					cs->dc.isac.mocr |= 0x0a;
					cs->writeisac(cs, ISAC_MOCR, cs->dc.isac.mocr);
					if (cs->dc.isac.mon_txc &&
						(cs->dc.isac.mon_txp >= cs->dc.isac.mon_txc))
						isac_sched_event(cs, D_TX_MON0);
					goto AfterMOX0;
				}
				if (cs->dc.isac.mon_txc && (cs->dc.isac.mon_txp >= cs->dc.isac.mon_txc)) {
					isac_sched_event(cs, D_TX_MON0);
					goto AfterMOX0;
				}
				cs->writeisac(cs, ISAC_MOX0,
					cs->dc.isac.mon_tx[cs->dc.isac.mon_txp++]);
				if (cs->debug & L1_DEB_MONITOR)
					debugl1(cs, "ISAC %02x -> MOX0", cs->dc.isac.mon_tx[cs->dc.isac.mon_txp -1]);
			}
		      AfterMOX0:
			if (v1 & 0x20) {
				if ((!cs->dc.isac.mon_tx) || (cs->dc.isac.mon_txc && 
					(cs->dc.isac.mon_txp >= cs->dc.isac.mon_txc) && 
					!(v1 & 0x80))) {
					cs->dc.isac.mocr &= 0x0f;
					cs->writeisac(cs, ISAC_MOCR, cs->dc.isac.mocr);
					cs->dc.isac.mocr |= 0xa0;
					cs->writeisac(cs, ISAC_MOCR, cs->dc.isac.mocr);
					if (cs->dc.isac.mon_txc &&
						(cs->dc.isac.mon_txp >= cs->dc.isac.mon_txc))
						isac_sched_event(cs, D_TX_MON1);
					goto AfterMOX1;
				}
				if (cs->dc.isac.mon_txc && (cs->dc.isac.mon_txp >= cs->dc.isac.mon_txc)) {
					isac_sched_event(cs, D_TX_MON1);
					goto AfterMOX1;
				}
				cs->writeisac(cs, ISAC_MOX1,
					cs->dc.isac.mon_tx[cs->dc.isac.mon_txp++]);
				if (cs->debug & L1_DEB_MONITOR)
					debugl1(cs, "ISAC %02x -> MOX1", cs->dc.isac.mon_tx[cs->dc.isac.mon_txp -1]);
			}
		      AfterMOX1:
#endif
		}
	}
}

static void
ISAC_l1hw(struct PStack *st, int pr, void *arg)
{
	struct IsdnCardState *cs = (struct IsdnCardState *) st->l1.hardware;
	struct sk_buff *skb = arg;
	int  val;

	switch (pr) {
		case (PH_DATA |REQUEST):
			if (cs->debug & DEB_DLOG_HEX)
				LogFrame(cs, skb->data, skb->len);
			if (cs->debug & DEB_DLOG_VERBOSE)
				dlogframe(cs, skb, 0);
			if (cs->tx_skb) {
				skb_queue_tail(&cs->sq, skb);
#ifdef L2FRAME_DEBUG		/* psa */
				if (cs->debug & L1_DEB_LAPD)
					Logl2Frame(cs, skb, "PH_DATA Queued", 0);
#endif
			} else {
				cs->tx_skb = skb;
				cs->tx_cnt = 0;
#ifdef L2FRAME_DEBUG		/* psa */
				if (cs->debug & L1_DEB_LAPD)
					Logl2Frame(cs, skb, "PH_DATA", 0);
#endif
				isac_fill_fifo(cs);
			}
			break;
		case (PH_PULL |INDICATION):
			if (cs->tx_skb) {
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, " l2l1 tx_skb exist this shouldn't happen");
				skb_queue_tail(&cs->sq, skb);
				break;
			}
			if (cs->debug & DEB_DLOG_HEX)
				LogFrame(cs, skb->data, skb->len);
			if (cs->debug & DEB_DLOG_VERBOSE)
				dlogframe(cs, skb, 0);
			cs->tx_skb = skb;
			cs->tx_cnt = 0;
#ifdef L2FRAME_DEBUG		/* psa */
			if (cs->debug & L1_DEB_LAPD)
				Logl2Frame(cs, skb, "PH_DATA_PULLED", 0);
#endif
			isac_fill_fifo(cs);
			break;
		case (PH_PULL | REQUEST):
#ifdef L2FRAME_DEBUG		/* psa */
			if (cs->debug & L1_DEB_LAPD)
				debugl1(cs, "-> PH_REQUEST_PULL");
#endif
			if (!cs->tx_skb) {
				test_and_clear_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
				st->l1.l1l2(st, PH_PULL | CONFIRM, NULL);
			} else
				test_and_set_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
			break;
		case (HW_RESET | REQUEST):
			if ((cs->dc.isac.ph_state == ISAC_IND_EI) ||
				(cs->dc.isac.ph_state == ISAC_IND_DR) ||
				(cs->dc.isac.ph_state == ISAC_IND_RS))
			        ph_command(cs, ISAC_CMD_TIM);
			else
				ph_command(cs, ISAC_CMD_RS);
			break;
		case (HW_ENABLE | REQUEST):
			ph_command(cs, ISAC_CMD_TIM);
			break;
		case (HW_INFO3 | REQUEST):
			ph_command(cs, ISAC_CMD_AR8);
			break;
		case (HW_TESTLOOP | REQUEST):
			val = 0;
			if (1 & (long) arg)
				val |= 0x0c;
			if (2 & (long) arg)
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
		case (HW_DEACTIVATE | RESPONSE):
			discard_queue(&cs->rq);
			discard_queue(&cs->sq);
			if (cs->tx_skb) {
				idev_kfree_skb(cs->tx_skb, FREE_WRITE);
				cs->tx_skb = NULL;
			}
			if (test_and_clear_bit(FLG_DBUSY_TIMER, &cs->HW_Flags))
				del_timer(&cs->dbusytimer);
			if (test_and_clear_bit(FLG_L1_DBUSY, &cs->HW_Flags))
				isac_sched_event(cs, D_CLEARBUSY);
			break;
		default:
			if (cs->debug & L1_DEB_WARN)
				debugl1(cs, "isac_l1hw unknown %04x", pr);
			break;
	}
}

void
setstack_isac(struct PStack *st, struct IsdnCardState *cs)
{
	st->l1.l1hw = ISAC_l1hw;
}

void 
DC_Close_isac(struct IsdnCardState *cs) {
	if (cs->dc.isac.mon_rx) {
		kfree(cs->dc.isac.mon_rx);
		cs->dc.isac.mon_rx = NULL;
	}
	if (cs->dc.isac.mon_tx) {
		kfree(cs->dc.isac.mon_tx);
		cs->dc.isac.mon_tx = NULL;
	}
}

static void
dbusy_timer_handler(struct IsdnCardState *cs)
{
	struct PStack *stptr;
	int	rbch, star;

	if (test_bit(FLG_DBUSY_TIMER, &cs->HW_Flags)) {
		rbch = cs->readisac(cs, ISAC_RBCH);
		star = cs->readisac(cs, ISAC_STAR);
		if (cs->debug) 
			debugl1(cs, "D-Channel Busy RBCH %02x STAR %02x",
				rbch, star);
		if (rbch & ISAC_RBCH_XAC) { /* D-Channel Busy */
			test_and_set_bit(FLG_L1_DBUSY, &cs->HW_Flags);
			stptr = cs->stlist;
			while (stptr != NULL) {
				stptr->l1.l1l2(stptr, PH_PAUSE | INDICATION, NULL);
				stptr = stptr->next;
			}
		} else {
			/* discard frame; reset transceiver */
			test_and_clear_bit(FLG_DBUSY_TIMER, &cs->HW_Flags);
			if (cs->tx_skb) {
				idev_kfree_skb(cs->tx_skb, FREE_WRITE);
				cs->tx_cnt = 0;
				cs->tx_skb = NULL;
			} else {
				printk(KERN_WARNING "HiSax: ISAC D-Channel Busy no skb\n");
				debugl1(cs, "D-Channel Busy no skb");
			}
			cs->writeisac(cs, ISAC_CMDR, 0x01); /* Transmitter reset */
			cs->irq_func(cs->irq, cs, NULL);
		}
	}
}

HISAX_INITFUNC(void
initisac(struct IsdnCardState *cs))
{
	cs->tqueue.routine = (void *) (void *) isac_bh;
	cs->setstack_d = setstack_isac;
	cs->DC_Close = DC_Close_isac;
	cs->dc.isac.mon_tx = NULL;
	cs->dc.isac.mon_rx = NULL;
	cs->dbusytimer.function = (void *) dbusy_timer_handler;
	cs->dbusytimer.data = (long) cs;
	init_timer(&cs->dbusytimer);
  	cs->writeisac(cs, ISAC_MASK, 0xff);
  	cs->dc.isac.mocr = 0xaa;
	if (test_bit(HW_IOM1, &cs->HW_Flags)) {
		/* IOM 1 Mode */
		cs->writeisac(cs, ISAC_ADF2, 0x0);
		cs->writeisac(cs, ISAC_SPCR, 0xa);
		cs->writeisac(cs, ISAC_ADF1, 0x2);
		cs->writeisac(cs, ISAC_STCR, 0x70);
		cs->writeisac(cs, ISAC_MODE, 0xc9);
	} else {
		/* IOM 2 Mode */
		if (!cs->dc.isac.adf2)
			cs->dc.isac.adf2 = 0x80;
		cs->writeisac(cs, ISAC_ADF2, cs->dc.isac.adf2);
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
	int val, eval;

	val = cs->readisac(cs, ISAC_STAR);
	debugl1(cs, "ISAC STAR %x", val);
	val = cs->readisac(cs, ISAC_MODE);
	debugl1(cs, "ISAC MODE %x", val);
	val = cs->readisac(cs, ISAC_ADF2);
	debugl1(cs, "ISAC ADF2 %x", val);
	val = cs->readisac(cs, ISAC_ISTA);
	debugl1(cs, "ISAC ISTA %x", val);
	if (val & 0x01) {
		eval = cs->readisac(cs, ISAC_EXIR);
		debugl1(cs, "ISAC EXIR %x", eval);
	}
	val = cs->readisac(cs, ISAC_CIR0);
	debugl1(cs, "ISAC CIR0 %x", val);
	cs->dc.isac.ph_state = (val >> 2) & 0xf;
	isac_sched_event(cs, D_L1STATECHANGE);
	/* Disable all IRQ */
	cs->writeisac(cs, ISAC_MASK, 0xFF);
}
