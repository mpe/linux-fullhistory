/*
 * 	NET3	Protocol independent device support routines.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	Derived from the non IP parts of dev.c 1.0.19
 * 		Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *				Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *				Mark Evans, <evansmp@uhura.aston.ac.uk>
 *
 *	Additional Authors:
 *		Florian la Roche <rzsfl@rz.uni-sb.de>
 *		Alan Cox <gw4pts@gw4pts.ampr.org>
 *		David Hinds <dhinds@allegro.stanford.edu>
 *
 *	Changes:
 *		Alan Cox	:	device private ioctl copies fields back.
 *		Alan Cox	:	Transmit queue code does relevant stunts to
 *					keep the queue safe.
 *		Alan Cox	:	Fixed double lock.
 *		Alan Cox	:	Fixed promisc NULL pointer trap
 *		????????	:	Support the full private ioctl range
 *		Alan Cox	:	Moved ioctl permission check into drivers
 *		Tim Kordas	:	SIOCADDMULTI/SIOCDELMULTI
 *		Alan Cox	:	100 backlog just doesn't cut it when
 *					you start doing multicast video 8)
 *		Alan Cox	:	Rewrote net_bh and list manager.
 *		Alan Cox	: 	Fix ETH_P_ALL echoback lengths.
 *		Alan Cox	:	Took out transmit every packet pass
 *					Saved a few bytes in the ioctl handler
 *		Alan Cox	:	Network driver sets packet type before calling netif_rx. Saves
 *					a function call a packet.
 *		Alan Cox	:	Hashed net_bh()
 *	Richard Kooijman	:	Timestamp fixes.
 *		Alan Cox	:	Wrong field in SIOCGIFDSTADDR
 *		Alan Cox	:	Device lock protection.
 *		Alan Cox	: 	Fixed nasty side effect of device close changes.
 *
 *	Cleaned up and recommented by Alan Cox 2nd April 1994. I hope to have
 *	the rest as well commented in the end.
 */

/*
 *	A lot of these includes will be going walkies very soon 
 */
 
#include <asm/segment.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/in.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/if_ether.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/notifier.h>
#include <net/ip.h>
#include <net/route.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/arp.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>

/*
 *	The list of packet types we will receive (as opposed to discard)
 *	and the routines to invoke.
 */

struct packet_type *ptype_base[16];
struct packet_type *ptype_all = NULL;		/* Taps */

/*
 *	Device list lock
 */
 
int dev_lockct=0;
 
/*
 *	Our notifier list
 */
 
struct notifier_block *netdev_chain=NULL;

/*
 *	Device drivers call our routines to queue packets here. We empty the
 *	queue in the bottom half handler.
 */

static struct sk_buff_head backlog = 
{
	(struct sk_buff *)&backlog, (struct sk_buff *)&backlog
#if CONFIG_SKB_CHECK
	,SK_HEAD_SKB
#endif
};

/* 
 *	We don't overdo the queue or we will thrash memory badly.
 */
 
static int backlog_size = 0;

/*
 *	Return the lesser of the two values. 
 */
 
static __inline__ unsigned long min(unsigned long a, unsigned long b)
{
  return (a < b)? a : b;
}


/******************************************************************************************

		Protocol management and registration routines

*******************************************************************************************/

/*
 *	For efficiency
 */

static int dev_nit=0;

/*
 *	Add a protocol ID to the list. Now that the input handler is
 *	smarter we can dispense with all the messy stuff that used to be
 *	here.
 */
 
void dev_add_pack(struct packet_type *pt)
{
	int hash;
	if(pt->type==htons(ETH_P_ALL))
	{
		dev_nit++;
		pt->next=ptype_all;
		ptype_all=pt;
	}
	else
	{	
		hash=ntohs(pt->type)&15;
		pt->next = ptype_base[hash];
		ptype_base[hash] = pt;
	}
}


/*
 *	Remove a protocol ID from the list.
 */
 
void dev_remove_pack(struct packet_type *pt)
{
	struct packet_type **pt1;
	if(pt->type==htons(ETH_P_ALL))
	{
		dev_nit--;
		pt1=&ptype_all;
	}
	else
		pt1=&ptype_base[ntohs(pt->type)&15];
	for(; (*pt1)!=NULL; pt1=&((*pt1)->next))
	{
		if(pt==(*pt1))
		{
			*pt1=pt->next;
			return;
		}
	}
	printk("dev_remove_pack: %p not found.\n", pt);
}

/*****************************************************************************************

			    Device Interface Subroutines

******************************************************************************************/

/* 
 *	Find an interface by name.
 */
 
