/*
 *	X.25 Packet Layer release 001
 *
 *	This is ALPHA test software. This code may break your machine, randomly fail to work with new 
 *	releases, misbehave and/or generally screw up. It might even work. 
 *
 *	This code REQUIRES 2.1.15 or higher
 *
 *	This module:
 *		This module is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	History
 *	X.25 001	Jonathan Naylor	Started coding.
 */
  
#include <linux/config.h>
#if defined(CONFIG_X25) || defined(CONFIG_X25_MODULE)
#include <linux/module.h>
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
#include <linux/stat.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <linux/fcntl.h>
#include <linux/termios.h>	/* For TIOCINQ/OUTQ */
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <linux/proc_fs.h>
#include <linux/if_arp.h>
#include <net/x25.h>

int sysctl_x25_restart_request_timeout = X25_DEFAULT_T20;
int sysctl_x25_call_request_timeout    = X25_DEFAULT_T21;
int sysctl_x25_reset_request_timeout   = X25_DEFAULT_T22;
int sysctl_x25_clear_request_timeout   = X25_DEFAULT_T23;
int sysctl_x25_ack_holdback_timeout    = X25_DEFAULT_T2;

static unsigned int lci = 1;

struct proto_ops x25_proto_ops;

static struct sock *volatile x25_list = NULL;

int x25_addr_ntoa(unsigned char *p, x25_address *called_addr, x25_address *calling_addr)
{
	int called_len, calling_len;
	char *called, *calling;
	int i;

	called_len  = (*p >> 0) & 0x0F;
	calling_len = (*p >> 4) & 0x0F;

	called  = called_addr->x25_addr;
	calling = calling_addr->x25_addr;
	p++;

	for (i = 0; i < (called_len + calling_len); i++) {
		if (i < called_len) {
			if (i % 2 != 0) {
				*called++ = ((*p >> 0) & 0x0F) + '0';
				p++;
			} else {
				*called++ = ((*p >> 4) & 0x0F) + '0';
			}
		} else {
			if (i % 2 != 0) {
				*calling++ = ((*p >> 0) & 0x0F) + '0';
				p++;
			} else {
				*calling++ = ((*p >> 4) & 0x0F) + '0';
			}
		}
	}

	*called  = '\0';
	*calling = '\0';

	return 1 + (called_len + calling_len + 1) / 2;
}

int x25_addr_aton(unsigned char *p, x25_address *called_addr, x25_address *calling_addr)
{
	unsigned int called_len, calling_len;
	char *called, *calling;
	int i;

	called  = called_addr->x25_addr;
	calling = calling_addr->x25_addr;

	called_len  = strlen(called);
	calling_len = strlen(calling);
	
	*p++ = (calling_len << 4) | (called_len << 0);
	
	for (i = 0; i < (called_len + calling_len); i++) {
		if (i < called_len) {
			if (i % 2 != 0) {
				*p |= (*called++ - '0') << 0;
				p++;
				*p = 0x00;
			} else {
				*p |= (*called++ - '0') << 4;
			}
		} else {
			if (i % 2 != 0) {
				*p |= (*calling++ - '0') << 0;
				p++;
				*p = 0x00;
			} else {
				*p |= (*calling++ - '0') << 4;
			}
		}
	}
	
	return 1 + (called_len + calling_len + 1) / 2;
}

/*
 *	Socket removal during an interrupt is now safe.
 */
