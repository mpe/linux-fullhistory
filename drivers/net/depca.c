/*  depca.c: A DIGITAL DEPCA  & EtherWORKS ethernet driver for linux.

    Written 1994 by David C. Davies.


                      Copyright 1994 David C. Davies
		                   and 
			 United States Government
	 (as represented by the Director, National Security Agency).  


    This software may be used and distributed according to the terms of
    the GNU Public License, incorporated herein by reference.

    This driver is written for the Digital Equipment Corporation series
    of DEPCA and EtherWORKS ethernet cards:

        DEPCA       (the original)
    	DE100
    	DE101
	DE200 Turbo
	DE201 Turbo
	DE202 Turbo (TP BNC)
	DE210
	DE422       (EISA)

    The  driver has been tested on DE100, DE200 and DE202 cards  in  a
    relatively busy network. The DE422 has been tested a little.

    This  driver will NOT work   for the DE203,  DE204  and DE205 series  of
    cards,  since they have  a  new custom ASIC in   place of the AMD  LANCE
    chip. 

    The author may be reached as davies@wanton.lkg.dec.com or
    Digital Equipment Corporation, 550 King Street, Littleton MA 01460.

    =========================================================================
    The driver was based on the 'lance.c' driver from Donald Becker which is
    included with the standard driver distribution for linux. Modifications
    were made to most routines and the hardware recognition routines were
    written from scratch. Primary references used were:

    1) Lance.c code in /linux/drivers/net/
    2) "Ethernet/IEEE 802.3 Family. 1992 World Network Data Book/Handbook",
       AMD, 1992 [(800) 222-9323].
    3) "Am79C90 CMOS Local Area Network Controller for Ethernet (C-LANCE)",
       AMD, Pub. #17881, May 1993.
    4) "Am79C960 PCnet-ISA(tm), Single-Chip Ethernet Controller for ISA",
       AMD, Pub. #16907, May 1992
    5) "DEC EtherWORKS LC Ethernet Controller Owners Manual",
       Digital Equipment corporation, 1990, Pub. #EK-DE100-OM.003
    6) "DEC EtherWORKS Turbo Ethernet Controller Owners Manual",
       Digital Equipment corporation, 1990, Pub. #EK-DE200-OM.003
    7) "DEPCA Hardware Reference Manual", Pub. #EK-DEPCA-PR
       Digital Equipment Corporation, 1989
    8) "DEC EtherWORKS Turbo_(TP BNC) Ethernet Controller Owners Manual",
       Digital Equipment corporation, 1991, Pub. #EK-DE202-OM.001
    
    Peter Bauer's depca.c (V0.5) was referred to when debugging this driver.
    The hash filter code was  derived from Reference  3 and has been  tested
    only to the extent that the Table  A-1, page A-7,  was confirmed to fill
    the   filter bit   positions  correctly.  Hash   filtering  is  not  yet
    implemented in the current driver set.

    The original DEPCA  card requires that the  ethernet ROM address counter
    be enabled to count and has an 8 bit NICSR.  The ROM counter enabling is
    only  done when a  0x08 is read as the  first address octet (to minimise
    the chances  of writing over some  other hardware's  I/O register).  The
    NICSR accesses   have been changed  to  byte accesses  for all the cards
    supported by this driver, since there is only one  useful bit in the MSB
    (remote boot timeout) and it  is not used.  Also, there  is a maximum of
    only 48kB network  RAM for this  card.  My thanks  to Torbjorn Lindh for
    help debugging all this (and holding my feet to  the fire until I got it
    right).

    The DE200  series  boards have  on-board 64kB  RAM for  use  as a shared
    memory network  buffer. Only the DE100  cards make use  of a  2kB buffer
    mode which has not  been implemented in  this driver (only the 32kB  and
    64kB modes are supported [16kB/48kB for the original DEPCA]).

    At the most only 2 DEPCA cards can  be supported on  the ISA bus because
    there is only provision  for two I/O base addresses  on each card (0x300
    and 0x200). The I/O address is detected by searching for a byte sequence
    in the Ethernet station address PROM at the expected I/O address for the
    Ethernet  PROM.   The shared memory  base   address  is 'autoprobed'  by
    looking  for the self  test PROM  and detecting the  card name.   When a
    second  DEPCA is  detected,  information  is   placed in the   base_addr
    variable of the  next device structure (which  is created if necessary),
    thus  enabling ethif_probe  initialization  for the device.  More than 2
    EISA cards can  be  supported, but  care will  be  needed assigning  the
    shared memory to ensure that each slot has the  correct IRQ, I/O address
    and shared memory address assigned.

    ************************************************************************

    NOTE: If you are using two  ISA DEPCAs, it is  important that you assign
    the base memory addresses correctly.   The  driver autoprobes I/O  0x300
    then 0x200.  The  base memory address for  the first device must be less
    than that of the second so that the auto probe will correctly assign the
    I/O and memory addresses on the same card.  I can't think of a way to do
    this unambiguously at the moment, since there is nothing on the cards to
    tie I/O and memory information together.

    I am unable  to  test  2 cards   together for now,    so this  code   is
    unchecked. All reports, good or bad, are welcome.

    ************************************************************************

    The board IRQ   setting must be  at an  unused IRQ which  is auto-probed
    using Donald Becker's autoprobe routines. DEPCA and DE100 board IRQs are
    {2,3,4,5,7}, whereas the  DE200 is at {5,9,10,11,15}.  Note that IRQ2 is
    really IRQ9 in machines with 16 IRQ lines.

    No 16MB memory  limitation should exist with this  driver as DMA is  not
    used and the common memory area is in low memory on the network card (my
    current system has 20MB and I've not had problems yet).

    The ability to load this driver as a loadable module has been added. To
    utilise this ability, you have to do <8 things:

    1) copy depca.c from the  /linux/drivers/net directory to your favourite
    temporary directory.
    2) edit the  source code near  line 1530 to reflect  the I/O address and
    IRQ you're using.
    3) compile  depca.c, but include -DMODULE in  the command line to ensure
    that the correct bits are compiled (see end of source code).
    4) if you are wanting to add a new  card, goto 5. Otherwise, recompile a
    kernel with the depca configuration turned off and reboot.
    5) insmod depca.o
    6) run the net startup bits for your eth?? interface manually 
    (usually /etc/rc.inet[12] at boot time). 
    7) enjoy!

    Note that autoprobing is not allowed in loadable modules - the system is
    already up and running and you're messing with interrupts. Also, there
    is no way to check on the number of depcas installed at the moment.

    To unload a module, turn off the associated interface 
    'ifconfig eth?? down' then 'rmmod depca'.

    [Alan Cox: Changed to split off the module values as ints for insmod
     
     you can now do insmod depca.c irq=7 io=0x200 ]
     

    TO DO:
    ------

    1. Implement the 2k buffer mode - does anyone need it??

    Revision History
    ----------------

    Version   Date        Description
  
      0.1     25-jan-94   Initial writing.
      0.2     27-jan-94   Added LANCE TX hardware buffer chaining.
      0.3      1-feb-94   Added multiple DEPCA support.
      0.31     4-feb-94   Added DE202 recognition.
      0.32    19-feb-94   Tidy up. Improve multi-DEPCA support.
      0.33    25-feb-94   Fix DEPCA ethernet ROM counter enable.
                          Add jabber packet fix from murf@perftech.com
			  and becker@super.org
      0.34     7-mar-94   Fix DEPCA max network memory RAM & NICSR access.
      0.35     8-mar-94   Added DE201 recognition. Tidied up.
      0.351   30-apr-94   Added EISA support. Added DE422 recognition.
      0.36    16-may-94   DE422 fix released.
      0.37    22-jul-94   Added MODULE support
      0.38    15-aug-94   Added DBR ROM switch in depca_close(). 
                          Multi DEPCA bug fix.
      0.38axp 15-sep-94   Special version for Alpha AXP Linux V1.0.
      0.381   12-dec-94   Added DE101 recognition, fix multicast bug.
      0.382    9-feb-95   Fix recognition bug reported by <bkm@star.rl.ac.uk>.
      0.383   22-feb-95   Fix for conflict with VESA SCSI reported by
                          <stromain@alf.dec.com>

    =========================================================================
*/

