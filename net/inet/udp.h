/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the UDP module.
 *
 * Version:	@(#)udp.h	1.0.2	05/07/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _UDP_H
#define _UDP_H

#include <linux/udp.h>


#define UDP_NO_CHECK	1


extern struct proto udp_prot;


extern int	udp_select(struct sock *sk, int sel_type, select_table *wait);
extern void	udp_err(int err, unsigned char *header, unsigned long daddr,
			unsigned long saddr, struct inet_protocol *protocol);
extern int	udp_recvfrom(struct sock *sk, unsigned char *to,
			     int len, int noblock, unsigned flags,
			     struct sockaddr_in *sin, int *addr_len);
extern int	udp_read(struct sock *sk, unsigned char *buff,
			 int len, int noblock, unsigned flags);
extern int	udp_connect(struct sock *sk,
			    struct sockaddr_in *usin, int addr_len);
extern int	udp_rcv(struct sk_buff *skb, struct device *dev,
			struct options *opt, unsigned long daddr,
			unsigned short len, unsigned long saddr, int redo,
			struct inet_protocol *protocol);
extern int	udp_ioctl(struct sock *sk, int cmd, unsigned long arg);


#endif	/* _UDP_H */
