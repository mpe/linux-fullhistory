/* $Id: ix1_micro.c,v 1.3 1997/04/13 19:54:02 keil Exp $

 * ix1_micro.c  low level stuff for ITK ix1-micro Rev.2 isdn cards
 *              derived from the original file teles3.c from Karsten Keil
 *
 * Copyright (C) 1997 Klaus-Peter Nischke (ITK AG) (for the modifications to
 *                                                  the original file teles.c)
 *
 * Thanks to    Jan den Ouden
 *              Fritz Elfert
 *              Beat Doebeli
 *
 * $Log: ix1_micro.c,v $
 * Revision 1.3  1997/04/13 19:54:02  keil
 * Change in IRQ check delay for SMP
 *
 * Revision 1.2  1997/04/06 22:54:21  keil
 * Using SKB's
 *
 * Revision 1.1  1997/01/27 15:43:10  keil
 * first version
 *
 *
 */

/*
   For the modification done by the author the following terms and conditions
   apply (GNU PUBLIC LICENSE)


   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.


   You may contact Klaus-Peter Nischke by email: klaus@nischke.do.eunet.de
   or by conventional mail:

   Klaus-Peter Nischke
   Deusener Str. 287
   44369 Dortmund
   Germany
 */


#define __NO_VERSION__
#include "siemens.h"
#include "hisax.h"
#include "teles3.h"
#include "isdnl1.h"
#include <linux/kernel_stat.h>

extern const char *CardType[];
const char *ix1_revision = "$Revision: 1.3 $";

#define byteout(addr,val) outb_p(val,addr)
#define bytein(addr) inb_p(addr)

#define SPECIAL_PORT_OFFSET 3

#define ISAC_COMMAND_OFFSET 2
#define ISAC_DATA_OFFSET 0
#define HSCX_COMMAND_OFFSET 2
#define HSCX_DATA_OFFSET 1

#define ISAC_FIFOSIZE 16
#define HSCX_FIFOSIZE 16

#define TIMEOUT 50

static inline u_char
IsacReadReg(unsigned int adr, u_char off)
{
	byteout(adr + ISAC_COMMAND_OFFSET, off + 0x20);
	return bytein(adr + ISAC_DATA_OFFSET);
}

static inline void
IsacWriteReg(unsigned int adr, u_char off, u_char data)
{
	byteout(adr + ISAC_COMMAND_OFFSET, off + 0x20);
	byteout(adr + ISAC_DATA_OFFSET, data);
}

#define HSCX_OFFSET(WhichHscx,offset) \
( (WhichHscx) ? (offset+0x60) : (offset+0x20) )

static inline u_char
HscxReadReg(unsigned int adr, int WhichHscx, u_char off)
{
	byteout(adr + HSCX_COMMAND_OFFSET, HSCX_OFFSET(WhichHscx, off));
	return bytein(adr + HSCX_DATA_OFFSET);
}

static inline void
HscxWriteReg(unsigned int adr, int WhichHscx, u_char off, u_char data)
{
	byteout(adr + HSCX_COMMAND_OFFSET, HSCX_OFFSET(WhichHscx, off));
	byteout(adr + HSCX_DATA_OFFSET, data);
}


static inline void
IsacReadFifo(unsigned int adr, u_char * data, int size)
{
	byteout(adr + ISAC_COMMAND_OFFSET, 0);
	while (size--)
		*data++ = bytein(adr + ISAC_DATA_OFFSET);
}

static void
IsacWriteFifo(unsigned int adr, u_char * data, int size)
{
	byteout(adr + ISAC_COMMAND_OFFSET, 0);
	while (size--) {
		byteout(adr + ISAC_DATA_OFFSET, *data);
		data++;
	}
}

static inline void
HscxReadFifo(unsigned int adr, int WhichHscx, u_char * data, int size)
{
	byteout(adr + HSCX_COMMAND_OFFSET, (WhichHscx) ? 0x40 : 0x00);
	while (size--)
		*data++ = bytein(adr + HSCX_DATA_OFFSET);
}

static void
HscxWriteFifo(unsigned int adr, int WhichHscx, u_char * data, int size)
{
	byteout(adr + HSCX_COMMAND_OFFSET, (WhichHscx) ? 0x40 : 0x00);
	while (size--) {
		byteout(adr + HSCX_DATA_OFFSET, *data);
		data++;
	}
}

