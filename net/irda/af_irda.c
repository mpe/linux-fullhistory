/*********************************************************************
 *                
 * Filename:      af_irda.c
 * Version:       0.1
 * Description:   IrDA sockets implementation
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sun May 31 10:12:43 1998
 * Modified at:   Thu Jan 14 13:42:16 1999
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

#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <net/sock.h>
#include <asm/segment.h>

#include <net/irda/irda.h>
#include <net/irda/irmod.h>
#include <net/irda/irttp.h>

extern int irda_init(void);
extern void irda_cleanup(void);
extern int irlap_input(struct sk_buff *, struct device *, struct packet_type *);

#define IRDA_MAX_HEADER (TTP_HEADER+LMP_HEADER+LAP_HEADER)

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
 * Function irda_sendmsg (sock, msg, len, noblock, flags)
 *
 *    
 *
 */
static int irda_sendmsg( struct socket *sock, struct msghdr *msg, int len, 
			 int noblock, int flags)
{
#if 0
	struct sock *sk = (struct sock *) sock->data;
	/* struct sockaddr_irda *usax = (struct sockaddr_irda *)msg->msg_name; */
	int err;
	struct sk_buff *skb;
	unsigned char *asmptr;
	int size;
	struct tsap_cb *tsap;
	
	DEBUG( 0, __FUNCTION__ "()\n");

	if (sk->err)
		return sock_error(sk);

	if (flags)
		return -EINVAL;
	
	if (sk->zapped)
		return -EADDRNOTAVAIL;
	
	if (sk->debug)
		printk( "IrDA: sendto: Addresses built.\n");
	
	/* Build a packet */
	if (sk->debug)
		printk( "IrDA: sendto: building packet.\n");
	
	size = len + IRDA_MAX_HEADER;
	
	if ((skb = sock_alloc_send_skb(sk, size, 0, 0, &err)) == NULL)
		return err;
	
	skb->sk   = sk;
	skb->free = 1;
	skb->arp  = 1;
	
	skb_reserve(skb, size - len);
	
	memcpy_fromiovec( asmptr, msg->msg_iov, len);
	
	if (sk->debug)
		printk( "IrDA: Transmitting buffer\n");
	
        if (sk->state != TCP_ESTABLISHED) {
                kfree_skb( skb, FREE_WRITE);
		return -ENOTCONN;
	}
	
	tsap = (struct tsap_cb *) sk->protinfo.irda;
	ASSERT( tsap != NULL, return -ENODEV;);
	ASSERT( tsap->magic == TTP_TSAP_MAGIC, return -EBADR;);
	
	irttp_data_request( tsap, skb);
#endif	
	return len;
}

/*
 * Function irda_recvmsg (sock, msg, size, noblock, flags, addr_len)
 *
 *    
 *
 */
static int irda_recvmsg( struct socket *sock, struct msghdr *msg, int size, 
			 int noblock, int flags, int *addr_len)
{
	int copied=0;
#if 0
	struct sock *sk = (struct sock *)sock->data;
	struct sockaddr_irda *sax = (struct sockaddr_irda *)msg->msg_name;
	struct sk_buff *skb;
	int er;

	DEBUG( 0, __FUNCTION__ "()\n");

	if (sk->err)
		return sock_error(sk);
	
	if (addr_len != NULL)
		*addr_len = sizeof(*sax);

	/*
	 * This works for seqpacket too. The receiver has ordered the queue for
	 * us! We do one quick check first though
	 */
	if (sk->state != TCP_ESTABLISHED)
		return -ENOTCONN;

	/* Now we can treat all alike */
	if ((skb = skb_recv_datagram( sk, flags, noblock, &er)) == NULL)
		return er;

/* 	if (!sk->nr->hdrincl) { */
/* 		skb_pull(skb, NR_NETWORK_LEN + NR_TRANSPORT_LEN); */
/* 		skb->h.raw = skb->data; */
/* 	} */

	copied = (size < skb->len) ? size : skb->len;
	skb_copy_datagram_iovec(skb, 0, msg->msg_iov, copied);

	skb_free_datagram(sk, skb);
#endif
	return copied;
}

/*
 * Function irda_listen (sock, backlog)
 *
 *    
 *
 */
static int irda_listen( struct socket *sock, int backlog)
{
#if 0
	struct sock *sk = (struct sock *)sock->data;

	if (sk->state != TCP_LISTEN) {
		sk->max_ack_backlog = backlog;
		sk->state           = TCP_LISTEN;
		return 0;
        }
#endif
	return -EOPNOTSUPP;
}

/*
 * Function irda_bind (sock, uaddr, addr_len)
 *
 *    Bind to a specified TSAP
 *
 */
