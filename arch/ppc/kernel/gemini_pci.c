#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/malloc.h>

#include <asm/machdep.h>
#include <asm/gemini.h>
#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/uaccess.h>

#include "pci.h"

#define pci_config_addr(bus,dev,offset) \
        (0x80000000 | (bus<<16) | (dev<<8) | offset)


int
gemini_pcibios_read_config_byte(unsigned char bus, unsigned char dev,
                                unsigned char offset, unsigned char *val)
{
	unsigned long reg;
	reg = grackle_read( pci_config_addr( bus, dev, (offset & ~(0x3))));
	*val = ((reg >> ((offset & 0x3) << 3)) & 0xff);
	return PCIBIOS_SUCCESSFUL;
}

int
gemini_pcibios_read_config_word(unsigned char bus, unsigned char dev,
                                unsigned char offset, unsigned short *val)
{
	unsigned long reg;
	reg = grackle_read( pci_config_addr( bus, dev, (offset & ~(0x3))));
	*val = ((reg >> ((offset & 0x3) << 3)) & 0xffff);
	return PCIBIOS_SUCCESSFUL;
}

int
gemini_pcibios_read_config_dword(unsigned char bus, unsigned char dev,
                                 unsigned char offset, unsigned int *val)
{
	*val = grackle_read( pci_config_addr( bus, dev, (offset & ~(0x3))));
	return PCIBIOS_SUCCESSFUL;
}

int
gemini_pcibios_write_config_byte(unsigned char bus, unsigned char dev,
                                 unsigned char offset, unsigned char val)
{
	unsigned long reg;
	int shifts = offset & 0x3;

	reg = grackle_read( pci_config_addr( bus, dev, (offset & ~(0x3))));
	reg = (reg & ~(0xff << (shifts << 3))) | (val << (shifts << 3));
	grackle_write( pci_config_addr( bus, dev, (offset & ~(0x3))), reg );
	return PCIBIOS_SUCCESSFUL;
}

int
gemini_pcibios_write_config_word(unsigned char bus, unsigned char dev,
                                 unsigned char offset, unsigned short val)
{
	unsigned long reg;
	int shifts = offset & 0x3;

	reg = grackle_read( pci_config_addr( bus, dev, (offset & ~(0x3))));
	reg = (reg & ~(0xffff << (shifts << 3))) | (val << (shifts << 3));
	grackle_write( pci_config_addr( bus, dev, (offset & ~(0x3))), reg );
	return PCIBIOS_SUCCESSFUL;
}

int
gemini_pcibios_write_config_dword(unsigned char bus, unsigned char dev,
                                  unsigned char offset, unsigned int val)
{
	grackle_write( pci_config_addr( bus, dev, (offset & ~(0x3))), val );
	return PCIBIOS_SUCCESSFUL;
}

struct gemini_device {
	unsigned short vendor, device;
	unsigned char irq;
	unsigned short cmd;
	unsigned char cache_line, latency;
	void (*init)(struct pci_dev *dev);
};

static struct gemini_device gemini_map[] = {
	{ PCI_VENDOR_ID_NCR, PCI_DEVICE_ID_NCR_53C885, 11, 0x15, 32, 248, NULL },
	{ PCI_VENDOR_ID_NCR, 0x701, 10, 0, 0, 0, NULL },
	{ PCI_VENDOR_ID_TUNDRA, PCI_DEVICE_ID_TUNDRA_CA91C042, 3, 0, 0, 0, NULL },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_MPIC, 0xff, 0, 0, 0, NULL },
	{ PCI_VENDOR_ID_CMD, PCI_DEVICE_ID_CMD_670, 0xff, 0, 0, 0, NULL },
	{ PCI_VENDOR_ID_MOTOROLA, PCI_DEVICE_ID_MOTOROLA_MPC106, 0xff, 0, 0, 0, NULL },
};

static int gemini_map_count = (sizeof( gemini_map ) /
                               sizeof( gemini_map[0] ));

 

/*  This just sets up the known devices on the board. */
static void __init mapin_device( struct pci_dev *dev )
{
	struct gemini_device *p;
	unsigned short cmd;
	int i;
  

	for( i=0; i < gemini_map_count; i++ ) {
		p = &(gemini_map[i]);
    
		if ( p->vendor == dev->vendor && 
		     p->device == dev->device ) {
     
			if (p->irq != 0xff) {
				pci_write_config_byte( dev, PCI_INTERRUPT_LINE, p->irq );
				dev->irq = p->irq;
			}

			if (p->cmd) {
				pci_read_config_word( dev, PCI_COMMAND, &cmd );
				pci_write_config_word( dev, PCI_COMMAND, (p->cmd|cmd));
			}

			if (p->cache_line)
				pci_write_config_byte( dev, PCI_CACHE_LINE_SIZE, p->cache_line );
 
			if (p->latency)
				pci_write_config_byte( dev, PCI_LATENCY_TIMER, p->latency );
		}
	}
}

