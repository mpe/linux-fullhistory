/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the TCP module.
 *
 * Version:	@(#)tcp.h	1.0.5	05/23/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _TCP_H
#define _TCP_H

#define TCP_DEBUG 1
#undef  TCP_FORMAL_WINDOW
#define TCP_MORE_COARSE_ACKS
#undef  TCP_LESS_COARSE_ACKS

#include <linux/config.h>
#include <linux/tcp.h>
#include <linux/slab.h>
#include <net/checksum.h>
#include <net/sock.h>

/* This is for all connections with a full identity, no wildcards.
 * New scheme, half the table is for TIME_WAIT, the other half is
 * for the rest.  I'll experiment with dynamic table growth later.
 */
struct tcp_ehash_bucket {
	rwlock_t	lock;
	struct sock	*chain;
} __attribute__((__aligned__(8)));

extern int tcp_ehash_size;
extern struct tcp_ehash_bucket *tcp_ehash;

/* This is for listening sockets, thus all sockets which possess wildcards. */
#define TCP_LHTABLE_SIZE	32	/* Yes, really, this is all you need. */

/* tcp_ipv4.c: These need to be shared by v4 and v6 because the lookup
 *             and hashing code needs to work with different AF's yet
 *             the port space is shared.
 */
extern struct sock *tcp_listening_hash[TCP_LHTABLE_SIZE];
extern rwlock_t tcp_lhash_lock;
extern atomic_t tcp_lhash_users;
extern wait_queue_head_t tcp_lhash_wait;

/* There are a few simple rules, which allow for local port reuse by
 * an application.  In essence:
 *
 *	1) Sockets bound to different interfaces may share a local port.
 *	   Failing that, goto test 2.
 *	2) If all sockets have sk->reuse set, and none of them are in
 *	   TCP_LISTEN state, the port may be shared.
 *	   Failing that, goto test 3.
 *	3) If all sockets are bound to a specific sk->rcv_saddr local
 *	   address, and none of them are the same, the port may be
 *	   shared.
 *	   Failing this, the port cannot be shared.
 *
 * The interesting point, is test #2.  This is what an FTP server does
 * all day.  To optimize this case we use a specific flag bit defined
 * below.  As we add sockets to a bind bucket list, we perform a
 * check of: (newsk->reuse && (newsk->state != TCP_LISTEN))
 * As long as all sockets added to a bind bucket pass this test,
 * the flag bit will be set.
 * The resulting situation is that tcp_v[46]_verify_bind() can just check
 * for this flag bit, if it is set and the socket trying to bind has
 * sk->reuse set, we don't even have to walk the owners list at all,
 * we return that it is ok to bind this socket to the requested local port.
 *
 * Sounds like a lot of work, but it is worth it.  In a more naive
 * implementation (ie. current FreeBSD etc.) the entire list of ports
 * must be walked for each data port opened by an ftp server.  Needless
 * to say, this does not scale at all.  With a couple thousand FTP
 * users logged onto your box, isn't it nice to know that new data
 * ports are created in O(1) time?  I thought so. ;-)	-DaveM
 */
struct tcp_bind_bucket {
	unsigned short		port;
	unsigned short		fastreuse;
	struct tcp_bind_bucket	*next;
	struct sock		*owners;
	struct tcp_bind_bucket	**pprev;
};

struct tcp_bind_hashbucket {
	spinlock_t		lock;
	struct tcp_bind_bucket	*chain;
};

extern struct tcp_bind_hashbucket *tcp_bhash;
extern int tcp_bhash_size;
extern spinlock_t tcp_portalloc_lock;

extern kmem_cache_t *tcp_bucket_cachep;
extern struct tcp_bind_bucket *tcp_bucket_create(struct tcp_bind_hashbucket *head,
						 unsigned short snum);
extern void tcp_bucket_unlock(struct sock *sk);
extern int tcp_port_rover;
extern struct sock *tcp_v4_lookup_listener(u32 addr, unsigned short hnum, int dif);

/* These are AF independent. */
static __inline__ int tcp_bhashfn(__u16 lport)
{
	return (lport & (tcp_bhash_size - 1));
}

/* This is a TIME_WAIT bucket.  It works around the memory consumption
 * problems of sockets in such a state on heavily loaded servers, but
 * without violating the protocol specification.
 */
struct tcp_tw_bucket {
	/* These _must_ match the beginning of struct sock precisely.
	 * XXX Yes I know this is gross, but I'd have to edit every single
	 * XXX networking file if I created a "struct sock_header". -DaveM
	 */
	__u32			daddr;
	__u32			rcv_saddr;
	__u16			dport;
	unsigned short		num;
	int			bound_dev_if;
	struct sock		*next;
	struct sock		**pprev;
	struct sock		*bind_next;
	struct sock		**bind_pprev;
	unsigned char		state,
				substate; /* "zapped" is replaced with "substate" */
	__u16			sport;
	unsigned short		family;
	unsigned char		reuse,
				rcv_wscale; /* It is also TW bucket specific */
	atomic_t		refcnt;

	/* And these are ours. */
	int			hashent;
	int			timeout;
	__u32			rcv_nxt;
	__u32			snd_nxt;
	__u32			rcv_wnd;
	__u32			syn_seq;
        __u32			ts_recent;
        long			ts_recent_stamp;
	unsigned long		ttd;
	struct tcp_bind_bucket	*tb;
	struct tcp_tw_bucket	*next_death;
	struct tcp_tw_bucket	**pprev_death;

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
	struct in6_addr		v6_daddr;
	struct in6_addr		v6_rcv_saddr;
#endif
};

extern kmem_cache_t *tcp_timewait_cachep;

extern __inline__ void tcp_tw_put(struct tcp_tw_bucket *tw)
{
	if (atomic_dec_and_test(&tw->refcnt)) {
#ifdef INET_REFCNT_DEBUG
		printk(KERN_DEBUG "tw_bucket %p released\n", tw);
#endif
		kmem_cache_free(tcp_timewait_cachep, tw);
	}
}

extern atomic_t tcp_orphan_count;
extern int  tcp_tw_count;
extern void tcp_time_wait(struct sock *sk, int state, int timeo);
extern void tcp_timewait_kill(struct tcp_tw_bucket *tw);
extern void tcp_tw_schedule(struct tcp_tw_bucket *tw, int timeo);
extern void tcp_tw_deschedule(struct tcp_tw_bucket *tw);


/* Socket demux engine toys. */
#ifdef __BIG_ENDIAN
#define TCP_COMBINED_PORTS(__sport, __dport) \
	(((__u32)(__sport)<<16) | (__u32)(__dport))
#else /* __LITTLE_ENDIAN */
#define TCP_COMBINED_PORTS(__sport, __dport) \
	(((__u32)(__dport)<<16) | (__u32)(__sport))
#endif

#if (BITS_PER_LONG == 64)
#ifdef __BIG_ENDIAN
#define TCP_V4_ADDR_COOKIE(__name, __saddr, __daddr) \
	__u64 __name = (((__u64)(__saddr))<<32)|((__u64)(__daddr));
#else /* __LITTLE_ENDIAN */
#define TCP_V4_ADDR_COOKIE(__name, __saddr, __daddr) \
	__u64 __name = (((__u64)(__daddr))<<32)|((__u64)(__saddr));
#endif /* __BIG_ENDIAN */
#define TCP_IPV4_MATCH(__sk, __cookie, __saddr, __daddr, __ports, __dif)\
	(((*((__u64 *)&((__sk)->daddr)))== (__cookie))	&&		\
	 ((*((__u32 *)&((__sk)->dport)))== (__ports))   &&		\
	 (!((__sk)->bound_dev_if) || ((__sk)->bound_dev_if == (__dif))))
#else /* 32-bit arch */
#define TCP_V4_ADDR_COOKIE(__name, __saddr, __daddr)
#define TCP_IPV4_MATCH(__sk, __cookie, __saddr, __daddr, __ports, __dif)\
	(((__sk)->daddr			== (__saddr))	&&		\
	 ((__sk)->rcv_saddr		== (__daddr))	&&		\
	 ((*((__u32 *)&((__sk)->dport)))== (__ports))   &&		\
	 (!((__sk)->bound_dev_if) || ((__sk)->bound_dev_if == (__dif))))
#endif /* 64-bit arch */

#define TCP_IPV6_MATCH(__sk, __saddr, __daddr, __ports, __dif)			   \
	(((*((__u32 *)&((__sk)->dport)))== (__ports))   			&& \
	 ((__sk)->family		== AF_INET6)				&& \
	 !ipv6_addr_cmp(&(__sk)->net_pinfo.af_inet6.daddr, (__saddr))		&& \
	 !ipv6_addr_cmp(&(__sk)->net_pinfo.af_inet6.rcv_saddr, (__daddr))	&& \
	 (!((__sk)->bound_dev_if) || ((__sk)->bound_dev_if == (__dif))))

/* These can have wildcards, don't try too hard. */
static __inline__ int tcp_lhashfn(unsigned short num)
{
	return num & (TCP_LHTABLE_SIZE - 1);
}

static __inline__ int tcp_sk_listen_hashfn(struct sock *sk)
{
	return tcp_lhashfn(sk->num);
}

#define MAX_TCP_HEADER	(128 + MAX_HEADER)

/* 
 * Never offer a window over 32767 without using window scaling. Some
 * poor stacks do signed 16bit maths! 
 */
#define MAX_TCP_WINDOW		32767

/* Minimal accepted MSS. It is (60+60+8) - (20+20). */
#define TCP_MIN_MSS		88

/* Minimal RCV_MSS. */
#define TCP_MIN_RCVMSS		536

/* 
 * How much of the receive buffer do we advertize 
 * (the rest is reserved for headers and driver packet overhead)
 * Use a power of 2.
 */
#define TCP_WINDOW_ADVERTISE_DIVISOR 2

/* urg_data states */
#define TCP_URG_VALID	0x0100
#define TCP_URG_NOTYET	0x0200
#define TCP_URG_READ	0x0400

#define TCP_RETR1	3	/*
				 * This is how many retries it does before it
				 * tries to figure out if the gateway is
				 * down. Minimal RFC value is 3; it corresponds
				 * to ~3sec-8min depending on RTO.
				 */

#define TCP_RETR2	15	/*
				 * This should take at least
				 * 90 minutes to time out.
				 * RFC1122 says that the limit is 100 sec.
				 * 15 is ~13-30min depending on RTO.
				 */

#define TCP_SYN_RETRIES	 5	/* number of times to retry active opening a
				 * connection: ~180sec is RFC minumum	*/

#define TCP_SYNACK_RETRIES 5	/* number of times to retry passive opening a
				 * connection: ~180sec is RFC minumum	*/


#define TCP_ORPHAN_RETRIES 7	/* number of times to retry on an orphaned
				 * socket. 7 is ~50sec-16min.
				 */


#define TCP_TIMEWAIT_LEN (60*HZ) /* how long to wait to destroy TIME-WAIT
				  * state, about 60 seconds	*/
#define TCP_FIN_TIMEOUT	TCP_TIMEWAIT_LEN
                                 /* BSD style FIN_WAIT2 deadlock breaker.
				  * It used to be 3min, new value is 60sec,
				  * to combine FIN-WAIT-2 timeout with
				  * TIME-WAIT timer.
				  */

#define TCP_DELACK_MAX	(HZ/5)	/* maximal time to delay before sending an ACK */
#define TCP_DELACK_MIN	(2)	/* minimal time to delay before sending an ACK,
				 * 2 scheduler ticks, not depending on HZ. */
#define TCP_ATO_MAX	(HZ/2)	/* Clamp ATO estimator at his value. */
#define TCP_ATO_MIN	2
#define TCP_RTO_MAX	(120*HZ)
#define TCP_RTO_MIN	(HZ/5)
#define TCP_TIMEOUT_INIT (3*HZ)	/* RFC 1122 initial RTO value	*/

#define TCP_RESOURCE_PROBE_INTERVAL (HZ/2) /* Maximal interval between probes
					    * for local resources.
					    */

#define TCP_KEEPALIVE_TIME	(120*60*HZ)	/* two hours */
#define TCP_KEEPALIVE_PROBES	9		/* Max of 9 keepalive probes	*/
#define TCP_KEEPALIVE_INTVL	(75*HZ)

#define MAX_TCP_KEEPIDLE	32767
#define MAX_TCP_KEEPINTVL	32767
#define MAX_TCP_KEEPCNT		127
#define MAX_TCP_SYNCNT		127

/* TIME_WAIT reaping mechanism. */
#define TCP_TWKILL_SLOTS	8	/* Please keep this a power of 2. */
#define TCP_TWKILL_PERIOD	(TCP_TIMEWAIT_LEN/TCP_TWKILL_SLOTS)

#define TCP_SYNQ_INTERVAL	(HZ/5)	/* Period of SYNACK timer */
#define TCP_SYNQ_HSIZE		64	/* Size of SYNACK hash table */

#define TCP_PAWS_24DAYS	(60 * 60 * 24 * 24)
#define TCP_PAWS_MSL	60		/* Per-host timestamps are invalidated
					 * after this time. It should be equal
					 * (or greater than) TCP_TIMEWAIT_LEN
					 * to provide reliability equal to one
					 * provided by timewait state.
					 */
#define TCP_PAWS_WINDOW	1		/* Replay window for per-host
					 * timestamps. It must be less than
					 * minimal timewait lifetime.
					 */

#define TCP_TW_RECYCLE_SLOTS_LOG	5
#define TCP_TW_RECYCLE_SLOTS		(1<<TCP_TW_RECYCLE_SLOTS_LOG)

/* If time > 4sec, it is "slow" path, no recycling is required,
   so that we select tick to get range about 4 seconds.
 */

#if HZ == 20
# define TCP_TW_RECYCLE_TICK (5+2-TCP_TW_RECYCLE_SLOTS_LOG)
#elif HZ == 64
# define TCP_TW_RECYCLE_TICK (6+2-TCP_TW_RECYCLE_SLOTS_LOG)
#elif HZ == 100 || HZ == 128
# define TCP_TW_RECYCLE_TICK (7+2-TCP_TW_RECYCLE_SLOTS_LOG)
#elif HZ == 1024 || HZ == 1000
# define TCP_TW_RECYCLE_TICK (10+2-TCP_TW_RECYCLE_SLOTS_LOG)
#else
# error HZ != 20 && HZ != 64 && HZ != 100 && HZ != 1000 && HZ != 1024
#endif

/*
 *	TCP option
 */
 
#define TCPOPT_NOP		1	/* Padding */
#define TCPOPT_EOL		0	/* End of options */
#define TCPOPT_MSS		2	/* Segment size negotiating */
#define TCPOPT_WINDOW		3	/* Window scaling */
#define TCPOPT_SACK_PERM        4       /* SACK Permitted */
#define TCPOPT_SACK             5       /* SACK Block */
#define TCPOPT_TIMESTAMP	8	/* Better RTT estimations/PAWS */

/*
 *     TCP option lengths
 */

#define TCPOLEN_MSS            4
#define TCPOLEN_WINDOW         3
#define TCPOLEN_SACK_PERM      2
#define TCPOLEN_TIMESTAMP      10

/* But this is what stacks really send out. */
#define TCPOLEN_TSTAMP_ALIGNED		12
#define TCPOLEN_WSCALE_ALIGNED		4
#define TCPOLEN_SACKPERM_ALIGNED	4
#define TCPOLEN_SACK_BASE		2
#define TCPOLEN_SACK_BASE_ALIGNED	4
#define TCPOLEN_SACK_PERBLOCK		8

#define TCP_TIME_RETRANS	1	/* Retransmit timer */
#define TCP_TIME_DACK		2	/* Delayed ack timer */
#define TCP_TIME_PROBE0		3	/* Zero window probe timer */
#define TCP_TIME_KEEPOPEN	4	/* Keepalive timer */

/* sysctl variables for tcp */
extern int sysctl_max_syn_backlog;
extern int sysctl_tcp_timestamps;
extern int sysctl_tcp_window_scaling;
extern int sysctl_tcp_sack;
extern int sysctl_tcp_fin_timeout;
extern int sysctl_tcp_tw_recycle;
extern int sysctl_tcp_keepalive_time;
extern int sysctl_tcp_keepalive_probes;
extern int sysctl_tcp_keepalive_intvl;
extern int sysctl_tcp_syn_retries;
extern int sysctl_tcp_synack_retries;
extern int sysctl_tcp_retries1;
extern int sysctl_tcp_retries2;
extern int sysctl_tcp_orphan_retries;
extern int sysctl_tcp_syncookies;
extern int sysctl_tcp_retrans_collapse;
extern int sysctl_tcp_stdurg;
extern int sysctl_tcp_rfc1337;
extern int sysctl_tcp_tw_recycle;
extern int sysctl_tcp_abort_on_overflow;
extern int sysctl_tcp_max_orphans;
extern int sysctl_tcp_max_tw_buckets;

struct open_request;

struct or_calltable {
	int  family;
	int  (*rtx_syn_ack)	(struct sock *sk, struct open_request *req, struct dst_entry*);
	void (*send_ack)	(struct sk_buff *skb, struct open_request *req);
	void (*destructor)	(struct open_request *req);
	void (*send_reset)	(struct sk_buff *skb);
};

struct tcp_v4_open_req {
	__u32			loc_addr;
	__u32			rmt_addr;
	struct ip_options	*opt;
};

#if defined(CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
struct tcp_v6_open_req {
	struct in6_addr		loc_addr;
	struct in6_addr		rmt_addr;
	struct sk_buff		*pktopts;
	int			iif;
};
#endif

/* this structure is too big */
struct open_request {
	struct open_request	*dl_next; /* Must be first member! */
	__u32			rcv_isn;
	__u32			snt_isn;
	__u16			rmt_port;
	__u16			mss;
	__u8			retrans;
	__u8			index;
	__u16	snd_wscale : 4, 
		rcv_wscale : 4, 
		tstamp_ok : 1,
		sack_ok : 1,
		wscale_ok : 1,
		ecn_ok : 1,
		acked : 1;
	/* The following two fields can be easily recomputed I think -AK */
	__u32			window_clamp;	/* window clamp at creation time */
	__u32			rcv_wnd;	/* rcv_wnd offered first time */
	__u32			ts_recent;
	unsigned long		expires;
	struct or_calltable	*class;
	struct sock		*sk;
	union {
		struct tcp_v4_open_req v4_req;
#if defined(CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
		struct tcp_v6_open_req v6_req;
#endif
	} af;
};

/* SLAB cache for open requests. */
extern kmem_cache_t *tcp_openreq_cachep;

#define tcp_openreq_alloc()		kmem_cache_alloc(tcp_openreq_cachep, SLAB_ATOMIC)
#define tcp_openreq_fastfree(req)	kmem_cache_free(tcp_openreq_cachep, req)

extern __inline__ void tcp_openreq_free(struct open_request *req)
{
	req->class->destructor(req);
	tcp_openreq_fastfree(req);
}

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
#define TCP_INET_FAMILY(fam) ((fam) == AF_INET)
#else
#define TCP_INET_FAMILY(fam) 1
#endif

/*
 *	Pointers to address related TCP functions
 *	(i.e. things that depend on the address family)
 *
 * 	BUGGG_FUTURE: all the idea behind this struct is wrong.
 *	It mixes socket frontend with transport function.
 *	With port sharing between IPv6/v4 it gives the only advantage,
 *	only poor IPv6 needs to permanently recheck, that it
 *	is still IPv6 8)8) It must be cleaned up as soon as possible.
 *						--ANK (980802)
 */

struct tcp_func {
	int			(*queue_xmit)		(struct sk_buff *skb);

	void			(*send_check)		(struct sock *sk,
							 struct tcphdr *th,
							 int len,
							 struct sk_buff *skb);

	int			(*rebuild_header)	(struct sock *sk);

	int			(*conn_request)		(struct sock *sk,
							 struct sk_buff *skb);

	struct sock *		(*syn_recv_sock)	(struct sock *sk,
							 struct sk_buff *skb,
							 struct open_request *req,
							 struct dst_entry *dst);
	
	int			(*hash_connecting)	(struct sock *sk);

	int			(*remember_stamp)	(struct sock *sk);

	__u16			net_header_len;

	int			(*setsockopt)		(struct sock *sk, 
							 int level, 
							 int optname, 
							 char *optval, 
							 int optlen);

	int			(*getsockopt)		(struct sock *sk, 
							 int level, 
							 int optname, 
							 char *optval, 
							 int *optlen);


	void			(*addr2sockaddr)	(struct sock *sk,
							 struct sockaddr *);

	int sockaddr_len;
};

/*
 * The next routines deal with comparing 32 bit unsigned ints
 * and worry about wraparound (automatic with unsigned arithmetic).
 */

extern __inline int before(__u32 seq1, __u32 seq2)
{
        return (__s32)(seq1-seq2) < 0;
}

extern __inline int after(__u32 seq1, __u32 seq2)
{
	return (__s32)(seq2-seq1) < 0;
}


/* is s2<=s1<=s3 ? */
extern __inline int between(__u32 seq1, __u32 seq2, __u32 seq3)
{
	return seq3 - seq2 >= seq1 - seq2;
}


extern struct proto tcp_prot;

extern struct tcp_mib tcp_statistics[NR_CPUS*2];
#define TCP_INC_STATS(field)		SNMP_INC_STATS(tcp_statistics, field)
#define TCP_INC_STATS_BH(field)		SNMP_INC_STATS_BH(tcp_statistics, field)
#define TCP_INC_STATS_USER(field) 	SNMP_INC_STATS_USER(tcp_statistics, field)

extern void			tcp_put_port(struct sock *sk);
extern void			__tcp_put_port(struct sock *sk);
extern void			tcp_inherit_port(struct sock *sk, struct sock *child);

extern void			tcp_v4_err(struct sk_buff *skb,
					   unsigned char *, int);

extern void			tcp_shutdown (struct sock *sk, int how);

extern int			tcp_v4_rcv(struct sk_buff *skb,
					   unsigned short len);

extern int			tcp_v4_remember_stamp(struct sock *sk);

extern int		    	tcp_v4_tw_remember_stamp(struct tcp_tw_bucket *tw);

extern int			tcp_sendmsg(struct sock *sk, struct msghdr *msg, int size);

extern int			tcp_ioctl(struct sock *sk, 
					  int cmd, 
					  unsigned long arg);

extern int			tcp_rcv_state_process(struct sock *sk, 
						      struct sk_buff *skb,
						      struct tcphdr *th,
						      unsigned len);

extern int			tcp_rcv_established(struct sock *sk, 
						    struct sk_buff *skb,
						    struct tcphdr *th, 
						    unsigned len);

static __inline__ void tcp_dec_quickack_mode(struct tcp_opt *tp)
{
	if (tp->ack.quick && --tp->ack.quick == 0) {
		/* Leaving quickack mode we deflate ATO. */
		tp->ack.ato = TCP_ATO_MIN;
	}
}

static __inline__ void tcp_delack_init(struct tcp_opt *tp)
{
	memset(&tp->ack, 0, sizeof(tp->ack));
}


enum tcp_tw_status
{
	TCP_TW_SUCCESS = 0,
	TCP_TW_RST = 1,
	TCP_TW_ACK = 2,
	TCP_TW_SYN = 3
};


extern enum tcp_tw_status	tcp_timewait_state_process(struct tcp_tw_bucket *tw,
							   struct sk_buff *skb,
							   struct tcphdr *th,
							   unsigned len);

extern struct sock *		tcp_check_req(struct sock *sk,struct sk_buff *skb,
					      struct open_request *req,
					      struct open_request **prev);
extern int			tcp_child_process(struct sock *parent,
						  struct sock *child,
						  struct sk_buff *skb);

extern void			tcp_close(struct sock *sk, 
					  long timeout);
extern struct sock *		tcp_accept(struct sock *sk, int flags, int *err);
extern unsigned int		tcp_poll(struct file * file, struct socket *sock, struct poll_table_struct *wait);
extern void			tcp_write_space(struct sock *sk); 

extern int			tcp_getsockopt(struct sock *sk, int level, 
					       int optname, char *optval, 
					       int *optlen);
extern int			tcp_setsockopt(struct sock *sk, int level, 
					       int optname, char *optval, 
					       int optlen);
extern void			tcp_set_keepalive(struct sock *sk, int val);
extern int			tcp_recvmsg(struct sock *sk, 
					    struct msghdr *msg,
					    int len, int nonblock, 
					    int flags, int *addr_len);

extern int			tcp_listen_start(struct sock *sk);

extern void			tcp_parse_options(struct sock *sk, struct tcphdr *th,
						  struct tcp_opt *tp, int no_fancy);

/*
 *	TCP v4 functions exported for the inet6 API
 */

extern int		       	tcp_v4_rebuild_header(struct sock *sk);

extern int		       	tcp_v4_build_header(struct sock *sk, 
						    struct sk_buff *skb);

extern void		       	tcp_v4_send_check(struct sock *sk, 
						  struct tcphdr *th, int len, 
						  struct sk_buff *skb);

extern void			tcp_v4_send_reset(struct sk_buff *skb);

extern int			tcp_v4_conn_request(struct sock *sk,
						    struct sk_buff *skb);

extern struct sock *		tcp_create_openreq_child(struct sock *sk,
							 struct open_request *req,
							 struct sk_buff *skb);

extern struct sock *		tcp_v4_syn_recv_sock(struct sock *sk,
						     struct sk_buff *skb,
						     struct open_request *req,
							struct dst_entry *dst);

extern int			tcp_v4_do_rcv(struct sock *sk,
					      struct sk_buff *skb);

extern int			tcp_v4_connect(struct sock *sk,
					       struct sockaddr *uaddr,
					       int addr_len);

extern int			tcp_connect(struct sock *sk,
					    struct sk_buff *skb);

extern struct sk_buff *		tcp_make_synack(struct sock *sk,
						struct dst_entry *dst,
						struct open_request *req);

extern int			tcp_disconnect(struct sock *sk, int flags);

extern void			tcp_unhash(struct sock *sk);

extern int			tcp_v4_hash_connecting(struct sock *sk);


/* From syncookies.c */
extern struct sock *cookie_v4_check(struct sock *sk, struct sk_buff *skb, 
				    struct ip_options *opt);
extern __u32 cookie_v4_init_sequence(struct sock *sk, struct sk_buff *skb, 
				     __u16 *mss);

/* tcp_output.c */

extern int tcp_write_xmit(struct sock *);
extern int tcp_retransmit_skb(struct sock *, struct sk_buff *);
extern void tcp_fack_retransmit(struct sock *);
extern void tcp_xmit_retransmit_queue(struct sock *);
extern void tcp_simple_retransmit(struct sock *);

extern void tcp_send_probe0(struct sock *);
extern void tcp_send_partial(struct sock *);
extern int  tcp_write_wakeup(struct sock *);
extern void tcp_send_fin(struct sock *sk);
extern void tcp_send_active_reset(struct sock *sk, int priority);
extern int  tcp_send_synack(struct sock *);
extern int  tcp_transmit_skb(struct sock *, struct sk_buff *);
extern void tcp_send_skb(struct sock *, struct sk_buff *, int force_queue, unsigned mss_now);
extern void tcp_send_ack(struct sock *sk);
extern void tcp_send_delayed_ack(struct sock *sk);

/* tcp_timer.c */
extern void tcp_reset_xmit_timer(struct sock *, int, unsigned long);
extern void tcp_init_xmit_timers(struct sock *);
extern void tcp_clear_xmit_timers(struct sock *);

extern void tcp_delete_keepalive_timer (struct sock *);
extern void tcp_reset_keepalive_timer (struct sock *, unsigned long);
extern int tcp_sync_mss(struct sock *sk, u32 pmtu);

/* Compute the current effective MSS, taking SACKs and IP options,
 * and even PMTU discovery events into account.
 */

static __inline__ unsigned int tcp_current_mss(struct sock *sk)
{
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;
	struct dst_entry *dst = __sk_dst_get(sk);
	int mss_now = tp->mss_cache; 

	if (dst && dst->pmtu != tp->pmtu_cookie)
		mss_now = tcp_sync_mss(sk, dst->pmtu);

	if(tp->sack_ok && tp->num_sacks)
		mss_now -= (TCPOLEN_SACK_BASE_ALIGNED +
			    (tp->num_sacks * TCPOLEN_SACK_PERBLOCK));
	return mss_now;
}

/* Initialize RCV_MSS value.
 * RCV_MSS is an our guess about MSS used by the peer.
 * We haven't any direct information about the MSS.
 * It's better to underestimate the RCV_MSS rather than overestimate.
 * Overestimations make us ACKing less frequently than needed.
 * Underestimations are more easy to detect and fix by tcp_measure_rcv_mss().
 */

extern __inline__ void tcp_initialize_rcv_mss(struct sock *sk)
{
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;
	struct dst_entry *dst = __sk_dst_get(sk);
	int mss;

	if (dst)
		mss = dst->advmss;
	else
		mss = tp->mss_cache;

	tp->ack.rcv_mss = max(min(mss, TCP_MIN_RCVMSS), TCP_MIN_MSS);
}

static __inline__ void __tcp_fast_path_on(struct tcp_opt *tp, u32 snd_wnd)
{
	tp->pred_flags = htonl((tp->tcp_header_len << 26) |
			       ntohl(TCP_FLAG_ACK) |
			       snd_wnd);
}

static __inline__ void tcp_fast_path_on(struct tcp_opt *tp)
{
	__tcp_fast_path_on(tp, tp->snd_wnd>>tp->snd_wscale);
}




/* Compute the actual receive window we are currently advertising.
 * Rcv_nxt can be after the window if our peer push more data
 * than the offered window.
 */
static __inline__ u32 tcp_receive_window(struct tcp_opt *tp)
{
	s32 win = tp->rcv_wup + tp->rcv_wnd - tp->rcv_nxt;

	if (win < 0)
		win = 0;
	return (u32) win;
}

/* Choose a new window, without checks for shrinking, and without
 * scaling applied to the result.  The caller does these things
 * if necessary.  This is a "raw" window selection.
 */
extern u32	__tcp_select_window(struct sock *sk);

/* Chose a new window to advertise, update state in tcp_opt for the
 * socket, and return result with RFC1323 scaling applied.  The return
 * value can be stuffed directly into th->window for an outgoing
 * frame.
 */
extern __inline__ u16 tcp_select_window(struct sock *sk)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	u32 cur_win = tcp_receive_window(tp);
	u32 new_win = __tcp_select_window(sk);

	/* Never shrink the offered window */
	if(new_win < cur_win) {
		/* Danger Will Robinson!
		 * Don't update rcv_wup/rcv_wnd here or else
		 * we will not be able to advertise a zero
		 * window in time.  --DaveM
		 *
		 * Relax Will Robinson.
		 */
		new_win = cur_win;
	}
	tp->rcv_wnd = new_win;
	tp->rcv_wup = tp->rcv_nxt;

	/* RFC1323 scaling applied */
	new_win >>= tp->rcv_wscale;

#ifdef TCP_FORMAL_WINDOW
	if (new_win == 0) {
		/* If we advertise zero window, disable fast path. */
		tp->pred_flags = 0;
	} else if (cur_win == 0 && tp->pred_flags == 0 &&
		   skb_queue_len(&tp->out_of_order_queue) == 0 &&
		   !tp->urg_data) {
		/* If we open zero window, enable fast path.
		   Without this it will be open by the first data packet,
		   it is too late to merge checksumming to copy.
		 */
		tcp_fast_path_on(tp);
	}
#endif

	return new_win;
}

