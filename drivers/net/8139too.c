/*

	8139too.c: A RealTek RTL-8139 Fast Ethernet driver for Linux.

	Copyright 2000 Jeff Garzik <jgarzik@mandrakesoft.com>
	Originally: Written 1997-1999 by Donald Becker.

	This software may be used and distributed according to the terms
	of the GNU Public License, incorporated herein by reference.

	Contributors:

		Donald Becker - he wrote the original driver, kudos to him!
		(but please don't e-mail him for support, this isn't his driver)

		Tigran Aivazian - bug fixes, skbuff free cleanup

		Martin Mares - suggestions for PCI cleanup
		
		David S. Miller - PCI DMA and softnet updates

-----------------------------------------------------------------------------

				Theory of Operation

I. Board Compatibility

This device driver is designed for the RealTek RTL8139 series, the RealTek
Fast Ethernet controllers for PCI and CardBus.  This chip is used on many
low-end boards, sometimes with its markings changed.


II. Board-specific settings

PCI bus devices are configured by the system at boot time, so no jumpers
need to be set on the board.  The system BIOS will assign the
PCI INTA signal to a (preferably otherwise unused) system IRQ line.

III. Driver operation

IIIa. Rx Ring buffers

The receive unit uses a single linear ring buffer rather than the more
common (and more efficient) descriptor-based architecture.  Incoming frames
are sequentially stored into the Rx region, and the host copies them into
skbuffs.

Comment: While it is theoretically possible to process many frames in place,
any delay in Rx processing would cause us to drop frames.  More importantly,
the Linux protocol stack is not designed to operate in this manner.

IIIb. Tx operation

The RTL8139 uses a fixed set of four Tx descriptors in register space.
In a stunningly bad design choice, Tx frames must be 32 bit aligned.  Linux
aligns the IP header on word boundaries, and 14 byte ethernet header means
that almost all frames will need to be copied to an alignment buffer.

IVb. References

http://www.realtek.com.tw/cn/cn.html
http://cesdis.gsfc.nasa.gov/linux/misc/NWay.html

IVc. Errata

1) The RTL-8139 has a serious problem with motherboards which do
posted MMIO writes to PCI space.  This driver works around the
problem by having an MMIO  register write be immediately followed by
an MMIO register read.

2) The RTL-8129 is only supported in Donald Becker's rtl8139 driver.

*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <asm/io.h>


#define RTL8139_VERSION "0.9.0"
#define RTL8139_MODULE_NAME "8139too"
#define RTL8139_DRIVER_NAME   RTL8139_MODULE_NAME " Fast Ethernet driver " RTL8139_VERSION
#define PFX RTL8139_MODULE_NAME ": "

#undef RTL8139_DEBUG /* define to 1 to enable copious debugging info */

#ifdef RTL8139_DEBUG
/* note: prints function name for you */
#define DPRINTK(fmt, args...) printk(KERN_DEBUG "%s: " fmt, __FUNCTION__ , ## args)
#else
#define DPRINTK(fmt, args...)
#endif

