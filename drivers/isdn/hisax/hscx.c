/* $Id: hscx.c,v 1.7 1998/02/12 23:07:36 keil Exp $

 * hscx.c   HSCX specific routines
 *
 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *
 *
 * $Log: hscx.c,v $
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
	int hscx = bcs->channel;

	if (cs->debug & L1_DEB_HSCX) {
		char tmp[40];
		sprintf(tmp, "hscx %c mode %d ichan %d",
			'A' + hscx, mode, bc);
		debugl1(cs, tmp);
	}
	bcs->mode = mode;
	cs->BC_Write_Reg(cs, hscx, HSCX_CCR1, 0x85);
	cs->BC_Write_Reg(cs, hscx, HSCX_XAD1, 0xFF);
	cs->BC_Write_Reg(cs, hscx, HSCX_XAD2, 0xFF);
	cs->BC_Write_Reg(cs, hscx, HSCX_RAH2, 0xFF);
	cs->BC_Write_Reg(cs, hscx, HSCX_XBCH, 0x0);
	cs->BC_Write_Reg(cs, hscx, HSCX_RLCR, 0x0);
	cs->BC_Write_Reg(cs, hscx, HSCX_CCR2, 0x30);
	cs->BC_Write_Reg(cs, hscx, HSCX_XCCR, 7);
	cs->BC_Write_Reg(cs, hscx, HSCX_RCCR, 7);

	/* Switch IOM 1 SSI */
	if (test_bit(HW_IOM1, &cs->HW_Flags) && (hscx == 0))
		bc = 1 - bc;

	if (bc == 0) {
		cs->BC_Write_Reg(cs, hscx, HSCX_TSAX,
			      test_bit(HW_IOM1, &cs->HW_Flags) ? 0x7 : 0x2f);
		cs->BC_Write_Reg(cs, hscx, HSCX_TSAR,
			      test_bit(HW_IOM1, &cs->HW_Flags) ? 0x7 : 0x2f);
	} else {
		cs->BC_Write_Reg(cs, hscx, HSCX_TSAX, 0x3);
		cs->BC_Write_Reg(cs, hscx, HSCX_TSAR, 0x3);
	}
	switch (mode) {
		case (L1_MODE_NULL):
			cs->BC_Write_Reg(cs, hscx, HSCX_TSAX, 0xff);
			cs->BC_Write_Reg(cs, hscx, HSCX_TSAR, 0xff);
			cs->BC_Write_Reg(cs, hscx, HSCX_MODE, 0x84);
			break;
		case (L1_MODE_TRANS):
			cs->BC_Write_Reg(cs, hscx, HSCX_MODE, 0xe4);
			break;
		case (L1_MODE_HDLC):
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

static void
hscx_l2l1(struct PStack *st, int pr, void *arg)
{
	struct sk_buff *skb = arg;
	long flags;

	switch (pr) {
		case (PH_DATA_REQ):
			save_flags(flags);
			cli();
			if (st->l1.bcs->hw.hscx.tx_skb) {
				skb_queue_tail(&st->l1.bcs->squeue, skb);
				restore_flags(flags);
			} else {
				st->l1.bcs->hw.hscx.tx_skb = skb;
				test_and_set_bit(BC_FLG_BUSY, &st->l1.bcs->Flag);
				st->l1.bcs->hw.hscx.count = 0;
				restore_flags(flags);
				st->l1.bcs->cs->BC_Send_Data(st->l1.bcs);
			}
			break;
		case (PH_PULL_IND):
			if (st->l1.bcs->hw.hscx.tx_skb) {
				printk(KERN_WARNING "hscx_l2l1: this shouldn't happen\n");
				break;
			}
			test_and_set_bit(BC_FLG_BUSY, &st->l1.bcs->Flag);
			st->l1.bcs->hw.hscx.tx_skb = skb;
			st->l1.bcs->hw.hscx.count = 0;
			st->l1.bcs->cs->BC_Send_Data(st->l1.bcs);
			break;
		case (PH_PULL_REQ):
			if (!st->l1.bcs->hw.hscx.tx_skb) {
				test_and_clear_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
				st->l1.l1l2(st, PH_PULL_CNF, NULL);
			} else
				test_and_set_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
			break;
	}

}

void
close_hscxstate(struct BCState *bcs)
{
	struct sk_buff *skb;

	modehscx(bcs, 0, 0);
	if (test_and_clear_bit(BC_FLG_INIT, &bcs->Flag)) {
		if (bcs->hw.hscx.rcvbuf) {
			kfree(bcs->hw.hscx.rcvbuf);
			bcs->hw.hscx.rcvbuf = NULL;
		}
		while ((skb = skb_dequeue(&bcs->rqueue))) {
			dev_kfree_skb(skb);
		}
		while ((skb = skb_dequeue(&bcs->squeue))) {
			dev_kfree_skb(skb);
		}
		if (bcs->hw.hscx.tx_skb) {
			dev_kfree_skb(bcs->hw.hscx.tx_skb);
			bcs->hw.hscx.tx_skb = NULL;
			test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
		}
	}
}

static int
open_hscxstate(struct IsdnCardState *cs,
	       int bc)
{
	struct BCState *bcs = cs->bcs + bc;

	if (!test_and_set_bit(BC_FLG_INIT, &bcs->Flag)) {
		if (!(bcs->hw.hscx.rcvbuf = kmalloc(HSCX_BUFMAX, GFP_ATOMIC))) {
			printk(KERN_WARNING
			       "HiSax: No memory for hscx.rcvbuf\n");
			return (1);
		}
		skb_queue_head_init(&bcs->rqueue);
		skb_queue_head_init(&bcs->squeue);
	}
	bcs->hw.hscx.tx_skb = NULL;
	test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
	bcs->event = 0;
	bcs->hw.hscx.rcvidx = 0;
	bcs->tx_cnt = 0;
	return (0);
}

static void
hscx_manl1(struct PStack *st, int pr,
	   void *arg)
{
	switch (pr) {
		case (PH_ACTIVATE_REQ):
			test_and_set_bit(BC_FLG_ACTIV, &st->l1.bcs->Flag);
			modehscx(st->l1.bcs, st->l1.mode, st->l1.bc);
			st->l1.l1man(st, PH_ACTIVATE_CNF, NULL);
			break;
		case (PH_DEACTIVATE_REQ):
			if (!test_bit(BC_FLG_BUSY, &st->l1.bcs->Flag))
				modehscx(st->l1.bcs, 0, 0);
			test_and_clear_bit(BC_FLG_ACTIV, &st->l1.bcs->Flag);
			break;
	}
}

int
setstack_hscx(struct PStack *st, struct BCState *bcs)
{
	if (open_hscxstate(st->l1.hardware, bcs->channel))
		return (-1);
	st->l1.bcs = bcs;
	st->l2.l2l1 = hscx_l2l1;
	st->ma.manl1 = hscx_manl1;
	setstack_manager(st);
	bcs->st = st;
	return (0);
}

HISAX_INITFUNC(void
clear_pending_hscx_ints(struct IsdnCardState *cs))
{
	int val;
	char tmp[64];

	val = cs->BC_Read_Reg(cs, 1, HSCX_ISTA);
	sprintf(tmp, "HSCX B ISTA %x", val);
	debugl1(cs, tmp);
	if (val & 0x01) {
		val = cs->BC_Read_Reg(cs, 1, HSCX_EXIR);
		sprintf(tmp, "HSCX B EXIR %x", val);
		debugl1(cs, tmp);
	} else if (val & 0x02) {
		val = cs->BC_Read_Reg(cs, 0, HSCX_EXIR);
		sprintf(tmp, "HSCX A EXIR %x", val);
		debugl1(cs, tmp);
	}
	val = cs->BC_Read_Reg(cs, 0, HSCX_ISTA);
	sprintf(tmp, "HSCX A ISTA %x", val);
	debugl1(cs, tmp);
	val = cs->BC_Read_Reg(cs, 1, HSCX_STAR);
	sprintf(tmp, "HSCX B STAR %x", val);
	debugl1(cs, tmp);
	val = cs->BC_Read_Reg(cs, 0, HSCX_STAR);
	sprintf(tmp, "HSCX A STAR %x", val);
	debugl1(cs, tmp);
	cs->BC_Write_Reg(cs, 0, HSCX_MASK, 0xFF);
	cs->BC_Write_Reg(cs, 1, HSCX_MASK, 0xFF);
	cs->BC_Write_Reg(cs, 0, HSCX_MASK, 0);
	cs->BC_Write_Reg(cs, 1, HSCX_MASK, 0);
}

HISAX_INITFUNC(void 
inithscx(struct IsdnCardState *cs))
{
	cs->bcs[0].BC_SetStack = setstack_hscx;
	cs->bcs[1].BC_SetStack = setstack_hscx;
	cs->bcs[0].BC_Close = close_hscxstate;
	cs->bcs[1].BC_Close = close_hscxstate;
	modehscx(cs->bcs, 0, 0);
	modehscx(cs->bcs + 1, 0, 0);
}
