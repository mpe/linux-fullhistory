/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the IP module.
 *
 * Version:	@(#)ip.h	1.0.2	05/07/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Alan Cox, <gw4pts@gw4pts.ampr.org>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _IP_H
#define _IP_H


#include <linux/config.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/ip.h>
#include <linux/netdevice.h>
#include <net/route.h>

#ifndef _SNMP_H
#include <net/snmp.h>
#endif

#include <net/sock.h>	/* struct sock */

struct inet_skb_parm
{
	struct ip_options	opt;		/* Compiled IP options		*/
	u16			redirport;	/* Redirect port		*/
	unsigned char		flags;
	char			vif;

#define IPSKB_MASQUERADED	1
#define IPSKB_TRANSLATED	2
#define IPSKB_TUNNELED		4
};

struct ipcm_cookie
{
	u32			addr;
	struct ip_options	*opt;
};

#define IPCB(skb) ((struct inet_skb_parm*)((skb)->cb))

/* IP flags. */
#define IP_CE		0x8000		/* Flag: "Congestion"		*/
#define IP_DF		0x4000		/* Flag: "Don't Fragment"	*/
#define IP_MF		0x2000		/* Flag: "More Fragments"	*/
#define IP_OFFSET	0x1FFF		/* "Fragment Offset" part	*/

#define IP_FRAG_TIME	(30 * HZ)		/* fragment lifetime	*/

extern void		ip_mc_dropsocket(struct sock *);
extern void		ip_mc_dropdevice(struct device *dev);
extern int		ip_mc_procinfo(char *, char **, off_t, int, int);

/*
 *	Functions provided by ip.c
 */

extern int		ip_ioctl(struct sock *sk, int cmd, unsigned long arg);
extern int		ip_build_pkt(struct sk_buff *skb, struct sock *sk,
				     u32 saddr, u32 daddr,
				     struct ip_options *opt);
extern int 		ip_build_header(struct sk_buff *skb, struct sock *sk);
extern int		ip_rcv(struct sk_buff *skb, struct device *dev,
			       struct packet_type *pt);
extern int		ip_local_deliver(struct sk_buff *skb);
extern int		ip_mr_input(struct sk_buff *skb);
extern int		ip_output(struct sk_buff *skb);
extern int		ip_mc_output(struct sk_buff *skb);
#ifdef CONFIG_IP_ACCT
extern int		ip_acct_output(struct sk_buff *skb);
#else
#define ip_acct_output	dev_queue_xmit
#endif
extern void		ip_fragment(struct sk_buff *skb, int, int (*out)(struct sk_buff*));
extern struct sk_buff *	ip_reply(struct sk_buff *skb, int payload);
extern int		ip_do_nat(struct sk_buff *skb);
extern void		ip_send_check(struct iphdr *ip);
extern int		ip_id_count;			  
extern void		ip_queue_xmit(struct sk_buff *skb);
extern void		ip_init(void);
extern int		ip_build_xmit(struct sock *sk,
				      int getfrag (const void *,
						   char *,
						   unsigned int,
						   unsigned int),
				      const void *frag,
				      unsigned short int length,
				      struct ipcm_cookie *ipc,
				      struct rtable *rt,
				      int flags);


static __inline__
void ip_send(struct sk_buff *skb)
{
	ip_ll_header(skb);

	if (skb->len > skb->dev->mtu + skb->dev->hard_header_len)
		ip_fragment(skb, 0, ip_acct_output);
	else
		ip_acct_output(skb);
}

static __inline__
int ip_decrease_ttl(struct iphdr *iph)
{
	u16 check = iph->check;
	check = ntohs(check) + 0x0100;
	if ((check & 0xFF00) == 0)
		check++;		/* carry overflow */
	iph->check = htons(check);
	return --iph->ttl;
}

extern struct ip_mib	ip_statistics;

struct ipv4_config
{
	int	accept_redirects;
	int	secure_redirects;
	int	rfc1620_redirects;
	int	rfc1812_filter;
	int	addrmask_agent;
	int	log_martians;
	int	source_route;
	int	multicast_route;
	int	bootp_agent;
	int	bootp_relay;
	int	fib_model;
	int	no_pmtu_disc;
};

extern struct ipv4_config ipv4_config;

#define IS_ROUTER	(ip_statistics.IpForwarding == 1)

/*
 *	Functions provided by ip_fragment.o
 */
 
struct sk_buff *ip_defrag(struct sk_buff *skb);

/*
 *	Functions provided by ip_forward.c
 */
 
extern int ip_forward(struct sk_buff *skb);
extern int ip_net_unreachable(struct sk_buff *skb);
 
/*
 *	Functions provided by ip_options.c
 */
 
extern void ip_options_build(struct sk_buff *skb, struct ip_options *opt, u32 daddr, u32 saddr, int is_frag);
extern int ip_options_echo(struct ip_options *dopt, struct sk_buff *skb);
extern void ip_options_fragment(struct sk_buff *skb);
extern int ip_options_compile(struct ip_options *opt, struct sk_buff *skb);
extern int ip_options_get(struct ip_options **optp, unsigned char *data, int optlen, int user);
extern void ip_options_undo(struct ip_options * opt);
extern void ip_forward_options(struct sk_buff *skb);
extern int ip_options_rcv_srr(struct sk_buff *skb);

/*
 *	Functions provided by ip_sockglue.c
 */

extern void	ip_cmsg_recv(struct msghdr *msg, struct sk_buff *skb);
extern int	ip_cmsg_send(struct msghdr *msg, struct ipcm_cookie *ipc, struct device **devp);
extern int	ip_setsockopt(struct sock *sk, int level, int optname, char *optval, int optlen);
extern int	ip_getsockopt(struct sock *sk, int level, int optname, char *optval, int *optlen);

extern int		ipv4_backlog_rcv(struct sock *sk, struct sk_buff *skb);  
#endif	/* _IP_H */
