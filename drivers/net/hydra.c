/* Linux/68k Hydra Amiganet board driver v2.1 BETA                          */
/* copyleft by Topi Kanerva (topi@susanna.oulu.fi)                          */
/* also some code & lots of fixes by Timo Rossi (trossi@cc.jyu.fi)          */

/* The code is mostly based on the linux/68k Ariadne driver                 */
/* copyrighted by Geert Uytterhoeven (Geert.Uytterhoeven@cs.kuleuven.ac.be) */
/* and Peter De Schrijver (Peter.DeSchrijver@linux.cc.kuleuven.ac.be)       */

/* This file is subject to the terms and conditions of the GNU General      */
/* Public License.  See the file COPYING in the main directory of the       */
/* Linux distribution for more details.                                     */

/* The Amiganet is a Zorro-II board made by Hydra Systems. It contains a    */
/* NS8390 NIC (network interface controller) clone, 16 or 64K on-board RAM  */
/* and 10BASE-2 (thin coax) and AUI connectors.                             */


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/irq.h>

#include <asm/bootinfo.h>
#include <asm/amigaints.h>
#include <asm/amigahw.h>
#include <asm/zorro.h>

#include "hydra.h"


#define HYDRA_DEBUG
#undef HAVE_MULTICAST

#define HYDRA_VERSION "v2.1 BETA"

#undef HYDRA_DEBUG        /* define this for (lots of) debugging information */

#if 0                         /* currently hardwired to one transmit buffer */
 #define TX_RING_SIZE	5
 #define RX_RING_SIZE	16
#else
 #define TX_RING_SIZE 1
 #define RX_RING_SIZE 8
#endif

#define ETHER_MIN_LEN 64
#define ETHER_MAX_LEN 1518
#define ETHER_ADDR_LEN 6


/*
 *   let's define here nice macros for writing and reading NIC registers
 *
 * the CIA accesses here are uses to make sure the minimum time
 * requirement between NIC chip selects is met.
 */
#define WRITE_REG(reg, val) (ciaa.pra, ((u_char)(*(nicbase+(reg))=val)))
#define READ_REG(reg) (ciaa.pra, ((u_char)(*(nicbase+(reg)))))

/* mask value for the interrupts we use */
#define NIC_INTS (ISR_PRX | ISR_PTX | ISR_RXE | ISR_TXE | ISR_OVW | ISR_CNT)

/* only broadcasts, no promiscuous mode for now */
#define NIC_RCRBITS (0)

/*
 *   Private Device Data
 */
struct hydra_private
    {
    u_char *hydra_base;
    u_char *hydra_nic_base;
    u_short tx_page_start;
    u_short rx_page_start;
    u_short rx_page_stop;
    u_short next_pkt;
    struct enet_statistics stats;
    int key;
    };

static int hydra_open(struct device *dev);
static int hydra_start_xmit(struct sk_buff *skb, struct device *dev);
static void hydra_interrupt(int irq, struct pt_regs *fp, void *data);
static void __inline__ hydra_rx(struct device *dev, struct hydra_private *priv, volatile u_char *nicbase);
static int hydra_close(struct device *dev);
static struct enet_statistics *hydra_get_stats(struct device *dev);
#ifdef HAVE_MULTICAST
static void set_multicast_list(struct device *dev, int num_addrs, void *addrs);
#endif


/* this is now coherent with the C version below, */
/* compile the source with -D__USE_ASM__ if you   */
/* want it - it'll only be some 10% faster though */

#if defined (__GNUC__) && defined (__mc68000__) && defined (USE_ASM)

static __inline__ void *memcpyw(u_short *dest, u_short *src, int len)
    {
    __asm__("   move.l %0,%/a1; move.l %1,%/a0; move.l %2,%/d0 \n\t"
	    "   cmpi.l #2,%/d0 \n\t"
	    "1: bcs.s  2f \n\t"
            "   move.w %/a0@+,%/a1@+ \n\t"
	    "   subq.l #2,%/d0 \n\t"
	    "   bra.s  1b \n\t"
	    "2: cmpi.l #1,%/d0 \n\t"
	    "   bne.s  3f \n\t"
	    "   move.w %/a0@,%/d0 \n\t"
	    "   swap.w %/d0 \n\t"
	    "   move.b %/d0,%/a1@ \n\t"
	    "3: moveq  #0,%/d0 \n\t"
	  :
	  : "g" (dest), "g" (src), "g" (len)
	  : "a1", "a0", "d0");
    return;
}

