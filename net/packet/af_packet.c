/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		PACKET - implements raw packet sockets.
 *
 * Version:	$Id: af_packet.c,v 1.18 1998/10/03 15:55:24 freitag Exp $
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Alan Cox, <gw4pts@gw4pts.ampr.org>
 *
 * Fixes:	
 *		Alan Cox	:	verify_area() now used correctly
 *		Alan Cox	:	new skbuff lists, look ma no backlogs!
 *		Alan Cox	:	tidied skbuff lists.
 *		Alan Cox	:	Now uses generic datagram routines I
 *					added. Also fixed the peek/read crash
 *					from all old Linux datagram code.
 *		Alan Cox	:	Uses the improved datagram code.
 *		Alan Cox	:	Added NULL's for socket options.
 *		Alan Cox	:	Re-commented the code.
 *		Alan Cox	:	Use new kernel side addressing
 *		Rob Janssen	:	Correct MTU usage.
 *		Dave Platt	:	Counter leaks caused by incorrect
 *					interrupt locking and some slightly
 *					dubious gcc output. Can you read
 *					compiler: it said _VOLATILE_
 *	Richard Kooijman	:	Timestamp fixes.
 *		Alan Cox	:	New buffers. Use sk->mac.raw.
 *		Alan Cox	:	sendmsg/recvmsg support.
 *		Alan Cox	:	Protocol setting support
 *	Alexey Kuznetsov	:	Untied from IPv4 stack.
 *	Cyrus Durgin		:	Fixed kerneld for kmod.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 */
 
#include <linux/config.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/fcntl.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/if_packet.h>
#include <linux/wireless.h>
#include <linux/kmod.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <linux/module.h>
#include <linux/init.h>

#ifdef CONFIG_INET
#include <net/inet_common.h>
#endif

#ifdef CONFIG_BRIDGE
#include <net/br.h>
#endif

#ifdef CONFIG_DLCI
extern int dlci_ioctl(unsigned int, void*);
#endif

/*
   Old SOCK_PACKET. Do exist programs, which use it?
   (not counting tcpdump) - lots of them yes - AC. 
   
 */
#define CONFIG_SOCK_PACKET	1

/*
   Proposed replacement for SIOC{ADD,DEL}MULTI and
   IFF_PROMISC, IFF_ALLMULTI flags.

   It is more expensive, but I believe,
   it is really correct solution: reentereble, safe and fault tolerant.

   Differences:
   - Changing IFF_ALLMULTI from user level is disabled.
     It could only confused multicast routing daemons, not more.
   - IFF_PROMISC is faked by keeping reference count and
     global flag, so that real IFF_PROMISC == (gflag|(count != 0))
     I'd remove it too, but it would require recompilation tcpdump
     and another applications, using promiscuous mode.
   - SIOC{ADD/DEL}MULTI are moved to deprecated state,
     they work, but complain. I do know who uses them.
     
 
*************FIXME***************
  Alexey : This doesnt cook Im afraid. We need the low level SIOCADD/DELMULTI
  and also IFF_ALLMULTI for DECNET, Appletalk and other stuff as well as
  BSD compatibility issues.
  
 */
#define CONFIG_PACKET_MULTICAST	1

/*
   Assumptions:
   - if device has no dev->hard_header routine, it adds and removes ll header
     inside itself. In this case ll header is invisible outside of device,
     but higher levels still should reserve dev->hard_header_len.
     Some devices are enough clever to reallocate skb, when header
     will not fit to reserved space (tunnel), another ones are silly
     (PPP).
   - packet socket receives packets with pulled ll header,
     so that SOCK_RAW should push it back.

On receive:
-----------

Incoming, dev->hard_header!=NULL
   mac.raw -> ll header
   data    -> data

Outgoing, dev->hard_header!=NULL
   mac.raw -> ll header
   data    -> ll header

Incoming, dev->hard_header==NULL
   mac.raw -> UNKNOWN position. It is very likely, that it points to ll header.
              PPP makes it, that is wrong, because introduce assymetry
	      between rx and tx paths.
   data    -> data

Outgoing, dev->hard_header==NULL
   mac.raw -> data. ll header is still not built!
   data    -> data

Resume
  If dev->hard_header==NULL we are unlikely to restore sensible ll header.


On transmit:
------------

dev->hard_header != NULL
   mac.raw -> ll header
   data    -> ll header

dev->hard_header == NULL (ll header is added by device, we cannot control it)
   mac.raw -> data
   data -> data

   We should set nh.raw on output to correct posistion,
   packet classifier depends on it.
 */

