/*
 *	AX.25 release 031
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
 *	AX.25 006	Alan(GW4PTS)		Nearly died of shock - it's working 8-)
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
 *						removed device registration (it's not used or needed). Clean up for
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
 *	AX.25 030	Alan(GW4PTS)		Added variable length headers.
 *			Jonathan(G4KLX)		Added BPQ Ethernet interface.
 *			Steven(GW7RRM)		Added digi-peating control ioctl.
 *						Added extended AX.25 support.
 *						Added AX.25 frame segmentation.
 *			Darryl(G7LED)		Changed connect(), recvfrom(), sendto() sockaddr/addrlen to
 *						fall inline with bind() and new policy.
 *						Moved digipeating ctl to new ax25_dev structs.
 *						Fixed ax25_release(), set TCP_CLOSE, wakeup app
 *						context, THEN make the sock dead.
 *			Alan(GW4PTS)		Cleaned up for single recvmsg methods.
 *			Alan(GW4PTS)		Fixed not clearing error on connect failure.
 *	AX.25 031	Jonathan(G4KLX)		Added binding to any device.
 *			Joerg(DL1BKE)		Added DAMA support, fixed (?) digipeating, fixed buffer locking
 *						for "virtual connect" mode... Result: Probably the
 *						"Most Buggiest Code You've Ever Seen" (TM)
 *			HaJo(DD8NE)		Implementation of a T5 (idle) timer
 *			Joerg(DL1BKE)		Renamed T5 to IDLE and changed behaviour:
 *						the timer gets reloaded on every received or transmitted
 *						I frame for IP or NETROM. The idle timer is not active
 *						on "vanilla AX.25" connections. Furthermore added PACLEN
 *						to provide AX.25-layer based fragmentation (like WAMPES)
 *      AX.25 032	Joerg(DL1BKE)		Fixed DAMA timeout error.
 *						ax25_send_frame() limits the number of enqueued
 *						datagrams per socket.
 *
 *	To do:
 *		Restructure the ax25_rcv code to be cleaner/faster and
 *		copy only when needed.
 *		Consider better arbitrary protocol support.
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
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/firewall.h>

#include <net/ip.h>
#include <net/arp.h>

/*
 *	The null address is defined as a callsign of all spaces with an
 *	SSID of zero.
 */
ax25_address null_ax25_address = {{0x40,0x40,0x40,0x40,0x40,0x40,0x00}};

ax25_cb *volatile ax25_list = NULL;

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

	if (*buf == '\0' || *buf == '-')
	   return "*";

	return buf;

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
			s->state  = AX25_STATE_0;
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
}

/*
 *	Handle device status changes.
 */
static int ax25_device_event(struct notifier_block *this,unsigned long event, void *ptr)
{
	struct device *dev = (struct device *)ptr;

	switch (event) {
		case NETDEV_UP:
			ax25_dev_device_up(dev);
			break;
		case NETDEV_DOWN:
			ax25_kill_by_device(dev);
			ax25_rt_device_down(dev);
			ax25_dev_device_down(dev);
			break;
		default:
			break;
	}

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
 *	Find a socket that wants to accept the SABM we have just
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
 *	Look for any matching address - RAW sockets can bind to arbitrary names
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
			atomic_add(copy->truesize, &sk->rmem_alloc);
			skb_queue_tail(&sk->receive_queue, copy);
			if (!sk->dead)
				sk->data_ready(sk, skb->len);
		}

		sk = sk->next;
	}
}	

/*
 *	Deferred destroy.
 */
