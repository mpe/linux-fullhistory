/*
 *	DDP:	An implementation of the Appletalk DDP protocol for
 *		ethernet 'ELAP'.
 *
 *		Alan Cox  <Alan.Cox@linux.org>
 *			  <iialan@www.linux.org.uk>
 *
 *		With more than a little assistance from 
 *	
 *		Wesley Craig <netatalk@umich.edu>
 *
 *	Fixes:
 *		Michael Callahan	:	Made routing work
 *		Wesley Craig		:	Fix probing to listen to a
 *						passed node id.
 *		Alan Cox		:	Added send/recvmsg support
 *		Alan Cox		:	Moved at. to protinfo in
 *						socket.
 *		Alan Cox		:	Added firewall hooks.
 *		Alan Cox		:	Supports new ARPHRD_LOOPBACK
 *		Christer Weinigel	: 	Routing and /proc fixes.
 *		Bradford Johnson	:	Localtalk.
 *		Tom Dyas		:	Module support.
 *		Alan Cox		:	Hooks for PPP (based on the
 *						localtalk hook).
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	TODO
 *		ASYNC I/O
 */
 
#include <linux/config.h>
#include <linux/module.h>
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
#include <linux/route.h>
#include <linux/inet.h>
#include <linux/notifier.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/skbuff.h>
#include <linux/termios.h>	/* For TIOCOUTQ/INQ */
#include <net/datalink.h>
#include <net/p8022.h>
#include <net/psnap.h>
#include <net/sock.h>
#include <linux/atalk.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/firewall.h>


#undef APPLETALK_DEBUG


#ifdef APPLETALK_DEBUG
#define DPRINT(x)		print(x)
#else
#define DPRINT(x)
#endif

struct datalink_proto *ddp_dl, *aarp_dl;

#define min(a,b)	(((a)<(b))?(a):(b))

/***********************************************************************************************************************\
*															*
*						Handlers for the socket list.						*
*															*
\***********************************************************************************************************************/

static atalk_socket *volatile atalk_socket_list=NULL;

/*
 *	Note: Sockets may not be removed _during_ an interrupt or inet_bh
 *	handler using this technique. They can be added although we do not
 *	use this facility.
 */
 
static void atalk_remove_socket(atalk_socket *sk)
{
	unsigned long flags;
	atalk_socket *s;
	
	save_flags(flags);
	cli();
	
	s=atalk_socket_list;
	if(s==sk)
	{
		atalk_socket_list=s->next;
		restore_flags(flags);
		return;
	}
	while(s && s->next)
	{
		if(s->next==sk)
		{
			s->next=sk->next;
			restore_flags(flags);
			return;
		}
		s=s->next;
	}
	restore_flags(flags);
}

static void atalk_insert_socket(atalk_socket *sk)
{
	unsigned long flags;
	save_flags(flags);
	cli();
	sk->next=atalk_socket_list;
	atalk_socket_list=sk;
	restore_flags(flags);
}

static atalk_socket *atalk_search_socket(struct sockaddr_at *to, struct atalk_iface *atif)
{
	atalk_socket *s;

	for( s = atalk_socket_list; s != NULL; s = s->next ) 
	{
		if ( to->sat_port != s->protinfo.af_at.src_port ) 
		{
			continue;
	   	}

	    	if ( to->sat_addr.s_net == 0 &&
		    to->sat_addr.s_node == ATADDR_BCAST &&
		    s->protinfo.af_at.src_net == atif->address.s_net ) 
		{
			break;
	    	}

	    	if ( to->sat_addr.s_net == s->protinfo.af_at.src_net &&
		    (to->sat_addr.s_node == s->protinfo.af_at.src_node 
		     ||to->sat_addr.s_node == ATADDR_BCAST ))
		{
			break;
	   	}

	    	/* XXXX.0 */
	}
	return( s );
}

/*
 *	Find a socket in the list.
 */
 
static atalk_socket *atalk_find_socket(struct sockaddr_at *sat)
{
	atalk_socket *s;

	for ( s = atalk_socket_list; s != NULL; s = s->next ) 
	{
		if ( s->protinfo.af_at.src_net != sat->sat_addr.s_net ) 
		{
			continue;
		}
		if ( s->protinfo.af_at.src_node != sat->sat_addr.s_node ) 
		{
			continue;
		}
		if ( s->protinfo.af_at.src_port != sat->sat_port ) 
		{
			continue;
		}
		break;
	}
	return( s );
}

/*
 *	This is only called from user mode. Thus it protects itself against
 *	interrupt users but doesn't worry about being called during work.
 *	Once it is removed from the queue no interrupt or bottom half will
 *	touch it and we are (fairly 8-) ) safe.
 */

static void atalk_destroy_socket(atalk_socket *sk);

/*
 *	Handler for deferred kills.
 */
 
static void atalk_destroy_timer(unsigned long data)
{
	atalk_destroy_socket((atalk_socket *)data);
}

static void atalk_destroy_socket(atalk_socket *sk)
{
	struct sk_buff *skb;
	atalk_remove_socket(sk);
	
	while((skb=skb_dequeue(&sk->receive_queue))!=NULL)
	{
		kfree_skb(skb,FREE_READ);
	}
	
	if(sk->wmem_alloc == 0 && sk->rmem_alloc == 0 && sk->dead)
	{
		sk_free(sk);
		MOD_DEC_USE_COUNT;
	}
	else
	{
		/*
		 *	Someone is using our buffers still.. defer
		 */
		init_timer(&sk->timer);
		sk->timer.expires=jiffies+10*HZ;
		sk->timer.function=atalk_destroy_timer;
		sk->timer.data = (unsigned long)sk;
		add_timer(&sk->timer);
	}
}


/*
 *	Called from proc fs 
 */
 
int atalk_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	atalk_socket *s;
	int len=0;
	off_t pos=0;
	off_t begin=0;

	/*
	 *	Output the appletalk data for the /proc virtual fs.
	 */

	len += sprintf (buffer,"Type local_addr  remote_addr tx_queue rx_queue st uid\n");
	for (s = atalk_socket_list; s != NULL; s = s->next)
	{
		len += sprintf (buffer+len,"%02X   ", s->type);
		len += sprintf (buffer+len,"%04X:%02X:%02X  ",
			ntohs(s->protinfo.af_at.src_net),
			s->protinfo.af_at.src_node,
			s->protinfo.af_at.src_port);
		len += sprintf (buffer+len,"%04X:%02X:%02X  ",
			ntohs(s->protinfo.af_at.dest_net),
			s->protinfo.af_at.dest_node,
			s->protinfo.af_at.dest_port);
		len += sprintf (buffer+len,"%08X:%08X ", s->wmem_alloc, s->rmem_alloc);
		len += sprintf (buffer+len,"%02X %d\n", s->state, SOCK_INODE(s->socket)->i_uid);
		
		/* Are we still dumping unwanted data then discard the record */
		pos=begin+len;
		
		if(pos<offset)
		{
			len=0;			/* Keep dumping into the buffer start */
			begin=pos;
		}
		if(pos>offset+length)		/* We have dumped enough */
			break;
	}
	
	/* The data in question runs from begin to begin+len */
	*start=buffer+(offset-begin);	/* Start of wanted data */
	len-=(offset-begin);		/* Remove unwanted header data from length */
	if(len>length)
		len=length;		/* Remove unwanted tail data from length */
	
	return len;
}

/*******************************************************************************************************************\
*													            *
*	            			Routing tables for the Appletalk socket layer			       	    *
*														    *
\*******************************************************************************************************************/


