/* yellowfin.c: A Packet Engines G-NIC ethernet driver for linux. */
/*
	Written 1997 by Donald Becker.

	This software may be used and distributed according to the terms
	of the GNU Public License, incorporated herein by reference.

	This driver is for the Packet Engines G-NIC PCI Gigabit Ethernet adapter.

	The author may be reached as becker@CESDIS.gsfc.nasa.gov, or C/O
	Center of Excellence in Space Data and Information Sciences
	   Code 930.5, Goddard Space Flight Center, Greenbelt MD 20771

	Support and updates available at
	http://cesdis.gsfc.nasa.gov/linux/drivers/yellowfin.html
*/

static const char *version = "yellowfin.c:v0.10 12/5/97 becker@cesdis.gsfc.nasa.gov\n";

/* A few user-configurable values. */

static int max_interrupt_work = 20;
static int min_pci_latency = 64;
static int mtu = 0;
#ifdef YF_PROTOTYPE			/* Support for prototype hardware errata. */
/* System-wide count of bogus-rx frames. */
static int bogus_rx = 0;
static int dma_ctrl = 0x004A0263; 			/* Constrained by errata */
static int fifo_cfg = 0x0020;				/* Bypass external Tx FIFO. */
#elif YF_NEW
static int dma_ctrl = 0x00CAC277;			/* Override when loading module! */
static int fifo_cfg = 0x0028;
#else
static int dma_ctrl = 0x004A0263; 			/* Constrained by errata */
static int fifo_cfg = 0x0020;				/* Bypass external Tx FIFO. */
#endif

/* Set the copy breakpoint for the copy-only-tiny-frames scheme.
   Setting to > 1518 effectively disables this feature. */
static const rx_copybreak = 100;

/* Keep the ring sizes a power of two for efficiency.
   Making the Tx ring too large decreases the effectiveness of channel
   bonding and packet priority.
   There are no ill effects from too-large receive rings. */
#define TX_RING_SIZE	16
#define RX_RING_SIZE	32

/* Operational parameters that usually are not changed. */
/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT  ((2000*HZ)/1000)

#include <linux/config.h>
#ifdef MODULE
#ifdef MODVERSIONS
#include <linux/modversions.h>
#endif
#include <linux/module.h>
#include <linux/version.h>
#else
#define MOD_INC_USE_COUNT
#define MOD_DEC_USE_COUNT
#endif

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

/* Kernel compatibility defines, common to David Hind's PCMCIA package.
   This is only in the support-all-kernels source code. */
#include <linux/version.h>		/* Evil, but neccessary */

#if defined (LINUX_VERSION_CODE) && LINUX_VERSION_CODE < 0x10300
#define RUN_AT(x) (x)			/* What to put in timer->expires.  */
#define DEV_ALLOC_SKB(len) alloc_skb(len, GFP_ATOMIC)
#define virt_to_bus(addr)  ((unsigned long)addr)
#define bus_to_virt(addr) ((void*)addr)

#else  /* 1.3.0 and later */
#define RUN_AT(x) (jiffies + (x))
#define DEV_ALLOC_SKB(len) dev_alloc_skb(len + 2)
#endif

#if defined (LINUX_VERSION_CODE) && LINUX_VERSION_CODE < 0x10338
#ifdef MODULE
#if !defined(CONFIG_MODVERSIONS) && !defined(__NO_VERSION__)
char kernel_version[] = UTS_RELEASE;
#endif
#else
#undef MOD_INC_USE_COUNT
#define MOD_INC_USE_COUNT
#undef MOD_DEC_USE_COUNT
#define MOD_DEC_USE_COUNT
#endif
#endif /* 1.3.38 */

#if (LINUX_VERSION_CODE >= 0x10344)
#define NEW_MULTICAST
#include <linux/delay.h>
#endif
#if (LINUX_VERSION_CODE >= 0x20100)
#ifdef MODULE
char kernel_version[] = UTS_RELEASE;
#endif
#endif
#ifdef SA_SHIRQ
#define IRQ(irq, dev_id, pt_regs) (irq, dev_id, pt_regs)
#else
#define IRQ(irq, dev_id, pt_regs) (irq, pt_regs)
#endif
#if (LINUX_VERSION_CODE < 0x20123)
#define test_and_set_bit(val, addr) set_bit(val, addr)
#include <linux/bios32.h>
#endif

static const char *card_name = "Yellowfin G-NIC Gbit Ethernet";

/* The PCI I/O space extent. */
#define YELLOWFIN_TOTAL_SIZE 0x100

#ifdef HAVE_DEVLIST
struct netdev_entry yellowfin_drv =
{card_name, yellowfin_pci_probe, YELLOWFIN_TOTAL_SIZE, NULL};
#endif

#ifdef YELLOWFIN_DEBUG
int yellowfin_debug = YELLOWFIN_DEBUG;
#else
int yellowfin_debug = 1;
#endif

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

IVb. References

Yellowfin Engineering Design Specification, 4/23/97 Preliminary/Confidential
http://cesdis.gsfc.nasa.gov/linux/misc/100mbps.html

IVc. Errata

See Packet Engines confidential appendix.

*/