static void x25_remove_socket(struct sock *sk)
{
	struct sock *s;
	unsigned long flags;
	
	save_flags(flags);
	cli();

	if ((s = x25_list) == sk) {
		x25_list = s->next;
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
 *	Kill all bound sockets on a dropped device.
 */
static void x25_kill_by_device(struct device *dev)
{
	struct sock *s;
	
	for (s = x25_list; s != NULL; s = s->next) {
		if (s->protinfo.x25->neighbour->dev == dev) {
			s->protinfo.x25->state  = X25_STATE_0;
			s->state                = TCP_CLOSE;
			s->err                  = ENETUNREACH;
			s->shutdown            |= SEND_SHUTDOWN;
			s->state_change(s);
			s->dead                 = 1;
		}
	}
}

/*
 *	Handle device status changes.
 */
static int x25_device_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct device *dev = (struct device *)ptr;

	if (dev->type == ARPHRD_X25 || dev->type == ARPHRD_ETHER) {
		switch (event) {
			case NETDEV_UP:
				x25_link_device_up(dev);
				break;
			case NETDEV_DOWN:
				x25_kill_by_device(dev);
				x25_route_device_down(dev);
				x25_link_device_down(dev);
				break;
		}
	}

	return NOTIFY_DONE;
}

/*
 *	Add a socket to the bound sockets list.
 */
static void x25_insert_socket(struct sock *sk)
{
	unsigned long flags;

	save_flags(flags);
	cli();

	sk->next = x25_list;
	x25_list  = sk;

	restore_flags(flags);
}

/*
 *	Find a socket that wants to accept the Call Request we just
 *	received.
 */
static struct sock *x25_find_listener(x25_address *addr)
{
	unsigned long flags;
	struct sock *s;

	save_flags(flags);
	cli();

	for (s = x25_list; s != NULL; s = s->next) {
		if (strcmp(s->protinfo.x25->source_addr.x25_addr, addr->x25_addr) == 0 && s->state == TCP_LISTEN) {
			restore_flags(flags);
			return s;
		}
	}

	restore_flags(flags);
	return NULL;
}

/*
 *	Find a connected X.25 socket given my LCI.
 */
struct sock *x25_find_socket(unsigned int lci)
{
	struct sock *s;
	unsigned long flags;

	save_flags(flags);
	cli();

	for (s = x25_list; s != NULL; s = s->next) {
		if (s->protinfo.x25->lci == lci) {
			restore_flags(flags);
			return s;
		}
	}

	restore_flags(flags);
	return NULL;
}

/*
 *	Find a unique LCI for a given device.
 */
unsigned int x25_new_lci(void)
{
	lci++;
	if (lci > 4095) lci = 1;

	while (x25_find_socket(lci) != NULL) {
		lci++;
		if (lci > 4095) lci = 1;
	}

	return lci;
}

/*
 *	Deferred destroy.
 */
void x25_destroy_socket(struct sock *);

/*
 *	Handler for deferred kills.
 */
static void x25_destroy_timer(unsigned long data)
{
	x25_destroy_socket((struct sock *)data);
}

/*
 *	This is called from user mode and the timers. Thus it protects itself against
 *	interrupt users but doesn't worry about being called during work.
 *	Once it is removed from the queue no interrupt or bottom half will
 *	touch it and we are (fairly 8-) ) safe.
 */
void x25_destroy_socket(struct sock *sk)	/* Not static as it's used by the timer */
{
	struct sk_buff *skb;
	unsigned long flags;
	
	save_flags(flags);
	cli();
	
	del_timer(&sk->timer);
	
	x25_remove_socket(sk);
	x25_clear_queues(sk);		/* Flush the queues */
	
	while ((skb = skb_dequeue(&sk->receive_queue)) != NULL) {
		if (skb->sk != sk) {		/* A pending connection */
			skb->sk->dead = 1;	/* Queue the unaccepted socket for death */
			x25_set_timer(skb->sk);
			skb->sk->protinfo.x25->state = X25_STATE_0;
		}

		kfree_skb(skb, FREE_READ);
	}
	
	if (sk->wmem_alloc || sk->rmem_alloc) { /* Defer: outstanding buffers */
		init_timer(&sk->timer);
		sk->timer.expires  = jiffies + 10 * HZ;
		sk->timer.function = x25_destroy_timer;
		sk->timer.data     = (unsigned long)sk;
		add_timer(&sk->timer);
	} else {
		kfree_s(sk->protinfo.x25, sizeof(*sk->protinfo.x25));
		sk_free(sk);
		MOD_DEC_USE_COUNT;
	}

	restore_flags(flags);
}

/*
 *	Handling for system calls applied via the various interfaces to a
 *	X.25 socket object.
 */
 
static int x25_fcntl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	return -EINVAL;
}

static int x25_setsockopt(struct socket *sock, int level, int optname,
	char *optval, int optlen)
{
	struct sock *sk;
	int err, opt;

	sk = (struct sock *)sock->sk;
	
	if (level != SOL_X25)
		return -EOPNOTSUPP;

	if (optval == NULL)
		return -EINVAL;

	if ((err = verify_area(VERIFY_READ, optval, sizeof(int))) != 0)
		return err;

	get_user(opt, (int *)optval);
	
	switch (optname) {
		case X25_QBITINCL:
			sk->protinfo.x25->qbitincl = opt ? 1 : 0;
			return 0;

		case X25_PACKET_SIZE:
			if (sk->state != TCP_LISTEN)
				return -EINVAL;
			if (opt < X25_PS16 || opt > X25_PS4096)
				return -EINVAL;
			sk->protinfo.x25->facilities.packet_size = opt;
			return 0;

		case X25_WINDOW_SIZE:
			if (sk->state != TCP_LISTEN)
				return -EINVAL;
			if (sk->protinfo.x25->neighbour->extended) {
				if (opt < 1 || opt > 127)
					return -EINVAL;
			} else {
				if (opt < 1 || opt > 7)
					return -EINVAL;
			}
			sk->protinfo.x25->facilities.window_size = opt;
			return 0;

		case X25_THROUGHPUT_SPEED:
			if (sk->state != TCP_LISTEN)
				return -EINVAL;
			if (opt < 0x03 || opt > 0x2C)
				return -EINVAL;
			sk->protinfo.x25->facilities.throughput = opt;
			return 0;

		case X25_REVERSE_CHARGE:
			if (sk->state != TCP_LISTEN)
				return -EINVAL;
			if (opt != 0 && opt != 1)
				return -EINVAL;
			sk->protinfo.x25->facilities.reverse = opt;
			return 0;

		default:
			return -ENOPROTOOPT;
	}
}

