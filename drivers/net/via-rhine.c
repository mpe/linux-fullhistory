/* via-rhine.c: A Linux Ethernet device driver for VIA Rhine family chips. */
/*
	Written 1998-1999 by Donald Becker.

	This software may be used and distributed according to the terms
	of the GNU Public License (GPL), incorporated herein by reference.
	Drivers derived from this code also fall under the GPL and must retain
	this authorship and copyright notice.

	This driver is designed for the VIA VT86c100A Rhine-II PCI Fast Ethernet
	controller.  It also works with the older 3043 Rhine-I chip.

	The author may be reached as becker@cesdis.edu, or
	Donald Becker
	312 Severn Ave. #W302
	Annapolis MD 21403

	Support and updates available at
	http://cesdis.gsfc.nasa.gov/linux/drivers/via-rhine.html


	Linux kernel version history:
	
	LK1.1.0:
	- Jeff Garzik: softnet 'n stuff
	
	LK1.1.1:
	- Justin Guyett: softnet and locking fixes
	- Jeff Garzik: use PCI interface

	LK1.1.2:
	- Urban Widmark: minor cleanups, merges from Becker 1.03a/1.04 versions

*/

static const char *versionA =
"via-rhine.c:v1.03a-LK1.1.2  3/19/2000  Written by Donald Becker\n";
static const char *versionB =
"  http://cesdis.gsfc.nasa.gov/linux/drivers/via-rhine.html\n";

/* A few user-configurable values.   These may be modified when a driver
   module is loaded.*/

static int debug = 1;			/* 1 normal messages, 0 quiet .. 7 verbose. */
static int max_interrupt_work = 20;

/* Set the copy breakpoint for the copy-only-tiny-frames scheme.
   Setting to > 1518 effectively disables this feature. */
static int rx_copybreak = 0;

/* Used to pass the media type, etc.
   Both 'options[]' and 'full_duplex[]' should exist for driver
   interoperability.
   The media type is usually passed in 'options[]'.
*/
#define MAX_UNITS 8		/* More are supported, limit only on options */
static int options[MAX_UNITS] = {-1, -1, -1, -1, -1, -1, -1, -1};
static int full_duplex[MAX_UNITS] = {-1, -1, -1, -1, -1, -1, -1, -1};

/* Maximum number of multicast addresses to filter (vs. rx-all-multicast).
   The Rhine has a 64 element 8390-like hash table.  */
static const int multicast_filter_limit = 32;

/* Operational parameters that are set at compile time. */

/* Keep the ring sizes a power of two for compile efficiency.
   The compiler will convert <unsigned>'%'<2^N> into a bit mask.
   Making the Tx ring too large decreases the effectiveness of channel
   bonding and packet priority.
   There are no ill effects from too-large receive rings. */
#define TX_RING_SIZE	8
#define RX_RING_SIZE	16

/* Operational parameters that usually are not changed. */
/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT  (2*HZ)

#define PKT_BUF_SZ		1536			/* Size of each temporary Rx buffer.*/

#if !defined(__OPTIMIZE__)  ||  !defined(__KERNEL__)
#warning  You must compile this file with the correct options!
#warning  See the last lines of the source file.
#error  See the last lines of the source file for the proper compile-command.
#endif

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <asm/processor.h>		/* Processor type for cache alignment. */
#include <asm/bitops.h>
#include <asm/io.h>

/* Condensed bus+endian portability operations. */
#define virt_to_le32desc(addr)  cpu_to_le32(virt_to_bus(addr))
#define le32desc_to_virt(addr)  bus_to_virt(le32_to_cpu(addr))

/* This driver was written to use PCI memory space, however most versions
   of the Rhine only work correctly with I/O space accesses. */
#if defined(VIA_USE_MEMORY)
#warning Many adapters using the VIA Rhine chip are not configured to work
#warning with PCI memory space accesses.
#else
#define USE_IO
#undef readb
#undef readw
#undef readl
#undef writeb
#undef writew
#undef writel
#define readb inb
#define readw inw
#define readl inl
#define writeb outb
#define writew outw
#define writel outl
#endif

/* Kernel compatibility defines, some common to David Hind's PCMCIA package.
   This is only in the support-all-kernels source code. */

#define RUN_AT(x) (jiffies + (x))

MODULE_AUTHOR("Donald Becker <becker@cesdis.gsfc.nasa.gov>");
MODULE_DESCRIPTION("VIA Rhine PCI Fast Ethernet driver");
MODULE_PARM(max_interrupt_work, "i");
MODULE_PARM(debug, "i");
MODULE_PARM(rx_copybreak, "i");
MODULE_PARM(options, "1-" __MODULE_STRING(MAX_UNITS) "i");
MODULE_PARM(full_duplex, "1-" __MODULE_STRING(MAX_UNITS) "i");


