/*********************************************************************
 *                
 * Filename:      af_irda.c
 * Version:       0.1
 * Description:   IrDA sockets implementation
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sun May 31 10:12:43 1998
 * Modified at:   Sat Feb 20 01:31:15 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * Sources:       af_netroom.c, af_ax25.x
 * 
 *     Copyright (c) 1997 Dag Brattli, All Rights Reserved.
 *      
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 *  
 *     Neither Dag Brattli nor University of Tromsø admit liability nor
 *     provide warranty for any of this software. This material is 
 *     provided "AS-IS" and at no charge.
 *     
 ********************************************************************/

#include <linux/config.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/init.h>
#include <linux/if_arp.h>
#include <linux/net.h>

#include <asm/uaccess.h>

#include <net/sock.h>

#include <net/irda/irda.h>
#include <net/irda/irmod.h>
#include <net/irda/irttp.h>

extern int irda_init(void);
extern void irda_cleanup(void);
extern int irlap_input(struct sk_buff *, struct device *, struct packet_type *);

static struct proto_ops irda_proto_ops;

#define IRDA_MAX_HEADER (TTP_HEADER+LMP_HEADER+LAP_HEADER)

#define IRDA_SOCKETS

#ifdef IRDA_SOCKETS

/*
 * Function irda_getname (sock, uaddr, uaddr_len, peer)
 *
 *    
 *
 */
static int irda_getname( struct socket *sock, struct sockaddr *uaddr,
			 int *uaddr_len, int peer)
{
	DEBUG( 0, __FUNCTION__ "(), Not implemented!\n");

	return 0;
}

/*
 * Function irda_listen (sock, backlog)
 *
 *    
 *
 */
static int irda_listen( struct socket *sock, int backlog)
{
	struct sock *sk = sock->sk;

	if (sk->type == SOCK_SEQPACKET && sk->state != TCP_LISTEN) {
		sk->max_ack_backlog = backlog;
		sk->state           = TCP_LISTEN;
		return 0;
	}

	return -EOPNOTSUPP;
}


/*
 * Function irda_connect (sock, uaddr, addr_len, flags)
 *
 *    Connect to a IrDA device
 *
 */
static int irda_connect( struct socket *sock, struct sockaddr *uaddr,
	int addr_len, int flags)
{
#if 0
	struct sock *sk = (struct sock *)sock->data;
	struct sockaddr_irda *addr = (struct sockaddr_irda *)uaddr;
	irda_address *user, *source = NULL;
	struct device *dev;

	DEBUG( 0, __FUNCTION__ "()\n");
	
	if (sk->state == TCP_ESTABLISHED && sock->state == SS_CONNECTING) {
		sock->state = SS_CONNECTED;
		return 0;   /* Connect completed during a ERESTARTSYS event */
	}
	
	if (sk->state == TCP_CLOSE && sock->state == SS_CONNECTING) {
		sock->state = SS_UNCONNECTED;
		return -ECONNREFUSED;
	}
	
	if (sk->state == TCP_ESTABLISHED)
		return -EISCONN;      /* No reconnect on a seqpacket socket */
	
	sk->state   = TCP_CLOSE;	
	sock->state = SS_UNCONNECTED;

	if (addr_len != sizeof(struct sockaddr_irda))
		return -EINVAL;


	/* Now the loop */
	if (sk->state != TCP_ESTABLISHED && (flags & O_NONBLOCK))
		return -EINPROGRESS;
		
	cli();	/* To avoid races on the sleep */
	
	/* A Connect Ack with Choke or timeout or failed routing will go to
	 * closed.  */
	while ( sk->state == TCP_SYN_SENT) {
		interruptible_sleep_on( sk->sleep);
		if (current->signal & ~current->blocked) {
			sti();
			return -ERESTARTSYS;
		}
	}
	
	if (sk->state != TCP_ESTABLISHED) {
		sti();
		sock->state = SS_UNCONNECTED;
		return sock_error( sk);	/* Always set at this point */
	}
	
	sock->state = SS_CONNECTED;

	sti();
#endif
	return 0;
}