/* List of all packet sockets. */
struct sock * packet_sklist = NULL;

/* Private packet socket structures. */

#ifdef CONFIG_PACKET_MULTICAST
struct packet_mclist
{
	struct packet_mclist	*next;
	int			ifindex;
	int			count;
	unsigned short		type;
	unsigned short		alen;
	unsigned char		addr[8];
};
#endif

static void packet_flush_mclist(struct sock *sk);

struct packet_opt
{
	struct packet_type	prot_hook;
	char			running;	/* prot_hook is attached*/
	int			ifindex;	/* bound device		*/
#ifdef CONFIG_PACKET_MULTICAST
	struct packet_mclist	*mclist;
#endif
};

extern struct proto_ops packet_ops;

#ifdef CONFIG_SOCK_PACKET
extern struct proto_ops packet_ops_spkt;

static int packet_rcv_spkt(struct sk_buff *skb, struct device *dev,  struct packet_type *pt)
{
	struct sock *sk;
	struct sockaddr_pkt *spkt = (struct sockaddr_pkt*)skb->cb;

	/*
	 *	When we registered the protocol we saved the socket in the data
	 *	field for just this event.
	 */

	sk = (struct sock *) pt->data;
	
	/*
	 *	Yank back the headers [hope the device set this
	 *	right or kerboom...]
	 *
	 *	Incoming packets have ll header pulled,
	 *	push it back.
	 *
	 *	For outgoing ones skb->data == skb->mac.raw
	 *	so that this procedure is noop.
	 */

	if (skb->pkt_type == PACKET_LOOPBACK) {
		kfree_skb(skb);
		return 0;
	}

	skb_push(skb, skb->data-skb->mac.raw);

	/*
	 *	The SOCK_PACKET socket receives _all_ frames.
	 */

	spkt->spkt_family = dev->type;
	strncpy(spkt->spkt_device, dev->name, 15);
	spkt->spkt_protocol = skb->protocol;

	/*
	 *	Charge the memory to the socket. This is done specifically
	 *	to prevent sockets using all the memory up.
	 */

	if (sock_queue_rcv_skb(sk,skb)<0)
	{
		kfree_skb(skb);
		return 0;
	}

	/*
	 *	Processing complete.
	 */
	return(0);
}


/*
 *	Output a raw packet to a device layer. This bypasses all the other
 *	protocol layers and you must therefore supply it with a complete frame
 */
 
static int packet_sendmsg_spkt(struct socket *sock, struct msghdr *msg, int len,
			       struct scm_cookie *scm)
{
	struct sock *sk = sock->sk;
	struct sockaddr_pkt *saddr=(struct sockaddr_pkt *)msg->msg_name;
	struct sk_buff *skb;
	struct device *dev;
	unsigned short proto=0;
	int err;
	
	/*
	 *	Check the flags. 
	 */

	if (msg->msg_flags&~MSG_DONTWAIT)
		return(-EINVAL);

	/*
	 *	Get and verify the address. 
	 */

	if (saddr)
	{
		if (msg->msg_namelen < sizeof(struct sockaddr))
			return(-EINVAL);
		if (msg->msg_namelen==sizeof(struct sockaddr_pkt))
			proto=saddr->spkt_protocol;
	}
	else
		return(-ENOTCONN);	/* SOCK_PACKET must be sent giving an address */

	/*
	 *	Find the device first to size check it 
	 */

	saddr->spkt_device[13] = 0;
	dev = dev_get(saddr->spkt_device);
	if (dev == NULL) 
	{
		return(-ENODEV);
  	}
	
	/*
	 *	You may not queue a frame bigger than the mtu. This is the lowest level
	 *	raw protocol and you must do your own fragmentation at this level.
	 */
	 
	if(len>dev->mtu+dev->hard_header_len)
  		return -EMSGSIZE;

	dev_lock_list();
	err = -ENOBUFS;
	skb = sock_wmalloc(sk, len+dev->hard_header_len+15, 0, GFP_KERNEL);

	/*
	 *	If the write buffer is full, then tough. At this level the user gets to
	 *	deal with the problem - do your own algorithmic backoffs. That's far
	 *	more flexible.
	 */
	 
	if (skb == NULL) 
		goto out_unlock;
	
	/*
	 *	Fill it in 
	 */
	 
