/*
 *	IPv6 BSD socket options interface
 *	Linux INET6 implementation 
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *
 *	Based on linux/net/ipv4/ip_sockglue.c
 *
 *	$Id: ipv6_sockglue.c,v 1.19 1998/04/30 16:24:26 freitag Exp $
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 *	FIXME: Make the setsockopt code POSIX compliant: That is
 *
 *	o	Return -EINVAL for setsockopt of short lengths
 *	o	Truncate getsockopt returns
 *	o	Return an optlen of the truncated length if need be
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/sched.h>
#include <linux/net.h>
#include <linux/in6.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/init.h>
#include <linux/sysctl.h>

#include <net/sock.h>
#include <net/snmp.h>
#include <net/ipv6.h>
#include <net/ndisc.h>
#include <net/protocol.h>
#include <net/transp_v6.h>
#include <net/ip6_route.h>
#include <net/addrconf.h>
#include <net/inet_common.h>
#include <net/tcp.h>
#include <net/udp.h>

#include <asm/uaccess.h>

struct ipv6_mib ipv6_statistics={0, };
struct packet_type ipv6_packet_type =
{
	__constant_htons(ETH_P_IPV6), 
	NULL,					/* All devices */
	ipv6_rcv,
	NULL,
	NULL
};

/*
 *	addrconf module should be notifyed of a device going up
 */
static struct notifier_block ipv6_dev_notf = {
	addrconf_notify,
	NULL,
	0
};

int ipv6_setsockopt(struct sock *sk, int level, int optname, char *optval, 
		    int optlen)
{
	struct ipv6_pinfo *np = &sk->net_pinfo.af_inet6;
	int val, err;
	int retv = -ENOPROTOOPT;

	if(level!=SOL_IPV6)
		goto out;

	if (optval == NULL) {
		val=0;
	} else {
		err = get_user(val, (int *) optval);
		if(err)
			return err;
	}
	

	switch (optname) {

	case IPV6_ADDRFORM:
		if (val == PF_INET) {
			if (sk->protocol != IPPROTO_UDP &&
			    sk->protocol != IPPROTO_TCP)
				goto out;
			
			if (sk->state != TCP_ESTABLISHED) {
				retv = ENOTCONN;
				goto out;
			}
			
			if (!(ipv6_addr_type(&np->daddr) & IPV6_ADDR_MAPPED)) {
				retv = -EADDRNOTAVAIL;
				goto out;
			}

			if (sk->protocol == IPPROTO_TCP) {
				struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
				
				sk->prot = &tcp_prot;
				tp->af_specific = &ipv4_specific;
				sk->socket->ops = &inet_stream_ops;
				sk->family = AF_INET;
			} else {
				sk->prot = &udp_prot;
				sk->socket->ops = &inet_dgram_ops;
			}
			retv = 0;
		} else {
			retv = -EINVAL;
		}
		break;

	case IPV6_PKTINFO:
		np->rxinfo = val;
		retv = 0;
		break;

	case IPV6_HOPLIMIT:
		np->rxhlim = val;
		retv = 0;
		break;

	case IPV6_UNICAST_HOPS:
		if (val > 255 || val < -1)
			retv = -EINVAL;
		else {
			np->hop_limit = val;
			retv = 0;
		}
		break;

	case IPV6_MULTICAST_HOPS:
		if (val > 255 || val < -1)
			retv = -EINVAL;
		else {
			np->mcast_hops = val;
			retv = 0;
		}
		break;
		break;

	case IPV6_MULTICAST_LOOP:
		np->mc_loop = (val != 0);
		retv = 0;
		break;

	case IPV6_MULTICAST_IF:
	{
		int oif = 0;
		struct in6_addr addr;

		if (copy_from_user(&addr, optval, sizeof(struct in6_addr)))
			return -EFAULT;
				
		if (!ipv6_addr_any(&addr)) {
			struct inet6_ifaddr *ifp;

			ifp = ipv6_chk_addr(&addr, NULL, 0);

			if (ifp == NULL) {
				retv = -EADDRNOTAVAIL;
				break;
			}

			oif = ifp->idev->dev->ifindex;
		}
		if (sk->bound_dev_if && sk->bound_dev_if != oif) {
			retv = -EINVAL;
			break;
		}
		np->mcast_oif = oif;
		retv = 0;
		break;
	}
	case IPV6_ADD_MEMBERSHIP:
	case IPV6_DROP_MEMBERSHIP:
	{
		struct ipv6_mreq mreq;
		int err;

		err = copy_from_user(&mreq, optval, sizeof(struct ipv6_mreq));
		if(err)
			return -EFAULT;
		
		if (optname == IPV6_ADD_MEMBERSHIP)
			retv = ipv6_sock_mc_join(sk, mreq.ipv6mr_ifindex, &mreq.ipv6mr_multiaddr);
		else
			retv = ipv6_sock_mc_drop(sk, mreq.ipv6mr_ifindex, &mreq.ipv6mr_multiaddr);
	}
	};

out:
	return retv;
}

int ipv6_getsockopt(struct sock *sk, int level, int optname, char *optval, 
		    int *optlen)
{
	return 0;
}

#if defined(MODULE) && defined(CONFIG_SYSCTL)

/*
 *	sysctl registration functions defined in sysctl_net_ipv6.c
 */

extern void ipv6_sysctl_register(void);
extern void ipv6_sysctl_unregister(void);
#endif

__initfunc(void ipv6_init(void))
{
	dev_add_pack(&ipv6_packet_type);

#if defined(MODULE) && defined(CONFIG_SYSCTL)
	ipv6_sysctl_register();
#endif

	register_netdevice_notifier(&ipv6_dev_notf);
	
	ip6_route_init();
}

#ifdef MODULE
void ipv6_cleanup(void)
{
	unregister_netdevice_notifier(&ipv6_dev_notf);
	dev_remove_pack(&ipv6_packet_type);
#ifdef CONFIG_SYSCTL
	ipv6_sysctl_unregister();	
#endif
	ip6_route_cleanup();
	icmpv6_cleanup();
	addrconf_cleanup();	
}
#endif


