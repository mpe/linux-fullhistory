/* $Id: diva.c,v 1.17 1999/09/04 06:20:06 keil Exp $

 * diva.c     low level stuff for Eicon.Diehl Diva Family ISDN cards
 *
 * Author     Karsten Keil (keil@isdn4linux.de)
 *
 *		This file is (c) under GNU PUBLIC LICENSE
 *		For changes and modifications please read
 *		../../../Documentation/isdn/HiSax.cert
 *
 * Thanks to Eicon Technology for documents and informations
 *
 *
 * $Log: diva.c,v $
 * Revision 1.17  1999/09/04 06:20:06  keil
 * Changes from kernel set_current_state()
 *
 * Revision 1.16  1999/08/11 21:01:25  keil
 * new PCI codefix
 *
 * Revision 1.15  1999/08/10 16:01:49  calle
 * struct pci_dev changed in 2.3.13. Made the necessary changes.
 *
 * Revision 1.14  1999/08/07 17:35:08  keil
 * approval for Eicon Technology Diva 2.01 PCI
 *
 * Revision 1.13  1999/07/21 14:46:07  keil
 * changes from EICON certification
 *
 * Revision 1.12  1999/07/12 21:05:04  keil
 * fix race in IRQ handling
 * added watchdog for lost IRQs
 *
 * Revision 1.11  1999/07/01 08:11:29  keil
 * Common HiSax version for 2.0, 2.1, 2.2 and 2.3 kernel
 *
 * Revision 1.10  1998/11/15 23:54:31  keil
 * changes from 2.0
 *
 * Revision 1.9  1998/06/27 22:52:03  keil
 * support for Diva 2.01
 *
 * Revision 1.8  1998/05/25 12:57:46  keil
 * HiSax golden code from certification, Don't use !!!
 * No leased lines, no X75, but many changes.
 *
 * Revision 1.7  1998/04/15 16:42:36  keil
 * new init code
 * new PCI init (2.1.94)
 *
 * Revision 1.6  1998/03/07 22:56:57  tsbogend
 * made HiSax working on Linux/Alpha
 *
 * Revision 1.5  1998/02/02 13:29:38  keil
 * fast io
 *
 * Revision 1.4  1997/11/08 21:35:44  keil
 * new l1 init
 *
 * Revision 1.3  1997/11/06 17:13:33  keil
 * New 2.1 init code
 *
 * Revision 1.2  1997/10/29 18:55:55  keil
 * changes for 2.1.60 (irq2dev_map)
 *
 * Revision 1.1  1997/09/18 17:11:20  keil
 * first version
 *
 *
 */

#define __NO_VERSION__
#include <linux/config.h>
#include "hisax.h"
#include "isac.h"
#include "hscx.h"
#include "ipac.h"
#include "isdnl1.h"
#include <linux/pci.h>

extern const char *CardType[];

const char *Diva_revision = "$Revision: 1.17 $";

#define byteout(addr,val) outb(val,addr)
#define bytein(addr) inb(addr)

#define DIVA_HSCX_DATA		0
#define DIVA_HSCX_ADR		4
#define DIVA_ISA_ISAC_DATA	2
#define DIVA_ISA_ISAC_ADR	6
#define DIVA_ISA_CTRL		7
#define DIVA_IPAC_ADR		0
#define DIVA_IPAC_DATA		1

#define DIVA_PCI_ISAC_DATA	8
#define DIVA_PCI_ISAC_ADR	0xc
#define DIVA_PCI_CTRL		0x10

/* SUB Types */
#define DIVA_ISA	1
#define DIVA_PCI	2
#define DIVA_IPAC_ISA	3
#define DIVA_IPAC_PCI	4

/* PCI stuff */
#define PCI_VENDOR_EICON_DIEHL	0x1133
#define PCI_DIVA20PRO_ID	0xe001
#define PCI_DIVA20_ID		0xe002
#define PCI_DIVA20PRO_U_ID	0xe003
#define PCI_DIVA20_U_ID		0xe004
#define PCI_DIVA_201		0xe005

/* CTRL (Read) */
#define DIVA_IRQ_STAT	0x01
#define DIVA_EEPROM_SDA	0x02

/* CTRL (Write) */
#define DIVA_IRQ_REQ	0x01
#define DIVA_RESET	0x08
#define DIVA_EEPROM_CLK	0x40
#define DIVA_PCI_LED_A	0x10
#define DIVA_PCI_LED_B	0x20
#define DIVA_ISA_LED_A	0x20
#define DIVA_ISA_LED_B	0x40
#define DIVA_IRQ_CLR	0x80

/* Siemens PITA */
#define PITA_MISC_REG		0x1c
#define PITA_PARA_SOFTRESET	0x01000000
#define PITA_PARA_MPX_MODE	0x04000000
#define PITA_INT0_ENABLE	0x00020000
#define PITA_INT0_STATUS	0x00000002

static inline u_char
readreg(unsigned int ale, unsigned int adr, u_char off)
{
	register u_char ret;
	long flags;

	save_flags(flags);
	cli();
	byteout(ale, off);
	ret = bytein(adr);
	restore_flags(flags);
	return (ret);
}