static struct atalk_route *atalk_router_list=NULL;
static struct atalk_route atrtr_default;		/* For probing devices or in a routerless network */
static struct atalk_iface *atalk_iface_list=NULL;

/*
 *	Appletalk interface control
 */
 
/*
 *	Drop a device. Doesn't drop any of its routes - that is the 
 *	the callers problem. Called when we down the interface or 
 *	delete the address.
 */
 
static void atif_drop_device(struct device *dev)
{
	struct atalk_iface **iface = &atalk_iface_list;
	struct atalk_iface *tmp;

	while ((tmp = *iface) != NULL) 
	{
		if (tmp->dev == dev) 
		{
			*iface = tmp->next;
			kfree_s(tmp, sizeof(struct atalk_iface));
		}
		else
			iface = &tmp->next;
	}
}

static struct atalk_iface *atif_add_device(struct device *dev, struct at_addr *sa)
{
	struct atalk_iface *iface=(struct atalk_iface *)
					kmalloc(sizeof(*iface), GFP_KERNEL);
	unsigned long flags;					
	if(iface==NULL)
		return NULL;
	iface->dev=dev;
	iface->address= *sa;
	iface->status=0;
	save_flags(flags);
	cli();
	iface->next=atalk_iface_list;
	atalk_iface_list=iface;
	restore_flags(flags);
	return iface;
}

/*
 *	Perform phase 2 AARP probing on our tentative address.
 */
 
static int atif_probe_device(struct atalk_iface *atif)
{
	int ct;
	int netrange=ntohs(atif->nets.nr_lastnet)-ntohs(atif->nets.nr_firstnet)+1;
	int probe_net=ntohs(atif->address.s_net);
	int probe_node=atif->address.s_node;
	int netct;
	int nodect;
	
	struct ifreq atreq;
	struct sockaddr_at *sa;
	int err;

/*
 *	THIS IS A HACK: Farallon cards want to do their own picking of
 *	addresses. This needs tidying up post 1.4, but we need it in 
 *	now for the 1.4 release as is.
 *
 */
	if((atif->dev->type == ARPHRD_LOCALTLK || atif->dev->type == ARPHRD_PPP)
		&& atif->dev->do_ioctl) 
	{
		/* fake up the request and pass it down */
		sa = (struct sockaddr_at*)&atreq.ifr_addr;
		sa->sat_addr.s_node = probe_node;
        	sa->sat_addr.s_net = probe_net;
		if (!(err=atif->dev->do_ioctl(atif->dev,&atreq,SIOCSIFADDR)))
		{
			(void)atif->dev->do_ioctl(atif->dev,&atreq,SIOCGIFADDR);
			atif->address.s_net=htons(sa->sat_addr.s_net);
			atif->address.s_node=sa->sat_addr.s_node;
			return 0;
		}
		/*
		 *	If it didn't like our faked request then fail:
		 *	This should check against -ENOIOCTLCMD and fall
		 *	through. That needs us to fix all the devices up
		 *	properly. We can then also dump the localtalk test.
		 */
		return err;
	}	
	/*
	 *	Offset the network we start probing with.
	 */
	 
	if(probe_net==ATADDR_ANYNET)
	{
		if(!netrange)
			probe_net=ntohs(atif->nets.nr_firstnet);
		else
			probe_net=ntohs(atif->nets.nr_firstnet) + (jiffies%netrange);
	}
	
	if(probe_node == ATADDR_ANYNODE)
		probe_node = jiffies&0xFF;
	
	
	/*
	 *	Scan the networks.
	 */
	 
	for(netct=0;netct<=netrange;netct++)
	{
		/*
		 *	Sweep the available nodes from a given start.
		 */

		atif->address.s_net=htons(probe_net);
		for(nodect=0;nodect<256;nodect++)
		{
			atif->address.s_node=((nodect+probe_node)&0xFF);
			if(atif->address.s_node>0&&atif->address.s_node<254)
			{
				/*
				 *	Probe a proposed address.
				 */
				for(ct=0;ct<AARP_RETRANSMIT_LIMIT;ct++)
				{
					aarp_send_probe(atif->dev, &atif->address);
					/*
					 *	Defer 1/10th
					 */
					current->timeout = jiffies + (HZ/10);
					current->state = TASK_INTERRUPTIBLE;
					schedule();
					if(atif->status&ATIF_PROBE_FAIL)
						break;
				}
				if(!(atif->status&ATIF_PROBE_FAIL))
					return 0;
			}
			atif->status&=~ATIF_PROBE_FAIL;
		}
		probe_net++;
		if(probe_net>ntohs(atif->nets.nr_lastnet))
			probe_net=ntohs(atif->nets.nr_firstnet);
	}
	return -EADDRINUSE;	/* Network is full... */
}

struct at_addr *atalk_find_dev_addr(struct device *dev)
{
	struct atalk_iface *iface;
	for(iface=atalk_iface_list;iface!=NULL;iface=iface->next)
		if(iface->dev==dev)
			return &iface->address;
	return NULL;
}

static struct at_addr *atalk_find_primary(void)
{
	struct atalk_iface *iface;
	for(iface=atalk_iface_list;iface!=NULL;iface=iface->next)
		if(!(iface->dev->flags&IFF_LOOPBACK))
			return &iface->address;
	if ( atalk_iface_list != NULL ) {
	    return &atalk_iface_list->address;
	} else {
	    return NULL;
	}
}

/*
 *	Give a device find its atif control structure
 */
 
struct atalk_iface *atalk_find_dev(struct device *dev)
{
	struct atalk_iface *iface;
	for(iface=atalk_iface_list;iface!=NULL;iface=iface->next)
		if(iface->dev==dev)
			return iface;
	return NULL;
}

/*
 *	Find a match for 'any network' - ie any of our interfaces with that
 *	node number will do just nicely.
 */
 
static struct atalk_iface *atalk_find_anynet(int node, struct device *dev)
{
	struct atalk_iface *iface;
	for(iface=atalk_iface_list;iface!=NULL;iface=iface->next) 
	{
		if ( iface->dev != dev || ( iface->status & ATIF_PROBE ))
			continue;
		if ( node == ATADDR_BCAST || iface->address.s_node == node ) 
			return iface;
	}
	return NULL;
}

/*
 *	Find a match for a specific network:node pair
 */
 
static struct atalk_iface *atalk_find_interface(int net, int node)
{
	struct atalk_iface *iface;
	for(iface=atalk_iface_list;iface!=NULL;iface=iface->next)
	{
		if((node==ATADDR_BCAST || iface->address.s_node==node) 
			&& iface->address.s_net==net && !(iface->status&ATIF_PROBE))
			return iface;
	}
	return NULL;
}


/*
 *	Find a route for an appletalk packet. This ought to get cached in
 *	the socket (later on...). We know about host routes and the fact
 *	that a route must be direct to broadcast.
 */
 
static struct atalk_route *atrtr_find(struct at_addr *target)
{
	struct atalk_route *r;
	for(r=atalk_router_list;r!=NULL;r=r->next)
	{
		if(!(r->flags&RTF_UP))
			continue;
		if(r->target.s_net==target->s_net)
		{
			if(!(r->flags&RTF_HOST) || r->target.s_node==target->s_node)
				return r;
		}
	}
	if(atrtr_default.dev)
		return &atrtr_default;
	return NULL;
}

			
/*
 *	Given an appletalk network find the device to use. This can be
 *	a simple lookup. Funny stuff like routers can wait 8)
 */
 
