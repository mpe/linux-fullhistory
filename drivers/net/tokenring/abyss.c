/*
 *  abyss.c: Network driver for the Madge Smart 16/4 PCI Mk2 token ring card.
 *
 *  Written 1999-2000 by Adam Fritzler
 *
 *  This software may be used and distributed according to the terms
 *  of the GNU Public License, incorporated herein by reference.
 *
 *  This driver module supports the following cards:
 *      - Madge Smart 16/4 PCI Mk2
 *
 *  Maintainer(s):
 *    AF	Adam Fritzler		mid@auk.cx
 *
 *  Modification History:
 *	30-Dec-99	AF	Split off from the tms380tr driver.
 *	22-Jan-00	AF	Updated to use indirect read/writes 
 *
 *
 *  TODO:
 *	1. See if we can use MMIO instead of inb/outb/inw/outw
 *	2. Add support for Mk1 (has AT24 attached to the PCI
 *		config registers)
 *
 */
static const char *version = "abyss.c: v1.01 22/01/2000 by Adam Fritzler\n";

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
#include "abyss.h"            /* Madge-specific constants */

#define ABYSS_IO_EXTENT 64

int abyss_probe(void);
static int abyss_open(struct net_device *dev);
static int abyss_close(struct net_device *dev);
static void abyss_enable(struct net_device *dev);
static int abyss_chipset_init(struct net_device *dev);
static void abyss_read_eeprom(struct net_device *dev);
static unsigned short abyss_setnselout_pins(struct net_device *dev);

void at24_writedatabyte(unsigned long regaddr, unsigned char byte);
int at24_sendfullcmd(unsigned long regaddr, unsigned char cmd, unsigned char addr);
int at24_sendcmd(unsigned long regaddr, unsigned char cmd);
unsigned char at24_readdatabit(unsigned long regaddr);
unsigned char at24_readdatabyte(unsigned long regaddr);
int at24_waitforack(unsigned long regaddr);
int at24_waitfornack(unsigned long regaddr);
void at24_setlines(unsigned long regaddr, unsigned char clock, unsigned char data);
void at24_start(unsigned long regaddr);
void at24_stop(unsigned long regaddr);
unsigned char at24_readb(unsigned long regaddr, unsigned char addr);

static unsigned short abyss_sifreadb(struct net_device *dev, unsigned short reg)
{
	return inb(dev->base_addr + reg);
}

static unsigned short abyss_sifreadw(struct net_device *dev, unsigned short reg)
{
	return inw(dev->base_addr + reg);
}

static void abyss_sifwriteb(struct net_device *dev, unsigned short val, unsigned short reg)
{
	outb(val, dev->base_addr + reg);
}

static void abyss_sifwritew(struct net_device *dev, unsigned short val, unsigned short reg)
{
	outw(val, dev->base_addr + reg);
}

struct tms_abyss_card {
	struct net_device *dev;
	struct pci_dev *pci_dev;
	struct tms_abyss_card *next;
};
static struct tms_abyss_card *abyss_card_list = NULL;

