/* sunbmac.c: Driver for Sparc BigMAC 100baseT ethernet adapters.
 *
 * Copyright (C) 1997, 1998 David S. Miller (davem@caip.rutgers.edu)
 */

static char *version =
        "sunbmac.c:v1.1 8/Dec/98 David S. Miller (davem@caipfs.rutgers.edu)\n";

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
#include <linux/errno.h>
#include <asm/byteorder.h>

#include <asm/idprom.h>
#include <asm/sbus.h>
#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/auxio.h>
#include <asm/pgtable.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include "sunbmac.h"

#undef DEBUG_PROBE
#undef DEBUG_TX
#undef DEBUG_IRQ

#ifdef DEBUG_PROBE
#define DP(x)  printk x
#else
#define DP(x)
#endif

#ifdef DEBUG_TX
#define DTX(x)  printk x
#else
#define DTX(x)
#endif

#ifdef DEBUG_IRQ
#define DIRQ(x)  printk x
#else
#define DIRQ(x)
#endif

#ifdef MODULE
static struct bigmac *root_bigmac_dev = NULL;
#endif

#define DEFAULT_JAMSIZE    4 /* Toe jam */

#define QEC_RESET_TRIES 200

static inline int qec_global_reset(struct qe_globreg *gregs)
{
	int tries = QEC_RESET_TRIES;

	gregs->ctrl = GLOB_CTRL_RESET;
	while(--tries) {
		if(gregs->ctrl & GLOB_CTRL_RESET) {
			udelay(20);
			continue;
		}
		break;
	}
	if(tries)
		return 0;
	printk("BigMAC: Cannot reset the QEC.\n");
	return -1;
}

static void qec_init(struct bigmac *bp)
{
	struct qe_globreg *gregs = bp->gregs;
	struct linux_sbus_device *qec_sdev = bp->qec_sbus_dev;
	unsigned char bsizes = bp->bigmac_bursts;
	unsigned int regval;

	/* 64byte bursts do not work at the moment, do
	 * not even try to enable them.  -DaveM
	 */
	if(bsizes & DMA_BURST32)
		regval = GLOB_CTRL_B32;
	else
		regval = GLOB_CTRL_B16;
	gregs->ctrl = regval | GLOB_CTRL_BMODE;

	gregs->psize = GLOB_PSIZE_2048;

	/* All of memsize is given to bigmac. */
	gregs->msize = qec_sdev->reg_addrs[1].reg_size;

	/* Half to the transmitter, half to the receiver. */
	gregs->rsize = gregs->tsize = qec_sdev->reg_addrs[1].reg_size >> 1;
}

/* XXX auto negotiation on these things might not be pleasant... */

#define TX_RESET_TRIES     32
#define RX_RESET_TRIES     32

static inline void bigmac_tx_reset(struct BIG_MAC_regs *bregs)
{
	int tries = TX_RESET_TRIES;

	bregs->tx_cfg = 0;

	/* The fifo threshold bit is read-only and does
	 * not clear.  -DaveM
	 */
	while((bregs->tx_cfg & ~(BIGMAC_TXCFG_FIFO)) != 0 &&
	      --tries != 0)
		udelay(20);

	if(!tries) {
		printk("BIGMAC: Transmitter will not reset.\n");
		printk("BIGMAC: tx_cfg is %08x\n", bregs->tx_cfg);
	}
}

static inline void bigmac_rx_reset(struct BIG_MAC_regs *bregs)
{
	int tries = RX_RESET_TRIES;

	bregs->rx_cfg = 0;
	while((bregs->rx_cfg) && --tries)
		udelay(20);

	if(!tries) {
		printk("BIGMAC: Receiver will not reset.\n");
		printk("BIGMAC: rx_cfg is %08x\n", bregs->rx_cfg);
	}
}

/* Reset the transmitter and receiver. */
static void bigmac_stop(struct bigmac *bp)
{
	bigmac_tx_reset(bp->bregs);
	bigmac_rx_reset(bp->bregs);
}

static void bigmac_get_counters(struct bigmac *bp, struct BIG_MAC_regs *bregs)
{
	struct enet_statistics *stats = &bp->enet_stats;

	stats->rx_crc_errors += bregs->rcrce_ctr;
	bregs->rcrce_ctr = 0;

	stats->rx_frame_errors += bregs->unale_ctr;
	bregs->unale_ctr = 0;

	stats->rx_length_errors += bregs->gle_ctr;
	bregs->gle_ctr = 0;

	stats->tx_aborted_errors += bregs->ex_ctr;

	stats->collisions += (bregs->ex_ctr + bregs->lt_ctr);
	bregs->ex_ctr = bregs->lt_ctr = 0;
}

static inline void bigmac_clean_rings(struct bigmac *bp)
{
	int i;

	for(i = 0; i < RX_RING_SIZE; i++) {
		if(bp->rx_skbs[i] != NULL) {
			dev_kfree_skb(bp->rx_skbs[i]);
			bp->rx_skbs[i] = NULL;
		}
	}

	for(i = 0; i < TX_RING_SIZE; i++) {
		if(bp->tx_skbs[i] != NULL) {
			dev_kfree_skb(bp->tx_skbs[i]);
			bp->tx_skbs[i] = NULL;
		}
	}
}

static void bigmac_init_rings(struct bigmac *bp, int from_irq)
{
	struct bmac_init_block *bb = bp->bmac_block;
	struct net_device *dev = bp->dev;
	int i, gfp_flags = GFP_KERNEL;

	if(from_irq || in_interrupt())
		gfp_flags = GFP_ATOMIC;

	bp->rx_new = bp->rx_old = bp->tx_new = bp->tx_old = 0;

	/* Free any skippy bufs left around in the rings. */
	bigmac_clean_rings(bp);

	/* Now get new skippy bufs for the receive ring. */
	for(i = 0; i < RX_RING_SIZE; i++) {
		struct sk_buff *skb;

		skb = big_mac_alloc_skb(RX_BUF_ALLOC_SIZE, gfp_flags);
		if(!skb)
			continue;

		bp->rx_skbs[i] = skb;
		skb->dev = dev;

		/* Because we reserve afterwards. */
		skb_put(skb, ETH_FRAME_LEN);
		skb_reserve(skb, 34);

		bb->be_rxd[i].rx_addr = sbus_dvma_addr(skb->data);
		bb->be_rxd[i].rx_flags =
			(RXD_OWN | ((RX_BUF_ALLOC_SIZE - 34) & RXD_LENGTH));
	}

	for(i = 0; i < TX_RING_SIZE; i++)
		bb->be_txd[i].tx_flags = bb->be_txd[i].tx_addr = 0;
}

