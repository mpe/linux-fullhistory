/*
 *	Linux NET3:	Internet Group Management Protocol  [IGMP]
 *
 *	This code implements the IGMP protocol as defined in RFC1112. There has
 *	been a further revision of this protocol since which is now supported.
 *
 *	If you have trouble with this module be careful what gcc you have used,
 *	the older version didn't come out right using gcc 2.5.8, the newer one
 *	seems to fall out with gcc 2.6.2.
 *
 *	Authors:
 *		Alan Cox <Alan.Cox@linux.org>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	Fixes:
 *
 *		Alan Cox	:	Added lots of __inline__ to optimise
 *					the memory usage of all the tiny little
 *					functions.
 *		Alan Cox	:	Dumped the header building experiment.
 *		Alan Cox	:	Minor tweaks ready for multicast routing
 *					and extended IGMP protocol.
 *		Alan Cox	:	Removed a load of inline directives. Gcc 2.5.8
 *					writes utterly bogus code otherwise (sigh)
 *					fixed IGMP loopback to behave in the manner
 *					desired by mrouted, fixed the fact it has been
 *					broken since 1.3.6 and cleaned up a few minor
 *					points.
 *
 *		Chih-Jen Chang	:	Tried to revise IGMP to Version 2
 *		Tsu-Sheng Tsao		E-mail: chihjenc@scf.usc.edu and tsusheng@scf.usc.edu
 *					The enhancements are mainly based on Steve Deering's 
 * 					ipmulti-3.5 source code.
 *		Chih-Jen Chang	:	Added the igmp_get_mrouter_info and
 *		Tsu-Sheng Tsao		igmp_set_mrouter_info to keep track of
 *					the mrouted version on that device.
 *		Chih-Jen Chang	:	Added the max_resp_time parameter to
 *		Tsu-Sheng Tsao		igmp_heard_query(). Using this parameter
 *					to identify the multicast router version
 *					and do what the IGMP version 2 specified.
 *		Chih-Jen Chang	:	Added a timer to revert to IGMP V2 router
 *		Tsu-Sheng Tsao		if the specified time expired.
 *		Alan Cox	:	Stop IGMP from 0.0.0.0 being accepted.
 *		Alan Cox	:	Use GFP_ATOMIC in the right places.
 *		Christian Daudt :	igmp timer wasn't set for local group
 *					memberships but was being deleted, 
 *					which caused a "del_timer() called 
 *					from %p with timer not initialized\n"
 *					message (960131).
 *		Christian Daudt :	removed del_timer from 
 *					igmp_timer_expire function (960205).
 *             Christian Daudt :       igmp_heard_report now only calls
 *                                     igmp_timer_expire if tm->running is
 *                                     true (960216).
 *		Malcolm Beattie :	ttl comparison wrong in igmp_rcv made
 *					igmp_heard_query never trigger. Expiry
 *					miscalculation fixed in igmp_heard_query
 *					and random() made to return unsigned to
 *					prevent negative expiry times.
 *		Alexey Kuznetsov:	Wrong group leaving behaviour, backport
 *					fix from pending 2.1.x patches.
 *		Alan Cox:		Forget to enable FDDI support earlier.
 */


#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <net/route.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <linux/igmp.h>
#include <net/checksum.h>


/*
 *	If time expired, change the router type to IGMP_NEW_ROUTER.
 */

static void ip_router_timer_expire(unsigned long data)
{
	struct ip_router_info *i=(struct ip_router_info *)data;

	del_timer(&i->timer);
	i->type=IGMP_NEW_ROUTER;	/* Revert to new multicast router */
	i->time=0;
}

/*
 *	Multicast router info manager
 */

struct	ip_router_info	*ip_router_info_head=(struct ip_router_info *)0;

/*
 *	Get the multicast router info on that device
 */

static	struct	ip_router_info	*igmp_get_mrouter_info(struct device *dev)
{
	register struct ip_router_info *i;

	for(i=ip_router_info_head;i!=NULL;i=i->next)
	{
		if (i->dev == dev)
		{
			return i;
		}
	}

	/*
	 *  Not found. Create a new entry. The default is IGMP V2 router
	 */
	 
	i=(struct ip_router_info *)kmalloc(sizeof(*i), GFP_ATOMIC);
	if(i==NULL)
		return NULL;
	i->dev = dev;
	i->type = IGMP_NEW_ROUTER;
	i->time = IGMP_AGE_THRESHOLD;
	i->next = ip_router_info_head;
	ip_router_info_head = i;

