/*
 * $Id: prep_pci.c,v 1.31 1999/04/21 18:21:37 cort Exp $
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
#include <linux/openpic.h>

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/ptrace.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <asm/residual.h>
#include <asm/processor.h>
#include <asm/irq.h>
#include <asm/machdep.h>

#include "pci.h"

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
   
/* Motorola Genesis2 MVME26XX, MVME 36XX */
/* The final version for these boards should use the Raven PPC/PCI bridge 
interrupt controller which is much sophisticated and allows more
devices on the PCI bus. */
static char Genesis2_pci_IRQ_map[23] __prepdata =
  {
        0,	/* Slot 0  - ECC memory controller/PCI bridge */
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
  	0,	/* Slot 11 - ISA bridge */
	3,	/* Slot 12 - SCSI */
  	2,	/* Slot 13 - Universe PCI/VME bridge (and 22..24) */
  	1,	/* Slot 14 - Ethernet */
  	0,	/* Slot 15 - Unused (graphics on 3600, would be 20 ?) */
	4,      /* Slot 16 - PMC slot, assume uses INTA */
	0,      /* Slot 17 */
	0,      /* Slot 18 */
	0,      /* Slot 19 */
	0,      /* Slot 20 */
	0,      /* Slot 21 */
	0,      /* Slot 22 */
  };