#ifndef __sparc_v9__
static void sun4c_bigmac_init_rings(struct bigmac *bp)
{
	struct bmac_init_block *bb = bp->bmac_block;
	__u32 bbufs_dvma = bp->s4c_buf_dvma;
	int i;

	bp->rx_new = bp->rx_old = bp->tx_new = bp->tx_old = 0;

	for(i = 0; i < RX_RING_SIZE; i++) {
		bb->be_rxd[i].rx_addr = bbufs_dvma + bbuf_offset(rx_buf, i);
		bb->be_rxd[i].rx_flags =
			(RXD_OWN | (SUN4C_RX_BUFF_SIZE & RXD_LENGTH));
	}

	for(i = 0; i < TX_RING_SIZE; i++)
		bb->be_txd[i].tx_flags = bb->be_txd[i].tx_addr = 0;
}
#endif

#define MGMT_CLKON  (MGMT_PAL_INT_MDIO|MGMT_PAL_EXT_MDIO|MGMT_PAL_OENAB|MGMT_PAL_DCLOCK)
#define MGMT_CLKOFF (MGMT_PAL_INT_MDIO|MGMT_PAL_EXT_MDIO|MGMT_PAL_OENAB)

static inline void idle_transceiver(struct bmac_tcvr *tregs)
{
	volatile unsigned int garbage;
	int i = 20;

	while(i--) {
		tregs->mgmt_pal = MGMT_CLKOFF;
		garbage = tregs->mgmt_pal;
		tregs->mgmt_pal = MGMT_CLKON;
		garbage = tregs->mgmt_pal;
	}
}

static void write_tcvr_bit(struct bigmac *bp, struct bmac_tcvr *tregs, int bit)
{
	volatile unsigned int garbage;

	if(bp->tcvr_type == internal) {
		bit = (bit & 1) << 3;
		tregs->mgmt_pal = bit | (MGMT_PAL_OENAB | MGMT_PAL_EXT_MDIO);
		garbage = tregs->mgmt_pal;
		tregs->mgmt_pal = bit | (MGMT_PAL_OENAB |
					 MGMT_PAL_EXT_MDIO |
					 MGMT_PAL_DCLOCK);
		garbage = tregs->mgmt_pal;
	} else if(bp->tcvr_type == external) {
		bit = (bit & 1) << 2;
		tregs->mgmt_pal = bit | (MGMT_PAL_INT_MDIO | MGMT_PAL_OENAB);
		garbage = tregs->mgmt_pal;
		tregs->mgmt_pal = bit | (MGMT_PAL_INT_MDIO |
					 MGMT_PAL_OENAB |
					 MGMT_PAL_DCLOCK);
		garbage = tregs->mgmt_pal;
	} else {
		printk("write_tcvr_bit: No transceiver type known!\n");
	}
}

static int read_tcvr_bit(struct bigmac *bp, struct bmac_tcvr *tregs)
{
	volatile unsigned int garbage;
	int retval = 0;

	if(bp->tcvr_type == internal) {
		tregs->mgmt_pal = MGMT_PAL_EXT_MDIO;
		garbage = tregs->mgmt_pal;
		tregs->mgmt_pal = MGMT_PAL_EXT_MDIO | MGMT_PAL_DCLOCK;
		garbage = tregs->mgmt_pal;
		retval = (tregs->mgmt_pal & MGMT_PAL_INT_MDIO) >> 3;
	} else if(bp->tcvr_type == external) {
		tregs->mgmt_pal = MGMT_PAL_INT_MDIO;
		garbage = tregs->mgmt_pal;
		tregs->mgmt_pal = MGMT_PAL_INT_MDIO | MGMT_PAL_DCLOCK;
		garbage = tregs->mgmt_pal;
		retval = (tregs->mgmt_pal & MGMT_PAL_EXT_MDIO) >> 2;
	} else {
		printk("read_tcvr_bit: No transceiver type known!\n");
	}
	return retval;
}

static int read_tcvr_bit2(struct bigmac *bp, struct bmac_tcvr *tregs)
{
	volatile unsigned int garbage;
	int retval = 0;

	if(bp->tcvr_type == internal) {
		tregs->mgmt_pal = MGMT_PAL_EXT_MDIO;
		garbage = tregs->mgmt_pal;
		retval = (tregs->mgmt_pal & MGMT_PAL_INT_MDIO) >> 3;
		tregs->mgmt_pal = (MGMT_PAL_EXT_MDIO | MGMT_PAL_DCLOCK);
		garbage = tregs->mgmt_pal;
	} else if(bp->tcvr_type == external) {
		tregs->mgmt_pal = MGMT_PAL_INT_MDIO;
		garbage = tregs->mgmt_pal;
		retval = (tregs->mgmt_pal & MGMT_PAL_EXT_MDIO) >> 2;
		tregs->mgmt_pal = (MGMT_PAL_INT_MDIO | MGMT_PAL_DCLOCK);
		garbage = tregs->mgmt_pal;
	} else {
		printk("read_tcvr_bit2: No transceiver type known!\n");
	}
	return retval;
}

static inline void put_tcvr_byte(struct bigmac *bp,
				 struct bmac_tcvr *tregs,
				 unsigned int byte)
{
	int shift = 4;

	do {
		write_tcvr_bit(bp, tregs, ((byte >> shift) & 1));
		shift -= 1;
	} while (shift >= 0);
}

static void bigmac_tcvr_write(struct bigmac *bp, struct bmac_tcvr *tregs,
			      int reg, unsigned short val)
{
	int shift;

	reg &= 0xff;
	val &= 0xffff;
	switch(bp->tcvr_type) {
	case internal:
	case external:
		break;

	default:
		printk("bigmac_tcvr_read: Whoops, no known transceiver type.\n");
		return;
	};

	idle_transceiver(tregs);
	write_tcvr_bit(bp, tregs, 0);
	write_tcvr_bit(bp, tregs, 1);
	write_tcvr_bit(bp, tregs, 0);
	write_tcvr_bit(bp, tregs, 1);

	put_tcvr_byte(bp, tregs,
		      ((bp->tcvr_type == internal) ?
		       BIGMAC_PHY_INTERNAL : BIGMAC_PHY_EXTERNAL));

	put_tcvr_byte(bp, tregs, reg);

	write_tcvr_bit(bp, tregs, 1);
	write_tcvr_bit(bp, tregs, 0);

	shift = 15;
	do {
		write_tcvr_bit(bp, tregs, (val >> shift) & 1);
		shift -= 1;
	} while(shift >= 0);
}

