/* tunnel.c: an IP tunnel driver

	The purpose of this driver is to provide an IP tunnel through
	which you can tunnel network traffic transparently across subnets.

	This was written by looking at Nick Holloway's dummy driver
	Thanks for the great code!

		-Sam Lantinga	(slouken@cs.ucdavis.edu)  02/01/95
		
	Minor tweaks:
		Cleaned up the code a little and added some pre-1.3.0 tweaks.
		dev->hard_header/hard_header_len changed to use no headers.
		Comments/bracketing tweaked.
		Made the tunnels use dev->name not tunnel: when error reporting.
		Added tx_dropped stat
		
		-Alan Cox	(Alan.Cox@linux.org) 21 March 95

	Reworked:
		Changed to tunnel to destination gateway in addition to the
			tunnel's pointopoint address
		Almost completely rewritten
		Note:  There is currently no firewall or ICMP handling done.

		-Sam Lantinga	(slouken@cs.ucdavis.edu) 02/13/96
		
	Note:
		The old driver is in tunnel.c if you have funnies with the
		new one.
*/

/* Things I wish I had known when writing the tunnel driver:

	When the tunnel_xmit() function is called, the skb contains the
	packet to be sent (plus a great deal of extra info), and dev
	contains the tunnel device that _we_ are.

	When we are passed a packet, we are expected to fill in the
	source address with our source IP address.

	What is the proper way to allocate, copy and free a buffer?
	After you allocate it, it is a "0 length" chunk of memory
	starting at zero.  If you want to add headers to the buffer
	later, you'll have to call "skb_reserve(skb, amount)" with
	the amount of memory you want reserved.  Then, you call
	"skb_put(skb, amount)" with the amount of space you want in
	the buffer.  skb_put() returns a pointer to the top (#0) of
	that buffer.  skb->len is set to the amount of space you have
	"allocated" with skb_put().  You can then write up to skb->len
	bytes to that buffer.  If you need more, you can call skb_put()
	again with the additional amount of space you need.  You can
	find out how much more space you can allocate by calling 
	"skb_tailroom(skb)".
	Now, to add header space, call "skb_push(skb, header_len)".
	This creates space at the beginning of the buffer and returns
	a pointer to this new space.  If later you need to strip a
	header from a buffer, call "skb_pull(skb, header_len)".
	skb_headroom() will return how much space is left at the top
	of the buffer (before the main data).  Remember, this headroom
	space must be reserved before the skb_put() function is called.
*/

#include <linux/module.h>
#include <linux/config.h>	/* for CONFIG_IP_FORWARD */

/* Only two headers!! :-) */
#include <net/ip.h>
#include <linux/if_arp.h>


/*#define TUNNEL_DEBUG*/

/* 
 *	Our header is a simple IP packet with no options
 */
 
#define tunnel_hlen	sizeof(struct iphdr)

/*
 *	Okay, this needs to be high enough that we can fit a "standard"
 *	ethernet header and an IP tunnel header into the outgoing packet.
 *	[36 bytes]
 */
 
#define TUNL_HLEN	(((ETH_HLEN+15)&~15)+tunnel_hlen)


static int tunnel_open(struct device *dev)
{
	MOD_INC_USE_COUNT;
	return 0;
}

static int tunnel_close(struct device *dev)
{
	MOD_DEC_USE_COUNT;
	return 0;
}

#ifdef TUNNEL_DEBUG
void print_ip(struct iphdr *ip)
{
	unsigned char *ipaddr;

	printk("IP packet:\n");
	printk("--- header len = %d\n", ip->ihl*4);
	printk("--- ip version: %d\n", ip->version);
	printk("--- ip protocol: %d\n", ip->protocol);
	ipaddr=(unsigned char *)&ip->saddr;
	printk("--- source address: %u.%u.%u.%u\n", 
			*ipaddr, *(ipaddr+1), *(ipaddr+2), *(ipaddr+3));
	ipaddr=(unsigned char *)&ip->daddr;
	printk("--- destination address: %u.%u.%u.%u\n", 
			*ipaddr, *(ipaddr+1), *(ipaddr+2), *(ipaddr+3));
	printk("--- total packet len: %d\n", ntohs(ip->tot_len));
}
#endif

/*
 *	This function assumes it is being called from dev_queue_xmit()
 *	and that skb is filled properly by that function.
 */

