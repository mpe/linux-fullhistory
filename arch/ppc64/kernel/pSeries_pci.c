/*
 * pSeries_pci.c
 *
 * Copyright (C) 2001 Dave Engebretsen, IBM Corporation
 * Copyright (C) 2003 Anton Blanchard <anton@au.ibm.com>, IBM
 *
 * pSeries specific routines for PCI.
 * 
 * Based on code from pci.c and chrp_pci.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *    
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/kernel.h>
#include <linux/threads.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/bootmem.h>

#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <asm/iommu.h>
#include <asm/rtas.h>

#include "mpic.h"
#include "pci.h"

/* RTAS tokens */
static int read_pci_config;
static int write_pci_config;
static int ibm_read_pci_config;
static int ibm_write_pci_config;

static int s7a_workaround;

extern struct mpic *pSeries_mpic;

static int rtas_read_config(struct device_node *dn, int where, int size, u32 *val)
{
	int returnval = -1;
	unsigned long buid, addr;
	int ret;

	if (!dn)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if (where & (size - 1))
		return PCIBIOS_BAD_REGISTER_NUMBER;

	addr = (dn->busno << 16) | (dn->devfn << 8) | where;
	buid = dn->phb->buid;
	if (buid) {
		ret = rtas_call(ibm_read_pci_config, 4, 2, &returnval,
				addr, buid >> 32, buid & 0xffffffff, size);
	} else {
		ret = rtas_call(read_pci_config, 2, 2, &returnval, addr, size);
	}
	*val = returnval;

	if (ret)
		return PCIBIOS_DEVICE_NOT_FOUND;

	if (returnval == EEH_IO_ERROR_VALUE(size)
	    && eeh_dn_check_failure (dn, NULL))
		return PCIBIOS_DEVICE_NOT_FOUND;

	return PCIBIOS_SUCCESSFUL;
}

static int rtas_pci_read_config(struct pci_bus *bus,
				unsigned int devfn,
				int where, int size, u32 *val)
{
	struct device_node *busdn, *dn;

	if (bus->self)
		busdn = pci_device_to_OF_node(bus->self);
	else
		busdn = bus->sysdata;	/* must be a phb */

	/* Search only direct children of the bus */
	for (dn = busdn->child; dn; dn = dn->sibling)
		if (dn->devfn == devfn)
			return rtas_read_config(dn, where, size, val);
	return PCIBIOS_DEVICE_NOT_FOUND;
}

static int rtas_write_config(struct device_node *dn, int where, int size, u32 val)
{
	unsigned long buid, addr;
	int ret;

	if (!dn)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if (where & (size - 1))
		return PCIBIOS_BAD_REGISTER_NUMBER;

	addr = (dn->busno << 16) | (dn->devfn << 8) | where;
	buid = dn->phb->buid;
	if (buid) {
		ret = rtas_call(ibm_write_pci_config, 5, 1, NULL, addr, buid >> 32, buid & 0xffffffff, size, (ulong) val);
	} else {
		ret = rtas_call(write_pci_config, 3, 1, NULL, addr, size, (ulong)val);
	}

	if (ret)
		return PCIBIOS_DEVICE_NOT_FOUND;

	return PCIBIOS_SUCCESSFUL;
}

static int rtas_pci_write_config(struct pci_bus *bus,
				 unsigned int devfn,
				 int where, int size, u32 val)
{
	struct device_node *busdn, *dn;

	if (bus->self)
		busdn = pci_device_to_OF_node(bus->self);
	else
		busdn = bus->sysdata;	/* must be a phb */

	/* Search only direct children of the bus */
	for (dn = busdn->child; dn; dn = dn->sibling)
		if (dn->devfn == devfn)
			return rtas_write_config(dn, where, size, val);
	return PCIBIOS_DEVICE_NOT_FOUND;
}

struct pci_ops rtas_pci_ops = {
	rtas_pci_read_config,
	rtas_pci_write_config
};

int is_python(struct device_node *dev)
{
	char *model = (char *)get_property(dev, "model", NULL);

	if (model && strstr(model, "Python"))
		return 1;

	return 0;
}

