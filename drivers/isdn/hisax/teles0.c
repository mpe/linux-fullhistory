/* $Id: teles0.c,v 1.6 1997/01/27 15:52:18 keil Exp $

 * teles0.c     low level stuff for Teles Memory IO isdn cards
 *              based on the teles driver from Jan den Ouden
 *
 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *
 * Thanks to    Jan den Ouden
 *              Fritz Elfert
 *              Beat Doebeli
 *
 * $Log: teles0.c,v $
 * Revision 1.6  1997/01/27 15:52:18  keil
 * SMP proof,cosmetics
 *
 * Revision 1.5  1997/01/21 22:25:59  keil
 * cleanups
 *
 * Revision 1.4  1996/11/05 19:41:27  keil
 * more changes for 2.1
 *
 * Revision 1.3  1996/10/30 10:22:58  keil
 * Changes for 2.1 kernels
 *
 * Revision 1.2  1996/10/27 22:08:34  keil
 * cosmetic changes
 *
 * Revision 1.1  1996/10/13 20:04:58  keil
 * Initial revision
 *
 *
 *
 */
#define __NO_VERSION__
#include "siemens.h"
#include "hisax.h"
#include "teles0.h"
#include "isdnl1.h"
#include <linux/kernel_stat.h>

extern const char *CardType[];

const char *teles0_revision = "$Revision: 1.6 $";

#define byteout(addr,val) outb_p(val,addr)
#define bytein(addr) inb_p(addr)

static inline byte
readisac(unsigned int adr, byte off)
{
	return readb(adr + 0x120 + ((off & 1) ? 0x1ff : 0) + off);
}

static inline void
writeisac(unsigned int adr, byte off, byte data)
{
	writeb(data, adr + 0x120 + ((off & 1) ? 0x1ff : 0) + off);
}


static inline byte
readhscx(unsigned int adr, int hscx, byte off)
{
	return readb(adr + (hscx ? 0x1e0 : 0x1a0) +
		     ((off & 1) ? 0x1ff : 0) + off);
}

static inline void
writehscx(unsigned int adr, int hscx, byte off, byte data)
{
	writeb(data, adr + (hscx ? 0x1e0 : 0x1a0) +
	       ((off & 1) ? 0x1ff : 0) + off);
}

static inline void
read_fifo_isac(unsigned int adr, byte * data, int size)
{
	register int i;
	register byte *ad = (byte *) (adr + 0x100);
	for (i = 0; i < size; i++)
		data[i] = readb(ad);
}

static void
write_fifo_isac(unsigned int adr, byte * data, int size)
{
	register int i;
	register byte *ad = (byte *) (adr + 0x100);
	for (i = 0; i < size; i++)
		writeb(data[i], ad);
}

static inline void
read_fifo_hscx(unsigned int adr, int hscx, byte * data, int size)
{
	register int i;
	register byte *ad = (byte *) (adr + (hscx ? 0x1c0 : 0x180));
	for (i = 0; i < size; i++)
		data[i] = readb(ad);
}

static inline void
write_fifo_hscx(unsigned int adr, int hscx, byte * data, int size)
{
	int i;
	register byte *ad = (byte *) (adr + (hscx ? 0x1c0 : 0x180));
	for (i = 0; i < size; i++)
		writeb(data[i], ad);
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
		printk(KERN_WARNING "Teles0: waitforCEC timeout\n");
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
		printk(KERN_WARNING "Teles0: waitforXFW timeout\n");
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
	printk(KERN_DEBUG "ISTA %x\n", readhscx(sp->membase, hscx, HSCX_ISTA));
	printk(KERN_DEBUG "STAR %x\n", readhscx(sp->membase, hscx, HSCX_STAR));
	printk(KERN_DEBUG "EXIR %x\n", readhscx(sp->membase, hscx, HSCX_EXIR));
}