static inline void
readfifo(unsigned int ale, unsigned int adr, u_char off, u_char * data, int size)
{
	/* fifo read without cli because it's allready done  */

	byteout(ale, off);
	insb(adr, data, size);
}


static inline void
writereg(unsigned int ale, unsigned int adr, u_char off, u_char data)
{
	long flags;

	save_flags(flags);
	cli();
	byteout(ale, off);
	byteout(adr, data);
	restore_flags(flags);
}

static inline void
writefifo(unsigned int ale, unsigned int adr, u_char off, u_char *data, int size)
{
	/* fifo write without cli because it's allready done  */
	byteout(ale, off);
	outsb(adr, data, size);
}

static inline u_char
memreadreg(unsigned long adr, u_char off)
{
	return(0xff & *((unsigned int *)
		(((unsigned int *)adr) + off)));
}

static inline void
memwritereg(unsigned long adr, u_char off, u_char data)
{
	register u_char *p;
	
	p = (unsigned char *)(((unsigned int *)adr) + off);
	*p = data;
}

/* Interface functions */

static u_char
ReadISAC(struct IsdnCardState *cs, u_char offset)
{
	return(readreg(cs->hw.diva.isac_adr, cs->hw.diva.isac, offset));
}

static void
WriteISAC(struct IsdnCardState *cs, u_char offset, u_char value)
{
	writereg(cs->hw.diva.isac_adr, cs->hw.diva.isac, offset, value);
}

static void
ReadISACfifo(struct IsdnCardState *cs, u_char *data, int size)
{
	readfifo(cs->hw.diva.isac_adr, cs->hw.diva.isac, 0, data, size);
}

static void
WriteISACfifo(struct IsdnCardState *cs, u_char *data, int size)
{
	writefifo(cs->hw.diva.isac_adr, cs->hw.diva.isac, 0, data, size);
}

static u_char
ReadISAC_IPAC(struct IsdnCardState *cs, u_char offset)
{
	return (readreg(cs->hw.diva.isac_adr, cs->hw.diva.isac, offset+0x80));
}

static void
WriteISAC_IPAC(struct IsdnCardState *cs, u_char offset, u_char value)
{
	writereg(cs->hw.diva.isac_adr, cs->hw.diva.isac, offset|0x80, value);
}

static void
ReadISACfifo_IPAC(struct IsdnCardState *cs, u_char * data, int size)
{
	readfifo(cs->hw.diva.isac_adr, cs->hw.diva.isac, 0x80, data, size);
}

static void
WriteISACfifo_IPAC(struct IsdnCardState *cs, u_char * data, int size)
{
	writefifo(cs->hw.diva.isac_adr, cs->hw.diva.isac, 0x80, data, size);
}

static u_char
ReadHSCX(struct IsdnCardState *cs, int hscx, u_char offset)
{
	return(readreg(cs->hw.diva.hscx_adr,
		cs->hw.diva.hscx, offset + (hscx ? 0x40 : 0)));
}

static void
WriteHSCX(struct IsdnCardState *cs, int hscx, u_char offset, u_char value)
{
	writereg(cs->hw.diva.hscx_adr,
		cs->hw.diva.hscx, offset + (hscx ? 0x40 : 0), value);
}

static u_char
MemReadISAC_IPAC(struct IsdnCardState *cs, u_char offset)
{
	return (memreadreg(cs->hw.diva.cfg_reg, offset+0x80));
}

static void
MemWriteISAC_IPAC(struct IsdnCardState *cs, u_char offset, u_char value)
{
	memwritereg(cs->hw.diva.cfg_reg, offset|0x80, value);
}

static void
MemReadISACfifo_IPAC(struct IsdnCardState *cs, u_char * data, int size)
{
	while(size--)
		*data++ = memreadreg(cs->hw.diva.cfg_reg, 0x80);
}

static void
MemWriteISACfifo_IPAC(struct IsdnCardState *cs, u_char * data, int size)
{
	while(size--)
		memwritereg(cs->hw.diva.cfg_reg, 0x80, *data++);
}

static u_char
MemReadHSCX(struct IsdnCardState *cs, int hscx, u_char offset)
{
	return(memreadreg(cs->hw.diva.cfg_reg, offset + (hscx ? 0x40 : 0)));
}

static void
MemWriteHSCX(struct IsdnCardState *cs, int hscx, u_char offset, u_char value)
{
	memwritereg(cs->hw.diva.cfg_reg, offset + (hscx ? 0x40 : 0), value);
}

/*
 * fast interrupt HSCX stuff goes here
 */

#define READHSCX(cs, nr, reg) readreg(cs->hw.diva.hscx_adr, \
		cs->hw.diva.hscx, reg + (nr ? 0x40 : 0))
#define WRITEHSCX(cs, nr, reg, data) writereg(cs->hw.diva.hscx_adr, \
                cs->hw.diva.hscx, reg + (nr ? 0x40 : 0), data)

#define READHSCXFIFO(cs, nr, ptr, cnt) readfifo(cs->hw.diva.hscx_adr, \
		cs->hw.diva.hscx, (nr ? 0x40 : 0), ptr, cnt)

#define WRITEHSCXFIFO(cs, nr, ptr, cnt) writefifo(cs->hw.diva.hscx_adr, \
		cs->hw.diva.hscx, (nr ? 0x40 : 0), ptr, cnt)

