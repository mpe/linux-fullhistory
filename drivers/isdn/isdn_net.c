/* $Id: isdn_net.c,v 1.55 1998/02/23 19:38:22 fritz Exp $

 * Linux ISDN subsystem, network interfaces and related functions (linklevel).
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
 * Note: This file differs from the corresponding revision as present in the
 * isdn4linux CVS repository because some later bug fixes have been extracted
 * from the repository and merged into this file. -- Henner Eisen
 *
 * $Log: isdn_net.c,v $
 * Revision 1.55  1998/02/23 19:38:22  fritz
 * Corrected check for modified feature-flags.
 *
 * Revision 1.54  1998/02/20 17:15:07  fritz
 * Changes for recent kernels.
 * Ugly workaround for adjusting Ethernet frames with recent kernels.
 * replaced direct calls to lowlevel-driver command by common hook.
 *
 * Revision 1.53  1998/01/31 22:05:54  keil
 * Lots of changes for X.25 support:
 * Added generic support for connection-controlling encapsulation protocols
 * Added support of BHUP status message
 * Added support for additional p_encap X25IFACE
 * Added support for kernels >= 2.1.72
 *
 * Revision 1.52  1998/01/31 19:29:51  calle
 * Merged changes from and for 2.1.82, not tested only compiled ...
 *
 * Revision 1.51  1997/10/09 21:28:50  fritz
 * New HL<->LL interface:
 *   New BSENT callback with nr. of bytes included.
 *   Sending without ACK.
 *   New L1 error status (not yet in use).
 *   Cleaned up obsolete structures.
 * Implemented Cisco-SLARP.
 * Changed local net-interface data to be dynamically allocated.
 * Removed old 2.0 compatibility stuff.
 *
 * Revision 1.50  1997/10/01 09:20:32  fritz
 * Removed old compatibility stuff for 2.0.X kernels.
 * From now on, this code is for 2.1.X ONLY!
 * Old stuff is still in the separate branch.
 *
 * Revision 1.49  1997/08/21 14:38:13  fritz
 * Bugfix: Did not compile without SyncPPP.
 *
 * Revision 1.48  1997/06/22 11:57:15  fritz
 * Added ability to adjust slave triggerlevel.
 *
 * Revision 1.47  1997/06/21 10:52:05  fritz
 * Removed wrong SET_SKB_FREE in isdn_net_send_skb()
 *
 * Revision 1.46  1997/06/17 13:05:24  hipp
 * Applied Eric's underflow-patches (slightly modified)
 *
 * Revision 1.45  1997/06/10 16:24:22  hipp
 * hard_header changes for syncPPP (now behaves like RAWIP)
 *
 * Revision 1.44  1997/05/27 15:17:26  fritz
 * Added changes for recent 2.1.x kernels:
 *   changed return type of isdn_close
 *   queue_task_* -> queue_task
 *   clear/set_bit -> test_and_... where apropriate.
 *   changed type of hard_header_cache parameter.
 *
 * Revision 1.43  1997/03/30 16:51:13  calle
 * changed calls to copy_from_user/copy_to_user and removed verify_area
 * were possible.
 *
 * Revision 1.42  1997/03/11 08:43:51  fritz
 * Perform a hangup if number is deleted while dialing.
 *
 * Revision 1.41  1997/03/08 08:16:31  fritz
 * Bugfix: Deleting a phone number during dial gave unpredictable results.
 *
 * Revision 1.40  1997/03/05 21:16:08  fritz
 * Fix: did not compile with 2.1.27
 *
 * Revision 1.39  1997/03/04 21:36:52  fritz
 * Added sending ICMP messages when no connetion is possible.
 *
 * Revision 1.38  1997/02/23 23:41:14  fritz
 * Bugfix: Slave interfaces have to be hung up before master.
 *
 * Revision 1.37  1997/02/11 18:32:51  fritz
 * Bugfix in isdn_ppp_free_mpqueue().
 *
 * Revision 1.36  1997/02/10 21:31:11  fritz
 * Changed setup-interface (incoming and outgoing).
 *
 * Revision 1.35  1997/02/10 20:12:45  fritz
 * Changed interface for reporting incoming calls.
 *
 * Revision 1.34  1997/02/03 23:15:07  fritz
 * Reformatted according CodingStyle.
 * replaced arp_find prototype by proper include.
 * made dev_purge_queues static.
 * Bugfix in bogocps calculation.
 * removed isdn_net_receive_callback - was never used ;-)
 * Misc. fixes for Kernel 2.1.X comaptibility.
 *
 * Revision 1.33  1997/01/17 01:19:25  fritz
 * Applied chargeint patch.
 *
 * Revision 1.32  1997/01/14 01:29:31  fritz
 * Bugfix: isdn_net_hangup() did not reset ISDN_NET_CONNECTED.
 *
 * Revision 1.31  1997/01/11 23:30:42  fritz
 * Speed up dial statemachine.
 *
 * Revision 1.30  1996/11/25 17:20:50  hipp
 * fixed pppbind bug in isdn_net_find_icall()
 *
 * Revision 1.29  1996/11/13 02:31:38  fritz
 * Minor cleanup.
 *
 * Revision 1.28  1996/10/27 20:49:06  keil
 * bugfix to compile without MPP
 *
 * Revision 1.27  1996/10/25 18:46:01  fritz
 * Another bugfix in isdn_net_autohup()
 *
 * Revision 1.26  1996/10/23 23:05:36  fritz
 * Bugfix: Divide by zero in isdn_net_autohup()
 *
 * Revision 1.25  1996/10/22 23:13:58  fritz
 * Changes for compatibility to 2.0.X and 2.1.X kernels.
 *
 * Revision 1.24  1996/10/11 13:57:40  fritz
 * Bugfix: Error in BogoCPS calculation.
 *
 * Revision 1.23  1996/09/23 01:58:08  fritz
 * Fix: With syncPPP encapsulation, discard LCP packets
 *      when calculating hangup timeout.
 *
 * Revision 1.22  1996/09/23 00:03:37  fritz
 * Fix: did not compile without CONFIG_ISDN_PPP
 *
 * Revision 1.21  1996/09/07 12:44:50  hipp
 * (hopefully) fixed callback problem with syncPPP
 * syncPPP network devices now show PPP link encap
 *
 * Revision 1.20  1996/08/29 20:06:03  fritz
 * Bugfix: Transmission timeout had been much to low.
 *
 * Revision 1.19  1996/08/12 16:24:32  hipp
 * removed some (now) obsolete functions for syncPPP in rebuild_header etc.
 *
 * Revision 1.18  1996/07/03 13:48:51  hipp
 * bugfix: Call dev_purge_queues() only for master device
 *
 * Revision 1.17  1996/06/25 18:37:37  fritz
 * Fixed return count for empty return string in isdn_net_getphones().
 *
 * Revision 1.16  1996/06/24 17:48:08  fritz
 * Bugfixes:
 *   - Did not free channel on unbinding.
 *   - ioctl returned wrong callback settings.
 *
 * Revision 1.15  1996/06/16 17:42:54  tsbogend
 * fixed problem with IP addresses on Linux/Alpha (long is 8 byte there)
 *
 * Revision 1.14  1996/06/11 14:54:08  hipp
 * minor bugfix in isdn_net_send_skb
 * changes in BSENT callback handler for syncPPP
 * added lp->sav_skb stuff
 *
 * Revision 1.13  1996/06/06 14:25:44  fritz
 * Changed loglevel of "incoming ... without OAD" message, since
 * with audio support this is quite normal.
 *
 * Revision 1.12  1996/06/05 02:36:45  fritz
 * Minor bugfixes by M. Hipp.
 *
 * Revision 1.11  1996/05/18 01:36:59  fritz
 * Added spelling corrections and some minor changes
 * to stay in sync with kernel.
 *
 * Revision 1.10  1996/05/17 03:49:01  fritz
 * Some cleanup.
 *
 * Revision 1.9  1996/05/06 11:34:57  hipp
 * fixed a few bugs
 *
 * Revision 1.8  1996/04/30 21:04:40  fritz
 * Test commit
 *
 * Revision 1.7  1996/04/30 11:10:42  fritz
 * Added Michael's ippp-bind patch.
 *
 * Revision 1.6  1996/04/30 09:34:35  fritz
 * Removed compatibility-macros.
 *
 * Revision 1.5  1996/04/20 16:28:38  fritz
 * Made more parameters of the dial statemachine user-configurable and
 * added hangup after dial for more reliability using callback.
 * Changed all io going through generic routines in isdn_common.c
 * Added missing call to dev_free_skb on failed dialing.
 * Added uihdlc encapsulation.
 * Fixed isdn_net_setcfg not to destroy interface-flags anymore.
 * Misc. typos.
 *
 * Revision 1.4  1996/02/19 15:23:38  fritz
 * Bugfix: Sync-PPP packets got compressed twice, when resent due to
 *         send-queue-full reject.
 *
 * Revision 1.3  1996/02/11 02:22:28  fritz
 * Changed status- receive-callbacks to use pointer-arrays for finding
 * a corresponding interface instead of looping over all interfaces.
 * Activate Auto-hangup-timer only when interface is online.
 * Some bugfixes in the dialing-statemachine.
 * Lot of bugfixes in sk_buff'ized encapsulation handling.
 * For speedup connection-setup after dialing, remember sk_buf that triggered
 * dialing.
 * Fixed isdn_net_log_packet according to different encapsulations.
 * Correct ARP-handling for ETHERNET-encapsulation.
 *
 * Revision 1.2  1996/01/22 05:05:12  fritz
 * Changed returncode-logic for isdn_net_start_xmit() and its
 * helper-functions.
 * Changed handling of buildheader for RAWIP and ETHERNET-encapsulation.
 *
 * Revision 1.1  1996/01/09 04:12:34  fritz
 * Initial revision
 *
 */

#include <linux/config.h>
#define __NO_VERSION__
#include <linux/module.h>
#include <linux/isdn.h>
#include <net/arp.h>
#include <net/dst.h>
#ifndef DEV_NUMBUFFS
#include <net/pkt_sched.h>
#endif
#include <linux/inetdevice.h>
#include "isdn_common.h"
#include "isdn_net.h"
#ifdef CONFIG_ISDN_PPP
#include "isdn_ppp.h"
#endif
#ifdef CONFIG_ISDN_X25
#include <linux/concap.h>
#include "isdn_concap.h"
#endif

/* Prototypes */

int isdn_net_force_dial_lp(isdn_net_local *);
static int isdn_net_wildmat(char *s, char *p);
static int isdn_net_start_xmit(struct sk_buff *, struct device *);
static int isdn_net_xmit(struct device *, isdn_net_local *, struct sk_buff *);
#ifdef DEV_NUMBUFFS
static void dev_purge_queues(struct device *dev);	/* move this to net/core/dev.c */
#endif

char *isdn_net_revision = "$Revision: 1.55 $";

 /*
  * Code for raw-networking over ISDN
  */

static void
isdn_net_unreachable(struct device *dev, struct sk_buff *skb, char *reason)
{

	if(skb) {

		u_short proto = ntohs(skb->protocol);

		printk(KERN_DEBUG "isdn_net: %s: %s, signalling dst_link_failure %s\n",
		       dev->name,
		       (reason != NULL) ? reason : "unknown",
		       (proto != ETH_P_IP) ? "Protocol != ETH_P_IP" : "");
		
		dst_link_failure(skb);
	}
}

static void
isdn_net_reset(struct device *dev)
{
#ifdef CONFIG_ISDN_X25
	struct concap_device_ops * dops = 
		( (isdn_net_local *) dev->priv ) -> dops;
	struct concap_proto * cprot = 
		( (isdn_net_local *) dev->priv ) -> netdev -> cprot; 
#endif
	ulong flags;

	save_flags(flags);
	cli();                  /* Avoid glitch on writes to CMD regs */
	dev->interrupt = 0;
	dev->tbusy = 0;
#ifdef CONFIG_ISDN_X25
	if( cprot && cprot -> pops && dops ) 
		cprot -> pops -> restart ( cprot, dev, dops );
#endif
	restore_flags(flags);
}

/* Open/initialize the board. */
static int
isdn_net_open(struct device *dev)
{
	int i;
	struct device *p;
	struct in_device *in_dev;

	isdn_net_reset(dev);
	dev->start = 1;
	/* Fill in the MAC-level header (not needed, but for compatibility... */
	for (i = 0; i < ETH_ALEN - sizeof(u32); i++)
		dev->dev_addr[i] = 0xfc;
	if ((in_dev = dev->ip_ptr) != NULL) {
		/*
		 *      Any address will do - we take the first
		 */
		struct in_ifaddr *ifa = in_dev->ifa_list;
		if (ifa != NULL)
			memcpy(dev->dev_addr+2, &ifa->ifa_local, 4);
	}
 
	/* If this interface has slaves, start them also */

	if ((p = (((isdn_net_local *) dev->priv)->slave))) {
		while (p) {
			isdn_net_reset(p);
			p->start = 1;
			p = (((isdn_net_local *) p->priv)->slave);
		}
	}
	isdn_MOD_INC_USE_COUNT();
	return 0;
}

