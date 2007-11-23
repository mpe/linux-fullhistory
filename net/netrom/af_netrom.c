/*
 *	NET/ROM release 003
 *
 *	This is ALPHA test software. This code may break your machine, randomly fail to work with new 
 *	releases, misbehave and/or generally screw up. It might even work. 
 *
 *	This code REQUIRES 1.3.0 or higher/ NET3.029
 *
 *	This module:
 *		This module is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	History
 *	NET/ROM 001	Jonathan(G4KLX)	Cloned from the AX25 code.
 *	NET/ROM 002	Darryl(G7LED)	Fixes and address enhancement.
 *			Jonathan(G4KLX)	Complete bind re-think.
 *			Alan(GW4PTS)	Trivial tweaks into new format.
 *
 *	To do:
 *		Fix non-blocking connect failure.
 *		Make it use normal SIOCADDRT/DELRT not funny node ioctl() calls.
 */
  
#include <linux/config.h>
#ifdef CONFIG_NETROM
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <net/ax25.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/termios.h>	/* For TIOCINQ/OUTQ */
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <net/netrom.h>

#include <net/ip.h>
#include <net/arp.h>

/************************************************************************\
*									*
*			Handlers for the socket list			*
*									*
\************************************************************************/

struct nr_parms_struct nr_default;

static unsigned short circuit = 0x101;

static struct sock *volatile nr_list = NULL;

/*
 *	Socket removal during an interrupt is now safe.
 */
static void nr_remove_socket(struct sock *sk)
{
	struct sock *s;
	unsigned long flags;
	
	save_flags(flags);
	cli();

	if ((s = nr_list) == sk) {
		nr_list = s->next;
		restore_flags(flags);
		return;
	}

	while (s != NULL && s->next != NULL) {
		if (s->next == sk) {
			s->next = sk->next;
			restore_flags(flags);
			return;
		}

		s = s->next;
	}

	restore_flags(flags);
}

/*
 *	Handle device status changes.
 */
static int nr_device_event(unsigned long event, void *ptr)
{
	if (event != NETDEV_DOWN)
		return NOTIFY_DONE;
		
	nr_rt_device_down(ptr);
	
	return NOTIFY_DONE;
}

/*
 *	Add a socket to the bound sockets list.
 */
static void nr_insert_socket(struct sock *sk)
{
	unsigned long flags;

	save_flags(flags);
	cli();

	sk->next = nr_list;
	nr_list  = sk;

	restore_flags(flags);
}

/*
 *	Find a socket that wants to accept the Connect Request we just
 *	received.
 */
static struct sock *nr_find_listener(ax25_address *addr, int type)
{
	unsigned long flags;
	struct sock *s;

	save_flags(flags);
	cli();

	for (s = nr_list; s != NULL; s = s->next) {
		if (ax25cmp(&s->nr->source_addr, addr) == 0 && s->type == type && s->state == TCP_LISTEN) {
			restore_flags(flags);
			return s;
		}
	}

	restore_flags(flags);
	return NULL;
}

/*
 *	Find a connected NET/ROM socket given my circuit IDs.
 */
static struct sock *nr_find_socket(unsigned char index, unsigned char id, int type)
{
	struct sock *s;
	unsigned long flags;

	save_flags(flags);
	cli();

	for (s = nr_list; s != NULL; s = s->next) {
		if (s->nr->my_index == index && s->nr->my_id == id && s->type == type) {
			restore_flags(flags);
			return s;
		}
	}

	restore_flags(flags);

	return NULL;
}

/*
 *	Find a connected NET/ROM socket given their circuit IDs.
 */
static struct sock *nr_find_peer(unsigned char index, unsigned char id, int type)
{
	struct sock *s;
	unsigned long flags;

	save_flags(flags);
	cli();

	for (s = nr_list; s != NULL; s = s->next) {
		if (s->nr->your_index == index && s->nr->your_id == id && s->type == type) {
			restore_flags(flags);
			return s;
		}
	}

	restore_flags(flags);

	return NULL;
}

/*
 *	Deferred destroy.
 */
void nr_destory_socket(struct sock *);

/*
 *	Handler for deferred kills.
 */
static void nr_destroy_timer(unsigned long data)
{
	nr_destroy_socket((struct sock *)data);
}

/*
 *	This is called from user mode and the timers. Thus it protects itself against
 *	interrupt users but doesn't worry about being called during work.
 *	Once it is removed from the queue no interrupt or bottom half will
 *	touch it and we are (fairly 8-) ) safe.
 */
