/* epic100.c: A SMC 83c170 EPIC/100 fast ethernet driver for Linux. */
/*
   NOTICE: THIS IS THE ALPHA TEST VERSION!
	Written 1997 by Donald Becker.

	This software may be used and distributed according to the terms
	of the GNU Public License, incorporated herein by reference.
    All other rights reserved.

	This driver is for the SMC EtherPower II 9432 PCI ethernet adapter based on
	the SMC83c170.

	The author may be reached as becker@CESDIS.gsfc.nasa.gov, or C/O
	Center of Excellence in Space Data and Information Sciences
	   Code 930.5, Goddard Space Flight Center, Greenbelt MD 20771

	Support and updates available at
	http://cesdis.gsfc.nasa.gov/linux/drivers/epic100.html
*/

static const char *version =
"epic100.c:v0.10 10/14/97 Donald Becker http://cesdis.gsfc.nasa.gov/linux/drivers/epic100.html\n";

/* A few user-configurable values. */

/* Keep the ring sizes a power of two for efficiency.
   Making the Tx ring too large decreases the effectiveness of channel
   bonding and packet priority.
   There are no ill effects from too-large receive rings. */
#define TX_RING_SIZE	16
#define RX_RING_SIZE	32

/* Set the copy breakpoint for the copy-only-tiny-frames scheme.
   Setting to > 1518 effectively disables this feature. */
static const int rx_copybreak = 200;

/* Maximum events (Rx packets, etc.) to handle at each interrupt. */
static int max_interrupt_work = 10;

/* Operational parameters that usually are not changed. */
/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT  ((2000*HZ)/1000)

#define PKT_BUF_SZ		1536			/* Size of each temporary Rx buffer.*/

/* Bytes transferred to chip before transmission starts. */
#define TX_FIFO_THRESH 128		/* Rounded down to 4 byte units. */
#define RX_FIFO_THRESH 1		/* 0-3, 0==32, 64,96, or 3==128 bytes  */

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
#else
#ifdef MODULE
MODULE_AUTHOR("Donald Becker <becker@cesdis.gsfc.nasa.gov>");
MODULE_DESCRIPTION("SMC 82c170 EPIC series Ethernet driver");
MODULE_PARM(debug, "i");
MODULE_PARM(options, "1-" __MODULE_STRING(8) "i");
MODULE_PARM(full_duplex, "1-" __MODULE_STRING(8) "i");
MODULE_PARM(rx_copybreak, "i");
MODULE_PARM(max_interrupt_work, "i");
#endif
#endif

/* The I/O extent. */
#define EPIC_TOTAL_SIZE 0x100

#ifdef HAVE_DEVLIST
struct netdev_entry epic100_drv =
{"Epic100", epic100_pci_probe, EPIC_TOTAL_SIZE, NULL};
#endif

static int epic_debug = 1;

/*
				Theory of Operation

I. Board Compatibility

This device driver is designed for the SMC "EPCI/100", the SMC
single-chip ethernet controllers for PCI.  This chip is used on
the SMC EtherPower II boards.


II. Board-specific settings

PCI bus devices are configured by the system at boot time, so no jumpers
need to be set on the board.  The system BIOS will assign the
PCI INTA signal to a (preferably otherwise unused) system IRQ line.
Note: Kernel versions earlier than 1.3.73 do not support shared PCI
interrupt lines.

III. Driver operation

IIIa. Ring buffers

IVb. References

http://www.smc.com/components/catalog/smc83c170.html
http://cesdis.gsfc.nasa.gov/linux/misc/NWay.html
http://www.national.com/pf/DP/DP83840.html

IVc. Errata

*/

#ifndef PCI_VENDOR_ID_SMC
#define PCI_VENDOR_ID_SMC			0x10B8
#endif
#ifndef PCI_DEVICE_ID_SMC_EPIC100
#define PCI_DEVICE_ID_SMC_EPIC100	0x0005
#endif

/* The rest of these values should never change. */
/* Offsets to registers, using the (ugh) SMC names. */
enum epic_registers {
  COMMAND=0, INTSTAT=4, INTMASK=8, GENCTL=0x0C, NVCTL=0x10, EECTL=0x14,
  TEST1=0x1C, CRCCNT=0x20, ALICNT=0x24, MPCNT=0x28,	/* Rx error counters. */
  MIICtrl=0x30, MIIData=0x34, MIICfg=0x38,
  LAN0=64,						/* MAC address. */
  MC0=80,						/* Multicast filter table. */
  RxCtrl=96, TxCtrl=112, TxSTAT=0x74,
  PRxCDAR=0x84, RxSTAT=0xA4, EarlyRx=0xB0, PTxCDAR=0xC4, TxThresh=0xDC,
};