	/* FIXME: Save some space for broken drivers that write a
	 * hard header at transmission time by themselves. PPP is the
	 * notable one here. This should really be fixed at the driver level.
	 */
	skb_reserve(skb,(dev->hard_header_len+15)&~15);
	skb->nh.raw = skb->data;

	/* Try to align data part correctly */
	if (dev->hard_header) {
		skb->data -= dev->hard_header_len;
		skb->tail -= dev->hard_header_len;
	}

	/* Returns -EFAULT on error */
	err = memcpy_fromiovec(skb_put(skb,len), msg->msg_iov, len);
	skb->protocol = proto;
	skb->dev = dev;
	skb->priority = sk->priority;
	if (err)
		goto out_free;

	err = -ENETDOWN;
	if (!(dev->flags & IFF_UP))
		goto out_free;

	/*
	 *	Now send it
	 */

	dev_unlock_list();
	dev_queue_xmit(skb);
	return(len);

out_free:
	kfree_skb(skb);
out_unlock:
	dev_unlock_list();
	return err;
}
#endif

static int packet_rcv(struct sk_buff *skb, struct device *dev,  struct packet_type *pt)
{
	struct sock *sk;
	struct sockaddr_ll *sll = (struct sockaddr_ll*)skb->cb;
	
	/*
	 *	When we registered the protocol we saved the socket in the data
	 *	field for just this event.
	 */

	sk = (struct sock *) pt->data;

	if (skb->pkt_type == PACKET_LOOPBACK) {
		kfree_skb(skb);
		return 0;
	}

	skb->dev = dev;

	sll->sll_family = AF_PACKET;
	sll->sll_hatype = dev->type;
	sll->sll_protocol = skb->protocol;
	sll->sll_pkttype = skb->pkt_type;
	sll->sll_ifindex = dev->ifindex;
	sll->sll_halen = 0;

	if (dev->hard_header_parse)
		sll->sll_halen = dev->hard_header_parse(skb, sll->sll_addr);

	if (dev->hard_header) {
		/* The device has an explicit notion of ll header,
		   exported to higher levels.

		   Otherwise, the device hides datails of it frame
		   structure, so that corresponding packet head
		   never delivered to user.
		 */
		if (sk->type != SOCK_DGRAM)
			skb_push(skb, skb->data - skb->mac.raw);
		else if (skb->pkt_type == PACKET_OUTGOING) {
			/* Special case: outgoing packets have ll header at head */
			skb_pull(skb, skb->nh.raw - skb->data);
		}
	}

	/*
	 *	Charge the memory to the socket. This is done specifically
	 *	to prevent sockets using all the memory up.
	 */

	if (sock_queue_rcv_skb(sk,skb)<0)
	{
		kfree_skb(skb);
		return 0;
	}
	return(0);
}

static int packet_sendmsg(struct socket *sock, struct msghdr *msg, int len,
			  struct scm_cookie *scm)
{
	struct sock *sk = sock->sk;
	struct sockaddr_ll *saddr=(struct sockaddr_ll *)msg->msg_name;
	struct sk_buff *skb;
	struct device *dev;
	unsigned short proto;
	unsigned char *addr;
	int ifindex, err, reserve = 0;
	
	/*
	 *	Check the flags. 
	 */

	if (msg->msg_flags&~MSG_DONTWAIT) 
		return(-EINVAL);

	/*
	 *	Get and verify the address. 
	 */
	 
	if (saddr == NULL) {
		ifindex	= sk->protinfo.af_packet->ifindex;
		proto	= sk->num;
		addr	= NULL;
	} else {
		if (msg->msg_namelen < sizeof(struct sockaddr_ll)) 
			return -EINVAL;
		ifindex	= saddr->sll_ifindex;
		proto	= saddr->sll_protocol;
		addr	= saddr->sll_addr;
	}

	dev = dev_get_by_index(ifindex);
	if (dev == NULL)
		return -ENXIO;
	if (sock->type == SOCK_RAW)
		reserve = dev->hard_header_len;

	if (len > dev->mtu+reserve)
  		return -EMSGSIZE;

	dev_lock_list();

	skb = sock_alloc_send_skb(sk, len+dev->hard_header_len+15, 0, 
				msg->msg_flags & MSG_DONTWAIT, &err);
	if (skb==NULL)
		goto out_unlock;

	skb_reserve(skb, (dev->hard_header_len+15)&~15);
	skb->nh.raw = skb->data;

