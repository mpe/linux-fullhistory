 
#include <linux/config.h>
#include <linux/module.h>
#include <asm/uaccess.h>
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
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/firewall.h>


#undef NETBEUI_DEBUG


#ifdef NETBEUI_DEBUG
#define DPRINT(x)		print(x)
#else
#define DPRINT(x)
#endif

#define min(a,b)	(((a)<(b))?(a):(b))

/***********************************************************************************************************************\
*															*
*						Handlers for the socket list.						*
*															*
\***********************************************************************************************************************/

static netbeui_socket *volatile netbeui_socket_list=NULL;

/*
 *	Note: Sockets may not be removed _during_ an interrupt or inet_bh
 *	handler using this technique. They can be added although we do not
 *	use this facility.
 */
 
static void netbeui_remove_socket(netbeui_socket *sk)
{
	unsigned long flags;
	netbeui_socket *s;
	
	save_flags(flags);
	cli();
	
	s=netbeui_socket_list;
	if(s==sk)
	{
		netbeui_socket_list=s->next;
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

static void netbeui_insert_socket(netbeui_socket *sk)
{
	unsigned long flags;
	save_flags(flags);
	cli();
	sk->next=netbeui_socket_list;
	netbeui_socket_list=sk;
	restore_flags(flags);
}

/*
 *	This is only called from user mode. Thus it protects itself against
 *	interrupt users but doesn't worry about being called during work.
 *	Once it is removed from the queue no interrupt or bottom half will
 *	touch it and we are (fairly 8-) ) safe.
 */

static void netbeui_destroy_socket(netbeui_socket *sk);

/*
 *	Handler for deferred kills.
 */
 
static void netbeui_destroy_timer(unsigned long data)
{
	netbeui_destroy_socket((netbeui_socket *)data);
}

static void netbeui_destroy_socket(netbeui_socket *sk)
{
	struct sk_buff *skb;
	netbeui_remove_socket(sk);
	
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
		sk->timer.function=netbeui_destroy_timer;
		sk->timer.data = (unsigned long)sk;
		add_timer(&sk->timer);
	}
}


/*
 *	Called from proc fs 
 */
 
int netbeui_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	netbeui_socket *s;
	int len=0;
	off_t pos=0;
	off_t begin=0;

	/*
	 *	Output the netbeui data for the /proc virtual fs.
	 */

	len += sprintf (buffer,"Type local_addr  remote_addr tx_queue rx_queue st uid\n");
	for (s = netbeui_socket_list; s != NULL; s = s->next)
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

/*
 *	A device event has occurred. Watch for devices going down and
 *	delete our use of them (iface and route).
 */

static int nb_device_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	if(event==NETDEV_DOWN)
	{
		/* Discard any use of this */
		netbeui_drop_device((struct device *)ptr);
	}
	return NOTIFY_DONE;
}

/*******************************************************************************************************************\
*													            *
*	      Handling for system calls applied via the various interfaces to a netbeui socket object		    *
*														    *
\*******************************************************************************************************************/

/*
 *	Generic fcntl calls are already dealt with. If we don't need funny ones
 *	this is the all you need. Async I/O is also separate.
 */
  
static int netbeui_fcntl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
/*	netbeui_socket *sk=(netbeui_socket *)sock->data;*/
	switch(cmd)
	{
		default:
			return(-EINVAL);
	}
}

/*
 *	Set 'magic' options for netbeui. If we don't have any this is fine 
 *	as it is.
 */
 
