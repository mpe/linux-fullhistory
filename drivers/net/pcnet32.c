/* pcnet32.c: An AMD PCnet32 ethernet driver for linux. */
/*
 *      Copyright 1996-1999 Thomas Bogendoerfer
 * 
 * 	Derived from the lance driver written 1993,1994,1995 by Donald Becker.
 * 
 * 	Copyright 1993 United States Government as represented by the
 * 	Director, National Security Agency.
 * 
 * 	This software may be used and distributed according to the terms
 * 	of the GNU Public License, incorporated herein by reference.
 *
 * 	This driver is for PCnet32 and PCnetPCI based ethercards
 */

static const char *version = "pcnet32.c:v1.21 31.3.99 tsbogend@alpha.franken.de\n";

#include <linux/config.h>
#include <linux/module.h>
#ifdef MODVERSIONS
#include <linux/modversions.h>
#endif

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

static unsigned int pcnet32_portlist[] __initdata = {0x300, 0x320, 0x340, 0x360, 0};

static int pcnet32_debug = 1;

#ifdef MODULE
static struct device *pcnet32_dev = NULL;
#endif

static const int max_interrupt_work = 20;
static const int rx_copybreak = 200;

#define PORT_AUI      0x00
#define PORT_10BT     0x01
#define PORT_GPSI     0x02
#define PORT_MII      0x03

#define PORT_PORTSEL  0x03
#define PORT_ASEL     0x04
#define PORT_100      0x40
#define PORT_FD       0x80


/*
 * table to translate option values from tulip
 * to internal options
 */
static unsigned char options_mapping[] = {
    PORT_ASEL,			   /*  0 Auto-select      */
    PORT_AUI,			   /*  1 BNC/AUI          */
    PORT_AUI,			   /*  2 AUI/BNC          */ 
    PORT_ASEL,			   /*  3 not supported    */
    PORT_10BT | PORT_FD,	   /*  4 10baseT-FD       */
    PORT_ASEL,			   /*  5 not supported    */
    PORT_ASEL,			   /*  6 not supported    */
    PORT_ASEL,			   /*  7 not supported    */
    PORT_ASEL,			   /*  8 not supported    */
    PORT_MII,		           /*  9 MII 10baseT      */
    PORT_MII | PORT_FD,            /* 10 MII 10baseT-FD   */
    PORT_MII,			   /* 11 MII (autosel)    */
    PORT_10BT,			   /* 12 10BaseT	  */
    PORT_MII | PORT_100,	   /* 13 MII 100BaseTx    */
    PORT_MII | PORT_100 | PORT_FD, /* 14 MII 100BaseTx-FD */
    PORT_ASEL			   /* 15 not supported    */
};

#define MAX_UNITS 8
static int options[MAX_UNITS] = {0, };
static int full_duplex[MAX_UNITS] = {0, };

/*
 * 				Theory of Operation
 * 
 * This driver uses the same software structure as the normal lance
 * driver. So look for a verbose description in lance.c. The differences
 * to the normal lance driver is the use of the 32bit mode of PCnet32
 * and PCnetPCI chips. Because these chips are 32bit chips, there is no
 * 16MB limitation and we don't need bounce buffers.
 */
 
/*
 * History:
 * v0.01:  Initial version
 *         only tested on Alpha Noname Board
 * v0.02:  changed IRQ handling for new interrupt scheme (dev_id)
 *         tested on a ASUS SP3G
 * v0.10:  fixed an odd problem with the 79C974 in a Compaq Deskpro XL
 *         looks like the 974 doesn't like stopping and restarting in a
 *         short period of time; now we do a reinit of the lance; the
 *         bug was triggered by doing ifconfig eth0 <ip> broadcast <addr>
 *         and hangs the machine (thanks to Klaus Liedl for debugging)
 * v0.12:  by suggestion from Donald Becker: Renamed driver to pcnet32,
 *         made it standalone (no need for lance.c)
 * v0.13:  added additional PCI detecting for special PCI devices (Compaq)
 * v0.14:  stripped down additional PCI probe (thanks to David C Niemi
 *         and sveneric@xs4all.nl for testing this on their Compaq boxes)
 * v0.15:  added 79C965 (VLB) probe
 *         added interrupt sharing for PCI chips
 * v0.16:  fixed set_multicast_list on Alpha machines
 * v0.17:  removed hack from dev.c; now pcnet32 uses ethif_probe in Space.c
 * v0.19:  changed setting of autoselect bit
 * v0.20:  removed additional Compaq PCI probe; there is now a working one
 *	   in arch/i386/bios32.c
 * v0.21:  added endian conversion for ppc, from work by cort@cs.nmt.edu
 * v0.22:  added printing of status to ring dump
 * v0.23:  changed enet_statistics to net_devive_stats
 * v0.90:  added multicast filter
 *         added module support
 *         changed irq probe to new style
 *         added PCnetFast chip id
 *         added fix for receive stalls with Intel saturn chipsets
 *         added in-place rx skbs like in the tulip driver
 *         minor cleanups
 * v0.91:  added PCnetFast+ chip id
 *         back port to 2.0.x
 * v1.00:  added some stuff from Donald Becker's 2.0.34 version
 *         added support for byte counters in net_dev_stats
 * v1.01:  do ring dumps, only when debugging the driver
 *         increased the transmit timeout
 * v1.02:  fixed memory leak in pcnet32_init_ring()
 * v1.10:  workaround for stopped transmitter
 *         added port selection for modules
 *         detect special T1/E1 WAN card and setup port selection
 * v1.11:  fixed wrong checking of Tx errors
 * v1.20:  added check of return value kmalloc (cpeterso@cs.washington.edu)
 *         added save original kmalloc addr for freeing (mcr@solidum.com)
 *         added support for PCnetHome chip (joe@MIT.EDU)
 *         rewritten PCI card detection
 *         added dwio mode to get driver working on some PPC machines
 * v1.21:  added mii selection and mii ioctl
 */


/*
 * Set the number of Tx and Rx buffers, using Log_2(# buffers).
 * Reasonable default values are 4 Tx buffers, and 16 Rx buffers.
 * That translates to 2 (4 == 2^^2) and 4 (16 == 2^^4).
 */
#ifndef PCNET32_LOG_TX_BUFFERS
#define PCNET32_LOG_TX_BUFFERS 4
#define PCNET32_LOG_RX_BUFFERS 4
#endif

#define TX_RING_SIZE			(1 << (PCNET32_LOG_TX_BUFFERS))
#define TX_RING_MOD_MASK		(TX_RING_SIZE - 1)
#define TX_RING_LEN_BITS		((PCNET32_LOG_TX_BUFFERS) << 12)

