/*
 *	An implementation of the Acorn Econet and AUN protocols.
 *	Philip Blundell <philb@gnu.org>
 *
 *	Fixes:
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 */

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
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/route.h>
#include <linux/inet.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/wireless.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/inet_common.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/if_ec.h>
#include <net/udp.h>
#include <net/ip.h>
#include <asm/spinlock.h>
#include <linux/inetdevice.h>

static struct proto_ops econet_ops;
static struct sock *econet_sklist;

static spinlock_t aun_queue_lock;

#ifdef CONFIG_ECONET_AUNUDP
static struct socket *udpsock;
#define AUN_PORT	0x8000

struct aunhdr
{
	unsigned char code;		/* AUN magic protocol byte */
	unsigned char port;
	unsigned char cb;
	unsigned char pad;
	unsigned long handle;
};

static unsigned long aun_seq = 0;

/* Queue of packets waiting to be transmitted. */
static struct sk_buff_head aun_queue;
static struct timer_list ab_cleanup_timer;

#endif		/* CONFIG_ECONET_AUNUDP */

/* Per-packet information */
struct ec_cb
{
	struct sockaddr_ec sec;
	unsigned long cookie;		/* Supplied by user. */
#ifdef CONFIG_ECONET_AUNUDP
	int done;
	unsigned long seq;		/* Sequencing */
	unsigned long timeout;		/* Timeout */
	unsigned long start;		/* jiffies */
#endif
#ifdef CONFIG_ECONET_NATIVE
	void (*sent)(struct sk_buff *, int result);
#endif
};

struct ec_device
{
	struct device *dev;		/* Real device structure */
	unsigned char station, net;	/* Econet protocol address */
	struct ec_device *prev, *next;	/* Linked list */
};

static struct ec_device *edevlist = NULL;

static spinlock_t edevlist_lock;

/*
 *	Faster version of edev_get - call with IRQs off
 */

static __inline__ struct ec_device *__edev_get(struct device *dev)
{
	struct ec_device *edev;
	for (edev = edevlist; edev; edev = edev->next)
	{
		if (edev->dev == dev)
			break;
	}
	return edev;
}

/*
 *	Find an Econet device given its `dev' pointer.  This is IRQ safe.
 */

static struct ec_device *edev_get(struct device *dev)
{
	struct ec_device *edev;
	unsigned long flags;
	spin_lock_irqsave(&edevlist_lock, flags);
	edev = __edev_get(dev);
	spin_unlock_irqrestore(&edevlist_lock, flags);
	return edev;
}

/*
 *	Pull a packet from our receive queue and hand it to the user.
 *	If necessary we block.
 */

static int econet_recvmsg(struct socket *sock, struct msghdr *msg, int len,
			  int flags, struct scm_cookie *scm)
{
	struct sock *sk = sock->sk;
	struct sk_buff *skb;
	int copied, err;

	msg->msg_namelen = sizeof(struct sockaddr_ec);

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

/*
 *	Bind an Econet socket.
 */

static int econet_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	struct sockaddr_ec *sec = (struct sockaddr_ec *)uaddr;
	struct sock *sk=sock->sk;
	
	/*
	 *	Check legality
	 */
	 
	if (addr_len < sizeof(struct sockaddr_ec))
		return -EINVAL;
	if (sec->sec_family != AF_ECONET)
		return -EINVAL;
	
	sk->protinfo.af_econet->cb = sec->cb;
	sk->protinfo.af_econet->port = sec->port;
	sk->protinfo.af_econet->station = sec->addr.station;
	sk->protinfo.af_econet->net = sec->addr.net;

	return 0;
}

/*
 *	Queue a transmit result for the user to be told about.
 */