static int tunnel_xmit(struct sk_buff *skb, struct device *dev)
{
	struct enet_statistics *stats;		/* This device's statistics */
	struct rtable *rt;     			/* Route to the other host */
	struct device *tdev;			/* Device to other host */
	struct iphdr  *iph;			/* Our new IP header */
	__u32          target;			/* The other host's IP address */
	int      max_headroom;			/* The extra header space needed */

	/*
	 *	Return if there is nothing to do.  (Does this ever happen?)
	 */
	if (skb == NULL || dev == NULL) {
#ifdef TUNNEL_DEBUG
		printk ( KERN_INFO "tunnel: Nothing to do!\n" );
#endif
		return 0;
	}

	/* 
	 *	Make sure we are not busy (check lock variable) 
	 */
	 
	stats = (struct enet_statistics *)dev->priv;
	cli();
	if (dev->tbusy != 0) 
	{
		sti();
		stats->tx_errors++;
		return(1);
	}
	dev->tbusy = 1;
	sti();
  
	/*printk("-");*/
	/*
	 *  First things first.  Look up the destination address in the 
	 *  routing tables
	 */
	iph = (struct iphdr *) skb->data;
	if ((rt = ip_rt_route(iph->daddr, 0)) == NULL)
	{ 
		/* No route to host */
		/* Where did the packet come from? */
		/*icmp_send(skb, ICMP_DEST_UNREACH, ICMP_NET_UNREACH, 0, dev);*/
		printk ( KERN_INFO "%s: Packet with no route!\n", dev->name);
		dev->tbusy=0;
		stats->tx_errors++;
		dev_kfree_skb(skb, FREE_WRITE);
		return 0;
	}

	/*
	 * Get the target address (other end of IP tunnel)
	 */
	if (rt->rt_flags & RTF_GATEWAY)
		target = rt->rt_gateway;
	else
		target = dev->pa_dstaddr;

	if ( ! target )
	{	/* No gateway to tunnel through? */
		/* Where did the packet come from? */
		/*icmp_send(skb, ICMP_DEST_UNREACH, ICMP_NET_UNREACH, 0, dev);*/
		printk ( KERN_INFO "%s: Packet with no target gateway!\n", dev->name);
		ip_rt_put(rt);
		dev->tbusy=0;
		stats->tx_errors++;
		dev_kfree_skb(skb, FREE_WRITE);
		return 0;
	}
	ip_rt_put(rt);

	if ((rt = ip_rt_route(target, 0)) == NULL)
	{ 
		/* No route to host */
		/* Where did the packet come from? */
		/*icmp_send(skb, ICMP_DEST_UNREACH, ICMP_NET_UNREACH, 0, dev);*/
		printk ( KERN_INFO "%s: Can't reach target gateway!\n", dev->name);
		dev->tbusy=0;
		stats->tx_errors++;
		dev_kfree_skb(skb, FREE_WRITE);
		return 0;
	}
	tdev = rt->rt_dev;

	if (tdev == dev)
	{ 
		/* Tunnel to ourselves?  -- I don't think so. */
		printk ( KERN_INFO "%s: Packet targetted at myself!\n" , dev->name);
		ip_rt_put(rt);
		dev->tbusy=0;
		stats->tx_errors++;
		dev_kfree_skb(skb, FREE_WRITE);
		return 0;
	}

#ifdef TUNNEL_DEBUG
	printk("Old IP Header....\n");
	print_ip(iph);
#endif

	/*
	 * Okay, now see if we can stuff it in the buffer as-is.
	 */
	max_headroom = (((tdev->hard_header_len+15)&~15)+tunnel_hlen);
#ifdef TUNNEL_DEBUG
printk("Room left at head: %d\n", skb_headroom(skb));
printk("Room left at tail: %d\n", skb_tailroom(skb));
printk("Required room: %d, Tunnel hlen: %d\n", max_headroom, TUNL_HLEN);
#endif
	if (skb_headroom(skb) >= max_headroom) {
		skb->h.iph = (struct iphdr *) skb_push(skb, tunnel_hlen);
	} else {
		struct sk_buff *new_skb;

		if ( !(new_skb = dev_alloc_skb(skb->len+max_headroom)) ) 
		{
			printk( KERN_INFO "%s: Out of memory, dropped packet\n",
				dev->name);
			ip_rt_put(rt);
  			dev->tbusy = 0;
  			stats->tx_dropped++;
			dev_kfree_skb(skb, FREE_WRITE);
			return 0;
		}
		new_skb->free = 1;

		/*
		 * Reserve space for our header and the lower device header
		 */
		skb_reserve(new_skb, max_headroom);

		/*
		 * Copy the old packet to the new buffer.
		 * Note that new_skb->h.iph will be our (tunnel driver's) header
		 * and new_skb->ip_hdr is the IP header of the old packet.
		 */
		new_skb->ip_hdr = (struct iphdr *) skb_put(new_skb, skb->len);
		new_skb->dev = skb->dev;
		memcpy(new_skb->ip_hdr, skb->data, skb->len);
		memset(new_skb->proto_priv, 0, sizeof(skb->proto_priv));

		/* Tack on our header */
		new_skb->h.iph = (struct iphdr *) skb_push(new_skb, tunnel_hlen);

		/* Free the old packet, we no longer need it */
		dev_kfree_skb(skb, FREE_WRITE);
		skb = new_skb;
	}

	/*
	 *	Push down and install the IPIP header.
	 */
	 
	iph 			=	skb->h.iph;
	iph->version		= 	4;
	iph->tos		=	skb->ip_hdr->tos;
	iph->ttl		=	skb->ip_hdr->ttl;
	iph->frag_off		=	0;
	iph->daddr		=	target;
	iph->saddr		=	tdev->pa_addr;
	iph->protocol		=	IPPROTO_IPIP;
	iph->ihl		=	5;
	iph->tot_len		=	htons(skb->len);
	iph->id			=	htons(ip_id_count++);	/* Race condition here? */
	ip_send_check(iph);
	skb->ip_hdr 		= skb->h.iph;
	skb->protocol		=	htons(ETH_P_IP);
#ifdef TUNNEL_DEBUG
	printk("New IP Header....\n");
	print_ip(iph);
#endif

	/*
	 *	Send the packet on its way!
	 *	Note that dev_queue_xmit() will eventually free the skb.
	 *	If ip_forward() made a copy, it will return 1 so we can free.
	 */

#ifdef CONFIG_IP_FORWARD
	if (ip_forward(skb, dev, 0, target))
#endif
		kfree_skb(skb, FREE_WRITE);

	/*
	 *	Clean up:  We're done with the route and the packet
	 */
	 
	ip_rt_put(rt);
 
#ifdef TUNNEL_DEBUG
	printk("Packet sent through tunnel interface!\n");
#endif
/*printk(">");*/
	/* Record statistics and return */
	stats->tx_packets++;
	dev->tbusy=0;
	return 0;
}

