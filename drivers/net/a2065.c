/*
 * Amiga Linux/68k A2065 Ethernet Driver
 *
 * (C) Copyright 1995 by Geert Uytterhoeven
 *                      (Geert.Uytterhoeven@cs.kuleuven.ac.be)
 *
 * Fixes and tips by:
 *	- Janos Farkas (CHEXUM@sparta.banki.hu)
 *	- Jes Degn Soerensen (jds@kom.auc.dk)
 *
 * ----------------------------------------------------------------------------
 *
 * This program is based on
 *
 *	ariadne.?:	Amiga Linux/68k Ariadne Ethernet Driver
 *			(C) Copyright 1995 by Geert Uytterhoeven,
 *                                            Peter De Schrijver
 *
 *	lance.c:	An AMD LANCE ethernet driver for linux.
 *			Written 1993-94 by Donald Becker.
 *
 *	Am79C960:	PCnet(tm)-ISA Single-Chip Ethernet Controller
 *			Advanced Micro Devices
 *			Publication #16907, Rev. B, Amendment/0, May 1994
 *
 * ----------------------------------------------------------------------------
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of the Linux
 * distribution for more details.
 *
 * ----------------------------------------------------------------------------
 *
 * The A2065 is a Zorro-II board made by Commodore/Ameristar. It contains:
 *
 *	- an Am7990 Local Area Network Controller for Ethernet (LANCE) with
 *	  both 10BASE-2 (thin coax) and AUI (DB-15) connectors
 */

#include <linux/module.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/config.h>
#include <linux/init.h>

#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <linux/errno.h>

#include <asm/amigaints.h>
#include <asm/amigahw.h>
#include <linux/zorro.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include "a2065.h"


	/*
	 *		Transmit/Receive Ring Definitions
	 */

#define LANCE_LOG_TX_BUFFERS	(2)
#define LANCE_LOG_RX_BUFFERS	(4)

#define TX_RING_SIZE		(1<<LANCE_LOG_TX_BUFFERS)
#define RX_RING_SIZE		(1<<LANCE_LOG_RX_BUFFERS)

#define TX_RING_MOD_MASK	(TX_RING_SIZE-1)
#define RX_RING_MOD_MASK	(RX_RING_SIZE-1)

#define PKT_BUF_SIZE		(1544)
#define RX_BUFF_SIZE            PKT_BUF_SIZE
#define TX_BUFF_SIZE            PKT_BUF_SIZE


	/*
	 *		Layout of the Lance's RAM Buffer
	 */


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

	char   rx_buf [RX_RING_SIZE][RX_BUFF_SIZE];
	char   tx_buf [TX_RING_SIZE][TX_BUFF_SIZE];
};


	/*
	 *		Private Device Data
	 */

struct lance_private {
	char *name;
	volatile struct lance_regs *ll;
	volatile struct lance_init_block *init_block;	    /* Hosts view */
	volatile struct lance_init_block *lance_init_block; /* Lance view */

	int rx_new, tx_new;
	int rx_old, tx_old;
    
	int lance_log_rx_bufs, lance_log_tx_bufs;
	int rx_ring_mod_mask, tx_ring_mod_mask;

	struct net_device_stats stats;
	int tpe;		      /* cable-selection is TPE */
	int auto_select;	      /* cable-selection by carrier */
	unsigned short busmaster_regval;

#ifdef CONFIG_AMIGA
	unsigned int key;
#endif
#ifdef CONFIG_SUNLANCE
	struct Linux_SBus_DMA *ledma; /* if set this points to ledma and arch=4m */
	int burst_sizes;	      /* ledma SBus burst sizes */
#endif
};

#define TX_BUFFS_AVAIL ((lp->tx_old<=lp->tx_new)?\
			lp->tx_old+lp->tx_ring_mod_mask-lp->tx_new:\
			lp->tx_old - lp->tx_new-1)


#define LANCE_ADDR(x) ((int)(x) & ~0xff000000)