	if (dev->hard_header) {
		int res;
		err = -EINVAL;
		res = dev->hard_header(skb, dev, ntohs(proto), addr, NULL, len);
		if (sock->type != SOCK_DGRAM) {
			skb->tail = skb->data;
			skb->len = 0;
		} else if (res < 0)
			goto out_free;
	}

	/* Returns -EFAULT on error */
	err = memcpy_fromiovec(skb_put(skb,len), msg->msg_iov, len);
	skb->protocol = proto;
	skb->dev = dev;
	skb->priority = sk->priority;
	if (err)
		goto out_free;

	err = -ENETDOWN;
	if (!(dev->flags & IFF_UP))
		goto out_free;

	/*
	 *	Now send it
	 */

	dev_unlock_list();
	dev_queue_xmit(skb);
	return(len);

out_free:
	kfree_skb(skb);
out_unlock:
	dev_unlock_list();
	return err;
}

static void packet_destroy_timer(unsigned long data)
{
	struct sock *sk=(struct sock *)data;

	if (!atomic_read(&sk->wmem_alloc) && !atomic_read(&sk->rmem_alloc)) {
		sk_free(sk);
		MOD_DEC_USE_COUNT;
		return;
	}

	sk->timer.expires=jiffies+10*HZ;
	add_timer(&sk->timer);
	printk(KERN_DEBUG "packet sk destroy delayed\n");
}

/*
 *	Close a PACKET socket. This is fairly simple. We immediately go
 *	to 'closed' state and remove our protocol entry in the device list.
 */

static int packet_release(struct socket *sock, struct socket *peersock)
{
	struct sk_buff	*skb;
	struct sock *sk = sock->sk;

	if (!sk)
		return 0;

	sklist_remove_socket(&packet_sklist, sk);

	/*
	 *	Unhook packet receive handler.
	 */

	if (sk->protinfo.af_packet->running)
	{
		/*
		 *	Remove the protocol hook
		 */
		 
		dev_remove_pack(&sk->protinfo.af_packet->prot_hook);
		sk->protinfo.af_packet->running = 0;
	}

#ifdef CONFIG_PACKET_MULTICAST
	packet_flush_mclist(sk);
#endif

	/*
	 *	Now the socket is dead. No more input will appear.
	 */

	sk->state_change(sk);	/* It is useless. Just for sanity. */

	sock->sk = NULL;
	sk->socket = NULL;
	sk->dead = 1;

	/* Purge queues */

	while ((skb=skb_dequeue(&sk->receive_queue))!=NULL)
		kfree_skb(skb);

	if (atomic_read(&sk->rmem_alloc) || atomic_read(&sk->wmem_alloc)) {
		sk->timer.data=(unsigned long)sk;
		sk->timer.expires=jiffies+HZ;
		sk->timer.function=packet_destroy_timer;
		add_timer(&sk->timer);
		return 0;
	}

	sk_free(sk);
	MOD_DEC_USE_COUNT;
	return 0;
}

/*
 *	Attach a packet hook.
 */

static int packet_do_bind(struct sock *sk, struct device *dev, int protocol)
{
	/*
	 *	Detach an existing hook if present.
	 */

	if (sk->protinfo.af_packet->running) {
		dev_remove_pack(&sk->protinfo.af_packet->prot_hook);
		sk->protinfo.af_packet->running = 0;
	}

	sk->num = protocol;
	sk->protinfo.af_packet->prot_hook.type = protocol;
	sk->protinfo.af_packet->prot_hook.dev = dev;

	if (protocol == 0)
		return 0;

	if (dev) {
		sk->protinfo.af_packet->ifindex = dev->ifindex;
		if (dev->flags&IFF_UP) {
			dev_add_pack(&sk->protinfo.af_packet->prot_hook);
			sk->protinfo.af_packet->running = 1;
		} else {
			sk->err = ENETDOWN;
			sk->error_report(sk);
		}
	} else {
		sk->protinfo.af_packet->ifindex = 0;
		dev_add_pack(&sk->protinfo.af_packet->prot_hook);
		sk->protinfo.af_packet->running = 1;
	}
	return 0;
}

/*
 *	Bind a packet socket to a device
 */

#ifdef CONFIG_SOCK_PACKET

