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

#include <linux/tcp.h>

#define MAX_SYN_SIZE	44 + sizeof (struct sk_buff) + MAX_HEADER
#define MAX_FIN_SIZE	40 + sizeof (struct sk_buff) + MAX_HEADER
#define MAX_ACK_SIZE	40 + sizeof (struct sk_buff) + MAX_HEADER
#define MAX_RESET_SIZE	40 + sizeof (struct sk_buff) + MAX_HEADER
#define MAX_WINDOW	4096
#define MIN_WINDOW	2048
#define MAX_ACK_BACKLOG	2
#define MIN_WRITE_SPACE	2048
#define TCP_WINDOW_DIFF	2048

#define TCP_RETR1	7	/*
				 * This is howmany retries it does before it
				 * tries to figure out if the gateway is
				 * down.
				 */

#define TCP_RETR2	15	/*
				 * This should take at least
				 * 90 minutes to time out.
				 */

#define TCP_TIMEOUT_LEN	720000	/* should be about 2 hrs		*/
#define TCP_TIMEWAIT_LEN 1000	/* how long to wait to sucessfully 
				 * close the socket, about 60 seconds	*/
#define TCP_ACK_TIME	30000	/* time to delay before sending an ACK	*/
#define TCP_DONE_TIME	250	/* maximum time to wait before actually
				 * destroying a socket			*/
#define TCP_WRITE_TIME	30000	/* initial time to wait for an ACK,
			         * after last transmit			*/
#define TCP_CONNECT_TIME 2000	/* time to retransmit first SYN		*/
#define TCP_SYN_RETRIES	5	/* number of times to retry openning a
				 * connection 				*/
#define TCP_PROBEWAIT_LEN 100	/* time to wait between probes when
				 * I've got something to write and
				 * there is no window			*/

#define TCP_NO_CHECK	0	/* turn to one if you want the default
				 * to be no checksum			*/

#define TCP_WRITE_QUEUE_MAGIC 0xa5f23477

/*
 * The next routines deal with comparing 32 bit unsigned ints
 * and worry about wraparound. The general strategy is to do a
 * normal compare so long as neither of the numbers is within
 * 4K of wrapping.  Otherwise we must check for the wrap.
 */
static inline int
before (unsigned long seq1, unsigned long seq2)
{
  /* this inequality is strict. */
  if (seq1 == seq2) return(0);

  if (seq1 < seq2) {
	if ((unsigned long)seq2-(unsigned long)seq1 < 65536UL) {
		return(1);
	} else {
		return(0);
	}
  }

  /*
   * Now we know seq1 > seq2.  So all we need to do is check
   * to see if seq1 has wrapped.
   */
  if (seq2 < 8192UL && seq1 > (0xffffffffUL - 8192UL)) {
	return(1);
  }
  return(0);
}


static inline int
after(unsigned long seq1, unsigned long seq2)
{
  return(before(seq2, seq1));
}


/* is s2<=s1<=s3 ? */
static inline int
between(unsigned long seq1, unsigned long seq2, unsigned long seq3)
{
  return(after(seq1+1, seq2) && before(seq1, seq3+1));
}


/*
 * List all states of a TCP socket that can be viewed as a "connected"
 * state.  This now includes TCP_SYN_RECV, although I am not yet fully
 * convinced that this is the solution for the 'getpeername(2)'
 * problem. Thanks to Stephen A. Wood <saw@cebaf.gov>  -FvK
 */
static inline const int
tcp_connected(const int state)
{
  return(state == TCP_ESTABLISHED || state == TCP_CLOSE_WAIT ||
	 state == TCP_FIN_WAIT1   || state == TCP_FIN_WAIT2 ||
	 state == TCP_SYN_RECV);
}


extern struct proto tcp_prot;


extern void	print_th(struct tcphdr *);
extern void	tcp_err(int err, unsigned char *header, unsigned long daddr,
			unsigned long saddr, struct inet_protocol *protocol);
extern void	tcp_shutdown (struct sock *sk, int how);
extern int	tcp_rcv(struct sk_buff *skb, struct device *dev,
			struct options *opt, unsigned long daddr,
			unsigned short len, unsigned long saddr, int redo,
			struct inet_protocol *protocol);

extern int	tcp_ioctl(struct sock *sk, int cmd, unsigned long arg);


#endif	/* _TCP_H */
