/* $Id: elsa.c,v 2.6 1998/02/02 13:29:40 keil Exp $

 * elsa.c     low level stuff for Elsa isdn cards
 *
 * Author     Karsten Keil (keil@temic-ech.spacenet.de)
 *
 * Thanks to    Elsa GmbH for documents and informations
 *
 *
 * $Log: elsa.c,v $
 * Revision 2.6  1998/02/02 13:29:40  keil
 * fast io
 *
 * Revision 2.5  1998/01/31 21:41:45  keil
 * changes for newer 2.1 kernels
 *
 * Revision 2.4  1997/11/08 21:35:46  keil
 * new l1 init
 *
 * Revision 2.3  1997/11/06 17:15:09  keil
 * New 2.1 init; PCMCIA wrapper changes
 *
 * Revision 2.2  1997/10/29 18:57:09  keil
 * changes for 2.1.60, arcofi support
 *
 * Revision 2.1  1997/07/27 21:47:08  keil
 * new interface structures
 *
 * Revision 2.0  1997/06/26 11:02:40  keil
 * New Layer and card interface
 *
 * Revision 1.14  1997/04/13 19:53:25  keil
 * Fixed QS1000 init, change in IRQ check delay for SMP
 *
 * Revision 1.13  1997/04/07 22:58:07  keil
 * need include config.h
 *
 * Revision 1.12  1997/04/06 22:54:14  keil
 * Using SKB's
 *
 * old changes removed KKe
 *
 */

#define __NO_VERSION__
#include <linux/config.h>
#include "hisax.h"
#include "arcofi.h"
#include "isac.h"
#include "ipac.h"
#include "hscx.h"
#include "isdnl1.h"
#include <linux/pci.h>
#include <linux/bios32.h>

extern const char *CardType[];

const char *Elsa_revision = "$Revision: 2.6 $";
const char *Elsa_Types[] =
{"None", "PC", "PCC-8", "PCC-16", "PCF", "PCF-Pro",
 "PCMCIA", "QS 1000", "QS 3000", "QS 1000 PCI"};

const char *ITACVer[] =
{"?0?", "?1?", "?2?", "?3?", "?4?", "V2.2",
 "B1", "A1"};

#define byteout(addr,val) outb(val,addr)
#define bytein(addr) inb(addr)

#define ELSA_ISAC	0
#define ELSA_ISAC_PCM	1
#define ELSA_ITAC	1
#define ELSA_HSCX	2
#define ELSA_ALE	3
#define ELSA_ALE_PCM	4
#define ELSA_CONTROL	4
#define ELSA_CONFIG	5
#define ELSA_START_TIMER 6
#define ELSA_TRIG_IRQ	7

#define ELSA_PC      1
#define ELSA_PCC8    2
#define ELSA_PCC16   3
#define ELSA_PCF     4
#define ELSA_PCFPRO  5
#define ELSA_PCMCIA  6
#define ELSA_QS1000  7
#define ELSA_QS3000  8
#define ELSA_QS1000PCI 9

/* PCI stuff */
#define PCI_VENDOR_ELSA	0x1048
#define PCI_QS1000_ID	0x1000


/* ITAC Registeradressen (only Microlink PC) */
#define ITAC_SYS	0x34
#define ITAC_ISEN	0x48
#define ITAC_RFIE	0x4A
#define ITAC_XFIE	0x4C
#define ITAC_SCIE	0x4E
#define ITAC_STIE	0x46

/***                                                                    ***
 ***   Makros als Befehle fuer die Kartenregister                       ***
 ***   (mehrere Befehle werden durch Bit-Oderung kombiniert)            ***
 ***                                                                    ***/

/* Config-Register (Read) */
#define ELSA_TIMER_RUN       0x02	/* Bit 1 des Config-Reg     */
#define ELSA_TIMER_RUN_PCC8  0x01	/* Bit 0 des Config-Reg  bei PCC */
#define ELSA_IRQ_IDX       0x38	/* Bit 3,4,5 des Config-Reg */
#define ELSA_IRQ_IDX_PCC8  0x30	/* Bit 4,5 des Config-Reg */
#define ELSA_IRQ_IDX_PC    0x0c	/* Bit 2,3 des Config-Reg */

/* Control-Register (Write) */
#define ELSA_LINE_LED        0x02	/* Bit 1 Gelbe LED */
#define ELSA_STAT_LED        0x08	/* Bit 3 Gruene LED */
#define ELSA_ISDN_RESET      0x20	/* Bit 5 Reset-Leitung */
#define ELSA_ENA_TIMER_INT   0x80	/* Bit 7 Freigabe Timer Interrupt */

/* ALE-Register (Read) */
#define ELSA_HW_RELEASE      0x07	/* Bit 0-2 Hardwarerkennung */
#define ELSA_S0_POWER_BAD    0x08	/* Bit 3 S0-Bus Spannung fehlt */

/* Status Flags */
#define ELSA_TIMER_AKTIV 1
#define ELSA_BAD_PWR     2
#define ELSA_ASSIGN      4

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
	return (readreg(cs->hw.elsa.ale, cs->hw.elsa.isac, offset));
}

