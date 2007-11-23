/*  de4x5.c: A DIGITAL DE425/DE434/DE435 ethernet driver for linux.

    Copyright 1994 Digital Equipment Corporation.

    This software may be used and distributed according to the terms of
    the GNU Public License, incorporated herein by reference.

    This driver is written for the Digital Equipment Corporation series
    of EtherWORKS ethernet cards:

	DE425 TP/COAX EISA
	DE434 TP PCI
	DE435 TP/COAX/AUI PCI

    The driver has been tested on a  relatively busy network using the DE425
    and DE435 cards and benchmarked with 'ttcp': it  transferred 16M of data
    at 1.08MB/s (8.6Mb/s) to a DECstation 5000/200.

    ************************************************************************
    However there is still a known bug which causes ttcp to hang on transmit
    (receive  is  OK), although  the  adapter/driver  continues to  function
    normally for  other applications e.g.  nfs  mounting disks, pinging etc.
    The cause is under investigation.
    ************************************************************************

    The author may    be  reached as davies@wanton.lkg.dec.com  or   Digital
    Equipment Corporation, 550 King Street, Littleton MA 01460.

    =========================================================================
    This driver has been written  substantially  from scratch, although  its
    inheritance of style and stack interface from 'ewrk3.c' and in turn from
    Donald Becker's 'lance.c' should be obvious.

    Upto 15 EISA cards can be supported under this driver, limited primarily
    by the available IRQ lines.  I have  checked different configurations of
    multiple depca, EtherWORKS 3 cards and de4x5 cards and  have not found a
    problem yet (provided you have at least depca.c v0.38) ...

    PCI support  has been added  to allow the  driver to work with the DE434
    and  DE435 cards. The I/O  accesses  are a  bit of a   kludge due to the
    differences  in the  EISA and PCI    CSR address offsets  from the  base
    address.

    The ability to load  this driver as a loadable  module has been included
    and  used extensively during the  driver development (to save those long
    reboot sequences).  Loadable module support under  PCI has been achieved
    by letting any I/O address less than 0x1000 be assigned as:

                       0xghh

    where g is the bus number (usually 0 until the BIOS's get fixed)
         hh is the device number (max is 32 per bus).

    Essentially, the I/O address and IRQ information  are ignored and filled
    in later by  the PCI BIOS   during the PCI  probe.  Note  that the board
    should be in the system at boot time so that its I/O address and IRQ are
    allocated by the PCI BIOS automatically. The special case of device 0 on
    bus 0  is  not allowed  as  the probe  will think   you're autoprobing a
    module.

    To utilise this ability, you have to do 8 things:

    0) have a copy of the loadable modules code installed on your system.
    1) copy de4x5.c from the  /linux/drivers/net directory to your favourite
    temporary directory.
    2) edit the  source code near  line 1945 to reflect  the I/O address and
    IRQ you're using, or assign these when loading by:

                   insmod de4x5.o irq=x io=y

    3) compile  de4x5.c, but include -DMODULE in  the command line to ensure
    that the correct bits are compiled (see end of source code).
    4) if you are wanting to add a new  card, goto 5. Otherwise, recompile a
    kernel with the de4x5 configuration turned off and reboot.
    5) insmod de4x5.o
    6) run the net startup bits for your new eth?? interface manually 
    (usually /etc/rc.inet[12] at boot time). 
    7) enjoy!

    Note that autoprobing is not allowed in loadable modules - the system is
    already up and running and you're messing with interrupts.

    To unload a module, turn off the associated interface 
    'ifconfig eth?? down' then 'rmmod de4x5'.

    Automedia detection is included so that in  principal you can disconnect
    from, e.g.  TP, reconnect  to BNC  and  things will still work  (after a
    pause whilst the   driver figures out   where its media went).  My tests
    using ping showed that it appears to work....

    A compile time  switch to allow  Zynx  recognition has been  added. This
    "feature" is in no way supported nor tested  in this driver and the user
    may use it at his/her sole discretion.  I have had 2 conflicting reports
    that  my driver  will or   won't  work with   Zynx. Try Donald  Becker's
    'tulip.c' if this driver doesn't work for  you. I will not be supporting
    Zynx cards since I have no information on them  and can't test them in a
    system.

    TO DO:
    ------
    1. Add DC21041 Nway/Autosense support
    2. Add DC21140 Autosense support
    3. Add timer support


    Revision History
    ----------------

    Version   Date        Description
  
      0.1     17-Nov-94   Initial writing. ALPHA code release.
      0.2     13-Jan-95   Added PCI support for DE435's
      0.21    19-Jan-95   Added auto media detection
      0.22    10-Feb-95   Fix interrupt handler call <chris@cosy.sbg.ac.at>
                          Fix recognition bug reported by <bkm@star.rl.ac.uk>
			  Add request/release_region code
			  Add loadable modules support for PCI
			  Clean up loadable modules support

    =========================================================================
*/

static char *version = "de4x5.c:v0.22 2/10/95 davies@wanton.lkg.dec.com\n";

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
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <linux/pci.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/segment.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include <linux/time.h>
#include <linux/types.h>
#include <linux/unistd.h>

#include "de4x5.h"

#ifdef DE4X5_DEBUG
static int de4x5_debug = DE4X5_DEBUG;
#else
static int de4x5_debug = 1;
#endif

/*
** Ethernet PROM defines
*/
#define PROBE_LENGTH    32
#define ETH_PROM_SIG    0xAA5500FFUL

/*
** Ethernet Info
*/
#define PKT_BUF_SZ	1544            /* Buffer size for each Tx/Rx buffer */
#define MAX_PKT_SZ   	1514            /* Maximum ethernet packet length */
#define MAX_DAT_SZ   	1500            /* Maximum ethernet data length */
#define MIN_DAT_SZ   	1               /* Minimum ethernet data length */
#define PKT_HDR_LEN     14              /* Addresses and data length info */

#define CRC_POLYNOMIAL_BE 0x04c11db7UL   /* Ethernet CRC, big endian */
#define CRC_POLYNOMIAL_LE 0xedb88320UL   /* Ethernet CRC, little endian */

/*
** EISA bus defines
*/
#define DE4X5_EISA_IO_PORTS   0x0c00     /* I/O port base address, slot 0 */
#define DE4X5_EISA_TOTAL_SIZE 0xfff      /* I/O address extent */

#define MAX_EISA_SLOTS 16
#define EISA_SLOT_INC 0x1000

#define DE4X5_SIGNATURE {"DE425",""}
#define DE4X5_NAME_LENGTH 8

/*
** PCI Bus defines
*/
#define PCI_MAX_BUS_NUM 8
#define DE4X5_PCI_TOTAL_SIZE 0x80        /* I/O address extent */

/*
** Timer defines
*/
#define TIMER_WIDTH   16
#define TIMER_PORT    0x43
#define TIMER_LATCH   0x06
#define TIMER_READ    0x40
#define TIMER_TICK    419  /*ns*/
#define DELAY_QUANT   5    /*us*/

#define LWPAD ((long)(sizeof(long) - 1)) /* for longword alignment */

#ifndef IS_ZYNX                          /* See README.de4x5 for using this */
static int is_zynx = 0;
#else
static int is_zynx = 1;
#endif

/*
** DE4X5 IRQ ENABLE/DISABLE
*/
static u_long irq_mask = IMR_RIM | IMR_TIM | IMR_TUM ;

static u_long irq_en   = IMR_NIM | IMR_AIM;

#define ENABLE_IRQs \
    imr |= irq_en;\
    outl(imr, DE4X5_IMR)                    /* Enable the IRQs */

#define DISABLE_IRQs \
    imr = inl(DE4X5_IMR);\
    imr &= ~irq_en;\
    outl(imr, DE4X5_IMR)                    /* Disable the IRQs */

#define UNMASK_IRQs \
    imr |= irq_mask;\
    outl(imr, DE4X5_IMR)                    /* Unmask the IRQs */

#define MASK_IRQs \
    imr = inl(DE4X5_IMR);\
    imr &= ~irq_mask;\
    outl(imr, DE4X5_IMR)                    /* Mask the IRQs */

/*
** DE4X5 START/STOP
*/
#define START_DE4X5 \
    omr = inl(DE4X5_OMR);\
    omr |= OMR_ST | OMR_SR;\
    outl(omr, DE4X5_OMR)                    /* Enable the TX and/or RX */

#define STOP_DE4X5 \
    omr = inl(DE4X5_OMR);\
    omr &= ~(OMR_ST|OMR_SR);\
    outl(omr, DE4X5_OMR)                    /* Disable the TX and/or RX */

/*
** DE4X5 SIA RESET
*/
#define RESET_SIA \
    outl(SICR_RESET, DE4X5_SICR);           /* Reset SIA connectivity regs */ \
    outl(STRR_RESET, DE4X5_STRR);           /* Write reset values */ \
    outl(SIGR_RESET, DE4X5_SIGR)            /* Write reset values */

