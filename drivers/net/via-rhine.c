/* via-rhine.c: A Linux Ethernet device driver for VIA Rhine family chips. */
/*
	Written 1998-2001 by Donald Becker.

	This software may be used and distributed according to the terms of
	the GNU General Public License (GPL), incorporated herein by reference.
	Drivers based on or derived from this code fall under the GPL and must
	retain the authorship, copyright and license notice.  This file is not
	a complete program and may only be used when the entire operating
	system is licensed under the GPL.

	This driver is designed for the VIA VT86c100A Rhine-II PCI Fast Ethernet
	controller.  It also works with the older 3043 Rhine-I chip.

	The author may be reached as becker@scyld.com, or C/O
	Scyld Computing Corporation
	410 Severn Ave., Suite 210
	Annapolis MD 21403


	This driver contains some changes from the original Donald Becker
	version. He may or may not be interested in bug reports on this
	code. You can find his versions at:
	http://www.scyld.com/network/via-rhine.html


	Linux kernel version history:
	
	LK1.1.0:
	- Jeff Garzik: softnet 'n stuff
	
	LK1.1.1:
	- Justin Guyett: softnet and locking fixes
	- Jeff Garzik: use PCI interface

	LK1.1.2:
	- Urban Widmark: minor cleanups, merges from Becker 1.03a/1.04 versions

	LK1.1.3:
	- Urban Widmark: use PCI DMA interface (with thanks to the eepro100.c
			 code) update "Theory of Operation" with
			 softnet/locking changes
	- Dave Miller: PCI DMA and endian fixups
	- Jeff Garzik: MOD_xxx race fixes, updated PCI resource allocation

	LK1.1.4:
	- Urban Widmark: fix gcc 2.95.2 problem and
	                 remove writel's to fixed address 0x7c

	LK1.1.5:
	- Urban Widmark: mdio locking, bounce buffer changes
	                 merges from Beckers 1.05 version
	                 added netif_running_on/off support

	LK1.1.6:
	- Urban Widmark: merges from Beckers 1.08b version (VT6102 + mdio)
	                 set netif_running_on/off on startup, del_timer_sync
	
	LK1.1.7:
	- Manfred Spraul: added reset into tx_timeout

	LK1.1.9:
	- Urban Widmark: merges from Beckers 1.10 version
	                 (media selection + eeprom reload)
	- David Vrabel:  merges from D-Link "1.11" version
	                 (disable WOL and PME on startup)

	LK1.1.10:
	- Manfred Spraul: use "singlecopy" for unaligned buffers
	                  don't allocate bounce buffers for !ReqTxAlign cards

	LK1.1.11:
	- David Woodhouse: Set dev->base_addr before the first time we call
					   wait_for_reset(). It's a lot happier that way.
					   Free np->tx_bufs only if we actually allocated it.

	LK1.1.12:
	- Martin Eriksson: Allow Memory-Mapped IO to be enabled.
*/


/* A few user-configurable values.
   These may be modified when a driver module is loaded. */

static int debug = 1;			/* 1 normal messages, 0 quiet .. 7 verbose. */
static int max_interrupt_work = 20;

/* Set the copy breakpoint for the copy-only-tiny-frames scheme.
   Setting to > 1518 effectively disables this feature. */
static int rx_copybreak;

/* Used to pass the media type, etc.
   Both 'options[]' and 'full_duplex[]' should exist for driver
   interoperability.
   The media type is usually passed in 'options[]'.
   The default is autonegotation for speed and duplex.
     This should rarely be overridden.
   Use option values 0x10/0x20 for 10Mbps, 0x100,0x200 for 100Mbps.
   Use option values 0x10 and 0x100 for forcing half duplex fixed speed.
   Use option values 0x20 and 0x200 for forcing full duplex operation.
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
#define TX_RING_SIZE	16
#define TX_QUEUE_LEN	10		/* Limit ring entries actually used.  */
#define RX_RING_SIZE	16


/* Operational parameters that usually are not changed. */

/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT  (2*HZ)

#define PKT_BUF_SZ		1536			/* Size of each temporary Rx buffer.*/

/* max time out delay time */
#define W_MAX_TIMEOUT	0x0FFFU

#if !defined(__OPTIMIZE__)  ||  !defined(__KERNEL__)
#warning  You must compile this file with the correct options!
#warning  See the last lines of the source file.
#error  You must compile this driver with "-O".
#endif

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/mii.h>
#include <asm/processor.h>		/* Processor type for cache alignment. */
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/irq.h>

/* These identify the driver base version and may not be removed. */
static char version[] __devinitdata =
KERN_INFO "via-rhine.c:v1.10-LK1.1.12  03/11/2001  Written by Donald Becker\n"
KERN_INFO "  http://www.scyld.com/network/via-rhine.html\n";

static char shortname[] __devinitdata = "via-rhine";


/* This driver was written to use PCI memory space, however most versions
   of the Rhine only work correctly with I/O space accesses. */
#ifdef CONFIG_VIA_RHINE_MMIO
#define USE_MEM
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

MODULE_AUTHOR("Donald Becker <becker@scyld.com>");
MODULE_DESCRIPTION("VIA Rhine PCI Fast Ethernet driver");
MODULE_LICENSE("GPL");

MODULE_PARM(max_interrupt_work, "i");
MODULE_PARM(debug, "i");
MODULE_PARM(rx_copybreak, "i");
MODULE_PARM(options, "1-" __MODULE_STRING(MAX_UNITS) "i");
MODULE_PARM(full_duplex, "1-" __MODULE_STRING(MAX_UNITS) "i");
MODULE_PARM_DESC(max_interrupt_work, "VIA Rhine maximum events handled per interrupt");
MODULE_PARM_DESC(debug, "VIA Rhine debug level (0-7)");
MODULE_PARM_DESC(rx_copybreak, "VIA Rhine copy breakpoint for copy-only-tiny-frames");
MODULE_PARM_DESC(options, "VIA Rhine: Bits 0-3: media type, bit 17: full duplex");
MODULE_PARM_DESC(full_duplex, "VIA Rhine full duplex setting(s) (1)");

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
boundaries, the IP header at offset 14 in an ethernet frame isn't
longword aligned for further processing.  Copying these unaligned buffers
has the beneficial effect of 16-byte aligning the IP header.

