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
#include <net/checksum.h>


#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
#define NETHDR_SIZE	sizeof(struct ipv6hdr)
#else
#define NETHDR_SIZE	sizeof(struct iphdr) + 40
#endif

/*
 * 40 is maximal IP options size
 * 4  is TCP option size (MSS)
 */

#define MAX_SYN_SIZE	(NETHDR_SIZE + sizeof(struct tcphdr) + 4 + MAX_HEADER + 15)
#define MAX_FIN_SIZE	(NETHDR_SIZE + sizeof(struct tcphdr) + MAX_HEADER + 15)
#define MAX_ACK_SIZE	(NETHDR_SIZE + sizeof(struct tcphdr) + MAX_HEADER + 15)
#define MAX_RESET_SIZE	(NETHDR_SIZE + sizeof(struct tcphdr) + MAX_HEADER + 15)

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
#define TCP_KEEPALIVE_TIME (180*60*HZ)	/* two hours */
#define TCP_KEEPALIVE_PROBES	9	/* Max of 9 keepalive probes	*/
#define TCP_KEEPALIVE_PERIOD (75*HZ)	/* period of keepalive check	*/
#define TCP_NO_CHECK	0	/* turn to one if you want the default
				 * to be no checksum			*/

#define TCP_SYNACK_PERIOD	(HZ/2)

/*
 *	TCP option
 */
 
#define TCPOPT_NOP		1	/* Padding */
#define TCPOPT_EOL		0	/* End of options */
#define TCPOPT_MSS		2	/* Segment size negotiating */
/*
 *	We don't use these yet, but they are for PAWS and big windows
 */
#define TCPOPT_WINDOW		3	/* Window scaling */
#define TCPOPT_TIMESTAMP	8	/* Better RTT estimations/PAWS */

/*
 *     TCP option lengths
 */

#define TCPOLEN_MSS            4
#define TCPOLEN_WINDOW         3
#define TCPOLEN_TIMESTAMP      10


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
};

struct open_request {
	struct open_request	*dl_next;
	struct open_request	*dl_prev;
	__u32			rcv_isn;
	__u32			snt_isn;
	__u16			mss;
	__u16			rmt_port;
	unsigned long		expires;
	int			retrans;
	struct or_calltable	*class;
	struct sock		*sk;
};

struct tcp_v4_open_req {
	struct open_request	req;
	__u32			loc_addr;
	__u32			rmt_addr;
	struct ip_options	*opt;
};

#if defined(CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
struct tcp_v6_open_req {
	struct open_request	req;
	struct in6_addr		loc_addr;
	struct in6_addr		rmt_addr;
	struct ipv6_options	*opt;
	struct device		*dev;
};
#endif

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
							 void *opt,
							 __u32 isn);

	struct sock *		(*syn_recv_sock)	(struct sock *sk,
							 struct sk_buff *skb,
							 struct open_request *req);
	
	__u32			(*init_sequence)	(struct sock *sk,
							 struct sk_buff *skb);

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

	void			(*send_reset)		(struct sk_buff *skb);

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
	return (after(seq1+1, seq2) && before(seq1, seq3+1));
}


extern struct proto tcp_prot;
extern struct tcp_mib tcp_statistics;

extern void			tcp_v4_err(struct sk_buff *skb,
					   unsigned char *);

extern void			tcp_shutdown (struct sock *sk, int how);

extern int			tcp_v4_rcv(struct sk_buff *skb,
					   unsigned short len);

extern int			tcp_do_sendmsg(struct sock *sk, 
					       int iovlen, struct iovec *iov,
					       int len, int flags);

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

extern void			tcp_close(struct sock *sk, 
					  unsigned long timeout);
extern struct sock *		tcp_accept(struct sock *sk, int flags);
extern int			tcp_select(struct socket *sock, int sel_type, 
					   select_table *wait);
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

extern int			tcp_parse_options(struct tcphdr *th);

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

extern struct sock *		tcp_v4_syn_recv_sock(struct sock *sk,
						     struct sk_buff *skb,
						     struct open_request *req);

extern int			tcp_v4_backlog_rcv(struct sock *sk,
						   struct sk_buff *skb);
extern int			tcp_v4_connect(struct sock *sk,
					       struct sockaddr *uaddr,
					       int addr_len);


extern void tcp_read_wakeup(struct sock *);
extern void tcp_write_xmit(struct sock *);
extern void tcp_time_wait(struct sock *);
extern void tcp_do_retransmit(struct sock *, int);

/* tcp_output.c */