/* TCP timestamps are only 32-bits, this causes a slight
 * complication on 64-bit systems since we store a snapshot
 * of jiffies in the buffer control blocks below.  We decidely
 * only use of the low 32-bits of jiffies and hide the ugly
 * casts with the following macro.
 */
#define tcp_time_stamp		((__u32)(jiffies))

/* This is what the send packet queueing engine uses to pass
 * TCP per-packet control information to the transmission
 * code.  We also store the host-order sequence numbers in
 * here too.  This is 36 bytes on 32-bit architectures,
 * 40 bytes on 64-bit machines, if this grows please adjust
 * skbuff.h:skbuff->cb[xxx] size appropriately.
 */
struct tcp_skb_cb {
	union {
		struct inet_skb_parm	h4;
#if defined(CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
		struct inet6_skb_parm	h6;
#endif
	} header;	/* For incoming frames		*/
	__u32		seq;		/* Starting sequence number	*/
	__u32		end_seq;	/* SEQ + FIN + SYN + datalen	*/
	__u32		when;		/* used to compute rtt's	*/
	__u8		flags;		/* TCP header flags.		*/

	/* NOTE: These must match up to the flags byte in a
	 *       real TCP header.
	 */
#define TCPCB_FLAG_FIN		0x01
#define TCPCB_FLAG_SYN		0x02
#define TCPCB_FLAG_RST		0x04
#define TCPCB_FLAG_PSH		0x08
#define TCPCB_FLAG_ACK		0x10
#define TCPCB_FLAG_URG		0x20
#define TCPCB_FLAG_ECE		0x40
#define TCPCB_FLAG_CWR		0x80

