/* sunqe.c: Sparc QuadEthernet 10baseT SBUS card driver.
 *          Once again I am out to prove that every ethernet
 *          controller out there can be most efficiently programmed
 *          if you make it look like a LANCE.
 *
 * Copyright (C) 1996, 1999 David S. Miller (davem@redhat.com)
 */

static char *version =
        "sunqe.c:v2.0 9/9/99 David S. Miller (davem@redhat.com)\n";

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
#include <asm/irq.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include "sunqe.h"

#ifdef MODULE
static struct sunqec *root_qec_dev = NULL;
#endif

static void qe_set_multicast(struct net_device *dev);

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
	printk("QuadEther: AIEEE cannot reset the QEC!\n");
	return -1;
}

#define MACE_RESET_RETRIES 200
#define QE_RESET_RETRIES   200

static inline int qe_stop(struct sunqe *qep)
{
	struct qe_creg *cregs = qep->qcregs;
	struct qe_mregs *mregs = qep->mregs;
	int tries;

	/* Reset the MACE, then the QEC channel. */
	mregs->bconfig = MREGS_BCONFIG_RESET;
	tries = MACE_RESET_RETRIES;
	while(--tries) {
		if(mregs->bconfig & MREGS_BCONFIG_RESET) {
			udelay(20);
			continue;
		}
		break;
	}
	if(!tries) {
		printk("QuadEther: AIEEE cannot reset the MACE!\n");
		return -1;
	}

	cregs->ctrl = CREG_CTRL_RESET;
	tries = QE_RESET_RETRIES;
	while(--tries) {
		if(cregs->ctrl & CREG_CTRL_RESET) {
			udelay(20);
			continue;
		}
		break;
	}
	if(!tries) {
		printk("QuadEther: Cannot reset QE channel!\n");
		return -1;
	}
	return 0;
}

static void qe_init_rings(struct sunqe *qep)
{
	struct qe_init_block *qb = qep->qe_block;
	struct sunqe_buffers *qbufs = qep->buffers;
	__u32 qbufs_dvma = qep->buffers_dvma;
	int i;

	qep->rx_new = qep->rx_old = qep->tx_new = qep->tx_old = 0;
	memset(qb, 0, sizeof(struct qe_init_block));
	memset(qbufs, 0, sizeof(struct sunqe_buffers));
	for(i = 0; i < RX_RING_SIZE; i++) {
		qb->qe_rxd[i].rx_addr = qbufs_dvma + qebuf_offset(rx_buf, i);
		qb->qe_rxd[i].rx_flags =
			(RXD_OWN | ((RXD_PKT_SZ) & RXD_LENGTH));
	}
}