	init_timer(&i->timer);
	i->timer.data=(unsigned long)i;
	i->timer.function=&ip_router_timer_expire;

	return i;
}

/*
 *	Set the multicast router info on that device
 */

static	struct	ip_router_info	*igmp_set_mrouter_info(struct device *dev,int type,int time)
{
	register struct ip_router_info *i;

	for(i=ip_router_info_head;i!=NULL;i=i->next)
	{
		if (i->dev == dev)
		{
			if(i->type==IGMP_OLD_ROUTER)
			{
				del_timer(&i->timer);
			}

			i->type = type;
			i->time = time;

			if(i->type==IGMP_OLD_ROUTER)
			{
				i->timer.expires=jiffies+i->time*HZ;
				add_timer(&i->timer);
			}
			return i;
		}
	}

	/*
	 *  Not found. Create a new entry.
	 */
	i=(struct ip_router_info *)kmalloc(sizeof(*i), GFP_ATOMIC);
	if(i==NULL)
		return NULL;
	i->dev = dev;
	i->type = type;
	i->time = time;
	i->next = ip_router_info_head;
	ip_router_info_head = i;

	init_timer(&i->timer);
	i->timer.data=(unsigned long)i;
	i->timer.function=&ip_router_timer_expire;
	if(i->type==IGMP_OLD_ROUTER)
	{
		i->timer.expires=jiffies+i->time*HZ;
		add_timer(&i->timer);
	}

	return i;
}


/*
 *	Timer management
 */

static void igmp_stop_timer(struct ip_mc_list *im)
{
  if (im->tm_running)
  {
	  del_timer(&im->timer);
	  im->tm_running=0;
  }
  else
	  printk(KERN_ERR "igmp_stop_timer() called with timer not running by %p\n",__builtin_return_address(0));
}

extern __inline__ unsigned int random(void)
{
	static unsigned long seed=152L;
	seed=seed*69069L+1;
	return seed^jiffies;
}

/*
 *	Inlined as it's only called once.
 */

static void igmp_start_timer(struct ip_mc_list *im,unsigned char max_resp_time)
{
	int tv;
	if(im->tm_running)
		return;
	tv=random()%(max_resp_time*HZ/IGMP_TIMER_SCALE); /* Pick a number any number 8) */
	im->timer.expires=jiffies+tv;
	im->tm_running=1;
	add_timer(&im->timer);
}

/*
 *	Send an IGMP report.
 */

#define IGMP_SIZE (sizeof(struct igmphdr)+sizeof(struct iphdr)+4)

static void igmp_send_report(struct device *dev, u32 group, int type)
{
	struct sk_buff *skb;
	struct iphdr *iph;
	struct igmphdr *ih;
	struct rtable *rt;

	if (ip_route_output(&rt, group, 0, 0, dev))
		return;

	skb=alloc_skb(IGMP_SIZE+dev->hard_header_len+15, GFP_ATOMIC);
	if (skb == NULL) {
		ip_rt_put(rt);
		return;
	}

	skb->dst = &rt->u.dst;

	skb_reserve(skb, (dev->hard_header_len+15)&~15);
	ip_ll_header(skb);

	skb->nh.iph = iph = (struct iphdr *)skb_put(skb, sizeof(struct iphdr)+4);

	iph->version  = 4;
	iph->ihl      = (sizeof(struct iphdr)+4)>>2;
	iph->tos      = 0;
	iph->frag_off = 0;
	iph->ttl      = 1;
	iph->daddr    = group;
	iph->saddr    = rt->rt_src;
	iph->protocol = IPPROTO_IGMP;
	iph->tot_len  = htons(IGMP_SIZE);
	iph->id	      = htons(ip_id_count++);
	((u8*)&iph[1])[0] = IPOPT_RA;
	((u8*)&iph[1])[1] = 4;
	((u8*)&iph[1])[2] = 0;
	((u8*)&iph[1])[3] = 0;
	ip_send_check(iph);

	ih = (struct igmphdr *)skb_put(skb, sizeof(struct igmphdr));
	ih->type=type;
	ih->code=0;
	ih->csum=0;
	ih->group=group;
	ih->csum=ip_compute_csum((void *)ih, sizeof(struct igmphdr));

	skb->dst->output(skb);
}


