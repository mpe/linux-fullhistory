/*
 *	AX.25 release 029
 *
 *	This is ALPHA test software. This code may break your machine, randomly fail to work with new 
 *	releases, misbehave and/or generally screw up. It might even work. 
 *
 *	This code REQUIRES 1.2.1 or higher/ NET3.029
 *
 *	This module:
 *		This module is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	History
 *	AX.25 006	Alan(GW4PTS)		Nearly died of shock - its working 8-)
 *	AX.25 007	Alan(GW4PTS)		Removed the silliest bugs
 *	AX.25 008	Alan(GW4PTS)		Cleaned up, fixed a few state machine problems, added callbacks
 *	AX.25 009	Alan(GW4PTS)		Emergency patch kit to fix memory corruption
 * 	AX.25 010	Alan(GW4PTS)		Added RAW sockets/Digipeat.
 *	AX.25 011	Alan(GW4PTS)		RAW socket and datagram fixes (thanks) - Raw sendto now gets PID right
 *						datagram sendto uses correct target address.
 *	AX.25 012	Alan(GW4PTS)		Correct incoming connection handling, send DM to failed connects.
 *						Use skb->data not skb+1. Support sk->priority correctly.
 *						Correct receive on SOCK_DGRAM.
 *	AX.25 013	Alan(GW4PTS)		Send DM to all unknown frames, missing initialiser fixed
 *						Leave spare SSID bits set (DAMA etc) - thanks for bug report,
 *						removed device registration (its not used or needed). Clean up for
 *						gcc 2.5.8. PID to AX25_P_
 *	AX.25 014	Alan(GW4PTS)		Cleanup and NET3 merge
 *	AX.25 015	Alan(GW4PTS)		Internal test version.
 *	AX.25 016	Alan(GW4PTS)		Semi Internal version for PI card
 *						work.
 *	AX.25 017	Alan(GW4PTS)		Fixed some small bugs reported by
 *						G4KLX
 *	AX.25 018	Alan(GW4PTS)		Fixed a small error in SOCK_DGRAM
 *	AX.25 019	Alan(GW4PTS)		Clean ups for the non INET kernel and device ioctls in AX.25
 *	AX.25 020	Jonathan(G4KLX)		/proc support and other changes.
 *	AX.25 021	Alan(GW4PTS)		Added AX25_T1, AX25_N2, AX25_T3 as requested.
 *	AX.25 022	Jonathan(G4KLX)		More work on the ax25 auto router and /proc improved (again)!
 *			Alan(GW4PTS)		Added TIOCINQ/OUTQ
 *	AX.25 023	Alan(GW4PTS)		Fixed shutdown bug
 *	AX.25 023	Alan(GW4PTS)		Linus changed timers
 *	AX.25 024	Alan(GW4PTS)		Small bug fixes
 *	AX.25 025	Alan(GW4PTS)		More fixes, Linux 1.1.51 compatibility stuff, timers again!
 *	AX.25 026	Alan(GW4PTS)		Small state fix.
 *	AX.25 027	Alan(GW4PTS)		Socket close crash fixes.
 *	AX.25 028	Alan(GW4PTS)		Callsign control including settings per uid.
 *						Small bug fixes.
 *						Protocol set by sockets only.
 *						Small changes to allow for start of NET/ROM layer.
 *	AX.25 028a	Jonathan(G4KLX)		Changes to state machine.
 *	AX.25 028b	Jonathan(G4KLX)		Extracted ax25 control block
 *						from sock structure.
 *	AX.25 029	Alan(GW4PTS)		Combined 028b and some KA9Q code
 *			Jonathan(G4KLX)		and removed all the old Berkeley, added IP mode registration.
 *			Darryl(G7LED)		stuff. Cross-port digipeating. Minor fixes and enhancements.
 *			Alan(GW4PTS)		Missed suser() on axassociate checks
 *
 *	To do:
 *		Support use as digipeater, including an on/off ioctl
 *		Restructure the ax25_rcv code to be cleaner/faster and
 *		copy only when needed.
 *		Consider better arbitary protocol support.
 *		Fix non-blocking connect failure.
 */
 
#include <linux/config.h>
#ifdef CONFIG_AX25
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
#include <linux/skbuff.h>
#include <net/sock.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/termios.h>	/* For TIOCINQ/OUTQ */
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/notifier.h>

#include <net/ip.h>
#include <net/arp.h>

#define	CONFIG_AX25_XDIGI	/* Cross port (band) digi stuff */

/**********************************************************************************************************************\
*														       *
*						Handlers for the socket list.					       *
*														       *
\**********************************************************************************************************************/

static ax25_cb *volatile ax25_list = NULL;

/*
 *	ax25 -> ascii conversion
 */
char *ax2asc(ax25_address *a)
{
	static char buf[11];
	char c, *s;
	int n;

	for (n = 0, s = buf; n < 6; n++)
	{
		c = (a->ax25_call[n] >> 1) & 0x7F;

		if (c != ' ') *s++ = c;
	}
	
	*s++ = '-';

	if ((n = ((a->ax25_call[6] >> 1) & 0x0F)) > 9)
	{
		*s++ = '1';
		n -= 10;
	}
	
	*s++ = n + '0';
	*s++ = '\0';

	return(buf);

}

/*
 *	Compare two ax.25 addresses
 */
int ax25cmp(ax25_address *a, ax25_address *b)
{
	int ct = 0;

	while (ct < 6) {
		if ((a->ax25_call[ct] & 0xFE) != (b->ax25_call[ct] & 0xFE))	/* Clean off repeater bits */
			return 1;
		ct++;
	}

 	if ((a->ax25_call[ct] & 0x1E) == (b->ax25_call[ct] & 0x1E))	/* SSID without control bit */
 		return 0;

 	return 2;			/* Partial match */
}

/*
 *	Socket removal during an interrupt is now safe.
 */
