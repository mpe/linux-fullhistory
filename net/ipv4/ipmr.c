/*
 *	IP multicast routing support for mrouted 3.6/3.8
 *
 *		(c) 1995 Alan Cox, <alan@cymru.net>
 *	  Linux Consultancy and Custom Driver Development
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *
 *	Fixes:
 *	Michael Chastain	:	Incorrect size of copying.
 *	Alan Cox		:	Added the cache manager code
 *	Alan Cox		:	Fixed the clone/copy bug and device race.
 *	Malcolm Beattie		:	Buffer handling fixes.
 *	Alexey Kuznetsov	:	Double buffer free and other fixes.
 *	SVR Anand		:	Fixed several multicast bugs and problems.
 *	Alexey Kuznetsov	:	Status, optimisations and more.
 *	Brad Parker		:	Better behaviour on mrouted upcall
 *					overflow.
 *
 *	Status:
 *		Cache manager under test. Forwarding in vague test mode
 *	Todo:
 *		Flow control
 *		Finish Tunnels
 *		Debug cache ttl handling properly
 *		Resolve IFF_ALLMULTI for rest of cards
 */

#include <linux/config.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/proc_fs.h>
#include <linux/mroute.h>
#include <linux/init.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/icmp.h>
#include <net/udp.h>
#include <linux/notifier.h>
#include <net/checksum.h>

/*
 *	Multicast router control variables
 */

static struct vif_device vif_table[MAXVIFS];		/* Devices 		*/
static unsigned long vifc_map;				/* Active device map	*/
static int maxvif;
int mroute_do_pim = 0;					/* Set in PIM assert	*/
static struct mfc_cache *mfc_cache_array[MFC_LINES];	/* Forwarding cache	*/
int cache_resolve_queue_len = 0;			/* Size of unresolved	*/

/*
 *	Delete a VIF entry
 */
 
static int vif_delete(int vifi)
{
	struct vif_device *v;
	
	if (vifi < 0 || vifi >= maxvif || !(vifc_map&(1<<vifi)))
		return -EADDRNOTAVAIL;

	v = &vif_table[vifi];

	start_bh_atomic();

	if (!(v->flags&VIFF_TUNNEL)) {
		v->u.dev->flags &= ~IFF_ALLMULTI;
		dev_mc_upload(v->u.dev);
		ip_rt_multicast_event(v->u.dev);
		v->u.dev = NULL;
	} else {
		ip_rt_put(v->u.rt);
		v->u.rt = NULL;
	}

	vifc_map&=~(1<<vifi);

	end_bh_atomic();

	if (vifi+1 == maxvif) {
		int tmp;
		for (tmp=vifi-1; tmp>=0; tmp--) {
			if (vifc_map&(1<<tmp))
				break;
		}
		maxvif = tmp+1;
	}
	return 0;
}

static void ipmr_set_bounds(struct mfc_cache *cache)
{
	int vifi;
	for (vifi=0; vifi<maxvif; vifi++) {
		if (vifc_map&(1<<vifi) && cache->mfc_ttls[vifi]) {
			cache->mfc_minvif = vifi;
			cache->mfc_maxvif = vifi+1;
			vifi++;
			break;
		}
	}
	for ( ; vifi<maxvif; vifi++) {
		if (vifc_map&(1<<vifi) && cache->mfc_ttls[vifi])
			cache->mfc_maxvif = vifi+1;
	}
}

/*
 *	Delete a multicast route cache entry
 */
 
