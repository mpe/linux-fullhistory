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
*/

#include <linux/config.h>
#ifdef CONFIG_IP_FORWARD
#ifdef MODULE
#include <linux/module.h>
#include <linux/version.h>
#endif

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <netinet/in.h>
#include <linux/ip.h>
#include <linux/string.h>
#include <asm/system.h>
#include <linux/errno.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/skbuff.h>
#include <net/ip.h>

#include <net/checksum.h>		/* If using 1.3.0-pre net code */

#define ip_header_len	sizeof(struct iphdr)

static int tunnel_xmit(struct sk_buff *skb, struct device *dev);
static struct enet_statistics *tunnel_get_stats(struct device *dev);

#ifdef MODULE
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

#endif


int tunnel_init(struct device *dev)
{
	static int tun_msg=0;
	if(!tun_msg)
	{
		printk ( KERN_INFO "tunnel: version v0.1a\n" );
		tun_msg=1;
	}

	/* Fill in fields of the dev structure with ethernet-generic values. */
	ether_setup(dev);

	/* Custom initialize the device structure. */
	dev->hard_start_xmit = tunnel_xmit;
	dev->get_stats = tunnel_get_stats;
	dev->priv = kmalloc(sizeof(struct enet_statistics), GFP_KERNEL);
	if (dev->priv == NULL)
		return -ENOMEM;
	memset(dev->priv, 0, sizeof(struct enet_statistics));
#ifdef MODULE
	dev->open = &tunnel_open;
	dev->stop = &tunnel_close;
#endif
	/* Now stomp the bits that are different */
	dev->type = ARPHRD_TUNNEL; /* IP tunnel hardware type (Linux 1.1.89) */
	dev->flags |= IFF_NOARP;
	dev->flags |= IFF_LOOPBACK; /* Why doesn't tunnel work without this? [ should do now - AC]*/
	dev->addr_len=0;
	dev->hard_header_len=0;
	dev->hard_header=NULL;
	dev->header_cache=NULL;
	dev->rebuild_header=NULL;
	/* End of stomp 8) */
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

/* This function assumes it is being called from dev_queue_xmit()
   and that skb is filled properly by that function.
   We also presume that if we return 0, we need to free skb, but
   if we return 1, we don't free anything.  Right?  Wrong?
*/

static int tunnel_xmit(struct sk_buff *skb, struct device *dev)
{
	struct enet_statistics *stats;
	struct sk_buff *skb2;		/* The output packet */
	int newlen;			/* The length of skb2->data */
	struct iphdr *iph;		/* Our new IP header */

	/*
	 *	Return if there is nothing to do 
	 */
	 
	if (skb == NULL || dev == NULL)
		return 0;

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
  
	/*
	 *	Perform some sanity checks on the packet 
	 */
	 
	if ( ! dev->pa_dstaddr ) 
	{
		printk("%s: packet sent through tunnel to never-never land!\n", dev->name);
  		dev_kfree_skb(skb, FREE_WRITE);
		dev->tbusy = 0;
		return(1);
	}

	iph=(struct iphdr *)skb->data;
	if ( iph->version != 4 ) 
	{  
		/* 
		 *	Bad IP packet? Possibly an ARP packet 
		 */
		printk("%s: Bad IP packet: ip version %d\n", dev->name, iph->version);
		dev_kfree_skb(skb, FREE_WRITE);
		dev->tbusy = 0;
		return(0);
	}


	/* 
	 *	Check for routing loops 
	 */
	 
	if ( iph->protocol == IPPROTO_IPIP && iph->saddr == dev->pa_addr ) 
	{
		/* 
		 *	We really should do an ICMP reply here... 
		 */
		printk("%s: Warning: IP routing loop!\n", dev->name);
		dev->tbusy = 0;
		dev_kfree_skb(skb, FREE_WRITE);
		return(0);
	}

	if ( iph->daddr == dev->pa_addr ) 
	{
		printk("%s: Received inbound packet -- not handled.\n",dev->name);
  		dev_kfree_skb(skb, FREE_WRITE);
		dev->tbusy = 0;
		return(0);
	}

#ifdef TUNNEL_DEBUG
printk("Old IP Header....\n");
print_ip(iph);
#endif
	/*
	 * Everything is okay: 
	 *		See if we need to allocate memory for a new packet 
	 */

	newlen = (skb->len + ip_header_len);
	if ( !(skb2 = dev_alloc_skb(newlen)) ) 
	{
		printk("%s: No free memory.\n",dev->name);
  		dev_kfree_skb(skb, FREE_WRITE);
  		dev->tbusy = 0;
  		stats->tx_dropped++;
		return(1);
	}

	/* Copy the packet to a new buffer, adding a new ip header */
	skb2->free=1;
	skb_put(skb2,newlen);
	iph=skb2->h.iph=(struct iphdr *)skb2->data;
	skb2->ip_hdr=iph;
	memcpy(skb2->h.iph, skb->data, ip_header_len );
	memcpy(skb2->data + ip_header_len, skb->data, skb->len);
	/* Free the old packet, we no longer need it */
	dev_kfree_skb(skb, FREE_WRITE);

	/* Correct the fields in the new ip header */
	++iph->ttl;	/* Note: ip_forward() decrements ttl, so compensate */
	iph->saddr = dev->pa_addr;
	iph->daddr = dev->pa_dstaddr;
	iph->protocol = IPPROTO_IPIP;
	iph->ihl = 5;
	iph->tot_len = htons(skb2->len);
	iph->frag_off = 0;

	/* Here is where we compute the IP checksum */
	/* ip_fast_csum() is an inline function from net/inet/ip.h/checksum.h */
	iph->check = 0;
	iph->check = ip_fast_csum((unsigned char *)iph, iph->ihl);

#ifdef TUNNEL_DEBUG
printk("New IP Header....\n");
print_ip(iph);
#endif
	/* Now send the packet on its way */
#ifdef TUNNEL_DEBUG
	printk("tunnel: calling ip_forward()\n");
#endif
	if(ip_forward(skb2, dev, 0, iph->daddr))
		kfree_skb(skb2, FREE_WRITE);

 
#ifdef TUNNEL_DEBUG
	printk("Packet sent through tunnel interface!\n");
#endif
	/* Record statistics */
	stats->tx_packets++;

#ifdef TUNNEL_DEBUG
	printk("tunnel: Updated usage statistics.\n");
#endif
	dev->tbusy=0;
	return 0;
}

static struct enet_statistics *
tunnel_get_stats(struct device *dev)
{
	return((struct enet_statistics*) dev->priv);
}

#ifdef MODULE
char kernel_version[] = UTS_RELEASE;

static int tunnel_probe(struct device *dev)
{
	tunnel_init(dev);
	return 0;
}

static struct device dev_tunnel = {
	"tunl0\0   ", 
		0, 0, 0, 0,
	 	0x0, 0,
	 	0, 0, 0, NULL, tunnel_probe };

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
#endif
