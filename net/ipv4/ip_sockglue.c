/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		The IP to API glue.
 *		
 * Version:	$Id: ip_sockglue.c,v 1.37 1998/08/26 12:03:57 davem Exp $
 *
 * Authors:	see ip.c
 *
 * Fixes:
 *		Many		:	Split from ip.c , see ip.c for history.
 *		Martin Mares	:	TOS setting fixed.
 *		Alan Cox	:	Fixed a couple of oopses in Martin's 
 *					TOS tweaks.
 *		Mike McLagan	:	Routing by source
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
#include <net/tcp.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/igmp.h>
#include <linux/firewall.h>
#include <linux/ip_fw.h>
#include <linux/route.h>
#include <linux/mroute.h>
#include <net/route.h>
#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
#include <net/transp_v6.h>
#endif

#include <asm/uaccess.h>

#define MAX(a,b) ((a)>(b)?(a):(b))

#define IP_CMSG_PKTINFO		1
#define IP_CMSG_TTL		2
#define IP_CMSG_TOS		4
#define IP_CMSG_RECVOPTS	8
#define IP_CMSG_RETOPTS		16

/*
 *	SOL_IP control messages.
 */

static void ip_cmsg_recv_pktinfo(struct msghdr *msg, struct sk_buff *skb)
{
	struct in_pktinfo info;
	struct rtable *rt = (struct rtable *)skb->dst;

	info.ipi_addr.s_addr = skb->nh.iph->daddr;
	if (rt) {
		info.ipi_ifindex = rt->rt_iif;
		info.ipi_spec_dst.s_addr = rt->rt_spec_dst;
	} else {
		info.ipi_ifindex = 0;
		info.ipi_spec_dst.s_addr = 0;
	}

	put_cmsg(msg, SOL_IP, IP_PKTINFO, sizeof(info), &info);
}

static void ip_cmsg_recv_ttl(struct msghdr *msg, struct sk_buff *skb)
{
	put_cmsg(msg, SOL_IP, IP_TTL, 1, &skb->nh.iph->ttl);
}

static void ip_cmsg_recv_tos(struct msghdr *msg, struct sk_buff *skb)
{
	put_cmsg(msg, SOL_IP, IP_TOS, 1, &skb->nh.iph->tos);
}

static void ip_cmsg_recv_opts(struct msghdr *msg, struct sk_buff *skb)
{
	if (IPCB(skb)->opt.optlen == 0)
		return;

	put_cmsg(msg, SOL_IP, IP_RECVOPTS, IPCB(skb)->opt.optlen, skb->nh.iph+1);
}


void ip_cmsg_recv_retopts(struct msghdr *msg, struct sk_buff *skb)
{
	unsigned char optbuf[sizeof(struct ip_options) + 40];
	struct ip_options * opt = (struct ip_options*)optbuf;

	if (IPCB(skb)->opt.optlen == 0)
		return;

	if (ip_options_echo(opt, skb)) {
		msg->msg_flags |= MSG_CTRUNC;
		return;
	}
	ip_options_undo(opt);

	put_cmsg(msg, SOL_IP, IP_RETOPTS, opt->optlen, opt->__data);
}


void ip_cmsg_recv(struct msghdr *msg, struct sk_buff *skb)
{
	unsigned flags = skb->sk->ip_cmsg_flags;

	/* Ordered by supposed usage frequency */
	if (flags & 1)
		ip_cmsg_recv_pktinfo(msg, skb);
	if ((flags>>=1) == 0)
		return;

	if (flags & 1)
		ip_cmsg_recv_ttl(msg, skb);
	if ((flags>>=1) == 0)
		return;

	if (flags & 1)
		ip_cmsg_recv_tos(msg, skb);
	if ((flags>>=1) == 0)
		return;

	if (flags & 1)
		ip_cmsg_recv_opts(msg, skb);
	if ((flags>>=1) == 0)
		return;

	if (flags & 1)
		ip_cmsg_recv_retopts(msg, skb);
}

