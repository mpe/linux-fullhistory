#ifndef _NET_RAWV6_H
#define _NET_RAWV6_H

#ifdef __KERNEL__

#define RAWV6_HTABLE_SIZE	MAX_INET_PROTOS
extern struct sock *raw_v6_htable[RAWV6_HTABLE_SIZE];


extern struct sock *raw_v6_lookup(struct sock *sk, unsigned short num,
				  struct in6_addr *loc_addr, struct in6_addr *rmt_addr);

extern int			rawv6_rcv(struct sk_buff *skb, 
					  struct device *dev,
					  struct in6_addr *saddr, 
					  struct in6_addr *daddr,
					  struct ipv6_options *opt, 
					  unsigned short len);


extern void			rawv6_err(struct sock *sk,
					  int type, int code, 
					  unsigned char *buff,
					  struct in6_addr *saddr,
					  struct in6_addr *daddr);

#endif

#endif