/*
 * Assign an ISDN-channel to a net-interface
 */
static void
isdn_net_bind_channel(isdn_net_local * lp, int idx)
{
	ulong flags;

	save_flags(flags);
	cli();
	lp->isdn_device = dev->drvmap[idx];
	lp->isdn_channel = dev->chanmap[idx];
	dev->rx_netdev[idx] = lp->netdev;
	dev->st_netdev[idx] = lp->netdev;
	restore_flags(flags);
}

/*
 * unbind a net-interface (resets interface after an error)
 */
static void
isdn_net_unbind_channel(isdn_net_local * lp)
{
	ulong flags;

	save_flags(flags);
	cli();
	if (lp->first_skb) {
		dev_kfree_skb(lp->first_skb);
		lp->first_skb = NULL;
	}
	if (lp->sav_skb) {
		dev_kfree_skb(lp->sav_skb);
		lp->sav_skb = NULL;
	}
#ifdef DEV_NUMBUFFS
	if (!lp->master)        /* purge only for master device */
		dev_purge_queues(&lp->netdev->dev);
#else
	if (!lp->master) {	/* reset only master device */
		/* Moral equivalent of dev_purge_queues():
		   BEWARE! This chunk of code cannot be called from hardware
		   interrupt handler. I hope it is true. --ANK
		 */
		qdisc_reset(lp->netdev->dev.qdisc);
	}
#endif
	lp->dialstate = 0;
	dev->rx_netdev[isdn_dc2minor(lp->isdn_device, lp->isdn_channel)] = NULL;
	dev->st_netdev[isdn_dc2minor(lp->isdn_device, lp->isdn_channel)] = NULL;
	isdn_free_channel(lp->isdn_device, lp->isdn_channel, ISDN_USAGE_NET);
	lp->flags &= ~ISDN_NET_CONNECTED;
	lp->isdn_device = -1;
	lp->isdn_channel = -1;

	restore_flags(flags);
}

/*
 * Perform auto-hangup and cps-calculation for net-interfaces.
 *
 * auto-hangup:
 * Increment idle-counter (this counter is reset on any incoming or
 * outgoing packet), if counter exceeds configured limit either do a
 * hangup immediately or - if configured - wait until just before the next
 * charge-info.
 *
 * cps-calculation (needed for dynamic channel-bundling):
 * Since this function is called every second, simply reset the
 * byte-counter of the interface after copying it to the cps-variable.
 */
unsigned long last_jiffies = -HZ;

void
isdn_net_autohup()
{
	isdn_net_dev *p = dev->netdev;
	int anymore;

	anymore = 0;
	while (p) {
		isdn_net_local *l = p->local;
		if ((jiffies - last_jiffies) == 0)
			l->cps = l->transcount;
		else
			l->cps = (l->transcount * HZ) / (jiffies - last_jiffies);
		l->transcount = 0;
		if (dev->net_verbose > 3)
			printk(KERN_DEBUG "%s: %d bogocps\n", l->name, l->cps);
		if ((l->flags & ISDN_NET_CONNECTED) && (!l->dialstate)) {
			anymore = 1;
			l->huptimer++;
			if ((l->onhtime) && (l->huptimer > l->onhtime)) {
				if (l->hupflags & ISDN_MANCHARGE &&
				    l->hupflags & ISDN_CHARGEHUP) {
					while (jiffies - l->chargetime > l->chargeint)
						l->chargetime += l->chargeint;
					if (jiffies - l->chargetime >= l->chargeint - 2 * HZ)
						if (l->outgoing || l->hupflags & ISDN_INHUP)
							isdn_net_hangup(&p->dev);
				} else if (l->outgoing) {
					if (l->hupflags & ISDN_CHARGEHUP) {
						if (l->hupflags & ISDN_WAITCHARGE) {
							printk(KERN_DEBUG "isdn_net: Hupflags of %s are %X\n",
							       l->name, l->hupflags);
							isdn_net_hangup(&p->dev);
						} else if (jiffies - l->chargetime > l->chargeint) {
							printk(KERN_DEBUG
							       "isdn_net: %s: chtime = %d, chint = %d\n",
							       l->name, l->chargetime, l->chargeint);
							isdn_net_hangup(&p->dev);
						}
					} else
						isdn_net_hangup(&p->dev);
				} else if (l->hupflags & ISDN_INHUP)
					isdn_net_hangup(&p->dev);
			}
		}
		p = (isdn_net_dev *) p->next;
	}
	last_jiffies = jiffies;
	isdn_timer_ctrl(ISDN_TIMER_NETHANGUP, anymore);
}

/*
 * Handle status-messages from ISDN-interfacecard.
 * This function is called from within the main-status-dispatcher
 * isdn_status_callback, which itself is called from the low-level driver.
 * Return: 1 = Event handled, 0 = not for us or unknown Event.
 */
int
isdn_net_stat_callback(int idx, isdn_ctrl *c)
{
	isdn_net_dev *p = dev->st_netdev[idx];
	int cmd = c->command;
	
	if (p) {
		isdn_net_local *lp = p->local;
#ifdef CONFIG_ISDN_X25
		struct concap_proto *cprot = lp -> netdev -> cprot;
		struct concap_proto_ops *pops = cprot ? cprot -> pops : 0;
#endif
		switch (cmd) {
			case ISDN_STAT_BSENT:
				/* A packet has successfully been sent out */
				if ((lp->flags & ISDN_NET_CONNECTED) &&
				    (!lp->dialstate)) {
					lp->stats.tx_packets++;
					lp->stats.tx_bytes += c->parm.length;
					if (lp->p_encap == ISDN_NET_ENCAP_SYNCPPP && lp->sav_skb) {
						struct device *mdev;
						if (lp->master)
							mdev = lp->master;
						else
							mdev = &lp->netdev->dev;
						if (!isdn_net_send_skb(mdev, lp, lp->sav_skb)) {
							lp->sav_skb = NULL;
							mark_bh(NET_BH);
						} else {
							return 1;
						}
					}
					if (test_and_clear_bit(0, (void *) &(p->dev.tbusy)))
						mark_bh(NET_BH);
				}
				return 1;
			case ISDN_STAT_DCONN:
				/* D-Channel is up */
				switch (lp->dialstate) {
					case 4:
					case 7:
					case 8:
						lp->dialstate++;
						return 1;
					case 12:
						lp->dialstate = 5;
						return 1;
				}
				break;
			case ISDN_STAT_DHUP:
				/* Either D-Channel-hangup or error during dialout */
#ifdef CONFIG_ISDN_X25
				/* If we are not connencted then dialing had
				   failed. If there are generic encap protocol
				   receiver routines signal the closure of
				   the link*/
				
				if( !(lp->flags & ISDN_NET_CONNECTED) 
				    && pops && pops -> disconn_ind )
					pops -> disconn_ind(cprot);
#endif /* CONFIG_ISDN_X25 */
				if ((!lp->dialstate) && (lp->flags & ISDN_NET_CONNECTED)) {
					lp->flags &= ~ISDN_NET_CONNECTED;
					if (lp->first_skb) {
						dev_kfree_skb(lp->first_skb);
						lp->first_skb = NULL;
					}
					if (lp->sav_skb) {
						dev_kfree_skb(lp->sav_skb);
						lp->sav_skb = NULL;
					}
					isdn_free_channel(lp->isdn_device, lp->isdn_channel,
							  ISDN_USAGE_NET);
#ifdef CONFIG_ISDN_PPP
					isdn_ppp_free(lp);
#endif
					isdn_all_eaz(lp->isdn_device, lp->isdn_channel);
					printk(KERN_INFO "%s: remote hangup\n", lp->name);
					printk(KERN_INFO "%s: Chargesum is %d\n", lp->name,
					       lp->charge);
					lp->isdn_device = -1;
					lp->isdn_channel = -1;
					dev->st_netdev[idx] = NULL;
					dev->rx_netdev[idx] = NULL;
					return 1;
				}
				break;
#ifdef CONFIG_ISDN_X25
			case ISDN_STAT_BHUP:
				/* B-Channel-hangup */
				/* try if there are generic encap protocol
				   receiver routines and signal the closure of
				   the link */
				if( pops  &&  pops -> disconn_ind ){
						pops -> disconn_ind(cprot);
						return 1;
					}
				break;
#endif /* CONFIG_ISDN_X25 */
			case ISDN_STAT_BCONN:
				/* B-Channel is up */
				switch (lp->dialstate) {
					case 5:
					case 6:
					case 7:
					case 8:
					case 9:
					case 10:
					case 12:
						if (lp->dialstate <= 6) {
							dev->usage[idx] |= ISDN_USAGE_OUTGOING;
							isdn_info_update();
						} else
							dev->rx_netdev[idx] = p;
						lp->dialstate = 0;
						isdn_timer_ctrl(ISDN_TIMER_NETHANGUP, 1);
						if (lp->p_encap == ISDN_NET_ENCAP_CISCOHDLCK)
							isdn_timer_ctrl(ISDN_TIMER_KEEPALIVE, 1);
						printk(KERN_INFO "isdn_net: %s connected\n", lp->name);
						/* If first Chargeinfo comes before B-Channel connect,
						 * we correct the timestamp here.
						 */
						lp->chargetime = jiffies;
						printk(KERN_DEBUG "isdn_net: chargetime of %s now %d\n",
						lp->name, lp->chargetime);
						/* Immediately send first skb to speed up arp */
#ifdef CONFIG_ISDN_PPP
						if (lp->p_encap == ISDN_NET_ENCAP_SYNCPPP)
							isdn_ppp_wakeup_daemon(lp);
#endif
#ifdef CONFIG_ISDN_X25
						/* try if there are generic concap receiver routines */
						if( pops )
							if( pops->connect_ind)
								pops->connect_ind(cprot);

#endif /* CONFIG_ISDN_X25 */
						if (lp->first_skb) {
							
							if (!(isdn_net_xmit(&p->dev, lp, lp->first_skb)))
								lp->first_skb = NULL;
						} else {
							/*
							 * dev.tbusy is usually cleared implicitly by isdn_net_xmit(,,lp->first_skb).
							 * With an empty lp->first_skb, we need to do this ourselves
							 */
							lp->netdev->dev.tbusy = 0;
							mark_bh(NET_BH);
						}
						return 1;
				}
				break;
			case ISDN_STAT_NODCH:
				/* No D-Channel avail. */
				if (lp->dialstate == 4) {
					lp->dialstate--;
					return 1;
				}
				break;
			case ISDN_STAT_CINF:
				/* Charge-info from TelCo. Calculate interval between
				 * charge-infos and set timestamp for last info for
				 * usage by isdn_net_autohup()
				 */
				lp->charge++;
				if (lp->hupflags & ISDN_HAVECHARGE) {
					lp->hupflags &= ~ISDN_WAITCHARGE;
					lp->chargeint = jiffies - lp->chargetime - (2 * HZ);
				}
				if (lp->hupflags & ISDN_WAITCHARGE)
					lp->hupflags |= ISDN_HAVECHARGE;
				lp->chargetime = jiffies;
				printk(KERN_DEBUG "isdn_net: Got CINF chargetime of %s now %d\n",
				       lp->name, lp->chargetime);
				return 1;
		}
	}
	return 0;
}

/*
 * Check, if a number contains wildcard-characters, in which case it
 * is for incoming purposes only.
 */
static int
isdn_net_checkwild(char *num)
{
	return ((strchr(num, '?')) ||
		(strchr(num, '*')) ||
		(strchr(num, '[')) ||
		(strchr(num, ']')) ||
		(strchr(num, '^')));
}

/*
 * Perform dialout for net-interfaces and timeout-handling for
 * D-Channel-up and B-Channel-up Messages.
 * This function is initially called from within isdn_net_start_xmit() or
 * or isdn_net_find_icall() after initializing the dialstate for an
 * interface. If further calls are needed, the function schedules itself
 * for a timer-callback via isdn_timer_function().
 * The dialstate is also affected by incoming status-messages from
 * the ISDN-Channel which are handled in isdn_net_stat_callback() above.
 */