IIId. Synchronization

The driver runs as two independent, single-threaded flows of control.  One
is the send-packet routine, which enforces single-threaded use by the
dev->priv->lock spinlock. The other thread is the interrupt handler, which 
is single threaded by the hardware and interrupt handling software.

The send packet thread has partial control over the Tx ring. It locks the 
dev->priv->lock whenever it's queuing a Tx packet. If the next slot in the ring
is not available it stops the transmit queue by calling netif_stop_queue.

The interrupt handler has exclusive control over the Rx ring and records stats
from the Tx ring.  After reaping the stats, it marks the Tx queue entry as
empty by incrementing the dirty_tx mark. If at least half of the entries in
the Rx ring are available the transmit queue is woken up if it was stopped.

IV. Notes

IVb. References

Preliminary VT86C100A manual from http://www.via.com.tw/
http://www.scyld.com/expert/100mbps.html
http://www.scyld.com/expert/NWay.html
ftp://ftp.via.com.tw/public/lan/Products/NIC/VT86C100A/Datasheet/VT86C100A03.pdf
ftp://ftp.via.com.tw/public/lan/Products/NIC/VT6102/Datasheet/VT6102_021.PDF


IVc. Errata

The VT86C100A manual is not reliable information.
The 3043 chip does not handle unaligned transmit or receive buffers, resulting
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
	VT6102,
	VT3043,
};

struct via_rhine_chip_info {
	const char *name;
	u16 pci_flags;
	int io_size;
	int drv_flags;
};


enum chip_capability_flags {
	CanHaveMII=1, HasESIPhy=2, HasDavicomPhy=4,
	ReqTxAlign=0x10, HasWOL=0x20, };

#ifdef USE_MEM
#define RHINE_IOTYPE (PCI_USES_MEM | PCI_USES_MASTER | PCI_ADDR1)
#else
#define RHINE_IOTYPE (PCI_USES_IO  | PCI_USES_MASTER | PCI_ADDR0)
#endif

/* directly indexed by enum via_rhine_chips, above */
static struct via_rhine_chip_info via_rhine_chip_info[] __devinitdata =
{
	{ "VIA VT86C100A Rhine", RHINE_IOTYPE, 128,
	  CanHaveMII | ReqTxAlign },
	{ "VIA VT6102 Rhine-II", RHINE_IOTYPE, 256,
	  CanHaveMII | HasWOL },
	{ "VIA VT3043 Rhine",    RHINE_IOTYPE, 128,
	  CanHaveMII | ReqTxAlign }
};

static struct pci_device_id via_rhine_pci_tbl[] __devinitdata =
{
	{0x1106, 0x6100, PCI_ANY_ID, PCI_ANY_ID, 0, 0, VT86C100A},
	{0x1106, 0x3065, PCI_ANY_ID, PCI_ANY_ID, 0, 0, VT6102},
	{0x1106, 0x3043, PCI_ANY_ID, PCI_ANY_ID, 0, 0, VT3043},
	{0,}			/* terminate list */
};
MODULE_DEVICE_TABLE(pci, via_rhine_pci_tbl);


/* Offsets to the device registers. */
enum register_offsets {
	StationAddr=0x00, RxConfig=0x06, TxConfig=0x07, ChipCmd=0x08,
	IntrStatus=0x0C, IntrEnable=0x0E,
	MulticastFilter0=0x10, MulticastFilter1=0x14,
	RxRingPtr=0x18, TxRingPtr=0x1C, GFIFOTest=0x54,
	MIIPhyAddr=0x6C, MIIStatus=0x6D, PCIBusConfig=0x6E,
	MIICmd=0x70, MIIRegAddr=0x71, MIIData=0x72, MACRegEEcsr=0x74,
	ConfigA=0x78, ConfigB=0x79, ConfigC=0x7A, ConfigD=0x7B,
	RxMissed=0x7C, RxCRCErrs=0x7E,
	StickyHW=0x83, WOLcrClr=0xA4, WOLcgClr=0xA7, PwrcsrClr=0xAC,
};

#ifdef USE_MEM
/* Registers we check that mmio and reg are the same. */
int mmio_verify_registers[] = {
	RxConfig, TxConfig, IntrEnable, ConfigA, ConfigB, ConfigC, ConfigD,
	0
};
#endif

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

/* MII interface, status flags.
   Not to be confused with the MIIStatus register ... */
enum mii_status_bits {
	MIICap100T4			= 0x8000,
	MIICap10100HdFd		= 0x7800,
	MIIPreambleSupr		= 0x0040,
	MIIAutoNegCompleted	= 0x0020,
	MIIRemoteFault		= 0x0010,
	MIICapAutoNeg		= 0x0008,
	MIILink				= 0x0004,
	MIIJabber			= 0x0002,
	MIIExtended			= 0x0001
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

#define MAX_MII_CNT	4
struct netdev_private {
	/* Descriptor rings */
	struct rx_desc *rx_ring;
	struct tx_desc *tx_ring;
	dma_addr_t rx_ring_dma;
	dma_addr_t tx_ring_dma;

	/* The addresses of receive-in-place skbuffs. */
	struct sk_buff *rx_skbuff[RX_RING_SIZE];
	dma_addr_t rx_skbuff_dma[RX_RING_SIZE];

	/* The saved address of a sent-in-place packet/buffer, for later free(). */
	struct sk_buff *tx_skbuff[TX_RING_SIZE];
	dma_addr_t tx_skbuff_dma[TX_RING_SIZE];

	/* Tx bounce buffers */
	unsigned char *tx_buf[TX_RING_SIZE];
	unsigned char *tx_bufs;
	dma_addr_t tx_bufs_dma;

	struct pci_dev *pdev;
	struct net_device_stats stats;
	struct timer_list timer;	/* Media monitoring timer. */
	spinlock_t lock;

