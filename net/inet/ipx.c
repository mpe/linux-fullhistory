/*
 *	Implements an IPX socket layer (badly - but I'm working on it).
 *
 *	This code is derived from work by
 *		Ross Biro	: 	Writing the original IP stack
 *		Fred Van Kempen :	Tidying up the TCP/IP
 *
 *	Many thanks go to Keith Baker, Institute For Industrial Information
 *	Technology Ltd, Swansea University for allowing me to work on this
 *	in my own time even though it was in some ways related to commercial
 *	work I am currently employed to do there.
 *
 *	All the material in this file is subject to the Gnu license version 2.
 *	Neither Alan Cox nor the Swansea University Computer Society admit liability
 *	nor provide warranty for any of this software. This material is provided 
 *	as is and at no charge.		
 *
 *	Revision 0.21:	Uses the new generic socket option code.
 *	Revision 0.22:	Gcc clean ups and drop out device registration. Use the
 *			new multi-protocol edition of hard_header 
 *	Revision 0.23:  IPX /proc by Mark Evans.
 *     			Adding a route will overwrite any existing route to the same
 *			network.
 *	Revision 0.24:	Supports new /proc with no 4K limit
 *
 */
 
#include <linux/config.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/ipx.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include "sock.h"
#include <asm/segment.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>

#ifdef CONFIG_IPX
/***********************************************************************************************************************\
*															*
*						Handlers for the socket list.						*
*															*
\***********************************************************************************************************************/

static ipx_socket *volatile ipx_socket_list=NULL;

/*
 *	Note: Sockets may not be removed _during_ an interrupt or inet_bh
 *	handler using this technique. They can be added although we do not
 *	use this facility.
 */
 
static void ipx_remove_socket(ipx_socket *sk)
{
	ipx_socket *s;
	
	cli();
	s=ipx_socket_list;
	if(s==sk)
	{
		ipx_socket_list=s->next;
		sti();
		return;
	}
	while(s && s->next)
	{
		if(s->next==sk)
		{
			s->next=sk->next;
			sti();
			return;
		}
		s=s->next;
	}
	sti();
}

static void ipx_insert_socket(ipx_socket *sk)
{
	cli();
	sk->next=ipx_socket_list;
	ipx_socket_list=sk;
	sti();
}

static ipx_socket *ipx_find_socket(int port)
{
	ipx_socket *s;
	s=ipx_socket_list;
	while(s)
	{
		if(s->ipx_source_addr.sock==port)
		{
			return(s);
		}
		s=s->next;
	}
	return(NULL);
}

/*
 *	This is only called from user mode. Thus it protects itself against
 *	interrupt users but doesn't worry about being called during work.
 *	Once it is removed from the queue no interrupt or bottom half will
 *	touch it and we are (fairly 8-) ) safe.
 */
 
static void ipx_destroy_socket(ipx_socket *sk)
{
	struct sk_buff *skb;
	ipx_remove_socket(sk);
	
	while((skb=skb_dequeue(&sk->receive_queue))!=NULL)
	{
		kfree_skb(skb,FREE_READ);
	}
	
	kfree_s(sk,sizeof(*sk));
}