void
isdn_net_dial(void)
{
	isdn_net_dev *p = dev->netdev;
	int anymore = 0;
	int i;
	int flags;
	isdn_ctrl cmd;

	while (p) {
		isdn_net_local *lp = p->local;

#ifdef ISDN_DEBUG_NET_DIAL
		if (lp->dialstate)
			printk(KERN_DEBUG "%s: dialstate=%d\n", lp->name, lp->dialstate);
#endif
		switch (lp->dialstate) {
			case 0:
				/* Nothing to do for this interface */
				break;
			case 1:
				/* Initiate dialout. Set phone-number-pointer to first number
				 * of interface.
				 */
				save_flags(flags);
				cli();
				lp->dial = lp->phone[1];
				restore_flags(flags);
				if (!lp->dial) {
					printk(KERN_WARNING "%s: phone number deleted?\n",
					       lp->name);
					isdn_net_hangup(&p->dev);
					break;
				}
				anymore = 1;
				lp->dialstate++;
				/* Fall through */
			case 2:
				/* Prepare dialing. Clear EAZ, then set EAZ. */
				cmd.driver = lp->isdn_device;
				cmd.arg = lp->isdn_channel;
				cmd.command = ISDN_CMD_CLREAZ;
				isdn_command(&cmd);
				sprintf(cmd.parm.num, "%s", isdn_map_eaz2msn(lp->msn, cmd.driver));
				cmd.command = ISDN_CMD_SETEAZ;
				isdn_command(&cmd);
				lp->dialretry = 0;
				anymore = 1;
				lp->dialstate++;
				/* Falls through */
			case 3:
				/* Setup interface, dial current phone-number, switch to next number.
				 * If list of phone-numbers is exhausted, increment
				 * retry-counter.
				 */
				cmd.driver = lp->isdn_device;
				cmd.command = ISDN_CMD_SETL2;
				cmd.arg = lp->isdn_channel + (lp->l2_proto << 8);
				isdn_command(&cmd);
				cmd.driver = lp->isdn_device;
				cmd.command = ISDN_CMD_SETL3;
				cmd.arg = lp->isdn_channel + (lp->l3_proto << 8);
				isdn_command(&cmd);
				cmd.driver = lp->isdn_device;
				cmd.arg = lp->isdn_channel;
				save_flags(flags);
				cli();
				if (!lp->dial) {
					restore_flags(flags);
					printk(KERN_WARNING "%s: phone number deleted?\n",
					       lp->name);
					isdn_net_hangup(&p->dev);
					break;
				}
				if (!strcmp(lp->dial->num, "LEASED")) {
					restore_flags(flags);
					lp->dialstate = 4;
					printk(KERN_INFO "%s: Open leased line ...\n", lp->name);
				} else {
					sprintf(cmd.parm.setup.phone, "%s", lp->dial->num);
					/*
					 * Switch to next number or back to start if at end of list.
					 */
					if (!(lp->dial = (isdn_net_phone *) lp->dial->next)) {
						lp->dial = lp->phone[1];
						lp->dialretry++;
					}
					restore_flags(flags);
					cmd.driver = lp->isdn_device;
					cmd.command = ISDN_CMD_DIAL;
					cmd.parm.setup.si1 = 7;
					cmd.parm.setup.si2 = 0;
					sprintf(cmd.parm.setup.eazmsn, "%s",
						isdn_map_eaz2msn(lp->msn, cmd.driver));
					i = isdn_dc2minor(lp->isdn_device, lp->isdn_channel);
					if (i >= 0) {
						strcpy(dev->num[i], cmd.parm.setup.phone);
						isdn_info_update();
					}
					printk(KERN_INFO "%s: dialing %d %s...\n", lp->name,
					       lp->dialretry - 1, cmd.parm.setup.phone);
					lp->dtimer = 0;
#ifdef ISDN_DEBUG_NET_DIAL
					printk(KERN_DEBUG "dial: d=%d c=%d\n", lp->isdn_device,
					       lp->isdn_channel);
#endif
					isdn_command(&cmd);
				}
				lp->huptimer = 0;
				lp->outgoing = 1;
				if (lp->chargeint) {
					lp->hupflags |= ISDN_HAVECHARGE;
					lp->hupflags &= ~ISDN_WAITCHARGE;
				} else {
					lp->hupflags |= ISDN_WAITCHARGE;
					lp->hupflags &= ~ISDN_HAVECHARGE;
				}
				anymore = 1;
				lp->dialstate =
				    (lp->cbdelay &&
				     (lp->flags & ISDN_NET_CBOUT)) ? 12 : 4;
				break;
			case 4:
				/* Wait for D-Channel-connect.
				 * If timeout and max retries not
				 * reached, switch back to state 3.
				 */
				if (lp->dtimer++ > ISDN_TIMER_DTIMEOUT10) {
					if (lp->dialretry < lp->dialmax) {
						lp->dialstate = 3;
					} else
						isdn_net_hangup(&p->dev);
				}
				anymore = 1;
				break;
			case 5:
				/* Got D-Channel-Connect, send B-Channel-request */
				cmd.driver = lp->isdn_device;
				cmd.arg = lp->isdn_channel;
				cmd.command = ISDN_CMD_ACCEPTB;
				anymore = 1;
				lp->dtimer = 0;
				lp->dialstate++;
				isdn_command(&cmd);
				break;
			case 6:
				/* Wait for B- or D-Channel-connect. If timeout,
				 * switch back to state 3.
				 */
#ifdef ISDN_DEBUG_NET_DIAL
				printk(KERN_DEBUG "dialtimer2: %d\n", lp->dtimer);
#endif
				if (lp->dtimer++ > ISDN_TIMER_DTIMEOUT10)
					lp->dialstate = 3;
				anymore = 1;
				break;
			case 7:
				/* Got incoming Call, setup L2 and L3 protocols,
				 * then wait for D-Channel-connect
				 */
#ifdef ISDN_DEBUG_NET_DIAL
				printk(KERN_DEBUG "dialtimer4: %d\n", lp->dtimer);
#endif
				cmd.driver = lp->isdn_device;
				cmd.command = ISDN_CMD_SETL2;
				cmd.arg = lp->isdn_channel + (lp->l2_proto << 8);
				isdn_command(&cmd);
				cmd.driver = lp->isdn_device;
				cmd.command = ISDN_CMD_SETL3;
				cmd.arg = lp->isdn_channel + (lp->l3_proto << 8);
				isdn_command(&cmd);
				if (lp->dtimer++ > ISDN_TIMER_DTIMEOUT15)
					isdn_net_hangup(&p->dev);
				else {
					anymore = 1;
					lp->dialstate++;
				}
				break;
			case 9:
				/* Got incoming D-Channel-Connect, send B-Channel-request */
				cmd.driver = lp->isdn_device;
				cmd.arg = lp->isdn_channel;
				cmd.command = ISDN_CMD_ACCEPTB;
				isdn_command(&cmd);
				anymore = 1;
				lp->dtimer = 0;
				lp->dialstate++;
				break;
			case 8:
			case 10:
				/*  Wait for B- or D-channel-connect */
#ifdef ISDN_DEBUG_NET_DIAL
				printk(KERN_DEBUG "dialtimer4: %d\n", lp->dtimer);
#endif
				if (lp->dtimer++ > ISDN_TIMER_DTIMEOUT10)
					isdn_net_hangup(&p->dev);
				else
					anymore = 1;
				break;
			case 11:
				/* Callback Delay */
				if (lp->dtimer++ > lp->cbdelay)
					lp->dialstate = 1;
				anymore = 1;
				break;
			case 12:
				/* Remote does callback. Hangup after cbdelay, then wait for incoming
				 * call (in state 4).
				 */
				if (lp->dtimer++ > lp->cbdelay) {
					printk(KERN_INFO "%s: hangup waiting for callback ...\n", lp->name);
					lp->dtimer = 0;
					lp->dialstate = 4;
					cmd.driver = lp->isdn_device;
					cmd.command = ISDN_CMD_HANGUP;
					cmd.arg = lp->isdn_channel;
					isdn_command(&cmd);
					isdn_all_eaz(lp->isdn_device, lp->isdn_channel);
				}
				anymore = 1;
				break;
			default:
				printk(KERN_WARNING "isdn_net: Illegal dialstate %d for device %s\n",
				       lp->dialstate, lp->name);
		}
		p = (isdn_net_dev *) p->next;
	}
	isdn_timer_ctrl(ISDN_TIMER_NETDIAL, anymore);
}

/*
 * Perform hangup for a net-interface.
 */
void
isdn_net_hangup(struct device *d)
{
	isdn_net_local *lp = (isdn_net_local *) d->priv;
	isdn_ctrl cmd;
#ifdef CONFIG_ISDN_X25
	struct concap_proto *cprot = lp -> netdev -> cprot;
	struct concap_proto_ops *pops = cprot ? cprot -> pops : 0;
#endif

	if (lp->flags & ISDN_NET_CONNECTED) {
		lp->flags &= ~ISDN_NET_CONNECTED;
		printk(KERN_INFO "isdn_net: local hangup %s\n", lp->name);
#ifdef CONFIG_ISDN_PPP
		isdn_ppp_free(lp);
#endif
#ifdef CONFIG_ISDN_X25
		/* try if there are generic encap protocol
		   receiver routines and signal the closure of
		   the link */		
		if( pops && pops -> disconn_ind )
		  pops -> disconn_ind(cprot);
#endif /* CONFIG_ISDN_X25 */

		cmd.driver = lp->isdn_device;
		cmd.command = ISDN_CMD_HANGUP;
		cmd.arg = lp->isdn_channel;
		isdn_command(&cmd);
		printk(KERN_INFO "%s: Chargesum is %d\n", lp->name, lp->charge);
		isdn_all_eaz(lp->isdn_device, lp->isdn_channel);
	}
	isdn_net_unbind_channel(lp);
}

typedef struct {
	unsigned short source;
	unsigned short dest;
} ip_ports;

static void
isdn_net_log_skb(struct sk_buff * skb, isdn_net_local * lp)
{
	u_char *p = skb->nh.raw; /* hopefully, this was set correctly */
	unsigned short proto = ntohs(skb->protocol);
	int data_ofs;
	ip_ports *ipp;
	char addinfo[100];

	addinfo[0] = '\0';
	/* This check stolen from 2.1.72 dev_queue_xmit_nit() */ 
	if (skb->nh.raw < skb->data || skb->nh.raw >= skb->tail) {
		/* fall back to old isdn_net_log_packet method() */
		char * buf = skb->data;

		printk(KERN_DEBUG "isdn_net: protocol %04x is buggy, dev %s\n", skb->protocol, lp->name);
		p = buf;
		proto = ETH_P_IP;
		switch (lp->p_encap) {
			case ISDN_NET_ENCAP_IPTYP:
				proto = ntohs(*(unsigned short *) &buf[0]);
				p = &buf[2];
				break;
			case ISDN_NET_ENCAP_ETHER:
				proto = ntohs(*(unsigned short *) &buf[12]);
				p = &buf[14];
				break;
			case ISDN_NET_ENCAP_CISCOHDLC:
				proto = ntohs(*(unsigned short *) &buf[2]);
				p = &buf[4];
				break;
#ifdef CONFIG_ISDN_PPP
			case ISDN_NET_ENCAP_SYNCPPP:
				proto = ntohs(skb->protocol);
				p = &buf[IPPP_MAX_HEADER];
				break;
#endif
		}
	}
	data_ofs = ((p[0] & 15) * 4);
	switch (proto) {
		case ETH_P_IP:
			switch (p[9]) {
				case 1:
					strcpy(addinfo, " ICMP");
					break;
				case 2:
					strcpy(addinfo, " IGMP");
					break;
				case 4:
					strcpy(addinfo, " IPIP");
					break;
				case 6:
					ipp = (ip_ports *) (&p[data_ofs]);
					sprintf(addinfo, " TCP, port: %d -> %d", ntohs(ipp->source),
						ntohs(ipp->dest));
					break;
				case 8:
					strcpy(addinfo, " EGP");
					break;
				case 12:
					strcpy(addinfo, " PUP");
					break;
				case 17:
					ipp = (ip_ports *) (&p[data_ofs]);
					sprintf(addinfo, " UDP, port: %d -> %d", ntohs(ipp->source),
						ntohs(ipp->dest));
					break;
				case 22:
					strcpy(addinfo, " IDP");
					break;
			}
			printk(KERN_INFO "OPEN: %d.%d.%d.%d -> %d.%d.%d.%d%s\n",
			       p[12], p[13], p[14], p[15],
			       p[16], p[17], p[18], p[19],
			       addinfo);
			break;
		case ETH_P_ARP:
			printk(KERN_INFO "OPEN: ARP %d.%d.%d.%d -> *.*.*.* ?%d.%d.%d.%d\n",
			       p[14], p[15], p[16], p[17],
			       p[24], p[25], p[26], p[27]);
			break;
	}
}

/*
 * Generic routine to send out an skbuf.
 * If lowlevel-device does not support support skbufs, use
 * standard send-routine, else send directly.
 *
 * Return: 0 on success, !0 on failure.
 * Side-effects: ndev->tbusy is cleared on success.
 */
int
isdn_net_send_skb(struct device *ndev, isdn_net_local * lp,
		  struct sk_buff *skb)
{
	int ret;
	int len = skb->len;     /* save len */

	ret = isdn_writebuf_skb_stub(lp->isdn_device, lp->isdn_channel, 1, skb);
	if (ret == len) {
		lp->transcount += len;
		clear_bit(0, (void *) &(ndev->tbusy));
		mark_bh(NET_BH);
		return 0;
	}
	if (ret < 0) {
		dev_kfree_skb(skb);
		lp->stats.tx_errors++;
		clear_bit(0, (void *) &(ndev->tbusy));
		mark_bh(NET_BH);
		return 0;
	}
	return 1;
}


