#ifndef _INET_ECN_H_
#define _INET_ECN_H_

#include <linux/ip.h>
#include <net/dsfield.h>

enum {
	INET_ECN_NOT_ECT = 0,
	INET_ECN_ECT_1 = 1,
	INET_ECN_ECT_0 = 2,
	INET_ECN_CE = 3,
	INET_ECN_MASK = 3,
};

static inline int INET_ECN_is_ce(__u8 dsfield)
{
	return (dsfield & INET_ECN_MASK) == INET_ECN_CE;
}

static inline int INET_ECN_is_not_ect(__u8 dsfield)
{
	return (dsfield & INET_ECN_MASK) == INET_ECN_NOT_ECT;
}

static inline int INET_ECN_is_capable(__u8 dsfield)
{
	return (dsfield & INET_ECN_ECT_0);
}

static inline __u8 INET_ECN_encapsulate(__u8 outer, __u8 inner)
{
	outer &= ~INET_ECN_MASK;
	outer |= !INET_ECN_is_ce(inner) ? (inner & INET_ECN_MASK) :
					  INET_ECN_ECT_0;
	return outer;
}

#define	INET_ECN_xmit(sk) do { inet_sk(sk)->tos |= INET_ECN_ECT_0; } while (0)
#define	INET_ECN_dontxmit(sk) \
	do { inet_sk(sk)->tos &= ~INET_ECN_MASK; } while (0)

#define IP6_ECN_flow_init(label) do {		\
      (label) &= ~htonl(INET_ECN_MASK << 20);	\
    } while (0)

#define	IP6_ECN_flow_xmit(sk, label) do {				\
	if (INET_ECN_is_capable(inet_sk(sk)->tos))			\
		(label) |= __constant_htons(INET_ECN_ECT_0 << 4);	\
    } while (0)

static inline void IP_ECN_set_ce(struct iphdr *iph)
{
	u32 check = iph->check;
	u32 ecn = (iph->tos + 1) & INET_ECN_MASK;

	/*
	 * After the last operation we have (in binary):
	 * INET_ECN_NOT_ECT => 01
	 * INET_ECN_ECT_1   => 10
	 * INET_ECN_ECT_0   => 11
	 * INET_ECN_CE      => 00
	 */
	if (!(ecn & 2))
		return;

	/*
	 * The following gives us:
	 * INET_ECN_ECT_1 => check += htons(0xFFFD)
	 * INET_ECN_ECT_0 => check += htons(0xFFFE)
	 */
	check += htons(0xFFFB) + htons(ecn);

	iph->check = check + (check>=0xFFFF);
	iph->tos |= INET_ECN_CE;
}

static inline void IP_ECN_clear(struct iphdr *iph)
{
	iph->tos &= ~INET_ECN_MASK;
}

static inline void ipv4_copy_dscp(struct iphdr *outer, struct iphdr *inner)
{
	u32 dscp = ipv4_get_dsfield(outer) & ~INET_ECN_MASK;
	ipv4_change_dsfield(inner, INET_ECN_MASK, dscp);
}

struct ipv6hdr;

static inline void IP6_ECN_set_ce(struct ipv6hdr *iph)
{
	if (INET_ECN_is_not_ect(ipv6_get_dsfield(iph)))
		return;
	*(u32*)iph |= htonl(INET_ECN_CE << 20);
}

static inline void IP6_ECN_clear(struct ipv6hdr *iph)
{
	*(u32*)iph &= ~htonl(INET_ECN_MASK << 20);
}

static inline void ipv6_copy_dscp(struct ipv6hdr *outer, struct ipv6hdr *inner)
{
	u32 dscp = ipv6_get_dsfield(outer) & ~INET_ECN_MASK;
	ipv6_change_dsfield(inner, INET_ECN_MASK, dscp);
}

#endif
