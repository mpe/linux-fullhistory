/*
 * sonic.c
 *
 * (C) 1996 by Thomas Bogendoerfer (tsbogend@bigbug.franken.de)
 * 
 * This driver is based on work from Andreas Busse, but most of
 * the code is rewritten.
 * 
 * (C) 1995 by Andreas Busse (andy@waldorf-gmbh.de)
 *
 * A driver for the onboard Sonic ethernet controller on Mips Jazz
 * systems (Acer Pica-61, Mips Magnum 4000, Olivetti M700 and
 * perhaps others, too)
 */

static const char *version =
	"sonic.c:v0.10 6.7.96 tsbogend@bigbug.franken.de\n";

/*
 * Sources: Olivetti M700-10 Risc Personal Computer hardware handbook,
 * National Semiconductors data sheet for the DP83932B Sonic Ethernet
 * controller, and the files "8390.c" and "skeleton.c" in this directory.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <asm/bootinfo.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/pgtable.h>
#include <asm/segment.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/jazz.h>
#include <asm/jazzdma.h>
#include <linux/errno.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include "sonic.h"

/* use 0 for production, 1 for verification, >2 for debug */
#ifdef SONIC_DEBUG
static unsigned int sonic_debug = SONIC_DEBUG;
#else 
static unsigned int sonic_debug = 2;
#endif

/*
 * Some tunables for the buffer areas. Power of 2 is required
 * the current driver uses one receive buffer for each descriptor.
 */
#define SONIC_NUM_RRS    16             /* number of receive resources */
#define SONIC_NUM_RDS    SONIC_NUM_RRS  /* number of receive descriptors */
#define SONIC_NUM_TDS    16      /* number of transmit descriptors */
#define SONIC_RBSIZE   1520      /* size of one resource buffer */

#define SONIC_RDS_MASK   (SONIC_NUM_RDS-1)
#define SONIC_TDS_MASK   (SONIC_NUM_TDS-1)

/*
 * Base address and interupt of the SONIC controller on JAZZ boards
 */
static struct {
    unsigned int port;
    unsigned int irq;
    } sonic_portlist[] = { {JAZZ_ETHERNET_BASE, JAZZ_ETHERNET_IRQ}, {0, 0}};


/* Information that need to be kept for each board. */
struct sonic_local {
    sonic_cda_t   cda;                     /* virtual CPU address of CDA */
    sonic_td_t    tda[SONIC_NUM_TDS];      /* transmit descriptor area */
    sonic_rr_t    rra[SONIC_NUM_RRS];      /* receive resource arrea */
    sonic_rd_t    rda[SONIC_NUM_RDS];      /* receive descriptor area */
    struct sk_buff* tx_skb[SONIC_NUM_TDS]; /* skbuffs for packets to transmit */
    unsigned int  tx_laddr[SONIC_NUM_TDS]; /* logical DMA address fro skbuffs */
    unsigned char *rba;                    /* start of receive buffer areas */    
    unsigned int  cda_laddr;               /* logical DMA address of CDA */    
    unsigned int  tda_laddr;               /* logical DMA address of TDA */
    unsigned int  rra_laddr;               /* logical DMA address of RRA */    
    unsigned int  rda_laddr;               /* logical DMA address of RDA */
    unsigned int  rba_laddr;               /* logical DMA address of RBA */
    unsigned int  cur_tx, cur_rx;          /* current indexes to resource areas */
    unsigned int  dirty_tx,cur_rra;        /* last unacked transmit packet */
    char tx_full;
    struct enet_statistics stats;
};

/*
 * We cannot use station (ethernet) address prefixes to detect the
 * sonic controller since these are board manufacturer depended.
 * So we check for known Silicon Revision IDs instead. 
 */
static unsigned short known_revisions[] =
{
  0x04,				/* Mips Magnum 4000 */
  0xffff			/* end of list */
};

/* Index to functions, as function prototypes. */