	__u8		sacked;		/* State flags for SACK/FACK.	*/
#define TCPCB_SACKED_ACKED	0x01	/* SKB ACK'd by a SACK block	*/
#define TCPCB_SACKED_RETRANS	0x02	/* SKB retransmitted		*/

	__u16		urg_ptr;	/* Valid w/URG flags is set.	*/
	__u32		ack_seq;	/* Sequence number ACK'd	*/
};

#define TCP_SKB_CB(__skb)	((struct tcp_skb_cb *)&((__skb)->cb[0]))

/* This determines how many packets are "in the network" to the best
 * of our knowledge.  In many cases it is conservative, but where
 * detailed information is available from the receiver (via SACK
 * blocks etc.) we can make more aggressive calculations.
 *
 * Use this for decisions involving congestion control, use just
 * tp->packets_out to determine if the send queue is empty or not.
 *
 * Read this equation as:
 *
 *	"Packets sent once on transmission queue" MINUS
 *	"Packets acknowledged by FACK information" PLUS
 *	"Packets fast retransmitted"
 */
static __inline__ int tcp_packets_in_flight(struct tcp_opt *tp)
{
	return tp->packets_out - tp->fackets_out + tp->retrans_out;
}

/* Recalculate snd_ssthresh, we want to set it to:
 *
 * 	one half the current congestion window, but no
 *	less than two segments
 *
 * We must take into account the current send window
 * as well, however we keep track of that using different
 * units so a conversion is necessary.  -DaveM
 *
 * RED-PEN.
 *  RFC 2581: "an easy mistake to make is to simply use cwnd,
 *             rather than FlightSize"
 * I see no references to FlightSize here. snd_wnd is not FlightSize,
 * it is also apriory characteristics.
 *
 *   FlightSize = min((snd_nxt-snd_una)/mss, packets_out) ?
 */
