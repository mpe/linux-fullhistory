/*
 *	common UDP/RAW code
 *	Linux INET6 implementation 
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *
 *	$Id: datagram.c,v 1.12 1997/05/15 18:55:09 davem Exp $
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/in6.h>
#include <linux/ipv6.h>
#include <linux/route.h>

#include <net/ipv6.h>
#include <net/ndisc.h>
#include <net/addrconf.h>
#include <net/transp_v6.h>

int datagram_recv_ctl(struct sock *sk, struct msghdr *msg, struct sk_buff *skb)
{
	struct ipv6_pinfo *np = &sk->net_pinfo.af_inet6;
	struct ipv6_options *opt = (struct ipv6_options *) skb->cb;
	
	if (np->rxinfo) {
		struct in6_pktinfo src_info;

		src_info.ipi6_ifindex = skb->dev->ifindex;
		ipv6_addr_copy(&src_info.ipi6_addr, &skb->nh.ipv6h->daddr);
		put_cmsg(msg, SOL_IPV6, IPV6_PKTINFO, sizeof(src_info), &src_info);
	}

	if (np->rxhlim) {
		int hlim = skb->nh.ipv6h->hop_limit;
		put_cmsg(msg, SOL_IPV6, IPV6_HOPLIMIT, sizeof(hlim), &hlim);
	}

	if (opt->srcrt) {
		int hdrlen = sizeof(struct rt0_hdr) + (opt->srcrt->hdrlen << 3);

		put_cmsg(msg, SOL_IPV6, IPV6_RXSRCRT, hdrlen, opt->srcrt);
	}
	return 0;
}

int datagram_send_ctl(struct msghdr *msg, struct device **src_dev,
		      struct in6_addr **src_addr, struct ipv6_options *opt, 
		      int *hlimit)
{
	struct in6_pktinfo *src_info;
	struct cmsghdr *cmsg;
	struct ipv6_rt_hdr *rthdr;
	int len;
	int err = 0;

	for (cmsg = CMSG_FIRSTHDR(msg); cmsg; cmsg = CMSG_NXTHDR(msg, cmsg)) {
		if (cmsg->cmsg_level != SOL_IPV6) {
			printk(KERN_DEBUG "invalid cmsg_level %d\n", cmsg->cmsg_level);
			continue;
		}

		switch (cmsg->cmsg_type) {
 		case IPV6_PKTINFO:
 			if (cmsg->cmsg_len != CMSG_LEN(sizeof(struct in6_pktinfo))) {
				err = -EINVAL;
				goto exit_f;
			}

			src_info = (struct in6_pktinfo *)CMSG_DATA(cmsg);
			
			if (src_info->ipi6_ifindex) {
				int index = src_info->ipi6_ifindex;

				*src_dev = dev_get_by_index(index);
			}
			
			if (!ipv6_addr_any(&src_info->ipi6_addr)) {
				struct inet6_ifaddr *ifp;

				ifp = ipv6_chk_addr(&src_info->ipi6_addr);

				if (ifp == NULL) {
					err = -EINVAL;
					goto exit_f;
				}

				*src_addr = &src_info->ipi6_addr;
			}

			break;
			
		case IPV6_RXSRCRT:
                        if (cmsg->cmsg_len < CMSG_LEN(sizeof(struct ipv6_rt_hdr))) {
				err = -EINVAL;
				goto exit_f;
			}

			len = cmsg->cmsg_len - sizeof(struct cmsghdr);
			rthdr = (struct ipv6_rt_hdr *)CMSG_DATA(cmsg);

			/*
			 *	TYPE 0
			 */
			if (rthdr->type) {
				err = -EINVAL;
				goto exit_f;
			}

			if (((rthdr->hdrlen + 1) << 3) < len) {
				err = -EINVAL;
				goto exit_f;
			}

			/* segments left must also match */
			if ((rthdr->hdrlen >> 1) != rthdr->segments_left) {
				err = -EINVAL;
				goto exit_f;
			}
			
			opt->opt_nflen += ((rthdr->hdrlen + 1) << 3);
			opt->srcrt = rthdr;

			break;
			
		case IPV6_HOPLIMIT:
			if (cmsg->cmsg_len != CMSG_LEN(sizeof(int))) {
				err = -EINVAL;
				goto exit_f;
			}

			*hlimit = *(int *)CMSG_DATA(cmsg);
			break;

		default:
			printk(KERN_DEBUG "invalid cmsg type: %d\n", cmsg->cmsg_type);
			err = -EINVAL;
			break;
		};
	}

exit_f:
	return err;
}
