/* $Id: isdn_ppp.c,v 1.49 1999/07/06 07:47:11 calle Exp $
 *
 * Linux ISDN subsystem, functions for synchronous PPP (linklevel).
 *
 * Copyright 1995,96 by Michael Hipp (Michael.Hipp@student.uni-tuebingen.de)
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
 * $Log: isdn_ppp.c,v $
 * Revision 1.49  1999/07/06 07:47:11  calle
 * bugfix: dev_alloc_skb only reserve 16 bytes. We need to look at the
 *  	hdrlen the driver want. So I changed dev_alloc_skb calls
 *         to alloc_skb and skb_reserve.
 *
 * Revision 1.48  1999/07/01 08:29:56  keil
 * compatibility to 2.3 kernel
 *
 * Revision 1.47  1999/04/18 14:06:59  fritz
 * Removed TIMRU stuff.
 *
 * Revision 1.46  1999/04/12 12:33:35  fritz
 * Changes from 2.0 tree.
 *
 * Revision 1.45  1998/12/30 17:48:24  paul
 * fixed syncPPP callback out
 *
 * Revision 1.44  1998/10/30 17:55:34  he
 * dialmode for x25iface and multulink ppp
 *
 * Revision 1.43  1998/10/29 17:23:54  hipp
 * Minor MPPP fixes, verboser logging.
 *
 * Revision 1.42  1998/07/20 11:30:07  hipp
 * Readded compression check
 *
 * Revision 1.41  1998/07/08 16:50:57  hipp
 * Compression changes
 *
 * Revision 1.40  1998/04/06 19:07:27  hipp
 * added check, whether compression is enabled.
 *
 * Revision 1.39  1998/03/25 22:46:53  hipp
 * Some additional CCP changes.
 *
 * Revision 1.38  1998/03/24 16:33:06  hipp
 * More CCP changes. BSD compression now "works" on a local loopback link.
 * Moved some isdn_ppp stuff from isdn.h to isdn_ppp.h
 *
 * Revision 1.37  1998/03/22 18:50:49  hipp
 * Added BSD Compression for syncPPP .. UNTESTED at the moment
 *
 * Revision 1.36  1998/03/09 17:46:30  he
 * merged in 2.1.89 changes
 *
 * Revision 1.35  1998/03/07 18:21:11  cal
 * Dynamic Timeout-Rule-Handling vs. 971110 included
 *
 * Revision 1.34  1998/02/25 17:49:48  he
 * Changed return codes caused be failing copy_{to,from}_user to -EFAULT
 *
 * Revision 1.33  1998/02/20 17:11:54  fritz
 * Changes for recent kernels.
 *
 * Revision 1.32  1998/01/31 19:29:55  calle
 * Merged changes from and for 2.1.82, not tested only compiled ...
 *
 * Revision 1.31  1997/10/09 21:29:01  fritz
 * New HL<->LL interface:
 *   New BSENT callback with nr. of bytes included.
 *   Sending without ACK.
 *   New L1 error status (not yet in use).
 *   Cleaned up obsolete structures.
 * Implemented Cisco-SLARP.
 * Changed local net-interface data to be dynamically allocated.
 * Removed old 2.0 compatibility stuff.
 *
 * Revision 1.30  1997/10/01 09:20:38  fritz
 * Removed old compatibility stuff for 2.0.X kernels.
 * From now on, this code is for 2.1.X ONLY!
 * Old stuff is still in the separate branch.
 *
 * Revision 1.29  1997/08/21 23:11:44  fritz
 * Added changes for kernels >= 2.1.45
 *
 * Revision 1.28  1997/06/17 13:05:57  hipp
 * Applied Eric's underflow-patches (slightly modified)
 * more compression changes (but disabled at the moment)
 * changed one copy_to_user() to run with enabled IRQs
 * a few MP changes
 * changed 'proto' handling in the isdn_ppp receive code
 *
 * Revision 1.27  1997/03/30 16:51:17  calle
 * changed calls to copy_from_user/copy_to_user and removed verify_area
 * were possible.
 *
 * Revision 1.26  1997/02/23 16:53:44  hipp
 * minor cleanup
 * some initial changes for future PPP compresion
 * added AC,PC compression for outgoing frames
 *
 * Revision 1.25  1997/02/12 20:37:35  hipp
 * New ioctl() PPPIOCGCALLINFO, minor cleanup
 *
 * Revision 1.24  1997/02/11 18:32:56  fritz
 * Bugfix in isdn_ppp_free_mpqueue().
 *
 * Revision 1.23  1997/02/10 11:12:19  fritz
 * More changes for Kernel 2.1.X compatibility.
 *
 * Revision 1.22  1997/02/06 15:03:51  hipp
 * changed GFP_KERNEL kmalloc to GFP_ATOMIC in isdn_ppp_fill_mpqueue()
 *
 * Revision 1.21  1997/02/03 23:29:38  fritz
 * Reformatted according CodingStyle
 * Bugfix: removed isdn_ppp_skb_destructor, used by upper layers.
 * Misc changes for Kernel 2.1.X compatibility.
 *
 * Revision 1.20  1996/10/30 12:21:58  fritz
 * Cosmetic fix: Compiler warning when compiling without MPP.
 *
 * Revision 1.19  1996/10/25 19:03:21  hipp
 * changed/added some defines to (re)allow compilation without MP/VJ
 *
 * Revision 1.18  1996/10/22 23:14:00  fritz
 * Changes for compatibility to 2.0.X and 2.1.X kernels.
 *
 * Revision 1.17  1996/10/22 09:39:49  hipp
 * a few MP changes and bugfixes
 *
 * Revision 1.16  1996/09/23 01:58:10  fritz
 * Fix: With syncPPP encapsulation, discard LCP packets
 *      when calculating hangup timeout.
 *
 * Revision 1.15  1996/09/07 12:50:12  hipp
 * bugfixes (unknown device after failed dial attempt, minor bugs)
 *
 * Revision 1.14  1996/08/12 16:26:47  hipp
 * code cleanup
 * changed connection management from minors to slots
 *
 * Revision 1.13  1996/07/01 19:47:24  hipp
 * Fixed memory leak in VJ handling and more VJ changes
 *
 * Revision 1.12  1996/06/24 17:42:03  fritz
 * Minor bugfixes.
 *
 * Revision 1.11  1996/06/16 17:46:05  tsbogend
 * changed unsigned long to u32 to make Alpha people happy
 *
 * Revision 1.10  1996/06/11 14:50:29  hipp
 * Lot of changes and bugfixes.
 * New scheme to resend packets to busy LL devices.
 *
 * Revision 1.9  1996/05/18 01:37:01  fritz
 * Added spelling corrections and some minor changes
 * to stay in sync with kernel.
 *
 * Revision 1.8  1996/05/06 11:34:55  hipp
 * fixed a few bugs
 *
 * Revision 1.7  1996/04/30 11:07:42  fritz
 * Added Michael's ippp-bind patch.
 *
 * Revision 1.6  1996/04/30 09:33:09  fritz
 * Removed compatibility-macros.
 *
 * Revision 1.5  1996/04/20 16:32:32  fritz
 * Changed ippp_table to an array of pointers, allocating each part
 * separately.
 *
 * Revision 1.4  1996/02/19 15:25:50  fritz
 * Bugfix: Sync-PPP packets got compressed twice, when resent due to
 * send-queue-full reject.
 *
 * Revision 1.3  1996/02/11 02:27:12  fritz
 * Lot of Bugfixes my Michael.
 * Moved calls to skb_push() into isdn_net_header()
 * Fixed a possible race-condition in isdn_ppp_timer_timeout().
 *
 * Revision 1.2  1996/01/22 05:08:06  fritz
 * Merged in Michael's patches for MP.
 * Minor changes in isdn_ppp_xmit.
 *
 * Revision 1.1  1996/01/09 04:11:29  fritz
 * Initial revision
 *
 */

/* TODO: right tbusy handling when using MP */

/*
 * experimental for dynamic addressing: readdress IP frames
 */
#undef ISDN_SYNCPPP_READDRESS
#define CONFIG_ISDN_CCP 1

#include <linux/config.h>
#define __NO_VERSION__
#include <linux/module.h>
#include <linux/version.h>
#include <linux/poll.h>
#include <linux/isdn.h>
#include <linux/ppp-comp.h>

#include "isdn_common.h"
#include "isdn_ppp.h"
#include "isdn_net.h"

#ifndef PPP_IPX
#define PPP_IPX 0x002b
#endif

/* set this if you use dynamic addressing */

/* Prototypes */
static int isdn_ppp_fill_rq(unsigned char *buf, int len, int proto, int slot);
static int isdn_ppp_closewait(int slot);
static void isdn_ppp_push_higher(isdn_net_dev * net_dev, isdn_net_local * lp,
				 struct sk_buff *skb, int proto);
static int isdn_ppp_if_get_unit(char *namebuf);
static int isdn_ppp_set_compressor(struct ippp_struct *is,struct isdn_ppp_comp_data *);
static struct sk_buff *isdn_ppp_decompress(struct sk_buff *,
				struct ippp_struct *,struct ippp_struct *,int proto);
static void isdn_ppp_receive_ccp(isdn_net_dev * net_dev, isdn_net_local * lp,
				struct sk_buff *skb,int proto);
static struct sk_buff *isdn_ppp_compress(struct sk_buff *skb_in,int *proto,
	struct ippp_struct *is,struct ippp_struct *master,int type);
static void isdn_ppp_send_ccp(isdn_net_dev *net_dev, isdn_net_local *lp,
	 struct sk_buff *skb);

/* New CCP stuff */
static void isdn_ppp_ccp_kickup(struct ippp_struct *is);
static void isdn_ppp_ccp_xmit_reset(struct ippp_struct *is, int proto,
				    unsigned char code, unsigned char id,
				    unsigned char *data, int len);
static struct ippp_ccp_reset *isdn_ppp_ccp_reset_alloc(struct ippp_struct *is);
static void isdn_ppp_ccp_reset_free_state(struct ippp_struct *is,
					  unsigned char id);
static void isdn_ppp_ccp_timer_callback(unsigned long closure);
static struct ippp_ccp_reset_state *isdn_ppp_ccp_reset_alloc_state(struct ippp_struct *is,
						      unsigned char id);
static void isdn_ppp_ccp_reset_trans(struct ippp_struct *is,
				     struct isdn_ppp_resetparams *rp);
static void isdn_ppp_ccp_reset_ack_rcvd(struct ippp_struct *is,
					unsigned char id);



#ifdef CONFIG_ISDN_MPP
static int isdn_ppp_bundle(struct ippp_struct *, int unit);
static void isdn_ppp_mask_queue(isdn_net_dev * dev, long mask);
static void isdn_ppp_cleanup_mpqueue(isdn_net_dev * dev, long min);
static void isdn_ppp_cleanup_sqqueue(isdn_net_dev * dev, isdn_net_local *, long min);
static void isdn_ppp_free_sqqueue(isdn_net_dev *);
static int isdn_ppp_fill_mpqueue(isdn_net_dev *, struct sk_buff **skb,
				 int BEbyte, long *sqno, int min_sqno);
static void isdn_ppp_free_mpqueue(isdn_net_dev *);
#endif

char *isdn_ppp_revision = "$Revision: 1.49 $";

static struct ippp_struct *ippp_table[ISDN_MAX_CHANNELS];
static struct isdn_ppp_compressor *ipc_head = NULL;

/*
 * frame log (debug)
 */
static void
isdn_ppp_frame_log(char *info, char *data, int len, int maxlen,int unit,int slot)
{
	int cnt,
	 j,
	 i;
	char buf[80];

	if (len < maxlen)
		maxlen = len;

	for (i = 0, cnt = 0; cnt < maxlen; i++) {
		for (j = 0; j < 16 && cnt < maxlen; j++, cnt++)
			sprintf(buf + j * 3, "%02x ", (unsigned char) data[cnt]);
		printk(KERN_DEBUG "[%d/%d].%s[%d]: %s\n",unit,slot, info, i, buf);
	}
}

/*
 * unbind isdn_net_local <=> ippp-device
 * note: it can happen, that we hangup/free the master before the slaves
 *       in this case we bind another lp to the master device
 */