int __init abyss_probe(void)
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
		struct tms_abyss_card *card;
		
		/* We only support Madge Smart 16/4 PCI Mk2 (Abyss) cards */
		if ( (pdev->vendor != PCI_VENDOR_ID_MADGE) ||
		     (pdev->device != PCI_DEVICE_ID_MADGE_MK2) )
			continue;
		
		if (versionprinted++ == 0)
			printk("%s", version);

		pci_enable_device(pdev);

		/* Remove I/O space marker in bit 0. */
		pci_irq_line = pdev->irq;
		pci_ioaddr = pdev->resource[0].start ; 
		
		if(check_region(pci_ioaddr, ABYSS_IO_EXTENT))
			continue;
		
		/* At this point we have found a valid card. */
		
		dev = init_trdev(NULL, 0);
		
		request_region(pci_ioaddr, ABYSS_IO_EXTENT, "abyss");
		if(request_irq(pdev->irq, tms380tr_interrupt, SA_SHIRQ,
			       "abyss", dev)) { 
			release_region(pci_ioaddr, ABYSS_IO_EXTENT) ; 
			continue; /*return (-ENODEV);*/ /* continue; ?? */
		}
		
		/*
		  if (load_tms380_module("abyss.c")) {
		  return 0;
		  }
		*/

		pci_ioaddr &= ~3 ; 
		dev->base_addr	= pci_ioaddr;
		dev->irq 		= pci_irq_line;
		dev->dma		= 0;
		
		printk("%s: Madge Smart 16/4 PCI Mk2 (Abyss)\n", dev->name);
		printk("%s:    IO: %#4lx  IRQ: %d\n",
		       dev->name, pci_ioaddr, dev->irq);
		/*
		 * The TMS SIF registers lay 0x10 above the card base address.
		 */
		dev->base_addr += 0x10;
		
		if (tmsdev_init(dev)) {
			printk("%s: unable to get memory for dev->priv.\n", 
			       dev->name);
			return 0;
		}

		abyss_read_eeprom(dev);
		
		printk("%s:    Ring Station Address: ", dev->name);
		printk("%2.2x", dev->dev_addr[0]);
		for (i = 1; i < 6; i++)
			printk(":%2.2x", dev->dev_addr[i]);
		printk("\n");

		tp = (struct net_local *)dev->priv;
		tp->dmalimit = 0; /* XXX: should be the max PCI32 DMA max */
		tp->setnselout = abyss_setnselout_pins;
		tp->sifreadb = abyss_sifreadb;
		tp->sifreadw = abyss_sifreadw;
		tp->sifwriteb = abyss_sifwriteb;
		tp->sifwritew = abyss_sifwritew;

		memcpy(tp->ProductID, "Madge PCI 16/4 Mk2", PROD_ID_SIZE + 1);
		
		dev->open = abyss_open;
		dev->stop = abyss_close;
		
		if (register_trdev(dev) == 0) {
			/* Enlist in the card list */
			card = kmalloc(sizeof(struct tms_abyss_card), 
				       GFP_KERNEL);
			card->next = abyss_card_list;
			abyss_card_list = card;
			card->dev = dev;
			card->pci_dev = pdev;
		} else {
			printk("abyss: register_trdev() returned non-zero.\n");
			kfree(dev->priv);
			kfree(dev);
			return -1;
		}
	}
	
	if (abyss_card_list)
		return 0;
	return (-1);
}

unsigned short abyss_setnselout_pins(struct net_device *dev)
{
	unsigned short val = 0;
	struct net_local *tp = (struct net_local *)dev->priv;
	
	if(tp->DataRate == SPEED_4)
		val |= 0x01;  /* Set 4Mbps */
	else
		val |= 0x00;  /* Set 16Mbps */
	
	return val;
}

/*
 * The following Madge boards should use this code:
 *   - Smart 16/4 PCI Mk2 (Abyss)
 *   - Smart 16/4 PCI Mk1 (PCI T)
 *   - Smart 16/4 Client Plus PnP (Big Apple)
 *   - Smart 16/4 Cardbus Mk2
 *
 * These access an Atmel AT24 SEEPROM using their glue chip registers. 
 *
 */
void at24_writedatabyte(unsigned long regaddr, unsigned char byte)
{
	int i;
	
	for (i = 0; i < 8; i++) {
		at24_setlines(regaddr, 0, (byte >> (7-i))&0x01);
		at24_setlines(regaddr, 1, (byte >> (7-i))&0x01);
		at24_setlines(regaddr, 0, (byte >> (7-i))&0x01);
	}
}

int at24_sendfullcmd(unsigned long regaddr, unsigned char cmd, unsigned char addr)
{
	if (at24_sendcmd(regaddr, cmd)) {
		at24_writedatabyte(regaddr, addr);
		return at24_waitforack(regaddr);
	}
	return 0;
}

int at24_sendcmd(unsigned long regaddr, unsigned char cmd)
{
	int i;
	
	for (i = 0; i < 10; i++) {
		at24_start(regaddr);
		at24_writedatabyte(regaddr, cmd);
		if (at24_waitforack(regaddr))
			return 1;
	}
	return 0;
}