extern int sonic_probe(struct device *dev);
static int sonic_probe1(struct device *dev, unsigned int base_addr, unsigned int irq);
static int sonic_open(struct device *dev);
static int sonic_send_packet(struct sk_buff *skb, struct device *dev);
static void sonic_interrupt(int irq, void *dev_id, struct pt_regs *regs);
static void sonic_rx(struct device *dev);
static int sonic_close(struct device *dev);
static struct enet_statistics *sonic_get_stats(struct device *dev);
static void sonic_multicast_list(struct device *dev);
static int sonic_init(struct device *dev);


/*
 * Probe for a SONIC ethernet controller on a Mips Jazz board.
 * Actually probing is superfluous but we're paranoid.
 */
__initfunc(int sonic_probe(struct device *dev))
{
    unsigned int base_addr = dev ? dev->base_addr : 0;
    int i;

    /*
     * Don't probe if we're not running on a Jazz board.
     */
    if (mips_machgroup != MACH_GROUP_JAZZ)
	return -ENODEV;
    if (base_addr > 0x1ff)	/* Check a single specified location. */
	return sonic_probe1(dev, base_addr, dev->irq);
    else if (base_addr != 0)	/* Don't probe at all. */
	return -ENXIO;
    
    for (i = 0; sonic_portlist[i].port; i++) {
	int base_addr = sonic_portlist[i].port;
	if (check_region(base_addr, 0x100))
	    continue;
	if (sonic_probe1(dev, base_addr, sonic_portlist[i].irq) == 0)
	    return 0;
    }
    return -ENODEV;
}

__initfunc(static int sonic_probe1(struct device *dev,
                                   unsigned int base_addr, unsigned int irq))
{
    static unsigned version_printed = 0;
    unsigned int silicon_revision;
    unsigned int val;
    struct sonic_local *lp;
    int i;
    
    /*
     * get the Silicon Revision ID. If this is one of the known
     * one assume that we found a SONIC ethernet controller at
     * the expected location.
     */
    silicon_revision = SONIC_READ(SONIC_SR);
    if (sonic_debug > 1)
      printk("SONIC Silicon Revision = 0x%04x\n",silicon_revision);

    i = 0;
    while ((known_revisions[i] != 0xffff) &&
	   (known_revisions[i] != silicon_revision))
      i++;
	
    if (known_revisions[i] == 0xffff) {
	printk("SONIC ethernet controller not found (0x%4x)\n",
	       silicon_revision);
	return -ENODEV;
    }
    
    request_region(base_addr, 0x100, "SONIC");
    
    /* Allocate a new 'dev' if needed. */
    if (dev == NULL)
      dev = init_etherdev(0, sizeof(struct sonic_local));

    if (sonic_debug  &&  version_printed++ == 0)
      printk(version);

    printk("%s: %s found at 0x%08x, ",
	   dev->name, "SONIC ethernet", base_addr);

    /* Fill in the 'dev' fields. */
    dev->base_addr = base_addr;
    dev->irq = irq;

    /*
     * Put the sonic into software reset, then
     * retrieve and print the ethernet address.
     */
    SONIC_WRITE(SONIC_CMD,SONIC_CR_RST);
    SONIC_WRITE(SONIC_CEP,0);
    for (i=0; i<3; i++) {
	val = SONIC_READ(SONIC_CAP0-i);
	dev->dev_addr[i*2] = val;
	dev->dev_addr[i*2+1] = val >> 8;
    }

    printk("HW Address ");
    for (i = 0; i < 6; i++) {
	printk("%2.2x", dev->dev_addr[i]);
	if (i<5)
	  printk(":");
    }
    
    printk(" IRQ %d\n", irq);
    
    /* Initialize the device structure. */
    if (dev->priv == NULL) {
	/*
	 * the memory be located in the same 64kb segment
	 */
	lp = NULL;
	i = 0;
	do {
	    lp = (struct sonic_local *)kmalloc(sizeof(*lp), GFP_KERNEL);
	    if ((unsigned long)lp >> 16 != ((unsigned long)lp + sizeof(*lp) ) >> 16) {
		/* FIXME, free the memory later */
		kfree (lp);
		lp = NULL;
	    }
	} while (lp == NULL && i++ < 20);
	
	if (lp == NULL) {
	    printk ("%s: couldn't allocate memory for descriptors\n",
	            dev->name);
	    return -ENOMEM;
	}
	
	memset(lp, 0, sizeof(struct sonic_local));
	
	/* get the virtual dma address */
	lp->cda_laddr = vdma_alloc(PHYSADDR(lp),sizeof(*lp));
	if (lp->cda_laddr == ~0UL) {
	    printk ("%s: couldn't get DMA page entry for descriptors\n",
	            dev->name);
	    return -ENOMEM;
	}

	lp->tda_laddr = lp->cda_laddr + sizeof (lp->cda);
	lp->rra_laddr = lp->tda_laddr + sizeof (lp->tda);
	lp->rda_laddr = lp->rra_laddr + sizeof (lp->rra);
	
	/* allocate receive buffer area */
	/* FIXME, maybe we should use skbs */
	if ((lp->rba = (char *)kmalloc(SONIC_NUM_RRS * SONIC_RBSIZE, GFP_KERNEL)) == NULL) {
	    printk ("%s: couldn't allocate receive buffers\n",dev->name);
	    return -ENOMEM;
	}
	
	/* get virtual dma address */
	if ((lp->rba_laddr = vdma_alloc(PHYSADDR(lp->rba),SONIC_NUM_RRS * SONIC_RBSIZE)) == ~0UL) {
	    printk ("%s: couldn't get DMA page entry for receive buffers\n",dev->name);
	    return -ENOMEM;
	}
	
	/* now convert pointer to KSEG1 pointer */
	lp->rba = (char *)KSEG1ADDR(lp->rba);
	flush_cache_all();
	dev->priv = (struct sonic_local *)KSEG1ADDR(lp);
    }

    lp = (struct sonic_local *)dev->priv;
    dev->open = sonic_open;
    dev->stop = sonic_close;
    dev->hard_start_xmit = sonic_send_packet;
    dev->get_stats	= sonic_get_stats;
    dev->set_multicast_list = &sonic_multicast_list;

    /* Fill in the fields of the device structure with ethernet values. */
    ether_setup(dev);
    return 0;
}

