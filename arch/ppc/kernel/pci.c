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

/*
 * PCI interrupt configuration.  This is motherboard specific.
 */
 
 
/* Which PCI interrupt line does a given device [slot] use? */
/* Note: This really should be two dimensional based in slot/pin used */
unsigned char *Motherboard_map;

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

/* #define PCI_DEBUG */

#ifdef PCI_STATS
int PCI_conversions[2];
#endif

unsigned long pcibios_init(unsigned long mem_start,
			   unsigned long mem_end)
{
	return mem_start;
}

unsigned long pcibios_fixup(unsigned long mem_start, unsigned long mem_end)
{
  return mem_start;
}


unsigned long
_LE_to_BE_long(unsigned long val)
{
	unsigned char *p = (unsigned char *)&val;
#ifdef PCI_STATS
	PCI_conversions[0]++;
#endif	
	return ((p[3] << 24) | (p[2] << 16) | (p[1] << 8) | (p[0] << 0));
}

unsigned short
_LE_to_BE_short(unsigned long val)
{
	unsigned char *p = (unsigned char *)&val;
#ifdef PCI_STATS
	PCI_conversions[1]++;
#endif	
	return ((p[3] << 8) | (p[2] << 0));
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
		_val = _LE_to_BE_long(*ptr);
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
		*val = 0xFFFFFFFF;
		return PCIBIOS_DEVICE_NOT_FOUND;
	} else
	{
		ptr = (unsigned short *)(0x80800000 | (1<<dev) | offset);
#ifdef PCI_DEBUG	
		printk("[%x] ", ptr);
#endif		
		_val = _LE_to_BE_short(*ptr);
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
	_val = _LE_to_BE_long(val);
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
	_val = _LE_to_BE_short(val);
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
 
void route_PCI_interrupts(void)
{
	unsigned char *ibc_pirq = (unsigned char *)0x80800860;
	unsigned char *ibc_pcicon = (unsigned char *)0x80800840;
	extern unsigned long isBeBox[];
	int i;
	/* Decide which motherboard this is & how the PCI interrupts are routed */
	if (isBeBox[0])
	{
		Motherboard_map = BeBox_pci_IRQ_map;
		Motherboard_routes = BeBox_pci_IRQ_routes;
	} else
	{ /* Motorola hardware */
		switch (inb(0x800) & 0xF0)
		{
			case 0x10: /* MVME16xx */
				Motherboard_map = Genesis_pci_IRQ_map;
				Motherboard_routes = Genesis_pci_IRQ_routes;
				break;
			case 0x20: /* Series E */
				Motherboard_map = Comet_pci_IRQ_map;
				Motherboard_routes = Comet_pci_IRQ_routes;
				break;
			case 0x40: /* PowerStack */
			default: /* Can't hurt, can it? */
				Motherboard_map = Blackhawk_pci_IRQ_map;
				Motherboard_routes = Blackhawk_pci_IRQ_routes;
				break;
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
