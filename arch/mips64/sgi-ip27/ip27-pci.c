/* $Id: ip27-pci.c,v 1.8 2000/02/16 01:07:30 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1999, 2000 Ralf Baechle (ralf@gnu.org)
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <asm/sn/arch.h>
#include <asm/pci/bridge.h>
#include <asm/paccess.h>
#include <asm/sn/sn0/ip27.h>
#include <asm/sn/sn0/hub.h>

/*
 * The Bridge ASIC supports both type 0 and type 1 access.  Type 1 is
 * not really documented, so right now I can't write code which uses it.
 * Therefore we use type 0 accesses for now even though they won't work
 * correcly for PCI-to-PCI bridges.
 */
#define CF0_READ_PCI_CFG(dev,where,value,bm,mask)			\
do {									\
	bridge_t *bridge;                                               \
	int slot = PCI_SLOT(dev->devfn);				\
	int fn = PCI_FUNC(dev->devfn);					\
	volatile u32 *addr;						\
	u32 cf, __bit;							\
	unsigned int bus_id = (unsigned) dev->bus->number;              \
									\
	bridge = (bridge_t *) NODE_SWIN_BASE(bus_to_nid[bus_id],        \
                                             bus_to_wid[bus_id]);       \
                                                                        \
	/*if (dev->bus->number)			 */			\
	/*	return PCIBIOS_DEVICE_NOT_FOUND; */			\
									\
	__bit = (((where) & (bm)) << 3);				\
	addr = &bridge->b_type0_cfg_dev[slot].f[fn].l[where >> 2];	\
	if (get_dbe(cf, addr))						\
		return PCIBIOS_DEVICE_NOT_FOUND;			\
	*value = (cf >> __bit) & (mask);				\
	return PCIBIOS_SUCCESSFUL;					\
} while (0)

static int
pci_conf0_read_config_byte(struct pci_dev *dev, int where, u8 *value)
{
	CF0_READ_PCI_CFG(dev,where,value,3,0xff);
}

static int
pci_conf0_read_config_word(struct pci_dev *dev, int where, u16 *value)
{
	CF0_READ_PCI_CFG(dev,where,value,2,0xffff);
}

static int
pci_conf0_read_config_dword(struct pci_dev *dev, int where, u32 *value)
{
	CF0_READ_PCI_CFG(dev,where,value,0,0xffffffff);
}

#define CF0_WRITE_PCI_CFG(dev,where,value,bm,mask)			\
do {									\
	bridge_t *bridge;                                               \
	int slot = PCI_SLOT(dev->devfn);				\
	int fn = PCI_FUNC(dev->devfn);					\
	volatile u32 *addr;						\
	u32 cf, __bit;							\
	unsigned int bus_id = (unsigned) dev->bus->number;              \
									\
	bridge = (bridge_t *) NODE_SWIN_BASE(bus_to_nid[bus_id],        \
                                             bus_to_wid[bus_id]);       \
                                                                        \
	/* if (dev->bus->number)		 */			\
	/* 	return PCIBIOS_DEVICE_NOT_FOUND; */			\
									\
	if (dev->vendor == PCI_VENDOR_ID_SGI				\
	    && dev->device == PCI_DEVICE_ID_SGI_IOC3)			\
		return PCIBIOS_SUCCESSFUL;				\
									\
	__bit = (((where) & (bm)) << 3);				\
	addr = &bridge->b_type0_cfg_dev[slot].f[fn].l[where >> 2];	\
	if (get_dbe(cf, addr))						\
		return PCIBIOS_DEVICE_NOT_FOUND;			\
	cf &= (~mask);							\
	cf |= (value);							\
	put_dbe(cf, addr);						\
	return PCIBIOS_SUCCESSFUL;					\
} while (0)

static int
pci_conf0_write_config_byte(struct pci_dev *dev, int where, u8 value)
{
	CF0_WRITE_PCI_CFG(dev,where,value,3,0xff);
}

static int
pci_conf0_write_config_word(struct pci_dev *dev, int where, u16 value)
{
	CF0_WRITE_PCI_CFG(dev,where,value,2,0xffff);
}

static int
pci_conf0_write_config_dword(struct pci_dev *dev, int where, u32 value)
{
	CF0_WRITE_PCI_CFG(dev,where,value,0,0xffffffff);
}