static void ipmr_cache_delete(struct mfc_cache *cache)
{
	struct sk_buff *skb;
	int line;
	struct mfc_cache **cp;
	
	/*
	 *	Find the right cache line
	 */

	line=MFC_HASH(cache->mfc_mcastgrp,cache->mfc_origin);
	cp=&(mfc_cache_array[line]);

	if(cache->mfc_flags&MFC_QUEUED)
		del_timer(&cache->mfc_timer);
	
	/*
	 *	Unlink the buffer
	 */
	
	while(*cp!=NULL)
	{
		if(*cp==cache)
		{
			*cp=cache->next;
			break;
		}
		cp=&((*cp)->next);
	}
	
	/*
	 *	Free the buffer. If it is a pending resolution
	 *	clean up the other resources.
	 */

	if(cache->mfc_flags&MFC_QUEUED)
	{
		cache_resolve_queue_len--;
		while((skb=skb_dequeue(&cache->mfc_unresolved)))
			kfree_skb(skb, FREE_WRITE);
	}
	kfree_s(cache,sizeof(cache));
}

/*
 *	Cache expiry timer
 */	
 
static void ipmr_cache_timer(unsigned long data)
{
	struct mfc_cache *cache=(struct mfc_cache *)data;
	ipmr_cache_delete(cache);
}

/*
 *	Insert a multicast cache entry
 */

static void ipmr_cache_insert(struct mfc_cache *c)
{
	int line=MFC_HASH(c->mfc_mcastgrp,c->mfc_origin);
	c->next=mfc_cache_array[line];
	mfc_cache_array[line]=c;
}
 
/*
 *	Find a multicast cache entry
 */
 
struct mfc_cache *ipmr_cache_find(__u32 origin, __u32 mcastgrp)
{
	int line=MFC_HASH(mcastgrp,origin);
	struct mfc_cache *cache;

	cache=mfc_cache_array[line];
	while(cache!=NULL)
	{
		if(cache->mfc_origin==origin && cache->mfc_mcastgrp==mcastgrp)
			return cache;
		cache=cache->next;
	}
	return NULL;
}

/*
 *	Allocate a multicast cache entry
 */
 
static struct mfc_cache *ipmr_cache_alloc(int priority)
{
	struct mfc_cache *c=(struct mfc_cache *)kmalloc(sizeof(struct mfc_cache), priority);
	if(c==NULL)
		return NULL;
	c->mfc_queuelen=0;
	skb_queue_head_init(&c->mfc_unresolved);
	init_timer(&c->mfc_timer);
	c->mfc_timer.data=(long)c;
	c->mfc_timer.function=ipmr_cache_timer;
	c->mfc_last_assert=0;
	c->mfc_minvif = MAXVIFS;
	c->mfc_maxvif = 0;
	return c;
}
 
/*
 *	A cache entry has gone into a resolved state from queued
 */
 
static void ipmr_cache_resolve(struct mfc_cache *cache)
{
	struct sk_buff *skb;

	start_bh_atomic();

	/*
	 *	Kill the queue entry timer.
	 */

	del_timer(&cache->mfc_timer);

	if (cache->mfc_flags&MFC_QUEUED) {
		cache->mfc_flags&=~MFC_QUEUED;
		cache_resolve_queue_len--;
	}

	end_bh_atomic();

	/*
	 *	Play the pending entries through our router
	 */
	while((skb=skb_dequeue(&cache->mfc_unresolved)))
		ip_mr_input(skb);
}

/*
 *	Bounce a cache query up to mrouted. We could use netlink for this but mrouted
 *	expects the following bizarre scheme..
 */
 
static int ipmr_cache_report(struct sk_buff *pkt, vifi_t vifi, int assert)
{
	struct sk_buff *skb = alloc_skb(128, GFP_ATOMIC);
	int ihl = pkt->nh.iph->ihl<<2;
	struct igmphdr *igmp;
	struct igmpmsg *msg;
	int ret;

	if(!skb)
		return -ENOMEM;
		
	/*
	 *	Copy the IP header
	 */

	skb->nh.iph = (struct iphdr *)skb_put(skb, ihl);
	memcpy(skb->data,pkt->data,ihl);
	skb->nh.iph->protocol = 0;			/* Flag to the kernel this is a route add */
	msg = (struct igmpmsg*)skb->nh.iph;
	if (assert)
		msg->im_vif = vifi;
	
	/*
	 *	Add our header
	 */
	
	igmp=(struct igmphdr *)skb_put(skb,sizeof(struct igmphdr));
	igmp->type	=
	msg->im_msgtype = assert ? IGMPMSG_WRONGVIF : IGMPMSG_NOCACHE;
	igmp->code 	=	0;
	skb->nh.iph->tot_len=htons(skb->len);			/* Fix the length */
	skb->h.raw = skb->nh.raw;
	
	/*
	 *	Deliver to mrouted
	 */
	if((ret=sock_queue_rcv_skb(mroute_socket,skb))<0)
	{
		static unsigned long last_warn;
		if(jiffies-last_warn>10*HZ)
		{
			last_warn=jiffies;
			printk("mroute: pending queue full, dropping entries.\n");
		}
		kfree_skb(skb, FREE_READ);
		return ret;
	}

	return ret;
}

