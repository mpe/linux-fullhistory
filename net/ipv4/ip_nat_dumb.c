/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Dumb Network Address Translation.
 *
 * Version:	$Id: ip_nat_dumb.c,v 1.3 1998/03/15 03:31:44 davem Exp $
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Fixes:
 *		Rani Assaf	:	A zero checksum is a special case
 *					only in UDP
 *
 * NOTE:	It is just working model of real NAT.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/icmp.h>
#include <linux/netdevice.h>
#include <net/sock.h>
#include <net/ip.h>
#include <net/icmp.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/firewall.h>
#include <linux/ip_fw.h>
#ifdef CONFIG_IP_MASQUERADE
#include <net/ip_masq.h>
#endif
#include <net/checksum.h>
#include <linux/route.h>
#include <net/route.h>


int
ip_do_nat(struct sk_buff *skb)
{
	struct rtable *rt = (struct rtable*)skb->dst;
	struct iphdr *iph = skb->nh.iph;
	u32 odaddr = iph->daddr;
	u32 osaddr = iph->saddr;
	u16	check;

	IPCB(skb)->flags |= IPSKB_TRANSLATED;

	/* Rewrite IP header */
	iph->daddr = rt->rt_dst_map;
	iph->saddr = rt->rt_src_map;
	iph->check = 0;
	iph->check = ip_fast_csum((unsigned char *)iph, iph->ihl);

	/* If it is the first fragment, rewrite protocol headers */

	if (!(iph->frag_off & htons(IP_OFFSET))) {
		u16	*cksum;

		switch(iph->protocol) {
		case IPPROTO_TCP:
			cksum  = (u16*)&((struct tcphdr*)(((char*)iph) + iph->ihl*4))->check;
			check  = csum_tcpudp_magic(iph->saddr, iph->daddr, 0, 0, ~(*cksum));
			*cksum = csum_tcpudp_magic(~osaddr, ~odaddr, 0, 0, ~check);
			break;
		case IPPROTO_UDP:
			cksum  = (u16*)&((struct udphdr*)(((char*)iph) + iph->ihl*4))->check;
			if ((check = *cksum) != 0) {
				check = csum_tcpudp_magic(iph->saddr, iph->daddr, 0, 0, ~check);
				check = csum_tcpudp_magic(~osaddr, ~odaddr, 0, 0, ~check);
				*cksum = check ? : 0xFFFF;
			}
		default:
			break;
		}
	}
	return 0;
}
