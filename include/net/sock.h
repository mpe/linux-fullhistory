/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the AF_INET socket handler.
 *
 * Version:	@(#)sock.h	1.0.4	05/13/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Corey Minyard <wf-rch!minyard@relay.EU.net>
 *		Florian La Roche <flla@stud.uni-sb.de>
 *
 * Fixes:
 *		Alan Cox	:	Volatiles in skbuff pointers. See
 *					skbuff comments. May be overdone,
 *					better to prove they can be removed
 *					than the reverse.
 *		Alan Cox	:	Added a zapped field for tcp to note
 *					a socket is reset and must stay shut up
 *		Alan Cox	:	New fields for options
 *	Pauline Middelink	:	identd support
 *		Alan Cox	:	Eliminate low level recv/recvfrom
 *		David S. Miller	:	New socket lookup architecture.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _SOCK_H
#define _SOCK_H

#include <linux/config.h>
#include <linux/timer.h>
#include <linux/in.h>		/* struct sockaddr_in */

#if defined(CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
#include <linux/in6.h>		/* struct sockaddr_in6 */
#include <linux/ipv6.h>		/* dest_cache, inet6_options */
#include <linux/icmpv6.h>
#include <net/if_inet6.h>	/* struct ipv6_mc_socklist */
#endif

#include <linux/tcp.h>		/* struct tcphdr */
#include <linux/config.h>

#include <linux/netdevice.h>
#include <linux/skbuff.h>	/* struct sk_buff */
#include <net/protocol.h>		/* struct inet_protocol */
#if defined(CONFIG_X25) || defined(CONFIG_X25_MODULE)
#include <net/x25.h>
#endif
#if defined(CONFIG_AX25) || defined(CONFIG_AX25_MODULE)
#include <net/ax25.h>
#if defined(CONFIG_NETROM) || defined(CONFIG_NETROM_MODULE)
#include <net/netrom.h>
#endif
#if defined(CONFIG_ROSE) || defined(CONFIG_ROSE_MODULE)
#include <net/rose.h>
#endif
#endif

#if defined(CONFIG_IPX) || defined(CONFIG_IPX_MODULE)
#include <net/ipx.h>
#endif

#if defined(CONFIG_ATALK) || defined(CONFIG_ATALK_MODULE)
#include <linux/atalk.h>
#endif

#include <linux/igmp.h>

#include <asm/atomic.h>

/*
 *	The AF_UNIX specific socket options
 */
 
struct unix_opt
{
	int 			family;
	char *			name;
	int  			locks;
	struct unix_address	*addr;
	struct inode *		inode;
	struct semaphore	readsem;
	struct sock *		other;
	struct sock **		list;
	int 			marksweep;
#define MARKED			1
	int			inflight;
};

/*
 *	IP packet socket options
 */

struct inet_packet_opt
{
	struct notifier_block	notifier;		/* Used when bound */
	struct device		*bound_dev;
	unsigned long		dev_stamp;
	struct packet_type	*prot_hook;
	char			device_name[15];
};

/*
 *	Once the IPX ncpd patches are in these are going into protinfo
 */

#if defined(CONFIG_IPX) || defined(CONFIG_IPX_MODULE)
struct ipx_opt
{
	ipx_address		dest_addr;
	ipx_interface		*intrfc;
	unsigned short		port;
#ifdef CONFIG_IPX_INTERN
	unsigned char           node[IPX_NODE_LEN];
#endif
	unsigned short		type;
/* 
 * To handle asynchronous messages from the NetWare server, we have to
 * know the connection this socket belongs to. 
 */
	struct ncp_server       *ncp_server;
	
};
#endif

#if defined(CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
struct ipv6_pinfo
{
	struct in6_addr 	saddr;
	struct in6_addr 	rcv_saddr;
	struct in6_addr		daddr;

	__u32			flow_lbl;
	__u8			priority;
	__u8			hop_limit;

	__u8			mcast_hops;

	/* sockopt flags */

	__u8			recvsrcrt:1,
	                        rxinfo:1,
				rxhlim:1,
				hopopts:1,
				dstopts:1,
				mc_loop:1,
                                unused:2;

	/* device for outgoing mcast packets */

	struct device		*mc_if;

	struct ipv6_mc_socklist	*ipv6_mc_list;
	/* 
	 * destination cache entry pointer
	 * contains a pointer to neighbour cache
	 * and other info related to network level 
	 * (ex. PMTU)
	 */
	
	struct dest_entry	*dest;
	__u32			dc_sernum;

	struct ipv6_options	*opt;
};

struct raw6_opt {
	__u32			checksum;	/* perform checksum */
	__u32			offset;		/* checksum offset  */

	struct icmp6_filter	filter;
};

#endif /* IPV6 */

struct tcp_opt
{
/*
 *	RFC793 variables by their proper names. This means you can
 *	read the code and the spec side by side (and laugh ...)
 *	See RFC793 and RFC1122. The RFC writes these in capitals.
 */
 	__u32	rcv_nxt;	/* What we want to receive next 	*/
 	__u32	rcv_up;		/* The urgent point (may not be valid) 	*/
 	__u32	rcv_wnd;	/* Current receiver window		*/
 	__u32	snd_nxt;	/* Next sequence we send		*/
 	__u32	snd_una;	/* First byte we want an ack for	*/
	__u32	snd_up;		/* Outgoing urgent pointer		*/
	__u32	snd_wl1;	/* Sequence for window update		*/
	__u32	snd_wl2;	/* Ack sequence for update		*/

	__u32	rcv_wup;	/* rcv_nxt on last window update sent	*/


	__u32	srtt;		/* smothed round trip time << 3		*/
	__u32	mdev;		/* medium deviation			*/
	__u32	rto;		/* retransmit timeout			*/
	__u32	backoff;	/* backoff				*/
/*
 *	Slow start and congestion control (see also Nagle, and Karn & Partridge)
 */
 	__u32	snd_cwnd;	/* Sending congestion window		*/
 	__u32	snd_ssthresh;	/* Slow start size threshold		*/
/*
 *	Timers used by the TCP protocol layer
 */
 	struct timer_list	delack_timer;		/* Ack delay 	*/
 	struct timer_list	idle_timer;		/* Idle watch 	*/
 	struct timer_list	completion_timer;	/* Up/Down timer */
 	struct timer_list	probe_timer;		/* Probes	*/
 	struct timer_list	retransmit_timer;	/* Resend (no ack) */

	__u32	basertt;	/* Vegas baseRTT */

	__u8	delayed_acks;
	__u8	dup_acks;

	__u32	lrcvtime;	/* timestamp of last received data packet  */
	__u32	rcv_tstamp;	/* timestamp of last received packet  */
	__u32	iat_mdev;	/* interarrival time medium deviation */
	__u32	iat;		/* interarrival time */
	__u32	ato;		/* delayed ack timeout */

	__u32	high_seq;
/*
 *	new send pointers
 */
	struct sk_buff *	send_head;
	struct sk_buff *	retrans_head;	/* retrans head can be 
						 * different to the head of
						 * write queue if we are doing
						 * fast retransmit
						 */
/*
 * pending events
 */
	__u8	pending;

/*
 *	Header prediction flags
 *	0x5?10 << 16 + snd_wnd in net byte order
 */
	__u32	pred_flags;
	__u32	snd_wnd;		/* The window we expect to receive */

	__u32	probes_out;		/* unanswered 0 window probes	   */

	struct open_request	*syn_wait_queue;
	struct tcp_func		*af_specific;
};

 	
/*
 * This structure really needs to be cleaned up.
 * Most of it is for TCP, and not used by any of
 * the other protocols.
 */

/*
 * The idea is to start moving to a newer struct gradualy
 * 
 * IMHO the newer struct should have the following format:
 * 
 *	struct sock {
 *		sockmem [mem, proto, callbacks]
 *
 *		union or struct {
 *			netrom;
 *			ax_25;
 *		} ll_pinfo;
 *	
 *		union {
 *			ipv4;
 *			ipv6;
 *			ipx;
 *		} net_pinfo;
 *
 *		union {
 *			tcp;
 *			udp;
 *			spx;
 *		} tp_pinfo;
 *
 *	}
 */

/* Define this to get the sk->debug debugging facility. */
#define SOCK_DEBUGGING
#ifdef SOCK_DEBUGGING
#define SOCK_DEBUG(sk, msg...) if((sk) && ((sk)->debug)) printk(KERN_DEBUG ## msg)
#else
#define SOCK_DEBUG(sk, msg...) do { } while (0)
#endif

/*
 *  TCP will start to use the new protinfo while *still using the old* fields 
 */

struct sock 
{
	/* This must be first. */
	struct sock		*sklist_next;
	struct sock		*sklist_prev;

	atomic_t		wmem_alloc;
	atomic_t		rmem_alloc;
	unsigned long		allocation;		/* Allocation mode */
	__u32			write_seq;
	__u32			copied_seq;
	__u32			fin_seq;
	__u32			syn_seq;
	__u32			urg_seq;
	__u32			urg_data;
	int			users;			/* user count */

	unsigned char		delayed_acks,
				dup_acks;
  /*
   *	Not all are volatile, but some are, so we
   * 	might as well say they all are.
   */
	volatile char		dead,
				urginline,
				intr,
				blog,
				done,
				reuse,
				keepopen,
				linger,
				destroy,
				ack_timed,
				no_check,
				zapped,	/* In ax25 & ipx means not linked */
				broadcast,
				nonagle,
				bsdism;
	unsigned long	        lingertime;
	int			proc;

	struct sock             **hashtable;
	int			hashent;
	struct sock		*next;
	struct sock		*prev;
	struct sock		*pair;

	struct sk_buff		* send_head;
	struct sk_buff		* send_tail;

	struct sk_buff_head	back_log;
	struct sk_buff		*partial;
	struct timer_list	partial_timer;
	atomic_t		retransmits;

	struct sk_buff_head	write_queue,
				receive_queue,
				out_of_order_queue,
	                        error_queue;

	unsigned short		family;
	struct proto		*prot;
	struct wait_queue	**sleep;

	__u32			daddr;
	__u32			saddr;		/* Sending source */
	__u32			rcv_saddr;	/* Bound address */

	struct dst_entry	*dst_cache;

	unsigned short		max_unacked;


	unsigned short		bytes_rcv;
/*
 *	mss is min(mtu, max_window) 
 */
	unsigned short		mtu;       /* mss negotiated in the syn's */
	unsigned short		mss;       /* current eff. mss - can change */
	unsigned short		user_mss;  /* mss requested by user in ioctl */
	unsigned short		max_window;
	unsigned long 		window_clamp;
	unsigned int		ssthresh;
	unsigned short		num;

	unsigned short		cong_window;
	unsigned short		cong_count;
	atomic_t		packets_out;
	unsigned short		shutdown;

#if defined(CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
	union {
		struct ipv6_pinfo	af_inet6;
	} net_pinfo;
#endif

	union {
		struct tcp_opt		af_tcp;
#if defined(CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
		struct raw6_opt		tp_raw;
#endif
	} tp_pinfo;
/*
 *	currently backoff isn't used, but I'm maintaining it in case
 *	we want to go back to a backoff formula that needs it
 */
/* 
	unsigned short		backoff;
 */
	int			err, err_soft;	/* Soft holds errors that don't
						   cause failure but are the cause
						   of a persistent failure not just
						   'timed out' */
	unsigned char		protocol;
	volatile unsigned char	state;
	unsigned char		ack_backlog;
	unsigned char		max_ack_backlog;
	unsigned char		priority;
	unsigned char		debug;
	int			rcvbuf;
	int			sndbuf;
	unsigned short		type;
	unsigned char		localroute;	/* Route locally only */
	struct ucred		peercred;
  
/*
 *	This is where all the private (optional) areas that don't
 *	overlap will eventually live. 
 */

	union
	{
	  	struct unix_opt	af_unix;
#if defined(CONFIG_ATALK) || defined(CONFIG_ATALK_MODULE)
		struct atalk_sock	af_at;
#endif
#if defined(CONFIG_IPX) || defined(CONFIG_IPX_MODULE)
		struct ipx_opt		af_ipx;
#endif
#ifdef CONFIG_INET
		struct inet_packet_opt  af_packet;
#ifdef CONFIG_NUTCP		
		struct tcp_opt		af_tcp;
#endif		
#endif
#if defined(CONFIG_X25) || defined(CONFIG_X25_MODULE)
		x25_cb			*x25;
#endif
#if defined(CONFIG_AX25) || defined(CONFIG_AX25_MODULE)
		ax25_cb			*ax25;
#if defined(CONFIG_NETROM) || defined(CONFIG_NETROM_MODULE)
		nr_cb			*nr;
#endif
#if defined(CONFIG_ROSE) || defined(CONFIG_ROSE_MODULE)
		rose_cb			*rose;
#endif
#endif
	} protinfo;  		

/* 
 *	IP 'private area' or will be eventually 
 */
	int			ip_ttl;			/* TTL setting */
	int			ip_tos;			/* TOS */
	unsigned	   	ip_cmsg_flags;
	struct tcphdr		dummy_th;
	struct timer_list	keepalive_timer;	/* TCP keepalive hack */
	struct timer_list	retransmit_timer;	/* TCP retransmit timer */
	struct timer_list	delack_timer;		/* TCP delayed ack timer */
	int			ip_xmit_timeout;	/* Why the timeout is running */
	struct ip_options	*opt;
	unsigned char		ip_hdrincl;		/* Include headers ? */
	__u8			ip_mc_ttl;		/* Multicasting TTL */
	__u8			ip_mc_loop;		/* Loopback */
	__u8			ip_recverr;
	__u8			ip_pmtudisc;
	int			ip_mc_index;		/* Multicast device index */
	__u32			ip_mc_addr;
	struct ip_mc_socklist	*ip_mc_list;		/* Group array */

/*
 *	This part is used for the timeout functions (timer.c). 
 */
 
	int			timeout;	/* What are we waiting for? */
	struct timer_list	timer;		/* This is the TIME_WAIT/receive timer
					 * when we are doing IP
					 */
	struct timeval		stamp;

 /*
  *	Identd 
  */
  
	struct socket		*socket;
  
  /*
   *	Callbacks 
   */
   
	void			(*state_change)(struct sock *sk);
	void			(*data_ready)(struct sock *sk,int bytes);
	void			(*write_space)(struct sock *sk);
	void			(*error_report)(struct sock *sk);

  	int			(*backlog_rcv) (struct sock *sk,
						struct sk_buff *skb);  
};

#if 0
/*
 *	Inet protocol options
 */
struct inet_options {
	__u8				version;
	union {
		struct options		opt_v4;
		struct ipv6_options	opt_v6;
	} u;
};
#endif

/*
 *	IP protocol blocks we attach to sockets.
 *	socket layer -> transport layer interface
 *	transport -> network interface is defined by struct inet_proto
 */
 
struct proto 
{
	/* These must be first. */
	struct sock		*sklist_next;
	struct sock		*sklist_prev;

	void			(*close)(struct sock *sk, 
					unsigned long timeout);
	int			(*connect)(struct sock *sk,
				        struct sockaddr *uaddr, 
					int addr_len);

	struct sock *		(*accept) (struct sock *sk, int flags);
	void			(*retransmit)(struct sock *sk, int all);
	void			(*write_wakeup)(struct sock *sk);
	void			(*read_wakeup)(struct sock *sk);

	unsigned int		(*poll)(struct socket *sock, poll_table *wait);

	int			(*ioctl)(struct sock *sk, int cmd,
					 unsigned long arg);
	int			(*init)(struct sock *sk);
	int			(*destroy)(struct sock *sk);
	void			(*shutdown)(struct sock *sk, int how);
	int			(*setsockopt)(struct sock *sk, int level, 
					int optname, char *optval, int optlen);
	int			(*getsockopt)(struct sock *sk, int level, 
					int optname, char *optval, 
					int *option);  	 
	int			(*sendmsg)(struct sock *sk, struct msghdr *msg,
					   int len);
	int			(*recvmsg)(struct sock *sk, struct msghdr *msg,
					int len, int noblock, int flags, 
					int *addr_len);
	int			(*bind)(struct sock *sk, 
					struct sockaddr *uaddr, int addr_len);

	int			(*backlog_rcv) (struct sock *sk, 
						struct sk_buff *skb);

	/* Keeping track of sk's, looking them up, and port selection methods. */
	void			(*hash)(struct sock *sk);
	void			(*unhash)(struct sock *sk);
	void			(*rehash)(struct sock *sk);
	unsigned short		(*good_socknum)(void);
	int			(*verify_bind)(struct sock *sk, unsigned short snum);

	unsigned short		max_header;
	unsigned long		retransmits;
	char			name[32];
	int			inuse, highestinuse;
};

#define TIME_WRITE	1	/* Not yet used */
#define TIME_RETRANS	2	/* Retransmit timer */
#define TIME_DACK	3	/* Delayed ack timer */
#define TIME_CLOSE	4
#define TIME_KEEPOPEN	5
#define TIME_DESTROY	6
#define TIME_DONE	7	/* Used to absorb those last few packets */
#define TIME_PROBE0	8

/*
 *	About 10 seconds 
 */

#define SOCK_DESTROY_TIME (10*HZ)

/*
 *	Sockets 0-1023 can't be bound to unless you are superuser 
 */
 
#define PROT_SOCK	1024

#define SHUTDOWN_MASK	3
#define RCV_SHUTDOWN	1
#define SEND_SHUTDOWN	2

/* Per-protocol hash table implementations use this to make sure
 * nothing changes.
 */
#define SOCKHASH_LOCK()		start_bh_atomic()
#define SOCKHASH_UNLOCK()	end_bh_atomic()

/* Some things in the kernel just want to get at a protocols
 * entire socket list commensurate, thus...
 */
static __inline__ void add_to_prot_sklist(struct sock *sk)
{
	SOCKHASH_LOCK();
	if(!sk->sklist_next) {
		struct proto *p = sk->prot;

		sk->sklist_prev = (struct sock *) p;
		sk->sklist_next = p->sklist_next;
		p->sklist_next->sklist_prev = sk;
		p->sklist_next = sk;

		/* Charge the protocol. */
		sk->prot->inuse += 1;
		if(sk->prot->highestinuse < sk->prot->inuse)
			sk->prot->highestinuse = sk->prot->inuse;
	}
	SOCKHASH_UNLOCK();
}

static __inline__ void del_from_prot_sklist(struct sock *sk)
{
	SOCKHASH_LOCK();
	if(sk->sklist_next) {
		sk->sklist_next->sklist_prev = sk->sklist_prev;
		sk->sklist_prev->sklist_next = sk->sklist_next;
		sk->sklist_next = NULL;
		sk->prot->inuse--;
	}
	SOCKHASH_UNLOCK();
}

/*
 * Used by processes to "lock" a socket state, so that
 * interrupts and bottom half handlers won't change it
 * from under us. It essentially blocks any incoming
 * packets, so that we won't get any new data or any
 * packets that change the state of the socket.
 *
 * Note the 'barrier()' calls: gcc may not move a lock
 * "downwards" or a unlock "upwards" when optimizing.
 */
extern void __release_sock(struct sock *sk);

static inline void lock_sock(struct sock *sk)
{
#if 0
/* debugging code: the test isn't even 100% correct, but it can catch bugs */
/* Note that a double lock is ok in theory - it's just _usually_ a bug */
	if (sk->users) {
		__label__ here;
		printk("double lock on socket at %p\n", &&here);
here:
	}
#endif
	sk->users++;
	barrier();
}

static inline void release_sock(struct sock *sk)
{
	barrier();
#if 0
/* debugging code: remove me when ok */
	if (sk->users == 0) {
		__label__ here;
		sk->users = 1;
		printk("trying to unlock unlocked socket at %p\n", &&here);
here:
	}
#endif
	if ((sk->users = sk->users-1) == 0)
		__release_sock(sk);
}

/*
 *	This might not be the most apropriate place for this two	 
 *	but since they are used by a lot of the net related code
 *	at least they get declared on a include that is common to all
 */

static __inline__ int min(unsigned int a, unsigned int b)
{
	if (a > b)
		a = b; 
	return a;
}

static __inline__ int max(unsigned int a, unsigned int b)
{
	if (a < b)
		a = b;
	return a;
}

extern struct sock *		sk_alloc(int priority);
extern void			sk_free(struct sock *sk);
extern void			destroy_sock(struct sock *sk);

extern struct sk_buff		*sock_wmalloc(struct sock *sk,
					      unsigned long size, int force,
					      int priority);
extern struct sk_buff		*sock_rmalloc(struct sock *sk,
					      unsigned long size, int force,
					      int priority);
extern void			sock_wfree(struct sk_buff *skb);
extern void			sock_rfree(struct sk_buff *skb);
extern unsigned long		sock_rspace(struct sock *sk);
extern unsigned long		sock_wspace(struct sock *sk);

extern int			sock_setsockopt(struct socket *sock, int level,
						int op, char *optval,
						int optlen);

extern int			sock_getsockopt(struct socket *sock, int level,
						int op, char *optval, 
						int *optlen);
extern struct sk_buff 		*sock_alloc_send_skb(struct sock *sk,
						     unsigned long size,
						     unsigned long fallback,
						     int noblock,
						     int *errcode);
extern int 			sock_no_fcntl(struct socket *, unsigned int, unsigned long);
extern int			sock_no_getsockopt(struct socket *, int , int,
						   char *, int *);
extern int			sock_no_setsockopt(struct socket *, int, int,
						   char *, int);
extern int			sock_no_listen(struct socket *, int);
/*
 *	Default socket callbacks and setup code
 */
 
extern void sock_def_callback1(struct sock *);
extern void sock_def_callback2(struct sock *, int);
extern void sock_def_callback3(struct sock *);

/* Initialise core socket variables */
extern void sock_init_data(struct socket *sock, struct sock *sk);

extern void sklist_remove_socket(struct sock **list, struct sock *sk);
extern void sklist_insert_socket(struct sock **list, struct sock *sk);
extern void sklist_destroy_socket(struct sock **list, struct sock *sk);

/*
 * 	Queue a received datagram if it will fit. Stream and sequenced
 *	protocols can't normally use this as they need to fit buffers in
 *	and play with them.
 *
 * 	Inlined as it's very short and called for pretty much every
 *	packet ever received.
 */

extern __inline__ void skb_set_owner_w(struct sk_buff *skb, struct sock *sk)
{
	skb->sk = sk;
	skb->destructor = sock_wfree;
	atomic_add(skb->truesize, &sk->wmem_alloc);
}

extern __inline__ void skb_set_owner_r(struct sk_buff *skb, struct sock *sk)
{
	skb->sk = sk;
	skb->destructor = sock_rfree;
	atomic_add(skb->truesize, &sk->rmem_alloc);
}


extern __inline__ int sock_queue_rcv_skb(struct sock *sk, struct sk_buff *skb)
{
	if (sk->rmem_alloc + skb->truesize >= sk->rcvbuf)
		return -ENOMEM;
	skb_set_owner_r(skb, sk);
	skb_queue_tail(&sk->receive_queue,skb);
	if (!sk->dead)
		sk->data_ready(sk,skb->len);
	return 0;
}

extern __inline__ int __sock_queue_rcv_skb(struct sock *sk, struct sk_buff *skb)
{
	if (sk->rmem_alloc + skb->truesize >= sk->rcvbuf)
		return -ENOMEM;
	skb_set_owner_r(skb, sk);
	__skb_queue_tail(&sk->receive_queue,skb);
	if (!sk->dead)
		sk->data_ready(sk,skb->len);
	return 0;
}

extern __inline__ int sock_queue_err_skb(struct sock *sk, struct sk_buff *skb)
{
	if (sk->rmem_alloc + skb->truesize >= sk->rcvbuf)
		return -ENOMEM;
	skb_set_owner_r(skb, sk);
	__skb_queue_tail(&sk->error_queue,skb);
	if (!sk->dead)
		sk->data_ready(sk,skb->len);
	return 0;
}

/*
 *	Recover an error report and clear atomically
 */
 
extern __inline__ int sock_error(struct sock *sk)
{
	int err=xchg(&sk->err,0);
	return -err;
}


/* 
 *	Declarations from timer.c 
 */
 
extern struct sock *timer_base;

extern void net_delete_timer (struct sock *);
extern void net_reset_timer (struct sock *, int, unsigned long);
extern void net_timer (unsigned long);


/* 
 *	Enable debug/info messages 
 */

#if 0
#define NETDEBUG(x)	do { } while (0)
#else
#define NETDEBUG(x)	do { x; } while (0)
#endif

#endif	/* _SOCK_H */
