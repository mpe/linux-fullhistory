/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Generic socket support routines. Memory allocators, socket lock/release
 *		handler for protocols to use and generic option handler.
 *
 *
 * Version:	$Id: sock.c,v 1.117 2002/02/01 22:01:03 davem Exp $
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Florian La Roche, <flla@stud.uni-sb.de>
 *		Alan Cox, <A.Cox@swansea.ac.uk>
 *
 * Fixes:
 *		Alan Cox	: 	Numerous verify_area() problems
 *		Alan Cox	:	Connecting on a connecting socket
 *					now returns an error for tcp.
 *		Alan Cox	:	sock->protocol is set correctly.
 *					and is not sometimes left as 0.
 *		Alan Cox	:	connect handles icmp errors on a
 *					connect properly. Unfortunately there
 *					is a restart syscall nasty there. I
 *					can't match BSD without hacking the C
 *					library. Ideas urgently sought!
 *		Alan Cox	:	Disallow bind() to addresses that are
 *					not ours - especially broadcast ones!!
 *		Alan Cox	:	Socket 1024 _IS_ ok for users. (fencepost)
 *		Alan Cox	:	sock_wfree/sock_rfree don't destroy sockets,
 *					instead they leave that for the DESTROY timer.
 *		Alan Cox	:	Clean up error flag in accept
 *		Alan Cox	:	TCP ack handling is buggy, the DESTROY timer
 *					was buggy. Put a remove_sock() in the handler
 *					for memory when we hit 0. Also altered the timer
 *					code. The ACK stuff can wait and needs major 
 *					TCP layer surgery.
 *		Alan Cox	:	Fixed TCP ack bug, removed remove sock
 *					and fixed timer/inet_bh race.
 *		Alan Cox	:	Added zapped flag for TCP
 *		Alan Cox	:	Move kfree_skb into skbuff.c and tidied up surplus code
 *		Alan Cox	:	for new sk_buff allocations wmalloc/rmalloc now call alloc_skb
 *		Alan Cox	:	kfree_s calls now are kfree_skbmem so we can track skb resources
 *		Alan Cox	:	Supports socket option broadcast now as does udp. Packet and raw need fixing.
 *		Alan Cox	:	Added RCVBUF,SNDBUF size setting. It suddenly occurred to me how easy it was so...
 *		Rick Sladkey	:	Relaxed UDP rules for matching packets.
 *		C.E.Hawkins	:	IFF_PROMISC/SIOCGHWADDR support
 *	Pauline Middelink	:	identd support
 *		Alan Cox	:	Fixed connect() taking signals I think.
 *		Alan Cox	:	SO_LINGER supported
 *		Alan Cox	:	Error reporting fixes
 *		Anonymous	:	inet_create tidied up (sk->reuse setting)
 *		Alan Cox	:	inet sockets don't set sk->type!
 *		Alan Cox	:	Split socket option code
 *		Alan Cox	:	Callbacks
 *		Alan Cox	:	Nagle flag for Charles & Johannes stuff
 *		Alex		:	Removed restriction on inet fioctl
 *		Alan Cox	:	Splitting INET from NET core
 *		Alan Cox	:	Fixed bogus SO_TYPE handling in getsockopt()
 *		Adam Caldwell	:	Missing return in SO_DONTROUTE/SO_DEBUG code
 *		Alan Cox	:	Split IP from generic code
 *		Alan Cox	:	New kfree_skbmem()
 *		Alan Cox	:	Make SO_DEBUG superuser only.
 *		Alan Cox	:	Allow anyone to clear SO_DEBUG
 *					(compatibility fix)
 *		Alan Cox	:	Added optimistic memory grabbing for AF_UNIX throughput.
 *		Alan Cox	:	Allocator for a socket is settable.
 *		Alan Cox	:	SO_ERROR includes soft errors.
 *		Alan Cox	:	Allow NULL arguments on some SO_ opts
 *		Alan Cox	: 	Generic socket allocation to make hooks
 *					easier (suggested by Craig Metz).
 *		Michael Pall	:	SO_ERROR returns positive errno again
 *              Steve Whitehouse:       Added default destructor to free
 *                                      protocol private data.
 *              Steve Whitehouse:       Added various other default routines
 *                                      common to several socket families.
 *              Chris Evans     :       Call suser() check last on F_SETOWN
 *		Jay Schulist	:	Added SO_ATTACH_FILTER and SO_DETACH_FILTER.
 *		Andi Kleen	:	Add sock_kmalloc()/sock_kfree_s()
 *		Andi Kleen	:	Fix write_space callback
 *		Chris Evans	:	Security fixes - signedness again
 *		Arnaldo C. Melo :       cleanups, use skb_queue_purge
 *
 * To Fix:
 *
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/poll.h>
#include <linux/tcp.h>
#include <linux/init.h>

#include <asm/uaccess.h>
#include <asm/system.h>

#include <linux/netdevice.h>
#include <net/protocol.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/xfrm.h>
#include <linux/ipsec.h>

#include <linux/filter.h>

#ifdef CONFIG_INET
#include <net/tcp.h>
#endif

/* Take into consideration the size of the struct sk_buff overhead in the
 * determination of these values, since that is non-constant across
 * platforms.  This makes socket queueing behavior and performance
 * not depend upon such differences.
 */
#define _SK_MEM_PACKETS		256
#define _SK_MEM_OVERHEAD	(sizeof(struct sk_buff) + 256)
#define SK_WMEM_MAX		(_SK_MEM_OVERHEAD * _SK_MEM_PACKETS)
#define SK_RMEM_MAX		(_SK_MEM_OVERHEAD * _SK_MEM_PACKETS)

/* Run time adjustable parameters. */
__u32 sysctl_wmem_max = SK_WMEM_MAX;
__u32 sysctl_rmem_max = SK_RMEM_MAX;
__u32 sysctl_wmem_default = SK_WMEM_MAX;
__u32 sysctl_rmem_default = SK_RMEM_MAX;

/* Maximal space eaten by iovec or ancilliary data plus some space */
int sysctl_optmem_max = sizeof(unsigned long)*(2*UIO_MAXIOV + 512);