static unsigned short bigmac_tcvr_read(struct bigmac *bp,
				       struct bmac_tcvr *tregs,
				       int reg)
{
	unsigned short retval = 0;

	reg &= 0xff;
	switch(bp->tcvr_type) {
	case internal:
	case external:
		break;

	default:
		printk("bigmac_tcvr_read: Whoops, no known transceiver type.\n");
		return 0xffff;
	};

	idle_transceiver(tregs);
	write_tcvr_bit(bp, tregs, 0);
	write_tcvr_bit(bp, tregs, 1);
	write_tcvr_bit(bp, tregs, 1);
	write_tcvr_bit(bp, tregs, 0);

	put_tcvr_byte(bp, tregs,
		      ((bp->tcvr_type == internal) ?
		       BIGMAC_PHY_INTERNAL : BIGMAC_PHY_EXTERNAL));

	put_tcvr_byte(bp, tregs, reg);

	if(bp->tcvr_type == external) {
		int shift = 15;

		(void) read_tcvr_bit2(bp, tregs);
		(void) read_tcvr_bit2(bp, tregs);

		do {
			int tmp;

			tmp = read_tcvr_bit2(bp, tregs);
			retval |= ((tmp & 1) << shift);
			shift -= 1;
		} while(shift >= 0);

		(void) read_tcvr_bit2(bp, tregs);
		(void) read_tcvr_bit2(bp, tregs);
		(void) read_tcvr_bit2(bp, tregs);
	} else {
		int shift = 15;

		(void) read_tcvr_bit(bp, tregs);
		(void) read_tcvr_bit(bp, tregs);

		do {
			int tmp;

			tmp = read_tcvr_bit(bp, tregs);
			retval |= ((tmp & 1) << shift);
			shift -= 1;
		} while(shift >= 0);

		(void) read_tcvr_bit(bp, tregs);
		(void) read_tcvr_bit(bp, tregs);
		(void) read_tcvr_bit(bp, tregs);
	}
	return retval;
}

static void bigmac_tcvr_init(struct bigmac *bp)
{
	volatile unsigned int garbage;
	struct bmac_tcvr *tregs = bp->tregs;

	idle_transceiver(tregs);
	tregs->mgmt_pal = (MGMT_PAL_INT_MDIO |
			   MGMT_PAL_EXT_MDIO |
			   MGMT_PAL_DCLOCK);
	garbage = tregs->mgmt_pal;

	/* Only the bit for the present transceiver (internal or
	 * external) will stick, set them both and see what stays.
	 */
	tregs->mgmt_pal = (MGMT_PAL_INT_MDIO |
			   MGMT_PAL_EXT_MDIO);
	garbage = tregs->mgmt_pal;
	udelay(20);

	if(tregs->mgmt_pal & MGMT_PAL_EXT_MDIO) {
		bp->tcvr_type = external;
		tregs->tcvr_pal = ~(TCVR_PAL_EXTLBACK |
				    TCVR_PAL_MSENSE |
				    TCVR_PAL_LTENABLE);
		garbage = tregs->tcvr_pal;
	} else if(tregs->mgmt_pal & MGMT_PAL_INT_MDIO) {
		bp->tcvr_type = internal;
		tregs->tcvr_pal = ~(TCVR_PAL_SERIAL |
				    TCVR_PAL_EXTLBACK |
				    TCVR_PAL_MSENSE |
				    TCVR_PAL_LTENABLE);
		garbage = tregs->tcvr_pal;
	} else {
		printk("BIGMAC: AIEEE, neither internal nor "
		       "external MDIO available!\n");
		printk("BIGMAC: mgmt_pal[%08x] tcvr_pal[%08x]\n",
		       tregs->mgmt_pal, tregs->tcvr_pal);
	}
}

static int bigmac_init(struct bigmac *, int);

static int try_next_permutation(struct bigmac *bp, struct bmac_tcvr *tregs)
{
	if(bp->sw_bmcr & BMCR_SPEED100) {
		int timeout;

		/* Reset the PHY. */
		bp->sw_bmcr	= (BMCR_ISOLATE | BMCR_PDOWN | BMCR_LOOPBACK);
		bigmac_tcvr_write(bp, tregs, BIGMAC_BMCR, bp->sw_bmcr);
		bp->sw_bmcr	= (BMCR_RESET);
		bigmac_tcvr_write(bp, tregs, BIGMAC_BMCR, bp->sw_bmcr);

		timeout = 64;
		while(--timeout) {
			bp->sw_bmcr = bigmac_tcvr_read(bp, tregs, BIGMAC_BMCR);
			if((bp->sw_bmcr & BMCR_RESET) == 0)
				break;
			udelay(20);
		}
		if(timeout == 0)
			printk("%s: PHY reset failed.\n", bp->dev->name);

		bp->sw_bmcr = bigmac_tcvr_read(bp, tregs, BIGMAC_BMCR);

		/* Now we try 10baseT. */
		bp->sw_bmcr &= ~(BMCR_SPEED100);
		bigmac_tcvr_write(bp, tregs, BIGMAC_BMCR, bp->sw_bmcr);
		return 0;
	}

	/* We've tried them all. */
	return -1;
}

static void bigmac_timer(unsigned long data)
{
	struct bigmac *bp = (struct bigmac *) data;
	struct bmac_tcvr *tregs = bp->tregs;
	int restart_timer = 0;

	bp->timer_ticks++;
	if(bp->timer_state == ltrywait) {
		bp->sw_bmsr = bigmac_tcvr_read(bp, tregs, BIGMAC_BMSR);
		bp->sw_bmcr = bigmac_tcvr_read(bp, tregs, BIGMAC_BMCR);
		if(bp->sw_bmsr & BMSR_LSTATUS) {
			printk("%s: Link is now up at %s.\n",
			       bp->dev->name,
			       (bp->sw_bmcr & BMCR_SPEED100) ?
			       "100baseT" : "10baseT");
			bp->timer_state = asleep;
			restart_timer = 0;
		} else {
			if(bp->timer_ticks >= 4) {
				int ret;

				ret = try_next_permutation(bp, tregs);
				if(ret == -1) {
					printk("%s: Link down, cable problem?\n",
					       bp->dev->name);
					ret = bigmac_init(bp, 0);
					if(ret) {
						printk("%s: Error, cannot re-init the "
						       "BigMAC.\n", bp->dev->name);
					}
					return;
				}
				bp->timer_ticks = 0;
				restart_timer = 1;
			} else {
				restart_timer = 1;
			}
		}
	} else {
		/* Can't happens.... */
		printk("%s: Aieee, link timer is asleep but we got one anyways!\n",
		       bp->dev->name);
		restart_timer = 0;
		bp->timer_ticks = 0;
		bp->timer_state = asleep; /* foo on you */
	}

	if(restart_timer != 0) {
		bp->bigmac_timer.expires = jiffies + ((12 * HZ)/10); /* 1.2 sec. */
		add_timer(&bp->bigmac_timer);
	}
}

/* Well, really we just force the chip into 100baseT then
 * 10baseT, each time checking for a link status.
 */
