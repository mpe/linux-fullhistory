/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		The IP to API glue.
 *		
 * Authors:	see ip.c
 *
 * Fixes:
 *		Many		:	Split from ip.c , see ip.c for history.
 *		Martin Mares	:	TOS setting fixed.
 *		Alan Cox	:	Fixed a couple of oopses in Martin's 
 *					TOS tweaks.
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
#include <net/checksum.h>
#include <linux/route.h>
#include <linux/mroute.h>
#include <net/route.h>

#include <asm/uaccess.h>

/*
 *	SOL_IP control messages.
 */

static void ip_cmsg_recv_rxinfo(struct msghdr *msg, struct sk_buff *skb)
{
	struct in_pktinfo info;
	struct rtable *rt = (struct rtable *)skb->dst;

	info.ipi_ifindex = skb->dev->ifindex;
	info.ipi_addr.s_addr = skb->nh.iph->daddr;
	info.ipi_spec_dst.s_addr = rt->rt_spec_dst;

	put_cmsg(msg, SOL_IP, IP_RXINFO, sizeof(info), &info);
}

static void ip_cmsg_recv_localaddr(struct msghdr *msg, struct sk_buff *skb, int local)
{
	struct in_addr addr;

	addr.s_addr = skb->nh.iph->daddr;

	if (local) {
		struct rtable *rt = (struct rtable *)skb->dst;
		addr.s_addr = rt->rt_spec_dst;
	}
	put_cmsg(msg, SOL_IP, local ? IP_LOCALADDR : IP_RECVDSTADDR,
		 sizeof(addr), &addr);
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
		ip_cmsg_recv_rxinfo(msg, skb);
	if ((flags>>=1) == 0)
		return;
	if (flags & 1)
		ip_cmsg_recv_localaddr(msg, skb, 1);
	if ((flags>>=1) == 0)
		return;
	if (flags & 1)
		ip_cmsg_recv_opts(msg, skb);
	if ((flags>>=1) == 0)
		return;
	if (flags & 1)
		ip_cmsg_recv_retopts(msg, skb);
	if ((flags>>=1) == 0)
		return;
	if (flags & 1)
		ip_cmsg_recv_localaddr(msg, skb, 0);
}