static void ax25_remove_socket(ax25_cb *ax25)
{
	ax25_cb *s;
	unsigned long flags;
	
	save_flags(flags);
	cli();

	if ((s = ax25_list) == ax25) {
		ax25_list = s->next;
		restore_flags(flags);
		return;
	}

	while (s != NULL && s->next != NULL) {
		if (s->next == ax25) {
			s->next = ax25->next;
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
static void ax25_kill_by_device(struct device *dev)
{
	ax25_cb *s;
	
	for (s = ax25_list; s != NULL; s = s->next) {
		if (s->device == dev) {
			s->device = NULL;
			if (s->sk != NULL) {
				s->sk->state = TCP_CLOSE;
				s->sk->err   = ENETUNREACH;
				if (!s->sk->dead)
					s->sk->state_change(s->sk);
				s->sk->dead  = 1;
			}
		}
	}
	
	ax25_rt_device_down(dev);
}

/*
 *	Handle device status changes.
 */
static int ax25_device_event(unsigned long event, void *ptr)
{
	if (event != NETDEV_DOWN)
		return NOTIFY_DONE;
		
	ax25_kill_by_device(ptr);
	
	return NOTIFY_DONE;
}

/*
 *	Add a socket to the bound sockets list.
 */
static void ax25_insert_socket(ax25_cb *ax25)
{
	unsigned long flags;

	save_flags(flags);
	cli();

	ax25->next = ax25_list;
	ax25_list  = ax25;

	restore_flags(flags);
}

/*
 *	Find a socket that wants to accept the SABM we just
 *	received.
 */
static struct sock *ax25_find_listener(ax25_address *addr, struct device *dev, int type)
{
	unsigned long flags;
	ax25_cb *s;

	save_flags(flags);
	cli();

	for (s = ax25_list; s != NULL; s = s->next) {
		if (s->sk != NULL && ax25cmp(&s->source_addr, addr) == 0 && s->sk->type == type && s->sk->state == TCP_LISTEN) {
			/* If device is null we match any device */
			if (s->device == NULL || s->device == dev) {
				restore_flags(flags);
				return s->sk;
			}
		}
	}

	restore_flags(flags);
	return NULL;
}

/*
 *	Find an AX.25 socket given both ends.
 */
static struct sock *ax25_find_socket(ax25_address *my_addr, ax25_address *dest_addr, int type)
{
	ax25_cb *s;
	unsigned long flags;

	save_flags(flags);
	cli();

	for (s = ax25_list; s != NULL; s = s->next) {
		if (s->sk != NULL && ax25cmp(&s->source_addr, my_addr) == 0 && ax25cmp(&s->dest_addr, dest_addr) == 0 && s->sk->type == type) {
			restore_flags(flags);
			return s->sk;
		}
	}

	restore_flags(flags);

	return NULL;
}

/*
 *	Find an AX.25 control block given both ends. It will only pick up
 *	floating AX.25 control blocks or non Raw socket bound control blocks.
 */
static ax25_cb *ax25_find_cb(ax25_address *my_addr, ax25_address *dest_addr, struct device *dev)
{
	ax25_cb *s;
	unsigned long flags;

	save_flags(flags);
	cli();

	for (s = ax25_list; s != NULL; s = s->next) {
		if (s->sk != NULL && s->sk->type != SOCK_SEQPACKET)
			continue;
		if (ax25cmp(&s->source_addr, my_addr) == 0 && ax25cmp(&s->dest_addr, dest_addr) == 0 && s->device == dev) {
			restore_flags(flags);
			return s;
		}
	}

	restore_flags(flags);

	return NULL;
}

/*
 *	Look for any matching address - RAW sockets can bind to arbitary names
 */
static struct sock *ax25_addr_match(ax25_address *addr)
{
	unsigned long flags;
	ax25_cb *s;

	save_flags(flags);
	cli();

	for (s = ax25_list; s != NULL; s = s->next) {
		if (s->sk != NULL && ax25cmp(&s->source_addr, addr) == 0 && s->sk->type == SOCK_RAW) {
			restore_flags(flags);
			return s->sk;
		}
	}

	restore_flags(flags);

	return NULL;
}

static void ax25_send_to_raw(struct sock *sk, struct sk_buff *skb, int proto)
{
	struct sk_buff *copy;
	
	while (sk != NULL) {
		if (sk->type == SOCK_RAW && sk->protocol == proto && sk->rmem_alloc <= sk->rcvbuf) {
			if ((copy = skb_clone(skb, GFP_ATOMIC)) == NULL)
				return;

			copy->sk = sk;
			sk->rmem_alloc += copy->mem_len;
			skb_queue_tail(&sk->receive_queue, copy);
			if (!sk->dead)
				sk->data_ready(sk, skb->len - 2);
		}

		sk = sk->next;
	}
}	

/*
 *	Deferred destroy.
 */
void ax25_destory_socket(ax25_cb *);

/*
 *	Handler for deferred kills.
 */
static void ax25_destroy_timer(unsigned long data)
{
	ax25_destroy_socket((ax25_cb *)data);
}

/*
 *	This is called from user mode and the timers. Thus it protects itself against
 *	interrupt users but doesn't worry about being called during work.
 *	Once it is removed from the queue no interrupt or bottom half will
 *	touch it and we are (fairly 8-) ) safe.
 */
void ax25_destroy_socket(ax25_cb *ax25)	/* Not static as its used by the timer */
{
	struct sk_buff *skb;
	unsigned long flags;
	
	save_flags(flags);
	cli();
	
	del_timer(&ax25->timer);
	
	ax25_remove_socket(ax25);
	ax25_clear_tx_queue(ax25);	/* Flush the send queue */
	
	if (ax25->sk != NULL) {
		while ((skb = skb_dequeue(&ax25->sk->receive_queue)) != NULL) {
			if (skb->sk != ax25->sk) {			/* A pending connection */
				skb->sk->dead = 1;	/* Queue the unaccepted socket for death */
				ax25_set_timer(skb->sk->ax25);
				skb->sk->ax25->state = AX25_STATE_0;
			}

			kfree_skb(skb, FREE_READ);
		}
	}
	
	if (ax25->digipeat != NULL) {
		kfree_s(ax25->digipeat, sizeof(ax25_digi));
		ax25->digipeat = NULL;
	}

	if (ax25->sk != NULL) {
		if (ax25->sk->wmem_alloc || ax25->sk->rmem_alloc) { /* Defer: outstanding buffers */
			init_timer(&ax25->timer);
			ax25->timer.expires  = 10 * HZ;
			ax25->timer.function = ax25_destroy_timer;
			ax25->timer.data     = (unsigned long)ax25;
			add_timer(&ax25->timer);
		} else {
			kfree_s(ax25->sk, sizeof(*ax25->sk));
			kfree_s(ax25, sizeof(*ax25));
		}
	} else {
		kfree_s(ax25, sizeof(*ax25));
	}

	restore_flags(flags);
}

/*
 *	Callsign/UID mapper. This is in kernel space for security on multi-amateur machines.
 */

ax25_uid_assoc *ax25_uid_list;

int ax25_uid_policy = 0;

ax25_address *ax25_findbyuid(uid_t uid)
{
	ax25_uid_assoc *a;
	
	for (a = ax25_uid_list; a != NULL; a = a->next) {
		if (a->uid == uid)
			return &a->call;
	}

	return NULL;
}

static int ax25_uid_ioctl(int cmd, struct sockaddr_ax25 *sax)
{
	ax25_uid_assoc *a;
	
	switch (cmd) {
		case SIOCAX25GETUID:
			for (a = ax25_uid_list; a != NULL; a = a->next) {
				if (ax25cmp(&sax->sax25_call, &a->call) == 0)
					return a->uid;
			}
			return -ENOENT;
		case SIOCAX25ADDUID:
			if(!suser())
				return -EPERM;
			if (ax25_findbyuid(sax->sax25_uid))
				return -EEXIST;
			a = (ax25_uid_assoc *)kmalloc(sizeof(*a), GFP_KERNEL);
			a->uid  = sax->sax25_uid;
			a->call = sax->sax25_call;
			a->next = ax25_uid_list;
			ax25_uid_list = a;
			return 0;
		case SIOCAX25DELUID:
		{
			ax25_uid_assoc **l;
			
			if(!suser())
				return -EPERM;
			l = &ax25_uid_list;
			while ((*l) != NULL) {
				if (ax25cmp(&((*l)->call), &(sax->sax25_call)) == 0) {
					a = *l;
					*l = (*l)->next;
					kfree_s(a, sizeof(*a));
					return 0;
				}
				
				l = &((*l)->next);
			}
			return -ENOENT;
		}
	}

	return -EINVAL;	/*NOTREACHED */
}	

/*
 * Create an empty AX.25 control block.
 */
static ax25_cb *ax25_create_cb(void)
{
	ax25_cb *ax25;

	if ((ax25 = (ax25_cb *)kmalloc(sizeof(*ax25), GFP_ATOMIC)) == NULL)
		return NULL;

	skb_queue_head_init(&ax25->write_queue);
	skb_queue_head_init(&ax25->ack_queue);

	init_timer(&ax25->timer);

	ax25->rtt     = DEFAULT_T1;
	ax25->t1      = DEFAULT_T1;
	ax25->t2      = DEFAULT_T2;
	ax25->n2      = DEFAULT_N2;
	ax25->t3      = DEFAULT_T3;

	ax25->condition = 0x00;
	ax25->t1timer   = 0;
	ax25->t2timer   = 0;
	ax25->t3timer   = 0;
	ax25->n2count   = 0;

	ax25->va      = 0;
	ax25->vr      = 0;
	ax25->vs      = 0;

	ax25->window   = DEFAULT_WINDOW;
	ax25->device   = NULL;
	ax25->digipeat = NULL;
	ax25->sk       = NULL;

	ax25->state    = AX25_STATE_0;

	memset(&ax25->dest_addr,   '\0', sizeof(ax25_address));
	memset(&ax25->source_addr, '\0', sizeof(ax25_address));

	return ax25;
}

int ax25_send_frame(struct sk_buff *skb, ax25_address *src, ax25_address *dest, struct device *dev)
{
	ax25_cb *ax25;

	if (skb == NULL)
		return 0;

	skb->h.raw = skb->data + 15;

	/*
	 * Look for an existing connection.
	 */
	for (ax25 = ax25_list; ax25 != NULL; ax25 = ax25->next) {
		if (ax25->sk != NULL && ax25->sk->type != SOCK_SEQPACKET)
			continue;

		if (ax25cmp(&ax25->source_addr, src) == 0 && ax25cmp(&ax25->dest_addr, dest) == 0 && ax25->device == dev) {
			ax25_output(ax25, skb);
			return 1;		/* It already existed */
		}
	}

	if ((ax25 = ax25_create_cb()) == NULL)
		return 0;

	ax25->device = dev;

	memcpy(&ax25->source_addr, src,  sizeof(ax25_address));
	memcpy(&ax25->dest_addr,   dest, sizeof(ax25_address));

	ax25_establish_data_link(ax25);
	ax25_insert_socket(ax25);

	ax25->state = AX25_STATE_1;

	ax25_set_timer(ax25);

	ax25_output(ax25, skb);
			
	return 1;			/* We had to create it */	
}

/*******************************************************************************************************************\
*														    *
*		Routing rules for AX.25: Basically iterate over the active interfaces 				    *
*														    *
\*******************************************************************************************************************/

struct device *ax25rtr_get_dev(ax25_address *addr)
{
	struct device *dev;
	
	for (dev = dev_base; dev != NULL; dev = dev->next) {
		if ((dev->flags & IFF_UP) && dev->type == ARPHRD_AX25) { /* Active kiss ax25 mode */ 
			if (ax25cmp(addr, (ax25_address *)dev->dev_addr) == 0)
				return dev;
		}
	}

	return NULL;
}

/*******************************************************************************************************************\
*													            *
*	      Handling for system calls applied via the various interfaces to an AX25 socket object		    *
*														    *
\*******************************************************************************************************************/
 
static int ax25_fcntl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	switch(cmd)
	{
		default:
			return(-EINVAL);
	}
}

static int ax25_setsockopt(struct socket *sock, int level, int optname,
	char *optval, int optlen)
{
	struct sock *sk;
	int err, opt;

	sk = (struct sock *)sock->data;
	
	if (level == SOL_SOCKET)
		return sock_setsockopt(sk, level, optname, optval, optlen);

	if (level != SOL_AX25)
		return -EOPNOTSUPP;

	if (optval == NULL)
		return -EINVAL;

	if ((err = verify_area(VERIFY_READ, optval, sizeof(int))) != 0)
		return err;

	opt = get_fs_long((unsigned long *)optval);
	
	switch (optname) {
		case AX25_WINDOW:
			if (opt < 1 || opt > 7)
				return -EINVAL;
			sk->ax25->window = opt;
			return 0;
			
		case AX25_T1:
			if (opt < 1)
				return -EINVAL;
			sk->ax25->t1 = opt * PR_SLOWHZ;
			return 0;

		case AX25_T2:
			if (opt < 1)
				return -EINVAL;
			sk->ax25->t2 = opt * PR_SLOWHZ;
			return 0;
			
		case AX25_N2:
			if (opt < 1 || opt > 31)
				return -EINVAL;
			sk->ax25->n2 = opt;
			return 0;
			
		case AX25_T3:
			if (opt < 1)
				return -EINVAL;
			sk->ax25->t3 = opt * PR_SLOWHZ;
			return 0;
	
		default:
			return -ENOPROTOOPT;
	}
}

static int ax25_getsockopt(struct socket *sock, int level, int optname,
	char *optval, int *optlen)
{
	struct sock *sk;
	int val = 0;
	int err; 

	sk = (struct sock *)sock->data;
	
	if (level == SOL_SOCKET)
		return sock_getsockopt(sk, level, optname, optval, optlen);
	
	if (level != SOL_AX25)
		return -EOPNOTSUPP;
	
	switch (optname) {
		case AX25_WINDOW:
			val = sk->ax25->window;
			break;

		case AX25_T1:
			val = sk->ax25->t1 / PR_SLOWHZ;
			break;
			
		case AX25_T2:
			val = sk->ax25->t2 / PR_SLOWHZ;
			break;
			
		case AX25_N2:
			val = sk->ax25->n2;
			break;
						
		case AX25_T3:
			val = sk->ax25->t3 / PR_SLOWHZ;
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

static int ax25_listen(struct socket *sock, int backlog)
{
	struct sock *sk = (struct sock *)sock->data;

	if (sk->type == SOCK_SEQPACKET && sk->state != TCP_LISTEN) {
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

static int ax25_create(struct socket *sock, int protocol)
{
	struct sock *sk;
	ax25_cb *ax25;

	if ((sk = (struct sock *)kmalloc(sizeof(*sk), GFP_ATOMIC)) == NULL)
		return -ENOMEM;

	if ((ax25 = ax25_create_cb()) == NULL) {
		kfree_s(sk, sizeof(*sk));
		return -ENOMEM;
	}

	sk->type = sock->type;

	switch (sock->type) {
		case SOCK_DGRAM:
		case SOCK_SEQPACKET:
			if (protocol == 0)
				protocol = AX25_P_TEXT;
			break;
		case SOCK_RAW:
			break;
		default:
			kfree_s((void *)sk, sizeof(*sk));
			kfree_s((void *)ax25, sizeof(*ax25));
			return -ESOCKTNOSUPPORT;
	}

	skb_queue_head_init(&sk->receive_queue);
	skb_queue_head_init(&sk->write_queue);
	skb_queue_head_init(&sk->back_log);

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
	sk->mtu           = AX25_MTU;	/* 256 */
	sk->zapped        = 1;

	sk->state_change = def_callback1;
	sk->data_ready   = def_callback2;
	sk->write_space  = def_callback1;
	sk->error_report = def_callback1;

	if (sock != NULL) {
		sock->data = (void *)sk;
		sk->sleep  = sock->wait;
	}

	ax25->sk = sk;
	sk->ax25 = ax25;
	
	return 0;
}

static struct sock *ax25_make_new(struct sock *osk, struct device *dev)
{
	struct sock *sk;
	ax25_cb *ax25;

	if ((sk = (struct sock *)kmalloc(sizeof(*sk), GFP_ATOMIC)) == NULL)
		return NULL;

	if ((ax25 = ax25_create_cb()) == NULL) {
		kfree_s(sk, sizeof(*sk));
		return NULL;
	}

	sk->type   = osk->type;
	sk->socket = osk->socket;

	switch(osk->type)
	{
		case SOCK_DGRAM:
			break;
		case SOCK_SEQPACKET:
			break;
		default:
			kfree_s((void *)sk, sizeof(*sk));
			kfree_s((void *)ax25, sizeof(*ax25));
			return NULL;
	}

	skb_queue_head_init(&sk->receive_queue);
	skb_queue_head_init(&sk->write_queue);
	skb_queue_head_init(&sk->back_log);

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

	ax25->rtt    = osk->ax25->rtt;
	ax25->t1     = osk->ax25->t1;
	ax25->t2     = osk->ax25->t2;
	ax25->t3     = osk->ax25->t3;
	ax25->n2     = osk->ax25->n2;

	ax25->window = osk->ax25->window;
	ax25->device = dev;

	memcpy(&ax25->source_addr, &osk->ax25->source_addr, sizeof(ax25_address));
	
	if (osk->ax25->digipeat != NULL) {
		if ((ax25->digipeat = (ax25_digi *)kmalloc(sizeof(ax25_digi), GFP_ATOMIC)) == NULL) {
			kfree_s(sk, sizeof(*sk));
			kfree_s(ax25, sizeof(*ax25));
			return NULL;
		}
	}

	sk->ax25 = ax25;
	ax25->sk = sk;

	return sk;
}

static int ax25_dup(struct socket *newsock, struct socket *oldsock)
{
	struct sock *sk = (struct sock *)oldsock->data;

	return ax25_create(newsock, sk->protocol);
}

static int ax25_release(struct socket *sock, struct socket *peer)
{
	struct sock *sk = (struct sock *)sock->data;

	if (sk == NULL) return 0;

	if (sk->type == SOCK_SEQPACKET) {
		switch (sk->ax25->state) {
			case AX25_STATE_0:
				sk->dead        = 1;
				sk->state_change(sk);
				ax25_destroy_socket(sk->ax25);
				break;

			case AX25_STATE_1:
				ax25_send_control(sk->ax25, DISC | PF, C_RESPONSE);
				sk->ax25->state = AX25_STATE_0;
				sk->dead        = 1;
				sk->state_change(sk);
				ax25_destroy_socket(sk->ax25);
				break;

			case AX25_STATE_2:
				ax25_send_control(sk->ax25, DM | PF, C_RESPONSE);
				sk->ax25->state = AX25_STATE_0;
				sk->dead        = 1;
				sk->state_change(sk);
				ax25_destroy_socket(sk->ax25);
				break;			

			case AX25_STATE_3:
			case AX25_STATE_4:
				ax25_clear_tx_queue(sk->ax25);
				sk->ax25->n2count = 0;
				ax25_send_control(sk->ax25, DISC | PF, C_COMMAND);
				sk->ax25->t3timer = 0;
				sk->ax25->t1timer = sk->ax25->t1 = ax25_calculate_t1(sk->ax25);
				sk->ax25->state   = AX25_STATE_2;
				sk->state_change(sk);
				sk->dead         = 1;
				break;

			default:
				break;
		}
	} else {
		sk->dead = 1;
		sk->state_change(sk);
		ax25_destroy_socket(sk->ax25);
	}

	sock->data = NULL;	

	return 0;
}

/*
 *	We support a funny extension here so you can (as root) give any callsign
 *	digipeated via a local address as source. This is a hack until we add
 *	BSD 4.4 ADDIFADDR type support. It is however small and trivially backward
 *	compatible 8)
 */
static int ax25_bind(struct socket *sock, struct sockaddr *uaddr,int addr_len)
{
	struct sock *sk;
	struct full_sockaddr_ax25 *addr = (struct full_sockaddr_ax25 *)uaddr;
	struct device *dev;
	ax25_address *call;
	
	sk = (struct sock *)sock->data;
	
	if (sk->zapped == 0)
		return -EIO;
		
	if (addr_len != sizeof(struct sockaddr_ax25) && addr_len != sizeof(struct full_sockaddr_ax25))
		return -EINVAL;

#ifdef DONTDO
	if (ax25_find_socket(&addr->fsa_ax25.sax25_call, sk->type) != NULL) {
		if (sk->debug)
			printk("AX25: bind failed: in use\n");
		return -EADDRINUSE;
	}
#endif

	call = ax25_findbyuid(current->euid);
	if (call == NULL && ax25_uid_policy && !suser())
		return -EPERM;
		
	if (call == NULL)
		memcpy(&sk->ax25->source_addr, &addr->fsa_ax25.sax25_call, sizeof(ax25_address));
	else
		memcpy(&sk->ax25->source_addr, call, sizeof(ax25_address));

	if (addr_len == sizeof(struct full_sockaddr_ax25) && addr->fsa_ax25.sax25_ndigis == 1) {
		if (!suser())
			return -EPERM;
		call = &addr->fsa_digipeater[0];
	} else {
		call = &addr->fsa_ax25.sax25_call;
	}

	if ((dev = ax25rtr_get_dev(call)) == NULL) {
		if (sk->debug)
			printk("AX25 bind failed: no device\n");
		return -EADDRNOTAVAIL;
	}

	sk->ax25->device = dev;
	ax25_insert_socket(sk->ax25);

	sk->zapped = 0;

	if (sk->debug)
		printk("AX25: socket is bound\n");

	return 0;
}

static int ax25_connect(struct socket *sock, struct sockaddr *uaddr,
	int addr_len, int flags)
{
	struct sock *sk = (struct sock *)sock->data;
	struct sockaddr_ax25 *addr = (struct sockaddr_ax25 *)uaddr;
	int err;
	
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

	if (addr_len > sizeof(*addr)) {
		int ct           = 0;
		int ndigi        = addr_len - sizeof(*addr);
		ax25_address *ap = (ax25_address *)(((char *)addr) + sizeof(*addr));

		/* Size is an exact number of digipeaters ? */
		if (ndigi % sizeof(ax25_address))
			return -EINVAL;

		ndigi /= sizeof(ax25_address);

		/* Valid number of digipeaters ? */
		if (ndigi < 1 || ndigi > 6)
			return -EINVAL;

		if (sk->ax25->digipeat == NULL) {
			if ((sk->ax25->digipeat = (ax25_digi *)kmalloc(sizeof(ax25_digi), GFP_KERNEL)) == NULL)
				return -ENOMEM;
		}

		sk->ax25->digipeat->ndigi = ndigi;

		while (ct < ndigi) {
			sk->ax25->digipeat->repeated[ct] = 0;
			memcpy(&sk->ax25->digipeat->calls[ct], &ap[ct], sizeof(ax25_address));
			ct++;
		}

		sk->ax25->digipeat->lastrepeat = 0;
		addr_len -= ndigi * sizeof(ax25_address);	
	}

	if (addr_len != sizeof(struct sockaddr_ax25))
		return -EINVAL;

	if (sk->zapped) {	/* Must bind first - autobinding in this may or may not work */
		if ((err = ax25_rt_autobind(sk->ax25, &addr->sax25_call)) < 0)
			return err;
		ax25_insert_socket(sk->ax25);		/* Finish the bind */
	}
		
	if (sk->type == SOCK_SEQPACKET && ax25_find_cb(&sk->ax25->source_addr, &addr->sax25_call, sk->ax25->device) != NULL)
		return -EBUSY;				/* Already such a connection */
		
	memcpy(&sk->ax25->dest_addr, &addr->sax25_call, sizeof(ax25_address));
	
	/* First the easy one */
	if (sk->type != SOCK_SEQPACKET) {
		sock->state = SS_CONNECTED;
		sk->state   = TCP_ESTABLISHED;
		return 0;
	}
	
	/* Move to connecting socket, ax.25 lapb WAIT_UA.. */	
	sock->state        = SS_CONNECTING;
	sk->state          = TCP_SYN_SENT;
	ax25_establish_data_link(sk->ax25);
	sk->ax25->state     = AX25_STATE_1;
	ax25_set_timer(sk->ax25);		/* Start going SABM SABM until a UA or a give up and DM */
	
	/* Now the loop */
	if (sk->state != TCP_ESTABLISHED && (flags & O_NONBLOCK))
		return -EINPROGRESS;
		
	cli();	/* To avoid races on the sleep */

	/* A DM or timeout will go to closed, a UA will go to ABM */
	while (sk->state == TCP_SYN_SENT) {
		interruptible_sleep_on(sk->sleep);
		if (current->signal & ~current->blocked) {
			sti();
			return -ERESTARTSYS;
		}
	}

	if (sk->state != TCP_ESTABLISHED) {	/* Not in ABM, not in WAIT_UA -> failed */
		sti();
		sock->state = SS_UNCONNECTED;
		return -sk->err;	/* Always set at this point */
	}
	
	sock->state = SS_CONNECTED;

	sti();
	
	return 0;
}
	
static int ax25_socketpair(struct socket *sock1, struct socket *sock2)
{
	return -EOPNOTSUPP;
}

static int ax25_accept(struct socket *sock, struct socket *newsock, int flags)
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

static int ax25_getname(struct socket *sock, struct sockaddr *uaddr,
	int *uaddr_len, int peer)
{
	ax25_address *addr;
	struct full_sockaddr_ax25 *sax = (struct full_sockaddr_ax25 *)uaddr;
	struct sock *sk;
	unsigned char ndigi, i;
	
	sk = (struct sock *)sock->data;
	
	if (peer != 0) {
		if (sk->state != TCP_ESTABLISHED)
			return -ENOTCONN;
		addr = &sk->ax25->dest_addr;
	} else {
		addr = &sk->ax25->source_addr;
	}
		
	sax->fsa_ax25.sax25_family = AF_AX25;
	memcpy(&sax->fsa_ax25.sax25_call, addr, sizeof(ax25_address));
	sax->fsa_ax25.sax25_ndigis = 0;
	*uaddr_len = sizeof(struct sockaddr_ax25);

	/* This will supply digipeat path on both getpeername() and getsockname() */
	if (sk->ax25->digipeat != NULL) {
		ndigi = sk->ax25->digipeat->ndigi;
		sax->fsa_ax25.sax25_ndigis = ndigi;
		*uaddr_len += sizeof(ax25_address) * ndigi;
		for (i = 0; i < ndigi; i++)
			memcpy(&sax->fsa_digipeater[i], &sk->ax25->digipeat->calls[i], sizeof(ax25_address));
	}

	return 0;
}
 
int ax25_rcv(struct sk_buff *skb, struct device *dev, struct packet_type *ptype)
{
	unsigned char *data = skb->data;
	struct sock *make;
	struct sock *sk;
	int type = 0;
	ax25_digi dp;
	ax25_cb *ax25;
	ax25_address src, dest;
	struct sock *raw;
	int mine = 0;

	skb->sk = NULL;		/* Initially we don't know who its for */
	
	if ((*data & 0x0F) != 0) {
		kfree_skb(skb, FREE_READ);	/* Not a KISS data frame */
		return 0;
	}

	data++;
	
	/*
	 *	Parse the address header.
	 */
	if ((data = ax25_parse_addr(data, skb->len + dev->hard_header_len - 1, &src, &dest, &dp, &type)) == NULL) {
		kfree_skb(skb, FREE_READ);
		return 0;
	}
	
	/*
	 *	Send the frame to the AX.25 auto-router
	 */
	ax25_rt_rx_frame(&src, dev);
	
	/*
	 *	Ours perhaps ?
	 */
	if (dp.lastrepeat + 1 < dp.ndigi) {		/* Not yet digipeated completely */
		if (ax25cmp(&dp.calls[dp.lastrepeat + 1], (ax25_address *)dev->dev_addr) == 0) {
			/* We are the digipeater. Mark ourselves as repeated
			   and throw the packet back out of the same device */
			dp.lastrepeat++;
			dp.repeated[(int)dp.lastrepeat] = 1;
#ifdef CONFIG_AX25_XDIGI
			while (dp.lastrepeat + 1 < dp.ndigi) {
				struct device *dev_scan;
				if ((dev_scan = ax25rtr_get_dev(&dp.calls[dp.lastrepeat + 1])) == NULL)
					break;
				dp.lastrepeat++;
				dp.repeated[(int)dp.lastrepeat] = 1;
				dev = dev_scan;
			}
#endif
			build_ax25_addr(skb->data + 1, &src, &dest, &dp, type);
			skb->len += dev->hard_header_len;
			skb->arp = 1;
			dev_queue_xmit(skb, dev, SOPRI_NORMAL);			
		} else {
			kfree_skb(skb, FREE_READ);
		}

		return 0;
	}

	/*
	 *	Adjust the lengths for digipeated input
	 */
	skb->len -= sizeof(ax25_address) * dp.ndigi;
	
	/* For our port addreses ? */
	if (ax25cmp(&dest, (ax25_address *)dev->dev_addr) == 0)
		mine = 1;

#ifdef CONFIG_NETROM
	/* Also match on any NET/ROM callsign */
	if (!mine && nr_dev_get(&dest) != NULL)
		mine = 1;
#endif	
	
	if ((*data & ~0x10) == LAPB_UI) {	/* UI frame - bypass LAPB processing */
		data++;
		skb->h.raw = data + 1;		/* skip pid */

		if ((raw = ax25_addr_match(&dest)) != NULL)
			ax25_send_to_raw(raw, skb, (int)*data);

		if (!mine && ax25cmp(&dest, (ax25_address *)dev->broadcast) != 0) {
			kfree_skb(skb, FREE_READ);
			return 0;
		}

		/* Now we are pointing at the pid byte */
		switch (*data++) {
#ifdef CONFIG_INET		
			case AX25_P_IP:
				ax25_ip_mode_set(&src, dev, 'D');
				ip_rcv(skb, dev, ptype);	/* Note ptype here is the wrong one, fix me later */
				break;

			case AX25_P_ARP:
				arp_rcv(skb, dev, ptype);	/* Note ptype here is wrong... */
				break;
#endif				
			case AX25_P_TEXT:
				/* Now find a suitable dgram socket */
				if ((sk = ax25_find_socket(&dest, &src, SOCK_DGRAM)) != NULL) {
					if (sk->rmem_alloc >= sk->rcvbuf) {
						kfree_skb(skb, FREE_READ);
					} else {
						skb_queue_tail(&sk->receive_queue, skb);
						skb->sk = sk;
						sk->rmem_alloc += skb->mem_len;
						if (!sk->dead)
							sk->data_ready(sk, skb->len - 2);
					}
				} else {
					kfree_skb(skb, FREE_READ);
				}
				break;	

			default:
				kfree_skb(skb, FREE_READ);	/* Will scan SOCK_AX25 RAW sockets */
				break;
		}

		return 0;
	}
	
	/* LAPB */
	if ((ax25 = ax25_find_cb(&dest, &src, dev)) != NULL) {
		skb->h.raw = data;
		/* Process the frame. If it is queued up internally it returns one otherwise we 
		   free it immediately. This routine itself wakes the user context layers so we
		   do no further work */
		if (ax25_process_rx_frame(ax25, skb, type) == 0)
			kfree_skb(skb, FREE_READ);

		return 0;
	}

	if ((data[0] & 0xEF) != SABM) {
		/*
		 *	Never reply to a DM. Also ignore any connects for
		 *	addresses that are not our interfaces and not a socket.
		 */
		if ((data[0] & 0xEF) != DM && mine)
			ax25_return_dm(dev, &src, &dest, &dp);

		kfree_skb(skb, FREE_READ);
		return 0;
	}

	if ((sk = ax25_find_listener(&dest, dev, SOCK_SEQPACKET)) != NULL) {
		if (sk->ack_backlog == sk->max_ack_backlog || (make = ax25_make_new(sk, dev)) == NULL) {
			if (mine)
				ax25_return_dm(dev, &src, &dest, &dp);

			kfree_skb(skb, FREE_READ);
			return 0;
		}
		
		ax25 = make->ax25;

		/*
		 *	Sort out any digipeated paths.
		 */
		if (dp.ndigi != 0 && ax25->digipeat == NULL && (ax25->digipeat = kmalloc(sizeof(ax25_digi), GFP_ATOMIC)) == NULL) {
			kfree_skb(skb, FREE_READ);
			ax25_destroy_socket(ax25);
			return 0;
		}

		if (dp.ndigi == 0) {
			if (ax25->digipeat != NULL) {
				kfree_s(ax25->digipeat, sizeof(ax25_digi));
				ax25->digipeat = NULL;
			}
		} else {
			/* Reverse the source SABM's path */
			ax25_digi_invert(&dp, ax25->digipeat);
		}

		skb_queue_head(&sk->receive_queue, skb);

		skb->sk     = make;
		make->state = TCP_ESTABLISHED;
		make->pair  = sk;

		sk->ack_backlog++;
	} else {
#ifdef CONFIG_NETROM
		if (!mine) {
			kfree_skb(skb, FREE_READ);
			return 0;
		}

		if (dp.ndigi != 0) {
			ax25_return_dm(dev, &src, &dest, &dp);
			kfree_skb(skb, FREE_READ);
			return 0;
		}

		if ((ax25 = ax25_create_cb()) == NULL) {
			ax25_return_dm(dev, &src, &dest, &dp);
			kfree_skb(skb, FREE_READ);
			return 0;
		}
#else
		if (mine)
			ax25_return_dm(dev, &src, &dest, &dp);

		kfree_skb(skb, FREE_READ);
		return 0;
#endif
	}

	memcpy(&ax25->source_addr, &dest, sizeof(ax25_address));
	memcpy(&ax25->dest_addr,   &src,  sizeof(ax25_address));

	ax25->device = dev;
	
	ax25_send_control(ax25, UA | PF, C_RESPONSE);

	ax25->t3timer = ax25->t3;
	ax25->state   = AX25_STATE_3;

	ax25_insert_socket(ax25);

	ax25_set_timer(ax25);

	if (sk != NULL) {
		if (!sk->dead)
			sk->data_ready(sk, skb->len - 2);
	} else {
		kfree_skb(skb, FREE_READ);
	}

	return 0;
}

static int ax25_sendto(struct socket *sock, void *ubuf, int len, int noblock,
	unsigned flags, struct sockaddr *usip, int addr_len)
{
	struct sock *sk = (struct sock *)sock->data;
	struct sockaddr_ax25 *usax = (struct sockaddr_ax25 *)usip;
	unsigned char *uaddr = (unsigned char *)usip;
	int err;
	struct sockaddr_ax25 sax;
	struct sk_buff *skb;
	unsigned char *asmptr;
	int size;
	ax25_digi *dp;
	ax25_digi dtmp;
	int lv;
	
	if (sk->err) {
		err     = sk->err;
		sk->err = 0;
		return -err;
	}

	if (flags)
		return -EINVAL;

	if (sk->zapped)
		return -EADDRNOTAVAIL;
		
	if (sk->ax25->device == NULL)
		return -ENETUNREACH;
		
	if (usax) {
		int ndigi = addr_len - sizeof(sax);
		if (addr_len < sizeof(sax))
			return -EINVAL;

		/* Trailing digipeaters on address ?? */
		if (addr_len > sizeof(sax)) {
			int ct = 0;

			ax25_address *ap = (ax25_address *)(((char *)uaddr) + sizeof(sax));
			/* Size is an exact number of digipeaters ? */
			if (ndigi % sizeof(ax25_address))
				return -EINVAL;
			ndigi /= sizeof(ax25_address);

			/* Valid number of digipeaters ? */
			if (ndigi < 1 || ndigi > 6)
				return -EINVAL;

			/* Copy data into digipeat structure */
			while (ct < ndigi) {
				dtmp.repeated[ct] = 0;
				memcpy(&dtmp.calls[ct], &ap[ct], sizeof(ax25_address));
				ct++;
			}

			dtmp.lastrepeat = 0;
			dtmp.ndigi      = ndigi;
			addr_len -= ndigi * sizeof(ax25_address);	
		}

		memcpy(&sax, usax, sizeof(sax));
		if (sk->type == SOCK_SEQPACKET && memcmp(&sk->ax25->dest_addr, &sax.sax25_call, sizeof(ax25_address)) != 0)
			return -EISCONN;
		if (sax.sax25_family != AF_AX25)
			return -EINVAL;
		if (ndigi != 0)
			dp = &dtmp;
		else
			dp = NULL;
	} else {
		if (sk->state != TCP_ESTABLISHED)
			return -ENOTCONN;
		sax.sax25_family = AF_AX25;
		memcpy(&sax.sax25_call, &sk->ax25->dest_addr, sizeof(ax25_address));
		dp = sk->ax25->digipeat;
	}
	
	if (sk->debug)
		printk("AX.25: sendto: Addresses built.\n");

	/* Build a packet */
	if (sk->debug)
		printk("AX.25: sendto: building packet.\n");

	size = 2 + len + 1 + size_ax25_addr(dp);	
	/* 2 bytes for PID and (U)I frame byte: 15+ for KISS data & calls */	

	if ((skb = sock_alloc_send_skb(sk, size, 0, &err)) == NULL)
		return err;

	skb->sk   = sk;
	skb->free = 1;
	skb->arp  = 1;
	skb->len  = size;
	
	asmptr = skb->data;
	if (sk->debug) {
		printk("Building AX.25 Header (dp=%p).\n", dp);
		if (dp != 0)
			printk("Num digipeaters=%d\n", dp->ndigi);
	}

	/* Build an AX.25 header */
	*asmptr++ = 0;	/* KISS data */
	asmptr   += (lv = build_ax25_addr(asmptr, &sk->ax25->source_addr, &sax.sax25_call, dp, C_COMMAND));
	if (sk->debug)
		printk("Built header (%d bytes)\n",lv);
	skb->h.raw = asmptr;
	
	if (sk->debug)
		printk("base=%p pos=%p\n", skb->data, asmptr);
	*asmptr++ = LAPB_UI;		/* Datagram - will get replaced for I frames */
	*asmptr++ = sk->protocol;	/* AX.25 TEXT by default */
		
	if (sk->debug)
		printk("AX.25: Appending user data\n");

	/* User data follows immediately after the AX.25 data */
	memcpy_fromfs(asmptr, ubuf, len);
	if (sk->debug)
		printk("AX.25: Transmitting buffer\n");
	if (sk->type == SOCK_SEQPACKET) {
		/* Connected mode sockets go via the LAPB machine */
		if (sk->state != TCP_ESTABLISHED) {
			kfree_skb(skb, FREE_WRITE);
			return -ENOTCONN;
		}
		ax25_output(sk->ax25, skb);	/* Shove it onto the queue and kick */
		return len;
	} else {
		/* Datagram frames go straight out of the door as UI */
		dev_queue_xmit(skb, sk->ax25->device, SOPRI_NORMAL);
		return len;
	}
}

static int ax25_send(struct socket *sock, void *ubuf, int size, int noblock, unsigned flags)
{
	return ax25_sendto(sock, ubuf, size, noblock, flags, NULL, 0);
}

static int ax25_write(struct socket *sock, char *ubuf, int size, int noblock)
{
	return ax25_send(sock, ubuf, size, noblock, 0);
}

static int ax25_recvfrom(struct socket *sock, void *ubuf, int size, int noblock,
		   unsigned flags, struct sockaddr *sip, int *addr_len)
{
	struct sock *sk = (struct sock *)sock->data;
	struct sockaddr_ax25 *sax = (struct sockaddr_ax25 *)sip;
	char *addrptr = (char *)sip;
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

	copied= (size < skb->len) ? size : skb->len;
	skb_copy_datagram(skb, sk->type == SOCK_SEQPACKET ? 2 : 0, ubuf, copied);
	
	if (sax) {
		struct sockaddr_ax25 addr;
		ax25_digi digi;
		ax25_address dest;
		unsigned char *dp = skb->data;
		int ct = 0;
		
		ax25_parse_addr(dp, skb->len, NULL, &dest, &digi, NULL);
		addr.sax25_family = AF_AX25;
		memcpy(&addr.sax25_call, &dest, sizeof(ax25_address));
		memcpy(sax,&addr, sizeof(*sax));
		addrptr += sizeof(*sax);

		while (ct < digi.ndigi) {
			memcpy(addrptr, &digi. calls[ct], 7);
			addrptr += 7;
			ct++;
		}
		if (addr_len)
			*addr_len = sizeof(*sax) + 7 * digi.ndigi;
	}

	skb_free_datagram(skb);

	return copied;
}		

static int ax25_recv(struct socket *sock, void *ubuf, int size , int noblock,
	unsigned flags)
{
	struct sock *sk = (struct sock *)sock->data;

	if (sk->zapped)
		return -ENOTCONN;

	return ax25_recvfrom(sock, ubuf, size, noblock, flags, NULL, NULL);
}

static int ax25_read(struct socket *sock, char *ubuf, int size, int noblock)
{
	return ax25_recv(sock, ubuf, size, noblock, 0);
}

static int ax25_shutdown(struct socket *sk, int how)
{
	/* FIXME - generate DM and RNR states */
	return -EOPNOTSUPP;
}

static int ax25_select(struct socket *sock , int sel_type, select_table *wait)
{
	struct sock *sk = (struct sock *)sock->data;

	return datagram_select(sk, sel_type, wait);
}

static int ax25_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
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

		case SIOCAX25ADDUID:	/* Add a uid to the uid/call map table */
		case SIOCAX25DELUID:	/* Delete a uid from the uid/call map table */
		case SIOCAX25GETUID:
		{
			struct sockaddr_ax25 sax25;
			if ((err = verify_area(VERIFY_READ, (void *)arg, sizeof(struct sockaddr_ax25))) != 0)
				return err;
			memcpy_fromfs(&sax25, (void *)arg, sizeof(sax25));
			return ax25_uid_ioctl(cmd, &sax25);
		}

		case SIOCAX25NOUID:	/* Set the default policy (default/bar) */
			if ((err = verify_area(VERIFY_READ, (void *)arg, sizeof(unsigned long))) != 0)
				return err;
			if(!suser())
				return -EPERM;
			amount = get_fs_long((void *)arg);
			if (amount > AX25_NOUID_BLOCK)
				return -EINVAL;
			ax25_uid_policy = amount;
			return 0;

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
			return(dev_ioctl(cmd, (void *)arg));
	}

	/*NOTREACHED*/
	return(0);
}

int ax25_get_info(char *buffer, char **start, off_t offset, int length)
{
	ax25_cb *ax25;
	struct device *dev;
	char *devname;
	int len = 0;
	off_t pos = 0;
	off_t begin = 0;
  
	cli();

	len += sprintf(buffer, "dest_addr src_addr  dev st vs vr va    t1     t2     t3     n2  rtt wnd Snd-Q Rcv-Q\n");

	for (ax25 = ax25_list; ax25 != NULL; ax25 = ax25->next) {
		if ((dev = ax25->device) == NULL)
			devname = "???";
		else
			devname = dev->name;

		len += sprintf(buffer + len, "%-9s ",
			ax2asc(&ax25->dest_addr));
		len += sprintf(buffer + len, "%-9s %-3s %2d %2d %2d %2d %3d/%03d %2d/%02d %3d/%03d %2d/%02d %3d %3d",
			ax2asc(&ax25->source_addr), devname,
			ax25->state,
			ax25->vs, ax25->vr, ax25->va,
			ax25->t1timer / PR_SLOWHZ,
			ax25->t1      / PR_SLOWHZ,
			ax25->t2timer / PR_SLOWHZ,
			ax25->t2      / PR_SLOWHZ,
			ax25->t3timer / PR_SLOWHZ,
			ax25->t3      / PR_SLOWHZ,
			ax25->n2count, ax25->n2,
			ax25->rtt     / PR_SLOWHZ,
			ax25->window);

		if (ax25->sk != NULL) {
			len += sprintf(buffer + len, " %5ld %5ld\n",
				ax25->sk->wmem_alloc,
				ax25->sk->rmem_alloc);
		} else {
			len += sprintf(buffer + len, "\n");
		}
		
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

static struct proto_ops ax25_proto_ops = {
	AF_AX25,
	
	ax25_create,
	ax25_dup,
	ax25_release,
	ax25_bind,
	ax25_connect,
	ax25_socketpair,
	ax25_accept,
	ax25_getname,
	ax25_read,
	ax25_write,
	ax25_select,
	ax25_ioctl,
	ax25_listen,
	ax25_send,
	ax25_recv,
	ax25_sendto,
	ax25_recvfrom,
	ax25_shutdown,
	ax25_setsockopt,
	ax25_getsockopt,
	ax25_fcntl,
};

/* Called by socket.c on kernel start up */

static struct packet_type ax25_packet_type = 
{
	0,	/* MUTTER ntohs(ETH_P_AX25),*/
	0,		/* copy */
	ax25_rcv,
	NULL,
	NULL,
};

static struct notifier_block ax25_dev_notifier = {
	ax25_device_event,
	0
};

void ax25_proto_init(struct net_proto *pro)
{
	sock_register(ax25_proto_ops.family, &ax25_proto_ops);
	ax25_packet_type.type = htons(ETH_P_AX25);
	dev_add_pack(&ax25_packet_type);	
	register_netdevice_notifier(&ax25_dev_notifier);
	printk("GW4PTS/G4KLX AX.25 for Linux. Version 0.29 ALPHA for Linux NET3.029 (Linux 1.3.0)\n");
}

/*******************************************************************************************************************\
*														    *
*		Driver encapsulation support: Moved out of SLIP because a) it should be here 			    *
*									b) for HDLC cards			    *
*														    *
\*******************************************************************************************************************/

/*
 *	Shove an AX.25 UI header on an IP packet and handle ARP
 */

#ifdef CONFIG_INET
 
int ax25_encapsulate(unsigned char *buff, struct device *dev, unsigned short type, void *daddr,
		void *saddr, unsigned len, struct sk_buff *skb)
{
  	/* header is an AX.25 UI frame from us to them */
  	*buff++ = 0;	/* KISS DATA */
  	
	if (daddr != NULL)
		memcpy(buff, daddr, dev->addr_len);	/* Address specified */
  	buff[6] &= ~LAPB_C;
  	buff[6] &= ~LAPB_E;
  	buff[6] |= SSID_SPARE;
  	buff += 7;

  	if (saddr != NULL)
  		memcpy(buff, saddr, dev->addr_len);
  	else
  		memcpy(buff, dev->dev_addr, dev->addr_len);

  	buff[6] &= ~LAPB_C;
  	buff[6] |= LAPB_E;
  	buff[6] |= SSID_SPARE;
  	buff   += 7;
  	*buff++ = LAPB_UI;	/* UI */

  	/* Append a suitable AX.25 PID */
  	switch (type) {
  		case ETH_P_IP:
  			*buff++ = AX25_P_IP;
 			break;

  		case ETH_P_ARP:
  			*buff++ = AX25_P_ARP;
  			break;

  		default:
  			*buff++ = 0;
  			break;
 	}
	
	if (daddr != NULL)  
	  	return 17;

	return -17;	/* Unfinished header */
}

int ax25_rebuild_header(unsigned char *bp, struct device *dev, unsigned long dest, struct sk_buff *skb)
{
  	if (arp_find(bp + 1, dest, dev, dev->pa_addr, skb))
  		return 1;

  	bp[7]  &= ~LAPB_C;
  	bp[7]  &= ~LAPB_E;
  	bp[7]  |= SSID_SPARE;
  	bp[14] &= ~LAPB_C;
  	bp[14] |= LAPB_E;
  	bp[14] |= SSID_SPARE;

  	return 0;
}	

#endif

#endif