void nr_destroy_socket(struct sock *sk)	/* Not static as its used by the timer */
{
	struct sk_buff *skb;
	unsigned long flags;
	
	save_flags(flags);
	cli();
	
	del_timer(&sk->timer);
	
	nr_remove_socket(sk);
	nr_clear_tx_queue(sk);	/* Flush the send queue */
	
	while ((skb = skb_dequeue(&sk->receive_queue)) != NULL) {
		if (skb->sk != sk) {			/* A pending connection */
			skb->sk->dead = 1;	/* Queue the unaccepted socket for death */
			nr_set_timer(skb->sk);
			skb->sk->nr->state = NR_STATE_0;
		}

		kfree_skb(skb, FREE_READ);
	}
	
	if (sk->wmem_alloc || sk->rmem_alloc) { /* Defer: outstanding buffers */
		init_timer(&sk->timer);
		sk->timer.expires  = 10 * HZ;
		sk->timer.function = nr_destroy_timer;
		sk->timer.data     = (unsigned long)sk;
		add_timer(&sk->timer);
	} else {
		kfree_s(sk->nr, sizeof(*sk->nr));
		kfree_s(sk, sizeof(*sk));
	}

	restore_flags(flags);
}

/*******************************************************************************************************************\
*													            *
* Handling for system calls applied via the various interfaces to a NET/ROM socket object		    	    *
*														    *
\*******************************************************************************************************************/
 
static int nr_fcntl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	switch(cmd)
	{
		default:
			return(-EINVAL);
	}
}

static int nr_setsockopt(struct socket *sock, int level, int optname,
	char *optval, int optlen)
{
	struct sock *sk;
	int err, opt;

	sk = (struct sock *)sock->data;
	
	if (level == SOL_SOCKET)
		return sock_setsockopt(sk, level, optname, optval, optlen);

	if (level != SOL_NETROM)
		return -EOPNOTSUPP;

	if (optval == NULL)
		return -EINVAL;

	if ((err = verify_area(VERIFY_READ, optval, sizeof(int))) != 0)
		return err;

	opt = get_fs_long((unsigned long *)optval);
	
	switch (optname) {
		case NETROM_T1:
			if (opt < 1)
				return -EINVAL;
			sk->nr->t1 = opt * PR_SLOWHZ;
			return 0;

		case NETROM_T2:
			if (opt < 1)
				return -EINVAL;
			sk->nr->t2 = opt * PR_SLOWHZ;
			return 0;
			
		case NETROM_N2:
			if (opt < 1 || opt > 31)
				return -EINVAL;
			sk->nr->n2 = opt;
			return 0;
			
		default:
			return -ENOPROTOOPT;
	}
}

static int nr_getsockopt(struct socket *sock, int level, int optname,
	char *optval, int *optlen)
{
	struct sock *sk;
	int val = 0;
	int err; 

	sk = (struct sock *)sock->data;
	
	if (level == SOL_SOCKET)
		return sock_getsockopt(sk, level, optname, optval, optlen);
	
	if (level != SOL_NETROM)
		return -EOPNOTSUPP;
	
	switch (optname) {
		case NETROM_T1:
			val = sk->nr->t1 / PR_SLOWHZ;
			break;
			
		case NETROM_T2:
			val = sk->nr->t2 / PR_SLOWHZ;
			break;
			
		case NETROM_N2:
			val = sk->nr->n2;
			break;
						
		default:
			return -ENOPROTOOPT;
	}

	if ((err = verify_area(VERIFY_WRITE, optlen, sizeof(int))) != 0)
		return err;

	put_fs_long(sizeof(int), (unsigned long *)optlen);

	if ((err = verify_area(VERIFY_WRITE, optval, sizeof(int))) != 0)
		return err;

	put_fs_long(val, (unsigned long *)optval);

	return 0;
}

static int nr_listen(struct socket *sock, int backlog)
{
	struct sock *sk = (struct sock *)sock->data;

	if (sk->type == SOCK_SEQPACKET && sk->state != TCP_LISTEN) {
		memset(&sk->nr->user_addr, '\0', sizeof(ax25_address));
		sk->max_ack_backlog = backlog;
		sk->state           = TCP_LISTEN;
		return 0;
	}

	return -EOPNOTSUPP;
}

static void def_callback1(struct sock *sk)
{
	if (!sk->dead)
		wake_up_interruptible(sk->sleep);
}

static void def_callback2(struct sock *sk, int len)
{
	if (!sk->dead)
		wake_up_interruptible(sk->sleep);
}

