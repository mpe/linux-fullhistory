/* sis900.c: A SiS 900/7016 PCI Fast Ethernet driver for Linux.
   Copyright 1999 Silicon Integrated System Corporation 
   Revision:	1.06.04	Feb 11 2000
   
   Modified from the driver which is originally written by Donald Becker. 
   
   This software may be used and distributed according to the terms
   of the GNU Public License (GPL), incorporated herein by reference.
   Drivers based on this skeleton fall under the GPL and must retain
   the authorship (implicit copyright) notice.
   
   References:
   SiS 7016 Fast Ethernet PCI Bus 10/100 Mbps LAN Controller with OnNow Support,
   preliminary Rev. 1.0 Jan. 14, 1998
   SiS 900 Fast Ethernet PCI Bus 10/100 Mbps LAN Single Chip with OnNow Support,
   preliminary Rev. 1.0 Nov. 10, 1998
   SiS 7014 Single Chip 100BASE-TX/10BASE-T Physical Layer Solution,
   preliminary Rev. 1.0 Jan. 18, 1998
   http://www.sis.com.tw/support/databook.htm

   Rev 1.06.04 Feb. 11 2000 Jeff Garzik <jgarzik@mandrakesoft.com> softnet and init for kernel 2.4
   Rev 1.06.03 Dec. 23 1999 Ollie Lho Third release
   Rev 1.06.02 Nov. 23 1999 Ollie Lho bug in mac probing fixed 
   Rev 1.06.01 Nov. 16 1999 Ollie Lho CRC calculation provide by Joseph Zbiciak (im14u2c@primenet.com)
   Rev 1.06 Nov. 4 1999 Ollie Lho (ollie@sis.com.tw) Second release
   Rev 1.05.05 Oct. 29 1999 Ollie Lho (ollie@sis.com.tw) Single buffer Tx/Rx  
   Chin-Shan Li (lcs@sis.com.tw) Added AMD Am79c901 HomePNA PHY support
   Rev 1.05 Aug. 7 1999 Jim Huang (cmhuang@sis.com.tw) Initial release
*/

#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/init.h>

#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <asm/processor.h>      /* Processor type for cache alignment. */
#include <asm/bitops.h>
#include <asm/io.h>
#include <linux/delay.h>

#include "sis900.h"

static const char *version =
"sis900.c: v1.06.04  02/11/2000\n";

static int max_interrupt_work = 20;
#define sis900_debug debug
static int sis900_debug = 0;

static int multicast_filter_limit = 128;

/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT  (4*HZ)

struct mac_chip_info {
	const char *name;
	u16 	vendor_id, device_id, flags;
	int 	io_size;
	struct net_device *(*probe) (struct mac_chip_info *mac, struct pci_dev * pci_dev, 
				 struct net_device * net_dev);
};
static struct net_device * sis900_mac_probe (struct mac_chip_info * mac, struct pci_dev * pci_dev,
					 struct net_device * net_dev);

static struct mac_chip_info  mac_chip_table[] = {
	{ "SiS 900 PCI Fast Ethernet", PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_900,
	  PCI_COMMAND_IO|PCI_COMMAND_MASTER, SIS900_TOTAL_SIZE, sis900_mac_probe},
	{ "SiS 7016 PCI Fast Ethernet",PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_7016,
	  PCI_COMMAND_IO|PCI_COMMAND_MASTER, SIS900_TOTAL_SIZE, sis900_mac_probe},
	{0,},                                          /* 0 terminated list. */
};

static void sis900_read_mode(struct net_device *net_dev, int phy_addr, int *speed, int *duplex);
static void amd79c901_read_mode(struct net_device *net_dev, int phy_addr, int *speed, int *duplex);

static struct mii_chip_info {
	const char * name;
	u16 phy_id0;
	u16 phy_id1;
	void (*read_mode) (struct net_device *net_dev, int phy_addr, int *speed, int *duplex);
} mii_chip_table[] = {
	{"SiS 900 Internal MII PHY", 0x001d, 0x8000, sis900_read_mode},
	{"SiS 7014 Physical Layer Solution", 0x0016, 0xf830,sis900_read_mode},
	{"AMD 79C901 10BASE-T PHY",  0x0000, 0x35b9, amd79c901_read_mode},
	{"AMD 79C901 HomePNA PHY",   0x0000, 0x35c8, amd79c901_read_mode},
	{0,},
};

struct mii_phy {
	struct mii_phy * next;
	struct mii_chip_info * chip_info;
	int phy_addr;
	u16 status;
};

typedef struct _BufferDesc {
	u32     link;
	u32     cmdsts;
	u32     bufptr;
} BufferDesc;

struct sis900_private {
	struct net_device *next_module;
	struct net_device_stats stats;
	struct pci_dev * pci_dev;
	
	spinlock_t lock;

	struct mac_chip_info * mac;
	struct mii_phy * mii;
	unsigned int cur_phy;

	struct timer_list timer;        		/* Link status detection timer. */
	unsigned int cur_rx, dirty_rx;		
	unsigned int cur_tx, dirty_tx;

	/* The saved address of a sent/receive-in-place packet buffer */
	struct sk_buff *tx_skbuff[NUM_TX_DESC];
	struct sk_buff *rx_skbuff[NUM_RX_DESC];
	BufferDesc tx_ring[NUM_TX_DESC];	
	BufferDesc rx_ring[NUM_RX_DESC];
	unsigned int tx_full;			/* The Tx queue is full.    */

	int LinkOn;
};

MODULE_AUTHOR("Jim Huang <cmhuang@sis.com.tw>, Ollie Lho <ollie@sis.com.tw>");
MODULE_DESCRIPTION("SiS 900 PCI Fast Ethernet driver");
MODULE_PARM(multicast_filter_limit, "i");
MODULE_PARM(max_interrupt_work, "i");
MODULE_PARM(debug, "i");