/*
 *	Queue a packet for resolution
 */
 
static void ipmr_cache_unresolved(struct mfc_cache *cache, vifi_t vifi, struct sk_buff *skb)
{
	if(cache==NULL)
	{	
		/*
		 *	Create a new entry if allowable
		 */
		if(cache_resolve_queue_len>=10 || (cache=ipmr_cache_alloc(GFP_ATOMIC))==NULL)
		{
			kfree_skb(skb, FREE_WRITE);
			return;
		}
		/*
		 *	Fill in the new cache entry
		 */
		cache->mfc_parent=vifi;
		cache->mfc_origin=skb->nh.iph->saddr;
		cache->mfc_mcastgrp=skb->nh.iph->daddr;
		cache->mfc_flags=MFC_QUEUED;
		/*
		 *	Link to the unresolved list
		 */
		ipmr_cache_insert(cache);
		cache_resolve_queue_len++;
		/*
		 *	Fire off the expiry timer
		 */
		cache->mfc_timer.expires=jiffies+10*HZ;
		add_timer(&cache->mfc_timer);
		/*
		 *	Reflect first query at mrouted.
		 */
		if(mroute_socket)
		{
			/* If the report failed throw the cache entry 
			   out - Brad Parker */
			if(ipmr_cache_report(skb, vifi, 0)<0)
				ipmr_cache_delete(cache);
		}
	}
	/*
	 *	See if we can append the packet
	 */
	if(cache->mfc_queuelen>3)
	{
		kfree_skb(skb, FREE_WRITE);
		return;
	}
	cache->mfc_queuelen++;
	skb_queue_tail(&cache->mfc_unresolved,skb);
}

/*
 *	MFC cache manipulation by user space mroute daemon
 */
 
int ipmr_mfc_modify(int action, struct mfcctl *mfc)
{
	struct mfc_cache *cache;

	if(!MULTICAST(mfc->mfcc_mcastgrp.s_addr))
		return -EINVAL;
	/*
	 *	Find the cache line
	 */
	
	start_bh_atomic();

	cache=ipmr_cache_find(mfc->mfcc_origin.s_addr,mfc->mfcc_mcastgrp.s_addr);
	
	/*
	 *	Delete an entry
	 */
	if(action==MRT_DEL_MFC)
	{
		if(cache)
		{
			ipmr_cache_delete(cache);
			end_bh_atomic();
			return 0;
		}
		end_bh_atomic();
		return -ENOENT;
	}
	if(cache)
	{

		/*
		 *	Update the cache, see if it frees a pending queue
		 */

		cache->mfc_flags|=MFC_RESOLVED;
		cache->mfc_parent=mfc->mfcc_parent;
		memcpy(cache->mfc_ttls, mfc->mfcc_ttls,sizeof(cache->mfc_ttls));
		ipmr_set_bounds(cache);
		 
		/*
		 *	Check to see if we resolved a queued list. If so we
		 *	need to send on the frames and tidy up.
		 */
		 
		if(cache->mfc_flags&MFC_QUEUED)
			ipmr_cache_resolve(cache);	/* Unhook & send the frames */
		end_bh_atomic();
		return 0;
	}

	/*
	 *	Unsolicited update - that's ok, add anyway.
	 */
	 
	
	cache=ipmr_cache_alloc(GFP_ATOMIC);
	if(cache==NULL)
	{
		end_bh_atomic();
		return -ENOMEM;
	}
	cache->mfc_flags=MFC_RESOLVED;
	cache->mfc_origin=mfc->mfcc_origin.s_addr;
	cache->mfc_mcastgrp=mfc->mfcc_mcastgrp.s_addr;
	cache->mfc_parent=mfc->mfcc_parent;
	memcpy(cache->mfc_ttls, mfc->mfcc_ttls,sizeof(cache->mfc_ttls));
	ipmr_set_bounds(cache);
	ipmr_cache_insert(cache);
	end_bh_atomic();
	return 0;
}
 