static int nr_create(struct socket *sock, int protocol)
{
	struct sock *sk;
	nr_cb *nr;

	if ((sk = (struct sock *)kmalloc(sizeof(*sk), GFP_ATOMIC)) == NULL)
		return -ENOMEM;

	if ((nr = (nr_cb *)kmalloc(sizeof(*nr), GFP_ATOMIC)) == NULL) {
		kfree_s(sk, sizeof(*sk));
		return -ENOMEM;
	}

	sk->type = sock->type;

	switch (sock->type) {
		case SOCK_SEQPACKET:
			break;
		default:
			kfree_s((void *)sk, sizeof(*sk));
			kfree_s((void *)nr, sizeof(*nr));
			return -ESOCKTNOSUPPORT;
	}

	skb_queue_head_init(&sk->receive_queue);
	skb_queue_head_init(&sk->write_queue);
	skb_queue_head_init(&sk->back_log);

	init_timer(&sk->timer);

	sk->socket        = sock;
	sk->protocol      = protocol;
	sk->dead          = 0;
	sk->next          = NULL;
	sk->broadcast     = 0;
	sk->rcvbuf        = SK_RMEM_MAX;
	sk->sndbuf        = SK_WMEM_MAX;
	sk->wmem_alloc    = 0;
	sk->rmem_alloc    = 0;
	sk->inuse         = 0;
	sk->debug         = 0;
	sk->prot          = NULL;	/* So we use default free mechanisms */
	sk->err           = 0;
	sk->localroute    = 0;
	sk->send_head     = NULL;
	sk->state         = TCP_CLOSE;
	sk->shutdown      = 0;
	sk->priority      = SOPRI_NORMAL;
	sk->ack_backlog   = 0;
	sk->mtu           = NETROM_MTU;	/* 236 */
	sk->zapped        = 1;
	sk->window	  = nr_default.window;

	sk->state_change = def_callback1;
	sk->data_ready   = def_callback2;
	sk->write_space  = def_callback1;
	sk->error_report = def_callback1;

	if (sock != NULL) {
		sock->data = (void *)sk;
		sk->sleep  = sock->wait;
	}

	skb_queue_head_init(&nr->ack_queue);
	skb_queue_head_init(&nr->reseq_queue);

	nr->my_index = 0;
	nr->my_id    = 0;
	nr->rtt      = nr_default.timeout;
	nr->t1       = nr_default.timeout;
	nr->t2       = nr_default.ack_delay;
	nr->n2       = nr_default.tries;

	nr->t1timer  = 0;
	nr->t2timer  = 0;
	nr->t4timer  = 0;
	nr->n2count  = 0;

	nr->va       = 0;
	nr->vr       = 0;
	nr->vs       = 0;
	nr->vl       = 0;

	nr->your_index = 0;
	nr->your_id    = 0;

	nr->my_index   = 0;
	nr->my_id      = 0;

	nr->state      = NR_STATE_0;

	memset(&nr->source_addr, '\0', sizeof(ax25_address));
	memset(&nr->user_addr,   '\0', sizeof(ax25_address));
	memset(&nr->dest_addr,   '\0', sizeof(ax25_address));

	nr->sk = sk;
	sk->nr = nr;

	return 0;
}

static struct sock *nr_make_new(struct sock *osk)
{
	struct sock *sk;
	nr_cb *nr;

	if ((sk = (struct sock *)kmalloc(sizeof(*sk), GFP_ATOMIC)) == NULL)
		return NULL;

	if ((nr = (nr_cb *)kmalloc(sizeof(*nr), GFP_ATOMIC)) == NULL) {
		kfree_s(sk, sizeof(*sk));
		return NULL;
	}

	sk->type   = osk->type;
	sk->socket = osk->socket;

	switch (osk->type) {
		case SOCK_SEQPACKET:
			break;
		default:
			kfree_s((void *)sk, sizeof(*sk));
			kfree_s((void *)nr, sizeof(*nr));
			return NULL;
	}

	skb_queue_head_init(&sk->receive_queue);
	skb_queue_head_init(&sk->write_queue);
	skb_queue_head_init(&sk->back_log);

	init_timer(&sk->timer);

	sk->rmem_alloc  = 0;
	sk->dead        = 0;
	sk->next        = NULL;
	sk->priority    = osk->priority;
	sk->broadcast   = 0;
	sk->protocol    = osk->protocol;
	sk->rcvbuf      = osk->rcvbuf;
	sk->sndbuf      = osk->sndbuf;
	sk->wmem_alloc  = 0;
	sk->rmem_alloc  = 0;
	sk->inuse       = 0;
	sk->ack_backlog = 0;
	sk->prot        = NULL;	/* So we use default free mechanisms */
	sk->err         = 0;
	sk->localroute  = 0;
	sk->send_head   = NULL;
	sk->debug       = osk->debug;
	sk->state       = TCP_ESTABLISHED;
	sk->window      = osk->window;
	sk->shutdown    = 0;
	sk->mtu         = osk->mtu;
	sk->sleep       = osk->sleep;
	sk->zapped      = osk->zapped;

	sk->state_change = def_callback1;
	sk->data_ready   = def_callback2;
	sk->write_space  = def_callback1;
	sk->error_report = def_callback1;

	skb_queue_head_init(&nr->ack_queue);
	skb_queue_head_init(&nr->reseq_queue);

