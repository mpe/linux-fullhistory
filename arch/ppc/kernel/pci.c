/*
 * $Id: pci.c,v 1.64 1999/09/17 18:01:53 cort Exp $
 * Common pmac/prep/chrp pci routines. -- Cort
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/openpic.h>
#include <linux/capability.h>
#include <linux/sched.h>
#include <linux/errno.h>

#include <asm/processor.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <asm/residual.h>
#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/gg2.h>
#include <asm/uaccess.h>

#include "pci.h"

static void __init pcibios_claim_resources(struct list_head *);

unsigned long isa_io_base     = 0;
unsigned long isa_mem_base    = 0;
unsigned long pci_dram_offset = 0;

struct pci_fixup pcibios_fixups[] = {
	{ 0 }
};

int generic_pcibios_read_byte(struct pci_dev *dev, int where, u8 *val)
{
	return ppc_md.pcibios_read_config_byte(dev->bus->number,dev->devfn,where,val);
}
int generic_pcibios_read_word(struct pci_dev *dev, int where, u16 *val)
{
	return ppc_md.pcibios_read_config_word(dev->bus->number,dev->devfn,where,val);
}
int generic_pcibios_read_dword(struct pci_dev *dev, int where, u32 *val)
{
	return ppc_md.pcibios_read_config_dword(dev->bus->number,dev->devfn,where,val);
}
int generic_pcibios_write_byte(struct pci_dev *dev, int where, u8 val)
{
	return ppc_md.pcibios_write_config_byte(dev->bus->number,dev->devfn,where,val);
}
int generic_pcibios_write_word(struct pci_dev *dev, int where, u16 val)
{
	return ppc_md.pcibios_write_config_word(dev->bus->number,dev->devfn,where,val);
}
int generic_pcibios_write_dword(struct pci_dev *dev, int where, u32 val)
{
	return ppc_md.pcibios_write_config_dword(dev->bus->number,dev->devfn,where,val);
}

struct pci_ops generic_pci_ops = 
{
	generic_pcibios_read_byte,
	generic_pcibios_read_word,
	generic_pcibios_read_dword,
	generic_pcibios_write_byte,
	generic_pcibios_write_word,
	generic_pcibios_write_dword
};

void __init pcibios_init(void)
{
	printk("PCI: Probing PCI hardware\n");
	pci_scan_bus(0, &generic_pci_ops, NULL);
	if (ppc_md.pcibios_fixup)
		ppc_md.pcibios_fixup();
	pcibios_claim_resources(&pci_root_buses);
}

void __init
pcibios_fixup_pbus_ranges(struct pci_bus * bus, struct pbus_set_ranges_data * ranges)
{
	ranges->io_start -= bus->resource[0]->start;
	ranges->io_end -= bus->resource[0]->start;
	ranges->mem_start -= bus->resource[1]->start;
	ranges->mem_end -= bus->resource[1]->start;
}

unsigned long resource_fixup(struct pci_dev * dev, struct resource * res,
			     unsigned long start, unsigned long size)
{
	return start;
}

static void __init pcibios_claim_resources(struct list_head *bus_list)
{
	struct list_head *ln, *dn;
	struct pci_bus *bus;
	struct pci_dev *dev;
	int idx;

	for (ln=bus_list->next; ln != bus_list; ln=ln->next) {
		bus = pci_bus_b(ln);
		for (dn=bus->devices.next; dn != &bus->devices; dn=dn->next) {
			dev = pci_dev_b(dn);
			for (idx = 0; idx < PCI_NUM_RESOURCES; idx++)
			{
				struct resource *r = &dev->resource[idx];
				struct resource *pr;
				if (!r->start)
					continue;
				pr = pci_find_parent_resource(dev, r);
				if (!pr || request_resource(pr, r) < 0)
				{
					printk(KERN_ERR "PCI: Address space collision on region %d of device %s\n", idx, dev->name);
					/* We probably should disable the region, shouldn't we? */
				}
			}
		}
		pcibios_claim_resources(&bus->children);
	}
}

void __init pcibios_fixup_bus(struct pci_bus *bus)
{
	if ( ppc_md.pcibios_fixup_bus )
		ppc_md.pcibios_fixup_bus(bus);
}

char __init *pcibios_setup(char *str)
{
	return str;
}

/* the next two are stolen from the alpha port... */
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
pcibios_update_irq(struct pci_dev *dev, int irq)
{
	pci_write_config_byte(dev, PCI_INTERRUPT_LINE, irq);
	/* XXX FIXME - update OF device tree node interrupt property */
}

void __init
pcibios_align_resource(void *data, struct resource *res, unsigned long size)
{
}