int
isdn_ppp_free(isdn_net_local * lp)
{
#ifdef CONFIG_ISDN_MPP
	isdn_net_local *master_lp = lp;
#endif
	unsigned long flags;
	struct ippp_struct *is;

	if (lp->ppp_slot < 0 || lp->ppp_slot > ISDN_MAX_CHANNELS)
		return 0;

	is = ippp_table[lp->ppp_slot];

	save_flags(flags);
	cli();
#ifdef CONFIG_ISDN_MPP
	if (lp->master)
		master_lp = (isdn_net_local *) lp->master->priv;

	lp->last->next = lp->next;
	lp->next->last = lp->last;
	if (master_lp->netdev->queue == lp) {
		master_lp->netdev->queue = lp->next;
		if (lp->next == lp) {	/* last link in queue? */
			master_lp->netdev->ib.bundled = 0;
			isdn_ppp_free_mpqueue(master_lp->netdev);
			isdn_ppp_free_sqqueue(master_lp->netdev);
		}
	}
	lp->next = lp->last = lp;	/* (re)set own pointers */
#endif

	if ((is->state & IPPP_CONNECT))
		isdn_ppp_closewait(lp->ppp_slot);	/* force wakeup on ippp device */
	else if (is->state & IPPP_ASSIGNED)
		is->state = IPPP_OPEN;	/* fallback to 'OPEN but not ASSIGNED' state */

	if (is->debug & 0x1)
		printk(KERN_DEBUG "isdn_ppp_free %d %lx %lx\n", lp->ppp_slot, (long) lp, (long) is->lp);

	is->lp = NULL;          /* link is down .. set lp to NULL */
#ifdef ISDN_SYNCPPP_READDRESS
	is->old_pa_addr = 0x0;
	is->old_pa_dstaddr = 0x0;
#endif
	lp->ppp_slot = -1;      /* is this OK ?? */
	restore_flags(flags);

	return 0;
}

/*
 * bind isdn_net_local <=> ippp-device
 */
int
isdn_ppp_bind(isdn_net_local * lp)
{
	int i;
	int unit = 0;
	long flags;
	struct ippp_struct *is;

	if (lp->p_encap != ISDN_NET_ENCAP_SYNCPPP)
		return -1;

	save_flags(flags);
	cli();

	if (lp->pppbind < 0) {  /* device bounded to ippp device ? */
		isdn_net_dev *net_dev = dev->netdev;
		char exclusive[ISDN_MAX_CHANNELS];	/* exclusive flags */
		memset(exclusive, 0, ISDN_MAX_CHANNELS);
		while (net_dev) {	/* step through net devices to find exclusive minors */
			isdn_net_local *lp = net_dev->local;
			if (lp->pppbind >= 0)
				exclusive[lp->pppbind] = 1;
			net_dev = net_dev->next;
		}
		/*
		 * search a free device / slot
		 */
		for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
			if (ippp_table[i]->state == IPPP_OPEN && !exclusive[ippp_table[i]->minor]) {	/* OPEN, but not connected! */
				break;
			}
		}
	} else {
		for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
			if (ippp_table[i]->minor == lp->pppbind &&
			    (ippp_table[i]->state & IPPP_OPEN) == IPPP_OPEN)
				break;
		}
	}

	if (i >= ISDN_MAX_CHANNELS) {
		restore_flags(flags);
		printk(KERN_WARNING "isdn_ppp_bind: Can't find a (free) connection to the ipppd daemon.\n");
		return -1;
	}
	unit = isdn_ppp_if_get_unit(lp->name);	/* get unit number from interface name .. ugly! */
	if (unit < 0) {
		printk(KERN_ERR "isdn_ppp_bind: illegal interface name %s.\n", lp->name);
		return -1;
	}
	lp->ppp_slot = i;

	/* reset some values */
	lp->netdev->ib.bundled = 0;
	lp->netdev->ib.next_num = 0;
	lp->netdev->ib.modify = 0;
	lp->netdev->ib.last = NULL;
	lp->netdev->ib.min = 0;
	lp->netdev->ib.sq = NULL;

	is = ippp_table[i];
	is->lp = lp;
	is->unit = unit;
	is->state = IPPP_OPEN | IPPP_ASSIGNED;	/* assigned to a netdevice but not connected */

	restore_flags(flags);

	return lp->ppp_slot;
}

/*
 * kick the ipppd on the device
 * (wakes up daemon after B-channel connect)
 */

void
isdn_ppp_wakeup_daemon(isdn_net_local * lp)
{
	if (lp->ppp_slot < 0 || lp->ppp_slot >= ISDN_MAX_CHANNELS)
		return;

	ippp_table[lp->ppp_slot]->state = IPPP_OPEN | IPPP_CONNECT | IPPP_NOBLOCK;

#ifndef COMPAT_HAS_NEW_WAITQ
	if (ippp_table[lp->ppp_slot]->wq)
#endif
		wake_up_interruptible(&ippp_table[lp->ppp_slot]->wq);
}

/*
 * there was a hangup on the netdevice
 * force wakeup of the ippp device
 * go into 'device waits for release' state
 */
static int
isdn_ppp_closewait(int slot)
{
	struct ippp_struct *is;

	if (slot < 0 || slot >= ISDN_MAX_CHANNELS)
		return 0;
	is = ippp_table[slot];

#ifdef COMPAT_HAS_NEW_WAITQ
	if (is->state)
#else
	if (is->state && is->wq)
#endif
		wake_up_interruptible(&is->wq);

	is->state = IPPP_CLOSEWAIT;
	return 1;
}

/*
 * isdn_ppp_find_slot / isdn_ppp_free_slot
 */

static int
isdn_ppp_get_slot(void)
{
	int i;
	for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
		if (!ippp_table[i]->state)
			return i;
	}
	return -1;
}

/*
 * isdn_ppp_open
 */

int
isdn_ppp_open(int min, struct file *file)
{
	int slot;
	struct ippp_struct *is;

	if (min < 0 || min > ISDN_MAX_CHANNELS)
		return -ENODEV;

	slot = isdn_ppp_get_slot();
	if (slot < 0) {
		return -EBUSY;
	}
	is = file->private_data = ippp_table[slot];

#if 0
	if (is->debug & 0x1)
#endif
		printk(KERN_DEBUG "ippp, open, slot: %d, minor: %d, state: %04x\n", slot, min, is->state);

	/* compression stuff */
	is->link_compressor   = is->compressor = NULL;
	is->link_decompressor = is->decompressor = NULL;
	is->link_comp_stat    = is->comp_stat = NULL;
	is->link_decomp_stat  = is->decomp_stat = NULL;
	is->compflags = 0;

	is->reset = isdn_ppp_ccp_reset_alloc(is);

	is->lp = NULL;
	is->mp_seqno = 0;       /* MP sequence number */
	is->pppcfg = 0;         /* ppp configuration */
	is->mpppcfg = 0;        /* mppp configuration */
	is->range = 0x1000000;  /* MP: 24 bit range */
	is->last_link_seqno = -1;	/* MP: maybe set to Bundle-MIN, when joining a bundle ?? */
	is->unit = -1;          /* set, when we have our interface */
	is->mru = 1524;         /* MRU, default 1524 */
	is->maxcid = 16;        /* VJ: maxcid */
	is->tk = current;
#ifdef COMPAT_HAS_NEW_WAITQ
	init_waitqueue_head(&is->wq);
#else
	is->wq = NULL;          /* read() wait queue */
#endif
	is->first = is->rq + NUM_RCV_BUFFS - 1;	/* receive queue */
	is->last = is->rq;
	is->minor = min;
#ifdef CONFIG_ISDN_PPP_VJ
	/*
	 * VJ header compression init
	 */
	is->slcomp = slhc_init(16, 16);	/* not necessary for 2. link in bundle */
#endif

	is->state = IPPP_OPEN;

	return 0;
}

/*
 * release ippp device
 */
void
isdn_ppp_release(int min, struct file *file)
{
	int i;
	struct ippp_struct *is;

	if (min < 0 || min >= ISDN_MAX_CHANNELS)
		return;
	is = file->private_data;

	if (is->debug & 0x1)
		printk(KERN_DEBUG "ippp: release, minor: %d %lx\n", min, (long) is->lp);

	if (is->lp) {           /* a lp address says: this link is still up */
		isdn_net_dev *p = is->lp->netdev;

		is->state &= ~IPPP_CONNECT;	/* -> effect: no call of wakeup */
		/*
		 * isdn_net_hangup() calls isdn_ppp_free()
		 * isdn_ppp_free() sets is->lp to NULL and lp->ppp_slot to -1
		 * removing the IPPP_CONNECT flag omits calling of isdn_ppp_wakeup_daemon()
		 */
		isdn_net_hangup(&p->dev);
	}
	for (i = 0; i < NUM_RCV_BUFFS; i++) {
		if (is->rq[i].buf) {
			kfree(is->rq[i].buf);
			is->rq[i].buf = NULL;
		}
	}
	is->first = is->rq + NUM_RCV_BUFFS - 1;	/* receive queue */
	is->last = is->rq;

#ifdef CONFIG_ISDN_PPP_VJ
/* TODO: if this was the previous master: link the slcomp to the new master */
	slhc_free(is->slcomp);
	is->slcomp = NULL;
#endif

/* TODO: if this was the previous master: link the the stuff to the new master */
	if(is->comp_stat)
		is->compressor->free(is->comp_stat);
	if(is->link_comp_stat)
		is->link_compressor->free(is->link_comp_stat);
	if(is->link_decomp_stat)
		is->link_decompressor->free(is->link_decomp_stat);
	if(is->decomp_stat)
		is->decompressor->free(is->decomp_stat);
        is->compressor   = is->link_compressor   = NULL;
        is->decompressor = is->link_decompressor = NULL;
	is->comp_stat    = is->link_comp_stat    = NULL;
        is->decomp_stat  = is->link_decomp_stat  = NULL;

	if(is->reset)
		kfree(is->reset);
	is->reset = NULL;

	/* this slot is ready for new connections */
	is->state = 0;
}

/*
 * get_arg .. ioctl helper
 */
static int
get_arg(void *b, void *val, int len)
{
	if (len <= 0)
		len = sizeof(void *);
	if (copy_from_user((void *) val, b, len))
		return -EFAULT;
	return 0;
}

/*
 * set arg .. ioctl helper
 */
static int
set_arg(void *b, void *val,int len)
{
	if(len <= 0)
		len = sizeof(void *);
	if (copy_to_user(b, (void *) val, len))
		return -EFAULT;
	return 0;
}

/*
 * ippp device ioctl
 */
int
isdn_ppp_ioctl(int min, struct file *file, unsigned int cmd, unsigned long arg)
{
	unsigned long val;
	int r,i,j;
	struct ippp_struct *is;
	isdn_net_local *lp;
	struct isdn_ppp_comp_data data;

	is = (struct ippp_struct *) file->private_data;
	lp = is->lp;

	if (is->debug & 0x1)
		printk(KERN_DEBUG "isdn_ppp_ioctl: minor: %d cmd: %x state: %x\n", min, cmd, is->state);

	if (!(is->state & IPPP_OPEN))
		return -EINVAL;

	switch (cmd) {
		case PPPIOCBUNDLE:
#ifdef CONFIG_ISDN_MPP
			if (!(is->state & IPPP_CONNECT))
				return -EINVAL;
			if ((r = get_arg((void *) arg, &val, sizeof(val) )))
				return r;
			printk(KERN_DEBUG "iPPP-bundle: minor: %d, slave unit: %d, master unit: %d\n",
			       (int) min, (int) is->unit, (int) val);
			return isdn_ppp_bundle(is, val);
#else
			return -1;
#endif
			break;
		case PPPIOCGUNIT:	/* get ppp/isdn unit number */
			if ((r = set_arg((void *) arg, &is->unit, sizeof(is->unit) )))
				return r;
			break;
		case PPPIOCGIFNAME:
			if(!lp)
				return -EINVAL;
			if ((r = set_arg((void *) arg, lp->name,strlen(lp->name))))
				return r;
			break;
		case PPPIOCGMPFLAGS:	/* get configuration flags */
			if ((r = set_arg((void *) arg, &is->mpppcfg, sizeof(is->mpppcfg) )))
				return r;
			break;
		case PPPIOCSMPFLAGS:	/* set configuration flags */
			if ((r = get_arg((void *) arg, &val, sizeof(val) )))
				return r;
			is->mpppcfg = val;
			break;
		case PPPIOCGFLAGS:	/* get configuration flags */
			if ((r = set_arg((void *) arg, &is->pppcfg,sizeof(is->pppcfg) )))
				return r;
			break;
		case PPPIOCSFLAGS:	/* set configuration flags */
			if ((r = get_arg((void *) arg, &val, sizeof(val) ))) {
				return r;
			}
			if (val & SC_ENABLE_IP && !(is->pppcfg & SC_ENABLE_IP) && (is->state & IPPP_CONNECT)) {
				if (lp) {
					lp->netdev->dev.tbusy = 0;
					mark_bh(NET_BH);	/* OK .. we are ready to send buffers */
				}
			}
			is->pppcfg = val;
			break;
#if 0
		case PPPIOCGSTAT:	/* read PPP statistic information */
			break;
#endif
		case PPPIOCGIDLE:	/* get idle time information */
			if (lp) {
				struct ppp_idle pidle;
				pidle.xmit_idle = pidle.recv_idle = lp->huptimer;
				if ((r = set_arg((void *) arg, &pidle,sizeof(struct ppp_idle))))
					 return r;
			}
			break;
		case PPPIOCSMRU:	/* set receive unit size for PPP */
			if ((r = get_arg((void *) arg, &val, sizeof(val) )))
				return r;
			is->mru = val;
			break;
		case PPPIOCSMPMRU:
			break;
		case PPPIOCSMPMTU:
			break;
		case PPPIOCSMAXCID:	/* set the maximum compression slot id */
			if ((r = get_arg((void *) arg, &val, sizeof(val) )))
				return r;
			val++;
			if (is->maxcid != val) {
#ifdef CONFIG_ISDN_PPP_VJ
				struct slcompress *sltmp;
#endif
				if (is->debug & 0x1)
					printk(KERN_DEBUG "ippp, ioctl: changed MAXCID to %ld\n", val);
				is->maxcid = val;
#ifdef CONFIG_ISDN_PPP_VJ
				sltmp = slhc_init(16, val);
				if (!sltmp) {
					printk(KERN_ERR "ippp, can't realloc slhc struct\n");
					return -ENOMEM;
				}
				if (is->slcomp)
					slhc_free(is->slcomp);
				is->slcomp = sltmp;
#endif
			}
			break;
		case PPPIOCGDEBUG:
			if ((r = set_arg((void *) arg, &is->debug, sizeof(is->debug) )))
				return r;
			break;
		case PPPIOCSDEBUG:
			if ((r = get_arg((void *) arg, &val, sizeof(val) )))
				return r;
			is->debug = val;
			break;
		case PPPIOCGCOMPRESSORS:
			{
				unsigned long protos[8] = {0,};
				struct isdn_ppp_compressor *ipc = ipc_head;
				while(ipc) {
					j = ipc->num / (sizeof(long)*8);
					i = ipc->num % (sizeof(long)*8);
					if(j < 8)
						protos[j] |= (0x1<<i);
					ipc = ipc->next;
				}
				if ((r = set_arg((void *) arg,protos,8*sizeof(long) )))
					return r;
			}
			break;
		case PPPIOCSCOMPRESSOR:
			if ((r = get_arg((void *) arg, &data, sizeof(struct isdn_ppp_comp_data))))
				return r;
			return isdn_ppp_set_compressor(is, &data);
		case PPPIOCGCALLINFO:
			{
				struct pppcallinfo pci;
				memset((char *) &pci,0,sizeof(struct pppcallinfo));
				if(lp)
				{
					strncpy(pci.local_num,lp->msn,63);
					if(lp->dial) {
						strncpy(pci.remote_num,lp->dial->num,63);
					}
					pci.charge_units = lp->charge;
					if(lp->outgoing)
						pci.calltype = CALLTYPE_OUTGOING;
					else
						pci.calltype = CALLTYPE_INCOMING;
					if(lp->flags & ISDN_NET_CALLBACK)
						pci.calltype |= CALLTYPE_CALLBACK;
				}
				return set_arg((void *)arg,&pci,sizeof(struct pppcallinfo));
			}
		default:
			break;
	}
	return 0;
}

