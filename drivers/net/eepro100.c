/* drivers/net/eepro100.c: An Intel i82557-559 Ethernet driver for Linux. */
/*
   NOTICE: this version tested with kernels 1.3.72 and later only!
	Written 1996-1999 by Donald Becker.

	This software may be used and distributed according to the terms
	of the GNU Public License, incorporated herein by reference.

	This driver is for the Intel EtherExpress Pro100 (Speedo3) design.
	It should work with all i82557/558/559 boards.

	To use as a module, use the compile-command at the end of the file.

	The author may be reached as becker@CESDIS.usra.edu, or C/O
	Center of Excellence in Space Data and Information Sciences
	   Code 930.5, NASA Goddard Space Flight Center, Greenbelt MD 20771
	For updates see
		http://cesdis.gsfc.nasa.gov/linux/drivers/eepro100.html
	For installation instructions
		http://cesdis.gsfc.nasa.gov/linux/misc/modules.html
	There is a Majordomo mailing list based at
		linux-eepro100@cesdis.gsfc.nasa.gov
*/

static const char *version =
"eepro100.c:v1.09j 7/27/99 Donald Becker http://cesdis.gsfc.nasa.gov/linux/drivers/eepro100.html\n";

/* A few user-configurable values that apply to all boards.
   First set is undocumented and spelled per Intel recommendations. */

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
static int max_interrupt_work = 20;

/* Maximum number of multicast addresses to filter (vs. rx-all-multicast) */
static int multicast_filter_limit = 64;

/* 'options' is used to pass a transceiver override or full-duplex flag
   e.g. "options=16" for FD, "options=32" for 100mbps-only. */
static int full_duplex[] = {-1, -1, -1, -1, -1, -1, -1, -1};
static int options[] = {-1, -1, -1, -1, -1, -1, -1, -1};
static int debug = -1;			/* The debug level */

/* A few values that may be tweaked. */
/* The ring sizes should be a power of two for efficiency. */
#define TX_RING_SIZE	32		/* Effectively 2 entries fewer. */
#define RX_RING_SIZE	32
/* Actual number of TX packets queued, must be <= TX_RING_SIZE-2. */
#define TX_QUEUE_LIMIT  12

/* Operational parameters that usually are not changed. */

/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT  (2*HZ)
/* Size of an pre-allocated Rx buffer: <Ethernet MTU> + slack.*/
#define PKT_BUF_SZ		1536

#if !defined(__OPTIMIZE__)  ||  !defined(__KERNEL__)
#warning  You must compile this file with the correct options!
#warning  See the last lines of the source file.
#error You must compile this driver with "-O".
#endif

#include <linux/version.h>
#include <linux/module.h>
#ifdef MODVERSIONS
#include <linux/modversions.h>
#endif

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#ifdef HAS_PCI_NETIF
#include "pci-netif.h"
#else
#include <linux/pci.h>
#if LINUX_VERSION_CODE < 0x20155
#include <linux/bios32.h>		/* Ignore the bogus warning in 2.1.100+ */
#endif
#endif
#include <linux/spinlock.h>
#include <linux/init.h>
#include <asm/bitops.h>
#include <asm/io.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/delay.h>

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

#if (LINUX_VERSION_CODE < 0x20123)
#define test_and_set_bit(val, addr) set_bit(val, addr)
#define le16_to_cpu(val) (val)
#define cpu_to_le16(val) (val)
#define le32_to_cpu(val) (val)
#define cpu_to_le32(val) (val)
#define spin_lock_irqsave(&sp->lock, flags)	save_flags(flags); cli();
#define spin_unlock_irqrestore(&sp->lock, flags); restore_flags(flags);
#endif
#if LINUX_VERSION_CODE < 0x20159
#define dev_free_skb(skb) dev_kfree_skb(skb, FREE_WRITE);
#else
#define dev_free_skb(skb) dev_kfree_skb(skb);
#endif
#if ! defined(CAP_NET_ADMIN)
#define capable(CAP_XXX) (suser())
#endif
#if ! defined(HAS_NETIF_QUEUE)
#define netif_wake_queue(dev)  mark_bh(NET_BH);
#endif

/* The total I/O port extent of the board.
   The registers beyond 0x18 only exist on the i82558. */
#define SPEEDO3_TOTAL_SIZE 0x20

int speedo_debug = 1;

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

/* This table drives the PCI probe routines. */
static struct net_device *speedo_found1(int pci_bus, int pci_devfn, long ioaddr, int irq, int chip_idx, int fnd_cnt);

#ifdef USE_IO
#define SPEEDO_IOTYPE   PCI_USES_MASTER|PCI_USES_IO|PCI_ADDR1
#define SPEEDO_SIZE		32
#else
#define SPEEDO_IOTYPE   PCI_USES_MASTER|PCI_USES_MEM|PCI_ADDR0
#define SPEEDO_SIZE		0x1000
#endif

#if defined(HAS_PCI_NETIF)
struct pci_id_info static pci_tbl[] = {
	{ "Intel PCI EtherExpress Pro100",
	  { 0x12298086, 0xffffffff,}, SPEEDO_IOTYPE, SPEEDO_SIZE,
	  0, speedo_found1 },
	{0,},						/* 0 terminated list. */
};
#else
enum pci_flags_bit {
	PCI_USES_IO=1, PCI_USES_MEM=2, PCI_USES_MASTER=4,
	PCI_ADDR0=0x10<<0, PCI_ADDR1=0x10<<1, PCI_ADDR2=0x10<<2, PCI_ADDR3=0x10<<3,
};
struct pci_id_info {
	const char *name;
	u16	vendor_id, device_id, device_id_mask, flags;
	int io_size;
	struct net_device *(*probe1)(int pci_bus, int pci_devfn, long ioaddr, int irq, int chip_idx, int fnd_cnt);
} static pci_tbl[] = {
	{ "Intel PCI EtherExpress Pro100",
	  0x8086, 0x1229, 0xffff, PCI_USES_IO|PCI_USES_MASTER, 32, speedo_found1 },
	{0,},						/* 0 terminated list. */
};
#endif

#ifndef USE_IO
#define inb readb
#define inw readw
#define inl readl
#define outb writeb
#define outw writew
#define outl writel
#endif

/* How to wait for the command unit to accept a command.
   Typically this takes 0 ticks. */
