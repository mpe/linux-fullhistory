/* $Id: niccy.c,v 1.2 1998/02/11 17:31:04 keil Exp $

 * niccy.c  low level stuff for Dr. Neuhaus NICCY PnP and NICCY PCI and
 *          compatible (SAGEM cybermodem)
 *
 * Author   Karsten Keil
 * 
 * Thanks to Dr. Neuhaus and SAGEM for informations
 *
 * $Log: niccy.c,v $
 * Revision 1.2  1998/02/11 17:31:04  keil
 * new file
 *
 *
 *
 */

#include <linux/config.h>
#define __NO_VERSION__
#include "hisax.h"
#include "isac.h"
#include "hscx.h"
#include "isdnl1.h"
#include <linux/pci.h>
#include <linux/bios32.h>

extern const char *CardType[];
const char *niccy_revision = "$Revision: 1.2 $";

#define byteout(addr,val) outb(val,addr)
#define bytein(addr) inb(addr)

#define ISAC_PCI_DATA	0
#define HSCX_PCI_DATA	1
#define ISAC_PCI_ADDR	2
#define HSCX_PCI_ADDR	3
#define ISAC_PNP	0
#define HSCX_PNP	1

/* SUB Types */
#define NICCY_PNP	1
#define NICCY_PCI	2

/* PCI stuff */
#define PCI_VENDOR_DR_NEUHAUS	0x1267
#define PCI_NICCY_ID		0x1016

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
	return (readreg(cs->hw.niccy.isac_ale, cs->hw.niccy.isac, offset));
}

static void
WriteISAC(struct IsdnCardState *cs, u_char offset, u_char value)
{
	writereg(cs->hw.niccy.isac_ale, cs->hw.niccy.isac, offset, value);
}

static void
ReadISACfifo(struct IsdnCardState *cs, u_char * data, int size)
{
	readfifo(cs->hw.niccy.isac_ale, cs->hw.niccy.isac, 0, data, size);
}

static void
WriteISACfifo(struct IsdnCardState *cs, u_char * data, int size)
{
	writefifo(cs->hw.niccy.isac_ale, cs->hw.niccy.isac, 0, data, size);
}

static u_char
ReadHSCX(struct IsdnCardState *cs, int hscx, u_char offset)
{
	return (readreg(cs->hw.niccy.hscx_ale,
			cs->hw.niccy.hscx, offset + (hscx ? 0x40 : 0)));
}

static void
WriteHSCX(struct IsdnCardState *cs, int hscx, u_char offset, u_char value)
{
	writereg(cs->hw.niccy.hscx_ale,
		 cs->hw.niccy.hscx, offset + (hscx ? 0x40 : 0), value);
}

#define READHSCX(cs, nr, reg) readreg(cs->hw.niccy.hscx_ale, \
		cs->hw.niccy.hscx, reg + (nr ? 0x40 : 0))
#define WRITEHSCX(cs, nr, reg, data) writereg(cs->hw.niccy.hscx_ale, \
		cs->hw.niccy.hscx, reg + (nr ? 0x40 : 0), data)

#define READHSCXFIFO(cs, nr, ptr, cnt) readfifo(cs->hw.niccy.hscx_ale, \
		cs->hw.niccy.hscx, (nr ? 0x40 : 0), ptr, cnt)

#define WRITEHSCXFIFO(cs, nr, ptr, cnt) writefifo(cs->hw.niccy.hscx_ale, \
		cs->hw.niccy.hscx, (nr ? 0x40 : 0), ptr, cnt)

#include "hscx_irq.c"

