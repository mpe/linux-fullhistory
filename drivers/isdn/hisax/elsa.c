/* $Id: elsa.c,v 1.8 1997/01/27 15:51:48 keil Exp $

 * elsa.c     low level stuff for Elsa isdn cards
 *
 * Author     Karsten Keil (keil@temic-ech.spacenet.de)
 *
 * Thanks to    Elsa GmbH for documents and informations
 *
 *
 * $Log: elsa.c,v $
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
#include "siemens.h"
#include "hisax.h"
#include "elsa.h"
#include "isdnl1.h"
#include <linux/kernel_stat.h>

extern const char *CardType[];

const char *Elsa_revision = "$Revision: 1.8 $";
const char *Elsa_Types[] =
{"None", "PCC-8", "PCF-Pro", "PCC-16", "PCF",
 "QS 1000"};

#define byteout(addr,val) outb_p(val,addr)
#define bytein(addr) inb_p(addr)

static inline byte
readhscx(unsigned int adr, int hscx, byte off)
{
	register byte ret;
	long flags;

	save_flags(flags);
	cli();
	byteout(adr + CARD_ALE, off + (hscx ? 0x60 : 0x20));
	ret = bytein(adr + CARD_HSCX);
	restore_flags(flags);
	return (ret);
}

static inline void
read_fifo_hscx(unsigned int adr, int hscx, byte * data, int size)
{
	/* fifo read without cli because it's allready done  */

	byteout(adr + CARD_ALE, (hscx ? 0x40 : 0));
	insb(adr + CARD_HSCX, data, size);
}


static inline void
writehscx(unsigned int adr, int hscx, byte off, byte data)
{
	long flags;

	save_flags(flags);
	cli();
	byteout(adr + CARD_ALE, off + (hscx ? 0x60 : 0x20));
	byteout(adr + CARD_HSCX, data);
	restore_flags(flags);
}

static inline void
write_fifo_hscx(unsigned int adr, int hscx, byte * data, int size)
{
	/* fifo write without cli because it's allready done  */
	byteout(adr + CARD_ALE, (hscx ? 0x40 : 0));
	outsb(adr + CARD_HSCX, data, size);
}

static inline byte
readisac(unsigned int adr, byte off)
{
	register byte ret;
	long flags;

	save_flags(flags);
	cli();
	byteout(adr + CARD_ALE, off + 0x20);
	ret = bytein(adr);
	restore_flags(flags);
	return (ret);
}

static inline void
read_fifo_isac(unsigned int adr, byte * data, int size)
{
	/* fifo read without cli because it's allready done  */

	byteout(adr + CARD_ALE, 0);
	insb(adr, data, size);
}


static inline void
writeisac(unsigned int adr, byte off, byte data)
{
	long flags;

	save_flags(flags);
	cli();
	byteout(adr + CARD_ALE, off + 0x20);
	byteout(adr, data);
	restore_flags(flags);
}

static inline void
write_fifo_isac(unsigned int adr, byte * data, int size)
{
	/* fifo write without cli because it's allready done  */

	byteout(adr + CARD_ALE, 0);
	outsb(adr, data, size);
}

static inline int
TimerRun(struct IsdnCardState *sp)
{
	register byte val;

	val = bytein(sp->cfg_reg + CARD_CONFIG);
	if (sp->subtyp == ELSA_QS1000)
		return (0 == (val & TIMER_RUN));
	return ((val & TIMER_RUN));
}

