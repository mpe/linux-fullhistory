/* tulip.c: A DEC 21040-family ethernet driver for linux. */
/*
   NOTICE: THIS IS THE ALPHA TEST VERSION!
	Written 1994-1997 by Donald Becker.

	This software may be used and distributed according to the terms
	of the GNU Public License, incorporated herein by reference.

	This driver is for the SMC EtherPower PCI ethernet adapter.
	It should work with most other DEC 21*40-based ethercards.

	The author may be reached as becker@CESDIS.gsfc.nasa.gov, or C/O
	Center of Excellence in Space Data and Information Sciences
	   Code 930.5, Goddard Space Flight Center, Greenbelt MD 20771

	Support and updates available at
	http://cesdis.gsfc.nasa.gov/linux/drivers/tulip.html
*/

static const char *version = "tulip.c:v0.83 10/19/97 becker@cesdis.gsfc.nasa.gov\n";

/* A few user-configurable values. */

/* Used to pass the full-duplex flag, etc. */
static int full_duplex[8] = {0, };
static int options[8] = {0, };
static int mtu[8] = {0, };			/* Jumbo MTU for interfaces. */

/* Set if the PCI BIOS detects the chips on a multiport board backwards. */
#ifdef REVERSE_PROBE_ORDER
static int reverse_probe = 1;
#else
static int reverse_probe = 0;
#endif

/* Keep the ring sizes a power of two for efficiency.
   Making the Tx ring too large decreases the effectiveness of channel
   bonding and packet priority.
   There are no ill effects from too-large receive rings. */
#define TX_RING_SIZE	16
#define RX_RING_SIZE	16

/* Set the copy breakpoint for the copy-only-tiny-buffer Rx structure. */
#ifdef __alpha__
static const rx_copybreak = 1518;
#else
static const rx_copybreak = 100;
#endif

/* The following example shows how to always use the 10base2 port. */
#ifdef notdef
#define TULIP_DEFAULT_MEDIA 1		/* 1 == 10base2 */
#define TULIP_NO_MEDIA_SWITCH		/* Don't switch from this port */
#endif

/* Define to force full-duplex operation on all Tulip interfaces. */
/* #define  TULIP_FULL_DUPLEX 1 */

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
#include <linux/bios32.h>
#include <asm/processor.h>		/* Processor type for cache alignment. */
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>

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
char kernel_version[] = UTS_RELEASE;
#endif
#ifdef SA_SHIRQ
#define IRQ(irq, dev_id, pt_regs) (irq, dev_id, pt_regs)
#else
#define IRQ(irq, dev_id, pt_regs) (irq, pt_regs)
#endif

#if (LINUX_VERSION_CODE < 0x20123)
#define test_and_set_bit(val, addr) set_bit(val, addr)
#endif

/* This my implementation of shared IRQs, now only used for 1.2.13. */
#ifdef HAVE_SHARED_IRQ
#define USE_SHARED_IRQ
#include <linux/shared_irq.h>
#endif

/* The total size is unusually large: The 21040 aligns each of its 16
   longword-wide registers on a quadword boundary. */
#define TULIP_TOTAL_SIZE 0x80

#ifdef HAVE_DEVLIST
struct netdev_entry tulip_drv =
{"Tulip", tulip_pci_probe, TULIP_TOTAL_SIZE, NULL};
#endif

#ifdef TULIP_DEBUG
int tulip_debug = TULIP_DEBUG;
#else
int tulip_debug = 1;
#endif

/*
				Theory of Operation

I. Board Compatibility

This device driver is designed for the DECchip "Tulip", Digital's
single-chip ethernet controllers for PCI.  Supported members of the family
are the 21040, 21041, 21140, 21140A and 21142.  These chips are used on
many PCI boards including the SMC EtherPower series.


II. Board-specific settings

PCI bus devices are configured by the system at boot time, so no jumpers
need to be set on the board.  The system BIOS preferably should assign the
PCI INTA signal to an otherwise unused system IRQ line.
Note: Kernel versions earlier than 1.3.73 do not support shared PCI
interrupt lines.

III. Driver operation

IIIa. Ring buffers

The Tulip can use either ring buffers or lists of Tx and Rx descriptors.
This driver uses statically allocated rings of Rx and Tx descriptors, set at
compile time by RX/TX_RING_SIZE.  This version of the driver allocates skbuffs
for the Rx ring buffers at open() time and passes the skb->data field to the
Tulip as receive data buffers.  When an incoming frame is less than
RX_COPYBREAK bytes long, a fresh skbuff is allocated and the frame is
copied to the new skbuff.  When the incoming frame is larger, the skbuff is
passed directly up the protocol stack and replaced by a newly allocated
skbuff.

The RX_COPYBREAK value is chosen to trade-off the memory wasted by
using a full-sized skbuff for small frames vs. the copying costs of larger
frames.  For small frames the copying cost is negligible (esp. considering
that we are pre-loading the cache with immediately useful header
information).  For large frames the copying cost is non-trivial, and the
larger copy might flush the cache of useful data.  A subtle aspect of this
choice is that the Tulip only receives into longword aligned buffers, thus
the IP header at offset 14 isn't longword aligned for further processing.
Copied frames are put into the new skbuff at an offset of "+2", thus copying
has the beneficial effect of aligning the IP header and preloading the
cache.

IIIC. Synchronization
The driver runs as two independent, single-threaded flows of control.  One
is the send-packet routine, which enforces single-threaded use by the
dev->tbusy flag.  The other thread is the interrupt handler, which is single
threaded by the hardware and other software.

The send packet thread has partial control over the Tx ring and 'dev->tbusy'
flag.  It sets the tbusy flag whenever it's queuing a Tx packet. If the next
queue slot is empty, it clears the tbusy flag when finished otherwise it sets
the 'tp->tx_full' flag.

The interrupt handler has exclusive control over the Rx ring and records stats
from the Tx ring.  (The Tx-done interrupt can't be selectively turned off, so
we can't avoid the interrupt overhead by having the Tx routine reap the Tx
stats.)	 After reaping the stats, it marks the queue entry as empty by setting
the 'base' to zero.	 Iff the 'tp->tx_full' flag is set, it clears both the
tx_full and tbusy flags.

IV. Notes

Thanks to Duke Kamstra of SMC for providing an EtherPower board.

IVb. References

http://cesdis.gsfc.nasa.gov/linux/misc/NWay.html
http://www.digital.com  (search for current 21*4* datasheets and "21X4 SROM")
http://www.national.com/pf/DP/DP83840.html

IVc. Errata

The DEC databook doesn't document which Rx filter settings accept broadcast
packets.  Nor does it document how to configure the part to configure the
serial subsystem for normal (vs. loopback) operation or how to have it
autoswitch between internal 10baseT, SIA and AUI transceivers.

The 21040 databook claims that CSR13, CSR14, and CSR15 should each be the last
register of the set CSR12-15 written.  Hmmm, now how is that possible?  */


/* A few values that may be tweaked. */
#define PKT_BUF_SZ		1536			/* Size of each temporary Rx buffer.*/

/* This is a mysterious value that can be written to CSR11 in the 21040 (only)
   to support a pre-NWay full-duplex signaling mechanism using short frames.
   No one knows what it should be, but if left at its default value some
   10base2(!) packets trigger a full-duplex-request interrupt. */
#define FULL_DUPLEX_MAGIC	0x6969

#ifndef PCI_VENDOR_ID_DEC		/* Now defined in linux/pci.h */
#define PCI_VENDOR_ID_DEC			0x1011
#define PCI_DEVICE_ID_TULIP			0x0002		/* 21040. */
#define PCI_DEVICE_ID_TULIP_FAST	0x0009		/* 21140. */
#endif

#ifndef PCI_DEVICE_ID_DEC_TULIP_PLUS
#define PCI_DEVICE_ID_DEC_TULIP_PLUS	0x0014		/* 21041. */
#endif
#ifndef PCI_DEVICE_ID_DEC_TULIP_21142
#define PCI_DEVICE_ID_DEC_TULIP_21142	0x0019
#endif

#ifndef PCI_VENDOR_ID_LITEON
#define PCI_VENDOR_ID_LITEON	0x11AD
#define PCI_DEVICE_ID_PNIC		0x0002
#define PCI_DEVICE_ID_PNIC_X	0x0168
#else
/* Now PCI_VENDOR_ID_LITEON is defined, but device IDs have different names */
#define PCI_DEVICE_ID_PNIC		PCI_DEVICE_ID_LITEON_LNE100TX
#define PCI_DEVICE_ID_PNIC_X	0x0168
#endif

/* The rest of these values should never change. */

static void tulip_timer(unsigned long data);

/* A table describing the chip types. */
static struct tulip_chip_table {
  int device_id;
  char *chip_name;
  int flags;
  void (*media_timer)(unsigned long data);
} tulip_tbl[] = {
  { PCI_DEVICE_ID_DEC_TULIP, "Digital DS21040 Tulip", 0, tulip_timer },
  { PCI_DEVICE_ID_DEC_TULIP_PLUS, "Digital DS21041 Tulip", 0, tulip_timer },
  { PCI_DEVICE_ID_DEC_TULIP_FAST, "Digital DS21140 Tulip", 0, tulip_timer },
  { PCI_DEVICE_ID_DEC_TULIP_21142, "Digital DS21142/3 Tulip", 0, tulip_timer },
  { PCI_DEVICE_ID_PNIC_X, "Lite-On 82c168 PNIC", 0, tulip_timer },
  {0, 0, 0, 0},
};
/* This matches the table above. */
enum chips { DC21040=0, DC21041=1, DC21140=2, DC21142=3, LC82C168};

static const char * const medianame[] = {
  "10baseT", "10base2", "AUI", "100baseTx",
  "10baseT-FD", "100baseTx-FD", "100baseT4", "100baseFx",
  "100baseFx-FD", "MII 10baseT", "MII 10baseT-FD", "MII",
  "10baseT(forced)", "MII 100baseTx", "MII 100baseTx-FD", "MII 100baseT4",
};
/* A full-duplex map for above. */
static const char media_fd[] =
{0,0,0,0,  0xff,0xff,0,0,  0xff,0,0xff,0x01, 0,0,0xff,0 };
/* 21041 transceiver register settings: 10-T, 10-2, AUI, 10-T, 10T-FD*/
static u16 t21041_csr13[] = { 0xEF05, 0xEF09, 0xEF09, 0xEF01, 0xEF09, };
static u16 t21041_csr14[] = { 0x7F3F, 0xF7FD, 0xF7FD, 0x7F3F, 0x7F3D, };
static u16 t21041_csr15[] = { 0x0008, 0x0006, 0x000E, 0x0008, 0x0008, };

static u16 t21142_csr13[] = { 0x0001, 0x0009, 0x0009, 0x0000, 0x0001, };
static u16 t21142_csr14[] = { 0xFFFF, 0x0705, 0x0705, 0x0000, 0x7F3D, };
static u16 t21142_csr15[] = { 0x0008, 0x0006, 0x000E, 0x0008, 0x0008, };

/* Offsets to the Command and Status Registers, "CSRs".  All accesses
   must be longword instructions and quadword aligned. */
enum tulip_offsets {
	CSR0=0,    CSR1=0x08, CSR2=0x10, CSR3=0x18, CSR4=0x20, CSR5=0x28,
	CSR6=0x30, CSR7=0x38, CSR8=0x40, CSR9=0x48, CSR10=0x50, CSR11=0x58,
	CSR12=0x60, CSR13=0x68, CSR14=0x70, CSR15=0x78 };

/* The bits in the CSR5 status registers, mostly interrupt sources. */
enum status_bits {
	TimerInt=0x800, TPLnkFail=0x1000, TPLnkPass=0x10,
	RxJabber=0x200, RxDied=0x100, RxNoBuf=0x80, RxIntr=0x40,
	TxFIFOUnderflow=0x20, TxJabber=0x08, TxNoBuf=0x04, TxDied=0x02,
	TxIntr=0x01,
};

/* The Tulip Rx and Tx buffer descriptors. */
struct tulip_rx_desc {
	s32 status;
	s32 length;
	u32 buffer1, buffer2;
};

struct tulip_tx_desc {
	s32 status;
	s32 length;
	u32 buffer1, buffer2;				/* We use only buffer 1.  */
};

struct medialeaf {
	u8 type;
	u8 media;
	unsigned char *leafdata;
};

struct mediatable {
	u16 defaultmedia;
	u8 leafcount, csr12dir;				/* General purpose pin directions. */
	unsigned has_mii:1;
	struct medialeaf mleaf[0];
};