	nr->rtt      = osk->nr->rtt;
	nr->t1       = osk->nr->t1;
	nr->t2       = osk->nr->t2;
	nr->n2       = osk->nr->n2;

	nr->t1timer  = 0;
	nr->t2timer  = 0;
	nr->t4timer  = 0;
	nr->n2count  = 0;

	nr->va       = 0;
	nr->vr       = 0;
	nr->vs       = 0;
	nr->vl       = 0;
	
	sk->nr = nr;
	nr->sk = sk;

	return sk;
}

static int nr_dup(struct socket *newsock, struct socket *oldsock)
{
	struct sock *sk = (struct sock *)oldsock->data;

	return nr_create(newsock, sk->protocol);
}

static int nr_release(struct socket *sock, struct socket *peer)
{
	struct sock *sk = (struct sock *)sock->data;

	if (sk == NULL) return 0;

	if (sk->type == SOCK_SEQPACKET) {
		switch (sk->nr->state) {
			case NR_STATE_0:
				sk->dead      = 1;
				sk->state_change(sk);
				nr_destroy_socket(sk);
				break;

			case NR_STATE_1:
				sk->nr->state = NR_STATE_0;
				sk->dead      = 1;
				sk->state_change(sk);
				nr_destroy_socket(sk);
				break;

			case NR_STATE_2:
				nr_write_internal(sk, NR_DISCACK);
				sk->nr->state = NR_STATE_0;
				sk->dead      = 1;
				sk->state_change(sk);
				nr_destroy_socket(sk);
				break;			

			case NR_STATE_3:
				nr_clear_tx_queue(sk);
				sk->nr->n2count = 0;
				nr_write_internal(sk, NR_DISCREQ);
				sk->nr->t1timer = sk->nr->t1 = nr_calculate_t1(sk);
				sk->nr->t2timer = 0;
				sk->nr->t4timer = 0;
				sk->nr->state   = NR_STATE_2;
				sk->state_change(sk);
				sk->dead        = 1;
				break;

			default:
				break;
		}
	} else {
		sk->dead = 1;
		sk->state_change(sk);
		nr_destroy_socket(sk);
	}

	sock->data = NULL;	

	return 0;
}

static int nr_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	struct sock *sk;
	struct full_sockaddr_ax25 *addr = (struct full_sockaddr_ax25 *)uaddr;
	ax25_address *user, *source;
	
	sk = (struct sock *)sock->data;

	if (sk->zapped == 0)
		return -EIO;
		
	if (addr_len != sizeof(struct sockaddr_ax25) && addr_len != sizeof(struct full_sockaddr_ax25))
		return -EINVAL;

#ifdef DONTDO
	if (nr_find_listener(&addr->fsa_ax25.sax25_call, sk->type) != NULL) {
		if (sk->debug)
			printk("NET/ROM: bind failed: in use\n");
		return -EADDRINUSE;
	}
#endif

	if (nr_dev_get(&addr->fsa_ax25.sax25_call) == NULL) {
		if (sk->debug)
			printk("NET/ROM: bind failed: invalid node callsign\n");
		return -EADDRNOTAVAIL;
	}

	/*
	 * Only the super user can set an arbitrary user callsign.
	 */
	if (addr->fsa_ax25.sax25_ndigis == 1) {
		if (!suser())
			return -EPERM;
		memcpy(&sk->nr->user_addr,   &addr->fsa_digipeater[0],   sizeof(ax25_address));
		memcpy(&sk->nr->source_addr, &addr->fsa_ax25.sax25_call, sizeof(ax25_address));
	} else {
		source = &addr->fsa_ax25.sax25_call;

		if ((user = ax25_findbyuid(current->euid)) == NULL) {
			if (ax25_uid_policy && !suser())
				return -EPERM;
			user = source;
		}

		memcpy(&sk->nr->user_addr,   user,   sizeof(ax25_address));
		memcpy(&sk->nr->source_addr, source, sizeof(ax25_address));
	}

	nr_insert_socket(sk);

	sk->zapped = 0;

	if (sk->debug)
		printk("NET/ROM: socket is bound\n");

	return 0;
}

