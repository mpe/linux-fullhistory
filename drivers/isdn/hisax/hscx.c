/* $Id: hscx.c,v 1.18 2000/02/26 00:35:13 keil Exp $

 * hscx.c   HSCX specific routines
 *
 * Author       Karsten Keil (keil@isdn4linux.de)
 *
 *
 * $Log: hscx.c,v $
 * Revision 1.18  2000/02/26 00:35:13  keil
 * Fix skb freeing in interrupt context
 *
 * Revision 1.17  1999/07/01 08:11:41  keil
 * Common HiSax version for 2.0, 2.1, 2.2 and 2.3 kernel
 *
 * Revision 1.16  1998/11/15 23:54:48  keil
 * changes from 2.0
 *
 * Revision 1.15  1998/08/20 13:50:42  keil
 * More support for hybrid modem (not working yet)
 *
 * Revision 1.14  1998/08/13 23:36:33  keil
 * HiSax 3.1 - don't work stable with current LinkLevel
 *
 * Revision 1.13  1998/06/26 22:03:28  keil
 * send flags between hdlc frames
 *
 * Revision 1.12  1998/06/09 18:26:01  keil
 * PH_DEACTIVATE B-channel every time signaled to higher layer
 *
 * Revision 1.11  1998/05/25 14:10:07  keil
 * HiSax 3.0
 * X.75 and leased are working again.
 *
 * Revision 1.10  1998/05/25 12:57:59  keil
 * HiSax golden code from certification, Don't use !!!
 * No leased lines, no X75, but many changes.
 *
 * Revision 1.9  1998/04/15 16:45:33  keil
 * new init code
 *
 * Revision 1.8  1998/03/19 13:16:24  keil
 * fix the correct release of the hscx
 *
 * Revision 1.7  1998/02/12 23:07:36  keil
 * change for 2.1.86 (removing FREE_READ/FREE_WRITE from [dev]_kfree_skb()
 *
 * Revision 1.6  1998/02/02 13:41:12  keil
 * new init
 *
 * Revision 1.5  1997/11/06 17:09:34  keil
 * New 2.1 init code
 *
 * Revision 1.4  1997/10/29 19:01:06  keil
 * changes for 2.1
 *
 * Revision 1.3  1997/07/27 21:38:34  keil
 * new B-channel interface
 *
 * Revision 1.2  1997/06/26 11:16:17  keil
 * first version
 *
 *
 */

#define __NO_VERSION__
#include "hisax.h"
#include "hscx.h"
#include "isac.h"
#include "isdnl1.h"
#include <linux/interrupt.h>

static char *HSCXVer[] HISAX_INITDATA =
{"A1", "?1", "A2", "?3", "A3", "V2.1", "?6", "?7",
 "?8", "?9", "?10", "?11", "?12", "?13", "?14", "???"};

HISAX_INITFUNC(int
HscxVersion(struct IsdnCardState *cs, char *s))
{
	int verA, verB;

	verA = cs->BC_Read_Reg(cs, 0, HSCX_VSTR) & 0xf;
	verB = cs->BC_Read_Reg(cs, 1, HSCX_VSTR) & 0xf;
	printk(KERN_INFO "%s HSCX version A: %s  B: %s\n", s,
	       HSCXVer[verA], HSCXVer[verB]);
	if ((verA == 0) | (verA == 0xf) | (verB == 0) | (verB == 0xf))
		return (1);
	else
		return (0);
}

