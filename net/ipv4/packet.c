/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		PACKET - implements raw packet sockets.
 *
 *		Doesn't belong in IP but it's currently too hooked into ip
 *		to separate.
 *
 * Version:	@(#)packet.c	1.0.6	05/25/93
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
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 */
 
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/fcntl.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/if_packet.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <asm/system.h>
#include <asm/segment.h>

/*
 *	We really ought to have a single public _inline_ min function!
 */

static unsigned long min(unsigned long a, unsigned long b)
{
	if (a < b) 
		return(a);
	return(b);
}


/*
 *	This should be the easiest of all, all we do is copy it into a buffer. 
 */
 
int packet_rcv(struct sk_buff *skb, struct device *dev,  struct packet_type *pt)
{
	struct sock *sk;
	
	/*
	 *	When we registered the protocol we saved the socket in the data
	 *	field for just this event.
	 */

	sk = (struct sock *) pt->data;	
	
	/*
	 *	Yank back the headers [hope the device set this
	 *	right or kerboom...]
	 */
	 
	skb_push(skb,skb->data-skb->mac.raw);

	/*
	 *	The SOCK_PACKET socket receives _all_ frames.
	 */
	 
	skb->dev = dev;

	/*
	 *	Charge the memory to the socket. This is done specifically
	 *	to prevent sockets using all the memory up.
	 */
	 
	if(sock_queue_rcv_skb(sk,skb)<0)
	{
		skb->sk = NULL;
		kfree_skb(skb, FREE_READ);
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
 
static int packet_sendmsg(struct sock *sk, struct msghdr *msg, int len,
	      int noblock, int flags)
{
	struct sk_buff *skb;
	struct device *dev;
	struct sockaddr_pkt *saddr=(struct sockaddr_pkt *)msg->msg_name;
	unsigned short proto=0;

	/*
	 *	Check the flags. 
	 */

	if (flags) 
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

	skb = sock_wmalloc(sk, len, 0, GFP_KERNEL);

	/*
	 *	If the write buffer is full, then tough. At this level the user gets to
	 *	deal with the problem - do your own algorithmic backoffs. That's far
	 *	more flexible.
	 */
	 
	if (skb == NULL) 
	{
		return(-ENOBUFS);
	}
	
	/*
	 *	Fill it in 
	 */
	 
	skb->sk = sk;
	skb->free = 1;
	memcpy_fromiovec(skb_put(skb,len), msg->msg_iov, len);
	skb->arp = 1;		/* No ARP needs doing on this (complete) frame */
	skb->protocol = proto;

	/*
	 *	Now send it
	 */

	if (dev->flags & IFF_UP) 
		dev_queue_xmit(skb, dev, sk->priority);
	else
		kfree_skb(skb, FREE_WRITE);
	return(len);
}

/*
 *	Close a SOCK_PACKET socket. This is fairly simple. We immediately go
 *	to 'closed' state and remove our protocol entry in the device list.
 *	The release_sock() will destroy the socket if a user has closed the
 *	file side of the object.
 */

static void packet_close(struct sock *sk, unsigned long timeout)
{
	/*
	 *	Stop more data and kill the socket off.
	 */

	lock_sock(sk);
	sk->state = TCP_CLOSE;

	/*
	 *	Unhook the notifier
	 */

	unregister_netdevice_notifier(&sk->protinfo.af_packet.notifier);

	if(sk->protinfo.af_packet.prot_hook)
	{
		/*
		 *	Remove the protocol hook
		 */
		 
		dev_remove_pack((struct packet_type *)sk->protinfo.af_packet.prot_hook);

		/*
		 *	Dispose of litter carefully.
		 */
	 
		kfree_s((void *)sk->protinfo.af_packet.prot_hook, sizeof(struct packet_type));
		sk->protinfo.af_packet.prot_hook = NULL;
	}
	
	release_sock(sk);
	destroy_sock(sk);
}

/*
 *	Attach a packet hook to a device.
 */

int packet_attach(struct sock *sk, struct device *dev)
{
	struct packet_type *p = (struct packet_type *) kmalloc(sizeof(*p), GFP_KERNEL);
	if (p == NULL) 
		return(-ENOMEM);

	p->func = packet_rcv;
	p->type = sk->num;
	p->data = (void *)sk;
	p->dev = dev;
	dev_add_pack(p);
   
	/*
	 *	We need to remember this somewhere. 
	 */
   
	sk->protinfo.af_packet.prot_hook = p;
	sk->protinfo.af_packet.bound_dev = dev;
	return 0;	
}
 
/*
 *	Bind a packet socket to a device
 */

static int packet_bind(struct sock *sk, struct sockaddr *uaddr, int addr_len)
{
	char name[15];
	struct device *dev;
	
	/*
	 *	Check legality
	 */
	 
	if(addr_len!=sizeof(struct sockaddr))
		return -EINVAL;
	strncpy(name,uaddr->sa_data,14);
	name[14]=0;
	
	/*
	 *	Lock the device chain while we sanity check
	 *	the bind request.
	 */
	 
	dev_lock_list();
	dev=dev_get(name);
	if(dev==NULL)
	{
		dev_unlock_list();
		return -ENODEV;
	}
	
	if(!(dev->flags&IFF_UP))
	{
		dev_unlock_list();
		return -ENETDOWN;
	}
	
	/*
	 *	Perform the request.
	 */
	 
	memcpy(sk->protinfo.af_packet.device_name,name,15);
	
	/*
	 *	Rewrite an existing hook if present.
	 */
	 
	if(sk->protinfo.af_packet.prot_hook)
	{
		dev_remove_pack(sk->protinfo.af_packet.prot_hook);
		sk->protinfo.af_packet.prot_hook->dev=dev;
		sk->protinfo.af_packet.bound_dev=dev;
		dev_add_pack(sk->protinfo.af_packet.prot_hook);
	}
	else
	{
		int err=packet_attach(sk, dev);
		if(err)
		{
			dev_unlock_list();
			return err;
		}
	}
	/*
	 *	Now the notifier is set up right this lot is safe.
	 */
	dev_unlock_list();
	return 0;
}

/*
 *	This hook is called when a device goes up or down so that
 *	SOCK_PACKET sockets can come unbound properly.
 */

static int packet_unbind(struct notifier_block *this, unsigned long msg, void *data)
{
	struct inet_packet_opt *ipo=(struct inet_packet_opt *)this;
	if(msg==NETDEV_DOWN && data==ipo->bound_dev)
	{
		/*
		 *	Our device has gone down.
		 */
		ipo->bound_dev=NULL;
		dev_remove_pack(ipo->prot_hook);
		kfree(ipo->prot_hook);
		ipo->prot_hook=NULL;
	}
	return NOTIFY_DONE;
}


/*
 *	Create a packet of type SOCK_PACKET. 
 */

static int packet_init(struct sock *sk)
{
	/*
	 *	Attach a protocol block
	 */
	 
	int err=packet_attach(sk, NULL);
	if(err)
		return err;
		
	/*
	 *	Set up the per socket notifier.
	 */
	 
	sk->protinfo.af_packet.notifier.notifier_call=packet_unbind;
	sk->protinfo.af_packet.notifier.priority=0;

	register_netdevice_notifier(&sk->protinfo.af_packet.notifier);

	return(0);
}


/*
 *	Pull a packet from our receive queue and hand it to the user.
 *	If necessary we block.
 */
 
int packet_recvmsg(struct sock *sk, struct msghdr *msg, int len,
	        int noblock, int flags,int *addr_len)
{
	int copied=0;
	struct sk_buff *skb;
	struct sockaddr_pkt *saddr=(struct sockaddr_pkt *)msg->msg_name;
	int err;

	if (sk->shutdown & RCV_SHUTDOWN) 
		return(0);
		
	/*
	 *	If there is no protocol hook then the device is down.
	 */
	 
	if(sk->protinfo.af_packet.prot_hook==NULL)
		return -ENETDOWN;
		
	/*
	 *	If the address length field is there to be filled in, we fill
	 *	it in now.
	 */

	if (addr_len) 
		*addr_len=sizeof(*saddr);
	
	/*
	 *	Call the generic datagram receiver. This handles all sorts
	 *	of horrible races and re-entrancy so we can forget about it
	 *	in the protocol layers.
	 */
	 
	skb=skb_recv_datagram(sk,flags,noblock,&err);
	
	/*
	 *	An error occurred so return it. Because skb_recv_datagram() 
	 *	handles the blocking we don't see and worry about blocking
	 *	retries.
	 */
	 
	if(skb==NULL)
		return err;
		
	/*
	 *	You lose any data beyond the buffer you gave. If it worries a
	 *	user program they can ask the device for its MTU anyway.
	 */
	 
	copied = min(len, skb->len);

	memcpy_toiovec(msg->msg_iov, skb->data, copied);	/* We can't use skb_copy_datagram here */
	sk->stamp=skb->stamp;

	/*
	 *	Copy the address. 
	 */
	 
	if (saddr) 
	{
		saddr->spkt_family = skb->dev->type;
		strncpy(saddr->spkt_device,skb->dev->name, 15);
		saddr->spkt_protocol = skb->protocol;
	}
	
	/*
	 *	Free or return the buffer as appropriate. Again this hides all the
	 *	races and re-entrancy issues from us.
	 */

	skb_free_datagram(sk, skb);

	return(copied);
}

/*
 *	This structure declares to the lower layer socket subsystem currently
 *	incorrectly embedded in the IP code how to behave. This interface needs
 *	a lot of work and will change.
 */
 
struct proto packet_prot = 
{
	packet_close,
	ip_build_header,	/* Not actually used */
	NULL,
	NULL,
	ip_queue_xmit,		/* These two are not actually used */
	NULL,
	NULL,
	NULL,
	NULL, 
	datagram_select,
	NULL,			/* No ioctl */
	packet_init,
	NULL,
	NULL,			/* No set/get socket options */
	NULL,
	packet_sendmsg,		/* Sendmsg */
	packet_recvmsg,		/* Recvmsg */
	packet_bind,		/* Bind */
	128,
	0,
	"PACKET",
	0, 0
};

	