static int sis900_open(struct net_device *net_dev);
static int sis900_mii_probe (struct net_device * net_dev);
static void sis900_init_rxfilter (struct net_device * net_dev);
static u16 read_eeprom(long ioaddr, int location);
static u16 mdio_read(struct net_device *net_dev, int phy_id, int location);
static void mdio_write(struct net_device *net_dev, int phy_id, int location, int val);
static void sis900_timer(unsigned long data);
static void sis900_check_mode (struct net_device *net_dev, struct mii_phy *mii_phy);
static void sis900_tx_timeout(struct net_device *net_dev);
static void sis900_init_tx_ring(struct net_device *net_dev);
static void sis900_init_rx_ring(struct net_device *net_dev);
static int sis900_start_xmit(struct sk_buff *skb, struct net_device *net_dev);
static int sis900_rx(struct net_device *net_dev);
static void sis900_finish_xmit (struct net_device *net_dev);
static void sis900_interrupt(int irq, void *dev_instance, struct pt_regs *regs);
static int sis900_close(struct net_device *net_dev);
static int mii_ioctl(struct net_device *net_dev, struct ifreq *rq, int cmd);
static struct enet_statistics *sis900_get_stats(struct net_device *net_dev);
static u16 sis900_compute_hashtable_index(u8 *addr);
static void set_rx_mode(struct net_device *net_dev);
static void sis900_reset(struct net_device *net_dev);

/* A list of all installed SiS900 devices, for removing the driver module. */
static struct net_device *root_sis900_dev = NULL;

/* walk through every ethernet PCI devices to see if some of them are matched with our card list*/
static int __init sis900_probe (void)
{
	int found = 0;
	struct pci_dev * pci_dev = NULL;
		
	while ((pci_dev = pci_find_class (PCI_CLASS_NETWORK_ETHERNET << 8, pci_dev)) != NULL) {
		/* pci_dev contains all ethernet devices */
		u32 pci_io_base;
		struct mac_chip_info * mac;
		struct net_device *net_dev = NULL;

		for (mac = mac_chip_table; mac->vendor_id; mac++) {
			/* try to match our card list */
			if (pci_dev->vendor == mac->vendor_id &&
			    pci_dev->device == mac->device_id)
				break;
		}
		
		if (mac->vendor_id == 0)
			/* pci_dev does not match any of our cards */
			continue;
		
		/* now, pci_dev should be either 900 or 7016 */
		pci_io_base = pci_dev->resource[0].start;
		if ((mac->flags & PCI_COMMAND_IO ) && 
		    check_region(pci_io_base, mac->io_size))
			continue;
		
		/* setup various bits in PCI command register */
		pci_enable_device (pci_dev);
		pci_set_master(pci_dev);

		/* do the real low level jobs */
		net_dev = mac->probe(mac, pci_dev, net_dev);
		
		if (net_dev != NULL) {
			found++;
		}
		net_dev = NULL;
	}
	return found ? 0 : -ENODEV;
}

static struct net_device * sis900_mac_probe (struct mac_chip_info * mac, struct pci_dev * pci_dev, 
					 struct net_device * net_dev)
{
	struct sis900_private *sis_priv;
	long ioaddr = pci_dev->resource[0].start; 
	int irq = pci_dev->irq;
	static int did_version = 0;
	u16 signature;
	int i;

	if (did_version++ == 0)
		printk(KERN_INFO "%s", version);

	if ((net_dev = init_etherdev(net_dev, 0)) == NULL)
		return NULL;
	/* check to see if we have sane EEPROM */
	signature = (u16) read_eeprom(ioaddr, EEPROMSignature);    
	if (signature == 0xffff || signature == 0x0000) {
		printk (KERN_INFO "%s: Error EERPOM read %x\n", 
			net_dev->name, signature);
		return NULL;
	}

	printk(KERN_INFO "%s: %s at %#lx, IRQ %d, ", net_dev->name, mac->name,
	       ioaddr, irq);

	/* get MAC address from EEPROM */
	for (i = 0; i < 3; i++)
		((u16 *)(net_dev->dev_addr))[i] = read_eeprom(ioaddr, i+EEPROMMACAddr);
	for (i = 0; i < 5; i++)
		printk("%2.2x:", (u8)net_dev->dev_addr[i]);
	printk("%2.2x.\n", net_dev->dev_addr[i]);

	if ((net_dev->priv = kmalloc(sizeof(struct sis900_private), GFP_KERNEL)) == NULL) {
		unregister_netdevice(net_dev);
		return NULL;
	}

	sis_priv = net_dev->priv;
	memset(sis_priv, 0, sizeof(struct sis900_private));

	/* We do a request_region() to register /proc/ioports info. */
	request_region(ioaddr, mac->io_size, net_dev->name);
	net_dev->base_addr = ioaddr;
	net_dev->irq = irq;
	sis_priv->pci_dev = pci_dev;
	sis_priv->mac = mac;
	spin_lock_init(&sis_priv->lock);

	/* probe for mii transciver */
	if (sis900_mii_probe(net_dev) == 0) {
		unregister_netdev(net_dev);
		kfree(sis_priv);
		release_region(ioaddr, mac->io_size);
		return NULL;
	}

	sis_priv->next_module = root_sis900_dev;
	root_sis900_dev = net_dev;

	/* The SiS900-specific entries in the device structure. */
	net_dev->open = &sis900_open;
	net_dev->hard_start_xmit = &sis900_start_xmit;
	net_dev->stop = &sis900_close;
	net_dev->get_stats = &sis900_get_stats;
	net_dev->set_multicast_list = &set_rx_mode;
	net_dev->do_ioctl = &mii_ioctl;
	net_dev->tx_timeout = sis900_tx_timeout;
	net_dev->watchdog_timeo = TX_TIMEOUT;

	return net_dev;
}