/*
 *	Socket options and virtual interface manipulation. The whole
 *	virtual interface system is a complete heap, but unfortunately
 *	that's how BSD mrouted happens to think. Maybe one day with a proper
 *	MOSPF/PIM router set up we can clean this up.
 */
 
int ip_mroute_setsockopt(struct sock *sk,int optname,char *optval,int optlen)
{
	int err;
	struct vifctl vif;
	struct mfcctl mfc;
	
	if(optname!=MRT_INIT)
	{
		if(sk!=mroute_socket)
			return -EACCES;
	}
	
	switch(optname)
	{
		case MRT_INIT:
			if(sk->type!=SOCK_RAW || sk->num!=IPPROTO_IGMP)
				return -EOPNOTSUPP;
			if(optlen!=sizeof(int))
				return -ENOPROTOOPT;
			{
				int opt;
				err = get_user(opt,(int *)optval);
				if (err) 
					return err; 
				if (opt != 1)
					return -ENOPROTOOPT;
			}
			if(mroute_socket)
				return -EADDRINUSE;
			mroute_socket=sk;
			ipv4_config.multicast_route = 1;
			/* Initialise state */
			return 0;
		case MRT_DONE:
			ipv4_config.multicast_route = 0;
			mroute_close(sk);
			mroute_socket=NULL;
			return 0;
		case MRT_ADD_VIF:
		case MRT_DEL_VIF:
			if(optlen!=sizeof(vif))
				return -EINVAL;
			err = copy_from_user(&vif,optval,sizeof(vif));
			if (err)
				return -EFAULT; 
			if(vif.vifc_vifi > MAXVIFS)
				return -ENFILE;
			if(optname==MRT_ADD_VIF)
			{
				struct vif_device *v=&vif_table[vif.vifc_vifi];
				struct device *dev;
				/* Empty vif ? */
				if(vifc_map&(1<<vif.vifc_vifi))
					return -EADDRINUSE;
				/* Find the interface */
				dev=ip_dev_find(vif.vifc_lcl_addr.s_addr, NULL);
				if(!dev)
					return -EADDRNOTAVAIL;
				/* Must be tunnelled or multicastable */
				if(vif.vifc_flags&VIFF_TUNNEL)
				{
					if(vif.vifc_flags&VIFF_SRCRT)
						return -EOPNOTSUPP;
				}
				else
				{
					if(dev->flags&IFF_MULTICAST)
					{
						/* Most ethernet cards don't know
						   how to do this yet.. */
						dev->flags|=IFF_ALLMULTI;
						dev_mc_upload(dev);
						ip_rt_multicast_event(dev);
					}
					else
					{
						/* We are stuck.. */
						return -EOPNOTSUPP;
					}
				}
				/*
				 *	Fill in the VIF structures
				 */
				cli();
				v->rate_limit=vif.vifc_rate_limit;
				v->local=vif.vifc_lcl_addr.s_addr;
				v->remote=vif.vifc_rmt_addr.s_addr;
				v->flags=vif.vifc_flags;
				v->threshold=vif.vifc_threshold;
				v->u.dev=NULL;
				if (!(vif.vifc_flags&VIFF_TUNNEL))
					v->u.dev=dev;
				v->bytes_in = 0;
				v->bytes_out = 0;
				v->pkt_in = 0;
				v->pkt_out = 0;
				vifc_map|=(1<<vif.vifc_vifi);
				if (vif.vifc_vifi+1 > maxvif)
					maxvif = vif.vifc_vifi+1;
				sti();
				return 0;
			} else
				return vif_delete(vif.vifc_vifi);

		/*
		 *	Manipulate the forwarding caches. These live
		 *	in a sort of kernel/user symbiosis.
		 */
		case MRT_ADD_MFC:
		case MRT_DEL_MFC:
			if(optlen!=sizeof(mfc))
				return -EINVAL;
			err = copy_from_user(&mfc,optval, sizeof(mfc));
			return err ? -EFAULT : ipmr_mfc_modify(optname, &mfc);
		/*
		 *	Control PIM assert.
		 */
		case MRT_ASSERT:
		{
			int v;
			if(get_user(v,(int *)optval))
				return -EFAULT;
			mroute_do_pim=(v)?1:0;
			return 0;
		}
		/*
		 *	Spurious command, or MRT_VERSION which you cannot
		 *	set.
		 */
		default:
			return -ENOPROTOOPT;
	}
}