int ip_cmsg_send(struct msghdr *msg, struct ipcm_cookie *ipc, struct device **devp)
{
	int err;
	struct cmsghdr *cmsg;

	for (cmsg = CMSG_FIRSTHDR(msg); cmsg; cmsg = CMSG_NXTHDR(msg, cmsg)) {
		if (cmsg->cmsg_level != SOL_IP)
			continue;
		switch (cmsg->cmsg_type)
		{
		case IP_LOCALADDR:
			if (cmsg->cmsg_len < sizeof(struct in_addr)+sizeof(*cmsg))
				return -EINVAL;
			memcpy(&ipc->addr, cmsg->cmsg_data, 4);
			break;
		case IP_RETOPTS:
			err = cmsg->cmsg_len - sizeof(*cmsg);
			err = ip_options_get(&ipc->opt, cmsg->cmsg_data,
					     err < 40 ? err : 40, 0);
			if (err)
				return err;
			break;
		case IP_TXINFO:
		{
			struct in_pktinfo *info;
			if (cmsg->cmsg_len < sizeof(*info)+sizeof(*cmsg))
				return -EINVAL;
			info = (struct in_pktinfo*)cmsg->cmsg_data;
			if (info->ipi_ifindex && !devp)
				return -EINVAL;
			if ((*devp = dev_get_by_index(info->ipi_ifindex)) == NULL)
				return -ENODEV;
			ipc->addr = info->ipi_spec_dst.s_addr;
			break;
		}
		default:
			return -EINVAL;
		}
	}
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
	unsigned char ucval = 0;
#if defined(CONFIG_IP_FIREWALL) || defined(CONFIG_IP_ACCT)
	struct ip_fw tmp_fw;
#endif	

	if(optlen>=sizeof(int)) {
		if(get_user(val, (int *) optval))
			return -EFAULT;
	} else if(optlen>=sizeof(char)) {
		if(get_user(ucval, (unsigned char *) optval))
			return -EFAULT;
	}
	
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
			struct ip_options * old_opt;
			if (optlen > 40 || optlen < 0)
				return -EINVAL;
			err = ip_options_get(&opt, optval, optlen, 1);
			if (err)
				return err;
			/*
			 * ANK: I'm afraid that receive handler may change
			 * options from under us.
			 */
			cli();
			old_opt = sk->opt;
			sk->opt = opt;
			sti();
			if (old_opt)
				kfree_s(old_opt, sizeof(struct optlen) + old_opt->optlen);
			return 0;
		}
		case IP_RXINFO:
			if (optlen<4)
				return -EINVAL;
			if (val)
				sk->ip_cmsg_flags |= 1;
			else
				sk->ip_cmsg_flags &= ~1;
			return 0;
		case IP_LOCALADDR:
			if (optlen<4)
				return -EINVAL;
			if (val)
				sk->ip_cmsg_flags |= 2;
			else
				sk->ip_cmsg_flags &= ~2;
			return 0;
		case IP_RECVOPTS:
			if (optlen<4)
				return -EINVAL;
			if (val)
				sk->ip_cmsg_flags |= 4;
			else
				sk->ip_cmsg_flags &= ~4;
			return 0;
		case IP_RETOPTS:
			if (optlen<4)
				return -EINVAL;
			if (val)
				sk->ip_cmsg_flags |= 8;
			else
				sk->ip_cmsg_flags &= ~8;
			return 0;
		case IP_RECVDSTADDR:
			if (optlen<4)
				return -EINVAL;
			if (val)
				sk->ip_cmsg_flags |= 0x10;
			else
				sk->ip_cmsg_flags &= ~0x10;
			return 0;
		case IP_TOS:	/* This sets both TOS and Precedence */
			  /* Reject setting of unused bits */
			if (optlen<4)
				return -EINVAL;
			if (val & ~(IPTOS_TOS_MASK|IPTOS_PREC_MASK))
				return -EINVAL;
			if (IPTOS_PREC(val) >= IPTOS_PREC_CRITIC_ECP && !suser())
				return -EPERM;
			sk->ip_tos=val;
			sk->priority = rt_tos2priority(val);
			return 0;
		case IP_TTL:
			if (optlen<4)
				return -EINVAL;
			if(val<1||val>255)
				return -EINVAL;
			sk->ip_ttl=val;
			return 0;
		case IP_HDRINCL:
			if (optlen<4)
				return -EINVAL;
			if(sk->type!=SOCK_RAW)
				return -ENOPROTOOPT;
			sk->ip_hdrincl=val?1:0;
			return 0;
		case IP_PMTUDISC:
			if (optlen<4)
				return -EINVAL;
			if (val<0 || val>2)
				return -EINVAL;
			sk->ip_pmtudisc = val;
			return 0;
		case IP_RECVERR:
			if (optlen<4)
				return -EINVAL;
			if (sk->type==SOCK_STREAM)
				return -ENOPROTOOPT;
			lock_sock(sk);
			if (sk->ip_recverr && !val) {
				struct sk_buff *skb;
				/* Drain queued errors */
				while((skb=skb_dequeue(&sk->error_queue))!=NULL) {
					IS_SKB(skb);
					kfree_skb(skb, FREE_READ);
				}
			}
			sk->ip_recverr = val?1:0;
			release_sock(sk);
			return 0;
		case IP_MULTICAST_TTL: 
			if (optlen<1)
				return -EINVAL;
			sk->ip_mc_ttl=(int)ucval;
	                return 0;
		case IP_MULTICAST_LOOP: 
			if (optlen<1)
				return -EINVAL;
			if(ucval!=0 && ucval!=1)
				 return -EINVAL;
			sk->ip_mc_loop=(int)ucval;
			return 0;
		case IP_MULTICAST_IF: 
		{
			struct in_addr addr;
			struct device *dev = NULL;
			
			/*
			 *	Check the arguments are allowable
			 */

			if(optlen<sizeof(addr))
				return -EINVAL;
				
			if(copy_from_user(&addr,optval,sizeof(addr)))
				return -EFAULT; 

			
			
			/*
			 *	What address has been requested
			 */
			
			if (addr.s_addr==INADDR_ANY)	/* Default */
			{
				sk->ip_mc_index = 0;
				return 0;
			}
			
			/*
			 *	Find the device
			 */
			 
			dev=ip_dev_find(addr.s_addr, NULL);
						
			/*
			 *	Did we find one
			 */
			 
			if(dev) 
			{
				sk->ip_mc_index = dev->ifindex;
				return 0;
			}
			return -EADDRNOTAVAIL;
		}

		
		case IP_ADD_MEMBERSHIP: 
		{
		
/*
 *	FIXME: Add/Del membership should have a semaphore protecting them from re-entry
 */
			struct ip_mreq mreq;
			struct rtable *rt;
			struct device *dev=NULL;
			
			/*
			 *	Check the arguments.
			 */
			 
			if(optlen<sizeof(mreq))
				return -EINVAL;
			if(copy_from_user(&mreq,optval,sizeof(mreq)))
				return -EFAULT; 

			/* 
			 *	Get device for use later
			 */

			if (mreq.imr_interface.s_addr==INADDR_ANY) {
				err = ip_route_output(&rt, mreq.imr_multiaddr.s_addr, 0, 1, NULL);
				if (err)
					return err;
				dev = rt->u.dst.dev;
				ip_rt_put(rt);
			} else
				dev = ip_dev_find(mreq.imr_interface.s_addr, NULL);
			
			/*
			 *	No device, no cookies.
			 */
			 
			if(!dev)
				return -ENODEV;
				
			/*
			 *	Join group.
			 */
			 
			return ip_mc_join_group(sk,dev,mreq.imr_multiaddr.s_addr);
		}
		
		case IP_DROP_MEMBERSHIP: 
		{
			struct ip_mreq mreq;
			struct rtable *rt;
			struct device *dev=NULL;

			/*
			 *	Check the arguments
			 */
			 
			if(optlen<sizeof(mreq))
				return -EINVAL;
			if(copy_from_user(&mreq,optval,sizeof(mreq)))
				return -EFAULT; 

			/*
			 *	Get device for use later 
			 */
 
			if (mreq.imr_interface.s_addr==INADDR_ANY) {
				err = ip_route_output(&rt, mreq.imr_multiaddr.s_addr, 0, 1, NULL);
				if (err)
					return err;
				dev = rt->u.dst.dev;
				ip_rt_put(rt);
			} else
				dev = ip_dev_find(mreq.imr_interface.s_addr, NULL);
			
			/*
			 *	Did we find a suitable device.
			 */
			 
			if(!dev)
				return -ENODEV;
				
			/*
			 *	Leave group
			 */
			 
			return ip_mc_leave_group(sk,dev,mreq.imr_multiaddr.s_addr);
		}

		case IP_MULTICAST_IFN:
		{
			struct ip_mreqn mreq;
			struct device *dev = NULL;
			
			if(optlen<sizeof(mreq))
				return -EINVAL;
			if(copy_from_user(&mreq,optval,sizeof(mreq)))
				return -EFAULT;

			if (!mreq.imr_ifindex) {
				if (!mreq.imr_address.s_addr) {
					sk->ip_mc_index = 0;
					sk->ip_mc_addr  = 0;
					return 0;
				}
				dev = ip_dev_find(mreq.imr_address.s_addr, NULL);
			} else
				dev = dev_get_by_index(mreq.imr_ifindex);

			if (!dev)
				return -ENODEV;

			sk->ip_mc_index = mreq.imr_ifindex;
			sk->ip_mc_addr  = mreq.imr_address.s_addr;
			return 0;
		}
		case IP_ADD_MEMBERSHIPN:
		{
			struct ip_mreqn mreq;
			struct device *dev = NULL;

			if(optlen<sizeof(mreq))
				return -EINVAL;			
			if(copy_from_user(&mreq,optval,sizeof(mreq)))
				return -EFAULT; 
			dev = dev_get_by_index(mreq.imr_ifindex);
			if (!dev)
				return -ENODEV;
			return ip_mc_join_group(sk,dev,mreq.imr_multiaddr.s_addr);
		}
		
		case IP_DROP_MEMBERSHIPN: 
		{
			struct ip_mreqn mreq;
			struct device *dev=NULL;

			/*
			 *	Check the arguments
			 */

			if(optlen<sizeof(mreq))
				return -EINVAL;			 
			if(copy_from_user(&mreq,optval,sizeof(mreq)))
				return -EFAULT; 

			dev=dev_get_by_index(mreq.imr_ifindex);
			if(!dev)
				return -ENODEV;
			 
			return ip_mc_leave_group(sk,dev,mreq.imr_multiaddr.s_addr);
		}