/*
** DE4X5 Descriptors. Make sure that all the RX buffers are contiguous
** and have sizes of both a power of 2 and a multiple of 4.
** A size of 256 bytes for each buffer was chosen because over 90% of
** all packets in our network are <256 bytes long.
*/
#define NUM_RX_DESC 64                       /* Number of RX descriptors */
#define NUM_TX_DESC 8                        /* Number of TX descriptors */
#define BUFF_ALLOC_RETRIES 10                /* In case of memory shortage */
#define RX_BUFF_SZ 256                       /* Power of 2 for kmalloc and */
                                             /* Multiple of 4 for DC21040 */

struct de4x5_desc {
    volatile long status;
    u_long des1;
    char *buf;
    char *next;
};

/*
** The DE4X5 private structure
*/
#define DE4X5_PKT_STAT_SZ 16
#define DE4X5_PKT_BIN_SZ  128                /* Should be >=100 unless you
                                                increase DE4X5_PKT_STAT_SZ */

struct de4x5_private {
    char adapter_name[80];                   /* Adapter name */
    struct de4x5_desc rx_ring[NUM_RX_DESC];  /* RX descriptor ring */
    struct de4x5_desc tx_ring[NUM_TX_DESC];  /* TX descriptor ring */
    struct sk_buff *skb[NUM_TX_DESC];        /* TX skb for freeing when sent */
    int rx_new, rx_old;                      /* RX descriptor ring pointers */
    int tx_new, tx_old;                      /* TX descriptor ring pointers */
    char setup_frame[SETUP_FRAME_LEN];       /* Holds MCA and PA info. */
    struct enet_statistics stats;            /* Public stats */
    struct {
	unsigned long bins[DE4X5_PKT_STAT_SZ]; /* Private stats counters */
	unsigned long unicast;
	unsigned long multicast;
	unsigned long broadcast;
	unsigned long excessive_collisions;
	unsigned long tx_underruns;
	unsigned long excessive_underruns;
    } pktStats;
    char rxRingSize;
    char txRingSize;
    char bus;                                /* EISA or PCI */
    char lostMedia;                          /* Possibly lost media */
};

#define TX_BUFFS_AVAIL ((lp->tx_old<=lp->tx_new)?\
			 lp->tx_old+lp->txRingSize-lp->tx_new-1:\
                         lp->tx_old               -lp->tx_new-1)
#define TX_SUSPENDED   (((sts & STS_TS) ^ TS_SUSP)==0)

/*
** Public Functions
*/
static int  de4x5_open(struct device *dev);
static int  de4x5_queue_pkt(struct sk_buff *skb, struct device *dev);
static void de4x5_interrupt(int irq, struct pt_regs * regs);
static int  de4x5_close(struct device *dev);
static struct enet_statistics *de4x5_get_stats(struct device *dev);
static void set_multicast_list(struct device *dev, int num_addrs, void *addrs);
static int  de4x5_ioctl(struct device *dev, struct ifreq *rq, int cmd);

/*
** Private functions
*/
static int  de4x5_hw_init(struct device *dev, short iobase);
static int  de4x5_init(struct device *dev);
static int  de4x5_rx(struct device *dev);
static int  de4x5_tx(struct device *dev);

static int  autoconf_media(struct device *dev);
static void create_packet(struct device *dev, char *frame, int len);
static u_short dce_get_ticks(void);
static void dce_us_delay(u_long usec);
static void dce_ms_delay(u_long msec);
static void load_packet(struct device *dev, char *buf, u_long flags, struct sk_buff *skb);
static void EISA_signature(char * name, short iobase);
static int  DevicePresent(short iobase);
static void SetMulticastFilter(struct device *dev, int num_addrs, char *addrs, char *multicast_table);

static int aprom_crc (struct device *dev);

static void eisa_probe(struct device *dev, short iobase);
static void pci_probe(struct device *dev, short iobase);
static struct device *alloc_device(struct device *dev, int iobase);

#ifdef MODULE
int  init_module(void);
void cleanup_module(void);
static int autoprobed = 1, loading_module = 1;
# else
static unsigned char de4x5_irq[] = {5,9,10,11};
static int autoprobed = 0, loading_module = 0;
#endif /* MODULE */

static char name[DE4X5_NAME_LENGTH + 1];
static int num_de4x5s = 0, num_eth = 0;

/*
** Kludge to get around the fact that the CSR addresses have different
** offsets in the PCI and EISA boards. Also note that the ethernet address
** PROM is accessed differently.
*/
static struct bus_type {
    int bus;
    int device;
} bus;

/*
** Miscellaneous defines...
*/
#define RESET_DE4X5 {\
    long i;\
    i=inl(DE4X5_BMR);\
    outl(i | BMR_SWR, DE4X5_BMR);\
    outl(i, DE4X5_BMR);\
    for (i=0;i<5;i++) inl(DE4X5_BMR);\
		   }




int de4x5_probe(struct device *dev)
{
  int tmp = num_de4x5s, iobase = dev->base_addr;
  int status = -ENODEV;

  if ((iobase == 0) && loading_module){
    printk("Autoprobing is not supported when loading a module based driver.\n");
    status = -EIO;
  } else {                              /* First probe for the Ethernet */
	                                /* Address PROM pattern */
    eisa_probe(dev, iobase);
    pci_probe(dev, iobase);

    if ((tmp == num_de4x5s) && (iobase != 0)) {
      printk("%s: de4x5_probe() cannot find device at 0x%04x.\n", dev->name, 
	                                                               iobase);
    }

    /*
    ** Walk the device list to check that at least one device
    ** initialised OK
    */
    for (; dev->priv == NULL && dev->next != NULL; dev = dev->next);

    if (dev->priv) status = 0;
    if (iobase == 0) autoprobed = 1;
  }

  return status;
}

static int
de4x5_hw_init(struct device *dev, short iobase)
{
  struct bus_type *lp = &bus;
  int tmpbus, i, j, status=0;
  char *tmp;
  u_long nicsr;

  RESET_DE4X5;

  if (((nicsr=inl(DE4X5_STS)) & (STS_TS | STS_RS)) == 0) {
    /* 
    ** Now find out what kind of DC21040/DC21041/DC21140 board we have.
    */
    if (lp->bus == PCI) {
      if (!is_zynx) {
	strcpy(name, "DE435");
      } else {
	strcpy(name, "ZYNX");
      }
    } else {
      EISA_signature(name, EISA_ID0);
    }

    if (*name != '\0') {                         /* found a board signature */
      dev->base_addr = iobase;
      request_region(iobase, (lp->bus == PCI ? DE4X5_PCI_TOTAL_SIZE :
			                       DE4X5_EISA_TOTAL_SIZE), name);
      
      if (lp->bus == EISA) {
	printk("%s: %s at %#3x (EISA slot %d)", 
	       dev->name, name, (u_short)iobase, (((u_short)iobase>>12)&0x0f));
      } else {                                   /* PCI port address */
	printk("%s: %s at %#3x (PCI device %d)", dev->name, name, (u_short)iobase,lp->device);
      }
	
      printk(", h/w address ");
      status = aprom_crc(dev);
      for (i = 0; i < ETH_ALEN - 1; i++) {       /* get the ethernet addr. */
	printk("%2.2x:", dev->dev_addr[i]);
      }
      printk("%2.2x,\n", dev->dev_addr[i]);
      
      tmpbus = lp->bus;

      if (status == 0) {
	struct de4x5_private *lp;

	/* 
	** Reserve a section of kernel memory for the adapter
	** private area and the TX/RX descriptor rings.
	*/
	dev->priv = (void *) kmalloc(sizeof(struct de4x5_private) + LWPAD, 
				                                   GFP_KERNEL);
	/*
	** Align to a longword boundary
	*/
	dev->priv = (void *)(((u_long)dev->priv + LWPAD) & ~LWPAD);
	lp = (struct de4x5_private *)dev->priv;
	memset(dev->priv, 0, sizeof(struct de4x5_private));
	lp->bus = tmpbus;
	strcpy(lp->adapter_name, name);

	/*
	** Allocate contiguous receive buffers, long word aligned. 
	** This could be a possible memory leak if the private area
	** is ever hosed.
	*/
	for (tmp=NULL, j=0; j<BUFF_ALLOC_RETRIES && tmp==NULL; j++) {
	  if ((tmp = (void *)kmalloc(RX_BUFF_SZ * NUM_RX_DESC + LWPAD, 
	       	  		                        GFP_KERNEL)) != NULL) {
	    tmp = (void *)(((u_long) tmp + LWPAD) & ~LWPAD);
	    for (i=0; i<NUM_RX_DESC; i++) {
	      lp->rx_ring[i].status = 0;
	      lp->rx_ring[i].des1 = RX_BUFF_SZ;
	      lp->rx_ring[i].buf = tmp + i * RX_BUFF_SZ;
	      lp->rx_ring[i].next = NULL;
	    }
	  }
	}

	if (tmp != NULL) {
	  lp->rxRingSize = NUM_RX_DESC;
	  lp->txRingSize = NUM_TX_DESC;
	  
	  /* Write the end of list marker to the descriptor lists */
	  lp->rx_ring[lp->rxRingSize - 1].des1 |= RD_RER;
	  lp->tx_ring[lp->txRingSize - 1].des1 |= TD_TER;

	  /* Tell the adapter where the TX/RX rings are located. */
	  outl((u_long)lp->rx_ring, DE4X5_RRBA);
	  outl((u_long)lp->tx_ring, DE4X5_TRBA);

	  if (dev->irq < 2) {
#ifndef MODULE
	    unsigned char irqnum;
	    u_long omr;
	    autoirq_setup(0);
	    
	    omr = inl(DE4X5_OMR);
	    outl(IMR_AIM|IMR_RUM, DE4X5_IMR); /* Unmask RUM interrupt */
	    outl(OMR_SR | omr, DE4X5_OMR);    /* Start RX w/no descriptors */

	    irqnum = autoirq_report(1);
	    if (!irqnum) {
	      printk("      and failed to detect IRQ line.\n");
	      status = -ENXIO;
	    } else {
	      for (dev->irq=0,i=0; i<sizeof(de4x5_irq) && !dev->irq; i++) {
		if (irqnum == de4x5_irq[i]) {
		  dev->irq = irqnum;
		  printk("      and uses IRQ%d.\n", dev->irq);
		}
	      }
		  
	      if (!dev->irq) {
		printk("      but incorrect IRQ line detected.\n");
		status = -ENXIO;
	      }
	    }
		
	    outl(0, DE4X5_IMR);               /* Re-mask RUM interrupt */

#endif /* MODULE */
	  } else {
	    printk("      and requires IRQ%d (not probed).\n", dev->irq);
	  }
	} else {
	  printk("%s: Kernel could not allocate RX buffer memory.\n", 
		                                                    dev->name);
	  status = -ENXIO;
	}
      } else {
	printk("      which has an Ethernet PROM CRC error.\n");
	status = -ENXIO;
      }
      if (status) release_region(iobase, (lp->bus == PCI ? 
					             DE4X5_PCI_TOTAL_SIZE :
			                             DE4X5_EISA_TOTAL_SIZE));
    } else {
      status = -ENXIO;
    }
  } else {
    status = -ENXIO;
  }
  
  if (!status) {
    if (de4x5_debug > 0) {
      printk(version);
    }
    
    /* The DE4X5-specific entries in the device structure. */
    dev->open = &de4x5_open;
    dev->hard_start_xmit = &de4x5_queue_pkt;
    dev->stop = &de4x5_close;
    dev->get_stats = &de4x5_get_stats;
#ifdef HAVE_MULTICAST
    dev->set_multicast_list = &set_multicast_list;
#endif
    dev->do_ioctl = &de4x5_ioctl;
    
    dev->mem_start = 0;
    
    /* Fill in the generic field of the device structure. */
    ether_setup(dev);
  } else {                            /* Incorrectly initialised hardware */
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    if (lp) {
      kfree_s(lp->rx_ring[0].buf, RX_BUFF_SZ * NUM_RX_DESC + LWPAD);
    }
    if (dev->priv) {
      kfree_s(dev->priv, sizeof(struct de4x5_private) + LWPAD);
      dev->priv = NULL;
    }
  }

  return status;
}


