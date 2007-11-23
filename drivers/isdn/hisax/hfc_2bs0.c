/* $Id: hfc_2bs0.c,v 1.4 1998/02/12 23:07:29 keil Exp $

 *  specific routines for CCD's HFC 2BS0
 *
 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *
 *
 * $Log: hfc_2bs0.c,v $
 * Revision 1.4  1998/02/12 23:07:29  keil
 * change for 2.1.86 (removing FREE_READ/FREE_WRITE from [dev]_kfree_skb()
 *
 * Revision 1.3  1997/11/06 17:13:35  keil
 * New 2.1 init code
 *
 * Revision 1.2  1997/10/29 19:04:47  keil
 * changes for 2.1
 *
 * Revision 1.1  1997/09/11 17:31:33  keil
 * Common part for HFC 2BS0 based cards
 *
 *
 */

#define __NO_VERSION__
#include "hisax.h"
#include "hfc_2bs0.h"
#include "isac.h"
#include "isdnl1.h"
#include <linux/interrupt.h>

static inline int
WaitForBusy(struct IsdnCardState *cs)
{
	int to = 130;
	long flags;
	u_char val;

	save_flags(flags);
	cli();
	while (!(cs->BC_Read_Reg(cs, HFC_STATUS, 0) & HFC_BUSY) && to) {
		val = cs->BC_Read_Reg(cs, HFC_DATA, HFC_CIP | HFC_F2 |
				      (cs->hw.hfc.cip & 3));
		udelay(1);
		to--;
	}
	restore_flags(flags);
	if (!to) {
		printk(KERN_WARNING "HiSax: waitforBusy timeout\n");
		return (0);
	} else
		return (to);
}

static inline int
WaitNoBusy(struct IsdnCardState *cs)
{
	int to = 125;

	while ((cs->BC_Read_Reg(cs, HFC_STATUS, 0) & HFC_BUSY) && to) {
		udelay(1);
		to--;
	}
	if (!to) {
		printk(KERN_WARNING "HiSax: waitforBusy timeout\n");
		return (0);
	} else
		return (to);
}

int
GetFreeFifoBytes(struct BCState *bcs)
{
	int s;

	if (bcs->hw.hfc.f1 == bcs->hw.hfc.f2)
		return (bcs->cs->hw.hfc.fifosize);
	s = bcs->hw.hfc.send[bcs->hw.hfc.f1] - bcs->hw.hfc.send[bcs->hw.hfc.f2];
	if (s <= 0)
		s += bcs->cs->hw.hfc.fifosize;
	s = bcs->cs->hw.hfc.fifosize - s;
	return (s);
}

int
ReadZReg(struct BCState *bcs, u_char reg)
{
	int val;

	WaitNoBusy(bcs->cs);
	val = 256 * bcs->cs->BC_Read_Reg(bcs->cs, HFC_DATA, reg | HFC_CIP | HFC_Z_HIGH);
	WaitNoBusy(bcs->cs);
	val += bcs->cs->BC_Read_Reg(bcs->cs, HFC_DATA, reg | HFC_CIP | HFC_Z_LOW);
	return (val);
}