unsigned char at24_readdatabit(unsigned long regaddr)
{
	unsigned char val;

	at24_setlines(regaddr, 0, 1);
	at24_setlines(regaddr, 1, 1);
	val = (inb(regaddr) & AT24_DATA)?1:0;
	at24_setlines(regaddr, 1, 1);
	at24_setlines(regaddr, 0, 1);
	return val;
}

unsigned char at24_readdatabyte(unsigned long regaddr)
{
	unsigned char data = 0;
	int i;
	
	for (i = 0; i < 8; i++) {
		data <<= 1;
		data |= at24_readdatabit(regaddr);
	}

	return data;
}

int at24_waitforack(unsigned long regaddr)
{
	int i;
	
	for (i = 0; i < 10; i++) {
		if ((at24_readdatabit(regaddr) & 0x01) == 0x00)
			return 1;
	}
	return 0;
}

int at24_waitfornack(unsigned long regaddr)
{
	int i;
	for (i = 0; i < 10; i++) {
		if ((at24_readdatabit(regaddr) & 0x01) == 0x01)
			return 1;
	}
	return 0;
}

void at24_setlines(unsigned long regaddr, unsigned char clock, unsigned char data)
{
	unsigned char val;
	val = AT24_ENABLE;
	if (clock)
		val |= AT24_CLOCK;
	if (data)
		val |= AT24_DATA;

	outb(val, regaddr); 
	tms380tr_wait(20); /* Very necessary. */
}

void at24_start(unsigned long regaddr)
{
	at24_setlines(regaddr, 0, 1);
	at24_setlines(regaddr, 1, 1);
	at24_setlines(regaddr, 1, 0);
	at24_setlines(regaddr, 0, 1);
	return;
}

void at24_stop(unsigned long regaddr)
{
	at24_setlines(regaddr, 0, 0);
	at24_setlines(regaddr, 1, 0);
	at24_setlines(regaddr, 1, 1);
	at24_setlines(regaddr, 0, 1);
	return;
}
     
unsigned char at24_readb(unsigned long regaddr, unsigned char addr)
{
	unsigned char data = 0xff;
	
	if (at24_sendfullcmd(regaddr, AT24_WRITE, addr)) {
		if (at24_sendcmd(regaddr, AT24_READ)) {
			data = at24_readdatabyte(regaddr);
			if (!at24_waitfornack(regaddr))
				data = 0xff;
		}
	}
	return data;
}


/*
 * Enable basic functions of the Madge chipset needed
 * for initialization.
 */
static void abyss_enable(struct net_device *dev)
{
	unsigned char reset_reg;
	unsigned long ioaddr;
	
	ioaddr = dev->base_addr;
	reset_reg = inb(ioaddr + PCIBM2_RESET_REG);
	reset_reg |= PCIBM2_RESET_REG_CHIP_NRES;
	outb(reset_reg, ioaddr + PCIBM2_RESET_REG);
	tms380tr_wait(100);
	return;
}

/*
 * Enable the functions of the Madge chipset needed for
 * full working order. 
 */
static int abyss_chipset_init(struct net_device *dev)
{
	unsigned char reset_reg;
	unsigned long ioaddr;
	
	ioaddr = dev->base_addr;
	
	reset_reg = inb(ioaddr + PCIBM2_RESET_REG);
	
	reset_reg |= PCIBM2_RESET_REG_CHIP_NRES;
	outb(reset_reg, ioaddr + PCIBM2_RESET_REG);
	
	reset_reg &= ~(PCIBM2_RESET_REG_CHIP_NRES |
		       PCIBM2_RESET_REG_FIFO_NRES | 
		       PCIBM2_RESET_REG_SIF_NRES);
	outb(reset_reg, ioaddr + PCIBM2_RESET_REG);
	
	tms380tr_wait(100);
	
	reset_reg |= PCIBM2_RESET_REG_CHIP_NRES;
	outb(reset_reg, ioaddr + PCIBM2_RESET_REG);
	
	reset_reg |= PCIBM2_RESET_REG_SIF_NRES;
	outb(reset_reg, ioaddr + PCIBM2_RESET_REG);

	reset_reg |= PCIBM2_RESET_REG_FIFO_NRES;
	outb(reset_reg, ioaddr + PCIBM2_RESET_REG);

	outb(PCIBM2_INT_CONTROL_REG_SINTEN | 
	     PCIBM2_INT_CONTROL_REG_PCI_ERR_ENABLE, 
	     ioaddr + PCIBM2_INT_CONTROL_REG);
  
	outb(30, ioaddr + PCIBM2_FIFO_THRESHOLD);
	
	return 0;
}

