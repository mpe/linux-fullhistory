/*
 * $Id: enet.c,v 1.8 1998/11/15 19:58:07 cort Exp $
 * Ethernet driver for Motorola MPC8xx.
 * Copyright (c) 1997 Dan Malek (dmalek@jlc.net)
 *
 * I copied the basic skeleton from the lance driver, because I did not
 * know how to write the Linux driver, but I did know how the LANCE worked.
 * This version of the driver is specific to the MBX implementation,
 * since the board contains control registers external to the processor
 * for the control of the MC68160 SIA/transceiver.  The MPC860 manual
 * describes connections using the internal parallel port I/O.
 *
 * The MBX860 uses the CPM SCC1 serial port for the Ethernet interface.
 * Buffer descriptors are kept in the CPM dual port RAM, and the frame
 * buffers are in the host memory.
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
#include <asm/mbx.h>
#include <asm/bitops.h>
#include <asm/uaccess.h>
#include "commproc.h"

/*
 *				Theory of Operation
 *
 * The MPC8xx CPM performs the Ethernet processing on SCC1.  It can use
 * an aribtrary number of buffers on byte boundaries, but must have at
 * least two receive buffers to prevent constand overrun conditions.
 *
 * The buffer descriptors are allocated from the CPM dual port memory
 * with the data buffers allocated from host memory, just like all other
 * serial communication protocols.  The host memory buffers are allocated
 * from the free page pool, and then divided into smaller receive and
 * transmit buffers.  The size of the buffers should be a power of two,
 * since that nicely divides the page.  This creates a ring buffer
 * structure similar to the LANCE and other controllers.
 *
 * Like the LANCE driver:
 * The driver runs as two independent, single-threaded flows of control.  One
 * is the send-packet routine, which enforces single-threaded use by the
 * dev->tbusy flag.  The other thread is the interrupt handler, which is single
 * threaded by the hardware and other software.
 *
 * The send packet thread has partial control over the Tx ring and 'dev->tbusy'
 * flag.  It sets the tbusy flag whenever it's queuing a Tx packet. If the next
 * queue slot is empty, it clears the tbusy flag when finished otherwise it sets
 * the 'lp->tx_full' flag.
 *
 * The MBX has a control register external to the MPC8xx that has some
 * control of the Ethernet interface.  Control Register 1 has the
 * following format:
 *	bit 0 - Set to enable Ethernet transceiver
 *	bit 1 - Set to enable Ethernet internal loopback
 *	bit 2 - Set to auto select AUI or TP port
 *	bit 3 - if bit 2 is 0, set to select TP port
 *	bit 4 - Set to disable full duplex (loopback)
 *	bit 5 - Set to disable XCVR collision test
 *	bit 6, 7 - Used for RS-232 control.
 *
 * EPPC-Bug sets this register to 0x98 for normal Ethernet operation,
 * so we should not have to touch it.
 *
 * The following I/O is used by the MBX implementation of the MPC8xx to
 * the MC68160 transceiver.  It DOES NOT exactly follow the cookbook
 * example from the MPC860 manual.
 *	Port A, 15 - SCC1 Ethernet Rx
 *	Port A, 14 - SCC1 Ethernet Tx
 *	Port A, 6 (CLK2) - SCC1 Ethernet Tx Clk
 *	Port A, 4 (CLK4) - SCC1 Ethernet Rx Clk
 *	Port C, 15 - SCC1 Ethernet Tx Enable
 *	Port C, 11 - SCC1 Ethernet Collision
 *	Port C, 10 - SCC1 Ethernet Rx Enable
 */

/* The number of Tx and Rx buffers.  These are allocated from the page
 * pool.  The code may assume these are power of two, so it is best
 * to keep them that size.
 * We don't need to allocate pages for the transmitter.  We just use
 * the skbuffer directly.
 */
#define CPM_ENET_RX_PAGES	4
#define CPM_ENET_RX_FRSIZE	2048
#define CPM_ENET_RX_FRPPG	(PAGE_SIZE / CPM_ENET_RX_FRSIZE)
#define RX_RING_SIZE		(CPM_ENET_RX_FRPPG * CPM_ENET_RX_PAGES)
#define TX_RING_SIZE		8	/* Must be power of two */
#define TX_RING_MOD_MASK	7	/*   for this to work */