	/* Frequently used values: keep some adjacent for cache effect. */
	int chip_id, drv_flags;
	struct rx_desc *rx_head_desc;
	unsigned int cur_rx, dirty_rx;		/* Producer/consumer ring indices */
	unsigned int cur_tx, dirty_tx;
	unsigned int rx_buf_sz;				/* Based on MTU+slack. */
	u16 chip_cmd;						/* Current setting for ChipCmd */

	/* These values are keep track of the transceiver/media in use. */
	unsigned int full_duplex:1;			/* Full-duplex operation requested. */
	unsigned int duplex_lock:1;
	unsigned int default_port:4;		/* Last dev->if_port value. */
	u8 tx_thresh, rx_thresh;

	/* MII transceiver section. */
	u16 advertising;					/* NWay media advertisement */
	unsigned char phys[MAX_MII_CNT];			/* MII device addresses. */
	unsigned int mii_cnt;			/* number of MIIs found, but only the first one is used */
	u16 mii_status;						/* last read MII status */
};

static int  mdio_read(struct net_device *dev, int phy_id, int location);
static void mdio_write(struct net_device *dev, int phy_id, int location, int value);
static int  via_rhine_open(struct net_device *dev);
static void via_rhine_check_duplex(struct net_device *dev);
static void via_rhine_timer(unsigned long data);
static void via_rhine_tx_timeout(struct net_device *dev);
static int  via_rhine_start_tx(struct sk_buff *skb, struct net_device *dev);
static void via_rhine_interrupt(int irq, void *dev_instance, struct pt_regs *regs);
static void via_rhine_tx(struct net_device *dev);
static void via_rhine_rx(struct net_device *dev);
static void via_rhine_error(struct net_device *dev, int intr_status);
static void via_rhine_set_rx_mode(struct net_device *dev);
static struct net_device_stats *via_rhine_get_stats(struct net_device *dev);
static int mii_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
static int  via_rhine_close(struct net_device *dev);
static inline void clear_tally_counters(long ioaddr);

static void wait_for_reset(struct net_device *dev, char *name)
{
	struct netdev_private *np = dev->priv;
	long ioaddr = dev->base_addr;
	int chip_id = np->chip_id;
	int i;

	/* 3043 may need long delay after reset (dlink) */
	if (chip_id == VT3043 || chip_id == VT86C100A)
		udelay(100);

	i = 0;
	do {
		udelay(5);
		i++;
		if(i > 2000) {
			printk(KERN_ERR "%s: reset did not complete in 10 ms.\n", name);
			break;
		}
	} while(readw(ioaddr + ChipCmd) & CmdReset);
	if (debug > 1)
		printk(KERN_INFO "%s: reset finished after %d microseconds.\n",
			   name, 5*i);
}

#ifdef USE_MEM
static void __devinit enable_mmio(long ioaddr, int chip_id)
{
	int n;
	if (chip_id == VT3043 || chip_id == VT86C100A) {
		/* More recent docs say that this bit is reserved ... */
		n = inb(ioaddr + ConfigA) | 0x20;
		outb(n, ioaddr + ConfigA);
	} else if (chip_id == VT6102) {
		n = inb(ioaddr + ConfigD) | 0x80;
		outb(n, ioaddr + ConfigD);
	}
}
#endif

static void __devinit reload_eeprom(long ioaddr)
{
	int i;
	outb(0x20, ioaddr + MACRegEEcsr);
	/* Typically 2 cycles to reload. */
	for (i = 0; i < 150; i++)
		if (! (inb(ioaddr + MACRegEEcsr) & 0x20))
			break;
}

static int __devinit via_rhine_init_one (struct pci_dev *pdev,
					 const struct pci_device_id *ent)
{
	struct net_device *dev;
	struct netdev_private *np;
	int i, option;
	int chip_id = (int) ent->driver_data;
	static int card_idx = -1;
	long ioaddr;
	long memaddr;
	int io_size;
	int pci_flags;
#ifdef USE_MEM
	long ioaddr0;
#endif
	
/* when built into the kernel, we only print version if device is found */
#ifndef MODULE
	static int printed_version;
	if (!printed_version++)
		printk(version);
#endif

	card_idx++;
	option = card_idx < MAX_UNITS ? options[card_idx] : 0;
	io_size = via_rhine_chip_info[chip_id].io_size;
	pci_flags = via_rhine_chip_info[chip_id].pci_flags;

	if (pci_enable_device (pdev))
		goto err_out;

	/* this should always be supported */
	if (pci_set_dma_mask(pdev, 0xffffffff)) {
		printk(KERN_ERR "32-bit PCI DMA addresses not supported by the card!?\n");
		goto err_out;
	}
	
	/* sanity check */
	if ((pci_resource_len (pdev, 0) < io_size) ||
	    (pci_resource_len (pdev, 1) < io_size)) {
		printk (KERN_ERR "Insufficient PCI resources, aborting\n");
		goto err_out;
	}

	ioaddr = pci_resource_start (pdev, 0);
	memaddr = pci_resource_start (pdev, 1);

	if (pci_flags & PCI_USES_MASTER)
		pci_set_master (pdev);

	dev = alloc_etherdev(sizeof(*np));
	if (dev == NULL) {
		printk (KERN_ERR "init_ethernet failed for card #%d\n", card_idx);
		goto err_out;
	}
	SET_MODULE_OWNER(dev);
	
	if (pci_request_regions(pdev, shortname))
		goto err_out_free_netdev;

#ifdef USE_MEM
	ioaddr0 = ioaddr;
	enable_mmio(ioaddr0, chip_id);

	ioaddr = (long) ioremap (memaddr, io_size);
	if (!ioaddr) {
		printk (KERN_ERR "ioremap failed for device %s, region 0x%X @ 0x%lX\n",
				pdev->slot_name, io_size, memaddr);
		goto err_out_free_res;
	}

	/* Check that selected MMIO registers match the PIO ones */
	i = 0;
	while (mmio_verify_registers[i]) {
		int reg = mmio_verify_registers[i++];
		unsigned char a = inb(ioaddr0+reg);
		unsigned char b = readb(ioaddr+reg);
		if (a != b) {
			printk (KERN_ERR "MMIO do not match PIO [%02x] (%02x != %02x)\n",
					reg, a, b);
			goto err_out_unmap;
		}
	}
#endif

	/* D-Link provided reset code (with comment additions) */
	if (via_rhine_chip_info[chip_id].drv_flags & HasWOL) {
		unsigned char byOrgValue;

		/* clear sticky bit before reset & read ethernet address */
		byOrgValue = readb(ioaddr + StickyHW);
		byOrgValue = byOrgValue & 0xFC;
		writeb(byOrgValue, ioaddr + StickyHW);

		/* (bits written are cleared?) */
		/* disable force PME-enable */
		writeb(0x80, ioaddr + WOLcgClr);
		/* disable power-event config bit */
		writeb(0xFF, ioaddr + WOLcrClr);
		/* clear power status (undocumented in vt6102 docs?) */
		writeb(0xFF, ioaddr + PwrcsrClr);
	}

	/* Reset the chip to erase previous misconfiguration. */
	writew(CmdReset, ioaddr + ChipCmd);

	dev->base_addr = ioaddr;
	wait_for_reset(dev, shortname);

	/* Reload the station address from the EEPROM. */
#ifdef USE_IO
	reload_eeprom(ioaddr);
#else
	reload_eeprom(ioaddr0);
	/* Reloading from eeprom overwrites cfgA-D, so we must re-enable MMIO.
	   If reload_eeprom() was done first this could be avoided, but it is
	   not known if that still works with the "win98-reboot" problem. */
	enable_mmio(ioaddr0, chip_id);
#endif

	for (i = 0; i < 6; i++)
		dev->dev_addr[i] = readb(ioaddr + StationAddr + i);

	if (!is_valid_ether_addr(dev->dev_addr)) {
		printk(KERN_ERR "Invalid MAC address for card #%d\n", card_idx);
		goto err_out_unmap;
	}

	if (chip_id == VT6102) {
		/*
		 * for 3065D, EEPROM reloaded will cause bit 0 in MAC_REG_CFGA
		 * turned on.  it makes MAC receive magic packet
		 * automatically. So, we turn it off. (D-Link)
		 */
		writeb(readb(ioaddr + ConfigA) & 0xFE, ioaddr + ConfigA);
	}

	dev->irq = pdev->irq;

	np = dev->priv;
	spin_lock_init (&np->lock);
	np->chip_id = chip_id;
	np->drv_flags = via_rhine_chip_info[chip_id].drv_flags;
	np->pdev = pdev;

	if (dev->mem_start)
		option = dev->mem_start;

	/* The lower four bits are the media type. */
	if (option > 0) {
		if (option & 0x200)
			np->full_duplex = 1;
		np->default_port = option & 15;
	}
	if (card_idx < MAX_UNITS  &&  full_duplex[card_idx] > 0)
		np->full_duplex = 1;

	if (np->full_duplex) {
		printk(KERN_INFO "%s: Set to forced full duplex, autonegotiation"
			   " disabled.\n", dev->name);
		np->duplex_lock = 1;
	}

	/* The chip-specific entries in the device structure. */
	dev->open = via_rhine_open;
	dev->hard_start_xmit = via_rhine_start_tx;
	dev->stop = via_rhine_close;
	dev->get_stats = via_rhine_get_stats;
	dev->set_multicast_list = via_rhine_set_rx_mode;
	dev->do_ioctl = mii_ioctl;
	dev->tx_timeout = via_rhine_tx_timeout;
	dev->watchdog_timeo = TX_TIMEOUT;
	if (np->drv_flags & ReqTxAlign)
		dev->features |= NETIF_F_SG|NETIF_F_HW_CSUM;
	
	i = register_netdev(dev);
	if (i)
		goto err_out_unmap;

	printk(KERN_INFO "%s: %s at 0x%lx, ",
		   dev->name, via_rhine_chip_info[chip_id].name,
		   (pci_flags & PCI_USES_IO) ? ioaddr : memaddr);

	for (i = 0; i < 5; i++)
			printk("%2.2x:", dev->dev_addr[i]);
	printk("%2.2x, IRQ %d.\n", dev->dev_addr[i], pdev->irq);

	pci_set_drvdata(pdev, dev);

	if (np->drv_flags & CanHaveMII) {
		int phy, phy_idx = 0;
		np->phys[0] = 1;		/* Standard for this chip. */
		for (phy = 1; phy < 32 && phy_idx < MAX_MII_CNT; phy++) {
			int mii_status = mdio_read(dev, phy, 1);
			if (mii_status != 0xffff  &&  mii_status != 0x0000) {
				np->phys[phy_idx++] = phy;
				np->advertising = mdio_read(dev, phy, 4);
				printk(KERN_INFO "%s: MII PHY found at address %d, status "
					   "0x%4.4x advertising %4.4x Link %4.4x.\n",
					   dev->name, phy, mii_status, np->advertising,
					   mdio_read(dev, phy, 5));

				/* set IFF_RUNNING */
				if (mii_status & MIILink)
					netif_carrier_on(dev);
				else
					netif_carrier_off(dev);
			}
		}
		np->mii_cnt = phy_idx;
	}

	/* Allow forcing the media type. */
	if (option > 0) {
		if (option & 0x220)
			np->full_duplex = 1;
		np->default_port = option & 0x3ff;
		if (np->default_port & 0x330) {
			/* FIXME: shouldn't someone check this variable? */
			/* np->medialock = 1; */
			printk(KERN_INFO "  Forcing %dMbs %s-duplex operation.\n",
				   (option & 0x300 ? 100 : 10),
				   (option & 0x220 ? "full" : "half"));
			if (np->mii_cnt)
				mdio_write(dev, np->phys[0], 0,
						   ((option & 0x300) ? 0x2000 : 0) |  /* 100mbps? */
						   ((option & 0x220) ? 0x0100 : 0));  /* Full duplex? */
		}
	}

	return 0;

err_out_unmap:
#ifdef USE_MEM
	iounmap((void *)ioaddr);
err_out_free_res:
#endif
	pci_release_regions(pdev);
err_out_free_netdev:
	kfree (dev);
err_out:
	return -ENODEV;
}

static int alloc_ring(struct net_device* dev)
{
	struct netdev_private *np = dev->priv;
	void *ring;
	dma_addr_t ring_dma;

	ring = pci_alloc_consistent(np->pdev, 
				    RX_RING_SIZE * sizeof(struct rx_desc) +
				    TX_RING_SIZE * sizeof(struct tx_desc),
				    &ring_dma);
	if (!ring) {
		printk(KERN_ERR "Could not allocate DMA memory.\n");
		return -ENOMEM;
	}
	if (np->drv_flags & ReqTxAlign) {
		np->tx_bufs = pci_alloc_consistent(np->pdev, PKT_BUF_SZ * TX_RING_SIZE,
								   &np->tx_bufs_dma);
		if (np->tx_bufs == NULL) {
			pci_free_consistent(np->pdev, 
				    RX_RING_SIZE * sizeof(struct rx_desc) +
				    TX_RING_SIZE * sizeof(struct tx_desc),
				    ring, ring_dma);
			return -ENOMEM;
		}
	}

	np->rx_ring = ring;
	np->tx_ring = ring + RX_RING_SIZE * sizeof(struct rx_desc);
	np->rx_ring_dma = ring_dma;
	np->tx_ring_dma = ring_dma + RX_RING_SIZE * sizeof(struct rx_desc);

	return 0;
}

void free_ring(struct net_device* dev)
{
	struct netdev_private *np = dev->priv;

	pci_free_consistent(np->pdev, 
			    RX_RING_SIZE * sizeof(struct rx_desc) +
			    TX_RING_SIZE * sizeof(struct tx_desc),
			    np->rx_ring, np->rx_ring_dma);
	np->tx_ring = NULL;

	if (np->tx_bufs)
		pci_free_consistent(np->pdev, PKT_BUF_SZ * TX_RING_SIZE,
							np->tx_bufs, np->tx_bufs_dma);

	np->tx_bufs = NULL;

}

static void alloc_rbufs(struct net_device *dev)
{
	struct netdev_private *np = dev->priv;
	dma_addr_t next;
	int i;

	np->dirty_rx = np->cur_rx = 0;

	np->rx_buf_sz = (dev->mtu <= 1500 ? PKT_BUF_SZ : dev->mtu + 32);
	np->rx_head_desc = &np->rx_ring[0];
	next = np->rx_ring_dma;
	
	/* Init the ring entries */
	for (i = 0; i < RX_RING_SIZE; i++) {
		np->rx_ring[i].rx_status = 0;
		np->rx_ring[i].desc_length = cpu_to_le32(np->rx_buf_sz);
		next += sizeof(struct rx_desc);
		np->rx_ring[i].next_desc = cpu_to_le32(next);
		np->rx_skbuff[i] = 0;
	}
	/* Mark the last entry as wrapping the ring. */
	np->rx_ring[i-1].next_desc = cpu_to_le32(np->rx_ring_dma);

	/* Fill in the Rx buffers.  Handle allocation failure gracefully. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		struct sk_buff *skb = dev_alloc_skb(np->rx_buf_sz);
		np->rx_skbuff[i] = skb;
		if (skb == NULL)
			break;
		skb->dev = dev;                 /* Mark as being used by this device. */

		np->rx_skbuff_dma[i] =
			pci_map_single(np->pdev, skb->tail, np->rx_buf_sz,
						   PCI_DMA_FROMDEVICE);

		np->rx_ring[i].addr = cpu_to_le32(np->rx_skbuff_dma[i]);
		np->rx_ring[i].rx_status = cpu_to_le32(DescOwn);
	}
	np->dirty_rx = (unsigned int)(i - RX_RING_SIZE);
}