static int nr_connect(struct socket *sock, struct sockaddr *uaddr,
	int addr_len, int flags)
{
	struct sock *sk = (struct sock *)sock->data;
	struct sockaddr_ax25 *addr = (struct sockaddr_ax25 *)uaddr;
	ax25_address *user, *source = NULL;
	struct device *dev;
	
	if (sk->state == TCP_ESTABLISHED && sock->state == SS_CONNECTING) {
		sock->state = SS_CONNECTED;
		return 0;	/* Connect completed during a ERESTARTSYS event */
	}
	
	if (sk->state == TCP_CLOSE && sock->state == SS_CONNECTING) {
		sock->state = SS_UNCONNECTED;
		return -ECONNREFUSED;
	}
	
	if (sk->state == TCP_ESTABLISHED && sk->type == SOCK_SEQPACKET)
		return -EISCONN;	/* No reconnect on a seqpacket socket */
		
	sk->state   = TCP_CLOSE;	
	sock->state = SS_UNCONNECTED;

	if (addr_len != sizeof(struct sockaddr_ax25))
		return -EINVAL;

	if ((dev = nr_dev_first()) == NULL)
		return -ENETUNREACH;
		
	if (sk->zapped) {	/* Must bind first - autobinding in this may or may not work */
		sk->zapped = 0;

		source = (ax25_address *)dev->dev_addr;

		if ((user = ax25_findbyuid(current->euid)) == NULL) {
			if (ax25_uid_policy && !suser())
				return -EPERM;
			user = source;
		}

		memcpy(&sk->nr->user_addr,   user,   sizeof(ax25_address));
		memcpy(&sk->nr->source_addr, source, sizeof(ax25_address));

		nr_insert_socket(sk);		/* Finish the bind */
	}
		
	memcpy(&sk->nr->dest_addr, &addr->sax25_call, sizeof(ax25_address));

	sk->nr->my_index = circuit / 256;
	sk->nr->my_id    = circuit % 256;

	circuit++;
	
	/* Move to connecting socket, start sending Connect Requests */
	sock->state   = SS_CONNECTING;
	sk->state     = TCP_SYN_SENT;
	nr_establish_data_link(sk);
	sk->nr->state = NR_STATE_1;
	nr_set_timer(sk);
	
	/* Now the loop */
	if (sk->state != TCP_ESTABLISHED && (flags & O_NONBLOCK))
		return -EINPROGRESS;
		
	cli();	/* To avoid races on the sleep */

	/*
	 * A Connect Ack with Choke or timeout or failed routing will go to closed.
	 */
	while (sk->state == TCP_SYN_SENT) {
		interruptible_sleep_on(sk->sleep);
		if (current->signal & ~current->blocked) {
			sti();
			return -ERESTARTSYS;
		}
	}

	if (sk->state != TCP_ESTABLISHED) {
		sti();
		sock->state = SS_UNCONNECTED;
		return -sk->err;	/* Always set at this point */
	}
	
	sock->state = SS_CONNECTED;

	sti();
	
	return 0;
}
	
static int nr_socketpair(struct socket *sock1, struct socket *sock2)
{
	return -EOPNOTSUPP;
}

static int nr_accept(struct socket *sock, struct socket *newsock, int flags)
{
	struct sock *sk;
	struct sock *newsk;
	struct sk_buff *skb;

	if (newsock->data)
		kfree_s(newsock->data, sizeof(struct sock));

	newsock->data = NULL;
	
	sk = (struct sock *)sock->data;

	if (sk->type != SOCK_SEQPACKET)
		return -EOPNOTSUPP;
	
	if (sk->state != TCP_LISTEN)
		return -EINVAL;
		
	/* The write queue this time is holding sockets ready to use
	   hooked into the SABM we saved */
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

	return 0;
}

static int nr_getname(struct socket *sock, struct sockaddr *uaddr,
	int *uaddr_len, int peer)
{
	struct full_sockaddr_ax25 *sax = (struct full_sockaddr_ax25 *)uaddr;
	struct sock *sk;
	
	sk = (struct sock *)sock->data;
	
	if (peer != 0) {
		if (sk->state != TCP_ESTABLISHED)
			return -ENOTCONN;
		sax->fsa_ax25.sax25_family = AF_NETROM;
		sax->fsa_ax25.sax25_ndigis = 1;
		memcpy(&sax->fsa_ax25.sax25_call, &sk->nr->user_addr, sizeof(ax25_address));
		memcpy(&sax->fsa_digipeater[0],   &sk->nr->dest_addr, sizeof(ax25_address));
		*uaddr_len = sizeof(struct sockaddr_ax25) + sizeof(ax25_address);
	} else {
		sax->fsa_ax25.sax25_family = AF_NETROM;
		sax->fsa_ax25.sax25_ndigis = 0;
		memcpy(&sax->fsa_ax25.sax25_call, &sk->nr->source_addr, sizeof(ax25_address));
		*uaddr_len = sizeof(struct sockaddr_ax25);
	}

	return 0;
}
 