/*
 *	Getsock opt support for the multicast routing system.
 */
 
int ip_mroute_getsockopt(struct sock *sk,int optname,char *optval,int *optlen)
{
	int olr;
	int val;

	if(sk!=mroute_socket)
		return -EACCES;
	if(optname!=MRT_VERSION && optname!=MRT_ASSERT)
		return -ENOPROTOOPT;
	
	if(get_user(olr, optlen))
		return -EFAULT;

	olr=min(olr,sizeof(int));
	if(put_user(olr,optlen))
		return -EFAULT;
	if(optname==MRT_VERSION)
		val=0x0305;
	else
		val=mroute_do_pim;
	if(copy_to_user(optval,&val,olr))
		return -EFAULT;
	return 0;
}

/*
 *	The IP multicast ioctl support routines.
 */
 
int ipmr_ioctl(struct sock *sk, int cmd, unsigned long arg)
{
	int err;
	struct sioc_sg_req sr;
	struct sioc_vif_req vr;
	struct vif_device *vif;
	struct mfc_cache *c;
	
	switch(cmd)
	{
		case SIOCGETVIFCNT:
			err = copy_from_user(&vr,(void *)arg,sizeof(vr));
			if (err)
				return -EFAULT; 
			if(vr.vifi>=maxvif)
				return -EINVAL;
			vif=&vif_table[vr.vifi];
			if(vifc_map&(1<<vr.vifi))
			{
				vr.icount=vif->pkt_in;
				vr.ocount=vif->pkt_out;
				vr.ibytes=vif->bytes_in;
				vr.obytes=vif->bytes_out;
				err = copy_to_user((void *)arg,&vr,sizeof(vr));
				if (err)
					err = -EFAULT;
				return err;
				return 0;
			}
			return -EADDRNOTAVAIL;
		case SIOCGETSGCNT:
			err = copy_from_user(&sr,(void *)arg,sizeof(sr));
			if (err)
				return -EFAULT; 
			for (c = mfc_cache_array[MFC_HASH(sr.grp.s_addr, sr.src.s_addr)];
			     c; c = c->next) {
				if (sr.grp.s_addr == c->mfc_mcastgrp &&
				    sr.src.s_addr == c->mfc_origin) {
					sr.pktcnt = c->mfc_pkt;
					sr.bytecnt = c->mfc_bytes;
					sr.wrong_if = c->mfc_wrong_if;
					err = copy_to_user((void *)arg,&sr,sizeof(sr));
					if (err)
						err = -EFAULT;
					return err;
					return 0;
				}
			}
			return -EADDRNOTAVAIL;
		default:
			return -EINVAL;
	}
}

