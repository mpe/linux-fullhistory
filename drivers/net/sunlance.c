/* $Id: sunlance.c,v 1.81 1998/08/10 09:08:23 jj Exp $
 * lance.c: Linux/Sparc/Lance driver
 *
 *	Written 1995, 1996 by Miguel de Icaza
 * Sources:
 *	The Linux  depca driver
 *	The Linux  lance driver.
 *	The Linux  skeleton driver.
 *	The NetBSD Sparc/Lance driver.
 *	Theo de Raadt (deraadt@openbsd.org)
 *	NCR92C990 Lan Controller manual
 *
 * 1.4:
 *	Added support to run with a ledma on the Sun4m
 *
 * 1.5:
 *	Added multiple card detection.
 *
 *	 4/17/96: Burst sizes and tpe selection on sun4m by Eddie C. Dost
 *		  (ecd@skynet.be)
 *
 *	 5/15/96: auto carrier detection on sun4m by Eddie C. Dost
 *		  (ecd@skynet.be)
 *
 *	 5/17/96: lebuffer on scsi/ether cards now work David S. Miller
 *		  (davem@caip.rutgers.edu)
 *
 *	 5/29/96: override option 'tpe-link-test?', if it is 'false', as
 *		  this disables auto carrier detection on sun4m. Eddie C. Dost
 *		  (ecd@skynet.be)
 *
 * 1.7:
 *	 6/26/96: Bug fix for multiple ledmas, miguel.
 *
 * 1.8:
 *		  Stole multicast code from depca.c, fixed lance_tx.
 *
 * 1.9:
 *	 8/21/96: Fixed the multicast code (Pedro Roque)
 *
 *	 8/28/96: Send fake packet in lance_open() if auto_select is true,
 *		  so we can detect the carrier loss condition in time.
 *		  Eddie C. Dost (ecd@skynet.be)
 *
 *	 9/15/96: Align rx_buf so that eth_copy_and_sum() won't cause an
 *		  MNA trap during chksum_partial_copy(). (ecd@skynet.be)
 *
 *	11/17/96: Handle LE_C0_MERR in lance_interrupt(). (ecd@skynet.be)
 *
 *	12/22/96: Don't loop forever in lance_rx() on incomplete packets.
 *		  This was the sun4c killer. Shit, stupid bug.
 *		  (ecd@skynet.be)
 *
 * 1.10:
 *	 1/26/97: Modularize driver. (ecd@skynet.be)
 *
 * 1.11:
 *	12/27/97: Added sun4d support. (jj@sunsite.mff.cuni.cz)
 */

#undef DEBUG_DRIVER

static char *version =
	"sunlance.c:v1.11 27/Dec/97 Miguel de Icaza (miguel@nuclecu.unam.mx)\n";

static char *lancestr = "LANCE";
static char *lancedma = "LANCE DMA";

#include <linux/config.h>
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/pgtable.h>
#include <linux/errno.h>
#include <asm/byteorder.h>	/* Used by the checksum routines */

/* Used for the temporal inet entries and routing */
#include <linux/socket.h>
#include <linux/route.h>

#include <asm/idprom.h>
#include <asm/sbus.h>
#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/auxio.h>		/* For tpe-link-test? setting */
#include <asm/irq.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include <asm/idprom.h>
#include <asm/machines.h>

/* Define: 2^4 Tx buffers and 2^4 Rx buffers */
#ifndef LANCE_LOG_TX_BUFFERS
#define LANCE_LOG_TX_BUFFERS 4
#define LANCE_LOG_RX_BUFFERS 4
#endif

#define CRC_POLYNOMIAL_BE 0x04c11db7UL  /* Ethernet CRC, big endian */
#define CRC_POLYNOMIAL_LE 0xedb88320UL  /* Ethernet CRC, little endian */

#define LE_CSR0 0
#define LE_CSR1 1
#define LE_CSR2 2
#define LE_CSR3 3

#define LE_MO_PROM      0x8000  /* Enable promiscuous mode */

#define	LE_C0_ERR	0x8000	/* Error: set if BAB, SQE, MISS or ME is set */
#define	LE_C0_BABL	0x4000	/* BAB:  Babble: tx timeout. */
#define	LE_C0_CERR	0x2000	/* SQE:  Signal quality error */
#define	LE_C0_MISS	0x1000	/* MISS: Missed a packet */
#define	LE_C0_MERR	0x0800	/* ME:   Memory error */
#define	LE_C0_RINT	0x0400	/* Received interrupt */
#define	LE_C0_TINT	0x0200	/* Transmitter Interrupt */
#define	LE_C0_IDON	0x0100	/* IFIN: Init finished. */
#define	LE_C0_INTR	0x0080	/* Interrupt or error */
#define	LE_C0_INEA	0x0040	/* Interrupt enable */
#define	LE_C0_RXON	0x0020	/* Receiver on */
#define	LE_C0_TXON	0x0010	/* Transmitter on */
#define	LE_C0_TDMD	0x0008	/* Transmitter demand */
#define	LE_C0_STOP	0x0004	/* Stop the card */
#define	LE_C0_STRT	0x0002	/* Start the card */
#define	LE_C0_INIT	0x0001	/* Init the card */