extern __inline__ __u32 tcp_recalc_ssthresh(struct tcp_opt *tp)
{
	u32 FlightSize = (tp->snd_nxt - tp->snd_una)/tp->mss_cache;

	FlightSize = min(FlightSize, tcp_packets_in_flight(tp));

	return max(min(FlightSize, tp->snd_cwnd) >> 1, 2);
}

/* Set slow start threshould and cwnd not falling to slow start */
extern __inline__ void __tcp_enter_cong_avoid(struct tcp_opt *tp)
{
	tp->snd_ssthresh = tcp_recalc_ssthresh(tp);
	if (tp->snd_ssthresh > tp->snd_cwnd_clamp)
		tp->snd_ssthresh = tp->snd_cwnd_clamp;
	tp->snd_cwnd = tp->snd_ssthresh;
	tp->snd_cwnd_cnt = 0;
	tp->high_seq = tp->snd_nxt;
}

extern __inline__ void tcp_enter_cong_avoid(struct tcp_opt *tp)
{
	if (!tp->high_seq || after(tp->snd_nxt, tp->high_seq))
		__tcp_enter_cong_avoid(tp);
}


/* Increase initial CWND conservatively, i.e. only if estimated
   RTT is low enough. It is not quite correct, we should use
   POWER i.e. RTT*BANDWIDTH, but we still cannot estimate this.

   Numbers are taken from RFC1414.
 */
