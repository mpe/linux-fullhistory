/*
 *  arch/mips/ddb5074/pci.c -- NEC DDB Vrc-5074 PCI access routines
 *
 *  Copyright (C) 2000 Geert Uytterhoeven <geert@sonycom.com>
 *                     Albert Dorofeev <albert@sonycom.com>
 *                     Sony Suprastructure Center Europe (SUPC-E), Brussels
 *
 *  $Id: pci.c,v 1.4 2000/02/18 00:02:17 ralf Exp $
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <asm-mips/nile4.h>


static u32 nile4_pre_pci_access0(int slot_num)
{
    u32 pci_addr = 0;
    u32 virt_addr = NILE4_PCI_CFG_BASE;

    /* Set window 1 address 8000000 - 64 bit - 2 MB (PCI config space) */
    nile4_set_pdar(NILE4_PCIW1, PHYSADDR(virt_addr), 0x00200000, 64, 0, 0);
    if (slot_num > 2)
	pci_addr = 0x00040000 << slot_num;
    else
	virt_addr += 0x00040000 << slot_num;
    nile4_set_pmr(NILE4_PCIINIT1, NILE4_PCICMD_CFG, pci_addr);
    return virt_addr;
}

static void nile4_post_pci_access0(void)
{
    /* Set window 1 back to address 8000000 - 64 bit - 128 MB (PCI IO space) */
    nile4_set_pdar(NILE4_PCIW1, PHYSADDR(NILE4_PCI_MEM_BASE), 0x08000000, 64,
    		   1, 1);
    nile4_set_pmr(NILE4_PCIINIT1, NILE4_PCICMD_MEM, 0);
}


static int nile4_pci_read_config_dword( struct pci_dev *dev,
				int where, u32 *val)
{
    int slot_num, func_num;
    u32 base;

    /*
     *  For starters let's do configuration cycle 0 only (one bus only)
     */
    if (dev->bus->number)
	return PCIBIOS_FUNC_NOT_SUPPORTED;

    slot_num = PCI_SLOT(dev->devfn);
    func_num = PCI_FUNC(dev->devfn);
    if (slot_num == 5) {
	/*
	 *  This is Nile 4 and it will crash if we access it like other
	 *   devices
	 */
	*val = nile4_in32(NILE4_PCI_BASE + where);
	return PCIBIOS_SUCCESSFUL;
    }
    base = nile4_pre_pci_access0(slot_num);
    *val = *((volatile u32 *)(base + (func_num << 8) + (where & 0xfc)));
    nile4_post_pci_access0();
    return PCIBIOS_SUCCESSFUL;
}

static int nile4_pci_write_config_dword(struct pci_dev *dev, int where,
					u32 val)
{
    int slot_num, func_num;
    u32 base;

    /*
     *  For starters let's do configuration cycle 0 only (one bus only)
     */
    if (dev->bus->number)
	return PCIBIOS_FUNC_NOT_SUPPORTED;

    slot_num = PCI_SLOT(dev->devfn);
    func_num = PCI_FUNC(dev->devfn);
    if (slot_num == 5) {
	/*
	 *  This is Nile 4 and it will crash if we access it like other
	 *   devices
	 */
	nile4_out32(NILE4_PCI_BASE + where, val);
	return PCIBIOS_SUCCESSFUL;
    }
    base = nile4_pre_pci_access0(slot_num);
    *((volatile u32 *)(base + (func_num << 8) + (where & 0xfc))) = val;
    nile4_post_pci_access0();
    return PCIBIOS_SUCCESSFUL;
}

static int nile4_pci_read_config_word(struct pci_dev *dev, int where, u16 *val)
{
    int status;
    u32 result;

    status = nile4_pci_read_config_dword(dev, where, &result);
    if (status != PCIBIOS_SUCCESSFUL)
	return status;
    if (where & 2)
	result >>= 16;
    *val = result & 0xffff;
    return PCIBIOS_SUCCESSFUL;
}

static int nile4_pci_read_config_byte(struct pci_dev *dev, int where, u8 *val)
{
    int status;
    u32 result;

    status = nile4_pci_read_config_dword(dev, where, &result);
    if (status != PCIBIOS_SUCCESSFUL)
	return status;
    if (where & 1)
	result >>= 8;
    if (where & 2)
	result >>= 16;
    *val = result & 0xff;
    return PCIBIOS_SUCCESSFUL;
}

