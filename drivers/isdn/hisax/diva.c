/* $Id: diva.c,v 1.10 1998/11/15 23:54:31 keil Exp $

 * diva.c     low level stuff for Eicon.Diehl Diva Family ISDN cards
 *
 * Author     Karsten Keil (keil@isdn4linux.de)
 *
 * Thanks to Eicon Technology Diehl GmbH & Co. oHG for documents and informations
 *
 *
 * $Log: diva.c,v $
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

const char *Diva_revision = "$Revision: 1.10 $";

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

/* PCI stuff */
#define PCI_VENDOR_EICON_DIEHL	0x1133
#define PCI_DIVA20PRO_ID	0xe001
#define PCI_DIVA20_ID		0xe002
#define PCI_DIVA20PRO_U_ID	0xe003
#define PCI_DIVA20_U_ID		0xe004

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
	u_char val, sval, stat = 0;
	int cnt=8;

	if (!cs) {
		printk(KERN_WARNING "Diva: Spurious interrupt!\n");
		return;
	}
	while (((sval = bytein(cs->hw.diva.ctrl)) & DIVA_IRQ_REQ) && cnt) {
		val = readreg(cs->hw.diva.hscx_adr, cs->hw.diva.hscx, HSCX_ISTA + 0x40);
		if (val) {
			hscx_int_main(cs, val);
			stat |= 1;
		}
		val = readreg(cs->hw.diva.isac_adr, cs->hw.diva.isac, ISAC_ISTA);
		if (val) {
			isac_interrupt(cs, val);
			stat |= 2;
		}
		cnt--;
	}
	if (!cnt)
		printk(KERN_WARNING "Diva: IRQ LOOP\n");
	if (stat & 1) {
		writereg(cs->hw.diva.hscx_adr, cs->hw.diva.hscx, HSCX_MASK, 0xFF);
		writereg(cs->hw.diva.hscx_adr, cs->hw.diva.hscx, HSCX_MASK + 0x40, 0xFF);
		writereg(cs->hw.diva.hscx_adr, cs->hw.diva.hscx, HSCX_MASK, 0x0);
		writereg(cs->hw.diva.hscx_adr, cs->hw.diva.hscx, HSCX_MASK + 0x40, 0x0);
	}
	if (stat & 2) {
		writereg(cs->hw.diva.isac_adr, cs->hw.diva.isac, ISAC_MASK, 0xFF);
		writereg(cs->hw.diva.isac_adr, cs->hw.diva.isac, ISAC_MASK, 0x0);
	}
}

static void
diva_interrupt_ipac(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs = dev_id;
	u_char ista,val;
	int icnt=20;

	if (!cs) {
		printk(KERN_WARNING "Diva: Spurious interrupt!\n");
		return;
	}
	ista = readreg(cs->hw.diva.isac_adr, cs->hw.diva.isac, IPAC_ISTA);
Start_IPAC:
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
		goto Start_IPAC;
	}
	if (!icnt)
		printk(KERN_WARNING "DIVA IPAC IRQ LOOP\n");
	writereg(cs->hw.diva.isac_adr, cs->hw.diva.isac, IPAC_MASK, 0xFF);
	writereg(cs->hw.diva.isac_adr, cs->hw.diva.isac, IPAC_MASK, 0xC0);
}


void
release_io_diva(struct IsdnCardState *cs)
{
	int bytecnt;

	if (cs->subtyp != DIVA_IPAC_ISA) {
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
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout((10*HZ)/1000); /* Timeout 10ms */
		writereg(cs->hw.diva.isac_adr, cs->hw.diva.isac, IPAC_POTA2, 0x00);
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout((10*HZ)/1000); /* Timeout 10ms */
		writereg(cs->hw.diva.isac_adr, cs->hw.diva.isac, IPAC_MASK, 0xc0);
	} else {
		cs->hw.diva.ctrl_reg = 0;        /* Reset On */
		byteout(cs->hw.diva.ctrl, cs->hw.diva.ctrl_reg);
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout((10*HZ)/1000); /* Timeout 10ms */
		cs->hw.diva.ctrl_reg |= DIVA_RESET;  /* Reset Off */
		byteout(cs->hw.diva.ctrl, cs->hw.diva.ctrl_reg);
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout((10*HZ)/1000); /* Timeout 10ms */
		if (cs->subtyp == DIVA_ISA)
			cs->hw.diva.ctrl_reg |= DIVA_ISA_LED_A;
		else
			cs->hw.diva.ctrl_reg |= DIVA_PCI_LED_A;
		byteout(cs->hw.diva.ctrl, cs->hw.diva.ctrl_reg);
	}
	restore_flags(flags);
}