#define RX_RING_SIZE			(1 << (PCNET32_LOG_RX_BUFFERS))
#define RX_RING_MOD_MASK		(RX_RING_SIZE - 1)
#define RX_RING_LEN_BITS		((PCNET32_LOG_RX_BUFFERS) << 4)

#define PKT_BUF_SZ		1544

/* Offsets from base I/O address. */
#define PCNET32_WIO_RDP		0x10
#define PCNET32_WIO_RAP		0x12
#define PCNET32_WIO_RESET	0x14
#define PCNET32_WIO_BDP		0x16

#define PCNET32_DWIO_RDP	0x10
#define PCNET32_DWIO_RAP	0x14
#define PCNET32_DWIO_RESET	0x18
#define PCNET32_DWIO_BDP	0x1C

#define PCNET32_TOTAL_SIZE 0x20

#define CRC_POLYNOMIAL_LE 0xedb88320UL  /* Ethernet CRC, little endian */

/* The PCNET32 Rx and Tx ring descriptors. */
struct pcnet32_rx_head {
    u32 base;
    s16 buf_length;
    s16 status;    
    u32 msg_length;
    u32 reserved;
};
	
struct pcnet32_tx_head {
    u32 base;
    s16 length;
    s16 status;
    u32 misc;
    u32 reserved;
};

/* The PCNET32 32-Bit initialization block, described in databook. */
struct pcnet32_init_block {
    u16 mode;
    u16 tlen_rlen;
    u8  phys_addr[6];
    u16 reserved;
    u32 filter[2];
    /* Receive and transmit ring base, along with extra bits. */    
    u32 rx_ring;
    u32 tx_ring;
};

/* PCnet32 access functions */
struct pcnet32_access {
    u16 (*read_csr)(unsigned long, int);
    void (*write_csr)(unsigned long, int, u16);
    u16 (*read_bcr)(unsigned long, int);
    void (*write_bcr)(unsigned long, int, u16);
    u16 (*read_rap)(unsigned long);
    void (*write_rap)(unsigned long, u16);
    void (*reset)(unsigned long);
};

struct pcnet32_private {
    /* The Tx and Rx ring entries must be aligned on 16-byte boundaries in 32bit mode. */
    struct pcnet32_rx_head   rx_ring[RX_RING_SIZE];
    struct pcnet32_tx_head   tx_ring[TX_RING_SIZE];
    struct pcnet32_init_block	init_block;
    const char *name;
    /* The saved address of a sent-in-place packet/buffer, for skfree(). */
    struct sk_buff *tx_skbuff[TX_RING_SIZE];
    struct sk_buff *rx_skbuff[RX_RING_SIZE];
    struct pcnet32_access a;
    void *origmem;
    int cur_rx, cur_tx;			/* The next free ring entry */
    int dirty_rx, dirty_tx;		        /* The ring entries to be free()ed. */
    struct net_device_stats stats;
    char tx_full;
    int  options;
    int  shared_irq:1,                      /* shared irq possible */
         full_duplex:1,                     /* full duplex possible */
         mii:1;                             /* mii port available */
#ifdef MODULE
    struct device *next;
#endif    
};

int  pcnet32_probe(struct device *);
static int  pcnet32_probe1(struct device *, unsigned long, unsigned char, int, int);
static int  pcnet32_open(struct device *);
static int  pcnet32_init_ring(struct device *);
static int  pcnet32_start_xmit(struct sk_buff *, struct device *);
static int  pcnet32_rx(struct device *);
static void pcnet32_interrupt(int, void *, struct pt_regs *);
static int  pcnet32_close(struct device *);
static struct net_device_stats *pcnet32_get_stats(struct device *);
static void pcnet32_set_multicast_list(struct device *);
#ifdef HAVE_PRIVATE_IOCTL
static int  pcnet32_mii_ioctl(struct device *, struct ifreq *, int);
#endif

enum pci_flags_bit {
    PCI_USES_IO=1, PCI_USES_MEM=2, PCI_USES_MASTER=4,
    PCI_ADDR0=0x10<<0, PCI_ADDR1=0x10<<1, PCI_ADDR2=0x10<<2, PCI_ADDR3=0x10<<3,
};

struct pcnet32_pci_id_info {
    const char *name;
    u16 vendor_id, device_id, device_id_mask, flags;
    int io_size;
    int (*probe1) (struct device *, unsigned long, unsigned char, int, int);
};

static struct pcnet32_pci_id_info pcnet32_tbl[] = {
    { "AMD PCnetPCI series",
	0x1022, 0x2000, 0xfffe, PCI_USES_IO|PCI_USES_MASTER, PCNET32_TOTAL_SIZE,
	pcnet32_probe1},
    {0,}
};

static u16 pcnet32_wio_read_csr (unsigned long addr, int index)
{
    outw (index, addr+PCNET32_WIO_RAP);
    return inw (addr+PCNET32_WIO_RDP);
}

static void pcnet32_wio_write_csr (unsigned long addr, int index, u16 val)
{
    outw (index, addr+PCNET32_WIO_RAP);
    outw (val, addr+PCNET32_WIO_RDP);
}

static u16 pcnet32_wio_read_bcr (unsigned long addr, int index)
{
    outw (index, addr+PCNET32_WIO_RAP);
    return inw (addr+PCNET32_WIO_BDP);
}

static void pcnet32_wio_write_bcr (unsigned long addr, int index, u16 val)
{
    outw (index, addr+PCNET32_WIO_RAP);
    outw (val, addr+PCNET32_WIO_BDP);
}

static u16 pcnet32_wio_read_rap (unsigned long addr)
{
    return inw (addr+PCNET32_WIO_RAP);
}

static void pcnet32_wio_write_rap (unsigned long addr, u16 val)
{
    outw (val, addr+PCNET32_WIO_RAP);
}

static void pcnet32_wio_reset (unsigned long addr)
{
    inw (addr+PCNET32_WIO_RESET);
}

static int pcnet32_wio_check (unsigned long addr)
{
    outw (88, addr+PCNET32_WIO_RAP);
    return (inw (addr+PCNET32_WIO_RAP) == 88);
}

static struct pcnet32_access pcnet32_wio = {
    pcnet32_wio_read_csr,
    pcnet32_wio_write_csr,
    pcnet32_wio_read_bcr,
    pcnet32_wio_write_bcr,
    pcnet32_wio_read_rap,
    pcnet32_wio_write_rap,
    pcnet32_wio_reset
};

static u16 pcnet32_dwio_read_csr (unsigned long addr, int index)
{
    outl (index, addr+PCNET32_DWIO_RAP);
    return (inl (addr+PCNET32_DWIO_RDP) & 0xffff);
}

