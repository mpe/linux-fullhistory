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
 *              Ross Martin     :       Rewrote arp_rcv() and arp_get_info()
 *		Stephen Henson	:	Add AX25 support to arp_get_info()
 *		Alan Cox	:	Drop data when a device is downed.
 *		Alan Cox	:	Use init_timer()
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
#include <asm/system.h>
#include <asm/segment.h>
#include <stdarg.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include "ip.h"
#include "route.h"
#include "protocol.h"
#include "tcp.h"
#include <linux/skbuff.h>
#include "sock.h"
#include "arp.h"
#ifdef CONFIG_AX25
#include "ax25.h"
#endif

/*
 *	This structure defines the ARP mapping cache. As long as we make changes
 *	in this structure, we keep interrupts of. But normally we can copy the
 *	hardware address and the device pointer in a local variable and then make
 *	any "long calls" to send a packet out.
 */

struct arp_table
{
	struct arp_table		*next;			/* Linked entry list 		*/
	unsigned long			last_used;		/* For expiry 			*/
	unsigned int			flags;			/* Control status 		*/
	unsigned long			ip;			/* ip address of entry 		*/
	unsigned char			ha[MAX_ADDR_LEN];	/* Hardware address		*/
	unsigned char			hlen;			/* Length of hardware address 	*/
	unsigned short			htype;			/* Type of hardware in use	*/
	struct device			*dev;			/* Device the entry is tied to 	*/

	/*
	 *	The following entries are only used for unresolved hw addresses.
	 */
	
	struct timer_list		timer;			/* expire timer 		*/
	int				retries;		/* remaining retries	 	*/
	struct sk_buff_head		skb;			/* list of queued packets 	*/
};

/*
 *	This structure defines an ethernet arp header.
 */

struct arphdr
{
	unsigned short	ar_hrd;		/* format of hardware address	*/
	unsigned short	ar_pro;		/* format of protocol address	*/
	unsigned char	ar_hln;		/* length of hardware address	*/
	unsigned char	ar_pln;		/* length of protocol address	*/
	unsigned short	ar_op;		/* ARP opcode (command)		*/

#if 0
	 /*
	  *	 Ethernet looks like this : This bit is variable sized however...
	  */
	unsigned char		ar_sha[ETH_ALEN];	/* sender hardware address	*/
	unsigned char		ar_sip[4];		/* sender IP address		*/
	unsigned char		ar_tha[ETH_ALEN];	/* target hardware address	*/
	unsigned char		ar_tip[4];		/* target IP address		*/
#endif

};

/*
 *	Configurable Parameters (don't touch unless you know what you are doing
 */

/*
 *	If an arp request is send, ARP_RES_TIME is the timeout value until the
 *	next request is send.
 */

#define ARP_RES_TIME		(250*(HZ/10))

/*
 *	The number of times an arp request is send, until the host is
 *	considered unreachable.
 */

#define ARP_MAX_TRIES		3

/*
 *	After that time, an unused entry is deleted from the arp table.
 */

#define ARP_TIMEOUT		(600*HZ)

/*
 *	How often is the function 'arp_check_retries' called.
 *	An entry is invalidated in the time between ARP_TIMEOUT and
 *	(ARP_TIMEOUT+ARP_CHECK_INTERVAL).
 */

#define ARP_CHECK_INTERVAL	(60 * HZ)


static void arp_check_expire (unsigned long);  /* Forward declaration. */


static struct timer_list arp_timer =
	{ NULL, NULL, ARP_CHECK_INTERVAL, 0L, &arp_check_expire };

/*
 * 	The size of the hash table. Must be a power of two.
 * 	Maybe we should remove hashing in the future for arp and concentrate
 * 	on Patrick Schaaf's Host-Cache-Lookup...
 */

#define ARP_TABLE_SIZE  16

struct arp_table *arp_tables[ARP_TABLE_SIZE] =
{
	NULL,
};

/*
 *	The last bits in the IP address are used for the cache lookup.
 */

#define HASH(paddr) 		(htonl(paddr) & (ARP_TABLE_SIZE - 1))

/*
 *	Number of proxy arp entries. This is normally zero and we use it to do
 *	some optimizing for normal uses.
 */

static int proxies = 0;