/*
 * Open/initialize the SONIC controller.
 *
 * This routine should set everything up anew at each open, even
 *  registers that "should" only need to be set once at boot, so that
 *  there is non-reboot way to recover if something goes wrong.
 */
static int sonic_open(struct device *dev)
{
    if (sonic_debug > 2)
      printk("sonic_open: initializing sonic driver.\n");
    
    /*
     * We don't need to deal with auto-irq stuff since we
     * hardwire the sonic interrupt.
     */
/*
 * XXX Horrible work around:  We install sonic_interrupt as fast interrupt.
 * This means that during execution of the handler interrupt are disabled
 * covering another bug otherwise corrupting data.  This doesn't mean
 * this glue works ok under all situations.
 */
//    if (request_irq(dev->irq, &sonic_interrupt, 0, "sonic", dev)) {
    if (request_irq(dev->irq, &sonic_interrupt, SA_INTERRUPT, "sonic", dev)) {
	printk ("\n%s: unable to get IRQ %d .\n", dev->name, dev->irq);
	return EAGAIN;
    }

    /*
     * Initialize the SONIC
     */
    sonic_init(dev);

    dev->tbusy = 0;
    dev->interrupt = 0;
    dev->start = 1;

    if (sonic_debug > 2)
      printk("sonic_open: Initialization done.\n");
	
    return 0;
}


/*
 * Close the SONIC device
 */
static int
sonic_close(struct device *dev)
{
    unsigned int base_addr = dev->base_addr;
    
    if (sonic_debug > 2)
      printk ("sonic_close\n");

    dev->tbusy = 1;
    dev->start = 0;
    
    /*
     * stop the SONIC, disable interrupts
     */
    SONIC_WRITE(SONIC_ISR,0x7fff);
    SONIC_WRITE(SONIC_IMR,0);
    SONIC_WRITE(SONIC_CMD,SONIC_CR_RST);

    free_irq(dev->irq, dev);			/* release the IRQ */

    return 0;
}


/*
 * transmit packet
 */