struct mediainfo {
	struct mediainfo *next;
	int info_type;
	int index;
	struct non_mii { char media; unsigned char csr12val; char bitnum, flags;}  non_mii;
	unsigned char *info;
};

struct tulip_private {
	char devname[8];			/* Used only for kernel debugging. */
	const char *product_name;
	struct device *next_module;
	struct tulip_rx_desc rx_ring[RX_RING_SIZE];
	struct tulip_tx_desc tx_ring[TX_RING_SIZE];
	/* The saved address of a sent-in-place packet/buffer, for skfree(). */
	struct sk_buff* tx_skbuff[TX_RING_SIZE];
	/* The addresses of receive-in-place skbuffs. */
	struct sk_buff* rx_skbuff[RX_RING_SIZE];
	char *rx_buffs;				/* Address of temporary Rx buffers. */
	u32 setup_frame[48];		/* Pseudo-Tx frame to init address table. */
	int chip_id;
	int revision;
#if LINUX_VERSION_CODE > 0x20139
	struct net_device_stats stats;
#else
	struct enet_statistics stats;
#endif
	struct timer_list timer;	/* Media selection timer. */
#ifdef CONFIG_NET_HW_FLOWCONTROL
	int fc_bit;
#endif
	unsigned int cur_rx, cur_tx;		/* The next free ring entry */
	unsigned int dirty_rx, dirty_tx;	/* The ring entries to be free()ed. */
	unsigned int tx_full:1;				/* The Tx queue is full. */
	unsigned int full_duplex:1;			/* Full-duplex operation requested. */
	unsigned int full_duplex_lock:1;
	unsigned int default_port:4;		/* Last dev->if_port value. */
	unsigned int media2:4;				/* Secondary monitored media port. */
	unsigned int medialock:1;			/* Don't sense media type. */
	unsigned int mediasense:1;			/* Media sensing in progress. */
	unsigned int csr6;					/* Current CSR6 control settings. */
	unsigned char eeprom[128];			/* Serial EEPROM contents. */
	signed char phys[4];				/* MII device addresses. */
	struct mediatable *mtable;
	int cur_index;						/* Current media index. */
	unsigned char pci_bus, pci_device_fn;
	int pad0, pad1;						/* Used for 8-byte alignment */
};

static struct device *tulip_probe1(struct device *dev, int ioaddr, int irq,
								   int chip_id, int options);
static void parse_eeprom(struct device *dev);
static int read_eeprom(int ioaddr, int location);
static int mdio_read(int ioaddr, int phy_id, int location);
static void select_media(struct device *dev, int startup);
static int tulip_open(struct device *dev);
static void tulip_timer(unsigned long data);
static void tulip_tx_timeout(struct device *dev);
static void tulip_init_ring(struct device *dev);
static int tulip_start_xmit(struct sk_buff *skb, struct device *dev);
static int tulip_rx(struct device *dev);
static void tulip_interrupt IRQ(int irq, void *dev_instance, struct pt_regs *regs);
static int tulip_close(struct device *dev);
static struct enet_statistics *tulip_get_stats(struct device *dev);
#ifdef NEW_MULTICAST
static void set_multicast_list(struct device *dev);
#else
static void set_multicast_list(struct device *dev, int num_addrs, void *addrs);
#endif
#ifdef CONFIG_NET_FASTROUTE
#include <linux/if_arp.h>
#include <net/ip.h>

static int tulip_accept_fastpath(struct device *dev, struct dst_entry *dst);
#endif



#ifdef MODULE
/* A list of all installed Tulip devices, for removing the driver module. */
static struct device *root_tulip_dev = NULL;
#endif

/* This 21040 probe no longer uses a large fixed contiguous Rx buffer region,
   but now receives directly into full-sized skbuffs that are allocated
   at open() time.
   This allows the probe routine to use the old driver initialization
   interface. */

int tulip_probe(struct device *dev)
{
	int cards_found = 0;
	static int pci_index = 0;	/* Static, for multiple probe calls. */

	/* Ideally we would detect all network cards in slot order.  That would
	   be best done a central PCI probe dispatch, which wouldn't work
	   well with the current structure.  So instead we detect just the
	   Tulip cards in slot order. */

	if (pcibios_present()) {
		unsigned char pci_bus, pci_device_fn;

		for (;pci_index < 0xff; pci_index++) {
			unsigned char pci_irq_line, pci_latency;
			unsigned short pci_command, vendor, device;
			unsigned int pci_ioaddr, chip_idx = 0;

			if (pcibios_find_class
				(PCI_CLASS_NETWORK_ETHERNET << 8,
				 reverse_probe ? 0xfe - pci_index : pci_index,
				 &pci_bus, &pci_device_fn) != PCIBIOS_SUCCESSFUL)
				if (reverse_probe)
					continue;
				else
					break;
			pcibios_read_config_word(pci_bus, pci_device_fn,
									 PCI_VENDOR_ID, &vendor);
			pcibios_read_config_word(pci_bus, pci_device_fn,
									 PCI_DEVICE_ID, &device);
			pcibios_read_config_byte(pci_bus, pci_device_fn,
									 PCI_INTERRUPT_LINE, &pci_irq_line);
			pcibios_read_config_dword(pci_bus, pci_device_fn,
									  PCI_BASE_ADDRESS_0, &pci_ioaddr);
			/* Remove I/O space marker in bit 0. */
			pci_ioaddr &= ~3;

			if (vendor != PCI_VENDOR_ID_DEC
				&& vendor != PCI_VENDOR_ID_LITEON)
				continue;
			if (vendor == PCI_VENDOR_ID_LITEON)
				device = PCI_DEVICE_ID_PNIC_X;

			for (chip_idx = 0; tulip_tbl[chip_idx].chip_name; chip_idx++)
				if (device == tulip_tbl[chip_idx].device_id)
					break;
			if (tulip_tbl[chip_idx].chip_name == 0) {
				printk(KERN_INFO "Unknown Digital PCI ethernet chip type"
					   " %4.4x"" detected: not configured.\n", device);
				continue;
			}
			if (tulip_debug > 2)
				printk(KERN_DEBUG "Found DEC PCI Tulip at I/O %#x, IRQ %d.\n",
					   pci_ioaddr, pci_irq_line);

			if (check_region(pci_ioaddr, TULIP_TOTAL_SIZE))
				continue;

#ifdef MODULE
			dev = tulip_probe1(dev, pci_ioaddr, pci_irq_line, chip_idx,
						 cards_found);
#else
			dev = tulip_probe1(dev, pci_ioaddr, pci_irq_line, chip_idx, -1);
#endif

			if (dev) {
			  /* Get and check the bus-master and latency values. */
			  pcibios_read_config_word(pci_bus, pci_device_fn,
									   PCI_COMMAND, &pci_command);
			  if ( ! (pci_command & PCI_COMMAND_MASTER)) {
				printk(KERN_INFO "  PCI Master Bit has not been set! Setting...\n");
				pci_command |= PCI_COMMAND_MASTER;
				pcibios_write_config_word(pci_bus, pci_device_fn,
										  PCI_COMMAND, pci_command);
			  }
			  pcibios_read_config_byte(pci_bus, pci_device_fn,
									   PCI_LATENCY_TIMER, &pci_latency);
			  if (pci_latency < 10) {
				printk(KERN_INFO "  PCI latency timer (CFLT) is unreasonably"
					   " low at %d.  Setting to 64 clocks.\n", pci_latency);
				pcibios_write_config_byte(pci_bus, pci_device_fn,
										  PCI_LATENCY_TIMER, 64);
			  } else if (tulip_debug > 1)
				printk(KERN_INFO "  PCI latency timer (CFLT) is %#x.\n",
					   pci_latency);
			  /* Bring the 21143 out power-down mode. */
			  if (device == PCI_DEVICE_ID_DEC_TULIP_21142)
				pcibios_write_config_dword(pci_bus, pci_device_fn,
										  	0x40, 0x40000000);
			  dev = 0;
			  cards_found++;
			}
		}
	}

	return cards_found ? 0 : -ENODEV;
}

static struct device *tulip_probe1(struct device *dev, int ioaddr, int irq,
								   int chip_id, int board_idx)
{
	static int did_version = 0;			/* Already printed version info. */
	struct tulip_private *tp;
	/* See note below on the multiport cards. */
	static unsigned char last_phys_addr[6] = {0x00, 'L', 'i', 'n', 'u', 'x'};
	static int last_irq = 0;
	int i;
	unsigned short sum;

	if (tulip_debug > 0  &&  did_version++ == 0)
		printk(KERN_INFO "%s", version);

	dev = init_etherdev(dev, 0);

	printk(KERN_INFO "%s: %s at %#3x,",
		   dev->name, tulip_tbl[chip_id].chip_name, ioaddr);

	/* Stop the chip's Tx and Rx processes. */
	outl(inl(ioaddr + CSR6) & ~0x2002, ioaddr + CSR6);
	/* Clear the missed-packet counter. */
	(volatile)inl(ioaddr + CSR8);

	if (chip_id == DC21041) {
		if (inl(ioaddr + CSR9) & 0x8000) {
			printk(" 21040 compatible mode,");
			chip_id = DC21040;
		} else {
			printk(" 21041 mode,");
		}
	}

	/* The station address ROM is read byte serially.  The register must
	   be polled, waiting for the value to be read bit serially from the
	   EEPROM.
	   */
	sum = 0;
	if (chip_id == DC21040) {
		outl(0, ioaddr + CSR9);		/* Reset the pointer with a dummy write. */
		for (i = 0; i < 6; i++) {
			int value, boguscnt = 100000;
			do
				value = inl(ioaddr + CSR9);
			while (value < 0  && --boguscnt > 0);
			dev->dev_addr[i] = value;
			sum += value & 0xff;
		}
	} else if (chip_id == LC82C168) {
		for (i = 0; i < 3; i++) {
			int value, boguscnt = 100000;
			outl(0x600 | i, ioaddr + 0x98);
			do
				value = inl(ioaddr + CSR9);
			while (value < 0  && --boguscnt > 0);
			((u16*)dev->dev_addr)[i] = value;
			sum += value & 0xffff;
		}
	} else {	/* Must be a new chip, with a serial EEPROM interface. */
		/* We read the whole EEPROM, and sort it out later.  DEC has a
		   specification _Digital Semiconductor 21X4 Serial ROM Format_
		   but early vendor boards just put the address in the first six
		   EEPROM locations. */
		unsigned char ee_data[128];
		int sa_offset = 0;

		for (i = 0; i < sizeof(ee_data)/2; i++)
			((u16 *)ee_data)[i] = read_eeprom(ioaddr, i);

		/* Detect the simple EEPROM format by the duplicated station addr. */
		for (i = 0; i < 8; i ++)
			if (ee_data[i] != ee_data[16+i])
				sa_offset = 20;
		for (i = 0; i < 6; i ++) {
			dev->dev_addr[i] = ee_data[i + sa_offset];
			sum += ee_data[i + sa_offset];
		}
	}
	/* Lite-On boards have the address byte-swapped. */
	if (dev->dev_addr[0] == 0xA0  &&  dev->dev_addr[1] == 0x00)
		for (i = 0; i < 6; i+=2) {
			char tmp = dev->dev_addr[i];
			dev->dev_addr[i] = dev->dev_addr[i+1];
			dev->dev_addr[i+1] = tmp;
		}
	/* On the Zynx 315 Etherarray and other multiport boards only the
	   first Tulip has an EEPROM.
	   The addresses of the subsequent ports are derived from the first.
	   Many PCI BIOSes also incorrectly report the IRQ line, so we correct
	   that here as well. */
	if (sum == 0  || sum == 6*0xff) {
		printk(" EEPROM not present,");
		for (i = 0; i < 5; i++)
			dev->dev_addr[i] = last_phys_addr[i];
		dev->dev_addr[i] = last_phys_addr[i] + 1;
#if defined(__i386)		/* This BIOS bug doesn't exist on Alphas. */
		irq = last_irq;
#endif
	}
	for (i = 0; i < 6; i++)
		printk(" %2.2x", last_phys_addr[i] = dev->dev_addr[i]);
	printk(", IRQ %d.\n", irq);
	last_irq = irq;

	/* We do a request_region() only to register /proc/ioports info. */
	request_region(ioaddr, TULIP_TOTAL_SIZE, tulip_tbl[chip_id].chip_name);

	dev->base_addr = ioaddr;
	dev->irq = irq;

	/* Make certain the data structures are quadword aligned. */
	tp = (void *)(((long)kmalloc(sizeof(*tp), GFP_KERNEL | GFP_DMA) + 7) & ~7);
	memset(tp, 0, sizeof(*tp));
	dev->priv = tp;

#ifdef MODULE
	tp->next_module = root_tulip_dev;
	root_tulip_dev = dev;
#endif

	tp->chip_id = chip_id;

#ifdef TULIP_FULL_DUPLEX
	tp->full_duplex = 1;
	tp->full_duplex_lock = 1;
#endif
#ifdef TULIP_DEFAULT_MEDIA
	tp->default_port = TULIP_DEFAULT_MEDIA;
#endif
#ifdef TULIP_NO_MEDIA_SWITCH
	tp->medialock = 1;
#endif

	/* The lower four bits are the media type. */
	if (board_idx >= 0) {
		tp->full_duplex = (options[board_idx]&16) || full_duplex[board_idx]>0;
		if (tp->full_duplex)
			tp->full_duplex_lock = 1;
		tp->default_port = options[board_idx] & 15;
		if (tp->default_port)
			tp->medialock = 1;
		if (mtu[board_idx] > 0)
			dev->mtu = mtu[board_idx];
	}

	/* This is logically part of probe1(), but too complex to write inline. */
	if (chip_id != DC21040  &&  chip_id != LC82C168)
		parse_eeprom(dev);

	if (tp->mtable  &&  tp->mtable->has_mii) {
		int phy, phy_idx;
		/* Find the connected MII xcvrs.
		   Doing this in open() would allow detecting external xcvrs later,
		   but takes much time. */
		for (phy = 0, phy_idx = 0; phy < 32 && phy_idx < sizeof(tp->phys);
			 phy++) {
			int mii_status = mdio_read(ioaddr, phy, 0);
			if (mii_status != 0xffff  && mii_status != 0x0000) {
				tp->phys[phy_idx++] = phy;
				printk(KERN_INFO "%s:  MII transceiver found at MDIO address %d.\n",
					   dev->name, phy);
			}
		}
		if (phy_idx == 0) {
			printk(KERN_INFO "%s: ***WARNING***: No MII transceiver found!\n",
				   dev->name);
			tp->phys[0] = 1;
		}
	}

	/* The Tulip-specific entries in the device structure. */
	dev->open = &tulip_open;
	dev->hard_start_xmit = &tulip_start_xmit;
	dev->stop = &tulip_close;
	dev->get_stats = &tulip_get_stats;
#ifdef HAVE_MULTICAST
	dev->set_multicast_list = &set_multicast_list;
#endif
#ifdef CONFIG_NET_FASTROUTE
	dev->accept_fastpath = tulip_accept_fastpath;
#endif

	/* Reset the xcvr interface and turn on heartbeat. */
	switch (chip_id) {
	case DC21041:
		outl(0x00000000, ioaddr + CSR13);
		outl(0xFFFFFFFF, ioaddr + CSR14);
		outl(0x00000008, ioaddr + CSR15); /* Listen on AUI also. */
		outl(inl(ioaddr + CSR6) | 0x200, ioaddr + CSR6);
		outl(0x0000EF05, ioaddr + CSR13);
		break;
	case DC21140:  case DC21142:
		if (tp->mtable)
			outl(tp->mtable->csr12dir | 0x100, ioaddr + CSR12);
		break;
	case DC21040:
		outl(0x00000000, ioaddr + CSR13);
		outl(0x00000004, ioaddr + CSR13);
		break;
	case LC82C168:
		outl(0x33, ioaddr + CSR12);
		outl(0x0201F078, ioaddr + 0xB8); /* Turn on autonegotiation. */
		break;
	}

	return dev;
}

