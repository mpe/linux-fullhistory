/* linux/net/inet/arp.c
 *
 * Copyright (C) 1994 by Florian  La Roche
 *
 * This module implements the Address Resolution Protocol ARP (RFC 826),
 * which is used to convert IP addresses (or in the future maybe other
 * high-level addresses) into a low-level hardware address (like an Ethernet
 * address).
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
 *		Stuart Cheshire	:	Metricom and grat arp fixes
 *					*** FOR 2.1 clean this up ***
 *		Lawrence V. Stefani: (08/12/96) Added FDDI support.
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
#include <linux/fddidevice.h>
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
#if defined(CONFIG_AX25) || defined(CONFIG_AX25_MODULE)
#include <net/ax25.h>
#if defined(CONFIG_NETROM) || defined(CONFIG_NETROM_MODULE)
#include <net/netrom.h>
#endif
#endif
#include <linux/net_alias.h>
#ifdef CONFIG_ARPD
#include <net/netlink.h>
#endif

#include <asm/system.h>
#include <asm/uaccess.h>

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
 *	Limit on unresolved ARP cache entries.
 */
#define ARP_MAX_UNRES (ARP_MAXSIZE/2)

/*
 *	Maximal number of skb's queued for resolution.
 */
#define ARP_MAX_UNRES_PACKETS 3

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
	union {
		struct dst_entry	dst;
		struct arp_table	*next;
	} u;
	unsigned long			last_updated;		/* For expiry 			*/
	unsigned int			flags;			/* Control status 		*/
	u32				ip;
	u32				mask;			/* netmask - used for generalised proxy arps (tridge) 		*/
	int				hatype;
	unsigned char			ha[MAX_ADDR_LEN];	/* Hardware address		*/

	/*
	 *	The following entries are only used for unresolved hw addresses.
	 */
	struct timer_list		timer;			/* expire timer 		*/
	int				retries;		/* remaining retries	 	*/
	struct sk_buff_head		skb;			/* list of queued packets 	*/
};

#if RT_CACHE_DEBUG >= 1
#define ASSERT_BH() if (!intr_count) printk(KERN_CRIT __FUNCTION__ " called from SPL=0\n");
#else
#define ASSERT_BH()
#endif

/*
 *	Interface to generic destionation cache.
 */

static void arp_dst_destroy(struct dst_entry * dst);
static struct dst_entry * arp_dst_check(struct dst_entry * dst)
{
	return dst;
}

static struct dst_entry * arp_dst_reroute(struct dst_entry * dst)
{
	return dst;
}


struct dst_ops arp_dst_ops =
{
	AF_UNSPEC,
	arp_dst_check,
	arp_dst_reroute,
	arp_dst_destroy
};


static atomic_t arp_size = 0;
static atomic_t arp_unres_size = 0;

#ifdef CONFIG_ARPD
static int arpd_not_running;
static int arpd_stamp;
#endif

static void arp_check_expire (unsigned long);  
static int  arp_update (u32 sip, char *sha, struct device * dev,
			unsigned long updated, int grat);

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
 *	Hardware header cache.
 *
 */

/*
 * Signal to device layer, that hardware address may be changed.
 */

static __inline__ void arp_update_hhs(struct arp_table * entry)
{
	struct hh_cache *hh;
	void (*update)(struct hh_cache*, struct device*, unsigned char*) =
		entry->u.dst.dev->header_cache_update;

#if RT_CACHE_DEBUG >= 1
	if (!update && entry->u.dst.hh)
	{
		printk(KERN_DEBUG "arp_update_hhs: no update callback for %s\n", entry->u.dst.dev->name);
		return;
	}
#endif
	for (hh=entry->u.dst.hh; hh; hh=hh->hh_next)
		update(hh, entry->u.dst.dev, entry->ha);
}

/*
 *	Invalidate all hh's, so that higher level will not try to use it.
 */

static __inline__ void arp_invalidate_hhs(struct arp_table * entry)
{
	struct hh_cache *hh;

	for (hh=entry->u.dst.hh; hh; hh=hh->hh_next)
		hh->hh_uptodate = 0;
}

/*
 * Purge all linked skb's of the entry.
 */

static void arp_purge_send_q(struct arp_table *entry)
{
	struct sk_buff *skb;

	ASSERT_BH();

	/* Release the list of `skb' pointers. */
	while ((skb = skb_dequeue(&entry->skb)) != NULL)
		kfree_skb(skb, FREE_WRITE);
	return;
}

static void __inline__ arp_free(struct arp_table **entryp)
{
	struct arp_table *entry = *entryp;
	*entryp = entry->u.next;

	ASSERT_BH();

	if (!(entry->flags&ATF_PUBL)) {
		atomic_dec(&arp_size);
		if (!(entry->flags&ATF_COM))
			atomic_dec(&arp_unres_size);
	}
	del_timer(&entry->timer);
	arp_purge_send_q(entry);
	arp_invalidate_hhs(entry);

	dst_free(&entry->u.dst);
}