static inline void wait_for_cmd_done(long cmd_ioaddr)
{
	int wait = 100;
	do   ;
	while(inb(cmd_ioaddr) && --wait >= 0);
}

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
	CmdNOp = 0, CmdIASetup = 0x10000, CmdConfigure = 0x20000,
	CmdMulticastList = 0x30000, CmdTx = 0x40000, CmdTDR = 0x50000,
	CmdDump = 0x60000, CmdDiagnose = 0x70000,
	CmdSuspend = 0x40000000,	/* Suspend after completion. */
	CmdIntr = 0x20000000,		/* Interrupt after completion. */
	CmdTxFlex = 0x00080000,		/* Use "Flexible mode" for CmdTx command. */
};
/* Do atomically if possible. */
#if defined(__i386__) || defined(__alpha__) || defined(__ia64__)
#define clear_suspend(cmd)	clear_bit(30, &(cmd)->cmd_status)
#elif defined(__powerpc__)
#define clear_suspend(cmd)	clear_bit(6, &(cmd)->cmd_status)
#else
#if 0
# error You are probably in trouble: clear_suspend() MUST be atomic.
#endif
# define clear_suspend(cmd)	(cmd)->cmd_status &= cpu_to_le32(~CmdSuspend)
#endif

enum SCBCmdBits {
     SCBMaskCmdDone=0x8000, SCBMaskRxDone=0x4000, SCBMaskCmdIdle=0x2000,
     SCBMaskRxSuspend=0x1000, SCBMaskEarlyRx=0x0800, SCBMaskFlowCtl=0x0400,
     SCBTriggerIntr=0x0200, SCBMaskAll=0x0100,
     /* The rest are Rx and Tx commands. */
     CUStart=0x0010, CUResume=0x0020, CUStatsAddr=0x0040, CUShowStats=0x0050,
     CUCmdBase=0x0060,  /* CU Base address (set to zero) . */
     CUDumpStats=0x0070, /* Dump then reset stats counters. */
     RxStart=0x0001, RxResume=0x0002, RxAbort=0x0004, RxAddrLoad=0x0006,
     RxResumeNoResources=0x0007,
};

enum SCBPort_cmds {
	PortReset=0, PortSelfTest=1, PortPartialReset=2, PortDump=3,
};

/* The Speedo3 Rx and Tx frame/buffer descriptors. */
struct descriptor {			/* A generic descriptor. */
	s32 cmd_status;			/* All command and status fields. */
	u32 link;					/* struct descriptor *  */
	unsigned char params[0];
};

/* The Speedo3 Rx and Tx buffer descriptors. */
struct RxFD {					/* Receive frame descriptor. */
	s32 status;
	u32 link;					/* struct RxFD * */
	u32 rx_buf_addr;			/* void * */
	u32 count;
};

/* Selected elements of the Tx/RxFD.status word. */
enum RxFD_bits {
	RxComplete=0x8000, RxOK=0x2000,
	RxErrCRC=0x0800, RxErrAlign=0x0400, RxErrTooBig=0x0200, RxErrSymbol=0x0010,
	RxEth2Type=0x0020, RxNoMatch=0x0004, RxNoIAMatch=0x0002,
	TxUnderrun=0x1000,  StatusComplete=0x8000,
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

/* Do not change the position (alignment) of the first few elements!
   The later elements are grouped for cache locality. */
struct speedo_private {
	struct TxFD	*tx_ring;		/* Commands (usually CmdTxPacket). */
	struct RxFD *rx_ringp[RX_RING_SIZE];	/* Rx descriptor, used as ring. */
	/* The addresses of a Tx/Rx-in-place packets/buffers. */
	struct sk_buff* tx_skbuff[TX_RING_SIZE];
	struct sk_buff* rx_skbuff[RX_RING_SIZE];
	dma_addr_t	rx_ring_dma[RX_RING_SIZE];
	dma_addr_t	tx_ring_dma;
	struct descriptor  *last_cmd;	/* Last command sent. */
	unsigned int cur_tx, dirty_tx;	/* The ring entries to be free()ed. */
	spinlock_t lock;				/* Group with Tx control cache line. */
	u32 tx_threshold;					/* The value for txdesc.count. */
	struct RxFD *last_rxf;	/* Last command sent. */
	unsigned int cur_rx, dirty_rx;		/* The next free ring entry */
	long last_rx_time;			/* Last Rx, in jiffies, to handle Rx hang. */
	const char *product_name;
	struct net_device *next_module;
	void *priv_addr;					/* Unaligned address for kfree */
	struct enet_statistics stats;
	struct speedo_stats *lstats;
	int chip_id;
	unsigned char pci_bus, pci_devfn, acpi_pwr;
	struct pci_dev *pdev;
	struct timer_list timer;	/* Media selection timer. */
	int mc_setup_frm_len;			 	/* The length of an allocated.. */
	struct descriptor *mc_setup_frm; 	/* ..multicast setup frame. */
	int mc_setup_busy;					/* Avoid double-use of setup frame. */
	dma_addr_t mc_setup_dma;
	char rx_mode;						/* Current PROMISC/ALLMULTI setting. */
	unsigned int tx_full:1;				/* The Tx queue is full. */
	unsigned int full_duplex:1;			/* Full-duplex operation requested. */
	unsigned int flow_ctrl:1;			/* Use 802.3x flow control. */
	unsigned int rx_bug:1;				/* Work around receiver hang errata. */
	unsigned int rx_bug10:1;			/* Receiver might hang at 10mbps. */
	unsigned int rx_bug100:1;			/* Receiver might hang at 100mbps. */
	unsigned char default_port:8;		/* Last dev->if_port value. */
	unsigned short phy[2];				/* PHY media interfaces available. */
	unsigned short advertising;			/* Current PHY advertised caps. */
	unsigned short partner;				/* Link partner caps. */
};

/* The parameters for a CmdConfigure operation.
   There are so many options that it would be difficult to document each bit.
   We mostly use the default or recommended settings. */
const char i82557_config_cmd[22] = {
	22, 0x08, 0, 0,  0, 0, 0x32, 0x03,  1, /* 1=Use MII  0=Use AUI */
	0, 0x2E, 0,  0x60, 0,
	0xf2, 0x48,   0, 0x40, 0xf2, 0x80, 		/* 0x40=Force full-duplex */
	0x3f, 0x05, };
const char i82558_config_cmd[22] = {
	22, 0x08, 0, 1,  0, 0, 0x22, 0x03,  1, /* 1=Use MII  0=Use AUI */
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
#define EE_READ_CMD		(6)

static int do_eeprom_cmd(long ioaddr, int cmd, int cmd_len);
static int mdio_read(long ioaddr, int phy_id, int location);
static int mdio_write(long ioaddr, int phy_id, int location, int value);
static int speedo_open(struct net_device *dev);
static void speedo_resume(struct net_device *dev);
static void speedo_timer(unsigned long data);
static void speedo_init_rx_ring(struct net_device *dev);
static void speedo_tx_timeout(struct net_device *dev);
static int speedo_start_xmit(struct sk_buff *skb, struct net_device *dev);
static int speedo_rx(struct net_device *dev);
static void speedo_interrupt(int irq, void *dev_instance, struct pt_regs *regs);
static int speedo_close(struct net_device *dev);
static struct enet_statistics *speedo_get_stats(struct net_device *dev);
static int speedo_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
static void set_rx_mode(struct net_device *dev);



#ifdef honor_default_port
/* Optional driver feature to allow forcing the transceiver setting.
   Not recommended. */
static int mii_ctrl[8] = { 0x3300, 0x3100, 0x0000, 0x0100,
						   0x2000, 0x2100, 0x0400, 0x3100};
#endif

/* A list of all installed Speedo devices, for removing the driver module. */
static struct net_device *root_speedo_dev = NULL;

#if ! defined(HAS_PCI_NETIF)
int eepro100_init(void)
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
#if LINUX_VERSION_CODE >= 0x20155  ||  PCI_SUPPORT_1
		{
			struct pci_dev *pdev = pci_find_slot(pci_bus, pci_device_fn);
#ifdef USE_IO
			ioaddr = pdev->resource[1].start;
#else
			ioaddr = pdev->resource[0].start;
#endif
			irq = pdev->irq;
		}
#else
		{
			u32 pciaddr;
			u8 pci_irq_line;
			pcibios_read_config_byte(pci_bus, pci_device_fn,
									 PCI_INTERRUPT_LINE, &pci_irq_line);
			/* Note: BASE_ADDRESS_0 is for memory-mapping the registers. */
#ifdef USE_IO
			pcibios_read_config_dword(pci_bus, pci_device_fn,
									  PCI_BASE_ADDRESS_1, &pciaddr);
			pciaddr &= ~3UL;
#else
			pcibios_read_config_dword(pci_bus, pci_device_fn,
									  PCI_BASE_ADDRESS_0, &pciaddr);
#endif
			ioaddr = pciaddr;
			irq = pci_irq_line;
		}
#endif
		/* Remove I/O space marker in bit 0. */
#ifdef USE_IO
		if (check_region(ioaddr, 32))
			continue;
#else
		{
			unsigned long orig_ioaddr = ioaddr;

			if ((ioaddr = (long)ioremap(ioaddr & ~0xfUL, 0x1000)) == 0) {
				printk(KERN_INFO "Failed to map PCI address %#lx.\n",
					   orig_ioaddr);
				continue;
			}
		}
#endif
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

		if(speedo_found1(pci_bus, pci_device_fn, ioaddr, irq, 0,cards_found))
			cards_found++;
	}