/*
 *	Close the multicast socket, and clear the vif tables etc
 */
 
void mroute_close(struct sock *sk)
{
	int i;
		
	/*
	 *	Shut down all active vif entries
	 */
	 
	for(i=0; i<maxvif; i++)
		vif_delete(i);

	/*
	 *	Wipe the cache
	 */
	for(i=0;i<MFC_LINES;i++)
	{
		start_bh_atomic();
		while(mfc_cache_array[i]!=NULL)
			ipmr_cache_delete(mfc_cache_array[i]);
		end_bh_atomic();
	}
}

static int ipmr_device_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct vif_device *v;
	int ct;
	if(event!=NETDEV_DOWN)
		return NOTIFY_DONE;
	v=&vif_table[0];
	for(ct=0;ct<maxvif;ct++)
	{
		if(vifc_map&(1<<ct) && !(v->flags&VIFF_TUNNEL) && v->u.dev==ptr)
			vif_delete(ct);
		v++;
	}
	return NOTIFY_DONE;
}


static struct notifier_block ip_mr_notifier={
	ipmr_device_event,
	NULL,
	0
};

/*
 * 	Encapsulate a packet by attaching a valid IPIP header to it.
 *	This avoids tunnel drivers and other mess and gives us the speed so
 *	important for multicast video.
 */
 
static void ip_encap(struct sk_buff *skb, u32 saddr, u32 daddr)
{
	struct iphdr *iph = (struct iphdr *)skb_push(skb,sizeof(struct iphdr));

	iph->version	= 	4;
	iph->tos	=	skb->nh.iph->tos;
	iph->ttl	=	skb->nh.iph->ttl;
	iph->frag_off	=	0;
	iph->daddr	=	daddr;
	iph->saddr	=	saddr;
	iph->protocol	=	IPPROTO_IPIP;
	iph->ihl	=	5;
	iph->tot_len	=	htons(skb->len);
	iph->id		=	htons(ip_id_count++);
	ip_send_check(iph);

	skb->h.ipiph = skb->nh.iph;
	skb->nh.iph = iph;
}

/*
 *	Processing handlers for ipmr_forward
 */

static void ipmr_queue_xmit(struct sk_buff *skb, struct mfc_cache *c,
			   int vifi, int last)
{
	struct iphdr *iph = skb->nh.iph;
	struct vif_device *vif = &vif_table[vifi];
	struct device *dev;
	struct rtable *rt;
	int    encap = 0;
	struct sk_buff *skb2;
	int    err;
		
	if (vif->flags&VIFF_TUNNEL) {
		rt = vif->u.rt;
		if (!rt || rt->u.dst.obsolete) {
			ip_rt_put(rt);
			vif->u.rt = NULL;
			err = ip_route_output(&rt, vif->remote, vif->local, RT_TOS(iph->tos), NULL);
			if (err)
				return;
			vif->u.rt = rt;
		}
		dst_clone(&rt->u.dst);
		encap = sizeof(struct iphdr);
	} else {
		dev = vif->u.dev;
		if (dev == NULL)
			return;
		err = ip_route_output(&rt, iph->daddr, 0, RT_TOS(iph->tos), dev);
		if (err)
			return;
	}

	dev = rt->u.dst.dev;

	if (skb->len+encap > dev->mtu && (ntohs(iph->frag_off) & IP_DF)) {
		ip_statistics.IpFragFails++;
		ip_rt_put(rt);
		return;
	}

	encap += dev->hard_header_len;

	if (skb->len+encap > 65534) {
		ip_rt_put(rt);
		return;
	}

	if (skb_headroom(skb) < encap || (encap && !last))
		skb2 = skb_realloc_headroom(skb, (encap + 15)&~15);
	else
		skb2 = skb_clone(skb, GFP_ATOMIC);

	if (skb2 == NULL) {
		ip_rt_put(rt);
		return;
	}

	vif->pkt_out++;
	vif->bytes_out+=skb->len;

	dst_release(skb2->dst);
	skb2->dst = &rt->u.dst;

	iph = skb2->nh.iph;
	ip_decrease_ttl(iph);

	if (vif->flags & VIFF_TUNNEL)
		ip_encap(skb2, vif->local, vif->remote);

	ip_send(skb2);
}