static inline void
elsa_led_handler(struct IsdnCardState *sp)
{

	byte outval = 0xf0;
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
writehscxCMDR(int adr, int hscx, byte data)
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
	byte *ptr;
	struct IsdnCardState *sp = hsp->sp;
	struct BufHeader *ibh = hsp->rcvibh;
	long flags;

	if ((sp->debug & L1_DEB_HSCX) && !(sp->debug & L1_DEB_HSCX_FIFO))
		debugl1(sp, "hscx_empty_fifo");

	if (hsp->rcvptr + count > BUFFER_SIZE(HSCX_RBUF_ORDER,
					      HSCX_RBUF_BPPS)) {
		if (sp->debug & L1_DEB_WARN)
			debugl1(sp, "hscx_empty_fifo: incoming packet too large");
		writehscxCMDR(sp->cfg_reg, hsp->hscx, 0x80);
		return;
	}
	ptr = DATAPTR(ibh);
	ptr += hsp->rcvptr;

	hsp->rcvptr += count;
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
	struct BufHeader *ibh;
	int more, count;
	byte *ptr;
	long flags;

	if ((sp->debug & L1_DEB_HSCX) && !(sp->debug & L1_DEB_HSCX_FIFO))
		debugl1(sp, "hscx_fill_fifo");

	ibh = hsp->xmtibh;
	if (!ibh)
		return;

	count = ibh->datasize - hsp->sendptr;
	if (count <= 0)
		return;

	more = (hsp->mode == 1) ? 1 : 0;
	if (count > 32) {
		more = !0;
		count = 32;
	}
	ptr = DATAPTR(ibh);
	ptr += hsp->sendptr;
	hsp->sendptr += count;

	waitforXFW(sp->cfg_reg, hsp->hscx);
	save_flags(flags);
	cli();
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
hscx_interrupt(struct IsdnCardState *sp, byte val, byte hscx)
{
	byte r;
	struct HscxState *hsp = sp->hs + hscx;
	int count, err;
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
			if (hsp->rcvibh)
				BufPoolRelease(hsp->rcvibh);
			hsp->rcvibh = NULL;
			writehscxCMDR(sp->cfg_reg, hsp->hscx, 0x80);
			goto afterRME;
		}
		if (!hsp->rcvibh)
			if (BufPoolGet(&hsp->rcvibh, &hsp->rbufpool,
				       GFP_ATOMIC, (void *) 1, 1)) {
				if (sp->debug & L1_DEB_WARN)
					debugl1(sp, "HSCX RME out of buffers");
				writehscxCMDR(sp->cfg_reg, hsp->hscx, 0x80);
				goto afterRME;
			} else
				hsp->rcvptr = 0;

		count = readhscx(sp->cfg_reg, hsp->hscx, HSCX_RBCL) & 0x1f;
		if (count == 0)
			count = 32;
		hscx_empty_fifo(hsp, count);
		hsp->rcvibh->datasize = hsp->rcvptr - 1;
		BufQueueLink(&hsp->rq, hsp->rcvibh);
		hsp->rcvibh = NULL;
		hscx_sched_event(hsp, HSCX_RCVBUFREADY);
	}
      afterRME:
	if (val & 0x40) {	/* RPF */
		if (!hsp->rcvibh) {
			if (hsp->mode == 1)
				err = BufPoolGet(&hsp->rcvibh, &hsp->smallpool,
					      GFP_ATOMIC, (void *) 1, 2);
			else
				err = BufPoolGet(&hsp->rcvibh, &hsp->rbufpool,
					      GFP_ATOMIC, (void *) 1, 2);

			if (err) {
				if (sp->debug & L1_DEB_WARN)
					debugl1(sp, "HSCX RPF out of buffers");
				writehscxCMDR(sp->cfg_reg, hsp->hscx, 0x80);
				goto afterRPF;
			} else
				hsp->rcvptr = 0;
		}
		hscx_empty_fifo(hsp, 32);
		if (hsp->mode == 1) {
			/* receive audio data */
			hsp->rcvibh->datasize = hsp->rcvptr;
			BufQueueLink(&hsp->rq, hsp->rcvibh);
			hsp->rcvibh = NULL;
			hscx_sched_event(hsp, HSCX_RCVBUFREADY);
		}
	}
      afterRPF:
	if (val & 0x10) {	/* XPR */
		if (hsp->xmtibh)
			if (hsp->xmtibh->datasize > hsp->sendptr) {
				hscx_fill_fifo(hsp);
				goto afterXPR;
			} else {
				if (hsp->releasebuf)
					BufPoolRelease(hsp->xmtibh);
				hsp->sendptr = 0;
				if (hsp->st->l4.l1writewakeup)
					hsp->st->l4.l1writewakeup(hsp->st);
				hsp->xmtibh = NULL;
			}
		if (!BufQueueUnlink(&hsp->xmtibh, &hsp->sq)) {
			hsp->releasebuf = !0;
			hscx_fill_fifo(hsp);
		} else
			hscx_sched_event(hsp, HSCX_XMTBUFREADY);
	}
      afterXPR:
}

