/* yellowfin.c: A Packet Engines G-NIC ethernet driver for linux. */
/*
	Written 1997-1998 by Donald Becker.

	This software may be used and distributed according to the terms
	of the GNU Public License, incorporated herein by reference.

	This driver is for the Packet Engines G-NIC PCI Gigabit Ethernet adapter.
	It also supports the Symbios Logic version of the same chip core.

	The author may be reached as becker@CESDIS.gsfc.nasa.gov, or C/O
	Center of Excellence in Space Data and Information Sciences
	   Code 930.5, Goddard Space Flight Center, Greenbelt MD 20771

	Support and updates available at
	http://cesdis.gsfc.nasa.gov/linux/drivers/yellowfin.html
*/

static const char *version =
"yellowfin.c:v1.02 7/26/98  Written by Donald Becker, becker@cesdis.edu\n"
" http://cesdis.gsfc.nasa.gov/linux/drivers/yellowfin.html\n";

/* A few user-configurable values. */

static int max_interrupt_work = 20;
static int min_pci_latency = 64;
static int mtu = 0;
#ifdef YF_PROTOTYPE			/* Support for prototype hardware errata. */
/* System-wide count of bogus-rx frames. */
static int bogus_rx = 0;
static int dma_ctrl = 0x004A0263; 			/* Constrained by errata */
static int fifo_cfg = 0x0020;				/* Bypass external Tx FIFO. */
#elif YF_NEW					/* A future perfect board :->.  */
static int dma_ctrl = 0x00CAC277;			/* Override when loading module! */
static int fifo_cfg = 0x0028;
#else
static int dma_ctrl = 0x004A0263; 			/* Constrained by errata */
static int fifo_cfg = 0x0020;				/* Bypass external Tx FIFO. */
#endif

/* Set the copy breakpoint for the copy-only-tiny-frames scheme.
   Setting to > 1514 effectively disables this feature. */
static int rx_copybreak = 0;

/* Used to pass the media type, etc.
   No media types are currently defined.  These exist for driver
   interoperability.
*/
#define MAX_UNITS 8				/* More are supported, limit only on options */
static int options[MAX_UNITS] = {-1, -1, -1, -1, -1, -1, -1, -1};
static int full_duplex[MAX_UNITS] = {-1, -1, -1, -1, -1, -1, -1, -1};

/* Operational parameters that are set at compile time. */

/* Keep the ring sizes a power of two for efficiency.
   Making the Tx ring too large decreases the effectiveness of channel
   bonding and packet priority.
   There are no ill effects from too-large receive rings. */
#define TX_RING_SIZE	16
#define RX_RING_SIZE	32

/* Operational parameters that usually are not changed. */
/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT  ((2000*HZ)/1000)

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <asm/processor.h>		/* Processor type for cache alignment. */
#include <asm/bitops.h>
#include <asm/io.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

/* Kernel compatibility defines, most common to the PCCard package. */
#include <linux/version.h>		/* Evil and unneccessary */

#define RUN_AT(x) (jiffies + (x))

#if (LINUX_VERSION_CODE < 0x20123)
#define test_and_set_bit(val, addr) set_bit(val, addr)
#endif
#if LINUX_VERSION_CODE <= 0x20139
#define	net_device_stats enet_statistics
#define NETSTATS_VER2
#endif
#if LINUX_VERSION_CODE < 0x20155
#define PCI_SUPPORT_VER1
#define pci_present pcibios_present
#endif
#if LINUX_VERSION_CODE < 0x20159
#define DEV_FREE_SKB(skb) dev_kfree_skb(skb, FREE_WRITE);
#else
#define DEV_FREE_SKB(skb) dev_kfree_skb(skb);
#endif

/* The PCI I/O space extent. */
#define YELLOWFIN_TOTAL_SIZE 0x100

int yellowfin_debug = 1;

/*
				Theory of Operation

I. Board Compatibility

This device driver is designed for the Packet Engines "Yellowfin" Gigabit
Ethernet adapter.  The only PCA currently supported is the G-NIC 64-bit
PCI card.

II. Board-specific settings

PCI bus devices are configured by the system at boot time, so no jumpers
need to be set on the board.  The system BIOS preferably should assign the
PCI INTA signal to an otherwise unused system IRQ line.
Note: Kernel versions earlier than 1.3.73 do not support shared PCI
interrupt lines.

III. Driver operation

IIIa. Ring buffers

The Yellowfin uses the Descriptor Based DMA Architecture specified by Apple.
This is a descriptor list scheme similar to that used by the EEPro100 and
Tulip.  This driver uses two statically allocated fixed-size descriptor lists
formed into rings by a branch from the final descriptor to the beginning of
the list.  The ring sizes are set at compile time by RX/TX_RING_SIZE.

The driver allocates full frame size skbuffs for the Rx ring buffers at
open() time and passes the skb->data field to the Yellowfin as receive data
buffers.  When an incoming frame is less than RX_COPYBREAK bytes long,
a fresh skbuff is allocated and the frame is copied to the new skbuff.
When the incoming frame is larger, the skbuff is passed directly up the
protocol stack and replaced by a newly allocated skbuff.

The RX_COPYBREAK value is chosen to trade-off the memory wasted by
using a full-sized skbuff for small frames vs. the copying costs of larger
frames.  For small frames the copying cost is negligible (esp. considering
that we are pre-loading the cache with immediately useful header
information).  For large frames the copying cost is non-trivial, and the
larger copy might flush the cache of useful data.

IIIC. Synchronization

The driver runs as two independent, single-threaded flows of control.  One
is the send-packet routine, which enforces single-threaded use by the
dev->tbusy flag.  The other thread is the interrupt handler, which is single
threaded by the hardware and other software.

The send packet thread has partial control over the Tx ring and 'dev->tbusy'
flag.  It sets the tbusy flag whenever it's queuing a Tx packet. If the next
queue slot is empty, it clears the tbusy flag when finished otherwise it sets
the 'yp->tx_full' flag.

The interrupt handler has exclusive control over the Rx ring and records stats
from the Tx ring.  After reaping the stats, it marks the Tx queue entry as
empty by incrementing the dirty_tx mark. Iff the 'yp->tx_full' flag is set, it
clears both the tx_full and tbusy flags.

IV. Notes

Thanks to Kim Stearns of Packet Engines for providing a pair of G-NIC boards.
Thanks to Bruce Faust of Digitalscape for providing both their SYM53C885 board
and an AlphaStation to verifty the Alpha port!

IVb. References

Yellowfin Engineering Design Specification, 4/23/97 Preliminary/Confidential
Symbios SYM53C885 PCI-SCSI/Fast Ethernet Multifunction Controller Preliminary
   Data Manual v3.0
http://cesdis.gsfc.nasa.gov/linux/misc/NWay.html
http://cesdis.gsfc.nasa.gov/linux/misc/100mbps.html

IVc. Errata

See Packet Engines confidential appendix (prototype chips only).

*/

/* A few values that may be tweaked. */
#define PKT_BUF_SZ		1536			/* Size of each temporary Rx buffer.*/

#ifndef PCI_VENDOR_ID_PKT_ENG		/* To be defined in linux/pci.h */
#define PCI_VENDOR_ID_PKT_ENG			0x1000 /* Hmm, likely number.. */
#define PCI_DEVICE_ID_SYM58C885			0x0701
#define PCI_DEVICE_ID_YELLOWFIN			0x0702
#endif

/* The rest of these values should never change. */

static void yellowfin_timer(unsigned long data);