static char *version = "depca.c:v0.383 2/22/94 davies@wanton.lkg.dec.com\n";

#include <linux/config.h>
#ifdef MODULE
#include <linux/module.h>
#include <linux/version.h>
#else
#define MOD_INC_USE_COUNT
#define MOD_DEC_USE_COUNT
#endif /* MODULE */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <linux/interrupt.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include "depca.h"

#ifdef DEPCA_DEBUG
static int depca_debug = DEPCA_DEBUG;
#else
static int depca_debug = 1;
#endif

#ifndef PROBE_LENGTH
#define PROBE_LENGTH    32
#endif

#define ETH_PROM_SIG    0xAA5500FFUL

#ifndef DEPCA_SIGNATURE
#define DEPCA_SIGNATURE {"DEPCA",\
			 "DE100","DE101",\
                         "DE200","DE201","DE202",\
			 "DE210",\
                         "DE422",\
                         ""}
#define DEPCA_NAME_LENGTH 8
#endif

#ifndef DEPCA_RAM_BASE_ADDRESSES
#define DEPCA_RAM_BASE_ADDRESSES {0xc0000,0xd0000,0xe0000,0x00000}
#endif
static short mem_chkd = 0;               /* holds which base addrs have been */
					 /* checked, for multi-DEPCA case */

#ifndef DEPCA_IO_PORTS
#define DEPCA_IO_PORTS {0x300, 0x200, 0}
#endif

#ifndef DEPCA_TOTAL_SIZE
#define DEPCA_TOTAL_SIZE 0x10
#endif

#ifndef MAX_NUM_DEPCAS
#define MAX_NUM_DEPCAS 2
#endif

#ifndef DEPCA_EISA_IO_PORTS 
#define DEPCA_EISA_IO_PORTS 0x0c00       /* I/O port base address, slot 0 */
#endif

#ifndef MAX_EISA_SLOTS
#define MAX_EISA_SLOTS 8
#endif

/*
** Set the number of Tx and Rx buffers. 
*/
#ifndef DEPCA_BUFFER_LOG_SZ
#define RING_SIZE	16              /* 16 buffers */
#else
#define RING_SIZE	(1 << (DEPCA_BUFFERS_LOG_SZ))
#endif  /* DEPCA_BUFFER_LOG_SZ */

#define PKT_BUF_SZ	1544            /* Buffer size for each Tx/Rx buffer */
#define PKT_SZ   	1514            /* Maximum ethernet packet length */
#define DAT_SZ   	1500            /* Maximum ethernet data length */
#define PKT_HDR_LEN     14              /* Addresses and data length info */

#ifdef HAVE_MULTICAST
#ifndef CRC_POLYNOMIAL
#define CRC_POLYNOMIAL 0x04c11db7       /* Ethernet CRC polynomial */
#endif /* CRC_POLYNOMIAL */
#endif /* HAVE_MULTICAST */

/*
** The DEPCA Rx and Tx ring descriptors. 
*/
struct depca_rx_head {
    volatile long base;
    short buf_length;		/* This length is negative 2's complement! */
    short msg_length;		/* This length is "normal". */
};

struct depca_tx_head {
    volatile long base;
    short length;		/* This length is negative 2's complement! */
    short misc;                 /* Errors and TDR info */
};

#define LA_MASK 0x0000ffff      /* LANCE address mask for mapping network RAM
				   to LANCE memory address space */

/*
** The Lance initialization block, described in databook, in common memory.
*/
struct depca_init {
    unsigned short mode;	/* Mode register */
    unsigned char phys_addr[ETH_ALEN];	/* Physical ethernet address */
    unsigned short filter[4];	/* Multicast filter. */
    unsigned long rx_ring;     	/* Rx ring base pointer & ring length */
    unsigned long tx_ring;	/* Tx ring base pointer & ring length */
};

struct depca_private {
    char devname[8];            /* Device Product String */
    struct depca_rx_head *rx_ring; /* Pointer to start of RX descriptor ring */
    struct depca_tx_head *tx_ring; /* Pointer to start of TX descriptor ring */
    struct depca_init	init_block;/* Initialization block */
    long bus_offset;            /* (E)ISA bus address offset vs LANCE */
    long dma_buffs;		/* Start address of Rx and Tx buffers. */
    int	cur_rx, cur_tx;		/* The next free ring entry */
    int dirty_rx, dirty_tx;	/* The ring entries to be free()ed. */
    int dma;
    struct enet_statistics stats;
    char depca_na;              /* NICSR access width: 0=>byte, 1=>word */
    short ringSize;             /* ring size based on available memory */
    short rmask;                /* modulus mask based on ring size */
    long rlen;                  /* log2(ringSize) for the descriptors */
};

/*
** Public Functions
*/
static int depca_open(struct device *dev);
static int depca_start_xmit(struct sk_buff *skb, struct device *dev);
static void depca_interrupt(int irq, struct pt_regs * regs);
static int depca_close(struct device *dev);
static struct enet_statistics *depca_get_stats(struct device *dev);
#ifdef HAVE_MULTICAST
static void set_multicast_list(struct device *dev, int num_addrs, void *addrs);
#endif

/*
** Private functions
*/
static int  depca_probe1(struct device *dev, short ioaddr);
static void depca_init_ring(struct device *dev);
static int  depca_rx(struct device *dev);
static int  depca_tx(struct device *dev);

static void LoadCSRs(struct device *dev);
static int  InitRestartDepca(struct device *dev);
static char *DepcaSignature(unsigned long mem_addr);
static int  DevicePresent(short ioaddr);
static int  EISA_signature(short iobase);
#ifdef HAVE_MULTICAST
static void SetMulticastFilter(int num_addrs, char *addrs, char *multicast_table);
#endif

#ifndef MODULE
static struct device *isa_probe(struct device *dev);
static struct device *eisa_probe(struct device *dev);
static struct device *alloc_device(struct device *dev, int ioaddr);

static int num_depcas = 0, num_eth = 0, autoprobed = 0;

#else
int  init_module(void);
void cleanup_module(void);

#endif /* MODULE */

/*
** Miscellaneous defines...
*/
#define STOP_DEPCA \
    outw(CSR0, DEPCA_ADDR);\
    outw(STOP, DEPCA_DATA)