/* Interrupt register bits, using my own meaningful names. */
enum IntrStatus {
  TxIdle=0x40000, RxIdle=0x20000,
  CntFull=0x0200, TxUnderrun=0x0100,
  TxEmpty=0x0080, TxDone=0x0020, RxError=0x0010,
  RxOverflow=0x0008, RxFull=0x0004, RxHeader=0x0002, RxDone=0x0001,
};

/* The EPIC100 Rx and Tx buffer descriptors. */

struct epic_tx_desc {
	s16 status;
	u16 txlength;
	u32 bufaddr;
	u16 buflength;
	u16 control;
    u32 next;
};

struct epic_rx_desc {
	s16 status;
	u16 rxlength;
	u32 bufaddr;
	u32 buflength;
    u32 next;
};

struct epic_private {
	char devname[8];			/* Used only for kernel debugging. */
	const char *product_name;
	struct device *next_module;
	struct epic_rx_desc rx_ring[RX_RING_SIZE];
	struct epic_tx_desc tx_ring[TX_RING_SIZE];
	/* The saved address of a sent-in-place packet/buffer, for skfree(). */
	struct sk_buff* tx_skbuff[TX_RING_SIZE];
	/* The addresses of receive-in-place skbuffs. */
	struct sk_buff* rx_skbuff[RX_RING_SIZE];
	int chip_id;
	int revision;
	struct enet_statistics stats;
	struct timer_list timer;	/* Media selection timer. */
	unsigned int cur_rx, cur_tx;		/* The next free ring entry */
	unsigned int dirty_rx, dirty_tx;	/* The ring entries to be free()ed. */
	unsigned char mc_filter[8];
	signed char phys[4];				/* MII device addresses. */
	unsigned int tx_full:1;				/* The Tx queue is full. */
	unsigned int full_duplex:1;			/* Full-duplex operation requested. */
	unsigned int default_port:4;		/* Last dev->if_port value. */
	unsigned int media2:4;				/* Secondary monitored media port. */
	unsigned int medialock:1;			/* Don't sense media type. */
	unsigned int mediasense:1;			/* Media sensing in progress. */
	int pad0, pad1;						/* Used for 8-byte alignment */
};

static int full_duplex[] = {-1, -1, -1, -1, -1, -1, -1, -1};
#ifdef MODULE
/* Used to pass the full-duplex flag, etc. */
static int options[] = {-1, -1, -1, -1, -1, -1, -1, -1};
#endif

static struct device *epic100_probe1(struct device *dev, int ioaddr, int irq,
									 int chip_id, int options, int card_idx);
static int epic_open(struct device *dev);
static int read_eeprom(int ioaddr, int location);
static int mii_read(int ioaddr, int phy_id, int location);
static void epic_timer(unsigned long data);
static void epic_tx_timeout(struct device *dev);
static void epic_init_ring(struct device *dev);
static int epic_start_xmit(struct sk_buff *skb, struct device *dev);
static int epic_rx(struct device *dev);
static void epic_interrupt(int irq, void *dev_instance, struct pt_regs *regs);
static int epic_close(struct device *dev);
static struct enet_statistics *epic_get_stats(struct device *dev);
#ifdef NEW_MULTICAST
static void set_rx_mode(struct device *dev);
#else
static void set_rx_mode(struct device *dev, int num_addrs, void *addrs);
#endif



#ifdef MODULE
/* A list of all installed EPIC devices, for removing the driver module. */
static struct device *root_epic_dev = NULL;
#endif