static void pcnet32_dwio_write_csr (unsigned long addr, int index, u16 val)
{
    outl (index, addr+PCNET32_DWIO_RAP);
    outl (val, addr+PCNET32_DWIO_RDP);
}

static u16 pcnet32_dwio_read_bcr (unsigned long addr, int index)
{
    outl (index, addr+PCNET32_DWIO_RAP);
    return (inl (addr+PCNET32_DWIO_BDP) & 0xffff);
}

static void pcnet32_dwio_write_bcr (unsigned long addr, int index, u16 val)
{
    outl (index, addr+PCNET32_DWIO_RAP);
    outl (val, addr+PCNET32_DWIO_BDP);
}

static u16 pcnet32_dwio_read_rap (unsigned long addr)
{
    return (inl (addr+PCNET32_DWIO_RAP) & 0xffff);
}

static void pcnet32_dwio_write_rap (unsigned long addr, u16 val)
{
    outl (val, addr+PCNET32_DWIO_RAP);
}

static void pcnet32_dwio_reset (unsigned long addr)
{
    inl (addr+PCNET32_DWIO_RESET);
}

static int pcnet32_dwio_check (unsigned long addr)
{
    outl (88, addr+PCNET32_DWIO_RAP);
    return (inl (addr+PCNET32_DWIO_RAP) == 88);
}

static struct pcnet32_access pcnet32_dwio = {
    pcnet32_dwio_read_csr,
    pcnet32_dwio_write_csr,
    pcnet32_dwio_read_bcr,
    pcnet32_dwio_write_bcr,
    pcnet32_dwio_read_rap,
    pcnet32_dwio_write_rap,
    pcnet32_dwio_reset

};



int __init pcnet32_probe (struct device *dev)
{
    unsigned long ioaddr = dev ? dev->base_addr: 0;
    unsigned int  irq_line = dev ? dev->irq : 0;
    int *port;
    int cards_found = 0;
    
    
#ifndef __powerpc__
    if (ioaddr > 0x1ff) {
	if (check_region(ioaddr, PCNET32_TOTAL_SIZE) == 0)
	    return pcnet32_probe1(dev, ioaddr, irq_line, 0, 0);
	else
	    return ENODEV;
    } else
#endif
	if(ioaddr != 0)
	    return ENXIO;
    
#if defined(CONFIG_PCI)
    if (pci_present()) {
	struct pci_dev *pdev;
	unsigned char pci_bus, pci_device_fn;
	int pci_index;
	
	printk("pcnet32.c: PCI bios is present, checking for devices...\n");
	for (pci_index = 0; pci_index < 0xff; pci_index++) {
	    u16 vendor, device, pci_command;
	    int chip_idx;

	    if (pcibios_find_class (PCI_CLASS_NETWORK_ETHERNET << 8,
				    pci_index, &pci_bus, &pci_device_fn) != PCIBIOS_SUCCESSFUL)
		break;
	    
	    pcibios_read_config_word(pci_bus, pci_device_fn, PCI_VENDOR_ID, &vendor);
	    pcibios_read_config_word(pci_bus, pci_device_fn, PCI_DEVICE_ID, &device);

	    for (chip_idx = 0; pcnet32_tbl[chip_idx].vendor_id; chip_idx++)
		if (vendor == pcnet32_tbl[chip_idx].vendor_id &&
		    (device & pcnet32_tbl[chip_idx].device_id_mask) == pcnet32_tbl[chip_idx].device_id)
		    break;
	    if (pcnet32_tbl[chip_idx].vendor_id == 0)
		continue;
	    
	    pdev = pci_find_slot(pci_bus, pci_device_fn);
	    ioaddr = pdev->base_address[0] & PCI_BASE_ADDRESS_IO_MASK;
#if defined(ADDR_64BITS) && defined(__alpha__)
	    ioaddr |= ((long)pdev->base_address[1]) << 32;
#endif
	    irq_line = pdev->irq;
	    
	    /* Avoid already found cards from previous pcnet32_probe() calls */
	    if ((pcnet32_tbl[chip_idx].flags & PCI_USES_IO) &&
		check_region(ioaddr, pcnet32_tbl[chip_idx].io_size))
		continue;

	    /* PCI Spec 2.1 states that it is either the driver or PCI card's
	     * responsibility to set the PCI Master Enable Bit if needed.
	     *	(From Mark Stockton <marks@schooner.sys.hou.compaq.com>)
	     */
	    pci_read_config_word(pdev, PCI_COMMAND, &pci_command);
	    if ( ! (pci_command & PCI_COMMAND_MASTER)) {
		printk("PCI Master Bit has not been set. Setting...\n");
		pci_command |= PCI_COMMAND_MASTER|PCI_COMMAND_IO;
		pci_write_config_word(pdev, PCI_COMMAND, pci_command);
	    }
	    printk("Found PCnet/PCI at %#lx, irq %d.\n", ioaddr, irq_line);
	    
	    if (pcnet32_tbl[chip_idx].probe1(dev, ioaddr, irq_line, 1, cards_found) == 0) {
		cards_found++;
		dev = NULL;
	    }
	}
    } else 
#endif  /* defined(CONFIG_PCI) */
    
    /* now look for PCnet32 VLB cards */
    for (port = pcnet32_portlist; *port; port++) {
	unsigned long ioaddr = *port;
	
	if ( check_region(ioaddr, PCNET32_TOTAL_SIZE) == 0) {
    	    /* check if there is really a pcnet chip on that ioaddr */
    	    if ((inb(ioaddr + 14) == 0x57) &&
		(inb(ioaddr + 15) == 0x57) &&
	        (pcnet32_probe1(dev, ioaddr, 0, 0, 0) == 0))
		cards_found++;
	}
    }
    return cards_found ? 0: ENODEV;
}