#define	LE_C3_BSWP	0x4     /* SWAP */
#define	LE_C3_ACON	0x2	/* ALE Control */
#define	LE_C3_BCON	0x1	/* Byte control */

/* Receive message descriptor 1 */
#define LE_R1_OWN       0x80    /* Who owns the entry */
#define LE_R1_ERR       0x40    /* Error: if FRA, OFL, CRC or BUF is set */
#define LE_R1_FRA       0x20    /* FRA: Frame error */
#define LE_R1_OFL       0x10    /* OFL: Frame overflow */
#define LE_R1_CRC       0x08    /* CRC error */
#define LE_R1_BUF       0x04    /* BUF: Buffer error */
#define LE_R1_SOP       0x02    /* Start of packet */
#define LE_R1_EOP       0x01    /* End of packet */
#define LE_R1_POK       0x03    /* Packet is complete: SOP + EOP */

#define LE_T1_OWN       0x80    /* Lance owns the packet */
#define LE_T1_ERR       0x40    /* Error summary */
#define LE_T1_EMORE     0x10    /* Error: more than one retry needed */
#define LE_T1_EONE      0x08    /* Error: one retry needed */
#define LE_T1_EDEF      0x04    /* Error: deferred */
#define LE_T1_SOP       0x02    /* Start of packet */
#define LE_T1_EOP       0x01    /* End of packet */
#define LE_T1_POK	0x03	/* Packet is complete: SOP + EOP */

#define LE_T3_BUF       0x8000  /* Buffer error */
#define LE_T3_UFL       0x4000  /* Error underflow */
#define LE_T3_LCOL      0x1000  /* Error late collision */
#define LE_T3_CLOS      0x0800  /* Error carrier loss */
#define LE_T3_RTY       0x0400  /* Error retry */
#define LE_T3_TDR       0x03ff  /* Time Domain Reflectometry counter */

#define TX_RING_SIZE			(1 << (LANCE_LOG_TX_BUFFERS))
#define TX_RING_MOD_MASK		(TX_RING_SIZE - 1)
#define TX_RING_LEN_BITS		((LANCE_LOG_TX_BUFFERS) << 29)

#define RX_RING_SIZE			(1 << (LANCE_LOG_RX_BUFFERS))
#define RX_RING_MOD_MASK		(RX_RING_SIZE - 1)
#define RX_RING_LEN_BITS		((LANCE_LOG_RX_BUFFERS) << 29)

#define PKT_BUF_SZ		1544
#define RX_BUFF_SIZE            PKT_BUF_SZ
#define TX_BUFF_SIZE            PKT_BUF_SZ

struct lance_rx_desc {
	unsigned short rmd0;        /* low address of packet */
	unsigned char  rmd1_bits;   /* descriptor bits */
	unsigned char  rmd1_hadr;   /* high address of packet */
	short    length;    	    /* This length is 2s complement (negative)!
				     * Buffer length
				     */
	unsigned short mblength;    /* This is the actual number of bytes received */
};

struct lance_tx_desc {
	unsigned short tmd0;        /* low address of packet */
	unsigned char  tmd1_bits;   /* descriptor bits */
	unsigned char  tmd1_hadr;   /* high address of packet */
	short length;          	    /* Length is 2s complement (negative)! */
	unsigned short misc;
};
		
/* The LANCE initialization block, described in databook. */
/* On the Sparc, this block should be on a DMA region     */
struct lance_init_block {
	unsigned short mode;		/* Pre-set mode (reg. 15) */
	unsigned char phys_addr[6];     /* Physical ethernet address */
	unsigned filter[2];		/* Multicast filter. */

	/* Receive and transmit ring base, along with extra bits. */
	unsigned short rx_ptr;		/* receive descriptor addr */
	unsigned short rx_len;		/* receive len and high addr */
	unsigned short tx_ptr;		/* transmit descriptor addr */
	unsigned short tx_len;		/* transmit len and high addr */
    
	/* The Tx and Rx ring entries must aligned on 8-byte boundaries. */
	struct lance_rx_desc brx_ring[RX_RING_SIZE];
	struct lance_tx_desc btx_ring[TX_RING_SIZE];
    
	char   tx_buf [TX_RING_SIZE][TX_BUFF_SIZE];
	char   pad[2];			/* align rx_buf for copy_and_sum(). */
	char   rx_buf [RX_RING_SIZE][RX_BUFF_SIZE];
};

#define libdesc_offset(rt, elem) \
((__u32)(((unsigned long)(&(((struct lance_init_block *)0)->rt[elem])))))

#define libbuff_offset(rt, elem) \
((__u32)(((unsigned long)(&(((struct lance_init_block *)0)->rt[elem][0])))))

struct lance_private {
	char *name;
	volatile struct lance_regs *ll;
	volatile struct lance_init_block *init_block;
	__u32 init_block_dvma;
    
	int rx_new, tx_new;
	int rx_old, tx_old;
    
	struct net_device_stats	stats;
	struct Linux_SBus_DMA *ledma; /* If set this points to ledma    */
				      /* and arch = sun4m		*/

	int tpe;		      /* cable-selection is TPE		*/
	int auto_select;	      /* cable-selection by carrier	*/
	int burst_sizes;	      /* ledma SBus burst sizes		*/