static inline void
waitforCEC(int adr, int WhichHscx)
{
	int to = TIMEOUT;

	while ((HscxReadReg(adr, WhichHscx, HSCX_STAR) & 0x04) && to) {
		udelay(1);
		to--;
	}
	if (!to)
		printk(KERN_WARNING "ix1-Micro: waitforCEC timeout\n");
}


static inline void
waitforXFW(int adr, int WhichHscx)
{
	int to = TIMEOUT;

	while ((!(HscxReadReg(adr, WhichHscx, HSCX_STAR) & 0x44) == 0x40) && to) {
		udelay(1);
		to--;
	}
	if (!to)
		printk(KERN_WARNING "ix1-Micro: waitforXFW timeout\n");
}

static inline void
writehscxCMDR(int adr, int WhichHscx, u_char data)
{
	long flags;

	save_flags(flags);
	cli();
	waitforCEC(adr, WhichHscx);
	HscxWriteReg(adr, WhichHscx, HSCX_CMDR, data);
	restore_flags(flags);
}

/*
 * fast interrupt here
 */

static void
hscxreport(struct IsdnCardState *sp, int hscx)
{
	printk(KERN_DEBUG "HSCX %d\n", hscx);
	printk(KERN_DEBUG "ISTA %x\n", HscxReadReg(sp->hscx[hscx], hscx, HSCX_ISTA));
	printk(KERN_DEBUG "STAR %x\n", HscxReadReg(sp->hscx[hscx], hscx, HSCX_STAR));
	printk(KERN_DEBUG "EXIR %x\n", HscxReadReg(sp->hscx[hscx], hscx, HSCX_EXIR));
}

void
ix1micro_report(struct IsdnCardState *sp)
{
	printk(KERN_DEBUG "ISAC\n");
	printk(KERN_DEBUG "ISTA %x\n", IsacReadReg(sp->isac, ISAC_ISTA));
	printk(KERN_DEBUG "STAR %x\n", IsacReadReg(sp->isac, ISAC_STAR));
	printk(KERN_DEBUG "EXIR %x\n", IsacReadReg(sp->isac, ISAC_EXIR));
	hscxreport(sp, 0);
	hscxreport(sp, 1);
}

/*
 * HSCX stuff goes here
 */

static void
hscx_empty_fifo(struct HscxState *hsp, int count)
{
	u_char *ptr;
	struct IsdnCardState *sp = hsp->sp;
	long flags;

	if ((sp->debug & L1_DEB_HSCX) && !(sp->debug & L1_DEB_HSCX_FIFO))
		debugl1(sp, "hscx_empty_fifo");

	if (hsp->rcvidx + count > HSCX_BUFMAX) {
		if (sp->debug & L1_DEB_WARN)
			debugl1(sp, "hscx_empty_fifo: incoming packet too large");
		writehscxCMDR(sp->hscx[hsp->hscx], hsp->hscx, 0x80);
		hsp->rcvidx = 0;
		return;
	}
	ptr = hsp->rcvbuf + hsp->rcvidx;
	hsp->rcvidx += count;
	save_flags(flags);
	cli();
	HscxReadFifo(sp->hscx[hsp->hscx], hsp->hscx, ptr, count);
	writehscxCMDR(sp->hscx[hsp->hscx], hsp->hscx, 0x80);
	restore_flags(flags);
	if (sp->debug & L1_DEB_HSCX_FIFO) {
		char tmp[128];
		char *t = tmp;

		t += sprintf(t, "hscx_empty_fifo %c cnt %d",
			     hsp->hscx ? 'B' : 'A', count);
		QuickHex(t, ptr, count);
		debugl1(sp, tmp);
	}
}

static void
hscx_fill_fifo(struct HscxState *hsp)
{
	struct IsdnCardState *sp = hsp->sp;
	int more, count;
	u_char *ptr;
	long flags;

	if ((sp->debug & L1_DEB_HSCX) && !(sp->debug & L1_DEB_HSCX_FIFO))
		debugl1(sp, "hscx_fill_fifo");

	if (!hsp->tx_skb)
		return;
	if (hsp->tx_skb->len <= 0)
		return;

	more = (hsp->mode == 1) ? 1 : 0;
	if (hsp->tx_skb->len > 32) {
		more = !0;
		count = 32;
	} else
		count = hsp->tx_skb->len;

	waitforXFW(sp->hscx[hsp->hscx], hsp->hscx);
	save_flags(flags);
	cli();
	ptr = hsp->tx_skb->data;
	skb_pull(hsp->tx_skb, count);
	hsp->tx_cnt -= count;
	hsp->count += count;
	HscxWriteFifo(sp->hscx[hsp->hscx], hsp->hscx, ptr, count);
	writehscxCMDR(sp->hscx[hsp->hscx], hsp->hscx, more ? 0x8 : 0xa);
	restore_flags(flags);
	if (sp->debug & L1_DEB_HSCX_FIFO) {
		char tmp[128];
		char *t = tmp;

		t += sprintf(t, "hscx_fill_fifo %c cnt %d",
			     hsp->hscx ? 'B' : 'A', count);
		QuickHex(t, ptr, count);
		debugl1(sp, tmp);
	}
}