#else

/* hydra memory can only be read or written as words or longwords.  */
/* that will mean that we'll have to write a special memcpy for it. */
/* this one here relies on the fact that _writes_ to hydra memory   */
/* are guaranteed to be of even length. (reads can be arbitrary)    */

static void memcpyw(u_short *dest, u_short *src, int len)
{
  if(len & 1)
    len++;

  while (len >= 2) {
    *(dest++) = *(src++);
    len -= 2;
  }
    
}

#endif

int hydra_probe(struct device *dev)
    {
    struct hydra_private *priv;
    u_long board;
    int key;
    struct ConfigDev *cd;
    int j;

#ifdef HYDRA_DEBUG
 printk("hydra_probe(%x)\n", dev);
#endif

    if ((key = zorro_find(MANUF_HYDRA_SYSTEMS, PROD_AMIGANET, 0, 0))) {
	cd = zorro_get_board(key);
	if((board = (u_long) cd->cd_BoardAddr))
	    {
	    for(j = 0; j < ETHER_ADDR_LEN; j++)
                dev->dev_addr[j] = *((u_char *)ZTWO_VADDR(board + HYDRA_ADDRPROM + 2*j));
	    
	    printk("%s: hydra at 0x%08x, address %02x:%02x:%02x:%02x:%02x:%02x (hydra.c " HYDRA_VERSION ")\n",
		   dev->name, (int)board, dev->dev_addr[0], dev->dev_addr[1], dev->dev_addr[2],
		   dev->dev_addr[3], dev->dev_addr[4], dev->dev_addr[5]);

	    init_etherdev(dev, 0);
	    
	    dev->priv = kmalloc(sizeof(struct hydra_private), GFP_KERNEL);
	    priv = (struct hydra_private *)dev->priv;
	    memset(priv, 0, sizeof(struct hydra_private));
	    
	    priv->hydra_base = (u_char *) ZTWO_VADDR(board);
	    priv->hydra_nic_base = (u_char *) ZTWO_VADDR(board) + HYDRA_NIC_BASE;
	    priv->key = key;
	    
	    dev->open = &hydra_open;
	    dev->stop = &hydra_close;
	    dev->hard_start_xmit = &hydra_start_xmit;
	    dev->get_stats = &hydra_get_stats;
#ifdef HAVE_MULTICAST
	    dev->set_multicast_list = &hydra_set_multicast_list;
#endif
	    zorro_config_board(key, 0);
	    return(0);
            }
        }
    return(ENODEV);
    }


