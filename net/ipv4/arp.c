/* linux/net/inet/arp.c
 *
 * Copyright (C) 1994 by Florian  La Roche
 *
 * This module implements the Address Resolution Protocol ARP (RFC 826),
 * which is used to convert IP addresses (or in the future maybe other
 * high-level addresses into a low-level hardware address (like an Ethernet
 * address).
 *
 * FIXME:
 *	Experiment with better retransmit timers
 *	Clean up the timer deletions
 *	If you create a proxy entry set your interface address to the address
 *	and then delete it, proxies may get out of sync with reality - check this
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 *
 * Fixes:
 *		Alan Cox	:	Removed the ethernet assumptions in Florian's code
 *		Alan Cox	:	Fixed some small errors in the ARP logic
 *		Alan Cox	:	Allow >4K in /proc
 *		Alan Cox	:	Make ARP add its own protocol entry
 *
 *		Ross Martin     :       Rewrote arp_rcv() and arp_get_info()
 *		Stephen Henson	:	Add AX25 support to arp_get_info()
 *		Alan Cox	:	Drop data when a device is downed.
 *		Alan Cox	:	Use init_timer().
 *		Alan Cox	:	Double lock fixes.
 *		Martin Seine	:	Move the arphdr structure
 *					to if_arp.h for compatibility.
 *					with BSD based programs.
 *		Andrew Tridgell :       Added ARP netmask code and
 *					re-arranged proxy handling.
 *		Alan Cox	:	Changed to use notifiers.
 *		Niibe Yutaka	:	Reply for this device or proxies only.
 *		Alan Cox	:	Don't proxy across hardware types!
 *		Jonathan Naylor :	Added support for NET/ROM.
 *		Mike Shaver     :       RFC1122 checks.
 *		Jonathan Naylor :	Only lookup the hardware address for
 *					the correct hardware type.
 *		Germano Caronni	:	Assorted subtle races.
 *		Craig Schlenter :	Don't modify permanent entry 
 *					during arp_rcv.
 *		Russ Nelson	:	Tidied up a few bits.
 *		Alexey Kuznetsov:	Major changes to caching and behaviour,
 *					eg intelligent arp probing and generation
 *					of host down events.
 *		Alan Cox	:	Missing unlock in device events.
 *		Eckes		:	ARP ioctl control errors.
 *		Alexey Kuznetsov:	Arp free fix.
 *		Manuel Rodriguez:	Gratutious ARP.
 */

/* RFC1122 Status:
   2.3.2.1 (ARP Cache Validation):
     MUST provide mechanism to flush stale cache entries (OK)
     SHOULD be able to configure cache timeout (NOT YET)
     MUST throttle ARP retransmits (OK)
   2.3.2.2 (ARP Packet Queue):
     SHOULD save at least one packet from each "conversation" with an
       unresolved IP address.  (OK)
   950727 -- MS
*/
      
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/config.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/errno.h>
#include <linux/if_arp.h>
#include <linux/in.h>
#include <linux/mm.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/trdevice.h>
#include <linux/skbuff.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>

#include <net/ip.h>
#include <net/icmp.h>
#include <net/route.h>
#include <net/protocol.h>
#include <net/tcp.h>
#include <net/sock.h>
#include <net/arp.h>
#ifdef CONFIG_AX25
#include <net/ax25.h>
#ifdef CONFIG_NETROM
#include <net/netrom.h>
#endif
#endif
#ifdef CONFIG_NET_ALIAS
#include <linux/net_alias.h>
#endif

#include <asm/system.h>
#include <asm/segment.h>

#include <stdarg.h>

/*
 *	This structure defines the ARP mapping cache. As long as we make changes
 *	in this structure, we keep interrupts off. But normally we can copy the
 *	hardware address and the device pointer in a local variable and then 
 *	make any "long calls" to send a packet out.
 */

struct arp_table
{
	struct arp_table		*next;			/* Linked entry list 		*/
	unsigned long			last_used;		/* For expiry 			*/
	unsigned long			last_updated;		/* For expiry 			*/
	unsigned int			flags;			/* Control status 		*/
	u32				ip;			/* ip address of entry 		*/
	u32				mask;			/* netmask - used for generalised proxy arps (tridge) 		*/
	unsigned char			ha[MAX_ADDR_LEN];	/* Hardware address		*/
	struct device			*dev;			/* Device the entry is tied to 	*/

	/*
	 *	The following entries are only used for unresolved hw addresses.
	 */
	
	struct timer_list		timer;			/* expire timer 		*/
	int				retries;		/* remaining retries	 	*/
	struct sk_buff_head		skb;			/* list of queued packets 	*/
	struct hh_cache			*hh;
};


/*
 *	Configurable Parameters (don't touch unless you know what you are doing
 */

/*
 *	If an arp request is send, ARP_RES_TIME is the timeout value until the
 *	next request is send.
 * 	RFC1122: OK.  Throttles ARPing, as per 2.3.2.1. (MUST)
 *	The recommended minimum timeout is 1 second per destination.
 *	This timeout is prolongated to ARP_DEAD_RES_TIME, if
 *	destination does not respond.
 */

#define ARP_RES_TIME		(5*HZ)
#define ARP_DEAD_RES_TIME	(60*HZ)

/*
 *	The number of times an arp request is send, until the host is
 *	considered temporarily unreachable.
 */

#define ARP_MAX_TRIES		3

/*
 *	After that time, an unused entry is deleted from the arp table.
 */

#define ARP_TIMEOUT		(600*HZ)

/*
 *	How often is the function 'arp_check_retries' called.
 *	An unused entry is invalidated in the time between ARP_TIMEOUT and
 *	(ARP_TIMEOUT+ARP_CHECK_INTERVAL).
 */

#define ARP_CHECK_INTERVAL	(60*HZ)

/*
 *	The entry is reconfirmed by sending point-to-point ARP
 *	request after ARP_CONFIRM_INTERVAL. If destinations does not respond
 *	for ARP_CONFIRM_TIMEOUT, normal broadcast resolution scheme is started.
 */

#define ARP_CONFIRM_INTERVAL	(300*HZ)
#define ARP_CONFIRM_TIMEOUT	ARP_RES_TIME

static unsigned int arp_lock;
static unsigned int arp_bh_mask;

#define ARP_BH_BACKLOG	1

static struct arp_table *arp_backlog;

static void arp_run_bh(void);
static void arp_check_expire (unsigned long);  

static struct timer_list arp_timer =
	{ NULL, NULL, ARP_CHECK_INTERVAL, 0L, &arp_check_expire };

/*
 * The default arp netmask is just 255.255.255.255 which means it's
 * a single machine entry. Only proxy entries can have other netmasks
 */

#define DEF_ARP_NETMASK (~0)

/*
 * 	The size of the hash table. Must be a power of two.
 * 	Maybe we should remove hashing in the future for arp and concentrate
 * 	on Patrick Schaaf's Host-Cache-Lookup...
 */

#define ARP_TABLE_SIZE  16
#define FULL_ARP_TABLE_SIZE (ARP_TABLE_SIZE+1)

struct arp_table *arp_tables[FULL_ARP_TABLE_SIZE] =
{
	NULL,
};