	unsigned short busmaster_regval;
	unsigned short pio_buffer;

	struct device		 *dev;		  /* Backpointer	*/
	struct lance_private	 *next_module;
	struct linux_sbus        *sbus;
};

#define TX_BUFFS_AVAIL ((lp->tx_old<=lp->tx_new)?\
			lp->tx_old+TX_RING_MOD_MASK-lp->tx_new:\
			lp->tx_old - lp->tx_new-1)

/* On the sparc, the lance control ports are memory mapped */
struct lance_regs {
	unsigned short rdp;			/* register data port */
	unsigned short rap;			/* register address port */
};

int sparc_lance_debug = 2;

/* The Lance uses 24 bit addresses */
/* On the Sun4c the DVMA will provide the remaining bytes for us */
/* On the Sun4m we have to instruct the ledma to provide them    */
/* Even worse, on scsi/ether SBUS cards, the init block and the
 * transmit/receive buffers are addresses as offsets from absolute
 * zero on the lebuffer PIO area. -davem
 */

#define LANCE_ADDR(x) ((long)(x) & ~0xff000000)

#ifdef MODULE
static struct lance_private *root_lance_dev = NULL;
#endif

/* Load the CSR registers */
static void load_csrs (struct lance_private *lp)
{
	volatile struct lance_regs *ll = lp->ll;
	__u32 ib_dvma = lp->init_block_dvma;
	int leptr;

	/* This is right now because when we are using a PIO buffered
	 * init block, init_block_dvma is set to zero. -DaveM
	 */
	leptr = LANCE_ADDR (ib_dvma);

	ll->rap = LE_CSR1;
	ll->rdp = (leptr & 0xFFFF);
	ll->rap = LE_CSR2;
	ll->rdp = leptr >> 16;
	ll->rap = LE_CSR3;
	ll->rdp = lp->busmaster_regval;

	/* Point back to csr0 */
	ll->rap = LE_CSR0;
}

#define ZERO 0

/* Setup the Lance Rx and Tx rings */
/* Sets dev->tbusy */
static void lance_init_ring (struct device *dev)
{
	struct lance_private *lp = (struct lance_private *) dev->priv;
	volatile struct lance_init_block *ib = lp->init_block;
	__u32 ib_dvma = lp->init_block_dvma;
	__u32 aib; /* for LANCE_ADDR computations */
	int leptr;
	int i;
    
	/* This is right now because when we are using a PIO buffered
	 * init block, init_block_dvma is set to zero. -DaveM
	 */
	aib = ib_dvma;

	/* Lock out other processes while setting up hardware */
	dev->tbusy = 1;
	lp->rx_new = lp->tx_new = 0;
	lp->rx_old = lp->tx_old = 0;

	ib->mode = 0;

	/* Copy the ethernet address to the lance init block
	 * Note that on the sparc you need to swap the ethernet address.
	 * Note also we want the CPU ptr of the init_block here.
	 */
	ib->phys_addr [0] = dev->dev_addr [1];
	ib->phys_addr [1] = dev->dev_addr [0];
	ib->phys_addr [2] = dev->dev_addr [3];
	ib->phys_addr [3] = dev->dev_addr [2];
	ib->phys_addr [4] = dev->dev_addr [5];
	ib->phys_addr [5] = dev->dev_addr [4];

	if (ZERO)
		printk ("TX rings:\n");
    
	/* Setup the Tx ring entries */
	for (i = 0; i <= TX_RING_SIZE; i++) {
		leptr = LANCE_ADDR(aib + libbuff_offset(tx_buf, i));
		ib->btx_ring [i].tmd0      = leptr;
		ib->btx_ring [i].tmd1_hadr = leptr >> 16;
		ib->btx_ring [i].tmd1_bits = 0;
		ib->btx_ring [i].length    = 0xf000; /* The ones required by tmd2 */
		ib->btx_ring [i].misc      = 0;
		if (i < 3)
			if (ZERO) printk ("%d: 0x%8.8x\n", i, leptr);
	}

	/* Setup the Rx ring entries */
	if (ZERO)
		printk ("RX rings:\n");
	for (i = 0; i < RX_RING_SIZE; i++) {
		leptr = LANCE_ADDR(aib + libbuff_offset(rx_buf, i));

		ib->brx_ring [i].rmd0      = leptr;
		ib->brx_ring [i].rmd1_hadr = leptr >> 16;
		ib->brx_ring [i].rmd1_bits = LE_R1_OWN;
		ib->brx_ring [i].length    = -RX_BUFF_SIZE | 0xf000;
		ib->brx_ring [i].mblength  = 0;
		if (i < 3 && ZERO)
			printk ("%d: 0x%8.8x\n", i, leptr);
	}

	/* Setup the initialization block */
    
	/* Setup rx descriptor pointer */
	leptr = LANCE_ADDR(aib + libdesc_offset(brx_ring, 0));
	ib->rx_len = (LANCE_LOG_RX_BUFFERS << 13) | (leptr >> 16);
	ib->rx_ptr = leptr;
	if (ZERO)
		printk ("RX ptr: %8.8x\n", leptr);
    
	/* Setup tx descriptor pointer */
	leptr = LANCE_ADDR(aib + libdesc_offset(btx_ring, 0));
	ib->tx_len = (LANCE_LOG_TX_BUFFERS << 13) | (leptr >> 16);
	ib->tx_ptr = leptr;
	if (ZERO)
		printk ("TX ptr: %8.8x\n", leptr);

	/* Clear the multicast filter */
	ib->filter [0] = 0;
	ib->filter [1] = 0;
}

