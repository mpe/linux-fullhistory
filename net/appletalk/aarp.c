/*
 *	AARP:		An implementation of the Appletalk aarp protocol for
 *			ethernet 'ELAP'.
 *
 *		Alan Cox  <Alan.Cox@linux.org>
 *			  <alan@cymru.net>
 *
 *	This doesn't fit cleanly with the IP arp. This isn't a problem as
 *	the IP arp wants extracting from the device layer in 1.3.x anyway.
 *	[see the pre-1.3 test code for details 8)]
 *
 *	FIXME:
 *		We ought to handle the retransmits with a single list and a 
 *	separate fast timer for when it is needed.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *
 *	References:
 *		Inside Appletalk (2nd Ed).
 */
 
#include <asm/segment.h>
#include <asm/system.h>
#include <asm/bitops.h>
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
#include <linux/notifier.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/datalink.h>
#include <net/psnap.h>
#include <linux/atalk.h>

/*
 *	Lists of aarp entries
 */
 
struct aarp_entry
{
	/* These first two are only used for unresolved entries */
	unsigned long last_sent;		/* Last time we xmitted the aarp request */
	struct sk_buff_head packet_queue;	/* Queue of frames wait for resolution */
	unsigned long expires_at;		/* Entry expiry time */
	struct at_addr target_addr;		/* DDP Address */
	struct device *dev;			/* Device to use */
	char hwaddr[6];				/* Physical i/f address of target/router */
	unsigned short xmit_count;		/* When this hits 10 we give up */
	struct aarp_entry *next;		/* Next entry in chain */
};


/*
 *	Hashed list of resolved and unresolved entries
 */

static struct aarp_entry *resolved[AARP_HASH_SIZE], *unresolved[AARP_HASH_SIZE];
static int unresolved_count=0;

/*
 *	Used to walk the list and purge/kick entries.
 */
 
static struct timer_list aarp_timer;

/*
 *	Delete an aarp queue
 */

static void aarp_expire(struct aarp_entry *a)
{
	struct sk_buff *skb;
	
	while((skb=skb_dequeue(&a->packet_queue))!=NULL)
		kfree_skb(skb, FREE_WRITE);
	kfree_s(a,sizeof(*a));
}

/*
 *	Send an aarp queue entry request
 */
 
static void aarp_send_query(struct aarp_entry *a)
{
	static char aarp_eth_multicast[ETH_ALEN]={ 0x09, 0x00, 0x07, 0xFF, 0xFF, 0xFF };
	struct device *dev=a->dev;
	int len=dev->hard_header_len+sizeof(struct elapaarp)+aarp_dl->header_length;
	struct sk_buff *skb=alloc_skb(len, GFP_ATOMIC);
	struct elapaarp *eah;
	struct at_addr *sat=atalk_find_dev_addr(dev);
	
	if(skb==NULL || sat==NULL)
		return;
	
	/*
	 *	Set up the buffer.
	 */		

	skb_reserve(skb,dev->hard_header_len+aarp_dl->header_length);
	eah		=	(struct elapaarp *)skb_put(skb,sizeof(struct elapaarp));
	skb->arp	=	1;
	skb->free	=	1;
	skb->dev	=	a->dev;
	
	/*
	 *	Set up the ARP.
	 */
	 
	eah->hw_type	=	htons(AARP_HW_TYPE_ETHERNET);
	eah->pa_type	=	htons(ETH_P_ATALK);
	eah->hw_len	=	ETH_ALEN;	
	eah->pa_len	=	AARP_PA_ALEN;
	eah->function	=	htons(AARP_REQUEST);
	
	memcpy(eah->hw_src, dev->dev_addr, ETH_ALEN);
	
	eah->pa_src_zero=	0;
	eah->pa_src_net	=	sat->s_net;
	eah->pa_src_node=	sat->s_node;
	
	memset(eah->hw_dst, '\0', ETH_ALEN);
	
	eah->pa_dst_zero=	0;
	eah->pa_dst_net	=	a->target_addr.s_net;
	eah->pa_dst_node=	a->target_addr.s_node;
	
	/*
	 *	Add ELAP headers and set target to the AARP multicast.
	 */
	 
	aarp_dl->datalink_header(aarp_dl, skb, aarp_eth_multicast);	

	/*
	 *	Send it.
	 */	
	 
	dev_queue_xmit(skb, dev, SOPRI_NORMAL);
	
	/*
	 *	Update the sending count
	 */
	 
	a->xmit_count++;
}