/*
 *	Multicast packets for forwarding arrive here
 */

int ip_mr_input(struct sk_buff *skb)
{
	struct mfc_cache *cache;
	int psend = -1;
	int vif, ct;
	int local = 0;
	int tunneled = IPCB(skb)->flags&IPSKB_TUNNELED;

	cache = ipmr_cache_find(skb->nh.iph->saddr, skb->nh.iph->daddr);
	
	/*
	 *	No usable cache entry
	 */
	 
	if (cache==NULL || (cache->mfc_flags&MFC_QUEUED)) {
		ipmr_cache_unresolved(cache, ALL_VIFS, skb);
		return -EAGAIN;
	}

	vif = cache->mfc_parent;
	cache->mfc_pkt++;
	cache->mfc_bytes += skb->len;

	/*
	 * Wrong interface: drop packet and (maybe) send PIM assert.
	 */
	if (vif >= maxvif || !(vifc_map&(1<<vif)) ||
	    (tunneled && IPCB(skb)->vif != vif) ||
	    (!tunneled && (vif_table[vif].flags&VIFF_TUNNEL ||
	     vif_table[vif].u.dev != skb->dev))) {
		cache->mfc_wrong_if++;
		if (vif < MAXVIFS && mroute_do_pim &&
		    !(vif_table[vif].flags&VIFF_TUNNEL) &&
		    skb->dev->flags&IFF_BROADCAST &&
		    jiffies - cache->mfc_last_assert > MFC_ASSERT_THRESH) {
			cache->mfc_last_assert = jiffies;
			/*
			 *	It is wrong! Routing daemon can
			 *	determine vif itself, but it cannot
			 *	determine REAL device.
			 *	BSD bug. Fix it later, PIM does not
			 *	work in any case 8) _ANK_
			 */
			ipmr_cache_report(skb, vif, 1);
		}
		kfree_skb(skb, FREE_WRITE);
		return -EINVAL;
	}

	vif_table[vif].pkt_in++;
	vif_table[vif].bytes_in+=skb->len;

	if (IPCB(skb)->opt.router_alert ||
	    ((struct rtable*)skb->dst)->rt_flags&RTF_LOCAL ||
	    skb->nh.iph->protocol == IPPROTO_IGMP)
		local = 1;

	/*
	 *	Forward the frame
	 */
	ct = cache->mfc_maxvif-1;
	while (ct>=cache->mfc_minvif) {
		/*
		 *	0 means don't do it. Silly idea, 255 as don't do it would be cleaner!
		 */
		if (skb->nh.iph->ttl > cache->mfc_ttls[ct] && cache->mfc_ttls[ct]>0) {
			if (psend != -1)
				ipmr_queue_xmit(skb, cache, psend, 0);
			psend=ct;
		}
		ct--;
	}
	if (psend != -1)
		ipmr_queue_xmit(skb, cache, psend, 1);
	if (!local) {
		kfree_skb(skb, FREE_READ);
		return 0;
	}
	return ip_local_deliver(skb);
}

int ip_mr_find_tunnel(u32 local, u32 remote)
{
	int ct;
	struct vif_device *vif;

	for (ct=0; ct<maxvif; ct++) {
		vif = &vif_table[ct];
		if (vifc_map&(1<<ct) && vif->flags&VIFF_TUNNEL &&
		    vif->local == local && vif->remote == remote)
			return ct;
	}
	return -1;
}

/*
 *	The /proc interfaces to multicast routing /proc/ip_mr_cache /proc/ip_mr_vif
 */
 
