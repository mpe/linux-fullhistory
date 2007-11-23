/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Pseudo-driver for the loopback interface.
 *
 * Version:	@(#)loopback.c	1.0.4b	08/16/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Donald Becker, <becker@cesdis.gsfc.nasa.gov>
 *
 *		Alan Cox	:	Fixed oddments for NET3.014
 *		Alan Cox	:	Rejig for NET3.029 snap #3
 *		Alan Cox	: 	Fixed NET3.029 bugs and sped up
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#include <linux/config.h>
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
#include <net/sock.h>


static int loopback_xmit(struct sk_buff *skb, struct device *dev)
{
	struct enet_statistics *stats = (struct enet_statistics *)dev->priv;
	unsigned long flags;
	int unlock=1;
  
	if (skb == NULL || dev == NULL) 
		return(0);

	save_flags(flags);
	cli();
	if (dev->tbusy != 0) 
	{
		restore_flags(flags);
		stats->tx_errors++;
		return(1);
	}
	dev->tbusy = 1;
	restore_flags(flags);
  
	/*
	 *	Optimise so buffers with skb->free=1 are not copied but
	 *	instead are lobbed from tx queue to rx queue 
	 */

	if(skb->free==0)
	{
	  	struct sk_buff *skb2=skb;
	  	skb=skb_clone(skb, GFP_ATOMIC);		/* Clone the buffer */
	  	if(skb==NULL)
	  		return 1;
	  	dev_kfree_skb(skb2, FREE_READ);
  		unlock=0;
	}
	else if(skb->sk)
	{
	  	/*
	  	 *	Packet sent but looped back around. Cease to charge
	  	 *	the socket for the frame.
	  	 */
		save_flags(flags);
	  	cli();
	  	skb->sk->wmem_alloc-=skb->mem_len;
	  	skb->sk->write_space(skb->sk);
	  	restore_flags(flags);
	}

	skb->protocol=eth_type_trans(skb,dev);
	skb->dev=dev;
	save_flags(flags);
	cli();
	netif_rx(skb);
	if(unlock)
	  	skb_device_unlock(skb);
	restore_flags(flags);
  
	stats->tx_packets++;

	dev->tbusy = 0;

	return(0);
}

static struct enet_statistics *get_stats(struct device *dev)
{
	return (struct enet_statistics *)dev->priv;
}

static int loopback_open(struct device *dev)
{
	dev->flags|=IFF_LOOPBACK;
	return 0;
}

/* Initialize the rest of the LOOPBACK device. */
int loopback_init(struct device *dev)
{
	int i;

	dev->mtu		= 2000;			/* MTU			*/
	dev->tbusy		= 0;
	dev->hard_start_xmit	= loopback_xmit;
	dev->open		= NULL;
	dev->hard_header	= eth_header;
	dev->hard_header_len	= ETH_HLEN;		/* 14			*/
	dev->addr_len		= ETH_ALEN;		/* 6			*/
	dev->type		= ARPHRD_ETHER;		/* 0x0001		*/
	dev->rebuild_header	= eth_rebuild_header;
	dev->open		= loopback_open;
	dev->flags		= IFF_LOOPBACK|IFF_BROADCAST;
	dev->family		= AF_INET;
#ifdef CONFIG_INET    
	dev->pa_addr		= in_aton("127.0.0.1");
	dev->pa_brdaddr	= in_aton("127.255.255.255");
	dev->pa_mask		= in_aton("255.0.0.0");
	dev->pa_alen		= sizeof(unsigned long);
#endif  
	dev->priv = kmalloc(sizeof(struct enet_statistics), GFP_KERNEL);
	memset(dev->priv, 0, sizeof(struct enet_statistics));
	dev->get_stats = get_stats;

	/*
	 *	Fill in the generic fields of the device structure. 
	 */
   
	for (i = 0; i < DEV_NUMBUFFS; i++)
		skb_queue_head_init(&dev->buffs[i]);
  
	return(0);
};