static int packet_bind_spkt(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	struct sock *sk=sock->sk;
	char name[15];
	struct device *dev;
	
	/*
	 *	Check legality
	 */
	 
	if(addr_len!=sizeof(struct sockaddr))
		return -EINVAL;
	strncpy(name,uaddr->sa_data,14);
	name[14]=0;

	dev = dev_get(name);
	if (dev)
		return packet_do_bind(sk, dev, sk->num);
	return -ENODEV;
}
#endif

static int packet_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	struct sockaddr_ll *sll = (struct sockaddr_ll*)uaddr;
	struct sock *sk=sock->sk;
	struct device *dev = NULL;
	
	/*
	 *	Check legality
	 */
	 
	if (addr_len < sizeof(struct sockaddr_ll))
		return -EINVAL;
	if (sll->sll_family != AF_PACKET)
		return -EINVAL;

	if (sll->sll_ifindex) {
		dev = dev_get_by_index(sll->sll_ifindex);
		if (dev == NULL)
			return -ENODEV;
	}
	return packet_do_bind(sk, dev, sll->sll_protocol ? : sk->num);
}


/*
 *	Create a packet of type SOCK_PACKET. 
 */

static int packet_create(struct socket *sock, int protocol)
{
	struct sock *sk;
	int err;

	if (!capable(CAP_NET_RAW))
		return -EPERM;
	if (sock->type != SOCK_DGRAM && sock->type != SOCK_RAW
#ifdef CONFIG_SOCK_PACKET
	    && sock->type != SOCK_PACKET
#endif
	    )
		return -ESOCKTNOSUPPORT;

	sock->state = SS_UNCONNECTED;
	MOD_INC_USE_COUNT;

	err = -ENOBUFS;
	sk = sk_alloc(PF_PACKET, GFP_KERNEL, 1);
	if (sk == NULL)
		goto out;

	sk->reuse = 1;
	sock->ops = &packet_ops;
#ifdef CONFIG_SOCK_PACKET
	if (sock->type == SOCK_PACKET)
		sock->ops = &packet_ops_spkt;
#endif
	sock_init_data(sock,sk);

	sk->protinfo.af_packet = kmalloc(sizeof(struct packet_opt), GFP_KERNEL);
	if (sk->protinfo.af_packet == NULL)
		goto out_free;
	memset(sk->protinfo.af_packet, 0, sizeof(struct packet_opt));
	sk->zapped=0;
	sk->family = PF_PACKET;
	sk->num = protocol;

	/*
	 *	Attach a protocol block
	 */

	sk->protinfo.af_packet->prot_hook.func = packet_rcv;
#ifdef CONFIG_SOCK_PACKET
	if (sock->type == SOCK_PACKET)
		sk->protinfo.af_packet->prot_hook.func = packet_rcv_spkt;
#endif
	sk->protinfo.af_packet->prot_hook.data = (void *)sk;

	if (protocol) {
		sk->protinfo.af_packet->prot_hook.type = protocol;
		dev_add_pack(&sk->protinfo.af_packet->prot_hook);
		sk->protinfo.af_packet->running = 1;
	}

	sklist_insert_socket(&packet_sklist, sk);
	return(0);

out_free:
	sk_free(sk);
out:
	MOD_DEC_USE_COUNT;
	return err;
}

/*
 *	Pull a packet from our receive queue and hand it to the user.
 *	If necessary we block.
 */

/*
 *	NOTE about lock_* & release_* primitives.
 *	I do not understand why skb_recv_datagram locks socket.
 *	My analysis shows that it is useless for datagram services:
 *	i.e. here, udp, raw and netlink. FIX ME if I am wrong,
 *	but lock&release are necessary only for SOCK_STREAM
 *	and, maybe, SOCK_SEQPACKET.
 *							--ANK
 */