#define RTL8139_NDEBUG 0	/* define to 1 to disable lightweight runtime checks */
#if RTL8139_NDEBUG
#define assert(expr)
#else
#define assert(expr) \
        if(!(expr)) {					\
        printk( "Assertion failed! %s,%s,%s,line=%d\n",	\
        #expr,__FILE__,__FUNCTION__,__LINE__);		\
        }
#endif

#define arraysize(x)            (sizeof(x)/sizeof(*(x)))


#ifndef PCI_GET_DRIVER_DATA
  #define PCI_GET_DRIVER_DATA(pdev)		((pdev)->driver_data)
  #define PCI_SET_DRIVER_DATA(pdev,data)	(((pdev)->driver_data) = (data))
#endif /* PCI_GET_DRIVER_DATA */


/* A few user-configurable values. */
/* Maximum events (Rx packets, etc.) to handle at each interrupt. */
static int max_interrupt_work = 20;

/* Maximum number of multicast addresses to filter (vs. Rx-all-multicast).
   The RTL chips use a 64 element hash table based on the Ethernet CRC.  */
static int multicast_filter_limit = 32;

/* Size of the in-memory receive ring. */
#define RX_BUF_LEN_IDX	2	/* 0==8K, 1==16K, 2==32K, 3==64K */
#define RX_BUF_LEN (8192 << RX_BUF_LEN_IDX)
/* Size of the Tx bounce buffers -- must be at least (dev->mtu+14+4). */
#define TX_BUF_SIZE	1536

/* PCI Tuning Parameters
   Threshold is bytes transferred to chip before transmission starts. */
#define TX_FIFO_THRESH 256	/* In bytes, rounded down to 32 byte units. */

/* The following settings are log_2(bytes)-4:  0 == 16 bytes .. 6==1024. */
#define RX_FIFO_THRESH	4	/* Rx buffer level before first PCI xfer.  */
#define RX_DMA_BURST	4	/* Maximum PCI burst, '4' is 256 bytes */
#define TX_DMA_BURST	4	/* Calculate as 16<<val. */

/* Operational parameters that usually are not changed. */
/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT  (6*HZ)


enum {
	HAS_CHIP_XCVR = 0x020000,
	HAS_LNK_CHNG = 0x040000,
};

#define RTL_IO_SIZE 256

#define RTL8139_CAPS  HAS_CHIP_XCVR|HAS_LNK_CHNG

typedef enum {
	UNKNOWN = 0,
	RTL8139,
	RTL8139_CB,
	SMC1211TX,
	MPX5030,
	SIS900,
	SIS7016,
} chip_t;

static struct {
	chip_t chip;
	const char *name;
} chip_info[] __devinitdata = {
	{ RTL8139, "RealTek RTL8139 Fast Ethernet"},
	{ RTL8139_CB, "RealTek RTL8139B PCI/CardBus"},
	{ SMC1211TX, "SMC1211TX EZCard 10/100 (RealTek RTL8139)"},
	{ MPX5030, "Accton MPX5030 (RealTek RTL8139)"},
	{ SIS900, "SiS 900 (RealTek RTL8139) Fast Ethernet"},
	{ SIS7016, "SiS 7016 (RealTek RTL8139) Fast Ethernet"},
	{0,},
};

static struct pci_device_id rtl8139_pci_tbl[] __devinitdata = {
	{0x10ec, 0x8139, PCI_ANY_ID, PCI_ANY_ID, 0, 0, RTL8139 },
	{0x10ec, 0x8138, PCI_ANY_ID, PCI_ANY_ID, 0, 0, RTL8139_CB },
	{0x1113, 0x1211, PCI_ANY_ID, PCI_ANY_ID, 0, 0, SMC1211TX },
	{0x1113, 0x1211, PCI_ANY_ID, PCI_ANY_ID, 0, 0, MPX5030 },
	{0x1039, 0x0900, PCI_ANY_ID, PCI_ANY_ID, 0, 0, SIS900 },
	{0x1039, 0x7016, PCI_ANY_ID, PCI_ANY_ID, 0, 0, SIS7016 },
	{0,},
};
MODULE_DEVICE_TABLE (pci, rtl8139_pci_tbl);


/* The rest of these values should never change. */
#define NUM_TX_DESC	4	/* Number of Tx descriptor registers. */

/* Symbolic offsets to registers. */
enum RTL8139_registers {
	MAC0 = 0,		/* Ethernet hardware address. */
	MAR0 = 8,		/* Multicast filter. */
	TxStatus0 = 0x10,	/* Transmit status (Four 32bit registers). */
	TxAddr0 = 0x20,		/* Tx descriptors (also four 32bit). */
	RxBuf = 0x30,
	RxEarlyCnt = 0x34,
	RxEarlyStatus = 0x36,
	ChipCmd = 0x37,
	RxBufPtr = 0x38,
	RxBufAddr = 0x3A,
	IntrMask = 0x3C,
	IntrStatus = 0x3E,
	TxConfig = 0x40,
	RxConfig = 0x44,
	Timer = 0x48,		/* A general-purpose counter. */
	RxMissed = 0x4C,	/* 24 bits valid, write clears. */
	Cfg9346 = 0x50,
	Config0 = 0x51,
	Config1 = 0x52,
	FlashReg = 0x54,
	GPPinData = 0x58,
	GPPinDir = 0x59,
	Config4 = 0x5A,		/* absent on RTL-8139A */
	HltClk = 0x5B,
	MultiIntr = 0x5C,
	TxSummary = 0x60,
	BasicModeCtrl = 0x62,
	BasicModeStatus = 0x64,
	NWayAdvert = 0x66,
	NWayLPAR = 0x68,
	NWayExpansion = 0x6A,
	/* Undocumented registers, but required for proper operation. */
	FIFOTMS = 0x70,		/* FIFO Control and test. */
	CSCR = 0x74,		/* Chip Status and Configuration Register. */
	PARA78 = 0x78,
	PARA7c = 0x7c,		/* Magic transceiver parameter register. */
};

enum ChipCmdBits {
	CmdReset = 0x10,
	CmdRxEnb = 0x08,
	CmdTxEnb = 0x04,
	RxBufEmpty = 0x01,
};

/* Interrupt register bits, using my own meaningful names. */
enum IntrStatusBits {
	PCIErr = 0x8000,
	PCSTimeout = 0x4000,
	RxFIFOOver = 0x40,
	RxUnderrun = 0x20,
	RxOverflow = 0x10,
	TxErr = 0x08,
	TxOK = 0x04,
	RxErr = 0x02,
	RxOK = 0x01,
};
enum TxStatusBits {
	TxHostOwns = 0x2000,
	TxUnderrun = 0x4000,
	TxStatOK = 0x8000,
	TxOutOfWindow = 0x20000000,
	TxAborted = 0x40000000,
	TxCarrierLost = 0x80000000,
};
enum RxStatusBits {
	RxMulticast = 0x8000,
	RxPhysical = 0x4000,
	RxBroadcast = 0x2000,
	RxBadSymbol = 0x0020,
	RxRunt = 0x0010,
	RxTooLong = 0x0008,
	RxCRCErr = 0x0004,
	RxBadAlign = 0x0002,
	RxStatusOK = 0x0001,
};

/* Bits in RxConfig. */
enum rx_mode_bits {
	AcceptErr = 0x20,
	AcceptRunt = 0x10,
	AcceptBroadcast = 0x08,
	AcceptMulticast = 0x04,
	AcceptMyPhys = 0x02,
	AcceptAllPhys = 0x01,
};

/* Bits in Config1 */
enum Config1Bits {
	Cfg1_PM_Enable = 0x01,
	Cfg1_VPD_Enable = 0x02,
	Cfg1_PIO = 0x04,
	Cfg1_MMIO = 0x08,
	Cfg1_LWAKE = 0x10,
	Cfg1_Driver_Load = 0x20,
	Cfg1_LED0 = 0x40,
	Cfg1_LED1 = 0x80,
};

/* Twister tuning parameters from RealTek.
   Completely undocumented, but required to tune bad links. */
enum CSCRBits {
	CSCR_LinkOKBit = 0x0400,
	CSCR_LinkChangeBit = 0x0800,
	CSCR_LinkStatusBits = 0x0f000,
	CSCR_LinkDownOffCmd = 0x003c0,
	CSCR_LinkDownCmd = 0x0f3c0,
};
#define PARA78_default	0x78fa8388
#define PARA7c_default	0xcb38de43	/* param[0][3] */
#define PARA7c_xxx		0xcb38de43
unsigned long param[4][4] = {
	{0xcb39de43, 0xcb39ce43, 0xfb38de03, 0xcb38de43},
	{0xcb39de43, 0xcb39ce43, 0xcb39ce83, 0xcb39ce83},
	{0xcb39de43, 0xcb39ce43, 0xcb39ce83, 0xcb39ce83},
	{0xbb39de43, 0xbb39ce43, 0xbb39ce83, 0xbb39ce83}
};

struct ring_info {
	struct sk_buff *skb;
	dma_addr_t mapping;
};


#define PRIV_ALIGN	15	/* Required alignment mask */
struct rtl8139_private {
	chip_t chip;
	void *mmio_addr;
	spinlock_t lock;
	int drv_flags;
	struct pci_dev *pci_dev;
	struct net_device_stats stats;
	struct timer_list timer;	/* Media selection timer. */
	unsigned char *rx_ring;
	unsigned int cur_rx;	/* Index into the Rx buffer of next Rx pkt. */
	unsigned int rx_config;
	unsigned int cur_tx, dirty_tx, tx_flag;
	/* The saved address of a sent-in-place packet/buffer, for skfree(). */
	struct ring_info tx_info[NUM_TX_DESC];
	unsigned char *tx_buf[NUM_TX_DESC];	/* Tx bounce buffers */
	unsigned char *tx_bufs;	/* Tx bounce buffer region. */
	dma_addr_t rx_ring_dma;
	dma_addr_t tx_bufs_dma;
	char phys[4];		/* MII device addresses. */
	char twistie, twist_row, twist_col;	/* Twister tune state. */
	unsigned int full_duplex:1;	/* Full-duplex operation requested. */
	unsigned int duplex_lock:1;
	unsigned int default_port:4;	/* Last dev->if_port value. */
	unsigned int media2:4;	/* Secondary monitored media port. */
	unsigned int medialock:1;	/* Don't sense media type. */
	unsigned int mediasense:1;	/* Media sensing in progress. */
};

MODULE_AUTHOR ("Jeff Garzik <jgarzik@mandrakesoft.com>");
MODULE_DESCRIPTION ("RealTek RTL-8139 Fast Ethernet driver");
MODULE_PARM (multicast_filter_limit, "i");
MODULE_PARM (max_interrupt_work, "i");
MODULE_PARM (debug, "i");

static int read_eeprom (void *ioaddr, int location, int addr_len);
static int rtl8139_open (struct net_device *dev);
static int mdio_read (struct net_device *dev, int phy_id, int location);
static void mdio_write (struct net_device *dev, int phy_id, int location,
			int val);
static void rtl8139_timer (unsigned long data);
static void rtl8139_tx_timeout (struct net_device *dev);
static void rtl8139_init_ring (struct net_device *dev);
static int rtl8139_start_xmit (struct sk_buff *skb,
			       struct net_device *dev);
static void rtl8139_interrupt (int irq, void *dev_instance,
			       struct pt_regs *regs);
static int rtl8139_close (struct net_device *dev);
static int mii_ioctl (struct net_device *dev, struct ifreq *rq, int cmd);
static struct net_device_stats *rtl8139_get_stats (struct net_device *dev);
static inline u32 ether_crc (int length, unsigned char *data);
static void rtl8139_set_rx_mode (struct net_device *dev);

/* read after write, to avoid rtl8139 bug w/ posted MMIO writes */
#define RTL_W8(reg, val8)	do { writeb (val8, ioaddr + reg); readb (ioaddr + reg); } while (0)
#define RTL_W16(reg, val16)	do { writew (val16, ioaddr + reg); readw (ioaddr + reg); } while (0)
#define RTL_W32(reg, val32)	do { writel (val32, ioaddr + reg); readl (ioaddr + reg); } while (0)
#define RTL_R8(reg)		readb (ioaddr + reg)
#define RTL_R16(reg)		readw (ioaddr + reg)
#define RTL_R32(reg)		readl (ioaddr + reg)


static const char * __devinit rtl8139_name_from_chip (chip_t chip)
{
	int i;
	
	for (i = 0; i < arraysize (chip_info); i++)
		if (chip == chip_info[i].chip)
			return chip_info[i].name;
	
	return "unknown";
}


static int __devinit rtl8139_init_pci (struct pci_dev *pdev, void **ioaddr_out)
{
	void *ioaddr = NULL;
	u8 tmp8;
	int rc;
	u32 pio_start, pio_end, pio_flags;
	u32 mmio_start, mmio_end, mmio_flags;

	DPRINTK ("ENTER\n");

	assert (pdev != NULL);
	assert (ioaddr_out != NULL);

	*ioaddr_out = NULL;

	pio_start = pci_resource_start (pdev, 0);
	pio_end = pci_resource_end (pdev, 0);
	pio_flags = pci_resource_flags (pdev, 0);

	mmio_start = pci_resource_start (pdev, 1);
	mmio_end = pci_resource_end (pdev, 1);
	mmio_flags = pci_resource_flags (pdev, 1);

	/* make sure PCI base addr 0 is PIO */
	if (pio_start == 0 || pio_end <= pio_start ||
	    (!(pio_flags & IORESOURCE_IO))) {
		printk (KERN_ERR PFX "no PIO resource, aborting\n");
		return -ENODEV;
	}
	
	/* make sure PCI base addr 1 is MMIO */
	if (mmio_start == 0 || mmio_end <= mmio_start ||
	    (!(mmio_flags & IORESOURCE_MEM))) {
		printk (KERN_ERR PFX "no MMIO resource, aborting\n");
		return -ENODEV;
	}

	/* make sure our PIO region in PCI space is available */
	if (!request_region (pio_start, RTL_IO_SIZE, RTL8139_MODULE_NAME)) {
		printk (KERN_ERR PFX "no I/O resource available, aborting\n");
		return -EBUSY;
	}
	
	/* make sure our MMIO region in PCI space is available */
	if (!request_mem_region (mmio_start, RTL_IO_SIZE, RTL8139_MODULE_NAME)) {
		release_region (pio_start, RTL_IO_SIZE);
		printk (KERN_ERR PFX "no mem resource available, aborting\n");
		return -EBUSY;
	}
	
	/* enable device (incl. PCI PM wakeup), and bus-mastering */
	pci_enable_device (pdev);
	pci_set_master (pdev);

	/* ioremap MMIO region */
	ioaddr = ioremap (mmio_start, RTL_IO_SIZE);
	if (ioaddr == NULL) {
		printk (KERN_ERR PFX "cannot remap MMIO, aborting\n");
		rc = -EIO;
		goto err_out;
	}

	/* Bring the chip out of low-power mode. */
	RTL_W8 (Config1, 0x00);

	/* make sure chip thinks PIO and MMIO are enabled */
	tmp8 = RTL_R8 (Config1);
	if ((tmp8 & Cfg1_PIO) == 0) {
		printk (KERN_ERR PFX "PIO not enabled, Cfg1=%02X, aborting\n", tmp8);
		rc = -EIO;
		goto err_out;
	}
	if ((tmp8 & Cfg1_MMIO) == 0) {
		printk (KERN_ERR PFX "MMIO not enabled, Cfg1=%02X, aborting\n", tmp8);
		rc = -EIO;
		goto err_out;
	}
	
	/* sanity checks -- ensure PIO and MMIO registers agree */
	assert (inb (pio_start+Config0) == RTL_R8 (Config0));
	assert (inb (pio_start+Config1) == RTL_R8 (Config1));
	assert (inb (pio_start+TxConfig) == RTL_R8 (TxConfig));
	assert (inb (pio_start+RxConfig) == RTL_R8 (RxConfig));

	DPRINTK ("EXIT, returning 0\n");
	*ioaddr_out = ioaddr;
	return 0;	

err_out:
	if (ioaddr)
		iounmap (ioaddr);
	release_region (pio_start, RTL_IO_SIZE);
	release_mem_region (mmio_start, RTL_IO_SIZE);
	DPRINTK ("EXIT, returning %d\n", rc);
	return rc;
}


static int __devinit rtl8139_init_one (struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct net_device *dev;
	struct rtl8139_private *tp;
	int i, addr_len, option = -1;
	void *ioaddr = NULL;

	DPRINTK ("ENTER\n");
	
	assert (pdev != NULL);
	assert (ent != NULL);
	
	i = rtl8139_init_pci (pdev, &ioaddr);
	if (i < 0) {
		DPRINTK ("EXIT, returning %d\n", i);
		return i;
	}
	
	assert (ioaddr != NULL);

	/* dev zeroed in init_etherdev */
	dev = init_etherdev (NULL, sizeof (*tp) + PRIV_ALIGN);
	if (dev == NULL) {
		iounmap (ioaddr);
		printk (KERN_ERR PFX "unable to alloc new ethernet\n");
		DPRINTK ("EXIT, returning -ENOMEM\n");
		return -ENOMEM;
	}

	addr_len = read_eeprom (ioaddr, 0, 8) == 0x8129 ? 8 : 6;
	for (i = 0; i < 3; i++)
		((u16 *) (dev->dev_addr))[i] =
		    le16_to_cpu (read_eeprom (ioaddr, i + 7, addr_len));

	/* The Rtl8139-specific entries in the device structure. */
	dev->open = rtl8139_open;
	dev->hard_start_xmit = rtl8139_start_xmit;
	dev->stop = rtl8139_close;
	dev->get_stats = rtl8139_get_stats;
	dev->set_multicast_list = rtl8139_set_rx_mode;
	dev->do_ioctl = mii_ioctl;
	dev->tx_timeout = rtl8139_tx_timeout;
	dev->watchdog_timeo = TX_TIMEOUT;

	dev->irq = pdev->irq;
	dev->base_addr = pci_resource_start (pdev, 1);

	dev->priv = tp = (void *)
		(((long)dev->priv + PRIV_ALIGN) & ~PRIV_ALIGN);

	printk (KERN_INFO "%s: %s at 0x%lx, IRQ %d, "
		"%2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x.\n",
		dev->name, rtl8139_name_from_chip(ent->driver_data),
		dev->base_addr, dev->irq,
		dev->dev_addr[0], dev->dev_addr[1],
		dev->dev_addr[2], dev->dev_addr[3],
		dev->dev_addr[4], dev->dev_addr[5]);

	/* tp zeroed in init_etherdev */
	tp->drv_flags = PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
			PCI_COMMAND_MASTER | RTL8139_CAPS;
	tp->pci_dev = pdev;
	tp->chip = ent->driver_data;
	tp->mmio_addr = ioaddr;
	tp->lock = SPIN_LOCK_UNLOCKED;

	PCI_SET_DRIVER_DATA (pdev, dev);

	tp->phys[0] = 32;

	/* Put the chip into low-power mode. */
	RTL_W8 (Cfg9346, 0xC0);
	RTL_W8 (Config1, 0x03);	/* Enable PM & PCI VPD */
	RTL_W8 (HltClk, 'H');	/* 'R' would leave the clock running. */

	/* The lower four bits are the media type. */
	if (option > 0) {
		tp->full_duplex = (option & 0x200) ? 1 : 0;
		tp->default_port = option & 15;
		if (tp->default_port)
			tp->medialock = 1;
	}

	if (tp->full_duplex) {
		printk (KERN_INFO
			"%s: Media type forced to Full Duplex.\n",
			dev->name);
		mdio_write (dev, tp->phys[0], 4, 0x141);
		tp->duplex_lock = 1;
	}

	DPRINTK ("EXIT - returning 0\n");
	return 0;
}


static void __devexit rtl8139_remove_one (struct pci_dev *pdev)
{
	struct net_device *dev = PCI_GET_DRIVER_DATA (pdev);
	struct rtl8139_private *np;

	DPRINTK ("ENTER\n");

	assert (dev != NULL);

	np = (struct rtl8139_private *) (dev->priv);
	assert (np != NULL);

	unregister_netdev (dev);

	iounmap (np->mmio_addr);
	release_region (pci_resource_start (pdev, 0), RTL_IO_SIZE);
	release_mem_region (pci_resource_start (pdev, 1), RTL_IO_SIZE);

#ifndef RTL8139_NDEBUG
	/* poison memory before freeing */
	memset (dev, 0xC0,
		sizeof (struct net_device) +
		sizeof (struct rtl8139_private) +
		PRIV_ALIGN);
#endif

	kfree (dev);
	
	DPRINTK ("EXIT\n");
}


/* Serial EEPROM section. */

/*  EEPROM_Ctrl bits. */
#define EE_SHIFT_CLK	0x04	/* EEPROM shift clock. */
#define EE_CS			0x08	/* EEPROM chip select. */
#define EE_DATA_WRITE	0x02	/* EEPROM chip data in. */
#define EE_WRITE_0		0x00
#define EE_WRITE_1		0x02
#define EE_DATA_READ	0x01	/* EEPROM chip data out. */
#define EE_ENB			(0x80 | EE_CS)

/* Delay between EEPROM clock transitions.
   No extra delay is needed with 33Mhz PCI, but 66Mhz may change this.
 */

#define eeprom_delay()	readl(ee_addr)

/* The EEPROM commands include the alway-set leading bit. */
#define EE_WRITE_CMD	(5)
#define EE_READ_CMD		(6)
#define EE_ERASE_CMD	(7)

static int __devinit read_eeprom (void *ioaddr, int location, int addr_len)
{
	int i;
	unsigned retval = 0;
	void *ee_addr = ioaddr + Cfg9346;
	int read_cmd = location | (EE_READ_CMD << addr_len);

	DPRINTK ("ENTER\n");

	writeb (EE_ENB & ~EE_CS, ee_addr);
	writeb (EE_ENB, ee_addr);
	eeprom_delay ();

	/* Shift the read command bits out. */
	for (i = 4 + addr_len; i >= 0; i--) {
		int dataval = (read_cmd & (1 << i)) ? EE_DATA_WRITE : 0;
		writeb (EE_ENB | dataval, ee_addr);
		eeprom_delay ();
		writeb (EE_ENB | dataval | EE_SHIFT_CLK, ee_addr);
		eeprom_delay ();
	}
	writeb (EE_ENB, ee_addr);
	eeprom_delay ();

	for (i = 16; i > 0; i--) {
		writeb (EE_ENB | EE_SHIFT_CLK, ee_addr);
		eeprom_delay ();
		retval =
		    (retval << 1) | ((readb (ee_addr) & EE_DATA_READ) ? 1 :
				     0);
		writeb (EE_ENB, ee_addr);
		eeprom_delay ();
	}

	/* Terminate the EEPROM access. */
	writeb (~EE_CS, ee_addr);
	eeprom_delay ();

	DPRINTK ("EXIT - returning %d\n", retval);
	return retval;
}

/* MII serial management: mostly bogus for now. */
/* Read and write the MII management registers using software-generated
   serial MDIO protocol.
   The maximum data clock rate is 2.5 Mhz.  The minimum timing is usually
   met by back-to-back PCI I/O cycles, but we insert a delay to avoid
   "overclocking" issues. */
#define MDIO_DIR		0x80
#define MDIO_DATA_OUT	0x04
#define MDIO_DATA_IN	0x02
#define MDIO_CLK		0x01
#define MDIO_WRITE0 (MDIO_DIR)
#define MDIO_WRITE1 (MDIO_DIR | MDIO_DATA_OUT)

#define mdio_delay()	readb(mdio_addr)


static char mii_2_8139_map[8] = {
	BasicModeCtrl,
	BasicModeStatus,
	0,
	0,
	NWayAdvert,
	NWayLPAR,
	NWayExpansion,
	0
};


/* Syncronize the MII management interface by shifting 32 one bits out. */
static void mdio_sync (void *mdio_addr)
{
	int i;

	DPRINTK ("ENTER\n");

	for (i = 32; i >= 0; i--) {
		writeb (MDIO_WRITE1, mdio_addr);
		mdio_delay ();
		writeb (MDIO_WRITE1 | MDIO_CLK, mdio_addr);
		mdio_delay ();
	}

	DPRINTK ("EXIT\n");
}


static int mdio_read (struct net_device *dev, int phy_id, int location)
{
	struct rtl8139_private *tp = (struct rtl8139_private *) dev->priv;
	void *mdio_addr = tp->mmio_addr + Config4;
	int mii_cmd = (0xf6 << 10) | (phy_id << 5) | location;
	int retval = 0;
	int i;

	DPRINTK ("ENTER\n");

	if (phy_id > 31) {	/* Really a 8139.  Use internal registers. */
		DPRINTK ("EXIT after directly using 8139 internal regs\n");
		return location < 8 && mii_2_8139_map[location] ?
		    readw (tp->mmio_addr + mii_2_8139_map[location]) : 0;
	}
	mdio_sync (mdio_addr);
	/* Shift the read command bits out. */
	for (i = 15; i >= 0; i--) {
		int dataval = (mii_cmd & (1 << i)) ? MDIO_DATA_OUT : 0;

		writeb (MDIO_DIR | dataval, mdio_addr);
		mdio_delay ();
		writeb (MDIO_DIR | dataval | MDIO_CLK, mdio_addr);
		mdio_delay ();
	}

	/* Read the two transition, 16 data, and wire-idle bits. */
	for (i = 19; i > 0; i--) {
		writeb (0, mdio_addr);
		mdio_delay ();
		retval =
		    (retval << 1) | ((readb (mdio_addr) & MDIO_DATA_IN) ? 1
				     : 0);
		writeb (MDIO_CLK, mdio_addr);
		mdio_delay ();
	}

	DPRINTK ("EXIT, returning %d\n", (retval >> 1) & 0xffff);
	return (retval >> 1) & 0xffff;
}


static void mdio_write (struct net_device *dev, int phy_id, int location,
			int value)
{
	struct rtl8139_private *tp = (struct rtl8139_private *) dev->priv;
	void *mdio_addr = tp->mmio_addr + Config4;
	int mii_cmd =
	    (0x5002 << 16) | (phy_id << 23) | (location << 18) | value;
	int i;

	DPRINTK ("ENTER\n");

	if (phy_id > 31) {	/* Really a 8139.  Use internal registers. */
		if (location < 8 && mii_2_8139_map[location]) {
			writew (value,
				tp->mmio_addr + mii_2_8139_map[location]);
			readw (tp->mmio_addr + mii_2_8139_map[location]);
		}
		DPRINTK ("EXIT after directly using 8139 internal regs\n");
		return;
	}
	mdio_sync (mdio_addr);

	/* Shift the command bits out. */
	for (i = 31; i >= 0; i--) {
		int dataval =
		    (mii_cmd & (1 << i)) ? MDIO_WRITE1 : MDIO_WRITE0;
		writeb (dataval, mdio_addr);
		mdio_delay ();
		writeb (dataval | MDIO_CLK, mdio_addr);
		mdio_delay ();
	}

	/* Clear out extra bits. */
	for (i = 2; i > 0; i--) {
		writeb (0, mdio_addr);
		mdio_delay ();
		writeb (MDIO_CLK, mdio_addr);
		mdio_delay ();
	}

	DPRINTK ("EXIT\n");
}


static int rtl8139_open (struct net_device *dev)
{
	struct rtl8139_private *tp = (struct rtl8139_private *) dev->priv;
	void *ioaddr = tp->mmio_addr;
	int i;

	DPRINTK ("ENTER\n");

	MOD_INC_USE_COUNT;

	/* Soft reset the chip. */
	RTL_W8 (ChipCmd, CmdReset);

	if (request_irq (dev->irq, &rtl8139_interrupt, SA_SHIRQ, dev->name, dev)) {
		DPRINTK ("EXIT, returning -EBUSY\n");
		MOD_DEC_USE_COUNT;
		return -EBUSY;
	}

	tp->tx_bufs = pci_alloc_consistent(tp->pci_dev, TX_BUF_SIZE * NUM_TX_DESC,
					   &tp->tx_bufs_dma);
	tp->rx_ring = pci_alloc_consistent(tp->pci_dev, RX_BUF_LEN + 16,
					   &tp->rx_ring_dma);
	if (tp->tx_bufs == NULL || tp->rx_ring == NULL) {
		free_irq(dev->irq, dev);

		if (tp->tx_bufs)
			pci_free_consistent(tp->pci_dev, TX_BUF_SIZE * NUM_TX_DESC,
					    tp->tx_bufs, tp->tx_bufs_dma);
		if (tp->rx_ring)
			pci_free_consistent(tp->pci_dev, RX_BUF_LEN + 16,
					    tp->rx_ring, tp->rx_ring_dma);

		DPRINTK ("EXIT, returning -ENOMEM\n");
		MOD_DEC_USE_COUNT;
		return -ENOMEM;
		
	}
	
	rtl8139_init_ring (dev);
	tp->full_duplex = tp->duplex_lock;
	tp->tx_flag = (TX_FIFO_THRESH << 11) & 0x003f0000;
	tp->rx_config =
	    (RX_FIFO_THRESH << 13) |
	    (RX_BUF_LEN_IDX << 11) |
	    (RX_DMA_BURST << 8);

	/* Check that the chip has finished the reset. */
	for (i = 1000; i > 0; i--)
		if ((RTL_R8 (ChipCmd) & CmdReset) == 0)
			break;

	RTL_W32 (MAC0 + 0, cpu_to_le32 (*(u32 *) (dev->dev_addr + 0)));
	RTL_W32 (MAC0 + 4, cpu_to_le32 (*(u32 *) (dev->dev_addr + 4)));

	/* Must enable Tx/Rx before setting transfer thresholds! */
	RTL_W8 (ChipCmd, CmdRxEnb | CmdTxEnb);
	RTL_W32 (RxConfig, tp->rx_config);
	RTL_W32 (TxConfig, (TX_DMA_BURST << 8) | 0x00000000);

	/* Reset N-Way to chipset defaults */
	RTL_W16 (BasicModeCtrl, (1<<15)|(1<<12)|(1<<9));
	for (i = 1000; i > 0; i--)
		if ((RTL_R8 (BasicModeCtrl) & (1<<15)) == 0)
			break;
	
	/* Set N-Way to sane defaults */
	RTL_W16 (FIFOTMS, 0x0000);
	RTL_W16 (NWayAdvert, (1<<13)|(1<<8)|(1<<7)|(1<<6)|(1<<5)|0x1);
	RTL_W16 (BasicModeCtrl, (1<<13)|(1<<12)|(1<<9)|(1<<8));
	
	RTL_W8 (Cfg9346, 0xC0);
	RTL_W8 (Config1, tp->full_duplex ? 0x60 : 0x20);
	RTL_W8 (Cfg9346, 0x00);

	RTL_W32 (RxBuf, tp->rx_ring_dma);

	/* Start the chip's Tx and Rx process. */
	RTL_W32 (RxMissed, 0);
	rtl8139_set_rx_mode (dev);

	RTL_W8 (ChipCmd, CmdRxEnb | CmdTxEnb);

	/* Enable all known interrupts by setting the interrupt mask. */
	RTL_W16 (IntrMask,
		 PCIErr | PCSTimeout | RxUnderrun | RxOverflow | RxFIFOOver
		 | TxErr | TxOK | RxErr | RxOK);

	DPRINTK ("%s: rtl8139_open() ioaddr %#lx IRQ %d"
			" GP Pins %2.2x %s-duplex.\n",
			dev->name, pci_resource_start (tp->pci_dev, 1),
			dev->irq, RTL_R8 (GPPinData),
			tp->full_duplex ? "full" : "half");

	/* Set the timer to switch to check for link beat and perhaps switch
	   to an alternate media type. */
	init_timer (&tp->timer);
	tp->timer.expires = jiffies + 3 * HZ;
	tp->timer.data = (unsigned long) dev;
	tp->timer.function = &rtl8139_timer;
	add_timer (&tp->timer);

	netif_start_queue (dev);

	DPRINTK ("EXIT, returning 0\n");
	return 0;
}

/* Start the hardware at open or resume. */
static void rtl8139_hw_start (struct net_device *dev)
{
	struct rtl8139_private *tp = (struct rtl8139_private *) dev->priv;
	void *ioaddr = tp->mmio_addr;
	int i;

	DPRINTK ("ENTER\n");

	/* Soft reset the chip. */
	RTL_W8 (ChipCmd, CmdReset);
	/* Check that the chip has finished the reset. */
	for (i = 1000; i > 0; i--)
		if ((RTL_R8 (ChipCmd) & CmdReset) == 0)
			break;

	/* Restore our idea of the MAC address. */
	RTL_W32 (MAC0 + 0, cpu_to_le32 (*(u32 *) (dev->dev_addr + 0)));
	RTL_W32 (MAC0 + 4, cpu_to_le32 (*(u32 *) (dev->dev_addr + 4)));

	/* Hmmm, do these belong here? */
	RTL_W8 (Cfg9346, 0x00);
	tp->cur_rx = 0;

	/* Must enable Tx/Rx before setting transfer thresholds! */
	RTL_W8 (ChipCmd, CmdRxEnb | CmdTxEnb);
	RTL_W32 (RxConfig, tp->rx_config);
	/* Check this value: the documentation contradicts ifself.  Is the
	   IFG correct with bit 28:27 zero, or with |0x03000000 ? */
	RTL_W32 (TxConfig, (TX_DMA_BURST << 8) | 0x00000000);

	/* Reset N-Way to chipset defaults */
	RTL_W16 (BasicModeCtrl, (1<<15)|(1<<12)|(1<<9));
	for (i = 1000; i > 0; i--)
		if ((RTL_R8 (BasicModeCtrl) & (1<<15)) == 0)
			break;
	
	/* Set N-Way to sane defaults */
	RTL_W16 (FIFOTMS, 0x0000);
	RTL_W16 (NWayAdvert, (1<<13)|(1<<8)|(1<<7)|(1<<6)|(1<<5)|0x1);
	RTL_W16 (BasicModeCtrl, (1<<13)|(1<<12)|(1<<9)|(1<<8));
	
	/* check_duplex() here. */
	RTL_W8 (Cfg9346, 0xC0);
	RTL_W8 (Config1, tp->full_duplex ? 0x60 : 0x20);
	RTL_W8 (Cfg9346, 0x00);

	RTL_W32 (RxBuf, tp->rx_ring_dma);
	/* Start the chip's Tx and Rx process. */
	RTL_W32 (RxMissed, 0);
	rtl8139_set_rx_mode (dev);
	RTL_W8 (ChipCmd, CmdRxEnb | CmdTxEnb);
	/* Enable all known interrupts by setting the interrupt mask. */
	RTL_W16 (IntrMask,
		 PCIErr | PCSTimeout | RxUnderrun | RxOverflow | RxFIFOOver
		 | TxErr | TxOK | RxErr | RxOK);

	netif_start_queue (dev);

	DPRINTK ("EXIT\n");
}


#ifndef RTL_TUNE_TWISTER
static inline void rtl8139_tune_twister (struct net_device *dev,
				  struct rtl8139_private *tp) {}
#else
static void rtl8139_tune_twister (struct net_device *dev,
				  struct rtl8139_private *tp)
{
	int linkcase;

	DPRINTK ("ENTER\n");

	/* This is a complicated state machine to configure the "twister" for
	   impedance/echos based on the cable length.
	   All of this is magic and undocumented.
	 */
	switch (tp->twistie) {
	case 1:
		if (RTL_R16 (CSCR) & CSCR_LinkOKBit) {
			/* We have link beat, let us tune the twister. */
			RTL_W16 (CSCR, CSCR_LinkDownOffCmd);
			tp->twistie = 2;	/* Change to state 2. */
			next_tick = HZ / 10;
		} else {
			/* Just put in some reasonable defaults for when beat returns. */
			RTL_W16 (CSCR, CSCR_LinkDownCmd);
			RTL_W32 (FIFOTMS, 0x20);	/* Turn on cable test mode. */
			RTL_W32 (PARA78, PARA78_default);
			RTL_W32 (PARA7c, PARA7c_default);
			tp->twistie = 0;	/* Bail from future actions. */
		}
		break;
	case 2:
		/* Read how long it took to hear the echo. */
		linkcase = RTL_R16 (CSCR) & CSCR_LinkStatusBits;
		if (linkcase == 0x7000)
			tp->twist_row = 3;
		else if (linkcase == 0x3000)
			tp->twist_row = 2;
		else if (linkcase == 0x1000)
			tp->twist_row = 1;
		else
			tp->twist_row = 0;
		tp->twist_col = 0;
		tp->twistie = 3;	/* Change to state 2. */
		next_tick = HZ / 10;
		break;
	case 3:
		/* Put out four tuning parameters, one per 100msec. */
		if (tp->twist_col == 0)
			RTL_W16 (FIFOTMS, 0);
		RTL_W32 (PARA7c, param[(int) tp->twist_row]
			 [(int) tp->twist_col]);
		next_tick = HZ / 10;
		if (++tp->twist_col >= 4) {
			/* For short cables we are done.
			   For long cables (row == 3) check for mistune. */
			tp->twistie =
			    (tp->twist_row == 3) ? 4 : 0;
		}
		break;
	case 4:
		/* Special case for long cables: check for mistune. */
		if ((RTL_R16 (CSCR) &
		     CSCR_LinkStatusBits) == 0x7000) {
			tp->twistie = 0;
			break;
		} else {
			RTL_W32 (PARA7c, 0xfb38de03);
			tp->twistie = 5;
			next_tick = HZ / 10;
		}
		break;
	case 5:
		/* Retune for shorter cable (column 2). */
		RTL_W32 (FIFOTMS, 0x20);
		RTL_W32 (PARA78, PARA78_default);
		RTL_W32 (PARA7c, PARA7c_default);
		RTL_W32 (FIFOTMS, 0x00);
		tp->twist_row = 2;
		tp->twist_col = 0;
		tp->twistie = 3;
		next_tick = HZ / 10;
		break;

	default:
		/* do nothing */
		break;
	}

	DPRINTK ("EXIT\n");
}
#endif /* RTL_TUNE_TWISTER */


static void rtl8139_timer (unsigned long data)
{
	struct net_device *dev = (struct net_device *) data;
	struct rtl8139_private *tp = (struct rtl8139_private *) dev->priv;
	void *ioaddr = tp->mmio_addr;
	int next_tick = 60 * HZ;
	int mii_reg5 = mdio_read (dev, tp->phys[0], 5);

	DPRINTK ("ENTER\n");

	if (!tp->duplex_lock && mii_reg5 != 0xffff) {
		int duplex = (mii_reg5 & 0x0100)
		    || (mii_reg5 & 0x01C0) == 0x0040;
		if (tp->full_duplex != duplex) {
			tp->full_duplex = duplex;
			printk (KERN_INFO
				"%s: Setting %s-duplex based on MII #%d link"
				" partner ability of %4.4x.\n", dev->name,
				tp->full_duplex ? "full" : "half",
				tp->phys[0], mii_reg5);
			RTL_W8 (Cfg9346, 0xC0);
			RTL_W8 (Config1, tp->full_duplex ? 0x60 : 0x20);
			RTL_W8 (Cfg9346, 0x00);
		}
	}

	rtl8139_tune_twister (dev, tp);

	DPRINTK ("%s: Media selection tick, Link partner %4.4x.\n",
		 dev->name, RTL_R16 (NWayLPAR));
	DPRINTK ("%s:  Other registers are IntMask %4.4x IntStatus %4.4x"
		 " RxStatus %4.4x.\n", dev->name,
		 RTL_R16 (IntrMask),
		 RTL_R16 (IntrStatus),
		 RTL_R32 (RxEarlyStatus));
	DPRINTK ("%s:  Chip config %2.2x %2.2x.\n",
		 dev->name, RTL_R8 (Config0),
		 RTL_R8 (Config1));

	tp->timer.expires = jiffies + next_tick;
	add_timer (&tp->timer);

	DPRINTK ("EXIT\n");
}


static void rtl8139_tx_timeout (struct net_device *dev)
{
	struct rtl8139_private *tp = (struct rtl8139_private *) dev->priv;
	void *ioaddr = tp->mmio_addr;
	int mii_reg, i;

	DPRINTK ("ENTER\n");

	netif_stop_queue (dev);

	DPRINTK ("%s: Transmit timeout, status %2.2x %4.4x "
		 "media %2.2x.\n", dev->name,
		 readb (ioaddr + ChipCmd),
		 readw (ioaddr + IntrStatus),
		 readb (ioaddr + GPPinData));

	/* Disable interrupts by clearing the interrupt mask. */
	RTL_W16 (IntrMask, 0x0000);
	/* Emit info to figure out what went wrong. */
	printk (KERN_DEBUG
		"%s: Tx queue start entry %d  dirty entry %d.\n",
		dev->name, tp->cur_tx, tp->dirty_tx);
	for (i = 0; i < NUM_TX_DESC; i++)
		printk (KERN_DEBUG "%s:  Tx descriptor %d is %8.8x.%s\n",
			dev->name, i, readl (ioaddr + TxStatus0 + i * 4),
			i ==
		      tp->dirty_tx % NUM_TX_DESC ? " (queue head)" : "");
	printk (KERN_DEBUG "%s: MII #%d registers are:", dev->name,
		tp->phys[0]);
	for (mii_reg = 0; mii_reg < 8; mii_reg++)
		printk (" %4.4x", mdio_read (dev, tp->phys[0], mii_reg));
	printk (".\n");

	/* Stop a shared interrupt from scavenging while we are. */
	tp->dirty_tx = tp->cur_tx = 0;

	/* Dump the unsent Tx packets. */
	for (i = 0; i < NUM_TX_DESC; i++) {
		struct ring_info *rp = &tp->tx_info[i];
		if (rp->skb) {
			dev_kfree_skb (rp->skb);
			rp->skb = NULL;
			tp->stats.tx_dropped++;
		}
		if (rp->mapping != 0) {
			pci_unmap_single (tp->pci_dev, rp->mapping, rp->skb->len);
			rp->mapping = 0;
		}
	}

	rtl8139_hw_start (dev);

	DPRINTK ("EXIT\n");
}


/* Initialize the Rx and Tx rings, along with various 'dev' bits. */
static void rtl8139_init_ring (struct net_device *dev)
{
	struct rtl8139_private *tp = (struct rtl8139_private *) dev->priv;
	int i;

	DPRINTK ("ENTER\n");

	tp->cur_rx = 0;
	tp->dirty_tx = tp->cur_tx = 0;

	for (i = 0; i < NUM_TX_DESC; i++) {
		tp->tx_info[i].skb = NULL;
		tp->tx_info[i].mapping = 0;
		tp->tx_buf[i] = &tp->tx_bufs[i * TX_BUF_SIZE];
	}

	DPRINTK ("EXIT\n");
}

static int rtl8139_start_xmit (struct sk_buff *skb, struct net_device *dev)
{
	struct rtl8139_private *tp = (struct rtl8139_private *) dev->priv;
	void *ioaddr = tp->mmio_addr;
	int entry;
	unsigned long flags;

	DPRINTK ("ENTER\n");
	
	netif_stop_queue (dev);

	/* Calculate the next Tx descriptor entry. */
	entry = tp->cur_tx % NUM_TX_DESC;

	spin_lock_irqsave (&tp->lock, flags);
	
	tp->tx_info[entry].skb = skb;
	if ((long) skb->data & 3) {	/* Must use alignment buffer. */
		tp->tx_info[entry].mapping = 0;
		memcpy (tp->tx_buf[entry], skb->data, skb->len);

		assert (tp->tx_bufs_dma > 0);
		RTL_W32 (TxAddr0 + entry * 4, tp->tx_bufs_dma + (tp->tx_buf[entry] - tp->tx_bufs));
	} else {
		tp->tx_info[entry].mapping =
			pci_map_single(tp->pci_dev, skb->data, skb->len);

		assert (tp->tx_info[entry].mapping > 0);
		RTL_W32 (TxAddr0 + entry * 4, tp->tx_info[entry].mapping);
	}
	
	/* Note: the chip doesn't have auto-pad! */
	RTL_W32 (TxStatus0 + entry * 4,
		 tp->tx_flag | (skb->len >= ETH_ZLEN ? skb->len : ETH_ZLEN));

	dev->trans_start = jiffies;
	if (++tp->cur_tx - tp->dirty_tx < NUM_TX_DESC)	/* Typical path */
		netif_start_queue (dev);

	spin_unlock_irqrestore (&tp->lock, flags);
	
	DPRINTK ("%s: Queued Tx packet at %p size %lu to slot %d.\n",
		 dev->name, skb->data, skb->len, entry);

	DPRINTK ("EXIT\n");
	return 0;
}


static inline void rtl8139_tx_interrupt (struct net_device *dev,
					 struct rtl8139_private *tp)
{
	void *ioaddr;
	unsigned int dirty_tx;

	assert (dev != NULL);
	assert (tp != NULL);

	dirty_tx = tp->dirty_tx;
	ioaddr = tp->mmio_addr;

	while (tp->cur_tx - dirty_tx > 0) {
		int entry = dirty_tx % NUM_TX_DESC;
		int txstatus = readl (ioaddr + TxStatus0 + entry * 4);

		if (!(txstatus & (TxStatOK | TxUnderrun | TxAborted)))
			break;	/* It still hasn't been Txed */

		/* Note: TxCarrierLost is always asserted at 100mbps. */
		if (txstatus & (TxOutOfWindow | TxAborted)) {
			/* There was an major error, log it. */
			DPRINTK ("%s: Transmit error, Tx status %8.8x.\n",
				 dev->name, txstatus);
			tp->stats.tx_errors++;
			if (txstatus & TxAborted) {
				tp->stats.tx_aborted_errors++;
				RTL_W32 (TxConfig, (TX_DMA_BURST << 8) | 0x03000001);
			}
			if (txstatus & TxCarrierLost)
				tp->stats.tx_carrier_errors++;
			if (txstatus & TxOutOfWindow)
				tp->stats.tx_window_errors++;
#ifdef ETHER_STATS
			if ((txstatus & 0x0f000000) == 0x0f000000)
				tp->stats.collisions16++;
#endif
		} else {
			if (txstatus & TxUnderrun) {
				/* Add 64 to the Tx FIFO threshold. */
				if (tp->tx_flag < 0x00300000)
					tp->tx_flag += 0x00020000;
				tp->stats.tx_fifo_errors++;
			}
			tp->stats.collisions += (txstatus >> 24) & 15;
			tp->stats.tx_bytes += txstatus & 0x7ff;
			tp->stats.tx_packets++;
		}

		if (tp->tx_info[entry].mapping != 0) {
			pci_unmap_single (tp->pci_dev,
					  tp->tx_info[entry].mapping,
					  tp->tx_info[entry].skb->len);
			tp->tx_info[entry].mapping = 0;
		}
		/* Free the original skb. */
		dev_kfree_skb_irq (tp->tx_info[entry].skb);
		tp->tx_info[entry].skb = NULL;
		dirty_tx++;
		if (tp->cur_tx - dirty_tx < NUM_TX_DESC)
			netif_wake_queue (dev);
		else
			netif_stop_queue (dev);
	}

#ifndef RTL8139_NDEBUG
	if (tp->cur_tx - dirty_tx > NUM_TX_DESC) {
		printk (KERN_ERR
		  "%s: Out-of-sync dirty pointer, %d vs. %d.\n",
		     dev->name, dirty_tx, tp->cur_tx);
		dirty_tx += NUM_TX_DESC;
	}
#endif

	tp->dirty_tx = dirty_tx;
}


/* The data sheet doesn't describe the Rx ring at all, so I'm guessing at the
   field alignments and semantics. */
static inline void rtl8139_rx_interrupt (struct net_device *dev,
					 struct rtl8139_private *tp)
{
	void *ioaddr = tp->mmio_addr;
	unsigned char *rx_ring = tp->rx_ring;
	u16 cur_rx = tp->cur_rx;

	DPRINTK ("%s: In rtl8139_rx(), current %4.4x BufAddr %4.4x,"
			" free to %4.4x, Cmd %2.2x.\n", dev->name, cur_rx,
			readw (ioaddr + RxBufAddr),
			readw (ioaddr + RxBufPtr),
			readb (ioaddr + ChipCmd));

	while ((readb (ioaddr + ChipCmd) & RxBufEmpty) == 0) {
		int ring_offset = cur_rx % RX_BUF_LEN;
		u32 rx_status =
		    le32_to_cpu (*(u32 *) (rx_ring + ring_offset));
		int rx_size = rx_status >> 16;

#ifdef RTL8139_DEBUG
		int i;
		DPRINTK ("%s:  rtl8139_rx() status %4.4x, size %4.4x,"
			" cur %4.4x.\n", dev->name, rx_status,
			rx_size, cur_rx);
		DPRINTK ("%s: Frame contents ", dev->name);
		for (i = 0; i < 70; i++)
			printk (" %2.2x", rx_ring[ring_offset + i]);
		printk (".\n");
#endif

		if (rx_status &
		    (RxBadSymbol | RxRunt | RxTooLong | RxCRCErr |
		     RxBadAlign)) {
			DPRINTK ("%s: Ethernet frame had errors,"
					" status %8.8x.\n", dev->name,
					rx_status);
			if (rx_status & RxTooLong) {
				DPRINTK ("%s: Oversized Ethernet frame, status %4.4x!\n",
						dev->name, rx_status);
				/* A.C.: The chip hangs here. */
			}
			tp->stats.rx_errors++;
			if (rx_status & (RxBadSymbol | RxBadAlign))
				tp->stats.rx_frame_errors++;
			if (rx_status & (RxRunt | RxTooLong))
				tp->stats.rx_length_errors++;
			if (rx_status & RxCRCErr)
				tp->stats.rx_crc_errors++;
			/* Reset the receiver, based on RealTek recommendation. (Bug?) */
			tp->cur_rx = 0;
			RTL_W8 (ChipCmd, CmdTxEnb);
			/* A.C.: Reset the multicast list. */
			rtl8139_set_rx_mode (dev);
			RTL_W8 (ChipCmd, CmdRxEnb | CmdTxEnb);
		} else {
			/* Malloc up new buffer, compatible with net-2e. */
			/* Omit the four octet CRC from the length. */
			struct sk_buff *skb;

			skb = dev_alloc_skb (rx_size + 2);
			if (skb == NULL) {
				printk (KERN_WARNING
					"%s: Memory squeeze, deferring packet.\n",
					dev->name);
				/* We should check that some rx space is free.
				   If not, free one and mark stats->rx_dropped++. */
				tp->stats.rx_dropped++;
				break;
			}
			skb->dev = dev;
			skb_reserve (skb, 2);	/* 16 byte align the IP fields. */
			if (ring_offset + rx_size + 4 > RX_BUF_LEN) {
				int semi_count =
				    RX_BUF_LEN - ring_offset - 4;
				/* This could presumably use two calls to copy_and_sum()? */
				memcpy (skb_put (skb, semi_count),
					&rx_ring[ring_offset + 4],
					semi_count);
				memcpy (skb_put
					(skb, rx_size - semi_count),
					rx_ring, rx_size - semi_count);
#ifdef RTL8139_DEBUG
				{
				int i;
				printk (KERN_DEBUG
					"%s:  Frame wrap @%d",
					dev->name, semi_count);
				for (i = 0; i < 16; i++)
					printk (" %2.2x",
						rx_ring[i]);
				printk (".\n");
				memset (rx_ring, 0xcc, 16);
				}
#endif /* RTL8139_DEBUG */

			} else {
				eth_copy_and_sum (skb,
						  &rx_ring[ring_offset +
							   4], rx_size, 0);
				skb_put (skb, rx_size);
			}
			skb->protocol = eth_type_trans (skb, dev);
			netif_rx (skb);
			tp->stats.rx_bytes += rx_size;
			tp->stats.rx_packets++;
		}

		cur_rx = (cur_rx + rx_size + 4 + 3) & ~3;
		RTL_W16 (RxBufPtr, cur_rx - 16);
	}
	DPRINTK ("%s: Done rtl8139_rx(), current %4.4x BufAddr %4.4x,"
		" free to %4.4x, Cmd %2.2x.\n", dev->name, cur_rx,
		readw (ioaddr + RxBufAddr),
		readw (ioaddr + RxBufPtr),
		readb (ioaddr + ChipCmd));
	tp->cur_rx = cur_rx;
}


static inline int rtl8139_weird_interrupt (struct net_device *dev,
					   struct rtl8139_private *tp,
					   int status, int link_changed)
{
	void *ioaddr;

	DPRINTK ("%s: Abnormal interrupt, status %8.8x.\n",
		 dev->name, status);
		 
	assert (dev != NULL);
	assert (tp != NULL);

	ioaddr = tp->mmio_addr;

	if (status == 0xffffffff) {
		printk (KERN_WARNING PFX "abnormal interrupt, card ejected? (ok to ignore)\n");
		return -1;
	}

	/* Update the error count. */
	tp->stats.rx_missed_errors +=
	    readl (ioaddr + RxMissed);
	RTL_W32 (RxMissed, 0);

	if ((status & RxUnderrun) && link_changed &&
	    (tp->drv_flags & HAS_LNK_CHNG)) {
		/* Really link-change on new chips. */
		int lpar = readw (ioaddr + NWayLPAR);
		int duplex = (lpar & 0x0100) || (lpar & 0x01C0) == 0x0040
				|| tp->duplex_lock;
		if (tp->full_duplex != duplex) {
			tp->full_duplex = duplex;
			RTL_W8 (Cfg9346, 0xC0);
			RTL_W8 (Config1, tp->full_duplex ? 0x60 : 0x20);
			RTL_W8 (Cfg9346, 0x00);
		}
		status &= ~RxUnderrun;
	}
	if (status &
	    (RxUnderrun | RxOverflow | RxErr | RxFIFOOver))
		tp->stats.rx_errors++;

	if (status & (PCSTimeout))
		tp->stats.rx_length_errors++;
	if (status & (RxUnderrun | RxFIFOOver))
		tp->stats.rx_fifo_errors++;
	if (status & RxOverflow) {
		tp->stats.rx_over_errors++;
		tp->cur_rx = readw (ioaddr + RxBufAddr) % RX_BUF_LEN;
		RTL_W16 (RxBufPtr, tp->cur_rx - 16);
	}
	if (status & PCIErr) {
		u16 pci_cmd_status;
		pci_read_config_word (tp->pci_dev, PCI_STATUS, &pci_cmd_status);

		printk (KERN_ERR "%s: PCI Bus error %4.4x.\n",
			dev->name, pci_cmd_status);
	}
	
	return 0;
}


/* The interrupt handler does all of the Rx thread work and cleans up
   after the Tx thread. */
static void rtl8139_interrupt (int irq, void *dev_instance,
			       struct pt_regs *regs)
{
	struct net_device *dev = (struct net_device *) dev_instance;
	struct rtl8139_private *tp = (struct rtl8139_private *) dev->priv;
	int boguscnt = max_interrupt_work;
	void *ioaddr = tp->mmio_addr;
	int link_changed = 0;	/* Grrr, avoid bogus "uninitialized" warning */

	spin_lock_irq (&tp->lock);

	do {
		int status = readw (ioaddr + IntrStatus);
		/* Acknowledge all of the current interrupt sources ASAP, but
		   an first get an additional status bit from CSCR. */
		if (status & RxUnderrun)
			link_changed = readw (ioaddr + CSCR) & CSCR_LinkChangeBit;
		RTL_W16 (IntrStatus, status);

		DPRINTK ("%s: interrupt  status=%#4.4x new intstat=%#4.4x.\n",
				dev->name, status,
				readw (ioaddr + IntrStatus));

		if ((status &
		     (PCIErr | PCSTimeout | RxUnderrun | RxOverflow |
		      RxFIFOOver | TxErr | TxOK | RxErr | RxOK)) == 0)
			break;

		/* Check uncommon events with one test. */
		if (status & (PCIErr | PCSTimeout | RxUnderrun | RxOverflow |
		  	      RxFIFOOver | TxErr | RxErr))
			if (rtl8139_weird_interrupt (dev, tp, status,
						     link_changed) == -1)
				break;

		if (status & (RxOK | RxUnderrun | RxOverflow | RxFIFOOver))	/* Rx interrupt */
			rtl8139_rx_interrupt (dev, tp);

		if (status & (TxOK | TxErr))
			rtl8139_tx_interrupt (dev, tp);

		if (--boguscnt < 0) {
			printk (KERN_WARNING
				"%s: Too much work at interrupt, "
				"IntrStatus=0x%4.4x.\n", dev->name,
				status);
			/* Clear all interrupt sources. */
			RTL_W16 (IntrStatus, 0xffff);
			break;
		}
	} while (1);

	DPRINTK ("%s: exiting interrupt, intr_status=%#4.4x.\n",
			dev->name, readw (ioaddr + IntrStatus));

	spin_unlock_irq (&tp->lock);
	return;
}

static int rtl8139_close (struct net_device *dev)
{
	struct rtl8139_private *tp = (struct rtl8139_private *) dev->priv;
	void *ioaddr = tp->mmio_addr;
	int i;

	DPRINTK ("ENTER\n");

	netif_stop_queue (dev);

	DPRINTK ("%s: Shutting down ethercard, status was 0x%4.4x.\n",
			dev->name, readw (ioaddr + IntrStatus));

	/* Disable interrupts by clearing the interrupt mask. */
	RTL_W16 (IntrMask, 0x0000);

	/* Stop the chip's Tx and Rx DMA processes. */
	RTL_W8 (ChipCmd, 0x00);

	/* Update the error counts. */
	tp->stats.rx_missed_errors += RTL_R32 (RxMissed);
	RTL_W32 (RxMissed, 0);

	del_timer (&tp->timer);

	free_irq (dev->irq, dev);

	for (i = 0; i < NUM_TX_DESC; i++) {
		struct sk_buff *skb = tp->tx_info[i].skb;
		dma_addr_t mapping = tp->tx_info[i].mapping;

		if (skb) {
			if (mapping)
				pci_unmap_single (tp->pci_dev, mapping, skb->len);
			dev_kfree_skb (skb);
		}
		tp->tx_info[i].skb = NULL;
		tp->tx_info[i].mapping = 0;
	}

	pci_free_consistent(tp->pci_dev, RX_BUF_LEN + 16,
			    tp->rx_ring, tp->rx_ring_dma);
	pci_free_consistent(tp->pci_dev, TX_BUF_SIZE * NUM_TX_DESC,
			    tp->tx_bufs, tp->tx_bufs_dma);
	tp->rx_ring = NULL;
	tp->tx_bufs = NULL;

	/* Green! Put the chip in low-power mode. */
	RTL_W8 (Cfg9346, 0xC0);
	RTL_W8 (Config1, 0x03);
	RTL_W8 (HltClk, 'H');	/* 'R' would leave the clock running. */

	MOD_DEC_USE_COUNT;

	DPRINTK ("EXIT\n");
	return 0;
}

static int mii_ioctl (struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct rtl8139_private *tp = (struct rtl8139_private *) dev->priv;
	u16 *data = (u16 *) & rq->ifr_data;

	DPRINTK ("ENTER\n");

	switch (cmd) {
	case SIOCDEVPRIVATE:	/* Get the address of the PHY in use. */
		data[0] = tp->phys[0] & 0x3f;
		/* Fall Through */
	case SIOCDEVPRIVATE + 1:	/* Read the specified MII register. */
		data[3] = mdio_read (dev, data[0], data[1] & 0x1f);
		DPRINTK ("EXIT\n");
		return 0;
	case SIOCDEVPRIVATE + 2:	/* Write the specified MII register */
		if (!capable (CAP_NET_ADMIN))
			return -EPERM;
		mdio_write (dev, data[0], data[1] & 0x1f, data[2]);
		DPRINTK ("EXIT\n");
		return 0;
	default:
		DPRINTK ("EXIT\n");
		return -EOPNOTSUPP;
	}

	DPRINTK ("EXIT\n");
}

static struct net_device_stats *rtl8139_get_stats (struct net_device *dev)
{
	struct rtl8139_private *tp = (struct rtl8139_private *) dev->priv;
	void *ioaddr = tp->mmio_addr;

	DPRINTK ("ENTER\n");

	assert (tp != NULL);

	if (test_bit(LINK_STATE_START, &dev->state)) {
		tp->stats.rx_missed_errors += RTL_R32 (RxMissed);
		RTL_W32 (RxMissed, 0);
	}

	DPRINTK ("EXIT\n");
	return &tp->stats;
}

/* Set or clear the multicast filter for this adaptor.
   This routine is not state sensitive and need not be SMP locked. */

static unsigned const ethernet_polynomial = 0x04c11db7U;
static inline u32 ether_crc (int length, unsigned char *data)
{
	int crc = -1;

	DPRINTK ("ENTER\n");

	while (--length >= 0) {
		unsigned char current_octet = *data++;
		int bit;
		for (bit = 0; bit < 8; bit++, current_octet >>= 1)
			crc = (crc << 1) ^
			    ((crc < 0) ^ (current_octet & 1) ?
			     ethernet_polynomial : 0);
	}

	DPRINTK ("EXIT\n");
	return crc;
}

static void rtl8139_set_rx_mode (struct net_device *dev)
{
	struct rtl8139_private *tp = (struct rtl8139_private *) dev->priv;
	void *ioaddr = tp->mmio_addr;
	u32 mc_filter[2];	/* Multicast hash filter */
	int i, rx_mode;

	DPRINTK ("ENTER\n");

	DPRINTK ("%s:   rtl8139_set_rx_mode(%4.4x) done -- Rx config %8.8x.\n",
			dev->name, dev->flags, readl (ioaddr + RxConfig));

	/* Note: do not reorder, GCC is clever about common statements. */
	if (dev->flags & IFF_PROMISC) {
		/* Unconditionally log net taps. */
		printk (KERN_NOTICE "%s: Promiscuous mode enabled.\n",
			dev->name);
		rx_mode =
		    AcceptBroadcast | AcceptMulticast | AcceptMyPhys |
		    AcceptAllPhys;
		mc_filter[1] = mc_filter[0] = 0xffffffff;
	} else if ((dev->mc_count > multicast_filter_limit)
		   || (dev->flags & IFF_ALLMULTI)) {
		/* Too many to filter perfectly -- accept all multicasts. */
		rx_mode = AcceptBroadcast | AcceptMulticast | AcceptMyPhys;
		mc_filter[1] = mc_filter[0] = 0xffffffff;
	} else {
		struct dev_mc_list *mclist;
		rx_mode = AcceptBroadcast | AcceptMulticast | AcceptMyPhys;
		mc_filter[1] = mc_filter[0] = 0;
		for (i = 0, mclist = dev->mc_list;
		     mclist && i < dev->mc_count;
		     i++, mclist =
		     mclist->next) set_bit (ether_crc (ETH_ALEN,
						       mclist->
						       dmi_addr) >> 26,
					    mc_filter);
	}
	/* We can safely update without stopping the chip. */
	RTL_W32 (RxConfig, tp->rx_config | rx_mode);
	RTL_W32 (MAR0 + 0, mc_filter[0]);
	RTL_W32 (MAR0 + 4, mc_filter[1]);

	DPRINTK ("EXIT\n");
}


static void rtl8139_suspend (struct pci_dev *pdev)
{
	struct net_device *dev = PCI_GET_DRIVER_DATA (pdev);
	struct rtl8139_private *tp = (struct rtl8139_private *) dev->priv;
	void *ioaddr = tp->mmio_addr;

	netif_stop_queue (dev);
	
	/* Disable interrupts, stop Tx and Rx. */
	RTL_W16 (IntrMask, 0x0000);
	RTL_W8 (ChipCmd, 0x00);

	/* Update the error counts. */
	tp->stats.rx_missed_errors += RTL_R32 (RxMissed);
	RTL_W32 (RxMissed, 0);
}


static void rtl8139_resume (struct pci_dev *pdev)
{
	struct net_device *dev = PCI_GET_DRIVER_DATA (pdev);

	rtl8139_hw_start(dev);
}


static struct pci_driver rtl8139_pci_driver = {
	name:		RTL8139_MODULE_NAME,
	id_table:	rtl8139_pci_tbl,
	probe:		rtl8139_init_one,
	remove:		rtl8139_remove_one,
	suspend:	rtl8139_suspend,
	resume:		rtl8139_resume,
};


static int __init rtl8139_init_module (void)
{
	int rc;
	
	DPRINTK ("ENTER\n");

	rc = pci_register_driver (&rtl8139_pci_driver);
	
	if (rc > 0) {
		printk (KERN_INFO RTL8139_DRIVER_NAME
			" loaded (%d device%s registered)\n",
			rc, rc > 1 ? "s" : "");
	}

	DPRINTK ("EXIT\n");
	return rc > 0 ? 0 : -ENODEV;
}


static void __exit rtl8139_cleanup_module (void)
{
	pci_unregister_driver (&rtl8139_pci_driver);
}


module_init(rtl8139_init_module);
module_exit(rtl8139_cleanup_module);