#define arp_proxy_list arp_tables[ARP_TABLE_SIZE]

/*
 *	The last bits in the IP address are used for the cache lookup.
 *	A special entry is used for proxy arp entries
 */

#define HASH(paddr) 		(htonl(paddr) & (ARP_TABLE_SIZE - 1))

/*
 * Lock/unlock arp_table chains.
 */

static __inline__ void arp_fast_lock(void)
{
	ATOMIC_INCR(&arp_lock);
}

static __inline__ void arp_fast_unlock(void)
{
	ATOMIC_DECR(&arp_lock);
}

static __inline__ void arp_unlock(void)
{
	if (!ATOMIC_DECR_AND_CHECK(&arp_lock) && arp_bh_mask)
		arp_run_bh();
}

/*
 * Enqueue to FIFO list.
 */

static void arp_enqueue(struct arp_table **q, struct arp_table *entry)
{
	unsigned long flags;
	struct arp_table * tail;

	save_flags(flags);
	cli();
	tail = *q;
	if (!tail)
		entry->next = entry;
	else
	{
		entry->next = tail->next;
		tail->next = entry;
	}
	*q = entry;
	restore_flags(flags);
	return;
}

/*
 * Dequeue from FIFO list,
 * caller should mask interrupts.
 */

static struct arp_table * arp_dequeue(struct arp_table **q)
{
	struct arp_table * entry;

	if (*q)
	{
		entry = (*q)->next;
		(*q)->next = entry->next;
		if (entry->next == entry)
			*q = NULL;
		entry->next = NULL;
		return entry;
	}
	return NULL;
}

/*
 * Purge all linked skb's of the entry.
 */

static void arp_release_entry(struct arp_table *entry)
{
	struct sk_buff *skb;
	unsigned long flags;

	save_flags(flags);
	cli();
	/* Release the list of `skb' pointers. */
	while ((skb = skb_dequeue(&entry->skb)) != NULL)
	{
		skb_device_lock(skb);
		restore_flags(flags);
		dev_kfree_skb(skb, FREE_WRITE);
		cli();
	}
	restore_flags(flags);
	return;
}

/*
 * 	Release the entry and all resources linked to it: skb's, hh's, timer
 * 	and certainly memory.
 */

static void arp_free_entry(struct arp_table *entry)
{
	unsigned long flags;
	struct hh_cache *hh, *next;

	del_timer(&entry->timer);

	save_flags(flags);
	cli();
	arp_release_entry(entry);

	for (hh = entry->hh; hh; hh = next)
	{
		next = hh->hh_next;
		hh->hh_arp = NULL;
		hh->hh_uptodate = 0;
		if (!--hh->hh_refcnt)
			kfree_s(hh, sizeof(struct(struct hh_cache)));
	}
	restore_flags(flags);

	kfree_s(entry, sizeof(struct arp_table));
	return;
}

/*
 * How many users has this entry?
 */

static __inline__ int arp_count_hhs(struct arp_table * entry)
{
	struct hh_cache *hh, **hhp;
	int count = 0;

	hhp = &entry->hh;
	while ((hh=*hhp) != NULL)
	{
		if (hh->hh_refcnt == 1)
		{
			*hhp = hh->hh_next;
			kfree_s(hh, sizeof(struct hh_cache));
			continue;
		}
		count += hh->hh_refcnt-1;
		hhp = &hh->hh_next;
	}

	return count;
}

/*
 * Invalidate all hh's, so that higher level will not try to use it.
 */

static __inline__ void arp_invalidate_hhs(struct arp_table * entry)
{
	struct hh_cache *hh;

	for (hh=entry->hh; hh; hh=hh->hh_next)
		hh->hh_uptodate = 0;
}

/*
 * Signal to device layer, that hardware address may be changed.
 */

static __inline__ void arp_update_hhs(struct arp_table * entry)
{
	struct hh_cache *hh;

	for (hh=entry->hh; hh; hh=hh->hh_next)
		entry->dev->header_cache_update(hh, entry->dev, entry->ha);
}

/*
 *	Check if there are too old entries and remove them. If the ATF_PERM
 *	flag is set, they are always left in the arp cache (permanent entry).
 *      If an entry was not be confirmed  for ARP_CONFIRM_INTERVAL,
 *	declare it invalid and send point-to-point ARP request.
 *	If it will not be confirmed for ARP_CONFIRM_TIMEOUT,
 *	give it to shred by arp_expire_entry.
 */

static void arp_check_expire(unsigned long dummy)
{
	int i;
	unsigned long now = jiffies;

	del_timer(&arp_timer);

	if (!arp_lock)
	{
		arp_fast_lock();

		for (i = 0; i < ARP_TABLE_SIZE; i++)
		{
			struct arp_table *entry;
			struct arp_table **pentry;
		
			pentry = &arp_tables[i];

			while ((entry = *pentry) != NULL)
			{
				cli();
				if (now - entry->last_used > ARP_TIMEOUT
				    && !(entry->flags & ATF_PERM)
				    && !arp_count_hhs(entry))
				{
					*pentry = entry->next;
					sti();
#if RT_CACHE_DEBUG >= 2
					printk("arp_expire: %08x expired\n", entry->ip);
#endif
					arp_free_entry(entry);
				}
				else if (entry->last_updated
					 && now - entry->last_updated > ARP_CONFIRM_INTERVAL
					 && !(entry->flags & ATF_PERM))
				{
					struct device * dev = entry->dev;
					pentry = &entry->next;
					entry->flags &= ~ATF_COM;
					arp_invalidate_hhs(entry);
					sti();
					entry->retries = ARP_MAX_TRIES+1;
					del_timer(&entry->timer);
					entry->timer.expires = jiffies + ARP_CONFIRM_TIMEOUT;
					add_timer(&entry->timer);
					arp_send(ARPOP_REQUEST, ETH_P_ARP, entry->ip,
						 dev, dev->pa_addr, entry->ha,
						 dev->dev_addr, NULL);
#if RT_CACHE_DEBUG >= 2
					printk("arp_expire: %08x requires confirmation\n", entry->ip);
#endif
				}
				else
					pentry = &entry->next;	/* go to next entry */
			}
		}
		arp_unlock();
	}

	ip_rt_check_expire();

	/*
	 *	Set the timer again.
	 */

	arp_timer.expires = jiffies + ARP_CHECK_INTERVAL;
	add_timer(&arp_timer);
}

/*
 *	This function is called, if an entry is not resolved in ARP_RES_TIME.
 *	When more than MAX_ARP_TRIES retries was done, release queued skb's,
 *	but not discard entry itself if  it is in use.
 */

