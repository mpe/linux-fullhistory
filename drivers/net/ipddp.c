/*
 *	ipddp.c: IP to Appletalk-IP Encapsulation driver for Linux
 *		 Appletalk-IP to IP Decapsulation driver for Linux
 *
 *	Authors:
 *      - DDP-IP Encap by: Bradford W. Johnson <johns393@maroon.tc.umn.edu>
 *	- DDP-IP Decap by: Jay Schulist <Jay.Schulist@spacs.k12.wi.us>
 *
 *	Derived from:
 *	- Almost all code already existed in net/appletalk/ddp.c I just
 *	  moved/reorginized it into a driver file. Original IP-over-DDP code
 *	  was done by Bradford W. Johnson <johns393@maroon.tc.umn.edu>
 *      - skeleton.c: A network driver outline for linux.
 *        Written 1993-94 by Donald Becker.
 *	- dummy.c: A dummy net driver. By Nick Holloway.
 *	- MacGate: A user space Daemon for Appletalk-IP Decap for
 *	  Linux by Jay Schulist <Jay.Schulist@spacs.k12.wi.us>
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
#include <linux/inetdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/atalk.h>
#include <linux/ip.h>
#include <net/route.h>
#include <linux/inet.h>

#include "ipddp.h"		/* Our stuff */

static struct ipddp_route *ipddp_route_list = NULL;

#ifdef CONFIG_IPDDP_ENCAP
static int ipddp_mode = IPDDP_ENCAP;
#else
static int ipddp_mode = IPDDP_DECAP;
#endif

/* Use 0 for production, 1 for verification, 2 for debug, 3 for verbose debug */
#ifndef IPDDP_DEBUG
#define IPDDP_DEBUG 1
#endif
static unsigned int ipddp_debug = IPDDP_DEBUG;

/* Index to functions, as function prototypes. */
static int ipddp_xmit(struct sk_buff *skb, struct device *dev);
static struct net_device_stats *ipddp_get_stats(struct device *dev);
static int ipddp_create(struct ipddp_route *new_rt);
static int ipddp_delete(struct ipddp_route *rt);
static struct ipddp_route* ipddp_find_route(struct ipddp_route *rt);
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

	/* Let the user now what mode we are in */
	if(ipddp_mode == IPDDP_ENCAP)
		printk("%s: Appletalk-IP Encap. mode by Bradford W. Johnson <johns393@maroon.tc.umn.edu>\n", 
			dev->name);
	if(ipddp_mode == IPDDP_DECAP)
		printk("%s: Appletalk-IP Decap. mode by Jay Schulist <Jay.Schulist@spacs.k12.wi.us>\n", 
			dev->name);

	/* Fill in the device structure with ethernet-generic values. */
        ether_setup(dev);

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

        dev->type = ARPHRD_IPDDP;       	/* IP over DDP tunnel */
        dev->mtu = 585;
        dev->flags |= IFF_NOARP;

        /*
         *      The worst case header we will need is currently a
         *      ethernet header (14 bytes) and a ddp header (sizeof ddpehdr+1)
         *      We send over SNAP so that takes another 8 bytes.
         */
        dev->hard_header_len = 14+8+sizeof(struct ddpehdr)+1;

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
 * Transmit LLAP/ELAP frame using aarp_send_ddp.
 */