enum capability_flags {HasMII=1, FullTxStatus=2};
static struct chip_info {
	u16	vendor_id, device_id, device_id_mask, pci_flags;
	const char *name;
	void (*media_timer)(unsigned long data);
	u32 chip_rev;				/* As read from ChipRev, not PCI dev ID. */
	int flags;
} chip_tbl[] = {
	{0x1000, 0x0702, 0xffff, 0, "Yellowfin G-NIC Gbit Ethernet",
	 yellowfin_timer, 0x0702, FullTxStatus},
	{0x1000, 0x0701, 0xffff, 0, "Symbios SYM83C885",
	 yellowfin_timer, 0x0701, HasMII},
	{0,},
};

/* Offsets to the Yellowfin registers.  Various sizes and alignments. */
enum yellowfin_offsets {
	TxCtrl=0x00, TxStatus=0x04, TxPtr=0x0C,
	TxIntrSel=0x10, TxBranchSel=0x14, TxWaitSel=0x18,
	RxCtrl=0x40, RxStatus=0x44, RxPtr=0x4C,
	RxIntrSel=0x50, RxBranchSel=0x54, RxWaitSel=0x58,
	EventStatus=0x80, IntrEnb=0x82, IntrClear=0x84, IntrStatus=0x86,
	ChipRev=0x8C, DMACtrl=0x90, Cnfg=0xA0, FrameGap0=0xA2, FrameGap1=0xA4,
	MII_Cmd=0xA6, MII_Addr=0xA8, MII_Wr_Data=0xAA, MII_Rd_Data=0xAC,
	MII_Status=0xAE,
	RxDepth=0xB8, FlowCtrl=0xBC,
	AddrMode=0xD0, StnAddr=0xD2, HashTbl=0xD8, FIFOcfg=0xF8,
	EEStatus=0xF0, EECtrl=0xF1, EEAddr=0xF2, EERead=0xF3, EEWrite=0xF4,
	EEFeature=0xF5,
};

/* The Yellowfin Rx and Tx buffer descriptors. */
struct yellowfin_desc {
	u16 request_cnt;
	u16 cmd;
	u32 addr;
	u32 branch_addr;
	u16 result_cnt;
	u16 status;
};

struct tx_status_words {
	u16 tx_cnt;
	u16 tx_errs;
	u16 total_tx_cnt;
	u16 paused;
};

/* Bits in yellowfin_desc.cmd */
enum desc_cmd_bits {
	CMD_TX_PKT=0x1000, CMD_RX_BUF=0x2000, CMD_TXSTATUS=0x3000,
	CMD_NOP=0x6000, CMD_STOP=0x7000,
	BRANCH_ALWAYS=0x0C, INTR_ALWAYS=0x30, WAIT_ALWAYS=0x03,
	BRANCH_IFTRUE=0x04,
};

/* Bits in yellowfin_desc.status */
enum desc_status_bits { RX_EOP=0x0040, };

/* Bits in the interrupt status/mask registers. */
enum intr_status_bits {
	IntrRxDone=0x01, IntrRxInvalid=0x02, IntrRxPCIFault=0x04,IntrRxPCIErr=0x08,
	IntrTxDone=0x10, IntrTxInvalid=0x20, IntrTxPCIFault=0x40,IntrTxPCIErr=0x80,
	IntrEarlyRx=0x100, IntrWakeup=0x200, };

struct yellowfin_private {
	/* Descriptor rings first for alignment.  Tx requires a second descriptor
	   for status. */
	struct yellowfin_desc rx_ring[RX_RING_SIZE];
	struct yellowfin_desc tx_ring[TX_RING_SIZE*2];
	const char *product_name;
	struct device *next_module;
	/* The addresses of receive-in-place skbuffs. */
	struct sk_buff* rx_skbuff[RX_RING_SIZE];
	/* The saved address of a sent-in-place packet/buffer, for skfree(). */
	struct sk_buff* tx_skbuff[TX_RING_SIZE];
	struct tx_status_words tx_status[TX_RING_SIZE];
	struct timer_list timer;	/* Media selection timer. */
	struct enet_statistics stats;
	/* Frequently used and paired value: keep adjacent for cache effect. */
	int chip_id;
	int in_interrupt;
	struct yellowfin_desc *rx_head_desc;
	struct tx_status_words *tx_tail_desc;
	unsigned int cur_rx, dirty_rx;		/* Producer/consumer ring indices */
	unsigned int cur_tx, dirty_tx;
	unsigned int rx_buf_sz;				/* Based on MTU+slack. */
	unsigned int tx_full:1;				/* The Tx queue is full. */
	unsigned int full_duplex:1;			/* Full-duplex operation requested. */
	unsigned int duplex_lock:1;
	unsigned int medialock:1;			/* Do not sense media. */
	unsigned int default_port:4;		/* Last dev->if_port value. */
	/* MII transceiver section. */
	int mii_cnt;						/* MII device addresses. */
	u16 advertising;					/* NWay media advertisement */
	unsigned char phys[2];				/* MII device addresses. */
	u32 pad[4];							/* Used for 32-byte alignment */
};

#ifdef MODULE

#if LINUX_VERSION_CODE > 0x20115
MODULE_AUTHOR("Donald Becker <becker@cesdis.gsfc.nasa.gov>");
MODULE_DESCRIPTION("Packet Engines Yellowfin G-NIC Gigabit Ethernet driver");
MODULE_PARM(max_interrupt_work, "i");
MODULE_PARM(min_pci_latency, "i");
MODULE_PARM(mtu, "i");
MODULE_PARM(debug, "i");
MODULE_PARM(rx_copybreak, "i");
MODULE_PARM(options, "1-" __MODULE_STRING(MAX_UNITS) "i");
MODULE_PARM(full_duplex, "1-" __MODULE_STRING(MAX_UNITS) "i");
#endif

#endif

static struct device *yellowfin_probe1(struct device *dev, long ioaddr,
									   int irq, int chip_id, int options);
static int read_eeprom(long ioaddr, int location);
static int mdio_read(long ioaddr, int phy_id, int location);
static void mdio_write(long ioaddr, int phy_id, int location, int value);
#ifdef HAVE_PRIVATE_IOCTL
static int mii_ioctl(struct device *dev, struct ifreq *rq, int cmd);
#endif
static int yellowfin_open(struct device *dev);
static void yellowfin_timer(unsigned long data);
static void yellowfin_tx_timeout(struct device *dev);
static void yellowfin_init_ring(struct device *dev);
static int yellowfin_start_xmit(struct sk_buff *skb, struct device *dev);
static void yellowfin_interrupt(int irq, void *dev_instance, struct pt_regs *regs);
static int yellowfin_rx(struct device *dev);
static void yellowfin_error(struct device *dev, int intr_status);
static int yellowfin_close(struct device *dev);
static struct enet_statistics *yellowfin_get_stats(struct device *dev);
static void set_rx_mode(struct device *dev);



/* A list of all installed Yellowfin devices, for removing the driver module. */
static struct device *root_yellowfin_dev = NULL;