static void igmp_timer_expire(unsigned long data)
{
	struct ip_mc_list *im=(struct ip_mc_list *)data;
	struct ip_router_info *r;

	im->tm_running=0;
	r=igmp_get_mrouter_info(im->interface);
	if(r==NULL)
		return;
	if(r->type==IGMP_NEW_ROUTER)
		igmp_send_report(im->interface, im->multiaddr, IGMP_HOST_NEW_MEMBERSHIP_REPORT);
	else
		igmp_send_report(im->interface, im->multiaddr, IGMP_HOST_MEMBERSHIP_REPORT);
	im->reporter = 1;
}

static void igmp_init_timer(struct ip_mc_list *im)
{
	im->tm_running=0;
	init_timer(&im->timer);
	im->timer.data=(unsigned long)im;
	im->timer.function=&igmp_timer_expire;
}


static void igmp_heard_report(struct device *dev, u32 group, u32 source)
{
	struct ip_mc_list *im;

	/* Timers are only set for non-local groups */
	if (LOCAL_MCAST(group))
		return;

	for (im=dev->ip_mc_list; im!=NULL; im=im->next) {
		if (im->multiaddr == group) {
			if (im->tm_running)
				igmp_stop_timer(im);
			if (source != dev->pa_addr)
				im->reporter = 0;
			return;
		}
	}
}

static void igmp_heard_query(struct device *dev, unsigned char max_resp_time,
			     u32 group)
{
	struct ip_mc_list *im;
	int	mrouter_type;

	/*
	 *	The max_resp_time is in units of 1/10 second.
	 */
	if(max_resp_time>0) {
		mrouter_type=IGMP_NEW_ROUTER;

		if (igmp_set_mrouter_info(dev,mrouter_type,0)==NULL)
			return;
		/*
		 * - Start the timers in all of our membership records
		 *   that the query applies to for the interface on
		 *   which the query arrived excl. those that belong
		 *   to a "local" group (224.0.0.X)
		 * - For timers already running check if they need to
		 *   be reset.
		 * - Use the igmp->igmp_code field as the maximum
		 *   delay possible
		 */
		for(im=dev->ip_mc_list;im!=NULL;im=im->next) {
			if (group && group != im->multiaddr)
				continue;
			if(im->tm_running) {
				if(im->timer.expires>jiffies+max_resp_time*HZ/IGMP_TIMER_SCALE) {
					igmp_stop_timer(im);
					igmp_start_timer(im,max_resp_time);
				}
			} else if (!LOCAL_MCAST(im->multiaddr))
				igmp_start_timer(im,max_resp_time);
		}
	} else {
		mrouter_type=IGMP_OLD_ROUTER;
		max_resp_time=IGMP_MAX_HOST_REPORT_DELAY*IGMP_TIMER_SCALE;

		if(igmp_set_mrouter_info(dev,mrouter_type,IGMP_AGE_THRESHOLD)==NULL)
			return;

		/*
		 * Start the timers in all of our membership records for
		 * the interface on which the query arrived, except those
		 * that are already running and those that belong to a
		 * "local" group (224.0.0.X).
		 */

		for(im=dev->ip_mc_list;im!=NULL;im=im->next) {
			if(!im->tm_running && !LOCAL_MCAST(im->multiaddr))
				igmp_start_timer(im,max_resp_time);
		}
	}
}

/*
 *	Map a multicast IP onto multicast MAC for type ethernet.
 */

extern __inline__ void ip_mc_map(unsigned long addr, char *buf)
{
	addr=ntohl(addr);
	buf[0]=0x01;
	buf[1]=0x00;
	buf[2]=0x5e;
	buf[5]=addr&0xFF;
	addr>>=8;
	buf[4]=addr&0xFF;
	addr>>=8;
	buf[3]=addr&0x7F;
}

/*
 *	Add a filter to a device
 */

void ip_mc_filter_add(struct device *dev, unsigned long addr)
{
	char buf[6];
	ip_rt_multicast_event(dev);
	if(!(dev->flags & IFF_MULTICAST))
		return;
	if(dev->type!=ARPHRD_ETHER && dev->type!=ARPHRD_FDDI)
		return; /* Only do ethernet or FDDI for now */
	ip_mc_map(addr,buf);
	dev_mc_add(dev,buf,ETH_ALEN,0);
}