void
teles0_report(struct IsdnCardState *sp)
{
	printk(KERN_DEBUG "ISAC\n");
	printk(KERN_DEBUG "ISTA %x\n", readisac(sp->membase, ISAC_ISTA));
	printk(KERN_DEBUG "STAR %x\n", readisac(sp->membase, ISAC_STAR));
	printk(KERN_DEBUG "EXIR %x\n", readisac(sp->membase, ISAC_EXIR));
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
		writehscxCMDR(sp->membase, hsp->hscx, 0x80);
		return;
	}
	ptr = DATAPTR(ibh);
	ptr += hsp->rcvptr;

	hsp->rcvptr += count;
	save_flags(flags);
	cli();
	read_fifo_hscx(sp->membase, hsp->hscx, ptr, count);
	writehscxCMDR(sp->membase, hsp->hscx, 0x80);
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

	waitforXFW(sp->membase, hsp->hscx);
	save_flags(flags);
	cli();
	write_fifo_hscx(sp->membase, hsp->hscx, ptr, count);
	writehscxCMDR(sp->membase, hsp->hscx, more ? 0x8 : 0xa);
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

		r = readhscx(sp->membase, hsp->hscx, HSCX_RSTA);
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
			writehscxCMDR(sp->membase, hsp->hscx, 0x80);
			goto afterRME;
		}
		if (!hsp->rcvibh)
			if (BufPoolGet(&hsp->rcvibh, &hsp->rbufpool,
				       GFP_ATOMIC, (void *) 1, 1)) {
				if (sp->debug & L1_DEB_WARN)
					debugl1(sp, "HSCX RME out of buffers");
				writehscxCMDR(sp->membase, hsp->hscx, 0x80);
				goto afterRME;
			} else
				hsp->rcvptr = 0;

		count = readhscx(sp->membase, hsp->hscx, HSCX_RBCL) & 0x1f;
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
				writehscxCMDR(sp->membase, hsp->hscx, 0x80);
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
	read_fifo_isac(sp->membase, ptr, count);
	writeisac(sp->membase, ISAC_CMDR, 0x80);
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
	write_fifo_isac(sp->membase, ptr, count);
	writeisac(sp->membase, ISAC_CMDR, more ? 0x8 : 0xa);
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
	writeisac(sp->membase, ISAC_CIX0, (command << 2) | 3);
}

