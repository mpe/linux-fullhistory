/* $Id: bkm_a8.c,v 1.8 1999/09/04 06:20:05 keil Exp $
 * bkm_a8.c     low level stuff for Scitel Quadro (4*S0, passive)
 *              derived from the original file sedlbauer.c
 *              derived from the original file niccy.c
 *              derived from the original file netjet.c
 *
 * Author       Roland Klabunde (R.Klabunde@Berkom.de)
 *
 * $Log: bkm_a8.c,v $
 * Revision 1.8  1999/09/04 06:20:05  keil
 * Changes from kernel set_current_state()
 *
 * Revision 1.7  1999/08/22 20:26:58  calle
 * backported changes from kernel 2.3.14:
 * - several #include "config.h" gone, others come.
 * - "struct device" changed to "struct net_device" in 2.3.14, added a
 *   define in isdn_compat.h for older kernel versions.
 *
 * Revision 1.6  1999/08/11 21:01:24  keil
 * new PCI codefix
 *
 * Revision 1.5  1999/08/10 16:01:48  calle
 * struct pci_dev changed in 2.3.13. Made the necessary changes.
 *
 * Revision 1.4  1999/07/14 11:43:15  keil
 * correct PCI_SUBSYSTEM_VENDOR_ID
 *
 * Revision 1.3  1999/07/12 21:04:59  keil
 * fix race in IRQ handling
 * added watchdog for lost IRQs
 *
 * Revision 1.2  1999/07/01 08:07:54  keil
 * Initial version
 *
 *
 */
#define __NO_VERSION__

#include <linux/config.h>
#include "hisax.h"
#include "isac.h"
#include "ipac.h"
#include "hscx.h"
#include "isdnl1.h"
#include "bkm_ax.h"
#include <linux/pci.h>

#define	ATTEMPT_PCI_REMAPPING	/* Required for PLX rev 1 */

extern const char *CardType[];

const char sct_quadro_revision[] = "$Revision: 1.8 $";

/* To survive the startup phase */
typedef struct {
	u_int active;		/* true/false */
	u_int base;		/* ipac base address */
} IPAC_STATE;

static IPAC_STATE ipac_state[4 + 1] __initdata =
{
	{0, 0},			/* dummy */
	{0, 0},			/* SCT_1 */
	{0, 0},			/* SCT_2 */
	{0, 0},			/* SCT_3 */
	{0, 0}			/* SCT_4 */
};

static const char *sct_quadro_subtypes[] =
{
	"",
	"#1",
	"#2",
	"#3",
	"#4"
};


#define wordout(addr,val) outw(val,addr)
#define wordin(addr) inw(addr)

static inline u_char
readreg(unsigned int ale, unsigned int adr, u_char off)
{
	register u_char ret;
	long flags;
	save_flags(flags);
	cli();
	wordout(ale, off);
	ret = wordin(adr) & 0xFF;
	restore_flags(flags);
	return (ret);
}

static inline void
readfifo(unsigned int ale, unsigned int adr, u_char off, u_char * data, int size)
{
	/* fifo read without cli because it's allready done  */
	int i;
	wordout(ale, off);
	for (i = 0; i < size; i++)
		data[i] = wordin(adr) & 0xFF;
}


static inline void
writereg(unsigned int ale, unsigned int adr, u_char off, u_char data)
{
	long flags;
	save_flags(flags);
	cli();
	wordout(ale, off);
	wordout(adr, data);
	restore_flags(flags);
}

static inline void
writefifo(unsigned int ale, unsigned int adr, u_char off, u_char * data, int size)
{
	/* fifo write without cli because it's allready done  */
	int i;
	wordout(ale, off);
	for (i = 0; i < size; i++)
		wordout(adr, data[i]);
}

/* Interface functions */

static u_char
ReadISAC(struct IsdnCardState *cs, u_char offset)
{
	return (readreg(cs->hw.ax.base, cs->hw.ax.data_adr, offset | 0x80));
}