static int sonic_send_packet(struct sk_buff *skb, struct device *dev)
{
    struct sonic_local *lp = (struct sonic_local *)dev->priv;
    unsigned int base_addr = dev->base_addr;
    unsigned int laddr;
    int entry,length;
    
    if (sonic_debug > 2)
      printk("sonic_send_packet: skb=%p, dev=%p\n",skb,dev);
  
    if (dev->tbusy) {
	int tickssofar = jiffies - dev->trans_start;

	/* If we get here, some higher level has decided we are broken.
	 There should really be a "kick me" function call instead. */
      
	if (sonic_debug > 1)
	  printk("sonic_send_packet: called with dev->tbusy = 1 !\n");
	
	if (tickssofar < 5)
	  return 1;
	
	printk("%s: transmit timed out.\n", dev->name);
	
	/* Try to restart the adaptor. */
	sonic_init(dev);
	dev->tbusy=0;
	dev->trans_start = jiffies;
    }

    /* 
     * Block a timer-based transmit from overlapping.  This could better be
     * done with atomic_swap(1, dev->tbusy), but set_bit() works as well.
     */
    if (test_and_set_bit(0, (void*)&dev->tbusy) != 0) {
	printk("%s: Transmitter access conflict.\n", dev->name);
	return 1;
    }
    
    /*
     * Map the packet data into the logical DMA address space
     */
    if ((laddr = vdma_alloc(PHYSADDR(skb->data),skb->len)) == ~0UL) {
	printk("%s: no VDMA entry for transmit available.\n",dev->name);
	dev_kfree_skb(skb);
	dev->tbusy = 0;
	return 1;
    }
    entry = lp->cur_tx & SONIC_TDS_MASK;    
    lp->tx_laddr[entry] = laddr;
    lp->tx_skb[entry] = skb;
    
    length = (skb->len < ETH_ZLEN) ? ETH_ZLEN : skb->len;
    flush_cache_all();
    
    /*
     * Setup the transmit descriptor and issue the transmit command.
     */
    lp->tda[entry].tx_status = 0;		/* clear status */
    lp->tda[entry].tx_frag_count = 1;		/* single fragment */
    lp->tda[entry].tx_pktsize = length;		/* length of packet */    
    lp->tda[entry].tx_frag_ptr_l = laddr & 0xffff;
    lp->tda[entry].tx_frag_ptr_h = laddr >> 16;
    lp->tda[entry].tx_frag_size  = length;
    
    /* if there are already packets queued, allow sending several packets at once */
    if (lp->dirty_tx != lp->cur_tx)
	lp->tda[(lp->cur_tx-1) % SONIC_TDS_MASK].link &= ~SONIC_END_OF_LINKS;
    
    lp->cur_tx++;
    lp->stats.tx_bytes += length;
    
    if (sonic_debug > 2)
      printk("sonic_send_packet: issueing Tx command\n");

    SONIC_WRITE(SONIC_CMD,SONIC_CR_TXP);

    dev->trans_start = jiffies;

    if (lp->cur_tx < lp->dirty_tx + SONIC_NUM_TDS)
      dev->tbusy = 0;
    else
      lp->tx_full = 1;
    
    return 0;
}


/*
 * The typical workload of the driver:
 * Handle the network interface interrupts.
 */