/*
 * Function irda_create (sock, protocol)
 *
 *    Create IrDA socket
 *
 */
static int irda_create(struct socket *sock, int protocol)
{
	struct sock *sk;

	DEBUG( 0, __FUNCTION__ "()\n");

	/* Compatibility */
	if (sock->type == SOCK_PACKET) {
		static int warned; 
		if (net_families[PF_PACKET]==NULL)
		{
#if defined(CONFIG_KMOD) && defined(CONFIG_PACKET_MODULE)
			char module_name[30];
			sprintf(module_name,"net-pf-%d", PF_PACKET);
			request_module(module_name);
			if (net_families[PF_PACKET] == NULL)
#endif
				return -ESOCKTNOSUPPORT;
		}
		if (!warned++)
			printk(KERN_INFO "%s uses obsolete (PF_INET,SOCK_PACKET)\n", current->comm);
		return net_families[PF_PACKET]->create(sock, protocol);
	}
	
/* 	if (sock->type != SOCK_SEQPACKET || protocol != 0) */
/* 		return -ESOCKTNOSUPPORT; */

	/* Allocate socket */
	if ((sk = sk_alloc(PF_IRDA, GFP_ATOMIC, 1)) == NULL)
		return -ENOMEM;
	
	sock_init_data(sock, sk);

	sock->ops    = &irda_proto_ops;
	sk->protocol = protocol;

	return 0;
}

/*
 * Function irda_destroy_socket (tsap)
 *
 *    Destroy socket
 *
 */
void irda_destroy_socket(struct tsap_cb *tsap)
{
	DEBUG( 0, __FUNCTION__ "()\n");

	return;
}

/*
 * Function irda_release (sock, peer)
 *
 *    
 *
 */
static int irda_release( struct socket *sock, struct socket *peer)
{
#if 0
        struct sock *sk = (struct sock *)sock->data;
	
	DEBUG( 0, __FUNCTION__ "()\n");


        if (sk == NULL) return 0;
	
        if (sk->type == SOCK_SEQPACKET) {                    

	
	} else {
		sk->state       = TCP_CLOSE;
                sk->shutdown   |= SEND_SHUTDOWN;
                sk->state_change(sk);
                sk->dead        = 1;
                irda_destroy_socket( sk->protinfo.irda);
        }

        sock->data = NULL;      
        sk->socket = NULL;      /* Not used, but we should do this. **/
#endif
        return 0;
}

static int irda_recvmsg(struct socket *sock, struct msghdr *msg, int size,
			int flags, struct scm_cookie *scm)
{
	struct sock *sk = sock->sk;
	struct sk_buff *skb;
	int copied, err;

	DEBUG(0, __FUNCTION__ "()\n");

	skb = skb_recv_datagram(sk, flags & ~MSG_DONTWAIT, 
				flags & MSG_DONTWAIT, &err);
	if (!skb)
		return err;

	skb->h.raw = skb->data;
	copied     = skb->len;
	
	if (copied > size) {
		copied = size;
		msg->msg_flags |= MSG_TRUNC;
	}
	skb_copy_datagram_iovec(skb, 0, msg->msg_iov, copied);

	skb_free_datagram(sk, skb);

	return copied;
}

static int irda_shutdown( struct socket *sk, int how)
{
	DEBUG( 0, __FUNCTION__ "()\n");

        /* FIXME - generate DM and RNR states */
        return -EOPNOTSUPP;
}


unsigned int irda_poll( struct file *file, struct socket *sock, 
			struct poll_table_struct *wait)
{
	DEBUG(0, __FUNCTION__ "()\n");
	return 0;
}

/*
 * Function irda_ioctl (sock, cmd, arg)
 *
 *    
 *
 */