int nr_rx_frame(struct sk_buff *skb, struct device *dev)
{
	struct sock *sk;
	struct sock *make;	
	ax25_address *src, *dest, *user;
	unsigned short circuit_index, circuit_id;
	unsigned short frametype, window;

	skb->sk = NULL;		/* Initially we don't know who its for */
	
	src  = (ax25_address *)(skb->data + 17);
	dest = (ax25_address *)(skb->data + 24);

	circuit_index = skb->data[32];
	circuit_id    = skb->data[33];
	frametype     = skb->data[36];

#ifdef CONFIG_INET
	/*
	 * Check for an incoming IP over NET/ROM frame.
	 */
	 if ((frametype & 0x0F) == NR_PROTOEXT && circuit_index == NR_PROTO_IP && circuit_id == NR_PROTO_IP) {
	 	skb->h.raw = skb->data + 37;

		return nr_rx_ip(skb, dev);
	 }
#endif

	/*
	 * Find an existing socket connection, based on circuit ID, if its
	 * a Connect Request base it on their circuit ID.
	 */
	if (((frametype & 0x0F) != NR_CONNREQ && (sk = nr_find_socket(circuit_index, circuit_id, SOCK_SEQPACKET)) != NULL) ||
	    ((frametype & 0x0F) == NR_CONNREQ && (sk = nr_find_peer(circuit_index, circuit_id, SOCK_SEQPACKET)) != NULL)) {
		skb->h.raw = skb->data + 37;
		skb->len  -= 20;

		return nr_process_rx_frame(sk, skb);
	}

	if ((frametype & 0x0F) != NR_CONNREQ)
		return 0;
		
	sk = nr_find_listener(dest, SOCK_SEQPACKET);

	if (sk == NULL || sk->ack_backlog == sk->max_ack_backlog || (make = nr_make_new(sk)) == NULL) {
		nr_transmit_dm(skb);
		return 0;
	}

	user   = (ax25_address *)(skb->data + 38);
	window = skb->data[37];

	skb->sk             = make;
	make->state         = TCP_ESTABLISHED;

	/* Fill in his circuit details */
	memcpy(&make->nr->source_addr, dest, sizeof(ax25_address));
	memcpy(&make->nr->dest_addr,   src,  sizeof(ax25_address));
	memcpy(&make->nr->user_addr,   user, sizeof(ax25_address));
		
	make->nr->your_index = circuit_index;
	make->nr->your_id    = circuit_id;

	make->nr->my_index   = circuit / 256;
	make->nr->my_id      = circuit % 256;
	
	circuit++;

	/* Window negotiation */
	if (window < make->window)
		make->window = window;

	nr_write_internal(make, NR_CONNACK);

	make->nr->condition = 0x00;
	make->nr->vs        = 0;
	make->nr->va        = 0;
	make->nr->vr        = 0;
	make->nr->vl        = 0;
	make->nr->state     = NR_STATE_3;
	sk->ack_backlog++;
	make->pair = sk;

	nr_insert_socket(make);

	skb_queue_head(&sk->receive_queue, skb);

	nr_set_timer(make);

	if (!sk->dead)
		sk->data_ready(sk, skb->len);

	return 1;
}

static int nr_sendto(struct socket *sock, void *ubuf, int len, int noblock,
	unsigned flags, struct sockaddr *usip, int addr_len)
{
	struct sock *sk = (struct sock *)sock->data;
	struct sockaddr_ax25 *usax = (struct sockaddr_ax25 *)usip;
	int err;
	struct sockaddr_ax25 sax;
	struct sk_buff *skb;
	unsigned char *asmptr;
	int size;
	
	if (sk->err) {
		err     = sk->err;
		sk->err = 0;
		return -err;
	}

	if (flags)
		return -EINVAL;

	if (sk->zapped)
		return -EADDRNOTAVAIL;
		
	if (usax) {
		if (addr_len < sizeof(sax))
			return -EINVAL;
		memcpy(&sax, usax, sizeof(sax));
		if (sk->type == SOCK_SEQPACKET && memcmp(&sk->nr->dest_addr, &sax.sax25_call, sizeof(ax25_address)) != 0)
			return -EISCONN;
		if (sax.sax25_family != AF_NETROM)
			return -EINVAL;
	} else {
		if (sk->state != TCP_ESTABLISHED)
			return -ENOTCONN;
		sax.sax25_family = AF_NETROM;
		memcpy(&sax.sax25_call, &sk->nr->dest_addr, sizeof(ax25_address));
	}
	
	if (sk->debug)
		printk("NET/ROM: sendto: Addresses built.\n");

	/* Build a packet */
	if (sk->debug)
		printk("NET/ROM: sendto: building packet.\n");

	size = len + 37;

	if ((skb = sock_alloc_send_skb(sk, size, 0, &err)) == NULL)
		return err;

	skb->sk   = sk;
	skb->free = 1;
	skb->arp  = 1;
	skb->len  = size;
	
	asmptr = skb->data + 16;

	if (sk->debug)
		printk("Building NET/ROM Header.\n");

	/* Build a NET/ROM Network header */

	*asmptr++ = AX25_P_NETROM;

	memcpy(asmptr, &sk->nr->source_addr, sizeof(ax25_address));
	asmptr[6] &= ~LAPB_C;
	asmptr[6] &= ~LAPB_E;
	asmptr[6] |= SSID_SPARE;
	asmptr += 7;

	memcpy(asmptr, &sax.sax25_call, sizeof(ax25_address));
	asmptr[6] &= ~LAPB_C;
	asmptr[6] |= LAPB_E;
	asmptr[6] |= SSID_SPARE;
	asmptr += 7;
	
	*asmptr++ = nr_default.ttl;

	/* Build a NET/ROM Transport header */

	*asmptr++ = sk->nr->your_index;
	*asmptr++ = sk->nr->your_id;
	*asmptr++ = 0;		/* To be filled in later */
	*asmptr++ = 0;		/*      Ditto            */
	*asmptr++ = NR_INFO;
	
	if (sk->debug)
		printk("Built header.\n");

	skb->h.raw = asmptr;
	
	if (sk->debug)
		printk("NET/ROM: Appending user data\n");

	/* User data follows immediately after the NET/ROM transport header */
	memcpy_fromfs(asmptr, ubuf, len);

	if (sk->debug)
		printk("NET/ROM: Transmitting buffer\n");

	if (sk->state != TCP_ESTABLISHED) {
		kfree_skb(skb, FREE_WRITE);
		return -ENOTCONN;
	}

	nr_output(sk, skb);	/* Shove it onto the queue */

	return len;
}