/* A few values that may be tweaked. */
#define PKT_BUF_SZ		1536			/* Size of each temporary Rx buffer.*/

#ifndef PCI_VENDOR_ID_PKT_ENG		/* To be defined in linux/pci.h */
#define PCI_VENDOR_ID_PKT_ENG			0x1000 /* Hmm, likely number.. */
#define PCI_DEVICE_ID_YELLOWFIN			0x0702
#endif

/* The rest of these values should never change. */

static void yellowfin_timer(unsigned long data);

/* Offsets to the Yellowfin registers.  Various sizes and alignments. */
enum yellowfin_offsets {
	TxCtrl=0x00, TxStatus=0x04, TxPtr=0x0C,
	TxIntrSel=0x10, TxBranchSel=0x14, TxWaitSel=0x18,
	RxCtrl=0x40, RxStatus=0x44, RxPtr=0x4C,
	RxIntrSel=0x50, RxBranchSel=0x54, RxWaitSel=0x58,
	EventStatus=0x80, IntrEnb=0x82, IntrClear=0x84, IntrStatus=0x86,
	ChipRev=0x8C, DMACtrl=0x90, Cnfg=0xA0, RxDepth=0xB8, FlowCtrl=0xBC,
	AddrMode=0xD0, StnAddr=0xD2, HashTbl=0xD8, FIFOcfg=0xF8,
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
	/* The saved address of a sent-in-place packet/buffer, for skfree(). */
	struct sk_buff* tx_skbuff[TX_RING_SIZE];
	struct tx_status_words tx_status[TX_RING_SIZE];
	/* The addresses of receive-in-place skbuffs. */
	struct sk_buff* rx_skbuff[RX_RING_SIZE];
	int chip_id;
	struct enet_statistics stats;
	struct timer_list timer;	/* Media selection timer. */
	unsigned int cur_rx, cur_tx;		/* The next free ring entry */
	unsigned int dirty_rx, dirty_tx;	/* The ring entries to be free()ed. */
	unsigned int tx_full:1;				/* The Tx queue is full. */
	unsigned int full_duplex:1;			/* Full-duplex operation requested. */
	unsigned int medialock:1;			/* Do not sense media. */
	unsigned int default_port:4;		/* Last dev->if_port value. */
	u32 pad[4];							/* Used for 32-byte alignment */
};

#ifdef MODULE
/* Used to pass the media type, etc. */
#define MAX_UNITS 8				/* More are supported, limit only on options */
static int options[MAX_UNITS] = {-1, -1, -1, -1, -1, -1, -1, -1};

#if LINUX_VERSION_CODE > 0x20115
MODULE_AUTHOR("Donald Becker <becker@cesdis.gsfc.nasa.gov>");
MODULE_DESCRIPTION("Packet Engines Yellowfin G-NIC Gigabit Ethernet driver");
MODULE_PARM(max_interrupt_work, "i");
MODULE_PARM(min_pci_latency, "i");
MODULE_PARM(mtu, "i");
MODULE_PARM(debug, "i");
MODULE_PARM(rx_copybreak, "i");
MODULE_PARM(options, "1-" __MODULE_STRING(MAX_UNITS) "i");
#endif

#endif

static struct device *yellowfin_probe1(struct device *dev, int ioaddr, int irq,
								   int chip_id, int options);
static int yellowfin_open(struct device *dev);
static void yellowfin_timer(unsigned long data);
static void yellowfin_tx_timeout(struct device *dev);
static void yellowfin_init_ring(struct device *dev);
static int yellowfin_start_xmit(struct sk_buff *skb, struct device *dev);
static int yellowfin_rx(struct device *dev);
static void yellowfin_interrupt IRQ(int irq, void *dev_instance, struct pt_regs *regs);
static int yellowfin_close(struct device *dev);
static struct enet_statistics *yellowfin_get_stats(struct device *dev);
#ifdef NEW_MULTICAST
static void set_rx_mode(struct device *dev);
#else
static void set_rx_mode(struct device *dev, int num_addrs, void *addrs);
#endif



#ifdef MODULE
/* A list of all installed Yellowfin devices, for removing the driver module. */
static struct device *root_yellowfin_dev = NULL;
#endif