static int x25_getsockopt(struct socket *sock, int level, int optname,
	char *optval, int *optlen)
{
	struct sock *sk;
	int val = 0;
	int err; 

	sk = (struct sock *)sock->sk;
	
	if (level != SOL_X25)
		return -EOPNOTSUPP;
	
	switch (optname) {
		case X25_QBITINCL:
			val = sk->protinfo.x25->qbitincl;
			break;

		case X25_PACKET_SIZE:
			val = sk->protinfo.x25->facilities.packet_size;
			break;
			
		case X25_WINDOW_SIZE:
			val = sk->protinfo.x25->facilities.window_size;
			break;

		case X25_THROUGHPUT_SPEED:
			val = sk->protinfo.x25->facilities.throughput;
			break;

		case X25_REVERSE_CHARGE:
			val = sk->protinfo.x25->facilities.reverse;
			break;

		default:
			return -ENOPROTOOPT;
	}

	if ((err = verify_area(VERIFY_WRITE, optlen, sizeof(int))) != 0)
		return err;

	put_user(sizeof(int), (unsigned long *)optlen);

	if ((err = verify_area(VERIFY_WRITE, optval, sizeof(int))) != 0)
		return err;

	put_user(val, (unsigned long *)optval);

	return 0;
}

static int x25_listen(struct socket *sock, int backlog)
{
	struct sock *sk = (struct sock *)sock->sk;

	if (sk->state != TCP_LISTEN) {
		memset(&sk->protinfo.x25->dest_addr, '\0', X25_ADDR_LEN);
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

static struct sock *x25_alloc_socket(void)
{
	struct sock *sk;
	x25_cb *x25;

	if ((sk = sk_alloc(GFP_ATOMIC)) == NULL)
		return NULL;

	if ((x25 = (x25_cb *)kmalloc(sizeof(*x25), GFP_ATOMIC)) == NULL) {
		sk_free(sk);
		return NULL;
	}

	x25->sk          = sk;
	sk->protinfo.x25 = x25;

	MOD_INC_USE_COUNT;

	skb_queue_head_init(&sk->receive_queue);
	skb_queue_head_init(&sk->write_queue);
	skb_queue_head_init(&sk->back_log);

	init_timer(&sk->timer);

	sk->state_change = def_callback1;
	sk->data_ready   = def_callback2;
	sk->write_space  = def_callback1;
	sk->error_report = def_callback1;

	skb_queue_head_init(&x25->ack_queue);
	skb_queue_head_init(&x25->fragment_queue);
	skb_queue_head_init(&x25->interrupt_queue);

	x25->condition = 0x00;
	x25->timer     = 0;

	x25->va    = 0;
	x25->vr    = 0;
	x25->vs    = 0;
	x25->vl    = 0;

	return sk;
}

static int x25_create(struct socket *sock, int protocol)
{
	struct sock *sk;
	x25_cb *x25;

	if (sock->type != SOCK_SEQPACKET || protocol != 0)
		return -ESOCKTNOSUPPORT;

	if ((sk = x25_alloc_socket()) == NULL)
		return -ENOMEM;

	x25 = sk->protinfo.x25;

	sock->ops         = &x25_proto_ops;

	sk->socket        = sock;
	sk->type          = sock->type;
	sk->protocol      = protocol;
	sk->allocation	  = GFP_KERNEL;
	sk->rcvbuf        = SK_RMEM_MAX;
	sk->sndbuf        = SK_WMEM_MAX;
	sk->state         = TCP_CLOSE;
	sk->priority      = SOPRI_NORMAL;
	sk->mtu           = X25_DEFAULT_PACKET_SIZE;	/* X25_PS128 */
	sk->zapped        = 1;

	if (sock != NULL) {
		sock->sk  = sk;
		sk->sleep = &sock->wait;
	}

	x25->lci      = 0;

	x25->t21      = sysctl_x25_call_request_timeout;
	x25->t22      = sysctl_x25_reset_request_timeout;
	x25->t23      = sysctl_x25_clear_request_timeout;
	x25->t2       = sysctl_x25_ack_holdback_timeout;

	x25->fraglen     = 0;
	x25->qbitincl    = 0;
	x25->intflg      = 0;
	x25->state       = X25_STATE_0;

	x25->facilities.window_size = X25_DEFAULT_WINDOW_SIZE;
	x25->facilities.packet_size = X25_DEFAULT_PACKET_SIZE;
	x25->facilities.throughput  = X25_DEFAULT_THROUGHPUT;
	x25->facilities.reverse     = X25_DEFAULT_REVERSE;

	x25->neighbour   = NULL;

	memset(&x25->source_addr, '\0', X25_ADDR_LEN);
	memset(&x25->dest_addr,   '\0', X25_ADDR_LEN);

	return 0;
}

static struct sock *x25_make_new(struct sock *osk)
{
	struct sock *sk;
	x25_cb *x25;

	if (osk->type != SOCK_SEQPACKET)
		return NULL;

	if ((sk = x25_alloc_socket()) == NULL)
		return NULL;

	x25 = sk->protinfo.x25;

	sk->type        = osk->type;
	sk->socket      = osk->socket;
	sk->priority    = osk->priority;
	sk->protocol    = osk->protocol;
	sk->rcvbuf      = osk->rcvbuf;
	sk->sndbuf      = osk->sndbuf;
	sk->debug       = osk->debug;
	sk->state       = TCP_ESTABLISHED;
	sk->mtu         = osk->mtu;
	sk->sleep       = osk->sleep;
	sk->zapped      = osk->zapped;

	x25->t21        = osk->protinfo.x25->t21;
	x25->t22        = osk->protinfo.x25->t22;
	x25->t23        = osk->protinfo.x25->t23;
	x25->t2         = osk->protinfo.x25->t2;

	x25->facilities = osk->protinfo.x25->facilities;

	x25->qbitincl    = osk->protinfo.x25->qbitincl;
	x25->intflg      = 0;
	x25->fraglen     = 0;

	return sk;
}

static int x25_dup(struct socket *newsock, struct socket *oldsock)
{
	struct sock *sk = (struct sock *)oldsock->sk;

	return x25_create(newsock, sk->protocol);
}

static int x25_release(struct socket *sock, struct socket *peer)
{
	struct sock *sk = (struct sock *)sock->sk;

	if (sk == NULL) return 0;

	switch (sk->protinfo.x25->state) {

		case X25_STATE_0:
			sk->state     = TCP_CLOSE;
			sk->shutdown |= SEND_SHUTDOWN;
			sk->state_change(sk);
			sk->dead      = 1;
			x25_destroy_socket(sk);
			break;

		case X25_STATE_1:
			sk->protinfo.x25->state = X25_STATE_0;
			sk->state               = TCP_CLOSE;
			sk->shutdown           |= SEND_SHUTDOWN;
			sk->state_change(sk);
			sk->dead                = 1;
			x25_destroy_socket(sk);
			break;

		case X25_STATE_2:
			sk->protinfo.x25->state = X25_STATE_0;
			sk->state               = TCP_CLOSE;
			sk->shutdown           |= SEND_SHUTDOWN;
			sk->state_change(sk);
			sk->dead                = 1;
			x25_destroy_socket(sk);
			break;			

		case X25_STATE_3:
		case X25_STATE_4:
			x25_clear_queues(sk);
			x25_write_internal(sk, X25_CLEAR_REQUEST);
			sk->protinfo.x25->timer = sk->protinfo.x25->t23;
			sk->protinfo.x25->state = X25_STATE_2;
			sk->state               = TCP_CLOSE;
			sk->shutdown           |= SEND_SHUTDOWN;
			sk->state_change(sk);
			sk->dead                = 1;
			sk->destroy             = 1;
			break;

		default:
			break;
	}

	sock->sk   = NULL;	
	sk->socket = NULL;	/* Not used, but we should do this */

	return 0;
}

static int x25_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	struct sock *sk;
	struct sockaddr_x25 *addr = (struct sockaddr_x25 *)uaddr;
	
	sk = (struct sock *)sock->sk;

	if (sk->zapped == 0)
		return -EINVAL;
		
	if (addr_len != sizeof(struct sockaddr_x25))
		return -EINVAL;

	if (addr->sx25_family != AF_X25)
		return -EINVAL;

	sk->protinfo.x25->source_addr = addr->sx25_addr;

	x25_insert_socket(sk);

	sk->zapped = 0;

	if (sk->debug)
		printk(KERN_DEBUG "x25_bind: socket is bound\n");

	return 0;
}

static int x25_connect(struct socket *sock, struct sockaddr *uaddr, int addr_len, int flags)
{
	struct sock *sk = (struct sock *)sock->sk;
	struct sockaddr_x25 *addr = (struct sockaddr_x25 *)uaddr;
	struct device *dev;
	
	if (sk->state == TCP_ESTABLISHED && sock->state == SS_CONNECTING) {
		sock->state = SS_CONNECTED;
		return 0;	/* Connect completed during a ERESTARTSYS event */
	}
	
	if (sk->state == TCP_CLOSE && sock->state == SS_CONNECTING) {
		sock->state = SS_UNCONNECTED;
		return -ECONNREFUSED;
	}
	
	if (sk->state == TCP_ESTABLISHED)
		return -EISCONN;	/* No reconnect on a seqpacket socket */
		
	sk->state   = TCP_CLOSE;	
	sock->state = SS_UNCONNECTED;

	if (addr_len != sizeof(struct sockaddr_x25))
		return -EINVAL;

	if ((dev = x25_get_route(&addr->sx25_addr)) == NULL)
		return -ENETUNREACH;

	if ((sk->protinfo.x25->neighbour = x25_get_neigh(dev)) == NULL)
		return -ENETUNREACH;

	if (sk->zapped)		/* Must bind first - autobinding does not work */
		return -EINVAL;

	sk->protinfo.x25->dest_addr = addr->sx25_addr;
	sk->protinfo.x25->lci       = x25_new_lci();

	/* Move to connecting socket, start sending Connect Requests */
	sock->state   = SS_CONNECTING;
	sk->state     = TCP_SYN_SENT;

	sk->protinfo.x25->state = X25_STATE_1;
	sk->protinfo.x25->timer = sk->protinfo.x25->t21;
	x25_write_internal(sk, X25_CALL_REQUEST);

	x25_set_timer(sk);
	
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
		return sock_error(sk);	/* Always set at this point */
	}
	
	sock->state = SS_CONNECTED;

	sti();
	
	return 0;
}
	
