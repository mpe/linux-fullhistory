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
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _SOCK_H
#define _SOCK_H


#define SOCK_ARRAY_SIZE	64


/*
 * This structure really needs to be cleaned up.
 * Most of it is for TCP, and not used by any of
 * the other protocols.
 */
struct sock {
  struct options		*opt;
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
				timeout,
				destroy,
				ack_timed,
				no_check,
				exp_growth;
  int				proc;
  struct sock			*next;
  struct sock			*pair;
  struct sk_buff		*send_tail;
  struct sk_buff		*send_head;
  struct sk_buff		*volatile back_log;
  struct sk_buff		*send_tmp;
  long				retransmits;
  struct sk_buff		*wback,
				*wfront,
				*rqueue;
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
  struct tcphdr			dummy_th;
  struct timer			time_wait;
};

struct proto {
  void			*(*wmalloc)(struct sock *sk,
				    unsigned long size, int force,
				    int priority);
  void			*(*rmalloc)(struct sock *sk,
				    unsigned long size, int force,
				    int priority);
  void			(*wfree)(struct sock *sk, void *mem,
				 unsigned long size);
  void			(*rfree)(struct sock *sk, void *mem,
				 unsigned long size);
  unsigned long		(*rspace)(struct sock *sk);
  unsigned long		(*wspace)(struct sock *sk);
  void			(*close)(struct sock *sk, int timeout);
  int			(*read)(struct sock *sk, unsigned char *to,
				int len, int nonblock, unsigned flags);
  int			(*write)(struct sock *sk, unsigned char *to,
				 int len, int nonblock, unsigned flags);
  int			(*sendto)(struct sock *sk,
				  unsigned char *from, int len, int noblock,
				  unsigned flags, struct sockaddr_in *usin,
				  int addr_len);
  int			(*recvfrom)(struct sock *sk,
				    unsigned char *from, int len, int noblock,
				    unsigned flags, struct sockaddr_in *usin,
				    int *addr_len);
  int			(*build_header)(struct sk_buff *skb,
					unsigned long saddr,
					unsigned long daddr,
					struct device **dev, int type,
					struct options *opt, int len);
  int			(*connect)(struct sock *sk,
				  struct sockaddr_in *usin, int addr_len);
  struct sock		*(*accept) (struct sock *sk, int flags);
  void			(*queue_xmit)(struct sock *sk,
				      struct device *dev, struct sk_buff *skb,
				      int free);
  void			(*retransmit)(struct sock *sk, int all);
  void			(*write_wakeup)(struct sock *sk);
  void			(*read_wakeup)(struct sock *sk);
  int			(*rcv)(struct sk_buff *buff, struct device *dev,
			       struct options *opt, unsigned long daddr,
			       unsigned short len, unsigned long saddr,
			       int redo, struct inet_protocol *protocol);
  int			(*select)(struct sock *sk, int which,
				  select_table *wait);
  int			(*ioctl)(struct sock *sk, int cmd,
				 unsigned long arg);
  int			(*init)(struct sock *sk);
  void			(*shutdown)(struct sock *sk, int how);
  unsigned short	max_header;
  unsigned long		retransmits;
  struct sock		*sock_array[SOCK_ARRAY_SIZE];
  char			name[80];
};

#define TIME_WRITE	1
#define TIME_CLOSE	2
#define TIME_KEEPOPEN	3
#define TIME_DESTROY	4
#define TIME_DONE	5	/* used to absorb those last few packets */
#define SOCK_DESTROY_TIME 1000	/* about 10 seconds			*/


#define PROT_SOCK	1024
#define SHUTDOWN_MASK	3
#define RCV_SHUTDOWN	1
#define SEND_SHUTDOWN	2


extern void			destroy_sock(struct sock *sk);
extern unsigned short		get_new_socknum(struct proto *, unsigned short);
extern void			put_sock(unsigned short, struct sock *); 
extern void			release_sock(struct sock *sk);
extern struct sock		*get_sock(struct proto *, unsigned short,
					  unsigned long, unsigned short,
					  unsigned long);
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

#endif	/* _SOCK_H */