int yellowfin_probe(struct device *dev)
{
	int cards_found = 0;
	static int pci_index = 0;	/* Static, for multiple probe calls. */

	/* Ideally we would detect all network cards in slot order.  That would
	   be best done a central PCI probe dispatch, which wouldn't work
	   well with the current structure.  So instead we detect just the
	   Yellowfin cards in slot order. */

	if (pci_present()) {
		unsigned char pci_bus, pci_device_fn;

		for (;pci_index < 0xff; pci_index++) {
#if LINUX_VERSION_CODE >= 0x20155
			unsigned int pci_irq_line;
			struct pci_dev *pdev;
#else
			unsigned char pci_irq_line;
#endif
			unsigned char pci_latency;
			unsigned short pci_command, vendor, device;
			unsigned int pci_ioaddr, chip_idx = 0;

#ifdef REVERSE_PROBE_ORDER
			if (pcibios_find_class (PCI_CLASS_NETWORK_ETHERNET << 8,
									0xfe - pci_index,
									&pci_bus, &pci_device_fn)
				!= PCIBIOS_SUCCESSFUL)
				continue;
#else
			if (pcibios_find_class (PCI_CLASS_NETWORK_ETHERNET << 8,
									pci_index,
									&pci_bus, &pci_device_fn)
				!= PCIBIOS_SUCCESSFUL)
				break;
#endif
			pcibios_read_config_word(pci_bus, pci_device_fn,
									 PCI_VENDOR_ID, &vendor);
			pcibios_read_config_word(pci_bus, pci_device_fn,
									 PCI_DEVICE_ID, &device);
#if LINUX_VERSION_CODE >= 0x20155
			pdev = pci_find_slot(pci_bus, pci_device_fn);
			pci_irq_line = pdev->irq;
			pci_ioaddr = pdev->base_address[0];
#else
			pcibios_read_config_byte(pci_bus, pci_device_fn,
									 PCI_INTERRUPT_LINE, &pci_irq_line);
			pcibios_read_config_dword(pci_bus, pci_device_fn,
									  PCI_BASE_ADDRESS_0, &pci_ioaddr);
#endif
			/* Remove I/O space marker in bit 0. */
			pci_ioaddr &= ~3;

			if (vendor != PCI_VENDOR_ID_PKT_ENG)
				continue;

			if (device != PCI_DEVICE_ID_YELLOWFIN)
				continue;

			if (yellowfin_debug > 2)
				printk("Found Packet Engines Yellowfin G-NIC at I/O %#x, IRQ %d.\n",
					   pci_ioaddr, pci_irq_line);

			if (check_region(pci_ioaddr, YELLOWFIN_TOTAL_SIZE))
				continue;

#ifdef MODULE
			dev = yellowfin_probe1(dev, pci_ioaddr, pci_irq_line, chip_idx,
						 cards_found < MAX_UNITS ? options[cards_found] : 0);
#else
			dev = yellowfin_probe1(dev, pci_ioaddr, pci_irq_line, chip_idx,
						 dev ? dev->mem_start : 0);
#endif

			if (dev) {
			  /* Get and check the bus-master and latency values. */
			  pcibios_read_config_word(pci_bus, pci_device_fn,
									   PCI_COMMAND, &pci_command);
			  if ( ! (pci_command & PCI_COMMAND_MASTER)) {
				printk("  PCI Master Bit has not been set! Setting...\n");
				pci_command |= PCI_COMMAND_MASTER;
				pcibios_write_config_word(pci_bus, pci_device_fn,
										  PCI_COMMAND, pci_command);
			  }
			  pcibios_read_config_byte(pci_bus, pci_device_fn,
									   PCI_LATENCY_TIMER, &pci_latency);
			  if (pci_latency < min_pci_latency) {
				printk("  PCI latency timer (CFLT) is unreasonably low at %d."
					   "  Setting to %d clocks.\n",
					   pci_latency, min_pci_latency);
				pcibios_write_config_byte(pci_bus, pci_device_fn,
										  PCI_LATENCY_TIMER, min_pci_latency);
			  } else if (yellowfin_debug > 1)
				printk("  PCI latency timer (CFLT) is %#x.\n", pci_latency);
			  dev = 0;
			  cards_found++;
			}
		}
	}

#if defined (MODULE)
	return cards_found;
#else
	return 0;
#endif
}

static struct device *yellowfin_probe1(struct device *dev, int ioaddr, int irq,
								   int chip_id, int options)
{
	static int did_version = 0;			/* Already printed version info. */
	struct yellowfin_private *yp;
	int i;

	if (yellowfin_debug > 0  &&  did_version++ == 0)
		printk(version);

	dev = init_etherdev(dev, sizeof(struct yellowfin_private));

	printk("%s: P-E Yellowfin type %8x at %#3x, ",
		   dev->name, inl(ioaddr + ChipRev), ioaddr);

	for (i = 0; i < 5; i++)
		printk("%2.2x:", inb(ioaddr + StnAddr + i));
	printk("%2.2x, IRQ %d.\n", inb(ioaddr + StnAddr + i), irq);
	for (i = 0; i < 6; i++)
		dev->dev_addr[i] = inb(ioaddr + StnAddr + i);

	/* Reset the chip. */
	outl(0x80000000, ioaddr + DMACtrl);


	/* We do a request_region() only to register /proc/ioports info. */
	request_region(ioaddr, YELLOWFIN_TOTAL_SIZE, card_name);

	dev->base_addr = ioaddr;
	dev->irq = irq;

	/* Make certain the descriptor lists are aligned. */
	yp = (void *)(((long)kmalloc(sizeof(*yp), GFP_KERNEL) + 31) & ~31);
	memset(yp, 0, sizeof(*yp));
	dev->priv = yp;

#ifdef MODULE
	yp->next_module = root_yellowfin_dev;
	root_yellowfin_dev = dev;
#endif

	yp->chip_id = chip_id;

	yp->full_duplex = 1;
#ifdef YELLOWFIN_DEFAULT_MEDIA
	yp->default_port = YELLOWFIN_DEFAULT_MEDIA;
#endif
#ifdef YELLOWFIN_NO_MEDIA_SWITCH
	yp->medialock = 1;
#endif

	/* The lower four bits are the media type. */
	if (options > 0) {
		yp->full_duplex = (options & 16) ? 1 : 0;
		yp->default_port = options & 15;
		if (yp->default_port)
			yp->medialock = 1;
	}

	/* The Yellowfin-specific entries in the device structure. */
	dev->open = &yellowfin_open;
	dev->hard_start_xmit = &yellowfin_start_xmit;
	dev->stop = &yellowfin_close;
	dev->get_stats = &yellowfin_get_stats;
	dev->set_multicast_list = &set_rx_mode;
	if (mtu)
		dev->mtu = mtu;

	/* todo: Reset the xcvr interface and turn on heartbeat. */

	return dev;
}