static int sock_set_timeout(long *timeo_p, char __user *optval, int optlen)
{
	struct timeval tv;

	if (optlen < sizeof(tv))
		return -EINVAL;
	if (copy_from_user(&tv, optval, sizeof(tv)))
		return -EFAULT;

	*timeo_p = MAX_SCHEDULE_TIMEOUT;
	if (tv.tv_sec == 0 && tv.tv_usec == 0)
		return 0;
	if (tv.tv_sec < (MAX_SCHEDULE_TIMEOUT/HZ - 1))
		*timeo_p = tv.tv_sec*HZ + (tv.tv_usec+(1000000/HZ-1))/(1000000/HZ);
	return 0;
}

static void sock_warn_obsolete_bsdism(const char *name)
{
	static int warned;
	static char warncomm[TASK_COMM_LEN];
	if (strcmp(warncomm, current->comm) && warned < 5) { 
		strcpy(warncomm,  current->comm); 
		printk(KERN_WARNING "process `%s' is using obsolete "
		       "%s SO_BSDCOMPAT\n", warncomm, name);
		warned++;
	}
}

static void sock_disable_timestamp(struct sock *sk)
{	
	if (sock_flag(sk, SOCK_TIMESTAMP)) { 
		sock_reset_flag(sk, SOCK_TIMESTAMP);
		net_disable_timestamp();
	}
}


/*
 *	This is meant for all protocols to use and covers goings on
 *	at the socket level. Everything here is generic.
 */

int sock_setsockopt(struct socket *sock, int level, int optname,
		    char __user *optval, int optlen)
{
	struct sock *sk=sock->sk;
	struct sk_filter *filter;
	int val;
	int valbool;
	struct linger ling;
	int ret = 0;
	
	/*
	 *	Options without arguments
	 */

#ifdef SO_DONTLINGER		/* Compatibility item... */
	switch (optname) {
		case SO_DONTLINGER:
			sock_reset_flag(sk, SOCK_LINGER);
			return 0;
	}
#endif	
		
  	if(optlen<sizeof(int))
  		return(-EINVAL);
  	
	if (get_user(val, (int __user *)optval))
		return -EFAULT;
	
  	valbool = val?1:0;

	lock_sock(sk);

  	switch(optname) 
  	{
		case SO_DEBUG:	
			if(val && !capable(CAP_NET_ADMIN))
			{
				ret = -EACCES;
			}
			else if (valbool)
				sock_set_flag(sk, SOCK_DBG);
			else
				sock_reset_flag(sk, SOCK_DBG);
			break;
		case SO_REUSEADDR:
			sk->sk_reuse = valbool;
			break;
		case SO_TYPE:
		case SO_ERROR:
			ret = -ENOPROTOOPT;
		  	break;
		case SO_DONTROUTE:
			if (valbool)
				sock_set_flag(sk, SOCK_LOCALROUTE);
			else
				sock_reset_flag(sk, SOCK_LOCALROUTE);
			break;
		case SO_BROADCAST:
			sock_valbool_flag(sk, SOCK_BROADCAST, valbool);
			break;
		case SO_SNDBUF:
			/* Don't error on this BSD doesn't and if you think
			   about it this is right. Otherwise apps have to
			   play 'guess the biggest size' games. RCVBUF/SNDBUF
			   are treated in BSD as hints */
			   
			if (val > sysctl_wmem_max)
				val = sysctl_wmem_max;

			sk->sk_userlocks |= SOCK_SNDBUF_LOCK;
			if ((val * 2) < SOCK_MIN_SNDBUF)
				sk->sk_sndbuf = SOCK_MIN_SNDBUF;
			else
				sk->sk_sndbuf = val * 2;

			/*
			 *	Wake up sending tasks if we
			 *	upped the value.
			 */
			sk->sk_write_space(sk);
			break;

		case SO_RCVBUF:
			/* Don't error on this BSD doesn't and if you think
			   about it this is right. Otherwise apps have to
			   play 'guess the biggest size' games. RCVBUF/SNDBUF
			   are treated in BSD as hints */
			  
			if (val > sysctl_rmem_max)
				val = sysctl_rmem_max;

			sk->sk_userlocks |= SOCK_RCVBUF_LOCK;
			/* FIXME: is this lower bound the right one? */
			if ((val * 2) < SOCK_MIN_RCVBUF)
				sk->sk_rcvbuf = SOCK_MIN_RCVBUF;
			else
				sk->sk_rcvbuf = val * 2;
			break;

		case SO_KEEPALIVE:
#ifdef CONFIG_INET
			if (sk->sk_protocol == IPPROTO_TCP)
				tcp_set_keepalive(sk, valbool);
#endif
			sock_valbool_flag(sk, SOCK_KEEPOPEN, valbool);
			break;

	 	case SO_OOBINLINE:
			sock_valbool_flag(sk, SOCK_URGINLINE, valbool);
			break;

	 	case SO_NO_CHECK:
			sk->sk_no_check = valbool;
			break;

		case SO_PRIORITY:
			if ((val >= 0 && val <= 6) || capable(CAP_NET_ADMIN)) 
				sk->sk_priority = val;
			else
				ret = -EPERM;
			break;

		case SO_LINGER:
			if(optlen<sizeof(ling)) {
				ret = -EINVAL;	/* 1003.1g */
				break;
			}
			if (copy_from_user(&ling,optval,sizeof(ling))) {
				ret = -EFAULT;
				break;
			}
			if (!ling.l_onoff)
				sock_reset_flag(sk, SOCK_LINGER);
			else {
#if (BITS_PER_LONG == 32)
				if (ling.l_linger >= MAX_SCHEDULE_TIMEOUT/HZ)
					sk->sk_lingertime = MAX_SCHEDULE_TIMEOUT;
				else
#endif
					sk->sk_lingertime = ling.l_linger * HZ;
				sock_set_flag(sk, SOCK_LINGER);
			}
			break;

		case SO_BSDCOMPAT:
			sock_warn_obsolete_bsdism("setsockopt");
			break;

		case SO_PASSCRED:
			if (valbool)
				set_bit(SOCK_PASSCRED, &sock->flags);
			else
				clear_bit(SOCK_PASSCRED, &sock->flags);
			break;

		case SO_TIMESTAMP:
			if (valbool)  {
				sock_set_flag(sk, SOCK_RCVTSTAMP);
				sock_enable_timestamp(sk);
			} else
				sock_reset_flag(sk, SOCK_RCVTSTAMP);
			break;

		case SO_RCVLOWAT:
			if (val < 0)
				val = INT_MAX;
			sk->sk_rcvlowat = val ? : 1;
			break;

		case SO_RCVTIMEO:
			ret = sock_set_timeout(&sk->sk_rcvtimeo, optval, optlen);
			break;

		case SO_SNDTIMEO:
			ret = sock_set_timeout(&sk->sk_sndtimeo, optval, optlen);
			break;

#ifdef CONFIG_NETDEVICES
		case SO_BINDTODEVICE:
		{
			char devname[IFNAMSIZ]; 

			/* Sorry... */ 
			if (!capable(CAP_NET_RAW)) {
				ret = -EPERM;
				break;
			}

			/* Bind this socket to a particular device like "eth0",
			 * as specified in the passed interface name. If the
			 * name is "" or the option length is zero the socket 
			 * is not bound. 
			 */ 

			if (!valbool) {
				sk->sk_bound_dev_if = 0;
			} else {
				if (optlen > IFNAMSIZ) 
					optlen = IFNAMSIZ; 
				if (copy_from_user(devname, optval, optlen)) {
					ret = -EFAULT;
					break;
				}

				/* Remove any cached route for this socket. */
				sk_dst_reset(sk);

				if (devname[0] == '\0') {
					sk->sk_bound_dev_if = 0;
				} else {
					struct net_device *dev = dev_get_by_name(devname);
					if (!dev) {
						ret = -ENODEV;
						break;
					}
					sk->sk_bound_dev_if = dev->ifindex;
					dev_put(dev);
				}
			}
			break;
		}
#endif


		case SO_ATTACH_FILTER:
			ret = -EINVAL;
			if (optlen == sizeof(struct sock_fprog)) {
				struct sock_fprog fprog;

				ret = -EFAULT;
				if (copy_from_user(&fprog, optval, sizeof(fprog)))
					break;

				ret = sk_attach_filter(&fprog, sk);
			}
			break;

		case SO_DETACH_FILTER:
			spin_lock_bh(&sk->sk_lock.slock);
			filter = sk->sk_filter;
                        if (filter) {
				sk->sk_filter = NULL;
				spin_unlock_bh(&sk->sk_lock.slock);
				sk_filter_release(sk, filter);
				break;
			}
			spin_unlock_bh(&sk->sk_lock.slock);
			ret = -ENONET;
			break;

		/* We implement the SO_SNDLOWAT etc to
		   not be settable (1003.1g 5.3) */
		default:
		  	ret = -ENOPROTOOPT;
			break;
  	}
	release_sock(sk);
	return ret;
}