unsigned int
isdn_ppp_poll(struct file *file, poll_table * wait)
{
	unsigned int mask;
	struct ippp_buf_queue *bf;
	struct ippp_buf_queue *bl;
	unsigned long flags;
	struct ippp_struct *is;

	is = file->private_data;

	if (is->debug & 0x2)
		printk(KERN_DEBUG "isdn_ppp_poll: minor: %d\n",
				MINOR(file->f_dentry->d_inode->i_rdev));

	/* just registers wait_queue hook. This doesn't really wait. */
	poll_wait(file, &is->wq, wait);

	if (!(is->state & IPPP_OPEN)) {
		if(is->state == IPPP_CLOSEWAIT)
			return POLLHUP;
		printk(KERN_DEBUG "isdn_ppp: device not open\n");
		return POLLERR;
	}
	/* we're always ready to send .. */
	mask = POLLOUT | POLLWRNORM;

	save_flags(flags);
	cli();
	bl = is->last;
	bf = is->first;
	/*
	 * if IPPP_NOBLOCK is set we return even if we have nothing to read
	 */
	if (bf->next != bl || (is->state & IPPP_NOBLOCK)) {
		is->state &= ~IPPP_NOBLOCK;
		mask |= POLLIN | POLLRDNORM;
	}
	restore_flags(flags);
	return mask;
}

/*
 *  fill up isdn_ppp_read() queue ..
 */

static int
isdn_ppp_fill_rq(unsigned char *buf, int len, int proto, int slot)
{
	struct ippp_buf_queue *bf,
	*bl;
	unsigned long flags;
	unsigned char *nbuf;
	struct ippp_struct *is;

	if (slot < 0 || slot >= ISDN_MAX_CHANNELS) {
		printk(KERN_WARNING "ippp: illegal slot.\n");
		return 0;
	}
	is = ippp_table[slot];

	if (!(is->state & IPPP_CONNECT)) {
		printk(KERN_DEBUG "ippp: device not activated.\n");
		return 0;
	}
	nbuf = (unsigned char *) kmalloc(len + 4, GFP_ATOMIC);
	if (!nbuf) {
		printk(KERN_WARNING "ippp: Can't alloc buf\n");
		return 0;
	}
	nbuf[0] = PPP_ALLSTATIONS;
	nbuf[1] = PPP_UI;
	nbuf[2] = proto >> 8;
	nbuf[3] = proto & 0xff;
	memcpy(nbuf + 4, buf, len);

	save_flags(flags);
	cli();

	bf = is->first;
	bl = is->last;

	if (bf == bl) {
		printk(KERN_WARNING "ippp: Queue is full; discarding first buffer\n");
		bf = bf->next;
		kfree(bf->buf);
		is->first = bf;
	}
	bl->buf = (char *) nbuf;
	bl->len = len + 4;

	is->last = bl->next;
	restore_flags(flags);

#ifndef COMPAT_HAS_NEW_WAITQ
	if (is->wq)
#endif
		wake_up_interruptible(&is->wq);

	return len;
}

/*
 * read() .. non-blocking: ipppd calls it only after select()
 *           reports, that there is data
 */

int
isdn_ppp_read(int min, struct file *file, char *buf, int count)
{
	struct ippp_struct *is;
	struct ippp_buf_queue *b;
	int r;
	unsigned long flags;
	unsigned char *save_buf;

	is = file->private_data;

	if (!(is->state & IPPP_OPEN))
		return 0;

	if ((r = verify_area(VERIFY_WRITE, (void *) buf, count)))
		return r;

	save_flags(flags);
	cli();

	b = is->first->next;
	save_buf = b->buf;
	if (!save_buf) {
		restore_flags(flags);
		return -EAGAIN;
	}
	if (b->len < count)
		count = b->len;
	b->buf = NULL;
	is->first = b;

	restore_flags(flags);

	copy_to_user(buf, save_buf, count);
	kfree(save_buf);

	return count;
}

/*
 * ipppd wanna write a packet to the card .. non-blocking
 */

int
isdn_ppp_write(int min, struct file *file, const char *buf, int count)
{
	isdn_net_local *lp;
	struct ippp_struct *is;
	int proto;
	unsigned char protobuf[4];

	is = file->private_data;

	if (!(is->state & IPPP_CONNECT))
		return 0;

	lp = is->lp;

	/* -> push it directly to the lowlevel interface */

	if (!lp)
		printk(KERN_DEBUG "isdn_ppp_write: lp == NULL\n");
	else {
		/*
		 * Don't reset huptimer for
		 * LCP packets. (Echo requests).
		 */
		if (copy_from_user(protobuf, buf, 4))
			return -EFAULT;
		proto = PPP_PROTOCOL(protobuf);
		if (proto != PPP_LCP)
			lp->huptimer = 0;

		if (lp->isdn_device < 0 || lp->isdn_channel < 0)
			return 0;

		if ((dev->drv[lp->isdn_device]->flags & DRV_FLAG_RUNNING) &&
			lp->dialstate == 0 &&
		    (lp->flags & ISDN_NET_CONNECTED)) {
			unsigned short hl;
			int cnt;
			struct sk_buff *skb;
			/*
			 * we need to reserve enought space in front of
			 * sk_buff. old call to dev_alloc_skb only reserved
			 * 16 bytes, now we are looking what the driver want
			 */
			hl = dev->drv[lp->isdn_device]->interface->hl_hdrlen;
			skb = alloc_skb(hl+count, GFP_ATOMIC);
			if (!skb) {
				printk(KERN_WARNING "isdn_ppp_write: out of memory!\n");
				return count;
			}
			skb_reserve(skb, hl);
			if (copy_from_user(skb_put(skb, count), buf, count))
				return -EFAULT;
			if (is->debug & 0x40) {
				printk(KERN_DEBUG "ppp xmit: len %d\n", (int) skb->len);
				isdn_ppp_frame_log("xmit", skb->data, skb->len, 32,is->unit,lp->ppp_slot);
			}

			isdn_ppp_send_ccp(lp->netdev,lp,skb); /* keeps CCP/compression states in sync */

			if ((cnt = isdn_writebuf_skb_stub(lp->isdn_device, lp->isdn_channel, 1, skb)) != count) {
				if (lp->sav_skb) {
					dev_kfree_skb(lp->sav_skb);
					printk(KERN_INFO "isdn_ppp_write: freeing sav_skb (%d,%d)!\n", cnt, count);
				} else
					printk(KERN_INFO "isdn_ppp_write: Can't write PPP frame to LL (%d,%d)!\n", cnt, count);
				lp->sav_skb = skb;
			}
		}
	}
	return count;
}

/*
 * init memory, structures etc.
 */

int
isdn_ppp_init(void)
{
	int i,
	 j;

	for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
		if (!(ippp_table[i] = (struct ippp_struct *)
		      kmalloc(sizeof(struct ippp_struct), GFP_KERNEL))) {
			printk(KERN_WARNING "isdn_ppp_init: Could not alloc ippp_table\n");
			for (j = 0; j < i; j++)
				kfree(ippp_table[i]);
			return -1;
		}
		memset((char *) ippp_table[i], 0, sizeof(struct ippp_struct));
		ippp_table[i]->state = 0;
		ippp_table[i]->first = ippp_table[i]->rq + NUM_RCV_BUFFS - 1;
		ippp_table[i]->last = ippp_table[i]->rq;

		for (j = 0; j < NUM_RCV_BUFFS; j++) {
			ippp_table[i]->rq[j].buf = NULL;
			ippp_table[i]->rq[j].last = ippp_table[i]->rq +
			    (NUM_RCV_BUFFS + j - 1) % NUM_RCV_BUFFS;
			ippp_table[i]->rq[j].next = ippp_table[i]->rq + (j + 1) % NUM_RCV_BUFFS;
		}
	}
	return 0;
}

void
isdn_ppp_cleanup(void)
{
	int i;

	for (i = 0; i < ISDN_MAX_CHANNELS; i++)
		kfree(ippp_table[i]);
}

/*
 * get the PPP protocol header and pull skb
 */
static int isdn_ppp_strip_proto(struct sk_buff *skb) 
{
	int proto;
	if (skb->data[0] & 0x1) {
		proto = skb->data[0];
		skb_pull(skb, 1);   /* protocol ID is only 8 bit */
	} else {
		proto = ((int) skb->data[0] << 8) + skb->data[1];
		skb_pull(skb, 2);
	}
	return proto;
}


/*
 * handler for incoming packets on a syncPPP interface
 */