static int packet_recvmsg(struct socket *sock, struct msghdr *msg, int len,
			  int flags, struct scm_cookie *scm)
{
	struct sock *sk = sock->sk;
	struct sk_buff *skb;
	int copied, err;

#if 0
	/* What error should we return now? EUNATTACH? */
	if (sk->protinfo.af_packet->ifindex < 0)
		return -ENODEV;
#endif

	/*
	 *	If the address length field is there to be filled in, we fill
	 *	it in now.
	 */

	if (sock->type == SOCK_PACKET)
		msg->msg_namelen = sizeof(struct sockaddr_pkt);
	else
		msg->msg_namelen = sizeof(struct sockaddr_ll);

	/*
	 *	Call the generic datagram receiver. This handles all sorts
	 *	of horrible races and re-entrancy so we can forget about it
	 *	in the protocol layers.
	 *
	 *	Now it will return ENETDOWN, if device have just gone down,
	 *	but then it will block.
	 */

	skb=skb_recv_datagram(sk,flags,flags&MSG_DONTWAIT,&err);

	/*
	 *	An error occurred so return it. Because skb_recv_datagram() 
	 *	handles the blocking we don't see and worry about blocking
	 *	retries.
	 */

	if(skb==NULL)
		goto out;

	/*
	 *	You lose any data beyond the buffer you gave. If it worries a
	 *	user program they can ask the device for its MTU anyway.
	 */

	copied = skb->len;
	if (copied > len)
	{
		copied=len;
		msg->msg_flags|=MSG_TRUNC;
	}

	/* We can't use skb_copy_datagram here */
	err = memcpy_toiovec(msg->msg_iov, skb->data, copied);
	if (err)
		goto out_free;
	sk->stamp=skb->stamp;

	if (msg->msg_name)
		memcpy(msg->msg_name, skb->cb, msg->msg_namelen);

	/*
	 *	Free or return the buffer as appropriate. Again this
	 *	hides all the races and re-entrancy issues from us.
	 */
	err = copied;

out_free:
	skb_free_datagram(sk, skb);
out:
	return err;
}

#ifdef CONFIG_SOCK_PACKET
static int packet_getname_spkt(struct socket *sock, struct sockaddr *uaddr,
			       int *uaddr_len, int peer)
{
	struct device *dev;
	struct sock *sk	= sock->sk;

	if (peer)
		return -EOPNOTSUPP;

	uaddr->sa_family = AF_PACKET;
	dev = dev_get_by_index(sk->protinfo.af_packet->ifindex);
	if (dev)
		strncpy(uaddr->sa_data, dev->name, 15);
	else
		memset(uaddr->sa_data, 0, 14);
	*uaddr_len = sizeof(*uaddr);

	return 0;
}
#endif

static int packet_getname(struct socket *sock, struct sockaddr *uaddr,
			  int *uaddr_len, int peer)
{
	struct device *dev;
	struct sock *sk = sock->sk;
	struct sockaddr_ll *sll = (struct sockaddr_ll*)uaddr;

	if (peer)
		return -EOPNOTSUPP;

	sll->sll_family = AF_PACKET;
	sll->sll_ifindex = sk->protinfo.af_packet->ifindex;
	sll->sll_protocol = sk->num;
	dev = dev_get_by_index(sk->protinfo.af_packet->ifindex);
	if (dev) {
		sll->sll_hatype = dev->type;
		sll->sll_halen = dev->addr_len;
		memcpy(sll->sll_addr, dev->dev_addr, dev->addr_len);
	} else {
		sll->sll_hatype = 0;	/* Bad: we have no ARPHRD_UNSPEC */
		sll->sll_halen = 0;
	}
	*uaddr_len = sizeof(*sll);

	return 0;
}

#ifdef CONFIG_PACKET_MULTICAST
static void packet_dev_mc(struct device *dev, struct packet_mclist *i, int what)
{
	switch (i->type) {
	case PACKET_MR_MULTICAST:
		if (what > 0)
			dev_mc_add(dev, i->addr, i->alen, 0);
		else
			dev_mc_delete(dev, i->addr, i->alen, 0);
		break;
	case PACKET_MR_PROMISC:
		dev_set_promiscuity(dev, what);
		break;
	case PACKET_MR_ALLMULTI:
		dev_set_allmulti(dev, what);
		break;
	default:
	}
}

static void packet_dev_mclist(struct device *dev, struct packet_mclist *i, int what)
{
	for ( ; i; i=i->next) {
		if (i->ifindex == dev->ifindex)
			packet_dev_mc(dev, i, what);
	}
}