/*
 *  Helper function for isdn_net_start_xmit.
 *  When called, the connection is already established.
 *  Based on cps-calculation, check if device is overloaded.
 *  If so, and if a slave exists, trigger dialing for it.
 *  If any slave is online, deliver packets using a simple round robin
 *  scheme.
 *
 *  Return: 0 on success, !0 on failure.
 */

static int
isdn_net_xmit(struct device *ndev, isdn_net_local * lp, struct sk_buff *skb)
{
	int ret;

	/* For the other encaps the header has already been built */
#ifdef CONFIG_ISDN_PPP
	if (lp->p_encap == ISDN_NET_ENCAP_SYNCPPP) {
		return isdn_ppp_xmit(skb, ndev);
	}
#endif
	/* Reset hangup-timeout */
	lp->huptimer = 0;
	if (lp->cps > lp->triggercps) {
		/* Device overloaded */

		/*
		 * Packet-delivery via round-robin over master
		 * and all connected slaves.
		 */
		if (lp->master)
			/* Slaves always deliver themselves */
			ret = isdn_net_send_skb(ndev, lp, skb);
		else {
			isdn_net_local *slp = (isdn_net_local *) (lp->srobin->priv);
			/* Master delivers via srobin and maintains srobin */
			if (lp->srobin == ndev)
				ret = isdn_net_send_skb(ndev, lp, skb);
			else
				ret = ndev->tbusy = isdn_net_start_xmit(skb, lp->srobin);
			lp->srobin = (slp->slave) ? slp->slave : ndev;
			slp = (isdn_net_local *) (lp->srobin->priv);
			if (!((slp->flags & ISDN_NET_CONNECTED) && (slp->dialstate == 0)))
				lp->srobin = ndev;
		}
		/* Slave-startup using delay-variable */
		if (lp->slave) {
			if (!lp->sqfull) {
				/* First time overload: set timestamp only */
				lp->sqfull = 1;
				lp->sqfull_stamp = jiffies;
			} else {
				/* subsequent overload: if slavedelay exceeded, start dialing */
				if ((jiffies - lp->sqfull_stamp) > lp->slavedelay)
					isdn_net_force_dial_lp((isdn_net_local *) lp->slave->priv);
			}
		}
	} else {
		/* Not overloaded, deliver locally */
		ret = isdn_net_send_skb(ndev, lp, skb);
		if (lp->sqfull && ((jiffies - lp->sqfull_stamp) > (lp->slavedelay + (10 * HZ))))
			lp->sqfull = 0;
	}
	return ret;
}

static void
isdn_net_adjust_hdr(struct sk_buff *skb, struct device *dev)
{
	isdn_net_local *lp = (isdn_net_local *) dev->priv;
	if (!skb)
		return;
	if (lp->p_encap == ISDN_NET_ENCAP_ETHER) {
		ulong pullsize = (ulong)skb->nh.raw - (ulong)skb->data - ETH_HLEN;
		if (pullsize)
			skb_pull(skb, pullsize);
	}
}

/*
 * Try sending a packet.
 * If this interface isn't connected to a ISDN-Channel, find a free channel,
 * and start dialing.
 */
static int
isdn_net_start_xmit(struct sk_buff *skb, struct device *ndev)
{
	isdn_net_local *lp = (isdn_net_local *) ndev->priv;
#ifdef CONFIG_ISDN_X25
	struct concap_proto * cprot = lp -> netdev -> cprot; 
#endif

	if (ndev->tbusy) {
		if (jiffies - ndev->trans_start < (2 * HZ))
			return 1;
		if (!lp->dialstate)
			lp->stats.tx_errors++;
		ndev->trans_start = jiffies;
	}
	ndev->tbusy = 1; /* left instead of obsolete test_and_set_bit() */
#ifdef CONFIG_ISDN_X25
/* At this point hard_start_xmit() passes control to the encapsulation
   protocol (if present). 
   For X.25 auto-dialing is completly bypassed because:
   - It does not conform with the semantics of a reliable datalink
     service as needed by X.25 PLP.
   - I don't want that the interface starts dialing when the network layer
     sends a message which requests to disconnect the lapb link (or if it
     sends any other message not resulting in data transmission).
   Instead, dialing will be initiated by the encapsulation protocol entity
   when a dl_establish request is received from the upper layer.
*/
	if( cprot ) {
		return  cprot -> pops -> encap_and_xmit ( cprot , skb);
	} else
#endif
	/* auto-dialing xmit function */
	{
#ifdef ISDN_DEBUG_NET_DUMP
		u_char *buf;
#endif
		isdn_net_adjust_hdr(skb, ndev);
#ifdef ISDN_DEBUG_NET_DUMP
		buf = skb->data;
		isdn_dumppkt("S:", buf, skb->len, 40);
#endif
		if (!(lp->flags & ISDN_NET_CONNECTED)) {
			int chi;
			if (lp->phone[1]) {
				ulong flags;
				save_flags(flags);
				cli();
				/* Grab a free ISDN-Channel */
				if ((chi =
				     isdn_get_free_channel(ISDN_USAGE_NET,
							   lp->l2_proto,
							   lp->l3_proto,
							   lp->pre_device,
						 lp->pre_channel)) < 0) {
					restore_flags(flags);
					isdn_net_unreachable(ndev, skb,
							   "No channel");
					dev_kfree_skb(skb);
					ndev->tbusy = 0;
					return 0;
				}
				/* Log packet, which triggered dialing */
				if (dev->net_verbose)
					isdn_net_log_skb(skb, lp);
				lp->dialstate = 1;
				lp->flags |= ISDN_NET_CONNECTED;
				/* Connect interface with channel */
				isdn_net_bind_channel(lp, chi);
#ifdef CONFIG_ISDN_PPP
				if (lp->p_encap == ISDN_NET_ENCAP_SYNCPPP) {
					/* no 'first_skb' handling for syncPPP */
					if (isdn_ppp_bind(lp) < 0) {
						dev_kfree_skb(skb);
						isdn_net_unbind_channel(lp);
						restore_flags(flags);
						return 0;	/* STN (skb to nirvana) ;) */
					}
					restore_flags(flags);
					isdn_net_dial();	/* Initiate dialing */
					return 1;	/* let upper layer requeue skb packet */
				}
#endif
				/* remember first skb to speed up arp
				 * when using encap ETHER
				 */
				if (lp->first_skb) {
					printk(KERN_WARNING "isdn_net_start_xmit: First skb already set!\n");
					dev_kfree_skb(lp->first_skb);
					lp->first_skb = NULL;
				}
				lp->first_skb = skb;
				/* Initiate dialing */
				ndev->tbusy = 0;
				restore_flags(flags);
				isdn_net_dial();
				return 0;
			} else {
				isdn_net_unreachable(ndev, skb,
						     "No phone number");
				dev_kfree_skb(skb);
				ndev->tbusy = 0;
				return 0;
			}
		} else {
			/* Connection is established, try sending */
			ndev->trans_start = jiffies;
			if (!lp->dialstate) {
				if (lp->first_skb) {
					if (isdn_net_xmit(ndev, lp, lp->first_skb))
						return 1;
					lp->first_skb = NULL;
				}
				return (isdn_net_xmit(ndev, lp, skb));
			} else
				ndev->tbusy = 1;
		}
	}
	return 1;
}

/*
 * Shutdown a net-interface.
 */
static int
isdn_net_close(struct device *dev)
{
	struct device *p;
#ifdef CONFIG_ISDN_X25
	struct concap_proto * cprot = 
		( (isdn_net_local *) dev->priv ) -> netdev -> cprot; 
	/* printk(KERN_DEBUG "isdn_net_close %s\n" , dev-> name ); */
#endif

#ifdef CONFIG_ISDN_X25
	if( cprot && cprot -> pops ) cprot -> pops -> close( cprot );
#endif
	dev->tbusy = 1;
	dev->start = 0;
	if ((p = (((isdn_net_local *) dev->priv)->slave))) {
		/* If this interface has slaves, stop them also */
		while (p) {
#ifdef CONFIG_ISDN_X25
			cprot = ( (isdn_net_local *) p->priv ) 
				-> netdev -> cprot; 
			if( cprot && cprot -> pops ) 
				cprot -> pops -> close( cprot );
#endif
			isdn_net_hangup(p);
			p->tbusy = 1;
			p->start = 0;
			p = (((isdn_net_local *) p->priv)->slave);
		}
	}
	isdn_net_hangup(dev);
	isdn_MOD_DEC_USE_COUNT();
	return 0;
}

/*
 * Get statistics
 */
static struct enet_statistics *
isdn_net_get_stats(struct device *dev)
{
	isdn_net_local *lp = (isdn_net_local *) dev->priv;
	return &lp->stats;
}

/*      This is simply a copy from std. eth.c EXCEPT we pull ETH_HLEN
 *      instead of dev->hard_header_len off. This is done because the
 *      lowlevel-driver has already pulled off its stuff when we get
 *      here and this routine only gets called with p_encap == ETHER.
 *      Determine the packet's protocol ID. The rule here is that we
 *      assume 802.3 if the type field is short enough to be a length.
 *      This is normal practice and works for any 'now in use' protocol.
 */

static unsigned short
isdn_net_type_trans(struct sk_buff *skb, struct device *dev)
{
	struct ethhdr *eth;
	unsigned char *rawp;

	skb->mac.raw = skb->data;
	skb_pull(skb, ETH_HLEN);
	eth = skb->mac.ethernet;

	if (*eth->h_dest & 1) {
		if (memcmp(eth->h_dest, dev->broadcast, ETH_ALEN) == 0)
			skb->pkt_type = PACKET_BROADCAST;
		else
			skb->pkt_type = PACKET_MULTICAST;
	}
	/*
	 *      This ALLMULTI check should be redundant by 1.4
	 *      so don't forget to remove it.
	 */

	else if (dev->flags & (IFF_PROMISC /*| IFF_ALLMULTI*/)) {
		if (memcmp(eth->h_dest, dev->dev_addr, ETH_ALEN))
			skb->pkt_type = PACKET_OTHERHOST;
	}
	if (ntohs(eth->h_proto) >= 1536)
		return eth->h_proto;

	rawp = skb->data;

	/*
	 *      This is a magic hack to spot IPX packets. Older Novell breaks
	 *      the protocol design and runs IPX over 802.3 without an 802.2 LLC
	 *      layer. We look for FFFF which isn't a used 802.2 SSAP/DSAP. This
	 *      won't work for fault tolerant netware but does for the rest.
	 */
	if (*(unsigned short *) rawp == 0xFFFF)
		return htons(ETH_P_802_3);
	/*
	 *      Real 802.2 LLC
	 */
	return htons(ETH_P_802_2);
}

static void
isdn_net_slarp_send(isdn_net_local *lp, int is_reply)
{
	unsigned short hl = dev->drv[lp->isdn_device]->interface->hl_hdrlen;
	struct sk_buff *skb = dev_alloc_skb(hl + sizeof(cisco_hdr) + sizeof(cisco_slarp));
	unsigned long t = (jiffies / HZ * 1000000);
	int len;
	cisco_hdr *ch;
	cisco_slarp *s;
	
	if (!skb) {
		printk(KERN_WARNING
		       "%s: Could not allocate SLARP reply\n", lp->name);
		return;
	}
	skb_reserve(skb, hl);
	ch = (cisco_hdr *)skb_put(skb, sizeof(cisco_hdr));
	ch->addr = CISCO_ADDR_UNICAST;
	ch->ctrl = 0;
	ch->type = htons(CISCO_TYPE_SLARP);
	s = (cisco_slarp *)skb_put(skb, sizeof(cisco_slarp));
	if (is_reply) {
		s->code = htonl(CISCO_SLARP_REPLY);
 		memset(&s->slarp.reply.ifaddr, 0, sizeof(__u32));
		memset(&s->slarp.reply.netmask, 0, sizeof(__u32));
	} else {
		lp->cisco_myseq++;
		s->code = htonl(CISCO_SLARP_KEEPALIVE);
		s->slarp.keepalive.my_seq = htonl(lp->cisco_myseq);
		s->slarp.keepalive.your_seq = htonl(lp->cisco_yourseq);
	}
	s->rel = 0xffff;
	s->t1 = t >> 16;
	s->t0 = t & 0xffff;
	len = skb->len;
	if (isdn_writebuf_skb_stub(lp->isdn_device, lp->isdn_channel, 0, skb) != len)
		dev_kfree_skb(skb);
}

static void
isdn_net_slarp_in(isdn_net_local *lp, struct sk_buff *skb)
{
	cisco_slarp *s = (cisco_slarp *)skb->data;

	switch (ntohl(s->code)) {
		case CISCO_SLARP_REQUEST:
			isdn_net_slarp_send(lp, 1);
			break;
		case CISCO_SLARP_REPLY:
			/* Ignore replies */
			break;
		case CISCO_SLARP_KEEPALIVE:
			lp->cisco_yourseq = s->slarp.keepalive.my_seq;
			if (ntohl(s->slarp.keepalive.my_seq == lp->cisco_myseq)) {
				if (lp->cisco_loop++ == 2) {
					printk(KERN_WARNING "%s: Keepalive Loop\n",
					       lp->name);
					lp->cisco_myseq ^= jiffies;
				}
			} else
				lp->cisco_loop = 0;
			break;
	}
	kfree_skb(skb);
}

