/*
 *	AF_INET6 socket family
 *	Linux INET6 implementation 
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *
 *	Adapted from linux/net/ipv4/af_inet.c
 *
 *	$Id: af_inet6.c,v 1.13 1996/10/31 19:47:17 roque Exp $
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */


#include <linux/module.h>
#include <linux/config.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>

#include <asm/uaccess.h>
#include <asm/system.h>

#include <linux/inet.h>
#include <linux/netdevice.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <net/protocol.h>
#include <net/arp.h>
#include <net/rarp.h>
#include <net/route.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/raw.h>
#include <net/icmp.h>
#include <linux/icmpv6.h>
#include <net/inet_common.h>
#include <net/transp_v6.h>
#include <net/ndisc.h>
#include <net/ipv6_route.h>
#include <net/sit.h>
#include <linux/ip_fw.h>
#include <net/addrconf.h>

/*
 *	Default callbacks for user INET sockets. These just wake up
 *	the user owning the socket.
 */

static void def_callback1(struct sock *sk)
{
	if(!sk->dead)
		wake_up_interruptible(sk->sleep);
}

static void def_callback2(struct sock *sk,int len)
{
	if(!sk->dead)
	{
		wake_up_interruptible(sk->sleep);
		sock_wake_async(sk->socket, 1);
	}
}

static void def_callback3(struct sock *sk)
{
	long wmem;
	
	wmem = (long) sk->wmem_alloc;

	if (wmem < 0) {
		printk(KERN_DEBUG "bug wmem_alloc < 0\n");
		sk->wmem_alloc = 0;
	}
		
	if(!sk->dead && sk->wmem_alloc*2 <= sk->sndbuf)
	{
		wake_up_interruptible(sk->sleep);
		sock_wake_async(sk->socket, 2);
	}
}

struct sock * rawv6_sock_array[SOCK_ARRAY_SIZE];

static int inet6_create(struct socket *sock, int protocol)
{
	struct sock *sk;
	struct proto *prot;
	int err;

	sk = sk_alloc(GFP_KERNEL);
	if (sk == NULL) 
		return(-ENOBUFS);

	/* Efficient way to set most fields to zero */
	memset(sk,0,sizeof(*sk));	

	/*
	 *	Note for tcp that also wiped the dummy_th block for us.
	 */

	switch(sock->type) 
	{
		case SOCK_STREAM:
		case SOCK_SEQPACKET:
			if (protocol && protocol != IPPROTO_TCP) 
			{
				kfree_s((void *)sk, sizeof(*sk));
				return(-EPROTONOSUPPORT);
			}
			protocol = IPPROTO_TCP;
			sk->no_check = TCP_NO_CHECK;
			prot = &tcpv6_prot;
			break;

		case SOCK_DGRAM:
			if (protocol && protocol != IPPROTO_UDP) 
			{
				kfree_s((void *)sk, sizeof(*sk));
				return(-EPROTONOSUPPORT);
			}
			protocol = IPPROTO_UDP;
			sk->no_check = UDP_NO_CHECK;
			prot=&udpv6_prot;
			break;
      
		case SOCK_RAW:
			if (!suser()) 
			{
				kfree_s((void *)sk, sizeof(*sk));
				return(-EPERM);
			}
			if (!protocol) 
			{
				kfree_s((void *)sk, sizeof(*sk));
				return(-EPROTONOSUPPORT);
			}
			prot = &rawv6_prot;
			sk->reuse = 1;
			sk->num = protocol;
			break;
		default:
			kfree_s((void *)sk, sizeof(*sk));
			return(-ESOCKTNOSUPPORT);
	}

	sk->socket = sock;

	sk->family = AF_INET6;
	sk->type = sock->type;
	sk->protocol = protocol;
	sk->allocation = GFP_KERNEL;
	sk->sndbuf = SK_WMEM_MAX;
	sk->rcvbuf = SK_RMEM_MAX;
	sk->priority = 1;

	sk->prot = prot;
	sk->backlog_rcv = prot->backlog_rcv;

	sk->sleep = sock->wait;
	sock->data =(void *) sk;

	sk->state = TCP_CLOSE;

	skb_queue_head_init(&sk->write_queue);
	skb_queue_head_init(&sk->receive_queue);
	skb_queue_head_init(&sk->back_log);

	sk->timer.data = (unsigned long)sk;
	sk->timer.function = &net_timer;
	init_timer(&sk->timer);

	sk->state_change = def_callback1;
	sk->data_ready   = def_callback2;
	sk->write_space  = def_callback3;
	sk->error_report = def_callback1;

	sk->net_pinfo.af_inet6.hop_limit  = ipv6_hop_limit;
	sk->net_pinfo.af_inet6.mcast_hops = IPV6_DEFAULT_MCASTHOPS;
	sk->net_pinfo.af_inet6.mc_loop	  = 1;

	/*
	 *	init the ipv4 part of the socket since
	 *	we can have sockets using v6 API for ipv4
	 */

	sk->ip_ttl=64;

#ifdef CONFIG_IP_MULTICAST
	sk->ip_mc_loop=1;
	sk->ip_mc_ttl=1;
	*sk->ip_mc_name=0;
	sk->ip_mc_list=NULL;
#endif


	if (sk->type==SOCK_RAW && protocol==IPPROTO_RAW)
		sk->ip_hdrincl=1;

	if (sk->num) 
	{
		/*
		 * It assumes that any protocol which allows
		 * the user to assign a number at socket
		 * creation time automatically
		 * shares.
		 */

		inet_put_sock(sk->num, sk);
		sk->dummy_th.source = ntohs(sk->num);
	}

	if (sk->prot->init) 
	{
		err = sk->prot->init(sk);
		if (err != 0) 
		{
			destroy_sock(sk);
			return(err);
		}
	}
	MOD_INC_USE_COUNT;
	return(0);
}