/*
				Theory of Operation

I. Board Compatibility

This driver is designed for the VIA 86c100A Rhine-II PCI Fast Ethernet
controller.

II. Board-specific settings

Boards with this chip are functional only in a bus-master PCI slot.

Many operational settings are loaded from the EEPROM to the Config word at
offset 0x78.  This driver assumes that they are correct.
If this driver is compiled to use PCI memory space operations the EEPROM
must be configured to enable memory ops.

III. Driver operation

IIIa. Ring buffers

This driver uses two statically allocated fixed-size descriptor lists
formed into rings by a branch from the final descriptor to the beginning of
the list.  The ring sizes are set at compile time by RX/TX_RING_SIZE.

IIIb/c. Transmit/Receive Structure

This driver attempts to use a zero-copy receive and transmit scheme.

Alas, all data buffers are required to start on a 32 bit boundary, so
the driver must often copy transmit packets into bounce buffers.

The driver allocates full frame size skbuffs for the Rx ring buffers at
open() time and passes the skb->data field to the chip as receive data
buffers.  When an incoming frame is less than RX_COPYBREAK bytes long,
a fresh skbuff is allocated and the frame is copied to the new skbuff.
When the incoming frame is larger, the skbuff is passed directly up the
protocol stack.  Buffers consumed this way are replaced by newly allocated
skbuffs in the last phase of via_rhine_rx().

The RX_COPYBREAK value is chosen to trade-off the memory wasted by
using a full-sized skbuff for small frames vs. the copying costs of larger
frames.  New boards are typically used in generously configured machines
and the underfilled buffers have negligible impact compared to the benefit of
a single allocation size, so the default value of zero results in never
copying packets.  When copying is done, the cost is usually mitigated by using
a combined copy/checksum routine.  Copying also preloads the cache, which is
most useful with small frames.

Since the VIA chips are only able to transfer data to buffers on 32 bit
boundaries, the the IP header at offset 14 in an ethernet frame isn't
longword aligned for further processing.  Copying these unaligned buffers
has the beneficial effect of 16-byte aligning the IP header.

IIId. Synchronization

The driver runs as two independent, single-threaded flows of control.  One
is the send-packet routine, which enforces single-threaded use by the
dev->tbusy flag.  The other thread is the interrupt handler, which is single
threaded by the hardware and interrupt handling software.

The send packet thread has partial control over the Tx ring and 'dev->tbusy'
flag.  It sets the tbusy flag whenever it's queuing a Tx packet. If the next
queue slot is empty, it clears the tbusy flag when finished otherwise it sets
the 'lp->tx_full' flag.

The interrupt handler has exclusive control over the Rx ring and records stats
from the Tx ring.  After reaping the stats, it marks the Tx queue entry as
empty by incrementing the dirty_tx mark. Iff the 'lp->tx_full' flag is set, it
clears both the tx_full and tbusy flags.

IV. Notes

IVb. References

Preliminary VT86C100A manual from http://www.via.com.tw/
http://cesdis.gsfc.nasa.gov/linux/misc/100mbps.html
http://cesdis.gsfc.nasa.gov/linux/misc/NWay.html

IVc. Errata

The VT86C100A manual is not reliable information.
The chip does not handle unaligned transmit or receive buffers, resulting
in significant performance degradation for bounce buffer copies on transmit
and unaligned IP headers on receive.
The chip does not pad to minimum transmit length.

*/



/* This table drives the PCI probe routines.  It's mostly boilerplate in all
   of the drivers, and will likely be provided by some future kernel.
   Note the matching code -- the first table entry matchs all 56** cards but
   second only the 1234 card.
*/

enum pci_flags_bit {
	PCI_USES_IO=1, PCI_USES_MEM=2, PCI_USES_MASTER=4,
	PCI_ADDR0=0x10<<0, PCI_ADDR1=0x10<<1, PCI_ADDR2=0x10<<2, PCI_ADDR3=0x10<<3,
};

enum via_rhine_chips {
	VT86C100A = 0,
	VT3043,
};

struct via_rhine_chip_info {
	const char *name;
	u16 pci_flags;
	int io_size;
	int drv_flags;
};


enum chip_capability_flags {CanHaveMII=1, };

#if defined(VIA_USE_MEMORY)
#define RHINE_IOTYPE (PCI_USES_MEM | PCI_USES_MASTER | PCI_ADDR1)
#else
#define RHINE_IOTYPE (PCI_USES_IO  | PCI_USES_MASTER | PCI_ADDR0)
#endif

/* directly indexed by enum via_rhine_chips, above */
static struct via_rhine_chip_info via_rhine_chip_info[] __devinitdata =
{
	{ "VIA VT86C100A Rhine-II", RHINE_IOTYPE, 128, CanHaveMII },
	{ "VIA VT3043 Rhine", RHINE_IOTYPE, 128, CanHaveMII }
};

static struct pci_device_id via_rhine_pci_tbl[] __devinitdata =
{
	{0x1106, 0x6100, PCI_ANY_ID, PCI_ANY_ID, 0, 0, VT86C100A},
	{0x1106, 0x3043, PCI_ANY_ID, PCI_ANY_ID, 0, 0, VT3043},
	{0,},			/* terminate list */
};
MODULE_DEVICE_TABLE(pci, via_rhine_pci_tbl);


/* Offsets to the device registers. */
enum register_offsets {
	StationAddr=0x00, RxConfig=0x06, TxConfig=0x07, ChipCmd=0x08,
	IntrStatus=0x0C, IntrEnable=0x0E,
	MulticastFilter0=0x10, MulticastFilter1=0x14,
	RxRingPtr=0x18, TxRingPtr=0x1C,
	MIIPhyAddr=0x6C, MIIStatus=0x6D, PCIBusConfig=0x6E,
	MIICmd=0x70, MIIRegAddr=0x71, MIIData=0x72,
	Config=0x78, RxMissed=0x7C, RxCRCErrs=0x7E,
};

/* Bits in the interrupt status/mask registers. */
enum intr_status_bits {
	IntrRxDone=0x0001, IntrRxErr=0x0004, IntrRxEmpty=0x0020,
	IntrTxDone=0x0002, IntrTxAbort=0x0008, IntrTxUnderrun=0x0010,
	IntrPCIErr=0x0040,
	IntrStatsMax=0x0080, IntrRxEarly=0x0100, IntrMIIChange=0x0200,
	IntrRxOverflow=0x0400, IntrRxDropped=0x0800, IntrRxNoBuf=0x1000,
	IntrTxAborted=0x2000, IntrLinkChange=0x4000,
	IntrRxWakeUp=0x8000,
	IntrNormalSummary=0x0003, IntrAbnormalSummary=0xC260,
};


/* The Rx and Tx buffer descriptors. */
struct rx_desc {
	s32 rx_status;
	u32 desc_length;
	u32 addr;
	u32 next_desc;
};
struct tx_desc {
	s32 tx_status;
	u32 desc_length;
	u32 addr;
	u32 next_desc;
};