/*
 * Called every 10 sec. via timer-interrupt if
 * any network-interface has Cisco-Keepalive-Encapsulation
 * and is online.
 * Send Keepalive-Packet and re-schedule.
 */
void
isdn_net_slarp_out(void)
{
	isdn_net_dev *p = dev->netdev;
	int anymore = 0;

	while (p) {
		isdn_net_local *l = p->local;
		if ((l->p_encap == ISDN_NET_ENCAP_CISCOHDLCK) &&
		    (l->flags & ISDN_NET_CONNECTED) &&
		    (!l->dialstate)                           ) {
			anymore = 1;
			isdn_net_slarp_send(l, 0);
		}
		p = (isdn_net_dev *) p->next;
	}
	isdn_timer_ctrl(ISDN_TIMER_KEEPALIVE, anymore);
}

/*
 * Got a packet from ISDN-Channel.
 */
static void
isdn_net_receive(struct device *ndev, struct sk_buff *skb)
{
	isdn_net_local *lp = (isdn_net_local *) ndev->priv;
	isdn_net_local *olp = lp;	/* original 'lp' */
#ifdef CONFIG_ISDN_PPP
	int proto = PPP_PROTOCOL(skb->data);
#endif
#ifdef CONFIG_ISDN_X25
	struct concap_proto *cprot = lp -> netdev -> cprot;
#endif
	cisco_hdr *ch;

	lp->transcount += skb->len;

	lp->stats.rx_packets++;
	lp->stats.rx_bytes += skb->len;
	if (lp->master) {
		/* Bundling: If device is a slave-device, deliver to master, also
		 * handle master's statistics and hangup-timeout
		 */
		ndev = lp->master;
		lp = (isdn_net_local *) ndev->priv;
		lp->stats.rx_packets++;
		lp->stats.rx_bytes += skb->len;
	}

	skb->dev = ndev;
	skb->pkt_type = PACKET_HOST;
	skb->mac.raw = skb->data;
#ifdef ISDN_DEBUG_NET_DUMP
	isdn_dumppkt("R:", skb->data, skb->len, 40);
#endif
	switch (lp->p_encap) {
		case ISDN_NET_ENCAP_ETHER:
			/* Ethernet over ISDN */
			olp->huptimer = 0;
			lp->huptimer = 0;
			skb->protocol = isdn_net_type_trans(skb, ndev);
			break;
		case ISDN_NET_ENCAP_UIHDLC:
			/* HDLC with UI-frame (for ispa with -h1 option) */
			olp->huptimer = 0;
			lp->huptimer = 0;
			skb_pull(skb, 2);
			/* Fall through */
		case ISDN_NET_ENCAP_RAWIP:
			/* RAW-IP without MAC-Header */
			olp->huptimer = 0;
			lp->huptimer = 0;
			skb->protocol = htons(ETH_P_IP);
			break;
		case ISDN_NET_ENCAP_CISCOHDLCK:
			ch = (cisco_hdr *)skb->data;
			if ((ch->addr != CISCO_ADDR_UNICAST) &&
			    (ch->addr != CISCO_ADDR_BROADCAST)  ) {
				printk(KERN_WARNING "%s: Unknown Cisco addr 0x%02x\n",
				       lp->name, ch->addr);
				kfree_skb(skb);
				return;
			}
			if (ch->ctrl != 0) {
				printk(KERN_WARNING "%s: Unknown Cisco ctrl 0x%02x\n",
				       lp->name, ch->ctrl);
				kfree_skb(skb);
				return;
			}
			switch (ntohs(ch->type)) {
				case CISCO_TYPE_INET:
					skb_pull(skb, 4);
					skb->protocol = htons(ETH_P_IP);
					break;
				case CISCO_TYPE_SLARP:
					skb_pull(skb, 4);
					isdn_net_slarp_in(olp, skb);
					return;
				default:
					printk(KERN_WARNING "%s: Unknown Cisco type 0x%04x\n",
					       lp->name, ch->type);
					kfree_skb(skb);
					return;
			}
			break;
		case ISDN_NET_ENCAP_CISCOHDLC:
			/* CISCO-HDLC IP with type field and  fake I-frame-header */
			skb_pull(skb, 2);
			/* Fall through */
		case ISDN_NET_ENCAP_IPTYP:
			/* IP with type field */
			olp->huptimer = 0;
			lp->huptimer = 0;
			skb->protocol = *(unsigned short *) &(skb->data[0]);
			skb_pull(skb, 2);
			if (*(unsigned short *) skb->data == 0xFFFF)
				skb->protocol = htons(ETH_P_802_3);
			break;
#ifdef CONFIG_ISDN_PPP
		case ISDN_NET_ENCAP_SYNCPPP:
			/*
			 * If encapsulation is syncppp, don't reset
			 * huptimer on LCP packets.
			 */
			if (proto != PPP_LCP) {
				olp->huptimer = 0;
				lp->huptimer = 0;
			}
			isdn_ppp_receive(lp->netdev, olp, skb);
			return;
#endif
		default:
#ifdef CONFIG_ISDN_X25
		  /* try if there are generic sync_device receiver routines */
			if(cprot) if(cprot -> pops) 
				if( cprot -> pops -> data_ind){
					cprot -> pops -> data_ind(cprot,skb);
					return;
				};
#endif /* CONFIG_ISDN_X25 */
			printk(KERN_WARNING "%s: unknown encapsulation, dropping\n",
			       lp->name);
			kfree_skb(skb);
			return;
	}
	netif_rx(skb);
	return;
}

/*
 * A packet arrived via ISDN. Search interface-chain for a corresponding
 * interface. If found, deliver packet to receiver-function and return 1,
 * else return 0.
 */
int
isdn_net_rcv_skb(int idx, struct sk_buff *skb)
{
	isdn_net_dev *p = dev->rx_netdev[idx];

	if (p) {
		isdn_net_local *lp = p->local;
		if ((lp->flags & ISDN_NET_CONNECTED) &&
		    (!lp->dialstate)) {
			isdn_net_receive(&p->dev, skb);
			return 1;
		}
	}
	return 0;
}

static int
my_eth_header(struct sk_buff *skb, struct device *dev, unsigned short type,
	      void *daddr, void *saddr, unsigned len)
{
	struct ethhdr *eth = (struct ethhdr *) skb_push(skb, ETH_HLEN);

	/*
	 * Set the protocol type. For a packet of type ETH_P_802_3 we
	 * put the length here instead. It is up to the 802.2 layer to
	 * carry protocol information.
	 */

	if (type != ETH_P_802_3)
		eth->h_proto = htons(type);
	else
		eth->h_proto = htons(len);

	/*
	 * Set the source hardware address.
	 */
	if (saddr)
		memcpy(eth->h_source, saddr, dev->addr_len);
	else
		memcpy(eth->h_source, dev->dev_addr, dev->addr_len);

	/*
	 * Anyway, the loopback-device should never use this function...
	 */

	if (dev->flags & (IFF_LOOPBACK | IFF_NOARP)) {
		memset(eth->h_dest, 0, dev->addr_len);
		return ETH_HLEN /*(dev->hard_header_len)*/;
	}
	if (daddr) {
		memcpy(eth->h_dest, daddr, dev->addr_len);
		return ETH_HLEN /*dev->hard_header_len*/;
	}
	return -ETH_HLEN /*dev->hard_header_len*/;
}

/*
 *  build an header
 *  depends on encaps that is being used.
 */

static int
isdn_net_header(struct sk_buff *skb, struct device *dev, unsigned short type,
		void *daddr, void *saddr, unsigned plen)
{
	isdn_net_local *lp = dev->priv;
	ushort len = 0;

	switch (lp->p_encap) {
		case ISDN_NET_ENCAP_ETHER:
			len = my_eth_header(skb, dev, type, daddr, saddr, plen);
			break;
#ifdef CONFIG_ISDN_PPP
		case ISDN_NET_ENCAP_SYNCPPP:
			/* stick on a fake header to keep fragmentation code happy. */
			len = IPPP_MAX_HEADER;
			skb_push(skb,len);
			break;
#endif
		case ISDN_NET_ENCAP_RAWIP:
			printk(KERN_WARNING "isdn_net_header called with RAW_IP!\n");
			len = 0;
			break;
		case ISDN_NET_ENCAP_IPTYP:
			/* ethernet type field */
			*((ushort *) skb_push(skb, 2)) = htons(type);
			len = 2;
			break;
		case ISDN_NET_ENCAP_UIHDLC:
			/* HDLC with UI-Frames (for ispa with -h1 option) */
			*((ushort *) skb_push(skb, 2)) = htons(0x0103);
			len = 2;
			break;
		case ISDN_NET_ENCAP_CISCOHDLC:
			skb_push(skb, 4);
			skb->data[0] = 0x0f;
			skb->data[1] = 0x00;
			*((ushort *) & skb->data[2]) = htons(type);
			len = 4;
			break;
#ifdef CONFIG_ISDN_X25
		default:
		  /* try if there are generic concap protocol routines */
			if( lp-> netdev -> cprot ){
				printk(KERN_WARNING "isdn_net_header called with concap_proto!\n");
				len = 0;
				break;
			}
			break;
#endif /* CONFIG_ISDN_X25 */
	}
	return len;
}

/* We don't need to send arp, because we have point-to-point connections. */
static int
isdn_net_rebuild_header(struct sk_buff *skb)
{
	struct device *dev = skb->dev;
	isdn_net_local *lp = dev->priv;
	int ret = 0;

	if (lp->p_encap == ISDN_NET_ENCAP_ETHER) {
		struct ethhdr *eth = (struct ethhdr *) skb->data;

		/*
		 *      Only ARP/IP is currently supported
		 */

		if (eth->h_proto != htons(ETH_P_IP)) {
			printk(KERN_WARNING
			       "isdn_net: %s don't know how to resolve type %d addresses?\n",
			       dev->name, (int) eth->h_proto);
			memcpy(eth->h_source, dev->dev_addr, dev->addr_len);
			return 0;
		}
		/*
		 *      Try to get ARP to resolve the header.
		 */
#ifdef CONFIG_INET
		ret = arp_find(eth->h_dest, skb);
#endif
	}
	return ret;
}

/*
 * Interface-setup. (called just after registering a new interface)
 */
static int
isdn_net_init(struct device *ndev)
{
	ushort max_hlhdr_len = 0;
	isdn_net_local *lp = (isdn_net_local *) ndev->priv;
	int drvidx,
	 i;

	if (ndev == NULL) {
		printk(KERN_WARNING "isdn_net_init: dev = NULL!\n");
		return -ENODEV;
	}
	if (ndev->priv == NULL) {
		printk(KERN_WARNING "isdn_net_init: dev->priv = NULL!\n");
		return -ENODEV;
	}
	ether_setup(ndev);
	lp->org_hhc = ndev->hard_header_cache;
	lp->org_hcu = ndev->header_cache_update;

	/* Setup the generic properties */

	ndev->hard_header = NULL;
	ndev->hard_header_cache = NULL;
	ndev->header_cache_update = NULL;
	ndev->mtu = 1500;
	ndev->flags = IFF_NOARP|IFF_POINTOPOINT;
	ndev->type = ARPHRD_ETHER;
	ndev->addr_len = ETH_ALEN;

	ndev->tx_queue_len = 10; /* for clients without MPPP 5 is better.  */

	for (i = 0; i < ETH_ALEN; i++)
		ndev->broadcast[i] = 0xff;

#ifdef DEV_NUMBUFFS
	for (i = 0; i < DEV_NUMBUFFS; i++)
		skb_queue_head_init(&ndev->buffs[i]);
#endif

	/* The ISDN-specific entries in the device structure. */
	ndev->open = &isdn_net_open;
	ndev->hard_start_xmit = &isdn_net_start_xmit;

	/*
	 *  up till binding we ask the protocol layer to reserve as much
	 *  as we might need for HL layer
	 */

	for (drvidx = 0; drvidx < ISDN_MAX_DRIVERS; drvidx++)
		if (dev->drv[drvidx])
			if (max_hlhdr_len < dev->drv[drvidx]->interface->hl_hdrlen)
				max_hlhdr_len = dev->drv[drvidx]->interface->hl_hdrlen;

	ndev->hard_header_len = ETH_HLEN + max_hlhdr_len;

	ndev->stop = &isdn_net_close;
	ndev->get_stats = &isdn_net_get_stats;
	ndev->rebuild_header = &isdn_net_rebuild_header;

#ifdef CONFIG_ISDN_PPP
	ndev->do_ioctl = isdn_ppp_dev_ioctl;
#endif
	return 0;
}