static int netbeui_setsockopt(struct socket *sock, int level, int optname, char *optval, int optlen)
{
	netbeui_socket *sk;
	int err,opt;
	
	sk=(netbeui_socket *)sock->data;
	
	if(optval==NULL)
		return(-EINVAL);

	err = get_user(opt, (int *)optval);
	if (err)
		return err;
	
	switch(level)
	{
		case SOL_NETBEUI:
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
 
static int netbeui_getsockopt(struct socket *sock, int level, int optname,
	char *optval, int *optlen)
{
	netbeui_socket *sk;
	int val=0;
	int err;
	
	sk=(netbeui_socket *)sock->data;

	switch(level)
	{

		case SOL_NETBEUI:
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
	err = put_user(sizeof(int),optlen);
	if (!err)
		err = put_user(val, (int *) optval);
	return err;
}

/*
 *	Only for connection oriented sockets - ignore
 */
 
static int netbeui_listen(struct socket *sock, int backlog)
{
	struct sock *sk=(netbeui_socket *)sock->data;
	if(sk->state!=TCP_CLOSED)
		return -EINVAL;
	if(backlog<0)
		return -EINVAL;
	if(backlog<128)
		sk->backlog=backlog;
	else
		sk->backlog=128;
	sk->state=TCP_LISTEN;
	sk->state_change(sk);
	netbeui_llc_listen(sk);
	return 0;	
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
 
static int netbeui_create(struct socket *sock, int protocol)
{
	netbeui_socket *sk;
	sk=(netbeui_socket *)sk_alloc(GFP_KERNEL);
	if(sk==NULL)
		return(-ENOBUFS);
	switch(sock->type)
	{
		case SOCK_DGRAM:
			break;
		case SOCK_SEQPACKET:
			break;
		default:
			sk_free((void *)sk);
			return(-ESOCKTNOSUPPORT);
	}
	
	sk->llc802=llc_alloc(GFP_KERNEL);
	if(sk->llc802==NULL)
	{
		sk_free((void *)sk);
		return -ENOBUFS:
	}
	
	MOD_INC_USE_COUNT;

	sk->allocation=GFP_KERNEL;
	sk->rcvbuf=SK_RMEM_MAX;
	sk->sndbuf=SK_WMEM_MAX;
	sk->pair=NULL;
	sk->priority=SOPRI_NORMAL;
	skb_queue_head_init(&sk->receive_queue);
	skb_queue_head_init(&sk->write_queue);
	skb_queue_head_init(&sk->back_log);
	sk->state=TCP_CLOSE;
	sk->socket=sock;
	sk->type=sock->type;
	sk->mtu=1500;
	
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
 
static int netbeui_dup(struct socket *newsock,struct socket *oldsock)
{
	return(netbeui_create(newsock,oldsock->type));
}

/*
 *	Free a socket. No work needed
 */
 
static int netbeui_release(struct socket *sock, struct socket *peer)
{
	netbeui_socket *sk=(netbeui_socket *)sock->data;
	if(sk==NULL)
		return(0);
	if(!sk->dead)
		sk->state_change(sk);
	sk->dead=1;
	sock->data=NULL;
	netbeui_destroy_socket(sk);
	return(0);
}
		
/*
 *	Set the address 'our end' of the connection.
 */
 
static int netbeui_bind(struct socket *sock, struct sockaddr *uaddr,size_t addr_len)
{
	netbeui_socket *sk;
	struct sockaddr_netbeui *addr=(struct sockaddr_netbeui *)uaddr;
	
	sk=(netbeui_socket *)sock->data;
	
	if(sk->zapped==0)
		return(-EINVAL);
		
	if(addr_len!=sizeof(struct sockaddr_at))
		return -EINVAL;

	if(addr->sat_family!=AF_NETBEUI)
		return -EAFNOSUPPORT;

	if(netbeui_find_socket(addr)!=NULL)
		return -EADDRINUSE;	   

	netbeui_insert_socket(sk);
	sk->zapped=0;
	return(0);
}

/*
 *	Set the address we talk to.
 */
 
static int netbeui_connect(struct socket *sock, struct sockaddr *uaddr,
	size_t addr_len, int flags)
{
	netbeui_socket *sk=(netbeui_socket *)sock->data;
	struct sockaddr_netbeui *addr;
	
	sk->state = TCP_CLOSE;	
	sock->state = SS_UNCONNECTED;

	if(addr_len!=sizeof(*addr))
		return(-EINVAL);
	addr=(struct sockaddr_netbeui *)uaddr;
	
	if(addr->sat_family!=AF_NETBEUI)
		return -EAFNOSUPPORT;

/* FIXME - Netbios broadcast semantics ?? */
	if(addr->sat_addr.s_node==ATADDR_BCAST && !sk->broadcast)
		return -EACCES;

	if(sk->zapped)
		return -EINVAL;
	
	if(atrtr_get_dev(&addr->sat_addr)==NULL)
		return -ENETUNREACH;
		
}

/*
 *	Not relevant
 */
 
static int netbeui_socketpair(struct socket *sock1, struct socket *sock2)
{
	return(-EOPNOTSUPP);
}

/*
 *	WRITE ME
 */
 
static int netbeui_accept(struct socket *sock, struct socket *newsock, int flags)
{
	if(newsock->data)
		sk_free(newsock->data);
	return -EOPNOTSUPP;
}

/*
 *	Find the name of a netbeui socket. Just copy the right
 *	fields into the sockaddr.
 */
 
static int netbeui_getname(struct socket *sock, struct sockaddr *uaddr,
	size_t *uaddr_len, int peer)
{
	struct sockaddr_netbeui snb;
	netbeui_socket *sk;
	
	sk=(netbeui_socket *)sock->data;
	if(sk->zapped)
	{
		return -EINVAL;
	}	
	
	*uaddr_len = sizeof(struct sockaddr_netbeui);
		
	if(peer)
	{
		if(sk->state!=TCP_ESTABLISHED)
			return -ENOTCONN;
	}
	else
	{
	}
	snb.snb_family = AF_NETBEUI;
	memcpy(uaddr,&snb,sizeof(snb));
	return(0);
}

/*
 *	Receive a packet (in skb) from device dev.
 */
 
static int netbeui_rcv(struct sk_buff *skb, struct device *dev, struct packet_type *pt)
{
	netbeui_socket *sock;
}

static int netbeui_sendmsg(struct socket *sock, struct msghdr *msg, int len, int nonblock, int flags)
{
	netbeui_socket *sk=(netbeui_socket *)sock->data;
	struct sockaddr_at *usat=(struct sockaddr_at *)msg->msg_name;
	struct sockaddr_at local_snetbeui, gsat;
	struct sk_buff *skb;
	struct device *dev;
	struct ddpehdr *ddp;
	int size;
	struct netbeui_route *rt;
	int loopback=0;
	int err;
	
	if(flags)
		return -EINVAL;
		
	if(len>587)
		return -EMSGSIZE;
		
	if(usat)
	{
		if(sk->zapped)
		{
			if(netbeui_autobind(sk)<0)
				return -EBUSY;
		}

		if(msg->msg_namelen <sizeof(*usat))
			return(-EINVAL);
		if(usat->sat_family != AF_NETBEUI)
			return -EINVAL;
#if 0 	/* netnetbeui doesn't implement this check */
		if(usat->sat_addr.s_node==ATADDR_BCAST && !sk->broadcast)
			return -EPERM;
#endif			
	}
	else
	{
		if(sk->state!=TCP_ESTABLISHED)
			return -ENOTCONN;
		usat=&local_snetbeui;
		usat->sat_family=AF_NETBEUI;
		usat->sat_port=sk->protinfo.af_at.dest_port;
		usat->sat_addr.s_node=sk->protinfo.af_at.dest_node;
		usat->sat_addr.s_net=sk->protinfo.af_at.dest_net;
	}
	
	/* Build a packet */
	
	if(sk->debug)
		printk("SK %p: Got address.\n",sk);
	
	size=sizeof(struct ddpehdr)+len+nb_dl->header_length;	/* For headers */

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
	skb_reserve(skb,nb_dl->header_length);
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
		
	err = memcpy_fromiovec(skb_put(skb,len),msg->msg_iov,len);
	if (err)
	{
		kfree_skb(skb, FREE_WRITE);
		return -EFAULT;
	}
		
	if(sk->no_check==1)
		ddp->deh_sum=0;
	else
		ddp->deh_sum=netbeui_checksum(ddp, len+sizeof(*ddp));
		
#ifdef CONFIG_FIREWALL

	if(call_out_firewall(AF_NETBEUI, skb->dev, ddp, NULL)!=FW_ACCEPT)
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
		nb_dl->datalink_header(nb_dl, skb, dev->dev_addr);
		skb->sk = NULL;
		skb->mac.raw=skb->data;
		skb->h.raw = skb->data + nb_dl->header_length + dev->hard_header_len;
		skb_pull(skb,dev->hard_header_len);
		skb_pull(skb,nb_dl->header_length);
		netbeui_rcv(skb,dev,NULL);
	}
	else 
	{
		if(sk->debug)
			printk("SK %p: send out.\n", sk);

		if ( rt->flags & RTF_GATEWAY ) {
		    gsat.sat_addr = rt->gateway;
		    usat = &gsat;
		}
	
		if(nb_send_low(dev,skb,&usat->sat_addr, NULL)==-1)
			kfree_skb(skb, FREE_WRITE);
		/* else queued/sent above in the aarp queue */
	}
	if(sk->debug)
		printk("SK %p: Done write (%d).\n", sk, len);
	return len;
}


static int netbeui_recvmsg(struct socket *sock, struct msghdr *msg, int size, int noblock, int flags, int *addr_len)
{
	netbeui_socket *sk=(netbeui_socket *)sock->data;
	struct sockaddr_at *sat=(struct sockaddr_at *)msg->msg_name;
	struct ddpehdr	*ddp = NULL;
	int copied = 0;
	struct sk_buff *skb;
	int er = 0;
	
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
		{
			copied=size;
			msg->msg_flags|=MSG_TRUNC;
		}
		er = skb_copy_datagram_iovec(skb,0,msg->msg_iov,copied);
		if (er)
			goto out;
	}
	else
	{
		copied=ddp->deh_len - sizeof(*ddp);
		if (copied > size)
		{
			copied = size;
			msg->msg_flags|=MSG_TRUNC;
		}
		er = skb_copy_datagram_iovec(skb,sizeof(*ddp),msg->msg_iov,copied);
		if (er) 
			goto out; 
	}
	if(sat)
	{
		sat->sat_family=AF_NETBEUI;
		sat->sat_port=ddp->deh_sport;
		sat->sat_addr.s_node=ddp->deh_snode;
		sat->sat_addr.s_net=ddp->deh_snet;
	}
out:
	skb_free_datagram(sk, skb);
	return er ? er : (copied);
}		


static int netbeui_shutdown(struct socket *sk,int how)
{
	return -EOPNOTSUPP;
}

static int netbeui_select(struct socket *sock , int sel_type, select_table *wait)
{
	netbeui_socket *sk=(netbeui_socket *)sock->data;
	
	return datagram_select(sk,sel_type,wait);
}

/*
 *	Netbeui ioctl calls.
 */

static int netbeui_ioctl(struct socket *sock,unsigned int cmd, unsigned long arg)
{
	long amount=0;
	netbeui_socket *sk=(netbeui_socket *)sock->data;
	
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
				return copy_to_user((void *)arg,&sk->stamp,sizeof(struct timeval)) ? -EFAULT : 0;
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
	return put_user(amount, (int *)arg);
}

static struct proto_ops netbeui_proto_ops = {
	AF_NETBEUI,
	
	netbeui_create,
	netbeui_dup,
	netbeui_release,
	netbeui_bind,
	netbeui_connect,
	netbeui_socketpair,
	netbeui_accept,
	netbeui_getname,
	netbeui_select,
	netbeui_ioctl,
	netbeui_listen,
	netbeui_shutdown,
	netbeui_setsockopt,
	netbeui_getsockopt,
	netbeui_fcntl,
	netbeui_sendmsg,
	netbeui_recvmsg
};

static struct notifier_block nb_notifier={
	nb_device_event,
	NULL,
	0
};

static char nb_snap_id[]={0x08,0x00,0x07,0x80,0x9B};

#ifdef CONFIG_PROC_FS
static struct proc_dir_entry proc_netbeui = {
	PROC_NET_NETBEUI, 9, "netbeui",
	S_IFREG | S_IRUGO, 1, 0, 0
	0, &proc_net_inode_operations,
	netbeui_get_info
};
#endif

/* Called by proto.c on kernel start up */

void netbeui_proto_init(struct net_proto *pro)
{
	(void) sock_register(netbeui_proto_ops.family, &netbeui_proto_ops);
/* ddp?  isn't it atalk too? 8) */
	if ((nb_dl = register_snap_client(nb_snap_id, netbeui_rcv)) == NULL)
		printk(KERN_CRIT "Unable to register DDP with SNAP.\n");
	
	register_netdevice_notifier(&nb_notifier);

#ifdef CONFIG_PROC_FS
	proc_net_register(&proc_netbeui);
#endif	

	printk(KERN_INFO "NetBEUI 0.02 for Linux NET3.037\n");
}

#ifdef MODULE

int init_module(void)
{
	netbeui_proto_init(NULL);
	register_symtab(0);
	return 0;
}

void cleanup_module(void)
{
	unsigned long flags;
#ifdef CONFIG_PROC_FS
	proc_net_unregister(PROC_NET_NETBEUI);
#endif	
	unregister_netdevice_notifier(&nb_notifier);
	unregister_snap_client(nb_snap_id);
	sock_unregister(netbeui_proto_ops.family);
}

#endif  /* MODULE */
