/* $Id: hfc_2bds0.c,v 1.3 1998/02/12 23:07:22 keil Exp $
 *
 *  specific routines for CCD's HFC 2BDS0
 *
 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *
 *
 * $Log: hfc_2bds0.c,v $
 * Revision 1.3  1998/02/12 23:07:22  keil
 * change for 2.1.86 (removing FREE_READ/FREE_WRITE from [dev]_kfree_skb()
 *
 * Revision 1.2  1998/02/02 13:26:13  keil
 * New
 *
 *
 *
 */
#define __NO_VERSION__
#include "hisax.h"
#include "hfc_2bds0.h"
#include "isdnl1.h"
#include <linux/interrupt.h>
/*
#define KDEBUG_DEF
#include "kdebug.h"
*/

#define byteout(addr,val) outb(val,addr)
#define bytein(addr) inb(addr)

static void
dummyf(struct IsdnCardState *cs, u_char * data, int size)
{
	printk(KERN_WARNING "HiSax: hfcd dummy fifo called\n");
}

static inline u_char
ReadReg(struct IsdnCardState *cs, int data, u_char reg)
{
	register u_char ret;

	if (data) {
		if (cs->hw.hfcD.cip != reg) { 
			cs->hw.hfcD.cip = reg;
			byteout(cs->hw.hfcD.addr | 1, reg);
		}
		ret = bytein(cs->hw.hfcD.addr);
#if HFC_REG_DEBUG
		if (cs->debug & L1_DEB_HSCX_FIFO && (data != 2)) {
			char tmp[32];
			sprintf(tmp, "t3c RD %02x %02x", reg, ret);
			debugl1(cs, tmp);
		}
#endif
	} else
		ret = bytein(cs->hw.hfcD.addr | 1);
	return (ret);
}

static inline void
WriteReg(struct IsdnCardState *cs, int data, u_char reg, u_char value)
{
	if (cs->hw.hfcD.cip != reg) { 
		cs->hw.hfcD.cip = reg;
		byteout(cs->hw.hfcD.addr | 1, reg);
	}
	if (data)
		byteout(cs->hw.hfcD.addr, value);
#if HFC_REG_DEBUG
	if (cs->debug & L1_DEB_HSCX_FIFO && (data != HFCD_DATA_NODEB)) {
		char tmp[16];
		sprintf(tmp, "t3c W%c %02x %02x", data ? 'D' : 'C', reg, value);
		debugl1(cs, tmp);
	}
#endif
}

/* Interface functions */

static u_char
readreghfcd(struct IsdnCardState *cs, u_char offset)
{
	return(ReadReg(cs, HFCD_DATA, offset));
}

static void
writereghfcd(struct IsdnCardState *cs, u_char offset, u_char value)
{
	WriteReg(cs, HFCD_DATA, offset, value);
}

void
set_cs_func(struct IsdnCardState *cs)
{
	cs->readisac = &readreghfcd;
	cs->writeisac = &writereghfcd;
	cs->readisacfifo = &dummyf;
	cs->writeisacfifo = &dummyf;
	cs->BC_Read_Reg = &ReadReg;
	cs->BC_Write_Reg = &WriteReg;
}

static inline int
WaitForBusy(struct IsdnCardState *cs)
{
	int to = 130;

	while (!(ReadReg(cs, HFCD_DATA, HFCD_STAT) & HFCD_BUSY) && to) {
		udelay(1);
		to--;
	}
	if (!to)
		printk(KERN_WARNING "HiSax: WaitForBusy timeout\n");
	return (to);
}

static inline int
WaitNoBusy(struct IsdnCardState *cs)
{
	long flags;
	int to = 130;

	while ((ReadReg(cs, HFCD_STATUS, HFCD_STATUS) & HFCD_BUSY) && to) {
		save_flags(flags);
		sti();
		udelay(1);
		to--;
		restore_flags(flags);
	}
	if (!to) 
		printk(KERN_WARNING "HiSax: WaitNoBusy timeout\n");
	return (to);
}