void isdn_ppp_receive(isdn_net_dev * net_dev, isdn_net_local * lp, struct sk_buff *skb)
{
	struct ippp_struct *is;
	int proto;

	is = ippp_table[lp->ppp_slot];

	if (is->debug & 0x4) {
		printk(KERN_DEBUG "ippp_receive: is:%08lx lp:%08lx slot:%d unit:%d len:%d\n",
		       (long)is,(long)lp,lp->ppp_slot,is->unit,(int) skb->len);
		isdn_ppp_frame_log("receive", skb->data, skb->len, 32,is->unit,lp->ppp_slot);
	}
	if (net_dev->local->master) {
		printk(KERN_WARNING "isdn_ppp_receice: net_dev != master\n");
		net_dev = ((isdn_net_local *) net_dev->local->master->priv)->netdev;
	}
	if (skb->data[0] == 0xff && skb->data[1] == 0x03)
		skb_pull(skb, 2);
	else if (is->pppcfg & SC_REJ_COMP_AC) {
		dev_kfree_skb(skb);
		return;         /* discard it silently */
	}

	proto = isdn_ppp_strip_proto(skb);

#ifdef CONFIG_ISDN_MPP
	if (!(is->mpppcfg & SC_REJ_MP_PROT)) {
		int sqno_end;

		if(is->compflags & SC_LINK_DECOMP_ON) {	
			if(proto == PPP_LINK_COMP) {
				if(is->debug & 0x10)
					printk(KERN_DEBUG "received single link compressed frame\n");
				skb = isdn_ppp_decompress(skb,is,NULL,proto);
				if(!skb)
					return;
				proto = isdn_ppp_strip_proto(skb);
			}
			else
				isdn_ppp_decompress(skb,is,NULL,proto);
		}

		if (proto == PPP_MP) {
			isdn_net_local *lpq;
			long sqno, min_sqno, tseq;

			u_char BEbyte = skb->data[0];
			if (is->debug & 0x8)
				printk(KERN_DEBUG "recv: %d/%04x/%d -> %02x %02x %02x %02x %02x %02x\n", lp->ppp_slot, proto,
				       (int) skb->len, (int) skb->data[0], (int) skb->data[1], (int) skb->data[2],
				       (int) skb->data[3], (int) skb->data[4], (int) skb->data[5]);
			if (!(is->mpppcfg & SC_IN_SHORT_SEQ)) {
				sqno = ((int) skb->data[1] << 16) + ((int) skb->data[2] << 8) + (int) skb->data[3];
				skb_pull(skb, 4);
			} else {
				sqno = (((int) skb->data[0] & 0xf) << 8) + (int) skb->data[1];
				skb_pull(skb, 2);
			}

			/*
			 * new sequence number lower than last number? (this is only allowed
			 * for overflow case)
			 */
			if ((tseq = is->last_link_seqno) >= sqno) {
				int range = is->range;
				if (tseq + 1024 < range + sqno)	/* redundancy check .. not MP conform */
					printk(KERN_WARNING "isdn_ppp_receive, MP, detected overflow with sqno: %ld, last: %ld !!!\n", sqno, tseq);
				else {
					sqno += range;
					is->last_link_seqno = sqno;
				}
			} else {
				/* here, we should also add an redundancy check */
				is->last_link_seqno = sqno;
			}

			/* 
			 * step over all links to find lowest link number
			 */
			for (min_sqno = LONG_MAX, lpq = net_dev->queue;;) {
				long lls = ippp_table[lpq->ppp_slot]->last_link_seqno;
				if (lls >= 0 && lls < min_sqno)
					min_sqno = lls;
				lpq = lpq->next;
				if (lpq == net_dev->queue)
					break;
			}

			/*
			 * for the case, that the last frame numbers of all 
			 * links are overflowed: mask/reduce the sequenece number to
			 * 'normal' numbering.
			 */
			if (min_sqno >= ippp_table[lpq->ppp_slot]->range) {
				int mask = ippp_table[lpq->ppp_slot]->range-1;	/* range is power of two, so a mask will do the job */
				isdn_ppp_mask_queue(net_dev, mask);
				net_dev->ib.next_num &= mask;
				{
					struct sqqueue *q = net_dev->ib.sq;
					while (q) {
						q->sqno_start &= mask;
						q->sqno_end &= mask;
					}
				}
				min_sqno &= mask;
				for (lpq = net_dev->queue;;) {
					if(ippp_table[lpq->ppp_slot]->last_link_seqno >= 0)
						ippp_table[lpq->ppp_slot]->last_link_seqno &= mask;
					lpq = lpq->next;
					if (lpq == net_dev->queue)
						break;
				}
			}
			if ((BEbyte & (MP_BEGIN_FRAG | MP_END_FRAG)) != (MP_BEGIN_FRAG | MP_END_FRAG)) {
				static int dmes = 0;
				if( !dmes ) {
					printk(KERN_DEBUG "ippp: trying ;) to fill mp_queue %d .. UNTESTED!!\n", lp->ppp_slot);
					dmes = 1;
				}
				if ((sqno_end = isdn_ppp_fill_mpqueue(net_dev, &skb, BEbyte, &sqno, min_sqno)) < 0) {
					net_dev->ib.modify = 1;	/* block timeout-timer */
					isdn_ppp_cleanup_sqqueue(net_dev, lp, min_sqno);
					net_dev->ib.modify = 0;
					return;	/* no packet complete */
				}
			} else
				sqno_end = sqno;

			if (is->debug & 0x40)
				printk(KERN_DEBUG "min_sqno: %ld sqno_end %d next: %ld\n", min_sqno, sqno_end, net_dev->ib.next_num);

			/*
			 * MP buffer management .. reorders incoming packets ..
			 * lotsa mem-copies and not heavily tested.
			 *
			 * first check whether there is more than one link in the bundle
			 * then check whether the number is in order
			 */
			net_dev->ib.modify = 1;	/* block timeout-timer */
			if (net_dev->ib.bundled && net_dev->ib.next_num != sqno) {
				/*
				 * packet is not 'in order'
				 */
				struct sqqueue *q;

				q = (struct sqqueue *) kmalloc(sizeof(struct sqqueue), GFP_ATOMIC);
				if (!q) {
					net_dev->ib.modify = 0;
					printk(KERN_WARNING "ippp/MPPP: Bad! Can't alloc sq node!\n");
					dev_kfree_skb(skb);
					return;	/* discard */
				}
				q->skb = skb;
				q->sqno_end = sqno_end;
				q->sqno_start = sqno;
				q->timer = jiffies + (ISDN_TIMER_1SEC) * 5;	/* timeout after 5 seconds */

				if (!net_dev->ib.sq) {
					net_dev->ib.sq = q;
					q->next = NULL;
				} else {
					struct sqqueue *ql = net_dev->ib.sq;
					if (ql->sqno_start > q->sqno_start) {
						q->next = ql;
						net_dev->ib.sq = q;
					} else {
						while (ql->next && ql->next->sqno_start < q->sqno_start)
							ql = ql->next;
						q->next = ql->next;
						ql->next = q;
					}
				}
			} else {
				/*
				 * packet was 'in order' .. push it higher
				 */
				net_dev->ib.next_num = sqno_end + 1;
				proto = isdn_ppp_strip_proto(skb);
				isdn_ppp_push_higher(net_dev, lp, skb, proto);
			}
			isdn_ppp_cleanup_sqqueue(net_dev, lp, min_sqno);
			net_dev->ib.modify = 0;

		} else
			isdn_ppp_push_higher(net_dev, lp, skb, proto);
	} else
#endif
		isdn_ppp_push_higher(net_dev, lp, skb, proto);
}

/*
 * push frame to higher layers
 * note: net_dev has to be master net_dev
 */
static void
isdn_ppp_push_higher(isdn_net_dev * net_dev, isdn_net_local * lp, struct sk_buff *skb, int proto)
{
	struct device *dev = &net_dev->dev;
	struct ippp_struct *is = ippp_table[lp->ppp_slot];

	if (is->debug & 0x10) {
		printk(KERN_DEBUG "push, skb %d %04x\n", (int) skb->len, proto);
		isdn_ppp_frame_log("rpush", skb->data, skb->len, 32,is->unit,lp->ppp_slot);
	}

	if(proto == PPP_COMP) {
		if(!lp->master)
			skb = isdn_ppp_decompress(skb,is,is,proto);
		else
			skb = isdn_ppp_decompress(skb,is,ippp_table[((isdn_net_local *) (lp->master->priv))->ppp_slot],proto);

		if(!skb) {
			printk(KERN_DEBUG "ippp: compressed frame discarded!\n");
			return;
		}

		proto = isdn_ppp_strip_proto(skb);
		if (is->debug & 0x10) {
			printk(KERN_DEBUG "RPostDecomp, skb %d %04x\n", (int) skb->len, proto);
			isdn_ppp_frame_log("R-Decomp", skb->data, skb->len, 32,is->unit,lp->ppp_slot);
		}
	}
	else if(is->compflags & SC_DECOMP_ON)  { /* If decomp is ON */
		if(!lp->master)
			isdn_ppp_decompress(skb,is,is,proto);
		else
			isdn_ppp_decompress(skb,is,ippp_table[((isdn_net_local *) (lp->master->priv))->ppp_slot],proto);
	}

	switch (proto) {
		case PPP_IPX:  /* untested */
			if (is->debug & 0x20)
				printk(KERN_DEBUG "isdn_ppp: IPX\n");
			skb->dev = dev;
			skb->mac.raw = skb->data;
			skb->protocol = htons(ETH_P_IPX);
			break;
#ifdef CONFIG_ISDN_PPP_VJ
		case PPP_VJC_UNCOMP:
			if (is->debug & 0x20)
				printk(KERN_DEBUG "isdn_ppp: VJC_UNCOMP\n");
			if (slhc_remember(ippp_table[net_dev->local->ppp_slot]->slcomp, skb->data, skb->len) <= 0) {
				printk(KERN_WARNING "isdn_ppp: received illegal VJC_UNCOMP frame!\n");
				net_dev->local->stats.rx_dropped++;
				dev_kfree_skb(skb);
				return;
			}
#endif
		case PPP_IP:
			if (is->debug & 0x20)
				printk(KERN_DEBUG "isdn_ppp: IP\n");
			skb->dev = dev;
			skb->mac.raw = skb->data;
			skb->protocol = htons(ETH_P_IP);
			break;
		case PPP_VJC_COMP:
			if (is->debug & 0x20)
				printk(KERN_DEBUG "isdn_ppp: VJC_COMP\n");
#ifdef CONFIG_ISDN_PPP_VJ
			{
				struct sk_buff *skb_old = skb;
				int pkt_len;
				skb = dev_alloc_skb(skb_old->len + 40);

				if (!skb) {
					printk(KERN_WARNING "%s: Memory squeeze, dropping packet.\n", dev->name);
					net_dev->local->stats.rx_dropped++;
					dev_kfree_skb(skb_old);
					return;
				}
				skb->dev = dev;
				skb_put(skb, skb_old->len + 40);
				memcpy(skb->data, skb_old->data, skb_old->len);
				skb->mac.raw = skb->data;
				pkt_len = slhc_uncompress(ippp_table[net_dev->local->ppp_slot]->slcomp,
						skb->data, skb_old->len);
				dev_kfree_skb(skb_old);
				if (pkt_len < 0) {
					dev_kfree_skb(skb);
					lp->stats.rx_dropped++;
					return;
				}
				skb_trim(skb, pkt_len);
				skb->protocol = htons(ETH_P_IP);
			}
#else
			printk(KERN_INFO "isdn: Ooopsa .. VJ-Compression support not compiled into isdn driver.\n");
			lp->stats.rx_dropped++;
			dev_kfree_skb(skb);
			return;
#endif
			break;
		case PPP_CCP:
		case PPP_LINK_CCP:
			isdn_ppp_receive_ccp(net_dev,lp,skb,proto);
			/* Dont pop up ResetReq/Ack stuff to the daemon any
			   longer - the job is done already */
			if(skb->data[0] == CCP_RESETREQ ||
			   skb->data[0] == CCP_RESETACK)
				break;
			/* fall through */
		default:
			isdn_ppp_fill_rq(skb->data, skb->len, proto, lp->ppp_slot);	/* push data to pppd device */
			dev_kfree_skb(skb);
			return;
	}

 	/* Reset hangup-timer */
 	lp->huptimer = 0;
	netif_rx(skb);
	/* net_dev->local->stats.rx_packets++; *//* done in isdn_net.c */

	return;
}

/*
 * isdn_ppp_skb_push ..
 * checks whether we have enough space at the beginning of the SKB
 * and allocs a new SKB if necessary
 */
static unsigned char *isdn_ppp_skb_push(struct sk_buff **skb_p,int len)
{
	struct sk_buff *skb = *skb_p;

	if(skb_headroom(skb) < len) {
		printk(KERN_ERR "isdn_ppp_skb_push:under %d %d\n",skb_headroom(skb),len);
		dev_kfree_skb(skb);
		return NULL;
	}
	return skb_push(skb,len);
}


/*
 * send ppp frame .. we expect a PIDCOMPressable proto --
 *  (here: currently always PPP_IP,PPP_VJC_COMP,PPP_VJC_UNCOMP)
 *
 * VJ compression may change skb pointer!!! .. requeue with old
 * skb isn't allowed!!
 */