static struct pci_ops bridge_pci_ops = {
	pci_conf0_read_config_byte,
	pci_conf0_read_config_word,
	pci_conf0_read_config_dword,
	pci_conf0_write_config_byte,
	pci_conf0_write_config_word,
	pci_conf0_write_config_dword
};

void __init pcibios_init(void)
{
	struct pci_ops *ops = &bridge_pci_ops;
	nasid_t nid = get_nasid();
	int	i;

	ioport_resource.end = ~0UL;

	for (i=0; i<num_bridges; i++) {
		printk("PCI: Probing PCI hardware on host bus %2d, node %d.\n", i, nid);
		pci_scan_bus(i, ops, NULL);
	}
}

static inline u8
bridge_swizzle(u8 pin, u8 slot) 
{
	return (((pin-1) + slot) % 4) + 1;
}

static u8 __init
pci_swizzle(struct pci_dev *dev, u8 *pinp)
{
	u8 pin = *pinp;

	while (dev->bus->self) {	/* Move up the chain of bridges. */
		pin = bridge_swizzle(pin, PCI_SLOT(dev->devfn));
		dev = dev->bus->self;
	}
	*pinp = pin;

	return PCI_SLOT(dev->devfn);
}

/* XXX This should include the node ID into the final interrupt number.  */
static int __init
pci_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	int rv;
	rv = (slot + (((pin-1) & 1) << 2)) & 7;
	rv |= (bus_to_wid[dev->bus->number] << 8);
	rv |= (bus_to_nid[dev->bus->number] << 16);
	return rv;
}

void __init
pcibios_update_irq(struct pci_dev *dev, int irq)
{
	pci_write_config_byte(dev, PCI_INTERRUPT_LINE, irq);
}

void __init
pcibios_update_resource(struct pci_dev *dev, struct resource *root,
                        struct resource *res, int resource)
{
	unsigned long where, size;
	u32 reg;

	where = PCI_BASE_ADDRESS_0 + (resource * 4);
	size = res->end - res->start;
	pci_read_config_dword(dev, where, &reg);
	reg = (reg & size) | (((u32)(res->start - root->start)) & ~size);
	pci_write_config_dword(dev, where, reg);
}

void __init
pcibios_fixup_bus(struct pci_bus *b)
{
	pci_fixup_irqs(pci_swizzle, pci_map_irq);
}

void __init
pcibios_fixup_pbus_ranges(struct pci_bus * bus,
                          struct pbus_set_ranges_data * ranges)
{
	ranges->io_start -= bus->resource[0]->start;
	ranges->io_end -= bus->resource[0]->start;
	ranges->mem_start -= bus->resource[1]->start;
	ranges->mem_end -= bus->resource[1]->start;
}

int __init
pcibios_enable_device(struct pci_dev *dev)
{
	/* Not needed, since we enable all devices at startup.  */
	return 0;
}

void __init
pcibios_align_resource(void *data, struct resource *res, unsigned long size)
{
}

char * __init
pcibios_setup(char *str)
{
	/* Nothing to do for now.  */

	return str;
}

static void __init
pci_fixup_ioc3(struct pci_dev *d)
{
	int i;

	/* IOC3 only decodes 0x20 bytes of the config space, so we end up
	   with tons of bogus information in the pci_dev.  On Origins the
	   INTA, INTB and INTC pins are all wired together as if it'd only
	   use INTA.  */
	printk("PCI: Fixing base addresses for IOC3 device %s\n", d->slot_name);

	for (i = 1; i <= PCI_ROM_RESOURCE; i++) {
		d->resource[i].start = 0UL;
		d->resource[i].end = 0UL;
		d->resource[i].flags = 0UL;
	}
	d->subsystem_vendor = 0;
	d->subsystem_device = 0;
	d->irq = 1;
}