int yellowfin_probe(struct device *dev)
{
	int cards_found = 0;
	int pci_index = 0;
	unsigned char pci_bus, pci_device_fn;

	if ( ! pci_present())
		return -ENODEV;

	for (;pci_index < 0xff; pci_index++) {
		u8 pci_latency;
		u16 pci_command, new_command, vendor, device;
		int chip_idx;
		int irq;
		long ioaddr;

		if (pcibios_find_class (PCI_CLASS_NETWORK_ETHERNET << 8,
								pci_index,
								&pci_bus, &pci_device_fn)
			!= PCIBIOS_SUCCESSFUL)
			break;

		pcibios_read_config_word(pci_bus, pci_device_fn,
								 PCI_VENDOR_ID, &vendor);
		pcibios_read_config_word(pci_bus, pci_device_fn,
								 PCI_DEVICE_ID, &device);

		for (chip_idx = 0; chip_tbl[chip_idx].vendor_id; chip_idx++)
			if (vendor == chip_tbl[chip_idx].vendor_id
				&& (device & chip_tbl[chip_idx].device_id_mask) ==
				chip_tbl[chip_idx].device_id)
				break;
		if (chip_tbl[chip_idx].vendor_id == 0) 		/* Compiled out! */
			continue;

		{
#if LINUX_VERSION_CODE >= 0x20155  ||  PCI_SUPPORT_1
			struct pci_dev *pdev = pci_find_slot(pci_bus, pci_device_fn);
			ioaddr = pdev->base_address[0];
			irq = pdev->irq;
#else
			u32 pci_ioaddr;
			u8 pci_irq_line;
			pcibios_read_config_byte(pci_bus, pci_device_fn,
									 PCI_INTERRUPT_LINE, &pci_irq_line);
			pcibios_read_config_dword(pci_bus, pci_device_fn,
									  PCI_BASE_ADDRESS_0, &pci_ioaddr);
			ioaddr = pci_ioaddr;
			irq = pci_irq_line;
#endif
		}
		/* Remove I/O space marker in bit 0. */
		ioaddr &= ~3;

		if (yellowfin_debug > 2)
			printk(KERN_INFO "Found %s at I/O %#lx, IRQ %d.\n",
				   chip_tbl[chip_idx].name, ioaddr, irq);

		if (check_region(ioaddr, YELLOWFIN_TOTAL_SIZE))
			continue;

		pcibios_read_config_word(pci_bus, pci_device_fn,
								 PCI_COMMAND, &pci_command);
		new_command = pci_command | PCI_COMMAND_MASTER|PCI_COMMAND_IO;
		if (pci_command != new_command) {
			printk(KERN_INFO "  The PCI BIOS has not enabled the"
				   " device at %d/%d!  Updating PCI command %4.4x->%4.4x.\n",
				   pci_bus, pci_device_fn, pci_command, new_command);
			pcibios_write_config_word(pci_bus, pci_device_fn,
									  PCI_COMMAND, new_command);
		}

		dev = yellowfin_probe1(dev, ioaddr, irq, chip_idx, cards_found);

		if (dev) {
			/* Get and check the bus-master and latency values. */
			pcibios_read_config_byte(pci_bus, pci_device_fn,
									 PCI_LATENCY_TIMER, &pci_latency);
			if (pci_latency < min_pci_latency) {
				printk(KERN_INFO "  PCI latency timer (CFLT) is "
					   "unreasonably low at %d.  Setting to %d clocks.\n",
					   pci_latency, min_pci_latency);
				pcibios_write_config_byte(pci_bus, pci_device_fn,
										  PCI_LATENCY_TIMER, min_pci_latency);
			} else if (yellowfin_debug > 1)
				printk(KERN_INFO "  PCI latency timer (CFLT) is %#x.\n",
					   pci_latency);
			dev = 0;
			cards_found++;
		}
	}

	return cards_found ? 0 : -ENODEV;
}

static struct device *yellowfin_probe1(struct device *dev, long ioaddr,
									   int irq, int chip_id, int card_idx)
{
	static int did_version = 0;			/* Already printed version info. */
	struct yellowfin_private *yp;
	int option, i;

	if (yellowfin_debug > 0  &&  did_version++ == 0)
		printk(version);

	dev = init_etherdev(dev, sizeof(struct yellowfin_private));

	printk(KERN_INFO "%s: %s type %8x at 0x%lx, ",
		   dev->name, chip_tbl[chip_id].name, inl(ioaddr + ChipRev), ioaddr);

	if (inw(ioaddr + ChipRev) == 0x0702)
		for (i = 0; i < 6; i++)
			dev->dev_addr[i] = inb(ioaddr + StnAddr + i);
	else {
		int ee_offset = (read_eeprom(ioaddr, 6) == 0xff ? 0x100 : 0);
		for (i = 0; i < 6; i++)
			dev->dev_addr[i] = read_eeprom(ioaddr, ee_offset + i);
	}
	for (i = 0; i < 5; i++)
			printk("%2.2x:", dev->dev_addr[i]);
	printk("%2.2x, IRQ %d.\n", dev->dev_addr[i], irq);

	/* Reset the chip. */
	outl(0x80000000, ioaddr + DMACtrl);

	/* We do a request_region() only to register /proc/ioports info. */
	request_region(ioaddr, YELLOWFIN_TOTAL_SIZE, dev->name);

	dev->base_addr = ioaddr;
	dev->irq = irq;

	/* Make certain the descriptor lists are aligned. */
	yp = (void *)(((long)kmalloc(sizeof(*yp), GFP_KERNEL) + 31) & ~31);
	memset(yp, 0, sizeof(*yp));
	dev->priv = yp;

	yp->next_module = root_yellowfin_dev;
	root_yellowfin_dev = dev;

	yp->chip_id = chip_id;

	option = card_idx < MAX_UNITS ? options[card_idx] : 0;
	if (dev->mem_start)
		option = dev->mem_start;

	/* The lower four bits are the media type. */
	if (option > 0) {
		if (option & 0x200)
			yp->full_duplex = 1;
		yp->default_port = option & 15;
		if (yp->default_port)
			yp->medialock = 1;
	}
	if (card_idx < MAX_UNITS  &&  full_duplex[card_idx] > 0)
		yp->full_duplex = 1;

	if (yp->full_duplex)
		yp->duplex_lock = 1;

	/* The Yellowfin-specific entries in the device structure. */
	dev->open = &yellowfin_open;
	dev->hard_start_xmit = &yellowfin_start_xmit;
	dev->stop = &yellowfin_close;
	dev->get_stats = &yellowfin_get_stats;
	dev->set_multicast_list = &set_rx_mode;
#ifdef HAVE_PRIVATE_IOCTL
	dev->do_ioctl = &mii_ioctl;
#endif
	if (mtu)
		dev->mtu = mtu;

	if (chip_tbl[yp->chip_id].flags & HasMII) {
		int phy, phy_idx = 0;
		for (phy = 0; phy < 32 && phy_idx < 4; phy++) {
			int mii_status = mdio_read(ioaddr, phy, 1);
			if (mii_status != 0xffff  &&
				mii_status != 0x0000) {
				yp->phys[phy_idx++] = phy;
				yp->advertising = mdio_read(ioaddr, phy, 4);
				printk(KERN_INFO "%s: MII PHY found at address %d, status "
					   "0x%4.4x advertising %4.4x.\n",
					   dev->name, phy, mii_status, yp->advertising);
			}
		}
		yp->mii_cnt = phy_idx;
	}

	return dev;
}

static int read_eeprom(long ioaddr, int location)
{
	int bogus_cnt = 1000;

	outb(location, ioaddr + EEAddr);
	outb(0x30 | ((location >> 8) & 7), ioaddr + EECtrl);
	while ((inb(ioaddr + EEStatus) & 0x80)  && --bogus_cnt > 0)
		;
	return inb(ioaddr + EERead);
}

/* MII Managemen Data I/O accesses.
   These routines assume the MDIO controller is idle, and do not exit until
   the command is finished. */