/*
 *	Check if there are too old entries and remove them. If the ATF_PERM
 *	flag is set, they are always left in the arp cache (permanent entry).
 *	Note: Only fully resolved entries, which don't have any packets in
 *	the queue, can be deleted, since ARP_TIMEOUT is much greater than
 *	ARP_MAX_TRIES*ARP_RES_TIME.
 */

static void arp_check_expire(unsigned long dummy)
{
	int i;
	unsigned long now = jiffies;
	unsigned long flags;
	save_flags(flags);
	cli();

	for (i = 0; i < ARP_TABLE_SIZE; i++)
	{
		struct arp_table *entry;
		struct arp_table **pentry = &arp_tables[i];

		while ((entry = *pentry) != NULL)
		{
			if ((now - entry->last_used) > ARP_TIMEOUT
				&& !(entry->flags & ATF_PERM))
			{
				*pentry = entry->next;	/* remove from list */
				if (entry->flags & ATF_PUBL)
					proxies--;
				del_timer(&entry->timer);	/* Paranoia */
				kfree_s(entry, sizeof(struct arp_table));
			}
			else
				pentry = &entry->next;	/* go to next entry */
		}
	}
	restore_flags(flags);

	/*
	 *	Set the timer again.
	 */

	del_timer(&arp_timer);
	arp_timer.expires = ARP_CHECK_INTERVAL;
	add_timer(&arp_timer);
}


/*
 *	Release all linked skb's and the memory for this entry.
 */

static void arp_release_entry(struct arp_table *entry)
{
	struct sk_buff *skb;
	unsigned long flags;

	if (entry->flags & ATF_PUBL)
		proxies--;
		
	save_flags(flags);
	cli();
	/* Release the list of `skb' pointers. */
	while ((skb = skb_dequeue(&entry->skb)) != NULL)
	{
		skb_device_lock(skb);
		restore_flags(flags);
		dev_kfree_skb(skb, FREE_WRITE);
	}
	restore_flags(flags);
	del_timer(&entry->timer);
	kfree_s(entry, sizeof(struct arp_table));
	return;
}

/*
 *	Purge a device from the ARP queue
 */
 
void arp_device_down(struct device *dev)
{
	int i;
	unsigned long flags;
	
	/*
	 *	This is a bit OTT - maybe we need some arp semaphores instead.
	 */
	save_flags(flags);
	cli();
	for (i = 0; i < ARP_TABLE_SIZE; i++)
	{
		struct arp_table *entry;
		struct arp_table **pentry = &arp_tables[i];

		while ((entry = *pentry) != NULL)
		{
			if(entry->dev==dev)
			{
				*pentry = entry->next;	/* remove from list */
				if (entry->flags & ATF_PUBL)
					proxies--;
				del_timer(&entry->timer);	/* Paranoia */
				kfree_s(entry, sizeof(struct arp_table));
			}
			else
				pentry = &entry->next;	/* go to next entry */
		}
	}
	restore_flags(flags);
}


/*
 *	Create and send an arp packet. If (dest_hw == NULL), we create a broadcast
 *	message.
 */

void arp_send(int type, int ptype, unsigned long dest_ip, 
	      struct device *dev, unsigned long src_ip, 
	      unsigned char *dest_hw, unsigned char *src_hw)
{
	struct sk_buff *skb;
	struct arphdr *arp;
	unsigned char *arp_ptr;

	/*
	 *	No arp on this interface.
	 */
	
	if(dev->flags&IFF_NOARP)
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
	skb->len = sizeof(struct arphdr) + dev->hard_header_len + 2*(dev->addr_len+4);
	skb->arp = 1;
	skb->dev = dev;
	skb->free = 1;

	/*
	 *	Fill the device header for the ARP frame
	 */

	dev->hard_header(skb->data,dev,ptype,dest_hw?dest_hw:dev->broadcast,src_hw?src_hw:NULL,skb->len,skb);

	/* Fill out the arp protocol part. */
	arp = (struct arphdr *) (skb->data + dev->hard_header_len);
	arp->ar_hrd = htons(dev->type);
#ifdef CONFIG_AX25
	arp->ar_pro = (dev->type != ARPHRD_AX25)? htons(ETH_P_IP) : htons(AX25_P_IP);
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
	if (dest_hw != NULL)
		memcpy(arp_ptr, dest_hw, dev->addr_len);
	else
		memset(arp_ptr, 0, dev->addr_len);
	arp_ptr+=dev->addr_len;
	memcpy(arp_ptr, &dest_ip, 4);

	dev_queue_xmit(skb, dev, 0);
}


