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

#include <linux/config.h>
#include <linux/tcp.h>
#include <linux/slab.h>
#include <net/checksum.h>

/* This is for all connections with a full identity, no wildcards.
 * New scheme, half the table is for TIME_WAIT, the other half is
 * for the rest.  I'll experiment with dynamic table growth later.
 */
#define TCP_HTABLE_SIZE		512

/* This is for listening sockets, thus all sockets which possess wildcards. */
#define TCP_LHTABLE_SIZE	32	/* Yes, really, this is all you need. */

/* This is for all sockets, to keep track of the local port allocations. */
#define TCP_BHTABLE_SIZE	512

/* tcp_ipv4.c: These need to be shared by v4 and v6 because the lookup
 *             and hashing code needs to work with different AF's yet
 *             the port space is shared.
 */
extern struct sock *tcp_established_hash[TCP_HTABLE_SIZE];
extern struct sock *tcp_listening_hash[TCP_LHTABLE_SIZE];

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
	unsigned short		flags;
#define TCPB_FLAG_LOCKED	0x0001
#define TCPB_FLAG_FASTREUSE	0x0002

	struct tcp_bind_bucket	*next;
	struct sock		*owners;
	struct tcp_bind_bucket	**pprev;
};

extern struct tcp_bind_bucket *tcp_bound_hash[TCP_BHTABLE_SIZE];
extern kmem_cache_t *tcp_bucket_cachep;
extern struct tcp_bind_bucket *tcp_bucket_create(unsigned short snum);
extern void tcp_bucket_unlock(struct sock *sk);
extern int tcp_port_rover;

/* Level-1 socket-demux cache. */
#define TCP_NUM_REGS		32
extern struct sock *tcp_regs[TCP_NUM_REGS];

#define TCP_RHASH_FN(__fport) \
	((((__fport) >> 7) ^ (__fport)) & (TCP_NUM_REGS - 1))
#define TCP_RHASH(__fport)	tcp_regs[TCP_RHASH_FN((__fport))]
#define TCP_SK_RHASH_FN(__sock)	TCP_RHASH_FN((__sock)->dummy_th.dest)
#define TCP_SK_RHASH(__sock)	tcp_regs[TCP_SK_RHASH_FN((__sock))]

static __inline__ void tcp_reg_zap(struct sock *sk)
{
	struct sock **rpp;

	rpp = &(TCP_SK_RHASH(sk));
	if(*rpp == sk)
		*rpp = NULL;
}

/* These are AF independent. */
static __inline__ int tcp_bhashfn(__u16 lport)
{
	return (lport & (TCP_BHTABLE_SIZE - 1));
}

static __inline__ void tcp_sk_bindify(struct sock *sk)
{
	struct tcp_bind_bucket *tb;
	unsigned short snum = sk->num;

	for(tb = tcp_bound_hash[tcp_bhashfn(snum)]; tb->port != snum; tb = tb->next)
		;
	/* Update bucket flags. */
	if(tb->owners == NULL) {
		/* We're the first. */
		if(sk->reuse && sk->state != TCP_LISTEN)
			tb->flags = TCPB_FLAG_FASTREUSE;
		else
			tb->flags = 0;
	} else {
		if((tb->flags & TCPB_FLAG_FASTREUSE) &&
		   ((sk->reuse == 0) || (sk->state == TCP_LISTEN)))
			tb->flags &= ~TCPB_FLAG_FASTREUSE;
	}
	if((sk->bind_next = tb->owners) != NULL)
		tb->owners->bind_pprev = &sk->bind_next;
	tb->owners = sk;
	sk->bind_pprev = &tb->owners;
	sk->prev = (struct sock *) tb;
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
	struct sock		*sklist_next;
	struct sock		*sklist_prev;
	struct sock		*bind_next;
	struct sock		**bind_pprev;
	struct sock		*next;
	struct sock		**pprev;
	__u32			daddr;
	__u32			rcv_saddr;
	int			bound_dev_if;
	unsigned short		num;
	unsigned char		state,
				family;		/* sk->zapped */
	__u16			source;		/* sk->dummy_th.source */
	__u16			dest;		/* sk->dummy_th.dest */

	/* And these are ours. */
	__u32			rcv_nxt;
	struct tcp_func		*af_specific;
	struct tcp_bind_bucket	*tb;
	struct timer_list	timer;
#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
	struct in6_addr		v6_daddr;
	struct in6_addr		v6_rcv_saddr;
#endif
};

