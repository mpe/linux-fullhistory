/* $Id: asuscom.c,v 1.11.6.3 2001/09/23 22:24:46 kai Exp $
 *
 * low level stuff for ASUSCOM NETWORK INC. ISDNLink cards
 *
 * Author       Karsten Keil
 * Copyright    by Karsten Keil      <keil@isdn4linux.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * Thanks to  ASUSCOM NETWORK INC. Taiwan and  Dynalink NL for information
 *
 */

#include <linux/init.h>
#include <linux/isapnp.h>
#include "hisax.h"
#include "isac.h"
#include "ipac.h"
#include "hscx.h"
#include "isdnl1.h"

extern const char *CardType[];

const char *Asuscom_revision = "$Revision: 1.11.6.3 $";

static spinlock_t asuscom_lock = SPIN_LOCK_UNLOCKED;
#define byteout(addr,val) outb(val,addr)
#define bytein(addr) inb(addr)

#define ASUS_ISAC	0
#define ASUS_HSCX	1
#define ASUS_ADR	2
#define ASUS_CTRL_U7	3
#define ASUS_CTRL_POTS	5

#define ASUS_IPAC_ALE	0
#define ASUS_IPAC_DATA	1

#define ASUS_ISACHSCX	1
#define ASUS_IPAC	2

/* CARD_ADR (Write) */
#define ASUS_RESET      0x80	/* Bit 7 Reset-Leitung */

static inline u8
readreg(struct IsdnCardState *cs, unsigned int adr, u8 off)
{
	u8 ret;
	unsigned long flags;

	spin_lock_irqsave(&asuscom_lock, flags);
	byteout(cs->hw.asus.adr, off);
	ret = bytein(adr);
	spin_unlock_irqrestore(&asuscom_lock, flags);
	return ret;
}

static inline void
writereg(struct IsdnCardState *cs, unsigned int adr, u8 off, u8 data)
{
	unsigned long flags;

	spin_lock_irqsave(&asuscom_lock, flags);
	byteout(cs->hw.asus.adr, off);
	byteout(adr, data);
	spin_unlock_irqrestore(&asuscom_lock, flags);
}

static inline void
readfifo(struct IsdnCardState *cs, unsigned int adr, u8 off, u8 * data, int size)
{
	byteout(cs->hw.asus.adr, off);
	insb(adr, data, size);
}


static inline void
writefifo(struct IsdnCardState *cs, unsigned int adr, u8 off, u8 * data, int size)
{
	byteout(cs->hw.asus.adr, off);
	outsb(adr, data, size);
}

static u8
isac_read(struct IsdnCardState *cs, u8 offset)
{
	return readreg(cs, cs->hw.asus.isac, offset);
}

static void
isac_write(struct IsdnCardState *cs, u8 offset, u8 value)
{
	writereg(cs, cs->hw.asus.isac, offset, value);
}

static void
isac_read_fifo(struct IsdnCardState *cs, u8 * data, int size)
{
	readfifo(cs, cs->hw.asus.isac, 0, data, size);
}

static void
isac_write_fifo(struct IsdnCardState *cs, u8 * data, int size)
{
	writefifo(cs, cs->hw.asus.isac, 0, data, size);
}

static struct dc_hw_ops isac_ops = {
	.read_reg   = isac_read,
	.write_reg  = isac_write,
	.read_fifo  = isac_read_fifo,
	.write_fifo = isac_write_fifo,
};

static u8
ipac_dc_read(struct IsdnCardState *cs, u8 offset)
{
	return readreg(cs, cs->hw.asus.isac, offset|0x80);
}

static void
ipac_dc_write(struct IsdnCardState *cs, u8 offset, u8 value)
{
	writereg(cs, cs->hw.asus.isac, offset|0x80, value);
}

static void
ipac_dc_read_fifo(struct IsdnCardState *cs, u8 * data, int size)
{
	readfifo(cs, cs->hw.asus.isac, 0x80, data, size);
}

