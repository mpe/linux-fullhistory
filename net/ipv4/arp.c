/* linux/net/inet/arp.c
 *
 * Copyright (C) 1994 by Florian  La Roche
 *
 * This module implements the Address Resolution Protocol ARP (RFC 826),
 * which is used to convert IP addresses (or in the future maybe other
 * high-level addresses) into a low-level hardware address (like an Ethernet
 * address).
 *
 * FIXME:
 *	Experiment with better retransmit timers
 *	Clean up the timer deletions
 *	If you create a proxy entry, set your interface address to the address
 *	and then delete it, proxies may get out of sync with reality - 
 *	check this.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * Fixes:
 *		Alan Cox	:	Removed the ethernet assumptions in 
 *					Florian's code
 *		Alan Cox	:	Fixed some small errors in the ARP 
 *					logic
 *		Alan Cox	:	Allow >4K in /proc
 *		Alan Cox	:	Make ARP add its own protocol entry
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
 *					eg intelligent arp probing and 
 *					generation
 *					of host down events.
 *		Alan Cox	:	Missing unlock in device events.
 *		Eckes		:	ARP ioctl control errors.
 *		Alexey Kuznetsov:	Arp free fix.
 *		Manuel Rodriguez:	Gratuitous ARP.
 *              Jonathan Layes  :       Added arpd support through kerneld 
 *                                      message queue (960314)
 *		Mike Shaver	:	/proc/sys/net/ipv4/arp_* support
 */

/* RFC1122 Status:
   2.3.2.1 (ARP Cache Validation):
     MUST provide mechanism to flush stale cache entries (OK)
     SHOULD be able to configure cache timeout (OK)
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
#include <linux/in.h>
#include <linux/mm.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
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
#ifdef CONFIG_ARPD
#include <net/netlink.h>
#endif

#include <asm/system.h>
#include <asm/segment.h>

#include <stdarg.h>

/*
 *	Configurable Parameters
 */

/*
 *	After that time, an unused entry is deleted from the arp table.
 *	RFC1122 recommends set it to 60*HZ, if your site uses proxy arp
 *	and dynamic routing.
 */

#define ARP_TIMEOUT		(60*HZ)

int sysctl_arp_timeout = ARP_TIMEOUT;

/*
 *	How often is ARP cache checked for expire.
 *	It is useless to set ARP_CHECK_INTERVAL > ARP_TIMEOUT
 */

#define ARP_CHECK_INTERVAL	(60*HZ)

int sysctl_arp_check_interval = ARP_CHECK_INTERVAL;

/*
 *	Soft limit on ARP cache size.
 *	Note that this number should be greater than
 *	number of simultaneously opened sockets, or else
 *	hardware header cache will not be efficient.
 */

#if RT_CACHE_DEBUG >= 2
#define ARP_MAXSIZE	4
#else
#ifdef CONFIG_ARPD
#define ARP_MAXSIZE	64
#else
#define ARP_MAXSIZE	256
#endif /* CONFIG_ARPD */
#endif

/*
 *	If an arp request is send, ARP_RES_TIME is the timeout value until the
 *	next request is send.
 * 	RFC1122: OK.  Throttles ARPing, as per 2.3.2.1. (MUST)
 *	The recommended minimum timeout is 1 second per destination.
 *
 */

#define ARP_RES_TIME		(5*HZ)

int sysctl_arp_res_time = ARP_RES_TIME;

/*
 *	The number of times an broadcast arp request is send, until
 *	the host is considered temporarily unreachable.
 */

#define ARP_MAX_TRIES		3

int sysctl_arp_max_tries = ARP_MAX_TRIES;

/*
 *	The entry is reconfirmed by sending point-to-point ARP
 *	request after ARP_CONFIRM_INTERVAL.
 *	RFC1122 recommends 60*HZ.
 *
 *	Warning: there exist nodes, that answer only broadcast
 *	ARP requests (Cisco-4000 in hot standby mode?)
 *	Now arp code should work with such nodes, but
 *	it still will generate redundant broadcast requests, so that
 *	this interval should be enough long.
 */

#define ARP_CONFIRM_INTERVAL	(300*HZ)

int sysctl_arp_confirm_interval = ARP_CONFIRM_INTERVAL;

/*
 *	We wait for answer to unicast request for ARP_CONFIRM_TIMEOUT.
 */

#define ARP_CONFIRM_TIMEOUT	ARP_RES_TIME

int sysctl_arp_confirm_timeout = ARP_CONFIRM_TIMEOUT;

/*
 *	The number of times an unicast arp request is retried, until
 *	the cache entry is considered suspicious.
 *	Value 0 means that no unicast pings will be sent.
 *	RFC1122 recommends 2.
 */

#define ARP_MAX_PINGS		1

int sysctl_arp_max_pings = ARP_MAX_PINGS;

/*
 *	When a host is dead, but someone tries to connect it,
 *	we do not remove corresponding cache entry (it would
 *	be useless, it will be created again immediately)
 *	Instead we prolongate interval between broadcasts
 *	to ARP_DEAD_RES_TIME.
 *	This interval should be not very long.
 *	(When the host will be up again, we will notice it only
 *	when ARP_DEAD_RES_TIME expires, or when the host will arp us.
 */

#define ARP_DEAD_RES_TIME	(60*HZ)

int sysctl_arp_dead_res_time = ARP_DEAD_RES_TIME;