static int nile4_pci_write_config_word(struct pci_dev *dev, int where, u16 val)
{
    int status, shift = 0;
    u32 result;

    status = nile4_pci_read_config_dword(dev, where, &result);
    if (status != PCIBIOS_SUCCESSFUL)
	return status;
    if (where & 2)
	shift += 16;
    result &= ~(0xffff << shift);
    result |= val << shift;
    return nile4_pci_write_config_dword(dev, where, result);
}

static int nile4_pci_write_config_byte( struct pci_dev *dev, int where, u8 val)
{
    int status, shift = 0;
    u32 result;

    status = nile4_pci_read_config_dword(dev, where, &result);
    if (status != PCIBIOS_SUCCESSFUL)
	return status;
    if (where & 2)
	shift += 16;
    if (where & 1)
	shift += 8;
    result &= ~(0xff << shift);
    result |= val << shift;
    return nile4_pci_write_config_dword(dev, where, result);
}

struct pci_ops nile4_pci_ops = {
    nile4_pci_read_config_byte,
    nile4_pci_read_config_word,
    nile4_pci_read_config_dword,
    nile4_pci_write_config_byte,
    nile4_pci_write_config_word,
    nile4_pci_write_config_dword
};


static void __init pcibios_claim_resources(struct list_head *bus_list)
{
    struct list_head *ln, *dn;
    struct pci_bus *bus;
    struct pci_dev *dev;
    int idx;

    for (ln = bus_list->next; ln != bus_list; ln = ln->next) {
	bus = pci_bus_b(ln);
	for (dn = bus->devices.next; dn != &bus->devices; dn = dn->next) {
	    dev = pci_dev_b(dn);
	    for (idx = 0; idx < PCI_NUM_RESOURCES; idx++) {
		struct resource *r = &dev->resource[idx];
		struct resource *pr;
		if (!r->start)
		    continue;
		pr = pci_find_parent_resource(dev, r);
		if (!pr || request_resource(pr, r) < 0) {
		    printk(KERN_ERR "PCI: Address space collision on region %d of device %s\n", idx, dev->name);
		    /* We probably should disable the region, shouldn't we? */
		}
	    }
	}
	pcibios_claim_resources(&bus->children);
    }
}


void pcibios_init(void)
{
    printk("PCI: Probing PCI hardware\n");
    ioport_resource.end = 0x1ffffff;
    pci_scan_bus(0, &nile4_pci_ops, NULL);
    pcibios_claim_resources(&pci_root_buses);
}