/* Load the CSR registers */
static void load_csrs (struct lance_private *lp)
{
	volatile struct lance_regs *ll = lp->ll;
	volatile struct lance_init_block *aib = lp->lance_init_block;
	int leptr;

	leptr = LANCE_ADDR (aib);

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
	volatile struct lance_init_block *aib; /* for LANCE_ADDR computations */
	int leptr;
	int i;

	aib = lp->lance_init_block;

	/* Lock out other processes while setting up hardware */
	dev->tbusy = 1;
	lp->rx_new = lp->tx_new = 0;
	lp->rx_old = lp->tx_old = 0;

	ib->mode = 0;

	/* Copy the ethernet address to the lance init block
	 * Note that on the sparc you need to swap the ethernet address.
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
	for (i = 0; i <= (1<<lp->lance_log_tx_bufs); i++) {
		leptr = LANCE_ADDR(&aib->tx_buf[i][0]);
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
	for (i = 0; i < (1<<lp->lance_log_rx_bufs); i++) {
		leptr = LANCE_ADDR(&aib->rx_buf[i][0]);

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
	leptr = LANCE_ADDR(&aib->brx_ring);
	ib->rx_len = (lp->lance_log_rx_bufs << 13) | (leptr >> 16);
	ib->rx_ptr = leptr;
	if (ZERO)
		printk ("RX ptr: %8.8x\n", leptr);
    
	/* Setup tx descriptor pointer */
	leptr = LANCE_ADDR(&aib->btx_ring);
	ib->tx_len = (lp->lance_log_tx_bufs << 13) | (leptr >> 16);
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

	ll->rap = LE_CSR0;
	ll->rdp = LE_C0_INIT;

	/* Wait for the lance to complete initialization */
	for (i = 0; (i < 100) && !(ll->rdp & (LE_C0_ERR | LE_C0_IDON)); i++)
		barrier();
	if ((i == 100) || (ll->rdp & LE_C0_ERR)) {
		printk ("LANCE unopened after %d ticks, csr0=%4.4x.\n", i, ll->rdp);
		return -1;
	}

	/* Clear IDON by writing a "1", enable interrupts and start lance */
	ll->rdp = LE_C0_IDON;
	ll->rdp = LE_C0_INEA | LE_C0_STRT;

	return 0;
}

static int lance_rx (struct device *dev)
{
	struct lance_private *lp = (struct lance_private *) dev->priv;
	volatile struct lance_init_block *ib = lp->init_block;
	volatile struct lance_regs *ll = lp->ll;
	volatile struct lance_rx_desc *rd;
	unsigned char bits;
	int len = 0;			/* XXX shut up gcc warnings */
	struct sk_buff *skb = 0;	/* XXX shut up gcc warnings */

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
    
	ll->rdp = LE_C0_RINT|LE_C0_INEA;
	for (rd = &ib->brx_ring [lp->rx_new];
	     !((bits = rd->rmd1_bits) & LE_R1_OWN);
	     rd = &ib->brx_ring [lp->rx_new]) {

		/* We got an incomplete frame? */
		if ((bits & LE_R1_POK) != LE_R1_POK) {
			lp->stats.rx_over_errors++;
			lp->stats.rx_errors++;
			continue;
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
				lp->rx_new = (lp->rx_new + 1) & lp->rx_ring_mod_mask;
				return 0;
			}
	    
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
		lp->rx_new = (lp->rx_new + 1) & lp->rx_ring_mod_mask;
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

	/* csr0 is 2f3 */
	ll->rdp = LE_C0_TINT | LE_C0_INEA;
	/* csr0 is 73 */

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

			/* buffer errors and underflows turn off the transmitter */
			/* Restart the adapter */
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
	
		j = (j + 1) & lp->tx_ring_mod_mask;
	}
	lp->tx_old = j;
	ll->rdp = LE_C0_TINT | LE_C0_INEA;
	return 0;
}

