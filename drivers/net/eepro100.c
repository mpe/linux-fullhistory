/* drivers/net/eepro100.c: An Intel i82557 Ethernet driver for Linux. */
/*
   NOTICE: this version tested with kernels 1.3.72 and later only!
	Written 1996-1998 by Donald Becker.

	This software may be used and distributed according to the terms
	of the GNU Public License, incorporated herein by reference.

	This driver is for the Intel EtherExpress Pro 100B boards.
	It should work with other i82557 and i82558 boards.
	To use a built-in driver, install as drivers/net/eepro100.c.
	To use as a module, use the compile-command at the end of the file.

	The author may be reached as becker@CESDIS.usra.edu, or C/O
	Center of Excellence in Space Data and Information Sciences
	   Code 930.5, NASA Goddard Space Flight Center, Greenbelt MD 20771
	For updates see
		http://cesdis.gsfc.nasa.gov/linux/drivers/eepro100.html
	There is also a mailing list based at
		linux-eepro100@cesdis.gsfc.nasa.gov
*/

static const char *version =
"eepro100.c:v1.06 10/16/98 Donald Becker http://cesdis.gsfc.nasa.gov/linux/drivers/eepro100.html\n";

/* A few user-configurable values that apply to all boards.
   First set are undocumented and spelled per Intel recommendations. */

static int congenb = 0;		/* Enable congestion control in the DP83840. */
static int txfifo = 8;		/* Tx FIFO threshold in 4 byte units, 0-15 */
static int rxfifo = 8;		/* Rx FIFO threshold, default 32 bytes. */
/* Tx/Rx DMA burst length, 0-127, 0 == no preemption, tx==128 -> disabled. */
static int txdmacount = 128;
static int rxdmacount = 0;

/* Set the copy breakpoint for the copy-only-tiny-buffer Rx method.
   Lower values use more memory, but are faster. */
static int rx_copybreak = 200;

/* Maximum events (Rx packets, etc.) to handle at each interrupt. */
static int max_interrupt_work = 200;

/* Maximum number of multicast addresses to filter (vs. rx-all-multicast) */
static int multicast_filter_limit = 64;

#include <linux/module.h>

#include <linux/version.h>
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
#include <linux/delay.h>

#include <asm/spinlock.h>
#include <asm/bitops.h>
#include <asm/io.h>

/*
 * Module documentation
 */
MODULE_AUTHOR("Donald Becker <becker@cesdis.gsfc.nasa.gov>");
MODULE_DESCRIPTION("Intel i82557/i82558 PCI EtherExpressPro driver");
MODULE_PARM(debug, "i");
MODULE_PARM(options, "1-" __MODULE_STRING(8) "i");
MODULE_PARM(full_duplex, "1-" __MODULE_STRING(8) "i");
MODULE_PARM(congenb, "i");
MODULE_PARM(txfifo, "i");
MODULE_PARM(rxfifo, "i");
MODULE_PARM(txdmacount, "i");
MODULE_PARM(rxdmacount, "i");
MODULE_PARM(rx_copybreak, "i");
MODULE_PARM(max_interrupt_work, "i");
MODULE_PARM(multicast_filter_limit, "i");

#define RUN_AT(x) (jiffies + (x))

#define dev_free_skb(skb) dev_kfree_skb(skb);

/* The total I/O port extent of the board.
   The registers beyond 0x18 only exist on the i82558. */
#define SPEEDO3_TOTAL_SIZE 0x20

int speedo_debug = 0;

/*
				Theory of Operation

I. Board Compatibility

This device driver is designed for the Intel i82557 "Speedo3" chip, Intel's
single-chip fast Ethernet controller for PCI, as used on the Intel
EtherExpress Pro 100 adapter.

II. Board-specific settings

PCI bus devices are configured by the system at boot time, so no jumpers
need to be set on the board.  The system BIOS should be set to assign the
PCI INTA signal to an otherwise unused system IRQ line.  While it's
possible to share PCI interrupt lines, it negatively impacts performance and
only recent kernels support it.

III. Driver operation

IIIA. General
The Speedo3 is very similar to other Intel network chips, that is to say
"apparently designed on a different planet".  This chips retains the complex
Rx and Tx descriptors and multiple buffers pointers as previous chips, but
also has simplified Tx and Rx buffer modes.  This driver uses the "flexible"
Tx mode, but in a simplified lower-overhead manner: it associates only a
single buffer descriptor with each frame descriptor.

Despite the extra space overhead in each receive skbuff, the driver must use
the simplified Rx buffer mode to assure that only a single data buffer is
associated with each RxFD. The driver implements this by reserving space
for the Rx descriptor at the head of each Rx skbuff.

The Speedo-3 has receive and command unit base addresses that are added to
almost all descriptor pointers.  The driver sets these to zero, so that all
pointer fields are absolute addresses.

The System Control Block (SCB) of some previous Intel chips exists on the
chip in both PCI I/O and memory space.  This driver uses the I/O space
registers, but might switch to memory mapped mode to better support non-x86
processors.

IIIB. Transmit structure

The driver must use the complex Tx command+descriptor mode in order to
have a indirect pointer to the skbuff data section.  Each Tx command block
(TxCB) is associated with two immediately appended Tx Buffer Descriptor
(TxBD).  A fixed ring of these TxCB+TxBD pairs are kept as part of the
speedo_private data structure for each adapter instance.

The newer i82558 explicitly supports this structure, and can read the two
TxBDs in the same PCI burst as the TxCB.

This ring structure is used for all normal transmit packets, but the
transmit packet descriptors aren't long enough for most non-Tx commands such
as CmdConfigure.  This is complicated by the possibility that the chip has
already loaded the link address in the previous descriptor.  So for these
commands we convert the next free descriptor on the ring to a NoOp, and point
that descriptor's link to the complex command.

An additional complexity of these non-transmit commands are that they may be
added asynchronous to the normal transmit queue, so we disable interrupts
whenever the Tx descriptor ring is manipulated.

A notable aspect of these special configure commands is that they do
work with the normal Tx ring entry scavenge method.  The Tx ring scavenge
is done at interrupt time using the 'dirty_tx' index, and checking for the
command-complete bit.  While the setup frames may have the NoOp command on the
Tx ring marked as complete, but not have completed the setup command, this
is not a problem.  The tx_ring entry can be still safely reused, as the
tx_skbuff[] entry is always empty for config_cmd and mc_setup frames.

Commands may have bits set e.g. CmdSuspend in the command word to either
suspend or stop the transmit/command unit.  This driver always flags the last
command with CmdSuspend, erases the CmdSuspend in the previous command, and
then issues a CU_RESUME.
Note: Watch out for the potential race condition here: imagine
	erasing the previous suspend
		the chip processes the previous command
		the chip processes the final command, and suspends
	doing the CU_RESUME
		the chip processes the next-yet-valid post-final-command.
So blindly sending a CU_RESUME is only safe if we do it immediately after
after erasing the previous CmdSuspend, without the possibility of an
intervening delay.  Thus the resume command is always within the
interrupts-disabled region.  This is a timing dependence, but handling this
condition in a timing-independent way would considerably complicate the code.

Note: In previous generation Intel chips, restarting the command unit was a
notoriously slow process.  This is presumably no longer true.

IIIC. Receive structure

Because of the bus-master support on the Speedo3 this driver uses the new
SKBUFF_RX_COPYBREAK scheme, rather than a fixed intermediate receive buffer.
This scheme allocates full-sized skbuffs as receive buffers.  The value
SKBUFF_RX_COPYBREAK is used as the copying breakpoint: it is chosen to
trade-off the memory wasted by passing the full-sized skbuff to the queue
layer for all frames vs. the copying cost of copying a frame to a
correctly-sized skbuff.

For small frames the copying cost is negligible (esp. considering that we
are pre-loading the cache with immediately useful header information), so we
allocate a new, minimally-sized skbuff.  For large frames the copying cost
is non-trivial, and the larger copy might flush the cache of useful data, so
we pass up the skbuff the packet was received into.

IIID. Synchronization
The driver runs as two independent, single-threaded flows of control.  One
is the send-packet routine, which enforces single-threaded use by the
dev->tbusy flag.  The other thread is the interrupt handler, which is single
threaded by the hardware and other software.

The send packet thread has partial control over the Tx ring and 'dev->tbusy'
flag.  It sets the tbusy flag whenever it's queuing a Tx packet. If the next
queue slot is empty, it clears the tbusy flag when finished otherwise it sets
the 'sp->tx_full' flag.

The interrupt handler has exclusive control over the Rx ring and records stats
from the Tx ring.  (The Tx-done interrupt can't be selectively turned off, so
we can't avoid the interrupt overhead by having the Tx routine reap the Tx
stats.)	 After reaping the stats, it marks the queue entry as empty by setting
the 'base' to zero.	 Iff the 'sp->tx_full' flag is set, it clears both the
tx_full and tbusy flags.

IV. Notes

Thanks to Steve Williams of Intel for arranging the non-disclosure agreement
that stated that I could disclose the information.  But I still resent
having to sign an Intel NDA when I'm helping Intel sell their own product!

*/