struct device *dev_get(const char *name)
{
	struct device *dev;

	for (dev = dev_base; dev != NULL; dev = dev->next) 
	{
		if (strcmp(dev->name, name) == 0)
			return(dev);
	}
	return(NULL);
}


/*
 *	Prepare an interface for use. 
 */
 
int dev_open(struct device *dev)
{
	int ret = 0;

	/*
	 *	Call device private open method
	 */
	if (dev->open) 
  		ret = dev->open(dev);

	/*
	 *	If it went open OK then set the flags
	 */
	 
	if (ret == 0) 
	{
		dev->flags |= (IFF_UP | IFF_RUNNING);
		/*
		 *	Initialise multicasting status 
		 */
#ifdef CONFIG_IP_MULTICAST
		/* 
		 *	Join the all host group 
		 */
		ip_mc_allhost(dev);
#endif				
		dev_mc_upload(dev);
		notifier_call_chain(&netdev_chain, NETDEV_UP, dev);
	}
	return(ret);
}


/*
 *	Completely shutdown an interface.
 */
 
int dev_close(struct device *dev)
{
	int ct=0;

	/*
	 *	Call the device specific close. This cannot fail.
	 *	Only if device is UP
	 */
	if ((dev->flags & IFF_UP) && dev->stop)
		dev->stop(dev);

	dev->flags = 0;

	/*
	 *	Tell people we are going down
	 */
	notifier_call_chain(&netdev_chain, NETDEV_DOWN, dev);
	/*
	 *	Flush the multicast chain
	 */
	dev_mc_discard(dev);
	/*
	 *	Blank the IP addresses
	 */
	dev->pa_addr = 0;
	dev->pa_dstaddr = 0;
	dev->pa_brdaddr = 0;
	dev->pa_mask = 0;
	/*
	 *	Purge any queued packets when we down the link 
	 */
	while(ct<DEV_NUMBUFFS)
	{
		struct sk_buff *skb;
		while((skb=skb_dequeue(&dev->buffs[ct]))!=NULL)
			if(skb->free)
				kfree_skb(skb,FREE_WRITE);
		ct++;
	}
	return(0);
}


/*
 *	Device change register/unregister. These are not inline or static
 *	as we export them to the world.
 */

int register_netdevice_notifier(struct notifier_block *nb)
{
	return notifier_chain_register(&netdev_chain, nb);
}

int unregister_netdevice_notifier(struct notifier_block *nb)
{
	return notifier_chain_unregister(&netdev_chain,nb);
}



/*
 *	Send (or queue for sending) a packet. 
 *
 *	IMPORTANT: When this is called to resend frames. The caller MUST
 *	already have locked the sk_buff. Apart from that we do the
 *	rest of the magic.
 */

void dev_queue_xmit(struct sk_buff *skb, struct device *dev, int pri)
{
	unsigned long flags;
	struct packet_type *ptype;
	int where = 0;		/* used to say if the packet should go	*/
				/* at the front or the back of the	*/
				/* queue - front is a retransmit try	*/

	if(pri>=0 && !skb_device_locked(skb))
		skb_device_lock(skb);	/* Shove a lock on the frame */
#if CONFIG_SKB_CHECK 
	IS_SKB(skb);
#endif    
	skb->dev = dev;

	/*
	 *	Negative priority is used to flag a frame that is being pulled from the
	 *	queue front as a retransmit attempt. It therefore goes back on the queue
	 *	start on a failure.
	 */
	 
  	if (pri < 0) 
  	{
		pri = -pri-1;
		where = 1;
  	}

#ifdef CONFIG_NET_DEBUG
	if (pri >= DEV_NUMBUFFS) 
	{
		printk("bad priority in dev_queue_xmit.\n");
		pri = 1;
	}
#endif

	/*
	 *	If the address has not been resolved. Call the device header rebuilder.
	 *	This can cover all protocols and technically not just ARP either.
	 */
	 
	if (!skb->arp && dev->rebuild_header(skb->data, dev, skb->raddr, skb)) {
		return;
	}

	save_flags(flags);
	cli();	
	if (/*dev_nit && */!where) 	/* Always keep order. It helps other hosts
					   far more than it costs us */
	{
		skb_queue_tail(dev->buffs + pri,skb);
		skb_device_unlock(skb);		/* Buffer is on the device queue and can be freed safely */
		skb = skb_dequeue(dev->buffs + pri);
		skb_device_lock(skb);		/* New buffer needs locking down */
	}
	restore_flags(flags);

	/* copy outgoing packets to any sniffer packet handlers */
	if(!where && dev_nit)
	{
		skb->stamp=xtime;
		for (ptype = ptype_all; ptype!=NULL; ptype = ptype->next) 
		{
			/* Never send packets back to the socket
			 * they originated from - MvS (miquels@drinkel.ow.org)
			 */
			if ((ptype->dev == dev || !ptype->dev) &&
			   ((struct sock *)ptype->data != skb->sk))
			{
				struct sk_buff *skb2;
				if ((skb2 = skb_clone(skb, GFP_ATOMIC)) == NULL)
					break;
				skb2->h.raw = skb2->data + dev->hard_header_len;
				skb2->mac.raw = skb2->data;
				ptype->func(skb2, skb->dev, ptype);
			}
		}
	}
	start_bh_atomic();
	if (dev->hard_start_xmit(skb, dev) == 0) {
		/*
		 *	Packet is now solely the responsibility of the driver
		 */
		end_bh_atomic();
		return;
	}
	end_bh_atomic();

	/*
	 *	Transmission failed, put skb back into a list. Once on the list it's safe and
	 *	no longer device locked (it can be freed safely from the device queue)
	 */
	cli();
	skb_device_unlock(skb);
	skb_queue_head(dev->buffs + pri,skb);
	restore_flags(flags);
}

