/* natsemi.c: A Linux PCI Ethernet driver for the NatSemi DP8381x series. */
/*
	Written/copyright 1999-2001 by Donald Becker.
	Portions copyright (c) 2001 Sun Microsystems (thockin@sun.com)

	This software may be used and distributed according to the terms of
	the GNU General Public License (GPL), incorporated herein by reference.
	Drivers based on or derived from this code fall under the GPL and must
	retain the authorship, copyright and license notice.  This file is not
	a complete program and may only be used when the entire operating
	system is licensed under the GPL.  License for under other terms may be
	available.  Contact the original author for details.

	The original author may be reached as becker@scyld.com, or at
	Scyld Computing Corporation
	410 Severn Ave., Suite 210
	Annapolis MD 21403

	Support information and updates available at
	http://www.scyld.com/network/netsemi.html


	Linux kernel modifications:

	Version 1.0.1:
		- Spinlock fixes
		- Bug fixes and better intr performance (Tjeerd)
	Version 1.0.2:
		- Now reads correct MAC address from eeprom
	Version 1.0.3:
		- Eliminate redundant priv->tx_full flag
		- Call netif_start_queue from dev->tx_timeout
		- wmb() in start_tx() to flush data
		- Update Tx locking
		- Clean up PCI enable (davej)
	Version 1.0.4:
		- Merge Donald Becker's natsemi.c version 1.07
	Version 1.0.5:
		- { fill me in }
	Version 1.0.6:
		* ethtool support (jgarzik)
		* Proper initialization of the card (which sometimes
		fails to occur and leaves the card in a non-functional
		state). (uzi)

		* Some documented register settings to optimize some
		of the 100Mbit autodetection circuitry in rev C cards. (uzi)

		* Polling of the PHY intr for stuff like link state
		change and auto- negotiation to finally work properly. (uzi)

		* One-liner removal of a duplicate declaration of
		netdev_error(). (uzi)

	Version 1.0.7: (Manfred Spraul)
		* pci dma
		* SMP locking update
		* full reset added into tx_timeout
		* correct multicast hash generation (both big and little endian)
			[copied from a natsemi driver version
			 from Myrio Corporation, Greg Smith]
		* suspend/resume

	version 1.0.8 (Tim Hockin <thockin@sun.com>)
		* ETHTOOL_* support
		* Wake on lan support (Erik Gilling)
		* MXDMA fixes for serverworks
		* EEPROM reload

	version 1.0.9 (Manfred Spraul)
		* Main change: fix lack of synchronize
		netif_close/netif_suspend against a last interrupt
		or packet.
		* do not enable superflous interrupts (e.g. the
		drivers relies on TxDone - TxIntr not needed)
		* wait that the hardware has really stopped in close
		and suspend.
		* workaround for the (at least) gcc-2.95.1 compiler
		problem. Also simplifies the code a bit.
		* disable_irq() in tx_timeout - needed to protect
		against rx interrupts.
		* stop the nic before switching into silent rx mode
		for wol (required according to docu).

	version 1.0.10:
		* use long for ee_addr (various)
		* print pointers properly (DaveM)
		* include asm/irq.h (?)
	
	version 1.0.11:
		* check and reset if PHY errors appear (Adrian Sun)
		* WoL cleanup (Tim Hockin)
		* Magic number cleanup (Tim Hockin)
		* Don't reload EEPROM on every reset (Tim Hockin)
		* Save and restore EEPROM state across reset (Tim Hockin)
		* MDIO Cleanup (Tim Hockin)
		* Reformat register offsets/bits (jgarzik)

	version 1.0.12:
		* ETHTOOL_* further support (Tim Hockin)

	version 1.0.13:
		* ETHTOOL_[GS]EEPROM support (Tim Hockin)

	TODO:
	* big endian support with CFG:BEM instead of cpu_to_le32
	* support for an external PHY
	* flow control
*/

#define DRV_NAME	"natsemi"
#define DRV_VERSION	"1.07+LK1.0.13"
#define DRV_RELDATE	"Oct 19, 2001"

/* Updated to recommendations in pci-skeleton v2.03. */

/* Automatically extracted configuration info:
probe-func: natsemi_probe
config-in: tristate 'National Semiconductor DP8381x series PCI Ethernet support' CONFIG_NATSEMI

c-help-name: National Semiconductor DP8381x series PCI Ethernet support
c-help-symbol: CONFIG_NATSEMI
c-help: This driver is for the National Semiconductor DP8381x series,
c-help: including the 8381[56] chips.
c-help: More specific information and updates are available from 
c-help: http://www.scyld.com/network/natsemi.html
*/

/* The user-configurable values.
   These may be modified when a driver module is loaded.*/

static int debug = 1; /* 1 normal messages, 0 quiet .. 7 verbose. */

/* Maximum events (Rx packets, etc.) to handle at each interrupt. */
static int max_interrupt_work = 20;
static int mtu;

/* Maximum number of multicast addresses to filter (vs. rx-all-multicast).
   This chip uses a 512 element hash table based on the Ethernet CRC.  */
static int multicast_filter_limit = 100;

/* Set the copy breakpoint for the copy-only-tiny-frames scheme.
   Setting to > 1518 effectively disables this feature. */
static int rx_copybreak;

/* Used to pass the media type, etc.
   Both 'options[]' and 'full_duplex[]' should exist for driver
   interoperability.
   The media type is usually passed in 'options[]'.
*/
#define MAX_UNITS 8		/* More are supported, limit only on options */
static int options[MAX_UNITS] = {-1, -1, -1, -1, -1, -1, -1, -1};
static int full_duplex[MAX_UNITS] = {-1, -1, -1, -1, -1, -1, -1, -1};

/* Operational parameters that are set at compile time. */

/* Keep the ring sizes a power of two for compile efficiency.
   The compiler will convert <unsigned>'%'<2^N> into a bit mask.
   Making the Tx ring too large decreases the effectiveness of channel
   bonding and packet priority.
   There are no ill effects from too-large receive rings. */
#define TX_RING_SIZE	16
#define TX_QUEUE_LEN	10 /* Limit ring entries actually used, min 4. */
#define RX_RING_SIZE	64

/* Operational parameters that usually are not changed. */
/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT  (2*HZ)

#define NATSEMI_HW_TIMEOUT	400
#define NATSEMI_TIMER_FREQ	3*HZ
#define NATSEMI_PG0_NREGS	64
#define NATSEMI_RFDR_NREGS	8
#define NATSEMI_PG1_NREGS	4
#define NATSEMI_NREGS		(NATSEMI_PG0_NREGS + NATSEMI_RFDR_NREGS + \
				 NATSEMI_PG1_NREGS)
#define NATSEMI_REGS_VER	1 /* v1 added RFDR registers */
#define NATSEMI_REGS_SIZE	(NATSEMI_NREGS * sizeof(u32))
#define NATSEMI_EEPROM_SIZE	24 /* 12 16-bit values */

#define PKT_BUF_SZ		1536 /* Size of each temporary Rx buffer. */

#if !defined(__OPTIMIZE__)
#warning  You must compile this file with the correct options!
#warning  See the last lines of the source file.
#error You must compile this driver with "-O".
#endif

#include <linux/config.h>
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
#include <linux/spinlock.h>
#include <linux/ethtool.h>
#include <linux/delay.h>
#include <linux/rtnetlink.h>
#include <linux/mii.h>
#include <asm/processor.h>	/* Processor type for cache alignment. */
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>

/* These identify the driver base version and may not be removed. */
static char version[] __devinitdata =
KERN_INFO DRV_NAME ".c:v1.07 1/9/2001  Written by Donald Becker <becker@scyld.com>\n"
KERN_INFO "  http://www.scyld.com/network/natsemi.html\n"
KERN_INFO "  (unofficial 2.4.x kernel port, version " DRV_VERSION ", " DRV_RELDATE "  Jeff Garzik, Tjeerd Mulder)\n";

MODULE_AUTHOR("Donald Becker <becker@scyld.com>");
MODULE_DESCRIPTION("National Semiconductor DP8381x series PCI Ethernet driver");
MODULE_LICENSE("GPL");

MODULE_PARM(max_interrupt_work, "i");
MODULE_PARM(mtu, "i");
MODULE_PARM(debug, "i");
MODULE_PARM(rx_copybreak, "i");
MODULE_PARM(options, "1-" __MODULE_STRING(MAX_UNITS) "i");
MODULE_PARM(full_duplex, "1-" __MODULE_STRING(MAX_UNITS) "i");
MODULE_PARM_DESC(max_interrupt_work, "DP8381x maximum events handled per interrupt");
MODULE_PARM_DESC(mtu, "DP8381x MTU (all boards)");
MODULE_PARM_DESC(debug, "DP8381x debug level (0-5)");
MODULE_PARM_DESC(rx_copybreak, "DP8381x copy breakpoint for copy-only-tiny-frames");
MODULE_PARM_DESC(options, "DP8381x: Bits 0-3: media type, bit 17: full duplex");
MODULE_PARM_DESC(full_duplex, "DP8381x full duplex setting(s) (1)");

/*
				Theory of Operation

I. Board Compatibility

This driver is designed for National Semiconductor DP83815 PCI Ethernet NIC.
It also works with other chips in in the DP83810 series.

II. Board-specific settings

This driver requires the PCI interrupt line to be valid.
It honors the EEPROM-set values. 

III. Driver operation

IIIa. Ring buffers

This driver uses two statically allocated fixed-size descriptor lists
formed into rings by a branch from the final descriptor to the beginning of
the list.  The ring sizes are set at compile time by RX/TX_RING_SIZE.
The NatSemi design uses a 'next descriptor' pointer that the driver forms
into a list. 

IIIb/c. Transmit/Receive Structure

This driver uses a zero-copy receive and transmit scheme.
The driver allocates full frame size skbuffs for the Rx ring buffers at
open() time and passes the skb->data field to the chip as receive data
buffers.  When an incoming frame is less than RX_COPYBREAK bytes long,
a fresh skbuff is allocated and the frame is copied to the new skbuff.
When the incoming frame is larger, the skbuff is passed directly up the
protocol stack.  Buffers consumed this way are replaced by newly allocated
skbuffs in a later phase of receives.

The RX_COPYBREAK value is chosen to trade-off the memory wasted by
using a full-sized skbuff for small frames vs. the copying costs of larger
frames.  New boards are typically used in generously configured machines
and the underfilled buffers have negligible impact compared to the benefit of
a single allocation size, so the default value of zero results in never
copying packets.  When copying is done, the cost is usually mitigated by using
a combined copy/checksum routine.  Copying also preloads the cache, which is
most useful with small frames.

A subtle aspect of the operation is that unaligned buffers are not permitted
by the hardware.  Thus the IP header at offset 14 in an ethernet frame isn't
longword aligned for further processing.  On copies frames are put into the
skbuff at an offset of "+2", 16-byte aligning the IP header.

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

NatSemi PCI network controllers are very uncommon.

IVb. References

http://www.scyld.com/expert/100mbps.html
http://www.scyld.com/expert/NWay.html
Datasheet is available from:
http://www.national.com/pf/DP/DP83815.html

IVc. Errata

None characterised.
*/



enum pcistuff {
	PCI_USES_IO = 0x01,
	PCI_USES_MEM = 0x02,
	PCI_USES_MASTER = 0x04,
	PCI_ADDR0 = 0x08,
	PCI_ADDR1 = 0x10,
};

/* MMIO operations required */
#define PCI_IOTYPE (PCI_USES_MASTER | PCI_USES_MEM | PCI_ADDR1)


/* array of board data directly indexed by pci_tbl[x].driver_data */
static struct {
	const char *name;
	unsigned long flags;
} natsemi_pci_info[] __devinitdata = {
	{ "NatSemi DP8381[56]", PCI_IOTYPE },
};