static int qe_init(struct sunqe *qep, int from_irq)
{
	struct sunqec *qecp = qep->parent;
	struct qe_creg *cregs = qep->qcregs;
	struct qe_mregs *mregs = qep->mregs;
	struct qe_globreg *gregs = qecp->gregs;
	unsigned char *e = &qep->dev->dev_addr[0];
	volatile unsigned char garbage;
	int i;

	/* Shut it up. */
	if(qe_stop(qep))
		return -EAGAIN;

	/* Setup initial rx/tx init block pointers. */
	cregs->rxds = qep->qblock_dvma + qib_offset(qe_rxd, 0);
	cregs->txds = qep->qblock_dvma + qib_offset(qe_txd, 0);

	/* Enable/mask the various irq's. */
	cregs->rimask = 0;
	cregs->timask = 1;

	cregs->qmask = 0;
	cregs->mmask = CREG_MMASK_RXCOLL;

	/* Setup the FIFO pointers into QEC local memory. */
	cregs->rxwbufptr = cregs->rxrbufptr = qep->channel * gregs->msize;
	cregs->txwbufptr = cregs->txrbufptr = cregs->rxrbufptr + gregs->rsize;

	/* Clear the channel collision counter. */
	cregs->ccnt = 0;

	/* For 10baseT, inter frame space nor throttle seems to be necessary. */
	cregs->pipg = 0;

	/* Now dork with the AMD MACE. */
	mregs->phyconfig = MREGS_PHYCONFIG_AUTO;
	mregs->txfcntl = MREGS_TXFCNTL_AUTOPAD; /* Save us some tx work. */
	mregs->rxfcntl = 0;

	/* The QEC dma's the rx'd packets from local memory out to main memory,
	 * and therefore it interrupts when the packet reception is "complete".
	 * So don't listen for the MACE talking about it.
	 */
	mregs->imask = (MREGS_IMASK_COLL | MREGS_IMASK_RXIRQ);

	mregs->bconfig = (MREGS_BCONFIG_BSWAP | MREGS_BCONFIG_64TS);
	mregs->fconfig = (MREGS_FCONFIG_TXF16 | MREGS_FCONFIG_RXF32 |
			  MREGS_FCONFIG_RFWU | MREGS_FCONFIG_TFWU);

	/* Only usable interface on QuadEther is twisted pair. */
	mregs->plsconfig = (MREGS_PLSCONFIG_TP);

	/* Tell MACE we are changing the ether address. */
	mregs->iaconfig = (MREGS_IACONFIG_ACHNGE | MREGS_IACONFIG_PARESET);
	while ((mregs->iaconfig & MREGS_IACONFIG_ACHNGE) != 0)
		barrier();
	mregs->ethaddr = e[0];
	mregs->ethaddr = e[1];
	mregs->ethaddr = e[2];
	mregs->ethaddr = e[3];
	mregs->ethaddr = e[4];
	mregs->ethaddr = e[5];

	/* Clear out the address filter. */
	mregs->iaconfig = (MREGS_IACONFIG_ACHNGE | MREGS_IACONFIG_LARESET);
	while ((mregs->iaconfig & MREGS_IACONFIG_ACHNGE) != 0)
		barrier();
	for(i = 0; i < 8; i++)
		mregs->filter = 0;

	/* Address changes are now complete. */
	mregs->iaconfig = 0;

	qe_init_rings(qep);

	/* Wait a little bit for the link to come up... */
	mdelay(5);
	if(!(mregs->phyconfig & MREGS_PHYCONFIG_LTESTDIS)) {
		int tries = 50;

		while (tries--) {
			mdelay(5);
			barrier();
			if((mregs->phyconfig & MREGS_PHYCONFIG_LSTAT) != 0)
				break;
		}
		if (tries == 0)
			printk("%s: Warning, link state is down.\n", qep->dev->name);
	}

	/* Missed packet counter is cleared on a read. */
	garbage = mregs->mpcnt;

	/* Reload multicast information, this will enable the receiver
	 * and transmitter.  But set the base mconfig value right now.
	 */
	qe_set_multicast(qep->dev);

	/* QEC should now start to show interrupts. */
	return 0;
}

/* Grrr, certain error conditions completely lock up the AMD MACE,
 * so when we get these we _must_ reset the chip.
 */