static char Genesis2_pci_IRQ_routes[] __prepdata=
   {
   	0,	/* Line 0 - Unused */
   	10,	/* Line 1 - INTA */
   	11,	/* Line 2 - INTB */
   	14,	/* Line 3 - INTC */
   	15	/* Line 4 - INTD */
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

/*
 * 8259 edge/level control definitions
 */
#define ISA8259_M_ELCR 0x4d0
#define ISA8259_S_ELCR 0x4d1

#define ELCRS_INT15_LVL         0x80
#define ELCRS_INT14_LVL         0x40
#define ELCRS_INT12_LVL         0x10
#define ELCRS_INT11_LVL         0x08
#define ELCRS_INT10_LVL         0x04
#define ELCRS_INT9_LVL          0x02
#define ELCRS_INT8_LVL          0x01
#define ELCRM_INT7_LVL          0x80
#define ELCRM_INT5_LVL          0x20

#define CFGPTR(dev) (0x80800000 | (1<<(dev>>3)) | ((dev&7)<<8) | offset)
#define DEVNO(dev)  (dev>>3)                                  

__prep
int
prep_pcibios_read_config_dword (unsigned char bus,
			   unsigned char dev, unsigned char offset, unsigned int *val)
{
	unsigned long _val;                                          
	unsigned long *ptr;

	if ((bus != 0) || (DEVNO(dev) > MAX_DEVNR))
	{                   
		*val = 0xFFFFFFFF;
		return PCIBIOS_DEVICE_NOT_FOUND;    
	} else                                                                
	{
		ptr = (unsigned long *)CFGPTR(dev);
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

	if ((bus != 0) || (DEVNO(dev) > MAX_DEVNR))
	{                   
		*val = 0xFFFF;
		return PCIBIOS_DEVICE_NOT_FOUND;    
	} else                                                                
	{
		ptr = (unsigned short *)CFGPTR(dev);
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
	unsigned char *ptr;

	if ((bus != 0) || (DEVNO(dev) > MAX_DEVNR))
	{                   
		*val = 0xFF;
		return PCIBIOS_DEVICE_NOT_FOUND;    
	} else                                                                
	{
		ptr = (unsigned char *)CFGPTR(dev);
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

	_val = le32_to_cpu(val);
	if ((bus != 0) || (DEVNO(dev) > MAX_DEVNR))
	{
		return PCIBIOS_DEVICE_NOT_FOUND;
	} else
	{
		ptr = (unsigned long *)CFGPTR(dev);
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

	_val = le16_to_cpu(val);
	if ((bus != 0) || (DEVNO(dev) > MAX_DEVNR))
	{
		return PCIBIOS_DEVICE_NOT_FOUND;
	} else
	{
		ptr = (unsigned short *)CFGPTR(dev);
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

	_val = val;
	if ((bus != 0) || (DEVNO(dev) > MAX_DEVNR))
	{
		return PCIBIOS_DEVICE_NOT_FOUND;
	} else
	{
		ptr = (unsigned char *)CFGPTR(dev);
		*ptr = _val;
	}
	return PCIBIOS_SUCCESSFUL;
}

__initfunc(unsigned long prep_route_pci_interrupts(void))
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
		case 0xE0: /* MVME 26xx, 36xx, MTX ? */
			Motherboard_map_name = "Genesis2";
			Motherboard_map = Genesis2_pci_IRQ_map;
			Motherboard_routes = Genesis2_pci_IRQ_routes;

			/* Return: different ibc_pcicon and
			   pirq already set up by firmware. */
			return 0; 
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
	} else if ( _prep_type == _PREP_Radstone )
	{
		unsigned char ucElcrM, ucElcrS;

		/*
		 * Set up edge/level
		 */
		switch(ucSystemType)
		{
			case RS_SYS_TYPE_PPC1:
			{
				if(ucBoardRevMaj<5)
				{
					ucElcrS=ELCRS_INT15_LVL;
				}
				else
				{
					ucElcrS=ELCRS_INT9_LVL |
					        ELCRS_INT11_LVL |
					        ELCRS_INT14_LVL |
					        ELCRS_INT15_LVL;
				}
				ucElcrM=ELCRM_INT5_LVL | ELCRM_INT7_LVL;
				break;
			}

			case RS_SYS_TYPE_PPC1a:
			{
				ucElcrS=ELCRS_INT9_LVL |
				        ELCRS_INT11_LVL |
				        ELCRS_INT14_LVL |
				        ELCRS_INT15_LVL;
				ucElcrM=ELCRM_INT5_LVL;
				break;
			}

			case RS_SYS_TYPE_PPC2:
			case RS_SYS_TYPE_PPC2a:
			case RS_SYS_TYPE_PPC2ep:
			case RS_SYS_TYPE_PPC4:
			case RS_SYS_TYPE_PPC4a:
			default:
			{
				ucElcrS=ELCRS_INT9_LVL |
				        ELCRS_INT10_LVL |
				        ELCRS_INT11_LVL |
				        ELCRS_INT14_LVL |
				        ELCRS_INT15_LVL;
				ucElcrM=ELCRM_INT5_LVL |
				        ELCRM_INT7_LVL;
				break;
			}
		}

		/*
		 * Write edge/level selection
		 */
		outb(ucElcrS, ISA8259_S_ELCR);
		outb(ucElcrM, ISA8259_M_ELCR);

		/*
		 * Radstone boards have PCI interrupts all set up
		 * so leave well alone
		 */
		return 0;
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

__initfunc(
static inline void fixup_pci_interrupts(PnP_TAG_PACKET *pkt)) {
#define data pkt->L4_Pack.L4_Data.L4_PPCPack.PPCData
        u_int bus = data[16];
        u_char *End, *p;

        End = data + ld_le16((u_short *)(&pkt->L4_Pack.Count0)) - 1;
        printk("Interrupt mapping from %d to %d\n", 20, End-data);
        for (p=data+20; p<End; p+=12){
                struct pci_dev *dev;
                for (dev=pci_devices; dev; dev=dev->next) {
                        unsigned code, irq;
                        u_char pin;

                        if ( dev->bus->number != bus ||
                             PCI_SLOT(dev->devfn) != PCI_SLOT(p[1]))
                                continue;
                        pci_read_config_byte(dev,  PCI_INTERRUPT_PIN, &pin);
                        if(!pin) continue;
                        code=ld_le16((unsigned short *)
                                    (p+4+2*(pin-1)));
                        /* Set vector to 0 for unrouted PCI ints. This code
                         * is ugly but handles correctly the special case of
                         * interrupt 0 (8259 cascade) on OpenPIC
                         */

                        irq = (code == 0xffff) ? 0 : code&0x7fff;
                        if (p[2] == 2) { /* OpenPIC */
                                if (irq) {
                                        openpic_set_sense(irq, code<0x8000);
                                        irq=openpic_to_irq(irq);
                                } else continue;
                        } else if (p[2] != 1){ /* Not 8259 */
                                printk("Unknown or unsupported "
                                       "interrupt controller"
                                       "type %d.\n",  p[2]);
                                continue;
                        }
                        dev->irq=irq;
                }

        }
}

__initfunc(
static inline void fixup_bases(struct pci_dev *dev)) {
        int k;
        for (k=0; k<6; k++) {
		/* FIXME: get the base address physical offset from 
			the Raven instead of hard coding it.
				-- Troy */
                if (dev->base_address[k] &&
                    (dev->base_address[k]&PCI_BASE_ADDRESS_SPACE)
                    == PCI_BASE_ADDRESS_SPACE_MEMORY)
                        dev->base_address[k]+=0xC0000000;
                if ((dev->base_address[k] &
                     (PCI_BASE_ADDRESS_SPACE |
                      PCI_BASE_ADDRESS_MEM_TYPE_MASK))
                     == (PCI_BASE_ADDRESS_SPACE_MEMORY |
                         PCI_BASE_ADDRESS_MEM_TYPE_64))
                        k++;
        }
}


__initfunc(
void
prep_pcibios_fixup(void))
{
        struct pci_dev *dev;
        extern unsigned char *Motherboard_map;
        extern unsigned char *Motherboard_routes;
        unsigned char i;

        if ( _prep_type == _PREP_Radstone )
        {
                printk("Radstone boards require no PCI fixups\n");
        }
        else
        {
	        prep_route_pci_interrupts();
                for(dev=pci_devices; dev; dev=dev->next)
                {
                        /*
                         * Use our old hard-coded kludge to figure out what
                         * irq this device uses.  This is necessary on things
                         * without residual data. -- Cort
                         */
                        unsigned char d = PCI_SLOT(dev->devfn);
                        dev->irq = Motherboard_routes[Motherboard_map[d]];
                        for ( i = 0 ; i <= 5 ; i++ )
                        {
                                if ( dev->base_address[i] > 0x10000000 )
                                {
                                        printk("Relocating PCI address %lx -> %lx\n",
                                               dev->base_address[i],
                                               (dev->base_address[i] & 0x00FFFFFF)
                                               | 0x01000000);
                                        dev->base_address[i] =
                                          (dev->base_address[i] & 0x00FFFFFF) | 0x01000000;
                                        pci_write_config_dword(dev,
                                                PCI_BASE_ADDRESS_0+(i*0x4),
                                               dev->base_address[i] );
                                }
                        }
#if 0
                        /*
                         * If we have residual data and if it knows about this
                         * device ask it what the irq is.
                         *  -- Cort
                         */
                        ppcd = residual_find_device_id( ~0L, dev->device,
                                                        -1,-1,-1, 0);
#endif
		}
	}
}

decl_config_access_method(indirect);

__initfunc(
void
prep_setup_pci_ptrs(void))
{
	PPC_DEVICE *hostbridge;

        printk("PReP architecture\n");
        if ( _prep_type == _PREP_Radstone )
        {
		pci_config_address = (unsigned *)0x80000cf8;
		pci_config_data = (char *)0x80000cfc;
                set_config_access_method(indirect);		
        }
        else
        {
                hostbridge = residual_find_device(PROCESSORDEVICE, NULL,
		       BridgeController, PCIBridge, -1, 0);
                if (hostbridge &&
                    hostbridge->DeviceId.Interface == PCIBridgeIndirect) {
                        PnP_TAG_PACKET * pkt;
                        set_config_access_method(indirect);
                        pkt = PnP_find_large_vendor_packet(
				res->DevicePnPHeap+hostbridge->AllocatedOffset,
				3, 0);
                        if(pkt)
			{
#define p pkt->L4_Pack.L4_Data.L4_PPCPack
                                pci_config_address= (unsigned *)ld_le32((unsigned *) p.PPCData);
				pci_config_data= (unsigned char *)ld_le32((unsigned *) (p.PPCData+8));
                        }
			else
			{
                                pci_config_address= (unsigned *) 0x80000cf8;
                                pci_config_data= (unsigned char *) 0x80000cfc;
                        }
                }
		else
		{
                        set_config_access_method(prep);
                }

        }

	ppc_md.pcibios_fixup = prep_pcibios_fixup;
}

