/*
 *  tmspci.c: A generic network driver for TMS380-based PCI token ring cards.
 *
 *  Written 1999 by Adam Fritzler
 *
 *  This software may be used and distributed according to the terms
 *  of the GNU Public License, incorporated herein by reference.
 *
 *  This driver module supports the following cards:
 *	- SysKonnect TR4/16(+) PCI	(SK-4590)
 *	- SysKonnect TR4/16 PCI		(SK-4591)
 *      - Compaq TR 4/16 PCI
 *      - Thomas-Conrad TC4048 4/16 PCI 
 *      - 3Com 3C339 Token Link Velocity
 *
 *  Maintainer(s):
 *    AF	Adam Fritzler		mid@auk.cx
 *
 *  Modification History:
 *	30-Dec-99	AF	Split off from the tms380tr driver.
 *	22-Jan-00	AF	Updated to use indirect read/writes
 *
 *  TODO:
 *	1. See if we can use MMIO instead of port accesses
 *
 */
static const char *version = "tmspci.c: v1.01 22/01/2000 by Adam Fritzler\n";

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>

#include <linux/netdevice.h>
#include <linux/trdevice.h>
#include "tms380tr.h"

#define TMS_PCI_IO_EXTENT 32

struct cardinfo_table {
	int vendor_id; /* PCI info */
	int device_id;
	int registeroffset; /* SIF offset from dev->base_addr */
	unsigned char nselout[2]; /* NSELOUT vals for 4mb([0]) and 16mb([1]) */
	char *name;
};

struct cardinfo_table probelist[] = {
	{ 0, 0,
	  0x0000, {0x00, 0x00}, "Unknown TMS380 Token Ring Adapter"},
	{ PCI_VENDOR_ID_COMPAQ, PCI_DEVICE_ID_COMPAQ_TOKENRING, 
	  0x0000, {0x03, 0x01}, "Compaq 4/16 TR PCI"},
	{ PCI_VENDOR_ID_SYSKONNECT, PCI_DEVICE_ID_SYSKONNECT_TR, 
	  0x0000, {0x03, 0x01}, "SK NET TR 4/16 PCI"},
	{ PCI_VENDOR_ID_TCONRAD, PCI_DEVICE_ID_TCONRAD_TOKENRING,
	  0x0000, {0x03, 0x01}, "Thomas-Conrad TC4048 PCI 4/16"},
	{ PCI_VENDOR_ID_3COM, PCI_DEVICE_ID_3COM_3C339,
	  0x0000, {0x03, 0x01}, "3Com Token Link Velocity"},
	{ 0, 0, 0, {0x00, 0x00}, NULL}
};

int tms_pci_probe(void);
static int tms_pci_open(struct net_device *dev);
static int tms_pci_close(struct net_device *dev);
static void tms_pci_read_eeprom(struct net_device *dev);
static unsigned short tms_pci_setnselout_pins(struct net_device *dev);

static unsigned short tms_pci_sifreadb(struct net_device *dev, unsigned short reg)
{
	return inb(dev->base_addr + reg);
}

static unsigned short tms_pci_sifreadw(struct net_device *dev, unsigned short reg)
{
	return inw(dev->base_addr + reg);
}

static void tms_pci_sifwriteb(struct net_device *dev, unsigned short val, unsigned short reg)
{
	outb(val, dev->base_addr + reg);
}

static void tms_pci_sifwritew(struct net_device *dev, unsigned short val, unsigned short reg)
{
	outw(val, dev->base_addr + reg);
}

struct tms_pci_card {
	struct net_device *dev;
	struct pci_dev *pci_dev;
	struct cardinfo_table *cardinfo;
	struct tms_pci_card *next;
};
static struct tms_pci_card *tms_pci_card_list = NULL;


struct cardinfo_table * __init tms_pci_getcardinfo(unsigned short vendor, 
						   unsigned short device)
{
	int cur;
	for (cur = 1; probelist[cur].name != NULL; cur++) {
		if ((probelist[cur].vendor_id == vendor) && 
		    (probelist[cur].device_id == device))
			return &probelist[cur];
	}
	
	return NULL;
}

