/*
 * Network device driver for the GMAC ethernet controller on
 * Apple G4 Powermacs.
 *
 * Copyright (C) 2000 Paul Mackerras.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <asm/prom.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include "gmac.h"

#define DEBUG_PHY

#define NTX		32		/* must be power of 2 */
#define NRX		32		/* must be power of 2 */
#define RX_BUFLEN	(ETH_FRAME_LEN + 8)

struct gmac_dma_desc {
	unsigned int	cmd;
	unsigned int	status;
	unsigned int	address;	/* phys addr, low 32 bits */
	unsigned int	hi_addr;
};

/* Bits in cmd */
#define RX_OWN	0x80000000		/* 1 = owned by chip */
#define TX_SOP	0x80000000
#define TX_EOP	0x40000000

struct gmac {
	volatile unsigned int *regs;	/* hardware registers, virtual addr */
	volatile unsigned int *sysregs;
	unsigned long	desc_page;	/* page for DMA descriptors */
	volatile struct gmac_dma_desc *rxring;
	struct sk_buff	*rx_buff[NRX];
	int		next_rx;
	volatile struct gmac_dma_desc *txring;
	struct sk_buff	*tx_buff[NTX];
	int		next_tx;
	int		tx_gone;
	unsigned char	tx_full;
	int		phy_addr;
	int		full_duplex;
	struct net_device_stats stats;
	struct net_device *next_gmac;
};

#define GM_OUT(r, v)	out_le32(gm->regs + (r)/4, (v))
#define GM_IN(r)	in_le32(gm->regs + (r)/4)
#define GM_BIS(r, v)	GM_OUT((r), GM_IN(r) | (v))
#define GM_BIC(r, v)	GM_OUT((r), GM_IN(r) & ~(v))

#define PHY_B5400	0x6040
#define PHY_B5201	0x6212

static unsigned char dummy_buf[RX_BUFLEN+2];
static struct net_device *gmacs = NULL;

/* Prototypes */
static int mii_read(struct gmac *gm, int phy, int r);
static int mii_write(struct gmac *gm, int phy, int r, int v);
static void powerup_transceiver(struct gmac *gm);
static int gmac_reset(struct net_device *dev);
static void gmac_mac_init(struct gmac *gm, unsigned char *mac_addr);
static void gmac_init_rings(struct gmac *gm);
static void gmac_start_dma(struct gmac *gm);
static int gmac_open(struct net_device *dev);
static int gmac_close(struct net_device *dev);
static int gmac_xmit_start(struct sk_buff *skb, struct net_device *dev);
static int gmac_tx_cleanup(struct gmac *gm);
static void gmac_receive(struct net_device *dev);
static void gmac_interrupt(int irq, void *dev_id, struct pt_regs *regs);
static struct net_device_stats *gmac_stats(struct net_device *dev);
static int gmac_probe(void);
static void gmac_probe1(struct device_node *gmac);

/* Stuff for talking to the physical-layer chip */
static int
mii_read(struct gmac *gm, int phy, int r)
{
	int timeout;

	GM_OUT(MIFFRAME, 0x60020000 | (phy << 23) | (r << 18));
	for (timeout = 1000; timeout > 0; --timeout) {
		udelay(20);
		if (GM_IN(MIFFRAME) & 0x10000)
			return GM_IN(MIFFRAME) & 0xffff;
	}
	return -1;
}

static int
mii_write(struct gmac *gm, int phy, int r, int v)
{
	int timeout;

	GM_OUT(MIFFRAME, 0x50020000 | (phy << 23) | (r << 18) | (v & 0xffff));
	for (timeout = 1000; timeout > 0; --timeout) {
		udelay(20);
		if (GM_IN(MIFFRAME) & 0x10000)
			return 0;
	}
	return -1;
}