static int
SelFiFo(struct IsdnCardState *cs, u_char FiFo)
{
	u_char cip;
	long flags;


	if (cs->hw.hfcD.fifo == FiFo)
		return(1);
	save_flags(flags);
	cli();
	switch(FiFo) {
		case 0: cip = HFCB_FIFO | HFCB_Z1 | HFCB_SEND | HFCB_B1;
			break;
		case 1: cip = HFCB_FIFO | HFCB_Z1 | HFCB_REC | HFCB_B1;
			break;
		case 2: cip = HFCB_FIFO | HFCB_Z1 | HFCB_SEND | HFCB_B2;
			break;
		case 3: cip = HFCB_FIFO | HFCB_Z1 | HFCB_REC | HFCB_B2;
			break;
		case 4: cip = HFCD_FIFO | HFCD_Z1 | HFCD_SEND;
			break;
		case 5: cip = HFCD_FIFO | HFCD_Z1 | HFCD_REC;
			break;
		default:
			restore_flags(flags);
			debugl1(cs, "SelFiFo Error");
			return(0);
	}
	cs->hw.hfcD.fifo = FiFo;
	WaitNoBusy(cs);
	cs->BC_Write_Reg(cs, HFCD_DATA, cip, 0);
	sti();
	WaitForBusy(cs);
	restore_flags(flags);
	return(2);
}
static int
GetFreeFifoBytes_B(struct BCState *bcs)
{
	int s;

	if (bcs->hw.hfc.f1 == bcs->hw.hfc.f2)
		return (bcs->cs->hw.hfcD.bfifosize);
	s = bcs->hw.hfc.send[bcs->hw.hfc.f1] - bcs->hw.hfc.send[bcs->hw.hfc.f2];
	if (s <= 0)
		s += bcs->cs->hw.hfcD.bfifosize;
	s = bcs->cs->hw.hfcD.bfifosize - s;
	return (s);
}

static int
GetFreeFifoBytes_D(struct IsdnCardState *cs)
{
	int s;

	if (cs->hw.hfcD.f1 == cs->hw.hfcD.f2)
		return (cs->hw.hfcD.dfifosize);
	s = cs->hw.hfcD.send[cs->hw.hfcD.f1] - cs->hw.hfcD.send[cs->hw.hfcD.f2];
	if (s <= 0)
		s += cs->hw.hfcD.dfifosize;
	s = cs->hw.hfcD.dfifosize - s;
	return (s);
}

static int
ReadZReg(struct IsdnCardState *cs, u_char reg)
{
	int val;

	WaitNoBusy(cs);
	val = 256 * ReadReg(cs, HFCD_DATA, reg | HFCB_Z_HIGH);
	WaitNoBusy(cs);
	val += ReadReg(cs, HFCD_DATA, reg | HFCB_Z_LOW);
	return (val);
}

