/*
 *	NET/ROM release 003
 *
 *	This is ALPHA test software. This code may break your machine, randomly fail to work with new 
 *	releases, misbehave and/or generally screw up. It might even work. 
 *
 *	This code REQUIRES 1.3.0 or higher/ NET3.029
 *
 *	This module:
 *		This module is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	History
 *	NET/ROM 001	Jonathan(G4KLX)	Cloned from loopback.c
 */

#include <linux/config.h>
#ifdef CONFIG_NETROM
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/in.h>
#include <linux/if_ether.h>	/* For the statistics structure. */

#include <asm/system.h>
#include <asm/segment.h>
#include <asm/io.h>

#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include <net/ip.h>
#include <net/arp.h>

#include <net/ax25.h>
#include <net/netrom.h>

/*
 * Only allow IP over NET/ROM frames through if the netrom device is up.
 */

int nr_rx_ip(struct sk_buff *skb, struct device *dev)
{
	struct enet_statistics *stats = (struct enet_statistics *)dev->priv;

	if (!dev->start) {
		stats->rx_errors++;
		return 0;
	}

	stats->rx_packets++;
	skb->protocol=htons(ETH_P_IP);
	/* Spoof incoming device */
	skb->dev=dev;

	ip_rcv(skb, dev, NULL);

	return 1;
}

/*
 * We can't handle ARP so put some identification characters into the ARP
 * packet so that the transmit routine can identify it, and throw it away.
 */

static int nr_header(struct sk_buff *skb, struct device *dev, unsigned short type,
	void *daddr, void *saddr, unsigned len)
{
	unsigned char *buff=skb_push(skb,37);
	if (type == ETH_P_ARP) {
		*buff++ = 0xFF;		/* Mark it */
		*buff++ = 0xFE;
		return 37;
	}

	buff += 16;
	
	*buff++ = AX25_P_NETROM;
	
	memcpy(buff, (saddr != NULL) ? saddr : dev->dev_addr, dev->addr_len);
	buff[6] &= ~LAPB_C;
	buff[6] &= ~LAPB_E;
	buff[6] |= SSID_SPARE;
	buff += dev->addr_len;

	if (daddr != NULL)
		memcpy(buff, daddr, dev->addr_len);
	buff[6] &= ~LAPB_C;
	buff[6] |= LAPB_E;
	buff[6] |= SSID_SPARE;
	buff += dev->addr_len;

	*buff++ = nr_default.ttl;

	*buff++ = NR_PROTO_IP;
	*buff++ = NR_PROTO_IP;
	*buff++ = 0;
	*buff++ = 0;
	*buff++ = NR_PROTOEXT;

	if (daddr != NULL)
		return 37;
	
	return -37;	
}

static int nr_rebuild_header(void *buff, struct device *dev,
	unsigned long raddr, struct sk_buff *skb)
{
	unsigned char *bp = (unsigned char *)buff;

	if (arp_find(bp + 24, raddr, dev, dev->pa_addr, skb))
		return 1;

	bp[23] &= ~LAPB_C;
	bp[23] &= ~LAPB_E;
	bp[23] |= SSID_SPARE;
	
	bp[30] &= ~LAPB_C;
	bp[30] |= LAPB_E;
	bp[30] |= SSID_SPARE;

	return 0;
}

static int nr_set_mac_address(struct device *dev, void *addr)
{
	memcpy(dev->dev_addr, addr, dev->addr_len);

	return 0;
}

static int nr_open(struct device *dev)
{
	dev->tbusy = 0;
	dev->start = 1;

	return 0;
}

static int nr_close(struct device *dev)
{
	dev->tbusy = 1;
	dev->start = 0;

	return 0;
}

static int nr_xmit(struct sk_buff *skb, struct device *dev)
{
	struct enet_statistics *stats = (struct enet_statistics *)dev->priv;
	struct sk_buff *skbn;

	if (skb == NULL || dev == NULL)
		return 0;

	if (!dev->start) {
		printk("netrom: xmit call when iface is down\n");
		return 1;
	}

	cli();

	if (dev->tbusy != 0) {
		sti();
		stats->tx_errors++;
		return 1;
	}

	dev->tbusy = 1;

	sti();

	if (skb->data[0] != 0xFF && skb->data[1] != 0xFE) {
		if ((skbn = skb_clone(skb, GFP_ATOMIC)) == NULL) {
			dev->tbusy = 0;
			stats->tx_errors++;
			return 1;
		}

		if (!nr_route_frame(skbn, NULL)) {
			skbn->free = 1;
			kfree_skb(skbn, FREE_WRITE);
			dev->tbusy = 0;
			stats->tx_errors++;
			return 1;
		}
	}

	dev_kfree_skb(skb, FREE_WRITE);

	stats->tx_packets++;

	dev->tbusy = 0;

	mark_bh(NET_BH);

	return 0;
}

static struct enet_statistics *nr_get_stats(struct device *dev)
{
	return (struct enet_statistics *)dev->priv;
}

int nr_init(struct device *dev)
{
	int i;

	dev->mtu		= 236;		/* MTU			*/
	dev->tbusy		= 0;
	dev->hard_start_xmit	= nr_xmit;
	dev->open		= nr_open;
	dev->stop		= nr_close;

	dev->hard_header	= nr_header;
	dev->hard_header_len	= 37;
	dev->addr_len		= 7;
	dev->type		= ARPHRD_NETROM;
	dev->rebuild_header	= nr_rebuild_header;
	dev->set_mac_address    = nr_set_mac_address;

	/* New-style flags. */
	dev->flags		= 0;
	dev->family		= AF_INET;

	dev->pa_addr		= 0;
	dev->pa_brdaddr		= 0;
	dev->pa_mask		= 0;
	dev->pa_alen		= sizeof(unsigned long);

	dev->priv = kmalloc(sizeof(struct enet_statistics), GFP_KERNEL);

	memset(dev->priv, 0, sizeof(struct enet_statistics));

	dev->get_stats = nr_get_stats;

	/* Fill in the generic fields of the device structure. */
	for (i = 0; i < DEV_NUMBUFFS; i++)
		skb_queue_head_init(&dev->buffs[i]);

	return 0;
};

#endif