static int init_restart_lance (struct lance_private *lp)
{
	volatile struct lance_regs *ll = lp->ll;
	int i;

	if (lp->ledma) {
		struct sparc_dma_registers *dregs = lp->ledma->regs;
		unsigned long creg;

		if (!(dregs->cond_reg & DMA_HNDL_ERROR)) {
			/* E-Cache draining */
			while (dregs->cond_reg & DMA_FIFO_ISDRAIN)
				barrier();
		}

		creg = dregs->cond_reg;
		if (lp->burst_sizes & DMA_BURST32)
			creg |= DMA_E_BURST8;
		else
			creg &= ~DMA_E_BURST8;

		creg |= (DMA_DSBL_RD_DRN | DMA_DSBL_WR_INV | DMA_FIFO_INV);

		if (lp->tpe)
			creg |= DMA_EN_ENETAUI;
		else
			creg &= ~DMA_EN_ENETAUI;
		udelay(20);
		dregs->cond_reg = creg;
		udelay(200);
	}

	ll->rap = LE_CSR0;
	ll->rdp = LE_C0_INIT;

	/* Wait for the lance to complete initialization */
	for (i = 0; (i < 100) && !(ll->rdp & (LE_C0_ERR | LE_C0_IDON)); i++)
		barrier();
	if ((i == 100) || (ll->rdp & LE_C0_ERR)) {
		printk ("LANCE unopened after %d ticks, csr0=%4.4x.\n", i, ll->rdp);
		if (lp->ledma)
			printk ("dcsr=%8.8x\n",
				(unsigned int) lp->ledma->regs->cond_reg);
		return -1;
	}

	/* Clear IDON by writing a "1", enable interrupts and start lance */
	ll->rdp = LE_C0_IDON;
	ll->rdp = LE_C0_INEA | LE_C0_STRT;

	if (lp->ledma)
		lp->ledma->regs->cond_reg |= DMA_INT_ENAB;

	return 0;
}

static int lance_rx (struct device *dev)
{
	struct lance_private *lp = (struct lance_private *) dev->priv;
	volatile struct lance_init_block *ib = lp->init_block;
	volatile struct lance_rx_desc *rd;
	unsigned char bits;
	int len;
	struct sk_buff *skb;

#ifdef TEST_HITS
	printk ("[");
	for (i = 0; i < RX_RING_SIZE; i++) {
		if (i == lp->rx_new)
			printk ("%s",
				ib->brx_ring [i].rmd1_bits & LE_R1_OWN ? "_" : "X");
		else
			printk ("%s",
				ib->brx_ring [i].rmd1_bits & LE_R1_OWN ? "." : "1");
	}
	printk ("]");
#endif
    
	for (rd = &ib->brx_ring [lp->rx_new];
	     !((bits = rd->rmd1_bits) & LE_R1_OWN);
	     rd = &ib->brx_ring [lp->rx_new]) {

		/* We got an incomplete frame? */
		if ((bits & LE_R1_POK) != LE_R1_POK) {
			lp->stats.rx_over_errors++;
			lp->stats.rx_errors++;
		} else if (bits & LE_R1_ERR) {
			/* Count only the end frame as a rx error,
			 * not the beginning
			 */
			if (bits & LE_R1_BUF) lp->stats.rx_fifo_errors++;
			if (bits & LE_R1_CRC) lp->stats.rx_crc_errors++;
			if (bits & LE_R1_OFL) lp->stats.rx_over_errors++;
			if (bits & LE_R1_FRA) lp->stats.rx_frame_errors++;
			if (bits & LE_R1_EOP) lp->stats.rx_errors++;
		} else {
			len = (rd->mblength & 0xfff) - 4;
			skb = dev_alloc_skb (len+2);

			if (skb == 0) {
				printk ("%s: Memory squeeze, deferring packet.\n",
					dev->name);
				lp->stats.rx_dropped++;
				rd->mblength = 0;
				rd->rmd1_bits = LE_R1_OWN;
				lp->rx_new = (lp->rx_new + 1) & RX_RING_MOD_MASK;
				return 0;
			}
	    
			lp->stats.rx_bytes += len;

			skb->dev = dev;
			skb_reserve (skb, 2);		/* 16 byte align */
			skb_put (skb, len);		/* make room */
			eth_copy_and_sum(skb,
					 (unsigned char *)&(ib->rx_buf [lp->rx_new][0]),
					 len, 0);
			skb->protocol = eth_type_trans (skb, dev);
			netif_rx (skb);
			lp->stats.rx_packets++;
		}

		/* Return the packet to the pool */
		rd->mblength = 0;
		rd->rmd1_bits = LE_R1_OWN;
		lp->rx_new = (lp->rx_new + 1) & RX_RING_MOD_MASK;
	}
	return 0;
}