void
hfc_sched_event(struct BCState *bcs, int event)
{
	bcs->event |= 1 << event;
	queue_task(&bcs->tqueue, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
}

static void
hfc_clear_fifo(struct BCState *bcs)
{
	struct IsdnCardState *cs = bcs->cs;
	long flags;
	int idx, cnt;
	int rcnt, z1, z2;
	u_char cip, f1, f2;
	char tmp[64];

	if ((cs->debug & L1_DEB_HSCX) && !(cs->debug & L1_DEB_HSCX_FIFO))
		debugl1(cs, "hfc_clear_fifo");
	save_flags(flags);
	cli();
	cip = HFC_CIP | HFC_F1 | HFC_REC | HFC_CHANNEL(bcs->channel);
	if ((cip & 0xc3) != (cs->hw.hfc.cip & 0xc3)) {
		cs->BC_Write_Reg(cs, HFC_STATUS, cip, cip);
		WaitForBusy(cs);
	}
	WaitNoBusy(cs);
	f1 = cs->BC_Read_Reg(cs, HFC_DATA, cip);
	cip = HFC_CIP | HFC_F2 | HFC_REC | HFC_CHANNEL(bcs->channel);
	WaitNoBusy(cs);
	f2 = cs->BC_Read_Reg(cs, HFC_DATA, cip);
	z1 = ReadZReg(bcs, HFC_Z1 | HFC_REC | HFC_CHANNEL(bcs->channel));
	z2 = ReadZReg(bcs, HFC_Z2 | HFC_REC | HFC_CHANNEL(bcs->channel));
	cnt = 32;
	while (((f1 != f2) || (z1 != z2)) && cnt--) {
		if (cs->debug & L1_DEB_HSCX) {
			sprintf(tmp, "hfc clear %d f1(%d) f2(%d)",
				bcs->channel, f1, f2);
			debugl1(cs, tmp);
		}
		rcnt = z1 - z2;
		if (rcnt < 0)
			rcnt += cs->hw.hfc.fifosize;
		if (rcnt)
			rcnt++;
		if (cs->debug & L1_DEB_HSCX) {
			sprintf(tmp, "hfc clear %d z1(%x) z2(%x) cnt(%d)",
				bcs->channel, z1, z2, rcnt);
			debugl1(cs, tmp);
		}
		cip = HFC_CIP | HFC_FIFO_OUT | HFC_REC | HFC_CHANNEL(bcs->channel);
		idx = 0;
		while ((idx < rcnt) && WaitNoBusy(cs)) {
			cs->BC_Read_Reg(cs, HFC_DATA_NODEB, cip);
			idx++;
		}
		if (f1 != f2) {
			WaitNoBusy(cs);
			cs->BC_Read_Reg(cs, HFC_DATA, HFC_CIP | HFC_F2_INC | HFC_REC |
					HFC_CHANNEL(bcs->channel));
			WaitForBusy(cs);
		}
		cip = HFC_CIP | HFC_F1 | HFC_REC | HFC_CHANNEL(bcs->channel);
		WaitNoBusy(cs);
		f1 = cs->BC_Read_Reg(cs, HFC_DATA, cip);
		cip = HFC_CIP | HFC_F2 | HFC_REC | HFC_CHANNEL(bcs->channel);
		WaitNoBusy(cs);
		f2 = cs->BC_Read_Reg(cs, HFC_DATA, cip);
		z1 = ReadZReg(bcs, HFC_Z1 | HFC_REC | HFC_CHANNEL(bcs->channel));
		z2 = ReadZReg(bcs, HFC_Z2 | HFC_REC | HFC_CHANNEL(bcs->channel));
	}
	restore_flags(flags);
	return;
}


static struct sk_buff
*
hfc_empty_fifo(struct BCState *bcs, int count)
{
	u_char *ptr;
	struct sk_buff *skb;
	struct IsdnCardState *cs = bcs->cs;
	int idx;
	int chksum;
	u_char stat, cip;
	char tmp[64];

	if ((cs->debug & L1_DEB_HSCX) && !(cs->debug & L1_DEB_HSCX_FIFO))
		debugl1(cs, "hfc_empty_fifo");
	idx = 0;
	if (count > HSCX_BUFMAX + 3) {
		if (cs->debug & L1_DEB_WARN)
			debugl1(cs, "hfc_empty_fifo: incoming packet too large");
		cip = HFC_CIP | HFC_FIFO_OUT | HFC_REC | HFC_CHANNEL(bcs->channel);
		while ((idx++ < count) && WaitNoBusy(cs))
			cs->BC_Read_Reg(cs, HFC_DATA_NODEB, cip);
		WaitNoBusy(cs);
		stat = cs->BC_Read_Reg(cs, HFC_DATA, HFC_CIP | HFC_F2_INC | HFC_REC |
				       HFC_CHANNEL(bcs->channel));
		WaitForBusy(cs);
		return (NULL);
	}
	if (count < 4) {
		if (cs->debug & L1_DEB_WARN)
			debugl1(cs, "hfc_empty_fifo: incoming packet too small");
		cip = HFC_CIP | HFC_FIFO_OUT | HFC_REC | HFC_CHANNEL(bcs->channel);
		while ((idx++ < count) && WaitNoBusy(cs))
			cs->BC_Read_Reg(cs, HFC_DATA_NODEB, cip);
		WaitNoBusy(cs);
		stat = cs->BC_Read_Reg(cs, HFC_DATA, HFC_CIP | HFC_F2_INC | HFC_REC |
				       HFC_CHANNEL(bcs->channel));
		WaitForBusy(cs);
		return (NULL);
	}
	if (!(skb = dev_alloc_skb(count - 3)))
		printk(KERN_WARNING "HFC: receive out of memory\n");
	else {
		ptr = skb_put(skb, count - 3);
		idx = 0;
		cip = HFC_CIP | HFC_FIFO_OUT | HFC_REC | HFC_CHANNEL(bcs->channel);
		while ((idx < count - 3) && WaitNoBusy(cs)) {
			*ptr++ = cs->BC_Read_Reg(cs, HFC_DATA_NODEB, cip);
			idx++;
		}
		if (idx != count - 3) {
			debugl1(cs, "RFIFO BUSY error");
			printk(KERN_WARNING "HFC FIFO channel %d BUSY Error\n", bcs->channel);
			dev_kfree_skb(skb);
			WaitNoBusy(cs);
			stat = cs->BC_Read_Reg(cs, HFC_DATA, HFC_CIP | HFC_F2_INC | HFC_REC |
					       HFC_CHANNEL(bcs->channel));
			WaitForBusy(cs);
			return (NULL);
		}
		WaitNoBusy(cs);
		chksum = (cs->BC_Read_Reg(cs, HFC_DATA, cip) << 8);
		WaitNoBusy(cs);
		chksum += cs->BC_Read_Reg(cs, HFC_DATA, cip);
		WaitNoBusy(cs);
		stat = cs->BC_Read_Reg(cs, HFC_DATA, cip);
		if (cs->debug & L1_DEB_HSCX) {
			sprintf(tmp, "hfc_empty_fifo %d chksum %x stat %x",
				bcs->channel, chksum, stat);
			debugl1(cs, tmp);
		}
		if (stat) {
			debugl1(cs, "FIFO CRC error");
			dev_kfree_skb(skb);
			skb = NULL;
		}
		WaitNoBusy(cs);
		stat = cs->BC_Read_Reg(cs, HFC_DATA, HFC_CIP | HFC_F2_INC | HFC_REC |
				       HFC_CHANNEL(bcs->channel));
		WaitForBusy(cs);
	}
	return (skb);
}

static void
hfc_fill_fifo(struct BCState *bcs)
{
	struct IsdnCardState *cs = bcs->cs;
	long flags;
	int idx, fcnt;
	int count;
	u_char cip;
	char tmp[64];

	if (!bcs->hw.hfc.tx_skb)
		return;
	if (bcs->hw.hfc.tx_skb->len <= 0)
		return;

	save_flags(flags);
	cli();
	cip = HFC_CIP | HFC_F1 | HFC_SEND | HFC_CHANNEL(bcs->channel);
	if ((cip & 0xc3) != (cs->hw.hfc.cip & 0xc3)) {
		cs->BC_Write_Reg(cs, HFC_STATUS, cip, cip);
		WaitForBusy(cs);
	}
	WaitNoBusy(cs);
	bcs->hw.hfc.f1 = cs->BC_Read_Reg(cs, HFC_DATA, cip);
	cip = HFC_CIP | HFC_F2 | HFC_SEND | HFC_CHANNEL(bcs->channel);
	WaitNoBusy(cs);
	bcs->hw.hfc.f2 = cs->BC_Read_Reg(cs, HFC_DATA, cip);
	bcs->hw.hfc.send[bcs->hw.hfc.f1] = ReadZReg(bcs, HFC_Z1 | HFC_SEND | HFC_CHANNEL(bcs->channel));
	if (cs->debug & L1_DEB_HSCX) {
		sprintf(tmp, "hfc_fill_fifo %d f1(%d) f2(%d) z1(%x)",
			bcs->channel, bcs->hw.hfc.f1, bcs->hw.hfc.f2,
			bcs->hw.hfc.send[bcs->hw.hfc.f1]);
		debugl1(cs, tmp);
	}
	fcnt = bcs->hw.hfc.f1 - bcs->hw.hfc.f2;
	if (fcnt < 0)
		fcnt += 32;
	if (fcnt > 30) {
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "hfc_fill_fifo more as 30 frames");
		restore_flags(flags);
		return;
	}
	count = GetFreeFifoBytes(bcs);
	if (cs->debug & L1_DEB_HSCX) {
		sprintf(tmp, "hfc_fill_fifo %d count(%d/%d)",
			bcs->channel, bcs->hw.hfc.tx_skb->len,
			count);
		debugl1(cs, tmp);
	}
	if (count < bcs->hw.hfc.tx_skb->len) {
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "hfc_fill_fifo no fifo mem");
		restore_flags(flags);
		return;
	}
	cip = HFC_CIP | HFC_FIFO_IN | HFC_SEND | HFC_CHANNEL(bcs->channel);
	idx = 0;
	while ((idx < bcs->hw.hfc.tx_skb->len) && WaitNoBusy(cs))
		cs->BC_Write_Reg(cs, HFC_DATA_NODEB, cip, bcs->hw.hfc.tx_skb->data[idx++]);
	if (idx != bcs->hw.hfc.tx_skb->len) {
		debugl1(cs, "FIFO Send BUSY error");
		printk(KERN_WARNING "HFC S FIFO channel %d BUSY Error\n", bcs->channel);
	} else {
		count =  bcs->hw.hfc.tx_skb->len;
		bcs->tx_cnt -= count;
		if (PACKET_NOACK == bcs->hw.hfc.tx_skb->pkt_type)
			count = -1;
		dev_kfree_skb(bcs->hw.hfc.tx_skb);
		bcs->hw.hfc.tx_skb = NULL;
		WaitForBusy(cs);
		WaitNoBusy(cs);
		cs->BC_Read_Reg(cs, HFC_DATA, HFC_CIP | HFC_F1_INC | HFC_SEND | HFC_CHANNEL(bcs->channel));
		if (bcs->st->lli.l1writewakeup && (count >= 0))
			bcs->st->lli.l1writewakeup(bcs->st, count);
		test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
	}
	restore_flags(flags);
	return;
}