static void free_rbufs(struct net_device* dev)
{
	struct netdev_private *np = dev->priv;
	int i;

	/* Free all the skbuffs in the Rx queue. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		np->rx_ring[i].rx_status = 0;
		np->rx_ring[i].addr = cpu_to_le32(0xBADF00D0); /* An invalid address. */
		if (np->rx_skbuff[i]) {
			pci_unmap_single(np->pdev,
							 np->rx_skbuff_dma[i],
							 np->rx_buf_sz, PCI_DMA_FROMDEVICE);
			dev_kfree_skb(np->rx_skbuff[i]);
		}
		np->rx_skbuff[i] = 0;
	}
}

static void alloc_tbufs(struct net_device* dev)
{
	struct netdev_private *np = dev->priv;
	dma_addr_t next;
	int i;

	np->dirty_tx = np->cur_tx = 0;
	next = np->tx_ring_dma;
	for (i = 0; i < TX_RING_SIZE; i++) {
		np->tx_skbuff[i] = 0;
		np->tx_ring[i].tx_status = 0;
		np->tx_ring[i].desc_length = cpu_to_le32(0x00e08000);
		next += sizeof(struct tx_desc);
		np->tx_ring[i].next_desc = cpu_to_le32(next);
		np->tx_buf[i] = &np->tx_bufs[i * PKT_BUF_SZ];
	}
	np->tx_ring[i-1].next_desc = cpu_to_le32(np->tx_ring_dma);

}

