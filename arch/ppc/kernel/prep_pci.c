/*
 * $Id: prep_pci.c,v 1.24 1998/12/10 02:39:51 cort Exp $
 * PReP pci functions.
 * Originally by Gary Thomas
 * rewritten and updated by Cort Dougan (cort@cs.nmt.edu)
 *
 * The motherboard routes/maps will disappear shortly. -- Cort
 */

#include <linux/types.h>
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

/* Motorola PowerStackII - Utah */
static char Utah_pci_IRQ_map[23] __prepdata =
{
        0,   /* Slot 0  - unused */
        0,   /* Slot 1  - unused */
        4,   /* Slot 2  - SCSI - NCR825A  */
        0,   /* Slot 3  - unused */
        1,   /* Slot 4  - Ethernet - DEC2114x */
        0,   /* Slot 5  - unused */
        2,   /* Slot 6  - PCI Card slot #1 */
        3,   /* Slot 7  - PCI Card slot #2 */
        4,   /* Slot 8  - PCI Card slot #3 */
        4,   /* Slot 9  - PCI Bridge */
             /* added here in case we ever support PCI bridges */
             /* Secondary PCI bus cards are at slot-9,6 & slot-9,7 */
        0,   /* Slot 10 - unused */
        0,   /* Slot 11 - unused */
        4,   /* Slot 12 - SCSI - NCR825A */
        0,   /* Slot 13 - unused */
        2,   /* Slot 14 - enet */
        0,   /* Slot 15 - unused */
        0,
        0,
        0,
        0,
        0,
        0,
        0,
};

static char Utah_pci_IRQ_routes[] __prepdata =
{
        0,   /* Line 0 - Unused */
        9,   /* Line 1 */
        11,  /* Line 2 */
        14,  /* Line 3 */
        15,  /* Line 4 */
};

/* Motorola PowerStackII - Omaha */
/* no integrated SCSI or ethernet */
static char Omaha_pci_IRQ_map[23] __prepdata =
{
        0,   /* Slot 0  - unused */
        0,   /* Slot 1  - unused */
        3,   /* Slot 2  - Winbond EIDE */
        0,   /* Slot 3  - unused */
        0,   /* Slot 4  - unused */
        0,   /* Slot 5  - unused */
        1,   /* Slot 6  - PCI slot 1 */
        2,   /* Slot 7  - PCI slot 2  */
        3,   /* Slot 8  - PCI slot 3 */
        4,   /* Slot 9  - PCI slot 4 */ /* needs indirect access */
        0,   /* Slot 10 - unused */
        0,   /* Slot 11 - unused */
        0,   /* Slot 12 - unused */
        0,   /* Slot 13 - unused */
        0,   /* Slot 14 - unused */
        0,   /* Slot 15 - unused */
        1,   /* Slot 16  - PCI slot 1 */
        2,   /* Slot 17  - PCI slot 2  */
        3,   /* Slot 18  - PCI slot 3 */
        4,   /* Slot 19  - PCI slot 4 */ /* needs indirect access */
        0,
        0,
        0,
};

static char Omaha_pci_IRQ_routes[] __prepdata =
{
        0,   /* Line 0 - Unused */
        9,   /* Line 1 */
        11,  /* Line 2 */
        14,  /* Line 3 */
        15   /* Line 4 */
};

/* Motorola PowerStack */
static char Blackhawk_pci_IRQ_map[19] __prepdata =
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
  	1,	/* Slot P7 */
  	2,	/* Slot P6 */
  	3,	/* Slot P5 */
};

static char Blackhawk_pci_IRQ_routes[] __prepdata =
{
   	0,	/* Line 0 - Unused */
   	9,	/* Line 1 */
   	11,	/* Line 2 */
   	15,	/* Line 3 */
   	15	/* Line 4 */
};
   
/* Motorola MVME16xx */
static char Genesis_pci_IRQ_map[16] __prepdata =
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

static char Genesis_pci_IRQ_routes[] __prepdata =
{
   	0,	/* Line 0 - Unused */
   	10,	/* Line 1 */
   	11,	/* Line 2 */
   	14,	/* Line 3 */
   	15	/* Line 4 */
};
   
/* Motorola Series-E */
static char Comet_pci_IRQ_map[16] __prepdata =
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

