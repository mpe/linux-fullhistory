/* $Id: elsa.c,v 1.14 1997/04/13 19:53:25 keil Exp $

 * elsa.c     low level stuff for Elsa isdn cards
 *
 * Author     Karsten Keil (keil@temic-ech.spacenet.de)
 *
 * Thanks to    Elsa GmbH for documents and informations
 *
 *
 * $Log: elsa.c,v $
 * Revision 1.14  1997/04/13 19:53:25  keil
 * Fixed QS1000 init, change in IRQ check delay for SMP
 *
 * Revision 1.13  1997/04/07 22:58:07  keil
 * need include config.h
 *
 * Revision 1.12  1997/04/06 22:54:14  keil
 * Using SKB's
 *
 * Revision 1.11  1997/03/23 21:45:46  keil
 * Add support for ELSA PCMCIA
 *
 * Revision 1.10  1997/03/12 21:42:19  keil
 * Bugfix: IRQ hangs with QS1000
 *
 * Revision 1.9  1997/03/04 15:57:39  keil
 * bugfix IRQ reset Quickstep, ELSA PC changes, some stuff for new cards
 *
 * Revision 1.8  1997/01/27 15:51:48  keil
 * SMP proof,cosmetics
 *
 * Revision 1.7  1997/01/21 22:20:48  keil
 * Elsa Quickstep support
 *
 * Revision 1.6  1997/01/09 18:22:46  keil
 * one more PCC-8 fix
 *
 * Revision 1.5  1996/12/08 19:46:14  keil
 * PCC-8 correct IRQs; starting ARCOFI support
 *
 * Revision 1.4  1996/11/18 20:50:54  keil
 * with PCF Pro release 16 Byte IO
 *
 * Revision 1.3  1996/11/18 15:33:04  keil
 * PCC and PCFPro support
 *
 * Revision 1.2  1996/10/27 22:08:03  keil
 * cosmetic changes
 *
 * Revision 1.1  1996/10/13 20:04:52  keil
 * Initial revision
 *
 *
 */

#define ARCOFI_USE	0

#define __NO_VERSION__
#include <linux/config.h>
#include "siemens.h"
#include "hisax.h"
#include "elsa.h"
#include "isdnl1.h"
#include <linux/kernel_stat.h>

extern const char *CardType[];

const char *Elsa_revision = "$Revision: 1.14 $";
const char *Elsa_Types[] =
{"None", "PC", "PCC-8", "PCC-16", "PCF", "PCF-Pro",
 "PCMCIA", "QS 1000", "QS 3000"};

const char *ITACVer[] =
{"?0?", "?1?", "?2?", "?3?", "?4?", "V2.2",
 "B1", "A1"};

#define byteout(addr,val) outb_p(val,addr)
#define bytein(addr) inb_p(addr)

static inline u_char
readhscx(unsigned int adr, int hscx, u_char off)
{
	register u_char ret;
	long flags;

	save_flags(flags);
	cli();
	byteout(adr + CARD_ALE, off + (hscx ? 0x60 : 0x20));
	ret = bytein(adr + CARD_HSCX);
	restore_flags(flags);
	return (ret);
}

static inline void
read_fifo_hscx(unsigned int adr, int hscx, u_char * data, int size)
{
	/* fifo read without cli because it's allready done  */

	byteout(adr + CARD_ALE, (hscx ? 0x40 : 0));
	insb(adr + CARD_HSCX, data, size);
}


static inline void
writehscx(unsigned int adr, int hscx, u_char off, u_char data)
{
	long flags;

	save_flags(flags);
	cli();
	byteout(adr + CARD_ALE, off + (hscx ? 0x60 : 0x20));
	byteout(adr + CARD_HSCX, data);
	restore_flags(flags);
}

static inline void
write_fifo_hscx(unsigned int adr, int hscx, u_char * data, int size)
{
	/* fifo write without cli because it's allready done  */
	byteout(adr + CARD_ALE, (hscx ? 0x40 : 0));
	outsb(adr + CARD_HSCX, data, size);
}

static inline u_char
readisac(unsigned int adr, u_char off)
{
	register u_char ret;
	long flags;

	save_flags(flags);
	cli();
	byteout(adr + CARD_ALE, off + 0x20);
	ret = bytein(adr + CARD_ISAC);
	restore_flags(flags);
	return (ret);
}

static inline void
read_fifo_isac(unsigned int adr, u_char * data, int size)
{
	/* fifo read without cli because it's allready done  */

	byteout(adr + CARD_ALE, 0);
	insb(adr + CARD_ISAC, data, size);
}


static inline void
writeisac(unsigned int adr, u_char off, u_char data)
{
	long flags;

	save_flags(flags);
	cli();
	byteout(adr + CARD_ALE, off + 0x20);
	byteout(adr + CARD_ISAC, data);
	restore_flags(flags);
}

static inline void
write_fifo_isac(unsigned int adr, u_char * data, int size)
{
	/* fifo write without cli because it's allready done  */

	byteout(adr + CARD_ALE, 0);
	outsb(adr + CARD_ISAC, data, size);
}

#ifdef CONFIG_HISAX_ELSA_PCC
static inline u_char
readitac(unsigned int adr, u_char off)
{
	register u_char ret;
	long flags;

	save_flags(flags);
	cli();
	byteout(adr + CARD_ALE, off);
	ret = bytein(adr + CARD_ITAC);
	restore_flags(flags);
	return (ret);
}