int pcibios_enable_device(struct pci_dev *dev)
{
	u16 cmd, old_cmd;
	int idx;
	struct resource *r;

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	old_cmd = cmd;
	for (idx=0; idx<6; idx++) {
		r = &dev->resource[idx];
		if (!r->start && r->end) {
			printk(KERN_ERR "PCI: Device %s not available because of resource collisions\n", dev->slot_name);
			return -EINVAL;
		}
		if (r->flags & IORESOURCE_IO)
			cmd |= PCI_COMMAND_IO;
		if (r->flags & IORESOURCE_MEM)
			cmd |= PCI_COMMAND_MEMORY;
	}
	if (cmd != old_cmd) {
		printk("PCI: Enabling device %s (%04x -> %04x)\n",
		       dev->slot_name, old_cmd, cmd);
		pci_write_config_word(dev, PCI_COMMAND, cmd);
	}
	return 0;
}

/*
 * Those syscalls are derived from the Alpha versions, they
 * allow userland apps to retreive the per-device iobase and
 * mem-base. They also provide wrapper for userland to do
 * config space accesses.
 * The "host_number" returns the number of the Uni-N sub bridge
 */

asmlinkage int
sys_pciconfig_read(unsigned long bus, unsigned long dfn,
		   unsigned long off, unsigned long len,
		   unsigned char *buf)
{
	unsigned char ubyte;
	unsigned short ushort;
	unsigned int uint;
	long err = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (!pcibios_present())
		return -ENOSYS;
	
	switch (len) {
	case 1:
		err = pcibios_read_config_byte(bus, dfn, off, &ubyte);
		put_user(ubyte, buf);
		break;
	case 2:
		err = pcibios_read_config_word(bus, dfn, off, &ushort);
		put_user(ushort, (unsigned short *)buf);
		break;
	case 4:
		err = pcibios_read_config_dword(bus, dfn, off, &uint);
		put_user(uint, (unsigned int *)buf);
		break;
	default:
		err = -EINVAL;
		break;
	}
	return err;
}

asmlinkage int
sys_pciconfig_write(unsigned long bus, unsigned long dfn,
		    unsigned long off, unsigned long len,
		    unsigned char *buf)
{
	unsigned char ubyte;
	unsigned short ushort;
	unsigned int uint;
	long err = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (!pcibios_present())
		return -ENOSYS;

	switch (len) {
	case 1:
		err = get_user(ubyte, buf);
		if (err)
			break;
		err = pcibios_write_config_byte(bus, dfn, off, ubyte);
		if (err != PCIBIOS_SUCCESSFUL) {
			err = -EFAULT;
		}
		break;
	case 2:
		err = get_user(ushort, (unsigned short *)buf);
		if (err)
			break;
		err = pcibios_write_config_word(bus, dfn, off, ushort);
		if (err != PCIBIOS_SUCCESSFUL) {
			err = -EFAULT;
		}
		break;
	case 4:
		err = get_user(uint, (unsigned int *)buf);
		if (err)
			break;
		err = pcibios_write_config_dword(bus, dfn, off, uint);
		if (err != PCIBIOS_SUCCESSFUL) {
			err = -EFAULT;
		}
		break;
	default:
		err = -EINVAL;
		break;
	}
	return err;
}

void *
pci_dev_io_base(unsigned char bus, unsigned char devfn)
{
	/* Defaults to old way */
	if (!ppc_md.pci_dev_io_base)
		return pci_io_base(bus);
	return ppc_md.pci_dev_io_base(bus, devfn);
}

void *
pci_dev_mem_base(unsigned char bus, unsigned char devfn)
{
	/* Default memory base is 0 (1:1 mapping) */
	if (!ppc_md.pci_dev_mem_base)
		return 0;
	return ppc_md.pci_dev_mem_base(bus, devfn);
}

/* Returns the root-bridge number (Uni-N number) of a device */
int
pci_dev_root_bridge(unsigned char bus, unsigned char devfn)
{
	/* Defaults to 0 */
	if (!ppc_md.pci_dev_root_bridge)
		return 0;
	return ppc_md.pci_dev_root_bridge(bus, devfn);
}

/* Provide information on locations of various I/O regions in physical
 * memory.  Do this on a per-card basis so that we choose the right
 * root bridge.
 * Note that the returned IO or memory base is a physical address
 */

asmlinkage long
sys_pciconfig_iobase(long which, unsigned long bus, unsigned long devfn)
{
	switch (which) {
	case IOBASE_BRIDGE_NUMBER:
		return (long)pci_dev_root_bridge(bus, devfn);
	case IOBASE_MEMORY:
		return (long)pci_dev_mem_base(bus, devfn);
	case IOBASE_IO:
		return (long)pci_dev_io_base(bus, devfn);
	}

	return -EOPNOTSUPP;
}