static int inet6_dup(struct socket *newsock, struct socket *oldsock)
{
	return(inet6_create(newsock, 
			    ((struct sock *)(oldsock->data))->protocol));
}


/*
 *	bind for INET6 API	
 */

static int inet6_bind(struct socket *sock, struct sockaddr *uaddr,
		      int addr_len)
{
	struct sockaddr_in6 *addr=(struct sockaddr_in6 *)uaddr;
	struct sock *sk=(struct sock *)sock->data, *sk2;
	__u32 v4addr = 0;
	unsigned short snum = 0;
	int addr_type = 0;

	/*
	 *	If the socket has its own bind function then use it.
	 */
	 
	if(sk->prot->bind)
		return sk->prot->bind(sk, uaddr, addr_len);
		
	/* check this error. */
	if (sk->state != TCP_CLOSE)
		return(-EINVAL);

	if(addr_len < sizeof(struct sockaddr_in6))
		return -EINVAL;
		
	if(sock->type != SOCK_RAW)
	{
		if (sk->num != 0) 
			return(-EINVAL);

		snum = ntohs(addr->sin6_port);
		
		if (snum == 0) 
			snum = get_new_socknum(sk->prot, 0);

		if (snum < PROT_SOCK && !suser()) 
			return(-EACCES);
	}
	
	addr_type = ipv6_addr_type(&addr->sin6_addr);

	if ((addr_type & IPV6_ADDR_MULTICAST) && sock->type == SOCK_STREAM)
	{
		return(-EINVAL);
	}

	/*
	 *	check if the address belongs to the host
	 */

	if (addr_type == IPV6_ADDR_MAPPED)
	{
		v4addr = addr->sin6_addr.s6_addr32[3];

		if (ip_chk_addr(v4addr) != IS_MYADDR)
			return(-EADDRNOTAVAIL);
	}
	else
	{
		if (addr_type != IPV6_ADDR_ANY)
		{
			/* 
			 *	ipv4 addr of the socket is invalid.
			 *	only the unpecified and mapped address	
			 *	have a v4 equivalent.
			 */

			v4addr = LOOPBACK4_IPV6;

			if (!(addr_type & IPV6_ADDR_MULTICAST))
			{
				if (ipv6_chk_addr(&addr->sin6_addr) == NULL)
					return(-EADDRNOTAVAIL);
			}
		}
	}

	sk->rcv_saddr = v4addr;
	sk->saddr = v4addr;
		
	memcpy(&sk->net_pinfo.af_inet6.rcv_saddr, &addr->sin6_addr, 
	       sizeof(struct in6_addr));
		
	if (!(addr_type & IPV6_ADDR_MULTICAST))
		memcpy(&sk->net_pinfo.af_inet6.saddr, &addr->sin6_addr, 
		       sizeof(struct in6_addr));

	if(sock->type != SOCK_RAW)
	{
		/* Make sure we are allowed to bind here. */
		cli();
		for(sk2 = sk->prot->sock_array[snum & (SOCK_ARRAY_SIZE -1)];
					sk2 != NULL; sk2 = sk2->next) 
		{
			/*
			 *	Hash collision or real match ?
			 */
			 
			if (sk2->num != snum) 
				continue;
				
			/*
			 *	Either bind on the port is wildcard means
			 *	they will overlap and thus be in error.
			 *	We use the sk2 v4 address to test the 
			 *	other socket since addr_any in av4 implies
			 *	addr_any in v6
			 */			
			 
			if (addr_type == IPV6_ADDR_ANY || (!sk2->rcv_saddr))
			{
				/*
				 *	Allow only if both are setting reuse.
				 */
				if(sk2->reuse && sk->reuse && sk2->state!=TCP_LISTEN)
					continue;
				sti();
				return(-EADDRINUSE);
			}

			/*
			 *	Two binds match ?
			 */

			if (ipv6_addr_cmp(&sk->net_pinfo.af_inet6.rcv_saddr,
					  &sk2->net_pinfo.af_inet6.rcv_saddr))

				continue;
			/*
			 *	Reusable port ?
			 */

			if (!sk->reuse)
			{
				sti();
				return(-EADDRINUSE);
			}
			
			/*
			 *	Reuse ?
			 */
			 
			if (!sk2->reuse || sk2->state==TCP_LISTEN) 
			{
				sti();
				return(-EADDRINUSE);
			}
		}
		sti();

		inet_remove_sock(sk);
		
		/*
		if(sock->type==SOCK_DGRAM)
			udp_cache_zap();
		if(sock->type==SOCK_STREAM)
			tcp_cache_zap();
			*/
		inet_put_sock(snum, sk);
		sk->dummy_th.source = ntohs(sk->num);
		sk->dummy_th.dest = 0;
		sk->daddr = 0;
	}

	return(0);
}