static void arp_dst_destroy(struct dst_entry * dst)
{
	struct arp_table *entry = (struct arp_table*)dst;
	struct hh_cache *hh, *next;

	ASSERT_BH();

	del_timer(&entry->timer);
	arp_purge_send_q(entry);

	hh = entry->u.dst.hh;
	entry->u.dst.hh = NULL;

	for ( ; hh; hh = next)
	{
		next = hh->hh_next;
		hh->hh_uptodate = 0;
		hh->hh_next = NULL;
		if (atomic_dec_and_test(&hh->hh_refcnt))
		{
#if RT_CACHE_DEBUG >= 2
			extern atomic_t hh_count;
			atomic_dec(&hh_count);
#endif
			kfree_s(hh, sizeof(struct(struct hh_cache)));
		}
	}
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

static __inline__ void arpd_update(u32 ip, struct device *dev, char *ha)
{
	if (arpd_not_running)
		return;
	arpd_send(ARPD_UPDATE, ip, dev, ha, jiffies);
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


static int arpd_callback(int minor, struct sk_buff *skb)
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

	if (!retreq->updated)
	{
/*
 *	Invalid mapping: drop it and send ARP broadcast.
 */
		arp_send(ARPOP_REQUEST, ETH_P_ARP, retreq->ip, dev, dev->pa_addr, NULL, 
			 dev->dev_addr, NULL);
	}
	else
	{
		start_bh_atomic();
		arp_update(retreq->ip, retreq->ha, dev, retreq->updated, 0);
		end_bh_atomic();
	}

	kfree_skb(skb, FREE_READ);
	return sizeof(struct arpd_request);
}

#else

static __inline__ void arpd_update(u32 ip, struct device *dev, char *ha)
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
	unsigned long now = jiffies;
	int result = 0;

	static int last_index;

	if (last_index >= ARP_TABLE_SIZE)
		last_index = 0;

	for (i = 0; i < ARP_TABLE_SIZE; i++, last_index++)
	{
		pentry = &arp_tables[last_index & (ARP_TABLE_SIZE-1)];

		while ((entry = *pentry) != NULL)
		{
			if (!(entry->flags & ATF_PERM))
			{
				if (!entry->u.dst.refcnt &&
				    now - entry->u.dst.lastuse > sysctl_arp_timeout)
				{
#if RT_CACHE_DEBUG >= 2
					printk("arp_force_expire: %08x expired\n", entry->ip);
#endif
					arp_free(pentry);
					result++;
					if (arp_size < ARP_MAXSIZE)
						goto done;
					continue;
				}
				if (!entry->u.dst.refcnt &&
				    entry->u.dst.lastuse < oldest_used)
				{
					oldest_entry = pentry;
					oldest_used = entry->u.dst.lastuse;
				}
			}
			pentry = &entry->u.next;
		}
	}

done:
	if (result || !oldest_entry)
		return result;

#if RT_CACHE_DEBUG >= 2
	printk("arp_force_expire: expiring %08x\n", (*oldest_entry)->ip);
#endif
	arp_free(oldest_entry);
	return 1;
}