int depca_probe(struct device *dev)
{
    short base_addr = dev->base_addr;
    int status = -ENODEV;
#ifndef MODULE
    struct device *eth0;
#endif

    if (base_addr > 0x1ff) {	      /* Check a single specified location. */
      if (DevicePresent(base_addr) == 0) { /* Is DEPCA really here? */
	status = depca_probe1(dev, base_addr);
      }
    } else if (base_addr > 0) {	      /* Don't probe at all. */
      status = -ENXIO;

#ifdef MODULE
    } else {
      printk("Autoprobing is not supported when loading a module based driver.\n");
      status = -EIO;
#else
    } else if (!autoprobed) {         /* First probe for the DEPCA test */
                                      /* pattern in ROM */
      eth0=isa_probe(dev);
      eth0=eisa_probe(eth0);
      if (dev->priv) status=0;
      autoprobed = 1;
    } else {
      status = -ENXIO;
#endif /* MODULE */

    }

    if (status) dev->base_addr = base_addr;

    return status;			/* ENODEV would be more accurate. */
}

static int
depca_probe1(struct device *dev, short ioaddr)
{
    struct depca_private *lp;
    int i,j, status=0;
    unsigned long mem_start, mem_base[] = DEPCA_RAM_BASE_ADDRESSES;
    char *name = NULL;
    unsigned int nicsr, offset, netRAM;


    /*
    ** Stop the DEPCA. Enable the DBR ROM. Disable interrupts and remote boot.
    */
    STOP_DEPCA;

    nicsr = inb(DEPCA_NICSR);
    nicsr = ((nicsr & ~SHE & ~RBE & ~IEN) | IM);
    outb(nicsr, DEPCA_NICSR);

    if (inw(DEPCA_DATA) == STOP) {

    /* Now find out what kind of DEPCA we have. The DE100 uses a different
    ** addressing scheme for some registers compared to the DE2xx series.
    ** Note that a base address location is marked as checked if no DEPCA is
    ** there or one is found (when the search is immediately terminated). This
    ** shortens the search time a little for multiple DEPCAs.
    */

      for (j = 0, i = 0; mem_base[i] && (j == 0);i++) {
	if (((mem_chkd >> i) & 0x01) == 0) { /* has the memory been checked? */
	  name = DepcaSignature(mem_base[i]);/* check for a DEPCA here */
	  mem_chkd |= (0x01 << i);           /* mark location checked */
	  if (*name != '\0') {               /* one found? */
	    j = 1;                           /* set exit flag */
	    --i;
	  }
	}
      }

      if (*name != '\0') {                   /* found a DEPCA device */
	mem_start = mem_base[i];
	dev->base_addr = ioaddr;

	if ((ioaddr&0x0fff)==DEPCA_EISA_IO_PORTS) {/* EISA slot address */
	  printk("%s: %s at %#3x (EISA slot %d)", 
		                 dev->name, name, ioaddr, ((ioaddr>>12)&0x0f));
	} else {                             /* ISA port address */
	  printk("%s: %s at %#3x", dev->name, name, ioaddr);
	}

      /* There is a 32 byte station address PROM at DEPCA_PROM address.
	 The first six bytes are the station address. They can be read
	 directly since the signature search set up the ROM address 
	 counter correctly just before this function.

	 For the DE100 we have to be careful about which port is used to
	 read the ROM info.
      */

	if (strstr(name,"DE100")!= NULL) {
	  j = 1;
	} else {
	  j = 0;
	}

	printk(", h/w address ");
	for (i = 0; i < ETH_ALEN - 1; i++) { /* get the ethernet address */
	  printk("%2.2x:", dev->dev_addr[i] = inb(DEPCA_PROM + j));
	}
	printk("%2.2x", dev->dev_addr[i] = inb(DEPCA_PROM + j));

	for (;i<32;i++) {                /* leave ROM counter in known state */
	  j=inb(DEPCA_PROM);
	}

	request_region(ioaddr, DEPCA_TOTAL_SIZE, dev->name);

	/*
	** Set up the maximum amount of network RAM(kB)
	*/
	if (strstr(name,"DEPCA")== NULL) {
	  netRAM=64;
	} else {
	  netRAM=48;
	}

	/* 
	** Determine the base address for the DEPCA RAM from the NI-CSR
	** and make up a DEPCA-specific-data structure. 
        */

	if (nicsr & BUF) {
	  offset = 0x8000;              /* 32kbyte RAM offset*/
	  nicsr &= ~BS;                 /* DEPCA RAM in top 32k */
	  printk(",\n      has %dkB RAM", netRAM - 32);
	} else 	if ((nicsr & _128KB) && (netRAM!=48)) {
	  offset = 0x0000;
	  printk(",\n      has 128kB RAM");
	} else {
	  offset = 0x0000;              /* 64k/48k bytes RAM */
	  printk(",\n      has %dkB RAM", netRAM);
	}

	mem_start += offset;            /* (E)ISA start address */
	printk(" at 0x%.5lx", mem_start);

	/*
	** Enable the shadow RAM.
	*/
	if (strstr(name,"DEPCA") == NULL) {
	  nicsr |= SHE;
	  outb(nicsr, DEPCA_NICSR);
	}
 
	/*
	** Calculate the ring size based on the available RAM
	** found above. Allocate an equal number of buffers, each
	** of size PKT_BUF_SZ (1544 bytes) to the Tx and Rx, allowing one
	** descriptor entry (8 bytes) for each buffer. Make sure
	** that this ring size is <= RING_SIZE. The ring size must be
	** a power of 2.
	*/

	j = (((netRAM << 10) - offset - sizeof(struct depca_private)) / 
	                                               (PKT_BUF_SZ + 8)) >> 1;
	for (i=0;j>1;i++) {
	  j >>= 1;
	}

	/* Hold the ring size information here before the depca
	** private structure is allocated. Need this for the memory
	** space calculations.
	*/
	j = 1 << i;

	/*
	** Set up memory information in the device structure.
	** Align the descriptor rings on an 8 byte (quadword) boundary.
	**
	**     depca_private area
	**     rx ring descriptors
	**     tx ring descriptors
	**     rx buffers
	**     tx buffers
	**
	*/

	/* private area & initialise */
	dev->priv = (void *)((mem_start + 0x07) & ~0x07);      
	lp = (struct depca_private *)dev->priv;
	memset(dev->priv, 0, sizeof(struct depca_private));
	strcpy(lp->devname,name);

	/* Tx & Rx descriptors (aligned to a quadword boundary) */
	mem_start = ((((unsigned long)dev->priv + 
		        sizeof(struct depca_private)) +
			(unsigned long)0x07) & (unsigned long)~0x07);
	lp->rx_ring = (struct depca_rx_head *)mem_start;

	mem_start += (sizeof(struct depca_rx_head) * j);
	lp->tx_ring = (struct depca_tx_head *)mem_start;

	mem_start += (sizeof(struct depca_tx_head) * j);
	lp->bus_offset = mem_start & 0x00ff0000;
	mem_start &= LA_MASK;           /* LANCE re-mapped start address */

	lp->dma_buffs = mem_start;

	mem_start += (PKT_BUF_SZ * j);
	/* (mem_start now points to the start of the Tx buffers) */

	/* Initialise the data structures wrt CPU */
	memset(lp->rx_ring, 0, sizeof(struct depca_rx_head)*j);
	memset(lp->tx_ring, 0, sizeof(struct depca_tx_head)*j);

	/* This should never happen. */
	if ((long)(lp->rx_ring) & 0x07) {
	  printk("\n **ERROR** DEPCA Rx and Tx descriptor rings not on a quadword boundary.\n");
	  return -ENXIO;
	}

	/*
	** Finish initialising the ring information.
	*/
	lp->ringSize = j;
	if (lp->ringSize > RING_SIZE) lp->ringSize = RING_SIZE;
	lp->rmask = lp->ringSize - 1;

	/*
	** calculate the real RLEN size for the descriptors. It is
	** log2(ringSize).
	*/
	for (i=0, j = lp->ringSize; j>1; i++) {
	  j >>= 1;
	}
	lp->rlen = (unsigned long)(i << 29);

	/*
	** load the initialisation block
	*/
	depca_init_ring(dev);

	/*
	** Initialise the control and status registers
	*/
	LoadCSRs(dev);

	/*
	** Enable DEPCA board interrupts for autoprobing
	*/
	nicsr = ((nicsr & ~IM)|IEN);
	outb(nicsr, DEPCA_NICSR);

	/* The DMA channel may be passed in on this parameter. */
	dev->dma = 0;

	/* To auto-IRQ we enable the initialization-done and DMA err,
	 interrupts. For now we will always get a DMA error. */
	if (dev->irq < 2) {
#ifndef MODULE
	  autoirq_setup(0);

	  /* Trigger an initialization just for the interrupt. */
	  outw(INEA | INIT, DEPCA_DATA);
	  
	  dev->irq = autoirq_report(1);
	  if (dev->irq) {
	    printk(" and uses IRQ%d.\n", dev->irq);
	  } else {
	    printk(" and failed to detect IRQ line.\n");
	    status = -EAGAIN;
	  }
#endif /* MODULE */
	} else {
	  printk(" and assigned IRQ%d.\n", dev->irq);
	}
      } else {
	status = -ENXIO;
      }
      if (!status) {
	if (depca_debug > 0) {
	  printk(version);
	}

	/* The DEPCA-specific entries in the device structure. */
	dev->open = &depca_open;
	dev->hard_start_xmit = &depca_start_xmit;
	dev->stop = &depca_close;
	dev->get_stats = &depca_get_stats;
#ifdef HAVE_MULTICAST
	dev->set_multicast_list = &set_multicast_list;
#endif

	dev->mem_start = 0;
	
	/* Fill in the generic field of the device structure. */
 	ether_setup(dev);
      }
    } else {
      status = -ENXIO;
    }

    return status;
}