extern kmem_cache_t *tcp_timewait_cachep;

/* tcp_ipv4.c: These sysctl variables need to be shared between v4 and v6
 * because the v6 tcp code to intialize a connection needs to interoperate
 * with the v4 code using the same variables.
 * FIXME: It would be better to rewrite the connection code to be
 * address family independent and just leave one copy in the ipv4 section.
 * This would also clean up some code duplication. -- erics
 */
extern int sysctl_tcp_timestamps;
extern int sysctl_tcp_window_scaling;

/* These can have wildcards, don't try too hard. */
static __inline__ int tcp_lhashfn(unsigned short num)
{
	return num & (TCP_LHTABLE_SIZE - 1);
}

static __inline__ int tcp_sk_listen_hashfn(struct sock *sk)
{
	return tcp_lhashfn(sk->num);
}

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
#define NETHDR_SIZE	sizeof(struct ipv6hdr)
#else
#define NETHDR_SIZE	sizeof(struct iphdr) + 40
#endif

/*
 * 40 is maximal IP options size
 * 20 is the maximum TCP options size we can currently construct on a SYN.
 * 40 is the maximum possible TCP options size.
 */

#define MAX_SYN_SIZE	(NETHDR_SIZE + sizeof(struct tcphdr) + 20 + MAX_HEADER + 15)
#define MAX_FIN_SIZE	(NETHDR_SIZE + sizeof(struct tcphdr) + MAX_HEADER + 15)
#define BASE_ACK_SIZE	(NETHDR_SIZE + MAX_HEADER + 15)
#define MAX_ACK_SIZE	(NETHDR_SIZE + sizeof(struct tcphdr) + MAX_HEADER + 15)
#define MAX_RESET_SIZE	(NETHDR_SIZE + sizeof(struct tcphdr) + MAX_HEADER + 15)
#define MAX_TCPHEADER_SIZE (NETHDR_SIZE + sizeof(struct tcphdr) + 20 + MAX_HEADER + 15)

#define MAX_WINDOW	32767		/* Never offer a window over 32767 without using
					   window scaling (not yet supported). Some poor
					   stacks do signed 16bit maths! */
#define MIN_WINDOW	2048
#define MAX_ACK_BACKLOG	2
#define MAX_DELAY_ACK	2
#define MIN_WRITE_SPACE	2048
#define TCP_WINDOW_DIFF	2048

/* urg_data states */
#define URG_VALID	0x0100
#define URG_NOTYET	0x0200
#define URG_READ	0x0400

#define TCP_RETR1	7	/*
				 * This is how many retries it does before it
				 * tries to figure out if the gateway is
				 * down.
				 */

#define TCP_RETR2	15	/*
				 * This should take at least
				 * 90 minutes to time out.
				 */

#define TCP_TIMEOUT_LEN	(15*60*HZ) /* should be about 15 mins		*/
#define TCP_TIMEWAIT_LEN (60*HZ) /* how long to wait to successfully 
				  * close the socket, about 60 seconds	*/
#define TCP_FIN_TIMEOUT (3*60*HZ) /* BSD style FIN_WAIT2 deadlock breaker */

#define TCP_ACK_TIME	(3*HZ)	/* time to delay before sending an ACK	*/
#define TCP_DONE_TIME	(5*HZ/2)/* maximum time to wait before actually
				 * destroying a socket			*/
#define TCP_WRITE_TIME	(30*HZ)	/* initial time to wait for an ACK,
			         * after last transmit			*/