/*
 *	This structure defines the ARP mapping cache.
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
	struct hh_cache			*hh;			/* Hardware headers chain	*/

	/*
	 *	The following entries are only used for unresolved hw addresses.
	 */
	
	struct timer_list		timer;			/* expire timer 		*/
	int				retries;		/* remaining retries	 	*/
	struct sk_buff_head		skb;			/* list of queued packets 	*/
};


static atomic_t arp_size = 0;

#ifdef CONFIG_ARPD
static int arpd_not_running;
static int arpd_stamp;
#endif

static unsigned int arp_bh_mask;

#define ARP_BH_BACKLOG	1

/*
 *	Backlog for ARP updates.
 */
static struct arp_table *arp_backlog;

/*
 *	Backlog for incomplete entries.
 */
static struct arp_table *arp_req_backlog;


static void arp_run_bh(void);
static void arp_check_expire (unsigned long);  
static int  arp_update (u32 sip, char *sha, struct device * dev,
	    unsigned long updated, struct arp_table *ientry, int grat);

static struct timer_list arp_timer =
	{ NULL, NULL, ARP_CHECK_INTERVAL, 0L, &arp_check_expire };

/*
 * The default arp netmask is just 255.255.255.255 which means it's
 * a single machine entry. Only proxy entries can have other netmasks
 */

#define DEF_ARP_NETMASK (~0)

/*
 * 	The size of the hash table. Must be a power of two.
 */

#define ARP_TABLE_SIZE		16
#define FULL_ARP_TABLE_SIZE	(ARP_TABLE_SIZE+1)

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
 *	ARP cache semaphore.
 *
 *	Every time when someone wants to traverse arp table,
 *	he MUST call arp_fast_lock.
 *	It will guarantee that arp cache list will not change
 *	by interrupts and the entry that you found will not
 *	disappear unexpectedly.
 *	
 *	If you want to modify arp cache lists, you MUST
 *	call arp_fast_lock, and check that you are the only
 *	owner of semaphore (arp_lock == 1). If it is not the case
 *	you can defer your operation or forgot it,
 *	but DO NOT TOUCH lists.
 *
 *	However, you are allowed to change arp entry contents.
 *
 *	Assumptions:
 *	     -- interrupt code MUST have lock/unlock balanced,
 *		you cannot lock cache on interrupt and defer unlocking
 *		to callback.
 *		In particular, it means that lock/unlock are allowed
 *		to be non-atomic. They are made atomic, but it was not
 *		necessary.
 *	     -- nobody is allowed to sleep while
 *		it keeps arp locked. (route cache has similar locking
 *		scheme, but allows sleeping)
 *		
 */

static atomic_t arp_lock;

#define ARP_LOCKED() (arp_lock != 1)

static __inline__ void arp_fast_lock(void)
{
	atomic_inc(&arp_lock);
}

