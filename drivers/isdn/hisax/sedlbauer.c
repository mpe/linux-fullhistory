/* $Id: sedlbauer.c,v 1.6 1998/02/09 18:46:06 keil Exp $

 * sedlbauer.c  low level stuff for Sedlbauer cards
 *              includes Support for the Sedlbauer Speed Star 
 *              derived from the original file dynalink.c from Karsten Keil
 *
 * Copyright (C) 1997,1998 Marcus Niemann (for the modifications to
 *                                         the original file dynalink.c)
 *
 * Author     Marcus Niemann (niemann@www-bib.fh-bielefeld.de)
 *
 * Thanks to  Karsten Keil
 *            Sedlbauer AG for informations
 *            Edgar Toernig
 *
 * $Log: sedlbauer.c,v $
 * Revision 1.6  1998/02/09 18:46:06  keil
 * Support for Sedlbauer PCMCIA (Marcus Niemann)
 *
 * Revision 1.5  1998/02/02 13:29:45  keil
 * fast io
 *
 * Revision 1.4  1997/11/08 21:35:52  keil
 * new l1 init
 *
 * Revision 1.3  1997/11/06 17:09:28  keil
 * New 2.1 init code
 *
 * Revision 1.2  1997/10/29 18:55:52  keil
 * changes for 2.1.60 (irq2dev_map)
 *
 * Revision 1.1  1997/09/11 17:32:04  keil
 * new
 *
 *
 */

#define __NO_VERSION__
#include "hisax.h"
#include "isac.h"
#include "hscx.h"
#include "isdnl1.h"

extern const char *CardType[];

const char *Sedlbauer_revision = "$Revision: 1.6 $";

const char *Sedlbauer_Types[] =
{"None", "Speed Card", "Speed Win", "Speed Star"};
 
#define SEDL_SPEED_CARD 1
#define SEDL_SPEED_WIN  2
#define SEDL_SPEED_STAR 3

#define byteout(addr,val) outb(val,addr)
#define bytein(addr) inb(addr)

#define SEDL_RESET_ON	0
#define SEDL_RESET_OFF	1
#define SEDL_ISAC	2
#define SEDL_HSCX	3
#define SEDL_ADR	4

#define SEDL_PCMCIA_RESET	0
#define SEDL_PCMCIA_ISAC	1
#define SEDL_PCMCIA_HSCX	2
#define SEDL_PCMCIA_ADR		4

#define SEDL_RESET      0x3	/* same as DOS driver */

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
writefifo(unsigned int ale, unsigned int adr, u_char off, u_char * data, int size)
{
	/* fifo write without cli because it's allready done  */
	byteout(ale, off);
	outsb(adr, data, size);
}

/* Interface functions */

static u_char
ReadISAC(struct IsdnCardState *cs, u_char offset)
{
	return (readreg(cs->hw.sedl.adr, cs->hw.sedl.isac, offset));
}

static void
WriteISAC(struct IsdnCardState *cs, u_char offset, u_char value)
{
	writereg(cs->hw.sedl.adr, cs->hw.sedl.isac, offset, value);
}

static void
ReadISACfifo(struct IsdnCardState *cs, u_char * data, int size)
{
	readfifo(cs->hw.sedl.adr, cs->hw.sedl.isac, 0, data, size);
}

static void
WriteISACfifo(struct IsdnCardState *cs, u_char * data, int size)
{
	writefifo(cs->hw.sedl.adr, cs->hw.sedl.isac, 0, data, size);
}

static u_char
ReadHSCX(struct IsdnCardState *cs, int hscx, u_char offset)
{
	return (readreg(cs->hw.sedl.adr,
			cs->hw.sedl.hscx, offset + (hscx ? 0x40 : 0)));
}

static void
WriteHSCX(struct IsdnCardState *cs, int hscx, u_char offset, u_char value)
{
	writereg(cs->hw.sedl.adr,
		 cs->hw.sedl.hscx, offset + (hscx ? 0x40 : 0), value);
}

/*
 * fast interrupt HSCX stuff goes here
 */

#define READHSCX(cs, nr, reg) readreg(cs->hw.sedl.adr, \
		cs->hw.sedl.hscx, reg + (nr ? 0x40 : 0))
