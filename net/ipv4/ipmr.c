/*
 *	IP multicast routing support for mrouted 3.6
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
#include <asm/segment.h>
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
int mroute_do_pim = 0;					/* Set in PIM assert	*/
static struct mfc_cache *mfc_cache_array[MFC_LINES];	/* Forwarding cache	*/
static struct mfc_cache *cache_resolve_queue;		/* Unresolved cache	*/
int cache_resolve_queue_len = 0;			/* Size of unresolved	*/

/*
 *	Delete a VIF entry
 */
 
static void vif_delete(struct vif_device *v)
{
	if(!(v->flags&VIFF_TUNNEL))
	{
		v->dev->flags&=~IFF_ALLMULTI;
		dev_mc_upload(v->dev);
	}
	v->dev=NULL;
}

/*
 *	Find a vif
 */
 
static int ipmr_vifi_find(struct device *dev)
{
	struct vif_device *v=&vif_table[0];
	int ct;
	for(ct=0;ct<MAXVIFS;ct++,v++)
	{
		if(v->dev==dev)
			return ct;
	}
	return -1;
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

	if(cache->mfc_flags&MFC_QUEUED)
	{	
		cp=&cache_resolve_queue;
		del_timer(&cache->mfc_timer);
	}	
	else
	{
		line=MFC_HASH(cache->mfc_mcastgrp,cache->mfc_origin);
		cp=&(mfc_cache_array[line]);
	}
	
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
	cache=cache_resolve_queue;
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
	return c;
}
 
/*
 *	A cache entry has gone into a resolved state from queued
 */
 
static void ipmr_cache_resolve(struct mfc_cache *cache)
{
	struct mfc_cache **p;
	struct sk_buff *skb;
	/*
	 *	Kill the queue entry timer.
	 */
	del_timer(&cache->mfc_timer);
	cache->mfc_flags&=~MFC_QUEUED;
	/*
	 *	Remove from the resolve queue
	 */
	p=&cache_resolve_queue;
	while((*p)!=NULL)
	{
		if((*p)==cache)
		{
			*p=cache->next;
			break;
		}
		p=&((*p)->next);
	}
	cache_resolve_queue_len--;
	sti();
	/*
	 *	Insert into the main cache
	 */
	ipmr_cache_insert(cache);
	/*
	 *	Play the pending entries through our router
	 */
	while((skb=skb_dequeue(&cache->mfc_unresolved)))
		ipmr_forward(skb, skb->protocol);
}

/*
 *	Bounce a cache query up to mrouted. We could use netlink for this but mrouted
 *	expects the following bizarre scheme..
 */
 
static void ipmr_cache_report(struct sk_buff *pkt)
{
	struct sk_buff *skb=alloc_skb(128, GFP_ATOMIC);
	int ihl=pkt->ip_hdr->ihl<<2;
	struct igmphdr *igmp;
	if(!skb)
		return;
		
	skb->free=1;
	
	/*
	 *	Copy the IP header
	 */

	skb->ip_hdr=(struct iphdr *)skb_put(skb,ihl);
	skb->h.iph=skb->ip_hdr;
	memcpy(skb->data,pkt->data,ihl);
	skb->ip_hdr->protocol = 0;			/* Flag to the kernel this is a route add */
	
	/*
	 *	Add our header
	 */
	
	igmp=(struct igmphdr *)skb_put(skb,sizeof(struct igmphdr));
	igmp->type	=	IGMPMSG_NOCACHE;		/* non IGMP dummy message */
	igmp->code 	=	0;
	skb->ip_hdr->tot_len=htons(skb->len);			/* Fix the length */
	
	/*
	 *	Deliver to mrouted
	 */
	if(sock_queue_rcv_skb(mroute_socket,skb)<0)
	{
		skb->sk=NULL;
		kfree_skb(skb, FREE_READ);
	}
}
		
 
/*
 *	Queue a packet for resolution
 */
 
