/* rtl8139.c: A RealTek RTL8129/8139 Fast Ethernet driver for Linux. */
/*
	Written 1997 by Donald Becker.

	This software may be used and distributed according to the terms
	of the GNU Public License, incorporated herein by reference.
    All other rights reserved.

	This driver is for boards based on the RTL8129 and RTL8139 PCI ethernet
	chips.

	The author may be reached as becker@CESDIS.gsfc.nasa.gov, or C/O
	Center of Excellence in Space Data and Information Sciences
	   Code 930.5, Goddard Space Flight Center, Greenbelt MD 20771

	Support and updates available at
	http://cesdis.gsfc.nasa.gov/linux/drivers/rtl8139.html
*/

static const char *version =
"rtl8139.c:v0.14 12/9/97 Donald Becker http://cesdis.gsfc.nasa.gov/linux/drivers/rtl8139.html\n";

/* A few user-configurable values. */
/* Maximum events (Rx packets, etc.) to handle at each interrupt. */
static int max_interrupt_work = 10;

/* Size of the in-memory receive ring. */
#define RX_BUF_LEN_IDX	3			/* 0==8K, 1==16K, 2==32K, 3==64K */
#define RX_BUF_LEN (8192 << RX_BUF_LEN_IDX)
/* Size of the Tx bounce buffers -- must be at least (dev->mtu+14+4). */
#define TX_BUF_SIZE	1536

/* PCI Tuning Parameters
   Threshold is bytes transferred to chip before transmission starts. */
#define TX_FIFO_THRESH 256	/* In bytes, rounded down to 32 byte units. */

/* The following settings are log_2(bytes)-4:  0 == 16 bytes .. 6==1024. */
#define RX_FIFO_THRESH	4		/* Rx buffer level before first PCI xfer.  */
#define RX_DMA_BURST	4		/* Maximum PCI burst, '4' is 256 bytes */
#define TX_DMA_BURST	4

/* Operational parameters that usually are not changed. */
/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT  ((4000*HZ)/1000)

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

#ifdef SA_SHIRQ
#define FREE_IRQ(irqnum, dev) free_irq(irqnum, dev)
#define REQUEST_IRQ(i,h,f,n, instance) request_irq(i,h,f,n, instance)
#define IRQ(irq, dev_id, pt_regs) (irq, dev_id, pt_regs)
#else
#define FREE_IRQ(irqnum, dev) free_irq(irqnum)
#define REQUEST_IRQ(i,h,f,n, instance) request_irq(i,h,f,n)
#define IRQ(irq, dev_id, pt_regs) (irq, pt_regs)
#endif

#if (LINUX_VERSION_CODE < 0x20123)
#define test_and_set_bit(val, addr) set_bit(val, addr)
#include <linux/bios32.h>
#endif

/* The I/O extent. */
#define RTL8129_TOTAL_SIZE 0x80

#ifdef HAVE_DEVLIST
struct netdev_entry rtl8139_drv =
{"RTL8139", rtl8139_probe, RTL8129_TOTAL_SIZE, NULL};
#endif

static int rtl8129_debug = 1;

/*
				Theory of Operation

I. Board Compatibility

This device driver is designed for the RealTek RTL8129, the RealTek Fast
Ethernet controllers for PCI.  This chip is used on a few clone boards.


II. Board-specific settings

PCI bus devices are configured by the system at boot time, so no jumpers
need to be set on the board.  The system BIOS will assign the
PCI INTA signal to a (preferably otherwise unused) system IRQ line.
Note: Kernel versions earlier than 1.3.73 do not support shared PCI
interrupt lines.

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

The RTL8129 uses a fixed set of four Tx descriptors in register space.
In a stunningly bad design choice, Tx frames must be 32 bit aligned.  Linux
aligns the IP header on word boundaries, and 14 byte ethernet header means
that almost all frames will need to be copied to an alignment buffer.

IVb. References

http://www.realtek.com.tw/cn/cn.html
http://cesdis.gsfc.nasa.gov/linux/misc/NWay.html

IVc. Errata

*/

#ifndef PCI_VENDOR_ID_REALTEK
#define PCI_VENDOR_ID_REALTEK		0x10ec
#endif
#ifndef PCI_DEVICE_ID_REALTEK_8129
#define PCI_DEVICE_ID_REALTEK_8129	0x8129
#endif
#ifndef PCI_DEVICE_ID_REALTEK_8139
#define PCI_DEVICE_ID_REALTEK_8139	0x8139
#endif

/* The rest of these values should never change. */
#define NUM_TX_DESC	4			/* Number of Tx descriptor registers. */

/* Symbolic offsets to registers. */
enum RTL8129_registers {
	MAC0=0,						/* Ethernet hardware address. */
	MAR0=8,						/* Multicast filter. */
	TxStat0=0x10,				/* Transmit status (Four 32bit registers). */
	TxAddr0=0x20,				/* Tx descriptors (also four 32bit). */
	RxBuf=0x30, RxEarlyCnt=0x34, RxEarlyStatus=0x36,
	ChipCmd=0x37, RxBufPtr=0x38, RxBufAddr=0x3A,
	IntrMask=0x3C, IntrStatus=0x3E,
	TxConfig=0x40, RxConfig=0x44,
	Timer=0x48,					/* A general-purpose counter. */
	RxMissed=0x4C,				/* 24 bits valid, write clears. */
	Cfg9346=0x50, Config0=0x51, Config1=0x52,
	FlashReg=0x54, GPPinData=0x58, GPPinDir=0x59, MII_SMI=0x5A, HltClk=0x5B,
	MultiIntr=0x5C, TxSummary=0x60,
	BMCR=0x62, BMSR=0x64, NWayAdvert=0x66, NWayLPAR=0x68, NWayExpansion=0x6A,
};

enum ChipCmdBits {
	CmdReset=0x10, CmdRxEnb=0x08, CmdTxEnb=0x04, RxBufEmpty=0x01, };