static void lance_interrupt (int irq, void *dev_id, struct pt_regs *regs)
{
	struct device *dev;
	struct lance_private *lp;
	volatile struct lance_regs *ll;
	int csr0;

	dev = (struct device *) dev_id;

	lp = (struct lance_private *) dev->priv;
	ll = lp->ll;

	ll->rap = LE_CSR0;		/* LANCE Controller Status */
	csr0 = ll->rdp;

	if (!(csr0 & LE_C0_INTR))	/* Check if any interrupt has */
		return;			/* been generated by the Lance. */

	if (dev->interrupt)
		printk ("%s: again", dev->name);

	dev->interrupt = 1;

	/* Acknowledge all the interrupt sources ASAP */
	ll->rdp = csr0 & ~(LE_C0_INEA|LE_C0_TDMD|LE_C0_STOP|LE_C0_STRT|
			   LE_C0_INIT);

	if ((csr0 & LE_C0_ERR)) {
		/* Clear the error condition */
		ll->rdp = LE_C0_BABL|LE_C0_ERR|LE_C0_MISS|LE_C0_INEA;
	}
    
	if (csr0 & LE_C0_RINT)
		lance_rx (dev);

	if (csr0 & LE_C0_TINT)
		lance_tx (dev);

	/* Log misc errors. */
	if (csr0 & LE_C0_BABL)
		lp->stats.tx_errors++;       /* Tx babble. */
	if (csr0 & LE_C0_MISS)
		lp->stats.rx_errors++;       /* Missed a Rx frame. */
	if (csr0 & LE_C0_MERR) {
		printk("%s: Bus master arbitration failure, status %4.4x.\n", dev->name, csr0);
		/* Restart the chip. */
		ll->rdp = LE_C0_STRT;
	}

	if ((TX_BUFFS_AVAIL >= 0) && dev->tbusy) {
		dev->tbusy = 0;
		mark_bh (NET_BH);
	}
	ll->rap = LE_CSR0;
	ll->rdp = LE_C0_BABL|LE_C0_CERR|LE_C0_MISS|LE_C0_MERR|
					LE_C0_IDON|LE_C0_INEA;

	dev->interrupt = 0;
}

struct device *last_dev = 0;

