/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		The options processing module for ip.c
 *
 * Authors:	A.N.Kuznetsov
 *		
 */

#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/icmp.h>
#include <linux/netdevice.h>
#include <net/sock.h>
#include <net/ip.h>
#include <net/icmp.h>

/* 
 * Write options to IP header, record destination address to
 * source route option, address of outgoing interface
 * (we should already know it, so that this  function is allowed be
 * called only after routing decision) and timestamp,
 * if we originate this datagram.
 */

void ip_options_build(struct sk_buff * skb, struct options * opt,
			    __u32 daddr, __u32 saddr,
			    int is_frag) 
{
	unsigned char * iph = (unsigned char*)skb->ip_hdr;

	memcpy(skb->proto_priv, opt, sizeof(struct options));
	memcpy(iph+sizeof(struct iphdr), opt->__data, opt->optlen);
	opt = (struct options*)skb->proto_priv;
	opt->is_data = 0;

	if (opt->srr)
		memcpy(iph+opt->srr+iph[opt->srr+1]-4, &daddr, 4);

	if (!is_frag) 
	{
		if (opt->rr_needaddr)
			memcpy(iph+opt->rr+iph[opt->rr+2]-5, &saddr, 4);
		if (opt->ts_needaddr)
			memcpy(iph+opt->ts+iph[opt->ts+2]-9, &saddr, 4);
		if (opt->ts_needtime) 
		{
			struct timeval tv;
			__u32 midtime;
			do_gettimeofday(&tv);
			midtime = htonl((tv.tv_sec % 86400) * 1000 + tv.tv_usec / 1000);
			memcpy(iph+opt->ts+iph[opt->ts+2]-5, &midtime, 4);
		}
		return;
	}
	if (opt->rr) 
	{
		memset(iph+opt->rr, IPOPT_NOP, iph[opt->rr+1]);
		opt->rr = 0;
		opt->rr_needaddr = 0;
	}
	if (opt->ts) 
	{
		memset(iph+opt->ts, IPOPT_NOP, iph[opt->ts+1]);
		opt->ts = 0;
		opt->ts_needaddr = opt->ts_needtime = 0;
	}
}

int ip_options_echo(struct options * dopt, struct options * sopt,
		     __u32 daddr, __u32 saddr,
		     struct sk_buff * skb) 
{
	unsigned char *sptr, *dptr;
	int soffset, doffset;
	int	optlen;

	memset(dopt, 0, sizeof(struct options));

	dopt->is_data = 1;

	if (!sopt)
		sopt = (struct options*)skb->proto_priv;

	if (sopt->optlen == 0) 
	{
		dopt->optlen = 0;
		return 0;
	}

	sptr = (sopt->is_data ? sopt->__data - sizeof(struct iphdr) :
		(unsigned char *)skb->ip_hdr);
	dptr = dopt->__data;

	if (sopt->rr) 
	{
		optlen  = sptr[sopt->rr+1];
		soffset = sptr[sopt->rr+2];
		dopt->rr = dopt->optlen + sizeof(struct iphdr);
		memcpy(dptr, sptr+sopt->rr, optlen);
		if (sopt->rr_needaddr && soffset <= optlen) {
			if (soffset + 3 > optlen)
			  return -EINVAL;
			dptr[2] = soffset + 4;
			dopt->rr_needaddr = 1;
		}
		dptr 	 += optlen;
		dopt->optlen += optlen;
	}
	if (sopt->ts) 
	{
		optlen = sptr[sopt->ts+1];
		soffset = sptr[sopt->ts+2];
		dopt->ts = dopt->optlen + sizeof(struct iphdr);
		memcpy(dptr, sptr+sopt->ts, optlen);
		if (soffset <= optlen) 
		{
			if (sopt->ts_needaddr) 
			{
				if (soffset + 3 > optlen)
					return -EINVAL;
				dopt->ts_needaddr = 1;
				soffset += 4;
			}
			if (sopt->ts_needtime) 
			{
				if (soffset + 3 > optlen)
					return -EINVAL;
				dopt->ts_needtime = 1;
				soffset += 4;
			}
			if (((struct timestamp*)(dptr+1))->flags == IPOPT_TS_PRESPEC) 
			{
				__u32 addr;
				memcpy(&addr, sptr+soffset-9, 4);
				if (ip_chk_addr(addr) == 0) 
				{
					dopt->ts_needtime = 0;
					dopt->ts_needaddr = 0;
					soffset -= 8;
				}
			}
			dptr[2] = soffset;
		}
		dptr += optlen;
		dopt->optlen += optlen;
	}
	if (sopt->srr) 
	{
		unsigned char * start = sptr+sopt->srr;
		__u32 faddr;

		optlen  = start[1];
		soffset = start[2];
		doffset = 0;
		if (soffset > optlen)
			soffset = optlen + 1;
		soffset -= 4;
		if (soffset > 3) 
		{
			memcpy(&faddr, &start[soffset-1], 4);
			for (soffset-=4, doffset=4; soffset > 3; soffset-=4, doffset+=4)
				memcpy(&dptr[doffset-1], &start[soffset-1], 4);
			/*
			 * RFC1812 requires to fix illegal source routes.
			 */
			if (memcmp(&saddr, &start[soffset+3], 4) == 0)
				doffset -= 4;
		}
		if (doffset > 3) 
		{
			memcpy(&start[doffset-1], &daddr, 4);
			dopt->faddr = faddr;
			dptr[0] = start[0];
			dptr[1] = doffset+3;
			dptr[2] = 4;
			dptr += doffset+3;
			dopt->srr = dopt->optlen + sizeof(struct iphdr);
			dopt->optlen += doffset+3;
			dopt->is_strictroute = sopt->is_strictroute;
		}
	}
	while (dopt->optlen & 3) 
	{
		*dptr++ = IPOPT_END;
		dopt->optlen++;
	}
	return 0;
}