static inline void
writeitac(unsigned int adr, u_char off, u_char data)
{
	long flags;

	save_flags(flags);
	cli();
	byteout(adr + CARD_ALE, off);
	byteout(adr + CARD_ITAC, data);
	restore_flags(flags);
}

static inline int
TimerRun(struct IsdnCardState *sp)
{
	register u_char val;

	val = bytein(sp->cfg_reg + CARD_CONFIG);
	if (sp->subtyp == ELSA_QS1000)
		return (0 == (val & TIMER_RUN));
	else if (sp->subtyp == ELSA_PCC8)
		return (val & TIMER_RUN_PCC8);
	return (val & TIMER_RUN);
}

static inline void
elsa_led_handler(struct IsdnCardState *sp)
{

	u_char outval = 0xf0;
	int stat = 0, cval;


	if ((sp->ph_state == 0) || (sp->ph_state == 15)) {
		stat = 1;
	} else {
		if (sp->hs[0].mode != 0)
			stat |= 2;
		if (sp->hs[1].mode != 0)
			stat |= 4;
	}
	cval = (sp->counter >> 6) & 3;
	switch (cval) {
		case 0:
			if (!stat)
				outval |= STAT_LED;
			else if (stat == 1)
				outval |= LINE_LED | STAT_LED;
			else {
				if (stat & 2)
					outval |= STAT_LED;
				if (stat & 4)
					outval |= LINE_LED;
			}
			break;
		case 1:
			if (!stat)
				outval |= LINE_LED;
			else if (stat == 1)
				outval |= LINE_LED | STAT_LED;
			else {
				if (stat & 2)
					outval |= STAT_LED;
				if (stat & 4)
					outval |= LINE_LED;
			}
			break;
		case 2:
			if (!stat)
				outval |= STAT_LED;
			else if (stat == 1)
				outval |= 0;
			else {
				if (stat & 2)
					outval |= STAT_LED;
				if (stat & 4)
					outval |= LINE_LED;
			}
			break;
		case 3:
			if (!stat)
				outval |= LINE_LED;
			break;
	}
	byteout(sp->cfg_reg + CARD_CONTROL, outval);
}
#endif

static inline void
waitforCEC(int adr, int hscx)
{
	int to = 50;

	while ((readhscx(adr, hscx, HSCX_STAR) & 0x04) && to) {
		udelay(1);
		to--;
	}
	if (!to)
		printk(KERN_WARNING "Elsa: waitforCEC timeout\n");
}


static inline void
waitforXFW(int adr, int hscx)
{
	int to = 50;

	while ((!(readhscx(adr, hscx, HSCX_STAR) & 0x44) == 0x40) && to) {
		udelay(1);
		to--;
	}
	if (!to)
		printk(KERN_WARNING "Elsa: waitforXFW timeout\n");
}

static inline void
writehscxCMDR(int adr, int hscx, u_char data)
{
	long flags;

	save_flags(flags);
	cli();
	waitforCEC(adr, hscx);
	writehscx(adr, hscx, HSCX_CMDR, data);
	restore_flags(flags);
}

/*
 * fast interrupt here
 */


static void
hscxreport(struct IsdnCardState *sp, int hscx)
{
	printk(KERN_DEBUG "HSCX %d\n", hscx);
	printk(KERN_DEBUG "ISTA %x\n", readhscx(sp->cfg_reg, hscx, HSCX_ISTA));
	printk(KERN_DEBUG "STAR %x\n", readhscx(sp->cfg_reg, hscx, HSCX_STAR));
	printk(KERN_DEBUG "EXIR %x\n", readhscx(sp->cfg_reg, hscx, HSCX_EXIR));
}