static void bigmac_begin_auto_negotiation(struct bigmac *bp)
{
	struct bmac_tcvr *tregs = bp->tregs;
	int timeout;

	/* Grab new software copies of PHY registers. */
	bp->sw_bmsr	= bigmac_tcvr_read(bp, tregs, BIGMAC_BMSR);
	bp->sw_bmcr	= bigmac_tcvr_read(bp, tregs, BIGMAC_BMCR);

	/* Reset the PHY. */
	bp->sw_bmcr	= (BMCR_ISOLATE | BMCR_PDOWN | BMCR_LOOPBACK);
	bigmac_tcvr_write(bp, tregs, BIGMAC_BMCR, bp->sw_bmcr);
	bp->sw_bmcr	= (BMCR_RESET);
	bigmac_tcvr_write(bp, tregs, BIGMAC_BMCR, bp->sw_bmcr);

	timeout = 64;
	while(--timeout) {
		bp->sw_bmcr = bigmac_tcvr_read(bp, tregs, BIGMAC_BMCR);
		if((bp->sw_bmcr & BMCR_RESET) == 0)
			break;
		udelay(20);
	}
	if(timeout == 0)
		printk("%s: PHY reset failed.\n", bp->dev->name);

	bp->sw_bmcr = bigmac_tcvr_read(bp, tregs, BIGMAC_BMCR);

	/* First we try 100baseT. */
	bp->sw_bmcr |= BMCR_SPEED100;
	bigmac_tcvr_write(bp, tregs, BIGMAC_BMCR, bp->sw_bmcr);

	bp->timer_state = ltrywait;
	bp->timer_ticks = 0;
	bp->bigmac_timer.expires = jiffies + (12 * HZ) / 10;
	bp->bigmac_timer.data = (unsigned long) bp;
	bp->bigmac_timer.function = &bigmac_timer;
	add_timer(&bp->bigmac_timer);
}

static int bigmac_init(struct bigmac *bp, int from_irq)
{
	struct qe_globreg    *gregs        = bp->gregs;
	struct qe_creg       *cregs        = bp->creg;
	struct BIG_MAC_regs  *bregs        = bp->bregs;
	unsigned char *e = &bp->dev->dev_addr[0];

	/* Latch current counters into statistics. */
	bigmac_get_counters(bp, bregs);

	/* Reset QEC. */
	qec_global_reset(gregs);

	/* Init QEC. */
	qec_init(bp);

	/* Alloc and reset the tx/rx descriptor chains. */
#ifndef __sparc_v9__
	if(sparc_cpu_model == sun4c)
		sun4c_bigmac_init_rings(bp);
	else
#endif
		bigmac_init_rings(bp, from_irq);

	/* Initialize the PHY. */
	bigmac_tcvr_init(bp);

	/* Stop transmitter and receiver. */
	bigmac_stop(bp);

	/* Set hardware ethernet address. */
	bregs->mac_addr2 = ((e[4] << 8) | e[5]);
	bregs->mac_addr1 = ((e[2] << 8) | e[3]);
	bregs->mac_addr0 = ((e[0] << 8) | e[1]);

	/* Clear the hash table until mc upload occurs. */
	bregs->htable3 = 0;
	bregs->htable2 = 0;
	bregs->htable1 = 0;
	bregs->htable0 = 0;

	/* Enable Big Mac hash table filter. */
	bregs->rx_cfg = (BIGMAC_RXCFG_HENABLE | BIGMAC_RXCFG_FIFO);

	udelay(20);

	/* Ok, configure the Big Mac transmitter. */
	bregs->tx_cfg = BIGMAC_TXCFG_FIFO;

	/* The HME docs recommend to use the 10LSB of our MAC here. */
	bregs->rand_seed = ((e[5] | e[4] << 8) & 0x3ff);

	/* Enable the output drivers no matter what. */
	bregs->xif_cfg = (BIGMAC_XCFG_ODENABLE | BIGMAC_XCFG_RESV);

	/* Tell the QEC where the ring descriptors are. */
	cregs->rxds = bp->bblock_dvma + bib_offset(be_rxd, 0);
	cregs->txds = bp->bblock_dvma + bib_offset(be_txd, 0);

	/* Setup the FIFO pointers into QEC local memory. */
	cregs->rxwbufptr = cregs->rxrbufptr = 0;
	cregs->txwbufptr = cregs->txrbufptr = gregs->rsize;

	/* Tell bigmac what interrupts we don't want to hear about. */
	bregs->imask = (BIGMAC_IMASK_GOTFRAME | BIGMAC_IMASK_SENTFRAME);

	/* Enable the various other irq's. */
	cregs->rimask = 0;
	cregs->timask = 0;
	cregs->qmask = 0;
	cregs->bmask = 0;

	/* Set jam size to a reasonable default. */
	bregs->jsize     = DEFAULT_JAMSIZE;

	/* Clear collision counter. */
	cregs->ccnt = 0;

	/* Enable transmitter and receiver. */
	bregs->tx_cfg |= BIGMAC_TXCFG_ENABLE;
	bregs->rx_cfg |= BIGMAC_RXCFG_ENABLE;

	/* Ok, start detecting link speed/duplex. */
	bigmac_begin_auto_negotiation(bp);

	/* Success. */
	return 0;
}

/* Error interrupts get sent here. */
static void bigmac_is_medium_rare(struct bigmac *bp,
				  unsigned int qec_status,
				  unsigned int bmac_status)
{
	printk("bigmac_is_medium_rare: ");
	if(qec_status & (GLOB_STAT_ER | GLOB_STAT_BM)) {
		if(qec_status & GLOB_STAT_ER)
			printk("QEC_ERROR, ");
		if(qec_status & GLOB_STAT_BM)
			printk("QEC_BMAC_ERROR, ");
	}
	if(bmac_status & CREG_STAT_ERRORS) {
		if(bmac_status & CREG_STAT_BERROR)
			printk("BMAC_ERROR, ");
		if(bmac_status & CREG_STAT_TXDERROR)
			printk("TXD_ERROR, ");
		if(bmac_status & CREG_STAT_TXLERR)
			printk("TX_LATE_ERROR, ");
		if(bmac_status & CREG_STAT_TXPERR)
			printk("TX_PARITY_ERROR, ");
		if(bmac_status & CREG_STAT_TXSERR)
			printk("TX_SBUS_ERROR, ");

		if(bmac_status & CREG_STAT_RXDROP)
			printk("RX_DROP_ERROR, ");

		if(bmac_status & CREG_STAT_RXSMALL)
			printk("RX_SMALL_ERROR, ");
		if(bmac_status & CREG_STAT_RXLERR)
			printk("RX_LATE_ERROR, ");
		if(bmac_status & CREG_STAT_RXPERR)
			printk("RX_PARITY_ERROR, ");
		if(bmac_status & CREG_STAT_RXSERR)
			printk("RX_SBUS_ERROR, ");
	}

	printk(" RESET\n");
	bigmac_init(bp, 1);
}

/* BigMAC transmit complete service routines. */
static inline void bigmac_tx(struct bigmac *bp)
{
	struct be_txd *txbase = &bp->bmac_block->be_txd[0];
	struct be_txd *this;
	int elem = bp->tx_old;

	DTX(("bigmac_tx: tx_old[%d] ", elem));
	while(elem != bp->tx_new) {
		struct sk_buff *skb;

		this = &txbase[elem];

		DTX(("this(%p) [flags(%08x)addr(%08x)]",
		     this, this->tx_flags, this->tx_addr));

		if(this->tx_flags & TXD_OWN)
			break;
		skb = bp->tx_skbs[elem];
		DTX(("skb(%p) ", skb));
		bp->tx_skbs[elem] = NULL;
		dev_kfree_skb(skb);

		bp->enet_stats.tx_packets++;
		elem = NEXT_TX(elem);
	}
	DTX((" DONE, tx_old=%d\n", elem));
	bp->tx_old = elem;
}