static void arp_expire_request (unsigned long arg)
{
	struct arp_table *entry = (struct arp_table *) arg;
	struct arp_table **pentry;
	unsigned long hash;
	unsigned long flags;

	save_flags(flags);
	cli();

	/*
	 *	Since all timeouts are handled with interrupts enabled, there is a
	 *	small chance, that this entry has just been resolved by an incoming
	 *	packet. This is the only race condition, but it is handled...
	 */
	
	if (entry->flags & ATF_COM)
	{
		restore_flags(flags);
		return;
	}

	if (arp_lock)
	{
#if RT_CACHE_DEBUG >= 1
		printk("arp_expire_request: %08x postponed\n", entry->ip);
#endif
		del_timer(&entry->timer);
		entry->timer.expires = jiffies + HZ/10;
		add_timer(&entry->timer);
		restore_flags(flags);
		return;
	}

	arp_fast_lock();
	restore_flags(flags);

	if (entry->last_updated && --entry->retries > 0)
	{
		struct device *dev = entry->dev;

#if RT_CACHE_DEBUG >= 2
		printk("arp_expire_request: %08x timed out\n", entry->ip);
#endif
		/* Set new timer. */
		del_timer(&entry->timer);
		entry->timer.expires = jiffies + ARP_RES_TIME;
		add_timer(&entry->timer);
		arp_send(ARPOP_REQUEST, ETH_P_ARP, entry->ip, dev, dev->pa_addr, 
			 NULL, dev->dev_addr, NULL);
		arp_unlock();
		return;
	}

	arp_release_entry(entry);

	cli();
	if (arp_count_hhs(entry))
	{
		struct device *dev = entry->dev;
#if RT_CACHE_DEBUG >= 2
		printk("arp_expire_request: %08x is dead\n", entry->ip);
#endif
		arp_release_entry(entry);
		entry->retries = ARP_MAX_TRIES;
		restore_flags(flags);
		entry->last_updated = 0;
		del_timer(&entry->timer);
		entry->timer.expires = jiffies + ARP_DEAD_RES_TIME;
		add_timer(&entry->timer);
		arp_send(ARPOP_REQUEST, ETH_P_ARP, entry->ip, dev, dev->pa_addr, 
			 NULL, dev->dev_addr, NULL);
		arp_unlock();
		return;
	}
	restore_flags(flags);

	hash = HASH(entry->ip);

	pentry = &arp_tables[hash];

	while (*pentry != NULL)
	{
		if (*pentry == entry)
		{
			cli();
			*pentry = entry->next;
			restore_flags(flags);
#if RT_CACHE_DEBUG >= 2
			printk("arp_expire_request: %08x is killed\n", entry->ip);
#endif
			arp_free_entry(entry);
			arp_unlock();
			return;
		}
		pentry = &(*pentry)->next;
	}
	printk("arp_expire_request: bug: ARP entry is lost!\n");
	arp_unlock();
}

/*
 *	Purge a device from the ARP queue
 */
 
int arp_device_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct device *dev=ptr;
	int i;
	
	if (event != NETDEV_DOWN)
		return NOTIFY_DONE;
	/*
	 *	This is a bit OTT - maybe we need some arp semaphores instead.
	 */

#if RT_CACHE_DEBUG >= 1	 
	if (arp_lock)
		printk("arp_device_event: bug\n");
#endif
	arp_fast_lock();

	for (i = 0; i < FULL_ARP_TABLE_SIZE; i++)
	{
		struct arp_table *entry;
		struct arp_table **pentry = &arp_tables[i];

		while ((entry = *pentry) != NULL)
		{
			if (entry->dev == dev)
			{
				*pentry = entry->next;	/* remove from list */
				arp_free_entry(entry);
			}
			else
				pentry = &entry->next;	/* go to next entry */
		}
	}
	arp_unlock();
	return NOTIFY_DONE;
}


/*
 *	Create and send an arp packet. If (dest_hw == NULL), we create a broadcast
 *	message.
 */

void arp_send(int type, int ptype, u32 dest_ip, 
	      struct device *dev, u32 src_ip, 
	      unsigned char *dest_hw, unsigned char *src_hw,
	      unsigned char *target_hw)
{
	struct sk_buff *skb;
	struct arphdr *arp;
	unsigned char *arp_ptr;

	/*
	 *	No arp on this interface.
	 */
	
	if (dev->flags&IFF_NOARP)
		return;

	/*
	 *	Allocate a buffer
	 */
	
	skb = alloc_skb(sizeof(struct arphdr)+ 2*(dev->addr_len+4)
				+ dev->hard_header_len, GFP_ATOMIC);
	if (skb == NULL)
	{
		printk("ARP: no memory to send an arp packet\n");
		return;
	}
	skb_reserve(skb, dev->hard_header_len);
	arp = (struct arphdr *) skb_put(skb,sizeof(struct arphdr) + 2*(dev->addr_len+4));
	skb->arp = 1;
	skb->dev = dev;
	skb->free = 1;
	skb->protocol = htons (ETH_P_IP);

	/*
	 *	Fill the device header for the ARP frame
	 */

	dev->hard_header(skb,dev,ptype,dest_hw?dest_hw:dev->broadcast,src_hw?src_hw:NULL,skb->len);

	/* Fill out the arp protocol part. */
	arp->ar_hrd = htons(dev->type);
#ifdef CONFIG_AX25
#ifdef CONFIG_NETROM
	arp->ar_pro = (dev->type == ARPHRD_AX25 || dev->type == ARPHRD_NETROM) ? htons(AX25_P_IP) : htons(ETH_P_IP);
#else
	arp->ar_pro = (dev->type != ARPHRD_AX25) ? htons(ETH_P_IP) : htons(AX25_P_IP);
#endif
#else
	arp->ar_pro = htons(ETH_P_IP);
#endif
	arp->ar_hln = dev->addr_len;
	arp->ar_pln = 4;
	arp->ar_op = htons(type);

	arp_ptr=(unsigned char *)(arp+1);

	memcpy(arp_ptr, src_hw, dev->addr_len);
	arp_ptr+=dev->addr_len;
	memcpy(arp_ptr, &src_ip,4);
	arp_ptr+=4;
	if (target_hw != NULL)
		memcpy(arp_ptr, target_hw, dev->addr_len);
	else
		memset(arp_ptr, 0, dev->addr_len);
	arp_ptr+=dev->addr_len;
	memcpy(arp_ptr, &dest_ip, 4);

	dev_queue_xmit(skb, dev, 0);
}

/*
 *	This will try to retransmit everything on the queue.
 */

static void arp_send_q(struct arp_table *entry)
{
	struct sk_buff *skb;

	unsigned long flags;

	/*
	 *	Empty the entire queue, building its data up ready to send
	 */
	
	if(!(entry->flags&ATF_COM))
	{
		printk("arp_send_q: incomplete entry for %s\n",
				in_ntoa(entry->ip));
		/* Can't flush the skb, because RFC1122 says to hang on to */
		/* at least one from any unresolved entry.  --MS */
		/* Whats happened is that someone has 'unresolved' the entry
		   as we got to use it - this 'can't happen' -- AC */
		return;
	}

	save_flags(flags);
	
	cli();
	while((skb = skb_dequeue(&entry->skb)) != NULL)
	{
		IS_SKB(skb);
		skb_device_lock(skb);
		restore_flags(flags);
		if(!skb->dev->rebuild_header(skb->data,skb->dev,skb->raddr,skb))
		{
			skb->arp  = 1;
			if(skb->sk==NULL)
				dev_queue_xmit(skb, skb->dev, 0);
			else
				dev_queue_xmit(skb,skb->dev,skb->sk->priority);
		}
	}
	restore_flags(flags);
}


/*
 *	Delete an ARP mapping entry in the cache.
 */