int
isdn_ppp_xmit(struct sk_buff *skb, struct device *netdev)
{
	struct device *mdev = ((isdn_net_local *) (netdev->priv))->master;	/* get master (for redundancy) */
	isdn_net_local *lp,*mlp;
	isdn_net_dev *nd;
	unsigned int proto = PPP_IP;     /* 0x21 */
	struct ippp_struct *ipt,*ipts;

	if (mdev)
		mlp = (isdn_net_local *) (mdev->priv);
	else {
		mdev = netdev;
		mlp = (isdn_net_local *) (netdev->priv);
	}
	nd = mlp->netdev;       /* get master lp */
	ipts = ippp_table[mlp->ppp_slot];

	if (!(ipts->pppcfg & SC_ENABLE_IP)) {	/* PPP connected ? */
#ifdef ISDN_SYNCPPP_READDRESS
		if (!ipts->old_pa_addr)
			ipts->old_pa_addr = mdev->pa_addr;
		if (!ipts->old_pa_dstaddr)
			ipts->old_pa_dstaddr = mdev->pa_dstaddr;
#endif
		if (ipts->debug & 0x1)
			printk(KERN_INFO "%s: IP frame delayed.\n", netdev->name);
		return 1;
	}

	switch (ntohs(skb->protocol)) {
		case ETH_P_IP:
			proto = PPP_IP;
#ifdef ISDN_SYNCPPP_READDRESS
			if (ipts->old_pa_addr != mdev->pa_addr) {
				struct iphdr *ipfr;
				ipfr = (struct iphdr *) skb->data;
				if(ipts->debug & 0x4)
					printk(KERN_DEBUG "IF-address changed from %lx to %lx\n", ipts->old_pa_addr, mdev->pa_addr);
				if (ipfr->version == 4) {
					if (ipfr->saddr == ipts->old_pa_addr) {
						printk(KERN_DEBUG "readdressing %lx to %lx\n", ipfr->saddr, mdev->pa_addr);
						ipfr->saddr = mdev->pa_addr;
					}
				}
			}
			/* dstaddr change not so important */
#endif
			break;
		case ETH_P_IPX:
			proto = PPP_IPX;	/* untested */
			break;
		default:
			dev_kfree_skb(skb);
			printk(KERN_ERR "isdn_ppp: skipped frame with unsupported protocoll: %#x.\n", skb->protocol);
			return 0;
	}

	lp = nd->queue;         /* get lp on top of queue */

	if (lp->sav_skb) {      /* find a non-busy device */
		isdn_net_local *nlp = lp->next;
		while (lp->sav_skb) {
			if (lp == nlp)
				return 1;
			nlp = nd->queue = nd->queue->next;
		}
		lp = nlp;
	}
	ipt = ippp_table[lp->ppp_slot];
	lp->huptimer = 0;

	/*
	 * after this line .. requeueing in the device queue is no longer allowed!!!
	 */

	/* Pull off the fake header we stuck on earlier to keep
     * the fragemntation code happy.
     * this will break the ISDN_SYNCPPP_READDRESS hack a few lines
     * above. So, enabling this is no longer allowed
     */
	skb_pull(skb,IPPP_MAX_HEADER);

	if (ipt->debug & 0x4)
		printk(KERN_DEBUG "xmit skb, len %d\n", (int) skb->len);
        if (ipts->debug & 0x40)
                isdn_ppp_frame_log("xmit0", skb->data, skb->len, 32,ipts->unit,lp->ppp_slot);

#ifdef CONFIG_ISDN_PPP_VJ
	if (proto == PPP_IP && ipts->pppcfg & SC_COMP_TCP) {	/* ipts here? probably yes, but check this again */
		struct sk_buff *new_skb;
	        unsigned short hl;
		/*
		 * we need to reserve enought space in front of
		 * sk_buff. old call to dev_alloc_skb only reserved
		 * 16 bytes, now we are looking what the driver want.
		 */
		hl = dev->drv[lp->isdn_device]->interface->hl_hdrlen;
		new_skb = alloc_skb(hl+skb->len, GFP_ATOMIC);
		if (new_skb) {
			u_char *buf;
			int pktlen;

			skb_reserve(new_skb, hl);
			new_skb->dev = skb->dev;
			skb_put(new_skb, skb->len);
			buf = skb->data;

			pktlen = slhc_compress(ipts->slcomp, skb->data, skb->len, new_skb->data,
				 &buf, !(ipts->pppcfg & SC_NO_TCP_CCID));

			if (buf != skb->data) {	
				if (new_skb->data != buf)
					printk(KERN_ERR "isdn_ppp: FATAL error after slhc_compress!!\n");
				dev_kfree_skb(skb);
				skb = new_skb;
			} else {
				dev_kfree_skb(new_skb);
			}

			skb_trim(skb, pktlen);
			if (skb->data[0] & SL_TYPE_COMPRESSED_TCP) {	/* cslip? style -> PPP */
				proto = PPP_VJC_COMP;
				skb->data[0] ^= SL_TYPE_COMPRESSED_TCP;
			} else {
				if (skb->data[0] >= SL_TYPE_UNCOMPRESSED_TCP)
					proto = PPP_VJC_UNCOMP;
				skb->data[0] = (skb->data[0] & 0x0f) | 0x40;
			}
		}
	}
#endif

	/*
	 * normal (single link) or bundle compression
	 */
	if(ipts->compflags & SC_COMP_ON)
		skb = isdn_ppp_compress(skb,&proto,ipt,ipts,0);

	if (ipt->debug & 0x24)
		printk(KERN_DEBUG "xmit2 skb, len %d, proto %04x\n", (int) skb->len, proto);

#ifdef CONFIG_ISDN_MPP
	if (ipt->mpppcfg & SC_MP_PROT) {
		/* we get mp_seqno from static isdn_net_local */
		long mp_seqno = ipts->mp_seqno;
		ipts->mp_seqno++;
		nd->queue = nd->queue->next;
		if (ipt->mpppcfg & SC_OUT_SHORT_SEQ) {
			unsigned char *data = isdn_ppp_skb_push(&skb, 3);
			if(!data)
				return 0;
			mp_seqno &= 0xfff;
			data[0] = MP_BEGIN_FRAG | MP_END_FRAG | ((mp_seqno >> 8) & 0xf);	/* (B)egin & (E)ndbit .. */
			data[1] = mp_seqno & 0xff;
			data[2] = proto;	/* PID compression */
		} else {
			unsigned char *data = isdn_ppp_skb_push(&skb, 5);
			if(!data)
				return 0;
			data[0] = MP_BEGIN_FRAG | MP_END_FRAG;	/* (B)egin & (E)ndbit .. */
			data[1] = (mp_seqno >> 16) & 0xff;	/* sequence number: 24bit */
			data[2] = (mp_seqno >> 8) & 0xff;
			data[3] = (mp_seqno >> 0) & 0xff;
			data[4] = proto;	/* PID compression */
		}
		proto = PPP_MP; /* MP Protocol, 0x003d */
	}
#endif

	/*
	 * 'link in bundle' compression  ...
	 */
	if(ipt->compflags & SC_LINK_COMP_ON)
		skb = isdn_ppp_compress(skb,&proto,ipt,ipts,1);

	if( (ipt->pppcfg & SC_COMP_PROT) && (proto <= 0xff) ) {
		unsigned char *data = isdn_ppp_skb_push(&skb,1);
		if(!data)
			return 0;
		data[0] = proto & 0xff;
	}
	else {
		unsigned char *data = isdn_ppp_skb_push(&skb,2);
		if(!data)
			return 0;
		data[0] = (proto >> 8) & 0xff;
		data[1] = proto & 0xff;
	}
	if(!(ipt->pppcfg & SC_COMP_AC)) {
		unsigned char *data = isdn_ppp_skb_push(&skb,2);
		if(!data)
			return 0;
		data[0] = 0xff;    /* All Stations */
		data[1] = 0x03;    /* Unnumbered information */
	}

	/* tx-stats are now updated via BSENT-callback */

	if (ipts->debug & 0x40) {
		printk(KERN_DEBUG "skb xmit: len: %d\n", (int) skb->len);
		isdn_ppp_frame_log("xmit", skb->data, skb->len, 32,ipt->unit,lp->ppp_slot);
	}
	if (isdn_net_send_skb(netdev, lp, skb)) {
		if (lp->sav_skb) {	/* whole sav_skb processing with disabled IRQs ?? */
			printk(KERN_ERR "%s: whoops .. there is another stored skb!\n", netdev->name);
			dev_kfree_skb(skb);
		} else
			lp->sav_skb = skb;
	}
	return 0;
}

#ifdef CONFIG_ISDN_MPP

/*
 * free SQ queue
 * -------------
 * Note: We need two queues for MPPP. The SQ queue holds fully (re)assembled frames,
 * that can't be delivered, because there is an outstanding earlier frame
 */
static void
isdn_ppp_free_sqqueue(isdn_net_dev * p)
{
	struct sqqueue *q = p->ib.sq;

	p->ib.sq = NULL;
	while (q) {
		struct sqqueue *qn = q->next;
		if (q->skb)
			dev_kfree_skb(q->skb);
		kfree(q);
		q = qn;
	}

}

/*
 * free MP queue
 * -------------
 * Note: The MP queue holds all frame fragments of frames, that can't be
 * reassembled, because there is at least one missing fragment.
 */
static void 
isdn_ppp_free_mpqueue(isdn_net_dev * p)
{
	struct mpqueue *q = p->mp_last;
	p->mp_last = NULL;

	while (q) {
		struct mpqueue *ql = q->next;
		dev_kfree_skb(q->skb);
		kfree(q);
		q = ql;
	}
}

static int
isdn_ppp_bundle(struct ippp_struct *is, int unit)
{
	char ifn[IFNAMSIZ + 1];
	long flags;
	isdn_net_dev *p;
	isdn_net_local *lp,
	*nlp;

	sprintf(ifn, "ippp%d", unit);
	p = isdn_net_findif(ifn);
	if (!p)
		return -1;

	isdn_timer_ctrl(ISDN_TIMER_IPPP, 1);	/* enable timer for ippp/MP */

	save_flags(flags);
	cli();

	nlp = is->lp;

	lp = p->queue;
	p->ib.bundled = 1;
	nlp->last = lp->last;
	lp->last->next = nlp;
	lp->last = nlp;
	nlp->next = lp;
	p->queue = nlp;

	ippp_table[nlp->ppp_slot]->unit = ippp_table[lp->ppp_slot]->unit;
/* maybe also SC_CCP stuff */
	ippp_table[nlp->ppp_slot]->pppcfg |= ippp_table[lp->ppp_slot]->pppcfg &
	    (SC_ENABLE_IP | SC_NO_TCP_CCID | SC_REJ_COMP_TCP);

	ippp_table[nlp->ppp_slot]->mpppcfg |= ippp_table[lp->ppp_slot]->mpppcfg &
	    (SC_MP_PROT | SC_REJ_MP_PROT | SC_OUT_SHORT_SEQ | SC_IN_SHORT_SEQ);
#if 0
	if (ippp_table[nlp->ppp_slot]->mpppcfg != ippp_table[lp->ppp_slot]->mpppcfg) {
		printk(KERN_WARNING "isdn_ppp_bundle: different MP options %04x and %04x\n",
		       ippp_table[nlp->ppp_slot]->mpppcfg, ippp_table[lp->ppp_slot]->mpppcfg);
	}
#endif

	restore_flags(flags);
	return 0;
}

/*
 * Mask sequence numbers in MP queue
 */
static void
isdn_ppp_mask_queue(isdn_net_dev * dev, long mask)
{
	struct mpqueue *q = dev->mp_last;
	while (q) {
		q->sqno &= mask;
		q = q->next;
	}
}

/*
 * put a fragment at the right place into the MP queue 
 * Also checks, whether this fragment completes a frame. In this case
 * the fragments are copied together into one SKB
 */
static int
isdn_ppp_fill_mpqueue(isdn_net_dev * dev, struct sk_buff **skb, int BEbyte, long *sqnop, int min_sqno)
{
	struct mpqueue *qe,
	*q1,
	*q;
	long cnt,
	 flags;
	int pktlen,
	 sqno_end;
	int sqno = *sqnop;

	q1 = (struct mpqueue *) kmalloc(sizeof(struct mpqueue), GFP_ATOMIC);
	if (!q1) {
		printk(KERN_WARNING "isdn_ppp_fill_mpqueue: Can't alloc struct memory.\n");
		save_flags(flags);
		cli();
		isdn_ppp_cleanup_mpqueue(dev, min_sqno);
		restore_flags(flags);
		return -1;
	}
	q1->skb = *skb;
	q1->sqno = sqno;
	q1->BEbyte = BEbyte;
	q1->time = jiffies;

	save_flags(flags);
	cli();

	if (!(q = dev->mp_last)) {
		dev->mp_last = q1;
		q1->next = NULL;
		q1->last = NULL;
		isdn_ppp_cleanup_mpqueue(dev, min_sqno);	/* not necessary */
		restore_flags(flags);
		return -1;	/* -1 is not an error. Just says, that this fragment hasn't complete a full frame */
	}
	for (;;) {              /* the faster way would be to step from the queue-end to the start */
		if (sqno > q->sqno) {
			if (q->next) {
				q = q->next;
				continue;
			}
			q->next = q1;
			q1->next = NULL;
			q1->last = q;
			break;
		}
		if (sqno == q->sqno)
			printk(KERN_WARNING "isdn_fill_mpqueue: illegal sqno received!!\n");
		q1->last = q->last;
		q1->next = q;
		if (q->last) {
			q->last->next = q1;
		} else
			dev->mp_last = q1;
		q->last = q1;
		break;
	}

/* now we check whether we completed a packet with this fragment */
	pktlen = -q1->skb->len;
	q = q1;
	cnt = q1->sqno;
	while (!(q->BEbyte & MP_END_FRAG)) {
		cnt++;
		if (!(q->next) || q->next->sqno != cnt) {
			isdn_ppp_cleanup_mpqueue(dev, min_sqno);
			restore_flags(flags);
			return -1;
		}
		pktlen += q->skb->len;
		q = q->next;
	}
	pktlen += q->skb->len;
	qe = q;

	q = q1;
	cnt = q1->sqno;
	while (!(q->BEbyte & MP_BEGIN_FRAG)) {
		cnt--;
		if (!(q->last) || q->last->sqno != cnt) {
			isdn_ppp_cleanup_mpqueue(dev, min_sqno);
			restore_flags(flags);
			return -1;
		}
		pktlen += q->skb->len;
		q = q->last;
	}
	pktlen += q->skb->len;

	if (q->last)
		q->last->next = qe->next;
	else
		dev->mp_last = qe->next;

	if (qe->next)
		qe->next->last = q->last;
	qe->next = NULL;
	sqno_end = qe->sqno;
	*sqnop = q->sqno;

	isdn_ppp_cleanup_mpqueue(dev, min_sqno);
	restore_flags(flags);

	*skb = dev_alloc_skb(pktlen + 40);	/* not needed: +40 for VJ compression .. */

	if (!(*skb)) {
		while (q) {
			struct mpqueue *ql = q->next;
			dev_kfree_skb(q->skb);
			kfree(q);
			q = ql;
		}
		return -2;
	}
	cnt = 0;
	skb_put(*skb, pktlen);
	while (q) {
		struct mpqueue *ql = q->next;
		memcpy((*skb)->data + cnt, q->skb->data, q->skb->len);
		cnt += q->skb->len;
		dev_kfree_skb(q->skb);
		kfree(q);
		q = ql;
	}

	return sqno_end;
}

