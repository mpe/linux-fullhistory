/*
 * Fast Ethernet Controller (FECC) driver for Motorola MPC8xx.
 * Copyright (c) 1997 Dan Malek (dmalek@jlc.net)
 *
 * This version of the driver is specific to the FADS implementation,
 * since the board contains control registers external to the processor
 * for the control of the LevelOne LXT970 transceiver.  The MPC860T manual
 * describes connections using the internal parallel port I/O, which
 * is basically all of Port D.
 *
 * Right now, I am very watseful with the buffers.  I allocate memory
 * pages and then divide them into 2K frame buffers.  This way I know I
 * have buffers large enough to hold one frame within one buffer descriptor.
 * Once I get this working, I will use 64 or 128 byte CPM buffers, which
 * will be much more memory efficient and will easily handle lots of
 * small packets.
 *
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include <asm/8xx_immap.h>
#include <asm/pgtable.h>
#include <asm/fads.h>
#include <asm/irq.h>
#include <asm/bitops.h>
#include <asm/uaccess.h>
#include "commproc.h"

/* The number of Tx and Rx buffers.  These are allocated from the page
 * pool.  The code may assume these are power of two, so it it best
 * to keep them that size.
 * We don't need to allocate pages for the transmitter.  We just use
 * the skbuffer directly.
 */
#if 1
#define FEC_ENET_RX_PAGES	4
#define FEC_ENET_RX_FRSIZE	2048
#define FEC_ENET_RX_FRPPG	(PAGE_SIZE / FEC_ENET_RX_FRSIZE)
#define RX_RING_SIZE		(FEC_ENET_RX_FRPPG * FEC_ENET_RX_PAGES)
#define TX_RING_SIZE		8	/* Must be power of two */
#define TX_RING_MOD_MASK	7	/*   for this to work */
#else
#define FEC_ENET_RX_PAGES	16
#define FEC_ENET_RX_FRSIZE	2048
#define FEC_ENET_RX_FRPPG	(PAGE_SIZE / FEC_ENET_RX_FRSIZE)
#define RX_RING_SIZE		(FEC_ENET_RX_FRPPG * FEC_ENET_RX_PAGES)
#define TX_RING_SIZE		16	/* Must be power of two */
#define TX_RING_MOD_MASK	15	/*   for this to work */
#endif

/* Interrupt events/masks.
*/
#define FEC_ENET_HBERR	((uint)0x80000000)	/* Heartbeat error */
#define FEC_ENET_BABR	((uint)0x40000000)	/* Babbling receiver */
#define FEC_ENET_BABT	((uint)0x20000000)	/* Babbling transmitter */
#define FEC_ENET_GRA	((uint)0x10000000)	/* Graceful stop complete */
#define FEC_ENET_TXF	((uint)0x08000000)	/* Full frame transmitted */
#define FEC_ENET_TXB	((uint)0x04000000)	/* A buffer was transmitted */
#define FEC_ENET_RXF	((uint)0x02000000)	/* Full frame received */
#define FEC_ENET_RXB	((uint)0x01000000)	/* A buffer was received */
#define FEC_ENET_MII	((uint)0x00800000)	/* MII interrupt */
#define FEC_ENET_EBERR	((uint)0x00400000)	/* SDMA bus error */

/* The FEC stores dest/src/type, data, and checksum for receive packets.
 */
#define PKT_MAXBUF_SIZE		1518
#define PKT_MINBUF_SIZE		64
#define PKT_MAXBLR_SIZE		1520

/* The FEC buffer descriptors track the ring buffers.  The rx_bd_base and
 * tx_bd_base always point to the base of the buffer descriptors.  The
 * cur_rx and cur_tx point to the currently available buffer.
 * The dirty_tx tracks the current buffer that is being sent by the
 * controller.  The cur_tx and dirty_tx are equal under both completely
 * empty and completely full conditions.  The empty/ready indicator in
 * the buffer descriptor determines the actual condition.
 */
struct fec_enet_private {
	/* The saved address of a sent-in-place packet/buffer, for skfree(). */
	struct	sk_buff* tx_skbuff[TX_RING_SIZE];
	ushort	skb_cur;
	ushort	skb_dirty;