/*
 * I picked the pattern-matching-functions from an old GNU-tar version (1.10)
 * It was originally written and put to PD by rs@mirror.TMC.COM (Rich Salz)
 */

static int
isdn_net_Star(char *s, char *p)
{
	while (isdn_net_wildmat(s, p) == 0)
		if (*++s == '\0')
			return (0);
	return (1);
}

/*
 * Shell-type Pattern-matching for incoming caller-Ids
 * This function gets a string in s and checks, if it matches the pattern
 * given in p. It returns 1 on success, 0 otherwise.
 *
 * Possible Patterns:
 *
 * '?'     matches one character
 * '*'     matches zero or more characters
 * [xyz]   matches the set of characters in brackets.
 * [^xyz]  matches any single character not in the set of characters
 */

static int
isdn_net_wildmat(char *s, char *p)
{
	register int last;
	register int matched;
	register int reverse;

	for (; *p; s++, p++)
		switch (*p) {
			case '\\':
				/*
				 * Literal match with following character,
				 * fall through.
				 */
				p++;
			default:
				if (*s != *p)
					return (0);
				continue;
			case '?':
				/* Match anything. */
				if (*s == '\0')
					return (0);
				continue;
			case '*':
				/* Trailing star matches everything. */
				return (*++p ? isdn_net_Star(s, p) : 1);
			case '[':
				/* [^....] means inverse character class. */
				if ((reverse = (p[1] == '^')))
					p++;
				for (last = 0, matched = 0; *++p && (*p != ']'); last = *p)
					/* This next line requires a good C compiler. */
					if (*p == '-' ? *s <= *++p && *s >= last : *s == *p)
						matched = 1;
				if (matched == reverse)
					return (0);
				continue;
		}
	return (*s == '\0');
}

static void
isdn_net_swapbind(int drvidx)
{
	isdn_net_dev *p;

#ifdef ISDN_DEBUG_NET_ICALL
	printk(KERN_DEBUG "n_fi: swapping ch of %d\n", drvidx);
#endif
	p = dev->netdev;
	while (p) {
		if (p->local->pre_device == drvidx)
			switch (p->local->pre_channel) {
				case 0:
					p->local->pre_channel = 1;
					break;
				case 1:
					p->local->pre_channel = 0;
					break;
			}
		p = (isdn_net_dev *) p->next;
	}
}

static void
isdn_net_swap_usage(int i1, int i2)
{
	int u1 = dev->usage[i1] & ISDN_USAGE_EXCLUSIVE;
	int u2 = dev->usage[i2] & ISDN_USAGE_EXCLUSIVE;

#ifdef ISDN_DEBUG_NET_ICALL
	printk(KERN_DEBUG "n_fi: usage of %d and %d\n", i1, i2);
#endif
	dev->usage[i1] &= ~ISDN_USAGE_EXCLUSIVE;
	dev->usage[i1] |= u2;
	dev->usage[i2] &= ~ISDN_USAGE_EXCLUSIVE;
	dev->usage[i2] |= u1;
	isdn_info_update();
}

/*
 * An incoming call-request has arrived.
 * Search the interface-chain for an appropriate interface.
 * If found, connect the interface to the ISDN-channel and initiate
 * D- and B-Channel-setup. If secure-flag is set, accept only
 * configured phone-numbers. If callback-flag is set, initiate
 * callback-dialing.
 *
 * Return-Value: 0 = No appropriate interface for this call.
 *               1 = Call accepted
 *               2 = Reject call, wait cbdelay, then call back
 *               3 = Reject call
 *               4 = Wait cbdelay, then call back
 */
int
isdn_net_find_icall(int di, int ch, int idx, setup_parm setup)
{
	char *eaz;
	int si1;
	int si2;
	int ematch;
	int swapped;
	int sidx = 0;
	isdn_net_dev *p;
	isdn_net_phone *n;
	ulong flags;
	char nr[32];

	/* Search name in netdev-chain */
	save_flags(flags);
	cli();
	if (!setup.phone[0]) {
		nr[0] = '0';
		nr[1] = '\0';
		printk(KERN_INFO "isdn_net: Incoming call without OAD, assuming '0'\n");
	} else
		strcpy(nr, setup.phone);
	si1 = (int) setup.si1;
	si2 = (int) setup.si2;
	if (!setup.eazmsn[0]) {
		printk(KERN_WARNING "isdn_net: Incoming call without CPN, assuming '0'\n");
		eaz = "0";
	} else
		eaz = setup.eazmsn;
	if (dev->net_verbose > 1)
		printk(KERN_INFO "isdn_net: call from %s,%d,%d -> %s\n", nr, si1, si2, eaz);
	/* Accept only calls with Si1 = 7 (Data-Transmission) */
	if (si1 != 7) {
		restore_flags(flags);
		if (dev->net_verbose > 1)
			printk(KERN_INFO "isdn_net: Service-Indicator not 7, ignored\n");
		return 0;
	}
	n = (isdn_net_phone *) 0;
	p = dev->netdev;
	ematch = 0;
#ifdef ISDN_DEBUG_NET_ICALL
	printk(KERN_DEBUG "n_fi: di=%d ch=%d idx=%d usg=%d\n", di, ch, idx,
	       dev->usage[idx]);
#endif
	swapped = 0;
	while (p) {
		isdn_net_local *lp = p->local;

		/* If last check has triggered as binding-swap, revert it */
		switch (swapped) {
			case 2:
				isdn_net_swap_usage(idx, sidx);
				/* fall through */
			case 1:
				isdn_net_swapbind(di);
				break;
		}
		swapped = 0;
		if (!strcmp(isdn_map_eaz2msn(lp->msn, di), eaz))
			ematch = 1;
#ifdef ISDN_DEBUG_NET_ICALL
		printk(KERN_DEBUG "n_fi: if='%s', l.msn=%s, l.flags=%d, l.dstate=%d\n",
		       lp->name, lp->msn, lp->flags, lp->dialstate);
#endif
		if ((!strcmp(isdn_map_eaz2msn(lp->msn, di), eaz)) &&	/* EAZ is matching   */
		    (((!(lp->flags & ISDN_NET_CONNECTED)) &&	/* but not connected */
		      (USG_NONE(dev->usage[idx]))) ||	/* and ch. unused or */
		     ((((lp->dialstate == 4) || (lp->dialstate == 12)) &&	/* if dialing        */
		       (!(lp->flags & ISDN_NET_CALLBACK)))	/* but no callback   */
		     ))) {
#ifdef ISDN_DEBUG_NET_ICALL
			printk(KERN_DEBUG "n_fi: match1, pdev=%d pch=%d\n",
			       lp->pre_device, lp->pre_channel);
#endif
			if (dev->usage[idx] & ISDN_USAGE_EXCLUSIVE) {
				if ((lp->pre_channel != ch) ||
				    (lp->pre_device != di)) {
					/* Here we got a problem:
					 * If using an ICN-Card, an incoming call is always signaled on
					 * on the first channel of the card, if both channels are
					 * down. However this channel may be bound exclusive. If the
					 * second channel is free, this call should be accepted.
					 * The solution is horribly but it runs, so what:
					 * We exchange the exclusive bindings of the two channels, the
					 * corresponding variables in the interface-structs.
					 */
					if (ch == 0) {
						sidx = isdn_dc2minor(di, 1);
#ifdef ISDN_DEBUG_NET_ICALL
						printk(KERN_DEBUG "n_fi: ch is 0\n");
#endif
						if (USG_NONE(dev->usage[sidx])) {
							/* Second Channel is free, now see if it is bound
							 * exclusive too. */
							if (dev->usage[sidx] & ISDN_USAGE_EXCLUSIVE) {
#ifdef ISDN_DEBUG_NET_ICALL
								printk(KERN_DEBUG "n_fi: 2nd channel is down and bound\n");
#endif
								/* Yes, swap bindings only, if the original
								 * binding is bound to channel 1 of this driver */
								if ((lp->pre_device == di) &&
								    (lp->pre_channel == 1)) {
									isdn_net_swapbind(di);
									swapped = 1;
								} else {
									/* ... else iterate next device */
									p = (isdn_net_dev *) p->next;
									continue;
								}
							} else {
#ifdef ISDN_DEBUG_NET_ICALL
								printk(KERN_DEBUG "n_fi: 2nd channel is down and unbound\n");
#endif
								/* No, swap always and swap excl-usage also */
								isdn_net_swap_usage(idx, sidx);
								isdn_net_swapbind(di);
								swapped = 2;
							}
							/* Now check for exclusive binding again */
#ifdef ISDN_DEBUG_NET_ICALL
							printk(KERN_DEBUG "n_fi: final check\n");
#endif
							if ((dev->usage[idx] & ISDN_USAGE_EXCLUSIVE) &&
							    ((lp->pre_channel != ch) ||
							     (lp->pre_device != di))) {
#ifdef ISDN_DEBUG_NET_ICALL
								printk(KERN_DEBUG "n_fi: final check failed\n");
#endif
								p = (isdn_net_dev *) p->next;
								continue;
							}
						}
					} else {
						/* We are already on the second channel, so nothing to do */
#ifdef ISDN_DEBUG_NET_ICALL
						printk(KERN_DEBUG "n_fi: already on 2nd channel\n");
#endif
						p = (isdn_net_dev *) p->next;
						continue;
					}
				}
			}
#ifdef ISDN_DEBUG_NET_ICALL
			printk(KERN_DEBUG "n_fi: match2\n");
#endif
			n = lp->phone[0];
			if (lp->flags & ISDN_NET_SECURE) {
				while (n) {
					if (isdn_net_wildmat(nr, n->num))
						break;
					n = (isdn_net_phone *) n->next;
				}
			}
			if (n || (!(lp->flags & ISDN_NET_SECURE))) {
#ifdef ISDN_DEBUG_NET_ICALL
				printk(KERN_DEBUG "n_fi: match3\n");
#endif
				/* Here we got an interface matched, now see if it is up.
				 * If not, reject the call actively.
				 */
				if (!p->dev.start) {
					restore_flags(flags);
					printk(KERN_INFO "%s: incoming call, interface down -> rejected\n",
					       lp->name);
					return 3;
				}
				/* Interface is up, now see if it's a slave. If so, see if
				 * it's master and parent slave is online. If not, reject the call.
				 */
				if (lp->master) {
					isdn_net_local *mlp = (isdn_net_local *) lp->master->priv;
					printk(KERN_DEBUG "ICALLslv: %s\n", lp->name);
					printk(KERN_DEBUG "master=%s\n", mlp->name);
					if (mlp->flags & ISDN_NET_CONNECTED) {
						printk(KERN_DEBUG "master online\n");
						/* Master is online, find parent-slave (master if first slave) */
						while (mlp->slave) {
							if ((isdn_net_local *) mlp->slave->priv == lp)
								break;
							mlp = (isdn_net_local *) mlp->slave->priv;
						}
					} else
						printk(KERN_DEBUG "master offline\n");
					/* Found parent, if it's offline iterate next device */
					printk(KERN_DEBUG "mlpf: %d\n", mlp->flags & ISDN_NET_CONNECTED);
					if (!(mlp->flags & ISDN_NET_CONNECTED)) {
						p = (isdn_net_dev *) p->next;
						continue;
					}
				}
				if (lp->flags & ISDN_NET_CALLBACK) {
					int chi;
					printk(KERN_DEBUG "%s: call from %s -> %s, start callback\n",
					       lp->name, nr, eaz);
					if (lp->phone[1]) {
						/* Grab a free ISDN-Channel */
						if ((chi = isdn_get_free_channel(ISDN_USAGE_NET, lp->l2_proto,
							    lp->l3_proto,
							  lp->pre_device,
						 lp->pre_channel)) < 0) {
							printk(KERN_WARNING "isdn_net_find_icall: No channel for %s\n", lp->name);
							restore_flags(flags);
							return 0;
						}
						/* Setup dialstate. */
						lp->dtimer = 0;
						lp->dialstate = 11;
						lp->flags |= ISDN_NET_CONNECTED;
						/* Connect interface with channel */
						isdn_net_bind_channel(lp, chi);
#ifdef CONFIG_ISDN_PPP
						if (lp->p_encap == ISDN_NET_ENCAP_SYNCPPP)
							if (isdn_ppp_bind(lp) < 0) {
								isdn_net_unbind_channel(lp);
								restore_flags(flags);
								return 0;
							}
#endif
						/* Initiate dialing by returning 2 or 4 */
						restore_flags(flags);
						return (lp->flags & ISDN_NET_CBHUP) ? 2 : 4;
					} else
						printk(KERN_WARNING "isdn_net: %s: No phone number\n", lp->name);
					restore_flags(flags);
					return 0;
				} else {
					printk(KERN_DEBUG "%s: call from %s -> %s accepted\n", lp->name, nr,
					       eaz);
					/* if this interface is dialing, it does it probably on a different
					   device, so free this device */
					if ((lp->dialstate == 4) || (lp->dialstate == 12)) {
#ifdef CONFIG_ISDN_PPP
						if (lp->p_encap == ISDN_NET_ENCAP_SYNCPPP)
							isdn_ppp_free(lp);
#endif
						isdn_free_channel(lp->isdn_device, lp->isdn_channel,
							 ISDN_USAGE_NET);
					}
					dev->usage[idx] &= ISDN_USAGE_EXCLUSIVE;
					dev->usage[idx] |= ISDN_USAGE_NET;
					strcpy(dev->num[idx], nr);
					isdn_info_update();
					dev->st_netdev[idx] = lp->netdev;
					lp->isdn_device = di;
					lp->isdn_channel = ch;
					lp->ppp_slot = -1;
					lp->flags |= ISDN_NET_CONNECTED;
					lp->dialstate = 7;
					lp->dtimer = 0;
					lp->outgoing = 0;
					lp->huptimer = 0;
					lp->hupflags |= ISDN_WAITCHARGE;
					lp->hupflags &= ~ISDN_HAVECHARGE;
#ifdef CONFIG_ISDN_PPP
					if (lp->p_encap == ISDN_NET_ENCAP_SYNCPPP)
						if (isdn_ppp_bind(lp) < 0) {
							isdn_net_unbind_channel(lp);
							restore_flags(flags);
							return 0;
						}
#endif
					restore_flags(flags);
					return 1;
				}
			}
		}
		p = (isdn_net_dev *) p->next;
	}
	/* If none of configured EAZ/MSN matched and not verbose, be silent */
	if (ematch || dev->net_verbose)
		printk(KERN_INFO "isdn_net: call from %s -> %d %s ignored\n", nr, di, eaz);
	restore_flags(flags);
	return 0;
}

