/* via-rhine.c: A Linux Ethernet device driver for VIA Rhine family chips. */
/*
	Written 1998 by Donald Becker.

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
*/

static const char *versionA =
"via-rhine.c:v1.00 9/5/98  Written by Donald Becker\n";
static const char *versionB =
"  http://cesdis.gsfc.nasa.gov/linux/drivers/via-rhine.html\n";

/* A few user-configurable values.   These may be modified when a driver
   module is loaded.*/

static int debug = 1;			/* 1 normal messages, 0 quiet .. 7 verbose. */
static int max_interrupt_work = 20;
static int min_pci_latency = 64;

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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
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
#include <asm/processor.h>		/* Processor type for cache alignment. */
#include <asm/bitops.h>
#include <asm/io.h>

/* This driver was written to use PCI memory space, however some boards
   only work with I/O space accesses. */
#define VIA_USE_IO
#ifdef VIA_USE_IO
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

#if (LINUX_VERSION_CODE >= 0x20100)
#else
#ifndef __alpha__
#define ioremap vremap
#define iounmap vfree
#endif
#endif
#if defined(MODULE) && LINUX_VERSION_CODE > 0x20115
MODULE_AUTHOR("Donald Becker <becker@cesdis.gsfc.nasa.gov>");
MODULE_DESCRIPTION("VIA Rhine PCI Fast Ethernet driver");
MODULE_PARM(max_interrupt_work, "i");
MODULE_PARM(min_pci_latency, "i");
MODULE_PARM(debug, "i");
MODULE_PARM(rx_copybreak, "i");
MODULE_PARM(options, "1-" __MODULE_STRING(MAX_UNITS) "i");
MODULE_PARM(full_duplex, "1-" __MODULE_STRING(MAX_UNITS) "i");
#endif
#if LINUX_VERSION_CODE < 0x20123
#define test_and_set_bit(val, addr) set_bit(val, addr)
#endif
#if LINUX_VERSION_CODE <= 0x20139
#define	net_device_stats enet_statistics
#else
#define NETSTATS_VER2
#endif
#if LINUX_VERSION_CODE < 0x20155  ||  defined(CARDBUS)
/* Grrrr, the PCI code changed, but did not consider CardBus... */
#include <linux/bios32.h>
#define PCI_SUPPORT_VER1
#else
#define PCI_SUPPORT_VER2
#endif
#if LINUX_VERSION_CODE < 0x20159
#define dev_free_skb(skb) dev_kfree_skb(skb, FREE_WRITE);
#else
#define dev_free_skb(skb) dev_kfree_skb(skb);
#endif


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
skbuffs in the last phase of netdev_rx().

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
struct pci_id_info {
	const char *name;
	u16	vendor_id, device_id, device_id_mask, flags;
	int io_size;
	struct device *(*probe1)(int pci_bus, int pci_devfn, struct device *dev,
							 long ioaddr, int irq, int chip_idx, int fnd_cnt);
};

static struct device *via_probe1(int pci_bus, int pci_devfn,
								 struct device *dev, long ioaddr, int irq,
								 int chp_idx, int fnd_cnt);

static struct pci_id_info pci_tbl[] = {
	{ "VIA VT86C100A Rhine-II", 0x1106, 0x6100, 0xffff,
	  PCI_USES_MEM|PCI_USES_IO|PCI_USES_MEM|PCI_USES_MASTER, 128, via_probe1},
	{ "VIA VT3043 Rhine", 0x1106, 0x3043, 0xffff,
	  PCI_USES_IO|PCI_USES_MEM|PCI_USES_MASTER, 128, via_probe1},
	{0,},						/* 0 terminated list. */
};


/* A chip capabilities table, matching the entries in pci_tbl[] above. */
enum chip_capability_flags {CanHaveMII=1, };
struct chip_info {
	int io_size;
	int flags;
} static cap_tbl[] = {
	{128, CanHaveMII, },
	{128, CanHaveMII, },
};