/* pcnet32_probe1 */
static int __init
pcnet32_probe1(struct device *dev, unsigned long ioaddr, unsigned char irq_line, int shared, int card_idx)
{
    struct pcnet32_private *lp;
    int i,media,fdx = 0, mii = 0;
    int chip_version;
    char *chipname;
    char *priv;
    struct pcnet32_access *a;

    /* reset the chip */
    pcnet32_dwio_reset(ioaddr);
    pcnet32_wio_reset(ioaddr);

    if (pcnet32_wio_read_csr (ioaddr, 0) == 4 && pcnet32_wio_check (ioaddr)) {
	a = &pcnet32_wio;
    } else {
	if (pcnet32_dwio_read_csr (ioaddr, 0) == 4 && pcnet32_dwio_check(ioaddr)) {
	    a = &pcnet32_dwio;
	} else
	    return ENODEV;
    }

    chip_version = a->read_csr (ioaddr, 88) | (a->read_csr (ioaddr,89) << 16);
    if (pcnet32_debug > 2)
	printk("  PCnet chip version is %#x.\n", chip_version);
    if ((chip_version & 0xfff) != 0x003)
	return ENODEV;
    chip_version = (chip_version >> 12) & 0xffff;
    switch (chip_version) {
     case 0x2420:
	chipname = "PCnet/PCI 79C970";
	break;
     case 0x2430:
	if (shared)
	    chipname = "PCnet/PCI 79C970"; /* 970 gives the wrong chip id back */
	else
	    chipname = "PCnet/32 79C965";
	break;
     case 0x2621:
	chipname = "PCnet/PCI II 79C970A";
	fdx = 1;
	break;
     case 0x2623:
	chipname = "PCnet/FAST 79C971";
	fdx = 1; mii = 1;
	break;
     case 0x2624:
	chipname = "PCnet/FAST+ 79C972";
	fdx = 1; mii = 1;
	break;
     case 0x2626:
	chipname = "PCnet/Home 79C978";
	fdx = 1;
	/* 
	 * This is based on specs published at www.amd.com.  This section
	 * assumes that a card with a 79C978 wants to go into 1Mb HomePNA
	 * mode.  The 79C978 can also go into standard ethernet, and there
	 * probably should be some sort of module option to select the
	 * mode by which the card should operate
	 */
	/* switch to home wiring mode */
	media = a->read_bcr (ioaddr, 49);
	if (pcnet32_debug > 2)
	    printk("pcnet32: pcnet32 media value %#x.\n",  media);
	media &= ~3;
	media |= 1;
	if (pcnet32_debug > 2)
	    printk("pcnet32: pcnet32 media reset to %#x.\n",  media);
	a->write_bcr (ioaddr, 49, media);
	break;
     default:
	printk("pcnet32: PCnet version %#x, no PCnet32 chip.\n",chip_version);
	return ENODEV;
    }
    
    dev = init_etherdev(dev, 0);

    printk(KERN_INFO "%s: %s at %#3lx,", dev->name, chipname, ioaddr);

    /* There is a 16 byte station address PROM at the base address.
     The first six bytes are the station address. */
    for (i = 0; i < 6; i++)
      printk(" %2.2x", dev->dev_addr[i] = inb(ioaddr + i));

    dev->base_addr = ioaddr;
    request_region(ioaddr, PCNET32_TOTAL_SIZE, chipname);
    
    if ((priv = kmalloc(sizeof(*lp)+15,GFP_KERNEL)) == NULL)
	return ENOMEM;

    /*
     * Make certain the data structures used by
     * the PCnet32 are 16byte aligned
      */
    lp = (struct pcnet32_private *)(((unsigned long)priv+15) & ~15);
      
    memset(lp, 0, sizeof(*lp));
    dev->priv = lp;
    lp->name = chipname;
    lp->shared_irq = shared;
    lp->full_duplex = fdx;
    lp->mii = mii;
    if (options[card_idx] > sizeof (options_mapping))
	lp->options = PORT_ASEL;
    else
	lp->options = options_mapping[options[card_idx]];
    
    if (fdx && !(lp->options & PORT_ASEL) && full_duplex[card_idx])
	lp->options |= PORT_FD;
    
    lp->origmem = priv;
    lp->a = *a;
    
    /* detect special T1/E1 WAN card by checking for MAC address */
    if (dev->dev_addr[0] == 0x00 && dev->dev_addr[1] == 0xe0 && dev->dev_addr[2] == 0x75)
	lp->options = PORT_FD | PORT_GPSI;

    lp->init_block.mode = le16_to_cpu(0x0003); 	/* Disable Rx and Tx. */
    lp->init_block.tlen_rlen = le16_to_cpu(TX_RING_LEN_BITS | RX_RING_LEN_BITS); 
    for (i = 0; i < 6; i++)
      lp->init_block.phys_addr[i] = dev->dev_addr[i];
    lp->init_block.filter[0] = 0x00000000;
    lp->init_block.filter[1] = 0x00000000;
    lp->init_block.rx_ring = (u32)le32_to_cpu(virt_to_bus(lp->rx_ring));
    lp->init_block.tx_ring = (u32)le32_to_cpu(virt_to_bus(lp->tx_ring));
    
    /* switch pcnet32 to 32bit mode */
    a->write_bcr (ioaddr, 20, 2);

    a->write_csr (ioaddr, 1, virt_to_bus(&lp->init_block) & 0xffff);
    a->write_csr (ioaddr, 2, virt_to_bus(&lp->init_block) >> 16);
    
    if (irq_line) {
	dev->irq = irq_line;
    }
    
    if (dev->irq >= 2)
	printk(" assigned IRQ %d.\n", dev->irq);
    else {
	unsigned long irq_mask = probe_irq_on();
	
	/*
	 * To auto-IRQ we enable the initialization-done and DMA error
	 * interrupts. For ISA boards we get a DMA error, but VLB and PCI
	 * boards will work.
	 */
	/* Trigger an initialization just for the interrupt. */
	a->write_csr (ioaddr, 0, 0x41);
	mdelay (1);
	
	dev->irq = probe_irq_off (irq_mask);
	if (dev->irq)
	  printk(", probed IRQ %d.\n", dev->irq);
	else {
	    printk(", failed to detect IRQ line.\n");
	    return ENODEV;
	}
    }

    if (pcnet32_debug > 0)
	printk(version);
    
    /* The PCNET32-specific entries in the device structure. */
    dev->open = &pcnet32_open;
    dev->hard_start_xmit = &pcnet32_start_xmit;
    dev->stop = &pcnet32_close;
    dev->get_stats = &pcnet32_get_stats;
    dev->set_multicast_list = &pcnet32_set_multicast_list;
#ifdef HAVE_PRIVATE_IOCTL
    dev->do_ioctl = &pcnet32_mii_ioctl;
#endif

    
#ifdef MODULE
    lp->next = pcnet32_dev;
    pcnet32_dev = dev;
#endif	

    /* Fill in the generic fields of the device structure. */
    ether_setup(dev);
    return 0;
}


