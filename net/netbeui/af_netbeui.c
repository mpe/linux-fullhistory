
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
#include <linux/poll.h>
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

static netbeui_socket *netbeui_socket_list=NULL;

/*
 *	Note: Sockets may not be removed _during_ an interrupt or inet_bh
 *	handler using this technique. They can be added although we do not
 *	use this facility.
 */

extern inline void netbeui_remove_socket(netbeui_socket *sk)
{
	sklist_remove_socket(&netbeui_socket_list,sk);
}

extenr inline void netbeui_insert_socket(netbeui_socket *sk)
{
	sklist_insert_socket(&netbeui_socket_list,sk);
	netbeui_socket_list=sk;
	restore_flags(flags);
}

static void netbeui_destroy_socket(netbeui_socket *sk)
{
	/*
	 *	Release netbios logical channels first
	 */
	if(sk->af_nb.nb_link)
	{
		netbeui_delete_channel(sk->af_nb.nb_link);
		sk->af_nb.nb_link=NULL;
	}
	if(sk->af_nb.src_name)
	{
		netbeui_release_name(sk->af_nb.src_name);
		sk->af_nb.src_name=NULL;
	}
	if(sk->af_nb.dst_name)
	{
		netbeui_release_name(sk->af_nb.dst_name);
		sk->af_nb.dst_name=NULL;
	}
	netbeui_remove_listener(sk);
	sklist_destroy_socket(&netbeui_socket,sk);
}

/*
 *	Called from proc fs
 */

int netbeui_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	return 0;
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
	netbeui_add_listener(sk);
	return 0;
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

	MOD_INC_USE_COUNT;

	sock_init_data(sock,sk);
	sk->mtu=1500;
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
	int err;

	sk=(netbeui_socket *)sock->data;

	if(sk->zapped==0)
		return(-EINVAL);

	if(addr_len!=sizeof(struct sockaddr_netbeui))
		return -EINVAL;

	if(addr->snb_family!=AF_NETBEUI)
		return -EAFNOSUPPORT;

	/*
	 *	This will sleep. To meet POSIX it is non interruptible.
	 *	Someone should give the 1003.1g authors an injection of
	 *	imagination...
	 */
	 
	if(sk->af_nb.src_name!=NULL)
		return -EINVAL;
	
	/*
	 *	Try and get the name. It may return various 'invalid' name
	 *	problem reports or EADDRINUSE if we or another node holds
	 *	the desired name.
	 */
	 	
	sk->af_nb.src_name=netbeui_alloc_name(addr, &err);
	if(sk->af_nb.src_name==NULL)
		return err;
	/*
	 *	Add us to the active socket list 
	 */
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
	struct sockaddr_netbeui *addr=(struct sockaddr_netbeui *)uaddr;

	/*
	 *	Check pending operations
	 */	
	
	if(sk->state==TCP_ESTABLISHED && sock->state == SS_CONNECTING) 
	{
		sock->state==SS_CONNECTED;
		return 0;
	}
	
	if(sk->state == TCP_CLOSE & sock->state == SS_CONNECTING)
	{
		sock->state==SS_UNCONNECTED;
		return -ECONNREFUSED;
	}
	
	if(sock->state == SS_CONNECTING && (flags & O_NONBLOCK))
		return -EINPROGRESS;
	
	if(sk->state==TCP_ESTABLISHED)
		return -EISCONN;	 
	
	/*
	 *	If this is new it must really be new...
	 */
	 
	if(sk->af_nb.dst_name==NULL)
	{
		if(addr_len != sizeof(struct sockaddr_nb))
			return -EINVAL;
		if(addr->snb_family!=AF_NETBEUI)
			return -EAFNOSUPPORT;
		/*
		 *	Try and find the name
		 */
	}
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
	return nb_llc_rcv(skb);
}