static int sis900_mii_probe (struct net_device * net_dev)
{
	struct sis900_private * sis_priv = (struct sis900_private *)net_dev->priv;
	int phy_addr;
	
	sis_priv->mii = NULL;
	
	/* search for total of 32 possible mii phy addresses */
	for (phy_addr = 0; phy_addr < 32; phy_addr++) {
		u16 mii_status;
		u16 phy_id0, phy_id1;
		int i;
		
		mii_status = mdio_read(net_dev, phy_addr, MII_STATUS);
		if (mii_status == 0xffff || mii_status == 0x0000)
			/* the mii is not accessable, try next one */
			continue;
		
		phy_id0 = mdio_read(net_dev, phy_addr, MII_PHY_ID0);
		phy_id1 = mdio_read(net_dev, phy_addr, MII_PHY_ID1);
		
		/* search our mii table for the current mii */ 
		for (i = 0; mii_chip_table[i].phy_id1; i++)
			if (phy_id0 == mii_chip_table[i].phy_id0) {
				struct mii_phy * mii_phy;
				
				printk(KERN_INFO 
				       "%s: %s transceiver found at address %d.\n",
				       net_dev->name, mii_chip_table[i].name, 
				       phy_addr);;
				if ((mii_phy = kmalloc(sizeof(struct mii_phy), GFP_KERNEL)) != NULL) {
					mii_phy->chip_info = mii_chip_table+i;
					mii_phy->phy_addr = phy_addr;
					mii_phy->status = mdio_read(net_dev, phy_addr, 
								    MII_STATUS);
					mii_phy->next = sis_priv->mii;
					sis_priv->mii = mii_phy;
				}
				/* the current mii is on our mii_info_table, 
				   try next address */
				break;
			}
	}
	
	if (sis_priv->mii == NULL) {
		printk(KERN_INFO "%s: No MII transceivers found!\n", 
		       net_dev->name);
		return 0;
	}

	/* arbitrary choose that last PHY and current PHY */
	sis_priv->cur_phy = sis_priv->mii->phy_addr;
	printk(KERN_INFO "%s: Using %s as default\n", net_dev->name,
	       sis_priv->mii->chip_info->name);

	if (sis_priv->mii->status & MII_STAT_LINK) 
		sis_priv->LinkOn = TRUE;
	else
		sis_priv->LinkOn = FALSE;

	return 1;
}

/* Delay between EEPROM clock transitions. */
#define eeprom_delay()  inl(ee_addr)

/* Read Serial EEPROM through EEPROM Access Register, Note that location is 
   in word (16 bits) unit */
static u16 read_eeprom(long ioaddr, int location)
{
	int i;
	u16 retval = 0;
	long ee_addr = ioaddr + mear;
	u32 read_cmd = location | EEread;

	outl(0, ee_addr);
	eeprom_delay();
	outl(EECLK, ee_addr);
	eeprom_delay();

	/* Shift the read command (9) bits out. */
	for (i = 8; i >= 0; i--) {
		u32 dataval = (read_cmd & (1 << i)) ? EEDI | EECS : EECS;
		outl(dataval, ee_addr);
		eeprom_delay();
		outl(dataval | EECLK, ee_addr);
		eeprom_delay();
	}
	outb(EECS, ee_addr);
	eeprom_delay();

	/* read the 16-bits data in */
	for (i = 16; i > 0; i--) {
		outl(EECS, ee_addr);
		eeprom_delay();
		outl(EECS | EECLK, ee_addr);
		eeprom_delay();
		retval = (retval << 1) | ((inl(ee_addr) & EEDO) ? 1 : 0);
		eeprom_delay();
	}
		
	/* Terminate the EEPROM access. */
	outl(0, ee_addr);
	eeprom_delay();
	outl(EECLK, ee_addr);

	return (retval);
}

/* Read and write the MII management registers using software-generated
   serial MDIO protocol. Note that the command bits and data bits are
   send out seperately */
#define mdio_delay()    inl(mdio_addr)

static void mdio_idle(long mdio_addr)
{
	outl(MDIO | MDDIR, mdio_addr);
	mdio_delay();
	outl(MDIO | MDDIR | MDC, mdio_addr);
}

/* Syncronize the MII management interface by shifting 32 one bits out. */
static void mdio_reset(long mdio_addr)
{
	int i;

	for (i = 31; i >= 0; i--) {
		outl(MDDIR | MDIO, mdio_addr);
		mdio_delay();
		outl(MDDIR | MDIO | MDC, mdio_addr);
		mdio_delay();
	}
	return;
}

static u16 mdio_read(struct net_device *net_dev, int phy_id, int location)
{
	long mdio_addr = net_dev->base_addr + mear;
	int mii_cmd = MIIread|(phy_id<<MIIpmdShift)|(location<<MIIregShift);
	u16 retval = 0;
	int i;

	mdio_reset(mdio_addr);
	mdio_idle(mdio_addr);

	for (i = 15; i >= 0; i--) {
		int dataval = (mii_cmd & (1 << i)) ? MDDIR | MDIO : MDDIR;
		outl(dataval, mdio_addr);
		mdio_delay();
		outl(dataval | MDC, mdio_addr);
		mdio_delay();
	}

	/* Read the 16 data bits. */
	for (i = 16; i > 0; i--) {
		outl(0, mdio_addr);
		mdio_delay();
		retval = (retval << 1) | ((inl(mdio_addr) & MDIO) ? 1 : 0);
		outl(MDC, mdio_addr);
		mdio_delay();
	}
	return retval;
}

static void mdio_write(struct net_device *net_dev, int phy_id, int location, int value)
{
	long mdio_addr = net_dev->base_addr + mear;
	int mii_cmd = MIIwrite|(phy_id<<MIIpmdShift)|(location<<MIIregShift);
	int i;

	mdio_reset(mdio_addr);
	mdio_idle(mdio_addr);

	/* Shift the command bits out. */
	for (i = 15; i >= 0; i--) {
		int dataval = (mii_cmd & (1 << i)) ? MDDIR | MDIO : MDDIR;
		outb(dataval, mdio_addr);
		mdio_delay();
		outb(dataval | MDC, mdio_addr);
		mdio_delay();
	}
	mdio_delay();

	/* Shift the value bits out. */
	for (i = 15; i >= 0; i--) {
		int dataval = (value & (1 << i)) ? MDDIR | MDIO : MDDIR;
		outl(dataval, mdio_addr);
		mdio_delay();
		outl(dataval | MDC, mdio_addr);
	       	mdio_delay();
	}
	mdio_delay();
	
	/* Clear out extra bits. */
	for (i = 2; i > 0; i--) {
		outb(0, mdio_addr);
		mdio_delay();
		outb(MDC, mdio_addr);
		mdio_delay();
	}
	return;
}