static int inet6_release(struct socket *sock, struct socket *peer)
{
	MOD_DEC_USE_COUNT;
	return inet_release(sock, peer);
}

static int inet6_socketpair(struct socket *sock1, struct socket *sock2)
{
	return(-EOPNOTSUPP);
}

/*
 *	This does both peername and sockname.
 */
 
static int inet6_getname(struct socket *sock, struct sockaddr *uaddr,
		 int *uaddr_len, int peer)
{
	struct sockaddr_in6 *sin=(struct sockaddr_in6 *)uaddr;
	struct sock *sk;
  
	sin->sin6_family = AF_INET6;
	sk = (struct sock *) sock->data;
	if (peer) 
	{
		if (!tcp_connected(sk->state))
			return(-ENOTCONN);
		sin->sin6_port = sk->dummy_th.dest;
		memcpy(&sin->sin6_addr, &sk->net_pinfo.af_inet6.daddr,
		       sizeof(struct in6_addr));
	} 
	else 
	{
		if (ipv6_addr_type(&sk->net_pinfo.af_inet6.rcv_saddr) ==
		    IPV6_ADDR_ANY)
			memcpy(&sin->sin6_addr, 
			       &sk->net_pinfo.af_inet6.saddr,
			       sizeof(struct in6_addr));

		else
			memcpy(&sin->sin6_addr, 
			       &sk->net_pinfo.af_inet6.rcv_saddr,
			       sizeof(struct in6_addr));

		sin->sin6_port = sk->dummy_th.source;

	}
	
	*uaddr_len = sizeof(*sin);	
	return(0);
}