int epic100_probe(struct device *dev)
{
	int cards_found = 0;
	static int pci_index = 0;	/* Static, for multiple probe calls. */

	/* Ideally we would detect all network cards in slot order.  That would
	   be best done a central PCI probe dispatch, which wouldn't work
	   well with the current structure.  So instead we detect just the
	   Epic cards in slot order. */

	if (pcibios_present()) {
		unsigned char pci_bus, pci_device_fn;

		for (;pci_index < 0xff; pci_index++) {
			unsigned char pci_irq_line, pci_latency;
			unsigned short pci_command, vendor, device;
			unsigned int pci_ioaddr, chip_idx = 0;

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
			if (vendor != PCI_VENDOR_ID_SMC)
				continue;

			pcibios_read_config_word(pci_bus, pci_device_fn,
									 PCI_DEVICE_ID, &device);
			pcibios_read_config_byte(pci_bus, pci_device_fn,
									 PCI_INTERRUPT_LINE, &pci_irq_line);
			pcibios_read_config_dword(pci_bus, pci_device_fn,
									  PCI_BASE_ADDRESS_0, &pci_ioaddr);
			/* Remove I/O space marker in bit 0. */
			pci_ioaddr &= ~3;

			if (device != PCI_DEVICE_ID_SMC_EPIC100) {
				printk("Unknown SMC PCI ethernet chip type %4.4x detected:"
					   " not configured.\n", device);
				continue;
			}
			if (epic_debug > 2)
				printk("Found SMC PCI EPIC/100 at I/O %#x, IRQ %d.\n",
					   pci_ioaddr, pci_irq_line);

			if (check_region(pci_ioaddr, EPIC_TOTAL_SIZE))
				continue;

#ifdef MODULE
			dev = epic100_probe1(dev, pci_ioaddr, pci_irq_line, chip_idx,
						 options[cards_found], cards_found);
#else
			dev = epic100_probe1(dev, pci_ioaddr, pci_irq_line, chip_idx,
						 dev ? dev->mem_start : 0, -1);
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
			  if (pci_latency < 10) {
				printk("  PCI latency timer (CFLT) is unreasonably low at %d."
					   "  Setting to 255 clocks.\n", pci_latency);
				pcibios_write_config_byte(pci_bus, pci_device_fn,
										  PCI_LATENCY_TIMER, 255);
			  } else if (epic_debug > 1)
				printk("  PCI latency timer (CFLT) is %#x.\n", pci_latency);
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

static struct device *epic100_probe1(struct device *dev, int ioaddr, int irq,
								   int chip_id, int options, int card_idx)
{
	static int did_version = 0;			/* Already printed version info. */
	struct epic_private *tp;
	int i;

	if (epic_debug > 0  &&  did_version++ == 0)
		printk(version);

	dev = init_etherdev(dev, 0);

	printk("%s: SMC EPIC/100 at %#3x, IRQ %d, ", dev->name, ioaddr, irq);

	/* Bring the chip out of low-power mode. */
	outl(0x0200, ioaddr + GENCTL);
	/* Magic?!  If we don't set this bit the MII interface won't work. */
	outl(0x0008, ioaddr + TEST1);

	/* This could also be read from the EEPROM. */
	for (i = 0; i < 3; i++)
		((u16 *)dev->dev_addr)[i] = inw(ioaddr + LAN0 + i*4);

	for (i = 0; i < 5; i++)
		printk("%2.2x:", dev->dev_addr[i]);
	printk("%2.2x.\n", dev->dev_addr[i]);

	if (epic_debug > 1) {
	  printk("%s: EEPROM contents\n", dev->name);
	  for (i = 0; i < 64; i++)
		printk(" %4.4x%s", read_eeprom(ioaddr, i), i % 16 == 15 ? "\n" : "");
	}

	/* We do a request_region() to register /proc/ioports info. */
	request_region(ioaddr, EPIC_TOTAL_SIZE, "SMC EPIC/100");

	dev->base_addr = ioaddr;
	dev->irq = irq;

	/* The data structures must be quadword aligned. */
	tp = kmalloc(sizeof(*tp), GFP_KERNEL | GFP_DMA);
	memset(tp, 0, sizeof(*tp));
	dev->priv = tp;

#ifdef MODULE
	tp->next_module = root_epic_dev;
	root_epic_dev = dev;
#endif

	tp->chip_id = chip_id;

	/* Find the connected MII xcvrs.
	   Doing this in open() would allow detecting external xcvrs later, but
	   takes too much time. */
	{
		int phy, phy_idx;
		for (phy = 0, phy_idx = 0; phy < 32 && phy_idx < sizeof(tp->phys);
			 phy++) {
			int mii_status = mii_read(ioaddr, phy, 0);
			if (mii_status != 0xffff  && mii_status != 0x0000) {
				tp->phys[phy_idx++] = phy;
				printk("%s: MII transceiver found at address %d.\n",
					   dev->name, phy);
			}
		}
		if (phy_idx == 0) {
			printk("%s: ***WARNING***: No MII transceiver found!\n",
				   dev->name);
			/* Use the known PHY address of the EPII. */
			tp->phys[0] = 3;
		}
	}

	/* Leave the chip in low-power mode. */
	outl(0x0008, ioaddr + GENCTL);

	/* The lower four bits are the media type. */
	if (options > 0) {
		tp->full_duplex = (options & 16) ? 1 : 0;
		tp->default_port = options & 15;
		if (tp->default_port)
		  tp->medialock = 1;
	}
	if (card_idx >= 0) {
		if (full_duplex[card_idx] >= 0)
			tp->full_duplex = full_duplex[card_idx];
	}

	/* The Epic-specific entries in the device structure. */
	dev->open = &epic_open;
	dev->hard_start_xmit = &epic_start_xmit;
	dev->stop = &epic_close;
	dev->get_stats = &epic_get_stats;
	dev->set_multicast_list = &set_rx_mode;

	return dev;
}

/* Serial EEPROM section. */

/*  EEPROM_Ctrl bits. */
#define EE_SHIFT_CLK	0x04	/* EEPROM shift clock. */
#define EE_CS			0x02	/* EEPROM chip select. */
#define EE_DATA_WRITE	0x08	/* EEPROM chip data in. */
#define EE_WRITE_0		0x01
#define EE_WRITE_1		0x09
#define EE_DATA_READ	0x10	/* EEPROM chip data out. */
#define EE_ENB			(0x0001 | EE_CS)

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
	int retval = 0;
	int ee_addr = ioaddr + EECTL;
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

#define MII_READOP		1
#define MII_WRITEOP		2
static int mii_read(int ioaddr, int phy_id, int location)
{
	int i;

	outl((phy_id << 9) | (location << 4) | MII_READOP, ioaddr + MIICtrl);
	/* Typical operation takes < 50 ticks. */
	for (i = 4000; i > 0; i--)
		if ((inl(ioaddr + MIICtrl) & MII_READOP) == 0)
			break;
	return inw(ioaddr + MIIData);
}


static int
epic_open(struct device *dev)
{
	struct epic_private *tp = (struct epic_private *)dev->priv;
	int ioaddr = dev->base_addr;
	int i;
	int mii_reg5;
	int full_duplex = 0;

	/* Soft reset the chip. */
	outl(0x0001, ioaddr + GENCTL);

#ifdef SA_SHIRQ
	if (request_irq(dev->irq, &epic_interrupt, SA_SHIRQ,
					"SMC EPIC/100", dev)) {
		return -EAGAIN;
	}
#else
	if (irq2dev_map[dev->irq] != NULL
		|| (irq2dev_map[dev->irq] = dev) == NULL
		|| dev->irq == 0
		|| request_irq(dev->irq, &epic_interrupt, 0, "SMC EPIC/100")) {
		return -EAGAIN;
	}
#endif

	MOD_INC_USE_COUNT;

	epic_init_ring(dev);

	/* This next line by Ken Yamaguchi.. ?? */
	outl(0x8, ioaddr + 0x1c);

	/* Pull the chip out of low-power mode, enable interrupts, and set for PCI read multiple. */
	outl(0x0412 | (RX_FIFO_THRESH<<8), ioaddr + GENCTL);

	for (i = 0; i < 3; i++)
		outl(((u16*)dev->dev_addr)[i], ioaddr + LAN0 + i*4);

	outl(TX_FIFO_THRESH, ioaddr + TxThresh);
	full_duplex = tp->full_duplex;

	mii_reg5 = mii_read(ioaddr, tp->phys[0], 5);
	if (mii_reg5 != 0xffff && (mii_reg5 & 0x0100)) {
		full_duplex = 1;
		if (epic_debug > 1)
			printk("%s: Setting %s-duplex based on MII xcvr %d"
				   " register read of %4.4x.\n", dev->name,
				   full_duplex ? "full" : "half", tp->phys[0],
				   mii_read(ioaddr, tp->phys[0], 5));
	}

	outl(full_duplex ? 0x7F : 0x79, ioaddr + TxCtrl);
	outl(virt_to_bus(tp->rx_ring), ioaddr + PRxCDAR);
	outl(virt_to_bus(tp->tx_ring), ioaddr + PTxCDAR);

	/* Start the chip's Rx process. */
	set_rx_mode(dev);
	outl(0x000A, ioaddr + COMMAND);

	dev->tbusy = 0;
	dev->interrupt = 0;
	dev->start = 1;

	/* Enable interrupts by setting the interrupt mask. */
	outl(CntFull | TxUnderrun | TxDone
		 | RxError | RxOverflow | RxFull | RxHeader | RxDone,
		 ioaddr + INTMASK);

	if (epic_debug > 1)
		printk("%s: epic_open() ioaddr %4.4x IRQ %d status %4.4x %s-duplex.\n",
			   dev->name, ioaddr, dev->irq, inl(ioaddr + GENCTL),
			   full_duplex ? "full" : "half");

	/* Set the timer to switch to check for link beat and perhaps switch
	   to an alternate media type. */
	init_timer(&tp->timer);
	tp->timer.expires = RUN_AT((24*HZ)/10);			/* 2.4 sec. */
	tp->timer.data = (unsigned long)dev;
	tp->timer.function = &epic_timer;				/* timer handler */
	add_timer(&tp->timer);

	return 0;
}

static void epic_timer(unsigned long data)
{
	struct device *dev = (struct device *)data;
	struct epic_private *tp = (struct epic_private *)dev->priv;
	int ioaddr = dev->base_addr;
	int next_tick = 0;

	if (epic_debug > 3) {
		printk("%s: Media selection tick, Tx status %8.8x.\n",
			   dev->name, inl(ioaddr + TxSTAT));
		printk("%s: Other registers are IntMask %4.4x IntStatus %4.4x RxStatus"
			   " %4.4x.\n",
			   dev->name, inl(ioaddr + INTMASK), inl(ioaddr + INTSTAT),
			   inl(ioaddr + RxSTAT));
	}

	if (next_tick) {
		tp->timer.expires = RUN_AT(next_tick);
		add_timer(&tp->timer);
	}
}

static void epic_tx_timeout(struct device *dev)
{
	struct epic_private *tp = (struct epic_private *)dev->priv;
	int ioaddr = dev->base_addr;

	if (epic_debug > 0) {
		printk("%s: Transmit timeout using MII device, Tx status %4.4x.\n",
			   dev->name, inw(ioaddr + TxSTAT));
		if (epic_debug > 1) {
			printk("%s: Tx indices: dirty_tx %d, cur_tx %d.\n",
			 dev->name, tp->dirty_tx, tp->cur_tx);
		}
	}
	/* Perhaps stop and restart the chip's Tx processes . */
	/* Trigger a transmit demand. */
	outl(0x0004, dev->base_addr + COMMAND);

	dev->trans_start = jiffies;
	tp->stats.tx_errors++;
	return;
}

/* Initialize the Rx and Tx rings, along with various 'dev' bits. */
static void
epic_init_ring(struct device *dev)
{
	struct epic_private *tp = (struct epic_private *)dev->priv;
	int i;

	tp->tx_full = 0;
	tp->cur_rx = tp->cur_tx = 0;
	tp->dirty_rx = tp->dirty_tx = 0;

	for (i = 0; i < RX_RING_SIZE; i++) {
		tp->rx_ring[i].status = 0x8000;		/* Owned by Epic chip */
		tp->rx_ring[i].buflength = PKT_BUF_SZ;
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
			skb_reserve(skb, 2); /* Align IP on 16 byte boundaries */
			tp->rx_ring[i].bufaddr = virt_to_bus(skb->tail);
#else
			tp->rx_ring[i].bufaddr = virt_to_bus(skb->data);
#endif
		}
		tp->rx_ring[i].next = virt_to_bus(&tp->rx_ring[i+1]);
	}
	/* Mark the last entry as wrapping the ring. */
	tp->rx_ring[i-1].next = virt_to_bus(&tp->rx_ring[0]);

	/* The Tx buffer descriptor is filled in as needed, but we
	   do need to clear the ownership bit. */
	for (i = 0; i < TX_RING_SIZE; i++) {
		tp->tx_skbuff[i] = 0;
		tp->tx_ring[i].status = 0x0000;
		tp->tx_ring[i].next = virt_to_bus(&tp->tx_ring[i+1]);
	}
	tp->tx_ring[i-1].next = virt_to_bus(&tp->tx_ring[0]);
}

static int
epic_start_xmit(struct sk_buff *skb, struct device *dev)
{
	struct epic_private *tp = (struct epic_private *)dev->priv;
	int entry;
	u32 flag;

	/* Block a timer-based transmit from overlapping.  This could better be
	   done with atomic_swap(1, dev->tbusy), but set_bit() works as well. */
	if (test_and_set_bit(0, (void*)&dev->tbusy) != 0) {
		if (jiffies - dev->trans_start < TX_TIMEOUT)
			return 1;
		epic_tx_timeout(dev);
		return 1;
	}

	/* Caution: the write order is important here, set the base address
	   with the "ownership" bits last. */

	/* Calculate the next Tx descriptor entry. */
	entry = tp->cur_tx % TX_RING_SIZE;

	tp->tx_skbuff[entry] = skb;
	tp->tx_ring[entry].txlength = (skb->len >= ETH_ZLEN ? skb->len : ETH_ZLEN);
	tp->tx_ring[entry].bufaddr = virt_to_bus(skb->data);
	tp->tx_ring[entry].buflength = skb->len;

	if (tp->cur_tx - tp->dirty_tx < TX_RING_SIZE/2) {/* Typical path */
	  flag = 0x10; /* No interrupt */
	  dev->tbusy = 0;
	} else if (tp->cur_tx - tp->dirty_tx == TX_RING_SIZE/2) {
	  flag = 0x14; /* Tx-done intr. */
	  dev->tbusy = 0;
	} else if (tp->cur_tx - tp->dirty_tx < TX_RING_SIZE - 2) {
	  flag = 0x10; /* No Tx-done intr. */
	  dev->tbusy = 0;
	} else {
	  /* Leave room for two additional entries. */
	  flag = 0x14; /* Tx-done intr. */
	  tp->tx_full = 1;
	}

	tp->tx_ring[entry].control = flag;
	tp->tx_ring[entry].status = 0x8000;	/* Pass ownership to the chip. */
	tp->cur_tx++;
	/* Trigger an immediate transmit demand. */
	outl(0x0004, dev->base_addr + COMMAND);

	dev->trans_start = jiffies;
	if (epic_debug > 4)
		printk("%s: Queued Tx packet size %d to slot %d, "
			   "flag %2.2x Tx status %8.8x.\n",
			   dev->name, (int)skb->len, entry, flag,
			   inl(dev->base_addr + TxSTAT));

	return 0;
}

/* The interrupt handler does all of the Rx thread work and cleans up
   after the Tx thread. */
static void epic_interrupt IRQ(int irq, void *dev_instance, struct pt_regs *regs)
{
#ifdef SA_SHIRQ
	struct device *dev = (struct device *)dev_instance;
#else
	struct device *dev = (struct device *)(irq2dev_map[irq]);
#endif
	struct epic_private *lp;
	int status, ioaddr, boguscnt = max_interrupt_work;

	if (dev == NULL) {
		printk ("epic_interrupt(): irq %d for unknown device.\n", irq);
		return;
	}

	ioaddr = dev->base_addr;
	lp = (struct epic_private *)dev->priv;
	if (dev->interrupt)
		printk("%s: Re-entering the interrupt handler.\n", dev->name);

	dev->interrupt = 1;

	do {
		status = inl(ioaddr + INTSTAT);
		/* Acknowledge all of the current interrupt sources ASAP. */
		outl(status & 0x00007fff, ioaddr + INTSTAT);

		if (epic_debug > 4)
			printk("%s: interrupt  interrupt=%#8.8x new intstat=%#8.8x.\n",
				   dev->name, status, inl(ioaddr + INTSTAT));

		if ((status & (RxDone | TxEmpty | TxDone)) == 0)
			break;

		if (status & RxDone)			/* Rx interrupt */
			epic_rx(dev);

		if (status & (TxEmpty | TxDone)) {
			int dirty_tx;

			for (dirty_tx = lp->dirty_tx; dirty_tx < lp->cur_tx; dirty_tx++) {
				int entry = dirty_tx % TX_RING_SIZE;
				int txstatus = lp->tx_ring[entry].status;

				if (txstatus < 0)
					break;			/* It still hasn't been Txed */

				if ( ! (txstatus & 0x0001)) {
					/* There was an major error, log it. */
#ifndef final_version
					if (epic_debug > 1)
						printk("%s: Transmit error, Tx status %8.8x.\n",
							   dev->name, txstatus);
#endif
					lp->stats.tx_errors++;
					if (txstatus & 0x1050) lp->stats.tx_aborted_errors++;
					if (txstatus & 0x0008) lp->stats.tx_carrier_errors++;
					if (txstatus & 0x0040) lp->stats.tx_window_errors++;
					if (txstatus & 0x0010) lp->stats.tx_fifo_errors++;
#ifdef ETHER_STATS
					if (txstatus & 0x1000) lp->stats.collisions16++;
#endif
				} else {
#ifdef ETHER_STATS
					if ((txstatus & 0x0002) != 0) lp->stats.tx_deferred++;
#endif
					lp->stats.collisions += (txstatus >> 8) & 15;
					lp->stats.tx_packets++;
				}

				/* Free the original skb. */
				dev_kfree_skb(lp->tx_skbuff[entry]);
				lp->tx_skbuff[entry] = 0;
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

		/* Check uncommon events all at once. */
		if (status & (CntFull | TxUnderrun | RxOverflow)) {
			/* Always update the error counts to avoid overhead later. */
			lp->stats.rx_missed_errors += inb(ioaddr + MPCNT);
			lp->stats.rx_frame_errors += inb(ioaddr + ALICNT);
			lp->stats.rx_crc_errors += inb(ioaddr + CRCCNT);

			if (status & TxUnderrun) { /* Tx FIFO underflow. */
			  lp->stats.tx_fifo_errors++;
			  /* Restart the transmit process. */
			  outl(0x0080, ioaddr + COMMAND);
			}
			if (status & RxOverflow) {		/* Missed a Rx frame. */
				lp->stats.rx_errors++;
			}
			/* Clear all error sources. */
			outl(status & 0x7f18, ioaddr + INTSTAT);
		}
		if (--boguscnt < 0) {
			printk("%s: Too much work at interrupt, IntrStatus=0x%8.8x.\n",
				   dev->name, status);
			/* Clear all interrupt sources. */
			outl(0x0001ffff, ioaddr + INTSTAT);
			break;
		}
	} while (1);

	if (epic_debug > 3)
		printk("%s: exiting interrupt, intr_status=%#4.4x.\n",
			   dev->name, inl(ioaddr + INTSTAT));

	/* Code that should never be run!  Perhaps remove after testing.. */
	{
		static int stopit = 10;
		if (dev->start == 0  &&  --stopit < 0) {
			printk("%s: Emergency stop, looping startup interrupt.\n",
				   dev->name);
			FREE_IRQ(irq, dev);
		}
	}

	dev->interrupt = 0;
	return;
}

static int
epic_rx(struct device *dev)
{
	struct epic_private *lp = (struct epic_private *)dev->priv;
	int entry = lp->cur_rx % RX_RING_SIZE;

	if (epic_debug > 4)
		printk(" In epic_rx(), entry %d %8.8x.\n", entry,
			   lp->rx_ring[entry].status);
	/* If we own the next entry, it's a new packet. Send it up. */
	while (lp->rx_ring[entry].status >= 0) {
		int status = lp->rx_ring[entry].status;

		if (epic_debug > 4)
			printk("  epic_rx() status was %8.8x.\n", status);
		if (status & 0x2000) {
			printk("%s: Oversized Ethernet frame spanned multiple buffers,"
				   " status %4.4x!\n", dev->name, status);
			  lp->stats.rx_length_errors++;
		} else if (status & 0x0006) {
			/* Rx Frame errors are counted in hardware. */
			lp->stats.rx_errors++;
		} else {
			/* Malloc up new buffer, compatible with net-2e. */
			/* Omit the four octet CRC from the length. */
			short pkt_len = lp->rx_ring[entry].rxlength - 4;
			struct sk_buff *skb;
			int rx_in_place = 0;

			/* Check if the packet is long enough to just accept without
			   copying to a properly sized skbuff. */
			if (pkt_len > rx_copybreak) {
				struct sk_buff *newskb;
				char *temp;

				/* Pass up the skb already on the Rx ring. */
				skb = lp->rx_skbuff[entry];
				temp = skb_put(skb, pkt_len);
				if (bus_to_virt(lp->rx_ring[entry].bufaddr) != temp)
					printk("%s: Warning -- the skbuff addresses do not match"
						   " in epic_rx: %p vs. %p / %p.\n", dev->name,
						   bus_to_virt(lp->rx_ring[entry].bufaddr),
						   skb->head, temp);
				/* Get a fresh skbuff to replace the filled one. */
				newskb = DEV_ALLOC_SKB(PKT_BUF_SZ);
				if (newskb) {
					rx_in_place = 1;
					lp->rx_skbuff[entry] = newskb;
					newskb->dev = dev;
#if LINUX_VERSION_CODE > 0x10300
					/* Align IP on 16 byte boundaries */
					skb_reserve(newskb, 2);
					lp->rx_ring[entry].bufaddr = virt_to_bus(newskb->tail);
#else
					lp->rx_ring[entry].bufaddr = virt_to_bus(newskb->data);
#endif
				} else			/* No memory, drop the packet. */
				  skb = 0;
			} else
				skb = DEV_ALLOC_SKB(pkt_len + 2);
			if (skb == NULL) {
				int i;
				printk("%s: Memory squeeze, deferring packet.\n", dev->name);
				/* Check that at least two ring entries are free.
				   If not, free one and mark stats->rx_dropped++. */
				for (i = 0; i < RX_RING_SIZE; i++)
					if (lp->rx_ring[(entry+i) % RX_RING_SIZE].status < 0)
						break;

				if (i > RX_RING_SIZE -2) {
					lp->stats.rx_dropped++;
					lp->rx_ring[entry].status = 0x8000;
					lp->cur_rx++;
				}
				break;
			}
			skb->dev = dev;
			if (! rx_in_place) {
				skb_reserve(skb, 2);	/* 16 byte align the data fields */
				memcpy(skb_put(skb, pkt_len),
					   bus_to_virt(lp->rx_ring[entry].bufaddr), pkt_len);
			}
#if LINUX_VERSION_CODE > 0x10300
			skb->protocol = eth_type_trans(skb, dev);
#else
			skb->len = pkt_len;
#endif
			netif_rx(skb);
			lp->stats.rx_packets++;
		}

		lp->rx_ring[entry].status = 0x8000;
		entry = (++lp->cur_rx) % RX_RING_SIZE;
	}

	return 0;
}

static int
epic_close(struct device *dev)
{
	int ioaddr = dev->base_addr;
	struct epic_private *tp = (struct epic_private *)dev->priv;
	int i;

	dev->start = 0;
	dev->tbusy = 1;

	if (epic_debug > 1)
		printk("%s: Shutting down ethercard, status was %2.2x.\n",
			   dev->name, inl(ioaddr + INTSTAT));

	/* Disable interrupts by clearing the interrupt mask. */
	outl(0x00000000, ioaddr + INTMASK);
	/* Stop the chip's Tx and Rx DMA processes. */
	outw(0x0061, ioaddr + COMMAND);

	/* Update the error counts. */
	tp->stats.rx_missed_errors += inb(ioaddr + MPCNT);
	tp->stats.rx_frame_errors += inb(ioaddr + ALICNT);
	tp->stats.rx_crc_errors += inb(ioaddr + CRCCNT);

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
		tp->rx_ring[i].status = 0;		/* Not owned by Epic chip. */
		tp->rx_ring[i].buflength = 0;
		tp->rx_ring[i].bufaddr = 0xBADF00D0; /* An invalid address. */
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


	/* Green! Leave the chip in low-power mode. */
	outl(0x0008, ioaddr + GENCTL);
	
	MOD_DEC_USE_COUNT;

	return 0;
}

static struct enet_statistics *
epic_get_stats(struct device *dev)
{
	struct epic_private *tp = (struct epic_private *)dev->priv;
	int ioaddr = dev->base_addr;

	if (dev->start) {
		/* Update the error counts. */
		tp->stats.rx_missed_errors += inb(ioaddr + MPCNT);
		tp->stats.rx_frame_errors += inb(ioaddr + ALICNT);
		tp->stats.rx_crc_errors += inb(ioaddr + CRCCNT);
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


#ifdef NEW_MULTICAST
static void set_rx_mode(struct device *dev)
#else
static void set_rx_mode(struct device *dev, int num_addrs, void *addrs);
#endif
{
	int ioaddr = dev->base_addr;
	struct epic_private *tp = (struct epic_private *)dev->priv;
	unsigned char mc_filter[8];		 /* Multicast hash filter */
	int i;

	if (dev->flags & IFF_PROMISC) {			/* Set promiscuous. */
		outl(0x002C, ioaddr + RxCtrl);
		/* Unconditionally log net taps. */
		printk("%s: Promiscuous mode enabled.\n", dev->name);
		memset(mc_filter, 0xff, sizeof(mc_filter));
	} else if ((dev->mc_count > 0)  ||  (dev->flags & IFF_ALLMULTI)) {
		/* There is apparently a chip bug, so the multicast filter
		   is never enabled. */
		/* Too many to filter perfectly -- accept all multicasts. */
		memset(mc_filter, 0xff, sizeof(mc_filter));
		outl(0x000C, ioaddr + RxCtrl);
	} else if (dev->mc_count == 0) {
		outl(0x0004, ioaddr + RxCtrl);
		return;
	} else {					/* Never executed, for now. */
		struct dev_mc_list *mclist;

		memset(mc_filter, 0, sizeof(mc_filter));
		for (i = 0, mclist = dev->mc_list; mclist && i < dev->mc_count;
			 i++, mclist = mclist->next)
			set_bit(ether_crc_le(ETH_ALEN, mclist->dmi_addr) & 0x3f,
					mc_filter);
	}
	/* ToDo: perhaps we need to stop the Tx and Rx process here? */
	if (memcmp(mc_filter, tp->mc_filter, sizeof(mc_filter))) {
		for (i = 0; i < 4; i++)
			outw(((u16 *)mc_filter)[i], ioaddr + MC0 + i*4);
		memcpy(tp->mc_filter, mc_filter, sizeof(mc_filter));
	}
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
		epic_debug = debug;

	root_epic_dev = NULL;
	cards_found = epic100_probe(0);

	return cards_found ? 0 : -ENODEV;
}

void
cleanup_module(void)
{
	struct device *next_dev;

	/* No need to check MOD_IN_USE, as sys_delete_module() checks. */
	while (root_epic_dev) {
		next_dev = ((struct epic_private *)root_epic_dev->priv)->next_module;
		unregister_netdev(root_epic_dev);
		release_region(root_epic_dev->base_addr, EPIC_TOTAL_SIZE);
		kfree(root_epic_dev);
		root_epic_dev = next_dev;
	}
}

#endif  /* MODULE */

/*
 * Local variables:
 *  compile-command: "gcc -DMODULE -DMODVERSIONS -D__KERNEL__ -Wall -Wstrict-prototypes -O6 -c epic100.c"
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 * End:
 */