static void
hfc_sched_event(struct BCState *bcs, int event)
{
	bcs->event |= 1 << event;
	queue_task(&bcs->tqueue, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
}

static struct sk_buff
*hfc_empty_fifo(struct BCState *bcs, int count)
{
	u_char *ptr;
	struct sk_buff *skb;
	struct IsdnCardState *cs = bcs->cs;
	int idx;
	int chksum;
	long flags;
	u_char stat, cip;
	char tmp[64];
	
	if ((cs->debug & L1_DEB_HSCX) && !(cs->debug & L1_DEB_HSCX_FIFO))
		debugl1(cs, "hfc_empty_fifo");
	idx = 0;
	save_flags(flags);
	if (count > HSCX_BUFMAX + 3) {
		if (cs->debug & L1_DEB_WARN)
			debugl1(cs, "hfc_empty_fifo: incoming packet too large");
		cip = HFCB_FIFO | HFCB_FIFO_OUT | HFCB_REC | HFCB_CHANNEL(bcs->channel);
		while (idx++ < count) {
			cli();
			WaitNoBusy(cs);
			ReadReg(cs, HFCD_DATA_NODEB, cip);
			sti();
		}
		skb = NULL;
	} else if (count < 4) {
		if (cs->debug & L1_DEB_WARN)
			debugl1(cs, "hfc_empty_fifo: incoming packet too small");
		cip = HFCB_FIFO | HFCB_FIFO_OUT | HFCB_REC | HFCB_CHANNEL(bcs->channel);
		cli();
		while ((idx++ < count) && WaitNoBusy(cs))
			ReadReg(cs, HFCD_DATA_NODEB, cip);
		skb = NULL;
	} else if (!(skb = dev_alloc_skb(count - 3)))
		printk(KERN_WARNING "HFC: receive out of memory\n");
	else {
		ptr = skb_put(skb, count - 3);
		idx = 0;
		cip = HFCB_FIFO | HFCB_FIFO_OUT | HFCB_REC | HFCB_CHANNEL(bcs->channel);
		cli();
		while (idx < (count - 3)) {
			cli();
			if (!WaitNoBusy(cs))
				break;
			*ptr = ReadReg(cs,  HFCD_DATA_NODEB, cip);
			sti();
			ptr++;
			idx++;
		}
		if (idx != count - 3) {
			sti();
			debugl1(cs, "RFIFO BUSY error");
			printk(KERN_WARNING "HFC FIFO channel %d BUSY Error\n", bcs->channel);
			dev_kfree_skb(skb);
			skb = NULL;
		} else {
			cli();
			WaitNoBusy(cs);
			chksum = (ReadReg(cs, HFCD_DATA, cip) << 8);
			WaitNoBusy(cs);
			chksum += ReadReg(cs, HFCD_DATA, cip);
			WaitNoBusy(cs);
			stat = ReadReg(cs, HFCD_DATA, cip);
			sti();
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
		}
	}
	sti();
	WaitForBusy(cs);
	cli();
	WaitNoBusy(cs);
	stat = ReadReg(cs, HFCD_DATA, HFCB_FIFO | HFCB_F2_INC |
		HFCB_REC | HFCB_CHANNEL(bcs->channel));
	sti();
	WaitForBusy(cs);
	restore_flags(flags);
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
	SelFiFo(cs, HFCB_SEND | HFCB_CHANNEL(bcs->channel)); 
	cip = HFCB_FIFO | HFCB_F1 | HFCB_SEND | HFCB_CHANNEL(bcs->channel);
	WaitNoBusy(cs);
	bcs->hw.hfc.f1 = ReadReg(cs, HFCD_DATA, cip);
	WaitNoBusy(cs);
	cip = HFCB_FIFO | HFCB_F2 | HFCB_SEND | HFCB_CHANNEL(bcs->channel);
	WaitNoBusy(cs);
	bcs->hw.hfc.f2 = ReadReg(cs, HFCD_DATA, cip);
	bcs->hw.hfc.send[bcs->hw.hfc.f1] = ReadZReg(cs, HFCB_FIFO | HFCB_Z1 | HFCB_SEND | HFCB_CHANNEL(bcs->channel));
	sti();
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
	count = GetFreeFifoBytes_B(bcs);
	if (cs->debug & L1_DEB_HSCX) {
		sprintf(tmp, "hfc_fill_fifo %d count(%d/%d),%lx",
			bcs->channel, bcs->hw.hfc.tx_skb->len,
			count, current->state);
		debugl1(cs, tmp);
	}
	if (count < bcs->hw.hfc.tx_skb->len) {
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "hfc_fill_fifo no fifo mem");
		restore_flags(flags);
		return;
	}
	cip = HFCB_FIFO | HFCB_FIFO_IN | HFCB_SEND | HFCB_CHANNEL(bcs->channel);
	idx = 0;
	cli();
	WaitForBusy(cs);
	WaitNoBusy(cs);
	WriteReg(cs, HFCD_DATA_NODEB, cip, bcs->hw.hfc.tx_skb->data[idx++]);
	while (idx < bcs->hw.hfc.tx_skb->len) {
		cli();
		if (!WaitNoBusy(cs))
			break;
		WriteReg(cs, HFCD_DATA_NODEB, cip, bcs->hw.hfc.tx_skb->data[idx]);
		sti();
		idx++;
	}
	if (idx != bcs->hw.hfc.tx_skb->len) {
		sti();
		debugl1(cs, "FIFO Send BUSY error");
		printk(KERN_WARNING "HFC S FIFO channel %d BUSY Error\n", bcs->channel);
	} else {
		bcs->tx_cnt -= bcs->hw.hfc.tx_skb->len;
		if (bcs->st->lli.l1writewakeup &&
			(PACKET_NOACK != bcs->hw.hfc.tx_skb->pkt_type))
			bcs->st->lli.l1writewakeup(bcs->st, bcs->hw.hfc.tx_skb->len);
		dev_kfree_skb(bcs->hw.hfc.tx_skb);
		bcs->hw.hfc.tx_skb = NULL;
	}
	WaitForBusy(cs);
	cli();
	WaitNoBusy(cs);
	ReadReg(cs, HFCD_DATA, HFCB_FIFO | HFCB_F1_INC | HFCB_SEND | HFCB_CHANNEL(bcs->channel));
	sti();
	WaitForBusy(cs);
	test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
	restore_flags(flags);
	return;
}

static void
hfc_send_data(struct BCState *bcs)
{
	struct IsdnCardState *cs = bcs->cs;
	char tmp[32];
	
	if (!test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
		hfc_fill_fifo(bcs);
		test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
	} else {
		sprintf(tmp,"send_data %d blocked", bcs->channel);
		debugl1(cs, tmp);
	}
}