static void
WriteISAC(struct IsdnCardState *cs, u_char offset, u_char value)
{
	writereg(cs->hw.elsa.ale, cs->hw.elsa.isac, offset, value);
}

static void
ReadISACfifo(struct IsdnCardState *cs, u_char * data, int size)
{
	readfifo(cs->hw.elsa.ale, cs->hw.elsa.isac, 0, data, size);
}

static void
WriteISACfifo(struct IsdnCardState *cs, u_char * data, int size)
{
	writefifo(cs->hw.elsa.ale, cs->hw.elsa.isac, 0, data, size);
}

static u_char
ReadISAC_IPAC(struct IsdnCardState *cs, u_char offset)
{
	return (readreg(cs->hw.elsa.ale, cs->hw.elsa.isac, offset+0x80));
}

static void
WriteISAC_IPAC(struct IsdnCardState *cs, u_char offset, u_char value)
{
	writereg(cs->hw.elsa.ale, cs->hw.elsa.isac, offset|0x80, value);
}

static void
ReadISACfifo_IPAC(struct IsdnCardState *cs, u_char * data, int size)
{
	readfifo(cs->hw.elsa.ale, cs->hw.elsa.isac, 0x80, data, size);
}

static void
WriteISACfifo_IPAC(struct IsdnCardState *cs, u_char * data, int size)
{
	writefifo(cs->hw.elsa.ale, cs->hw.elsa.isac, 0x80, data, size);
}

static u_char
ReadHSCX(struct IsdnCardState *cs, int hscx, u_char offset)
{
	return (readreg(cs->hw.elsa.ale,
			cs->hw.elsa.hscx, offset + (hscx ? 0x40 : 0)));
}

static void
WriteHSCX(struct IsdnCardState *cs, int hscx, u_char offset, u_char value)
{
	writereg(cs->hw.elsa.ale,
		 cs->hw.elsa.hscx, offset + (hscx ? 0x40 : 0), value);
}

static inline u_char
readitac(struct IsdnCardState *cs, u_char off)
{
	register u_char ret;
	long flags;

	save_flags(flags);
	cli();
	byteout(cs->hw.elsa.ale, off);
	ret = bytein(cs->hw.elsa.itac);
	restore_flags(flags);
	return (ret);
}

static inline void
writeitac(struct IsdnCardState *cs, u_char off, u_char data)
{
	long flags;

	save_flags(flags);
	cli();
	byteout(cs->hw.elsa.ale, off);
	byteout(cs->hw.elsa.itac, data);
	restore_flags(flags);
}

static inline int
TimerRun(struct IsdnCardState *cs)
{
	register u_char v;

	v = bytein(cs->hw.elsa.cfg);
	if ((cs->subtyp == ELSA_QS1000) || (cs->subtyp == ELSA_QS3000))
		return (0 == (v & ELSA_TIMER_RUN));
	else if (cs->subtyp == ELSA_PCC8)
		return (v & ELSA_TIMER_RUN_PCC8);
	return (v & ELSA_TIMER_RUN);
}
/*
 * fast interrupt HSCX stuff goes here
 */

#define READHSCX(cs, nr, reg) readreg(cs->hw.elsa.ale, \
		cs->hw.elsa.hscx, reg + (nr ? 0x40 : 0))
#define WRITEHSCX(cs, nr, reg, data) writereg(cs->hw.elsa.ale, \
		cs->hw.elsa.hscx, reg + (nr ? 0x40 : 0), data)

#define READHSCXFIFO(cs, nr, ptr, cnt) readfifo(cs->hw.elsa.ale, \
		cs->hw.elsa.hscx, (nr ? 0x40 : 0), ptr, cnt)

#define WRITEHSCXFIFO(cs, nr, ptr, cnt) writefifo(cs->hw.elsa.ale, \
		cs->hw.elsa.hscx, (nr ? 0x40 : 0), ptr, cnt)

#include "hscx_irq.c"

static void
elsa_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs = dev_id;
	u_char val;
	int icnt=20;

	if (!cs) {
		printk(KERN_WARNING "Elsa: Spurious interrupt!\n");
		return;
	}
	val = readreg(cs->hw.elsa.ale, cs->hw.elsa.hscx, HSCX_ISTA + 0x40);
      Start_HSCX:
	if (val) {
		hscx_int_main(cs, val);
	}
	val = readreg(cs->hw.elsa.ale, cs->hw.elsa.isac, ISAC_ISTA);
      Start_ISAC:
	if (val) {
		isac_interrupt(cs, val);
	}
	val = readreg(cs->hw.elsa.ale, cs->hw.elsa.hscx, HSCX_ISTA + 0x40);
	if (val && icnt) {
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "HSCX IntStat after IntRoutine");
		icnt--;
		goto Start_HSCX;
	}
	val = readreg(cs->hw.elsa.ale, cs->hw.elsa.isac, ISAC_ISTA);
	if (val && icnt) {
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "ISAC IntStat after IntRoutine");
		icnt--;
		goto Start_ISAC;
	}
	if (!icnt)
		printk(KERN_WARNING"ELSA IRQ LOOP\n");
	writereg(cs->hw.elsa.ale, cs->hw.elsa.hscx, HSCX_MASK, 0xFF);
	writereg(cs->hw.elsa.ale, cs->hw.elsa.hscx, HSCX_MASK + 0x40, 0xFF);
	writereg(cs->hw.elsa.ale, cs->hw.elsa.isac, ISAC_MASK, 0xFF);
	if (cs->hw.elsa.status & ELSA_TIMER_AKTIV) {
		if (!TimerRun(cs)) {
			/* Timer Restart */
			byteout(cs->hw.elsa.timer, 0);
			cs->hw.elsa.counter++;
		}
	}
	if (cs->hw.elsa.trig)
		byteout(cs->hw.elsa.trig, 0x00);
	writereg(cs->hw.elsa.ale, cs->hw.elsa.hscx, HSCX_MASK, 0x0);
	writereg(cs->hw.elsa.ale, cs->hw.elsa.hscx, HSCX_MASK + 0x40, 0x0);
	writereg(cs->hw.elsa.ale, cs->hw.elsa.isac, ISAC_MASK, 0x0);
}