static struct device *atrtr_get_dev(struct at_addr *sa)
{
	struct atalk_route *atr=atrtr_find(sa);
	if(atr==NULL)
		return NULL;
	else
		return atr->dev;
}

/*
 *	Set up a default router.
 */
 
static void atrtr_set_default(struct device *dev)
{
	atrtr_default.dev=dev;
	atrtr_default.flags= RTF_UP;
	atrtr_default.gateway.s_net=htons(0);
	atrtr_default.gateway.s_node=0;
}

/*
 *	Add a router. Basically make sure it looks valid and stuff the
 *	entry in the list. While it uses netranges we always set them to one
 *	entry to work like netatalk.
 */
 
static int atrtr_create(struct rtentry *r, struct device *devhint)
{
	struct sockaddr_at *ta=(struct sockaddr_at *)&r->rt_dst;
	struct sockaddr_at *ga=(struct sockaddr_at *)&r->rt_gateway;
	struct atalk_route *rt;
	struct atalk_iface *iface, *riface;
	unsigned long flags;
	
	save_flags(flags);
	
	/*
	 *	Fixme: Raise/Lower a routing change semaphore for these
	 *	operations.
	 */
	 
	/*
	 *	Validate the request
	 */	
	if(ta->sat_family!=AF_APPLETALK)
		return -EINVAL;
	if(devhint == NULL && ga->sat_family != AF_APPLETALK)
		return -EINVAL;
	
	/*
	 *	Now walk the routing table and make our decisions
	 */

	for(rt=atalk_router_list;rt!=NULL;rt=rt->next)
	{
		if(r->rt_flags != rt->flags)
			continue;

		if(ta->sat_addr.s_net == rt->target.s_net) 
		{
			if(!(rt->flags&RTF_HOST))
				break;
			if(ta->sat_addr.s_node == rt->target.s_node)
				break;
		}
	}

	if ( devhint == NULL ) 
	{
		for ( riface = NULL, iface = atalk_iface_list; iface; iface = iface->next ) 
		{
			if ( riface == NULL && ntohs( ga->sat_addr.s_net ) >= ntohs( iface->nets.nr_firstnet ) &&
				ntohs( ga->sat_addr.s_net ) <=	ntohs( iface->nets.nr_lastnet ))
			{
				riface = iface;
			}
			if ( ga->sat_addr.s_net == iface->address.s_net && ga->sat_addr.s_node == iface->address.s_node )
				riface = iface;
		}
		if ( riface == NULL )
			return -ENETUNREACH;
		devhint = riface->dev;
	}

	if(rt==NULL)
	{
		rt=(struct atalk_route *)kmalloc(sizeof(struct atalk_route), GFP_KERNEL);
		if(rt==NULL)
			return -ENOBUFS;
		cli();
		rt->next=atalk_router_list;
		atalk_router_list=rt;
	}

	/*
	 *	Fill in the entry.
	 */
	rt->target=ta->sat_addr;			
	rt->dev=devhint;
	rt->flags=r->rt_flags;
	rt->gateway=ga->sat_addr;
	
	restore_flags(flags);
	return 0;
}


/*
 *	Delete a route. Find it and discard it.
 */
 
static int atrtr_delete( struct at_addr *addr )
{
	struct atalk_route **r = &atalk_router_list;
	struct atalk_route *tmp;

	while ((tmp = *r) != NULL) 
	{
		if (tmp->target.s_net == addr->s_net &&
			    (!(tmp->flags&RTF_GATEWAY) ||
			    tmp->target.s_node == addr->s_node )) 
		{
			*r = tmp->next;
			kfree_s(tmp, sizeof(struct atalk_route));
			return 0;
		}
		r = &tmp->next;
	}
	return -ENOENT;
}

/*
 *	Called when a device is downed. Just throw away any routes
 *	via it.
 */
 
void atrtr_device_down(struct device *dev)
{
	struct atalk_route **r = &atalk_router_list;
	struct atalk_route *tmp;

	while ((tmp = *r) != NULL) 
	{
		if (tmp->dev == dev) 
		{
			*r = tmp->next;
			kfree_s(tmp, sizeof(struct atalk_route));
		}
		else
			r = &tmp->next;
	}
	if(atrtr_default.dev==dev)
		atrtr_set_default(NULL);
}

/*
 *	A device event has occurred. Watch for devices going down and
 *	delete our use of them (iface and route).
 */

static int ddp_device_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	if(event==NETDEV_DOWN)
	{
		/* Discard any use of this */
		atrtr_device_down((struct device *)ptr);
		atif_drop_device((struct device *)ptr);
	}
	return NOTIFY_DONE;
}

/*
 *	ioctl calls. Shouldn't even need touching.
 */

/*
 *	Device configuration ioctl calls.
 */
 
int atif_ioctl(int cmd, void *arg)
{
	struct ifreq atreq;
	static char aarp_mcast[6]={0x09,0x00,0x00,0xFF,0xFF,0xFF};
	struct netrange *nr;
	struct sockaddr_at *sa;
	struct device *dev;
	struct atalk_iface *atif;
	int ro=(cmd==SIOCSIFADDR);
	int err=verify_area(ro?VERIFY_READ:VERIFY_WRITE, arg,sizeof(atreq));
	int ct;
	int limit;
	struct rtentry rtdef;
	
	if(err)
		return err;
	
	memcpy_fromfs(&atreq,arg,sizeof(atreq));
	
	if((dev=dev_get(atreq.ifr_name))==NULL)
		return -ENODEV;
		
	sa=(struct sockaddr_at*)&atreq.ifr_addr;
	atif=atalk_find_dev(dev);
	
	switch(cmd)
	{
		case SIOCSIFADDR:
			if(!suser())
				return -EPERM;
			if(sa->sat_family!=AF_APPLETALK)
				return -EINVAL;
			if(dev->type!=ARPHRD_ETHER&&dev->type!=ARPHRD_LOOPBACK
				&&dev->type!=ARPHRD_LOCALTLK && dev->type!=ARPHRD_PPP)
				return -EPROTONOSUPPORT;
			nr=(struct netrange *)&sa->sat_zero[0];
			/*
			 *	Phase 1 is fine on localtalk but we don't
			 *	do Ethertalk phase 1. Anyone wanting to add
			 *	it go ahead.
			 */
			if(dev->type==ARPHRD_ETHER && nr->nr_phase!=2)
				return -EPROTONOSUPPORT;
			if(sa->sat_addr.s_node==ATADDR_BCAST || sa->sat_addr.s_node == 254)
				return -EINVAL;
			if(atif)
			{
				/*
				 *	Already setting address.
				 */
				if(atif->status&ATIF_PROBE)
					return -EBUSY;
					
				atif->address.s_net=sa->sat_addr.s_net;
				atif->address.s_node=sa->sat_addr.s_node;
				atrtr_device_down(dev);	/* Flush old routes */
			}
			else
			{
				atif=atif_add_device(dev, &sa->sat_addr);
			}
			atif->nets= *nr;

			/*
			 *	Check if the chosen address is used. If so we 
			 *	error and atalkd will try another. 
			 */
			 
			if(!(dev->flags&IFF_LOOPBACK) && atif_probe_device(atif)<0)
			{
				atif_drop_device(dev);
				return -EADDRINUSE;
			}

			/*
			 *	Hey it worked - add the direct
			 *	routes.
			 */
				
			sa=(struct sockaddr_at *)&rtdef.rt_gateway;
			sa->sat_family=AF_APPLETALK;
			sa->sat_addr.s_net=atif->address.s_net;
			sa->sat_addr.s_node=atif->address.s_node;
			sa=(struct sockaddr_at *)&rtdef.rt_dst;
			rtdef.rt_flags=RTF_UP;
			sa->sat_family=AF_APPLETALK;
			sa->sat_addr.s_node=ATADDR_ANYNODE;
			if(dev->flags&IFF_LOOPBACK)
				rtdef.rt_flags|=RTF_HOST;
			/*
			 *	Routerless initial state.
			 */
			if(nr->nr_firstnet==htons(0) && nr->nr_lastnet==htons(0xFFFE)) 
			{
				sa->sat_addr.s_net=atif->address.s_net;
				atrtr_create(&rtdef, dev);
				atrtr_set_default(dev);
			} 
			else 
			{
				limit=ntohs(nr->nr_lastnet);
				if(limit-ntohs(nr->nr_firstnet) > 256)
				{
					printk(KERN_WARNING "Too many routes/iface.\n");
					return -EINVAL;
				}
				for(ct=ntohs(nr->nr_firstnet);ct<=limit;ct++)
				{
					sa->sat_addr.s_net=htons(ct);
					atrtr_create(&rtdef, dev);
				}
			}
			dev_mc_add(dev, aarp_mcast, 6, 1);
			return 0;
		case SIOCGIFADDR:
			if(atif==NULL)
				return -EADDRNOTAVAIL;
			((struct sockaddr_at *)(&atreq.ifr_addr))->sat_family=AF_APPLETALK;
			((struct sockaddr_at *)(&atreq.ifr_addr))->sat_addr=atif->address;
			break;
		case SIOCGIFBRDADDR:
			if(atif==NULL)
				return -EADDRNOTAVAIL;
			((struct sockaddr_at *)(&atreq.ifr_addr))->sat_family=AF_APPLETALK;
			((struct sockaddr_at *)(&atreq.ifr_addr))->sat_addr.s_net=atif->address.s_net;
			((struct sockaddr_at *)(&atreq.ifr_addr))->sat_addr.s_node=ATADDR_BCAST;
			break;
	}
	memcpy_tofs(arg,&atreq,sizeof(atreq));
	return 0;
}