#ifdef CONFIG_IP_FIREWALL
		case IP_FW_INSERT_IN:
		case IP_FW_INSERT_OUT:
		case IP_FW_INSERT_FWD:
		case IP_FW_APPEND_IN:
		case IP_FW_APPEND_OUT:
		case IP_FW_APPEND_FWD:
		case IP_FW_DELETE_IN:
		case IP_FW_DELETE_OUT:
		case IP_FW_DELETE_FWD:
		case IP_FW_CHECK_IN:
		case IP_FW_CHECK_OUT:
		case IP_FW_CHECK_FWD:
		case IP_FW_FLUSH_IN:
		case IP_FW_FLUSH_OUT:
		case IP_FW_FLUSH_FWD:
		case IP_FW_ZERO_IN:
		case IP_FW_ZERO_OUT:
		case IP_FW_ZERO_FWD:
		case IP_FW_POLICY_IN:
		case IP_FW_POLICY_OUT:
		case IP_FW_POLICY_FWD:
		case IP_FW_MASQ_TIMEOUTS:
			if(!suser())
				return -EACCES;
			if(optlen>sizeof(tmp_fw) || optlen<1)
				return -EINVAL;
			if(copy_from_user(&tmp_fw,optval,optlen))
				return -EFAULT;
			err=ip_fw_ctl(optname, &tmp_fw,optlen);
			return -err;	/* -0 is 0 after all */
			