static int x25_socketpair(struct socket *sock1, struct socket *sock2)
{
	return -EOPNOTSUPP;
}

static int x25_accept(struct socket *sock, struct socket *newsock, int flags)
{
	struct sock *sk;
	struct sock *newsk;
	struct sk_buff *skb;

	if (newsock->sk)
		sk_free(newsock->sk);

	newsock->sk = NULL;
	
	sk = (struct sock *)sock->sk;

	if (sk->type != SOCK_SEQPACKET)
		return -EOPNOTSUPP;
	
	if (sk->state != TCP_LISTEN)
		return -EINVAL;
		
	/*
	 *	The write queue this time is holding sockets ready to use
	 *	hooked into the CALL INDICATION we saved
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
	newsock->sk = newsk;

	return 0;
}

static int x25_getname(struct socket *sock, struct sockaddr *uaddr, int *uaddr_len, int peer)
{
	struct sockaddr_x25 *sx25 = (struct sockaddr_x25 *)uaddr;
	struct sock *sk;
	
	sk = (struct sock *)sock->sk;
	
	if (peer != 0) {
		if (sk->state != TCP_ESTABLISHED)
			return -ENOTCONN;
		sx25->sx25_addr   = sk->protinfo.x25->dest_addr;
	} else {
		sx25->sx25_addr   = sk->protinfo.x25->source_addr;
	}

	sx25->sx25_family = AF_X25;
	*uaddr_len = sizeof(struct sockaddr_x25);

	return 0;
}
 
int x25_rx_call_request(struct sk_buff *skb, struct x25_neigh *neigh, unsigned int lci)
{
	struct sock *sk;
	struct sock *make;
	x25_address source_addr, dest_addr;
	struct x25_facilities facilities;
	int len;

	/*
	 *	skb->data points to the x25 frame start
	 */

	/*
	 *	Remove the LCI and frame type.
	 */
	skb_pull(skb, X25_STD_MIN_LEN);

	/*
	 *	Extract the X.25 addresses and convert them to ASCII strings.
	 */
	len = x25_addr_ntoa(skb->data, &source_addr, &dest_addr);

	/*
	 *	Remove address lengths and addresses.
	 */
	skb_pull(skb, len);

	/*
	 *	Find a listener for the particular address.
	 */
	sk = x25_find_listener(&source_addr);

	/*
	 *	We can't accept the Call Request.
	 */
	if (sk == NULL || sk->ack_backlog == sk->max_ack_backlog || (make = x25_make_new(sk)) == NULL) {
		x25_transmit_clear_request(neigh, lci, 0x01);
		return 0;
	}

	/*
	 *	Parse the facilities.
	 */
	len = x25_parse_facilities(skb, &facilities);

	/*
	 *	Then remove them, leaving any Call User Data.
	 */
	skb_pull(skb, len);

	skb->sk     = make;
	make->state = TCP_ESTABLISHED;

	make->protinfo.x25->lci           = lci;
	make->protinfo.x25->dest_addr     = dest_addr;
	make->protinfo.x25->source_addr   = source_addr;
	make->protinfo.x25->neighbour     = neigh;

	/*
	 *	This implies that we accept all the incoming facilities
	 *	values (if any). This needs fixing. XXX
	 */
	make->protinfo.x25->facilities    = facilities;

	/*
	 *	Incoming Call User Data. XXX
	 */
	
	x25_write_internal(make, X25_CALL_ACCEPTED);

	make->protinfo.x25->state = X25_STATE_3;

	sk->ack_backlog++;
	make->pair = sk;

	x25_insert_socket(make);

	skb_queue_head(&sk->receive_queue, skb);

	x25_set_timer(make);

	if (!sk->dead)
		sk->data_ready(sk, skb->len);

	return 1;
}