/* A few values that may be tweaked. */
/* The ring sizes should be a power of two for efficiency. */
#define TX_RING_SIZE	16		/* Effectively 2 entries fewer. */
#define RX_RING_SIZE	16
/* Size of an pre-allocated Rx buffer: <Ethernet MTU> + slack.*/
#define PKT_BUF_SZ		1536

/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT  ((800*HZ)/1000)

/* How to wait for the command unit to accept a command.
   Typically this takes 0 ticks. */
static inline void wait_for_cmd_done(long cmd_ioaddr)
{
	int wait = 100;
	do   ;
	while(inb(cmd_ioaddr) && --wait >= 0);
}

/* Operational parameter that usually are not changed. */

/* The rest of these values should never change. */

/* Offsets to the various registers.
   All accesses need not be longword aligned. */
enum speedo_offsets {
	SCBStatus = 0, SCBCmd = 2,	/* Rx/Command Unit command and status. */
	SCBPointer = 4,				/* General purpose pointer. */
	SCBPort = 8,				/* Misc. commands and operands.  */
	SCBflash = 12, SCBeeprom = 14, /* EEPROM and flash memory control. */
	SCBCtrlMDI = 16,			/* MDI interface control. */
	SCBEarlyRx = 20,			/* Early receive byte count. */
};
/* Commands that can be put in a command list entry. */
enum commands {
	CmdNOp = 0, CmdIASetup = 1, CmdConfigure = 2, CmdMulticastList = 3,
	CmdTx = 4, CmdTDR = 5, CmdDump = 6, CmdDiagnose = 7,
	CmdSuspend = 0x4000,		/* Suspend after completion. */
	CmdIntr = 0x2000,			/* Interrupt after completion. */
	CmdTxFlex = 0x0008,			/* Use "Flexible mode" for CmdTx command. */
};

/* The SCB accepts the following controls for the Tx and Rx units: */
#define	 CU_START		0x0010
#define	 CU_RESUME		0x0020
#define	 CU_STATSADDR	0x0040
#define	 CU_SHOWSTATS	0x0050	/* Dump statistics counters. */
#define	 CU_CMD_BASE	0x0060	/* Base address to add to add CU commands. */
#define	 CU_DUMPSTATS	0x0070	/* Dump then reset stats counters. */

#define	 RX_START	0x0001
#define	 RX_RESUME	0x0002
#define	 RX_ABORT	0x0004
#define	 RX_ADDR_LOAD	0x0006
#define	 RX_RESUMENR	0x0007
#define INT_MASK	0x0100
#define DRVR_INT	0x0200		/* Driver generated interrupt. */

/* The Speedo3 Rx and Tx frame/buffer descriptors. */
struct descriptor {			/* A generic descriptor. */
	s16 status;		/* Offset 0. */
	s16 command;		/* Offset 2. */
	u32 link;					/* struct descriptor *  */
	unsigned char params[0];
};

/* The Speedo3 Rx and Tx buffer descriptors. */
struct RxFD {					/* Receive frame descriptor. */
	s32 status;
	u32 link;					/* struct RxFD * */
	u32 rx_buf_addr;			/* void * */
	u16 count;
	u16 size;
};

/* Selected elements of the Tx/RxFD.status word. */
enum RxFD_bits {
	RxComplete=0x8000, RxOK=0x2000,
	RxErrCRC=0x0800, RxErrAlign=0x0400, RxErrTooBig=0x0200, RxErrSymbol=0x0010,
	RxEth2Type=0x0020, RxNoMatch=0x0004, RxNoIAMatch=0x0002,
	StatusComplete=0x8000,
};

struct TxFD {					/* Transmit frame descriptor set. */
	s32 status;
	u32 link;					/* void * */
	u32 tx_desc_addr;			/* Always points to the tx_buf_addr element. */
	s32 count;					/* # of TBD (=1), Tx start thresh., etc. */
	/* This constitutes two "TBD" entries -- we only use one. */
	u32 tx_buf_addr0;			/* void *, frame to be transmitted.  */
	s32 tx_buf_size0;			/* Length of Tx frame. */
	u32 tx_buf_addr1;			/* void *, frame to be transmitted.  */
	s32 tx_buf_size1;			/* Length of Tx frame. */
};

/* Elements of the dump_statistics block. This block must be lword aligned. */
struct speedo_stats {
	u32 tx_good_frames;
	u32 tx_coll16_errs;
	u32 tx_late_colls;
	u32 tx_underruns;
	u32 tx_lost_carrier;
	u32 tx_deferred;
	u32 tx_one_colls;
	u32 tx_multi_colls;
	u32 tx_total_colls;
	u32 rx_good_frames;
	u32 rx_crc_errs;
	u32 rx_align_errs;
	u32 rx_resource_errs;
	u32 rx_overrun_errs;
	u32 rx_colls_errs;
	u32 rx_runt_errs;
	u32 done_marker;
};

struct speedo_private {
	char devname[8];			/* Used only for kernel debugging. */
	const char *product_name;
	struct device *next_module;
	spinlock_t lock;
	struct TxFD	tx_ring[TX_RING_SIZE];	/* Commands (usually CmdTxPacket). */
	/* The saved address of a sent-in-place packet/buffer, for skfree(). */
	struct sk_buff* tx_skbuff[TX_RING_SIZE];
	struct descriptor  *last_cmd;	/* Last command sent. */
	/* Rx descriptor ring & addresses of receive-in-place skbuffs. */
	struct RxFD *rx_ringp[RX_RING_SIZE];
	struct sk_buff* rx_skbuff[RX_RING_SIZE];
	struct RxFD *last_rxf;	/* Last command sent. */
	struct enet_statistics stats;
	struct speedo_stats lstats;
	struct timer_list timer;	/* Media selection timer. */
	long last_rx_time;			/* Last Rx, in jiffies, to handle Rx hang. */
	unsigned int cur_rx, cur_tx;		/* The next free ring entry */
	unsigned int dirty_rx, dirty_tx;	/* The ring entries to be free()ed. */
	int mc_setup_frm_len;			 	/* The length of an allocated.. */
	struct descriptor *mc_setup_frm; 	/* ..multicast setup frame. */
	int mc_setup_busy;					/* Avoid double-use of setup frame. */
	char rx_mode;						/* Current PROMISC/ALLMULTI setting. */
	unsigned int tx_full:1;				/* The Tx queue is full. */
	unsigned int full_duplex:1;			/* Full-duplex operation requested. */
	unsigned int default_port:1;		/* Last dev->if_port value. */
	unsigned int rx_bug:1;				/* Work around receiver hang errata. */
	unsigned int rx_bug10:1;			/* Receiver might hang at 10mbps. */
	unsigned int rx_bug100:1;			/* Receiver might hang at 100mbps. */
	unsigned short phy[2];				/* PHY media interfaces available. */
};

/* The parameters for a CmdConfigure operation.
   There are so many options that it would be difficult to document each bit.
   We mostly use the default or recommended settings. */
const char i82557_config_cmd[22] = {
	22, 0x08, 0, 0,  0, 0x80, 0x32, 0x03,  1, /* 1=Use MII  0=Use AUI */
	0, 0x2E, 0,  0x60, 0,
	0xf2, 0x48,   0, 0x40, 0xf2, 0x80, 		/* 0x40=Force full-duplex */
	0x3f, 0x05, };
const char i82558_config_cmd[22] = {
	22, 0x08, 0, 1,  0, 0x80, 0x22, 0x03,  1, /* 1=Use MII  0=Use AUI */
	0, 0x2E, 0,  0x60, 0x08, 0x88,
	0x68, 0, 0x40, 0xf2, 0xBD, 		/* 0xBD->0xFD=Force full-duplex */
	0x31, 0x05, };

/* PHY media interface chips. */
static const char *phys[] = {
	"None", "i82553-A/B", "i82553-C", "i82503",
	"DP83840", "80c240", "80c24", "i82555",
	"unknown-8", "unknown-9", "DP83840A", "unknown-11",
	"unknown-12", "unknown-13", "unknown-14", "unknown-15", };