void
elsa_report(struct IsdnCardState *sp)
{
	printk(KERN_DEBUG "ISAC\n");
	printk(KERN_DEBUG "ISTA %x\n", readisac(sp->cfg_reg, ISAC_ISTA));
	printk(KERN_DEBUG "STAR %x\n", readisac(sp->cfg_reg, ISAC_STAR));
	printk(KERN_DEBUG "EXIR %x\n", readisac(sp->cfg_reg, ISAC_EXIR));
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
		writehscxCMDR(sp->cfg_reg, hsp->hscx, 0x80);
		hsp->rcvidx = 0;
		return;
	}
	ptr = hsp->rcvbuf + hsp->rcvidx;
	hsp->rcvidx += count;
	save_flags(flags);
	cli();
	read_fifo_hscx(sp->cfg_reg, hsp->hscx, ptr, count);
	writehscxCMDR(sp->cfg_reg, hsp->hscx, 0x80);
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

	waitforXFW(sp->cfg_reg, hsp->hscx);
	save_flags(flags);
	cli();
	ptr = hsp->tx_skb->data;
	skb_pull(hsp->tx_skb, count);
	hsp->tx_cnt -= count;
	hsp->count += count;
	write_fifo_hscx(sp->cfg_reg, hsp->hscx, ptr, count);
	writehscxCMDR(sp->cfg_reg, hsp->hscx, more ? 0x8 : 0xa);
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

		r = readhscx(sp->cfg_reg, hsp->hscx, HSCX_RSTA);
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
			writehscxCMDR(sp->cfg_reg, hsp->hscx, 0x80);
		} else {
			count = readhscx(sp->cfg_reg, hsp->hscx, HSCX_RBCL) & 0x1f;
			if (count == 0)
				count = 32;
			hscx_empty_fifo(hsp, count);
			if ((count = hsp->rcvidx - 1) > 0) {
				if (sp->debug & L1_DEB_HSCX_FIFO) {
					sprintf(tmp, "HX Frame %d", count);
					debugl1(sp, tmp);
				}
				if (!(skb = dev_alloc_skb(count)))
					printk(KERN_WARNING "Elsa: receive out of memory\n");
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
				printk(KERN_WARNING "elsa: receive out of memory\n");
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
		debugl1(sp, "isac_empty_fifo");

	if ((sp->rcvidx + count) >= MAX_DFRAME_LEN) {
		if (sp->debug & L1_DEB_WARN) {
			char tmp[40];
			sprintf(tmp, "isac_empty_fifo overrun %d",
				sp->rcvidx + count);
			debugl1(sp, tmp);
		}
		writeisac(sp->cfg_reg, ISAC_CMDR, 0x80);
		sp->rcvidx = 0;
		return;
	}
	ptr = sp->rcvbuf + sp->rcvidx;
	sp->rcvidx += count;
	save_flags(flags);
	cli();
	read_fifo_isac(sp->cfg_reg, ptr, count);
	writeisac(sp->cfg_reg, ISAC_CMDR, 0x80);
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
	write_fifo_isac(sp->cfg_reg, ptr, count);
	writeisac(sp->cfg_reg, ISAC_CMDR, more ? 0x8 : 0xa);
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
	writeisac(sp->cfg_reg, ISAC_CIX0, (command << 2) | 3);
}

static inline void
isac_interrupt(struct IsdnCardState *sp, u_char val)
{
	u_char exval, v1;
	struct sk_buff *skb;
	unsigned int count;
	char tmp[32];
#if ARCOFI_USE
	struct BufHeader *ibh;
	u_char *ptr;
#endif

	if (sp->debug & L1_DEB_ISAC) {
		sprintf(tmp, "ISAC interrupt %x", val);
		debugl1(sp, tmp);
	}
	if (val & 0x80) {	/* RME */
		exval = readisac(sp->cfg_reg, ISAC_RSTA);
		if ((exval & 0x70) != 0x20) {
			if (exval & 0x40)
				if (sp->debug & L1_DEB_WARN)
					debugl1(sp, "ISAC RDO");
			if (!exval & 0x20)
				if (sp->debug & L1_DEB_WARN)
					debugl1(sp, "ISAC CRC error");
			writeisac(sp->cfg_reg, ISAC_CMDR, 0x80);
		} else {
			count = readisac(sp->cfg_reg, ISAC_RBCL) & 0x1f;
			if (count == 0)
				count = 32;
			isac_empty_fifo(sp, count);
			if ((count = sp->rcvidx) > 0) {
				sp->rcvidx = 0;
				if (!(skb = alloc_skb(count, GFP_ATOMIC)))
					printk(KERN_WARNING "Elsa: D receive out of memory\n");
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
		sp->ph_state = (readisac(sp->cfg_reg, ISAC_CIX0) >> 2)
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
		exval = readisac(sp->cfg_reg, ISAC_EXIR);
		if (sp->debug & L1_DEB_WARN) {
			sprintf(tmp, "ISAC EXIR %02x", exval);
			debugl1(sp, tmp);
		}
		if (exval & 0x08) {
			v1 = readisac(sp->cfg_reg, ISAC_MOSR);
			if (sp->debug & L1_DEB_WARN) {
				sprintf(tmp, "ISAC MOSR %02x", v1);
				debugl1(sp, tmp);
			}
#if ARCOFI_USE
			if (v1 & 0x08) {
				if (!sp->mon_rx)
					if (BufPoolGet(&(sp->mon_rx), &(sp->rbufpool),
					    GFP_ATOMIC, (void *) 1, 3)) {
						if (sp->debug & L1_DEB_WARN)
							debugl1(sp, "ISAC MON RX out of buffers!");
						writeisac(sp->cfg_reg, ISAC_MOCR, 0x0a);
						goto afterMONR0;
					} else
						sp->mon_rxp = 0;
				ibh = sp->mon_rx;
				ptr = DATAPTR(ibh);
				ptr += sp->mon_rxp;
				sp->mon_rxp++;
				if (sp->mon_rxp >= 3072) {
					writeisac(sp->cfg_reg, ISAC_MOCR, 0x0a);
					sp->mon_rxp = 0;
					if (sp->debug & L1_DEB_WARN)
						debugl1(sp, "ISAC MON RX overflow!");
					goto afterMONR0;
				}
				*ptr = readisac(sp->cfg_reg, ISAC_MOR0);
				if (sp->debug & L1_DEB_WARN) {
					sprintf(tmp, "ISAC MOR0 %02x", *ptr);
					debugl1(sp, tmp);
				}
			}
		      afterMONR0:
			if (v1 & 0x80) {
				if (!sp->mon_rx)
					if (BufPoolGet(&(sp->mon_rx), &(sp->rbufpool),
					    GFP_ATOMIC, (void *) 1, 3)) {
						if (sp->debug & L1_DEB_WARN)
							debugl1(sp, "ISAC MON RX out of buffers!");
						writeisac(sp->cfg_reg, ISAC_MOCR, 0xa0);
						goto afterMONR1;
					} else
						sp->mon_rxp = 0;
				ibh = sp->mon_rx;
				ptr = DATAPTR(ibh);
				ptr += sp->mon_rxp;
				sp->mon_rxp++;
				if (sp->mon_rxp >= 3072) {
					writeisac(sp->cfg_reg, ISAC_MOCR, 0xa0);
					sp->mon_rxp = 0;
					if (sp->debug & L1_DEB_WARN)
						debugl1(sp, "ISAC MON RX overflow!");
					goto afterMONR1;
				}
				*ptr = readisac(sp->cfg_reg, ISAC_MOR1);
				if (sp->debug & L1_DEB_WARN) {
					sprintf(tmp, "ISAC MOR1 %02x", *ptr);
					debugl1(sp, tmp);
				}
			}
		      afterMONR1:
			if (v1 & 0x04) {
				writeisac(sp->cfg_reg, ISAC_MOCR, 0x0a);
				sp->mon_rx->datasize = sp->mon_rxp;
				sp->mon_flg |= MON0_RX;
			}
			if (v1 & 0x40) {
				writeisac(sp->cfg_reg, ISAC_MOCR, 0xa0);
				sp->mon_rx->datasize = sp->mon_rxp;
				sp->mon_flg |= MON1_RX;
			}
			if (v1 == 0x02) {
				ibh = sp->mon_tx;
				if (!ibh) {
					writeisac(sp->cfg_reg, ISAC_MOCR, 0x0a);
					goto AfterMOX0;
				}
				count = ibh->datasize - sp->mon_txp;
				if (count <= 0) {
					writeisac(sp->cfg_reg, ISAC_MOCR, 0x0f);
					BufPoolRelease(sp->mon_tx);
					sp->mon_tx = NULL;
					sp->mon_txp = 0;
					sp->mon_flg |= MON0_TX;
					goto AfterMOX0;
				}
				ptr = DATAPTR(ibh);
				ptr += sp->mon_txp;
				sp->mon_txp++;
				writeisac(sp->cfg_reg, ISAC_MOX0, *ptr);
			}
		      AfterMOX0:
			if (v1 == 0x20) {
				ibh = sp->mon_tx;
				if (!ibh) {
					writeisac(sp->cfg_reg, ISAC_MOCR, 0xa0);
					goto AfterMOX1;
				}
				count = ibh->datasize - sp->mon_txp;
				if (count <= 0) {
					writeisac(sp->cfg_reg, ISAC_MOCR, 0xf0);
					BufPoolRelease(sp->mon_tx);
					sp->mon_tx = NULL;
					sp->mon_txp = 0;
					sp->mon_flg |= MON1_TX;
					goto AfterMOX1;
				}
				ptr = DATAPTR(ibh);
				ptr += sp->mon_txp;
				sp->mon_txp++;
				writeisac(sp->cfg_reg, ISAC_MOX1, *ptr);
			}
		      AfterMOX1:
#endif
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
		exval = readhscx(sp->cfg_reg, 1, HSCX_EXIR);
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
				writehscxCMDR(sp->cfg_reg, hsp->hscx, 0x01);
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
		exval = readhscx(sp->cfg_reg, 0, HSCX_EXIR);
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
				writehscxCMDR(sp->cfg_reg, hsp->hscx, 0x01);
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
		exval = readhscx(sp->cfg_reg, 0, HSCX_ISTA);
		if (sp->debug & L1_DEB_HSCX) {
			sprintf(tmp, "HSCX A interrupt %x", exval);
			debugl1(sp, tmp);
		}
		hscx_interrupt(sp, exval, 0);
	}
}

static void
elsa_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *sp;
	u_char val;

	sp = (struct IsdnCardState *) dev_id;

	if (!sp) {
		printk(KERN_WARNING "Elsa: Spurious interrupt!\n");
		return;
	}
#ifdef CONFIG_HISAX_ELSA_PCC
      INT_RESTART:
	if (!TimerRun(sp)) {
		/* Timer Restart */
		byteout(sp->cfg_reg + CARD_START_TIMER, 0);
		if (!(sp->counter++ & 0x3f)) {
			/* Call LEDs all 64 tics */
			elsa_led_handler(sp);
		}
	}
#endif
	val = readhscx(sp->cfg_reg, 1, HSCX_ISTA);
      Start_HSCX:
	if (val) {
		hscx_int_main(sp, val);
	}
	val = readisac(sp->cfg_reg, ISAC_ISTA);
      Start_ISAC:
	if (val) {
		isac_interrupt(sp, val);
	}
#ifdef CONFIG_HISAX_ELSA_PCC
	if (!TimerRun(sp))
		goto INT_RESTART;
#endif
	val = readhscx(sp->cfg_reg, 1, HSCX_ISTA);
	if (val) {
		if (sp->debug & L1_DEB_HSCX)
			debugl1(sp, "HSCX IntStat after IntRoutine");
		goto Start_HSCX;
	}
	val = readisac(sp->cfg_reg, ISAC_ISTA);
	if (val) {
		if (sp->debug & L1_DEB_ISAC)
			debugl1(sp, "ISAC IntStat after IntRoutine");
		goto Start_ISAC;
	}
	writehscx(sp->cfg_reg, 0, HSCX_MASK, 0xFF);
	writehscx(sp->cfg_reg, 1, HSCX_MASK, 0xFF);
	writeisac(sp->cfg_reg, ISAC_MASK, 0xFF);
#ifdef CONFIG_HISAX_ELSA_PCC
	if (sp->subtyp == ELSA_QS1000) {
		byteout(sp->cfg_reg + CARD_START_TIMER, 0);
		byteout(sp->cfg_reg + CARD_TRIG_IRQ, 0xff);
	}
#endif
	writehscx(sp->cfg_reg, 0, HSCX_MASK, 0x0);
	writehscx(sp->cfg_reg, 1, HSCX_MASK, 0x0);
	writeisac(sp->cfg_reg, ISAC_MASK, 0x0);
}


static void
initisac(struct IsdnCardState *sp)
{
	unsigned int adr = sp->cfg_reg;

	/* Elsa IOM 2 Mode */
	writeisac(adr, ISAC_MASK, 0xff);
	writeisac(adr, ISAC_ADF2, 0x80);
	writeisac(adr, ISAC_SQXR, 0x2f);
	writeisac(adr, ISAC_SPCR, 0x00);
	writeisac(adr, ISAC_STCR, 0x70);
	writeisac(adr, ISAC_MODE, 0xc9);
	writeisac(adr, ISAC_TIMR, 0x00);
	writeisac(adr, ISAC_ADF1, 0x00);
	writeisac(adr, ISAC_CIX0, (1 << 2) | 3);
	writeisac(adr, ISAC_MASK, 0xff);
	writeisac(adr, ISAC_MASK, 0x0);
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
	writehscx(sp->cfg_reg, hscx, HSCX_CCR1, 0x85);
	writehscx(sp->cfg_reg, hscx, HSCX_XAD1, 0xFF);
	writehscx(sp->cfg_reg, hscx, HSCX_XAD2, 0xFF);
	writehscx(sp->cfg_reg, hscx, HSCX_RAH2, 0xFF);
	writehscx(sp->cfg_reg, hscx, HSCX_XBCH, 0x0);
	writehscx(sp->cfg_reg, hscx, HSCX_RLCR, 0x0);
	writehscx(sp->cfg_reg, hscx, HSCX_CCR2, 0x30);

	switch (mode) {
		case (0):
			writehscx(sp->cfg_reg, hscx, HSCX_TSAX, 0xff);
			writehscx(sp->cfg_reg, hscx, HSCX_TSAR, 0xff);
			writehscx(sp->cfg_reg, hscx, HSCX_XCCR, 7);
			writehscx(sp->cfg_reg, hscx, HSCX_RCCR, 7);
			writehscx(sp->cfg_reg, hscx, HSCX_MODE, 0x84);
			break;
		case (1):
			if (ichan == 0) {
				writehscx(sp->cfg_reg, hscx, HSCX_TSAX, 0x2f);
				writehscx(sp->cfg_reg, hscx, HSCX_TSAR, 0x2f);
			} else {
				writehscx(sp->cfg_reg, hscx, HSCX_TSAX, 0x3);
				writehscx(sp->cfg_reg, hscx, HSCX_TSAR, 0x3);
			}
			writehscx(sp->cfg_reg, hscx, HSCX_XCCR, 7);
			writehscx(sp->cfg_reg, hscx, HSCX_RCCR, 7);
			writehscx(sp->cfg_reg, hscx, HSCX_MODE, 0xe4);
			writehscx(sp->cfg_reg, hscx, HSCX_CMDR, 0x41);
			break;
		case (2):
			if (ichan == 0) {
				writehscx(sp->cfg_reg, hscx, HSCX_TSAX, 0x2f);
				writehscx(sp->cfg_reg, hscx, HSCX_TSAR, 0x2f);
			} else {
				writehscx(sp->cfg_reg, hscx, HSCX_TSAX, 0x3);
				writehscx(sp->cfg_reg, hscx, HSCX_TSAR, 0x3);
			}
			writehscx(sp->cfg_reg, hscx, HSCX_XCCR, 7);
			writehscx(sp->cfg_reg, hscx, HSCX_RCCR, 7);
			writehscx(sp->cfg_reg, hscx, HSCX_MODE, 0x8c);
			writehscx(sp->cfg_reg, hscx, HSCX_CMDR, 0x41);
			break;
	}
	writehscx(sp->cfg_reg, hscx, HSCX_ISTA, 0x00);
}

void
release_io_elsa(struct IsdnCard *card)
{
	int bytecnt = 8;

	if (card->sp->subtyp == ELSA_PCFPRO)
		bytecnt = 16;
	if (card->sp->cfg_reg)
		release_region(card->sp->cfg_reg, bytecnt);
}

static void
reset_elsa(struct IsdnCardState *sp)
{
#ifdef CONFIG_HISAX_ELSA_PCC
	/* Wait 1 Timer */
	byteout(sp->cfg_reg + CARD_START_TIMER, 0);
	while (TimerRun(sp));
	byteout(sp->cfg_reg + CARD_CONTROL, 0x00);	/* Reset On */
	/* Wait 1 Timer */
	byteout(sp->cfg_reg + CARD_START_TIMER, 0);
	while (TimerRun(sp));
	byteout(sp->cfg_reg + CARD_CONTROL, ISDN_RESET);	/* Reset Off */
	/* Wait 1 Timer */
	byteout(sp->cfg_reg + CARD_START_TIMER, 0);
	while (TimerRun(sp));
	byteout(sp->cfg_reg + CARD_TRIG_IRQ, 0xff);
#endif
}

static void
clear_pending_ints(struct IsdnCardState *sp)
{
#ifdef CONFIG_HISAX_ELSA_PCMCIA
	int val;
	char tmp[64];

	val = readhscx(sp->cfg_reg, 1, HSCX_ISTA);
	sprintf(tmp, "HSCX B ISTA %x", val);
	debugl1(sp, tmp);
	if (val & 0x01) {
		val = readhscx(sp->cfg_reg, 1, HSCX_EXIR);
		sprintf(tmp, "HSCX B EXIR %x", val);
		debugl1(sp, tmp);
	} else if (val & 0x02) {
		val = readhscx(sp->cfg_reg, 0, HSCX_EXIR);
		sprintf(tmp, "HSCX A EXIR %x", val);
		debugl1(sp, tmp);
	}
	val = readhscx(sp->cfg_reg, 0, HSCX_ISTA);
	sprintf(tmp, "HSCX A ISTA %x", val);
	debugl1(sp, tmp);
	val = readhscx(sp->cfg_reg, 1, HSCX_STAR);
	sprintf(tmp, "HSCX B STAR %x", val);
	debugl1(sp, tmp);
	val = readhscx(sp->cfg_reg, 0, HSCX_STAR);
	sprintf(tmp, "HSCX A STAR %x", val);
	debugl1(sp, tmp);
	val = readisac(sp->cfg_reg, ISAC_STAR);
	sprintf(tmp, "ISAC STAR %x", val);
	debugl1(sp, tmp);
	val = readisac(sp->cfg_reg, ISAC_MODE);
	sprintf(tmp, "ISAC MODE %x", val);
	debugl1(sp, tmp);
	val = readisac(sp->cfg_reg, ISAC_ADF2);
	sprintf(tmp, "ISAC ADF2 %x", val);
	debugl1(sp, tmp);
	val = readisac(sp->cfg_reg, ISAC_ISTA);
	sprintf(tmp, "ISAC ISTA %x", val);
	debugl1(sp, tmp);
	if (val & 0x01) {
		val = readisac(sp->cfg_reg, ISAC_EXIR);
		sprintf(tmp, "ISAC EXIR %x", val);
		debugl1(sp, tmp);
	} else if (val & 0x04) {
		val = readisac(sp->cfg_reg, ISAC_CIR0);
		sprintf(tmp, "ISAC CIR0 %x", val);
		debugl1(sp, tmp);
	}
#endif
	writehscx(sp->cfg_reg, 0, HSCX_MASK, 0xFF);
	writehscx(sp->cfg_reg, 1, HSCX_MASK, 0xFF);
	writeisac(sp->cfg_reg, ISAC_MASK, 0xFF);
#ifdef CONFIG_HISAX_ELSA_PCC
	if (sp->subtyp == ELSA_QS1000) {
		byteout(sp->cfg_reg + CARD_START_TIMER, 0);
		byteout(sp->cfg_reg + CARD_TRIG_IRQ, 0xff);
	}
#endif
	writehscx(sp->cfg_reg, 0, HSCX_MASK, 0x0);
	writehscx(sp->cfg_reg, 1, HSCX_MASK, 0x0);
	writeisac(sp->cfg_reg, ISAC_MASK, 0x0);
	writeisac(sp->cfg_reg, ISAC_CMDR, 0x41);
}

static void
check_arcofi(struct IsdnCardState *sp)
{
#if 0
	u_char val;
	char tmp[40];
	char *t;
	long flags;
	u_char *p;

	if (BufPoolGet(&(sp->mon_tx), &(sp->sbufpool),
		       GFP_ATOMIC, (void *) 1, 3)) {
		if (sp->debug & L1_DEB_WARN)
			debugl1(sp, "ISAC MON TX out of buffers!");
		return;
	} else
		sp->mon_txp = 0;
	p = DATAPTR(sp->mon_tx);
	*p++ = 0xa0;
	*p++ = 0x0;
	sp->mon_tx->datasize = 2;
	sp->mon_txp = 1;
	sp->mon_flg = 0;
	writeisac(sp->cfg_reg, ISAC_MOCR, 0xa0);
	val = readisac(sp->cfg_reg, ISAC_MOSR);
	writeisac(sp->cfg_reg, ISAC_MOX1, 0xa0);
	writeisac(sp->cfg_reg, ISAC_MOCR, 0xb0);
	save_flags(flags);
	sti();
	HZDELAY(3);
	restore_flags(flags);
	if (sp->mon_flg & MON1_TX) {
		if (sp->mon_flg & MON1_RX) {
			sprintf(tmp, "Arcofi response received %d bytes", sp->mon_rx->datasize);
			debugl1(sp, tmp);
			p = DATAPTR(sp->mon_rx);
			t = tmp;
			t += sprintf(tmp, "Arcofi data");
			QuickHex(t, p, sp->mon_rx->datasize);
			debugl1(sp, tmp);
			BufPoolRelease(sp->mon_rx);
			sp->mon_rx = NULL;
			sp->mon_rxp = 0;
			sp->mon_flg = 0;
		}
	} else if (sp->mon_tx) {
		BufPoolRelease(sp->mon_tx);
		sp->mon_tx = NULL;
		sp->mon_txp = 0;
		sprintf(tmp, "Arcofi not detected");
		debugl1(sp, tmp);
	}
	sp->mon_flg = 0;
#endif
}

int
initelsa(struct IsdnCardState *sp)
{
	int ret, irq_cnt, cnt = 3;
	long flags;

	irq_cnt = kstat.interrupts[sp->irq];
	printk(KERN_INFO "Elsa: IRQ %d count %d\n", sp->irq, irq_cnt);
	ret = get_irq(sp->cardnr, &elsa_interrupt);
#ifdef CONFIG_HISAX_ELSA_PCC
	byteout(sp->cfg_reg + CARD_TRIG_IRQ, 0xff);
#endif
	while (ret && cnt) {
		sp->counter = 0;
		clear_pending_ints(sp);
		initisac(sp);
		sp->modehscx(sp->hs, 0, 0);
		sp->modehscx(sp->hs + 1, 0, 0);
		save_flags(flags);
		sp->counter = 0;
		sti();
#ifdef CONFIG_HISAX_ELSA_PCC
		byteout(sp->cfg_reg + CARD_CONTROL, ISDN_RESET | ENABLE_TIM_INT);
		byteout(sp->cfg_reg + CARD_START_TIMER, 0);
		current->state = TASK_INTERRUPTIBLE;
		current->timeout = jiffies + (110 * HZ) / 1000;		/* Timeout 110ms */
		schedule();
		restore_flags(flags);
		printk(KERN_INFO "Elsa: %d timer tics in 110 msek\n",
		       sp->counter);
		if (abs(sp->counter - 13) < 3) {
			printk(KERN_INFO "Elsa: timer and irq OK\n");
		} else {
			printk(KERN_WARNING
			       "Elsa: timer tic problem (%d/12) maybe an IRQ(%d) conflict\n",
			       sp->counter, sp->irq);
		}
#endif
		printk(KERN_INFO "Elsa: IRQ %d count %d\n", sp->irq,
		       kstat.interrupts[sp->irq]);
		if (kstat.interrupts[sp->irq] == irq_cnt) {
			printk(KERN_WARNING
			       "Elsa: IRQ(%d) getting no interrupts during init %d\n",
			       sp->irq, 4 - cnt);
			if (cnt == 1) {
				free_irq(sp->irq, sp);
				return (0);
			} else {
				reset_elsa(sp);
				cnt--;
			}
		} else {
			check_arcofi(sp);
			cnt = 0;
		}
	}
	sp->counter = 0;
	return (ret);
}

#ifdef CONFIG_HISAX_ELSA_PCC
static unsigned char
probe_elsa_adr(unsigned int adr)
{
	int i, in1, in2, p16_1 = 0, p16_2 = 0, p8_1 = 0, p8_2 = 0, pc_1 = 0,
	 pc_2 = 0, pfp_1 = 0, pfp_2 = 0;
	long flags;

	if (check_region(adr, 8)) {
		printk(KERN_WARNING
		       "Elsa: Probing Port 0x%x: already in use\n",
		       adr);
		return (0);
	}
	save_flags(flags);
	cli();
	for (i = 0; i < 16; i++) {
		in1 = inb(adr + CARD_CONFIG);	/* 'toggelt' bei */
		in2 = inb(adr + CARD_CONFIG);	/* jedem Zugriff */
		p16_1 += 0x04 & in1;
		p16_2 += 0x04 & in2;
		p8_1 += 0x02 & in1;
		p8_2 += 0x02 & in2;
		pc_1 += 0x01 & in1;
		pc_2 += 0x01 & in2;
		pfp_1 += 0x40 & in1;
		pfp_2 += 0x40 & in2;
	}
	restore_flags(flags);
	printk(KERN_INFO "Elsa: Probing IO 0x%x", adr);
	if (65 == ++p16_1 * ++p16_2) {
		printk(" PCC-16/PCF found\n");
		return (ELSA_PCC16);
	} else if (1025 == ++pfp_1 * ++pfp_2) {
		printk(" PCF-Pro found\n");
		return (ELSA_PCFPRO);
	} else if (33 == ++p8_1 * ++p8_2) {
		printk(" PCC8 found\n");
		return (ELSA_PCC8);
	} else if (17 == ++pc_1 * ++pc_2) {
		printk(" PC found\n");
		return (ELSA_PC);
	} else {
		printk(" failed\n");
		return (0);
	}
}

static unsigned int
probe_elsa(struct IsdnCardState *sp)
{
	int i;
	unsigned int CARD_portlist[] =
	{0x160, 0x170, 0x260, 0x360, 0};

	for (i = 0; CARD_portlist[i]; i++) {
		if ((sp->subtyp = probe_elsa_adr(CARD_portlist[i])))
			break;
	}
	return (CARD_portlist[i]);
}
#endif

int
setup_elsa(struct IsdnCard *card)
{
#ifdef CONFIG_HISAX_ELSA_PCC
	long flags;
#endif
	int bytecnt;
	u_char val, verA, verB;
	struct IsdnCardState *sp = card->sp;
	char tmp[64];

	strcpy(tmp, Elsa_revision);
	printk(KERN_NOTICE "HiSax: Elsa driver Rev. %s\n", HiSax_getrev(tmp));
#ifdef CONFIG_HISAX_ELSA_PCC
	if (sp->typ == ISDN_CTYPE_ELSA) {
		sp->cfg_reg = card->para[0];
		printk(KERN_INFO "Elsa: Microlink IO probing\n");
		if (sp->cfg_reg) {
			if (!(sp->subtyp = probe_elsa_adr(sp->cfg_reg))) {
				printk(KERN_WARNING
				     "Elsa: no Elsa Microlink at 0x%x\n",
				       sp->cfg_reg);
				return (0);
			}
		} else
			sp->cfg_reg = probe_elsa(sp);
		if (sp->cfg_reg) {
			val = bytein(sp->cfg_reg + CARD_CONFIG);
			if (sp->subtyp == ELSA_PC) {
				const u_char CARD_IrqTab[8] =
				{7, 3, 5, 9, 0, 0, 0, 0};
				sp->irq = CARD_IrqTab[(val & IRQ_INDEX_PC) >> 2];
			} else if (sp->subtyp == ELSA_PCC8) {
				const u_char CARD_IrqTab[8] =
				{7, 3, 5, 9, 0, 0, 0, 0};
				sp->irq = CARD_IrqTab[(val & IRQ_INDEX_PCC8) >> 4];
			} else {
				const u_char CARD_IrqTab[8] =
				{15, 10, 15, 3, 11, 5, 11, 9};
				sp->irq = CARD_IrqTab[(val & IRQ_INDEX) >> 3];
			}
			val = bytein(sp->cfg_reg + CARD_ALE) & 0x7;
			if (val < 3)
				val |= 8;
			val += 'A' - 3;
			if (val == 'B' || val == 'C')
				val ^= 1;
			if ((sp->subtyp == ELSA_PCFPRO) && (val = 'G'))
				val = 'C';
			printk(KERN_INFO
			       "Elsa: %s found at 0x%x Rev.:%c IRQ %d\n",
			       Elsa_Types[sp->subtyp],
			       sp->cfg_reg,
			       val, sp->irq);
			val = bytein(sp->cfg_reg + CARD_ALE) & 0x08;
			if (val)
				printk(KERN_WARNING
				   "Elsa: Microlink S0 bus power bad\n");
		} else {
			printk(KERN_WARNING
			       "No Elsa Microlink found\n");
			return (0);
		}
	} else if (sp->typ == ISDN_CTYPE_ELSA_QS1000) {
		sp->cfg_reg = card->para[1];
		sp->irq = card->para[0];
		sp->subtyp = ELSA_QS1000;
		printk(KERN_INFO
		       "Elsa: %s found at 0x%x IRQ %d\n",
		       Elsa_Types[sp->subtyp],
		       sp->cfg_reg,
		       sp->irq);
	} else
		return (0);
#endif
#ifdef CONFIG_HISAX_ELSA_PCMCIA
	if (sp->typ == ISDN_CTYPE_ELSA_QS1000) {
		sp->cfg_reg = card->para[1];
		sp->irq = card->para[0];
		sp->subtyp = ELSA_PCMCIA;
		printk(KERN_INFO
		       "Elsa: %s found at 0x%x IRQ %d\n",
		       Elsa_Types[sp->subtyp],
		       sp->cfg_reg,
		       sp->irq);
	} else
		return (0);
#endif

	switch (sp->subtyp) {
		case ELSA_PC:
			bytecnt = 8;
			break;
		case ELSA_PCC8:
			bytecnt = 8;
			break;
		case ELSA_PCFPRO:
			bytecnt = 16;
			break;
		case ELSA_PCC16:
			bytecnt = 8;
			break;
		case ELSA_PCF:
			bytecnt = 16;
			break;
		case ELSA_QS1000:
			bytecnt = 8;
			break;
		case ELSA_PCMCIA:
			bytecnt = 8;
			break;
		default:
			printk(KERN_WARNING
			       "Unknown ELSA subtype %d\n", sp->subtyp);
			return (0);
	}

	if (check_region((sp->cfg_reg), bytecnt)) {
		printk(KERN_WARNING
		       "HiSax: %s config port %x-%x already in use\n",
		       CardType[card->typ],
		       sp->cfg_reg,
		       sp->cfg_reg + bytecnt);
		return (0);
	} else {
		request_region(sp->cfg_reg, bytecnt, "elsa isdn");
	}

	/* Teste Timer */
#ifdef CONFIG_HISAX_ELSA_PCC
	byteout(sp->cfg_reg + CARD_TRIG_IRQ, 0xff);
	byteout(sp->cfg_reg + CARD_START_TIMER, 0);
	if (!TimerRun(sp)) {
		byteout(sp->cfg_reg + CARD_START_TIMER, 0);	/* 2. Versuch */
		if (!TimerRun(sp)) {
			printk(KERN_WARNING
			       "Elsa: timer do not start\n");
			release_io_elsa(card);
			return (0);
		}
	}
	save_flags(flags);
	sti();
	HZDELAY(1);		/* wait >=10 ms */
	restore_flags(flags);
	if (TimerRun(sp)) {
		printk(KERN_WARNING "Elsa: timer do not run down\n");
		release_io_elsa(card);
		return (0);
	}
	printk(KERN_INFO "Elsa: timer OK; resetting card\n");
	reset_elsa(sp);
#endif
	verA = readhscx(sp->cfg_reg, 0, HSCX_VSTR) & 0xf;
	verB = readhscx(sp->cfg_reg, 1, HSCX_VSTR) & 0xf;
	printk(KERN_INFO "Elsa: HSCX version A: %s  B: %s\n",
	       HscxVersion(verA), HscxVersion(verB));
	val = readisac(sp->cfg_reg, ISAC_RBCH);
	printk(KERN_INFO "Elsa: ISAC %s\n",
	       ISACVersion(val));

#ifdef CONFIG_HISAX_ELSA_PCMCIA
	if ((verA == 0) | (verA == 0xf) | (verB == 0) | (verB == 0xf)) {
		printk(KERN_WARNING
		       "Elsa: wrong HSCX versions check IO address\n");
		release_io_elsa(card);
		return (0);
	}
#endif

#ifdef CONFIG_HISAX_ELSA_PCC
	if (sp->subtyp == ELSA_PC) {
		val = readitac(sp->cfg_reg, ITAC_SYS);
		printk(KERN_INFO "Elsa: ITAC version %s\n", ITACVer[val & 7]);
		writeitac(sp->cfg_reg, ITAC_ISEN, 0);
		writeitac(sp->cfg_reg, ITAC_RFIE, 0);
		writeitac(sp->cfg_reg, ITAC_XFIE, 0);
		writeitac(sp->cfg_reg, ITAC_SCIE, 0);
		writeitac(sp->cfg_reg, ITAC_STIE, 0);
	}
#endif
	sp->modehscx = &modehscx;
	sp->ph_command = &ph_command;
	sp->hscx_fill_fifo = &hscx_fill_fifo;
	sp->isac_fill_fifo = &isac_fill_fifo;

	return (1);
}