#define DIVA_ASSIGN 1

static void
diva_led_handler(struct IsdnCardState *cs)
{
	int blink = 0;

	if (cs->subtyp == DIVA_IPAC_ISA)
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
	u_int irq_flag = I4L_IRQ_FLAG;

	switch (mt) {
		case CARD_RESET:
			reset_diva(cs);
			return(0);
		case CARD_RELEASE:
			release_io_diva(cs);
			return(0);
		case CARD_SETIRQ:
			if (cs->subtyp == DIVA_PCI)
				irq_flag |= SA_SHIRQ;
			if (cs->subtyp == DIVA_IPAC_ISA) {
				return(request_irq(cs->irq, &diva_interrupt_ipac,
						irq_flag, "HiSax", cs));
			} else {
				return(request_irq(cs->irq, &diva_interrupt,
						irq_flag, "HiSax", cs));
			}
		case CARD_INIT:
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
	if (cs->subtyp != DIVA_IPAC_ISA)
		diva_led_handler(cs);
	return(0);
}

static 	struct pci_dev *dev_diva __initdata = NULL;
static 	struct pci_dev *dev_diva_u __initdata = NULL;

int __init 
setup_diva(struct IsdnCard *card)
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
		if (val == 1) {
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
			/* get IRQ */
			cs->irq = dev_diva->irq;
			/* get IO address */
			cs->hw.diva.cfg_reg = dev_diva->resource[2].start
				& PCI_BASE_ADDRESS_IO_MASK;
		} else if ((dev_diva_u = pci_find_device(PCI_VENDOR_EICON_DIEHL,
			PCI_DIVA20_U_ID, dev_diva_u))) {
			   	cs->subtyp = DIVA_PCI;
			/* get IRQ */
			cs->irq = dev_diva_u->irq;
			/* get IO address */
			cs->hw.diva.cfg_reg = dev_diva_u->resource[2].start
				& PCI_BASE_ADDRESS_IO_MASK;
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
		cs->hw.diva.ctrl = cs->hw.diva.cfg_reg + DIVA_PCI_CTRL;
		cs->hw.diva.isac = cs->hw.diva.cfg_reg + DIVA_PCI_ISAC_DATA;
		cs->hw.diva.hscx = cs->hw.diva.cfg_reg + DIVA_HSCX_DATA;
		cs->hw.diva.isac_adr = cs->hw.diva.cfg_reg + DIVA_PCI_ISAC_ADR;
		cs->hw.diva.hscx_adr = cs->hw.diva.cfg_reg + DIVA_HSCX_ADR;
		bytecnt = 32;
#else
		printk(KERN_WARNING "Diva: cfgreg 0 and NO_PCI_BIOS\n");
		printk(KERN_WARNING "Diva: unable to config DIVA PCI\n");
		return (0);
#endif /* CONFIG_PCI */
	}

	printk(KERN_INFO
		"Diva: %s card configured at 0x%x IRQ %d\n",
		(cs->subtyp == DIVA_PCI) ? "PCI" :
		(cs->subtyp == DIVA_ISA) ? "ISA" : "IPAC",
		cs->hw.diva.cfg_reg, cs->irq);
	if (check_region(cs->hw.diva.cfg_reg, bytecnt)) {
		printk(KERN_WARNING
		       "HiSax: %s config port %x-%x already in use\n",
		       CardType[card->typ],
		       cs->hw.diva.cfg_reg,
		       cs->hw.diva.cfg_reg + bytecnt);
		return (0);
	} else {
		request_region(cs->hw.diva.cfg_reg, bytecnt, "diva isdn");
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
		val = readreg(cs->hw.diva.isac_adr, cs->hw.diva.isac, IPAC_ID);
		printk(KERN_INFO "Diva: IPAC version %x\n", val);
	} else {
		cs->hw.diva.tl.function = (void *) diva_led_handler;
		cs->hw.diva.tl.data = (long) cs;
		init_timer(&cs->hw.diva.tl);
		cs->readisac  = &ReadISAC;
		cs->writeisac = &WriteISAC;
		cs->readisacfifo  = &ReadISACfifo;
		cs->writeisacfifo = &WriteISACfifo;
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