static int lance_tx (struct device *dev)
{
	struct lance_private *lp = (struct lance_private *) dev->priv;
	volatile struct lance_init_block *ib = lp->init_block;
	volatile struct lance_regs *ll = lp->ll;
	volatile struct lance_tx_desc *td;
	int i, j;
	int status;

	j = lp->tx_old;
	for (i = j; i != lp->tx_new; i = j) {
		td = &ib->btx_ring [i];

		/* If we hit a packet not owned by us, stop */
		if (td->tmd1_bits & LE_T1_OWN)
			break;
		
		if (td->tmd1_bits & LE_T1_ERR) {
			status = td->misc;
	    
			lp->stats.tx_errors++;
			if (status & LE_T3_RTY)  lp->stats.tx_aborted_errors++;
			if (status & LE_T3_LCOL) lp->stats.tx_window_errors++;

			if (status & LE_T3_CLOS) {
				lp->stats.tx_carrier_errors++;
				if (lp->auto_select) {
					lp->tpe = 1 - lp->tpe;
					printk("%s: Carrier Lost, trying %s\n",
					       dev->name, lp->tpe?"TPE":"AUI");
					/* Stop the lance */
					ll->rap = LE_CSR0;
					ll->rdp = LE_C0_STOP;
					lance_init_ring (dev);
					load_csrs (lp);
					init_restart_lance (lp);
					return 0;
				}
			}

			/* Buffer errors and underflows turn off the
			 * transmitter, restart the adapter.
			 */
			if (status & (LE_T3_BUF|LE_T3_UFL)) {
				lp->stats.tx_fifo_errors++;

				printk ("%s: Tx: ERR_BUF|ERR_UFL, restarting\n",
					dev->name);
				/* Stop the lance */
				ll->rap = LE_CSR0;
				ll->rdp = LE_C0_STOP;
				lance_init_ring (dev);
				load_csrs (lp);
				init_restart_lance (lp);
				return 0;
			}
		} else if ((td->tmd1_bits & LE_T1_POK) == LE_T1_POK) {
			/*
			 * So we don't count the packet more than once.
			 */
			td->tmd1_bits &= ~(LE_T1_POK);

			/* One collision before packet was sent. */
			if (td->tmd1_bits & LE_T1_EONE)
				lp->stats.collisions++;

			/* More than one collision, be optimistic. */
			if (td->tmd1_bits & LE_T1_EMORE)
				lp->stats.collisions += 2;

			lp->stats.tx_packets++;
		}
	
		j = (j + 1) & TX_RING_MOD_MASK;
	}
	lp->tx_old = j;
	return 0;
}

static void lance_interrupt (int irq, void *dev_id, struct pt_regs *regs)
{
	struct device *dev = (struct device *)dev_id;
	struct lance_private *lp = (struct lance_private *)dev->priv;
	volatile struct lance_regs *ll = lp->ll;
	int csr0;
    
	if (dev->interrupt)
		printk ("%s: again", dev->name);
    
	dev->interrupt = 1;

	ll->rap = LE_CSR0;
	csr0 = ll->rdp;

	/* Acknowledge all the interrupt sources ASAP */
	ll->rdp = csr0 & (LE_C0_INTR | LE_C0_TINT | LE_C0_RINT);
    
	if ((csr0 & LE_C0_ERR)) {
		/* Clear the error condition */
		ll->rdp = LE_C0_BABL | LE_C0_ERR | LE_C0_MISS |
			  LE_C0_CERR | LE_C0_MERR;
	}
    
	if (csr0 & LE_C0_RINT)
		lance_rx (dev);
    
	if (csr0 & LE_C0_TINT)
		lance_tx (dev);
    
	if ((TX_BUFFS_AVAIL >= 0) && dev->tbusy) {
		dev->tbusy = 0;
		mark_bh (NET_BH);
	}

	if (csr0 & LE_C0_BABL)
		lp->stats.tx_errors++;

	if (csr0 & LE_C0_MISS)
		lp->stats.rx_errors++;

	if (csr0 & LE_C0_MERR) {
		struct sparc_dma_registers *dregs = lp->ledma->regs;
		unsigned long tst = (unsigned long)dregs->st_addr;

		printk ("%s: Memory error, status %04x, addr %06lx\n",
			dev->name, csr0, tst & 0xffffff);

		ll->rdp = LE_C0_STOP;

		if (lp->ledma)
			lp->ledma->regs->cond_reg |= DMA_FIFO_INV;

		lance_init_ring (dev);
		load_csrs (lp);
		init_restart_lance (lp);
		dev->tbusy = 0;
	}

	ll->rdp = LE_C0_INEA;
	dev->interrupt = 0;
}

struct device *last_dev = 0;

