/*
 * PCI support
 * -- rough emulation of "PCI BIOS" functions
 *
 * Note: these are very motherboard specific!  Some way needs to
 * be worked out to handle the differences.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/bios32.h>
#include <linux/pci.h>

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/ptrace.h>
#include <asm/processor.h>

/*
 * PCI interrupt configuration.  This is motherboard specific.
 */
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

/* BeBox */
static char BeBox_pci_IRQ_map[16] =
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
  	16,	/* Slot 12 - SCSI */
  	0,	/* Slot 13 - unused */
  	0,	/* Slot 14 - unused */
  	0,	/* Slot 15 - unused */
};

static char BeBox_pci_IRQ_routes[] =
{
   	0,	/* Line 0 - Unused */
   	9,	/* Line 1 */
   	11,	/* Line 2 */
   	14,	/* Line 3 */
   	15	/* Line 4 */
};

/* IBM Nobis */
static char Nobis_pci_IRQ_map[16] =
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
  	0,	/* Slot 14 - unused */
  	0,	/* Slot 15 - unused */
};

static char Nobis_pci_IRQ_routes[] =
{
   	0,	/* Line 0 - Unused */
   	13,	/* Line 1 */
   	13,	/* Line 2 */
   	13,	/* Line 3 */
   	13      /* Line 4 */
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
/* This just changes the PCI slots & onboard SCSI + S3 to IRQ10, but
 * it really needs some logic to set them to unique IRQ's, or even
 * add some logic to the drivers to ask an irq.c routine to re-map
 * the IRQ if it needs one to itself...
 */
static char Carolina_PIRQ_routes[] = {
   0xad,	/* INTB# = 10, INTA# = 13 */
   0xff		/* INTD# = 15, INTC# = 15 */
};
/* We have to turn on LEVEL mode for changed IRQ's */
/* All PCI IRQ's need to be level mode, so this should be something
 * other than hard-coded as well... IRQ's are individually mappable
 * to either edge or level.
 */
#define CAROLINA_IRQ_EDGE_MASK_LO   0x00  /* IRQ's 0-7  */
#define CAROLINA_IRQ_EDGE_MASK_HI   0xA4  /* IRQ's 8-15 [10,13,15] */
#define PCI_DEVICE_ID_IBM_CORAL		0x000a

#undef  PCI_DEBUG

#ifdef PCI_STATS
int PCI_conversions[2];
#endif


unsigned long pcibios_fixup(unsigned long mem_start, unsigned long mem_end)
{
	return mem_start;
}

int
pcibios_present (void)
{
#ifdef PCI_DEBUG	
	printk("PCI [BIOS] present?\n");
#endif	
	return (1);
}

int
pcibios_read_config_dword (unsigned char bus,
			   unsigned char dev, unsigned char offset, unsigned int *val)
{
	unsigned long _val;
	unsigned long *ptr;
	dev >>= 3;
#ifdef PCI_DEBUG	
	printk("PCI Read config dword[%d.%d.%x] = ", bus, dev, offset);
#endif	
	if ((bus != 0) || (dev < 11) || (dev > 16))
	{
		*val = 0xFFFFFFFF;
		return PCIBIOS_DEVICE_NOT_FOUND;
	} else
	{
		ptr = (unsigned long *)(0x80800000 | (1<<dev) | offset);
#ifdef PCI_DEBUG	
		printk("[%x] ", ptr);
#endif		
		_val = le32_to_cpu(*ptr);
	}
#ifdef PCI_DEBUG	
	printk("%x\n", _val);
#endif	
	*val = _val;
	return PCIBIOS_SUCCESSFUL;
}

int
pcibios_read_config_word (unsigned char bus,
			  unsigned char dev, unsigned char offset, unsigned short *val)
{
	unsigned short _val;
	unsigned short *ptr;
	dev >>= 3;
#ifdef PCI_DEBUG
	printk("PCI Read config word[%d.%d.%x] = ", bus, dev, offset);
#endif	
	if ((bus != 0) || (dev < 11) || (dev > 16))
	{
		*val = (unsigned short)0xFFFFFFFF;
		return PCIBIOS_DEVICE_NOT_FOUND;
	} else
	{
		ptr = (unsigned short *)(0x80800000 | (1<<dev) | offset);
#ifdef PCI_DEBUG
		printk("[%x] ", ptr);
#endif		
		_val = le16_to_cpu(*ptr);
	}
#ifdef PCI_DEBUG
	printk("%x\n", _val);
#endif		
	*val = _val;
	return PCIBIOS_SUCCESSFUL;
}

int
pcibios_read_config_byte (unsigned char bus,
			  unsigned char dev, unsigned char offset, unsigned char *val)
{
	unsigned char _val;
	volatile unsigned char *ptr;
	dev >>= 3;
	/* Note: the configuration registers don't always have this right! */
	if (offset == PCI_INTERRUPT_LINE)
	{
		if (Motherboard_map[dev] <= 4)
		{
			*val = Motherboard_routes[Motherboard_map[dev]];
			/*printk("dev %d map %d route %d\n",
			  dev,Motherboard_map[dev],
			  Motherboard_routes[Motherboard_map[dev]]);*/
		} else
		{ /* Pseudo interrupts [for BeBox] */
			*val = Motherboard_map[dev];
		}
#ifdef PCI_DEBUG	
		printk("PCI Read Interrupt Line[%d.%d] = %d\n", bus, dev, *val);
#endif		
		return PCIBIOS_SUCCESSFUL;
	}
#ifdef PCI_DEBUG	
	printk("PCI Read config byte[%d.%d.%x] = ", bus, dev, offset);
#endif		
	if ((bus != 0) || (dev < 11) || (dev > 16))
	{
		*val = 0xFFFFFFFF;
		return PCIBIOS_DEVICE_NOT_FOUND;
	} else
	{
		ptr = (unsigned char *)(0x80800000 | (1<<dev) | offset ^ 1);
#ifdef PCI_DEBUG	
		printk("[%x] ", ptr);
#endif		
		_val = *ptr;
	}
#ifdef PCI_DEBUG	
	printk("%x\n", _val);
#endif
	*val = _val;
	return PCIBIOS_SUCCESSFUL;
}

int
pcibios_write_config_dword (unsigned char bus,
			    unsigned char dev, unsigned char offset, unsigned int val)
{
	unsigned long _val;
	unsigned long *ptr;
	dev >>= 3;
	_val = le32_to_cpu(val);
#ifdef PCI_DEBUG	
	printk("PCI Write config dword[%d.%d.%x] = %x\n", bus, dev, offset, _val);
#endif		
	if ((bus != 0) || (dev < 11) || (dev > 16))
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
pcibios_write_config_word (unsigned char bus,
			   unsigned char dev, unsigned char offset, unsigned short val)
{
	unsigned short _val;
	unsigned short *ptr;
	dev >>= 3;
	_val = le16_to_cpu(val);
#ifdef PCI_DEBUG	
	printk("PCI Write config word[%d.%d.%x] = %x\n", bus, dev, offset, _val);
#endif		
	if ((bus != 0) || (dev < 11) || (dev > 16))
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
pcibios_write_config_byte (unsigned char bus,
			   unsigned char dev, unsigned char offset, unsigned char val)
{
	unsigned char _val;
	unsigned char *ptr;
	dev >>= 3;
	_val = val;
#ifdef PCI_DEBUG	
	printk("PCI Write config byte[%d.%d.%x] = %x\n", bus, dev, offset, _val);
#endif		
	if ((bus != 0) || (dev < 11) || (dev > 16))
	{
		return PCIBIOS_DEVICE_NOT_FOUND;
	} else
	{
		ptr = (unsigned char *)(0x80800000 | (1<<dev) | offset ^ 1);
		*ptr = _val;
	}
	return PCIBIOS_SUCCESSFUL;
}

int
pcibios_find_device (unsigned short vendor, unsigned short device_id,
		     unsigned short index, unsigned char *bus,
		     unsigned char *dev)
{
	unsigned long w, desired = (device_id << 16) | vendor;
	int devnr;

	if (vendor == 0xffff) {
		return PCIBIOS_BAD_VENDOR_ID;
	}

	for (devnr = 11;  devnr < 16;  devnr++)
	{
		pcibios_read_config_dword(0, devnr<<3, PCI_VENDOR_ID, &w);
		if (w == desired) {
			if (index == 0) {
				*bus = 0;
				*dev = devnr<<3;
				return PCIBIOS_SUCCESSFUL;
			}
			--index;
		}
	}
	return PCIBIOS_DEVICE_NOT_FOUND;
}

int
pcibios_find_class (unsigned int class_code, unsigned short index, 
		    unsigned char *bus, unsigned char *dev)
{
	int dev_nr, class, indx;
	indx = 0;
#ifdef PCI_DEBUG	
	printk("pcibios_find_class - class: %x, index: %x", class_code, index);
#endif	
	for (dev_nr = 11;  dev_nr < 16;  dev_nr++)
	{
		pcibios_read_config_dword(0, dev_nr<<3, PCI_CLASS_REVISION, &class);
		if ((class>>8) == class_code)
		{
			if (index == indx)
			{
				*bus = 0;
				*dev = dev_nr<<3;
#ifdef PCI_DEBUG
				printk(" - device: %x\n", dev_nr);
#endif	
				return (0);
			}
			indx++;
		}
	}
#ifdef PCI_DEBUG
	printk(" - not found\n");
#endif	
	return PCIBIOS_DEVICE_NOT_FOUND;
}    

const char *pcibios_strerror(int error)
{
	static char buf[32];
	switch (error)
	{	case PCIBIOS_SUCCESSFUL:
		return ("PCI BIOS: no error");
	case PCIBIOS_FUNC_NOT_SUPPORTED:
		return ("PCI BIOS: function not supported");
	case PCIBIOS_BAD_VENDOR_ID:
		return ("PCI BIOS: bad vendor ID");
	case PCIBIOS_DEVICE_NOT_FOUND:
		return ("PCI BIOS: device not found");
	case PCIBIOS_BAD_REGISTER_NUMBER:
		return ("PCI BIOS: bad register number");
	case PCIBIOS_SET_FAILED:
		return ("PCI BIOS: set failed");
	case PCIBIOS_BUFFER_TOO_SMALL:
		return ("PCI BIOS: buffer too small");
	default:
		sprintf(buf, "PCI BIOS: invalid error #%d", error);
		return(buf);
	}
}

/*
 * Note: This routine has to access the PCI configuration space
 * for the PCI bridge chip (Intel 82378).
 */
unsigned long pcibios_init(unsigned long mem_start,unsigned long mem_end)
{
	return mem_start;
}

unsigned long route_pci_interrupts(void)
{
	unsigned char *ibc_pirq = (unsigned char *)0x80800860;
	unsigned char *ibc_pcicon = (unsigned char *)0x80800840;
	extern unsigned long isBeBox[];
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
	} else
	{
		if ( _machine == _MACH_IBM )
		{
			unsigned char pl_id;
			unsigned long flags;
			unsigned index;
			unsigned char fn, bus;
			unsigned int addr;
			unsigned char dma_mode, ide_mode;
			int i;
			
			Motherboard_map_name = "IBM 8xx (Carolina)";
			Motherboard_map = ibm8xx_pci_IRQ_map;
			Motherboard_routes = ibm8xx_pci_IRQ_routes;
ll_printk("before loop\n");			
			
			for (index = 0;
			     !pcibios_find_device (PCI_VENDOR_ID_IBM, 
						   PCI_DEVICE_ID_IBM_CORAL, 
						   index, &bus, &fn); ++index)
			{
				pcibios_read_config_dword(bus, fn, 0x10, &addr);
				addr &= ~0x3;
				outb(0x26, addr);
				dma_mode = inb(addr+4);
				outb(0x25, addr);
				ide_mode = inb(addr+4);
				/*printk("CORAL I/O at 0x%x, DMA mode: %x, IDE mode: %x", 
				       addr, dma_mode, ide_mode);*/
				/* Make CDROM non-DMA */
				ide_mode = (ide_mode & 0x0F) | 0x20;
				outb(0x25, addr);
				outb(ide_mode, addr+4);
				dma_mode = dma_mode & ~0x80;
				outb(0x26, addr);
				outb(dma_mode, addr+4);
				outb(0x26, addr);
				dma_mode = inb(addr+4);
				outb(0x25, addr);
				ide_mode = inb(addr+4);
				/*printk("=> DMA mode: %x, IDE mode: %x\n", 
				       dma_mode, ide_mode);*/
			}
			
			/* Setup the PCI INT mappings for the Carolina */
			/* These are PCI Interrupt Route Control [1|2] Register */
			outb(Carolina_PIRQ_routes[0], 0x0890);
			outb(Carolina_PIRQ_routes[1], 0x0891);
			
			pl_id=inb(0x0852);
			/*printk("CPU Planar ID is %#0x\n", pl_id);*/
			
			if (pl_id == 0x0C) {
				/* INDI */
				Motherboard_map[12] = 1;
			}
ll_printk("before edge/level\n");			
#if 0			
			/*printk("Changing IRQ mode\n");*/
			pl_id=inb(0x04d0);
			/*printk("Low mask is %#0x\n", pl_id);*/
			outb(pl_id|CAROLINA_IRQ_EDGE_MASK_LO, 0x04d0);
			
			pl_id=inb(0x04d1);
			/*printk("Hi mask is  %#0x\n", pl_id);*/
			outb(pl_id|CAROLINA_IRQ_EDGE_MASK_HI, 0x04d1);
			pl_id=inb(0x04d1);
			/*printk("Hi mask now %#0x\n", pl_id);*/
#endif			
		}
	}
	
	/* Set up mapping from slots */
	for (i = 1;  i <= 4;  i++)
	{
		ibc_pirq[i-1] = Motherboard_routes[i];
	}
	/* Enable PCI interrupts */
	*ibc_pcicon |= 0x20;
}