/* Called from proc fs */
int ipx_get_info(char *buffer, char **start, off_t offset, int length)
{
	ipx_socket *s;
	int len=0;
	off_t pos=0;
	off_t begin=0;

	/* Theory.. Keep printing in the same place until we pass offset */
	
	len += sprintf (buffer,"Type local_address             rem_address              tx_queue rx_queue st uid\n");
	for (s = ipx_socket_list; s != NULL; s = s->next)
	{
		len += sprintf (buffer+len,"%02X   ", s->ipx_type);
		len += sprintf (buffer+len,"%08lX:%02X%02X%02X%02X%02X%02X:%02X ", htonl(s->ipx_source_addr.net),
			s->ipx_source_addr.node[0], s->ipx_source_addr.node[1], s->ipx_source_addr.node[2],
			s->ipx_source_addr.node[3], s->ipx_source_addr.node[4], s->ipx_source_addr.node[5],
			htons(s->ipx_source_addr.sock));
		len += sprintf (buffer+len,"%08lX:%02X%02X%02X%02X%02X%02X:%02X ", htonl(s->ipx_dest_addr.net),
			s->ipx_dest_addr.node[0], s->ipx_dest_addr.node[1], s->ipx_dest_addr.node[2],
			s->ipx_dest_addr.node[3], s->ipx_dest_addr.node[4], s->ipx_dest_addr.node[5],
			htons(s->ipx_dest_addr.sock));
		len += sprintf (buffer+len,"%08lX:%08lX ", s->wmem_alloc, s->rmem_alloc);
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
	len-=(offset-begin);		/* Remove unwanted header data from lenth */
	if(len>length)
		len=length;		/* Remove unwanted tail data from length */
	
	return len;
}

/*******************************************************************************************************************\
*													            *
*	            			Routing tables for the IPX socket layer				            *
*														    *
\*******************************************************************************************************************/


static ipx_route *ipx_router_list=NULL;

static ipx_route *ipxrtr_get_dev(long net)
{
	ipx_route *r;
	unsigned long flags;
	save_flags(flags);
	cli();
	r=ipx_router_list;
	while(r!=NULL)
	{
		if(r->net==net)
		{
			restore_flags(flags);
			return r;
		}
		r=r->next;
	}
	restore_flags(flags);
	return NULL;
}

static int ipxrtr_create(struct ipx_route_def *r)
{
	ipx_route *rt=ipxrtr_get_dev(r->ipx_network);
	struct device *dev;

	if(r->ipx_router_network!=0)
	{
		/* Adding an indirect route */
		ipx_route *rt1=ipxrtr_get_dev(r->ipx_router_network);
		if(rt1==NULL)
			return -ENETUNREACH;
		if(rt1->flags&IPX_RT_ROUTED)
			return -EMULTIHOP;
		if (rt==NULL)
		{
			rt=(ipx_route *)kmalloc(sizeof(ipx_route),GFP_ATOMIC);	/* Because we are brave and don't lock the table! */
			if(rt==NULL)
				return -EAGAIN;
			rt->next=ipx_router_list;
			ipx_router_list=rt;
		}
		rt->net=r->ipx_network;
		rt->router_net=r->ipx_router_network;
		memcpy(rt->router_node,r->ipx_router_node,sizeof(rt->router_node));
		rt->flags=(rt1->flags&IPX_RT_BLUEBOOK)|IPX_RT_ROUTED;
		rt->dev=rt1->dev;
		return 0;
	}
	/* Add a direct route */
	dev=dev_get(r->ipx_device);
	if(dev==NULL)
		return -ENODEV;
	/* Check addresses are suitable */
	if(dev->addr_len>6)
		return -EINVAL;
	if(dev->addr_len<2)
		return -EINVAL;
	/* Ok now create */
	if (rt==NULL)
	{
		rt=(ipx_route *)kmalloc(sizeof(ipx_route),GFP_ATOMIC);	/* Because we are brave and don't lock the table! */
		if(rt==NULL)
			return -EAGAIN;
		rt->next=ipx_router_list;
		ipx_router_list=rt;
	}
	rt->router_net=0;
	memset(rt->router_node,0,sizeof(rt->router_node));
	rt->dev=dev;
	rt->net=r->ipx_network;
	rt->flags=r->ipx_flags&IPX_RT_BLUEBOOK;
	return 0;
}

static int ipxrtr_delete(long net)
{
	ipx_route *r=ipx_router_list;
	if(r->net==net)
	{
		ipx_router_list=r->next;
		return 0;
	}
	while(r->next!=NULL)
	{
		if(r->next->net==net)
		{
			ipx_route *d=r->next;
			r->next=d->next;
			kfree_s(d,sizeof(ipx_route));
			return 0;
		}
		r=r->next;
	}
	return -ENOENT;
}

static int ipxrtr_ioctl(unsigned int cmd, void *arg)
{
	int err;
	switch(cmd)
	{
		case SIOCDELRT:
			err=verify_area(VERIFY_READ,arg,sizeof(long));
			if(err)
				return err;
			return ipxrtr_delete(get_fs_long(arg));
		case SIOCADDRT:
		{
			struct ipx_route_def f;
			err=verify_area(VERIFY_READ,arg,sizeof(f));
			if(err)
				return err;
			memcpy_fromfs(&f,arg,sizeof(f));
			return ipxrtr_create(&f);
		}
		default:
			return -EINVAL;
	}
}

/* Called from proc fs */
int ipx_rt_get_info(char *buffer, char **start, off_t offset, int length)
{
	ipx_route *rt;
	int len=0;
	off_t pos=0;
	off_t begin=0;

	len += sprintf (buffer,"Net      Router                Flags Dev\n");
	for (rt = ipx_router_list; rt != NULL; rt = rt->next)
	{
		len += sprintf (buffer+len,"%08lX %08lX:%02X%02X%02X%02X%02X%02X %02X    %s\n", ntohl(rt->net),
			ntohl(rt->router_net), rt->router_node[0], rt->router_node[1], rt->router_node[2],
			rt->router_node[3], rt->router_node[4], rt->router_node[5], rt->flags, rt->dev->name);
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
*	      Handling for system calls applied via the various interfaces to an IPX socket object		    *
*														    *
\*******************************************************************************************************************/
 
static int ipx_fcntl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	ipx_socket *sk;
	
	sk=(ipx_socket *)sock->data;
	
	if(sk==NULL)
	{
		printk("IPX:fcntl:passed sock->data=NULL\n");
		return(0);
	}
	
	switch(cmd)
	{
		default:
			return(-EINVAL);
	}
}

static int ipx_setsockopt(struct socket *sock, int level, int optname,
	char *optval, int optlen)
{
	ipx_socket *sk;
	int err,opt;
	
	sk=(ipx_socket *)sock->data;
	
	if(sk==NULL)
	{
		printk("IPX:setsockopt:passed sock->data=NULL\n");
		return 0;
	}
	
	if(optval==NULL)
		return(-EINVAL);
	err=verify_area(VERIFY_READ,optval,sizeof(int));
	if(err)
		return err;
	opt=get_fs_long((unsigned long *)optval);
	
	switch(level)
	{
		case SOL_IPX:
			switch(optname)
			{
				case IPX_TYPE:
					if(!suser())
						return(-EPERM);
					sk->ipx_type=opt;
					return 0;
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

static int ipx_getsockopt(struct socket *sock, int level, int optname,
	char *optval, int *optlen)
{
	ipx_socket *sk;
	int val=0;
	int err;
	
	sk=(ipx_socket *)sock->data;
	if(sk==NULL)
	{
		printk("IPX:getsockopt:passed NULL sock->data.\n");
		return 0;
	}

	switch(level)
	{

		case SOL_IPX:
			switch(optname)
			{
				case IPX_TYPE:
					val=sk->ipx_type;
					break;
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
	put_fs_long(sizeof(int),(unsigned long *)optlen);
	err=verify_area(VERIFY_WRITE,optval,sizeof(int));
	put_fs_long(val,(unsigned long *)optval);
	return(0);
}

static int ipx_listen(struct socket *sock, int backlog)
{
	return -EOPNOTSUPP;
}

static void def_callback1(struct sock *sk)
{
	if(!sk->dead)
		wake_up_interruptible(sk->sleep);
}

static void def_callback2(struct sock *sk, int len)
{
	if(!sk->dead)
		wake_up_interruptible(sk->sleep);
}

static int ipx_create(struct socket *sock, int protocol)
{
	ipx_socket *sk;
	sk=(ipx_socket *)kmalloc(sizeof(*sk),GFP_KERNEL);
	if(sk==NULL)
		return(-ENOMEM);
	switch(sock->type)
	{
		case SOCK_DGRAM:
			break;
		default:
			kfree_s((void *)sk,sizeof(*sk));
			return(-ESOCKTNOSUPPORT);
	}
	sk->stamp.tv_sec=0;
	sk->rmem_alloc=0;
	sk->dead=0;
	sk->next=NULL;
	sk->broadcast=0;
	sk->rcvbuf=SK_RMEM_MAX;
	sk->sndbuf=SK_WMEM_MAX;
	sk->wmem_alloc=0;
	sk->rmem_alloc=0;
	sk->inuse=0;
	sk->dead=0;
	sk->prot=NULL;	/* So we use default free mechanisms */
	sk->broadcast=0;
	sk->err=0;
	skb_queue_head_init(&sk->receive_queue);
	skb_queue_head_init(&sk->write_queue);
	sk->send_head=NULL;
	skb_queue_head_init(&sk->back_log);
	sk->mtu=512;
	sk->state=TCP_CLOSE;
	sk->socket=sock;
	sk->type=sock->type;
	sk->ipx_type=0;		/* General user level IPX */
	sk->debug=0;
	sk->localroute=0;
	
	memset(&sk->ipx_dest_addr,'\0',sizeof(sk->ipx_dest_addr));
	memset(&sk->ipx_source_addr,'\0',sizeof(sk->ipx_source_addr));
	sk->mtu=IPX_MTU;
	
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

static int ipx_dup(struct socket *newsock,struct socket *oldsock)
{
	return(ipx_create(newsock,SOCK_DGRAM));
}

static int ipx_release(struct socket *sock, struct socket *peer)
{
	ipx_socket *sk=(ipx_socket *)sock->data;
	if(sk==NULL)
		return(0);
	if(!sk->dead)
		sk->state_change(sk);
	sk->dead=1;
	sock->data=NULL;
	ipx_destroy_socket(sk);
	return(0);
}
		
static int ipx_bind(struct socket *sock, struct sockaddr *uaddr,int addr_len)
{
	ipx_socket *sk;
	int err;
	struct sockaddr_ipx addr;
	struct ipx_route *rt;
	
	sk=(ipx_socket *)sock->data;
	if(sk==NULL)
	{
		printk("IPX:bind:sock->data=NULL\n");
		return 0;
	}

	if(sk->zapped==0)
		return(-EIO);
		
	err=verify_area(VERIFY_READ,uaddr,addr_len);
	if(err)
		return err;
	if(addr_len!=sizeof(addr))
		return -EINVAL;
	memcpy_fromfs(&addr,uaddr,addr_len);
	
	if(ntohs(addr.sipx_port)<0x4000 && !suser())
		return(-EPERM);	/* protect IPX system stuff like routing/sap */
	
	/* Source addresses are easy. It must be our network:node pair for
	   an interface routed to IPX with the ipx routing ioctl() */

	if(ipx_find_socket(addr.sipx_port)!=NULL)
	{
		if(sk->debug)
			printk("IPX: bind failed because port %X in use.\n",
				(int)addr.sipx_port);
		return(-EADDRINUSE);	   
	}
	sk->ipx_source_addr.sock=addr.sipx_port;
	memcpy(sk->ipx_source_addr.node,addr.sipx_node,sizeof(sk->ipx_source_addr.node));
	sk->ipx_source_addr.net=addr.sipx_network;
	if((rt=ipxrtr_get_dev(sk->ipx_source_addr.net))==NULL)
	{
		if(sk->debug)
			printk("IPX: bind failed (no device for net %lX)\n",
				sk->ipx_source_addr.net);
		return(-EADDRNOTAVAIL);
	}
	memset(sk->ipx_source_addr.node,'\0',6);
	memcpy(sk->ipx_source_addr.node,rt->dev->dev_addr,rt->dev->addr_len);
	ipx_insert_socket(sk);
	sk->zapped=0;
	if(sk->debug)
		printk("IPX: socket is bound.\n");
	return(0);
}

static int ipx_connect(struct socket *sock, struct sockaddr *uaddr,
	int addr_len, int flags)
{
	ipx_socket *sk=(ipx_socket *)sock->data;
	struct sockaddr_ipx addr;
	int err;
	
	if(sk==NULL)
	{
		printk("IPX:connect:sock->data=NULL!\n");
		return 0;
	}

	sk->state = TCP_CLOSE;	
	sock->state = SS_UNCONNECTED;
	
	if(addr_len!=sizeof(addr))
		return(-EINVAL);
	err=verify_area(VERIFY_READ,uaddr,addr_len);
	if(err)
		return err;
	memcpy_fromfs(&addr,uaddr,sizeof(addr));
	
	if(ntohs(addr.sipx_port)<0x4000 && !suser())
		return -EPERM;
	if(sk->ipx_source_addr.net==0)	/* Must bind first - no autobinding in this */
		return -EINVAL;
		
	
	sk->ipx_dest_addr.net=addr.sipx_network;
	sk->ipx_dest_addr.sock=addr.sipx_port;
	memcpy(sk->ipx_dest_addr.node,addr.sipx_node,sizeof(sk->ipx_source_addr.node));
	if(ipxrtr_get_dev(sk->ipx_dest_addr.net)==NULL)
		return -ENETUNREACH;
	sock->state = SS_CONNECTED;
	sk->state=TCP_ESTABLISHED;
	return(0);
}

static int ipx_socketpair(struct socket *sock1, struct socket *sock2)
{
	return(-EOPNOTSUPP);
}

static int ipx_accept(struct socket *sock, struct socket *newsock, int flags)
{
	if(newsock->data)
		kfree_s(newsock->data,sizeof(ipx_socket));
	return -EOPNOTSUPP;
}

static int ipx_getname(struct socket *sock, struct sockaddr *uaddr,
	int *uaddr_len, int peer)
{
	ipx_address *addr;
	struct sockaddr_ipx sipx;
	ipx_socket *sk;
	int len;
	int err;
	
	sk=(ipx_socket *)sock->data;
	
	err = verify_area(VERIFY_WRITE,uaddr_len,sizeof(long));
	if(err)
		return err;
		
	len = get_fs_long(uaddr_len);
	
	err = verify_area(VERIFY_WRITE, uaddr, len);
	if(err)
		return err;
	
	if(len<sizeof(struct sockaddr_ipx))
		return -EINVAL;
		
	if(peer)
	{
		if(sk->state!=TCP_ESTABLISHED)
			return -ENOTCONN;
		addr=&sk->ipx_dest_addr;
	}
	else
		addr=&sk->ipx_source_addr;
		
	sipx.sipx_family = AF_IPX;
	sipx.sipx_port = addr->sock;
	sipx.sipx_network = addr->net;
	memcpy(sipx.sipx_node,addr->node,sizeof(sipx.sipx_node));
	memcpy_tofs(uaddr,&sipx,sizeof(sipx));
	put_fs_long(len,uaddr_len);
	return(0);
}


int ipx_rcv(struct sk_buff *skb, struct device *dev, struct packet_type *pt)
{
	/* NULL here for pt means the packet was looped back */
	ipx_socket *sock;
	unsigned char *buff;
	ipx_packet *ipx;
	ipx_route *rt;
	
	buff=skb->data;
	buff+=dev->hard_header_len;
	ipx=(ipx_packet *)buff;
	
	if(ipx->ipx_checksum!=IPX_NO_CHECKSUM)
	{
		/* We don't do checksum options. We can't really. Novell don't seem to have documented them.
		   If you need them try the XNS checksum since IPX is basically XNS in disguise. It might be
		   the same... */
		kfree_skb(skb,FREE_READ);
		return(0);
	}
	
	/* Too small */
	if(htons(ipx->ipx_pktsize)<sizeof(ipx_packet)+dev->hard_header_len)
	{
		kfree_skb(skb,FREE_READ);
		return(0);
	}
	
	/* Too many hops */
	if(ipx->ipx_tctrl>16)
	{
		kfree_skb(skb,FREE_READ);
		return(0);
	}
	
	/* Not us/broadcast */
	if(memcmp(dev->dev_addr,ipx->ipx_dest.node,dev->addr_len)!=0
	     && memcmp(ipx_broadcast_node,ipx->ipx_dest.node,dev->addr_len)!=0)
	{
		/**********************************************************************************************
		
			IPX router. Roughly as per the Novell spec. This doesn't handle netbios flood fill
			broadcast frames. See the Novell IPX router specification for more details
			(for ftp from ftp.novell.com)
			
		***********************************************************************************************/
		
		struct sk_buff *skb2;
		int free_it=0;
		
		/* Rule: Don't forward packets that have exceeded the hop limit. This is fixed at 16 in IPX */
		if(ipx->ipx_tctrl==16)
		{
			kfree_skb(skb,FREE_READ);
			return(0);
		}

		/* Don't forward if we don't have a route. We ought to go off and start hunting out routes but
		   if someone needs this _THEY_ can add it */		
		rt=ipxrtr_get_dev(ipx->ipx_dest.net);
		if(rt==NULL)   /* Unlike IP we can send on the interface we received. Eg doing DIX/802.3 conversion */
		{
			kfree_skb(skb,FREE_READ);
			return(0);
		}
		if(rt->dev->hard_header_len!=dev->hard_header_len)
		{
			/* A different header length causes a copy. Awkward to avoid with the current
			   sk_buff stuff. */
			skb2=alloc_skb(skb->len,GFP_ATOMIC);
			if(skb2==NULL)
			{
				kfree_skb(skb,FREE_READ);
				return 0;
			}
			free_it=1;
			skb2->free=1;
			skb2->len=skb->len;
			memcpy((char *)(skb2+1),(char *)(skb+1),skb->len);
			buff=(char *)(skb2+1);
			ipx=(ipx_packet *)(buff+dev->hard_header_len);
		}
		else
		{
			skb2=skb;
			buff=(char *)(skb+1);
		}
		/* Now operate on the buffer */
		/* Increase hop count */
		ipx->ipx_tctrl++;
		/* If the route is a gateway then forward to it */
		
		dev->hard_header(buff, dev, 
			(rt->flags&IPX_RT_BLUEBOOK)?ntohs(ETH_P_IPX):ntohs(skb2->len),
			(rt->flags&IPX_RT_ROUTED)?rt->router_node:ipx->ipx_dest.node,
			NULL,	/* Our source */
			skb2->len,
			skb2);
			
		dev_queue_xmit(skb2,dev,SOPRI_NORMAL);

		if(free_it)
			kfree_skb(skb,FREE_READ);
		return(0);
	}
	/************ End of router: Now sanity check stuff for us ***************/
	
	/* Ok its for us ! */
	
	sock=ipx_find_socket(ipx->ipx_dest.sock);
	if(sock==NULL)	/* But not one of our sockets */
	{
		kfree_skb(skb,FREE_READ);
		return(0);
	}
	
	if(sock->rmem_alloc>=sock->rcvbuf)
	{
		kfree_skb(skb,FREE_READ);	/* Socket is full */
		return(0);
	}
	
	sock->rmem_alloc+=skb->mem_len;
	skb_queue_tail(&sock->receive_queue,skb);
	if(!sock->dead)
		sock->data_ready(sock,skb->len);
	return(0);
}




static int ipx_sendto(struct socket *sock, void *ubuf, int len, int noblock,
	unsigned flags, struct sockaddr *usip, int addr_len)
{
	ipx_socket *sk=(ipx_socket *)sock->data;
	struct sockaddr_ipx *usipx=(struct sockaddr_ipx *)usip;
	int err;
	struct sockaddr_ipx sipx;
	struct sk_buff *skb;
	struct device *dev;
	struct ipx_packet *ipx;
	int size;
	ipx_route *rt;

	if(flags&~MSG_DONTROUTE)
		return -EINVAL;
	if(len<0)
		return -EINVAL;
	if(len == 0)
		return 0;
		
	if(usipx)
	{
		if(addr_len <sizeof(sipx))
			return(-EINVAL);
		err=verify_area(VERIFY_READ,usipx,sizeof(sipx));
		if(err)
			return(err);
		memcpy_fromfs(&sipx,usipx,sizeof(sipx));
		if(sipx.sipx_family != AF_IPX)
			return -EINVAL;
		if(htons(sipx.sipx_port)<0x4000 && !suser())
			return -EPERM;
	}
	else
	{
		if(sk->state!=TCP_ESTABLISHED)
			return -ENOTCONN;
		sipx.sipx_family=AF_IPX;
		sipx.sipx_port=sk->ipx_dest_addr.sock;
		sipx.sipx_network=sk->ipx_dest_addr.net;
		memcpy(sipx.sipx_node,sk->ipx_dest_addr.node,sizeof(sipx.sipx_node));
	}
	
	if(sk->debug)
		printk("IPX: sendto: Addresses built.\n");
	if(!sk->broadcast && memcmp(&sipx.sipx_node,&ipx_broadcast_node,6)==0)
		return -ENETUNREACH;
	/* Build a packet */
	
	if(sk->debug)
		printk("IPX: sendto: building packet.\n");
	err=verify_area(VERIFY_READ,ubuf,len);
	if(err)
		return err;
		
	size=sizeof(ipx_packet)+len;	/* For mac headers */

	/* Find out where this has to go */
	rt=ipxrtr_get_dev(sipx.sipx_network);
	/* No suitable route - no gateways when not routing */
	if(rt==NULL || ((flags&IPX_RT_ROUTED)&& ((flags&MSG_DONTROUTE)||sk->localroute)))
	{
		return -ENETUNREACH;
	}
	
	dev=rt->dev;
	
	size+=dev->hard_header_len;
		
	if(sk->debug)
		printk("IPX: sendto: allocating buffer (%d)\n",size-sizeof(struct sk_buff));
	
	if(size+sk->wmem_alloc>sk->sndbuf)
		return -EAGAIN;
		
	skb=alloc_skb(size,GFP_KERNEL);
	if(skb==NULL)
		return -ENOMEM;
	if(skb->mem_len+sk->wmem_alloc>sk->sndbuf)
	{
		kfree_skb(skb,FREE_WRITE);
		return -EAGAIN;
	}

	sk->wmem_alloc+=skb->mem_len;
	skb->sk=sk;
	skb->free=1;
	skb->arp=1;
	skb->len=size-sizeof(struct sk_buff);

	if(sk->debug)
		printk("Building MAC header.\n");		
	skb->dev=rt->dev;
	
	dev->hard_header(skb->data,skb->dev,
		(rt->flags&IPX_RT_BLUEBOOK)?ETH_P_IPX:ETH_P_802_3),
		(rt->flags&IPX_RT_ROUTED)?rt->router_node:sipx.sipx_node,
		NULL,
		len+sizeof(ipx_packet),
		skb);

	/* Now the IPX */
	if(sk->debug)
		printk("Building IPX Header.\n");
	ipx=(ipx_packet *)skb->data+skb->dev->hard_header_len;
	ipx->ipx_checksum=0xFFFF;
	ipx->ipx_pktsize=htons(len+sizeof(ipx_packet));
	ipx->ipx_tctrl=0;
	ipx->ipx_type=sk->ipx_type;
	memcpy(&ipx->ipx_source,&sk->ipx_source_addr,sizeof(ipx->ipx_source));
	ipx->ipx_dest.net=sipx.sipx_network;
	memcpy(ipx->ipx_dest.node,sipx.sipx_node,sizeof(ipx->ipx_dest.node));
	ipx->ipx_dest.sock=sipx.sipx_port;
	if(sk->debug)
		printk("IPX: Appending user data.\n");
	/* User data follows immediately after the IPX data */
	memcpy_fromfs((char *)(ipx+1),ubuf,len);
	if(sk->debug)
		printk("IPX: Transmitting buffer\n");
	if(dev->flags&IFF_LOOPBACK)
		/* loop back */
		ipx_rcv(skb,dev,NULL);
	else
		dev_queue_xmit(skb,dev,SOPRI_NORMAL);
	return len;
}

static int ipx_send(struct socket *sock, void *ubuf, int size, int noblock, unsigned flags)
{
	return ipx_sendto(sock,ubuf,size,noblock,flags,NULL,0);
}

static int ipx_write(struct socket *sock, char *ubuf, int size, int noblock)
{
	return ipx_send(sock,ubuf,size,noblock,0);
}

static int ipx_recvfrom(struct socket *sock, void *ubuf, int size, int noblock,
		   unsigned flags, struct sockaddr *sip, int *addr_len)
{
	ipx_socket *sk=(ipx_socket *)sock->data;
	struct sockaddr_ipx *sipx=(struct sockaddr_ipx *)sip;
	/* FILL ME IN */
	int copied = 0;
	struct sk_buff *skb;
	int er;

	if(sk->err)
	{
		er= -sk->err;
		sk->err=0;
		return er;
	}
	
	if(size==0)
		return 0;
	if(size<0)
		return -EINVAL;
	if(addr_len)
	{
		er=verify_area(VERIFY_WRITE,addr_len,sizeof(*addr_len));
		if(er)
			return er;
		put_fs_long(sizeof(*sipx),addr_len);
	}
	if(sipx)
	{
		er=verify_area(VERIFY_WRITE,sipx,sizeof(*sipx));
		if(er)
			return er;
	}
	er=verify_area(VERIFY_WRITE,ubuf,size);
	if(er)
		return er;
	skb=skb_recv_datagram(sk,flags,noblock,&er);
	if(skb==NULL)
		return er;
	copied=(size<skb->len)?size:skb->len;
	skb_copy_datagram(skb,sizeof(struct ipx_packet),ubuf,copied);
	sk->stamp=skb->stamp;
	
	if(sipx)
	{
		struct sockaddr_ipx addr;
		
		addr.sipx_family=AF_IPX;
		addr.sipx_port=((ipx_packet*)skb->h.raw)->ipx_source.sock;
		memcpy(addr.sipx_node,((ipx_packet*)skb->h.raw)->ipx_source.node,sizeof(addr.sipx_node));
		addr.sipx_network=((ipx_packet*)skb->h.raw)->ipx_source.net;
		memcpy_tofs(sipx,&addr,sizeof(*sipx));
	}
	skb_free_datagram(skb);
	return(copied);
}		

static int ipx_recv(struct socket *sock, void *ubuf, int size , int noblock,
	unsigned flags)
{
	ipx_socket *sk=(ipx_socket *)sock->data;
	if(sk->zapped)
		return -ENOTCONN;
	return ipx_recvfrom(sock,ubuf,size,noblock,flags,NULL, NULL);
}

static int ipx_read(struct socket *sock, char *ubuf, int size, int noblock)
{
	return ipx_recv(sock,ubuf,size,noblock,0);
}


static int ipx_shutdown(struct socket *sk,int how)
{
	return -EOPNOTSUPP;
}

static int ipx_select(struct socket *sock , int sel_type, select_table *wait)
{
	ipx_socket *sk=(ipx_socket *)sock->data;
	
	return datagram_select(sk,sel_type,wait);
}

static int ipx_ioctl(struct socket *sock,unsigned int cmd, unsigned long arg)
{
	
	switch(cmd)
	{
		case SIOCADDRT:
		case SIOCDELRT:
			if(!suser())
				return -EPERM;
			return(ipxrtr_ioctl(cmd,(void *)arg));
		default:
			return -EINVAL;
	}
	/*NOTREACHED*/
	return(0);
}

static struct proto_ops ipx_proto_ops = {
	AF_IPX,
	
	ipx_create,
	ipx_dup,
	ipx_release,
	ipx_bind,
	ipx_connect,
	ipx_socketpair,
	ipx_accept,
	ipx_getname,
	ipx_read,
	ipx_write,
	ipx_select,
	ipx_ioctl,
	ipx_listen,
	ipx_send,
	ipx_recv,
	ipx_sendto,
	ipx_recvfrom,
	ipx_shutdown,
	ipx_setsockopt,
	ipx_getsockopt,
	ipx_fcntl,
};

/* Called by ddi.c on kernel start up */

static struct packet_type ipx_8023_packet_type = 
{
	0,	/* MUTTER ntohs(ETH_P_8023),*/
	0,		/* copy */
	ipx_rcv,
	NULL,
	NULL,
};
 
static struct packet_type ipx_dix_packet_type = 
{
	0,	/* MUTTER ntohs(ETH_P_IPX),*/
	0,		/* copy */
	ipx_rcv,
	NULL,
	NULL,
};
 

void ipx_proto_init(struct ddi_proto *pro)
{
	(void) sock_register(ipx_proto_ops.family, &ipx_proto_ops);
	ipx_dix_packet_type.type=htons(ETH_P_IPX);
	dev_add_pack(&ipx_dix_packet_type);
	ipx_8023_packet_type.type=htons(ETH_P_802_3);
	dev_add_pack(&ipx_8023_packet_type);
	
	printk("Swansea University Computer Society IPX 0.24 BETA for NET3 ALPHA.008\n");
	
}

#endif