static void free_tbufs(struct net_device* dev)
{
	struct netdev_private *np = dev->priv;
	int i;

	for (i = 0; i < TX_RING_SIZE; i++) {
		np->tx_ring[i].tx_status = 0;
		np->tx_ring[i].desc_length = cpu_to_le32(0x00e08000);
		np->tx_ring[i].addr = cpu_to_le32(0xBADF00D0); /* An invalid address. */
		if (np->tx_skbuff[i]) {
			if (np->tx_skbuff_dma[i]) {
				pci_unmap_single(np->pdev,
								 np->tx_skbuff_dma[i],
								 np->tx_skbuff[i]->len, PCI_DMA_TODEVICE);
			}
			dev_kfree_skb(np->tx_skbuff[i]);
		}
		np->tx_skbuff[i] = 0;
		np->tx_buf[i] = 0;
	}
}

static void init_registers(struct net_device *dev)
{
	struct netdev_private *np = dev->priv;
	long ioaddr = dev->base_addr;
	int i;

	for (i = 0; i < 6; i++)
		writeb(dev->dev_addr[i], ioaddr + StationAddr + i);

	/* Initialize other registers. */
	writew(0x0006, ioaddr + PCIBusConfig);	/* Tune configuration??? */
	/* Configure the FIFO thresholds. */
	writeb(0x20, ioaddr + TxConfig);	/* Initial threshold 32 bytes */
	np->tx_thresh = 0x20;
	np->rx_thresh = 0x60;			/* Written in via_rhine_set_rx_mode(). */

	if (dev->if_port == 0)
		dev->if_port = np->default_port;

	writel(np->rx_ring_dma, ioaddr + RxRingPtr);
	writel(np->tx_ring_dma, ioaddr + TxRingPtr);

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

	/* The LED outputs of various MII xcvrs should be configured.  */
	/* For NS or Mison phys, turn on bit 1 in register 0x17 */
	/* For ESI phys, turn on bit 7 in register 0x17. */
	mdio_write(dev, np->phys[0], 0x17, mdio_read(dev, np->phys[0], 0x17) |
			   (np->drv_flags & HasESIPhy) ? 0x0080 : 0x0001);
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
	struct netdev_private *np = dev->priv;
	long ioaddr = dev->base_addr;
	int boguscnt = 1024;

	if (phy_id == np->phys[0]) {
		switch (regnum) {
		case 0:							/* Is user forcing speed/duplex? */
			if (value & 0x9000)			/* Autonegotiation. */
				np->duplex_lock = 0;
			else
				np->full_duplex = (value & 0x0100) ? 1 : 0;
			break;
		case 4:
			np->advertising = value;
			break;
		}
	}

	/* Wait for a previous command to complete. */
	while ((readb(ioaddr + MIICmd) & 0x60) && --boguscnt > 0)
		;
	writeb(0x00, ioaddr + MIICmd);
	writeb(phy_id, ioaddr + MIIPhyAddr);
	writeb(regnum, ioaddr + MIIRegAddr);
	writew(value, ioaddr + MIIData);
	writeb(0x20, ioaddr + MIICmd);			/* Trigger write. */
}