	/* CPM dual port RAM relative addresses.
	*/
	cbd_t	*rx_bd_base;		/* Address of Rx and Tx buffers. */
	cbd_t	*tx_bd_base;
	cbd_t	*cur_rx, *cur_tx;		/* The next free ring entry */
	cbd_t	*dirty_tx;	/* The ring entries to be free()ed. */
	scc_t	*sccp;
	struct	net_device_stats stats;
	char	tx_full;
	unsigned long lock;
};

static int fec_enet_open(struct device *dev);
static int fec_enet_start_xmit(struct sk_buff *skb, struct device *dev);
static int fec_enet_rx(struct device *dev);
static void fec_enet_mii(struct device *dev);
static	void fec_enet_interrupt(int irq, void * dev_id, struct pt_regs * regs);
static int fec_enet_close(struct device *dev);
static struct net_device_stats *fec_enet_get_stats(struct device *dev);
static void set_multicast_list(struct device *dev);

static	ushort	my_enet_addr[] = { 0x0800, 0x3e26, 0x1559 };

/* MII processing.  We keep this as simple as possible.  Requests are
 * placed on the list (if there is room).  When the request is finished
 * by the MII, an optional function may be called.
 */
typedef struct mii_list {
	uint	mii_regval;
	void	(*mii_func)(uint val);
	struct	mii_list *mii_next;
} mii_list_t;

#define		NMII	10
mii_list_t	mii_cmds[NMII];
mii_list_t	*mii_free;
mii_list_t	*mii_head;
mii_list_t	*mii_tail;

static int	mii_queue(int request, void (*func)(int));

/* Make MII read/write commands for the FEC.
*/
#define mk_mii_read(REG)	(0x60020000 | ((REG & 0x1f) << 18))
#define mk_mii_write(REG, VAL)	(0x50020000 | ((REG & 0x1f) << 18) | \
						(VAL & 0xffff))

static int
fec_enet_open(struct device *dev)
{

	/* I should reset the ring buffers here, but I don't yet know
	 * a simple way to do that.
	 */

	dev->tbusy = 0;
	dev->interrupt = 0;
	dev->start = 1;

	return 0;					/* Always succeed */
}