static inline void
hscx_interrupt(struct IsdnCardState *sp, u_char val, u_char hscx)
{
	u_char r;
	struct HscxState *hsp = sp->hs + hscx;
	struct sk_buff *skb;
	int count;
	char tmp[32];

	if (!hsp->init)
		return;

	if (val & 0x80) {	/* RME */

		r = HscxReadReg(sp->hscx[hsp->hscx], hscx, HSCX_RSTA);
		if ((r & 0xf0) != 0xa0) {
			if (!r & 0x80)
				if (sp->debug & L1_DEB_WARN)
					debugl1(sp, "HSCX invalid frame");
			if ((r & 0x40) && hsp->mode)
				if (sp->debug & L1_DEB_WARN) {
					sprintf(tmp, "HSCX RDO mode=%d",
						hsp->mode);
					debugl1(sp, tmp);
				}
			if (!r & 0x20)
				if (sp->debug & L1_DEB_WARN)
					debugl1(sp, "HSCX CRC error");
			writehscxCMDR(sp->hscx[hsp->hscx], hsp->hscx, 0x80);
		} else {
			count = HscxReadReg(sp->hscx[hsp->hscx], hscx, HSCX_RBCL) & 0x1f;
			if (count == 0)
				count = 32;
			hscx_empty_fifo(hsp, count);
			if ((count = hsp->rcvidx - 1) > 0) {
				if (sp->debug & L1_DEB_HSCX_FIFO) {
					sprintf(tmp, "HX Frame %d", count);
					debugl1(sp, tmp);
				}
				if (!(skb = dev_alloc_skb(count)))
					printk(KERN_WARNING "IX1: receive out of memory\n");
				else {
					memcpy(skb_put(skb, count), hsp->rcvbuf, count);
					skb_queue_tail(&hsp->rqueue, skb);
				}
			}
		}
		hsp->rcvidx = 0;
		hscx_sched_event(hsp, HSCX_RCVBUFREADY);
	}
	if (val & 0x40) {	/* RPF */
		hscx_empty_fifo(hsp, 32);
		if (hsp->mode == 1) {
			/* receive audio data */
			if (!(skb = dev_alloc_skb(32)))
				printk(KERN_WARNING "IX1: receive out of memory\n");
			else {
				memcpy(skb_put(skb, 32), hsp->rcvbuf, 32);
				skb_queue_tail(&hsp->rqueue, skb);
			}
			hsp->rcvidx = 0;
			hscx_sched_event(hsp, HSCX_RCVBUFREADY);
		}
	}
	if (val & 0x10) {	/* XPR */
		if (hsp->tx_skb)
			if (hsp->tx_skb->len) {
				hscx_fill_fifo(hsp);
				return;
			} else {
				SET_SKB_FREE(hsp->tx_skb);
				dev_kfree_skb(hsp->tx_skb, FREE_WRITE);
				hsp->count = 0;
				if (hsp->st->l4.l1writewakeup)
					hsp->st->l4.l1writewakeup(hsp->st);
				hsp->tx_skb = NULL;
			}
		if ((hsp->tx_skb = skb_dequeue(&hsp->squeue))) {
			hsp->count = 0;
			hscx_fill_fifo(hsp);
		} else
			hscx_sched_event(hsp, HSCX_XMTBUFREADY);
	}
}

/*
 * ISAC stuff goes here
 */

