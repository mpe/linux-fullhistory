/* $Id: card.c,v 1.12 1996/06/24 17:16:52 fritz Exp $
 *
 * card.c     low level stuff for the Teles S0 isdn card
 * 
 * Author     Jan den Ouden
 * 
 * Beat Doebeli         log all D channel traffic
 * 
 * $Log: card.c,v $
 * Revision 1.12  1996/06/24 17:16:52  fritz
 * Added check for misconfigured membase.
 *
 * Revision 1.11  1996/06/14 03:30:37  fritz
 * Added recovery from EXIR 40 interrupt.
 * Some cleanup.
 *
 * Revision 1.10  1996/06/11 14:57:20  hipp
 * minor changes to ensure, that SKBs are sent in the right order
 *
 * Revision 1.9  1996/06/06 14:42:09  fritz
 * Bugfix: forgot hsp-> in last change.
 *
 * Revision 1.7  1996/05/31 01:02:21  fritz
 * Cosmetic changes.
 *
 * Revision 1.6  1996/05/26 14:58:10  fritz
 * Bugfix: Did not show port correctly, when no card found.
 *
 * Revision 1.5  1996/05/17 03:45:02  fritz
 * Made error messages more clearly.
 * Bugfix: Only 31 bytes of 32-byte audio frames
 *         have been transfered to upper layers.
 *
 * Revision 1.4  1996/05/06 10:17:57  fritz
 * Added voice-send stuff
 *  (Not reporting EXIR when in voice-mode, since it's normal).
 *
 * Revision 1.3  1996/04/30 22:02:40  isdn4dev
 * Bugfixes for 16.3
 *     -improved IO allocation
 *     -fix second B channel problem
 *     -correct ph_command patch
 *
 * Revision 1.2  1996/04/30 10:00:59  fritz
 * Bugfix: Added ph_command(8) for 16.3.
 * Bugfix: Ports did not get registered correctly
 *         when using a 16.3.
 *         Started voice support.
 *         Some experimental changes of waitforXFW().
 *
 * Revision 1.1  1996/04/13 10:22:42  fritz
 * Initial revision
 *
 *
 */

#define __NO_VERSION__
#include "teles.h"

#define INCLUDE_INLINE_FUNCS
#include <linux/tqueue.h>
#include <linux/interrupt.h>

#undef DCHAN_VERBOSE

extern void     tei_handler(struct PStack *st, byte pr,
			    struct BufHeader *ibh);
extern struct   IsdnCard cards[];
extern int      nrcards;

#define byteout(addr,val) outb_p(val,addr)
#define bytein(addr) inb_p(addr)

static inline   byte
readisac_0(byte * cardm, byte offset)
{
	return *(byte *) (cardm + 0x100 + ((offset & 1) ? 0x1ff : 0) + offset);
}

static inline   byte
readisac_3(int iobase, byte offset)
{
        return (bytein(iobase - 0x420 + offset));
}

#define READISAC(mbase,ibase,ofs) \
        ((mbase)?readisac_0(mbase,ofs):readisac_3(ibase,ofs))

static inline void
writeisac_0(byte * cardm, byte offset, byte value)
{
	*(byte *) (cardm + 0x100 + ((offset & 1) ? 0x1ff : 0) + offset) = value;
}

static inline void
writeisac_3(int iobase, byte offset, byte value)
{
	byteout(iobase - 0x420 + offset, value);
}

#define WRITEISAC(mbase,ibase,ofs,val) \
        ((mbase)?writeisac_0(mbase,ofs,val):writeisac_3(ibase,ofs,val))

static inline void
readisac_s(int iobase, byte offset, byte * dest, int count)
{
	insb(iobase - 0x420 + offset, dest, count);
}

static inline void
writeisac_s(int iobase, byte offset, byte * src, int count)
{
	outsb(iobase - 0x420 + offset, src, count);
}

static inline   byte
readhscx_0(byte * base, byte hscx, byte offset)
{
	return *(byte *) (base + 0x180 + ((offset & 1) ? 0x1FF : 0) +
			  ((hscx & 1) ? 0x40 : 0) + offset);
}

static inline   byte
readhscx_3(int iobase, byte hscx, byte offset)
{
	return (bytein(iobase - (hscx ? 0x820 : 0xc20) + offset));
}

#define READHSCX(mbase,ibase,hscx,ofs) \
        ((mbase)?readhscx_0(mbase,hscx,ofs):readhscx_3(ibase,hscx,ofs))

static inline void
writehscx_0(byte * base, byte hscx, byte offset, byte data)
{
	*(byte *) (base + 0x180 + ((offset & 1) ? 0x1FF : 0) +
		   ((hscx & 1) ? 0x40 : 0) + offset) = data;
}

static inline void
writehscx_3(int iobase, byte hscx, byte offset, byte data)
{
	byteout(iobase - (hscx ? 0x820 : 0xc20) + offset, data);
}

static inline void
readhscx_s(int iobase, byte hscx, byte offset, byte * dest, int count)
{
	insb(iobase - (hscx ? 0x820 : 0xc20) + offset, dest, count);
}

static inline void
writehscx_s(int iobase, byte hscx, byte offset, byte * src, int count)
{
	outsb(iobase - (hscx ? 0x820 : 0xc20) + offset, src, count);
}

#define ISAC_MASK 0x20
#define ISAC_ISTA 0x20
#define ISAC_STAR 0x21
#define ISAC_CMDR 0x21
#define ISAC_EXIR 0x24

#define ISAC_RBCH 0x2a

#define ISAC_ADF2 0x39
#define ISAC_SPCR 0x30
#define ISAC_ADF1 0x38
#define ISAC_CIX0 0x31
#define ISAC_STCR 0x37
#define ISAC_MODE 0x22
#define ISAC_RSTA 0x27
#define ISAC_RBCL 0x25
#define ISAC_TIMR 0x23
#define ISAC_SQXR 0x3b

#define HSCX_ISTA 0x20
#define HSCX_CCR1 0x2f
#define HSCX_CCR2 0x2c
#define HSCX_TSAR 0x31
#define HSCX_TSAX 0x30
#define HSCX_XCCR 0x32
#define HSCX_RCCR 0x33
#define HSCX_MODE 0x22
#define HSCX_CMDR 0x21
#define HSCX_EXIR 0x24
#define HSCX_XAD1 0x24
#define HSCX_XAD2 0x25
#define HSCX_RAH2 0x27
#define HSCX_RSTA 0x27
#define HSCX_TIMR 0x23
#define HSCX_STAR 0x21
#define HSCX_RBCL 0x25
#define HSCX_XBCH 0x2d
#define HSCX_VSTR 0x2e
#define HSCX_RLCR 0x2e
#define HSCX_MASK 0x20

static inline void
waitforCEC_0(byte * base, byte hscx)
{
	long            to = 10;

	while ((readhscx_0(base, hscx, HSCX_STAR) & 0x04) && to) {
		udelay(5);
		to--;
	}
	if (!to)
		printk(KERN_WARNING "waitforCEC timeout\n");
}

static inline void
waitforCEC_3(int iobase, byte hscx)
{
	long            to = 10;

	while ((readhscx_3(iobase, hscx, HSCX_STAR) & 0x04) && to) {
		udelay(5);
		to--;
	}
	if (!to)
		printk(KERN_WARNING "waitforCEC timeout\n");
}

static inline void
waitforXFW_0(byte * base, byte hscx)
{
	long            to = 20;

	while ((!(readhscx_0(base, hscx, HSCX_STAR) & 0x44)==0x40) && to) {
		udelay(5);
		to--;
	}
	if (!to)
		printk(KERN_WARNING "waitforXFW timeout\n");
}