static int irda_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	struct sock *sk = sock->sk;

	DEBUG( 0, __FUNCTION__ "()\n");
	
	switch (cmd) {
	case TIOCOUTQ: {
		long amount;
		amount = sk->sndbuf - atomic_read(&sk->wmem_alloc);
		if (amount < 0)
			amount = 0;
		if (put_user(amount, (unsigned int *)arg))
			return -EFAULT;
		return 0;
	}
	
	case TIOCINQ: {
		struct sk_buff *skb;
		long amount = 0L;
		/* These two are safe on a single CPU system as only user tasks fiddle here */
		if ((skb = skb_peek(&sk->receive_queue)) != NULL)
			amount = skb->len;
		if (put_user(amount, (unsigned int *)arg))
			return -EFAULT;
		return 0;
	}
	
	case SIOCGSTAMP:
		if (sk != NULL) {
			if (sk->stamp.tv_sec == 0)
				return -ENOENT;
			if (copy_to_user((void *)arg, &sk->stamp, sizeof(struct timeval)))
				return -EFAULT;
			return 0;
		}
		return -EINVAL;
		
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
		return -EINVAL;
		
	default:
		return dev_ioctl(cmd, (void *)arg);
	}

	/*NOTREACHED*/
	return 0;
}

static struct net_proto_family irda_family_ops =
{
	PF_IRDA,
	irda_create
};

static struct proto_ops irda_proto_ops = {
	PF_IRDA,
	
	sock_no_dup,
	irda_release,
	sock_no_bind,
	irda_connect,
	sock_no_socketpair,
	sock_no_accept,
	irda_getname,
	irda_poll,
	irda_ioctl,
	irda_listen,
	irda_shutdown,
	sock_no_setsockopt,
	sock_no_getsockopt,
	sock_no_fcntl,
	sock_no_sendmsg,
	irda_recvmsg
};

#endif /* IRDA_SOCKETS */

static int irda_device_event(struct notifier_block *this, unsigned long event,
			     void *ptr)
{
	struct device *dev = (struct device *) ptr;
	
	DEBUG(3, __FUNCTION__ "()\n");
	
        /* Reject non IrDA devices */
	if (dev->type != ARPHRD_IRDA) 
		return NOTIFY_DONE;
	
        switch (event) {
	case NETDEV_UP:
		DEBUG(3, __FUNCTION__ "(), NETDEV_UP\n");
		/* irda_dev_device_up(dev); */
		break;
	case NETDEV_DOWN:
		DEBUG(3, __FUNCTION__ "(), NETDEV_DOWN\n");
		/* irda_kill_by_device(dev); */
		/* irda_rt_device_down(dev); */
		/* irda_dev_device_down(dev); */
		break;
	default:
		break;
        }

        return NOTIFY_DONE;
}

static struct packet_type irda_packet_type = 
{
	0,	/* MUTTER ntohs(ETH_P_IRDA),*/
	NULL,
	irlap_input, /* irda_driver_rcv, */
	NULL,
	NULL,
};

static struct notifier_block irda_dev_notifier = {
	irda_device_event,
	NULL,
	0
};

/*
 * Function irda_proto_init (pro)
 *
 *    Initialize IrDA protocol layer
 *
 */
__initfunc(void irda_proto_init(struct net_proto *pro))
{
	DEBUG( 4, __FUNCTION__ "\n");

	sock_register(&irda_family_ops);

	irda_packet_type.type = htons(ETH_P_IRDA);
        dev_add_pack(&irda_packet_type);

	register_netdevice_notifier( &irda_dev_notifier);

	irda_init();
}

/*
 * Function irda_proto_cleanup (void)
 *
 *    Remove IrDA protocol layer
 *
 */
void irda_proto_cleanup(void)
{
	DEBUG( 4, __FUNCTION__ "\n");

	irda_packet_type.type = htons(ETH_P_IRDA);
        dev_remove_pack(&irda_packet_type);

        unregister_netdevice_notifier( &irda_dev_notifier);
	
	sock_unregister(PF_IRDA);
	irda_cleanup();
	
        return;
}

