/*
 *	ipddp.c: IP-over-DDP driver for Linux
 *
 *	Authors:
 *      - Original code by: Bradford W. Johnson <johns393@maroon.tc.umn.edu>
 *	- Moved to driver by: Jay Schulist <Jay.Schulist@spacs.k12.wi.us>
 *
 *	Derived from:
 *	- Almost all code already existed in net/appletalk/ddp.c I just
 *	  moved/reorginized it into a driver file. Original IP-over-DDP code
 *	  was done by Bradford W. Johnson <johns393@maroon.tc.umn.edu>
 *      - skeleton.c: A network driver outline for linux.
 *        Written 1993-94 by Donald Becker.
 *	- dummy.c: A dummy net driver. By Nick Holloway.
 *
 *      Copyright 1993 United States Government as represented by the
 *      Director, National Security Agency.
 *
 *      This software may be used and distributed according to the terms
 *      of the GNU Public License, incorporated herein by reference.
 */

static const char *version = 
"ipddp.c:v0.01 8/28/97 Bradford W. Johnson <johns393@maroon.tc.umn.edu>\n";

#include <linux/config.h>
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
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/atalk.h>
#include <linux/ip.h>
#include <net/route.h>

#include "ipddp.h"		/* Our stuff */

/*
 *      The name of the card. Is used for messages and in the requests for
 *      io regions, irqs and dma channels
 */

static const char *cardname = "ipddp";

/* Use 0 for production, 1 for verification, 2 for debug, 3 for verbose debug */
#ifndef IPDDP_DEBUG
#define IPDDP_DEBUG 1
#endif
static unsigned int ipddp_debug = IPDDP_DEBUG;

/* Index to functions, as function prototypes. */
static int ipddp_xmit(struct sk_buff *skb, struct device *dev);
static struct net_device_stats *ipddp_get_stats(struct device *dev);
static int ipddp_rebuild_header(struct sk_buff *skb);
static int ipddp_header(struct sk_buff *skb, struct device *dev,
                unsigned short type, void *daddr, void *saddr, unsigned len);
static int ipddp_ioctl(struct device *dev, struct ifreq *ifr, int cmd);


static int ipddp_open(struct device *dev)
{
#ifdef MODULE
        MOD_INC_USE_COUNT;
#endif

        return 0;
}

static int ipddp_close(struct device *dev)
{
#ifdef MODULE
        MOD_DEC_USE_COUNT;
#endif

        return 0;
}

int ipddp_init(struct device *dev)
{
	static unsigned version_printed = 0;

	if (ipddp_debug && version_printed++ == 0)
                printk("%s", version);

	/* Initalize the device structure. */
        dev->hard_start_xmit = ipddp_xmit;

        dev->priv = kmalloc(sizeof(struct enet_statistics), GFP_KERNEL);
        if(!dev->priv)
                return -ENOMEM;
        memset(dev->priv,0,sizeof(struct enet_statistics));

	dev->open 	    = ipddp_open;
        dev->stop 	    = ipddp_close;
        dev->get_stats      = ipddp_get_stats;
        dev->do_ioctl       = ipddp_ioctl;
	dev->hard_header    = ipddp_header;        /* see ip_output.c */
	dev->rebuild_header = ipddp_rebuild_header;

        dev->type = ARPHRD_IPDDP;       	/* IP over DDP tunnel */
        dev->family = AF_INET;
        dev->mtu = 585;
        dev->flags |= IFF_NOARP;

        /*
         *      The worst case header we will need is currently a
         *      ethernet header (14 bytes) and a ddp header (sizeof ddpehdr+1)
         *      We send over SNAP so that takes another 8 bytes.
         */
        dev->hard_header_len = 14+8+sizeof(struct ddpehdr)+1;

	/* Fill in the device structure with ethernet-generic values. */
	ether_setup(dev);

        return 0;
}

/*
 * Transmit LLAP/ELAP frame using aarp_send_ddp.
 */
static int ipddp_xmit(struct sk_buff *skb, struct device *dev)
{
        /* Retrieve the saved address hint */
        struct at_addr *a=(struct at_addr *)skb->data;
        skb_pull(skb,4);

        ((struct net_device_stats *) dev->priv)->tx_packets++;
        ((struct net_device_stats *) dev->priv)->tx_bytes+=skb->len;

	if(ipddp_debug>1)
        	printk("ipddp_xmit: Headroom %d\n",skb_headroom(skb));

        if(aarp_send_ddp(skb->dev,skb,a,NULL) < 0)
                dev_kfree_skb(skb,FREE_WRITE);

        return 0;
}