	return cards_found;
}
#endif

static struct net_device *speedo_found1(int pci_bus, int pci_devfn, 
			  long ioaddr, int irq, int chip_idx, int card_idx)
{
	struct net_device *dev;
	struct speedo_private *sp;
	struct pci_dev *pdev;
	unsigned char *tx_ring;
	dma_addr_t tx_ring_dma;
	const char *product;
	int i, option;
	u16 eeprom[0x100];
	int acpi_idle_state = 0;

	static int did_version = 0;			/* Already printed version info. */
	if (speedo_debug > 0  &&  did_version++ == 0)
		printk(version);

	pdev = pci_find_slot(pci_bus, pci_devfn);

	tx_ring = pci_alloc_consistent(pdev, TX_RING_SIZE * sizeof(struct TxFD)
					     + sizeof(struct speedo_stats), &tx_ring_dma);
	if (!tx_ring) {
		printk(KERN_ERR "Could not allocate DMA memory.\n");
		return NULL;
	}

	dev = init_etherdev(NULL, sizeof(struct speedo_private));
	if (dev == NULL) {
		pci_free_consistent(pdev, TX_RING_SIZE * sizeof(struct TxFD)
					  + sizeof(struct speedo_stats),
				    tx_ring, tx_ring_dma);
		return NULL;
	}

	if (dev->mem_start > 0)
		option = dev->mem_start;
	else if (card_idx >= 0  &&  options[card_idx] >= 0)
		option = options[card_idx];
	else
		option = 0;

#if defined(HAS_PCI_NETIF)
	acpi_idle_state = acpi_set_pwr_state(pci_bus, pci_devfn, ACPI_D0);
#endif

	/* Read the station address EEPROM before doing the reset.
	   Nominally his should even be done before accepting the device, but
	   then we wouldn't have a device name with which to report the error.
	   The size test is for 6 bit vs. 8 bit address serial EEPROMs.
	*/
	{
		u16 sum = 0;
		int j;
		int read_cmd, ee_size;

		if ((do_eeprom_cmd(ioaddr, EE_READ_CMD << 24, 27) & 0xffe0000)
			== 0xffe0000) {
			ee_size = 0x100;
			read_cmd = EE_READ_CMD << 24;
		} else {
			ee_size = 0x40;
			read_cmd = EE_READ_CMD << 22;
		}

		for (j = 0, i = 0; i < ee_size; i++) {
			u16 value = do_eeprom_cmd(ioaddr, read_cmd | (i << 16), 27);
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
	outl(PortReset, ioaddr + SCBPort);

	if (eeprom[3] & 0x0100)
		product = "OEM i82557/i82558 10/100 Ethernet";
	else
		product = pci_tbl[chip_idx].name;

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
		volatile s32 *self_test_results = (volatile s32 *)tx_ring;
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

		/* Perform a system self-test. Use the tx_ring consistent DMA mapping for it. */
		self_test_results[0] = 0;
		self_test_results[1] = -1;
		outl(tx_ring_dma | PortSelfTest, ioaddr + SCBPort);
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

	outl(PortReset, ioaddr + SCBPort);
#if defined(HAS_PCI_NETIF)
	/* Return the chip to its original power state. */
	acpi_set_pwr_state(pci_bus, pci_devfn, acpi_idle_state);
#endif

	/* We do a request_region() only to register /proc/ioports info. */
	request_region(ioaddr, SPEEDO3_TOTAL_SIZE, "Intel Speedo3 Ethernet");

	dev->base_addr = ioaddr;
	dev->irq = irq;

	sp = dev->priv;
	if (dev->priv == NULL) {
		void *mem = kmalloc(sizeof(*sp), GFP_KERNEL);
		dev->priv = sp = mem;		/* Cache align here if kmalloc does not. */
		sp->priv_addr = mem;
	}
	memset(sp, 0, sizeof(*sp));
	sp->next_module = root_speedo_dev;
	root_speedo_dev = dev;

	sp->pci_bus = pci_bus;
	sp->pci_devfn = pci_devfn;
	sp->pdev = pdev;
	sp->chip_id = chip_idx;
	sp->acpi_pwr = acpi_idle_state;
	sp->tx_ring = (struct TxFD *)tx_ring;
	sp->tx_ring_dma = tx_ring_dma;
	sp->lstats = (struct speedo_stats *)(sp->tx_ring + TX_RING_SIZE);
	
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
	dev->tx_timeout = &speedo_tx_timeout;
	dev->watchdog_timeo = TX_TIMEOUT;
	dev->stop = &speedo_close;
	dev->get_stats = &speedo_get_stats;
	dev->set_multicast_list = &set_rx_mode;
	dev->do_ioctl = &speedo_ioctl;

	return dev;
}

/* Serial EEPROM section.
   A "bit" grungy, but we work our way through bit-by-bit :->. */
/*  EEPROM_Ctrl bits. */
#define EE_SHIFT_CLK	0x01	/* EEPROM shift clock. */
#define EE_CS			0x02	/* EEPROM chip select. */
#define EE_DATA_WRITE	0x04	/* EEPROM chip data in. */
#define EE_DATA_READ	0x08	/* EEPROM chip data out. */
#define EE_ENB			(0x4800 | EE_CS)
#define EE_WRITE_0		0x4802
#define EE_WRITE_1		0x4806
#define EE_OFFSET		SCBeeprom

/* Delay between EEPROM clock transitions.
   The code works with no delay on 33Mhz PCI.  */
#define eeprom_delay()	inw(ee_addr)

static int do_eeprom_cmd(long ioaddr, int cmd, int cmd_len)
{
	unsigned retval = 0;
	long ee_addr = ioaddr + SCBeeprom;

	outw(EE_ENB | EE_SHIFT_CLK, ee_addr);

	/* Shift the command bits out. */
	do {
		short dataval = (cmd & (1 << cmd_len)) ? EE_WRITE_1 : EE_WRITE_0;
		outw(dataval, ee_addr);
		eeprom_delay();
		outw(dataval | EE_SHIFT_CLK, ee_addr);
		eeprom_delay();
		retval = (retval << 1) | ((inw(ee_addr) & EE_DATA_READ) ? 1 : 0);
	} while (--cmd_len >= 0);
	outw(EE_ENB, ee_addr);

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
			break;
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
			break;
		}
	} while (! (val & 0x10000000));
	return val & 0xffff;
}