static int
de4x5_open(struct device *dev)
{
  struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
  short iobase = dev->base_addr;
  int i, status = 0;
  u_long imr, omr, sts;

  /*
  ** Stop the TX and RX...
  */
  STOP_DE4X5;

  if (request_irq(dev->irq, (void *)de4x5_interrupt, 0, lp->adapter_name)) {
    printk("de4x5_open(): Requested IRQ%d is busy\n",dev->irq);
    status = -EAGAIN;
  } else {

    irq2dev_map[dev->irq] = dev;
    /* 
    ** Re-initialize the DE4X5... 
    */
    status = de4x5_init(dev);

    if (de4x5_debug > 1){
      printk("%s: de4x5 open with irq %d\n",dev->name,dev->irq);
      printk("\tphysical address: ");
      for (i=0;i<6;i++){
	printk("%2.2x:",(short)dev->dev_addr[i]);
      }
      printk("\n");
      printk("Descriptor head addresses:\n");
      printk("\t0x%8.8lx  0x%8.8lx\n",(long)lp->rx_ring,(long)lp->tx_ring);
      printk("Descriptor addresses:\nRX: ");
      for (i=0;i<lp->rxRingSize-1;i++){
	if (i < 3) {
	  printk("0x%8.8lx  ",(long)&lp->rx_ring[i].status);
	}
      }
      printk("...0x%8.8lx\n",(long)&lp->rx_ring[i].status);
      printk("TX: ");
      for (i=0;i<lp->txRingSize-1;i++){
	if (i < 3) {
	  printk("0x%8.8lx  ", (long)&lp->tx_ring[i].status);
	}
      }
      printk("...0x%8.8lx\n", (long)&lp->tx_ring[i].status);
      printk("Descriptor buffers:\nRX: ");
      for (i=0;i<lp->rxRingSize-1;i++){
	if (i < 3) {
	  printk("0x%8.8lx  ",(long)lp->rx_ring[i].buf);
	}
      }
      printk("...0x%8.8lx\n",(long)lp->rx_ring[i].buf);
      printk("TX: ");
      for (i=0;i<lp->txRingSize-1;i++){
	if (i < 3) {
	  printk("0x%8.8lx  ", (long)lp->tx_ring[i].buf);
	}
      }
      printk("...0x%8.8lx\n", (long)lp->tx_ring[i].buf);
      printk("Ring size: \nRX: %d\nTX: %d\n", 
	     (short)lp->rxRingSize, 
	     (short)lp->txRingSize); 
      printk("\tstatus:  %d\n", status);
    }

    if (!status) {
      dev->tbusy = 0;                         
      dev->start = 1;
      dev->interrupt = UNMASK_INTERRUPTS;
      
      /*
      ** Reset any pending interrupts
      */
      sts = inl(DE4X5_STS);
      outl(sts, DE4X5_STS);

      /*
      ** Unmask and enable DE4X5 board interrupts
      */
      imr = 0;
      UNMASK_IRQs;
      ENABLE_IRQs;

      START_DE4X5;
    }
    if (de4x5_debug > 1) {
      printk("\tsts:  0x%08x\n", inl(DE4X5_STS));
      printk("\tbmr:  0x%08x\n", inl(DE4X5_BMR));
      printk("\timr:  0x%08x\n", inl(DE4X5_IMR));
      printk("\tomr:  0x%08x\n", inl(DE4X5_OMR));
      printk("\tsisr: 0x%08x\n", inl(DE4X5_SISR));
      printk("\tsicr: 0x%08x\n", inl(DE4X5_SICR));
      printk("\tstrr: 0x%08x\n", inl(DE4X5_STRR));
      printk("\tsigr: 0x%08x\n", inl(DE4X5_SIGR));
    }
  }

  MOD_INC_USE_COUNT;

  return status;
}

/*
** Initialize the DE4X5 operating conditions
*/
static int
de4x5_init(struct device *dev)
{  
  struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
  short iobase = dev->base_addr;
  int offset, status = 0;
  u_long i, j, bmr, omr;
  char *pa;

  /* Ensure a full reset */
  RESET_DE4X5;

  /* Set up automatic transmit polling every 1.6ms */
  bmr = inl(DE4X5_BMR);
  bmr |= TAP_1_6MS | CAL_16LONG;
  outl(bmr, DE4X5_BMR);

  /* Set up imperfect filtering mode as default, turn off promiscuous mode */
  omr = OMR_HP;
  offset = IMPERF_PA_OFFSET;

  /* Lock out other processes whilst setting up the hardware */
  set_bit(0, (void *)&dev->tbusy);

  /* Rewrite the descriptor lists' start addresses */
  outl((u_long)lp->rx_ring, DE4X5_RRBA);  /* Start of RX Descriptor List */
  outl((u_long)lp->tx_ring, DE4X5_TRBA);  /* Start of TX Descriptor List */

  /* Reset the buffer pointers */
  lp->rx_new = lp->rx_old = 0;
  lp->tx_new = lp->tx_old = 0;

  /* Initialize each descriptor ownership in the RX ring */
  for (i = 0; i < lp->rxRingSize; i++) {
    lp->rx_ring[i].status = R_OWN;
  }

  /* Initialize each descriptor ownership in the TX ring */
  for (i = 0; i < lp->txRingSize; i++) {
    lp->tx_ring[i].status = 0;
  }

  /* Initialise the setup frame prior to starting the receive process */
  memset(lp->setup_frame, 0, SETUP_FRAME_LEN);

  /* Insert the physical address */
  for (pa=lp->setup_frame+offset, j=0; j<ETH_ALEN; j++) {
    *(pa + j) = dev->dev_addr[j];
    if (j & 0x01) pa += 2;
  }

  /* Clear the multicast list */
  set_multicast_list(dev, 0, NULL);

  /* Tell the hardware there's a new packet to be sent */
  load_packet(dev, lp->setup_frame, HASH_F | TD_SET | SETUP_FRAME_LEN, NULL);

  /* Start the TX process */
  outl(omr|OMR_ST, DE4X5_OMR);

  /* Poll for completion of setup frame (interrupts are disabled for now) */
  for (j=0, i=0;i<100 && j==0;i++) {
    if (lp->tx_ring[lp->tx_new].status >= 0) j=1;
  }
  outl(omr, DE4X5_OMR);                        /* Stop everything! */

  if (i == 100) {
    printk("%s: Setup frame timed out, status %08x\n", dev->name, 
	                                                       inl(DE4X5_STS));
    status = -EIO;
  }

  /* Update pointers */
  lp->tx_new = (++lp->tx_new) % lp->txRingSize;
  lp->tx_old = lp->tx_new;

  /* Autoconfigure the connected port */
  if (autoconf_media(dev) == 0) {
    status = -EIO;
  }

  return 0;
}