static void
sonic_interrupt(int irq, void *dev_id, struct pt_regs * regs)
{
    struct device *dev = (struct device *)dev_id;
    unsigned int base_addr = dev->base_addr;
    struct sonic_local *lp;
    int status;

    if (dev == NULL) {
	printk ("sonic_interrupt: irq %d for unknown device.\n", irq);
	return;
    }
    dev->interrupt = 1;
    lp = (struct sonic_local *)dev->priv;

    status = SONIC_READ(SONIC_ISR);
    SONIC_WRITE(SONIC_ISR,0x7fff); /* clear all bits */
  
    if (sonic_debug > 2)
      printk("sonic_interrupt: ISR=%x\n",status);

    if (status & SONIC_INT_PKTRX) {
	sonic_rx(dev);				/* got packet(s) */
    }
    
    if (status & SONIC_INT_TXDN) {
	int dirty_tx = lp->dirty_tx;
	
	while (dirty_tx < lp->cur_tx) {
	    int entry = dirty_tx & SONIC_TDS_MASK;
	    int status = lp->tda[entry].tx_status;
	    
	    if (sonic_debug > 3)
	      printk ("sonic_interrupt: status %d, cur_tx %d, dirty_tx %d\n",
		      status,lp->cur_tx,lp->dirty_tx);
	    
	    if (status == 0)
	      break;			/* It still hasn't been Txed */

	    /* put back EOL and free descriptor */
	    lp->tda[entry].link |= SONIC_END_OF_LINKS;
	    lp->tda[entry].tx_status = 0;

	    if (status & 0x0001)
	      lp->stats.tx_packets++;
	    else {
		lp->stats.tx_errors++;
		if (status & 0x0642) lp->stats.tx_aborted_errors++;
		if (status & 0x0180) lp->stats.tx_carrier_errors++;
		if (status & 0x0020) lp->stats.tx_window_errors++;
		if (status & 0x0004) lp->stats.tx_fifo_errors++;
	    }

	    /* We must free the original skb */
	    if (lp->tx_skb[entry]) {
		dev_kfree_skb(lp->tx_skb[entry]);
		lp->tx_skb[entry] = 0;
	    }
	    /* and the VDMA address */
	    vdma_free(lp->tx_laddr[entry]);
	    dirty_tx++;
	}
	
	if (lp->tx_full && dev->tbusy
	    && dirty_tx + SONIC_NUM_TDS > lp->cur_tx + 2) {
	    /* The ring is no longer full, clear tbusy. */
	    lp->tx_full = 0;
	    dev->tbusy = 0;
	    mark_bh(NET_BH);
	}
	
	lp->dirty_tx = dirty_tx;
    }
    
    /*
     * check error conditions
     */
    if (status & SONIC_INT_RFO) {
	printk ("%s: receive fifo underrun\n",dev->name);
	lp->stats.rx_fifo_errors++;
    }
    if (status & SONIC_INT_RDE) {
	printk ("%s: receive descriptors exhausted\n",dev->name);
	lp->stats.rx_dropped++;
    }
    if (status & SONIC_INT_RBE) {
	printk ("%s: receive buffer exhausted\n",dev->name);
	lp->stats.rx_dropped++;	
    }
    if (status & SONIC_INT_RBAE) {
	printk ("%s: receive buffer area exhausted\n",dev->name);
	lp->stats.rx_dropped++;	
    }

    /* counter overruns; all counters are 16bit wide */
    if (status & SONIC_INT_FAE)
      lp->stats.rx_frame_errors += 65536;
    if (status & SONIC_INT_CRC)
      lp->stats.rx_crc_errors += 65536;
    if (status & SONIC_INT_MP)
      lp->stats.rx_missed_errors += 65536;

    /* transmit error */
    if (status & SONIC_INT_TXER)
      lp->stats.tx_errors++;
    
    /*
     * clear interrupt bits and return
     */
    SONIC_WRITE(SONIC_ISR,status);
    dev->interrupt = 0;
    return;
}

/*
 * We have a good packet(s), get it/them out of the buffers.
 */