/* Bits in *_desc.status */
enum rx_status_bits {
	RxOK=0x8000, RxWholePkt=0x0300, RxErr=0x008F
};

enum desc_status_bits {
	DescOwn=0x80000000, DescEndPacket=0x4000, DescIntr=0x1000,
};

/* Bits in ChipCmd. */
enum chip_cmd_bits {
	CmdInit=0x0001, CmdStart=0x0002, CmdStop=0x0004, CmdRxOn=0x0008,
	CmdTxOn=0x0010, CmdTxDemand=0x0020, CmdRxDemand=0x0040,
	CmdEarlyRx=0x0100, CmdEarlyTx=0x0200, CmdFDuplex=0x0400,
	CmdNoTxPoll=0x0800, CmdReset=0x8000,
};

struct netdev_private {
	/* Descriptor rings first for alignment. */
	struct rx_desc rx_ring[RX_RING_SIZE];
	struct tx_desc tx_ring[TX_RING_SIZE];
	/* The addresses of receive-in-place skbuffs. */
	struct sk_buff* rx_skbuff[RX_RING_SIZE];
	/* The saved address of a sent-in-place packet/buffer, for later free(). */
	struct sk_buff* tx_skbuff[TX_RING_SIZE];
	unsigned char *tx_buf[TX_RING_SIZE];	/* Tx bounce buffers */
	unsigned char *tx_bufs;				/* Tx bounce buffer region. */
	struct net_device_stats stats;
	struct timer_list timer;	/* Media monitoring timer. */
	spinlock_t lock;
	/* Frequently used values: keep some adjacent for cache effect. */
	int chip_id;
	struct rx_desc *rx_head_desc;
	unsigned int cur_rx, dirty_rx;		/* Producer/consumer ring indices */
	unsigned int cur_tx, dirty_tx;
	unsigned int rx_buf_sz;				/* Based on MTU+slack. */
	u16 chip_cmd;						/* Current setting for ChipCmd */
	unsigned int tx_full:1;				/* The Tx queue is full. */
	/* These values are keep track of the transceiver/media in use. */
	unsigned int full_duplex:1;			/* Full-duplex operation requested. */
	unsigned int duplex_lock:1;
	unsigned int medialock:1;			/* Do not sense media. */
	unsigned int default_port:4;		/* Last dev->if_port value. */
	u8 tx_thresh, rx_thresh;
	/* MII transceiver section. */
	int mii_cnt;						/* MII device addresses. */
	u16 advertising;					/* NWay media advertisement */
	unsigned char phys[2];				/* MII device addresses. */
};

static int  mdio_read(struct net_device *dev, int phy_id, int location);
static void mdio_write(struct net_device *dev, int phy_id, int location, int value);
static int  via_rhine_open(struct net_device *dev);
static void via_rhine_check_duplex(struct net_device *dev);
static void via_rhine_timer(unsigned long data);
static void via_rhine_tx_timeout(struct net_device *dev);
static void via_rhine_init_ring(struct net_device *dev);
static int  via_rhine_start_tx(struct sk_buff *skb, struct net_device *dev);
static void via_rhine_interrupt(int irq, void *dev_instance, struct pt_regs *regs);
static void via_rhine_tx(struct net_device *dev);
static void via_rhine_rx(struct net_device *dev);
static void via_rhine_error(struct net_device *dev, int intr_status);
static void via_rhine_set_rx_mode(struct net_device *dev);
static struct net_device_stats *via_rhine_get_stats(struct net_device *dev);
static int mii_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
static int  via_rhine_close(struct net_device *dev);