enum phy_chips { NonSuchPhy=0, I82553AB, I82553C, I82503, DP83840, S80C240,
					 S80C24, I82555, DP83840A=10, };
static const char is_mii[] = { 0, 1, 1, 0, 1, 1, 0, 1 };

static void speedo_found1(struct device *dev, long ioaddr, int irq,
						  int card_idx);

static int read_eeprom(long ioaddr, int location, int addr_len);
static int mdio_read(long ioaddr, int phy_id, int location);
static int mdio_write(long ioaddr, int phy_id, int location, int value);
static int speedo_open(struct device *dev);
static void speedo_timer(unsigned long data);
static void speedo_init_rx_ring(struct device *dev);
static int speedo_start_xmit(struct sk_buff *skb, struct device *dev);
static int speedo_rx(struct device *dev);
static void speedo_interrupt(int irq, void *dev_instance, struct pt_regs *regs);
static int speedo_close(struct device *dev);
static struct enet_statistics *speedo_get_stats(struct device *dev);
static int speedo_ioctl(struct device *dev, struct ifreq *rq, int cmd);
static void set_rx_mode(struct device *dev);



/* The parameters that may be passed in... */
/* 'options' is used to pass a transceiver override or full-duplex flag
   e.g. "options=16" for FD, "options=32" for 100mbps-only. */
static int full_duplex[] = {-1, -1, -1, -1, -1, -1, -1, -1};
static int options[] = {-1, -1, -1, -1, -1, -1, -1, -1};
#ifdef MODULE
static int debug = -1;			/* The debug level */
#endif

#ifdef honor_default_port
/* Optional driver feature to allow forcing the transceiver setting.
   Not recommended. */
static int mii_ctrl[8] = { 0x3300, 0x3100, 0x0000, 0x0100,
						   0x2000, 0x2100, 0x0400, 0x3100};
#endif

/* A list of all installed Speedo devices, for removing the driver module. */
static struct device *root_speedo_dev = NULL;

int eepro100_init(struct device *dev)
{
	int cards_found = 0;
	static int pci_index = 0;

	if (! pcibios_present())
		return cards_found;

	for (; pci_index < 8; pci_index++) {
		unsigned char pci_bus, pci_device_fn, pci_latency;
		long ioaddr;
		int irq;

		u16 pci_command, new_command;

		if (pcibios_find_device(PCI_VENDOR_ID_INTEL,
								PCI_DEVICE_ID_INTEL_82557,
								pci_index, &pci_bus,
								&pci_device_fn))
			break;
		{
			struct pci_dev *pdev = pci_find_slot(pci_bus, pci_device_fn);
			ioaddr = pdev->base_address[1];		/* Use [0] to mem-map */
			irq = pdev->irq;
		}
		/* Remove I/O space marker in bit 0. */
		ioaddr &= ~3;
		if (speedo_debug > 2)
			printk("Found Intel i82557 PCI Speedo at I/O %#lx, IRQ %d.\n",
				   ioaddr, irq);

		/* Get and check the bus-master and latency values. */
		pcibios_read_config_word(pci_bus, pci_device_fn,
								 PCI_COMMAND, &pci_command);
		new_command = pci_command | PCI_COMMAND_MASTER|PCI_COMMAND_IO;
		if (pci_command != new_command) {
			printk(KERN_INFO "  The PCI BIOS has not enabled this"
				   " device!  Updating PCI command %4.4x->%4.4x.\n",
				   pci_command, new_command);
			pcibios_write_config_word(pci_bus, pci_device_fn,
									  PCI_COMMAND, new_command);
		}
		pcibios_read_config_byte(pci_bus, pci_device_fn,
								 PCI_LATENCY_TIMER, &pci_latency);
		if (pci_latency < 32) {
			printk("  PCI latency timer (CFLT) is unreasonably low at %d."
				   "  Setting to 32 clocks.\n", pci_latency);
			pcibios_write_config_byte(pci_bus, pci_device_fn,
									  PCI_LATENCY_TIMER, 32);
		} else if (speedo_debug > 1)
			printk("  PCI latency timer (CFLT) is %#x.\n", pci_latency);

		speedo_found1(dev, ioaddr, irq, cards_found);
		dev = NULL;
		cards_found++;
	}

	return cards_found;
}

static void speedo_found1(struct device *dev, long ioaddr, int irq,
						  int card_idx)
{
	static int did_version = 0;			/* Already printed version info. */
	struct speedo_private *sp;
	char *product;
	int i, option;
	u16 eeprom[0x40];

	if (speedo_debug > 0  &&  did_version++ == 0)
		printk(version);

	dev = init_etherdev(dev, sizeof(struct speedo_private));

	if (dev->mem_start > 0)
		option = dev->mem_start;
	else if (card_idx >= 0  &&  options[card_idx] >= 0)
		option = options[card_idx];
	else
		option = 0;

	/* Read the station address EEPROM before doing the reset.
	   Perhaps this should even be done before accepting the device,
	   then we wouldn't have a device name with which to report the error. */
	{
		u16 sum = 0;
		int j;
		int addr_len = read_eeprom(ioaddr, 0, 6) == 0xffff ? 8 : 6;

		for (j = 0, i = 0; i < 0x40; i++) {
			u16 value = read_eeprom(ioaddr, i, addr_len);
			eeprom[i] = value;
			sum += value;
			if (i < 3) {
				dev->dev_addr[j++] = value;
				dev->dev_addr[j++] = value >> 8;
			}
		}
		if (sum != 0xBABA)
			printk(KERN_WARNING "%s: Invalid EEPROM checksum %#4.4x, "
				   "check settings before activating this device!\n",
				   dev->name, sum);
		/* Don't  unregister_netdev(dev);  as the EEPro may actually be
		   usable, especially if the MAC address is set later. */
	}

	/* Reset the chip: stop Tx and Rx processes and clear counters.
	   This takes less than 10usec and will easily finish before the next
	   action. */
	outl(0, ioaddr + SCBPort);

	if (eeprom[3] & 0x0100)
		product = "OEM i82557/i82558 10/100 Ethernet";
	else
		product = "Intel EtherExpress Pro 10/100";

	printk(KERN_INFO "%s: %s at %#3lx, ", dev->name, product, ioaddr);

	for (i = 0; i < 5; i++)
		printk("%2.2X:", dev->dev_addr[i]);
	printk("%2.2X, IRQ %d.\n", dev->dev_addr[i], irq);

#ifndef kernel_bloat
	/* OK, this is pure kernel bloat.  I don't like it when other drivers
	   waste non-pageable kernel space to emit similar messages, but I need
	   them for bug reports. */
	{
		const char *connectors[] = {" RJ45", " BNC", " AUI", " MII"};
		/* The self-test results must be paragraph aligned. */
		s32 str[6], *volatile self_test_results;
		int boguscnt = 16000;	/* Timeout for set-test. */
		if (eeprom[3] & 0x03)
			printk(KERN_INFO "  Receiver lock-up bug exists -- enabling"
				   " work-around.\n");
		printk(KERN_INFO "  Board assembly %4.4x%2.2x-%3.3d, Physical"
			   " connectors present:",
			   eeprom[8], eeprom[9]>>8, eeprom[9] & 0xff);
		for (i = 0; i < 4; i++)
			if (eeprom[5] & (1<<i))
				printk(connectors[i]);
		printk("\n"KERN_INFO"  Primary interface chip %s PHY #%d.\n",
			   phys[(eeprom[6]>>8)&15], eeprom[6] & 0x1f);
		if (eeprom[7] & 0x0700)
			printk(KERN_INFO "    Secondary interface chip %s.\n",
				   phys[(eeprom[7]>>8)&7]);
		if (((eeprom[6]>>8) & 0x3f) == DP83840
			||  ((eeprom[6]>>8) & 0x3f) == DP83840A) {
			int mdi_reg23 = mdio_read(ioaddr, eeprom[6] & 0x1f, 23) | 0x0422;
			if (congenb)
			  mdi_reg23 |= 0x0100;
			printk(KERN_INFO"  DP83840 specific setup, setting register 23 to %4.4x.\n",
				   mdi_reg23);
			mdio_write(ioaddr, eeprom[6] & 0x1f, 23, mdi_reg23);
		}
		if ((option >= 0) && (option & 0x70)) {
			printk(KERN_INFO "  Forcing %dMbs %s-duplex operation.\n",
				   (option & 0x20 ? 100 : 10),
				   (option & 0x10 ? "full" : "half"));
			mdio_write(ioaddr, eeprom[6] & 0x1f, 0,
					   ((option & 0x20) ? 0x2000 : 0) | 	/* 100mbps? */
					   ((option & 0x10) ? 0x0100 : 0)); /* Full duplex? */
		}

		/* Perform a system self-test. */
		self_test_results = (s32*) ((((long) str) + 15) & ~0xf);
		self_test_results[0] = 0;
		self_test_results[1] = -1;
		outl(virt_to_bus(self_test_results) | 1, ioaddr + SCBPort);
		do {
			udelay(10);
		} while (self_test_results[1] == -1  &&  --boguscnt >= 0);

		if (boguscnt < 0) {		/* Test optimized out. */
			printk(KERN_ERR "Self test failed, status %8.8x:\n"
				   KERN_ERR " Failure to initialize the i82557.\n"
				   KERN_ERR " Verify that the card is a bus-master"
				   " capable slot.\n",
				   self_test_results[1]);
		} else
			printk(KERN_INFO "  General self-test: %s.\n"
				   KERN_INFO "  Serial sub-system self-test: %s.\n"
				   KERN_INFO "  Internal registers self-test: %s.\n"
				   KERN_INFO "  ROM checksum self-test: %s (%#8.8x).\n",
				   self_test_results[1] & 0x1000 ? "failed" : "passed",
				   self_test_results[1] & 0x0020 ? "failed" : "passed",
				   self_test_results[1] & 0x0008 ? "failed" : "passed",
				   self_test_results[1] & 0x0004 ? "failed" : "passed",
				   self_test_results[0]);
	}
#endif  /* kernel_bloat */

	/* We do a request_region() only to register /proc/ioports info. */
	request_region(ioaddr, SPEEDO3_TOTAL_SIZE, "Intel Speedo3 Ethernet");

	dev->base_addr = ioaddr;
	dev->irq = irq;

	if (dev->priv == NULL)
		dev->priv = kmalloc(sizeof(*sp), GFP_KERNEL);
	sp = dev->priv;
	memset(sp, 0, sizeof(*sp));
	sp->next_module = root_speedo_dev;
	root_speedo_dev = dev;

	sp->full_duplex = option >= 0 && (option & 0x10) ? 1 : 0;
	if (card_idx >= 0) {
		if (full_duplex[card_idx] >= 0)
			sp->full_duplex = full_duplex[card_idx];
	}
	sp->default_port = option >= 0 ? (option & 0x0f) : 0;

	sp->phy[0] = eeprom[6];
	sp->phy[1] = eeprom[7];
	sp->rx_bug = (eeprom[3] & 0x03) == 3 ? 0 : 1;

	if (sp->rx_bug)
		printk(KERN_INFO "  Receiver lock-up workaround activated.\n");

	/* The Speedo-specific entries in the device structure. */
	dev->open = &speedo_open;
	dev->hard_start_xmit = &speedo_start_xmit;
	dev->stop = &speedo_close;
	dev->get_stats = &speedo_get_stats;
	dev->set_multicast_list = &set_rx_mode;
	dev->do_ioctl = &speedo_ioctl;

	return;
}

