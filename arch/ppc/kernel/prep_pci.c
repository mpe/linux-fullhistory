/*
 * $Id: prep_pci.c,v 1.7 1997/08/23 22:46:02 cort Exp $
 * PReP pci functions.
 * Originally by Gary Thomas
 * rewritten and updated by Cort Dougan (cort@cs.nmt.edu)
 *
 * The motherboard routes/maps will disappear shortly. -- Cort
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/bios32.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/ptrace.h>
#include <asm/processor.h>

#define MAX_DEVNR 22

/* Which PCI interrupt line does a given device [slot] use? */
/* Note: This really should be two dimensional based in slot/pin used */
unsigned char *Motherboard_map;
unsigned char *Motherboard_map_name;

/* How is the 82378 PIRQ mapping setup? */
unsigned char *Motherboard_routes;

/* Tables for known hardware */   

/* Motorola PowerStack */
static char Blackhawk_pci_IRQ_map[16] =
{
  	0,	/* Slot 0  - unused */
  	0,	/* Slot 1  - unused */
  	0,	/* Slot 2  - unused */
  	0,	/* Slot 3  - unused */
  	0,	/* Slot 4  - unused */
  	0,	/* Slot 5  - unused */
  	0,	/* Slot 6  - unused */
  	0,	/* Slot 7  - unused */
  	0,	/* Slot 8  - unused */
  	0,	/* Slot 9  - unused */
  	0,	/* Slot 10 - unused */
  	0,	/* Slot 11 - unused */
  	3,	/* Slot 12 - SCSI */
  	0,	/* Slot 13 - unused */
  	1,	/* Slot 14 - Ethernet */
  	0,	/* Slot 15 - unused */
};

static char Blackhawk_pci_IRQ_routes[] =
{
   	0,	/* Line 0 - Unused */
   	9,	/* Line 1 */
   	11,	/* Line 2 */
   	14,	/* Line 3 */
   	15	/* Line 4 */
};
   
/* Motorola MVME16xx */
static char Genesis_pci_IRQ_map[16] =
{
  	0,	/* Slot 0  - unused */
  	0,	/* Slot 1  - unused */
  	0,	/* Slot 2  - unused */
  	0,	/* Slot 3  - unused */
  	0,	/* Slot 4  - unused */
  	0,	/* Slot 5  - unused */
  	0,	/* Slot 6  - unused */
  	0,	/* Slot 7  - unused */
  	0,	/* Slot 8  - unused */
  	0,	/* Slot 9  - unused */
  	0,	/* Slot 10 - unused */
  	0,	/* Slot 11 - unused */
  	3,	/* Slot 12 - SCSI */
  	0,	/* Slot 13 - unused */
  	1,	/* Slot 14 - Ethernet */
  	0,	/* Slot 15 - unused */
};

static char Genesis_pci_IRQ_routes[] =
{
   	0,	/* Line 0 - Unused */
   	10,	/* Line 1 */
   	11,	/* Line 2 */
   	14,	/* Line 3 */
   	15	/* Line 4 */
};
   
/* Motorola Series-E */
static char Comet_pci_IRQ_map[16] =
{
  	0,	/* Slot 0  - unused */
  	0,	/* Slot 1  - unused */
  	0,	/* Slot 2  - unused */
  	0,	/* Slot 3  - unused */
  	0,	/* Slot 4  - unused */
  	0,	/* Slot 5  - unused */
  	0,	/* Slot 6  - unused */
  	0,	/* Slot 7  - unused */
  	0,	/* Slot 8  - unused */
  	0,	/* Slot 9  - unused */
  	0,	/* Slot 10 - unused */
  	0,	/* Slot 11 - unused */
  	3,	/* Slot 12 - SCSI */
  	0,	/* Slot 13 - unused */
  	1,	/* Slot 14 - Ethernet */
  	0,	/* Slot 15 - unused */
};

static char Comet_pci_IRQ_routes[] =
{
   	0,	/* Line 0 - Unused */
   	10,	/* Line 1 */
   	11,	/* Line 2 */
   	14,	/* Line 3 */
   	15	/* Line 4 */
};

/*
 * ibm 830 (and 850?).
 * This is actually based on the Carolina motherboard
 * -- Cort
 */
static char ibm8xx_pci_IRQ_map[23] = {
        0, /* Slot 0  - unused */
        0, /* Slot 1  - unused */
        0, /* Slot 2  - unused */
        0, /* Slot 3  - unused */
        0, /* Slot 4  - unused */
        0, /* Slot 5  - unused */
        0, /* Slot 6  - unused */
        0, /* Slot 7  - unused */
        0, /* Slot 8  - unused */
        0, /* Slot 9  - unused */
        0, /* Slot 10 - unused */
        0, /* Slot 11 - FireCoral */
        4, /* Slot 12 - Ethernet  PCIINTD# */
        2, /* Slot 13 - PCI Slot #2 */
        2, /* Slot 14 - S3 Video PCIINTD# */
        0, /* Slot 15 - onboard SCSI (INDI) [1] */
        3, /* Slot 16 - NCR58C810 RS6000 Only PCIINTC# */
        0, /* Slot 17 - unused */
        2, /* Slot 18 - PCI Slot 2 PCIINTx# (See below) */
        0, /* Slot 19 - unused */
        0, /* Slot 20 - unused */
        0, /* Slot 21 - unused */
        2, /* Slot 22 - PCI slot 1 PCIINTx# (See below) */
};
static char ibm8xx_pci_IRQ_routes[] = {
        0,      /* Line 0 - unused */
        13,     /* Line 1 */
        10,     /* Line 2 */
        15,     /* Line 3 */
        15,     /* Line 4 */
};