static int via_rhine_open(struct net_device *dev)
{
	struct netdev_private *np = dev->priv;
	long ioaddr = dev->base_addr;
	int i;

	/* Reset the chip. */
	writew(CmdReset, ioaddr + ChipCmd);

	i = request_irq(np->pdev->irq, &via_rhine_interrupt, SA_SHIRQ, dev->name, dev);
	if (i)
		return i;

	if (debug > 1)
		printk(KERN_DEBUG "%s: via_rhine_open() irq %d.\n",
			   dev->name, np->pdev->irq);
	
	i = alloc_ring(dev);
	if (i)
		return i;
	alloc_rbufs(dev);
	alloc_tbufs(dev);
	wait_for_reset(dev, dev->name);
	init_registers(dev);
	if (debug > 2)
		printk(KERN_DEBUG "%s: Done via_rhine_open(), status %4.4x "
			   "MII status: %4.4x.\n",
			   dev->name, readw(ioaddr + ChipCmd),
			   mdio_read(dev, np->phys[0], 1));

	netif_start_queue(dev);

	/* Set the timer to check for link beat. */
	init_timer(&np->timer);
	np->timer.expires = jiffies + 2;
	np->timer.data = (unsigned long)dev;
	np->timer.function = &via_rhine_timer;				/* timer handler */
	add_timer(&np->timer);

	return 0;
}

static void via_rhine_check_duplex(struct net_device *dev)
{
	struct netdev_private *np = dev->priv;
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
	struct netdev_private *np = dev->priv;
	long ioaddr = dev->base_addr;
	int next_tick = 10*HZ;
	int mii_status;

	if (debug > 3) {
		printk(KERN_DEBUG "%s: VIA Rhine monitor tick, status %4.4x.\n",
			   dev->name, readw(ioaddr + IntrStatus));
	}

	spin_lock_irq (&np->lock);

	via_rhine_check_duplex(dev);

	/* make IFF_RUNNING follow the MII status bit "Link established" */
	mii_status = mdio_read(dev, np->phys[0], 1);
	if ( (mii_status & MIILink) != (np->mii_status & MIILink) ) {
		if (mii_status & MIILink)
			netif_carrier_on(dev);
		else
			netif_carrier_off(dev);
	}
	np->mii_status = mii_status;

	spin_unlock_irq (&np->lock);

	np->timer.expires = jiffies + next_tick;
	add_timer(&np->timer);
}


static void via_rhine_tx_timeout (struct net_device *dev)
{
	struct netdev_private *np = dev->priv;
	long ioaddr = dev->base_addr;

	printk (KERN_WARNING "%s: Transmit timed out, status %4.4x, PHY status "
		"%4.4x, resetting...\n",
		dev->name, readw (ioaddr + IntrStatus),
		mdio_read (dev, np->phys[0], 1));

	dev->if_port = 0;

	/* protect against concurrent rx interrupts */
	disable_irq(np->pdev->irq);

	spin_lock(&np->lock);

	/* Reset the chip. */
	writew(CmdReset, ioaddr + ChipCmd);

	/* clear all descriptors */
	free_tbufs(dev);
	free_rbufs(dev);
	alloc_tbufs(dev);
	alloc_rbufs(dev);

	/* Reinitialize the hardware. */
	wait_for_reset(dev, dev->name);
	init_registers(dev);
	
	spin_unlock(&np->lock);
	enable_irq(np->pdev->irq);

	dev->trans_start = jiffies;
	np->stats.tx_errors++;
	netif_wake_queue(dev);
}