/* 
** Writes a socket buffer address to the next available transmit descriptor
*/
static int
de4x5_queue_pkt(struct sk_buff *skb, struct device *dev)
{
  volatile struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
  int iobase = dev->base_addr;
  int status = 0;
  u_long imr, omr, sts;

  sts = inl(DE4X5_STS);

  /* 
  ** Transmitter timeout, possibly serious problems.
  ** The 'lostMedia' threshold accounts for transient errors that
  ** were noticed when switching media.
  */
  if (dev->tbusy || (lp->lostMedia > 3)) {
    int tickssofar = jiffies - dev->trans_start;
    if (tickssofar < 10 && !lp->lostMedia) {
      /* Check if TX ring is full or not - 'tbusy' cleared if not full. */
      if ((TX_BUFFS_AVAIL > 0) && dev->tbusy) {
	dev->tbusy = 0;
      }
      status = -1;
    } else {
      printk("%s: transmit timed out, status %08x, tbusy:%d, lostMedia:%d tickssofar:%d, resetting.\n",dev->name, inl(DE4X5_STS), dev->tbusy, lp->lostMedia, tickssofar);
	
      /* Stop and reset the TX and RX... */
      STOP_DE4X5;
      status = de4x5_init(dev);

      /* Unmask DE4X5 board interrupts */
      if (!status) {
	/* Start here to clean stale interrupts later */
	dev->trans_start = jiffies;
	START_DE4X5;

	/* Clear any pending (stale) interrupts */
	sts = inl(DE4X5_STS);
	outl(sts, DE4X5_STS);

	/* Unmask DE4X5 board interrupts */
	imr = 0;
	UNMASK_IRQs;
	  
	dev->interrupt = UNMASK_INTERRUPTS;
	dev->start = 1;
	dev->tbusy = 0;                         
      
	ENABLE_IRQs;
      } else {
	printk("%s: hardware initialisation failure, status %08x.\n",
	                                            dev->name, inl(DE4X5_STS));
      }
    }
  } else if (skb == NULL) {
    dev_tint(dev);
  } else if (skb->len > 0) {

    /* 
    ** Block a timer-based transmit from overlapping.  This could better be
    ** done with atomic_swap(1, dev->tbusy), but set_bit() works as well. 
    */
    if (set_bit(0, (void*)&dev->tbusy) != 0)
      printk("%s: Transmitter access conflict.\n", dev->name);

    if (TX_BUFFS_AVAIL > 0) {                   /* Fill in a Tx ring entry */
      if (((u_long)skb->data & ~0x03) != (u_long)skb->data) {
	printk("%s: TX skb buffer alignment prob..\n", dev->name);
      }

      load_packet(dev, skb->data, TD_IC | TD_LS | TD_FS | skb->len, skb);
      outl(POLL_DEMAND, DE4X5_TPD);             /* Start the TX */

      lp->tx_new = (++lp->tx_new) % lp->txRingSize; /* Ensure a wrap */
	
      dev->trans_start = jiffies;
    }

    if (TX_BUFFS_AVAIL > 0) {
      dev->tbusy = 0;                           /* Another pkt may be queued */
    }
  }

  return status;
}

/*
** The DE4X5 interrupt handler. 
*/
static void
de4x5_interrupt(int irq, struct pt_regs * regs)
{
    struct device *dev = (struct device *)(irq2dev_map[irq]);
    struct de4x5_private *lp;
    int iobase;
    u_long imr, sts;

    if (dev == NULL) {
	printk ("de4x5_interrupt(): irq %d for unknown device.\n", irq);
    } else {
      lp = (struct de4x5_private *)dev->priv;
      iobase = dev->base_addr;

      if (dev->interrupt)
	printk("%s: Re-entering the interrupt handler.\n", dev->name);

      dev->interrupt = MASK_INTERRUPTS;

      /* 
      ** Get the interrupt information and disable them. 
      ** The device read will ensure pending buffers are flushed
      ** in intermediate PCI bridges, so that the posted interrupt
      ** has some real data to work with.
      */
      sts = inl(DE4X5_STS);
      MASK_IRQs;

      /* 
      ** Acknowledge the DE4X5 board interrupts
      */
      outl(sts, DE4X5_STS);

      if (sts & STS_RI)	                 /* Rx interrupt (packet[s] arrived) */
	de4x5_rx(dev);

      if (sts & STS_TI)                  /* Tx interrupt (packet sent) */
	de4x5_tx(dev); 

      if ((TX_BUFFS_AVAIL > 0) && dev->tbusy) { /* any resources available? */
	dev->tbusy = 0;                  /* clear TX busy flag */
	mark_bh(NET_BH);
      }

      dev->interrupt = UNMASK_INTERRUPTS;

      UNMASK_IRQs;
    }

    return;
}

static int
de4x5_rx(struct device *dev)
{
  struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
  int i, entry;
  volatile long status;
  char *buf;

  /* Loop over any new packets for sending up the stack */
  for (entry = lp->rx_new; lp->rx_ring[entry].status >= 0;entry = lp->rx_new) {
    status = lp->rx_ring[entry].status;

    if (status & RD_FS) {                   /* Remember the start of frame */
      lp->rx_old = entry;
    }

    if (status & RD_LS) {                   /* Valid frame status */
      if (status & RD_ES) {	            /* There was an error. */
	lp->stats.rx_errors++;              /* Update the error stats. */
	if (status & (RD_RF | RD_TL)) lp->stats.rx_frame_errors++;
	if (status & RD_CE)           lp->stats.rx_crc_errors++;
	if (status & RD_OF)           lp->stats.rx_fifo_errors++;
      } else {                              /* A valid frame received */
	struct sk_buff *skb;
	short pkt_len = (short)(lp->rx_ring[entry].status >> 16);

	if ((skb = alloc_skb(pkt_len, GFP_ATOMIC)) != NULL) {
	  skb->len = pkt_len;
	  skb->dev = dev;
	
	  if (entry < lp->rx_old) {         /* Wrapped buffer */
	    short len = (lp->rxRingSize - lp->rx_old) * RX_BUFF_SZ;
	    memcpy(skb->data, lp->rx_ring[lp->rx_old].buf, len);
	    memcpy(skb->data + len, lp->rx_ring[0].buf, pkt_len - len);
	  } else {                          /* Linear buffer */
	    memcpy(skb->data, lp->rx_ring[lp->rx_old].buf, pkt_len);
	  }

	  /* 
	  ** Notify the upper protocol layers that there is another 
	  ** packet to handle
	  */
	  netif_rx(skb);

	  /*
	  ** Update stats
	  */
	  lp->stats.rx_packets++;
	  for (i=1; i<DE4X5_PKT_STAT_SZ-1; i++) {
	    if (pkt_len < i*DE4X5_PKT_BIN_SZ) {
	      lp->pktStats.bins[i]++;
	      i = DE4X5_PKT_STAT_SZ;
	    }
	  }
	  buf = skb->data;                  /* Look at the dest addr */
	  if (buf[0] & 0x01) {              /* Multicast/Broadcast */
	    if ((*(long *)&buf[0] == -1) && (*(short *)&buf[4] == -1)) {
	      lp->pktStats.broadcast++;
	    } else {
	      lp->pktStats.multicast++;
	    }
	  } else if ((*(long *)&buf[0] == *(long *)&dev->dev_addr[0]) &&
		     (*(short *)&buf[4] == *(short *)&dev->dev_addr[4])) {
	    lp->pktStats.unicast++;
	  }
	  
	  lp->pktStats.bins[0]++;           /* Duplicates stats.rx_packets */
	  if (lp->pktStats.bins[0] == 0) {  /* Reset counters */
	    memset((char *)&lp->pktStats, 0, sizeof(lp->pktStats));
	  }
	} else {
	  printk("%s: Insufficient memory; nuking packet.\n", dev->name);
	  lp->stats.rx_dropped++;	      /* Really, deferred. */
	  break;
	}
      }

      /* Change buffer ownership for this last frame, back to the adapter */
      for (; lp->rx_old!=entry; lp->rx_old=(++lp->rx_old)%lp->rxRingSize) {
	lp->rx_ring[lp->rx_old].status = R_OWN;
      }
      lp->rx_ring[entry].status = R_OWN;
    }

    /*
    ** Update entry information
    */
    lp->rx_new = (++lp->rx_new) % lp->rxRingSize;
  }

  return 0;
}