static int
fec_enet_start_xmit(struct sk_buff *skb, struct device *dev)
{
	struct fec_enet_private *fep = (struct fec_enet_private *)dev->priv;
	volatile cbd_t	*bdp;
	unsigned long flags;

	/* Transmitter timeout, serious problems. */
	if (dev->tbusy) {
		int tickssofar = jiffies - dev->trans_start;
		if (tickssofar < 20)
			return 1;
		printk("%s: transmit timed out.\n", dev->name);
		fep->stats.tx_errors++;
#ifndef final_version
		{
			int	i;
			cbd_t	*bdp;
			printk(" Ring data dump: cur_tx %x%s cur_rx %x.\n",
				   fep->cur_tx, fep->tx_full ? " (full)" : "",
				   fep->cur_rx);
			bdp = fep->tx_bd_base;
			for (i = 0 ; i < TX_RING_SIZE; i++)
				printk("%04x %04x %08x\n",
					bdp->cbd_sc,
					bdp->cbd_datlen,
					bdp->cbd_bufaddr);
			bdp = fep->rx_bd_base;
			for (i = 0 ; i < RX_RING_SIZE; i++)
				printk("%04x %04x %08x\n",
					bdp->cbd_sc,
					bdp->cbd_datlen,
					bdp->cbd_bufaddr);
		}
#endif

		dev->tbusy=0;
		dev->trans_start = jiffies;

		return 0;
	}

	/* Block a timer-based transmit from overlapping.  This could better be
	   done with atomic_swap(1, dev->tbusy), but set_bit() works as well. */
	if (test_and_set_bit(0, (void*)&dev->tbusy) != 0) {
		printk("%s: Transmitter access conflict.\n", dev->name);
		return 1;
	}

	if (test_and_set_bit(0, (void*)&fep->lock) != 0) {
		printk("%s: tx queue lock!.\n", dev->name);
		/* don't clear dev->tbusy flag. */
		return 1;
	}

	/* Fill in a Tx ring entry */
	bdp = fep->cur_tx;

#ifndef final_version
	if (bdp->cbd_sc & BD_ENET_TX_READY) {
		/* Ooops.  All transmit buffers are full.  Bail out.
		 * This should not happen, since dev->tbusy should be set.
		 */
		printk("%s: tx queue full!.\n", dev->name);
		fep->lock = 0;
		return 1;
	}
#endif

	/* Clear all of the status flags.
	 */
	bdp->cbd_sc &= ~BD_ENET_TX_STATS;

	/* Set buffer length and buffer pointer.
	*/
	bdp->cbd_bufaddr = __pa(skb->data);
	bdp->cbd_datlen = skb->len;

	/* Save skb pointer.
	*/
	fep->tx_skbuff[fep->skb_cur] = skb;

	fep->stats.tx_bytes += skb->len;
	fep->skb_cur = (fep->skb_cur+1) & TX_RING_MOD_MASK;
	
	/* Push the data cache so the CPM does not get stale memory
	 * data.
	 */
	/*flush_dcache_range(skb->data, skb->data + skb->len);*/

	/* Send it on its way.  Tell CPM its ready, interrupt when done,
	 * its the last BD of the frame, and to put the CRC on the end.
	 */
	save_flags(flags);
	cli();

	bdp->cbd_sc |= (BD_ENET_TX_READY | BD_ENET_TX_INTR | BD_ENET_TX_LAST | BD_ENET_TX_TC);

	dev->trans_start = jiffies;
	(&(((immap_t *)IMAP_ADDR)->im_cpm.cp_fec))->fec_x_des_active = 0x01000000;

	/* If this was the last BD in the ring, start at the beginning again.
	*/
	if (bdp->cbd_sc & BD_ENET_TX_WRAP)
		bdp = fep->tx_bd_base;
	else
		bdp++;

	fep->lock = 0;
	if (bdp->cbd_sc & BD_ENET_TX_READY)
		fep->tx_full = 1;
	else
		dev->tbusy=0;
	restore_flags(flags);

	fep->cur_tx = (cbd_t *)bdp;

	return 0;
}

/* The interrupt handler.
 * This is called from the MPC core interrupt.
 */
static	void
fec_enet_interrupt(int irq, void * dev_id, struct pt_regs * regs)
{
	struct	device *dev = dev_id;
	struct	fec_enet_private *fep;
	volatile cbd_t	*bdp;
	volatile fec_t	*ep;
	uint	int_events;
	int c=0;

	fep = (struct fec_enet_private *)dev->priv;
	ep = &(((immap_t *)IMAP_ADDR)->im_cpm.cp_fec);
	if (dev->interrupt)
		printk("%s: Re-entering the interrupt handler.\n", dev->name);
	dev->interrupt = 1;

	/* Get the interrupt events that caused us to be here.
	*/
	while ((int_events = ep->fec_ievent) != 0) {
	ep->fec_ievent = int_events;
	if ((int_events &
		(FEC_ENET_HBERR | FEC_ENET_BABR |
			FEC_ENET_BABT | FEC_ENET_EBERR)) != 0)
				printk("FEC ERROR %x\n", int_events);

	/* Handle receive event in its own function.
	*/
	if (int_events & (FEC_ENET_RXF | FEC_ENET_RXB))
		fec_enet_rx(dev_id);

	/* Transmit OK, or non-fatal error.  Update the buffer descriptors.
	 * FEC handles all errors, we just discover them as part of the
	 * transmit process.
	 */
	if (int_events & (FEC_ENET_TXF | FEC_ENET_TXB)) {
	    bdp = fep->dirty_tx;
	    while ((bdp->cbd_sc&BD_ENET_TX_READY)==0) {
#if 1
		if (bdp==fep->cur_tx)
		    break;
#endif
		if (++c>1) {/*we go here when an it has been lost*/};


		if (bdp->cbd_sc & BD_ENET_TX_HB)	/* No heartbeat */
		    fep->stats.tx_heartbeat_errors++;
		if (bdp->cbd_sc & BD_ENET_TX_LC)	/* Late collision */
		    fep->stats.tx_window_errors++;
		if (bdp->cbd_sc & BD_ENET_TX_RL)	/* Retrans limit */
		    fep->stats.tx_aborted_errors++;
		if (bdp->cbd_sc & BD_ENET_TX_UN)	/* Underrun */
		    fep->stats.tx_fifo_errors++;
		if (bdp->cbd_sc & BD_ENET_TX_CSL)	/* Carrier lost */
		    fep->stats.tx_carrier_errors++;

		fep->stats.tx_errors++;
	    
		fep->stats.tx_packets++;
		
#ifndef final_version
		if (bdp->cbd_sc & BD_ENET_TX_READY)
		    printk("HEY! Enet xmit interrupt and TX_READY.\n");
#endif
		/* Deferred means some collisions occurred during transmit,
		 * but we eventually sent the packet OK.
		 */
		if (bdp->cbd_sc & BD_ENET_TX_DEF)
		    fep->stats.collisions++;
	    
		/* Free the sk buffer associated with this last transmit.
		 */
		dev_kfree_skb(fep->tx_skbuff[fep->skb_dirty]/*, FREE_WRITE*/);
		fep->skb_dirty = (fep->skb_dirty + 1) & TX_RING_MOD_MASK;
	    
		/* Update pointer to next buffer descriptor to be transmitted.
		 */
		if (bdp->cbd_sc & BD_ENET_TX_WRAP)
		    bdp = fep->tx_bd_base;
		else
		    bdp++;
	    
		/* Since we have freed up a buffer, the ring is no longer
		 * full.
		 */
		if (fep->tx_full && dev->tbusy) {
		    fep->tx_full = 0;
		    dev->tbusy = 0;
		    mark_bh(NET_BH);
		}

		fep->dirty_tx = (cbd_t *)bdp;
#if 0
		if (bdp==fep->cur_tx)
		    break;
#endif
	    }/*while (bdp->cbd_sc&BD_ENET_TX_READY)==0*/
	 } /* if tx events */

	if (int_events & FEC_ENET_MII)
		fec_enet_mii(dev_id);
	
	} /* while any events */

	dev->interrupt = 0;

	return;
}