static int qe_is_bolixed(struct sunqe *qep, unsigned int qe_status)
{
	struct net_device *dev = qep->dev;
	int mace_hwbug_workaround = 0;

	if(qe_status & CREG_STAT_EDEFER) {
		printk("%s: Excessive transmit defers.\n", dev->name);
		qep->net_stats.tx_errors++;
	}

	if(qe_status & CREG_STAT_CLOSS) {
		printk("%s: Carrier lost, link down?\n", dev->name);
		qep->net_stats.tx_errors++;
		qep->net_stats.tx_carrier_errors++;
	}

	if(qe_status & CREG_STAT_ERETRIES) {
		printk("%s: Excessive transmit retries (more than 16).\n", dev->name);
		qep->net_stats.tx_errors++;
		mace_hwbug_workaround = 1;
	}

	if(qe_status & CREG_STAT_LCOLL) {
		printk("%s: Late transmit collision.\n", dev->name);
		qep->net_stats.tx_errors++;
		qep->net_stats.collisions++;
		mace_hwbug_workaround = 1;
	}

	if(qe_status & CREG_STAT_FUFLOW) {
		printk("%s: Transmit fifo underflow, driver bug.\n", dev->name);
		qep->net_stats.tx_errors++;
		mace_hwbug_workaround = 1;
	}

	if(qe_status & CREG_STAT_JERROR) {
		printk("%s: Jabber error.\n", dev->name);
	}

	if(qe_status & CREG_STAT_BERROR) {
		printk("%s: Babble error.\n", dev->name);
	}

	if(qe_status & CREG_STAT_CCOFLOW) {
		qep->net_stats.tx_errors += 256;
		qep->net_stats.collisions += 256;
	}

	if(qe_status & CREG_STAT_TXDERROR) {
		printk("%s: Transmit descriptor is bogus, driver bug.\n", dev->name);
		qep->net_stats.tx_errors++;
		qep->net_stats.tx_aborted_errors++;
		mace_hwbug_workaround = 1;
	}

	if(qe_status & CREG_STAT_TXLERR) {
		printk("%s: Transmit late error.\n", dev->name);
		qep->net_stats.tx_errors++;
		mace_hwbug_workaround = 1;
	}

	if(qe_status & CREG_STAT_TXPERR) {
		printk("%s: Transmit DMA parity error.\n", dev->name);
		qep->net_stats.tx_errors++;
		qep->net_stats.tx_aborted_errors++;
		mace_hwbug_workaround = 1;
	}

	if(qe_status & CREG_STAT_TXSERR) {
		printk("%s: Transmit DMA sbus error ack.\n", dev->name);
		qep->net_stats.tx_errors++;
		qep->net_stats.tx_aborted_errors++;
		mace_hwbug_workaround = 1;
	}

	if(qe_status & CREG_STAT_RCCOFLOW) {
		qep->net_stats.rx_errors += 256;
		qep->net_stats.collisions += 256;
	}

	if(qe_status & CREG_STAT_RUOFLOW) {
		qep->net_stats.rx_errors += 256;
		qep->net_stats.rx_over_errors += 256;
	}

	if(qe_status & CREG_STAT_MCOFLOW) {
		qep->net_stats.rx_errors += 256;
		qep->net_stats.rx_missed_errors += 256;
	}

	if(qe_status & CREG_STAT_RXFOFLOW) {
		printk("%s: Receive fifo overflow.\n", dev->name);
		qep->net_stats.rx_errors++;
		qep->net_stats.rx_over_errors++;
	}

	if(qe_status & CREG_STAT_RLCOLL) {
		printk("%s: Late receive collision.\n", dev->name);
		qep->net_stats.rx_errors++;
		qep->net_stats.collisions++;
	}

	if(qe_status & CREG_STAT_FCOFLOW) {
		qep->net_stats.rx_errors += 256;
		qep->net_stats.rx_frame_errors += 256;
	}

	if(qe_status & CREG_STAT_CECOFLOW) {
		qep->net_stats.rx_errors += 256;
		qep->net_stats.rx_crc_errors += 256;
	}

	if(qe_status & CREG_STAT_RXDROP) {
		printk("%s: Receive packet dropped.\n", dev->name);
		qep->net_stats.rx_errors++;
		qep->net_stats.rx_dropped++;
		qep->net_stats.rx_missed_errors++;
	}

	if(qe_status & CREG_STAT_RXSMALL) {
		printk("%s: Receive buffer too small, driver bug.\n", dev->name);
		qep->net_stats.rx_errors++;
		qep->net_stats.rx_length_errors++;
	}

	if(qe_status & CREG_STAT_RXLERR) {
		printk("%s: Receive late error.\n", dev->name);
		qep->net_stats.rx_errors++;
		mace_hwbug_workaround = 1;
	}

	if(qe_status & CREG_STAT_RXPERR) {
		printk("%s: Receive DMA parity error.\n", dev->name);
		qep->net_stats.rx_errors++;
		qep->net_stats.rx_missed_errors++;
		mace_hwbug_workaround = 1;
	}

	if(qe_status & CREG_STAT_RXSERR) {
		printk("%s: Receive DMA sbus error ack.\n", dev->name);
		qep->net_stats.rx_errors++;
		qep->net_stats.rx_missed_errors++;
		mace_hwbug_workaround = 1;
	}

	if(mace_hwbug_workaround)
		qe_init(qep, 1);
	return mace_hwbug_workaround;
}

/* Per-QE receive interrupt service routine.  Just like on the happy meal
 * we receive directly into skb's with a small packet copy water mark.
 */