static int mdio_read(long ioaddr, int phy_id, int location)
{
	int i;

	outw((phy_id<<8) + location, ioaddr + MII_Addr);
	outw(1, ioaddr + MII_Cmd);
	for (i = 10000; i >= 0; i--)
		if ((inw(ioaddr + MII_Status) & 1) == 0)
			break;
	return inw(ioaddr + MII_Rd_Data);
}

static void mdio_write(long ioaddr, int phy_id, int location, int value)
{
	int i;

	outw((phy_id<<8) + location, ioaddr + MII_Addr);
	outw(value, ioaddr + MII_Wr_Data);

	/* Wait for the command to finish. */
	for (i = 10000; i >= 0; i--)
		if ((inw(ioaddr + MII_Status) & 1) == 0)
			break;
	return;
}


static int yellowfin_open(struct device *dev)
{
	struct yellowfin_private *yp = (struct yellowfin_private *)dev->priv;
	long ioaddr = dev->base_addr;
	int i;

	/* Reset the chip. */
	outl(0x80000000, ioaddr + DMACtrl);

	if (request_irq(dev->irq, &yellowfin_interrupt, SA_SHIRQ, dev->name, dev))
		return -EAGAIN;

	if (yellowfin_debug > 1)
		printk(KERN_DEBUG "%s: yellowfin_open() irq %d.\n",
			   dev->name, dev->irq);

	MOD_INC_USE_COUNT;

	yellowfin_init_ring(dev);

	outl(virt_to_bus(yp->rx_ring), ioaddr + RxPtr);
	outl(virt_to_bus(yp->tx_ring), ioaddr + TxPtr);

	for (i = 0; i < 6; i++)
		outb(dev->dev_addr[i], ioaddr + StnAddr + i);

	/* Set up various condition 'select' registers.
	   There are no options here. */
	outl(0x00800080, ioaddr + TxIntrSel); 	/* Interrupt on Tx abort */
	outl(0x00800080, ioaddr + TxBranchSel);	/* Branch on Tx abort */
	outl(0x00400040, ioaddr + TxWaitSel); 	/* Wait on Tx status */
	outl(0x00400040, ioaddr + RxIntrSel);	/* Interrupt on Rx done */
	outl(0x00400040, ioaddr + RxBranchSel);	/* Branch on Rx error */
	outl(0x00400040, ioaddr + RxWaitSel);	/* Wait on Rx done */

	/* Initialize other registers: with so many this eventually this will
	   converted to an offset/value list. */
	outl(dma_ctrl, ioaddr + DMACtrl);
	outw(fifo_cfg, ioaddr + FIFOcfg);
	/* Enable automatic generation of flow control frames, period 0xffff. */
	outl(0x0030FFFF, ioaddr + FlowCtrl);

	if (dev->if_port == 0)
		dev->if_port = yp->default_port;

	dev->tbusy = 0;
	dev->interrupt = 0;
	yp->in_interrupt = 0;

	/* Setting the Rx mode will start the Rx process. */
	if (yp->chip_id == 0) {
		/* We are always in full-duplex mode with gigabit! */
		yp->full_duplex = 1;
		outw(0x01CF, ioaddr + Cnfg);
	} else {
		outw(0x0018, ioaddr + FrameGap0); /* 0060/4060 for non-MII 10baseT */
		outw(0x1018, ioaddr + FrameGap1);
		outw(0x101C | (yp->full_duplex ? 2 : 0), ioaddr + Cnfg);
	}
	set_rx_mode(dev);

	dev->start = 1;

	/* Enable interrupts by setting the interrupt mask. */
	outw(0x81ff, ioaddr + IntrEnb);			/* See enum intr_status_bits */
	outw(0x0000, ioaddr + EventStatus);		/* Clear non-interrupting events */
	outl(0x80008000, ioaddr + RxCtrl);		/* Start Rx and Tx channels. */
	outl(0x80008000, ioaddr + TxCtrl);

	if (yellowfin_debug > 2) {
		printk(KERN_DEBUG "%s: Done yellowfin_open().\n",
			   dev->name);
	}
	/* Set the timer to check for link beat. */
	init_timer(&yp->timer);
	yp->timer.expires = RUN_AT((24*HZ)/10);			/* 2.4 sec. */
	yp->timer.data = (unsigned long)dev;
	yp->timer.function = &yellowfin_timer;				/* timer handler */
	add_timer(&yp->timer);

	return 0;
}

static void yellowfin_timer(unsigned long data)
{
	struct device *dev = (struct device *)data;
	struct yellowfin_private *yp = (struct yellowfin_private *)dev->priv;
	long ioaddr = dev->base_addr;
	int next_tick = 0;

	if (yellowfin_debug > 3) {
		printk(KERN_DEBUG "%s: Yellowfin timer tick, status %8.8x.\n",
			   dev->name, inw(ioaddr + IntrStatus));
	}

	if (yp->mii_cnt) {
		int mii_reg1 = mdio_read(ioaddr, yp->phys[0], 1);
		int mii_reg5 = mdio_read(ioaddr, yp->phys[0], 5);
		int negotiated = mii_reg5 & yp->advertising;
		if (yellowfin_debug > 1)
			printk(KERN_DEBUG "%s: MII #%d status register is %4.4x, "
				   "link partner capability %4.4x.\n",
				   dev->name, yp->phys[0], mii_reg1, mii_reg5);

		if ( ! yp->duplex_lock &&
			 ((negotiated & 0x0300) == 0x0100
			  || (negotiated & 0x00C0) == 0x0040)) {
			yp->full_duplex = 1;
		}
		outw(0x101C | (yp->full_duplex ? 2 : 0), ioaddr + Cnfg);

		if (mii_reg1 & 0x0004)
			next_tick = 60*HZ;
		else
			next_tick = 3*HZ;
	}

	if (next_tick) {
		yp->timer.expires = RUN_AT(next_tick);
		add_timer(&yp->timer);
	}
}

static void yellowfin_tx_timeout(struct device *dev)
{
	struct yellowfin_private *yp = (struct yellowfin_private *)dev->priv;
	long ioaddr = dev->base_addr;

	printk(KERN_WARNING "%s: Yellowfin transmit timed out, status %8.8x, resetting...\n",
		   dev->name, inl(ioaddr));

#ifndef __alpha__
	{
		int i;
		printk(KERN_DEBUG "  Rx ring %8.8x: ", (int)yp->rx_ring);
		for (i = 0; i < RX_RING_SIZE; i++)
			printk(" %8.8x", (unsigned int)yp->rx_ring[i].status);
		printk("\n"KERN_DEBUG"  Tx ring %8.8x: ", (int)yp->tx_ring);
		for (i = 0; i < TX_RING_SIZE; i++)
			printk(" %4.4x /%4.4x", yp->tx_status[i].tx_errs, yp->tx_ring[i].status);
		printk("\n");
	}
#endif

  /* Perhaps we should reinitialize the hardware here. */
  dev->if_port = 0;
  /* Stop and restart the chip's Tx processes . */

  /* Trigger an immediate transmit demand. */

  dev->trans_start = jiffies;
  yp->stats.tx_errors++;
  return;
}