static void aarp_send_reply(struct device *dev, struct at_addr *us, struct at_addr *them, unsigned char *sha)
{
	int len=dev->hard_header_len+sizeof(struct elapaarp)+aarp_dl->header_length;
	struct sk_buff *skb=alloc_skb(len, GFP_ATOMIC);
	struct elapaarp *eah;
	
	if(skb==NULL)
		return;
	
	/*
	 *	Set up the buffer.
	 */		

	skb_reserve(skb,dev->hard_header_len+aarp_dl->header_length);
	eah		=	(struct elapaarp *)skb_put(skb,sizeof(struct elapaarp));	 
	skb->arp	=	1;
	skb->free	=	1;
	skb->dev	=	dev;
	
	/*
	 *	Set up the ARP.
	 */
	 
	eah->hw_type	=	htons(AARP_HW_TYPE_ETHERNET);
	eah->pa_type	=	htons(ETH_P_ATALK);
	eah->hw_len	=	ETH_ALEN;	
	eah->pa_len	=	AARP_PA_ALEN;
	eah->function	=	htons(AARP_REPLY);
	
	memcpy(eah->hw_src, dev->dev_addr, ETH_ALEN);
	
	eah->pa_src_zero=	0;
	eah->pa_src_net	=	us->s_net;
	eah->pa_src_node=	us->s_node;
	
	if(sha==NULL)
		memset(eah->hw_dst, '\0', ETH_ALEN);
	else
		memcpy(eah->hw_dst, sha, ETH_ALEN);
	
	eah->pa_dst_zero=	0;
	eah->pa_dst_net	=	them->s_net;
	eah->pa_dst_node=	them->s_node;
	
	/*
	 *	Add ELAP headers and set target to the AARP multicast.
	 */
	 
	aarp_dl->datalink_header(aarp_dl, skb, sha);	

	/*
	 *	Send it.
	 */	
	 
	dev_queue_xmit(skb, dev, SOPRI_NORMAL);
	
}

/*
 *	Send probe frames. Called from atif_probe_device.
 */
 
void aarp_send_probe(struct device *dev, struct at_addr *us)
{
	int len=dev->hard_header_len+sizeof(struct elapaarp)+aarp_dl->header_length;
	struct sk_buff *skb=alloc_skb(len, GFP_ATOMIC);
	struct elapaarp *eah;
	static char aarp_eth_multicast[ETH_ALEN]={ 0x09, 0x00, 0x07, 0xFF, 0xFF, 0xFF };
	
	if(skb==NULL)
		return;
	
	/*
	 *	Set up the buffer.
	 */		

	skb_reserve(skb,dev->hard_header_len+aarp_dl->header_length);
	eah		=	(struct elapaarp *)skb_put(skb,sizeof(struct elapaarp));
	
	skb->arp	=	1;
	skb->free	=	1;
	skb->dev	=	dev;
	
	/*
	 *	Set up the ARP.
	 */
	 
	eah->hw_type	=	htons(AARP_HW_TYPE_ETHERNET);
	eah->pa_type	=	htons(ETH_P_ATALK);
	eah->hw_len	=	ETH_ALEN;	
	eah->pa_len	=	AARP_PA_ALEN;
	eah->function	=	htons(AARP_PROBE);
	
	memcpy(eah->hw_src, dev->dev_addr, ETH_ALEN);
	
	eah->pa_src_zero=	0;
	eah->pa_src_net	=	us->s_net;
	eah->pa_src_node=	us->s_node;
	
	memset(eah->hw_dst, '\0', ETH_ALEN);
	
	eah->pa_dst_zero=	0;
	eah->pa_dst_net	=	us->s_net;
	eah->pa_dst_node=	us->s_node;
	
	/*
	 *	Add ELAP headers and set target to the AARP multicast.
	 */
	 
	aarp_dl->datalink_header(aarp_dl, skb, aarp_eth_multicast);	

	/*
	 *	Send it.
	 */	
	 
	dev_queue_xmit(skb, dev, SOPRI_NORMAL);
	
}
	