/* The CPM stores dest/src/type, data, and checksum for receive packets.
 */
#define PKT_MAXBUF_SIZE		1518
#define PKT_MINBUF_SIZE		64
#define PKT_MAXBLR_SIZE		1520

/* The CPM buffer descriptors track the ring buffers.  The rx_bd_base and
 * tx_bd_base always point to the base of the buffer descriptors.  The
 * cur_rx and cur_tx point to the currently available buffer.
 * The dirty_tx tracks the current buffer that is being sent by the
 * controller.  The cur_tx and dirty_tx are equal under both completely
 * empty and completely full conditions.  The empty/ready indicator in
 * the buffer descriptor determines the actual condition.
 */
struct cpm_enet_private {
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

static int cpm_enet_open(struct device *dev);
static int cpm_enet_start_xmit(struct sk_buff *skb, struct device *dev);
static int cpm_enet_rx(struct device *dev);
static void cpm_enet_interrupt(void *dev_id);
static int cpm_enet_close(struct device *dev);
static struct net_device_stats *cpm_enet_get_stats(struct device *dev);
static void set_multicast_list(struct device *dev);

/* GET THIS FROM THE VPD!!!!
*/
/*static	ushort	my_enet_addr[] = { 0x0800, 0x3e26, 0x1559 };*/

static int
cpm_enet_open(struct device *dev)
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
cpm_enet_start_xmit(struct sk_buff *skb, struct device *dev)
{
	struct cpm_enet_private *cep = (struct cpm_enet_private *)dev->priv;
	volatile cbd_t	*bdp;
	unsigned long flags;

	/* Transmitter timeout, serious problems. */
	if (dev->tbusy) {
		int tickssofar = jiffies - dev->trans_start;
		if (tickssofar < 20)
			return 1;
		printk("%s: transmit timed out.\n", dev->name);
		cep->stats.tx_errors++;
#ifndef final_version
		{
			int	i;
			cbd_t	*bdp;
			printk(" Ring data dump: cur_tx %x%s cur_rx %x.\n",
				   cep->cur_tx, cep->tx_full ? " (full)" : "",
				   cep->cur_rx);
			bdp = cep->tx_bd_base;
			for (i = 0 ; i < TX_RING_SIZE; i++)
				printk("%04x %04x %08x\n",
					bdp->cbd_sc,
					bdp->cbd_datlen,
					bdp->cbd_bufaddr);
			bdp = cep->rx_bd_base;
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

	if (test_and_set_bit(0, (void*)&cep->lock) != 0) {
		printk("%s: tx queue lock!.\n", dev->name);
		/* don't clear dev->tbusy flag. */
		return 1;
	}

	/* Fill in a Tx ring entry */
	bdp = cep->cur_tx;

#ifndef final_version
	if (bdp->cbd_sc & BD_ENET_TX_READY) {
		/* Ooops.  All transmit buffers are full.  Bail out.
		 * This should not happen, since dev->tbusy should be set.
		 */
		printk("%s: tx queue full!.\n", dev->name);
		cep->lock = 0;
		return 1;
	}
#endif

	/* Clear all of the status flags.
	 */
	bdp->cbd_sc &= ~BD_ENET_TX_STATS;

	/* If the frame is short, tell CPM to pad it.
	*/
	if (skb->len <= ETH_ZLEN)
		bdp->cbd_sc |= BD_ENET_TX_PAD;
	else
		bdp->cbd_sc &= ~BD_ENET_TX_PAD;

	/* Set buffer length and buffer pointer.
	*/
	bdp->cbd_datlen = skb->len;
	bdp->cbd_bufaddr = __pa(skb->data);

	/* Save skb pointer.
	*/
	cep->tx_skbuff[cep->skb_cur] = skb;

	cep->stats.tx_bytes += skb->len;
	cep->skb_cur = (cep->skb_cur+1) & TX_RING_MOD_MASK;
	
	/* Push the data cache so the CPM does not get stale memory
	 * data.
	 */
	/*flush_dcache_range(skb->data, skb->data + skb->len);*/

	/* Send it on its way.  Tell CPM its ready, interrupt when done,
	 * its the last BD of the frame, and to put the CRC on the end.
	 */
	bdp->cbd_sc |= (BD_ENET_TX_READY | BD_ENET_TX_INTR | BD_ENET_TX_LAST | BD_ENET_TX_TC);

	dev->trans_start = jiffies;

	/* If this was the last BD in the ring, start at the beginning again.
	*/
	if (bdp->cbd_sc & BD_ENET_TX_WRAP)
		bdp = cep->tx_bd_base;
	else
		bdp++;

	save_flags(flags);
	cli();
	cep->lock = 0;
	if (bdp->cbd_sc & BD_ENET_TX_READY)
		cep->tx_full = 1;
	else
		dev->tbusy=0;
	restore_flags(flags);

	cep->cur_tx = (cbd_t *)bdp;

	return 0;
}

/* The interrupt handler.
 * This is called from the CPM handler, not the MPC core interrupt.
 */
static void
cpm_enet_interrupt(void *dev_id)
{
	struct	device *dev = dev_id;
	struct	cpm_enet_private *cep;
	volatile cbd_t	*bdp;
	ushort	int_events;
	int	must_restart;

	cep = (struct cpm_enet_private *)dev->priv;
	if (dev->interrupt)
		printk("%s: Re-entering the interrupt handler.\n", dev->name);

	dev->interrupt = 1;

	/* Get the interrupt events that caused us to be here.
	*/
	int_events = cep->sccp->scc_scce;
	must_restart = 0;

	/* Handle receive event in its own function.
	*/
	if (int_events & SCCE_ENET_RXF)
		cpm_enet_rx(dev_id);

	/* Check for a transmit error.  The manual is a little unclear
	 * about this, so the debug code until I get it figured out.  It
	 * appears that if TXE is set, then TXB is not set.  However,
	 * if carrier sense is lost during frame transmission, the TXE
	 * bit is set, "and continues the buffer transmission normally."
	 * I don't know if "normally" implies TXB is set when the buffer
	 * descriptor is closed.....trial and error :-).
	 */
	if (int_events & SCCE_ENET_TXE) {

		/* Transmission errors.
		*/
		bdp = cep->dirty_tx;
#ifndef final_version
		printk("CPM ENET xmit error %x\n", bdp->cbd_sc);
		if (bdp->cbd_sc & BD_ENET_TX_READY)
			printk("HEY! Enet xmit interrupt and TX_READY.\n");
#endif
		if (bdp->cbd_sc & BD_ENET_TX_HB)	/* No heartbeat */
			cep->stats.tx_heartbeat_errors++;
		if (bdp->cbd_sc & BD_ENET_TX_LC)	/* Late collision */
			cep->stats.tx_window_errors++;
		if (bdp->cbd_sc & BD_ENET_TX_RL)	/* Retrans limit */
			cep->stats.tx_aborted_errors++;
		if (bdp->cbd_sc & BD_ENET_TX_UN)	/* Underrun */
			cep->stats.tx_fifo_errors++;
		if (bdp->cbd_sc & BD_ENET_TX_CSL)	/* Carrier lost */
			cep->stats.tx_carrier_errors++;

		cep->stats.tx_errors++;

		/* No heartbeat or Lost carrier are not really bad errors.
		 * The others require a restart transmit command.
		 */
		if (bdp->cbd_sc &
		    (BD_ENET_TX_LC | BD_ENET_TX_RL | BD_ENET_TX_UN))
			must_restart = 1;
	}

	/* Transmit OK, or non-fatal error.  Update the buffer descriptors.
	*/
	if (int_events & (SCCE_ENET_TXE | SCCE_ENET_TXB)) {
		cep->stats.tx_packets++;
		bdp = cep->dirty_tx;
#ifndef final_version
		if (bdp->cbd_sc & BD_ENET_TX_READY)
			printk("HEY! Enet xmit interrupt and TX_READY.\n");
#endif
		/* Deferred means some collisions occurred during transmit,
		 * but we eventually sent the packet OK.
		 */
		if (bdp->cbd_sc & BD_ENET_TX_DEF)
			cep->stats.collisions++;

		/* Free the sk buffer associated with this last transmit.
		*/
		dev_kfree_skb(cep->tx_skbuff[cep->skb_dirty]/*, FREE_WRITE*/);
		cep->skb_dirty = (cep->skb_dirty + 1) & TX_RING_MOD_MASK;

		/* Update pointer to next buffer descriptor to be transmitted.
		*/
		if (bdp->cbd_sc & BD_ENET_TX_WRAP)
			bdp = cep->tx_bd_base;
		else
			bdp++;

		/* I don't know if we can be held off from processing these
		 * interrupts for more than one frame time.  I really hope
		 * not.  In such a case, we would now want to check the
		 * currently available BD (cur_tx) and determine if any
		 * buffers between the dirty_tx and cur_tx have also been
		 * sent.  We would want to process anything in between that
		 * does not have BD_ENET_TX_READY set.
		 */

		/* Since we have freed up a buffer, the ring is no longer
		 * full.
		 */
		if (cep->tx_full && dev->tbusy) {
			cep->tx_full = 0;
			dev->tbusy = 0;
			mark_bh(NET_BH);
		}

		cep->dirty_tx = (cbd_t *)bdp;
	}

	if (must_restart) {
		volatile cpm8xx_t *cp;

		/* Some transmit errors cause the transmitter to shut
		 * down.  We now issue a restart transmit.  Since the
		 * errors close the BD and update the pointers, the restart
		 * _should_ pick up without having to reset any of our
		 * pointers either.
		 */
		cp = cpmp;
		cp->cp_cpcr =
		    mk_cr_cmd(CPM_CR_CH_SCC1, CPM_CR_RESTART_TX) | CPM_CR_FLG;
		while (cp->cp_cpcr & CPM_CR_FLG);
	}

	/* Check for receive busy, i.e. packets coming but no place to
	 * put them.  This "can't happen" because the receive interrupt
	 * is tossing previous frames.
	 */
	if (int_events & SCCE_ENET_BSY) {
		cep->stats.rx_dropped++;
		printk("CPM ENET: BSY can't happen.\n");
	}

	/* Write the SCC event register with the events we have handled
	 * to clear them.  Maybe we should do this sooner?
	 */
	cep->sccp->scc_scce = int_events;

	dev->interrupt = 0;

	return;
}

/* During a receive, the cur_rx points to the current incoming buffer.
 * When we update through the ring, if the next incoming buffer has
 * not been given to the system, we just set the empty indicator,
 * effectively tossing the packet.
 */
static int
cpm_enet_rx(struct device *dev)
{
	struct	cpm_enet_private *cep;
	volatile cbd_t	*bdp;
	struct	sk_buff *skb;
	ushort	pkt_len;

	cep = (struct cpm_enet_private *)dev->priv;

	/* First, grab all of the stats for the incoming packet.
	 * These get messed up if we get called due to a busy condition.
	 */
	bdp = cep->cur_rx;

for (;;) {
	if (bdp->cbd_sc & BD_ENET_RX_EMPTY)
		break;
		
#ifndef final_version
	/* Since we have allocated space to hold a complete frame, both
	 * the first and last indicators should be set.
	 */
	if ((bdp->cbd_sc & (BD_ENET_RX_FIRST | BD_ENET_RX_LAST)) !=
		(BD_ENET_RX_FIRST | BD_ENET_RX_LAST))
			printk("CPM ENET: rcv is not first+last\n");
#endif

	/* Frame too long or too short.
	*/
	if (bdp->cbd_sc & (BD_ENET_RX_LG | BD_ENET_RX_SH))
		cep->stats.rx_length_errors++;
	if (bdp->cbd_sc & BD_ENET_RX_NO)	/* Frame alignment */
		cep->stats.rx_frame_errors++;
	if (bdp->cbd_sc & BD_ENET_RX_CR)	/* CRC Error */
		cep->stats.rx_crc_errors++;
	if (bdp->cbd_sc & BD_ENET_RX_OV)	/* FIFO overrun */
		cep->stats.rx_crc_errors++;

	/* Report late collisions as a frame error.
	 * On this error, the BD is closed, but we don't know what we
	 * have in the buffer.  So, just drop this frame on the floor.
	 */
	if (bdp->cbd_sc & BD_ENET_RX_CL) {
		cep->stats.rx_frame_errors++;
	}
	else {

		/* Process the incoming frame.
		*/
		cep->stats.rx_packets++;
		pkt_len = bdp->cbd_datlen;
		cep->stats.rx_bytes += pkt_len;

		/* This does 16 byte alignment, much more than we need.
		*/
		skb = dev_alloc_skb(pkt_len);

		if (skb == NULL) {
			printk("%s: Memory squeeze, dropping packet.\n", dev->name);
			cep->stats.rx_dropped++;
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
		bdp = cep->rx_bd_base;
	else
		bdp++;

   }
	cep->cur_rx = (cbd_t *)bdp;

	return 0;
}

static int
cpm_enet_close(struct device *dev)
{
	/* Don't know what to do yet.
	*/

	return 0;
}

static struct net_device_stats *cpm_enet_get_stats(struct device *dev)
{
	struct cpm_enet_private *cep = (struct cpm_enet_private *)dev->priv;

	return &cep->stats;
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
	struct	cpm_enet_private *cep;
	struct	dev_mc_list *dmi;
	u_char	*mcptr, *tdptr;
	volatile scc_enet_t *ep;
	int	i, j;
	cep = (struct cpm_enet_private *)dev->priv;

	/* Get pointer to SCC1 area in parameter RAM.
	*/
	ep = (scc_enet_t *)dev->base_addr;

	if (dev->flags&IFF_PROMISC) {
	  
		/* Log any net taps. */
		printk("%s: Promiscuous mode enabled.\n", dev->name);
		cep->sccp->scc_pmsr |= SCC_PMSR_PRO;
	} else {

		cep->sccp->scc_pmsr &= ~SCC_PMSR_PRO;

		if (dev->flags & IFF_ALLMULTI) {
			/* Catch all multicast addresses, so set the
			 * filter to all 1's.
			 */
			ep->sen_gaddr1 = 0xffff;
			ep->sen_gaddr2 = 0xffff;
			ep->sen_gaddr3 = 0xffff;
			ep->sen_gaddr4 = 0xffff;
		}
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
	}
}

/* Initialize the CPM Ethernet on SCC1.  If EPPC-Bug loaded us, or performed
 * some other network I/O, a whole bunch of this has already been set up.
 * It is no big deal if we do it again, we just have to disable the
 * transmit and receive to make sure we don't catch the CPM with some
 * inconsistent control information.
 */
__initfunc(int cpm_enet_init(void))
{
	struct device *dev;
	struct cpm_enet_private *cep;
	int i, j;
	unsigned char	*eap;
	unsigned long	mem_addr;
	pte_t		*pte;
	volatile	cbd_t		*bdp;
	volatile	cpm8xx_t	*cp;
	volatile	scc_t		*sccp;
	volatile	scc_enet_t	*ep;
	volatile	immap_t		*immap;

	cp = cpmp;	/* Get pointer to Communication Processor */

	immap = (immap_t *)IMAP_ADDR;	/* and to internal registers */

	/* Allocate some private information.
	*/
	cep = (struct cpm_enet_private *)kmalloc(sizeof(*cep), GFP_KERNEL);
	/*memset(cep, 0, sizeof(*cep));*/
	__clear_user(cep,sizeof(*cep));

	/* Create an Ethernet device instance.
	*/
	dev = init_etherdev(0, 0);

	/* Get pointer to SCC1 area in parameter RAM.
	*/
	ep = (scc_enet_t *)(&cp->cp_dparam[PROFF_SCC1]);

	/* And another to the SCC register area.
	*/
	sccp = (volatile scc_t *)(&cp->cp_scc[0]);
	cep->sccp = (scc_t *)sccp;		/* Keep the pointer handy */

	/* Disable receive and transmit in case EPPC-Bug started it.
	*/
	sccp->scc_gsmrl &= ~(SCC_GSMRL_ENR | SCC_GSMRL_ENT);

	/* Cookbook style from the MPC860 manual.....
	 * Not all of this is necessary if EPPC-Bug has initialized
	 * the network.
	 */

	/* Configure port A pins for Txd and Rxd.
	*/
	immap->im_ioport.iop_papar |= (PA_ENET_RXD | PA_ENET_TXD);
	immap->im_ioport.iop_padir &= ~(PA_ENET_RXD | PA_ENET_TXD);
	immap->im_ioport.iop_paodr &= ~PA_ENET_TXD;

	/* Configure port C pins to enable CLSN and RENA.
	*/
	immap->im_ioport.iop_pcpar &= ~(PC_ENET_CLSN | PC_ENET_RENA);
	immap->im_ioport.iop_pcdir &= ~(PC_ENET_CLSN | PC_ENET_RENA);
	immap->im_ioport.iop_pcso |= (PC_ENET_CLSN | PC_ENET_RENA);

	/* Configure port A for TCLK and RCLK.
	*/
	immap->im_ioport.iop_papar |= (PA_ENET_TCLK | PA_ENET_RCLK);
	immap->im_ioport.iop_padir &= ~(PA_ENET_TCLK | PA_ENET_RCLK);

	/* Configure Serial Interface clock routing.
	 * First, clear all SCC1 bits to zero, then set the ones we want.
	 */
	cp->cp_sicr &= ~SICR_ENET_MASK;
	cp->cp_sicr |= SICR_ENET_CLKRT;

	/* Manual says set SDDR, but I can't find anything with that
	 * name.  I think it is a misprint, and should be SDCR.  This
	 * has already been set by the communication processor initialization.
	 */

	/* Allocate space for the buffer descriptors in the DP ram.
	 * These are relative offsets in the DP ram address space.
	 * Initialize base addresses for the buffer descriptors.
	 */
	i = m8xx_cpm_dpalloc(sizeof(cbd_t) * RX_RING_SIZE);
	ep->sen_genscc.scc_rbase = i;
	cep->rx_bd_base = (cbd_t *)&cp->cp_dpmem[i];

	i = m8xx_cpm_dpalloc(sizeof(cbd_t) * TX_RING_SIZE);
	ep->sen_genscc.scc_tbase = i;
	cep->tx_bd_base = (cbd_t *)&cp->cp_dpmem[i];

	cep->dirty_tx = cep->cur_tx = cep->tx_bd_base;
	cep->cur_rx = cep->rx_bd_base;

	/* Issue init Rx BD command for SCC1.
	 * Manual says to perform an Init Rx parameters here.  We have
	 * to perform both Rx and Tx because the SCC may have been
	 * already running.
	 * In addition, we have to do it later because we don't yet have
	 * all of the BD control/status set properly.
	cp->cp_cpcr = mk_cr_cmd(CPM_CR_CH_SCC1, CPM_CR_INIT_RX) | CPM_CR_FLG;
	while (cp->cp_cpcr & CPM_CR_FLG);
	 */

	/* Initialize function code registers for big-endian.
	*/
	ep->sen_genscc.scc_rfcr = SCC_EB;
	ep->sen_genscc.scc_tfcr = SCC_EB;

	/* Set maximum bytes per receive buffer.
	 * This appears to be an Ethernet frame size, not the buffer
	 * fragment size.  It must be a multiple of four.
	 */
	ep->sen_genscc.scc_mrblr = PKT_MAXBLR_SIZE;

	/* Set CRC preset and mask.
	*/
	ep->sen_cpres = 0xffffffff;
	ep->sen_cmask = 0xdebb20e3;

	ep->sen_crcec = 0;	/* CRC Error counter */
	ep->sen_alec = 0;	/* alignment error counter */
	ep->sen_disfc = 0;	/* discard frame counter */

	ep->sen_pads = 0x8888;	/* Tx short frame pad character */
	ep->sen_retlim = 15;	/* Retry limit threshold */

	ep->sen_maxflr = PKT_MAXBUF_SIZE;   /* maximum frame length register */
	ep->sen_minflr = PKT_MINBUF_SIZE;  /* minimum frame length register */

	ep->sen_maxd1 = PKT_MAXBUF_SIZE;	/* maximum DMA1 length */
	ep->sen_maxd2 = PKT_MAXBUF_SIZE;	/* maximum DMA2 length */

	/* Clear hash tables.
	*/
	ep->sen_gaddr1 = 0;
	ep->sen_gaddr2 = 0;
	ep->sen_gaddr3 = 0;
	ep->sen_gaddr4 = 0;
	ep->sen_iaddr1 = 0;
	ep->sen_iaddr2 = 0;
	ep->sen_iaddr3 = 0;
	ep->sen_iaddr4 = 0;

	/* Set Ethernet station address.  This must come from the
	 * Vital Product Data (VPD) EEPROM.....as soon as I get the
	 * I2C interface working.....
	 *
	 * Since we performed a diskless boot, the Ethernet controller
	 * has been initialized and we copy the address out into our
	 * own structure.
	 */
#ifdef notdef
	ep->sen_paddrh = my_enet_addr[0];
	ep->sen_paddrm = my_enet_addr[1];
	ep->sen_paddrl = my_enet_addr[2];
#else
	eap = (unsigned char *)&(ep->sen_paddrh);
	for (i=5; i>=0; i--)
		dev->dev_addr[i] = *eap++;
#endif

	ep->sen_pper = 0;	/* 'cause the book says so */
	ep->sen_taddrl = 0;	/* temp address (LSB) */
	ep->sen_taddrm = 0;
	ep->sen_taddrh = 0;	/* temp address (MSB) */

	/* Now allocate the host memory pages and initialize the
	 * buffer descriptors.
	 */
	bdp = cep->tx_bd_base;
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

	bdp = cep->rx_bd_base;
	for (i=0; i<CPM_ENET_RX_PAGES; i++) {

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
		for (j=0; j<CPM_ENET_RX_FRPPG; j++) {
			bdp->cbd_sc = BD_ENET_RX_EMPTY | BD_ENET_RX_INTR;
			bdp->cbd_bufaddr = __pa(mem_addr);
			mem_addr += CPM_ENET_RX_FRSIZE;
			bdp++;
		}
	}

	/* Set the last buffer to wrap.
	*/
	bdp--;
	bdp->cbd_sc |= BD_SC_WRAP;

	/* Let's re-initialize the channel now.  We have to do it later
	 * than the manual describes because we have just now finished
	 * the BD initialization.
	 */
	cp->cp_cpcr = mk_cr_cmd(CPM_CR_CH_SCC1, CPM_CR_INIT_TRX) | CPM_CR_FLG;
	while (cp->cp_cpcr & CPM_CR_FLG);

	cep->skb_cur = cep->skb_dirty = 0;

	sccp->scc_scce = 0xffff;	/* Clear any pending events */

	/* Enable interrupts for transmit error, complete frame
	 * received, and any transmit buffer we have also set the
	 * interrupt flag.
	 */
	sccp->scc_sccm = (SCCE_ENET_TXE | SCCE_ENET_RXF | SCCE_ENET_TXB);

	/* Install our interrupt handler.
	*/
	cpm_install_handler(CPMVEC_SCC1, cpm_enet_interrupt, dev);

	/* Set GSMR_H to enable all normal operating modes.
	 * Set GSMR_L to enable Ethernet to MC68160.
	 */
	sccp->scc_gsmrh = 0;
	sccp->scc_gsmrl = (SCC_GSMRL_TCI | SCC_GSMRL_TPL_48 | SCC_GSMRL_TPP_10 | SCC_GSMRL_MODE_ENET);

	/* Set sync/delimiters.
	*/
	sccp->scc_dsr = 0xd555;

	/* Set processing mode.  Use Ethernet CRC, catch broadcast, and
	 * start frame search 22 bit times after RENA.
	 */
	sccp->scc_pmsr = (SCC_PMSR_ENCRC | SCC_PMSR_BRO | SCC_PMSR_NIB22);

	/* It is now OK to enable the Ethernet transmitter.
	*/
	immap->im_ioport.iop_pcpar |= PC_ENET_TENA;
	immap->im_ioport.iop_pcdir &= ~PC_ENET_TENA;

	dev->base_addr = (unsigned long)ep;
	dev->priv = cep;
	dev->name = "CPM_ENET";

	/* The CPM Ethernet specific entries in the device structure. */
	dev->open = cpm_enet_open;
	dev->hard_start_xmit = cpm_enet_start_xmit;
	dev->stop = cpm_enet_close;
	dev->get_stats = cpm_enet_get_stats;
	dev->set_multicast_list = set_multicast_list;

	/* And last, enable the transmit and receive processing.
	*/
	sccp->scc_gsmrl |= (SCC_GSMRL_ENR | SCC_GSMRL_ENT);

	printk("CPM ENET Version 0.1, ");
	for (i=0; i<5; i++)
		printk("%02x:", dev->dev_addr[i]);
	printk("%02x\n", dev->dev_addr[5]);

	return 0;
}