/* Serial EEPROM section.
   A "bit" grungy, but we work our way through bit-by-bit :->. */
/*  EEPROM_Ctrl bits. */
#define EE_SHIFT_CLK	0x01	/* EEPROM shift clock. */
#define EE_CS			0x02	/* EEPROM chip select. */
#define EE_DATA_WRITE	0x04	/* EEPROM chip data in. */
#define EE_WRITE_0		0x01
#define EE_WRITE_1		0x05
#define EE_DATA_READ	0x08	/* EEPROM chip data out. */
#define EE_ENB			(0x4800 | EE_CS)

/* Delay between EEPROM clock transitions.
   This will actually work with no delay on 33Mhz PCI.  */
#define eeprom_delay(nanosec)		udelay(1);

/* The EEPROM commands include the alway-set leading bit. */
#define EE_WRITE_CMD	(5 << addr_len)
#define EE_READ_CMD		(6 << addr_len)
#define EE_ERASE_CMD	(7 << addr_len)

static int read_eeprom(long ioaddr, int location, int addr_len)
{
	unsigned short retval = 0;
	int ee_addr = ioaddr + SCBeeprom;
	int read_cmd = location | EE_READ_CMD;
	int i;

	outw(EE_ENB & ~EE_CS, ee_addr);
	outw(EE_ENB, ee_addr);

	/* Shift the read command bits out. */
	for (i = 12; i >= 0; i--) {
		short dataval = (read_cmd & (1 << i)) ? EE_DATA_WRITE : 0;
		outw(EE_ENB | dataval, ee_addr);
		eeprom_delay(100);
		outw(EE_ENB | dataval | EE_SHIFT_CLK, ee_addr);
		eeprom_delay(150);
	}
	outw(EE_ENB, ee_addr);

	for (i = 15; i >= 0; i--) {
		outw(EE_ENB | EE_SHIFT_CLK, ee_addr);
		eeprom_delay(100);
		retval = (retval << 1) | ((inw(ee_addr) & EE_DATA_READ) ? 1 : 0);
		outw(EE_ENB, ee_addr);
		eeprom_delay(100);
	}

	/* Terminate the EEPROM access. */
	outw(EE_ENB & ~EE_CS, ee_addr);
	return retval;
}

static int mdio_read(long ioaddr, int phy_id, int location)
{
	int val, boguscnt = 64*10;		/* <64 usec. to complete, typ 27 ticks */
	outl(0x08000000 | (location<<16) | (phy_id<<21), ioaddr + SCBCtrlMDI);
	do {
		val = inl(ioaddr + SCBCtrlMDI);
		if (--boguscnt < 0) {
			printk(KERN_ERR " mdio_read() timed out with val = %8.8x.\n", val);
		}
	} while (! (val & 0x10000000));
	return val & 0xffff;
}

static int mdio_write(long ioaddr, int phy_id, int location, int value)
{
	int val, boguscnt = 64*10;		/* <64 usec. to complete, typ 27 ticks */
	outl(0x04000000 | (location<<16) | (phy_id<<21) | value,
		 ioaddr + SCBCtrlMDI);
	do {
		val = inl(ioaddr + SCBCtrlMDI);
		if (--boguscnt < 0) {
			printk(KERN_ERR" mdio_write() timed out with val = %8.8x.\n", val);
		}
	} while (! (val & 0x10000000));
	return val & 0xffff;
}


