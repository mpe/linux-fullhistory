/*
 *	IPv6 over IPv4 tunnel device - Simple Internet Transition (SIT)
 *	Linux INET6 implementation
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/sched.h>
#include <linux/net.h>
#include <linux/in6.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/icmp.h>

#include <net/sock.h>
#include <net/snmp.h>

#include <net/ipv6.h>
#include <net/protocol.h>
#include <net/transp_v6.h>
#include <net/ndisc.h>
#include <net/ipv6_route.h>
#include <net/addrconf.h>
#include <net/ip.h>
#include <net/udp.h>
#include <net/sit.h>


static int sit_init_dev(struct device *dev);

static struct device sit_device = {
	"sit0",
	0, 0, 0, 0,
	0x0, 0,
	0, 0, 0, NULL, sit_init_dev
};

static unsigned long		sit_gc_last_run;
static void			sit_mtu_cache_gc(void);

static int			sit_xmit(struct sk_buff *skb, 
					 struct device *dev);
static int			sit_rcv(struct sk_buff *skb, 
					struct device *dev, 
					struct options *opt,
					__u32 daddr, unsigned short len,
					__u32 saddr, int redo, 
					struct inet_protocol * protocol);

static int			sit_open(struct device *dev);
static int			sit_close(struct device *dev);

static struct enet_statistics *	sit_get_stats(struct device *dev);

static void			sit_err(int type, int code, 
					unsigned char *buff, __u32 info,
					__u32 daddr, __u32 saddr,
					struct inet_protocol *protocol);

static struct inet_protocol sit_protocol = {
	sit_rcv,
	sit_err,
	0,
	IPPROTO_IPV6,
	0,
	NULL,
	"IPv6"
};

#define SIT_NUM_BUCKETS	16

struct sit_mtu_info *sit_mtu_cache[SIT_NUM_BUCKETS];

static int		vif_num = 0;
static struct sit_vif	*vif_list = NULL;

static __inline__ __u32 sit_addr_hash(__u32 addr)
{
        
        __u32 hash_val;
        
        hash_val = addr;

        hash_val ^= hash_val >> 16;
	hash_val ^= hash_val >> 8;
        
        return (hash_val & (SIT_NUM_BUCKETS - 1));
}

static void sit_cache_insert(__u32 addr, int mtu)
{
	struct sit_mtu_info *minfo;
	int hash;
	
	minfo = kmalloc(sizeof(struct sit_mtu_info), GFP_ATOMIC);
	
	if (minfo == NULL)
		return;

	minfo->addr = addr;
	minfo->tstamp = jiffies;
	minfo->mtu = mtu;

	hash = sit_addr_hash(addr);

	minfo->next = sit_mtu_cache[hash];
	sit_mtu_cache[hash] = minfo;
}

static struct sit_mtu_info * sit_mtu_lookup(__u32 addr)
{
	struct sit_mtu_info *iter;
	int hash;

	hash = sit_addr_hash(addr);

	for(iter = sit_mtu_cache[hash]; iter; iter=iter->next)
	{
		if (iter->addr == addr)
		{
			iter->tstamp = jiffies;
			break;
		}
	}

	/*
	 *	run garbage collector
	 */

	if (jiffies - sit_gc_last_run > SIT_GC_FREQUENCY)
	{
		sit_mtu_cache_gc();
		sit_gc_last_run = jiffies;
	}

	return iter;
}

static void sit_mtu_cache_gc(void)
{
	struct sit_mtu_info *iter, *back;
	unsigned long now = jiffies;
	int i;

	for (i=0; i < SIT_NUM_BUCKETS; i++)
	{
		back = NULL;
		for (iter = sit_mtu_cache[i]; iter;)
		{
			if (now - iter->tstamp > SIT_GC_TIMEOUT)
			{
				struct sit_mtu_info *old;

				old = iter;
				iter = iter->next;

				if (back)
				{
					back->next = iter;
				}
				else
				{
					sit_mtu_cache[i] = iter;
				}

				kfree(old);
				continue;
			}
			back = iter;
			iter = iter->next;
		}
	}
}