static inline void
waitforXFW_3(int iobase, byte hscx)
{
	long            to = 20;

	while ((!(readhscx_3(iobase, hscx, HSCX_STAR) & 0x44)==0x40) && to) {
		udelay(5);
		to--;
	}
	if (!to)
		printk(KERN_WARNING "waitforXFW timeout\n");
}

static inline void
writehscxCMDR_0(byte * base, byte hscx, byte data)
{
	long            flags;

	save_flags(flags);
	cli();
	waitforCEC_0(base, hscx);
	writehscx_0(base, hscx, HSCX_CMDR, data);
	restore_flags(flags);
}

static inline void
writehscxCMDR_3(int iobase, byte hscx, byte data)
{
	long            flags;

	save_flags(flags);
	cli();
	waitforCEC_3(iobase, hscx);
	writehscx_3(iobase, hscx, HSCX_CMDR, data);
	restore_flags(flags);
}

#define WRITEHSCX_CMDR(mbase,ibase,hscx,data) \
        ((mbase)?writehscxCMDR_0(mbase,hscx,data):writehscxCMDR_3(ibase,hscx,data))

/*
 * fast interrupt here
 */

#define ISAC_RCVBUFREADY 0
#define ISAC_XMTBUFREADY 1
#define ISAC_PHCHANGE    2

#define HSCX_RCVBUFREADY 0
#define HSCX_XMTBUFREADY 1

void
teles_hscxreport(struct IsdnCardState *sp, int hscx)
{
        printk(KERN_DEBUG "HSCX %d\n", hscx);
        if (sp->membase) {
                printk(KERN_DEBUG "  ISTA %x\n", readhscx_0(sp->membase,
                                                          hscx, HSCX_ISTA));
                printk(KERN_DEBUG "  STAR %x\n", readhscx_0(sp->membase,
                                                          hscx, HSCX_STAR));
                printk(KERN_DEBUG "  EXIR %x\n", readhscx_0(sp->membase,
                                                          hscx, HSCX_EXIR));
        } else {
                printk(KERN_DEBUG "  ISTA %x\n", readhscx_3(sp->iobase,
                                                          hscx, HSCX_ISTA));
                printk(KERN_DEBUG "  STAR %x\n", readhscx_3(sp->iobase,
                                                          hscx, HSCX_STAR));
                printk(KERN_DEBUG "  EXIR %x\n", readhscx_3(sp->iobase,
                                                          hscx, HSCX_EXIR));
        }
}

void
teles_report(struct IsdnCardState *sp)
{
	printk(KERN_DEBUG "ISAC\n");
        if (sp->membase) {
               printk(KERN_DEBUG "  ISTA %x\n", readisac_0(sp->membase,
                                                            ISAC_ISTA));
               printk(KERN_DEBUG "  STAR %x\n", readisac_0(sp->membase,
                                                           ISAC_STAR));
               printk(KERN_DEBUG "  EXIR %x\n", readisac_0(sp->membase,
                                                           ISAC_EXIR));
        } else {
                printk(KERN_DEBUG "  ISTA %x\n", readisac_3(sp->iobase,
                                                          ISAC_ISTA));
                printk(KERN_DEBUG "  STAR %x\n", readisac_3(sp->iobase,
                                                          ISAC_STAR));
                printk(KERN_DEBUG "  EXIR %x\n", readisac_3(sp->iobase,
                                                          ISAC_EXIR));
        }
	teles_hscxreport(sp, 0);
	teles_hscxreport(sp, 1);
}

/*
 * HSCX stuff goes here
 */