int sock_getsockopt(struct socket *sock, int level, int optname,
		    char __user *optval, int __user *optlen)
{
	struct sock *sk = sock->sk;
	
	union
	{
  		int val;
  		struct linger ling;
		struct timeval tm;
	} v;
	
	unsigned int lv = sizeof(int);
	int len;
  	
  	if(get_user(len,optlen))
  		return -EFAULT;
	if(len < 0)
		return -EINVAL;
		
  	switch(optname) 
  	{
		case SO_DEBUG:		
			v.val = sock_flag(sk, SOCK_DBG);
			break;
		
		case SO_DONTROUTE:
			v.val = sock_flag(sk, SOCK_LOCALROUTE);
			break;
		
		case SO_BROADCAST:
			v.val = !!sock_flag(sk, SOCK_BROADCAST);
			break;

		case SO_SNDBUF:
			v.val = sk->sk_sndbuf;
			break;
		
		case SO_RCVBUF:
			v.val = sk->sk_rcvbuf;
			break;

		case SO_REUSEADDR:
			v.val = sk->sk_reuse;
			break;

		case SO_KEEPALIVE:
			v.val = !!sock_flag(sk, SOCK_KEEPOPEN);
			break;

		case SO_TYPE:
			v.val = sk->sk_type;		  		
			break;

		case SO_ERROR:
			v.val = -sock_error(sk);
			if(v.val==0)
				v.val = xchg(&sk->sk_err_soft, 0);
			break;

		case SO_OOBINLINE:
			v.val = !!sock_flag(sk, SOCK_URGINLINE);
			break;
	
		case SO_NO_CHECK:
			v.val = sk->sk_no_check;
			break;

		case SO_PRIORITY:
			v.val = sk->sk_priority;
			break;
		
		case SO_LINGER:	
			lv		= sizeof(v.ling);
			v.ling.l_onoff	= !!sock_flag(sk, SOCK_LINGER);
 			v.ling.l_linger	= sk->sk_lingertime / HZ;
			break;
					
		case SO_BSDCOMPAT:
			sock_warn_obsolete_bsdism("getsockopt");
			break;

		case SO_TIMESTAMP:
			v.val = sock_flag(sk, SOCK_RCVTSTAMP);
			break;

		case SO_RCVTIMEO:
			lv=sizeof(struct timeval);
			if (sk->sk_rcvtimeo == MAX_SCHEDULE_TIMEOUT) {
				v.tm.tv_sec = 0;
				v.tm.tv_usec = 0;
			} else {
				v.tm.tv_sec = sk->sk_rcvtimeo / HZ;
				v.tm.tv_usec = ((sk->sk_rcvtimeo % HZ) * 1000000) / HZ;
			}
			break;

		case SO_SNDTIMEO:
			lv=sizeof(struct timeval);
			if (sk->sk_sndtimeo == MAX_SCHEDULE_TIMEOUT) {
				v.tm.tv_sec = 0;
				v.tm.tv_usec = 0;
			} else {
				v.tm.tv_sec = sk->sk_sndtimeo / HZ;
				v.tm.tv_usec = ((sk->sk_sndtimeo % HZ) * 1000000) / HZ;
			}
			break;

		case SO_RCVLOWAT:
			v.val = sk->sk_rcvlowat;
			break;

		case SO_SNDLOWAT:
			v.val=1;
			break; 

		case SO_PASSCRED:
			v.val = test_bit(SOCK_PASSCRED, &sock->flags) ? 1 : 0;
			break;

		case SO_PEERCRED:
			if (len > sizeof(sk->sk_peercred))
				len = sizeof(sk->sk_peercred);
			if (copy_to_user(optval, &sk->sk_peercred, len))
				return -EFAULT;
			goto lenout;

		case SO_PEERNAME:
		{
			char address[128];

			if (sock->ops->getname(sock, (struct sockaddr *)address, &lv, 2))
				return -ENOTCONN;
			if (lv < len)
				return -EINVAL;
			if (copy_to_user(optval, address, len))
				return -EFAULT;
			goto lenout;
		}

		/* Dubious BSD thing... Probably nobody even uses it, but
		 * the UNIX standard wants it for whatever reason... -DaveM
		 */
		case SO_ACCEPTCONN:
			v.val = sk->sk_state == TCP_LISTEN;
			break;

		case SO_PEERSEC:
			return security_socket_getpeersec(sock, optval, optlen, len);

		default:
			return(-ENOPROTOOPT);
	}
	if (len > lv)
		len = lv;
	if (copy_to_user(optval, &v, len))
		return -EFAULT;
lenout:
  	if (put_user(len, optlen))
  		return -EFAULT;
  	return 0;
}