static int
sis900_open(struct net_device *net_dev)
{
	struct sis900_private *sis_priv = (struct sis900_private *)net_dev->priv;
	long ioaddr = net_dev->base_addr;

	/* Soft reset the chip. */
	sis900_reset(net_dev);

	if (request_irq(net_dev->irq, &sis900_interrupt, SA_SHIRQ, net_dev->name, net_dev)) {
		return -EAGAIN;
	}

	MOD_INC_USE_COUNT;

	sis900_init_rxfilter(net_dev);

	sis900_init_tx_ring(net_dev);
	sis900_init_rx_ring(net_dev);

	set_rx_mode(net_dev);

	netif_start_queue(net_dev);

	/* Enable all known interrupts by setting the interrupt mask. */
	outl((RxSOVR|RxORN|RxERR|RxOK|TxURN|TxERR|TxIDLE), ioaddr + imr);
	outl(RxENA, ioaddr + cr);
	outl(IE, ioaddr + ier);

	sis900_check_mode(net_dev, sis_priv->mii);

	/* Set the timer to switch to check for link beat and perhaps switch
	   to an alternate media type. */
	init_timer(&sis_priv->timer);
	sis_priv->timer.expires = jiffies + HZ;
	sis_priv->timer.data = (unsigned long)net_dev;
	sis_priv->timer.function = &sis900_timer;
	add_timer(&sis_priv->timer);

	return 0;
}

/* set receive filter address to our MAC address */
static void
sis900_init_rxfilter (struct net_device * net_dev)
{
	long ioaddr = net_dev->base_addr;
	u32 rfcrSave;
	u32 i;
	
	rfcrSave = inl(rfcr + ioaddr);

	/* disable packet filtering before setting filter */
	outl(rfcrSave & ~RFEN, rfcr);

	/* load MAC addr to filter data register */
	for (i = 0 ; i < 3 ; i++) {
		u32 w;

		w = (u32) *((u16 *)(net_dev->dev_addr)+i);
		outl((i << RFADDR_shift), ioaddr + rfcr);
		outl(w, ioaddr + rfdr);

		if (sis900_debug > 2) {
			printk(KERN_INFO "%s: Receive Filter Addrss[%d]=%x\n",
			       net_dev->name, i, inl(ioaddr + rfdr));
		}
	}

	/* enable packet filitering */
	outl(rfcrSave | RFEN, rfcr + ioaddr);
}

/* Initialize the Tx ring. */
static void
sis900_init_tx_ring(struct net_device *net_dev)
{
	struct sis900_private *sis_priv = (struct sis900_private *)net_dev->priv;
	long ioaddr = net_dev->base_addr; 
	int i;
	
	sis_priv->tx_full = 0;
	sis_priv->dirty_tx = sis_priv->cur_tx = 0;
	
	for (i = 0; i < NUM_TX_DESC; i++) {
		sis_priv->tx_skbuff[i] = NULL;

		sis_priv->tx_ring[i].link = (u32) virt_to_bus(&sis_priv->tx_ring[i+1]);
		sis_priv->tx_ring[i].cmdsts = 0;
		sis_priv->tx_ring[i].bufptr = 0;
	}
	sis_priv->tx_ring[i-1].link = (u32) virt_to_bus(&sis_priv->tx_ring[0]);

	/* load Transmit Descriptor Register */
	outl(virt_to_bus(&sis_priv->tx_ring[0]), ioaddr + txdp); 
	if (sis900_debug > 2)
		printk(KERN_INFO "%s: TX descriptor register loaded with: %8.8x\n", 
		       net_dev->name, inl(ioaddr + txdp));
}

/* Initialize the Rx descriptor ring, pre-allocate recevie buffers */ 
static void 
sis900_init_rx_ring(struct net_device *net_dev) 
{ 
	struct sis900_private *sis_priv = (struct sis900_private *)net_dev->priv; 
	long ioaddr = net_dev->base_addr; 
	int i;
 
	sis_priv->cur_rx = 0; 
	sis_priv->dirty_rx = 0;

	/* init RX descriptor */
	for (i = 0; i < NUM_RX_DESC; i++) {
		sis_priv->rx_skbuff[i] = NULL;

		sis_priv->rx_ring[i].link = (u32) virt_to_bus(&sis_priv->rx_ring[i+1]);
		sis_priv->rx_ring[i].cmdsts = 0;
		sis_priv->rx_ring[i].bufptr = 0;
	}
	sis_priv->rx_ring[i-1].link = (u32) virt_to_bus(&sis_priv->rx_ring[0]);

	/* allocate sock buffers */
	for (i = 0; i < NUM_RX_DESC; i++) {
		struct sk_buff *skb;

		if ((skb = dev_alloc_skb(RX_BUF_SIZE)) == NULL) {
			/* not enough memory for skbuff, this makes a "hole"
			   on the buffer ring, it is not clear how the 
			   hardware will react to this kind of degenerated 
			   buffer */
			break;
		}
		skb->dev = net_dev;
		sis_priv->rx_skbuff[i] = skb;
		sis_priv->rx_ring[i].cmdsts = RX_BUF_SIZE;
		sis_priv->rx_ring[i].bufptr = virt_to_bus(skb->tail);
	}
	sis_priv->dirty_rx = (unsigned int) (i - NUM_RX_DESC);

	/* load Receive Descriptor Register */
	outl(virt_to_bus(&sis_priv->rx_ring[0]), ioaddr + rxdp);
	if (sis900_debug > 2)
		printk(KERN_INFO "%s: RX descriptor register loaded with: %8.8x\n", 
		       net_dev->name, inl(ioaddr + rxdp));
}
/* on each timer ticks we check two things, Link Status (ON/OFF) and 
   Link Mode (10/100/Full/Half)
 */