static void tx_result(struct sock *sk, unsigned long cookie, int result)
{
	struct sk_buff *skb = alloc_skb(0, GFP_ATOMIC);
	struct ec_cb *eb;
	struct sockaddr_ec *sec;

	if (skb == NULL)
	{
		printk(KERN_DEBUG "ec: memory squeeze, transmit result dropped.\n");
		return;
	}

	eb = (struct ec_cb *)&skb->cb;
	sec = (struct sockaddr_ec *)&eb->sec;
	memset(sec, 0, sizeof(struct sockaddr_ec));
	sec->cookie = cookie;
	sec->type = ECTYPE_TRANSMIT_STATUS | result;
	sec->sec_family = AF_ECONET;

	if (sock_queue_rcv_skb(sk, skb) < 0)
		kfree_skb(skb);
}

#ifdef CONFIG_ECONET_NATIVE
/*
 *	Called by the Econet hardware driver when a packet transmit
 *	has completed.  Tell the user.
 */

static void ec_tx_done(struct sk_buff *skb, int result)
{
	struct ec_cb *eb = (struct ec_cb *)&skb->cb;
	tx_result(skb->sk, eb->cookie, result);
}
#endif

/*
 *	Send a packet.  We have to work out which device it's going out on
 *	and hence whether to use real Econet or the UDP emulation.
 */

static int econet_sendmsg(struct socket *sock, struct msghdr *msg, int len,
			  struct scm_cookie *scm)
{
	struct sock *sk = sock->sk;
	struct sockaddr_ec *saddr=(struct sockaddr_ec *)msg->msg_name;
	struct device *dev;
	struct ec_addr addr;
	struct ec_device *edev;
	int err;
	unsigned char port, cb;
	struct sk_buff *skb;
	struct ec_cb *eb;
#ifdef CONFIG_ECONET_NATIVE
	unsigned short proto = 0;
#endif
#ifdef CONFIG_ECONET_AUNUDP
	struct msghdr udpmsg;
	struct iovec iov[msg->msg_iovlen+1];
	struct aunhdr ah;
	struct sockaddr_in udpdest;
	__kernel_size_t size;
	int i;
	mm_segment_t oldfs;
#endif
		
	/*
	 *	Check the flags. 
	 */

	if (msg->msg_flags&~MSG_DONTWAIT) 
		return(-EINVAL);

	/*
	 *	Get and verify the address. 
	 */
	 
	if (saddr == NULL) {
		addr.station = sk->protinfo.af_econet->station;
		addr.net = sk->protinfo.af_econet->net;
		port = sk->protinfo.af_econet->port;
		cb = sk->protinfo.af_econet->cb;
	} else {
		if (msg->msg_namelen < sizeof(struct sockaddr_ec)) 
			return -EINVAL;
		addr.station = saddr->addr.station;
		addr.net = saddr->addr.net;
		port = saddr->port;
		cb = saddr->cb;
	}

	/* Look for a device with the right network number. */
	for (edev = edevlist; edev && (edev->net != addr.net); 
	     edev = edev->next);

	/* Bridge?  What's that? */
	if (edev == NULL) 
		return -ENETUNREACH;

	dev = edev->dev;

	if (dev->type == ARPHRD_ECONET)
	{
		/* Real hardware Econet.  We're not worthy etc. */
#ifdef CONFIG_ECONET_NATIVE
		dev_lock_list();
		
		skb = sock_alloc_send_skb(sk, len+dev->hard_header_len+15, 0, 
					  msg->msg_flags & MSG_DONTWAIT, &err);
		if (skb==NULL)
			goto out_unlock;
		
		skb_reserve(skb, (dev->hard_header_len+15)&~15);
		skb->nh.raw = skb->data;
		
		eb = (struct ec_cb *)&skb->cb;
		
		eb->cookie = saddr->cookie;
		eb->sec = *saddr;
		eb->sent = ec_tx_done;

		if (dev->hard_header) {
			int res;
			err = -EINVAL;
			res = dev->hard_header(skb, dev, ntohs(proto), &addr, NULL, len);
			if (sock->type != SOCK_DGRAM) {
				skb->tail = skb->data;
				skb->len = 0;
			} else if (res < 0)
				goto out_free;
		}
		
		/* Copy the data. Returns -EFAULT on error */
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
#else
		err = -EPROTOTYPE;
#endif
		return err;
	}

#ifdef CONFIG_ECONET_AUNUDP
	/* AUN virtual Econet. */