/*
** Buffer sent - check for TX buffer errors.
*/
static int
de4x5_tx(struct device *dev)
{
  struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
  int entry, iobase = dev->base_addr;
  volatile long status;

  for (entry = lp->tx_old; entry != lp->tx_new; entry = lp->tx_old) {
    status = lp->tx_ring[entry].status;
    if (status < 0) {                            /* Buffer not sent yet */
      break;
    } else if (status & TD_ES) {                 /* An error happened */
      lp->stats.tx_errors++; 
      if (status & TD_NC)  lp->stats.tx_carrier_errors++;
      if (status & TD_LC)  lp->stats.tx_window_errors++;
      if (status & TD_UF)  lp->stats.tx_fifo_errors++;
      if (status & TD_LC)  lp->stats.collisions++;
      if (status & TD_EC)  lp->pktStats.excessive_collisions++;
      if (status & TD_DE)  lp->stats.tx_aborted_errors++;

      if (status & (TD_LO | TD_NC | TD_EC | TD_LF)) {
	lp->lostMedia++;
      } else {
	outl(POLL_DEMAND, DE4X5_TPD);            /* Restart a stalled TX */
      }
    } else {                                     /* Packet sent */
      lp->stats.tx_packets++;
      lp->lostMedia = 0;                         /* Remove transient problem */
    }
    /* Free the buffer if it's not a setup frame. */
    if (lp->skb[entry] != NULL) {
      dev_kfree_skb(lp->skb[entry], FREE_WRITE);
    }

    /* Update all the pointers */
    lp->tx_old = (++lp->tx_old) % lp->txRingSize;
  }

  return 0;
}

static int
de4x5_close(struct device *dev)
{
  struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
  int iobase = dev->base_addr;
  u_long imr, omr;

  dev->start = 0;
  dev->tbusy = 1;

  if (de4x5_debug > 1) {
    printk("%s: Shutting down ethercard, status was %8.8x.\n",
	   dev->name, inl(DE4X5_STS));
  }

  /* 
  ** We stop the DE4X5 here... mask interrupts and stop TX & RX
  */
  DISABLE_IRQs;

  STOP_DE4X5;

  /*
  ** Free the associated irq
  */
  free_irq(dev->irq);
  irq2dev_map[dev->irq] = 0;

  MOD_DEC_USE_COUNT;

  return 0;
}

static struct enet_statistics *
de4x5_get_stats(struct device *dev)
{
  struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
  int iobase = dev->base_addr;

  lp->stats.rx_missed_errors = (int) inl(DE4X5_MFC);
    
  return &lp->stats;
}

static void load_packet(struct device *dev, char *buf, u_long flags, struct sk_buff *skb)
{
  struct de4x5_private *lp = (struct de4x5_private *)dev->priv;

  lp->tx_ring[lp->tx_new].buf = buf;
  lp->tx_ring[lp->tx_new].des1 &= TD_TER;
  lp->tx_ring[lp->tx_new].des1 |= flags;
  lp->skb[lp->tx_new] = skb;
  lp->tx_ring[lp->tx_new].status = T_OWN;

  return;
}
/*
** Set or clear the multicast filter for this adaptor.
** num_addrs == -1	Promiscuous mode, receive all packets
** num_addrs == 0	Normal mode, clear multicast list
** num_addrs > 0	Multicast mode, receive normal and MC packets, and do
** 			best-effort filtering.
** num_addrs == HASH_TABLE_LEN
**	                Set all multicast bits
*/
static void
set_multicast_list(struct device *dev, int num_addrs, void *addrs)
{
  struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
  int iobase = dev->base_addr;
  u_long omr;

  /* First, double check that the adapter is open */
  if (irq2dev_map[dev->irq] != NULL) {
    omr = inl(DE4X5_OMR);

    if (num_addrs >= 0) {
      SetMulticastFilter(dev, num_addrs, (char *)addrs, lp->setup_frame);

      /* Tell the hardware that there's a new packet to be sent */
      load_packet(dev, lp->setup_frame, TD_IC | HASH_F | TD_SET | 
		                                        SETUP_FRAME_LEN, NULL);
      lp->tx_new = (++lp->tx_new) % lp->txRingSize;
      outl(POLL_DEMAND, DE4X5_TPD);                /* Start the TX */

      omr &= ~OMR_PR;
      omr |= OMR_PM;
      outl(omr, DE4X5_OMR);
    } else {                             /* set promiscuous mode */
      omr |= OMR_PR;
      omr &= ~OMR_PM;
      outl(omr, DE4X5_OMR);
    }
  }
}

/*
** Calculate the hash code and update the logical address filter
** from a list of ethernet multicast addresses.
** Little endian crc one liner from Matt Thomas, DEC.
*/
static void SetMulticastFilter(struct device *dev, int num_addrs, char *addrs, char *multicast_table)
{
  char j, bit, byte;
  long *p = (long *) multicast_table;
  int i;
  u_short hashcode;
  u_long crc, poly = CRC_POLYNOMIAL_LE;

  if (num_addrs == HASH_TABLE_LEN) {
    for (i=0; i<(HASH_TABLE_LEN >> 4); i++) {
      *p++ = 0x0000ffff;
    }
  } else {
    /* Clear the multicast table except for the broadcast bit */
    memset(multicast_table, 0, (HASH_TABLE_LEN >> 2));
    *(multicast_table + (HASH_TABLE_LEN >> 3) - 3) = 0x80;

    /* Now update the table */
    for (i=0;i<num_addrs;i++) {              /* for each address in the list */
      if ((*addrs & 0x01) == 1) {            /* multicast address? */ 
	crc = 0xffffffff;                    /* init CRC for each address */
	for (byte=0;byte<ETH_ALEN;byte++) {  /* for each address byte */
	                                     /* process each address bit */ 
	  for (bit = *addrs++,j=0;j<8;j++, bit>>=1) {
	    crc = (crc >> 1) ^ (((crc ^ bit) & 0x01) ? poly : 0);
	  }
	}
	hashcode = crc & ((1 << 9) - 1);     /* hashcode is 9 LSb of CRC */

	byte = hashcode >> 3;                /* bit[3-8] -> byte in filter */
	bit = 1 << (hashcode & 0x07);        /* bit[0-2] -> bit in byte */

	byte <<= 1;                          /* calc offset into setup frame */
	if (byte & 0x02) {
	  byte -= 1;
	}
	multicast_table[byte] |= bit;

      } else {                               /* skip this address */
	addrs += ETH_ALEN;
      }
    }
  }

  return;
}

/*
** EISA bus I/O device probe. Probe from slot 1 since slot 0 is usually
** the motherboard. Upto 15 EISA devices are supported.
*/
static void eisa_probe(struct device *dev, short ioaddr)
{
  int i, maxSlots;
  int status;
  u_short iobase;
  struct bus_type *lp = &bus;

  if (!ioaddr && autoprobed) return ;            /* Been here before ! */
  if ((ioaddr < 0x1000) && (ioaddr > 0)) return; /* PCI MODULE special */

  lp->bus = EISA;

  if (ioaddr == 0) {                     /* Autoprobing */
    iobase = EISA_SLOT_INC;              /* Get the first slot address */
    i = 1;
    maxSlots = MAX_EISA_SLOTS;
  } else {                               /* Probe a specific location */
    iobase = ioaddr;
    i = (ioaddr >> 12);
    maxSlots = i + 1;
  }

  for (status = -ENODEV; i<maxSlots && dev!=NULL; i++, iobase+=EISA_SLOT_INC) {
    if ((DevicePresent(EISA_APROM) == 0) || is_zynx) { 
      if (check_region(iobase, DE4X5_EISA_TOTAL_SIZE) == 0) {
	if ((dev = alloc_device(dev, iobase)) != NULL) {
	  if ((status = de4x5_hw_init(dev, iobase)) == 0) {
	    num_de4x5s++;
	  }
	  num_eth++;
	}
      } else if (autoprobed) {
	printk("%s: region already allocated at 0x%04x.\n", dev->name, iobase);
      }
    }
  }

  return;
}

/*
** PCI bus I/O device probe
*/
#define PCI_DEVICE    (dev_num << 3)
#define PCI_LAST_DEV  32

static void pci_probe(struct device *dev, short ioaddr)