static void
sonic_rx(struct device *dev)
{
    unsigned int base_addr = dev->base_addr;
    struct sonic_local *lp = (struct sonic_local *)dev->priv;
    int entry = lp->cur_rx & SONIC_RDS_MASK;
    int status;

    while(lp->rda[entry].in_use == 0)
    {
	struct sk_buff *skb;
	int pkt_len;
	unsigned char *pkt_ptr;
	
	status = lp->rda[entry].rx_status;
	if (sonic_debug > 3)
	  printk ("status %x, cur_rx %d, cur_rra %d\n",status,lp->cur_rx,lp->cur_rra);
	if (status & SONIC_RCR_PRX) {	    
	    pkt_len = lp->rda[entry].rx_pktlen;
	    pkt_ptr = (char *)KSEG1ADDR(vdma_log2phys((lp->rda[entry].rx_pktptr_h << 16) +
						      lp->rda[entry].rx_pktptr_l));
	    
	    if (sonic_debug > 3)
	      printk ("pktptr %p (rba %p) h:%x l:%x, rra h:%x l:%x bsize h:%x l:%x\n", pkt_ptr,lp->rba,
		      lp->rda[entry].rx_pktptr_h,lp->rda[entry].rx_pktptr_l,
		      lp->rra[lp->cur_rra & 15].rx_bufadr_h,lp->rra[lp->cur_rra & 15].rx_bufadr_l,
		      SONIC_READ(SONIC_RBWC1),SONIC_READ(SONIC_RBWC0));
	
	    /* Malloc up new buffer. */
	    skb = dev_alloc_skb(pkt_len+2);
	    if (skb == NULL) {
		printk("%s: Memory squeeze, dropping packet.\n", dev->name);
		lp->stats.rx_dropped++;
		break;
	    }
	    skb->dev = dev;
	    skb_reserve(skb,2);	/* 16 byte align */
	    skb_put(skb,pkt_len);	/* Make room */
	    eth_copy_and_sum(skb, pkt_ptr, pkt_len, 0);
	    skb->protocol=eth_type_trans(skb,dev);
	    netif_rx(skb);			/* pass the packet to upper layers */
	    lp->stats.rx_packets++;
	    lp->stats.rx_bytes += pkt_len;
	    
	} else {
	    /* This should only happen, if we enable accepting broken packets. */
	    lp->stats.rx_errors++;
	    if (status & SONIC_RCR_FAER) lp->stats.rx_frame_errors++;
	    if (status & SONIC_RCR_CRCR) lp->stats.rx_crc_errors++;
	}
	
	lp->rda[entry].in_use = 1;
	entry = (++lp->cur_rx) & SONIC_RDS_MASK;
	/* now give back the buffer to the receive buffer area */
	if (status & SONIC_RCR_LPKT) {
	    /*
	     * this was the last packet out of the current receice buffer
	     * give the buffer back to the SONIC
	     */
	    SONIC_WRITE(SONIC_RWP,(lp->rra_laddr + (++lp->cur_rra & 15) * sizeof(sonic_rr_t)) & 0xffff);
	}
    }
  
    /* If any worth-while packets have been received, dev_rint()
     has done a mark_bh(NET_BH) for us and will work on them
     when we get to the bottom-half routine. */
    return;
}


/*
 * Get the current statistics.
 * This may be called with the device open or closed.
 */
static struct enet_statistics *
sonic_get_stats(struct device *dev)
{
    struct sonic_local *lp = (struct sonic_local *)dev->priv;
    unsigned int base_addr = dev->base_addr;

    /* read the tally counter from the SONIC and reset them */
    lp->stats.rx_crc_errors += SONIC_READ(SONIC_CRCT);
    SONIC_WRITE(SONIC_CRCT,0xffff);
    lp->stats.rx_frame_errors += SONIC_READ(SONIC_FAET);
    SONIC_WRITE(SONIC_FAET,0xffff);
    lp->stats.rx_missed_errors += SONIC_READ(SONIC_MPT);
    SONIC_WRITE(SONIC_MPT,0xffff);
    
    return &lp->stats;
}


/*
 * Set or clear the multicast filter for this adaptor.
 */
static void
sonic_multicast_list(struct device *dev)
{
    struct sonic_local *lp = (struct sonic_local *)dev->priv;    
    unsigned int base_addr = dev->base_addr;    
    unsigned int rcr;
    struct dev_mc_list *dmi = dev->mc_list;
    unsigned char *addr;
    int i;

    rcr = SONIC_READ(SONIC_RCR) & ~(SONIC_RCR_PRO | SONIC_RCR_AMC);
    rcr |= SONIC_RCR_BRD; /* accept broadcast packets */
    
    if (dev->flags & IFF_PROMISC) {         /* set promiscuous mode */
	rcr |= SONIC_RCR_PRO;
    } else {
	if ((dev->flags & IFF_ALLMULTI) || (dev->mc_count > 15)) {
	    rcr |= SONIC_RCR_AMC;
	} else {
	    if (sonic_debug > 2)
	      printk ("sonic_multicast_list: mc_count %d\n",dev->mc_count);
	    lp->cda.cam_enable = 1; /* always enable our own address */
	    for (i = 1; i <= dev->mc_count; i++) {
		addr = dmi->dmi_addr;
		dmi = dmi->next;
		lp->cda.cam_desc[i].cam_frag2 = addr[1] << 8 | addr[0];
		lp->cda.cam_desc[i].cam_frag1 = addr[3] << 8 | addr[2];
		lp->cda.cam_desc[i].cam_frag0 = addr[5] << 8 | addr[4];
		lp->cda.cam_enable |= (1 << i);
	    }
	    /* number of CAM entries to load */
	    SONIC_WRITE(SONIC_CDC,dev->mc_count+1);
	    /* issue Load CAM command */
	    SONIC_WRITE(SONIC_CDP, lp->cda_laddr & 0xffff);	    
	    SONIC_WRITE(SONIC_CMD,SONIC_CR_LCAM);	    
	}
    }
    
    if (sonic_debug > 2)
      printk("sonic_multicast_list: setting RCR=%x\n",rcr);
    
    SONIC_WRITE(SONIC_RCR,rcr);
}