static void arp_unres_expire(void)
{
	int i;
	struct arp_table *entry, **pentry;
	unsigned long now = jiffies;

	for (i = 0; i < ARP_TABLE_SIZE; i++) {
		pentry = &arp_tables[i & (ARP_TABLE_SIZE-1)];

		while ((entry = *pentry) != NULL) {
			if (!(entry->flags & (ATF_PERM|ATF_COM)) &&
			    (entry->retries < sysctl_arp_max_tries ||
			     entry->timer.expires - now <
			     sysctl_arp_res_time - sysctl_arp_res_time/32)) {
				if (!entry->u.dst.refcnt) {
#if RT_CACHE_DEBUG >= 2
					printk("arp_unres_expire: %08x discarded\n", entry->ip);
#endif
					arp_free(pentry);
					continue;
				}
				arp_purge_send_q(entry);
			}
			pentry = &entry->u.next;
		}
	}
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

	for (i = 0; i < ARP_TABLE_SIZE; i++)
	{
		struct arp_table *entry, **pentry;
		
		pentry = &arp_tables[i];

		while ((entry = *pentry) != NULL)
		{
			if (entry->flags & ATF_PERM)
			{
				pentry = &entry->u.next;
				continue;
			}

			if (!entry->u.dst.refcnt &&
			    now - entry->u.dst.lastuse > sysctl_arp_timeout)
			{
#if RT_CACHE_DEBUG >= 2
				printk("arp_expire: %08x expired\n", entry->ip);
#endif
				arp_free(pentry);
				continue;
			}
			if (entry->last_updated &&
			    now - entry->last_updated > sysctl_arp_confirm_interval)
			{
				struct device * dev = entry->u.dst.dev;
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
			pentry = &entry->u.next;	/* go to next entry */
		}
	}

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

	del_timer(&entry->timer);

	/*	If entry is COMPLETE but old,
	 *	it means that point-to-point ARP ping has been failed
	 *	(It really occurs with Cisco 4000 routers)
	 *	We should reconfirm it.
	 */
	
	if ((entry->flags & ATF_COM) && entry->last_updated
	    && jiffies - entry->last_updated <= sysctl_arp_confirm_interval)
		return;

	if (entry->last_updated && --entry->retries > 0)
	{
		struct device *dev = entry->u.dst.dev;

#if RT_CACHE_DEBUG >= 2
		printk("arp_expire_request: %08x timed out\n", entry->ip);
#endif
		/* Set new timer. */
		entry->timer.expires = jiffies + sysctl_arp_res_time;
		add_timer(&entry->timer);
		arp_send(ARPOP_REQUEST, ETH_P_ARP, entry->ip, dev, dev->pa_addr,
			 entry->retries > sysctl_arp_max_tries ? entry->ha : NULL,
			 dev->dev_addr, NULL);
		return;
	}

	/*
	 *	The host is really dead.
	 */

	arp_purge_send_q(entry);

	if (entry->u.dst.refcnt)
	{
		/*
		 *	The host is dead, but someone refers to it.
		 *	It is useless to drop this entry just now,
		 *	it will be born again, so that
		 *	we keep it, but slow down retransmitting
		 *	to ARP_DEAD_RES_TIME.
		 */

		struct device *dev = entry->u.dst.dev;
#if RT_CACHE_DEBUG >= 2
		printk("arp_expire_request: %08x is dead\n", entry->ip);
#endif
		entry->retries = sysctl_arp_max_tries;
		if (entry->flags&ATF_COM)
			atomic_inc(&arp_unres_size);
		entry->flags &= ~ATF_COM;
		arp_invalidate_hhs(entry);

		/*
		 *	Declare the entry dead.
		 */
		entry->last_updated = 0;

		entry->timer.expires = jiffies + sysctl_arp_dead_res_time;
		add_timer(&entry->timer);
		arp_send(ARPOP_REQUEST, ETH_P_ARP, entry->ip, dev, dev->pa_addr, 
			 NULL, dev->dev_addr, NULL);
		return;
	}

	entry->last_updated = 0;

	hash = HASH(entry->ip);

	pentry = &arp_tables[hash];

	while (*pentry != NULL)
	{
		if (*pentry != entry)
		{
			pentry = &(*pentry)->u.next;
			continue;
		}
#if RT_CACHE_DEBUG >= 2
		printk("arp_expire_request: %08x is killed\n", entry->ip);
#endif
		arp_free(pentry);
	}
}


/* 
 * Allocate memory for a new entry.  If we are at the maximum limit
 * of the internal ARP cache, arp_force_expire() an entry.
 */

static struct arp_table * arp_alloc(int how)
{
	struct arp_table * entry;

	if (how && arp_size >= ARP_MAXSIZE)
		arp_force_expire();
	if (how > 1 && arp_unres_size >= ARP_MAX_UNRES) {
		arp_unres_expire();
		if (arp_unres_size >= ARP_MAX_UNRES) {
			printk("arp_unres_size=%d\n", arp_unres_size);
			return NULL;
		}
	}

	entry = (struct arp_table *)dst_alloc(sizeof(struct arp_table), &arp_dst_ops);

	if (entry != NULL)
	{
		if (how)
			atomic_inc(&arp_size);

                entry->mask = DEF_ARP_NETMASK;
		init_timer(&entry->timer);
		entry->timer.function = arp_expire_request;
		entry->timer.data = (unsigned long)entry;
		entry->last_updated = jiffies;
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

	for (i = 0; i < FULL_ARP_TABLE_SIZE; i++)
	{
		struct arp_table *entry;
		struct arp_table **pentry = &arp_tables[i];

		start_bh_atomic();

		while ((entry = *pentry) != NULL)
		{
			if (entry->u.dst.dev != dev)
			{
				pentry = &entry->u.next;
				continue;
			}
			arp_free(pentry);
		}

		end_bh_atomic();
	}
	return NOTIFY_DONE;
}



/*
 *	This will try to retransmit everything on the queue.
 */

static void arp_send_q(struct arp_table *entry)
{
	struct sk_buff *skb;

	ASSERT_BH();

	while((skb = skb_dequeue(&entry->skb)) != NULL)	{
		dev_queue_xmit(skb);
	}
}


static int
arp_update (u32 sip, char *sha, struct device * dev,
	    unsigned long updated, int grat)
{
	struct arp_table * entry;
	unsigned long hash;

	if (updated == 0)
	{
		updated = jiffies;
		arpd_update(sip, dev, sha);
	}

	hash = HASH(sip);

	for (entry=arp_tables[hash]; entry; entry = entry->u.next)
		if (entry->ip == sip && entry->u.dst.dev == dev)
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
			if (memcmp(entry->ha, sha, dev->addr_len) != 0)
			{
				memcpy(entry->ha, sha, dev->addr_len);
				if (entry->flags & ATF_COM)
					arp_update_hhs(entry);
			}
		}

		if (!(entry->flags & ATF_COM))
		{
/*
 *	Switch to complete status.
 */
			entry->flags |= ATF_COM;
			atomic_dec(&arp_unres_size);
			arp_update_hhs(entry);
/* 
 *	Send out waiting packets.
 */
			arp_send_q(entry);
		}
		return 1;
	}

/*
 * 	No entry found.  Need to add a new entry to the arp table.
 */
	if (grat)
		return 0;

	entry = arp_alloc(1);
	if (!entry)
		return 0;

	entry->ip = sip;
	entry->flags = ATF_COM;
	memcpy(entry->ha, sha, dev->addr_len);
	entry->u.dst.dev = dev;
	entry->hatype = dev->type;
	entry->last_updated = updated;

	entry->u.next = arp_tables[hash];
	arp_tables[hash] = entry;
	dst_release(&entry->u.dst);
	return 0;
}