static struct pci_device_id natsemi_pci_tbl[] __devinitdata = {
	{ PCI_VENDOR_ID_NS, PCI_DEVICE_ID_NS_83815, PCI_ANY_ID, PCI_ANY_ID, },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, natsemi_pci_tbl);

/* Offsets to the device registers.
   Unlike software-only systems, device drivers interact with complex hardware.
   It's not useful to define symbolic names for every register bit in the
   device.
*/
enum register_offsets {
	ChipCmd			= 0x00,
	ChipConfig		= 0x04,
	EECtrl			= 0x08,
	PCIBusCfg		= 0x0C,
	IntrStatus		= 0x10,
	IntrMask		= 0x14,
	IntrEnable		= 0x18,
	TxRingPtr		= 0x20,
	TxConfig		= 0x24,
	RxRingPtr		= 0x30,
	RxConfig		= 0x34,
	ClkRun			= 0x3C,
	WOLCmd			= 0x40,
	PauseCmd		= 0x44,
	RxFilterAddr		= 0x48,
	RxFilterData		= 0x4C,
	BootRomAddr		= 0x50,
	BootRomData		= 0x54,
	SiliconRev		= 0x58,
	StatsCtrl		= 0x5C,
	StatsData		= 0x60,
	RxPktErrs		= 0x60,
	RxMissed		= 0x68,
	RxCRCErrs		= 0x64,
	BasicControl		= 0x80,
	BasicStatus		= 0x84,
	AnegAdv			= 0x90,
	AnegPeer		= 0x94,
	PhyStatus		= 0xC0,
	MIntrCtrl		= 0xC4,
	MIntrStatus		= 0xC8,
	PhyCtrl			= 0xE4,

	/* These are from the spec, around page 78... on a separate table.
	 * The meaning of these registers depend on the value of PGSEL. */
	PGSEL			= 0xCC,
	PMDCSR			= 0xE4,
	TSTDAT			= 0xFC,
	DSPCFG			= 0xF4,
	SDCFG			= 0xF8
};
/* the values for the 'magic' registers above (PGSEL=1) */
#define PMDCSR_VAL	0x189C
#define TSTDAT_VAL	0x0
#define DSPCFG_VAL	0x5040
#define SDCFG_VAL	0x008c

/* misc PCI space registers */
enum pci_register_offsets {
	PCIPM			= 0x44,
};

enum ChipCmd_bits {
	ChipReset		= 0x100,
	RxReset			= 0x20,
	TxReset			= 0x10,
	RxOff			= 0x08,
	RxOn			= 0x04,
	TxOff			= 0x02,
	TxOn			= 0x01,
};

enum ChipConfig_bits {
	CfgPhyDis		= 0x200,
	CfgPhyRst		= 0x400,
	CfgAnegEnable		= 0x2000,
	CfgAneg100		= 0x4000,
	CfgAnegFull		= 0x8000,
	CfgAnegDone		= 0x8000000,
	CfgFullDuplex		= 0x20000000,
	CfgSpeed100		= 0x40000000,
	CfgLink			= 0x80000000,
};

enum EECtrl_bits {
	EE_ShiftClk		= 0x04,
	EE_DataIn		= 0x01,
	EE_ChipSelect		= 0x08,
	EE_DataOut		= 0x02,
};

enum PCIBusCfg_bits {
	EepromReload		= 0x4,
};

/* Bits in the interrupt status/mask registers. */
enum IntrStatus_bits {
	IntrRxDone		= 0x0001,
	IntrRxIntr		= 0x0002,
	IntrRxErr		= 0x0004,
	IntrRxEarly		= 0x0008,
	IntrRxIdle		= 0x0010,
	IntrRxOverrun		= 0x0020,
	IntrTxDone		= 0x0040,
	IntrTxIntr		= 0x0080,
	IntrTxErr		= 0x0100,
	IntrTxIdle		= 0x0200,
	IntrTxUnderrun		= 0x0400,
	StatsMax		= 0x0800,
	SWInt			= 0x1000,
	WOLPkt			= 0x2000,
	LinkChange		= 0x4000,
	IntrHighBits		= 0x8000,
	RxStatusFIFOOver	= 0x10000,
	IntrPCIErr		= 0xf00000,
	RxResetDone		= 0x1000000,
	TxResetDone		= 0x2000000,
	IntrAbnormalSummary	= 0xCD20,
};

/*
 * Default Interrupts:
 * Rx OK, Rx Packet Error, Rx Overrun, 
 * Tx OK, Tx Packet Error, Tx Underrun, 
 * MIB Service, Phy Interrupt, High Bits,
 * Rx Status FIFO overrun,
 * Received Target Abort, Received Master Abort, 
 * Signalled System Error, Received Parity Error
 */
#define DEFAULT_INTR 0x00f1cd65

enum TxConfig_bits {
	TxDrthMask		= 0x3f,
	TxFlthMask		= 0x3f00,
	TxMxdmaMask		= 0x700000,
	TxMxdma_512		= 0x0,
	TxMxdma_4		= 0x100000,
	TxMxdma_8		= 0x200000,
	TxMxdma_16		= 0x300000,
	TxMxdma_32		= 0x400000,
	TxMxdma_64		= 0x500000,
	TxMxdma_128		= 0x600000,
	TxMxdma_256		= 0x700000,
	TxCollRetry		= 0x800000,
	TxAutoPad		= 0x10000000,
	TxMacLoop		= 0x20000000,
	TxHeartIgn		= 0x40000000,
	TxCarrierIgn		= 0x80000000
};

enum RxConfig_bits {
	RxDrthMask		= 0x3e,
	RxMxdmaMask		= 0x700000,
	RxMxdma_512		= 0x0,
	RxMxdma_4		= 0x100000,
	RxMxdma_8		= 0x200000,
	RxMxdma_16		= 0x300000,
	RxMxdma_32		= 0x400000,
	RxMxdma_64		= 0x500000,
	RxMxdma_128		= 0x600000,
	RxMxdma_256		= 0x700000,
	RxAcceptLong		= 0x8000000,
	RxAcceptTx		= 0x10000000,
	RxAcceptRunt		= 0x40000000,
	RxAcceptErr		= 0x80000000
};

enum ClkRun_bits {
	PMEEnable		= 0x100,
	PMEStatus		= 0x8000,
};

enum WolCmd_bits {
	WakePhy			= 0x1,
	WakeUnicast		= 0x2,
	WakeMulticast		= 0x4,
	WakeBroadcast		= 0x8,
	WakeArp			= 0x10,
	WakePMatch0		= 0x20,
	WakePMatch1		= 0x40,
	WakePMatch2		= 0x80,
	WakePMatch3		= 0x100,
	WakeMagic		= 0x200,
	WakeMagicSecure		= 0x400,
	SecureHack		= 0x100000,
	WokePhy			= 0x400000,
	WokeUnicast		= 0x800000,
	WokeMulticast		= 0x1000000,
	WokeBroadcast		= 0x2000000,
	WokeArp			= 0x4000000,
	WokePMatch0		= 0x8000000,
	WokePMatch1		= 0x10000000,
	WokePMatch2		= 0x20000000,
	WokePMatch3		= 0x40000000,
	WokeMagic		= 0x80000000,
	WakeOptsSummary		= 0x7ff
};

enum RxFilterAddr_bits {
	RFCRAddressMask		= 0x3ff,
	AcceptMulticast		= 0x00200000,
	AcceptMyPhys		= 0x08000000,
	AcceptAllPhys		= 0x10000000,
	AcceptAllMulticast	= 0x20000000,
	AcceptBroadcast		= 0x40000000,
	RxFilterEnable		= 0x80000000
};

enum StatsCtrl_bits {
	StatsWarn		= 0x1,
	StatsFreeze		= 0x2,
	StatsClear		= 0x4,
	StatsStrobe		= 0x8,
};

enum MIntrCtrl_bits {
	MICRIntEn		= 0x2,
};

enum PhyCtrl_bits {
	PhyAddrMask		= 0xf,
};

#define SRR_REV_C	0x0302
#define SRR_REV_D	0x0403

/* The Rx and Tx buffer descriptors. */
/* Note that using only 32 bit fields simplifies conversion to big-endian
   architectures. */
struct netdev_desc {
	u32 next_desc;
	s32 cmd_status;
	u32 addr;
	u32 software_use;
};

/* Bits in network_desc.status */
enum desc_status_bits {
	DescOwn=0x80000000, DescMore=0x40000000, DescIntr=0x20000000,
	DescNoCRC=0x10000000, DescPktOK=0x08000000, 
	DescSizeMask=0xfff,

	DescTxAbort=0x04000000, DescTxFIFO=0x02000000, 
	DescTxCarrier=0x01000000, DescTxDefer=0x00800000,
	DescTxExcDefer=0x00400000, DescTxOOWCol=0x00200000,
	DescTxExcColl=0x00100000, DescTxCollCount=0x000f0000,
	
	DescRxAbort=0x04000000, DescRxOver=0x02000000,
	DescRxDest=0x01800000, DescRxLong=0x00400000,
	DescRxRunt=0x00200000, DescRxInvalid=0x00100000,
	DescRxCRC=0x00080000, DescRxAlign=0x00040000,
	DescRxLoop=0x00020000, DesRxColl=0x00010000,
};

struct netdev_private {
	/* Descriptor rings first for alignment. */
	dma_addr_t ring_dma;
	struct netdev_desc* rx_ring;
	struct netdev_desc* tx_ring;
	/* The addresses of receive-in-place skbuffs. */
	struct sk_buff* rx_skbuff[RX_RING_SIZE];
	dma_addr_t rx_dma[RX_RING_SIZE];
	/* The saved address of a sent-in-place packet/buffer, for later free(). */
	struct sk_buff* tx_skbuff[TX_RING_SIZE];
	dma_addr_t tx_dma[TX_RING_SIZE];
	struct net_device_stats stats;
	struct timer_list timer;	/* Media monitoring timer. */
	/* Frequently used values: keep some adjacent for cache effect. */
	struct pci_dev *pci_dev;
	struct netdev_desc *rx_head_desc;
	unsigned int cur_rx, dirty_rx;		/* Producer/consumer ring indices */
	unsigned int cur_tx, dirty_tx;
	unsigned int rx_buf_sz;				/* Based on MTU+slack. */
	/* These values are keep track of the transceiver/media in use. */
	unsigned int full_duplex;
	/* Rx filter. */
	u32 cur_rx_mode;
	u32 rx_filter[16];
	/* FIFO and PCI burst thresholds. */
	u32 tx_config, rx_config;
	/* original contents of ClkRun register */
	u32 SavedClkRun;
	/* silicon revision */
	u32 srr;
	/* MII transceiver section. */
	u16 advertising; /* NWay media advertisement */
	unsigned int iosize;
	spinlock_t lock;
};