static void
ipac_dc_write_fifo(struct IsdnCardState *cs, u8 * data, int size)
{
	writefifo(cs, cs->hw.asus.isac, 0x80, data, size);
}

static struct dc_hw_ops ipac_dc_ops = {
	.read_reg   = ipac_dc_read,
	.write_reg  = ipac_dc_write,
	.read_fifo  = ipac_dc_read_fifo,
	.write_fifo = ipac_dc_write_fifo,
};

static u8
hscx_read(struct IsdnCardState *cs, int hscx, u8 offset)
{
	return readreg(cs, cs->hw.asus.hscx, offset + (hscx ? 0x40 : 0));
}

static void
hscx_write(struct IsdnCardState *cs, int hscx, u8 offset, u8 value)
{
	writereg(cs, cs->hw.asus.hscx, offset + (hscx ? 0x40 : 0), value);
}

static void
hscx_read_fifo(struct IsdnCardState *cs, int hscx, u8 *data, int size)
{
	readfifo(cs, cs->hw.asus.hscx, hscx ? 0x40 : 0, data, size);
}

static void
hscx_write_fifo(struct IsdnCardState *cs, int hscx, u8 *data, int size)
{
	writefifo(cs, cs->hw.asus.hscx, hscx ? 0x40 : 0, data, size);
}

static struct bc_hw_ops hscx_ops = {
	.read_reg   = hscx_read,
	.write_reg  = hscx_write,
	.read_fifo  = hscx_read_fifo,
	.write_fifo = hscx_write_fifo,
};

static void
asuscom_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs = dev_id;
	u8 val;

	spin_lock(&cs->lock);
	val = hscx_read(cs, 1, HSCX_ISTA);
      Start_HSCX:
	if (val)
		hscx_int_main(cs, val);
	val = isac_read(cs, ISAC_ISTA);
      Start_ISAC:
	if (val)
		isac_interrupt(cs, val);
	val = hscx_read(cs, 1, HSCX_ISTA);
	if (val) {
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "HSCX IntStat after IntRoutine");
		goto Start_HSCX;
	}
	val = isac_read(cs, ISAC_ISTA);
	if (val) {
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "ISAC IntStat after IntRoutine");
		goto Start_ISAC;
	}
	hscx_write(cs, 0, HSCX_MASK, 0xFF);
	hscx_write(cs, 1, HSCX_MASK, 0xFF);
	isac_write(cs, ISAC_MASK, 0xFF);
	isac_write(cs, ISAC_MASK, 0x0);
	hscx_write(cs, 0, HSCX_MASK, 0x0);
	hscx_write(cs, 1, HSCX_MASK, 0x0);
	spin_unlock(&cs->lock);
}

static void
asuscom_interrupt_ipac(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs = dev_id;
	u8 ista, val, icnt = 5;

	if (!cs) {
		printk(KERN_WARNING "ISDNLink: Spurious interrupt!\n");
		return;
	}
	ista = readreg(cs, cs->hw.asus.isac, IPAC_ISTA);
Start_IPAC:
	if (cs->debug & L1_DEB_IPAC)
		debugl1(cs, "IPAC ISTA %02X", ista);
	if (ista & 0x0f) {
		val = hscx_read(cs, 1, HSCX_ISTA);
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
		val = ipac_dc_read(cs, ISAC_ISTA) & 0xfe;
		if (val) {
			isac_interrupt(cs, val);
		}
	}
	if (ista & 0x10) {
		val = 0x01;
		isac_interrupt(cs, val);
	}
	ista  = readreg(cs, cs->hw.asus.isac, IPAC_ISTA);
	if ((ista & 0x3f) && icnt) {
		icnt--;
		goto Start_IPAC;
	}
	if (!icnt)
		printk(KERN_WARNING "ASUS IRQ LOOP\n");
	writereg(cs, cs->hw.asus.isac, IPAC_MASK, 0xFF);
	writereg(cs, cs->hw.asus.isac, IPAC_MASK, 0xC0);
}