static int __devinit via_rhine_init_one (struct pci_dev *pdev,
					 const struct pci_device_id *ent)
{
	struct net_device *dev;
	struct netdev_private *np;
	int i, option;
	int chip_id = (int) ent->driver_data;
	int irq = pdev->irq;
	static int card_idx = -1;
	static int did_version = 0;
	long ioaddr;
	int io_size;
	int pci_flags;
	
	/* print version once and once only */
	if (! did_version++) {
		printk (KERN_INFO "%s", versionA);
		printk (KERN_INFO "%s", versionB);
	}
	
	card_idx++;
	option = card_idx < MAX_UNITS ? options[card_idx] : 0;
	io_size = via_rhine_chip_info[chip_id].io_size;
	pci_flags = via_rhine_chip_info[chip_id].pci_flags;

	ioaddr = pci_resource_start (pdev, pci_flags & PCI_ADDR0 ? 0 : 1);

	if (pci_enable_device (pdev)) {
		printk (KERN_ERR "unable to init PCI device (card #%d)\n",
			card_idx);
		goto err_out;
	}
	
	if (pci_flags & PCI_USES_MASTER)
		pci_set_master (pdev);

	dev = init_etherdev(NULL, sizeof(*np));
	if (dev == NULL) {
		printk (KERN_ERR "init_ethernet failed for card #%d\n",
			card_idx);
		goto err_out;
	}
	
	if (!request_region(pci_resource_start (pdev, 0), io_size, dev->name)) {
		printk (KERN_ERR "request_region failed for device %s, region 0x%X @ 0x%lX\n",
			dev->name, io_size,
			pci_resource_start (pdev, 0));
		goto err_out_free_netdev;
	}
	if (!request_mem_region(pci_resource_start (pdev, 1), io_size, dev->name)) {
		printk (KERN_ERR "request_mem_region failed for device %s, region 0x%X @ 0x%lX\n",
			dev->name, io_size,
			pci_resource_start (pdev, 1));
		goto err_out_free_pio;
	}

#ifndef USE_IO
	ioaddr = (long) ioremap (ioaddr, io_size);
	if (!ioaddr) {
		printk (KERN_ERR "ioremap failed for device %s, region 0x%X @ 0x%X\n",
			dev->name, io_size,
			pci_resource_start (pdev, 1));
		goto err_out_free_mmio;
	}
#endif

	printk(KERN_INFO "%s: %s at 0x%lx, ",
		   dev->name, via_rhine_chip_info[chip_id].name, ioaddr);

	/* Ideally we would be read the EEPROM but access may be locked. */
	for (i = 0; i <6; i++)
		dev->dev_addr[i] = readb(ioaddr + StationAddr + i);
	for (i = 0; i < 5; i++)
			printk("%2.2x:", dev->dev_addr[i]);
	printk("%2.2x, IRQ %d.\n", dev->dev_addr[i], irq);

	/* Reset the chip to erase previous misconfiguration. */
	writew(CmdReset, ioaddr + ChipCmd);

	dev->base_addr = ioaddr;
	dev->irq = irq;

	np = dev->priv;
	spin_lock_init (&np->lock);
	np->chip_id = chip_id;

	if (dev->mem_start)
		option = dev->mem_start;

	/* The lower four bits are the media type. */
	if (option > 0) {
		if (option & 0x200)
			np->full_duplex = 1;
		np->default_port = option & 15;
		if (np->default_port)
			np->medialock = 1;
	}
	if (card_idx < MAX_UNITS  &&  full_duplex[card_idx] > 0)
		np->full_duplex = 1;

	if (np->full_duplex)
		np->duplex_lock = 1;

	/* The chip-specific entries in the device structure. */
	dev->open = via_rhine_open;
	dev->hard_start_xmit = via_rhine_start_tx;
	dev->stop = via_rhine_close;
	dev->get_stats = via_rhine_get_stats;
	dev->set_multicast_list = via_rhine_set_rx_mode;
	dev->do_ioctl = mii_ioctl;
	dev->tx_timeout = via_rhine_tx_timeout;
	dev->watchdog_timeo = TX_TIMEOUT;
	
	pdev->driver_data = dev;

	if (via_rhine_chip_info[chip_id].drv_flags & CanHaveMII) {
		int phy, phy_idx = 0;
		np->phys[0] = 1;		/* Standard for this chip. */
		for (phy = 1; phy < 32 && phy_idx < 4; phy++) {
			int mii_status = mdio_read(dev, phy, 1);
			if (mii_status != 0xffff  &&  mii_status != 0x0000) {
				np->phys[phy_idx++] = phy;
				np->advertising = mdio_read(dev, phy, 4);
				printk(KERN_INFO "%s: MII PHY found at address %d, status "
					   "0x%4.4x advertising %4.4x Link %4.4x.\n",
					   dev->name, phy, mii_status, np->advertising,
					   mdio_read(dev, phy, 5));
			}
		}
		np->mii_cnt = phy_idx;
	}

	return 0;

#ifndef USE_IO
/* note this is ifdef'd because the ioremap is ifdef'd...
 * so additional exit conditions above this must move
 * release_mem_region outside of the ifdef */
err_out_free_mmio:
	release_mem_region(pci_resource_start (pdev, 1), io_size));
#endif
err_out_free_pio:
	release_region(pci_resource_start (pdev, 0), io_size);
err_out_free_netdev:
	unregister_netdev (dev);
	kfree (dev);
err_out:
	return -ENODEV;
}


/* Read and write over the MII Management Data I/O (MDIO) interface. */

static int mdio_read(struct net_device *dev, int phy_id, int regnum)
{
	long ioaddr = dev->base_addr;
	int boguscnt = 1024;

	/* Wait for a previous command to complete. */
	while ((readb(ioaddr + MIICmd) & 0x60) && --boguscnt > 0)
		;
	writeb(0x00, ioaddr + MIICmd);
	writeb(phy_id, ioaddr + MIIPhyAddr);
	writeb(regnum, ioaddr + MIIRegAddr);
	writeb(0x40, ioaddr + MIICmd);			/* Trigger read */
	boguscnt = 1024;
	while ((readb(ioaddr + MIICmd) & 0x40) && --boguscnt > 0)
		;
	return readw(ioaddr + MIIData);
}

static void mdio_write(struct net_device *dev, int phy_id, int regnum, int value)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	long ioaddr = dev->base_addr;
	int boguscnt = 1024;

	if (phy_id == np->phys[0]  &&  regnum == 4)
		np->advertising = value;
	/* Wait for a previous command to complete. */
	while ((readb(ioaddr + MIICmd) & 0x60) && --boguscnt > 0)
		;
	writeb(0x00, ioaddr + MIICmd);
	writeb(phy_id, ioaddr + MIIPhyAddr);
	writeb(regnum, ioaddr + MIIRegAddr);
	writew(value, ioaddr + MIIData);
	writeb(0x20, ioaddr + MIICmd);			/* Trigger write. */
	return;
}