static int lance_open (struct device *dev)
{
	struct lance_private *lp = (struct lance_private *)dev->priv;
	volatile struct lance_regs *ll = lp->ll;
	int status = 0;

	last_dev = dev;

	if (request_irq (dev->irq, &lance_interrupt, SA_SHIRQ,
			 lancestr, (void *) dev)) {
		printk ("Lance: Can't get irq %s\n", __irq_itoa(dev->irq));
		return -EAGAIN;
	}

	/* Stop the Lance */
	ll->rap = LE_CSR0;
	ll->rdp = LE_C0_STOP;

	/* On the 4m, setup the ledma to provide the upper bits for buffers */
	if (lp->ledma)
		lp->ledma->regs->dma_test = ((__u32) lp->init_block_dvma) & 0xff000000;

	lance_init_ring (dev);
	load_csrs (lp);

	dev->tbusy = 0;
	dev->interrupt = 0;
	dev->start = 1;

	status = init_restart_lance (lp);
#if 0
	/* To emulate SunOS, we add a route to the local network */
	rt_add (RTF_UP,
		dev->pa_addr & ip_get_mask (dev->pa_addr),
		ip_get_mask (dev->pa_addr),
		0, dev, dev->mtu, 0, 0);
#endif
	if (!status && lp->auto_select) {
		/*
		 * Build a fake network packet and send it to ourselfs.
		 */
		volatile struct lance_init_block *ib = lp->init_block;
		volatile unsigned long flush;
		unsigned char packet[ETH_ZLEN];
		struct ethhdr *eth = (struct ethhdr *)packet;
		int i, entry;

		memset(packet, 0, ETH_ZLEN);
		for (i = 0; i < 6; i++) {
			eth->h_dest[i] = dev->dev_addr[i];
			eth->h_source[i] = dev->dev_addr[i];
		}

		entry = lp->tx_new & TX_RING_MOD_MASK;
		ib->btx_ring[entry].length = (-ETH_ZLEN) | 0xf000;
		ib->btx_ring[entry].misc = 0;

		memcpy((char *)&ib->tx_buf[entry][0], packet, ETH_ZLEN);
		ib->btx_ring[entry].tmd1_bits = (LE_T1_POK|LE_T1_OWN);
		lp->tx_new = (lp->tx_new + 1) & TX_RING_MOD_MASK;

		ll->rdp = LE_C0_INEA | LE_C0_TDMD;
		flush = ll->rdp;
	}

	if (!status)
		MOD_INC_USE_COUNT;

	return status;
}

static int lance_close (struct device *dev)
{
	struct lance_private *lp = (struct lance_private *) dev->priv;
	volatile struct lance_regs *ll = lp->ll;

	dev->start = 0;
	dev->tbusy = 1;

	/* Stop the card */
	ll->rap = LE_CSR0;
	ll->rdp = LE_C0_STOP;

	free_irq (dev->irq, (void *) dev);
	MOD_DEC_USE_COUNT;
	return 0;
}

static inline int lance_reset (struct device *dev)
{
	struct lance_private *lp = (struct lance_private *)dev->priv;
	volatile struct lance_regs *ll = lp->ll;
	int status;
    
	/* Stop the lance */
	ll->rap = LE_CSR0;
	ll->rdp = LE_C0_STOP;

	/* On the 4m, reset the dma too */
	if (lp->ledma) {
		printk ("resetting ledma\n");
		lp->ledma->regs->cond_reg |= DMA_RST_ENET;
		udelay (200);
		lp->ledma->regs->cond_reg &= ~DMA_RST_ENET;
		lp->ledma->regs->dma_test = ((__u32) lp->init_block_dvma) & 0xff000000;
	}
	lance_init_ring (dev);
	load_csrs (lp);
	dev->trans_start = jiffies;
	dev->interrupt = 0;
	dev->start = 1;
	dev->tbusy = 0;
	status = init_restart_lance (lp);
#ifdef DEBUG_DRIVER
	printk ("Lance restart=%d\n", status);
#endif
	return status;
}

static int lance_start_xmit (struct sk_buff *skb, struct device *dev)
{
	struct lance_private *lp = (struct lance_private *)dev->priv;
	volatile struct lance_regs *ll = lp->ll;
	volatile struct lance_init_block *ib = lp->init_block;
	volatile unsigned long flush;
	unsigned long flags;
	int entry, skblen, len;
	int status = 0;
	static int outs;

	/* Transmitter timeout, serious problems */
	if (dev->tbusy) {
		int tickssofar = jiffies - dev->trans_start;
	    
		if (tickssofar < 100) {
			status = -1;
		} else {
			printk ("%s: transmit timed out, status %04x, reset\n",
				dev->name, ll->rdp);
			lance_reset (dev);
		}
		return status;
	}

	/* Block a timer-based transmit from overlapping. */
	if (test_and_set_bit (0, (void *) &dev->tbusy) != 0) {
		printk ("Transmitter access conflict.\n");
		return -1;
	}

	skblen = skb->len;

	save_and_cli(flags);

	if (!TX_BUFFS_AVAIL) {
		restore_flags(flags);
		return -1;
	}

	len = (skblen <= ETH_ZLEN) ? ETH_ZLEN : skblen;
	
	lp->stats.tx_bytes += len;
	
	entry = lp->tx_new & TX_RING_MOD_MASK;
	ib->btx_ring [entry].length = (-len) | 0xf000;
	ib->btx_ring [entry].misc = 0;
    
	memcpy ((char *)&ib->tx_buf [entry][0], skb->data, skblen);

	/* Clear the slack of the packet, do I need this? */
	/* For a firewall its a good idea - AC */
	if (len != skblen)
		memset ((char *) &ib->tx_buf [entry][skblen], 0, len - skblen);
    
	/* Now, give the packet to the lance */
	ib->btx_ring [entry].tmd1_bits = (LE_T1_POK|LE_T1_OWN);
	lp->tx_new = (lp->tx_new+1) & TX_RING_MOD_MASK;

	outs++;
	/* Kick the lance: transmit now */
	ll->rdp = LE_C0_INEA | LE_C0_TDMD;
	dev->trans_start = jiffies;
	dev_kfree_skb (skb);
    
	if (TX_BUFFS_AVAIL)
		dev->tbusy = 0;

	/* Read back CSR to invalidate the E-Cache.
	 * This is needed, because DMA_DSBL_WR_INV is set. */
	if (lp->ledma)
		flush = ll->rdp;

	restore_flags(flags);
	return status;
}