void
modehscx(struct BCState *bcs, int mode, int bc)
{
	struct IsdnCardState *cs = bcs->cs;
	int hscx = bcs->hw.hscx.hscx;

	if (cs->debug & L1_DEB_HSCX)
		debugl1(cs, "hscx %c mode %d ichan %d",
			'A' + hscx, mode, bc);
	bcs->mode = mode;
	bcs->channel = bc;
	cs->BC_Write_Reg(cs, hscx, HSCX_XAD1, 0xFF);
	cs->BC_Write_Reg(cs, hscx, HSCX_XAD2, 0xFF);
	cs->BC_Write_Reg(cs, hscx, HSCX_RAH2, 0xFF);
	cs->BC_Write_Reg(cs, hscx, HSCX_XBCH, 0x0);
	cs->BC_Write_Reg(cs, hscx, HSCX_RLCR, 0x0);
	cs->BC_Write_Reg(cs, hscx, HSCX_CCR1,
		test_bit(HW_IPAC, &cs->HW_Flags) ? 0x82 : 0x85);
	cs->BC_Write_Reg(cs, hscx, HSCX_CCR2, 0x30);
	cs->BC_Write_Reg(cs, hscx, HSCX_XCCR, 7);
	cs->BC_Write_Reg(cs, hscx, HSCX_RCCR, 7);

	/* Switch IOM 1 SSI */
	if (test_bit(HW_IOM1, &cs->HW_Flags) && (hscx == 0))
		bc = 1 - bc;

	if (bc == 0) {
		cs->BC_Write_Reg(cs, hscx, HSCX_TSAX,
			      test_bit(HW_IOM1, &cs->HW_Flags) ? 0x7 : bcs->hw.hscx.tsaxr0);
		cs->BC_Write_Reg(cs, hscx, HSCX_TSAR,
			      test_bit(HW_IOM1, &cs->HW_Flags) ? 0x7 : bcs->hw.hscx.tsaxr0);
	} else {
		cs->BC_Write_Reg(cs, hscx, HSCX_TSAX, bcs->hw.hscx.tsaxr1);
		cs->BC_Write_Reg(cs, hscx, HSCX_TSAR, bcs->hw.hscx.tsaxr1);
	}
	switch (mode) {
		case (L1_MODE_NULL):
			cs->BC_Write_Reg(cs, hscx, HSCX_TSAX, 0x1f);
			cs->BC_Write_Reg(cs, hscx, HSCX_TSAR, 0x1f);
			cs->BC_Write_Reg(cs, hscx, HSCX_MODE, 0x84);
			break;
		case (L1_MODE_TRANS):
			cs->BC_Write_Reg(cs, hscx, HSCX_MODE, 0xe4);
			break;
		case (L1_MODE_HDLC):
			cs->BC_Write_Reg(cs, hscx, HSCX_CCR1,
				test_bit(HW_IPAC, &cs->HW_Flags) ? 0x8a : 0x8d);
			cs->BC_Write_Reg(cs, hscx, HSCX_MODE, 0x8c);
			break;
	}
	if (mode)
		cs->BC_Write_Reg(cs, hscx, HSCX_CMDR, 0x41);
	cs->BC_Write_Reg(cs, hscx, HSCX_ISTA, 0x00);
}