#include "hscx_irq.c"

static void
diva_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs = dev_id;
	u_char val, sval;
	int cnt=5;

	if (!cs) {
		printk(KERN_WARNING "Diva: Spurious interrupt!\n");
		return;
	}
	while (((sval = bytein(cs->hw.diva.ctrl)) & DIVA_IRQ_REQ) && cnt) {
		val = readreg(cs->hw.diva.hscx_adr, cs->hw.diva.hscx, HSCX_ISTA + 0x40);
		if (val)
			hscx_int_main(cs, val);
		val = readreg(cs->hw.diva.isac_adr, cs->hw.diva.isac, ISAC_ISTA);
		if (val)
			isac_interrupt(cs, val);
		cnt--;
	}
	if (!cnt)
		printk(KERN_WARNING "Diva: IRQ LOOP\n");
	writereg(cs->hw.diva.hscx_adr, cs->hw.diva.hscx, HSCX_MASK, 0xFF);
	writereg(cs->hw.diva.hscx_adr, cs->hw.diva.hscx, HSCX_MASK + 0x40, 0xFF);
	writereg(cs->hw.diva.isac_adr, cs->hw.diva.isac, ISAC_MASK, 0xFF);
	writereg(cs->hw.diva.isac_adr, cs->hw.diva.isac, ISAC_MASK, 0x0);
	writereg(cs->hw.diva.hscx_adr, cs->hw.diva.hscx, HSCX_MASK, 0x0);
	writereg(cs->hw.diva.hscx_adr, cs->hw.diva.hscx, HSCX_MASK + 0x40, 0x0);
}

static void
diva_irq_ipac_isa(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs = dev_id;
	u_char ista,val;
	int icnt=5;

	if (!cs) {
		printk(KERN_WARNING "Diva: Spurious interrupt!\n");
		return;
	}
	ista = readreg(cs->hw.diva.isac_adr, cs->hw.diva.isac, IPAC_ISTA);
Start_IPACISA:
	if (cs->debug & L1_DEB_IPAC)
		debugl1(cs, "IPAC ISTA %02X", ista);
	if (ista & 0x0f) {
		val = readreg(cs->hw.diva.isac_adr, cs->hw.diva.isac, HSCX_ISTA + 0x40);
		if (ista & 0x01)
			val |= 0x01;
		if (ista & 0x04)
			val |= 0x02;
		if (ista & 0x08)
			val |= 0x04;
		if (val)
			hscx_int_main(cs, val);
	}
	if (ista & 0x20) {
		val = 0xfe & readreg(cs->hw.diva.isac_adr, cs->hw.diva.isac, ISAC_ISTA + 0x80);
		if (val) {
			isac_interrupt(cs, val);
		}
	}
	if (ista & 0x10) {
		val = 0x01;
		isac_interrupt(cs, val);
	}
	ista  = readreg(cs->hw.diva.isac_adr, cs->hw.diva.isac, IPAC_ISTA);
	if ((ista & 0x3f) && icnt) {
		icnt--;
		goto Start_IPACISA;
	}
	if (!icnt)
		printk(KERN_WARNING "DIVA IPAC IRQ LOOP\n");
	writereg(cs->hw.diva.isac_adr, cs->hw.diva.isac, IPAC_MASK, 0xFF);
	writereg(cs->hw.diva.isac_adr, cs->hw.diva.isac, IPAC_MASK, 0xC0);
}

static inline void
MemwaitforCEC(struct IsdnCardState *cs, int hscx)
{
	int to = 50;

	while ((MemReadHSCX(cs, hscx, HSCX_STAR) & 0x04) && to) {
		udelay(1);
		to--;
	}
	if (!to)
		printk(KERN_WARNING "HiSax: waitforCEC timeout\n");
}


static inline void
MemwaitforXFW(struct IsdnCardState *cs, int hscx)
{
	int to = 50;

	while ((!(MemReadHSCX(cs, hscx, HSCX_STAR) & 0x44) == 0x40) && to) {
		udelay(1);
		to--;
	}
	if (!to)
		printk(KERN_WARNING "HiSax: waitforXFW timeout\n");
}

static inline void
MemWriteHSCXCMDR(struct IsdnCardState *cs, int hscx, u_char data)
{
	long flags;

	save_flags(flags);
	cli();
	MemwaitforCEC(cs, hscx);
	MemWriteHSCX(cs, hscx, HSCX_CMDR, data);
	restore_flags(flags);
}