static struct net_device_stats *lance_get_stats (struct device *dev)
{
	struct lance_private *lp = (struct lance_private *) dev->priv;

	return &lp->stats;
}

/* taken from the depca driver */
static void lance_load_multicast (struct device *dev)
{
	struct lance_private *lp = (struct lance_private *) dev->priv;
	volatile struct lance_init_block *ib = lp->init_block;
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

static void lance_set_multicast (struct device *dev)
{
	struct lance_private *lp = (struct lance_private *) dev->priv;
	volatile struct lance_init_block *ib = lp->init_block;
	volatile struct lance_regs *ll = lp->ll;

	while (dev->tbusy)
		schedule();
	set_bit (0, (void *) &dev->tbusy);

	while (lp->tx_old != lp->tx_new)
		schedule();

	ll->rap = LE_CSR0;
	ll->rdp = LE_C0_STOP;
	lance_init_ring (dev);

	if (dev->flags & IFF_PROMISC) {
		ib->mode |= LE_MO_PROM;
	} else {
		ib->mode &= ~LE_MO_PROM;
		lance_load_multicast (dev);
	}
	load_csrs (lp);
	init_restart_lance (lp);
	dev->tbusy = 0;
}

__initfunc(static int 
sparc_lance_init (struct device *dev, struct linux_sbus_device *sdev,
		  struct Linux_SBus_DMA *ledma,
		  struct linux_sbus_device *lebuffer))
{
	static unsigned version_printed = 0;
	int    i;
	struct lance_private *lp;
	volatile struct lance_regs *ll;

	if (dev == NULL) {
		dev = init_etherdev (0, sizeof (struct lance_private) + 8);
	} else {
		dev->priv = kmalloc (sizeof (struct lance_private) + 8,
				     GFP_KERNEL);
		if (dev->priv == NULL)
			return -ENOMEM;
		memset(dev->priv, 0, sizeof (struct lance_private) + 8);
	}
	if (sparc_lance_debug && version_printed++ == 0)
		printk (version);

	printk ("%s: LANCE ", dev->name);
	/* Fill the dev fields */
	dev->base_addr = (long) sdev;

	/* Copy the IDPROM ethernet address to the device structure, later we
	 * will copy the address in the device structure to the lance
	 * initialization block.
	 */
	for (i = 0; i < 6; i++)
		printk ("%2.2x%c", dev->dev_addr[i] = idprom->id_ethaddr[i],
			i == 5 ? ' ': ':');
	printk("\n");

	/* Get the IO region */
	prom_apply_sbus_ranges (sdev->my_bus, &sdev->reg_addrs [0],
				sdev->num_registers, sdev);
	ll = sparc_alloc_io (sdev->reg_addrs [0].phys_addr, 0,
			     sizeof (struct lance_regs), lancestr,
			     sdev->reg_addrs[0].which_io, 0x0);

	/* Make certain the data structures used by the LANCE are aligned. */
	dev->priv = (void *)(((unsigned long)dev->priv + 7) & ~7);
	lp = (struct lance_private *) dev->priv;
	lp->sbus = sdev->my_bus;
	if (lebuffer){
		prom_apply_sbus_ranges (lebuffer->my_bus,
					&lebuffer->reg_addrs [0],
					lebuffer->num_registers,
					lebuffer);

		lp->init_block = (void *)
			sparc_alloc_io (lebuffer->reg_addrs [0].phys_addr, 0,
				sizeof (struct lance_init_block), "lebuffer", 
				lebuffer->reg_addrs [0].which_io, 0);
		lp->init_block_dvma = 0;

		lp->pio_buffer = 1;
	} else {
		lp->init_block = (void *)
			sparc_dvma_malloc (sizeof (struct lance_init_block),
					   lancedma, &lp->init_block_dvma);
		lp->pio_buffer = 0;
	}
	lp->busmaster_regval = prom_getintdefault(sdev->prom_node,
						  "busmaster-regval",
						  (LE_C3_BSWP | LE_C3_ACON |
						   LE_C3_BCON));

	lp->ll = ll;
	lp->name = lancestr;
	lp->ledma = ledma;

	lp->burst_sizes = 0;
	if (lp->ledma) {
		char prop[6];
		unsigned int sbmask;

		/* Find burst-size property for ledma */
		lp->burst_sizes = prom_getintdefault(ledma->SBus_dev->prom_node,
						     "burst-sizes", 0);

		/* ledma may be capable of fast bursts, but sbus may not. */
		sbmask = prom_getintdefault(ledma->SBus_dev->my_bus->prom_node,
					    "burst-sizes", DMA_BURSTBITS);
		lp->burst_sizes &= sbmask;

		/* Get the cable-selection property */
		memset(prop, 0, sizeof(prop));
		prom_getstring(ledma->SBus_dev->prom_node, "cable-selection",
			       prop, sizeof(prop));
		if (prop[0] == 0) {
			int topnd, nd;

			printk("%s: using auto-carrier-detection.\n",
			       dev->name);

			/* Is this found at /options .attributes in all
			 * Prom versions? XXX
			 */
			topnd = prom_getchild(prom_root_node);

			nd = prom_searchsiblings(topnd, "options");
			if (!nd)
				goto no_link_test;

			if (!prom_node_has_property(nd, "tpe-link-test?"))
				goto no_link_test;

			memset(prop, 0, sizeof(prop));
			prom_getstring(nd, "tpe-link-test?", prop,
				       sizeof(prop));

			if (strcmp(prop, "true")) {
				printk("%s: warning: overriding option "
				       "'tpe-link-test?'\n", dev->name);
				printk("%s: warning: mail any problems "
				       "to ecd@skynet.be\n", dev->name);
				set_auxio(AUXIO_LINK_TEST, 0);
			}
no_link_test:
			lp->auto_select = 1;
			lp->tpe = 0;
		} else if (!strcmp(prop, "aui")) {
			lp->auto_select = 0;
			lp->tpe = 0;
		} else {
			lp->auto_select = 0;
			lp->tpe = 1;
		}

		/* Reset ledma */
		lp->ledma->regs->cond_reg |= DMA_RST_ENET;
		udelay (200);
		lp->ledma->regs->cond_reg &= ~DMA_RST_ENET;
	}

	/* This should never happen. */
	if ((unsigned long)(lp->init_block->brx_ring) & 0x07) {
		printk("%s: ERROR: Rx and Tx rings not on even boundary.\n",
		       dev->name);
		return ENODEV;
	}

	lp->dev = dev;
	dev->open = &lance_open;
	dev->stop = &lance_close;
	dev->hard_start_xmit = &lance_start_xmit;
	dev->get_stats = &lance_get_stats;
	dev->set_multicast_list = &lance_set_multicast;

	dev->irq = sdev->irqs[0];

	dev->dma = 0;
	ether_setup (dev);

#ifdef MODULE
	dev->ifindex = dev_new_index();
	lp->next_module = root_lance_dev;
	root_lance_dev = lp;
#endif
	return 0;
}

/* On 4m, find the associated dma for the lance chip */
static inline struct Linux_SBus_DMA *
find_ledma (struct linux_sbus_device *dev)
{
	struct Linux_SBus_DMA *p;

	for_each_dvma(p)
		if (p->SBus_dev == dev)
			return p;
	return 0;
}

#ifdef CONFIG_SUN4

#include <asm/sun4paddr.h>

/* Find all the lance cards on the system and initialize them */
__initfunc(int sparc_lance_probe (struct device *dev))
{
	static struct linux_sbus_device sdev;
	static int called = 0;

	if(called)
		return ENODEV;
	called++;

	if ((idprom->id_machtype == (SM_SUN4|SM_4_330)) ||
	    (idprom->id_machtype == (SM_SUN4|SM_4_470))) {
		memset (&sdev, 0, sizeof(sdev));
		sdev.reg_addrs[0].phys_addr = sun4_eth_physaddr;
		sdev.irqs[0] = 6;
		return sparc_lance_init(dev, &sdev, 0, 0);
	}
	return ENODEV;
}

#else /* !CONFIG_SUN4 */

/* Find all the lance cards on the system and initialize them */
__initfunc(int sparc_lance_probe (struct device *dev))
{
	struct linux_sbus *bus;
	struct linux_sbus_device *sdev = 0;
	struct Linux_SBus_DMA *ledma = 0;
	static int called = 0;
	int cards = 0, v;

	if(called)
		return ENODEV;
	called++;

	for_each_sbus (bus) {
		for_each_sbusdev (sdev, bus) {
			if (cards) dev = NULL;
			if (strcmp (sdev->prom_name, "le") == 0) {
				cards++;
				if ((v = sparc_lance_init(dev, sdev, 0, 0)))
					return v;
				continue;
			}
			if (strcmp (sdev->prom_name, "ledma") == 0) {
				cards++;
				ledma = find_ledma (sdev);
				if ((v = sparc_lance_init(dev, sdev->child,
							  ledma, 0)))
					return v;
				continue;
			}
			if (strcmp (sdev->prom_name, "lebuffer") == 0){
				cards++;
				if ((v = sparc_lance_init(dev, sdev->child,
							  0, sdev)))
					return v;
				continue;
			}
		} /* for each sbusdev */
	} /* for each sbus */
	if (!cards)
		return ENODEV;
	return 0;
}
#endif /* !CONFIG_SUN4 */

#ifdef MODULE

int
init_module(void)
{
	root_lance_dev = NULL;
	return sparc_lance_probe(NULL);
}

void
cleanup_module(void)
{
	struct lance_private *lp;

	while (root_lance_dev) {
		lp = root_lance_dev->next_module;

		unregister_netdev(root_lance_dev->dev);
		kfree(root_lance_dev->dev);
		root_lance_dev = lp;
	}
}

#endif /* MODULE */