/* Interrupt register bits, using my own meaningful names. */
enum IntrStatusBits {
	PCIErr=0x8000, PCSTimeout=0x4000,
	RxFIFOOver=0x40, RxUnderrun=0x20, RxOverflow=0x10,
	TxErr=0x08, TxOK=0x04, RxErr=0x02, RxOK=0x01,
};
enum TxStatusBits {
	TxHostOwns=0x2000, TxUnderrun=0x4000, TxStatOK=0x8000,
	TxOutOfWindow=0x20000000, TxAborted=0x40000000, TxCarrierLost=0x80000000,
};
enum RxStatusBits {
	RxMulticast=0x8000, RxPhysical=0x4000, RxBroadcast=0x2000,
	RxBadSymbol=0x0020, RxRunt=0x0010, RxTooLong=0x0008, RxCRCErr=0x0004,
	RxBadAlign=0x0002, RxStatusOK=0x0001,
};

struct rtl8129_private {
	char devname[8];			/* Used only for kernel debugging. */
	const char *product_name;
	struct device *next_module;
	int chip_id;
	int chip_revision;
	struct enet_statistics stats;
	struct timer_list timer;	/* Media selection timer. */
	unsigned int cur_rx, cur_tx;		/* The next free and used entries */
	unsigned int dirty_rx, dirty_tx;
	/* The saved address of a sent-in-place packet/buffer, for skfree(). */
	struct sk_buff* tx_skbuff[NUM_TX_DESC];
	unsigned char *tx_buf[NUM_TX_DESC];	/* Tx bounce buffers */
	unsigned char *rx_ring;
	unsigned char *tx_bufs;				/* Tx bounce buffer region. */
	unsigned char mc_filter[8];			/* Current multicast filter. */
	char phys[4];						/* MII device addresses. */
	unsigned int tx_full:1;				/* The Tx queue is full. */
	unsigned int full_duplex:1;			/* Full-duplex operation requested. */
	unsigned int default_port:4;		/* Last dev->if_port value. */
	unsigned int media2:4;				/* Secondary monitored media port. */
	unsigned int medialock:1;			/* Don't sense media type. */
	unsigned int mediasense:1;			/* Media sensing in progress. */
};

#ifdef MODULE
/* Used to pass the full-duplex flag, etc. */
static int options[] = {-1, -1, -1, -1, -1, -1, -1, -1};
static int full_duplex[] = {-1, -1, -1, -1, -1, -1, -1, -1};
#if LINUX_VERSION_CODE > 0x20118
MODULE_AUTHOR("Donald Becker <becker@cesdis.gsfc.nasa.gov>");
MODULE_DESCRIPTION("RealTek RTL8129/8139 Fast Ethernet driver");
MODULE_PARM(debug, "i");
MODULE_PARM(options, "1-" __MODULE_STRING(8) "i");
MODULE_PARM(full_duplex, "1-" __MODULE_STRING(8) "i");
MODULE_PARM(max_interrupt_work, "i");
#endif
#endif

static struct device *rtl8129_probe1(struct device *dev, int ioaddr, int irq,
									 int chip_id, int options, int card_idx);
static int rtl8129_open(struct device *dev);
static int read_eeprom(int ioaddr, int location);
static int mdio_read(int ioaddr, int phy_id, int location);
static void rtl8129_timer(unsigned long data);
static void rtl8129_tx_timeout(struct device *dev);
static void rtl8129_init_ring(struct device *dev);
static int rtl8129_start_xmit(struct sk_buff *skb, struct device *dev);
static int rtl8129_rx(struct device *dev);
static void rtl8129_interrupt(int irq, void *dev_instance, struct pt_regs *regs);
static int rtl8129_close(struct device *dev);
static struct enet_statistics *rtl8129_get_stats(struct device *dev);
#ifdef NEW_MULTICAST
static void set_rx_mode(struct device *dev);
#else
static void set_rx_mode(struct device *dev, int num_addrs, void *addrs);
#endif



#ifdef MODULE
/* A list of all installed RTL8129 devices, for removing the driver module. */
static struct device *root_rtl8129_dev = NULL;
#endif

int rtl8139_probe(struct device *dev)
{
	int cards_found = 0;
	static int pci_index = 0;	/* Static, for multiple probe calls. */

	/* Ideally we would detect all network cards in slot order.  That would
	   be best done a central PCI probe dispatch, which wouldn't work
	   well with the current structure.  So instead we detect just the
	   Rtl81*9 cards in slot order. */

	if (pci_present()) {
		unsigned char pci_bus, pci_device_fn;

		for (;pci_index < 0xff; pci_index++) {
			unsigned char pci_latency;
#if LINUX_VERSION_CODE >= 0x20155
			unsigned int pci_irq_line;
			struct pci_dev *pdev;
#else
			unsigned char pci_irq_line;
#endif
			unsigned short pci_command, new_command, vendor, device;
			unsigned int pci_ioaddr;

			if (pcibios_find_class (PCI_CLASS_NETWORK_ETHERNET << 8,
#ifdef REVERSE_PROBE_ORDER
									0xff - pci_index,
#else
									pci_index,
#endif
									&pci_bus, &pci_device_fn)
				!= PCIBIOS_SUCCESSFUL)
				break;
			pcibios_read_config_word(pci_bus, pci_device_fn,
									 PCI_VENDOR_ID, &vendor);
			if (vendor != PCI_VENDOR_ID_REALTEK)
				continue;

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

			if (device != PCI_DEVICE_ID_REALTEK_8129
				&&  device != PCI_DEVICE_ID_REALTEK_8139) {
				printk(KERN_NOTICE"Unknown RealTek PCI ethernet chip type "
					   "%4.4x detected: not configured.\n", device);
				continue;
			}
			if (check_region(pci_ioaddr, RTL8129_TOTAL_SIZE))
				continue;

			pcibios_read_config_word(pci_bus, pci_device_fn,
									 PCI_COMMAND, &pci_command);
			new_command = pci_command | PCI_COMMAND_MASTER|PCI_COMMAND_IO;
			if (pci_command != new_command) {
				printk(KERN_INFO "  The PCI BIOS has not enabled this"
					   " device!  Updating PCI config %4.4x->%4.4x.\n",
					   pci_command, new_command);
				pcibios_write_config_word(pci_bus, pci_device_fn,
										  PCI_COMMAND, new_command);
			}

#ifdef MODULE
			dev = rtl8129_probe1(dev, pci_ioaddr, pci_irq_line, device,
								 options[cards_found], cards_found);
#else
			dev = rtl8129_probe1(dev, pci_ioaddr, pci_irq_line, device,
								 dev ? dev->mem_start : 0, -1);
#endif

			if (dev) {
				pcibios_read_config_byte(pci_bus, pci_device_fn,
										 PCI_LATENCY_TIMER, &pci_latency);
				if (pci_latency < 32) {
					printk(KERN_NOTICE"  PCI latency timer (CFLT) is "
						   "unreasonably low at %d.  Setting to 64 clocks.\n",
						   pci_latency);
					pcibios_write_config_byte(pci_bus, pci_device_fn,
											  PCI_LATENCY_TIMER, 64);
				} else if (rtl8129_debug > 1)
					printk(KERN_INFO"  PCI latency timer (CFLT) is %#x.\n",
						   pci_latency);
				dev = 0;
				cards_found++;
			}
		}
	}

#if defined (MODULE)
	return cards_found;
#else
	return cards_found ? 0 : -ENODEV;
#endif
}

