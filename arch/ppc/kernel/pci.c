/*
 * $Id: pci.c,v 1.24 1998/02/19 21:29:49 cort Exp $
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
#include <asm/irq.h>

#if !defined(CONFIG_MACH_SPECIFIC) || defined(CONFIG_PMAC)
unsigned long isa_io_base;
#endif /* CONFIG_MACH_SPECIFIC || CONFIG_PMAC */
#if !defined(CONFIG_MACH_SPECIFIC)
unsigned long isa_mem_base;
unsigned long pci_dram_offset;
#endif /* CONFIG_MACH_SPECIFIC */

/*
 * It would be nice if we could create a include/asm/pci.h and have just
 * function ptrs for all these in there, but that isn't the case.
 * We have a function, pcibios_*() which calls the function ptr ptr_pcibios_*()
 * which has been setup by pcibios_init().  This is all to avoid a check
 * for pmac/prep every time we call one of these.  It should also make the move
 * to a include/asm/pcibios.h easier, we can drop the ptr_ on these functions
 * and create pci.h
 *   -- Cort
 */
int (*ptr_pcibios_read_config_byte)(unsigned char bus, unsigned char dev_fn,
				    unsigned char offset, unsigned char *val);
int (*ptr_pcibios_read_config_word)(unsigned char bus, unsigned char dev_fn,
				    unsigned char offset, unsigned short *val);
int (*ptr_pcibios_read_config_dword)(unsigned char bus, unsigned char dev_fn,
				     unsigned char offset, unsigned int *val);
int (*ptr_pcibios_write_config_byte)(unsigned char bus, unsigned char dev_fn,
				     unsigned char offset, unsigned char val);
int (*ptr_pcibios_write_config_word)(unsigned char bus, unsigned char dev_fn,
				     unsigned char offset, unsigned short val);
int (*ptr_pcibios_write_config_dword)(unsigned char bus, unsigned char dev_fn,
				      unsigned char offset, unsigned int val);

extern int pmac_pcibios_read_config_byte(unsigned char bus, unsigned char dev_fn,
					 unsigned char offset, unsigned char *val);
extern int pmac_pcibios_read_config_word(unsigned char bus, unsigned char dev_fn,
					 unsigned char offset, unsigned short *val);
extern int pmac_pcibios_read_config_dword(unsigned char bus, unsigned char dev_fn,
					  unsigned char offset, unsigned int *val);
extern int pmac_pcibios_write_config_byte(unsigned char bus, unsigned char dev_fn,
					  unsigned char offset, unsigned char val);
extern int pmac_pcibios_write_config_word(unsigned char bus, unsigned char dev_fn,
					  unsigned char offset, unsigned short val);
extern int pmac_pcibios_write_config_dword(unsigned char bus, unsigned char dev_fn,
					   unsigned char offset, unsigned int val);

extern int chrp_pcibios_read_config_byte(unsigned char bus, unsigned char dev_fn,
					 unsigned char offset, unsigned char *val);
extern int chrp_pcibios_read_config_word(unsigned char bus, unsigned char dev_fn,
					 unsigned char offset, unsigned short *val);
extern int chrp_pcibios_read_config_dword(unsigned char bus, unsigned char dev_fn,
					  unsigned char offset, unsigned int *val);
extern int chrp_pcibios_write_config_byte(unsigned char bus, unsigned char dev_fn,
					  unsigned char offset, unsigned char val);
extern int chrp_pcibios_write_config_word(unsigned char bus, unsigned char dev_fn,
					  unsigned char offset, unsigned short val);
extern int chrp_pcibios_write_config_dword(unsigned char bus, unsigned char dev_fn,
					   unsigned char offset, unsigned int val);

extern int prep_pcibios_read_config_byte(unsigned char bus, unsigned char dev_fn,
					 unsigned char offset, unsigned char *val);
extern int prep_pcibios_read_config_word(unsigned char bus, unsigned char dev_fn,
					 unsigned char offset, unsigned short *val);
extern int prep_pcibios_read_config_dword(unsigned char bus, unsigned char dev_fn,
					  unsigned char offset, unsigned int *val);
extern int prep_pcibios_write_config_byte(unsigned char bus, unsigned char dev_fn,
					  unsigned char offset, unsigned char val);
extern int prep_pcibios_write_config_word(unsigned char bus, unsigned char dev_fn,
					  unsigned char offset, unsigned short val);
extern int prep_pcibios_write_config_dword(unsigned char bus, unsigned char dev_fn,
					   unsigned char offset, unsigned int val);