static int
speedo_open(struct net_device *dev)
{
	struct speedo_private *sp = (struct speedo_private *)dev->priv;
	long ioaddr = dev->base_addr;

#if defined(HAS_PCI_NETIF)
	acpi_set_pwr_state(sp->pci_bus, sp->pci_devfn, ACPI_D0);
#endif

	if (speedo_debug > 1)
		printk(KERN_DEBUG "%s: speedo_open() irq %d.\n", dev->name, dev->irq);

	/* Set up the Tx queue early.. */
	sp->cur_tx = 0;
	sp->dirty_tx = 0;
	sp->last_cmd = 0;
	sp->tx_full = 0;
	spin_lock_init(&sp->lock);

	/* .. we can safely take handler calls during init. */
	if (request_irq(dev->irq, &speedo_interrupt, SA_SHIRQ, dev->name, dev)) {
		return -EAGAIN;
	}
	MOD_INC_USE_COUNT;

	dev->if_port = sp->default_port;
#if 0
	/* With some transceivers we must retrigger negotiation to reset
	   power-up errors. */
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

	speedo_init_rx_ring(dev);

	/* Fire up the hardware. */
	speedo_resume(dev);

	clear_bit(LINK_STATE_RXSEM, &dev->state);
	netif_start_queue(dev);

	/* Setup the chip and configure the multicast list. */
	sp->mc_setup_frm = NULL;
	sp->mc_setup_frm_len = 0;
	sp->mc_setup_busy = 0;
	sp->rx_mode = -1;			/* Invalid -> always reset the mode. */
	sp->flow_ctrl = sp->partner = 0;
	set_rx_mode(dev);
	if ((sp->phy[0] & 0x8000) == 0)
		sp->advertising = mdio_read(ioaddr, sp->phy[0] & 0x1f, 4);

	if (speedo_debug > 2) {
		printk(KERN_DEBUG "%s: Done speedo_open(), status %8.8x.\n",
			   dev->name, inw(ioaddr + SCBStatus));
	}

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

	/* No need to wait for the command unit to accept here. */
	if ((sp->phy[0] & 0x8000) == 0)
		mdio_read(ioaddr, sp->phy[0] & 0x1f, 0);
	return 0;
}

/* Start the chip hardware after a full reset. */
static void speedo_resume(struct net_device *dev)
{
	struct speedo_private *sp = (struct speedo_private *)dev->priv;
	long ioaddr = dev->base_addr;

	outw(SCBMaskAll, ioaddr + SCBCmd);

	/* Start with a Tx threshold of 256 (0x..20.... 8 byte units). */
	sp->tx_threshold = 0x01208000;

	/* Set the segment registers to '0'. */
	wait_for_cmd_done(ioaddr + SCBCmd);
	outl(0, ioaddr + SCBPointer);
	outb(RxAddrLoad, ioaddr + SCBCmd);
	wait_for_cmd_done(ioaddr + SCBCmd);
	outb(CUCmdBase, ioaddr + SCBCmd);
	wait_for_cmd_done(ioaddr + SCBCmd);

	/* Load the statistics block and rx ring addresses. */
	outl(sp->tx_ring_dma + sizeof(struct TxFD) * TX_RING_SIZE, ioaddr + SCBPointer);
	outb(CUStatsAddr, ioaddr + SCBCmd);
	sp->lstats->done_marker = 0;
	wait_for_cmd_done(ioaddr + SCBCmd);

	outl(sp->rx_ring_dma[sp->cur_rx % RX_RING_SIZE],
		 ioaddr + SCBPointer);
	outb(RxStart, ioaddr + SCBCmd);
	wait_for_cmd_done(ioaddr + SCBCmd);

	outb(CUDumpStats, ioaddr + SCBCmd);

	/* Fill the first command with our physical address. */
	{
		int entry = sp->cur_tx++ % TX_RING_SIZE;
		struct descriptor *cur_cmd = (struct descriptor *)&sp->tx_ring[entry];

		/* Avoid a bug(?!) here by marking the command already completed. */
		cur_cmd->cmd_status = cpu_to_le32((CmdSuspend | CmdIASetup) | 0xa000);
		cur_cmd->link =
			cpu_to_le32(sp->tx_ring_dma + (sp->cur_tx % TX_RING_SIZE)
						      * sizeof(struct TxFD));
		memcpy(cur_cmd->params, dev->dev_addr, 6);
		if (sp->last_cmd)
			clear_suspend(sp->last_cmd);
		sp->last_cmd = cur_cmd;
	}

	/* Start the chip's Tx process and unmask interrupts. */
	wait_for_cmd_done(ioaddr + SCBCmd);
	outl(sp->tx_ring_dma
	     + (sp->dirty_tx % TX_RING_SIZE) * sizeof(struct TxFD),
		 ioaddr + SCBPointer);
	outw(CUStart, ioaddr + SCBCmd);
}