static void
elsa_interrupt_ipac(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs = dev_id;
	u_char ista,val;
	char   tmp[64];
	int icnt=20;

	if (!cs) {
		printk(KERN_WARNING "Elsa: Spurious interrupt!\n");
		return;
	}
	if ((cs->typ == ISDN_CTYPE_ELSA_PCMCIA) && (*cs->busy_flag == 1)) {
	  /* The card tends to generate interrupts while being removed
	     causing us to just crash the kernel. bad. */
	  printk(KERN_WARNING "Elsa: card not available!\n");
	  return;
	}
	ista = readreg(cs->hw.elsa.ale, cs->hw.elsa.isac, IPAC_ISTA);
Start_IPAC:
	if (cs->debug & L1_DEB_IPAC) {
		sprintf(tmp, "IPAC ISTA %02X", ista);
		debugl1(cs, tmp);
	}
	if (ista & 0x0f) {
		val = readreg(cs->hw.elsa.ale, cs->hw.elsa.hscx, HSCX_ISTA + 0x40);
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
		val = 0xfe & readreg(cs->hw.elsa.ale, cs->hw.elsa.isac, ISAC_ISTA + 0x80);
		if (val) {
			isac_interrupt(cs, val);
		}
	}
	if (ista & 0x10) {
		val = 0x01;
		isac_interrupt(cs, val);
	}
	ista  = readreg(cs->hw.elsa.ale, cs->hw.elsa.isac, IPAC_ISTA);
	if ((ista & 0x3f) && icnt) {
		icnt--;
		goto Start_IPAC;
	}
	if (!icnt)
		printk(KERN_WARNING "ELSA IRQ LOOP\n");
	writereg(cs->hw.elsa.ale, cs->hw.elsa.isac, IPAC_MASK, 0xFF);
	writereg(cs->hw.elsa.ale, cs->hw.elsa.isac, IPAC_MASK, 0xC0);
}

void
release_io_elsa(struct IsdnCardState *cs)
{
	int bytecnt = 8;

	del_timer(&cs->hw.elsa.tl);
	if (cs->hw.elsa.ctrl)
		byteout(cs->hw.elsa.ctrl, 0);	/* LEDs Out */
	if ((cs->subtyp == ELSA_PCFPRO) || 
		(cs->subtyp == ELSA_QS3000) ||
		(cs->subtyp == ELSA_PCF))
		bytecnt = 16;
	if (cs->subtyp == ELSA_QS1000PCI) {
		byteout(cs->hw.elsa.cfg + 0x4c, 0x01);  /* disable IRQ */
		bytecnt = 2;
		release_region(cs->hw.elsa.cfg, 0x80);
	}
	if (cs->hw.elsa.base)
		release_region(cs->hw.elsa.base, bytecnt);
}

static void
reset_elsa(struct IsdnCardState *cs)
{
	long flags;

	if (cs->hw.elsa.timer) {
		/* Wait 1 Timer */
		byteout(cs->hw.elsa.timer, 0);
		while (TimerRun(cs));
		cs->hw.elsa.ctrl_reg |= 0x50;
		cs->hw.elsa.ctrl_reg &= ~ELSA_ISDN_RESET;	/* Reset On */
		byteout(cs->hw.elsa.ctrl, cs->hw.elsa.ctrl_reg);
		/* Wait 1 Timer */
		byteout(cs->hw.elsa.timer, 0);
		while (TimerRun(cs));
		cs->hw.elsa.ctrl_reg |= ELSA_ISDN_RESET;	/* Reset Off */
		byteout(cs->hw.elsa.ctrl, cs->hw.elsa.ctrl_reg);
		/* Wait 1 Timer */
		byteout(cs->hw.elsa.timer, 0);
		while (TimerRun(cs));
		if (cs->hw.elsa.trig)
			byteout(cs->hw.elsa.trig, 0xff);
	}
	if (cs->subtyp == ELSA_QS1000PCI) {
		save_flags(flags);
		sti();
		writereg(cs->hw.elsa.ale, cs->hw.elsa.isac, IPAC_POTA2, 0x20);
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout((10*HZ)/1000);	/* Timeout 10ms */
		writereg(cs->hw.elsa.ale, cs->hw.elsa.isac, IPAC_POTA2, 0x00);
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout((10*HZ)/1000);	/* Timeout 10ms */
		writereg(cs->hw.elsa.ale, cs->hw.elsa.isac, IPAC_MASK, 0xc0);
		schedule();
		restore_flags(flags);
		byteout(cs->hw.elsa.cfg + 0x4c, 0x41); /* enable ELSA PCI IRQ */
	}
}