static int hydra_open(struct device *dev)
  {
    struct hydra_private *priv = (struct hydra_private *)dev->priv;
    volatile u_char *nicbase = priv->hydra_nic_base;
#ifdef HAVE_MULTICAST
    int i;
#endif
    
#ifdef HYDRA_DEBUG
 printk("hydra_open(0x%x)\n", dev);
#endif

    /* first, initialize the private structure */
    priv->tx_page_start = 0;   /* these are 256 byte buffers for NS8390 */
    priv->rx_page_start = 6;
    priv->rx_page_stop  = 62;  /* these values are hard coded for now */

    /* Reset the NS8390 NIC */
    WRITE_REG(NIC_CR, CR_PAGE0 | CR_NODMA | CR_STOP);

    /* be sure that the NIC is in stopped state */
    while(!(READ_REG(NIC_ISR) & ISR_RST));

    /* word transfer, big endian bytes, loopback, FIFO threshold 4 bytes */
    WRITE_REG(NIC_DCR, DCR_WTS | DCR_BOS | DCR_LS | DCR_FT0);

    /* clear remote byte count registers */
    WRITE_REG(NIC_RBCR0, 0);
    WRITE_REG(NIC_RBCR1, 0);

    /* accept packets addressed to this card and also broadcast packets */
    WRITE_REG(NIC_RCR, NIC_RCRBITS);

    /* enable loopback mode 1 */
    WRITE_REG(NIC_TCR, TCR_LB1);

    /* initialize receive buffer ring */
    WRITE_REG(NIC_PSTART, priv->rx_page_start);
    WRITE_REG(NIC_PSTOP, priv->rx_page_stop);
    WRITE_REG(NIC_BNDRY, priv->rx_page_start);

    /* clear interrupts */
    WRITE_REG(NIC_ISR, 0xff);

    /* enable interrupts */
    WRITE_REG(NIC_IMR, NIC_INTS);

    /* set the ethernet hardware address */
    WRITE_REG(NIC_CR, CR_PAGE1 | CR_NODMA | CR_STOP); /* goto page 1 */

    WRITE_REG(NIC_PAR0, dev->dev_addr[0]);
    WRITE_REG(NIC_PAR1, dev->dev_addr[1]);
    WRITE_REG(NIC_PAR2, dev->dev_addr[2]);
    WRITE_REG(NIC_PAR3, dev->dev_addr[3]);
    WRITE_REG(NIC_PAR4, dev->dev_addr[4]);
    WRITE_REG(NIC_PAR5, dev->dev_addr[5]);

#ifdef HAVE_MULTICAST
    /* clear multicast hash table */
    for(i = 0; i < 8; i++)
      WRITE_REG(NIC_MAR0 + 2*i, 0);
#endif

    priv->next_pkt = priv->rx_page_start+1; /* init our s/w variable */
    WRITE_REG(NIC_CURR, priv->next_pkt);    /* set the next buf for current */

    /* goto page 0, start NIC */
    WRITE_REG(NIC_CR, CR_PAGE0 | CR_NODMA | CR_START);

    /* take interface out of loopback */
    WRITE_REG(NIC_TCR, 0);

    dev->tbusy = 0;
    dev->interrupt = 0;
    dev->start = 1;
    
    if(!add_isr(IRQ_AMIGA_PORTS, hydra_interrupt, 0, dev, "Hydra Ethernet"))
      return(-EAGAIN);

    MOD_INC_USE_COUNT;

    return(0);
  }


static int hydra_close(struct device *dev)
{
  struct hydra_private *priv = (struct hydra_private *)dev->priv;
  volatile u_char *nicbase = priv->hydra_nic_base;
  int n = 5000;

  dev->start = 0;
  dev->tbusy = 1;

#ifdef HYDRA_DEBUG
  printk("%s: Shutting down ethercard\n", dev->name);
  printk("%s: %d packets missed\n", dev->name, priv->stats.rx_missed_errors);
#endif

  WRITE_REG(NIC_CR, CR_PAGE0 | CR_NODMA | CR_STOP);

  /* wait for NIC to stop (what a nice timeout..) */
  while(((READ_REG(NIC_ISR) & ISR_RST) == 0) && --n);
    
  remove_isr(IRQ_AMIGA_PORTS, hydra_interrupt, dev);

  MOD_DEC_USE_COUNT;

  return(0);
}