static void
WriteISAC(struct IsdnCardState *cs, u_char offset, u_char value)
{
	writereg(cs->hw.ax.base, cs->hw.ax.data_adr, offset | 0x80, value);
}

static void
ReadISACfifo(struct IsdnCardState *cs, u_char * data, int size)
{
	readfifo(cs->hw.ax.base, cs->hw.ax.data_adr, 0x80, data, size);
}

static void
WriteISACfifo(struct IsdnCardState *cs, u_char * data, int size)
{
	writefifo(cs->hw.ax.base, cs->hw.ax.data_adr, 0x80, data, size);
}


static u_char
ReadHSCX(struct IsdnCardState *cs, int hscx, u_char offset)
{
	return (readreg(cs->hw.ax.base, cs->hw.ax.data_adr, offset + (hscx ? 0x40 : 0)));
}

static void
WriteHSCX(struct IsdnCardState *cs, int hscx, u_char offset, u_char value)
{
	writereg(cs->hw.ax.base, cs->hw.ax.data_adr, offset + (hscx ? 0x40 : 0), value);
}

/* Check whether the specified ipac is already active or not */
static int
is_ipac_active(u_int ipac_nr)
{
	return (ipac_state[ipac_nr].active);
}

/* Set the specific ipac to active */
static void
set_ipac_active(u_int ipac_nr, u_int active)
{
	/* set activation state */
	ipac_state[ipac_nr].active = active;
}

/*
 * fast interrupt HSCX stuff goes here
 */

#define READHSCX(cs, nr, reg) readreg(cs->hw.ax.base, \
	cs->hw.ax.data_adr, reg + (nr ? 0x40 : 0))
#define WRITEHSCX(cs, nr, reg, data) writereg(cs->hw.ax.base, \
	cs->hw.ax.data_adr, reg + (nr ? 0x40 : 0), data)
#define READHSCXFIFO(cs, nr, ptr, cnt) readfifo(cs->hw.ax.base, \
	cs->hw.ax.data_adr, (nr ? 0x40 : 0), ptr, cnt)
#define WRITEHSCXFIFO(cs, nr, ptr, cnt) writefifo(cs->hw.ax.base, \
	cs->hw.ax.data_adr, (nr ? 0x40 : 0), ptr, cnt)

#include "hscx_irq.c"

static void
bkm_interrupt_ipac(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs = dev_id;
	u_char ista, val, icnt = 5;
	int i;
	if (!cs) {
		printk(KERN_WARNING "HiSax: Scitel Quadro: Spurious interrupt!\n");
		return;
	}
	ista = readreg(cs->hw.ax.base, cs->hw.ax.data_adr, IPAC_ISTA);

      Start_IPAC:
	if (cs->debug & L1_DEB_IPAC)
		debugl1(cs, "IPAC ISTA %02X", ista);
	if (ista & 0x0f) {
		val = readreg(cs->hw.ax.base, cs->hw.ax.data_adr, HSCX_ISTA + 0x40);
		if (ista & 0x01)
			val |= 0x01;
		if (ista & 0x04)
			val |= 0x02;
		if (ista & 0x08)
			val |= 0x04;
		if (val) {
			hscx_int_main(cs, val);
		}
	}
	if (ista & 0x20) {
		val = 0xfe & readreg(cs->hw.ax.base, cs->hw.ax.data_adr, ISAC_ISTA | 0x80);
		if (val) {
			isac_interrupt(cs, val);
		}
	}
	if (ista & 0x10) {
		val = 0x01;
		isac_interrupt(cs, val);
	}
	ista = readreg(cs->hw.ax.base, cs->hw.ax.data_adr, IPAC_ISTA);
	if ((ista & 0x3f) && icnt) {
		icnt--;
		goto Start_IPAC;
	}
	if (!icnt)
		printk(KERN_WARNING "HiSax: %s (%s) IRQ LOOP\n",
		       CardType[cs->typ],
		       sct_quadro_subtypes[cs->subtyp]);
	writereg(cs->hw.ax.base, cs->hw.ax.data_adr, IPAC_MASK, 0xFF);
	writereg(cs->hw.ax.base, cs->hw.ax.data_adr, IPAC_MASK, 0xC0);

	/* Read out all interrupt sources from currently not active ipacs */
	/* "Handle" all interrupts from currently not active ipac by reading the regs */
	for (i = SCT_1; i <= SCT_4; i++)
		if (!is_ipac_active(i)) {
			u_int base = ipac_state[i].base;
			if (readreg(base, base + 4, 0xC1)) {
				readreg(base, base + 4, 0xA0);
				readreg(base, base + 4, 0xA4);
				readreg(base, base + 4, 0x20);
				readreg(base, base + 4, 0x24);
				readreg(base, base + 4, 0x60);
				readreg(base, base + 4, 0x64);
				readreg(base, base + 4, 0xC1);
				readreg(base, base + 4, ISAC_CIR0 + 0x80);
			}
		}
}