#define TCP_TIMEOUT_INIT (3*HZ)	/* RFC 1122 initial timeout value	*/
#define TCP_SYN_RETRIES	 10	/* number of times to retry opening a
				 * connection 	(TCP_RETR2-....)	*/
#define TCP_PROBEWAIT_LEN (1*HZ)/* time to wait between probes when
				 * I've got something to write and
				 * there is no window			*/
#define TCP_KEEPALIVE_TIME (180*60*HZ)		/* two hours */
#define TCP_KEEPALIVE_PROBES	9		/* Max of 9 keepalive probes	*/
#define TCP_KEEPALIVE_PERIOD ((75*HZ)>>2)	/* period of keepalive check	*/
#define TCP_NO_CHECK	0	/* turn to one if you want the default
				 * to be no checksum			*/

#define TCP_SYNACK_PERIOD	(HZ/2)
#define TCP_QUICK_TRIES		8  /* How often we try to retransmit, until
				    * we tell the LL layer that it is something
				    * wrong (e.g. that it can expire redirects) */

#define TCP_BUCKETGC_PERIOD	(HZ)

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
#define TCPOLEN_TSTAMP_ALIGNED	12
#define TCPOLEN_WSCALE_ALIGNED	4

/*
 *      TCP option flags for parsed options.
 */

#define TCPOPTF_SACK_PERM       1
#define TCPOPTF_TIMESTAMP       2

/*
 *	TCP Vegas constants
 */

#define TCP_VEGAS_ALPHA		2	/*  v_cong_detect_top_nseg */
#define TCP_VEGAS_BETA		4	/*  v_cong_detect_bot_nseg */
#define TCP_VEGAS_GAMMA		1	/*  v_exp_inc_nseg	   */

struct open_request;

struct or_calltable {
	void (*rtx_syn_ack)	(struct sock *sk, struct open_request *req);
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
	struct ipv6_options	*opt;
	struct device		*dev;
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
	__u8			__pad;
	unsigned snd_wscale : 4, 
		rcv_wscale : 4, 
		tstamp_ok : 1,
		wscale_ok : 1;
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
#ifdef CONFIG_IP_TRANSPARENT_PROXY
	__u16			lcl_port; /* LVE */
#endif
};

/* SLAB cache for open requests. */
extern kmem_cache_t *tcp_openreq_cachep;

#define tcp_openreq_alloc()	kmem_cache_alloc(tcp_openreq_cachep, SLAB_ATOMIC)
#define tcp_openreq_free(req)	kmem_cache_free(tcp_openreq_cachep, req)

/*
 *	Pointers to address related TCP functions
 *	(i.e. things that depend on the address family)
 */

struct tcp_func {
	int			(*build_net_header)	(struct sock *sk, 
							 struct sk_buff *skb);

	void			(*queue_xmit)		(struct sk_buff *skb);

	void			(*send_check)		(struct sock *sk,
							 struct tcphdr *th,
							 int len,
							 struct sk_buff *skb);

	int			(*rebuild_header)	(struct sock *sk,
							 struct sk_buff *skb);

	int			(*conn_request)		(struct sock *sk,
							 struct sk_buff *skb,
							 void *opt, __u32 isn);

	struct sock *		(*syn_recv_sock)	(struct sock *sk,
							 struct sk_buff *skb,
							 struct open_request *req,
							 struct dst_entry *dst);
	