/*
 *	Handle an aarp timer expire
 */

static void aarp_expire_timer(struct aarp_entry **n)
{
	struct aarp_entry *t;
	while((*n)!=NULL)
	{
		/* Expired ? */
		if((*n)->expires_at < jiffies)
		{
			t= *n;
			*n=(*n)->next;
			aarp_expire(t);
		}
		else
			n=&((*n)->next);
	}
}

/*
 *	Kick all pending requests 5 times a second.
 */
 
static void aarp_kick(struct aarp_entry **n)
{
	struct aarp_entry *t;
	while((*n)!=NULL)
	{
		/* Expired - if this will be the 11th transmit, we delete
		   instead */
		if((*n)->xmit_count>=AARP_RETRANSMIT_LIMIT)
		{
			t= *n;
			*n=(*n)->next;
			aarp_expire(t);
		}
		else
		{
			aarp_send_query(*n);
			n=&((*n)->next);
		}
	}
}

/*
 *	A device has gone down. Take all entries referring to the device
 *	and remove them.
 */
 
static void aarp_expire_device(struct aarp_entry **n, struct device *dev)
{
	struct aarp_entry *t;
	while((*n)!=NULL)
	{
		if((*n)->dev==dev)
		{
			t= *n;
			*n=(*n)->next;
			aarp_expire(t);
		}
		else
			n=&((*n)->next);
	}
}
		
/*
 *	Handle the timer event 
 */
 
static void aarp_expire_timeout(unsigned long unused)
{
	int ct=0;
	for(ct=0;ct<AARP_HASH_SIZE;ct++)
	{
		aarp_expire_timer(&resolved[ct]);
		aarp_kick(&unresolved[ct]);
		aarp_expire_timer(&unresolved[ct]);
	}
	del_timer(&aarp_timer);
	if(unresolved_count==0)
		aarp_timer.expires=jiffies+AARP_EXPIRY_TIME;
	else
		aarp_timer.expires=jiffies+AARP_TICK_TIME;
	add_timer(&aarp_timer);
}

/*
 *	Network device notifier chain handler.
 */
 
static int aarp_device_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	int ct=0;
	if(event==NETDEV_DOWN)
	{
		for(ct=0;ct<AARP_HASH_SIZE;ct++)
		{
			aarp_expire_device(&resolved[ct],ptr);
			aarp_expire_device(&unresolved[ct],ptr);
		}
	}
	return NOTIFY_DONE;
}

/*
 *	Create a new aarp entry.
 */
 
static struct aarp_entry *aarp_alloc(void)
{
	struct aarp_entry *a=kmalloc(sizeof(struct aarp_entry), GFP_ATOMIC);
	if(a==NULL)
		return NULL;
	skb_queue_head_init(&a->packet_queue);
	return a;
}

/*
 *	Find an entry. We might return an expired but not yet purged entry. We
 *	don't care as it will do no harm.
 */
 