/**
 *	sk_alloc - All socket objects are allocated here
 *	@family - protocol family
 *	@priority - for allocation (%GFP_KERNEL, %GFP_ATOMIC, etc)
 *	@prot - struct proto associated with this new sock instance
 *	@zero_it - if we should zero the newly allocated sock
 */
struct sock *sk_alloc(int family, int priority, struct proto *prot, int zero_it)
{
	struct sock *sk = NULL;
	kmem_cache_t *slab = prot->slab;

	if (slab != NULL)
		sk = kmem_cache_alloc(slab, priority);
	else
		sk = kmalloc(prot->obj_size, priority);

	if (sk) {
		if (zero_it) {
			memset(sk, 0, prot->obj_size);
			sk->sk_family = family;
			sk->sk_prot = prot;
			sock_lock_init(sk);
		}
		
		if (security_sk_alloc(sk, family, priority)) {
			kmem_cache_free(slab, sk);
			sk = NULL;
		} else
			__module_get(prot->owner);
	}
	return sk;
}

void sk_free(struct sock *sk)
{
	struct sk_filter *filter;
	struct module *owner = sk->sk_prot->owner;

	if (sk->sk_destruct)
		sk->sk_destruct(sk);

	filter = sk->sk_filter;
	if (filter) {
		sk_filter_release(sk, filter);
		sk->sk_filter = NULL;
	}

	sock_disable_timestamp(sk);

	if (atomic_read(&sk->sk_omem_alloc))
		printk(KERN_DEBUG "%s: optmem leakage (%d bytes) detected.\n",
		       __FUNCTION__, atomic_read(&sk->sk_omem_alloc));

	security_sk_free(sk);
	if (sk->sk_prot->slab != NULL)
		kmem_cache_free(sk->sk_prot->slab, sk);
	else
		kfree(sk);
	module_put(owner);
}

void __init sk_init(void)
{
	if (num_physpages <= 4096) {
		sysctl_wmem_max = 32767;
		sysctl_rmem_max = 32767;
		sysctl_wmem_default = 32767;
		sysctl_rmem_default = 32767;
	} else if (num_physpages >= 131072) {
		sysctl_wmem_max = 131071;
		sysctl_rmem_max = 131071;
	}
}

/*
 *	Simple resource managers for sockets.
 */


/* 
 * Write buffer destructor automatically called from kfree_skb. 
 */
void sock_wfree(struct sk_buff *skb)
{
	struct sock *sk = skb->sk;

	/* In case it might be waiting for more memory. */
	atomic_sub(skb->truesize, &sk->sk_wmem_alloc);
	if (!sock_flag(sk, SOCK_USE_WRITE_QUEUE))
		sk->sk_write_space(sk);
	sock_put(sk);
}

/* 
 * Read buffer destructor automatically called from kfree_skb. 
 */
void sock_rfree(struct sk_buff *skb)
{
	struct sock *sk = skb->sk;

	atomic_sub(skb->truesize, &sk->sk_rmem_alloc);
}


int sock_i_uid(struct sock *sk)
{
	int uid;

	read_lock(&sk->sk_callback_lock);
	uid = sk->sk_socket ? SOCK_INODE(sk->sk_socket)->i_uid : 0;
	read_unlock(&sk->sk_callback_lock);
	return uid;
}

unsigned long sock_i_ino(struct sock *sk)
{
	unsigned long ino;

	read_lock(&sk->sk_callback_lock);
	ino = sk->sk_socket ? SOCK_INODE(sk->sk_socket)->i_ino : 0;
	read_unlock(&sk->sk_callback_lock);
	return ino;
}

/*
 * Allocate a skb from the socket's send buffer.
 */
struct sk_buff *sock_wmalloc(struct sock *sk, unsigned long size, int force, int priority)
{
	if (force || atomic_read(&sk->sk_wmem_alloc) < sk->sk_sndbuf) {
		struct sk_buff * skb = alloc_skb(size, priority);
		if (skb) {
			skb_set_owner_w(skb, sk);
			return skb;
		}
	}
	return NULL;
}

/*
 * Allocate a skb from the socket's receive buffer.
 */ 
struct sk_buff *sock_rmalloc(struct sock *sk, unsigned long size, int force, int priority)
{
	if (force || atomic_read(&sk->sk_rmem_alloc) < sk->sk_rcvbuf) {
		struct sk_buff *skb = alloc_skb(size, priority);
		if (skb) {
			skb_set_owner_r(skb, sk);
			return skb;
		}
	}
	return NULL;
}

/* 
 * Allocate a memory block from the socket's option memory buffer.
 */ 
void *sock_kmalloc(struct sock *sk, int size, int priority)
{
	if ((unsigned)size <= sysctl_optmem_max &&
	    atomic_read(&sk->sk_omem_alloc) + size < sysctl_optmem_max) {
		void *mem;
		/* First do the add, to avoid the race if kmalloc
 		 * might sleep.
		 */
		atomic_add(size, &sk->sk_omem_alloc);
		mem = kmalloc(size, priority);
		if (mem)
			return mem;
		atomic_sub(size, &sk->sk_omem_alloc);
	}
	return NULL;
}