static int
speedo_open(struct device *dev)
{
	struct speedo_private *sp = (struct speedo_private *)dev->priv;
	long ioaddr = dev->base_addr;

#ifdef notdef
	/* We could reset the chip, but should not need to. */
	/* In fact we MUST NOT, unless we also re-do the init */
	outl(0, ioaddr + SCBPort);
	udelay(10);
#endif

	/* This had better be initialized before we initialize the interrupt! */
	sp->lock = (spinlock_t) SPIN_LOCK_UNLOCKED;

	if (speedo_debug > 1)
		printk(KERN_DEBUG "%s: speedo_open() irq %d.\n", dev->name, dev->irq);

#ifdef oh_no_you_dont_unless_you_honour_the_options_passed_in_to_us
	/* Retrigger negotiation to reset previous errors. */
	if ((sp->phy[0] & 0x8000) == 0) {
		int phy_addr = sp->phy[0] & 0x1f ;
		/* Use 0x3300 for restarting NWay, other values to force xcvr:
		   0x0000 10-HD
		   0x0100 10-FD
		   0x2000 100-HD
		   0x2100 100-FD
		*/
#ifdef honor_default_port
		mdio_write(ioaddr, phy_addr, 0, mii_ctrl[dev->default_port & 7]);
#else
		mdio_write(ioaddr, phy_addr, 0, 0x3300);
#endif
	}
#endif

	/* Load the statistics block address. */
	wait_for_cmd_done(ioaddr + SCBCmd);
	outl(virt_to_bus(&sp->lstats), ioaddr + SCBPointer);
	outw(INT_MASK | CU_STATSADDR, ioaddr + SCBCmd);
	sp->lstats.done_marker = 0;

	speedo_init_rx_ring(dev);
	wait_for_cmd_done(ioaddr + SCBCmd);
	outl(0, ioaddr + SCBPointer);
	outw(INT_MASK | RX_ADDR_LOAD, ioaddr + SCBCmd);

	/* Todo: verify that we must wait for previous command completion. */
	wait_for_cmd_done(ioaddr + SCBCmd);
	outl(virt_to_bus(sp->rx_ringp[0]), ioaddr + SCBPointer);
	outw(INT_MASK | RX_START, ioaddr + SCBCmd);

	/* Fill the first command with our physical address. */
	{
		u16 *eaddrs = (u16 *)dev->dev_addr;
		u16 *setup_frm = (u16 *)&(sp->tx_ring[0].tx_desc_addr);

		/* Avoid a bug(?!) here by marking the command already completed. */
		sp->tx_ring[0].status = ((CmdSuspend | CmdIASetup) << 16) | 0xa000;
		sp->tx_ring[0].link = virt_to_bus(&(sp->tx_ring[1]));
		*setup_frm++ = eaddrs[0];
		*setup_frm++ = eaddrs[1];
		*setup_frm++ = eaddrs[2];
	}
	sp->last_cmd = (struct descriptor *)&sp->tx_ring[0];
	sp->cur_tx = 1;
	sp->dirty_tx = 0;
	sp->tx_full = 0;

	wait_for_cmd_done(ioaddr + SCBCmd);
	outl(0, ioaddr + SCBPointer);
	outw(INT_MASK | CU_CMD_BASE, ioaddr + SCBCmd);

	dev->if_port = sp->default_port;

	dev->tbusy = 0;
	dev->interrupt = 0;
	dev->start = 1;

	/* Start the chip's Tx process and unmask interrupts. */
	/* Todo: verify that we must wait for previous command completion. */
	wait_for_cmd_done(ioaddr + SCBCmd);
	outl(virt_to_bus(&sp->tx_ring[0]), ioaddr + SCBPointer);
	outw(CU_START, ioaddr + SCBCmd);

	/* Setup the chip and configure the multicast list. */
	sp->mc_setup_frm = NULL;
	sp->mc_setup_frm_len = 0;
	sp->mc_setup_busy = 0;
	sp->rx_mode = -1;			/* Invalid -> always reset the mode. */
	set_rx_mode(dev);

	if (speedo_debug > 2) {
		printk(KERN_DEBUG "%s: Done speedo_open(), status %8.8x.\n",
			   dev->name, inw(ioaddr + SCBStatus));
	}

	wait_for_cmd_done(ioaddr + SCBCmd);
	outw(CU_DUMPSTATS, ioaddr + SCBCmd);
	/* No need to wait for the command unit to accept here. */
	if ((sp->phy[0] & 0x8000) == 0)
		mdio_read(ioaddr, sp->phy[0] & 0x1f, 0);

	/*
	 * Request the IRQ last, after we have set up all data structures.
	 * It would be bad to get an interrupt before we're ready.
	 */
	if (request_irq(dev->irq, &speedo_interrupt, SA_SHIRQ,
					"Intel EtherExpress Pro 10/100 Ethernet", dev)) {
		return -EAGAIN;
	}

	MOD_INC_USE_COUNT;

	/* Set the timer.  The timer serves a dual purpose:
	   1) to monitor the media interface (e.g. link beat) and perhaps switch
	   to an alternate media type
	   2) to monitor Rx activity, and restart the Rx process if the receiver
	   hangs. */
	init_timer(&sp->timer);
	sp->timer.expires = RUN_AT((24*HZ)/10); 			/* 2.4 sec. */
	sp->timer.data = (unsigned long)dev;
	sp->timer.function = &speedo_timer;					/* timer handler */
	add_timer(&sp->timer);

	return 0;
}

/* Media monitoring and control. */
static void speedo_timer(unsigned long data)
{
	struct device *dev = (struct device *)data;
	struct speedo_private *sp = (struct speedo_private *)dev->priv;

	if (speedo_debug > 3) {
		long ioaddr = dev->base_addr;
		printk(KERN_DEBUG "%s: Media control tick, status %4.4x.\n",
			   dev->name, inw(ioaddr + SCBStatus));
	}
	if (sp->rx_mode < 0  ||
		(sp->rx_bug  && jiffies - sp->last_rx_time > 2*HZ)) {
		/* We haven't received a packet in a Long Time.  We might have been
		   bitten by the receiver hang bug.  This can be cleared by sending
		   a set multicast list command. */
		set_rx_mode(dev);
	}
	/* We must continue to monitor the media. */
	sp->timer.expires = RUN_AT(2*HZ); 			/* 2.0 sec. */
	add_timer(&sp->timer);
}

/* Initialize the Rx and Tx rings, along with various 'dev' bits. */
static void
speedo_init_rx_ring(struct device *dev)
{
	struct speedo_private *sp = (struct speedo_private *)dev->priv;
	struct RxFD *rxf, *last_rxf = NULL;
	int i;

	sp->cur_rx = 0;

	for (i = 0; i < RX_RING_SIZE; i++) {
		struct sk_buff *skb;
		skb = dev_alloc_skb(PKT_BUF_SZ + sizeof(struct RxFD));
		sp->rx_skbuff[i] = skb;
		if (skb == NULL)
			break;			/* OK.  Just initially short of Rx bufs. */
		skb->dev = dev;			/* Mark as being used by this device. */
		rxf = (struct RxFD *)skb->tail;
		sp->rx_ringp[i] = rxf;
		skb_reserve(skb, sizeof(struct RxFD));
		if (last_rxf)
			last_rxf->link = virt_to_bus(rxf);
		last_rxf = rxf;
		rxf->status = 0x00000001; 			/* '1' is flag value only. */
		rxf->link = 0;						/* None yet. */
		/* This field unused by i82557, we use it as a consistency check. */
#ifdef final_version
		rxf->rx_buf_addr = 0xffffffff;
#else
		rxf->rx_buf_addr = virt_to_bus(skb->tail);
#endif
		rxf->count = 0;
		rxf->size = PKT_BUF_SZ;
	}
	sp->dirty_rx = (unsigned int)(i - RX_RING_SIZE);
	/* Mark the last entry as end-of-list. */
	last_rxf->status = 0xC0000002; 			/* '2' is flag value only. */
	sp->last_rxf = last_rxf;
}

static void speedo_tx_timeout(struct device *dev)
{
	struct speedo_private *sp = (struct speedo_private *)dev->priv;
	long ioaddr = dev->base_addr;

	printk(KERN_WARNING "%s: Transmit timed out: status %4.4x "
		   " %4.4x at %d/%d command %8.8x.\n",
		   dev->name, inw(ioaddr + SCBStatus), inw(ioaddr + SCBCmd),
		   sp->dirty_tx, sp->cur_tx,
		   sp->tx_ring[sp->dirty_tx % TX_RING_SIZE].status);
	if ((inw(ioaddr + SCBStatus) & 0x00C0) != 0x0080) {
		printk(KERN_WARNING "%s: Trying to restart the transmitter...\n",
			   dev->name);
		outl(virt_to_bus(&sp->tx_ring[sp->dirty_tx % TX_RING_SIZE]),
			 ioaddr + SCBPointer);
		outw(CU_START, ioaddr + SCBCmd);
	} else {
		outw(DRVR_INT, ioaddr + SCBCmd);
	}
#ifdef oh_no_you_dont_unless_you_honour_the_options_passed_in_to_us
	/* Reset the MII transceiver, suggested by Fred Young @ scalable.com. */
	if ((sp->phy[0] & 0x8000) == 0) {
		int phy_addr = sp->phy[0] & 0x1f;
		mdio_write(ioaddr, phy_addr, 0, 0x0400);
		mdio_write(ioaddr, phy_addr, 1, 0x0000);
		mdio_write(ioaddr, phy_addr, 4, 0x0000);
		mdio_write(ioaddr, phy_addr, 0, 0x8000);
#ifdef honor_default_port
		mdio_write(ioaddr, phy_addr, 0, mii_ctrl[dev->default_port & 7]);
#endif
	}
#endif
	sp->stats.tx_errors++;
	dev->trans_start = jiffies;
	return;
}