static struct device *rtl8129_probe1(struct device *dev, int ioaddr, int irq,
								   int chip_id, int options, int card_idx)
{
	static int did_version = 0;			/* Already printed version info. */
	struct rtl8129_private *tp;
	int i;

	if (rtl8129_debug > 0  &&  did_version++ == 0)
		printk(KERN_INFO "%s", version);

	dev = init_etherdev(dev, 0);

	printk(KERN_INFO "%s: RealTek RTL%x at %#3x, IRQ %d, ",
		   dev->name, chip_id, ioaddr, irq);

	/* Bring the chip out of low-power mode. */
	outb(0x00, ioaddr + Config1);

	/* Perhaps this should be read from the EEPROM? */
	for (i = 0; i < 6; i++)
		dev->dev_addr[i] = inb(ioaddr + MAC0 + i);

	for (i = 0; i < 5; i++)
		printk("%2.2x:", dev->dev_addr[i]);
	printk("%2.2x.\n", dev->dev_addr[i]);

	if (rtl8129_debug > 1) {
		printk(KERN_INFO "%s: EEPROM contents\n", dev->name);
		for (i = 0; i < 64; i++)
			printk(" %4.4x%s", read_eeprom(ioaddr, i),
				   i%16 == 15 ? "\n"KERN_INFO : "");
	}

	/* We do a request_region() to register /proc/ioports info. */
	request_region(ioaddr, RTL8129_TOTAL_SIZE, "RealTek RTL8129/39 Fast Ethernet");

	dev->base_addr = ioaddr;
	dev->irq = irq;

	/* Some data structures must be quadword aligned. */
	tp = kmalloc(sizeof(*tp), GFP_KERNEL | GFP_DMA);
	memset(tp, 0, sizeof(*tp));
	dev->priv = tp;

#ifdef MODULE
	tp->next_module = root_rtl8129_dev;
	root_rtl8129_dev = dev;
#endif

	tp->chip_id = chip_id;

	/* Find the connected MII xcvrs.
	   Doing this in open() would allow detecting external xcvrs later, but
	   takes too much time. */
	if (chip_id == 0x8129) {
		int phy, phy_idx;
		for (phy = 0, phy_idx = 0; phy < 32 && phy_idx < sizeof(tp->phys);
			 phy++) {
			int mii_status = mdio_read(ioaddr, phy, 1);

			if (mii_status != 0xffff  && mii_status != 0x0000) {
				tp->phys[phy_idx++] = phy;
				printk(KERN_INFO "%s: MII transceiver found at address %d.\n",
					   dev->name, phy);
			}
		}
		if (phy_idx == 0) {
			printk(KERN_INFO "%s: No MII transceivers found!  Assuming SYM "
				   "transceiver.\n",
				   dev->name);
			tp->phys[0] = -1;
		}
	} else {
			tp->phys[0] = -1;
	}

	/* Put the chip into low-power mode. */
	outb(0xC0, ioaddr + Cfg9346);
	outb(0x03, ioaddr + Config1);
	outb('H', ioaddr + HltClk);		/* 'R' would leave the clock running. */

	/* The lower four bits are the media type. */
	if (options > 0) {
		tp->full_duplex = (options & 16) ? 1 : 0;
		tp->default_port = options & 15;
		if (tp->default_port)
			tp->medialock = 1;
	}
#ifdef MODULE
	if (card_idx >= 0) {
		if (full_duplex[card_idx] >= 0)
			tp->full_duplex = full_duplex[card_idx];
	}
#endif

	/* The Rtl8129-specific entries in the device structure. */
	dev->open = &rtl8129_open;
	dev->hard_start_xmit = &rtl8129_start_xmit;
	dev->stop = &rtl8129_close;
	dev->get_stats = &rtl8129_get_stats;
	dev->set_multicast_list = &set_rx_mode;

	return dev;
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
	short ee_addr = ioaddr + Cfg9346;
	int read_cmd = location | EE_READ_CMD;

	outb(EE_ENB & ~EE_CS, ee_addr);
	outb(EE_ENB, ee_addr);

	/* Shift the read command bits out. */
	for (i = 10; i >= 0; i--) {
		short dataval = (read_cmd & (1 << i)) ? EE_DATA_WRITE : 0;
		outb(EE_ENB | dataval, ee_addr);
		eeprom_delay(100);
		outb(EE_ENB | dataval | EE_SHIFT_CLK, ee_addr);
		eeprom_delay(150);
		outb(EE_ENB | dataval, ee_addr);	/* Finish EEPROM a clock tick. */
		eeprom_delay(250);
	}
	outb(EE_ENB, ee_addr);

	for (i = 16; i > 0; i--) {
		outb(EE_ENB | EE_SHIFT_CLK, ee_addr);
		eeprom_delay(100);
		retval = (retval << 1) | ((inb(ee_addr) & EE_DATA_READ) ? 1 : 0);
		outb(EE_ENB, ee_addr);
		eeprom_delay(100);
	}

	/* Terminate the EEPROM access. */
	outb(~EE_CS, ee_addr);
	return retval;
}

/* MII serial management: mostly bogus for now. */
/* Read and write the MII management registers using software-generated
   serial MDIO protocol.  The maxium data clock rate is 2.5 Mhz. */