/* During a receive, the cur_rx points to the current incoming buffer.
 * When we update through the ring, if the next incoming buffer has
 * not been given to the system, we just set the empty indicator,
 * effectively tossing the packet.
 */
static int
fec_enet_rx(struct device *dev)
{
	struct	fec_enet_private *fep;
	volatile cbd_t *bdp;
	struct	sk_buff	*skb;
	ushort	pkt_len;
	volatile fec_t	*ep;

	fep = (struct fec_enet_private *)dev->priv;
	ep = &(((immap_t *)IMAP_ADDR)->im_cpm.cp_fec);

	/* First, grab all of the stats for the incoming packet.
	 * These get messed up if we get called due to a busy condition.
	 */
	bdp = fep->cur_rx;

for (;;) {
	if (bdp->cbd_sc & BD_ENET_RX_EMPTY)
		break;
		
#ifndef final_version
	/* Since we have allocated space to hold a complete frame,
	 * the last indicator should be set.
	 */
	if ((bdp->cbd_sc & BD_ENET_RX_LAST) == 0)
		printk("FEC ENET: rcv is not +last\n");
#endif

	/* Frame too long or too short.
	*/
	if (bdp->cbd_sc & (BD_ENET_RX_LG | BD_ENET_RX_SH))
		fep->stats.rx_length_errors++;
	if (bdp->cbd_sc & BD_ENET_RX_NO)	/* Frame alignment */
		fep->stats.rx_frame_errors++;
	if (bdp->cbd_sc & BD_ENET_RX_CR)	/* CRC Error */
		fep->stats.rx_crc_errors++;
	if (bdp->cbd_sc & BD_ENET_RX_OV)	/* FIFO overrun */
		fep->stats.rx_crc_errors++;

	/* Report late collisions as a frame error.
	 * On this error, the BD is closed, but we don't know what we
	 * have in the buffer.  So, just drop this frame on the floor.
	 */
	if (bdp->cbd_sc & BD_ENET_RX_CL) {
		fep->stats.rx_frame_errors++;
	}
	else {

		/* Process the incoming frame.
		*/
		fep->stats.rx_packets++;
		pkt_len = bdp->cbd_datlen;
		fep->stats.rx_bytes += pkt_len;

		/* This does 16 byte alignment, exactly what we need.
		*/
		skb = dev_alloc_skb(pkt_len);

		if (skb == NULL) {
			printk("%s: Memory squeeze, dropping packet.\n", dev->name);
			fep->stats.rx_dropped++;
		}
		else {
			skb->dev = dev;
			skb_put(skb,pkt_len);	/* Make room */
			eth_copy_and_sum(skb,
				(unsigned char *)__va(bdp->cbd_bufaddr),
				pkt_len, 0);
			skb->protocol=eth_type_trans(skb,dev);
			netif_rx(skb);
		}
	}

	/* Clear the status flags for this buffer.
	*/
	bdp->cbd_sc &= ~BD_ENET_RX_STATS;

	/* Mark the buffer empty.
	*/
	bdp->cbd_sc |= BD_ENET_RX_EMPTY;

	/* Update BD pointer to next entry.
	*/
	if (bdp->cbd_sc & BD_ENET_RX_WRAP)
		bdp = fep->rx_bd_base;
	else
		bdp++;
	
#if 1
	/* Doing this here will keep the FEC running while we process
	 * incoming frames.  On a heavily loaded network, we should be
	 * able to keep up at the expense of system resources.
	 */
	ep->fec_r_des_active = 0x01000000;
#endif
   }
	fep->cur_rx = (cbd_t *)bdp;

#if 0
	/* Doing this here will allow us to process all frames in the
	 * ring before the FEC is allowed to put more there.  On a heavily
	 * loaded network, some frames may be lost.  Unfortunately, this
	 * increases the interrupt overhead since we can potentially work
	 * our way back to the interrupt return only to come right back
	 * here.
	 */
	ep->fec_r_des_active = 0x01000000;
#endif

	return 0;
}