void
release_io_asuscom(struct IsdnCardState *cs)
{
	int bytecnt = 8;

	if (cs->hw.asus.cfg_reg)
		release_region(cs->hw.asus.cfg_reg, bytecnt);
}

static void
reset_asuscom(struct IsdnCardState *cs)
{
	if (cs->subtyp == ASUS_IPAC)
		writereg(cs, cs->hw.asus.isac, IPAC_POTA2, 0x20);
	else
		byteout(cs->hw.asus.adr, ASUS_RESET);	/* Reset On */
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((10*HZ)/1000);
	if (cs->subtyp == ASUS_IPAC)
		writereg(cs, cs->hw.asus.isac, IPAC_POTA2, 0x0);
	else
		byteout(cs->hw.asus.adr, 0);	/* Reset Off */
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((10*HZ)/1000);
	if (cs->subtyp == ASUS_IPAC) {
		writereg(cs, cs->hw.asus.isac, IPAC_CONF, 0x0);
		writereg(cs, cs->hw.asus.isac, IPAC_ACFG, 0xff);
		writereg(cs, cs->hw.asus.isac, IPAC_AOE, 0x0);
		writereg(cs, cs->hw.asus.isac, IPAC_MASK, 0xc0);
		writereg(cs, cs->hw.asus.isac, IPAC_PCFG, 0x12);
	}
}

static int
Asus_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
	switch (mt) {
		case CARD_RESET:
			reset_asuscom(cs);
			return(0);
		case CARD_RELEASE:
			release_io_asuscom(cs);
			return(0);
		case CARD_INIT:
			cs->debug |= L1_DEB_IPAC;
			inithscxisac(cs);
			return(0);
		case CARD_TEST:
			return(0);
	}
	return(0);
}

#ifdef __ISAPNP__
static struct isapnp_device_id asus_ids[] __initdata = {
	{ ISAPNP_VENDOR('A', 'S', 'U'), ISAPNP_FUNCTION(0x1688),
	  ISAPNP_VENDOR('A', 'S', 'U'), ISAPNP_FUNCTION(0x1688), 
	  (unsigned long) "Asus1688 PnP" },
	{ ISAPNP_VENDOR('A', 'S', 'U'), ISAPNP_FUNCTION(0x1690),
	  ISAPNP_VENDOR('A', 'S', 'U'), ISAPNP_FUNCTION(0x1690), 
	  (unsigned long) "Asus1690 PnP" },
	{ ISAPNP_VENDOR('S', 'I', 'E'), ISAPNP_FUNCTION(0x0020),
	  ISAPNP_VENDOR('S', 'I', 'E'), ISAPNP_FUNCTION(0x0020), 
	  (unsigned long) "Isurf2 PnP" },
	{ ISAPNP_VENDOR('E', 'L', 'F'), ISAPNP_FUNCTION(0x0000),
	  ISAPNP_VENDOR('E', 'L', 'F'), ISAPNP_FUNCTION(0x0000), 
	  (unsigned long) "Iscas TE320" },
	{ 0, }
};

static struct isapnp_device_id *adev = &asus_ids[0];
static struct pci_bus *pnp_c __devinitdata = NULL;
#endif

