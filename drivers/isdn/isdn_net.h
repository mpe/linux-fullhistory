/* $Id: isdn_net.h,v 1.6 1997/10/09 21:28:54 fritz Exp $

 * header for Linux ISDN subsystem, network related functions (linklevel).
 *
 * Copyright 1994,95,96 by Fritz Elfert (fritz@wuemaus.franken.de)
 * Copyright 1995,96    by Thinking Objects Software GmbH Wuerzburg
 * Copyright 1995,96    by Michael Hipp (Michael.Hipp@student.uni-tuebingen.de)
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
 * $Log: isdn_net.h,v $
 * Revision 1.6  1997/10/09 21:28:54  fritz
 * New HL<->LL interface:
 *   New BSENT callback with nr. of bytes included.
 *   Sending without ACK.
 *   New L1 error status (not yet in use).
 *   Cleaned up obsolete structures.
 * Implemented Cisco-SLARP.
 * Changed local net-interface data to be dynamically allocated.
 * Removed old 2.0 compatibility stuff.
 *
 * Revision 1.5  1997/02/10 20:12:47  fritz
 * Changed interface for reporting incoming calls.
 *
 * Revision 1.4  1997/02/03 23:16:48  fritz
 * Removed isdn_net_receive_callback prototype.
 *
 * Revision 1.3  1997/01/17 01:19:30  fritz
 * Applied chargeint patch.
 *
 * Revision 1.2  1996/04/20 16:29:43  fritz
 * Misc. typos
 *
 * Revision 1.1  1996/02/11 02:35:13  fritz
 * Initial revision
 *
 */

			      /* Definitions for hupflags:                */
#define ISDN_WAITCHARGE  1      /* did not get a charge info yet            */
#define ISDN_HAVECHARGE  2      /* We know a charge info                    */
#define ISDN_CHARGEHUP   4      /* We want to use the charge mechanism      */
#define ISDN_INHUP       8      /* Even if incoming, close after huptimeout */
#define ISDN_MANCHARGE  16      /* Charge Interval manually set             */

/*
 * Definitions for Cisco-HDLC header.
 */

typedef struct cisco_hdr {
	__u8  addr; /* unicast/broadcast */
	__u8  ctrl; /* Always 0          */
	__u16 type; /* IP-typefield      */
} cisco_hdr;

typedef struct cisco_slarp {
	__u32 code;                     /* SLREQ/SLREPLY/KEEPALIVE */
	union {
		struct {
			__u32 ifaddr;   /* My interface address     */
			__u32 netmask;  /* My interface netmask     */
		} reply;
		struct {
			__u32 my_seq;   /* Packet sequence number   */
			__u32 your_seq;
		} keepalive;
	} slarp;
	__u16 rel;                      /* Always 0xffff            */
	__u16 t1;                       /* Uptime in usec >> 16     */
	__u16 t0;                       /* Uptime in usec & 0xffff  */
} cisco_slarp;

#define CISCO_ADDR_UNICAST    0x0f
#define CISCO_ADDR_BROADCAST  0x8f
#define CISCO_TYPE_INET       0x0800
#define CISCO_TYPE_SLARP      0x8035
#define CISCO_SLARP_REPLY     0
#define CISCO_SLARP_REQUEST   1
#define CISCO_SLARP_KEEPALIVE 2

extern char *isdn_net_new(char *, struct device *);
extern char *isdn_net_newslave(char *);
extern int isdn_net_rm(char *);
extern int isdn_net_rmall(void);
extern int isdn_net_stat_callback(int, isdn_ctrl *);
extern int isdn_net_setcfg(isdn_net_ioctl_cfg *);
extern int isdn_net_getcfg(isdn_net_ioctl_cfg *);
extern int isdn_net_addphone(isdn_net_ioctl_phone *);
extern int isdn_net_getphones(isdn_net_ioctl_phone *, char *);
extern int isdn_net_delphone(isdn_net_ioctl_phone *);
extern int isdn_net_find_icall(int, int, int, setup_parm);
extern void isdn_net_hangup(struct device *);
extern void isdn_net_dial(void);
extern void isdn_net_autohup(void);
extern int isdn_net_force_hangup(char *);
extern int isdn_net_force_dial(char *);
extern isdn_net_dev *isdn_net_findif(char *);
extern int isdn_net_send_skb(struct device *, isdn_net_local *,
			     struct sk_buff *);
extern int isdn_net_rcv_skb(int, struct sk_buff *);
extern void isdn_net_slarp_out(void);