/*
 *	This function is called, if an entry is not resolved in ARP_RES_TIME.
 *	Either resend a request, or give it up and free the entry.
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

	if (--entry->retries > 0)
	{
		unsigned long ip = entry->ip;
		struct device *dev = entry->dev;

		/* Set new timer. */
		del_timer(&entry->timer);
		entry->timer.expires = ARP_RES_TIME;
		add_timer(&entry->timer);
		restore_flags(flags);
		arp_send(ARPOP_REQUEST, ETH_P_ARP, ip, dev, dev->pa_addr, 
			 NULL, dev->dev_addr);
		return;
	}

	/*
	 *	Arp request timed out. Delete entry and all waiting packets.
	 *	If we give each entry a pointer to itself, we don't have to
	 *	loop through everything again. Maybe hash is good enough, but
	 *	I will look at it later.
	 */
	
	hash = HASH(entry->ip);
	pentry = &arp_tables[hash];
	while (*pentry != NULL)
	{
		if (*pentry == entry)
		{
			*pentry = entry->next;	/* delete from linked list */
			del_timer(&entry->timer);
			restore_flags(flags);
			arp_release_entry(entry);
			return;
		}
		pentry = &(*pentry)->next;
	}
	restore_flags(flags);
	printk("Possible ARP queue corruption.\n");
	/*
	 *	We should never arrive here.
	 */
}


/*
 *	This will try to retransmit everything on the queue.
 */

static void arp_send_q(struct arp_table *entry, unsigned char *hw_dest)
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
		else
		{
			/* This routine is only ever called when 'entry' is
			   complete. Thus this can't fail. */
			printk("arp_send_q: The impossible occurred. Please notify Alan.\n");
			printk("arp_send_q: active entity %s\n",in_ntoa(entry->ip));
			printk("arp_send_q: failed to find %s\n",in_ntoa(skb->raddr));
		}
	}
	restore_flags(flags);
}


/*
 *	Delete an ARP mapping entry in the cache.
 */

void arp_destroy(unsigned long ip_addr, int force)
{
	struct arp_table *entry;
	struct arp_table **pentry;
	unsigned long hash = HASH(ip_addr);

	cli();
	pentry = &arp_tables[hash];
	while ((entry = *pentry) != NULL)
	{
		if (entry->ip == ip_addr)
		{
			if ((entry->flags & ATF_PERM) && !force)
				return;
			*pentry = entry->next;
			del_timer(&entry->timer);
			sti();
			arp_release_entry(entry);
			return;
		}
		pentry = &entry->next;
	}
	sti();
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
	int addr_hint,hlen,htype;
	unsigned long hash,dest_hash;
	unsigned char ha[MAX_ADDR_LEN];	/* So we can enable ints again. */
	long sip,tip;
	unsigned char *sha,*tha;

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
	}

/*
 *	Another test.
 *	The logic here is that the protocol being looked up by arp should 
 *	match the protocol the device speaks.  If it doesn't, there is a
 *	problem, so toss the packet.
 */
  	switch(dev->type)
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
		case ARPHRD_ETHER:
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

	hlen  = dev->addr_len;
	htype = dev->type;

	sha=arp_ptr;
	arp_ptr+=hlen;
	memcpy(&sip,arp_ptr,4);
	arp_ptr+=4;
	tha=arp_ptr;
	arp_ptr+=hlen;
	memcpy(&tip,arp_ptr,4);
  