void
main_irq_hfc(struct BCState *bcs)
{
	long flags;
	struct IsdnCardState *cs = bcs->cs;
	int z1, z2, rcnt;
	u_char f1, f2, cip;
	int receive, transmit, count = 5;
	struct sk_buff *skb;
	char tmp[64];

	save_flags(flags);
      Begin:
	cli();
	count--;
	cip = HFC_CIP | HFC_F1 | HFC_REC | HFC_CHANNEL(bcs->channel);
	if ((cip & 0xc3) != (cs->hw.hfc.cip & 0xc3)) {
		cs->BC_Write_Reg(cs, HFC_STATUS, cip, cip);
		WaitForBusy(cs);
	}
	WaitNoBusy(cs);
	f1 = cs->BC_Read_Reg(cs, HFC_DATA, cip);
	cip = HFC_CIP | HFC_F2 | HFC_REC | HFC_CHANNEL(bcs->channel);
	WaitNoBusy(cs);
	f2 = cs->BC_Read_Reg(cs, HFC_DATA, cip);
	if (f1 != f2) {
		if (cs->debug & L1_DEB_HSCX) {
			sprintf(tmp, "hfc rec %d f1(%d) f2(%d)",
				bcs->channel, f1, f2);
			debugl1(cs, tmp);
		}
		WaitForBusy(cs);
		z1 = ReadZReg(bcs, HFC_Z1 | HFC_REC | HFC_CHANNEL(bcs->channel));
		z2 = ReadZReg(bcs, HFC_Z2 | HFC_REC | HFC_CHANNEL(bcs->channel));
		rcnt = z1 - z2;
		if (rcnt < 0)
			rcnt += cs->hw.hfc.fifosize;
		rcnt++;
		if (cs->debug & L1_DEB_HSCX) {
			sprintf(tmp, "hfc rec %d z1(%x) z2(%x) cnt(%d)",
				bcs->channel, z1, z2, rcnt);
			debugl1(cs, tmp);
		}
/*              sti(); */
		if ((skb = hfc_empty_fifo(bcs, rcnt))) {
			skb_queue_tail(&bcs->rqueue, skb);
			hfc_sched_event(bcs, B_RCVBUFREADY);
		}
		receive = 1;
	} else
		receive = 0;
	restore_flags(flags);
	udelay(1);
	cli();
	if (bcs->hw.hfc.tx_skb) {
		transmit = 1;
		test_and_set_bit(BC_FLG_BUSY, &bcs->Flag);
		hfc_fill_fifo(bcs);
		if (test_bit(BC_FLG_BUSY, &bcs->Flag))
			transmit = 0;
	} else {
		if ((bcs->hw.hfc.tx_skb = skb_dequeue(&bcs->squeue))) {
			transmit = 1;
			test_and_set_bit(BC_FLG_BUSY, &bcs->Flag);
			hfc_fill_fifo(bcs);
			if (test_bit(BC_FLG_BUSY, &bcs->Flag))
				transmit = 0;
		} else {
			transmit = 0;
			hfc_sched_event(bcs, B_XMTBUFREADY);
		}
	}
	restore_flags(flags);
	if ((receive || transmit) && count)
		goto Begin;
	return;
}