/* Initialize the Rx and Tx rings, along with various 'dev' bits. */
static void yellowfin_init_ring(struct device *dev)
{
	struct yellowfin_private *yp = (struct yellowfin_private *)dev->priv;
	int i;

	yp->tx_full = 0;
	yp->cur_rx = yp->cur_tx = 0;
	yp->dirty_rx = yp->dirty_tx = 0;

	yp->rx_buf_sz = (dev->mtu <= 1500 ? PKT_BUF_SZ : dev->mtu + 32);
	yp->rx_head_desc = &yp->rx_ring[0];

	for (i = 0; i < RX_RING_SIZE; i++) {
		struct sk_buff *skb;

		yp->rx_ring[i].request_cnt = yp->rx_buf_sz;
		yp->rx_ring[i].cmd = CMD_RX_BUF | INTR_ALWAYS;

		skb = dev_alloc_skb(yp->rx_buf_sz);
		yp->rx_skbuff[i] = skb;
		if (skb) {
			skb->dev = dev;			/* Mark as being used by this device. */
			skb_reserve(skb, 2);	/* 16 byte align the IP header. */
			yp->rx_ring[i].addr = virt_to_bus(skb->tail);
		} else if (yp->dirty_rx == 0)
			yp->dirty_rx = (unsigned int)(0 - RX_RING_SIZE);
		yp->rx_ring[i].branch_addr = virt_to_bus(&yp->rx_ring[i+1]);
	}
	/* Mark the last entry as wrapping the ring. */
	yp->rx_ring[i-1].cmd = CMD_RX_BUF | INTR_ALWAYS | BRANCH_ALWAYS;
	yp->rx_ring[i-1].branch_addr = virt_to_bus(&yp->rx_ring[0]);

/*#define NO_TXSTATS*/
#ifdef NO_TXSTATS
	/* In this mode the Tx ring needs only a single descriptor. */
	for (i = 0; i < TX_RING_SIZE; i++) {
		yp->tx_skbuff[i] = 0;
		yp->tx_ring[i].cmd = CMD_STOP;
		yp->tx_ring[i].branch_addr = virt_to_bus(&yp->tx_ring[i+1]);
	}
	yp->tx_ring[--i].cmd = CMD_STOP | BRANCH_ALWAYS; /* Wrap ring */
	yp->tx_ring[i].branch_addr = virt_to_bus(&yp->tx_ring[0]);
#else
	/* Tx ring needs a pair of descriptors, the second for the status. */
	for (i = 0; i < TX_RING_SIZE*2; i++) {
		yp->tx_skbuff[i/2] = 0;
		yp->tx_ring[i].cmd = CMD_STOP; /* Branch on Tx error. */
		yp->tx_ring[i].branch_addr = virt_to_bus(&yp->tx_ring[i+1]);
		i++;
		if (chip_tbl[yp->chip_id].flags & FullTxStatus) {
			yp->tx_ring[i].cmd = CMD_TXSTATUS;
			yp->tx_ring[i].request_cnt = sizeof(yp->tx_status[i]);
			yp->tx_ring[i].addr = virt_to_bus(&yp->tx_status[i/2]);
		} else {				/* Symbios chips write only tx_errs word. */
			yp->tx_ring[i].cmd = CMD_TXSTATUS | INTR_ALWAYS;
			yp->tx_ring[i].request_cnt = 2;
			yp->tx_ring[i].addr = virt_to_bus(&yp->tx_status[i/2].tx_errs);
		}
		yp->tx_ring[i].branch_addr = virt_to_bus(&yp->tx_ring[i+1]);
	}
	/* Wrap ring */
	yp->tx_ring[--i].cmd = CMD_TXSTATUS | BRANCH_ALWAYS | INTR_ALWAYS;
	yp->tx_ring[i].branch_addr = virt_to_bus(&yp->tx_ring[0]);
#endif
	yp->tx_tail_desc = &yp->tx_status[0];
	return;
}

static int yellowfin_start_xmit(struct sk_buff *skb, struct device *dev)
{
	struct yellowfin_private *yp = (struct yellowfin_private *)dev->priv;
	unsigned entry;

	/* Block a timer-based transmit from overlapping.  This could better be
	   done with atomic_swap(1, dev->tbusy), but set_bit() works as well. */
	if (test_and_set_bit(0, (void*)&dev->tbusy) != 0) {
		if (jiffies - dev->trans_start < TX_TIMEOUT)
			return 1;
		yellowfin_tx_timeout(dev);
		return 1;
	}

	/* Caution: the write order is important here, set the base address
	   with the "ownership" bits last. */

	/* Calculate the next Tx descriptor entry. */
	entry = yp->cur_tx % TX_RING_SIZE;

	yp->tx_skbuff[entry] = skb;

#ifdef NO_TXSTATS
	yp->tx_ring[entry].request_cnt = skb->len;
	yp->tx_ring[entry].addr = virt_to_bus(skb->data);
	yp->tx_ring[entry].status = 0;
	if (entry >= TX_RING_SIZE-1) {
		yp->tx_ring[0].cmd = CMD_STOP; /* New stop command. */
		yp->tx_ring[TX_RING_SIZE-1].cmd = CMD_TX_PKT | BRANCH_ALWAYS;
	} else {
		yp->tx_ring[entry+1].cmd = CMD_STOP; /* New stop command. */
		yp->tx_ring[entry].cmd = CMD_TX_PKT | BRANCH_IFTRUE;
	}
	yp->cur_tx++;
#else
	yp->tx_ring[entry<<1].request_cnt = skb->len;
	yp->tx_ring[entry<<1].addr = virt_to_bus(skb->data);
	/* The input_last (status-write) command is constant, but we must rewrite
	   the subsequent 'stop' command. */

	yp->cur_tx++;
	{
		unsigned next_entry = yp->cur_tx % TX_RING_SIZE;
		yp->tx_ring[next_entry<<1].cmd = CMD_STOP;
	}
	/* Final step -- overwrite the old 'stop' command. */

	yp->tx_ring[entry<<1].cmd =
		(entry % 6) == 0 ? CMD_TX_PKT | INTR_ALWAYS | BRANCH_IFTRUE :
		CMD_TX_PKT | BRANCH_IFTRUE;
#endif

	/* Non-x86 Todo: explicitly flush cache lines here. */

	/* Wake the potentially-idle transmit channel. */
	outl(0x10001000, dev->base_addr + TxCtrl);

	if (yp->cur_tx - yp->dirty_tx < TX_RING_SIZE - 1)
		clear_bit(0, (void*)&dev->tbusy);		/* Typical path */
	else
		yp->tx_full = 1;
	dev->trans_start = jiffies;

	if (yellowfin_debug > 4) {
		printk(KERN_DEBUG "%s: Yellowfin transmit frame #%d queued in slot %d.\n",
			   dev->name, yp->cur_tx, entry);
	}
	return 0;
}

/* The interrupt handler does all of the Rx thread work and cleans up
   after the Tx thread. */
