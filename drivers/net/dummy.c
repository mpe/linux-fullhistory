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
	[I tweaked this explanation a little but thats all]
			Alan Cox, 30th May 1994
*/

/* To have statistics (just packets sent) define this */
#undef DUMMY_STATS

#ifdef MODULE
#include <linux/module.h>
#include <linux/version.h>
#endif

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
#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <linux/errno.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

static int dummy_xmit(struct sk_buff *skb, struct device *dev);
#ifdef DUMMY_STATS
static struct enet_statistics *dummy_get_stats(struct device *dev);
#endif

#ifdef MODULE
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

#endif


int dummy_init(struct device *dev)
{
/* I commented this out as bootup is noisy enough anyway and this driver
   seems pretty reliable 8) 8) 8) */
/*	printk ( KERN_INFO "Dummy net driver (94/05/27 v1.0)\n" ); */

	/* Initialize the device structure. */
	dev->hard_start_xmit	= dummy_xmit;

#if DUMMY_STATS
	dev->priv = kmalloc(sizeof(struct enet_statistics), GFP_KERNEL);
	memset(dev->priv, 0, sizeof(struct enet_statistics));
	dev->get_stats		= dummy_get_stats;
#endif
#ifdef MODULE
	dev->open = &dummy_open;
	dev->stop = &dummy_close;
#endif

	/* Fill in the fields of the device structure with ethernet-generic values. */
	ether_setup(dev);
	dev->flags |= IFF_NOARP;

	return 0;
}

static int
dummy_xmit(struct sk_buff *skb, struct device *dev)
{
#if DUMMY_STATS
	struct enet_statistics *stats;
#endif

	if (skb == NULL || dev == NULL)
		return 0;

	dev_kfree_skb(skb, FREE_WRITE);

#if DUMMY_STATS
	stats = (struct enet_statistics *)dev->priv;
	stats->tx_packets++;
#endif

	return 0;
}

#if DUMMY_STATS
static struct enet_statistics *
dummy_get_stats(struct device *dev)
{
	struct enet_statistics *stats = (struct enet_statistics*) dev->priv;
	return stats;
}
#endif

#ifdef MODULE
char kernel_version[] = UTS_RELEASE;

static int dummy_probe(struct device *dev)
{
	dummy_init(dev);
	return 0;
}

static struct device dev_dummy = {
	"dummy0\0   ", 
		0, 0, 0, 0,
	 	0x0, 0,
	 	0, 0, 0, NULL, dummy_probe };

int init_module(void)
{
	/* Find a name for this unit */
	int ct= 1;
	
	while(dev_get(dev_dummy.name)!=NULL && ct<100)
	{
		sprintf(dev_dummy.name,"dummy%d",ct);
		ct++;
	}
	
	if (register_netdev(&dev_dummy) != 0)
		return -EIO;
	return 0;
}

void cleanup_module(void)
{
	if (MOD_IN_USE)
		printk("dummy: device busy, remove delayed\n");
	else
	{
		unregister_netdev(&dev_dummy);
	}
}
#endif /* MODULE */