/*
 * check sq-queue, whether we have still buffered the next packet(s)
 * or packets with a sqno less or equal to min_sqno
 * net_dev: master netdevice , lp: 'real' local connection
 */
static void
isdn_ppp_cleanup_sqqueue(isdn_net_dev * net_dev, isdn_net_local * lp, long min_sqno)
{
	struct sqqueue *q;

	while ((q = net_dev->ib.sq) && (q->sqno_start == net_dev->ib.next_num || q->sqno_end <= min_sqno)) {
		int proto;
		if (q->sqno_start != net_dev->ib.next_num) {
			printk(KERN_DEBUG "ippp: MP, stepping over missing frame: %ld\n", net_dev->ib.next_num);
#ifdef CONFIG_ISDN_PPP_VJ
			slhc_toss(ippp_table[net_dev->local->ppp_slot]->slcomp);
#endif
		}
		proto = isdn_ppp_strip_proto(q->skb);
		isdn_ppp_push_higher(net_dev, lp, q->skb, proto);
		net_dev->ib.sq = q->next;
		net_dev->ib.next_num = q->sqno_end + 1;
		kfree(q);
	}
}

/*
 * remove stale packets from list
 */
static void
isdn_ppp_cleanup_mpqueue(isdn_net_dev * dev, long min_sqno)
{
#ifdef CONFIG_ISDN_PPP_VJ
	int toss = 0;
#endif
/* z.z einfaches aussortieren gammeliger pakete. Fuer die Zukunft:
   eventuell, solange vorne kein B-paket ist und sqno<=min_sqno: auch rauswerfen
   wenn sqno<min_sqno und Luecken vorhanden sind: auch weg (die koennen nicht mehr gefuellt werden)
   bei paketen groesser min_sqno: ueber mp_mrru: wenn summe ueber pktlen der rumhaengenden Pakete
   groesser als mrru ist: raus damit , Pakete muessen allerdings zusammenhaengen sonst koennte
   ja ein Paket mit B und eins mit E dazwischenpassen */

	struct mpqueue *ql,
	*q = dev->mp_last;
	while(q && (q->sqno < min_sqno) ) {
		if ( (q->BEbyte & MP_END_FRAG) || 
			 (q->next && (q->next->sqno <= min_sqno) && (q->next->BEbyte & MP_BEGIN_FRAG)) ) {
			printk(KERN_DEBUG "ippp: freeing stale packet(s), min_sq: %ld!\n",min_sqno);
			if ((dev->mp_last = q->next))
				q->next->last = NULL;
			while (q) {
				ql = q->last;
				printk(KERN_DEBUG "ippp, freeing packet with sqno: %ld\n",q->sqno);
				dev_kfree_skb(q->skb);
				kfree(q);
#ifdef CONFIG_ISDN_PPP_VJ
				toss = 1;
#endif
				q = ql;
			}
			q = dev->mp_last;
		} else
			q = q->next;
	}
#ifdef CONFIG_ISDN_PPP_VJ
	/* did we free a stale frame ? */
	if (toss)
		slhc_toss(ippp_table[dev->local->ppp_slot]->slcomp);
#endif
}
#endif

/*
 * a buffered packet timed-out?
 */
void
isdn_ppp_timer_timeout(void)
{
#ifdef CONFIG_ISDN_MPP
	isdn_net_dev *net_dev = dev->netdev;
	struct sqqueue *q,
	*ql = NULL,
	*qn;

	while (net_dev) {
		isdn_net_local *lp = net_dev->local;
		if (net_dev->ib.modify || lp->master) {	/* interface locked or slave? */
			net_dev = net_dev->next;
			continue;
		}
		q = net_dev->ib.sq;
		while (q) {
			if (q->sqno_start == net_dev->ib.next_num || q->timer < jiffies) {

#ifdef CONFIG_ISDN_PPP_VJ
				/* did we step over a missing frame ? */
				if (q->sqno_start != net_dev->ib.next_num)
					slhc_toss(ippp_table[lp->ppp_slot]->slcomp);
#endif

				ql = net_dev->ib.sq;
				net_dev->ib.sq = q->next;
				net_dev->ib.next_num = q->sqno_end + 1;
				q->next = NULL;
				for (; ql;) {
					int proto = isdn_ppp_strip_proto(ql->skb);
					isdn_ppp_push_higher(net_dev, lp, ql->skb, proto);
					qn = ql->next;
					kfree(ql);
					ql = qn;
				}
				q = net_dev->ib.sq;
			} else
				q = q->next;
		}
		net_dev = net_dev->next;
	}
#endif
}

/*
 * network device ioctl handlers
 */

static int
isdn_ppp_dev_ioctl_stats(int slot, struct ifreq *ifr, struct device *dev)
{
	struct ppp_stats *res,
	 t;
	isdn_net_local *lp = (isdn_net_local *) dev->priv;
	int err;

	res = (struct ppp_stats *) ifr->ifr_ifru.ifru_data;
	err = verify_area(VERIFY_WRITE, res, sizeof(struct ppp_stats));

	if (err)
		return err;

	/* build a temporary stat struct and copy it to user space */

	memset(&t, 0, sizeof(struct ppp_stats));
	if (dev->flags & IFF_UP) {
		t.p.ppp_ipackets = lp->stats.rx_packets;
		t.p.ppp_ierrors = lp->stats.rx_errors;
		t.p.ppp_opackets = lp->stats.tx_packets;
		t.p.ppp_oerrors = lp->stats.tx_errors;
#ifdef CONFIG_ISDN_PPP_VJ
		if (slot >= 0 && ippp_table[slot]->slcomp) {
			struct slcompress *slcomp = ippp_table[slot]->slcomp;
			t.vj.vjs_packets = slcomp->sls_o_compressed + slcomp->sls_o_uncompressed;
			t.vj.vjs_compressed = slcomp->sls_o_compressed;
			t.vj.vjs_searches = slcomp->sls_o_searches;
			t.vj.vjs_misses = slcomp->sls_o_misses;
			t.vj.vjs_errorin = slcomp->sls_i_error;
			t.vj.vjs_tossed = slcomp->sls_i_tossed;
			t.vj.vjs_uncompressedin = slcomp->sls_i_uncompressed;
			t.vj.vjs_compressedin = slcomp->sls_i_compressed;
		}
#endif
	}
	if( copy_to_user(res, &t, sizeof(struct ppp_stats))) return -EFAULT;
	return 0;
}

int
isdn_ppp_dev_ioctl(struct device *dev, struct ifreq *ifr, int cmd)
{
	int error=0;
	char *r;
	int len;
	isdn_net_local *lp = (isdn_net_local *) dev->priv;

#if 0
	printk(KERN_DEBUG "ippp, dev_ioctl: cmd %#08x , %d \n", cmd, lp->ppp_slot);
#endif

	if (lp->p_encap != ISDN_NET_ENCAP_SYNCPPP)
		return -EINVAL;

	switch (cmd) {
		case SIOCGPPPVER:
			r = (char *) ifr->ifr_ifru.ifru_data;
			len = strlen(PPP_VERSION) + 1;
			if(copy_to_user(r, PPP_VERSION, len)) error = -EFAULT;
			break;
		case SIOCGPPPSTATS:
			error = isdn_ppp_dev_ioctl_stats(lp->ppp_slot, ifr, dev);
			break;
		default:
			error = -EINVAL;
			break;
	}
	return error;
}

static int
isdn_ppp_if_get_unit(char *name)
{
	int len,
	 i,
	 unit = 0,
	 deci;

	len = strlen(name);

	if (strncmp("ippp", name, 4) || len > 8)
		return -1;

	for (i = 0, deci = 1; i < len; i++, deci *= 10) {
		char a = name[len - i - 1];
		if (a >= '0' && a <= '9')
			unit += (a - '0') * deci;
		else
			break;
	}
	if (!i || len - i != 4)
		unit = -1;

	return unit;
}


int
isdn_ppp_dial_slave(char *name)
{
#ifdef CONFIG_ISDN_MPP
	isdn_net_dev *ndev;
	isdn_net_local *lp;
	struct device *sdev;

	if (!(ndev = isdn_net_findif(name)))
		return 1;
	lp = ndev->local;
	if (!(lp->flags & ISDN_NET_CONNECTED))
		return 5;

	sdev = lp->slave;
	while (sdev) {
		isdn_net_local *mlp = (isdn_net_local *) sdev->priv;
		if (!(mlp->flags & ISDN_NET_CONNECTED))
			break;
		sdev = mlp->slave;
	}
	if (!sdev)
		return 2;

	isdn_net_dial_req((isdn_net_local *) sdev->priv);
	return 0;
#else
	return -1;
#endif
}

int
isdn_ppp_hangup_slave(char *name)
{
#ifdef CONFIG_ISDN_MPP
	isdn_net_dev *ndev;
	isdn_net_local *lp;
	struct device *sdev;

	if (!(ndev = isdn_net_findif(name)))
		return 1;
	lp = ndev->local;
	if (!(lp->flags & ISDN_NET_CONNECTED))
		return 5;

	sdev = lp->slave;
	while (sdev) {
		isdn_net_local *mlp = (isdn_net_local *) sdev->priv;
		if ((mlp->flags & ISDN_NET_CONNECTED))
			break;
		sdev = mlp->slave;
	}
	if (!sdev)
		return 2;

	isdn_net_hangup(sdev);
	return 0;
#else
	return -1;
#endif
}

/*
 * PPP compression stuff
 */


/* Push an empty CCP Data Frame up to the daemon to wake it up and let it
   generate a CCP Reset-Request or tear down CCP altogether */

static void isdn_ppp_ccp_kickup(struct ippp_struct *is)
{
	isdn_ppp_fill_rq(NULL, 0, PPP_COMP, is->lp->ppp_slot);
}

/* In-kernel handling of CCP Reset-Request and Reset-Ack is necessary,
   but absolutely nontrivial. The most abstruse problem we are facing is
   that the generation, reception and all the handling of timeouts and
   resends including proper request id management should be entirely left
   to the (de)compressor, but indeed is not covered by the current API to
   the (de)compressor. The API is a prototype version from PPP where only
   some (de)compressors have yet been implemented and all of them are
   rather simple in their reset handling. Especially, their is only one
   outstanding ResetAck at a time with all of them and ResetReq/-Acks do
   not have parameters. For this very special case it was sufficient to
   just return an error code from the decompressor and have a single
   reset() entry to communicate all the necessary information between
   the framework and the (de)compressor. Bad enough, LZS is different
   (and any other compressor may be different, too). It has multiple
   histories (eventually) and needs to Reset each of them independently
   and thus uses multiple outstanding Acks and history numbers as an
   additional parameter to Reqs/Acks.
   All that makes it harder to port the reset state engine into the
   kernel because it is not just the same simple one as in (i)pppd but
   it must be able to pass additional parameters and have multiple out-
   standing Acks. We are trying to achieve the impossible by handling
   reset transactions independent by their id. The id MUST change when
   the data portion changes, thus any (de)compressor who uses more than
   one resettable state must provide and recognize individual ids for
   each individual reset transaction. The framework itself does _only_
   differentiate them by id, because it has no other semantics like the
   (de)compressor might.
   This looks like a major redesign of the interface would be nice,
   but I don't have an idea how to do it better. */

/* Send a CCP Reset-Request or Reset-Ack directly from the kernel. This is
   getting that lengthy because there is no simple "send-this-frame-out"
   function above but every wrapper does a bit different. Hope I guess
   correct in this hack... */