static __inline__ __u32 tcp_init_cwnd(struct tcp_opt *tp)
{
	__u32 cwnd;

	if (!tp->srtt || tp->srtt > ((HZ/50)<<3) || tp->mss_cache > 1460)
		cwnd = 2;
	else if (tp->mss_cache > 1095)
		cwnd = 3;
	else
		cwnd = 4;

	return min(cwnd, tp->snd_cwnd_clamp);
}


static __inline__ int tcp_minshall_check(struct tcp_opt *tp)
{
	return after(tp->snd_sml,tp->snd_una) &&
		!after(tp->snd_sml, tp->snd_nxt);
}

static __inline__ void tcp_minshall_update(struct tcp_opt *tp, int mss, int len)
{
	if (len < mss)
		tp->snd_sml = tp->snd_nxt;
}

/* Return 0, if packet can be sent now without violation Nagle's rules:
   1. It is full sized.
   2. Or it contains FIN or URG.
   3. Or TCP_NODELAY was set.
   4. Or TCP_CORK is not set, and all sent packets are ACKed.
      With Minshall's modification: all sent small packets are ACKed.
 */

static __inline__ int tcp_nagle_check(struct tcp_opt *tp, struct sk_buff *skb, unsigned mss_now)
{
	return (skb->len < mss_now &&
		!(TCP_SKB_CB(skb)->flags & (TCPCB_FLAG_URG|TCPCB_FLAG_FIN)) &&
		(tp->nonagle == 2 ||
		 (!tp->nonagle &&
		  tp->packets_out &&
		  tcp_minshall_check(tp))));
}