/* 
 *	Check for bad requests for 127.0.0.1.  If this is one such, delete it.
 */
	if(tip == INADDR_LOOPBACK)
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

	addr_hint = ip_chk_addr(tip);

	if(arp->ar_op == htons(ARPOP_REPLY))
	{
		if(addr_hint!=IS_MYADDR)
		{
/* 
 *	Replies to other machines get tossed. 
 */
			kfree_skb(skb, FREE_READ);
			return 0;
		}
/*
 *	Fall through to code below that adds sender to cache. 
 */
	}
	else
	{ 
/* 
 * 	It is now an arp request 
 */
		if(addr_hint != IS_MYADDR)
		{
/*
 * 	To get in here, it is a request for someone else.  We need to
 * 	check if that someone else is one of our proxies.  If it isn't,
 * 	we can toss it.
 */
			if (proxies == 0)
			{
				kfree_skb(skb, FREE_READ);
				return 0;
			}
	
			dest_hash = HASH(tip);
			cli();
			for(proxy_entry=arp_tables[dest_hash];
				proxy_entry;
				proxy_entry = proxy_entry->next)
			{
				if(proxy_entry->ip == tip && proxy_entry->htype==htype)
					break;
			}
			if (proxy_entry && (proxy_entry->flags & ATF_PUBL))
			{
				memcpy(ha, proxy_entry->ha, hlen);
				sti();
				arp_send(ARPOP_REPLY,ETH_P_ARP,sip,dev,tip,sha,ha);
				kfree_skb(skb, FREE_READ);
				return 0;
			}
			else
			{
				sti();
				kfree_skb(skb, FREE_READ);
				return 0;
			}
		}
		else
		{
/*
 * 	To get here, it must be an arp request for us.  We need to reply.
 */
			arp_send(ARPOP_REPLY,ETH_P_ARP,sip,dev,tip,sha,dev->dev_addr);
		}
	}


/*
 * Now all replies are handled.  Next, anything that falls through to here
 * needs to be added to the arp cache, or have its entry updated if it is 
 * there.
 */

	hash = HASH(sip);
	cli();
	for(entry=arp_tables[hash];entry;entry=entry->next)
		if(entry->ip==sip && entry->htype==htype)
			break;

	if(entry)
	{
/*
 *	Entry found; update it.
 */
		memcpy(entry->ha, sha, hlen);
		entry->hlen = hlen;
		entry->last_used = jiffies;
		if (!(entry->flags & ATF_COM))
		{
/*
 *	This entry was incomplete.  Delete the retransmit timer
 *	and switch to complete status.
 */
			del_timer(&entry->timer);
			entry->flags |= ATF_COM;
			sti();
/* 
 *	Send out waiting packets. We might have problems, if someone is 
 *	manually removing entries right now -- entry might become invalid 
 *	underneath us.
 */
			arp_send_q(entry, sha);
		}
		else
		{
			sti();
		}
	}
	else
	{
/*
 * 	No entry found.  Need to add a new entry to the arp table.
 */
		entry = (struct arp_table *)kmalloc(sizeof(struct arp_table),GFP_ATOMIC);
		if(entry == NULL)
		{
			sti();
			printk("ARP: no memory for new arp entry\n");

			kfree_skb(skb, FREE_READ);
			return 0;
		}

		entry->ip = sip;
		entry->hlen = hlen;
		entry->htype = htype;
		entry->flags = ATF_COM;
		init_timer(&entry->timer);
		memcpy(entry->ha, sha, hlen);
		entry->last_used = jiffies;
		entry->dev = skb->dev;
		skb_queue_head_init(&entry->skb);
		entry->next = arp_tables[hash];
		arp_tables[hash] = entry;
		sti();
	}

/*
 *	Replies have been sent, and entries have been added.  All done.
 */
	kfree_skb(skb, FREE_READ);
	return 0;
}


/*
 *	Find an arp mapping in the cache. If not found, post a request.
 */