void
main_rec_2bds0(struct BCState *bcs)
{
	long flags;
	struct IsdnCardState *cs = bcs->cs;
	int z1, z2, rcnt;
	u_char f1, f2, cip;
	int receive, count = 5;
	struct sk_buff *skb;
	char tmp[64];

	save_flags(flags);
    Begin:
	count--;
	cli();
	if (test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
		sprintf(tmp,"rec_data %d blocked", bcs->channel);
		debugl1(cs, tmp);
		restore_flags(flags);
		return;
	}
	SelFiFo(cs, HFCB_REC | HFCB_CHANNEL(bcs->channel));
	cip = HFCB_FIFO | HFCB_F1 | HFCB_REC | HFCB_CHANNEL(bcs->channel);
	WaitNoBusy(cs);
	f1 = ReadReg(cs, HFCD_DATA, cip);
	cip = HFCB_FIFO | HFCB_F2 | HFCB_REC | HFCB_CHANNEL(bcs->channel);
	WaitNoBusy(cs);
	f2 = ReadReg(cs, HFCD_DATA, cip);
	sti();
	if (f1 != f2) {
		if (cs->debug & L1_DEB_HSCX) {
			sprintf(tmp, "hfc rec %d f1(%d) f2(%d)",
				bcs->channel, f1, f2);
			debugl1(cs, tmp);
		}
		cli();
		z1 = ReadZReg(cs, HFCB_FIFO | HFCB_Z1 | HFCB_REC | HFCB_CHANNEL(bcs->channel));
		z2 = ReadZReg(cs, HFCB_FIFO | HFCB_Z2 | HFCB_REC | HFCB_CHANNEL(bcs->channel));
		sti();
		rcnt = z1 - z2;
		if (rcnt < 0)
			rcnt += cs->hw.hfcD.bfifosize;
		rcnt++;
		if (cs->debug & L1_DEB_HSCX) {
			sprintf(tmp, "hfc rec %d z1(%x) z2(%x) cnt(%d)",
				bcs->channel, z1, z2, rcnt);
			debugl1(cs, tmp);
		}
		if ((skb = hfc_empty_fifo(bcs, rcnt))) {
			cli();
			skb_queue_tail(&bcs->rqueue, skb);
			sti();
			hfc_sched_event(bcs, B_RCVBUFREADY);
		}
		rcnt = f1 -f2;
		if (rcnt<0)
			rcnt += 32;
		if (rcnt>1)
			receive = 1;
		else
			receive = 0;
	} else
		receive = 0;
	test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
	if (count && receive)
		goto Begin;	
	restore_flags(flags);
	return;
}