static void
fec_enet_mii(struct device *dev)
{
	struct	fec_enet_private *fep;
	volatile fec_t	*ep;
	mii_list_t	*mip;
	uint		mii_reg;

	fep = (struct fec_enet_private *)dev->priv;
	ep = &(((immap_t *)IMAP_ADDR)->im_cpm.cp_fec);
	mii_reg = ep->fec_mii_data;
	
	if ((mip = mii_head) == NULL) {
		printk("MII and no head!\n");
		return;
	}

	if (mip->mii_func != NULL)
		(*(mip->mii_func))(mii_reg);

	mii_head = mip->mii_next;
	mip->mii_next = mii_free;
	mii_free = mip;

	if ((mip = mii_head) != NULL)
		ep->fec_mii_data = mip->mii_regval;
}

static int
mii_queue(int regval, void (*func)(int))
{
	unsigned long	flags;
	mii_list_t	*mip;
	int		retval;

	retval = 0;

	save_flags(flags);
	cli();

	if ((mip = mii_free) != NULL) {
		mii_free = mip->mii_next;
		mip->mii_regval = regval;
		mip->mii_func = func;
		mip->mii_next = NULL;
		if (mii_head) {
			mii_tail->mii_next = mip;
			mii_tail = mip;
		}
		else {
			mii_head = mii_tail = mip;
			(&(((immap_t *)IMAP_ADDR)->im_cpm.cp_fec))->fec_mii_data = regval;
		}
	}
	else {
		retval = 1;
	}

	restore_flags(flags);

	return(retval);
}

static void
mii_status(uint mii_reg)
{
	if (((mii_reg >> 18) & 0x1f) == 1) {
		/* status register.
		*/
		printk("fec: ");
		if (mii_reg & 0x0004)
			printk("link up");
		else
			printk("link down");

		if (mii_reg & 0x0010)
			printk(",remote fault");
		if (mii_reg & 0x0020)
			printk(",auto complete");
		printk("\n");
	}
	if (((mii_reg >> 18) & 0x1f) == 0x14) {
		/* Extended chip status register.
		*/
		printk("fec: ");
		if (mii_reg & 0x0800)
			printk("100 Mbps");
		else
			printk("10 Mbps");

		if (mii_reg & 0x1000)
			printk(", Full-Duplex\n");
		else
			printk(", Half-Duplex\n");
	}
}