#define MDIO_DIR		0x80
#define MDIO_DATA_OUT	0x04
#define MDIO_DATA_IN	0x02
#define MDIO_CLK		0x01
#ifdef _LINUX_DELAY_H
#define mdio_delay()	udelay(1) /* Really 400ns. */
#else
#define mdio_delay()	__SLOW_DOWN_IO;
#endif

/* Syncronize the MII management interface by shifting 32 one bits out. */
static void mdio_sync(int ioaddr)
{
	int i;
	int mdio_addr = ioaddr + MII_SMI;

	for (i = 32; i >= 0; i--) {
		outb(MDIO_DIR | MDIO_DATA_OUT, mdio_addr);
		mdio_delay();
		outb(MDIO_DIR | MDIO_DATA_OUT | MDIO_CLK, mdio_addr);
		mdio_delay();
	}
	return;
}
static int mdio_read(int ioaddr, int phy_id, int location)
{
	int i;
	int read_cmd = (0xf6 << 10) | (phy_id << 5) | location;
	int retval = 0;
	int mdio_addr = ioaddr + MII_SMI;

	mdio_sync(ioaddr);
	/* Shift the read command bits out. */
	for (i = 15; i >= 0; i--) {
		int dataval =
		  (read_cmd & (1 << i)) ? MDIO_DATA_OUT : 0;

		outb(MDIO_DIR | dataval, mdio_addr);
		mdio_delay();
		outb(MDIO_DIR | dataval | MDIO_CLK, mdio_addr);
		mdio_delay();
	}

	/* Read the two transition, 16 data, and wire-idle bits. */
	for (i = 19; i > 0; i--) {
		outb(0, mdio_addr);
		mdio_delay();
		retval = (retval << 1) | ((inb(mdio_addr) & MDIO_DATA_IN) ? 1 : 0);
		outb(MDIO_CLK, mdio_addr);
		mdio_delay();
	}
	return (retval>>1) & 0xffff;
}

static int
rtl8129_open(struct device *dev)
{
	struct rtl8129_private *tp = (struct rtl8129_private *)dev->priv;
	int ioaddr = dev->base_addr;
	int i;
	int full_duplex = 0;

	/* Soft reset the chip. */
	outb(CmdReset, ioaddr + ChipCmd);

#ifdef SA_SHIRQ
	if (request_irq(dev->irq, &rtl8129_interrupt, SA_SHIRQ,
					"RealTek RTL8129/39 Fast Ethernet", dev)) {
		return -EAGAIN;
	}
#else
	if (irq2dev_map[dev->irq] != NULL
		|| (irq2dev_map[dev->irq] = dev) == NULL
		|| dev->irq == 0
		|| request_irq(dev->irq, &rtl8129_interrupt, 0, "RTL8129")) {
		return -EAGAIN;
	}
#endif

	MOD_INC_USE_COUNT;

	tp->tx_bufs = kmalloc(TX_BUF_SIZE * NUM_TX_DESC, GFP_KERNEL);
	tp->rx_ring = kmalloc(RX_BUF_LEN + 16, GFP_KERNEL);
	if (tp->tx_bufs == NULL ||  tp->rx_ring == NULL) {
		if (tp->tx_bufs)
			kfree(tp->tx_bufs);
		if (rtl8129_debug > 0)
			printk(KERN_ERR "%s: Couldn't allocate a %d byte receive ring.\n",
				   dev->name, RX_BUF_LEN);
		return -ENOMEM;
	}
	rtl8129_init_ring(dev);

#ifndef final_version
	/* Used to monitor rx ring overflow. */
	memset(tp->rx_ring + RX_BUF_LEN, 0xcc, 16);
#endif

	/* Check that the chip has finished the reset. */
	for (i = 1000; i > 0; i--)
		if ((inb(ioaddr + ChipCmd) & CmdReset) == 0)
			break;
#ifndef final_version
	if (rtl8129_debug > 2)
		printk(KERN_DEBUG"%s: reset finished with status %2.2x after %d loops.\n",
			   dev->name, inb(ioaddr + ChipCmd), 1000-i);
#endif

	for (i = 0; i < 6; i++)
		outb(dev->dev_addr[i], ioaddr + MAC0 + i);

	/* Must enable Tx/Rx before setting transfer thresholds! */
	outb(CmdRxEnb | CmdTxEnb, ioaddr + ChipCmd);
	outl((RX_FIFO_THRESH << 13) | (RX_BUF_LEN_IDX << 11) | (RX_DMA_BURST<<8),
		 ioaddr + RxConfig);
	outl((TX_DMA_BURST<<8)|0x03000000, ioaddr + TxConfig);

	full_duplex = tp->full_duplex;
	if (tp->phys[0] >= 0  ||  tp->chip_id == 0x8139) {
		u16 mii_reg5;
		if (tp->chip_id == 0x8139)
			mii_reg5 = inw(ioaddr + NWayLPAR);
		else
			mii_reg5 = mdio_read(ioaddr, tp->phys[0], 5);
		if (mii_reg5 == 0xffff)
			;					/* Not there */
		else if ((mii_reg5 & 0x0100) == 0x0100
				 || (mii_reg5 & 0x00C0) == 0x0040)
			full_duplex = 1;
		if (rtl8129_debug > 1)
			printk(KERN_INFO"%s: Setting %s%s-duplex based on"
				   " auto-negotiated partner ability %4.4x.\n", dev->name,
				   mii_reg5 == 0 ? "" :
				   (mii_reg5 & 0x0180) ? "100mbps " : "10mbps ",
				   full_duplex ? "full" : "half", mii_reg5);
	}

	outb(0xC0, ioaddr + Cfg9346);
	outb(full_duplex ? 0x60 : 0x20, ioaddr + Config1);
	outb(0x00, ioaddr + Cfg9346);

	outl(virt_to_bus(tp->rx_ring), ioaddr + RxBuf);

	/* Start the chip's Tx and Rx process. */
	outl(0, ioaddr + RxMissed);
	set_rx_mode(dev);

	outb(CmdRxEnb | CmdTxEnb, ioaddr + ChipCmd);
#ifndef final_version
	if (rtl8129_debug > 1)
		printk(KERN_DEBUG"%s:  In rtl8129_open() Tx/Rx Config %8.8x/%8.8x"
			   " Chip Config %2.2x/%2.2x.\n",
			   dev->name, inl(ioaddr + TxConfig), inl(ioaddr + RxConfig),
			   inb(ioaddr + Config0), inb(ioaddr + Config1));
#endif

	dev->tbusy = 0;
	dev->interrupt = 0;
	dev->start = 1;

	/* Enable all known interrupts by setting the interrupt mask. */
	outw(PCIErr | PCSTimeout | RxUnderrun | RxOverflow | RxFIFOOver
		| TxErr | TxOK | RxErr | RxOK, ioaddr + IntrMask);

	if (rtl8129_debug > 1)
		printk(KERN_DEBUG"%s: rtl8129_open() ioaddr %4.4x IRQ %d"
			   " GP Pins %2.2x %s-duplex.\n",
			   dev->name, ioaddr, dev->irq, inb(ioaddr + GPPinData),
			   full_duplex ? "full" : "half");

	/* Set the timer to switch to check for link beat and perhaps switch
	   to an alternate media type. */
	init_timer(&tp->timer);
	tp->timer.expires = RUN_AT((24*HZ)/10);			/* 2.4 sec. */
	tp->timer.data = (unsigned long)dev;
	tp->timer.function = &rtl8129_timer;				/* timer handler */
	add_timer(&tp->timer);

	return 0;
}