void
mode_2bs0(struct BCState *bcs, int mode, int bc)
{
	struct IsdnCardState *cs = bcs->cs;

	if (cs->debug & L1_DEB_HSCX) {
		char tmp[40];
		sprintf(tmp, "HFCD bchannel mode %d bchan %d/%d",
			mode, bc, bcs->channel);
		debugl1(cs, tmp);
	}
	bcs->mode = mode;
	bcs->channel = bc;
	switch (mode) {
		case (L1_MODE_NULL):
			if (bc) {
				cs->hw.hfcD.conn |= 0x18;
				cs->hw.hfcD.sctrl &= ~SCTRL_B2_ENA;
			} else {
				cs->hw.hfcD.conn |= 0x3;
				cs->hw.hfcD.sctrl &= ~SCTRL_B1_ENA;
			}
			break;
		case (L1_MODE_TRANS):
			if (bc) {
				cs->hw.hfcD.ctmt |= 2;
				cs->hw.hfcD.conn &= ~0x18;
				cs->hw.hfcD.sctrl |= SCTRL_B2_ENA;
			} else {
				cs->hw.hfcD.ctmt |= 1;
				cs->hw.hfcD.conn &= ~0x3;
				cs->hw.hfcD.sctrl |= SCTRL_B1_ENA;
			}
			break;
		case (L1_MODE_HDLC):
			if (bc) {
				cs->hw.hfcD.ctmt &= ~2;
				cs->hw.hfcD.conn &= ~0x18;
				cs->hw.hfcD.sctrl |= SCTRL_B2_ENA;
			} else {
				cs->hw.hfcD.ctmt &= ~1;
				cs->hw.hfcD.conn &= ~0x3;
				cs->hw.hfcD.sctrl |= SCTRL_B1_ENA;
			}
			break;
	}
	WriteReg(cs, HFCD_DATA, HFCD_SCTRL, cs->hw.hfcD.sctrl);
	WriteReg(cs, HFCD_DATA, HFCD_CTMT, cs->hw.hfcD.ctmt);
	WriteReg(cs, HFCD_DATA, HFCD_CONN, cs->hw.hfcD.conn);
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
/*				test_and_set_bit(BC_FLG_BUSY, &st->l1.bcs->Flag);
*/				st->l1.bcs->cs->BC_Send_Data(st->l1.bcs);
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
/*			test_and_set_bit(BC_FLG_BUSY, &st->l1.bcs->Flag);
*/			st->l1.bcs->hw.hfc.tx_skb = skb;
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
close_2bs0(struct BCState *bcs)
{
	struct sk_buff *skb;

	mode_2bs0(bcs, 0, 0);
	if (test_and_clear_bit(BC_FLG_INIT, &bcs->Flag)) {
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
			mode_2bs0(st->l1.bcs, st->l1.mode, st->l1.bc);
			st->l1.l1man(st, PH_ACTIVATE_CNF, NULL);
			break;
		case (PH_DEACTIVATE_REQ):
			if (!test_bit(BC_FLG_BUSY, &st->l1.bcs->Flag))
				mode_2bs0(st->l1.bcs, 0, 0);
			test_and_clear_bit(BC_FLG_ACTIV, &st->l1.bcs->Flag);
			break;
	}
}

int
setstack_2b(struct PStack *st, struct BCState *bcs)
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
hfcd_bh(struct IsdnCardState *cs)
{
/*	struct PStack *stptr;
*/
	if (!cs)
		return;
#if 0	
	if (test_and_clear_bit(D_CLEARBUSY, &cs->event)) {
		if (cs->debug)
			debugl1(cs, "D-Channel Busy cleared");
		stptr = cs->stlist;
		while (stptr != NULL) {
			stptr->l1.l1l2(stptr, PH_PAUSE_CNF, NULL);
			stptr = stptr->next;
		}
	}
#endif
	if (test_and_clear_bit(D_L1STATECHANGE, &cs->event)) {
		switch (cs->ph_state) {
			case (0):
				manl1_msg(cs, PH_RESET_IND, NULL);
				break;
			case (3):
				manl1_msg(cs, PH_DEACT_IND, NULL);
				break;
			case (8):
				manl1_msg(cs, PH_RSYNC_IND, NULL);
				break;
			case (6):
				manl1_msg(cs, PH_INFO2_IND, NULL);
				break;
			case (7):
				manl1_msg(cs, PH_I4_P8_IND, NULL);
				break;
			default:
				break;
		}
	}
	if (test_and_clear_bit(D_RCVBUFREADY, &cs->event))
		DChannel_proc_rcv(cs);
	if (test_and_clear_bit(D_XMTBUFREADY, &cs->event))
		DChannel_proc_xmt(cs);
}

void
sched_event_D(struct IsdnCardState *cs, int event)
{
	test_and_set_bit(event, &cs->event);
	queue_task(&cs->tqueue, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
}

static
int receive_dmsg(struct IsdnCardState *cs)
{
	struct sk_buff *skb;
	long flags;
	int idx;
	int rcnt, z1, z2;
	u_char stat, cip, f1, f2;
	int chksum;
	int count=5;
	u_char *ptr;
	char tmp[64];

	save_flags(flags);
	cli();
	if (test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
		debugl1(cs, "rec_dmsg blocked");
		restore_flags(flags);
		return(1);
	}
	SelFiFo(cs, 4 | HFCD_REC);
	cip = HFCD_FIFO | HFCD_F1 | HFCD_REC;
	WaitNoBusy(cs);
	f1 = cs->readisac(cs, cip) & 0xf;
	cip = HFCD_FIFO | HFCD_F2 | HFCD_REC;
	WaitNoBusy(cs);
	f2 = cs->readisac(cs, cip) & 0xf;
	while ((f1 != f2) && count--) {
		z1 = ReadZReg(cs, HFCD_FIFO | HFCD_Z1 | HFCD_REC);
		z2 = ReadZReg(cs, HFCD_FIFO | HFCD_Z2 | HFCD_REC);
		rcnt = z1 - z2;
		if (rcnt < 0)
			rcnt += cs->hw.hfcD.dfifosize;
		rcnt++;
		if (cs->debug & L1_DEB_ISAC) {
			sprintf(tmp, "hfcd recd f1(%d) f2(%d) z1(%x) z2(%x) cnt(%d)",
				f1, f2, z1, z2, rcnt);
			debugl1(cs, tmp);
		}
		sti();
		idx = 0;
		cip = HFCD_FIFO | HFCD_FIFO_OUT | HFCD_REC;
		if (rcnt > MAX_DFRAME_LEN + 3) {
			if (cs->debug & L1_DEB_WARN)
				debugl1(cs, "empty_fifo d: incoming packet too large");
			while (idx < rcnt) {
				cli();
				if (!(WaitNoBusy(cs)))
					break;
				ReadReg(cs, HFCD_DATA_NODEB, cip);
				sti();
				idx++;
			}
		} else if (rcnt < 4) {
			if (cs->debug & L1_DEB_WARN)
				debugl1(cs, "empty_fifo d: incoming packet too small");
			cli();
			while ((idx++ < rcnt) && WaitNoBusy(cs))
				ReadReg(cs, HFCD_DATA_NODEB, cip);
		} else if ((skb = dev_alloc_skb(rcnt - 3))) {
			ptr = skb_put(skb, rcnt - 3);
			while (idx < (rcnt - 3)) {
				cli();
				if (!(WaitNoBusy(cs)))
					break;
				*ptr = ReadReg(cs, HFCD_DATA_NODEB, cip);
				sti();
				idx++;
				ptr++;
			}
			if (idx != (rcnt - 3)) {
				sti();
				debugl1(cs, "RFIFO D BUSY error");
				printk(KERN_WARNING "HFC DFIFO channel BUSY Error\n");
				dev_kfree_skb(skb);
				skb = NULL;
			} else {
				cli();
				WaitNoBusy(cs);
				chksum = (ReadReg(cs, HFCD_DATA, cip) << 8);
				WaitNoBusy(cs);
				chksum += ReadReg(cs, HFCD_DATA, cip);
				WaitNoBusy(cs);
				stat = ReadReg(cs, HFCD_DATA, cip);
				sti();
				if (cs->debug & L1_DEB_ISAC) {
					sprintf(tmp, "empty_dfifo chksum %x stat %x",
						chksum, stat);
					debugl1(cs, tmp);
				}
				if (stat) {
					debugl1(cs, "FIFO CRC error");
					dev_kfree_skb(skb);
					skb = NULL;
				} else {
					skb_queue_tail(&cs->rq, skb);
					sched_event_D(cs, D_RCVBUFREADY);
				}
			}
		} else
			printk(KERN_WARNING "HFC: D receive out of memory\n");
		sti();
		WaitForBusy(cs);
		cip = HFCD_FIFO | HFCD_F2_INC | HFCD_REC;
		cli();
		WaitNoBusy(cs);
		stat = ReadReg(cs, HFCD_DATA, cip);
		sti();
		WaitForBusy(cs);
		cip = HFCD_FIFO | HFCD_F2 | HFCD_REC;
		cli();
		WaitNoBusy(cs);
		f2 = cs->readisac(cs, cip) & 0xf;
	}
	test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
	restore_flags(flags);
	return(1);
} 

static void
hfc_fill_dfifo(struct IsdnCardState *cs)
{
	long flags;
	int idx, fcnt;
	int count;
	u_char cip;
	char tmp[64];

	if (!cs->tx_skb)
		return;
	if (cs->tx_skb->len <= 0)
		return;

	save_flags(flags);
	cli();
	SelFiFo(cs, 4 | HFCD_SEND);
	cip = HFCD_FIFO | HFCD_F1 | HFCD_SEND;
	WaitNoBusy(cs);
	cs->hw.hfcD.f1 = ReadReg(cs, HFCD_DATA, cip) & 0xf;
	WaitNoBusy(cs);
	cip = HFCD_FIFO | HFCD_F2 | HFCD_SEND;
	cs->hw.hfcD.f2 = ReadReg(cs, HFCD_DATA, cip) & 0xf;
	cs->hw.hfcD.send[cs->hw.hfcD.f1] = ReadZReg(cs, HFCD_FIFO | HFCD_Z1 | HFCD_SEND);
	sti();
	if (cs->debug & L1_DEB_ISAC) {
		sprintf(tmp, "hfc_fill_Dfifo f1(%d) f2(%d) z1(%x)",
			cs->hw.hfcD.f1, cs->hw.hfcD.f2,
			cs->hw.hfcD.send[cs->hw.hfcD.f1]);
		debugl1(cs, tmp);
	}
	fcnt = cs->hw.hfcD.f1 - cs->hw.hfcD.f2;
	if (fcnt < 0)
		fcnt += 16;
	if (fcnt > 14) {
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "hfc_fill_Dfifo more as 14 frames");
		restore_flags(flags);
		return;
	}
	count = GetFreeFifoBytes_D(cs);
	if (cs->debug & L1_DEB_ISAC) {
		sprintf(tmp, "hfc_fill_Dfifo count(%d/%d)",
			cs->tx_skb->len, count);
		debugl1(cs, tmp);
	}
	if (count < cs->tx_skb->len) {
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "hfc_fill_Dfifo no fifo mem");
		restore_flags(flags);
		return;
	}
	cip = HFCD_FIFO | HFCD_FIFO_IN | HFCD_SEND;
	idx = 0;
	cli();
	WaitForBusy(cs);
	WaitNoBusy(cs);
	WriteReg(cs, HFCD_DATA_NODEB, cip, cs->tx_skb->data[idx++]);
	while (idx < cs->tx_skb->len) {
		cli();
		if (!(WaitNoBusy(cs)))
			break;
		WriteReg(cs, HFCD_DATA_NODEB, cip, cs->tx_skb->data[idx]);
		sti();
		idx++;
	}
	if (idx != cs->tx_skb->len) {
		sti();
		debugl1(cs, "DFIFO Send BUSY error");
		printk(KERN_WARNING "HFC S DFIFO channel BUSY Error\n");
	}
	WaitForBusy(cs);
	cli();
	WaitNoBusy(cs);
	ReadReg(cs, HFCD_DATA, HFCD_FIFO | HFCD_F1_INC | HFCD_SEND);
	dev_kfree_skb(cs->tx_skb);
	cs->tx_skb = NULL;
	sti();
	WaitForBusy(cs);
	restore_flags(flags);
	return;
}