static	void
mii_startup_cmds(void)
{

	/* Read status registers to clear any pending interrupt.
	*/
	mii_queue(mk_mii_read(1), mii_status);
	mii_queue(mk_mii_read(18), mii_status);

	/* Read extended chip status register.
	*/
	mii_queue(mk_mii_read(0x14), mii_status);

	/* Enable Link status change interrupts.
	mii_queue(mk_mii_write(0x11, 0x0002), NULL);
	*/
}

/* This supports the mii_link interrupt below.
 * We should get called three times.  Once for register 1, once for
 * register 18, and once for register 20.
 */
static	uint mii_saved_reg1;

static void
mii_relink(uint mii_reg)
{
	if (((mii_reg >> 18) & 0x1f) == 1) {
		/* Just save the status register and get out.
		*/
		mii_saved_reg1 = mii_reg;
		return;
	}
	if (((mii_reg >> 18) & 0x1f) == 18) {
		/* Not much here, but has to be read to clear the
		 * interrupt condition.
		 */
		if ((mii_reg & 0x8000) == 0)
			printk("fec: re-link and no IRQ?\n");
		if ((mii_reg & 0x4000) == 0)
			printk("fec: no PHY power?\n");
	}
	if (((mii_reg >> 18) & 0x1f) == 20) {
		/* Extended chip status register.
		 * OK, now we have it all, so figure out what is going on.
		 */
		printk("fec: ");
		if (mii_saved_reg1 & 0x0004)
			printk("link up");
		else
			printk("link down");

		if (mii_saved_reg1 & 0x0010)
			printk(", remote fault");
		if (mii_saved_reg1 & 0x0020)
			printk(", auto complete");

		if (mii_reg & 0x0800)
			printk(", 100 Mbps");
		else
			printk(", 10 Mbps");

		if (mii_reg & 0x1000)
			printk(", Full-Duplex\n");
		else
			printk(", Half-Duplex\n");
	}
}

/* This interrupt occurs when the LTX970 detects a link change.
*/
static	void
mii_link_interrupt(int irq, void * dev_id, struct pt_regs * regs)
{
	struct	device *dev = dev_id;
	struct	fec_enet_private *fep;
	volatile fec_t	*ep;

	fep = (struct fec_enet_private *)dev->priv;
	ep = &(((immap_t *)IMAP_ADDR)->im_cpm.cp_fec);

	/* We need to sequentially read registers 1 and 18 to clear
	 * the interrupt.  We don't need to do that here because this
	 * is an edge triggered interrupt that has already been acknowledged
	 * by the top level handler.  We also read the extended status
	 * register 20.  We just queue the commands and let them happen
	 * as part of the "normal" processing.
	 */
	mii_queue(mk_mii_read(1), mii_relink);
	mii_queue(mk_mii_read(18), mii_relink);
	mii_queue(mk_mii_read(20), mii_relink);
}

static int
fec_enet_close(struct device *dev)
{
	/* Don't know what to do yet.
	*/

	return 0;
}

static struct net_device_stats *fec_enet_get_stats(struct device *dev)
{
	struct fec_enet_private *fep = (struct fec_enet_private *)dev->priv;

	return &fep->stats;
}

/* Set or clear the multicast filter for this adaptor.
 * Skeleton taken from sunlance driver.
 * The CPM Ethernet implementation allows Multicast as well as individual
 * MAC address filtering.  Some of the drivers check to make sure it is
 * a group multicast address, and discard those that are not.  I guess I
 * will do the same for now, but just remove the test if you want
 * individual filtering as well (do the upper net layers want or support
 * this kind of feature?).
 */