static void hydra_interrupt(int irq, struct pt_regs *fp, void *data)
    {
    volatile u_char *nicbase;
  
    struct device *dev = (struct device *) data;
    struct hydra_private *priv;
    u_short intbits;

    if(dev == NULL)
        {
        printk("hydra_interrupt(): irq for unknown device\n");
        return;
        }

/* this is not likely a problem - i think */
    if(dev->interrupt)
        printk("%s: re-entering the interrupt handler\n", dev->name);

    dev->interrupt = 1;

    priv = (struct hydra_private *) dev->priv;
    nicbase = (u_char *) priv->hydra_nic_base;

    /* select page 0 */
    WRITE_REG(NIC_CR, CR_PAGE0 | CR_START | CR_NODMA);

    intbits = READ_REG(NIC_ISR) & NIC_INTS;
    if(intbits == 0)
        {
        dev->interrupt = 0;
        return;
        }

	/* acknowledge all interrupts, by clearing the interrupt flag */
	WRITE_REG(NIC_ISR, intbits);

	if((intbits & ISR_PTX) && !(intbits & ISR_TXE))
	    {
	    dev->tbusy = 0;
	    mark_bh(NET_BH);
	    }
	
	if((intbits & ISR_PRX) && !(intbits & ISR_RXE))/* packet received OK */
	      hydra_rx(dev, priv, nicbase);

        if(intbits & ISR_TXE)
	    priv->stats.tx_errors++;

        if(intbits & ISR_RXE)
	    priv->stats.rx_errors++;

	if(intbits & ISR_CNT) {
	  /*
	   * read the tally counters and (currently) ignore the values
	   * might be useful because of bugs of some versions of the 8390 NIC
	   */
#ifdef HYDRA_DEBUG
	  printk("hydra_interrupt(): ISR_CNT\n");
#endif
	  (void)READ_REG(NIC_CNTR0);
	  (void)READ_REG(NIC_CNTR1);
	  (void)READ_REG(NIC_CNTR2);
	}
	
	if(intbits & ISR_OVW)
	    {
            #ifdef HYDRA_DEBUG
	    WRITE_REG(NIC_CR, CR_PAGE1 | CR_START | CR_NODMA);
/* another one just too much for me to comprehend - basically this could  */
/* only occur because of invalid access to hydra ram, thus invalidating  */
/* the interrupt bits read - in average usage these do not occur at all */
	    printk("hydra_interrupt(): overwrite warning, NIC_ISR %02x, NIC_CURR %02x\n",
		 intbits, READ_REG(NIC_CURR));
	    WRITE_REG(NIC_CR, CR_PAGE0 | CR_START | CR_NODMA);
            #endif
	    

	    /* overwrite warning occurred, stop NIC & check the BOUNDARY pointer */
	    /* FIXME - real overwrite handling needed !! */

	    printk("hydra_interrupt(): overwrite warning, resetting NIC\n");
	    WRITE_REG(NIC_CR, CR_PAGE0 | CR_NODMA | CR_STOP);
	    while(!(READ_REG(NIC_ISR) & ISR_RST));
	    /* wait for NIC to reset */
	    WRITE_REG(NIC_DCR, DCR_WTS | DCR_BOS | DCR_LS | DCR_FT0);
	    WRITE_REG(NIC_RBCR0, 0);
	    WRITE_REG(NIC_RBCR1, 0);
	    WRITE_REG(NIC_RCR, NIC_RCRBITS);
	    WRITE_REG(NIC_TCR, TCR_LB1);
	    WRITE_REG(NIC_PSTART, priv->rx_page_start);
	    WRITE_REG(NIC_PSTOP, priv->rx_page_stop);
	    WRITE_REG(NIC_BNDRY, priv->rx_page_start);
	    WRITE_REG(NIC_ISR, 0xff);
	    WRITE_REG(NIC_IMR, NIC_INTS);
/* currently this _won't_ reset my hydra, even though it is */
/* basically the same code as in the board init - any ideas? */

	    priv->next_pkt = priv->rx_page_start+1; /* init our s/w variable */
	    WRITE_REG(NIC_CURR, priv->next_pkt);    /* set the next buf for current */
	    
	    WRITE_REG(NIC_CR, CR_PAGE0 | CR_NODMA | CR_START);
	    
	    WRITE_REG(NIC_TCR, 0);
	    }

    dev->interrupt = 0;
    return;
    }


/*
 * packet transmit routine
 */