/* Serial EEPROM section. */
/* The main routine to parse the very complicated SROM structure.
   Search www.digital.com for "21X4 SROM" to get details.
   This code is very complex, and will require changes to support
   additional cards, so I'll be verbose about what is going on.
   */

/* Known cards that have old-style EEPROMs. */
static struct fixups {
  char *name;
  unsigned char addr0, addr1, addr2;
  u16 newtable[32];				/* Max length below. */
} eeprom_fixups[] = {
  {"Asante", 0, 0, 0x94, {0x1e00, 0x0000, 0x0800, 0x0100, 0x018c,
						  0x0000, 0x0000, 0xe078, 0x0001, 0x0050, 0x0018 }},
  {"SMC9332DST", 0, 0, 0xC0, { 0x1e00, 0x0000, 0x0800, 0x021f,
							   0x0000, 0x009E, /* 10baseT */
							   0x0903, 0x006D, /* 100baseTx */ }},
  {"Cogent EM100", 0, 0, 0x92, { 0x1e00, 0x0000, 0x0800, 0x013f,
								 0x0103, 0x006D, /* 100baseTx */ }},
  {"Maxtech NX-110", 0, 0, 0xE8, { 0x1e00, 0x0000, 0x0800, 0x0313,
							   0x1001, 0x009E, /* 10base2, CSR12 0x10*/
							   0x0000, 0x009E, /* 10baseT */
							   0x0303, 0x006D, /* 100baseTx, CSR12 0x03 */ }},
  {"Accton EN1207", 0, 0, 0xE8, { 0x1e00, 0x0000, 0x0800, 0x031F,
							0x1B01, 0x0000, /* 10base2,   CSR12 0x1B */
							0x1B03, 0x006D, /* 100baseTx, CSR12 0x1B */ 
							0x0B00, 0x009E, /* 10baseT,   CSR12 0x0B */
   }},
  {0, 0, 0, 0, {}}};

static const char * block_name[] = {"21140 non-MII", "21140 MII PHY",
 "21142 non-MII PHY", "21142 MII PHY", "21143 SYM PHY", "21143 reset method"};

#define EEPROM_SIZE 128
static void parse_eeprom(struct device *dev)
{
	/* The last media info list parsed, for multiport boards.  */
	static struct mediatable *last_mediatable = NULL;
	static unsigned char *last_ee_data = NULL;
	static controller_index = 0;
	struct tulip_private *tp = (struct tulip_private *)dev->priv;
	int ioaddr = dev->base_addr;
	unsigned char *ee_data = tp->eeprom;
	int i;

	tp->mtable = 0;
	for (i = 0; i < EEPROM_SIZE/2; i++)
		((u16 *)ee_data)[i] = read_eeprom(ioaddr, i);

	/* Detect an old-style (SA only) EEPROM layout:
	   memcmp(eedata, eedata+16, 8). */
	for (i = 0; i < 8; i ++)
		if (ee_data[i] != ee_data[16+i])
			break;
	if (i >= 8) {
		if (ee_data[0] == 0xff) {
			if (last_mediatable) {
				controller_index++;
				printk(KERN_INFO "%s:  Controller %d of multiport board.\n",
					   dev->name, controller_index);
				tp->mtable = last_mediatable;
				ee_data = last_ee_data;
				goto subsequent_board;
			} else
				printk(KERN_INFO "%s:  Missing EEPROM, this interface may "
					   "not work correctly!\n",
			   dev->name);
			return;
		}
	  /* Do a fix-up based on the vendor half of the station address prefix. */
	  for (i = 0; eeprom_fixups[i].name; i++) {
		if (dev->dev_addr[0] == eeprom_fixups[i].addr0
			&&  dev->dev_addr[1] == eeprom_fixups[i].addr1
			&&  dev->dev_addr[2] == eeprom_fixups[i].addr2) {
		  if (dev->dev_addr[2] == 0xE8  &&  ee_data[0x1a] == 0x55)
			  i++;			/* An Accton EN1207, not an outlaw Maxtech. */
		  memcpy(ee_data + 26, eeprom_fixups[i].newtable,
				 sizeof(eeprom_fixups[i].newtable));
		  printk(KERN_INFO "%s: Old format EEPROM on '%s' board.  Using"
				 " substitute media control info.\n",
				 dev->name, eeprom_fixups[i].name);
		  break;
		}
	  }
	  if (eeprom_fixups[i].name == NULL) { /* No fixup found. */
		printk(KERN_INFO "%s: Old style EEPROM -- no media selection information.\n",
			   dev->name);
		return;
	  }
	}
	if (tulip_debug > 1) {
	  printk(KERN_DEBUG "read_eeprom:");
	  for (i = 0; i < 64; i++) {
		printk("%s%4.4x", (i & 7) == 0 ? "\n" KERN_DEBUG : " ",
			   read_eeprom(ioaddr, i));
	  }
	  printk("\n");
	}
	
	controller_index = 0;
	if (ee_data[19] > 1) {		/* Multiport board. */
		last_ee_data = ee_data;
	}
subsequent_board:

	if (tp->chip_id == DC21041) {
		unsigned char *p = (void *)ee_data + ee_data[27 + controller_index*3];
		short media = *(u16 *)p;
		int count = p[2];

		printk(KERN_INFO "%s:21041 Media information at %d, default media "
			   "%4.4x (%s).\n", dev->name, ee_data[27], media,
			   media & 0x0800 ? "Autosense" : medianame[media & 15]);
		for (i = 0; i < count; i++) {
			unsigned char media_code = p[3 + i*7];
			u16 *csrvals = (u16 *)&p[3 + i*7 + 1];
			printk(KERN_INFO "%s:  21041 media %2.2x (%s),"
				   " csr13 %4.4x csr14 %4.4x csr15 %4.4x.\n",
				   dev->name, media_code & 15, medianame[media_code & 15],
				   csrvals[0], csrvals[1], csrvals[2]);
		}
	} else {
		unsigned char *p = (void *)ee_data + ee_data[27];
		unsigned char csr12dir = 0;
		int count;
		struct mediatable *mtable;
		u16 media = *((u16 *)p)++;

		if (tp->chip_id == DC21140)
			csr12dir = *p++;
		count = *p++;
		mtable = (struct mediatable *)
			kmalloc(sizeof(struct mediatable) + count*sizeof(struct medialeaf),
					GFP_KERNEL);
		if (mtable == NULL)
			return;				/* Horrible, impossible failure. */
		last_mediatable = tp->mtable = mtable;
		mtable->defaultmedia = media;
		mtable->leafcount = count;
		mtable->csr12dir = csr12dir;
		mtable->has_mii = 0;

		printk(KERN_INFO "%s:  EEPROM default media type %s.\n", dev->name,
			   media & 0x0800 ? "Autosense" : medianame[media & 15]);
		for (i = 0; i < count; i++) {
			struct medialeaf *leaf = &mtable->mleaf[i];
			
			if ((p[0] & 0x80) == 0) { /* 21140 Compact block. */
				leaf->type = 0;
				leaf->media = p[0] & 0x3f;
				leaf->leafdata = p;
				p += 4;
			} else {
				leaf->type = p[1];
				if (p[1] & 1) {
					mtable->has_mii = 1;
					leaf->media = 11;
				} else
					leaf->media = p[2] & 0x0f;
				leaf->leafdata = p + 2;
				p += (p[0] & 0x3f) + 1;
			}
			if (tulip_debug > 1  &&  leaf->media == 11) {
				unsigned char *bp = leaf->leafdata;
				printk(KERN_INFO "%s:  MII interface PHY %d, setup/reset "
					   "sequences %d/%d long, capabilities %2.2x %2.2x.\n",
					   dev->name, bp[0], bp[1], bp[1 + bp[1]*2],
					   bp[5 + bp[2 + bp[1]*2]*2], bp[4 + bp[2 + bp[1]*2]*2]);
			}
			printk(KERN_INFO "%s:  Index #%d - Media %s (#%d) described "
				   "by a %s (%d) block.\n",
				   dev->name, i, medianame[leaf->media], leaf->media,
				   block_name[leaf->type], leaf->type);
		}
	}
}
/* Reading a serial EEPROM is a "bit" grungy, but we work our way through:->.*/

/*  EEPROM_Ctrl bits. */
#define EE_SHIFT_CLK	0x02	/* EEPROM shift clock. */
#define EE_CS			0x01	/* EEPROM chip select. */
#define EE_DATA_WRITE	0x04	/* EEPROM chip data in. */
#define EE_WRITE_0		0x01
#define EE_WRITE_1		0x05
#define EE_DATA_READ	0x08	/* EEPROM chip data out. */
#define EE_ENB			(0x4800 | EE_CS)

/* Delay between EEPROM clock transitions.
   The 1.2 code is a "nasty" timing loop, but PC compatible machines are
   *supposed* to delay an ISA-compatible period for the SLOW_DOWN_IO macro.  */