/* This checks if the data bearing packet SKB (usually tp->send_head)
 * should be put on the wire right now.
 */
static __inline__ int tcp_snd_test(struct tcp_opt *tp, struct sk_buff *skb,
				   unsigned cur_mss, int tail)
{
	/*
	 * Reset CWND after idle period longer RTO to "restart window".
	 * It is "side" effect of the function, which is _not_ good
	 * from viewpoint of clarity. But we have to make it before
	 * checking congestion window below. Alternative is to prepend
	 * all the calls with this test.
	 */
	if (tp->packets_out==0 &&
	    (s32)(tcp_time_stamp - tp->lsndtime) > tp->rto)
		tp->snd_cwnd = min(tp->snd_cwnd, tcp_init_cwnd(tp));

	/*	RFC 1122 - section 4.2.3.4
	 *
	 *	We must queue if
	 *
	 *	a) The right edge of this frame exceeds the window
	 *	b) There are packets in flight and we have a small segment
	 *	   [SWS avoidance and Nagle algorithm]
	 *	   (part of SWS is done on packetization)
	 *	   Minshall version sounds: there are no _small_
	 *	   segments in flight. (tcp_nagle_check)
	 *	c) We are retransmiting [Nagle]
	 *	d) We have too many packets 'in flight'
	 *
	 * 	Don't use the nagle rule for urgent data (or
	 *	for the final FIN -DaveM).
	 *
	 *	Also, Nagle rule does not apply to frames, which
	 *	sit in the middle of queue (they have no chances
	 *	to get new data) and if room at tail of skb is
	 *	not enough to save something seriously (<32 for now).
	 */

	/* Don't be strict about the congestion window for the
	 * final FIN frame.  -DaveM
	 */
	return ((!tail || !tcp_nagle_check(tp, skb, cur_mss) ||
		 skb_tailroom(skb) < 32) &&
		((tcp_packets_in_flight(tp) < tp->snd_cwnd) ||
		 (TCP_SKB_CB(skb)->flags & TCPCB_FLAG_FIN)) &&
		!after(TCP_SKB_CB(skb)->end_seq, tp->snd_una + tp->snd_wnd) &&
		tp->retransmits == 0);
}

static __inline__ void tcp_check_probe_timer(struct sock *sk, struct tcp_opt *tp)
{
	if (!tp->packets_out && !tp->probe_timer.prev)
		tcp_reset_xmit_timer(sk, TCP_TIME_PROBE0, tp->rto);
}

static __inline__ int tcp_skb_is_last(struct sock *sk, struct sk_buff *skb)
{
	return (skb->next == (struct sk_buff*)&sk->write_queue);
}

/* Push out any pending frames which were held back due to
 * TCP_CORK or attempt at coalescing tiny packets.
 * The socket must be locked by the caller.
 */
static __inline__ void __tcp_push_pending_frames(struct sock *sk,
						 struct tcp_opt *tp,
						 unsigned cur_mss)
{
	struct sk_buff *skb = tp->send_head;

	if (skb) {
		if (!tcp_snd_test(tp, skb, cur_mss, tcp_skb_is_last(sk, skb)) ||
		    tcp_write_xmit(sk))
			tcp_check_probe_timer(sk, tp);
	}
}

static __inline__ void tcp_push_pending_frames(struct sock *sk,
					       struct tcp_opt *tp)
{
	__tcp_push_pending_frames(sk, tp, tcp_current_mss(sk));
}

extern void			tcp_destroy_sock(struct sock *sk);


/*
 * Calculate(/check) TCP checksum
 */
static __inline__ u16 tcp_v4_check(struct tcphdr *th, int len,
				   unsigned long saddr, unsigned long daddr, 
				   unsigned long base)
{
	return csum_tcpudp_magic(saddr,daddr,len,IPPROTO_TCP,base);
}

static __inline__ int __tcp_checksum_complete(struct sk_buff *skb)
{
	return (unsigned short)csum_fold(csum_partial(skb->h.raw, skb->len, skb->csum));
}

static __inline__ int tcp_checksum_complete(struct sk_buff *skb)
{
	return skb->ip_summed != CHECKSUM_UNNECESSARY &&
		__tcp_checksum_complete(skb);
}


/* Prequeue for VJ style copy to user, combined with checksumming. */

static __inline__ void tcp_prequeue_init(struct tcp_opt *tp)
{
	tp->ucopy.task = NULL;
	tp->ucopy.len = 0;
	tp->ucopy.memory = 0;
	skb_queue_head_init(&tp->ucopy.prequeue);
}

/* Packet is added to VJ-style prequeue for processing in process
 * context, if a reader task is waiting. Apparently, this exciting
 * idea (VJ's mail "Re: query about TCP header on tcp-ip" of 07 Sep 93)
 * failed somewhere. Latency? Burstiness? Well, at least now we will
 * see, why it failed. 8)8)				  --ANK
 */
static __inline__ int tcp_prequeue(struct sock *sk, struct sk_buff *skb)
{
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;

	if (tp->ucopy.task) {
		if ((tp->ucopy.memory += skb->truesize) <= (sk->rcvbuf<<1)) {
			__skb_queue_tail(&tp->ucopy.prequeue, skb);
			if (skb_queue_len(&tp->ucopy.prequeue) == 1)
				wake_up_interruptible(sk->sleep);
		} else {
			NET_INC_STATS_BH(TCPPrequeueDropped);
			tp->ucopy.memory -= skb->truesize;
			kfree_skb(skb);
		}
		return 1;
	}
	return 0;
}


#undef STATE_TRACE

#ifdef STATE_TRACE
static char *statename[]={
	"Unused","Established","Syn Sent","Syn Recv",
	"Fin Wait 1","Fin Wait 2","Time Wait", "Close",
	"Close Wait","Last ACK","Listen","Closing"
};
#endif