static void ipmr_cache_unresolved(struct mfc_cache *cache, vifi_t vifi, struct sk_buff *skb, int is_frag)
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
		cache->mfc_origin=skb->ip_hdr->saddr;
		cache->mfc_mcastgrp=skb->ip_hdr->daddr;
		cache->mfc_flags=MFC_QUEUED;
		/*
		 *	Link to the unresolved list
		 */
		cache->next=cache_resolve_queue;
		cache_resolve_queue=cache;
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
			ipmr_cache_report(skb);
	}
	/*
	 *	See if we can append the packet
	 */
	if(cache->mfc_queuelen>3)
	{
		kfree_skb(skb, FREE_WRITE);
		return;
	}
	/*
	 *	Add to our 'pending' list. Cache the is_frag data
	 *	in skb->protocol now it is spare.
	 */
	cache->mfc_queuelen++;
	skb->protocol=is_frag;
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
	
	cli();

	cache=ipmr_cache_find(mfc->mfcc_origin.s_addr,mfc->mfcc_mcastgrp.s_addr);
	
	/*
	 *	Delete an entry
	 */
	if(action==MRT_DEL_MFC)
	{
		if(cache)
		{
			ipmr_cache_delete(cache);
			sti();
			return 0;
		}
		sti();
		return -ENOENT;
	}
	if(cache)
	{
		/*
		 *	Update the cache, see if it frees a pending queue
		 */

		cache->mfc_flags|=MFC_RESOLVED;
		memcpy(cache->mfc_ttls, mfc->mfcc_ttls,sizeof(cache->mfc_ttls));
		 
		/*
		 *	Check to see if we resolved a queued list. If so we
		 *	need to send on the frames and tidy up.
		 */
		 
		if(cache->mfc_flags&MFC_QUEUED)
			ipmr_cache_resolve(cache);	/* Unhook & send the frames */
		sti();
		return 0;
	}
	/*
	 *	Unsolicited update - that's ok, add anyway.
	 */
	 
	
	cache=ipmr_cache_alloc(GFP_ATOMIC);
	if(cache==NULL)
	{
		sti();
		return -ENOMEM;
	}
	cache->mfc_flags=MFC_RESOLVED;
	cache->mfc_origin=mfc->mfcc_origin.s_addr;
	cache->mfc_mcastgrp=mfc->mfcc_mcastgrp.s_addr;
	cache->mfc_parent=mfc->mfcc_parent;
	memcpy(cache->mfc_ttls, mfc->mfcc_ttls,sizeof(cache->mfc_ttls));
	ipmr_cache_insert(cache);
	sti();
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
			if((err=verify_area(VERIFY_READ,optval,sizeof(int)))<0)
				return err;
			if(get_user((int *)optval)!=1)
				return -ENOPROTOOPT;
			if(mroute_socket)
				return -EADDRINUSE;
			mroute_socket=sk;
			/* Initialise state */
			return 0;
		case MRT_DONE:
			mroute_close(sk);
			mroute_socket=NULL;
			return 0;
		case MRT_ADD_VIF:
		case MRT_DEL_VIF:
			if(optlen!=sizeof(vif))
				return -EINVAL;
			if((err=verify_area(VERIFY_READ, optval, sizeof(vif)))<0)
				return err;
			memcpy_fromfs(&vif,optval,sizeof(vif));
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
				dev=ip_dev_find(vif.vifc_lcl_addr.s_addr);
				if(!dev)
					return -EADDRNOTAVAIL;
				/* Must be tunnelled or multicastable */
				if(vif.vifc_flags&VIFF_TUNNEL)
				{
					if(vif.vifc_flags&VIFF_SRCRT)
						return -EOPNOTSUPP;
					/* IPIP will do all the work */
				}
				else
				{
					if(dev->flags&IFF_MULTICAST)
					{
						/* Most ethernet cards don't know
						   how to do this yet.. */
						dev->flags|=IFF_ALLMULTI;
						dev_mc_upload(dev);
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
				v->dev=dev;
				v->bytes_in = 0;
				v->bytes_out = 0;
				v->pkt_in = 0;
				v->pkt_out = 0;
				vifc_map|=(1<<vif.vifc_vifi);
				sti();
				return 0;
			}
			else
			/*
			 *	VIF deletion
			 */
			{
				struct vif_device *v=&vif_table[vif.vifc_vifi];
				if(vifc_map&(1<<vif.vifc_vifi))
				{
					vif_delete(v);
					vifc_map&=~(1<<vif.vifc_vifi);
					return 0;					
				}
				else
					return -EADDRNOTAVAIL;
			}
		/*
		 *	Manipulate the forwarding caches. These live
		 *	in a sort of kernel/user symbiosis.
		 */
		case MRT_ADD_MFC:
		case MRT_DEL_MFC:
			err=verify_area(VERIFY_READ, optval, sizeof(mfc));
			if(err)
				return err;
			memcpy_fromfs(&mfc,optval, sizeof(mfc));
			return ipmr_mfc_modify(optname, &mfc);
		/*
		 *	Control PIM assert.
		 */
		case MRT_ASSERT:
			if(optlen!=sizeof(int))
				return -EINVAL;
			if((err=verify_area(VERIFY_READ, optval,sizeof(int)))<0)
				return err;
			mroute_do_pim= (optval)?1:0;
			return 0;
		/*
		 *	Spurious command, or MRT_VERSION which you cannot
		 *	set.
		 */
		default:
			return -EOPNOTSUPP;
	}
}