static void
Memhscx_empty_fifo(struct BCState *bcs, int count)
{
	u_char *ptr;
	struct IsdnCardState *cs = bcs->cs;
	long flags;
	int cnt;

	if ((cs->debug & L1_DEB_HSCX) && !(cs->debug & L1_DEB_HSCX_FIFO))
		debugl1(cs, "hscx_empty_fifo");

	if (bcs->hw.hscx.rcvidx + count > HSCX_BUFMAX) {
		if (cs->debug & L1_DEB_WARN)
			debugl1(cs, "hscx_empty_fifo: incoming packet too large");
		MemWriteHSCXCMDR(cs, bcs->hw.hscx.hscx, 0x80);
		bcs->hw.hscx.rcvidx = 0;
		return;
	}
	save_flags(flags);
	cli();
	ptr = bcs->hw.hscx.rcvbuf + bcs->hw.hscx.rcvidx;
	cnt = count;
	while (cnt--)
		*ptr++ = memreadreg(cs->hw.diva.cfg_reg, bcs->hw.hscx.hscx ? 0x40 : 0);
	MemWriteHSCXCMDR(cs, bcs->hw.hscx.hscx, 0x80);
	ptr = bcs->hw.hscx.rcvbuf + bcs->hw.hscx.rcvidx;
	bcs->hw.hscx.rcvidx += count;
	restore_flags(flags);
	if (cs->debug & L1_DEB_HSCX_FIFO) {
		char *t = bcs->blog;

		t += sprintf(t, "hscx_empty_fifo %c cnt %d",
			     bcs->hw.hscx.hscx ? 'B' : 'A', count);
		QuickHex(t, ptr, count);
		debugl1(cs, bcs->blog);
	}
}

static void
Memhscx_fill_fifo(struct BCState *bcs)
{
	struct IsdnCardState *cs = bcs->cs;
	int more, count, cnt;
	int fifo_size = test_bit(HW_IPAC, &cs->HW_Flags)? 64: 32;
	u_char *ptr,*p;
	long flags;


	if ((cs->debug & L1_DEB_HSCX) && !(cs->debug & L1_DEB_HSCX_FIFO))
		debugl1(cs, "hscx_fill_fifo");

	if (!bcs->tx_skb)
		return;
	if (bcs->tx_skb->len <= 0)
		return;

	more = (bcs->mode == L1_MODE_TRANS) ? 1 : 0;
	if (bcs->tx_skb->len > fifo_size) {
		more = !0;
		count = fifo_size;
	} else
		count = bcs->tx_skb->len;
	cnt = count;
	MemwaitforXFW(cs, bcs->hw.hscx.hscx);
	save_flags(flags);
	cli();
	p = ptr = bcs->tx_skb->data;
	skb_pull(bcs->tx_skb, count);
	bcs->tx_cnt -= count;
	bcs->hw.hscx.count += count;
	while(cnt--)
		memwritereg(cs->hw.diva.cfg_reg, bcs->hw.hscx.hscx ? 0x40 : 0,
			*p++);
	MemWriteHSCXCMDR(cs, bcs->hw.hscx.hscx, more ? 0x8 : 0xa);
	restore_flags(flags);
	if (cs->debug & L1_DEB_HSCX_FIFO) {
		char *t = bcs->blog;

		t += sprintf(t, "hscx_fill_fifo %c cnt %d",
			     bcs->hw.hscx.hscx ? 'B' : 'A', count);
		QuickHex(t, ptr, count);
		debugl1(cs, bcs->blog);
	}
}

static inline void
Memhscx_interrupt(struct IsdnCardState *cs, u_char val, u_char hscx)
{
	u_char r;
	struct BCState *bcs = cs->bcs + hscx;
	struct sk_buff *skb;
	int fifo_size = test_bit(HW_IPAC, &cs->HW_Flags)? 64: 32;
	int count;

	if (!test_bit(BC_FLG_INIT, &bcs->Flag))
		return;

	if (val & 0x80) {	/* RME */
		r = MemReadHSCX(cs, hscx, HSCX_RSTA);
		if ((r & 0xf0) != 0xa0) {
			if (!(r & 0x80))
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, "HSCX invalid frame");
			if ((r & 0x40) && bcs->mode)
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, "HSCX RDO mode=%d",
						bcs->mode);
			if (!(r & 0x20))
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, "HSCX CRC error");
			MemWriteHSCXCMDR(cs, hscx, 0x80);
		} else {
			count = MemReadHSCX(cs, hscx, HSCX_RBCL) & (
				test_bit(HW_IPAC, &cs->HW_Flags)? 0x3f: 0x1f);
			if (count == 0)
				count = fifo_size;
			Memhscx_empty_fifo(bcs, count);
			if ((count = bcs->hw.hscx.rcvidx - 1) > 0) {
				if (cs->debug & L1_DEB_HSCX_FIFO)
					debugl1(cs, "HX Frame %d", count);
				if (!(skb = dev_alloc_skb(count)))
					printk(KERN_WARNING "HSCX: receive out of memory\n");
				else {
					memcpy(skb_put(skb, count), bcs->hw.hscx.rcvbuf, count);
					skb_queue_tail(&bcs->rqueue, skb);
				}
			}
		}
		bcs->hw.hscx.rcvidx = 0;
		hscx_sched_event(bcs, B_RCVBUFREADY);
	}
	if (val & 0x40) {	/* RPF */
		Memhscx_empty_fifo(bcs, fifo_size);
		if (bcs->mode == L1_MODE_TRANS) {
			/* receive audio data */
			if (!(skb = dev_alloc_skb(fifo_size)))
				printk(KERN_WARNING "HiSax: receive out of memory\n");
			else {
				memcpy(skb_put(skb, fifo_size), bcs->hw.hscx.rcvbuf, fifo_size);
				skb_queue_tail(&bcs->rqueue, skb);
			}
			bcs->hw.hscx.rcvidx = 0;
			hscx_sched_event(bcs, B_RCVBUFREADY);
		}
	}
	if (val & 0x10) {	/* XPR */
		if (bcs->tx_skb) {
			if (bcs->tx_skb->len) {
				Memhscx_fill_fifo(bcs);
				return;
			} else {
				if (bcs->st->lli.l1writewakeup &&
					(PACKET_NOACK != bcs->tx_skb->pkt_type))
					bcs->st->lli.l1writewakeup(bcs->st, bcs->hw.hscx.count);
				dev_kfree_skb(bcs->tx_skb);
				bcs->hw.hscx.count = 0; 
				bcs->tx_skb = NULL;
			}
		}
		if ((bcs->tx_skb = skb_dequeue(&bcs->squeue))) {
			bcs->hw.hscx.count = 0;
			test_and_set_bit(BC_FLG_BUSY, &bcs->Flag);
			Memhscx_fill_fifo(bcs);
		} else {
			test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
			hscx_sched_event(bcs, B_XMTBUFREADY);
		}
	}
}