	struct sock *		(*get_sock)		(struct sk_buff *skb,
							 struct tcphdr *th);

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
extern struct tcp_mib tcp_statistics;

extern unsigned short		tcp_good_socknum(void);

extern void			tcp_v4_err(struct sk_buff *skb,
					   unsigned char *, int);

extern void			tcp_shutdown (struct sock *sk, int how);

extern int			tcp_v4_rcv(struct sk_buff *skb,
					   unsigned short len);

extern int			tcp_do_sendmsg(struct sock *sk, 
					       int iovlen, struct iovec *iov,
					       int flags);

extern int			tcp_ioctl(struct sock *sk, 
					  int cmd, 
					  unsigned long arg);

extern int			tcp_rcv_state_process(struct sock *sk, 
						      struct sk_buff *skb,
						      struct tcphdr *th,
						      void *opt, __u16 len);

extern int			tcp_rcv_established(struct sock *sk, 
						    struct sk_buff *skb,
						    struct tcphdr *th, 
						    __u16 len);

extern int			tcp_timewait_state_process(struct tcp_tw_bucket *tw,
							   struct sk_buff *skb,
							   struct tcphdr *th,
							   void *opt, __u16 len);

extern void			tcp_close(struct sock *sk, 
					  unsigned long timeout);
extern struct sock *		tcp_accept(struct sock *sk, int flags);
extern unsigned int		tcp_poll(struct file * file, struct socket *sock, struct poll_table_struct *wait);
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

extern void			tcp_parse_options(struct tcphdr *th, struct tcp_opt *tp, 
									  int no_fancy);

/*
 *	TCP v4 functions exported for the inet6 API
 */

extern int		       	tcp_v4_rebuild_header(struct sock *sk, 
						      struct sk_buff *skb);

extern int		       	tcp_v4_build_header(struct sock *sk, 
						    struct sk_buff *skb);

extern void		       	tcp_v4_send_check(struct sock *sk, 
						  struct tcphdr *th, int len, 
						  struct sk_buff *skb);

extern int			tcp_v4_conn_request(struct sock *sk,
						    struct sk_buff *skb,
						    void *ptr, __u32 isn);

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

/* From syncookies.c */
extern struct sock *cookie_v4_check(struct sock *sk, struct sk_buff *skb, 
				    struct ip_options *opt);
extern __u32 cookie_v4_init_sequence(struct sock *sk, struct sk_buff *skb, 
				     __u16 *mss);

extern void tcp_read_wakeup(struct sock *);
extern void tcp_write_xmit(struct sock *);
extern void tcp_time_wait(struct sock *);
extern void tcp_do_retransmit(struct sock *, int);
extern void tcp_simple_retransmit(struct sock *);

/* tcp_output.c */

extern void tcp_send_probe0(struct sock *);
extern void tcp_send_partial(struct sock *);
extern void tcp_write_wakeup(struct sock *);
extern void tcp_send_fin(struct sock *sk);
extern void tcp_send_active_reset(struct sock *sk);
extern int  tcp_send_synack(struct sock *);
extern void tcp_send_skb(struct sock *, struct sk_buff *, int force_queue);
extern void tcp_send_ack(struct sock *sk);
extern void tcp_send_delayed_ack(struct tcp_opt *tp, int max_timeout);

/* CONFIG_IP_TRANSPARENT_PROXY */
extern int tcp_chkaddr(struct sk_buff *);

/* tcp_timer.c */
#define     tcp_reset_msl_timer(x,y,z)	net_reset_timer(x,y,z)
extern void tcp_reset_xmit_timer(struct sock *, int, unsigned long);
extern void tcp_init_xmit_timers(struct sock *);
extern void tcp_clear_xmit_timers(struct sock *);

extern void tcp_retransmit_timer(unsigned long);
extern void tcp_delack_timer(unsigned long);
extern void tcp_probe_timer(unsigned long);

extern struct sock *tcp_check_req(struct sock *sk, struct sk_buff *skb, 
				  struct open_request *req);

/*
 *	TCP slow timer
 */
extern struct timer_list	tcp_slow_timer;

struct tcp_sl_timer {
	atomic_t	count;
	unsigned long	period;
	unsigned long	last;
	void (*handler)	(unsigned long);
};

#define TCP_SLT_SYNACK		0
#define TCP_SLT_KEEPALIVE	1
#define TCP_SLT_BUCKETGC	2
#define TCP_SLT_MAX		3

extern struct tcp_sl_timer tcp_slt_array[TCP_SLT_MAX];
 
/* Compute the actual receive window we are currently advertising. */
static __inline__ u32 tcp_receive_window(struct tcp_opt *tp)
{
	return tp->rcv_wup - (tp->rcv_nxt - tp->rcv_wnd);
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
	u32 new_win = __tcp_select_window(sk);
	u32 cur_win = tcp_receive_window(tp);

	/* Never shrink the offered window */
	if(new_win < cur_win)
		new_win = cur_win;
	tp->rcv_wnd = new_win;
	tp->rcv_wup = tp->rcv_nxt;

	/* RFC1323 scaling applied */
	return new_win >> tp->rcv_wscale;
}

/* See if we can advertise non-zero, and if so how much we
 * can increase our advertisement.  If it becomes more than
 * twice what we are talking about right now, return true.
 */
extern __inline__ int tcp_raise_window(struct sock *sk)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	u32 new_win = __tcp_select_window(sk);
	u32 cur_win = tcp_receive_window(tp);