int ip_cmsg_send(struct msghdr *msg, struct ipcm_cookie *ipc)
{
	int err;
	struct cmsghdr *cmsg;

	for (cmsg = CMSG_FIRSTHDR(msg); cmsg; cmsg = CMSG_NXTHDR(msg, cmsg)) {
		if ((unsigned long)(((char*)cmsg - (char*)msg->msg_control)
				    + cmsg->cmsg_len) > msg->msg_controllen) {
			return -EINVAL;
		}
		if (cmsg->cmsg_level != SOL_IP)
			continue;
		switch (cmsg->cmsg_type) {
		case IP_RETOPTS:
			err = cmsg->cmsg_len - CMSG_ALIGN(sizeof(struct cmsghdr));
			err = ip_options_get(&ipc->opt, CMSG_DATA(cmsg), err < 40 ? err : 40, 0);
			if (err)
				return err;
			break;
		case IP_PKTINFO:
		{
			struct in_pktinfo *info;
			if (cmsg->cmsg_len != CMSG_LEN(sizeof(struct in_pktinfo)))
				return -EINVAL;
			info = (struct in_pktinfo *)CMSG_DATA(cmsg);
			ipc->oif = info->ipi_ifindex;
			ipc->addr = info->ipi_spec_dst.s_addr;
			break;
		}
		default:
			return -EINVAL;
		}
	}
	return 0;
}


/* Special input handler for packets catched by router alert option.
   They are selected only by protocol field, and then processed likely
   local ones; but only if someone wants them! Otherwise, router
   not running rsvpd will kill RSVP.

   It is user level problem, what it will make with them.
   I have no idea, how it will masquearde or NAT them (it is joke, joke :-)),
   but receiver should be enough clever f.e. to forward mtrace requests,
   sent to multicast group to reach destination designated router.
 */
struct ip_ra_chain *ip_ra_chain;

int ip_ra_control(struct sock *sk, unsigned char on, void (*destructor)(struct sock *))
{
	struct ip_ra_chain *ra, *new_ra, **rap;

	if (sk->type != SOCK_RAW || sk->num == IPPROTO_RAW)
		return -EINVAL;

	new_ra = on ? kmalloc(sizeof(*new_ra), GFP_KERNEL) : NULL;

	for (rap = &ip_ra_chain; (ra=*rap) != NULL; rap = &ra->next) {
		if (ra->sk == sk) {
			if (on) {
				if (new_ra)
					kfree(new_ra);
				return -EADDRINUSE;
			}
			*rap = ra->next;
			if (ra->destructor)
				ra->destructor(sk);
			kfree(ra);
			return 0;
		}
	}
	if (new_ra == NULL)
		return -ENOBUFS;
	new_ra->sk = sk;
	new_ra->destructor = destructor;
	start_bh_atomic();
	new_ra->next = ra;
	*rap = new_ra;
	end_bh_atomic();
	return 0;
}

/*
 *	Socket option code for IP. This is the end of the line after any TCP,UDP etc options on
 *	an IP socket.
 *
 *	We implement IP_TOS (type of service), IP_TTL (time to live).
 */