static void isdn_ppp_ccp_xmit_reset(struct ippp_struct *is, int proto,
				    unsigned char code, unsigned char id,
				    unsigned char *data, int len)
{
	struct sk_buff *skb;
	unsigned char *p;
	int count;
	int cnt = 0;
	isdn_net_local *lp = is->lp;

	/* Alloc large enough skb */
	skb = dev_alloc_skb(len + 16);
	if(!skb) {
		printk(KERN_WARNING
		       "ippp: CCP cannot send reset - out of memory\n");
		return;
	}

	/* We may need to stuff an address and control field first */
	if(!(is->pppcfg & SC_COMP_AC)) {
		p = skb_put(skb, 2);
		*p++ = 0xff;
		*p++ = 0x03;
	}

	/* Stuff proto, code, id and length */
	p = skb_put(skb, 6);
	*p++ = (proto >> 8);
	*p++ = (proto & 0xff);
	*p++ = code;
	*p++ = id;
	cnt = 4 + len;
	*p++ = (cnt >> 8);
	*p++ = (cnt & 0xff);

	/* Now stuff remaining bytes */
	if(len) {
		p = skb_put(skb, len);
		memcpy(p, data, len);
	}

	/* skb is now ready for xmit */
	printk(KERN_DEBUG "Sending CCP Frame:\n");
	isdn_ppp_frame_log("ccp-xmit", skb->data, skb->len, 32, is->unit,lp->ppp_slot);

	/* Just ripped from isdn_ppp_write. Dunno whether it makes sense,
	   especially dunno what the sav_skb stuff is good for. */

	count = skb->len;
	if ((cnt = isdn_writebuf_skb_stub(lp->isdn_device, lp->isdn_channel,
					  1, skb)) != count) {
		if (lp->sav_skb) {
			dev_kfree_skb(lp->sav_skb);
			printk(KERN_INFO
			       "isdn_ppp_write: freeing sav_skb (%d,%d)!\n",
			       cnt, count);
		} else
			printk(KERN_INFO
			       "isdn_ppp_write: Can't write PPP frame to LL (%d,%d)!\n",
			       cnt, count);
		lp->sav_skb = skb;
	}
}

/* Allocate the reset state vector */
static struct ippp_ccp_reset *isdn_ppp_ccp_reset_alloc(struct ippp_struct *is)
{
	struct ippp_ccp_reset *r;
	printk(KERN_DEBUG "ippp_ccp: allocating reset data structure\n");
	r = kmalloc(sizeof(struct ippp_ccp_reset), GFP_KERNEL);
	if(!r)
		return NULL;
	memset(r, 0, sizeof(struct ippp_ccp_reset));
	is->reset = r;
	return r;
}

/* Free a given state and clear everything up for later reallocation */
static void isdn_ppp_ccp_reset_free_state(struct ippp_struct *is,
					  unsigned char id)
{
	struct ippp_ccp_reset_state *rs;

	if(is->reset->rs[id]) {
		printk(KERN_DEBUG "ippp_ccp: freeing state for id %d\n", id);
		rs = is->reset->rs[id];
		/* Make sure the kernel will not call back later */
		if(rs->ta)
			del_timer(&rs->timer);
		is->reset->rs[id] = NULL;
		kfree(rs);
	} else {
		printk(KERN_WARNING "ippp_ccp: id %d is not allocated\n", id);
	}
}

/* The timer callback function which is called when a ResetReq has timed out,
   aka has never been answered by a ResetAck */
static void isdn_ppp_ccp_timer_callback(unsigned long closure)
{
	struct ippp_ccp_reset_state *rs =
		(struct ippp_ccp_reset_state *)closure;

	if(!rs) {
		printk(KERN_ERR "ippp_ccp: timer cb with zero closure.\n");
		return;
	}
	if(rs->ta && rs->state == CCPResetSentReq) {
		/* We are correct here */
		printk(KERN_DEBUG "ippp_ccp: CCP Reset timed out for id %d\n",
		       rs->id);
		if(!rs->expra) {
			/* Hmm, there is no Ack really expected. We can clean
			   up the state now, it will be reallocated if the
			   decompressor insists on another reset */
			rs->ta = 0;
			isdn_ppp_ccp_reset_free_state(rs->is, rs->id);
			return;
		}
		/* Push it again */
		isdn_ppp_ccp_xmit_reset(rs->is, PPP_CCP, CCP_RESETREQ, rs->id,
					rs->data, rs->dlen);
		/* Restart timer */
		rs->timer.expires = jiffies + HZ*5;
		add_timer(&rs->timer);
	} else {
		printk(KERN_WARNING "ippp_ccp: timer cb in wrong state %d\n",
		       rs->state);
	}
}

/* Allocate a new reset transaction state */
static struct ippp_ccp_reset_state *isdn_ppp_ccp_reset_alloc_state(struct ippp_struct *is,
						      unsigned char id)
{
	struct ippp_ccp_reset_state *rs;
	if(is->reset->rs[id]) {
		printk(KERN_WARNING "ippp_ccp: old state exists for id %d\n",
		       id);
		return NULL;
	} else {
		rs = kmalloc(sizeof(struct ippp_ccp_reset_state), GFP_KERNEL);
		if(!rs)
			return NULL;
		memset(rs, 0, sizeof(struct ippp_ccp_reset_state));
		rs->state = CCPResetIdle;
		rs->is = is;
		rs->id = id;
		rs->timer.data = (unsigned long)rs;
		rs->timer.function = isdn_ppp_ccp_timer_callback;
		is->reset->rs[id] = rs;
	}
	return rs;
}


/* A decompressor wants a reset with a set of parameters - do what is
   necessary to fulfill it */
static void isdn_ppp_ccp_reset_trans(struct ippp_struct *is,
				     struct isdn_ppp_resetparams *rp)
{
	struct ippp_ccp_reset_state *rs;

	if(rp->valid) {
		/* The decompressor defines parameters by itself */
		if(rp->rsend) {
			/* And he wants us to send a request */
			if(!(rp->idval)) {
				printk(KERN_ERR "ippp_ccp: decompressor must"
				       " specify reset id\n");
				return;
			}
			if(is->reset->rs[rp->id]) {
				/* There is already a transaction in existence
				   for this id. May be still waiting for a
				   Ack or may be wrong. */
				rs = is->reset->rs[rp->id];
				if(rs->state == CCPResetSentReq && rs->ta) {
					printk(KERN_DEBUG "ippp_ccp: reset"
					       " trans still in progress"
					       " for id %d\n", rp->id);
				} else {
					printk(KERN_WARNING "ippp_ccp: reset"
					       " trans in wrong state %d for"
					       " id %d\n", rs->state, rp->id);
				}
			} else {
				/* Ok, this is a new transaction */
				printk(KERN_DEBUG "ippp_ccp: new trans for id"
				       " %d to be started\n", rp->id);
				rs = isdn_ppp_ccp_reset_alloc_state(is, rp->id);
				if(!rs) {
					printk(KERN_ERR "ippp_ccp: out of mem"
					       " allocing ccp trans\n");
					return;
				}
				rs->state = CCPResetSentReq;
				rs->expra = rp->expra;
				if(rp->dtval) {
					rs->dlen = rp->dlen;
					memcpy(rs->data, rp->data, rp->dlen);
				}
				/* HACK TODO - add link comp here */
				isdn_ppp_ccp_xmit_reset(is, PPP_CCP,
							CCP_RESETREQ, rs->id,
							rs->data, rs->dlen);
				/* Start the timer */
				rs->timer.expires = jiffies + 5*HZ;
				add_timer(&rs->timer);
				rs->ta = 1;
			}
		} else {
			printk(KERN_DEBUG "ippp_ccp: no reset sent\n");
		}
	} else {
		/* The reset params are invalid. The decompressor does not
		   care about them, so we just send the minimal requests
		   and increase ids only when an Ack is received for a
		   given id */
		if(is->reset->rs[is->reset->lastid]) {
			/* There is already a transaction in existence
			   for this id. May be still waiting for a
			   Ack or may be wrong. */
			rs = is->reset->rs[is->reset->lastid];
			if(rs->state == CCPResetSentReq && rs->ta) {
				printk(KERN_DEBUG "ippp_ccp: reset"
				       " trans still in progress"
				       " for id %d\n", rp->id);
			} else {
				printk(KERN_WARNING "ippp_ccp: reset"
				       " trans in wrong state %d for"
				       " id %d\n", rs->state, rp->id);
			}
		} else {
			printk(KERN_DEBUG "ippp_ccp: new trans for id"
			       " %d to be started\n", is->reset->lastid);
			rs = isdn_ppp_ccp_reset_alloc_state(is,
							    is->reset->lastid);
			if(!rs) {
				printk(KERN_ERR "ippp_ccp: out of mem"
				       " allocing ccp trans\n");
				return;
			}
			rs->state = CCPResetSentReq;
			/* We always expect an Ack if the decompressor doesnt
			   know	better */
			rs->expra = 1;
			rs->dlen = 0;
			/* HACK TODO - add link comp here */
			isdn_ppp_ccp_xmit_reset(is, PPP_CCP, CCP_RESETREQ,
						rs->id, NULL, 0);
			/* Start the timer */
			rs->timer.expires = jiffies + 5*HZ;
			add_timer(&rs->timer);
			rs->ta = 1;
		}
	}
}

/* An Ack was received for this id. This means we stop the timer and clean
   up the state prior to calling the decompressors reset routine. */
static void isdn_ppp_ccp_reset_ack_rcvd(struct ippp_struct *is,
					unsigned char id)
{
	struct ippp_ccp_reset_state *rs = is->reset->rs[id];

	if(rs) {
		if(rs->ta && rs->state == CCPResetSentReq) {
			/* Great, we are correct */
			if(!rs->expra)
				printk(KERN_DEBUG "ippp_ccp: ResetAck received"
				       " for id %d but not expected\n", id);
		} else {
			printk(KERN_INFO "ippp_ccp: ResetAck received out of"
			       "sync for id %d\n", id);
		}
		if(rs->ta) {
			rs->ta = 0;
			del_timer(&rs->timer);
		}
		isdn_ppp_ccp_reset_free_state(is, id);
	} else {
		printk(KERN_INFO "ippp_ccp: ResetAck received for unknown id"
		       " %d\n", id);
	}
	/* Make sure the simple reset stuff uses a new id next time */
	is->reset->lastid++;
}

static struct sk_buff *isdn_ppp_decompress(struct sk_buff *skb,struct ippp_struct *is,struct ippp_struct *master,
	int proto)
{
#ifndef CONFIG_ISDN_CCP
	if(proto == PPP_COMP || proto == PPP_LINK_COMP) {
		printk(KERN_ERR "isdn_ppp: Ouch! Compression not included!\n");
		dev_kfree_skb(skb);
		return NULL;
	}
	return skb;
#else
	void *stat = NULL;
	struct isdn_ppp_compressor *ipc = NULL;
	struct sk_buff *skb_out;
	int len;
	struct ippp_struct *ri;
	struct isdn_ppp_resetparams rsparm;
	unsigned char rsdata[IPPP_RESET_MAXDATABYTES];

	if(!master) {
		/* 
		 * single link decompression 
		 */
		if(!is->link_decompressor) {
			printk(KERN_ERR "ippp: no link decompressor defined!\n");
			dev_kfree_skb(skb);
			return NULL;
		}
		if(!is->link_decomp_stat) {
			printk(KERN_DEBUG "ippp: no link decompressor data allocated\n");
			dev_kfree_skb(skb);
			return NULL;
		}
		stat = is->link_decomp_stat;
		ipc = is->link_decompressor;
		ri = is;
	}
	else {
		/*
		 * 'normal' or bundle-compression 
		 */
		if(!master->decompressor) {
			printk(KERN_ERR "ippp: no decompressor defined!\n");
			dev_kfree_skb(skb);
			return NULL;
		}
		if(!master->decomp_stat) {
			printk(KERN_DEBUG "ippp: no decompressor data allocated\n");
			dev_kfree_skb(skb);
			return NULL;
		}
		stat = master->decomp_stat;
		ipc = master->decompressor;
		ri = master;
	}

	/*
	printk(KERN_DEBUG "ippp: Decompress valid!\n");
	*/

	if((master && proto == PPP_COMP) || (!master && proto == PPP_LINK_COMP) ) {
		/* Set up reset params for the decompressor */
		memset(&rsparm, 0, sizeof(rsparm));
		rsparm.data = rsdata;
		rsparm.maxdlen = IPPP_RESET_MAXDATABYTES;

/* !!!HACK,HACK,HACK!!! 2048 is only assumed */
		skb_out = dev_alloc_skb(2048);
		len = ipc->decompress(stat,skb,skb_out, &rsparm);
		dev_kfree_skb(skb);
		if(len <= 0) {
		  /* Ok, some error */
		  switch(len) {
		  case DECOMP_ERROR:
		    ri->pppcfg |= SC_DC_ERROR;
		    printk(KERN_INFO "ippp: decomp wants reset %s params\n",
			   rsparm.valid ? "with" : "without");

		    isdn_ppp_ccp_reset_trans(ri, &rsparm);

		    break;
		  case DECOMP_FATALERROR:
		    ri->pppcfg |= SC_DC_FERROR;
		    /* Kick ipppd to recognize the error */
		    isdn_ppp_ccp_kickup(ri);
		    break;
		  }
		  /* Did I see a leak here ? */
		  dev_kfree_skb(skb_out);
		  return NULL;
		}
		return skb_out;
	}
	else {
		/*
		printk(KERN_DEBUG "isdn_ppp: [%d] Calling incomp with this frame!\n",is->unit);
		*/
		ipc->incomp(stat,skb,proto);
		return skb;
	}
#endif
}