static int
depca_open(struct device *dev)
{
    struct depca_private *lp = (struct depca_private *)dev->priv;
    int i,nicsr,ioaddr = dev->base_addr;

    if (request_irq(dev->irq, &depca_interrupt, 0, "depca")) {
        printk("depca_open(): Requested IRQ%d is busy\n",dev->irq);
	return -EAGAIN;
    }

    irq2dev_map[dev->irq] = dev;

    /*
    ** Stop the DEPCA & get the board status information.  
    */
    STOP_DEPCA;
    nicsr = inb(DEPCA_NICSR);

    /*
    ** Make sure the shadow RAM is enabled
    */
    if (strstr(lp->devname,"DEPCA") == NULL) {
      nicsr |= SHE;
      outb(nicsr, DEPCA_NICSR);
    }

    /* 
    ** Re-initialize the DEPCA... 
    */
    depca_init_ring(dev);                 /* initialize the descriptor rings */
    LoadCSRs(dev);

    if (depca_debug > 1){
      printk("%s: depca open with irq %d\n",dev->name,dev->irq);
      printk("Descriptor head addresses:\n");
      printk("\t0x%8.8lx  0x%8.8lx\n",(long)lp->rx_ring,(long)lp->tx_ring);
      printk("Descriptor addresses:\n");
      for (i=0;i<lp->ringSize;i++){
	printk("\t0x%8.8lx  0x%8.8lx\n",(long)&lp->rx_ring[i].base,
	                                (long)&lp->tx_ring[i].base);
      }
      printk("Buffer addresses:\n");
      for (i=0;i<lp->ringSize;i++){
	printk("\t0x%8.8lx  0x%8.8lx\n",(long)lp->rx_ring[i].base,
                                        (long)lp->tx_ring[i].base);
      }
      printk("Initialisation block at 0x%8.8lx\n",(long)&lp->init_block);
      printk("\tmode: 0x%4.4x\n",lp->init_block.mode);
      printk("\tphysical address: ");
      for (i=0;i<6;i++){
	printk("%2.2x:",(short)lp->init_block.phys_addr[i]);
      }
      printk("\n\tlogical address filter: 0x");
      for (i=0;i<4;i++){
	printk("%2.2x",(short)lp->init_block.filter[i]);
      }
      printk("\n\trx_ring at: 0x%8.8lx\n",(long)lp->init_block.rx_ring);
      printk("\ttx_ring at: 0x%8.8lx\n",(long)lp->init_block.tx_ring);
      printk("dma_buffs: 0x%8.8lx\n",(long)lp->dma_buffs);
      printk("Ring size: %d\nMask: 0x%2.2x\nLog2(ringSize): 0x%8.8lx\n", 
                                         (short)lp->ringSize, 
                                          (char)lp->rmask,
                                          (long)lp->rlen);
      outw(CSR2,DEPCA_ADDR);
      printk("CSR2&1: 0x%4.4x",inw(DEPCA_DATA));
      outw(CSR1,DEPCA_ADDR);
      printk("%4.4x\n",inw(DEPCA_DATA));
      outw(CSR3,DEPCA_ADDR);
      printk("CSR3: 0x%4.4x\n",inw(DEPCA_DATA));
    }

    /*
    ** Enable DEPCA board interrupts and turn off LED
    */
    nicsr = ((nicsr & ~IM & ~LED)|IEN);
    outb(nicsr, DEPCA_NICSR);
    outw(CSR0,DEPCA_ADDR);

    dev->tbusy = 0;                         
    dev->interrupt = 0;
    dev->start = 1;

    InitRestartDepca(dev);                /* ignore the return status */

    if (depca_debug > 1){
      printk("CSR0: 0x%4.4x\n",inw(DEPCA_DATA));
      printk("nicsr: 0x%02x\n",inb(DEPCA_NICSR));
    }

    MOD_INC_USE_COUNT;

    return 0;			          /* Always succeed */
}

