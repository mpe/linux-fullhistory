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
 *	Revision 0.25:	Add ephemeral sockets, passive local network 
 *			identification, support for local net 0 and
 *			multiple datalinks <Greg Page>
 *	Revision 0.26:  Device drop kills IPX routes via it. (needed for modules)
 *	Revision 0.27:  Autobind <Mark Evans>
 *	Revision 0.28:  Small fix for multiple local networks <Thomas Winder>
 *
 *			
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
#include <linux/termios.h>	/* For TIOCOUTQ/INQ */
#include <linux/interrupt.h>
#include "p8022.h"

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
	len-=(offset-begin);		/* Remove unwanted header data from length */
	if(len>length)
		len=length;		/* Remove unwanted tail data from length */
	
	return len;
}

/*******************************************************************************************************************\
*													            *
*	            			Routing tables for the IPX socket layer				            *
*														    *
\*******************************************************************************************************************/


static struct datalink_proto	*p8022_datalink = NULL;
static struct datalink_proto	*pEII_datalink = NULL;
static struct datalink_proto	*p8023_datalink = NULL;
static struct datalink_proto	*pSNAP_datalink = NULL;

static ipx_route *ipx_router_list=NULL;
static ipx_route *ipx_localnet_list=NULL;

static ipx_route *
ipxrtr_get_local_net(struct device *dev, unsigned short datalink)
{
	ipx_route *r;
	unsigned long flags;
	save_flags(flags);
	cli();
	r=ipx_localnet_list;
	while(r!=NULL)
	{
		if((r->dev==dev) && (r->dlink_type == datalink))
		{
			restore_flags(flags);
			return r;
		}
		r=r->nextlocal;
	}
	restore_flags(flags);
	return NULL;
}
	
static ipx_route *
ipxrtr_get_default_net(void)
{
	return ipx_localnet_list;
}

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

static void ipxrtr_add_localnet(ipx_route *newnet)
{
	ipx_route *r;
	unsigned long flags;
	save_flags(flags);
	cli();

	newnet->nextlocal = NULL;
	if (ipx_localnet_list == NULL) {
		ipx_localnet_list = newnet;
		restore_flags(flags);
		return;
	}

	r=ipx_localnet_list;
	while(r->nextlocal!=NULL)
		r=r->nextlocal;

	r->nextlocal = newnet;
	
	restore_flags(flags);
	return;
}

static int ipxrtr_create(struct ipx_route_def *r)
{
	ipx_route *rt=ipxrtr_get_dev(r->ipx_network);
	struct device *dev;
	unsigned short	dlink_type;
	struct datalink_proto *datalink = NULL;

	if (r->ipx_flags & IPX_RT_BLUEBOOK) {
		dlink_type = htons(ETH_P_IPX);
		datalink = pEII_datalink;
	} else if (r->ipx_flags & IPX_RT_8022) {
		dlink_type = htons(ETH_P_802_2);
		datalink = p8022_datalink;
	} else if (r->ipx_flags & IPX_RT_SNAP) {
		dlink_type = htons(ETH_P_SNAP);
		datalink = pSNAP_datalink;
	} else {
		dlink_type = htons(ETH_P_802_3);
		datalink = p8023_datalink;
	}

	if (datalink == NULL) {
		printk("IPX: Unsupported datalink protocol.\n");
		return -EPROTONOSUPPORT;
	}

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
		rt->flags=IPX_RT_ROUTED;
		rt->dlink_type = dlink_type;
		rt->datalink = datalink;
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
	if (ipxrtr_get_local_net(dev, dlink_type) != NULL)
		return -EEXIST;
	/* Ok now create */
	rt=(ipx_route *)kmalloc(sizeof(ipx_route),GFP_ATOMIC);	/* Because we are brave and don't lock the table! */
	if(rt==NULL)
		return -EAGAIN;
	rt->next=ipx_router_list;
	ipx_router_list=rt;
	rt->router_net=0;
	memset(rt->router_node,0,sizeof(rt->router_node));
	rt->dev=dev;
	rt->net=r->ipx_network;
	rt->flags=0;
	rt->dlink_type = dlink_type;
	rt->datalink = datalink;
	ipxrtr_add_localnet(rt);
	return 0;
}