static int
speedo_start_xmit(struct sk_buff *skb, struct device *dev)
{
	struct speedo_private *sp = (struct speedo_private *)dev->priv;
	long ioaddr = dev->base_addr;
	int entry;

	/* Block a timer-based transmit from overlapping.  This could better be
	   done with atomic_swap(1, dev->tbusy), but set_bit() works as well.
	   If this ever occurs the queue layer is doing something evil! */
	if (test_and_set_bit(0, (void*)&dev->tbusy) != 0) {
		int tickssofar = jiffies - dev->trans_start;
		if (tickssofar < TX_TIMEOUT - 2)
			return 1;
		if (tickssofar < TX_TIMEOUT) {
			/* Reap sent packets from the full Tx queue. */
			outw(DRVR_INT, ioaddr + SCBCmd);
			return 1;
		}
		speedo_tx_timeout(dev);
		return 1;
	}

	/* Caution: the write order is important here, set the base address
	   with the "ownership" bits last. */

	{	/* Prevent interrupts from changing the Tx ring from underneath us. */
		unsigned long flags;

		spin_lock_irqsave(&sp->lock, flags);

		/* Calculate the Tx descriptor entry. */
		entry = sp->cur_tx++ % TX_RING_SIZE;

		sp->tx_skbuff[entry] = skb;
		/* Todo: be a little more clever about setting the interrupt bit. */
		sp->tx_ring[entry].status =
			(CmdSuspend | CmdTx | CmdTxFlex) << 16;
		sp->tx_ring[entry].link =
		  virt_to_bus(&sp->tx_ring[sp->cur_tx % TX_RING_SIZE]);
		sp->tx_ring[entry].tx_desc_addr =
		  virt_to_bus(&sp->tx_ring[entry].tx_buf_addr0);
		/* The data region is always in one buffer descriptor, Tx FIFO
		   threshold of 256. */
		sp->tx_ring[entry].count = 0x01208000;
		sp->tx_ring[entry].tx_buf_addr0 = virt_to_bus(skb->data);
		sp->tx_ring[entry].tx_buf_size0 = skb->len;
		/* Todo: perhaps leave the interrupt bit set if the Tx queue is more
		   than half full.  Argument against: we should be receiving packets
		   and scavenging the queue.  Argument for: if so, it shouldn't
		   matter. */
		sp->last_cmd->command &= ~(CmdSuspend | CmdIntr);
		sp->last_cmd = (struct descriptor *)&sp->tx_ring[entry];

		/* Trigger the command unit resume. */
		wait_for_cmd_done(ioaddr + SCBCmd);
		outw(CU_RESUME, ioaddr + SCBCmd);

		spin_unlock_irqrestore(&sp->lock, flags);
	}

	/* Leave room for set_rx_mode() to fill two entries. */
	if (sp->cur_tx - sp->dirty_tx > TX_RING_SIZE - 3)
		sp->tx_full = 1;
	else
		clear_bit(0, (void*)&dev->tbusy);

	dev->trans_start = jiffies;

	return 0;
}

/* The interrupt handler does all of the Rx thread work and cleans up
   after the Tx thread. */
static void speedo_interrupt(int irq, void *dev_instance, struct pt_regs *regs)
{
	struct device *dev = (struct device *)dev_instance;
	struct speedo_private *sp;
	long ioaddr, boguscnt = max_interrupt_work;
	unsigned short status;

#ifndef final_version
	if (dev == NULL) {
		printk(KERN_ERR "speedo_interrupt(): irq %d for unknown device.\n", irq);
		return;
	}
#endif

	ioaddr = dev->base_addr;
	sp = (struct speedo_private *)dev->priv;
	spin_lock(&sp->lock);

#ifndef final_version
	dev->interrupt = 1;
#endif

	do {
		status = inw(ioaddr + SCBStatus);
		/* Acknowledge all of the current interrupt sources ASAP. */
		outw(status & 0xfc00, ioaddr + SCBStatus);

		if (speedo_debug > 4)
			printk(KERN_DEBUG "%s: interrupt  status=%#4.4x.\n",
				   dev->name, status);

		if ((status & 0xfc00) == 0)
			break;

		if (status & 0x4000)	 /* Packet received. */
			speedo_rx(dev);

		if (status & 0x1000) {
		  if ((status & 0x003c) == 0x0028) /* No more Rx buffers. */
			outw(RX_RESUMENR, ioaddr + SCBCmd);
		  else if ((status & 0x003c) == 0x0008) { /* No resources (why?!) */
			/* No idea of what went wrong.  Restart the receiver. */
			outl(virt_to_bus(sp->rx_ringp[sp->cur_rx % RX_RING_SIZE]),
				 ioaddr + SCBPointer);
			outw(RX_START, ioaddr + SCBCmd);
		  }
		  sp->stats.rx_errors++;
		}

		/* User interrupt, Command/Tx unit interrupt or CU not active. */
		if (status & 0xA400) {
			unsigned int dirty_tx = sp->dirty_tx;

			while (sp->cur_tx - dirty_tx > 0) {
				int entry = dirty_tx % TX_RING_SIZE;
				int status = sp->tx_ring[entry].status;

				if (speedo_debug > 5)
					printk(KERN_DEBUG " scavenge candidate %d status %4.4x.\n",
						   entry, status);
				if ((status & StatusComplete) == 0)
					break;			/* It still hasn't been processed. */
				/* Free the original skb. */
				if (sp->tx_skbuff[entry]) {
					sp->stats.tx_packets++;	/* Count only user packets. */
					sp->stats.tx_bytes += sp->tx_skbuff[entry]->len; /* Count transmitted bytes */
					dev_free_skb(sp->tx_skbuff[entry]);
					sp->tx_skbuff[entry] = 0;
				} else if ((sp->tx_ring[entry].status&0x70000) == CmdNOp << 16)
					sp->mc_setup_busy = 0;
				dirty_tx++;
			}

#ifndef final_version
			if (sp->cur_tx - dirty_tx > TX_RING_SIZE) {
				printk(KERN_ERR "out-of-sync dirty pointer, %d vs. %d,"
					   " full=%d.\n",
					   dirty_tx, sp->cur_tx, sp->tx_full);
				dirty_tx += TX_RING_SIZE;
			}
#endif

			if (sp->tx_full && dev->tbusy
				&& dirty_tx > sp->cur_tx - TX_RING_SIZE + 2) {
				/* The ring is no longer full, clear tbusy. */
				sp->tx_full = 0;
				clear_bit(0, (void*)&dev->tbusy);
				mark_bh(NET_BH);
			}

			sp->dirty_tx = dirty_tx;
		}

		if (--boguscnt < 0) {
			printk(KERN_ERR "%s: Too much work at interrupt, status=0x%4.4x.\n",
				   dev->name, status);
			/* Clear all interrupt sources. */
			outl(0xfc00, ioaddr + SCBStatus);
			break;
		}
	} while (1);

	if (speedo_debug > 3)
		printk(KERN_DEBUG "%s: exiting interrupt, status=%#4.4x.\n",
			   dev->name, inw(ioaddr + SCBStatus));

	dev->interrupt = 0;
	spin_unlock(&sp->lock);
	return;
}

static int
speedo_rx(struct device *dev)
{
	struct speedo_private *sp = (struct speedo_private *)dev->priv;
	int entry = sp->cur_rx % RX_RING_SIZE;
	int status;
	int rx_work_limit = sp->dirty_rx + RX_RING_SIZE - sp->cur_rx;

	if (speedo_debug > 4)
		printk(KERN_DEBUG " In speedo_rx().\n");
	/* If we own the next entry, it's a new packet. Send it up. */
	while (sp->rx_ringp[entry] != NULL &&
		   (status = sp->rx_ringp[entry]->status) & RxComplete) {

		if (--rx_work_limit < 0)
			break;
		if (speedo_debug > 4)
			printk(KERN_DEBUG "  speedo_rx() status %8.8x len %d.\n", status,
				   sp->rx_ringp[entry]->count & 0x3fff);
		if ((status & (RxErrTooBig|RxOK)) != RxOK) {
			if (status & RxErrTooBig)
				printk(KERN_ERR "%s: Ethernet frame overran the Rx buffer, "
					   "status %8.8x!\n", dev->name, status);
			else if ( ! (status & 0x2000)) {
				/* There was a fatal error.  This *should* be impossible. */
				sp->stats.rx_errors++;
				printk(KERN_ERR "%s: Anomalous event in speedo_rx(), "
					   "status %8.8x.\n",
					   dev->name, status);
			}
		} else {
			int pkt_len = sp->rx_ringp[entry]->count & 0x3fff;
			struct sk_buff *skb;

			/* Check if the packet is long enough to just accept without
			   copying to a properly sized skbuff. */
			if (pkt_len < rx_copybreak
				&& (skb = dev_alloc_skb(pkt_len + 2)) != 0) {
				skb->dev = dev;
				skb_reserve(skb, 2);	/* Align IP on 16 byte boundaries */
				/* 'skb_put()' points to the start of sk_buff data area. */
#if 1 || USE_IP_CSUM
				/* Packet is in one chunk -- we can copy + cksum. */
				eth_copy_and_sum(skb,
								 bus_to_virt(sp->rx_ringp[entry]->rx_buf_addr),
								 pkt_len, 0);
				skb_put(skb, pkt_len);
#else
				memcpy(skb_put(skb, pkt_len),
					   bus_to_virt(sp->rx_ringp[entry]->rx_buf_addr), pkt_len);
#endif
			} else {
				void *temp;
				/* Pass up the already-filled skbuff. */
				skb = sp->rx_skbuff[entry];
				if (skb == NULL) {
					printk(KERN_ERR "%s: Inconsistent Rx descriptor chain.\n",
						   dev->name);
					break;
				}
				sp->rx_skbuff[entry] = NULL;
				temp = skb_put(skb, pkt_len);
				if (bus_to_virt(sp->rx_ringp[entry]->rx_buf_addr) != temp)
					printk(KERN_ERR "%s: Rx consistency error -- the skbuff "
						   "addresses do not match in speedo_rx: %p vs. %p "
						   "/ %p.\n", dev->name,
						   bus_to_virt(sp->rx_ringp[entry]->rx_buf_addr),
						   skb->head, temp);
				sp->rx_ringp[entry] = NULL;
			}
			skb->protocol = eth_type_trans(skb, dev);
			netif_rx(skb);
			sp->stats.rx_packets++;
			sp->stats.rx_bytes += pkt_len; /* Count received bytes */
		}
		entry = (++sp->cur_rx) % RX_RING_SIZE;
	}

	/* Refill the Rx ring buffers. */
	for (; sp->dirty_rx < sp->cur_rx; sp->dirty_rx++) {
		struct RxFD *rxf;
		entry = sp->dirty_rx % RX_RING_SIZE;
		if (sp->rx_skbuff[entry] == NULL) {
			struct sk_buff *skb;
			/* Get a fresh skbuff to replace the consumed one. */
			skb = dev_alloc_skb(PKT_BUF_SZ + sizeof(struct RxFD));
			sp->rx_skbuff[entry] = skb;
			if (skb == NULL) {
				sp->rx_ringp[entry] = NULL;
				break;			/* Better luck next time!  */
			}
			rxf = sp->rx_ringp[entry] = (struct RxFD *)skb->tail;
			skb->dev = dev;
			skb_reserve(skb, sizeof(struct RxFD));
			rxf->rx_buf_addr = virt_to_bus(skb->tail);
		} else {
			rxf = sp->rx_ringp[entry];
		}
		rxf->status = 0xC0000001; 	/* '1' for driver use only. */
		rxf->link = 0;			/* None yet. */
		rxf->count = 0;
		rxf->size = PKT_BUF_SZ;
		sp->last_rxf->link = virt_to_bus(rxf);
		sp->last_rxf->status &= ~0xC0000000;
		sp->last_rxf = rxf;
	}

	sp->last_rx_time = jiffies;
	return 0;
}