static int x25_sendmsg(struct socket *sock, struct msghdr *msg, int len, struct scm_cookie *scm)
{
	struct sock *sk = (struct sock *)sock->sk;
	struct sockaddr_x25 *usx25 = (struct sockaddr_x25 *)msg->msg_name;
	int err;
	struct sockaddr_x25 sx25;
	struct sk_buff *skb;
	unsigned char *asmptr;
	int size, qbit = 0;

	if (msg->msg_flags & ~MSG_DONTWAIT)
		return -EINVAL;

	if (sk->zapped)
		return -EADDRNOTAVAIL;

	if (sk->shutdown & SEND_SHUTDOWN) {
		send_sig(SIGPIPE, current, 0);
		return -EPIPE;
	}

	if (sk->protinfo.x25->neighbour == NULL)
		return -ENETUNREACH;
		
	if (usx25 != NULL) {
		if (msg->msg_namelen < sizeof(sx25))
			return -EINVAL;
		sx25 = *usx25;
		if (strcmp(sk->protinfo.x25->dest_addr.x25_addr, sx25.sx25_addr.x25_addr) != 0)
			return -EISCONN;
		if (sx25.sx25_family != AF_X25)
			return -EINVAL;
	} else {
		/*
		 *	FIXME 1003.1g - if the socket is like this because
		 *	it has become closed (not started closed) we ought
		 *	to SIGPIPE, EPIPE;
		 */
		if (sk->state != TCP_ESTABLISHED)
			return -ENOTCONN;

		sx25.sx25_family = AF_X25;
		sx25.sx25_addr   = sk->protinfo.x25->dest_addr;
	}
	
	if (sk->debug)
		printk(KERN_DEBUG "x25_sendmsg: sendto: Addresses built.\n");

	/* Build a packet */
	if (sk->debug)
		printk(KERN_DEBUG "x25_sendmsg: sendto: building packet.\n");

	size = len + X25_MAX_L2_LEN + X25_EXT_MIN_LEN;

	if ((skb = sock_alloc_send_skb(sk, size, 0, msg->msg_flags & MSG_DONTWAIT, &err)) == NULL)
		return err;

	skb->sk   = sk;
	skb->arp  = 1;

	skb_reserve(skb, X25_MAX_L2_LEN + X25_EXT_MIN_LEN);
	
	/*
	 *	Put the data on the end
	 */
	if (sk->debug)
		printk(KERN_DEBUG "x25_sendmsg: Copying user data\n");

	skb->h.raw = skb_put(skb, len);
	asmptr = skb->h.raw;

	memcpy_fromiovec(asmptr, msg->msg_iov, len);

	/*
	 *	If the Q BIT Include socket option is in force, the first
	 *	byte of the user data is the logical value of the Q Bit.
	 */
	if (sk->protinfo.x25->qbitincl) {
		qbit = skb->data[0];
		skb_pull(skb, 1);
	}

	/*
	 *	Push down the X.25 header
	 */
	if (sk->debug)
		printk(KERN_DEBUG "x25_sendmsg: Building X.25 Header.\n");

	if (sk->protinfo.x25->neighbour->extended) {
		/* Build an Extended X.25 header */
		asmptr    = skb_push(skb, X25_EXT_MIN_LEN);
		*asmptr++ = ((sk->protinfo.x25->lci >> 8) & 0x0F) | X25_GFI_EXTSEQ;
		*asmptr++ = (sk->protinfo.x25->lci >> 0) & 0xFF;
		*asmptr++ = X25_DATA;
		*asmptr++ = X25_DATA;
	} else {
		/* Build an Standard X.25 header */
		asmptr    = skb_push(skb, X25_STD_MIN_LEN);
		*asmptr++ = ((sk->protinfo.x25->lci >> 8) & 0x0F) | X25_GFI_STDSEQ;
		*asmptr++ = (sk->protinfo.x25->lci >> 0) & 0xFF;
		*asmptr++ = X25_DATA;
	}

	if (qbit)
		skb->data[0] |= X25_Q_BIT;

	if (sk->debug)
		printk(KERN_DEBUG "x25_sendmsg: Built header.\n");

	if (sk->debug)
		printk(KERN_DEBUG "x25_sendmsg: Transmitting buffer\n");

	if (sk->state != TCP_ESTABLISHED) {
		kfree_skb(skb, FREE_WRITE);
		return -ENOTCONN;
	}

	x25_output(sk, skb);	/* Shove it onto the queue */

	return len;
}