/*
 * compress a frame 
 *   type=0: normal/bundle compression
 *       =1: link compression
 * returns original skb if we haven't compressed the frame
 * and a new skb pointer if we've done it
 */
static struct sk_buff *isdn_ppp_compress(struct sk_buff *skb_in,int *proto,
	struct ippp_struct *is,struct ippp_struct *master,int type)
{
    int ret;
    int new_proto;
    struct isdn_ppp_compressor *compressor;
    void *stat;
    struct sk_buff *skb_out;

#ifdef CONFIG_ISDN_CCP
	/* we do not compress control protocols */
    if(*proto < 0 || *proto > 0x3fff) {
#else
    {
#endif
      return skb_in;
    }

	if(type) { /* type=1 => Link compression */
#if 0
		compressor = is->link_compressor;
		stat = is->link_comp_stat;
		new_proto = PPP_LINK_COMP;
#else
		return skb_in;
#endif
	}
	else {
		if(!master) {
			compressor = is->compressor;
			stat = is->comp_stat;
		}
		else {
			compressor = master->compressor;
			stat = master->comp_stat;
		}
		new_proto = PPP_COMP;
	}

	if(!compressor) {
		printk(KERN_ERR "isdn_ppp: No compressor set!\n");
		return skb_in;
	}
	if(!stat) {
		printk(KERN_ERR "isdn_ppp: Compressor not initialized?\n");
		return skb_in;
	}

	/* Allow for at least 150 % expansion (for now) */
	skb_out = dev_alloc_skb(skb_in->len + skb_in->len/2 + 32);
	if(!skb_out)
		return skb_in;

	ret = (compressor->compress)(stat,skb_in,skb_out,*proto);
	if(!ret) {
		dev_kfree_skb(skb_out);
		return skb_in;
	}
	
	dev_kfree_skb(skb_in);
	*proto = new_proto;
	return skb_out;
}

/*
 * we received a CCP frame .. 
 * not a clean solution, but we MUST handle a few cases in the kernel
 */
static void isdn_ppp_receive_ccp(isdn_net_dev *net_dev, isdn_net_local *lp,
	 struct sk_buff *skb,int proto)
{
	struct ippp_struct *is = ippp_table[lp->ppp_slot];
	struct ippp_struct *mis;
	int len;
	struct isdn_ppp_resetparams rsparm;
	unsigned char rsdata[IPPP_RESET_MAXDATABYTES];	

	printk(KERN_DEBUG "Received CCP frame from peer\n");
	isdn_ppp_frame_log("ccp-rcv", skb->data, skb->len, 32, is->unit,lp->ppp_slot);

	if(lp->master)
		mis = ippp_table[((isdn_net_local *) (lp->master->priv))->ppp_slot];
	else
		mis = is;

	switch(skb->data[0]) {
	case CCP_CONFREQ:
	case CCP_TERMREQ:
	case CCP_TERMACK:
		if(is->debug & 0x10)
			printk(KERN_DEBUG "Disable (de)compression here!\n");
		if(proto == PPP_CCP)
			mis->compflags &= ~(SC_DECOMP_ON|SC_COMP_ON);		
		else
			is->compflags &= ~(SC_LINK_DECOMP_ON|SC_LINK_COMP_ON);		
		break;
	case CCP_CONFACK:
		/* if we RECEIVE an ackowledge we enable the decompressor */
		if(is->debug & 0x10)
			printk(KERN_DEBUG "Enable decompression here!\n");
		if(proto == PPP_CCP)
			mis->compflags |= SC_DECOMP_ON;
		else
			is->compflags |= SC_LINK_DECOMP_ON;
		break;

	case CCP_RESETACK:
		printk(KERN_DEBUG "Received ResetAck from peer\n");
		len = (skb->data[2] << 8) | skb->data[3];
		len -= 4;

		if(proto == PPP_CCP) {
			/* If a reset Ack was outstanding for this id, then
			   clean up the state engine */
			isdn_ppp_ccp_reset_ack_rcvd(mis, skb->data[1]);
			if(mis->decompressor && mis->decomp_stat)
				mis->decompressor->
					reset(mis->decomp_stat,
					      skb->data[0],
					      skb->data[1],
					      len ? &skb->data[4] : NULL,
					      len, NULL);
			/* TODO: This is not easy to decide here */
			mis->compflags &= ~SC_DECOMP_DISCARD;
			mis->pppcfg &= ~SC_DC_ERROR;
		}
		else {
			isdn_ppp_ccp_reset_ack_rcvd(is, skb->data[1]);
			if(is->link_decompressor && is->link_decomp_stat)
				is->link_decompressor->
					reset(is->link_decomp_stat,
					      skb->data[0],
					      skb->data[1],
					      len ? &skb->data[4] : NULL,
					      len, NULL);
			/* TODO: neither here */
			is->compflags &= ~SC_LINK_DECOMP_DISCARD;
			is->pppcfg &= ~SC_DC_ERROR;
		}
		break;

	case CCP_RESETREQ:
		printk(KERN_DEBUG "Received ResetReq from peer\n");
		/* Receiving a ResetReq means we must reset our compressor */
		/* Set up reset params for the reset entry */
		memset(&rsparm, 0, sizeof(rsparm));
		rsparm.data = rsdata;
		rsparm.maxdlen = IPPP_RESET_MAXDATABYTES; 
		/* Isolate data length */
		len = (skb->data[2] << 8) | skb->data[3];
		len -= 4;
		if(proto == PPP_CCP) {
			if(mis->compressor && mis->comp_stat)
				mis->compressor->
					reset(mis->comp_stat,
					      skb->data[0],
					      skb->data[1],
					      len ? &skb->data[4] : NULL,
					      len, &rsparm);
		}
		else {
			if(is->link_compressor && is->link_comp_stat)
				is->link_compressor->
					reset(is->link_comp_stat,
					      skb->data[0],
					      skb->data[1],
					      len ? &skb->data[4] : NULL,
					      len, &rsparm);
		}
		/* Ack the Req as specified by rsparm */
		if(rsparm.valid) {
			/* Compressor reset handler decided how to answer */
			if(rsparm.rsend) {
				/* We should send a Frame */
				isdn_ppp_ccp_xmit_reset(is, proto, CCP_RESETACK,
							rsparm.idval ? rsparm.id
							: skb->data[1],
							rsparm.dtval ?
							rsparm.data : NULL,
							rsparm.dtval ?
							rsparm.dlen : 0);
			} else {
				printk(KERN_DEBUG "ResetAck suppressed\n");
			}
		} else {
			/* We answer with a straight reflected Ack */
			isdn_ppp_ccp_xmit_reset(is, proto, CCP_RESETACK,
						skb->data[1],
						len ? &skb->data[4] : NULL,
						len);
		}
		break;
	}
}


/*
 * Daemon sends a CCP frame ...
 */

/* TODO: Clean this up with new Reset semantics */

static void isdn_ppp_send_ccp(isdn_net_dev *net_dev, isdn_net_local *lp, struct sk_buff *skb)
{
	struct ippp_struct *mis,*is = ippp_table[lp->ppp_slot];
	int proto;
	unsigned char *data;

	if(!skb || skb->len < 3)
		return;

	/* Daemon may send with or without address and control field comp */
	data = skb->data;
	if(!(is->pppcfg & SC_COMP_AC) && data[0] == 0xff && data[1] == 0x03) {
		data += 2;
		if(skb->len < 5)
			return;
	}

	proto = ((int)data[0]<<8)+data[1];
	if(proto != PPP_CCP && proto != PPP_LINK_CCP)
		return;

	printk(KERN_DEBUG "Received CCP frame from daemon:\n");
	isdn_ppp_frame_log("ccp-xmit", skb->data, skb->len, 32, is->unit,lp->ppp_slot);

        if(lp->master)
                mis = ippp_table[((isdn_net_local *) (lp->master->priv))->ppp_slot];
        else
                mis = is;
	
	if(mis != is)
		printk(KERN_DEBUG "isdn_ppp: Ouch! Master CCP sends on slave slot!\n");
	
        switch(data[2]) {
	case CCP_CONFREQ:
	case CCP_TERMREQ:
	case CCP_TERMACK:
		if(is->debug & 0x10)
			printk(KERN_DEBUG "Disable (de)compression here!\n");
		if(proto == PPP_CCP)
			is->compflags &= ~(SC_DECOMP_ON|SC_COMP_ON);
		else
			is->compflags &= ~(SC_LINK_DECOMP_ON|SC_LINK_COMP_ON);
		break;
	case CCP_CONFACK:
		/* if we SEND an ackowledge we can/must enable the compressor */
		if(is->debug & 0x10)
			printk(KERN_DEBUG "Enable compression here!\n");
		if(proto == PPP_CCP)
			is->compflags |= SC_COMP_ON;
		else
			is->compflags |= SC_LINK_COMP_ON;
		break;
	case CCP_RESETACK:
		/* If we send a ACK we should reset our compressor */
		if(is->debug & 0x10)
			printk(KERN_DEBUG "Reset decompression state here!\n");
		printk(KERN_DEBUG "ResetAck from daemon passed by\n");
		if(proto == PPP_CCP) {
			/* link to master? */
			if(is->compressor && is->comp_stat)
				is->compressor->reset(is->comp_stat, 0, 0,
						      NULL, 0, NULL);
			is->compflags &= ~SC_COMP_DISCARD;	
		}
		else {
			if(is->link_compressor && is->link_comp_stat)
				is->link_compressor->reset(is->link_comp_stat,
							   0, 0, NULL, 0, NULL);
			is->compflags &= ~SC_LINK_COMP_DISCARD;	
		}
		break;
	case CCP_RESETREQ:
		/* Just let it pass by */
		printk(KERN_DEBUG "ResetReq from daemon passed by\n");
		break;
	}
}


int isdn_ppp_register_compressor(struct isdn_ppp_compressor *ipc)
{
	ipc->next = ipc_head;
	ipc->prev = NULL;
	if(ipc_head) {
		ipc_head->prev = ipc;
	}
	ipc_head = ipc;
	return 0;
}

int isdn_ppp_unregister_compressor(struct isdn_ppp_compressor *ipc)
{
	if(ipc->prev)
		ipc->prev->next = ipc->next;
	else
		ipc_head = ipc->next;
	if(ipc->next)
		ipc->next->prev = ipc->prev;
	ipc->prev = ipc->next = NULL;
	return 0;
}

static int isdn_ppp_set_compressor(struct ippp_struct *is, struct isdn_ppp_comp_data *data)
{
	struct isdn_ppp_compressor *ipc = ipc_head;
	int ret;
	void *stat;
	int num = data->num;

	if(is->debug & 0x10)
		printk(KERN_DEBUG "[%d] Set %s type %d\n",is->unit,
			(data->flags&IPPP_COMP_FLAG_XMIT)?"compressor":"decompressor",num);

	while(ipc) {
		if(ipc->num == num) {
			stat = ipc->alloc(data);
			if(stat) {
				ret = ipc->init(stat,data,is->unit,0);
				if(!ret) {
					printk(KERN_ERR "Can't init (de)compression!\n");
					ipc->free(stat);
					stat = NULL;
					break;
				}
			}
			else {
				printk(KERN_ERR "Can't alloc (de)compression!\n");
				break;
			}

                        if(data->flags & IPPP_COMP_FLAG_XMIT) {
				if(data->flags & IPPP_COMP_FLAG_LINK) {
					if(is->link_comp_stat)
						is->link_compressor->free(is->link_comp_stat);
					is->link_comp_stat = stat;
                                	is->link_compressor = ipc;
				}
				else {
					if(is->comp_stat)
						is->compressor->free(is->comp_stat);
					is->comp_stat = stat;
                                	is->compressor = ipc;
				}
			}
                        else {
				if(data->flags & IPPP_COMP_FLAG_LINK) {
					if(is->link_decomp_stat)
						is->link_decompressor->free(is->link_decomp_stat);
					is->link_decomp_stat = stat;
        	                        is->link_decompressor = ipc;
				}
				else {
					if(is->decomp_stat)
						is->decompressor->free(is->decomp_stat);
					is->decomp_stat = stat;
        	                        is->decompressor = ipc;
				}
			}
			return 0;
		}
		ipc = ipc->next;
	}
	return -EINVAL;
}