void
release_io_sct_quadro(struct IsdnCardState *cs)
{
	/* ?? */
}

static void
enable_bkm_int(struct IsdnCardState *cs, unsigned bEnable)
{
	if (cs->typ == ISDN_CTYPE_SCT_QUADRO) {
		if (bEnable)
			wordout(cs->hw.ax.plx_adr + 0x4C, (wordin(cs->hw.ax.plx_adr + 0x4C) | 0x41));
		else
			/* Issue general di only if no ipac is active */
			if (!is_ipac_active(SCT_1) &&
			    !is_ipac_active(SCT_2) &&
			    !is_ipac_active(SCT_3) &&
			    !is_ipac_active(SCT_4))
			wordout(cs->hw.ax.plx_adr + 0x4C, (wordin(cs->hw.ax.plx_adr + 0x4C) & ~0x41));
	}
}

static void
reset_bkm(struct IsdnCardState *cs)
{
	long flags;

	if (cs->typ == ISDN_CTYPE_SCT_QUADRO) {
		if (!is_ipac_active(SCT_1) &&
		    !is_ipac_active(SCT_2) &&
		    !is_ipac_active(SCT_3) &&
		    !is_ipac_active(SCT_4)) {
			/* Issue total reset only if no ipac is active */
			wordout(cs->hw.ax.plx_adr + 0x50, (wordin(cs->hw.ax.plx_adr + 0x50) & ~4));

			save_flags(flags);
			sti();
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout((10 * HZ) / 1000);

			/* Remove the soft reset */
			wordout(cs->hw.ax.plx_adr + 0x50, (wordin(cs->hw.ax.plx_adr + 0x50) | 4));

			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout((10 * HZ) / 1000);
			restore_flags(flags);
		}
	}
}

static int
BKM_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
	switch (mt) {
		case CARD_RESET:
			/* Disable ints */
			set_ipac_active(cs->subtyp, 0);
			enable_bkm_int(cs, 0);
			reset_bkm(cs);
			return (0);
		case CARD_RELEASE:
			/* Sanity */
			set_ipac_active(cs->subtyp, 0);
			enable_bkm_int(cs, 0);
			reset_bkm(cs);
			release_io_sct_quadro(cs);
			return (0);
		case CARD_INIT:
			cs->debug |= L1_DEB_IPAC;
			set_ipac_active(cs->subtyp, 1);
			inithscxisac(cs, 3);
			/* Enable ints */
			enable_bkm_int(cs, 1);
			return (0);
		case CARD_TEST:
			return (0);
	}
	return (0);
}

static struct pci_dev *dev_a8 __initdata = NULL;