static int get_phb_reg_prop(struct device_node *dev,
			    unsigned int addr_size_words,
			    struct reg_property64 *reg)
{
	unsigned int *ui_ptr = NULL, len;

	/* Found a PHB, now figure out where his registers are mapped. */
	ui_ptr = (unsigned int *)get_property(dev, "reg", &len);
	if (ui_ptr == NULL)
		return 1;

	if (addr_size_words == 1) {
		reg->address = ((struct reg_property32 *)ui_ptr)->address;
		reg->size    = ((struct reg_property32 *)ui_ptr)->size;
	} else {
		*reg = *((struct reg_property64 *)ui_ptr);
	}

	return 0;
}

static void python_countermeasures(struct device_node *dev,
				   unsigned int addr_size_words)
{
	struct reg_property64 reg_struct;
	void __iomem *chip_regs;
	volatile u32 val;

	if (get_phb_reg_prop(dev, addr_size_words, &reg_struct))
		return;

	/* Python's register file is 1 MB in size. */
	chip_regs = ioremap(reg_struct.address & ~(0xfffffUL), 0x100000);

	/* 
	 * Firmware doesn't always clear this bit which is critical
	 * for good performance - Anton
	 */

#define PRG_CL_RESET_VALID 0x00010000

	val = in_be32(chip_regs + 0xf6030);
	if (val & PRG_CL_RESET_VALID) {
		printk(KERN_INFO "Python workaround: ");
		val &= ~PRG_CL_RESET_VALID;
		out_be32(chip_regs + 0xf6030, val);
		/*
		 * We must read it back for changes to
		 * take effect
		 */
		val = in_be32(chip_regs + 0xf6030);
		printk("reg0: %x\n", val);
	}

	iounmap(chip_regs);
}

void __init init_pci_config_tokens (void)
{
	read_pci_config = rtas_token("read-pci-config");
	write_pci_config = rtas_token("write-pci-config");
	ibm_read_pci_config = rtas_token("ibm,read-pci-config");
	ibm_write_pci_config = rtas_token("ibm,write-pci-config");
}

unsigned long __devinit get_phb_buid (struct device_node *phb)
{
	int addr_cells;
	unsigned int *buid_vals;
	unsigned int len;
	unsigned long buid;

	if (ibm_read_pci_config == -1) return 0;

	/* PHB's will always be children of the root node,
	 * or so it is promised by the current firmware. */
	if (phb->parent == NULL)
		return 0;
	if (phb->parent->parent)
		return 0;

	buid_vals = (unsigned int *) get_property(phb, "reg", &len);
	if (buid_vals == NULL)
		return 0;

	addr_cells = prom_n_addr_cells(phb);
	if (addr_cells == 1) {
		buid = (unsigned long) buid_vals[0];
	} else {
		buid = (((unsigned long)buid_vals[0]) << 32UL) |
			(((unsigned long)buid_vals[1]) & 0xffffffff);
	}
	return buid;
}

static int phb_set_bus_ranges(struct device_node *dev,
			      struct pci_controller *phb)
{
	int *bus_range;
	unsigned int len;

	bus_range = (int *) get_property(dev, "bus-range", &len);
	if (bus_range == NULL || len < 2 * sizeof(int)) {
		return 1;
 	}
 
	phb->first_busno =  bus_range[0];
	phb->last_busno  =  bus_range[1];

	return 0;
}

static int __devinit setup_phb(struct device_node *dev,
			       struct pci_controller *phb,
			       unsigned int addr_size_words)
{
	pci_setup_pci_controller(phb);

	if (is_python(dev))
		python_countermeasures(dev, addr_size_words);

	if (phb_set_bus_ranges(dev, phb))
		return 1;

	phb->arch_data = dev;
	phb->ops = &rtas_pci_ops;
	phb->buid = get_phb_buid(dev);

	return 0;
}

static void __devinit add_linux_pci_domain(struct device_node *dev,
					   struct pci_controller *phb,
					   struct property *of_prop)
{
	memset(of_prop, 0, sizeof(struct property));
	of_prop->name = "linux,pci-domain";
	of_prop->length = sizeof(phb->global_number);
	of_prop->value = (unsigned char *)&of_prop[1];
	memcpy(of_prop->value, &phb->global_number, sizeof(phb->global_number));
	prom_add_property(dev, of_prop);
}