#ifndef __sparc_v9__
static inline void sun4c_bigmac_tx(struct bigmac *bp)
{
	struct be_txd *txbase = &bp->bmac_block->be_txd[0];
	struct be_txd *this;
	int elem = bp->tx_old;

	while(elem != bp->tx_new) {
		this = &txbase[elem];
		if(this->tx_flags & TXD_OWN)
			break;
		bp->enet_stats.tx_packets++;
		elem = NEXT_TX(elem);
	}
	bp->tx_old = elem;
}
#endif

/* BigMAC receive complete service routines. */
static inline void bigmac_rx(struct bigmac *bp)
{
	struct be_rxd *rxbase = &bp->bmac_block->be_rxd[0];
	struct be_rxd *this;
	int elem = bp->rx_new, drops = 0;

	this = &rxbase[elem];
	while(!(this->rx_flags & RXD_OWN)) {
		struct sk_buff *skb;
		unsigned int flags = this->rx_flags;
		int len = (flags & RXD_LENGTH); /* FCS not included */

		/* Check for errors. */
		if(len < ETH_ZLEN) {
			bp->enet_stats.rx_errors++;
			bp->enet_stats.rx_length_errors++;

	drop_it:
			/* Return it to the BigMAC. */
			bp->enet_stats.rx_dropped++;
			this->rx_addr = sbus_dvma_addr(bp->rx_skbs[elem]->data);
			this->rx_flags =
				(RXD_OWN | (RX_BUF_ALLOC_SIZE & RXD_LENGTH));
			goto next;
		}
		skb = bp->rx_skbs[elem];
#ifdef NEED_DMA_SYNCHRONIZATION
#ifdef __sparc_v9__
		if ((unsigned long) (skb->data + skb->len) >= MAX_DMA_ADDRESS) {
			printk("sunbmac: Bogus DMA buffer address "
			       "[%016lx]\n", ((unsigned long) skb->data));
			panic("DMA address too large, tell DaveM");
		}
#endif
		mmu_sync_dma(sbus_dvma_addr(skb->data),
			     skb->len, bp->bigmac_sbus_dev->my_bus);
#endif
		if(len > RX_COPY_THRESHOLD) {
			struct sk_buff *new_skb;

			/* Now refill the entry, if we can. */
			new_skb = big_mac_alloc_skb(RX_BUF_ALLOC_SIZE, GFP_ATOMIC);
			if(!new_skb) {
				drops++;
				goto drop_it;
			}
			bp->rx_skbs[elem] = new_skb;
			new_skb->dev = bp->dev;
			skb_put(new_skb, ETH_FRAME_LEN);
			skb_reserve(new_skb, 34);
			rxbase[elem].rx_addr = sbus_dvma_addr(new_skb->data);
			rxbase[elem].rx_flags =
				(RXD_OWN | ((RX_BUF_ALLOC_SIZE - 34) & RXD_LENGTH));

			/* Trim the original skb for the netif. */
			skb_trim(skb, len);
		} else {
			struct sk_buff *copy_skb = dev_alloc_skb(len + 2);

			if(!copy_skb) {
				drops++;
				goto drop_it;
			}
			copy_skb->dev = bp->dev;
			skb_reserve(copy_skb, 2);
			skb_put(copy_skb, len);
			eth_copy_and_sum(copy_skb, (unsigned char *)skb->data, len, 0);

			/* Reuse otiginal ring buffer. */
			rxbase[elem].rx_addr = sbus_dvma_addr(skb->data);
			rxbase[elem].rx_flags =
				(RXD_OWN | ((RX_BUF_ALLOC_SIZE - 34) & RXD_LENGTH));

			skb = copy_skb;
		}

		/* No checksums done by the BigMAC ;-( */
		skb->protocol = eth_type_trans(skb, bp->dev);
		netif_rx(skb);
		bp->enet_stats.rx_packets++;
	next:
		elem = NEXT_RX(elem);
		this = &rxbase[elem];
	}
	bp->rx_new = elem;
	if(drops)
		printk("%s: Memory squeeze, deferring packet.\n", bp->dev->name);
}

#ifndef __sparc_v9__
static inline void sun4c_bigmac_rx(struct bigmac *bp)
{
	struct be_rxd *rxbase = &bp->bmac_block->be_rxd[0];
	struct be_rxd *this;
	struct bigmac_buffers *bbufs = bp->sun4c_buffers;
	__u32 bbufs_dvma = bp->s4c_buf_dvma;
	int elem = bp->rx_new, drops = 0;

	this = &rxbase[elem];
	while(!(this->rx_flags & RXD_OWN)) {
		struct sk_buff *skb;
		unsigned char *this_bbuf =
			bbufs->rx_buf[elem & (SUN4C_RX_RING_SIZE - 1)];
		__u32 this_bbuf_dvma = bbufs_dvma +
			bbuf_offset(rx_buf, (elem & (SUN4C_RX_RING_SIZE - 1)));
		struct be_rxd *end_rxd =
			&rxbase[(elem+SUN4C_RX_RING_SIZE)&(RX_RING_SIZE-1)];
		unsigned int flags = this->rx_flags;
		int len = (flags & RXD_LENGTH) - 4; /* FCS not included */

		/* Check for errors. */
		if(len < ETH_ZLEN) {
			bp->enet_stats.rx_errors++;
			bp->enet_stats.rx_length_errors++;
			bp->enet_stats.rx_dropped++;
		} else {
			skb = dev_alloc_skb(len + 2);
			if(skb == 0) {
				drops++;
				bp->enet_stats.rx_dropped++;
			} else {
				skb->dev = bp->dev;
				skb_reserve(skb, 2);
				skb_put(skb, len);
				eth_copy_and_sum(skb, (unsigned char *)this_bbuf,
						 len, 0);
				skb->protocol = eth_type_trans(skb, bp->dev);
				netif_rx(skb);
				bp->enet_stats.rx_packets++;
			}
		}
		end_rxd->rx_addr = this_bbuf_dvma;
		end_rxd->rx_flags = (RXD_OWN | (SUN4C_RX_BUFF_SIZE & RXD_LENGTH));
		
		elem = NEXT_RX(elem);
		this = &rxbase[elem];
	}
	bp->rx_new = elem;
	if(drops)
		printk("%s: Memory squeeze, deferring packet.\n", bp->dev->name);
}
#endif

static void bigmac_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct bigmac *bp = (struct bigmac *) dev_id;
	unsigned int qec_status, bmac_status;

	DIRQ(("bigmac_interrupt: "));

	/* Latch status registers now. */
	bmac_status = bp->creg->stat;
	qec_status = bp->gregs->stat;

	bp->dev->interrupt = 1;

	DIRQ(("qec_status=%08x bmac_status=%08x\n", qec_status, bmac_status));
	if((qec_status & (GLOB_STAT_ER | GLOB_STAT_BM)) ||
	   (bmac_status & CREG_STAT_ERRORS))
		bigmac_is_medium_rare(bp, qec_status, bmac_status);

	if(bmac_status & CREG_STAT_TXIRQ)
		bigmac_tx(bp);

	if(bmac_status & CREG_STAT_RXIRQ)
		bigmac_rx(bp);

	if(bp->dev->tbusy && (TX_BUFFS_AVAIL(bp) >= 0)) {
		bp->dev->tbusy = 0;
		mark_bh(NET_BH);
	}

	bp->dev->interrupt = 0;
}