static int inet6_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	struct sock *sk=(struct sock *)sock->data;
	int err;
	int pid;

	switch(cmd) 
	{
	case FIOSETOWN:
	case SIOCSPGRP:
		err = get_user(pid, (int *) arg);
		if(err)
			return err;

		/* see inet_fcntl */
		if (current->pid != pid && current->pgrp != -pid && !suser())
			return -EPERM;
		sk->proc = pid;
		return(0);
	case FIOGETOWN:
	case SIOCGPGRP:
		err = put_user(sk->proc,(int *)arg);
		if(err)
			return err;
		return(0);
	case SIOCGSTAMP:
		if(sk->stamp.tv_sec==0)
			return -ENOENT;
		err = copy_to_user((void *)arg, &sk->stamp,
				   sizeof(struct timeval));
		if (err)
			return -EFAULT;
		return 0;

	case SIOCADDRT:
	case SIOCDELRT:
	  
		return(ipv6_route_ioctl(cmd,(void *)arg));

	case SIOCGIFCONF:
	case SIOCGIFFLAGS:
	case SIOCSIFFLAGS:
	case SIOCADDMULTI:
	case SIOCDELMULTI:
/*

  this ioctls deal with addresses
  must process the addr info before
  calling dev_ioctl to perform dev specific functions

	case SIOCGIFADDR:
	case SIOCSIFADDR:


	case SIOCGIFDSTADDR:

	case SIOCGIFBRDADDR:
	case SIOCSIFBRDADDR:
	case SIOCGIFNETMASK:
	case SIOCSIFNETMASK:
	*/

	case SIOCGIFMETRIC:
	case SIOCSIFMETRIC:
	case SIOCGIFMEM:
	case SIOCSIFMEM:
	case SIOCGIFMTU:
	case SIOCSIFMTU:
	case SIOCSIFLINK:
	case SIOCGIFHWADDR:
	case SIOCSIFHWADDR:
	case SIOCSIFMAP:
	case SIOCGIFMAP:
	case SIOCSIFSLAVE:
	case SIOCGIFSLAVE:

		return(dev_ioctl(cmd,(void *) arg));		
		
		return -EINVAL;

	case SIOGIFINDEX:
		/*
		 *	This one will be moved to the generic device
		 *	layer in the near future
		 */
		return addrconf_get_ifindex((void *) arg);
		
	case SIOCSIFADDR:
		return addrconf_add_ifaddr((void *) arg);
	case SIOCSIFDSTADDR:
		return addrconf_set_dstaddr((void *) arg);
	default:
		if ((cmd >= SIOCDEVPRIVATE) &&
		    (cmd <= (SIOCDEVPRIVATE + 15)))
			return(dev_ioctl(cmd,(void *) arg));
		
		if (sk->prot->ioctl==NULL) 
			return(-EINVAL);
		return(sk->prot->ioctl(sk, cmd, arg));
	}
	/*NOTREACHED*/
	return(0);
}

/*
 * This routine must find a socket given a TCP or UDP header.
 * Everything is assumed to be in net order.
 *
 * We give priority to more closely bound ports: if some socket
 * is bound to a particular foreign address, it will get the packet
 * rather than somebody listening to any address..
 */

struct sock *inet6_get_sock(struct proto *prot, 
			    struct in6_addr *loc_addr, 
			    struct in6_addr *rmt_addr, 			   
			    unsigned short loc_port,
			    unsigned short rmt_port)
{
	struct sock *s;
	struct sock *result = NULL;
	int badness = -1;
	unsigned short hnum;
	struct ipv6_pinfo *np;
	hnum = ntohs(loc_port);

	/*
	 * SOCK_ARRAY_SIZE must be a power of two.  This will work better
	 * than a prime unless 3 or more sockets end up using the same
	 * array entry.  This should not be a problem because most
	 * well known sockets don't overlap that much, and for
	 * the other ones, we can just be careful about picking our
	 * socket number when we choose an arbitrary one.
	 */

	for(s = prot->sock_array[hnum & (SOCK_ARRAY_SIZE - 1)];
			s != NULL; s = s->next) 
	{
		int score = 0;
		
		if ((s->num != hnum) || s->family != AF_INET6)
			continue;

		if(s->dead && (s->state == TCP_CLOSE))
		{
			printk(KERN_DEBUG "dead or closed socket\n");
			continue;
		}

		np = &s->net_pinfo.af_inet6;

		/* remote port matches? */

		if (s->dummy_th.dest) {
			if (s->dummy_th.dest != rmt_port)
			{
				continue;
			}
			score++;
		}

		/* local address matches? */

		if (!ipv6_addr_any(&np->rcv_saddr))
		{
			if (ipv6_addr_cmp(&np->rcv_saddr, loc_addr))
			{
				continue;
			}
			score++;
		}

		/* remote address matches? */
		if (!ipv6_addr_any(&np->daddr))
		{
			if (ipv6_addr_cmp(&np->daddr, rmt_addr))
			{
				continue;
			}
			score++;
		}

		/* perfect match? */
		if (score == 3)
			return s;
		/* no, check if this is the best so far.. */
		if (score <= badness)
			continue;
		result = s;
		badness = score;
  	}
  	return result;
}

static int __inline__ inet6_mc_check(struct sock *sk, struct in6_addr *addr)
{
	struct ipv6_mc_socklist *mc;
		
	for (mc = sk->net_pinfo.af_inet6.ipv6_mc_list; mc; mc=mc->next)
	{
		if (ipv6_addr_cmp(&mc->addr, addr) == 0)
			return 1;
	}

	return 0;
}

/*
 *	Deliver a datagram to raw sockets.
 */
 
struct sock *inet6_get_sock_raw(struct sock *sk, unsigned short num,
				struct in6_addr *loc_addr, 
				struct in6_addr *rmt_addr)
			  