static void sis900_timer(unsigned long data)
{
	struct net_device *net_dev = (struct net_device *)data;
	struct sis900_private *sis_priv = (struct sis900_private *)net_dev->priv;
	struct mii_phy *mii_phy = sis_priv->mii;
	static int next_tick = 5*HZ;
	u16 status;

	status = mdio_read(net_dev, sis_priv->cur_phy, MII_STATUS);

	/* current mii phy is failed to link, try another one */
	while (!(status & MII_STAT_LINK)) {		
		if (mii_phy->next == NULL) { 
			if (sis_priv->LinkOn) {
				/* link stat change from ON to OFF */
				next_tick = HZ;
				sis_priv->LinkOn = FALSE;
				printk(KERN_INFO "%s: Media Link Off\n",
				       net_dev->name);
			}
			sis_priv->timer.expires = jiffies + next_tick;
			add_timer(&sis_priv->timer);
			return;
		}
		mii_phy = mii_phy->next;
		status = mdio_read(net_dev, mii_phy->phy_addr, MII_STATUS);
	}

	if (!sis_priv->LinkOn) {
		/* link stat change forn OFF to ON, read and report link mode */
		sis_priv->LinkOn = TRUE;
		next_tick = 5*HZ;
		/* change what cur_phy means */
		if (mii_phy->phy_addr != sis_priv->cur_phy) {
			printk(KERN_INFO "%s: Changing transceiver to %s\n",
			       net_dev->name, mii_phy->chip_info->name);
			status = mdio_read(net_dev, sis_priv->cur_phy, MII_CONTROL);
			mdio_write(net_dev, sis_priv->cur_phy, 
				   MII_CONTROL, status | MII_CNTL_ISOLATE);
			status = mdio_read(net_dev, mii_phy->phy_addr, MII_CONTROL);
			mdio_write(net_dev, mii_phy->phy_addr, 
				   MII_CONTROL, status & ~MII_CNTL_ISOLATE);
			sis_priv->cur_phy = mii_phy->phy_addr;
		}
		sis900_check_mode(net_dev, mii_phy);
	}

	sis_priv->timer.expires = jiffies + next_tick;
	add_timer(&sis_priv->timer);
}
static void sis900_check_mode (struct net_device *net_dev, struct mii_phy *mii_phy)
{
	struct sis900_private *sis_priv = (struct sis900_private *)net_dev->priv;
	long ioaddr = net_dev->base_addr;
	int speed, duplex;
	u32 tx_flags = 0, rx_flags = 0;

	mii_phy->chip_info->read_mode(net_dev, sis_priv->cur_phy, &speed, &duplex);

	tx_flags = TxATP | (TX_DMA_BURST << TxMXDMA_shift) | (TX_FILL_THRESH << TxFILLT_shift);
	rx_flags = RX_DMA_BURST << RxMXDMA_shift;

	if (speed == HW_SPEED_HOME || speed == HW_SPEED_10_MBPS ) {
		rx_flags |= (RxDRNT_10 << RxDRNT_shift);
		tx_flags |= (TxDRNT_10 << TxDRNT_shift);
	}
	else {
		rx_flags |= (RxDRNT_100 << RxDRNT_shift);
		tx_flags |= (TxDRNT_100 << TxDRNT_shift);
	}

	if (duplex == FDX_CAPABLE_FULL_SELECTED) {
		tx_flags |= (TxCSI | TxHBI);
		rx_flags |= RxATX;
	}

	outl (tx_flags, ioaddr + txcfg);
	outl (rx_flags, ioaddr + rxcfg);
}
static void sis900_read_mode(struct net_device *net_dev, int phy_addr, int *speed, int *duplex)
{
	int i = 0;
	u32 status;
	
	/* STSOUT register is Latched on Transition, read operation updates it */
	while (i++ < 2)
		status = mdio_read(net_dev, phy_addr, MII_STSOUT);

	if (status & MII_STSOUT_SPD)
		*speed = HW_SPEED_100_MBPS;
	else
		*speed = HW_SPEED_10_MBPS;

	if (status & MII_STSOUT_DPLX)
		*duplex = FDX_CAPABLE_FULL_SELECTED;
	else
		*duplex = FDX_CAPABLE_HALF_SELECTED;

	if (status & MII_STSOUT_LINK_FAIL)
		printk(KERN_INFO "%s: Media Link Off\n", net_dev->name);
	else
		printk(KERN_INFO "%s: Media Link On %s %s-duplex \n", 
		       net_dev->name,
		       *speed == HW_SPEED_100_MBPS ? 
		       "100mbps" : "10mbps",
		       *duplex == FDX_CAPABLE_FULL_SELECTED ?
		       "full" : "half");
}
static void amd79c901_read_mode(struct net_device *net_dev, int phy_addr, int *speed, int *duplex)
{
	int i;
	u16 status;
	
	for (i = 0; i < 2; i++)
		status = mdio_read(net_dev, phy_addr, MII_STATUS);

	if (status & MII_STAT_CAN_AUTO) {
		/* 10BASE-T PHY */
		for (i = 0; i < 2; i++)
			status = mdio_read(net_dev, phy_addr, MII_STATUS_SUMMARY);
		if (status & MII_STSSUM_SPD)
			*speed = HW_SPEED_100_MBPS;
		else
			*speed = HW_SPEED_10_MBPS;
		if (status & MII_STSSUM_DPLX)
			*duplex = FDX_CAPABLE_FULL_SELECTED;
		else
			*duplex = FDX_CAPABLE_HALF_SELECTED;

		if (status & MII_STSSUM_LINK)
			printk(KERN_INFO "%s: Media Link On %s %s-duplex \n", 
			       net_dev->name,
			       *speed == HW_SPEED_100_MBPS ? 
			       "100mbps" : "10mbps",
			       *duplex == FDX_CAPABLE_FULL_SELECTED ?
			       "full" : "half");
		else
			printk(KERN_INFO "%s: Media Link Off\n", net_dev->name);
			
	}
	else {
		/* HomePNA */
		*speed = HW_SPEED_HOME;
		*duplex = FDX_CAPABLE_HALF_SELECTED;
		if (status & MII_STAT_LINK)
			printk(KERN_INFO "%s: Media Link On 1mbps half-duplex \n", 
			       net_dev->name);
		else
			printk(KERN_INFO "%s: Media Link Off\n", net_dev->name);
	}
}
static void sis900_tx_timeout(struct net_device *net_dev)
{
	struct sis900_private *sis_priv = (struct sis900_private *)net_dev->priv;
	long ioaddr = net_dev->base_addr;
	unsigned long flags;
	int i;

	printk(KERN_INFO "%s: Transmit timeout, status %8.8x %8.8x \n",
	       net_dev->name, inl(ioaddr + cr), inl(ioaddr + isr));
	
	/* Disable interrupts by clearing the interrupt mask. */
	outl(0x0000, ioaddr + imr);

	/* use spinlock to prevent interrupt handler accessing buffer ring */
	spin_lock_irqsave(&sis_priv->lock, flags);

	/* discard unsent packets */
	sis_priv->dirty_tx = sis_priv->cur_tx = 0;
	for (i = 0; i < NUM_TX_DESC; i++) {
		if (sis_priv->tx_skbuff[i] != NULL) {
			dev_kfree_skb(sis_priv->tx_skbuff[i]);
			sis_priv->tx_skbuff[i] = 0;
			sis_priv->tx_ring[i].cmdsts = 0;
			sis_priv->tx_ring[i].bufptr = 0;
			sis_priv->stats.tx_dropped++;
		}
	}
	sis_priv->tx_full = 0;
	netif_wake_queue(net_dev);

	spin_unlock_irqrestore(&sis_priv->lock, flags);

	net_dev->trans_start = jiffies;

	/* FIXME: Should we restart the transmission thread here  ?? */
	outl(TxENA, ioaddr + cr);

	/* Enable all known interrupts by setting the interrupt mask. */
	outl((RxSOVR|RxORN|RxERR|RxOK|TxURN|TxERR|TxIDLE), ioaddr + imr);
	return;
}