/*
 * Free an option memory block.
 */
void sock_kfree_s(struct sock *sk, void *mem, int size)
{
	kfree(mem);
	atomic_sub(size, &sk->sk_omem_alloc);
}

/* It is almost wait_for_tcp_memory minus release_sock/lock_sock.
   I think, these locks should be removed for datagram sockets.
 */
static long sock_wait_for_wmem(struct sock * sk, long timeo)
{
	DEFINE_WAIT(wait);

	clear_bit(SOCK_ASYNC_NOSPACE, &sk->sk_socket->flags);
	for (;;) {
		if (!timeo)
			break;
		if (signal_pending(current))
			break;
		set_bit(SOCK_NOSPACE, &sk->sk_socket->flags);
		prepare_to_wait(sk->sk_sleep, &wait, TASK_INTERRUPTIBLE);
		if (atomic_read(&sk->sk_wmem_alloc) < sk->sk_sndbuf)
			break;
		if (sk->sk_shutdown & SEND_SHUTDOWN)
			break;
		if (sk->sk_err)
			break;
		timeo = schedule_timeout(timeo);
	}
	finish_wait(sk->sk_sleep, &wait);
	return timeo;
}


/*
 *	Generic send/receive buffer handlers
 */

static struct sk_buff *sock_alloc_send_pskb(struct sock *sk,
					    unsigned long header_len,
					    unsigned long data_len,
					    int noblock, int *errcode)
{
	struct sk_buff *skb;
	unsigned int gfp_mask;
	long timeo;
	int err;

	gfp_mask = sk->sk_allocation;
	if (gfp_mask & __GFP_WAIT)
		gfp_mask |= __GFP_REPEAT;

	timeo = sock_sndtimeo(sk, noblock);
	while (1) {
		err = sock_error(sk);
		if (err != 0)
			goto failure;

		err = -EPIPE;
		if (sk->sk_shutdown & SEND_SHUTDOWN)
			goto failure;

		if (atomic_read(&sk->sk_wmem_alloc) < sk->sk_sndbuf) {
			skb = alloc_skb(header_len, sk->sk_allocation);
			if (skb) {
				int npages;
				int i;

				/* No pages, we're done... */
				if (!data_len)
					break;

				npages = (data_len + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
				skb->truesize += data_len;
				skb_shinfo(skb)->nr_frags = npages;
				for (i = 0; i < npages; i++) {
					struct page *page;
					skb_frag_t *frag;

					page = alloc_pages(sk->sk_allocation, 0);
					if (!page) {
						err = -ENOBUFS;
						skb_shinfo(skb)->nr_frags = i;
						kfree_skb(skb);
						goto failure;
					}

					frag = &skb_shinfo(skb)->frags[i];
					frag->page = page;
					frag->page_offset = 0;
					frag->size = (data_len >= PAGE_SIZE ?
						      PAGE_SIZE :
						      data_len);
					data_len -= PAGE_SIZE;
				}

				/* Full success... */
				break;
			}
			err = -ENOBUFS;
			goto failure;
		}
		set_bit(SOCK_ASYNC_NOSPACE, &sk->sk_socket->flags);
		set_bit(SOCK_NOSPACE, &sk->sk_socket->flags);
		err = -EAGAIN;
		if (!timeo)
			goto failure;
		if (signal_pending(current))
			goto interrupted;
		timeo = sock_wait_for_wmem(sk, timeo);
	}

	skb_set_owner_w(skb, sk);
	return skb;

interrupted:
	err = sock_intr_errno(timeo);
failure:
	*errcode = err;
	return NULL;
}

struct sk_buff *sock_alloc_send_skb(struct sock *sk, unsigned long size, 
				    int noblock, int *errcode)
{
	return sock_alloc_send_pskb(sk, size, 0, noblock, errcode);
}

static void __lock_sock(struct sock *sk)
{
	DEFINE_WAIT(wait);

	for(;;) {
		prepare_to_wait_exclusive(&sk->sk_lock.wq, &wait,
					TASK_UNINTERRUPTIBLE);
		spin_unlock_bh(&sk->sk_lock.slock);
		schedule();
		spin_lock_bh(&sk->sk_lock.slock);
		if(!sock_owned_by_user(sk))
			break;
	}
	finish_wait(&sk->sk_lock.wq, &wait);
}

static void __release_sock(struct sock *sk)
{
	struct sk_buff *skb = sk->sk_backlog.head;

	do {
		sk->sk_backlog.head = sk->sk_backlog.tail = NULL;
		bh_unlock_sock(sk);

		do {
			struct sk_buff *next = skb->next;

			skb->next = NULL;
			sk->sk_backlog_rcv(sk, skb);

			/*
			 * We are in process context here with softirqs
			 * disabled, use cond_resched_softirq() to preempt.
			 * This is safe to do because we've taken the backlog
			 * queue private:
			 */
			cond_resched_softirq();

			skb = next;
		} while (skb != NULL);

		bh_lock_sock(sk);
	} while((skb = sk->sk_backlog.head) != NULL);
}

/**
 * sk_wait_data - wait for data to arrive at sk_receive_queue
 * sk - sock to wait on
 * timeo - for how long
 *
 * Now socket state including sk->sk_err is changed only under lock,
 * hence we may omit checks after joining wait queue.
 * We check receive queue before schedule() only as optimization;
 * it is very likely that release_sock() added new data.
 */
int sk_wait_data(struct sock *sk, long *timeo)
{
	int rc;
	DEFINE_WAIT(wait);

	prepare_to_wait(sk->sk_sleep, &wait, TASK_INTERRUPTIBLE);
	set_bit(SOCK_ASYNC_WAITDATA, &sk->sk_socket->flags);
	rc = sk_wait_event(sk, timeo, !skb_queue_empty(&sk->sk_receive_queue));
	clear_bit(SOCK_ASYNC_WAITDATA, &sk->sk_socket->flags);
	finish_wait(sk->sk_sleep, &wait);
	return rc;
}

EXPORT_SYMBOL(sk_wait_data);

/*
 * Set of default routines for initialising struct proto_ops when
 * the protocol does not support a particular function. In certain
 * cases where it makes no sense for a protocol to have a "do nothing"
 * function, some default processing is provided.
 */

int sock_no_bind(struct socket *sock, struct sockaddr *saddr, int len)
{
	return -EOPNOTSUPP;
}

int sock_no_connect(struct socket *sock, struct sockaddr *saddr, 
		    int len, int flags)
{
	return -EOPNOTSUPP;
}

int sock_no_socketpair(struct socket *sock1, struct socket *sock2)
{
	return -EOPNOTSUPP;
}

int sock_no_accept(struct socket *sock, struct socket *newsock, int flags)
{
	return -EOPNOTSUPP;
}

int sock_no_getname(struct socket *sock, struct sockaddr *saddr, 
		    int *len, int peer)
{
	return -EOPNOTSUPP;
}

unsigned int sock_no_poll(struct file * file, struct socket *sock, poll_table *pt)
{
	return 0;
}

int sock_no_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	return -EOPNOTSUPP;
}