static int
speedo_close(struct device *dev)
{
	long ioaddr = dev->base_addr;
	struct speedo_private *sp = (struct speedo_private *)dev->priv;
	int i;

	dev->start = 0;
	dev->tbusy = 1;

	if (speedo_debug > 1)
		printk(KERN_DEBUG "%s: Shutting down ethercard, status was %4.4x.\n",
			   dev->name, inw(ioaddr + SCBStatus));

	/* Shut off the media monitoring timer. */
	del_timer(&sp->timer);

	/* Disable interrupts, and stop the chip's Rx process. */
	outw(INT_MASK, ioaddr + SCBCmd);
	outw(INT_MASK | RX_ABORT, ioaddr + SCBCmd);

	free_irq(dev->irq, dev);

	/* Free all the skbuffs in the Rx and Tx queues. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		struct sk_buff *skb = sp->rx_skbuff[i];
		sp->rx_skbuff[i] = 0;
		/* Clear the Rx descriptors. */
		if (skb)
			dev_free_skb(skb);
	}

	for (i = 0; i < TX_RING_SIZE; i++) {
		struct sk_buff *skb = sp->tx_skbuff[i];
		sp->tx_skbuff[i] = 0;
		/* Clear the Tx descriptors. */
		if (skb)
			dev_free_skb(skb);
	}
	if (sp->mc_setup_frm) {
		kfree(sp->mc_setup_frm);
		sp->mc_setup_frm_len = 0;
	}

	/* Print a few items for debugging. */
	if (speedo_debug > 3) {
		int phy_num = sp->phy[0] & 0x1f;
		printk(KERN_DEBUG "%s:Printing Rx ring (next to receive into %d).\n",
			   dev->name, sp->cur_rx);

		for (i = 0; i < RX_RING_SIZE; i++)
			printk(KERN_DEBUG "  Rx ring entry %d  %8.8x.\n",
				   i, (int)sp->rx_ringp[i]->status);

		for (i = 0; i < 5; i++)
			printk(KERN_DEBUG "  PHY index %d register %d is %4.4x.\n",
				   phy_num, i, mdio_read(ioaddr, phy_num, i));
		for (i = 21; i < 26; i++)
			printk(KERN_DEBUG "  PHY index %d register %d is %4.4x.\n",
				   phy_num, i, mdio_read(ioaddr, phy_num, i));
	}
	MOD_DEC_USE_COUNT;

	return 0;
}

/* The Speedo-3 has an especially awkward and unusable method of getting
   statistics out of the chip.  It takes an unpredictable length of time
   for the dump-stats command to complete.  To avoid a busy-wait loop we
   update the stats with the previous dump results, and then trigger a
   new dump.

   These problems are mitigated by the current /proc implementation, which
   calls this routine first to judge the output length, and then to emit the
   output.

   Oh, and incoming frames are dropped while executing dump-stats!
   */
static struct enet_statistics *
speedo_get_stats(struct device *dev)
{
	struct speedo_private *sp = (struct speedo_private *)dev->priv;
	long ioaddr = dev->base_addr;

	if (sp->lstats.done_marker == 0xA007) {	/* Previous dump finished */
		sp->stats.tx_aborted_errors += sp->lstats.tx_coll16_errs;
		sp->stats.tx_window_errors += sp->lstats.tx_late_colls;
		sp->stats.tx_fifo_errors += sp->lstats.tx_underruns;
		sp->stats.tx_fifo_errors += sp->lstats.tx_lost_carrier;
		/*sp->stats.tx_deferred += sp->lstats.tx_deferred;*/
		sp->stats.collisions += sp->lstats.tx_total_colls;
		sp->stats.rx_crc_errors += sp->lstats.rx_crc_errs;
		sp->stats.rx_frame_errors += sp->lstats.rx_align_errs;
		sp->stats.rx_over_errors += sp->lstats.rx_resource_errs;
		sp->stats.rx_fifo_errors += sp->lstats.rx_overrun_errs;
		sp->stats.rx_length_errors += sp->lstats.rx_runt_errs;
		sp->lstats.done_marker = 0x0000;
		if (dev->start) {
			wait_for_cmd_done(ioaddr + SCBCmd);
			outw(CU_DUMPSTATS, ioaddr + SCBCmd);
		}
	}
	return &sp->stats;
}

