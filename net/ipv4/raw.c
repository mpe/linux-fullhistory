/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		RAW - implementation of IP "raw" sockets.
 *
 * Version:	@(#)raw.c	1.0.4	05/25/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 * Fixes:
 *		Alan Cox	:	verify_area() fixed up
 *		Alan Cox	:	ICMP error handling
 *		Alan Cox	:	EMSGSIZE if you send too big a packet
 *		Alan Cox	: 	Now uses generic datagrams and shared skbuff
 *					library. No more peek crashes, no more backlogs
 *		Alan Cox	:	Checks sk->broadcast.
 *		Alan Cox	:	Uses skb_free_datagram/skb_copy_datagram
 *		Alan Cox	:	Raw passes ip options too
 *		Alan Cox	:	Setsocketopt added
 *		Alan Cox	:	Fixed error return for broadcasts
 *		Alan Cox	:	Removed wake_up calls
 *		Alan Cox	:	Use ttl/tos
 *		Alan Cox	:	Cleaned up old debugging
 *		Alan Cox	:	Use new kernel side addresses
 *	Arnt Gulbrandsen	:	Fixed MSG_DONTROUTE in raw sockets.
 *		Alan Cox	:	BSD style RAW socket demultiplexing.
 *		Alan Cox	:	Beginnings of mrouted support.
 *		Alan Cox	:	Added IP_HDRINCL option.
 *		Alan Cox	:	Skip broadcast check if BSDism set.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
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
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/mroute.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/icmp.h>
#include <net/udp.h>
#include <net/checksum.h>

#ifdef CONFIG_IP_MROUTE
struct sock *mroute_socket=NULL;
#endif


/*
 *	Raw_err does not currently get called by the icmp module - FIXME:
 */
 
void raw_err (int type, int code, unsigned char *header, __u32 daddr,
	 __u32 saddr, struct inet_protocol *protocol)
{
	struct sock *sk;
   
	if (protocol == NULL) 
		return;
	sk = (struct sock *) protocol->data;
	if (sk == NULL) 
		return;

	/* This is meaningless in raw sockets. */
	if (type == ICMP_SOURCE_QUENCH) 
	{
		if (sk->cong_window > 1) sk->cong_window = sk->cong_window/2;
		return;
	}
	
	if(type == ICMP_PARAMETERPROB)
	{
		sk->err = EPROTO;
		sk->error_report(sk);
	}

	if(code<=NR_ICMP_UNREACH)
	{
		sk->err = icmp_err_convert[code & 0xff].errno;
		sk->error_report(sk);
	}
	
	return;
}

static inline int raw_rcv_skb(struct sock * sk, struct sk_buff * skb)
{
	/* Charge it to the socket. */
	
	if (__sock_queue_rcv_skb(sk,skb)<0)
	{
		ip_statistics.IpInDiscards++;
		skb->sk=NULL;
		kfree_skb(skb, FREE_READ);
		return 0;
	}

	ip_statistics.IpInDelivers++;
	return 0;
}

/*
 *	This should be the easiest of all, all we do is
 *	copy it into a buffer. All demultiplexing is done
 *	in ip.c
 */

int raw_rcv(struct sock *sk, struct sk_buff *skb, struct device *dev, __u32 saddr, __u32 daddr)
{
	/* Now we need to copy this into memory. */
	skb->sk = sk;
	skb_trim(skb,ntohs(skb->ip_hdr->tot_len));
	
	skb->h.raw = (unsigned char *) skb->ip_hdr;
	skb->dev = dev;
	skb->saddr = daddr;
	skb->daddr = saddr;

#if 0	
	/*
	 *	For no adequately explained reasons BSD likes to mess up the header of
	 *	the received frame. 
	 */
	 
	if(sk->bsdism)
		skb->ip_hdr->tot_len=ntohs(skb->ip_hdr->tot_len-4*skb->ip_hdr->ihl);
#endif
	
	if (sk->users) {
		__skb_queue_tail(&sk->back_log, skb);
		return 0;
	}
	raw_rcv_skb(sk, skb);
	return 0;
}

/*
 *	Send a RAW IP packet.
 */

/*
 *	Callback support is trivial for SOCK_RAW
 */
  
static int raw_getfrag(const void *p, __u32 saddr, char *to,
		       unsigned int offset, unsigned int fraglen)
{
	return copy_from_user(to, (const unsigned char *)p+offset, fraglen);
}

/*
 *	IPPROTO_RAW needs extra work.
 */
 
static int raw_getrawfrag(const void *p, __u32 saddr, char *to, unsigned int offset, unsigned int fraglen)
{
	int err; 
	err = copy_from_user(to, (const unsigned char *)p+offset, fraglen);
	if (err)
		return err; 
	if(offset==0)
	{
		struct iphdr *iph=(struct iphdr *)to;
		if(!iph->saddr)
			iph->saddr=saddr;
		iph->check=0;
		iph->tot_len=htons(fraglen);	/* This is right as you can't frag
					   RAW packets */
		/*
	 	 *	Deliberate breach of modularity to keep 
	 	 *	ip_build_xmit clean (well less messy).
		 */
		if (!iph->id)
			iph->id = htons(ip_id_count++);
		iph->check=ip_fast_csum((unsigned char *)iph, iph->ihl);
	}
	return 0; 
}

static int raw_sendto(struct sock *sk, const unsigned char *from, 
	int len, int noblock, unsigned flags, struct sockaddr_in *usin, int addr_len)
{
	int err;
	struct sockaddr_in sin;

	/*
	 *	Check the flags. Only MSG_DONTROUTE is permitted.
	 */

	if (flags & MSG_OOB)		/* Mirror BSD error message compatibility */
		return -EOPNOTSUPP;
			 
	if (flags & ~MSG_DONTROUTE)
		return(-EINVAL);
	/*
	 *	Get and verify the address. 
	 */

	if (usin) 
	{
		if (addr_len < sizeof(sin)) 
			return(-EINVAL);
		memcpy(&sin, usin, sizeof(sin));
		if (sin.sin_family && sin.sin_family != AF_INET) 
			return(-EINVAL);
		/*
		 *	Protocol type is host ordered byte.
		 */
		sin.sin_port=ntohs(sin.sin_port);
	}
	else 
	{
		if (sk->state != TCP_ESTABLISHED) 
			return(-EINVAL);
		sin.sin_family = AF_INET;
		sin.sin_port = sk->num;
		sin.sin_addr.s_addr = sk->daddr;
	}
	if (sin.sin_port == 0) 
		sin.sin_port = sk->num;
  
	if (sin.sin_addr.s_addr == INADDR_ANY)
		sin.sin_addr.s_addr = ip_my_addr();

	/*
	 *	BSD raw sockets forget to check SO_BROADCAST ....
	 */
	 
	if (!sk->bsdism && sk->broadcast == 0 && ip_chk_addr(sin.sin_addr.s_addr)==IS_BROADCAST)
		return -EACCES;

	if(sk->ip_hdrincl)
	{
		if(len>65535)
			return -EMSGSIZE;
		err=ip_build_xmit(sk, raw_getrawfrag, from, len, sin.sin_addr.s_addr, 0, sk->opt, flags, sin.sin_port, noblock);
	}
	else
	{
		if(len>65535-sizeof(struct iphdr))
			return -EMSGSIZE;
		err=ip_build_xmit(sk, raw_getfrag, from, len, sin.sin_addr.s_addr, 0, sk->opt, flags, sin.sin_port, noblock);
	}
	return err<0?err:len;
}

/*
 *	Temporary
 */
 
static int raw_sendmsg(struct sock *sk, struct msghdr *msg, int len, int noblock, 
	int flags)
{
	if(msg->msg_iovlen==1)
		return raw_sendto(sk,msg->msg_iov[0].iov_base,len, noblock, flags, msg->msg_name, msg->msg_namelen);
	else
	{
		/*
		 *	For awkward cases we linearise the buffer first. In theory this is only frames
		 *	whose iovec's don't split on 4 byte boundaries, and soon encrypted stuff (to keep
		 *	skip happy). We are a bit more general about it.
		 */
		 
		unsigned char *buf;
		int fs;
		int err;
		if(len>65515)
			return -EMSGSIZE;
		buf=kmalloc(len, GFP_KERNEL);
		if(buf==NULL)
			return -ENOBUFS;
		err = memcpy_fromiovec(buf, msg->msg_iov, len);
		if (!err)
		{
			fs=get_fs();
			set_fs(get_ds());
			err=raw_sendto(sk,buf,len, noblock, flags, msg->msg_name, msg->msg_namelen);
			set_fs(fs);
		}
		else
			err = -EFAULT;
		
		kfree_s(buf,len);
		return err;
	}
}

static void raw_close(struct sock *sk, unsigned long timeout)
{
	sk->state = TCP_CLOSE;
#ifdef CONFIG_IP_MROUTE	
	if(sk==mroute_socket)
	{
		mroute_close(sk);
		mroute_socket=NULL;
	}
#endif	
	destroy_sock(sk);
}


static int raw_init(struct sock *sk)
{
	return(0);
}


/*
 *	This should be easy, if there is something there
 *	we return it, otherwise we block.
 */

int raw_recvmsg(struct sock *sk, struct msghdr *msg, int len,
     int noblock, int flags,int *addr_len)
{
	int copied=0;
	struct sk_buff *skb;
	int err;
	struct sockaddr_in *sin=(struct sockaddr_in *)msg->msg_name;

	if (flags & MSG_OOB)
		return -EOPNOTSUPP;
		
	if (sk->shutdown & RCV_SHUTDOWN) 
		return(0);

	if (addr_len) 
		*addr_len=sizeof(*sin);

	skb=skb_recv_datagram(sk,flags,noblock,&err);
	if(skb==NULL)
 		return err;

	copied=skb->len;
	if(copied>len)
	{
		copied=len;
		msg->msg_flags|=MSG_TRUNC;
	}
	
	err = skb_copy_datagram_iovec(skb, 0, msg->msg_iov, copied);
	sk->stamp=skb->stamp;

	/* Copy the address. */
	if (sin) 
	{
		sin->sin_family = AF_INET;
		sin->sin_addr.s_addr = skb->daddr;
	}
	skb_free_datagram(sk, skb);
	return err ? err : (copied);
}


struct proto raw_prot = {
	raw_close,
	udp_connect,
	NULL,
	NULL,
	NULL,
	NULL,
	datagram_select,
#ifdef CONFIG_IP_MROUTE	
	ipmr_ioctl,
#else
	NULL,
#endif		
	raw_init,
	NULL,
	NULL,
	ip_setsockopt,
	ip_getsockopt,
	raw_sendmsg,
	raw_recvmsg,
	NULL,		/* No special bind */
	raw_rcv_skb,
	128,
	0,
	"RAW",
	0, 0,
	NULL
};