static int eeprom_read(long ioaddr, int location);
static int mdio_read(struct net_device *dev, int phy_id, int reg);
static void mdio_write(struct net_device *dev, int phy_id, int reg, u16 data);
static void natsemi_reset(struct net_device *dev);
static void natsemi_reload_eeprom(struct net_device *dev);
static void natsemi_stop_rxtx(struct net_device *dev);
static int netdev_open(struct net_device *dev);
static void check_link(struct net_device *dev);
static void netdev_timer(unsigned long data);
static void tx_timeout(struct net_device *dev);
static int alloc_ring(struct net_device *dev);
static void init_ring(struct net_device *dev);
static void drain_ring(struct net_device *dev);
static void free_ring(struct net_device *dev);
static void init_registers(struct net_device *dev);
static int start_tx(struct sk_buff *skb, struct net_device *dev);
static void intr_handler(int irq, void *dev_instance, struct pt_regs *regs);
static void netdev_error(struct net_device *dev, int intr_status);
static void netdev_rx(struct net_device *dev);
static void netdev_tx_done(struct net_device *dev);
static void __set_rx_mode(struct net_device *dev);
static void set_rx_mode(struct net_device *dev);
static void __get_stats(struct net_device *dev);
static struct net_device_stats *get_stats(struct net_device *dev);
static int netdev_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
static int netdev_set_wol(struct net_device *dev, u32 newval);
static int netdev_get_wol(struct net_device *dev, u32 *supported, u32 *cur);
static int netdev_set_sopass(struct net_device *dev, u8 *newval);
static int netdev_get_sopass(struct net_device *dev, u8 *data);
static int netdev_get_ecmd(struct net_device *dev, struct ethtool_cmd *ecmd);
static int netdev_set_ecmd(struct net_device *dev, struct ethtool_cmd *ecmd);
static void enable_wol_mode(struct net_device *dev, int enable_intr);
static int netdev_close(struct net_device *dev);
static int netdev_get_regs(struct net_device *dev, u8 *buf);
static int netdev_get_eeprom(struct net_device *dev, u8 *buf);


static int __devinit natsemi_probe1 (struct pci_dev *pdev,
				     const struct pci_device_id *ent)
{
	struct net_device *dev;
	struct netdev_private *np;
	int i, option, irq, chip_idx = ent->driver_data;
	static int find_cnt = -1;
	unsigned long ioaddr, iosize;
	const int pcibar = 1; /* PCI base address register */
	int prev_eedata;
	u32 tmp;

/* when built into the kernel, we only print version if device is found */
#ifndef MODULE
	static int printed_version;
	if (!printed_version++)
		printk(version);
#endif

	i = pci_enable_device(pdev);
	if (i) return i;

	/* natsemi has a non-standard PM control register
	 * in PCI config space.  Some boards apparently need
	 * to be brought to D0 in this manner.
	 */
	pci_read_config_dword(pdev, PCIPM, &tmp);
	if (tmp & PCI_PM_CTRL_STATE_MASK) {
		/* D0 state, disable PME assertion */
		u32 newtmp = tmp & ~PCI_PM_CTRL_STATE_MASK;
		pci_write_config_dword(pdev, PCIPM, newtmp);
	}

	find_cnt++;
	ioaddr = pci_resource_start(pdev, pcibar);
	iosize = pci_resource_len(pdev, pcibar);
	irq = pdev->irq;

	if (natsemi_pci_info[chip_idx].flags & PCI_USES_MASTER)
		pci_set_master(pdev);

	dev = alloc_etherdev(sizeof (struct netdev_private));
	if (!dev)
		return -ENOMEM;
	SET_MODULE_OWNER(dev);

	i = pci_request_regions(pdev, dev->name);
	if (i) {
		kfree(dev);
		return i;
	}

	{
		void *mmio = ioremap (ioaddr, iosize);
		if (!mmio) {
			pci_release_regions(pdev);
			kfree(dev);
			return -ENOMEM;
		}
		ioaddr = (unsigned long) mmio;
	}

	/* Work around the dropped serial bit. */
	prev_eedata = eeprom_read(ioaddr, 6);
	for (i = 0; i < 3; i++) {
		int eedata = eeprom_read(ioaddr, i + 7);
		dev->dev_addr[i*2] = (eedata << 1) + (prev_eedata >> 15);
		dev->dev_addr[i*2+1] = eedata >> 7;
		prev_eedata = eedata;
	}

	dev->base_addr = ioaddr;
	dev->irq = irq;

	np = dev->priv;

	np->pci_dev = pdev;
	pci_set_drvdata(pdev, dev);
	np->iosize = iosize;
	spin_lock_init(&np->lock);

	/* Reset the chip to erase previous misconfiguration. */
	natsemi_reload_eeprom(dev);
	natsemi_reset(dev);

	option = find_cnt < MAX_UNITS ? options[find_cnt] : 0;
	if (dev->mem_start)
		option = dev->mem_start;

	/* The lower four bits are the media type. */
	if (option > 0) {
		if (option & 0x200)
			np->full_duplex = 1;
		if (option & 15)
			printk(KERN_INFO "%s: ignoring user supplied media type %d",
				dev->name, option & 15);
	}
	if (find_cnt < MAX_UNITS  &&  full_duplex[find_cnt] > 0)
		np->full_duplex = 1;

	/* The chip-specific entries in the device structure. */
	dev->open = &netdev_open;
	dev->hard_start_xmit = &start_tx;
	dev->stop = &netdev_close;
	dev->get_stats = &get_stats;
	dev->set_multicast_list = &set_rx_mode;
	dev->do_ioctl = &netdev_ioctl;
	dev->tx_timeout = &tx_timeout;
	dev->watchdog_timeo = TX_TIMEOUT;

	if (mtu)
		dev->mtu = mtu;

	i = register_netdev(dev);
	if (i) {
		pci_release_regions(pdev);
		unregister_netdev(dev);
		kfree(dev);
		pci_set_drvdata(pdev, NULL);
		return i;
	}
	netif_carrier_off(dev);

	printk(KERN_INFO "%s: %s at 0x%lx, ",
		   dev->name, natsemi_pci_info[chip_idx].name, ioaddr);
	for (i = 0; i < ETH_ALEN-1; i++)
			printk("%2.2x:", dev->dev_addr[i]);
	printk("%2.2x, IRQ %d.\n", dev->dev_addr[i], irq);

	np->advertising = mdio_read(dev, 1, MII_ADVERTISE);
	if ((readl(ioaddr + ChipConfig) & 0xe000) != 0xe000) {
		u32 chip_config = readl(ioaddr + ChipConfig);
		printk(KERN_INFO "%s: Transceiver default autonegotiation %s "
			   "10%s %s duplex.\n",
			   dev->name,
			   chip_config & CfgAnegEnable ? "enabled, advertise" : "disabled, force",
			   chip_config & CfgAneg100 ? "0" : "",
			   chip_config & CfgAnegFull ? "full" : "half");
	}
	printk(KERN_INFO "%s: Transceiver status 0x%4.4x advertising %4.4x.\n",
		   dev->name, mdio_read(dev, 1, MII_BMSR), 
		   np->advertising);

	/* save the silicon revision for later querying */
	np->srr = readl(ioaddr + SiliconRev);

	return 0;
}


/* Read the EEPROM and MII Management Data I/O (MDIO) interfaces.
   The EEPROM code is for the common 93c06/46 EEPROMs with 6 bit addresses. */

/* Delay between EEPROM clock transitions.
   No extra delay is needed with 33Mhz PCI, but future 66Mhz access may need
   a delay.  Note that pre-2.0.34 kernels had a cache-alignment bug that
   made udelay() unreliable.
   The old method of using an ISA access as a delay, __SLOW_DOWN_IO__, is
   depricated.
*/
#define eeprom_delay(ee_addr)	readl(ee_addr)

#define EE_Write0 (EE_ChipSelect)
#define EE_Write1 (EE_ChipSelect | EE_DataIn)

/* The EEPROM commands include the alway-set leading bit. */
enum EEPROM_Cmds {
	EE_WriteCmd=(5 << 6), EE_ReadCmd=(6 << 6), EE_EraseCmd=(7 << 6),
};

static int eeprom_read(long addr, int location)
{
	int i;
	int retval = 0;
	long ee_addr = addr + EECtrl;
	int read_cmd = location | EE_ReadCmd;
	writel(EE_Write0, ee_addr);

	/* Shift the read command bits out. */
	for (i = 10; i >= 0; i--) {
		short dataval = (read_cmd & (1 << i)) ? EE_Write1 : EE_Write0;
		writel(dataval, ee_addr);
		eeprom_delay(ee_addr);
		writel(dataval | EE_ShiftClk, ee_addr);
		eeprom_delay(ee_addr);
	}
	writel(EE_ChipSelect, ee_addr);
	eeprom_delay(ee_addr);

	for (i = 0; i < 16; i++) {
		writel(EE_ChipSelect | EE_ShiftClk, ee_addr);
		eeprom_delay(ee_addr);
		retval |= (readl(ee_addr) & EE_DataOut) ? 1 << i : 0;
		writel(EE_ChipSelect, ee_addr);
		eeprom_delay(ee_addr);
	}

	/* Terminate the EEPROM access. */
	writel(EE_Write0, ee_addr);
	writel(0, ee_addr);
	return retval;
}

/*  MII transceiver control section.
	The 83815 series has an internal transceiver, and we present the
	management registers as if they were MII connected. */

static int mdio_read(struct net_device *dev, int phy_id, int reg)
{
	if (phy_id == 1 && reg < 32)
		return readl(dev->base_addr+BasicControl+(reg<<2))&0xffff;
	else
		return 0xffff;
}

static void mdio_write(struct net_device *dev, int phy_id, int reg, u16 data)
{
	struct netdev_private *np = dev->priv;
	if (phy_id == 1 && reg < 32) {
		writew(data, dev->base_addr+BasicControl+(reg<<2));
		switch (reg) {
			case MII_ADVERTISE: np->advertising = data; break;
		}
	}
}

/* CFG bits [13:16] [18:23] */
#define CFG_RESET_SAVE 0xfde000
/* WCSR bits [0:4] [9:10] */
#define WCSR_RESET_SAVE 0x61f
/* RFCR bits [20] [22] [27:31] */
#define RFCR_RESET_SAVE 0xf8500000;

static void natsemi_reset(struct net_device *dev)
{
	int i;
	u32 cfg;
	u32 wcsr;
	u32 rfcr;
	u16 pmatch[3];
	u16 sopass[3];

	/* 
	 * Resetting the chip causes some registers to be lost.
	 * Natsemi suggests NOT reloading the EEPROM while live, so instead
	 * we save the state that would have been loaded from EEPROM
	 * on a normal power-up (see the spec EEPROM map).  This assumes 
	 * whoever calls this will follow up with init_registers() eventually.
	 */

	/* CFG */
	cfg = readl(dev->base_addr + ChipConfig) & CFG_RESET_SAVE;
	/* WCSR */
	wcsr = readl(dev->base_addr + WOLCmd) & WCSR_RESET_SAVE;
	/* RFCR */
	rfcr = readl(dev->base_addr + RxFilterAddr) & RFCR_RESET_SAVE;
	/* PMATCH */
	for (i = 0; i < 3; i++) {
		writel(i*2, dev->base_addr + RxFilterAddr);
		pmatch[i] = readw(dev->base_addr + RxFilterData);
	}
	/* SOPAS */
	for (i = 0; i < 3; i++) {
		writel(0xa+(i*2), dev->base_addr + RxFilterAddr);
		sopass[i] = readw(dev->base_addr + RxFilterData);
	}

	/* now whack the chip */
	writel(ChipReset, dev->base_addr + ChipCmd);
	for (i=0;i<NATSEMI_HW_TIMEOUT;i++) {
		if (!(readl(dev->base_addr + ChipCmd) & ChipReset))
			break;
		udelay(5);
	}
	if (i==NATSEMI_HW_TIMEOUT && debug) {
		printk(KERN_INFO "%s: reset did not complete in %d usec.\n",
		   dev->name, i*5);
	} else if (debug > 2) {
		printk(KERN_DEBUG "%s: reset completed in %d usec.\n",
		   dev->name, i*5);
	}

	/* restore CFG */
	cfg |= readl(dev->base_addr + ChipConfig) & ~CFG_RESET_SAVE;
	writel(cfg, dev->base_addr + ChipConfig);
	/* restore WCSR */
	wcsr |= readl(dev->base_addr + WOLCmd) & ~WCSR_RESET_SAVE;
	writel(wcsr, dev->base_addr + WOLCmd);
	/* read RFCR */
	rfcr |= readl(dev->base_addr + RxFilterAddr) & ~RFCR_RESET_SAVE;
	/* restore PMATCH */ 
	for (i = 0; i < 3; i++) {
		writel(i*2, dev->base_addr + RxFilterAddr);
		writew(pmatch[i], dev->base_addr + RxFilterData);
	}
	for (i = 0; i < 3; i++) {
		writel(0xa+(i*2), dev->base_addr + RxFilterAddr);
		writew(sopass[i], dev->base_addr + RxFilterData);
	}
	/* restore RFCR */
	writel(rfcr, dev->base_addr + RxFilterAddr);
	
}