static void
hscx_sched_event(struct HscxState *hsp, int event)
{
	hsp->event |= 1 << event;
	queue_task_irq_off(&hsp->tqueue, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
}

static void
hscx_empty_fifo(struct HscxState *hsp, int count)
{
	byte             *ptr;
	struct BufHeader *ibh = hsp->rcvibh;

	if (hsp->sp->debug)
		printk(KERN_DEBUG "hscx_empty_fifo\n");

	if (hsp->rcvptr + count > BUFFER_SIZE(HSCX_RBUF_ORDER,
					      HSCX_RBUF_BPPS)) {
		printk(KERN_WARNING
                       "hscx_empty_fifo: incoming packet too large\n");
		WRITEHSCX_CMDR(hsp->membase, hsp->iobase, hsp->hscx, 0x80);
		return;
	}
	ptr = DATAPTR(ibh);
	ptr += hsp->rcvptr;

	hsp->rcvptr += count;
        if (hsp->membase) {
                while (count--)
                        *ptr++ = readhscx_0(hsp->membase, hsp->hscx, 0x0);
                writehscxCMDR_0(hsp->membase, hsp->hscx, 0x80);
        } else {
                readhscx_s(hsp->iobase, hsp->hscx, 0x3e, ptr, count);
                writehscxCMDR_3(hsp->iobase, hsp->hscx, 0x80);
        }
}

static void
hscx_fill_fifo(struct HscxState *hsp)
{
	struct BufHeader *ibh;
	int              more, count;
	byte             *ptr;

	if (hsp->sp->debug)
		printk(KERN_DEBUG "hscx_fill_fifo\n");

	ibh = hsp->xmtibh;
	if (!ibh)
                return;

	count = ibh->datasize - hsp->sendptr;
	if (count <= 0)
                return;

	more = (hsp->mode == 1)?1:0;
	if (count > 32) {
		more = !0;
		count = 32;
	}
	ptr = DATAPTR(ibh);
	ptr += hsp->sendptr;
	hsp->sendptr += count;

#ifdef BCHAN_VERBOSE
        {
                int i;
                printk(KERN_DEBUG "hscx_fill_fifo ");
                for (i = 0; i < count; i++)
                        printk(" %2x", ptr[i]);
                printk("\n");
        }
#endif
        if (hsp->membase) {
                waitforXFW_0(hsp->membase, hsp->hscx);
                while (count--)
                        writehscx_0(hsp->membase, hsp->hscx, 0x0, *ptr++);
                writehscxCMDR_0(hsp->membase, hsp->hscx, more ? 0x8 : 0xa);
        } else {
                waitforXFW_3(hsp->iobase, hsp->hscx);
                writehscx_s(hsp->iobase, hsp->hscx, 0x3e, ptr, count);
                writehscxCMDR_3(hsp->iobase, hsp->hscx, more ? 0x8 : 0xa);
        }
}

static inline void
hscx_interrupt(struct IsdnCardState *sp, byte val, byte hscx)
{
	byte             r;
	struct HscxState *hsp = sp->hs + hscx;
	int              count;

	if (!hsp->init)
		return;

	if (val & 0x80) {	/* RME */

		r = READHSCX(hsp->membase, sp->iobase, hsp->hscx, HSCX_RSTA);
		if ((r & 0xf0) != 0xa0) {
			if (!r & 0x80)
				printk(KERN_WARNING
                                       "Teles: HSCX invalid frame\n");
			if ((r & 0x40) && hsp->mode)
				printk(KERN_WARNING "Teles: HSCX RDO mode=%d\n",hsp->mode);
			if (!r & 0x20)
				printk(KERN_WARNING "Teles: HSCX CRC error\n");
			if (hsp->rcvibh)
				BufPoolRelease(hsp->rcvibh);
			hsp->rcvibh = NULL;
			WRITEHSCX_CMDR(hsp->membase, hsp->iobase, hsp->hscx,
                                       0x80);
			goto afterRME;
		}
		if (!hsp->rcvibh)
			if (BufPoolGet(&hsp->rcvibh, &hsp->rbufpool,
                                       GFP_ATOMIC, (void *) 1, 1)) {
				printk(KERN_WARNING
                                       "HSCX RME out of buffers at %ld\n",
                                       jiffies);
				WRITEHSCX_CMDR(hsp->membase, hsp->iobase,
                                               hsp->hscx, 0x80);
				goto afterRME;
			} else
				hsp->rcvptr = 0;

		count = READHSCX(hsp->membase, sp->iobase, hsp->hscx,
                                 HSCX_RBCL) & 0x1f;
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
		if (!hsp->rcvibh)
			if (BufPoolGet(&hsp->rcvibh, &hsp->rbufpool,
				       GFP_ATOMIC, (void *) 1, 2)) {
				printk(KERN_WARNING
                                       "HSCX RPF out of buffers at %ld\n",
                                       jiffies);
				WRITEHSCX_CMDR(hsp->membase, hsp->iobase,
                                               hsp->hscx, 0x80);
				goto afterRPF;
			} else
				hsp->rcvptr = 0;

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
isac_sched_event(struct IsdnCardState *sp, int event)
{
	sp->event |= 1 << event;
	queue_task_irq_off(&sp->tqueue, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
}

static void
empty_fifo(struct IsdnCardState *sp, int count)
{
	byte             *ptr;
	struct BufHeader *ibh = sp->rcvibh;

	if (sp->debug)
		printk(KERN_DEBUG "empty_fifo\n");

	if (sp->rcvptr >= 3072) {
		printk(KERN_WARNING "empty_fifo rcvptr %d\n", sp->rcvptr);
		return;
	}
	ptr = DATAPTR(ibh);
	ptr += sp->rcvptr;
	sp->rcvptr += count;

        if (sp->membase) {
#ifdef DCHAN_VERBOSE
                printk(KERN_DEBUG "empty_fifo ");
                while (count--) {
                        *ptr = readisac_0(sp->membase, 0x0);
                        printk("%2x ", *ptr);
                        ptr++;
                }
                printk("\n");
#else
                while (count--)
                        *ptr++ = readisac_0(sp->membase, 0x0);
#endif
                writeisac_0(sp->membase, ISAC_CMDR, 0x80);
        } else {
#ifdef DCHAN_VERBOSE
                int i;
                printk(KERN_DEBUG "empty_fifo ");
                readisac_s(sp->iobase, 0x3e, ptr, count);
                for (i = 0; i < count; i++)
                        printk("%2x ", ptr[i]);
                printk("\n");
#else
                readisac_s(sp->iobase, 0x3e, ptr, count);
#endif
                writeisac_3(sp->iobase, ISAC_CMDR, 0x80);
        }
}

static void
fill_fifo(struct IsdnCardState *sp)
{
	struct BufHeader *ibh;
	int              count, more;
	byte             *ptr;

	if (sp->debug)
		printk(KERN_DEBUG "fill_fifo\n");

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

        if (sp->membase) {
#ifdef DCHAN_VERBOSE
                printk(KERN_DEBUG "fill_fifo ");
                while (count--) {
                        writeisac_0(sp->membase, 0x0, *ptr);
                        printk("%2x ", *ptr);
                        ptr++;
                }
                printk("\n");
#else
                while (count--)
                        writeisac_0(sp->membase, 0x0, *ptr++);
#endif
                writeisac_0(sp->membase, ISAC_CMDR, more ? 0x8 : 0xa);
        } else {
#ifdef DCHAN_VERBOSE
                int i;
                writeisac_s(sp->iobase, 0x3e, ptr, count);
                printk(KERN_DEBUG "fill_fifo ");
                for (i = 0; i < count; i++)
                        printk("%2x ", ptr[i]);
                printk("\n");
#else
                writeisac_s(sp->iobase, 0x3e, ptr, count);
#endif
                writeisac_3(sp->iobase, ISAC_CMDR, more ? 0x8 : 0xa);
        }
}

static int
act_wanted(struct IsdnCardState *sp)
{
	struct PStack  *st;

	st = sp->stlist;
	while (st)
		if (st->l1.act_state)
			return (!0);
		else
			st = st->next;
	return (0);
}

static void
ph_command(struct IsdnCardState *sp, unsigned int command)
{
	printk(KERN_DEBUG "ph_command %d\n", command);
	WRITEISAC(sp->membase, sp->iobase, ISAC_CIX0, (command << 2) | 3);
}

static void
isac_new_ph(struct IsdnCardState *sp)
{
	int             enq;

	enq = act_wanted(sp);

	switch (sp->ph_state) {
	  case (0):
	  case (6):
		  if (enq)
			  ph_command(sp, 0);
		  else
			  ph_command(sp, 15);
		  break;
	  case (7):
		  if (enq)
			  ph_command(sp, 9);
		  break;
	  case (12):
	          ph_command(sp, 8);
		  sp->ph_active = 5;
		  isac_sched_event(sp, ISAC_PHCHANGE);
		  if (!sp->xmtibh)
			  if (!BufQueueUnlink(&sp->xmtibh, &sp->sq))
				  sp->sendptr = 0;
		  if (sp->xmtibh)
			  fill_fifo(sp);
		  break;
	  case (13):
	          ph_command(sp, 9);
		  sp->ph_active = 5;
		  isac_sched_event(sp, ISAC_PHCHANGE);
		  if (!sp->xmtibh)
			  if (!BufQueueUnlink(&sp->xmtibh, &sp->sq))
				  sp->sendptr = 0;
		  if (sp->xmtibh)
			  fill_fifo(sp);
		  break;
	  case (4):
	  case (8):
		  break;
	  default:
		  sp->ph_active = 0;
		  break;
	}
}

static void
teles_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	byte                 val, r, exval;
	struct IsdnCardState *sp;
	unsigned int         count;
	struct HscxState     *hsp;

	sp = (struct IsdnCardState *) irq2dev_map[intno];

	if (!sp) {
		printk(KERN_WARNING "Teles: Spurious interrupt!\n");
		return;
	}
	val = READHSCX(sp->membase, sp->iobase, 1, HSCX_ISTA);

	if (val & 0x01) {
		hsp = sp->hs + 1;
                exval = READHSCX(sp->membase, sp->iobase, 1, HSCX_EXIR);
                if (exval == 0x40) {
                        if (hsp->mode == 1)
                                hscx_fill_fifo(hsp);
                        else {
                                /* Here we lost an TX interrupt, so
                                 * restart transmitting the whole frame.
                                 */
                                hsp->sendptr = 0;
			        WRITEHSCX_CMDR(hsp->membase, hsp->iobase,
                                               hsp->hscx, 0x01);
                                printk(KERN_DEBUG "HSCX B EXIR %x\n", exval);
                        }
                } else
                        printk(KERN_WARNING "HSCX B EXIR %x\n", exval);
	}
	if (val & 0xf8) {
		if (sp->debug)
			printk(KERN_DEBUG "HSCX B interrupt %x\n", val);
		hscx_interrupt(sp, val, 1);
	}
	if (val & 0x02) {
		hsp = sp->hs;
                exval = READHSCX(sp->membase, sp->iobase, 0, HSCX_EXIR);
                if (exval == 0x40) {
                        if (hsp->mode == 1)
                                hscx_fill_fifo(hsp);
                        else {
                                /* Here we lost an TX interrupt, so
                                 * restart transmitting the whole frame.
                                 */
                                hsp->sendptr = 0;
			        WRITEHSCX_CMDR(hsp->membase, hsp->iobase,
                                               hsp->hscx, 0x01);
                                printk(KERN_DEBUG "HSCX A EXIR %x\n", exval);
                        }
                } else
                        printk(KERN_WARNING "HSCX A EXIR %x\n", exval);
	}
        if (val & 0x04) {
                val = READHSCX(sp->membase, sp->iobase, 0, HSCX_ISTA);
                if (sp->debug)
                        printk(KERN_DEBUG "HSCX A interrupt %x\n",
                               val);
                hscx_interrupt(sp, val, 0);
        }

	val = READISAC(sp->membase, sp->iobase, ISAC_ISTA);

	if (sp->debug)
		printk(KERN_DEBUG "ISAC interrupt %x\n", val);

	if (val & 0x80) {	/* RME */

		r = READISAC(sp->membase, sp->iobase, ISAC_RSTA);
		if ((r & 0x70) != 0x20) {
			if (r & 0x40)
				printk(KERN_WARNING "Teles: ISAC RDO\n");
			if (!r & 0x20)
				printk(KERN_WARNING "Teles: ISAC CRC error\n");
			if (sp->rcvibh)
				BufPoolRelease(sp->rcvibh);
			sp->rcvibh = NULL;
			WRITEISAC(sp->membase, sp->iobase, ISAC_CMDR, 0x80);
			goto afterRME;
		}
		if (!sp->rcvibh)
			if (BufPoolGet(&(sp->rcvibh), &(sp->rbufpool),
                                       GFP_ATOMIC,
				       (void *) 1, 3)) {
				printk(KERN_WARNING
                                       "ISAC RME out of buffers!\n");
				WRITEISAC(sp->membase, sp->iobase, 
                                          ISAC_CMDR, 0x80);
				goto afterRME;
			} else
				sp->rcvptr = 0;

		count = READISAC(sp->membase, sp->iobase, ISAC_RBCL) & 0x1f;
		if (count == 0)
			count = 32;
		empty_fifo(sp, count);
		sp->rcvibh->datasize = sp->rcvptr;
		BufQueueLink(&(sp->rq), sp->rcvibh);
		sp->rcvibh = NULL;
		isac_sched_event(sp, ISAC_RCVBUFREADY);
	}
      afterRME:
	if (val & 0x40) {	/* RPF */
		if (!sp->rcvibh)
			if (BufPoolGet(&(sp->rcvibh), &(sp->rbufpool),
                                       GFP_ATOMIC,
				       (void *) 1, 4)) {
				printk(KERN_WARNING
                                       "ISAC RME out of buffers!\n");
				WRITEISAC(sp->membase, sp->iobase,
                                          ISAC_CMDR, 0x80);
				goto afterRPF;
			} else
				sp->rcvptr = 0;
		empty_fifo(sp, 32);
	}
      afterRPF:
	if (val & 0x20) {
	}
	if (val & 0x10) {	/* XPR */
		if (sp->xmtibh)
			if (sp->xmtibh->datasize > sp->sendptr) {
				fill_fifo(sp);
				goto afterXPR;
			} else {
				if (sp->releasebuf)
					BufPoolRelease(sp->xmtibh);
				sp->xmtibh = NULL;
				sp->sendptr = 0;
			}
		if (!BufQueueUnlink(&sp->xmtibh, &sp->sq)) {
			sp->releasebuf = !0;
			fill_fifo(sp);
		} else
			isac_sched_event(sp, ISAC_XMTBUFREADY);
	}
      afterXPR:
	if (val & 0x04) {	/* CISQ */
		sp->ph_state = (READISAC(sp->membase, sp->iobase, ISAC_CIX0)
                                >> 2) & 0xf;
		printk(KERN_DEBUG "l1state %d\n", sp->ph_state);
		isac_new_ph(sp);
	}
        if (sp->membase) {
                writeisac_0(sp->membase, ISAC_MASK, 0xFF);
                writehscx_0(sp->membase, 0, HSCX_MASK, 0xFF);
                writehscx_0(sp->membase, 1, HSCX_MASK, 0xFF);
                writeisac_0(sp->membase, ISAC_MASK, 0x0);
                writehscx_0(sp->membase, 0, HSCX_MASK, 0x0);
                writehscx_0(sp->membase, 1, HSCX_MASK, 0x0);
        } else {
                writeisac_3(sp->iobase, ISAC_MASK, 0xFF);
                writehscx_3(sp->iobase, 0, HSCX_MASK, 0xFF);
                writehscx_3(sp->iobase, 1, HSCX_MASK, 0xFF);
                writeisac_3(sp->iobase, ISAC_MASK, 0x0);
                writehscx_3(sp->iobase, 0, HSCX_MASK, 0x0);
                writehscx_3(sp->iobase, 1, HSCX_MASK, 0x0);
        }
}

/*
 * soft interrupt
 */

static void
act_ivated(struct IsdnCardState *sp)
{
	struct PStack  *st;

	st = sp->stlist;
	while (st) {
		if (st->l1.act_state == 1) {
			st->l1.act_state = 2;
			st->l1.l1man(st, PH_ACTIVATE, NULL);
		}
		st = st->next;
	}
}

static void
process_new_ph(struct IsdnCardState *sp)
{
	if (sp->ph_active == 5)
		act_ivated(sp);
}

static void
process_xmt(struct IsdnCardState *sp)
{
	struct PStack  *stptr;

	if (sp->xmtibh)
		return;

	stptr = sp->stlist;
	while (stptr != NULL)
		if (stptr->l1.requestpull) {
			stptr->l1.requestpull = 0;
			stptr->l1.l1l2(stptr, PH_PULL_ACK, NULL);
			break;
		} else
			stptr = stptr->next;
}

static void
process_rcv(struct IsdnCardState *sp)
{
	struct BufHeader *ibh, *cibh;
	struct PStack    *stptr;
	byte             *ptr;
	int              found, broadc;
	char             tmp[64];

	while (!BufQueueUnlink(&ibh, &sp->rq)) {
		stptr = sp->stlist;
		ptr = DATAPTR(ibh);
		broadc = (ptr[1] >> 1) == 127;

		if (broadc && sp->dlogflag && (!(ptr[0] >> 2)))
			dlogframe(sp, ptr + 3, ibh->datasize - 3,
				  "Q.931 frame network->user broadcast");

		if (broadc) {
			while (stptr != NULL) {
				if ((ptr[0] >> 2) == stptr->l2.sap)
					if (!BufPoolGet(&cibh, &sp->rbufpool, GFP_ATOMIC,
							(void *) 1, 5)) {
						memcpy(DATAPTR(cibh), DATAPTR(ibh), ibh->datasize);
						cibh->datasize = ibh->datasize;
						stptr->l1.l1l2(stptr, PH_DATA, cibh);
					} else
						printk(KERN_WARNING "isdn broadcast buffer shortage\n");
				stptr = stptr->next;
			}
			BufPoolRelease(ibh);
		} else {
			found = 0;
			while (stptr != NULL)
				if (((ptr[0] >> 2) == stptr->l2.sap) &&
				    ((ptr[1] >> 1) == stptr->l2.tei)) {
					stptr->l1.l1l2(stptr, PH_DATA, ibh);
					found = !0;
					break;
				} else
					stptr = stptr->next;
			if (!found) {
				/* BD 10.10.95
				 * Print out D-Channel msg not processed
				 * by isdn4linux
                                 */

				if ((!(ptr[0] >> 2)) && (!(ptr[2] & 0x01))) {
					sprintf(tmp, "Q.931 frame network->user with tei %d (not for us)", ptr[1] >> 1);
					dlogframe(sp, ptr + 4, ibh->datasize - 4, tmp);
				}
				BufPoolRelease(ibh);
			}
		}

	}

}

static void
isac_bh(struct IsdnCardState *sp)
{
	if (!sp)
		return;

	if (clear_bit(ISAC_PHCHANGE, &sp->event))
		process_new_ph(sp);
	if (clear_bit(ISAC_RCVBUFREADY, &sp->event))
		process_rcv(sp);
	if (clear_bit(ISAC_XMTBUFREADY, &sp->event))
		process_xmt(sp);
}


static void
hscx_process_xmt(struct HscxState *hsp)
{
	struct PStack  *st = hsp->st;

	if (hsp->xmtibh)
		return;

	if (st->l1.requestpull) {
		st->l1.requestpull = 0;
		st->l1.l1l2(st, PH_PULL_ACK, NULL);
	}
	if (!hsp->active)
		if ((!hsp->xmtibh) && (!hsp->sq.head))
			modehscx(hsp, 0, 0);
}

static void
hscx_process_rcv(struct HscxState *hsp)
{
	struct BufHeader *ibh;

#ifdef DEBUG_MAGIC
	if (hsp->magic != 301270) {
		printk(KERN_DEBUG "hscx_process_rcv magic not 301270\n");
		return;
	}
#endif
	while (!BufQueueUnlink(&ibh, &hsp->rq)) {
		hsp->st->l1.l1l2(hsp->st, PH_DATA, ibh);
	}
}

static void
hscx_bh(struct HscxState *hsp)
{

	if (!hsp)
		return;

	if (clear_bit(HSCX_RCVBUFREADY, &hsp->event))
		hscx_process_rcv(hsp);
	if (clear_bit(HSCX_XMTBUFREADY, &hsp->event))
		hscx_process_xmt(hsp);

}

/*
 * interrupt stuff ends here
 */

static void
restart_ph(struct IsdnCardState *sp)
{
	switch (sp->ph_active) {
	  case (0):
		  if (sp->ph_state == 6)
			  ph_command(sp, 0);
		  else
			  ph_command(sp, 1);
		  sp->ph_active = 1;
		  break;
	}
}

static void
initisac(byte * cardmem, int iobase)
{
        if (cardmem) {
                writeisac_0(cardmem, ISAC_MASK, 0xff);
                writeisac_0(cardmem, ISAC_ADF2, 0x0);
                writeisac_0(cardmem, ISAC_SPCR, 0xa);
                writeisac_0(cardmem, ISAC_ADF1, 0x2);
                writeisac_0(cardmem, ISAC_STCR, 0x70);
                writeisac_0(cardmem, ISAC_MODE, 0xc9);
                writeisac_0(cardmem, ISAC_CMDR, 0x41);
                writeisac_0(cardmem, ISAC_CIX0, (1 << 2) | 3);
        } else {
                writeisac_3(iobase, ISAC_MASK, 0xff);
                writeisac_3(iobase, ISAC_ADF2, 0x80);
                writeisac_3(iobase, ISAC_SQXR, 0x2f);
                writeisac_3(iobase, ISAC_SPCR, 0x00);
                writeisac_3(iobase, ISAC_ADF1, 0x02);
                writeisac_3(iobase, ISAC_STCR, 0x70);
                writeisac_3(iobase, ISAC_MODE, 0xc9);
                writeisac_3(iobase, ISAC_TIMR, 0x00);
                writeisac_3(iobase, ISAC_ADF1, 0x00);
                writeisac_3(iobase, ISAC_CMDR, 0x41);
                writeisac_3(iobase, ISAC_CIX0, (1 << 2) | 3);
        }
}

static int
checkcard(int cardnr)
{
	int             timout;
	byte            cfval, val;
	struct IsdnCard *card = cards + cardnr;

        if (card->membase)
                if ((unsigned long)card->membase < 0x10000) {
                        (unsigned long)card->membase <<= 4;
                        printk(KERN_INFO
                               "Teles membase configured DOSish, assuming 0x%lx\n",
                               (unsigned long)card->membase);
                }
        if (!card->iobase) {
                if (card->membase) {
                        printk(KERN_NOTICE
                               "Teles 8 assumed, mem: %lx irq: %d proto: %s\n",
                               (long) card->membase, card->interrupt,
                               (card->protocol == ISDN_PTYPE_1TR6) ?
                               "1TR6" : "EDSS1");
                        printk(KERN_INFO "HSCX version A:%x B:%x\n",
                               readhscx_0(card->membase, 0, HSCX_VSTR) & 0xf,
                               readhscx_0(card->membase, 1, HSCX_VSTR) & 0xf);
                }
        } else {
                switch (card->iobase) {
                        case 0x180:
                        case 0x280:
                        case 0x380:
                                card->iobase |= 0xc00;
                                break;
                }
                if (card->membase) {  /* 16.0 */
                	if (check_region(card->iobase, 8)) {
                        	printk(KERN_WARNING
                               		"teles: ports %x-%x already in use\n",
                               		card->iobase,
                               		card->iobase + 8 );
                        	return -1;
                	}
                } else { /* 16.3 */
                	if (check_region(card->iobase, 16)) {
                        	printk(KERN_WARNING
                               		"teles: 16.3 ports %x-%x already in use\n",
                               		card->iobase,
                               		card->iobase + 16 );
                        	return -1;
                	}
                	if (check_region((card->iobase - 0xc00) , 32)) {
                        	printk(KERN_WARNING
                  			"teles: 16.3 ports %x-%x already in use\n",
                               		card->iobase - 0xc00,
                               		card->iobase - 0xc00 + 32);
                        	return -1;
                        }
                	if (check_region((card->iobase - 0x800) , 32)) {
                        	printk(KERN_WARNING
                  			"teles: 16.3 ports %x-%x already in use\n",
                               		card->iobase - 0x800,
                               		card->iobase - 0x800 + 32);
                        	return -1;
                        }
                	if (check_region((card->iobase - 0x400) , 32)) {
                        	printk(KERN_WARNING
                  			"teles: 16.3 ports %x-%x already in use\n",
                               		card->iobase - 0x400,
                               		card->iobase - 0x400 + 32);
                        	return -1;
                        }
                }
                switch (card->interrupt) {
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
                if (card->membase) {
                        cfval |= (((unsigned int) card->membase >> 9) & 0xF0);
                }   
                if (bytein(card->iobase + 0) != 0x51) {
                        printk(KERN_INFO "XXX Byte at %x is %x\n",
                                card->iobase + 0,
                                bytein(card->iobase + 0));
                        return -2;
                }
                if (bytein(card->iobase + 1) != 0x93) {
                        printk(KERN_INFO "XXX Byte at %x is %x\n",
                                card->iobase + 1,
                                bytein(card->iobase + 1));
                        return -2;
                }
                val = bytein(card->iobase + 2);	/* 0x1e=without AB
                                                 * 0x1f=with AB
                                                 * 0x1c 16.3 ???
                                                 */
                if (val != 0x1c && val != 0x1e && val != 0x1f) {
                        printk(KERN_INFO "XXX Byte at %x is %x\n",
                                card->iobase + 2,
                                bytein(card->iobase + 2));
                        return -2;
                }
                if (card->membase) {  /* 16.0 */
                        request_region(card->iobase, 8, "teles 16.0");
                } else {
                	request_region(card->iobase, 16, "teles 16.3");
                	request_region(card->iobase - 0xC00, 32, "teles HSCX0");
                	request_region(card->iobase - 0x800, 32, "teles HSCX1");
                	request_region(card->iobase - 0x400, 32, "teles ISAC");
                }
                cli();
                timout = jiffies + (HZ / 10) + 1;
                byteout(card->iobase + 4, cfval);
                sti();
                while (jiffies <= timout);
                
                cli();
                timout = jiffies + (HZ / 10) + 1;
                byteout(card->iobase + 4, cfval | 1);
                sti();
                while (jiffies <= timout);
                
                if (card->membase)
                        printk(KERN_NOTICE
                               "Teles 16.0 found, io: %x mem: %lx irq: %d proto: %s\n",
                               card->iobase, (long) card->membase,
                               card->interrupt,
                               (card->protocol == ISDN_PTYPE_1TR6) ?
                               "1TR6" : "EDSS1");
                else
                        printk(KERN_NOTICE
                               "Teles 16.3 found, io: %x irq: %d proto: %s\n",
                               card->iobase, card->interrupt,
                               (card->protocol == ISDN_PTYPE_1TR6) ?
                               "1TR6" : "EDSS1");
                printk(KERN_INFO "HSCX version A:%x B:%x\n",
                       READHSCX(card->membase, card->iobase, 0,
                                HSCX_VSTR) & 0xf,
                       READHSCX(card->membase, card->iobase, 1,
                                HSCX_VSTR) & 0xf);
                
        }
        if (card->membase) {
                cli();
                timout = jiffies + (HZ / 5) + 1;
                *(byte *) (card->membase + 0x80) = 0;
                sti();
                while (jiffies <= timout);
                
                cli();
                *(byte *) (card->membase + 0x80) = 1;
                timout = jiffies + (HZ / 5) + 1;
                sti();
                while (jiffies <= timout);
        }
	return (0);
}

void
modehscx(struct HscxState *hs, int mode,
	 int ichan)
{
	struct IsdnCardState *sp = hs->sp;
	int             hscx = hs->hscx;

	printk(KERN_DEBUG "modehscx hscx %d mode %d ichan %d\n",
	       hscx, mode, ichan);

        hs->mode = mode;
        if (sp->membase) {
                /* What's that ??? KKeil */
		if (hscx == 0)
			ichan = 1 - ichan;	/* raar maar waar... */
                writehscx_0(sp->membase, hscx, HSCX_CCR1, 0x85);
                writehscx_0(sp->membase, hscx, HSCX_XAD1, 0xFF);
                writehscx_0(sp->membase, hscx, HSCX_XAD2, 0xFF);
                writehscx_0(sp->membase, hscx, HSCX_RAH2, 0xFF);
                writehscx_0(sp->membase, hscx, HSCX_XBCH, 0x0);

                switch (mode) {
                case (0):
                        writehscx_0(sp->membase, hscx, HSCX_CCR2, 0x30);
                        writehscx_0(sp->membase, hscx, HSCX_TSAX, 0xff);
                        writehscx_0(sp->membase, hscx, HSCX_TSAR, 0xff);
                        writehscx_0(sp->membase, hscx, HSCX_XCCR, 7);
                        writehscx_0(sp->membase, hscx, HSCX_RCCR, 7);
                        writehscx_0(sp->membase, hscx, HSCX_MODE, 0x84);
                        break;
                case (1):
                        if (ichan == 0) {
                                writehscx_0(sp->membase, hscx, HSCX_CCR2, 0x30);
                                writehscx_0(sp->membase, hscx, HSCX_TSAX, 0x7);
                                writehscx_0(sp->membase, hscx, HSCX_TSAR, 0x7);
                                writehscx_0(sp->membase, hscx, HSCX_XCCR, 7);
                                writehscx_0(sp->membase, hscx, HSCX_RCCR, 7);
                        } else {
                                writehscx_0(sp->membase, hscx, HSCX_CCR2, 0x30);
                                writehscx_0(sp->membase, hscx, HSCX_TSAX, 0x3);
                                writehscx_0(sp->membase, hscx, HSCX_TSAR, 0x3);
                                writehscx_0(sp->membase, hscx, HSCX_XCCR, 7);
                                writehscx_0(sp->membase, hscx, HSCX_RCCR, 7);
                        }
                        writehscx_0(sp->membase, hscx, HSCX_MODE, 0xe4);
                        writehscx_0(sp->membase, hscx, HSCX_CMDR, 0x41);
                        break;
                case (2):
                        if (ichan == 0) {
                                writehscx_0(sp->membase, hscx, HSCX_CCR2, 0x30);
                                writehscx_0(sp->membase, hscx, HSCX_TSAX, 0x7);
                                writehscx_0(sp->membase, hscx, HSCX_TSAR, 0x7);
                                writehscx_0(sp->membase, hscx, HSCX_XCCR, 7);
                                writehscx_0(sp->membase, hscx, HSCX_RCCR, 7);
                        } else {
                                writehscx_0(sp->membase, hscx, HSCX_CCR2, 0x30);
                                writehscx_0(sp->membase, hscx, HSCX_TSAX, 0x3);
                                writehscx_0(sp->membase, hscx, HSCX_TSAR, 0x3);
                                writehscx_0(sp->membase, hscx, HSCX_XCCR, 7);
                                writehscx_0(sp->membase, hscx, HSCX_RCCR, 7);
                        }
                        writehscx_0(sp->membase, hscx, HSCX_MODE, 0x8c);
                        writehscx_0(sp->membase, hscx, HSCX_CMDR, 0x41);
                        break;
                }
                writehscx_0(sp->membase, hscx, HSCX_ISTA, 0x00);
        } else {
                writehscx_3(sp->iobase, hscx, HSCX_CCR1, 0x85);
                writehscx_3(sp->iobase, hscx, HSCX_XAD1, 0xFF);
                writehscx_3(sp->iobase, hscx, HSCX_XAD2, 0xFF);
                writehscx_3(sp->iobase, hscx, HSCX_RAH2, 0xFF);
                writehscx_3(sp->iobase, hscx, HSCX_XBCH, 0x00);
                writehscx_3(sp->iobase, hscx, HSCX_RLCR, 0x00);
                
                switch (mode) {
                case (0):
                        writehscx_3(sp->iobase, hscx, HSCX_CCR2, 0x30);
                        writehscx_3(sp->iobase, hscx, HSCX_TSAX, 0xff);
                        writehscx_3(sp->iobase, hscx, HSCX_TSAR, 0xff);
                        writehscx_3(sp->iobase, hscx, HSCX_XCCR, 7);
                        writehscx_3(sp->iobase, hscx, HSCX_RCCR, 7);
                        writehscx_3(sp->iobase, hscx, HSCX_MODE, 0x84);
                        break;
                case (1):
                        if (ichan == 0) {
                                writehscx_3(sp->iobase, hscx, HSCX_CCR2, 0x30);
                                writehscx_3(sp->iobase, hscx, HSCX_TSAX, 0x2f);
                                writehscx_3(sp->iobase, hscx, HSCX_TSAR, 0x2f);
                                writehscx_3(sp->iobase, hscx, HSCX_XCCR, 7);
                                writehscx_3(sp->iobase, hscx, HSCX_RCCR, 7);
                        } else {
                                writehscx_3(sp->iobase, hscx, HSCX_CCR2, 0x30);
                                writehscx_3(sp->iobase, hscx, HSCX_TSAX, 0x3);
                                writehscx_3(sp->iobase, hscx, HSCX_TSAR, 0x3);
                                writehscx_3(sp->iobase, hscx, HSCX_XCCR, 7);
                                writehscx_3(sp->iobase, hscx, HSCX_RCCR, 7);
                        }
                        writehscx_3(sp->iobase, hscx, HSCX_MODE, 0xe4);
                        writehscx_3(sp->iobase, hscx, HSCX_CMDR, 0x41);
                        break;
                case (2):
                        if (ichan == 0) {
                                writehscx_3(sp->iobase, hscx, HSCX_CCR2, 0x30);
                                writehscx_3(sp->iobase, hscx, HSCX_TSAX, 0x2f);
                                writehscx_3(sp->iobase, hscx, HSCX_TSAR, 0x2f);
                                writehscx_3(sp->iobase, hscx, HSCX_XCCR, 7);
                                writehscx_3(sp->iobase, hscx, HSCX_RCCR, 7);
                        } else {
                                writehscx_3(sp->iobase, hscx, HSCX_CCR2, 0x30);
                                writehscx_3(sp->iobase, hscx, HSCX_TSAX, 0x3);
                                writehscx_3(sp->iobase, hscx, HSCX_TSAR, 0x3);
                                writehscx_3(sp->iobase, hscx, HSCX_XCCR, 7);
                                writehscx_3(sp->iobase, hscx, HSCX_RCCR, 7);
                        }
                        writehscx_3(sp->iobase, hscx, HSCX_MODE, 0x8c);
                        writehscx_3(sp->iobase, hscx, HSCX_CMDR, 0x41);
                        break;
                }
                writehscx_3(sp->iobase, hscx, HSCX_ISTA, 0x00);
        }
}

void
teles_addlist(struct IsdnCardState *sp,
	      struct PStack *st)
{
	st->next = sp->stlist;
	sp->stlist = st;
}

void
teles_rmlist(struct IsdnCardState *sp,
	     struct PStack *st)
{
	struct PStack  *p;

	if (sp->stlist == st)
		sp->stlist = st->next;
	else {
		p = sp->stlist;
		while (p)
			if (p->next == st) {
				p->next = st->next;
				return;
			} else
				p = p->next;
	}
}


static void
teles_l2l1(struct PStack *st, int pr,
	   struct BufHeader *ibh)
{
	struct IsdnCardState *sp = (struct IsdnCardState *)
	st->l1.hardware;


	switch (pr) {
	  case (PH_DATA):
		  if (sp->xmtibh)
			  BufQueueLink(&sp->sq, ibh);
		  else {
			  sp->xmtibh = ibh;
			  sp->sendptr = 0;
			  sp->releasebuf = !0;
			  fill_fifo(sp);
		  }
		  break;
	  case (PH_DATA_PULLED):
		  if (sp->xmtibh) {
			  printk(KERN_DEBUG "teles_l2l1: this shouldn't happen\n");
			  break;
		  }
		  sp->xmtibh = ibh;
		  sp->sendptr = 0;
		  sp->releasebuf = 0;
		  fill_fifo(sp);
		  break;
	  case (PH_REQUEST_PULL):
		  if (!sp->xmtibh) {
			  st->l1.requestpull = 0;
			  st->l1.l1l2(st, PH_PULL_ACK, NULL);
		  } else
			  st->l1.requestpull = !0;
		  break;
	}
}

static void
check_ph_act(struct IsdnCardState *sp)
{
	struct PStack  *st = sp->stlist;

	while (st) {
		if (st->l1.act_state)
			return;
		st = st->next;
	}
	sp->ph_active = 0;
}

static void
teles_manl1(struct PStack *st, int pr,
	    void *arg)
{
	struct IsdnCardState *sp = (struct IsdnCardState *)
	st->l1.hardware;
	long            flags;

	switch (pr) {
	  case (PH_ACTIVATE):
		  save_flags(flags);
		  cli();
		  if (sp->ph_active == 5) {
			  st->l1.act_state = 2;
			  restore_flags(flags);
			  st->l1.l1man(st, PH_ACTIVATE, NULL);
		  } else {
			  st->l1.act_state = 1;
			  if (sp->ph_active == 0)
				  restart_ph(sp);
			  restore_flags(flags);
		  }
		  break;
	  case (PH_DEACTIVATE):
		  st->l1.act_state = 0;
		  check_ph_act(sp);
		  break;
	}
}

static void
teles_l2l1discardq(struct PStack *st, int pr,
		   void *heldby, int releasetoo)
{
	struct IsdnCardState *sp = (struct IsdnCardState *) st->l1.hardware;

#ifdef DEBUG_MAGIC
	if (sp->magic != 301271) {
		printk(KERN_DEBUG "isac_discardq magic not 301271\n");
		return;
	}
#endif

	BufQueueDiscard(&sp->sq, pr, heldby, releasetoo);
}

void
setstack_teles(struct PStack *st, struct IsdnCardState *sp)
{
	st->l1.hardware = sp;
	st->l1.sbufpool = &(sp->sbufpool);
	st->l1.rbufpool = &(sp->rbufpool);
	st->l1.smallpool = &(sp->smallpool);
	st->protocol = sp->teistack->protocol;

	setstack_tei(st);

	st->l1.stlistp = &(sp->stlist);
	st->l1.act_state = 0;
	st->l2.l2l1 = teles_l2l1;
	st->l2.l2l1discardq = teles_l2l1discardq;
	st->ma.manl1 = teles_manl1;
	st->l1.requestpull = 0;
}

void
init_hscxstate(struct IsdnCardState *sp,
	       int hscx)
{
	struct HscxState *hsp = sp->hs + hscx;

	hsp->sp = sp;
	hsp->hscx = hscx;
	hsp->membase = sp->membase;
	hsp->iobase = sp->iobase;

	hsp->tqueue.next = 0;
	hsp->tqueue.sync = 0;
	hsp->tqueue.routine = (void *) (void *) hscx_bh;
	hsp->tqueue.data = hsp;

	hsp->inuse = 0;
	hsp->init = 0;
	hsp->active = 0;

#ifdef DEBUG_MAGIC
	hsp->magic = 301270;
#endif
}

void
initcard(int cardnr)
{
	struct IsdnCardState *sp;
	struct IsdnCard *card = cards + cardnr;

	sp = (struct IsdnCardState *)
	    Smalloc(sizeof(struct IsdnCardState), GFP_KERNEL,
		    "struct IsdnCardState");

	sp->membase = card->membase;
	sp->iobase = card->iobase;
	sp->cardnr = cardnr;

	BufPoolInit(&sp->sbufpool, ISAC_SBUF_ORDER, ISAC_SBUF_BPPS,
		    ISAC_SBUF_MAXPAGES);
	BufPoolInit(&sp->rbufpool, ISAC_RBUF_ORDER, ISAC_RBUF_BPPS,
		    ISAC_RBUF_MAXPAGES);
	BufPoolInit(&sp->smallpool, ISAC_SMALLBUF_ORDER, ISAC_SMALLBUF_BPPS,
		    ISAC_SMALLBUF_MAXPAGES);

	sp->dlogspace = Smalloc(4096, GFP_KERNEL, "dlogspace");

	initisac(card->membase, card->iobase);

	sp->rcvibh = NULL;
	sp->rcvptr = 0;
	sp->xmtibh = NULL;
	sp->sendptr = 0;
	sp->event = 0;
	sp->tqueue.next = 0;
	sp->tqueue.sync = 0;
	sp->tqueue.routine = (void *) (void *) isac_bh;
	sp->tqueue.data = sp;

	BufQueueInit(&sp->rq);
	BufQueueInit(&sp->sq);

	sp->stlist = NULL;

	sp->ph_active = 0;

	sp->dlogflag = 0;
	sp->debug = 0;

	sp->releasebuf = 0;
#ifdef DEBUG_MAGIC
	sp->magic = 301271;
#endif

	cards[sp->cardnr].sp = sp;

	init_hscxstate(sp, 0);
	init_hscxstate(sp, 1);

	modehscx(sp->hs, 0, 0);
	modehscx(sp->hs + 1, 0, 0);

	WRITEISAC(sp->membase, sp->iobase, ISAC_MASK, 0x0);
}

static int
get_irq(int cardnr)
{
	struct IsdnCard *card = cards + cardnr;
	long            flags;

	save_flags(flags);
	cli();
	if (request_irq(card->interrupt, &teles_interrupt,
			SA_INTERRUPT, "teles", NULL)) {
		printk(KERN_WARNING "Teles couldn't get interrupt %d\n",
                       card->interrupt);
		restore_flags(flags);
		return (!0);
	}
	irq2dev_map[card->interrupt] = (void *) card->sp;
	restore_flags(flags);
	return (0);
}

static void
release_irq(int cardnr)
{
	struct	IsdnCard *card = cards + cardnr;

	irq2dev_map[card->interrupt] = NULL;
	free_irq(card->interrupt, NULL);
}

void
close_hscxstate(struct HscxState *hs)
{
	modehscx(hs, 0, 0);
	hs->inuse = 0;

	if (hs->init) {
		BufPoolFree(&hs->smallpool);
		BufPoolFree(&hs->rbufpool);
		BufPoolFree(&hs->sbufpool);
	}
	hs->init = 0;
}

void
closecard(int cardnr)
{
	struct IsdnCardState *sp = cards[cardnr].sp;

	cards[cardnr].sp = NULL;

	Sfree(sp->dlogspace);

	BufPoolFree(&sp->smallpool);
	BufPoolFree(&sp->rbufpool);
	BufPoolFree(&sp->sbufpool);

	close_hscxstate(sp->hs + 1);
	close_hscxstate(sp->hs);

	if (cards[cardnr].iobase)
        	if (cards[cardnr].membase) {  /* 16.0 */
			release_region(cards[cardnr].iobase, 8);
        	} else {
			release_region(cards[cardnr].iobase, 16);
                	release_region(cards[cardnr].iobase - 0xC00, 32);
                	release_region(cards[cardnr].iobase - 0x800, 32);
                	release_region(cards[cardnr].iobase - 0x400, 32);
                }

	Sfree((void *) sp);
}

void
teles_shiftcards(int idx)
{
        int i;

        for (i = idx; i < 15; i++)
                memcpy(&cards[i],&cards[i+1],sizeof(cards[i]));
}

int
teles_inithardware(void)
{
        int             foundcards = 0;
	int             i = 0;

	while (i < nrcards) {
                if (!cards[i].protocol)
                        break;
		switch (checkcard(i)) {
		  case (0):
			  initcard(i);
			  if (get_irq(i)) {
				  closecard(i);
                                  teles_shiftcards(i);
                          } else {
                                  foundcards++;
                                  i++;
                          }
			  break;
		  case (-1):
                          teles_shiftcards(i);
			  break;
		  case (-2):
			  release_region(cards[i].iobase, 8);
			  printk(KERN_WARNING "NO Teles card found at 0x%x!\n", cards[i].iobase);
                          teles_shiftcards(i);
			  break;
		}
        }
        return foundcards;
}

void
teles_closehardware(void)
{
	int             i;

	for (i = 0; i < nrcards; i++)
		if (cards[i].sp) {
			release_irq(i);
			closecard(i);
		}
}

static void
hscx_l2l1(struct PStack *st, int pr,
	  struct BufHeader *ibh)
{
	struct IsdnCardState *sp = (struct IsdnCardState *)
	st->l1.hardware;
	struct HscxState *hsp = sp->hs + st->l1.hscx;
	long flags;

	switch (pr) {
                case (PH_DATA):
			save_flags(flags);
			cli();
                        if (hsp->xmtibh) {
                                BufQueueLink(&hsp->sq, ibh);
				restore_flags(flags);
			}
                        else {
				restore_flags(flags);
                                hsp->xmtibh = ibh;
                                hsp->sendptr = 0;
                                hsp->releasebuf = !0;
                                hscx_fill_fifo(hsp);
                        }
                        break;
                case (PH_DATA_PULLED):
                        if (hsp->xmtibh) {
                                printk(KERN_DEBUG "hscx_l2l1: this shouldn't happen\n");
                                break;
                        }
                        hsp->xmtibh = ibh;
                        hsp->sendptr = 0;
                        hsp->releasebuf = 0;
                        hscx_fill_fifo(hsp);
                        break;
                case (PH_REQUEST_PULL):
                        if (!hsp->xmtibh) {
                                st->l1.requestpull = 0;
                                st->l1.l1l2(st, PH_PULL_ACK, NULL);
                        } else
                                st->l1.requestpull = !0;
                        break;
	}
        
}

extern struct IsdnBuffers *tracebuf;

static void
hscx_l2l1discardq(struct PStack *st, int pr, void *heldby,
		  int releasetoo)
{
	struct IsdnCardState *sp = (struct IsdnCardState *)
	st->l1.hardware;
	struct HscxState *hsp = sp->hs + st->l1.hscx;

#ifdef DEBUG_MAGIC
	if (hsp->magic != 301270) {
		printk(KERN_DEBUG "hscx_discardq magic not 301270\n");
		return;
	}
#endif

	BufQueueDiscard(&hsp->sq, pr, heldby, releasetoo);
}

static int
open_hscxstate(struct IsdnCardState *sp,
	       int hscx)
{
	struct HscxState *hsp = sp->hs + hscx;

	if (!hsp->init) {
		BufPoolInit(&hsp->sbufpool, HSCX_SBUF_ORDER, HSCX_SBUF_BPPS,
			    HSCX_SBUF_MAXPAGES);
		BufPoolInit(&hsp->rbufpool, HSCX_RBUF_ORDER, HSCX_RBUF_BPPS,
			    HSCX_RBUF_MAXPAGES);
		BufPoolInit(&hsp->smallpool, HSCX_SMALLBUF_ORDER, HSCX_SMALLBUF_BPPS,
			    HSCX_SMALLBUF_MAXPAGES);
	}
	hsp->init = !0;

	BufQueueInit(&hsp->rq);
	BufQueueInit(&hsp->sq);

	hsp->releasebuf = 0;
	hsp->rcvibh = NULL;
	hsp->xmtibh = NULL;
	hsp->rcvptr = 0;
	hsp->sendptr = 0;
	hsp->event = 0;
	return (0);
}

static void
hscx_manl1(struct PStack *st, int pr,
	   void *arg)
{
	struct IsdnCardState *sp = (struct IsdnCardState *)
	st->l1.hardware;
	struct HscxState *hsp = sp->hs + st->l1.hscx;

	switch (pr) {
	  case (PH_ACTIVATE):
		  hsp->active = !0;
		  modehscx(hsp, st->l1.hscxmode, st->l1.hscxchannel);
		  st->l1.l1man(st, PH_ACTIVATE, NULL);
		  break;
	  case (PH_DEACTIVATE):
		  if (!hsp->xmtibh)
			  modehscx(hsp, 0, 0);

		  hsp->active = 0;
		  break;
	}
}

int
setstack_hscx(struct PStack *st, struct HscxState *hs)
{
	if (open_hscxstate(st->l1.hardware, hs->hscx))
		return (-1);

	st->l1.hscx = hs->hscx;
	st->l2.l2l1 = hscx_l2l1;
	st->ma.manl1 = hscx_manl1;
	st->l2.l2l1discardq = hscx_l2l1discardq;

	st->l1.sbufpool = &hs->sbufpool;
	st->l1.rbufpool = &hs->rbufpool;
	st->l1.smallpool = &hs->smallpool;
	st->l1.act_state = 0;
	st->l1.requestpull = 0;

	hs->st = st;
	return (0);
}

void
teles_reportcard(int cardnr)
{
	printk(KERN_DEBUG "teles_reportcard\n");
}