void
mode_hfc(struct BCState *bcs, int mode, int bc)
{
	struct IsdnCardState *cs = bcs->cs;

	if (cs->debug & L1_DEB_HSCX) {
		char tmp[40];
		sprintf(tmp, "HFC 2BS0 mode %d bchan %d/%d",
			mode, bc, bcs->channel);
		debugl1(cs, tmp);
	}
	bcs->mode = mode;

	switch (mode) {
		case (L1_MODE_NULL):
			if (bc)
				cs->hw.hfc.isac_spcr &= ~0x03;
			else
				cs->hw.hfc.isac_spcr &= ~0x0c;
			break;
		case (L1_MODE_TRANS):
			if (bc) {
				cs->hw.hfc.ctmt |= 1;
				cs->hw.hfc.isac_spcr &= ~0x03;
				cs->hw.hfc.isac_spcr |= 0x02;
			} else {
				cs->hw.hfc.ctmt |= 2;
				cs->hw.hfc.isac_spcr &= ~0x0c;
				cs->hw.hfc.isac_spcr |= 0x08;
			}
			break;
		case (L1_MODE_HDLC):
			if (bc) {
				cs->hw.hfc.ctmt &= ~1;
				cs->hw.hfc.isac_spcr &= ~0x03;
				cs->hw.hfc.isac_spcr |= 0x02;
			} else {
				cs->hw.hfc.ctmt &= ~2;
				cs->hw.hfc.isac_spcr &= ~0x0c;
				cs->hw.hfc.isac_spcr |= 0x08;
			}
			break;
	}
	cs->BC_Write_Reg(cs, HFC_STATUS, cs->hw.hfc.ctmt, cs->hw.hfc.ctmt);
	cs->writeisac(cs, ISAC_SPCR, cs->hw.hfc.isac_spcr);
	if (mode)
		hfc_clear_fifo(bcs);
}