static int via_rhine_start_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct netdev_private *np = dev->priv;
	unsigned entry;

	/* Caution: the write order is important here, set the field
	   with the "ownership" bits last. */

	/* Calculate the next Tx descriptor entry. */
	entry = np->cur_tx % TX_RING_SIZE;

	np->tx_skbuff[entry] = skb;

	if ((np->drv_flags & ReqTxAlign) &&
		(((long)skb->data & 3) || skb_shinfo(skb)->nr_frags != 0 || skb->ip_summed == CHECKSUM_HW)
		) {
		/* Must use alignment buffer. */
		if (skb->len > PKT_BUF_SZ) {
			/* packet too long, drop it */
			dev_kfree_skb(skb);
			np->tx_skbuff[entry] = NULL;
			np->stats.tx_dropped++;
			return 0;
		}
		skb_copy_and_csum_dev(skb, np->tx_buf[entry]);
		np->tx_skbuff_dma[entry] = 0;
		np->tx_ring[entry].addr = cpu_to_le32(np->tx_bufs_dma +
										  (np->tx_buf[entry] - np->tx_bufs));
	} else {
		np->tx_skbuff_dma[entry] =
			pci_map_single(np->pdev, skb->data, skb->len, PCI_DMA_TODEVICE);
		np->tx_ring[entry].addr = cpu_to_le32(np->tx_skbuff_dma[entry]);
	}

	np->tx_ring[entry].desc_length = 
		cpu_to_le32(0x00E08000 | (skb->len >= ETH_ZLEN ? skb->len : ETH_ZLEN));

	/* lock eth irq */
	spin_lock_irq (&np->lock);
	wmb();
	np->tx_ring[entry].tx_status = cpu_to_le32(DescOwn);
	wmb();

	np->cur_tx++;

	/* Non-x86 Todo: explicitly flush cache lines here. */

	/* Wake the potentially-idle transmit channel. */
	writew(CmdTxDemand | np->chip_cmd, dev->base_addr + ChipCmd);

	if (np->cur_tx == np->dirty_tx + TX_QUEUE_LEN)
		netif_stop_queue(dev);

	dev->trans_start = jiffies;

	spin_unlock_irq (&np->lock);

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
	struct net_device *dev = dev_instance;
	long ioaddr;
	u32 intr_status;
	int boguscnt = max_interrupt_work;

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
	struct netdev_private *np = dev->priv;
	int txstatus = 0, entry = np->dirty_tx % TX_RING_SIZE;

	spin_lock (&np->lock);

	/* find and cleanup dirty tx descriptors */
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
			np->stats.tx_bytes += np->tx_skbuff[entry]->len;
			np->stats.tx_packets++;
		}
		/* Free the original skb. */
		if (np->tx_skbuff_dma[entry]) {
			pci_unmap_single(np->pdev,
							 np->tx_skbuff_dma[entry],
							 np->tx_skbuff[entry]->len, PCI_DMA_TODEVICE);
		}
		dev_kfree_skb_irq(np->tx_skbuff[entry]);
		np->tx_skbuff[entry] = NULL;
		entry = (++np->dirty_tx) % TX_RING_SIZE;
	}
	if ((np->cur_tx - np->dirty_tx) < TX_QUEUE_LEN - 4)
		netif_wake_queue (dev);

	spin_unlock (&np->lock);
}

/* This routine is logically part of the interrupt handler, but isolated
   for clarity and better register allocation. */
static void via_rhine_rx(struct net_device *dev)
{
	struct netdev_private *np = dev->priv;
	int entry = np->cur_rx % RX_RING_SIZE;
	int boguscnt = np->dirty_rx + RX_RING_SIZE - np->cur_rx;

	if (debug > 4) {
		printk(KERN_DEBUG " In via_rhine_rx(), entry %d status %8.8x.\n",
			   entry, le32_to_cpu(np->rx_head_desc->rx_status));
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
					   dev->name, np->rx_head_desc, &np->rx_ring[entry]);
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
				if (desc_status & 0x0002) {
					/* this can also be updated outside the interrupt handler */
					spin_lock (&np->lock);
					np->stats.rx_crc_errors++;
					spin_unlock (&np->lock);
				}
			}
		} else {
			struct sk_buff *skb;
			/* Length should omit the CRC */
			int pkt_len = data_size - 4;

			/* Check if the packet is long enough to accept without copying
			   to a minimally-sized skbuff. */
			if (pkt_len < rx_copybreak &&
				(skb = dev_alloc_skb(pkt_len + 2)) != NULL) {
				skb->dev = dev;
				skb_reserve(skb, 2);	/* 16 byte align the IP header */
				pci_dma_sync_single(np->pdev, np->rx_skbuff_dma[entry],
						    np->rx_buf_sz, PCI_DMA_FROMDEVICE);

				/* *_IP_COPYSUM isn't defined anywhere and eth_copy_and_sum
				   is memcpy for all archs so this is kind of pointless right
				   now ... or? */
#if HAS_IP_COPYSUM                     /* Call copy + cksum if available. */
				eth_copy_and_sum(skb, np->rx_skbuff[entry]->tail, pkt_len, 0);
				skb_put(skb, pkt_len);
#else
				memcpy(skb_put(skb, pkt_len), np->rx_skbuff[entry]->tail,
					   pkt_len);
#endif
			} else {
				skb = np->rx_skbuff[entry];
				if (skb == NULL) {
					printk(KERN_ERR "%s: Inconsistent Rx descriptor chain.\n",
						   dev->name);
					break;
				}
				np->rx_skbuff[entry] = NULL;
				skb_put(skb, pkt_len);
				pci_unmap_single(np->pdev, np->rx_skbuff_dma[entry],
								 np->rx_buf_sz, PCI_DMA_FROMDEVICE);
			}
			skb->protocol = eth_type_trans(skb, dev);
			netif_rx(skb);
			dev->last_rx = jiffies;
			np->stats.rx_bytes += pkt_len;
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
			np->rx_skbuff_dma[entry] =
				pci_map_single(np->pdev, skb->tail, np->rx_buf_sz, 
							   PCI_DMA_FROMDEVICE);
			np->rx_ring[entry].addr = cpu_to_le32(np->rx_skbuff_dma[entry]);
		}
		np->rx_ring[entry].rx_status = cpu_to_le32(DescOwn);
	}

	/* Pre-emptively restart Rx engine. */
	writew(CmdRxDemand | np->chip_cmd, dev->base_addr + ChipCmd);
}