/* Media monitoring and control. */
static void speedo_timer(unsigned long data)
{
	struct net_device *dev = (struct net_device *)data;
	struct speedo_private *sp = (struct speedo_private *)dev->priv;
	long ioaddr = dev->base_addr;
	int phy_num = sp->phy[0] & 0x1f;

	/* We have MII and lost link beat. */
	if ((sp->phy[0] & 0x8000) == 0) {
		int partner = mdio_read(ioaddr, phy_num, 5);
		if (partner != sp->partner) {
			int flow_ctrl = sp->advertising & partner & 0x0400 ? 1 : 0;
			sp->partner = partner;
			if (flow_ctrl != sp->flow_ctrl) {
				sp->flow_ctrl = flow_ctrl;
				sp->rx_mode = -1;	/* Trigger a reload. */
			}
			/* Clear sticky bit. */
			mdio_read(ioaddr, phy_num, 1);
			/* If link beat has returned... */
			if (mdio_read(ioaddr, phy_num, 1) & 0x0004)
				dev->flags |= IFF_RUNNING; 
			else
				dev->flags &= ~IFF_RUNNING;
		}
	}

	if (speedo_debug > 3) {
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

static void speedo_show_state(struct net_device *dev)
{
	struct speedo_private *sp = (struct speedo_private *)dev->priv;
	long ioaddr = dev->base_addr;
	int phy_num = sp->phy[0] & 0x1f;
	int i;

	/* Print a few items for debugging. */
	if (speedo_debug > 0) {
		int i;
		printk(KERN_DEBUG "%s: Tx ring dump,  Tx queue %d / %d:\n", dev->name,
			   sp->cur_tx, sp->dirty_tx);
		for (i = 0; i < TX_RING_SIZE; i++)
			printk(KERN_DEBUG "%s: %c%c%d %8.8x.\n", dev->name,
				   i == sp->dirty_tx % TX_RING_SIZE ? '*' : ' ',
				   i == sp->cur_tx % TX_RING_SIZE ? '=' : ' ',
				   i, sp->tx_ring[i].status);
	}
	printk(KERN_DEBUG "%s:Printing Rx ring (next to receive into %d).\n",
		   dev->name, sp->cur_rx);

	for (i = 0; i < RX_RING_SIZE; i++)
		printk(KERN_DEBUG "  Rx ring entry %d  %8.8x.\n",
			   i, (int)sp->rx_ringp[i]->status);

	for (i = 0; i < 16; i++) {
		if (i == 6) i = 21;
		printk(KERN_DEBUG "  PHY index %d register %d is %4.4x.\n",
			   phy_num, i, mdio_read(ioaddr, phy_num, i));
	}

}

/* Initialize the Rx and Tx rings, along with various 'dev' bits. */
static void
speedo_init_rx_ring(struct net_device *dev)
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
		sp->rx_ring_dma[i] =
			pci_map_single(sp->pdev, rxf, PKT_BUF_SZ + sizeof(struct RxFD));
		skb_reserve(skb, sizeof(struct RxFD));
		if (last_rxf)
			last_rxf->link = cpu_to_le32(sp->rx_ring_dma[i]);
		last_rxf = rxf;
		rxf->status = cpu_to_le32(0x00000001);	/* '1' is flag value only. */
		rxf->link = 0;						/* None yet. */
		/* This field unused by i82557. */
		rxf->rx_buf_addr = 0xffffffff;
		rxf->count = cpu_to_le32(PKT_BUF_SZ << 16);
	}
	sp->dirty_rx = (unsigned int)(i - RX_RING_SIZE);
	/* Mark the last entry as end-of-list. */
	last_rxf->status = cpu_to_le32(0xC0000002);	/* '2' is flag value only. */
	sp->last_rxf = last_rxf;
}

static void speedo_tx_timeout(struct net_device *dev)
{
	struct speedo_private *sp = (struct speedo_private *)dev->priv;
	long ioaddr = dev->base_addr;
	int status = inw(ioaddr + SCBStatus);

	/* Trigger a stats dump to give time before the reset. */
	speedo_get_stats(dev);

	printk(KERN_WARNING "%s: Transmit timed out: status %4.4x "
		   " %4.4x at %d/%d command %8.8x.\n",
		   dev->name, status, inw(ioaddr + SCBCmd),
		   sp->dirty_tx, sp->cur_tx,
		   sp->tx_ring[sp->dirty_tx % TX_RING_SIZE].status);
	speedo_show_state(dev);
	if ((status & 0x00C0) != 0x0080
		&&  (status & 0x003C) == 0x0010) {
		/* Only the command unit has stopped. */
		printk(KERN_WARNING "%s: Trying to restart the transmitter...\n",
			   dev->name);
		outl(sp->tx_ring_dma
		     + (sp->dirty_tx % TX_RING_SIZE) * sizeof(struct TxFD),
			 ioaddr + SCBPointer);
		outw(CUStart, ioaddr + SCBCmd);
	} else {
		/* Reset the Tx and Rx units. */
		outl(PortReset, ioaddr + SCBPort);
		if (speedo_debug > 0)
			speedo_show_state(dev);
		udelay(10);
		speedo_resume(dev);
	}
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
	sp->stats.tx_errors++;
	dev->trans_start = jiffies;
	return;
}

static int
speedo_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct speedo_private *sp = (struct speedo_private *)dev->priv;
	long ioaddr = dev->base_addr;
	int entry;

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
			cpu_to_le32(CmdSuspend | CmdTx | CmdTxFlex);
		sp->tx_ring[entry].link =
			cpu_to_le32(sp->tx_ring_dma
				    + (sp->cur_tx % TX_RING_SIZE)
				    * sizeof(struct TxFD));
		sp->tx_ring[entry].tx_desc_addr =
			cpu_to_le32(sp->tx_ring_dma
				    + ((long)&sp->tx_ring[entry].tx_buf_addr0
				       - (long)sp->tx_ring));
		/* The data region is always in one buffer descriptor. */
		sp->tx_ring[entry].count = cpu_to_le32(sp->tx_threshold);
		sp->tx_ring[entry].tx_buf_addr0 =
			cpu_to_le32(pci_map_single(sp->pdev, skb->data,
						   skb->len));
		sp->tx_ring[entry].tx_buf_size0 = cpu_to_le32(skb->len);
		/* Todo: perhaps leave the interrupt bit set if the Tx queue is more
		   than half full.  Argument against: we should be receiving packets
		   and scavenging the queue.  Argument for: if so, it shouldn't
		   matter. */
		/* Trigger the command unit resume. */
		{
			struct descriptor *last_cmd = sp->last_cmd;
			sp->last_cmd = (struct descriptor *)&sp->tx_ring[entry];
			last_cmd->cmd_status &= cpu_to_le32(~(CmdSuspend | CmdIntr));
		}
		if (sp->cur_tx - sp->dirty_tx >= TX_QUEUE_LIMIT) {
			sp->tx_full = 1;
			netif_stop_queue(dev);
		}
		spin_unlock_irqrestore(&sp->lock, flags);
	}
	wait_for_cmd_done(ioaddr + SCBCmd);
	outw(CUResume, ioaddr + SCBCmd);
	dev->trans_start = jiffies;

	return 0;
}

