/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the 'struct sk_buff' memory handlers.
 *
 * Version:	@(#)skbuff.h	1.0.4	05/20/93
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
#ifndef _SKBUFF_H
#define _SKBUFF_H


#define FREE_READ	1
#define FREE_WRITE	0


struct sk_buff {
  struct sk_buff		*next;
  struct sk_buff		*prev;
  struct sk_buff		*link3;
  struct sock			*sk;
  volatile unsigned long	when;	/* used to compute rtt's	*/
  struct device			*dev;
  void				*mem_addr;
  union {
	struct tcphdr	*th;
	struct ethhdr	*eth;
	struct iphdr	*iph;
	struct udphdr	*uh;
	struct arphdr	*arp;
	unsigned char	*raw;
	unsigned long	seq;
  } h;
  unsigned long			mem_len;
  unsigned long 		len;
  unsigned long 		saddr;
  unsigned long 		daddr;
  int				magic;
  volatile char 		acked,
				used,
				free,
				arp,
				urg_used,
				lock;
  unsigned char			tries;
};

#define SK_WMEM_MAX	8192
#define SK_RMEM_MAX	32767


extern void			print_skb(struct sk_buff *);
extern void			kfree_skb(struct sk_buff *skb, int rw);
extern void			lock_skb(struct sk_buff *skb);
extern void			unlock_skb(struct sk_buff *skb, int rw);

#endif	/* _SKBUFF_H */
