  /*
   * Copyright 1996 The Australian National University.
   * Copyright 1996 Fujitsu Laboratories Limited
   * 
   * This software may be distributed under the terms of the Gnu
   * Public License version 2 or later
  */
/*
 * $Id: bif.c,v 1.13 1996/12/18 01:45:52 tridge Exp $
 *
 * Network interface definitions for bif device.
 */

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
#include <linux/netdevice.h>
#include <linux/if_arp.h>	/* For ARPHRD_BIF */

#include <asm/system.h>
#include <asm/segment.h>
#include <asm/io.h>

#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <net/sock.h>

#include <asm/ap1000/apservice.h>
#include <asm/ap1000/apreg.h>

#define BIF_DEBUG 0
#if BIF_DEBUG
static int seq = 0;
#endif

#define BIF_MTU 10240

static struct device *bif_device = 0;
static struct net_device_stats *bif_stats = 0;

int bif_init(struct device *dev);
int bif_open(struct device *dev);
static int bif_xmit(struct sk_buff *skb, struct device *dev);
int bif_rx(struct sk_buff *skb);
int bif_stop(struct device *dev);
static struct net_device_stats *bif_get_stats(struct device *dev);

static int bif_hard_header(struct sk_buff *skb, struct device *dev,
			   unsigned short type, void *daddr,
			   void *saddr, unsigned len)
{
#if BIF_DEBUG
  printk("bif_hard_header()\n");
#endif
  
  skb_push(skb,dev->hard_header_len);

  if (daddr) skb->arp = 1;

  /* tell IP how much space we took */
  return (dev->hard_header_len);
}

static int bif_rebuild_header(void *buff, struct device *dev,
			      unsigned long raddr, struct sk_buff *skb)
{
  /* this would normally be used to fill in hardware addresses after
     an ARP */
#if BIF_DEBUG
  printk("bif_rebuild_header()\n");
#endif
  if (skb) skb->arp = 1;
  return(0);
}

static int bif_set_mac_address(struct device *dev, void *addr)
{
  printk("BIF: set_mac_address called\n");
  return (0);
}

static void bif_set_multicast_list(struct device *dev)
{
  return;
}

static int bif_do_ioctl(struct device *dev, struct ifreq *ifr, int cmd)
{
  printk("BIF: Called do_ioctl\n");
  return (0);
}

static int bif_set_config(struct device *dev, struct ifmap *map)
{
  printk("BIF: Called bif_set_config\n");
  return (0);
}

/*
 * Initialise bif network interface.
 */
int bif_init(struct device *dev)
{
    int i;

    printk("bif_init(): Initialising bif interface\n");
    bif_device = dev;

    dev->mtu = BIF_MTU;
    dev->tbusy = 0;
    dev->hard_start_xmit = bif_xmit;
    dev->hard_header = bif_hard_header;
    dev->hard_header_len = sizeof(struct cap_request);
    dev->addr_len = 0;
    dev->tx_queue_len = 50000;    /* no limit (almost!) */
    dev->type = ARPHRD_BIF;
    dev->rebuild_header	= bif_rebuild_header;
    dev->open = bif_open;
    dev->flags = IFF_NOARP;   /* Don't use ARP on this device */
    dev->priv = kmalloc(sizeof(struct net_device_stats), GFP_KERNEL);
    if (dev->priv == NULL)
	return -ENOMEM;
    memset(dev->priv, 0, sizeof(struct net_device_stats));
    bif_stats = (struct net_device_stats *)bif_device->priv;


    dev->stop = bif_stop;
    dev->get_stats = bif_get_stats;

    dev->set_mac_address = bif_set_mac_address;
    dev->header_cache_update = NULL;
    dev->do_ioctl = bif_do_ioctl;    
    dev->set_config = bif_set_config;
    dev->set_multicast_list = bif_set_multicast_list;

    memset(dev->broadcast, 0xFF, ETH_ALEN);

    dev_init_buffers(dev);
    
    return(0);
}

int bif_open(struct device *dev)
{
    printk("In bif_open\n");
    dev->tbusy = 0;
    dev->start = 1;
    return 0;
}

#if BIF_DEBUG
static void dump_packet(char *action, char *buf, int len, int seq)
{
    int flags;
    char *sep;

    printk("%s packet %d of %d bytes at %d:\n", action, seq,
	   len, (int)jiffies);
    printk("  from %x to %x pktid=%d ttl=%d pcol=%d len=%d\n",
	   *(long *)(buf+12), *(long *)(buf+16), *(u_short *)(buf+4),
	   *(unsigned char *)(buf+8), buf[9], *(u_short *)(buf+2));
    if( buf[9] == 6 || buf[9] == 17 ){
	/* TCP or UDP */
	printk("  sport=%d dport=%d",
	       *(u_short *)(buf+20), *(u_short *)(buf+22));
	if( buf[9] == 6 ){
	    printk(" seq=%d ack=%d win=%d flags=<",
		   *(long *)(buf+24), *(long *)(buf+28),
		   *(unsigned short *)(buf+34));
	    flags = buf[33];
	    sep = "";
	    printk(">");
	}
	printk("\n");
    }
    else {
	printk("  protocol = %d\n", buf[9]);
    }
}
#endif


static int bif_xmit(struct sk_buff *skb, struct device *dev)
{
	extern int bif_send_ip(int cid,struct sk_buff *skb);
	extern int tnet_send_ip(int cid,struct sk_buff *skb);
	extern int msc_blocked, tnet_ip_enabled;
	u_long destip;
	int cid;

	if (skb == NULL || dev == NULL) 
		return(0);
  
	destip = *(u_long *)(skb->data+sizeof(struct cap_request)+16);
	cid = ap_ip_to_cid(destip);

	skb->dev = dev;
	skb->mac.raw = skb->data;

	if (cid != -1 && tnet_ip_enabled && !msc_blocked) {
		tnet_send_ip(cid,skb);
	} else {
		bif_send_ip(cid, skb);
	}
  
	dev->tbusy = 0;
  
	bif_stats->tx_packets++;

	mark_bh(NET_BH);
  
	return 0;
}


/*
 * Receive a packet from the BIF - called from interrupt handler.
 */
int bif_rx(struct sk_buff *skb)
{
#if BIF_DEBUG
	dump_packet("bif_rx:", skb->data, skb->len, seq++);
#endif

	if (bif_device == NULL) {
		printk("bif: bif_device is NULL in bif_rx\n");
		dev_kfree_skb(skb);
		return 0;
	}
	skb->dev = bif_device;
	skb->protocol = ETH_P_IP;
	
#if 1
	/* try disabling checksums on receive */
	if (ap_ip_to_cid(*(u_long *)(((char *)skb->data)+12)) != -1)
		skb->ip_summed = CHECKSUM_UNNECESSARY;
#endif
	
	/*
	 * Inform the network layer of the new packet.
	 */
	skb->mac.raw = skb->data;
	netif_rx(skb);
	
	if (bif_stats == NULL) {
		printk("bif: bif_stats is NULL is bif_rx\n");
		return 0;
	}
	bif_stats->rx_packets++;
	
	return 0;
}

int bif_stop(struct device *dev)
{
	printk("in bif_close\n");

	dev->tbusy = 1;
	dev->start = 0;

	return 0;
}

/*
 * Return statistics of bif driver.
 */
static struct net_device_stats *bif_get_stats(struct device *dev)
{
    return((struct net_device_stats *)dev->priv);
}
    