static int x25_recvmsg(struct socket *sock, struct msghdr *msg, int size, int flags, struct scm_cookie *scm)
{
	struct sock *sk = (struct sock *)sock->sk;
	struct sockaddr_x25 *sx25 = (struct sockaddr_x25 *)msg->msg_name;
	int copied, qbit;
	struct sk_buff *skb;
	unsigned char *asmptr;
	int er;

	/*
	 * This works for seqpacket too. The receiver has ordered the queue for
	 * us! We do one quick check first though
	 */
	if (sk->state != TCP_ESTABLISHED)
		return -ENOTCONN;

	/* Now we can treat all alike */
	if ((skb = skb_recv_datagram(sk, flags, msg->msg_flags & MSG_DONTWAIT, &er)) == NULL)
		return er;

	qbit = (skb->data[0] & X25_Q_BIT) == X25_Q_BIT;

	skb_pull(skb, (sk->protinfo.x25->neighbour->extended) ? X25_EXT_MIN_LEN : X25_STD_MIN_LEN);
	skb->h.raw = skb->data;

	if (sk->protinfo.x25->qbitincl) {
		asmptr  = skb_push(skb, 1);
		*asmptr = qbit;
		skb->h.raw = skb->data;
	}

	copied = skb->len;
	
	if (copied > size) {
		copied = size;
		msg->msg_flags |= MSG_TRUNC;
	}

	skb_copy_datagram_iovec(skb, 0, msg->msg_iov, copied);
	
	if (sx25 != NULL) {
		struct sockaddr_x25 addr;
		
		addr.sx25_family = AF_X25;
		addr.sx25_addr   = sk->protinfo.x25->dest_addr;

		*sx25 = addr;
	}

	msg->msg_namelen = sizeof(struct sockaddr_x25);

	skb_free_datagram(sk, skb);

	return copied;
}