static void
niccy_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs = dev_id;
	u_char val, stat = 0;

	if (!cs) {
		printk(KERN_WARNING "Niccy: Spurious interrupt!\n");
		return;
	}
	val = readreg(cs->hw.niccy.hscx_ale, cs->hw.niccy.hscx, HSCX_ISTA + 0x40);
      Start_HSCX:
	if (val) {
		hscx_int_main(cs, val);
		stat |= 1;
	}
	val = readreg(cs->hw.niccy.isac_ale, cs->hw.niccy.isac, ISAC_ISTA);
      Start_ISAC:
	if (val) {
		isac_interrupt(cs, val);
		stat |= 2;
	}
	val = readreg(cs->hw.niccy.hscx_ale, cs->hw.niccy.hscx, HSCX_ISTA + 0x40);
	if (val) {
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "HSCX IntStat after IntRoutine");
		goto Start_HSCX;
	}
	val = readreg(cs->hw.niccy.isac_ale, cs->hw.niccy.isac, ISAC_ISTA);
	if (val) {
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "ISAC IntStat after IntRoutine");
		goto Start_ISAC;
	}
	if (stat & 1) {
		writereg(cs->hw.niccy.hscx_ale, cs->hw.niccy.hscx, HSCX_MASK, 0xFF);
		writereg(cs->hw.niccy.hscx_ale, cs->hw.niccy.hscx, HSCX_MASK + 0x40, 0xFF);
		writereg(cs->hw.niccy.hscx_ale, cs->hw.niccy.hscx, HSCX_MASK, 0);
		writereg(cs->hw.niccy.hscx_ale, cs->hw.niccy.hscx, HSCX_MASK + 0x40, 0);
	}
	if (stat & 2) {
		writereg(cs->hw.niccy.isac_ale, cs->hw.niccy.isac, ISAC_MASK, 0xFF);
		writereg(cs->hw.niccy.isac_ale, cs->hw.niccy.isac, ISAC_MASK, 0);
	}
}

void
release_io_niccy(struct IsdnCardState *cs)
{
	if (cs->subtyp == NICCY_PCI)
		release_region(cs->hw.niccy.isac, 4);
	else {
		release_region(cs->hw.niccy.isac, 2);
		release_region(cs->hw.niccy.isac_ale, 2);
	}
}

static void
niccy_reset(struct IsdnCardState *cs)
{
	// No reset procedure known
}