const u_char ARCOFI_VERSION[] = {2,0xa0,0};
const u_char ARCOFI_COP_5[] = {4,0xa1,0x25,0xbb,0x4a}; /* GTX */
const u_char ARCOFI_COP_6[] = {6,0xa1,0x26,0,0,0x82,0x7c}; /* GRL GRH */
const u_char ARCOFI_COP_7[] = {4,0xa1,0x27,0x80,0x80}; /* GZ */
const u_char ARCOFI_COP_8[] = {10,0xa1,0x28,0x49,0x31,0x8,0x13,0x6e,0x88,0x2a,0x61}; /* TX */
const u_char ARCOFI_COP_9[] = {10,0xa1,0x29,0x80,0xcb,0x9e,0x88,0x00,0xc8,0xd8,0x80}; /* RX */
const u_char ARCOFI_XOP_0[] = {2,0xa1,0x30}; /* PWR Down */
const u_char ARCOFI_XOP_1[] = {2,0xa1,0x31}; /* PWR Down */
const u_char ARCOFI_XOP_F[] = {2,0xa1,0x3f}; /* PWR Down */
const u_char ARCOFI_SOP_F[] = {10,0xa1,0x1f,0x00,0x50,0x10,0x00,0x00,0x80,0x02,0x12};
 
static void
init_arcofi(struct IsdnCardState *cs) {
	send_arcofi(cs, ARCOFI_COP_5);
	send_arcofi(cs, ARCOFI_COP_6);
	send_arcofi(cs, ARCOFI_COP_7);
	send_arcofi(cs, ARCOFI_COP_8);
	send_arcofi(cs, ARCOFI_COP_9);
	send_arcofi(cs, ARCOFI_SOP_F);
	send_arcofi(cs, ARCOFI_XOP_F);
}

static void
check_arcofi(struct IsdnCardState *cs)
{
#if ARCOFI_USE
	int arcofi_present = 0;
	char tmp[40];
	char *t;
	u_char *p;

	if (!cs->mon_tx)
		if (!(cs->mon_tx=kmalloc(MAX_MON_FRAME, GFP_ATOMIC))) {
			if (cs->debug & L1_DEB_WARN)
				debugl1(cs, "ISAC MON TX out of buffers!");
			return;
		}
	send_arcofi(cs, ARCOFI_VERSION);
	if (test_and_clear_bit(HW_MON1_TX_END, &cs->HW_Flags)) {
		if (test_and_clear_bit(HW_MON1_RX_END, &cs->HW_Flags)) {
			sprintf(tmp, "Arcofi response received %d bytes", cs->mon_rxp);
			debugl1(cs, tmp);
			p = cs->mon_rx;
			t = tmp;
			t += sprintf(tmp, "Arcofi data");
			QuickHex(t, p, cs->mon_rxp);
			debugl1(cs, tmp);
			if ((cs->mon_rxp == 2) && (cs->mon_rx[0] == 0xa0)) {
				switch(cs->mon_rx[1]) {
					case 0x80:
						debugl1(cs, "Arcofi 2160 detected");
						arcofi_present = 1;
						break;
					case 0x82:
						debugl1(cs, "Arcofi 2165 detected");
						arcofi_present = 2;
						break;
					case 0x84:
						debugl1(cs, "Arcofi 2163 detected");
						arcofi_present = 3;
						break;
					default:
						debugl1(cs, "unknown Arcofi response");
						break;
				}
			} else
				debugl1(cs, "undefined Monitor response");
			cs->mon_rxp = 0;
		}
	} else if (cs->mon_tx) {
		sprintf(tmp, "Arcofi not detected");
		debugl1(cs, tmp);
	}
	if (arcofi_present) {
		if (cs->subtyp==ELSA_QS1000) {
			cs->subtyp = ELSA_QS3000;
			printk(KERN_INFO
				"Elsa: %s detected modem at 0x%x\n",
				Elsa_Types[cs->subtyp],
				cs->hw.elsa.base+8);
			release_region(cs->hw.elsa.base, 8);
			if (check_region(cs->hw.elsa.base, 16)) {
				printk(KERN_WARNING
				"HiSax: %s config port %x-%x already in use\n",
				Elsa_Types[cs->subtyp],
				cs->hw.elsa.base + 8,
				cs->hw.elsa.base + 16);
			} else
				request_region(cs->hw.elsa.base, 16,
					"elsa isdn modem");
		} else if (cs->subtyp==ELSA_PCC16) {
			cs->subtyp = ELSA_PCF;
			printk(KERN_INFO
				"Elsa: %s detected modem at 0x%x\n",
				Elsa_Types[cs->subtyp],
				cs->hw.elsa.base+8);
			release_region(cs->hw.elsa.base, 8);
			if (check_region(cs->hw.elsa.base, 16)) {
				printk(KERN_WARNING
				"HiSax: %s config port %x-%x already in use\n",
				Elsa_Types[cs->subtyp],
				cs->hw.elsa.base + 8,
				cs->hw.elsa.base + 16);
			} else
				request_region(cs->hw.elsa.base, 16,
					"elsa isdn modem");
		} else
			printk(KERN_INFO
				"Elsa: %s detected modem at 0x%x\n",
				Elsa_Types[cs->subtyp],
				cs->hw.elsa.base+8);
		init_arcofi(cs);
	}
#endif
}

