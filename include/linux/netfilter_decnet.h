#ifndef __LINUX_DECNET_NETFILTER_H
#define __LINUX_DECNET_NETFILTER_H

/* DECnet-specific defines for netfilter. 
 * This file (C) Steve Whitehouse 1999 derived from the
 * ipv4 netfilter header file which is
 * (C)1998 Rusty Russell -- This code is GPL.
 */

#include <linux/netfilter.h>

/* IP Cache bits. */
/* Src IP address. */
#define NFC_DN_SRC		0x0001
/* Dest IP address. */
#define NFC_DN_DST		0x0002
/* Input device. */
#define NFC_DN_IF_IN		0x0004
/* Output device. */
#define NFC_DN_IF_OUT		0x0008

/* DECnet Hooks */
/* After promisc drops, checksum checks. */
#define NF_DN_PRE_ROUTING	0
/* If the packet is destined for this box. */
#define NF_DN_LOCAL_IN		1
/* If the packet is destined for another interface. */
#define NF_DN_FORWARD		2
/* Packets coming from a local process. */
#define NF_DN_LOCAL_OUT		3
/* Packets about to hit the wire. */
#define NF_DN_POST_ROUTING	4
/* Input Hello Packets */
#define NF_DN_HELLO		5
/* Input Routing Packets */
#define NF_DN_ROUTE		6
#define NF_DN_NUMHOOKS		7

#endif /*__LINUX_DECNET_NETFILTER_H*/
