/* $Id: isdn_net.h,v 1.16 2000/03/21 23:53:22 kai Exp $

 * header for Linux ISDN subsystem, network related functions (linklevel).
 *
 * Copyright 1994-1999  by Fritz Elfert (fritz@isdn4linux.de)
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
 * Revision 1.16  2000/03/21 23:53:22  kai
 * fix backwards compatibility
 *
 * Revision 1.15  2000/03/19 15:27:53  kai
 * no known bugs left...
 *
 * Revision 1.14  2000/03/18 16:20:25  kai
 * cosmetics / renaming
 *
 * Revision 1.13  2000/03/17 18:20:46  kai
 * moved to frame_cnt based flow control
 * some races still need to be fixed
 *
 * Revision 1.12  2000/03/17 17:01:00  kai
 * cleanup
 *
 * Revision 1.11  2000/03/17 16:22:55  kai
 * we keep track of outstanding packets (given to HL, but not confirmed yet)
 * now, but we don't use it for flow control yet.
 *
 * Revision 1.10  1999/08/22 20:26:06  calle
 * backported changes from kernel 2.3.14:
 * - several #include "config.h" gone, others come.
 * - "struct device" changed to "struct net_device" in 2.3.14, added a
 *   define in isdn_compat.h for older kernel versions.
 *
 * Revision 1.9  1999/04/12 12:33:27  fritz
 * Changes from 2.0 tree.
 *
 * Revision 1.8  1998/10/30 17:55:33  he
 * dialmode for x25iface and multulink ppp
 *
 * Revision 1.7  1998/08/31 21:09:55  he
 * new ioctl IIOCNETGPN for /dev/isdninfo (get network interface'
 *     peer phone number)
 *
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

extern char *isdn_net_new(char *, struct net_device *);
extern char *isdn_net_newslave(char *);
extern int isdn_net_rm(char *);
extern int isdn_net_rmall(void);
extern int isdn_net_stat_callback(int, isdn_ctrl *);
extern int isdn_net_setcfg(isdn_net_ioctl_cfg *);
extern int isdn_net_getcfg(isdn_net_ioctl_cfg *);
extern int isdn_net_addphone(isdn_net_ioctl_phone *);
extern int isdn_net_getphones(isdn_net_ioctl_phone *, char *);
extern int isdn_net_getpeer(isdn_net_ioctl_phone *, isdn_net_ioctl_phone *);
extern int isdn_net_delphone(isdn_net_ioctl_phone *);
extern int isdn_net_find_icall(int, int, int, setup_parm);
extern void isdn_net_hangup(struct net_device *);
extern void isdn_net_dial(void);
extern void isdn_net_autohup(void);
extern int isdn_net_force_hangup(char *);
extern int isdn_net_force_dial(char *);
extern isdn_net_dev *isdn_net_findif(char *);
extern int isdn_net_rcv_skb(int, struct sk_buff *);
extern void isdn_net_slarp_out(void);
extern int isdn_net_dial_req(isdn_net_local *);
extern void isdn_net_writebuf_skb(isdn_net_local *lp, struct sk_buff *skb);
extern void isdn_net_write_super(isdn_net_local *lp, struct sk_buff *skb);

#define ISDN_NET_MAX_QUEUE_LENGTH 2

/*
 * is this particular channel busy?
 */
static __inline__ int isdn_net_lp_busy(isdn_net_local *lp)
{
	if (atomic_read(&lp->frame_cnt) < ISDN_NET_MAX_QUEUE_LENGTH)
		return 0;
	else 
		return 1;
}

/*
 * For the given net device, this will get a non-busy channel out of the
 * corresponding bundle. The returned channel is locked.
 */
static __inline__ isdn_net_local * isdn_net_get_locked_lp(isdn_net_dev *nd)
{
	unsigned long flags;
	isdn_net_local *lp;

	spin_lock_irqsave(&nd->queue_lock, flags);
	lp = nd->queue;         /* get lp on top of queue */
	spin_lock_bh(&nd->queue->xmit_lock);
	while (isdn_net_lp_busy(nd->queue)) {
		spin_unlock_bh(&nd->queue->xmit_lock);
		nd->queue = nd->queue->next;
		if (nd->queue == lp) /* not found -- should never happen */
			return 0;
		spin_lock_bh(&nd->queue->xmit_lock);
	}
	lp = nd->queue;

	nd->queue = nd->queue->next;
	spin_unlock_irqrestore(&nd->queue_lock, flags);
	return lp;
}

/*
 * add a channel to a bundle
 */
static __inline__ void isdn_net_add_to_bundle(isdn_net_dev *nd, isdn_net_local *nlp)
{
	isdn_net_local *lp;
	unsigned long flags;

	spin_lock_irqsave(&nd->queue_lock, flags);

	lp = nd->queue;
	nlp->last = lp->last;
	lp->last->next = nlp;
	lp->last = nlp;
	nlp->next = lp;
	nd->queue = nlp;

	spin_unlock_irqrestore(&nd->queue_lock, flags);
}
/*
 * remove a channel from the bundle it belongs to
 */
static __inline__ void isdn_net_rm_from_bundle(isdn_net_local *lp)
{
	isdn_net_local *master_lp = lp;
	unsigned long flags;

	if (lp->master)
		master_lp = (isdn_net_local *) lp->master->priv;

	spin_lock_irqsave(&master_lp->netdev->queue_lock, flags);
	lp->last->next = lp->next;
	lp->next->last = lp->last;
	if (master_lp->netdev->queue == lp)
		master_lp->netdev->queue = lp->next;
	lp->next = lp->last = lp;	/* (re)set own pointers */
	spin_unlock_irqrestore(&master_lp->netdev->queue_lock, flags);
}