static void natsemi_reload_eeprom(struct net_device *dev)
{
	int i;

	writel(EepromReload, dev->base_addr + PCIBusCfg);
	for (i=0;i<NATSEMI_HW_TIMEOUT;i++) {
		if (!(readl(dev->base_addr + PCIBusCfg) & EepromReload))
			break;
		udelay(5);
	}
	if (i==NATSEMI_HW_TIMEOUT && debug) {
		printk(KERN_INFO "%s: EEPROM did not reload in %d usec.\n",
		   dev->name, i*5);
	} else if (debug > 2) {
		printk(KERN_DEBUG "%s: EEPROM reloaded in %d usec.\n",
		   dev->name, i*5);
	}
}

static void natsemi_stop_rxtx(struct net_device *dev)
{
	long ioaddr = dev->base_addr;
	int i;

	writel(RxOff | TxOff, ioaddr + ChipCmd);
	for(i=0;i< NATSEMI_HW_TIMEOUT;i++) {
		if ((readl(ioaddr + ChipCmd) & (TxOn|RxOn)) == 0)
			break;
		udelay(5);
	}
	if (i==NATSEMI_HW_TIMEOUT && debug) {
		printk(KERN_INFO "%s: Tx/Rx process did not stop in %d usec.\n",
				dev->name, i*5);
	} else if (debug > 2) {
		printk(KERN_DEBUG "%s: Tx/Rx process stopped in %d usec.\n",
				dev->name, i*5);
	}
}

static int netdev_open(struct net_device *dev)
{
	struct netdev_private *np = dev->priv;
	long ioaddr = dev->base_addr;
	int i;

	/* Reset the chip, just in case. */
	natsemi_reset(dev);

	i = request_irq(dev->irq, &intr_handler, SA_SHIRQ, dev->name, dev);
	if (i) return i;

	if (debug > 1)
		printk(KERN_DEBUG "%s: netdev_open() irq %d.\n",
			   dev->name, dev->irq);
	i = alloc_ring(dev);
	if (i < 0) {
		free_irq(dev->irq, dev);
		return i;
	}
	init_ring(dev);
	spin_lock_irq(&np->lock);
	init_registers(dev);
	spin_unlock_irq(&np->lock);

	netif_start_queue(dev);

	if (debug > 2)
		printk(KERN_DEBUG "%s: Done netdev_open(), status: %x.\n",
			   dev->name, (int)readl(ioaddr + ChipCmd));

	/* Set the timer to check for link beat. */
	init_timer(&np->timer);
	np->timer.expires = jiffies + NATSEMI_TIMER_FREQ;
	np->timer.data = (unsigned long)dev;
	np->timer.function = &netdev_timer; /* timer handler */
	add_timer(&np->timer);

	return 0;
}

static void check_link(struct net_device *dev)
{
	struct netdev_private *np = dev->priv;
	long ioaddr = dev->base_addr;
	int duplex;
	int chipcfg = readl(ioaddr + ChipConfig);

	if(!(chipcfg & CfgLink)) {
		if (netif_carrier_ok(dev)) {
			if (debug)
				printk(KERN_INFO "%s: no link. Disabling watchdog.\n",
					dev->name);
			netif_carrier_off(dev);
		}
		return;
	}
	if (!netif_carrier_ok(dev)) {
		if (debug)
			printk(KERN_INFO "%s: link is back. Enabling watchdog.\n",
					dev->name);
		netif_carrier_on(dev);
	}

	duplex = np->full_duplex || (chipcfg & CfgFullDuplex ? 1 : 0);

	/* if duplex is set then bit 28 must be set, too */
	if (duplex ^ !!(np->rx_config & RxAcceptTx)) {
		if (debug)
			printk(KERN_INFO "%s: Setting %s-duplex based on negotiated link"
				   " capability.\n", dev->name,
				   duplex ? "full" : "half");
		if (duplex) {
			np->rx_config |= RxAcceptTx;
			np->tx_config |= TxCarrierIgn | TxHeartIgn;
		} else {
			np->rx_config &= ~RxAcceptTx;
			np->tx_config &= ~(TxCarrierIgn | TxHeartIgn);
		}
		writel(np->tx_config, ioaddr + TxConfig);
		writel(np->rx_config, ioaddr + RxConfig);
	}
}

static void init_registers(struct net_device *dev)
{
	struct netdev_private *np = dev->priv;
	long ioaddr = dev->base_addr;
	int i;

	/* save the silicon revision for later */
	if (debug > 4)
		printk(KERN_DEBUG "%s: found silicon revision %xh.\n",
				dev->name, np->srr);

	for (i=0;i<NATSEMI_HW_TIMEOUT;i++) {
		if (readl(dev->base_addr + ChipConfig) & CfgAnegDone)
			break;
		udelay(10);
	}
	if (i==NATSEMI_HW_TIMEOUT && debug) {
		printk(KERN_INFO 
			"%s: autonegotiation did not complete in %d usec.\n",
			dev->name, i*10);
	}

	/* On page 78 of the spec, they recommend some settings for "optimum
	   performance" to be done in sequence.  These settings optimize some
	   of the 100Mbit autodetection circuitry.  They say we only want to 
	   do this for rev C of the chip, but engineers at NSC (Bradley 
	   Kennedy) recommends always setting them.  If you don't, you get 
	   errors on some autonegotiations that make the device unusable.
	*/
	writew(1, ioaddr + PGSEL);
	writew(PMDCSR_VAL, ioaddr + PMDCSR);
	writew(TSTDAT_VAL, ioaddr + TSTDAT);
	writew(DSPCFG_VAL, ioaddr + DSPCFG);
	writew(SDCFG_VAL, ioaddr + SDCFG);
	writew(0, ioaddr + PGSEL);

	/* Enable PHY Specific event based interrupts.  Link state change
	   and Auto-Negotiation Completion are among the affected.
	   Read the intr status to clear it (needed for wake events).
	*/
	readw(ioaddr + MIntrStatus);
	writew(MICRIntEn, ioaddr + MIntrCtrl);

	/* clear any interrupts that are pending, such as wake events */
	readl(ioaddr + IntrStatus);

	writel(np->ring_dma, ioaddr + RxRingPtr);
	writel(np->ring_dma + RX_RING_SIZE * sizeof(struct netdev_desc), 
		ioaddr + TxRingPtr);

	/* Initialize other registers.
	 * Configure the PCI bus bursts and FIFO thresholds.
	 * Configure for standard, in-spec Ethernet.
	 * Start with half-duplex. check_link will update
	 * to the correct settings. 
	 */

	/* DRTH: 2: start tx if 64 bytes are in the fifo
	 * FLTH: 0x10: refill with next packet if 512 bytes are free
	 * MXDMA: 0: up to 256 byte bursts.
	 * 	MXDMA must be <= FLTH
	 * ECRETRY=1
	 * ATP=1
	 */
	np->tx_config = TxAutoPad | TxCollRetry | TxMxdma_256 | (0x1002);
	writel(np->tx_config, ioaddr + TxConfig);

	/* DRTH 0x10: start copying to memory if 128 bytes are in the fifo
	 * MXDMA 0: up to 256 byte bursts
	 */
	np->rx_config = RxMxdma_256 | 0x20;
	writel(np->rx_config, ioaddr + RxConfig);

	/* Disable PME:
	 * The PME bit is initialized from the EEPROM contents.
	 * PCI cards probably have PME disabled, but motherboard
	 * implementations may have PME set to enable WakeOnLan. 
	 * With PME set the chip will scan incoming packets but
	 * nothing will be written to memory. */
	np->SavedClkRun = readl(ioaddr + ClkRun);
	writel(np->SavedClkRun & ~PMEEnable, ioaddr + ClkRun);
	if (np->SavedClkRun & PMEStatus) {
		printk(KERN_NOTICE "%s: Wake-up event %8.8x\n", 
			dev->name, readl(ioaddr + WOLCmd));
	}

	check_link(dev);
	__set_rx_mode(dev);

	/* Enable interrupts by setting the interrupt mask. */
	writel(DEFAULT_INTR, ioaddr + IntrMask);
	writel(1, ioaddr + IntrEnable);

	writel(RxOn | TxOn, ioaddr + ChipCmd);
	writel(StatsClear, ioaddr + StatsCtrl); /* Clear Stats */
}

/* 
 * The frequency on this has been increased because of a nasty little problem.
 * It seems that a reference set for this chip went out with incorrect info,
 * and there exist boards that aren't quite right.  An unexpected voltage drop
 * can cause the PHY to get itself in a weird state (basically reset..).
 * NOTE: this only seems to affect revC chips.
 */
static void netdev_timer(unsigned long data)
{
	struct net_device *dev = (struct net_device *)data;
	struct netdev_private *np = dev->priv;
	int next_tick = 5*HZ;
	long ioaddr = dev->base_addr;
	u16 dspcfg;

	if (debug > 3) {
		/* DO NOT read the IntrStatus register, 
		 * a read clears any pending interrupts.
		 */
		printk(KERN_DEBUG "%s: Media selection timer tick.\n",
			   dev->name);
	}

	/* check for a nasty random phy-reset - use dspcfg as a flag */
	writew(1, ioaddr+PGSEL);
	dspcfg = readw(ioaddr+DSPCFG);
	writew(0, ioaddr+PGSEL);
	if (dspcfg != DSPCFG_VAL) {
		if (!netif_queue_stopped(dev)) {
			printk(KERN_INFO 
				"%s: possible phy reset: re-initializing\n",
				dev->name);
			disable_irq(dev->irq);
			spin_lock_irq(&np->lock);
			init_registers(dev);
			spin_unlock_irq(&np->lock);
			enable_irq(dev->irq);
		} else {
			/* hurry back */
			next_tick = HZ;
		}
	} else {
		/* init_registers() calls check_link() for the above case */
		spin_lock_irq(&np->lock);
		check_link(dev);
		spin_unlock_irq(&np->lock);
	}
	mod_timer(&np->timer, jiffies + next_tick);
}

static void dump_ring(struct net_device *dev)
{
	struct netdev_private *np = dev->priv;

	if (debug > 2) {
		int i;
		printk(KERN_DEBUG "  Tx ring at %p:\n", np->tx_ring);
		for (i = 0; i < TX_RING_SIZE; i++) {
			printk(KERN_DEBUG " #%d desc. %8.8x %8.8x %8.8x.\n",
				   i, np->tx_ring[i].next_desc,
				   np->tx_ring[i].cmd_status, 
				   np->tx_ring[i].addr);
		}
		printk(KERN_DEBUG "  Rx ring %p:\n", np->rx_ring);
		for (i = 0; i < RX_RING_SIZE; i++) {
			printk(KERN_DEBUG " #%d desc. %8.8x %8.8x %8.8x.\n",
				   i, np->rx_ring[i].next_desc,
				   np->rx_ring[i].cmd_status, 
				   np->rx_ring[i].addr);
		}
	}
}