static void
isac_empty_fifo(struct IsdnCardState *sp, int count)
{
	u_char *ptr;
	long flags;

	if ((sp->debug & L1_DEB_ISAC) && !(sp->debug & L1_DEB_ISAC_FIFO))
		if (sp->debug & L1_DEB_ISAC)
			debugl1(sp, "isac_empty_fifo");

	if ((sp->rcvidx + count) >= MAX_DFRAME_LEN) {
		if (sp->debug & L1_DEB_WARN) {
			char tmp[40];
			sprintf(tmp, "isac_empty_fifo overrun %d",
				sp->rcvidx + count);
			debugl1(sp, tmp);
		}
		IsacWriteReg(sp->isac, ISAC_CMDR, 0x80);
		sp->rcvidx = 0;
		return;
	}
	ptr = sp->rcvbuf + sp->rcvidx;
	sp->rcvidx += count;
	save_flags(flags);
	cli();
	IsacReadFifo(sp->isac, ptr, count);
	IsacWriteReg(sp->isac, ISAC_CMDR, 0x80);
	restore_flags(flags);
	if (sp->debug & L1_DEB_ISAC_FIFO) {
		char tmp[128];
		char *t = tmp;

		t += sprintf(t, "isac_empty_fifo cnt %d", count);
		QuickHex(t, ptr, count);
		debugl1(sp, tmp);
	}
}

static void
isac_fill_fifo(struct IsdnCardState *sp)
{
	int count, more;
	u_char *ptr;
	long flags;

	if ((sp->debug & L1_DEB_ISAC) && !(sp->debug & L1_DEB_ISAC_FIFO))
		debugl1(sp, "isac_fill_fifo");

	if (!sp->tx_skb)
		return;

	count = sp->tx_skb->len;
	if (count <= 0)
		return;

	more = 0;
	if (count > 32) {
		more = !0;
		count = 32;
	}
	save_flags(flags);
	cli();
	ptr = sp->tx_skb->data;
	skb_pull(sp->tx_skb, count);
	sp->tx_cnt += count;
	IsacWriteFifo(sp->isac, ptr, count);
	IsacWriteReg(sp->isac, ISAC_CMDR, more ? 0x8 : 0xa);
	restore_flags(flags);
	if (sp->debug & L1_DEB_ISAC_FIFO) {
		char tmp[128];
		char *t = tmp;

		t += sprintf(t, "isac_fill_fifo cnt %d", count);
		QuickHex(t, ptr, count);
		debugl1(sp, tmp);
	}
}

static void
ph_command(struct IsdnCardState *sp, unsigned int command)
{
	if (sp->debug & L1_DEB_ISAC) {
		char tmp[32];
		sprintf(tmp, "ph_command %d", command);
		debugl1(sp, tmp);
	}
	IsacWriteReg(sp->isac, ISAC_CIX0, (command << 2) | 3);
}


static inline void
isac_interrupt(struct IsdnCardState *sp, u_char val)
{
	u_char exval;
	struct sk_buff *skb;
	unsigned int count;
	char tmp[32];

	if (sp->debug & L1_DEB_ISAC) {
		sprintf(tmp, "ISAC interrupt %x", val);
		debugl1(sp, tmp);
	}
	if (val & 0x80) {	/* RME */
		exval = IsacReadReg(sp->isac, ISAC_RSTA);
		if ((exval & 0x70) != 0x20) {
			if (exval & 0x40)
				if (sp->debug & L1_DEB_WARN)
					debugl1(sp, "ISAC RDO");
			if (!exval & 0x20)
				if (sp->debug & L1_DEB_WARN)
					debugl1(sp, "ISAC CRC error");
			IsacWriteReg(sp->isac, ISAC_CMDR, 0x80);
		} else {
			count = IsacReadReg(sp->isac, ISAC_RBCL) & 0x1f;
			if (count == 0)
				count = 32;
			isac_empty_fifo(sp, count);
			if ((count = sp->rcvidx) > 0) {
				sp->rcvidx = 0;
				if (!(skb = alloc_skb(count, GFP_ATOMIC)))
					printk(KERN_WARNING "IX1: D receive out of memory\n");
				else {
					memcpy(skb_put(skb, count), sp->rcvbuf, count);
					skb_queue_tail(&sp->rq, skb);
				}
			}
		}
		sp->rcvidx = 0;
		isac_sched_event(sp, ISAC_RCVBUFREADY);
	}
	if (val & 0x40) {	/* RPF */
		isac_empty_fifo(sp, 32);
	}
	if (val & 0x20) {	/* RSC */
		/* never */
		if (sp->debug & L1_DEB_WARN)
			debugl1(sp, "ISAC RSC interrupt");
	}
	if (val & 0x10) {	/* XPR */
		if (sp->tx_skb)
			if (sp->tx_skb->len) {
				isac_fill_fifo(sp);
				goto afterXPR;
			} else {
				SET_SKB_FREE(sp->tx_skb);
				dev_kfree_skb(sp->tx_skb, FREE_WRITE);
				sp->tx_cnt = 0;
				sp->tx_skb = NULL;
			}
		if ((sp->tx_skb = skb_dequeue(&sp->sq))) {
			sp->tx_cnt = 0;
			isac_fill_fifo(sp);
		} else
			isac_sched_event(sp, ISAC_XMTBUFREADY);
	}
      afterXPR:
	if (val & 0x04) {	/* CISQ */
		sp->ph_state = (IsacReadReg(sp->isac, ISAC_CIX0) >> 2)
		    & 0xf;
		if (sp->debug & L1_DEB_ISAC) {
			sprintf(tmp, "l1state %d", sp->ph_state);
			debugl1(sp, tmp);
		}
		isac_new_ph(sp);
	}
	if (val & 0x02) {	/* SIN */
		/* never */
		if (sp->debug & L1_DEB_WARN)
			debugl1(sp, "ISAC SIN interrupt");
	}
	if (val & 0x01) {	/* EXI */
		exval = IsacReadReg(sp->isac, ISAC_EXIR);
		if (sp->debug & L1_DEB_WARN) {
			sprintf(tmp, "ISAC EXIR %02x", exval);
			debugl1(sp, tmp);
		}
	}
}