/*
 *	Receive a packet from a device driver and queue it for the upper
 *	(protocol) levels.  It always succeeds. This is the recommended 
 *	interface to use.
 */

void netif_rx(struct sk_buff *skb)
{
	static int dropping = 0;

	/*
	 *	Any received buffers are un-owned and should be discarded
	 *	when freed. These will be updated later as the frames get
	 *	owners.
	 */
	skb->sk = NULL;
	skb->free = 1;
	if(skb->stamp.tv_sec==0)
		skb->stamp = xtime;

	/*
	 *	Check that we aren't overdoing things.
	 */

	if (!backlog_size)
  		dropping = 0;
	else if (backlog_size > 300)
		dropping = 1;

	if (dropping) 
	{
		kfree_skb(skb, FREE_READ);
		return;
	}

	/*
	 *	Add it to the "backlog" queue. 
	 */
#if CONFIG_SKB_CHECK
	IS_SKB(skb);
#endif	
	skb_queue_tail(&backlog,skb);
	backlog_size++;
  
	/*
	 *	If any packet arrived, mark it for processing after the
	 *	hardware interrupt returns.
	 */

#ifdef CONFIG_NET_RUNONIRQ	/* Dont enable yet, needs some driver mods */
	net_bh();
#else
	mark_bh(NET_BH);
#endif
	return;
}


/*
 *	The old interface to fetch a packet from a device driver.
 *	This function is the base level entry point for all drivers that
 *	want to send a packet to the upper (protocol) levels.  It takes
 *	care of de-multiplexing the packet to the various modules based
 *	on their protocol ID.
 *
 *	Return values:	1 <- exit I can't do any more
 *			0 <- feed me more (i.e. "done", "OK"). 
 *
 *	This function is OBSOLETE and should not be used by any new
 *	device.
 */

int dev_rint(unsigned char *buff, long len, int flags, struct device *dev)
{
	static int dropping = 0;
	struct sk_buff *skb = NULL;
	unsigned char *to;
	int amount, left;
	int len2;

	if (dev == NULL || buff == NULL || len <= 0) 
		return(1);

	if (flags & IN_SKBUFF) 
	{
		skb = (struct sk_buff *) buff;
	}
	else
	{
		if (dropping) 
		{
			if (skb_peek(&backlog) != NULL)
				return(1);
			printk("INET: dev_rint: no longer dropping packets.\n");
			dropping = 0;
		}

		skb = alloc_skb(len, GFP_ATOMIC);
		if (skb == NULL) 
		{
			printk("dev_rint: packet dropped on %s (no memory) !\n",
			       dev->name);
			dropping = 1;
			return(1);
		}

		/* 
		 *	First we copy the packet into a buffer, and save it for later. We
		 *	in effect handle the incoming data as if it were from a circular buffer
		 */

		to = skb_put(skb,len);
		left = len;

		len2 = len;
		while (len2 > 0) 
		{
			amount = min(len2, (unsigned long) dev->rmem_end -
						(unsigned long) buff);
			memcpy(to, buff, amount);
			len2 -= amount;
			left -= amount;
			buff += amount;
			to += amount;
			if ((unsigned long) buff == dev->rmem_end)
				buff = (unsigned char *) dev->rmem_start;
		}
	}

	/*
	 *	Tag the frame and kick it to the proper receive routine
	 */
	 
	skb->dev = dev;
	skb->free = 1;

	netif_rx(skb);
	/*
	 *	OK, all done. 
	 */
	return(0);
}