	if (udpsock == NULL)
		return -ENETDOWN;		/* No socket - can't send */
	
	/* Make up a UDP datagram and hand it off to some higher intellect. */

	memset(&udpdest, 0, sizeof(udpdest));
	udpdest.sin_family = AF_INET;
	udpdest.sin_port = htons(AUN_PORT);

	/* At the moment we use the stupid Acorn scheme of Econet address
	   y.x maps to IP a.b.c.x.  This should be replaced with something
	   more flexible and more aware of subnet masks.  */
	{
		struct in_device *idev = (struct in_device *)dev->ip_ptr;
		unsigned long network = ntohl(idev->ifa_list->ifa_address) & 
			0xffffff00;		/* !!! */
		udpdest.sin_addr.s_addr = htonl(network | addr.station);
	}

	ah.port = port;
	ah.cb = cb & 0x7f;
	ah.code = 2;		/* magic */
	ah.pad = 0;

	/* tack our header on the front of the iovec */
	size = sizeof(struct aunhdr);
	iov[0].iov_base = (void *)&ah;
	iov[0].iov_len = size;
	for (i = 0; i < msg->msg_iovlen; i++) {
		void *base = msg->msg_iov[i].iov_base;
		size_t len = msg->msg_iov[i].iov_len;
		/* Check it now since we switch to KERNEL_DS later. */
		if ((err = verify_area(VERIFY_READ, base, len)) < 0)
			return err;
		iov[i+1].iov_base = base;
		iov[i+1].iov_len = len;
		size += len;
	}

	/* Get a skbuff (no data, just holds our cb information) */
	if ((skb = sock_alloc_send_skb(sk, 0, 0, 
			     msg->msg_flags & MSG_DONTWAIT, &err)) == NULL)
		return err;

	eb = (struct ec_cb *)&skb->cb;

	eb->cookie = saddr->cookie;
	eb->timeout = (5*HZ);
	eb->start = jiffies;
	ah.handle = aun_seq;
	eb->seq = (aun_seq++);
	eb->sec = *saddr;

	skb_queue_tail(&aun_queue, skb);

	udpmsg.msg_name = (void *)&udpdest;
	udpmsg.msg_namelen = sizeof(udpdest);
	udpmsg.msg_iov = &iov[0];
	udpmsg.msg_iovlen = msg->msg_iovlen + 1;
	udpmsg.msg_control = NULL;
	udpmsg.msg_controllen = 0;
	udpmsg.msg_flags=0;

	oldfs = get_fs(); set_fs(KERNEL_DS);	/* More privs :-) */
	err = sock_sendmsg(udpsock, &udpmsg, size);
	set_fs(oldfs);
#else
	err = -EPROTOTYPE;
#endif
	return err;
}

/*
 *	Look up the address of a socket.
 */

static int econet_getname(struct socket *sock, struct sockaddr *uaddr,
			  int *uaddr_len, int peer)
{
	struct sock *sk = sock->sk;
	struct sockaddr_ec *sec = (struct sockaddr_ec *)uaddr;

	if (peer)
		return -EOPNOTSUPP;

	sec->sec_family = AF_ECONET;
	sec->port = sk->protinfo.af_econet->port;
	sec->addr.station = sk->protinfo.af_econet->station;
	sec->addr.net = sk->protinfo.af_econet->net;

	*uaddr_len = sizeof(*sec);
	return 0;
}

static void econet_destroy_timer(unsigned long data)
{
	struct sock *sk=(struct sock *)data;

	if (!atomic_read(&sk->wmem_alloc) && !atomic_read(&sk->rmem_alloc)) {
		sk_free(sk);
		MOD_DEC_USE_COUNT;
		return;
	}

	sk->timer.expires=jiffies+10*HZ;
	add_timer(&sk->timer);
	printk(KERN_DEBUG "econet socket destroy delayed\n");
}