int __init tms_pci_probe(void)
{	
	static int versionprinted = 0;
	struct pci_dev *pdev = NULL ; 
	struct net_device *dev;
	struct net_local *tp;
	int i;

	if (!pci_present())
		return (-1);	/* No PCI present. */
  
	while ( (pdev=pci_find_class(PCI_CLASS_NETWORK_TOKEN_RING<<8, pdev))) { 
		unsigned int pci_irq_line;
		unsigned long pci_ioaddr;
		struct tms_pci_card *card;
		struct cardinfo_table *cardinfo;
		
		if ((cardinfo = 
		     tms_pci_getcardinfo(pdev->vendor, pdev->device)) == NULL)
			continue;	

		if (versionprinted++ == 0)
			printk("%s", version);

		pci_enable_device(pdev);

		/* Remove I/O space marker in bit 0. */
		pci_irq_line = pdev->irq;
		pci_ioaddr = pdev->resource[0].start ; 
		
		if(check_region(pci_ioaddr, TMS_PCI_IO_EXTENT))
			continue;
    
		/* At this point we have found a valid card. */
    
		dev = init_trdev(NULL, 0);
		
		request_region(pci_ioaddr, TMS_PCI_IO_EXTENT, cardinfo->name);
		if(request_irq(pdev->irq, tms380tr_interrupt, SA_SHIRQ,
			       cardinfo->name, dev)) { 
			release_region(pci_ioaddr, TMS_PCI_IO_EXTENT); 
			continue; /*return (-ENODEV);*/ /* continue; ?? */
		}

		/*
		  if (load_tms380_module("tmspci.c")) {
		  return 0;
		  }
		*/

		pci_ioaddr &= ~3 ; 
		dev->base_addr	= pci_ioaddr;
		dev->irq 		= pci_irq_line;
		dev->dma		= 0;
		
		printk("%s: %s\n", dev->name, cardinfo->name);
		printk("%s:    IO: %#4lx  IRQ: %d\n",
		       dev->name, dev->base_addr, dev->irq);
		/*
		 * Some cards have their TMS SIF registers offset from
		 * their given base address.  Account for that here. 
		 */
		dev->base_addr += cardinfo->registeroffset;
		
		tms_pci_read_eeprom(dev);

		printk("%s:    Ring Station Address: ", dev->name);
		printk("%2.2x", dev->dev_addr[0]);
		for (i = 1; i < 6; i++)
			printk(":%2.2x", dev->dev_addr[i]);
		printk("\n");
		
		if (tmsdev_init(dev)) {
			printk("%s: unable to get memory for dev->priv.\n", dev->name);
			return 0;
		}

		tp = (struct net_local *)dev->priv;
		tp->dmalimit = 0; /* XXX: should be the max PCI32 DMA max */
		tp->setnselout = tms_pci_setnselout_pins;
		
		tp->sifreadb = tms_pci_sifreadb;
		tp->sifreadw = tms_pci_sifreadw;
		tp->sifwriteb = tms_pci_sifwriteb;
		tp->sifwritew = tms_pci_sifwritew;
		
		memcpy(tp->ProductID, cardinfo->name, PROD_ID_SIZE + 1);

		tp->tmspriv = cardinfo;

		dev->open = tms_pci_open;
		dev->stop = tms_pci_close;

		if (register_trdev(dev) == 0) {
			/* Enlist in the card list */
			card = kmalloc(sizeof(struct tms_pci_card), GFP_KERNEL);
			card->next = tms_pci_card_list;
			tms_pci_card_list = card;
			card->dev = dev;
			card->pci_dev = pdev;
			card->cardinfo = cardinfo;
		} else {
			printk("%s: register_trdev() returned non-zero.\n", dev->name);
			kfree(dev->priv);
			kfree(dev);
			return -1;
		}
	}
	
	if (tms_pci_card_list)
		return 0;
	return (-1);
}

/*
 * Reads MAC address from adapter RAM, which should've read it from
 * the onboard ROM.  
 *
 * Calling this on a board that does not support it can be a very
 * dangerous thing.  The Madge board, for instance, will lock your
 * machine hard when this is called.  Luckily, its supported in a
 * seperate driver.  --ASF
 */
static void tms_pci_read_eeprom(struct net_device *dev)
{
	int i;
	
	/* Address: 0000:0000 */
	tms_pci_sifwritew(dev, 0, SIFADX);
	tms_pci_sifwritew(dev, 0, SIFADR);	
	
	/* Read six byte MAC address data */
	dev->addr_len = 6;
	for(i = 0; i < 6; i++)
		dev->dev_addr[i] = tms_pci_sifreadw(dev, SIFINC) >> 8;
}

unsigned short tms_pci_setnselout_pins(struct net_device *dev)
{
	unsigned short val = 0;
	struct net_local *tp = (struct net_local *)dev->priv;
	struct cardinfo_table *cardinfo = (struct cardinfo_table *)tp->tmspriv;
  
	if(tp->DataRate == SPEED_4)
		val |= cardinfo->nselout[0];	/* Set 4Mbps */
	else
		val |= cardinfo->nselout[1];	/* Set 16Mbps */
	return val;
}

static int tms_pci_open(struct net_device *dev)
{  
	tms380tr_open(dev);
	MOD_INC_USE_COUNT;
	return 0;
}

static int tms_pci_close(struct net_device *dev)
{
	tms380tr_close(dev);
	MOD_DEC_USE_COUNT;
	return 0;
}

#ifdef MODULE

int init_module(void)
{
	/* Probe for cards. */
	if (tms_pci_probe()) {
		printk(KERN_NOTICE "tmspci.c: No cards found.\n");
	}
	/* lock_tms380_module(); */
	return (0);
}

void cleanup_module(void)
{
	struct net_device *dev;
	struct tms_pci_card *this_card;

	while (tms_pci_card_list) {
		dev = tms_pci_card_list->dev;
		
		/*
		 * If we used a register offset, revert here.
		 */
		if (dev->priv)
		{	
			struct net_local *tp;
			struct cardinfo_table *cardinfo;

			tp = (struct net_local *)dev->priv;
			cardinfo = (struct cardinfo_table *)tp->tmspriv;
			
			dev->base_addr -= cardinfo->registeroffset;
		}
		unregister_netdev(dev);
		release_region(dev->base_addr, TMS_PCI_IO_EXTENT);
		free_irq(dev->irq, dev);
		kfree(dev->priv);
		kfree(dev);
		this_card = tms_pci_card_list;
		tms_pci_card_list = this_card->next;
		kfree(this_card);
	}
	/* unlock_tms380_module(); */
}
#endif /* MODULE */


/*
 * Local variables:
 *  compile-command: "gcc -DMODVERSIONS  -DMODULE -D__KERNEL__ -Wall -Wstrict-prototypes -O6 -fomit-frame-pointer -I/usr/src/linux/drivers/net/tokenring/ -c tmspci.c"
 *  alt-compile-command: "gcc -DMODULE -D__KERNEL__ -Wall -Wstrict-prototypes -O6 -fomit-frame-pointer -I/usr/src/linux/drivers/net/tokenring/ -c tmspci.c"
 *  c-set-style "K&R"
 *  c-indent-level: 8
 *  c-basic-offset: 8
 *  tab-width: 8
 * End:
 */
