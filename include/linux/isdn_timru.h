/* isdn_timru.h
 *
 * Linux ISDN subsystem, timeout-rules for network interfaces.
 *
 * Copyright 1997       by Christian Lademann <cal@zls.de>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. 
 *
 */

/*
02.06.97:cal:ISDN_TIMRU_PACKET_NONE def., ISDN_TIMRU_PACKET_* inkr.
*/

#ifndef __isdn_timru_h__
#define __isdn_timru_h__

#define	ISDN_TIMRU_PACKET_NONE		0
#define	ISDN_TIMRU_PACKET_SKB		1
#define	ISDN_TIMRU_PACKET_PPP		2
#define	ISDN_TIMRU_PACKET_PPP_NO_HEADER	3

#define	ISDN_TIMRU_BRINGUP		0
#define	ISDN_TIMRU_KEEPUP_IN		1
#define	ISDN_TIMRU_KEEPUP_OUT		2
#define	ISDN_TIMRU_BRINGDOWN		3
#define	ISDN_TIMRU_NUM_CHECK		4

#define	ISDN_TIMRU_PROTFAM_WILDCARD	0
#define	ISDN_TIMRU_PROTFAM_IP		1
#define	ISDN_TIMRU_PROTFAM_PPP		2
#define	ISDN_TIMRU_PROTFAM_IPX		3
#define	ISDN_TIMRU_NUM_PROTFAM		4

#define	ISDN_TIMRU_IP_WILDCARD		0
#define	ISDN_TIMRU_IP_ICMP		1
#define	ISDN_TIMRU_IP_TCP		2
#define	ISDN_TIMRU_IP_UDP		3

#define	ISDN_TIMRU_PPP_WILDCARD		0
#define	ISDN_TIMRU_PPP_IPCP		1
#define	ISDN_TIMRU_PPP_IPXCP		2
#define	ISDN_TIMRU_PPP_CCP		3
#define	ISDN_TIMRU_PPP_LCP		4
#define	ISDN_TIMRU_PPP_PAP		5
#define	ISDN_TIMRU_PPP_LQR		6
#define	ISDN_TIMRU_PPP_CHAP		7

typedef struct {
	struct in_addr	saddr,		/* Source Address */
			smask,		/* Source Subnetmask */
			daddr,		/* Dest. Address */
			dmask;		/* Dest. Subnetmask */
	ushort		protocol;	/* TCP, UDP, ... */
	union {
		struct {
			__u16	s_from,	/* Source Port */
				s_to,
				d_from,
				d_to;
		}		port;
		struct {
			__u8	from,	/* ICMP-Type */
				to;
		}		type;
	}		pt;
}	isdn_timeout_rule_ip;


typedef struct {
	ushort		protocol;	/* IPCP, LCP, ... */
}	isdn_timeout_rule_ppp;


typedef struct isdn_timeout_rule_s {
	struct isdn_timeout_rule_s	*next,		/* Pointer to next rule */
					*prev;		/* Pointer to previous rule */
	ushort				type,		/* BRINGUP, KEEPUP_*, ... */
					neg;
	int				timeout;	/* Timeout value */
	ushort				protfam;	/* IP, IPX, PPP, ... */
	union {
		isdn_timeout_rule_ip	ip;	/* IP-Rule */
		isdn_timeout_rule_ppp	ppp;	/* PPP-Rule */
	}				rule;	/* Prot.-specific rule */
}	isdn_timeout_rule;


typedef struct {
	char			name [9];	/* Interface */
	int			where,		/* 0/1: add to start/end of list, -1: handle default */
				type,
				protfam,
				index,
				defval;
	isdn_timeout_rule	rule;		/* Rule */
}	isdn_ioctl_timeout_rule;

#ifdef __KERNEL__
extern int	isdn_net_recalc_timeout(int, int, struct device *, void *, ulong);
extern int	isdn_timru_alloc_timeout_rules(struct device *);
extern int	isdn_timru_ioctl_add_rule(isdn_ioctl_timeout_rule *);
extern int	isdn_timru_ioctl_del_rule(isdn_ioctl_timeout_rule *);
extern int	isdn_timru_ioctl_get_rule(isdn_ioctl_timeout_rule *);
#endif /* __KERNEL__ */

#endif /* __isdn_timru_h__ */