void ax25_destroy_socket(ax25_cb *);

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
void ax25_destroy_socket(ax25_cb *ax25)	/* Not static as it's used by the timer */
{
	struct sk_buff *skb;
	unsigned long flags;
	
	save_flags(flags);
	cli();
	
	del_timer(&ax25->timer);
	
	ax25_remove_socket(ax25);
	ax25_clear_queues(ax25);	/* Flush the queues */
	
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
	
	if (ax25->sk != NULL) {
		if (ax25->sk->wmem_alloc || ax25->sk->rmem_alloc) { /* Defer: outstanding buffers */
			init_timer(&ax25->timer);
			ax25->timer.expires  = jiffies + 10 * HZ;
			ax25->timer.function = ax25_destroy_timer;
			ax25->timer.data     = (unsigned long)ax25;
			add_timer(&ax25->timer);
		} else {
			if (ax25->digipeat != NULL) {
				kfree_s(ax25->digipeat, sizeof(ax25_digi));
				ax25->digipeat = NULL;
			}
		
			sk_free(ax25->sk);
			kfree_s(ax25, sizeof(*ax25));
		}
	} else {
		if (ax25->digipeat != NULL) {
			kfree_s(ax25->digipeat, sizeof(ax25_digi));
			ax25->digipeat = NULL;
		}	
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
			if (a == NULL)
				return -ENOMEM;
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
 * dl1bke 960311: set parameters for existing AX.25 connections,
 *		  includes a KILL command to abort any connection.
 *		  VERY useful for debugging ;-)
 */
static int ax25_ctl_ioctl(const unsigned int cmd, void *arg)
{
	struct ax25_ctl_struct ax25_ctl;
	struct device *dev;
	ax25_cb *ax25;
	unsigned long flags;
	int err;
	
	if ((err = verify_area(VERIFY_READ, arg, sizeof(ax25_ctl))) != 0)
		return err;

	memcpy_fromfs(&ax25_ctl, arg, sizeof(ax25_ctl));
	
	if ((dev = ax25rtr_get_dev(&ax25_ctl.port_addr)) == NULL)
		return -ENODEV;

	if ((ax25 = ax25_find_cb(&ax25_ctl.source_addr, &ax25_ctl.dest_addr, dev)) == NULL)
		return -ENOTCONN;

	switch (ax25_ctl.cmd) {
		case AX25_KILL:
#ifdef CONFIG_NETROM
			nr_link_failed(&ax25->dest_addr, ax25->device);
#endif
			ax25_clear_queues(ax25);
			ax25_send_control(ax25, DISC, POLLON, C_COMMAND);
				
			ax25->state = AX25_STATE_0;
			if (ax25->sk != NULL) {
				ax25->sk->state = TCP_CLOSE;
				ax25->sk->err   = ENETRESET;
				if (!ax25->sk->dead)
					ax25->sk->state_change(ax25->sk);
				ax25->sk->dead  = 1;
			}

			ax25_dama_off(ax25);
			ax25_set_timer(ax25);
	  		break;

	  	case AX25_WINDOW:
	  		if (ax25->modulus == MODULUS) {
	  			if (ax25_ctl.arg < 1 || ax25_ctl.arg > 7) 
	  				return -EINVAL;
	  		} else {
	  			if (ax25_ctl.arg < 1 || ax25_ctl.arg > 63) 
	  				return -EINVAL;
	  		}
	  		ax25->window = ax25_ctl.arg;
	  		break;

	  	case AX25_T1:
  			if (ax25_ctl.arg < 1) 
  				return -EINVAL;
  			ax25->rtt = (ax25_ctl.arg * PR_SLOWHZ) / 2;
  			ax25->t1 = ax25_ctl.arg * PR_SLOWHZ;
  			save_flags(flags); cli();
  			if (ax25->t1timer > ax25->t1)
  				ax25->t1timer = ax25->t1;
  			restore_flags(flags);
  			break;

	  	case AX25_T2:
	  		if (ax25_ctl.arg < 1) 
	  			return -EINVAL;
	  		save_flags(flags); cli();
	  		ax25->t2 = ax25_ctl.arg * PR_SLOWHZ;
	  		if (ax25->t2timer > ax25->t2)
	  			ax25->t2timer = ax25->t2;
	  		restore_flags(flags);
	  		break;

	  	case AX25_N2:
	  		if (ax25_ctl.arg < 1 || ax25_ctl.arg > 31) 
	  			return -EINVAL;
	  		ax25->n2count = 0;
	  		ax25->n2 = ax25_ctl.arg;
	  		break;

	  	case AX25_T3:
	  		if (ax25_ctl.arg < 0) 
	  			return -EINVAL;
	  		save_flags(flags); cli();
	  		ax25->t3 = ax25_ctl.arg * PR_SLOWHZ;
	  		if (ax25->t3timer != 0)
	  			ax25->t3timer = ax25->t3;
	  		restore_flags(flags);
	  		break;

	  	case AX25_IDLE:
	  		if (ax25_ctl.arg < 0) 
	  			return -EINVAL;
			save_flags(flags); cli();
	  		ax25->idle = ax25_ctl.arg * PR_SLOWHZ * 60;
	  		if (ax25->idletimer != 0)
	  			ax25->idletimer = ax25->idle;
	  		restore_flags(flags);
	  		break;

	  	case AX25_PACLEN:
	  		if (ax25_ctl.arg < 16 || ax25_ctl.arg > 65535) 
	  			return -EINVAL;
#if 0
	  		if (ax25_ctl.arg > 256) /* we probably want this */
	  			printk(KERN_WARNING "ax25_ctl_ioctl: Warning --- huge paclen %d\n", (int)ax25_ctl.arg);
#endif	  			
	  		ax25->paclen = ax25_ctl.arg;
	  		break;

	  	case AX25_IPMAXQUEUE:
	  		if (ax25_ctl.arg < 1)
	  			return -EINVAL;
	  		ax25->maxqueue = ax25_ctl.arg;
	  		break;

	  	default:
	  		return -EINVAL;
	  }
	  
	  return 0;
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
	skb_queue_head_init(&ax25->frag_queue);
	skb_queue_head_init(&ax25->ack_queue);
	skb_queue_head_init(&ax25->reseq_queue);

	init_timer(&ax25->timer);

	ax25->dama_slave = 0;	/* dl1bke 951121 */

	ax25->rtt     = (AX25_DEF_T1 * PR_SLOWHZ) / 2;
	ax25->t1      = AX25_DEF_T1 * PR_SLOWHZ;
	ax25->t2      = AX25_DEF_T2 * PR_SLOWHZ;
	ax25->t3      = AX25_DEF_T3 * PR_SLOWHZ;
	ax25->n2      = AX25_DEF_N2;
	ax25->paclen  = AX25_DEF_PACLEN;
	ax25->maxqueue= AX25_DEF_IPMAXQUEUE;
	ax25->idle    = AX25_DEF_IDLE;

	ax25->modulus   = AX25_DEF_AXDEFMODE;
	ax25->fragno    = 0;
	ax25->fraglen   = 0;
	ax25->hdrincl   = 0;
	ax25->backoff   = AX25_DEF_BACKOFF == 'E';
	ax25->condition = 0x00;
	ax25->t1timer   = 0;
	ax25->t2timer   = 0;
	ax25->t3timer   = 0;
	ax25->n2count   = 0;
	ax25->idletimer = 0;

	ax25->va      = 0;
	ax25->vr      = 0;
	ax25->vs      = 0;

	if (AX25_DEF_AXDEFMODE == EMODULUS) {
		ax25->window = AX25_DEF_EWINDOW;
	} else {
		ax25->window = AX25_DEF_WINDOW;
	}
	ax25->device   = NULL;
	ax25->digipeat = NULL;
	ax25->sk       = NULL;

	ax25->state    = AX25_STATE_0;

	memset(&ax25->dest_addr,   '\0', AX25_ADDR_LEN);
	memset(&ax25->source_addr, '\0', AX25_ADDR_LEN);

	return ax25;
}

/*
 *	Find out if we are a DAMA slave for this device and count the
 *	number of connections.
 *
 *	dl1bke 951121
 */
int ax25_dev_is_dama_slave(struct device *dev)
{
	ax25_cb *ax25;
	int count = 0;
	
	for (ax25 = ax25_list; ax25 != NULL; ax25 = ax25->next) {
		if (ax25->device == dev && ax25->dama_slave) {
			count++;
			break;
		}
	}
		
	return count;
}

/*
 *	Fill in a created AX.25 created control block with the default
 *	values for a particular device.
 */
static void ax25_fillin_cb(ax25_cb *ax25, struct device *dev)
{
	ax25->device  = dev;

	ax25->rtt      = ax25_dev_get_value(dev, AX25_VALUES_T1);
	ax25->t1       = ax25_dev_get_value(dev, AX25_VALUES_T1);
	ax25->t2       = ax25_dev_get_value(dev, AX25_VALUES_T2);
	ax25->t3       = ax25_dev_get_value(dev, AX25_VALUES_T3);
	ax25->n2       = ax25_dev_get_value(dev, AX25_VALUES_N2);
	ax25->paclen   = ax25_dev_get_value(dev, AX25_VALUES_PACLEN);
	ax25->maxqueue = ax25_dev_get_value(dev, AX25_VALUES_IPMAXQUEUE);
	ax25->idle     = ax25_dev_get_value(dev, AX25_VALUES_IDLE);

	ax25->dama_slave = 0;

	ax25->modulus = ax25_dev_get_value(dev, AX25_VALUES_AXDEFMODE);

	if (ax25->modulus == MODULUS) {
		ax25->window = ax25_dev_get_value(dev, AX25_VALUES_WINDOW);
	} else {
		ax25->window = ax25_dev_get_value(dev, AX25_VALUES_EWINDOW);
	}

	ax25->backoff = ax25_dev_get_value(dev, AX25_VALUES_BACKOFF) == 'E';
}

int ax25_send_frame(struct sk_buff *skb, ax25_address *src, ax25_address *dest,
	ax25_digi *digi, struct device *dev)
{
	ax25_cb *ax25;

	if (skb == NULL)
		return 0;

	/*
	 * Look for an existing connection.
	 */
	for (ax25 = ax25_list; ax25 != NULL; ax25 = ax25->next) {
		if (ax25->sk != NULL && ax25->sk->type != SOCK_SEQPACKET)
			continue;

		if (ax25cmp(&ax25->source_addr, src) == 0 && ax25cmp(&ax25->dest_addr, dest) == 0 && ax25->device == dev) {
			if (ax25_queue_length(ax25, skb) > ax25->maxqueue * ax25->window) {
				kfree_skb(skb, FREE_WRITE);
			} else {
				ax25_output(ax25, skb);
			}
			ax25->idletimer = ax25->idle;	/* dl1bke 960228 */
			return 1;		/* It already existed */
		}
	}

	if ((ax25 = ax25_create_cb()) == NULL)
		return 0;

	ax25_fillin_cb(ax25, dev);

	ax25->source_addr = *src;
	ax25->dest_addr   = *dest;

	if (digi != NULL) {
		if ((ax25->digipeat = kmalloc(sizeof(ax25_digi), GFP_ATOMIC)) == NULL) {
			kfree_s(ax25, sizeof(ax25));
			return 0;
		}
		*ax25->digipeat = *digi;
	} else {
		ax25_rt_build_path(ax25, dest);
	}

	if (ax25_dev_is_dama_slave(ax25->device))	/* dl1bke 960116 */
		dama_establish_data_link(ax25);
	else
		ax25_establish_data_link(ax25);

	/* idle timeouts only for mode vc connections */

	ax25->idletimer = ax25->idle;
		
	ax25_insert_socket(ax25);

	ax25->state = AX25_STATE_1;

	ax25_set_timer(ax25);

	ax25_output(ax25, skb);
			
	return 1;			/* We had to create it */	
}

/*
 *	Find the AX.25 device that matches the hardware address supplied.
 */
struct device *ax25rtr_get_dev(ax25_address *addr)
{
	struct device *dev;
	
	for (dev = dev_base; dev != NULL; dev = dev->next) {
		if (dev->flags & IFF_UP) {
			switch (dev->type) {
				case ARPHRD_AX25: /* Active kiss ax25 mode */ 
					if (ax25cmp(addr, (ax25_address *)dev->dev_addr) == 0)
						return dev;
					break;
#ifdef CONFIG_BPQETHER
				case ARPHRD_ETHER: {
						ax25_address *dev_addr;

						if ((dev_addr = ax25_bpq_get_addr(dev)) != NULL)
							if (ax25cmp(addr, dev_addr) == 0)
								return dev;
					}
					break;
#endif
				default:
					break;
			}
		}
	}

	return NULL;
}

/*
 *	Handling for system calls applied via the various interfaces to an
 *	AX25 socket object
 */
 
static int ax25_fcntl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	return -EINVAL;
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
			if (sk->ax25->modulus == MODULUS) {
				if (opt < 1 || opt > 7)
					return -EINVAL;
			} else {
				if (opt < 1 || opt > 63)
					return -EINVAL;
			}
			sk->ax25->window = opt;
			return 0;

		case AX25_T1:
			if (opt < 1)
				return -EINVAL;
			sk->ax25->rtt = (opt * PR_SLOWHZ) / 2;
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
			
		case AX25_IDLE:
			if (opt < 0)
				return -EINVAL;
			sk->ax25->idle = opt * PR_SLOWHZ * 60;
			return 0;

		case AX25_BACKOFF:
			sk->ax25->backoff = opt ? 1 : 0;
			return 0;

		case AX25_EXTSEQ:
			sk->ax25->modulus = opt ? EMODULUS : MODULUS;
			return 0;

		case AX25_HDRINCL:
			sk->ax25->hdrincl = opt ? 1 : 0;
			return 0;
			
		case AX25_PACLEN:
			if (opt < 16 || opt > 65535)
				return -EINVAL;
			sk->ax25->paclen = opt;
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
			val = (sk->ax25->t1 * 2) / PR_SLOWHZ;
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
			
		case AX25_IDLE:
			val = sk->ax25->idle / (PR_SLOWHZ * 60);
			break;

		case AX25_BACKOFF:
			val = sk->ax25->backoff;
			break;

		case AX25_EXTSEQ:
			val = (sk->ax25->modulus == EMODULUS);
			break;

		case AX25_HDRINCL:
			val = sk->ax25->hdrincl;
			break;
			
		case AX25_PACLEN:
			val = sk->ax25->paclen;
			break;

		default:
			return -ENOPROTOOPT;
	}

	if ((err = verify_area(VERIFY_WRITE, optlen, sizeof(int))) != 0)
		return err;

	put_user(sizeof(int), optlen);

	if ((err = verify_area(VERIFY_WRITE, optval, sizeof(int))) != 0)
		return err;

	put_user(val, optval);

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

	switch (sock->type) {
		case SOCK_DGRAM:
			if (protocol == 0 || protocol == AF_AX25)
				protocol = AX25_P_TEXT;
			break;
		case SOCK_SEQPACKET:
			switch (protocol) {
				case 0:
				case AF_AX25:	/* For CLX */
					protocol = AX25_P_TEXT;
					break;
				case AX25_P_SEGMENT:
#ifdef CONFIG_INET
				case AX25_P_ARP:
				case AX25_P_IP:
#endif
#ifdef CONFIG_NETROM
				case AX25_P_NETROM:
#endif
					return -ESOCKTNOSUPPORT;
				default:
					break;
			}
			break;
		case SOCK_RAW:
			break;
		default:
			return -ESOCKTNOSUPPORT;
	}

	if ((sk = sk_alloc(GFP_ATOMIC)) == NULL)
		return -ENOMEM;

	if ((ax25 = ax25_create_cb()) == NULL) {
		sk_free(sk);
		return -ENOMEM;
	}

	skb_queue_head_init(&sk->receive_queue);
	skb_queue_head_init(&sk->write_queue);
	skb_queue_head_init(&sk->back_log);

	sk->socket        = sock;
	sk->type          = sock->type;
	sk->protocol      = protocol;
	sk->next          = NULL;
	sk->allocation	  = GFP_KERNEL;
	sk->rcvbuf        = SK_RMEM_MAX;
	sk->sndbuf        = SK_WMEM_MAX;
	sk->state         = TCP_CLOSE;
	sk->priority      = SOPRI_NORMAL;
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

	if ((sk = sk_alloc(GFP_ATOMIC)) == NULL)
		return NULL;

	if ((ax25 = ax25_create_cb()) == NULL) {
		sk_free(sk);
		return NULL;
	}

	ax25_fillin_cb(ax25, dev);

	sk->type   = osk->type;
	sk->socket = osk->socket;

	switch (osk->type) {
		case SOCK_DGRAM:
			break;
		case SOCK_SEQPACKET:
			break;
		default:
			sk_free(sk);
			kfree_s((void *)ax25, sizeof(*ax25));
			return NULL;
	}

	skb_queue_head_init(&sk->receive_queue);
	skb_queue_head_init(&sk->write_queue);
	skb_queue_head_init(&sk->back_log);

	sk->next        = NULL;
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

	ax25->modulus = osk->ax25->modulus;
	ax25->backoff = osk->ax25->backoff;
	ax25->hdrincl = osk->ax25->hdrincl;
	ax25->rtt     = osk->ax25->rtt;
	ax25->t1      = osk->ax25->t1;
	ax25->t2      = osk->ax25->t2;
	ax25->t3      = osk->ax25->t3;
	ax25->n2      = osk->ax25->n2;
	ax25->idle    = osk->ax25->idle;
	ax25->paclen  = osk->ax25->paclen;

	ax25->window   = osk->ax25->window;
	ax25->maxqueue = osk->ax25->maxqueue;

	ax25->source_addr = osk->ax25->source_addr;
	
	if (osk->ax25->digipeat != NULL) {
		if ((ax25->digipeat = (ax25_digi *)kmalloc(sizeof(ax25_digi), GFP_ATOMIC)) == NULL) {
			sk_free(sk);
			kfree_s(ax25, sizeof(*ax25));
			return NULL;
		}
		
		/* dl1bke 960119: we have to copy the old digipeater list! */
		*ax25->digipeat = *osk->ax25->digipeat;
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
				sk->state       = TCP_CLOSE;
				sk->state_change(sk);
				sk->dead        = 1;
				ax25_destroy_socket(sk->ax25);
				break;

			case AX25_STATE_1:
				ax25_send_control(sk->ax25, DISC, POLLON, C_COMMAND);
				sk->ax25->state = AX25_STATE_0;
				sk->state       = TCP_CLOSE;
				sk->state_change(sk);
				sk->dead        = 1;
				ax25_destroy_socket(sk->ax25);
				break;

			case AX25_STATE_2:
				if (sk->ax25->dama_slave)
					ax25_send_control(sk->ax25, DISC, POLLON, C_COMMAND);
				else
					ax25_send_control(sk->ax25, DM, POLLON, C_RESPONSE);
				sk->ax25->state = AX25_STATE_0;
				sk->state       = TCP_CLOSE;
				sk->state_change(sk);
				sk->dead        = 1;
				ax25_destroy_socket(sk->ax25);
				break;			

			case AX25_STATE_3:
			case AX25_STATE_4:
				ax25_clear_queues(sk->ax25);
				sk->ax25->n2count = 0;
				if (!sk->ax25->dama_slave) {
					ax25_send_control(sk->ax25, DISC, POLLON, C_COMMAND);
					sk->ax25->t3timer = 0;
				} else {
					sk->ax25->t3timer = sk->ax25->t3;	/* DAMA slave timeout */
				}
				sk->ax25->t1timer = sk->ax25->t1 = ax25_calculate_t1(sk->ax25);
				sk->ax25->state   = AX25_STATE_2;
				sk->state         = TCP_CLOSE;
				sk->state_change(sk);
				sk->dead          = 1;
				sk->destroy       = 1;
				break;

			default:
				break;
		}
	} else {
		sk->state       = TCP_CLOSE;
		sk->state_change(sk);
		sk->dead = 1;
		ax25_destroy_socket(sk->ax25);
	}

	sock->data = NULL;	
	sk->socket = NULL;	/* Not used, but we should do this. **/

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

	call = ax25_findbyuid(current->euid);
	if (call == NULL && ax25_uid_policy && !suser())
		return -EPERM;
		
	if (call == NULL)
		sk->ax25->source_addr = addr->fsa_ax25.sax25_call;
	else
		sk->ax25->source_addr = *call;

	if (sk->debug)
		printk("AX25: source address set to %s\n", ax2asc(&sk->ax25->source_addr));

	if (addr_len == sizeof(struct full_sockaddr_ax25) && addr->fsa_ax25.sax25_ndigis == 1) {
		if (ax25cmp(&addr->fsa_digipeater[0], &null_ax25_address) == 0) {
			dev = NULL;
			if (sk->debug)
				printk("AX25: bound to any device\n");
		} else {
			if ((dev = ax25rtr_get_dev(&addr->fsa_digipeater[0])) == NULL) {
				if (sk->debug)
					printk("AX25: bind failed - no device\n");
				return -EADDRNOTAVAIL;
			}
			if (sk->debug)
				printk("AX25: bound to device %s\n", dev->name);
		}
	} else {
		if ((dev = ax25rtr_get_dev(&addr->fsa_ax25.sax25_call)) == NULL) {
			if (sk->debug)
				printk("AX25: bind failed - no device\n");
			return -EADDRNOTAVAIL;
		}
		if (sk->debug)
			printk("AX25: bound to device %s\n", dev->name);
	}

	ax25_fillin_cb(sk->ax25, dev);
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

	if (addr_len != sizeof(struct sockaddr_ax25) && addr_len != sizeof(struct full_sockaddr_ax25))
		return -EINVAL;

	/*
	 *	Handle digi-peaters to be used.
	 */
	if (addr_len == sizeof(struct full_sockaddr_ax25) && addr->sax25_ndigis != 0) {
		int ct           = 0;
		struct full_sockaddr_ax25 *fsa = (struct full_sockaddr_ax25 *)addr;

		/* Valid number of digipeaters ? */
		if (addr->sax25_ndigis < 1 || addr->sax25_ndigis > AX25_MAX_DIGIS)
			return -EINVAL;

		if (sk->ax25->digipeat == NULL) {
			if ((sk->ax25->digipeat = (ax25_digi *)kmalloc(sizeof(ax25_digi), GFP_KERNEL)) == NULL)
				return -ENOMEM;
		}

		sk->ax25->digipeat->ndigi = addr->sax25_ndigis;

		while (ct < addr->sax25_ndigis) {
			sk->ax25->digipeat->repeated[ct] = 0;
			sk->ax25->digipeat->calls[ct] = fsa->fsa_digipeater[ct];
			ct++;
		}

		sk->ax25->digipeat->lastrepeat = 0;
	} else { /* dl1bke 960117 */
		if (sk->debug)
			printk("building digipeater path\n");
		ax25_rt_build_path(sk->ax25, &addr->sax25_call);
	}

	/*
	 *	Must bind first - autobinding in this may or may not work. If
	 *	the socket is already bound, check to see if the device has
	 *	been filled in, error if it hasn't.
	 */
	if (sk->zapped) {
		if ((err = ax25_rt_autobind(sk->ax25, &addr->sax25_call)) < 0)
			return err;
		ax25_fillin_cb(sk->ax25, sk->ax25->device);
		ax25_insert_socket(sk->ax25);
	} else {
		if (sk->ax25->device == NULL)
			return -EHOSTUNREACH;
	}
		
	if (sk->type == SOCK_SEQPACKET && ax25_find_cb(&sk->ax25->source_addr, &addr->sax25_call, sk->ax25->device) != NULL)
		return -EBUSY;				/* Already such a connection */

	sk->ax25->dest_addr = addr->sax25_call;
	
	/* First the easy one */
	if (sk->type != SOCK_SEQPACKET) {
		sock->state = SS_CONNECTED;
		sk->state   = TCP_ESTABLISHED;
		return 0;
	}
	
	/* Move to connecting socket, ax.25 lapb WAIT_UA.. */	
	sock->state        = SS_CONNECTING;
	sk->state          = TCP_SYN_SENT;
	
	if (ax25_dev_is_dama_slave(sk->ax25->device))
		dama_establish_data_link(sk->ax25);
	else
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

	if (sk->state != TCP_ESTABLISHED) 
	{
		/* Not in ABM, not in WAIT_UA -> failed */
		sti();
		sock->state = SS_UNCONNECTED;
		return sock_error(sk);	/* Always set at this point */
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
	sax->fsa_ax25.sax25_call   = *addr;
	sax->fsa_ax25.sax25_ndigis = 0;
	*uaddr_len = sizeof(struct sockaddr_ax25);

	/* This will supply digipeat path on both getpeername() and getsockname() */
	if (sk->ax25->digipeat != NULL) {
		ndigi = sk->ax25->digipeat->ndigi;
		sax->fsa_ax25.sax25_ndigis = ndigi;
		*uaddr_len += AX25_ADDR_LEN * ndigi;
		for (i = 0; i < ndigi; i++)
			sax->fsa_digipeater[i] = sk->ax25->digipeat->calls[i];
	}

	return 0;
}
 