static 
struct BCState *Sel_BCS(struct IsdnCardState *cs, int channel)
{
	if (cs->bcs[0].mode && (cs->bcs[0].channel == channel))
		return(&cs->bcs[0]);
	else if (cs->bcs[1].mode && (cs->bcs[1].channel == channel))
		return(&cs->bcs[1]);
	else
		return(NULL);
}

void
hfc2bds0_interrupt(struct IsdnCardState *cs, u_char val)
{
       	u_char exval;
       	struct BCState *bcs;
	char tmp[32];
	int count=15;
	long flags;

	if (cs->debug & L1_DEB_ISAC) {
		sprintf(tmp, "HFCD irq %x %s", val,
			test_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags) ?
			"locked" : "unlocked");
		debugl1(cs, tmp);
	}
	val &= cs->hw.hfcD.int_m1;
	if (val & 0x40) { /* TE state machine irq */
		exval = cs->readisac(cs, HFCD_STATES) & 0xf;
		if (cs->debug & L1_DEB_ISAC) {
			sprintf(tmp, "ph_state chg %d->%d", cs->ph_state,
				exval);
			debugl1(cs, tmp);
		}
		cs->ph_state = exval;
		sched_event_D(cs, D_L1STATECHANGE);
		val &= ~0x40;
	}
	while (val) {
		save_flags(flags);
		cli();
		if (test_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
			cs->hw.hfcD.int_s1 |= val;
			restore_flags(flags);
			return;
		}
		if (cs->hw.hfcD.int_s1 & 0x18) {
			exval = val;
			val =  cs->hw.hfcD.int_s1;
			cs->hw.hfcD.int_s1 = exval;
		}	
		if (val & 0x08) {
			if (!(bcs=Sel_BCS(cs, 0))) {
				if (cs->debug)
					debugl1(cs, "hfcd spurious 0x08 IRQ");
			} else 
				main_rec_2bds0(bcs);
		}
		if (val & 0x10) {
			if (!(bcs=Sel_BCS(cs, 1))) {
				if (cs->debug)
					debugl1(cs, "hfcd spurious 0x10 IRQ");
			} else 
				main_rec_2bds0(bcs);
		}
		if (val & 0x01) {
			if (!(bcs=Sel_BCS(cs, 0))) {
				if (cs->debug)
					debugl1(cs, "hfcd spurious 0x01 IRQ");
			} else {
				if (bcs->hw.hfc.tx_skb) {
					if (!test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
						hfc_fill_fifo(bcs);
						test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
					} else {
						sprintf(tmp,"fill_data %d blocked", bcs->channel);
						debugl1(cs, tmp);
					}
				} else {
					if ((bcs->hw.hfc.tx_skb = skb_dequeue(&bcs->squeue))) {
						if (!test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
							hfc_fill_fifo(bcs);
							test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
						} else {
							sprintf(tmp,"fill_data %d blocked", bcs->channel);
							debugl1(cs, tmp);
						}
					} else {
						hfc_sched_event(bcs, B_XMTBUFREADY);
					}
				}
			}
		}
		if (val & 0x02) {
			if (!(bcs=Sel_BCS(cs, 1))) {
				if (cs->debug)
					debugl1(cs, "hfcd spurious 0x02 IRQ");
			} else {
				if (bcs->hw.hfc.tx_skb) {
					if (!test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
						hfc_fill_fifo(bcs);
						test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
					} else {
						sprintf(tmp,"fill_data %d blocked", bcs->channel);
						debugl1(cs, tmp);
					}
				} else {
					if ((bcs->hw.hfc.tx_skb = skb_dequeue(&bcs->squeue))) {
						if (!test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
							hfc_fill_fifo(bcs);
							test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
						} else {
							sprintf(tmp,"fill_data %d blocked", bcs->channel);
							debugl1(cs, tmp);
						}
					} else {
						hfc_sched_event(bcs, B_XMTBUFREADY);
					}
				}
			}
		}
		if (val & 0x20) {	/* receive dframe */
			receive_dmsg(cs);
		}
		if (val & 0x04) {	/* dframe transmitted */
			if (test_and_clear_bit(FLG_DBUSY_TIMER, &cs->HW_Flags))
				del_timer(&cs->dbusytimer);
			if (test_and_clear_bit(FLG_L1_DBUSY, &cs->HW_Flags))
				sched_event_D(cs, D_CLEARBUSY);
			if (cs->tx_skb) {
				if (cs->tx_skb->len) {
					if (!test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
						hfc_fill_dfifo(cs);
						test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
					} else {
						debugl1(cs, "hfc_fill_dfifo irq blocked");
					}
					goto afterXPR;
				} else {
					dev_kfree_skb(cs->tx_skb);
					cs->tx_cnt = 0;
					cs->tx_skb = NULL;
				}
			}
			if ((cs->tx_skb = skb_dequeue(&cs->sq))) {
				cs->tx_cnt = 0;
				if (!test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
					hfc_fill_dfifo(cs);
					test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
				} else {
					debugl1(cs, "hfc_fill_dfifo irq blocked");
				}
			} else
				sched_event_D(cs, D_XMTBUFREADY);
		}
      afterXPR:
		if (cs->hw.hfcD.int_s1 && count--) {
			val = cs->hw.hfcD.int_s1;
			cs->hw.hfcD.int_s1 = 0;
			if (cs->debug & L1_DEB_ISAC) {
				sprintf(tmp, "HFCD irq %x loop %d", val, 15-count);
				debugl1(cs, tmp);
			}
		} else
			val = 0;
		restore_flags(flags);
	}
}