static __inline__ struct arp_table *arp_lookup(u32 paddr, struct device * dev)
{
	struct arp_table *entry;

	for (entry = arp_tables[HASH(paddr)]; entry != NULL; entry = entry->u.next)
		if (entry->ip == paddr && entry->u.dst.dev == dev)
			return entry;
	return NULL;
}

static int arp_set_predefined(int addr_hint, unsigned char * haddr, u32 paddr, struct device * dev)
{
	switch (addr_hint)
	{
		case IS_MYADDR:
			printk(KERN_DEBUG "ARP: arp called for own IP address\n");
			memcpy(haddr, dev->dev_addr, dev->addr_len);
			return 1;
		case IS_MULTICAST:
			if(dev->type==ARPHRD_ETHER || dev->type==ARPHRD_IEEE802 
			   || dev->type==ARPHRD_FDDI)
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
		
		case IS_BROADCAST:
			memcpy(haddr, dev->broadcast, dev->addr_len);
			return 1;
	}
	return 0;
}


static void arp_start_resolution(struct arp_table *entry)
{
	struct device * dev = entry->u.dst.dev;

	del_timer(&entry->timer);
	entry->timer.expires = jiffies + sysctl_arp_res_time;
	entry->retries = sysctl_arp_max_tries;
	add_timer(&entry->timer);
#ifdef CONFIG_ARPD
	if (!arpd_not_running)
		arpd_lookup(entry->ip, dev);
	else
#endif
		arp_send(ARPOP_REQUEST, ETH_P_ARP, entry->ip, dev,
			 dev->pa_addr, NULL, dev->dev_addr, NULL);
}

/*
 *	Create a new unresolved entry.
 */

struct arp_table * arp_new_entry(u32 paddr, struct device *dev, struct sk_buff *skb)
{
	struct arp_table *entry;
	unsigned long hash = HASH(paddr);

	entry = arp_alloc(2);

	if (entry != NULL)
	{
		entry->ip = paddr;
		entry->u.dst.dev = dev;
		entry->hatype = dev->type;

		if (skb != NULL)
			skb_queue_tail(&entry->skb, skb);

		atomic_inc(&arp_unres_size);
		entry->u.next = arp_tables[hash];
		arp_tables[hash] = entry;
		arp_start_resolution(entry);
		dst_release(&entry->u.dst);
	}
	return entry;
}


/*
 *	Find an arp mapping in the cache. If not found, post a request.
 */

int arp_find(unsigned char *haddr, struct sk_buff *skb)
{
	struct device *dev = skb->dev;
	u32 paddr;
	struct arp_table *entry;
	unsigned long hash;

	if (!skb->dst) {
		printk(KERN_DEBUG "arp_find called with dst==NULL\n");
		return 1;
	}

	paddr = ((struct rtable*)skb->dst)->rt_gateway;

	if (arp_set_predefined(__ip_chk_addr(paddr), haddr, paddr, dev)) {
		if (skb)
			skb->arp = 1;
		return 0;
	}

	hash = HASH(paddr);

	start_bh_atomic();

	/*
	 *	Find an entry
	 */
	entry = arp_lookup(paddr, dev);

	if (entry != NULL) 	/* It exists */
	{
		if (entry->flags & ATF_COM)
		{
			entry->u.dst.lastuse = jiffies;
			memcpy(haddr, entry->ha, dev->addr_len);
			if (skb)
				skb->arp = 1;
			end_bh_atomic();
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
				if (entry->skb.qlen < ARP_MAX_UNRES_PACKETS)
					skb_queue_tail(&entry->skb, skb);
				else
					kfree_skb(skb, FREE_WRITE);
			}
			/*
			 * If last_updated==0 host is dead, so
			 * drop skb's and set socket error.
			 */
			else
			{
				icmp_send(skb, ICMP_DEST_UNREACH, ICMP_HOST_UNREACH, 0);
				kfree_skb(skb, FREE_WRITE);
			}
		}
		end_bh_atomic();
		return 1;
	}

	entry = arp_new_entry(paddr, dev, skb);

	if (skb != NULL && !entry)
		kfree_skb(skb, FREE_WRITE);

	end_bh_atomic();
	return 1;
}

int arp_find_1(unsigned char *haddr, struct dst_entry *dst,
	       struct dst_entry *neigh)
{
	struct rtable *rt = (struct rtable*)dst;
	struct device *dev = dst->dev;
	u32 paddr = rt->rt_gateway;
	struct arp_table *entry;
	unsigned long hash;

	if (!neigh)
	{
		if ((rt->rt_flags & RTF_MULTICAST) &&
		    (dev->type==ARPHRD_ETHER || dev->type==ARPHRD_IEEE802))
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
		if (rt->rt_flags & (RTF_BROADCAST|RTF_MULTICAST))
		{
			memcpy(haddr, dev->broadcast, dev->addr_len);
			return 1;
		}
		if (rt->rt_flags & RTF_LOCAL)
		{
			printk(KERN_DEBUG "ARP: arp called for own IP address\n");
			memcpy(haddr, dev->dev_addr, dev->addr_len);
			return 1;
		}
		return 0;
	}