/*
 * Get the current statistics. This may be called with the card open or closed.
 */
static struct net_device_stats *ipddp_get_stats(struct device *dev)
{
        return (struct net_device_stats *)dev->priv;
}

/*
 * Now the packet really wants to go out.
 */
static int ipddp_rebuild_header(struct sk_buff *skb)
{
	u32 paddr = ((struct rtable*)skb->dst)->rt_gateway;
        struct ddpehdr *ddp;
        struct at_addr at;
        struct ipddp_route *rt;
        struct at_addr *our_addr;

        /*
         * On entry skb->data points to the ddpehdr we reserved earlier.
         * skb->h.raw will be the higher level header.
         */

        /*
         * We created this earlier.
         */

        ddp = (struct ddpehdr *) (skb->data+4);

        /* find appropriate route */

        for(rt=ipddp_route_head;rt;rt=rt->next)
        {
                if(rt->ip == paddr)
                        break;
        }

        if(!rt) {
                printk("ipddp unreachable dst %08lx\n",ntohl(paddr));
                return -ENETUNREACH;
        }

        our_addr = atalk_find_dev_addr(rt->dev);

        /* fill in ddpehdr */
        ddp->deh_len = skb->len;
        ddp->deh_hops = 1;
        ddp->deh_pad = 0;
        ddp->deh_sum = 0;
        ddp->deh_dnet = rt->at.s_net;   /* FIXME more hops?? */
        ddp->deh_snet = our_addr->s_net;
        ddp->deh_dnode = rt->at.s_node;
        ddp->deh_snode = our_addr->s_node;
        ddp->deh_dport = 72;
        ddp->deh_sport = 72;

        *((__u8 *)(ddp+1)) = 22;        /* ddp type = IP */

        /* fix up length field */
        *((__u16 *)ddp)=ntohs(*((__u16 *)ddp));

        /* set skb->dev to appropriate device */
        skb->dev = rt->dev;

        /* skb->raddr = (unsigned long) at */
        at = rt->at;
        /* Hide it at the start of the buffer */
        memcpy(skb->data,(void *)&at,sizeof(at));
        skb->arp = 1;   /* so the actual device doesn't try to arp it... */
        skb->protocol = htons(ETH_P_ATALK);     /* Protocol has changed */

        return 0;
}

static int ipddp_header(struct sk_buff *skb, struct device *dev, 
		unsigned short type, void *daddr, void *saddr, unsigned len)
{
	if(ipddp_debug>=2)
        	printk("%s: ipddp_header\n", cardname);

        /* Push down the header space and the type byte */
        skb_push(skb, sizeof(struct ddpehdr)+1+4);

        return 0;
}

static int ipddp_ioctl(struct device *dev, struct ifreq *ifr, int cmd)
{
        struct ipddp_route *urt = (struct ipddp_route *)ifr->ifr_data;

        if(!suser())
                return -EPERM;

        /* for now we only have one route at a time */

        switch(cmd)
        {
                case SIOCADDIPDDPRT:
                        if(copy_from_user(&ipddp_route_test,urt,sizeof(struct ipddp_route)))
                                return -EFAULT;
                        ipddp_route_test.dev = atrtr_get_dev(&ipddp_route_test.at);
                        if (dev==NULL)
                                return -ENETUNREACH;
                        ipddp_route_test.next = NULL;
                        printk("%s: Added route through %s\n",
				ipddp_route_test.dev->name, cardname);
                        ipddp_route_head = &ipddp_route_test;
                        return 0;

                case SIOCFINDIPDDPRT:
                        if(copy_to_user(urt,&ipddp_route_test,sizeof(struct ipddp_route)))
                                return -EFAULT;
                        return 0;

                case SIOCDELIPDDPRT:
                        ipddp_route_test.dev = NULL;
                        ipddp_route_head = NULL;
                        return 0;

                default:
                        return -EINVAL;
        }
}

#ifdef MODULE	/* Module specific functions for ipddp.c */

static struct device dev_ipddp=
{
        "ipddp0\0   ",
                0, 0, 0, 0,
                0x0, 0,
                0, 0, 0, NULL, ipddp_init
};

int init_module(void)
{
	if (register_netdev(&dev_ipddp) != 0)
                return -EIO;

	return 0;
}

void cleanup_module(void)
{
	unregister_netdev(&dev_ipddp);
        kfree(dev_ipddp.priv);
	dev_ipddp.priv = NULL;
}

#endif /* MODULE */