static void arp_destroy(struct arp_table * entry)
{
	struct arp_table *entry1;
	struct arp_table **pentry;

	if (entry->flags & ATF_PUBL)
		pentry = &arp_proxy_list;
	else
		pentry = &arp_tables[HASH(entry->ip)];

	while ((entry1 = *pentry) != NULL)
	{
		if (entry1 == entry)
		{
			*pentry = entry1->next;
			del_timer(&entry->timer);
			arp_free_entry(entry);
			return;
		}
		pentry = &entry1->next;
	}
}

/*
 *	Receive an arp request by the device layer. Maybe I rewrite it, to
 *	use the incoming packet for the reply. The time for the current
 *	"overhead" isn't that high...
 */

int arp_rcv(struct sk_buff *skb, struct device *dev, struct packet_type *pt)
{
/*
 *	We shouldn't use this type conversion. Check later.
 */
	
	struct arphdr *arp = (struct arphdr *)skb->h.raw;
	unsigned char *arp_ptr= (unsigned char *)(arp+1);
	struct arp_table *entry;
	struct arp_table *proxy_entry;
	unsigned long hash, grat=0;
	unsigned char ha[MAX_ADDR_LEN];	/* So we can enable ints again. */
	unsigned char *sha,*tha;
	u32 sip,tip;
	
/*
 *	The hardware length of the packet should match the hardware length
 *	of the device.  Similarly, the hardware types should match.  The
 *	device should be ARP-able.  Also, if pln is not 4, then the lookup
 *	is not from an IP number.  We can't currently handle this, so toss
 *	it. 
 */  
	if (arp->ar_hln != dev->addr_len    || 
     		dev->type != ntohs(arp->ar_hrd) || 
		dev->flags & IFF_NOARP          ||
		arp->ar_pln != 4)
	{
		kfree_skb(skb, FREE_READ);
		return 0;
		/* Should this be an error/printk?  Seems like something */
		/* you'd want to know about. Unless it's just !IFF_NOARP. -- MS */
	}

/*
 *	Another test.
 *	The logic here is that the protocol being looked up by arp should 
 *	match the protocol the device speaks.  If it doesn't, there is a
 *	problem, so toss the packet.
 */
/* Again, should this be an error/printk? -- MS */

  	switch (dev->type)
  	{
#ifdef CONFIG_AX25
		case ARPHRD_AX25:
			if(arp->ar_pro != htons(AX25_P_IP))
			{
				kfree_skb(skb, FREE_READ);
				return 0;
			}
			break;
#endif
#ifdef CONFIG_NETROM
		case ARPHRD_NETROM:
			if(arp->ar_pro != htons(AX25_P_IP))
			{
				kfree_skb(skb, FREE_READ);
				return 0;
			}
			break;
#endif
		case ARPHRD_ETHER:
		case ARPHRD_ARCNET:
			if(arp->ar_pro != htons(ETH_P_IP))
			{
				kfree_skb(skb, FREE_READ);
				return 0;
			}
			break;

		case ARPHRD_IEEE802:
			if(arp->ar_pro != htons(ETH_P_IP))
			{
				kfree_skb(skb, FREE_READ);
				return 0;
			}
			break;

		default:
			printk("ARP: dev->type mangled!\n");
			kfree_skb(skb, FREE_READ);
			return 0;
	}

/*
 *	Extract fields
 */

	sha=arp_ptr;
	arp_ptr += dev->addr_len;
	memcpy(&sip, arp_ptr, 4);
	arp_ptr += 4;
	tha=arp_ptr;
	arp_ptr += dev->addr_len;
	memcpy(&tip, arp_ptr, 4);
  
/* 
 *	Check for bad requests for 127.x.x.x and requests for multicast
 *	addresses.  If this is one such, delete it.
 */
	if (LOOPBACK(tip) || MULTICAST(tip))
	{
		kfree_skb(skb, FREE_READ);
		return 0;
	}

/*
 *  Process entry.  The idea here is we want to send a reply if it is a
 *  request for us or if it is a request for someone else that we hold
 *  a proxy for.  We want to add an entry to our cache if it is a reply
 *  to us or if it is a request for our address.  
 *  (The assumption for this last is that if someone is requesting our 
 *  address, they are probably intending to talk to us, so it saves time 
 *  if we cache their address.  Their address is also probably not in 
 *  our cache, since ours is not in their cache.)
 * 
 *  Putting this another way, we only care about replies if they are to
 *  us, in which case we add them to the cache.  For requests, we care
 *  about those for us and those for our proxies.  We reply to both,
 *  and in the case of requests for us we add the requester to the arp 
 *  cache.
 */

/*
 *	try to switch to alias device whose addr is tip or closest to sip.
 */

#ifdef CONFIG_NET_ALIAS
	if (tip != dev->pa_addr && net_alias_has(skb->dev)) 
	{
		/*
		 *	net_alias_dev_rcv_sel32 returns main dev if it fails to found other.
		 */
		dev = net_alias_dev_rcv_sel32(dev, AF_INET, sip, tip);

		if (dev->type != ntohs(arp->ar_hrd) || dev->flags & IFF_NOARP)
		{
			kfree_skb(skb, FREE_READ);
			return 0;
		}
	}
#endif

	if (arp->ar_op == htons(ARPOP_REQUEST))
	{ 
/*
 * Only reply for the real device address or when it's in our proxy tables
 */
		if (tip != dev->pa_addr)
		{
/*
 * 	To get in here, it is a request for someone else.  We need to
 * 	check if that someone else is one of our proxies.  If it isn't,
 * 	we can toss it.
 */
			grat = (sip == tip) && (sha == tha);
			arp_fast_lock();

			for (proxy_entry=arp_proxy_list;
			     proxy_entry;
			     proxy_entry = proxy_entry->next)
			{
				/* we will respond to a proxy arp request
				   if the masked arp table ip matches the masked
				   tip. This allows a single proxy arp table
				   entry to be used on a gateway machine to handle
				   all requests for a whole network, rather than
				   having to use a huge number of proxy arp entries
				   and having to keep them uptodate.
				   */
				if (proxy_entry->dev == dev &&
				    !((proxy_entry->ip^tip)&proxy_entry->mask))
					break;

			}
			if (proxy_entry)
			{
				if (grat)
				{
					if(!(proxy_entry->flags&ATF_PERM))
						arp_destroy(proxy_entry);
					goto gratuitous;
				}
				memcpy(ha, proxy_entry->ha, dev->addr_len);
				arp_unlock();
				arp_send(ARPOP_REPLY,ETH_P_ARP,sip,dev,tip,sha,ha, sha);
				kfree_skb(skb, FREE_READ);
				return 0;
			}
			else
			{
				if (grat) 
					goto gratuitous;
				arp_unlock();
				kfree_skb(skb, FREE_READ);
				return 0;
			}
		}
		else
		{
/*
 * 	To get here, it must be an arp request for us.  We need to reply.
 */
			arp_send(ARPOP_REPLY,ETH_P_ARP,sip,dev,tip,sha,dev->dev_addr,sha);
		}
	}
/*
 *	It is now an arp reply.
 */
	if(ip_chk_addr(tip)!=IS_MYADDR)
	{
/*
 *	Replies to other machines get tossed.
 */
 		kfree_skb(skb, FREE_READ);
 		return 0;
 	}
/*
 * Now all replies are handled.  Next, anything that falls through to here
 * needs to be added to the arp cache, or have its entry updated if it is 
 * there.
 */

	arp_fast_lock();

gratuitous:

	hash = HASH(sip);

	for (entry=arp_tables[hash]; entry; entry=entry->next)
		if (entry->ip == sip && entry->dev == dev)
			break;

	if (entry)
	{
/*
 *	Entry found; update it only if it is not a permanent entry.
 */
		if (!(entry->flags & ATF_PERM)) {
			memcpy(entry->ha, sha, dev->addr_len);
			entry->last_updated = jiffies;
		}
		if (!(entry->flags & ATF_COM))
		{
/*
 *	This entry was incomplete.  Delete the retransmit timer
 *	and switch to complete status.
 */
			del_timer(&entry->timer);
			entry->flags |= ATF_COM;
			arp_update_hhs(entry);
/* 
 *	Send out waiting packets. We might have problems, if someone is 
 *	manually removing entries right now -- entry might become invalid 
 *	underneath us.
 */
			arp_send_q(entry);
		}
	}
	else
	{
/*
 * 	No entry found.  Need to add a new entry to the arp table.
 */

		if (grat) 
			goto end;

		entry = (struct arp_table *)kmalloc(sizeof(struct arp_table),GFP_ATOMIC);
		if(entry == NULL)
		{
			arp_unlock();
			printk("ARP: no memory for new arp entry\n");
			kfree_skb(skb, FREE_READ);
			return 0;
		}

                entry->mask = DEF_ARP_NETMASK;
		entry->ip = sip;
		entry->flags = ATF_COM;
		entry->hh    = NULL;
		init_timer(&entry->timer);
		entry->timer.function = arp_expire_request;
		entry->timer.data = (unsigned long)entry;
		memcpy(entry->ha, sha, dev->addr_len);
		entry->last_updated = entry->last_used = jiffies;
/*
 *	make entry point to	'correct' device
 */

#ifdef CONFIG_NET_ALIAS
		entry->dev = dev;
#else
		entry->dev = skb->dev;
#endif
		skb_queue_head_init(&entry->skb);
		if (arp_lock == 1)
		{
			entry->next = arp_tables[hash];
			arp_tables[hash] = entry;
		}
		else
		{
#if RT_CACHE_DEBUG >= 1
			printk("arp_rcv: %08x backlogged\n", entry->ip);
#endif
			arp_enqueue(&arp_backlog, entry);
			arp_bh_mask |= ARP_BH_BACKLOG;
		}
	}

/*
 *	Replies have been sent, and entries have been added.  All done.
 */

end:
	kfree_skb(skb, FREE_READ);
	arp_unlock();
	return 0;
}