	hash = HASH(paddr);

	start_bh_atomic();

	entry = (struct arp_table*)neigh;

	if (entry->flags & ATF_COM)
	{
		entry->u.dst.lastuse = jiffies;
		memcpy(haddr, entry->ha, dev->addr_len);
		end_bh_atomic();
		return 1;
	}

	end_bh_atomic();
	return 0;
}


struct dst_entry* arp_find_neighbour(struct dst_entry *dst, int resolve)
{
	struct rtable *rt = (struct rtable*)dst;
	struct device *dev = rt->u.dst.dev;
	u32 paddr = rt->rt_gateway;
	struct arp_table *entry;
	unsigned long hash;

	if (dst->ops->family != AF_INET)
		return NULL;

	if ((dev->flags & (IFF_LOOPBACK|IFF_NOARP)) ||
	    (rt->rt_flags & (RTF_LOCAL|RTF_BROADCAST|RTF_MULTICAST)))
		return NULL;

	hash = HASH(paddr);

	start_bh_atomic();

	/*
	 *	Find an entry
	 */
	entry = arp_lookup(paddr, dev);

	if (entry != NULL) 	/* It exists */
	{
		atomic_inc(&entry->u.dst.refcnt);
		end_bh_atomic();
		entry->u.dst.lastuse = jiffies;
		return (struct dst_entry*)entry;
	}

	if (!resolve)
		return NULL;

	entry = arp_new_entry(paddr, dev, NULL);

	if (entry)
		atomic_inc(&entry->u.dst.refcnt);

	end_bh_atomic();

	return (struct dst_entry*)entry;
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
	skb->protocol = htons (ETH_P_IP);

	/*
	 *	Fill the device header for the ARP frame
	 */
	dev->hard_header(skb,dev,ptype,dest_hw?dest_hw:dev->broadcast,src_hw?src_hw:NULL,skb->len);

	/*
	 * Fill out the arp protocol part.
	 *
	 * The arp hardware type should match the device type, except for FDDI,
	 * which (according to RFC 1390) should always equal 1 (Ethernet).
	 */
#ifdef CONFIG_FDDI
	arp->ar_hrd = (dev->type == ARPHRD_FDDI) ? htons(ARPHRD_ETHER) : htons(dev->type);
#else
	arp->ar_hrd = htons(dev->type);
#endif
	/*
	 *	Exceptions everywhere. AX.25 uses the AX.25 PID value not the
	 *	DIX code for the protocol. Make these device structure fields.
	 */
#if defined(CONFIG_AX25) || defined(CONFIG_AX25_MODULE)
#if defined(CONFIG_NETROM) || defined(CONFIG_NETROM_MODULE)
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
	skb->dev = dev;
	skb->priority = 0;

	dev_queue_xmit(skb);
}


/*
 *	Receive an arp request by the device layer.
 */