/* The interrupt handler does all of the Rx thread work and cleans up
   after the Tx thread. */
static void speedo_interrupt(int irq, void *dev_instance, struct pt_regs *regs)
{
	struct net_device *dev = (struct net_device *)dev_instance;
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
				outw(RxResumeNoResources, ioaddr + SCBCmd);
			else if ((status & 0x003c) == 0x0008) { /* No resources (why?!) */
				/* No idea of what went wrong.  Restart the receiver. */
				outl(sp->rx_ring_dma[sp->cur_rx % RX_RING_SIZE],
					 ioaddr + SCBPointer);
				outw(RxStart, ioaddr + SCBCmd);
			}
			sp->stats.rx_errors++;
		}

		/* User interrupt, Command/Tx unit interrupt or CU not active. */
		if (status & 0xA400) {
			unsigned int dirty_tx;
			spin_lock(&sp->lock);

			dirty_tx = sp->dirty_tx;
			while (sp->cur_tx - dirty_tx > 0) {
				int entry = dirty_tx % TX_RING_SIZE;
				int status = le32_to_cpu(sp->tx_ring[entry].status);

				if (speedo_debug > 5)
					printk(KERN_DEBUG " scavenge candidate %d status %4.4x.\n",
						   entry, status);
				if ((status & StatusComplete) == 0)
					break;			/* It still hasn't been processed. */
				if (status & TxUnderrun)
					if (sp->tx_threshold < 0x01e08000)
						sp->tx_threshold += 0x00040000;
				/* Free the original skb. */
				if (sp->tx_skbuff[entry]) {
					sp->stats.tx_packets++;	/* Count only user packets. */
#if LINUX_VERSION_CODE > 0x20127
					sp->stats.tx_bytes += sp->tx_skbuff[entry]->len;
#endif
					pci_unmap_single(sp->pdev,
							 le32_to_cpu(sp->tx_ring[entry].tx_buf_addr0),
							 sp->tx_skbuff[entry]->len);
					dev_kfree_skb_irq(sp->tx_skbuff[entry]);
					sp->tx_skbuff[entry] = 0;
				} else if ((status & 0x70000) == CmdNOp) {
					if (sp->mc_setup_busy)
						pci_unmap_single(sp->pdev,
								 sp->mc_setup_dma,
								 sp->mc_setup_frm_len);
					sp->mc_setup_busy = 0;
				}
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

			sp->dirty_tx = dirty_tx;
			if (sp->tx_full
				&&  sp->cur_tx - dirty_tx < TX_QUEUE_LIMIT - 1) {
				/* The ring is no longer full, clear tbusy. */
				sp->tx_full = 0;
				spin_unlock(&sp->lock);
				netif_wake_queue(dev);
			} else
				spin_unlock(&sp->lock);
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

	return;
}

static int
speedo_rx(struct net_device *dev)
{
	struct speedo_private *sp = (struct speedo_private *)dev->priv;
	int entry = sp->cur_rx % RX_RING_SIZE;
	int status;
	int rx_work_limit = sp->dirty_rx + RX_RING_SIZE - sp->cur_rx;

	if (speedo_debug > 4)
		printk(KERN_DEBUG " In speedo_rx().\n");
	/* If we own the next entry, it's a new packet. Send it up. */
	while (sp->rx_ringp[entry] != NULL &&
		   (status = le32_to_cpu(sp->rx_ringp[entry]->status)) & RxComplete) {
		int pkt_len = le32_to_cpu(sp->rx_ringp[entry]->count) & 0x3fff;

		if (--rx_work_limit < 0)
			break;
		if (speedo_debug > 4)
			printk(KERN_DEBUG "  speedo_rx() status %8.8x len %d.\n", status,
				   pkt_len);
		if ((status & (RxErrTooBig|RxOK|0x0f90)) != RxOK) {
			if (status & RxErrTooBig)
				printk(KERN_ERR "%s: Ethernet frame overran the Rx buffer, "
					   "status %8.8x!\n", dev->name, status);
			else if ( ! (status & RxOK)) {
				/* There was a fatal error.  This *should* be impossible. */
				sp->stats.rx_errors++;
				printk(KERN_ERR "%s: Anomalous event in speedo_rx(), "
					   "status %8.8x.\n", dev->name, status);
			}
		} else {
			struct sk_buff *skb;

			/* Check if the packet is long enough to just accept without
			   copying to a properly sized skbuff. */
			if (pkt_len < rx_copybreak
				&& (skb = dev_alloc_skb(pkt_len + 2)) != 0) {
				skb->dev = dev;
				skb_reserve(skb, 2);	/* Align IP on 16 byte boundaries */
				/* 'skb_put()' points to the start of sk_buff data area. */
				pci_dma_sync_single(sp->pdev, sp->rx_ring_dma[entry],
						    PKT_BUF_SZ + sizeof(struct RxFD));
#if 1 || USE_IP_CSUM
				/* Packet is in one chunk -- we can copy + cksum. */
				eth_copy_and_sum(skb, sp->rx_skbuff[entry]->tail, pkt_len, 0);
				skb_put(skb, pkt_len);
#else
				memcpy(skb_put(skb, pkt_len), sp->rx_skbuff[entry]->tail,
					   pkt_len);
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
				sp->rx_ringp[entry] = NULL;
				pci_unmap_single(sp->pdev, sp->rx_ring_dma[entry],
						 PKT_BUF_SZ + sizeof(struct RxFD));
			}
			skb->protocol = eth_type_trans(skb, dev);
			netif_rx(skb);
			sp->stats.rx_packets++;
#if LINUX_VERSION_CODE > 0x20127
			sp->stats.rx_bytes += pkt_len;
#endif
		}
		entry = (++sp->cur_rx) % RX_RING_SIZE;
	}

	/* Refill the Rx ring buffers. */
	for (; sp->cur_rx - sp->dirty_rx > 0; sp->dirty_rx++) {
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
			sp->rx_ring_dma[entry] =
				pci_map_single(sp->pdev, rxf, PKT_BUF_SZ
					       + sizeof(struct RxFD));
			skb->dev = dev;
			skb_reserve(skb, sizeof(struct RxFD));
			rxf->rx_buf_addr = 0xffffffff;
		} else {
			rxf = sp->rx_ringp[entry];
		}
		rxf->status = cpu_to_le32(0xC0000001); 	/* '1' for driver use only. */
		rxf->link = 0;			/* None yet. */
		rxf->count = cpu_to_le32(PKT_BUF_SZ << 16);
		sp->last_rxf->link = cpu_to_le32(sp->rx_ring_dma[entry]);
		sp->last_rxf->status &= cpu_to_le32(~0xC0000000);
		sp->last_rxf = rxf;
	}

	sp->last_rx_time = jiffies;
	return 0;
}