static int ipddp_xmit(struct sk_buff *skb, struct device *dev)
{
	u32 paddr = ((struct rtable*)skb->dst)->rt_gateway;
        struct ddpehdr *ddp;
        struct ipddp_route *rt;
        struct at_addr *our_addr;

	/*
         * Find appropriate route to use, based only on IP number.
         */
        for(rt = ipddp_route_list; rt != NULL; rt = rt->next)
        {
                if(rt->ip == paddr)
                        break;
        }
        if(rt == NULL)
                return 0;

        our_addr = atalk_find_dev_addr(rt->dev);

	if(ipddp_mode == IPDDP_DECAP)
		/* 
		 * Pull off the excess room that should not be there.
		 * This is due to a hard-header problem. This is the
		 * quick fix for now though, till it breaks.
		 */
		skb_pull(skb, 35-(sizeof(struct ddpehdr)+1));

	/* Create the Extended DDP header */
	ddp = (struct ddpehdr *)skb->data;
        ddp->deh_len = skb->len;
        ddp->deh_hops = 1;
        ddp->deh_pad = 0;
        ddp->deh_sum = 0;

	/*
         * For Localtalk we need aarp_send_ddp to strip the
         * long DDP header and place a shot DDP header on it.
         */
        if(rt->dev->type == ARPHRD_LOCALTLK)
        {
                ddp->deh_dnet  = 0;   /* FIXME more hops?? */
                ddp->deh_snet  = 0;
        }
        else
        {
                ddp->deh_dnet  = rt->at.s_net;   /* FIXME more hops?? */
                ddp->deh_snet  = our_addr->s_net;
        }
        ddp->deh_dnode = rt->at.s_node;
        ddp->deh_snode = our_addr->s_node;
        ddp->deh_dport = 72;
        ddp->deh_sport = 72;

        *((__u8 *)(ddp+1)) = 22;        	/* ddp type = IP */
        *((__u16 *)ddp)=ntohs(*((__u16 *)ddp));	/* fix up length field */

        skb->protocol = htons(ETH_P_ATALK);     /* Protocol has changed */

	((struct net_device_stats *) dev->priv)->tx_packets++;
        ((struct net_device_stats *) dev->priv)->tx_bytes+=skb->len;

        if(aarp_send_ddp(rt->dev, skb, &rt->at, NULL) < 0)
                dev_kfree_skb(skb);

        return 0;
}

/*
 * Create a routing entry. We first verify that the
 * record does not already exist. If it does we return -EEXIST
 */
static int ipddp_create(struct ipddp_route *new_rt)
{
        struct ipddp_route *rt =(struct ipddp_route*) kmalloc(sizeof(*rt), GFP_KERNEL);
	struct ipddp_route *test;

        if(rt == NULL)
                return -ENOMEM;

        rt->ip = new_rt->ip;
        rt->at = new_rt->at;
        rt->next = NULL;
        rt->dev = atrtr_get_dev(&rt->at);
        if(rt->dev == NULL)
                return (-ENETUNREACH);

	test = ipddp_find_route(rt);
	if(test != NULL)
		return (-EEXIST);
	
        rt->next = ipddp_route_list;
        ipddp_route_list = rt;

        return 0;
}

/*
 * Delete a route, we only delete a FULL match.
 * If route does not exist we return -ENOENT.
 */
static int ipddp_delete(struct ipddp_route *rt)
{
        struct ipddp_route **r = &ipddp_route_list;
        struct ipddp_route *tmp;

        while((tmp = *r) != NULL)
        {
                if(tmp->ip == rt->ip
                        && tmp->at.s_net == rt->at.s_net
                        && tmp->at.s_node == rt->at.s_node)
                {
                        *r = tmp->next;
                        kfree_s(tmp, sizeof(struct ipddp_route));
                        return 0;
                }
                r = &tmp->next;
        }

        return (-ENOENT);
}

/*
 * Find a routing entry, we only return a FULL match
 */
static struct ipddp_route* ipddp_find_route(struct ipddp_route *rt)
{
        struct ipddp_route *f;

        for(f = ipddp_route_list; f != NULL; f = f->next)
        {
                if(f->ip == rt->ip
                        && f->at.s_net == rt->at.s_net
                        && f->at.s_node == rt->at.s_node)
                        return (f);
        }

        return (NULL);
}

static int ipddp_ioctl(struct device *dev, struct ifreq *ifr, int cmd)
{
        struct ipddp_route *rt = (struct ipddp_route *)ifr->ifr_data;

        if(!capable(CAP_NET_ADMIN))
                return -EPERM;

        switch(cmd)
        {
		case SIOCADDIPDDPRT:
                        return (ipddp_create(rt));

                case SIOCFINDIPDDPRT:
                        if(copy_to_user(rt, ipddp_find_route(rt), sizeof(struct ipddp_route)))
                                return -EFAULT;
                        return 0;

                case SIOCDELIPDDPRT:
                        return (ipddp_delete(rt));

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

MODULE_PARM(ipddp_mode, "i");

int init_module(void)
{
	int err;

	err=dev_alloc_name(&dev_ipddp, "ipddp%d");
        if(err < 0)
                return err;

	if(register_netdev(&dev_ipddp) != 0)
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