int arp_find(unsigned char *haddr, unsigned long paddr, struct device *dev,
	   unsigned long saddr, struct sk_buff *skb)
{
	struct arp_table *entry;
	unsigned long hash;
	switch (ip_chk_addr(paddr))
	{
		case IS_MYADDR:
			printk("ARP: arp called for own IP address\n");
			memcpy(haddr, dev->dev_addr, dev->addr_len);
			skb->arp = 1;
			return 0;
		case IS_BROADCAST:
			memcpy(haddr, dev->broadcast, dev->addr_len);
			skb->arp = 1;
			return 0;
	}

	hash = HASH(paddr);
	cli();

	/*
	 *	Find an entry
	 */
	for (entry = arp_tables[hash]; entry != NULL; entry = entry->next)
		if (entry->ip == paddr)
			break;


	if (entry != NULL) 	/* It exists */
	{
	        if (!(entry->flags & ATF_COM))
	        {
			/*
			 *	A request was already send, but no reply yet. Thus
			 *	queue the packet with the previous attempt
			 */
			
			if (skb != NULL)
				skb_queue_tail(&entry->skb, skb);
			sti();
			return 1;
		}

		/*
		 *	Update the record
		 */
		
		entry->last_used = jiffies;
		memcpy(haddr, entry->ha, dev->addr_len);
		if (skb)
			skb->arp = 1;
		sti();
		return 0;
	}

	/*
	 *	Create a new unresolved entry.
	 */
	
	entry = (struct arp_table *) kmalloc(sizeof(struct arp_table),
					GFP_ATOMIC);
	if (entry != NULL)
	{
		entry->ip = paddr;
		entry->hlen = dev->addr_len;
		entry->htype = dev->type;
		entry->flags = 0;
		memset(entry->ha, 0, dev->addr_len);
		entry->dev = dev;
		entry->last_used = jiffies;
		init_timer(&entry->timer);
		entry->timer.function = arp_expire_request;
		entry->timer.data = (unsigned long)entry;
		entry->timer.expires = ARP_RES_TIME;
		entry->next = arp_tables[hash];
		arp_tables[hash] = entry;
		add_timer(&entry->timer);
		entry->retries = ARP_MAX_TRIES;
		skb_queue_head_init(&entry->skb);
		if (skb != NULL)
			skb_queue_tail(&entry->skb, skb);
	}
	else
	{
		if (skb != NULL && skb->free)
			kfree_skb(skb, FREE_WRITE);
  	}
	sti();

	/*
	 *	If we didn't find an entry, we will try to send an ARP packet.
	 */
	
	arp_send(ARPOP_REQUEST, ETH_P_ARP, paddr, dev, saddr, NULL, 
		 dev->dev_addr);

	return 1;
}


/*
 *	Write the contents of the ARP cache to a PROCfs file.
 */

#define HBUFFERLEN 30