static int
speedo_close(struct net_device *dev)
{
	long ioaddr = dev->base_addr;
	struct speedo_private *sp = (struct speedo_private *)dev->priv;
	int i;

	netif_stop_queue(dev);

	if (speedo_debug > 1)
		printk(KERN_DEBUG "%s: Shutting down ethercard, status was %4.4x.\n",
			   dev->name, inw(ioaddr + SCBStatus));

	/* Shut off the media monitoring timer. */
	del_timer(&sp->timer);

	/* Disable interrupts, and stop the chip's Rx process. */
	outw(SCBMaskAll, ioaddr + SCBCmd);
	outw(SCBMaskAll | RxAbort, ioaddr + SCBCmd);

	free_irq(dev->irq, dev);

	/* Free all the skbuffs in the Rx and Tx queues. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		struct sk_buff *skb = sp->rx_skbuff[i];
		sp->rx_skbuff[i] = 0;
		/* Clear the Rx descriptors. */
		if (skb) {
			pci_unmap_single(sp->pdev,
					 sp->rx_ring_dma[i],
					 PKT_BUF_SZ + sizeof(struct RxFD));
#if LINUX_VERSION_CODE < 0x20100
			skb->free = 1;
#endif
			dev_free_skb(skb);
		}
	}

	for (i = 0; i < TX_RING_SIZE; i++) {
		struct sk_buff *skb = sp->tx_skbuff[i];
		sp->tx_skbuff[i] = 0;

		/* Clear the Tx descriptors. */
		if (skb) {
			pci_unmap_single(sp->pdev,
							 le32_to_cpu(sp->tx_ring[i].tx_buf_addr0),
							 skb->len);
			dev_free_skb(skb);
		}
	}
	if (sp->mc_setup_frm) {
		kfree(sp->mc_setup_frm);
		sp->mc_setup_frm_len = 0;
	}

	/* Print a few items for debugging. */
	if (speedo_debug > 3)
		speedo_show_state(dev);

#if defined(HAS_PCI_NETIF)
	/* Alt: acpi_set_pwr_state(pci_bus, pci_devfn, sp->acpi_pwr); */
	acpi_set_pwr_state(sp->pci_bus, sp->pci_devfn, ACPI_D2);
#endif
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
speedo_get_stats(struct net_device *dev)
{
	struct speedo_private *sp = (struct speedo_private *)dev->priv;
	long ioaddr = dev->base_addr;

	/* Update only if the previous dump finished. */
	if (sp->lstats->done_marker == le32_to_cpu(0xA007)) {
		sp->stats.tx_aborted_errors += le32_to_cpu(sp->lstats->tx_coll16_errs);
		sp->stats.tx_window_errors += le32_to_cpu(sp->lstats->tx_late_colls);
		sp->stats.tx_fifo_errors += le32_to_cpu(sp->lstats->tx_underruns);
		sp->stats.tx_fifo_errors += le32_to_cpu(sp->lstats->tx_lost_carrier);
		/*sp->stats.tx_deferred += le32_to_cpu(sp->lstats->tx_deferred);*/
		sp->stats.collisions += le32_to_cpu(sp->lstats->tx_total_colls);
		sp->stats.rx_crc_errors += le32_to_cpu(sp->lstats->rx_crc_errs);
		sp->stats.rx_frame_errors += le32_to_cpu(sp->lstats->rx_align_errs);
		sp->stats.rx_over_errors += le32_to_cpu(sp->lstats->rx_resource_errs);
		sp->stats.rx_fifo_errors += le32_to_cpu(sp->lstats->rx_overrun_errs);
		sp->stats.rx_length_errors += le32_to_cpu(sp->lstats->rx_runt_errs);
		sp->lstats->done_marker = 0x0000;
		if (test_bit(LINK_STATE_START, &dev->state)) {
			wait_for_cmd_done(ioaddr + SCBCmd);
			outw(CUDumpStats, ioaddr + SCBCmd);
		}
	}
	return &sp->stats;
}

static int speedo_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct speedo_private *sp = (struct speedo_private *)dev->priv;
	long ioaddr = dev->base_addr;
	u16 *data = (u16 *)&rq->ifr_data;
	int phy = sp->phy[0] & 0x1f;
#if defined(HAS_PCI_NETIF)
	int saved_acpi;