/* Initialize the lance Rx and Tx descriptor rings. */
static void
depca_init_ring(struct device *dev)
{
    struct depca_private *lp = (struct depca_private *)dev->priv;
    unsigned long i;

    lp->init_block.mode = DTX | DRX;	     /* Disable Rx and Tx. */
    lp->cur_rx = lp->cur_tx = 0;
    lp->dirty_rx = lp->dirty_tx = 0;

    /* Initialize the base addresses and length of each buffer in the ring */
    for (i = 0; i < lp->ringSize; i++) {
	lp->rx_ring[i].base = (lp->dma_buffs + i*PKT_BUF_SZ) | R_OWN;
	lp->rx_ring[i].buf_length = -PKT_BUF_SZ;
	lp->tx_ring[i].base = (lp->dma_buffs + (i+lp->ringSize) * PKT_BUF_SZ) &
	                                           (unsigned long)(0x00ffffff);
    }

    /* Set up the initialization block */
    for (i = 0; i < ETH_ALEN; i++) {
      lp->init_block.phys_addr[i] = dev->dev_addr[i];
    }
    for (i = 0; i < 4; i++) {
      lp->init_block.filter[i] = 0x0000;
    }
    lp->init_block.rx_ring = ((unsigned long)lp->rx_ring & LA_MASK) | lp->rlen;
    lp->init_block.tx_ring = ((unsigned long)lp->tx_ring & LA_MASK) | lp->rlen;

    lp->init_block.mode = 0x0000;            /* Enable the Tx and Rx */ 
}

/* 
** Writes a socket buffer to TX descriptor ring and starts transmission 
*/
static int
depca_start_xmit(struct sk_buff *skb, struct device *dev)
{
    struct depca_private *lp = (struct depca_private *)dev->priv;
    int ioaddr = dev->base_addr;
    int status = 0;

    /* Transmitter timeout, serious problems. */
    if (dev->tbusy) {
      int tickssofar = jiffies - dev->trans_start;
      if (tickssofar < 10) {
	status = -1;
      } else {
	printk("%s: transmit timed out, status %04x, resetting.\n",
	       dev->name, inw(DEPCA_DATA));
	
	STOP_DEPCA;
	depca_init_ring(dev);
	LoadCSRs(dev);
	InitRestartDepca(dev);
	dev->tbusy=0;
	dev->trans_start = jiffies;
      }
      return status;
    }

    if (skb == NULL) {
	dev_tint(dev);
	return 0;
    }

    if (skb->len <= 0) {
      return 0;
    }

    if (depca_debug > 3) {
	outw(CSR0, DEPCA_ADDR);
	printk("%s: depca_start_xmit() called, csr0 %4.4x.\n", dev->name,
	       inw(DEPCA_DATA));
    }

    /* Block a timer-based transmit from overlapping.  This could better be
       done with atomic_swap(1, dev->tbusy), but set_bit() works as well. */
    if (set_bit(0, (void*)&dev->tbusy) != 0)
	printk("%s: Transmitter access conflict.\n", dev->name);

    /*
    ** The TX buffer, skb, has to be copied into the local network RAM
    ** for the LANCE to access it. The skb may be at > 16MB for large 
    ** (memory) systems.
    */
    {				/* Fill in a Tx ring entry */
      unsigned char *buf;
      int entry = lp->cur_tx++;
      int len;
      long skbL = skb->len;
      char *p = (char *) skb->data;

      entry &= lp->rmask;  		    /* Ring around buffer number. */
      buf = (unsigned char *)((lp->tx_ring[entry].base+lp->bus_offset) & 
			                                           0x00ffffff);

      /* Wait for a full ring to free up */
      while (lp->tx_ring[entry].base < 0);

      /* 
      ** Caution: the write order is important here... don't set up the
      ** ownership rights until all the other information is in place.
      */
      len = ((skbL > PKT_SZ) ? PKT_SZ : skbL); /* skb too long */
      if (len < ETH_ZLEN) len = ETH_ZLEN;      /* len too short */
      skbL -= len;
      lp->tx_ring[entry].length = -len;

      /* Clears various error flags */
      lp->tx_ring[entry].misc = 0x0000;

      /* copy the data from the socket buffer to the net memory */
      memcpy((unsigned char *)(buf), skb->data, len);

      /* Hand over buffer ownership to the LANCE */
      if (skbL <= 0) lp->tx_ring[entry].base |= (T_ENP);
      lp->tx_ring[entry].base |= (T_OWN|T_STP);

      /* Trigger an immediate send demand. */
      outw(CSR0, DEPCA_ADDR);
      outw(INEA | TDMD, DEPCA_DATA);

      dev->trans_start = jiffies;

      for (p += len; skbL > 0; p += len) {

	/* Get new buffer pointer */
	entry = lp->cur_tx++;
	entry &= lp->rmask;  		    /* Ring around buffer number. */
	buf = (unsigned char *)((lp->tx_ring[entry].base+lp->bus_offset) &
				                                   0x00ffffff);

	/* Wait for a full ring to free up */
	while (lp->tx_ring[entry].base < 0);
	dev->tbusy=0;

	/* Copy ethernet header to the new buffer */
	memcpy((unsigned char *)buf, skb->data, PKT_HDR_LEN);

	/* Determine length of data buffer */
	len = ((skbL > DAT_SZ) ? DAT_SZ : skbL); /* skbL too long */
	if (len < ETH_ZLEN) len = ETH_ZLEN;      /* len too short */
	skbL -= len;
	lp->tx_ring[entry].length = -len;

	/* Clears various error flags */
	lp->tx_ring[entry].misc = 0x0000;

	/* copy the data from the socket buffer to the net memory */
	memcpy((unsigned char *)(buf + PKT_HDR_LEN), (unsigned char *)p, len);

	/* Hand over buffer ownership to the LANCE */
	if (skbL <= 0) lp->tx_ring[entry].base |= T_ENP;
	lp->tx_ring[entry].base |= T_OWN;
      }

      if (depca_debug > 4) {
	unsigned char *pkt =
	  (unsigned char *)((lp->tx_ring[entry].base+lp->bus_offset) &
			                                           0x00ffffff);

	printk("%s: tx ring[%d], %#lx, sk_buf %#lx len %d.\n",
	       dev->name, entry, (unsigned long) &lp->tx_ring[entry],
	       lp->tx_ring[entry].base, -lp->tx_ring[entry].length);
	printk("%s:  Tx %2.2x %2.2x %2.2x ... %2.2x  %2.2x %2.2x %2.2x...%2.2x len %2.2x %2.2x  %2.2x %2.2x.\n",
	       dev->name, pkt[0], pkt[1], pkt[2], pkt[5], pkt[6],
	       pkt[7], pkt[8], pkt[11], pkt[12], pkt[13],
	       pkt[14], pkt[15]);
      }
      
      /* Check if the TX ring is full or not - 'tbusy' cleared if not full. */
      if (lp->tx_ring[(entry+1) & lp->rmask].base >= 0) {
	dev->tbusy=0;
      }

      dev_kfree_skb (skb, FREE_WRITE);
    }

    return 0;
}