__initfunc(int
	   setup_sct_quadro(struct IsdnCard *card))
{
	struct IsdnCardState *cs = card->cs;
	char tmp[64];
#if CONFIG_PCI
	u_char pci_bus = 0, pci_device_fn = 0, pci_irq = 0, pci_rev_id;
	u_int found = 0;
	u_int pci_ioaddr1, pci_ioaddr2, pci_ioaddr3, pci_ioaddr4, pci_ioaddr5;
#endif

	strcpy(tmp, sct_quadro_revision);
	printk(KERN_INFO "HiSax: T-Berkom driver Rev. %s\n", HiSax_getrev(tmp));
	if (cs->typ == ISDN_CTYPE_SCT_QUADRO) {
		cs->subtyp = SCT_1;	/* Preset */
	} else
		return (0);

	/* Identify subtype by para[0] */
	if (card->para[0] >= SCT_1 && card->para[0] <= SCT_4)
		cs->subtyp = card->para[0];
	else
		printk(KERN_WARNING "HiSax: %s: Invalid subcontroller in configuration, default to 1\n",
		       CardType[card->typ]);
#if CONFIG_PCI
	if (!pci_present()) {
		printk(KERN_ERR "bkm_a4t: no PCI bus present\n");
		return (0);
	}
	if ((dev_a8 = pci_find_device(PLX_VENDOR_ID, PLX_DEVICE_ID, dev_a8))) {
		u_int sub_sys_id = 0;

		pci_read_config_dword(dev_a8, PCI_SUBSYSTEM_VENDOR_ID,
			&sub_sys_id);
		if (sub_sys_id == ((SCT_SUBSYS_ID << 16) | SCT_SUBVEN_ID)) {
			found = 1;
			pci_ioaddr1 = dev_a8->resource[ 1].start;
			pci_irq = dev_a8->irq;
			pci_bus = dev_a8->bus->number;
			pci_device_fn = dev_a8->devfn;
		}
	}
	if (!found) {
		printk(KERN_WARNING "HiSax: %s (%s): Card not found\n",
		       CardType[card->typ],
		       sct_quadro_subtypes[cs->subtyp]);
		return (0);
	}
	if (!pci_irq) {		/* IRQ range check ?? */
		printk(KERN_WARNING "HiSax: %s (%s): No IRQ\n",
		       CardType[card->typ],
		       sct_quadro_subtypes[cs->subtyp]);
		return (0);
	}
#ifdef ATTEMPT_PCI_REMAPPING
/* HACK: PLX revision 1 bug: PLX address bit 7 must not be set */
	pcibios_read_config_byte(pci_bus, pci_device_fn, PCI_REVISION_ID, &pci_rev_id);
	if ((pci_ioaddr1 & 0x80) && (pci_rev_id == 1)) {
		printk(KERN_WARNING "HiSax: %s (%s): PLX rev 1, remapping required!\n",
			CardType[card->typ],
			sct_quadro_subtypes[cs->subtyp]);
		/* Restart PCI negotiation */
		pcibios_write_config_dword(pci_bus, pci_device_fn,
			PCI_BASE_ADDRESS_1, (u_int) - 1);
		/* Move up by 0x80 byte */
		pci_ioaddr1 += 0x80;
		pci_ioaddr1 &= PCI_BASE_ADDRESS_IO_MASK;
		pcibios_write_config_dword(pci_bus, pci_device_fn,
			PCI_BASE_ADDRESS_1, pci_ioaddr1);
		dev_a8->resource[ 1].start = pci_ioaddr1;
	}
/* End HACK */
#endif
	pcibios_read_config_dword(pci_bus, pci_device_fn, PCI_BASE_ADDRESS_1, &pci_ioaddr1);
	pcibios_read_config_dword(pci_bus, pci_device_fn, PCI_BASE_ADDRESS_2, &pci_ioaddr2);
	pcibios_read_config_dword(pci_bus, pci_device_fn, PCI_BASE_ADDRESS_3, &pci_ioaddr3);
	pcibios_read_config_dword(pci_bus, pci_device_fn, PCI_BASE_ADDRESS_4, &pci_ioaddr4);
	pcibios_read_config_dword(pci_bus, pci_device_fn, PCI_BASE_ADDRESS_5, &pci_ioaddr5);
	if (!pci_ioaddr1 || !pci_ioaddr2 || !pci_ioaddr3 || !pci_ioaddr4 || !pci_ioaddr5) {
		printk(KERN_WARNING "HiSax: %s (%s): No IO base address(es)\n",
		       CardType[card->typ],
		       sct_quadro_subtypes[cs->subtyp]);
		return (0);
	}
	pci_ioaddr1 &= PCI_BASE_ADDRESS_IO_MASK;
	pci_ioaddr2 &= PCI_BASE_ADDRESS_IO_MASK;
	pci_ioaddr3 &= PCI_BASE_ADDRESS_IO_MASK;
	pci_ioaddr4 &= PCI_BASE_ADDRESS_IO_MASK;
	pci_ioaddr5 &= PCI_BASE_ADDRESS_IO_MASK;
	/* Take over */
	cs->irq = pci_irq;
	cs->irq_flags |= SA_SHIRQ;
	/* pci_ioaddr1 is unique to all subdevices */
	/* pci_ioaddr2 is for the fourth subdevice only */
	/* pci_ioaddr3 is for the third subdevice only */
	/* pci_ioaddr4 is for the second subdevice only */
	/* pci_ioaddr5 is for the first subdevice only */
	cs->hw.ax.plx_adr = pci_ioaddr1;
	/* Enter all ipac_base addresses */
	ipac_state[SCT_1].base = pci_ioaddr5 + 0x00;
	ipac_state[SCT_2].base = pci_ioaddr4 + 0x08;
	ipac_state[SCT_3].base = pci_ioaddr3 + 0x10;
	ipac_state[SCT_4].base = pci_ioaddr2 + 0x20;
	/* For isac and hscx control path */
	cs->hw.ax.base = ipac_state[cs->subtyp].base;
	/* For isac and hscx data path */
	cs->hw.ax.data_adr = cs->hw.ax.base + 4;
#else
	printk(KERN_WARNING "HiSax: %s (%s): NO_PCI_BIOS\n",
	       CardType[card->typ],
	       sct_quadro_subtypes[cs->subtyp]);
	printk(KERN_WARNING "HiSax: %s (%s): Unable to configure\n",
	       CardType[card->typ],
	       sct_quadro_subtypes[cs->subtyp]);
	return (0);
#endif				/* CONFIG_PCI */
	printk(KERN_INFO "HiSax: %s (%s) configured at 0x%.4X, 0x%.4X, 0x%.4X and IRQ %d\n",
	       CardType[card->typ],
	       sct_quadro_subtypes[cs->subtyp],
	       cs->hw.ax.plx_adr,
	       cs->hw.ax.base,
	       cs->hw.ax.data_adr,
	       cs->irq);

	test_and_set_bit(HW_IPAC, &cs->HW_Flags);

	/* Disable all currently not active ipacs */
	if (!is_ipac_active(SCT_1))
		set_ipac_active(SCT_1, 0);
	if (!is_ipac_active(SCT_2))
		set_ipac_active(SCT_2, 0);
	if (!is_ipac_active(SCT_3))
		set_ipac_active(SCT_3, 0);
	if (!is_ipac_active(SCT_4))
		set_ipac_active(SCT_4, 0);

	/* Perfom general reset (if possible) */
	reset_bkm(cs);

	cs->readisac = &ReadISAC;
	cs->writeisac = &WriteISAC;
	cs->readisacfifo = &ReadISACfifo;
	cs->writeisacfifo = &WriteISACfifo;

	cs->BC_Read_Reg = &ReadHSCX;
	cs->BC_Write_Reg = &WriteHSCX;
	cs->BC_Send_Data = &hscx_fill_fifo;
	cs->cardmsg = &BKM_card_msg;
	cs->irq_func = &bkm_interrupt_ipac;

	printk(KERN_INFO "HiSax: %s (%s): IPAC Version %d\n",
		CardType[card->typ],
		sct_quadro_subtypes[cs->subtyp],
		readreg(cs->hw.ax.base, cs->hw.ax.data_adr, IPAC_ID));
	return (1);
}