static struct aarp_entry *aarp_find_entry(struct aarp_entry *list, struct device *dev, struct at_addr *sat)
{
	unsigned long flags;
	save_flags(flags);
	cli();
	while(list)
	{
		if(list->target_addr.s_net==sat->s_net &&
		   list->target_addr.s_node==sat->s_node && list->dev==dev)
			break;
		list=list->next;
	}
	restore_flags(flags);
	return list;
}

/*
 *	Send a DDP frame
 */
 
int aarp_send_ddp(struct device *dev,struct sk_buff *skb, struct at_addr *sa, void *hwaddr)
{
	static char ddp_eth_multicast[ETH_ALEN]={ 0x09, 0x00, 0x07, 0xFF, 0xFF, 0xFF };
	int hash;
	struct aarp_entry *a;
	unsigned long flags;
	
	/*
	 *	Check for localtalk first
	 */
	 
	if(dev->type==ARPHRD_LOCALTLK)
	{
		struct at_addr *at=atalk_find_dev_addr(dev);
		struct ddpehdr *ddp=(struct ddpehdr *)skb->data;
		int ft=2;
		
		/*
		 *	Compressible ?
		 * 
		 *	IFF: src_net==dest_net==device_net
		 */
		 
		if(at->s_net==sa->s_net && sa->s_net==ddp->deh_snet)
		{
			skb_pull(skb,sizeof(struct ddpehdr)-4);
			/*
			 *	The upper two remaining bytes are the port 
			 *	numbers	we just happen to need. Now put the 
			 *	length in the lower two.
			 */
			*((__u16 *)skb->data)=htons(skb->len);
			ft=1;
		}
		/*
		 *	Nice and easy. No AARP type protocols occur here
		 *	so we can just shovel it out with a 3 byte LLAP header
		 */
		 
		skb_push(skb,3);
		skb->data[0]=sa->s_node;
		skb->data[1]=at->s_node;
		skb->data[2]=ft;
		 
		if(skb->sk==NULL)
			dev_queue_xmit(skb, skb->dev, SOPRI_NORMAL);
		else
			dev_queue_xmit(skb, skb->dev, skb->sk->priority);
		return 1;
	}	
	 
	/*
	 *	Non ELAP we cannot do.
	 */

	if(dev->type!=ARPHRD_ETHER)
	{
		return -1;
	}

	skb->dev = dev;
	skb->protocol = htons(ETH_P_ATALK);
			
	hash=sa->s_node%(AARP_HASH_SIZE-1);
	save_flags(flags);
	cli();
	
	/*
	 *	Do we have a resolved entry ?
	 */
	 
	if(sa->s_node==ATADDR_BCAST)
	{
		ddp_dl->datalink_header(ddp_dl, skb, ddp_eth_multicast);
		if(skb->sk==NULL)
			dev_queue_xmit(skb, skb->dev, SOPRI_NORMAL);
		else
			dev_queue_xmit(skb, skb->dev, skb->sk->priority);
		restore_flags(flags);
		return 1;
	}
	a=aarp_find_entry(resolved[hash],dev,sa);
	if(a!=NULL)
	{
		/*
		 *	Return 1 and fill in the address
		 */

		a->expires_at=jiffies+AARP_EXPIRY_TIME*10;
		ddp_dl->datalink_header(ddp_dl, skb, a->hwaddr);
		if(skb->sk==NULL)
			dev_queue_xmit(skb, skb->dev, SOPRI_NORMAL);
		else
			dev_queue_xmit(skb, skb->dev, skb->sk->priority);
		restore_flags(flags);
		return 1;
	}

	/*
	 *	Do we have an unresolved entry: This is the less common path
	 */

	a=aarp_find_entry(unresolved[hash],dev,sa);
	if(a!=NULL)
	{
		/*
		 *	Queue onto the unresolved queue
		 */

		skb_queue_tail(&a->packet_queue, skb);
		restore_flags(flags);
		return 0;
	}

	/*
	 *	Allocate a new entry
	 */

	a=aarp_alloc();
	if(a==NULL)
	{
		/*
		 *	Whoops slipped... good job it's an unreliable 
		 *	protocol 8)	
		 */
		restore_flags(flags);
		return -1;
	}

	/*
	 *	Set up the queue
	 */

	skb_queue_tail(&a->packet_queue, skb);
	a->expires_at=jiffies+AARP_RESOLVE_TIME;
	a->dev=dev;
	a->next=unresolved[hash];
	a->target_addr= *sa;
	a->xmit_count=0;
	unresolved[hash]=a;
	unresolved_count++;
	restore_flags(flags);

	/*
	 *	Send an initial request for the address
	 */

	aarp_send_query(a);

	/*
	 *	Switch to fast timer if needed (That is if this is the
	 *	first unresolved entry to get added)
	 */

	if(unresolved_count==1)
	{
		del_timer(&aarp_timer);
		aarp_timer.expires=jiffies+AARP_TICK_TIME;
		add_timer(&aarp_timer);
	}

	/*
	 *	Tell the ddp layer we have taken over for this frame.
	 */

	return 0;
}