#define KB 1024
#define MB (KB*KB)

#define ALIGN(val,align) (((val) + ((align) -1))&(~((align) -1)))
#define MAX(a,b)   (((a) > (b)) ? (a) : (b))

#define FIRST_IO_ADDR 0x10000
#define FIRST_MEM_ADDR 0x02000000

#define GEMINI_PCI_MEM_BASE (0xf0000000)
#define GEMINI_PCI_IO_BASE  (0xfe800000)

static unsigned long pci_mem_base = GEMINI_PCI_MEM_BASE;
static unsigned long pci_io_base = GEMINI_PCI_IO_BASE;

static unsigned int io_base = FIRST_IO_ADDR;
static unsigned int mem_base = FIRST_MEM_ADDR;



__init void layout_dev( struct pci_dev *dev )
{
	int i;
	struct pci_bus *bus;
	unsigned short cmd;
	unsigned int reg, base, mask, size, alignto, type;

	bus = dev->bus;

	/* make any known settings happen */
	mapin_device( dev );

	gemini_pcibios_read_config_word( bus->number, dev->devfn, PCI_COMMAND, &cmd );

	for( reg = PCI_BASE_ADDRESS_0, i=0; reg <= PCI_BASE_ADDRESS_5; reg += 4, i++ ) {
    
		/* MPIC already done */
		if (dev->vendor == PCI_VENDOR_ID_IBM &&
		    dev->device == PCI_DEVICE_ID_IBM_MPIC)
			return;

		gemini_pcibios_write_config_dword( bus->number, dev->devfn, reg, 0xffffffff );
		gemini_pcibios_read_config_dword( bus->number, dev->devfn, reg, &base );
		if (!base) {
			dev->resource[i].start = 0;
			continue;
		}

		if (base & PCI_BASE_ADDRESS_SPACE_IO) {
			cmd |= PCI_COMMAND_IO;
			base &= PCI_BASE_ADDRESS_IO_MASK;
			mask = (~base << 1) | 0x1;
			size = (mask & base) & 0xffffffff;
			alignto = MAX(0x400, size);
			base = ALIGN(io_base, alignto);
			io_base = base + size;
			gemini_pcibios_write_config_dword( bus->number, dev->devfn, reg, 
							   ((pci_io_base + base) & 0x00ffffff) | 0x1);
			dev->resource[i].start = (pci_io_base + base) | 0x1;
		}

		else {
			cmd |= PCI_COMMAND_MEMORY;
			type = base & PCI_BASE_ADDRESS_MEM_TYPE_MASK;
			mask = (~base << 1) | 0x1;
			size = (mask & base) & 0xffffffff;
			switch( type ) {

			case PCI_BASE_ADDRESS_MEM_TYPE_32:
				break;
			case PCI_BASE_ADDRESS_MEM_TYPE_64:
				printk("Warning: Ignoring 64-bit device; slot %d, function %d.\n",
				       PCI_SLOT( dev->devfn ), PCI_FUNC( dev->devfn ));
				reg += 4;
				continue;
			}

			alignto = MAX(0x1000, size);
			base = ALIGN(mem_base, alignto);
			mem_base = base + size;
			gemini_pcibios_write_config_dword( bus->number, dev->devfn,
							   reg, (pci_mem_base + base));
			dev->resource[i].start = pci_mem_base + base;
		}
	}

	if ((dev->class >> 8) == PCI_CLASS_DISPLAY_VGA)
		cmd |= PCI_COMMAND_IO;

	gemini_pcibios_write_config_word( bus->number, dev->devfn, PCI_COMMAND,
					  (cmd|PCI_COMMAND_MASTER));
}

__init void layout_bus( struct pci_bus *bus )
{
	struct pci_dev *dev;

	if (!bus->devices && !bus->children)
		return;

	io_base = ALIGN(io_base, 4*KB);
	mem_base = ALIGN(mem_base, 4*KB);

	for( dev = bus->devices; dev; dev = dev->sibling ) {
		if (((dev->class >> 16) != PCI_BASE_CLASS_BRIDGE) ||
		    ((dev->class >> 8) == PCI_CLASS_BRIDGE_OTHER))
			layout_dev( dev );
	}
}

void __init gemini_pcibios_fixup(void)
{
	unsigned long orig_mem_base, orig_io_base;
	
	orig_mem_base = pci_mem_base;
	orig_io_base = pci_io_base;

	pci_mem_base = orig_mem_base;
	pci_io_base = orig_io_base;
}

decl_config_access_method(gemini);

/* The "bootloader" for Synergy boards does none of this for us, so we need to
   lay it all out ourselves... --Dan */
void __init gemini_setup_pci_ptrs(void)
{
	set_config_access_method(gemini);
	ppc_md.pcibios_fixup = gemini_pcibios_fixup;
}