static int sit_init_dev(struct device *dev)
{
	int i;

	dev->open	= sit_open;
	dev->stop	= sit_close;

	dev->hard_start_xmit	= sit_xmit;
	dev->get_stats		= sit_get_stats;

	dev->priv = kmalloc(sizeof(struct enet_statistics), GFP_KERNEL);

	if (dev->priv == NULL)
		return -ENOMEM;

	memset(dev->priv, 0, sizeof(struct enet_statistics));


	for (i = 0; i < DEV_NUMBUFFS; i++)
		skb_queue_head_init(&dev->buffs[i]);

	dev->hard_header	= NULL;
	dev->rebuild_header 	= NULL;
	dev->set_mac_address 	= NULL;
	dev->header_cache_bind 	= NULL;
	dev->header_cache_update= NULL;

	dev->type		= ARPHRD_SIT;

	dev->hard_header_len 	= MAX_HEADER;
	dev->mtu		= 1500 - sizeof(struct iphdr);
	dev->addr_len		= 0;
	dev->tx_queue_len	= 2;

	memset(dev->broadcast, 0, MAX_ADDR_LEN);
	memset(dev->dev_addr,  0, MAX_ADDR_LEN);

	dev->flags		= IFF_NOARP;	

	dev->family		= AF_INET6;
	dev->pa_addr		= 0;
	dev->pa_brdaddr 	= 0;
	dev->pa_dstaddr		= 0;
	dev->pa_mask		= 0;
	dev->pa_alen		= 4;

	return 0;
}

static int sit_init_vif(struct device *dev)
{
	int i;

	dev->flags = IFF_NOARP|IFF_POINTOPOINT|IFF_MULTICAST;
	dev->priv = kmalloc(sizeof(struct enet_statistics), GFP_KERNEL);

	if (dev->priv == NULL)
		return -ENOMEM;

	memset(dev->priv, 0, sizeof(struct enet_statistics));

	for (i = 0; i < DEV_NUMBUFFS; i++)
		skb_queue_head_init(&dev->buffs[i]);
		
	return 0;
}

static int sit_open(struct device *dev)
{
	return 0;
}

static int sit_close(struct device *dev)
{
	return 0;
}


int sit_init(void)
{
	int i;

	/* register device */

	if (register_netdev(&sit_device) != 0)
	{
		return -EIO;
	}

	inet_add_protocol(&sit_protocol);

	for (i=0; i < SIT_NUM_BUCKETS; i++)
		sit_mtu_cache[i] = NULL;

	sit_gc_last_run = jiffies;

	return 0;
}

struct device *sit_add_tunnel(__u32 dstaddr)
{
	struct sit_vif *vif;
	struct device *dev;
	
	vif = kmalloc(sizeof(struct sit_vif), GFP_KERNEL);
	if (vif == NULL)
		return NULL;
	
	/*
	 *	Create PtoP configured tunnel
	 */
	
	dev = kmalloc(sizeof(struct device), GFP_KERNEL);
	if (dev == NULL)
		return NULL;

	memcpy(dev, &sit_device, sizeof(struct device));
	dev->init = sit_init_vif;
	dev->pa_dstaddr = dstaddr;

	dev->name = vif->name;
	sprintf(vif->name, "sit%d", ++vif_num);

	register_netdev(dev);

	vif->dev = dev;
	vif->next = vif_list;
	vif_list = vif;

	return dev;
}

void sit_cleanup(void)
{
	struct sit_vif *vif;
	
	for (vif = vif_list; vif;)
	{
		struct device *dev = vif->dev;
		struct sit_vif *cur;

		unregister_netdev(dev);
		kfree(dev->priv);
		kfree(dev);
		
		cur = vif;
		vif = vif->next;
	}

	vif_list = NULL;

	unregister_netdev(&sit_device);
	inet_del_protocol(&sit_protocol);
	
}



/*
 *	receive IPv4 ICMP messages
 */

static void sit_err(int type, int code, unsigned char *buff, __u32 info,
		    __u32 daddr, __u32 saddr, struct inet_protocol *protocol)
		    
{
	if (type == ICMP_DEST_UNREACH && code == ICMP_FRAG_NEEDED)
	{
		struct sit_mtu_info *minfo;

		info -= sizeof(struct iphdr);

		minfo = sit_mtu_lookup(daddr);

		printk(KERN_DEBUG "sit: %08lx pmtu = %ul\n", ntohl(saddr),
		       info);
		if (minfo == NULL)
		{
			minfo = kmalloc(sizeof(struct sit_mtu_info),
					GFP_ATOMIC);

			if (minfo == NULL)
				return;

			start_bh_atomic();
			sit_cache_insert(daddr, info);
			end_bh_atomic();
		}
		else
		{
			minfo->mtu = info;
		}
	}
}

static int sit_rcv(struct sk_buff *skb, struct device *idev, 
		   struct options *opt,
		   __u32 daddr, unsigned short len,
		   __u32 saddr, int redo, struct inet_protocol * protocol)
{
	struct enet_statistics *stats;
	struct device *dev = NULL;
	struct sit_vif *vif;	
       
	skb->h.raw = skb_pull(skb, skb->h.raw - skb->data);
	skb->protocol = __constant_htons(ETH_P_IPV6);

	for (vif = vif_list; vif; vif = vif->next)
	{
		if (saddr == vif->dev->pa_dstaddr)
		{
			dev = vif->dev;
			break;
		}
	}

	if (dev == NULL)
	{
		dev = &sit_device;
	}

	skb->dev = dev;
	skb->ip_summed = CHECKSUM_NONE;

	stats = (struct enet_statistics *)dev->priv;
	stats->rx_packets++;

	ipv6_rcv(skb, dev, NULL);
	return 0;
}