{
  u_char irq;
  u_short pb, dev_num, dev_last;
  u_short vendor, device, status;
  u_long class, iobase;
  struct bus_type *lp = &bus;

  if (!ioaddr && autoprobed) return ;        /* Been here before ! */

  if (pcibios_present()) {
    lp->bus = PCI;

    if (ioaddr < 0x1000) {
      pb = (u_short)(ioaddr >> 8);
      dev_num = (u_short)(ioaddr & 0xff);
    } else {
      pb = 0;
      dev_num = 0;
    }
    if (ioaddr > 0) {
      dev_last = (dev_num < PCI_LAST_DEV) ? dev_num + 1 : PCI_LAST_DEV;
    } else {
      dev_last = PCI_LAST_DEV;
    }

    for (; dev_num < dev_last && dev != NULL; dev_num++) {
      pcibios_read_config_dword(pb, PCI_DEVICE, PCI_CLASS_REVISION, &class);
      if (class != 0xffffffff) {
	pcibios_read_config_word(pb, PCI_DEVICE, PCI_VENDOR_ID, &vendor);
	pcibios_read_config_word(pb, PCI_DEVICE, PCI_DEVICE_ID, &device);
	if ((vendor == DC21040_VID) && (device == DC21040_DID)) {
	  /* Set the device number information */
	  lp->device = dev_num;

	  /* Get the board I/O address */
	  pcibios_read_config_dword(pb, PCI_DEVICE, PCI_BASE_ADDRESS_0, &iobase);
	  iobase &= CBIO_MASK;
	  
	  /* Fetch the IRQ to be used */
	  pcibios_read_config_byte(pb, PCI_DEVICE, PCI_INTERRUPT_LINE, &irq);

	  /* Enable I/O Accesses and Bus Mastering */
	  pcibios_read_config_word(pb, PCI_DEVICE, PCI_COMMAND, &status);
	  status |= PCI_COMMAND_IO | PCI_COMMAND_MASTER;
	  pcibios_write_config_word(pb, PCI_DEVICE, PCI_COMMAND, status);

	  /* If there is a device and I/O region is open, initialise dev. */
	  if ((DevicePresent(DE4X5_APROM) == 0) || is_zynx) {
	    if (check_region(iobase, DE4X5_PCI_TOTAL_SIZE) == 0) {
	      if ((dev = alloc_device(dev, iobase)) != NULL) {
		dev->irq = irq;
		if ((status = de4x5_hw_init(dev, iobase)) == 0) {
		  num_de4x5s++;
		}
		num_eth++;
	      }
	    } else if (autoprobed) {
	      printk("%s: region already allocated at 0x%04x.\n", dev->name, (u_short)iobase);
	    }
	  }
	}
      }
    }
  }

  return;
}

/*
** Allocate the device by pointing to the next available space in the
** device structure. Should one not be available, it is created.
*/
static struct device *alloc_device(struct device *dev, int iobase)
{
  int addAutoProbe = 0;
  struct device *tmp = NULL, *ret;
  int (*init)(struct device *) = NULL;

  /*
  ** Check the device structures for an end of list or unused device
  */
  if (!loading_module) {
    while (dev->next != NULL) {
      if ((dev->base_addr == 0xffe0) || (dev->base_addr == 0)) break;
      dev = dev->next;                     /* walk through eth device list */
      num_eth++;                           /* increment eth device number */
    }

    /*
    ** If an autoprobe is requested for another device, we must re-insert
    ** the request later in the list. Remember the current position first.
    */
    if ((dev->base_addr == 0) && (num_de4x5s > 0)) {
      addAutoProbe++;
      tmp = dev->next;                     /* point to the next device */
      init = dev->init;                    /* remember the probe function */
    }

    /*
    ** If at end of list and can't use current entry, malloc one up. 
    ** If memory could not be allocated, print an error message.
    */
    if ((dev->next == NULL) &&  
	!((dev->base_addr == 0xffe0) || (dev->base_addr == 0))){
      dev->next = (struct device *)kmalloc(sizeof(struct device) + 8,
					   GFP_KERNEL);

      dev = dev->next;                     /* point to the new device */
      if (dev == NULL) {
	printk("eth%d: Device not initialised, insufficient memory\n",
	       num_eth);
      } else {
	/*
	** If the memory was allocated, point to the new memory area
	** and initialize it (name, I/O address, next device (NULL) and
	** initialisation probe routine).
	*/
	dev->name = (char *)(dev + sizeof(struct device));
	if (num_eth > 9999) {
	  sprintf(dev->name,"eth????");    /* New device name */
	} else {
	  sprintf(dev->name,"eth%d", num_eth);/* New device name */
	}
	dev->base_addr = iobase;           /* assign the io address */
	dev->next = NULL;                  /* mark the end of list */
	dev->init = &de4x5_probe;          /* initialisation routine */
	num_de4x5s++;
      }
    }
    ret = dev;                             /* return current struct, or NULL */
  
    /*
    ** Now figure out what to do with the autoprobe that has to be inserted.
    ** Firstly, search the (possibly altered) list for an empty space.
    */
    if (ret != NULL) {
      if (addAutoProbe) {
	for (; (tmp->next!=NULL) && (tmp->base_addr!=0xffe0); tmp=tmp->next);

	/*
	** If no more device structures and can't use the current one, malloc
	** one up. If memory could not be allocated, print an error message.
	*/
	if ((tmp->next == NULL) && !(tmp->base_addr == 0xffe0)) {
	  tmp->next = (struct device *)kmalloc(sizeof(struct device) + 8,
					       GFP_KERNEL);
	  tmp = tmp->next;                     /* point to the new device */
	  if (tmp == NULL) {
	    printk("%s: Insufficient memory to extend the device list.\n", 
		   dev->name);
	  } else {
	    /*
	    ** If the memory was allocated, point to the new memory area
	    ** and initialize it (name, I/O address, next device (NULL) and
	    ** initialisation probe routine).
	    */
	    tmp->name = (char *)(tmp + sizeof(struct device));
	    if (num_eth > 9999) {
	      sprintf(tmp->name,"eth????");       /* New device name */
	    } else {
	      sprintf(tmp->name,"eth%d", num_eth);/* New device name */
	    }
	    tmp->base_addr = 0;                /* re-insert the io address */
	    tmp->next = NULL;                  /* mark the end of list */
	    tmp->init = init;                  /* initialisation routine */
	  }
	} else {                               /* structure already exists */
	  tmp->base_addr = 0;                  /* re-insert the io address */
	}
      }
    }
  } else {
    ret = dev;
  }

  return ret;
}

/*
** Auto configure the media here rather than setting the port at compile
** time. This routine is called by de4x5_init() when a loss of media is
** detected (excessive collisions, loss of carrier, no carrier or link fail
** [TP]) to check whether the user has been sneaky and changed the port on us.
*/
static int autoconf_media(struct device *dev)
{
  struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
  int media, entry, iobase = dev->base_addr;
  char frame[64];
  u_long i, omr, sisr, linkBad;
/*  u_long t_330ms = 920000;*/
  u_long t_3s    = 8000000;

  /* Set up for TP port, with LEDs */
  media = TP;
  RESET_SIA;
  outl(SICR_OE57 | SICR_SEL | SICR_SRL, DE4X5_SICR);

  /* Test the TP port */
  for (linkBad=1,i=0;i<t_3s && linkBad;i++) {
    if (((sisr = inl(DE4X5_SISR)) & SISR_LKF) == 0) linkBad = 0;
    if (sisr & SISR_NCR) break;
  }
    
  if (linkBad) {
    /* Set up for BNC (Thinwire) port, with LEDs */
    media = BNC;
    RESET_SIA;
    outl(SIGR_JCK | SIGR_HUJ, DE4X5_SIGR);
    outl(STRR_CLD | STRR_CSQ | STRR_RSQ | STRR_DREN | STRR_ECEN, DE4X5_STRR);
    outl(SICR_OE57| SICR_OE24 | SICR_OE13 | SICR_SEL |
 	                                    SICR_AUI | SICR_SRL, DE4X5_SICR);

    /* Wait 330ms */
    dce_ms_delay(330);
/*    for (i=0; i<t_330ms; i++) {
      sisr = inl(DE4X5_SISR);
    }
*/
    /* Make up a dummy packet with CRC error */
    create_packet(dev, frame, sizeof(frame));

    /* Setup the packet descriptor */
    entry = lp->tx_new;                        /* Remember the ring position */
    load_packet(dev, frame, TD_LS | TD_FS | TD_AC | sizeof(frame), NULL);

    /* Start the TX process */
    omr = inl(DE4X5_OMR);
    outl(omr|OMR_ST, DE4X5_OMR);

    /* Update pointers */
    lp->tx_new = (++lp->tx_new) % lp->txRingSize;
    lp->tx_old = lp->tx_new;

    /* 
    ** Poll for completion of frame (interrupts are disabled for now)...
    ** Allow upto 3 seconds to complete.
    */
    for (linkBad=1,i=0;i<t_3s && linkBad;i++) {
      if ((inl(DE4X5_SISR) & SISR_NCR) == 1) break;
      if (lp->tx_ring[entry].status >= 0) linkBad=0;
    }
    
    outl(omr, DE4X5_OMR);                        /* Stop everything! */

    if (linkBad || (lp->tx_ring[entry].status & TD_ES)) {
      /* Set up for AUI (Thickwire) port, with LEDs */
      media = AUI;
      RESET_SIA;
      outl(SIGR_JCK | SIGR_HUJ, DE4X5_SIGR);
      outl(STRR_CLD | STRR_CSQ | STRR_RSQ | STRR_DREN | STRR_ECEN, DE4X5_STRR);
      outl(SICR_OE57| SICR_SEL | SICR_AUI | SICR_SRL, DE4X5_SICR);
      
      /* Wait 330ms */
      dce_ms_delay(330);

      /* Setup the packet descriptor */
      entry = lp->tx_new;                      /* Remember the ring position */
      load_packet(dev, frame, TD_LS | TD_FS | TD_AC | sizeof(frame), NULL);
      
      /* Start the TX process */
      omr = inl(DE4X5_OMR);
      outl(omr|OMR_ST, DE4X5_OMR);

      /* Update pointers */
      lp->tx_new = (++lp->tx_new) % lp->txRingSize;
      lp->tx_old = lp->tx_new;

      /* 
      ** Poll for completion of frame (interrupts are disabled for now)...
      ** Allow 3 seconds to complete.
      */
      for (linkBad=1,i=0;i<t_3s && linkBad;i++) {
	if ((inl(DE4X5_SISR) & SISR_NCR) == 1) break;
	if (lp->tx_ring[entry].status >= 0) linkBad=0;
      }
    
      outl(omr, DE4X5_OMR);                        /* Stop everything! */

      if (linkBad || (lp->tx_ring[entry].status & TD_ES)) {
	/* Reset the SIA */
	outl(SICR_RESET, DE4X5_SICR);        /* Reset SIA connectivity regs */
	outl(STRR_RESET, DE4X5_STRR);        /* Write reset values */
	outl(SIGR_RESET, DE4X5_SIGR);        /* Write reset values */

	media = NC;
      }
    } 
  }

  if (de4x5_debug >= 1 ) {
    printk("%s: Media is %s.\n",dev->name, 
                                 (media == NC  ? "unconnected to this device" :
                                 (media == TP  ? "TP" :
                                 (media == BNC ? "BNC" : 
                                                 "AUI"))));
  }

  if (media) lp->lostMedia = 0;

  return media;
}