static int
sis900_start_xmit(struct sk_buff *skb, struct net_device *net_dev)
{
	struct sis900_private *sis_priv = (struct sis900_private *)net_dev->priv;
	long ioaddr = net_dev->base_addr;
	unsigned int  entry;
	unsigned long flags;

	spin_lock_irqsave(&sis_priv->lock, flags);

	/* Calculate the next Tx descriptor entry. */
	entry = sis_priv->cur_tx % NUM_TX_DESC;
	sis_priv->tx_skbuff[entry] = skb;

	/* set the transmit buffer descriptor and enable Transmit State Machine */
	sis_priv->tx_ring[entry].bufptr = virt_to_bus(skb->data);
	sis_priv->tx_ring[entry].cmdsts = (OWN | skb->len);
	outl(TxENA, ioaddr + cr);

	if (++sis_priv->cur_tx - sis_priv->dirty_tx < NUM_TX_DESC) {
		/* Typical path, tell upper layer that more transmission is possible */
		netif_start_queue(net_dev);
	} else {
		/* buffer full, tell upper layer no more transmission */
		sis_priv->tx_full = 1;
		netif_stop_queue(net_dev);
	}

	spin_unlock_irqrestore(&sis_priv->lock, flags);

	net_dev->trans_start = jiffies;

	if (sis900_debug > 3)
		printk(KERN_INFO "%s: Queued Tx packet at %p size %d "
		       "to slot %d.\n",
		       net_dev->name, skb->data, (int)skb->len, entry);

	return 0;
}

/* The interrupt handler does all of the Rx thread work and cleans up
   after the Tx thread. */
static void sis900_interrupt(int irq, void *dev_instance, struct pt_regs *regs)
{
	struct net_device *net_dev = (struct net_device *)dev_instance;
	struct sis900_private *sis_priv = (struct sis900_private *)net_dev->priv;
	int boguscnt = max_interrupt_work;
	long ioaddr = net_dev->base_addr;
	u32 status;

	spin_lock (&sis_priv->lock);

	do {
		status = inl(ioaddr + isr);
		
		if ((status & (HIBERR|TxURN|TxERR|TxIDLE|RxORN|RxERR|RxOK)) == 0)
			/* nothing intresting happened */
			break;

		/* why dow't we break after Tx/Rx case ?? keyword: full-duplex */
		if (status & (RxORN | RxERR | RxOK))
			/* Rx interrupt */
			sis900_rx(net_dev);

		if (status & (TxURN | TxERR | TxIDLE))
			/* Tx interrupt */
			sis900_finish_xmit(net_dev);

		/* something strange happened !!! */
		if (status & HIBERR) {
			printk(KERN_INFO "%s: Abnormal interrupt,"
			       "status %#8.8x.\n", net_dev->name, status);
			break;
		}
		if (--boguscnt < 0) {
			printk(KERN_INFO "%s: Too much work at interrupt, "
			       "interrupt status = %#8.8x.\n",
			       net_dev->name, status);
			break;
		}
	} while (1);
	
	if (sis900_debug > 3)
		printk(KERN_INFO "%s: exiting interrupt, "
		       "interrupt status = 0x%#8.8x.\n",
		       net_dev->name, inl(ioaddr + isr));
	
	spin_unlock (&sis_priv->lock);
	return;
}

/* Process receive interrupt events, put buffer to higher layer and refill buffer pool 
   Note: This fucntion is called by interrupt handler, don't do "too much" work here */