static void
hfc_l2l1(struct PStack *st, int pr, void *arg)
{
	struct sk_buff *skb = arg;
	long flags;

	switch (pr) {
		case (PH_DATA_REQ):
			save_flags(flags);
			cli();
			if (st->l1.bcs->hw.hfc.tx_skb) {
				skb_queue_tail(&st->l1.bcs->squeue, skb);
				restore_flags(flags);
			} else {
				st->l1.bcs->hw.hfc.tx_skb = skb;
				test_and_set_bit(BC_FLG_BUSY, &st->l1.bcs->Flag);
				st->l1.bcs->cs->BC_Send_Data(st->l1.bcs);
				restore_flags(flags);
			}
			break;
		case (PH_PULL_IND):
			if (st->l1.bcs->hw.hfc.tx_skb) {
				printk(KERN_WARNING "hfc_l2l1: this shouldn't happen\n");
				break;
			}
			save_flags(flags);
			cli();
			test_and_set_bit(BC_FLG_BUSY, &st->l1.bcs->Flag);
			st->l1.bcs->hw.hfc.tx_skb = skb;
			st->l1.bcs->cs->BC_Send_Data(st->l1.bcs);
			restore_flags(flags);
			break;
		case (PH_PULL_REQ):
			if (!st->l1.bcs->hw.hfc.tx_skb) {
				test_and_clear_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
				st->l1.l1l2(st, PH_PULL_CNF, NULL);
			} else
				test_and_set_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
			break;
	}
}