static int hydra_start_xmit(struct sk_buff *skb, struct device *dev)
    {
    struct hydra_private *priv = (struct hydra_private *)dev->priv;
    volatile u_char *nicbase = priv->hydra_nic_base;
    int len, len1;

	/* Transmitter timeout, serious problems. */

    if(dev->tbusy)
	{
        int tickssofar = jiffies - dev->trans_start;
	if(tickssofar < 20)
	    return(1);
	WRITE_REG(NIC_CR, CR_STOP);
	printk("%s: transmit timed out, status %4.4x, resetting.\n", dev->name, 0);
	priv->stats.tx_errors++;


	dev->tbusy = 0;
	dev->trans_start = jiffies;

	return(0);
	}


    if(skb == NULL)
        {
	dev_tint(dev);
	return(0);
	}

    if((len = skb->len) <= 0)
	return(0);

    /* fill in a tx ring entry */
    
#ifdef HYDRA_DEBUG
 printk("TX pkt type 0x%04x from ", ((u_short *)skb->data)[6]);
	{
		int i;
		u_char *ptr = &((u_char *)skb->data)[6];
		for (i = 0; i < 6; i++)
			printk("%02x", ptr[i]);
	}
	printk(" to ");
	{
		int i;
		u_char *ptr = (u_char *)skb->data;
		for (i = 0; i < 6; i++)
			printk("%02x", ptr[i]);
	}
	printk(" data 0x%08x len %d\n", (int)skb->data, len);
#endif

    /*
     * make sure that the packet size is at least the minimum
     * allowed ethernet packet length.
     * (possibly should also clear the unused space...)
     * note: minimum packet length is 64, including CRC
     */
    len1 = len;
    if(len < (ETHER_MIN_LEN-4))
	len = (ETHER_MIN_LEN-1);

    /* make sure we've got an even number of bytes to copy to hydra's mem */
    if(len & 1) len++;

    if((u_long)(priv->hydra_base + (priv->tx_page_start << 8)) < 0x80000000)
      printk("weirdness: memcpyw(txbuf, skbdata, len): txbuf = 0x%x\n", (u_int)(priv->hydra_base+(priv->tx_page_start<<8)));

    /* copy the packet data to the transmit buffer
       in the ethernet card RAM */
    memcpyw((u_short *)(priv->hydra_base + (priv->tx_page_start << 8)),
	    (u_short *)skb->data, len);
    /* clear the unused space */
/*    for(; len1<len; len1++)
      (u_short)*(priv->hydra_base + (priv->tx_page_start<<8) + len1) = 0;
*/
    dev_kfree_skb(skb, FREE_WRITE);

    priv->stats.tx_packets++;

    cli();
    /* make sure we are on the correct page */
    WRITE_REG(NIC_CR, CR_PAGE0 | CR_NODMA | CR_START);

    /* here we configure the transmit page start register etc */
    /* notice that this code is hardwired to one transmit buffer */
    WRITE_REG(NIC_TPSR, priv->tx_page_start);
    WRITE_REG(NIC_TBCR0, len & 0xff);
    WRITE_REG(NIC_TBCR1, len >> 8);

    /* commit the packet to the wire */
    WRITE_REG(NIC_CR, CR_PAGE0 | CR_START | CR_NODMA | CR_TXP);
    sti();

    dev->trans_start = jiffies;

    return(0);
    }