/*
** Create an Ethernet packet with an invalid CRC
*/
static void create_packet(struct device *dev, char *frame, int len)
{
  int i, j;
  char *buf = frame;

  for (i=0; i<ETH_ALEN; i++) {             /* Use this source address */
    *buf++ = dev->dev_addr[i];
  }
  for (i=0; i<ETH_ALEN; i++) {             /* Use this destination address */
    *buf++ = dev->dev_addr[i];
  }
  for (j=1; j>=0; j--) {                   /* Packet length (2 bytes) */
    *buf++ = (char) ((len >> 8*j) & 0xff);
  }
  *buf++ = 0;                              /* Data */

  for (i=len-4; i<len; i++) {              /* CRC */
    buf[i] = 0;
  }
  
  return;
}

/*
** Get the timer ticks from the PIT
*/
static u_short dce_get_ticks(void)
{
  u_short ticks = 0;
  
  /* Command 8254 to latch T0's count */
  outb(TIMER_PORT, TIMER_LATCH);
  
  /* Read the counter */
  ticks = inb(TIMER_READ);
  ticks |= (inb(TIMER_READ) << 8);
  
  return ticks;
}

/*
** Known delay in microseconds
*/
static void dce_us_delay(u_long usec)
{
  u_long i, start, now, quant=(DELAY_QUANT*1000)/TIMER_TICK+1;
  
  for (i=0; i<usec/DELAY_QUANT; i++) {
    start=dce_get_ticks();  
    for (now=start; (start-now)<quant;) {
      now=dce_get_ticks();
      if (now > start) {         /* Wrapped counter counting down */
	quant -= start;
	start = (1 << TIMER_WIDTH);
      }
    }
  }

  return;
}

/*
** Known delay in milliseconds
*/
static void dce_ms_delay(u_long msec)
{
  u_long i;
  
  for (i=0; i<msec; i++) {
    dce_us_delay(1000);
  }

  return;
}


/*
** Look for a particular board name in the EISA configuration space
*/
static void EISA_signature(char *name, short iobase)
{
  unsigned long i;
  char *signatures[] = DE4X5_SIGNATURE;
  char ManCode[8];
  union {
    u_long ID;
    u_char Id[4];
  } Eisa;

  strcpy(name, "");
  Eisa.ID = inl(iobase);

  ManCode[0]=(((Eisa.Id[0]>>2)&0x1f)+0x40);
  ManCode[1]=(((Eisa.Id[1]&0xe0)>>5)+((Eisa.Id[0]&0x03)<<3)+0x40);
  ManCode[2]=(((Eisa.Id[2]>>4)&0x0f)+0x30);
  ManCode[3]=((Eisa.Id[2]&0x0f)+0x30);
  ManCode[4]=(((Eisa.Id[3]>>4)&0x0f)+0x30);
  ManCode[5]='\0';

  for (i=0;*signatures[i] != '\0' && *name == '\0';i++) {
    if (strstr(ManCode, signatures[i]) != NULL) {
      strcpy(name,ManCode);
    }
  }
  
  return;                                   /* return the device name string */
}

/*
** Look for a special sequence in the Ethernet station address PROM that
** is common across all DIGITAL network adapter products.
** 
** Search the Ethernet address ROM for the signature. Since the ROM address
** counter can start at an arbitrary point, the search must include the entire
** probe sequence length plus the (length_of_the_signature - 1).
** Stop the search IMMEDIATELY after the signature is found so that the
** PROM address counter is correctly positioned at the start of the
** ethernet address for later read out.
*/