static void rtl8129_timer(unsigned long data)
{
	struct device *dev = (struct device *)data;
	struct rtl8129_private *tp = (struct rtl8129_private *)dev->priv;
	int ioaddr = dev->base_addr;
	int next_tick = 0;

	if (tp->chip_id == 0x8139) {
		u16 mii_reg5 = inw(ioaddr + NWayLPAR);
		if ((mii_reg5 & 0x0100) == 0x0100
			|| (mii_reg5 & 0x00C0) == 0x0040)
			if ( ! tp->full_duplex) {
				tp->full_duplex = 1;
				if (rtl8129_debug > 0)
					printk(KERN_INFO "%s: Switching to full-duplex based on "
						   "link partner ability of %4.4x.\n",
						   dev->name, mii_reg5);
				outb(0xC0, ioaddr + Cfg9346);
				outb(tp->full_duplex ? 0x60 : 0x20, ioaddr + Config1);
				outb(0x00, ioaddr + Cfg9346);
			}
	}
	if (rtl8129_debug > 2) {
		if (tp->chip_id == 0x8129)
			printk(KERN_DEBUG"%s: Media selection tick, GP pins %2.2x.\n",
				   dev->name, inb(ioaddr + GPPinData));
		else
			printk(KERN_DEBUG"%s: Media selection tick, Link partner %4.4x.\n",
				   dev->name, inw(ioaddr + NWayLPAR));
		printk(KERN_DEBUG"%s:  Other registers are IntMask %4.4x IntStatus %4.4x"
			   " RxStatus %4.4x.\n",
			   dev->name, inw(ioaddr + IntrMask), inw(ioaddr + IntrStatus),
			   inl(ioaddr + RxEarlyStatus));
		printk(KERN_DEBUG"%s:  Chip config %2.2x %2.2x.\n",
			   dev->name, inb(ioaddr + Config0), inb(ioaddr + Config1));
	}

	if (next_tick) {
		tp->timer.expires = RUN_AT(next_tick);
		add_timer(&tp->timer);
	}
}

static void rtl8129_tx_timeout(struct device *dev)
{
	struct rtl8129_private *tp = (struct rtl8129_private *)dev->priv;
	int ioaddr = dev->base_addr;
	int i;

	if (rtl8129_debug > 0)
		printk(KERN_WARNING "%s: Transmit timeout, status %2.2x %4.4x.\n",
			   dev->name, inb(ioaddr + ChipCmd), inw(ioaddr + IntrStatus));
	for (i = 0; i < NUM_TX_DESC; i++)
		printk(KERN_DEBUG"%s:  Tx descriptor %d is %8.8x.%s\n",
			   dev->name, i, inl(ioaddr + TxStat0 + i*4),
			   i == tp->dirty_tx % NUM_TX_DESC ? " (queue head)" : "");
	if (tp->chip_id == 0x8129) {
		int mii_reg;
		printk(KERN_DEBUG"%s: MII #%d registers are:", dev->name, tp->phys[0]);
		for (mii_reg = 0; mii_reg < 8; mii_reg++)
			printk(" %4.4x", mdio_read(ioaddr, tp->phys[0], mii_reg));
		printk(".\n");
	} else {
		printk(KERN_DEBUG"%s: MII status register is %4.4x.\n",
			   dev->name, inw(ioaddr + BMSR));
	}
	/* Restart the chip Tx process. */
	outb(CmdRxEnb | CmdTxEnb, ioaddr + ChipCmd);
	/* Continue from any transmit abort. */
	outl((TX_DMA_BURST<<8) || 0x03000001, ioaddr + TxConfig);

	dev->trans_start = jiffies;
	tp->stats.tx_errors++;
	return;
}


/* Initialize the Rx and Tx rings, along with various 'dev' bits. */
static void
rtl8129_init_ring(struct device *dev)
{
	struct rtl8129_private *tp = (struct rtl8129_private *)dev->priv;
	int i;

	tp->tx_full = 0;
	tp->cur_rx = tp->cur_tx = 0;
	tp->dirty_rx = tp->dirty_tx = 0;

	for (i = 0; i < NUM_TX_DESC; i++) {
		tp->tx_skbuff[i] = 0;
		tp->tx_buf[i] = &tp->tx_bufs[i*TX_BUF_SIZE];
	}
}