	return (new_win && (new_win > (cur_win << 1)));
}

/* This checks if the data bearing packet SKB (usually tp->send_head)
 * should be put on the wire right now.
 */
static __inline__ int tcp_snd_test(struct sock *sk, struct sk_buff *skb)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	int nagle_check = 1;
	int len;

	/*	RFC 1122 - section 4.2.3.4
	 *
	 *	We must queue if
	 *
	 *	a) The right edge of this frame exceeds the window
	 *	b) There are packets in flight and we have a small segment
	 *	   [SWS avoidance and Nagle algorithm]
	 *	   (part of SWS is done on packetization)
	 *	c) We are retransmiting [Nagle]
	 *	d) We have too many packets 'in flight'
	 *
	 * 	Don't use the nagle rule for urgent data.
	 */
	len = skb->end_seq - skb->seq;
	if (!sk->nonagle && len < (sk->mss >> 1) && tp->packets_out && 
	    !skb->h.th->urg)
		nagle_check = 0;

	return (nagle_check && tp->packets_out < tp->snd_cwnd &&
		!after(skb->end_seq, tp->snd_una + tp->snd_wnd) &&
		tp->retransmits == 0);
}

/* This tells the input processing path that an ACK should go out
 * right now.
 */
#define tcp_enter_quickack_mode(__tp)	((__tp)->ato = (HZ/100))
#define tcp_in_quickack_mode(__tp)	((__tp)->ato == (HZ/100))

/*
 * List all states of a TCP socket that can be viewed as a "connected"
 * state.  This now includes TCP_SYN_RECV, although I am not yet fully
 * convinced that this is the solution for the 'getpeername(2)'
 * problem. Thanks to Stephen A. Wood <saw@cebaf.gov>  -FvK
 */

extern __inline const int tcp_connected(const int state)
{
	return ((1 << state) &
	       	(TCPF_ESTABLISHED|TCPF_CLOSE_WAIT|TCPF_FIN_WAIT1|
		 TCPF_FIN_WAIT2|TCPF_SYN_RECV));
}

/*
 * Calculate(/check) TCP checksum
 */
static __inline__ u16 tcp_v4_check(struct tcphdr *th, int len,
				   unsigned long saddr, unsigned long daddr, 
				   unsigned long base)
{
	return csum_tcpudp_magic(saddr,daddr,len,IPPROTO_TCP,base);
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
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;
	int oldstate = sk->state;

	sk->state = state;

#ifdef STATE_TRACE
	SOCK_DEBUG(sk, "TCP sk=%p, State %s -> %s\n",sk, statename[oldstate],statename[state]);
#endif	

	switch (state) {
	case TCP_ESTABLISHED:
		if (oldstate != TCP_ESTABLISHED)
			tcp_statistics.TcpCurrEstab++;
		break;

	case TCP_CLOSE:
		/* Should be about 2 rtt's */
		net_reset_timer(sk, TIME_DONE, min(tp->srtt * 2, TCP_DONE_TIME));
		sk->prot->unhash(sk);
		/* fall through */
	default:
		if (oldstate==TCP_ESTABLISHED)
			tcp_statistics.TcpCurrEstab--;
	}
}