static int speedo_ioctl(struct device *dev, struct ifreq *rq, int cmd)
{
	struct speedo_private *sp = (struct speedo_private *)dev->priv;
	long ioaddr = dev->base_addr;
	u16 *data = (u16 *)&rq->ifr_data;
	int phy = sp->phy[0] & 0x1f;

    switch(cmd) {
	case SIOCDEVPRIVATE:		/* Get the address of the PHY in use. */
		data[0] = phy;
	case SIOCDEVPRIVATE+1:		/* Read the specified MII register. */
		data[3] = mdio_read(ioaddr, data[0], data[1]);
		return 0;
	case SIOCDEVPRIVATE+2:		/* Write the specified MII register */
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		mdio_write(ioaddr, data[0], data[1], data[2]);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

/* Set or clear the multicast filter for this adaptor.
   This is very ugly with Intel chips -- we usually have to execute an
   entire configuration command, plus process a multicast command.
   This is complicated.  We must put a large configuration command and
   an arbitrarily-sized multicast command in the transmit list.
   To minimize the disruption -- the previous command might have already
   loaded the link -- we convert the current command block, normally a Tx
   command, into a no-op and link it to the new command.
*/
static void
set_rx_mode(struct device *dev)
{
	struct speedo_private *sp = (struct speedo_private *)dev->priv;
	long ioaddr = dev->base_addr;
	struct descriptor *last_cmd;
	char new_rx_mode;
	unsigned long flags;
	int entry, i;

	if (dev->flags & IFF_PROMISC) {			/* Set promiscuous. */
		new_rx_mode = 3;
	} else if ((dev->flags & IFF_ALLMULTI)  ||
			   dev->mc_count > multicast_filter_limit) {
		new_rx_mode = 1;
	} else
		new_rx_mode = 0;

	if (sp->cur_tx - sp->dirty_tx >= TX_RING_SIZE - 1) {
	  /* The Tx ring is full -- don't add anything!  Presumably the new mode
		 is in config_cmd_data and will be added anyway. */
		sp->rx_mode = -1;
		return;
	}

	if (new_rx_mode != sp->rx_mode) {
		u8 *config_cmd_data;

		spin_lock_irqsave(&sp->lock, flags);
		entry = sp->cur_tx++ % TX_RING_SIZE;
		last_cmd = sp->last_cmd;
		sp->last_cmd = (struct descriptor *)&sp->tx_ring[entry];

		sp->tx_skbuff[entry] = 0;			/* Redundant. */
		sp->tx_ring[entry].status = (CmdSuspend | CmdConfigure) << 16;
		sp->tx_ring[entry].link =
			virt_to_bus(&sp->tx_ring[(entry + 1) % TX_RING_SIZE]);
		config_cmd_data = (void *)&sp->tx_ring[entry].tx_desc_addr;
		/* Construct a full CmdConfig frame. */
		memcpy(config_cmd_data, i82558_config_cmd, sizeof(i82558_config_cmd));
		config_cmd_data[1] = (txfifo << 4) | rxfifo;
		config_cmd_data[4] = rxdmacount;
		config_cmd_data[5] = txdmacount + 0x80;
		config_cmd_data[15] |= (new_rx_mode & 2) ? 1 : 0;
		config_cmd_data[19] |= sp->full_duplex ? 0x40 : 0;
		config_cmd_data[21] = (new_rx_mode & 1) ? 0x0D : 0x05;
		if (sp->phy[0] & 0x8000) {			/* Use the AUI port instead. */
			config_cmd_data[15] |= 0x80;
			config_cmd_data[8] = 0;
		}
		/* Trigger the command unit resume. */
		last_cmd->command &= ~CmdSuspend;

		wait_for_cmd_done(ioaddr + SCBCmd);
		outw(CU_RESUME, ioaddr + SCBCmd);

		spin_unlock_irqrestore(&sp->lock, flags);
	}

	if (new_rx_mode == 0  &&  dev->mc_count < 4) {
		/* The simple case of 0-3 multicast list entries occurs often, and
		   fits within one tx_ring[] entry. */
		struct dev_mc_list *mclist;
		u16 *setup_params, *eaddrs;

		spin_lock_irqsave(&sp->lock, flags);
		entry = sp->cur_tx++ % TX_RING_SIZE;
		last_cmd = sp->last_cmd;
		sp->last_cmd = (struct descriptor *)&sp->tx_ring[entry];

		sp->tx_skbuff[entry] = 0;
		sp->tx_ring[entry].status = (CmdSuspend | CmdMulticastList) << 16;
		sp->tx_ring[entry].link =
			virt_to_bus(&sp->tx_ring[(entry + 1) % TX_RING_SIZE]);
		sp->tx_ring[entry].tx_desc_addr = 0; /* Really MC list count. */
		setup_params = (u16 *)&sp->tx_ring[entry].tx_desc_addr;
		*setup_params++ = dev->mc_count*6;
		/* Fill in the multicast addresses. */
		for (i = 0, mclist = dev->mc_list; i < dev->mc_count;
			 i++, mclist = mclist->next) {
			eaddrs = (u16 *)mclist->dmi_addr;
			*setup_params++ = *eaddrs++;
			*setup_params++ = *eaddrs++;
			*setup_params++ = *eaddrs++;
		}

		last_cmd->command &= ~CmdSuspend;

		/* Immediately trigger the command unit resume. */
		wait_for_cmd_done(ioaddr + SCBCmd);
		outw(CU_RESUME, ioaddr + SCBCmd);

		spin_unlock_irqrestore(&sp->lock, flags);
	} else if (new_rx_mode == 0) {
		struct dev_mc_list *mclist;
		u16 *setup_params, *eaddrs;
		struct descriptor *mc_setup_frm = sp->mc_setup_frm;
		int i;

		if (sp->mc_setup_frm_len < 10 + dev->mc_count*6
			|| sp->mc_setup_frm == NULL) {
			/* Allocate a full setup frame, 10bytes + <max addrs>. */
			if (sp->mc_setup_frm)
				kfree(sp->mc_setup_frm);
			sp->mc_setup_busy = 0;
			sp->mc_setup_frm_len = 10 + multicast_filter_limit*6;
			sp->mc_setup_frm = kmalloc(sp->mc_setup_frm_len, GFP_ATOMIC);
			if (sp->mc_setup_frm == NULL) {
				printk(KERN_ERR "%s: Failed to allocate a setup frame.\n",
					   dev->name);
				sp->rx_mode = -1; /* We failed, try again. */
				return;
			}
		}
		/* If we are busy, someone might be quickly adding to the MC list.
		   Try again later when the list changes stop. */
		if (sp->mc_setup_busy) {
			sp->rx_mode = -1;
			return;
		}
		mc_setup_frm = sp->mc_setup_frm;
		/* Fill the setup frame. */
		if (speedo_debug > 1)
			printk(KERN_DEBUG "%s: Constructing a setup frame at %p, "
				   "%d bytes.\n",
				   dev->name, sp->mc_setup_frm, sp->mc_setup_frm_len);
		mc_setup_frm->status = 0;
		mc_setup_frm->command = CmdSuspend | CmdIntr | CmdMulticastList;
		/* Link set below. */
		setup_params = (u16 *)&mc_setup_frm->params;
		*setup_params++ = dev->mc_count*6;
		/* Fill in the multicast addresses. */
		for (i = 0, mclist = dev->mc_list; i < dev->mc_count;
			 i++, mclist = mclist->next) {
			eaddrs = (u16 *)mclist->dmi_addr;
			*setup_params++ = *eaddrs++;
			*setup_params++ = *eaddrs++;
			*setup_params++ = *eaddrs++;
		}

		/* Disable interrupts while playing with the Tx Cmd list. */
		spin_lock_irqsave(&sp->lock, flags);

		entry = sp->cur_tx++ % TX_RING_SIZE;
		last_cmd = sp->last_cmd;
		sp->last_cmd = mc_setup_frm;
		sp->mc_setup_busy++;

		/* Change the command to a NoOp, pointing to the CmdMulti command. */
		sp->tx_skbuff[entry] = 0;
		sp->tx_ring[entry].status = CmdNOp << 16;
		sp->tx_ring[entry].link = virt_to_bus(mc_setup_frm);

		/* Set the link in the setup frame. */
		mc_setup_frm->link =
			virt_to_bus(&(sp->tx_ring[(entry+1) % TX_RING_SIZE]));

		last_cmd->command &= ~CmdSuspend;

		/* Immediately trigger the command unit resume. */
		wait_for_cmd_done(ioaddr + SCBCmd);
		outw(CU_RESUME, ioaddr + SCBCmd);

		spin_unlock_irqrestore(&sp->lock, flags);

		if (speedo_debug > 5)
			printk(" CmdMCSetup frame length %d in entry %d.\n",
				   dev->mc_count, entry);
	}

	sp->rx_mode = new_rx_mode;
}

#ifdef MODULE

int
init_module(void)
{
	int cards_found;

	if (debug >= 0)
		speedo_debug = debug;
	if (speedo_debug)
		printk(KERN_INFO "%s", version);

	root_speedo_dev = NULL;
	cards_found = eepro100_init(NULL);
	return cards_found ? 0 : -ENODEV;
}

void
cleanup_module(void)
{
	struct device *next_dev;

	/* No need to check MOD_IN_USE, as sys_delete_module() checks. */
	while (root_speedo_dev) {
		next_dev = ((struct speedo_private *)root_speedo_dev->priv)->next_module;
		unregister_netdev(root_speedo_dev);
		release_region(root_speedo_dev->base_addr, SPEEDO3_TOTAL_SIZE);
		kfree(root_speedo_dev);
		root_speedo_dev = next_dev;
	}
}
#else   /* not MODULE */
int eepro100_probe(struct device *dev)
{
	int cards_found = 0;

	cards_found = eepro100_init(dev);

	if (speedo_debug > 0  &&  cards_found)
		printk(version);

	return cards_found ? 0 : -ENODEV;
}
#endif  /* MODULE */

/*
 * Local variables:
 *  compile-command: "gcc -DMODULE -D__KERNEL__ -I/usr/src/linux/net/inet -Wall -Wstrict-prototypes -O6 -c eepro100.c `[ -f /usr/include/linux/modversions.h ] && echo -DMODVERSIONS`"
 *  SMP-compile-command: "gcc -D__SMP__ -DMODULE -D__KERNEL__ -I/usr/src/linux/net/inet -Wall -Wstrict-prototypes -O6 -c eepro100.c `[ -f /usr/include/linux/modversions.h ] && echo -DMODVERSIONS`"
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 * End:
 */