#endif
#ifdef CONFIG_IP_ACCT
		case IP_ACCT_INSERT:
		case IP_ACCT_APPEND:
		case IP_ACCT_DELETE:
		case IP_ACCT_FLUSH:
		case IP_ACCT_ZERO:
			if(!suser())
				return -EACCES;
			if(optlen>sizeof(tmp_fw) || optlen<1)
				return -EINVAL;
			if(copy_from_user(&tmp_fw, optval,optlen))
				return -EFAULT; 
			err=ip_acct_ctl(optname, &tmp_fw,optlen);
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
				cli();
				opt->optlen = 0;
				if (sk->opt)
					memcpy(optbuf, sk->opt, sizeof(struct ip_options)+sk->opt->optlen);
				sti();
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
		case IP_RXINFO:
			val = (sk->ip_cmsg_flags & 1) != 0;
			return 0;
		case IP_LOCALADDR:
			val = (sk->ip_cmsg_flags & 2) != 0;
			return 0;
		case IP_RECVOPTS:
			val = (sk->ip_cmsg_flags & 4) != 0;
			return 0;
		case IP_RETOPTS:
			val = (sk->ip_cmsg_flags & 8) != 0;
			return 0;
		case IP_RECVDSTADDR:
			val = (sk->ip_cmsg_flags & 0x10) != 0;
			return 0;
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
			return 0;
		case IP_RECVERR:
			val=sk->ip_recverr;
			return 0;
		case IP_MULTICAST_TTL:
			val=sk->ip_mc_ttl;
			break;
		case IP_MULTICAST_LOOP:
			val=sk->ip_mc_loop;
			break;
		case IP_MULTICAST_IFN:
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
		case IP_MULTICAST_IF:
		{
			struct device *dev = dev_get_by_index(sk->ip_mc_index);
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
	
	len=min(sizeof(int),len);
	
	if(put_user(len, optlen))
		return -EFAULT;
	if(copy_to_user(optval,&val,len))
		return -EFAULT;
	return 0;
}