static int nr_send(struct socket *sock, void *ubuf, int size, int noblock, unsigned flags)
{
	return nr_sendto(sock, ubuf, size, noblock, flags, NULL, 0);
}

static int nr_write(struct socket *sock, char *ubuf, int size, int noblock)
{
	return nr_send(sock, ubuf, size, noblock, 0);
}

static int nr_recvfrom(struct socket *sock, void *ubuf, int size, int noblock,
		   unsigned flags, struct sockaddr *sip, int *addr_len)
{
	struct sock *sk = (struct sock *)sock->data;
	struct sockaddr_ax25 *sax = (struct sockaddr_ax25 *)sip;
	int copied = 0;
	struct sk_buff *skb;
	int er;

	if (sk->err) {
		er      = -sk->err;
		sk->err = 0;
		return er;
	}
	
	if (addr_len != NULL)
		*addr_len = sizeof(*sax);

	/* This works for seqpacket too. The receiver has ordered the queue for us! We do one quick check first though */
	if (sk->type == SOCK_SEQPACKET && sk->state != TCP_ESTABLISHED)
		return -ENOTCONN;

	/* Now we can treat all alike */
	if ((skb = skb_recv_datagram(sk, flags, noblock, &er)) == NULL)
		return er;

	copied = (size < skb->len) ? size : skb->len;

	skb_copy_datagram(skb, 0, ubuf, copied);
	
	if (sax != NULL) {
		struct sockaddr_ax25 addr;
		
		addr.sax25_family = AF_NETROM;
		memcpy(&addr.sax25_call, skb->data + 24, sizeof(ax25_address));

		memcpy(sax, &addr, sizeof(*sax));

		*addr_len = sizeof(*sax);
	}

	skb_free_datagram(skb);

	return copied;
}		

static int nr_recv(struct socket *sock, void *ubuf, int size , int noblock,
	unsigned flags)
{
	struct sock *sk = (struct sock *)sock->data;

	if (sk->zapped)
		return -ENOTCONN;

	return nr_recvfrom(sock, ubuf, size, noblock, flags, NULL, NULL);
}

static int nr_read(struct socket *sock, char *ubuf, int size, int noblock)
{
	return nr_recv(sock, ubuf, size, noblock, 0);
}

static int nr_shutdown(struct socket *sk, int how)
{
	return -EOPNOTSUPP;
}

static int nr_select(struct socket *sock , int sel_type, select_table *wait)
{
	struct sock *sk = (struct sock *)sock->data;

	return datagram_select(sk, sel_type, wait);
}

