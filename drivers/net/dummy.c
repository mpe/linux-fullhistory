/* dummy.c: a dummy net driver

	The purpose of this driver is to provide a device to point a
	route through, but not to actually transmit packets.

	Why?  If you have a machine whose only connection is an occasional
	PPP/SLIP/PLIP link, you can only connect to your own hostname
	when the link is up.  Otherwise you have to use localhost.
	This isn't very consistent.

	One solution is to set up a dummy link using PPP/SLIP/PLIP,
	but this seems (to me) too much overhead for too little gain.
	This driver provides a small alternative. Thus you can do
	
	[when not running slip]
		ifconfig dummy slip.addr.ess.here up
	[to go to slip]
		ifconfig dummy down
		dip whatever

	This was written by looking at Donald Becker's skeleton driver
	and the loopback driver.  I then threw away anything that didn't
	apply!	Thanks to Alan Cox for the key clue on what to do with
	misguided packets.

			Nick Holloway, 27th May 1994
	[I tweaked this explanation a little but that's all]
			Alan Cox, 30th May 1994
*/

/* To have statistics (just packets sent) define this */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/init.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <linux/errno.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

static int dummy_xmit(struct sk_buff *skb, struct device *dev);
static struct net_device_stats *dummy_get_stats(struct device *dev);

static int dummy_open(struct device *dev)
{
	MOD_INC_USE_COUNT;
	return 0;
}

static int dummy_close(struct device *dev)
{
	MOD_DEC_USE_COUNT;
	return 0;
}

/* fake multicast ability */
static void set_multicast_list(struct device *dev)
{
}

#ifdef CONFIG_NET_FASTROUTE
static int dummy_accept_fastpath(struct device *dev, struct dst_entry *dst)
{
	return -1;
}
#endif

__initfunc(int dummy_init(struct device *dev))
{
	/* Initialize the device structure. */
	dev->hard_start_xmit	= dummy_xmit;

	dev->priv = kmalloc(sizeof(struct net_device_stats), GFP_KERNEL);
	if (dev->priv == NULL)
		return -ENOMEM;
	memset(dev->priv, 0, sizeof(struct net_device_stats));
	dev->get_stats	= dummy_get_stats;

	dev->open = dummy_open;
	dev->stop = dummy_close;
	dev->set_multicast_list = set_multicast_list;

	/* Fill in the fields of the device structure with ethernet-generic values. */
	ether_setup(dev);
	dev->tx_queue_len = 0;
	dev->flags |= IFF_NOARP;
	dev->flags &= ~IFF_MULTICAST;
#ifdef CONFIG_NET_FASTROUTE
	dev->accept_fastpath = dummy_accept_fastpath;
#endif

	return 0;
}

static int dummy_xmit(struct sk_buff *skb, struct device *dev)
{
	struct net_device_stats *stats;
	dev_kfree_skb(skb);

	stats = (struct net_device_stats *)dev->priv;
	stats->tx_packets++;
	stats->tx_bytes+=skb->len;

	return 0;
}

static struct net_device_stats *dummy_get_stats(struct device *dev)
{
	struct net_device_stats *stats = (struct net_device_stats *) dev->priv;
	return stats;
}

#ifdef MODULE

__initfunc(static int dummy_probe(struct device *dev))
{
	dummy_init(dev);
	return 0;
}

static char dummy_name[16];

static struct device dev_dummy = {
		dummy_name, 	/* Needs to be writeable */
		0, 0, 0, 0,
	 	0x0, 0,
	 	0, 0, 0, NULL, dummy_probe };

int init_module(void)
{
	/* Find a name for this unit */
	int err=dev_alloc_name(&dev_dummy,"dummy%d");
	if(err<0)
		return err;
	if (register_netdev(&dev_dummy) != 0)
		return -EIO;
	return 0;
}

void cleanup_module(void)
{
	unregister_netdev(&dev_dummy);
	kfree(dev_dummy.priv);
	dev_dummy.priv = NULL;
}
#endif /* MODULE */