/*
 *	Remove a filter from a device
 */

void ip_mc_filter_del(struct device *dev, unsigned long addr)
{
	char buf[6];
	ip_rt_multicast_event(dev);
	if(dev->type!=ARPHRD_ETHER && dev->type!=ARPHRD_FDDI)
		return; /* Only do ethernet or FDDI for now */
	ip_mc_map(addr,buf);
	dev_mc_delete(dev,buf,ETH_ALEN,0);
}

extern __inline__ void igmp_group_dropped(struct ip_mc_list *im)
{
	del_timer(&im->timer);
	if (im->reporter)
		igmp_send_report(im->interface, im->multiaddr, IGMP_HOST_LEAVE_MESSAGE);
	ip_mc_filter_del(im->interface, im->multiaddr);
}

extern __inline__ void igmp_group_added(struct ip_mc_list *im)
{
	struct ip_router_info *r;
	igmp_init_timer(im);
	ip_mc_filter_add(im->interface, im->multiaddr);
	r=igmp_get_mrouter_info(im->interface);
	if(r==NULL)
		return;
	if(r->type==IGMP_NEW_ROUTER)
		igmp_send_report(im->interface, im->multiaddr, IGMP_HOST_NEW_MEMBERSHIP_REPORT);
	else
		igmp_send_report(im->interface, im->multiaddr, IGMP_HOST_MEMBERSHIP_REPORT);
}

int igmp_rcv(struct sk_buff *skb, unsigned short len)
{
	/* This basically follows the spec line by line -- see RFC1112 */
	struct igmphdr *ih = skb->h.igmph;

	if (len < sizeof(struct igmphdr) || ip_compute_csum((void *)ih, len)) {
		kfree_skb(skb, FREE_READ);
		return 0;
	}
	
	switch (ih->type) {
	case IGMP_HOST_MEMBERSHIP_QUERY:
		igmp_heard_query(skb->dev, ih->code, ih->group);
		break;
	case IGMP_HOST_MEMBERSHIP_REPORT:
	case IGMP_HOST_NEW_MEMBERSHIP_REPORT:
		igmp_heard_report(skb->dev, ih->group, skb->nh.iph->saddr);
		break;
	case IGMP_DVMRP:
	case IGMP_PIM:
	case IGMP_TRACE:
	case IGMP_HOST_LEAVE_MESSAGE:
	case IGMP_MTRACE:
	case IGMP_MTRACE_RESP:
		break;
	default:
		NETDEBUG(printk(KERN_DEBUG "Unknown IGMP type=%d\n", ih->type));
	}
	kfree_skb(skb, FREE_READ);
	return 0;
}

/*
 *	Multicast list managers
 */


/*
 *	A socket has joined a multicast group on device dev.
 */

static void ip_mc_inc_group(struct device *dev, unsigned long addr)
{
	struct ip_mc_list *i;
	for(i=dev->ip_mc_list;i!=NULL;i=i->next)
	{
		if(i->multiaddr==addr)
		{
			i->users++;
			return;
		}
	}
	i=(struct ip_mc_list *)kmalloc(sizeof(*i), GFP_KERNEL);
	if(!i)
		return;
	i->users=1;
	i->interface=dev;
	i->multiaddr=addr;
	i->next=dev->ip_mc_list;
	igmp_group_added(i);
	dev->ip_mc_list=i;
}

/*
 *	A socket has left a multicast group on device dev
 */

static void ip_mc_dec_group(struct device *dev, unsigned long addr)
{
	struct ip_mc_list **i;
	for(i=&(dev->ip_mc_list);(*i)!=NULL;i=&(*i)->next)
	{
		if((*i)->multiaddr==addr)
		{
			if(--((*i)->users) == 0)
			{
				struct ip_mc_list *tmp= *i;
				igmp_group_dropped(tmp);
				*i=(*i)->next;
				kfree_s(tmp,sizeof(*tmp));
			}
			return;
		}
	}
}

/*
 *	Device going down: Clean up.
 */

void ip_mc_drop_device(struct device *dev)
{
	struct ip_mc_list *i;
	struct ip_mc_list *j;
	for(i=dev->ip_mc_list;i!=NULL;i=j)
	{
		j=i->next;
		kfree_s(i,sizeof(*i));
	}
	dev->ip_mc_list=NULL;
}

/*
 *	Device going up. Make sure it is in all hosts
 */