/*
 *	This routine causes all interfaces to try to send some data. 
 */
 
void dev_transmit(void)
{
	struct device *dev;

	for (dev = dev_base; dev != NULL; dev = dev->next) 
	{
		if (dev->flags != 0 && !dev->tbusy) {
			/*
			 *	Kick the device
			 */
			dev_tint(dev);
		}
	}
}


/**********************************************************************************

			Receive Queue Processor
			
***********************************************************************************/

/*
 *	This is a single non-reentrant routine which takes the received packet
 *	queue and throws it at the networking layers in the hope that something
 *	useful will emerge.
 */
 
volatile char in_bh = 0;	/* Non-reentrant remember */

int in_net_bh()	/* Used by timer.c */
{
	return(in_bh==0?0:1);
}

/*
 *	When we are called the queue is ready to grab, the interrupts are
 *	on and hardware can interrupt and queue to the receive queue a we
 *	run with no problems.
 *	This is run as a bottom half after an interrupt handler that does
 *	mark_bh(NET_BH);
 */
 
void net_bh(void *tmp)
{
	struct sk_buff *skb;
	struct packet_type *ptype;
	struct packet_type *pt_prev;
	unsigned short type;

	/*
	 *	Atomically check and mark our BUSY state. 
	 */

	if (set_bit(1, (void*)&in_bh))
		return;

	/*
	 *	Can we send anything now? We want to clear the
	 *	decks for any more sends that get done as we
	 *	process the input. This also minimises the
	 *	latency on a transmit interrupt bh.
	 */

	dev_transmit();
  
	/*
	 *	Any data left to process. This may occur because a
	 *	mark_bh() is done after we empty the queue including
	 *	that from the device which does a mark_bh() just after
	 */

	cli();
	
	/*
	 *	While the queue is not empty
	 */
	 
	while((skb=skb_dequeue(&backlog))!=NULL)
	{
		/*
		 *	We have a packet. Therefore the queue has shrunk
		 */
  		backlog_size--;

		sti();
		
	       /*
		*	Bump the pointer to the next structure.
		*
		*	On entry to the protocol layer. skb->data and
		*	skb->h.raw point to the MAC and encapsulated data
		*/

		skb->h.raw = skb->data;

	       /*
		* 	Fetch the packet protocol ID. 
		*/
		
		type = skb->protocol;

		/*
		 *	We got a packet ID.  Now loop over the "known protocols"
		 * 	list. There are two lists. The ptype_all list of taps (normally empty)
		 *	and the main protocol list which is hashed perfectly for normal protocols.
		 */
		pt_prev = NULL;
		for (ptype = ptype_all; ptype!=NULL; ptype=ptype->next)
		{
			if(pt_prev)
			{
				struct sk_buff *skb2=skb_clone(skb, GFP_ATOMIC);
				if(skb2)
					pt_prev->func(skb2,skb->dev, pt_prev);
			}
			pt_prev=ptype;
		}
		
		for (ptype = ptype_base[ntohs(type)&15]; ptype != NULL; ptype = ptype->next) 
		{
			if (ptype->type == type && (!ptype->dev || ptype->dev==skb->dev))
			{
				/*
				 *	We already have a match queued. Deliver
				 *	to it and then remember the new match
				 */
				if(pt_prev)
				{
					struct sk_buff *skb2;

					skb2=skb_clone(skb, GFP_ATOMIC);

					/*
					 *	Kick the protocol handler. This should be fast
					 *	and efficient code.
					 */

					if(skb2)
						pt_prev->func(skb2, skb->dev, pt_prev);
				}
				/* Remember the current last to do */
				pt_prev=ptype;
			}
		} /* End of protocol list loop */
		
		/*
		 *	Is there a last item to send to ?
		 */

		if(pt_prev)
			pt_prev->func(skb, skb->dev, pt_prev);
		/*
		 * 	Has an unknown packet has been received ?
		 */
	 
		else
			kfree_skb(skb, FREE_WRITE);

		/*
		 *	Again, see if we can transmit anything now. 
		 *	[Ought to take this out judging by tests it slows
		 *	 us down not speeds us up]
		 */
#ifdef CONFIG_XMIT_EVERY
		dev_transmit();
#endif		
		cli();
  	}	/* End of queue loop */
  	
  	/*
  	 *	We have emptied the queue
  	 */
  	 
  	in_bh = 0;
	sti();
	
	/*
	 *	One last output flush.
	 */
	 
	dev_transmit();
}


/*
 *	This routine is called when an device driver (i.e. an
 *	interface) is ready to transmit a packet.
 */
 