void
close_hfcstate(struct BCState *bcs)
{
	struct sk_buff *skb;

	mode_hfc(bcs, 0, 0);
	if (test_bit(BC_FLG_INIT, &bcs->Flag)) {
		while ((skb = skb_dequeue(&bcs->rqueue))) {
			dev_kfree_skb(skb);
		}
		while ((skb = skb_dequeue(&bcs->squeue))) {
			dev_kfree_skb(skb);
		}
		if (bcs->hw.hfc.tx_skb) {
			dev_kfree_skb(bcs->hw.hfc.tx_skb);
			bcs->hw.hfc.tx_skb = NULL;
			test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
		}
	}
	test_and_clear_bit(BC_FLG_INIT, &bcs->Flag);
}

static int
open_hfcstate(struct IsdnCardState *cs,
	      int bc)
{
	struct BCState *bcs = cs->bcs + bc;

	if (!test_and_set_bit(BC_FLG_INIT, &bcs->Flag)) {
		skb_queue_head_init(&bcs->rqueue);
		skb_queue_head_init(&bcs->squeue);
	}
	bcs->hw.hfc.tx_skb = NULL;
	test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
	bcs->event = 0;
	bcs->tx_cnt = 0;
	return (0);
}

static void
hfc_manl1(struct PStack *st, int pr,
	  void *arg)
{
	switch (pr) {
		case (PH_ACTIVATE_REQ):
			test_and_set_bit(BC_FLG_ACTIV, &st->l1.bcs->Flag);
			mode_hfc(st->l1.bcs, st->l1.mode, st->l1.bc);
			st->l1.l1man(st, PH_ACTIVATE_CNF, NULL);
			break;
		case (PH_DEACTIVATE_REQ):
			if (!test_bit(BC_FLG_BUSY, &st->l1.bcs->Flag))
				mode_hfc(st->l1.bcs, 0, 0);
			test_and_clear_bit(BC_FLG_ACTIV, &st->l1.bcs->Flag);
			break;
	}
}

int
setstack_hfc(struct PStack *st, struct BCState *bcs)
{
	if (open_hfcstate(st->l1.hardware, bcs->channel))
		return (-1);
	st->l1.bcs = bcs;
	st->l2.l2l1 = hfc_l2l1;
	st->ma.manl1 = hfc_manl1;
	setstack_manager(st);
	bcs->st = st;
	return (0);
}

__initfunc(void
init_send(struct BCState *bcs))
{
	int i;

	if (!(bcs->hw.hfc.send = kmalloc(32 * sizeof(unsigned int), GFP_ATOMIC))) {
		printk(KERN_WARNING
		       "HiSax: No memory for hfc.send\n");
		return;
	}
	for (i = 0; i < 32; i++)
		bcs->hw.hfc.send[i] = 0x1fff;
}

__initfunc(void
inithfc(struct IsdnCardState *cs))
{
	init_send(&cs->bcs[0]);
	init_send(&cs->bcs[1]);
	cs->BC_Send_Data = &hfc_fill_fifo;
	cs->bcs[0].BC_SetStack = setstack_hfc;
	cs->bcs[1].BC_SetStack = setstack_hfc;
	cs->bcs[0].BC_Close = close_hfcstate;
	cs->bcs[1].BC_Close = close_hfcstate;
	mode_hfc(cs->bcs, 0, 0);
	mode_hfc(cs->bcs + 1, 0, 0);
}

void
releasehfc(struct IsdnCardState *cs)
{
	if (cs->bcs[0].hw.hfc.send) {
		kfree(cs->bcs[0].hw.hfc.send);
		cs->bcs[0].hw.hfc.send = NULL;
	}
	if (cs->bcs[1].hw.hfc.send) {
		kfree(cs->bcs[1].hw.hfc.send);
		cs->bcs[1].hw.hfc.send = NULL;
	}
}