static inline void
hscx_int_main(struct IsdnCardState *sp, u_char val)
{

	u_char exval;
	struct HscxState *hsp;
	char tmp[32];


	if (val & 0x01) {
		hsp = sp->hs + 1;
		exval = HscxReadReg(sp->hscx[1], 1, HSCX_EXIR);
		if (exval == 0x40) {
			if (hsp->mode == 1)
				hscx_fill_fifo(hsp);
			else {
				/* Here we lost an TX interrupt, so
				   * restart transmitting the whole frame.
				 */
				if (hsp->tx_skb) {
					skb_push(hsp->tx_skb, hsp->count);
					hsp->tx_cnt += hsp->count;
					hsp->count = 0;
				}
				writehscxCMDR(sp->hscx[hsp->hscx], hsp->hscx, 0x01);
				if (sp->debug & L1_DEB_WARN) {
					sprintf(tmp, "HSCX B EXIR %x Lost TX", exval);
					debugl1(sp, tmp);
				}
			}
		} else if (sp->debug & L1_DEB_HSCX) {
			sprintf(tmp, "HSCX B EXIR %x", exval);
			debugl1(sp, tmp);
		}
	}
	if (val & 0xf8) {
		if (sp->debug & L1_DEB_HSCX) {
			sprintf(tmp, "HSCX B interrupt %x", val);
			debugl1(sp, tmp);
		}
		hscx_interrupt(sp, val, 1);
	}
	if (val & 0x02) {
		hsp = sp->hs;
		exval = HscxReadReg(sp->hscx[0], 0, HSCX_EXIR);
		if (exval == 0x40) {
			if (hsp->mode == 1)
				hscx_fill_fifo(hsp);
			else {
				/* Here we lost an TX interrupt, so
				   * restart transmitting the whole frame.
				 */
				if (hsp->tx_skb) {
					skb_push(hsp->tx_skb, hsp->count);
					hsp->tx_cnt += hsp->count;
					hsp->count = 0;
				}
				writehscxCMDR(sp->hscx[hsp->hscx], hsp->hscx, 0x01);
				if (sp->debug & L1_DEB_WARN) {
					sprintf(tmp, "HSCX A EXIR %x Lost TX", exval);
					debugl1(sp, tmp);
				}
			}
		} else if (sp->debug & L1_DEB_HSCX) {
			sprintf(tmp, "HSCX A EXIR %x", exval);
			debugl1(sp, tmp);
		}
	}
	if (val & 0x04) {
		exval = HscxReadReg(sp->hscx[0], 0, HSCX_ISTA);
		if (sp->debug & L1_DEB_HSCX) {
			sprintf(tmp, "HSCX A interrupt %x", exval);
			debugl1(sp, tmp);
		}
		hscx_interrupt(sp, exval, 0);
	}
}