static void
elsa_led_handler(struct IsdnCardState *cs)
{
	int blink = 0;

	if ((cs->subtyp == ELSA_PCMCIA) &&
		(cs->subtyp == ELSA_QS1000PCI))
		return;
	del_timer(&cs->hw.elsa.tl);
	if (cs->hw.elsa.status & ELSA_ASSIGN)
		cs->hw.elsa.ctrl_reg |= ELSA_STAT_LED;
	else if (cs->hw.elsa.status & ELSA_BAD_PWR)
		cs->hw.elsa.ctrl_reg &= ~ELSA_STAT_LED;
	else {
		cs->hw.elsa.ctrl_reg ^= ELSA_STAT_LED;
		blink = 250;
	}
	if (cs->hw.elsa.status & 0xf000)
		cs->hw.elsa.ctrl_reg |= ELSA_LINE_LED;
	else if (cs->hw.elsa.status & 0x0f00) {
		cs->hw.elsa.ctrl_reg ^= ELSA_LINE_LED;
		blink = 500;
	} else
		cs->hw.elsa.ctrl_reg &= ~ELSA_LINE_LED;

	byteout(cs->hw.elsa.ctrl, cs->hw.elsa.ctrl_reg);
	if (blink) {
		init_timer(&cs->hw.elsa.tl);
		cs->hw.elsa.tl.expires = jiffies + ((blink * HZ) / 1000);
		add_timer(&cs->hw.elsa.tl);
	}
}

static int
Elsa_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
	int pwr, ret = 0;
	long flags;	

	switch (mt) {
		case CARD_RESET:
			reset_elsa(cs);
			return(0);
		case CARD_RELEASE:
			release_io_elsa(cs);
			return(0);
		case CARD_SETIRQ:
			if (cs->subtyp == ELSA_QS1000PCI)
				ret = request_irq(cs->irq, &elsa_interrupt_ipac,
					I4L_IRQ_FLAG, "HiSax", cs);
			else
				ret = request_irq(cs->irq, &elsa_interrupt,
					I4L_IRQ_FLAG, "HiSax", cs);
			return(ret);
		case CARD_INIT:
			if (cs->hw.elsa.trig)
				byteout(cs->hw.elsa.trig, 0xff);
			clear_pending_isac_ints(cs);
			clear_pending_hscx_ints(cs);
			initisac(cs);
			inithscx(cs);
			if (cs->subtyp == ELSA_QS1000) {
				byteout(cs->hw.elsa.timer, 0);
				byteout(cs->hw.elsa.trig, 0xff);
			}
			return(0);
		case CARD_TEST:
			if ((cs->subtyp != ELSA_PCMCIA) &&
				(cs->subtyp != ELSA_QS1000PCI)) {
				save_flags(flags);
				cs->hw.elsa.counter = 0;
				sti();
				cs->hw.elsa.ctrl_reg |= ELSA_ENA_TIMER_INT;
				cs->hw.elsa.status |= ELSA_TIMER_AKTIV;
				byteout(cs->hw.elsa.ctrl, cs->hw.elsa.ctrl_reg);
				byteout(cs->hw.elsa.timer, 0);
			} else
				return(0);
			current->state = TASK_INTERRUPTIBLE;
			schedule_timeout((110*HZ)/1000);		/* Timeout 110ms */
			restore_flags(flags);
			cs->hw.elsa.ctrl_reg &= ~ELSA_ENA_TIMER_INT;
			byteout(cs->hw.elsa.ctrl, cs->hw.elsa.ctrl_reg);
			cs->hw.elsa.status &= ~ELSA_TIMER_AKTIV;
			printk(KERN_INFO "Elsa: %d timer tics in 110 msek\n",
			       cs->hw.elsa.counter);
			if (abs(cs->hw.elsa.counter - 13) < 3) {
				printk(KERN_INFO "Elsa: timer and irq OK\n");
				ret = 0;
			} else {
				printk(KERN_WARNING
				       "Elsa: timer tic problem (%d/12) maybe an IRQ(%d) conflict\n",
				       cs->hw.elsa.counter, cs->irq);
				ret = 1;
			}
			check_arcofi(cs);
			elsa_led_handler(cs);
			return(ret);
		case MDL_REMOVE_REQ:
			cs->hw.elsa.status &= 0;
			break;
		case MDL_ASSIGN_REQ:
			cs->hw.elsa.status |= ELSA_ASSIGN;
			break;
		case MDL_INFO_SETUP:
			if ((int) arg)
				cs->hw.elsa.status |= 0x0200;
			else
				cs->hw.elsa.status |= 0x0100;
			break;
		case MDL_INFO_CONN:
			if ((int) arg)
				cs->hw.elsa.status |= 0x2000;
			else
				cs->hw.elsa.status |= 0x1000;
			break;
		case MDL_INFO_REL:
			if ((int) arg) {
				cs->hw.elsa.status &= ~0x2000;
				cs->hw.elsa.status &= ~0x0200;
			} else {
				cs->hw.elsa.status &= ~0x1000;
				cs->hw.elsa.status &= ~0x0100;
			}
			break;
		case CARD_AUX_IND:
			break;
	}
	pwr = bytein(cs->hw.elsa.ale);
	if (pwr & 0x08)
		cs->hw.elsa.status |= ELSA_BAD_PWR;
	else
		cs->hw.elsa.status &= ~ELSA_BAD_PWR;
	elsa_led_handler(cs);
	return(ret);
}