static int via_rhine_open(struct net_device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	long ioaddr = dev->base_addr;
	int i;

	/* Reset the chip. */
	writew(CmdReset, ioaddr + ChipCmd);

	if (request_irq(dev->irq, &via_rhine_interrupt, SA_SHIRQ, dev->name, dev))
		return -EAGAIN;

	if (debug > 1)
		printk(KERN_DEBUG "%s: via_rhine_open() irq %d.\n",
			   dev->name, dev->irq);

	MOD_INC_USE_COUNT;

	via_rhine_init_ring(dev);

	writel(virt_to_bus(np->rx_ring), ioaddr + RxRingPtr);
	writel(virt_to_bus(np->tx_ring), ioaddr + TxRingPtr);

	for (i = 0; i < 6; i++)
		writeb(dev->dev_addr[i], ioaddr + StationAddr + i);

	/* Initialize other registers. */
	writew(0x0006, ioaddr + PCIBusConfig);	/* Tune configuration??? */
	/* Configure the FIFO thresholds. */
	writeb(0x20, ioaddr + TxConfig);	/* Initial threshold 32 bytes */
	np->tx_thresh = 0x20;
	np->rx_thresh = 0x60;				/* Written in via_rhine_set_rx_mode(). */

	if (dev->if_port == 0)
		dev->if_port = np->default_port;

	netif_start_queue(dev);

	via_rhine_set_rx_mode(dev);

	/* Enable interrupts by setting the interrupt mask. */
	writew(IntrRxDone | IntrRxErr | IntrRxEmpty| IntrRxOverflow| IntrRxDropped|
		   IntrTxDone | IntrTxAbort | IntrTxUnderrun |
		   IntrPCIErr | IntrStatsMax | IntrLinkChange | IntrMIIChange,
		   ioaddr + IntrEnable);

	np->chip_cmd = CmdStart|CmdTxOn|CmdRxOn|CmdNoTxPoll;
	if (np->duplex_lock)
		np->chip_cmd |= CmdFDuplex;
	writew(np->chip_cmd, ioaddr + ChipCmd);

	via_rhine_check_duplex(dev);

	if (debug > 2)
		printk(KERN_DEBUG "%s: Done via_rhine_open(), status %4.4x "
			   "MII status: %4.4x.\n",
			   dev->name, readw(ioaddr + ChipCmd),
			   mdio_read(dev, np->phys[0], 1));

	/* Set the timer to check for link beat. */
	init_timer(&np->timer);
	np->timer.expires = RUN_AT(1);
	np->timer.data = (unsigned long)dev;
	np->timer.function = &via_rhine_timer;				/* timer handler */
	add_timer(&np->timer);

	return 0;
}

static void via_rhine_check_duplex(struct net_device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	long ioaddr = dev->base_addr;
	int mii_reg5 = mdio_read(dev, np->phys[0], 5);
	int negotiated = mii_reg5 & np->advertising;
	int duplex;

	if (np->duplex_lock  ||  mii_reg5 == 0xffff)
		return;
	duplex = (negotiated & 0x0100) || (negotiated & 0x01C0) == 0x0040;
	if (np->full_duplex != duplex) {
		np->full_duplex = duplex;
		if (debug)
			printk(KERN_INFO "%s: Setting %s-duplex based on MII #%d link"
				   " partner capability of %4.4x.\n", dev->name,
				   duplex ? "full" : "half", np->phys[0], mii_reg5);
		if (duplex)
			np->chip_cmd |= CmdFDuplex;
		else
			np->chip_cmd &= ~CmdFDuplex;
		writew(np->chip_cmd, ioaddr + ChipCmd);
	}
}


static void via_rhine_timer(unsigned long data)
{
	struct net_device *dev = (struct net_device *)data;
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	long ioaddr = dev->base_addr;
	int next_tick = 10*HZ;

	if (debug > 3) {
		printk(KERN_DEBUG "%s: VIA Rhine monitor tick, status %4.4x.\n",
			   dev->name, readw(ioaddr + IntrStatus));
	}

	via_rhine_check_duplex(dev);

	np->timer.expires = RUN_AT(next_tick);
	add_timer(&np->timer);
}


static void via_rhine_tx_timeout (struct net_device *dev)
{
	struct netdev_private *np = (struct netdev_private *) dev->priv;
	long ioaddr = dev->base_addr;

	printk (KERN_WARNING "%s: Transmit timed out, status %4.4x, PHY status "
		"%4.4x, resetting...\n",
		dev->name, readw (ioaddr + IntrStatus),
		mdio_read (dev, np->phys[0], 1));

	/* XXX Perhaps we should reinitialize the hardware here. */
	dev->if_port = 0;

	/* Stop and restart the chip's Tx processes . */
	/* XXX to do */

	/* Trigger an immediate transmit demand. */
	/* XXX to do */

	dev->trans_start = jiffies;
	np->stats.tx_errors++;
}


/* Initialize the Rx and Tx rings, along with various 'dev' bits. */
static void via_rhine_init_ring(struct net_device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	int i;

	np->cur_rx = np->cur_tx = 0;
	np->dirty_rx = np->dirty_tx = 0;

	np->rx_buf_sz = (dev->mtu <= 1500 ? PKT_BUF_SZ : dev->mtu + 32);
	np->rx_head_desc = &np->rx_ring[0];

	for (i = 0; i < RX_RING_SIZE; i++) {
		np->rx_ring[i].rx_status = 0;
		np->rx_ring[i].desc_length = cpu_to_le32(np->rx_buf_sz);
		np->rx_ring[i].next_desc = virt_to_le32desc(&np->rx_ring[i+1]);
		np->rx_skbuff[i] = 0;
	}
	/* Mark the last entry as wrapping the ring. */
	np->rx_ring[i-1].next_desc = virt_to_le32desc(&np->rx_ring[0]);

	/* Fill in the Rx buffers. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		struct sk_buff *skb = dev_alloc_skb(np->rx_buf_sz);
		np->rx_skbuff[i] = skb;
		if (skb == NULL)
			break;
		skb->dev = dev;			/* Mark as being used by this device. */
		np->rx_ring[i].addr = virt_to_le32desc(skb->tail);
		np->rx_ring[i].rx_status = cpu_to_le32(DescOwn);
	}

	for (i = 0; i < TX_RING_SIZE; i++) {
		np->tx_skbuff[i] = 0;
		np->tx_ring[i].tx_status = 0;
		np->tx_ring[i].desc_length = 0x00e08000;
		np->tx_ring[i].next_desc = virt_to_le32desc(&np->tx_ring[i+1]);
		np->tx_buf[i] = kmalloc(PKT_BUF_SZ, GFP_KERNEL);
	}
	np->tx_ring[i-1].next_desc = virt_to_le32desc(&np->tx_ring[0]);

	return;
}