static int
rtl8129_start_xmit(struct sk_buff *skb, struct device *dev)
{
	struct rtl8129_private *tp = (struct rtl8129_private *)dev->priv;
	int ioaddr = dev->base_addr;
	int entry;

#ifndef final_version
	if (skb == NULL || skb->len <= 0) {
		printk(KERN_ERR"%s: Obsolete driver Tx request made: skbuff==NULL.\n",
			   dev->name);
#if 0
		dev_tint(dev);
#endif
		return 0;
	}
#endif

	/* Block a timer-based transmit from overlapping.  This could better be
	   done with atomic_swap(1, dev->tbusy), but set_bit() works as well. */
	if (test_and_set_bit(0, (void*)&dev->tbusy) != 0) {
		if (jiffies - dev->trans_start < TX_TIMEOUT)
			return 1;
		rtl8129_tx_timeout(dev);
		return 1;
	}

	/* Calculate the next Tx descriptor entry. */
	entry = tp->cur_tx % NUM_TX_DESC;

	tp->tx_skbuff[entry] = skb;
	if ((long)skb->data & 3) {			/* Must use alignment buffer. */
		memcpy(tp->tx_buf[entry], skb->data, skb->len);
		outl(virt_to_bus(tp->tx_buf[entry]), ioaddr + TxAddr0 + entry*4);
	} else
		outl(virt_to_bus(skb->data), ioaddr + TxAddr0 + entry*4);
	/* Note: the chip doesn't have auto-pad! */
	outl(((TX_FIFO_THRESH<<11) & 0x003f0000) |
		 (skb->len >= ETH_ZLEN ? skb->len : ETH_ZLEN),
		 ioaddr + TxStat0 + entry*4);

	if (++tp->cur_tx - tp->dirty_tx < NUM_TX_DESC) {/* Typical path */
		dev->tbusy = 0;
	} else {
		tp->tx_full = 1;
	}

	dev->trans_start = jiffies;
	if (rtl8129_debug > 4)
		printk(KERN_DEBUG"%s: Queued Tx packet at %p size %d to slot %d.\n",
			   dev->name, skb->data, skb->len, entry);

	return 0;
}

/* The interrupt handler does all of the Rx thread work and cleans up
   after the Tx thread. */
static void rtl8129_interrupt IRQ(int irq, void *dev_instance, struct pt_regs *regs)
{
#ifdef SA_SHIRQ
	struct device *dev = (struct device *)dev_instance;
#else
	struct device *dev = (struct device *)(irq2dev_map[irq]);
#endif
	struct rtl8129_private *tp;
	int ioaddr, boguscnt = max_interrupt_work;
	int status;

	if (dev == NULL) {
		printk (KERN_ERR"rtl8139_interrupt(): IRQ %d for unknown device.\n",
				irq);
		return;
	}

	ioaddr = dev->base_addr;
	tp = (struct rtl8129_private *)dev->priv;
	if (test_and_set_bit(0, (void*)&dev->interrupt)) {
		printk(KERN_ERR "%s: Re-entering the interrupt handler.\n", dev->name);
		return;
	}

	do {
		status = inw(ioaddr + IntrStatus);
		/* Acknowledge all of the current interrupt sources ASAP. */
		outw(status, ioaddr + IntrStatus);

		if (rtl8129_debug > 4)
			printk(KERN_DEBUG"%s: interrupt  status=%#4.4x new intstat=%#4.4x.\n",
				   dev->name, status, inw(ioaddr + IntrStatus));

		if ((status & (PCIErr|PCSTimeout|RxUnderrun|RxOverflow|RxFIFOOver
					   |TxErr|TxOK|RxErr|RxOK)) == 0)
			break;

		if (status & (RxOK|RxUnderrun|RxOverflow|RxFIFOOver))/* Rx interrupt */
			rtl8129_rx(dev);

		if (status & (TxOK | TxErr)) {
			unsigned int dirty_tx;

			for (dirty_tx = tp->dirty_tx; dirty_tx < tp->cur_tx; dirty_tx++) {
				int entry = dirty_tx % NUM_TX_DESC;
				int txstatus = inl(ioaddr + TxStat0 + entry*4);

				if ( ! (txstatus & TxHostOwns))
					break;			/* It still hasn't been Txed */

				/* Note: TxCarrierLost is always asserted at 100mbps. */
				if (txstatus & (TxOutOfWindow | TxAborted)) {
					/* There was an major error, log it. */
#ifndef final_version
					if (rtl8129_debug > 1)
						printk(KERN_NOTICE"%s: Transmit error, Tx status %8.8x.\n",
							   dev->name, txstatus);
#endif
					tp->stats.tx_errors++;
					if (txstatus&TxAborted) {
						tp->stats.tx_aborted_errors++;
						outl((TX_DMA_BURST<<8)|0x03000001, ioaddr + TxConfig);
					}
					if (txstatus&TxCarrierLost) tp->stats.tx_carrier_errors++;
					if (txstatus&TxOutOfWindow) tp->stats.tx_window_errors++;
#ifdef ETHER_STATS
					if ((txstatus & 0x0f000000) == 0x0f000000)
						tp->stats.collisions16++;
#endif
				} else {
#ifdef ETHER_STATS
					/* No count for tp->stats.tx_deferred */
#endif
					if (txstatus & TxUnderrun) {
						/* Todo: increase the Tx FIFO threshold. */
						tp->stats.tx_fifo_errors++;
					}
					tp->stats.collisions += (txstatus >> 24) & 15;
					tp->stats.tx_packets++;
				}

				/* Free the original skb. */
				dev_kfree_skb(tp->tx_skbuff[entry]);
				tp->tx_skbuff[entry] = 0;
			}

#ifndef final_version
			if (tp->cur_tx - dirty_tx > NUM_TX_DESC) {
				printk(KERN_ERR"%s: Out-of-sync dirty pointer, %d vs. %d, full=%d.\n",
					   dev->name, dirty_tx, tp->cur_tx, tp->tx_full);
				dirty_tx += NUM_TX_DESC;
			}
#endif

			if (tp->tx_full && dev->tbusy
				&& dirty_tx > tp->cur_tx - NUM_TX_DESC) {
				/* The ring is no longer full, clear tbusy. */
				tp->tx_full = 0;
				dev->tbusy = 0;
				mark_bh(NET_BH);
			}

			tp->dirty_tx = dirty_tx;
		}

		/* Check uncommon events with one test. */
		if (status & (PCIErr|PCSTimeout |RxUnderrun|RxOverflow|RxFIFOOver
					  |TxErr|RxErr)) {

#ifndef final_version
			if (rtl8129_debug > 0)
				printk(KERN_DEBUG"%s: Unusual error, status %4.4x.\n",
							   dev->name, status);
#endif

			/* Update the error count. */
			tp->stats.rx_missed_errors += inl(ioaddr + RxMissed);
			outl(0, ioaddr + RxMissed);

			if (status & (RxUnderrun | RxOverflow | RxErr | RxFIFOOver))
				tp->stats.rx_errors++;

			if (status & (PCSTimeout)) tp->stats.rx_length_errors++;
			if (status & (RxUnderrun|RxFIFOOver)) tp->stats.rx_fifo_errors++;
			if (status & RxOverflow) {
				tp->stats.rx_over_errors++;
				tp->cur_rx = inw(ioaddr + RxBufAddr) % RX_BUF_LEN;
				outw(tp->cur_rx - 16, ioaddr + RxBufPtr);
			}
			/* Error sources cleared above. */
		}
		if (--boguscnt < 0) {
			printk(KERN_WARNING"%s: Too much work at interrupt, "
				   "IntrStatus=0x%4.4x.\n",
				   dev->name, status);
			/* Clear all interrupt sources. */
			outw(0xffff, ioaddr + IntrStatus);
			break;
		}
	} while (1);

	if (rtl8129_debug > 3)
		printk(KERN_DEBUG"%s: exiting interrupt, intr_status=%#4.4x.\n",
			   dev->name, inl(ioaddr + IntrStatus));

#ifndef final_version
	/* Code that should never be run!  Perhaps remove after testing.. */
	{
		static int stopit = 10;
		if (dev->start == 0  &&  --stopit < 0) {
			printk(KERN_ERR"%s: Emergency stop, looping startup interrupt.\n",
				   dev->name);
			FREE_IRQ(irq, dev);
		}
	}
#endif

	dev->interrupt = 0;
	return;
}