static void qe_rx(struct sunqe *qep)
{
	struct qe_rxd *rxbase = &qep->qe_block->qe_rxd[0];
	struct qe_rxd *this;
	struct sunqe_buffers *qbufs = qep->buffers;
	__u32 qbufs_dvma = qep->buffers_dvma;
	int elem = qep->rx_new, drops = 0;
	unsigned int flags;

	this = &rxbase[elem];
	while(!((flags = this->rx_flags) & RXD_OWN)) {
		struct sk_buff *skb;
		unsigned char *this_qbuf =
			&qbufs->rx_buf[elem & (RX_RING_SIZE - 1)][0];
		__u32 this_qbuf_dvma = qbufs_dvma +
			qebuf_offset(rx_buf, (elem & (RX_RING_SIZE - 1)));
		struct qe_rxd *end_rxd =
			&rxbase[(elem+RX_RING_SIZE)&(RX_RING_MAXSIZE-1)];
		int len = (flags & RXD_LENGTH) - 4;  /* QE adds ether FCS size to len */

		/* Check for errors. */
		if(len < ETH_ZLEN) {
			qep->net_stats.rx_errors++;
			qep->net_stats.rx_length_errors++;
			qep->net_stats.rx_dropped++;
		} else {
			skb = dev_alloc_skb(len + 2);
			if(skb == 0) {
				drops++;
				qep->net_stats.rx_dropped++;
			} else {
				skb->dev = qep->dev;
				skb_reserve(skb, 2);
				skb_put(skb, len);
				eth_copy_and_sum(skb, (unsigned char *)this_qbuf,
						 len, 0);
				skb->protocol = eth_type_trans(skb, qep->dev);
				netif_rx(skb);
				qep->net_stats.rx_packets++;
				qep->net_stats.rx_bytes+=len;
			}
		}
		end_rxd->rx_addr = this_qbuf_dvma;
		end_rxd->rx_flags = (RXD_OWN | ((RXD_PKT_SZ) & RXD_LENGTH));
		
		elem = NEXT_RX(elem);
		this = &rxbase[elem];
	}
	qep->rx_new = elem;
	if(drops)
		printk("%s: Memory squeeze, deferring packet.\n", qep->dev->name);
}

/* Interrupts for all QE's get filtered out via the QEC master controller,
 * so we just run through each qe and check to see who is signaling
 * and thus needs to be serviced.
 */
static void qec_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct sunqec *qecp = (struct sunqec *) dev_id;
	unsigned int qec_status;
	int channel = 0;

	/* Latch the status now. */
	qec_status = qecp->gregs->stat;
	while(channel < 4) {
		if(qec_status & 0xf) {
			struct sunqe *qep = qecp->qes[channel];
			struct net_device *dev = qep->dev;
			unsigned int qe_status;

			dev->interrupt = 1;

			qe_status = qep->qcregs->stat;
			if(qe_status & CREG_STAT_ERRORS)
				if(qe_is_bolixed(qep, qe_status))
					goto next;

			if(qe_status & CREG_STAT_RXIRQ)
				qe_rx(qep);
	next:
			dev->interrupt = 0;
		}
		qec_status >>= 4;
		channel++;
	}
}

static int qe_open(struct net_device *dev)
{
	struct sunqe *qep = (struct sunqe *) dev->priv;
	int res;

	qep->mconfig = (MREGS_MCONFIG_TXENAB |
			MREGS_MCONFIG_RXENAB |
			MREGS_MCONFIG_MBAENAB);
	res = qe_init(qep, 0);
	if(!res)
		MOD_INC_USE_COUNT;

	return res;
}

static int qe_close(struct net_device *dev)
{
	struct sunqe *qep = (struct sunqe *) dev->priv;

	qe_stop(qep);
	MOD_DEC_USE_COUNT;
	return 0;
}

/* Reclaim TX'd frames from the ring. */
static void qe_tx_reclaim(struct sunqe *qep)
{
	struct qe_txd *txbase = &qep->qe_block->qe_txd[0];
	struct net_device *dev = qep->dev;
	int elem = qep->tx_old;

	while(elem != qep->tx_new) {
		unsigned int flags = txbase[elem].tx_flags;

		if (flags & TXD_OWN)
			break;
		qep->net_stats.tx_packets++;
		qep->net_stats.tx_bytes+=(flags & TXD_LENGTH);
		elem = NEXT_TX(elem);
	}
	qep->tx_old = elem;

	if(dev->tbusy && (TX_BUFFS_AVAIL(qep) > 0)) {
		dev->tbusy = 0;
		mark_bh(NET_BH);
	}
}