static void
HFCD_l2l1(struct PStack *st, int pr, void *arg)
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
				if (!test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
					hfc_fill_dfifo(cs);
					test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
				} else
					debugl1(cs, "hfc_fill_dfifo blocked");

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
			if (!test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
				hfc_fill_dfifo(cs);
				test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
			} else
				debugl1(cs, "hfc_fill_dfifo blocked");
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
hfcd_l1cmd(struct IsdnCardState *cs, int msg, void *arg)
{
	char tmp[32];
	switch(msg) {
		case PH_RESET_REQ:
			cs->writeisac(cs, HFCD_STATES, HFCD_LOAD_STATE | 3); /* HFC ST 3 */
			udelay(6);
			cs->writeisac(cs, HFCD_STATES, 3); /* HFC ST 2 */
			cs->hw.hfcD.mst_m |= HFCD_MASTER;
			cs->writeisac(cs, HFCD_MST_MODE, cs->hw.hfcD.mst_m);
			cs->writeisac(cs, HFCD_STATES, HFCD_ACTIVATE | HFCD_DO_ACTION);
			manl1_msg(cs, PH_POWERUP_CNF, NULL);
			break;
		case PH_ENABLE_REQ:
			cs->writeisac(cs, HFCD_STATES, HFCD_ACTIVATE | HFCD_DO_ACTION);
			break;
		case PH_DEACT_ACK:
			cs->hw.hfcD.mst_m &= ~HFCD_MASTER;
			cs->writeisac(cs, HFCD_MST_MODE, cs->hw.hfcD.mst_m);
			break;
		case PH_INFO3_REQ:
			cs->hw.hfcD.mst_m |= HFCD_MASTER;
			cs->writeisac(cs, HFCD_MST_MODE, cs->hw.hfcD.mst_m);
			break;
#if 0
		case PH_TESTLOOP_REQ:
			u_char val = 0;
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
#endif
		default:
			if (cs->debug & L1_DEB_WARN) {
				sprintf(tmp, "hfcd_l1cmd unknown %4x", msg);
				debugl1(cs, tmp);
			}
			break;
	}
}