static char Comet_pci_IRQ_routes[] __prepdata =
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
static char ibm8xx_pci_IRQ_map[23] __prepdata = {
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

static char ibm8xx_pci_IRQ_routes[] __prepdata = {
        0,      /* Line 0 - unused */
        13,     /* Line 1 */
        10,     /* Line 2 */
        15,     /* Line 3 */
        15,     /* Line 4 */
};

/*
 * a 6015 ibm board
 * -- Cort
 */
static char ibm6015_pci_IRQ_map[23] __prepdata = {
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
        0, /* Slot 11 -  */
        1, /* Slot 12 - SCSI */
        2, /* Slot 13 -  */
        2, /* Slot 14 -  */
        1, /* Slot 15 -  */
        1, /* Slot 16 -  */
        0, /* Slot 17 -  */
        2, /* Slot 18 -  */
        0, /* Slot 19 -  */
        0, /* Slot 20 -  */
        0, /* Slot 21 -  */
        2, /* Slot 22 -  */
};
static char ibm6015_pci_IRQ_routes[] __prepdata = {
        0,      /* Line 0 - unused */
        13,     /* Line 1 */
        10,     /* Line 2 */
        15,     /* Line 3 */
        15,     /* Line 4 */
};


/* IBM Nobis and 850 */
static char Nobis_pci_IRQ_map[23] __prepdata ={
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

static char Nobis_pci_IRQ_routes[] __prepdata = {
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

__prep
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

__prep
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

__prep
int
prep_pcibios_read_config_byte (unsigned char bus,
			  unsigned char dev, unsigned char offset, unsigned char *val)
{
	unsigned char _val;
	volatile unsigned char *ptr;
	dev >>= 3;
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

__prep
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

__prep
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

__prep
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

__initfunc(unsigned long route_pci_interrupts(void))
{
	unsigned char *ibc_pirq = (unsigned char *)0x80800860;
	unsigned char *ibc_pcicon = (unsigned char *)0x80800840;
	int i;
	
	if ( _prep_type == _PREP_Motorola)
	{
		unsigned short irq_mode;

		switch (inb(0x800) & 0xF0)
		{
		case 0x10: /* MVME16xx */
			Motherboard_map_name = "Genesis";
			Motherboard_map = Genesis_pci_IRQ_map;
			Motherboard_routes = Genesis_pci_IRQ_routes;
			break;
		case 0x20: /* Series E */
			Motherboard_map_name = "Powerstack (Series E)";
			Motherboard_map = Comet_pci_IRQ_map;
			Motherboard_routes = Comet_pci_IRQ_routes;
			break;
		case 0x50: /* PowerStackII Pro3000 */
			Motherboard_map_name = "Omaha (PowerStack II Pro3000)";
			Motherboard_map = Omaha_pci_IRQ_map;
			Motherboard_routes = Omaha_pci_IRQ_routes;
			break;
		case 0x60: /* PowerStackII Pro4000 */
			Motherboard_map_name = "Utah (Powerstack II Pro4000)";
			Motherboard_map = Utah_pci_IRQ_map;
			Motherboard_routes = Utah_pci_IRQ_routes;
			break;
                case 0xE0: /* MTX -- close enough?? to Genesis, so reuse it */
                        Motherboard_map_name = "Motorola MTX";
                        Motherboard_map = Genesis_pci_IRQ_map;
                        Motherboard_routes = Genesis_pci_IRQ_routes;
                        break;
		case 0x40: /* PowerStack */
		default: /* Can't hurt, can it? */
			Motherboard_map_name = "Blackhawk (Powerstack)";
			Motherboard_map = Blackhawk_pci_IRQ_map;
			Motherboard_routes = Blackhawk_pci_IRQ_routes;
			break;
		}
		/* AJF adjust level/edge control according to routes */
		irq_mode = 0;
		for (i = 1;  i <= 4;  i++)
		{
			irq_mode |= ( 1 << Motherboard_routes[i] );
		}
		outb( irq_mode & 0xff, 0x4d0 );
		outb( (irq_mode >> 8) & 0xff, 0x4d1 );
	} else if ( _prep_type == _PREP_IBM )
	{
		unsigned char pl_id;
		/*
		 * my carolina is 0xf0
		 * 6015 has 0xfc
		 * -- Cort
		 */
		printk("IBM ID: %08x\n", inb(0x0852));
		switch(inb(0x0852))
		{
		case 0xff:
			Motherboard_map_name = "IBM 850/860 Portable\n";
			Motherboard_map = Nobis_pci_IRQ_map;
			Motherboard_routes = Nobis_pci_IRQ_routes;
			break;
		case 0xfc:
			Motherboard_map_name = "IBM 6015";
			Motherboard_map = ibm6015_pci_IRQ_map;
			Motherboard_routes = ibm6015_pci_IRQ_routes;
			break;			
		default:
			Motherboard_map_name = "IBM 8xx (Carolina)";
			Motherboard_map = ibm8xx_pci_IRQ_map;
			Motherboard_routes = ibm8xx_pci_IRQ_routes;
			break;
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