/*
 *	Close an econet socket.
 */

static int econet_release(struct socket *sock, struct socket *peersock)
{
	struct sk_buff	*skb;
	struct sock *sk = sock->sk;

	if (!sk)
		return 0;

	sklist_remove_socket(&econet_sklist, sk);

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
		sk->timer.function=econet_destroy_timer;
		add_timer(&sk->timer);
		return 0;
	}

	sk_free(sk);
	MOD_DEC_USE_COUNT;
	return 0;
}

/*
 *	Create an Econet socket
 */

static int econet_create(struct socket *sock, int protocol)
{
	struct sock *sk;
	int err;

	/* Econet only provides datagram services. */
	if (sock->type != SOCK_DGRAM)
		return -ESOCKTNOSUPPORT;

	sock->state = SS_UNCONNECTED;
	MOD_INC_USE_COUNT;

	err = -ENOBUFS;
	sk = sk_alloc(PF_ECONET, GFP_KERNEL, 1);
	if (sk == NULL)
		goto out;

	sk->reuse = 1;
	sock->ops = &econet_ops;
	sock_init_data(sock,sk);

	sk->protinfo.af_econet = kmalloc(sizeof(struct econet_opt), GFP_KERNEL);
	if (sk->protinfo.af_econet == NULL)
		goto out_free;
	memset(sk->protinfo.af_econet, 0, sizeof(struct econet_opt));
	sk->zapped=0;
	sk->family = PF_ECONET;
	sk->num = protocol;

	sklist_insert_socket(&econet_sklist, sk);
	return(0);

out_free:
	sk_free(sk);
out:
	MOD_DEC_USE_COUNT;
	return err;
}

/*
 *	Handle Econet specific ioctls
 */

static int ec_dev_ioctl(struct socket *sock, unsigned int cmd, void *arg)
{
	struct ifreq ifr;
	struct ec_device *edev;
	struct device *dev;
	unsigned long flags;
	struct sockaddr_ec *sec;

	/*
	 *	Fetch the caller's info block into kernel space
	 */

	if (copy_from_user(&ifr, arg, sizeof(struct ifreq)))
		return -EFAULT;

	if ((dev = dev_get(ifr.ifr_name)) == NULL) 
		return -ENODEV;

	sec = (struct sockaddr_ec *)&ifr.ifr_addr;

	switch (cmd)
	{
	case SIOCSIFADDR:
		spin_lock_irqsave(&edevlist_lock, flags);
		edev = __edev_get(dev);
		if (edev == NULL)
		{
			/* Magic up a new one. */
			edev = kmalloc(GFP_KERNEL, sizeof(struct ec_device));
			if (edev == NULL) {
				printk("af_ec: memory squeeze.\n");
				spin_unlock_irqrestore(&edevlist_lock, flags);
				return -ENOMEM;
			}
			memset(edev, 0, sizeof(struct ec_device));
			edev->dev = dev;
			edev->next = edevlist;
			edevlist = edev;
		}
		edev->station = sec->addr.station;
		edev->net = sec->addr.net;
		spin_unlock_irqrestore(&edevlist_lock, flags);
		return 0;

	case SIOCGIFADDR:
		spin_lock_irqsave(&edevlist_lock, flags);
		edev = __edev_get(dev);
		if (edev == NULL)
		{
			spin_unlock_irqrestore(&edevlist_lock, flags);
			return -ENODEV;
		}
		memset(sec, 0, sizeof(struct sockaddr_ec));
		sec->addr.station = edev->station;
		sec->addr.net = edev->net;
		sec->sec_family = AF_ECONET;
		spin_unlock_irqrestore(&edevlist_lock, flags);
		if (copy_to_user(arg, &ifr, sizeof(struct ifreq)))
			return -EFAULT;
		return 0;
	}

	return -EINVAL;
}