/*
 * Lookup ARP entry by (addr, dev) pair.
 * Flags: ATF_PUBL - search for proxy entries
 *	  ATF_NETMASK - search for proxy network entry.
 * NOTE:  should be called with locked ARP tables.
 */

static struct arp_table *arp_lookup(u32 paddr, unsigned short flags, struct device * dev)
{
	struct arp_table *entry;

	if (!(flags & ATF_PUBL))
	{
		for (entry = arp_tables[HASH(paddr)];
		     entry != NULL; entry = entry->next)
			if (entry->ip == paddr && (!dev || entry->dev == dev))
				break;
		return entry;
	}

	if (!(flags & ATF_NETMASK))
	{
		for (entry = arp_proxy_list;
		     entry != NULL; entry = entry->next)
			if (entry->ip == paddr && (!dev || entry->dev == dev))
				break;
		return entry;
	}

	for (entry=arp_proxy_list; entry != NULL; entry = entry->next)
		if (!((entry->ip^paddr)&entry->mask) && 
		                                  (!dev || entry->dev == dev))
			break;
	return entry;
}

/*
 *	Find an arp mapping in the cache. If not found, return false.
 */

int arp_query(unsigned char *haddr, u32 paddr, struct device * dev)
{
	struct arp_table *entry;

	arp_fast_lock();

	entry = arp_lookup(paddr, 0, dev);

	if (entry != NULL)
	{
		entry->last_used = jiffies;
		if (entry->flags & ATF_COM)
		{
			memcpy(haddr, entry->ha, dev->addr_len);
			arp_unlock();
			return 1;
		}
	}
	arp_unlock();
	return 0;
}


static int arp_set_predefined(int addr_hint, unsigned char * haddr, __u32 paddr, struct device * dev)
{
	switch (addr_hint)
	{
		case IS_MYADDR:
			printk("ARP: arp called for own IP address\n");
			memcpy(haddr, dev->dev_addr, dev->addr_len);
			return 1;
#ifdef CONFIG_IP_MULTICAST
		case IS_MULTICAST:
			if(dev->type==ARPHRD_ETHER || dev->type==ARPHRD_IEEE802)
			{
				u32 taddr;
				haddr[0]=0x01;
				haddr[1]=0x00;
				haddr[2]=0x5e;
				taddr=ntohl(paddr);
				haddr[5]=taddr&0xff;
				taddr=taddr>>8;
				haddr[4]=taddr&0xff;
				taddr=taddr>>8;
				haddr[3]=taddr&0x7f;
				return 1;
			}
		/*
		 *	If a device does not support multicast broadcast the stuff (eg AX.25 for now)
		 */
#endif
		
		case IS_BROADCAST:
			memcpy(haddr, dev->broadcast, dev->addr_len);
			return 1;
	}
	return 0;
}

/*
 *	Find an arp mapping in the cache. If not found, post a request.
 */