static int lance_open (struct device *dev)
{
	struct lance_private *lp = (struct lance_private *)dev->priv;
	volatile struct lance_regs *ll = lp->ll;
	int status = 0;

	last_dev = dev;

	/* Install the Interrupt handler */
	if (request_irq(IRQ_AMIGA_PORTS, lance_interrupt, 0,
			"a2065 Ethernet", dev))
		return -EAGAIN;

	/* Stop the Lance */
	ll->rap = LE_CSR0;
	ll->rdp = LE_C0_STOP;

	load_csrs (lp);
	lance_init_ring (dev);

	dev->tbusy = 0;
	dev->interrupt = 0;
	dev->start = 1;

	status = init_restart_lance (lp);

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

	free_irq(IRQ_AMIGA_PORTS, dev);

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

	load_csrs (lp);
	lance_init_ring (dev);
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
	int entry, skblen, len;
	int status = 0;
	static int outs;
	unsigned long flags;

	/* Transmitter timeout, serious problems */
	if (dev->tbusy) {
		int tickssofar = jiffies - dev->trans_start;
	    
		if (tickssofar < 100) {
			status = -1;
		} else {
			printk ("%s: transmit timed out, status %04x, resetting\n",
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

	save_flags(flags);
	cli();

	if (!TX_BUFFS_AVAIL){
		restore_flags(flags);
		return -1;
	}

#ifdef DEBUG_DRIVER
	/* dump the packet */
	{
		int i;
	
		for (i = 0; i < 64; i++) {
			if ((i % 16) == 0)
				printk ("\n");
			printk ("%2.2x ", skb->data [i]);
		}
	}
#endif
	len = (skblen <= ETH_ZLEN) ? ETH_ZLEN : skblen;
	entry = lp->tx_new & lp->tx_ring_mod_mask;
	ib->btx_ring [entry].length = (-len) | 0xf000;
	ib->btx_ring [entry].misc = 0;
    
	memcpy ((char *)&ib->tx_buf [entry][0], skb->data, skblen);

	/* Clear the slack of the packet, do I need this? */
	if (len != skblen)
		memset ((char *) &ib->tx_buf [entry][skblen], 0, len - skblen);
    
	/* Now, give the packet to the lance */
	ib->btx_ring [entry].tmd1_bits = (LE_T1_POK|LE_T1_OWN);
	lp->tx_new = (lp->tx_new+1) & lp->tx_ring_mod_mask;

	outs++;
	/* Kick the lance: transmit now */
	ll->rdp = LE_C0_INEA | LE_C0_TDMD;
	dev->trans_start = jiffies;
	dev_kfree_skb (skb);
    
	if (TX_BUFFS_AVAIL)
		dev->tbusy = 0;
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
			for (bit = *addrs++, j = 0; j < 8; j++, bit>>=1)
			{
				int test;

				test = ((bit ^ crc) & 0x01);
				crc >>= 1;

				if (test)
				{
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


__initfunc(int a2065_probe(struct device *dev))
{
	unsigned int key, is_cbm;
	const struct ConfigDev *cd;
	u_long board;
	u_long sn;
	struct lance_private *priv;
	struct A2065Board *a2065;

	if ((key = is_cbm = zorro_find(ZORRO_PROD_CBM_A2065_1, 0, 0)) ||
	    (key = is_cbm = zorro_find(ZORRO_PROD_CBM_A2065_2, 0, 0)) ||
	    (key = zorro_find(ZORRO_PROD_AMERISTAR_A2065, 0, 0))) {
		cd = zorro_get_board(key);
		if ((board = (u_long)cd->cd_BoardAddr)) {
			sn = cd->cd_Rom.er_SerialNumber;
			if (is_cbm) {			/* Commodore */
				dev->dev_addr[0] = 0x00;
				dev->dev_addr[1] = 0x80;
				dev->dev_addr[2] = 0x10;
			} else {			/* Ameristar */
				dev->dev_addr[0] = 0x00;
				dev->dev_addr[1] = 0x00;
				dev->dev_addr[2] = 0x9f;
			}
			dev->dev_addr[3] = (sn>>16) & 0xff;
			dev->dev_addr[4] = (sn>>8) & 0xff;
			dev->dev_addr[5] = sn & 0xff;
			printk("%s: A2065 at 0x%08lx, Ethernet Address %02x:%02x:%02x:%02x:%02x:%02x\n",
			       dev->name, board, dev->dev_addr[0],
			       dev->dev_addr[1], dev->dev_addr[2],
			       dev->dev_addr[3], dev->dev_addr[4],
			       dev->dev_addr[5]);

			init_etherdev(dev, 0);

			dev->priv = kmalloc(sizeof(struct
						   lance_private),
					    GFP_KERNEL);
			priv = (struct lance_private *)dev->priv;
			memset(priv, 0, sizeof(struct lance_private));

			a2065 = (struct A2065Board *)ZTWO_VADDR(board);
			priv->ll = &a2065->Lance;
			priv->init_block =
					(struct lance_init_block *)&a2065->RAM;
			priv->lance_init_block = (struct lance_init_block *)
					      offsetof(struct A2065Board, RAM);
			priv->auto_select = 0;
			priv->key = key;
			priv->busmaster_regval = LE_C3_BSWP;

			priv->lance_log_rx_bufs = LANCE_LOG_RX_BUFFERS;
			priv->lance_log_tx_bufs = LANCE_LOG_TX_BUFFERS;
			priv->rx_ring_mod_mask = RX_RING_MOD_MASK;
			priv->tx_ring_mod_mask = TX_RING_MOD_MASK;

			dev->open = &lance_open;
			dev->stop = &lance_close;
			dev->hard_start_xmit = &lance_start_xmit;
			dev->get_stats = &lance_get_stats;
			dev->set_multicast_list = &lance_set_multicast;
			dev->dma = 0;

			ether_setup(dev);
			zorro_config_board(key, 0);
			return(0);
		}
	}
	return(-ENODEV);
}


#ifdef MODULE
static char devicename[9] = { 0, };

static struct device a2065_dev =
{
	devicename,			/* filled in by register_netdev() */
	0, 0, 0, 0,			/* memory */
	0, 0,				/* base, irq */
	0, 0, 0, NULL, a2065_probe,
};

int init_module(void)
{
	int err;

	if ((err = register_netdev(&a2065_dev))) {
		if (err == -EIO)
			printk("No A2065 board found. Module not loaded.\n");
		return(err);
	}
	return(0);
}

void cleanup_module(void)
{
	struct lance_private *priv = (struct lance_private *)a2065_dev.priv;

	unregister_netdev(&a2065_dev);
	zorro_unconfig_board(priv->key, 0);
	kfree(priv);
}

#endif /* MODULE */