static int x25_shutdown(struct socket *sk, int how)
{
	return -EOPNOTSUPP;
}

static int x25_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	struct sock *sk = (struct sock *)sock->sk;
	int err;
	long amount = 0;

	switch (cmd) {
		case TIOCOUTQ:
			if ((err = verify_area(VERIFY_WRITE, (void *)arg, sizeof(unsigned long))) != 0)
				return err;
			amount = sk->sndbuf - sk->wmem_alloc;
			if (amount < 0)
				amount = 0;
			put_user(amount, (unsigned long *)arg);
			return 0;

		case TIOCINQ: {
			struct sk_buff *skb;
			/* These two are safe on a single CPU system as only user tasks fiddle here */
			if ((skb = skb_peek(&sk->receive_queue)) != NULL)
				amount = skb->len - 20;
			if ((err = verify_area(VERIFY_WRITE, (void *)arg, sizeof(unsigned long))) != 0)
				return err;
			put_user(amount, (unsigned long *)arg);
			return 0;
		}

		case SIOCGSTAMP:
			if (sk != NULL) {
				if (sk->stamp.tv_sec==0)
					return -ENOENT;
				if ((err = verify_area(VERIFY_WRITE,(void *)arg,sizeof(struct timeval))) != 0)
					return err;
				copy_to_user((void *)arg, &sk->stamp, sizeof(struct timeval));
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

		case SIOCADDRT:
		case SIOCDELRT:
			if (!suser()) return -EPERM;
			return x25_route_ioctl(cmd, (void *)arg);

		case SIOCX25SETSUBSCR:
			if (!suser()) return -EPERM;
			return x25_subscr_ioctl(cmd, (void *)arg);

 		default:
			return dev_ioctl(cmd, (void *)arg);
	}

	/*NOTREACHED*/
	return 0;
}