/* Get a packet queued to go onto the wire. */
static int qe_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct sunqe *qep = (struct sunqe *) dev->priv;
	struct sunqe_buffers *qbufs = qep->buffers;
	__u32 txbuf_dvma, qbufs_dvma = qep->buffers_dvma;
	unsigned char *txbuf;
	int len, entry;

	qe_tx_reclaim(qep);

	if(test_and_set_bit(0, (void *) &dev->tbusy) != 0) {
		long tickssofar = jiffies - dev->trans_start;

		if (tickssofar >= 40) {
			printk("%s: transmit timed out, resetting\n", dev->name);
			qe_init(qep, 1);
			dev->tbusy = 0;
			dev->trans_start = jiffies;
		}
		return 1;
	}

	if(!TX_BUFFS_AVAIL(qep))
		return 1;

	len = skb->len;
	entry = qep->tx_new;

	txbuf = &qbufs->tx_buf[entry & (TX_RING_SIZE - 1)][0];
	txbuf_dvma = qbufs_dvma +
		qebuf_offset(tx_buf, (entry & (TX_RING_SIZE - 1)));

	/* Avoid a race... */
	qep->qe_block->qe_txd[entry].tx_flags = TXD_UPDATE;

	memcpy(txbuf, skb->data, len);

	qep->qe_block->qe_txd[entry].tx_addr = txbuf_dvma;
	qep->qe_block->qe_txd[entry].tx_flags =
		(TXD_OWN | TXD_SOP | TXD_EOP | (len & TXD_LENGTH));
	qep->tx_new = NEXT_TX(entry);

	/* Get it going. */
	dev->trans_start = jiffies;
	qep->qcregs->ctrl = CREG_CTRL_TWAKEUP;

	dev_kfree_skb(skb);

	if(TX_BUFFS_AVAIL(qep))
		dev->tbusy = 0;

	return 0;
}

static struct net_device_stats *qe_get_stats(struct net_device *dev)
{
	struct sunqe *qep = (struct sunqe *) dev->priv;

	return &qep->net_stats;
}

#define CRC_POLYNOMIAL_BE 0x04c11db7UL  /* Ethernet CRC, big endian */
#define CRC_POLYNOMIAL_LE 0xedb88320UL  /* Ethernet CRC, little endian */

static void qe_set_multicast(struct net_device *dev)
{
	struct sunqe *qep = (struct sunqe *) dev->priv;
	struct dev_mc_list *dmi = dev->mc_list;
	unsigned char new_mconfig = qep->mconfig;
	char *addrs;
	int i, j, bit, byte;
	u32 crc, poly = CRC_POLYNOMIAL_LE;

	/* Lock out others. */
	set_bit(0, (void *) &dev->tbusy);

	if((dev->flags & IFF_ALLMULTI) || (dev->mc_count > 64)) {
		qep->mregs->iaconfig = MREGS_IACONFIG_ACHNGE | MREGS_IACONFIG_LARESET;
		while ((qep->mregs->iaconfig & MREGS_IACONFIG_ACHNGE) != 0)
			barrier();
		for(i = 0; i < 8; i++)
			qep->mregs->filter = 0xff;
		qep->mregs->iaconfig = 0;
	} else if(dev->flags & IFF_PROMISC) {
		new_mconfig |= MREGS_MCONFIG_PROMISC;
	} else {
		u16 hash_table[4];
		unsigned char *hbytes = (unsigned char *) &hash_table[0];

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
		/* Program the qe with the new filter value. */
		qep->mregs->iaconfig = MREGS_IACONFIG_ACHNGE | MREGS_IACONFIG_LARESET;
		while ((qep->mregs->iaconfig & MREGS_IACONFIG_ACHNGE) != 0)
			barrier();
		for(i = 0; i < 8; i++)
			qep->mregs->filter = *hbytes++;
		qep->mregs->iaconfig = 0;
	}

	/* Any change of the logical address filter, the physical address,
	 * or enabling/disabling promiscuous mode causes the MACE to disable
	 * the receiver.  So we must re-enable them here or else the MACE
	 * refuses to listen to anything on the network.  Sheesh, took
	 * me a day or two to find this bug.
	 */
	qep->mconfig = new_mconfig;
	qep->mregs->mconfig = qep->mconfig;

	/* Let us get going again. */
	dev->tbusy = 0;
}