/*
 *	Routing ioctl() calls
 */
  
static int atrtr_ioctl(unsigned int cmd, void *arg)
{
	int err;
	struct rtentry rt;
	
	err=verify_area(VERIFY_READ, arg, sizeof(rt));
	if(err)
		return err;
	memcpy_fromfs(&rt,arg,sizeof(rt));
	
	switch(cmd)
	{
		case SIOCDELRT:
			if(rt.rt_dst.sa_family!=AF_APPLETALK)
				return -EINVAL;
			return atrtr_delete(&((struct sockaddr_at *)&rt.rt_dst)->sat_addr);
		case SIOCADDRT:
			return atrtr_create(&rt, NULL);
		default:
			return -EINVAL;
	}
}

/* Called from proc fs - just make it print the ifaces neatly */

int atalk_if_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	struct atalk_iface *iface;
	int len=0;
	off_t pos=0;
	off_t begin=0;

	len += sprintf (buffer,"Interface	  Address   Networks   Status\n");
	for (iface = atalk_iface_list; iface != NULL; iface = iface->next)
	{
		len += sprintf (buffer+len,"%-16s %04X:%02X  %04X-%04X  %d\n",
			iface->dev->name,
			ntohs(iface->address.s_net),iface->address.s_node,
			ntohs(iface->nets.nr_firstnet),ntohs(iface->nets.nr_lastnet),
			iface->status);
		pos=begin+len;
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

/* Called from proc fs - just make it print the routes neatly */

int atalk_rt_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	struct atalk_route *rt;
	int len=0;
	off_t pos=0;
	off_t begin=0;

	len += sprintf (buffer,"Target        Router  Flags Dev\n");
	if(atrtr_default.dev)
	{
		rt=&atrtr_default;
		len += sprintf (buffer+len,"Default     %04X:%02X  %-4d  %s\n",
			ntohs(rt->gateway.s_net), rt->gateway.s_node, rt->flags,
			rt->dev->name);
	}
	for (rt = atalk_router_list; rt != NULL; rt = rt->next)
	{
		len += sprintf (buffer+len,"%04X:%02X     %04X:%02X  %-4d  %s\n",
			ntohs(rt->target.s_net),rt->target.s_node,
			ntohs(rt->gateway.s_net), rt->gateway.s_node, rt->flags,
			rt->dev->name);
		pos=begin+len;
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

/*******************************************************************************************************************\
*													            *
*	      Handling for system calls applied via the various interfaces to an Appletalk socket object	    *
*														    *
\*******************************************************************************************************************/

/*
 *	Checksum: This is 'optional'. It's quite likely also a good
 *	candidate for assembler hackery 8)
 */
 
unsigned short atalk_checksum(struct ddpehdr *ddp, int len)
{
	unsigned long sum=0;	/* Assume unsigned long is >16 bits */
	unsigned char *data=(unsigned char *)ddp;
	
	len-=4;		/* skip header 4 bytes */
	data+=4;	
	
	/* This ought to be unwrapped neatly. I'll trust gcc for now */
	while(len--)
	{
		sum+=*data;
		sum<<=1;
		if(sum&0x10000)
		{
			sum++;
			sum&=0xFFFF;
		}
		data++;
	}
	if(sum)
		return htons((unsigned short)sum);
	return 0xFFFF;		/* Use 0xFFFF for 0. 0 itself means none */
}
	
/*
 *	Generic fcntl calls are already dealt with. If we don't need funny ones
 *	this is the all you need. Async I/O is also separate.
 */
  
static int atalk_fcntl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
/*	atalk_socket *sk=(atalk_socket *)sock->data;*/
	switch(cmd)
	{
		default:
			return(-EINVAL);
	}
}

/*
 *	Set 'magic' options for appletalk. If we don't have any this is fine 
 *	as it is.
 */
 
static int atalk_setsockopt(struct socket *sock, int level, int optname, char *optval, int optlen)
{
	atalk_socket *sk;
	int err,opt;
	
	sk=(atalk_socket *)sock->data;
	
	if(optval==NULL)
		return(-EINVAL);

	err=verify_area(VERIFY_READ,optval,sizeof(int));
	if(err)
		return err;
	opt=get_fs_long((unsigned long *)optval);
	
	switch(level)
	{
		case SOL_ATALK:
			switch(optname)
			{
				default:
					return -EOPNOTSUPP;
			}
			break;
			
		case SOL_SOCKET:
			return sock_setsockopt(sk,level,optname,optval,optlen);

		default:
			return -EOPNOTSUPP;
	}
}


/*
 *	Get any magic options. Comment above applies.
 */
 
static int atalk_getsockopt(struct socket *sock, int level, int optname,
	char *optval, int *optlen)
{
	atalk_socket *sk;
	int val=0;
	int err;
	
	sk=(atalk_socket *)sock->data;

	switch(level)
	{

		case SOL_ATALK:
			switch(optname)
			{
				default:
					return -ENOPROTOOPT;
			}
			break;
			
		case SOL_SOCKET:
			return sock_getsockopt(sk,level,optname,optval,optlen);
			
		default:
			return -EOPNOTSUPP;
	}
	err=verify_area(VERIFY_WRITE,optlen,sizeof(int));
	if(err)
		return err;
	put_user(sizeof(int),optlen);
	err=verify_area(VERIFY_WRITE,optval,sizeof(int));
	if (err) 
		return err;
	put_user(val,optval);
	return(0);
}

/*
 *	Only for connection oriented sockets - ignore
 */
 
static int atalk_listen(struct socket *sock, int backlog)
{
	return -EOPNOTSUPP;
}

/*
 *	These are standard.
 */
 
static void def_callback1(struct sock *sk)
{
	if(!sk->dead)
		wake_up_interruptible(sk->sleep);
}

static void def_callback2(struct sock *sk, int len)
{
	if(!sk->dead)
	{
		wake_up_interruptible(sk->sleep);
		sock_wake_async(sk->socket,0);
	}
}

/*
 *	Create a socket. Initialise the socket, blank the addresses
 *	set the state.
 */
 
static int atalk_create(struct socket *sock, int protocol)
{
	atalk_socket *sk;
	sk=(atalk_socket *)sk_alloc(GFP_KERNEL);
	if(sk==NULL)
		return(-ENOMEM);
	switch(sock->type)
	{
		/* This RAW is an extension. It is trivial to do and gives you
		   the full ELAP frame. Should be handy for CAP 8) */
		case SOCK_RAW:
		/* We permit DDP datagram sockets */
		case SOCK_DGRAM:
			break;
		default:
			sk_free((void *)sk);
			return(-ESOCKTNOSUPPORT);
	}

	MOD_INC_USE_COUNT;

	sk->no_check=0;		/* Checksums on by default */
	sk->allocation=GFP_KERNEL;
	sk->rcvbuf=SK_RMEM_MAX;
	sk->sndbuf=SK_WMEM_MAX;
	sk->pair=NULL;
	sk->priority=1;
	skb_queue_head_init(&sk->receive_queue);
	skb_queue_head_init(&sk->write_queue);
	skb_queue_head_init(&sk->back_log);
	sk->state=TCP_CLOSE;
	sk->socket=sock;
	sk->type=sock->type;
	
	sk->mtu=DDP_MAXSZ;
	
	if(sock!=NULL)
	{
		sock->data=(void *)sk;
		sk->sleep=sock->wait;
	}
	
	sk->state_change=def_callback1;
	sk->data_ready=def_callback2;
	sk->write_space=def_callback1;
	sk->error_report=def_callback1;

	sk->zapped=1;
	return(0);
}

/*
 *	Copy a socket. No work needed.
 */
 
static int atalk_dup(struct socket *newsock,struct socket *oldsock)
{
	return(atalk_create(newsock,SOCK_DGRAM));
}

/*
 *	Free a socket. No work needed
 */
 
static int atalk_release(struct socket *sock, struct socket *peer)
{
	atalk_socket *sk=(atalk_socket *)sock->data;
	if(sk==NULL)
		return(0);
	if(!sk->dead)
		sk->state_change(sk);
	sk->dead=1;
	sock->data=NULL;
	atalk_destroy_socket(sk);
	return(0);
}
		
/*
 *	Pick a source address if one is not given. Just return
 *	an error if not supportable.
 */
 
static int atalk_pick_port(struct sockaddr_at *sat)
{
	for ( sat->sat_port = ATPORT_RESERVED; sat->sat_port < ATPORT_LAST; sat->sat_port++ )
	{
		if ( atalk_find_socket( sat ) == NULL )
			return sat->sat_port;
	}
	return -EBUSY;
}
 		
static int atalk_autobind(atalk_socket *sk)
{
	struct at_addr *ap = atalk_find_primary();
	struct sockaddr_at sat;
	int n;

	if ( ap == NULL || ap->s_net == htons( ATADDR_ANYNET ))
		return -EADDRNOTAVAIL;
	sk->protinfo.af_at.src_net = sat.sat_addr.s_net = ap->s_net;
	sk->protinfo.af_at.src_node = sat.sat_addr.s_node = ap->s_node;

	if (( n = atalk_pick_port( &sat )) < 0 )
		return( n );
	sk->protinfo.af_at.src_port=n;
	atalk_insert_socket(sk);
	sk->zapped=0;
	return 0;
}

/*
 *	Set the address 'our end' of the connection.
 */
 
static int atalk_bind(struct socket *sock, struct sockaddr *uaddr,int addr_len)
{
	atalk_socket *sk;
	struct sockaddr_at *addr=(struct sockaddr_at *)uaddr;
	
	sk=(atalk_socket *)sock->data;
	
	if(sk->zapped==0)
		return(-EIO);
		
	if(addr_len!=sizeof(struct sockaddr_at))
		return -EINVAL;

	if(addr->sat_family!=AF_APPLETALK)
		return -EAFNOSUPPORT;

	if(addr->sat_addr.s_net==htons(ATADDR_ANYNET))
	{
		struct at_addr *ap=atalk_find_primary();
		if(ap==NULL)
			return -EADDRNOTAVAIL;
		sk->protinfo.af_at.src_net=addr->sat_addr.s_net=ap->s_net;
		sk->protinfo.af_at.src_node=addr->sat_addr.s_node=ap->s_node;
	}
	else
	{			
		if ( atalk_find_interface( addr->sat_addr.s_net, addr->sat_addr.s_node ) == NULL )
			return -EADDRNOTAVAIL;
		sk->protinfo.af_at.src_net=addr->sat_addr.s_net;
		sk->protinfo.af_at.src_node=addr->sat_addr.s_node;
	}

	if(addr->sat_port == ATADDR_ANYPORT)
	{
		int n = atalk_pick_port(addr);
		if(n < 0)
			return n;
		sk->protinfo.af_at.src_port=addr->sat_port=n;
	}
	else
		sk->protinfo.af_at.src_port=addr->sat_port;

	if(atalk_find_socket(addr)!=NULL)
		return -EADDRINUSE;	   

	atalk_insert_socket(sk);
	sk->zapped=0;
	return(0);
}

/*
 *	Set the address we talk to.
 */
 
static int atalk_connect(struct socket *sock, struct sockaddr *uaddr,
	int addr_len, int flags)
{
	atalk_socket *sk=(atalk_socket *)sock->data;
	struct sockaddr_at *addr;
	
	sk->state = TCP_CLOSE;	
	sock->state = SS_UNCONNECTED;

	if(addr_len!=sizeof(*addr))
		return(-EINVAL);
	addr=(struct sockaddr_at *)uaddr;
	
	if(addr->sat_family!=AF_APPLETALK)
		return -EAFNOSUPPORT;
#if 0 	/* Netatalk doesn't check this */
	if(addr->sat_addr.s_node==ATADDR_BCAST && !sk->broadcast)
		return -EPERM;
#endif		
	if(sk->zapped)
	{
		if(atalk_autobind(sk)<0)
			return -EBUSY;
	}	
	
	if(atrtr_get_dev(&addr->sat_addr)==NULL)
		return -ENETUNREACH;
		
	sk->protinfo.af_at.dest_port=addr->sat_port;
	sk->protinfo.af_at.dest_net=addr->sat_addr.s_net;
	sk->protinfo.af_at.dest_node=addr->sat_addr.s_node;
	sock->state = SS_CONNECTED;
	sk->state=TCP_ESTABLISHED;
	return(0);
}

/*
 *	Not relevant
 */
 
static int atalk_socketpair(struct socket *sock1, struct socket *sock2)
{
	return(-EOPNOTSUPP);
}

/*
 *	Not relevant
 */
 
static int atalk_accept(struct socket *sock, struct socket *newsock, int flags)
{
	if(newsock->data)
		sk_free(newsock->data);
	return -EOPNOTSUPP;
}

/*
 *	Find the name of an appletalk socket. Just copy the right
 *	fields into the sockaddr.
 */
 
static int atalk_getname(struct socket *sock, struct sockaddr *uaddr,
	int *uaddr_len, int peer)
{
	struct sockaddr_at sat;
	atalk_socket *sk;
	
	sk=(atalk_socket *)sock->data;
	if(sk->zapped)
	{
		if(atalk_autobind(sk)<0)
			return -EBUSY;
	}	
	
	*uaddr_len = sizeof(struct sockaddr_at);
		
	if(peer)
	{
		if(sk->state!=TCP_ESTABLISHED)
			return -ENOTCONN;
		sat.sat_addr.s_net=sk->protinfo.af_at.dest_net;
		sat.sat_addr.s_node=sk->protinfo.af_at.dest_node;
		sat.sat_port=sk->protinfo.af_at.dest_port;
	}
	else
	{
		sat.sat_addr.s_net=sk->protinfo.af_at.src_net;
		sat.sat_addr.s_node=sk->protinfo.af_at.src_node;
		sat.sat_port=sk->protinfo.af_at.src_port;
	}
	sat.sat_family = AF_APPLETALK;
	memcpy(uaddr,&sat,sizeof(sat));
	return(0);
}

/*
 *	Receive a packet (in skb) from device dev. This has come from the SNAP decoder, and on entry
 *	skb->h.raw is the DDP header, skb->len is the DDP length. The physical headers have been 
 *	extracted. PPP should probably pass frames marked as for this layer
 *	[ie ARPHRD_ETHERTALK]
 */
 
static int atalk_rcv(struct sk_buff *skb, struct device *dev, struct packet_type *pt)
{
	atalk_socket *sock;
	struct ddpehdr *ddp=(void *)skb->h.raw;
	struct atalk_iface *atif;
	struct sockaddr_at tosat;
        int origlen;
	
	/* Size check */
	if(skb->len<sizeof(*ddp))
	{
		kfree_skb(skb,FREE_READ);
		return(0);
	}
	
	
	/*
	 *	Fix up the length field	[Ok this is horrible but otherwise
	 *	I end up with unions of bit fields and messy bit field order
	 *	compiler/endian dependencies..]
	 */

	*((__u16 *)ddp)=ntohs(*((__u16 *)ddp));

	/*
	 *	Trim buffer in case of stray trailing data
	 */
	   
	origlen = skb->len;
	
	skb_trim(skb,min(skb->len,ddp->deh_len));

	/*
	 *	Size check to see if ddp->deh_len was crap
	 *	(Otherwise we'll detonate most spectacularly
	 *	 in the middle of recvmsg()).
	 */
	 
	if(skb->len<sizeof(*ddp))
	{
		kfree_skb(skb,FREE_READ);
		return(0);
	}

	/*
	 *	Any checksums. Note we don't do htons() on this == is assumed to be
	 *	valid for net byte orders all over the networking code... 
	 */

	if(ddp->deh_sum && atalk_checksum(ddp, ddp->deh_len)!= ddp->deh_sum)
	{
		/* Not a valid appletalk frame - dustbin time */
		kfree_skb(skb,FREE_READ);
		return(0);
	}

#ifdef CONFIG_FIREWALL
	
	if(call_in_firewall(AF_APPLETALK, skb->dev, ddp, NULL)!=FW_ACCEPT)
	{
		kfree_skb(skb, FREE_READ);
		return 0;
	}
	
#endif	
	
	/* Check the packet is aimed at us */

	if(ddp->deh_dnet == 0)	/* Net 0 is 'this network' */
		atif=atalk_find_anynet(ddp->deh_dnode, dev);
	else
		atif=atalk_find_interface(ddp->deh_dnet,ddp->deh_dnode);

	/* Not ours */
	if(atif==NULL)		
	{
		struct atalk_route *rt;
		struct at_addr ta;

		/* Don't route multicast, etc., packets, or packets
		   sent to "this network" */
		if (skb->pkt_type != PACKET_HOST || ddp->deh_dnet == 0) 
		{
			kfree_skb(skb, FREE_READ);
			return(0);
		}
		
#ifdef CONFIG_FIREWALL		
		/*
		 *	Check firewall allows this routing
		 */
		
		if(call_fw_firewall(AF_APPLETALK, skb->dev, ddp, NULL)!=FW_ACCEPT)
		{
			kfree_skb(skb, FREE_READ);
			return(0);
		}
#endif
		ta.s_net=ddp->deh_dnet;
		ta.s_node=ddp->deh_dnode;

		/* Route the packet */
		rt=atrtr_find(&ta);
		if(rt==NULL || ddp->deh_hops==15)
		{
			kfree_skb(skb, FREE_READ);
			return(0);
		}
		ddp->deh_hops++;

		/*
		 *      Route goes through another gateway, so
		 *      set the target to the gateway instead.
		 */
 
		if(rt->flags&RTF_GATEWAY)
		{
			ta.s_net = rt->gateway.s_net;
			ta.s_node = rt->gateway.s_node;
		}

                /* Fix up skb->len field */
                skb_trim(skb,min(origlen, rt->dev->hard_header_len + 
			ddp_dl->header_length + ddp->deh_len));

		*((__u16 *)ddp)=ntohs(*((__u16 *)ddp));		/* Mend the byte order */
		/*
		 *	Send the buffer onwards
		 */
		 
		skb->arp = 1;	/* Resolved */
		
		if(aarp_send_ddp(rt->dev, skb, &ta, NULL)==-1)
			kfree_skb(skb, FREE_READ);
		return 0;
	}

	/* Which socket - atalk_search_socket() looks for a *full match*
	   of the <net,node,port> tuple */
	tosat.sat_addr.s_net = ddp->deh_dnet;
	tosat.sat_addr.s_node = ddp->deh_dnode;
	tosat.sat_port = ddp->deh_dport;

	sock=atalk_search_socket( &tosat, atif );
	
	if(sock==NULL)	/* But not one of our sockets */
	{
		kfree_skb(skb,FREE_READ);
		return(0);
	}

	
	/*
	 *	Queue packet (standard)
	 */
	 
	skb->sk = sock;

	if(sock_queue_rcv_skb(sock,skb)<0)
	{
		skb->sk=NULL;
		kfree_skb(skb, FREE_WRITE);
	}
	return(0);
}

/*
 *	Receive a localtalk frame. We make some demands on the caller here.
 *	Caller must provide enough headroom on the packet to pull the short
 *	header and append a long one.
 */

 
static int ltalk_rcv(struct sk_buff *skb, struct device *dev, struct packet_type *pt)
{
	struct ddpehdr *ddp;
	struct at_addr *ap;
	/*
	 *	Expand any short form frames.
	 */
	 
	if(skb->mac.raw[2]==1)
	{
		/*
		 *	Find our address.
		 */
		 
		ap=atalk_find_dev_addr(dev);
		if(ap==NULL || skb->len<sizeof(struct ddpshdr))
		{
			kfree_skb(skb, FREE_READ);
			return 0;
		}
	
		/*
		 *	The push leaves us with a ddephdr not an shdr, and
		 *	handily the port bytes in the right place preset.
		 */
	 
		skb_push(skb, sizeof(*ddp)-4);
		ddp=(struct ddpehdr *)skb->data;
	
		/*
		 *	Now fill in the long header.
		 */
	 
	 	/*
	 	 *	These two first. The mac overlays the new source/dest
	 	 *	network information so we MUST copy these before
	 	 *	we write the network numbers !
	 	 */
	 	 
		ddp->deh_dnode=skb->mac.raw[0];	/* From physical header */
		ddp->deh_snode=skb->mac.raw[1];	/* From physical header */
		
		ddp->deh_dnet=ap->s_net;	/* Network number */
		ddp->deh_snet=ap->s_net;	
		ddp->deh_sum=0;			/* No checksum */
		/*
		 *	Not sure about this bit...
		 */
		ddp->deh_len=skb->len;
		ddp->deh_hops=15;		/* Non routable, so force a drop 
						   if we slip up later */
		*((__u16 *)ddp)=htons(*((__u16 *)ddp));		/* Mend the byte order */
	}
	skb->h.raw = skb->data;
	return atalk_rcv(skb,dev,pt);
}

static int atalk_sendmsg(struct socket *sock, struct msghdr *msg, int len, int nonblock, int flags)
{
	atalk_socket *sk=(atalk_socket *)sock->data;
	struct sockaddr_at *usat=(struct sockaddr_at *)msg->msg_name;
	struct sockaddr_at local_satalk, gsat;
	struct sk_buff *skb;
	struct device *dev;
	struct ddpehdr *ddp;
	int size;
	struct atalk_route *rt;
	int loopback=0;
	int err;
	
	if(flags)
		return -EINVAL;
		
	if(len>587)
		return -EMSGSIZE;
		
	if(usat)
	{
		if(sk->zapped)
		/* put the autobinding in */
		{
			if(atalk_autobind(sk)<0)
				return -EBUSY;
		}

		if(msg->msg_namelen <sizeof(*usat))
			return(-EINVAL);
		if(usat->sat_family != AF_APPLETALK)
			return -EINVAL;
#if 0 	/* netatalk doesn't implement this check */
		if(usat->sat_addr.s_node==ATADDR_BCAST && !sk->broadcast)
			return -EPERM;
#endif			
	}
	else
	{
		if(sk->state!=TCP_ESTABLISHED)
			return -ENOTCONN;
		usat=&local_satalk;
		usat->sat_family=AF_APPLETALK;
		usat->sat_port=sk->protinfo.af_at.dest_port;
		usat->sat_addr.s_node=sk->protinfo.af_at.dest_node;
		usat->sat_addr.s_net=sk->protinfo.af_at.dest_net;
	}
	
	/* Build a packet */
	
	if(sk->debug)
		printk("SK %p: Got address.\n",sk);
	
	size=sizeof(struct ddpehdr)+len+ddp_dl->header_length;	/* For headers */

	if(usat->sat_addr.s_net!=0 || usat->sat_addr.s_node == ATADDR_ANYNODE)
	{
		rt=atrtr_find(&usat->sat_addr);
		if(rt==NULL)
			return -ENETUNREACH;	
		dev=rt->dev;
	}
	else
	{
		struct at_addr at_hint;
		at_hint.s_node=0;
		at_hint.s_net=sk->protinfo.af_at.src_net;
		rt=atrtr_find(&at_hint);
		if(rt==NULL)
			return -ENETUNREACH;
		dev=rt->dev;
	}

	if(sk->debug)
		printk("SK %p: Size needed %d, device %s\n", sk, size, dev->name);
	
	size += dev->hard_header_len;

	skb = sock_alloc_send_skb(sk, size, 0, 0 , &err);
	if(skb==NULL)
		return err;

	skb->sk=sk;
	skb->free=1;
	skb->arp=1;
	skb_reserve(skb,ddp_dl->header_length);
	skb_reserve(skb,dev->hard_header_len);

	skb->dev=dev;
	
	if(sk->debug)
		printk("SK %p: Begin build.\n", sk);
	
	ddp=(struct ddpehdr *)skb_put(skb,sizeof(struct ddpehdr));
	ddp->deh_pad=0;
	ddp->deh_hops=0;
	ddp->deh_len=len+sizeof(*ddp);
	/*
	 *	Fix up the length field	[Ok this is horrible but otherwise
	 *	I end up with unions of bit fields and messy bit field order
	 *	compiler/endian dependencies..
	 */
	*((__u16 *)ddp)=ntohs(*((__u16 *)ddp));

	ddp->deh_dnet=usat->sat_addr.s_net;
	ddp->deh_snet=sk->protinfo.af_at.src_net;
	ddp->deh_dnode=usat->sat_addr.s_node;
	ddp->deh_snode=sk->protinfo.af_at.src_node;
	ddp->deh_dport=usat->sat_port;
	ddp->deh_sport=sk->protinfo.af_at.src_port;

	if(sk->debug)
		printk("SK %p: Copy user data (%d bytes).\n", sk, len);
		
	memcpy_fromiovec(skb_put(skb,len),msg->msg_iov,len);

	if(sk->no_check==1)
		ddp->deh_sum=0;
	else
		ddp->deh_sum=atalk_checksum(ddp, len+sizeof(*ddp));
		
#ifdef CONFIG_FIREWALL

	if(call_out_firewall(AF_APPLETALK, skb->dev, ddp, NULL)!=FW_ACCEPT)
	{
		kfree_skb(skb, FREE_WRITE);
		return -EPERM;
	}	
	
#endif
	
	/*
	 *	Loopback broadcast packets to non gateway targets (ie routes
	 *	to group we are in)
	 */
	 
	if(ddp->deh_dnode==ATADDR_BCAST)
	{
		if((!(rt->flags&RTF_GATEWAY))&&(!(dev->flags&IFF_LOOPBACK)))
		{
			struct sk_buff *skb2=skb_clone(skb, GFP_KERNEL);
			if(skb2)
			{
				loopback=1;
				if(sk->debug)
					printk("SK %p: send out(copy).\n", sk);
				if(aarp_send_ddp(dev,skb2,&usat->sat_addr, NULL)==-1)
					kfree_skb(skb2, FREE_WRITE);
				/* else queued/sent above in the aarp queue */
			}
		}
	}

	if((dev->flags&IFF_LOOPBACK) || loopback) 
	{
		if(sk->debug)
			printk("SK %p: Loop back.\n", sk);
		/* loop back */
		atomic_sub(skb->truesize, &sk->wmem_alloc);
		ddp_dl->datalink_header(ddp_dl, skb, dev->dev_addr);
		skb->sk = NULL;
		skb->mac.raw=skb->data;
		skb->h.raw = skb->data + ddp_dl->header_length + dev->hard_header_len;
		skb_pull(skb,dev->hard_header_len);
		skb_pull(skb,ddp_dl->header_length);
		atalk_rcv(skb,dev,NULL);
	}
	else 
	{
		if(sk->debug)
			printk("SK %p: send out.\n", sk);

		if ( rt->flags & RTF_GATEWAY ) {
		    gsat.sat_addr = rt->gateway;
		    usat = &gsat;
		}
	
		if(aarp_send_ddp(dev,skb,&usat->sat_addr, NULL)==-1)
			kfree_skb(skb, FREE_WRITE);
		/* else queued/sent above in the aarp queue */
	}
	if(sk->debug)
		printk("SK %p: Done write (%d).\n", sk, len);
	return len;
}


static int atalk_recvmsg(struct socket *sock, struct msghdr *msg, int size, int noblock, int flags, int *addr_len)
{
	atalk_socket *sk=(atalk_socket *)sock->data;
	struct sockaddr_at *sat=(struct sockaddr_at *)msg->msg_name;
	struct ddpehdr	*ddp = NULL;
	int copied = 0;
	struct sk_buff *skb;
	int er;
	
	if(sk->err)
		return sock_error(sk);
	
	if(addr_len)
		*addr_len=sizeof(*sat);

	skb=skb_recv_datagram(sk,flags,noblock,&er);
	if(skb==NULL)
		return er;

	ddp = (struct ddpehdr *)(skb->h.raw);
	if(sk->type==SOCK_RAW)
	{
		copied=ddp->deh_len;
		if(copied > size)
			copied=size;
		skb_copy_datagram_iovec(skb,0,msg->msg_iov,copied);
	}
	else
	{
		copied=ddp->deh_len - sizeof(*ddp);
		if (copied > size)
			copied = size;
		skb_copy_datagram_iovec(skb,sizeof(*ddp),msg->msg_iov,copied);
	}
	if(sat)
	{
		sat->sat_family=AF_APPLETALK;
		sat->sat_port=ddp->deh_sport;
		sat->sat_addr.s_node=ddp->deh_snode;
		sat->sat_addr.s_net=ddp->deh_snet;
	}
	skb_free_datagram(sk, skb);
	return(copied);
}		


static int atalk_shutdown(struct socket *sk,int how)
{
	return -EOPNOTSUPP;
}

static int atalk_select(struct socket *sock , int sel_type, select_table *wait)
{
	atalk_socket *sk=(atalk_socket *)sock->data;
	
	return datagram_select(sk,sel_type,wait);
}

/*
 *	Appletalk ioctl calls.
 */

static int atalk_ioctl(struct socket *sock,unsigned int cmd, unsigned long arg)
{
	int err;
	long amount=0;
	atalk_socket *sk=(atalk_socket *)sock->data;
	
	switch(cmd)
	{
		/*
		 *	Protocol layer
		 */
		case TIOCOUTQ:
			amount=sk->sndbuf-sk->wmem_alloc;
			if(amount<0)
				amount=0;
			break;
		case TIOCINQ:
		{
			struct sk_buff *skb;
			/* These two are safe on a single CPU system as only user tasks fiddle here */
			if((skb=skb_peek(&sk->receive_queue))!=NULL)
				amount=skb->len-sizeof(struct ddpehdr);
			break;
		}
		case SIOCGSTAMP:
			if (sk)
			{
				if(sk->stamp.tv_sec==0)
					return -ENOENT;
				err=verify_area(VERIFY_WRITE,(void *)arg,sizeof(struct timeval));
				if(err)
					return err;
					memcpy_tofs((void *)arg,&sk->stamp,sizeof(struct timeval));
				return 0;
			}
			return -EINVAL;
		/*
		 *	Routing
		 */
		case SIOCADDRT:
		case SIOCDELRT:
			if(!suser())
				return -EPERM;
			return(atrtr_ioctl(cmd,(void *)arg));
		/*
		 *	Interface
		 */			
		case SIOCGIFADDR:
		case SIOCSIFADDR:
		case SIOCGIFBRDADDR:
			return atif_ioctl(cmd,(void *)arg);
		/*
		 *	Physical layer ioctl calls
		 */
		case SIOCSIFLINK:
		case SIOCGIFHWADDR:
		case SIOCSIFHWADDR:
		case SIOCGIFFLAGS:
		case SIOCSIFFLAGS:
		case SIOCGIFMTU:
		case SIOCGIFCONF:
		case SIOCADDMULTI:
		case SIOCDELMULTI:

			return(dev_ioctl(cmd,(void *) arg));

		case SIOCSIFMETRIC:
		case SIOCSIFBRDADDR:
		case SIOCGIFNETMASK:
		case SIOCSIFNETMASK:
		case SIOCGIFMEM:
		case SIOCSIFMEM:
		case SIOCGIFDSTADDR:
		case SIOCSIFDSTADDR:
			return -EINVAL;

		default:
			return -EINVAL;
	}
	err=verify_area(VERIFY_WRITE,(void *)arg,sizeof(unsigned long));
	if(err)
		return err;
	put_fs_long(amount,(unsigned long *)arg);
	return(0);
}

static struct proto_ops atalk_proto_ops = {
	AF_APPLETALK,
	
	atalk_create,
	atalk_dup,
	atalk_release,
	atalk_bind,
	atalk_connect,
	atalk_socketpair,
	atalk_accept,
	atalk_getname,
	atalk_select,
	atalk_ioctl,
	atalk_listen,
	atalk_shutdown,
	atalk_setsockopt,
	atalk_getsockopt,
	atalk_fcntl,
	atalk_sendmsg,
	atalk_recvmsg
};

static struct notifier_block ddp_notifier={
	ddp_device_event,
	NULL,
	0
};

struct packet_type ltalk_packet_type=
{
	0,
	NULL,
	ltalk_rcv,
	NULL,
	NULL
};

struct packet_type ppptalk_packet_type=
{
	0,
	NULL,
	atalk_rcv,
	NULL,
	NULL
};

static char ddp_snap_id[]={0x08,0x00,0x07,0x80,0x9B};


/* Called by proto.c on kernel start up */

void atalk_proto_init(struct net_proto *pro)
{
	(void) sock_register(atalk_proto_ops.family, &atalk_proto_ops);
	if ((ddp_dl = register_snap_client(ddp_snap_id, atalk_rcv)) == NULL)
		printk(KERN_CRIT "Unable to register DDP with SNAP.\n");
	
	ltalk_packet_type.type=htons(ETH_P_LOCALTALK);	
	dev_add_pack(&ltalk_packet_type);
	
	ppptalk_packet_type.type=htons(ETH_P_PPPTALK);
	dev_add_pack(&ppptalk_packet_type);
	
	register_netdevice_notifier(&ddp_notifier);
	aarp_proto_init();

#ifdef CONFIG_PROC_FS
	proc_net_register(&(struct proc_dir_entry) {
		PROC_NET_ATALK, 9, "appletalk",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_net_inode_operations,
		atalk_get_info
	});
	proc_net_register(&(struct proc_dir_entry) {
		PROC_NET_AT_ROUTE, 11,"atalk_route",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_net_inode_operations,
		atalk_rt_get_info
	});
	proc_net_register(&(struct proc_dir_entry) {
		PROC_NET_ATIF, 11,"atalk_iface",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_net_inode_operations,
		atalk_if_get_info
	});
#endif	

	printk(KERN_INFO "Appletalk 0.17 for Linux NET3.035\n");
}

#ifdef MODULE

int init_module(void)
{
	atalk_proto_init(NULL);
	register_symtab(0);
	return 0;
}

/*
 *	FIX THIS: If there are any routes/devices configured
 *	for appletalk we must not be unloaded.
 */
 
/* Remove all route entries. Interrupts must be off. */
extern inline void free_route_list(void)
{
	struct atalk_route *list = atalk_router_list, *tmp;

	while (list != NULL)
	{
		tmp = list->next;
		kfree_s(list, sizeof(struct atalk_route));
		list = tmp;
	}
}

/* Remove all interface entries. Interrupts must be off. */
extern inline void free_interface_list(void)
{
	struct atalk_iface *list = atalk_iface_list, *tmp;

	while (list != NULL)
	{
		tmp = list->next;
		kfree_s(list, sizeof(struct atalk_iface));
		list = tmp;
	}
}

void cleanup_module(void)
{
	unsigned long flags;

	save_flags(flags);
	cli();

	aarp_cleanup_module();

#ifdef CONFIG_PROC_FS
	proc_net_unregister(PROC_NET_ATALK);
	proc_net_unregister(PROC_NET_AT_ROUTE);
	proc_net_unregister(PROC_NET_ATIF);
#endif	
	unregister_netdevice_notifier(&ddp_notifier);
	dev_remove_pack(&ltalk_packet_type);
	dev_remove_pack(&ppptalk_packet_type);
	unregister_snap_client(ddp_snap_id);
	sock_unregister(atalk_proto_ops.family);

	free_route_list();
	free_interface_list();

	restore_flags(flags);
}

#endif  /* MODULE */