static int ipxrtr_delete_localnet(ipx_route *d)
{
	ipx_route **r = &ipx_localnet_list;
	ipx_route *tmp;

	while ((tmp = *r) != NULL) {
		if (tmp == d) {
			*r = tmp->next;
			return 0;
		}
		r = &tmp->nextlocal;
	}
	return -ENOENT;
}

static int ipxrtr_delete(long net)
{
	ipx_route **r = &ipx_router_list;
	ipx_route *tmp;

	while ((tmp = *r) != NULL) {
		if (tmp->net == net) {
			*r = tmp->next;
			if (tmp->router_net == 0) {
				ipxrtr_delete_localnet(tmp);
			}
			kfree_s(tmp, sizeof(ipx_route));
			return 0;
		}
		r = &tmp->next;
	}
	return -ENOENT;
}

void ipxrtr_device_down(struct device *dev)
{
	ipx_route **r = &ipx_router_list;
	ipx_route *tmp;

	while ((tmp = *r) != NULL) {
		if (tmp->dev == dev) {
			*r = tmp->next;
			if(tmp->router_net == 0)
				ipxrtr_delete_localnet(tmp);
			kfree_s(tmp, sizeof(ipx_route));
		}
		r = &tmp->next;
	}
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
	/* ipx_socket *sk=(ipx_socket *)sock->data; */
	switch(cmd)
	{
		default:
			return(-EINVAL);
	}
}