static int via_rhine_start_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	unsigned entry;
	unsigned long flags;

	/* Caution: the write order is important here, set the field
	   with the "ownership" bits last. */

	/* lock eth irq */
	spin_lock_irqsave (&np->lock, flags);

	/* Calculate the next Tx descriptor entry. */
	entry = np->cur_tx % TX_RING_SIZE;

	np->tx_skbuff[entry] = skb;

	if ((long)skb->data & 3) {			/* Must use alignment buffer. */
		if (np->tx_buf[entry] == NULL &&
			(np->tx_buf[entry] = kmalloc(PKT_BUF_SZ, GFP_KERNEL)) == NULL) {
			spin_unlock_irqrestore (&np->lock, flags);
			return 1;
		}
		memcpy(np->tx_buf[entry], skb->data, skb->len);
		np->tx_ring[entry].addr = virt_to_le32desc(np->tx_buf[entry]);
	} else
		np->tx_ring[entry].addr = virt_to_le32desc(skb->data);

	np->tx_ring[entry].desc_length = 
		cpu_to_le32(0x00E08000 | (skb->len >= ETH_ZLEN ? skb->len : ETH_ZLEN));
	np->tx_ring[entry].tx_status = cpu_to_le32(DescOwn);

	np->cur_tx++;

	/* Non-x86 Todo: explicitly flush cache lines here. */

	/* Wake the potentially-idle transmit channel. */
	writew(CmdTxDemand | np->chip_cmd, dev->base_addr + ChipCmd);

	if (np->cur_tx == np->dirty_tx + TX_RING_SIZE)
		netif_stop_queue(dev);

	dev->trans_start = jiffies;

	spin_unlock_irqrestore (&np->lock, flags);

	if (debug > 4) {
		printk(KERN_DEBUG "%s: Transmit frame #%d queued in slot %d.\n",
			   dev->name, np->cur_tx, entry);
	}
	return 0;
}

/* The interrupt handler does all of the Rx thread work and cleans up
   after the Tx thread. */
static void via_rhine_interrupt(int irq, void *dev_instance, struct pt_regs *rgs)
{
	struct net_device *dev = (struct net_device *)dev_instance;
	long ioaddr, boguscnt = max_interrupt_work;
	u32 intr_status;

	ioaddr = dev->base_addr;
	
	while ((intr_status = readw(ioaddr + IntrStatus))) {
		/* Acknowledge all of the current interrupt sources ASAP. */
		writew(intr_status & 0xffff, ioaddr + IntrStatus);

		if (debug > 4)
			printk(KERN_DEBUG "%s: Interrupt, status %4.4x.\n",
				   dev->name, intr_status);

		if (intr_status & (IntrRxDone | IntrRxErr | IntrRxDropped |
						   IntrRxWakeUp | IntrRxEmpty | IntrRxNoBuf))
			via_rhine_rx(dev);

		if (intr_status & (IntrTxDone | IntrTxAbort | IntrTxUnderrun |
						   IntrTxAborted))
			via_rhine_tx(dev);

		/* Abnormal error summary/uncommon events handlers. */
		if (intr_status & (IntrPCIErr | IntrLinkChange | IntrMIIChange |
						   IntrStatsMax | IntrTxAbort | IntrTxUnderrun))
			via_rhine_error(dev, intr_status);

		if (--boguscnt < 0) {
			printk(KERN_WARNING "%s: Too much work at interrupt, "
				   "status=0x%4.4x.\n",
				   dev->name, intr_status);
			break;
		}
	}

	if (debug > 3)
		printk(KERN_DEBUG "%s: exiting interrupt, status=%#4.4x.\n",
			   dev->name, readw(ioaddr + IntrStatus));
}

/* This routine is logically part of the interrupt handler, but isolated
   for clarity. */
static void via_rhine_tx(struct net_device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	int txstatus = 0, entry = np->dirty_tx % TX_RING_SIZE;

	spin_lock (&np->lock);

	/* if tx_full is set, they're all dirty, not clean */
	while (np->dirty_tx != np->cur_tx) {
		txstatus = le32_to_cpu(np->tx_ring[entry].tx_status);
		if (txstatus & DescOwn)
			break;
		if (debug > 6)
			printk(KERN_DEBUG " Tx scavenge %d status %8.8x.\n",
				   entry, txstatus);
		if (txstatus & 0x8000) {
			if (debug > 1)
				printk(KERN_DEBUG "%s: Transmit error, Tx status %8.8x.\n",
					   dev->name, txstatus);
			np->stats.tx_errors++;
			if (txstatus & 0x0400) np->stats.tx_carrier_errors++;
			if (txstatus & 0x0200) np->stats.tx_window_errors++;
			if (txstatus & 0x0100) np->stats.tx_aborted_errors++;
			if (txstatus & 0x0080) np->stats.tx_heartbeat_errors++;
			if (txstatus & 0x0002) np->stats.tx_fifo_errors++;
			/* Transmitter restarted in 'abnormal' handler. */
		} else {
			np->stats.collisions += (txstatus >> 3) & 15;
			np->stats.tx_bytes += np->tx_ring[entry].desc_length & 0x7ff;
			np->stats.tx_packets++;
		}
		/* Free the original skb. */
		dev_kfree_skb_irq(np->tx_skbuff[entry]);
		np->tx_skbuff[entry] = NULL;
		entry = (++np->dirty_tx) % TX_RING_SIZE;
	}
	if ((np->cur_tx - np->dirty_tx) <= TX_RING_SIZE/2)
		netif_wake_queue (dev);

	spin_unlock (&np->lock);
}

/* This routine is logically part of the interrupt handler, but isolated
   for clarity and better register allocation. */