static struct pci_controller * __init alloc_phb(struct device_node *dev,
						unsigned int addr_size_words)
{
	struct pci_controller *phb;
	struct property *of_prop;

	phb = alloc_bootmem(sizeof(struct pci_controller));
	if (phb == NULL)
		return NULL;

	of_prop = alloc_bootmem(sizeof(struct property) +
				sizeof(phb->global_number));
	if (!of_prop)
		return NULL;

	if (setup_phb(dev, phb, addr_size_words))
		return NULL;

	add_linux_pci_domain(dev, phb, of_prop);

	return phb;
}

static struct pci_controller * __devinit alloc_phb_dynamic(struct device_node *dev, unsigned int addr_size_words)
{
	struct pci_controller *phb;

	phb = (struct pci_controller *)kmalloc(sizeof(struct pci_controller),
					       GFP_KERNEL);
	if (phb == NULL)
		return NULL;

	if (setup_phb(dev, phb, addr_size_words))
		return NULL;

	phb->is_dynamic = 1;

	/* TODO: linux,pci-domain? */

 	return phb;
}

unsigned long __init find_and_init_phbs(void)
{
	struct device_node *node;
	struct pci_controller *phb;
	unsigned int root_size_cells = 0;
	unsigned int index;
	unsigned int *opprop = NULL;
	struct device_node *root = of_find_node_by_path("/");

	if (ppc64_interrupt_controller == IC_OPEN_PIC) {
		opprop = (unsigned int *)get_property(root,
				"platform-open-pic", NULL);
	}

	root_size_cells = prom_n_size_cells(root);

	index = 0;

	for (node = of_get_next_child(root, NULL);
	     node != NULL;
	     node = of_get_next_child(root, node)) {
		if (node->type == NULL || strcmp(node->type, "pci") != 0)
			continue;

		phb = alloc_phb(node, root_size_cells);
		if (!phb)
			continue;

		pci_process_bridge_OF_ranges(phb, node);
		pci_setup_phb_io(phb, index == 0);

		if (ppc64_interrupt_controller == IC_OPEN_PIC && pSeries_mpic) {
			int addr = root_size_cells * (index + 2) - 1;
			mpic_assign_isu(pSeries_mpic, index, opprop[addr]);
		}

		index++;
	}

	of_node_put(root);
	pci_devs_phb_init();

	/*
	 * pci_probe_only and pci_assign_all_buses can be set via properties
	 * in chosen.
	 */
	if (of_chosen) {
		int *prop;

		prop = (int *)get_property(of_chosen, "linux,pci-probe-only",
					   NULL);
		if (prop)
			pci_probe_only = *prop;

		prop = (int *)get_property(of_chosen,
					   "linux,pci-assign-all-buses", NULL);
		if (prop)
			pci_assign_all_buses = *prop;
	}

	return 0;
}

struct pci_controller * __devinit init_phb_dynamic(struct device_node *dn)
{
	struct device_node *root = of_find_node_by_path("/");
	unsigned int root_size_cells = 0;
	struct pci_controller *phb;
	struct pci_bus *bus;

	root_size_cells = prom_n_size_cells(root);

	phb = alloc_phb_dynamic(dn, root_size_cells);
	if (!phb)
		return NULL;

	pci_process_bridge_OF_ranges(phb, dn);

	pci_setup_phb_io_dynamic(phb);
	of_node_put(root);

	pci_devs_phb_init_dynamic(phb);
	phb->last_busno = 0xff;
	bus = pci_scan_bus(phb->first_busno, phb->ops, phb->arch_data);
	phb->bus = bus;
	phb->last_busno = bus->subordinate;

	return phb;
}
EXPORT_SYMBOL(init_phb_dynamic);