int sock_no_listen(struct socket *sock, int backlog)
{
	return -EOPNOTSUPP;
}

int sock_no_shutdown(struct socket *sock, int how)
{
	return -EOPNOTSUPP;
}

int sock_no_setsockopt(struct socket *sock, int level, int optname,
		    char __user *optval, int optlen)
{
	return -EOPNOTSUPP;
}

int sock_no_getsockopt(struct socket *sock, int level, int optname,
		    char __user *optval, int __user *optlen)
{
	return -EOPNOTSUPP;
}

int sock_no_sendmsg(struct kiocb *iocb, struct socket *sock, struct msghdr *m,
		    size_t len)
{
	return -EOPNOTSUPP;
}

int sock_no_recvmsg(struct kiocb *iocb, struct socket *sock, struct msghdr *m,
		    size_t len, int flags)
{
	return -EOPNOTSUPP;
}

int sock_no_mmap(struct file *file, struct socket *sock, struct vm_area_struct *vma)
{
	/* Mirror missing mmap method error code */
	return -ENODEV;
}

ssize_t sock_no_sendpage(struct socket *sock, struct page *page, int offset, size_t size, int flags)
{
	ssize_t res;
	struct msghdr msg = {.msg_flags = flags};
	struct kvec iov;
	char *kaddr = kmap(page);
	iov.iov_base = kaddr + offset;
	iov.iov_len = size;
	res = kernel_sendmsg(sock, &msg, &iov, 1, size);
	kunmap(page);
	return res;
}

/*
 *	Default Socket Callbacks
 */

static void sock_def_wakeup(struct sock *sk)
{
	read_lock(&sk->sk_callback_lock);
	if (sk->sk_sleep && waitqueue_active(sk->sk_sleep))
		wake_up_interruptible_all(sk->sk_sleep);
	read_unlock(&sk->sk_callback_lock);
}

static void sock_def_error_report(struct sock *sk)
{
	read_lock(&sk->sk_callback_lock);
	if (sk->sk_sleep && waitqueue_active(sk->sk_sleep))
		wake_up_interruptible(sk->sk_sleep);
	sk_wake_async(sk,0,POLL_ERR); 
	read_unlock(&sk->sk_callback_lock);
}

static void sock_def_readable(struct sock *sk, int len)
{
	read_lock(&sk->sk_callback_lock);
	if (sk->sk_sleep && waitqueue_active(sk->sk_sleep))
		wake_up_interruptible(sk->sk_sleep);
	sk_wake_async(sk,1,POLL_IN);
	read_unlock(&sk->sk_callback_lock);
}

static void sock_def_write_space(struct sock *sk)
{
	read_lock(&sk->sk_callback_lock);

	/* Do not wake up a writer until he can make "significant"
	 * progress.  --DaveM
	 */
	if((atomic_read(&sk->sk_wmem_alloc) << 1) <= sk->sk_sndbuf) {
		if (sk->sk_sleep && waitqueue_active(sk->sk_sleep))
			wake_up_interruptible(sk->sk_sleep);

		/* Should agree with poll, otherwise some programs break */
		if (sock_writeable(sk))
			sk_wake_async(sk, 2, POLL_OUT);
	}

	read_unlock(&sk->sk_callback_lock);
}

static void sock_def_destruct(struct sock *sk)
{
	if (sk->sk_protinfo)
		kfree(sk->sk_protinfo);
}

void sk_send_sigurg(struct sock *sk)
{
	if (sk->sk_socket && sk->sk_socket->file)
		if (send_sigurg(&sk->sk_socket->file->f_owner))
			sk_wake_async(sk, 3, POLL_PRI);
}

void sk_reset_timer(struct sock *sk, struct timer_list* timer,
		    unsigned long expires)
{
	if (!mod_timer(timer, expires))
		sock_hold(sk);
}

EXPORT_SYMBOL(sk_reset_timer);

void sk_stop_timer(struct sock *sk, struct timer_list* timer)
{
	if (timer_pending(timer) && del_timer(timer))
		__sock_put(sk);
}

EXPORT_SYMBOL(sk_stop_timer);

void sock_init_data(struct socket *sock, struct sock *sk)
{
	skb_queue_head_init(&sk->sk_receive_queue);
	skb_queue_head_init(&sk->sk_write_queue);
	skb_queue_head_init(&sk->sk_error_queue);

	sk->sk_send_head	=	NULL;

	init_timer(&sk->sk_timer);
	
	sk->sk_allocation	=	GFP_KERNEL;
	sk->sk_rcvbuf		=	sysctl_rmem_default;
	sk->sk_sndbuf		=	sysctl_wmem_default;
	sk->sk_state		=	TCP_CLOSE;
	sk->sk_socket		=	sock;

	sock_set_flag(sk, SOCK_ZAPPED);

	if(sock)
	{
		sk->sk_type	=	sock->type;
		sk->sk_sleep	=	&sock->wait;
		sock->sk	=	sk;
	} else
		sk->sk_sleep	=	NULL;

	rwlock_init(&sk->sk_dst_lock);
	rwlock_init(&sk->sk_callback_lock);

	sk->sk_state_change	=	sock_def_wakeup;
	sk->sk_data_ready	=	sock_def_readable;
	sk->sk_write_space	=	sock_def_write_space;
	sk->sk_error_report	=	sock_def_error_report;
	sk->sk_destruct		=	sock_def_destruct;

	sk->sk_sndmsg_page	=	NULL;
	sk->sk_sndmsg_off	=	0;

	sk->sk_peercred.pid 	=	0;
	sk->sk_peercred.uid	=	-1;
	sk->sk_peercred.gid	=	-1;
	sk->sk_write_pending	=	0;
	sk->sk_rcvlowat		=	1;
	sk->sk_rcvtimeo		=	MAX_SCHEDULE_TIMEOUT;
	sk->sk_sndtimeo		=	MAX_SCHEDULE_TIMEOUT;

	sk->sk_stamp.tv_sec     = -1L;
	sk->sk_stamp.tv_usec    = -1L;

	atomic_set(&sk->sk_refcnt, 1);
}