static __inline__ void tcp_set_state(struct sock *sk, int state)
{
	int oldstate = sk->state;

	switch (state) {
	case TCP_ESTABLISHED:
		if (oldstate != TCP_ESTABLISHED)
			TCP_INC_STATS(TcpCurrEstab);
		break;

	case TCP_CLOSE:
		sk->prot->unhash(sk);
		/* fall through */
	default:
		if (oldstate==TCP_ESTABLISHED)
			tcp_statistics[smp_processor_id()*2+!in_softirq()].TcpCurrEstab--;
	}

	/* Change state AFTER socket is unhashed to avoid closed
	 * socket sitting in hash tables.
	 */
	sk->state = state;

#ifdef STATE_TRACE
	SOCK_DEBUG(sk, "TCP sk=%p, State %s -> %s\n",sk, statename[oldstate],statename[state]);
#endif	
}

static __inline__ void tcp_done(struct sock *sk)
{
	tcp_set_state(sk, TCP_CLOSE);
	tcp_clear_xmit_timers(sk);

	sk->shutdown = SHUTDOWN_MASK;

	if (!sk->dead)
		sk->state_change(sk);
	else
		tcp_destroy_sock(sk);
}

static __inline__ void tcp_build_and_update_options(__u32 *ptr, struct tcp_opt *tp, __u32 tstamp)
{
	if (tp->tstamp_ok) {
		*ptr++ = __constant_htonl((TCPOPT_NOP << 24) |
					  (TCPOPT_NOP << 16) |
					  (TCPOPT_TIMESTAMP << 8) |
					  TCPOLEN_TIMESTAMP);
		*ptr++ = htonl(tstamp);
		*ptr++ = htonl(tp->ts_recent);
	}
	if(tp->sack_ok && tp->num_sacks) {
		int this_sack;

		*ptr++ = __constant_htonl((TCPOPT_NOP << 24) |
					  (TCPOPT_NOP << 16) |
					  (TCPOPT_SACK << 8) |
					  (TCPOLEN_SACK_BASE +
					   (tp->num_sacks * TCPOLEN_SACK_PERBLOCK)));
		for(this_sack = 0; this_sack < tp->num_sacks; this_sack++) {
			*ptr++ = htonl(tp->selective_acks[this_sack].start_seq);
			*ptr++ = htonl(tp->selective_acks[this_sack].end_seq);
		}
	}
}

/* Construct a tcp options header for a SYN or SYN_ACK packet.
 * If this is every changed make sure to change the definition of
 * MAX_SYN_SIZE to match the new maximum number of options that you
 * can generate.
 */
extern __inline__ void tcp_syn_build_options(__u32 *ptr, int mss, int ts, int sack,
					     int offer_wscale, int wscale, __u32 tstamp, __u32 ts_recent)
{
	/* We always get an MSS option.
	 * The option bytes which will be seen in normal data
	 * packets should timestamps be used, must be in the MSS
	 * advertised.  But we subtract them from tp->mss_cache so
	 * that calculations in tcp_sendmsg are simpler etc.
	 * So account for this fact here if necessary.  If we
	 * don't do this correctly, as a receiver we won't
	 * recognize data packets as being full sized when we
	 * should, and thus we won't abide by the delayed ACK
	 * rules correctly.
	 * SACKs don't matter, we never delay an ACK when we
	 * have any of those going out.
	 */
	*ptr++ = htonl((TCPOPT_MSS << 24) | (TCPOLEN_MSS << 16) | mss);
	if (ts) {
		if(sack)
			*ptr++ = __constant_htonl((TCPOPT_SACK_PERM << 24) | (TCPOLEN_SACK_PERM << 16) |
						  (TCPOPT_TIMESTAMP << 8) | TCPOLEN_TIMESTAMP);
		else
			*ptr++ = __constant_htonl((TCPOPT_NOP << 24) | (TCPOPT_NOP << 16) |
						  (TCPOPT_TIMESTAMP << 8) | TCPOLEN_TIMESTAMP);
		*ptr++ = htonl(tstamp);		/* TSVAL */
		*ptr++ = htonl(ts_recent);	/* TSECR */
	} else if(sack)
		*ptr++ = __constant_htonl((TCPOPT_NOP << 24) | (TCPOPT_NOP << 16) |
					  (TCPOPT_SACK_PERM << 8) | TCPOLEN_SACK_PERM);
	if (offer_wscale)
		*ptr++ = htonl((TCPOPT_NOP << 24) | (TCPOPT_WINDOW << 16) | (TCPOLEN_WINDOW << 8) | (wscale));
}

/* Determine a window scaling and initial window to offer.
 * Based on the assumption that the given amount of space
 * will be offered. Store the results in the tp structure.
 * NOTE: for smooth operation initial space offering should
 * be a multiple of mss if possible. We assume here that mss >= 1.
 * This MUST be enforced by all callers.
 */
extern __inline__ void tcp_select_initial_window(int space, __u32 mss,
	__u32 *rcv_wnd,
	__u32 *window_clamp,
	int wscale_ok,
	__u8 *rcv_wscale)
{
	/* If no clamp set the clamp to the max possible scaled window */
	if (*window_clamp == 0)
		(*window_clamp) = (65535<<14);
	space = min(*window_clamp,space);

	/* Quantize space offering to a multiple of mss if possible. */
	if (space > mss)
		space = (space/mss)*mss;

	/* NOTE: offering an initial window larger than 32767
	 * will break some buggy TCP stacks. We try to be nice.
	 * If we are not window scaling, then this truncates
	 * our initial window offering to 32k. There should also
	 * be a sysctl option to stop being nice.
	 */
	(*rcv_wnd) = min(space, MAX_TCP_WINDOW);
	(*rcv_wscale) = 0;
	if (wscale_ok) {
		/* See RFC1323 for an explanation of the limit to 14 */
		while (space > 65535 && (*rcv_wscale) < 14) {
			space >>= 1;
			(*rcv_wscale)++;
		}
	}
	/* Set the clamp no higher than max representable value */
	(*window_clamp) = min(65535<<(*rcv_wscale),*window_clamp);
}

/* Note: caller must be prepared to deal with negative returns */ 
extern __inline__ int tcp_space(struct sock *sk)
{
	return (sk->rcvbuf - atomic_read(&sk->rmem_alloc)) / 
		TCP_WINDOW_ADVERTISE_DIVISOR; 
} 

extern __inline__ int tcp_full_space( struct sock *sk)
{
	return sk->rcvbuf / TCP_WINDOW_ADVERTISE_DIVISOR; 
}

extern __inline__ void tcp_init_buffer_space(struct sock *sk)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	int rcvbuf = tp->advmss+MAX_TCP_HEADER+16+sizeof(struct sk_buff);
	int sndbuf = tp->mss_clamp+MAX_TCP_HEADER+16+sizeof(struct sk_buff);

	if (sk->rcvbuf < 3*rcvbuf)
		sk->rcvbuf = min (3*rcvbuf, sysctl_rmem_max);
	if (sk->sndbuf < 3*sndbuf)
		sk->sndbuf = min (3*sndbuf, sysctl_wmem_max);
}

extern __inline__ void tcp_acceptq_removed(struct sock *sk)
{
	sk->ack_backlog--;
}

extern __inline__ void tcp_acceptq_added(struct sock *sk)
{
	sk->ack_backlog++;
}

extern __inline__ int tcp_acceptq_is_full(struct sock *sk)
{
	return sk->ack_backlog > sk->max_ack_backlog;
}

extern __inline__ void tcp_acceptq_queue(struct sock *sk, struct open_request *req,
					 struct sock *child)
{
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;