void ip_mc_allhost(struct device *dev)
{
	struct ip_mc_list *i;
	for(i=dev->ip_mc_list;i!=NULL;i=i->next)
		if(i->multiaddr==IGMP_ALL_HOSTS)
			return;
	i=(struct ip_mc_list *)kmalloc(sizeof(*i), GFP_KERNEL);
	if(!i)
		return;
	i->users=1;
	i->interface=dev;
	i->multiaddr=IGMP_ALL_HOSTS;
	i->tm_running=0;
	i->next=dev->ip_mc_list;
	dev->ip_mc_list=i;
	ip_mc_filter_add(i->interface, i->multiaddr);
}

/*
 *	Join a socket to a group
 */

int ip_mc_join_group(struct sock *sk , struct device *dev, unsigned long addr)
{
	int unused= -1;
	int i;
	if(!MULTICAST(addr))
		return -EINVAL;
	if(sk->ip_mc_list==NULL)
	{
		if((sk->ip_mc_list=(struct ip_mc_socklist *)kmalloc(sizeof(*sk->ip_mc_list), GFP_KERNEL))==NULL)
			return -ENOMEM;
		memset(sk->ip_mc_list,'\0',sizeof(*sk->ip_mc_list));
	}
	for(i=0;i<IP_MAX_MEMBERSHIPS;i++)
	{
		if(sk->ip_mc_list->multiaddr[i]==addr && sk->ip_mc_list->multidev[i]==dev)
			return -EADDRINUSE;
		if(sk->ip_mc_list->multidev[i]==NULL)
			unused=i;
	}

	if(unused==-1)
		return -ENOBUFS;
	sk->ip_mc_list->multiaddr[unused]=addr;
	sk->ip_mc_list->multidev[unused]=dev;
	ip_mc_inc_group(dev,addr);
	return 0;
}

/*
 *	Ask a socket to leave a group.
 */

int ip_mc_leave_group(struct sock *sk, struct device *dev, unsigned long addr)
{
	int i;
	if(!MULTICAST(addr))
		return -EINVAL;
	if(sk->ip_mc_list==NULL)
		return -EADDRNOTAVAIL;

	for(i=0;i<IP_MAX_MEMBERSHIPS;i++)
	{
		if(sk->ip_mc_list->multiaddr[i]==addr && sk->ip_mc_list->multidev[i]==dev)
		{
			sk->ip_mc_list->multidev[i]=NULL;
			ip_mc_dec_group(dev,addr);
			return 0;
		}
	}
	return -EADDRNOTAVAIL;
}

/*
 *	A socket is closing.
 */

void ip_mc_drop_socket(struct sock *sk)
{
	int i;

	if(sk->ip_mc_list==NULL)
		return;

	for(i=0;i<IP_MAX_MEMBERSHIPS;i++)
	{
		if(sk->ip_mc_list->multidev[i])
		{
			ip_mc_dec_group(sk->ip_mc_list->multidev[i], sk->ip_mc_list->multiaddr[i]);
			sk->ip_mc_list->multidev[i]=NULL;
		}
	}
	kfree_s(sk->ip_mc_list,sizeof(*sk->ip_mc_list));
	sk->ip_mc_list=NULL;
}


/*
 *	Write an multicast group list table for the IGMP daemon to
 *	read.
 */
 
int ip_mc_procinfo(char *buffer, char **start, off_t offset, int length, int dummy)
{
	off_t pos=0, begin=0;
	struct ip_mc_list *im;
	unsigned long flags;
	int len=0;
	struct device *dev;
	
	len=sprintf(buffer,"Device    : Count\tGroup    Users Timer\tReporter\n");  
	save_flags(flags);
	cli();
	
	for(dev = dev_base; dev; dev = dev->next)
	{
                if(dev->flags&IFF_UP)
                {
                        len+=sprintf(buffer+len,"%-10s: %5d\n",
					dev->name, dev->mc_count);
                        for(im = dev->ip_mc_list; im; im = im->next)
                        {
                                len+=sprintf(buffer+len,
					"\t\t\t%08lX %5d %d:%08lX\t%d\n",
                                        im->multiaddr, im->users,
					im->tm_running, im->timer.expires-jiffies, im->reporter);
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
	}
	restore_flags(flags);
	*start=buffer+(offset-begin);
	len-=(offset-begin);
	if(len>length)
		len=length;	
	return len;
}