static void tx_timeout(struct net_device *dev)
{
	struct netdev_private *np = dev->priv;
	long ioaddr = dev->base_addr;

	disable_irq(dev->irq);
	spin_lock_irq(&np->lock);
	if (netif_device_present(dev)) {
		printk(KERN_WARNING "%s: Transmit timed out, status %8.8x,"
			" resetting...\n", 
			dev->name, readl(ioaddr + IntrStatus));
		dump_ring(dev);

		natsemi_reset(dev);
		drain_ring(dev);
		init_ring(dev);
		init_registers(dev);
	} else {
		printk(KERN_WARNING 
			"%s: tx_timeout while in suspended state?\n",
		   	dev->name);
	}
	spin_unlock_irq(&np->lock);
	enable_irq(dev->irq);

	dev->trans_start = jiffies;
	np->stats.tx_errors++;
	netif_wake_queue(dev);
}

static int alloc_ring(struct net_device *dev)
{
	struct netdev_private *np = dev->priv;
	np->rx_ring = pci_alloc_consistent(np->pci_dev,
				sizeof(struct netdev_desc) * (RX_RING_SIZE+TX_RING_SIZE),
				&np->ring_dma);
	if (!np->rx_ring)
		return -ENOMEM;
	np->tx_ring = &np->rx_ring[RX_RING_SIZE];
	return 0;
}

/* Initialize the Rx and Tx rings, along with various 'dev' bits. */
static void init_ring(struct net_device *dev)
{
	struct netdev_private *np = dev->priv;
	int i;

	np->cur_rx = np->cur_tx = 0;
	np->dirty_rx = np->dirty_tx = 0;

	np->rx_buf_sz = (dev->mtu <= 1500 ? PKT_BUF_SZ : dev->mtu + 32);
	np->rx_head_desc = &np->rx_ring[0];

	/* Please be carefull before changing this loop - at least gcc-2.95.1
	 * miscompiles it otherwise.
	 */
	/* Initialize all Rx descriptors. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		np->rx_ring[i].next_desc = cpu_to_le32(np->ring_dma
				+sizeof(struct netdev_desc)
				*((i+1)%RX_RING_SIZE));
		np->rx_ring[i].cmd_status = cpu_to_le32(DescOwn);
		np->rx_skbuff[i] = NULL;
	}

	/* Fill in the Rx buffers.  Handle allocation failure gracefully. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		struct sk_buff *skb = dev_alloc_skb(np->rx_buf_sz);
		np->rx_skbuff[i] = skb;
		if (skb == NULL)
			break;
		skb->dev = dev;			/* Mark as being used by this device. */
		np->rx_dma[i] = pci_map_single(np->pci_dev,
						skb->data, skb->len, PCI_DMA_FROMDEVICE);
		np->rx_ring[i].addr = cpu_to_le32(np->rx_dma[i]);
		np->rx_ring[i].cmd_status = cpu_to_le32(np->rx_buf_sz);
	}
	np->dirty_rx = (unsigned int)(i - RX_RING_SIZE);

	for (i = 0; i < TX_RING_SIZE; i++) {
		np->tx_skbuff[i] = NULL;
		np->tx_ring[i].next_desc = cpu_to_le32(np->ring_dma
					+sizeof(struct netdev_desc)
					 *((i+1)%TX_RING_SIZE+RX_RING_SIZE));
		np->tx_ring[i].cmd_status = 0;
	}
	dump_ring(dev);
}

static void drain_ring(struct net_device *dev)
{
	struct netdev_private *np = dev->priv;
	int i;

	/* Free all the skbuffs in the Rx queue. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		np->rx_ring[i].cmd_status = 0;
		np->rx_ring[i].addr = 0xBADF00D0; /* An invalid address. */
		if (np->rx_skbuff[i]) {
			pci_unmap_single(np->pci_dev,
						np->rx_dma[i],
						np->rx_skbuff[i]->len,
						PCI_DMA_FROMDEVICE);
			dev_kfree_skb(np->rx_skbuff[i]);
		}
		np->rx_skbuff[i] = NULL;
	}
	for (i = 0; i < TX_RING_SIZE; i++) {
		if (np->tx_skbuff[i]) {
			pci_unmap_single(np->pci_dev,
						np->rx_dma[i],
						np->rx_skbuff[i]->len,
						PCI_DMA_TODEVICE);
			dev_kfree_skb(np->tx_skbuff[i]);
		}
		np->tx_skbuff[i] = NULL;
	}
}

static void free_ring(struct net_device *dev)
{
	struct netdev_private *np = dev->priv;
	pci_free_consistent(np->pci_dev,
				sizeof(struct netdev_desc) * (RX_RING_SIZE+TX_RING_SIZE),
				np->rx_ring, np->ring_dma);
}

static int start_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct netdev_private *np = dev->priv;
	unsigned entry;

	/* Note: Ordering is important here, set the field with the
	   "ownership" bit last, and only then increment cur_tx. */

	/* Calculate the next Tx descriptor entry. */
	entry = np->cur_tx % TX_RING_SIZE;

	np->tx_skbuff[entry] = skb;
	np->tx_dma[entry] = pci_map_single(np->pci_dev,
				skb->data,skb->len, PCI_DMA_TODEVICE);

	np->tx_ring[entry].addr = cpu_to_le32(np->tx_dma[entry]);

	spin_lock_irq(&np->lock);
	
	if (netif_device_present(dev)) {
		np->tx_ring[entry].cmd_status = cpu_to_le32(DescOwn | skb->len);
		/* StrongARM: Explicitly cache flush np->tx_ring and 
		 * skb->data,skb->len. */
		wmb();
		np->cur_tx++;
		if (np->cur_tx - np->dirty_tx >= TX_QUEUE_LEN - 1) {
			netdev_tx_done(dev);
			if (np->cur_tx - np->dirty_tx >= TX_QUEUE_LEN - 1)
				netif_stop_queue(dev);
		}
		/* Wake the potentially-idle transmit channel. */
		writel(TxOn, dev->base_addr + ChipCmd);
	} else {
		dev_kfree_skb_irq(skb);
		np->stats.tx_dropped++;
	}
	spin_unlock_irq(&np->lock);

	dev->trans_start = jiffies;

	if (debug > 4) {
		printk(KERN_DEBUG "%s: Transmit frame #%d queued in slot %d.\n",
			   dev->name, np->cur_tx, entry);
	}
	return 0;
}

static void netdev_tx_done(struct net_device *dev)
{
	struct netdev_private *np = dev->priv;

	for (; np->cur_tx - np->dirty_tx > 0; np->dirty_tx++) {
		int entry = np->dirty_tx % TX_RING_SIZE;
		if (np->tx_ring[entry].cmd_status & cpu_to_le32(DescOwn)) {
			if (debug > 4)
				printk(KERN_DEBUG "%s: tx frame #%d is busy.\n",
						dev->name, np->dirty_tx);
			break;
		}
		if (debug > 4)
			printk(KERN_DEBUG "%s: tx frame #%d finished with status %8.8xh.\n",
					dev->name, np->dirty_tx,
					le32_to_cpu(np->tx_ring[entry].cmd_status));
		if (np->tx_ring[entry].cmd_status & cpu_to_le32(DescPktOK)) {
			np->stats.tx_packets++;
			np->stats.tx_bytes += np->tx_skbuff[entry]->len;
		} else { /* Various Tx errors */
			int tx_status = le32_to_cpu(np->tx_ring[entry].cmd_status);
			if (tx_status & (DescTxAbort|DescTxExcColl)) 
				np->stats.tx_aborted_errors++;
			if (tx_status & DescTxFIFO) 
				np->stats.tx_fifo_errors++;
			if (tx_status & DescTxCarrier) 
				np->stats.tx_carrier_errors++;
			if (tx_status & DescTxOOWCol) 
				np->stats.tx_window_errors++;
			np->stats.tx_errors++;
		}
		pci_unmap_single(np->pci_dev,np->tx_dma[entry],
					np->tx_skbuff[entry]->len,
					PCI_DMA_TODEVICE);
		/* Free the original skb. */
		dev_kfree_skb_irq(np->tx_skbuff[entry]);
		np->tx_skbuff[entry] = NULL;
	}
	if (netif_queue_stopped(dev)
		&& np->cur_tx - np->dirty_tx < TX_QUEUE_LEN - 4) {
		/* The ring is no longer full, wake queue. */
		netif_wake_queue(dev);
	}
}

/* The interrupt handler does all of the Rx thread work and cleans up
   after the Tx thread. */
static void intr_handler(int irq, void *dev_instance, struct pt_regs *rgs)
{
	struct net_device *dev = dev_instance;
	struct netdev_private *np;
	long ioaddr;
	int boguscnt = max_interrupt_work;

	ioaddr = dev->base_addr;
	np = dev->priv;
	
	if (!netif_device_present(dev))
		return;
	do {
		/* Reading automatically acknowledges all int sources. */
		u32 intr_status = readl(ioaddr + IntrStatus);

		if (debug > 4)
			printk(KERN_DEBUG "%s: Interrupt, status %4.4x.\n",
				   dev->name, intr_status);

		if (intr_status == 0)
			break;

		if (intr_status & (IntrRxDone | IntrRxIntr))
			netdev_rx(dev);

		if (intr_status & (IntrTxDone | IntrTxIntr | IntrTxIdle | IntrTxErr) ) {
			spin_lock(&np->lock);
			netdev_tx_done(dev);
			spin_unlock(&np->lock);
		}

		/* Abnormal error summary/uncommon events handlers. */
		if (intr_status & IntrAbnormalSummary)
			netdev_error(dev, intr_status);

		if (--boguscnt < 0) {
			printk(KERN_WARNING "%s: Too much work at interrupt, "
				   "status=0x%4.4x.\n",
				   dev->name, intr_status);
			break;
		}
	} while (1);

	if (debug > 4)
		printk(KERN_DEBUG "%s: exiting interrupt.\n",
			   dev->name);
}

/* This routine is logically part of the interrupt handler, but separated
   for clarity and better register allocation. */