/*
 *	Handle generic ioctls
 */

static int econet_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
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
			if (current->pid != pid && current->pgrp != -pid && !suser())
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
		case SIOCSIFFLAGS:
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

		case SIOCSIFADDR:
		case SIOCGIFADDR:
			return ec_dev_ioctl(sock, cmd, (void *)arg);
			break;

		default:
			return(dev_ioctl(cmd,(void *) arg));
	}
	/*NOTREACHED*/
	return 0;
}

static struct net_proto_family econet_family_ops = {
	PF_ECONET,
	econet_create
};

static struct proto_ops econet_ops = {
	PF_ECONET,

	sock_no_dup,
	econet_release,
	econet_bind,
	sock_no_connect,
	sock_no_socketpair,
	sock_no_accept,
	econet_getname, 
	datagram_poll,
	econet_ioctl,
	sock_no_listen,
	sock_no_shutdown,
	sock_no_setsockopt,
	sock_no_getsockopt,
	sock_no_fcntl,
	econet_sendmsg,
	econet_recvmsg
};

/*
 *	Find the listening socket, if any, for the given data.
 */

static struct sock *ec_listening_socket(unsigned char port, unsigned char
					station, unsigned char net)
{
	struct sock *sk = econet_sklist;

	while (sk)
	{
		struct econet_opt *opt = sk->protinfo.af_econet;
		if ((opt->port == port || opt->port == 0) && 
		    (opt->station == station || opt->station == 0) &&
		    (opt->net == net || opt->net == 0))
			return sk;
		sk = sk->sklist_next;
	}

	return NULL;
}

#ifdef CONFIG_ECONET_AUNUDP

/*
 *	Send an AUN protocol response. 
 */

static void aun_send_response(__u32 addr, unsigned long seq, int code, int cb)
{
	struct sockaddr_in sin;
	struct iovec iov;
	struct aunhdr ah;
	struct msghdr udpmsg;
	int err;
	mm_segment_t oldfs;
	
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(AUN_PORT);
	sin.sin_addr.s_addr = addr;

	ah.code = code;
	ah.pad = 0;
	ah.port = 0;
	ah.cb = cb;
	ah.handle = seq;

	iov.iov_base = (void *)&ah;
	iov.iov_len = sizeof(ah);

	udpmsg.msg_name = (void *)&sin;
	udpmsg.msg_namelen = sizeof(sin);
	udpmsg.msg_iov = &iov;
	udpmsg.msg_iovlen = 1;
	udpmsg.msg_control = NULL;
	udpmsg.msg_controllen = 0;
	udpmsg.msg_flags=0;

	oldfs = get_fs(); set_fs(KERNEL_DS);
	err = sock_sendmsg(udpsock, &udpmsg, sizeof(ah));
	set_fs(oldfs);
}

/*
 *	Handle incoming AUN packets.  Work out if anybody wants them,
 *	and send positive or negative acknowledgements as appropriate.
 */