#define WRITEHSCX(cs, nr, reg, data) writereg(cs->hw.sedl.adr, \
		cs->hw.sedl.hscx, reg + (nr ? 0x40 : 0), data)

#define READHSCXFIFO(cs, nr, ptr, cnt) readfifo(cs->hw.sedl.adr, \
		cs->hw.sedl.hscx, (nr ? 0x40 : 0), ptr, cnt)

#define WRITEHSCXFIFO(cs, nr, ptr, cnt) writefifo(cs->hw.sedl.adr, \
		cs->hw.sedl.hscx, (nr ? 0x40 : 0), ptr, cnt)

#include "hscx_irq.c"

static void
sedlbauer_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs = dev_id;
	u_char val, stat = 0;

	if (!cs) {
		printk(KERN_WARNING "Sedlbauer: Spurious interrupt!\n");
		return;
	}

        if ((cs->typ == ISDN_CTYPE_SEDLBAUER_PCMCIA) && (*cs->busy_flag == 1)) {
          /* The card tends to generate interrupts while being removed
             causing us to just crash the kernel. bad. */
          printk(KERN_WARNING "Sedlbauer: card not available!\n");
          return;
        }

	val = readreg(cs->hw.sedl.adr, cs->hw.sedl.hscx, HSCX_ISTA + 0x40);
      Start_HSCX:
	if (val) {
		hscx_int_main(cs, val);
		stat |= 1;
	}
	val = readreg(cs->hw.sedl.adr, cs->hw.sedl.isac, ISAC_ISTA);
      Start_ISAC:
	if (val) {
		isac_interrupt(cs, val);
		stat |= 2;
	}
	val = readreg(cs->hw.sedl.adr, cs->hw.sedl.hscx, HSCX_ISTA + 0x40);
	if (val) {
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "HSCX IntStat after IntRoutine");
		goto Start_HSCX;
	}
	val = readreg(cs->hw.sedl.adr, cs->hw.sedl.isac, ISAC_ISTA);
	if (val) {
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "ISAC IntStat after IntRoutine");
		goto Start_ISAC;
	}
	if (stat & 1) {
		writereg(cs->hw.sedl.adr, cs->hw.sedl.hscx, HSCX_MASK, 0xFF);
		writereg(cs->hw.sedl.adr, cs->hw.sedl.hscx, HSCX_MASK + 0x40, 0xFF);
		writereg(cs->hw.sedl.adr, cs->hw.sedl.hscx, HSCX_MASK, 0x0);
		writereg(cs->hw.sedl.adr, cs->hw.sedl.hscx, HSCX_MASK + 0x40, 0x0);
	}
	if (stat & 2) {
		writereg(cs->hw.sedl.adr, cs->hw.sedl.isac, ISAC_MASK, 0xFF);
		writereg(cs->hw.sedl.adr, cs->hw.sedl.isac, ISAC_MASK, 0x0);
	}
}

void
release_io_sedlbauer(struct IsdnCardState *cs)
{
	int bytecnt = 8;

	if (cs->hw.sedl.cfg_reg)
		release_region(cs->hw.sedl.cfg_reg, bytecnt);
}

static void
reset_sedlbauer(struct IsdnCardState *cs)
{
	long flags;

	if (cs->typ != ISDN_CTYPE_SEDLBAUER_PCMCIA) {
		byteout(cs->hw.sedl.reset_on, SEDL_RESET);	/* Reset On */
		save_flags(flags);
		sti();
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(1);
		byteout(cs->hw.sedl.reset_off, 0);	/* Reset Off */
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(1);
		restore_flags(flags);
	}
}

static int
Sedl_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
	switch (mt) {
		case CARD_RESET:
			reset_sedlbauer(cs);
			return(0);
		case CARD_RELEASE:
			release_io_sedlbauer(cs);
			return(0);
		case CARD_SETIRQ:
			return(request_irq(cs->irq, &sedlbauer_interrupt,
					I4L_IRQ_FLAG, "HiSax", cs));
		case CARD_INIT:
			clear_pending_isac_ints(cs);
			clear_pending_hscx_ints(cs);
			initisac(cs);
			inithscx(cs);
			return(0);
		case CARD_TEST:
			return(0);
	}
	return(0);
}