/* Todo: The data sheet doesn't describe the Rx ring at all, so I'm winging
   it here until I have a chip to play with. 8/30/97 */
static int
rtl8129_rx(struct device *dev)
{
	struct rtl8129_private *tp = (struct rtl8129_private *)dev->priv;
	int ioaddr = dev->base_addr;
	unsigned char *rx_ring = tp->rx_ring;
	u16 cur_rx = tp->cur_rx;

	if (rtl8129_debug > 4)
		printk(KERN_DEBUG"%s: In rtl8129_rx(), current %4.4x BufAddr %4.4x,"
			   " free to %4.4x, Cmd %2.2x.\n",
			   dev->name, cur_rx, inw(ioaddr + RxBufAddr),
			   inw(ioaddr + RxBufPtr), inb(ioaddr + ChipCmd));

	while ((inb(ioaddr + ChipCmd) & 1) == 0) {
		u16 ring_offset = cur_rx % RX_BUF_LEN;
		u32 rx_status = *(u32*)(rx_ring + ring_offset);
		u16 rx_size = rx_status >> 16;

		if (rtl8129_debug > 4) {
			int i;
			printk(KERN_DEBUG"%s:  rtl8129_rx() status %4.4x, size %4.4x, cur %4.4x.\n",
				   dev->name, rx_status, rx_size, cur_rx);
			printk(KERN_DEBUG"%s: Frame contents ", dev->name);
			for (i = 0; i < 70; i++)
				printk(" %2.2x", rx_ring[ring_offset + i]);
			printk(".\n");
		}
		if (rx_status & RxTooLong) {
			if (rtl8129_debug > 0)
				printk(KERN_NOTICE"%s: Oversized Ethernet frame, status %4.4x!\n",
					   dev->name, rx_status);
			tp->stats.rx_length_errors++;
		} else if (rx_status &
				   (RxBadSymbol|RxRunt|RxTooLong|RxCRCErr|RxBadAlign)) {
			if (rtl8129_debug > 1)
				printk(KERN_DEBUG"%s: Ethernet frame had errors,"
					   " status %4.4x.\n", dev->name, rx_status);
			tp->stats.rx_errors++;
			if (rx_status & (RxBadSymbol|RxBadAlign))
				tp->stats.rx_frame_errors++;
			if (rx_status & (RxRunt|RxTooLong)) tp->stats.rx_length_errors++;
			if (rx_status & RxCRCErr) tp->stats.rx_crc_errors++;
		} else {
			/* Malloc up new buffer, compatible with net-2e. */
			/* Omit the four octet CRC from the length. */
			struct sk_buff *skb;

			skb = DEV_ALLOC_SKB(rx_size + 2);
			if (skb == NULL) {
				printk(KERN_WARNING"%s: Memory squeeze, deferring packet.\n",
					   dev->name);
				/* We should check that some rx space is free.
				   If not, free one and mark stats->rx_dropped++. */
				tp->stats.rx_dropped++;
				break;
			}
			skb->dev = dev;
			skb_reserve(skb, 2);	/* 16 byte align the IP fields. */
			if (ring_offset+rx_size+4 > RX_BUF_LEN) {
				int semi_count = RX_BUF_LEN - ring_offset - 4;
				memcpy(skb_put(skb, semi_count), &rx_ring[ring_offset + 4],
					   semi_count);
				memcpy(skb_put(skb, rx_size-semi_count), rx_ring,
					   rx_size-semi_count);
				if (rtl8129_debug > 4) {
					int i;
					printk(KERN_DEBUG"%s:  Frame wrap @%d", dev->name, semi_count);
					for (i = 0; i < 16; i++)
						printk(" %2.2x", rx_ring[i]);
					printk(".\n");
					memset(rx_ring, 0xcc, 16);
				}
			} else
				memcpy(skb_put(skb, rx_size), &rx_ring[ring_offset + 4],
					   rx_size);
#if LINUX_VERSION_CODE >= 0x10300
			skb->protocol = eth_type_trans(skb, dev);
#else
			skb->len = rx_size;
#endif
			netif_rx(skb);
			tp->stats.rx_packets++;
		}

		cur_rx += rx_size + 4;
		cur_rx = (cur_rx + 3) & ~3;
		outw(cur_rx - 16, ioaddr + RxBufPtr);
	}
	if (rtl8129_debug > 4)
		printk(KERN_DEBUG"%s: Done rtl8129_rx(), current %4.4x BufAddr %4.4x,"
			   " free to %4.4x, Cmd %2.2x.\n",
			   dev->name, cur_rx, inw(ioaddr + RxBufAddr),
			   inw(ioaddr + RxBufPtr), inb(ioaddr + ChipCmd));
	tp->cur_rx = cur_rx;
	return 0;
}