static int
pcnet32_open(struct device *dev)
{
    struct pcnet32_private *lp = (struct pcnet32_private *)dev->priv;
    unsigned long ioaddr = dev->base_addr;
    u16 val;
    int i;

    if (dev->irq == 0 ||
	request_irq(dev->irq, &pcnet32_interrupt,
		    lp->shared_irq ? SA_SHIRQ : 0, lp->name, (void *)dev)) {
	return -EAGAIN;
    }

    /* Reset the PCNET32 */
    lp->a.reset (ioaddr);

    /* switch pcnet32 to 32bit mode */
    lp->a.write_csr (ioaddr, 20, 2);

    if (pcnet32_debug > 1)
	printk("%s: pcnet32_open() irq %d tx/rx rings %#x/%#x init %#x.\n",
	       dev->name, dev->irq,
	       (u32) virt_to_bus(lp->tx_ring),
	       (u32) virt_to_bus(lp->rx_ring),
	       (u32) virt_to_bus(&lp->init_block));
    
    /* set/reset autoselect bit */
    val = lp->a.read_bcr (ioaddr, 2) & ~2;
    if (lp->options & PORT_ASEL)
	val |= 2;
    lp->a.write_bcr (ioaddr, 2, val);
    
    /* handle full duplex setting */
    if (lp->full_duplex) {
	val = lp->a.read_bcr (ioaddr, 9) & ~3;
	if (lp->options & PORT_FD) {
	    val |= 1;
	    if (lp->options == (PORT_FD | PORT_AUI))
		val |= 2;
	}
	lp->a.write_bcr (ioaddr, 9, val);
    }
    
    /* set/reset GPSI bit in test register */
    val = lp->a.read_csr (ioaddr, 124) & ~0x10;
    if ((lp->options & PORT_PORTSEL) == PORT_GPSI)
	val |= 0x10;
    lp->a.write_csr (ioaddr, 124, val);
    
    if (lp->mii & (lp->options & PORT_ASEL)) {
	val = lp->a.read_bcr (ioaddr, 32) & ~0x38; /* disable Auto Negotiation, set 10Mpbs, HD */
	if (lp->options & PORT_FD)
	    val |= 0x10;
	if (lp->options & PORT_100)
	    val |= 0x08;
	lp->a.write_bcr (ioaddr, 32, val);
    }
    
    lp->init_block.mode = le16_to_cpu((lp->options & PORT_PORTSEL) << 7);
    lp->init_block.filter[0] = 0x00000000;
    lp->init_block.filter[1] = 0x00000000;
    if (pcnet32_init_ring(dev))
	return -ENOMEM;
    
    /* Re-initialize the PCNET32, and start it when done. */
    lp->a.write_csr (ioaddr, 1, virt_to_bus(&lp->init_block) &0xffff);
    lp->a.write_csr (ioaddr, 2, virt_to_bus(&lp->init_block) >> 16);

    lp->a.write_csr (ioaddr, 4, 0x0915);
    lp->a.write_csr (ioaddr, 0, 0x0001);

    dev->tbusy = 0;
    dev->interrupt = 0;
    dev->start = 1;
    i = 0;
    while (i++ < 100)
	if (lp->a.read_csr (ioaddr, 0) & 0x0100)
	    break;
    /* 
     * We used to clear the InitDone bit, 0x0100, here but Mark Stockton
     * reports that doing so triggers a bug in the '974.
     */
    lp->a.write_csr (ioaddr, 0, 0x0042);

    if (pcnet32_debug > 2)
	printk("%s: PCNET32 open after %d ticks, init block %#x csr0 %4.4x.\n",
	       dev->name, i, (u32) virt_to_bus(&lp->init_block),
	       lp->a.read_csr (ioaddr, 0));

    MOD_INC_USE_COUNT;
    
    return 0;	/* Always succeed */
}

/*
 * The LANCE has been halted for one reason or another (busmaster memory
 * arbitration error, Tx FIFO underflow, driver stopped it to reconfigure,
 * etc.).  Modern LANCE variants always reload their ring-buffer
 * configuration when restarted, so we must reinitialize our ring
 * context before restarting.  As part of this reinitialization,
 * find all packets still on the Tx ring and pretend that they had been
 * sent (in effect, drop the packets on the floor) - the higher-level
 * protocols will time out and retransmit.  It'd be better to shuffle
 * these skbs to a temp list and then actually re-Tx them after
 * restarting the chip, but I'm too lazy to do so right now.  dplatt@3do.com
 */

static void 
pcnet32_purge_tx_ring(struct device *dev)
{
    struct pcnet32_private *lp = (struct pcnet32_private *)dev->priv;
    int i;

    for (i = 0; i < TX_RING_SIZE; i++) {
	if (lp->tx_skbuff[i]) {
	    dev_kfree_skb(lp->tx_skbuff[i]);
	    lp->tx_skbuff[i] = NULL;
	}
    }
}


/* Initialize the PCNET32 Rx and Tx rings. */
static int
pcnet32_init_ring(struct device *dev)
{
    struct pcnet32_private *lp = (struct pcnet32_private *)dev->priv;
    int i;

    lp->tx_full = 0;
    lp->cur_rx = lp->cur_tx = 0;
    lp->dirty_rx = lp->dirty_tx = 0;

    for (i = 0; i < RX_RING_SIZE; i++) {
	if (lp->rx_skbuff[i] == NULL) {
	    if (!(lp->rx_skbuff[i] = dev_alloc_skb (PKT_BUF_SZ))) {
		/* there is not much, we can do at this point */
		printk ("%s: pcnet32_init_ring dev_alloc_skb failed.\n",dev->name);
		return -1;
	    }
	    skb_reserve (lp->rx_skbuff[i], 2);
	}
	lp->rx_ring[i].base = (u32)le32_to_cpu(virt_to_bus(lp->rx_skbuff[i]->tail));
	lp->rx_ring[i].buf_length = le16_to_cpu(-PKT_BUF_SZ);
	lp->rx_ring[i].status = le16_to_cpu(0x8000);
    }
    /* The Tx buffer address is filled in as needed, but we do need to clear
     the upper ownership bit. */
    for (i = 0; i < TX_RING_SIZE; i++) {
	lp->tx_ring[i].base = 0;
	lp->tx_ring[i].status = 0;
    }

    lp->init_block.tlen_rlen = TX_RING_LEN_BITS | RX_RING_LEN_BITS;
    for (i = 0; i < 6; i++)
	lp->init_block.phys_addr[i] = dev->dev_addr[i];
    lp->init_block.rx_ring = (u32)le32_to_cpu(virt_to_bus(lp->rx_ring));
    lp->init_block.tx_ring = (u32)le32_to_cpu(virt_to_bus(lp->tx_ring));
    return 0;
}

static void
pcnet32_restart(struct device *dev, unsigned int csr0_bits)
{
    struct pcnet32_private *lp = (struct pcnet32_private *)dev->priv;
    unsigned long ioaddr = dev->base_addr;
    int i;
    
    pcnet32_purge_tx_ring(dev);
    if (pcnet32_init_ring(dev))
	return;
    
    /* ReInit Ring */
    lp->a.write_csr (ioaddr, 0, 1);
    i = 0;
    while (i++ < 100)
	if (lp->a.read_csr (ioaddr, 0) & 0x0100)
	    break;

    lp->a.write_csr (ioaddr, 0, csr0_bits);
}