void fastcall lock_sock(struct sock *sk)
{
	might_sleep();
	spin_lock_bh(&(sk->sk_lock.slock));
	if (sk->sk_lock.owner)
		__lock_sock(sk);
	sk->sk_lock.owner = (void *)1;
	spin_unlock_bh(&(sk->sk_lock.slock));
}

EXPORT_SYMBOL(lock_sock);

void fastcall release_sock(struct sock *sk)
{
	spin_lock_bh(&(sk->sk_lock.slock));
	if (sk->sk_backlog.tail)
		__release_sock(sk);
	sk->sk_lock.owner = NULL;
        if (waitqueue_active(&(sk->sk_lock.wq)))
		wake_up(&(sk->sk_lock.wq));
	spin_unlock_bh(&(sk->sk_lock.slock));
}
EXPORT_SYMBOL(release_sock);

int sock_get_timestamp(struct sock *sk, struct timeval __user *userstamp)
{ 
	if (!sock_flag(sk, SOCK_TIMESTAMP))
		sock_enable_timestamp(sk);
	if (sk->sk_stamp.tv_sec == -1) 
		return -ENOENT;
	if (sk->sk_stamp.tv_sec == 0)
		do_gettimeofday(&sk->sk_stamp);
	return copy_to_user(userstamp, &sk->sk_stamp, sizeof(struct timeval)) ?
		-EFAULT : 0; 
} 
EXPORT_SYMBOL(sock_get_timestamp);

void sock_enable_timestamp(struct sock *sk)
{	
	if (!sock_flag(sk, SOCK_TIMESTAMP)) { 
		sock_set_flag(sk, SOCK_TIMESTAMP);
		net_enable_timestamp();
	}
}
EXPORT_SYMBOL(sock_enable_timestamp); 

/*
 *	Get a socket option on an socket.
 *
 *	FIX: POSIX 1003.1g is very ambiguous here. It states that
 *	asynchronous errors should be reported by getsockopt. We assume
 *	this means if you specify SO_ERROR (otherwise whats the point of it).
 */
int sock_common_getsockopt(struct socket *sock, int level, int optname,
			   char __user *optval, int __user *optlen)
{
	struct sock *sk = sock->sk;

	return sk->sk_prot->getsockopt(sk, level, optname, optval, optlen);
}

EXPORT_SYMBOL(sock_common_getsockopt);

int sock_common_recvmsg(struct kiocb *iocb, struct socket *sock,
			struct msghdr *msg, size_t size, int flags)
{
	struct sock *sk = sock->sk;
	int addr_len = 0;
	int err;

	err = sk->sk_prot->recvmsg(iocb, sk, msg, size, flags & MSG_DONTWAIT,
				   flags & ~MSG_DONTWAIT, &addr_len);
	if (err >= 0)
		msg->msg_namelen = addr_len;
	return err;
}

EXPORT_SYMBOL(sock_common_recvmsg);

/*
 *	Set socket options on an inet socket.
 */
int sock_common_setsockopt(struct socket *sock, int level, int optname,
			   char __user *optval, int optlen)
{
	struct sock *sk = sock->sk;

	return sk->sk_prot->setsockopt(sk, level, optname, optval, optlen);
}

EXPORT_SYMBOL(sock_common_setsockopt);

void sk_common_release(struct sock *sk)
{
	if (sk->sk_prot->destroy)
		sk->sk_prot->destroy(sk);

	/*
	 * Observation: when sock_common_release is called, processes have
	 * no access to socket. But net still has.
	 * Step one, detach it from networking:
	 *
	 * A. Remove from hash tables.
	 */

	sk->sk_prot->unhash(sk);

	/*
	 * In this point socket cannot receive new packets, but it is possible
	 * that some packets are in flight because some CPU runs receiver and
	 * did hash table lookup before we unhashed socket. They will achieve
	 * receive queue and will be purged by socket destructor.
	 *
	 * Also we still have packets pending on receive queue and probably,
	 * our own packets waiting in device queues. sock_destroy will drain
	 * receive queue, but transmitted packets will delay socket destruction
	 * until the last reference will be released.
	 */

	sock_orphan(sk);

	xfrm_sk_free_policy(sk);

#ifdef INET_REFCNT_DEBUG
	if (atomic_read(&sk->sk_refcnt) != 1)
		printk(KERN_DEBUG "Destruction of the socket %p delayed, c=%d\n",
		       sk, atomic_read(&sk->sk_refcnt));
#endif
	sock_put(sk);
}

EXPORT_SYMBOL(sk_common_release);

static DEFINE_RWLOCK(proto_list_lock);
static LIST_HEAD(proto_list);

int proto_register(struct proto *prot, int alloc_slab)
{
	int rc = -ENOBUFS;

	write_lock(&proto_list_lock);

	if (alloc_slab) {
		prot->slab = kmem_cache_create(prot->name, prot->obj_size, 0,
					       SLAB_HWCACHE_ALIGN, NULL, NULL);

		if (prot->slab == NULL) {
			printk(KERN_CRIT "%s: Can't create sock SLAB cache!\n",
			       prot->name);
			goto out_unlock;
		}
	}

	list_add(&prot->node, &proto_list);
	rc = 0;
out_unlock:
	write_unlock(&proto_list_lock);
	return rc;
}

EXPORT_SYMBOL(proto_register);

void proto_unregister(struct proto *prot)
{
	write_lock(&proto_list_lock);

	if (prot->slab != NULL) {
		kmem_cache_destroy(prot->slab);
		prot->slab = NULL;
	}

	list_del(&prot->node);
	write_unlock(&proto_list_lock);
}