/*
 * Search list of net-interfaces for an interface with given name.
 */
isdn_net_dev *
isdn_net_findif(char *name)
{
	isdn_net_dev *p = dev->netdev;

	while (p) {
		if (!strcmp(p->local->name, name))
			return p;
		p = (isdn_net_dev *) p->next;
	}
	return (isdn_net_dev *) NULL;
}

/*
 * Force a net-interface to dial out.
 * This is called from the userlevel-routine below or
 * from isdn_net_start_xmit().
 */
int
isdn_net_force_dial_lp(isdn_net_local * lp)
{
	if ((!(lp->flags & ISDN_NET_CONNECTED)) && !lp->dialstate) {
		int chi;
		if (lp->phone[1]) {
			ulong flags;
			save_flags(flags);
			cli();
			/* Grab a free ISDN-Channel */
			if ((chi = isdn_get_free_channel(ISDN_USAGE_NET, lp->l2_proto,
							 lp->l3_proto,
							 lp->pre_device,
						 lp->pre_channel)) < 0) {
				printk(KERN_WARNING "isdn_net_force_dial: No channel for %s\n", lp->name);
				restore_flags(flags);
				return -EAGAIN;
			}
			lp->dialstate = 1;
			lp->flags |= ISDN_NET_CONNECTED;
			/* Connect interface with channel */
			isdn_net_bind_channel(lp, chi);
#ifdef CONFIG_ISDN_PPP
			if (lp->p_encap == ISDN_NET_ENCAP_SYNCPPP)
				if (isdn_ppp_bind(lp) < 0) {
					isdn_net_unbind_channel(lp);
					restore_flags(flags);
					return -EAGAIN;
				}
#endif
			/* Initiate dialing */
			restore_flags(flags);
			isdn_net_dial();
			return 0;
		} else
			return -EINVAL;
	} else
		return -EBUSY;
}

/*
 * Force a net-interface to dial out.
 * This is always called from within userspace (ISDN_IOCTL_NET_DIAL).
 */
int
isdn_net_force_dial(char *name)
{
	isdn_net_dev *p = isdn_net_findif(name);

	if (!p)
		return -ENODEV;
	return (isdn_net_force_dial_lp(p->local));
}

/*
 * Allocate a new network-interface and initialize its data structures.
 */
char *
isdn_net_new(char *name, struct device *master)
{
	isdn_net_dev *netdev;

	/* Avoid creating an existing interface */
	if (isdn_net_findif(name)) {
		printk(KERN_WARNING "isdn_net: interface %s already exists\n", name);
		return NULL;
	}
	if (!(netdev = (isdn_net_dev *) kmalloc(sizeof(isdn_net_dev), GFP_KERNEL))) {
		printk(KERN_WARNING "isdn_net: Could not allocate net-device\n");
		return NULL;
	}
	memset(netdev, 0, sizeof(isdn_net_dev));
	if (!(netdev->local = (isdn_net_local *) kmalloc(sizeof(isdn_net_local), GFP_KERNEL))) {
		printk(KERN_WARNING "isdn_net: Could not allocate device locals\n");
		return NULL;
	}
	memset(netdev->local, 0, sizeof(isdn_net_local));
	if (name == NULL)
		strcpy(netdev->local->name, "         ");
	else
		strcpy(netdev->local->name, name);
	netdev->dev.name = netdev->local->name;
	netdev->dev.priv = netdev->local;
	netdev->dev.init = isdn_net_init;
	netdev->local->p_encap = ISDN_NET_ENCAP_RAWIP;
	if (master) {
		/* Device shall be a slave */
		struct device *p = (((isdn_net_local *) master->priv)->slave);
		struct device *q = master;

		netdev->local->master = master;
		/* Put device at end of slave-chain */
		while (p) {
			q = p;
			p = (((isdn_net_local *) p->priv)->slave);
		}
		((isdn_net_local *) q->priv)->slave = &(netdev->dev);
		q->interrupt = 0;
		q->tbusy = 0;
		q->start = master->start;
	} else {
		/* Device shall be a master */
		if (register_netdev(&netdev->dev) != 0) {
			printk(KERN_WARNING "isdn_net: Could not register net-device\n");
			kfree(netdev->local);
			kfree(netdev);
			return NULL;
		}
	}
	netdev->local->magic = ISDN_NET_MAGIC;

#ifdef CONFIG_ISDN_PPP
	netdev->mp_last = NULL; /* mpqueue is empty */
	netdev->ib.next_num = 0;
	netdev->ib.last = NULL;
#endif
	netdev->queue = netdev->local;
	netdev->local->last = netdev->local;
	netdev->local->netdev = netdev;
	netdev->local->next = netdev->local;

	netdev->local->isdn_device = -1;
	netdev->local->isdn_channel = -1;
	netdev->local->pre_device = -1;
	netdev->local->pre_channel = -1;
	netdev->local->exclusive = -1;
	netdev->local->ppp_slot = -1;
	netdev->local->pppbind = -1;
	netdev->local->sav_skb = NULL;
	netdev->local->first_skb = NULL;
	netdev->local->l2_proto = ISDN_PROTO_L2_X75I;
	netdev->local->l3_proto = ISDN_PROTO_L3_TRANS;
	netdev->local->triggercps = 6000;
	netdev->local->slavedelay = 10 * HZ;
	netdev->local->srobin = &netdev->dev;
	netdev->local->hupflags = ISDN_INHUP;	/* Do hangup even on incoming calls */
	netdev->local->onhtime = 10;	/* Default hangup-time for saving costs
	   of those who forget configuring this */
	netdev->local->dialmax = 1;
	netdev->local->flags = ISDN_NET_CBHUP;	/* Hangup before Callback */
	netdev->local->cbdelay = 25;	/* Wait 5 secs before Callback */
	/* Put into to netdev-chain */
	netdev->next = (void *) dev->netdev;
	dev->netdev = netdev;
	return netdev->dev.name;
}

char *
isdn_net_newslave(char *parm)
{
	char *p = strchr(parm, ',');
	isdn_net_dev *n;
	char newname[10];

	if (p) {
		/* Slave-Name MUST not be empty */
		if (!strlen(p + 1))
			return NULL;
		strcpy(newname, p + 1);
		*p = 0;
		/* Master must already exist */
		if (!(n = isdn_net_findif(parm)))
			return NULL;
		/* Master must be a real interface, not a slave */
		if (n->local->master)
			return NULL;
		/* Master must not be started yet */
		if (n->dev.start)
			return NULL;
		return (isdn_net_new(newname, &(n->dev)));
	}
	return NULL;
}

/*
 * Set interface-parameters.
 * Always set all parameters, so the user-level application is responsible
 * for not overwriting existing setups. It has to get the current
 * setup first, if only selected parameters are to be changed.
 */
int
isdn_net_setcfg(isdn_net_ioctl_cfg * cfg)
{
	isdn_net_dev *p = isdn_net_findif(cfg->name);
	ulong features;
	int i;
	int drvidx;
	int chidx;
	char drvid[25];
#ifdef CONFIG_ISDN_X25
	ulong flags;
#endif
	if (p) {
		isdn_net_local *lp = p->local;

		/* See if any registered driver supports the features we want */
		features = ((1 << cfg->l2_proto) << ISDN_FEATURE_L2_SHIFT) |
			((1 << cfg->l3_proto) << ISDN_FEATURE_L3_SHIFT);
		for (i = 0; i < ISDN_MAX_DRIVERS; i++)
			if (dev->drv[i])
				if ((dev->drv[i]->interface->features & features) == features)
					break;
		if (i == ISDN_MAX_DRIVERS) {
			printk(KERN_WARNING "isdn_net: No driver with selected features\n");
			return -ENODEV;
		}
		if (lp->p_encap != cfg->p_encap){
#ifdef CONFIG_ISDN_X25
			struct concap_proto * cprot = p -> cprot;
#endif
			if (p->dev.start) {
				printk(KERN_WARNING
				"%s: cannot change encap when if is up\n",
				       lp->name);
				return -EBUSY;
			}
#ifdef CONFIG_ISDN_X25
			/* delete old encapsulation protocol if present ... */
			save_flags(flags);
			cli(); /* avoid races with incoming events trying to
				  call cprot->pops methods */
			if( cprot && cprot -> pops ) 
				cprot -> pops -> proto_del ( cprot );
			p -> cprot = NULL;
			lp -> dops = NULL;
			restore_flags(flags);
			/* ... ,  prepare for configuration of new one ... */
			switch ( cfg -> p_encap ){
			case ISDN_NET_ENCAP_X25IFACE:
				lp -> dops = &isdn_concap_reliable_dl_dops;
			}
			/* ... and allocate new one ... */
			p -> cprot = isdn_concap_new( cfg -> p_encap );
			/* p -> cprot == NULL now if p_encap is not supported
			   by means of the concap_proto mechanism */
			/* the protocol is not configured yet; this will 
			   happen later when isdn_net_reset() is called */
#endif
		}
		switch ( cfg->p_encap ) {
		case ISDN_NET_ENCAP_SYNCPPP:
#ifndef CONFIG_ISDN_PPP
			printk(KERN_WARNING "%s: SyncPPP support not configured\n",
			       lp->name);
			return -EINVAL;
#else
			p->dev.type = ARPHRD_PPP;	/* change ARP type */
			p->dev.addr_len = 0;
#endif
			break;
		case ISDN_NET_ENCAP_X25IFACE:
#ifndef CONFIG_ISDN_X25
			printk(KERN_WARNING "%s: isdn-x25 support not configured\n",
			       p->local->name);
			return -EINVAL;
#else
			p->dev.type = ARPHRD_X25;	/* change ARP type */
			p->dev.addr_len = 0;
#endif
			break;
		default:
			if( cfg->p_encap >= 0 &&
			    cfg->p_encap <= ISDN_NET_ENCAP_MAX_ENCAP )
				break;
			printk(KERN_WARNING 
			       "%s: encapsulation protocol %d not supported\n",
			       p->local->name, cfg->p_encap);
			return -EINVAL;
		}
		if (strlen(cfg->drvid)) {
			/* A bind has been requested ... */
			char *c,
			*e;

			drvidx = -1;
			chidx = -1;
			strcpy(drvid, cfg->drvid);
			if ((c = strchr(drvid, ','))) {
				/* The channel-number is appended to the driver-Id with a comma */
				chidx = (int) simple_strtoul(c + 1, &e, 10);
				if (e == c)
					chidx = -1;
				*c = '\0';
			}
			for (i = 0; i < ISDN_MAX_DRIVERS; i++)
				/* Lookup driver-Id in array */
				if (!(strcmp(dev->drvid[i], drvid))) {
					drvidx = i;
					break;
				}
			if ((drvidx == -1) || (chidx == -1))
				/* Either driver-Id or channel-number invalid */
				return -ENODEV;
		} else {
			/* Parameters are valid, so get them */
			drvidx = lp->pre_device;
			chidx = lp->pre_channel;
		}
		if (cfg->exclusive > 0) {
			int flags;

			/* If binding is exclusive, try to grab the channel */
			save_flags(flags);
			if ((i = isdn_get_free_channel(ISDN_USAGE_NET, lp->l2_proto,
						       lp->l3_proto,
						       drvidx,
						       chidx)) < 0) {
				/* Grab failed, because desired channel is in use */
				lp->exclusive = -1;
				restore_flags(flags);
				return -EBUSY;
			}
			/* All went ok, so update isdninfo */
			dev->usage[i] = ISDN_USAGE_EXCLUSIVE;
			isdn_info_update();
			restore_flags(flags);
			lp->exclusive = i;
		} else {
			/* Non-exclusive binding or unbind. */
			lp->exclusive = -1;
			if ((lp->pre_device != -1) && (cfg->exclusive == -1)) {
				isdn_unexclusive_channel(lp->pre_device, lp->pre_channel);
				isdn_free_channel(lp->pre_device, lp->pre_channel, ISDN_USAGE_NET);
				drvidx = -1;
				chidx = -1;
			}
		}
		strcpy(lp->msn, cfg->eaz);
		lp->pre_device = drvidx;
		lp->pre_channel = chidx;
		lp->onhtime = cfg->onhtime;
		lp->charge = cfg->charge;
		lp->l2_proto = cfg->l2_proto;
		lp->l3_proto = cfg->l3_proto;
		lp->cbdelay = cfg->cbdelay;
		lp->dialmax = cfg->dialmax;
		lp->triggercps = cfg->triggercps;
		lp->slavedelay = cfg->slavedelay * HZ;
		lp->pppbind = cfg->pppbind;
		if (cfg->secure)
			lp->flags |= ISDN_NET_SECURE;
		else
			lp->flags &= ~ISDN_NET_SECURE;
		if (cfg->cbhup)
			lp->flags |= ISDN_NET_CBHUP;
		else
			lp->flags &= ~ISDN_NET_CBHUP;
		switch (cfg->callback) {
			case 0:
				lp->flags &= ~(ISDN_NET_CALLBACK | ISDN_NET_CBOUT);
				break;
			case 1:
				lp->flags |= ISDN_NET_CALLBACK;
				lp->flags &= ~ISDN_NET_CBOUT;
				break;
			case 2:
				lp->flags |= ISDN_NET_CBOUT;
				lp->flags &= ~ISDN_NET_CALLBACK;
				break;
		}
		if (cfg->chargehup)
			lp->hupflags |= ISDN_CHARGEHUP;
		else
			lp->hupflags &= ~ISDN_CHARGEHUP;
		if (cfg->ihup)
			lp->hupflags |= ISDN_INHUP;
		else
			lp->hupflags &= ~ISDN_INHUP;
		if (cfg->chargeint > 10) {
			lp->hupflags |= ISDN_CHARGEHUP | ISDN_HAVECHARGE | ISDN_MANCHARGE;
			lp->chargeint = cfg->chargeint * HZ;
		}
		if (cfg->p_encap != lp->p_encap) {
			if (cfg->p_encap == ISDN_NET_ENCAP_RAWIP) {
				p->dev.hard_header = NULL;
				p->dev.hard_header_cache = NULL;
				p->dev.header_cache_update = NULL;
				p->dev.flags = IFF_NOARP|IFF_POINTOPOINT;
			} else {
				p->dev.hard_header = isdn_net_header;
				if (cfg->p_encap == ISDN_NET_ENCAP_ETHER) {
					p->dev.hard_header_cache = lp->org_hhc;
					p->dev.header_cache_update = lp->org_hcu;
					p->dev.flags = IFF_BROADCAST | IFF_MULTICAST;
				} else {
					p->dev.hard_header_cache = NULL;
					p->dev.header_cache_update = NULL;
					p->dev.flags = IFF_NOARP|IFF_POINTOPOINT;
				}
			}
		}
		lp->p_encap = cfg->p_encap;
		return 0;
	}
	return -ENODEV;
}