void
setstack_hfcd(struct PStack *st, struct IsdnCardState *cs)
{
	st->l2.l2l1 = HFCD_l2l1;
}

static void
hfc_dbusy_timer(struct IsdnCardState *cs)
{
#if 0
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
#endif
}

__initfunc(unsigned int
*init_send_hfcd(int cnt))
{
	int i, *send;

	if (!(send = kmalloc(cnt * sizeof(unsigned int), GFP_ATOMIC))) {
		printk(KERN_WARNING
		       "HiSax: No memory for hfcd.send\n");
		return(NULL);
	}
	for (i = 0; i < cnt; i++)
		send[i] = 0x1fff;
	return(send);
}

__initfunc(void
init2bds0(struct IsdnCardState *cs))
{
	cs->setstack_d = setstack_hfcd;
	cs->l1cmd = hfcd_l1cmd;
	cs->dbusytimer.function = (void *) hfc_dbusy_timer;
	cs->dbusytimer.data = (long) cs;
	init_timer(&cs->dbusytimer);
	cs->tqueue.routine = (void *) (void *) hfcd_bh;
	if (!cs->hw.hfcD.send)
		cs->hw.hfcD.send = init_send_hfcd(16);
	if (!cs->bcs[0].hw.hfc.send)
		cs->bcs[0].hw.hfc.send = init_send_hfcd(32);
	if (!cs->bcs[1].hw.hfc.send)
		cs->bcs[1].hw.hfc.send = init_send_hfcd(32);
	cs->BC_Send_Data = &hfc_send_data;
	cs->bcs[0].BC_SetStack = setstack_2b;
	cs->bcs[1].BC_SetStack = setstack_2b;
	cs->bcs[0].BC_Close = close_2bs0;
	cs->bcs[1].BC_Close = close_2bs0;
	mode_2bs0(cs->bcs, 0, 0);
	mode_2bs0(cs->bcs + 1, 0, 1);
}

void
release2bds0(struct IsdnCardState *cs)
{
	if (cs->bcs[0].hw.hfc.send) {
		kfree(cs->bcs[0].hw.hfc.send);
		cs->bcs[0].hw.hfc.send = NULL;
	}
	if (cs->bcs[1].hw.hfc.send) {
		kfree(cs->bcs[1].hw.hfc.send);
		cs->bcs[1].hw.hfc.send = NULL;
	}
	if (cs->hw.hfcD.send) {
		kfree(cs->hw.hfcD.send);
		cs->hw.hfcD.send = NULL;
	}
}