#ifdef _LINUX_DELAY_H
#define eeprom_delay(nanosec)	udelay((nanosec + 999)/1000)
#else
#define eeprom_delay(nanosec)	do { int _i = 3; while (--_i > 0) { __SLOW_DOWN_IO; }} while (0)
#endif

/* The EEPROM commands include the alway-set leading bit. */
#define EE_WRITE_CMD	(5 << 6)
#define EE_READ_CMD		(6 << 6)
#define EE_ERASE_CMD	(7 << 6)

static int read_eeprom(int ioaddr, int location)
{
	int i;
	unsigned short retval = 0;
	int ee_addr = ioaddr + CSR9;
	int read_cmd = location | EE_READ_CMD;
	
	outl(EE_ENB & ~EE_CS, ee_addr);
	outl(EE_ENB, ee_addr);
	
	/* Shift the read command bits out. */
	for (i = 10; i >= 0; i--) {
		short dataval = (read_cmd & (1 << i)) ? EE_DATA_WRITE : 0;
		outl(EE_ENB | dataval, ee_addr);
		eeprom_delay(100);
		outl(EE_ENB | dataval | EE_SHIFT_CLK, ee_addr);
		eeprom_delay(150);
		outl(EE_ENB | dataval, ee_addr);	/* Finish EEPROM a clock tick. */
		eeprom_delay(250);
	}
	outl(EE_ENB, ee_addr);
	
	for (i = 16; i > 0; i--) {
		outl(EE_ENB | EE_SHIFT_CLK, ee_addr);
		eeprom_delay(100);
		retval = (retval << 1) | ((inl(ee_addr) & EE_DATA_READ) ? 1 : 0);
		outl(EE_ENB, ee_addr);
		eeprom_delay(100);
	}

	/* Terminate the EEPROM access. */
	outl(EE_ENB & ~EE_CS, ee_addr);
	return retval;
}

/* Read and write the MII registers using software-generated serial
   MDIO protocol.  It is just different enough from the EEPROM protocol
   to not share code.  The maxium data clock rate is 2.5 Mhz. */
#define MDIO_SHIFT_CLK	0x10000
#define MDIO_DATA_WRITE0 0x00000
#define MDIO_DATA_WRITE1 0x20000
#define MDIO_ENB		0x00000		/* Ignore the 0x02000 databook setting. */
#define MDIO_ENB_IN		0x40000
#define MDIO_DATA_READ	0x80000
#ifdef _LINUX_DELAY_H
#define mdio_delay()	udelay(1)
#else
#define mdio_delay()	__SLOW_DOWN_IO
#endif

static int mdio_read(int ioaddr, int phy_id, int location)
{
	int i;
	int read_cmd = (0xf6 << 10) | (phy_id << 5) | location;
	unsigned short retval = 0;
	int mdio_addr = ioaddr + CSR9;

	/* Establish sync by sending at least 32 logic ones. */ 
	for (i = 32; i >= 0; i--) {
		outl(MDIO_ENB | MDIO_DATA_WRITE1, mdio_addr);
		mdio_delay();
		outl(MDIO_ENB | MDIO_DATA_WRITE1 | MDIO_SHIFT_CLK, mdio_addr);
		mdio_delay();
	}
	/* Shift the read command bits out. */
	for (i = 17; i >= 0; i--) {
		int dataval = (read_cmd & (1 << i)) ? MDIO_DATA_WRITE1 : 0;

		outl(dataval, mdio_addr);
		mdio_delay();
		outl(dataval | MDIO_SHIFT_CLK, mdio_addr);
		mdio_delay();
		outl(dataval, mdio_addr);
		mdio_delay();
	}
	outl(MDIO_ENB_IN | MDIO_SHIFT_CLK, mdio_addr);
	mdio_delay();
	outl(MDIO_ENB_IN, mdio_addr);

	for (i = 16; i > 0; i--) {
		outl(MDIO_ENB_IN | MDIO_SHIFT_CLK, mdio_addr);
		mdio_delay();
		retval = (retval << 1) | ((inl(mdio_addr) & MDIO_DATA_READ) ? 1 : 0);
		outl(MDIO_ENB_IN, mdio_addr);
		mdio_delay();
	}
	/* Clear out extra bits. */
	for (i = 16; i > 0; i--) {
		outl(MDIO_ENB_IN | MDIO_SHIFT_CLK, mdio_addr);
		mdio_delay();
		outl(MDIO_ENB_IN, mdio_addr);
		mdio_delay();
	}
	return retval;
}

#ifdef CONFIG_NET_HW_FLOWCONTROL
/* Enable receiver */

void tulip_xon(struct device *dev)
{
	struct tulip_private *lp = (struct tulip_private *)dev->priv;

	clear_bit(lp->fc_bit, &netdev_fc_xoff);
	if (dev->start)
				outl(lp->csr6 | 0x2002, dev->base_addr + CSR6);
}
#endif


static int
tulip_open(struct device *dev)
{
	struct tulip_private *tp = (struct tulip_private *)dev->priv;
	int ioaddr = dev->base_addr;
	int i = 0;

	/* On some chip revs we must set the MII/SYM port before the reset!? */
	if (tp->mtable  &&  tp->mtable->has_mii)
		outl(0x00040000, ioaddr + CSR6);

	/* Reset the chip, holding bit 0 set at least 50 PCI cycles. */
	outl(0x00000001, ioaddr + CSR0);
#ifdef _LINUX_DELAY_H
	udelay(2);
#else
	SLOW_DOWN_IO;
#endif
	/* Deassert reset.
	   486: Set 8 longword cache alignment, 8 longword burst.
	   586: Set 16 longword cache alignment, no burst limit.
	   Cache alignment bits 15:14	     Burst length 13:8
		0000	No alignment  0x00000000 unlimited		0800 8 longwords
		4000	8  longwords		0100 1 longword		1000 16 longwords
		8000	16 longwords		0200 2 longwords	2000 32 longwords
		C000	32  longwords		0400 4 longwords
	   Wait the specified 50 PCI cycles after a reset by initializing
	   Tx and Rx queues and the address filter list. */
#if defined(__alpha__)
	/* ToDo: Alpha setting could be better. */
	outl(0x00200000 | 0xE000, ioaddr + CSR0);
#elif defined(__powerpc__)
	outl(0x00200080 | 0x8000, ioaddr + CSR0);
#elif defined(__i386)
#if defined(MODULE)
	/* When a module we don't have 'x86' to check. */
	outl(0x00200000 | 0x4800, ioaddr + CSR0);
#else
#ifndef ORIGINAL_TEXT
#define x86 (boot_cpu_data.x86)
#endif
	outl(0x00200000 | (x86 <= 4 ? 0x4800 : 0x8000), ioaddr + CSR0);
	if (x86 <= 4)
	  printk(KERN_INFO "%s: This is a 386/486 PCI system, setting cache "
			 "alignment to %x.\n", dev->name,
			 0x00200000 | (x86 <= 4 ? 0x4800 : 0x8000));
#endif
#else
	outl(0x00200000 | 0x4800, ioaddr + CSR0);
#warning Processor architecture undefined!
#endif

#ifdef SA_SHIRQ
	if (request_irq(dev->irq, &tulip_interrupt, SA_SHIRQ,
					tulip_tbl[tp->chip_id].chip_name, dev)) {
		return -EAGAIN;
	}
#else
	if (irq2dev_map[dev->irq] != NULL
		|| (irq2dev_map[dev->irq] = dev) == NULL
		|| dev->irq == 0
		|| request_irq(dev->irq, &tulip_interrupt, 0,
					   tulip_tbl[tp->chip_id].chip_name)) {
		return -EAGAIN;
	}
#endif

	if (tulip_debug > 1)
		printk(KERN_DEBUG "%s: tulip_open() irq %d.\n", dev->name, dev->irq);

	MOD_INC_USE_COUNT;

	tulip_init_ring(dev);

	/* This is set_rx_mode(), but without starting the transmitter. */
	/* Fill the whole address filter table with our physical address. */
	{
		u16 *eaddrs = (u16 *)dev->dev_addr;
		u32 *setup_frm = tp->setup_frame, i;

		/* You must add the broadcast address when doing perfect filtering! */
		*setup_frm++ = 0xffff;
		*setup_frm++ = 0xffff;
		*setup_frm++ = 0xffff;
		/* Fill the rest of the accept table with our physical address. */
		for (i = 1; i < 16; i++) {
			*setup_frm++ = eaddrs[0];
			*setup_frm++ = eaddrs[1];
			*setup_frm++ = eaddrs[2];
		}
		/* Put the setup frame on the Tx list. */
		tp->tx_ring[0].length = 0x08000000 | 192;
		tp->tx_ring[0].buffer1 = virt_to_bus(tp->setup_frame);
		tp->tx_ring[0].status = 0x80000000;

		tp->cur_tx++;
	}

	outl(virt_to_bus(tp->rx_ring), ioaddr + CSR3);
	outl(virt_to_bus(tp->tx_ring), ioaddr + CSR4);

	if (dev->if_port == 0)
	  dev->if_port = tp->default_port;
	if (tp->chip_id == DC21041  &&  dev->if_port > 4)
		/* Invalid: Select initial TP, autosense, autonegotiate.  */
		dev->if_port = 4;

	/* Allow selecting a default media. */
	if (tp->mtable == NULL)
		goto media_picked;
	if (dev->if_port)
		for (i = 0; i < tp->mtable->leafcount; i++)
		  if (tp->mtable->mleaf[i].media ==
			  (dev->if_port == 12 ? 0 : dev->if_port)) {
			printk(KERN_INFO "%s: Using user-specified media %s.\n",
				   dev->name, medianame[dev->if_port]);
			goto media_picked;
		  }
	if ((tp->mtable->defaultmedia & 0x0800) == 0)
		for (i = 0; i < tp->mtable->leafcount; i++)
		  if (tp->mtable->mleaf[i].media == (tp->mtable->defaultmedia & 15)) {
			printk(KERN_INFO "%s: Using EEPROM-set media %s.\n",
				   dev->name, medianame[tp->mtable->mleaf[i].media]);
			goto media_picked;
		  }
	for (i = tp->mtable->leafcount - 1;
		 (media_fd[tp->mtable->mleaf[i].media] & 2) && i > 0; i--)
	  ;
media_picked:

	tp->cur_index = i;
	tp->csr6 = 0;
	select_media(dev, 1);

	/* Start the chip's Tx to process setup frame. */
	outl(tp->csr6, ioaddr + CSR6);
	outl(tp->csr6 | 0x2000, ioaddr + CSR6);

	dev->tbusy = 0;
	dev->interrupt = 0;
	dev->start = 1;


	/* Enable interrupts by setting the interrupt mask. */
	outl(0x0001ebef, ioaddr + CSR7);
	outl(tp->csr6 | 0x2002, ioaddr + CSR6);
	outl(0, ioaddr + CSR2);		/* Rx poll demand */

	if (tulip_debug > 2) {
		printk(KERN_DEBUG "%s: Done tulip_open(), CSR0 %8.8x, CSR5 %8.8x CSR13 %8.8x.\n",
			   dev->name, inl(ioaddr + CSR0), inl(ioaddr + CSR5),
			   inl(ioaddr + CSR13));
	}
	/* Set the timer to switch to check for link beat and perhaps switch
	   to an alternate media type. */
	init_timer(&tp->timer);
	tp->timer.expires = RUN_AT((24*HZ)/10);			/* 2.4 sec. */
	tp->timer.data = (unsigned long)dev;
	tp->timer.function = &tulip_timer;				/* timer handler */
	add_timer(&tp->timer);

#ifdef CONFIG_NET_HW_FLOWCONTROL
	tp->fc_bit = netdev_register_fc(dev, tulip_xon);
#endif
#ifdef CONFIG_NET_FASTROUTE
	dev->tx_semaphore = 1;
#endif
	return 0;
}