static int
yellowfin_open(struct device *dev)
{
	struct yellowfin_private *yp = (struct yellowfin_private *)dev->priv;
	int ioaddr = dev->base_addr;

	/* Reset the chip. */
	outl(0x80000000, ioaddr + DMACtrl);

#ifdef SA_SHIRQ
	if (request_irq(dev->irq, &yellowfin_interrupt, SA_SHIRQ,
					card_name, dev)) {
		return -EAGAIN;
	}
#else
	if (irq2dev_map[dev->irq] != NULL
		|| (irq2dev_map[dev->irq] = dev) == NULL
		|| dev->irq == 0
		|| request_irq(dev->irq, &yellowfin_interrupt, 0, card_name)) {
		return -EAGAIN;
	}
#endif

	if (yellowfin_debug > 1)
		printk("%s: yellowfin_open() irq %d.\n", dev->name, dev->irq);

	MOD_INC_USE_COUNT;

	yellowfin_init_ring(dev);

	outl(virt_to_bus(yp->rx_ring), ioaddr + RxPtr);
	outl(virt_to_bus(yp->tx_ring), ioaddr + TxPtr);

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

	/* We are always in full-duplex mode with the current chip! */
	yp->full_duplex = 1;

	/* Setting the Rx mode will start the Rx process. */
	outw(0x01CD | (yp->full_duplex ? 2 : 0), ioaddr + Cnfg);
#ifdef NEW_MULTICAST
	set_rx_mode(dev);
#else
	set_rx_mode(dev, 0, 0);
#endif

	dev->start = 1;

	/* Enable interrupts by setting the interrupt mask. */
	outw(0x81ff, ioaddr + IntrEnb);			/* See enum intr_status_bits */
	outw(0x0000, ioaddr + EventStatus);		/* Clear non-interrupting events */
	outl(0x80008000, ioaddr + RxCtrl);		/* Start Rx and Tx channels. */
	outl(0x80008000, ioaddr + TxCtrl);

	if (yellowfin_debug > 2) {
		printk("%s: Done yellowfin_open().\n",
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
	int ioaddr = dev->base_addr;
	int next_tick = 0;

	if (yellowfin_debug > 3) {
		printk("%s: Yellowfin timer tick, status %8.8x.\n",
			   dev->name, inl(ioaddr + IntrStatus));
	}
	if (next_tick) {
		yp->timer.expires = RUN_AT(next_tick);
		add_timer(&yp->timer);
	}
}

static void yellowfin_tx_timeout(struct device *dev)
{
	struct yellowfin_private *yp = (struct yellowfin_private *)dev->priv;
	int ioaddr = dev->base_addr;
	int i;

	printk("%s: Yellowfin transmit timed out, status %8.8x, resetting...\n",
		   dev->name, inl(ioaddr));

#ifndef __alpha__
  printk("  Rx ring %8.8x: ", (int)yp->rx_ring);
  for (i = 0; i < RX_RING_SIZE; i++)
	printk(" %8.8x", (unsigned int)yp->rx_ring[i].status);
  printk("\n  Tx ring %8.8x: ", (int)yp->tx_ring);
  for (i = 0; i < TX_RING_SIZE; i++)
	printk(" %4.4x /%4.4x", yp->tx_status[i].tx_errs, yp->tx_ring[i].status);
  printk("\n");
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
static void
yellowfin_init_ring(struct device *dev)
{
	struct yellowfin_private *yp = (struct yellowfin_private *)dev->priv;
	int i;

	yp->tx_full = 0;
	yp->cur_rx = yp->cur_tx = 0;
	yp->dirty_rx = yp->dirty_tx = 0;

	for (i = 0; i < RX_RING_SIZE; i++) {
		struct sk_buff *skb;
		int pkt_buf_sz = (dev->mtu <= 1500 ? PKT_BUF_SZ : dev->mtu + 32);

		yp->rx_ring[i].request_cnt = pkt_buf_sz;
		yp->rx_ring[i].cmd = CMD_RX_BUF | INTR_ALWAYS;

		skb = DEV_ALLOC_SKB(pkt_buf_sz);
		skb_reserve(skb, 2);	/* 16 byte align the IP header. */
		yp->rx_skbuff[i] = skb;
		if (skb == NULL)
			break;			/* Bad news!  */
		skb->dev = dev;			/* Mark as being used by this device. */
#if LINUX_VERSION_CODE > 0x10300
		yp->rx_ring[i].addr = virt_to_bus(skb->tail);
#else
		yp->rx_ring[i].addr = virt_to_bus(skb->data);
#endif
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
		yp->tx_ring[i].cmd = CMD_TXSTATUS; /* Interrupt, no wait. */
		yp->tx_ring[i].request_cnt = sizeof(yp->tx_status[i]);
		yp->tx_ring[i].addr = virt_to_bus(&yp->tx_status[i/2]);
		yp->tx_ring[i].branch_addr = virt_to_bus(&yp->tx_ring[i+1]);
	}
	/* Wrap ring */
	yp->tx_ring[--i].cmd = CMD_TXSTATUS | BRANCH_ALWAYS | INTR_ALWAYS;
	yp->tx_ring[i].branch_addr = virt_to_bus(&yp->tx_ring[0]);
#endif
}

static int
yellowfin_start_xmit(struct sk_buff *skb, struct device *dev)
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

	/* Todo: explicitly flush cache lines here. */

	/* Wake the potentially-idle transmit channel. */
	outl(0x10001000, dev->base_addr + TxCtrl);

	if (yp->cur_tx - yp->dirty_tx < TX_RING_SIZE - 1)
		dev->tbusy = 0;					/* Typical path */
	else
		yp->tx_full = 1;
	dev->trans_start = jiffies;

	if (yellowfin_debug > 4) {
		printk("%s: Yellowfin transmit frame #%d queued in slot %d.\n",
			   dev->name, yp->cur_tx, entry);
	}
	return 0;
}

/* The interrupt handler does all of the Rx thread work and cleans up
   after the Tx thread. */
static void yellowfin_interrupt IRQ(int irq, void *dev_instance, struct pt_regs *regs)
{
#ifdef SA_SHIRQ		/* Use the now-standard shared IRQ implementation. */
	struct device *dev = (struct device *)dev_instance;
#else
	struct device *dev = (struct device *)(irq2dev_map[irq]);
#endif

	struct yellowfin_private *lp;
	int ioaddr, boguscnt = max_interrupt_work;

	if (dev == NULL) {
		printk ("yellowfin_interrupt(): irq %d for unknown device.\n", irq);
		return;
	}

	ioaddr = dev->base_addr;
	lp = (struct yellowfin_private *)dev->priv;
	if (test_and_set_bit(0, (void*)&dev->interrupt)) {
		printk(KERN_ERR "%s: Re-entering the interrupt handler.\n", dev->name);
		return;
	}

	do {
		u16 intr_status = inw(ioaddr + IntrClear);
		unsigned dirty_tx = lp->dirty_tx;

		if (yellowfin_debug > 4)
			printk("%s: Yellowfin interrupt, status %4.4x.\n",
				   dev->name, intr_status);

		if (intr_status == 0)
			break;

		if (intr_status & (IntrRxDone | IntrEarlyRx))
			yellowfin_rx(dev);

#ifdef NO_TXSTATS
		for (; dirty_tx < lp->cur_tx; dirty_tx++) {
			int entry = dirty_tx % TX_RING_SIZE;
			if (lp->tx_ring[entry].status == 0)
				break;
			/* Free the original skb. */
			dev_kfree_skb(lp->tx_skbuff[entry]);
			lp->tx_skbuff[entry] = 0;
			lp->stats.tx_packets++;
		}
		if (lp->tx_full && dev->tbusy
			&& dirty_tx > lp->cur_tx - TX_RING_SIZE + 4) {
			/* The ring is no longer full, clear tbusy. */
			lp->tx_full = 0;
			dev->tbusy = 0;
			mark_bh(NET_BH);
		}
		lp->dirty_tx = dirty_tx;
#else
		if (intr_status & IntrTxDone
			|| lp->tx_status[dirty_tx % TX_RING_SIZE].tx_errs) {

			for (dirty_tx = lp->dirty_tx; dirty_tx < lp->cur_tx; dirty_tx++) {
				/* Todo: optimize this. */
				int entry = dirty_tx % TX_RING_SIZE;
				u16 tx_errs = lp->tx_status[entry].tx_errs;

				if (tx_errs == 0)
					break;			/* It still hasn't been Txed */
				if (tx_errs & 0xF8100000) {
					/* There was an major error, log it. */
#ifndef final_version
					if (yellowfin_debug > 1)
						printk("%s: Transmit error, Tx status %4.4x.\n",
							   dev->name, tx_errs);
#endif
					lp->stats.tx_errors++;
					if (tx_errs & 0xF800) lp->stats.tx_aborted_errors++;
					if (tx_errs & 0x0800) lp->stats.tx_carrier_errors++;
					if (tx_errs & 0x2000) lp->stats.tx_window_errors++;
					if (tx_errs & 0x8000) lp->stats.tx_fifo_errors++;
#ifdef ETHER_STATS
					if (tx_errs & 0x1000) lp->stats.collisions16++;
#endif
				} else {
#ifdef ETHER_STATS
					if (status & 0x0400) lp->stats.tx_deferred++;
#endif
					lp->stats.collisions += tx_errs & 15;
					lp->stats.tx_packets++;
				}

				/* Free the original skb. */
				dev_kfree_skb(lp->tx_skbuff[entry]);
				lp->tx_skbuff[entry] = 0;
				/* Mark status as empty. */
				lp->tx_status[entry].tx_errs = 0;
			}

#ifndef final_version
			if (lp->cur_tx - dirty_tx > TX_RING_SIZE) {
				printk("%s: Out-of-sync dirty pointer, %d vs. %d, full=%d.\n",
					   dev->name, dirty_tx, lp->cur_tx, lp->tx_full);
				dirty_tx += TX_RING_SIZE;
			}
#endif

			if (lp->tx_full && dev->tbusy
				&& dirty_tx > lp->cur_tx - TX_RING_SIZE + 2) {
				/* The ring is no longer full, clear tbusy. */
				lp->tx_full = 0;
				dev->tbusy = 0;
				mark_bh(NET_BH);
			}

			lp->dirty_tx = dirty_tx;
		}
#endif

		/* Log errors and other events. */
		if (intr_status & 0x2ee) {	/* Abnormal error summary. */
			printk("%s: Something Wicked happened! %4.4x.\n",
				   dev->name, intr_status);
			/* Hmmmmm, it's not clear what to do here. */
			if (intr_status & (IntrTxPCIErr | IntrTxPCIFault))
				lp->stats.tx_errors++;
			if (intr_status & (IntrRxPCIErr | IntrRxPCIFault))
				lp->stats.rx_errors++;
		}
		if (--boguscnt < 0) {
			printk("%s: Too much work at interrupt, status=0x%4.4x.\n",
				   dev->name, intr_status);
			break;
		}
	} while (1);

	if (yellowfin_debug > 3)
		printk("%s: exiting interrupt, status=%#4.4x.\n",
			   dev->name, inw(ioaddr + IntrStatus));

	/* Code that should never be run!  Perhaps remove after testing.. */
	{
		static int stopit = 10;
		if (dev->start == 0  &&  --stopit < 0) {
			printk("%s: Emergency stop, looping startup interrupt.\n",
				   dev->name);
#ifdef SA_SHIRQ
			free_irq(irq, dev);
#else
			free_irq(irq);
#endif
		}
	}

	dev->interrupt = 0;
	return;
}

/* This routine is logically part of the interrupt handler, but seperated
   for clarity and better register allocation. */
static int
yellowfin_rx(struct device *dev)
{
	struct yellowfin_private *lp = (struct yellowfin_private *)dev->priv;
	struct yellowfin_private *yp = (struct yellowfin_private *)dev->priv;
	int entry = lp->cur_rx % RX_RING_SIZE;
	int boguscnt = 20;

	if (yellowfin_debug > 4) {
		printk(" In yellowfin_rx(), entry %d status %4.4x.\n", entry,
			   yp->rx_ring[entry].status);
		printk("   #%d desc. %4.4x %4.4x %8.8x %4.4x %4.4x.\n",
			   entry, yp->rx_ring[entry].cmd,
			   yp->rx_ring[entry].request_cnt, yp->rx_ring[entry].addr,
			   yp->rx_ring[entry].result_cnt, yp->rx_ring[entry].status);
	}


	/* If EOP is set on the next entry, it's a new packet. Send it up. */
	while (yp->rx_ring[entry].status) {
		/* Todo: optimize this mess. */
		u16 desc_status = yp->rx_ring[entry].status;
		struct yellowfin_desc *desc = &lp->rx_ring[entry];
		int frm_size = desc->request_cnt - desc->result_cnt;
		u8 *buf_addr = bus_to_virt(lp->rx_ring[entry].addr);
		s16 frame_status = *(s16*)&(buf_addr[frm_size - 2]);

		if (yellowfin_debug > 4)
			printk("  yellowfin_rx() status was %4.4x.\n", frame_status);
		if (--boguscnt < 0)
			break;
		if ( ! (desc_status & RX_EOP)) {
			printk("%s: Oversized Ethernet frame spanned multiple buffers,"
				   " status %4.4x!\n", dev->name, desc_status);
			  lp->stats.rx_length_errors++;
		} else if (frame_status & 0x0038) {
			/* There was a error. */
			if (yellowfin_debug > 3)
				printk("  yellowfin_rx() Rx error was %4.4x.\n", frame_status);
			lp->stats.rx_errors++;
			if (frame_status & 0x0060) lp->stats.rx_length_errors++;
			if (frame_status & 0x0008) lp->stats.rx_frame_errors++;
			if (frame_status & 0x0010) lp->stats.rx_crc_errors++;
			if (frame_status < 0) lp->stats.rx_dropped++;
#ifdef YF_PROTOTYPE			/* Support for prototype hardware errata. */
		} else if (memcmp(bus_to_virt(lp->rx_ring[entry].addr),
						  dev->dev_addr, 6) != 0
				   && memcmp(bus_to_virt(lp->rx_ring[entry].addr),
							 "\0377\0377\0377\0377\0377\0377", 6) != 0) {
			printk("%s: Bad frame to %2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x.\n",
				   dev->name,
				   ((char *)bus_to_virt(lp->rx_ring[entry].addr))[0],
				   ((char *)bus_to_virt(lp->rx_ring[entry].addr))[1],
				   ((char *)bus_to_virt(lp->rx_ring[entry].addr))[2],
				   ((char *)bus_to_virt(lp->rx_ring[entry].addr))[3],
				   ((char *)bus_to_virt(lp->rx_ring[entry].addr))[4],
				   ((char *)bus_to_virt(lp->rx_ring[entry].addr))[5]);
			bogus_rx++;
#endif
		} else {
			u8 bogus_cnt = buf_addr[frm_size - 8];
			short pkt_len = frm_size - 8 - bogus_cnt;
			struct sk_buff *skb;
			int rx_in_place = 0;

			/* Check if the packet is long enough to just accept without
			   copying to a properly sized skbuff. */
			if (pkt_len > rx_copybreak) {
				struct sk_buff *newskb;
				char *temp;

				/* Get a fresh skbuff to replace the filled one. */
				newskb = DEV_ALLOC_SKB(dev->mtu <= 1500 ? PKT_BUF_SZ
									   : dev->mtu + 32);
				if (newskb == NULL) {
					skb = 0;		/* No memory, drop the packet. */
					goto memory_squeeze;
				}
				/* Pass up the skb already on the Rx ring. */
				skb = lp->rx_skbuff[entry];
				temp = skb_put(skb, pkt_len);
				if (bus_to_virt(lp->rx_ring[entry].addr) != temp)
					printk("%s: Warning -- the skbuff addresses do not match"
						   " in yellowfin_rx: %p vs. %p / %p.\n", dev->name,
						   bus_to_virt(lp->rx_ring[entry].addr),
						   skb->head, temp);
				rx_in_place = 1;
				lp->rx_skbuff[entry] = newskb;
				newskb->dev = dev;
				skb_reserve(newskb, 2);	/* 16 byte align IP header */
				lp->rx_ring[entry].addr = virt_to_bus(newskb->tail);
			} else
				skb = DEV_ALLOC_SKB(pkt_len + 2);
			memory_squeeze:
			if (skb == NULL) {
				printk("%s: Memory squeeze, deferring packet.\n", dev->name);
				/* todo: Check that at least two ring entries are free.
				   If not, free one and mark stats->rx_dropped++. */
				break;
			}
			skb->dev = dev;
			if (! rx_in_place) {
				skb_reserve(skb, 2);	/* 16 byte align the data fields */
				memcpy(skb_put(skb, pkt_len),
					   bus_to_virt(lp->rx_ring[entry].addr), pkt_len);
			}
#if LINUX_VERSION_CODE > 0x10300
			skb->protocol = eth_type_trans(skb, dev);
#else
			skb->len = pkt_len;
#endif
			netif_rx(skb);
			lp->stats.rx_packets++;
		}

		/* Mark this entry as being the end-of-list, and the prior entry
		   as now valid. */
		lp->rx_ring[entry].cmd = CMD_STOP;
		yp->rx_ring[entry].status = 0;
		{
			int prev_entry = entry - 1;
			if (prev_entry < 0)
				lp->rx_ring[RX_RING_SIZE - 1].cmd =
					CMD_RX_BUF | INTR_ALWAYS | BRANCH_ALWAYS;
			else
				lp->rx_ring[prev_entry].cmd = CMD_RX_BUF | INTR_ALWAYS;
		}
		entry = (++lp->cur_rx) % RX_RING_SIZE;
	}
	/* todo: restart Rx engine if stopped.  For now we just make the Rx ring
	   large enough to avoid this. */

	return 0;
}

static int
yellowfin_close(struct device *dev)
{
	int ioaddr = dev->base_addr;
	struct yellowfin_private *yp = (struct yellowfin_private *)dev->priv;
	int i;

	dev->start = 0;
	dev->tbusy = 1;

	if (yellowfin_debug > 1) {
		printk("%s: Shutting down ethercard, status was Tx %4.4x Rx %4.4x Int %2.2x.\n",
			   dev->name, inw(ioaddr + TxStatus),
			   inw(ioaddr + RxStatus), inl(ioaddr + IntrStatus));
		printk("%s: Queue pointers were Tx %d / %d,  Rx %d / %d.\n",
			   dev->name, yp->cur_tx, yp->dirty_tx, yp->cur_rx, yp->dirty_rx);
	}

	/* Disable interrupts by clearing the interrupt mask. */
	outw(0x0000, ioaddr + IntrEnb);

	/* Stop the chip's Tx and Rx processes. */
	outl(0x80000000, ioaddr + RxCtrl);
	outl(0x80000000, ioaddr + TxCtrl);

	del_timer(&yp->timer);

	if (yellowfin_debug > 2) {
		printk("\n  Tx ring at %8.8x:\n", (int)virt_to_bus(yp->tx_ring));
		for (i = 0; i < TX_RING_SIZE*2; i++)
			printk(" %c #%d desc. %4.4x %4.4x %8.8x %8.8x %4.4x %4.4x.\n",
				   inl(ioaddr + TxPtr) == (long)&yp->tx_ring[i] ? '>' : ' ',
				   i, yp->tx_ring[i].cmd,
				   yp->tx_ring[i].request_cnt, yp->tx_ring[i].addr,
				   yp->tx_ring[i].branch_addr,
				   yp->tx_ring[i].result_cnt, yp->tx_ring[i].status);
		printk("  Tx status %p:\n", yp->tx_status);
		for (i = 0; i < TX_RING_SIZE; i++)
			printk("   #%d status %4.4x %4.4x %4.4x %4.4x.\n",
				   i, yp->tx_status[i].tx_cnt, yp->tx_status[i].tx_errs,
				   yp->tx_status[i].total_tx_cnt, yp->tx_status[i].paused);

		printk("\n  Rx ring %8.8x:\n", (int)virt_to_bus(yp->rx_ring));
		for (i = 0; i < RX_RING_SIZE; i++) {
			printk(" %c #%d desc. %4.4x %4.4x %8.8x %4.4x %4.4x\n",
				   inl(ioaddr + RxPtr) == (long)&yp->rx_ring[i] ? '>' : ' ',
				   i, yp->rx_ring[i].cmd,
				   yp->rx_ring[i].request_cnt, yp->rx_ring[i].addr,
				   yp->rx_ring[i].result_cnt, yp->rx_ring[i].status);
			if (yellowfin_debug > 5) {
				if (*(u8*)yp->rx_ring[i].addr != 0x69) {
					int j;
					for (j = 0; j < 0x50; j++)
						printk(" %4.4x", ((u16*)yp->rx_ring[i].addr)[j]);
					printk("\n");
				}
			}
		}
	}

#ifdef SA_SHIRQ
	free_irq(dev->irq, dev);
#else
	free_irq(dev->irq);
	irq2dev_map[dev->irq] = 0;
#endif

	/* Free all the skbuffs in the Rx queue. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		yp->rx_ring[i].cmd = CMD_STOP;
		yp->rx_ring[i].addr = 0xBADF00D0; /* An invalid address. */
		if (yp->rx_skbuff[i]) {
#if LINUX_VERSION_CODE < 0x20100
			yp->rx_skbuff[i]->free = 1;
#endif
			dev_kfree_skb(yp->rx_skbuff[i]);
		}
		yp->rx_skbuff[i] = 0;
	}
	for (i = 0; i < TX_RING_SIZE; i++) {
		if (yp->tx_skbuff[i])
			dev_kfree_skb(yp->tx_skbuff[i]);
		yp->tx_skbuff[i] = 0;
	}

#ifdef YF_PROTOTYPE			/* Support for prototype hardware errata. */
	if (yellowfin_debug > 0) {
		printk("%s: Received %d frames that we should not have.\n",
			   dev->name, bogus_rx);
	}
#endif
	MOD_DEC_USE_COUNT;

	return 0;
}

static struct enet_statistics *
yellowfin_get_stats(struct device *dev)
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


static void
#ifdef NEW_MULTICAST
set_rx_mode(struct device *dev)
#else
static void set_rx_mode(struct device *dev, int num_addrs, void *addrs);
#endif
{
	int ioaddr = dev->base_addr;
	u16 cfg_value = inw(ioaddr + Cnfg);

	/* Stop the Rx process to change any value. */
	outw(cfg_value & ~0x1000, ioaddr + Cnfg);
	if (dev->flags & IFF_PROMISC) {			/* Set promiscuous. */
		/* Unconditionally log net taps. */
		printk("%s: Promiscuous mode enabled.\n", dev->name);
		outw(0x000F, ioaddr + AddrMode);
	} else if ((dev->mc_count > 64)  ||  (dev->flags & IFF_ALLMULTI)) {
		/* Too many to filter well, or accept all multicasts. */
		printk("%s: Set all-multicast mode.\n", dev->name);
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
			set_bit((ether_crc_le(3, mclist->dmi_addr) >> 3) & 0x3f,
					hash_table);
			set_bit((ether_crc_le(4, mclist->dmi_addr) >> 3) & 0x3f,
					hash_table);
			set_bit((ether_crc_le(5, mclist->dmi_addr) >> 3) & 0x3f,
					hash_table);
			set_bit((ether_crc_le(6, mclist->dmi_addr) >> 3) & 0x3f,
					hash_table);
		}
		/* Copy the hash table to the chip. */
		for (i = 0; i < 4; i++)
			outw(hash_table[i], ioaddr + HashTbl + i*2);
		printk("%s: Set multicast mode.\n", dev->name);
		outw(0x0003, ioaddr + AddrMode);
	} else {					/* Normal, unicast/broadcast-only mode. */
		printk("%s: Set unicast mode.\n", dev->name);
		outw(0x0001, ioaddr + AddrMode);
	}
	/* Restart the Rx process. */
	outw(cfg_value | 0x1000, ioaddr + Cnfg);
}

#ifdef MODULE

/* An additional parameter that may be passed in... */
static int debug = -1;

int
init_module(void)
{
	int cards_found;

	if (debug >= 0)
		yellowfin_debug = debug;

	root_yellowfin_dev = NULL;
	cards_found = yellowfin_probe(0);

	return cards_found ? 0 : -ENODEV;
}

void
cleanup_module(void)
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
 *  compile-command: "gcc -DMODULE -D__KERNEL__ -I/usr/src/linux/net/inet -Wall -Wstrict-prototypes -O6 -c yellowfin.c"
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 * End:
 */