static void aun_incoming(struct sk_buff *skb, struct aunhdr *ah, size_t len)
{
	struct ec_device *edev = edev_get(skb->dev);
	struct iphdr *ip = skb->nh.iph;
	unsigned char stn = ntohl(ip->saddr) & 0xff;
	struct sock *sk;
	struct sk_buff *newskb;
	struct ec_cb *eb;
	struct sockaddr_ec *sec;

	if (edev == NULL)
		return;		/* Device not configured for AUN */
	
	if ((sk = ec_listening_socket(ah->port, stn, edev->net)) == NULL)
		goto bad;		/* Nobody wants it */

	newskb = alloc_skb((len - sizeof(struct aunhdr) + 15) & ~15, 
			   GFP_ATOMIC);
	if (newskb == NULL)
	{
		printk(KERN_DEBUG "AUN: memory squeeze, dropping packet.\n");
		/* Send nack and hope sender tries again */
		goto bad;
	}

	eb = (struct ec_cb *)&newskb->cb;
	sec = (struct sockaddr_ec *)&eb->sec;
	memset(sec, 0, sizeof(struct sockaddr_ec));
	sec->sec_family = AF_ECONET;
	sec->type = ECTYPE_PACKET_RECEIVED;
	sec->port = ah->port;
	sec->cb = ah->cb;
	sec->addr.net = edev->net;
	sec->addr.station = stn;

	memcpy(skb_put(newskb, len - sizeof(struct aunhdr)), (void *)(ah+1), 
	       len - sizeof(struct aunhdr));

	if (sock_queue_rcv_skb(sk, newskb) < 0)
	{
		/* Socket is bankrupt. */
		kfree_skb(newskb);
		goto bad;
	}

	aun_send_response(ip->saddr, ah->handle, 3, 0);
	return;

bad:
	aun_send_response(ip->saddr, ah->handle, 4, 0);
}

/*
 *	Handle incoming AUN transmit acknowledgements.  If the sequence
 *      number matches something in our backlog then kill it and tell
 *	the user.  If the remote took too long to reply then we may have
 *	dropped the packet already.
 */

static void aun_tx_ack(unsigned long seq, int result)
{
	struct sk_buff *skb;
	unsigned long flags;
	struct ec_cb *eb;

	spin_lock_irqsave(&aun_queue_lock, flags);
	skb = skb_peek(&aun_queue);
	while (skb && skb != (struct sk_buff *)&aun_queue)
	{
		struct sk_buff *newskb = skb->next;
		eb = (struct ec_cb *)&skb->cb;
		if (eb->seq == seq)
			goto foundit;

		skb = newskb;
	}
	spin_unlock_irqrestore(&aun_queue_lock, flags);
	printk(KERN_DEBUG "AUN: unknown sequence %ld\n", seq);
	return;

foundit:
	tx_result(skb->sk, eb->cookie, result);
	skb_unlink(skb);
	spin_unlock_irqrestore(&aun_queue_lock, flags);
}

/*
 *	Deal with received AUN frames - sort out what type of thing it is
 *	and hand it to the right function.
 */

static void aun_data_available(struct sock *sk, int slen)
{
	int err;
	struct sk_buff *skb;
	unsigned char *data;
	struct aunhdr *ah;
	struct iphdr *ip;
	size_t len;

	while ((skb = skb_recv_datagram(sk, 0, 1, &err)) == NULL) {
		if (err == -EAGAIN) {
			printk(KERN_ERR "AUN: no data available?!");
			return;
		}
		printk(KERN_DEBUG "AUN: recvfrom() error %d\n", -err);
	}

	data = skb->h.raw + sizeof(struct udphdr);
	ah = (struct aunhdr *)data;
	len = skb->len - sizeof(struct udphdr);
	ip = skb->nh.iph;

	switch (ah->code)
	{
	case 2:
		aun_incoming(skb, ah, len);
		break;
	case 3:
		aun_tx_ack(ah->handle, ECTYPE_TRANSMIT_OK);
		break;
	case 4:
		aun_tx_ack(ah->handle, ECTYPE_TRANSMIT_NOT_LISTENING);
		break;
#if 0
		/* This isn't quite right yet. */
	case 5:
		aun_send_response(ip->saddr, ah->handle, 6, ah->cb);
		break;
#endif
	default:
		printk(KERN_DEBUG "unknown AUN packet (type %d)\n", data[0]);
	}

	skb_free_datagram(sk, skb);
}

/*
 *	Called by the timer to manage the AUN transmit queue.  If a packet
 *	was sent to a dead or nonexistent host then we will never get an
 *	acknowledgement back.  After a few seconds we need to spot this and
 *	drop the packet.
 */