static inline void
Memhscx_int_main(struct IsdnCardState *cs, u_char val)
{

	u_char exval;
	struct BCState *bcs;

	if (val & 0x01) {
		bcs = cs->bcs + 1;
		exval = MemReadHSCX(cs, 1, HSCX_EXIR);
		if (exval & 0x40) {
			if (bcs->mode == 1)
				Memhscx_fill_fifo(bcs);
			else {
				/* Here we lost an TX interrupt, so
				   * restart transmitting the whole frame.
				 */
				if (bcs->tx_skb) {
					skb_push(bcs->tx_skb, bcs->hw.hscx.count);
					bcs->tx_cnt += bcs->hw.hscx.count;
					bcs->hw.hscx.count = 0;
				}
				MemWriteHSCXCMDR(cs, bcs->hw.hscx.hscx, 0x01);
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, "HSCX B EXIR %x Lost TX", exval);
			}
		} else if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "HSCX B EXIR %x", exval);
	}
	if (val & 0xf8) {
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "HSCX B interrupt %x", val);
		Memhscx_interrupt(cs, val, 1);
	}
	if (val & 0x02) {
		bcs = cs->bcs;
		exval = MemReadHSCX(cs, 0, HSCX_EXIR);
		if (exval & 0x40) {
			if (bcs->mode == L1_MODE_TRANS)
				Memhscx_fill_fifo(bcs);
			else {
				/* Here we lost an TX interrupt, so
				   * restart transmitting the whole frame.
				 */
				if (bcs->tx_skb) {
					skb_push(bcs->tx_skb, bcs->hw.hscx.count);
					bcs->tx_cnt += bcs->hw.hscx.count;
					bcs->hw.hscx.count = 0;
				}
				MemWriteHSCXCMDR(cs, bcs->hw.hscx.hscx, 0x01);
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, "HSCX A EXIR %x Lost TX", exval);
			}
		} else if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "HSCX A EXIR %x", exval);
	}
	if (val & 0x04) {
		exval = MemReadHSCX(cs, 0, HSCX_ISTA);
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "HSCX A interrupt %x", exval);
		Memhscx_interrupt(cs, exval, 0);
	}
}

static void
diva_irq_ipac_pci(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs = dev_id;
	u_char ista,val;
	int icnt=5;
	u_char *cfg;

	if (!cs) {
		printk(KERN_WARNING "Diva: Spurious interrupt!\n");
		return;
	}
	cfg = (u_char *) cs->hw.diva.pci_cfg;
	val = *cfg;
	if (!(val & PITA_INT0_STATUS))
		return; /* other shared IRQ */
	*cfg = PITA_INT0_STATUS; /* Reset pending INT0 */
	ista = memreadreg(cs->hw.diva.cfg_reg, IPAC_ISTA);
Start_IPACPCI:
	if (cs->debug & L1_DEB_IPAC)
		debugl1(cs, "IPAC ISTA %02X", ista);
	if (ista & 0x0f) {
		val = memreadreg(cs->hw.diva.cfg_reg, HSCX_ISTA + 0x40);
		if (ista & 0x01)
			val |= 0x01;
		if (ista & 0x04)
			val |= 0x02;
		if (ista & 0x08)
			val |= 0x04;
		if (val)
			Memhscx_int_main(cs, val);
	}
	if (ista & 0x20) {
		val = 0xfe & memreadreg(cs->hw.diva.cfg_reg, ISAC_ISTA + 0x80);
		if (val) {
			isac_interrupt(cs, val);
		}
	}
	if (ista & 0x10) {
		val = 0x01;
		isac_interrupt(cs, val);
	}
	ista  = memreadreg(cs->hw.diva.cfg_reg, IPAC_ISTA);
	if ((ista & 0x3f) && icnt) {
		icnt--;
		goto Start_IPACPCI;
	}
	if (!icnt)
		printk(KERN_WARNING "DIVA IPAC PCI IRQ LOOP\n");
	memwritereg(cs->hw.diva.cfg_reg, IPAC_MASK, 0xFF);
	memwritereg(cs->hw.diva.cfg_reg, IPAC_MASK, 0xC0);
}