void dev_tint(struct device *dev)
{
	int i;
	struct sk_buff *skb;
	unsigned long flags;
	
	save_flags(flags);	
	/*
	 *	Work the queues in priority order
	 */
	 
	for(i = 0;i < DEV_NUMBUFFS; i++) 
	{
		/*
		 *	Pull packets from the queue
		 */
		 

		cli();
		while((skb=skb_dequeue(&dev->buffs[i]))!=NULL)
		{
			/*
			 *	Stop anyone freeing the buffer while we retransmit it
			 */
			skb_device_lock(skb);
			restore_flags(flags);
			/*
			 *	Feed them to the output stage and if it fails
			 *	indicate they re-queue at the front.
			 */
			dev_queue_xmit(skb,dev,-i - 1);
			/*
			 *	If we can take no more then stop here.
			 */
			if (dev->tbusy)
				return;
			cli();
		}
	}
	restore_flags(flags);
}


/*
 *	Perform a SIOCGIFCONF call. This structure will change
 *	size shortly, and there is nothing I can do about it.
 *	Thus we will need a 'compatibility mode'.
 */

static int dev_ifconf(char *arg)
{
	struct ifconf ifc;
	struct ifreq ifr;
	struct device *dev;
	char *pos;
	int len;
	int err;

	/*
	 *	Fetch the caller's info block. 
	 */
	 
	err=verify_area(VERIFY_WRITE, arg, sizeof(struct ifconf));
	if(err)
	  	return err;
	memcpy_fromfs(&ifc, arg, sizeof(struct ifconf));
	len = ifc.ifc_len;
	pos = ifc.ifc_buf;

	/*
	 *	We now walk the device list filling each active device
	 *	into the array.
	 */
	 
	err=verify_area(VERIFY_WRITE,pos,len);
	if(err)
	  	return err;
  	
	/*
	 *	Loop over the interfaces, and write an info block for each. 
	 */

	for (dev = dev_base; dev != NULL; dev = dev->next) 
	{
        	if(!(dev->flags & IFF_UP))	/* Downed devices don't count */
	        	continue;
		memset(&ifr, 0, sizeof(struct ifreq));
		strcpy(ifr.ifr_name, dev->name);
		(*(struct sockaddr_in *) &ifr.ifr_addr).sin_family = dev->family;
		(*(struct sockaddr_in *) &ifr.ifr_addr).sin_addr.s_addr = dev->pa_addr;

		/*
		 *	Have we run out of space here ?
		 */
	
		if (len < sizeof(struct ifreq)) 
			break;

		/*
		 *	Write this block to the caller's space. 
		 */
		 
		memcpy_tofs(pos, &ifr, sizeof(struct ifreq));
		pos += sizeof(struct ifreq);
		len -= sizeof(struct ifreq);		
  	}

	/*
	 *	All done.  Write the updated control block back to the caller. 
	 */
	 
	ifc.ifc_len = (pos - ifc.ifc_buf);
	ifc.ifc_req = (struct ifreq *) ifc.ifc_buf;
	memcpy_tofs(arg, &ifc, sizeof(struct ifconf));
	
	/*
	 *	Report how much was filled in
	 */
	 
	return(pos - arg);
}


/*
 *	This is invoked by the /proc filesystem handler to display a device
 *	in detail.
 */

static int sprintf_stats(char *buffer, struct device *dev)
{
	struct enet_statistics *stats = (dev->get_stats ? dev->get_stats(dev): NULL);
	int size;
	
	if (stats)
		size = sprintf(buffer, "%6s:%7d %4d %4d %4d %4d %8d %4d %4d %4d %5d %4d\n",
		   dev->name,
		   stats->rx_packets, stats->rx_errors,
		   stats->rx_dropped + stats->rx_missed_errors,
		   stats->rx_fifo_errors,
		   stats->rx_length_errors + stats->rx_over_errors
		   + stats->rx_crc_errors + stats->rx_frame_errors,
		   stats->tx_packets, stats->tx_errors, stats->tx_dropped,
		   stats->tx_fifo_errors, stats->collisions,
		   stats->tx_carrier_errors + stats->tx_aborted_errors
		   + stats->tx_window_errors + stats->tx_heartbeat_errors);
	else
		size = sprintf(buffer, "%6s: No statistics available.\n", dev->name);

	return size;
}

/*
 *	Called from the PROCfs module. This now uses the new arbitrary sized /proc/net interface
 *	to create /proc/net/dev
 */
 