void pcibios_fixup_bus(struct pci_bus *bus)
{
    struct list_head *dn;
    struct pci_dev *dev;
    extern struct pci_dev *pci_pmu;	/* for LEDs D2 and D3 */
    int slot_num, func_num;
    u8 t8;

    /*
     *  FIXME: PMON doesn't autoconfigure the PCI devices
     *         For now we just hardcode them for our configuration
     */
    printk("PCI: Configuring PCI devices (hardcoded)\n");
    for (dn = bus->devices.next; dn != &bus->devices; dn = dn->next) {
	dev = pci_dev_b(dn);

	slot_num = PCI_SLOT(dev->devfn);
	func_num = PCI_FUNC(dev->devfn);
	printk("  Device %2d: ", slot_num);
	switch (slot_num) {
	    case 0:
		printk("[onboard] Acer Labs M1533 Aladdin IV\n");
		dev->irq = nile4_to_irq(NILE4_INT_INTE);
		break;
	    case 1:
		printk("[onboard] DEC DC21140\n");
		dev->irq = nile4_to_irq(NILE4_INT_INTA);
		dev->resource[0].start = 0x100000;
		dev->resource[0].end = dev->resource[0].start+0x7f;
		nile4_pci_write_config_dword(dev, PCI_BASE_ADDRESS_0,
					     dev->resource[0].start);
		dev->resource[1].start = 0x1000000;
		dev->resource[1].end = dev->resource[1].start+0x7f;
		nile4_pci_write_config_dword(dev, PCI_BASE_ADDRESS_0,
					     dev->resource[1].start);
		break;
	    case 2:
		printk("[slot 1]  Realtek 8029\n");
		dev->irq = nile4_to_irq(NILE4_INT_INTA);
		dev->resource[0].start = 0x800000;
		dev->resource[0].end = dev->resource[0].start+0x1f;
		nile4_pci_write_config_dword(dev, PCI_BASE_ADDRESS_0,
					     dev->resource[0].start);
		break;
	    case 3:
		printk("[slot 2]  DEC DC21140 (#2)\n");
		dev->irq = nile4_to_irq(NILE4_INT_INTB);
		dev->resource[0].start = 0x1000000;
		dev->resource[0].end = dev->resource[0].start+0x7f;
		nile4_pci_write_config_dword(dev, PCI_BASE_ADDRESS_0,
					     dev->resource[0].start);
		dev->resource[1].start = 0x4000000;
		dev->resource[1].end = dev->resource[1].start+0x7f;
		nile4_pci_write_config_dword(dev, PCI_BASE_ADDRESS_0,
					     dev->resource[1].start);
		break;
	    case 4:
		printk("[slot 3]  Promise Technology IDE UltraDMA/33");
		printk(" or 3Com 3c905 :-)\n");
		dev->irq = nile4_to_irq(NILE4_INT_INTC);
		dev->resource[0].start = 0x1800000;
		dev->resource[0].end = dev->resource[0].start+0x7fffff;
		nile4_pci_write_config_dword(dev, PCI_BASE_ADDRESS_0,
					     dev->resource[0].start);
		break;
	    case 5:
		printk("[onboard] NEC Vrc-5074 Nile 4 Host Bridge\n");
		/*
		 * Fixup so the serial driver can use the UART
		 */
		dev->irq = nile4_to_irq(NILE4_INT_UART);
		dev->resource[0].start = PHYSADDR(NILE4_BASE);
		dev->resource[0].end = dev->resource[0].start+NILE4_SIZE-1;
		dev->resource[0].flags = IORESOURCE_MEM |
					 PCI_BASE_ADDRESS_MEM_TYPE_64;
		
		break;
	    case 10:
		printk("[onboard] Acer Labs M7101 PMU\n");
		pci_pmu = dev;
		/* Program the lines for LEDs D2 and D3 to output */
		nile4_pci_read_config_byte(dev, 0x7d, &t8);
		t8 |= 0xc0;
		nile4_pci_write_config_byte(dev, 0x7d, t8);
		/* Turn LEDs D2 and D3 off */
		nile4_pci_read_config_byte(dev, 0x7e, &t8);
		t8 |= 0xc0;
		nile4_pci_write_config_byte(dev, 0x7e, t8);
		break;
	    case 13:
		printk("[onboard] Acer Labs M5237 USB\n");
		dev->irq = nile4_to_irq(NILE4_INT_INTE);
		dev->resource[0].start = 0x1001000;
		dev->resource[0].end = dev->resource[0].start+0xfff;
		nile4_pci_write_config_dword(dev, PCI_BASE_ADDRESS_0,
					     dev->resource[0].start);
		break;
	    default:
		printk("\n");
		break;
	}
    }
}

char *pcibios_setup (char *str)
{
    return str;
}

void __init pcibios_update_resource(struct pci_dev *dev, struct resource *root,
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

void __init pcibios_update_irq(struct pci_dev *dev, int irq)
{
    pci_write_config_byte(dev, PCI_INTERRUPT_LINE, irq);
}

void __init pcibios_fixup_pbus_ranges(struct pci_bus *bus,
				      struct pbus_set_ranges_data *ranges)
{
    ranges->io_start -= bus->resource[0]->start;
    ranges->io_end -= bus->resource[0]->start;
    ranges->mem_start -= bus->resource[1]->start;
    ranges->mem_end -= bus->resource[1]->start;
}

int __init pcibios_enable_device(struct pci_dev *dev)
{
    printk("pcibios_enable_device for %04x:%04x\n", dev->vendor, dev->device);
    panic("pcibios_enable_device: not yet implemented\n");
}

void __init pcibios_align_resource(void *data, struct resource *res,
				   unsigned long size)
{}

struct pci_fixup pcibios_fixups[] = {};