static void via_rhine_rx(struct net_device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	int entry = np->cur_rx % RX_RING_SIZE;
	int boguscnt = np->dirty_rx + RX_RING_SIZE - np->cur_rx;

	if (debug > 4) {
		printk(KERN_DEBUG " In via_rhine_rx(), entry %d status %8.8x.\n",
			   entry, np->rx_head_desc->rx_status);
	}

	/* If EOP is set on the next entry, it's a new packet. Send it up. */
	while ( ! (np->rx_head_desc->rx_status & cpu_to_le32(DescOwn))) {
		struct rx_desc *desc = np->rx_head_desc;
		u32 desc_status = le32_to_cpu(desc->rx_status);
		int data_size = desc_status >> 16;

		if (debug > 4)
			printk(KERN_DEBUG "  via_rhine_rx() status is %8.8x.\n",
				   desc_status);
		if (--boguscnt < 0)
			break;
		if ( (desc_status & (RxWholePkt | RxErr)) !=  RxWholePkt) {
			if ((desc_status & RxWholePkt) !=  RxWholePkt) {
				printk(KERN_WARNING "%s: Oversized Ethernet frame spanned "
					   "multiple buffers, entry %#x length %d status %8.8x!\n",
					   dev->name, entry, data_size, desc_status);
				printk(KERN_WARNING "%s: Oversized Ethernet frame %p vs %p.\n",
					   dev->name, np->rx_head_desc,
					   &np->rx_ring[entry]);
				np->stats.rx_length_errors++;
			} else if (desc_status & RxErr) {
				/* There was a error. */
				if (debug > 2)
					printk(KERN_DEBUG "  via_rhine_rx() Rx error was %8.8x.\n",
						   desc_status);
				np->stats.rx_errors++;
				if (desc_status & 0x0030) np->stats.rx_length_errors++;
				if (desc_status & 0x0048) np->stats.rx_fifo_errors++;
				if (desc_status & 0x0004) np->stats.rx_frame_errors++;
				if (desc_status & 0x0002) np->stats.rx_crc_errors++;
			}
		} else {
			struct sk_buff *skb;
			/* Length should omit the CRC */
			u16 pkt_len = data_size - 4;

			/* Check if the packet is long enough to accept without copying
			   to a minimally-sized skbuff. */
			if (pkt_len < rx_copybreak
				&& (skb = dev_alloc_skb(pkt_len + 2)) != NULL) {
				skb->dev = dev;
				skb_reserve(skb, 2);	/* 16 byte align the IP header */
#if ! defined(__alpha__) || USE_IP_COPYSUM		/* Avoid misaligned on Alpha */
				eth_copy_and_sum(skb, bus_to_virt(desc->addr),
								 pkt_len, 0);
				skb_put(skb, pkt_len);
#else
				memcpy(skb_put(skb,pkt_len), bus_to_virt(desc->addr), pkt_len);
#endif
			} else {
				skb_put(skb = np->rx_skbuff[entry], pkt_len);
				np->rx_skbuff[entry] = NULL;
			}
			skb->protocol = eth_type_trans(skb, dev);
			netif_rx(skb);
			dev->last_rx = jiffies;
			np->stats.rx_bytes += skb->len;
			np->stats.rx_packets++;
		}
		entry = (++np->cur_rx) % RX_RING_SIZE;
		np->rx_head_desc = &np->rx_ring[entry];
	}

	/* Refill the Rx ring buffers. */
	for (; np->cur_rx - np->dirty_rx > 0; np->dirty_rx++) {
		struct sk_buff *skb;
		entry = np->dirty_rx % RX_RING_SIZE;
		if (np->rx_skbuff[entry] == NULL) {
			skb = dev_alloc_skb(np->rx_buf_sz);
			np->rx_skbuff[entry] = skb;
			if (skb == NULL)
				break;			/* Better luck next round. */
			skb->dev = dev;			/* Mark as being used by this device. */
			np->rx_ring[entry].addr = virt_to_le32desc(skb->tail);
		}
		np->rx_ring[entry].rx_status = cpu_to_le32(DescOwn);
	}

	/* Pre-emptively restart Rx engine. */
	writew(CmdRxDemand | np->chip_cmd, dev->base_addr + ChipCmd);
}

static void via_rhine_error(struct net_device *dev, int intr_status)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	long ioaddr = dev->base_addr;

	if (intr_status & (IntrMIIChange | IntrLinkChange)) {
		if (readb(ioaddr + MIIStatus) & 0x02)
			/* Link failed, restart autonegotiation. */
			mdio_write(dev, np->phys[0], 0, 0x3300);
		else
			via_rhine_check_duplex(dev);
		if (debug)
			printk(KERN_ERR "%s: MII status changed: Autonegotiation "
				   "advertising %4.4x  partner %4.4x.\n", dev->name,
			   mdio_read(dev, np->phys[0], 4),
			   mdio_read(dev, np->phys[0], 5));
	}
	if (intr_status & IntrStatsMax) {
		np->stats.rx_crc_errors	+= readw(ioaddr + RxCRCErrs);
		np->stats.rx_missed_errors	+= readw(ioaddr + RxMissed);
		writel(0, RxMissed);
	}
	if (intr_status & IntrTxAbort) {
		/* Stats counted in Tx-done handler, just restart Tx. */
		writew(CmdTxDemand | np->chip_cmd, dev->base_addr + ChipCmd);
	}
	if (intr_status & IntrTxUnderrun) {
		if (np->tx_thresh < 0xE0)
			writeb(np->tx_thresh += 0x20, ioaddr + TxConfig);
		if (debug > 1)
			printk(KERN_INFO "%s: Transmitter underrun, increasing Tx "
				   "threshold setting to %2.2x.\n", dev->name, np->tx_thresh);
	}
	if ((intr_status & ~( IntrLinkChange | IntrStatsMax |
						  IntrTxAbort | IntrTxAborted)) && debug > 1) {
		printk(KERN_ERR "%s: Something Wicked happened! %4.4x.\n",
			   dev->name, intr_status);
		/* Recovery for other fault sources not known. */
		writew(CmdTxDemand | np->chip_cmd, dev->base_addr + ChipCmd);
	}
}

static struct net_device_stats *via_rhine_get_stats(struct net_device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	long ioaddr = dev->base_addr;

	/* Nominally we should lock this segment of code for SMP, although
	   the vulnerability window is very small and statistics are
	   non-critical. */
	np->stats.rx_crc_errors	+= readw(ioaddr + RxCRCErrs);
	np->stats.rx_missed_errors	+= readw(ioaddr + RxMissed);
	writel(0, RxMissed);

	return &np->stats;
}