{
	struct sock *s;
	struct ipv6_pinfo *np;
	int addr_type = 0;

	s=sk;

	addr_type = ipv6_addr_type(loc_addr);

	for(; s != NULL; s = s->next) 
	{
		if (s->num != num) 
			continue;

		if(s->dead && (s->state == TCP_CLOSE))
			continue;

		np = &s->net_pinfo.af_inet6;

		if (!ipv6_addr_any(&np->daddr) &&
		    ipv6_addr_cmp(&np->daddr, rmt_addr))
		{
			continue;
		}

		if (!ipv6_addr_any(&np->rcv_saddr))
		{
			if (ipv6_addr_cmp(&np->rcv_saddr, loc_addr) == 0)
				return(s);
		
			if ((addr_type & IPV6_ADDR_MULTICAST) &&
			    inet6_mc_check(s, loc_addr))
				return (s);
			
			continue;
		}

		return(s);
  	}
  	return(NULL);
}

/*
 *	inet6_get_sock_mcast for UDP sockets.
 */

struct sock *inet6_get_sock_mcast(struct sock *sk, 
				  unsigned short num, unsigned short rmt_port,
				  struct in6_addr *loc_addr, 
				  struct in6_addr *rmt_addr)
{	
	struct sock *s;
	struct ipv6_pinfo *np;

	s=sk;

	for(; s != NULL; s = s->next) 
	{
		if (s->num != num) 
			continue;

		if(s->dead && (s->state == TCP_CLOSE))
			continue;

		np = &s->net_pinfo.af_inet6;

		if (s->dummy_th.dest) {
			if (s->dummy_th.dest != rmt_port)
			{
				continue;
			}
		}

		if (!ipv6_addr_any(&np->daddr) &&
		    ipv6_addr_cmp(&np->daddr, rmt_addr))
		{
			continue;
		}


		if (!ipv6_addr_any(&np->rcv_saddr))
		{
			if (ipv6_addr_cmp(&np->rcv_saddr, loc_addr) == 0)
				return(s);
		}
		
		if (!inet6_mc_check(s, loc_addr))
		{
			continue;
		}

		return(s);
  	}
  	return(NULL);
}
	

static struct proto_ops inet6_proto_ops = {
	AF_INET6,

	inet6_create,
	inet6_dup,
	inet6_release,
	inet6_bind,
	inet_connect,			/* ok		*/
	inet6_socketpair,		/* a do nothing	*/
	inet_accept,			/* ok		*/
	inet6_getname, 
	inet_select,			/* ok		*/
	inet6_ioctl,			/* must change  */
	inet_listen,			/* ok		*/
	inet_shutdown,			/* ok		*/
	inet_setsockopt,		/* ok		*/
	inet_getsockopt,		/* ok		*/
	inet_fcntl,			/* ok		*/
	inet_sendmsg,			/* ok		*/
	inet_recvmsg			/* ok		*/
};

#ifdef MODULE
int init_module(void)
#else
void inet6_proto_init(struct net_proto *pro)
#endif
{
	int i;

	printk(KERN_INFO "IPv6 v0.1 for NET3.037\n");

	sock_register(inet6_proto_ops.family, &inet6_proto_ops);
	
	for(i = 0; i < SOCK_ARRAY_SIZE; i++) 
	{
		rawv6_sock_array[i] = NULL;
  	}

	/*
	 *	ipngwg API draft makes clear that the correct semantics
	 *	for TCP and UDP is to consider one TCP and UDP instance
	 *	in a host availiable by both INET and INET6 APIs and
	 *	hable to communicate via both network protocols.
	 */
	
	tcpv6_prot.inuse = 0;
	tcpv6_prot.highestinuse = 0;       
	tcpv6_prot.sock_array = tcp_sock_array;

	udpv6_prot.inuse = 0;
	udpv6_prot.highestinuse = 0;
	udpv6_prot.sock_array = udp_sock_array;

	rawv6_prot.inuse = 0;
	rawv6_prot.highestinuse = 0;
	rawv6_prot.sock_array = rawv6_sock_array;
	
	ipv6_init();

	icmpv6_init(&inet6_proto_ops);
	ndisc_init(&inet6_proto_ops);

        addrconf_init();
 
        sit_init();

	/* init v6 transport protocols */

	udpv6_init();
	/* add /proc entries here */

	tcpv6_init();

#ifdef MODULE
	return 0;
#endif
}

#ifdef MODULE
void cleanup_module(void)
{
	sit_cleanup();
	ipv6_cleanup();
	sock_unregister(AF_INET6);
}
#endif