/* Set up the transceiver control registers for the selected media type. */
static void select_media(struct device *dev, int startup)
{
	int ioaddr = dev->base_addr;
	struct tulip_private *tp = (struct tulip_private *)dev->priv;
	struct mediatable *mtable = tp->mtable;
	u32 new_csr6;
	int check_mii =0, i;

	if (mtable) {
		struct medialeaf *mleaf = &mtable->mleaf[tp->cur_index];
		unsigned char *p = mleaf->leafdata;
		switch (mleaf->type) {
		case 0:					/* 21140 non-MII xcvr. */
			if (tulip_debug > 1)
				printk(KERN_DEBUG "%s: Using a 21140 non-MII transceiver with control"
					   " setting %2.2x.\n",
					   dev->name, p[1]);
			dev->if_port = p[0];
			if (startup)
				outl(mtable->csr12dir | 0x100, ioaddr + CSR12);
			outl(p[1], ioaddr + CSR12);
			new_csr6 = 0x02000000 | ((p[2] & 0x71) << 18);
			break;
		case 1:
			if (startup) {
				outl(mtable->csr12dir | 0x100, ioaddr + CSR12);
				dev->if_port = 11;
				if (tulip_debug > 2)
					printk(KERN_DEBUG "%s:  Doing a reset sequence of length %d.\n",
						   dev->name, p[2 + p[1]]);
				for (i = 0; i < p[2 + p[1]]; i++)
					outl(p[3 + p[1] + i], ioaddr + CSR12);
				if (tulip_debug > 2)
					printk(KERN_DEBUG "%s  Doing a transceiver setup sequence of length %d.\n",
						   dev->name, p[1]);
				for (i = 0; i < p[1]; i++)
					outl(p[2 + i], ioaddr + CSR12);
			}
			check_mii = 1;
			new_csr6 = 0x020C0000;
			break;
		case 2: case 4: {
			u16 *setup = (u16*)&p[1];
			dev->if_port = p[0] & 15;
			if (tulip_debug > 1)
				printk(KERN_DEBUG "%s: 21142 non-MII %s transceiver control %4.4x/%4.4x.\n",
					   dev->name, medianame[dev->if_port], setup[0], setup[1]);
			if (p[0] & 0x40) {	/* SIA (CSR13-15) setup values are provided. */
				outl(0, ioaddr + CSR13);
				outl(setup[1], ioaddr + CSR14);
				outl(setup[2], ioaddr + CSR15);
				outl(setup[0], ioaddr + CSR13);
				setup += 3;
			} else {
				outl(0, ioaddr + CSR13);
				outl(t21142_csr14[dev->if_port], ioaddr + CSR14);
				outl(t21142_csr15[dev->if_port], ioaddr + CSR15);
				outl(t21142_csr13[dev->if_port], ioaddr + CSR13);
			}
			outl(setup[0]<<16, ioaddr + CSR15);	/* Direction */
			outl(setup[1]<<16, ioaddr + CSR15);	/* Data */
			if (mleaf->type == 4)
				new_csr6 = 0x02000000 | ((setup[2] & 0x71) << 18);
			else
				new_csr6 = 0x82420000;
			break;
		}
		case 3: {
			int init_length = p[1];
			u16 * init_sequence = (u16*)(p + 2);
			int reset_length = p[2 + init_length*2];
			u16 * reset_sequence = (u16*)&p[3 + init_length*2];

			dev->if_port = 11;
			if (startup) {
				if (tulip_debug > 2)
					printk(KERN_DEBUG "%s:  Doing a 21142 reset sequence of length %d.\n",
						   dev->name, reset_length);
				for (i = 0; i < reset_length; i++)
					outl(reset_sequence[i] << 16, ioaddr + CSR15);
			}
			if (tulip_debug > 2)
				printk(KERN_DEBUG "%s: Doing a 21142 xcvr setup sequence of length %d.\n",
					   dev->name, init_length);
			for (i = 0; i < init_length; i++)
				outl(init_sequence[i] << 16, ioaddr + CSR15);
			check_mii = 1;
			new_csr6 = 0x020C0000;
			break;
		}
		default:
		  new_csr6 = 0x020C0000;
		}
		if (tulip_debug > 1)
			printk(KERN_DEBUG "%s: Using media type %s, CSR12 is %2.2x.\n",
				   dev->name, medianame[dev->if_port],
				   inl(ioaddr + CSR12) & 0xff);
	} else if (tp->chip_id == DC21140) {
		/* Set media type to MII @ 100mbps: 0x020C0000 */
		new_csr6 = 0x020C0000;
		dev->if_port = 11;
		if (tulip_debug > 1) {
			printk(KERN_DEBUG "%s: Unknown media control, assuming MII, CSR12 %2.2x.\n",
				   dev->name, inl(ioaddr + CSR12) & 0xff);
		}
	} else if (tp->chip_id == DC21041) {
		if (tulip_debug > 1)
			printk(KERN_DEBUG "%s: 21041 using media %s, CSR12 is %4.4x.\n",
				   dev->name, medianame[dev->if_port & 15],
				   inl(ioaddr + CSR12) & 0xffff);
		outl(0x00000000, ioaddr + CSR13); /* Reset the serial interface */
		outl(t21041_csr14[dev->if_port], ioaddr + CSR14);
		outl(t21041_csr15[dev->if_port], ioaddr + CSR15);
		outl(t21041_csr13[dev->if_port], ioaddr + CSR13);
		new_csr6 = 0x80020000;
	} else if (tp->chip_id == LC82C168) {
		if (startup)
			dev->if_port = 3;
		if (tulip_debug > 1)
			printk(KERN_DEBUG "%s: LiteOn PHY status is %3.3x, CSR12 %4.4x,"
				   " media %s.\n",
				   dev->name, inl(ioaddr + 0xB8), inl(ioaddr + CSR12),
				   medianame[dev->if_port]);
		if (dev->if_port == 3  ||  dev->if_port == 5) {
			outl(0x33, ioaddr + CSR12);
			new_csr6 = 0x01860000;
			if (startup)
				outl(0x0201F868, ioaddr + 0xB8); /* Trigger autonegotiation. */
			else
				outl(0x1F868, ioaddr + 0xB8);
		} else {
			outl(0x32, ioaddr + CSR12);
			new_csr6 = 0x00420000;
			outl(0x1F078, ioaddr + 0xB8);
		}
	} else {					/* 21040 */
		/* Turn on the xcvr interface. */
		int csr12 = inl(ioaddr + CSR12);
		if (tulip_debug > 1)
			printk(KERN_DEBUG "%s: 21040 media type is %s, CSR12 is %2.2x.\n",
				   dev->name, dev->if_port ? "AUI" : "10baseT", csr12);
		new_csr6 = (dev->if_port ? 0x01860000 : 0x00420000);
		/* Set the full duplux match frame. */
		outl(FULL_DUPLEX_MAGIC, ioaddr + CSR11);
		outl(0x00000000, ioaddr + CSR13); /* Reset the serial interface */
		outl(dev->if_port ? 0x0000000C : 0x00000004, ioaddr + CSR13);
	}

	tp->csr6 = new_csr6 | (tp->csr6 & 0xfdff) | (tp->full_duplex ? 0x0200 : 0);
	return;
}

static void tulip_timer(unsigned long data)
{
	struct device *dev = (struct device *)data;
	struct tulip_private *tp = (struct tulip_private *)dev->priv;
	int ioaddr = dev->base_addr;
	u32 csr12 = inl(ioaddr + CSR12);
	int next_tick = 0;

	if (tulip_debug > 3) {
		printk(KERN_DEBUG "%s: Media selection tick, status %8.8x mode %8.8x "
			   "SIA %8.8x %8.8x %8.8x %8.8x.\n",
			   dev->name, inl(ioaddr + CSR5), inl(ioaddr + CSR6),
			   csr12, inl(ioaddr + CSR13),
			   inl(ioaddr + CSR14), inl(ioaddr + CSR15));
	}
	switch (tp->chip_id) {
	case DC21040:
		if (csr12 & 0x0002) { /* Network error */
			printk(KERN_INFO "%s: No 10baseT link beat found, switching to %s media.\n",
				   dev->name, dev->if_port ? "10baseT" : "AUI");
			dev->if_port ^= 1;
			outl(dev->if_port ? 0x0000000C : 0x00000004, ioaddr + CSR13);
			dev->trans_start = jiffies;
		}
		break;
	case DC21041:
		if (tulip_debug > 2)
			printk(KERN_DEBUG "%s: 21041 media tick  CSR12 %8.8x.\n",
				   dev->name, csr12);
		switch (dev->if_port) {
		case 0: case 3: case 4:
		  if (csr12 & 0x0004) { /*LnkFail */
			/* 10baseT is dead.  Check for activity on alternate port. */
			tp->mediasense = 1;
			if (csr12 & 0x0200)
				dev->if_port = 2;
			else
				dev->if_port = 1;
			printk(KERN_INFO "%s: No 21041 10baseT link beat, Media switched to %s.\n",
				   dev->name, medianame[dev->if_port]);
			outl(0, ioaddr + CSR13); /* Reset */
			outl(t21041_csr14[dev->if_port], ioaddr + CSR14);
			outl(t21041_csr15[dev->if_port], ioaddr + CSR15);
			outl(t21041_csr13[dev->if_port], ioaddr + CSR13);
			next_tick = 10*HZ;			/* 2.4 sec. */
		  } else
			next_tick = 30*HZ;
		  break;
		case 1:					/* 10base2 */
		case 2:					/* AUI */
		  if (csr12 & 0x0100) {
			next_tick = (30*HZ);			/* 30 sec. */
			tp->mediasense = 0;
		  } else if ((csr12 & 0x0004) == 0) {
			printk(KERN_INFO "%s: 21041 media switched to 10baseT.\n", dev->name);
			dev->if_port = 0;
			select_media(dev, 0);
			next_tick = (24*HZ)/10;				/* 2.4 sec. */
		  } else if (tp->mediasense || (csr12 & 0x0002)) {
			dev->if_port = 3 - dev->if_port; /* Swap ports. */
			select_media(dev, 0);
			next_tick = 20*HZ;
		  } else {
			next_tick = 20*HZ;
		  }
		  break;
		}
		break;
	case LC82C168: {
		int phy_reg = inl(ioaddr + 0xB8);
		if (tulip_debug > 1)
			printk(KERN_DEBUG "%s: LC82C168 phy status %8.8x, CSR5 %8.8x.\n",
				   dev->name, phy_reg, inl(ioaddr + CSR5));
		if (phy_reg & 0x04000000) {	/* Remote link fault */
			outl(0x0201F078, ioaddr + 0xB8);
			next_tick = (24*HZ)/10;
		} else
			next_tick = 10*HZ;
		if (inl(ioaddr + CSR5) & TPLnkFail) { /* 100baseTx link beat */
			if (tulip_debug > 1)
				printk(KERN_DEBUG "%s: %s link beat failed, CSR12 %4.4x, "
					   "CSR5 %8.8x, PHY %3.3x.\n",
					   dev->name, medianame[dev->if_port], csr12,
					   inl(ioaddr + CSR5), inl(ioaddr + 0xB8));
			if (dev->if_port == 0) {
				dev->if_port = 3;
			} else
				dev->if_port = 0;
			next_tick = (24*HZ)/10;
			select_media(dev, 0);
			outl(tp->csr6 | 0x0002, ioaddr + CSR6);
			outl(tp->csr6 | 0x2002, ioaddr + CSR6);
			dev->trans_start = jiffies;
		}
		break;
	}
	case DC21140:  case DC21142: {
		struct medialeaf *mleaf;
		unsigned char *p;
		if (tp->mtable == NULL) {	/* No EEPROM info, use generic code. */
			/* Assume this is like a SMC card, and check its link beat bit. */
			if ((dev->if_port == 0 && (csr12 & 0x0080)) ||
				(dev->if_port == 1 && (csr12 & 0x0040) == 0)) {
				dev->if_port ^= 1;
				/* Stop the transmit process. */
				tp->csr6 = (dev->if_port ? 0x03860000 : 0x02420000);
				outl(tp->csr6 | 0x0002, ioaddr + CSR6);
				printk(KERN_INFO "%s: link beat timed out, CSR12 is 0x%2.2x, switching to"
					   " %s media.\n", dev->name,
					   csr12 & 0xff,
					   dev->if_port ? "100baseTx" : "10baseT");
				outl(tp->csr6 | 0xA002, ioaddr + CSR6);
				dev->trans_start = jiffies;
				next_tick = (24*HZ)/10;
			} else {
				next_tick = 10*HZ;
				if (tulip_debug > 2)
					printk(KERN_DEBUG "%s: network media monitor 0x%2.2x, link"
						   " beat detected as %s.\n", dev->name,
						   csr12 & 0xff,
						   dev->if_port ? "100baseTx" : "10baseT");
			}
			break;
		}
	  mleaf = &tp->mtable->mleaf[tp->cur_index];
	  p = mleaf->leafdata;
	  switch (mleaf->type) {
	  case 0: case 4: {
		/* Type 0 non-MII or #4 SYM transceiver.  Check the link beat bit. */
		  s8 bitnum = p[mleaf->type == 4 ? 5 : 2];
		  if (tulip_debug > 2)
			  printk(KERN_DEBUG "%s: Transceiver monitor tick: CSR12=%#2.2x bit %d is"
					 " %d, expecting %d.\n",
					 dev->name, csr12, (bitnum >> 1) & 7,
					 (csr12 & (1 << ((bitnum >> 1) & 7))) != 0,
					 (bitnum >= 0));
		  /* Check that the specified bit has the proper value. */
		  if ((bitnum < 0) !=
			  ((csr12 & (1 << ((bitnum >> 1) & 7))) != 0)) {
			  if (tulip_debug > 1)
				  printk(KERN_DEBUG "%s: Link beat detected for %s.\n", dev->name,
						 medianame[mleaf->media]);
			  break;
		  }
		  if (tp->medialock)
			  break;
	  select_next_media:
		  if (--tp->cur_index < 0) {
			/* We start again, but should instead look for default. */
			tp->cur_index = tp->mtable->leafcount - 1;
		  }
		  dev->if_port = tp->mtable->mleaf[tp->cur_index].media;
		  if (media_fd[dev->if_port])
			goto select_next_media; /* Skip FD entries. */
		  if (tulip_debug > 1)
			  printk(KERN_DEBUG "%s: No link beat on media %s,"
					 " trying transceiver type %s.\n",
					 dev->name, medianame[mleaf->media & 15],
					 medianame[tp->mtable->mleaf[tp->cur_index].media]);
		  select_media(dev, 0);
		  /* Restart the transmit process. */
		  outl(tp->csr6 | 0x0002, ioaddr + CSR6);
		  outl(tp->csr6 | 0x2002, ioaddr + CSR6);
		  next_tick = (24*HZ)/10;
		  break;
	  }
	  case 1:  case 3:		/* 21140, 21142 MII */
		  {
			  int mii_reg1 = mdio_read(ioaddr, tp->phys[0], 1);
			  int mii_reg5 = mdio_read(ioaddr, tp->phys[0], 5);
			  printk(KERN_INFO "%s: MII monitoring tick: CSR12 %2.2x, "
					 "MII status %4.4x, Link partner report %4.4x.\n",
					 dev->name, csr12, mii_reg1, mii_reg5);
#ifdef notdef
			  if (mii_reg1 != 0xffff  &&  (mii_reg1 & 0x0004) == 0)
				  goto select_next_media;
#else
			  if (mii_reg1 != 0xffff  &&  (mii_reg1 & 0x0004) == 0)
				  printk(KERN_INFO "%s: No link beat on the MII interface, "
						 "status then %4.4x now %4.4x.\n",
						 dev->name, mii_reg1,
						 mdio_read(ioaddr, tp->phys[0], 1));
#endif
			  if (mii_reg5 == 0xffff  ||  mii_reg5 == 0x0000)
				  ;				/* No MII device or no link partner report */
			  else if (tp->full_duplex_lock)
				  ;
			  else if ((mii_reg5 & 0x0100) != 0
					   || (mii_reg5 & 0x00C0) == 0x0040) {
				  /* 100baseTx-FD  or  10T-FD, but not 100-HD */
				  if (tp->full_duplex == 0) {
					  tp->full_duplex = 1;
					  tp->csr6 |= 0x0200;
					  outl(tp->csr6 | 0x0002, ioaddr + CSR6);
					  outl(tp->csr6 | 0x2002, ioaddr + CSR6);
				  }
				  if (tulip_debug > 0) /* Gurppp, should be >1 */
					  printk(KERN_INFO "%s: Setting %s-duplex based on MII"
							 " Xcvr #%d parter capability of %4.4x.\n",
							 dev->name, full_duplex ? "full" : "half",
							 tp->phys[0], mii_reg5);
			  }
		  }
		  break;
	  case 2:					/* 21142 serial block has no link beat. */
	  default:
		  break;
	  }
	}
	break;
	default:					/* Invalid chip type. */
	  break;
	}
	if (next_tick) {
		tp->timer.expires = RUN_AT(next_tick);
		add_timer(&tp->timer);
	}
}