/* This is only called once at boot time for each card probed. */
static inline void qec_init_once(struct sunqec *qecp, struct linux_sbus_device *qsdev)
{
	unsigned char bsizes = qecp->qec_bursts;

#ifdef __sparc_v9__
	if (bsizes & DMA_BURST64) {
		qecp->gregs->ctrl = GLOB_CTRL_B64;
	} else
#endif
	if(bsizes & DMA_BURST32) {
		qecp->gregs->ctrl = GLOB_CTRL_B32;
	} else {
		qecp->gregs->ctrl = GLOB_CTRL_B16;
	}

	/* Packetsize only used in 100baseT BigMAC configurations,
	 * set it to zero just to be on the safe side.
	 */
	qecp->gregs->psize = 0;

	/* Set the local memsize register, divided up to one piece per QE channel. */
	qecp->gregs->msize = (qsdev->reg_addrs[1].reg_size >> 2);

	/* Divide up the local QEC memory amongst the 4 QE receiver and
	 * transmitter FIFOs.  Basically it is (total / 2 / num_channels).
	 */
	qecp->gregs->rsize = qecp->gregs->tsize =
		(qsdev->reg_addrs[1].reg_size >> 2) >> 1;

}

/* Four QE's per QEC card. */
static inline int qec_ether_init(struct net_device *dev, struct linux_sbus_device *sdev)
{
	static unsigned version_printed = 0;
	struct net_device *qe_devs[4];
	struct sunqe *qeps[4];
	struct linux_sbus_device *qesdevs[4];
	struct sunqec *qecp;
	struct linux_prom_ranges qranges[8];
	unsigned char bsizes, bsizes_more, num_qranges;
	int i, j, res = ENOMEM;

	dev = init_etherdev(0, sizeof(struct sunqe));
	qe_devs[0] = dev;
	qeps[0] = (struct sunqe *) dev->priv;
	qeps[0]->channel = 0;
	for(j = 0; j < 6; j++)
		qe_devs[0]->dev_addr[j] = idprom->id_ethaddr[j];

	if(version_printed++ == 0)
		printk(version);

	qe_devs[1] = qe_devs[2] = qe_devs[3] = NULL;
	for(i = 1; i < 4; i++) {
		qe_devs[i] = init_etherdev(0, sizeof(struct sunqe));
		if(qe_devs[i] == NULL || qe_devs[i]->priv == NULL)
			goto qec_free_devs;
		qeps[i] = (struct sunqe *) qe_devs[i]->priv;
		for(j = 0; j < 6; j++)
			qe_devs[i]->dev_addr[j] = idprom->id_ethaddr[j];
		qeps[i]->channel = i;
	}
	qecp = kmalloc(sizeof(struct sunqec), GFP_KERNEL);
	if(qecp == NULL)
		goto qec_free_devs;
	qecp->qec_sbus_dev = sdev;

	for(i = 0; i < 4; i++) {
		qecp->qes[i] = qeps[i];
		qeps[i]->dev = qe_devs[i];
		qeps[i]->parent = qecp;
	}

	/* Link in channel 0. */
	i = prom_getintdefault(sdev->child->prom_node, "channel#", -1);
	if(i == -1) { res=ENODEV; goto qec_free_devs; }
	qesdevs[i] = sdev->child;
	qe_devs[i]->base_addr = (long) qesdevs[i];

	/* Link in channel 1. */
	i = prom_getintdefault(sdev->child->next->prom_node, "channel#", -1);
	if(i == -1) { res=ENODEV; goto qec_free_devs; }
	qesdevs[i] = sdev->child->next;
	qe_devs[i]->base_addr = (long) qesdevs[i];

	/* Link in channel 2. */
	i = prom_getintdefault(sdev->child->next->next->prom_node, "channel#", -1);
	if(i == -1) { res=ENODEV; goto qec_free_devs; }
	qesdevs[i] = sdev->child->next->next;
	qe_devs[i]->base_addr = (long) qesdevs[i];

	/* Link in channel 3. */
	i = prom_getintdefault(sdev->child->next->next->next->prom_node, "channel#", -1);
	if(i == -1) { res=ENODEV; goto qec_free_devs; }
	qesdevs[i] = sdev->child->next->next->next;
	qe_devs[i]->base_addr = (long) qesdevs[i];

	for(i = 0; i < 4; i++)
		qeps[i]->qe_sbusdev = qesdevs[i];

	/* This is a bit of fun, get QEC ranges. */
	i = prom_getproperty(sdev->prom_node, "ranges",
			     (char *) &qranges[0], sizeof(qranges));
	num_qranges = (i / sizeof(struct linux_prom_ranges));

	/* Now, apply all the ranges, QEC ranges then the SBUS ones for each QE. */
	if (sdev->ranges_applied == 0) {
		for(i = 0; i < 4; i++) {
			for(j = 0; j < 2; j++) {
				int k;

				for(k = 0; k < num_qranges; k++)
					if(qesdevs[i]->reg_addrs[j].which_io ==
					   qranges[k].ot_child_space)
						break;
				if(k >= num_qranges)
					printk("QuadEther: Aieee, bogus QEC range for "
					       "space %08x\n",qesdevs[i]->reg_addrs[j].which_io);
				qesdevs[i]->reg_addrs[j].which_io = qranges[k].ot_parent_space;
				qesdevs[i]->reg_addrs[j].phys_addr += qranges[k].ot_parent_base;
			}

			prom_apply_sbus_ranges(qesdevs[i]->my_bus, &qesdevs[i]->reg_addrs[0],
					       2, qesdevs[i]);
		}
		prom_apply_sbus_ranges(sdev->my_bus, &sdev->reg_addrs[0],
				       sdev->num_registers, sdev);
	}

	/* Now map in the registers, QEC globals first. */
	qecp->gregs = sparc_alloc_io(sdev->reg_addrs[0].phys_addr, 0,
				     sizeof(struct qe_globreg),
				     "QEC Global Registers",
				     sdev->reg_addrs[0].which_io, 0);
	if(!qecp->gregs) {
		printk("QuadEther: Cannot map QEC global registers.\n");
		res = ENODEV;
		goto qec_free_devs;
	}

	/* Make sure the QEC is in MACE mode. */
	if((qecp->gregs->ctrl & 0xf0000000) != GLOB_CTRL_MMODE) {
		printk("QuadEther: AIEEE, QEC is not in MACE mode!\n");
		res = ENODEV;
		goto qec_free_devs;
	}

	/* Reset the QEC. */
	if(qec_global_reset(qecp->gregs)) {
		res = ENODEV;
		goto qec_free_devs;
	}

	/* Find and set the burst sizes for the QEC, since it does
	 * the actual dma for all 4 channels.
	 */
	bsizes = prom_getintdefault(sdev->prom_node, "burst-sizes", 0xff);
	bsizes &= 0xff;
	bsizes_more = prom_getintdefault(sdev->my_bus->prom_node, "burst-sizes", 0xff);

	if(bsizes_more != 0xff)
		bsizes &= bsizes_more;
	if(bsizes == 0xff || (bsizes & DMA_BURST16) == 0 ||
	   (bsizes & DMA_BURST32)==0)
		bsizes = (DMA_BURST32 - 1);

	qecp->qec_bursts = bsizes;

	/* Perform one time QEC initialization, we never touch the QEC
	 * globals again after this.
	 */
	qec_init_once(qecp, sdev);

	for(i = 0; i < 4; i++) {
		/* Map in QEC per-channel control registers. */
		qeps[i]->qcregs = sparc_alloc_io(qesdevs[i]->reg_addrs[0].phys_addr, 0,
						 sizeof(struct qe_creg),
						 "QEC Per-Channel Registers",
						 qesdevs[i]->reg_addrs[0].which_io, 0);
		if(!qeps[i]->qcregs) {
			printk("QuadEther: Cannot map QE %d's channel registers.\n", i);
			res = ENODEV;
			goto qec_free_devs;
		}

		/* Map in per-channel AMD MACE registers. */
		qeps[i]->mregs = sparc_alloc_io(qesdevs[i]->reg_addrs[1].phys_addr, 0,
						sizeof(struct qe_mregs),
						"QE MACE Registers",
						qesdevs[i]->reg_addrs[1].which_io, 0);
		if(!qeps[i]->mregs) {
			printk("QuadEther: Cannot map QE %d's MACE registers.\n", i);
			res = ENODEV;
			goto qec_free_devs;
		}

		qeps[i]->qe_block = (struct qe_init_block *)
			sparc_dvma_malloc(PAGE_SIZE, "QE Init Block",
					  &qeps[i]->qblock_dvma);

		qeps[i]->buffers = (struct sunqe_buffers *)
			sparc_dvma_malloc(sizeof(struct sunqe_buffers),
					  "QE RX/TX Buffers",
					  &qeps[i]->buffers_dvma);

		/* Stop this QE. */
		qe_stop(qeps[i]);
	}

	for(i = 0; i < 4; i++) {
		qe_devs[i]->open = qe_open;
		qe_devs[i]->stop = qe_close;
		qe_devs[i]->hard_start_xmit = qe_start_xmit;
		qe_devs[i]->get_stats = qe_get_stats;
		qe_devs[i]->set_multicast_list = qe_set_multicast;
		qe_devs[i]->irq = sdev->irqs[0];
		qe_devs[i]->dma = 0;
		ether_setup(qe_devs[i]);
	}

	/* QEC receives interrupts from each QE, then it sends the actual
	 * IRQ to the cpu itself.  Since QEC is the single point of
	 * interrupt for all QE channels we register the IRQ handler
	 * for it now.
	 */
	if(request_irq(sdev->irqs[0], &qec_interrupt,
		       SA_SHIRQ, "QuadEther", (void *) qecp)) {
		printk("QuadEther: Can't register QEC master irq handler.\n");
		res = EAGAIN;
		goto qec_free_devs;
	}

	/* Report the QE channels. */
	for(i = 0; i < 4; i++) {
		printk("%s: QuadEthernet channel[%d] ", qe_devs[i]->name, i);
		for(j = 0; j < 6; j++)
			printk ("%2.2x%c",
				qe_devs[i]->dev_addr[j],
				j == 5 ? ' ': ':');
		printk("\n");
	}

#ifdef MODULE
	/* We are home free at this point, link the qe's into
	 * the master list for later module unloading.
	 */
	for(i = 0; i < 4; i++)
		qe_devs[i]->ifindex = dev_new_index();
	qecp->next_module = root_qec_dev;
	root_qec_dev = qecp;
#endif

	return 0;

qec_free_devs:
	for(i = 0; i < 4; i++) {
		if(qe_devs[i]) {
			if(qe_devs[i]->priv)
				kfree(qe_devs[i]->priv);
			kfree(qe_devs[i]);
		}
	}
	return res;
}