static int
pcnet32_start_xmit(struct sk_buff *skb, struct device *dev)
{
    struct pcnet32_private *lp = (struct pcnet32_private *)dev->priv;
    unsigned int ioaddr = dev->base_addr;
    int entry;
    unsigned long flags;

    /* Transmitter timeout, serious problems. */
    if (dev->tbusy) {
	int tickssofar = jiffies - dev->trans_start;
	if (tickssofar < HZ/2)
	    return 1;
	printk("%s: transmit timed out, status %4.4x, resetting.\n",
	       dev->name, lp->a.read_csr (ioaddr, 0));
	lp->a.write_csr (ioaddr, 0, 0x0004);
	lp->stats.tx_errors++;
	if (pcnet32_debug > 2) {
	    int i;
	    printk(" Ring data dump: dirty_tx %d cur_tx %d%s cur_rx %d.",
		   lp->dirty_tx, lp->cur_tx, lp->tx_full ? " (full)" : "",
		   lp->cur_rx);
	    for (i = 0 ; i < RX_RING_SIZE; i++)
		printk("%s %08x %04x %08x %04x", i & 1 ? "" : "\n ",
		       lp->rx_ring[i].base, -lp->rx_ring[i].buf_length,
		       lp->rx_ring[i].msg_length, (unsigned)lp->rx_ring[i].status);
	    for (i = 0 ; i < TX_RING_SIZE; i++)
		printk("%s %08x %04x %08x %04x", i & 1 ? "" : "\n ",
		       lp->tx_ring[i].base, -lp->tx_ring[i].length,
		       lp->tx_ring[i].misc, (unsigned)lp->tx_ring[i].status);
	    printk("\n");
	}
	pcnet32_restart(dev, 0x0042);

	dev->tbusy = 0;
	dev->trans_start = jiffies;
	dev_kfree_skb(skb);
	return 0;
    }

    if (pcnet32_debug > 3) {
	printk("%s: pcnet32_start_xmit() called, csr0 %4.4x.\n",
	       dev->name, lp->a.read_csr (ioaddr, 0));
    }

    /* Block a timer-based transmit from overlapping.  This could better be
       done with atomic_swap(1, dev->tbusy), but set_bit() works as well. */
    if (test_and_set_bit(0, (void*)&dev->tbusy) != 0) {
	printk("%s: Transmitter access conflict.\n", dev->name);
	return 1;
    }

    save_flags (flags);
    cli ();

    /* Fill in a Tx ring entry */

    /* Mask to ring buffer boundary. */
    entry = lp->cur_tx & TX_RING_MOD_MASK;

    /* Caution: the write order is important here, set the base address
       with the "ownership" bits last. */

    lp->tx_ring[entry].length = le16_to_cpu(-skb->len);

    lp->tx_ring[entry].misc = 0x00000000;

    lp->tx_skbuff[entry] = skb;
    lp->tx_ring[entry].base = (u32)le32_to_cpu(virt_to_bus(skb->data));
    lp->tx_ring[entry].status = le16_to_cpu(0x8300);

    lp->cur_tx++;
    lp->stats.tx_bytes += skb->len;

    /* Trigger an immediate send poll. */
    lp->a.write_csr (ioaddr, 0, 0x0048);

    dev->trans_start = jiffies;

    if (lp->tx_ring[(entry+1) & TX_RING_MOD_MASK].base == 0)
	clear_bit (0, (void *)&dev->tbusy);
    else
	lp->tx_full = 1;
    restore_flags(flags);
    return 0;
}

/* The PCNET32 interrupt handler. */
static void
pcnet32_interrupt(int irq, void *dev_id, struct pt_regs * regs)
{
    struct device *dev = (struct device *)dev_id;
    struct pcnet32_private *lp;
    unsigned long ioaddr;
    u16 csr0;
    int boguscnt =  max_interrupt_work;
    int must_restart;

    if (dev == NULL) {
	printk ("pcnet32_interrupt(): irq %d for unknown device.\n", irq);
	return;
    }

    ioaddr = dev->base_addr;
    lp = (struct pcnet32_private *)dev->priv;
    if (dev->interrupt)
	printk("%s: Re-entering the interrupt handler.\n", dev->name);

    dev->interrupt = 1;

    while ((csr0 = lp->a.read_csr (ioaddr, 0)) & 0x8600 && --boguscnt >= 0) {
	/* Acknowledge all of the current interrupt sources ASAP. */
	lp->a.write_csr (ioaddr, 0, csr0 & ~0x004f);

	must_restart = 0;

	if (pcnet32_debug > 5)
	    printk("%s: interrupt  csr0=%#2.2x new csr=%#2.2x.\n",
		   dev->name, csr0, lp->a.read_csr (ioaddr, 0));

	if (csr0 & 0x0400)		/* Rx interrupt */
	    pcnet32_rx(dev);

	if (csr0 & 0x0200) {		/* Tx-done interrupt */
	    int dirty_tx = lp->dirty_tx;

	    while (dirty_tx < lp->cur_tx) {
		int entry = dirty_tx & TX_RING_MOD_MASK;
		int status = (short)le16_to_cpu(lp->tx_ring[entry].status);
			
		if (status < 0)
		    break;		/* It still hasn't been Txed */

		lp->tx_ring[entry].base = 0;

		if (status & 0x4000) {
		    /* There was an major error, log it. */
		    int err_status = le32_to_cpu(lp->tx_ring[entry].misc);
		    lp->stats.tx_errors++;
		    if (err_status & 0x04000000) lp->stats.tx_aborted_errors++;
		    if (err_status & 0x08000000) lp->stats.tx_carrier_errors++;
		    if (err_status & 0x10000000) lp->stats.tx_window_errors++;
		    if (err_status & 0x40000000) {
			/* Ackk!  On FIFO errors the Tx unit is turned off! */
			lp->stats.tx_fifo_errors++;
			/* Remove this verbosity later! */
			printk("%s: Tx FIFO error! Status %4.4x.\n",
			       dev->name, csr0);
			must_restart = 1;
					}
		} else {
		    if (status & 0x1800)
			lp->stats.collisions++;
		    lp->stats.tx_packets++;
		}

		/* We must free the original skb */
		if (lp->tx_skbuff[entry]) {
		    dev_kfree_skb(lp->tx_skbuff[entry]);
		    lp->tx_skbuff[entry] = 0;
		}
		dirty_tx++;
	    }

#ifndef final_version
	    if (lp->cur_tx - dirty_tx >= TX_RING_SIZE) {
		printk("out-of-sync dirty pointer, %d vs. %d, full=%d.\n",
		       dirty_tx, lp->cur_tx, lp->tx_full);
		dirty_tx += TX_RING_SIZE;
	    }
#endif
	    if (lp->tx_full && dev->tbusy
		&& dirty_tx > lp->cur_tx - TX_RING_SIZE + 2) {
		/* The ring is no longer full, clear tbusy. */
		lp->tx_full = 0;
		clear_bit(0, (void *)&dev->tbusy);
		mark_bh(NET_BH);
	    }
	    lp->dirty_tx = dirty_tx;
	}

	/* Log misc errors. */
	if (csr0 & 0x4000) lp->stats.tx_errors++; /* Tx babble. */
	if (csr0 & 0x1000) {
	    /*
	     * this happens when our receive ring is full. This shouldn't
	     * be a problem as we will see normal rx interrupts for the frames
	     * in the receive ring. But there are some PCI chipsets (I can reproduce
	     * this on SP3G with Intel saturn chipset) which have sometimes problems
	     * and will fill up the receive ring with error descriptors. In this
	     * situation we don't get a rx interrupt, but a missed frame interrupt sooner
	     * or later. So we try to clean up our receive ring here.
	     */
	    pcnet32_rx(dev);
	    lp->stats.rx_errors++; /* Missed a Rx frame. */
	}
	if (csr0 & 0x0800) {
	    printk("%s: Bus master arbitration failure, status %4.4x.\n",
		   dev->name, csr0);
	    /* unlike for the lance, there is no restart needed */
	}

	if (must_restart) {
	    /* stop the chip to clear the error condition, then restart */
	    lp->a.write_csr (ioaddr, 0, 0x0004);
	    pcnet32_restart(dev, 0x0002);
	}
    }

    /* Clear any other interrupt, and set interrupt enable. */
    lp->a.write_csr (ioaddr, 0, 0x7940);

    if (pcnet32_debug > 4)
	printk("%s: exiting interrupt, csr0=%#4.4x.\n",
	       dev->name, lp->a.read_csr (ioaddr, 0));

    dev->interrupt = 0;
    return;
}