int dev_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	int len=0;
	off_t begin=0;
	off_t pos=0;
	int size;
	
	struct device *dev;


	size = sprintf(buffer, "Inter-|   Receive                  |  Transmit\n"
			    " face |packets errs drop fifo frame|packets errs drop fifo colls carrier\n");
	
	pos+=size;
	len+=size;
	

	for (dev = dev_base; dev != NULL; dev = dev->next) 
	{
		size = sprintf_stats(buffer+len, dev);
		len+=size;
		pos=begin+len;
				
		if(pos<offset)
		{
			len=0;
			begin=pos;
		}
		if(pos>offset+length)
			break;
	}
	
	*start=buffer+(offset-begin);	/* Start of wanted data */
	len-=(offset-begin);		/* Start slop */
	if(len>length)
		len=length;		/* Ending slop */
	return len;
}


/*
 *	This checks bitmasks for the ioctl calls for devices.
 */
 
static inline int bad_mask(unsigned long mask, unsigned long addr)
{
	if (addr & (mask = ~mask))
		return 1;
	mask = ntohl(mask);
	if (mask & (mask+1))
		return 1;
	return 0;
}

/*
 *	Perform the SIOCxIFxxx calls. 
 *
 *	The socket layer has seen an ioctl the address family thinks is
 *	for the device. At this point we get invoked to make a decision
 */
 