#if 0
void pcibios_name_device(struct pci_dev *dev)
{
	struct device_node *dn;

	/*
	 * Add IBM loc code (slot) as a prefix to the device names for service
	 */
	dn = pci_device_to_OF_node(dev);
	if (dn) {
		char *loc_code = get_property(dn, "ibm,loc-code", 0);
		if (loc_code) {
			int loc_len = strlen(loc_code);
			if (loc_len < sizeof(dev->dev.name)) {
				memmove(dev->dev.name+loc_len+1, dev->dev.name,
					sizeof(dev->dev.name)-loc_len-1);
				memcpy(dev->dev.name, loc_code, loc_len);
				dev->dev.name[loc_len] = ' ';
				dev->dev.name[sizeof(dev->dev.name)-1] = '\0';
			}
		}
	}
}   
DECLARE_PCI_FIXUP_HEADER(PCI_ANY_ID, PCI_ANY_ID, pcibios_name_device);
#endif

static void check_s7a(void)
{
	struct device_node *root;
	char *model;

	root = of_find_node_by_path("/");
	if (root) {
		model = get_property(root, "model", NULL);
		if (model && !strcmp(model, "IBM,7013-S7A"))
			s7a_workaround = 1;
		of_node_put(root);
	}
}

/* RPA-specific bits for removing PHBs */
int pcibios_remove_root_bus(struct pci_controller *phb)
{
	struct pci_bus *b = phb->bus;
	struct resource *res;
	int rc, i;

	res = b->resource[0];
	if (!res->flags) {
		printk(KERN_ERR "%s: no IO resource for PHB %s\n", __FUNCTION__,
				b->name);
		return 1;
	}

	rc = unmap_bus_range(b);
	if (rc) {
		printk(KERN_ERR "%s: failed to unmap IO on bus %s\n",
			__FUNCTION__, b->name);
		return 1;
	}

	if (release_resource(res)) {
		printk(KERN_ERR "%s: failed to release IO on bus %s\n",
				__FUNCTION__, b->name);
		return 1;
	}

	for (i = 1; i < 3; ++i) {
		res = b->resource[i];
		if (!res->flags && i == 0) {
			printk(KERN_ERR "%s: no MEM resource for PHB %s\n",
				__FUNCTION__, b->name);
			return 1;
		}
		if (res->flags && release_resource(res)) {
			printk(KERN_ERR
			       "%s: failed to release IO %d on bus %s\n",
				__FUNCTION__, i, b->name);
			return 1;
		}
	}

	list_del(&phb->list_node);
	if (phb->is_dynamic)
		kfree(phb);

	return 0;
}
EXPORT_SYMBOL(pcibios_remove_root_bus);

static void __init pSeries_request_regions(void)
{
	request_region(0x20,0x20,"pic1");
	request_region(0xa0,0x20,"pic2");
	request_region(0x00,0x20,"dma1");
	request_region(0x40,0x20,"timer");
	request_region(0x80,0x10,"dma page reg");
	request_region(0xc0,0x20,"dma2");
}

void __init pSeries_final_fixup(void)
{
	struct pci_dev *dev = NULL;

	check_s7a();

	for_each_pci_dev(dev) {
		pci_read_irq_line(dev);
		if (s7a_workaround) {
			if (dev->irq > 16) {
				dev->irq -= 3;
				pci_write_config_byte(dev, PCI_INTERRUPT_LINE, dev->irq);
			}
		}
	}

	phbs_remap_io();
	pSeries_request_regions();

	pci_addr_cache_build();
}

/*
 * Assume the winbond 82c105 is the IDE controller on a
 * p610.  We should probably be more careful in case
 * someone tries to plug in a similar adapter.
 */
static void fixup_winbond_82c105(struct pci_dev* dev)
{
	int i;
	unsigned int reg;

	if (!(systemcfg->platform & PLATFORM_PSERIES))
		return;

	printk("Using INTC for W82c105 IDE controller.\n");
	pci_read_config_dword(dev, 0x40, &reg);
	/* Enable LEGIRQ to use INTC instead of ISA interrupts */
	pci_write_config_dword(dev, 0x40, reg | (1<<11));

	for (i = 0; i < DEVICE_COUNT_RESOURCE; ++i) {
		/* zap the 2nd function of the winbond chip */
		if (dev->resource[i].flags & IORESOURCE_IO
		    && dev->bus->number == 0 && dev->devfn == 0x81)
			dev->resource[i].flags &= ~IORESOURCE_IO;
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_WINBOND, PCI_DEVICE_ID_WINBOND_82C105,
			 fixup_winbond_82c105);