void ip_options_fragment(struct sk_buff * skb) 
{
	unsigned char * optptr = (unsigned char*)skb->ip_hdr;
	struct options * opt = (struct options*)skb->proto_priv;
	int  l = opt->optlen;
	int  optlen;

	while (l > 0) 
	{
		switch (*optptr) 
		{
		      case IPOPT_END:
			return;
		      case IPOPT_NOOP:
			l--;
			optptr++;
			continue;
		}
		optlen = optptr[1];
		if (optlen<2 || optlen>l)
		  return;
		if (!(*optptr & 0x80))
			memset(optptr, IPOPT_NOOP, optlen);
		l -= optlen;
		optptr += optlen;
	}
	opt->ts = 0;
	opt->rr = 0;
	opt->rr_needaddr = 0;
	opt->ts_needaddr = 0;
	opt->ts_needtime = 0;
	return;
}

/*
 * Verify options and fill pointers in struct options.
 * Caller should clear *opt, and set opt->data.
 * If opt == NULL, then skb->data should point to IP header.
 */

int ip_options_compile(struct options * opt, struct sk_buff * skb)
{
	int l;
	unsigned char * iph;
	unsigned char * optptr;
	int optlen;
	unsigned char * pp_ptr = NULL;

	if (!opt) 
	{
		opt = (struct options*)skb->proto_priv;
		memset(opt, 0, sizeof(struct options));
		iph = (unsigned char*)skb->ip_hdr;
		opt->optlen = ((struct iphdr *)iph)->ihl*4 - sizeof(struct iphdr);
		optptr = iph + sizeof(struct iphdr);
		opt->is_data = 0;
	}
	else 
	{
		optptr = opt->is_data ? opt->__data : (unsigned char*)&skb->ip_hdr[1];
		iph = optptr - sizeof(struct iphdr);
	}

	for (l = opt->optlen; l > 0; ) 
	{
		switch (*optptr) 
		{
		      case IPOPT_END:
			for (optptr++, l--; l>0; l--) 
			{
				if (*optptr != IPOPT_END) 
				{
					*optptr = IPOPT_END;
					opt->is_changed = 1;
				}
			}
			goto eol;
		      case IPOPT_NOOP:
			l--;
			optptr++;
			continue;
		}
		optlen = optptr[1];
		if (optlen<2 || optlen>l)
		{
			pp_ptr = optptr;
			goto error;
		}
		switch (*optptr) 
		{
		      case IPOPT_SSRR:
		      case IPOPT_LSRR:
			if (optlen < 3) 
			{
				pp_ptr = optptr + 1;
				goto error;
			}
			if (optptr[2] < 4) 
			{
				pp_ptr = optptr + 2;
				goto error;
			}
			/* NB: cf RFC-1812 5.2.4.1 */
			if (opt->srr) 
			{
				pp_ptr = optptr;
				goto error;
			}
			if (!skb) 
			{
				if (optptr[2] != 4 || optlen < 7 || ((optlen-3) & 3)) 
				{
					pp_ptr = optptr + 1;
					goto error;
				}
				memcpy(&opt->faddr, &optptr[3], 4);
				if (optlen > 7)
					memmove(&optptr[3], &optptr[7], optlen-7);
			}
			opt->is_strictroute = (optptr[0] == IPOPT_SSRR);
			opt->srr = optptr - iph;
			break;
		      case IPOPT_RR:
			if (opt->rr) 
			{
				pp_ptr = optptr;
				goto error;
			}
			if (optlen < 3) 
			{
				pp_ptr = optptr + 1;
				goto error;
			}
			if (optptr[2] < 4) 
			{
				pp_ptr = optptr + 2;
				goto error;
			}
			if (optptr[2] <= optlen) 
			{
				if (optptr[2]+3 > optlen) 
				{
					pp_ptr = optptr + 2;
					goto error;
				}
				if (skb) 
				{
					memcpy(&optptr[optptr[2]-1], &skb->dev->pa_addr, 4);
					opt->is_changed = 1;
				}
				optptr[2] += 4;
				opt->rr_needaddr = 1;
			}
			opt->rr = optptr - iph;
			break;
		      case IPOPT_TIMESTAMP:
			if (opt->ts) 
			{
				pp_ptr = optptr;
				goto error;
			}
			if (optlen < 4) 
			{
				pp_ptr = optptr + 1;
				goto error;
			}
			if (optptr[2] < 5) 
			{
				pp_ptr = optptr + 2;
				goto error;
			}
			if (optptr[2] <= optlen) 
			{
				struct timestamp * ts = (struct timestamp*)(optptr+1);
				__u32 * timeptr = NULL;
				if (ts->ptr+3 > ts->len) 
				{
					pp_ptr = optptr + 2;
					goto error;
				}
				switch (ts->flags) 
				{
				      case IPOPT_TS_TSONLY:
					opt->ts = optptr - iph;
					if (skb) 
						timeptr = (__u32*)&optptr[ts->ptr-1];
					opt->ts_needtime = 1;
					ts->ptr += 4;
					break;
				      case IPOPT_TS_TSANDADDR:
					if (ts->ptr+7 > ts->len) 
					{
						pp_ptr = optptr + 2;
						goto error;
					}
					opt->ts = optptr - iph;
					if (skb) 
					{
						memcpy(&optptr[ts->ptr-1], &skb->dev->pa_addr, 4);
						timeptr = (__u32*)&optptr[ts->ptr+3];
					}
					opt->ts_needaddr = 1;
					opt->ts_needtime = 1;
					ts->ptr += 8;
					break;
				      case IPOPT_TS_PRESPEC:
					if (ts->ptr+7 > ts->len) 
					{
						pp_ptr = optptr + 2;
						goto error;
					}
					opt->ts = optptr - iph;
					{
						__u32 addr;
						memcpy(&addr, &optptr[ts->ptr-1], 4);
						if (ip_chk_addr(addr) == 0)
							break;
						if (skb)
							timeptr = (__u32*)&optptr[ts->ptr+3];
					}
					opt->ts_needaddr = 1;
					opt->ts_needtime = 1;
					ts->ptr += 8;
					break;
				      default:
					pp_ptr = optptr + 3;
					goto error;
				}
				if (timeptr) 
				{
					struct timeval tv;
					__u32  midtime;
					do_gettimeofday(&tv);
					midtime = htonl((tv.tv_sec % 86400) * 1000 + tv.tv_usec / 1000);
					memcpy(timeptr, &midtime, sizeof(__u32));
					opt->is_changed = 1;
				}
			} 
			else 
			{
				struct timestamp * ts = (struct timestamp*)(optptr+1);
				if (ts->overflow == 15) 
				{
					pp_ptr = optptr + 3;
					goto error;
				}
				opt->ts = optptr - iph;
				if (skb) 
				{
					ts->overflow++;
					opt->is_changed = 1;
				}
			}
			break;
		      case IPOPT_SEC:
		      case IPOPT_SID:
		      default:
			if (!skb) 
			{
				pp_ptr = optptr;
				goto error;
			}
			break;
		}
		l -= optlen;
		optptr += optlen;
	}

eol:
	if (!pp_ptr)
		return 0;

error:
	if (skb) 
	{
		icmp_send(skb, ICMP_PARAMETERPROB, 0, pp_ptr-iph, skb->dev);
		kfree_skb(skb, FREE_READ);
	}
	return -EINVAL;
}