static void __inline__ hydra_rx(struct device *dev, struct hydra_private *priv, volatile u_char *nicbase)
    {
    volatile u_short *board_ram_ptr;
    struct sk_buff *skb;
    int hdr_next_pkt, pkt_len, len1, boundary;


    /* remove packet(s) from the ring and commit them to TCP layer */
    WRITE_REG(NIC_CR, CR_PAGE1 | CR_NODMA | CR_START); /* page 1 */
    while(priv->next_pkt != READ_REG(NIC_CURR)) /* should read this only once? */
      {
	board_ram_ptr = (u_short *)(priv->hydra_base + (priv->next_pkt << 8));
	
#ifdef HYDRA_DEBUG
	printk("next_pkt = 0x%x, board_ram_ptr = 0x%x\n", priv->next_pkt, board_ram_ptr);
#endif
	
	/* the following must be done with two steps, or
	   GCC optimizes it to a byte access to Hydra memory,
	   which doesn't work... */
	hdr_next_pkt = board_ram_ptr[0];
	hdr_next_pkt >>= 8;
	
	pkt_len = board_ram_ptr[1];
	pkt_len = ((pkt_len >> 8) | ((pkt_len & 0xff) << 8));
	
#ifdef HYDRA_DEBUG
	printk("hydra_interrupt(): hdr_next_pkt = 0x%02x, len = %d\n", hdr_next_pkt, pkt_len);
#endif
	
	if(pkt_len >= ETHER_MIN_LEN && pkt_len <= ETHER_MAX_LEN)
	  {
	    /* note that board_ram_ptr is u_short */
	    /* CRC is not included in the packet length */
	    
	    pkt_len -= 4;
	    skb = dev_alloc_skb(pkt_len+2);
	    if(skb == NULL)
	      {
		printk("%s: memory squeeze, dropping packet.\n", dev->name);
		priv->stats.rx_dropped++;
	      }
	    else
	      {
		skb->dev = dev;
		skb_reserve(skb, 2);
		
		if(hdr_next_pkt < priv->next_pkt && hdr_next_pkt != priv->rx_page_start)
		  {
		    /* here, the packet is wrapped */
		    len1 = ((priv->rx_page_stop - priv->next_pkt)<<8)-4;
		    
		    memcpyw((u_short *)skb_put(skb, len1), (u_short *)(board_ram_ptr+2), len1);
		    memcpyw((u_short *)skb_put(skb, pkt_len-len1),  (u_short *)(priv->hydra_base+(priv->rx_page_start<<8)), pkt_len-len1);
		    
#ifdef HYDRA_DEBUG
		    printk("wrapped packet: %d/%d bytes\n", len1, pkt_len-len1);
#endif
		  }  /* ... here, packet is not wrapped */
		else memcpyw((u_short *) skb_put(skb, pkt_len), (u_short *)(board_ram_ptr+2), pkt_len);
	      }
	    /* if(skb == NULL) ... */
	  }
	else
	  {
	    WRITE_REG(NIC_CR, CR_PAGE1 | CR_START | CR_NODMA);
	    printk("hydra_interrupt(): invalid packet len: %d, NIC_CURR = %02x\n", pkt_len, READ_REG(NIC_CURR));
/*
this is the error i kept getting until i switched to 0.9.10. it still doesn't
mean that the bug would have gone away - so be alarmed. the packet is likely
being fetched from a wrong memory location - but why - dunno
   
note-for-v2.1: not really problem anymore. hasn't been for a long time.
*/
	    
	    WRITE_REG(NIC_CR, CR_PAGE0 | CR_START | CR_NODMA);
	    /* should probably reset the NIC here ?? */
	    
	    hydra_open(dev);  /* FIXME - i shouldn't really be doing this. */
	    return;
	  }
	
	/* now, update the next_pkt pointer */
	if(hdr_next_pkt < priv->rx_page_stop) priv->next_pkt = hdr_next_pkt;
	else printk("hydra_interrupt(): invalid next_pkt pointer %d\n", hdr_next_pkt);
	
	/* update the boundary pointer */
	boundary = priv->next_pkt - 1;
	if(boundary < priv->rx_page_start)
	  boundary = priv->rx_page_stop - 1;
	
	/* set NIC to page 0 to update the NIC_BNDRY register */
	WRITE_REG(NIC_CR, CR_PAGE0 | CR_START | CR_NODMA);
	WRITE_REG(NIC_BNDRY, boundary);
	
	/* select page1 to access the NIC_CURR register */
	WRITE_REG(NIC_CR, CR_PAGE1 | CR_START | CR_NODMA);
	
	
	skb->protocol = eth_type_trans(skb, dev);
	netif_rx(skb);
	priv->stats.rx_packets++;
	
      }
    return;
    }
    

static struct enet_statistics *hydra_get_stats(struct device *dev)
{
	struct hydra_private *priv = (struct hydra_private *)dev->priv;
#if 0
	u_char *board = priv->hydra_base; 

	short saved_addr;
#endif
/* currently does nothing :) i'll finish this later */

	return(&priv->stats);
}

#ifdef HAVE_MULTICAST
static void set_multicast_list(struct device *dev, int num_addrs, void *addrs)
    {
    struct hydra_private *priv = (struct hydra_private *)dev->priv;
    u_char *board = priv->hydra_base;

    /* yes, this code is also waiting for someone to complete.. :) */
    /* (personally i don't care about multicasts at all :) */
    return;
    }
#endif


#ifdef MODULE
static char devicename[9] = { 0, };

static struct device hydra_dev =
{
	devicename,			/* filled in by register_netdev() */
	0, 0, 0, 0,			/* memory */
	0, 0,				/* base, irq */
	0, 0, 0, NULL, hydra_probe,
};

int init_module(void)
{
	int err;

	if ((err = register_netdev(&hydra_dev))) {
		if (err == -EIO)
			printk("No Hydra board found. Module not loaded.\n");
		return(err);
	}
	return(0);
}

void cleanup_module(void)
{
	struct hydra_private *priv = (struct hydra_private *)hydra_dev.priv;

	unregister_netdev(&hydra_dev);
	zorro_unconfig_board(priv->key, 0);
	kfree(priv);
}

#endif /* MODULE */