static void netdev_rx(struct net_device *dev)
{
	struct netdev_private *np = dev->priv;
	int entry = np->cur_rx % RX_RING_SIZE;
	int boguscnt = np->dirty_rx + RX_RING_SIZE - np->cur_rx;
	s32 desc_status = le32_to_cpu(np->rx_head_desc->cmd_status);

	/* If the driver owns the next entry it's a new packet. Send it up. */
	while (desc_status < 0) {        /* e.g. & DescOwn */
		if (debug > 4)
			printk(KERN_DEBUG "  In netdev_rx() entry %d status was %8.8x.\n",
				   entry, desc_status);
		if (--boguscnt < 0)
			break;
		if ((desc_status & (DescMore|DescPktOK|DescRxLong)) != DescPktOK) {
			if (desc_status & DescMore) {
				printk(KERN_WARNING "%s: Oversized(?) Ethernet frame spanned "
					   "multiple buffers, entry %#x status %x.\n",
					   dev->name, np->cur_rx, desc_status);
				np->stats.rx_length_errors++;
			} else {
				/* There was a error. */
				if (debug > 2)
					printk(KERN_DEBUG "  netdev_rx() Rx error was %8.8x.\n",
						   desc_status);
				np->stats.rx_errors++;
				if (desc_status & (DescRxAbort|DescRxOver)) 
					np->stats.rx_over_errors++;
				if (desc_status & (DescRxLong|DescRxRunt)) 
					np->stats.rx_length_errors++;
				if (desc_status & (DescRxInvalid|DescRxAlign)) 
					np->stats.rx_frame_errors++;
				if (desc_status & DescRxCRC) 
					np->stats.rx_crc_errors++;
			}
		} else {
			struct sk_buff *skb;
			/* Omit CRC size. */
			int pkt_len = (desc_status & DescSizeMask) - 4;
			/* Check if the packet is long enough to accept without copying
			   to a minimally-sized skbuff. */
			if (pkt_len < rx_copybreak
				&& (skb = dev_alloc_skb(pkt_len + 2)) != NULL) {
				skb->dev = dev;
				skb_reserve(skb, 2);	/* 16 byte align the IP header */
				pci_dma_sync_single(np->pci_dev, np->rx_dma[entry],
							np->rx_skbuff[entry]->len,
							PCI_DMA_FROMDEVICE);
#if HAS_IP_COPYSUM
				eth_copy_and_sum(skb, np->rx_skbuff[entry]->tail, pkt_len, 0);
				skb_put(skb, pkt_len);
#else
				memcpy(skb_put(skb, pkt_len), np->rx_skbuff[entry]->tail,
					   pkt_len);
#endif
			} else {
				pci_unmap_single(np->pci_dev, np->rx_dma[entry],
							np->rx_skbuff[entry]->len,
							PCI_DMA_FROMDEVICE);
				skb_put(skb = np->rx_skbuff[entry], pkt_len);
				np->rx_skbuff[entry] = NULL;
			}
			skb->protocol = eth_type_trans(skb, dev);
			/* W/ hardware checksum: skb->ip_summed = CHECKSUM_UNNECESSARY; */
			netif_rx(skb);
			dev->last_rx = jiffies;
			np->stats.rx_packets++;
			np->stats.rx_bytes += pkt_len;
		}
		entry = (++np->cur_rx) % RX_RING_SIZE;
		np->rx_head_desc = &np->rx_ring[entry];
		desc_status = le32_to_cpu(np->rx_head_desc->cmd_status);
	}

	/* Refill the Rx ring buffers. */
	for (; np->cur_rx - np->dirty_rx > 0; np->dirty_rx++) {
		struct sk_buff *skb;
		entry = np->dirty_rx % RX_RING_SIZE;
		if (np->rx_skbuff[entry] == NULL) {
			skb = dev_alloc_skb(np->rx_buf_sz);
			np->rx_skbuff[entry] = skb;
			if (skb == NULL)
				break;				/* Better luck next round. */
			skb->dev = dev;			/* Mark as being used by this device. */
			np->rx_dma[entry] = pci_map_single(np->pci_dev,
							skb->data, skb->len, PCI_DMA_FROMDEVICE);
			np->rx_ring[entry].addr = cpu_to_le32(np->rx_dma[entry]);
		}
		np->rx_ring[entry].cmd_status =
			cpu_to_le32(np->rx_buf_sz);
	}

	/* Restart Rx engine if stopped. */
	writel(RxOn, dev->base_addr + ChipCmd);
}

static void netdev_error(struct net_device *dev, int intr_status)
{
	struct netdev_private *np = dev->priv;
	long ioaddr = dev->base_addr;

	spin_lock(&np->lock);
	if (intr_status & LinkChange) {
		printk(KERN_NOTICE 
			"%s: Link changed: Autonegotiation advertising"
			" %4.4x  partner %4.4x.\n", dev->name,
			(int)mdio_read(dev, 1, MII_ADVERTISE), 
			(int)mdio_read(dev, 1, MII_LPA));
		/* read MII int status to clear the flag */
		readw(ioaddr + MIntrStatus);
		check_link(dev);
	}
	if (intr_status & StatsMax) {
		__get_stats(dev);
	}
	if (intr_status & IntrTxUnderrun) {
		if ((np->tx_config & TxDrthMask) < 62)
			np->tx_config += 2;
		if (debug > 2)
			printk(KERN_NOTICE "%s: increasing Tx theshold, new tx cfg %8.8xh.\n",
					dev->name, np->tx_config);
		writel(np->tx_config, ioaddr + TxConfig);
	}
	if (intr_status & WOLPkt) {
		int wol_status = readl(ioaddr + WOLCmd);
		printk(KERN_NOTICE "%s: Link wake-up event %8.8x\n",
			   dev->name, wol_status);
	}
	if (intr_status & RxStatusFIFOOver) {
		if (debug >= 2) {
			printk(KERN_NOTICE "%s: Rx status FIFO overrun\n", 
				dev->name);
		}
		np->stats.rx_fifo_errors++;
	}
	/* Hmmmmm, it's not clear how to recover from PCI faults. */
	if (intr_status & IntrPCIErr) {
		if (debug) {
			printk(KERN_NOTICE "%s: PCI error %08x\n", dev->name,
				intr_status & IntrPCIErr);
		}
		np->stats.tx_fifo_errors++;
		np->stats.rx_fifo_errors++;
	}
	spin_unlock(&np->lock);
}

static void __get_stats(struct net_device *dev)
{
	long ioaddr = dev->base_addr;
	struct netdev_private *np = dev->priv;

	/* The chip only need report frame silently dropped. */
	np->stats.rx_crc_errors	+= readl(ioaddr + RxCRCErrs);
	np->stats.rx_missed_errors += readl(ioaddr + RxMissed);
}

static struct net_device_stats *get_stats(struct net_device *dev)
{
	struct netdev_private *np = dev->priv;

	/* The chip only need report frame silently dropped. */
	spin_lock_irq(&np->lock);
 	if (netif_running(dev) && netif_device_present(dev))
 		__get_stats(dev);
	spin_unlock_irq(&np->lock);

	return &np->stats;
}

/* The little-endian AUTODIN II ethernet CRC calculations.
   A big-endian version is also available.
   This is slow but compact code.  Do not use this routine for bulk data,
   use a table-based routine instead.
   This is common code and should be moved to net/core/crc.c.
   Chips may use the upper or lower CRC bits, and may reverse and/or invert
   them.  Select the endian-ness that results in minimal calculations.
*/
#if 0
static unsigned const ethernet_polynomial_le = 0xedb88320U;
static inline unsigned ether_crc_le(int length, unsigned char *data)
{
	unsigned int crc = 0xffffffff;	/* Initial value. */
	while(--length >= 0) {
		unsigned char current_octet = *data++;
		int bit;
		for (bit = 8; --bit >= 0; current_octet >>= 1) {
			if ((crc ^ current_octet) & 1) {
				crc >>= 1;
				crc ^= ethernet_polynomial_le;
			} else
				crc >>= 1;
		}
	}
	return crc;
}
#else
#define DP_POLYNOMIAL			0x04C11DB7
/* dp83815_crc - computer CRC for hash table entries */
static unsigned ether_crc_le(int length, unsigned char *data)
{
    u32 crc;
    u8 cur_byte;
    u8 msb;
    u8 byte, bit;

    crc = ~0;
    for (byte=0; byte<length; byte++) {
        cur_byte = *data++;
        for (bit=0; bit<8; bit++) {
            msb = crc >> 31;
            crc <<= 1;
            if (msb ^ (cur_byte & 1)) {
                crc ^= DP_POLYNOMIAL;
                crc |= 1;
            }
            cur_byte >>= 1;
        }
    }
    crc >>= 23;

    return (crc);
}
#endif

void set_bit_le(int offset, unsigned char * data)
{
	data[offset >> 3] |= (1 << (offset & 0x07));
}
#define HASH_TABLE	0x200
static void __set_rx_mode(struct net_device *dev)
{
	long ioaddr = dev->base_addr;
	struct netdev_private *np = dev->priv;
	u8 mc_filter[64];			/* Multicast hash filter */
	u32 rx_mode;

	if (dev->flags & IFF_PROMISC) {			/* Set promiscuous. */
		/* Unconditionally log net taps. */
		printk(KERN_NOTICE "%s: Promiscuous mode enabled.\n", dev->name);
		rx_mode = RxFilterEnable | AcceptBroadcast 
			| AcceptAllMulticast | AcceptAllPhys | AcceptMyPhys;
	} else if ((dev->mc_count > multicast_filter_limit)
			   ||  (dev->flags & IFF_ALLMULTI)) {
		rx_mode = RxFilterEnable | AcceptBroadcast 
			| AcceptAllMulticast | AcceptMyPhys;
	} else {
		struct dev_mc_list *mclist;
		int i;
		memset(mc_filter, 0, sizeof(mc_filter));
		for (i = 0, mclist = dev->mc_list; mclist && i < dev->mc_count;
			 i++, mclist = mclist->next) {
			set_bit_le(ether_crc_le(ETH_ALEN, mclist->dmi_addr) & 0x1ff,
					mc_filter);
		}
		rx_mode = RxFilterEnable | AcceptBroadcast 
			| AcceptMulticast | AcceptMyPhys;
		for (i = 0; i < 64; i += 2) {
			writew(HASH_TABLE + i, ioaddr + RxFilterAddr);
			writew((mc_filter[i+1]<<8) + mc_filter[i], 
				ioaddr + RxFilterData);
		}
	}
	writel(rx_mode, ioaddr + RxFilterAddr);
	np->cur_rx_mode = rx_mode;
}

static void set_rx_mode(struct net_device *dev)
{
	struct netdev_private *np = dev->priv;
	spin_lock_irq(&np->lock);
	if (netif_device_present(dev))
		__set_rx_mode(dev);
	spin_unlock_irq(&np->lock);
}