static int nr_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	struct sock *sk = (struct sock *)sock->data;
	int err;
	long amount = 0;

	switch (cmd) {
		case TIOCOUTQ:
			if ((err = verify_area(VERIFY_WRITE, (void *)arg, sizeof(unsigned long))) != 0)
				return err;
			amount = sk->sndbuf - sk->wmem_alloc;
			if (amount < 0)
				amount = 0;
			put_fs_long(amount, (unsigned long *)arg);
			return 0;

		case TIOCINQ:
		{
			struct sk_buff *skb;
			/* These two are safe on a single CPU system as only user tasks fiddle here */
			if ((skb = skb_peek(&sk->receive_queue)) != NULL)
				amount = skb->len;
			if ((err = verify_area(VERIFY_WRITE, (void *)arg, sizeof(unsigned long))) != 0)
				return err;
			put_fs_long(amount, (unsigned long *)arg);
			return 0;
		}

		case SIOCGSTAMP:
			if (sk != NULL) {
				if (sk->stamp.tv_sec==0)
					return -ENOENT;
				if ((err = verify_area(VERIFY_WRITE,(void *)arg,sizeof(struct timeval))) != 0)
					return err;
				memcpy_tofs((void *)arg, &sk->stamp, sizeof(struct timeval));
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

		case SIOCNRADDNODE:
		case SIOCNRDELNODE:
		case SIOCNRADDNEIGH:
		case SIOCNRDELNEIGH:
		case SIOCNRDECOBS:
			if (!suser()) return -EPERM;
			return nr_rt_ioctl(cmd, (void *)arg);

		case SIOCNRGETPARMS:
		{
			struct nr_parms_struct nr_parms;
			if ((err = verify_area(VERIFY_WRITE, (void *)arg, sizeof(struct nr_parms_struct))) != 0)
				return err;
			memcpy_fromfs(&nr_parms, (void *)arg, sizeof(struct nr_parms_struct));
			nr_parms = nr_default;
			memcpy_tofs((void *)arg, &nr_parms, sizeof(struct nr_parms_struct));
			return 0;
		}

		case SIOCNRSETPARMS:
		{
			struct nr_parms_struct nr_parms;
			if (!suser()) return -EPERM;
			if ((err = verify_area(VERIFY_READ, (void *)arg, sizeof(struct nr_parms_struct))) != 0)
				return err;
			memcpy_fromfs(&nr_parms, (void *)arg, sizeof(struct nr_parms_struct));
			nr_default = nr_parms;
			return 0;
		}
		
		default:
			return dev_ioctl(cmd, (void *)arg);
	}

	/*NOTREACHED*/
	return(0);
}

int nr_get_info(char *buffer, char **start, off_t offset, int length)
{
	struct sock *s;
	int len = 0;
	off_t pos = 0;
	off_t begin = 0;
  
	cli();

	len += sprintf(buffer, "user_addr dest_node src_node    my  your  st vs vr va    t1     t2    n2  rtt wnd Snd-Q Rcv-Q\n");

	for (s = nr_list; s != NULL; s = s->next) {
		len += sprintf(buffer + len, "%-9s ",
			ax2asc(&s->nr->user_addr));
		len += sprintf(buffer + len, "%-9s ",
			ax2asc(&s->nr->dest_addr));
		len += sprintf(buffer + len, "%-9s %02X/%02X %02X/%02X %2d %2d %2d %2d %3d/%03d %2d/%02d %2d/%02d %3d %3d %5ld %5ld\n",
			ax2asc(&s->nr->source_addr),
			s->nr->my_index, s->nr->my_id,
			s->nr->your_index, s->nr->your_id,
			s->nr->state,
			s->nr->vs, s->nr->vr, s->nr->va,
			s->nr->t1timer / PR_SLOWHZ,
			s->nr->t1      / PR_SLOWHZ,
			s->nr->t2timer / PR_SLOWHZ,
			s->nr->t2      / PR_SLOWHZ,
			s->nr->n2count, s->nr->n2,
			s->nr->rtt     / PR_SLOWHZ,
			s->window,
			s->wmem_alloc, s->rmem_alloc);
		
		pos = begin + len;

		if (pos < offset) {
			len   = 0;
			begin = pos;
		}
		
		if (pos > offset + length)
			break;
	}

	sti();

	*start = buffer + (offset - begin);
	len   -= (offset - begin);

	if (len > length) len = length;

	return(len);
} 

static struct proto_ops nr_proto_ops = {
	AF_NETROM,
	
	nr_create,
	nr_dup,
	nr_release,
	nr_bind,
	nr_connect,
	nr_socketpair,
	nr_accept,
	nr_getname,
	nr_read,
	nr_write,
	nr_select,
	nr_ioctl,
	nr_listen,
	nr_send,
	nr_recv,
	nr_sendto,
	nr_recvfrom,
	nr_shutdown,
	nr_setsockopt,
	nr_getsockopt,
	nr_fcntl,
};

static struct notifier_block nr_dev_notifier = {
	nr_device_event,
	0
};

void nr_proto_init(struct net_proto *pro)
{
	sock_register(nr_proto_ops.family, &nr_proto_ops);
	register_netdevice_notifier(&nr_dev_notifier);
	printk("G4KLX NET/ROM for Linux. Version 0.2 ALPHA for AX.25 029 for Linux 1.3.0\n");

	nr_default.quality    = NR_DEFAULT_QUAL;
	nr_default.obs_count  = NR_DEFAULT_OBS;
	nr_default.ttl        = NR_DEFAULT_TTL;
	nr_default.timeout    = NR_DEFAULT_T1;
	nr_default.ack_delay  = NR_DEFAULT_T2;
	nr_default.busy_delay = NR_DEFAULT_T4;
	nr_default.tries      = NR_DEFAULT_N2;
	nr_default.window     = NR_DEFAULT_WINDOW;
}

#endif