/*
 * Initialize the SONIC ethernet controller.
 */
static int sonic_init(struct device *dev)
{
    unsigned int base_addr = dev->base_addr;
    unsigned int cmd;
    struct sonic_local *lp = (struct sonic_local *)dev->priv;
    unsigned int rra_start;
    unsigned int rra_end;
    int i;
    
    /*
     * put the Sonic into software-reset mode and
     * disable all interrupts
     */
    SONIC_WRITE(SONIC_ISR,0x7fff);
    SONIC_WRITE(SONIC_IMR,0);
    SONIC_WRITE(SONIC_CMD,SONIC_CR_RST);
  
    /*
     * clear software reset flag, disable receiver, clear and
     * enable interrupts, then completely initialize the SONIC
     */
    SONIC_WRITE(SONIC_CMD,0);
    SONIC_WRITE(SONIC_CMD,SONIC_CR_RXDIS);


    /*
     * initialize the receive resource area
     */
    if (sonic_debug > 2)
      printk ("sonic_init: initialize receive resource area\n");
    
    rra_start = lp->rra_laddr & 0xffff;
    rra_end   = (rra_start + (SONIC_NUM_RRS * sizeof(sonic_rr_t))) & 0xffff;
  
    for (i = 0; i < SONIC_NUM_RRS; i++) {
	lp->rra[i].rx_bufadr_l = (lp->rba_laddr + i * SONIC_RBSIZE) & 0xffff;
	lp->rra[i].rx_bufadr_h = (lp->rba_laddr + i * SONIC_RBSIZE) >> 16;
	lp->rra[i].rx_bufsize_l = SONIC_RBSIZE >> 1;
	lp->rra[i].rx_bufsize_h = 0;
    }

    /* initialize all RRA registers */
    SONIC_WRITE(SONIC_RSA,rra_start);
    SONIC_WRITE(SONIC_REA,rra_end);
    SONIC_WRITE(SONIC_RRP,rra_start);
    SONIC_WRITE(SONIC_RWP,rra_end);
    SONIC_WRITE(SONIC_URRA,lp->rra_laddr >> 16);
    SONIC_WRITE(SONIC_EOBC,(SONIC_RBSIZE-2) >> 1);
    
    lp->cur_rra = SONIC_NUM_RRS - 2;

    /* load the resource pointers */
    if (sonic_debug > 3)
      printk("sonic_init: issueing RRRA command\n");
  
    SONIC_WRITE(SONIC_CMD,SONIC_CR_RRRA);
    i = 0;
    while (i++ < 100) {
	if (SONIC_READ(SONIC_CMD) & SONIC_CR_RRRA)
	  break;
    }
    
    if (sonic_debug > 2)
      printk("sonic_init: status=%x\n",SONIC_READ(SONIC_CMD));
    
    /*
     * Initialize the receive descriptors so that they
     * become a circular linked list, ie. let the last
     * descriptor point to the first again.
     */
    if (sonic_debug > 2)
      printk ("sonic_init: initialize receive descriptors\n");      
    for (i=0; i<SONIC_NUM_RDS; i++) {
	lp->rda[i].rx_status = 0;
	lp->rda[i].rx_pktlen = 0;
	lp->rda[i].rx_pktptr_l = 0;
	lp->rda[i].rx_pktptr_h = 0;
	lp->rda[i].rx_seqno = 0;
	lp->rda[i].in_use = 1;		       	
	lp->rda[i].link = lp->rda_laddr + (i+1) * sizeof (sonic_rd_t);
    }
    /* fix last descriptor */
    lp->rda[SONIC_NUM_RDS-1].link = lp->rda_laddr;
    lp->cur_rx = 0;
    
    SONIC_WRITE(SONIC_URDA,lp->rda_laddr >> 16);
    SONIC_WRITE(SONIC_CRDA,lp->rda_laddr & 0xffff);
    
    /* 
     * initialize transmit descriptors
     */
    if (sonic_debug > 2)
      printk ("sonic_init: initialize transmit descriptors\n");
    for (i = 0; i < SONIC_NUM_TDS; i++) {
	lp->tda[i].tx_status = 0;
	lp->tda[i].tx_config = 0;
	lp->tda[i].tx_pktsize = 0;
	lp->tda[i].tx_frag_count = 0;
	lp->tda[i].link = (lp->tda_laddr + (i+1) * sizeof (sonic_td_t)) | SONIC_END_OF_LINKS;
    }
    lp->tda[SONIC_NUM_TDS-1].link = (lp->tda_laddr & 0xffff) | SONIC_END_OF_LINKS;    

    SONIC_WRITE(SONIC_UTDA,lp->tda_laddr >> 16);
    SONIC_WRITE(SONIC_CTDA,lp->tda_laddr & 0xffff);
    
    /*
     * put our own address to CAM desc[0]
     */
    lp->cda.cam_desc[0].cam_frag2 = dev->dev_addr[1] << 8 | dev->dev_addr[0];
    lp->cda.cam_desc[0].cam_frag1 = dev->dev_addr[3] << 8 | dev->dev_addr[2];
    lp->cda.cam_desc[0].cam_frag0 = dev->dev_addr[5] << 8 | dev->dev_addr[4];
    lp->cda.cam_enable = 1;
    
    for (i=0; i < 16; i++)
      lp->cda.cam_desc[i].cam_entry_pointer = i;

    /*
     * initialize CAM registers
     */
    SONIC_WRITE(SONIC_CDP, lp->cda_laddr & 0xffff);
    SONIC_WRITE(SONIC_CDC,1);
    
    /*
     * load the CAM
     */
    SONIC_WRITE(SONIC_CMD,SONIC_CR_LCAM);
    
    i = 0;
    while (i++ < 100) {
	if (SONIC_READ(SONIC_ISR) & SONIC_INT_LCD)
	  break;
    }
    if (sonic_debug > 2) {
	printk("sonic_init: CMD=%x, ISR=%x\n",
	       SONIC_READ(SONIC_CMD),
	       SONIC_READ(SONIC_ISR));
    }

    /*
     * enable receiver, disable loopback
     * and enable all interrupts
     */
    SONIC_WRITE(SONIC_CMD,SONIC_CR_RXEN | SONIC_CR_STP);
    SONIC_WRITE(SONIC_RCR,SONIC_RCR_DEFAULT);
    SONIC_WRITE(SONIC_TCR,SONIC_TCR_DEFAULT);
    SONIC_WRITE(SONIC_ISR,0x7fff);
    SONIC_WRITE(SONIC_IMR,SONIC_IMR_DEFAULT);

    cmd = SONIC_READ(SONIC_CMD);
    if ((cmd & SONIC_CR_RXEN) == 0 ||
	(cmd & SONIC_CR_STP) == 0)
      printk("sonic_init: failed, status=%x\n",cmd);

    if (sonic_debug > 2)
      printk("sonic_init: new status=%x\n",SONIC_READ(SONIC_CMD));

    return(0);
}


/*
 * Local variables:
 *  compile-command: "mipsel-linux-gcc -D__KERNEL__ -D__mips64 -I/usr/src/linux/net/inet -Wall -Wstrict-prototypes -O2 -mcpu=r4000 -c sonic.c"
 *  version-control: t
 *  kept-new-versions: 5
 *  tab-width: 4
 * End:
 */