int ip_setsockopt(struct sock *sk, int level, int optname, char *optval, int optlen)
{
	int val=0,err;
#if defined(CONFIG_IP_FIREWALL)
	char tmp_fw[MAX(sizeof(struct ip_fwtest),sizeof(struct ip_fwnew))];
#endif
#ifdef CONFIG_IP_MASQUERADE
	char masq_ctl[IP_FW_MASQCTL_MAX];
#endif

	if(optlen>=sizeof(int)) {
		if(get_user(val, (int *) optval))
			return -EFAULT;
	} else if(optlen>=sizeof(char)) {
		unsigned char ucval;
		if(get_user(ucval, (unsigned char *) optval))
			return -EFAULT;
		val = (int)ucval;
	}
	/* If optlen==0, it is equivalent to val == 0 */
	
	if(level!=SOL_IP)
		return -ENOPROTOOPT;
#ifdef CONFIG_IP_MROUTE
	if(optname>=MRT_BASE && optname <=MRT_BASE+10)
	{
		return ip_mroute_setsockopt(sk,optname,optval,optlen);
	}
#endif
	
	switch(optname)
	{
		case IP_OPTIONS:
		{
			struct ip_options * opt = NULL;
			if (optlen > 40 || optlen < 0)
				return -EINVAL;
			err = ip_options_get(&opt, optval, optlen, 1);
			if (err)
				return err;
			start_bh_atomic();
			if (sk->type == SOCK_STREAM) {
				struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;
#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
				if (sk->family == PF_INET ||
				    ((tcp_connected(sk->state) || sk->state == TCP_SYN_SENT)
				     && sk->daddr != LOOPBACK4_IPV6)) {
#endif
					if (opt)
						tp->ext_header_len = opt->optlen;
					tcp_sync_mss(sk, tp->pmtu_cookie);
#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
				}
#endif
			}
			opt = xchg(&sk->opt, opt);
			end_bh_atomic();
			if (opt)
				kfree_s(opt, sizeof(struct ip_options) + opt->optlen);
			return 0;
		}
		case IP_PKTINFO:
			if (val)
				sk->ip_cmsg_flags |= IP_CMSG_PKTINFO;
			else
				sk->ip_cmsg_flags &= ~IP_CMSG_PKTINFO;
			return 0;
		case IP_RECVTTL:
			if (val)
				sk->ip_cmsg_flags |=  IP_CMSG_TTL;
			else
				sk->ip_cmsg_flags &= ~IP_CMSG_TTL;
			return 0;
		case IP_RECVTOS:
			if (val)
				sk->ip_cmsg_flags |=  IP_CMSG_TOS;
			else
				sk->ip_cmsg_flags &= ~IP_CMSG_TOS;
			return 0;
		case IP_RECVOPTS:
			if (val)
				sk->ip_cmsg_flags |=  IP_CMSG_RECVOPTS;
			else
				sk->ip_cmsg_flags &= ~IP_CMSG_RECVOPTS;
			return 0;
		case IP_RETOPTS:
			if (val)
				sk->ip_cmsg_flags |= IP_CMSG_RETOPTS;
			else
				sk->ip_cmsg_flags &= ~IP_CMSG_RETOPTS;
			return 0;
		case IP_TOS:	/* This sets both TOS and Precedence */
			  /* Reject setting of unused bits */
			if (val & ~(IPTOS_TOS_MASK|IPTOS_PREC_MASK))
				return -EINVAL;
			if (IPTOS_PREC(val) >= IPTOS_PREC_CRITIC_ECP && 
			    !capable(CAP_NET_ADMIN))
				return -EPERM;
			if (sk->ip_tos != val) {
				sk->ip_tos=val;
				sk->priority = rt_tos2priority(val);
				dst_release(xchg(&sk->dst_cache, NULL)); 
			}
			sk->priority = rt_tos2priority(val);
			return 0;
		case IP_TTL:
			if (optlen<1)
				return -EINVAL;
			if(val==-1)
				val = ip_statistics.IpDefaultTTL;
			if(val<1||val>255)
				return -EINVAL;
			sk->ip_ttl=val;
			return 0;
		case IP_HDRINCL:
			if(sk->type!=SOCK_RAW)
				return -ENOPROTOOPT;
			sk->ip_hdrincl=val?1:0;
			return 0;
		case IP_PMTUDISC:
			if (val<0 || val>2)
				return -EINVAL;
			sk->ip_pmtudisc = val;
			return 0;
		case IP_RECVERR:
			if (sk->type==SOCK_STREAM)
				return -ENOPROTOOPT;
			lock_sock(sk);
			if (sk->ip_recverr && !val) {
				struct sk_buff *skb;
				/* Drain queued errors */
				while((skb=skb_dequeue(&sk->error_queue))!=NULL)
					kfree_skb(skb);
			}
			sk->ip_recverr = val?1:0;
			release_sock(sk);
			return 0;
		case IP_MULTICAST_TTL: 
			if (optlen<1)
				return -EINVAL;
			if (val==-1)
				val = 1;
			if (val < 0 || val > 255)
				return -EINVAL;
			sk->ip_mc_ttl=val;
	                return 0;
		case IP_MULTICAST_LOOP: 
			if (optlen<1)
				return -EINVAL;
			sk->ip_mc_loop = val ? 1 : 0;
			return 0;
		case IP_MULTICAST_IF: 
		{
			struct ip_mreqn mreq;
			struct device *dev = NULL;
			
			/*
			 *	Check the arguments are allowable
			 */

			if (optlen >= sizeof(struct ip_mreqn)) {
				if (copy_from_user(&mreq,optval,sizeof(mreq)))
					return -EFAULT;
			} else {
				memset(&mreq, 0, sizeof(mreq));
				if (optlen >= sizeof(struct in_addr) &&
				    copy_from_user(&mreq.imr_address,optval,sizeof(struct in_addr)))
					return -EFAULT;
			}

			if (!mreq.imr_ifindex) {
				if (mreq.imr_address.s_addr == INADDR_ANY) {
					sk->ip_mc_index = 0;
					sk->ip_mc_addr  = 0;
					return 0;
				}
				dev = ip_dev_find(mreq.imr_address.s_addr);
			} else
				dev = dev_get_by_index(mreq.imr_ifindex);

			if (!dev)
				return -EADDRNOTAVAIL;

			if (sk->bound_dev_if && dev->ifindex != sk->bound_dev_if)
				return -EINVAL;

			sk->ip_mc_index = mreq.imr_ifindex;
			sk->ip_mc_addr  = mreq.imr_address.s_addr;
			return 0;
		}

		case IP_ADD_MEMBERSHIP:
		case IP_DROP_MEMBERSHIP: 
		{
			struct ip_mreqn mreq;
			
			if (optlen < sizeof(struct ip_mreq))
				return -EINVAL;
			if (optlen >= sizeof(struct ip_mreqn)) {
				if(copy_from_user(&mreq,optval,sizeof(mreq)))
					return -EFAULT;
			} else {
				memset(&mreq, 0, sizeof(mreq));
				if (copy_from_user(&mreq,optval,sizeof(struct ip_mreq)))
					return -EFAULT; 
			}

			if (optname == IP_ADD_MEMBERSHIP)
				return ip_mc_join_group(sk,&mreq);
			else
				return ip_mc_leave_group(sk,&mreq);
		}
		case IP_ROUTER_ALERT:	
			return ip_ra_control(sk, val ? 1 : 0, NULL);
		
#ifdef CONFIG_IP_FIREWALL
		case IP_FW_MASQ_TIMEOUTS:
		case IP_FW_APPEND:
		case IP_FW_REPLACE:
		case IP_FW_DELETE:
		case IP_FW_DELETE_NUM:
		case IP_FW_INSERT:
		case IP_FW_FLUSH:
		case IP_FW_ZERO:
		case IP_FW_CHECK:
		case IP_FW_CREATECHAIN:
		case IP_FW_DELETECHAIN:
		case IP_FW_POLICY:
			if(!capable(CAP_NET_ADMIN))
				return -EACCES;
			if(optlen>sizeof(tmp_fw) || optlen<1)
				return -EINVAL;
			if(copy_from_user(&tmp_fw,optval,optlen))
				return -EFAULT;
			err=ip_fw_ctl(optname, &tmp_fw,optlen);
			return -err;	/* -0 is 0 after all */
#endif /* CONFIG_IP_FIREWALL */
#ifdef CONFIG_IP_MASQUERADE
		case IP_FW_MASQ_ADD:
		case IP_FW_MASQ_DEL:
		case IP_FW_MASQ_FLUSH:
			if(!capable(CAP_NET_ADMIN))
				return -EPERM;
			if(optlen>sizeof(masq_ctl) || optlen<1)
				return -EINVAL;
			if(copy_from_user(masq_ctl,optval,optlen))
				return -EFAULT;
			err=ip_masq_ctl(optname, masq_ctl,optlen);
			return -err;	/* -0 is 0 after all */
			
#endif
		default:
			return(-ENOPROTOOPT);
	}
}