static int ipx_setsockopt(struct socket *sock, int level, int optname, char *optval, int optlen)
{
	ipx_socket *sk;
	int err,opt;
	
	sk=(ipx_socket *)sock->data;
	
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
	sk->dead=0;
	sk->next=NULL;
	sk->broadcast=0;
	sk->rcvbuf=SK_RMEM_MAX;
	sk->sndbuf=SK_WMEM_MAX;
	sk->wmem_alloc=0;
	sk->rmem_alloc=0;
	sk->inuse=0;
	sk->shutdown=0;
	sk->prot=NULL;	/* So we use default free mechanisms */
	sk->broadcast=0;
	sk->err=0;
	skb_queue_head_init(&sk->receive_queue);
	skb_queue_head_init(&sk->write_queue);
	sk->send_head=NULL;
	skb_queue_head_init(&sk->back_log);
	sk->state=TCP_CLOSE;
	sk->socket=sock;
	sk->type=sock->type;
	sk->ipx_type=0;		/* General user level IPX */
	sk->debug=0;
	
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
		
static unsigned short first_free_socketnum(void)
{
	static unsigned short	socketNum = 0x4000;

	while (ipx_find_socket(htons(socketNum)) != NULL)
		if (socketNum > 0x7ffc) socketNum = 0x4000;

	return	htons(socketNum++);
}
	
static int ipx_bind(struct socket *sock, struct sockaddr *uaddr,int addr_len)
{
	ipx_socket *sk;
	struct ipx_route *rt;
	unsigned char	*nodestart;
	struct sockaddr_ipx *addr=(struct sockaddr_ipx *)uaddr;
	
	sk=(ipx_socket *)sock->data;
	
	if(sk->zapped==0)
		return(-EIO);
		
	if(addr_len!=sizeof(struct sockaddr_ipx))
		return -EINVAL;
	
	if (addr->sipx_port == 0) 
	{
		addr->sipx_port = first_free_socketnum();
		if (addr->sipx_port == 0)
			return -EINVAL;
	}
		
	if(ntohs(addr->sipx_port)<0x4000 && !suser())
		return(-EPERM);	/* protect IPX system stuff like routing/sap */
	
	/* Source addresses are easy. It must be our network:node pair for
	   an interface routed to IPX with the ipx routing ioctl() */

	if(ipx_find_socket(addr->sipx_port)!=NULL)
	{
		if(sk->debug)
			printk("IPX: bind failed because port %X in use.\n",
				(int)addr->sipx_port);
		return -EADDRINUSE;	   
	}

	sk->ipx_source_addr.sock=addr->sipx_port;

	if (addr->sipx_network == 0L) 
	{
		rt = ipxrtr_get_default_net();
	}
	else 
	{
		rt = ipxrtr_get_dev(addr->sipx_network);
	}

	if(rt == NULL)
	{
		if(sk->debug)
			printk("IPX: bind failed (no device for net %lX)\n",
				sk->ipx_source_addr.net);
		return -EADDRNOTAVAIL;
	}

	sk->ipx_source_addr.net=rt->net;

	/* IPX addresses zero pad physical addresses less than 6 */
	memset(sk->ipx_source_addr.node,'\0',6);
	nodestart = sk->ipx_source_addr.node + (6 - rt->dev->addr_len);
	memcpy(nodestart,rt->dev->dev_addr,rt->dev->addr_len);

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
	struct sockaddr_ipx *addr;
	
	sk->state = TCP_CLOSE;	
	sock->state = SS_UNCONNECTED;
	
	if(addr_len!=sizeof(addr))
		return(-EINVAL);
	addr=(struct sockaddr_ipx *)uaddr;
	
	if(sk->ipx_source_addr.net==0)
	/* put the autobinding in */
	{
		struct sockaddr_ipx uaddr;
		int ret;
	
		uaddr.sipx_port = 0;
		uaddr.sipx_network = 0L; 
		ret = ipx_bind (sock, (struct sockaddr *)&uaddr, sizeof(struct sockaddr_ipx));
		if (ret != 0) return (ret);
	}
	
	sk->ipx_dest_addr.net=addr->sipx_network;
	sk->ipx_dest_addr.sock=addr->sipx_port;
	memcpy(sk->ipx_dest_addr.node,addr->sipx_node,sizeof(sk->ipx_source_addr.node));
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
	
	sk=(ipx_socket *)sock->data;
	
	*uaddr_len = sizeof(struct sockaddr_ipx);
		
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
	memcpy(uaddr,&sipx,sizeof(sipx));
	return(0);
}

int ipx_rcv(struct sk_buff *skb, struct device *dev, struct packet_type *pt)
{
	/* NULL here for pt means the packet was looped back */
	ipx_socket *sock;
	ipx_packet *ipx;
	ipx_route *rt;
	ipx_route *ln;
	unsigned char IPXaddr[6];
	
	ipx=(ipx_packet *)skb->h.raw;
	
	if(ipx->ipx_checksum!=IPX_NO_CHECKSUM)
	{
		/* We don't do checksum options. We can't really. Novell don't seem to have documented them.
		   If you need them try the XNS checksum since IPX is basically XNS in disguise. It might be
		   the same... */
		kfree_skb(skb,FREE_READ);
		return(0);
	}
	
	/* Too small */
	if(htons(ipx->ipx_pktsize)<sizeof(ipx_packet))
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
	
	/* Determine what local ipx endpoint this is */
	ln = ipxrtr_get_local_net(dev, pt->type);
	if (ln == NULL) 
	{
		kfree_skb(skb,FREE_READ);
		return(0);
	}

	memset(IPXaddr, '\0', 6);
	memcpy(IPXaddr+(6 - dev->addr_len), dev->dev_addr, dev->addr_len);

	/* Not us/broadcast */
	if(memcmp(IPXaddr,ipx->ipx_dest.node,6)!=0
	     && memcmp(ipx_broadcast_node,ipx->ipx_dest.node,6)!=0)
	{
		/**********************************************************************************************
		
			IPX router. Roughly as per the Novell spec. This doesn't handle netbios flood fill
			broadcast frames. See the Novell IPX router specification for more details
			(for ftp from ftp.novell.com)
			
		***********************************************************************************************/
		
		int incoming_size;
		int outgoing_size;
		struct sk_buff *skb2;
		int free_it=0;
		
		/* Rule: Don't forward packets that have exceeded the hop limit. This is fixed at 16 in IPX */
		if(ipx->ipx_tctrl==16)
		{
			kfree_skb(skb,FREE_READ);
			return(0);
		}

		ipx->ipx_tctrl++;
		/* Don't forward if we don't have a route. We ought to go off and start hunting out routes but
		   if someone needs this _THEY_ can add it */		
		rt=ipxrtr_get_dev(ipx->ipx_dest.net);
		if(rt==NULL)   /* Unlike IP we can send on the interface we received. Eg doing DIX/802.3 conversion */
		{
			kfree_skb(skb,FREE_READ);
			return(0);
		}

		/* Check for differences in outgoing and incoming packet size */
		incoming_size = skb->len - ntohs(ipx->ipx_pktsize);
		outgoing_size = rt->datalink->header_length + rt->dev->hard_header_len;
		if(incoming_size != outgoing_size)
		{
			/* A different header length causes a copy. Awkward to avoid with the current
			   sk_buff stuff. */
			skb2=alloc_skb(ntohs(ipx->ipx_pktsize) + outgoing_size,
					GFP_ATOMIC);
			if(skb2==NULL)
			{
				kfree_skb(skb,FREE_READ);
				return 0;
			}
			free_it=1;
			skb2->free=1;
			skb2->len=ntohs(ipx->ipx_pktsize) + outgoing_size;
			skb2->mem_addr = skb2;
			skb2->arp = 1;
			skb2->sk = NULL;

			/* Need to copy with appropriate offsets */
			memcpy((char *)(skb2+1)+outgoing_size,
				(char *)(skb+1)+incoming_size,
				ntohs(ipx->ipx_pktsize));
		}
		else
		{
			skb2=skb;
		}

		/* Now operate on the buffer */
		/* Increase hop count */
		
		skb2->dev = rt->dev;
		rt->datalink->datalink_header(rt->datalink, skb2, 
			(rt->flags&IPX_RT_ROUTED)?rt->router_node
						:ipx->ipx_dest.node);

		dev_queue_xmit(skb2,rt->dev,SOPRI_NORMAL);

		if(free_it)
			kfree_skb(skb,FREE_READ);
		return(0);
	}
	/************ End of router: Now sanity check stuff for us ***************/
	
	/* Ok its for us ! */
	if (ln->net == 0L) {
/*		printk("IPX: Registering local net %lx\n", ipx->ipx_dest.net);*/
		ln->net = ipx->ipx_dest.net;
	}

	sock=ipx_find_socket(ipx->ipx_dest.sock);
	if(sock==NULL)	/* But not one of our sockets */
	{
		kfree_skb(skb,FREE_READ);
		return(0);
	}

	/* Check to see if this socket needs its network number */
	ln = ipxrtr_get_default_net();
	if (sock->ipx_source_addr.net == 0L)
		sock->ipx_source_addr.net = ln->net;
	
	if(sock->rmem_alloc>=sock->rcvbuf)
	{
		kfree_skb(skb,FREE_READ);	/* Socket is full */
		return(0);
	}
	
	sock->rmem_alloc+=skb->mem_len;
	skb->sk = sock;

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
	struct sockaddr_ipx local_sipx;
	struct sk_buff *skb;
	struct device *dev;
	struct ipx_packet *ipx;
	int size;
	ipx_route *rt;
	struct datalink_proto *dl = NULL;
	unsigned char IPXaddr[6];
	int self_addressing = 0;
	int broadcast = 0;

	if(flags)
		return -EINVAL;
		
	if(usipx)
	{
		if(sk->ipx_source_addr.net==0)
		/* put the autobinding in */
		{
			struct sockaddr_ipx uaddr;
			int ret;

			uaddr.sipx_port = 0;
			uaddr.sipx_network = 0L; 
			ret = ipx_bind (sock, (struct sockaddr *)&uaddr, sizeof(struct sockaddr_ipx));
			if (ret != 0) return (ret);
		}

		if(addr_len <sizeof(*usipx))
			return(-EINVAL);
		if(usipx->sipx_family != AF_IPX)
			return -EINVAL;
		if(htons(usipx->sipx_port)<0x4000 && !suser())
			return -EPERM;
	}
	else
	{
		if(sk->state!=TCP_ESTABLISHED)
			return -ENOTCONN;
		usipx=&local_sipx;
		usipx->sipx_family=AF_IPX;
		usipx->sipx_port=sk->ipx_dest_addr.sock;
		usipx->sipx_network=sk->ipx_dest_addr.net;
		memcpy(usipx->sipx_node,sk->ipx_dest_addr.node,sizeof(usipx->sipx_node));
	}
	
	if(sk->debug)
		printk("IPX: sendto: Addresses built.\n");

	if(memcmp(&usipx->sipx_node,&ipx_broadcast_node,6)==0) 
	{
		if (!sk->broadcast)
			return -ENETUNREACH;
		broadcast = 1;
	}

	/* Build a packet */
	
	if(sk->debug)
		printk("IPX: sendto: building packet.\n");
		
	size=sizeof(ipx_packet)+len;	/* For mac headers */

	/* Find out where this has to go */
	if (usipx->sipx_network == 0L) {
		rt = ipxrtr_get_default_net();
		if (rt != NULL)
			usipx->sipx_network = rt->net;
	} else
		rt=ipxrtr_get_dev(usipx->sipx_network);

	if(rt==NULL)
	{
		return -ENETUNREACH;
	}
	
	dev=rt->dev;
	dl = rt->datalink;
	
	size += dev->hard_header_len;
	size += dl->header_length;

	if(sk->debug)
		printk("IPX: sendto: allocating buffer (%d)\n",size);
	
	if(size+sk->wmem_alloc>sk->sndbuf) {
		return -EAGAIN;
	}
		
	skb=alloc_skb(size,GFP_KERNEL);
	if(skb==NULL)
		return -ENOMEM;
		
	skb->mem_addr=skb;
	skb->sk=sk;
	skb->free=1;
	skb->arp=1;
	skb->len=size;

	sk->wmem_alloc+=skb->mem_len;

	if(sk->debug)
		printk("Building MAC header.\n");		
	skb->dev=rt->dev;
	
	/* Build Data Link header */
	dl->datalink_header(dl, skb, 
		(rt->flags&IPX_RT_ROUTED)?rt->router_node:usipx->sipx_node);

	/* See if we are sending to ourself */
	memset(IPXaddr, '\0', 6);
	memcpy(IPXaddr+(6 - skb->dev->addr_len), skb->dev->dev_addr, 
			skb->dev->addr_len);

	self_addressing = !memcmp(IPXaddr, 
				(rt->flags&IPX_RT_ROUTED)?rt->router_node
				:usipx->sipx_node,
				6);

	/* Now the IPX */
	if(sk->debug)
		printk("Building IPX Header.\n");
	ipx=(ipx_packet *)skb->h.raw;
	ipx->ipx_checksum=0xFFFF;
	ipx->ipx_pktsize=htons(len+sizeof(ipx_packet));
	ipx->ipx_tctrl=0;
	ipx->ipx_type=usipx->sipx_type;

	memcpy(&ipx->ipx_source,&sk->ipx_source_addr,sizeof(ipx->ipx_source));
	ipx->ipx_dest.net=usipx->sipx_network;
	memcpy(ipx->ipx_dest.node,usipx->sipx_node,sizeof(ipx->ipx_dest.node));
	ipx->ipx_dest.sock=usipx->sipx_port;
	if(sk->debug)
		printk("IPX: Appending user data.\n");
	/* User data follows immediately after the IPX data */
	memcpy_fromfs((char *)(ipx+1),ubuf,len);
	if(sk->debug)
		printk("IPX: Transmitting buffer\n");
	if((dev->flags&IFF_LOOPBACK) || self_addressing) {
		struct packet_type	pt;

		/* loop back */
		pt.type = rt->dlink_type;
		sk->wmem_alloc-=skb->mem_len;
		skb->sk = NULL;
		ipx_rcv(skb,dev,&pt);
	} else {
		if (broadcast) {
			struct packet_type	pt;
			struct sk_buff		*skb2;

			/* loop back */
			pt.type = rt->dlink_type;
			
			skb2=alloc_skb(skb->len, GFP_ATOMIC);
			skb2->mem_addr=skb2;
			skb2->free=1;
			skb2->arp=1;
			skb2->len=skb->len;
			skb2->sk = NULL;
			skb2->h.raw = skb2->data + rt->datalink->header_length
				+ dev->hard_header_len;
			memcpy(skb2->data, skb->data, skb->len);
			ipx_rcv(skb2,dev,&pt);
		}
		dev_queue_xmit(skb,dev,SOPRI_NORMAL);
	}
	return len;
}

static int ipx_send(struct socket *sock, void *ubuf, int size, int noblock, unsigned flags)
{
	return ipx_sendto(sock,ubuf,size,noblock,flags,NULL,0);
}

static int ipx_recvfrom(struct socket *sock, void *ubuf, int size, int noblock,
		   unsigned flags, struct sockaddr *sip, int *addr_len)
{
	ipx_socket *sk=(ipx_socket *)sock->data;
	struct sockaddr_ipx *sipx=(struct sockaddr_ipx *)sip;
	struct ipx_packet	*ipx = NULL;
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
	
	if(addr_len)
		*addr_len=sizeof(*sipx);

	skb=skb_recv_datagram(sk,flags,noblock,&er);
	if(skb==NULL)
		return er;

	ipx = (ipx_packet *)(skb->h.raw);
	copied=ntohs(ipx->ipx_pktsize) - sizeof(ipx_packet);
	skb_copy_datagram(skb,sizeof(struct ipx_packet),ubuf,copied);
	
	if(sipx)
	{
		sipx->sipx_family=AF_IPX;
		sipx->sipx_port=ipx->ipx_source.sock;
		memcpy(sipx->sipx_node,ipx->ipx_source.node,sizeof(sipx->sipx_node));
		sipx->sipx_network=ipx->ipx_source.net;
		sipx->sipx_type = ipx->ipx_type;
	}
	skb_free_datagram(skb);
	return(copied);
}		


static int ipx_write(struct socket *sock, char *ubuf, int size, int noblock)
{
	return ipx_send(sock,ubuf,size,noblock,0);
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
	int err;
	long amount=0;
	ipx_socket *sk=(ipx_socket *)sock->data;
	
	switch(cmd)
	{
		case TIOCOUTQ:
			err=verify_area(VERIFY_WRITE,(void *)arg,sizeof(unsigned long));
			if(err)
				return err;
			amount=sk->sndbuf-sk->wmem_alloc;
			if(amount<0)
				amount=0;
			put_fs_long(amount,(unsigned long *)arg);
			return 0;
		case TIOCINQ:
		{
			struct sk_buff *skb;
			/* These two are safe on a single CPU system as only user tasks fiddle here */
			if((skb=skb_peek(&sk->receive_queue))!=NULL)
				amount=skb->len;
			err=verify_area(VERIFY_WRITE,(void *)arg,sizeof(unsigned long));
			put_fs_long(amount,(unsigned long *)arg);
			return 0;
		}
		case SIOCADDRT:
		case SIOCDELRT:
			if(!suser())
				return -EPERM;
			return(ipxrtr_ioctl(cmd,(void *)arg));
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
		case SIOCGIFCONF:
		case SIOCGIFFLAGS:
		case SIOCSIFFLAGS:
		case SIOCGIFADDR:
		case SIOCSIFADDR:
		case SIOCGIFDSTADDR:
		case SIOCSIFDSTADDR:
		case SIOCGIFBRDADDR:
		case SIOCSIFBRDADDR:
		case SIOCGIFNETMASK:
		case SIOCSIFNETMASK:
		case SIOCGIFMETRIC:
		case SIOCSIFMETRIC:
		case SIOCGIFMEM:
		case SIOCSIFMEM:
		case SIOCGIFMTU:
		case SIOCSIFMTU:
		case SIOCSIFLINK:
		case SIOCGIFHWADDR:
		case SIOCSIFHWADDR:
		case OLD_SIOCGIFHWADDR:
			return(dev_ioctl(cmd,(void *) arg));


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
 

extern struct datalink_proto	*make_EII_client(void);
extern struct datalink_proto	*make_8023_client(void);

void ipx_proto_init(struct net_proto *pro)
{
	unsigned char	val = 0xE0;
	(void) sock_register(ipx_proto_ops.family, &ipx_proto_ops);

	pEII_datalink = make_EII_client();
	ipx_dix_packet_type.type=htons(ETH_P_IPX);
	dev_add_pack(&ipx_dix_packet_type);

	p8023_datalink = make_8023_client();
	ipx_8023_packet_type.type=htons(ETH_P_802_3);
	dev_add_pack(&ipx_8023_packet_type);
	
	if ((p8022_datalink = register_8022_client(val, ipx_rcv)) == NULL)
		printk("IPX: Unable to register with 802.2\n");
	
	printk("Swansea University Computer Society IPX 0.28 BETA for NET3.016\n");
	
}
#endif