static void ab_cleanup(unsigned long h)
{
	struct sk_buff *skb;
	unsigned long flags;

	spin_lock_irqsave(&aun_queue_lock, flags);
	skb = skb_peek(&aun_queue);
	while (skb && skb != (struct sk_buff *)&aun_queue)
	{
		struct sk_buff *newskb = skb->next;
		struct ec_cb *eb = (struct ec_cb *)&skb->cb;
		if ((jiffies - eb->start) > eb->timeout)
		{
			tx_result(skb->sk, eb->cookie, 
				  ECTYPE_TRANSMIT_NOT_PRESENT);
			skb_unlink(skb);
		}
		skb = newskb;
	}
	spin_unlock_irqrestore(&aun_queue_lock, flags);

	mod_timer(&ab_cleanup_timer, jiffies + (HZ*2));
}

__initfunc(static int aun_udp_initialise(void))
{
	int error;
	struct sockaddr_in sin;

	skb_queue_head_init(&aun_queue);
	spin_lock_init(&aun_queue_lock);
	init_timer(&ab_cleanup_timer);
	ab_cleanup_timer.expires = jiffies + (HZ*2);
	ab_cleanup_timer.function = ab_cleanup;
	add_timer(&ab_cleanup_timer);

	memset(&sin, 0, sizeof(sin));
	sin.sin_port = htons(AUN_PORT);

	/* We can count ourselves lucky Acorn machines are too dim to
	   speak IPv6. :-) */
	if ((error = sock_create(PF_INET, SOCK_DGRAM, 0, &udpsock)) < 0)
	{
		printk("AUN: socket error %d\n", -error);
		return error;
	}
	
	udpsock->sk->reuse = 1;
	udpsock->sk->allocation = GFP_ATOMIC;	/* we're going to call it
						   from interrupts */
	
	error = udpsock->ops->bind(udpsock, (struct sockaddr *)&sin,
				sizeof(sin));
	if (error < 0)
	{
		printk("AUN: bind error %d\n", -error);
		goto release;
	}

	udpsock->sk->data_ready = aun_data_available;

	return 0;

release:
	sock_release(udpsock);
	udpsock = NULL;
	return error;
}
#endif

static int econet_notifier(struct notifier_block *this, unsigned long msg, void *data)
{
	struct device *dev = (struct device *)data;
	struct ec_device *edev;
	unsigned long flags;

	switch (msg) {
	case NETDEV_UNREGISTER:
		/* A device has gone down - kill any data we hold for it. */
		spin_lock_irqsave(&edevlist_lock, flags);
		for (edev = edevlist; edev; edev = edev->next)
		{
			if (edev->dev == dev)
			{
				if (edev->prev)
					edev->prev->next = edev->next;
				else
					edevlist = edev->next;
				if (edev->next)
					edev->next->prev = edev->prev;
				kfree(edev);
				break;
			}
		}
		spin_unlock_irqrestore(&edevlist_lock, flags);
		break;
	}

	return NOTIFY_DONE;
}

struct notifier_block econet_netdev_notifier={
	econet_notifier,
	NULL,
	0
};

#ifdef MODULE
void cleanup_module(void)
{
#ifdef CONFIG_ECONET_AUNUDP
	del_timer(&ab_cleanup_timer);
	if (udpsock)
		sock_release(udpsock);
#endif
	unregister_netdevice_notifier(&econet_netdev_notifier);
	sock_unregister(econet_family_ops.family);
	return;
}

int init_module(void)
#else
__initfunc(void econet_proto_init(struct net_proto *pro))
#endif
{
	spin_lock_init(&edevlist_lock);
	spin_lock_init(&aun_queue_lock);
	/* Stop warnings from happening on UP systems. */
	(void)edevlist_lock;
	(void)aun_queue_lock;
	sock_register(&econet_family_ops);
#ifdef CONFIG_ECONET_AUNUDP
	aun_udp_initialise();
#endif
	register_netdevice_notifier(&econet_netdev_notifier);
#ifdef MODULE
	return 0;
#endif
}
