/*
 * $Id: pci.c,v 1.54 1999/03/18 04:16:04 cort Exp $
 * Common pmac/prep/chrp pci routines. -- Cort
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/config.h>
#include <linux/pci.h>
#include <linux/openpic.h>

#include <asm/processor.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <asm/residual.h>
#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/gg2.h>

#include "pci.h"

unsigned long isa_io_base     = 0;
unsigned long isa_mem_base    = 0;
unsigned long pci_dram_offset = 0;

int pcibios_read_config_byte(unsigned char bus, unsigned char dev_fn,
			     unsigned char offset, unsigned char *val)
{
	return ppc_md.pcibios_read_config_byte(bus,dev_fn,offset,val);
}
int pcibios_read_config_word(unsigned char bus, unsigned char dev_fn,
			     unsigned char offset, unsigned short *val)
{
	return ppc_md.pcibios_read_config_word(bus,dev_fn,offset,val);
}
int pcibios_read_config_dword(unsigned char bus, unsigned char dev_fn,
			      unsigned char offset, unsigned int *val)
{
	return ppc_md.pcibios_read_config_dword(bus,dev_fn,offset,val);
}
int pcibios_write_config_byte(unsigned char bus, unsigned char dev_fn,
			      unsigned char offset, unsigned char val)
{
	return ppc_md.pcibios_write_config_byte(bus,dev_fn,offset,val);
}
int pcibios_write_config_word(unsigned char bus, unsigned char dev_fn,
			      unsigned char offset, unsigned short val)
{
	return ppc_md.pcibios_write_config_word(bus,dev_fn,offset,val);
}
int pcibios_write_config_dword(unsigned char bus, unsigned char dev_fn,
			       unsigned char offset, unsigned int val)
{
	return ppc_md.pcibios_write_config_dword(bus,dev_fn,offset,val);
}

int pcibios_present(void)
{
	return 1;
}

void __init pcibios_init(void)
{
}


void __init pcibios_fixup(void)
{
	ppc_md.pcibios_fixup();
}

void __init pcibios_fixup_bus(struct pci_bus *bus)
{
}

char __init *pcibios_setup(char *str)
{
	return str;
}

#ifndef CONFIG_MBX
/* Recursively searches any node that is of type PCI-PCI bridge. Without
 * this, the old code would miss children of P2P bridges and hence not
 * fix IRQ's for cards located behind P2P bridges.
 * - Ranjit Deshpande, 01/20/99
 */
void __init fix_intr(struct device_node *node, struct pci_dev *dev)
{
	unsigned int *reg, *class_code;

	for (; node != 0;node = node->sibling) {
		class_code = (unsigned int *) get_property(node, "class-code", 0);
		if((*class_code >> 8) == PCI_CLASS_BRIDGE_PCI)
			fix_intr(node->child, dev);
		reg = (unsigned int *) get_property(node, "reg", 0);
		if (reg == 0 || ((reg[0] >> 8) & 0xff) != dev->devfn)
			continue;
		/* this is the node, see if it has interrupts */
		if (node->n_intrs > 0) 
			dev->irq = node->intrs[0].line;
		break;
	}
}
#endif