static int packet_mc_add(struct sock *sk, struct packet_mreq *mreq)
{
	struct packet_mclist *ml, *i;
	struct device *dev;
	int err;

	rtnl_shlock();

	err = -ENODEV;
	dev = dev_get_by_index(mreq->mr_ifindex);
	if (!dev)
		goto done;

	err = -EINVAL;
	if (mreq->mr_alen > dev->addr_len)
		goto done;

	err = -ENOBUFS;
	i = (struct packet_mclist *)kmalloc(sizeof(*i), GFP_KERNEL);
	if (i == NULL)
		goto done;

	err = 0;
	for (ml=sk->protinfo.af_packet->mclist; ml; ml=ml->next) {
		if (ml->ifindex == mreq->mr_ifindex &&
		    ml->type == mreq->mr_type &&
		    ml->alen == mreq->mr_alen &&
		    memcmp(ml->addr, mreq->mr_address, ml->alen) == 0) {
			ml->count++;
			/* Free the new element ... */
			kfree(i);
			goto done;
		}
	}

	i->type = mreq->mr_type;
	i->ifindex = mreq->mr_ifindex;
	i->alen = mreq->mr_alen;
	memcpy(i->addr, mreq->mr_address, i->alen);
	i->count = 1;
	i->next = sk->protinfo.af_packet->mclist;
	sk->protinfo.af_packet->mclist = i;
	packet_dev_mc(dev, i, +1);

done:
	rtnl_shunlock();
	return err;
}

static int packet_mc_drop(struct sock *sk, struct packet_mreq *mreq)
{
	struct packet_mclist *ml, **mlp;

	for (mlp=&sk->protinfo.af_packet->mclist; (ml=*mlp)!=NULL; mlp=&ml->next) {
		if (ml->ifindex == mreq->mr_ifindex &&
		    ml->type == mreq->mr_type &&
		    ml->alen == mreq->mr_alen &&
		    memcmp(ml->addr, mreq->mr_address, ml->alen) == 0) {
			if (--ml->count == 0) {
				struct device *dev;
				*mlp = ml->next;
				dev = dev_get_by_index(ml->ifindex);
				if (dev)
					packet_dev_mc(dev, ml, -1);
				kfree_s(ml, sizeof(*ml));
			}
			return 0;
		}
	}
	return -EADDRNOTAVAIL;
}

static void packet_flush_mclist(struct sock *sk)
{
	struct packet_mclist *ml;

	while ((ml=sk->protinfo.af_packet->mclist) != NULL) {
		struct device *dev;
		sk->protinfo.af_packet->mclist = ml->next;
		if ((dev = dev_get_by_index(ml->ifindex)) != NULL)
			packet_dev_mc(dev, ml, -1);
		kfree_s(ml, sizeof(*ml));
	}
}

static int
packet_setsockopt(struct socket *sock, int level, int optname, char *optval, int optlen)
{
	struct sock *sk = sock->sk;
	struct packet_mreq mreq;

	if (level != SOL_PACKET)
		return -ENOPROTOOPT;
	
	switch(optname)	{
	case PACKET_ADD_MEMBERSHIP:	
	case PACKET_DROP_MEMBERSHIP:
			
		if (optlen<sizeof(mreq))
			return -EINVAL;
		if (copy_from_user(&mreq,optval,sizeof(mreq)))
			return -EFAULT;
		if (optname == PACKET_ADD_MEMBERSHIP)
			return packet_mc_add(sk, &mreq);
		else
			return packet_mc_drop(sk, &mreq);
	default:	
		return -ENOPROTOOPT;
	}
}
#endif

static int packet_notifier(struct notifier_block *this, unsigned long msg, void *data)
{
	struct sock *sk;
	struct packet_opt *po;
	struct device *dev = (struct device*)data;

	for (sk = packet_sklist; sk; sk = sk->next) {
		po = sk->protinfo.af_packet;

		switch (msg) {
		case NETDEV_DOWN:
		case NETDEV_UNREGISTER:
			if (dev->ifindex == po->ifindex) {
				if (po->running) {
					dev_remove_pack(&po->prot_hook);
					po->running = 0;
					sk->err = ENETDOWN;
					sk->error_report(sk);
				}
				if (msg == NETDEV_UNREGISTER) {
					po->ifindex = -1;
					po->prot_hook.dev = NULL;
				}
			}
#ifdef CONFIG_PACKET_MULTICAST
			if (po->mclist)
				packet_dev_mclist(dev, po->mclist, -1);
#endif
			break;
		case NETDEV_UP:
			if (dev->ifindex == po->ifindex && sk->num && po->running==0) {
				dev_add_pack(&po->prot_hook);
				po->running = 1;
			}
#ifdef CONFIG_PACKET_MULTICAST
			if (po->mclist)
				packet_dev_mclist(dev, po->mclist, +1);
#endif
			break;
		}
	}
	return NOTIFY_DONE;
}