extern void tcp_send_probe0(struct sock *);
extern void tcp_send_partial(struct sock *);
extern void tcp_write_wakeup(struct sock *);
extern void tcp_send_fin(struct sock *sk);
extern int  tcp_send_synack(struct sock *);
extern int  tcp_send_skb(struct sock *, struct sk_buff *);
extern void tcp_send_ack(struct sock *sk);
extern void tcp_send_delayed_ack(struct sock *sk, int max_timeout);

/* tcp_input.c */
extern void tcp_cache_zap(void);

/* CONFIG_IP_TRANSPARENT_PROXY */
extern int tcp_chkaddr(struct sk_buff *);

/* tcp_timer.c */
#define     tcp_reset_msl_timer(x,y,z)	net_reset_timer(x,y,z)
extern void tcp_reset_xmit_timer(struct sock *, int, unsigned long);
extern void tcp_clear_xmit_timer(struct sock *, int);
extern int  tcp_timer_is_set(struct sock *, int);
extern void tcp_init_xmit_timers(struct sock *);
extern void tcp_clear_xmit_timers(struct sock *);

extern void tcp_retransmit_timer(unsigned long);
extern void tcp_delack_timer(unsigned long);
extern void tcp_probe_timer(unsigned long);


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
#define TCP_SLT_MAX		2

extern struct tcp_sl_timer tcp_slt_array[TCP_SLT_MAX];
 
static __inline__ unsigned short tcp_raise_window(struct sock *sk)
{
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;
	long cur_win;
	int res = 0;
	
	/* 
         * compute the actual window i.e. 
         * old_window - received_bytes_on_that_win
	 */

	cur_win = tp->rcv_wup - (tp->rcv_nxt - tp->rcv_wnd);


	/*
	 *	We need to send an ack right away if
	 *	our rcv window is blocking the sender and 
	 *	we have more free space to offer.
	 */

	if (cur_win < (sk->mss << 1))
		res = 1;
	return res;
}

extern unsigned short	tcp_select_window(struct sock *sk);

/*
 * List all states of a TCP socket that can be viewed as a "connected"
 * state.  This now includes TCP_SYN_RECV, although I am not yet fully
 * convinced that this is the solution for the 'getpeername(2)'
 * problem. Thanks to Stephen A. Wood <saw@cebaf.gov>  -FvK
 */

extern __inline const int tcp_connected(const int state)
{
  return(state == TCP_ESTABLISHED || state == TCP_CLOSE_WAIT ||
	 state == TCP_FIN_WAIT1   || state == TCP_FIN_WAIT2 ||
	 state == TCP_SYN_RECV);
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
	if(sk->debug)
		printk("TCP sk=%p, State %s -> %s\n",sk, statename[oldstate],statename[state]);
#endif	

	switch (state) {
	case TCP_ESTABLISHED:
		if (oldstate != TCP_ESTABLISHED) {
			tcp_statistics.TcpCurrEstab++;
		}
		break;

	case TCP_CLOSE:
		tcp_cache_zap();
		/* Should be about 2 rtt's */
		net_reset_timer(sk, TIME_DONE, min(tp->srtt * 2, TCP_DONE_TIME));
		/* fall through */
	default:
		if (oldstate==TCP_ESTABLISHED)
			tcp_statistics.TcpCurrEstab--;
	}
}

extern __inline__ void tcp_synq_unlink(struct tcp_opt *tp, struct open_request *req)
{
	if (req->dl_next == req)
	{
		tp->syn_wait_queue = NULL;
	}
	else
	{
		req->dl_prev->dl_next = req->dl_next;
		req->dl_next->dl_prev = req->dl_prev;
		
		if (tp->syn_wait_queue == req)
		{
			tp->syn_wait_queue = req->dl_next;
		}
	}

	req->dl_prev = req->dl_next = NULL;
}

extern __inline__ void tcp_synq_queue(struct tcp_opt *tp, struct open_request *req)
{
	if (!tp->syn_wait_queue)
	{
		req->dl_next = req;
		req->dl_prev = req;
		tp->syn_wait_queue = req;
	}
	else
	{
		struct open_request *list = tp->syn_wait_queue;
		
		req->dl_next = list;
		req->dl_prev = list->dl_prev;
		list->dl_prev->dl_next = req;
		list->dl_prev = req;
	}

}

extern void __tcp_inc_slow_timer(struct tcp_sl_timer *slt);
extern __inline__ void tcp_inc_slow_timer(int timer)
{
	struct tcp_sl_timer *slt = &tcp_slt_array[timer];
	
	if (slt->count == 0)
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

#endif	/* _TCP_H */