/* The big-endian AUTODIN II ethernet CRC calculation.
   N.B. Do not use for bulk data, use a table-based routine instead.
   This is common code and should be moved to net/core/crc.c */
static unsigned const ethernet_polynomial = 0x04c11db7U;
static inline u32 ether_crc(int length, unsigned char *data)
{
    int crc = -1;

    while(--length >= 0) {
		unsigned char current_octet = *data++;
		int bit;
		for (bit = 0; bit < 8; bit++, current_octet >>= 1) {
			crc = (crc << 1) ^
				((crc < 0) ^ (current_octet & 1) ? ethernet_polynomial : 0);
		}
    }
    return crc;
}

static void via_rhine_set_rx_mode(struct net_device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	long ioaddr = dev->base_addr;
	u32 mc_filter[2];			/* Multicast hash filter */
	u8 rx_mode;					/* Note: 0x02=accept runt, 0x01=accept errs */

	if (dev->flags & IFF_PROMISC) {			/* Set promiscuous. */
		/* Unconditionally log net taps. */
		printk(KERN_NOTICE "%s: Promiscuous mode enabled.\n", dev->name);
		rx_mode = 0x1C;
	} else if ((dev->mc_count > multicast_filter_limit)
			   ||  (dev->flags & IFF_ALLMULTI)) {
		/* Too many to match, or accept all multicasts. */
		writel(0xffffffff, ioaddr + MulticastFilter0);
		writel(0xffffffff, ioaddr + MulticastFilter1);
		rx_mode = 0x0C;
	} else {
		struct dev_mc_list *mclist;
		int i;
		memset(mc_filter, 0, sizeof(mc_filter));
		for (i = 0, mclist = dev->mc_list; mclist && i < dev->mc_count;
			 i++, mclist = mclist->next) {
			set_bit(ether_crc(ETH_ALEN, mclist->dmi_addr) >> 26, mc_filter);
		}
		writel(mc_filter[0], ioaddr + MulticastFilter0);
		writel(mc_filter[1], ioaddr + MulticastFilter1);
		rx_mode = 0x0C;
	}
	writeb(np->rx_thresh | rx_mode, ioaddr + RxConfig);
}

static int mii_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	u16 *data = (u16 *)&rq->ifr_data;

	switch(cmd) {
	case SIOCDEVPRIVATE:		/* Get the address of the PHY in use. */
		data[0] = ((struct netdev_private *)dev->priv)->phys[0] & 0x1f;
		/* Fall Through */
	case SIOCDEVPRIVATE+1:		/* Read the specified MII register. */
		data[3] = mdio_read(dev, data[0] & 0x1f, data[1] & 0x1f);
		return 0;
	case SIOCDEVPRIVATE+2:		/* Write the specified MII register */
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		mdio_write(dev, data[0] & 0x1f, data[1] & 0x1f, data[2]);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int via_rhine_close(struct net_device *dev)
{
	long ioaddr = dev->base_addr;
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	int i;

	netif_stop_queue(dev);

	if (debug > 1)
		printk(KERN_DEBUG "%s: Shutting down ethercard, status was %4.4x.\n",
			   dev->name, readw(ioaddr + ChipCmd));

	/* Disable interrupts by clearing the interrupt mask. */
	writew(0x0000, ioaddr + IntrEnable);

	/* Stop the chip's Tx and Rx processes. */
	writew(CmdStop, ioaddr + ChipCmd);

	del_timer(&np->timer);

	free_irq(dev->irq, dev);

	/* Free all the skbuffs in the Rx queue. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		np->rx_ring[i].rx_status = 0;
		np->rx_ring[i].addr = 0xBADF00D0; /* An invalid address. */
		if (np->rx_skbuff[i]) {
			dev_kfree_skb(np->rx_skbuff[i]);
		}
		np->rx_skbuff[i] = 0;
	}
	for (i = 0; i < TX_RING_SIZE; i++) {
		if (np->tx_skbuff[i])
			dev_kfree_skb(np->tx_skbuff[i]);
		np->tx_skbuff[i] = 0;
	}

	MOD_DEC_USE_COUNT;

	return 0;
}


static void __devexit via_rhine_remove_one (struct pci_dev *pdev)
{
	struct net_device *dev = pdev->driver_data;
	struct netdev_private *np = (struct netdev_private *)(dev->priv);
	
	unregister_netdev(dev);

	release_region(pci_resource_start (pdev, 0),
		       via_rhine_chip_info[np->chip_id].io_size);
	release_mem_region(pci_resource_start (pdev, 1),
		           via_rhine_chip_info[np->chip_id].io_size);

#ifndef USE_IO
	iounmap((char *)(dev->base_addr));
#endif

	kfree(dev);
}


static struct pci_driver via_rhine_driver = {
	name:		"via-rhine",
	id_table:	via_rhine_pci_tbl,
	probe:		via_rhine_init_one,
	remove:		via_rhine_remove_one,
};


static int __init via_rhine_init (void)
{
	return pci_module_init (&via_rhine_driver);
}


static void __exit via_rhine_cleanup (void)
{
	pci_unregister_driver (&via_rhine_driver);
}


module_init(via_rhine_init);
module_exit(via_rhine_cleanup);


/*
 * Local variables:
 *  compile-command: "gcc -DMODULE -D__KERNEL__ -I/usr/src/linux/net/inet -Wall -Wstrict-prototypes -O6 -c via-rhine.c `[ -f /usr/include/linux/modversions.h ] && echo -DMODVERSIONS`"
 *  SMP-compile-command: "gcc -D__SMP__ -DMODULE -D__KERNEL__ -I/usr/src/linux/net/inet -Wall -Wstrict-prototypes -O6 -c via-rhine.c `[ -f /usr/include/linux/modversions.h ] && echo -DMODVERSIONS`"
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 * End:
 */