/* IBM Nobis and 850 */
static char Nobis_pci_IRQ_map[23] ={
        0, /* Slot 0  - unused */
        0, /* Slot 1  - unused */
        0, /* Slot 2  - unused */
        0, /* Slot 3  - unused */
        0, /* Slot 4  - unused */
        0, /* Slot 5  - unused */
        0, /* Slot 6  - unused */
        0, /* Slot 7  - unused */
        0, /* Slot 8  - unused */
        0, /* Slot 9  - unused */
        0, /* Slot 10 - unused */
        0, /* Slot 11 - unused */
        3, /* Slot 12 - SCSI */
        0, /* Slot 13 - unused */
        0, /* Slot 14 - unused */
        0, /* Slot 15 - unused */
};

static char Nobis_pci_IRQ_routes[] = {
        0, /* Line 0 - Unused */
        13, /* Line 1 */
        13, /* Line 2 */
        13, /* Line 3 */
        13      /* Line 4 */
};

/* We have to turn on LEVEL mode for changed IRQ's */
/* All PCI IRQ's need to be level mode, so this should be something
 * other than hard-coded as well... IRQ's are individually mappable
 * to either edge or level.
 */
#define CAROLINA_IRQ_EDGE_MASK_LO   0x00  /* IRQ's 0-7  */
#define CAROLINA_IRQ_EDGE_MASK_HI   0xA4  /* IRQ's 8-15 [10,13,15] */

int
prep_pcibios_read_config_dword (unsigned char bus,
			   unsigned char dev, unsigned char offset, unsigned int *val)
{
	unsigned long _val;
	unsigned long *ptr;
	dev >>= 3;
	
	if ((bus != 0) || (dev > MAX_DEVNR))
	{
		*val = 0xFFFFFFFF;
		return PCIBIOS_DEVICE_NOT_FOUND;
	} else
	{
		ptr = (unsigned long *)(0x80800000 | (1<<dev) | offset);
		_val = le32_to_cpu(*ptr);
	}
	*val = _val;
	return PCIBIOS_SUCCESSFUL;
}

int
prep_pcibios_read_config_word (unsigned char bus,
			  unsigned char dev, unsigned char offset, unsigned short *val)
{
	unsigned short _val;
	unsigned short *ptr;
	dev >>= 3;
	if ((bus != 0) || (dev > MAX_DEVNR))
	{
		*val = (unsigned short)0xFFFFFFFF;
		return PCIBIOS_DEVICE_NOT_FOUND;
	} else
	{
		ptr = (unsigned short *)(0x80800000 | (1<<dev) | offset);
		_val = le16_to_cpu(*ptr);
	}
	*val = _val;
	return PCIBIOS_SUCCESSFUL;
}

int
prep_pcibios_read_config_byte (unsigned char bus,
			  unsigned char dev, unsigned char offset, unsigned char *val)
{
	unsigned char _val;
	volatile unsigned char *ptr;
	dev >>= 3;
	/* Note: the configuration registers don't always have this right! */
	if (offset == PCI_INTERRUPT_LINE)
	{
		*val = Motherboard_routes[Motherboard_map[dev]];
/*printk("dev %d map %d route %d on board %d\n",
  dev,Motherboard_map[dev],
  Motherboard_routes[Motherboard_map[dev]],
  *(unsigned char *)(0x80800000 | (1<<dev) | (offset ^ 1)));*/
		return PCIBIOS_SUCCESSFUL;
	}
	if ((bus != 0) || (dev > MAX_DEVNR))
	{
		*(unsigned long *)val = (unsigned long) 0xFFFFFFFF;
		return PCIBIOS_DEVICE_NOT_FOUND;
	} else
	{
		ptr = (unsigned char *)(0x80800000 | (1<<dev) | (offset ^ 1));
		_val = *ptr;
	}
	*val = _val;
	return PCIBIOS_SUCCESSFUL;
}

int
prep_pcibios_write_config_dword (unsigned char bus,
			    unsigned char dev, unsigned char offset, unsigned int val)
{
	unsigned long _val;
	unsigned long *ptr;
	dev >>= 3;
	_val = le32_to_cpu(val);
	if ((bus != 0) || (dev > MAX_DEVNR))
	{
		return PCIBIOS_DEVICE_NOT_FOUND;
	} else
	{
		ptr = (unsigned long *)(0x80800000 | (1<<dev) | offset);
		*ptr = _val;
	}
	return PCIBIOS_SUCCESSFUL;
}