int arp_find(unsigned char *haddr, u32 paddr, struct device *dev,
	     u32 saddr, struct sk_buff *skb)
{
	struct arp_table *entry;
	unsigned long hash;

	if (arp_set_predefined(ip_chk_addr(paddr), haddr, paddr, dev))
	{
		if (skb)
			skb->arp = 1;
		return 0;
	}

	hash = HASH(paddr);
	arp_fast_lock();

	/*
	 *	Find an entry
	 */
	entry = arp_lookup(paddr, 0, dev);

	if (entry != NULL) 	/* It exists */
	{
		if (!(entry->flags & ATF_COM))
		{
			/*
			 *	A request was already send, but no reply yet. Thus
			 *	queue the packet with the previous attempt
			 */
			
			if (skb != NULL)
			{
				if (entry->last_updated)
				{
					skb_queue_tail(&entry->skb, skb);
					skb_device_unlock(skb);
				}
				/*
				 * If last_updated==0 host is dead, so
				 * drop skb's and set socket error.
				 */
				else
				{
#if 0				
					/*
					 * FIXME: ICMP HOST UNREACHABLE should be
					 *	  sent in this situation. --ANK
					 */
					if (skb->sk)
					{
						skb->sk->err = EHOSTDOWN;
						skb->sk->error_report(skb->sk);
					}
#else
					icmp_send(skb, ICMP_DEST_UNREACH, ICMP_HOST_UNREACH, 0, dev);
#endif										
					dev_kfree_skb(skb, FREE_WRITE);
				}
			}
			arp_unlock();
			return 1;
		}

		/*
		 *	Update the record
		 */
		
		entry->last_used = jiffies;
		memcpy(haddr, entry->ha, dev->addr_len);
		if (skb)
			skb->arp = 1;
		arp_unlock();
		return 0;
	}

	/*
	 *	Create a new unresolved entry.
	 */
	
	entry = (struct arp_table *) kmalloc(sizeof(struct arp_table),
					GFP_ATOMIC);
	if (entry != NULL)
	{
		entry->last_updated = entry->last_used = jiffies;
		entry->flags = 0;
		entry->ip = paddr;
		entry->mask = DEF_ARP_NETMASK;
		memset(entry->ha, 0, dev->addr_len);
		entry->dev = dev;
		entry->hh    = NULL;
		init_timer(&entry->timer);
		entry->timer.function = arp_expire_request;
		entry->timer.data = (unsigned long)entry;
		entry->timer.expires = jiffies + ARP_RES_TIME;
		skb_queue_head_init(&entry->skb);
		if (skb != NULL)
		{
			skb_queue_tail(&entry->skb, skb);
			skb_device_unlock(skb);
		}
		if (arp_lock == 1)
		{
			entry->next = arp_tables[hash];
			arp_tables[hash] = entry;
			add_timer(&entry->timer);
			entry->retries = ARP_MAX_TRIES;
		}
		else
		{
#if RT_CACHE_DEBUG >= 1
			printk("arp_find: %08x backlogged\n", entry->ip);
#endif
			arp_enqueue(&arp_backlog, entry);
			arp_bh_mask |= ARP_BH_BACKLOG;
		}
	}
	else if (skb != NULL)
		dev_kfree_skb(skb, FREE_WRITE);
	arp_unlock();

	/*
	 *	If we didn't find an entry, we will try to send an ARP packet.
	 */
	
	arp_send(ARPOP_REQUEST, ETH_P_ARP, paddr, dev, saddr, NULL, 
		 dev->dev_addr, NULL);

	return 1;
}


/*
 *	Write the contents of the ARP cache to a PROCfs file.
 */

#define HBUFFERLEN 30

int arp_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	int len=0;
	off_t pos=0;
	int size;
	struct arp_table *entry;
	char hbuffer[HBUFFERLEN];
	int i,j,k;
	const char hexbuf[] =  "0123456789ABCDEF";

	size = sprintf(buffer,"IP address       HW type     Flags       HW address            Mask     Device\n");

	pos+=size;
	len+=size;

	arp_fast_lock();

	for(i=0; i<FULL_ARP_TABLE_SIZE; i++)
	{
		for(entry=arp_tables[i]; entry!=NULL; entry=entry->next)
		{
/*
 *	Convert hardware address to XX:XX:XX:XX ... form.
 */
#ifdef CONFIG_AX25
#ifdef CONFIG_NETROM
			if (entry->dev->type == ARPHRD_AX25 || entry->dev->type == ARPHRD_NETROM)
			     strcpy(hbuffer,ax2asc((ax25_address *)entry->ha));
			else {
#else
			if(entry->dev->type==ARPHRD_AX25)
			     strcpy(hbuffer,ax2asc((ax25_address *)entry->ha));
			else {
#endif
#endif

			for(k=0,j=0;k<HBUFFERLEN-3 && j<entry->dev->addr_len;j++)
			{
				hbuffer[k++]=hexbuf[ (entry->ha[j]>>4)&15 ];
				hbuffer[k++]=hexbuf[  entry->ha[j]&15     ];
				hbuffer[k++]=':';
			}
			hbuffer[--k]=0;
	
#ifdef CONFIG_AX25
			}
#endif
			size = sprintf(buffer+len,
				"%-17s0x%-10x0x%-10x%s",
				in_ntoa(entry->ip),
				(unsigned int)entry->dev->type,
				entry->flags,
				hbuffer);
#if RT_CACHE_DEBUG < 2
			size += sprintf(buffer+len+size,
				 "     %-17s %s\n",
				 entry->mask==DEF_ARP_NETMASK ?
				 "*" : in_ntoa(entry->mask), entry->dev->name);
#else
			size += sprintf(buffer+len+size,
				 "     %-17s %s\t%ld\t%1d\n",
				 entry->mask==DEF_ARP_NETMASK ?
				 "*" : in_ntoa(entry->mask), entry->dev->name, 
				 entry->hh ? entry->hh->hh_refcnt : -1,
				 entry->hh ? entry->hh->hh_uptodate : 0);
#endif
	
			len += size;
			pos += size;
		  
			if (pos <= offset)
				len=0;
			if (pos >= offset+length)
				goto done;
		}
	}
done:
	arp_unlock();
  
	*start = buffer+len-(pos-offset);	/* Start of wanted data */
	len = pos-offset;			/* Start slop */
	if (len>length)
		len = length;			/* Ending slop */
	return len;
}



int arp_bind_cache(struct hh_cache ** hhp, struct device *dev, unsigned short htype, u32 paddr)
{
	struct arp_table *entry;
	struct hh_cache *hh = *hhp;
	int addr_hint;
	unsigned long flags;

	if (hh)
		return 1;

	if ((addr_hint = ip_chk_addr(paddr)) != 0)
	{
		unsigned char haddr[MAX_ADDR_LEN];
		if (hh)
			return 1;
		hh = kmalloc(sizeof(struct hh_cache), GFP_ATOMIC);
		if (!hh)
			return 1;
		arp_set_predefined(addr_hint, haddr, paddr, dev);
		hh->hh_uptodate = 0;
		hh->hh_refcnt = 1;
		hh->hh_arp = NULL;
		hh->hh_next = NULL;
		hh->hh_type = htype;
		*hhp = hh;
		dev->header_cache_update(hh, dev, haddr);
		return 0;
	}

	save_flags(flags);

	arp_fast_lock();

	entry = arp_lookup(paddr, 0, dev);

	if (entry)
	{
		cli();
		for (hh = entry->hh; hh; hh=hh->hh_next)
			if (hh->hh_type == htype)
				break;
		if (hh)
		{
			hh->hh_refcnt++;
			*hhp = hh;
			restore_flags(flags);
			arp_unlock();
			return 1;
		}
		restore_flags(flags);
	}

	hh = kmalloc(sizeof(struct hh_cache), GFP_ATOMIC);
	if (!hh)
	{
		arp_unlock();
		return 1;
	}

	hh->hh_uptodate = 0;
	hh->hh_refcnt = 1;
	hh->hh_arp = NULL;
	hh->hh_next = NULL;
	hh->hh_type = htype;

	if (entry)
	{
		dev->header_cache_update(hh, dev, entry->ha);
		*hhp = hh;
		cli();
		hh->hh_arp = (void*)entry;
		entry->hh = hh;
		hh->hh_refcnt++;
		restore_flags(flags);
		entry->last_used = jiffies;
		arp_unlock();
		return 0;
	}


	/*
	 *	Create a new unresolved entry.
	 */
	
	entry = (struct arp_table *) kmalloc(sizeof(struct arp_table),
					GFP_ATOMIC);
	if (entry == NULL)
	{
		kfree_s(hh, sizeof(struct hh_cache));
		arp_unlock();
		return 1;
	}

	entry->last_updated = entry->last_used = jiffies;
	entry->flags = 0;
	entry->ip = paddr;
	entry->mask = DEF_ARP_NETMASK;
	memset(entry->ha, 0, dev->addr_len);
	entry->dev = dev;
	entry->hh = hh;
	ATOMIC_INCR(&hh->hh_refcnt);
	init_timer(&entry->timer);
	entry->timer.function = arp_expire_request;
	entry->timer.data = (unsigned long)entry;
	entry->timer.expires = jiffies + ARP_RES_TIME;
	skb_queue_head_init(&entry->skb);

	if (arp_lock == 1)
	{
		unsigned long hash = HASH(paddr);
		cli();
		entry->next = arp_tables[hash];
		arp_tables[hash] = entry;
		hh->hh_arp = (void*)entry;
		entry->retries = ARP_MAX_TRIES;
		restore_flags(flags);

		add_timer(&entry->timer);
		arp_send(ARPOP_REQUEST, ETH_P_ARP, paddr, dev, dev->pa_addr, NULL, dev->dev_addr, NULL);
	}
	else
	{
#if RT_CACHE_DEBUG >= 1
		printk("arp_cache_bind: %08x backlogged\n", entry->ip);
#endif
		arp_enqueue(&arp_backlog, entry);
		arp_bh_mask |= ARP_BH_BACKLOG;
	}
	*hhp = hh;
	arp_unlock();
	return 0;
}