#ifndef __sparc_v9__
static void sun4c_bigmac_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct bigmac *bp = (struct bigmac *) dev_id;
	unsigned int qec_status, bmac_status;

	/* Latch status registers now. */
	bmac_status = bp->creg->stat;
	qec_status = bp->gregs->stat;

	bp->dev->interrupt = 1;

	if(qec_status & (GLOB_STAT_ER | GLOB_STAT_BM) ||
	   (bmac_status & CREG_STAT_ERRORS))
		bigmac_is_medium_rare(bp, qec_status, bmac_status);

	if(bmac_status & CREG_STAT_TXIRQ)
		sun4c_bigmac_tx(bp);

	if(bmac_status & CREG_STAT_RXIRQ)
		sun4c_bigmac_rx(bp);

	if(bp->dev->tbusy && (SUN4C_TX_BUFFS_AVAIL(bp) >= 0)) {
		bp->dev->tbusy = 0;
		mark_bh(NET_BH);
	}

	bp->dev->interrupt = 0;
}
#endif

static int bigmac_open(struct net_device *dev)
{
	struct bigmac *bp = (struct bigmac *) dev->priv;
	int res;

#ifndef __sparc_v9__
	if(sparc_cpu_model == sun4c) {
		if(request_irq(dev->irq, &sun4c_bigmac_interrupt,
			       SA_SHIRQ, "BIG MAC", (void *) bp)) {
			printk("BIGMAC: Can't order irq %d to go.\n", dev->irq);
			return -EAGAIN;
		}
	} else
#endif
	if(request_irq(dev->irq, &bigmac_interrupt,
		       SA_SHIRQ, "BIG MAC", (void *) bp)) {
		printk("BIGMAC: Can't order irq %d to go.\n", dev->irq);
		return -EAGAIN;
	}
	init_timer(&bp->bigmac_timer);
	res = bigmac_init(bp, 0);
	if(!res) {
		MOD_INC_USE_COUNT;
	}
	return res;
}

static int bigmac_close(struct net_device *dev)
{
	struct bigmac *bp = (struct bigmac *) dev->priv;

	del_timer(&bp->bigmac_timer);
	bp->timer_state = asleep;
	bp->timer_ticks = 0;

	bigmac_stop(bp);
	bigmac_clean_rings(bp);
	free_irq(dev->irq, (void *)bp);
	MOD_DEC_USE_COUNT;
	return 0;
}

/* Put a packet on the wire. */
static int bigmac_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct bigmac *bp = (struct bigmac *) dev->priv;
	int len, entry;

	if(dev->tbusy) {
		int tickssofar = jiffies - dev->trans_start;
	    
		if (tickssofar < 40) {
			return 1;
		} else {
			printk ("%s: transmit timed out, resetting\n", dev->name);
			bp->enet_stats.tx_errors++;
			bigmac_init(bp, 0);
			dev->tbusy = 0;
			dev->trans_start = jiffies;
			dev_kfree_skb(skb);
			return 0;
		}
	}

	if(test_and_set_bit(0, (void *) &dev->tbusy) != 0) {
		printk("%s: Transmitter access conflict.\n", dev->name);
		return 1;
	}

	if(!TX_BUFFS_AVAIL(bp))
		return 1;

#ifdef NEED_DMA_SYNCHRONIZATION
#ifdef __sparc_v9__
	if ((unsigned long) (skb->data + skb->len) >= MAX_DMA_ADDRESS) {
		struct sk_buff *new_skb = skb_copy(skb, GFP_DMA | GFP_ATOMIC);
		if(!new_skb)
			return 1;
		dev_kfree_skb(skb);
		skb = new_skb;
	}
#endif
	mmu_sync_dma(sbus_dvma_addr(skb->data),
		     skb->len, bp->bigmac_sbus_dev->my_bus);
#endif
	len = skb->len;
	entry = bp->tx_new;
	DTX(("bigmac_start_xmit: len(%d) entry(%d)\n", len, entry));

	/* Avoid a race... */
	bp->bmac_block->be_txd[entry].tx_flags = TXD_UPDATE;

	bp->tx_skbs[entry] = skb;
	bp->bmac_block->be_txd[entry].tx_addr = sbus_dvma_addr(skb->data);
	bp->bmac_block->be_txd[entry].tx_flags =
		(TXD_OWN | TXD_SOP | TXD_EOP | (len & TXD_LENGTH));
	dev->trans_start = jiffies;
	bp->tx_new = NEXT_TX(entry);

	/* Get it going. */
	bp->creg->ctrl = CREG_CTRL_TWAKEUP;

	if(TX_BUFFS_AVAIL(bp))
		dev->tbusy = 0;

	return 0;
}

#ifndef __sparc_v9__
static int sun4c_bigmac_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct bigmac *bp = (struct bigmac *) dev->priv;
	struct bigmac_buffers *bbufs = bp->sun4c_buffers;
	__u32 txbuf_dvma, bbufs_dvma = bp->s4c_buf_dvma;
	unsigned char *txbuf;
	int len, entry;

	if(dev->tbusy) {
		int tickssofar = jiffies - dev->trans_start;
	    
		if (tickssofar < 40) {
			return 1;
		} else {
			printk ("%s: transmit timed out, resetting\n", dev->name);
			bp->enet_stats.tx_errors++;
			bigmac_init(bp, 0);
			dev->tbusy = 0;
			dev->trans_start = jiffies;
			return 0;
		}
	}

	if(test_and_set_bit(0, (void *) &dev->tbusy) != 0) {
		printk("%s: Transmitter access conflict.\n", dev->name);
		return 1;
	}

	if(!SUN4C_TX_BUFFS_AVAIL(bp))
		return 1;

	len = skb->len;
	entry = bp->tx_new;

	txbuf = &bbufs->tx_buf[entry][0];
	txbuf_dvma = bbufs_dvma + bbuf_offset(tx_buf, entry);
	memcpy(txbuf, skb->data, len);

	/* Avoid a race... */
	bp->bmac_block->be_txd[entry].tx_flags = TXD_UPDATE;

	bp->bmac_block->be_txd[entry].tx_addr = txbuf_dvma;
	bp->bmac_block->be_txd[entry].tx_flags =
		(TXD_OWN | TXD_SOP | TXD_EOP | (len & TXD_LENGTH));
	bp->tx_new = NEXT_TX(entry);

	/* Get it going. */
	dev->trans_start = jiffies;
	bp->creg->ctrl = CREG_CTRL_TWAKEUP;

	dev_kfree_skb(skb);

	if(SUN4C_TX_BUFFS_AVAIL(bp))
		dev->tbusy = 0;

	return 0;
}
#endif