/*
** The DEPCA interrupt handler. 
*/
static void
depca_interrupt(int irq, struct pt_regs * regs)
{
    struct device *dev = (struct device *)(irq2dev_map[irq]);
    struct depca_private *lp;
    int csr0, ioaddr, nicsr;

    if (dev == NULL) {
	printk ("depca_interrupt(): irq %d for unknown device.\n", irq);
    } else {
      lp = (struct depca_private *)dev->priv;
      ioaddr = dev->base_addr;

      if (dev->interrupt)
	printk("%s: Re-entering the interrupt handler.\n", dev->name);

      dev->interrupt = MASK_INTERRUPTS;

      /* mask the DEPCA board interrupts and turn on the LED */
      nicsr = inb(DEPCA_NICSR);
      nicsr |= (IM|LED);
      outb(nicsr, DEPCA_NICSR);

      outw(CSR0, DEPCA_ADDR);
      csr0 = inw(DEPCA_DATA);

      /* Acknowledge all of the current interrupt sources ASAP. */
      outw(csr0 & ~(INEA|TDMD|STOP|STRT|INIT), DEPCA_DATA);

      if (depca_debug > 5)
	printk("%s: interrupt  csr0=%#2.2x new csr=%#2.2x.\n",
	       dev->name, csr0, inw(DEPCA_DATA));

      if (csr0 & RINT)		/* Rx interrupt (packet arrived) */
	depca_rx(dev);

      if (csr0 & TINT) 	        /* Tx interrupt (packet sent) */
        depca_tx(dev);

      /* Clear the interrupts we've handled. */
      outw(CSR0, DEPCA_ADDR);
      outw(BABL|CERR|MISS|MERR|RINT|TINT|IDON|INEA, DEPCA_DATA);

      if (depca_debug > 4) {
	printk("%s: exiting interrupt, csr%d=%#4.4x.\n",
	       dev->name, inw(DEPCA_ADDR),
	       inw(DEPCA_DATA));
      }

      /* Unmask the DEPCA board interrupts and turn off the LED */
      nicsr = (nicsr & ~IM & ~LED);
      outb(nicsr, DEPCA_NICSR);
      dev->interrupt = UNMASK_INTERRUPTS;
    }

    return;
}

static int
depca_rx(struct device *dev)
{
    struct depca_private *lp = (struct depca_private *)dev->priv;
    int entry = lp->cur_rx & lp->rmask;

    /* If we own the next entry, it's a new packet. Send it up. */
    for (; lp->rx_ring[entry].base >= 0; entry = (++lp->cur_rx) & lp->rmask) {
	int status = lp->rx_ring[entry].base >> 16 ;
	int chained;

	/*
	** There is a tricky error noted by John Murphy, <murf@perftech.com>
	** to Russ Nelson: even with full-sized buffers, it's possible for a
	** jabber packet to use two buffers, with only the last one correctly
	** noting the error.
	*/

	/* Check for a chaining buffer */
	chained = 0;
	if (status == R_STP) { 
	  chained = 1;

	  /* 
	  ** Wait for next buffer to complete to check for errors. This
	  ** is slow but infrequent and allows for correct hardware buffer
	  ** chaining (whilst defeating the chaining's purpose).
	  */
	  while ((status=(lp->rx_ring[(entry+1)&lp->rmask].base >> 16)) < 0);

	  /* NB: 'status' now comes from the buffer following 'entry'. */
	}
	  
	if (status & R_ERR) {	               /* There was an error. */
	    lp->stats.rx_errors++;             /* Update the error stats. */
	    if (status & R_FRAM) lp->stats.rx_frame_errors++;
	    if (status & R_OFLO) lp->stats.rx_over_errors++;
	    if (status & R_CRC)  lp->stats.rx_crc_errors++;
	    if (status & R_BUFF) lp->stats.rx_fifo_errors++;
	} else {	  /* Malloc up new buffer, compatible  with net-2e. */
	    short pkt_len = lp->rx_ring[entry].msg_length;
	    struct sk_buff *skb;

	    skb = alloc_skb(pkt_len, GFP_ATOMIC);
	    if (skb == NULL) {
		printk("%s: Memory squeeze, deferring packet.\n", dev->name);
		lp->stats.rx_dropped++;	/* Really, deferred. */
		break;
	    }
	    skb->len = pkt_len;
	    skb->dev = dev;
	    memcpy(skb->data,
		  (unsigned char *)((lp->rx_ring[entry].base+lp->bus_offset) &
				                                   0x00ffffff),
		   pkt_len);
	    /* 
	    ** Notify the upper protocol layers that there is another 
	    ** packet to handle
	    */
	    netif_rx(skb);
	    lp->stats.rx_packets++;
	}

	/* turn over ownership of the current entry back to the LANCE */
	lp->rx_ring[entry].base |= R_OWN;
	if (chained && (status & R_ERR)) {          /* next entry also bad */
	  entry = (++lp->cur_rx) & lp->rmask;
	  lp->rx_ring[entry].base |= R_OWN;
	}
    }

    /* 
    ** We should check that at least two ring entries are free.  If not,
    ** we should free one and mark stats->rx_dropped++. 
    */

    return 0;
}

/*
** Buffer sent - check for buffer errors.
*/
static int
depca_tx(struct device *dev)
{
  struct depca_private *lp = (struct depca_private *)dev->priv;
  int dirty_tx = lp->dirty_tx & lp->rmask;

  if (depca_debug > 5)
    printk("%s: Cleaning tx ring, dirty %d clean %d.\n",
	   dev->name, dirty_tx, (lp->cur_tx & lp->rmask));
  
  /* 
  ** While the dirty entry is not the current one AND 
  ** the LANCE doesn't own it... 
  */
  for (; dirty_tx!=(lp->cur_tx & lp->rmask) && lp->tx_ring[dirty_tx].base>0;
                                       dirty_tx = ++lp->dirty_tx & lp->rmask) {
    unsigned long *tmdp = (unsigned long *)(&lp->tx_ring[dirty_tx]);
    int status = lp->tx_ring[dirty_tx].base >> 16;

    if (status < 0) {                          /* Packet not yet sent! */
      printk("interrupt for packet not yet sent!\n");
      break;
    }
    if (status & T_ERR) { /* There was an major error, log it. */
      int err_status = lp->tx_ring[dirty_tx].misc;

      lp->stats.tx_errors++;
      if (err_status & TMD3_RTRY) lp->stats.tx_aborted_errors++;
      if (err_status & TMD3_LCAR) lp->stats.tx_carrier_errors++;
      if (err_status & TMD3_LCOL) lp->stats.tx_window_errors++;
      if (err_status & TMD3_UFLO) lp->stats.tx_fifo_errors++;
      /* We should re-init() after the FIFO error. */
    } else if (status & (T_MORE | T_ONE)) {
      lp->stats.collisions++;
    } else {
      lp->stats.tx_packets++;
    }

    if (depca_debug > 5)
      printk("%s: Tx done entry %d, %4.4lx %4.4lx %4.4lx %4.4lx.\n",
	     dev->name, dirty_tx,
	     tmdp[0], tmdp[1], tmdp[2], tmdp[3]);
  }
  /*mark_bh(INET_BH);*/
  return 0;
}

static int
depca_close(struct device *dev)
{
  struct depca_private *lp = (struct depca_private *)dev->priv;
  int nicsr, ioaddr = dev->base_addr;

  dev->start = 0;
  dev->tbusy = 1;

  outw(CSR0, DEPCA_ADDR);

  if (depca_debug > 1) {
    printk("%s: Shutting down ethercard, status was %2.2x.\n",
	   dev->name, inw(DEPCA_DATA));
  }

  /* 
  ** We stop the DEPCA here -- it occasionally polls
  ** memory if we don't. 
  */
  outw(STOP, DEPCA_DATA);

  /*
  ** Give back the ROM in case the user wants to go to DOS
  */
  if (strstr(lp->devname,"DEPCA") == NULL) {
    nicsr = inb(DEPCA_NICSR);
    nicsr &= ~SHE;
    outb(nicsr, DEPCA_NICSR);
  }

  free_irq(dev->irq);

  irq2dev_map[dev->irq] = 0;

  MOD_DEC_USE_COUNT;

  return 0;
}

