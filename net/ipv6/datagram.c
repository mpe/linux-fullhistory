/*
 *	common UDP/RAW code
 *	Linux INET6 implementation 
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *
 *	$Id: datagram.c,v 1.15 1998/08/26 12:04:47 davem Exp $
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
	struct inet6_skb_parm *opt = (struct inet6_skb_parm *) skb->cb;

	if (np->rxopt.bits.rxinfo) {
		struct in6_pktinfo src_info;

		src_info.ipi6_ifindex = opt->iif;
		ipv6_addr_copy(&src_info.ipi6_addr, &skb->nh.ipv6h->daddr);
		put_cmsg(msg, SOL_IPV6, IPV6_PKTINFO, sizeof(src_info), &src_info);
	}

	if (np->rxopt.bits.rxhlim) {
		int hlim = skb->nh.ipv6h->hop_limit;
		put_cmsg(msg, SOL_IPV6, IPV6_HOPLIMIT, sizeof(hlim), &hlim);
	}

	if (np->rxopt.bits.hopopts && opt->hop) {
		u8 *ptr = skb->nh.raw + opt->hop;
		put_cmsg(msg, SOL_IPV6, IPV6_HOPOPTS, (ptr[1]+1)<<3, ptr);
	}
	if (np->rxopt.bits.dstopts && opt->dst0) {
		u8 *ptr = skb->nh.raw + opt->dst0;
		put_cmsg(msg, SOL_IPV6, IPV6_DSTOPTS, (ptr[1]+1)<<3, ptr);
	}
	if (np->rxopt.bits.srcrt && opt->srcrt) {
		struct ipv6_rt_hdr *rthdr = (struct ipv6_rt_hdr *)(skb->nh.raw + opt->srcrt);
		put_cmsg(msg, SOL_IPV6, IPV6_RTHDR, (rthdr->hdrlen+1) << 3, rthdr);
	}
	if (np->rxopt.bits.authhdr && opt->auth) {
		u8 *ptr = skb->nh.raw + opt->auth;
		put_cmsg(msg, SOL_IPV6, IPV6_AUTHHDR, (ptr[1]+1)<<2, ptr);
	}
	if (np->rxopt.bits.dstopts && opt->dst1) {
		u8 *ptr = skb->nh.raw + opt->dst1;
		put_cmsg(msg, SOL_IPV6, IPV6_DSTOPTS, (ptr[1]+1)<<3, ptr);
	}
	return 0;
}

int datagram_send_ctl(struct msghdr *msg, int *oif,
		      struct in6_addr **src_addr, struct ipv6_txoptions *opt,
		      int *hlimit)
{
	struct in6_pktinfo *src_info;
	struct cmsghdr *cmsg;
	struct ipv6_rt_hdr *rthdr;
	struct ipv6_opt_hdr *hdr;
	int len;
	int err = 0;

	for (cmsg = CMSG_FIRSTHDR(msg); cmsg; cmsg = CMSG_NXTHDR(msg, cmsg)) {

		if ((unsigned long)(((char*)cmsg - (char*)msg->msg_control)
				    + cmsg->cmsg_len) > msg->msg_controllen) {
			err = -EINVAL;
			goto exit_f;
		}

		if (cmsg->cmsg_level != SOL_IPV6) {
			if (net_ratelimit())
				printk(KERN_DEBUG "invalid cmsg_level %d\n", cmsg->cmsg_level);
			continue;
		}

		switch (cmsg->cmsg_type) {
 		case IPV6_PKTINFO:
 			if (cmsg->cmsg_len < CMSG_LEN(sizeof(struct in6_pktinfo))) {
				err = -EINVAL;
				goto exit_f;
			}

			src_info = (struct in6_pktinfo *)CMSG_DATA(cmsg);
			
			if (src_info->ipi6_ifindex) {
				if (*oif && src_info->ipi6_ifindex != *oif)
					return -EINVAL;
				*oif = src_info->ipi6_ifindex;
			}

			if (!ipv6_addr_any(&src_info->ipi6_addr)) {
				struct inet6_ifaddr *ifp;

				ifp = ipv6_chk_addr(&src_info->ipi6_addr, NULL, 0);

				if (ifp == NULL) {
					err = -EINVAL;
					goto exit_f;
				}

				*src_addr = &src_info->ipi6_addr;
			}

			break;

		case IPV6_HOPOPTS:
                        if (opt->hopopt || cmsg->cmsg_len < CMSG_LEN(sizeof(struct ipv6_opt_hdr))) {
				err = -EINVAL;
				goto exit_f;
			}

			hdr = (struct ipv6_opt_hdr *)CMSG_DATA(cmsg);
			len = ((hdr->hdrlen + 1) << 3);
			if (cmsg->cmsg_len < CMSG_LEN(len)) {
				err = -EINVAL;
				goto exit_f;
			}
			if (!capable(CAP_NET_RAW)) {
				err = -EPERM;
				goto exit_f;
			}
			opt->opt_nflen += len;
			opt->hopopt = hdr;
			break;

		case IPV6_DSTOPTS:
                        if (cmsg->cmsg_len < CMSG_LEN(sizeof(struct ipv6_opt_hdr))) {
				err = -EINVAL;
				goto exit_f;
			}

			hdr = (struct ipv6_opt_hdr *)CMSG_DATA(cmsg);
			len = ((hdr->hdrlen + 1) << 3);
			if (cmsg->cmsg_len < CMSG_LEN(len)) {
				err = -EINVAL;
				goto exit_f;
			}
			if (!capable(CAP_NET_RAW)) {
				err = -EPERM;
				goto exit_f;
			}
			if (opt->dst1opt) {
				err = -EINVAL;
				goto exit_f;
			}
			opt->opt_flen += len;
			opt->dst1opt = hdr;
			break;

		case IPV6_AUTHHDR:
                        if (cmsg->cmsg_len < CMSG_LEN(sizeof(struct ipv6_opt_hdr))) {
				err = -EINVAL;
				goto exit_f;
			}

			hdr = (struct ipv6_opt_hdr *)CMSG_DATA(cmsg);
			len = ((hdr->hdrlen + 2) << 2);
			if (cmsg->cmsg_len < CMSG_LEN(len)) {
				err = -EINVAL;
				goto exit_f;
			}
			if (len & ~7) {
				err = -EINVAL;
				goto exit_f;
			}
			opt->opt_flen += len;
			opt->auth = hdr;
			break;

		case IPV6_RTHDR:
                        if (cmsg->cmsg_len < CMSG_LEN(sizeof(struct ipv6_rt_hdr))) {
				err = -EINVAL;
				goto exit_f;
			}

			rthdr = (struct ipv6_rt_hdr *)CMSG_DATA(cmsg);

			/*
			 *	TYPE 0
			 */
			if (rthdr->type) {
				err = -EINVAL;
				goto exit_f;
			}

			len = ((rthdr->hdrlen + 1) << 3);

                        if (cmsg->cmsg_len < CMSG_LEN(len)) {
				err = -EINVAL;
				goto exit_f;
			}

			/* segments left must also match */
			if ((rthdr->hdrlen >> 1) != rthdr->segments_left) {
				err = -EINVAL;
				goto exit_f;
			}

			opt->opt_nflen += len;
			opt->srcrt = rthdr;

			if (opt->dst1opt) {
				int dsthdrlen = ((opt->dst1opt->hdrlen+1)<<3);

				opt->opt_nflen += dsthdrlen;
				opt->dst0opt = opt->dst1opt;
				opt->dst1opt = NULL;
				opt->opt_flen -= dsthdrlen;
			}

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