static void
ix1micro_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *sp;
	u_char val, stat = 0;

	sp = (struct IsdnCardState *) dev_id;

	if (!sp) {
		printk(KERN_WARNING "Teles: Spurious interrupt!\n");
		return;
	}
	val = HscxReadReg(sp->hscx[1], 1, HSCX_ISTA);
      Start_HSCX:
	if (val) {
		hscx_int_main(sp, val);
		stat |= 1;
	}
	val = IsacReadReg(sp->isac, ISAC_ISTA);
      Start_ISAC:
	if (val) {
		isac_interrupt(sp, val);
		stat |= 2;
	}
	val = HscxReadReg(sp->hscx[1], 1, HSCX_ISTA);
	if (val) {
		if (sp->debug & L1_DEB_HSCX)
			debugl1(sp, "HSCX IntStat after IntRoutine");
		goto Start_HSCX;
	}
	val = IsacReadReg(sp->isac, ISAC_ISTA);
	if (val) {
		if (sp->debug & L1_DEB_ISAC)
			debugl1(sp, "ISAC IntStat after IntRoutine");
		goto Start_ISAC;
	}
	if (stat & 1) {
		HscxWriteReg(sp->hscx[0], 0, HSCX_MASK, 0xFF);
		HscxWriteReg(sp->hscx[1], 1, HSCX_MASK, 0xFF);
		HscxWriteReg(sp->hscx[0], 0, HSCX_MASK, 0x0);
		HscxWriteReg(sp->hscx[1], 1, HSCX_MASK, 0x0);
	}
	if (stat & 2) {
		IsacWriteReg(sp->isac, ISAC_MASK, 0xFF);
		IsacWriteReg(sp->isac, ISAC_MASK, 0x0);
	}
}


static void
initisac(struct IsdnCardState *sp)
{
	unsigned int adr = sp->isac;

	/* 16.3 IOM 2 Mode */
	IsacWriteReg(adr, ISAC_MASK, 0xff);
	IsacWriteReg(adr, ISAC_ADF2, 0x80);
	IsacWriteReg(adr, ISAC_SQXR, 0x2f);
	IsacWriteReg(adr, ISAC_SPCR, 0x0);
	IsacWriteReg(adr, ISAC_ADF1, 0x2);
	IsacWriteReg(adr, ISAC_STCR, 0x70);
	IsacWriteReg(adr, ISAC_MODE, 0xc9);
	IsacWriteReg(adr, ISAC_TIMR, 0x0);
	IsacWriteReg(adr, ISAC_ADF1, 0x0);
	IsacWriteReg(adr, ISAC_CMDR, 0x41);
	IsacWriteReg(adr, ISAC_CIX0, (1 << 2) | 3);
	IsacWriteReg(adr, ISAC_MASK, 0xff);
	IsacWriteReg(adr, ISAC_MASK, 0x0);
}