static int
rtl8129_close(struct device *dev)
{
	int ioaddr = dev->base_addr;
	struct rtl8129_private *tp = (struct rtl8129_private *)dev->priv;
	int i;

	dev->start = 0;
	dev->tbusy = 1;

	if (rtl8129_debug > 1)
		printk(KERN_DEBUG"%s: Shutting down ethercard, status was 0x%4.4x.\n",
			   dev->name, inw(ioaddr + IntrStatus));

	/* Disable interrupts by clearing the interrupt mask. */
	outw(0x0000, ioaddr + IntrMask);

	/* Stop the chip's Tx and Rx DMA processes. */
	outb(0x00, ioaddr + ChipCmd);

	/* Update the error counts. */
	tp->stats.rx_missed_errors += inl(ioaddr + RxMissed);
	outl(0, ioaddr + RxMissed);

	del_timer(&tp->timer);

#ifdef SA_SHIRQ
	free_irq(dev->irq, dev);
#else
	free_irq(dev->irq);
	irq2dev_map[dev->irq] = 0;
#endif

#ifndef final_version
	/* Used to monitor rx ring overflow. */
	for (i = 0; i < 16; i++)
		if (tp->rx_ring[RX_BUF_LEN+i] != 0xcc) {
			printk(KERN_WARNING"%s: Rx ring overflowed!  Values are ",
				   dev->name);
			for (i = 0; i < 16; i++)
				printk(" %2.2x", tp->rx_ring[RX_BUF_LEN + i]);
			printk(".\n");
			break;
		}
#endif

	for (i = 0; i < NUM_TX_DESC; i++) {
		if (tp->tx_skbuff[i])
			dev_kfree_skb(tp->tx_skbuff[i]);
		tp->tx_skbuff[i] = 0;
	}
	kfree(tp->rx_ring);
	kfree(tp->tx_bufs);

	/* Green! Put the chip in low-power mode. */
	outb(0xC0, ioaddr + Cfg9346);
	outb(0x03, ioaddr + Config1);
	outb('H', ioaddr + HltClk);		/* 'R' would leave the clock running. */

	MOD_DEC_USE_COUNT;

	return 0;
}

static struct enet_statistics *
rtl8129_get_stats(struct device *dev)
{
	struct rtl8129_private *tp = (struct rtl8129_private *)dev->priv;
	int ioaddr = dev->base_addr;

	if (dev->start) {
		tp->stats.rx_missed_errors += inl(ioaddr + RxMissed);
		outl(0, ioaddr + RxMissed);
	}

	return &tp->stats;
}

/* Set or clear the multicast filter for this adaptor.
   Note that we only use exclusion around actually queueing the
   new frame, not around filling tp->setup_frame.  This is non-deterministic
   when re-entered but still correct. */

/* The little-endian AUTODIN II ethernet CRC calculation.
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
	struct rtl8129_private *tp = (struct rtl8129_private *)dev->priv;
	unsigned char mc_filter[8];		 /* Multicast hash filter */
	int i;

	if (dev->flags & IFF_PROMISC) {			/* Set promiscuous. */
		/* Unconditionally log net taps. */
		printk(KERN_NOTICE"%s: Promiscuous mode enabled.\n", dev->name);
		memset(mc_filter, 0xff, sizeof(mc_filter));
		outb(0x0F, ioaddr + RxConfig);
	} else if ((dev->mc_count > 1000)  ||  (dev->flags & IFF_ALLMULTI)) {
		/* Too many to filter perfectly -- accept all multicasts. */
		memset(mc_filter, 0xff, sizeof(mc_filter));
		outb(0x0E, ioaddr + RxConfig);
	} else if (dev->mc_count == 0) {
		outb(0x0A, ioaddr + RxConfig);
		return;
	} else {
		struct dev_mc_list *mclist;

		memset(mc_filter, 0, sizeof(mc_filter));
		for (i = 0, mclist = dev->mc_list; mclist && i < dev->mc_count;
			 i++, mclist = mclist->next)
			set_bit(ether_crc_le(ETH_ALEN, mclist->dmi_addr) & 0x3f,
					mc_filter);
	}
	/* ToDo: perhaps we need to stop the Tx and Rx process here? */
	if (memcmp(mc_filter, tp->mc_filter, sizeof(mc_filter))) {
		for (i = 0; i < 2; i++)
			outl(((u32 *)mc_filter)[i], ioaddr + MAR0 + i*4);
		memcpy(tp->mc_filter, mc_filter, sizeof(mc_filter));
	}
	if (rtl8129_debug > 3)
		printk(KERN_DEBUG"%s:   set_rx_mode(%4.4x) done -- Rx config %8.8x.\n",
			   dev->name, dev->flags, inl(ioaddr + RxConfig));
	return;
}

#ifdef MODULE

/* An additional parameter that may be passed in... */
static int debug = -1;

int
init_module(void)
{
	int cards_found;

	if (debug >= 0)
		rtl8129_debug = debug;

	root_rtl8129_dev = NULL;
	cards_found = rtl8139_probe(0);

	return cards_found ? 0 : -ENODEV;
}

void
cleanup_module(void)
{
	struct device *next_dev;

	/* No need to check MOD_IN_USE, as sys_delete_module() checks. */
	while (root_rtl8129_dev) {
		next_dev = ((struct rtl8129_private *)root_rtl8129_dev->priv)->next_module;
		unregister_netdev(root_rtl8129_dev);
		release_region(root_rtl8129_dev->base_addr, RTL8129_TOTAL_SIZE);
		kfree(root_rtl8129_dev);
		root_rtl8129_dev = next_dev;
	}
}

#endif  /* MODULE */

/*
 * Local variables:
 *  compile-command: "gcc -DMODVERSIONS  -DMODULE -D__KERNEL__ -Wall -Wstrict-prototypes -O6 -c rtl8139.c"
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 * End:
 */