/*
 *	Getsock opt support for the multicast routing system.
 */
 
int ip_mroute_getsockopt(struct sock *sk,int optname,char *optval,int *optlen)
{
	int olr;
	int err;

	if(sk!=mroute_socket)
		return -EACCES;
	if(optname!=MRT_VERSION && optname!=MRT_ASSERT)
		return -EOPNOTSUPP;
	
	olr=get_user(optlen);
	if(olr!=sizeof(int))
		return -EINVAL;
	err=verify_area(VERIFY_WRITE, optval,sizeof(int));
	if(err)
		return err;
	put_user(sizeof(int),optlen);
	if(optname==MRT_VERSION)
		put_user(0x0305,(int *)optval);
	else
		put_user(mroute_do_pim,(int *)optval);
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
	
	switch(cmd)
	{
		case SIOCGETVIFCNT:
			err=verify_area(VERIFY_WRITE, (void *)arg, sizeof(vr));
			if(err)
				return err;
			memcpy_fromfs(&vr,(void *)arg,sizeof(vr));
			if(vr.vifi>=MAXVIFS)
				return -EINVAL;
			vif=&vif_table[vr.vifi];
			if(vifc_map&(1<<vr.vifi))
			{
				vr.icount=vif->pkt_in;
				vr.ocount=vif->pkt_out;
				vr.ibytes=vif->bytes_in;
				vr.obytes=vif->bytes_out;
				memcpy_tofs((void *)arg,&vr,sizeof(vr));
				return 0;
			}
			return -EADDRNOTAVAIL;
		case SIOCGETSGCNT:
			err=verify_area(VERIFY_WRITE, (void *)arg, sizeof(sr));
			if(err)
				return err;
			memcpy_fromfs(&sr,(void *)arg,sizeof(sr));
			memcpy_tofs((void *)arg,&sr,sizeof(sr));
			return 0;
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
	struct vif_device *v=&vif_table[0];
		
	/*
	 *	Shut down all active vif entries
	 */
	 
	for(i=0;i<MAXVIFS;i++)
	{
		if(vifc_map&(1<<i))
		{
			if(!(v->flags&VIFF_TUNNEL))
			{
				v->dev->flags&=~IFF_ALLMULTI;
				dev_mc_upload(v->dev);
			}
		}
		v++;
	}		
	vifc_map=0;	
	/*
	 *	Wipe the cache
	 */
	for(i=0;i<MFC_LINES;i++)
	{
		while(mfc_cache_array[i]!=NULL)
			ipmr_cache_delete(mfc_cache_array[i]);
	}	
	/* The timer will clear any 'pending' stuff */
}

static int ipmr_device_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct vif_device *v;
	int ct;
	if(event!=NETDEV_DOWN)
		return NOTIFY_DONE;
	v=&vif_table[0];
	for(ct=0;ct<MAXVIFS;ct++)
	{
		if((vifc_map&(1<<ct)) && v->dev==ptr)
		{
			vif_delete(v);
			vifc_map&=~(1<<ct);
		}
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
 *	Processing handlers for ipmr_forward
 */

static void ipmr_queue_xmit(struct sk_buff *skb, struct vif_device *vif, struct device *in_dev, int frag)
{
	int tunnel=0;
	__u32 raddr=skb->raddr;
	if(vif->flags&VIFF_TUNNEL)
	{
		tunnel=IPFWD_MULTITUNNEL;
		raddr=vif->remote;
	}
	vif->pkt_out++;
	vif->bytes_out+=skb->len;
	skb->dev=vif->dev;
	skb->raddr=skb->h.iph->daddr;
	/*
	 *	If the vif went down as we were forwarding.. just throw the
	 *	frame.
	 */
	if(vif->dev==NULL || ip_forward(skb, in_dev, frag|IPFWD_MULTICASTING|tunnel, raddr)==-1)
		kfree_skb(skb, FREE_WRITE);
}

/*
 *	Multicast packets for forwarding arrive here
 */

void ipmr_forward(struct sk_buff *skb, int is_frag)
{
	struct mfc_cache *cache;
	struct sk_buff *skb2;
	int psend = -1;
	int vif=ipmr_vifi_find(skb->dev);
	if(vif==-1)
	{
		kfree_skb(skb, FREE_WRITE);
		return;
	}
	
	vif_table[vif].pkt_in++;
	vif_table[vif].bytes_in+=skb->len;
	
	cache=ipmr_cache_find(skb->ip_hdr->saddr,skb->ip_hdr->daddr);
	
	/*
	 *	No usable cache entry
	 */
	 
	if(cache==NULL || (cache->mfc_flags&MFC_QUEUED))
		ipmr_cache_unresolved(cache,vif,skb, is_frag);
	else
	{
		/*
		 *	Forward the frame
		 */
		 int ct=0;
		 while(ct<MAXVIFS)
		 {
		 	/*
		 	 *	0 means don't do it. Silly idea, 255 as don't do it would be cleaner!
		 	 */
		 	if(skb->ip_hdr->ttl > cache->mfc_ttls[ct] && cache->mfc_ttls[ct]>0)
		 	{
		 		if(psend!=-1)
		 		{
		 			/*
		 			 *	May get variant mac headers
		 			 *	so must copy -- boo hoo.
		 			 */
		 			skb2=skb_copy(skb, GFP_ATOMIC);
		 			if(skb2)
		 			{
		 				skb2->free=1;
		 				ipmr_queue_xmit(skb2, &vif_table[psend], skb->dev, is_frag);
		 			}
				}
				psend=ct;
			}
			ct++;
		}
		if(psend==-1)
			kfree_skb(skb, FREE_WRITE);
		else
		{
			ipmr_queue_xmit(skb, &vif_table[psend], skb->dev, is_frag);
		}
		/*
		 *	Adjust the stats
		 */
	}
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
  
	for (ct=0;ct<MAXVIFS;ct++) 
	{
		vif=&vif_table[ct];
		if(!(vifc_map&(1<<ct)))
			continue;
		if(vif->dev==NULL)
			continue;
        	size = sprintf(buffer+len, "%-10s %8ld  %7ld  %8ld   %7ld   %05X %08lX %08lX\n",
        		vif->dev->name,vif->bytes_in, vif->pkt_in, vif->bytes_out,vif->pkt_out,
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
		 "Group    Origin   SrcIface \n");
	pos=len;
  
	for (ct=0;ct<MFC_LINES;ct++) 
	{
		cli();
		mfc=mfc_cache_array[ct];
		while(mfc!=NULL)
		{
			char *name="none";
			char vifmap[MAXVIFS+1];
			int n;
			/*
			 *	Device name
			 */
			if(vifc_map&(1<<mfc->mfc_parent))
				name=vif_table[mfc->mfc_parent].dev->name;
			/*
			 *	Interface forwarding map
			 */
			for(n=0;n<MAXVIFS;n++)
				if(vifc_map&(1<<n) && mfc->mfc_ttls[ct])
					vifmap[n]='X';
				else
					vifmap[n]='-';
			vifmap[n]=0;
			/*
			 *	Now print it out
			 */
			size = sprintf(buffer+len, "%08lX %08lX %-8s %s\n",
				(unsigned long)mfc->mfc_mcastgrp,
				(unsigned long)mfc->mfc_origin,
				name,
				vifmap);
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
	  	sti();
  	}
done:
  	*start=buffer+(offset-begin);
  	len-=(offset-begin);
  	if(len>length)
  		len=length;
  	return len;
}

/*
 *	Setup for IP multicast routing
 */
 
void ip_mr_init(void)
{
	printk(KERN_INFO "Linux IP multicast router 0.05.\n");
	register_netdevice_notifier(&ip_mr_notifier);
#ifdef CONFIG_PROC_FS	
	proc_net_register(&(struct proc_dir_entry) {
		PROC_NET_IPMR_VIF, 9 ,"ip_mr_vif",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_net_inode_operations,
		ipmr_vif_info
	});
	proc_net_register(&(struct proc_dir_entry) {
		PROC_NET_IPMR_MFC, 11 ,"ip_mr_cache",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_net_inode_operations,
		ipmr_mfc_info
	});
#endif	
}