static void set_multicast_list(struct device *dev)
{
	struct	fec_enet_private *fep;
	struct	dev_mc_list *dmi;
	u_char	*mcptr, *tdptr;
	volatile fec_t *ep;
	int	i, j;

	fep = (struct fec_enet_private *)dev->priv;
	ep = &(((immap_t *)IMAP_ADDR)->im_cpm.cp_fec);

	if (dev->flags&IFF_PROMISC) {
	  
		/* Log any net taps. */
		printk("%s: Promiscuous mode enabled.\n", dev->name);
		ep->fec_r_cntrl |= 0x0008;
	} else {

		ep->fec_r_cntrl &= ~0x0008;

		if (dev->flags & IFF_ALLMULTI) {
			/* Catch all multicast addresses, so set the
			 * filter to all 1's.
			 */
			ep->fec_hash_table_high = 0xffffffff;
			ep->fec_hash_table_low = 0xffffffff;
		}
#if 0
		else {
			/* Clear filter and add the addresses in the list.
			*/
			ep->sen_gaddr1 = 0;
			ep->sen_gaddr2 = 0;
			ep->sen_gaddr3 = 0;
			ep->sen_gaddr4 = 0;

			dmi = dev->mc_list;

			for (i=0; i<dev->mc_count; i++) {
				
				/* Only support group multicast for now.
				*/
				if (!(dmi->dmi_addr[0] & 1))
					continue;

				/* The address in dmi_addr is LSB first,
				 * and taddr is MSB first.  We have to
				 * copy bytes MSB first from dmi_addr.
				 */
				mcptr = (u_char *)dmi->dmi_addr + 5;
				tdptr = (u_char *)&ep->sen_taddrh;
				for (j=0; j<6; j++)
					*tdptr++ = *mcptr--;

				/* Ask CPM to run CRC and set bit in
				 * filter mask.
				 */
				cpmp->cp_cpcr = mk_cr_cmd(CPM_CR_CH_SCC1, CPM_CR_SET_GADDR) | CPM_CR_FLG;
				/* this delay is necessary here -- Cort */
				udelay(10);
				while (cpmp->cp_cpcr & CPM_CR_FLG);
			}
		}
#endif
	}
}

/* Initialize the FECC Ethernet on 860T.
 */