static void tulip_tx_timeout(struct device *dev)
{
  struct tulip_private *tp = (struct tulip_private *)dev->priv;
  int ioaddr = dev->base_addr;

  if (tp->mtable && tp->mtable->has_mii) {
	/* Do nothing -- the media monitor should handle this. */
	if (tulip_debug > 1)
	  printk(KERN_WARNING "%s: Transmit timeout using MII device.\n",
			 dev->name);
  } else if (tp->chip_id == DC21040) {
	  if (inl(ioaddr + CSR12) & 0x0002) {
		  printk(KERN_INFO "%s: transmit timed out, switching to %s media.\n",
				 dev->name, dev->if_port ? "10baseT" : "AUI");
		  dev->if_port ^= 1;
		  outl(dev->if_port ? 0x0000000C : 0x00000004, ioaddr + CSR13);
	  }
	  dev->trans_start = jiffies;
	  return;
  } else if (tp->chip_id == DC21140 || tp->chip_id == DC21142) {
	/* Stop the transmit process. */
	outl(tp->csr6 | 0x0002, ioaddr + CSR6);
	dev->if_port ^= 1;
	printk(KERN_WARNING "%s: 21140 transmit timed out, status %8.8x, "
		   "SIA %8.8x %8.8x %8.8x %8.8x, resetting...\n",
		   dev->name, inl(ioaddr + CSR5), inl(ioaddr + CSR12),
		   inl(ioaddr + CSR13), inl(ioaddr + CSR14), inl(ioaddr + CSR15));
	printk(KERN_WARNING "%s: transmit timed out, switching to %s media.\n",
		   dev->name, dev->if_port ? "100baseTx" : "10baseT");
	outl(tp->csr6 | 0x2002, ioaddr + CSR6);
	tp->stats.tx_errors++;
	dev->trans_start = jiffies;
	return;
  } else if (tp->chip_id == DC21041) {
	u32 csr12 = inl(ioaddr + CSR12);

	printk(KERN_WARNING "%s: 21041 transmit timed out, status %8.8x, CSR12 %8.8x,"
		   " CSR13 %8.8x, CSR14 %8.8x, resetting...\n",
		   dev->name, inl(ioaddr + CSR5), csr12,
		   inl(ioaddr + CSR13), inl(ioaddr + CSR14));
	tp->mediasense = 1;
	if (dev->if_port == 1 || dev->if_port == 2)
		if (csr12 & 0x0004) {
			dev->if_port = 2 - dev->if_port;
		} else
			dev->if_port = 0;
	else
		dev->if_port = 1;
	select_media(dev, 0);
	tp->stats.tx_errors++;
	dev->trans_start = jiffies;
	return;
  } else
	printk(KERN_WARNING "%s: transmit timed out, status %8.8x, CSR12 %8.8x,"
		   " resetting...\n",
		   dev->name, inl(ioaddr + CSR5), inl(ioaddr + CSR12));
#ifdef way_too_many_messages
  printk("  Rx ring %8.8x: ", (int)tp->rx_ring);
  for (i = 0; i < RX_RING_SIZE; i++)
	printk(" %8.8x", (unsigned int)tp->rx_ring[i].status);
  printk("\n  Tx ring %8.8x: ", (int)tp->tx_ring);
  for (i = 0; i < TX_RING_SIZE; i++)
	printk(" %8.8x", (unsigned int)tp->tx_ring[i].status);
  printk("\n");
#endif

  /* Perhaps we should reinitialize the hardware here. */
  dev->if_port = 0;
  /* Stop and restart the chip's Tx processes . */
  outl(tp->csr6 | 0x0002, ioaddr + CSR6);
  outl(tp->csr6 | 0x2002, ioaddr + CSR6);
  /* Trigger an immediate transmit demand. */
  outl(0, ioaddr + CSR1);

  dev->trans_start = jiffies;
  tp->stats.tx_errors++;
  return;
}


/* Initialize the Rx and Tx rings, along with various 'dev' bits. */
static void
tulip_init_ring(struct device *dev)
{
	struct tulip_private *tp = (struct tulip_private *)dev->priv;
	int i;

	tp->tx_full = 0;
	tp->cur_rx = tp->cur_tx = 0;
	tp->dirty_rx = tp->dirty_tx = 0;

	for (i = 0; i < RX_RING_SIZE; i++) {
		tp->rx_ring[i].status = 0x80000000;	/* Owned by Tulip chip */
		tp->rx_ring[i].length = PKT_BUF_SZ;
		{
			/* Note the receive buffer must be longword aligned.
			   dev_alloc_skb() provides 16 byte alignment.  But do *not*
			   use skb_reserve() to align the IP header! */
			struct sk_buff *skb;
			skb = DEV_ALLOC_SKB(PKT_BUF_SZ);
			tp->rx_skbuff[i] = skb;
			if (skb == NULL)
				break;			/* Bad news!  */
			skb->dev = dev;			/* Mark as being used by this device. */
#if LINUX_VERSION_CODE > 0x10300
			tp->rx_ring[i].buffer1 = virt_to_bus(skb->tail);
#else
			tp->rx_ring[i].buffer1 = virt_to_bus(skb->data);
#endif
		}
		tp->rx_ring[i].buffer2 = virt_to_bus(&tp->rx_ring[i+1]);
	}
	/* Mark the last entry as wrapping the ring. */
	tp->rx_ring[i-1].length = PKT_BUF_SZ | 0x02000000;
	tp->rx_ring[i-1].buffer2 = virt_to_bus(&tp->rx_ring[0]);

	/* The Tx buffer descriptor is filled in as needed, but we
	   do need to clear the ownership bit. */
	for (i = 0; i < TX_RING_SIZE; i++) {
		tp->tx_skbuff[i] = 0;
		tp->tx_ring[i].status = 0x00000000;
		tp->tx_ring[i].buffer2 = virt_to_bus(&tp->tx_ring[i+1]);
	}
	tp->tx_ring[i-1].buffer2 = virt_to_bus(&tp->tx_ring[0]);
}

static int
tulip_start_xmit(struct sk_buff *skb, struct device *dev)
{
	struct tulip_private *tp = (struct tulip_private *)dev->priv;
	int entry;
	u32 flag;

#ifdef ORIGINAL_TEXT
#ifndef final_version
	if (skb == NULL || skb->len <= 0) {
		printk(KERN_ERR "%s: Obsolete driver layer request made: skbuff==NULL.\n",
			   dev->name);
		dev_tint(dev);
		return 0;
	}
#endif
#endif

#ifdef CONFIG_NET_FASTROUTE
	cli();
	if (xchg(&dev->tx_semaphore,0) == 0) {
		sti();
		/* With new queueing algorithm returning 1 when dev->tbusy == 0
		   should not result in lockups, but I am still not sure. --ANK
		 */
		if (net_ratelimit())
				printk(KERN_CRIT "Please check: are you still alive?\n");
		return 1;
	}
#endif
	/* Block a timer-based transmit from overlapping.  This could better be
	   done with atomic_swap(1, dev->tbusy), but set_bit() works as well. */
	if (test_and_set_bit(0, (void*)&dev->tbusy) != 0) {
#ifdef CONFIG_NET_FASTROUTE
		sti();
#endif
		if (jiffies - dev->trans_start >= TX_TIMEOUT)
				tulip_tx_timeout(dev);
#ifdef CONFIG_NET_FASTROUTE
		dev->tx_semaphore = 1;
#endif
		return 1;
	}

	/* Caution: the write order is important here, set the base address
	   with the "ownership" bits last. */

	/* Calculate the next Tx descriptor entry. */
	entry = tp->cur_tx % TX_RING_SIZE;

	tp->tx_skbuff[entry] = skb;
	tp->tx_ring[entry].buffer1 = virt_to_bus(skb->data);

	if (tp->cur_tx - tp->dirty_tx < TX_RING_SIZE/2) {/* Typical path */
	  flag = 0x60000000; /* No interrupt */
	  dev->tbusy = 0;
	} else if (tp->cur_tx - tp->dirty_tx == TX_RING_SIZE/2) {
	  flag = 0xe0000000; /* Tx-done intr. */
	  dev->tbusy = 0;
	} else if (tp->cur_tx - tp->dirty_tx < TX_RING_SIZE - 2) {
	  flag = 0x60000000; /* No Tx-done intr. */
	  dev->tbusy = 0;
	} else {
	  /* Leave room for set_rx_mode() to fill entries. */
	  flag = 0xe0000000; /* Tx-done intr. */
	  tp->tx_full = 1;
	}
	if (entry == TX_RING_SIZE-1)
		flag |= 0xe2000000;

	tp->stats.tx_bytes += skb->len;
	tp->tx_ring[entry].length = skb->len | flag;
	tp->tx_ring[entry].status = 0x80000000;	/* Pass ownership to the chip. */
	tp->cur_tx++;
	/* Trigger an immediate transmit demand. */
	outl(0, dev->base_addr + CSR1);

	dev->trans_start = jiffies;
#ifdef CONFIG_NET_FASTROUTE
	dev->tx_semaphore = 1;
	sti();
#endif

	return 0;
}