static struct enet_statistics *bigmac_get_stats(struct net_device *dev)
{
	struct bigmac *bp = (struct bigmac *) dev->priv;

	bigmac_get_counters(bp, bp->bregs);
	return &bp->enet_stats;
}

#define CRC_POLYNOMIAL_BE 0x04c11db7UL  /* Ethernet CRC, big endian */
#define CRC_POLYNOMIAL_LE 0xedb88320UL  /* Ethernet CRC, little endian */

static void bigmac_set_multicast(struct net_device *dev)
{
	struct bigmac *bp = (struct bigmac *) dev->priv;
	struct BIG_MAC_regs *bregs = bp->bregs;
	struct dev_mc_list *dmi = dev->mc_list;
	char *addrs;
	int i, j, bit, byte;
	u32 crc, poly = CRC_POLYNOMIAL_LE;

	/* Disable the receiver.  The bit self-clears when
	 * the operation is complete.
	 */
	bregs->rx_cfg &= ~(BIGMAC_RXCFG_ENABLE);
	while((bregs->rx_cfg & BIGMAC_RXCFG_ENABLE) != 0)
		udelay(20);

	if((dev->flags & IFF_ALLMULTI) || (dev->mc_count > 64)) {
		bregs->htable0 = 0xffff;
		bregs->htable1 = 0xffff;
		bregs->htable2 = 0xffff;
		bregs->htable3 = 0xffff;
	} else if(dev->flags & IFF_PROMISC) {
		bregs->rx_cfg |= BIGMAC_RXCFG_PMISC;
	} else {
		u16 hash_table[4];

		for(i = 0; i < 4; i++)
			hash_table[i] = 0;

		for(i = 0; i < dev->mc_count; i++) {
			addrs = dmi->dmi_addr;
			dmi = dmi->next;

			if(!(*addrs & 1))
				continue;

			crc = 0xffffffffU;
			for(byte = 0; byte < 6; byte++) {
				for(bit = *addrs++, j = 0; j < 8; j++, bit >>= 1) {
					int test;

					test = ((bit ^ crc) & 0x01);
					crc >>= 1;
					if(test)
						crc = crc ^ poly;
				}
			}
			crc >>= 26;
			hash_table[crc >> 4] |= 1 << (crc & 0xf);
		}
		bregs->htable0 = hash_table[0];
		bregs->htable1 = hash_table[1];
		bregs->htable2 = hash_table[2];
		bregs->htable3 = hash_table[3];
	}

	/* Re-enable the receiver. */
	bregs->rx_cfg |= BIGMAC_RXCFG_ENABLE;
}