/* Offsets to the device registers.
*/
enum register_offsets {
	StationAddr=0x00, RxConfig=0x06, TxConfig=0x07, ChipCmd=0x08,
	IntrStatus=0x0C, IntrEnable=0x0E,
	MulticastFilter0=0x10, MulticastFilter1=0x14,
	RxRingPtr=0x18, TxRingPtr=0x1C,
	MIIPhyAddr=0x6C, MIIStatus=0x6D, PCIConfig=0x6E,
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
	IntrNormalSummary=0x0003, IntrAbnormalSummary=0x8260,
};


/* The Rx and Tx buffer descriptors. */
struct rx_desc {
	u16 rx_status;
	u16 rx_length;
	u32 desc_length;
	u32 addr;
	u32 next_desc;
};
struct tx_desc {
	u16 tx_status;
	u16 tx_own;
	u32 desc_length;
	u32 addr;
	u32 next_desc;
};

/* Bits in *_desc.status */
enum rx_status_bits {
	RxDescOwn=0x80000000, RxOK=0x8000, RxWholePkt=0x0300, RxErr=0x008F};
enum desc_status_bits {
	DescOwn=0x8000, DescEndPacket=0x4000, DescIntr=0x1000,
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
	struct device *next_module;			/* Link for devices of this type. */
	struct net_device_stats stats;
	struct timer_list timer;	/* Media monitoring timer. */
	unsigned char pci_bus, pci_devfn;
	/* Frequently used values: keep some adjacent for cache effect. */
	int chip_id;
	long in_interrupt;			/* Word-long for SMP locks. */
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

static int  mdio_read(struct device *dev, int phy_id, int location);
static void mdio_write(struct device *dev, int phy_id, int location, int value);
static int  netdev_open(struct device *dev);
static void check_duplex(struct device *dev);
static void netdev_timer(unsigned long data);
static void tx_timeout(struct device *dev);
static void init_ring(struct device *dev);
static int  start_tx(struct sk_buff *skb, struct device *dev);
static void intr_handler(int irq, void *dev_instance, struct pt_regs *regs);
static int  netdev_rx(struct device *dev);
static void netdev_error(struct device *dev, int intr_status);
static void set_rx_mode(struct device *dev);
static struct net_device_stats *get_stats(struct device *dev);
static int mii_ioctl(struct device *dev, struct ifreq *rq, int cmd);
static int  netdev_close(struct device *dev);



/* A list of our installed devices, for removing the driver module. */
static struct device *root_net_dev = NULL;

/* Ideally we would detect all network cards in slot order.  That would
   be best done a central PCI probe dispatch, which wouldn't work
   well when dynamically adding drivers.  So instead we detect just the
   cards we know about in slot order. */

static int pci_etherdev_probe(struct device *dev, struct pci_id_info pci_tbl[])
{
	int cards_found = 0;
	int pci_index = 0;
	unsigned char pci_bus, pci_device_fn;

	if ( ! pcibios_present())
		return -ENODEV;

	for (;pci_index < 0xff; pci_index++) {
		u16 vendor, device, pci_command, new_command;
		int chip_idx, irq;
		long pciaddr;
		long ioaddr;

		if (pcibios_find_class (PCI_CLASS_NETWORK_ETHERNET << 8, pci_index,
								&pci_bus, &pci_device_fn)
			!= PCIBIOS_SUCCESSFUL)
			break;
		pcibios_read_config_word(pci_bus, pci_device_fn,
								 PCI_VENDOR_ID, &vendor);
		pcibios_read_config_word(pci_bus, pci_device_fn,
								 PCI_DEVICE_ID, &device);

		for (chip_idx = 0; pci_tbl[chip_idx].vendor_id; chip_idx++)
			if (vendor == pci_tbl[chip_idx].vendor_id
				&& (device & pci_tbl[chip_idx].device_id_mask) ==
				pci_tbl[chip_idx].device_id)
				break;
		if (pci_tbl[chip_idx].vendor_id == 0) 		/* Compiled out! */
			continue;

		{
#if defined(PCI_SUPPORT_VER2)
			struct pci_dev *pdev = pci_find_slot(pci_bus, pci_device_fn);
#ifdef VIA_USE_IO
			pciaddr = pdev->base_address[0];
#else
			pciaddr = pdev->base_address[1];
#endif
			irq = pdev->irq;
#else
			u32 pci_memaddr;
			u8 pci_irq_line;
			pcibios_read_config_byte(pci_bus, pci_device_fn,
									 PCI_INTERRUPT_LINE, &pci_irq_line);
#ifdef VIA_USE_IO
			pcibios_read_config_dword(pci_bus, pci_device_fn,
									  PCI_BASE_ADDRESS_0, &pci_memaddr);
			pciaddr = pci_memaddr;
#else
			pcibios_read_config_dword(pci_bus, pci_device_fn,
									  PCI_BASE_ADDRESS_1, &pci_memaddr);
			pciaddr = pci_memaddr;
#endif
			irq = pci_irq_line;
#endif
		}

		if (debug > 2)
			printk(KERN_INFO "Found %s at PCI address %#lx, IRQ %d.\n",
				   pci_tbl[chip_idx].name, pciaddr, irq);

		if (pci_tbl[chip_idx].flags & PCI_USES_IO) {
			if (check_region(pciaddr, pci_tbl[chip_idx].io_size))
				continue;
			ioaddr = pciaddr & ~3;
		} else if ((ioaddr = (long)ioremap(pciaddr & ~0xf,
										 pci_tbl[chip_idx].io_size)) == 0) {
			printk(KERN_INFO "Failed to map PCI address %#lx.\n",
				   pciaddr);
			continue;
		}

		pcibios_read_config_word(pci_bus, pci_device_fn,
								 PCI_COMMAND, &pci_command);
		new_command = pci_command | (pci_tbl[chip_idx].flags & 7);
		if (pci_command != new_command) {
			printk(KERN_INFO "  The PCI BIOS has not enabled the"
				   " device at %d/%d!  Updating PCI command %4.4x->%4.4x.\n",
				   pci_bus, pci_device_fn, pci_command, new_command);
			pcibios_write_config_word(pci_bus, pci_device_fn,
									  PCI_COMMAND, new_command);
		}

		dev = pci_tbl[chip_idx].probe1(pci_bus, pci_device_fn, dev, ioaddr,
									   irq, chip_idx, cards_found);

		if (dev  && (pci_tbl[chip_idx].flags & PCI_COMMAND_MASTER)) {
			u8 pci_latency;
			pcibios_read_config_byte(pci_bus, pci_device_fn,
									 PCI_LATENCY_TIMER, &pci_latency);
			if (pci_latency < min_pci_latency) {
				printk(KERN_INFO "  PCI latency timer (CFLT) is "
					   "unreasonably low at %d.  Setting to %d clocks.\n",
					   pci_latency, min_pci_latency);
				pcibios_write_config_byte(pci_bus, pci_device_fn,
										  PCI_LATENCY_TIMER, min_pci_latency);
			}
		}
		dev = 0;
		cards_found++;
	}

	return cards_found ? 0 : -ENODEV;
}

#ifndef MODULE
int via_rhine_probe(struct device *dev)
{
	return pci_etherdev_probe(dev, pci_tbl);
}
#endif

static struct device *via_probe1(int pci_bus, int pci_devfn,
								 struct device *dev, long ioaddr, int irq,
								 int chip_id, int card_idx)
{
	static int did_version = 0;		/* Already printed version info */
	struct netdev_private *np;
	int i, option = card_idx < MAX_UNITS ? options[card_idx] : 0;

	if (debug > 0 && did_version++ == 0)
		printk(KERN_INFO "%s" KERN_INFO "%s", versionA, versionB);

	dev = init_etherdev(dev, 0);

	printk(KERN_INFO "%s: %s at 0x%lx, ",
		   dev->name, pci_tbl[chip_id].name, ioaddr);

	/* Ideally we would be read the EEPROM but access may be locked. */
	for (i = 0; i <6; i++)
		dev->dev_addr[i] = readb(ioaddr + StationAddr + i);
	for (i = 0; i < 5; i++)
			printk("%2.2x:", dev->dev_addr[i]);
	printk("%2.2x, IRQ %d.\n", dev->dev_addr[i], irq);

#ifdef VIA_USE_IO
	request_region(ioaddr, pci_tbl[chip_id].io_size, dev->name);
#endif

	/* Reset the chip to erase previous misconfiguration. */
	writew(CmdReset, ioaddr + ChipCmd);

	dev->base_addr = ioaddr;
	dev->irq = irq;

	/* Make certain the descriptor lists are cache-aligned. */
	np = (void *)(((long)kmalloc(sizeof(*np), GFP_KERNEL) + 31) & ~31);
	memset(np, 0, sizeof(*np));
	dev->priv = np;

	np->next_module = root_net_dev;
	root_net_dev = dev;

	np->pci_bus = pci_bus;
	np->pci_devfn = pci_devfn;
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
	dev->open = &netdev_open;
	dev->hard_start_xmit = &start_tx;
	dev->stop = &netdev_close;
	dev->get_stats = &get_stats;
	dev->set_multicast_list = &set_rx_mode;
	dev->do_ioctl = &mii_ioctl;

	if (cap_tbl[np->chip_id].flags & CanHaveMII) {
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

	return dev;
}


/* Read and write over the MII Management Data I/O (MDIO) interface. */

static int mdio_read(struct device *dev, int phy_id, int regnum)
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

static void mdio_write(struct device *dev, int phy_id, int regnum, int value)
{
	long ioaddr = dev->base_addr;
	int boguscnt = 1024;

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


static int netdev_open(struct device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	long ioaddr = dev->base_addr;
	int i;

	/* Reset the chip. */
	writew(CmdReset, ioaddr + ChipCmd);

	if (request_irq(dev->irq, &intr_handler, SA_SHIRQ, dev->name, dev))
		return -EAGAIN;

	if (debug > 1)
		printk(KERN_DEBUG "%s: netdev_open() irq %d.\n",
			   dev->name, dev->irq);

	MOD_INC_USE_COUNT;

	init_ring(dev);

	writel(virt_to_bus(np->rx_ring), ioaddr + RxRingPtr);
	writel(virt_to_bus(np->tx_ring), ioaddr + TxRingPtr);

	for (i = 0; i < 6; i++)
		writeb(dev->dev_addr[i], ioaddr + StationAddr + i);

	/* Initialize other registers. */
	writew(0x0006, ioaddr + PCIConfig);	/* Tune configuration??? */
	/* Configure the FIFO thresholds. */
	writeb(0x20, ioaddr + TxConfig);	/* Initial threshold 32 bytes */
	np->tx_thresh = 0x20;
	np->rx_thresh = 0x60;				/* Written in set_rx_mode(). */

	if (dev->if_port == 0)
		dev->if_port = np->default_port;

	dev->tbusy = 0;
	dev->interrupt = 0;
	np->in_interrupt = 0;

	set_rx_mode(dev);

	dev->start = 1;

	/* Enable interrupts by setting the interrupt mask. */
	writew(IntrRxDone | IntrRxErr | IntrRxEmpty| IntrRxOverflow| IntrRxDropped|
		   IntrTxDone | IntrTxAbort | IntrTxUnderrun |
		   IntrPCIErr | IntrStatsMax | IntrLinkChange | IntrMIIChange,
		   ioaddr + IntrEnable);

	np->chip_cmd = CmdStart|CmdTxOn|CmdRxOn|CmdNoTxPoll;
	writew(np->chip_cmd, ioaddr + ChipCmd);

	check_duplex(dev);

	if (debug > 2)
		printk(KERN_DEBUG "%s: Done netdev_open(), status %4.4x "
			   "MII status: %4.4x.\n",
			   dev->name, readw(ioaddr + ChipCmd),
			   mdio_read(dev, np->phys[0], 1));

	/* Set the timer to check for link beat. */
	init_timer(&np->timer);
	np->timer.expires = RUN_AT(1);
	np->timer.data = (unsigned long)dev;
	np->timer.function = &netdev_timer;				/* timer handler */
	add_timer(&np->timer);

	return 0;
}

static void check_duplex(struct device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	long ioaddr = dev->base_addr;
	int mii_reg5 = mdio_read(dev, np->phys[0], 5);
	int duplex;

	if (np->duplex_lock  ||  mii_reg5 == 0xffff)
		return;
	duplex = (mii_reg5 & 0x0100) || (mii_reg5 & 0x01C0) == 0x0040;
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

static void netdev_timer(unsigned long data)
{
	struct device *dev = (struct device *)data;
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	long ioaddr = dev->base_addr;
	int next_tick = 10*HZ;

	if (debug > 3) {
		printk(KERN_DEBUG "%s: VIA Rhine monitor tick, status %4.4x.\n",
			   dev->name, readw(ioaddr + IntrStatus));
	}
	check_duplex(dev);

	np->timer.expires = RUN_AT(next_tick);
	add_timer(&np->timer);
}

static void tx_timeout(struct device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	long ioaddr = dev->base_addr;

	printk(KERN_WARNING "%s: Transmit timed out, status %4.4x, PHY status "
		   "%4.4x, resetting...\n",
		   dev->name, readw(ioaddr + IntrStatus),
		   mdio_read(dev, np->phys[0], 1));

  /* Perhaps we should reinitialize the hardware here. */
  dev->if_port = 0;
  /* Stop and restart the chip's Tx processes . */

  /* Trigger an immediate transmit demand. */

  dev->trans_start = jiffies;
  np->stats.tx_errors++;
  return;
}


/* Initialize the Rx and Tx rings, along with various 'dev' bits. */
static void init_ring(struct device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	int i;

	np->tx_full = 0;
	np->cur_rx = np->cur_tx = 0;
	np->dirty_rx = np->dirty_tx = 0;

	np->rx_buf_sz = (dev->mtu <= 1500 ? PKT_BUF_SZ : dev->mtu + 32);
	np->rx_head_desc = &np->rx_ring[0];

	for (i = 0; i < RX_RING_SIZE; i++) {
		np->rx_ring[i].rx_status = 0;
		np->rx_ring[i].rx_length = 0;
		np->rx_ring[i].desc_length = np->rx_buf_sz;
		np->rx_ring[i].next_desc = virt_to_bus(&np->rx_ring[i+1]);
		np->rx_skbuff[i] = 0;
	}
	/* Mark the last entry as wrapping the ring. */
	np->rx_ring[i-1].next_desc = virt_to_bus(&np->rx_ring[0]);

	/* Fill in the Rx buffers. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		struct sk_buff *skb = dev_alloc_skb(np->rx_buf_sz);
		np->rx_skbuff[i] = skb;
		if (skb == NULL)
			break;
		skb->dev = dev;			/* Mark as being used by this device. */
		np->rx_ring[i].addr = virt_to_bus(skb->tail);
		np->rx_ring[i].rx_status = 0;
		np->rx_ring[i].rx_length = DescOwn;
	}
	np->dirty_rx = (unsigned int)(i - RX_RING_SIZE);

	for (i = 0; i < TX_RING_SIZE; i++) {
		np->tx_skbuff[i] = 0;
		np->tx_ring[i].tx_own = 0;
		np->tx_ring[i].desc_length = 0x00e08000;
		np->tx_ring[i].next_desc = virt_to_bus(&np->tx_ring[i+1]);
		np->tx_buf[i] = kmalloc(PKT_BUF_SZ, GFP_KERNEL);
	}
	np->tx_ring[i-1].next_desc = virt_to_bus(&np->tx_ring[0]);

	return;
}

static int start_tx(struct sk_buff *skb, struct device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	unsigned entry;

	/* Block a timer-based transmit from overlapping.  This could better be
	   done with atomic_swap(1, dev->tbusy), but set_bit() works as well. */
	if (test_and_set_bit(0, (void*)&dev->tbusy) != 0) {
		if (jiffies - dev->trans_start < TX_TIMEOUT)
			return 1;
		tx_timeout(dev);
		return 1;
	}

	/* Caution: the write order is important here, set the field
	   with the "ownership" bits last. */

	/* Calculate the next Tx descriptor entry. */
	entry = np->cur_tx % TX_RING_SIZE;

	np->tx_skbuff[entry] = skb;

	if ((long)skb->data & 3) {			/* Must use alignment buffer. */
		if (np->tx_buf[entry] == NULL &&
			(np->tx_buf[entry] = kmalloc(PKT_BUF_SZ, GFP_KERNEL)) == NULL)
			return 1;
		memcpy(np->tx_buf[entry], skb->data, skb->len);
		np->tx_ring[entry].addr = virt_to_bus(np->tx_buf[entry]);
	} else
		np->tx_ring[entry].addr = virt_to_bus(skb->data);

	np->tx_ring[entry].desc_length = 0x00E08000 |
		(skb->len >= ETH_ZLEN ? skb->len : ETH_ZLEN);
	np->tx_ring[entry].tx_own = DescOwn;

	np->cur_tx++;

	/* Non-x86 Todo: explicitly flush cache lines here. */

	/* Wake the potentially-idle transmit channel. */
	writew(CmdTxDemand | np->chip_cmd, dev->base_addr + ChipCmd);

	if (np->cur_tx - np->dirty_tx < TX_RING_SIZE - 1)
		clear_bit(0, (void*)&dev->tbusy);		/* Typical path */
	else
		np->tx_full = 1;
	dev->trans_start = jiffies;

	if (debug > 4) {
		printk(KERN_DEBUG "%s: Transmit frame #%d queued in slot %d.\n",
			   dev->name, np->cur_tx, entry);
	}
	return 0;
}

/* The interrupt handler does all of the Rx thread work and cleans up
   after the Tx thread. */
static void intr_handler(int irq, void *dev_instance, struct pt_regs *rgs)
{
	struct device *dev = (struct device *)dev_instance;
	struct netdev_private *np;
	long ioaddr, boguscnt = max_interrupt_work;

	ioaddr = dev->base_addr;
	np = (struct netdev_private *)dev->priv;
#if defined(__i386__)
	/* A lock to prevent simultaneous entry bug on Intel SMP machines. */
	if (test_and_set_bit(0, (void*)&dev->interrupt)) {
		printk(KERN_ERR"%s: SMP simultaneous entry of an interrupt handler.\n",
			   dev->name);
		dev->interrupt = 0;	/* Avoid halting machine. */
		return;
	}
#else
	if (dev->interrupt) {
		printk(KERN_ERR "%s: Re-entering the interrupt handler.\n", dev->name);
		return;
	}
	dev->interrupt = 1;
#endif

	do {
		u32 intr_status = readw(ioaddr + IntrStatus);

		/* Acknowledge all of the current interrupt sources ASAP. */
		writew(intr_status & 0xffff, ioaddr + IntrStatus);

		if (debug > 4)
			printk(KERN_DEBUG "%s: Interrupt, status %4.4x.\n",
				   dev->name, intr_status);

		if (intr_status == 0)
			break;

		if (intr_status & (IntrRxDone | IntrRxErr | IntrRxDropped |
						   IntrRxWakeUp | IntrRxEmpty | IntrRxNoBuf))
			netdev_rx(dev);

		for (; np->cur_tx - np->dirty_tx > 0; np->dirty_tx++) {
			int entry = np->dirty_tx % TX_RING_SIZE;
			int txstatus;
			if (np->tx_ring[entry].tx_own)
				break;
			txstatus = np->tx_ring[entry].tx_status;
			if (debug > 6)
				printk(KERN_DEBUG " Tx scavenge %d status %4.4x.\n",
					   entry, txstatus);
			if (txstatus & 0x8000) {
				if (debug > 1)
					printk(KERN_DEBUG "%s: Transmit error, Tx status %4.4x.\n",
						   dev->name, txstatus);
				np->stats.tx_errors++;
				if (txstatus & 0x0400) np->stats.tx_carrier_errors++;
				if (txstatus & 0x0200) np->stats.tx_window_errors++;
				if (txstatus & 0x0100) np->stats.tx_aborted_errors++;
				if (txstatus & 0x0080) np->stats.tx_heartbeat_errors++;
				if (txstatus & 0x0002) np->stats.tx_fifo_errors++;
#ifdef ETHER_STATS
				if (txstatus & 0x0100) np->stats.collisions16++;
#endif
				/* Transmitter restarted in 'abnormal' handler. */
			} else {
#ifdef ETHER_STATS
				if (txstatus & 0x0001) np->stats.tx_deferred++;
#endif
				np->stats.collisions += (txstatus >> 3) & 15;
#if defined(NETSTATS_VER2)
				np->stats.tx_bytes += np->tx_ring[entry].desc_length & 0x7ff;
#endif
				np->stats.tx_packets++;
			}
			/* Free the original skb. */
			dev_free_skb(np->tx_skbuff[entry]);
			np->tx_skbuff[entry] = 0;
		}
		if (np->tx_full && dev->tbusy
			&& np->cur_tx - np->dirty_tx < TX_RING_SIZE - 4) {
			/* The ring is no longer full, clear tbusy. */
			np->tx_full = 0;
			clear_bit(0, (void*)&dev->tbusy);
			mark_bh(NET_BH);
		}

		/* Abnormal error summary/uncommon events handlers. */
		if (intr_status & (IntrPCIErr | IntrLinkChange | IntrMIIChange |
						   IntrStatsMax | IntrTxAbort | IntrTxUnderrun))
			netdev_error(dev, intr_status);

		if (--boguscnt < 0) {
			printk(KERN_WARNING "%s: Too much work at interrupt, "
				   "status=0x%4.4x.\n",
				   dev->name, intr_status);
			break;
		}
	} while (1);

	if (debug > 3)
		printk(KERN_DEBUG "%s: exiting interrupt, status=%#4.4x.\n",
			   dev->name, readw(ioaddr + IntrStatus));

#if defined(__i386__)
	clear_bit(0, (void*)&dev->interrupt);
#else
	dev->interrupt = 0;
#endif
	return;
}

/* This routine is logically part of the interrupt handler, but isolated
   for clarity and better register allocation. */
static int netdev_rx(struct device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	int entry = np->cur_rx % RX_RING_SIZE;
	int boguscnt = np->dirty_rx + RX_RING_SIZE - np->cur_rx;

	if (debug > 4) {
		printk(KERN_DEBUG " In netdev_rx(), entry %d status %4.4x.\n",
			   entry, np->rx_head_desc->rx_length);
	}

	/* If EOP is set on the next entry, it's a new packet. Send it up. */
	while ( ! (np->rx_head_desc->rx_length & DescOwn)) {
		struct rx_desc *desc = np->rx_head_desc;
		int data_size = desc->rx_length;
		u16 desc_status = desc->rx_status;

		if (debug > 4)
			printk(KERN_DEBUG "  netdev_rx() status is %4.4x.\n",
				   desc_status);
		if (--boguscnt < 0)
			break;
		if ( (desc_status & (RxWholePkt | RxErr)) !=  RxWholePkt) {
			if ((desc_status & RxWholePkt) !=  RxWholePkt) {
				printk(KERN_WARNING "%s: Oversized Ethernet frame spanned "
					   "multiple buffers, entry %#x length %d status %4.4x!\n",
					   dev->name, np->cur_rx, data_size, desc_status);
				printk(KERN_WARNING "%s: Oversized Ethernet frame %p vs %p.\n",
					   dev->name, np->rx_head_desc,
					   &np->rx_ring[np->cur_rx % RX_RING_SIZE]);
				np->stats.rx_length_errors++;
			} else if (desc_status & RxErr) {
				/* There was a error. */
				if (debug > 2)
					printk(KERN_DEBUG "  netdev_rx() Rx error was %8.8x.\n",
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
			np->rx_ring[entry].addr = virt_to_bus(skb->tail);
		}
		np->rx_ring[entry].rx_status = 0;
		np->rx_ring[entry].rx_length = DescOwn;
	}

	/* Pre-emptively restart Rx engine. */
	writew(CmdRxDemand | np->chip_cmd, dev->base_addr + ChipCmd);
	return 0;
}

static void netdev_error(struct device *dev, int intr_status)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	long ioaddr = dev->base_addr;

	if (intr_status & (IntrMIIChange | IntrLinkChange)) {
		if (readb(ioaddr + MIIStatus) & 0x02)
			/* Link failed, restart autonegotiation. */
			mdio_write(dev, np->phys[0], 0, 0x3300);
		else
			check_duplex(dev);
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
	if ((intr_status & ~(IntrLinkChange|IntrStatsMax|IntrTxAbort)) && debug) {
		printk(KERN_ERR "%s: Something Wicked happened! %4.4x.\n",
			   dev->name, intr_status);
		/* Recovery for other fault sources not known. */
		writew(CmdTxDemand | np->chip_cmd, dev->base_addr + ChipCmd);
	}
}

static struct enet_statistics *get_stats(struct device *dev)
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

static void set_rx_mode(struct device *dev)
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
		rx_mode = 0x0C;
	} else {
		struct dev_mc_list *mclist;
		int i;
		memset(mc_filter, 0, sizeof(mc_filter));
		for (i = 0, mclist = dev->mc_list; mclist && i < dev->mc_count;
			 i++, mclist = mclist->next) {
			set_bit(ether_crc(ETH_ALEN, mclist->dmi_addr) >> 26,
					mc_filter);
		}
		writel(mc_filter[0], ioaddr + MulticastFilter0);
		writel(mc_filter[1], ioaddr + MulticastFilter1);
		rx_mode = 0x0C;
	}
	writeb(np->rx_thresh | rx_mode, ioaddr + RxConfig);
}

static int mii_ioctl(struct device *dev, struct ifreq *rq, int cmd)
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
		if (!suser())
			return -EPERM;
		mdio_write(dev, data[0] & 0x1f, data[1] & 0x1f, data[2]);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int netdev_close(struct device *dev)
{
	long ioaddr = dev->base_addr;
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	int i;

	dev->start = 0;
	dev->tbusy = 1;

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
		np->rx_ring[i].rx_length = 0;
		np->rx_ring[i].addr = 0xBADF00D0; /* An invalid address. */
		if (np->rx_skbuff[i]) {
#if LINUX_VERSION_CODE < 0x20100
			np->rx_skbuff[i]->free = 1;
#endif
			dev_free_skb(np->rx_skbuff[i]);
		}
		np->rx_skbuff[i] = 0;
	}
	for (i = 0; i < TX_RING_SIZE; i++) {
		if (np->tx_skbuff[i])
			dev_free_skb(np->tx_skbuff[i]);
		np->tx_skbuff[i] = 0;
	}

	MOD_DEC_USE_COUNT;

	return 0;
}


#ifdef MODULE
int init_module(void)
{
	if (debug)					/* Emit version even if no cards detected. */
		printk(KERN_INFO "%s" KERN_INFO "%s", versionA, versionB);
#ifdef CARDBUS
	register_driver(&etherdev_ops);
	return 0;
#else
	return pci_etherdev_probe(NULL, pci_tbl);
#endif
}

void cleanup_module(void)
{

#ifdef CARDBUS
	unregister_driver(&etherdev_ops);
#endif

	/* No need to check MOD_IN_USE, as sys_delete_module() checks. */
	while (root_net_dev) {
		struct netdev_private *np =
			(struct netdev_private *)(root_net_dev->priv);
		unregister_netdev(root_net_dev);
#ifdef VIA_USE_IO
		release_region(root_net_dev->base_addr, pci_tbl[np->chip_id].io_size);
#else
		iounmap((char *)(root_net_dev->base_addr));
#endif
		kfree(root_net_dev);
		root_net_dev = np->next_module;
#if 0
		kfree(np);				/* Assumption: no struct realignment. */
#endif
	}
}

#endif  /* MODULE */

/*
 * Local variables:
 *  compile-command: "gcc -DMODULE -D__KERNEL__ -I/usr/src/linux/net/inet -Wall -Wstrict-prototypes -O6 -c via-rhine.c `[ -f /usr/include/linux/modversions.h ] && echo -DMODVERSIONS`"
 *  SMP-compile-command: "gcc -D__SMP__ -DMODULE -D__KERNEL__ -I/usr/src/linux/net/inet -Wall -Wstrict-prototypes -O6 -c via-rhine.c `[ -f /usr/include/linux/modversions.h ] && echo -DMODVERSIONS`"
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 * End:
 */