static int irda_bind( struct socket *sock, struct sockaddr *uaddr, 
		      int addr_len)
{
#if 0	
	struct sock *sk;
	struct full_sockaddr_irda *addr = (struct full_sockaddr_irda *)uaddr;
	struct device *dev;
        irda_address *user, *source;
        struct tsap_cb *tsap;
	struct notify_t notify;

	DEBUG( 0, __FUNCTION__ "()\n");

	sk = (struct sock *) sock->data;

	if ( sk->zapped == 0)
		return -EIO;
	
	irda_notify_init( &notify);
	tsap = irttp_open_tsap( LSAP_ANY, DEFAULT_INITIAL_CREDIT, &notify);

	sk->zapped = 0;

	if (sk->debug)
		printk("IrDA: socket is bound\n");
#endif
	return 0;
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
	
static int irda_socketpair( struct socket *sock1, struct socket *sock2)
{
	DEBUG( 0, __FUNCTION__ "(), Not implemented\n");

	return -EOPNOTSUPP;
}

static int irda_accept(struct socket *sock, struct socket *newsock, int flags)
{
	struct sock *sk;
	struct sock *newsk;
	struct sk_buff *skb;

	DEBUG( 0, __FUNCTION__ "()\n");
#if 0
	if (newsock->data)
		sk_free(newsock->data);

	newsock->data = NULL;
	
	sk = (struct sock *)sock->data;

	if (sk->type != SOCK_SEQPACKET)
		return -EOPNOTSUPP;
	
	if (sk->state != TCP_LISTEN)
		return -EINVAL;
		
	/*
	 *	The write queue this time is holding sockets ready to use
	 *	hooked into the SABM we saved
	 */
	do {
		cli();
		if ((skb = skb_dequeue(&sk->receive_queue)) == NULL) {
			if (flags & O_NONBLOCK) {
				sti();
				return 0;
			}
			interruptible_sleep_on(sk->sleep);
			if (current->signal & ~current->blocked) {
				sti();
				return -ERESTARTSYS;
			}
		}
	} while (skb == NULL);

	newsk = skb->sk;
	newsk->pair = NULL;
	sti();

	/* Now attach up the new socket */
	skb->sk = NULL;
	kfree_skb(skb, FREE_READ);
	sk->ack_backlog--;
	newsock->data = newsk;
#endif
	return 0;
}

static void def_callback1(struct sock *sk)
{
	DEBUG( 0, __FUNCTION__ "()\n");

        if (!sk->dead)
                wake_up_interruptible(sk->sleep);
}

static void def_callback2(struct sock *sk, int len)
{
	DEBUG( 0, __FUNCTION__ "()\n");

        if (!sk->dead)
                wake_up_interruptible(sk->sleep);
}


/*
 * Function irda_create (sock, protocol)
 *
 *    Create IrDA socket
 *
 */
static int irda_create( struct socket *sock, int protocol)
{
	struct sock *sk;

	DEBUG( 0, __FUNCTION__ "()\n");
#if 0
	if (sock->type != SOCK_SEQPACKET || protocol != 0)
		return -ESOCKTNOSUPPORT;

	/* Allocate socket */
	if ((sk = sk_alloc( GFP_ATOMIC)) == NULL)
		return -ENOMEM;

	skb_queue_head_init(&sk->receive_queue);
	skb_queue_head_init(&sk->write_queue);
	skb_queue_head_init(&sk->back_log);

	init_timer( &sk->timer);

	sk->socket        = sock;
	sk->type          = sock->type;
	sk->protocol      = protocol;
	sk->allocation	  = GFP_KERNEL;
	sk->rcvbuf        = SK_RMEM_MAX;
	sk->sndbuf        = SK_WMEM_MAX;
	sk->state         = TCP_CLOSE;
	sk->priority      = SOPRI_NORMAL;
	sk->mtu           = 2048; /* FIXME, insert the right size*/
	sk->zapped        = 1;
/* 	sk->window	  = nr_default.window; */

	sk->state_change = def_callback1;
	sk->data_ready   = def_callback2;
	sk->write_space  = def_callback1;
	sk->error_report = def_callback1;

	if (sock != NULL) {
		sock->data = (void *)sk;
		sk->sleep  = sock->wait;
	}
#endif
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


/*
 * Function irda_dup (newsock, oldsock)
 *
 *    
 *
 */
static int irda_dup( struct socket *newsock, struct socket *oldsock)
{
#if 0
        struct sock *sk = (struct sock *)oldsock->data;

	DEBUG( 0, __FUNCTION__ "()\n");

        if (sk == NULL || newsock == NULL)
                return -EINVAL;

        return irda_create( newsock, sk->protocol);
#endif
	return 0;
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
	
}

/*
 * Function irda_ioctl (sock, cmd, arg)
 *
 *    
 *
 */
static int irda_ioctl( struct socket *sock, unsigned int cmd, 
		       unsigned long arg)
{
#if 0
        struct sock *sk = (struct sock *) sock->data;
        int err;

	DEBUG( 0, __FUNCTION__ "(), Not implemented!\n");
#endif
	return 0;
}

static int irda_setsockopt( struct socket *sock, int level, int optname,
			    char *optval, int optlen)
{
#if 0
        struct sock *sk = (struct sock *)sock->data;
        int err, opt;

	DEBUG( 0, __FUNCTION__ "()\n");

        if (level == SOL_SOCKET)
                return sock_setsockopt(sk, level, optname, optval, optlen);

        /* if (level != SOL_AX25) */
/*                 return -EOPNOTSUPP; */

        if (optval == NULL)
                return -EINVAL;

        if ((err = verify_area(VERIFY_READ, optval, sizeof(int))) != 0)
                return err;

        opt = get_fs_long((int *)optval);

        switch (optname) {  
	default:
		return -ENOPROTOOPT;
        }
#endif
	return -ENOPROTOOPT;
}

static int irda_getsockopt(struct socket *sock, int level, int optname,
        char *optval, int *optlen)
{
#if 0
        struct sock *sk = (struct sock *)sock->data;
        int val = 0;
        int err;

	DEBUG( 0, __FUNCTION__ "()\n");

        if (level == SOL_SOCKET)
                return sock_getsockopt(sk, level, optname, optval, optlen);

        /* if (level != SOL_AX25) */
/*                 return -EOPNOTSUPP; */

        switch (optname) {
	default:
		return -ENOPROTOOPT;
        }
	
        if ((err = verify_area(VERIFY_WRITE, optlen, sizeof(int))) != 0)
                return err;

        put_user(sizeof(int), optlen);
	
        if ((err = verify_area(VERIFY_WRITE, optval, sizeof(int))) != 0)
                return err;

        put_user(val, (int *)optval);
#endif
        return 0;
}

static int irda_fcntl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	DEBUG( 0, __FUNCTION__ "()\n");

        return -EINVAL;
}
/*
 * Function irda_rcv (skb, dev, dev_addr, ptype)
 *
 *    
 *
 */
static int irda_rcv( struct sk_buff *skb, struct device *dev, 
		     irda_address *dev_addr, struct packet_type *ptype)
{
	DEBUG( 0, __FUNCTION__ "()\n");

	return 0;
}

/*
 * Function irda_driver_rcv (skb, dev, ptype)
 *
 *    
 *
 */
static int irda_driver_rcv( struct sk_buff *skb, struct device *dev, 
			    struct packet_type *ptype)
{
        skb->sk = NULL;         /* Initially we don't know who it's for */
	
	DEBUG( 0, __FUNCTION__ "()\n");

#if 0
        if ((*skb->data & 0x0F) != 0) {
                kfree_skb(skb, FREE_READ);      /* Not a KISS data frame */
                return 0;
        }

        /* skb_pull(skb, AX25_KISS_HEADER_LEN);  */   /* Remove the KISS byte */

        return irda_rcv( skb, dev, (irda_address *)dev->dev_addr, ptype);
#endif
	return NULL;
}

static struct proto_ops irda_proto_ops = {
	AF_IRDA,
	
	irda_create,
	irda_dup,
	irda_release,
	irda_bind,
	irda_connect,
	irda_socketpair,
	irda_accept,
	irda_getname,
	irda_poll,
	irda_ioctl,
	irda_listen,
	irda_shutdown,
	irda_setsockopt,
	irda_getsockopt,
	irda_fcntl,
	irda_sendmsg,
	irda_recvmsg
};

#endif /* IRDA_SOCKETS */

static int irda_device_event( struct notifier_block *this, unsigned long event,
			      void *ptr)
{
	/* struct device *dev = (struct device *) ptr; */
	
	DEBUG( 0, __FUNCTION__ "()\n");
	
        /* Reject non AX.25 devices */
	/*  if (dev->type != ARPHRD_AX25) */
/*                 return NOTIFY_DONE; */
	
        switch (event) {
	case NETDEV_UP:
		/* ax25_dev_device_up(dev); */
		break;
	case NETDEV_DOWN:
		/* ax25_kill_by_device(dev); */
/*                         ax25_rt_device_down(dev); */
/*                         ax25_dev_device_down(dev); */
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
void irda_proto_init(struct net_proto *pro)
{
	DEBUG( 4, __FUNCTION__ "\n");

	/* sock_register( irda_proto_ops.family, &irda_proto_ops); */
	irda_packet_type.type = htons(ETH_P_IRDA);
        dev_add_pack(&irda_packet_type);

	/* register_netdevice_notifier( &irda_dev_notifier); */

	/* printk( KERN_INFO "IrDA Sockets for Linux (Dag Brattli)\n"); */
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

        /* unregister_netdevice_notifier( &irda_dev_notifier); */
	
	/* (void) sock_unregister( irda_proto_ops.family); */
	irda_cleanup();
	
        return;
}