static int
pcnet32_rx(struct device *dev)
{
    struct pcnet32_private *lp = (struct pcnet32_private *)dev->priv;
    int entry = lp->cur_rx & RX_RING_MOD_MASK;
    int i;

    /* If we own the next entry, it's a new packet. Send it up. */
    while ((short)le16_to_cpu(lp->rx_ring[entry].status) >= 0) {
	int status = (short)le16_to_cpu(lp->rx_ring[entry].status) >> 8;

	if (status != 0x03) {			/* There was an error. */
	    /* 
	     * There is a tricky error noted by John Murphy,
	     * <murf@perftech.com> to Russ Nelson: Even with full-sized
	     * buffers it's possible for a jabber packet to use two
	     * buffers, with only the last correctly noting the error.
	     */
	    if (status & 0x01)	/* Only count a general error at the */
		lp->stats.rx_errors++; /* end of a packet.*/
	    if (status & 0x20) lp->stats.rx_frame_errors++;
	    if (status & 0x10) lp->stats.rx_over_errors++;
	    if (status & 0x08) lp->stats.rx_crc_errors++;
	    if (status & 0x04) lp->stats.rx_fifo_errors++;
	    lp->rx_ring[entry].status &= le16_to_cpu(0x03ff);
	} else {
	    /* Malloc up new buffer, compatible with net-2e. */
	    short pkt_len = (le32_to_cpu(lp->rx_ring[entry].msg_length) & 0xfff)-4;
	    struct sk_buff *skb;
			
	    if(pkt_len < 60) {
		printk("%s: Runt packet!\n",dev->name);
		lp->stats.rx_errors++;
	    } else {
		int rx_in_place = 0;
			    
		if (pkt_len > rx_copybreak) {
		    struct sk_buff *newskb;
				
		    if ((newskb = dev_alloc_skb (PKT_BUF_SZ))) {
			skb_reserve (newskb, 2);
			skb = lp->rx_skbuff[entry];
			skb_put (skb, pkt_len);
			lp->rx_skbuff[entry] = newskb;
			newskb->dev = dev;
			lp->rx_ring[entry].base = le32_to_cpu(virt_to_bus(newskb->tail));
			rx_in_place = 1;
		    } else
			skb = NULL;
		} else
		    skb = dev_alloc_skb(pkt_len+2);
			    
		if (skb == NULL) {
		    printk("%s: Memory squeeze, deferring packet.\n", dev->name);
		    for (i=0; i < RX_RING_SIZE; i++)
			if ((short)le16_to_cpu(lp->rx_ring[(entry+i) & RX_RING_MOD_MASK].status) < 0)
			    break;

		    if (i > RX_RING_SIZE -2) {
			lp->stats.rx_dropped++;
			lp->rx_ring[entry].status |= le16_to_cpu(0x8000);
			lp->cur_rx++;
		    }
		    break;
		}
		skb->dev = dev;
		if (!rx_in_place) {
		    skb_reserve(skb,2);	/* 16 byte align */
		    skb_put(skb,pkt_len);	/* Make room */
		    eth_copy_and_sum(skb,
				     (unsigned char *)bus_to_virt(le32_to_cpu(lp->rx_ring[entry].base)),
				     pkt_len,0);
		}
		lp->stats.rx_bytes += skb->len;
		skb->protocol=eth_type_trans(skb,dev);
		netif_rx(skb);
		lp->stats.rx_packets++;
	    }
	}
	/*
	 * The docs say that the buffer length isn't touched, but Andrew Boyd
	 * of QNX reports that some revs of the 79C965 clear it.
	 */
	lp->rx_ring[entry].buf_length = le16_to_cpu(-PKT_BUF_SZ);
	lp->rx_ring[entry].status |= le16_to_cpu(0x8000);
	entry = (++lp->cur_rx) & RX_RING_MOD_MASK;
    }

    return 0;
}

static int
pcnet32_close(struct device *dev)
{
    unsigned long ioaddr = dev->base_addr;
    struct pcnet32_private *lp = (struct pcnet32_private *)dev->priv;
    int i;

    dev->start = 0;
    set_bit (0, (void *)&dev->tbusy);

    lp->stats.rx_missed_errors = lp->a.read_csr (ioaddr, 112);

    if (pcnet32_debug > 1)
	printk("%s: Shutting down ethercard, status was %2.2x.\n",
	       dev->name, lp->a.read_csr (ioaddr, 0));

    /* We stop the PCNET32 here -- it occasionally polls memory if we don't. */
    lp->a.write_csr (ioaddr, 0, 0x0004);

    /*
     * Switch back to 16bit mode to avoid problems with dumb 
     * DOS packet driver after a warm reboot
     */
    lp->a.write_bcr (ioaddr, 20, 4);

    free_irq(dev->irq, dev);
    
    /* free all allocated skbuffs */
    for (i = 0; i < RX_RING_SIZE; i++) {
	lp->rx_ring[i].status = 0;	    	    	    
	if (lp->rx_skbuff[i])
	    dev_kfree_skb(lp->rx_skbuff[i]);
	lp->rx_skbuff[i] = NULL;
    }
    
    for (i = 0; i < TX_RING_SIZE; i++) {
	if (lp->tx_skbuff[i])
	    dev_kfree_skb(lp->tx_skbuff[i]);
	lp->rx_skbuff[i] = NULL;
    }
    
    MOD_DEC_USE_COUNT;

    return 0;
}