EXPORT_SYMBOL(proto_unregister);

#ifdef CONFIG_PROC_FS
static inline struct proto *__proto_head(void)
{
	return list_entry(proto_list.next, struct proto, node);
}

static inline struct proto *proto_head(void)
{
	return list_empty(&proto_list) ? NULL : __proto_head();
}

static inline struct proto *proto_next(struct proto *proto)
{
	return proto->node.next == &proto_list ? NULL :
		list_entry(proto->node.next, struct proto, node);
}

static inline struct proto *proto_get_idx(loff_t pos)
{
	struct proto *proto;
	loff_t i = 0;

	list_for_each_entry(proto, &proto_list, node)
		if (i++ == pos)
			goto out;

	proto = NULL;
out:
	return proto;
}

static void *proto_seq_start(struct seq_file *seq, loff_t *pos)
{
	read_lock(&proto_list_lock);
	return *pos ? proto_get_idx(*pos - 1) : SEQ_START_TOKEN;
}

static void *proto_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	++*pos;
	return v == SEQ_START_TOKEN ? proto_head() : proto_next(v);
}

static void proto_seq_stop(struct seq_file *seq, void *v)
{
	read_unlock(&proto_list_lock);
}

static char proto_method_implemented(const void *method)
{
	return method == NULL ? 'n' : 'y';
}

static void proto_seq_printf(struct seq_file *seq, struct proto *proto)
{
	seq_printf(seq, "%-9s %4u %6d  %6d   %-3s %6u   %-3s  %-10s "
			"%2c %2c %2c %2c %2c %2c %2c %2c %2c %2c %2c %2c %2c %2c %2c %2c %2c %2c %2c\n",
		   proto->name,
		   proto->obj_size,
		   proto->sockets_allocated != NULL ? atomic_read(proto->sockets_allocated) : -1,
		   proto->memory_allocated != NULL ? atomic_read(proto->memory_allocated) : -1,
		   proto->memory_pressure != NULL ? *proto->memory_pressure ? "yes" : "no" : "NI",
		   proto->max_header,
		   proto->slab == NULL ? "no" : "yes",
		   module_name(proto->owner),
		   proto_method_implemented(proto->close),
		   proto_method_implemented(proto->connect),
		   proto_method_implemented(proto->disconnect),
		   proto_method_implemented(proto->accept),
		   proto_method_implemented(proto->ioctl),
		   proto_method_implemented(proto->init),
		   proto_method_implemented(proto->destroy),
		   proto_method_implemented(proto->shutdown),
		   proto_method_implemented(proto->setsockopt),
		   proto_method_implemented(proto->getsockopt),
		   proto_method_implemented(proto->sendmsg),
		   proto_method_implemented(proto->recvmsg),
		   proto_method_implemented(proto->sendpage),
		   proto_method_implemented(proto->bind),
		   proto_method_implemented(proto->backlog_rcv),
		   proto_method_implemented(proto->hash),
		   proto_method_implemented(proto->unhash),
		   proto_method_implemented(proto->get_port),
		   proto_method_implemented(proto->enter_memory_pressure));
}

static int proto_seq_show(struct seq_file *seq, void *v)
{
	if (v == SEQ_START_TOKEN)
		seq_printf(seq, "%-9s %-4s %-8s %-6s %-5s %-7s %-4s %-10s %s",
			   "protocol",
			   "size",
			   "sockets",
			   "memory",
			   "press",
			   "maxhdr",
			   "slab",
			   "module",
			   "cl co di ac io in de sh ss gs se re sp bi br ha uh gp em\n");
	else
		proto_seq_printf(seq, v);
	return 0;
}

static struct seq_operations proto_seq_ops = {
	.start  = proto_seq_start,
	.next   = proto_seq_next,
	.stop   = proto_seq_stop,
	.show   = proto_seq_show,
};

static int proto_seq_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &proto_seq_ops);
}

static struct file_operations proto_seq_fops = {
	.owner		= THIS_MODULE,
	.open		= proto_seq_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int __init proto_init(void)
{
	/* register /proc/net/protocols */
	return proc_net_fops_create("protocols", S_IRUGO, &proto_seq_fops) == NULL ? -ENOBUFS : 0;
}

subsys_initcall(proto_init);

#endif /* PROC_FS */

EXPORT_SYMBOL(sk_alloc);
EXPORT_SYMBOL(sk_free);
EXPORT_SYMBOL(sk_send_sigurg);
EXPORT_SYMBOL(sock_alloc_send_skb);
EXPORT_SYMBOL(sock_init_data);
EXPORT_SYMBOL(sock_kfree_s);
EXPORT_SYMBOL(sock_kmalloc);
EXPORT_SYMBOL(sock_no_accept);
EXPORT_SYMBOL(sock_no_bind);
EXPORT_SYMBOL(sock_no_connect);
EXPORT_SYMBOL(sock_no_getname);
EXPORT_SYMBOL(sock_no_getsockopt);
EXPORT_SYMBOL(sock_no_ioctl);
EXPORT_SYMBOL(sock_no_listen);
EXPORT_SYMBOL(sock_no_mmap);
EXPORT_SYMBOL(sock_no_poll);
EXPORT_SYMBOL(sock_no_recvmsg);
EXPORT_SYMBOL(sock_no_sendmsg);
EXPORT_SYMBOL(sock_no_sendpage);
EXPORT_SYMBOL(sock_no_setsockopt);
EXPORT_SYMBOL(sock_no_shutdown);
EXPORT_SYMBOL(sock_no_socketpair);
EXPORT_SYMBOL(sock_rfree);
EXPORT_SYMBOL(sock_setsockopt);
EXPORT_SYMBOL(sock_wfree);
EXPORT_SYMBOL(sock_wmalloc);
EXPORT_SYMBOL(sock_i_uid);
EXPORT_SYMBOL(sock_i_ino);
#ifdef CONFIG_SYSCTL
EXPORT_SYMBOL(sysctl_optmem_max);
EXPORT_SYMBOL(sysctl_rmem_max);
EXPORT_SYMBOL(sysctl_wmem_max);
#endif