/*
 *	An entry in the aarp unresolved queue has become resolved. Send
 *	all the frames queued under it.
 */
 
static void aarp_resolved(struct aarp_entry **list, struct aarp_entry *a, int hash)
{
	struct sk_buff *skb;
	while(*list!=NULL)
	{
		if(*list==a)
		{
			unresolved_count--;
			*list=a->next;
			
			/* 
			 *	Move into the resolved list 
			 */
			 
			a->next=resolved[hash];
			resolved[hash]=a;
			
			/*
			 *	Kick frames off 
			 */
			 
			while((skb=skb_dequeue(&a->packet_queue))!=NULL)
			{
				a->expires_at=jiffies+AARP_EXPIRY_TIME*10;
				ddp_dl->datalink_header(ddp_dl,skb,a->hwaddr);
				if(skb->sk==NULL)
					dev_queue_xmit(skb, skb->dev, SOPRI_NORMAL);
				else
					dev_queue_xmit(skb, skb->dev, skb->sk->priority);
			}
		}
		else
			list=&((*list)->next);
	}
}

/*
 *	This is called by the SNAP driver whenever we see an AARP SNAP
 *	frame. We currently only support ethernet.
 */
 
static int aarp_rcv(struct sk_buff *skb, struct device *dev, struct packet_type *pt)
{
	struct elapaarp *ea=(struct elapaarp *)skb->h.raw;
	struct aarp_entry *a;
	struct at_addr sa, *ma;
	unsigned long flags;
	int hash;
	struct atalk_iface *ifa;
	
	
	/*
	 *	We only do ethernet SNAP AARP
	 */
	 
	if(dev->type!=ARPHRD_ETHER)
	{
		kfree_skb(skb, FREE_READ);
		return 0;
	}
	
	/*
	 *	Frame size ok ?
	 */
	 
	if(!skb_pull(skb,sizeof(*ea)))
	{
		kfree_skb(skb, FREE_READ);
		return 0;
	}

	ea->function=ntohs(ea->function);
	
	/*
	 *	Sanity check fields.
	 */
	 
	if(ea->function<AARP_REQUEST || ea->function > AARP_PROBE || ea->hw_len != ETH_ALEN || ea->pa_len != AARP_PA_ALEN ||
		ea->pa_src_zero != 0 || ea->pa_dst_zero != 0)
	{
		kfree_skb(skb, FREE_READ);
		return 0;
	}
	
	/*
	 *	Looks good
	 */
	
	hash=ea->pa_src_node%(AARP_HASH_SIZE-1);

	/*
	 *	Build an address
	 */
	 
	sa.s_node=ea->pa_src_node;
	sa.s_net=ea->pa_src_net;
	
	/*
	 *	Process the packet
	 */
	 
	save_flags(flags);

	/*
	 *	Check for replies of me
	 */
			
	ifa=atalk_find_dev(dev);
	if(ifa==NULL)
	{
		restore_flags(flags);
		kfree_skb(skb, FREE_READ);
		return 1;		
	}
	if(ifa->status&ATIF_PROBE)
	{			
		if(ifa->address.s_node==ea->pa_dst_node && ifa->address.s_net==ea->pa_dst_net)
		{
			/*
			 *	Fail the probe (in use)
			 */
			 
			ifa->status|=ATIF_PROBE_FAIL;
			restore_flags(flags);
			kfree_skb(skb, FREE_READ);
			return 1;		
		}
	}				 
	
	switch(ea->function)
	{
		case AARP_REPLY:	
			if(unresolved_count==0)	/* Speed up */
				break;
			/*
			 *	Find the entry	
			 */
			 
			cli();
			if((a=aarp_find_entry(unresolved[hash],dev,&sa))==NULL || dev != a->dev)
				break;
			/*
			 *	We can fill one in - this is good
			 */
			 
			memcpy(a->hwaddr,ea->hw_src,ETH_ALEN);
			aarp_resolved(&unresolved[hash],a,hash);
			if(unresolved_count==0)
			{
				del_timer(&aarp_timer);
				aarp_timer.expires=jiffies+AARP_EXPIRY_TIME;
				add_timer(&aarp_timer);
			}
			break;
			
		case AARP_REQUEST:
		case AARP_PROBE:
			/*
			 *	If it is my address set ma to my address and reply. We can treat probe and
			 *	request the same. Probe simply means we shouldn't cache the querying host, 
			 *	as in a probe they are proposing an address not using one.
			 */
			 
			ma=&ifa->address;
			sa.s_node=ea->pa_dst_node;
			sa.s_net=ea->pa_dst_net;
			
			if(sa.s_node!=ma->s_node)
				break;
			if(sa.s_net && ma->s_net && sa.s_net!=ma->s_net)
				break;

			sa.s_node=ea->pa_src_node;
			sa.s_net=ea->pa_src_net;
			
			/*
			 *	aarp_my_address has found the address to use for us.
			 */
			 
			aarp_send_reply(dev,ma,&sa,ea->hw_src);
			break;
	}
	restore_flags(flags);
	kfree_skb(skb, FREE_READ);
	return 1;		
}

