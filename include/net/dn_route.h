#ifndef _NET_DN_ROUTE_H
#define _NET_DN_ROUTE_H

/******************************************************************************
    (c) 1995-1998 E.M. Serrat		emserrat@geocities.com
    
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
*******************************************************************************/
/* dn_route.c functions prototyping */
extern void dn_send_skb(struct sk_buff *);
extern struct sk_buff *dn_alloc_skb(struct sock *sk, int size, int pri);
extern int dn_route_output(struct sock *sk);

/* Masks for flags field */
#define DN_RT_F_PID 0x07 /* Mask for packet type                      */
#define DN_RT_F_PF  0x80 /* Padding Follows                           */
#define DN_RT_F_VER 0x40 /* Version =0 discard packet if ==1          */
#define DN_RT_F_IE  0x20 /* Intra Ethernet, Reserved in short pkt     */
#define DN_RT_F_RTS 0x10 /* Packet is being returned to sender        */
#define DN_RT_F_RQR 0x08 /* Return packet to sender upon non-delivery */

/* Mask for types of routing packets */
#define DN_RT_PKT_MSK   0x06
/* Types of routing packets */
#define DN_RT_PKT_SHORT 0x02 /* Short routing packet */
#define DN_RT_PKT_LONG  0x06 /* Long routing packet  */

/* Mask for control/routing selection */
#define DN_RT_PKT_CNTL  0x01 /* Set to 1 if a control packet  */
/* Types of control packets */
#define DN_RT_CNTL_MSK  0x0f /* Mask for control packets      */
#define DN_RT_PKT_INIT  0x01 /* Initialisation packet         */
#define DN_RT_PKT_VERI  0x03 /* Verification Message          */
#define DN_RT_PKT_HELO  0x05 /* Hello and Test Message        */
#define DN_RT_PKT_L1RT  0x07 /* Level 1 Routing Message       */
#define DN_RT_PKT_L2RT  0x09 /* Level 2 Routing Message       */
#define DN_RT_PKT_ERTH  0x0b /* Ethernet Router Hello         */
#define DN_RT_PKT_EEDH  0x0d /* Ethernet EndNode Hello        */

/* Values for info field in hello message */
#define DN_RT_INFO_TYPE 0x03 /* Type mask                     */
#define DN_RT_INFO_L1RT 0x02 /* L1 Router                     */
#define DN_RT_INFO_L2RT 0x01 /* L2 Router                     */
#define DN_RT_INFO_ENDN 0x03 /* EndNode                       */
#define DN_RT_INFO_VERI 0x04 /* Verification Reqd.            */
#define DN_RT_INFO_RJCT 0x08 /* Reject Flag, Reserved         */
#define DN_RT_INFO_VFLD 0x10 /* Verification Failed, Reserved */
#define DN_RT_INFO_NOML 0x20 /* No Multicast traffic accepted */
#define DN_RT_INFO_BLKR 0x40 /* Blocking Requested            */


struct dn_route {
	union {
		struct dst_entry dst;
		struct dn_route *rt_next;
	} u;
	unsigned short rt_saddr;
	unsigned short rt_daddr;
	int rt_iif;
	int rt_oif;
};

extern void dn_route_init(void);
extern void dn_route_cleanup(void);

#endif /* _NET_DN_ROUTE_H */