static int ax25_rcv(struct sk_buff *skb, struct device *dev, ax25_address *dev_addr, struct packet_type *ptype)
{
	struct sock *make;
	struct sock *sk;
	int type = 0;
	ax25_digi dp;
	ax25_cb *ax25;
	ax25_address src, dest;
	struct sock *raw;
	int mine = 0;
	int dama;

	/*
	 *	Process the AX.25/LAPB frame.
	 */
	 
	skb->h.raw = skb->data;
	
#ifdef CONFIG_FIREWALL
	if (call_in_firewall(PF_AX25, skb->dev, skb->h.raw, NULL) != FW_ACCEPT) {
		kfree_skb(skb, FREE_READ);
		return 0;
	}
#endif	

	/*
	 *	Parse the address header.
	 */
	 
	if (ax25_parse_addr(skb->data, skb->len, &src, &dest, &dp, &type, &dama) == NULL) {
		kfree_skb(skb, FREE_READ);
		return 0;
	}

	/*
	 *	Send the frame to the AX.25 auto-router
	 */
#ifdef notdef	/* dl1bke 960310 */
	ax25_rt_rx_frame(&src, dev, &dp);
#endif
	
	/*
	 *	Ours perhaps ?
	 */
	if (dp.lastrepeat + 1 < dp.ndigi) {		/* Not yet digipeated completely */
		if (ax25cmp(&dp.calls[dp.lastrepeat + 1], dev_addr) == 0) {
			struct device *dev_out = dev;

			/* We are the digipeater. Mark ourselves as repeated
			   and throw the packet back out of the same device */
			dp.lastrepeat++;
			dp.repeated[(int)dp.lastrepeat] = 1;

			if (ax25_dev_get_value(dev, AX25_VALUES_DIGI) & AX25_DIGI_XBAND) {
				while (dp.lastrepeat + 1 < dp.ndigi) {
					struct device *dev_scan;
					if ((dev_scan = ax25rtr_get_dev(&dp.calls[dp.lastrepeat + 1])) == NULL)
						break;
					dp.lastrepeat++;
					dp.repeated[(int)dp.lastrepeat] = 1;
					dev_out = dev_scan;
				}
				if (dev != dev_out && (ax25_dev_get_value(dev_out, AX25_VALUES_DIGI) & AX25_DIGI_XBAND) == 0) {
					kfree_skb(skb, FREE_READ);
					return 0;
				}
			}

			if (dev == dev_out && (ax25_dev_get_value(dev, AX25_VALUES_DIGI) & AX25_DIGI_INBAND) == 0) {
				kfree_skb(skb, FREE_READ);
				return 0;
			}

			ax25_rt_rx_frame(&src, dev, &dp);

			build_ax25_addr(skb->data, &src, &dest, &dp, type, MODULUS);
#ifdef CONFIG_FIREWALL
			if (call_fw_firewall(PF_AX25, skb->dev, skb->data, NULL) != FW_ACCEPT) {
				kfree_skb(skb, FREE_READ);
				return 0;
			}
#endif

			skb->arp = 1;
			ax25_queue_xmit(skb, dev_out, SOPRI_NORMAL);
		} else {
			kfree_skb(skb, FREE_READ);
		}

		return 0;
	}

	/*
	 *	Pull of the AX.25 headers leaving the CTRL/PID bytes
	 */
	skb_pull(skb, size_ax25_addr(&dp));

	/* For our port addresses ? */
	if (ax25cmp(&dest, dev_addr) == 0)
		mine = 1;

#ifdef CONFIG_NETROM
	/* Also match on any NET/ROM callsign */
	if (!mine && nr_dev_get(&dest) != NULL)
		mine = 1;
#endif	
	
	if ((*skb->data & ~0x10) == LAPB_UI) {	/* UI frame - bypass LAPB processing */
		skb->h.raw = skb->data + 2;		/* skip control and pid */

		if ((raw = ax25_addr_match(&dest)) != NULL)
			ax25_send_to_raw(raw, skb, skb->data[1]);

		if (!mine && ax25cmp(&dest, (ax25_address *)dev->broadcast) != 0) {
			kfree_skb(skb, FREE_READ);
			return 0;
		}

		/* Now we are pointing at the pid byte */
		switch (skb->data[1]) {
#ifdef CONFIG_INET		
			case AX25_P_IP:
				ax25_rt_rx_frame(&src, dev, &dp);
				skb_pull(skb,2);		/* drop PID/CTRL */
				ax25_ip_mode_set(&src, dev, 'D');
				ip_rcv(skb, dev, ptype);	/* Note ptype here is the wrong one, fix me later */
				break;

			case AX25_P_ARP:
				ax25_rt_rx_frame(&src, dev, &dp);
				skb_pull(skb,2);
				arp_rcv(skb, dev, ptype);	/* Note ptype here is wrong... */
				break;
#endif				
			case AX25_P_TEXT:
				/* Now find a suitable dgram socket */
				if ((sk = ax25_find_socket(&dest, &src, SOCK_DGRAM)) != NULL) {
					if (sk->rmem_alloc >= sk->rcvbuf) {
						kfree_skb(skb, FREE_READ);
					} else {
						ax25_rt_rx_frame(&src, dev, &dp);
						/*
						 *	Remove the control and PID.
						 */
						skb_pull(skb, 2);
						skb_queue_tail(&sk->receive_queue, skb);
						skb->sk = sk;
						atomic_add(skb->truesize, &sk->rmem_alloc);
						if (!sk->dead)
							sk->data_ready(sk, skb->len);
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

	/*
	 *	Is connected mode supported on this device ?
	 *	If not, should we DM the incoming frame (except DMs) or
	 *	silently ignore them. For now we stay quiet.
	 */
	if (!ax25_dev_get_value(dev, AX25_VALUES_CONMODE)) {
		kfree_skb(skb, FREE_READ);
		return 0;
	}
	
	/* LAPB */
	
	/* AX.25 state 1-4 */
	
	if ((ax25 = ax25_find_cb(&dest, &src, dev)) != NULL) {
		/*
		 *	Process the frame. If it is queued up internally it returns one otherwise we 
		 *	free it immediately. This routine itself wakes the user context layers so we
		 *	do no further work
		 */
		ax25_rt_rx_frame(&src, dev, &dp);
		if (ax25_process_rx_frame(ax25, skb, type, dama) == 0)
			kfree_skb(skb, FREE_READ);

		return 0;
	}

	/* AX.25 state 0 (disconnected) */

	/* a) received not a SABM(E) */
	
	if ((*skb->data & ~PF) != SABM && (*skb->data & ~PF) != SABME) {
		/*
		 *	Never reply to a DM. Also ignore any connects for
		 *	addresses that are not our interfaces and not a socket.
		 */
		if ((*skb->data & ~PF) != DM && mine)
			ax25_return_dm(dev, &src, &dest, &dp);

		kfree_skb(skb, FREE_READ);
		return 0;
	}

	/* b) received SABM(E) */
	
	if ((sk = ax25_find_listener(&dest, dev, SOCK_SEQPACKET)) != NULL) {
		ax25_rt_rx_frame(&src, dev, &dp);
		if (sk->ack_backlog == sk->max_ack_backlog || (make = ax25_make_new(sk, dev)) == NULL) {
			if (mine)
				ax25_return_dm(dev, &src, &dest, &dp);

			kfree_skb(skb, FREE_READ);
			return 0;
		}
		
		ax25 = make->ax25;

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
		
		ax25_rt_rx_frame(&src, dev, &dp);

		if ((ax25 = ax25_create_cb()) == NULL) {
			ax25_return_dm(dev, &src, &dest, &dp);
			kfree_skb(skb, FREE_READ);
			return 0;
		}

		ax25_fillin_cb(ax25, dev);
		ax25->idletimer = ax25->idle;
#else
		if (mine) {
			ax25_rt_rx_frame(&src, dev, &dp);
			ax25_return_dm(dev, &src, &dest, &dp);
		}

		kfree_skb(skb, FREE_READ);
		return 0;
#endif
	}

	ax25->source_addr = dest;
	ax25->dest_addr   = src;

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

	if ((*skb->data & ~PF) == SABME) {
		ax25->modulus = EMODULUS;
		ax25->window  = ax25_dev_get_value(dev, AX25_VALUES_EWINDOW);
	} else {
		ax25->modulus = MODULUS;
		ax25->window  = ax25_dev_get_value(dev, AX25_VALUES_WINDOW);
	}

	ax25->device = dev;
	
	ax25_send_control(ax25, UA, POLLON, C_RESPONSE);

	if (dama) ax25_dama_on(ax25);	/* bke 951121 */

	ax25->dama_slave = dama;
	ax25->t3timer = ax25->t3;
	ax25->state   = AX25_STATE_3;

	ax25_insert_socket(ax25);

	ax25_set_timer(ax25);

	if (sk != NULL) {
		if (!sk->dead)
			sk->data_ready(sk, skb->len );
	} else {
		kfree_skb(skb, FREE_READ);
	}

	return 0;
}

/*
 *	Receive an AX.25 frame via a SLIP interface.
 */
static int kiss_rcv(struct sk_buff *skb, struct device *dev, struct packet_type *ptype)
{
	skb->sk = NULL;		/* Initially we don't know who it's for */

	if ((*skb->data & 0x0F) != 0) {
		kfree_skb(skb, FREE_READ);	/* Not a KISS data frame */
		return 0;
	}

	skb_pull(skb, AX25_KISS_HEADER_LEN);	/* Remove the KISS byte */

	return ax25_rcv(skb, dev, (ax25_address *)dev->dev_addr, ptype);
}

#ifdef CONFIG_BPQETHER
/*
 *	Receive an AX.25 frame via an Ethernet interface.
 */
static int bpq_rcv(struct sk_buff *skb, struct device *dev, struct packet_type *ptype)
{
	ax25_address *port_call;
	int len;

	skb->sk = NULL;		/* Initially we don't know who it's for */

	if ((port_call = ax25_bpq_get_addr(dev)) == NULL) {
		kfree_skb(skb, FREE_READ);	/* We have no port callsign */
		return 0;
	}

	len = skb->data[0] + skb->data[1] * 256 - 5;

	skb_pull(skb, 2);	/* Remove the length bytes */
	skb_trim(skb, len);	/* Set the length of the data */

	return ax25_rcv(skb, dev, port_call, ptype);
}
#endif

static int ax25_sendmsg(struct socket *sock, struct msghdr *msg, int len, int noblock, int flags)
{
	struct sock *sk = (struct sock *)sock->data;
	struct sockaddr_ax25 *usax = (struct sockaddr_ax25 *)msg->msg_name;
	int err;
	struct sockaddr_ax25 sax;
	struct sk_buff *skb;
	unsigned char *asmptr;
	int size;
	ax25_digi *dp;
	ax25_digi dtmp;
	int lv;
	int addr_len=msg->msg_namelen;
	
	if (sk->err) {
		return sock_error(sk);
	}

	if (flags|| msg->msg_control)
		return -EINVAL;

	if (sk->zapped)
		return -EADDRNOTAVAIL;
		
	if (sk->ax25->device == NULL)
		return -ENETUNREACH;
		
	if (usax) {
		if (addr_len != sizeof(struct sockaddr_ax25) && addr_len != sizeof(struct full_sockaddr_ax25))
			return -EINVAL;
		if (usax->sax25_family != AF_AX25)
			return -EINVAL;
		if (addr_len == sizeof(struct full_sockaddr_ax25) && usax->sax25_ndigis != 0) {
			int ct           = 0;
			struct full_sockaddr_ax25 *fsa = (struct full_sockaddr_ax25 *)usax;

			/* Valid number of digipeaters ? */
			if (usax->sax25_ndigis < 1 || usax->sax25_ndigis > AX25_MAX_DIGIS)
				return -EINVAL;

			dtmp.ndigi      = usax->sax25_ndigis;

			while (ct < usax->sax25_ndigis) {
				dtmp.repeated[ct] = 0;
				dtmp.calls[ct]    = fsa->fsa_digipeater[ct];
				ct++;
			}

			dtmp.lastrepeat = 0;
		}

		sax = *usax;
		if (sk->type == SOCK_SEQPACKET && ax25cmp(&sk->ax25->dest_addr, &sax.sax25_call) != 0)
			return -EISCONN;
		if (usax->sax25_ndigis == 0)
			dp = NULL;
		else
			dp = &dtmp;
	} else {
		if (sk->state != TCP_ESTABLISHED)
			return -ENOTCONN;
		sax.sax25_family = AF_AX25;
		sax.sax25_call   = sk->ax25->dest_addr;
		dp = sk->ax25->digipeat;
	}
	
	if (sk->debug)
		printk("AX.25: sendto: Addresses built.\n");

	/* Build a packet */
	if (sk->debug)
		printk("AX.25: sendto: building packet.\n");

	/* Assume the worst case */
	size = len + 3 + size_ax25_addr(dp) + AX25_BPQ_HEADER_LEN;

	if ((skb = sock_alloc_send_skb(sk, size, 0, 0, &err)) == NULL)
		return err;

	skb->sk   = sk;
	skb->free = 1;
	skb->arp  = 1;

	skb_reserve(skb, size - len);

	if (sk->debug)
		printk("AX.25: Appending user data\n");

	/* User data follows immediately after the AX.25 data */
	memcpy_fromiovec(skb_put(skb, len), msg->msg_iov, len);

	/* Add the PID, usually AX25_TEXT */
	asmptr  = skb_push(skb, 1);
	*asmptr = sk->protocol;

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
		asmptr = skb_push(skb, 1 + size_ax25_addr(dp));

		if (sk->debug) {
			printk("Building AX.25 Header (dp=%p).\n", dp);
			if (dp != 0)
				printk("Num digipeaters=%d\n", dp->ndigi);
		}

		/* Build an AX.25 header */
		asmptr += (lv = build_ax25_addr(asmptr, &sk->ax25->source_addr, &sax.sax25_call, dp, C_COMMAND, MODULUS));

		if (sk->debug)
			printk("Built header (%d bytes)\n",lv);

		skb->h.raw = asmptr;
	
		if (sk->debug)
			printk("base=%p pos=%p\n", skb->data, asmptr);

		*asmptr = LAPB_UI;

		/* Datagram frames go straight out of the door as UI */
		ax25_queue_xmit(skb, sk->ax25->device, SOPRI_NORMAL);

		return len;
	}
		
}

static int ax25_recvmsg(struct socket *sock, struct msghdr *msg, int size, int noblock, int flags, int *addr_len)
{
	struct sock *sk = (struct sock *)sock->data;
	struct sockaddr_ax25 *sax = (struct sockaddr_ax25 *)msg->msg_name;
	int copied, length;
	struct sk_buff *skb;
	int er;
	int dama;

	if (sk->err) {
		return sock_error(sk);
	}
	
	if (addr_len != NULL)
		*addr_len = sizeof(*sax);

	/*
	 * 	This works for seqpacket too. The receiver has ordered the
	 *	queue for us! We do one quick check first though
	 */
	if (sk->type == SOCK_SEQPACKET && sk->state != TCP_ESTABLISHED)
		return -ENOTCONN;

	/* Now we can treat all alike */
	if ((skb = skb_recv_datagram(sk, flags, noblock, &er)) == NULL)
		return er;

	if (sk->ax25->hdrincl) {
		length = skb->len + (skb->data - skb->h.raw);
	} else {
		if (sk->type == SOCK_SEQPACKET)
			skb_pull(skb, 1);		/* Remove PID */
		length     = skb->len;
		skb->h.raw = skb->data;
	}

	copied = (size < length) ? size : length;
	skb_copy_datagram_iovec(skb, 0, msg->msg_iov, copied);
	
	if (sax) {
		ax25_digi digi;
		ax25_address dest;

		if (addr_len == (int *)0)
			return -EINVAL;
		if (*addr_len != sizeof(struct sockaddr_ax25) && *addr_len != sizeof(struct full_sockaddr_ax25))
			return -EINVAL;

		ax25_parse_addr(skb->data, skb->len, NULL, &dest, &digi, NULL, &dama);

		sax->sax25_family = AF_AX25;
		/* We set this correctly, even though we may not let the
		   application know the digi calls further down (because it
		   did NOT ask to know them).  This could get political... **/
		sax->sax25_ndigis = digi.ndigi;
		sax->sax25_call   = dest;

		*addr_len = sizeof(struct sockaddr_ax25);

		if (*addr_len == sizeof(struct full_sockaddr_ax25) && sax->sax25_ndigis != 0) {
			int ct           = 0;
			struct full_sockaddr_ax25 *fsa = (struct full_sockaddr_ax25 *)sax;

			while (ct < digi.ndigi) {
				fsa->fsa_digipeater[ct] = digi.calls[ct];
				ct++;
			}

			*addr_len = sizeof(struct full_sockaddr_ax25);
		}
	}

	skb_free_datagram(sk, skb);

	return copied;
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

#ifdef CONFIG_BPQETHER
		case SIOCAX25BPQADDR:
			if (!suser())
				return -EPERM;
			return ax25_bpq_ioctl(cmd, (void *)arg);
#endif

		case SIOCAX25GETPARMS:
		case SIOCAX25SETPARMS:
			return ax25_dev_ioctl(cmd, (void *)arg);

		case SIOCADDRT:
		case SIOCDELRT:
		case SIOCAX25OPTRT:
			if (!suser())
				return -EPERM;
			return ax25_rt_ioctl(cmd, (void *)arg);
			
		case SIOCAX25CTLCON:
			if (!suser())
				return -EPERM;
			return ax25_ctl_ioctl(cmd, (void *)arg);

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


static int ax25_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	ax25_cb *ax25;
	struct device *dev;
	const char *devname;
	int len = 0;
	off_t pos = 0;
	off_t begin = 0;

	cli();

	len += sprintf(buffer, "dest_addr src_addr  dev  st  vs  vr  va    t1     t2     t3      idle   n2  rtt wnd paclen   dama Snd-Q Rcv-Q\n");

	for (ax25 = ax25_list; ax25 != NULL; ax25 = ax25->next) {
		if ((dev = ax25->device) == NULL)
			devname = "???";
		else
			devname = dev->name;

		len += sprintf(buffer + len, "%-9s ",
			ax2asc(&ax25->dest_addr));
		len += sprintf(buffer + len, "%-9s %-4s %2d %3d %3d %3d %3d/%03d %2d/%02d %3d/%03d %3d/%03d %2d/%02d %3d %3d  %5d",
			ax2asc(&ax25->source_addr), devname,
			ax25->state,
			ax25->vs, ax25->vr, ax25->va,
			ax25->t1timer / PR_SLOWHZ,
			ax25->t1      / PR_SLOWHZ,
			ax25->t2timer / PR_SLOWHZ,
			ax25->t2      / PR_SLOWHZ,
			ax25->t3timer / PR_SLOWHZ,
			ax25->t3      / PR_SLOWHZ,
			ax25->idletimer / (PR_SLOWHZ * 60),
			ax25->idle      / (PR_SLOWHZ * 60),
			ax25->n2count, ax25->n2,
			ax25->rtt     / PR_SLOWHZ,
			ax25->window,
			ax25->paclen);
			
		len += sprintf(buffer + len, " %s", ax25->dama_slave? " slave" : "    no");

		if (ax25->sk != NULL) {
			len += sprintf(buffer + len, " %5d %5d\n",
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
	ax25_select,
	ax25_ioctl,
	ax25_listen,
	ax25_shutdown,
	ax25_setsockopt,
	ax25_getsockopt,
	ax25_fcntl,
	ax25_sendmsg,
	ax25_recvmsg
};

/*
 *	Called by socket.c on kernel start up
 */

static struct packet_type ax25_packet_type = 
{
	0,	/* MUTTER ntohs(ETH_P_AX25),*/
	0,		/* copy */
	kiss_rcv,
	NULL,
	NULL,
};

#ifdef CONFIG_BPQETHER
static struct packet_type bpq_packet_type = 
{
	0,	/* MUTTER ntohs(ETH_P_BPQ),*/
	0,		/* copy */
	bpq_rcv,
	NULL,
	NULL,
};
#endif

static struct notifier_block ax25_dev_notifier = {
	ax25_device_event,
	0
};

void ax25_proto_init(struct net_proto *pro)
{
	sock_register(ax25_proto_ops.family, &ax25_proto_ops);
	ax25_packet_type.type = htons(ETH_P_AX25);
	dev_add_pack(&ax25_packet_type);	
#ifdef CONFIG_BPQETHER
	bpq_packet_type.type  = htons(ETH_P_BPQ);
	dev_add_pack(&bpq_packet_type);
#endif
	register_netdevice_notifier(&ax25_dev_notifier);
#ifdef CONFIG_PROC_FS			  
	proc_net_register(&(struct proc_dir_entry) {
		PROC_NET_AX25_ROUTE, 10, "ax25_route",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_net_inode_operations,
		ax25_rt_get_info
	});
	proc_net_register(&(struct proc_dir_entry) {
		PROC_NET_AX25, 4, "ax25",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_net_inode_operations,
		ax25_get_info
	});
	proc_net_register(&(struct proc_dir_entry) {
		PROC_NET_AX25_CALLS, 10, "ax25_calls",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_net_inode_operations,
		ax25_cs_get_info
	});
#endif	

	printk(KERN_INFO "G4KLX/GW4PTS AX.25 for Linux. Version 0.32 for Linux NET3.035 (Linux 2.0)\n");

#ifdef CONFIG_BPQETHER
	proc_net_register(&(struct proc_dir_entry) {
		PROC_NET_AX25_BPQETHER, 13, "ax25_bpqether",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_net_inode_operations,
		ax25_bpq_get_info
	});

	printk(KERN_INFO "G8BPQ Encapsulation of AX.25 frames enabled\n");
#endif
}

/*
 *	A small shim to dev_queue_xmit to handle the difference between
 *	KISS AX.25 and BPQ AX.25.
 */

void ax25_queue_xmit(struct sk_buff *skb, struct device *dev, int pri)
{
	unsigned char *ptr;
	
#ifdef CONFIG_FIREWALL
	if (call_out_firewall(PF_AX25, skb->dev, skb->data, NULL) != FW_ACCEPT) {
		dev_kfree_skb(skb, FREE_WRITE);
		return;
	}
#endif	

	skb->protocol = htons (ETH_P_AX25);

#ifdef CONFIG_BPQETHER
	if(dev->type == ARPHRD_ETHER) {
		static char bcast_addr[6]={0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
		int size;

		if(skb_headroom(skb) < AX25_BPQ_HEADER_LEN) {
			printk(KERN_CRIT "ax25_queue_xmit: not enough space to add BPQ Ether header\n");
			dev_kfree_skb(skb, FREE_WRITE);
			return;
		}

		size = skb->len;
	
		ptr = skb_push(skb, 2);

		*ptr++ = (size + 5) % 256;
		*ptr++ = (size + 5) / 256;

		dev->hard_header(skb, dev, ETH_P_BPQ, bcast_addr, NULL, 0);
		dev_queue_xmit(skb, dev, pri);
		return;
	} 
#endif

	ptr = skb_push(skb, 1);
	*ptr++ = 0;			/* KISS */
	dev_queue_xmit(skb, dev, pri);
}

/*
 *	IP over AX.25 encapsulation.
 */

/*
 *	Shove an AX.25 UI header on an IP packet and handle ARP
 */

#ifdef CONFIG_INET
 
int ax25_encapsulate(struct sk_buff *skb, struct device *dev, unsigned short type, void *daddr,
		void *saddr, unsigned len)
{
  	/* header is an AX.25 UI frame from us to them */
 	unsigned char *buff = skb_push(skb, AX25_HEADER_LEN);

  	*buff++ = 0;	/* KISS DATA */
  	
	if (daddr != NULL)
		memcpy(buff, daddr, dev->addr_len);	/* Address specified */

  	buff[6] &= ~LAPB_C;
  	buff[6] &= ~LAPB_E;
  	buff[6] |= SSSID_SPARE;
  	buff += AX25_ADDR_LEN;

  	if (saddr != NULL)
  		memcpy(buff, saddr, dev->addr_len);
  	else
  		memcpy(buff, dev->dev_addr, dev->addr_len);

  	buff[6] &= ~LAPB_C;
  	buff[6] |= LAPB_E;
  	buff[6] |= SSSID_SPARE;
  	buff   += AX25_ADDR_LEN;

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
  			printk(KERN_ERR "wrong protocol type 0x%x2.2\n", type);
  			*buff++ = 0;
  			break;
 	}
	
	if (daddr != NULL)
	  	return AX25_HEADER_LEN;

	return -AX25_HEADER_LEN;	/* Unfinished header */
}

int ax25_rebuild_header(unsigned char *bp, struct device *dev, unsigned long dest, struct sk_buff *skb)
{
	struct sk_buff *ourskb;
	int mode;

  	if (arp_find(bp + 1, dest, dev, dev->pa_addr, skb))
  		return 1;

	if (bp[16] == AX25_P_IP) {
		mode = ax25_ip_mode_get((ax25_address *)(bp + 1), dev);
		if (mode == 'V' || mode == 'v' || (mode == ' ' && ax25_dev_get_value(dev, AX25_VALUES_IPDEFMODE) == 'V')) 
		{
			/*
			 *	This is a workaround to try to keep the device locking
			 *	straight until skb->free=0 is abolished post 1.4.
			 *
			 *	We clone the buffer and release the original thereby
			 *	keeping it straight
			 *
			 *	Note: we report 1 back so the caller will
			 *	not feed the frame direct to the physical device
			 *	We don't want that to happen. (It won't be upset
			 *	as we have pulled the frame from the queue by
			 *	freeing it).
			 */
			if ((ourskb = skb_clone(skb, GFP_ATOMIC)) == NULL) {
				dev_kfree_skb(skb, FREE_WRITE);
				return 1;
			}

			ourskb->sk = skb->sk;

			if (ourskb->sk != NULL)
				atomic_add(ourskb->truesize, &ourskb->sk->wmem_alloc);

			dev_kfree_skb(skb, FREE_WRITE);

			skb_pull(ourskb, AX25_HEADER_LEN - 1);	/* Keep PID */

			ax25_send_frame(ourskb, (ax25_address *)(bp + 8), (ax25_address *)(bp + 1), NULL, dev);

			return 1;
		}
	}

  	bp[7]  &= ~LAPB_C;
  	bp[7]  &= ~LAPB_E;
  	bp[7]  |= SSSID_SPARE;

  	bp[14] &= ~LAPB_C;
  	bp[14] |= LAPB_E;
  	bp[14] |= SSSID_SPARE;
  	
  	/*
  	 * dl1bke 960317: we use ax25_queue_xmit here to allow mode datagram
  	 *		  over ethernet. I don't know if this is valid, though.
  	 */

	ax25_dg_build_path(skb, (ax25_address *)(bp + 1), dev);
	ax25_queue_xmit(skb, dev, SOPRI_NORMAL);

  	return 1;
}	

#endif

#endif