int __init
setup_asuscom(struct IsdnCard *card)
{
	int bytecnt;
	struct IsdnCardState *cs = card->cs;
	u8 val;
	char tmp[64];

	strcpy(tmp, Asuscom_revision);
	printk(KERN_INFO "HiSax: Asuscom ISDNLink driver Rev. %s\n", HiSax_getrev(tmp));
	if (cs->typ != ISDN_CTYPE_ASUSCOM)
		return (0);
#ifdef __ISAPNP__
	if (!card->para[1] && isapnp_present()) {
		struct pci_bus *pb;
		struct pci_dev *pd;

		while(adev->card_vendor) {
			if ((pb = isapnp_find_card(adev->card_vendor,
				adev->card_device, pnp_c))) {
				pnp_c = pb;
				pd = NULL;
				if ((pd = isapnp_find_dev(pnp_c,
					adev->vendor, adev->function, pd))) {
					printk(KERN_INFO "HiSax: %s detected\n",
						(char *)adev->driver_data);
					pd->prepare(pd);
					pd->deactivate(pd);
					pd->activate(pd);
					card->para[1] = pd->resource[0].start;
					card->para[0] = pd->irq_resource[0].start;
					if (!card->para[0] || !card->para[1]) {
						printk(KERN_ERR "AsusPnP:some resources are missing %ld/%lx\n",
						card->para[0], card->para[1]);
						pd->deactivate(pd);
						return(0);
					}
					break;
				} else {
					printk(KERN_ERR "AsusPnP: PnP error card found, no device\n");
				}
			}
			adev++;
			pnp_c=NULL;
		} 
		if (!adev->card_vendor) {
			printk(KERN_INFO "AsusPnP: no ISAPnP card found\n");
			return(0);
		}
	}
#endif
	bytecnt = 8;
	cs->hw.asus.cfg_reg = card->para[1];
	cs->irq = card->para[0];
	if (!request_region((cs->hw.asus.cfg_reg), bytecnt,
			    "asuscom isdn")) {
		printk(KERN_WARNING
		       "HiSax: %s config port %x-%x already in use\n",
		       CardType[card->typ],
		       cs->hw.asus.cfg_reg,
		       cs->hw.asus.cfg_reg + bytecnt);
		return (0);
	}
	printk(KERN_INFO "ISDNLink: defined at 0x%x IRQ %d\n",
		cs->hw.asus.cfg_reg, cs->irq);
	cs->bc_hw_ops = &hscx_ops;
	cs->cardmsg = &Asus_card_msg;
	cs->hw.asus.adr = cs->hw.asus.cfg_reg + ASUS_IPAC_ALE;
	val = readreg(cs, cs->hw.asus.cfg_reg + ASUS_IPAC_DATA, IPAC_ID);
	if ((val == 1) || (val == 2)) {
		cs->subtyp = ASUS_IPAC;
		cs->hw.asus.isac = cs->hw.asus.cfg_reg + ASUS_IPAC_DATA;
		cs->hw.asus.hscx = cs->hw.asus.cfg_reg + ASUS_IPAC_DATA;
		test_and_set_bit(HW_IPAC, &cs->HW_Flags);
		cs->dc_hw_ops = &ipac_dc_ops;
		cs->irq_func = &asuscom_interrupt_ipac;
		printk(KERN_INFO "Asus: IPAC version %x\n", val);
	} else {
		cs->subtyp = ASUS_ISACHSCX;
		cs->hw.asus.adr = cs->hw.asus.cfg_reg + ASUS_ADR;
		cs->hw.asus.isac = cs->hw.asus.cfg_reg + ASUS_ISAC;
		cs->hw.asus.hscx = cs->hw.asus.cfg_reg + ASUS_HSCX;
		cs->hw.asus.u7 = cs->hw.asus.cfg_reg + ASUS_CTRL_U7;
		cs->hw.asus.pots = cs->hw.asus.cfg_reg + ASUS_CTRL_POTS;
		cs->dc_hw_ops = &isac_ops;
		cs->irq_func = &asuscom_interrupt;
		ISACVersion(cs, "ISDNLink:");
		if (HscxVersion(cs, "ISDNLink:")) {
			printk(KERN_WARNING
		     	"ISDNLink: wrong HSCX versions check IO address\n");
			release_io_asuscom(cs);
			return (0);
		}
	}
	printk(KERN_INFO "ISDNLink: resetting card\n");
	reset_asuscom(cs);
	return (1);
}