int arp_get_info(char *buffer, char **start, off_t offset, int length)
{
	int len=0;
	off_t begin=0;
	off_t pos=0;
	int size;
	struct arp_table *entry;
	char hbuffer[HBUFFERLEN];
	int i,j,k;
	const char hexbuf[] =  "0123456789ABCDEF";

	size = sprintf(buffer,"IP address       HW type     Flags       HW address\n");
	pos+=size;
	len+=size;
	  
	cli();
	for(i=0; i<ARP_TABLE_SIZE; i++)
	{
		for(entry=arp_tables[i]; entry!=NULL; entry=entry->next)
		{
/*
 *	Convert hardware address to XX:XX:XX:XX ... form.
 */
#ifdef CONFIG_AX25

			if(entry->htype==ARPHRD_AX25)
			     strcpy(hbuffer,ax2asc((ax25_address *)entry->ha));
			else {
#endif

			for(k=0,j=0;k<HBUFFERLEN-3 && j<entry->hlen;j++)
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
				"%-17s0x%-10x0x%-10x%s\n",
				in_ntoa(entry->ip),
				(unsigned int)entry->htype,
				entry->flags,
				hbuffer);
	
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
	}
	sti();
  
	*start=buffer+(offset-begin);	/* Start of wanted data */
	len-=(offset-begin);		/* Start slop */
	if(len>length)
		len=length;		        /* Ending slop */
	return len;
}


/*
 *	This will find an entry in the ARP table by looking at the IP address.
 *	Be careful, interrupts are turned off on exit!!!
 */

static struct arp_table *arp_lookup(unsigned long paddr)
{
	struct arp_table *entry;
	unsigned long hash = HASH(paddr);

	cli();
	for (entry = arp_tables[hash]; entry != NULL; entry = entry->next)
		if (entry->ip == paddr) break;
	return entry;
}


/*
 *	Set (create) an ARP cache entry.
 */

static int arp_req_set(struct arpreq *req)
{
	struct arpreq r;
	struct arp_table *entry;
	struct sockaddr_in *si;
	int htype, hlen;
	unsigned long ip, hash;
	struct rtable *rt;

	memcpy_fromfs(&r, req, sizeof(r));

	/* We only understand about IP addresses... */
	if (r.arp_pa.sa_family != AF_INET)
		return -EPFNOSUPPORT;

	/*
	 * Find out about the hardware type.
	 * We have to be compatible with BSD UNIX, so we have to
	 * assume that a "not set" value (i.e. 0) means Ethernet.
	 */
	
	switch (r.arp_ha.sa_family) {
		case ARPHRD_ETHER:
			htype = ARPHRD_ETHER;
			hlen = ETH_ALEN;
			break;
#ifdef CONFIG_AX25
		case ARPHRD_AX25:
			htype = ARPHRD_AX25;
			hlen = 7;
			break;
#endif
		default:
			return -EPFNOSUPPORT;
	}

	si = (struct sockaddr_in *) &r.arp_pa;
	ip = si->sin_addr.s_addr;
	if (ip == 0)
	{
		printk("ARP: SETARP: requested PA is 0.0.0.0 !\n");
		return -EINVAL;
	}

	/*
	 *	Is it reachable directly ?
	 */

	rt = ip_rt_route(ip, NULL, NULL);
	if (rt == NULL)
		return -ENETUNREACH;

	/*
	 *	Is there an existing entry for this address?
	 */
	
	hash = HASH(ip);
	cli();

	/*
	 *	Find the entry
	 */
	for (entry = arp_tables[hash]; entry != NULL; entry = entry->next)
		if (entry->ip == ip)
			break;

	/*
	 *	Do we need to create a new entry
	 */
	
	if (entry == NULL)
	{
		entry = (struct arp_table *) kmalloc(sizeof(struct arp_table),
					GFP_ATOMIC);
		if (entry == NULL)
		{
			sti();
			return -ENOMEM;
		}
		entry->ip = ip;
		entry->hlen = hlen;
		entry->htype = htype;
		init_timer(&entry->timer);
		entry->next = arp_tables[hash];
		arp_tables[hash] = entry;
		skb_queue_head_init(&entry->skb);
	}
	else
		if (entry->flags & ATF_PUBL)
			proxies--;
	/*
	 *	We now have a pointer to an ARP entry.  Update it!
	 */
	
	memcpy(&entry->ha, &r.arp_ha.sa_data, hlen);
	entry->last_used = jiffies;
	entry->flags = r.arp_flags | ATF_COM;
	if (entry->flags & ATF_PUBL)
		proxies++;
	entry->dev = rt->rt_dev;
	sti();

	return 0;
}


/*
 *	Get an ARP cache entry.
 */

static int arp_req_get(struct arpreq *req)
{
	struct arpreq r;
	struct arp_table *entry;
	struct sockaddr_in *si;

	/*
	 *	We only understand about IP addresses...
	 */
	
	memcpy_fromfs(&r, req, sizeof(r));

	if (r.arp_pa.sa_family != AF_INET)
		return -EPFNOSUPPORT;

	/*
	 *	Is there an existing entry for this address?
	 */
	
	si = (struct sockaddr_in *) &r.arp_pa;
	entry = arp_lookup(si->sin_addr.s_addr);

	if (entry == NULL)
	{
		sti();
		return -ENXIO;
	}

	/*
	 *	We found it; copy into structure.
	 */
	
	memcpy(r.arp_ha.sa_data, &entry->ha, entry->hlen);
	r.arp_ha.sa_family = entry->htype;
	r.arp_flags = entry->flags;
	sti();

	/*
	 *	Copy the information back
	 */
	
	memcpy_tofs(req, &r, sizeof(r));
	return 0;
}


/*
 *	Handle an ARP layer I/O control request.
 */

int arp_ioctl(unsigned int cmd, void *arg)
{
	struct arpreq r;
	struct sockaddr_in *si;
	int err;

	switch(cmd)
	{
		case SIOCDARP:
			if (!suser())
				return -EPERM;
			err = verify_area(VERIFY_READ, arg, sizeof(struct arpreq));
			if(err)
				return err;
			memcpy_fromfs(&r, arg, sizeof(r));
			if (r.arp_pa.sa_family != AF_INET)
				return -EPFNOSUPPORT;
			si = (struct sockaddr_in *) &r.arp_pa;
			arp_destroy(si->sin_addr.s_addr, 1);
			return 0;
		case SIOCGARP:
			err = verify_area(VERIFY_WRITE, arg, sizeof(struct arpreq));
			if(err)
				return err;
			return arp_req_get((struct arpreq *)arg);
		case SIOCSARP:
			if (!suser())
				return -EPERM;
			err = verify_area(VERIFY_READ, arg, sizeof(struct arpreq));
			if(err)
				return err;
			return arp_req_set((struct arpreq *)arg);
		default:
			return -EINVAL;
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
	0,		/* copy */
	arp_rcv,
	NULL,
	NULL
};

void arp_init (void)
{
	/* Register the packet type */
	arp_packet_type.type=htons(ETH_P_ARP);
	dev_add_pack(&arp_packet_type);
	/* Start with the regular checks for expired arp entries. */
	add_timer(&arp_timer);
}

