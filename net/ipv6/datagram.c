/*
 *	common UDP/RAW code
 *	Linux INET6 implementation 
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *
 *	$Id: datagram.c,v 1.3 1996/10/11 16:03:05 roque Exp $
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/in6.h>
#include <linux/ipv6.h>

#include <net/ipv6.h>
#include <net/ndisc.h>
#include <net/ipv6_route.h>
#include <net/addrconf.h>
#include <net/transp_v6.h>


int datagram_recv_ctl(struct sock *sk, struct msghdr *msg, struct sk_buff *skb)
{
	struct ipv6_pinfo *np = &sk->net_pinfo.af_inet6;
	struct ipv6_options *opt = (struct ipv6_options *) skb->proto_priv;
	struct cmsghdr *cmsg = msg->msg_control;
	int len = msg->msg_controllen;

	msg->msg_controllen = 0;
	
	if (np->rxinfo && (len >= sizeof(struct cmsghdr) +
			   sizeof(struct in6_pktinfo)))
	{
		struct in6_pktinfo *src_info;
		struct inet6_dev *in6_dev;

		cmsg->cmsg_len = (sizeof(struct cmsghdr) + 
				  sizeof(struct in6_pktinfo));
		cmsg->cmsg_level = SOL_IPV6;
		cmsg->cmsg_type = IPV6_RXINFO;

		src_info = (struct in6_pktinfo *) cmsg->cmsg_data;
		in6_dev = ipv6_get_idev(skb->dev);

		if (in6_dev == NULL)
		{
			printk(KERN_DEBUG "recv_ctl: unknown device\n");
			return -ENODEV;
		}

		src_info->ipi6_ifindex = in6_dev->if_index;
		ipv6_addr_copy(&src_info->ipi6_addr,
			       &skb->ipv6_hdr->daddr);

		len -= cmsg->cmsg_len;
		msg->msg_controllen += cmsg->cmsg_len;
		cmsg = (struct cmsghdr *)((u8*) cmsg + cmsg->cmsg_len);
	}

	if (opt->srcrt)
	{
		int hdrlen = sizeof(struct rt0_hdr) + (opt->srcrt->hdrlen << 3);

		if (len >= sizeof(struct cmsghdr) + hdrlen)
		{
			struct rt0_hdr *rt0;

			cmsg->cmsg_len = sizeof(struct cmsghdr) + hdrlen;
			cmsg->cmsg_level = SOL_IPV6;
			cmsg->cmsg_type = IPV6_RXINFO;
		
			rt0 = (struct rt0_hdr *) cmsg->cmsg_data;
			memcpy(rt0, opt->srcrt, hdrlen);

			len -= cmsg->cmsg_len;
			msg->msg_controllen += cmsg->cmsg_len;
			cmsg = (struct cmsghdr *)((u8*) cmsg + cmsg->cmsg_len);
		}
	}
	return 0;
}
			 

int datagram_send_ctl(struct msghdr *msg, struct device **src_dev,
		      struct in6_addr **src_addr, struct ipv6_options *opt)
{
	struct inet6_dev *in6_dev = NULL;
	struct in6_pktinfo *src_info;
	struct cmsghdr *cmsg;
	struct ipv6_rt_hdr *rthdr;
	int len;
	int err = -EINVAL;

	for (cmsg = msg->msg_control; cmsg; cmsg = cmsg_nxthdr(msg, cmsg))
	{
		if (cmsg->cmsg_level != SOL_IPV6)
		{
			printk(KERN_DEBUG "cmsg_level %d\n", cmsg->cmsg_level);
			continue;
		}

		switch (cmsg->cmsg_type) {

		case IPV6_TXINFO:
			if (cmsg->cmsg_len < (sizeof(struct cmsghdr) +
					      sizeof(struct in6_pktinfo)))
			{
				goto exit_f;
			}

			src_info = (struct in6_pktinfo *) cmsg->cmsg_data;
			
			if (src_info->ipi6_ifindex)
			{
				in6_dev = ipv6_dev_by_index(src_info->ipi6_ifindex);
				if (in6_dev == NULL)
				{
					goto exit_f;
				}

				*src_dev = in6_dev->dev;
			}
			
			if (!ipv6_addr_any(&src_info->ipi6_addr))
			{
				struct inet6_ifaddr *ifp;

				ifp = ipv6_chk_addr(&src_info->ipi6_addr);

				if ( ifp == NULL)
				{
					goto exit_f;
				}

				*src_addr = &src_info->ipi6_addr;
				err = 0;
			}

			break;
			
		case SCM_SRCRT:

			len = cmsg->cmsg_len;

			len -= sizeof(struct cmsghdr);

			/* validate option length */
			if (len < sizeof(struct ipv6_rt_hdr))
			{
				goto exit_f;
			}

			rthdr = (struct ipv6_rt_hdr *) cmsg->cmsg_data;

			/*
			 *	TYPE 0
			 */
			if (rthdr->type)
			{
				goto exit_f;
			}

			if (((rthdr->hdrlen + 1) << 3) < len)
			{				
				goto exit_f;
			}

			/* segments left must also match */
			if ((rthdr->hdrlen >> 1) != rthdr->segments_left)
			{
				goto exit_f;
			}
			
			opt->opt_nflen += ((rthdr->hdrlen + 1) << 3);
			opt->srcrt = rthdr;
			err = 0;

			break;
		default:
			printk(KERN_DEBUG "invalid cmsg type: %d\n",
			       cmsg->cmsg_type);
			break;
		}
	}

  exit_f:
	return err;
}