static int __init bigmac_ether_init(struct net_device *dev, struct linux_sbus_device *qec_sdev)
{
	static unsigned version_printed = 0;
	struct bigmac *bp = 0;
	unsigned char bsizes, bsizes_more;
	int i, j, num_qranges, res = ENOMEM;
	struct linux_prom_ranges qranges[8];

	/* Get a new device struct for this interface. */
	dev = init_etherdev(0, sizeof(struct bigmac));

	if(version_printed++ == 0)
		printk(version);

	/* Report what we have found to the user. */
	printk("%s: BigMAC 100baseT Ethernet ", dev->name);
	dev->base_addr = (long) qec_sdev;
	for(i = 0; i < 6; i++)
		printk("%2.2x%c", dev->dev_addr[i] = idprom->id_ethaddr[i],
		       i == 5 ? ' ' : ':');
	printk("\n");

	/* Setup softc, with backpointers to QEC and BigMAC SBUS device structs. */
	bp = (struct bigmac *) dev->priv;
	bp->qec_sbus_dev = qec_sdev;
	bp->bigmac_sbus_dev = qec_sdev->child;

	/* All further failures we find return this. */
	res = ENODEV;

	/* Verify the registers we expect, are actually there. */
	if((bp->bigmac_sbus_dev->num_registers != 3) ||
	   (bp->qec_sbus_dev->num_registers != 2)) {
		printk("BIGMAC: Device does not have 2 and 3 regs, it has %d and %d.\n",
		       bp->qec_sbus_dev->num_registers,
		       bp->bigmac_sbus_dev->num_registers);
		printk("BIGMAC: Would you like that for here or to go?\n");
		goto fail_and_cleanup;
	}

	/* Fun with QEC ranges... */
	if(bp->bigmac_sbus_dev->ranges_applied == 0) {
		i = prom_getproperty(bp->qec_sbus_dev->prom_node, "ranges",
				     (char *)&qranges[0], sizeof(qranges));
		num_qranges = (i / sizeof(struct linux_prom_ranges));

		/* Now, apply all the ranges for the BigMAC. */
		for(j = 0; j < bp->bigmac_sbus_dev->num_registers; j++) {
			int k;

			for(k = 0; k < num_qranges; k++)
				if(bp->bigmac_sbus_dev->reg_addrs[j].which_io ==
				   qranges[k].ot_child_space)
					break;
			if(k >= num_qranges) {
				printk("BigMAC: Aieee, bogus QEC range for space %08x\n",
				       bp->bigmac_sbus_dev->reg_addrs[j].which_io);
				goto fail_and_cleanup;
			}
			bp->bigmac_sbus_dev->reg_addrs[j].which_io = qranges[k].ot_parent_space;
			bp->bigmac_sbus_dev->reg_addrs[j].phys_addr += qranges[k].ot_parent_base;
		}

		/* Next, apply SBUS ranges on top of what we just changed. */
		prom_apply_sbus_ranges(bp->bigmac_sbus_dev->my_bus,
				       &bp->bigmac_sbus_dev->reg_addrs[0],
				       bp->bigmac_sbus_dev->num_registers,
				       bp->bigmac_sbus_dev);
	}

	/* Apply SBUS ranges for the QEC parent. */
	prom_apply_sbus_ranges(bp->qec_sbus_dev->my_bus,
			       &bp->qec_sbus_dev->reg_addrs[0],
			       bp->qec_sbus_dev->num_registers,
			       bp->qec_sbus_dev);

	/* Map in QEC global control registers. */
	bp->gregs = sparc_alloc_io(bp->qec_sbus_dev->reg_addrs[0].phys_addr,
				   0,
				   sizeof(struct qe_globreg),
				   "BigMAC QEC Global Regs",
				   bp->qec_sbus_dev->reg_addrs[0].which_io,
				   0);
	if(!bp->gregs) {
		printk("BIGMAC: Cannot map QEC global registers.\n");
		goto fail_and_cleanup;
	}

	/* Make sure QEC is in BigMAC mode. */
	if((bp->gregs->ctrl & 0xf0000000) != GLOB_CTRL_BMODE) {
		printk("BigMAC: AIEEE, QEC is not in BigMAC mode!\n");
		goto fail_and_cleanup;
	}

	/* Reset the QEC. */
	if(qec_global_reset(bp->gregs))
		goto fail_and_cleanup;

	/* Get supported SBUS burst sizes. */
	bsizes = prom_getintdefault(bp->qec_sbus_dev->prom_node,
				    "burst-sizes",
				    0xff);

	bsizes_more = prom_getintdefault(bp->qec_sbus_dev->my_bus->prom_node,
					 "burst-sizes",
					 0xff);

	bsizes &= 0xff;
	if(bsizes_more != 0xff)
		bsizes &= bsizes_more;
	if(bsizes == 0xff || (bsizes & DMA_BURST16) == 0 ||
	   (bsizes & DMA_BURST32) == 0)
		bsizes = (DMA_BURST32 - 1);
	bp->bigmac_bursts = bsizes;

	/* Perform QEC initialization. */
	qec_init(bp);

	/* Map in the BigMAC channel registers. */
	bp->creg = sparc_alloc_io(bp->bigmac_sbus_dev->reg_addrs[0].phys_addr,
				  0,
				  sizeof(struct qe_creg),
				  "BigMAC QEC Channel Regs",
				  bp->bigmac_sbus_dev->reg_addrs[0].which_io,
				  0);
	if(!bp->creg) {
		printk("BIGMAC: Cannot map QEC channel registers.\n");
		goto fail_and_cleanup;
	}

	/* Map in the BigMAC control registers. */
	bp->bregs = sparc_alloc_io(bp->bigmac_sbus_dev->reg_addrs[1].phys_addr,
				   0,
				   sizeof(struct BIG_MAC_regs),
				   "BigMAC Primary Regs",
				   bp->bigmac_sbus_dev->reg_addrs[1].which_io,
				   0);
	if(!bp->bregs) {
		printk("BIGMAC: Cannot map BigMAC primary registers.\n");
		goto fail_and_cleanup;
	}

	/* Map in the BigMAC transceiver registers, this is how you poke at
	 * the BigMAC's PHY.
	 */
	bp->tregs = sparc_alloc_io(bp->bigmac_sbus_dev->reg_addrs[2].phys_addr,
				   0,
				   sizeof(struct bmac_tcvr),
				   "BigMAC Transceiver Regs",
				   bp->bigmac_sbus_dev->reg_addrs[2].which_io,
				   0);
	if(!bp->tregs) {
		printk("BIGMAC: Cannot map BigMAC transceiver registers.\n");
		goto fail_and_cleanup;
	}

	/* Stop the BigMAC. */
	bigmac_stop(bp);

	/* Allocate transmit/receive descriptor DVMA block. */
	bp->bmac_block = (struct bmac_init_block *)
		sparc_dvma_malloc(PAGE_SIZE, "BigMAC Init Block",
				  &bp->bblock_dvma);

	/* Get the board revision of this BigMAC. */
	bp->board_rev = prom_getintdefault(bp->bigmac_sbus_dev->prom_node,
					   "board-version", 1);

	/* If on sun4c, we use a static buffer pool, on sun4m we DMA directly
	 * in and out of sk_buffs instead for speed and one copy to userspace.
	 */
#ifndef __sparc_v9__
	if(sparc_cpu_model == sun4c)
		bp->sun4c_buffers = (struct bigmac_buffers *)
			sparc_dvma_malloc(sizeof(struct bigmac_buffers),
					  "BigMAC Bufs",
					  &bp->s4c_buf_dvma);
	else
#endif
		bp->sun4c_buffers = 0;

	/* Init auto-negotiation timer state. */
	init_timer(&bp->bigmac_timer);
	bp->timer_state = asleep;
	bp->timer_ticks = 0;

	/* Backlink to generic net device struct. */
	bp->dev = dev;

	/* Set links to our BigMAC open and close routines. */
	dev->open = &bigmac_open;
	dev->stop = &bigmac_close;

	/* Choose transmit routine based upon buffering scheme. */
#ifndef __sparc_v9__
	if(sparc_cpu_model == sun4c)
		dev->hard_start_xmit = &sun4c_bigmac_start_xmit;
	else
#endif
		dev->hard_start_xmit = &bigmac_start_xmit;

	/* Set links to BigMAC statistic and multi-cast loading code. */
	dev->get_stats = &bigmac_get_stats;
	dev->set_multicast_list = &bigmac_set_multicast;

	/* Finish net device registration. */
	dev->irq = bp->bigmac_sbus_dev->irqs[0];
	dev->dma = 0;
	ether_setup(dev);

#ifdef MODULE
	/* Put us into the list of instances attached for later module unloading. */
	bp->next_module = root_bigmac_dev;
	root_bigmac_dev = bp;
#endif
	return 0;

fail_and_cleanup:
	/* Something went wrong, undo whatever we did so far. */
	if(bp) {
		/* Free register mappings if any. */
		if(bp->gregs)
			sparc_free_io(bp->gregs, sizeof(struct qe_globreg));
		if(bp->creg)
			sparc_free_io(bp->creg, sizeof(struct qe_creg));
		if(bp->bregs)
			sparc_free_io(bp->bregs, sizeof(struct BIG_MAC_regs));
		if(bp->tregs)
			sparc_free_io(bp->tregs, sizeof(struct bmac_tcvr));

		/* XXX todo, bmac_block and sun4c_buffers */

		/* Free the BigMAC softc. */
		kfree(bp);
		dev->priv = 0;
	}
	return res;	/* Return error code. */
}

int __init bigmac_probe(void)
{
	struct net_device *dev = NULL;
	struct linux_sbus *bus;
	struct linux_sbus_device *sdev = 0;
	static int called = 0;
	int cards = 0, v;

	if(called)
		return ENODEV;
	called++;

	for_each_sbus(bus) {
		for_each_sbusdev(sdev, bus) {
			if(cards) dev = NULL;

			/* QEC can be the parent of either QuadEthernet or
			 * a BigMAC.  We want the latter.
			 */
			if(!strcmp(sdev->prom_name, "qec") && sdev->child &&
			   !strcmp(sdev->child->prom_name, "be")) {
				cards++;
				if((v = bigmac_ether_init(dev, sdev)))
				   return v;
			}
		}
	}
	if(!cards)
		return ENODEV;
	return 0;
}

#ifdef MODULE

int
init_module(void)
{
	root_bigmac_dev = NULL;
	return bigmac_probe();
}

void
cleanup_module(void)
{
	/* No need to check MOD_IN_USE, as sys_delete_module() checks. */
	while (root_bigmac_dev) {
		struct bigmac *bp = root_bigmac_dev;
		struct bigmac *bp_nxt = root_bigmac_dev->next_module;

		sparc_free_io(bp->gregs, sizeof(struct qe_globreg));
		sparc_free_io(bp->creg, sizeof(struct qe_creg));
		sparc_free_io(bp->bregs, sizeof(struct BIG_MAC_regs));
		sparc_free_io(bp->tregs, sizeof(struct bmac_tcvr));

		/* XXX todo, bmac_block and sun4c_buffers */

		unregister_netdev(bp->dev);
		kfree(bp->dev);
		root_bigmac_dev = bp_nxt;
	}
}

#endif /* MODULE */