static void arp_run_bh()
{
	unsigned long flags;
	struct arp_table *entry, *entry1;
	struct hh_cache *hh;
	__u32 sip;

	save_flags(flags);
	cli();
	if (!arp_lock)
	{
		arp_fast_lock();

		while ((entry = arp_dequeue(&arp_backlog)) != NULL)
		{
			unsigned long hash;
			sti();
			sip = entry->ip;
			hash = HASH(sip);

			/* It's possible, that an entry with the same pair 
			 * (addr,type) was already created. Our entry is older,
			 * so it should be discarded.
			 */
			for (entry1=arp_tables[hash]; entry1; entry1=entry1->next)
				if (entry1->ip==sip && entry1->dev == entry->dev)
					break;

			if (!entry1)
			{
				struct device  * dev = entry->dev;
				cli();
				entry->next = arp_tables[hash];
				arp_tables[hash] = entry;
				for (hh=entry->hh; hh; hh=hh->hh_next)
					hh->hh_arp = (void*)entry;
				sti();
				del_timer(&entry->timer);
				entry->timer.expires = jiffies + ARP_RES_TIME;
				add_timer(&entry->timer);
				entry->retries = ARP_MAX_TRIES;
				arp_send(ARPOP_REQUEST, ETH_P_ARP, entry->ip, dev, dev->pa_addr, NULL, dev->dev_addr, NULL);
#if RT_CACHE_DEBUG >= 1
				printk("arp_run_bh: %08x reinstalled\n", sip);
#endif
			}
			else
			{
				struct sk_buff * skb;
				struct hh_cache * next;

				/* Discard entry, but preserve its hh's and
				 * skb's.
				 */
				cli();
				for (hh=entry->hh; hh; hh=next)
				{
					next = hh->hh_next;
					hh->hh_next = entry1->hh;
					entry1->hh = hh;
					hh->hh_arp = (void*)entry1;
				}
				entry->hh = NULL;

				/* Prune skb list from entry
				 * and graft it to entry1.
				 */
				while ((skb = skb_dequeue(&entry->skb)) != NULL)
				{
					skb_device_lock(skb);
					sti();
					skb_queue_tail(&entry1->skb, skb);
					skb_device_unlock(skb);
					cli();
				}
				sti();
				
#if RT_CACHE_DEBUG >= 1
				printk("arp_run_bh: entry %08x was born dead\n", entry->ip);
#endif
				arp_free_entry(entry);

				if (entry1->flags & ATF_COM)
				{
					arp_update_hhs(entry1);
					arp_send_q(entry1);
				}
			}
			cli();
		}
		arp_bh_mask  &= ~ARP_BH_BACKLOG;
		arp_unlock();
	}
	restore_flags(flags);
}

/*
 * Test if a hardware address is all zero
 */

static inline int empty(unsigned char * addr, int len)
{
	while (len > 0) {
		if (*addr)
			return 0;
		len--;
		addr++;
	}
	return 1;
}

/*
 *	Set (create) an ARP cache entry.
 */

static int arp_req_set(struct arpreq *r, struct device * dev)
{
	struct arp_table *entry;
	struct sockaddr_in *si;
	struct rtable *rt;
	struct device *dev1;
	unsigned char *ha;
	u32 ip;

	/*
	 *	Extract destination.
	 */
	
	si = (struct sockaddr_in *) &r->arp_pa;
	ip = si->sin_addr.s_addr;

	/*
	 *	Is it reachable ?
	 */

	if (ip_chk_addr(ip) == IS_MYADDR)
		dev1 = dev_get("lo");
	else {
		rt = ip_rt_route(ip, 0);
		if (!rt)
			return -ENETUNREACH;
		dev1 = rt->rt_dev;
		ip_rt_put(rt);
	}

	/* good guess about the device if it isn't a ATF_PUBL entry */
	if (!dev) {
		if (dev1->flags&(IFF_LOOPBACK|IFF_NOARP))
			return -ENODEV;
		dev = dev1;
	}

	/* this needs to be checked only for dev=dev1 but it doesnt hurt */	
	if (r->arp_ha.sa_family != dev->type)	
		return -EINVAL;
		
	if (((r->arp_flags & ATF_PUBL) && dev == dev1) ||
	    (!(r->arp_flags & ATF_PUBL) && dev != dev1))
		return -EINVAL;

#if RT_CACHE_DEBUG >= 1
	if (arp_lock)
		printk("arp_req_set: bug\n");
#endif
	arp_fast_lock();

	/*
	 *	Is there an existing entry for this address?
	 */

	/*
	 *	Find the entry
	 */
	
	entry = arp_lookup(ip, r->arp_flags & ~ATF_NETMASK, dev);

	if (entry)
	{
		arp_destroy(entry);
		entry = NULL;
	}

	/*
	 *	Do we need to create a new entry
	 */
	
	if (entry == NULL)
	{
		entry = (struct arp_table *) kmalloc(sizeof(struct arp_table),
					GFP_ATOMIC);
		if (entry == NULL)
		{
			arp_unlock();
			return -ENOMEM;
		}
		entry->ip = ip;
		entry->hh = NULL;
		init_timer(&entry->timer);
		entry->timer.function = arp_expire_request;
		entry->timer.data = (unsigned long)entry;

		if (r->arp_flags & ATF_PUBL)
		{
			cli();
			entry->next = arp_proxy_list;
			arp_proxy_list = entry;
			sti();
		}
		else
		{
			unsigned long hash = HASH(ip);
			cli();
			entry->next = arp_tables[hash];
			arp_tables[hash] = entry;
			sti();
		}
		skb_queue_head_init(&entry->skb);
	}
	/*
	 *	We now have a pointer to an ARP entry.  Update it!
	 */
	ha = r->arp_ha.sa_data;
	if ((r->arp_flags & ATF_COM) && empty(ha, dev->addr_len))
		ha = dev->dev_addr;
	memcpy(entry->ha, ha, dev->addr_len);
	entry->last_updated = entry->last_used = jiffies;
	entry->flags = r->arp_flags | ATF_COM;
	if ((entry->flags & ATF_PUBL) && (entry->flags & ATF_NETMASK))
	{
		si = (struct sockaddr_in *) &r->arp_netmask;
		entry->mask = si->sin_addr.s_addr;
	}
	else
		entry->mask = DEF_ARP_NETMASK;
	entry->dev = dev;
	arp_update_hhs(entry);
	arp_unlock();
	return 0;
}