/*
 * Perform get-interface-parameters.ioctl
 */
int
isdn_net_getcfg(isdn_net_ioctl_cfg * cfg)
{
	isdn_net_dev *p = isdn_net_findif(cfg->name);

	if (p) {
		isdn_net_local *lp = p->local;

		strcpy(cfg->eaz, lp->msn);
		cfg->exclusive = lp->exclusive;
		if (lp->pre_device >= 0) {
			sprintf(cfg->drvid, "%s,%d", dev->drvid[lp->pre_device],
				lp->pre_channel);
		} else
			cfg->drvid[0] = '\0';
		cfg->onhtime = lp->onhtime;
		cfg->charge = lp->charge;
		cfg->l2_proto = lp->l2_proto;
		cfg->l3_proto = lp->l3_proto;
		cfg->p_encap = lp->p_encap;
		cfg->secure = (lp->flags & ISDN_NET_SECURE) ? 1 : 0;
		cfg->callback = 0;
		if (lp->flags & ISDN_NET_CALLBACK)
			cfg->callback = 1;
		if (lp->flags & ISDN_NET_CBOUT)
			cfg->callback = 2;
		cfg->cbhup = (lp->flags & ISDN_NET_CBHUP) ? 1 : 0;
		cfg->chargehup = (lp->hupflags & 4) ? 1 : 0;
		cfg->ihup = (lp->hupflags & 8) ? 1 : 0;
		cfg->cbdelay = lp->cbdelay;
		cfg->dialmax = lp->dialmax;
		cfg->triggercps = lp->triggercps;
		cfg->slavedelay = lp->slavedelay / HZ;
		cfg->chargeint = (lp->hupflags & ISDN_CHARGEHUP) ?
		    (lp->chargeint / HZ) : 0;
		cfg->pppbind = lp->pppbind;
		if (lp->slave)
			strcpy(cfg->slave, ((isdn_net_local *) lp->slave->priv)->name);
		else
			cfg->slave[0] = '\0';
		if (lp->master)
			strcpy(cfg->master, ((isdn_net_local *) lp->master->priv)->name);
		else
			cfg->master[0] = '\0';
		return 0;
	}
	return -ENODEV;
}

/*
 * Add a phone-number to an interface.
 */
int
isdn_net_addphone(isdn_net_ioctl_phone * phone)
{
	isdn_net_dev *p = isdn_net_findif(phone->name);
	isdn_net_phone *n;

	if (isdn_net_checkwild(phone->phone) && (phone->outgoing & 1))
		return -EINVAL;
	if (p) {
		if (!(n = (isdn_net_phone *) kmalloc(sizeof(isdn_net_phone), GFP_KERNEL)))
			return -ENOMEM;
		strcpy(n->num, phone->phone);
		n->next = p->local->phone[phone->outgoing & 1];
		p->local->phone[phone->outgoing & 1] = n;
		return 0;
	}
	return -ENODEV;
}

/*
 * Copy a string of all phone-numbers of an interface to user space.
 * This might sleep and must be called with the isdn semaphore down.
 */
int
isdn_net_getphones(isdn_net_ioctl_phone * phone, char *phones)
{
	isdn_net_dev *p = isdn_net_findif(phone->name);
	int inout = phone->outgoing & 1;
	int more = 0;
	int count = 0;
	isdn_net_phone *n;

	if (!p)
		return -ENODEV;
	inout &= 1;
	for (n = p->local->phone[inout]; n; n = n->next) {
		if (more) {
			put_user(' ', phones++);
			count++;
		}
		if (copy_to_user(phones, n->num, strlen(n->num) + 1)) {
			return -EFAULT;
		}
		phones += strlen(n->num);
		count += strlen(n->num);
		more = 1;
	}
	put_user(0, phones);
	count++;
	return count;
}

/*
 * Delete a phone-number from an interface.
 */

int
isdn_net_delphone(isdn_net_ioctl_phone * phone)
{
	isdn_net_dev *p = isdn_net_findif(phone->name);
	int inout = phone->outgoing & 1;
	isdn_net_phone *n;
	isdn_net_phone *m;
	int flags;

	if (p) {
		save_flags(flags);
		cli();
		n = p->local->phone[inout];
		m = NULL;
		while (n) {
			if (!strcmp(n->num, phone->phone)) {
				if (p->local->dial == n)
					p->local->dial = n->next;
				if (m)
					m->next = n->next;
				else
					p->local->phone[inout] = n->next;
				kfree(n);
				restore_flags(flags);
				return 0;
			}
			m = n;
			n = (isdn_net_phone *) n->next;
		}
		restore_flags(flags);
		return -EINVAL;
	}
	return -ENODEV;
}

/*
 * Delete all phone-numbers of an interface.
 */
static int
isdn_net_rmallphone(isdn_net_dev * p)
{
	isdn_net_phone *n;
	isdn_net_phone *m;
	int flags;
	int i;

	save_flags(flags);
	cli();
	for (i = 0; i < 2; i++) {
		n = p->local->phone[i];
		while (n) {
			m = n->next;
			kfree(n);
			n = m;
		}
		p->local->phone[i] = NULL;
	}
	p->local->dial = NULL;
	restore_flags(flags);
	return 0;
}

/*
 * Force a hangup of a network-interface.
 */
int
isdn_net_force_hangup(char *name)
{
	isdn_net_dev *p = isdn_net_findif(name);
	struct device *q;

	if (p) {
		if (p->local->isdn_device < 0)
			return 1;
		q = p->local->slave;
		/* If this interface has slaves, do a hangup for them also. */
		while (q) {
			isdn_net_hangup(q);
			q = (((isdn_net_local *) q->priv)->slave);
		}
		isdn_net_hangup(&p->dev);
		return 0;
	}
	return -ENODEV;
}

/*
 * Helper-function for isdn_net_rm: Do the real work.
 */
static int
isdn_net_realrm(isdn_net_dev * p, isdn_net_dev * q)
{
	int flags;

	save_flags(flags);
	cli();
	if (p->local->master) {
		/* If it's a slave, it may be removed even if it is busy. However
		 * it has to be hung up first.
		 */
		isdn_net_hangup(&p->dev);
		p->dev.start = 0;
	}
	if (p->dev.start) {
		restore_flags(flags);
		return -EBUSY;
	}
#ifdef CONFIG_ISDN_X25
	if( p -> cprot && p -> cprot -> pops )
		p -> cprot -> pops -> proto_del ( p -> cprot );
#endif
	/* Free all phone-entries */
	isdn_net_rmallphone(p);
	/* If interface is bound exclusive, free channel-usage */
	if (p->local->exclusive != -1)
		isdn_unexclusive_channel(p->local->pre_device, p->local->pre_channel);
	if (p->local->master) {
		/* It's a slave-device, so update master's slave-pointer if necessary */
		if (((isdn_net_local *) (p->local->master->priv))->slave == &p->dev)
			((isdn_net_local *) (p->local->master->priv))->slave = p->local->slave;
	} else {
		/* Unregister only if it's a master-device */
		p->dev.hard_header_cache = p->local->org_hhc;
		p->dev.header_cache_update = p->local->org_hcu;
		unregister_netdev(&p->dev);
	}
	/* Unlink device from chain */
	if (q)
		q->next = p->next;
	else
		dev->netdev = p->next;
	if (p->local->slave) {
		/* If this interface has a slave, remove it also */
		char *slavename = ((isdn_net_local *) (p->local->slave->priv))->name;
		isdn_net_dev *n = dev->netdev;
		q = NULL;
		while (n) {
			if (!strcmp(n->local->name, slavename)) {
				isdn_net_realrm(n, q);
				break;
			}
			q = n;
			n = (isdn_net_dev *) n->next;
		}
	}
	/* If no more net-devices remain, disable auto-hangup timer */
	if (dev->netdev == NULL)
		isdn_timer_ctrl(ISDN_TIMER_NETHANGUP, 0);
	restore_flags(flags);

	kfree(p->local);
	kfree(p);

	return 0;
}

/*
 * Remove a single network-interface.
 */
int
isdn_net_rm(char *name)
{
	isdn_net_dev *p;
	isdn_net_dev *q;

	/* Search name in netdev-chain */
	p = dev->netdev;
	q = NULL;
	while (p) {
		if (!strcmp(p->local->name, name))
			return (isdn_net_realrm(p, q));
		q = p;
		p = (isdn_net_dev *) p->next;
	}
	/* If no more net-devices remain, disable auto-hangup timer */
	if (dev->netdev == NULL)
		isdn_timer_ctrl(ISDN_TIMER_NETHANGUP, 0);
	return -ENODEV;
}

/*
 * Remove all network-interfaces
 */
int
isdn_net_rmall(void)
{
	int flags;
	int ret;

	/* Walk through netdev-chain */
	save_flags(flags);
	cli();
	while (dev->netdev) {
		if (!dev->netdev->local->master) {
			/* Remove master-devices only, slaves get removed with their master */
			if ((ret = isdn_net_realrm(dev->netdev, NULL))) {
				restore_flags(flags);
				return ret;
			}
		}
	}
	dev->netdev = NULL;
	restore_flags(flags);
	return 0;
}

#ifdef DEV_NUMBUFFS
/*
 * helper function to flush device queues
 * the better place would be net/core/dev.c
 */
static void
dev_purge_queues(struct device *dev)
{
	int i;
	for (i = 0; i < DEV_NUMBUFFS; i++) {
		struct sk_buff *skb;
		while ((skb = skb_dequeue(&dev->buffs[i])))
			dev_kfree_skb(skb);
	}

}
#endif