static struct net_device_stats *
pcnet32_get_stats(struct device *dev)
{
    struct pcnet32_private *lp = (struct pcnet32_private *)dev->priv;
    unsigned long ioaddr = dev->base_addr;
    u16 saved_addr;
    unsigned long flags;

    save_flags(flags);
    cli();
    saved_addr = lp->a.read_rap(ioaddr);
    lp->stats.rx_missed_errors = lp->a.read_csr (ioaddr, 112);
    lp->a.write_rap(ioaddr, saved_addr);
    restore_flags(flags);

    return &lp->stats;
}

/* taken from the sunlance driver, which it took from the depca driver */
static void pcnet32_load_multicast (struct device *dev)
{
    struct pcnet32_private *lp = (struct pcnet32_private *) dev->priv;
    volatile struct pcnet32_init_block *ib = &lp->init_block;
    volatile u16 *mcast_table = (u16 *)&ib->filter;
    struct dev_mc_list *dmi=dev->mc_list;
    char *addrs;
    int i, j, bit, byte;
    u32 crc, poly = CRC_POLYNOMIAL_LE;
	
    /* set all multicast bits */
    if (dev->flags & IFF_ALLMULTI){ 
	ib->filter [0] = 0xffffffff;
	ib->filter [1] = 0xffffffff;
	return;
    }
    /* clear the multicast filter */
    ib->filter [0] = 0;
    ib->filter [1] = 0;

    /* Add addresses */
    for (i = 0; i < dev->mc_count; i++){
	addrs = dmi->dmi_addr;
	dmi   = dmi->next;
	
	/* multicast address? */
	if (!(*addrs & 1))
	    continue;
	
	crc = 0xffffffff;
	for (byte = 0; byte < 6; byte++)
	    for (bit = *addrs++, j = 0; j < 8; j++, bit >>= 1) {
		int test;
		
		test = ((bit ^ crc) & 0x01);
		crc >>= 1;
		
		if (test) {
		    crc = crc ^ poly;
		}
	    }
	
	crc = crc >> 26;
	mcast_table [crc >> 4] |= 1 << (crc & 0xf);
    }
    return;
}


/*
 * Set or clear the multicast filter for this adaptor.
 */
static void pcnet32_set_multicast_list(struct device *dev)
{
    unsigned long ioaddr = dev->base_addr;
    struct pcnet32_private *lp = (struct pcnet32_private *)dev->priv;    

    if (dev->flags&IFF_PROMISC) {
	/* Log any net taps. */
	printk("%s: Promiscuous mode enabled.\n", dev->name);
	lp->init_block.mode = le16_to_cpu(0x8000 | (lp->options & PORT_PORTSEL) << 7);
    } else {
	lp->init_block.mode = le16_to_cpu((lp->options & PORT_PORTSEL) << 7);
	pcnet32_load_multicast (dev);
    }
    
    lp->a.write_csr (ioaddr, 0, 0x0004); /* Temporarily stop the lance. */

    pcnet32_restart(dev, 0x0042); /*  Resume normal operation */
}

#ifdef HAVE_PRIVATE_IOCTL
static int pcnet32_mii_ioctl(struct device *dev, struct ifreq *rq, int cmd)
{
    unsigned long ioaddr = dev->base_addr;
    struct pcnet32_private *lp = (struct pcnet32_private *)dev->priv;    
    u16 *data = (u16 *)&rq->ifr_data;
    int phyaddr = lp->a.read_bcr (ioaddr, 33);

    if (lp->mii) {
	switch(cmd) {
	 case SIOCDEVPRIVATE:            /* Get the address of the PHY in use. */
	    data[0] = (phyaddr >> 5) & 0x1f;
	    /* Fall Through */
	 case SIOCDEVPRIVATE+1:          /* Read the specified MII register. */
	    lp->a.write_bcr (ioaddr, 33, ((data[0] & 0x1f) << 5) | (data[1] & 0x1f));
	    data[3] = lp->a.read_bcr (ioaddr, 34);
	    lp->a.write_bcr (ioaddr, 33, phyaddr);
	    return 0;
	 case SIOCDEVPRIVATE+2:          /* Write the specified MII register */
	    if (!suser())
		return -EPERM;
	    lp->a.write_bcr (ioaddr, 33, ((data[0] & 0x1f) << 5) | (data[1] & 0x1f));
	    lp->a.write_bcr (ioaddr, 34, data[2]);
	    lp->a.write_bcr (ioaddr, 33, phyaddr);
	    return 0;
	 default:
	    return -EOPNOTSUPP;
	}
    }
    return -EOPNOTSUPP;
}
#endif  /* HAVE_PRIVATE_IOCTL */
					    
#ifdef MODULE
MODULE_PARM(debug, "i");
MODULE_PARM(max_interrupt_work, "i");
MODULE_PARM(rx_copybreak, "i");
MODULE_PARM(options, "1-" __MODULE_STRING(MAX_UNITS) "i");
MODULE_PARM(full_duplex, "1-" __MODULE_STRING(MAX_UNITS) "i");
					     

/* An additional parameter that may be passed in... */
static int debug = -1;

int
init_module(void)
{
    if (debug > 0)
	pcnet32_debug = debug;
    
    pcnet32_dev = NULL;
    return pcnet32_probe(NULL);
}

void
cleanup_module(void)
{
    struct device *next_dev;

    /* No need to check MOD_IN_USE, as sys_delete_module() checks. */
    while (pcnet32_dev) {
	next_dev = ((struct pcnet32_private *) pcnet32_dev->priv)->next;
	unregister_netdev(pcnet32_dev);
	release_region(pcnet32_dev->base_addr, PCNET32_TOTAL_SIZE);
	kfree(((struct pcnet32_private *)pcnet32_dev->priv)->origmem);
	kfree(pcnet32_dev);
	pcnet32_dev = next_dev;
    }
}
#endif /* MODULE */



/*
 * Local variables:
 *  compile-command: "gcc -D__KERNEL__ -I/usr/src/linux/net/inet -Wall -Wstrict-prototypes -O6 -m486 -c pcnet32.c"
 *  c-indent-level: 4
 *  tab-width: 4
 * End:
 */