/*
 *	Get an ARP cache entry.
 */

static int arp_req_get(struct arpreq *r, struct device *dev)
{
	struct arp_table *entry;
	struct sockaddr_in *si;

	si = (struct sockaddr_in *) &r->arp_pa;

#if RT_CACHE_DEBUG >= 1
	if (arp_lock)
		printk("arp_req_set: bug\n");
#endif
	arp_fast_lock();

	entry = arp_lookup(si->sin_addr.s_addr, r->arp_flags|ATF_NETMASK, dev);

	if (entry == NULL)
	{
		arp_unlock();
		return -ENXIO;
	}

	/*
	 *	We found it; copy into structure.
	 */
	
	memcpy(r->arp_ha.sa_data, &entry->ha, entry->dev->addr_len);
	r->arp_ha.sa_family = entry->dev->type;
	r->arp_flags = entry->flags;
	strncpy(r->arp_dev, entry->dev->name, 16);
	arp_unlock();
	return 0;
}

static int arp_req_delete(struct arpreq *r, struct device * dev)
{
	struct arp_table *entry;
	struct sockaddr_in *si;

	si = (struct sockaddr_in *) &r->arp_pa;
#if RT_CACHE_DEBUG >= 1
	if (arp_lock)
		printk("arp_req_delete: bug\n");
#endif
	arp_fast_lock();

	if (!(r->arp_flags & ATF_PUBL))
	{
		for (entry = arp_tables[HASH(si->sin_addr.s_addr)];
		     entry != NULL; entry = entry->next)
			if (entry->ip == si->sin_addr.s_addr 
			    && (!dev || entry->dev == dev))
			{
				arp_destroy(entry);
				arp_unlock();
				return 0;
			}
	}
	else
	{
		for (entry = arp_proxy_list;
		     entry != NULL; entry = entry->next)
			if (entry->ip == si->sin_addr.s_addr 
			    && (!dev || entry->dev == dev)) 
			{
				arp_destroy(entry);
				arp_unlock();
				return 0;
			}
	}

	arp_unlock();
	return -ENXIO;
}

/*
 *	Handle an ARP layer I/O control request.
 */

int arp_ioctl(unsigned int cmd, void *arg)
{
	int err;
	struct arpreq r;

	struct device * dev = NULL;

	switch(cmd)
	{
		case SIOCDARP:
		case SIOCSARP:
			if (!suser())
				return -EPERM;
		case SIOCGARP:
			err = verify_area(VERIFY_READ, arg, sizeof(struct arpreq));
			if (err)
				return err;
			memcpy_fromfs(&r, arg, sizeof(struct arpreq));
			break;
		case OLD_SIOCDARP:
		case OLD_SIOCSARP:
			if (!suser())
				return -EPERM;
		case OLD_SIOCGARP:
			err = verify_area(VERIFY_READ, arg, sizeof(struct arpreq_old));
			if (err)
				return err;
			memcpy_fromfs(&r, arg, sizeof(struct arpreq_old));
			memset(&r.arp_dev, 0, sizeof(r.arp_dev));
			break;
		default:
			return -EINVAL;
	}

	if (r.arp_pa.sa_family != AF_INET)
		return -EPFNOSUPPORT;
	if (((struct sockaddr_in *)&r.arp_pa)->sin_addr.s_addr == 0)
		return -EINVAL;

	if (r.arp_dev[0])
	{
		if ((dev = dev_get(r.arp_dev)) == NULL)
			return -ENODEV;

		if (!r.arp_ha.sa_family)
			r.arp_ha.sa_family = dev->type;
		else if (r.arp_ha.sa_family != dev->type)
			return -EINVAL;
	}
	else
	{
		if ((r.arp_flags & ATF_PUBL) &&
		    ((cmd == SIOCSARP) || (cmd == OLD_SIOCSARP))) {
			if ((dev = dev_getbytype(r.arp_ha.sa_family)) == NULL)
				return -ENODEV;
		}
	}		 

	switch(cmd)
	{
		case SIOCDARP:
		        return arp_req_delete(&r, dev);
		case SIOCSARP:
			return arp_req_set(&r, dev);
		case OLD_SIOCDARP:
			/* old  SIOCDARP destoyes both
			 * normal and proxy mappings
			 */
			r.arp_flags &= ~ATF_PUBL;
			err = arp_req_delete(&r, dev);
			r.arp_flags |= ATF_PUBL;
			if (!err)
				arp_req_delete(&r, dev);
			else
				err = arp_req_delete(&r, dev);
			return err;
		case OLD_SIOCSARP:
			err = arp_req_set(&r, dev);
			/* old SIOCSARP works so funny,
			 * that its behaviour can be emulated
			 * only approximately 8).
			 * It should work. --ANK
			 */
			if (r.arp_flags & ATF_PUBL)
			{	
				r.arp_flags &= ~ATF_PUBL;
				arp_req_delete(&r, dev);
			}
			return err;
		case SIOCGARP:
			err = verify_area(VERIFY_WRITE, arg, sizeof(struct arpreq));
			if (err)
				return err;
			err = arp_req_get(&r, dev);
			if (!err)
				memcpy_tofs(arg, &r, sizeof(r));
			return err;
		case OLD_SIOCGARP:
			err = verify_area(VERIFY_WRITE, arg, sizeof(struct arpreq_old));
			if (err)
				return err;
			r.arp_flags &= ~ATF_PUBL;
			err = arp_req_get(&r, dev);
			if (err < 0)
			{
				r.arp_flags |= ATF_PUBL;
				err = arp_req_get(&r, dev);
			}
			if (!err)
				memcpy_tofs(arg, &r, sizeof(struct arpreq_old));
			return err;
	}
	/*NOTREACHED*/
	return 0;
}


/*
 *	Called once on startup.
 */

static struct packet_type arp_packet_type =
{
	0,	/* Should be: __constant_htons(ETH_P_ARP) - but this _doesn't_ come out constant! */
	NULL,		/* All devices */
	arp_rcv,
	NULL,
	NULL
};

static struct notifier_block arp_dev_notifier={
	arp_device_event,
	NULL,
	0
};

void arp_init (void)
{
	/* Register the packet type */
	arp_packet_type.type=htons(ETH_P_ARP);
	dev_add_pack(&arp_packet_type);
	/* Start with the regular checks for expired arp entries. */
	add_timer(&arp_timer);
	/* Register for device down reports */
	register_netdevice_notifier(&arp_dev_notifier);

#ifdef CONFIG_PROC_FS
	proc_net_register(&(struct proc_dir_entry) {
		PROC_NET_ARP, 3, "arp",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_net_inode_operations,
		arp_get_info
	});
#endif
}