static int netbeui_sendmsg(struct socket *sock, struct msghdr *msg, int len, int nonblock, int flags)
{
	netbeui_socket *sk=(netbeui_socket *)sock->data;
	struct sockaddr_nb *usnb=(struct sockaddr_nb *)msg->msg_name;
	struct sk_buff *skb;
	struct device *dev;
	struct nbhdr *nbp;
	int size;
	struct netbeui_route *rt;
	int loopback=0;
	int err;

	if(flags)
		return -EINVAL;

	if(len>1500)	/* - headers!! */
		return -EMSGSIZE;

	if(usnb)
	{
		if(sk->zapped)
		{
			if(netbeui_autobind(sk)<0)
				return -EBUSY;
		}

		if(msg->msg_namelen <sizeof(*usnb))
			return(-EINVAL);
		if(usnb->snb_family != AF_NETBEUI)
			return -EINVAL;
		/* Check broadcast */
	}
	else
	{
		if(sk->state!=TCP_ESTABLISHED)
			return -ENOTCONN;
		/* Connected .. */
	}

	/* Build a packet */
	SOCK_DEBUG(sk, "SK %p: Got address.\n",sk);
	size=sizeof(struct nbhdr)+len+nb_dl->header_length;	/* For headers */

	SOCK_DEBUG(sk, "SK %p: Size needed %d, device %s\n", sk, size, dev->name);
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
	SOCK_DEBUG(sk, "SK %p: Begin build.\n", sk);
	nbp=(struct nbhdr *)skb_put(skb,sizeof(struct nbhdr));
	SOCK_DEBUG(sk, "SK %p: Copy user data (%d bytes).\n", sk, len);
	err = memcpy_fromiovec(skb_put(skb,len),msg->msg_iov,len);
	if (err)
	{
		kfree_skb(skb, FREE_WRITE);
		return -EFAULT;
	}

#ifdef CONFIG_FIREWALL

	if(call_out_firewall(AF_NETBEUI, skb->dev, nbp, NULL)!=FW_ACCEPT)
	{
		kfree_skb(skb, FREE_WRITE);
		return -EPERM;
	}

#endif

	if(nb_send_low(dev,skb,&usat->sat_addr, NULL)==-1)
		kfree_skb(skb, FREE_WRITE);
	SOCK_DEBUG(sk, "SK %p: Done write (%d).\n", sk, len);
	return len;
}


static int netbeui_recvmsg(struct socket *sock, struct msghdr *msg, int size, int noblock, int flags, int *addr_len)
{
	netbeui_socket *sk=(netbeui_socket *)sock->data;
	struct sockaddr_nb *snb=(struct sockaddr_nb *)msg->msg_name;
	struct nbphdr	*nbp = NULL;
	int copied = 0;
	struct sk_buff *skb;
	int er = 0;

	if(addr_len)
		*addr_len=sizeof(*snb);

	skb=skb_recv_datagram(sk,flags,noblock,&er);
	if(skb==NULL)
		return er;

	snb = (struct nbphdr *)(skb->h.raw);
	if(sk->type==SOCK_RAW)
	{
		copied=skb->len
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
		copied=skb->len - sizeof(*nbp);
		if (copied > size)
		{
			copied = size;
			msg->msg_flags|=MSG_TRUNC;
		}
		er = skb_copy_datagram_iovec(skb,sizeof(*nbp),msg->msg_iov,copied);
		if (er)
			goto out;
	}
	if(snb)
	{
		sat->sat_family=AF_NETBEUI;
		/* Copy name over */
	}
out:
	skb_free_datagram(sk, skb);
	return er ? er : (copied);
}


static int netbeui_shutdown(struct socket *sk,int how)
{
	return -EOPNOTSUPP;
}

static int netbeui_poll(struct socket *sock, poll_table *wait)
{
	netbeui_socket *sk=(netbeui_socket *)sock->data;

	return datagram_poll(sk,wait);
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
			return(nbrtr_ioctl(cmd,(void *)arg));
		/*
		 *	Interface
		 */
		case SIOCGIFADDR:
		case SIOCSIFADDR:
		case SIOCGIFBRDADDR:
			return nbif_ioctl(cmd,(void *)arg);
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
	netbeui_poll,
	netbeui_ioctl,
	netbeui_listen,
	netbeui_shutdown,
	sock_no_setsockopt,
	sock_no_getsockopt,
	sock_no_fcntl,
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
	if ((nb_dl = register_8022_client(nb_8022_id, netbeui_rcv)) == NULL)
		printk(KERN_CRIT "Unable to register Netbeui with 802.2.\n");

	register_netdevice_notifier(&nb_notifier);

#ifdef CONFIG_PROC_FS
	proc_net_register(&proc_netbeui);
#endif

	printk(KERN_INFO "NetBEUI 0.03 for Linux NET3.037\n");
}

#ifdef MODULE
EXPORT_NO_SYMBOLS;

int init_module(void)
{
	netbeui_proto_init(NULL);
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