static struct enet_statistics *tunnel_get_stats(struct device *dev)
{
	return((struct enet_statistics*) dev->priv);
}

/*
 *	Called when a new tunnel device is initialized.
 *	The new tunnel device structure is passed to us.
 */
 
int tunnel_init(struct device *dev)
{
	int i;

	/* Oh, just say we're here, in case anyone cares */
	static int tun_msg=0;
	if(!tun_msg)
	{
		printk ( KERN_INFO "tunnel: version v0.2b2\n" );
		tun_msg=1;
	}

	/* Add our tunnel functions to the device */
	dev->open		= tunnel_open;
	dev->stop		= tunnel_close;
	dev->hard_start_xmit	= tunnel_xmit;
	dev->get_stats		= tunnel_get_stats;
	dev->priv = kmalloc(sizeof(struct enet_statistics), GFP_KERNEL);
	if (dev->priv == NULL)
		return -ENOMEM;
	memset(dev->priv, 0, sizeof(struct enet_statistics));

	/* Initialize the tunnel device structure */
	for (i = 0; i < DEV_NUMBUFFS; i++)
		skb_queue_head_init(&dev->buffs[i]);

	dev->hard_header	= NULL;
	dev->rebuild_header 	= NULL;
	dev->set_mac_address 	= NULL;
	dev->header_cache_bind 	= NULL;
	dev->header_cache_update= NULL;

	dev->type				= ARPHRD_TUNNEL;
	dev->hard_header_len 	= TUNL_HLEN;
	dev->mtu		= 1500-tunnel_hlen; 	/* eth_mtu */
	dev->addr_len		= 0;		/* Is this only for ARP? */
	dev->tx_queue_len	= 2;		/* Small queue */
	memset(dev->broadcast,0xFF, ETH_ALEN);

	/* New-style flags. */
	dev->flags		= IFF_NOARP;	/* Don't use ARP on this device */
						/* No broadcasting through a tunnel */
	dev->family		= AF_INET;
	dev->pa_addr		= 0;
	dev->pa_brdaddr 	= 0;
	dev->pa_mask		= 0;
	dev->pa_alen		= 4;

	/* We're done.  Have I forgotten anything? */
	return 0;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*  Module specific interface                                      */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#ifdef MODULE

static int tunnel_probe(struct device *dev)
{
	tunnel_init(dev);
	return 0;
}

static struct device dev_tunnel = 
{
	"tunl0\0   ", 
	0, 0, 0, 0,
 	0x0, 0,
 	0, 0, 0, NULL, tunnel_probe 
 };

int init_module(void)
{
	/* Find a name for this unit */
	int ct= 1;
	
	while(dev_get(dev_tunnel.name)!=NULL && ct<100)
	{
		sprintf(dev_tunnel.name,"tunl%d",ct);
		ct++;
	}
	
#ifdef TUNNEL_DEBUG
	printk("tunnel: registering device %s\n", dev_tunnel.name);
#endif
	if (register_netdev(&dev_tunnel) != 0)
		return -EIO;
	return 0;
}

void cleanup_module(void)
{
	unregister_netdev(&dev_tunnel);
	kfree_s(dev_tunnel.priv,sizeof(struct enet_statistics));
	dev_tunnel.priv=NULL;
}
#endif /* MODULE */

