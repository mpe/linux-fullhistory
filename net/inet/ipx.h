
/*
 *	The following information is in its entirety obtained from:
 *
 *	Novell 'IPX Router Specification' Version 1.10 
 *		Part No. 107-000029-001
 *
 *	Which is available from ftp.novell.com
 */

#ifndef _NET_INET_IPX_H_
#define _NET_INET_IPX_H_

#include <linux/ipx.h>
#include "datalink.h"

typedef struct
{
	unsigned long net;
	unsigned char  node[6]; 
	unsigned short sock;
} ipx_address;

#define ipx_broadcast_node	"\377\377\377\377\377\377"

typedef struct ipx_packet
{
	unsigned short	ipx_checksum;
#define IPX_NO_CHECKSUM	0xFFFF
	unsigned short  ipx_pktsize;
	unsigned char   ipx_tctrl;
	unsigned char   ipx_type;
#define IPX_TYPE_UNKNOWN	0x00
#define IPX_TYPE_RIP		0x01	/* may also be 0 */
#define IPX_TYPE_SAP		0x04	/* may also be 0 */
#define IPX_TYPE_SPX		0x05	/* Not yet implemented */
#define IPX_TYPE_NCP		0x11	/* $lots for docs on this (SPIT) */
#define IPX_TYPE_PPROP		0x14	/* complicated flood fill brdcast [Not supported] */
	ipx_address	ipx_dest __attribute__ ((packed));
	ipx_address	ipx_source __attribute__ ((packed));
} ipx_packet;


typedef struct ipx_route
{
	unsigned long net;
	unsigned char router_node[6];
	unsigned long router_net;
	unsigned short flags;
#define IPX_RT_ROUTED	1		/* This isn't a direct route. Send via this if to node router_node */
#define IPX_RT_BLUEBOOK	2		
#define IPX_RT_8022	4		
#define IPX_RT_SNAP	8		
	unsigned short dlink_type;
	struct device *dev;
	struct datalink_proto *datalink;
	struct ipx_route *next;
	struct ipx_route *nextlocal;
} ipx_route;


typedef struct sock ipx_socket;


#include "ipxcall.h"
extern int ipx_rcv(struct sk_buff *skb, struct device *dev, struct packet_type *pt);
extern void ipxrtr_device_down(struct device *dev);



#endif