static int netdev_ethtool_ioctl(struct net_device *dev, void *useraddr)
{
	struct netdev_private *np = dev->priv;
	u32 cmd;
	
	if (get_user(cmd, (u32 *)useraddr))
		return -EFAULT;

        switch (cmd) {
	/* get driver info */
        case ETHTOOL_GDRVINFO: {
		struct ethtool_drvinfo info = {ETHTOOL_GDRVINFO};
		strncpy(info.driver, DRV_NAME, ETHTOOL_BUSINFO_LEN);
		strncpy(info.version, DRV_VERSION, ETHTOOL_BUSINFO_LEN);
		info.fw_version[0] = '\0';
		strncpy(info.bus_info, np->pci_dev->slot_name, 
			ETHTOOL_BUSINFO_LEN);
		info.eedump_len = NATSEMI_EEPROM_SIZE;
		info.regdump_len = NATSEMI_REGS_SIZE;
		if (copy_to_user(useraddr, &info, sizeof(info)))
			return -EFAULT;
		return 0;
	}
	/* get settings */
	case ETHTOOL_GSET: {
		struct ethtool_cmd ecmd = { ETHTOOL_GSET };
		spin_lock_irq(&np->lock);
		netdev_get_ecmd(dev, &ecmd);
		spin_unlock_irq(&np->lock);
		if (copy_to_user(useraddr, &ecmd, sizeof(ecmd)))
			return -EFAULT;
		return 0;
	}
	/* set settings */
	case ETHTOOL_SSET: {
		struct ethtool_cmd ecmd;
		int r;
		if (copy_from_user(&ecmd, useraddr, sizeof(ecmd)))
			return -EFAULT;
		spin_lock_irq(&np->lock);
		r = netdev_set_ecmd(dev, &ecmd);
		spin_unlock_irq(&np->lock);
		return r;
	}
	/* get wake-on-lan */
	case ETHTOOL_GWOL: {
		struct ethtool_wolinfo wol = {ETHTOOL_GWOL};
		spin_lock_irq(&np->lock);
		netdev_get_wol(dev, &wol.supported, &wol.wolopts);
		netdev_get_sopass(dev, wol.sopass);
		spin_unlock_irq(&np->lock);
		if (copy_to_user(useraddr, &wol, sizeof(wol)))
			return -EFAULT;
		return 0;
	}
	/* set wake-on-lan */
	case ETHTOOL_SWOL: {
		struct ethtool_wolinfo wol;
		int r;
		if (copy_from_user(&wol, useraddr, sizeof(wol)))
			return -EFAULT;
		spin_lock_irq(&np->lock);
		netdev_set_wol(dev, wol.wolopts);
		r = netdev_set_sopass(dev, wol.sopass);
		spin_unlock_irq(&np->lock);
		return r;
	}
	/* get registers */
	case ETHTOOL_GREGS: {
		struct ethtool_regs regs;
		u8 regbuf[NATSEMI_REGS_SIZE];
		int r;

		if (copy_from_user(&regs, useraddr, sizeof(regs)))
			return -EFAULT;
		
		if (regs.len > NATSEMI_REGS_SIZE) {
			regs.len = NATSEMI_REGS_SIZE;
		}
		regs.version = NATSEMI_REGS_VER;
		if (copy_to_user(useraddr, &regs, sizeof(regs)))
			return -EFAULT;

		useraddr += offsetof(struct ethtool_regs, data);

		spin_lock_irq(&np->lock);
		r = netdev_get_regs(dev, regbuf);
		spin_unlock_irq(&np->lock);

		if (r)
			return r;
		if (copy_to_user(useraddr, regbuf, regs.len))
			return -EFAULT;
		return 0;
	}
	/* get message-level */
	case ETHTOOL_GMSGLVL: {
		struct ethtool_value edata = {ETHTOOL_GMSGLVL};
		edata.data = debug;
		if (copy_to_user(useraddr, &edata, sizeof(edata)))
			return -EFAULT;
		return 0;
	}
	/* set message-level */
	case ETHTOOL_SMSGLVL: {
		struct ethtool_value edata;
		if (copy_from_user(&edata, useraddr, sizeof(edata)))
			return -EFAULT;
		debug = edata.data;
		return 0;
	}
	/* restart autonegotiation */
	case ETHTOOL_NWAY_RST: {
		int tmp;
		int r = -EINVAL;
		/* if autoneg is off, it's an error */
		tmp = mdio_read(dev, 1, MII_BMCR);
		if (tmp & BMCR_ANENABLE) {
			tmp |= (BMCR_ANRESTART);
			mdio_write(dev, 1, MII_BMCR, tmp);
			r = 0;
		}
		return r;
	}
	/* get link status */
	case ETHTOOL_GLINK: {
		struct ethtool_value edata = {ETHTOOL_GLINK};
		edata.data = (mdio_read(dev, 1, MII_BMSR)&BMSR_LSTATUS) ? 1:0;
		if (copy_to_user(useraddr, &edata, sizeof(edata)))
			return -EFAULT;
		return 0;
	}
	/* get EEPROM */
	case ETHTOOL_GEEPROM: {
		struct ethtool_eeprom eeprom;
		u8 eebuf[NATSEMI_EEPROM_SIZE];
		int r;

		if (copy_from_user(&eeprom, useraddr, sizeof(eeprom)))
			return -EFAULT;
		
		if ((eeprom.offset+eeprom.len) > NATSEMI_EEPROM_SIZE) {
			eeprom.len = NATSEMI_EEPROM_SIZE-eeprom.offset;
		}
		eeprom.magic = PCI_VENDOR_ID_NS | (PCI_DEVICE_ID_NS_83815<<16);
		if (copy_to_user(useraddr, &eeprom, sizeof(eeprom)))
			return -EFAULT;

		useraddr += offsetof(struct ethtool_eeprom, data);

		spin_lock_irq(&np->lock);
		r = netdev_get_eeprom(dev, eebuf);
		spin_unlock_irq(&np->lock);

		if (r)
			return r;
		if (copy_to_user(useraddr, eebuf+eeprom.offset, eeprom.len))
			return -EFAULT;
		return 0;
	}

        }
	
	return -EOPNOTSUPP;
}

static int netdev_set_wol(struct net_device *dev, u32 newval)
{
	struct netdev_private *np = dev->priv;
	u32 data = readl(dev->base_addr + WOLCmd) & ~WakeOptsSummary;

	/* translate to bitmasks this chip understands */
	if (newval & WAKE_PHY)
		data |= WakePhy;
	if (newval & WAKE_UCAST)
		data |= WakeUnicast;
	if (newval & WAKE_MCAST)
		data |= WakeMulticast;
	if (newval & WAKE_BCAST)
		data |= WakeBroadcast;
	if (newval & WAKE_ARP)
		data |= WakeArp;
	if (newval & WAKE_MAGIC)
		data |= WakeMagic;
	if (np->srr >= SRR_REV_D) {
		if (newval & WAKE_MAGICSECURE) {
			data |= WakeMagicSecure;
		}
	}

	writel(data, dev->base_addr + WOLCmd);

	return 0;
}

static int netdev_get_wol(struct net_device *dev, u32 *supported, u32 *cur)
{
	struct netdev_private *np = dev->priv;
	u32 regval = readl(dev->base_addr + WOLCmd);

	*supported = (WAKE_PHY | WAKE_UCAST | WAKE_MCAST | WAKE_BCAST 
			| WAKE_ARP | WAKE_MAGIC);
	
	if (np->srr >= SRR_REV_D) {
		/* SOPASS works on revD and higher */
		*supported |= WAKE_MAGICSECURE;
	}
	*cur = 0;

	/* translate from chip bitmasks */
	if (regval & WakePhy)
		*cur |= WAKE_PHY;
	if (regval & WakeUnicast)
		*cur |= WAKE_UCAST;
	if (regval & WakeMulticast)
		*cur |= WAKE_MCAST;
	if (regval & WakeBroadcast)
		*cur |= WAKE_BCAST;
	if (regval & WakeArp)
		*cur |= WAKE_ARP;
	if (regval & WakeMagic)
		*cur |= WAKE_MAGIC;
	if (regval & WakeMagicSecure) {
		/* this can be on in revC, but it's broken */
		*cur |= WAKE_MAGICSECURE;
	}

	return 0;
}

static int netdev_set_sopass(struct net_device *dev, u8 *newval)
{
	struct netdev_private *np = dev->priv;
	u16 *sval = (u16 *)newval;
	u32 addr;
	
	if (np->srr < SRR_REV_D) {
		return 0;
	}

	/* enable writing to these registers by disabling the RX filter */
	addr = readl(dev->base_addr + RxFilterAddr) & ~RFCRAddressMask;
	addr &= ~RxFilterEnable;
	writel(addr, dev->base_addr + RxFilterAddr);

	/* write the three words to (undocumented) RFCR vals 0xa, 0xc, 0xe */
	writel(addr | 0xa, dev->base_addr + RxFilterAddr);
	writew(sval[0], dev->base_addr + RxFilterData);

	writel(addr | 0xc, dev->base_addr + RxFilterAddr);
	writew(sval[1], dev->base_addr + RxFilterData);
	
	writel(addr | 0xe, dev->base_addr + RxFilterAddr);
	writew(sval[2], dev->base_addr + RxFilterData);
	
	/* re-enable the RX filter */
	writel(addr | RxFilterEnable, dev->base_addr + RxFilterAddr);

	return 0;
}

static int netdev_get_sopass(struct net_device *dev, u8 *data)
{
	struct netdev_private *np = dev->priv;
	u16 *sval = (u16 *)data;
	u32 addr;

	if (np->srr < SRR_REV_D) {
		sval[0] = sval[1] = sval[2] = 0;
		return 0;
	}

	/* read the three words from (undocumented) RFCR vals 0xa, 0xc, 0xe */
	addr = readl(dev->base_addr + RxFilterAddr) & ~RFCRAddressMask;

	writel(addr | 0xa, dev->base_addr + RxFilterAddr);
	sval[0] = readw(dev->base_addr + RxFilterData);

	writel(addr | 0xc, dev->base_addr + RxFilterAddr);
	sval[1] = readw(dev->base_addr + RxFilterData);
	
	writel(addr | 0xe, dev->base_addr + RxFilterAddr);
	sval[2] = readw(dev->base_addr + RxFilterData);
	
	writel(addr, dev->base_addr + RxFilterAddr);

	return 0;
}

static int netdev_get_ecmd(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	u32 tmp;

	ecmd->supported = 
		(SUPPORTED_10baseT_Half | SUPPORTED_10baseT_Full |
		SUPPORTED_100baseT_Half | SUPPORTED_100baseT_Full |
		SUPPORTED_Autoneg | SUPPORTED_TP);
	
	/* only supports twisted-pair */
	ecmd->port = PORT_TP;

	/* only supports internal transceiver */
	ecmd->transceiver = XCVR_INTERNAL;

	/* this isn't fully supported at higher layers */
	ecmd->phy_address = readw(dev->base_addr + PhyCtrl) & PhyAddrMask;

	ecmd->advertising = ADVERTISED_TP;
	tmp = mdio_read(dev, 1, MII_ADVERTISE);
	if (tmp & ADVERTISE_10HALF)
		ecmd->advertising |= ADVERTISED_10baseT_Half;
	if (tmp & ADVERTISE_10FULL)
		ecmd->advertising |= ADVERTISED_10baseT_Full;
	if (tmp & ADVERTISE_100HALF)
		ecmd->advertising |= ADVERTISED_100baseT_Half;
	if (tmp & ADVERTISE_100FULL)
		ecmd->advertising |= ADVERTISED_100baseT_Full;

	tmp = readl(dev->base_addr + ChipConfig);
	if (tmp & CfgAnegEnable) {
		ecmd->advertising |= ADVERTISED_Autoneg;
		ecmd->autoneg = AUTONEG_ENABLE;
	} else {
		ecmd->autoneg = AUTONEG_DISABLE;
	}

	if (tmp & CfgSpeed100) {
		ecmd->speed = SPEED_100;
	} else {
		ecmd->speed = SPEED_10;
	}

	if (tmp & CfgFullDuplex) {
		ecmd->duplex = DUPLEX_FULL;
	} else {
		ecmd->duplex = DUPLEX_HALF;
	}

	/* ignore maxtxpkt, maxrxpkt for now */

	return 0;
}

static int netdev_set_ecmd(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	struct netdev_private *np = dev->priv;
	u32 tmp;

	if (ecmd->speed != SPEED_10 && ecmd->speed != SPEED_100)
		return -EINVAL;
	if (ecmd->duplex != DUPLEX_HALF && ecmd->duplex != DUPLEX_FULL)
		return -EINVAL;
	if (ecmd->port != PORT_TP)
		return -EINVAL;
	if (ecmd->transceiver != XCVR_INTERNAL)
		return -EINVAL;
	if (ecmd->autoneg != AUTONEG_DISABLE && ecmd->autoneg != AUTONEG_ENABLE)
		return -EINVAL;
	/* ignore phy_address, maxtxpkt, maxrxpkt for now */
	
	/* WHEW! now lets bang some bits */
	
	if (ecmd->autoneg == AUTONEG_ENABLE) {
		/* advertise only what has been requested */
		tmp = readl(dev->base_addr + ChipConfig);
		tmp &= ~(CfgAneg100 | CfgAnegFull);
		tmp |= CfgAnegEnable;
		if (ecmd->advertising & ADVERTISED_100baseT_Half 
		 || ecmd->advertising & ADVERTISED_100baseT_Full) {
			tmp |= CfgAneg100;
		}
		if (ecmd->advertising & ADVERTISED_10baseT_Full 
		 || ecmd->advertising & ADVERTISED_100baseT_Full) {
			tmp |= CfgAnegFull;
		}
		writel(tmp, dev->base_addr + ChipConfig);
		/* turn on autonegotiation, and force a renegotiate */
		tmp = mdio_read(dev, 1, MII_BMCR);
		tmp |= (BMCR_ANENABLE | BMCR_ANRESTART);
		mdio_write(dev, 1, MII_BMCR, tmp);
		np->advertising = mdio_read(dev, 1, MII_ADVERTISE);
	} else {
		/* turn off auto negotiation, set speed and duplexity */
		tmp = mdio_read(dev, 1, MII_BMCR);
		tmp &= ~(BMCR_ANENABLE | BMCR_SPEED100 | BMCR_FULLDPLX);
		if (ecmd->speed == SPEED_100) {
			tmp |= BMCR_SPEED100;
		}
		if (ecmd->duplex == DUPLEX_FULL) {
			tmp |= BMCR_FULLDPLX;
		} else {
			np->full_duplex = 0;
		}
		mdio_write(dev, 1, MII_BMCR, tmp);
	}
	return 0;
}