static __inline__ void tcp_build_options(__u32 *ptr, struct tcp_opt *tp)
{
	if (tp->tstamp_ok) {
		*ptr = __constant_htonl((TCPOPT_NOP << 24) |
					(TCPOPT_NOP << 16) |
					(TCPOPT_TIMESTAMP << 8) |
					TCPOLEN_TIMESTAMP);
		/* rest filled in by tcp_update_options */
	}
}

static __inline__ void tcp_update_options(__u32 *ptr, struct tcp_opt *tp)
{
	if (tp->tstamp_ok) {
		*++ptr = htonl(jiffies);
		*++ptr = htonl(tp->ts_recent);
	}
}

static __inline__ void tcp_build_and_update_options(__u32 *ptr, struct tcp_opt *tp)
{
	if (tp->tstamp_ok) {
		*ptr++ = __constant_htonl((TCPOPT_NOP << 24) |
					  (TCPOPT_NOP << 16) |
					  (TCPOPT_TIMESTAMP << 8) |
					  TCPOLEN_TIMESTAMP);
		*ptr++ = htonl(jiffies);
		*ptr   = htonl(tp->ts_recent);
	}
}

/* 
 *	This routines builds a generic TCP header. 
 *	They also build the RFC1323 Timestamp, but don't fill the
 *	actual timestamp in (you need to call tcp_update_options for this).
 *	XXX: pass tp instead of sk here.
 */
 
static inline void tcp_build_header_data(struct tcphdr *th, struct sock *sk, int push)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

	memcpy(th,(void *) &(sk->dummy_th), sizeof(*th));
	th->seq = htonl(tp->write_seq);
	if (!push)
		th->psh = 1;
	tcp_build_options((__u32*)(th+1), tp);
}

/*
 * Construct a tcp options header for a SYN or SYN_ACK packet.
 * If this is every changed make sure to change the definition of
 * MAX_SYN_SIZE to match the new maximum number of options that you
 * can generate.
 * FIXME: This is completely disgusting.
 * This is probably a good candidate for a bit of assembly magic.
 * It would be especially magical to compute the checksum for this
 * stuff on the fly here.
 */
extern __inline__ int tcp_syn_build_options(struct sk_buff *skb, int mss, int ts, int offer_wscale, int wscale)
{
	int count = 4 + (offer_wscale ? TCPOLEN_WSCALE_ALIGNED : 0) +
		((ts) ? TCPOLEN_TSTAMP_ALIGNED : 0);
	unsigned char *optr = skb_put(skb,count);
	__u32 *ptr = (__u32 *)optr;

	/* We always get an MSS option.
	 * The option bytes which will be seen in normal data
	 * packets should timestamps be used, must be in the MSS
	 * advertised.  But we subtract them from sk->mss so
	 * that calculations in tcp_sendmsg are simpler etc.
	 * So account for this fact here if necessary.  If we
	 * don't do this correctly, as a receiver we won't
	 * recognize data packets as being full sized when we
	 * should, and thus we won't abide by the delayed ACK
	 * rules correctly.
	 */
	if(ts)
		mss += TCPOLEN_TSTAMP_ALIGNED;
	*ptr++ = htonl((TCPOPT_MSS << 24) | (TCPOLEN_MSS << 16) | mss);
	if (ts) {
		*ptr++ = __constant_htonl((TCPOPT_NOP << 24) | (TCPOPT_NOP << 16) |
					  (TCPOPT_TIMESTAMP << 8) | TCPOLEN_TIMESTAMP);
		*ptr++ = htonl(jiffies);	/* TSVAL */
		*ptr++ = __constant_htonl(0);	/* TSECR */
	}
	if (offer_wscale)
		*ptr++ = htonl((TCPOPT_WINDOW << 24) | (TCPOLEN_WINDOW << 16) | (wscale << 8));
	skb->csum = csum_partial(optr, count, 0);
	return count;
}