static void yellowfin_interrupt(int irq, void *dev_instance, struct pt_regs *regs)
{
	struct device *dev = (struct device *)dev_instance;
	struct yellowfin_private *yp;
	long ioaddr, boguscnt = max_interrupt_work;

#ifndef final_version			/* Can never occur. */
	if (dev == NULL) {
		printk (KERN_ERR "yellowfin_interrupt(): irq %d for unknown device.\n", irq);
		return;
	}
#endif

	ioaddr = dev->base_addr;
	yp = (struct yellowfin_private *)dev->priv;
	if (test_and_set_bit(0, (void*)&yp->in_interrupt)) {
		dev->interrupt = 1;
		printk(KERN_ERR "%s: Re-entering the interrupt handler.\n", dev->name);
		return;
	}

	do {
		u16 intr_status = inw(ioaddr + IntrClear);

		if (yellowfin_debug > 4)
			printk(KERN_DEBUG "%s: Yellowfin interrupt, status %4.4x.\n",
				   dev->name, intr_status);

		if (intr_status == 0)
			break;

		if (intr_status & (IntrRxDone | IntrEarlyRx)) {
			yellowfin_rx(dev);
			outl(0x10001000, ioaddr + RxCtrl);		/* Wake Rx engine. */
		}

#ifdef NO_TXSTATS
		for (; yp->cur_tx - yp->dirty_tx > 0; yp->dirty_tx++) {
			int entry = yp->dirty_tx % TX_RING_SIZE;
			if (yp->tx_ring[entry].status == 0)
				break;
			/* Free the original skb. */
			DEV_FREE_SKB(yp->tx_skbuff[entry]);
			yp->tx_skbuff[entry] = 0;
			yp->stats.tx_packets++;
		}
		if (yp->tx_full && dev->tbusy
			&& yp->cur_tx - yp->dirty_tx < TX_RING_SIZE - 4) {
			/* The ring is no longer full, clear tbusy. */
			yp->tx_full = 0;
			clear_bit(0, (void*)&dev->tbusy);
			mark_bh(NET_BH);
		}
#else
		if (intr_status & IntrTxDone
			|| yp->tx_tail_desc->tx_errs) {
			unsigned dirty_tx = yp->dirty_tx;

			for (dirty_tx = yp->dirty_tx; yp->cur_tx - dirty_tx > 0;
				 dirty_tx++) {
				/* Todo: optimize this. */
				int entry = dirty_tx % TX_RING_SIZE;
				u16 tx_errs = yp->tx_status[entry].tx_errs;

#ifndef final_version
				if (yellowfin_debug > 5)
					printk(KERN_DEBUG "%s: Tx queue %d check, Tx status "
						   "%4.4x %4.4x %4.4x %4.4x.\n",
						   dev->name, entry,
						   yp->tx_status[entry].tx_cnt,
						   yp->tx_status[entry].tx_errs,
						   yp->tx_status[entry].total_tx_cnt,
						   yp->tx_status[entry].paused);
#endif
				if (tx_errs == 0)
					break;			/* It still hasn't been Txed */
				if (tx_errs & 0xF8100000) {
					/* There was an major error, log it. */
#ifndef final_version
					if (yellowfin_debug > 1)
						printk(KERN_DEBUG "%s: Transmit error, Tx status %4.4x.\n",
							   dev->name, tx_errs);
#endif
					yp->stats.tx_errors++;
					if (tx_errs & 0xF800) yp->stats.tx_aborted_errors++;
					if (tx_errs & 0x0800) yp->stats.tx_carrier_errors++;
					if (tx_errs & 0x2000) yp->stats.tx_window_errors++;
					if (tx_errs & 0x8000) yp->stats.tx_fifo_errors++;
#ifdef ETHER_STATS
					if (tx_errs & 0x1000) yp->stats.collisions16++;
#endif
				} else {
#ifndef final_version
					if (yellowfin_debug > 4)
						printk(KERN_DEBUG "%s: Normal transmit, Tx status %4.4x.\n",
							   dev->name, tx_errs);
#endif
#ifdef ETHER_STATS
					if (tx_errs & 0x0400) yp->stats.tx_deferred++;
#endif
					yp->stats.collisions += tx_errs & 15;
					yp->stats.tx_packets++;
				}

				/* Free the original skb. */
				DEV_FREE_SKB(yp->tx_skbuff[entry]);
				yp->tx_skbuff[entry] = 0;
				/* Mark status as empty. */
				yp->tx_status[entry].tx_errs = 0;
			}

#ifndef final_version
			if (yp->cur_tx - dirty_tx > TX_RING_SIZE) {
				printk(KERN_ERR "%s: Out-of-sync dirty pointer, %d vs. %d, full=%d.\n",
					   dev->name, dirty_tx, yp->cur_tx, yp->tx_full);
				dirty_tx += TX_RING_SIZE;
			}
#endif

			if (yp->tx_full && dev->tbusy
				&& yp->cur_tx - dirty_tx < TX_RING_SIZE - 2) {
				/* The ring is no longer full, clear tbusy. */
				yp->tx_full = 0;
				clear_bit(0, (void*)&dev->tbusy);
				mark_bh(NET_BH);
			}

			yp->dirty_tx = dirty_tx;
			yp->tx_tail_desc = &yp->tx_status[dirty_tx % TX_RING_SIZE];
		}
#endif

		/* Log errors and other uncommon events. */
		if (intr_status & 0x2ee)	/* Abnormal error summary. */
			yellowfin_error(dev, intr_status);

		if (--boguscnt < 0) {
			printk(KERN_WARNING "%s: Too much work at interrupt, status=0x%4.4x.\n",
				   dev->name, intr_status);
			break;
		}
	} while (1);

	if (yellowfin_debug > 3)
		printk(KERN_DEBUG "%s: exiting interrupt, status=%#4.4x.\n",
			   dev->name, inw(ioaddr + IntrStatus));

	/* Code that should never be run!  Perhaps remove after testing.. */
	{
		static int stopit = 10;
		if (dev->start == 0  &&  --stopit < 0) {
			printk(KERN_ERR "%s: Emergency stop, looping startup interrupt.\n",
				   dev->name);
			free_irq(irq, dev);
		}
	}

	dev->interrupt = 0;
	clear_bit(0, (void*)&yp->in_interrupt);
	return;
}

/* This routine is logically part of the interrupt handler, but separated
   for clarity and better register allocation. */