static int netdev_get_regs(struct net_device *dev, u8 *buf)
{
	int i;
	int j;
	u32 rfcr;
	u32 *rbuf = (u32 *)buf;
	
	/* read all of page 0 of registers */
	for (i = 0; i < NATSEMI_PG0_NREGS; i++) {
		rbuf[i] = readl(dev->base_addr + i*4);
	}

	/* read only the 'magic' registers from page 1 */
	writew(1, dev->base_addr + PGSEL);
	rbuf[i++] = readw(dev->base_addr + PMDCSR);
	rbuf[i++] = readw(dev->base_addr + TSTDAT);
	rbuf[i++] = readw(dev->base_addr + DSPCFG);
	rbuf[i++] = readw(dev->base_addr + SDCFG);
	writew(0, dev->base_addr + PGSEL);

	/* read RFCR indexed registers */
	rfcr = readl(dev->base_addr + RxFilterAddr);
	for (j = 0; j < NATSEMI_RFDR_NREGS; j++) {
		writel(j*2, dev->base_addr + RxFilterAddr);
		rbuf[i++] = readw(dev->base_addr + RxFilterData);
	}
	writel(rfcr, dev->base_addr + RxFilterAddr);

	/* the interrupt status is clear-on-read - see if we missed any */
	if (rbuf[4] & rbuf[5]) {
		printk(KERN_WARNING 
			"%s: shoot, we dropped an interrupt (0x%x)\n", 
			dev->name, rbuf[4] & rbuf[5]);
	}

	return 0;
}

#define SWAP_BITS(x)	( (((x) & 0x0001) << 15) | (((x) & 0x0002) << 13) \
			| (((x) & 0x0004) << 11) | (((x) & 0x0008) << 9)  \
			| (((x) & 0x0010) << 7)  | (((x) & 0x0020) << 5)  \
			| (((x) & 0x0040) << 3)  | (((x) & 0x0080) << 1)  \
			| (((x) & 0x0100) >> 1)  | (((x) & 0x0200) >> 3)  \
			| (((x) & 0x0400) >> 5)  | (((x) & 0x0800) >> 7)  \
			| (((x) & 0x1000) >> 9)  | (((x) & 0x2000) >> 11) \
			| (((x) & 0x4000) >> 13) | (((x) & 0x8000) >> 15) )

static int netdev_get_eeprom(struct net_device *dev, u8 *buf)
{
	int i;
	u16 *ebuf = (u16 *)buf;

	/* eeprom_read reads 16 bits, and indexes by 16 bits */
	for (i = 0; i < NATSEMI_EEPROM_SIZE/2; i++) {
		ebuf[i] = eeprom_read(dev->base_addr, i);
		/* The EEPROM itself stores data bit-swapped, but eeprom_read 
		 * reads it back "sanely". So we swap it back here in order to
		 * present it to userland as it is stored. */
		ebuf[i] = SWAP_BITS(ebuf[i]);
	}
	return 0;
}

static int netdev_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct mii_ioctl_data *data = (struct mii_ioctl_data *)&rq->ifr_data;

	switch(cmd) {
	case SIOCETHTOOL:
		return netdev_ethtool_ioctl(dev, (void *) rq->ifr_data);
	case SIOCGMIIPHY:		/* Get address of MII PHY in use. */
	case SIOCDEVPRIVATE:		/* for binary compat, remove in 2.5 */
		data->phy_id = 1;
		/* Fall Through */

	case SIOCGMIIREG:		/* Read MII PHY register. */
	case SIOCDEVPRIVATE+1:		/* for binary compat, remove in 2.5 */
		data->val_out = mdio_read(dev, data->phy_id & 0x1f, 
			data->reg_num & 0x1f);
		return 0;

	case SIOCSMIIREG:		/* Write MII PHY register. */
	case SIOCDEVPRIVATE+2:		/* for binary compat, remove in 2.5 */
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		mdio_write(dev, data->phy_id & 0x1f, data->reg_num & 0x1f, 
			data->val_in);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static void enable_wol_mode(struct net_device *dev, int enable_intr)
{
	long ioaddr = dev->base_addr;
	struct netdev_private *np = dev->priv;

	if (debug > 1)
		printk(KERN_INFO "%s: remaining active for wake-on-lan\n", 
			dev->name);

	/* For WOL we must restart the rx process in silent mode.
	 * Write NULL to the RxRingPtr. Only possible if
	 * rx process is stopped
	 */
	writel(0, ioaddr + RxRingPtr);

	/* read WoL status to clear */
	readl(ioaddr + WOLCmd);

	/* PME on, clear status */
	writel(np->SavedClkRun | PMEEnable | PMEStatus, ioaddr + ClkRun);

	/* and restart the rx process */
	writel(RxOn, ioaddr + ChipCmd);

	if (enable_intr) {
		/* enable the WOL interrupt.
		 * Could be used to send a netlink message.
		 */
		writel(readl(ioaddr + IntrMask) | WOLPkt, ioaddr + IntrMask);
	}
}

static int netdev_close(struct net_device *dev)
{
	long ioaddr = dev->base_addr;
	struct netdev_private *np = dev->priv;

	netif_stop_queue(dev);
	netif_carrier_off(dev);

	if (debug > 1) {
		printk(KERN_DEBUG "%s: Shutting down ethercard, status was %4.4x.\n",
			   dev->name, (int)readl(ioaddr + ChipCmd));
		printk(KERN_DEBUG "%s: Queue pointers were Tx %d / %d,  Rx %d / %d.\n",
			   dev->name, np->cur_tx, np->dirty_tx, np->cur_rx, np->dirty_rx);
	}

	del_timer_sync(&np->timer);

	disable_irq(dev->irq);
	spin_lock_irq(&np->lock);

	/* Disable and clear interrupts */
	writel(0, ioaddr + IntrEnable);
	readl(ioaddr + IntrMask);
	readw(ioaddr + MIntrStatus);

 	/* Freeze Stats */
	writel(StatsFreeze, ioaddr + StatsCtrl);
	    
	/* Stop the chip's Tx and Rx processes. */
	natsemi_stop_rxtx(dev);

	__get_stats(dev);
	spin_unlock_irq(&np->lock);

	/* race: shared irq and as most nics the DP83815
	 * reports _all_ interrupt conditions in IntrStatus, even
	 * disabled ones.
	 * packet received after disable_irq, but before stop_rxtx
	 * --> race. intr_handler would restart the rx process.
	 * netif_device_{de,a}tach around {enable,free}_irq.
	 */
	netif_device_detach(dev);
	enable_irq(dev->irq);
	free_irq(dev->irq, dev);
	netif_device_attach(dev);
	/* clear the carrier last - an interrupt could reenable it otherwise */
	netif_carrier_off(dev);

	dump_ring(dev);
	drain_ring(dev);
	free_ring(dev);

	 {
		u32 wol = readl(ioaddr + WOLCmd) & WakeOptsSummary;
		if (wol) {
			/* restart the NIC in WOL mode.
			 * The nic must be stopped for this.
			 */
			enable_wol_mode(dev, 0);
		} else {
			/* Restore PME enable bit unmolested */
			writel(np->SavedClkRun, ioaddr + ClkRun);
		}
	}
	return 0;
}


static void __devexit natsemi_remove1 (struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);

	unregister_netdev (dev);
	pci_release_regions (pdev);
	iounmap ((char *) dev->base_addr);
	kfree (dev);
	pci_set_drvdata(pdev, NULL);
}

#ifdef CONFIG_PM

/*
 * suspend/resume synchronization:
 * entry points:
 *   netdev_open, netdev_close, netdev_ioctl, set_rx_mode, intr_handler,
 *   start_tx, tx_timeout
 * Reading from some registers can restart the nic!
 * No function accesses the hardware without checking netif_device_present().
 * 	the check occurs under spin_lock_irq(&np->lock);
 * exceptions:
 * 	* netdev_ioctl, netdev_open.
 * 		net/core checks netif_device_present() before calling them.
 * 	* netdev_close: doesn't hurt.
 *	* netdev_timer: timer stopped by natsemi_suspend.
 *	* intr_handler: doesn't acquire the spinlock. suspend calls
 *		disable_irq() to enforce synchronization.
 *
 * netif_device_detach must occur under spin_unlock_irq(), interrupts from a
 * detached device would cause an irq storm.
 */

static int natsemi_suspend (struct pci_dev *pdev, u32 state)
{
	struct net_device *dev = pci_get_drvdata (pdev);
	struct netdev_private *np = dev->priv;
	long ioaddr = dev->base_addr;

	rtnl_lock();
	if (netif_running (dev)) {
		del_timer_sync(&np->timer);

		disable_irq(dev->irq);
		spin_lock_irq(&np->lock);

		writel(0, ioaddr + IntrEnable);
		natsemi_stop_rxtx(dev);
		netif_stop_queue(dev);
		netif_device_detach(dev);

		spin_unlock_irq(&np->lock);
		enable_irq(dev->irq);
		
		/* Update the error counts. */
		__get_stats(dev);

		/* pci_power_off(pdev, -1); */
		drain_ring(dev);
		{
			u32 wol = readl(ioaddr + WOLCmd) & WakeOptsSummary;
			/* Restore PME enable bit */
			if (wol) {
				/* restart the NIC in WOL mode.
				 * The nic must be stopped for this.
				 * FIXME: use the WOL interupt 
				 */
				enable_wol_mode(dev, 0);
			} else {
				/* Restore PME enable bit unmolested */
				writel(np->SavedClkRun, ioaddr + ClkRun);
			}
		}
	} else {
		netif_device_detach(dev);
	}
	rtnl_unlock();
	return 0;
}


static int natsemi_resume (struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata (pdev);
	struct netdev_private *np = dev->priv;

	rtnl_lock();
	if (netif_device_present(dev))
		goto out;
	if (netif_running(dev)) {
		pci_enable_device(pdev);
	/*	pci_power_on(pdev); */
		
		natsemi_reset(dev);
		init_ring(dev);
		spin_lock_irq(&np->lock);
		init_registers(dev);
		netif_device_attach(dev);
		spin_unlock_irq(&np->lock);

		mod_timer(&np->timer, jiffies + 1*HZ);
	} else {
		netif_device_attach(dev);
	}
out:
	rtnl_unlock();
	return 0;
}

#endif /* CONFIG_PM */

static struct pci_driver natsemi_driver = {
	name:		DRV_NAME,
	id_table:	natsemi_pci_tbl,
	probe:		natsemi_probe1,
	remove:		natsemi_remove1,
#ifdef CONFIG_PM
	suspend:	natsemi_suspend,
	resume:		natsemi_resume,
#endif
};

static int __init natsemi_init_mod (void)
{
/* when a module, this is printed whether or not devices are found in probe */
#ifdef MODULE
	printk(version);
#endif

	return pci_module_init (&natsemi_driver);
}

static void __exit natsemi_exit_mod (void)
{
	pci_unregister_driver (&natsemi_driver);
}

module_init(natsemi_init_mod);
module_exit(natsemi_exit_mod);