static void LoadCSRs(struct device *dev)
{
  struct depca_private *lp = (struct depca_private *)dev->priv;
  int ioaddr = dev->base_addr;

  outw(CSR1, DEPCA_ADDR);                /* initialisation block address LSW */
  outw((unsigned short)((unsigned long)(&lp->init_block) & LA_MASK), 
                                                                   DEPCA_DATA);
  outw(CSR2, DEPCA_ADDR);                /* initialisation block address MSW */
  outw((unsigned short)(((unsigned long)(&lp->init_block) & LA_MASK) >> 16), 
                                                                   DEPCA_DATA);
  outw(CSR3, DEPCA_ADDR);                /* ALE control */
  outw(ACON, DEPCA_DATA);
  outw(CSR0, DEPCA_ADDR);                /* point back to CSR0 */
}

static int InitRestartDepca(struct device *dev)
{
  struct depca_private *lp = (struct depca_private *)dev->priv;
  int ioaddr = dev->base_addr;
  int i, status=0;

  outw(CSR0, DEPCA_ADDR);                /* point back to CSR0 */
  outw(INIT, DEPCA_DATA);                /* initialize DEPCA */

  /* wait for lance to complete initialisation */
  for (i=0;(i<100) && !(inw(DEPCA_DATA) & IDON); i++); 

  if (i!=100) {
    /* clear IDON by writing a "1", enable interrupts and start lance */
    outw(IDON | INEA | STRT, DEPCA_DATA);
    if (depca_debug > 2) {
      printk("%s: DEPCA open after %d ticks, init block %#lx csr0 %4.4x.\n",
	     dev->name, i, (long) &lp->init_block, inw(DEPCA_DATA));
    }
  } else {
    status = -1;
    printk("%s: DEPCA unopened after %d ticks, init block %#lx csr0 %4.4x.\n",
	   dev->name, i, (long) &lp->init_block, inw(DEPCA_DATA));
  }

  return status;
}

static struct enet_statistics *
depca_get_stats(struct device *dev)
{
    struct depca_private *lp = (struct depca_private *)dev->priv;

    /* Null body since there is no framing error counter */

    return &lp->stats;
}

#ifdef HAVE_MULTICAST
/*
** Set or clear the multicast filter for this adaptor.
** num_addrs == -1	Promiscuous mode, receive all packets
** num_addrs == 0	Normal mode, clear multicast list
** num_addrs > 0	Multicast mode, receive normal and MC packets, and do
** 			best-effort filtering.
*/
#define hash_filter lp->init_block.filter

static void
set_multicast_list(struct device *dev, int num_addrs, void *addrs)
{
  short ioaddr = dev->base_addr;
  struct depca_private *lp = (struct depca_private *)dev->priv;
  
  if (irq2dev_map[dev->irq] != NULL) {
    STOP_DEPCA;                       /* Temporarily stop the depca.  */
    depca_init_ring(dev);             /* Initialize the descriptor rings */

    if (num_addrs >= 0) {
      SetMulticastFilter(num_addrs, (char *)addrs, (char *)hash_filter);
      lp->init_block.mode &= ~PROM;   /* Unset promiscuous mode */
    } else {
      lp->init_block.mode |= PROM;    /* Set promiscuous mode */
    }

    LoadCSRs(dev);                    /* Reload CSR3 */
    InitRestartDepca(dev);            /* Resume normal operation. */
  }
}

/*
** Calculate the hash code and update the logical address filter
** from a list of ethernet multicast addresses.
** Derived from a 'C' program in the AMD data book:
** "Am79C90 CMOS Local Area Network Controller for Ethernet (C-LANCE)", 
** Pub #17781, Rev. A, May 1993
*/
static void SetMulticastFilter(int num_addrs, char *addrs, char *multicast_table)
{
  char j, ctrl, bit, octet, hashcode;
  short int i;
  long int CRC, poly = (long int) CRC_POLYNOMIAL;

  for (i=0;i<num_addrs;i++) {                /* for each address in the list */
    if (((char) *(addrs+ETH_ALEN*i) & 0x01) == 1) {/* is multicast address? */ 
      CRC = (long int) 0xffffffff;           /* init CRC for each address */
      for (octet=0;octet<ETH_ALEN;octet++) { /* for each address octet */
	for(j=0;j<8;j++) {                   /* process each address bit */
	  bit = (((char)* (addrs+ETH_ALEN*i+octet)) >> j) & 0x01;
	  ctrl = ((CRC < 0) ? 1 : 0);        /* shift the control bit */
	  CRC <<= 1;                         /* shift the CRC */
	  if (bit ^ ctrl) {                  /* (bit) XOR (control bit) */
	    CRC ^= poly;                     /* (CRC) XOR (polynomial) */
	  }
	}
      }
      hashcode = (CRC & 0x00000001);         /* hashcode is 6 LSb of CRC ... */
      for (j=0;j<5;j++) {                    /* ... in reverse order. */
	hashcode <<= 1;
	CRC >>= 1;
	hashcode |= (CRC & 0x00000001);
      }                                      
      octet = hashcode >> 3;                  /* bit[3-5] -> octet in filter */
                                              /* bit[0-2] -> bit in octet */
      multicast_table[octet] |= (1 << (hashcode & 0x07));
    }
  }
  return;
}

#endif  /* HAVE_MULTICAST */

#ifndef MODULE
/*
** ISA bus I/O device probe
*/
static struct device *isa_probe(struct device *dev)
{
  int *port, ports[] = DEPCA_IO_PORTS;
  int status;

  for (status = -ENODEV, port = &ports[0]; 
                             *port && (num_depcas < MAX_NUM_DEPCAS); port++) {
    int ioaddr = *port;

    if (DevicePresent(ioaddr) == 0) {
      if (num_depcas > 0) {        /* only gets here in autoprobe */
	dev = alloc_device(dev, ioaddr);
      } else {
	if ((status = depca_probe1(dev, ioaddr)) == 0) {
	  num_depcas++;
	}
      }
      num_eth++;
    }
  }
  return dev;
}

/*
** EISA bus I/O device probe. Probe from slot 1 since slot 0 is usually
** the motherboard.
*/
static struct device *eisa_probe(struct device *dev)
{
  int i, ioaddr = DEPCA_EISA_IO_PORTS;
  int status;

  ioaddr+=0x1000;                         /* get the first slot address */
  for (status = -ENODEV, i=1; i<MAX_EISA_SLOTS; i++, ioaddr+=0x1000) {

    if (EISA_signature(DEPCA_EISA_ID) == 0) {
      if (num_depcas > 0) {        /* only gets here in autoprobe */
	dev = alloc_device(dev, ioaddr);
      } else {
	if ((status = depca_probe1(dev, ioaddr)) == 0) {
	  num_depcas++;
	}
      }
      num_eth++;
    }
  }
  return dev;
}

