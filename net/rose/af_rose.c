/*
 *	Rose release 001
 *
 *	This is ALPHA test software. This code may break your machine, randomly fail to work with new 
 *	releases, misbehave and/or generally screw up. It might even work. 
 *
 *	This code REQUIRES 2.1.0 or higher/ NET3.029
 *
 *	This module:
 *		This module is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	History
 *	Rose 001	Jonathan(G4KLX)	Cloned from af_netrom.c.
 */
  
#include <linux/config.h>
#if defined(CONFIG_ROSE) || defined(CONFIG_ROSE_MODULE)
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
#include <asm/uaccess.h>
#include <linux/fcntl.h>
#include <linux/termios.h>	/* For TIOCINQ/OUTQ */
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <net/rose.h>
#include <linux/proc_fs.h>
#include <net/ip.h>
#include <net/arp.h>
#include <linux/if_arp.h>

int sysctl_rose_restart_request_timeout = ROSE_DEFAULT_T0;
int sysctl_rose_call_request_timeout    = ROSE_DEFAULT_T1;
int sysctl_rose_reset_request_timeout   = ROSE_DEFAULT_T2;
int sysctl_rose_clear_request_timeout   = ROSE_DEFAULT_T3;
int sysctl_rose_no_activity_timeout     = ROSE_DEFAULT_IDLE;
int sysctl_rose_routing_control         = 1;

static unsigned int lci = 1;

static struct sock *volatile rose_list = NULL;

/*
 *	Convert a Rose address into text.
 */
char *rose2asc(rose_address *addr)
{
	static char buffer[11];

	if (addr->rose_addr[0] == 0x00 && addr->rose_addr[1] == 0x00 &&
	    addr->rose_addr[2] == 0x00 && addr->rose_addr[3] == 0x00 &&
	    addr->rose_addr[4] == 0x00) {
		strcpy(buffer, "*");
	} else {
		sprintf(buffer, "%02X%02X%02X%02X%02X", addr->rose_addr[0] & 0xFF,
						addr->rose_addr[1] & 0xFF,
						addr->rose_addr[2] & 0xFF,
						addr->rose_addr[3] & 0xFF,
						addr->rose_addr[4] & 0xFF);
	}

	return buffer;
}

/*
 *	Compare two Rose addresses, 0 == equal.
 */
int rosecmp(rose_address *addr1, rose_address *addr2)
{
	int i;

	for (i = 0; i < 5; i++)
		if (addr1->rose_addr[i] != addr2->rose_addr[i])
			return 1;
			
	return 0;
}

/*
 *	Socket removal during an interrupt is now safe.
 */