int arp_rcv(struct sk_buff *skb, struct device *dev, struct packet_type *pt)
{
	struct arphdr *arp = skb->nh.arph;
	unsigned char *arp_ptr= (unsigned char *)(arp+1);
	struct rtable *rt;
	unsigned char *sha, *tha;
	u32 sip, tip;

/*
 *	The hardware length of the packet should match the hardware length
 *	of the device.  Similarly, the hardware types should match.  The
 *	device should be ARP-able.  Also, if pln is not 4, then the lookup
 *	is not from an IP number.  We can't currently handle this, so toss
 *	it. 
 */  
#ifdef CONFIG_FDDI
	if (dev->type == ARPHRD_FDDI)
	{
		/*
		 * According to RFC 1390, FDDI devices should accept ARP hardware types
		 * of 1 (Ethernet).  However, to be more robust, we'll accept hardware
		 * types of either 1 (Ethernet) or 6 (IEEE 802.2).
		 */
		if (arp->ar_hln != dev->addr_len    || 
		    ((ntohs(arp->ar_hrd) != ARPHRD_ETHER) && (ntohs(arp->ar_hrd) != ARPHRD_IEEE802)) ||
		    dev->flags & IFF_NOARP          ||
		    skb->pkt_type == PACKET_OTHERHOST ||
		    arp->ar_pln != 4)
		{
			kfree_skb(skb, FREE_READ);
			return 0;
		}
	}
	else
	{
		if (arp->ar_hln != dev->addr_len    || 
		    dev->type != ntohs(arp->ar_hrd) ||
		    dev->flags & IFF_NOARP          ||
		    skb->pkt_type == PACKET_OTHERHOST ||
		    arp->ar_pln != 4)
		{
			kfree_skb(skb, FREE_READ);
			return 0;
		}
	}
#else
	if (arp->ar_hln != dev->addr_len    || 
#if CONFIG_AP1000
	    /*
	     * ARP from cafe-f was found to use ARPHDR_IEEE802 instead of
	     * the expected ARPHDR_ETHER.
	     */
	    (strcmp(dev->name,"fddi") == 0 && 
	     arp->ar_hrd != ARPHRD_ETHER && arp->ar_hrd != ARPHRD_IEEE802) ||
	    (strcmp(dev->name,"fddi") != 0 &&
	     dev->type != ntohs(arp->ar_hrd)) ||
#else
	    dev->type != ntohs(arp->ar_hrd) || 
#endif
		dev->flags & IFF_NOARP          ||
	        skb->pkt_type == PACKET_OTHERHOST ||
		arp->ar_pln != 4) {
		kfree_skb(skb, FREE_READ);
		return 0;
	}
#endif

/*
 *	Another test.
 *	The logic here is that the protocol being looked up by arp should 
 *	match the protocol the device speaks.  If it doesn't, there is a
 *	problem, so toss the packet.
 */

  	switch (dev->type)
  	{
#if defined(CONFIG_AX25) || defined(CONFIG_AX25_MODULE)
		case ARPHRD_AX25:
			if(arp->ar_pro != htons(AX25_P_IP))
			{
				kfree_skb(skb, FREE_READ);
				return 0;
			}
			break;
#endif
#if defined(CONFIG_NETROM) || defined(CONFIG_NETROM_MODULE)
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
		case ARPHRD_IEEE802:
		case ARPHRD_FDDI:
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
	if (LOOPBACK(tip) || MULTICAST(tip)) {
		kfree_skb(skb, FREE_READ);
		return 0;
	}
	if (ip_route_input(skb, tip, sip, 0, dev)) {
		kfree_skb(skb, FREE_READ);
		return 0;
	}
	dev = skb->dev;
	rt = (struct rtable*)skb->dst;
	if (dev->type != ntohs(arp->ar_hrd) || dev->flags&IFF_NOARP ||
	    rt->rt_flags&RTF_BROADCAST) {
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

	if (arp->ar_op == htons(ARPOP_REQUEST)) {
		struct arp_table *entry;

		for (entry = arp_proxy_list; entry; entry = entry->u.next) {
			if (!((entry->ip^tip)&entry->mask) &&
			    ((!entry->u.dst.dev &&
			      (!(entry->flags & ATF_COM) || entry->hatype == dev->type))
			     || entry->u.dst.dev == dev) )
				break;
		}

		if (entry && !(entry->flags & ATF_DONTPUB)) {
			char *ha = (entry->flags & ATF_COM) ? entry->ha : dev->dev_addr;

			if (rt->rt_flags&(RTF_LOCAL|RTF_NAT) ||
			    (!(rt->rt_flags&RTCF_DOREDIRECT) &&
			     rt->u.dst.dev != dev))
				arp_send(ARPOP_REPLY,ETH_P_ARP,sip,dev,tip,sha,ha,sha);
		}
	}

	start_bh_atomic();
	arp_update(sip, sha, dev, 0, !RT_LOCALADDR(rt->rt_flags) && dev->type != ARPHRD_METRICOM);
	end_bh_atomic();
	kfree_skb(skb, FREE_READ);
	return 0;
}



/*
 *	User level interface (ioctl, /proc)
 */

/*
 *	Set (create) an ARP cache entry.
 */

int arp_req_set(struct arpreq *r, struct device * dev)
{
	struct arp_table *entry, **entryp;
	struct sockaddr_in *si;
	unsigned char *ha = NULL;
	u32 ip;
	u32 mask = DEF_ARP_NETMASK;

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
		if (ip & ~mask)
			return -EINVAL;
		if (!dev && (r->arp_flags & ATF_COM))
		{
			dev = dev_getbyhwaddr(r->arp_ha.sa_family, r->arp_ha.sa_data);
			if (!dev)
				return -ENODEV;
		}
	}
	else
	{
		struct rtable * rt;
		int err;

		if ((r->arp_flags & ATF_PERM) && !(r->arp_flags & ATF_COM))
			return -EINVAL;
		err = ip_route_output(&rt, ip, 0, 1, dev);
		if (err)
			return err;
		if (!dev)
			dev = rt->u.dst.dev;
		if (rt->rt_flags&(RTF_LOCAL|RTF_BROADCAST|RTF_MULTICAST|RTCF_NAT)) {
			if (rt->rt_flags&RTF_BROADCAST &&
			    dev->type == ARPHRD_METRICOM &&
			    r->arp_ha.sa_family == ARPHRD_METRICOM) {
				memcpy(dev->broadcast, r->arp_ha.sa_data, dev->addr_len);
				ip_rt_put(rt);
				return 0;
			}
			ip_rt_put(rt);
			return -EINVAL;
		}
		ip_rt_put(rt);
	}
	
	if (dev && (dev->flags&(IFF_LOOPBACK|IFF_NOARP)))
		return -ENODEV;

	if (dev && r->arp_ha.sa_family != dev->type)	
		return -EINVAL;
		
	start_bh_atomic();

	if (!(r->arp_flags & ATF_PUBL))
		entryp = &arp_tables[HASH(ip)];
	else
		entryp = &arp_proxy_list;

	while ((entry = *entryp) != NULL)
	{
		if (entry->mask == mask)
			break;
		if ((entry->mask & mask) != mask)
			break;
		entryp = &entry->u.next;
	}
	while ((entry = *entryp) != NULL && entry->mask == mask)
	{
		if (entry->ip == ip)
			break;
		entryp = &entry->u.next;
	}
	while ((entry = *entryp) != NULL && entry->mask == mask &&
	       entry->ip == ip)
	{
		if (!entry->u.dst.dev || entry->u.dst.dev == dev)
			break;
		entryp = &entry->u.next;
	}

	while ((entry = *entryp) != NULL)
	{
		if (entry->ip != ip || entry->mask != mask ||
		    entry->u.dst.dev != dev)
		{
			entry = NULL;
			break;
		}
		if (entry->hatype == r->arp_ha.sa_family &&
		    (!(r->arp_flags & ATF_MAGIC) ||
		     entry->flags == r->arp_flags))
			break;
		entryp = &entry->u.next;
	}

	if (entry)
		atomic_inc(&entry->u.dst.refcnt);
	else
	{
		entry = arp_alloc(r->arp_flags&ATF_PUBL ? 0 : 1);
		if (entry == NULL)
		{
			end_bh_atomic();
			return -ENOMEM;
		}
		entry->ip = ip;
		entry->u.dst.dev = dev;
		entry->mask = mask;

		if (dev)
			entry->hatype = dev->type;

		entry->u.next = *entryp;
		*entryp = entry;
	}
	entry->flags = r->arp_flags;
	if (!(entry->flags&(ATF_PUBL|ATF_COM)))
		atomic_inc(&arp_unres_size);

	if (entry->flags & ATF_PUBL)
	{
		if (entry->flags & ATF_COM)
		{
			entry->hatype = r->arp_ha.sa_family;
			ha = r->arp_ha.sa_data;
		}
		else if (dev)
			ha = dev->dev_addr;
	}
	else
		ha = r->arp_ha.sa_data;

	if (ha)
		memcpy(entry->ha, ha, dev ? dev->addr_len : MAX_ADDR_LEN);
	else
		memset(entry->ha, 0, MAX_ADDR_LEN);

	entry->last_updated = entry->u.dst.lastuse = jiffies;
	
	if (!(entry->flags & ATF_PUBL))
	{
		if (entry->flags & ATF_COM)
		{
			arpd_update(entry->ip, entry->u.dst.dev, ha);
			arp_update_hhs(entry);
		}
		else
			arp_start_resolution(entry);
	}

	dst_release(&entry->u.dst);
	end_bh_atomic();
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

	start_bh_atomic();

	if (!(r->arp_flags & ATF_PUBL))
		entry = arp_tables[HASH(si->sin_addr.s_addr)];
	else
		entry = arp_proxy_list;

	for ( ; entry ;entry = entry->u.next)
	{
		if (entry->ip == si->sin_addr.s_addr &&
		    (!(r->arp_flags&ATF_NETMASK) || entry->mask == mask) &&
		    ( (r->arp_flags&ATF_PUBL) ?
		      (entry->u.dst.dev == dev && entry->hatype == r->arp_ha.sa_family)
		     : (entry->u.dst.dev == dev || !dev)))
		{
			if (entry->u.dst.dev)
			{
				memcpy(r->arp_ha.sa_data, entry->ha, entry->u.dst.dev->addr_len);
				r->arp_ha.sa_family = entry->u.dst.dev->type;
				strncpy(r->arp_dev, entry->u.dst.dev->name, sizeof(r->arp_dev));
			}
			else
			{
				r->arp_ha.sa_family = entry->hatype;
				memset(r->arp_ha.sa_data, 0, sizeof(r->arp_ha.sa_data));
			}
			r->arp_flags = entry->flags;
			end_bh_atomic();
			return 0;
		}
	}

	end_bh_atomic();
	return -ENXIO;
}

int arp_req_delete(struct arpreq *r, struct device * dev)
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

	start_bh_atomic();

	if (!(r->arp_flags & ATF_PUBL))
		entryp = &arp_tables[HASH(si->sin_addr.s_addr)];
	else
		entryp = &arp_proxy_list;

	while ((entry = *entryp) != NULL)
	{
		if (entry->ip == si->sin_addr.s_addr
		    && (!(r->arp_flags&ATF_NETMASK) || entry->mask == mask)
		    && (entry->u.dst.dev == dev || (!(r->arp_flags&ATF_PUBL) && !dev))
		    && (!(r->arp_flags&ATF_MAGIC) || r->arp_flags == entry->flags))
		{
			if (!entry->u.dst.refcnt)
			{
				arp_free(entryp);
				retval = 0;
				continue;
			}
			if (retval)
				retval = -EBUSY;
		}
		entryp = &entry->u.next;
	}

	end_bh_atomic();
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
			err = copy_from_user(&r, arg, sizeof(struct arpreq));
			if (err)
				return -EFAULT;
			break;
		case OLD_SIOCDARP:
		case OLD_SIOCSARP:
			if (!suser())
				return -EPERM;
		case OLD_SIOCGARP:
			err = copy_from_user(&r, arg, sizeof(struct arpreq_old));
			if (err)
				return -EFAULT;
			memset(&r.arp_dev, 0, sizeof(r.arp_dev));
			break;
		default:
			return -EINVAL;
	}

	if (r.arp_pa.sa_family != AF_INET)
		return -EPFNOSUPPORT;

	if (!(r.arp_flags & ATF_PUBL) &&
	    (r.arp_flags & (ATF_NETMASK|ATF_DONTPUB|ATF_MAGIC)))
		return -EINVAL;
	if (!(r.arp_flags & ATF_NETMASK))
		((struct sockaddr_in *)&r.arp_netmask)->sin_addr.s_addr=DEF_ARP_NETMASK;

	if (r.arp_dev[0])
	{
		if ((dev = dev_get(r.arp_dev)) == NULL)
			return -ENODEV;

		if (!r.arp_ha.sa_family)
			r.arp_ha.sa_family = dev->type;
		if ((r.arp_flags & ATF_COM) && r.arp_ha.sa_family != dev->type)
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
			err = arp_req_get(&r, dev);
			if (!err)
				err = copy_to_user(arg, &r, sizeof(r));
			return err;
		case OLD_SIOCGARP:
			r.arp_flags &= ~ATF_PUBL;
			err = arp_req_get(&r, dev);
			if (err < 0)
			{
				r.arp_flags |= ATF_PUBL;
				err = arp_req_get(&r, dev);
			}
			if (!err)
				err = copy_to_user(arg, &r, sizeof(struct arpreq_old));
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


	for(i=0; i<FULL_ARP_TABLE_SIZE; i++)
	{
		start_bh_atomic();

		for(entry=arp_tables[i]; entry!=NULL; entry=entry->u.next)
		{
/*
 *	Convert hardware address to XX:XX:XX:XX ... form.
 */
#if defined(CONFIG_AX25) || defined(CONFIG_AX25_MODULE)
#if defined(CONFIG_NETROM) || defined(CONFIG_NETROM_MODULE)
			if (entry->hatype == ARPHRD_AX25 || entry->hatype == ARPHRD_NETROM)
			     strcpy(hbuffer,ax2asc((ax25_address *)entry->ha));
			else {
#else
			if(entry->hatype==ARPHRD_AX25)
			     strcpy(hbuffer,ax2asc((ax25_address *)entry->ha));
			else {
#endif
#endif
				
			if (entry->u.dst.dev)
			{
				for(k=0,j=0;k<HBUFFERLEN-3 && j<entry->u.dst.dev->addr_len;j++)
				{
					hbuffer[k++]=hexbuf[ (entry->ha[j]>>4)&15 ];
					hbuffer[k++]=hexbuf[  entry->ha[j]&15     ];
					hbuffer[k++]=':';
				}
				hbuffer[--k]=0;
			}
			else
				strcpy(hbuffer, "00:00:00:00:00:00");
	
#if defined(CONFIG_AX25) || defined(CONFIG_AX25_MODULE)
			}
#endif

			size = sprintf(buffer+len,
				"%-17s0x%-10x0x%-10x%s",
				in_ntoa(entry->ip),
				entry->hatype,
				entry->flags,
				hbuffer);
#if RT_CACHE_DEBUG < 2
			size += sprintf(buffer+len+size,
				 "     %-17s %s\n",
				 entry->mask==DEF_ARP_NETMASK ?
				 "*" : in_ntoa(entry->mask),
					entry->u.dst.dev ? entry->u.dst.dev->name : "*");
#else
			size += sprintf(buffer+len+size,
				 "     %-17s %s\t%d\t%d\t%1d\n",
				 entry->mask==DEF_ARP_NETMASK ?
				 "*" : in_ntoa(entry->mask),
				 entry->u.dst.dev ? entry->u.dst.dev->name : "*", 
				 entry->u.dst.refcnt,
				 entry->u.dst.hh ? entry->u.dst.hh->hh_refcnt : -1,
				 entry->u.dst.hh ? entry->u.dst.hh->hh_uptodate : 0);
#endif
	
			len += size;
			pos += size;
		  
			if (pos <= offset)
				len=0;
			if (pos >= offset+length)
			{
				end_bh_atomic();
				goto done;
			}
		}
		end_bh_atomic();	
	}
done:
  
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
	__constant_htons(ETH_P_ARP),
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

#ifdef CONFIG_PROC_FS
static struct proc_dir_entry proc_net_arp = {
	PROC_NET_ARP, 3, "arp",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_net_inode_operations,
	arp_get_info
};
#endif

void arp_init (void)
{
	dev_add_pack(&arp_packet_type);
	/* Start with the regular checks for expired arp entries. */
	add_timer(&arp_timer);
	/* Register for device down reports */
	register_netdevice_notifier(&arp_dev_notifier);

#ifdef CONFIG_PROC_FS
	proc_net_register(&proc_net_arp);
#endif

#ifdef CONFIG_ARPD
	netlink_attach(NETLINK_ARPD, arpd_callback);
#endif
}


#ifdef CONFIG_AX25_MODULE

/*
 *	ax25 -> ascii conversion
 */
char *ax2asc(ax25_address *a)
{
	static char buf[11];
	char c, *s;
	int n;

	for (n = 0, s = buf; n < 6; n++) {
		c = (a->ax25_call[n] >> 1) & 0x7F;

		if (c != ' ') *s++ = c;
	}
	
	*s++ = '-';

	if ((n = ((a->ax25_call[6] >> 1) & 0x0F)) > 9) {
		*s++ = '1';
		n -= 10;
	}
	
	*s++ = n + '0';
	*s++ = '\0';

	if (*buf == '\0' || *buf == '-')
	   return "*";

	return buf;

}

#endif
