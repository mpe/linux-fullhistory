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
 *	NET/ROM	003	Jonathan(G4KLX)	Added G8BPQ extensions.
 *					Added NET/ROM routing ioctl.
 *			Darryl(G7LED)	Fix autobinding (on connect).
 *					Fixed nr_release(), set TCP_CLOSE, wakeup app
 *					context, THEN make the sock dead.
 *					Circuit ID check before allocating it on
 *					a connection.
 *			Alan(GW4PTS)	sendmsg/recvmsg only. Fixed connect clear bug
 *					inherited from AX.25
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
#include <linux/stat.h>
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
#include <linux/proc_fs.h>
#include <net/ip.h>
#include <net/arp.h>
#include <linux/if_arp.h>

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
 *	Kill all bound sockets on a dropped device.
 */
static void nr_kill_by_device(struct device *dev)
{
	struct sock *s;
	
	for (s = nr_list; s != NULL; s = s->next) {
		if (s->nr->device == dev) {
			s->nr->state  = NR_STATE_0;
			s->nr->device = NULL;
			s->state = TCP_CLOSE;
			s->err   = ENETUNREACH;
			s->state_change(s);
			s->dead  = 1;
		}
	}
}

/*
 *	Handle device status changes.
 */
static int nr_device_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct device *dev = (struct device *)ptr;

	if (event != NETDEV_DOWN)
		return NOTIFY_DONE;
		
	nr_kill_by_device(dev);
	nr_rt_device_down(dev);
	
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
static struct sock *nr_find_listener(ax25_address *addr)
{
	unsigned long flags;
	struct sock *s;

	save_flags(flags);
	cli();

