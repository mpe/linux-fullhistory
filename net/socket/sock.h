/*
 *		Definitions for the socket handler
 *
 * Version:	@(#)sock.h	1.28	26/12/93
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
 *		Alan Cox	:	Split into sock.h and sockinet.h
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _SOCK_H
#define _SOCK_H

#include <linux/timer.h>
#include <linux/ip.h>		/* struct options */
#include <linux/tcp.h>		/* struct tcphdr */

#include "skbuff.h"		/* struct sk_buff */
#ifdef CONFIG_AX25
#include "ax25/ax25.h"
#endif
#ifdef CONFIG_IPX
#include "ipx/ipx.h"
#endif

#define SOCK_ARRAY_SIZE	64


/*
 * This structure really needs to be cleaned up.
 * Most of it is for TCP, and not used by any of
 * the other protocols.
 */
struct sock {
  struct options		*opt;
  struct options		*rcv_opt;
  volatile unsigned long	wmem_alloc;
  volatile unsigned long	rmem_alloc;
  unsigned long			send_seq;
  unsigned long			acked_seq;
  unsigned long			copied_seq;
  unsigned long			rcv_ack_seq;
  unsigned long			window_seq;
  unsigned long			fin_seq;

  /*
   * Not all are volatile, but some are, so we
   * might as well say they all are.
   */
  volatile char                 inuse,
				dead,
				urginline,
				intr,
				blog,
				done,
				reuse,
				keepopen,
				linger,
				delay_acks,
				destroy,
				ack_timed,
				no_check,
				exp_growth,
				zapped,	/* In ax25 & ipx means not linked */
				broadcast;
  unsigned long		        lingertime;
  int				proc;
  struct sock			*next;
  struct sock			*pair;
  struct sk_buff		*volatile send_tail;
  struct sk_buff		*volatile send_head;
  struct sk_buff		*volatile back_log;
  struct sk_buff		*send_tmp;
  long				retransmits;
  struct sk_buff		*volatile wback,
				*volatile wfront,
				*volatile rqueue;
  struct proto			*prot;
  struct wait_queue		**sleep;
  unsigned long			daddr;
  unsigned long			saddr;
  unsigned short		max_unacked;
  unsigned short		window;
  unsigned short		bytes_rcv;
  unsigned short		mtu;
  unsigned short		num;
  volatile unsigned short	cong_window;
  volatile unsigned short	packets_out;
  volatile unsigned short	urg;
  volatile unsigned short	shutdown;
  unsigned short		mss;
  volatile unsigned long	rtt;
  volatile unsigned long	mdev;
  volatile unsigned short	backoff;
  volatile short		err;
  unsigned char			protocol;
  volatile unsigned char	state;
  volatile unsigned char	ack_backlog;
  unsigned char			max_ack_backlog;
  unsigned char			priority;
  unsigned char			debug;
  unsigned short		rcvbuf;
  unsigned short		sndbuf;
  unsigned short		type;
#ifdef CONFIG_IPX
  ipx_address			ipx_source_addr,ipx_dest_addr;
  unsigned short		ipx_type;
#endif
#ifdef CONFIG_AX25
/* Really we want to add a per protocol private area */
  ax25_address			ax25_source_addr,ax25_dest_addr;
  struct sk_buff *volatile	ax25_retxq[8];
  char				ax25_state,ax25_vs,ax25_vr,ax25_lastrxnr,ax25_lasttxnr;
  char				ax25_condition;
  char				ax25_retxcnt;
  char				ax25_xx;
  char				ax25_retxqi;
  char				ax25_rrtimer;
  char				ax25_timer;
#endif  
/* IP 'private area' or will be eventually */
  int				ip_ttl;		/* TTL setting */
  int				ip_tos;		/* TOS */
  
  struct tcphdr			dummy_th;

  /* This part is used for the timeout functions (timer.c). */
  int				timeout;	/* What are we waiting for? */
  struct timer_list		timer;

  /* identd */
  struct socket			*socket;
  
  /* Event callbacks */
  
  void				(*state_change)(struct sock *sk);
  void				(*data_ready)(struct sock *sk,int bytes);
  void				(*write_space)(struct sock *sk);
  void				(*error_report)(struct sock *sk);
};


#define TIME_WRITE	1
#define TIME_CLOSE	2
#define TIME_KEEPOPEN	3
#define TIME_DESTROY	4
#define TIME_DONE	5	/* used to absorb those last few packets */
#define SOCK_DESTROY_TIME 1000	/* about 10 seconds			*/

#define PROT_SOCK	1024	/* Sockets 0-1023 can't be bound too unless you are superuser */

#define SHUTDOWN_MASK	3
#define RCV_SHUTDOWN	1
#define SEND_SHUTDOWN	2

extern void			print_sk(struct sock *);
extern void			*sock_wmalloc(struct sock *sk,
					      unsigned long size, int force,
					      int priority);
extern void			*sock_rmalloc(struct sock *sk,
					      unsigned long size, int force,
					      int priority);
extern void			sock_wfree(struct sock *sk, void *mem,
					   unsigned long size);
extern void			sock_rfree(struct sock *sk, void *mem,
					   unsigned long size);
extern unsigned long		sock_rspace(struct sock *sk);
extern unsigned long		sock_wspace(struct sock *sk);

extern int 			sock_setsockopt(struct sock *sk, int level, int optname, char *optval,
						int optlen);
extern int			sock_getsockopt(struct sock *sk, int level, int optname, char *optval,
						int *optlen);						

#endif	/* _SOCK_H */