static void __init
pci_fixup_isp1020(struct pci_dev *d)
{
	unsigned short command;

	printk("PCI: Fixing isp1020 in [bus:slot.fn] %s\n", d->slot_name);

	/* Configure device to allow bus mastering, i/o and memory mapping. 
	 * Older qlogicisp driver expects to have the IO space enable 
	 * bit set. Things stop working if we program the controllers as not having
	 * PCI_COMMAND_MEMORY, so we have to fudge the mem_flags.
	 */

	/* only turn on scsi's on main bus */
	if (d->bus->number == 0) {
		pci_set_master(d);
		pci_read_config_word(d, PCI_COMMAND, &command);
		command |= PCI_COMMAND_MEMORY;
		command |= PCI_COMMAND_IO;
		pci_write_config_word(d, PCI_COMMAND, command);
		d->resource[1].flags |= 1;
	}
}
static void __init
pci_fixup_isp2x00(struct pci_dev *d)
{
	unsigned int bus_id = (unsigned) d->bus->number;
	bridge_t *bridge = (bridge_t *) NODE_SWIN_BASE(bus_to_nid[bus_id],
				     bus_to_wid[bus_id]);
	bridgereg_t 	devreg;
	int		i;
	int		slot = PCI_SLOT(d->devfn);
	unsigned int	start;
	unsigned short command;

	printk("PCI: Fixing isp2x00 in [bus:slot.fn] %s\n", d->slot_name);

	/* set the resource struct for this device */
	start = (u32) bridge;	/* yes, we want to lose the upper 32 bits here */
	start |= BRIDGE_DEVIO(slot);

	d->resource[0].start = start;
	d->resource[0].end = d->resource[0].start + 0xff;
	d->resource[0].flags = IORESOURCE_IO;

	d->resource[1].start = start;
	d->resource[1].end = d->resource[0].start + 0xfff;
	d->resource[1].flags = IORESOURCE_MEM;

	/*
	 * set the bridge device(x) reg for this device
	 */
	devreg = bridge->b_device[slot].reg;
	/* point device(x) to it appropriate small window */
	devreg &= ~BRIDGE_DEV_OFF_MASK;
	devreg |= (start >> 20) & BRIDGE_DEV_OFF_MASK;

	/* turn on byte swapping in direct map mode (how we currently run dma's) */
	devreg |= BRIDGE_DEV_SWAP_DIR;		/* turn on byte swapping */

	bridge->b_device[slot].reg = devreg;

	/* set card's base addr reg */
	//pci_conf0_write_config_dword(d, PCI_BASE_ADDRESS_0, 0x500001);
	//pci_conf0_write_config_dword(d, PCI_BASE_ADDRESS_1, 0x8b00000);
	//pci_conf0_write_config_dword(d, PCI_ROM_ADDRESS, 0x8b20000);

	/* I got these from booting irix on system...*/
	pci_conf0_write_config_dword(d, PCI_BASE_ADDRESS_0, 0x200001);
	//pci_conf0_write_config_dword(d, PCI_BASE_ADDRESS_1, 0xf800000);
	pci_conf0_write_config_dword(d, PCI_ROM_ADDRESS,   0x10200000);

	pci_conf0_write_config_dword(d, PCI_BASE_ADDRESS_1, start);
	//pci_conf0_write_config_dword(d, PCI_ROM_ADDRESS, (start | 0x20000));


	/* set cache line size */
	pci_conf0_write_config_dword(d, PCI_CACHE_LINE_SIZE, 0xf080);

	/* set pci bus timeout */
	bridge->b_bus_timeout |= BRIDGE_BUS_PCI_RETRY_HLD(0x3);
	bridge->b_wid_tflush;
	printk("PCI: bridge bus timeout= 0x%x \n", bridge->b_bus_timeout);

	/* set host error field */
	bridge->b_int_host_err = 0x44;
	bridge->b_wid_tflush;
		
	bridge->b_wid_tflush;   	/* wait until Bridge PIO complete */
	for (i=0; i<8; i++)
		printk("PCI: device(%d)= 0x%x\n",i,bridge->b_device[i].reg);

	/* configure device to allow bus mastering, i/o and memory mapping */
	pci_set_master(d);
	pci_read_config_word(d, PCI_COMMAND, &command);
	command |= PCI_COMMAND_MEMORY;
	command |= PCI_COMMAND_IO;
	pci_write_config_word(d, PCI_COMMAND, command);
	/*d->resource[1].flags |= 1;*/
}

struct pci_fixup pcibios_fixups[] = {
	{ PCI_FIXUP_HEADER, PCI_VENDOR_ID_SGI, PCI_DEVICE_ID_SGI_IOC3,
	  pci_fixup_ioc3 },
	{ PCI_FIXUP_HEADER, PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP1020,
	  pci_fixup_isp1020 },
	{ PCI_FIXUP_HEADER, PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP2100,
	  pci_fixup_isp2x00 },
	{ PCI_FIXUP_HEADER, PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP2200,
	  pci_fixup_isp2x00 },
	{ 0 }
};