/* The interrupt handler does all of the Rx thread work and cleans up
   after the Tx thread. */
static void tulip_interrupt IRQ(int irq, void *dev_instance, struct pt_regs *regs)
{
#ifdef SA_SHIRQ		/* Use the now-standard shared IRQ implementation. */
	struct device *dev = (struct device *)dev_instance;
#else
	struct device *dev = (struct device *)(irq2dev_map[irq]);
#endif

	struct tulip_private *lp;
	int csr5, ioaddr, boguscnt = 12;

	if (dev == NULL) {
		printk (KERN_ERR" tulip_interrupt(): irq %d for unknown device.\n",
				irq);
		return;
	}

	ioaddr = dev->base_addr;
	lp = (struct tulip_private *)dev->priv;
	if (dev->interrupt)
		printk(KERN_ERR "%s: Re-entering the interrupt handler.\n", dev->name);

	dev->interrupt = 1;

	do {
		csr5 = inl(ioaddr + CSR5);
		/* Acknowledge all of the current interrupt sources ASAP. */
		outl(csr5 & 0x0001ffff, ioaddr + CSR5);

		if (tulip_debug > 4)
			printk(KERN_DEBUG "%s: interrupt  csr5=%#8.8x new csr5=%#8.8x.\n",
				   dev->name, csr5, inl(dev->base_addr + CSR5));

		if ((csr5 & 0x00018000) == 0)
			break;

		if (csr5 & RxIntr)
			tulip_rx(dev);

		if (csr5 & (TxNoBuf | TxDied | TxIntr)) {
			int dirty_tx;

			for (dirty_tx = lp->dirty_tx; dirty_tx < lp->cur_tx; dirty_tx++) {
				int entry = dirty_tx % TX_RING_SIZE;
				int status = lp->tx_ring[entry].status;

				if (status < 0)
					break;			/* It still hasn't been Txed */
				/* Check for Rx filter setup frames. */
				if (lp->tx_skbuff[entry] == NULL)
				  continue;

				if (status & 0x8000) {
					/* There was an major error, log it. */
#ifndef final_version
					if (tulip_debug > 1)
						printk(KERN_DEBUG "%s: Transmit error, Tx status %8.8x.\n",
							   dev->name, status);
#endif
					lp->stats.tx_errors++;
					if (status & 0x4104) lp->stats.tx_aborted_errors++;
					if (status & 0x0C00) lp->stats.tx_carrier_errors++;
					if (status & 0x0200) lp->stats.tx_window_errors++;
					if (status & 0x0002) lp->stats.tx_fifo_errors++;
					if ((status & 0x0080) && lp->full_duplex == 0)
						lp->stats.tx_heartbeat_errors++;
#ifdef ETHER_STATS
					if (status & 0x0100) lp->stats.collisions16++;
#endif
				} else {
#ifdef ETHER_STATS
					if (status & 0x0001) lp->stats.tx_deferred++;
#endif
					lp->stats.collisions += (status >> 3) & 15;
					lp->stats.tx_packets++;
				}

				/* Free the original skb. */
				dev_kfree_skb(lp->tx_skbuff[entry]);
				lp->tx_skbuff[entry] = 0;
			}

#ifndef final_version
			if (lp->cur_tx - dirty_tx > TX_RING_SIZE) {
				printk(KERN_ERR "%s: Out-of-sync dirty pointer, %d vs. %d, full=%d.\n",
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

		/* Log errors. */
		if (csr5 & 0x8000) {	/* Abnormal error summary bit. */
			if (csr5 & TxJabber) lp->stats.tx_errors++;
			if (csr5 & TxFIFOUnderflow) {
			  lp->csr6 |= 0x00200000;  /* Reconfigure to store-n-forward. */
			  /* Restart the transmit process. */
			  outl(lp->csr6 | 0x0002, ioaddr + CSR6);
			  outl(lp->csr6 | 0x2002, ioaddr + CSR6);
			}
			if (csr5 & RxDied) {		/* Missed a Rx frame. */
				lp->stats.rx_errors++;
				lp->stats.rx_missed_errors += inl(ioaddr + CSR8) & 0xffff;
			}
			if (csr5 & TimerInt) {
				printk(KERN_ERR "%s: Something Wicked happened! %8.8x.\n",
					   dev->name, csr5);
				/* Hmmmmm, it's not clear what to do here. */
			}
			/* Clear all error sources, included undocumented ones! */
			outl(0x000f7ba, ioaddr + CSR5);
		}
		if (--boguscnt < 0) {
			printk(KERN_WARNING "%s: Too much work at interrupt, csr5=0x%8.8x.\n",
				   dev->name, csr5);
			/* Clear all interrupt sources. */
			outl(0x0001ffff, ioaddr + CSR5);
			break;
		}
	} while (1);

	if (tulip_debug > 3)
		printk(KERN_DEBUG "%s: exiting interrupt, csr5=%#4.4x.\n",
			   dev->name, inl(ioaddr + CSR5));

	/* Code that should never be run!  Perhaps remove after testing.. */
	{
		static int stopit = 10;
		if (dev->start == 0  &&  --stopit < 0) {
			printk(KERN_ERR "%s: Emergency stop, looping startup interrupt.\n"
				   KERN_ERR "%s: Disabling interrupt handler %d to avoid "
				   "locking up the machine.\n",
				   dev->name, dev->name, dev->irq);
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

#ifdef CONFIG_NET_FASTROUTE
/* DMAing cards are the most easy in this respect,
   they are able to make fast route to any device.

   Now we allow to make it only to another ethernet card.
 */
static int tulip_accept_fastpath(struct device *dev, struct dst_entry *dst)
{
	struct device *odev = dst->dev;

	if (dst->ops->protocol != __constant_htons(ETH_P_IP))
			return -1;
	if (odev->type != ARPHRD_ETHER || odev->accept_fastpath == NULL)
			return -1;

	return 0;
}

/*
   Return values:

   0 - packet has gone by fast path.
   1 - fast path is OK, but device deferred xmit. (semifast path)

   2 - fast path is hit, but packet is a bit strange. (NI)
   3 - oom

   4 - fast path miss.
 */

static int tulip_fast_forward(struct device *dev, int entry, int len)
{
	struct tulip_private *lp = (struct tulip_private *)dev->priv;
	struct sk_buff *skb = lp->rx_skbuff[entry];
	struct ethhdr *eth = (void*)skb->data;

	if (eth->h_proto == __constant_htons(ETH_P_IP)) {
		struct rtable *rt;
		struct iphdr *iph;
		unsigned h;

		iph = (struct iphdr*)(skb->data + ETH_HLEN);
		h = (*(u8*)&iph->daddr^*(u8*)&iph->saddr)&NETDEV_FASTROUTE_HMASK;
		rt = (struct rtable*)(dev->fastpath[h]);
		if (rt &&
			((u16*)&iph->daddr)[0] == ((u16*)&rt->key.dst)[0] &&
			((u16*)&iph->daddr)[1] == ((u16*)&rt->key.dst)[1] &&
			((u16*)&iph->saddr)[0] == ((u16*)&rt->key.src)[0] &&
			((u16*)&iph->saddr)[1] == ((u16*)&rt->key.src)[1] &&
			rt->u.dst.obsolete == 0) {
			struct device *odev = rt->u.dst.dev;

			dev_fastroute_stat.hits++;

			if (*(u8*)iph != 0x45 ||
				(eth->h_dest[0]&1) ||
				!neigh_is_valid(rt->u.dst.neighbour) ||
				iph->ttl <= 1)
					goto alas2;

			ip_decrease_ttl(iph);

			if (1) {
				struct sk_buff *skb2 = DEV_ALLOC_SKB(PKT_BUF_SZ);
				if (skb2 == NULL)
						goto oom;
				lp->rx_ring[entry].buffer1 = virt_to_bus(skb2->tail);
				skb2->dev = dev;
				lp->rx_skbuff[entry] = skb2;
			}

			skb_put(skb, len);

			ip_statistics.IpInReceives++;
			ip_statistics.IpForwDatagrams++;

			/* Could use hh cache */
			memcpy(eth->h_source, odev->dev_addr, 6);
			memcpy(eth->h_dest, rt->u.dst.neighbour->ha, 6);
			skb->dev = odev;

#ifdef FAST_SKB_RECYCLE /* DO NOT DEFINE IT! READ COMMENT */
			/* We could use fast buffer recycling here if odev
			   is not DMAing.

			   The only problem is that we must allocate skb2
			   BEFORE we lose skb, otherwise we would make hole in
			   tulip rx array. Hence, to implement FAST_SKB_RECYCLE
			   we need always keep at least one skb in a safe place.
			 */
			atomic_inc(&skb->users);
#endif

			if (odev->tx_semaphore &&
				odev->tbusy == 0 &&
				odev->interrupt == 0 &&
				odev->hard_start_xmit(skb, odev) == 0) {
#ifdef FAST_SKB_RECYCLE
				if (atomic_read(&skb->users) == 1) {
					skb->tail = skb->data;
					skb->len = 0;
				}
#endif
				dev_fastroute_stat.succeed++;
				return 0;
			}
#ifdef FAST_SKB_RECYCLE
			atomic_dec(&skb->users);
#endif

			/* Otherwise... */
			skb->pkt_type = PACKET_FASTROUTE;
			skb->nh.raw = skb->data + ETH_HLEN;
			skb->protocol = __constant_htons(ETH_P_IP);
			dev_fastroute_stat.deferred++;
			return 1;
		}
	}
	return 4;

oom:
	return 3;

alas2:
#ifdef not_yet
	skb->dst = dst_clone(&rt->u.dst);
	return 2;
#else
	return 4;
#endif
}
#endif

static int
tulip_rx(struct device *dev)
{
	struct tulip_private *lp = (struct tulip_private *)dev->priv;
	int entry = lp->cur_rx % RX_RING_SIZE;

	if (tulip_debug > 4)
		printk(KERN_DEBUG " In tulip_rx(), entry %d %8.8x.\n", entry,
			   lp->rx_ring[entry].status);
	/* If we own the next entry, it's a new packet. Send it up. */
	while (lp->rx_ring[entry].status >= 0) {
		int status = lp->rx_ring[entry].status;

		if ((status & 0x0300) != 0x0300) {
			if ((status & 0xffff) != 0x7fff) { /* Ingore earlier buffers. */
			  printk(KERN_WARNING "%s: Oversized Ethernet frame spanned "
					 "multiple buffers, status %8.8x!\n", dev->name, status);
			  lp->stats.rx_length_errors++;
			}
		} else if (status & 0x8000) {
			/* There was a fatal error. */
			lp->stats.rx_errors++; /* end of a packet.*/
			if (status & 0x0890) lp->stats.rx_length_errors++;
			if (status & 0x0004) lp->stats.rx_frame_errors++;
			if (status & 0x0002) lp->stats.rx_crc_errors++;
			if (status & 0x0001) lp->stats.rx_fifo_errors++;
		} else {
			/* Malloc up new buffer, compatible with net-2e. */
			/* Omit the four octet CRC from the length. */
			short pkt_len = (lp->rx_ring[entry].status >> 16) - 4;
			struct sk_buff *skb;
			int rx_in_place = 0;

#ifdef CONFIG_NET_HW_FLOWCONTROL
			if (netdev_dropping)
					goto throttle;
#endif

			skb = lp->rx_skbuff[entry];

#ifdef CONFIG_NET_FASTROUTE
			switch (tulip_fast_forward(dev, entry, pkt_len)) {
			case 0:	
					goto gone;
			case 1:
					goto semi_gone;
			case 2:
					break;
			case 3:
					skb = NULL;
					goto memory_squeeze;
			}
#endif
			/* Check if the packet is long enough to just accept without
			   copying to a properly sized skbuff. */
			if (pkt_len > rx_copybreak) {
				struct sk_buff *newskb;
				char *temp;

				/* Get a fresh skbuff to replace the filled one. */
				newskb = DEV_ALLOC_SKB(PKT_BUF_SZ);
				if (newskb == NULL) {
					skb = NULL;		/* No memory, drop the packet. */
					goto memory_squeeze;
				}
				/* Pass up the skb already on the Rx ring. */
				temp = skb_put(skb, pkt_len);
				if (bus_to_virt(lp->rx_ring[entry].buffer1) != temp)
					printk(KERN_ERR "%s: Internal consistency error -- the "
						   "skbuff addresses do not match"
						   " in tulip_rx: %p vs. %p / %p.\n", dev->name,
						   bus_to_virt(lp->rx_ring[entry].buffer1),
						   skb->head, temp);
				rx_in_place = 1;
				lp->rx_skbuff[entry] = newskb;
				newskb->dev = dev;
				/* Longword alignment required: do not skb_reserve(2)! */
				lp->rx_ring[entry].buffer1 = virt_to_bus(newskb->tail);
			} else
				skb = DEV_ALLOC_SKB(pkt_len + 2);
memory_squeeze:
			if (skb == NULL) {
				int i;
				printk(KERN_WARNING "%s: Memory squeeze, deferring packet.\n",
					   dev->name);
				/* Check that at least two ring entries are free.
				   If not, free one and mark stats->rx_dropped++. */
				for (i = 0; i < RX_RING_SIZE; i++)
					if (lp->rx_ring[(entry+i) % RX_RING_SIZE].status < 0)
						break;

				if (i > RX_RING_SIZE -2) {
					lp->stats.rx_dropped++;
					lp->rx_ring[entry].status = 0x80000000;
					lp->cur_rx++;
				}
				break;
			}
			skb->dev = dev;
			if (! rx_in_place) {
				skb_reserve(skb, 2);	/* 16 byte align the data fields */
#if LINUX_VERSION_CODE < 0x20200  || defined(__alpha__)
				memcpy(skb_put(skb, pkt_len),
					   bus_to_virt(lp->rx_ring[entry].buffer1), pkt_len);
#else
#ifdef ORIGINAL_TEXT
#warning Code untested
#else
#error Code is wrong, and it has nothing to do with 2.2 :-)
#endif
				eth_copy_and_sum(skb, bus_to_virt(lp->rx_ring[entry].buffer1),
									pkt_len, 0);
				skb_put(skb, pkt_len);
#endif
			}
#if LINUX_VERSION_CODE > 0x10300
			skb->protocol = eth_type_trans(skb, dev);
#else
			skb->len = pkt_len;
#endif
#ifdef CONFIG_NET_FASTROUTE
semi_gone:
#endif
			netif_rx(skb);
#ifdef CONFIG_NET_HW_FLOWCONTROL
			if (netdev_dropping) {
throttle:
				if (lp->fc_bit) {
					outl(lp->csr6 | 0x2000, dev->base_addr + CSR6);
					set_bit(lp->fc_bit, &netdev_fc_xoff);
				}
			}
#endif
#ifdef CONFIG_NET_FASTROUTE
gone:
#endif
			lp->stats.rx_packets++;
#ifndef ORIGINAL_TEXT
			lp->stats.rx_bytes += pkt_len;
#endif
		}

		lp->rx_ring[entry].status = 0x80000000;
		entry = (++lp->cur_rx) % RX_RING_SIZE;
	}

	return 0;
}

static int
tulip_close(struct device *dev)
{
	int ioaddr = dev->base_addr;
	struct tulip_private *tp = (struct tulip_private *)dev->priv;
	int i;

#ifdef CONFIG_NET_FASTROUTE
	dev->tx_semaphore = 0;
#endif
	dev->start = 0;
	dev->tbusy = 1;

#ifdef CONFIG_NET_HW_FLOWCONTROL
	if (tp->fc_bit) {
		int bit = tp->fc_bit;
		tp->fc_bit = 0;
		netdev_unregister_fc(bit);
	}
#endif

	if (tulip_debug > 1)
		printk(KERN_DEBUG "%s: Shutting down ethercard, status was %2.2x.\n",
			   dev->name, inl(ioaddr + CSR5));

	/* Disable interrupts by clearing the interrupt mask. */
	outl(0x00000000, ioaddr + CSR7);
	/* Stop the chip's Tx and Rx processes. */
	outl(inl(ioaddr + CSR6) & ~0x2002, ioaddr + CSR6);
	/* 21040 -- Leave the card in 10baseT state. */
	if (tp->chip_id == DC21040)
		outl(0x00000004, ioaddr + CSR13);

	tp->stats.rx_missed_errors += inl(ioaddr + CSR8) & 0xffff;

	del_timer(&tp->timer);

#ifdef SA_SHIRQ
	free_irq(dev->irq, dev);
#else
	free_irq(dev->irq);
	irq2dev_map[dev->irq] = 0;
#endif

	/* Free all the skbuffs in the Rx queue. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		struct sk_buff *skb = tp->rx_skbuff[i];
		tp->rx_skbuff[i] = 0;
		tp->rx_ring[i].status = 0;		/* Not owned by Tulip chip. */
		tp->rx_ring[i].length = 0;
		tp->rx_ring[i].buffer1 = 0xBADF00D0; /* An invalid address. */
		if (skb) {
#if LINUX_VERSION_CODE < 0x20100
			skb->free = 1;
#endif
			dev_kfree_skb(skb);
		}
	}
	for (i = 0; i < TX_RING_SIZE; i++) {
		if (tp->tx_skbuff[i])
			dev_kfree_skb(tp->tx_skbuff[i]);
		tp->tx_skbuff[i] = 0;
	}


	MOD_DEC_USE_COUNT;

	return 0;
}

static struct enet_statistics *
tulip_get_stats(struct device *dev)
{
	struct tulip_private *tp = (struct tulip_private *)dev->priv;
	int ioaddr = dev->base_addr;

	if (dev->start)
		tp->stats.rx_missed_errors += inl(ioaddr + CSR8) & 0xffff;

	return &tp->stats;
}

/* Set or clear the multicast filter for this adaptor.
   Note that we only use exclusion around actually queueing the
   new frame, not around filling tp->setup_frame.  This is non-deterministic
   when re-entered but still correct. */

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

#ifdef NEW_MULTICAST
static void set_multicast_list(struct device *dev)
#else
static void set_multicast_list(struct device *dev, int num_addrs, void *addrs)
#endif
{
	int ioaddr = dev->base_addr;
	int csr6 = inl(ioaddr + CSR6) & ~0x00D5;
	struct tulip_private *tp = (struct tulip_private *)dev->priv;

	tp->csr6 &= ~0x00D5;
	if (dev->flags & IFF_PROMISC) {			/* Set promiscuous. */
		outl(csr6 | 0x00C0, ioaddr + CSR6);
		/* Unconditionally log net taps. */
		printk(KERN_INFO "%s: Promiscuous mode enabled.\n", dev->name);
		tp->csr6 |= 0xC0;
	} else if ((dev->mc_count > 1000)  ||  (dev->flags & IFF_ALLMULTI)) {
		/* Too many to filter perfectly -- accept all multicasts. */
		outl(csr6 | 0x0080, ioaddr + CSR6);
		tp->csr6 |= 0x80;
	} else {
		u32 *setup_frm = tp->setup_frame;
		struct dev_mc_list *mclist;
		u16 *eaddrs;
		u32 tx_flags;
		int i;

		if (dev->mc_count > 14) { /* Must use a multicast hash table. */
		  u16 hash_table[32];
		  memset(hash_table, 0, sizeof(hash_table));
		  /* This should work on big-endian machines as well. */
		  for (i = 0, mclist = dev->mc_list; mclist && i < dev->mc_count;
			   i++, mclist = mclist->next)
			  set_bit(ether_crc_le(ETH_ALEN, mclist->dmi_addr) & 0x1ff,
					  hash_table);
		  /* Copy the hash table to the setup frame.
			 NOTE that only the LOW SHORTWORD of setup_frame[] is valid! */
		  for (i = 0; i < 32; i++)
			*setup_frm++ = hash_table[i];
		  setup_frm += 7;
		  tx_flags = 0x08400000 | 192;
		  /* Too clever: i > 15 for fall-though. */
		} else {
		  /* We have <= 15 addresses so we can use the wonderful
			 16 address perfect filtering of the Tulip. */
		  for (i = 0, mclist = dev->mc_list; i < dev->mc_count;
			   i++, mclist = mclist->next) {
			/* Note that only the low shortword of setup_frame[] is valid!
			   This code may require tweaking for non-x86 architectures! */
			eaddrs = (u16 *)mclist->dmi_addr;
			*setup_frm++ = *eaddrs++;
			*setup_frm++ = *eaddrs++;
			*setup_frm++ = *eaddrs++;
		  }
		  /* Fill the rest of the table with our physical address.
			 Once again, only the low shortword or setup_frame[] is valid! */
		  *setup_frm++ = 0xffff;
		  *setup_frm++ = 0xffff;
		  *setup_frm++ = 0xffff;
		  tx_flags = 0x08000000 | 192;
		}
		eaddrs = (u16 *)dev->dev_addr;
		do {
		  *setup_frm++ = eaddrs[0];
		  *setup_frm++ = eaddrs[1];
		  *setup_frm++ = eaddrs[2];
		} while (++i < 15);
		/* Now add this frame to the Tx list. */
		if (tp->cur_tx - tp->dirty_tx > TX_RING_SIZE - 2) {
			/* Same setup recently queued, we need not add it. */
		} else {
			unsigned long flags;
			unsigned int entry;
			
			save_flags(flags); cli();
			entry = tp->cur_tx++ % TX_RING_SIZE;

			if (entry != 0) {
			  /* Avoid a chip errata by prefixing a dummy entry. */
			  tp->tx_skbuff[entry] = 0;
			  tp->tx_ring[entry].length =
				(entry == TX_RING_SIZE-1) ? 0x02000000 : 0;
			  tp->tx_ring[entry].buffer1 = 0;
			  tp->tx_ring[entry].status = 0x80000000;
			  entry = tp->cur_tx++ % TX_RING_SIZE;
			}

			tp->tx_skbuff[entry] = 0;
			/* Put the setup frame on the Tx list. */
			if (entry == TX_RING_SIZE-1)
			  tx_flags |= 0x02000000;		/* Wrap ring. */
			tp->tx_ring[entry].length = tx_flags;
			tp->tx_ring[entry].buffer1 = virt_to_bus(tp->setup_frame);
			tp->tx_ring[entry].status = 0x80000000;
			if (tp->cur_tx - tp->dirty_tx >= TX_RING_SIZE - 2) {
				dev->tbusy = 1;
				tp->tx_full = 1;
			}
			restore_flags(flags);
			/* Trigger an immediate transmit demand. */
			outl(0, ioaddr + CSR1);
		}
		outl(csr6 | 0x0000, ioaddr + CSR6);
	}
}

#ifdef MODULE
#if LINUX_VERSION_CODE > 0x20118
MODULE_AUTHOR("Donald Becker <becker@cesdis.gsfc.nasa.gov>");
MODULE_DESCRIPTION("Digital 21*4* Tulip ethernet driver");
MODULE_PARM(debug, "i");
MODULE_PARM(reverse_probe, "i");
MODULE_PARM(rx_copybreak, "i");
MODULE_PARM(options, "1-" __MODULE_STRING(8) "i");
MODULE_PARM(full_duplex, "1-" __MODULE_STRING(8) "i");
#endif

/* An additional parameter that may be passed in... */
static int debug = -1;

int
init_module(void)
{
	if (debug >= 0)
		tulip_debug = debug;

	root_tulip_dev = NULL;
	return tulip_probe(NULL);
}

void
cleanup_module(void)
{
	struct device *next_dev;

	/* No need to check MOD_IN_USE, as sys_delete_module() checks. */
	while (root_tulip_dev) {
		next_dev = ((struct tulip_private *)root_tulip_dev->priv)->next_module;
		unregister_netdev(root_tulip_dev);
		release_region(root_tulip_dev->base_addr, TULIP_TOTAL_SIZE);
		kfree(root_tulip_dev);
		root_tulip_dev = next_dev;
	}
}

#endif  /* MODULE */

/*
 * Local variables:
 *  compile-command: "gcc -DMODVERSIONS -DMODULE -D__KERNEL__ -I/usr/src/linux/net/inet -Wall -Wstrict-prototypes -O6 -c tulip.c"
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 * End:
 */