static void rose_remove_socket(struct sock *sk)
{
	struct sock *s;
	unsigned long flags;
	
	save_flags(flags);
	cli();

	if ((s = rose_list) == sk) {
		rose_list = s->next;
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
static void rose_kill_by_device(struct device *dev)
{
	struct sock *s;
	
	for (s = rose_list; s != NULL; s = s->next) {
		if (s->protinfo.rose->device == dev) {
			s->protinfo.rose->state  = ROSE_STATE_0;
			s->protinfo.rose->device = NULL;
			s->state                 = TCP_CLOSE;
			s->err                   = ENETUNREACH;
			s->state_change(s);
			s->dead                  = 1;
		}
	}
}

/*
 *	Handle device status changes.
 */
static int rose_device_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct device *dev = (struct device *)ptr;

	if (event != NETDEV_DOWN)
		return NOTIFY_DONE;
		
	rose_kill_by_device(dev);
	rose_rt_device_down(dev);
	rose_link_device_down(dev);
	
	return NOTIFY_DONE;
}

/*
 *	Add a socket to the bound sockets list.
 */
static void rose_insert_socket(struct sock *sk)
{
	unsigned long flags;

	save_flags(flags);
	cli();

	sk->next = rose_list;
	rose_list  = sk;

	restore_flags(flags);
}

/*
 *	Find a socket that wants to accept the Call Request we just
 *	received.
 */
static struct sock *rose_find_listener(ax25_address *call)
{
	unsigned long flags;
	struct sock *s;

	save_flags(flags);
	cli();

	for (s = rose_list; s != NULL; s = s->next) {
		if (ax25cmp(&s->protinfo.rose->source_call, call) == 0 && s->protinfo.rose->source_ndigis == 0 && s->state == TCP_LISTEN) {
			restore_flags(flags);
			return s;
		}
	}

	for (s = rose_list; s != NULL; s = s->next) {
		if (ax25cmp(&s->protinfo.rose->source_call, &null_ax25_address) == 0 && s->state == TCP_LISTEN) {
			restore_flags(flags);
			return s;
		}
	}

	restore_flags(flags);
	return NULL;
}

/*
 *	Find a connected Rose socket given my LCI and device.
 */
struct sock *rose_find_socket(unsigned int lci, struct device *dev)
{
	struct sock *s;
	unsigned long flags;

	save_flags(flags);
	cli();

	for (s = rose_list; s != NULL; s = s->next) {
		if (s->protinfo.rose->lci == lci && s->protinfo.rose->neighbour->dev == dev) {
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
unsigned int rose_new_lci(struct device *dev)
{
	lci++;
	if (lci > 4095) lci = 1;

	while (rose_find_socket(lci, dev) != NULL) {
		lci++;
		if (lci > 4095) lci = 1;
	}

	return lci;
}

/*
 *	Deferred destroy.
 */
void rose_destroy_socket(struct sock *);

/*
 *	Handler for deferred kills.
 */
static void rose_destroy_timer(unsigned long data)
{
	rose_destroy_socket((struct sock *)data);
}

/*
 *	This is called from user mode and the timers. Thus it protects itself against
 *	interrupt users but doesn't worry about being called during work.
 *	Once it is removed from the queue no interrupt or bottom half will
 *	touch it and we are (fairly 8-) ) safe.
 */
void rose_destroy_socket(struct sock *sk)	/* Not static as it's used by the timer */
{
	struct sk_buff *skb;
	unsigned long flags;
	
	save_flags(flags);
	cli();
	
	del_timer(&sk->timer);
	
	rose_remove_socket(sk);
	rose_clear_queues(sk);		/* Flush the queues */
	
	while ((skb = skb_dequeue(&sk->receive_queue)) != NULL) {
		if (skb->sk != sk) {			/* A pending connection */
			skb->sk->dead = 1;	/* Queue the unaccepted socket for death */
			rose_set_timer(skb->sk);
			skb->sk->protinfo.rose->state = ROSE_STATE_0;
		}

		kfree_skb(skb, FREE_READ);
	}
	
	if (sk->wmem_alloc || sk->rmem_alloc) { /* Defer: outstanding buffers */
		init_timer(&sk->timer);
		sk->timer.expires  = jiffies + 10 * HZ;
		sk->timer.function = rose_destroy_timer;
		sk->timer.data     = (unsigned long)sk;
		add_timer(&sk->timer);
	} else {
		kfree_s(sk->protinfo.rose, sizeof(*sk->protinfo.rose));
		sk_free(sk);
	}

	restore_flags(flags);
}

/*
 *	Handling for system calls applied via the various interfaces to a
 *	Rose socket object.
 */
 
static int rose_fcntl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	return -EINVAL;
}

/*
 * dl1bke 960311: set parameters for existing Rose connections,
 *		  includes a KILL command to abort any connection.
 *		  VERY useful for debugging ;-)
 */
static int rose_ctl_ioctl(const unsigned int cmd, void *arg)
{
	struct rose_ctl_struct rose_ctl;
	struct sock *sk;
	unsigned long flags;
	struct device *dev;
	int err;
	
	if ((err = verify_area(VERIFY_READ, arg, sizeof(rose_ctl))) != 0)
		return err;

	copy_from_user(&rose_ctl, arg, sizeof(rose_ctl));

	if ((dev = rose_ax25_dev_get(rose_ctl.dev)) == NULL)
		return -EINVAL;
	
	if ((sk = rose_find_socket(rose_ctl.lci, dev)) == NULL)
		return -ENOTCONN;

	switch (rose_ctl.cmd) {
		case ROSE_KILL:
			rose_clear_queues(sk);
			rose_write_internal(sk, ROSE_CLEAR_REQUEST);
			sk->protinfo.rose->state = ROSE_STATE_0;
			sk->state                = TCP_CLOSE;
			sk->err                  = ENETRESET;
			if (!sk->dead)
				sk->state_change(sk);
			sk->dead                 = 1;
			rose_set_timer(sk);
	  		break;

	  	case ROSE_T0:
  			if (rose_ctl.arg < 1) 
  				return -EINVAL;
  			if (sk->protinfo.rose->neighbour != NULL) {
	  			save_flags(flags); cli();
	  			sk->protinfo.rose->neighbour->t0 = rose_ctl.arg * PR_SLOWHZ;
	  			restore_flags(flags);
	  		}
  			break;

	  	case ROSE_T1:
  			if (rose_ctl.arg < 1) 
  				return -EINVAL;
  			save_flags(flags); cli();
  			sk->protinfo.rose->t1 = rose_ctl.arg * PR_SLOWHZ;
  			restore_flags(flags);
  			break;

	  	case ROSE_T2:
	  		if (rose_ctl.arg < 1) 
	  			return -EINVAL;
	  		save_flags(flags); cli();
	  		sk->protinfo.rose->t2 = rose_ctl.arg * PR_SLOWHZ;
	  		restore_flags(flags);
	  		break;

	  	case ROSE_T3:
	  		if (rose_ctl.arg < 1) 
	  			return -EINVAL;
	  		save_flags(flags); cli();
	  		sk->protinfo.rose->t3 = rose_ctl.arg * PR_SLOWHZ;
	  		restore_flags(flags);
	  		break;

	  	case ROSE_IDLE:
	  		if (rose_ctl.arg < 1) 
	  			return -EINVAL;
	  		save_flags(flags); cli();
	  		sk->protinfo.rose->idle = rose_ctl.arg * 60 * PR_SLOWHZ;
	  		restore_flags(flags);
	  		break;

	  	default:
	  		return -EINVAL;
	  }
	  
	  return 0;
}

static int rose_setsockopt(struct socket *sock, int level, int optname,
	char *optval, int optlen)
{
	struct sock *sk;
	int err, opt;

	sk = (struct sock *)sock->data;
	
	if (level == SOL_SOCKET)
		return sock_setsockopt(sk, level, optname, optval, optlen);

	if (level != SOL_ROSE)
		return -EOPNOTSUPP;

	if (optval == NULL)
		return -EINVAL;

	if ((err = verify_area(VERIFY_READ, optval, sizeof(int))) != 0)
		return err;

	get_user(opt, (int *)optval);
	
	switch (optname) {
		case ROSE_T0:
			if (opt < 1)
				return -EINVAL;
			if (sk->protinfo.rose->neighbour != NULL)
				sk->protinfo.rose->neighbour->t0 = opt * PR_SLOWHZ;
			return 0;

		case ROSE_T1:
			if (opt < 1)
				return -EINVAL;
			sk->protinfo.rose->t1 = opt * PR_SLOWHZ;
			return 0;

		case ROSE_T2:
			if (opt < 1)
				return -EINVAL;
			sk->protinfo.rose->t2 = opt * PR_SLOWHZ;
			return 0;
			
		case ROSE_T3:
			if (opt < 1)
				return -EINVAL;
			sk->protinfo.rose->t3 = opt * PR_SLOWHZ;
			return 0;
			
		case ROSE_IDLE:
			if (opt < 1)
				return -EINVAL;
			sk->protinfo.rose->idle = opt * 60 * PR_SLOWHZ;
			return 0;
			
		case ROSE_HDRINCL:
			sk->protinfo.rose->hdrincl = opt ? 1 : 0;
			return 0;

		default:
			return -ENOPROTOOPT;
	}
}

static int rose_getsockopt(struct socket *sock, int level, int optname,
	char *optval, int *optlen)
{
	struct sock *sk;
	int val = 0;
	int err; 

	sk = (struct sock *)sock->data;
	
	if (level == SOL_SOCKET)
		return sock_getsockopt(sk, level, optname, optval, optlen);
	
	if (level != SOL_ROSE)
		return -EOPNOTSUPP;
	
	switch (optname) {
		case ROSE_T0:
			if (sk->protinfo.rose->neighbour != NULL)
				val = sk->protinfo.rose->neighbour->t0 / PR_SLOWHZ;
			else
				val = sysctl_rose_restart_request_timeout / PR_SLOWHZ;
			break;
			
		case ROSE_T1:
			val = sk->protinfo.rose->t1 / PR_SLOWHZ;
			break;
			
		case ROSE_T2:
			val = sk->protinfo.rose->t2 / PR_SLOWHZ;
			break;
			
		case ROSE_T3:
			val = sk->protinfo.rose->t3 / PR_SLOWHZ;
			break;
						
		case ROSE_IDLE:
			val = sk->protinfo.rose->idle / (PR_SLOWHZ * 60);
			break;
						
		case ROSE_HDRINCL:
			val = sk->protinfo.rose->hdrincl;
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

static int rose_listen(struct socket *sock, int backlog)
{
	struct sock *sk = (struct sock *)sock->data;

	if (sk->state != TCP_LISTEN) {
		sk->protinfo.rose->dest_ndigis = 0;
		memset(&sk->protinfo.rose->dest_addr, '\0', ROSE_ADDR_LEN);
		memset(&sk->protinfo.rose->dest_call, '\0', AX25_ADDR_LEN);
		memset(&sk->protinfo.rose->dest_digi, '\0', AX25_ADDR_LEN);
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

static int rose_create(struct socket *sock, int protocol)
{
	struct sock *sk;
	rose_cb *rose;

	if (sock->type != SOCK_SEQPACKET || protocol != 0)
		return -ESOCKTNOSUPPORT;

	if ((sk = sk_alloc(GFP_ATOMIC)) == NULL)
		return -ENOMEM;

	if ((rose = (rose_cb *)kmalloc(sizeof(*rose), GFP_ATOMIC)) == NULL) {
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
	sk->mtu           = ROSE_MTU;	/* 128 */
	sk->zapped        = 1;
	sk->window	  = ROSE_DEFAULT_WINDOW;

	sk->state_change = def_callback1;
	sk->data_ready   = def_callback2;
	sk->write_space  = def_callback1;
	sk->error_report = def_callback1;

	if (sock != NULL) {
		sock->data = (void *)sk;
		sk->sleep  = sock->wait;
	}

	skb_queue_head_init(&rose->ack_queue);
	skb_queue_head_init(&rose->frag_queue);

	rose->lci      = 0;

	rose->t1       = sysctl_rose_call_request_timeout;
	rose->t2       = sysctl_rose_reset_request_timeout;
	rose->t3       = sysctl_rose_clear_request_timeout;
	rose->idle     = sysctl_rose_no_activity_timeout;

	rose->timer    = 0;

	rose->va       = 0;
	rose->vr       = 0;
	rose->vs       = 0;
	rose->vl       = 0;

	rose->fraglen    = 0;
	rose->hdrincl    = 0;
	rose->state      = ROSE_STATE_0;
	rose->neighbour  = NULL;
	rose->device     = NULL;

	rose->source_ndigis = 0;
	rose->dest_ndigis   = 0;

	memset(&rose->source_addr, '\0', ROSE_ADDR_LEN);
	memset(&rose->dest_addr,   '\0', ROSE_ADDR_LEN);
	memset(&rose->source_call, '\0', AX25_ADDR_LEN);
	memset(&rose->dest_call,   '\0', AX25_ADDR_LEN);
	memset(&rose->source_digi, '\0', AX25_ADDR_LEN);
	memset(&rose->dest_digi,   '\0', AX25_ADDR_LEN);

	rose->sk          = sk;
	sk->protinfo.rose = rose;

	return 0;
}

static struct sock *rose_make_new(struct sock *osk)
{
	struct sock *sk;
	rose_cb *rose;

	if (osk->type != SOCK_SEQPACKET)
		return NULL;

	if ((sk = (struct sock *)sk_alloc(GFP_ATOMIC)) == NULL)
		return NULL;

	if ((rose = (rose_cb *)kmalloc(sizeof(*rose), GFP_ATOMIC)) == NULL) {
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

	skb_queue_head_init(&rose->ack_queue);
	skb_queue_head_init(&rose->frag_queue);

	rose->t1       = osk->protinfo.rose->t1;
	rose->t2       = osk->protinfo.rose->t2;
	rose->t3       = osk->protinfo.rose->t3;
	rose->idle     = osk->protinfo.rose->idle;

	rose->device   = osk->protinfo.rose->device;
	rose->hdrincl  = osk->protinfo.rose->hdrincl;
	rose->fraglen  = 0;

	rose->timer    = 0;

	rose->va       = 0;
	rose->vr       = 0;
	rose->vs       = 0;
	rose->vl       = 0;
	
	sk->protinfo.rose = rose;
	rose->sk          = sk;

	return sk;
}

static int rose_dup(struct socket *newsock, struct socket *oldsock)
{
	struct sock *sk = (struct sock *)oldsock->data;

	return rose_create(newsock, sk->protocol);
}

static int rose_release(struct socket *sock, struct socket *peer)
{
	struct sock *sk = (struct sock *)sock->data;

	if (sk == NULL) return 0;

	switch (sk->protinfo.rose->state) {

		case ROSE_STATE_0:
			sk->state     = TCP_CLOSE;
			sk->state_change(sk);
			sk->dead      = 1;
			rose_destroy_socket(sk);
			break;

		case ROSE_STATE_1:
			sk->protinfo.rose->state = ROSE_STATE_0;
			sk->state                = TCP_CLOSE;
			sk->state_change(sk);
			sk->dead                 = 1;
			rose_destroy_socket(sk);
			break;

		case ROSE_STATE_2:
			sk->protinfo.rose->state = ROSE_STATE_0;
			sk->state                = TCP_CLOSE;
			sk->state_change(sk);
			sk->dead                 = 1;
			rose_destroy_socket(sk);
			break;			

		case ROSE_STATE_3:
		case ROSE_STATE_4:
			rose_clear_queues(sk);
			rose_write_internal(sk, ROSE_CLEAR_REQUEST);
			sk->protinfo.rose->timer = sk->protinfo.rose->t3;
			sk->protinfo.rose->state = ROSE_STATE_2;
			sk->state                = TCP_CLOSE;
			sk->state_change(sk);
			sk->dead                 = 1;
			sk->destroy              = 1;
			break;

		default:
			break;
	}

	sock->data = NULL;	
	sk->socket = NULL;	/* Not used, but we should do this. **/

	return 0;
}

static int rose_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	struct sock *sk;
	struct sockaddr_rose *addr = (struct sockaddr_rose *)uaddr;
	struct device *dev;
	ax25_address *user, *source;
	
	sk = (struct sock *)sock->data;

	if (sk->zapped == 0)
		return -EIO;
		
	if (addr_len != sizeof(struct sockaddr_rose))
		return -EINVAL;

	if ((dev = rose_dev_get(&addr->srose_addr)) == NULL) {
		if (sk->debug)
			printk("Rose: bind failed: invalid address\n");
		return -EADDRNOTAVAIL;
	}

	source = &addr->srose_call;

	if ((user = ax25_findbyuid(current->euid)) == NULL) {
		if (ax25_uid_policy && !suser())
			return -EPERM;
		user = source;
	}

	sk->protinfo.rose->source_addr = addr->srose_addr;
	sk->protinfo.rose->source_call = *user;
	sk->protinfo.rose->device      = dev;

	if (addr->srose_ndigis == 1) {
		sk->protinfo.rose->source_ndigis = 1;
		sk->protinfo.rose->source_digi   = addr->srose_digi;
	}

	rose_insert_socket(sk);

	sk->zapped = 0;

	if (sk->debug)
		printk("Rose: socket is bound\n");

	return 0;
}

static int rose_connect(struct socket *sock, struct sockaddr *uaddr, int addr_len, int flags)
{
	struct sock *sk = (struct sock *)sock->data;
	struct sockaddr_rose *addr = (struct sockaddr_rose *)uaddr;
	ax25_address *user;
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

	if (addr_len != sizeof(struct sockaddr_rose))
		return -EINVAL;

	if ((sk->protinfo.rose->neighbour = rose_get_neigh(&addr->srose_addr)) == NULL)	
		return -ENETUNREACH;

	if (sk->zapped) {	/* Must bind first - autobinding in this may or may not work */
		sk->zapped = 0;

		if ((dev = rose_dev_first()) == NULL)
			return -ENETUNREACH;

		if ((user = ax25_findbyuid(current->euid)) == NULL)
			return -EINVAL;

		memcpy(&sk->protinfo.rose->source_addr, dev->dev_addr, ROSE_ADDR_LEN);
		sk->protinfo.rose->source_call = *user;
		sk->protinfo.rose->device      = dev;

		rose_insert_socket(sk);		/* Finish the bind */
	}

	sk->protinfo.rose->dest_addr = addr->srose_addr;
	sk->protinfo.rose->dest_call = addr->srose_call;
	if (addr->srose_ndigis == 1) {
		sk->protinfo.rose->dest_ndigis = 1;
		sk->protinfo.rose->dest_digi   = addr->srose_digi;
	}
	sk->protinfo.rose->lci       = rose_new_lci(sk->protinfo.rose->neighbour->dev);

	/* Move to connecting socket, start sending Connect Requests */
	sock->state   = SS_CONNECTING;
	sk->state     = TCP_SYN_SENT;

	sk->protinfo.rose->state = ROSE_STATE_1;
	sk->protinfo.rose->timer = sk->protinfo.rose->t1;
	rose_write_internal(sk, ROSE_CALL_REQUEST);

	rose_set_timer(sk);
	
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
	
static int rose_socketpair(struct socket *sock1, struct socket *sock2)
{
	return -EOPNOTSUPP;
}

static int rose_accept(struct socket *sock, struct socket *newsock, int flags)
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

static int rose_getname(struct socket *sock, struct sockaddr *uaddr,
	int *uaddr_len, int peer)
{
	struct sockaddr_rose *srose = (struct sockaddr_rose *)uaddr;
	struct sock *sk;
	
	sk = (struct sock *)sock->data;
	
	if (peer != 0) {
		if (sk->state != TCP_ESTABLISHED)
			return -ENOTCONN;
		srose->srose_family = AF_ROSE;
		srose->srose_ndigis = 0;
		srose->srose_addr   = sk->protinfo.rose->dest_addr;
		srose->srose_call   = sk->protinfo.rose->dest_call;
		if (sk->protinfo.rose->dest_ndigis == 1) {
			srose->srose_ndigis = 1;
			srose->srose_digi   = sk->protinfo.rose->dest_digi;
		}
		*uaddr_len = sizeof(struct sockaddr_rose);
	} else {
		srose->srose_family = AF_ROSE;
		srose->srose_ndigis = 0;
		srose->srose_addr   = sk->protinfo.rose->source_addr;
		srose->srose_call   = sk->protinfo.rose->source_call;
		if (sk->protinfo.rose->source_ndigis == 1) {
			srose->srose_ndigis = 1;
			srose->srose_digi   = sk->protinfo.rose->source_digi;
		}
		*uaddr_len = sizeof(struct sockaddr_rose);
	}

	return 0;
}
 
int rose_rx_call_request(struct sk_buff *skb, struct device *dev, struct rose_neigh *neigh, unsigned int lci)
{
	struct sock *sk;
	struct sock *make;	
	rose_cb rose;

	skb->sk = NULL;		/* Initially we don't know who it's for */

	/*
	 *	skb->data points to the rose frame start
	 */

	/*
	 * XXX This is an error.
	 */
	if (!rose_parse_facilities(skb, &rose)) {
		return 0;
	}
		
	sk = rose_find_listener(&rose.source_call);

	/*
	 * We can't accept the Call Request.
	 */
	if (sk == NULL || sk->ack_backlog == sk->max_ack_backlog || (make = rose_make_new(sk)) == NULL) {
		rose_transmit_clear_request(neigh, lci, 0x01);
		return 0;
	}

	skb->sk     = make;
	make->state = TCP_ESTABLISHED;

	make->protinfo.rose->lci           = lci;
	make->protinfo.rose->dest_addr     = rose.dest_addr;
	make->protinfo.rose->dest_call     = rose.dest_call;
	make->protinfo.rose->dest_ndigis   = rose.dest_ndigis;
	make->protinfo.rose->dest_digi     = rose.dest_digi;
	make->protinfo.rose->source_addr   = rose.source_addr;
	make->protinfo.rose->source_call   = rose.source_call;
	make->protinfo.rose->source_ndigis = rose.source_ndigis;
	make->protinfo.rose->source_digi   = rose.source_digi;
	make->protinfo.rose->neighbour     = neigh;
	make->protinfo.rose->device        = dev;
	
	rose_write_internal(make, ROSE_CALL_ACCEPTED);

	make->protinfo.rose->condition = 0x00;
	make->protinfo.rose->vs        = 0;
	make->protinfo.rose->va        = 0;
	make->protinfo.rose->vr        = 0;
	make->protinfo.rose->vl        = 0;
	make->protinfo.rose->state     = ROSE_STATE_3;
	sk->ack_backlog++;
	make->pair = sk;

	rose_insert_socket(make);

	skb_queue_head(&sk->receive_queue, skb);

	rose_set_timer(make);

	if (!sk->dead)
		sk->data_ready(sk, skb->len);

	return 1;
}

static int rose_sendmsg(struct socket *sock, struct msghdr *msg, int len, int noblock, int flags)
{
	struct sock *sk = (struct sock *)sock->data;
	struct sockaddr_rose *usrose = (struct sockaddr_rose *)msg->msg_name;
	int err;
	struct sockaddr_rose srose;
	struct sk_buff *skb;
	unsigned char *asmptr;
	int size;
	
	if (sk->err)
		return sock_error(sk);

	if (flags)
		return -EINVAL;

	if (sk->zapped)
		return -EADDRNOTAVAIL;

	if (sk->protinfo.rose->device == NULL)
		return -ENETUNREACH;
		
	if (usrose) {
		if (msg->msg_namelen < sizeof(srose))
			return -EINVAL;
		srose = *usrose;
		if (rosecmp(&sk->protinfo.rose->dest_addr, &srose.srose_addr) != 0 ||
		    ax25cmp(&sk->protinfo.rose->dest_call, &srose.srose_call) != 0)
			return -EISCONN;
		if (srose.srose_ndigis == 1 && sk->protinfo.rose->dest_ndigis == 1) {
			if (ax25cmp(&sk->protinfo.rose->dest_digi, &srose.srose_digi) != 0)
				return -EISCONN;
		}
		if (srose.srose_family != AF_ROSE)
			return -EINVAL;
	} else {
		if (sk->state != TCP_ESTABLISHED)
			return -ENOTCONN;

		srose.srose_family = AF_ROSE;
		srose.srose_addr   = sk->protinfo.rose->dest_addr;
		srose.srose_call   = sk->protinfo.rose->dest_call;
		srose.srose_ndigis = 0;

		if (sk->protinfo.rose->dest_ndigis == 1) {
			srose.srose_ndigis = 1;
			srose.srose_digi   = sk->protinfo.rose->dest_digi;
		}
	}
	
	if (sk->debug)
		printk("Rose: sendto: Addresses built.\n");

	/* Build a packet */
	if (sk->debug)
		printk("Rose: sendto: building packet.\n");

	size = len + AX25_BPQ_HEADER_LEN + AX25_MAX_HEADER_LEN + ROSE_MIN_LEN;

	if ((skb = sock_alloc_send_skb(sk, size, 0, 0, &err)) == NULL)
		return err;

	skb->sk   = sk;
	skb->free = 1;
	skb->arp  = 1;

	skb_reserve(skb, size - len);
	
	/*
	 *	Push down the Rose header
	 */

	asmptr = skb_push(skb, ROSE_MIN_LEN);

	if (sk->debug)
		printk("Building Rose Header.\n");

	/* Build a Rose Transport header */

	*asmptr++ = ((sk->protinfo.rose->lci >> 8) & 0x0F) | GFI;
	*asmptr++ = (sk->protinfo.rose->lci >> 0) & 0xFF;
	*asmptr++ = ROSE_DATA;
	
	if (sk->debug)
		printk("Built header.\n");

	/*
	 *	Put the data on the end
	 */

	skb->h.raw = skb_put(skb, len);

	asmptr = skb->h.raw;
	
	if (sk->debug)
		printk("Rose: Appending user data\n");

	/* User data follows immediately after the Rose transport header */
	memcpy_fromiovec(asmptr, msg->msg_iov, len);

	if (sk->debug)
		printk("Rose: Transmitting buffer\n");

	if (sk->state != TCP_ESTABLISHED) {
		kfree_skb(skb, FREE_WRITE);
		return -ENOTCONN;
	}

	rose_output(sk, skb);	/* Shove it onto the queue */

	return len;
}


static int rose_recvmsg(struct socket *sock, struct msghdr *msg, int size, int noblock,
		   int flags, int *addr_len)
{
	struct sock *sk = (struct sock *)sock->data;
	struct sockaddr_rose *srose = (struct sockaddr_rose *)msg->msg_name;
	int copied;
	struct sk_buff *skb;
	int er;

	if (sk->err)
		return sock_error(sk);
	
	if (addr_len != NULL)
		*addr_len = sizeof(*srose);

	/*
	 * This works for seqpacket too. The receiver has ordered the queue for
	 * us! We do one quick check first though
	 */
	if (sk->state != TCP_ESTABLISHED)
		return -ENOTCONN;

	/* Now we can treat all alike */
	if ((skb = skb_recv_datagram(sk, flags, noblock, &er)) == NULL)
		return er;

	if (!sk->protinfo.rose->hdrincl) {
		skb_pull(skb, ROSE_MIN_LEN);
		skb->h.raw = skb->data;
	}

	copied = (size < skb->len) ? size : skb->len;
	skb_copy_datagram_iovec(skb, 0, msg->msg_iov, copied);
	
	if (srose != NULL) {
		struct sockaddr_rose addr;
		
		addr.srose_family = AF_ROSE;
		addr.srose_addr   = sk->protinfo.rose->dest_addr;
		addr.srose_call   = sk->protinfo.rose->dest_call;
		addr.srose_ndigis = 0;

		if (sk->protinfo.rose->dest_ndigis == 1) {
			addr.srose_ndigis = 1;
			addr.srose_digi   = sk->protinfo.rose->dest_digi;
		}

		*srose = addr;

		*addr_len = sizeof(*srose);
	}

	skb_free_datagram(sk, skb);

	return copied;
}

static int rose_shutdown(struct socket *sk, int how)
{
	return -EOPNOTSUPP;
}

static int rose_select(struct socket *sock , int sel_type, select_table *wait)
{
	struct sock *sk = (struct sock *)sock->data;

	return datagram_select(sk, sel_type, wait);
}

static int rose_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
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
			return rose_rt_ioctl(cmd, (void *)arg);

 		case SIOCRSCTLCON:
 			if (!suser()) return -EPERM;
 			return rose_ctl_ioctl(cmd, (void *)arg);
 
 		default:
			return dev_ioctl(cmd, (void *)arg);
	}

	/*NOTREACHED*/
	return 0;
}

static int rose_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	struct sock *s;
	struct device *dev;
	const char *devname, *callsign;
	int len = 0;
	off_t pos = 0;
	off_t begin = 0;
  
	cli();

	len += sprintf(buffer, "dest_addr  dest_call dest_digi src_addr   src_call  src_digi  dev   lci st vs vr va   t  t1  t2  t3 Snd-Q Rcv-Q\n");

	for (s = rose_list; s != NULL; s = s->next) {
		if ((dev = s->protinfo.rose->device) == NULL)
			devname = "???";
		else
			devname = dev->name;

		len += sprintf(buffer + len, "%-10s %-9s ",
			rose2asc(&s->protinfo.rose->dest_addr),
			ax2asc(&s->protinfo.rose->dest_call));
		len += sprintf(buffer + len, "%-9s ",
			ax2asc(&s->protinfo.rose->dest_digi));

		if (ax25cmp(&s->protinfo.rose->source_call, &null_ax25_address) == 0)
			callsign = "??????-?";
		else
			callsign = ax2asc(&s->protinfo.rose->source_call);

		len += sprintf(buffer + len, "%-10s %-9s ",
			rose2asc(&s->protinfo.rose->source_addr),
			callsign);
		len += sprintf(buffer + len, "%-9s %-5s %3.3X  %d  %d  %d  %d %3d %3d %3d %3d %5d %5d\n",
			ax2asc(&s->protinfo.rose->source_digi),
			devname,  s->protinfo.rose->lci & 0x0FFF,
			s->protinfo.rose->state,
			s->protinfo.rose->vs, s->protinfo.rose->vr, s->protinfo.rose->va,
			s->protinfo.rose->timer / PR_SLOWHZ,
			s->protinfo.rose->t1    / PR_SLOWHZ,
			s->protinfo.rose->t2    / PR_SLOWHZ,
			s->protinfo.rose->t3    / PR_SLOWHZ,
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

struct proto_ops rose_proto_ops = {
	AF_ROSE,
	
	rose_create,
	rose_dup,
	rose_release,
	rose_bind,
	rose_connect,
	rose_socketpair,
	rose_accept,
	rose_getname,
	rose_select,
	rose_ioctl,
	rose_listen,
	rose_shutdown,
	rose_setsockopt,
	rose_getsockopt,
	rose_fcntl,
	rose_sendmsg,
	rose_recvmsg
};

struct notifier_block rose_dev_notifier = {
	rose_device_event,
	0
};

void rose_proto_init(struct net_proto *pro)
{
	sock_register(rose_proto_ops.family, &rose_proto_ops);
	register_netdevice_notifier(&rose_dev_notifier);
	printk(KERN_INFO "G4KLX Rose for Linux. Version 0.1 for AX25.033 Linux 2.1\n");

	if (!ax25_protocol_register(AX25_P_ROSE, rose_route_frame))
		printk(KERN_ERR "Rose unable to register protocol with AX.25\n");
	if (!ax25_linkfail_register(rose_link_failed))
		printk(KERN_ERR "Rose unable to register linkfail handler with AX.25\n");

	rose_register_sysctl();

#ifdef CONFIG_PROC_FS
	proc_net_register(&(struct proc_dir_entry) {
		PROC_NET_RS, 4, "rose",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_net_inode_operations, 
		rose_get_info
	});
	proc_net_register(&(struct proc_dir_entry) {
		PROC_NET_RS_NEIGH, 10, "rose_neigh",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_net_inode_operations, 
		rose_neigh_get_info
	});
	proc_net_register(&(struct proc_dir_entry) {
		PROC_NET_RS_NODES, 10, "rose_nodes",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_net_inode_operations, 
		rose_nodes_get_info
	});

	proc_net_register(&(struct proc_dir_entry) {
		PROC_NET_RS_ROUTES, 11, "rose_routes",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_net_inode_operations, 
		rose_routes_get_info
	});
#endif	
}

#endif
