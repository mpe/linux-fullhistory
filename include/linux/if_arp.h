/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Global definitions for the ARP (RFC 826) protocol.
 *
 * Version:	@(#)if_arp.h	1.0.1	04/16/93
 *
 * Authors:	Original taken from Berkeley UNIX 4.3, (c) UCB 1986-1988
 *		Portions taken from the KA9Q/NOS (v2.00m PA0GRI) source.
 *		Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Florian La Roche,
 *		Jonathan Layes <layes@loran.com>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _LINUX_IF_ARP_H
#define _LINUX_IF_ARP_H


/* ARP protocol HARDWARE identifiers. */
#define ARPHRD_NETROM	0		/* from KA9Q: NET/ROM pseudo	*/
#define ARPHRD_ETHER 	1		/* Ethernet 10Mbps		*/
#define	ARPHRD_EETHER	2		/* Experimental Ethernet	*/
#define	ARPHRD_AX25	3		/* AX.25 Level 2		*/
#define	ARPHRD_PRONET	4		/* PROnet token ring		*/
#define	ARPHRD_CHAOS	5		/* Chaosnet			*/
#define	ARPHRD_IEEE802	6		/* IEEE 802.2 Ethernet/TR/TB	*/
#define	ARPHRD_ARCNET	7		/* ARCnet			*/
#define	ARPHRD_APPLETLK	8		/* APPLEtalk			*/
#define ARPHRD_DLCI	15		/* Frame Relay DLCI		*/

/* Dummy types for non ARP hardware */
#define ARPHRD_SLIP	256
#define ARPHRD_CSLIP	257
#define ARPHRD_SLIP6	258
#define ARPHRD_CSLIP6	259
#define ARPHRD_RSRVD	260		/* Notional KISS type 		*/
#define ARPHRD_ADAPT	264
#define ARPHRD_PPP	512

#define ARPHRD_TUNNEL	768		/* IPIP tunnel			*/
#define ARPHRD_TUNNEL6	769		/* IPIP6 tunnel			*/
#define ARPHRD_FRAD	770		/* Frame Relay Access Device	*/
#define ARPHRD_SKIP	771		/* SKIP vif			*/
#define ARPHRD_LOOPBACK	772		/* Loopback device		*/
#define ARPHRD_LOCALTLK 773		/* Localtalk device		*/
#define ARPHRD_METRICOM	774		/* Metricom STRIP		*/

/* ARP protocol opcodes. */
#define	ARPOP_REQUEST	1		/* ARP request			*/
#define	ARPOP_REPLY	2		/* ARP reply			*/
#define	ARPOP_RREQUEST	3		/* RARP request			*/
#define	ARPOP_RREPLY	4		/* RARP reply			*/


/* ARP ioctl request. */
struct arpreq {
  struct sockaddr	arp_pa;		/* protocol address		*/
  struct sockaddr	arp_ha;		/* hardware address		*/
  int			arp_flags;	/* flags			*/
  struct sockaddr       arp_netmask;    /* netmask (only for proxy arps) */
  char			arp_dev[16];
};

struct arpreq_old {
  struct sockaddr	arp_pa;		/* protocol address		*/
  struct sockaddr	arp_ha;		/* hardware address		*/
  int			arp_flags;	/* flags			*/
  struct sockaddr       arp_netmask;    /* netmask (only for proxy arps) */
};

/* ARP Flag values. */
#define ATF_COM		0x02		/* completed entry (ha valid)	*/
#define	ATF_PERM	0x04		/* permanent entry		*/
#define	ATF_PUBL	0x08		/* publish entry		*/
#define	ATF_USETRAILERS	0x10		/* has requested trailers	*/
#define ATF_NETMASK     0x20            /* want to use a netmask (only
					   for proxy entries) */

/*
 *	This structure defines an ethernet arp header.
 */

struct arphdr
{
	unsigned short	ar_hrd;		/* format of hardware address	*/
	unsigned short	ar_pro;		/* format of protocol address	*/
	unsigned char	ar_hln;		/* length of hardware address	*/
	unsigned char	ar_pln;		/* length of protocol address	*/
	unsigned short	ar_op;		/* ARP opcode (command)		*/

#if 0
	 /*
	  *	 Ethernet looks like this : This bit is variable sized however...
	  */
	unsigned char		ar_sha[ETH_ALEN];	/* sender hardware address	*/
	unsigned char		ar_sip[4];		/* sender IP address		*/
	unsigned char		ar_tha[ETH_ALEN];	/* target hardware address	*/
	unsigned char		ar_tip[4];		/* target IP address		*/
#endif

};

/* Support for the user space arp daemon, arpd */

#define ARPD_UPDATE	0x01
#define ARPD_LOOKUP	0x02
#define ARPD_FLUSH	0x03

struct arpd_request
{
	unsigned short	req;			/* request type */
	__u32		ip;			/* ip address of entry */
	unsigned long	dev;			/* Device entry is tied to */
	unsigned long	stamp;
	unsigned long	updated;
	unsigned char	ha[MAX_ADDR_LEN];	/* Hardware address */
};

#endif	/* _LINUX_IF_ARP_H */