static int sis900_rx(struct net_device *net_dev)
{
	struct sis900_private *sis_priv = (struct sis900_private *)net_dev->priv;
	long ioaddr = net_dev->base_addr;
	unsigned int entry = sis_priv->cur_rx % NUM_RX_DESC;
	u32 rx_status = sis_priv->rx_ring[entry].cmdsts;
	
	if (sis900_debug > 3)
		printk(KERN_INFO "sis900_rx, cur_rx:%4.4d, dirty_rx:%4.4d "
		       "status:0x%8.8x\n",
		       sis_priv->cur_rx, sis_priv->dirty_rx, rx_status);
	
	while (rx_status & OWN) {
		unsigned int rx_size;
		
		rx_size = (rx_status & DSIZE) - CRC_SIZE;
		
		if (rx_status & (ABORT|OVERRUN|TOOLONG|RUNT|RXISERR|CRCERR|FAERR)) {
			/* corrupted packet received */
			if (sis900_debug > 3)
				printk(KERN_INFO "%s: Corrupted packet "
				       "received, buffer status = 0x%8.8x.\n",
				       net_dev->name, rx_status);
			sis_priv->stats.rx_errors++;
			if (rx_status & OVERRUN)
				sis_priv->stats.rx_over_errors++;
			if (rx_status & (TOOLONG|RUNT))
				sis_priv->stats.rx_length_errors++;
			if (rx_status & (RXISERR | FAERR))
				sis_priv->stats.rx_frame_errors++;
			if (rx_status & CRCERR) 
				sis_priv->stats.rx_crc_errors++;
			/* reset buffer descriptor state */
			sis_priv->rx_ring[entry].cmdsts = RX_BUF_SIZE;
		} else {
			struct sk_buff * skb;
			
			/* This situation should never happen, but due to
			   some unknow bugs, it is possible that
			   we are working on NULL sk_buff :-( */			
			if (sis_priv->rx_skbuff[entry] == NULL) {
				printk(KERN_INFO "%s: NULL pointer " 
				       "encountered in Rx ring, skipping\n",
				       net_dev->name);
				break;			
			}
			skb = sis_priv->rx_skbuff[entry];
			sis_priv->rx_skbuff[entry] = NULL;
			/* reset buffer descriptor state */
			sis_priv->rx_ring[entry].cmdsts = 0;
			sis_priv->rx_ring[entry].bufptr = 0;
			
			skb_put(skb, rx_size);
			skb->protocol = eth_type_trans(skb, net_dev);
			netif_rx(skb);
			
			if ((rx_status & BCAST) == MCAST)
				sis_priv->stats.multicast++;
			net_dev->last_rx = jiffies;
			sis_priv->stats.rx_bytes += rx_size;
			sis_priv->stats.rx_packets++;
		}
		sis_priv->cur_rx++;
		entry = sis_priv->cur_rx % NUM_RX_DESC;
		rx_status = sis_priv->rx_ring[entry].cmdsts;
	} // while
	
	/* refill the Rx buffer, what if the rate of refilling is slower than 
	   consuming ?? */
	for (;sis_priv->cur_rx - sis_priv->dirty_rx > 0; sis_priv->dirty_rx++) {
		struct sk_buff *skb;
		
		entry = sis_priv->dirty_rx % NUM_RX_DESC;
		
		if (sis_priv->rx_skbuff[entry] == NULL) {
			if ((skb = dev_alloc_skb(RX_BUF_SIZE)) == NULL) {
				/* not enough memory for skbuff, this makes a "hole"
				   on the buffer ring, it is not clear how the 
				   hardware will react to this kind of degenerated 
				   buffer */
				printk(KERN_INFO "%s: Memory squeeze,"
				       "deferring packet.\n",
				       net_dev->name);
				sis_priv->stats.rx_dropped++;
				break;
			}
			skb->dev = net_dev;
			sis_priv->rx_skbuff[entry] = skb;
			sis_priv->rx_ring[entry].cmdsts = RX_BUF_SIZE;
			sis_priv->rx_ring[entry].bufptr = virt_to_bus(skb->tail);
		}
	}
	/* re-enable the potentially idle receive state matchine */
	outl(RxENA , ioaddr + cr );
	
	return 0;
}

/* finish up transmission of packets, check for error condition and free skbuff etc.
   Note: This fucntion is called by interrupt handler, don't do "too much" work here */
static void sis900_finish_xmit (struct net_device *net_dev)
{
	struct sis900_private *sis_priv = (struct sis900_private *)net_dev->priv;
	
	for (; sis_priv->dirty_tx < sis_priv->cur_tx; sis_priv->dirty_tx++) {
		unsigned int entry;
		u32 tx_status;
		
		entry = sis_priv->dirty_tx % NUM_TX_DESC;
		tx_status = sis_priv->tx_ring[entry].cmdsts;
		
		if (tx_status & OWN) {
			/* The packet is not transmited yet (owned by hardware) !
			   Note: the interrupt is generated only when Tx Machine
			   is idle, so this is an almost impossible case */
			break;
		}
		
		if (tx_status & (ABORT | UNDERRUN | OWCOLL)) {
			/* packet unsuccessfully transmited */
			if (sis900_debug > 3)
				printk(KERN_INFO "%s: Transmit "
				       "error, Tx status %8.8x.\n",
				       net_dev->name, tx_status);
			sis_priv->stats.tx_errors++;
			if (tx_status & UNDERRUN)
				sis_priv->stats.tx_fifo_errors++;
			if (tx_status & ABORT)
				sis_priv->stats.tx_aborted_errors++;
			if (tx_status & NOCARRIER)
				sis_priv->stats.tx_carrier_errors++;
			if (tx_status & OWCOLL)
				sis_priv->stats.tx_window_errors++;
		} else {
			/* packet successfully transmited */
			sis_priv->stats.collisions += (tx_status & COLCNT) >> 16;
			sis_priv->stats.tx_bytes += tx_status & DSIZE;
			sis_priv->stats.tx_packets++;
		}
		/* Free the original skb. */
		dev_kfree_skb_irq(sis_priv->tx_skbuff[entry]);
		sis_priv->tx_skbuff[entry] = NULL;
		sis_priv->tx_ring[entry].bufptr = 0;
		sis_priv->tx_ring[entry].cmdsts = 0;
	}
	
	if (sis_priv->tx_full && netif_queue_stopped(net_dev) && 
	    sis_priv->cur_tx - sis_priv->dirty_tx < NUM_TX_DESC - 4) {
		/* The ring is no longer full, clear tx_full and schedule more transmission
		   by netif_wake_queue(net_dev) */
		sis_priv->tx_full = 0;
		netif_wake_queue (net_dev);
	}
}

static int
sis900_close(struct net_device *net_dev)
{
	long ioaddr = net_dev->base_addr;
	struct sis900_private *sis_priv = (struct sis900_private *)net_dev->priv;
	int i;
	
	netif_stop_queue(net_dev);

	/* Disable interrupts by clearing the interrupt mask. */
	outl(0x0000, ioaddr + imr);
	outl(0x0000, ioaddr + ier);

	/* Stop the chip's Tx and Rx Status Machine */
	outl(RxDIS | TxDIS, ioaddr + cr);

	del_timer(&sis_priv->timer);

	free_irq(net_dev->irq, net_dev);

	/* Free Tx and RX skbuff */
	for (i = 0; i < NUM_RX_DESC; i++) {
		if (sis_priv->rx_skbuff[i] != NULL)
			dev_kfree_skb(sis_priv->rx_skbuff[i]);
		sis_priv->rx_skbuff[i] = 0;
	}
	for (i = 0; i < NUM_TX_DESC; i++) {
		if (sis_priv->tx_skbuff[i] != NULL)
			dev_kfree_skb(sis_priv->tx_skbuff[i]);
		sis_priv->tx_skbuff[i] = 0;
	}

	/* Green! Put the chip in low-power mode. */

	MOD_DEC_USE_COUNT;

	return 0;
}