static int yellowfin_rx(struct device *dev)
{
	struct yellowfin_private *yp = (struct yellowfin_private *)dev->priv;
	int entry = yp->cur_rx % RX_RING_SIZE;
	int boguscnt = 20;

	if (yellowfin_debug > 4) {
		printk(KERN_DEBUG " In yellowfin_rx(), entry %d status %4.4x.\n",
			   entry, yp->rx_ring[entry].status);
		printk(KERN_DEBUG "   #%d desc. %4.4x %4.4x %8.8x %4.4x %4.4x.\n",
			   entry, yp->rx_ring[entry].cmd,
			   yp->rx_ring[entry].request_cnt, yp->rx_ring[entry].addr,
			   yp->rx_ring[entry].result_cnt, yp->rx_ring[entry].status);
	}

	/* If EOP is set on the next entry, it's a new packet. Send it up. */
	while (yp->rx_head_desc->status) {
		struct yellowfin_desc *desc = yp->rx_head_desc;
		u16 desc_status = desc->status;
		int data_size = desc->request_cnt - desc->result_cnt;
		u8 *buf_addr = bus_to_virt(desc->addr);
		s16 frame_status = *(s16*)&(buf_addr[data_size - 2]); /* ?Alpha safe on 885? */

		if (yellowfin_debug > 4)
			printk(KERN_DEBUG "  yellowfin_rx() status was %4.4x.\n",
				   frame_status);
		if (--boguscnt < 0)
			break;
		if ( ! (desc_status & RX_EOP)) {
			printk(KERN_WARNING "%s: Oversized Ethernet frame spanned multiple buffers,"
				   " status %4.4x!\n", dev->name, desc_status);
			yp->stats.rx_length_errors++;
		} else if (yp->chip_id == 0  &&  (frame_status & 0x0038)) {
			/* There was a error. */
			if (yellowfin_debug > 3)
				printk(KERN_DEBUG "  yellowfin_rx() Rx error was %4.4x.\n",
					   frame_status);
			yp->stats.rx_errors++;
			if (frame_status & 0x0060) yp->stats.rx_length_errors++;
			if (frame_status & 0x0008) yp->stats.rx_frame_errors++;
			if (frame_status & 0x0010) yp->stats.rx_crc_errors++;
			if (frame_status < 0) yp->stats.rx_dropped++;
		} else if (yp->chip_id != 0  &&
				   ((buf_addr[data_size-1] & 0x85) || buf_addr[data_size-2] & 0xC0)) {
			u8 status1 = buf_addr[data_size-2];
			u8 status2 = buf_addr[data_size-1];
			yp->stats.rx_errors++;
			if (status1 & 0xC0) yp->stats.rx_length_errors++;
			if (status2 & 0x03) yp->stats.rx_frame_errors++;
			if (status2 & 0x04) yp->stats.rx_crc_errors++;
			if (status2 & 0x80) yp->stats.rx_dropped++;
#ifdef YF_PROTOTYPE			/* Support for prototype hardware errata. */
		} else if (memcmp(bus_to_virt(yp->rx_ring[entry].addr),
						  dev->dev_addr, 6) != 0
				   && memcmp(bus_to_virt(yp->rx_ring[entry].addr),
							 "\377\377\377\377\377\377", 6) != 0) {
			printk(KERN_WARNING "%s: Bad frame to %2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x.\n",
				   dev->name, buf_addr[0], buf_addr[1], buf_addr[2],
				   buf_addr[3], buf_addr[4], buf_addr[5]);
			bogus_rx++;
#endif
		} else {
			struct sk_buff *skb;
			int pkt_len = data_size -
				(yp->chip_id ? 7 : 8 + buf_addr[data_size - 8]);
			/* To verify: Yellowfin Length should omit the CRC! */

#ifndef final_version
			if (yellowfin_debug > 4)
				printk(KERN_DEBUG "  yellowfin_rx() normal Rx pkt length %d"
					   " of %d, bogus_cnt %d.\n",
					   pkt_len, data_size, boguscnt);
#endif
			/* Check if the packet is long enough to just pass up the skbuff
			   without copying to a properly sized skbuff. */
			if (pkt_len > rx_copybreak) {
				char *temp = skb_put(skb = yp->rx_skbuff[entry], pkt_len);
#ifndef final_verison				/* Remove after testing. */
				if (bus_to_virt(yp->rx_ring[entry].addr) != temp)
					printk(KERN_WARNING "%s: Warning -- the skbuff addresses "
						   "do not match in yellowfin_rx: %p vs. %p / %p.\n",
						   dev->name, bus_to_virt(yp->rx_ring[entry].addr),
						   skb->head, temp);
#endif
				yp->rx_skbuff[entry] = NULL;
			} else {
				skb = dev_alloc_skb(pkt_len + 2);
				if (skb == NULL)
					break;
				skb->dev = dev;
				skb_reserve(skb, 2);	/* 16 byte align the data fields */
#if 1
				eth_copy_and_sum(skb, bus_to_virt(yp->rx_ring[entry].addr),
								 pkt_len, 0);
				skb_put(skb, pkt_len);
#else
				memcpy(skb_put(skb, pkt_len),
					   bus_to_virt(yp->rx_ring[entry].addr), pkt_len);
#endif
			}
			skb->protocol = eth_type_trans(skb, dev);
			netif_rx(skb);
			dev->last_rx = jiffies;
			yp->stats.rx_packets++;
		}
		entry = (++yp->cur_rx) % RX_RING_SIZE;
		yp->rx_head_desc = &yp->rx_ring[entry];
	}

	/* Refill the Rx ring buffers. */
	for (; yp->cur_rx - yp->dirty_rx > 0; yp->dirty_rx++) {
		struct sk_buff *skb;
		entry = yp->dirty_rx % RX_RING_SIZE;
		if (yp->rx_skbuff[entry] == NULL) {
			skb = dev_alloc_skb(yp->rx_buf_sz);
			if (skb == NULL)
				break;			/* Better luck next round. */
			skb->dev = dev;			/* Mark as being used by this device. */
			skb_reserve(skb, 2);	/* Align IP on 16 byte boundaries */
			yp->rx_ring[entry].addr = virt_to_bus(skb->tail);
			yp->rx_skbuff[entry] = skb;
		}
		yp->rx_ring[entry].cmd = CMD_STOP;
		yp->rx_ring[entry].status = 0;	/* Clear complete bit. */
		if (entry != 0)
			yp->rx_ring[entry - 1].cmd = CMD_RX_BUF | INTR_ALWAYS;
		else
			yp->rx_ring[RX_RING_SIZE - 1].cmd =
				CMD_RX_BUF | INTR_ALWAYS | BRANCH_ALWAYS;
	}

	return 0;
}

static void yellowfin_error(struct device *dev, int intr_status)
{
	struct yellowfin_private *yp = (struct yellowfin_private *)dev->priv;

	printk(KERN_ERR "%s: Something Wicked happened! %4.4x.\n",
		   dev->name, intr_status);
	/* Hmmmmm, it's not clear what to do here. */
	if (intr_status & (IntrTxPCIErr | IntrTxPCIFault))
		yp->stats.tx_errors++;
	if (intr_status & (IntrRxPCIErr | IntrRxPCIFault))
		yp->stats.rx_errors++;
}

static int yellowfin_close(struct device *dev)
{
	long ioaddr = dev->base_addr;
	struct yellowfin_private *yp = (struct yellowfin_private *)dev->priv;
	int i;

	dev->start = 0;
	dev->tbusy = 1;

	if (yellowfin_debug > 1) {
		printk(KERN_DEBUG "%s: Shutting down ethercard, status was Tx %4.4x Rx %4.4x Int %2.2x.\n",
			   dev->name, inw(ioaddr + TxStatus),
			   inw(ioaddr + RxStatus), inw(ioaddr + IntrStatus));
		printk(KERN_DEBUG "%s: Queue pointers were Tx %d / %d,  Rx %d / %d.\n",
			   dev->name, yp->cur_tx, yp->dirty_tx, yp->cur_rx, yp->dirty_rx);
	}

	/* Disable interrupts by clearing the interrupt mask. */
	outw(0x0000, ioaddr + IntrEnb);

	/* Stop the chip's Tx and Rx processes. */
	outl(0x80000000, ioaddr + RxCtrl);
	outl(0x80000000, ioaddr + TxCtrl);

	del_timer(&yp->timer);

#ifdef __i386__
	if (yellowfin_debug > 2) {
		printk("\n"KERN_DEBUG"  Tx ring at %8.8x:\n", (int)virt_to_bus(yp->tx_ring));
		for (i = 0; i < TX_RING_SIZE*2; i++)
			printk(" %c #%d desc. %4.4x %4.4x %8.8x %8.8x %4.4x %4.4x.\n",
				   inl(ioaddr + TxPtr) == (long)&yp->tx_ring[i] ? '>' : ' ',
				   i, yp->tx_ring[i].cmd,
				   yp->tx_ring[i].request_cnt, yp->tx_ring[i].addr,
				   yp->tx_ring[i].branch_addr,
				   yp->tx_ring[i].result_cnt, yp->tx_ring[i].status);
		printk(KERN_DEBUG "  Tx status %p:\n", yp->tx_status);
		for (i = 0; i < TX_RING_SIZE; i++)
			printk("   #%d status %4.4x %4.4x %4.4x %4.4x.\n",
				   i, yp->tx_status[i].tx_cnt, yp->tx_status[i].tx_errs,
				   yp->tx_status[i].total_tx_cnt, yp->tx_status[i].paused);

		printk("\n"KERN_DEBUG "  Rx ring %8.8x:\n", (int)virt_to_bus(yp->rx_ring));
		for (i = 0; i < RX_RING_SIZE; i++) {
			printk(KERN_DEBUG " %c #%d desc. %4.4x %4.4x %8.8x %4.4x %4.4x\n",
				   inl(ioaddr + RxPtr) == (long)&yp->rx_ring[i] ? '>' : ' ',
				   i, yp->rx_ring[i].cmd,
				   yp->rx_ring[i].request_cnt, yp->rx_ring[i].addr,
				   yp->rx_ring[i].result_cnt, yp->rx_ring[i].status);
			if (yellowfin_debug > 6) {
				if (*(u8*)yp->rx_ring[i].addr != 0x69) {
					int j;
					for (j = 0; j < 0x50; j++)
						printk(" %4.4x", ((u16*)yp->rx_ring[i].addr)[j]);
					printk("\n");
				}
			}
		}
	}
#endif /* __i386__ debugging only */

	free_irq(dev->irq, dev);

	/* Free all the skbuffs in the Rx queue. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		yp->rx_ring[i].cmd = CMD_STOP;
		yp->rx_ring[i].addr = 0xBADF00D0; /* An invalid address. */
		if (yp->rx_skbuff[i]) {
#if LINUX_VERSION_CODE < 0x20100
			yp->rx_skbuff[i]->free = 1;
#endif
			DEV_FREE_SKB(yp->rx_skbuff[i]);
		}
		yp->rx_skbuff[i] = 0;
	}
	for (i = 0; i < TX_RING_SIZE; i++) {
		if (yp->tx_skbuff[i])
			DEV_FREE_SKB(yp->tx_skbuff[i]);
		yp->tx_skbuff[i] = 0;
	}