static void 
mii_poll_start(struct gmac *gm)
{
	unsigned int tmp;
	
	/* Start the MIF polling on the external transceiver. */
	tmp = GM_IN(MIFCONFIG);
	tmp &= ~(GMAC_MIF_CFGPR_MASK | GMAC_MIF_CFGPD_MASK);
	tmp |= ((gm->phy_addr & 0x1f) << GMAC_MIF_CFGPD_SHIFT);
	tmp |= (0x19 << GMAC_MIF_CFGPR_SHIFT);
	tmp |= GMAC_MIF_CFGPE;
	GM_OUT(MIFCONFIG, tmp);

	/* Let the bits set. */
	udelay(GMAC_MIF_POLL_DELAY);

	GM_OUT(MIFINTMASK, 0xffc0);
}

static void 
mii_poll_stop(struct gmac *gm)
{
	GM_OUT(MIFINTMASK, 0xffff);
	GM_BIC(MIFCONFIG, GMAC_MIF_CFGPE);
	udelay(GMAC_MIF_POLL_DELAY);
}

static void
mii_interrupt(struct gmac *gm)
{
	unsigned long	flags;
	int		phy_status;
	
	save_flags(flags);
	cli();

	mii_poll_stop(gm);

	/* May the status change before polling is re-enabled ? */
	mii_poll_start(gm);
	
	/* We read the Auxilliary Status Summary register */
	phy_status = mii_read(gm, gm->phy_addr, 0x19);
#ifdef DEBUG_PHY
	printk("mii_interrupt, phy_status: %x\n", phy_status);
#endif
	/* Auto-neg. complete ? */
	if (phy_status & 0x8000) {
		int full_duplex = 0;
		switch((phy_status >> 8) & 0x7) {
			case 2:
			case 5:
				full_duplex = 1;
				break;
		}
		if (full_duplex != gm->full_duplex) {
			GM_BIC(TXMAC_CONFIG, 1);
			udelay(200);
			if (full_duplex) {
				printk("full duplex active\n");
				GM_OUT(TXMAC_CONFIG, 6);
				GM_OUT(XIF_CONFIG, 1);
			} else {
				printk("half duplex active\n");
				GM_OUT(TXMAC_CONFIG, 0);
				GM_OUT(XIF_CONFIG, 5);
			}
			GM_BIS(TXMAC_CONFIG, 1);
			gm->full_duplex = full_duplex;
		}
	}

	restore_flags(flags);
}

static void
powerup_transceiver(struct gmac *gm)
{
	int phytype = mii_read(gm, 0, 3);
#ifdef DEBUG_PHY
	int i;
#endif	
	switch (phytype) {
	case PHY_B5400:
		mii_write(gm, 0, 0, mii_read(gm, 0, 0) & ~0x800);
		mii_write(gm, 31, 30, mii_read(gm, 31, 30) & ~8);
		break;
	case PHY_B5201:
		mii_write(gm, 0, 30, mii_read(gm, 0, 30) & ~8);
		break;
	default:
		printk(KERN_ERR "GMAC: unknown PHY type %x\n", phytype);
	}
	/* Check this */
	gm->phy_addr = 0;
	gm->full_duplex = 0;

#ifdef DEBUG_PHY
	printk("PHY regs:\n");
	for (i=0; i<0x20; i++) {
		printk("%04x ", mii_read(gm, 0, i)); 
		if ((i % 4) == 3)
			printk("\n");
	}
#endif
}

static int
gmac_reset(struct net_device *dev)
{
	struct gmac *gm = (struct gmac *) dev->priv;
	int timeout;

	/* turn on GB clock */
	out_le32(gm->sysregs + 0x20/4, in_le32(gm->sysregs + 0x20/4) | 2);
	udelay(10);
	GM_OUT(SW_RESET, 3);
	for (timeout = 100; timeout > 0; --timeout) {
		mdelay(10);
		if ((GM_IN(SW_RESET) & 3) == 0)
			return 0;
	}
	printk(KERN_ERR "GMAC: reset failed!\n");
	return -1;
}