void
release_io_diva(struct IsdnCardState *cs)
{
	int bytecnt;

	if (cs->subtyp == DIVA_IPAC_PCI) {
		u_int *cfg = (unsigned int *)cs->hw.diva.pci_cfg;

		*cfg = 0; /* disable INT0/1 */ 
		*cfg = 2; /* reset pending INT0 */
		iounmap((void *)cs->hw.diva.cfg_reg);
		iounmap((void *)cs->hw.diva.pci_cfg);
		return;
	} else if (cs->subtyp != DIVA_IPAC_ISA) {
		del_timer(&cs->hw.diva.tl);
		if (cs->hw.diva.cfg_reg)
			byteout(cs->hw.diva.ctrl, 0); /* LED off, Reset */
	}
	if ((cs->subtyp == DIVA_ISA) || (cs->subtyp == DIVA_IPAC_ISA))
		bytecnt = 8;
	else
		bytecnt = 32;
	if (cs->hw.diva.cfg_reg) {
		release_region(cs->hw.diva.cfg_reg, bytecnt);
	}
}

static void
reset_diva(struct IsdnCardState *cs)
{
	long flags;

	save_flags(flags);
	sti();
	if (cs->subtyp == DIVA_IPAC_ISA) {
		writereg(cs->hw.diva.isac_adr, cs->hw.diva.isac, IPAC_POTA2, 0x20);
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout((10*HZ)/1000);
		writereg(cs->hw.diva.isac_adr, cs->hw.diva.isac, IPAC_POTA2, 0x00);
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout((10*HZ)/1000);
		writereg(cs->hw.diva.isac_adr, cs->hw.diva.isac, IPAC_MASK, 0xc0);
	} else if (cs->subtyp == DIVA_IPAC_PCI) {
		unsigned int *ireg = (unsigned int *)(cs->hw.diva.pci_cfg +
					PITA_MISC_REG);
		*ireg = PITA_PARA_SOFTRESET | PITA_PARA_MPX_MODE;
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout((10*HZ)/1000);
		*ireg = PITA_PARA_MPX_MODE;
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout((10*HZ)/1000);
		memwritereg(cs->hw.diva.cfg_reg, IPAC_MASK, 0xc0);
	} else { /* DIVA 2.0 */
		cs->hw.diva.ctrl_reg = 0;        /* Reset On */
		byteout(cs->hw.diva.ctrl, cs->hw.diva.ctrl_reg);
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout((10*HZ)/1000);
		cs->hw.diva.ctrl_reg |= DIVA_RESET;  /* Reset Off */
		byteout(cs->hw.diva.ctrl, cs->hw.diva.ctrl_reg);
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout((10*HZ)/1000);
		if (cs->subtyp == DIVA_ISA)
			cs->hw.diva.ctrl_reg |= DIVA_ISA_LED_A;
		else {
			/* Workaround PCI9060 */
			byteout(cs->hw.diva.pci_cfg + 0x69, 9);
			cs->hw.diva.ctrl_reg |= DIVA_PCI_LED_A;
		}
		byteout(cs->hw.diva.ctrl, cs->hw.diva.ctrl_reg);
	}
	restore_flags(flags);
}

#define DIVA_ASSIGN 1

static void
diva_led_handler(struct IsdnCardState *cs)
{
	int blink = 0;

	if ((cs->subtyp == DIVA_IPAC_ISA) || (cs->subtyp == DIVA_IPAC_PCI))
		return;
	del_timer(&cs->hw.diva.tl);
	if (cs->hw.diva.status & DIVA_ASSIGN)
		cs->hw.diva.ctrl_reg |= (DIVA_ISA == cs->subtyp) ?
			DIVA_ISA_LED_A : DIVA_PCI_LED_A;
	else {
		cs->hw.diva.ctrl_reg ^= (DIVA_ISA == cs->subtyp) ?
			DIVA_ISA_LED_A : DIVA_PCI_LED_A;
		blink = 250;
	}
	if (cs->hw.diva.status & 0xf000)
		cs->hw.diva.ctrl_reg |= (DIVA_ISA == cs->subtyp) ?
			DIVA_ISA_LED_B : DIVA_PCI_LED_B;
	else if (cs->hw.diva.status & 0x0f00) {
		cs->hw.diva.ctrl_reg ^= (DIVA_ISA == cs->subtyp) ?
			DIVA_ISA_LED_B : DIVA_PCI_LED_B;
		blink = 500;
	} else
		cs->hw.diva.ctrl_reg &= ~((DIVA_ISA == cs->subtyp) ?
			DIVA_ISA_LED_B : DIVA_PCI_LED_B);

	byteout(cs->hw.diva.ctrl, cs->hw.diva.ctrl_reg);
	if (blink) {
		init_timer(&cs->hw.diva.tl);
		cs->hw.diva.tl.expires = jiffies + ((blink * HZ) / 1000);
		add_timer(&cs->hw.diva.tl);
	}
}