#ifdef YF_PROTOTYPE			/* Support for prototype hardware errata. */
	if (yellowfin_debug > 0) {
		printk(KERN_DEBUG "%s: Received %d frames that we should not have.\n",
			   dev->name, bogus_rx);
	}
#endif
	MOD_DEC_USE_COUNT;

	return 0;
}

static struct enet_statistics *yellowfin_get_stats(struct device *dev)
{
	struct yellowfin_private *yp = (struct yellowfin_private *)dev->priv;
	return &yp->stats;
}

/* Set or clear the multicast filter for this adaptor. */

/* The little-endian AUTODIN32 ethernet CRC calculation.
   N.B. Do not use for bulk data, use a table-based routine instead.
   This is common code and should be moved to net/core/crc.c */
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


static void set_rx_mode(struct device *dev)
{
	struct yellowfin_private *yp = (struct yellowfin_private *)dev->priv;
	long ioaddr = dev->base_addr;
	u16 cfg_value = inw(ioaddr + Cnfg);

	/* Stop the Rx process to change any value. */
	outw(cfg_value & ~0x1000, ioaddr + Cnfg);
	if (dev->flags & IFF_PROMISC) {			/* Set promiscuous. */
		/* Unconditionally log net taps. */
		printk(KERN_NOTICE "%s: Promiscuous mode enabled.\n", dev->name);
		outw(0x000F, ioaddr + AddrMode);
	} else if ((dev->mc_count > 64)  ||  (dev->flags & IFF_ALLMULTI)) {
		/* Too many to filter well, or accept all multicasts. */
		outw(0x000B, ioaddr + AddrMode);
	} else if (dev->mc_count > 0) { /* Must use the multicast hash table. */
		struct dev_mc_list *mclist;
		u16 hash_table[4];
		int i;
		memset(hash_table, 0, sizeof(hash_table));
		for (i = 0, mclist = dev->mc_list; mclist && i < dev->mc_count;
			 i++, mclist = mclist->next) {
			/* Due to a bug in the early chip versions, multiple filter
			   slots must be set for each address. */
			if (yp->chip_id == 0) {
				set_bit((ether_crc_le(3, mclist->dmi_addr) >> 3) & 0x3f,
						hash_table);
				set_bit((ether_crc_le(4, mclist->dmi_addr) >> 3) & 0x3f,
						hash_table);
				set_bit((ether_crc_le(5, mclist->dmi_addr) >> 3) & 0x3f,
						hash_table);
			}
			set_bit((ether_crc_le(6, mclist->dmi_addr) >> 3) & 0x3f,
					hash_table);
		}
		/* Copy the hash table to the chip. */
		for (i = 0; i < 4; i++)
			outw(hash_table[i], ioaddr + HashTbl + i*2);
		outw(0x0003, ioaddr + AddrMode);
	} else {					/* Normal, unicast/broadcast-only mode. */
		outw(0x0001, ioaddr + AddrMode);
	}
	/* Restart the Rx process. */
	outw(cfg_value | 0x1000, ioaddr + Cnfg);
}

#ifdef HAVE_PRIVATE_IOCTL
static int mii_ioctl(struct device *dev, struct ifreq *rq, int cmd)
{
	long ioaddr = dev->base_addr;
	u16 *data = (u16 *)&rq->ifr_data;

	switch(cmd) {
	case SIOCDEVPRIVATE:		/* Get the address of the PHY in use. */
		data[0] = ((struct yellowfin_private *)dev->priv)->phys[0] & 0x1f;
		/* Fall Through */
	case SIOCDEVPRIVATE+1:		/* Read the specified MII register. */
		data[3] = mdio_read(ioaddr, data[0] & 0x1f, data[1] & 0x1f);
		return 0;
	case SIOCDEVPRIVATE+2:		/* Write the specified MII register */
		if (!suser())
			return -EPERM;
		mdio_write(ioaddr, data[0] & 0x1f, data[1] & 0x1f, data[2]);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}
#endif  /* HAVE_PRIVATE_IOCTL */


#ifdef MODULE

/* An additional parameter that may be passed in... */
static int debug = -1;

int init_module(void)
{
	if (debug >= 0)
		yellowfin_debug = debug;

	return yellowfin_probe(0);
}

void cleanup_module(void)
{
	struct device *next_dev;

	/* No need to check MOD_IN_USE, as sys_delete_module() checks. */
	while (root_yellowfin_dev) {
		next_dev = ((struct yellowfin_private *)root_yellowfin_dev->priv)->next_module;
		unregister_netdev(root_yellowfin_dev);
		release_region(root_yellowfin_dev->base_addr, YELLOWFIN_TOTAL_SIZE);
		kfree(root_yellowfin_dev);
		root_yellowfin_dev = next_dev;
	}
}

#endif  /* MODULE */

/*
 * Local variables:
 *  compile-command: "gcc -DMODULE -D__KERNEL__ -I/usr/src/linux/net/inet -Wall -Wstrict-prototypes -O6 -c yellowfin.c `[ -f /usr/include/linux/modversions.h ] && echo -DMODVERSIONS`"
 *  compile-command-alphaLX: "gcc -DMODULE -D__KERNEL__ -I/usr/src/linux/net/inet -Wall -Wstrict-prototypes -O2 -c yellowfin.c `[ -f /usr/include/linux/modversions.h ] && echo -DMODVERSIONS`  -fomit-frame-pointer -fno-strength-reduce -mno-fp-regs -Wa,-m21164a -DBWX_USABLE -DBWIO_ENABLED"
 *  SMP-compile-command: "gcc -D__SMP__ -DMODULE -D__KERNEL__ -I/usr/src/linux/net/inet -Wall -Wstrict-prototypes -O6 -c yellowfin.c `[ -f /usr/include/linux/modversions.h ] && echo -DMODVERSIONS`"
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 * End:
 */