static void
gmac_mac_init(struct gmac *gm, unsigned char *mac_addr)
{
	int i;

	GM_OUT(RANSEED, 937);
	GM_OUT(DATAPATHMODE, 4);
	mii_write(gm, 0, 0, 0x1000);
	GM_OUT(TXDMA_CONFIG, 0xffc00);
	GM_OUT(RXDMA_CONFIG, 0);
	GM_OUT(MACPAUSE, 0x1bf0);
	GM_OUT(IPG0, 0);
	GM_OUT(IPG1, 8);
	GM_OUT(IPG2, 4);
	GM_OUT(MINFRAMESIZE, 64);
	GM_OUT(MAXFRAMESIZE, 2000);
	GM_OUT(PASIZE, 7);
	GM_OUT(JAMSIZE, 4);
	GM_OUT(ATTEMPT_LIMIT, 16);
	GM_OUT(SLOTTIME, 64);
	GM_OUT(MACCNTL_TYPE, 0x8808);
	GM_OUT(MAC_ADDR_0, (mac_addr[4] << 8) + mac_addr[5]);
	GM_OUT(MAC_ADDR_1, (mac_addr[2] << 8) + mac_addr[3]);
	GM_OUT(MAC_ADDR_2, (mac_addr[0] << 8) + mac_addr[1]);
	GM_OUT(MAC_ADDR_3, 0);
	GM_OUT(MAC_ADDR_4, 0);
	GM_OUT(MAC_ADDR_5, 0);
	GM_OUT(MAC_ADDR_6, 0x0180);
	GM_OUT(MAC_ADDR_7, 0xc200);
	GM_OUT(MAC_ADDR_8, 0x0001);
	GM_OUT(MAC_ADDR_FILTER_0, 0);
	GM_OUT(MAC_ADDR_FILTER_1, 0);
	GM_OUT(MAC_ADDR_FILTER_2, 0);
	GM_OUT(MAC_ADDR_FILTER_MASK21, 0);
	GM_OUT(MAC_ADDR_FILTER_MASK0, 0);
	for (i = 0; i < 27; ++i)
		GM_OUT(MAC_HASHTABLE + i, 0);
	GM_OUT(MACCNTL_CONFIG, 0);
	/* default to half duplex */
	GM_OUT(TXMAC_CONFIG, 0);
	GM_OUT(XIF_CONFIG, 5);
}

static void
gmac_init_rings(struct gmac *gm)
{
	int i;
	struct sk_buff *skb;
	unsigned char *data;
	struct gmac_dma_desc *ring;

	/* init rx ring */
	ring = (struct gmac_dma_desc *) gm->rxring;
	memset(ring, 0, NRX * sizeof(struct gmac_dma_desc));
	for (i = 0; i < NRX; ++i, ++ring) {
		data = dummy_buf;
		gm->rx_buff[i] = skb = dev_alloc_skb(RX_BUFLEN + 2);
		if (skb != 0) {
			/*skb_reserve(skb, 2);*/
			data = skb->data;
		}
		st_le32(&ring->address, virt_to_bus(data));
		st_le32(&ring->cmd, RX_OWN);
	}

	/* init tx ring */
	ring = (struct gmac_dma_desc *) gm->txring;
	memset(ring, 0, NRX * sizeof(struct gmac_dma_desc));

	/* set pointers in chip */
	mb();
	GM_OUT(RXDMA_BASE_HIGH, 0);
	GM_OUT(RXDMA_BASE_LOW, virt_to_bus(gm->rxring));
	GM_OUT(TXDMA_BASE_HIGH, 0);
	GM_OUT(TXDMA_BASE_LOW, virt_to_bus(gm->txring));
}

static void
gmac_start_dma(struct gmac *gm)
{
	GM_BIS(RXDMA_CONFIG, 1);
	GM_BIS(RXMAC_CONFIG, 1);
	GM_OUT(RXDMA_KICK, NRX);
	GM_BIS(TXDMA_CONFIG, 1);
	GM_BIS(TXMAC_CONFIG, 1);
}

static int gmac_open(struct net_device *dev)
{
	struct gmac *gm = (struct gmac *) dev->priv;

	if (gmac_reset(dev))
		return -EIO;

	MOD_INC_USE_COUNT;

	powerup_transceiver(gm);
	gmac_mac_init(gm, dev->dev_addr);
	gmac_init_rings(gm);
	gmac_start_dma(gm);
	mii_interrupt(gm);

	GM_OUT(INTR_DISABLE, 0xfffdffe8);

	return 0;
}