void abyss_chipset_close(struct net_device *dev)
{
	unsigned long ioaddr;
	
	ioaddr = dev->base_addr;
	outb(0, ioaddr + PCIBM2_RESET_REG);
	
	return;
}

/*
 * Read configuration data from the AT24 SEEPROM on Madge cards.
 *
 */
static void abyss_read_eeprom(struct net_device *dev)
{
	struct net_local *tp;
	unsigned long ioaddr;
	unsigned short val;
	int i;
	
	tp = (struct net_local *)dev->priv;
	ioaddr = dev->base_addr;
	
	/* Must enable glue chip first */
	abyss_enable(dev);
	
	val = at24_readb(ioaddr + PCIBM2_SEEPROM_REG, 
			 PCIBM2_SEEPROM_RING_SPEED);
	tp->DataRate = val?SPEED_4:SPEED_16; /* set open speed */
	printk("%s:    SEEPROM: ring speed: %dMb/sec\n", dev->name, tp->DataRate);
	
	val = at24_readb(ioaddr + PCIBM2_SEEPROM_REG,
			 PCIBM2_SEEPROM_RAM_SIZE) * 128;
	printk("%s:    SEEPROM: adapter RAM: %dkb\n", dev->name, val);
	
	dev->addr_len = 6;
	for (i = 0; i < 6; i++) 
		dev->dev_addr[i] = at24_readb(ioaddr + PCIBM2_SEEPROM_REG, 
					      PCIBM2_SEEPROM_BIA+i);
	
	return;
}

static int abyss_open(struct net_device *dev)
{  
	abyss_chipset_init(dev);
	tms380tr_open(dev);
	MOD_INC_USE_COUNT;
	return 0;
}

static int abyss_close(struct net_device *dev)
{
	tms380tr_close(dev);
	abyss_chipset_close(dev);
	MOD_DEC_USE_COUNT;
	return 0;
}

#ifdef MODULE

int init_module(void)
{
	/* Probe for cards. */
	if (abyss_probe()) {
		printk(KERN_NOTICE "abyss.c: No cards found.\n");
	}
	/* lock_tms380_module(); */
	return (0);
}

void cleanup_module(void)
{
	struct net_device *dev;
	struct tms_abyss_card *this_card;
	
	while (abyss_card_list) {
		dev = abyss_card_list->dev;
		unregister_netdev(dev);
		release_region(dev->base_addr-0x10, ABYSS_IO_EXTENT);
		free_irq(dev->irq, dev);
		kfree(dev->priv);
		kfree(dev);
		this_card = abyss_card_list;
		abyss_card_list = this_card->next;
		kfree(this_card);
	}
	/* unlock_tms380_module(); */
}
#endif /* MODULE */


/*
 * Local variables:
 *  compile-command: "gcc -DMODVERSIONS  -DMODULE -D__KERNEL__ -Wall -Wstrict-prototypes -O6 -fomit-frame-pointer -I/usr/src/linux/drivers/net/tokenring/ -c abyss.c"
 *  alt-compile-command: "gcc -DMODULE -D__KERNEL__ -Wall -Wstrict-prototypes -O6 -fomit-frame-pointer -I/usr/src/linux/drivers/net/tokenring/ -c abyss.c"
 *  c-set-style "K&R"
 *  c-indent-level: 8
 *  c-basic-offset: 8
 *  tab-width: 8
 * End:
 */