void
hscx_sched_event(struct BCState *bcs, int event)
{
	bcs->event |= 1 << event;
	queue_task(&bcs->tqueue, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
}

void
hscx_l2l1(struct PStack *st, int pr, void *arg)
{
	struct sk_buff *skb = arg;
	long flags;

	switch (pr) {
		case (PH_DATA | REQUEST):
			save_flags(flags);
			cli();
			if (st->l1.bcs->tx_skb) {
				skb_queue_tail(&st->l1.bcs->squeue, skb);
				restore_flags(flags);
			} else {
				st->l1.bcs->tx_skb = skb;
				test_and_set_bit(BC_FLG_BUSY, &st->l1.bcs->Flag);
				st->l1.bcs->hw.hscx.count = 0;
				restore_flags(flags);
				st->l1.bcs->cs->BC_Send_Data(st->l1.bcs);
			}
			break;
		case (PH_PULL | INDICATION):
			if (st->l1.bcs->tx_skb) {
				printk(KERN_WARNING "hscx_l2l1: this shouldn't happen\n");
				break;
			}
			test_and_set_bit(BC_FLG_BUSY, &st->l1.bcs->Flag);
			st->l1.bcs->tx_skb = skb;
			st->l1.bcs->hw.hscx.count = 0;
			st->l1.bcs->cs->BC_Send_Data(st->l1.bcs);
			break;
		case (PH_PULL | REQUEST):
			if (!st->l1.bcs->tx_skb) {
				test_and_clear_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
				st->l1.l1l2(st, PH_PULL | CONFIRM, NULL);
			} else
				test_and_set_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
			break;
		case (PH_ACTIVATE | REQUEST):
			test_and_set_bit(BC_FLG_ACTIV, &st->l1.bcs->Flag);
			modehscx(st->l1.bcs, st->l1.mode, st->l1.bc);
			l1_msg_b(st, pr, arg);
			break;
		case (PH_DEACTIVATE | REQUEST):
			l1_msg_b(st, pr, arg);
			break;
		case (PH_DEACTIVATE | CONFIRM):
			test_and_clear_bit(BC_FLG_ACTIV, &st->l1.bcs->Flag);
			test_and_clear_bit(BC_FLG_BUSY, &st->l1.bcs->Flag);
			modehscx(st->l1.bcs, 0, st->l1.bc);
			st->l1.l1l2(st, PH_DEACTIVATE | CONFIRM, NULL);
			break;
	}
}

void
close_hscxstate(struct BCState *bcs)
{
	modehscx(bcs, 0, bcs->channel);
	if (test_and_clear_bit(BC_FLG_INIT, &bcs->Flag)) {
		if (bcs->hw.hscx.rcvbuf) {
			kfree(bcs->hw.hscx.rcvbuf);
			bcs->hw.hscx.rcvbuf = NULL;
		}
		if (bcs->blog) {
			kfree(bcs->blog);
			bcs->blog = NULL;
		}
		discard_queue(&bcs->rqueue);
		discard_queue(&bcs->squeue);
		if (bcs->tx_skb) {
			dev_kfree_skb_any(bcs->tx_skb);
			bcs->tx_skb = NULL;
			test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
		}
	}
}

int
open_hscxstate(struct IsdnCardState *cs, struct BCState *bcs)
{
	if (!test_and_set_bit(BC_FLG_INIT, &bcs->Flag)) {
		if (!(bcs->hw.hscx.rcvbuf = kmalloc(HSCX_BUFMAX, GFP_ATOMIC))) {
			printk(KERN_WARNING
				"HiSax: No memory for hscx.rcvbuf\n");
			test_and_clear_bit(BC_FLG_INIT, &bcs->Flag);
			return (1);
		}
		if (!(bcs->blog = kmalloc(MAX_BLOG_SPACE, GFP_ATOMIC))) {
			printk(KERN_WARNING
				"HiSax: No memory for bcs->blog\n");
			test_and_clear_bit(BC_FLG_INIT, &bcs->Flag);
			kfree(bcs->hw.hscx.rcvbuf);
			bcs->hw.hscx.rcvbuf = NULL;
			return (2);
		}
		skb_queue_head_init(&bcs->rqueue);
		skb_queue_head_init(&bcs->squeue);
	}
	bcs->tx_skb = NULL;
	test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
	bcs->event = 0;
	bcs->hw.hscx.rcvidx = 0;
	bcs->tx_cnt = 0;
	return (0);
}

int
setstack_hscx(struct PStack *st, struct BCState *bcs)
{
	bcs->channel = st->l1.bc;
	if (open_hscxstate(st->l1.hardware, bcs))
		return (-1);
	st->l1.bcs = bcs;
	st->l2.l2l1 = hscx_l2l1;
	setstack_manager(st);
	bcs->st = st;
	setstack_l1_B(st);
	return (0);
}

HISAX_INITFUNC(void
clear_pending_hscx_ints(struct IsdnCardState *cs))
{
	int val, eval;

	val = cs->BC_Read_Reg(cs, 1, HSCX_ISTA);
	debugl1(cs, "HSCX B ISTA %x", val);
	if (val & 0x01) {
		eval = cs->BC_Read_Reg(cs, 1, HSCX_EXIR);
		debugl1(cs, "HSCX B EXIR %x", eval);
	}
	if (val & 0x02) {
		eval = cs->BC_Read_Reg(cs, 0, HSCX_EXIR);
		debugl1(cs, "HSCX A EXIR %x", eval);
	}
	val = cs->BC_Read_Reg(cs, 0, HSCX_ISTA);
	debugl1(cs, "HSCX A ISTA %x", val);
	val = cs->BC_Read_Reg(cs, 1, HSCX_STAR);
	debugl1(cs, "HSCX B STAR %x", val);
	val = cs->BC_Read_Reg(cs, 0, HSCX_STAR);
	debugl1(cs, "HSCX A STAR %x", val);
	/* disable all IRQ */
	cs->BC_Write_Reg(cs, 0, HSCX_MASK, 0xFF);
	cs->BC_Write_Reg(cs, 1, HSCX_MASK, 0xFF);
}

HISAX_INITFUNC(void
inithscx(struct IsdnCardState *cs))
{
	cs->bcs[0].BC_SetStack = setstack_hscx;
	cs->bcs[1].BC_SetStack = setstack_hscx;
	cs->bcs[0].BC_Close = close_hscxstate;
	cs->bcs[1].BC_Close = close_hscxstate;
	cs->bcs[0].hw.hscx.hscx = 0;
	cs->bcs[1].hw.hscx.hscx = 1;
	cs->bcs[0].hw.hscx.tsaxr0 = 0x2f;
	cs->bcs[0].hw.hscx.tsaxr1 = 3;
	cs->bcs[1].hw.hscx.tsaxr0 = 0x2f;
	cs->bcs[1].hw.hscx.tsaxr1 = 3;
	modehscx(cs->bcs, 0, 0);
	modehscx(cs->bcs + 1, 0, 0);
}

HISAX_INITFUNC(void
inithscxisac(struct IsdnCardState *cs, int part))
{
	if (part & 1) {
		clear_pending_isac_ints(cs);
		clear_pending_hscx_ints(cs);
		initisac(cs);
		inithscx(cs);
	}
	if (part & 2) {
		/* Reenable all IRQ */
		cs->writeisac(cs, ISAC_MASK, 0);
		cs->BC_Write_Reg(cs, 0, HSCX_MASK, 0);
		cs->BC_Write_Reg(cs, 1, HSCX_MASK, 0);
		/* RESET Receiver and Transmitter */
		cs->writeisac(cs, ISAC_CMDR, 0x41);
	}
}