static __inline__ void arp_unlock(void)
{
	if (atomic_dec_and_test(&arp_lock) && arp_bh_mask)
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

static void arp_purge_send_q(struct arp_table *entry)
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
 *	The entry should be already removed from lists.
 */

static void arp_free_entry(struct arp_table *entry)
{
	unsigned long flags;
	struct hh_cache *hh, *next;

	del_timer(&entry->timer);
	arp_purge_send_q(entry);

	save_flags(flags);
	cli();
	hh = entry->hh;
	entry->hh = NULL;
	restore_flags(flags);

	for ( ; hh; hh = next)
	{
		next = hh->hh_next;
		hh->hh_uptodate = 0;
		hh->hh_next = NULL;
		hh->hh_arp = NULL;
		if (atomic_dec_and_test(&hh->hh_refcnt))
			kfree_s(hh, sizeof(struct(struct hh_cache)));
	}

	kfree_s(entry, sizeof(struct arp_table));
	atomic_dec(&arp_size);
	return;
}

/*
 *	Hardware header cache.
 *
 *	BEWARE! Hardware header cache has no locking, so that
 *	it requires especially careful handling.
 *	It is the only part of arp+route, where a list
 *	should be traversed with masked interrupts.
 *	Luckily, this list contains one element 8), as rule.
 */

/*
 *	How many users has this entry?
 *	The answer is reliable only when interrupts are masked.
 */

static __inline__ int arp_count_hhs(struct arp_table * entry)
{
	struct hh_cache *hh;
	int count = 0;

	for (hh = entry->hh; hh; hh = hh->hh_next)
		count += hh->hh_refcnt-1;

	return count;
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
 *	Invalidate all hh's, so that higher level will not try to use it.
 */

static __inline__ void arp_invalidate_hhs(struct arp_table * entry)
{
	struct hh_cache *hh;

	for (hh=entry->hh; hh; hh=hh->hh_next)
		hh->hh_uptodate = 0;
}

/*
 *	Atomic attaching new hh entry.
 *	Return 1, if entry has been freed, rather than attached.
 */

static int arp_set_hh(struct hh_cache **hhp, struct hh_cache *hh)
{
	unsigned long flags;
	struct hh_cache *hh1;
	struct arp_table *entry;

	atomic_inc(&hh->hh_refcnt);

	save_flags(flags);
	cli();
	if ((hh1 = *hhp) == NULL)
	{
		*hhp = hh;
		restore_flags(flags);
		return 0;
	}

	entry = (struct arp_table*)hh->hh_arp;

	/*
	 *	An hh1 entry is already attached to this point.
	 *	Is it not linked to arp entry? Link it!
	 */
	if (!hh1->hh_arp && entry)
	{
		atomic_inc(&hh1->hh_refcnt);
		hh1->hh_next = entry->hh;
		entry->hh = hh1;
		hh1->hh_arp = (void*)entry;
		restore_flags(flags);

		if (entry->flags & ATF_COM)
			entry->dev->header_cache_update(hh1, entry->dev, entry->ha);
#if RT_CACHE_DEBUG >= 1
		printk("arp_set_hh: %08x is reattached. Good!\n", entry->ip);
#endif
	}
#if RT_CACHE_DEBUG >= 1
	else if (entry)
		printk("arp_set_hh: %08x rr1 ok!\n", entry->ip);
#endif
	restore_flags(flags);
	if (atomic_dec_and_test(&hh->hh_refcnt))
		kfree_s(hh, sizeof(struct hh_cache));
	return 1;
}

static __inline__ struct hh_cache * arp_alloc_hh(int htype)
{
	struct hh_cache *hh;
	hh = kmalloc(sizeof(struct hh_cache), GFP_ATOMIC);
	if (hh)
	{
		memset(hh, 0, sizeof(struct hh_cache));
		hh->hh_type = htype;
	}
	return hh;
}

/*
 * Test if a hardware address is all zero
 */

static __inline__ int empty(unsigned char * addr, int len)
{
	while (len > 0)
	{
		if (*addr)
			return 0;
		len--;
		addr++;
	}
	return 1;
}


#ifdef CONFIG_ARPD

/*
 *	Send ARPD message.
 */
static void arpd_send(int req, u32 addr, struct device * dev, char *ha,
		      unsigned long updated)
{
	int retval;
	struct sk_buff *skb;
	struct arpd_request *arpreq;

	if (arpd_not_running)
		return;

	skb = alloc_skb(sizeof(struct arpd_request), GFP_ATOMIC);
	if (skb == NULL)
		return;

	skb->free=1;
	arpreq=(struct arpd_request *)skb_put(skb, sizeof(struct arpd_request));
	arpreq->req = req;
	arpreq->ip  = addr;
	arpreq->dev = (unsigned long)dev;
	arpreq->stamp = arpd_stamp;
	arpreq->updated = updated;
	if (ha)
		memcpy(arpreq->ha, ha, sizeof(arpreq->ha));

	retval = netlink_post(NETLINK_ARPD, skb);
	if (retval)
	{
		kfree_skb(skb, FREE_WRITE);
		if (retval == -EUNATCH)
			arpd_not_running = 1;
	}
}

/*
 *	Send ARPD update message.
 */

static __inline__ void arpd_update(struct arp_table * entry)
{
	if (arpd_not_running)
		return;
	arpd_send(ARPD_UPDATE, entry->ip, entry->dev, entry->ha,
		  entry->last_updated);
}

/*
 *	Send ARPD lookup request.
 */

static __inline__ void arpd_lookup(u32 addr, struct device * dev)
{
	if (arpd_not_running)
		return;
	arpd_send(ARPD_LOOKUP, addr, dev, NULL, 0);
}

/*
 *	Send ARPD flush message.
 */

static __inline__ void arpd_flush(struct device * dev)
{
	if (arpd_not_running)
		return;
	arpd_send(ARPD_FLUSH, 0, dev, NULL, 0);
}


static int arpd_callback(struct sk_buff *skb)
{
	struct device * dev;
	struct arpd_request *retreq;

	arpd_not_running = 0;

	if (skb->len != sizeof(struct arpd_request))
	{
		kfree_skb(skb, FREE_READ);
		return -EINVAL;
	}

	retreq = (struct arpd_request *)skb->data;
	dev = (struct device*)retreq->dev;

	if (retreq->stamp != arpd_stamp || !dev)
	{
		kfree_skb(skb, FREE_READ);
		return -EINVAL;
	}

	if (!retreq->updated || empty(retreq->ha, sizeof(retreq->ha)))
	{
/*
 *	Invalid mapping: drop it and send ARP broadcast.
 */
		arp_send(ARPOP_REQUEST, ETH_P_ARP, retreq->ip, dev, dev->pa_addr, NULL, 
			 dev->dev_addr, NULL);
	}
	else
	{
		arp_fast_lock();
		arp_update(retreq->ip, retreq->ha, dev, retreq->updated, NULL, 0);
		arp_unlock();
	}

	kfree_skb(skb, FREE_READ);
	return sizeof(struct arpd_request);
}

#else

static __inline__ void arpd_update(struct arp_table * entry)
{
	return;
}

#endif /* CONFIG_ARPD */




/*
 *	ARP expiration routines.
 */

/*
 *	Force the expiry of an entry in the internal cache so the memory
 *	can be used for a new request.
 */

static int arp_force_expire(void)
{
	int i;
	struct arp_table *entry, **pentry;
	struct arp_table **oldest_entry = NULL;
	unsigned long oldest_used = ~0;
	unsigned long flags;
	unsigned long now = jiffies;
	int result = 0;

	static last_index;

	if (ARP_LOCKED())
		return 0;

	save_flags(flags);

	if (last_index >= ARP_TABLE_SIZE)
		last_index = 0;

	for (i = 0; i < ARP_TABLE_SIZE; i++, last_index++)
	{
		pentry = &arp_tables[last_index & (ARP_TABLE_SIZE-1)];

		while ((entry = *pentry) != NULL)
		{
			if (!(entry->flags & ATF_PERM))
			{
				int users;
				cli();
				users = arp_count_hhs(entry);

				if (!users && now - entry->last_used > sysctl_arp_timeout)
				{
					*pentry = entry->next;
					restore_flags(flags);
#if RT_CACHE_DEBUG >= 2
					printk("arp_force_expire: %08x expired\n", entry->ip);
#endif
					arp_free_entry(entry);
					result++;
					if (arp_size < ARP_MAXSIZE)
						goto done;
					continue;
				}
				restore_flags(flags);
				if (!users && entry->last_used < oldest_used)
				{
					oldest_entry = pentry;
					oldest_used = entry->last_used;
				}
			}
			pentry = &entry->next;
		}
	}

done:
	if (result || !oldest_entry)
		return result;

	entry = *oldest_entry;
	*oldest_entry = entry->next;
#if RT_CACHE_DEBUG >= 2
	printk("arp_force_expire: expiring %08x\n", entry->ip);
#endif
	arp_free_entry(entry);
	return 1;
}

/*
 *	Check if there are entries that are too old and remove them. If the
 *	ATF_PERM flag is set, they are always left in the arp cache (permanent
 *      entries). If an entry was not confirmed for ARP_CONFIRM_INTERVAL,
 *	send point-to-point ARP request.
 *	If it will not be confirmed for ARP_CONFIRM_TIMEOUT,
 *	give it to shred by arp_expire_entry.
 */

static void arp_check_expire(unsigned long dummy)
{
	int i;
	unsigned long now = jiffies;

	del_timer(&arp_timer);

#ifdef CONFIG_ARPD
	arpd_not_running = 0;
#endif

	ip_rt_check_expire();

	arp_fast_lock();

	if (!ARP_LOCKED())
	{

		for (i = 0; i < ARP_TABLE_SIZE; i++)
		{
			struct arp_table *entry, **pentry;
		
			pentry = &arp_tables[i];

			while ((entry = *pentry) != NULL)
			{
				if (entry->flags & ATF_PERM)
				{
					pentry = &entry->next;
					continue;
				}

				cli();
				if (now - entry->last_used > sysctl_arp_timeout
				    && !arp_count_hhs(entry))
				{
					*pentry = entry->next;
					sti();
#if RT_CACHE_DEBUG >= 2
					printk("arp_expire: %08x expired\n", entry->ip);
#endif
					arp_free_entry(entry);
					continue;
				}
				sti();
				if (entry->last_updated
				    && now - entry->last_updated > sysctl_arp_confirm_interval
				    && !(entry->flags & ATF_PERM))
				{
					struct device * dev = entry->dev;
					entry->retries = sysctl_arp_max_tries+sysctl_arp_max_pings;
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
				pentry = &entry->next;	/* go to next entry */
			}
		}
	}

	arp_unlock();

	/*
	 *	Set the timer again.
	 */

	arp_timer.expires = jiffies + sysctl_arp_check_interval;
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

	arp_fast_lock();

	save_flags(flags);
	cli();
	del_timer(&entry->timer);

	/*
	 *	If arp table is locked, defer expire processing.
	 */
	if (ARP_LOCKED())
	{
#if RT_CACHE_DEBUG >= 1
		printk(KERN_DEBUG "arp_expire_request: %08x deferred\n", entry->ip);
#endif
		entry->timer.expires = jiffies + HZ/10;
		add_timer(&entry->timer);
		restore_flags(flags);
		arp_unlock();
		return;
	}

	/*
	 *	Since all timeouts are handled with interrupts enabled, there is a
	 *	small chance, that this entry has just been resolved by an incoming
	 *	packet. This is the only race condition, but it is handled...
	 *
	 *	One exception: if entry is COMPLETE but old,
	 *	it means that point-to-point ARP ping has been failed
	 *	(It really occurs with Cisco 4000 routers)
	 *	We should reconfirm it.
	 */
	
	if ((entry->flags & ATF_COM) && entry->last_updated
	    && jiffies - entry->last_updated <= sysctl_arp_confirm_interval)
	{
		restore_flags(flags);
		arp_unlock();
		return;
	}

	restore_flags(flags);

	if (entry->last_updated && --entry->retries > 0)
	{
		struct device *dev = entry->dev;

#if RT_CACHE_DEBUG >= 2
		printk("arp_expire_request: %08x timed out\n", entry->ip);
#endif
		/* Set new timer. */
		entry->timer.expires = jiffies + sysctl_arp_res_time;
		add_timer(&entry->timer);
		arp_send(ARPOP_REQUEST, ETH_P_ARP, entry->ip, dev, dev->pa_addr,
			 entry->retries > sysctl_arp_max_tries ? entry->ha : NULL,
			 dev->dev_addr, NULL);
		arp_unlock();
		return;
	}

	/*
	 *	The host is really dead.
	 */

	arp_purge_send_q(entry);

	cli();
	if (arp_count_hhs(entry))
	{
		/*
		 *	The host is dead, but someone refers to it.
		 *	It is useless to drop this entry just now,
		 *	it will be born again, so that
		 *	we keep it, but slow down retransmitting
		 *	to ARP_DEAD_RES_TIME.
		 */

		struct device *dev = entry->dev;
#if RT_CACHE_DEBUG >= 2
		printk("arp_expire_request: %08x is dead\n", entry->ip);
#endif
		entry->retries = sysctl_arp_max_tries;
		entry->flags &= ~ATF_COM;
		arp_invalidate_hhs(entry);
		restore_flags(flags);

		/*
		 *	Declare the entry dead.
		 */
		entry->last_updated = 0;
		arpd_update(entry);

		entry->timer.expires = jiffies + sysctl_arp_dead_res_time;
		add_timer(&entry->timer);
		arp_send(ARPOP_REQUEST, ETH_P_ARP, entry->ip, dev, dev->pa_addr, 
			 NULL, dev->dev_addr, NULL);
		arp_unlock();
		return;
	}
	restore_flags(flags);

	entry->last_updated = 0;
	arpd_update(entry);

	hash = HASH(entry->ip);

	pentry = &arp_tables[hash];

	while (*pentry != NULL)
	{
		if (*pentry != entry)
		{
			pentry = &(*pentry)->next;
			continue;
		}
		*pentry = entry->next;
#if RT_CACHE_DEBUG >= 2
		printk("arp_expire_request: %08x is killed\n", entry->ip);
#endif
		arp_free_entry(entry);
	}
	arp_unlock();
}


/* 
 * Allocate memory for a new entry.  If we are at the maximum limit
 * of the internal ARP cache, arp_force_expire() an entry.  NOTE:  
 * arp_force_expire() needs the cache to be locked, so therefore
 * arp_alloc_entry() should only be called with the cache locked too!
 */

static struct arp_table * arp_alloc_entry(void)
{
	struct arp_table * entry;


	if (arp_size >= ARP_MAXSIZE)
		arp_force_expire();

	entry = (struct arp_table *)
		kmalloc(sizeof(struct arp_table),GFP_ATOMIC);

	if (entry != NULL)
	{
		atomic_inc(&arp_size);
		memset(entry, 0, sizeof(struct arp_table));

                entry->mask = DEF_ARP_NETMASK;
		init_timer(&entry->timer);
		entry->timer.function = arp_expire_request;
		entry->timer.data = (unsigned long)entry;
		entry->last_updated = entry->last_used = jiffies;
		skb_queue_head_init(&entry->skb);
	}
	return entry;
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

#ifdef  CONFIG_ARPD
	arpd_flush(dev);
	arpd_stamp++;
#endif

	arp_fast_lock();
#if RT_CACHE_DEBUG >= 1	 
	if (ARP_LOCKED())
		printk("arp_device_event: impossible\n");
#endif

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
		printk(KERN_ERR "arp_send_q: incomplete entry for %s\n",
				in_ntoa(entry->ip));
		/* Can't flush the skb, because RFC1122 says to hang on to */
		/* at least one from any unresolved entry.  --MS */
		/* What's happened is that someone has 'unresolved' the entry
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


static int
arp_update (u32 sip, char *sha, struct device * dev,
	    unsigned long updated, struct arp_table *ientry, int grat)
{
	struct arp_table * entry;
	unsigned long hash;
	int do_arpd = 0;

	if (updated == 0)
	{
		updated = jiffies;
		do_arpd = 1;
	}

	hash = HASH(sip);

	for (entry=arp_tables[hash]; entry; entry = entry->next)
		if (entry->ip == sip && entry->dev == dev)
			break;

	if (entry)
	{
/*
 *	Entry found; update it only if it is not a permanent entry.
 */
		if (!(entry->flags & ATF_PERM)) 
		{
			del_timer(&entry->timer);
			entry->last_updated = updated;
			if (memcmp(entry->ha, sha, dev->addr_len)!=0)
			{
				memcpy(entry->ha, sha, dev->addr_len);
				if (entry->flags & ATF_COM)
					arp_update_hhs(entry);
			}
			if (do_arpd)
				arpd_update(entry);
		}

		if (!(entry->flags & ATF_COM))
		{
/*
 *	This entry was incomplete.  Delete the retransmit timer
 *	and switch to complete status.
 */
			entry->flags |= ATF_COM;
			arp_update_hhs(entry);
/* 
 *	Send out waiting packets. We might have problems, if someone is 
 *	manually removing entries right now -- entry might become invalid 
 *	underneath us.
 */
			arp_send_q(entry);
		}
		return 1;
	}

/*
 * 	No entry found.  Need to add a new entry to the arp table.
 */
	entry = ientry;

	if (grat && !entry)
		return 0;

	if (!entry)
	{
		entry = arp_alloc_entry();
		if (!entry)
			return 0;

		entry->ip = sip;
		entry->flags = ATF_COM;
		memcpy(entry->ha, sha, dev->addr_len);
		entry->dev = dev;
	}

	entry->last_updated = updated;
	entry->last_used = jiffies;
	if (do_arpd)
		arpd_update(entry);

	if (!ARP_LOCKED())
	{
		entry->next = arp_tables[hash];
		arp_tables[hash] = entry;
		return 0;
	}
#if RT_CACHE_DEBUG >= 2
	printk("arp_update: %08x backlogged\n", entry->ip);
#endif
	arp_enqueue(&arp_backlog, entry);
	arp_bh_mask |= ARP_BH_BACKLOG;
	return 0;
}



static __inline__ struct arp_table *arp_lookup(u32 paddr, struct device * dev)
{
	struct arp_table *entry;

	for (entry = arp_tables[HASH(paddr)]; entry != NULL; entry = entry->next)
		if (entry->ip == paddr && (!dev || entry->dev == dev))
			return entry;
	return NULL;
}

/*
 *	Find an arp mapping in the cache. If not found, return false.
 */

int arp_query(unsigned char *haddr, u32 paddr, struct device * dev)
{
	struct arp_table *entry;

	arp_fast_lock();

	entry = arp_lookup(paddr, dev);

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


static int arp_set_predefined(int addr_hint, unsigned char * haddr, u32 paddr, struct device * dev)
{
	switch (addr_hint)
	{
		case IS_MYADDR:
			printk(KERN_DEBUG "ARP: arp called for own IP address\n");
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
 *	Create a new unresolved entry.
 */

struct arp_table * arp_new_entry(u32 paddr, struct device *dev, struct hh_cache *hh, struct sk_buff *skb)
{
	struct arp_table *entry;

	entry = arp_alloc_entry();

	if (entry != NULL)
	{
		entry->ip = paddr;
		entry->dev = dev;
		if (hh)
		{
			entry->hh = hh;
			atomic_inc(&hh->hh_refcnt);
			hh->hh_arp = (void*)entry;
		}
		entry->timer.expires = jiffies + sysctl_arp_res_time;

		if (skb != NULL)
		{
			skb_queue_tail(&entry->skb, skb);
			skb_device_unlock(skb);
		}

		if (!ARP_LOCKED())
		{
			unsigned long hash = HASH(paddr);
			entry->next = arp_tables[hash];
			arp_tables[hash] = entry;
			add_timer(&entry->timer);
			entry->retries = sysctl_arp_max_tries;
#ifdef CONFIG_ARPD
			if (!arpd_not_running)
				arpd_lookup(paddr, dev);
			else
#endif
				arp_send(ARPOP_REQUEST, ETH_P_ARP, paddr, dev, dev->pa_addr, NULL, 
					 dev->dev_addr, NULL);
		}
		else
		{
#if RT_CACHE_DEBUG >= 2
			printk("arp_new_entry: %08x backlogged\n", entry->ip);
#endif
			arp_enqueue(&arp_req_backlog, entry);
			arp_bh_mask |= ARP_BH_BACKLOG;
		}
	}
	return entry;
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
	entry = arp_lookup(paddr, dev);

	if (entry != NULL) 	/* It exists */
	{
		if (entry->flags & ATF_COM)
		{
			entry->last_used = jiffies;
			memcpy(haddr, entry->ha, dev->addr_len);
			if (skb)
				skb->arp = 1;
			arp_unlock();
			return 0;
		}

		/*
		 *	A request was already sent, but no reply yet. Thus
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
				icmp_send(skb, ICMP_DEST_UNREACH, ICMP_HOST_UNREACH, 0, dev);
				dev_kfree_skb(skb, FREE_WRITE);
			}
		}
		arp_unlock();
		return 1;
	}

	entry = arp_new_entry(paddr, dev, NULL, skb);

	if (skb != NULL && !entry)
		dev_kfree_skb(skb, FREE_WRITE);

	arp_unlock();
	return 1;
}

/*
 *	Binding hardware header cache entry.
 *	It is the only really complicated part of arp code.
 *	We have no locking for hh records, so that
 *	all possible race conditions should be resolved by
 *	cli()/sti() pairs.
 *
 *	Important note: hhs never disappear from lists, if ARP_LOCKED,
 *	this fact allows to scan hh lists with enabled interrupts,
 *	but results in generating duplicate hh entries.
 *	It is harmless. (and I've never seen such event)
 *
 *	Returns 0, if hh has been just created, so that
 *	caller should fill it.
 */

int arp_bind_cache(struct hh_cache ** hhp, struct device *dev, unsigned short htype, u32 paddr)
{
	struct arp_table *entry;
	struct hh_cache *hh;
	int addr_hint;
	unsigned long flags;

	save_flags(flags);

	if ((addr_hint = ip_chk_addr(paddr)) != 0)
	{
		unsigned char haddr[MAX_ADDR_LEN];
		if (*hhp)
			return 1;
		hh = arp_alloc_hh(htype);
		if (!hh)
			return 1;
		arp_set_predefined(addr_hint, haddr, paddr, dev);
		dev->header_cache_update(hh, dev, haddr);
		return arp_set_hh(hhp, hh);
	}

	arp_fast_lock();

	entry = arp_lookup(paddr, dev);

	if (entry)
	{
		for (hh = entry->hh; hh; hh=hh->hh_next)
			if (hh->hh_type == htype)
				break;

		if (hh)
		{
			arp_set_hh(hhp, hh);
			arp_unlock();
			return 1;
		}
	}

	hh = arp_alloc_hh(htype);
	if (!hh)
	{
		arp_unlock();
		return 1;
	}

	if (entry)
	{

		cli();
		hh->hh_arp = (void*)entry;
		hh->hh_next = entry->hh;
		entry->hh = hh;
		atomic_inc(&hh->hh_refcnt);
		restore_flags(flags);

		if (entry->flags & ATF_COM)
			dev->header_cache_update(hh, dev, entry->ha);

		if (arp_set_hh(hhp, hh))
		{
			arp_unlock();
			return 0;
		}

		entry->last_used = jiffies;
		arp_unlock();
		return 0;
	}

	entry = arp_new_entry(paddr, dev, hh, NULL);
	if (entry == NULL)
	{
		kfree_s(hh, sizeof(struct hh_cache));
		arp_unlock();
		return 1;
	}

	if (!arp_set_hh(hhp, hh))
	{
		arp_unlock();
		return 0;
	}
	arp_unlock();
	return 1;
}

static void arp_run_bh()
{
	unsigned long flags;
	struct arp_table *entry, *entry1;
	struct device  * dev;
	unsigned long hash;
	struct hh_cache *hh;
	u32 sip;

	save_flags(flags);
	cli();
	arp_fast_lock();

	while (arp_bh_mask)
	{
		arp_bh_mask  &= ~ARP_BH_BACKLOG;

		while ((entry = arp_dequeue(&arp_backlog)) != NULL)
		{
			restore_flags(flags);
			if (arp_update(entry->ip, entry->ha, entry->dev, 0, entry, 0))
				arp_free_entry(entry);
			cli();
		}

		cli();
		while ((entry = arp_dequeue(&arp_req_backlog)) != NULL)
		{
			restore_flags(flags);

			dev = entry->dev;
			sip = entry->ip;
			hash = HASH(sip);

			for (entry1 = arp_tables[hash]; entry1; entry1 = entry1->next)
				if (entry1->ip == sip && entry1->dev == dev)
					break;

			if (!entry1)
			{
				cli();
				entry->next = arp_tables[hash];
				arp_tables[hash] = entry;
				restore_flags(flags);
				entry->timer.expires = jiffies + sysctl_arp_res_time;
				entry->retries = sysctl_arp_max_tries;
				entry->last_used = jiffies;
				if (!(entry->flags & ATF_COM))
				{
					add_timer(&entry->timer);
#ifdef CONFIG_ARPD
					if (!arpd_not_running)
						arpd_lookup(sip, dev);
					else
#endif
						arp_send(ARPOP_REQUEST, ETH_P_ARP, sip, dev, dev->pa_addr, NULL, dev->dev_addr, NULL);
				}
#if RT_CACHE_DEBUG >= 1
				printk(KERN_DEBUG "arp_run_bh: %08x reinstalled\n", sip);
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
					restore_flags(flags);
					skb_queue_tail(&entry1->skb, skb);
					skb_device_unlock(skb);
					cli();
				}
				restore_flags(flags);
				
				arp_free_entry(entry);

				if (entry1->flags & ATF_COM)
				{
					arp_update_hhs(entry1);
					arp_send_q(entry1);
				}
			}
			cli();
		}
		cli();
	}
	arp_unlock();
	restore_flags(flags);
}


/*
 *	Interface to link layer: send routine and receive handler.
 */

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
		printk(KERN_DEBUG "ARP: no memory to send an arp packet\n");
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
 *	Receive an arp request by the device layer.
 */

int arp_rcv(struct sk_buff *skb, struct device *dev, struct packet_type *pt)
{
/*
 *	We shouldn't use this type conversion. Check later.
 */
	
	struct arphdr *arp = (struct arphdr *)skb->h.raw;
	unsigned char *arp_ptr= (unsigned char *)(arp+1);
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
		case ARPHRD_METRICOM:
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
			printk(KERN_ERR "ARP: dev->type mangled!\n");
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
			struct arp_table *proxy_entry;

/*
 * 	To get in here, it is a request for someone else.  We need to
 * 	check if that someone else is one of our proxies.  If it isn't,
 * 	we can toss it.
 *
 *	Make "longest match" lookup, a la routing.
 */

			arp_fast_lock();

			for (proxy_entry = arp_proxy_list; proxy_entry;
			     proxy_entry = proxy_entry->next)
			{
				if (proxy_entry->dev == dev &&
				    !((proxy_entry->ip^tip)&proxy_entry->mask))
					break;
			}

			if (proxy_entry && (proxy_entry->mask || ((dev->pa_addr^tip)&dev->pa_mask)))
			{
				char ha[MAX_ADDR_LEN];
				struct rtable * rt;

				/* Unlock arp tables to make life for
				 * ip_rt_route easy. Note, that we are obliged
				 * to make local copy of hardware address.
				 */

				memcpy(ha, proxy_entry->ha, dev->addr_len);
				arp_unlock();

				rt = ip_rt_route(tip, 0);
				if (rt  && rt->rt_dev != dev)
					arp_send(ARPOP_REPLY,ETH_P_ARP,sip,dev,tip,sha,ha,sha);
				ip_rt_put(rt);

			}
			else
				arp_unlock();
		}
		else
			arp_send(ARPOP_REPLY,ETH_P_ARP,sip,dev,tip,sha,dev->dev_addr,sha);

/*
 *	Handle gratuitous arp.
 */
		arp_fast_lock();
		arp_update(sip, sha, dev, 0, NULL, 1);
		arp_unlock();
		kfree_skb(skb, FREE_READ);
		return 0;
	}

	arp_fast_lock();
	arp_update(sip, sha, dev, 0, NULL, ip_chk_addr(tip) != IS_MYADDR);
	arp_unlock();
	kfree_skb(skb, FREE_READ);
	return 0;
}



/*
 *	User level interface (ioctl, /proc)
 */

/*
 *	Set (create) an ARP cache entry.
 */

static int arp_req_set(struct arpreq *r, struct device * dev)
{
	struct arp_table *entry, **entryp;
	struct sockaddr_in *si;
	unsigned char *ha;
	u32 ip;
	u32 mask = DEF_ARP_NETMASK;
	unsigned long flags;

	/*
	 *	Extract netmask (if supplied).
	 */

	if (r->arp_flags&ATF_NETMASK)
	{
		si = (struct sockaddr_in *) &r->arp_netmask;
		mask = si->sin_addr.s_addr;
	}

	/*
	 *	Extract destination.
	 */
	
	si = (struct sockaddr_in *) &r->arp_pa;
	ip = si->sin_addr.s_addr;


	if (r->arp_flags&ATF_PUBL)
	{
		if (!mask && ip)
			return -EINVAL;
		if (!dev)
			dev = dev_getbytype(r->arp_ha.sa_family);
	}
	else
	{
		if (ip_chk_addr(ip))
			return -EINVAL;
		if (!dev)
		{
			struct rtable * rt;
			rt = ip_rt_route(ip, 0);
			if (!rt)
				return -ENETUNREACH;
			dev = rt->rt_dev;
			ip_rt_put(rt);
		}
	}
	if (!dev || (dev->flags&(IFF_LOOPBACK|IFF_NOARP)))
		return -ENODEV;

	if (r->arp_ha.sa_family != dev->type)	
		return -EINVAL;
		
	arp_fast_lock();
#if RT_CACHE_DEBUG >= 1
	if (ARP_LOCKED())
		printk("arp_req_set: bug\n");
#endif

	if (!(r->arp_flags & ATF_PUBL))
		entryp = &arp_tables[HASH(ip)];
	else
		entryp = &arp_proxy_list;

	while ((entry = *entryp) != NULL)
	{
		/* User supplied arp entries are definitive - RHP 960603 */

		if (entry->ip == ip && entry->mask == mask && entry->dev == dev) {
			*entryp=entry->next;
			arp_free_entry(entry);
			continue;
		}
		if ((entry->mask & mask) != mask)
			break;
		entryp = &entry->next;
	}

	entry = arp_alloc_entry();
	if (entry == NULL)
	{
		arp_unlock();
		return -ENOMEM;
	}
	entry->ip = ip;
	entry->dev = dev;
	entry->mask = mask;
	entry->flags = r->arp_flags;

	entry->next = *entryp;
	*entryp = entry;

	ha = r->arp_ha.sa_data;
	if (empty(ha, dev->addr_len))
		ha = dev->dev_addr;

	save_flags(flags);
	cli();
	memcpy(entry->ha, ha, dev->addr_len);
	entry->last_updated = entry->last_used = jiffies;
	entry->flags |= ATF_COM;
	restore_flags(flags);
	arpd_update(entry);
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
	u32 mask = DEF_ARP_NETMASK;

	if (r->arp_flags&ATF_NETMASK)
	{
		si = (struct sockaddr_in *) &r->arp_netmask;
		mask = si->sin_addr.s_addr;
	}

	si = (struct sockaddr_in *) &r->arp_pa;

	arp_fast_lock();
#if RT_CACHE_DEBUG >= 1
	if (ARP_LOCKED())
		printk("arp_req_set: impossible\n");
#endif

	if (!(r->arp_flags & ATF_PUBL))
		entry = arp_tables[HASH(si->sin_addr.s_addr)];
	else
		entry = arp_proxy_list;

	for ( ; entry ;entry = entry->next)
	{
		if (entry->ip == si->sin_addr.s_addr 
		    && (!dev || entry->dev == dev)
		    && (!(r->arp_flags&ATF_NETMASK) || entry->mask == mask))
		{
			memcpy(r->arp_ha.sa_data, entry->ha, entry->dev->addr_len);
			r->arp_ha.sa_family = entry->dev->type;
			r->arp_flags = entry->flags;
			strncpy(r->arp_dev, entry->dev->name, sizeof(r->arp_dev));
			arp_unlock();
			return 0;
		}
	}

	arp_unlock();
	return -ENXIO;
}

static int arp_req_delete(struct arpreq *r, struct device * dev)
{
	struct sockaddr_in	*si;
	struct arp_table	*entry, **entryp;
	int	retval = -ENXIO;
	u32	mask = DEF_ARP_NETMASK;

	if (r->arp_flags&ATF_NETMASK)
	{
		si = (struct sockaddr_in *) &r->arp_netmask;
		mask = si->sin_addr.s_addr;
	}

	si = (struct sockaddr_in *) &r->arp_pa;

	arp_fast_lock();
#if RT_CACHE_DEBUG >= 1
	if (ARP_LOCKED())
		printk("arp_req_delete: impossible\n");
#endif

	if (!(r->arp_flags & ATF_PUBL))
		entryp = &arp_tables[HASH(si->sin_addr.s_addr)];
	else
		entryp = &arp_proxy_list;

	while ((entry = *entryp) != NULL)
	{
		if (entry->ip == si->sin_addr.s_addr 
		    && (!dev || entry->dev == dev)
		    && (!(r->arp_flags&ATF_NETMASK) || entry->mask == mask))
		{
			*entryp = entry->next;
			arp_free_entry(entry);
			retval = 0;
			continue;
		}
		entryp = &entry->next;
	}

	arp_unlock();
	return retval;
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

	if (!(r.arp_flags & ATF_PUBL))
		r.arp_flags &= ~ATF_NETMASK;
	if (!(r.arp_flags & ATF_NETMASK))
		((struct sockaddr_in *)&r.arp_netmask)->sin_addr.s_addr=DEF_ARP_NETMASK;

	if (r.arp_dev[0])
	{
		if ((dev = dev_get(r.arp_dev)) == NULL)
			return -ENODEV;

		if (!r.arp_ha.sa_family)
			r.arp_ha.sa_family = dev->type;
		else if (r.arp_ha.sa_family != dev->type)
			return -EINVAL;
	}

	switch(cmd)
	{
		case SIOCDARP:
		        return arp_req_delete(&r, dev);
		case SIOCSARP:
			return arp_req_set(&r, dev);
		case OLD_SIOCDARP:
			/* old  SIOCDARP destroys both
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
				 "     %-17s %s\t%d\t%1d\n",
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

#ifdef CONFIG_ARPD
	netlink_attach(NETLINK_ARPD, arpd_callback);
#endif
}