static int
niccy_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
	switch (mt) {
		case CARD_RESET:
			niccy_reset(cs);
			return(0);
		case CARD_RELEASE:
			release_io_niccy(cs);
			return(0);
		case CARD_SETIRQ:
			return(request_irq(cs->irq, &niccy_interrupt,
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

static 	int pci_index __initdata = 0;

__initfunc(int
setup_niccy(struct IsdnCard *card))
{
	struct IsdnCardState *cs = card->cs;
	char tmp[64];

	strcpy(tmp, niccy_revision);
	printk(KERN_INFO "HiSax: Niccy driver Rev. %s\n", HiSax_getrev(tmp));
	if (cs->typ != ISDN_CTYPE_NICCY)
		return (0);

	if (card->para[1]) {
		cs->hw.niccy.isac = card->para[1] + ISAC_PNP;
		cs->hw.niccy.hscx = card->para[1] + HSCX_PNP;
		cs->hw.niccy.isac_ale = card->para[2] + ISAC_PNP;
		cs->hw.niccy.hscx_ale = card->para[2] + HSCX_PNP;
		cs->hw.niccy.cfg_reg = 0;
		cs->subtyp = NICCY_PNP;
		cs->irq = card->para[0];
		if (check_region((cs->hw.niccy.isac), 2)) {
			printk(KERN_WARNING
				"HiSax: %s data port %x-%x already in use\n",
				CardType[card->typ],
				cs->hw.niccy.isac,
				cs->hw.niccy.isac + 1);
			return (0);
		} else
			request_region(cs->hw.niccy.isac, 2, "niccy data");
		if (check_region((cs->hw.niccy.isac_ale), 2)) {
			printk(KERN_WARNING
				"HiSax: %s address port %x-%x already in use\n",
				CardType[card->typ],
				cs->hw.niccy.isac_ale,
				cs->hw.niccy.isac_ale + 1);
			release_region(cs->hw.niccy.isac, 2);
			return (0);
		} else
			request_region(cs->hw.niccy.isac_ale, 2, "niccy addr");
	} else {
#if CONFIG_PCI
		u_char pci_bus, pci_device_fn, pci_irq;
		u_int pci_ioaddr;

		cs->subtyp = 0;
		for (; pci_index < 0xff; pci_index++) {
			if (pcibios_find_device(PCI_VENDOR_DR_NEUHAUS,
			   PCI_NICCY_ID, pci_index, &pci_bus, &pci_device_fn)
			   == PCIBIOS_SUCCESSFUL)
				cs->subtyp = NICCY_PCI;
			else
				break;
			/* get IRQ */
			pcibios_read_config_byte(pci_bus, pci_device_fn,
				PCI_INTERRUPT_LINE, &pci_irq);

			/* get IO address */
			/* if it won't work try the other PCI addresses
			 * PCI_BASE_ADDRESS_0 ... PCI_BASE_ADDRESS_5
			 */
			pcibios_read_config_dword(pci_bus, pci_device_fn,
				PCI_BASE_ADDRESS_2, &pci_ioaddr);
			if (cs->subtyp)
				break;
		}
		if (!cs->subtyp) {
			printk(KERN_WARNING "Niccy: No PCI card found\n");
			return(0);
		}
		if (!pci_irq) {
			printk(KERN_WARNING "Niccy: No IRQ for PCI card found\n");
			return(0);
		}

		if (!pci_ioaddr) {
			printk(KERN_WARNING "Niccy: No IO-Adr for PCI card found\n");
			return(0);
		}
		pci_ioaddr &= ~3; /* remove io/mem flag */
		cs->hw.niccy.isac = pci_ioaddr + ISAC_PCI_DATA;
		cs->hw.niccy.isac_ale = pci_ioaddr + ISAC_PCI_ADDR;
		cs->hw.niccy.hscx = pci_ioaddr + HSCX_PCI_DATA;
		cs->hw.niccy.hscx_ale = pci_ioaddr + HSCX_PCI_ADDR;
		cs->irq = pci_irq;
		if (check_region((cs->hw.niccy.isac), 4)) {
			printk(KERN_WARNING
				"HiSax: %s data port %x-%x already in use\n",
				CardType[card->typ],
				cs->hw.niccy.isac,
				cs->hw.niccy.isac + 4);
			return (0);
		} else
			request_region(cs->hw.niccy.isac, 4, "niccy");
#else
		printk(KERN_WARNING "Niccy: io0 0 and NO_PCI_BIOS\n");
		printk(KERN_WARNING "Niccy: unable to config NICCY PCI\n");
		return (0);
#endif /* CONFIG_PCI */
	}
	printk(KERN_INFO
		"HiSax: %s %s config irq:%d data:0x%X ale:0x%X\n",
		CardType[cs->typ], (cs->subtyp==1) ? "PnP":"PCI",
		cs->irq, cs->hw.niccy.isac, cs->hw.niccy.isac_ale);
	niccy_reset(cs);
	cs->readisac = &ReadISAC;
	cs->writeisac = &WriteISAC;
	cs->readisacfifo = &ReadISACfifo;
	cs->writeisacfifo = &WriteISACfifo;
	cs->BC_Read_Reg = &ReadHSCX;
	cs->BC_Write_Reg = &WriteHSCX;
	cs->BC_Send_Data = &hscx_fill_fifo;
	cs->cardmsg = &niccy_card_msg;
	ISACVersion(cs, "Niccy:");
	if (HscxVersion(cs, "Niccy:")) {
		printk(KERN_WARNING
		    "Niccy: wrong HSCX versions check IO address\n");
		release_io_niccy(cs);
		return (0);
	}
	return (1);
}