static inline void
isac_interrupt(struct IsdnCardState *sp, byte val)
{
	byte exval;
	unsigned int count;
	char tmp[32];

	if (sp->debug & L1_DEB_ISAC) {
		sprintf(tmp, "ISAC interrupt %x", val);
		debugl1(sp, tmp);
	}
	if (val & 0x80) {	/* RME */
		exval = readisac(sp->membase, ISAC_RSTA);
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
			writeisac(sp->membase, ISAC_CMDR, 0x80);
			goto afterRME;
		}
		if (!sp->rcvibh)
			if (BufPoolGet(&(sp->rcvibh), &(sp->rbufpool),
				       GFP_ATOMIC, (void *) 1, 3)) {
				if (sp->debug & L1_DEB_WARN)
					debugl1(sp, "ISAC RME out of buffers!");
				writeisac(sp->membase, ISAC_CMDR, 0x80);
				goto afterRME;
			} else
				sp->rcvptr = 0;
		count = readisac(sp->membase, ISAC_RBCL) & 0x1f;
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
				writeisac(sp->membase, ISAC_CMDR, 0x80);
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
		sp->ph_state = (readisac(sp->membase, ISAC_CIX0) >> 2)
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
		exval = readisac(sp->membase, ISAC_EXIR);
		if (sp->debug & L1_DEB_WARN) {
			sprintf(tmp, "ISAC EXIR %02x", exval);
			debugl1(sp, tmp);
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
		exval = readhscx(sp->membase, 1, HSCX_EXIR);
		if (exval == 0x40) {
			if (hsp->mode == 1)
				hscx_fill_fifo(hsp);
			else {
				/* Here we lost an TX interrupt, so
				   * restart transmitting the whole frame.
				 */
				hsp->sendptr = 0;
				writehscxCMDR(sp->membase, hsp->hscx, 0x01);
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
		exval = readhscx(sp->membase, 0, HSCX_EXIR);
		if (exval == 0x40) {
			if (hsp->mode == 1)
				hscx_fill_fifo(hsp);
			else {
				/* Here we lost an TX interrupt, so
				   * restart transmitting the whole frame.
				 */
				hsp->sendptr = 0;
				writehscxCMDR(sp->membase, hsp->hscx, 0x01);
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
		exval = readhscx(sp->membase, 0, HSCX_ISTA);
		if (sp->debug & L1_DEB_HSCX) {
			sprintf(tmp, "HSCX A interrupt %x", exval);
			debugl1(sp, tmp);
		}
		hscx_interrupt(sp, exval, 0);
	}
}

static void
telesS0_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *sp;
	byte val, stat = 0;

	sp = (struct IsdnCardState *) irq2dev_map[intno];

	if (!sp) {
		printk(KERN_WARNING "Teles0: Spurious interrupt!\n");
		return;
	}
	val = readhscx(sp->membase, 1, HSCX_ISTA);
      Start_HSCX:
	if (val) {
		hscx_int_main(sp, val);
		stat |= 1;
	}
	val = readisac(sp->membase, ISAC_ISTA);
      Start_ISAC:
	if (val) {
		isac_interrupt(sp, val);
		stat |= 2;
	}
	val = readhscx(sp->membase, 1, HSCX_ISTA);
	if (val) {
		if (sp->debug & L1_DEB_HSCX)
			debugl1(sp, "HSCX IntStat after IntRoutine");
		goto Start_HSCX;
	}
	val = readisac(sp->membase, ISAC_ISTA);
	if (val) {
		if (sp->debug & L1_DEB_ISAC)
			debugl1(sp, "ISAC IntStat after IntRoutine");
		goto Start_ISAC;
	}
	if (stat & 1) {
		writehscx(sp->membase, 0, HSCX_MASK, 0xFF);
		writehscx(sp->membase, 1, HSCX_MASK, 0xFF);
		writehscx(sp->membase, 0, HSCX_MASK, 0x0);
		writehscx(sp->membase, 1, HSCX_MASK, 0x0);
	}
	if (stat & 2) {
		writeisac(sp->membase, ISAC_MASK, 0xFF);
		writeisac(sp->membase, ISAC_MASK, 0x0);
	}
}


static void
initisac(struct IsdnCardState *sp)
{
	unsigned int adr = sp->membase;

	/* 16.0 IOM 1 Mode */
	writeisac(adr, ISAC_MASK, 0xff);
	writeisac(adr, ISAC_ADF2, 0x0);
	writeisac(adr, ISAC_SPCR, 0xa);
	writeisac(adr, ISAC_ADF1, 0x2);
	writeisac(adr, ISAC_STCR, 0x70);
	writeisac(adr, ISAC_MODE, 0xc9);
	writeisac(adr, ISAC_CMDR, 0x41);
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
	writehscx(sp->membase, hscx, HSCX_CCR1, 0x85);
	writehscx(sp->membase, hscx, HSCX_XAD1, 0xFF);
	writehscx(sp->membase, hscx, HSCX_XAD2, 0xFF);
	writehscx(sp->membase, hscx, HSCX_RAH2, 0xFF);
	writehscx(sp->membase, hscx, HSCX_XBCH, 0x0);

	/* Switch IOM 1 SSI */
	if (hscx == 0)
		ichan = 1 - ichan;

	switch (mode) {
	case (0):
		writehscx(sp->membase, hscx, HSCX_CCR2, 0x30);
		writehscx(sp->membase, hscx, HSCX_TSAX, 0xff);
		writehscx(sp->membase, hscx, HSCX_TSAR, 0xff);
		writehscx(sp->membase, hscx, HSCX_XCCR, 7);
		writehscx(sp->membase, hscx, HSCX_RCCR, 7);
		writehscx(sp->membase, hscx, HSCX_MODE, 0x84);
		break;
	case (1):
		if (ichan == 0) {
			writehscx(sp->membase, hscx, HSCX_CCR2, 0x30);
			writehscx(sp->membase, hscx, HSCX_TSAX, 0x7);
			writehscx(sp->membase, hscx, HSCX_TSAR, 0x7);
			writehscx(sp->membase, hscx, HSCX_XCCR, 7);
			writehscx(sp->membase, hscx, HSCX_RCCR, 7);
		} else {
			writehscx(sp->membase, hscx, HSCX_CCR2, 0x30);
			writehscx(sp->membase, hscx, HSCX_TSAX, 0x3);
			writehscx(sp->membase, hscx, HSCX_TSAR, 0x3);
			writehscx(sp->membase, hscx, HSCX_XCCR, 7);
			writehscx(sp->membase, hscx, HSCX_RCCR, 7);
		}
		writehscx(sp->membase, hscx, HSCX_MODE, 0xe4);
		writehscx(sp->membase, hscx, HSCX_CMDR, 0x41);
		break;
	case (2):
		if (ichan == 0) {
			writehscx(sp->membase, hscx, HSCX_CCR2, 0x30);
			writehscx(sp->membase, hscx, HSCX_TSAX, 0x7);
			writehscx(sp->membase, hscx, HSCX_TSAR, 0x7);
			writehscx(sp->membase, hscx, HSCX_XCCR, 7);
			writehscx(sp->membase, hscx, HSCX_RCCR, 7);
		} else {
			writehscx(sp->membase, hscx, HSCX_CCR2, 0x30);
			writehscx(sp->membase, hscx, HSCX_TSAX, 0x3);
			writehscx(sp->membase, hscx, HSCX_TSAR, 0x3);
			writehscx(sp->membase, hscx, HSCX_XCCR, 7);
			writehscx(sp->membase, hscx, HSCX_RCCR, 7);
		}
		writehscx(sp->membase, hscx, HSCX_MODE, 0x8c);
		writehscx(sp->membase, hscx, HSCX_CMDR, 0x41);
		break;
	}
	writehscx(sp->membase, hscx, HSCX_ISTA, 0x00);
}

void
release_io_teles0(struct IsdnCard *card)
{
	if (card->sp->cfg_reg)
		release_region(card->sp->cfg_reg, 8);
}

static void
clear_pending_ints(struct IsdnCardState *sp)
{
	int val;
	char tmp[64];

	val = readhscx(sp->membase, 1, HSCX_ISTA);
	sprintf(tmp, "HSCX B ISTA %x", val);
	debugl1(sp, tmp);
	if (val & 0x01) {
		val = readhscx(sp->membase, 1, HSCX_EXIR);
		sprintf(tmp, "HSCX B EXIR %x", val);
		debugl1(sp, tmp);
	} else if (val & 0x02) {
		val = readhscx(sp->membase, 0, HSCX_EXIR);
		sprintf(tmp, "HSCX A EXIR %x", val);
		debugl1(sp, tmp);
	}
	val = readhscx(sp->membase, 0, HSCX_ISTA);
	sprintf(tmp, "HSCX A ISTA %x", val);
	debugl1(sp, tmp);
	val = readhscx(sp->membase, 1, HSCX_STAR);
	sprintf(tmp, "HSCX B STAR %x", val);
	debugl1(sp, tmp);
	val = readhscx(sp->membase, 0, HSCX_STAR);
	sprintf(tmp, "HSCX A STAR %x", val);
	debugl1(sp, tmp);
	val = readisac(sp->membase, ISAC_STAR);
	sprintf(tmp, "ISAC STAR %x", val);
	debugl1(sp, tmp);
	val = readisac(sp->membase, ISAC_MODE);
	sprintf(tmp, "ISAC MODE %x", val);
	debugl1(sp, tmp);
	val = readisac(sp->membase, ISAC_ADF2);
	sprintf(tmp, "ISAC ADF2 %x", val);
	debugl1(sp, tmp);
	val = readisac(sp->membase, ISAC_ISTA);
	sprintf(tmp, "ISAC ISTA %x", val);
	debugl1(sp, tmp);
	if (val & 0x01) {
		val = readisac(sp->membase, ISAC_EXIR);
		sprintf(tmp, "ISAC EXIR %x", val);
		debugl1(sp, tmp);
	} else if (val & 0x04) {
		val = readisac(sp->membase, ISAC_CIR0);
		sprintf(tmp, "ISAC CIR0 %x", val);
		debugl1(sp, tmp);
	}
	writeisac(sp->membase, ISAC_MASK, 0);
	writeisac(sp->membase, ISAC_CMDR, 0x41);
}

int
initteles0(struct IsdnCardState *sp)
{
	int ret;
	char tmp[40];

	sp->counter = kstat.interrupts[sp->irq];
	sprintf(tmp, "IRQ %d count %d", sp->irq, sp->counter);
	debugl1(sp, tmp);
	clear_pending_ints(sp);
	ret = get_irq(sp->cardnr, &telesS0_interrupt);
	if (ret) {
		initisac(sp);
		sp->modehscx(sp->hs, 0, 0);
		sp->modehscx(sp->hs + 1, 0, 0);
		sprintf(tmp, "IRQ %d count %d", sp->irq,
			kstat.interrupts[sp->irq]);
		debugl1(sp, tmp);
		if (kstat.interrupts[sp->irq] == sp->counter) {
			printk(KERN_WARNING
			       "Teles0: IRQ(%d) getting no interrupts during init\n",
			       sp->irq);
			irq2dev_map[sp->irq] = NULL;
			free_irq(sp->irq, NULL);
			return (0);
		}
	}
	return (ret);
}

int
setup_teles0(struct IsdnCard *card)
{
	byte cfval, val, verA, verB;
	struct IsdnCardState *sp = card->sp;
	long flags;
	char tmp[64];

	strcpy(tmp, teles0_revision);
	printk(KERN_NOTICE "HiSax: Teles 8.0/16.0 driver Rev. %s\n", HiSax_getrev(tmp));
	if ((sp->typ != ISDN_CTYPE_16_0) && (sp->typ != ISDN_CTYPE_8_0))
		return (0);

	if (sp->typ == ISDN_CTYPE_16_0)
		sp->cfg_reg = card->para[2];
	else			/* 8.0 */
		sp->cfg_reg = 0;

	if (card->para[1] < 0x10000) {
		card->para[1] <<= 4;
		printk(KERN_INFO
		   "Teles0: membase configured DOSish, assuming 0x%lx\n",
		       (unsigned long) card->para[1]);
	}
	sp->membase = card->para[1];
	sp->irq = card->para[0];
	if (sp->cfg_reg) {
		if (check_region((sp->cfg_reg), 8)) {
			printk(KERN_WARNING
			  "HiSax: %s config port %x-%x already in use\n",
			       CardType[card->typ],
			       sp->cfg_reg,
			       sp->cfg_reg + 8);
			return (0);
		} else {
			request_region(sp->cfg_reg, 8, "teles cfg");
		}
	}
	switch (sp->irq) {
	case 2:
		cfval = 0x00;
		break;
	case 3:
		cfval = 0x02;
		break;
	case 4:
		cfval = 0x04;
		break;
	case 5:
		cfval = 0x06;
		break;
	case 10:
		cfval = 0x08;
		break;
	case 11:
		cfval = 0x0A;
		break;
	case 12:
		cfval = 0x0C;
		break;
	case 15:
		cfval = 0x0E;
		break;
	default:
		cfval = 0x00;
		break;
	}
	cfval |= ((card->para[1] >> 9) & 0xF0);
	if (sp->cfg_reg) {
		if ((val = bytein(sp->cfg_reg + 0)) != 0x51) {
			printk(KERN_WARNING "Teles0: 16.0 Byte at %x is %x\n",
			       sp->cfg_reg + 0, val);
			release_region(sp->cfg_reg, 8);
			return (0);
		}
		if ((val = bytein(sp->cfg_reg + 1)) != 0x93) {
			printk(KERN_WARNING "Teles0: 16.0 Byte at %x is %x\n",
			       sp->cfg_reg + 1, val);
			release_region(sp->cfg_reg, 8);
			return (0);
		}
		val = bytein(sp->cfg_reg + 2);	/* 0x1e=without AB
						   * 0x1f=with AB
						   * 0x1c 16.3 ???
						 */
		if (val != 0x1e && val != 0x1f) {
			printk(KERN_WARNING "Teles0: 16.0 Byte at %x is %x\n",
			       sp->cfg_reg + 2, val);
			release_region(sp->cfg_reg, 8);
			return (0);
		}
		save_flags(flags);
		byteout(sp->cfg_reg + 4, cfval);
		sti();
		HZDELAY(HZ / 10 + 1);
		byteout(sp->cfg_reg + 4, cfval | 1);
		HZDELAY(HZ / 10 + 1);
		restore_flags(flags);
	}
	printk(KERN_NOTICE
	       "HiSax: %s config irq:%d mem:%x cfg:%x\n",
	       CardType[sp->typ], sp->irq,
	       sp->membase, sp->cfg_reg);
	verA = readhscx(sp->membase, 0, HSCX_VSTR) & 0xf;
	verB = readhscx(sp->membase, 1, HSCX_VSTR) & 0xf;
	printk(KERN_INFO "Teles0: HSCX version A: %s  B: %s\n",
	       HscxVersion(verA), HscxVersion(verB));
	val = readisac(sp->membase, ISAC_RBCH);
	printk(KERN_INFO "Teles0: ISAC %s\n",
	       ISACVersion(val));

	if ((verA == 0) | (verA == 0xf) | (verB == 0) | (verB == 0xf)) {
		printk(KERN_WARNING
		 "Teles0: wrong HSCX versions check IO/MEM addresses\n");
		release_io_teles0(card);
		return (0);
	}
	save_flags(flags);
	writeb(0, sp->membase + 0x80);
	sti();
	HZDELAY(HZ / 5 + 1);
	writeb(1, sp->membase + 0x80);
	HZDELAY(HZ / 5 + 1);
	restore_flags(flags);

	sp->modehscx = &modehscx;
	sp->ph_command = &ph_command;
	sp->hscx_fill_fifo = &hscx_fill_fifo;
	sp->isac_fill_fifo = &isac_fill_fifo;
	return (1);
}
