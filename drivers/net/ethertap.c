/*
 *	Ethertap: A network device for bouncing packets via user space
 *
 *	This is a very simple ethernet driver. It bounces ethernet frames
 *	to user space on /dev/tap0->/dev/tap15 and expects ethernet frames
 *	to be written back to it. By default it does not ARP. If you turn ARP
 *	on it will attempt to ARP the user space and reply to ARPS from the
 *	user space.
 *
 *	As this is an ethernet device you cau use it for appletalk, IPX etc
 *	even for building bridging tunnels.
 */
 
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/errno.h>

#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/init.h>

#include <linux/netlink.h>

/*
 *	Index to functions.
 */

int	    ethertap_probe(struct device *dev);
static int  ethertap_open(struct device *dev);
static int  ethertap_start_xmit(struct sk_buff *skb, struct device *dev);
static int  ethertap_close(struct device *dev);
static struct net_device_stats *ethertap_get_stats(struct device *dev);
static int ethertap_rx(int id, struct sk_buff *skb);

static int ethertap_debug = 0;

static struct device *tap_map[32];	/* Returns the tap device for a given netlink */

/*
 *	Board-specific info in dev->priv.
 */

struct net_local
{
	struct net_device_stats stats;
};

/*
 *	To call this a probe is a bit misleading, however for real
 *	hardware it would have to check what was present.
 */
 
__initfunc(int ethertap_probe(struct device *dev))
{
	memcpy(dev->dev_addr, "\xFD\xFD\x00\x00\x00\x00", 6);
	if (dev->mem_start & 0xf)
		ethertap_debug = dev->mem_start & 0x7;

	/*
	 *	Initialize the device structure.
	 */

	dev->priv = kmalloc(sizeof(struct net_local), GFP_KERNEL);
	if (dev->priv == NULL)
		return -ENOMEM;
	memset(dev->priv, 0, sizeof(struct net_local));

	/*
	 *	The tap specific entries in the device structure.
	 */

	dev->open = ethertap_open;
	dev->hard_start_xmit = ethertap_start_xmit;
	dev->stop = ethertap_close;
	dev->get_stats = ethertap_get_stats;

	/*
	 *	Setup the generic properties
	 */

	ether_setup(dev);

	dev->flags|=IFF_NOARP;	/* Need to set ARP - looks like there is a bug
				   in the 2.1.x hard header code currently */
	tap_map[dev->base_addr]=dev;
	
	return 0;
}

/*
 *	Open/initialize the board.
 */

static int ethertap_open(struct device *dev)
{
	struct in_device *in_dev;
	if (ethertap_debug > 2)
		printk("%s: Doing ethertap_open()...", dev->name);
	netlink_attach(dev->base_addr, ethertap_rx);
	dev->start = 1;
	dev->tbusy = 0;

	/* Fill in the MAC based on the IP address. We do the same thing
	   here as PLIP does */
	
	if((in_dev=dev->ip_ptr)!=NULL)
	{
		/*
		 *	Any address wil do - we take the first
		 */
		struct in_ifaddr *ifa=in_dev->ifa_list;
		if(ifa!=NULL)
			memcpy(dev->dev_addr+2,&ifa->ifa_local,4);
	}
	MOD_INC_USE_COUNT;
	return 0;
}

/*
 *	We transmit by throwing the packet at netlink. We have to clone
 *	it for 2.0 so that we dev_kfree_skb() the locked original.
 */
 
static int ethertap_start_xmit(struct sk_buff *skb, struct device *dev)
{
	struct net_local *lp = (struct net_local *)dev->priv;
	struct sk_buff *tmp;
	/* copy buffer to tap */
	tmp=skb_clone(skb, GFP_ATOMIC);
	if(tmp)
	{
		if(netlink_post(dev->base_addr, tmp)<0)
			kfree_skb(tmp);
		lp->stats.tx_bytes+=skb->len;
		lp->stats.tx_packets++;
	}
	dev_kfree_skb (skb);
	return 0;
}

/*
 *	The typical workload of the driver:
 *	Handle the ether interface interrupts.
 *
 *	(In this case handle the packets posted from user space..)
 */

static int ethertap_rx(int id, struct sk_buff *skb)
{
	struct device *dev = (struct device *)(tap_map[id]);
	struct net_local *lp;
	int len=skb->len;
	
	if(dev==NULL)
	{
		printk("ethertap: bad unit!\n");
		kfree_skb(skb);
		return -ENXIO;
	}
	lp = (struct net_local *)dev->priv;

	if (ethertap_debug > 3)
		printk("%s: ethertap_rx()\n", dev->name);
	skb->dev = dev;
	skb->protocol=eth_type_trans(skb,dev);
	lp->stats.rx_packets++;
	lp->stats.rx_bytes+=len;
	netif_rx(skb);
	return len;
}

static int ethertap_close(struct device *dev)
{
	if (ethertap_debug > 2)
		printk("%s: Shutting down tap %ld.\n", dev->name, dev->base_addr);

	dev->tbusy = 1;
	dev->start = 0;
	MOD_DEC_USE_COUNT;
	return 0;
}

static struct net_device_stats *ethertap_get_stats(struct device *dev)
{
	struct net_local *lp = (struct net_local *)dev->priv;
	return &lp->stats;
}

#ifdef MODULE

int unit;
MODULE_PARM(unit,"i");

static char devicename[9] = { 0, };

static struct device dev_ethertap =
{
	devicename,
	0, 0, 0, 0,
	1, 5,
	0, 0, 0, NULL, ethertap_probe
};

int init_module(void)
{
	dev_ethertap.base_addr=unit+NETLINK_TAPBASE;
	sprintf(devicename,"tap%d",unit);
	if (dev_get(devicename))
	{
		printk(KERN_INFO "ethertap: tap %d already loaded.\n", unit);
		return -EBUSY;
	}
	if (register_netdev(&dev_ethertap) != 0)
		return -EIO;
	return 0;
}

void cleanup_module(void)
{
	tap_map[dev_ethertap.base_addr]=NULL;
	unregister_netdev(&dev_ethertap);

	/*
	 *	Free up the private structure.
	 */

	kfree(dev_ethertap.priv);
	dev_ethertap.priv = NULL;	/* gets re-allocated by ethertap_probe */
}

#endif /* MODULE */