static unsigned char
probe_elsa_adr(unsigned int adr, int typ)
{
	int i, in1, in2, p16_1 = 0, p16_2 = 0, p8_1 = 0, p8_2 = 0, pc_1 = 0,
	 pc_2 = 0, pfp_1 = 0, pfp_2 = 0;
	long flags;

	/* In case of the elsa pcmcia card, this region is in use,
	   reserved for us by the card manager. So we do not check it
	   here, it would fail. */
	if (typ != ISDN_CTYPE_ELSA_PCMCIA && check_region(adr, 8)) {
		printk(KERN_WARNING
		       "Elsa: Probing Port 0x%x: already in use\n",
		       adr);
		return (0);
	}
	save_flags(flags);
	cli();
	for (i = 0; i < 16; i++) {
		in1 = inb(adr + ELSA_CONFIG);	/* 'toggelt' bei */
		in2 = inb(adr + ELSA_CONFIG);	/* jedem Zugriff */
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
probe_elsa(struct IsdnCardState *cs)
{
	int i;
	unsigned int CARD_portlist[] =
	{0x160, 0x170, 0x260, 0x360, 0};

	for (i = 0; CARD_portlist[i]; i++) {
		if ((cs->subtyp = probe_elsa_adr(CARD_portlist[i], cs->typ)))
			break;
	}
	return (CARD_portlist[i]);
}

static 	int pci_index __initdata = 0;

int
setup_elsa(struct IsdnCard *card)
{
	long flags;
	int bytecnt;
	u_char val;
	struct IsdnCardState *cs = card->cs;
	char tmp[64];

	strcpy(tmp, Elsa_revision);
	printk(KERN_INFO "HiSax: Elsa driver Rev. %s\n", HiSax_getrev(tmp));
	cs->hw.elsa.ctrl_reg = 0;
	cs->hw.elsa.status = 0;
	if (cs->typ == ISDN_CTYPE_ELSA) {
		cs->hw.elsa.base = card->para[0];
		printk(KERN_INFO "Elsa: Microlink IO probing\n");
		if (cs->hw.elsa.base) {
			if (!(cs->subtyp = probe_elsa_adr(cs->hw.elsa.base,
							  cs->typ))) {
				printk(KERN_WARNING
				     "Elsa: no Elsa Microlink at 0x%x\n",
				       cs->hw.elsa.base);
				return (0);
			}
		} else
			cs->hw.elsa.base = probe_elsa(cs);
		if (cs->hw.elsa.base) {
			cs->hw.elsa.cfg = cs->hw.elsa.base + ELSA_CONFIG;
			cs->hw.elsa.ctrl = cs->hw.elsa.base + ELSA_CONTROL;
			cs->hw.elsa.ale = cs->hw.elsa.base + ELSA_ALE;
			cs->hw.elsa.isac = cs->hw.elsa.base + ELSA_ISAC;
			cs->hw.elsa.itac = cs->hw.elsa.base + ELSA_ITAC;
			cs->hw.elsa.hscx = cs->hw.elsa.base + ELSA_HSCX;
			cs->hw.elsa.trig = cs->hw.elsa.base + ELSA_TRIG_IRQ;
			cs->hw.elsa.timer = cs->hw.elsa.base + ELSA_START_TIMER;
			val = bytein(cs->hw.elsa.cfg);
			if (cs->subtyp == ELSA_PC) {
				const u_char CARD_IrqTab[8] =
				{7, 3, 5, 9, 0, 0, 0, 0};
				cs->irq = CARD_IrqTab[(val & ELSA_IRQ_IDX_PC) >> 2];
			} else if (cs->subtyp == ELSA_PCC8) {
				const u_char CARD_IrqTab[8] =
				{7, 3, 5, 9, 0, 0, 0, 0};
				cs->irq = CARD_IrqTab[(val & ELSA_IRQ_IDX_PCC8) >> 4];
			} else {
				const u_char CARD_IrqTab[8] =
				{15, 10, 15, 3, 11, 5, 11, 9};
				cs->irq = CARD_IrqTab[(val & ELSA_IRQ_IDX) >> 3];
			}
			val = bytein(cs->hw.elsa.ale) & ELSA_HW_RELEASE;
			if (val < 3)
				val |= 8;
			val += 'A' - 3;
			if (val == 'B' || val == 'C')
				val ^= 1;
			if ((cs->subtyp == ELSA_PCFPRO) && (val = 'G'))
				val = 'C';
			printk(KERN_INFO
			       "Elsa: %s found at 0x%x Rev.:%c IRQ %d\n",
			       Elsa_Types[cs->subtyp],
			       cs->hw.elsa.base,
			       val, cs->irq);
			val = bytein(cs->hw.elsa.ale) & ELSA_S0_POWER_BAD;
			if (val) {
				printk(KERN_WARNING
				   "Elsa: Microlink S0 bus power bad\n");
				cs->hw.elsa.status |= ELSA_BAD_PWR;
			}
		} else {
			printk(KERN_WARNING
			       "No Elsa Microlink found\n");
			return (0);
		}
	} else if (cs->typ == ISDN_CTYPE_ELSA_PNP) {
		cs->hw.elsa.base = card->para[1];
		cs->irq = card->para[0];
		cs->subtyp = ELSA_QS1000;
		cs->hw.elsa.cfg = cs->hw.elsa.base + ELSA_CONFIG;
		cs->hw.elsa.ale = cs->hw.elsa.base + ELSA_ALE;
		cs->hw.elsa.isac = cs->hw.elsa.base + ELSA_ISAC;
		cs->hw.elsa.hscx = cs->hw.elsa.base + ELSA_HSCX;
		cs->hw.elsa.trig = cs->hw.elsa.base + ELSA_TRIG_IRQ;
		cs->hw.elsa.timer = cs->hw.elsa.base + ELSA_START_TIMER;
		cs->hw.elsa.ctrl = cs->hw.elsa.base + ELSA_CONTROL;
		printk(KERN_INFO
		       "Elsa: %s defined at 0x%x IRQ %d\n",
		       Elsa_Types[cs->subtyp],
		       cs->hw.elsa.base,
		       cs->irq);
	} else if (cs->typ == ISDN_CTYPE_ELSA_PCMCIA) {
		cs->hw.elsa.base = card->para[1];
		cs->irq = card->para[0];
		cs->subtyp = ELSA_PCMCIA;
		cs->hw.elsa.ale = cs->hw.elsa.base + ELSA_ALE_PCM;
		cs->hw.elsa.isac = cs->hw.elsa.base + ELSA_ISAC_PCM;
		cs->hw.elsa.hscx = cs->hw.elsa.base + ELSA_HSCX;
		cs->hw.elsa.timer = 0;
		cs->hw.elsa.trig = 0;
		cs->hw.elsa.ctrl = 0;
		printk(KERN_INFO
		       "Elsa: %s defined at 0x%x IRQ %d\n",
		       Elsa_Types[cs->subtyp],
		       cs->hw.elsa.base,
		       cs->irq);
	} else if (cs->typ == ISDN_CTYPE_ELSA_PCI) {
#if CONFIG_PCI
		u_char pci_bus, pci_device_fn, pci_irq;
		u_int pci_ioaddr;

		cs->subtyp = 0;
		for (; pci_index < 0xff; pci_index++) {
			if (pcibios_find_device(PCI_VENDOR_ELSA,
			   PCI_QS1000_ID, pci_index, &pci_bus, &pci_device_fn)
			   == PCIBIOS_SUCCESSFUL)
				cs->subtyp = ELSA_QS1000PCI;
			else
				break;
			/* get IRQ */
			pcibios_read_config_byte(pci_bus, pci_device_fn,
				PCI_INTERRUPT_LINE, &pci_irq);

			/* get IO address */
			pcibios_read_config_dword(pci_bus, pci_device_fn,
				PCI_BASE_ADDRESS_1, &pci_ioaddr);
			pci_ioaddr &= ~3; /* remove io/mem flag */
			cs->hw.elsa.cfg = pci_ioaddr;
			pcibios_read_config_dword(pci_bus, pci_device_fn,
				PCI_BASE_ADDRESS_3, &pci_ioaddr);
			if (cs->subtyp)
				break;
		}
		if (!cs->subtyp) {
			printk(KERN_WARNING "Elsa: No PCI card found\n");
			return(0);
		}
		if (!pci_irq) {
			printk(KERN_WARNING "Elsa: No IRQ for PCI card found\n");
			return(0);
		}

		if (!pci_ioaddr) {
			printk(KERN_WARNING "Elsa: No IO-Adr for PCI card found\n");
			return(0);
		}
		pci_ioaddr &= ~3; /* remove io/mem flag */
		cs->hw.elsa.base = pci_ioaddr;
		cs->hw.elsa.ale  = pci_ioaddr;
		cs->hw.elsa.isac = pci_ioaddr +1;
		cs->hw.elsa.hscx = pci_ioaddr +1; 
		cs->irq = pci_irq;
		test_and_set_bit(HW_IPAC, &cs->HW_Flags);
		cs->hw.elsa.timer = 0;
		cs->hw.elsa.trig  = 0;
		printk(KERN_INFO
		       "Elsa: %s defined at 0x%x/0x%x IRQ %d\n",
		       Elsa_Types[cs->subtyp],
		       cs->hw.elsa.base,
		       cs->hw.elsa.cfg,
		       cs->irq);
#else
		printk(KERN_WARNING "Elsa: Elsa PCI and NO_PCI_BIOS\n");
		printk(KERN_WARNING "Elsa: unable to config Elsa PCI\n");
		return (0);
#endif /* CONFIG_PCI */
	} else 
		return (0);

	switch (cs->subtyp) {
		case ELSA_PC:
		case ELSA_PCC8:
		case ELSA_PCC16:
		case ELSA_QS1000:
		case ELSA_PCMCIA:
			bytecnt = 8;
			break;
		case ELSA_PCFPRO:
		case ELSA_PCF:
			bytecnt = 16;
			break;
		case ELSA_QS1000PCI:
			bytecnt = 2;
			break;
		default:
			printk(KERN_WARNING
			       "Unknown ELSA subtype %d\n", cs->subtyp);
			return (0);
	}
	/* In case of the elsa pcmcia card, this region is in use,
	   reserved for us by the card manager. So we do not check it
	   here, it would fail. */
	if (cs->typ != ISDN_CTYPE_ELSA_PCMCIA && check_region(cs->hw.elsa.base, bytecnt)) {
		printk(KERN_WARNING
		       "HiSax: %s config port %x-%x already in use\n",
		       CardType[card->typ],
		       cs->hw.elsa.base,
		       cs->hw.elsa.base + bytecnt);
		return (0);
	} else {
		request_region(cs->hw.elsa.base, bytecnt, "elsa isdn");
	}
	if (cs->subtyp == ELSA_QS1000PCI) {
		if (check_region(cs->hw.elsa.cfg, 0x80)) {
			printk(KERN_WARNING
			       "HiSax: %s pci port %x-%x already in use\n",
				CardType[card->typ],
				cs->hw.elsa.cfg,
				cs->hw.elsa.cfg + 0x80);
			release_region(cs->hw.elsa.base, bytecnt);
			return (0);
		} else {
			request_region(cs->hw.elsa.cfg, 0x80, "elsa isdn pci");
		}
	}
	cs->hw.elsa.tl.function = (void *) elsa_led_handler;
	cs->hw.elsa.tl.data = (long) cs;
	init_timer(&cs->hw.elsa.tl);
	/* Teste Timer */
	if (cs->hw.elsa.timer) {
		byteout(cs->hw.elsa.trig, 0xff);
		byteout(cs->hw.elsa.timer, 0);
		if (!TimerRun(cs)) {
			byteout(cs->hw.elsa.timer, 0);	/* 2. Versuch */
			if (!TimerRun(cs)) {
				printk(KERN_WARNING
				       "Elsa: timer do not start\n");
				release_io_elsa(cs);
				return (0);
			}
		}
		save_flags(flags);
		sti();
		HZDELAY(1);	/* wait >=10 ms */
		restore_flags(flags);
		if (TimerRun(cs)) {
			printk(KERN_WARNING "Elsa: timer do not run down\n");
			release_io_elsa(cs);
			return (0);
		}
		printk(KERN_INFO "Elsa: timer OK; resetting card\n");
	}
	reset_elsa(cs);
	cs->BC_Read_Reg = &ReadHSCX;
	cs->BC_Write_Reg = &WriteHSCX;
	cs->BC_Send_Data = &hscx_fill_fifo;
	cs->cardmsg = &Elsa_card_msg;
	if (cs->subtyp == ELSA_QS1000PCI) {
		cs->readisac = &ReadISAC_IPAC;
		cs->writeisac = &WriteISAC_IPAC;
		cs->readisacfifo = &ReadISACfifo_IPAC;
		cs->writeisacfifo = &WriteISACfifo_IPAC;
		val = readreg(cs->hw.elsa.ale, cs->hw.elsa.isac, IPAC_ID);
		printk(KERN_INFO "Elsa: IPAC version %x\n", val);
	} else {
		cs->readisac = &ReadISAC;
		cs->writeisac = &WriteISAC;
		cs->readisacfifo = &ReadISACfifo;
		cs->writeisacfifo = &WriteISACfifo;
		ISACVersion(cs, "Elsa:");
		if (HscxVersion(cs, "Elsa:")) {
			printk(KERN_WARNING
				"Elsa: wrong HSCX versions check IO address\n");
			release_io_elsa(cs);
			return (0);
		}
	}
	if (cs->subtyp == ELSA_PC) {
		val = readitac(cs, ITAC_SYS);
		printk(KERN_INFO "Elsa: ITAC version %s\n", ITACVer[val & 7]);
		writeitac(cs, ITAC_ISEN, 0);
		writeitac(cs, ITAC_RFIE, 0);
		writeitac(cs, ITAC_XFIE, 0);
		writeitac(cs, ITAC_SCIE, 0);
		writeitac(cs, ITAC_STIE, 0);
	}
	return (1);
}