static int dev_ifsioc(void *arg, unsigned int getset)
{
	struct ifreq ifr;
	struct device *dev;
	int ret;

	/*
	 *	Fetch the caller's info block into kernel space
	 */

	int err=verify_area(VERIFY_WRITE, arg, sizeof(struct ifreq));
	if(err)
		return err;
	
	memcpy_fromfs(&ifr, arg, sizeof(struct ifreq));

	/*
	 *	See which interface the caller is talking about. 
	 */
	 
	if ((dev = dev_get(ifr.ifr_name)) == NULL) 
		return(-ENODEV);

	switch(getset) 
	{
		case SIOCGIFFLAGS:	/* Get interface flags */
			ifr.ifr_flags = dev->flags;
			goto rarok;

		case SIOCSIFFLAGS:	/* Set interface flags */
			{
				int old_flags = dev->flags;
				
				/*
				 *	We are not allowed to potentially close/unload
				 *	a device until we get this lock.
				 */
				
				dev_lock_wait();
				
				dev->flags = ifr.ifr_flags & (
					IFF_UP | IFF_BROADCAST | IFF_DEBUG | IFF_LOOPBACK |
					IFF_POINTOPOINT | IFF_NOTRAILERS | IFF_RUNNING |
					IFF_NOARP | IFF_PROMISC | IFF_ALLMULTI | IFF_SLAVE | IFF_MASTER
					| IFF_MULTICAST);
				/*
				 *	Load in the correct multicast list now the flags have changed.
				 */				

				dev_mc_upload(dev);

			  	/*
			  	 *	Have we downed the interface
			  	 */
		
				if ((old_flags & IFF_UP) && ((dev->flags & IFF_UP) == 0)) 
				{
					/*
					 *	Restore IFF_UP so dev_close knows to shut
					 *	it down. FIXME: Tidy me up sometime.
					 */
					dev->flags|=IFF_UP;
					ret = dev_close(dev);
				}
				else
				{
					/*
					 *	Have we upped the interface 
					 */
					 
			      		ret = (! (old_flags & IFF_UP) && (dev->flags & IFF_UP))
						? dev_open(dev) : 0;
					/* 
					 *	Check the flags.
					 */
					if(ret<0)
						dev->flags&=~IFF_UP;	/* Didn't open so down the if */
			  	}
	        	}
			break;
		
		case SIOCGIFADDR:	/* Get interface address (and family) */
			(*(struct sockaddr_in *)
				  &ifr.ifr_addr).sin_addr.s_addr = dev->pa_addr;
			(*(struct sockaddr_in *)
				  &ifr.ifr_addr).sin_family = dev->family;
			(*(struct sockaddr_in *)
				  &ifr.ifr_addr).sin_port = 0;
			goto rarok;
	
		case SIOCSIFADDR:	/* Set interface address (and family) */
			dev->pa_addr = (*(struct sockaddr_in *)
				 &ifr.ifr_addr).sin_addr.s_addr;
			dev->family = ifr.ifr_addr.sa_family;
			
#ifdef CONFIG_INET	
			/* This is naughty. When net-032e comes out It wants moving into the net032
			   code not the kernel. Till then it can sit here (SIGH) */		
			dev->pa_mask = ip_get_mask(dev->pa_addr);
#endif			
			dev->pa_brdaddr = dev->pa_addr | ~dev->pa_mask;
			ret = 0;
			break;
			
		case SIOCGIFBRDADDR:	/* Get the broadcast address */
			(*(struct sockaddr_in *)
				&ifr.ifr_broadaddr).sin_addr.s_addr = dev->pa_brdaddr;
			(*(struct sockaddr_in *)
				&ifr.ifr_broadaddr).sin_family = dev->family;
			(*(struct sockaddr_in *)
				&ifr.ifr_broadaddr).sin_port = 0;
			goto rarok;

		case SIOCSIFBRDADDR:	/* Set the broadcast address */
			dev->pa_brdaddr = (*(struct sockaddr_in *)
				&ifr.ifr_broadaddr).sin_addr.s_addr;
			ret = 0;
			break;
			
		case SIOCGIFDSTADDR:	/* Get the destination address (for point-to-point links) */
			(*(struct sockaddr_in *)
				&ifr.ifr_dstaddr).sin_addr.s_addr = dev->pa_dstaddr;
			(*(struct sockaddr_in *)
				&ifr.ifr_dstaddr).sin_family = dev->family;
			(*(struct sockaddr_in *)
				&ifr.ifr_dstaddr).sin_port = 0;
			goto rarok;
	
		case SIOCSIFDSTADDR:	/* Set the destination address (for point-to-point links) */
			dev->pa_dstaddr = (*(struct sockaddr_in *)
				&ifr.ifr_dstaddr).sin_addr.s_addr;
			ret = 0;
			break;
			
		case SIOCGIFNETMASK:	/* Get the netmask for the interface */
			(*(struct sockaddr_in *)
				&ifr.ifr_netmask).sin_addr.s_addr = dev->pa_mask;
			(*(struct sockaddr_in *)
				&ifr.ifr_netmask).sin_family = dev->family;
			(*(struct sockaddr_in *)
				&ifr.ifr_netmask).sin_port = 0;
			goto rarok;

		case SIOCSIFNETMASK: 	/* Set the netmask for the interface */
			{
				unsigned long mask = (*(struct sockaddr_in *)
					&ifr.ifr_netmask).sin_addr.s_addr;
				ret = -EINVAL;
				/*
				 *	The mask we set must be legal.
				 */
				if (bad_mask(mask,0))
					break;
				dev->pa_mask = mask;
				ret = 0;
			}
			break;
			
		case SIOCGIFMETRIC:	/* Get the metric on the interface (currently unused) */
			
			ifr.ifr_metric = dev->metric;
			goto  rarok;
			
		case SIOCSIFMETRIC:	/* Set the metric on the interface (currently unused) */
			dev->metric = ifr.ifr_metric;
			ret=0;
			break;
	
		case SIOCGIFMTU:	/* Get the MTU of a device */
			ifr.ifr_mtu = dev->mtu;
			goto rarok;
	
		case SIOCSIFMTU:	/* Set the MTU of a device */
		
			/*
			 *	MTU must be positive.
			 */
			 
			if(ifr.ifr_mtu<68)
				return -EINVAL;
			dev->mtu = ifr.ifr_mtu;
			ret = 0;
			break;
	
		case SIOCGIFMEM:	/* Get the per device memory space. We can add this but currently
					   do not support it */
			ret = -EINVAL;
			break;
		
		case SIOCSIFMEM:	/* Set the per device memory buffer space. Not applicable in our case */
			ret = -EINVAL;
			break;

		case OLD_SIOCGIFHWADDR:	/* Get the hardware address. This will change and SIFHWADDR will be added */
			memcpy(ifr.old_ifr_hwaddr,dev->dev_addr, MAX_ADDR_LEN);
			goto rarok;

		case SIOCGIFHWADDR:
			memcpy(ifr.ifr_hwaddr.sa_data,dev->dev_addr, MAX_ADDR_LEN);
			ifr.ifr_hwaddr.sa_family=dev->type;			
			goto rarok;
				
		case SIOCSIFHWADDR:
			if(dev->set_mac_address==NULL)
				return -EOPNOTSUPP;
			if(ifr.ifr_hwaddr.sa_family!=dev->type)
				return -EINVAL;
			ret=dev->set_mac_address(dev,ifr.ifr_hwaddr.sa_data);
			break;
			
		case SIOCGIFMAP:
			ifr.ifr_map.mem_start=dev->mem_start;
			ifr.ifr_map.mem_end=dev->mem_end;
			ifr.ifr_map.base_addr=dev->base_addr;
			ifr.ifr_map.irq=dev->irq;
			ifr.ifr_map.dma=dev->dma;
			ifr.ifr_map.port=dev->if_port;
			goto rarok;
			
		case SIOCSIFMAP:
			if(dev->set_config==NULL)
				return -EOPNOTSUPP;
			return dev->set_config(dev,&ifr.ifr_map);
			
		case SIOCADDMULTI:
			if(dev->set_multicast_list==NULL)
				return -EINVAL;
			if(ifr.ifr_hwaddr.sa_family!=AF_UNSPEC)
				return -EINVAL;
			dev_mc_add(dev,ifr.ifr_hwaddr.sa_data, dev->addr_len, 1);
			return 0;

		case SIOCDELMULTI:
			if(dev->set_multicast_list==NULL)
				return -EINVAL;
			if(ifr.ifr_hwaddr.sa_family!=AF_UNSPEC)
				return -EINVAL;
			dev_mc_delete(dev,ifr.ifr_hwaddr.sa_data,dev->addr_len, 1);
			return 0;
		/*
		 *	Unknown or private ioctl
		 */

		default:
			if((getset >= SIOCDEVPRIVATE) &&
			   (getset <= (SIOCDEVPRIVATE + 15))) {
				if(dev->do_ioctl==NULL)
					return -EOPNOTSUPP;
				ret=dev->do_ioctl(dev, &ifr, getset);
				memcpy_tofs(arg,&ifr,sizeof(struct ifreq));
				break;
			}
			
			ret = -EINVAL;
	}
	return(ret);
/*
 *	The load of calls that return an ifreq and ok (saves memory).
 */
rarok:
	memcpy_tofs(arg, &ifr, sizeof(struct ifreq));
	return 0;
}