#endif

    switch(cmd) {
	case SIOCDEVPRIVATE:		/* Get the address of the PHY in use. */
		data[0] = phy;
	case SIOCDEVPRIVATE+1:		/* Read the specified MII register. */
#if defined(HAS_PCI_NETIF)
		saved_acpi = acpi_set_pwr_state(sp->pci_bus, sp->pci_devfn, ACPI_D0);
		data[3] = mdio_read(ioaddr, data[0], data[1]);
		acpi_set_pwr_state(sp->pci_bus, sp->pci_devfn, saved_acpi);
#else
		data[3] = mdio_read(ioaddr, data[0], data[1]);
#endif
		return 0;
	case SIOCDEVPRIVATE+2:		/* Write the specified MII register */
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
#if defined(HAS_PCI_NETIF)
		saved_acpi = acpi_set_pwr_state(sp->pci_bus, sp->pci_devfn, ACPI_D0);
		mdio_write(ioaddr, data[0], data[1], data[2]);
		acpi_set_pwr_state(sp->pci_bus, sp->pci_devfn, saved_acpi);
#else
		mdio_write(ioaddr, data[0], data[1], data[2]);
#endif
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
static void set_rx_mode(struct net_device *dev)
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
		sp->tx_ring[entry].status = cpu_to_le32(CmdSuspend | CmdConfigure);
		sp->tx_ring[entry].link =
			cpu_to_le32(sp->tx_ring_dma + ((entry + 1) % TX_RING_SIZE)
				    * sizeof(struct TxFD));
		config_cmd_data = (void *)&sp->tx_ring[entry].tx_desc_addr;
		/* Construct a full CmdConfig frame. */
		memcpy(config_cmd_data, i82558_config_cmd, sizeof(i82558_config_cmd));
		config_cmd_data[1] = (txfifo << 4) | rxfifo;
		config_cmd_data[4] = rxdmacount;
		config_cmd_data[5] = txdmacount + 0x80;
		config_cmd_data[15] |= (new_rx_mode & 2) ? 1 : 0;
		config_cmd_data[19] = sp->flow_ctrl ? 0xBD : 0x80;
		config_cmd_data[19] |= sp->full_duplex ? 0x40 : 0;
		config_cmd_data[21] = (new_rx_mode & 1) ? 0x0D : 0x05;
		if (sp->phy[0] & 0x8000) {			/* Use the AUI port instead. */
			config_cmd_data[15] |= 0x80;
			config_cmd_data[8] = 0;
		}
		/* Trigger the command unit resume. */
		wait_for_cmd_done(ioaddr + SCBCmd);
		clear_suspend(last_cmd);
		outw(CUResume, ioaddr + SCBCmd);
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
		sp->tx_ring[entry].status = cpu_to_le32(CmdSuspend | CmdMulticastList);
		sp->tx_ring[entry].link =
			cpu_to_le32(sp->tx_ring_dma + ((entry + 1) % TX_RING_SIZE)
				    * sizeof(struct TxFD));
		sp->tx_ring[entry].tx_desc_addr = 0; /* Really MC list count. */
		setup_params = (u16 *)&sp->tx_ring[entry].tx_desc_addr;
		*setup_params++ = cpu_to_le16(dev->mc_count*6);
		/* Fill in the multicast addresses. */
		for (i = 0, mclist = dev->mc_list; i < dev->mc_count;
			 i++, mclist = mclist->next) {
			eaddrs = (u16 *)mclist->dmi_addr;
			*setup_params++ = *eaddrs++;
			*setup_params++ = *eaddrs++;
			*setup_params++ = *eaddrs++;
		}

		wait_for_cmd_done(ioaddr + SCBCmd);
		clear_suspend(last_cmd);
		/* Immediately trigger the command unit resume. */
		outw(CUResume, ioaddr + SCBCmd);
		spin_unlock_irqrestore(&sp->lock, flags);
	} else if (new_rx_mode == 0) {
		struct dev_mc_list *mclist;
		u16 *setup_params, *eaddrs;
		struct descriptor *mc_setup_frm = sp->mc_setup_frm;
		int i;

		/* If we are busy, someone might be quickly adding to the MC list.
		   Try again later when the list updates stop. */
		if (sp->mc_setup_busy) {
			sp->rx_mode = -1;
			return;
		}
		if (sp->mc_setup_frm_len < 10 + dev->mc_count*6
			|| sp->mc_setup_frm == NULL) {
			/* Allocate a full setup frame, 10bytes + <max addrs>. */
			if (sp->mc_setup_frm)
				kfree(sp->mc_setup_frm);
			sp->mc_setup_frm_len = 10 + multicast_filter_limit*6;
			sp->mc_setup_frm = kmalloc(sp->mc_setup_frm_len, GFP_ATOMIC);
			if (sp->mc_setup_frm == NULL) {
				printk(KERN_ERR "%s: Failed to allocate a setup frame.\n",
					   dev->name);
				sp->rx_mode = -1; /* We failed, try again. */
				return;
			}
		}
		mc_setup_frm = sp->mc_setup_frm;
		/* Fill the setup frame. */
		if (speedo_debug > 1)
			printk(KERN_DEBUG "%s: Constructing a setup frame at %p, "
				   "%d bytes.\n",
				   dev->name, sp->mc_setup_frm, sp->mc_setup_frm_len);
		mc_setup_frm->cmd_status =
			cpu_to_le32(CmdSuspend | CmdIntr | CmdMulticastList);
		/* Link set below. */
		setup_params = (u16 *)&mc_setup_frm->params;
		*setup_params++ = cpu_to_le16(dev->mc_count*6);
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
		sp->mc_setup_busy = 1;

		/* Change the command to a NoOp, pointing to the CmdMulti command. */
		sp->tx_skbuff[entry] = 0;
		sp->tx_ring[entry].status = cpu_to_le32(CmdNOp);
		sp->mc_setup_dma = pci_map_single(sp->pdev, mc_setup_frm, sp->mc_setup_frm_len);
		sp->tx_ring[entry].link = cpu_to_le32(sp->mc_setup_dma);

		/* Set the link in the setup frame. */
		mc_setup_frm->link =
			cpu_to_le32(sp->tx_ring_dma + ((entry + 1) % TX_RING_SIZE)
				    * sizeof(struct TxFD));

		wait_for_cmd_done(ioaddr + SCBCmd);
		clear_suspend(last_cmd);
		/* Immediately trigger the command unit resume. */
		outw(CUResume, ioaddr + SCBCmd);
		spin_unlock_irqrestore(&sp->lock, flags);
		if (speedo_debug > 5)
			printk(" CmdMCSetup frame length %d in entry %d.\n",
				   dev->mc_count, entry);
	}

	sp->rx_mode = new_rx_mode;
}


static int __init eepro100_init_module(void)
{
	int cards_found;

	if (debug >= 0)
		speedo_debug = debug;
	/* Always emit the version message. */
	if (speedo_debug)
		printk(KERN_INFO "%s", version);

#if defined(HAS_PCI_NETIF)
	cards_found = netif_pci_probe(pci_tbl);
	if (cards_found < 0)
		printk(KERN_INFO "eepro100: No cards found, driver not installed.\n");
	return cards_found;
#else
	cards_found = eepro100_init();
	if (cards_found <= 0) {
		printk(KERN_INFO "eepro100: No cards found, driver not installed.\n");
		return -ENODEV;
	}
#endif
	return 0;
}

static void __exit eepro100_cleanup_module(void)
{
	struct net_device *next_dev;

	/* No need to check MOD_IN_USE, as sys_delete_module() checks. */
	while (root_speedo_dev) {
		struct speedo_private *sp = (void *)root_speedo_dev->priv;
		unregister_netdev(root_speedo_dev);
#ifdef USE_IO
		release_region(root_speedo_dev->base_addr, SPEEDO3_TOTAL_SIZE);
#else
		iounmap((char *)root_speedo_dev->base_addr);
#endif
#if defined(HAS_PCI_NETIF)
		acpi_set_pwr_state(sp->pci_bus, sp->pci_devfn, sp->acpi_pwr);
#endif
		next_dev = sp->next_module;
		if (sp->priv_addr)
			kfree(sp->priv_addr);
		kfree(root_speedo_dev);
		root_speedo_dev = next_dev;
	}
}

module_init(eepro100_init_module);
module_exit(eepro100_cleanup_module);

/*
 * Local variables:
 *  compile-command: "gcc -DMODULE -D__KERNEL__ -Wall -Wstrict-prototypes -O6 -c eepro100.c `[ -f /usr/include/linux/modversions.h ] && echo -DMODVERSIONS` `[ -f ./pci-netif.h ] && echo -DHAS_PCI_NETIF`"
 *  SMP-compile-command: "gcc -D__SMP__ -DMODULE -D__KERNEL__ -Wall -Wstrict-prototypes -O6 -c eepro100.c `[ -f /usr/include/linux/modversions.h ] && echo -DMODVERSIONS`"
 *  simple-compile-command: "gcc -DMODULE -D__KERNEL__ -O6 -c eepro100.c"
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 * End:
 */