static int sit_xmit(struct sk_buff *skb, struct device *dev)
{
	struct enet_statistics *stats;
	struct sit_mtu_info *minfo;
	struct in6_addr *addr6;	
	unsigned long flags;
	struct rtable *rt;
	struct iphdr *iph;
	__u32 saddr;
	__u32 daddr;
	__u32 raddr;
	int addr_type;
	int mtu;
	int len;

	/* 
	 *	Make sure we are not busy (check lock variable) 
	 */

	stats = (struct enet_statistics *)dev->priv;
	save_flags(flags);
	cli();
	if (dev->tbusy != 0) 
	{
		restore_flags(flags);
		printk(KERN_DEBUG "sit_xmit: busy\n");
		return(1);
	}
	dev->tbusy = 1;
	restore_flags(flags);

	daddr = dev->pa_dstaddr;
	if (daddr == 0)
	{
		addr6 = &skb->ipv6_hdr->daddr;
		addr_type = ipv6_addr_type(addr6);

		if ((addr_type & IPV6_ADDR_COMPATv4) == 0)
		{
			printk(KERN_DEBUG "sit_xmit: non v4 address\n");
			goto on_error;
		}
	}

	len = skb->tail - (skb->data + sizeof(struct ipv6hdr));

	if (skb->sk)
	{
		atomic_sub(skb->truesize, &skb->sk->wmem_alloc);
	}

	skb->sk = NULL;
		
	iph = (struct iphdr *) skb_push(skb, sizeof(struct iphdr));
	
	skb->protocol = htons(ETH_P_IP);

	/* get route */

	rt = ip_rt_route(daddr, skb->localroute);
	
	if (rt == NULL)
	{
		printk(KERN_DEBUG "sit: no route to host\n");
		goto on_error;
	}

	minfo = sit_mtu_lookup(daddr);

	if (minfo)
		mtu = minfo->mtu;
	else
		mtu = rt->rt_dev->mtu;

	if (mtu > 576 && len > mtu)
	{
		icmpv6_send(skb, ICMPV6_PKT_TOOBIG, 0, mtu, dev);
		goto on_error;
	}

	saddr = rt->rt_src;
	skb->dev = rt->rt_dev;
	raddr = rt->rt_gateway;	

	if (raddr == 0)
		raddr = daddr;

	/* now for the device header */

	skb->arp = 1;
		
	if (skb->dev->hard_header_len) 
	{
		int mac;

		if (skb->data - skb->head < skb->dev->hard_header_len)
		{
			printk(KERN_DEBUG "sit: space at head < dev header\n");
			goto on_error;
		}

		if (skb->dev->hard_header)
		{
			mac = skb->dev->hard_header(skb, skb->dev, ETH_P_IP, 
					       NULL, NULL, len);

			if (mac < 0)
				skb->arp = 0;

			skb->raddr = raddr;		
		}
		
	}

	ip_rt_put(rt);


	iph->version  = 4;
	iph->ihl      = 5;
	iph->tos      = 0;				/* tos set to 0... */

	if (mtu > 576)
	{
		iph->frag_off = htons(IP_DF);
	}
	else
		iph->frag_off = 0;

	iph->ttl      = 64;
	iph->saddr    = saddr;
	iph->daddr    = daddr;
	iph->protocol = IPPROTO_IPV6;
	skb->ip_hdr   = iph;

	ip_send_check(iph);

	ip_queue_xmit(NULL, skb->dev, skb, 1);

	stats->tx_packets++;
	dev->tbusy=0;

	return 0;

  on_error:
	kfree_skb(skb, FREE_WRITE);
	dev->tbusy=0;
	stats->tx_errors++;
	return 0;	
}

static struct enet_statistics *sit_get_stats(struct device *dev)
{
	return((struct enet_statistics*) dev->priv);
}


/*
 * Local variables:
 *  compile-command: "gcc -D__KERNEL__ -I/usr/src/linux/include -Wall -Wstrict-prototypes -O2 -fomit-frame-pointer -fno-strength-reduce -pipe -m486 -DCPU=486 -DMODULE -DMODVERSIONS -include /usr/src/linux/include/linux/modversions.h  -c -o sit.o sit.c"
 * c-file-style: "Linux"
 * End:
 */