static int gmac_close(struct net_device *dev)
{
	struct gmac *gm = (struct gmac *) dev->priv;
	int i;

	mii_poll_stop(gm);
	
	GM_BIC(RXDMA_CONFIG, 1);
	GM_BIC(RXMAC_CONFIG, 1);
	GM_BIC(TXDMA_CONFIG, 1);
	GM_BIC(TXMAC_CONFIG, 1);
	GM_OUT(INTR_DISABLE, ~0U);
	for (i = 0; i < NRX; ++i) {
		if (gm->rx_buff[i] != 0) {
			dev_kfree_skb(gm->rx_buff[i]);
			gm->rx_buff[i] = 0;
		}
	}
	for (i = 0; i < NTX; ++i) {
		if (gm->tx_buff[i] != 0) {
			dev_kfree_skb(gm->tx_buff[i]);
			gm->tx_buff[i] = 0;
		}
	}

	MOD_DEC_USE_COUNT;
	return 0;
}

static int gmac_xmit_start(struct sk_buff *skb, struct net_device *dev)
{
	struct gmac *gm = (struct gmac *) dev->priv;
	volatile struct gmac_dma_desc *dp;
	unsigned long flags;
	int i;

	save_flags(flags); cli();
	i = gm->next_tx;
	if (gm->tx_buff[i] != 0) {
		/* buffer is full, can't send this packet at the moment */
		netif_stop_queue(dev);
		gm->tx_full = 1;
		restore_flags(flags);
		return 1;
	}
	gm->next_tx = (i + 1) & (NTX - 1);
	gm->tx_buff[i] = skb;
	restore_flags(flags);

	dp = &gm->txring[i];
	dp->status = 0;
	dp->hi_addr = 0;
	st_le32(&dp->address, virt_to_bus(skb->data));
	mb();
	st_le32(&dp->cmd, TX_SOP | TX_EOP | skb->len);
	mb();

	GM_OUT(TXDMA_KICK, gm->next_tx);

	return 0;
}

static int gmac_tx_cleanup(struct gmac *gm)
{
	int i = gm->tx_gone;
	volatile struct gmac_dma_desc *dp;
	struct sk_buff *skb;
	int ret = 0;
	int gone = GM_IN(TXDMA_COMPLETE);

	while (i != gone) {
		skb = gm->tx_buff[i];
		if (skb == NULL)
			break;
		dp = &gm->txring[i];
		gm->stats.tx_bytes += skb->len;
		++gm->stats.tx_packets;
		gm->tx_buff[i] = NULL;
		dev_kfree_skb_irq(skb);
		if (++i >= NTX)
			i = 0;
	}
	if (i != gm->tx_gone) {
		ret = gm->tx_full;
		gm->tx_gone = i;
		gm->tx_full = 0;
	}
	return ret;
}

static void gmac_receive(struct net_device *dev)
{
	struct gmac *gm = (struct gmac *) dev->priv;
	int i = gm->next_rx;
	volatile struct gmac_dma_desc *dp;
	struct sk_buff *skb;
	int len;
	unsigned char *data;

	for (;;) {
		dp = &gm->rxring[i];
		if (ld_le32(&dp->cmd) & RX_OWN)
			break;
		len = (ld_le32(&dp->cmd) >> 16) & 0x7fff;
		skb = gm->rx_buff[i];
		if (skb == 0) {
			++gm->stats.rx_dropped;
		} else if (ld_le32(&dp->status) & 0x40000000) {
			++gm->stats.rx_errors;
			dev_kfree_skb_irq(skb);
		} else {
			skb_put(skb, len);
			skb->dev = dev;
			skb->protocol = eth_type_trans(skb, dev);
			netif_rx(skb);
			gm->stats.rx_bytes += skb->len;
			++gm->stats.rx_packets;
		}
		data = dummy_buf;
		gm->rx_buff[i] = skb = dev_alloc_skb(RX_BUFLEN + 2);
		if (skb != 0) {
			/*skb_reserve(skb, 2);*/
			data = skb->data;
		}
		st_le32(&dp->address, virt_to_bus(data));
		dp->hi_addr = 0;
		mb();
		st_le32(&dp->cmd, RX_OWN);
		if (++i >= NRX)
			i = 0;
	}
	gm->next_rx = i;
}