static int DevicePresent(short aprom_addr)
{
  union {
    struct {
      u_long a;
      u_long b;
    } llsig;
    char Sig[sizeof(long) << 1];
  } dev;
  char data;
  long i, j, tmp;
  short sigLength;
  int status = 0;
  struct bus_type *lp = &bus;

  dev.llsig.a = ETH_PROM_SIG;
  dev.llsig.b = ETH_PROM_SIG;
  sigLength = sizeof(long) << 1;

  for (i=0,j=0;j<sigLength && i<PROBE_LENGTH+sigLength-1;i++) {
    if (lp->bus == PCI) {
      while ((tmp = inl(aprom_addr)) < 0);
      data = (char)tmp;
    } else {
      data = inb(aprom_addr);
    }
    if (dev.Sig[j] == data) {   /* track signature */
      j++;
    } else {                    /* lost signature; begin search again */
      if (data == dev.Sig[0]) {
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

static int aprom_crc(struct device *dev)
{
  int iobase = dev->base_addr;
  long i, k, tmp;
  unsigned short j,chksum;
  unsigned char status = 0;
  struct bus_type *lp = &bus;

  for (i=0,k=0,j=0;j<3;j++) {
    k <<= 1 ;
    if (k > 0xffff) k-=0xffff;

    if (lp->bus == PCI) {
      while ((tmp = inl(DE4X5_APROM)) < 0);
      k += (u_char) tmp;
      dev->dev_addr[i++] = (u_char) tmp;
      while ((tmp = inl(DE4X5_APROM)) < 0);
      k += (u_short) (tmp << 8);
      dev->dev_addr[i++] = (u_char) tmp;
    } else {
      k += (u_char) (tmp = inb(EISA_APROM));
      dev->dev_addr[i++] = (u_char) tmp;
      k += (u_short) ((tmp = inb(EISA_APROM)) << 8);
      dev->dev_addr[i++] = (u_char) tmp;
    }

    if (k > 0xffff) k-=0xffff;
  }
  if (k == 0xffff) k=0;

  if (lp->bus == PCI) {
    while ((tmp = inl(DE4X5_APROM)) < 0);
    chksum = (u_char) tmp;
    while ((tmp = inl(DE4X5_APROM)) < 0);
    chksum |= (u_short) (tmp << 8);
  } else {
    chksum = (u_char) inb(EISA_APROM);
    chksum |= (u_short) (inb(EISA_APROM) << 8);
  }

  if (k != chksum) status = -1;

  return status;
}

/*
** Perform IOCTL call functions here. Some are privileged operations and the
** effective uid is checked in those cases.
*/
static int de4x5_ioctl(struct device *dev, struct ifreq *rq, int cmd)
{
  struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
  struct de4x5_ioctl *ioc = (struct de4x5_ioctl *) &rq->ifr_data;
  int i, j, iobase = dev->base_addr, status = 0;
  u_long omr;
  union {
    unsigned char  addr[(HASH_TABLE_LEN * ETH_ALEN)];
    unsigned short sval[(HASH_TABLE_LEN * ETH_ALEN) >> 1];
    unsigned long  lval[(HASH_TABLE_LEN * ETH_ALEN) >> 2];
  } tmp;

  switch(ioc->cmd) {
  case DE4X5_GET_HWADDR:             /* Get the hardware address */
    for (i=0; i<ETH_ALEN; i++) {
      tmp.addr[i] = dev->dev_addr[i];
    }
    ioc->len = ETH_ALEN;
    memcpy_tofs(ioc->data, tmp.addr, ioc->len);

    break;
  case DE4X5_SET_HWADDR:             /* Set the hardware address */
    if (suser()) {
      int offset;
      char *pa;
      u_long omr;

      memcpy_fromfs(tmp.addr,ioc->data,ETH_ALEN);
      for (i=0; i<ETH_ALEN; i++) {
	dev->dev_addr[i] = tmp.addr[i];
      }
      omr = inl(DE4X5_OMR);
      if (omr & OMR_HP) {
	offset = IMPERF_PA_OFFSET;
      } else {
	offset = PERF_PA_OFFSET;
      }
      /* Insert the physical address */
      for (pa=lp->setup_frame+offset, i=0; i<ETH_ALEN; i++) {
	*(pa + i) = dev->dev_addr[i];
	if (i & 0x01) pa += 2;
      }
      /* Set up the descriptor and give ownership to the card */
      while (set_bit(0, (void *)&dev->tbusy) != 0); /* Wait for lock to free */
      load_packet(dev, lp->setup_frame, TD_IC | HASH_F | TD_SET | 
		                                        SETUP_FRAME_LEN, NULL);
      lp->tx_new = (++lp->tx_new) % lp->txRingSize;
      outl(POLL_DEMAND, DE4X5_TPD);                /* Start the TX */
      dev->tbusy = 0;                              /* Unlock the TX ring */

    } else {
      status = -EPERM;
    }

    break;
  case DE4X5_SET_PROM:               /* Set Promiscuous Mode */
    if (suser()) {
      omr = inl(DE4X5_OMR);
      omr |= OMR_PR;
      omr &= ~OMR_PM;
      outl(omr, DE4X5_OMR);
    } else {
      status = -EPERM;
    }

    break;
  case DE4X5_CLR_PROM:               /* Clear Promiscuous Mode */
    if (suser()) {
      omr = inl(DE4X5_OMR);
      omr &= ~OMR_PR;
      outb(omr, DE4X5_OMR);
    } else {
      status = -EPERM;
    }

    break;
  case DE4X5_SAY_BOO:                /* Say "Boo!" to the kernel log file */
    printk("%s: Boo!\n", dev->name);

    break;
  case DE4X5_GET_MCA:                /* Get the multicast address table */
    ioc->len = (HASH_TABLE_LEN >> 3);
    memcpy_tofs(ioc->data, lp->setup_frame, 192); 

    break;
  case DE4X5_SET_MCA:                /* Set a multicast address */
    if (suser()) {
      if (ioc->len != HASH_TABLE_LEN) {         /* MCA changes */
	memcpy_fromfs(tmp.addr, ioc->data, ETH_ALEN * ioc->len);
      }
      set_multicast_list(dev, ioc->len, tmp.addr);
    } else {
      status = -EPERM;
    }

    break;
  case DE4X5_CLR_MCA:                /* Clear all multicast addresses */
    if (suser()) {
      set_multicast_list(dev, 0, NULL);
    } else {
      status = -EPERM;
    }

    break;
  case DE4X5_MCA_EN:                 /* Enable multicast addressing */
    if (suser()) {
      omr = inl(DE4X5_OMR);
      omr |= OMR_PM;
      omr &= ~OMR_PR;
      outl(omr, DE4X5_OMR);
    } else {
      status = -EPERM;
    }

    break;
  case DE4X5_GET_STATS:              /* Get the driver statistics */
    cli();
    memcpy_tofs(ioc->data, &lp->pktStats, sizeof(lp->pktStats)); 
    ioc->len = DE4X5_PKT_STAT_SZ;
    sti();

    break;
  case DE4X5_CLR_STATS:              /* Zero out the driver statistics */
    if (suser()) {
      cli();
      memset(&lp->pktStats, 0, sizeof(lp->pktStats));
      sti();
    } else {
      status = -EPERM;
    }

    break;
  case DE4X5_GET_OMR:                /* Get the OMR Register contents */
    tmp.addr[0] = inl(DE4X5_OMR);
    memcpy_tofs(ioc->data, tmp.addr, 1);

    break;
  case DE4X5_SET_OMR:                /* Set the OMR Register contents */
    if (suser()) {
      memcpy_fromfs(tmp.addr, ioc->data, 1);
      outl(tmp.addr[0], DE4X5_OMR);
    } else {
      status = -EPERM;
    }

    break;
  case DE4X5_GET_REG:                /* Get the DE4X5 Registers */
    tmp.lval[0] = inl(DE4X5_STS);
    tmp.lval[1] = inl(DE4X5_BMR);
    tmp.lval[2] = inl(DE4X5_IMR);
    tmp.lval[3] = inl(DE4X5_OMR);
    tmp.lval[4] = inl(DE4X5_SISR);
    tmp.lval[5] = inl(DE4X5_SICR);
    tmp.lval[6] = inl(DE4X5_STRR);
    tmp.lval[7] = inl(DE4X5_SIGR);
    memcpy_tofs(ioc->data, tmp.addr, 32);

    break;

#define DE4X5_DUMP              0x0f /* Dump the DE4X5 Status */

  case DE4X5_DUMP:
    j = 0;
    tmp.addr[j++] = dev->irq;
    for (i=0; i<ETH_ALEN; i++) {
      tmp.addr[j++] = dev->dev_addr[i];
    }
    tmp.addr[j++] = lp->rxRingSize;
    tmp.lval[j>>2] = (long)lp->rx_ring; j+=4;
    tmp.lval[j>>2] = (long)lp->tx_ring; j+=4;

    for (i=0;i<lp->rxRingSize-1;i++){
      if (i < 3) {
	tmp.lval[j>>2] = (long)&lp->rx_ring[i].status; j+=4;
      }
    }
    tmp.lval[j>>2] = (long)&lp->rx_ring[i].status; j+=4;
    for (i=0;i<lp->txRingSize-1;i++){
      if (i < 3) {
	tmp.lval[j>>2] = (long)&lp->tx_ring[i].status; j+=4;
      }
    }
    tmp.lval[j>>2] = (long)&lp->tx_ring[i].status; j+=4;

    for (i=0;i<lp->rxRingSize-1;i++){
      if (i < 3) {
	tmp.lval[j>>2] = (long)lp->rx_ring[i].buf; j+=4;
      }
    }
    tmp.lval[j>>2] = (long)lp->rx_ring[i].buf; j+=4;
    for (i=0;i<lp->txRingSize-1;i++){
      if (i < 3) {
	tmp.lval[j>>2] = (long)lp->tx_ring[i].buf; j+=4;
      }
    }
    tmp.lval[j>>2] = (long)lp->tx_ring[i].buf; j+=4;

    for (i=0;i<lp->rxRingSize;i++){
      tmp.lval[j>>2] = lp->rx_ring[i].status; j+=4;
    }
    for (i=0;i<lp->txRingSize;i++){
      tmp.lval[j>>2] = lp->tx_ring[i].status; j+=4;
    }

    tmp.lval[j>>2] = inl(DE4X5_STS); j+=4;
    tmp.lval[j>>2] = inl(DE4X5_BMR); j+=4;
    tmp.lval[j>>2] = inl(DE4X5_IMR); j+=4;
    tmp.lval[j>>2] = inl(DE4X5_OMR); j+=4;
    tmp.lval[j>>2] = inl(DE4X5_SISR); j+=4;
    tmp.lval[j>>2] = inl(DE4X5_SICR); j+=4;
    tmp.lval[j>>2] = inl(DE4X5_STRR); j+=4;
    tmp.lval[j>>2] = inl(DE4X5_SIGR); j+=4; 

    tmp.addr[j++] = lp->txRingSize;
    tmp.addr[j++] = dev->tbusy;

    ioc->len = j;
    memcpy_tofs(ioc->data, tmp.addr, ioc->len);

    break;
  default:
    status = -EOPNOTSUPP;
  }

  return status;
}

#ifdef MODULE
char kernel_version[] = UTS_RELEASE;
static struct device thisDE4X5 = {
  "        ", /* device name inserted by /linux/drivers/net/net_init.c */
  0, 0, 0, 0,
  0x2000, 10, /* I/O address, IRQ */
  0, 0, 0, NULL, de4x5_probe };
	
int io=0x000b;	/* <--- EDIT THESE LINES FOR YOUR CONFIGURATION */
int irq=10;	/* or use the insmod io= irq= options 		*/

int
init_module(void)
{
  thisDE4X5.base_addr=io;
  thisDE4X5.irq=irq;
  if (register_netdev(&thisDE4X5) != 0)
    return -EIO;
  return 0;
}

void
cleanup_module(void)
{
  struct de4x5_private *lp = (struct de4x5_private *) thisDE4X5.priv;

  if (MOD_IN_USE) {
    printk("%s: device busy, remove delayed\n",thisDE4X5.name);
  } else {
    release_region(thisDE4X5.base_addr, (lp->bus == PCI ? 
					             DE4X5_PCI_TOTAL_SIZE :
			                             DE4X5_EISA_TOTAL_SIZE));
    if (lp) {
      kfree_s(lp->rx_ring[0].buf, RX_BUFF_SZ * NUM_RX_DESC + LWPAD);
    }
    kfree_s(thisDE4X5.priv, sizeof(struct de4x5_private) + LWPAD);
    thisDE4X5.priv = NULL;

    unregister_netdev(&thisDE4X5);
  }
}
#endif /* MODULE */


/*
 * Local variables:
 *  kernel-compile-command: "gcc -D__KERNEL__ -I/usr/src/linux/net/inet -Wall -Wstrict-prototypes -O2 -m486 -c de4x5.c"
 *
 *  module-compile-command: "gcc -D__KERNEL__ -DMODULE -I/usr/src/linux/net/inet -Wall -Wstrict-prototypes -O2 -m486 -c de4x5.c"
 * End:
 */