int ipmr_vif_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	struct vif_device *vif;
	int len=0;
	off_t pos=0;
	off_t begin=0;
	int size;
	int ct;

	len += sprintf(buffer,
		 "Interface  Bytes In  Pkts In  Bytes Out  Pkts Out  Flags Local    Remote\n");
	pos=len;
  
	for (ct=0;ct<maxvif;ct++) 
	{
		vif=&vif_table[ct];
		if(!(vifc_map&(1<<ct)))
			continue;
        	size = sprintf(buffer+len, "%2d %-10s %8ld  %7ld  %8ld   %7ld   %05X %08lX %08lX\n",
        		ct, vif->flags&VIFF_TUNNEL ? "Tunnel" : vif->u.dev->name, vif->bytes_in, vif->pkt_in, vif->bytes_out, vif->pkt_out,
        		vif->flags, vif->local, vif->remote);
		len+=size;
		pos+=size;
		if(pos<offset)
		{
			len=0;
			begin=pos;
		}
		if(pos>offset+length)
			break;
  	}
  	
  	*start=buffer+(offset-begin);
  	len-=(offset-begin);
  	if(len>length)
  		len=length;
  	return len;
}

int ipmr_mfc_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	struct mfc_cache *mfc;
	int len=0;
	off_t pos=0;
	off_t begin=0;
	int size;
	int ct;

	len += sprintf(buffer,
		 "Group    Origin   SrcIface Pkts     Bytes    Wrong    VifTtls\n");
	pos=len;
  
	for (ct=0;ct<MFC_LINES;ct++) 
	{
		start_bh_atomic();
		mfc=mfc_cache_array[ct];
		while(mfc!=NULL)
		{
			char *name="none";
			int n;
			/*
			 *	Device name
			 */
			if(mfc->mfc_parent < maxvif && vifc_map&(1<<mfc->mfc_parent)) {
				if (vif_table[mfc->mfc_parent].flags&VIFF_TUNNEL)
					name="Tunnel";
				else
					name=vif_table[mfc->mfc_parent].u.dev->name;
			}
			/*
			 *	Interface forwarding map
			 */
			size = sprintf(buffer+len, "%08lX %08lX %-8s %8ld %8ld %8ld",
				(unsigned long)mfc->mfc_mcastgrp,
				(unsigned long)mfc->mfc_origin,
				name,
				mfc->mfc_bytes,
				mfc->mfc_pkt,
				mfc->mfc_wrong_if);
			for(n=0;n<maxvif;n++)
			{
				if(vifc_map&(1<<n))
					size += sprintf(buffer+len+size, " %-3d", mfc->mfc_ttls[n]);
				else
					size += sprintf(buffer+len+size, " --- ");
			}
			size += sprintf(buffer+len+size, "\n");
			len+=size;
			pos+=size;
			if(pos<offset)
			{
				len=0;
				begin=pos;
			}
			if(pos>offset+length)
			{
				sti();
				goto done;
			}
			mfc=mfc->next;
	  	}
	  	end_bh_atomic();
  	}
done:
  	*start=buffer+(offset-begin);
  	len-=(offset-begin);
  	if(len>length)
  		len=length;
  	return len;
}

#ifdef CONFIG_PROC_FS	
static struct proc_dir_entry proc_net_ipmr_vif = {
	PROC_NET_IPMR_VIF, 9 ,"ip_mr_vif",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_net_inode_operations,
	ipmr_vif_info
};
static struct proc_dir_entry proc_net_ipmr_mfc = {
	PROC_NET_IPMR_MFC, 11 ,"ip_mr_cache",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_net_inode_operations,
	ipmr_mfc_info
};
#endif	


/*
 *	Setup for IP multicast routing
 */
 
__initfunc(void ip_mr_init(void))
{
	printk(KERN_INFO "Linux IP multicast router 0.06.\n");
	register_netdevice_notifier(&ip_mr_notifier);
#ifdef CONFIG_PROC_FS	
	proc_net_register(&proc_net_ipmr_vif);
	proc_net_register(&proc_net_ipmr_mfc);
#endif	
}