static void gmac_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct net_device *dev = (struct net_device *) dev_id;
	struct gmac *gm = (struct gmac *) dev->priv;
	unsigned int status;

	status = GM_IN(INTR_STATUS);
	GM_OUT(INTR_ACK, status);
	
	if (status & GMAC_IRQ_MIF)
		mii_interrupt(gm);
	gmac_receive(dev);
	if (gmac_tx_cleanup(gm))
		netif_wake_queue(dev);
}

static struct net_device_stats *gmac_stats(struct net_device *dev)
{
	struct gmac *gm = (struct gmac *) dev->priv;

	return &gm->stats;
}

static int __init gmac_probe(void)
{
	struct device_node *gmac;

	/*
	 * We could (and maybe should) do this using PCI scanning
	 * for vendor/net_device ID 0x106b/0x21.
	 */
	for (gmac = find_compatible_devices("network", "gmac"); gmac != 0;
	     gmac = gmac->next)
		gmac_probe1(gmac);

	return 0;
}

static void gmac_probe1(struct device_node *gmac)
{
	struct gmac *gm;
	unsigned long descpage;
	unsigned char *addr;
	struct net_device *dev;
	int i;

	if (gmac->n_addrs < 1 || gmac->n_intrs < 1) {
		printk(KERN_ERR "can't use GMAC %s: %d addrs and %d intrs\n",
		       gmac->full_name, gmac->n_addrs, gmac->n_intrs);
		return;
	}

	addr = get_property(gmac, "local-mac-address", NULL);
	if (addr == NULL) {
		printk(KERN_ERR "Can't get mac-address for GMAC %s\n",
		       gmac->full_name);
		return;
	}

	descpage = get_free_page(GFP_KERNEL);
	if (descpage == 0) {
		printk(KERN_ERR "GMAC: can't get a page for descriptors\n");
		return;
	}

	dev = init_etherdev(0, sizeof(struct gmac));
	memset(dev->priv, 0, sizeof(struct gmac));

	gm = (struct gmac *) dev->priv;
	dev->base_addr = gmac->addrs[0].address;
	gm->regs = (volatile unsigned int *)
		ioremap(gmac->addrs[0].address, 0x10000);
	gm->sysregs = (volatile unsigned int *) ioremap(0xf8000000, 0x1000);
	dev->irq = gmac->intrs[0].line;

	printk(KERN_INFO "%s: GMAC at", dev->name);
	for (i = 0; i < 6; ++i) {
		dev->dev_addr[i] = addr[i];
		printk("%c%.2x", (i? ':': ' '), addr[i]);
	}
	printk("\n");

	gm->desc_page = descpage;
	gm->rxring = (volatile struct gmac_dma_desc *) descpage;
	gm->txring = (volatile struct gmac_dma_desc *) (descpage + 0x800);

	gm->phy_addr = 0;
	
	dev->open = gmac_open;
	dev->stop = gmac_close;
	dev->hard_start_xmit = gmac_xmit_start;
	dev->get_stats = gmac_stats;

	ether_setup(dev);

	if (request_irq(dev->irq, gmac_interrupt, 0, "GMAC", dev))
		printk(KERN_ERR "GMAC: can't get irq %d\n", dev->irq);

	gm->next_gmac = gmacs;
	gmacs = dev;
}

MODULE_AUTHOR("Paul Mackerras/Ben Herrenschmidt");
MODULE_DESCRIPTION("PowerMac GMAC driver.");

static void __exit gmac_cleanup_module(void)
{
	struct gmac *gm;
	struct net_device *dev;

	while ((dev = gmacs) != NULL) {
		gm = (struct gmac *) dev->priv;
		gmacs = gm->next_gmac;
		free_irq(dev->irq, dev);
		free_page(gm->desc_page);
		unregister_netdev(dev);
		kfree(dev);
	}
}

module_init(gmac_probe);
module_exit(gmac_cleanup_module);