static void
modehscx(struct HscxState *hs, int mode, int ichan)
{
	struct IsdnCardState *sp = hs->sp;
	int hscx = hs->hscx;

	if (sp->debug & L1_DEB_HSCX) {
		char tmp[40];
		sprintf(tmp, "hscx %c mode %d ichan %d",
			'A' + hscx, mode, ichan);
		debugl1(sp, tmp);
	}
	hs->mode = mode;
	HscxWriteReg(sp->hscx[hscx], hscx, HSCX_CCR1, 0x85);
	HscxWriteReg(sp->hscx[hscx], hscx, HSCX_XAD1, 0xFF);
	HscxWriteReg(sp->hscx[hscx], hscx, HSCX_XAD2, 0xFF);
	HscxWriteReg(sp->hscx[hscx], hscx, HSCX_RAH2, 0xFF);
	HscxWriteReg(sp->hscx[hscx], hscx, HSCX_XBCH, 0x0);
	HscxWriteReg(sp->hscx[hscx], hscx, HSCX_RLCR, 0x0);

	switch (mode) {
		case 0:
			HscxWriteReg(sp->hscx[hscx], hscx, HSCX_CCR2, 0x30);
			HscxWriteReg(sp->hscx[hscx], hscx, HSCX_TSAX, 0xff);
			HscxWriteReg(sp->hscx[hscx], hscx, HSCX_TSAR, 0xff);
			HscxWriteReg(sp->hscx[hscx], hscx, HSCX_XCCR, 7);
			HscxWriteReg(sp->hscx[hscx], hscx, HSCX_RCCR, 7);
			HscxWriteReg(sp->hscx[hscx], hscx, HSCX_MODE, 0x84);
			break;
		case 1:
			if (ichan == 0) {
				HscxWriteReg(sp->hscx[hscx], hscx, HSCX_CCR2, 0x30);
				HscxWriteReg(sp->hscx[hscx], hscx, HSCX_TSAX, 0x2f);
				HscxWriteReg(sp->hscx[hscx], hscx, HSCX_TSAR, 0x2f);
				HscxWriteReg(sp->hscx[hscx], hscx, HSCX_XCCR, 7);
				HscxWriteReg(sp->hscx[hscx], hscx, HSCX_RCCR, 7);
			} else {
				HscxWriteReg(sp->hscx[hscx], hscx, HSCX_CCR2, 0x30);
				HscxWriteReg(sp->hscx[hscx], hscx, HSCX_TSAX, 0x3);
				HscxWriteReg(sp->hscx[hscx], hscx, HSCX_TSAR, 0x3);
				HscxWriteReg(sp->hscx[hscx], hscx, HSCX_XCCR, 7);
				HscxWriteReg(sp->hscx[hscx], hscx, HSCX_RCCR, 7);
			}
			HscxWriteReg(sp->hscx[hscx], hscx, HSCX_MODE, 0xe4);
			HscxWriteReg(sp->hscx[hscx], hscx, HSCX_CMDR, 0x41);
			break;
		case 2:
			if (ichan == 0) {
				HscxWriteReg(sp->hscx[hscx], hscx, HSCX_CCR2, 0x30);
				HscxWriteReg(sp->hscx[hscx], hscx, HSCX_TSAX, 0x2f);
				HscxWriteReg(sp->hscx[hscx], hscx, HSCX_TSAR, 0x2f);
				HscxWriteReg(sp->hscx[hscx], hscx, HSCX_XCCR, 7);
				HscxWriteReg(sp->hscx[hscx], hscx, HSCX_RCCR, 7);
			} else {
				HscxWriteReg(sp->hscx[hscx], hscx, HSCX_CCR2, 0x30);
				HscxWriteReg(sp->hscx[hscx], hscx, HSCX_TSAX, 0x3);
				HscxWriteReg(sp->hscx[hscx], hscx, HSCX_TSAR, 0x3);
				HscxWriteReg(sp->hscx[hscx], hscx, HSCX_XCCR, 7);
				HscxWriteReg(sp->hscx[hscx], hscx, HSCX_RCCR, 7);
			}
			HscxWriteReg(sp->hscx[hscx], hscx, HSCX_MODE, 0x8c);
			HscxWriteReg(sp->hscx[hscx], hscx, HSCX_CMDR, 0x41);
			break;
	}
	HscxWriteReg(sp->hscx[hscx], hscx, HSCX_ISTA, 0x00);
}

void
release_io_ix1micro(struct IsdnCard *card)
{
	if (card->sp->cfg_reg)
		release_region(card->sp->cfg_reg, 4);
}

static void
clear_pending_ints(struct IsdnCardState *sp)
{
	int val;
	char tmp[64];

	val = HscxReadReg(sp->hscx[1], 1, HSCX_ISTA);
	sprintf(tmp, "HSCX B ISTA %x", val);
	debugl1(sp, tmp);
	if (val & 0x01) {
		val = HscxReadReg(sp->hscx[1], 1, HSCX_EXIR);
		sprintf(tmp, "HSCX B EXIR %x", val);
		debugl1(sp, tmp);
	} else if (val & 0x02) {
		val = HscxReadReg(sp->hscx[0], 0, HSCX_EXIR);
		sprintf(tmp, "HSCX A EXIR %x", val);
		debugl1(sp, tmp);
	}
	val = HscxReadReg(sp->hscx[0], 0, HSCX_ISTA);
	sprintf(tmp, "HSCX A ISTA %x", val);
	debugl1(sp, tmp);
	val = HscxReadReg(sp->hscx[1], 1, HSCX_STAR);
	sprintf(tmp, "HSCX B STAR %x", val);
	debugl1(sp, tmp);
	val = HscxReadReg(sp->hscx[0], 0, HSCX_STAR);
	sprintf(tmp, "HSCX A STAR %x", val);
	debugl1(sp, tmp);
	val = IsacReadReg(sp->isac, ISAC_STAR);
	sprintf(tmp, "ISAC STAR %x", val);
	debugl1(sp, tmp);
	val = IsacReadReg(sp->isac, ISAC_MODE);
	sprintf(tmp, "ISAC MODE %x", val);
	debugl1(sp, tmp);
	val = IsacReadReg(sp->isac, ISAC_ADF2);
	sprintf(tmp, "ISAC ADF2 %x", val);
	debugl1(sp, tmp);
	val = IsacReadReg(sp->isac, ISAC_ISTA);
	sprintf(tmp, "ISAC ISTA %x", val);
	debugl1(sp, tmp);
	if (val & 0x01) {
		val = IsacReadReg(sp->isac, ISAC_EXIR);
		sprintf(tmp, "ISAC EXIR %x", val);
		debugl1(sp, tmp);
	} else if (val & 0x04) {
		val = IsacReadReg(sp->isac, ISAC_CIR0);
		sprintf(tmp, "ISAC CIR0 %x", val);
		debugl1(sp, tmp);
	}
	IsacWriteReg(sp->isac, ISAC_MASK, 0);
	IsacWriteReg(sp->isac, ISAC_CMDR, 0x41);
}