static int
Diva_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
	u_int *ireg;

	switch (mt) {
		case CARD_RESET:
			reset_diva(cs);
			return(0);
		case CARD_RELEASE:
			release_io_diva(cs);
			return(0);
		case CARD_INIT:
			if (cs->subtyp == DIVA_IPAC_PCI) {
				ireg = (unsigned int *)cs->hw.diva.pci_cfg;
				*ireg = PITA_INT0_ENABLE;
			}
			inithscxisac(cs, 3);
			return(0);
		case CARD_TEST:
			return(0);
		case (MDL_REMOVE | REQUEST):
			cs->hw.diva.status = 0;
			break;
		case (MDL_ASSIGN | REQUEST):
			cs->hw.diva.status |= DIVA_ASSIGN;
			break;
		case MDL_INFO_SETUP:
			if ((long)arg)
				cs->hw.diva.status |=  0x0200;
			else
				cs->hw.diva.status |=  0x0100;
			break;
		case MDL_INFO_CONN:
			if ((long)arg)
				cs->hw.diva.status |=  0x2000;
			else
				cs->hw.diva.status |=  0x1000;
			break;
		case MDL_INFO_REL:
			if ((long)arg) {
				cs->hw.diva.status &=  ~0x2000;
				cs->hw.diva.status &=  ~0x0200;
			} else {
				cs->hw.diva.status &=  ~0x1000;
				cs->hw.diva.status &=  ~0x0100;
			}
			break;
	}
	if ((cs->subtyp != DIVA_IPAC_ISA) && (cs->subtyp != DIVA_IPAC_PCI))
		diva_led_handler(cs);
	return(0);
}

static 	struct pci_dev *dev_diva __initdata = NULL;
static 	struct pci_dev *dev_diva_u __initdata = NULL;
static 	struct pci_dev *dev_diva201 __initdata = NULL;