int __init qec_probe(struct net_device *dev)
{
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

			/* QEC can be parent of either QuadEthernet or BigMAC
			 * children.
			 */
			if(!strcmp(sdev->prom_name, "qec") && sdev->child &&
			   !strcmp(sdev->child->prom_name, "qe") &&
			   sdev->child->next &&
			   !strcmp(sdev->child->next->prom_name, "qe") &&
			   sdev->child->next->next &&
			   !strcmp(sdev->child->next->next->prom_name, "qe") &&
			   sdev->child->next->next->next &&
			   !strcmp(sdev->child->next->next->next->prom_name, "qe")) {
				cards++;
				if((v = qec_ether_init(dev, sdev)))
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
	root_qec_dev = NULL;
	return qec_probe(NULL);
}

void
cleanup_module(void)
{
	struct sunqec *next_qec;
	int i;

	/* No need to check MOD_IN_USE, as sys_delete_module() checks. */
	while (root_qec_dev) {
		next_qec = root_qec_dev->next_module;

		/* Release all four QE channels, then the QEC itself. */
		for(i = 0; i < 4; i++) {
			unregister_netdev(root_qec_dev->qes[i]->dev);
			sparc_free_io(root_qec_dev->qes[i]->qcregs, sizeof(struct qe_creg));
			sparc_free_io(root_qec_dev->qes[i]->mregs, sizeof(struct qe_mregs));
			kfree(root_qec_dev->qes[i]->dev);
		}
		free_irq(root_qec_dev->qec_sbus_dev->irqs[0], (void *)root_qec_dev);
		sparc_free_io(root_qec_dev->gregs, sizeof(struct qe_globreg));
		kfree(root_qec_dev);
		root_qec_dev = next_qec;
	}
}

#endif /* MODULE */