static int x25_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	struct sock *s;
	struct device *dev;
	const char *devname;
	int len = 0;
	off_t pos = 0;
	off_t begin = 0;
  
	cli();

	len += sprintf(buffer, "dest_addr  src_addr   dev   lci st vs vr va   t  t2 t21 t22 t23 Snd-Q Rcv-Q\n");

	for (s = x25_list; s != NULL; s = s->next) {
		if (s->protinfo.x25->neighbour == NULL || (dev = s->protinfo.x25->neighbour->dev) == NULL)
			devname = "???";
		else
			devname = s->protinfo.x25->neighbour->dev->name;

		len += sprintf(buffer + len, "%-10s %-10s %-5s %3.3X  %d  %d  %d  %d %3d %3d %3d %3d %3d %5d %5d\n",
			(s->protinfo.x25->dest_addr.x25_addr[0] == '\0')   ? "*" : s->protinfo.x25->dest_addr.x25_addr,
			(s->protinfo.x25->source_addr.x25_addr[0] == '\0') ? "*" : s->protinfo.x25->source_addr.x25_addr,
			devname,  s->protinfo.x25->lci & 0x0FFF,
			s->protinfo.x25->state,
			s->protinfo.x25->vs, s->protinfo.x25->vr, s->protinfo.x25->va,
			s->protinfo.x25->timer / X25_SLOWHZ,
			s->protinfo.x25->t2    / X25_SLOWHZ,
			s->protinfo.x25->t21   / X25_SLOWHZ,
			s->protinfo.x25->t22   / X25_SLOWHZ,
			s->protinfo.x25->t23   / X25_SLOWHZ,
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

struct net_proto_family x25_family_ops = {
	AF_X25,
	x25_create
};

struct proto_ops x25_proto_ops = {
	AF_X25,

	x25_dup,
	x25_release,
	x25_bind,
	x25_connect,
	x25_socketpair,
	x25_accept,
	x25_getname,
	datagram_select,
	x25_ioctl,
	x25_listen,
	x25_shutdown,
	x25_setsockopt,
	x25_getsockopt,
	x25_fcntl,
	x25_sendmsg,
	x25_recvmsg
};

static struct packet_type x25_packet_type =
{
	0,		/* MUTTER ntohs(ETH_P_X25),*/
	0,		/* copy */
	x25_llc_receive_frame,
	NULL,
	NULL,
};

struct notifier_block x25_dev_notifier = {
	x25_device_event,
	0
};

void x25_proto_init(struct net_proto *pro)
{
	sock_register(&x25_family_ops);

	x25_packet_type.type = htons(ETH_P_X25);
	dev_add_pack(&x25_packet_type);

	register_netdevice_notifier(&x25_dev_notifier);

	printk(KERN_INFO "X.25 for Linux. Version 0.1 for Linux 2.1.15\n");

	x25_register_sysctl();

#ifdef CONFIG_PROC_FS
	proc_net_register(&(struct proc_dir_entry) {
		PROC_NET_X25, 3, "x25",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_net_inode_operations, 
		x25_get_info
	});

	proc_net_register(&(struct proc_dir_entry) {
		PROC_NET_X25_LINKS, 9, "x25_links",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_net_inode_operations, 
		x25_link_get_info
	});

	proc_net_register(&(struct proc_dir_entry) {
		PROC_NET_X25_ROUTES, 10, "x25_routes",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_net_inode_operations, 
		x25_routes_get_info
	});
#endif	
}

#ifdef MODULE

int init_module(void)
{
	register_symtab(NULL);

	x25_proto_init(NULL);
	
	return 0;
}

void cleanup_module(void)
{

#ifdef CONFIG_PROC_FS
	proc_net_unregister(PROC_NET_X25);
	proc_net_unregister(PROC_NET_X25_LINKS);
	proc_net_unregister(PROC_NET_X25_ROUTES);
#endif

	x25_link_free();
	x25_route_free();

	x25_unregister_sysctl();

	unregister_netdevice_notifier(&x25_dev_notifier);

	dev_remove_pack(&x25_packet_type);

	sock_unregister(x25_proto_ops.family);
}

#endif

#endif