static int mii_ioctl(struct net_device *net_dev, struct ifreq *rq, int cmd)
{
	struct sis900_private *sis_priv = (struct sis900_private *)net_dev->priv;
	u16 *data = (u16 *)&rq->ifr_data;

	switch(cmd) {
	case SIOCDEVPRIVATE:            	/* Get the address of the PHY in use. */
		data[0] = sis_priv->mii->phy_addr;
		/* Fall Through */
	case SIOCDEVPRIVATE+1:          	/* Read the specified MII register. */
		data[3] = mdio_read(net_dev, data[0] & 0x1f, data[1] & 0x1f);
		return 0;
	case SIOCDEVPRIVATE+2:          	/* Write the specified MII register */
		if (!suser())
			return -EPERM;
		mdio_write(net_dev, data[0] & 0x1f, data[1] & 0x1f, data[2]);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static struct enet_statistics *
sis900_get_stats(struct net_device *net_dev)
{
	struct sis900_private *sis_priv = (struct sis900_private *)net_dev->priv;

	return &sis_priv->stats;
}

/* SiS 900 uses the most sigificant 7 bits to index a 128 bits multicast hash table, which makes
   this function a little bit different from other drivers */
static u16 sis900_compute_hashtable_index(u8 *addr)
{

/* what is the correct value of the POLYNOMIAL ??
   Donald Becker use 0x04C11DB7U
   Joseph Zbiciak im14u2c@primenet.com gives me the
   correct answer, thank you Joe !! */
#define POLYNOMIAL 0x04C11DB7L
	u32 crc = 0xffffffff, msb;
	int  i, j;
	u32  byte;

	for (i = 0; i < 6; i++) {
		byte = *addr++;
		for (j = 0; j < 8; j++) {
			msb = crc >> 31;
			crc <<= 1;
			if (msb ^ (byte & 1)) {
				crc ^= POLYNOMIAL;
			}
			byte >>= 1;
		}
	}
	/* leave 7 most siginifant bits */ 
	return ((int)(crc >> 25));
}

static void set_rx_mode(struct net_device *net_dev)
{
	long ioaddr = net_dev->base_addr;
	u16 mc_filter[8];			/* 128 bits multicast hash table */
	int i;
	u32 rx_mode;

	if (net_dev->flags & IFF_PROMISC) {
		/* Accept any kinds of packets */
		rx_mode = RFPromiscuous;
		for (i = 0; i < 8; i++)
			mc_filter[i] = 0xffff;
	} else if ((net_dev->mc_count > multicast_filter_limit) ||
		   (net_dev->flags & IFF_ALLMULTI)) {
		/* too many multicast addresses or accept all multicast packet */
		rx_mode = RFAAB | RFAAM;
		for (i = 0; i < 8; i++)
			mc_filter[i] = 0xffff;
	} else {
		/* Accept Broadcast packet, destination address matchs our MAC address,
		   use Receive Filter to reject unwanted MCAST packet */
		struct dev_mc_list *mclist;
		rx_mode = RFAAB;
		for (i = 0; i < 8; i++)
			mc_filter[i]=0;
		for (i = 0, mclist = net_dev->mc_list; mclist && i < net_dev->mc_count;
		     i++, mclist = mclist->next)
			set_bit(sis900_compute_hashtable_index(mclist->dmi_addr),
				mc_filter);
	}

	/* update Multicast Hash Table in Receive Filter */
	for (i = 0; i < 8; i++) {
                /* why plus 0x04 ??, That makes the correct value for hash table. */
		outl((u32)(0x00000004+i) << RFADDR_shift, ioaddr + rfcr);
		outl(mc_filter[i], ioaddr + rfdr);
	}

	outl(RFEN | rx_mode, ioaddr + rfcr);

	/* sis900 is capatable of looping back packet at MAC level for debugging purpose */
	if (net_dev->flags & IFF_LOOPBACK) {
		u32 cr_saved;
		/* We must disable Tx/Rx before setting loopback mode */
		cr_saved = inl(ioaddr + cr);
		outl(cr_saved | TxDIS | RxDIS, ioaddr + cr);
		/* enable loopback */
		outl(inl(ioaddr + txcfg) | TxMLB, ioaddr + txcfg);
		outl(inl(ioaddr + rxcfg) | RxATX, ioaddr + rxcfg);
		/* restore cr */
		outl(cr_saved, ioaddr + cr);
	}		

	return;
}

static void sis900_reset(struct net_device *net_dev)
{
	long ioaddr = net_dev->base_addr;
	int i = 0;
	u32 status = TxRCMP | RxRCMP;

	outl(0, ioaddr + ier);
	outl(0, ioaddr + imr);
	outl(0, ioaddr + rfcr);

	outl(RxRESET | TxRESET | RESET, ioaddr + cr);
	
	/* Check that the chip has finished the reset. */
	while (status && (i++ < 1000)) {
		status ^= (inl(isr + ioaddr) & status);
	}

	outl(PESEL, ioaddr + cfg);
}

static void __exit sis900_cleanup_module(void)
{
	/* No need to check MOD_IN_USE, as sys_delete_module() checks. */
	while (root_sis900_dev) {
		struct sis900_private *sis_priv =
			(struct sis900_private *)root_sis900_dev->priv;
		struct net_device *next_dev = sis_priv->next_module;
		
		unregister_netdev(root_sis900_dev);
		release_region(root_sis900_dev->base_addr,
			       sis_priv->mac->io_size);
		kfree(sis_priv);
		kfree(root_sis900_dev);

		root_sis900_dev = next_dev;
	}
}

module_init(sis900_probe);
module_exit(sis900_cleanup_module);