/* Determine a window scaling and initial window to offer.
 * Based on the assumption that the given amount of space
 * will be offered. Store the results in the tp structure.
 * NOTE: for smooth operation initial space offering should
 * be a multiple of mss if possible. We assume here that mss >= 1.
 * This MUST be enforced by all callers.
 */
extern __inline__ void tcp_select_initial_window(__u32 space, __u16 mss,
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
	(*rcv_wnd) = min(space,32767);
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

extern __inline__ void tcp_synq_unlink(struct tcp_opt *tp, struct open_request *req, struct open_request *prev)
{
	if(!req->dl_next)
		tp->syn_wait_last = (struct open_request **)prev;
	prev->dl_next = req->dl_next;
}

extern __inline__ void tcp_synq_queue(struct tcp_opt *tp, struct open_request *req)
{ 
	req->dl_next = NULL;
	*tp->syn_wait_last = req; 
	tp->syn_wait_last = &req->dl_next;
}

extern __inline__ void tcp_synq_init(struct tcp_opt *tp)
{
	tp->syn_wait_queue = NULL;
	tp->syn_wait_last = &tp->syn_wait_queue;
}

extern __inline__ struct open_request *tcp_synq_unlink_tail(struct tcp_opt *tp)
{
	struct open_request *head = tp->syn_wait_queue;
#if 0
	/* Should be a net-ratelimit'd thing, not all the time. */
	printk(KERN_DEBUG "synq tail drop with expire=%ld\n", 
	       head->expires-jiffies);
#endif
	if (head->dl_next == NULL)
		tp->syn_wait_last = &tp->syn_wait_queue;
	tp->syn_wait_queue = head->dl_next;
	return head;
}

extern void __tcp_inc_slow_timer(struct tcp_sl_timer *slt);
extern __inline__ void tcp_inc_slow_timer(int timer)
{
	struct tcp_sl_timer *slt = &tcp_slt_array[timer];
	
	if (atomic_read(&slt->count) == 0)
	{
		__tcp_inc_slow_timer(slt);
	}

	atomic_inc(&slt->count);
}

extern __inline__ void tcp_dec_slow_timer(int timer)
{
	struct tcp_sl_timer *slt = &tcp_slt_array[timer];

	atomic_dec(&slt->count);
}

/* This needs to use a slow timer, so it is here. */
static __inline__ void tcp_sk_unbindify(struct sock *sk)
{
	struct tcp_bind_bucket *tb = (struct tcp_bind_bucket *) sk->prev;
	if(sk->bind_next)
		sk->bind_next->bind_pprev = sk->bind_pprev;
	*sk->bind_pprev = sk->bind_next;
	if(tb->owners == NULL)
		tcp_inc_slow_timer(TCP_SLT_BUCKETGC);
}

extern const char timer_bug_msg[];

static inline void tcp_clear_xmit_timer(struct sock *sk, int what)
{
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;
	struct timer_list *timer;
	
	switch (what) {
	case TIME_RETRANS:
		timer = &tp->retransmit_timer;
		break;
	case TIME_DACK:
		timer = &tp->delack_timer;
		break;
	case TIME_PROBE0:
		timer = &tp->probe_timer;
		break;	
	default:
		printk(timer_bug_msg);
		return;
	};
	if(timer->prev != NULL)
		del_timer(timer);
}

static inline int tcp_timer_is_set(struct sock *sk, int what)
{
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;

	switch (what) {
	case TIME_RETRANS:
		return tp->retransmit_timer.prev != NULL;
		break;
	case TIME_DACK:
		return tp->delack_timer.prev != NULL;
		break;
	case TIME_PROBE0:
		return tp->probe_timer.prev != NULL;
		break;	
	default:
		printk(timer_bug_msg);
	};
	return 0;
}


#endif	/* _TCP_H */