__initfunc(int
setup_sedlbauer(struct IsdnCard *card))
{
	int bytecnt;
	struct IsdnCardState *cs = card->cs;
	char tmp[64];

	strcpy(tmp, Sedlbauer_revision);
	printk(KERN_INFO "HiSax: Sedlbauer driver Rev. %s\n", HiSax_getrev(tmp));
 	if (cs->typ == ISDN_CTYPE_SEDLBAUER) {
 		cs->subtyp = SEDL_SPEED_CARD;
 	} else if (cs->typ == ISDN_CTYPE_SEDLBAUER_PCMCIA) {	
 		cs->subtyp = SEDL_SPEED_STAR;
 	} else
		return (0);

	bytecnt = 8;
	cs->hw.sedl.cfg_reg = card->para[1];
	cs->irq = card->para[0];
	if (cs->subtyp == SEDL_SPEED_STAR) {
		cs->hw.sedl.adr = cs->hw.sedl.cfg_reg + SEDL_PCMCIA_ADR;
		cs->hw.sedl.isac = cs->hw.sedl.cfg_reg + SEDL_PCMCIA_ISAC;
		cs->hw.sedl.hscx = cs->hw.sedl.cfg_reg + SEDL_PCMCIA_HSCX;
		cs->hw.sedl.reset_on = cs->hw.sedl.cfg_reg + SEDL_PCMCIA_RESET;
		cs->hw.sedl.reset_off = cs->hw.sedl.cfg_reg + SEDL_PCMCIA_RESET;
	} else {
		cs->hw.sedl.adr = cs->hw.sedl.cfg_reg + SEDL_ADR;
		cs->hw.sedl.isac = cs->hw.sedl.cfg_reg + SEDL_ISAC;
		cs->hw.sedl.hscx = cs->hw.sedl.cfg_reg + SEDL_HSCX;
		cs->hw.sedl.reset_on = cs->hw.sedl.cfg_reg + SEDL_RESET_ON;
		cs->hw.sedl.reset_off = cs->hw.sedl.cfg_reg + SEDL_RESET_OFF;
	}
        
	/* In case of the sedlbauer pcmcia card, this region is in use,
           reserved for us by the card manager. So we do not check it
           here, it would fail. */
	if (cs->typ != ISDN_CTYPE_SEDLBAUER_PCMCIA &&
	   check_region((cs->hw.sedl.cfg_reg), bytecnt)) {
		printk(KERN_WARNING
		       "HiSax: %s config port %x-%x already in use\n",
		       CardType[card->typ],
		       cs->hw.sedl.cfg_reg,
		       cs->hw.sedl.cfg_reg + bytecnt);
		return (0);
	} else {
		request_region(cs->hw.sedl.cfg_reg, bytecnt, "sedlbauer isdn");
	}

	printk(KERN_INFO
	       "Sedlbauer: defined at 0x%x IRQ %d\n",
	       cs->hw.sedl.cfg_reg,
	       cs->irq);
	printk(KERN_WARNING
		       "Sedlbauer %s uses ports 0x%x-0x%x\n",
		       Sedlbauer_Types[cs->subtyp],
		       cs->hw.sedl.cfg_reg,
		       cs->hw.sedl.cfg_reg + bytecnt);

	printk(KERN_INFO "Sedlbauer: resetting card\n");
	reset_sedlbauer(cs);
	cs->readisac = &ReadISAC;
	cs->writeisac = &WriteISAC;
	cs->readisacfifo = &ReadISACfifo;
	cs->writeisacfifo = &WriteISACfifo;
	cs->BC_Read_Reg = &ReadHSCX;
	cs->BC_Write_Reg = &WriteHSCX;
	cs->BC_Send_Data = &hscx_fill_fifo;
	cs->cardmsg = &Sedl_card_msg;
	ISACVersion(cs, "Sedlbauer:");
	if (HscxVersion(cs, "Sedlbauer:")) {
		printk(KERN_WARNING
		    "Sedlbauer: wrong HSCX versions check IO address\n");
		release_io_sedlbauer(cs);
		return (0);
	}
	return (1);
}