/*
 *	This function handles all "interface"-type I/O control requests. The actual
 *	'doing' part of this is dev_ifsioc above.
 */

int dev_ioctl(unsigned int cmd, void *arg)
{
	switch(cmd) 
	{
		case SIOCGIFCONF:
			(void) dev_ifconf((char *) arg);
			return 0;

		/*
		 *	Ioctl calls that can be done by all.
		 */
		 
		case SIOCGIFFLAGS:
		case SIOCGIFADDR:
		case SIOCGIFDSTADDR:
		case SIOCGIFBRDADDR:
		case SIOCGIFNETMASK:
		case SIOCGIFMETRIC:
		case SIOCGIFMTU:
		case SIOCGIFMEM:
		case SIOCGIFHWADDR:
		case SIOCSIFHWADDR:
		case OLD_SIOCGIFHWADDR:
		case SIOCGIFSLAVE:
		case SIOCGIFMAP:
			return dev_ifsioc(arg, cmd);

		/*
		 *	Ioctl calls requiring the power of a superuser
		 */
		 
		case SIOCSIFFLAGS:
		case SIOCSIFADDR:
		case SIOCSIFDSTADDR:
		case SIOCSIFBRDADDR:
		case SIOCSIFNETMASK:
		case SIOCSIFMETRIC:
		case SIOCSIFMTU:
		case SIOCSIFMEM:
		case SIOCSIFMAP:
		case SIOCSIFSLAVE:
		case SIOCADDMULTI:
		case SIOCDELMULTI:
			if (!suser())
				return -EPERM;
			return dev_ifsioc(arg, cmd);
	
		case SIOCSIFLINK:
			return -EINVAL;

		/*
		 *	Unknown or private ioctl.
		 */	
		 
		default:
			if((cmd >= SIOCDEVPRIVATE) &&
			   (cmd <= (SIOCDEVPRIVATE + 15))) {
				return dev_ifsioc(arg, cmd);
			}
			return -EINVAL;
	}
}


/*
 *	Initialize the DEV module. At boot time this walks the device list and
 *	unhooks any devices that fail to initialise (normally hardware not 
 *	present) and leaves us with a valid list of present and active devices.
 *
 */
 
void dev_init(void)
{
	struct device *dev, **dp;

	/*
	 *	Add the devices.
	 *	If the call to dev->init fails, the dev is removed
	 *	from the chain disconnecting the device until the
	 *	next reboot.
	 */

	dp = &dev_base;
	while ((dev = *dp) != NULL)
	{
		int i;
		for (i = 0; i < DEV_NUMBUFFS; i++)  {
			skb_queue_head_init(dev->buffs + i);
		}

		if (dev->init && dev->init(dev)) 
		{
			/*
			 *	It failed to come up. Unhook it.
			 */
			*dp = dev->next;
		} 
		else
		{
			dp = &dev->next;
		}
	}
	proc_net_register(&(struct proc_dir_entry) {
		PROC_NET_DEV, 3, "dev",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_net_inode_operations,
		dev_get_info
	});
}