__initfunc(int m8xx_enet_init(void))
{
	struct device *dev;
	struct fec_enet_private *fep;
	int i, j;
	unsigned char	*eap;
	unsigned long	mem_addr;
	pte_t		*pte;
	volatile	cbd_t	*bdp;
	cbd_t		*cbd_base;
	volatile	immap_t	*immap;
	volatile	fec_t	*fecp;
	unsigned char	rtc_save_cfg, rtc_val;

	immap = (immap_t *)IMAP_ADDR;	/* pointer to internal registers */

	/* Allocate some private information.
	*/
	fep = (struct fec_enet_private *)kmalloc(sizeof(*fep), GFP_KERNEL);
	__clear_user(fep,sizeof(*fep));

	/* Create an Ethernet device instance.
	*/
	dev = init_etherdev(0, 0);

	fecp = &(immap->im_cpm.cp_fec);

	/* Whack a reset.  We should wait for this.
	*/
	fecp->fec_ecntrl = 1;
	udelay(10);

	/* Enable interrupts we wish to service.
	*/
	fecp->fec_imask = (FEC_ENET_TXF | FEC_ENET_TXB |
				FEC_ENET_RXF | FEC_ENET_RXB | FEC_ENET_MII);

	/* Clear any outstanding interrupt.
	*/
	fecp->fec_ievent = 0xffc0;

	fecp->fec_ivec = (FEC_INTERRUPT/2) << 29;

	/* Set station address.
	*/
	fecp->fec_addr_low = (my_enet_addr[0] << 16) | my_enet_addr[1];
	fecp->fec_addr_high = my_enet_addr[2];

	eap = (unsigned char *)&my_enet_addr[0];
	for (i=0; i<6; i++)
		dev->dev_addr[i] = *eap++;

	/* Reset all multicast.
	*/
	fecp->fec_hash_table_high = 0;
	fecp->fec_hash_table_low = 0;

	/* Set maximum receive buffer size.
	*/
	fecp->fec_r_buff_size = PKT_MAXBLR_SIZE;
	fecp->fec_r_hash = PKT_MAXBUF_SIZE;

	/* Allocate memory for buffer descriptors.
	*/
	if (((RX_RING_SIZE + TX_RING_SIZE) * sizeof(cbd_t)) > PAGE_SIZE) {
		printk("FECC init error.  Need more space.\n");
		printk("FECC initialization failed.\n");
		return 1;
	}
	mem_addr = __get_free_page(GFP_KERNEL);
	cbd_base = (cbd_t *)mem_addr;

	/* Make it uncached.
	*/
	pte = va_to_pte(&init_task, (int)mem_addr);
	pte_val(*pte) |= _PAGE_NO_CACHE;
	flush_tlb_page(current->mm->mmap, mem_addr);

	/* Set receive and transmit descriptor base.
	*/
	fecp->fec_r_des_start = __pa(mem_addr);
	fep->rx_bd_base = cbd_base;
	fecp->fec_x_des_start = __pa((unsigned long)(cbd_base + RX_RING_SIZE));
	fep->tx_bd_base = cbd_base + RX_RING_SIZE;

	fep->dirty_tx = fep->cur_tx = fep->tx_bd_base;
	fep->cur_rx = fep->rx_bd_base;

	fep->skb_cur = fep->skb_dirty = 0;

	/* Initialize the receive buffer descriptors.
	*/
	bdp = fep->rx_bd_base;
	for (i=0; i<FEC_ENET_RX_PAGES; i++) {

		/* Allocate a page.
		*/
		mem_addr = __get_free_page(GFP_KERNEL);

		/* Make it uncached.
		*/
		pte = va_to_pte(&init_task, mem_addr);
		pte_val(*pte) |= _PAGE_NO_CACHE;
		flush_tlb_page(current->mm->mmap, mem_addr);

		/* Initialize the BD for every fragment in the page.
		*/
		for (j=0; j<FEC_ENET_RX_FRPPG; j++) {
			bdp->cbd_sc = BD_ENET_RX_EMPTY;
			bdp->cbd_bufaddr = __pa(mem_addr);
			mem_addr += FEC_ENET_RX_FRSIZE;
			bdp++;
		}
	}

	/* Set the last buffer to wrap.
	*/
	bdp--;
	bdp->cbd_sc |= BD_SC_WRAP;

	/* ...and the same for transmmit.
	*/
	bdp = fep->tx_bd_base;
	for (i=0; i<TX_RING_SIZE; i++) {

		/* Initialize the BD for every fragment in the page.
		*/
		bdp->cbd_sc = 0;
		bdp->cbd_bufaddr = 0;
		bdp++;
	}

	/* Set the last buffer to wrap.
	*/
	bdp--;
	bdp->cbd_sc |= BD_SC_WRAP;

	/* Enable MII mode, half-duplex until we know better..
	*/
	fecp->fec_r_cntrl = 0x0c;
	fecp->fec_x_cntrl = 0x00;

	/* Enable big endian and don't care about SDMA FC.
	*/
	fecp->fec_fun_code = 0x78000000;

	/* Set MII speed (50 MHz core).
	*/
	fecp->fec_mii_speed = 0x14;

	/* Configure all of port D for MII.
	*/
	immap->im_ioport.iop_pdpar = 0x1fff;
	immap->im_ioport.iop_pddir = 0x1c58;

	/* Install our interrupt handlers.  The 860T FADS board uses
	 * IRQ2 for the MII interrupt.
	 */
	if (request_irq(FEC_INTERRUPT, fec_enet_interrupt, 0, "fec", dev) != 0)
		panic("Could not allocate FEC IRQ!");
	if (request_irq(SIU_IRQ2, mii_link_interrupt, 0, "mii", dev) != 0)
		panic("Could not allocate MII IRQ!");

	dev->base_addr = (unsigned long)fecp;
	dev->priv = fep;
	dev->name = "fec";

	/* The FEC Ethernet specific entries in the device structure. */
	dev->open = fec_enet_open;
	dev->hard_start_xmit = fec_enet_start_xmit;
	dev->stop = fec_enet_close;
	dev->get_stats = fec_enet_get_stats;
	dev->set_multicast_list = set_multicast_list;

	/* And last, enable the transmit and receive processing.
	*/
	fecp->fec_ecntrl = 2;
	fecp->fec_r_des_active = 0x01000000;

	printk("FEC ENET Version 0.1, ");
	for (i=0; i<5; i++)
		printk("%02x:", dev->dev_addr[i]);
	printk("%02x\n", dev->dev_addr[5]);

	for (i=0; i<NMII-1; i++)
		mii_cmds[i].mii_next = &mii_cmds[i+1];
	mii_free = mii_cmds;

	mii_startup_cmds();

	return 0;
}