static void via_rhine_error(struct net_device *dev, int intr_status)
{
	struct netdev_private *np = dev->priv;
	long ioaddr = dev->base_addr;

	spin_lock (&np->lock);

	if (intr_status & (IntrMIIChange | IntrLinkChange)) {
		if (readb(ioaddr + MIIStatus) & 0x02) {
			/* Link failed, restart autonegotiation. */
			if (np->drv_flags & HasDavicomPhy)
				mdio_write(dev, np->phys[0], 0, 0x3300);
		} else
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
		clear_tally_counters(ioaddr);
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
						  IntrTxAbort | IntrTxAborted))) {
		if (debug > 1)
			printk(KERN_ERR "%s: Something Wicked happened! %4.4x.\n",
			   dev->name, intr_status);
		/* Recovery for other fault sources not known. */
		writew(CmdTxDemand | np->chip_cmd, dev->base_addr + ChipCmd);
	}

	spin_unlock (&np->lock);
}

static struct net_device_stats *via_rhine_get_stats(struct net_device *dev)
{
	struct netdev_private *np = dev->priv;
	long ioaddr = dev->base_addr;
	unsigned long flags;

	spin_lock_irqsave(&np->lock, flags);
	np->stats.rx_crc_errors	+= readw(ioaddr + RxCRCErrs);
	np->stats.rx_missed_errors	+= readw(ioaddr + RxMissed);
	clear_tally_counters(ioaddr);
	spin_unlock_irqrestore(&np->lock, flags);

	return &np->stats;
}

/* Clears the "tally counters" for CRC errors and missed frames(?).
   It has been reported that some chips need a write of 0 to clear
   these, for others the counters are set to 1 when written to and
   instead cleared when read. So we clear them both ways ... */
static inline void clear_tally_counters(const long ioaddr)
{
	writel(0, ioaddr + RxMissed);
	readw(ioaddr + RxCRCErrs);
	readw(ioaddr + RxMissed);
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
	struct netdev_private *np = dev->priv;
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
			int bit_nr = ether_crc(ETH_ALEN, mclist->dmi_addr) >> 26;

			mc_filter[bit_nr >> 5] |= cpu_to_le32(1 << (bit_nr & 31));
		}
		writel(mc_filter[0], ioaddr + MulticastFilter0);
		writel(mc_filter[1], ioaddr + MulticastFilter1);
		rx_mode = 0x0C;
	}
	writeb(np->rx_thresh | rx_mode, ioaddr + RxConfig);
}

static int mii_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct netdev_private *np = dev->priv;
	struct mii_ioctl_data *data = (struct mii_ioctl_data *)&rq->ifr_data;
	unsigned long flags;
	int retval;

	spin_lock_irqsave(&np->lock, flags);
	retval = 0;

	switch(cmd) {
	case SIOCGMIIPHY:		/* Get address of MII PHY in use. */
	case SIOCDEVPRIVATE:		/* for binary compat, remove in 2.5 */
		data->phy_id = np->phys[0] & 0x1f;
		/* Fall Through */

	case SIOCGMIIREG:		/* Read MII PHY register. */
	case SIOCDEVPRIVATE+1:		/* for binary compat, remove in 2.5 */
		data->val_out = mdio_read(dev, data->phy_id & 0x1f, data->reg_num & 0x1f);
		break;

	case SIOCSMIIREG:		/* Write MII PHY register. */
	case SIOCDEVPRIVATE+2:		/* for binary compat, remove in 2.5 */
		if (!capable(CAP_NET_ADMIN)) {
			retval = -EPERM;
			break;
		}
		mdio_write(dev, data->phy_id & 0x1f, data->reg_num & 0x1f, data->val_in);
		break;
	default:
		retval = -EOPNOTSUPP;
	}

	spin_unlock_irqrestore(&np->lock, flags);
	return retval;
}

static int via_rhine_close(struct net_device *dev)
{
	long ioaddr = dev->base_addr;
	struct netdev_private *np = dev->priv;

	del_timer_sync(&np->timer);

	spin_lock_irq(&np->lock);

	netif_stop_queue(dev);

	if (debug > 1)
		printk(KERN_DEBUG "%s: Shutting down ethercard, status was %4.4x.\n",
			   dev->name, readw(ioaddr + ChipCmd));

	/* Switch to loopback mode to avoid hardware races. */
	writeb(np->tx_thresh | 0x02, ioaddr + TxConfig);

	/* Disable interrupts by clearing the interrupt mask. */
	writew(0x0000, ioaddr + IntrEnable);

	/* Stop the chip's Tx and Rx processes. */
	writew(CmdStop, ioaddr + ChipCmd);

	spin_unlock_irq(&np->lock);

	free_irq(np->pdev->irq, dev);
	free_rbufs(dev);
	free_tbufs(dev);
	free_ring(dev);

	return 0;
}


static void __devexit via_rhine_remove_one (struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	
	unregister_netdev(dev);

	pci_release_regions(pdev);

#ifdef USE_MEM
	iounmap((char *)(dev->base_addr));
#endif

	kfree(dev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
}


static struct pci_driver via_rhine_driver = {
	name:		"via-rhine",
	id_table:	via_rhine_pci_tbl,
	probe:		via_rhine_init_one,
	remove:		via_rhine_remove_one,
};


static int __init via_rhine_init (void)
{
/* when a module, this is printed whether or not devices are found in probe */
#ifdef MODULE
	printk(version);
#endif
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
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 * End:
 */