int pcibios_read_config_byte(unsigned char bus, unsigned char dev_fn,
			     unsigned char offset, unsigned char *val)
{
	return ptr_pcibios_read_config_byte(bus,dev_fn,offset,val);
}
int pcibios_read_config_word(unsigned char bus, unsigned char dev_fn,
			     unsigned char offset, unsigned short *val)
{
	return ptr_pcibios_read_config_word(bus,dev_fn,offset,val);
}
int pcibios_read_config_dword(unsigned char bus, unsigned char dev_fn,
			      unsigned char offset, unsigned int *val)
{
	return ptr_pcibios_read_config_dword(bus,dev_fn,offset,val);
}
int pcibios_write_config_byte(unsigned char bus, unsigned char dev_fn,
			      unsigned char offset, unsigned char val)
{
	return ptr_pcibios_write_config_byte(bus,dev_fn,offset,val);
}
int pcibios_write_config_word(unsigned char bus, unsigned char dev_fn,
			      unsigned char offset, unsigned short val)
{
	return ptr_pcibios_write_config_word(bus,dev_fn,offset,val);
}
int pcibios_write_config_dword(unsigned char bus, unsigned char dev_fn,
			       unsigned char offset, unsigned int val)
{
	return ptr_pcibios_write_config_dword(bus,dev_fn,offset,val);
}

int pcibios_present(void)
{
	return 1;
}

__initfunc(unsigned long
	   pcibios_init(unsigned long mem_start,unsigned long mem_end))
{
	return mem_start;
}

__initfunc(void
	   setup_pci_ptrs(void))
{
	switch (_machine) {
	case _MACH_prep:
		ptr_pcibios_read_config_byte = prep_pcibios_read_config_byte;
		ptr_pcibios_read_config_word = prep_pcibios_read_config_word;
		ptr_pcibios_read_config_dword = prep_pcibios_read_config_dword;
		ptr_pcibios_write_config_byte = prep_pcibios_write_config_byte;
		ptr_pcibios_write_config_word = prep_pcibios_write_config_word;
		ptr_pcibios_write_config_dword = prep_pcibios_write_config_dword;
		break;
	case _MACH_Pmac:
		ptr_pcibios_read_config_byte = pmac_pcibios_read_config_byte;
		ptr_pcibios_read_config_word = pmac_pcibios_read_config_word;
		ptr_pcibios_read_config_dword = pmac_pcibios_read_config_dword;
		ptr_pcibios_write_config_byte = pmac_pcibios_write_config_byte;
		ptr_pcibios_write_config_word = pmac_pcibios_write_config_word;
		ptr_pcibios_write_config_dword = pmac_pcibios_write_config_dword;
		break;
	case _MACH_chrp:
		ptr_pcibios_read_config_byte = chrp_pcibios_read_config_byte;
		ptr_pcibios_read_config_word = chrp_pcibios_read_config_word;
		ptr_pcibios_read_config_dword = chrp_pcibios_read_config_dword;
		ptr_pcibios_write_config_byte = chrp_pcibios_write_config_byte;
		ptr_pcibios_write_config_word = chrp_pcibios_write_config_word;
		ptr_pcibios_write_config_dword = chrp_pcibios_write_config_dword;
		break;
	}
}

__initfunc(unsigned long
	   pcibios_fixup(unsigned long mem_start, unsigned long mem_end))

{
	extern route_pci_interrupts(void);
	struct pci_dev *dev;
	extern struct bridge_data **bridges;
	extern unsigned char *Motherboard_map;
	extern unsigned char *Motherboard_routes;
	
	/*
	 * FIXME: This is broken: We should not assign IRQ's to IRQless
	 *	  devices (look at PCI_INTERRUPT_PIN) and we also should
	 *	  honor the existence of multi-function devices where
	 *	  different functions have different interrupt pins. [mj]
	 */
	switch (_machine )
	{
	case _MACH_prep:
		route_pci_interrupts();
		for(dev=pci_devices; dev; dev=dev->next)
		{
			unsigned char d = PCI_SLOT(dev->devfn);
			dev->irq = Motherboard_routes[Motherboard_map[d]];
		}
		break;
	case _MACH_chrp:
		/* PCI interrupts are controlled by the OpenPIC */
		for(dev=pci_devices; dev; dev=dev->next)
			if (dev->irq)
				dev->irq = openpic_to_irq(dev->irq);
		break;
	case _MACH_Pmac:
		for(dev=pci_devices; dev; dev=dev->next)
		{
			/*
			 * Open Firmware often doesn't initialize the,
			 * PCI_INTERRUPT_LINE config register properly, so we
			 * should find the device node and se if it has an
			 * AAPL,interrupts property.
			 */
			struct bridge_data *bp = bridges[dev->bus->number];
			struct device_node *node;
			unsigned int *reg;
			unsigned char pin;
			
			if (pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &pin) ||
			    !pin)
				continue;	/* No interrupt generated -> no fixup */
			for (node = bp->node->child; node != 0;
			     node = node->sibling) {
				reg = (unsigned int *) get_property(node, "reg", 0);
				if (reg == 0 || ((reg[0] >> 8) & 0xff) != dev->devfn)
					continue;
				/* this is the node, see if it has interrupts */
				if (node->n_intrs > 0)
					dev->irq = node->intrs[0].line;
				break;
			}
		}
		break;
	}
	return mem_start;
}

__initfunc(char *pcibios_setup(char *str))
{
	return str;
}