int
prep_pcibios_write_config_word (unsigned char bus,
			   unsigned char dev, unsigned char offset, unsigned short val)
{
	unsigned short _val;
	unsigned short *ptr;
	dev >>= 3;
	_val = le16_to_cpu(val);
	if ((bus != 0) || (dev > MAX_DEVNR))
	{
		return PCIBIOS_DEVICE_NOT_FOUND;
	} else
	{
		ptr = (unsigned short *)(0x80800000 | (1<<dev) | offset);
		*ptr = _val;
	}
	return PCIBIOS_SUCCESSFUL;
}

int
prep_pcibios_write_config_byte (unsigned char bus,
			   unsigned char dev, unsigned char offset, unsigned char val)
{
	unsigned char _val;
	unsigned char *ptr;
	dev >>= 3;
	_val = val;
	if ((bus != 0) || (dev > MAX_DEVNR))
	{
		return PCIBIOS_DEVICE_NOT_FOUND;
	} else
	{
		ptr = (unsigned char *)(0x80800000 | (1<<dev) | (offset^1));
		*ptr = _val;
	}
	return PCIBIOS_SUCCESSFUL;
}

int prep_pcibios_find_device (unsigned short vendor, unsigned short device_id,
			   unsigned short index, unsigned char *bus,
			   unsigned char *devfn)
{
    unsigned int curr = 0;
    struct pci_dev *dev;
/*printk("pcibios_find_device(): vendor %04x devid %04x index %d\n",    
       vendor,device_id,index);*/
    for (dev = pci_devices; dev; dev = dev->next) {
/*printk("   dev->vendor %04x dev->device %04x\n",
       dev->vendor,dev->device);*/
	if (dev->vendor == vendor && dev->device == device_id) {
	    if (curr == index) {
		*devfn = dev->devfn;
		*bus = dev->bus->number;
		return PCIBIOS_SUCCESSFUL;
	    }
	    ++curr;
	}
    }
    return PCIBIOS_DEVICE_NOT_FOUND;
}

/*
 * Given the class, find the n'th instance of that device
 * in the system.
 */
int prep_pcibios_find_class (unsigned int class_code, unsigned short index,
			  unsigned char *bus, unsigned char *devfn)
{
    unsigned int curr = 0;
    struct pci_dev *dev;

    for (dev = pci_devices; dev; dev = dev->next) {
	if (dev->class == class_code) {
	    if (curr == index) {
		*devfn = dev->devfn;
		*bus = dev->bus->number;
		return PCIBIOS_SUCCESSFUL;
	    }
	    ++curr;
	}
    }
    return PCIBIOS_DEVICE_NOT_FOUND;
}

__initfunc(unsigned long route_pci_interrupts(void))
{
	unsigned char *ibc_pirq = (unsigned char *)0x80800860;
	unsigned char *ibc_pcicon = (unsigned char *)0x80800840;
	int i;
	
	if ( _machine == _MACH_Motorola)
	{ 
		switch (inb(0x800) & 0xF0)
		{
		case 0x10: /* MVME16xx */
			Motherboard_map_name = "Genesis";
			Motherboard_map = Genesis_pci_IRQ_map;
			Motherboard_routes = Genesis_pci_IRQ_routes;
			break;
		case 0x20: /* Series E */
			Motherboard_map_name = "Series E";
			Motherboard_map = Comet_pci_IRQ_map;
			Motherboard_routes = Comet_pci_IRQ_routes;
			break;
		case 0x40: /* PowerStack */
		default: /* Can't hurt, can it? */
			Motherboard_map_name = "Blackhawk (Powerstack)";
			Motherboard_map = Blackhawk_pci_IRQ_map;
			Motherboard_routes = Blackhawk_pci_IRQ_routes;
			break;
		}
	} else if ( _machine == _MACH_IBM )
	{
		unsigned char pl_id;
		
		if (inb(0x0852) == 0xFF) {
			Motherboard_map_name = "IBM 850/860 Portable\n";
			Motherboard_map = Nobis_pci_IRQ_map;
			Motherboard_routes = Nobis_pci_IRQ_routes;
		} else {
			Motherboard_map_name = "IBM 8xx (Carolina)";
			Motherboard_map = ibm8xx_pci_IRQ_map;
			Motherboard_routes = ibm8xx_pci_IRQ_routes;
		}
		/*printk("Changing IRQ mode\n");*/
		pl_id=inb(0x04d0);
		/*printk("Low mask is %#0x\n", pl_id);*/
		outb(pl_id|CAROLINA_IRQ_EDGE_MASK_LO, 0x04d0);
		
		pl_id=inb(0x04d1);
		/*printk("Hi mask is  %#0x\n", pl_id);*/
		outb(pl_id|CAROLINA_IRQ_EDGE_MASK_HI, 0x04d1);
		pl_id=inb(0x04d1);
		/*printk("Hi mask now %#0x\n", pl_id);*/
	} else
	{
		printk("No known machine pci routing!\n");
		return -1;
	}
	
	/* Set up mapping from slots */
	for (i = 1;  i <= 4;  i++)
	{
		ibc_pirq[i-1] = Motherboard_routes[i];
	}
	/* Enable PCI interrupts */
	*ibc_pcicon |= 0x20;
	return 0;
}