/*
 *	Get the options. Note for future reference. The GET of IP options gets the
 *	_received_ ones. The set sets the _sent_ ones.
 */

int ip_getsockopt(struct sock *sk, int level, int optname, char *optval, int *optlen)
{
	int val,err;
	int len;
	
	if(level!=SOL_IP)
		return -EOPNOTSUPP;

#ifdef CONFIG_IP_MROUTE
	if(optname>=MRT_BASE && optname <=MRT_BASE+10)
	{
		return ip_mroute_getsockopt(sk,optname,optval,optlen);
	}
#endif

	if(get_user(len,optlen))
		return -EFAULT;

	switch(optname)
	{
		case IP_OPTIONS:
			{
				unsigned char optbuf[sizeof(struct ip_options)+40];
				struct ip_options * opt = (struct ip_options*)optbuf;
				start_bh_atomic();
				opt->optlen = 0;
				if (sk->opt)
					memcpy(optbuf, sk->opt, sizeof(struct ip_options)+sk->opt->optlen);
				end_bh_atomic();
				if (opt->optlen == 0) 
					return put_user(0, optlen);

				ip_options_undo(opt);

				len=min(len, opt->optlen);
				if(put_user(len, optlen))
					return -EFAULT;
				if(copy_to_user(optval, opt->__data, len))
					return -EFAULT;
				return 0;
			}
		case IP_PKTINFO:
			val = (sk->ip_cmsg_flags & IP_CMSG_PKTINFO) != 0;
			break;
		case IP_RECVTTL:
			val = (sk->ip_cmsg_flags & IP_CMSG_TTL) != 0;
			break;
		case IP_RECVTOS:
			val = (sk->ip_cmsg_flags & IP_CMSG_TOS) != 0;
			break;
		case IP_RECVOPTS:
			val = (sk->ip_cmsg_flags & IP_CMSG_RECVOPTS) != 0;
			break;
		case IP_RETOPTS:
			val = (sk->ip_cmsg_flags & IP_CMSG_RETOPTS) != 0;
			break;
		case IP_TOS:
			val=sk->ip_tos;
			break;
		case IP_TTL:
			val=sk->ip_ttl;
			break;
		case IP_HDRINCL:
			val=sk->ip_hdrincl;
			break;
		case IP_PMTUDISC:
			val=sk->ip_pmtudisc;
			break;
		case IP_RECVERR:
			val=sk->ip_recverr;
			break;
		case IP_MULTICAST_TTL:
			val=sk->ip_mc_ttl;
			break;
		case IP_MULTICAST_LOOP:
			val=sk->ip_mc_loop;
			break;
#if 0
		case IP_MULTICAST_IF:
		{
			struct ip_mreqn mreq;
			len = min(len,sizeof(struct ip_mreqn));
  			if(put_user(len, optlen))
  				return -EFAULT;
			mreq.imr_ifindex = sk->ip_mc_index;
			mreq.imr_address.s_addr = sk->ip_mc_addr;
			mreq.imr_multiaddr.s_addr = 0;
			if(copy_to_user((void *)optval, &mreq, len))
				return -EFAULT;
			return 0;
		}
#endif
		case IP_MULTICAST_IF:
		{
			struct device *dev = dev_get_by_index(sk->ip_mc_index);

			printk(KERN_INFO "application %s uses old get IP_MULTICAST_IF. Please, report!\n", current->comm);

			if (dev == NULL) 
			{
				len = 0;
				return put_user(len, optlen);
			}
			dev_lock_list();
  			len = min(len,strlen(dev->name));
  			err = put_user(len, optlen);
			if (!err)
			{
				err = copy_to_user((void *)optval,dev->name, len);
				if(err)
					err=-EFAULT;
			}
			dev_unlock_list();
			return err;
		}
		default:
			return(-ENOPROTOOPT);
	}
	
	if (len < sizeof(int) && len > 0 && val>=0 && val<255) {
		unsigned char ucval = (unsigned char)val;
		len = 1;
		if(put_user(len, optlen))
			return -EFAULT;
		if(copy_to_user(optval,&ucval,1))
			return -EFAULT;
	} else {
		len=min(sizeof(int),len);
		if(put_user(len, optlen))
			return -EFAULT;
		if(copy_to_user(optval,&val,len))
			return -EFAULT;
	}
	return 0;
}