static int packet_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	struct sock *sk = sock->sk;
	int err;
	int pid;

	switch(cmd) 
	{
		case FIOSETOWN:
		case SIOCSPGRP:
			err = get_user(pid, (int *) arg);
			if (err)
				return err; 
			if (current->pid != pid && current->pgrp != -pid && 
			    !capable(CAP_NET_ADMIN))
				return -EPERM;
			sk->proc = pid;
			return(0);
		case FIOGETOWN:
		case SIOCGPGRP:
			return put_user(sk->proc, (int *)arg);
		case SIOCGSTAMP:
			if(sk->stamp.tv_sec==0)
				return -ENOENT;
			err = -EFAULT;
			if (!copy_to_user((void *)arg, &sk->stamp, sizeof(struct timeval)))
				err = 0;
			return err;
		case SIOCGIFFLAGS:
#ifndef CONFIG_INET
		case SIOCSIFFLAGS:
#endif
		case SIOCGIFCONF:
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
		case SIOCGIFINDEX:
		case SIOCGIFNAME:
		case SIOCGIFCOUNT:
		case SIOCSIFHWBROADCAST:
			return(dev_ioctl(cmd,(void *) arg));

		case SIOCGIFBR:
		case SIOCSIFBR:
#ifdef CONFIG_BRIDGE		
			return(br_ioctl(cmd,(void *) arg));
#else
			return -ENOPKG;
#endif						
			
#ifdef CONFIG_INET
		case SIOCADDRT:
		case SIOCDELRT:
		case SIOCDARP:
		case SIOCGARP:
		case SIOCSARP:
		case SIOCDRARP:
		case SIOCGRARP:
		case SIOCSRARP:
		case SIOCGIFADDR:
		case SIOCSIFADDR:
		case SIOCGIFBRDADDR:
		case SIOCSIFBRDADDR:
		case SIOCGIFNETMASK:
		case SIOCSIFNETMASK:
		case SIOCGIFDSTADDR:
		case SIOCSIFDSTADDR:
		case SIOCSIFFLAGS:
		case SIOCADDDLCI:
		case SIOCDELDLCI:
			return inet_dgram_ops.ioctl(sock, cmd, arg);
#endif

		default:
			if ((cmd >= SIOCDEVPRIVATE) &&
			    (cmd <= (SIOCDEVPRIVATE + 15)))
				return(dev_ioctl(cmd,(void *) arg));

#ifdef CONFIG_NET_RADIO
			if((cmd >= SIOCIWFIRST) && (cmd <= SIOCIWLAST))
				return(dev_ioctl(cmd,(void *) arg));
#endif
			return -EOPNOTSUPP;
	}
	/*NOTREACHED*/
	return(0);
}

#ifdef CONFIG_SOCK_PACKET
struct proto_ops packet_ops_spkt = {
	PF_PACKET,

	sock_no_dup,
	packet_release,
	packet_bind_spkt,
	sock_no_connect,
	sock_no_socketpair,
	sock_no_accept,
	packet_getname_spkt,
	datagram_poll,
	packet_ioctl,
	sock_no_listen,
	sock_no_shutdown,
	sock_no_setsockopt,
	sock_no_getsockopt,
	sock_no_fcntl,
	packet_sendmsg_spkt,
	packet_recvmsg
};
#endif

struct proto_ops packet_ops = {
	PF_PACKET,

	sock_no_dup,
	packet_release,
	packet_bind,
	sock_no_connect,
	sock_no_socketpair,
	sock_no_accept,
	packet_getname, 
	datagram_poll,
	packet_ioctl,
	sock_no_listen,
	sock_no_shutdown,
#ifdef CONFIG_PACKET_MULTICAST
	packet_setsockopt,
#else
	sock_no_setsockopt,
#endif
	sock_no_getsockopt,
	sock_no_fcntl,
	packet_sendmsg,
	packet_recvmsg
};

static struct net_proto_family packet_family_ops = {
	PF_PACKET,
	packet_create
};

struct notifier_block packet_netdev_notifier={
	packet_notifier,
	NULL,
	0
};


#ifdef MODULE
void cleanup_module(void)
{
	unregister_netdevice_notifier(&packet_netdev_notifier);
	sock_unregister(PF_PACKET);
	return;
}


int init_module(void)
#else
void __init packet_proto_init(struct net_proto *pro)
#endif
{
	sock_register(&packet_family_ops);
	register_netdevice_notifier(&packet_netdev_notifier);
#ifdef MODULE
	return 0;
#endif
}