	req->sk = child;
	tcp_acceptq_added(sk);

	req->dl_next = tp->accept_queue;
	tp->accept_queue = req;
}

struct tcp_listen_opt
{
	u8			max_qlen_log;	/* log_2 of maximal queued SYNs */
	int			qlen;
	int			qlen_young;
	int			clock_hand;
	struct open_request	*syn_table[TCP_SYNQ_HSIZE];
};

extern __inline__ void
tcp_synq_removed(struct sock *sk, struct open_request *req)
{
	struct tcp_listen_opt *lopt = sk->tp_pinfo.af_tcp.listen_opt;

	if (--lopt->qlen == 0)
		tcp_delete_keepalive_timer(sk);
	if (req->retrans == 0)
		lopt->qlen_young--;
}

extern __inline__ void tcp_synq_added(struct sock *sk)
{
	struct tcp_listen_opt *lopt = sk->tp_pinfo.af_tcp.listen_opt;

	if (lopt->qlen++ == 0)
		tcp_reset_keepalive_timer(sk, TCP_TIMEOUT_INIT);
	lopt->qlen_young++;
}

extern __inline__ int tcp_synq_len(struct sock *sk)
{
	return sk->tp_pinfo.af_tcp.listen_opt->qlen;
}

extern __inline__ int tcp_synq_young(struct sock *sk)
{
	return sk->tp_pinfo.af_tcp.listen_opt->qlen_young;
}

extern __inline__ int tcp_synq_is_full(struct sock *sk)
{
	return tcp_synq_len(sk)>>sk->tp_pinfo.af_tcp.listen_opt->max_qlen_log;
}

extern __inline__ void tcp_synq_unlink(struct tcp_opt *tp, struct open_request *req,
				       struct open_request **prev)
{
	write_lock(&tp->syn_wait_lock);
	*prev = req->dl_next;
	write_unlock(&tp->syn_wait_lock);
}

extern __inline__ void tcp_synq_drop(struct sock *sk, struct open_request *req,
				     struct open_request **prev)
{
	tcp_synq_unlink(&sk->tp_pinfo.af_tcp, req, prev);
	tcp_synq_removed(sk, req);
	tcp_openreq_free(req);
}

static __inline__ void tcp_openreq_init(struct open_request *req,
					struct tcp_opt *tp,
					struct sk_buff *skb)
{
	req->rcv_wnd = 0;		/* So that tcp_send_synack() knows! */
	req->rcv_isn = TCP_SKB_CB(skb)->seq;
	req->mss = tp->mss_clamp;
	req->ts_recent = tp->saw_tstamp ? tp->rcv_tsval : 0;
	req->tstamp_ok = tp->tstamp_ok;
	req->sack_ok = tp->sack_ok;
	req->snd_wscale = tp->snd_wscale;
	req->wscale_ok = tp->wscale_ok;
	req->acked = 0;
	req->rmt_port = skb->h.th->source;
}

extern const char timer_bug_msg[];

static inline void tcp_clear_xmit_timer(struct sock *sk, int what)
{
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;
	struct timer_list *timer;
	
	switch (what) {
	case TCP_TIME_RETRANS:
		timer = &tp->retransmit_timer;
		break;
	case TCP_TIME_DACK:
		tp->ack.blocked = 0;
		timer = &tp->delack_timer;
		break;
	case TCP_TIME_PROBE0:
		timer = &tp->probe_timer;
		break;	
	default:
		printk(timer_bug_msg);
		return;
	};

	spin_lock_bh(&sk->timer_lock);
	if (timer->prev != NULL && del_timer(timer))
		__sock_put(sk);
	spin_unlock_bh(&sk->timer_lock);
}

/* This function does not return reliable answer. Use it only as advice.
 */

static inline int tcp_timer_is_set(struct sock *sk, int what)
{
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;
	int ret;

	switch (what) {
	case TCP_TIME_RETRANS:
		ret = tp->retransmit_timer.prev != NULL;
		break;
	case TCP_TIME_DACK:
		ret = tp->delack_timer.prev != NULL;
		break;
	case TCP_TIME_PROBE0:
		ret = tp->probe_timer.prev != NULL;
		break;	
	default:
		ret = 0;
		printk(timer_bug_msg);
	};
	return ret;
}


extern void tcp_listen_wlock(void);

/* - We may sleep inside this lock.
 * - If sleeping is not required (or called from BH),
 *   use plain read_(un)lock(&tcp_lhash_lock).
 */

extern __inline__ void tcp_listen_lock(void)
{
	/* read_lock synchronizes to candidates to writers */
	read_lock(&tcp_lhash_lock);
	atomic_inc(&tcp_lhash_users);
	read_unlock(&tcp_lhash_lock);
}

extern __inline__ void tcp_listen_unlock(void)
{
	if (atomic_dec_and_test(&tcp_lhash_users))
		wake_up(&tcp_lhash_wait);
}

static inline int keepalive_intvl_when(struct tcp_opt *tp)
{
	return tp->keepalive_intvl ? : sysctl_tcp_keepalive_intvl;
}

static inline int keepalive_time_when(struct tcp_opt *tp)
{
	return tp->keepalive_time ? : sysctl_tcp_keepalive_time;
}

static inline int tcp_fin_time(struct tcp_opt *tp)
{
	int fin_timeout = tp->linger2 ? : sysctl_tcp_fin_timeout;

	if (fin_timeout < (tp->rto<<2) - (tp->rto>>1))
		fin_timeout = (tp->rto<<2) - (tp->rto>>1);

	return fin_timeout;
}

#if 0 /* TCP_DEBUG */
#define TCP_CHECK_TIMER(sk) \
do { 	struct tcp_opt *__tp = &sk->tp_pinfo.af_tcp; \
	if (sk->state != TCP_CLOSE) { \
		if (__tp->packets_out) { \
			if (!tcp_timer_is_set(sk, TCP_TIME_RETRANS) && !timer_is_running(&__tp->retransmit_timer) && net_ratelimit()) \
				printk(KERN_DEBUG "sk=%p RETRANS" __FUNCTION__ "(%d) %d\n", sk, __LINE__, sk->state); \
		} else if (__tp->send_head) { \
			if (!tcp_timer_is_set(sk, TCP_TIME_PROBE0) && !timer_is_running(&__tp->probe_timer) && net_ratelimit()) \
				printk(KERN_DEBUG "sk=%p PROBE0" __FUNCTION__ "(%d) %d\n", sk, __LINE__, sk->state); \
		} \
	        if (__tp->ack.pending) { \
			if (!tcp_timer_is_set(sk, TCP_TIME_DACK) && !timer_is_running(&__tp->delack_timer) && net_ratelimit()) \
				printk(KERN_DEBUG "sk=%p DACK" __FUNCTION__ "(%d) %d\n", sk, __LINE__, sk->state); \
		} \
                if (__tp->packets_out > skb_queue_len(&sk->write_queue) || \
		    (__tp->send_head && skb_queue_len(&sk->write_queue) == 0)) { \
			 printk(KERN_DEBUG "sk=%p QUEUE" __FUNCTION__ "(%d) %d %d %d %p\n", sk, __LINE__, sk->state, __tp->packets_out, skb_queue_len(&sk->write_queue), __tp->send_head); \
		} \
	} } while (0)
#else
#define TCP_CHECK_TIMER(sk) do { } while (0);
#endif

#endif	/* _TCP_H */