static struct notifier_block aarp_notifier={
	aarp_device_event,
	NULL,
	0
};

static char aarp_snap_id[]={0x00,0x00,0x00,0x80,0xF3};


void aarp_proto_init(void)
{
	if((aarp_dl=register_snap_client(aarp_snap_id, aarp_rcv))==NULL)
		printk(KERN_CRIT "Unable to register AARP with SNAP.\n");
	init_timer(&aarp_timer);
	aarp_timer.function=aarp_expire_timeout;
	aarp_timer.data=0;
	aarp_timer.expires=jiffies+AARP_EXPIRY_TIME;
	add_timer(&aarp_timer);
	register_netdevice_notifier(&aarp_notifier);
}


#ifdef MODULE

/* Free all the entries in an aarp list. Caller should turn off interrupts. */
static void free_entry_list(struct aarp_entry *list)
{
	struct aarp_entry *tmp;

	while (list != NULL)
	{
		tmp = list->next;
		aarp_expire(list);
		list = tmp;
	}
}

/* General module cleanup. Called from cleanup_module() in ddp.c. */
void aarp_cleanup_module(void)
{
	unsigned long flags;
	int i;

	save_flags(flags);
	cli();

	del_timer(&aarp_timer);
	unregister_netdevice_notifier(&aarp_notifier);
	unregister_snap_client(aarp_snap_id);

	for (i = 0; i < AARP_HASH_SIZE; i++)
	{
		free_entry_list(resolved[i]);
		free_entry_list(unresolved[i]);
	}

	restore_flags(flags);
}

#endif  /* MODULE */