	for (s = nr_list; s != NULL; s = s->next) {
		if (ax25cmp(&s->nr->source_addr, addr) == 0 && s->state == TCP_LISTEN) {
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
static struct sock *nr_find_socket(unsigned char index, unsigned char id)
{
	struct sock *s;
	unsigned long flags;

	save_flags(flags);
	cli();

	for (s = nr_list; s != NULL; s = s->next) {
		if (s->nr->my_index == index && s->nr->my_id == id) {
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
static struct sock *nr_find_peer(unsigned char index, unsigned char id)
{
	struct sock *s;
	unsigned long flags;

	save_flags(flags);
	cli();

	for (s = nr_list; s != NULL; s = s->next) {
		if (s->nr->your_index == index && s->nr->your_id == id) {
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
void nr_destroy_socket(struct sock *);

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
void nr_destroy_socket(struct sock *sk)	/* Not static as it's used by the timer */
{
	struct sk_buff *skb;
	unsigned long flags;
	
	save_flags(flags);
	cli();
	
	del_timer(&sk->timer);
	
	nr_remove_socket(sk);
	nr_clear_queues(sk);		/* Flush the queues */
	
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
		sk->timer.expires  = jiffies + 10 * HZ;
		sk->timer.function = nr_destroy_timer;
		sk->timer.data     = (unsigned long)sk;
		add_timer(&sk->timer);
	} else {
		kfree_s(sk->nr, sizeof(*sk->nr));
		sk_free(sk);
	}

	restore_flags(flags);
}

/*
 *	Handling for system calls applied via the various interfaces to a
 *	NET/ROM socket object.
 */
 
static int nr_fcntl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	return -EINVAL;
}

/*
 * dl1bke 960311: set parameters for existing NET/ROM connections,
 *		  includes a KILL command to abort any connection.
 *		  VERY useful for debugging ;-)
 */
static int nr_ctl_ioctl(const unsigned int cmd, void *arg)
{
	struct nr_ctl_struct nr_ctl;
	struct sock *sk;
	unsigned long flags;
	int err;
	
	if ((err = verify_area(VERIFY_READ, arg, sizeof(nr_ctl))) != 0)
		return err;

	memcpy_fromfs(&nr_ctl, arg, sizeof(nr_ctl));
	
	if ((sk = nr_find_socket(nr_ctl.index, nr_ctl.id)) == NULL)
		return -ENOTCONN;

	switch (nr_ctl.cmd) {
		case NETROM_KILL:
			nr_clear_queues(sk);
			nr_write_internal(sk, NR_DISCREQ);
			sk->nr->state = NR_STATE_0;
			sk->state = TCP_CLOSE;
			sk->err   = ENETRESET;
			if (!sk->dead)
				sk->state_change(sk);
			sk->dead  = 1;
			nr_set_timer(sk);
	  		break;

	  	case NETROM_T1:
  			if (nr_ctl.arg < 1) 
  				return -EINVAL;
  			sk->nr->rtt = (nr_ctl.arg * PR_SLOWHZ) / 2;
  			sk->nr->t1 = nr_ctl.arg * PR_SLOWHZ;
  			save_flags(flags); cli();
  			if (sk->nr->t1timer > sk->nr->t1)
  				sk->nr->t1timer = sk->nr->t1;
  			restore_flags(flags);
  			break;

	  	case NETROM_T2:
	  		if (nr_ctl.arg < 1) 
	  			return -EINVAL;
	  		save_flags(flags); cli();
	  		sk->nr->t2 = nr_ctl.arg * PR_SLOWHZ;
	  		if (sk->nr->t2timer > sk->nr->t2)
	  			sk->nr->t2timer = sk->nr->t2;
	  		restore_flags(flags);
	  		break;

	  	case NETROM_N2:
	  		if (nr_ctl.arg < 1 || nr_ctl.arg > 10) 
	  			return -EINVAL;
	  		sk->nr->n2count = 0;
	  		sk->nr->n2 = nr_ctl.arg;
	  		break;

	  	case NETROM_PACLEN:
	  		if (nr_ctl.arg < 16 || nr_ctl.arg > 65535) 
	  			return -EINVAL;
	  		if (nr_ctl.arg > 236) /* we probably want this */
	  			printk(KERN_WARNING "nr_ctl_ioctl: Warning --- huge paclen %d\n", (int)nr_ctl.arg);
	  		sk->nr->paclen = nr_ctl.arg;
	  		break;

	  	default:
	  		return -EINVAL;
	  }
	  
	  return 0;
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
			sk->nr->rtt = (opt * PR_SLOWHZ) / 2;
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
			
		case NETROM_HDRINCL:
			sk->nr->hdrincl = opt ? 1 : 0;
			return 0;

		case NETROM_PACLEN:
			if (opt < 1 || opt > 65536)
				return -EINVAL;
			sk->nr->paclen = opt;
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
			val = (sk->nr->t1 * 2) / PR_SLOWHZ;
			break;
			
		case NETROM_T2:
			val = sk->nr->t2 / PR_SLOWHZ;
			break;
			
		case NETROM_N2:
			val = sk->nr->n2;
			break;
						
		case NETROM_HDRINCL:
			val = sk->nr->hdrincl;
			break;

		case NETROM_PACLEN:
			val = sk->nr->paclen;
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

	if (sk->state != TCP_LISTEN) {
		memset(&sk->nr->user_addr, '\0', AX25_ADDR_LEN);
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

	if (sock->type != SOCK_SEQPACKET || protocol != 0)
		return -ESOCKTNOSUPPORT;

	if ((sk = sk_alloc(GFP_ATOMIC)) == NULL)
		return -ENOMEM;

	if ((nr = (nr_cb *)kmalloc(sizeof(*nr), GFP_ATOMIC)) == NULL) {
		sk_free(sk);
		return -ENOMEM;
	}

	skb_queue_head_init(&sk->receive_queue);
	skb_queue_head_init(&sk->write_queue);
	skb_queue_head_init(&sk->back_log);

	init_timer(&sk->timer);

	sk->socket        = sock;
	sk->type          = sock->type;
	sk->protocol      = protocol;
	sk->allocation	  = GFP_KERNEL;
	sk->rcvbuf        = SK_RMEM_MAX;
	sk->sndbuf        = SK_WMEM_MAX;
	sk->state         = TCP_CLOSE;
	sk->priority      = SOPRI_NORMAL;
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
	skb_queue_head_init(&nr->frag_queue);

	nr->my_index = 0;
	nr->my_id    = 0;
	nr->rtt      = nr_default.timeout / 2;
	nr->t1       = nr_default.timeout;
	nr->t2       = nr_default.ack_delay;
	nr->n2       = nr_default.tries;
	nr->paclen   = nr_default.paclen;

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

	nr->bpqext     = 1;
	nr->fraglen    = 0;
	nr->hdrincl    = 0;
	nr->state      = NR_STATE_0;
	nr->device     = NULL;

	memset(&nr->source_addr, '\0', AX25_ADDR_LEN);
	memset(&nr->user_addr,   '\0', AX25_ADDR_LEN);
	memset(&nr->dest_addr,   '\0', AX25_ADDR_LEN);

	nr->sk = sk;
	sk->nr = nr;

	return 0;
}

static struct sock *nr_make_new(struct sock *osk)
{
	struct sock *sk;
	nr_cb *nr;

	if (osk->type != SOCK_SEQPACKET)
		return NULL;

	if ((sk = (struct sock *)sk_alloc(GFP_ATOMIC)) == NULL)
		return NULL;

	if ((nr = (nr_cb *)kmalloc(sizeof(*nr), GFP_ATOMIC)) == NULL) {
		sk_free(sk);
		return NULL;
	}

	skb_queue_head_init(&sk->receive_queue);
	skb_queue_head_init(&sk->write_queue);
	skb_queue_head_init(&sk->back_log);

	init_timer(&sk->timer);

	sk->type        = osk->type;
	sk->socket      = osk->socket;
	sk->priority    = osk->priority;
	sk->protocol    = osk->protocol;
	sk->rcvbuf      = osk->rcvbuf;
	sk->sndbuf      = osk->sndbuf;
	sk->debug       = osk->debug;
	sk->state       = TCP_ESTABLISHED;
	sk->window      = osk->window;
	sk->mtu         = osk->mtu;
	sk->sleep       = osk->sleep;
	sk->zapped      = osk->zapped;

	sk->state_change = def_callback1;
	sk->data_ready   = def_callback2;
	sk->write_space  = def_callback1;
	sk->error_report = def_callback1;

	skb_queue_head_init(&nr->ack_queue);
	skb_queue_head_init(&nr->reseq_queue);
	skb_queue_head_init(&nr->frag_queue);

	nr->rtt      = osk->nr->rtt;
	nr->t1       = osk->nr->t1;
	nr->t2       = osk->nr->t2;
	nr->n2       = osk->nr->n2;
	nr->paclen   = osk->nr->paclen;

	nr->device   = osk->nr->device;
	nr->bpqext   = osk->nr->bpqext;
	nr->hdrincl  = osk->nr->hdrincl;
	nr->fraglen  = 0;

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

	switch (sk->nr->state) {

		case NR_STATE_0:
			sk->state     = TCP_CLOSE;
			sk->state_change(sk);
			sk->dead      = 1;
			nr_destroy_socket(sk);
			break;

		case NR_STATE_1:
			sk->nr->state = NR_STATE_0;
			sk->state     = TCP_CLOSE;
			sk->state_change(sk);
			sk->dead      = 1;
			nr_destroy_socket(sk);
			break;

		case NR_STATE_2:
			nr_write_internal(sk, NR_DISCACK);
			sk->nr->state = NR_STATE_0;
			sk->state     = TCP_CLOSE;
			sk->state_change(sk);
			sk->dead      = 1;
			nr_destroy_socket(sk);
			break;			

		case NR_STATE_3:
			nr_clear_queues(sk);
			sk->nr->n2count = 0;
			nr_write_internal(sk, NR_DISCREQ);
			sk->nr->t1timer = sk->nr->t1 = nr_calculate_t1(sk);
			sk->nr->t2timer = 0;
			sk->nr->t4timer = 0;
			sk->nr->state   = NR_STATE_2;
			sk->state       = TCP_CLOSE;
			sk->state_change(sk);
			sk->dead        = 1;
			sk->destroy     = 1;
			break;

		default:
			break;
	}

	sock->data = NULL;	
	sk->socket = NULL;	/* Not used, but we should do this. **/

	return 0;
}

static int nr_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	struct sock *sk;
	struct full_sockaddr_ax25 *addr = (struct full_sockaddr_ax25 *)uaddr;
	struct device *dev;
	ax25_address *user, *source;
	
	sk = (struct sock *)sock->data;

	if (sk->zapped == 0)
		return -EIO;
		
	if (addr_len != sizeof(struct sockaddr_ax25) && addr_len != sizeof(struct full_sockaddr_ax25))
		return -EINVAL;

	if ((dev = nr_dev_get(&addr->fsa_ax25.sax25_call)) == NULL) {
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
		sk->nr->user_addr   = addr->fsa_digipeater[0];
		sk->nr->source_addr = addr->fsa_ax25.sax25_call;
	} else {
		source = &addr->fsa_ax25.sax25_call;

		if ((user = ax25_findbyuid(current->euid)) == NULL) {
			if (ax25_uid_policy && !suser())
				return -EPERM;
			user = source;
		}

		sk->nr->user_addr   = *user;
		sk->nr->source_addr = *source;
	}

	sk->nr->device = dev;
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
	
	if (sk->state == TCP_ESTABLISHED)
		return -EISCONN;	/* No reconnect on a seqpacket socket */
		
	sk->state   = TCP_CLOSE;	
	sock->state = SS_UNCONNECTED;

	if (addr_len != sizeof(struct sockaddr_ax25))
		return -EINVAL;

	if (sk->zapped) {	/* Must bind first - autobinding in this may or may not work */
		sk->zapped = 0;

		if ((dev = nr_dev_first()) == NULL)
			return -ENETUNREACH;

		source = (ax25_address *)dev->dev_addr;

		if ((user = ax25_findbyuid(current->euid)) == NULL) {
			if (ax25_uid_policy && !suser())
				return -EPERM;
			user = source;
		}

		sk->nr->user_addr   = *user;
		sk->nr->source_addr = *source;
		sk->nr->device      = dev;

		nr_insert_socket(sk);		/* Finish the bind */
	}

	sk->nr->dest_addr = addr->sax25_call;

	while (nr_find_socket((unsigned char)circuit / 256, (unsigned char)circuit % 256) != NULL)
		circuit++;

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
		return sock_error(sk);	/* Always set at this point */
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
		sax->fsa_ax25.sax25_call = sk->nr->user_addr;
		sax->fsa_digipeater[0]   = sk->nr->dest_addr;
		*uaddr_len = sizeof(struct sockaddr_ax25) + AX25_ADDR_LEN;
	} else {
		sax->fsa_ax25.sax25_family = AF_NETROM;
		sax->fsa_ax25.sax25_ndigis = 0;
		sax->fsa_ax25.sax25_call   = sk->nr->source_addr;
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
	unsigned short frametype, window, timeout;
	

	skb->sk = NULL;		/* Initially we don't know who it's for */

	/*
	 *	skb->data points to the netrom frame start
	 */
	
	src  = (ax25_address *)(skb->data + 0);
	dest = (ax25_address *)(skb->data + 7);

	circuit_index = skb->data[15];
	circuit_id    = skb->data[16];
	frametype     = skb->data[19];

#ifdef CONFIG_INET
	/*
	 * Check for an incoming IP over NET/ROM frame.
	 */
	 if ((frametype & 0x0F) == NR_PROTOEXT && circuit_index == NR_PROTO_IP && circuit_id == NR_PROTO_IP) {
		skb_pull(skb, NR_NETWORK_LEN + NR_TRANSPORT_LEN);
	 	skb->h.raw = skb->data;

		return nr_rx_ip(skb, dev);
	 }
#endif

	/*
	 * Find an existing socket connection, based on circuit ID, if it's
	 * a Connect Request base it on their circuit ID.
	 */
	if (((frametype & 0x0F) != NR_CONNREQ && (sk = nr_find_socket(circuit_index, circuit_id)) != NULL) ||
	    ((frametype & 0x0F) == NR_CONNREQ && (sk = nr_find_peer(circuit_index, circuit_id)) != NULL)) {
		skb->h.raw = skb->data;

		if ((frametype & 0x0F) == NR_CONNACK && skb->len == 22)
			sk->nr->bpqext = 1;
		else
			sk->nr->bpqext = 0;

		return nr_process_rx_frame(sk, skb);
	}

	if ((frametype & 0x0F) != NR_CONNREQ)
		return 0;
		
	sk = nr_find_listener(dest);

	user = (ax25_address *)(skb->data + 21);

	if (sk == NULL || sk->ack_backlog == sk->max_ack_backlog || (make = nr_make_new(sk)) == NULL) {
		nr_transmit_dm(skb);
		return 0;
	}

	window = skb->data[20];

	skb->sk             = make;
	make->state         = TCP_ESTABLISHED;

	/* Fill in his circuit details */
	make->nr->source_addr = *dest;
	make->nr->dest_addr   = *src;
	make->nr->user_addr   = *user;

	make->nr->your_index = circuit_index;
	make->nr->your_id    = circuit_id;

	make->nr->my_index   = circuit / 256;
	make->nr->my_id      = circuit % 256;
	
	circuit++;

	/* Window negotiation */
	if (window < make->window)
		make->window = window;

	/* L4 timeout negotiation */
	if (skb->len == 37) {
		timeout = skb->data[36] * 256 + skb->data[35];
		if (timeout * PR_SLOWHZ < make->nr->rtt * 2)
			make->nr->rtt = (timeout * PR_SLOWHZ) / 2;
		make->nr->bpqext = 1;
	} else {
		make->nr->bpqext = 0;
	}

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

static int nr_sendmsg(struct socket *sock, struct msghdr *msg, int len, int noblock, int flags)
{
	struct sock *sk = (struct sock *)sock->data;
	struct sockaddr_ax25 *usax = (struct sockaddr_ax25 *)msg->msg_name;
	int err;
	struct sockaddr_ax25 sax;
	struct sk_buff *skb;
	unsigned char *asmptr;
	int size;
	
	if (sk->err)
		return sock_error(sk);

	if (flags)
		return -EINVAL;

	if (sk->zapped)
		return -EADDRNOTAVAIL;

	if (sk->nr->device == NULL)
		return -ENETUNREACH;
		
	if (usax) {
		if (msg->msg_namelen < sizeof(sax))
			return -EINVAL;
		sax = *usax;
		if (ax25cmp(&sk->nr->dest_addr, &sax.sax25_call) != 0)
			return -EISCONN;
		if (sax.sax25_family != AF_NETROM)
			return -EINVAL;
	} else {
		if (sk->state != TCP_ESTABLISHED)
			return -ENOTCONN;
		sax.sax25_family = AF_NETROM;
		sax.sax25_call   = sk->nr->dest_addr;
	}
	
	if (sk->debug)
		printk("NET/ROM: sendto: Addresses built.\n");

	/* Build a packet */
	if (sk->debug)
		printk("NET/ROM: sendto: building packet.\n");

	size = len + AX25_BPQ_HEADER_LEN + AX25_MAX_HEADER_LEN + NR_NETWORK_LEN + NR_TRANSPORT_LEN;

	if ((skb = sock_alloc_send_skb(sk, size, 0, 0, &err)) == NULL)
		return err;

	skb->sk   = sk;
	skb->free = 1;
	skb->arp  = 1;

	skb_reserve(skb, size - len);
	
	/*
	 *	Push down the NET/ROM header
	 */

	asmptr = skb_push(skb, NR_TRANSPORT_LEN);

	if (sk->debug)
		printk("Building NET/ROM Header.\n");

	/* Build a NET/ROM Transport header */

	*asmptr++ = sk->nr->your_index;
	*asmptr++ = sk->nr->your_id;
	*asmptr++ = 0;		/* To be filled in later */
	*asmptr++ = 0;		/*      Ditto            */
	*asmptr++ = NR_INFO;
	
	if (sk->debug)
		printk("Built header.\n");

	/*
	 *	Put the data on the end
	 */

	skb->h.raw = skb_put(skb, len);

	asmptr = skb->h.raw;
	
	if (sk->debug)
		printk("NET/ROM: Appending user data\n");

	/* User data follows immediately after the NET/ROM transport header */
	memcpy_fromiovec(asmptr, msg->msg_iov, len);

	if (sk->debug)
		printk("NET/ROM: Transmitting buffer\n");

	if (sk->state != TCP_ESTABLISHED) {
		kfree_skb(skb, FREE_WRITE);
		return -ENOTCONN;
	}

	nr_output(sk, skb);	/* Shove it onto the queue */

	return len;
}


static int nr_recvmsg(struct socket *sock, struct msghdr *msg, int size, int noblock,
		   int flags, int *addr_len)
{
	struct sock *sk = (struct sock *)sock->data;
	struct sockaddr_ax25 *sax = (struct sockaddr_ax25 *)msg->msg_name;
	int copied;
	struct sk_buff *skb;
	int er;

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
	if ((skb = skb_recv_datagram(sk, flags, noblock, &er)) == NULL)
		return er;

	if (!sk->nr->hdrincl) {
		skb_pull(skb, NR_NETWORK_LEN + NR_TRANSPORT_LEN);
		skb->h.raw = skb->data;
	}

	copied = (size < skb->len) ? size : skb->len;
	skb_copy_datagram_iovec(skb, 0, msg->msg_iov, copied);
	
	if (sax != NULL) {
		struct sockaddr_ax25 addr;
		
		addr.sax25_family = AF_NETROM;
		memcpy(&addr.sax25_call, skb->data + 7, AX25_ADDR_LEN);

		*sax = addr;

		*addr_len = sizeof(*sax);
	}

	skb_free_datagram(sk, skb);

	return copied;
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

		case TIOCINQ: {
			struct sk_buff *skb;
			/* These two are safe on a single CPU system as only user tasks fiddle here */
			if ((skb = skb_peek(&sk->receive_queue)) != NULL)
				amount = skb->len - 20;
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

		case SIOCADDRT:
		case SIOCDELRT:
		case SIOCNRDECOBS:
		case SIOCNRRTCTL:
			if (!suser()) return -EPERM;
			return nr_rt_ioctl(cmd, (void *)arg);

		case SIOCNRGETPARMS: {
			struct nr_parms_struct nr_parms;
			if ((err = verify_area(VERIFY_WRITE, (void *)arg, sizeof(struct nr_parms_struct))) != 0)
				return err;
			memcpy_fromfs(&nr_parms, (void *)arg, sizeof(struct nr_parms_struct));
			nr_parms = nr_default;
			memcpy_tofs((void *)arg, &nr_parms, sizeof(struct nr_parms_struct));
			return 0;
		}

		case SIOCNRSETPARMS: {
			struct nr_parms_struct nr_parms;
			if (!suser()) return -EPERM;
			if ((err = verify_area(VERIFY_READ, (void *)arg, sizeof(struct nr_parms_struct))) != 0)
				return err;
			memcpy_fromfs(&nr_parms, (void *)arg, sizeof(struct nr_parms_struct));
			nr_default = nr_parms;
			return 0;
		}
		
 		case SIOCNRCTLCON:
 			if (!suser()) return -EPERM;
 			return nr_ctl_ioctl(cmd, (void *)arg);
 
 		default:
			return dev_ioctl(cmd, (void *)arg);
	}

	/*NOTREACHED*/
	return 0;
}

static int nr_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	struct sock *s;
	struct device *dev;
	const char *devname;
	int len = 0;
	off_t pos = 0;
	off_t begin = 0;
  
	cli();

	len += sprintf(buffer, "user_addr dest_node src_node  dev    my  your  st vs vr va    t1     t2    n2  rtt wnd paclen Snd-Q Rcv-Q\n");

	for (s = nr_list; s != NULL; s = s->next) {
		if ((dev = s->nr->device) == NULL)
			devname = "???";
		else
			devname = dev->name;
	
		len += sprintf(buffer + len, "%-9s ",
			ax2asc(&s->nr->user_addr));
		len += sprintf(buffer + len, "%-9s ",
			ax2asc(&s->nr->dest_addr));
		len += sprintf(buffer + len, "%-9s %-3s  %02X/%02X %02X/%02X %2d %2d %2d %2d %3d/%03d %2d/%02d %2d/%02d %3d %3d %6d %5d %5d\n",
			ax2asc(&s->nr->source_addr),
			devname, s->nr->my_index, s->nr->my_id,
			s->nr->your_index, s->nr->your_id,
			s->nr->state,
			s->nr->vs, s->nr->vr, s->nr->va,
			s->nr->t1timer / PR_SLOWHZ,
			s->nr->t1      / PR_SLOWHZ,
			s->nr->t2timer / PR_SLOWHZ,
			s->nr->t2      / PR_SLOWHZ,
			s->nr->n2count, s->nr->n2,
			s->nr->rtt     / PR_SLOWHZ,
			s->window, s->nr->paclen,
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
	nr_select,
	nr_ioctl,
	nr_listen,
	nr_shutdown,
	nr_setsockopt,
	nr_getsockopt,
	nr_fcntl,
	nr_sendmsg,
	nr_recvmsg
};

static struct notifier_block nr_dev_notifier = {
	nr_device_event,
	0
};

void nr_proto_init(struct net_proto *pro)
{
	sock_register(nr_proto_ops.family, &nr_proto_ops);
	register_netdevice_notifier(&nr_dev_notifier);
	printk(KERN_INFO "G4KLX NET/ROM for Linux. Version 0.5 for AX25.032 Linux 2.0\n");

	nr_default.quality    = NR_DEFAULT_QUAL;
	nr_default.obs_count  = NR_DEFAULT_OBS;
	nr_default.ttl        = NR_DEFAULT_TTL;
	nr_default.timeout    = NR_DEFAULT_T1;
	nr_default.ack_delay  = NR_DEFAULT_T2;
	nr_default.busy_delay = NR_DEFAULT_T4;
	nr_default.tries      = NR_DEFAULT_N2;
	nr_default.window     = NR_DEFAULT_WINDOW;
	nr_default.paclen     = NR_DEFAULT_PACLEN;

#ifdef CONFIG_PROC_FS
	proc_net_register(&(struct proc_dir_entry) {
		PROC_NET_NR, 2, "nr",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_net_inode_operations, 
		nr_get_info
	});
	proc_net_register(&(struct proc_dir_entry) {
		PROC_NET_NR_NEIGH, 8, "nr_neigh",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_net_inode_operations, 
		nr_neigh_get_info
	});
	proc_net_register(&(struct proc_dir_entry) {
		PROC_NET_NR_NODES, 8, "nr_nodes",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_net_inode_operations, 
		nr_nodes_get_info
	});
#endif	
}

#endif