/*
** Allocate the device by pointing to the next available space in the
** device structure. Should one not be available, it is created.
*/
static struct device *alloc_device(struct device *dev, int ioaddr)
{
  /*
  ** Check the device structures for an end of list or unused device
  */
  while (dev->next != NULL) {
    if (dev->next->base_addr == 0xffe0) break;
    dev = dev->next;         /* walk through eth device list */
    num_eth++;               /* increment eth device number */
  }

  /*
  ** If no more device structures, malloc one up. If memory could
  ** not be allocated, print an error message.
  */
  if (dev->next == NULL) {
    dev->next = (struct device *)kmalloc(sizeof(struct device) + 8,
					 GFP_KERNEL);
    if (dev->next == NULL) {
      printk("eth%d: Device not initialised, insufficient memory\n",
	     num_eth);
    }
  }
  
  /*
  ** If the memory was allocated, point to the new memory area
  ** and initialize it (name, I/O address, next device (NULL) and
  ** initialisation probe routine).
  */
  if ((dev->next != NULL) &&
      (num_eth > 0) && (num_eth < 9999)) {
    dev = dev->next;                    /* point to the new device */
    dev->name = (char *)(dev + 1);
    sprintf(dev->name,"eth%d", num_eth);/* New device name */
    dev->base_addr = ioaddr;            /* assign the io address */
    dev->next = NULL;                   /* mark the end of list */
    dev->init = &depca_probe;           /* initialisation routine */
    num_depcas++;
  }

  return dev;
}
#endif    /* MODULE */

/*
** Look for a particular board name in the on-board Remote Diagnostics
** and Boot (RDB) ROM. This will also give us a clue to the network RAM
** base address.
*/
static char *DepcaSignature(unsigned long mem_addr)
{
  unsigned long i,j,k;
  static char signatures[][DEPCA_NAME_LENGTH] = DEPCA_SIGNATURE;
  static char thisName[DEPCA_NAME_LENGTH];
  char tmpstr[17];

  for (i=0;i<16;i++) {                  /* copy the first 16 bytes of ROM to */
    tmpstr[i] = *(unsigned char *)(mem_addr+0xc000+i); /* a temporary string */
  }
  tmpstr[i]='\0';

  strcpy(thisName,"");
  for (i=0;*signatures[i]!='\0' && *thisName=='\0';i++) {
    for (j=0,k=0;j<16 && k<strlen(signatures[i]);j++) {
      if (signatures[i][k] == tmpstr[j]) {              /* track signature */
	k++;
      } else {                     /* lost signature; begin search again */
	k=0;
      }
    }
    if (k == strlen(signatures[i])) {
      strcpy(thisName,signatures[i]);
    }
  }

  return thisName;                    /* return the device name string */
}

/*
** Look for a special sequence in the Ethernet station address PROM that
** is common across all DEPCA products. Note that the original DEPCA needs
** its ROM address counter to be initialized and enabled. Only enable
** if the first address octet is a 0x08 - this minimises the chances of
** messing around with some other hardware, but it assumes that this DEPCA
** card initialized itself correctly.
** 
** Search the Ethernet address ROM for the signature. Since the ROM address
** counter can start at an arbitrary point, the search must include the entire
** probe sequence length plus the (length_of_the_signature - 1).
** Stop the search IMMEDIATELY after the signature is found so that the
** PROM address counter is correctly positioned at the start of the
** ethernet address for later read out.
*/
static int DevicePresent(short ioaddr)
{
  union {
    struct {
      u_long a;
      u_long b;
    } llsig;
    char Sig[sizeof(long) << 1];
  } dev;
  short sigLength=0;
  char data;
  int i, j, nicsr, status = 0;

  data = inb(DEPCA_PROM);                /* clear counter on DEPCA */
  data = inb(DEPCA_PROM);                /* read data */

  if (data == 0x08) {                    /* Enable counter on DEPCA */
    nicsr = inb(DEPCA_NICSR);
    nicsr |= AAC;
    outb(nicsr, DEPCA_NICSR);
  }
  
  dev.llsig.a = ETH_PROM_SIG;
  dev.llsig.b = ETH_PROM_SIG;
  sigLength = sizeof(long) << 1;

  for (i=0,j=0;j<sigLength && i<PROBE_LENGTH+sigLength-1;i++) {
    data = inb(DEPCA_PROM);
    if (dev.Sig[j] == data) {    /* track signature */
      j++;
    } else {                    /* lost signature; begin search again */
      if (data == dev.Sig[0]) {  /* rare case.... */
	j=1;
      } else {
	j=0;
      }
    }
  }

  if (j!=sigLength) {
    status = -ENODEV;           /* search failed */
  }

  return status;
}

/*
** Look for a particular board name in the EISA configuration space
*/
static int EISA_signature(short iobase)
{
  unsigned long i;
  int status;
  char *signatures[] = DEPCA_SIGNATURE;
  char ManCode[8];
  union {
    u_long ID;
    u_char Id[4];
  } Eisa;

  for (i=0; i<4; i++) {
    Eisa.Id[i] = inb(iobase + i);
  }

  ManCode[0]=(((Eisa.Id[0]>>2)&0x1f)+0x40);
  ManCode[1]=(((Eisa.Id[1]&0xe0)>>5)+((Eisa.Id[0]&0x03)<<3)+0x40);
  ManCode[2]=(((Eisa.Id[2]>>4)&0x0f)+0x30);
  ManCode[3]=((Eisa.Id[2]&0x0f)+0x30);
  ManCode[4]=(((Eisa.Id[3]>>4)&0x0f)+0x30);
  ManCode[5]='\0';

  for (status = -ENXIO, i=0;*signatures[i] != '\0' && status;i++) {
    if (strstr(ManCode, signatures[i]) != NULL) {
      status = 0;
    }
  }
  
  return status;                            /* return the device name string */
}

#ifdef MODULE
char kernel_version[] = UTS_RELEASE;
static struct device thisDepca = {
  "        ",  /* device name inserted by /linux/drivers/net/net_init.c */
  0, 0, 0, 0,
  0x200, 7,   /* I/O address, IRQ */
  0, 0, 0, NULL, depca_probe };

/*
 *	This is a tweak to keep the insmod program happy. It can only
 *	set int values with var=value so we split these out.
 */
 
int irq=7;	/* EDIT THESE LINE FOR YOUR CONFIGURATION */
int io=0x200;   /* Or use the irq= io= options to insmod */
	
int
init_module(void)
{
  thisDepca.irq=irq;
  thisDepca.base_addr=io;
  if (register_netdev(&thisDepca) != 0)
    return -EIO;
  return 0;
}

void
cleanup_module(void)
{
  if (MOD_IN_USE) {
    printk("%s: device busy, remove delayed\n",thisDepca.name);
  } else {
    release_region(thisDepca.base_addr, DEPCA_TOTAL_SIZE);
    unregister_netdev(&thisDepca);
  }
}
#endif /* MODULE */


/*
 * Local variables:
 *  kernel-compile-command: "gcc -D__KERNEL__ -I/usr/src/linux/net/inet -Wall -Wstrict-prototypes -O2 -m486 -c depca.c"
 *
 *  module-compile-command: "gcc -D__KERNEL__ -DMODULE -I/usr/src/linux/net/inet -Wall -Wstrict-prototypes -O2 -m486 -c depca.c"
 * End:
 */