int
initix1micro(struct IsdnCardState *sp)
{
	int ret;
	int loop = 0;
	char tmp[40];

	sp->counter = kstat_irqs(sp->irq);
	sprintf(tmp, "IRQ %d count %d", sp->irq, sp->counter);
	debugl1(sp, tmp);
	clear_pending_ints(sp);
	ret = get_irq(sp->cardnr, &ix1micro_interrupt);
	if (ret) {
		initisac(sp);
		sp->modehscx(sp->hs, 0, 0);
		sp->modehscx(sp->hs + 1, 0, 0);
		while (loop++ < 10) {
			/* At least 1-3 irqs must happen
			 * (one from HSCX A, one from HSCX B, 3rd from ISAC)
			 */
			if (kstat_irqs(sp->irq) > sp->counter)
				break;
			current->state = TASK_INTERRUPTIBLE;
			current->timeout = jiffies + 1;
			schedule();
		}
		sprintf(tmp, "IRQ %d count %d", sp->irq,
			kstat_irqs(sp->irq));
		debugl1(sp, tmp);
		if (kstat_irqs(sp->irq) == sp->counter) {
			printk(KERN_WARNING
			       "ix1-Micro: IRQ(%d) getting no interrupts during init\n",
			       sp->irq);
			free_irq(sp->irq, sp);
			return (0);
		}
	}
	return (ret);
}

int
setup_ix1micro(struct IsdnCard *card)
{
	u_char val, verA, verB;
	struct IsdnCardState *sp = card->sp;
	long flags;
	char tmp[64];

	strcpy(tmp, ix1_revision);
	printk(KERN_NOTICE "HiSax: ITK IX1 driver Rev. %s\n", HiSax_getrev(tmp));
	if (sp->typ != ISDN_CTYPE_IX1MICROR2)
		return (0);

	/* IO-Ports */
	sp->isac = sp->hscx[0] = sp->hscx[1] = sp->cfg_reg = card->para[1];
	sp->irq = card->para[0];
	if (sp->cfg_reg) {
		if (check_region((sp->cfg_reg), 4)) {
			printk(KERN_WARNING
			  "HiSax: %s config port %x-%x already in use\n",
			       CardType[card->typ],
			       sp->cfg_reg,
			       sp->cfg_reg + 4);
			return (0);
		} else
			request_region(sp->cfg_reg, 4, "ix1micro cfg");
	}
	/* reset isac */
	save_flags(flags);
	val = 3 * (HZ / 10) + 1;
	sti();
	while (val--) {
		byteout(sp->cfg_reg + SPECIAL_PORT_OFFSET, 1);
		HZDELAY(1);	/* wait >=10 ms */
	}
	byteout(sp->cfg_reg + SPECIAL_PORT_OFFSET, 0);
	restore_flags(flags);

	printk(KERN_NOTICE
	       "HiSax: %s config irq:%d io:0x%x\n",
	       CardType[sp->typ], sp->irq,
	       sp->cfg_reg);
	verA = HscxReadReg(sp->hscx[0], 0, HSCX_VSTR) & 0xf;
	verB = HscxReadReg(sp->hscx[1], 1, HSCX_VSTR) & 0xf;
	printk(KERN_INFO "ix1-Micro: HSCX version A: %s  B: %s\n",
	       HscxVersion(verA), HscxVersion(verB));
	val = IsacReadReg(sp->isac, ISAC_RBCH);
	printk(KERN_INFO "ix1-Micro: ISAC %s\n",
	       ISACVersion(val));
	if ((verA == 0) | (verA == 0xf) | (verB == 0) | (verB == 0xf)) {
		printk(KERN_WARNING
		    "ix1-Micro: wrong HSCX versions check IO address\n");
		release_io_ix1micro(card);
		return (0);
	}
	sp->modehscx = &modehscx;
	sp->ph_command = &ph_command;
	sp->hscx_fill_fifo = &hscx_fill_fifo;
	sp->isac_fill_fifo = &isac_fill_fifo;
	return (1);
}