__initfunc(int
setup_diva(struct IsdnCard *card))
{
	int bytecnt;
	u_char val;
	struct IsdnCardState *cs = card->cs;
	char tmp[64];

	strcpy(tmp, Diva_revision);
	printk(KERN_INFO "HiSax: Eicon.Diehl Diva driver Rev. %s\n", HiSax_getrev(tmp));
	if (cs->typ != ISDN_CTYPE_DIEHLDIVA)
		return(0);
	cs->hw.diva.status = 0;
	if (card->para[1]) {
		cs->hw.diva.ctrl_reg = 0;
		cs->hw.diva.cfg_reg = card->para[1];
		val = readreg(cs->hw.diva.cfg_reg + DIVA_IPAC_ADR,
			cs->hw.diva.cfg_reg + DIVA_IPAC_DATA, IPAC_ID);
		printk(KERN_INFO "Diva: IPAC version %x\n", val);
		if ((val == 1) || (val==2)) {
			cs->subtyp = DIVA_IPAC_ISA;
			cs->hw.diva.ctrl = 0;
			cs->hw.diva.isac = card->para[1] + DIVA_IPAC_DATA;
			cs->hw.diva.hscx = card->para[1] + DIVA_IPAC_DATA;
			cs->hw.diva.isac_adr = card->para[1] + DIVA_IPAC_ADR;
			cs->hw.diva.hscx_adr = card->para[1] + DIVA_IPAC_ADR;
			test_and_set_bit(HW_IPAC, &cs->HW_Flags);
		} else {
			cs->subtyp = DIVA_ISA;
			cs->hw.diva.ctrl = card->para[1] + DIVA_ISA_CTRL;
			cs->hw.diva.isac = card->para[1] + DIVA_ISA_ISAC_DATA;
			cs->hw.diva.hscx = card->para[1] + DIVA_HSCX_DATA;
			cs->hw.diva.isac_adr = card->para[1] + DIVA_ISA_ISAC_ADR;
			cs->hw.diva.hscx_adr = card->para[1] + DIVA_HSCX_ADR;
		}
		cs->irq = card->para[0];
		bytecnt = 8;
	} else {
#if CONFIG_PCI
		if (!pci_present()) {
			printk(KERN_ERR "Diva: no PCI bus present\n");
			return(0);
		}

		cs->subtyp = 0;
		if ((dev_diva = pci_find_device(PCI_VENDOR_EICON_DIEHL,
			PCI_DIVA20_ID, dev_diva))) {
			cs->subtyp = DIVA_PCI;
			cs->irq = dev_diva->irq;
			cs->hw.diva.cfg_reg = dev_diva->resource[ 2].start
				& PCI_BASE_ADDRESS_IO_MASK;
		} else if ((dev_diva_u = pci_find_device(PCI_VENDOR_EICON_DIEHL,
			PCI_DIVA20_U_ID, dev_diva_u))) {
			cs->subtyp = DIVA_PCI;
			cs->irq = dev_diva_u->irq;
			cs->hw.diva.cfg_reg = dev_diva_u->resource[ 2].start
				& PCI_BASE_ADDRESS_IO_MASK;
		} else if ((dev_diva201 = pci_find_device(PCI_VENDOR_EICON_DIEHL,
			PCI_DIVA_201, dev_diva201))) {
			cs->subtyp = DIVA_IPAC_PCI;
			cs->irq = dev_diva201->irq;
			cs->hw.diva.pci_cfg =
				(ulong) ioremap((dev_diva201->resource[ 0].start
					& PCI_BASE_ADDRESS_IO_MASK), 4096);
			cs->hw.diva.cfg_reg =
				(ulong) ioremap((dev_diva201->resource[ 1].start
					& PCI_BASE_ADDRESS_IO_MASK), 4096);
		} else {
			printk(KERN_WARNING "Diva: No PCI card found\n");
			return(0);
		}

		if (!cs->irq) {
			printk(KERN_WARNING "Diva: No IRQ for PCI card found\n");
			return(0);
		}

		if (!cs->hw.diva.cfg_reg) {
			printk(KERN_WARNING "Diva: No IO-Adr for PCI card found\n");
			return(0);
		}
		cs->irq_flags |= SA_SHIRQ;
#else
		printk(KERN_WARNING "Diva: cfgreg 0 and NO_PCI_BIOS\n");
		printk(KERN_WARNING "Diva: unable to config DIVA PCI\n");
		return (0);
#endif /* CONFIG_PCI */
		if (cs->subtyp == DIVA_IPAC_PCI) {
			cs->hw.diva.ctrl = 0;
			cs->hw.diva.isac = 0;
			cs->hw.diva.hscx = 0;
			cs->hw.diva.isac_adr = 0;
			cs->hw.diva.hscx_adr = 0;
			test_and_set_bit(HW_IPAC, &cs->HW_Flags);
			bytecnt = 0;
		} else {
			cs->hw.diva.ctrl = cs->hw.diva.cfg_reg + DIVA_PCI_CTRL;
			cs->hw.diva.isac = cs->hw.diva.cfg_reg + DIVA_PCI_ISAC_DATA;
			cs->hw.diva.hscx = cs->hw.diva.cfg_reg + DIVA_HSCX_DATA;
			cs->hw.diva.isac_adr = cs->hw.diva.cfg_reg + DIVA_PCI_ISAC_ADR;
			cs->hw.diva.hscx_adr = cs->hw.diva.cfg_reg + DIVA_HSCX_ADR;
			bytecnt = 32;
		}
	}

	printk(KERN_INFO
		"Diva: %s card configured at %#lx IRQ %d\n",
		(cs->subtyp == DIVA_PCI) ? "PCI" :
		(cs->subtyp == DIVA_ISA) ? "ISA" : 
		(cs->subtyp == DIVA_IPAC_ISA) ? "IPAC ISA" : "IPAC PCI",
		cs->hw.diva.cfg_reg, cs->irq);
	if ((cs->subtyp == DIVA_IPAC_PCI) || (cs->subtyp == DIVA_PCI))
		printk(KERN_INFO "Diva: %s PCI space at %#lx\n",
			(cs->subtyp == DIVA_PCI) ? "PCI" : "IPAC PCI",
			cs->hw.diva.pci_cfg);
	if (cs->subtyp != DIVA_IPAC_PCI) {
		if (check_region(cs->hw.diva.cfg_reg, bytecnt)) {
			printk(KERN_WARNING
			       "HiSax: %s config port %lx-%lx already in use\n",
			       CardType[card->typ],
			       cs->hw.diva.cfg_reg,
			       cs->hw.diva.cfg_reg + bytecnt);
			return (0);
		} else {
			request_region(cs->hw.diva.cfg_reg, bytecnt, "diva isdn");
		}
	}
	reset_diva(cs);
	cs->BC_Read_Reg  = &ReadHSCX;
	cs->BC_Write_Reg = &WriteHSCX;
	cs->BC_Send_Data = &hscx_fill_fifo;
	cs->cardmsg = &Diva_card_msg;
	if (cs->subtyp == DIVA_IPAC_ISA) {
		cs->readisac  = &ReadISAC_IPAC;
		cs->writeisac = &WriteISAC_IPAC;
		cs->readisacfifo  = &ReadISACfifo_IPAC;
		cs->writeisacfifo = &WriteISACfifo_IPAC;
		cs->irq_func = &diva_irq_ipac_isa;
		val = readreg(cs->hw.diva.isac_adr, cs->hw.diva.isac, IPAC_ID);
		printk(KERN_INFO "Diva: IPAC version %x\n", val);
	} else if (cs->subtyp == DIVA_IPAC_PCI) {
		cs->readisac  = &MemReadISAC_IPAC;
		cs->writeisac = &MemWriteISAC_IPAC;
		cs->readisacfifo  = &MemReadISACfifo_IPAC;
		cs->writeisacfifo = &MemWriteISACfifo_IPAC;
		cs->BC_Read_Reg  = &MemReadHSCX;
		cs->BC_Write_Reg = &MemWriteHSCX;
		cs->BC_Send_Data = &Memhscx_fill_fifo;
		cs->irq_func = &diva_irq_ipac_pci;
		val = memreadreg(cs->hw.diva.cfg_reg, IPAC_ID);
		printk(KERN_INFO "Diva: IPAC version %x\n", val);
	} else { /* DIVA 2.0 */
		cs->hw.diva.tl.function = (void *) diva_led_handler;
		cs->hw.diva.tl.data = (long) cs;
		init_timer(&cs->hw.diva.tl);
		cs->readisac  = &ReadISAC;
		cs->writeisac = &WriteISAC;
		cs->readisacfifo  = &ReadISACfifo;
		cs->writeisacfifo = &WriteISACfifo;
		cs->irq_func = &diva_interrupt;
		ISACVersion(cs, "Diva:");
		if (HscxVersion(cs, "Diva:")) {
			printk(KERN_WARNING
		       "Diva: wrong HSCX versions check IO address\n");
			release_io_diva(cs);
			return (0);
		}
	}
	return (1);
}