/*
 * ISAC stuff goes here
 */

static void
isac_empty_fifo(struct IsdnCardState *sp, int count)
{
	byte *ptr;
	struct BufHeader *ibh = sp->rcvibh;
	long flags;

	if ((sp->debug & L1_DEB_ISAC) && !(sp->debug & L1_DEB_ISAC_FIFO))
		debugl1(sp, "isac_empty_fifo");

	if (sp->rcvptr >= 3072) {
		if (sp->debug & L1_DEB_WARN) {
			char tmp[40];
			sprintf(tmp, "isac_empty_fifo rcvptr %d", sp->rcvptr);
			debugl1(sp, tmp);
		}
		return;
	}
	ptr = DATAPTR(ibh);
	ptr += sp->rcvptr;
	sp->rcvptr += count;

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
	struct BufHeader *ibh;
	int count, more;
	byte *ptr;
	long flags;

	if ((sp->debug & L1_DEB_ISAC) && !(sp->debug & L1_DEB_ISAC_FIFO))
		debugl1(sp, "isac_fill_fifo");

	ibh = sp->xmtibh;
	if (!ibh)
		return;

	count = ibh->datasize - sp->sendptr;
	if (count <= 0)
		return;
	if (count >= 3072)
		return;

	more = 0;
	if (count > 32) {
		more = !0;
		count = 32;
	}
	ptr = DATAPTR(ibh);
	ptr += sp->sendptr;
	sp->sendptr += count;

	save_flags(flags);
	cli();
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
isac_interrupt(struct IsdnCardState *sp, byte val)
{
	byte exval, v1;
	unsigned int count;
	char tmp[32];
#if ARCOFI_USE
	struct BufHeader *ibh;
	byte *ptr;
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
			if (sp->rcvibh)
				BufPoolRelease(sp->rcvibh);
			sp->rcvibh = NULL;
			writeisac(sp->cfg_reg, ISAC_CMDR, 0x80);
			goto afterRME;
		}
		if (!sp->rcvibh)
			if (BufPoolGet(&(sp->rcvibh), &(sp->rbufpool),
				       GFP_ATOMIC, (void *) 1, 3)) {
				if (sp->debug & L1_DEB_WARN)
					debugl1(sp, "ISAC RME out of buffers!");
				writeisac(sp->cfg_reg, ISAC_CMDR, 0x80);
				goto afterRME;
			} else
				sp->rcvptr = 0;
		count = readisac(sp->cfg_reg, ISAC_RBCL) & 0x1f;
		if (count == 0)
			count = 32;
		isac_empty_fifo(sp, count);
		sp->rcvibh->datasize = sp->rcvptr;
		BufQueueLink(&(sp->rq), sp->rcvibh);
		sp->rcvibh = NULL;
		isac_sched_event(sp, ISAC_RCVBUFREADY);
	}
      afterRME:
	if (val & 0x40) {	/* RPF */
		if (!sp->rcvibh)
			if (BufPoolGet(&(sp->rcvibh), &(sp->rbufpool),
				       GFP_ATOMIC, (void *) 1, 4)) {
				if (sp->debug & L1_DEB_WARN)
					debugl1(sp, "ISAC RME out of buffers!");
				writeisac(sp->cfg_reg, ISAC_CMDR, 0x80);
				goto afterRPF;
			} else
				sp->rcvptr = 0;
		isac_empty_fifo(sp, 32);
	}
      afterRPF:
	if (val & 0x20) {	/* RSC */
		/* never */
		if (sp->debug & L1_DEB_WARN)
			debugl1(sp, "ISAC RSC interrupt");
	}
	if (val & 0x10) {	/* XPR */
		if (sp->xmtibh)
			if (sp->xmtibh->datasize > sp->sendptr) {
				isac_fill_fifo(sp);
				goto afterXPR;
			} else {
				if (sp->releasebuf)
					BufPoolRelease(sp->xmtibh);
				sp->xmtibh = NULL;
				sp->sendptr = 0;
			}
		if (!BufQueueUnlink(&sp->xmtibh, &sp->sq)) {
			sp->releasebuf = !0;
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
hscx_int_main(struct IsdnCardState *sp, byte val)
{

	byte exval;
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
				hsp->sendptr = 0;
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
				hsp->sendptr = 0;
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
	byte val, sval, stat = 0;

	sp = (struct IsdnCardState *) irq2dev_map[intno];

	if (!sp) {
		printk(KERN_WARNING "Elsa: Spurious interrupt!\n");
		return;
	}
	sval = bytein(sp->cfg_reg + CARD_CONFIG);
      INT_RESTART:
	if (!TimerRun(sp)) {
		/* Timer Restart */
		bytein(sp->cfg_reg + CARD_START_TIMER);
		if (!(sp->counter++ & 0x3f)) {
			/* Call LEDs all 64 tics */
			elsa_led_handler(sp);
		}
	}
	val = readhscx(sp->cfg_reg, 1, HSCX_ISTA);
      Start_HSCX:
	if (val) {
		hscx_int_main(sp, val);
		stat |= 1;
	}
	val = readisac(sp->cfg_reg, ISAC_ISTA);
      Start_ISAC:
	if (val) {
		isac_interrupt(sp, val);
		stat |= 2;
	}
	sval = bytein(sp->cfg_reg + CARD_CONFIG);
	if (!TimerRun(sp))
		goto INT_RESTART;

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
	if (stat & 1) {
		writehscx(sp->cfg_reg, 0, HSCX_MASK, 0xFF);
		writehscx(sp->cfg_reg, 1, HSCX_MASK, 0xFF);
		writehscx(sp->cfg_reg, 0, HSCX_MASK, 0x0);
		writehscx(sp->cfg_reg, 1, HSCX_MASK, 0x0);
	}
	if (stat & 2) {
		writeisac(sp->cfg_reg, ISAC_MASK, 0xFF);
		writeisac(sp->cfg_reg, ISAC_MASK, 0x0);
	}
	byteout(sp->cfg_reg + 7, 0xff);
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
clear_pending_ints(struct IsdnCardState *sp)
{
	writeisac(sp->cfg_reg, ISAC_MASK, 0);
	writeisac(sp->cfg_reg, ISAC_CMDR, 0x41);
}

static void
check_arcofi(struct IsdnCardState *sp)
{
#if 0
	byte val;
	char tmp[40];
	char *t;
	long flags;
	byte *p;

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
	int ret, irq_cnt;
	long flags;

	sp->counter = 0;
	irq_cnt = kstat.interrupts[sp->irq];
	printk(KERN_INFO "Elsa: IRQ %d count %d\n", sp->irq, irq_cnt);
	clear_pending_ints(sp);
	ret = get_irq(sp->cardnr, &elsa_interrupt);
	if (ret) {
		initisac(sp);
		sp->modehscx(sp->hs, 0, 0);
		sp->modehscx(sp->hs + 1, 0, 0);
		save_flags(flags);
		sp->counter = 0;
		sti();
		byteout(sp->cfg_reg + CARD_CONTROL, ISDN_RESET | ENABLE_TIM_INT);
		bytein(sp->cfg_reg + CARD_START_TIMER);
		HZDELAY(11);	/* Warte 110 ms */
		restore_flags(flags);
		printk(KERN_INFO "Elsa: %d timer tics in 110 msek\n",
		       sp->counter);
		if (abs(sp->counter - 12) < 3) {
			printk(KERN_INFO "Elsa: timer and irq OK\n");
		} else {
			printk(KERN_WARNING
			"Elsa: timer problem maybe an IRQ(%d) conflict\n",
			       sp->irq);
		}
		printk(KERN_INFO "Elsa: IRQ %d count %d\n", sp->irq,
		       kstat.interrupts[sp->irq]);
		if (kstat.interrupts[sp->irq] == irq_cnt) {
			printk(KERN_WARNING
			       "Elsa: IRQ(%d) getting no interrupts during init\n",
			       sp->irq);
			irq2dev_map[sp->irq] = NULL;
			free_irq(sp->irq, NULL);
			return (0);
		}
		check_arcofi(sp);
	}
	sp->counter = 0;
	return (ret);
}

static unsigned char
probe_elsa_adr(unsigned int adr)
{
	int i, in1, in2, p16_1 = 0, p16_2 = 0, pcc_1 = 0, pcc_2 = 0,
	 pfp_1 = 0, pfp_2 = 0;
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
		pcc_1 += 0x01 & in1;
		pcc_2 += 0x01 & in2;
		pfp_1 += 0x40 & in1;
		pfp_2 += 0x40 & in2;
	}
	restore_flags(flags);
	printk(KERN_INFO "Elsa: Probing IO 0x%x", adr);
	if (65 == ++p16_1 * ++p16_2) {
		printk(" PCC-16/PCF found\n");
		return (3);
	} else if (1025 == ++pfp_1 * ++pfp_2) {
		printk(" PCF-Pro found\n");
		return (2);
	} else if (17 == ++pcc_1 * ++pcc_2) {
		printk(" PCC8 found\n");
		return (1);
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

int
setup_elsa(struct IsdnCard *card)
{
	long flags;
	int bytecnt;
	byte val, verA, verB;
	struct IsdnCardState *sp = card->sp;
	char tmp[64];

	strcpy(tmp, Elsa_revision);
	printk(KERN_NOTICE "HiSax: Elsa driver Rev. %s\n", HiSax_getrev(tmp));
	if (sp->typ == ISDN_CTYPE_ELSA) {
		sp->cfg_reg = card->para[0];
		printk(KERN_INFO "Elsa: Mircolink IO probing\n");
		if (sp->cfg_reg) {
			if (!(sp->subtyp = probe_elsa_adr(sp->cfg_reg))) {
				printk(KERN_WARNING
				     "Elsa: no Elsa Mircolink at 0x%x\n",
				       sp->cfg_reg);
				return (0);
			}
		} else
			sp->cfg_reg = probe_elsa(sp);
		if (sp->cfg_reg) {
			val = bytein(sp->cfg_reg + CARD_CONFIG);
			if (sp->subtyp == ELSA_PCC) {
				const byte CARD_IrqTab[8] =
				{7, 3, 5, 9, 0, 0, 0, 0};
				sp->irq = CARD_IrqTab[(val & 0x0c) >> 2];
			} else {
				const byte CARD_IrqTab[8] =
				{15, 10, 15, 3, 11, 5, 11, 9};
				sp->irq = CARD_IrqTab[(val & 0x38) >> 3];
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
				   "Elsa: Mircolink S0 bus power bad\n");
		} else {
			printk(KERN_WARNING
			       "No Elsa Mircolink found\n");
			return (0);
		}
	} else if (sp->typ == ISDN_CTYPE_ELSA_QS1000) {
		sp->cfg_reg = card->para[1];
		sp->irq = card->para[0];
		sp->subtyp = ELSA_QS1000;
	} else
		return (0);

	switch (sp->subtyp) {
	case ELSA_PCC:
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
	bytein(sp->cfg_reg + CARD_START_TIMER);
	if (!TimerRun(sp)) {
		bytein(sp->cfg_reg + CARD_START_TIMER);		/* 2. Versuch */
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
	/* Wait 1 Timer */
	bytein(sp->cfg_reg + CARD_START_TIMER);
	while (TimerRun(sp));
	byteout(sp->cfg_reg + CARD_CONTROL, 0x00);	/* Reset On */
	/* Wait 1 Timer */
	bytein(sp->cfg_reg + CARD_START_TIMER);
	while (TimerRun(sp));
	byteout(sp->cfg_reg + CARD_CONTROL, ISDN_RESET);	/* Reset Off */
	/* Wait 1 Timer */
	bytein(sp->cfg_reg + CARD_START_TIMER);
	while (TimerRun(sp));

	verA = readhscx(sp->cfg_reg, 0, HSCX_VSTR) & 0xf;
	verB = readhscx(sp->cfg_reg, 1, HSCX_VSTR) & 0xf;
	printk(KERN_INFO "Elsa: HSCX version A: %s  B: %s\n",
	       HscxVersion(verA), HscxVersion(verB));
	val = readisac(sp->cfg_reg, ISAC_RBCH);
	printk(KERN_INFO "Elsa: ISAC %s\n",
	       ISACVersion(val));

	sp->modehscx = &modehscx;
	sp->ph_command = &ph_command;
	sp->hscx_fill_fifo = &hscx_fill_fifo;
	sp->isac_fill_fifo = &isac_fill_fifo;

	return (1);
}
