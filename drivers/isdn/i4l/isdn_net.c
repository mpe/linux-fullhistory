/* $Id: isdn_net.c,v 1.140.6.11 2001/11/06 20:58:28 kai Exp $
 *
 * Linux ISDN subsystem, network interfaces and related functions (linklevel).
 *
 * Copyright 1994-1998  by Fritz Elfert (fritz@isdn4linux.de)
 * Copyright 1995,96    by Thinking Objects Software GmbH Wuerzburg
 * Copyright 1995,96    by Michael Hipp (Michael.Hipp@student.uni-tuebingen.de)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * Data Over Voice (DOV) support added - Guy Ellis 23-Mar-02 
 *                                       guy@traverse.com.au
 * Outgoing calls - looks for a 'V' in first char of dialed number
 * Incoming calls - checks first character of eaz as follows:
 *   Numeric - accept DATA only - original functionality
 *   'V'     - accept VOICE (DOV) only
 *   'B'     - accept BOTH DATA and DOV types
 *
 * Jan 2001: fix CISCO HDLC      Bjoern A. Zeeb <i4l@zabbadoz.net>
 *           for info on the protocol, see 
 *           http://i4l.zabbadoz.net/i4l/cisco-hdlc.txt
 */

#include <linux/config.h>
#include <linux/isdn.h>
#include <net/arp.h>
#include <net/dst.h>
#include <net/pkt_sched.h>
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

enum {
	ST_NULL,
	ST_OUT_WAIT_DCONN,
	ST_OUT_WAIT_BCONN,
	ST_IN_WAIT_DCONN,
	ST_IN_WAIT_BCONN,
	ST_ACTIVE,
	ST_WAIT_BEFORE_CB,
};

enum {
	ST_CHARGE_NULL,
	ST_CHARGE_GOT_CINF,  /* got a first charge info */
	ST_CHARGE_HAVE_CINT, /* got a second chare info and thus the timing */
};

/* keep clear of ISDN_CMD_* and ISDN_STAT_* */
enum {
	EV_NET_DIAL            = 0x200,
	EV_NET_TIMER_IN_DCONN  = 0x201,
	EV_NET_TIMER_IN_BCONN  = 0x202,
	EV_NET_TIMER_OUT_DCONN = 0x203,
	EV_NET_TIMER_OUT_BCONN = 0x204,
	EV_NET_TIMER_CB        = 0x205,
};

LIST_HEAD(isdn_net_devs); /* Linked list of isdn_net_dev's */

/*
 * Outline of new tbusy handling: 
 *
 * Old method, roughly spoken, consisted of setting tbusy when entering
 * isdn_net_start_xmit() and at several other locations and clearing
 * it from isdn_net_start_xmit() thread when sending was successful.
 *
 * With 2.3.x multithreaded network core, to prevent problems, tbusy should
 * only be set by the isdn_net_start_xmit() thread and only when a tx-busy
 * condition is detected. Other threads (in particular isdn_net_stat_callb())
 * are only allowed to clear tbusy.
 *
 * -HE
 */

/*
 * About SOFTNET:
 * Most of the changes were pretty obvious and basically done by HE already.
 *
 * One problem of the isdn net device code is that is uses struct net_device
 * for masters and slaves. However, only master interface are registered to 
 * the network layer, and therefore, it only makes sense to call netif_* 
 * functions on them.
 *
 * --KG
 */

/* 
 * Find out if the netdevice has been ifup-ed yet.
 * For slaves, look at the corresponding master.
 */
static __inline__ int isdn_net_device_started(isdn_net_dev *n)
{
	isdn_net_local *lp = &n->local;
	struct net_device *dev;
	
	if (lp->master) 
		dev = lp->master;
	else
		dev = &n->dev;
	return netif_running(dev);
}

/*
 * wake up the network -> net_device queue.
 * For slaves, wake the corresponding master interface.
 */
static __inline__ void isdn_net_device_wake_queue(isdn_net_local *lp)
{
	if (lp->master) 
		netif_wake_queue(lp->master);
	else
		netif_wake_queue(&lp->netdev->dev);
}

/*
 * stop the network -> net_device queue.
 * For slaves, stop the corresponding master interface.
 */
static __inline__ void isdn_net_device_stop_queue(isdn_net_local *lp)
{
	if (lp->master)
		netif_stop_queue(lp->master);
	else
		netif_stop_queue(&lp->netdev->dev);
}

/*
 * find out if the net_device which this lp belongs to (lp can be
 * master or slave) is busy. It's busy iff all (master and slave) 
 * queues are busy
 */
static __inline__ int isdn_net_device_busy(isdn_net_local *lp)
{
	isdn_net_local *nlp;
	isdn_net_dev *nd;
	unsigned long flags;

	if (!isdn_net_lp_busy(lp))
		return 0;

	if (lp->master)
		nd = ((isdn_net_local *) lp->master->priv)->netdev;
	else
		nd = lp->netdev;
	
	spin_lock_irqsave(&nd->queue_lock, flags);
	nlp = lp->next;
	while (nlp != lp) {
		if (!isdn_net_lp_busy(nlp)) {
			spin_unlock_irqrestore(&nd->queue_lock, flags);
			return 0;
		}
		nlp = nlp->next;
	}
	spin_unlock_irqrestore(&nd->queue_lock, flags);
	return 1;
}

static __inline__ void isdn_net_inc_frame_cnt(isdn_net_local *lp)
{
	atomic_inc(&lp->frame_cnt);
	if (isdn_net_device_busy(lp))
		isdn_net_device_stop_queue(lp);
}

static __inline__ void isdn_net_dec_frame_cnt(isdn_net_local *lp)
{
	atomic_dec(&lp->frame_cnt);

	if (!(isdn_net_device_busy(lp))) {
		if (!skb_queue_empty(&lp->super_tx_queue)) {
			queue_task(&lp->tqueue, &tq_immediate);
			mark_bh(IMMEDIATE_BH);
		} else {
			isdn_net_device_wake_queue(lp);
		}
       }                                                                      
}

static __inline__ void isdn_net_zero_frame_cnt(isdn_net_local *lp)
{
	atomic_set(&lp->frame_cnt, 0);
}

/* For 2.2.x we leave the transmitter busy timeout at 2 secs, just 
 * to be safe.
 * For 2.3.x we push it up to 20 secs, because call establishment
 * (in particular callback) may take such a long time, and we 
 * don't want confusing messages in the log. However, there is a slight
 * possibility that this large timeout will break other things like MPPP,
 * which might rely on the tx timeout. If so, we'll find out this way...
 */

#define ISDN_NET_TX_TIMEOUT (20*HZ) 

/* Prototypes */

int isdn_net_force_dial_lp(isdn_net_local *);
static int isdn_net_start_xmit(struct sk_buff *, struct net_device *);
static void do_dialout(isdn_net_local *lp);

static void isdn_net_ciscohdlck_connected(isdn_net_local *lp);
static void isdn_net_ciscohdlck_disconnected(isdn_net_local *lp);

static int isdn_net_handle_event(isdn_net_local *lp, int pr, void *arg);

char *isdn_net_revision = "$Revision: 1.140.6.11 $";

 /*
  * Code for raw-networking over ISDN
  */

static void
isdn_net_unreachable(struct net_device *dev, struct sk_buff *skb, char *reason)
{
	u_short proto = ntohs(skb->protocol);
	
	printk(KERN_DEBUG "isdn_net: %s: %s, signalling dst_link_failure %s\n",
	       dev->name,
	       (reason != NULL) ? reason : "unknown",
	       (proto != ETH_P_IP) ? "Protocol != ETH_P_IP" : "");
	
	dst_link_failure(skb);
}

static void
isdn_net_reset(struct net_device *dev)
{
#ifdef CONFIG_ISDN_X25
	struct concap_device_ops * dops =
		( (isdn_net_local *) dev->priv ) -> dops;
	struct concap_proto * cprot =
		( (isdn_net_local *) dev->priv ) -> netdev -> cprot;
#endif
	ulong flags;

	/* not sure if the cli() is needed at all --KG */
	save_flags(flags);
	cli();                  /* Avoid glitch on writes to CMD regs */
#ifdef CONFIG_ISDN_X25
	if( cprot && cprot -> pops && dops )
		cprot -> pops -> restart ( cprot, dev, dops );
#endif
	restore_flags(flags);
}

/* Open/initialize the board. */
static int
isdn_net_open(struct net_device *dev)
{
	int i;
	struct net_device *p;
	struct in_device *in_dev;

	/* moved here from isdn_net_reset, because only the master has an
	   interface associated which is supposed to be started. BTW:
	   we need to call netif_start_queue, not netif_wake_queue here */
	netif_start_queue(dev);

	isdn_net_reset(dev);
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
	lp->flags |= ISDN_NET_CONNECTED;
	lp->isdn_slot = idx;
	isdn_slot_set_rx_netdev(lp->isdn_slot, lp->netdev);
	isdn_slot_set_st_netdev(lp->isdn_slot, lp->netdev);
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
	skb_queue_purge(&lp->super_tx_queue);

	if (!lp->master) {	/* reset only master device */
		/* Moral equivalent of dev_purge_queues():
		   BEWARE! This chunk of code cannot be called from hardware
		   interrupt handler. I hope it is true. --ANK
		 */
		qdisc_reset(lp->netdev->dev.qdisc);
	}
	lp->dialstate = ST_NULL;
	if (lp->isdn_slot >= 0) {
		isdn_slot_set_rx_netdev(lp->isdn_slot, NULL);
		isdn_slot_set_st_netdev(lp->isdn_slot, NULL);
		isdn_slot_free(lp->isdn_slot, ISDN_USAGE_NET);
	}
	lp->flags &= ~ISDN_NET_CONNECTED;
	lp->isdn_slot = -1;

	restore_flags(flags);
}

/*
 * Perform auto-hangup for net-interfaces.
 *
 * auto-hangup:
 * Increment idle-counter (this counter is reset on any incoming or
 * outgoing packet), if counter exceeds configured limit either do a
 * hangup immediately or - if configured - wait until just before the next
 * charge-info.
 */

static void isdn_net_hup_timer(unsigned long data)
{
	isdn_net_local *lp = (isdn_net_local *) data;

	if (!(lp->flags & ISDN_NET_CONNECTED) || lp->dialstate != ST_ACTIVE) {
		isdn_BUG();
		return;
	}
	
	dbg_net_dial("%s: huptimer %d, onhtime %d, chargetime %ld, chargeint %d\n",
		     l->name, l->huptimer, l->onhtime, l->chargetime, l->chargeint);

	if (!(lp->onhtime))
		return;
	
	if (lp->huptimer++ <= lp->onhtime)
		goto mod_timer;

	if (lp->hupflags & ISDN_MANCHARGE && lp->hupflags & ISDN_CHARGEHUP) {
		while (time_after(jiffies, lp->chargetime + lp->chargeint))
			lp->chargetime += lp->chargeint;
		
		if (time_after(jiffies, lp->chargetime + lp->chargeint - 2 * HZ)) {
			if (lp->outgoing || lp->hupflags & ISDN_INHUP) {
				isdn_net_hangup(lp);
				return;
			}
		}
	} else if (lp->outgoing) {
		if (lp->hupflags & ISDN_CHARGEHUP) {
			if (lp->charge_state != ST_CHARGE_HAVE_CINT) {
				dbg_net_dial("%s: did not get CINT\n", lp->name);
				isdn_net_hangup(lp);
				return;
			} else if (time_after(jiffies, lp->chargetime + lp->chargeint)) {
				dbg_net_dial("%s: chtime = %lu, chint = %d\n",
					     lp->name, lp->chargetime, lp->chargeint);
				isdn_net_hangup(lp);
				return;
			}
		}
	} else if (lp->hupflags & ISDN_INHUP) {
		isdn_net_hangup(lp);
		return;
	}
 mod_timer:
	mod_timer(&lp->hup_timer, lp->hup_timer.expires + HZ);
}

static void isdn_net_lp_disconnected(isdn_net_local *lp)
{
	isdn_net_rm_from_bundle(lp);
}

static void isdn_net_connected(isdn_net_local *lp)
{
#ifdef CONFIG_ISDN_X25
	struct concap_proto *cprot = lp -> netdev -> cprot;
	struct concap_proto_ops *pops = cprot ? cprot -> pops : 0;
#endif
	lp->dialstate = ST_ACTIVE;
	lp->hup_timer.expires = jiffies + HZ;
	add_timer(&lp->hup_timer);

	if (lp->p_encap == ISDN_NET_ENCAP_CISCOHDLCK)
		isdn_net_ciscohdlck_connected(lp);
	if (lp->p_encap != ISDN_NET_ENCAP_SYNCPPP) {
		if (lp->master) { /* is lp a slave? */
			isdn_net_dev *nd = ((isdn_net_local *)lp->master->priv)->netdev;
			isdn_net_add_to_bundle(nd, lp);
		}
	}
	printk(KERN_INFO "isdn_net: %s connected\n", lp->name);
	/* If first Chargeinfo comes before B-Channel connect,
	 * we correct the timestamp here.
	 */
	lp->chargetime = jiffies;
	
	/* reset dial-timeout */
	lp->dialstarted = 0;
	lp->dialwait_timer = 0;
	
	lp->transcount = 0;
	lp->cps = 0;
	lp->last_jiffies = jiffies;

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
	/* ppp needs to do negotiations first */
	if (lp->p_encap != ISDN_NET_ENCAP_SYNCPPP)
		isdn_net_device_wake_queue(lp);
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
	isdn_net_dev *p = isdn_slot_st_netdev(idx);
	isdn_net_local *lp;
	int cmd = c->command;

	if (!p) {
		HERE;
		return 0;
	}
	lp = &p->local;

	return isdn_net_handle_event(lp, cmd, c);
}

static void
isdn_net_dial_timer(unsigned long data)
{
	isdn_net_local *lp = (isdn_net_local *) data;

	isdn_net_handle_event(lp, lp->dial_event, NULL);
}

/* Initiate dialout. Set phone-number-pointer to first number
 * of interface.
 */
static void
init_dialout(isdn_net_local *lp)
{
	lp->dial = 0;

	if (lp->dialtimeout > 0 &&
	    (lp->dialstarted == 0 || 
	     time_after(jiffies, lp->dialstarted + lp->dialtimeout + lp->dialwait))) {
		lp->dialstarted = jiffies;
		lp->dialwait_timer = 0;
	}
	lp->dialretry = 0;
	do_dialout(lp);
}

/* Setup interface, dial current phone-number, switch to next number.
 * If list of phone-numbers is exhausted, increment
 * retry-counter.
 */
static void
do_dialout(isdn_net_local *lp)
{
	int i;
	unsigned long flags;
	struct isdn_net_phone *phone;
	struct dial_info dial = {
		.l2_proto = lp->l2_proto,
		.l3_proto = lp->l3_proto,
		.si1      = 7,
		.si2      = 0,
		.msn      = lp->msn,
	};

	if (ISDN_NET_DIALMODE(*lp) == ISDN_NET_DM_OFF)
		return;
	
	spin_lock_irqsave(&lp->lock, flags);
	if (list_empty(&lp->phone[1])) {
		spin_unlock_irqrestore(&lp->lock, flags);
		return;
	}
	i = 0;
	list_for_each_entry(phone, &lp->phone[1], list) {
		if (i++ == lp->dial)
			goto found;
	}
	/* otherwise start in front */
	phone = list_entry(lp->phone[1].next, struct isdn_net_phone, list);
	lp->dial = 0;
	lp->dialretry++;
	
 found:
	lp->dial++;
	dial.phone = phone->num;
	spin_unlock_irqrestore(&lp->lock, flags);

	if (lp->dialretry > lp->dialmax) {
		if (lp->dialtimeout == 0) {
			lp->dialwait_timer = jiffies + lp->dialwait;
			lp->dialstarted = 0;
		}
		isdn_net_hangup(lp);
		return;
	}
	if(lp->dialtimeout > 0 &&
	   time_after(jiffies, lp->dialstarted + lp->dialtimeout)) {
		lp->dialwait_timer = jiffies + lp->dialwait;
		lp->dialstarted = 0;
		isdn_net_hangup(lp);
		return;
	}
	/*
	 * Switch to next number or back to start if at end of list.
	 */
	isdn_slot_dial(lp->isdn_slot, &dial);

	lp->huptimer = 0;
	lp->outgoing = 1;
	if (lp->chargeint)
		lp->charge_state = ST_CHARGE_HAVE_CINT;
	else
		lp->charge_state = ST_CHARGE_NULL;

	if (lp->cbdelay && (lp->flags & ISDN_NET_CBOUT)) {
		lp->dial_timer.expires = jiffies + lp->cbdelay;
		lp->dial_event = EV_NET_TIMER_CB;
	} else {
		lp->dial_timer.expires = jiffies + 10 * HZ;
		lp->dial_event = EV_NET_TIMER_OUT_DCONN;
	}
	lp->dialstate = ST_OUT_WAIT_DCONN;
	add_timer(&lp->dial_timer);
}

/* For EV_NET_DIAL, returns 1 if timer callback is needed 
 * For ISDN_STAT_*, returns 1 if event was for us 
 */
static int
isdn_net_handle_event(isdn_net_local *lp, int pr, void *arg)
{
#ifdef CONFIG_ISDN_X25
	struct concap_proto *cprot = lp -> netdev -> cprot;
	struct concap_proto_ops *pops = cprot ? cprot -> pops : 0;
#endif
	isdn_net_dev *p = lp->netdev;
	isdn_ctrl *c = arg;
	isdn_ctrl cmd;

	dbg_net_dial("%s: dialstate=%d pr=%#x\n", lp->name, lp->dialstate,pr);

	switch (lp->dialstate) {
	case ST_ACTIVE:
		switch (pr) {
		case ISDN_STAT_BSENT:
			/* A packet has successfully been sent out */
			if (lp->flags & ISDN_NET_CONNECTED) {
				isdn_net_dec_frame_cnt(lp);
				lp->stats.tx_packets++;
				lp->stats.tx_bytes += c->parm.length;
				return 1;
			}
			break;
		case ISDN_STAT_DHUP:
			/* Either D-Channel-hangup or error during dialout */
#ifdef CONFIG_ISDN_X25 // FIXME handle != ST_0?
			/* If we are not connencted then dialing had
			   failed. If there are generic encap protocol
			   receiver routines signal the closure of
			   the link*/
			
			if (!(lp->flags & ISDN_NET_CONNECTED)
			    && pops && pops->disconn_ind)
				pops -> disconn_ind(cprot);
#endif /* CONFIG_ISDN_X25 */
			if (lp->flags & ISDN_NET_CONNECTED) {
				if (lp->p_encap == ISDN_NET_ENCAP_CISCOHDLCK)
					isdn_net_ciscohdlck_disconnected(lp);
#ifdef CONFIG_ISDN_PPP
				if (lp->p_encap == ISDN_NET_ENCAP_SYNCPPP)
					isdn_ppp_free(lp);
#endif
				isdn_net_lp_disconnected(lp);
				isdn_slot_all_eaz(lp->isdn_slot);
				printk(KERN_INFO "%s: remote hangup\n", lp->name);
				printk(KERN_INFO "%s: Chargesum is %d\n", lp->name,
				       lp->charge);
				isdn_net_unbind_channel(lp);
				return 1;
			}
			break;
#ifdef CONFIG_ISDN_X25 // FIXME handle != ST_0?
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
		case ISDN_STAT_CINF:
			/* Charge-info from TelCo. Calculate interval between
			 * charge-infos and set timestamp for last info for
			 * usage by isdn_net_autohup()
			 */
			lp->charge++;
			switch (lp->charge_state) {
			case ST_CHARGE_NULL:
				lp->charge_state = ST_CHARGE_GOT_CINF;
				break;
			case ST_CHARGE_GOT_CINF:
				lp->charge_state = ST_CHARGE_HAVE_CINT;
				/* fall through */
			case ST_CHARGE_HAVE_CINT:
				lp->chargeint = jiffies - lp->chargetime - 2 * HZ;
				break;
			}
			lp->chargetime = jiffies;
			dbg_net_dial("%s: got CINF\n", lp->name);
			return 1;
		}
		break;
	case ST_OUT_WAIT_DCONN:
		switch (pr) {
		case EV_NET_TIMER_OUT_DCONN:
			/* try again */
			do_dialout(lp);
			return 1;
		case EV_NET_TIMER_CB:
			/* Remote does callback. Hangup after cbdelay, 
			 * then wait for incoming call */
			printk(KERN_INFO "%s: hangup waiting for callback ...\n", lp->name);
			isdn_net_hangup(lp);
			return 1;
		case ISDN_STAT_DCONN:
			/* Got D-Channel-Connect, send B-Channel-request */
			del_timer(&lp->dial_timer);
			lp->dialstate = ST_OUT_WAIT_BCONN;
			isdn_slot_command(lp->isdn_slot, ISDN_CMD_ACCEPTB, &cmd);
			lp->dial_timer.expires = jiffies + 10 * HZ;
			lp->dial_event = EV_NET_TIMER_OUT_BCONN;
			add_timer(&lp->dial_timer);
			return 1;
		case ISDN_STAT_DHUP:
			del_timer(&lp->dial_timer);
			isdn_slot_all_eaz(lp->isdn_slot);
			printk(KERN_INFO "%s: remote hangup\n", lp->name);
			isdn_net_unbind_channel(lp);
			return 1;
		}
		break;
	case ST_OUT_WAIT_BCONN:
		switch (pr) {
		case EV_NET_TIMER_OUT_BCONN:
			/* try again */
			do_dialout(lp);
			return 1;
		case ISDN_STAT_BCONN:
			del_timer(&lp->dial_timer);
			isdn_slot_set_usage(lp->isdn_slot, isdn_slot_usage(lp->isdn_slot) | ISDN_USAGE_OUTGOING);
			isdn_net_connected(lp);
			return 1;
		case ISDN_STAT_DHUP:
			del_timer(&lp->dial_timer);
			isdn_slot_all_eaz(lp->isdn_slot);
			printk(KERN_INFO "%s: remote hangup\n", lp->name);
			isdn_net_unbind_channel(lp);
			return 1;
		}
		break;
	case ST_IN_WAIT_DCONN:
		switch (pr) {
		case EV_NET_TIMER_IN_DCONN:
			isdn_net_hangup(lp);
			return 1;
		case ISDN_STAT_DCONN:
			del_timer(&lp->dial_timer);
			lp->dialstate = ST_IN_WAIT_BCONN;
			isdn_slot_command(lp->isdn_slot, ISDN_CMD_ACCEPTB, &cmd);
			lp->dial_timer.expires = jiffies + 10 * HZ;
			lp->dial_event = EV_NET_TIMER_IN_BCONN;
			add_timer(&lp->dial_timer);
			return 1;
		case ISDN_STAT_DHUP:
			del_timer(&lp->dial_timer);
			isdn_slot_all_eaz(lp->isdn_slot);
			printk(KERN_INFO "%s: remote hangup\n", lp->name);
			isdn_net_unbind_channel(lp);
			return 1;
		}
		break;
	case ST_IN_WAIT_BCONN:
		switch (pr) {
		case EV_NET_TIMER_IN_BCONN:
			isdn_net_hangup(lp);
			break;
		case ISDN_STAT_BCONN:
			del_timer(&lp->dial_timer);
			isdn_slot_set_rx_netdev(lp->isdn_slot, p);
			isdn_net_connected(lp);
			return 1;
		case ISDN_STAT_DHUP:
			del_timer(&lp->dial_timer);
			isdn_slot_all_eaz(lp->isdn_slot);
			printk(KERN_INFO "%s: remote hangup\n", lp->name);
			isdn_net_unbind_channel(lp);
			return 1;
		}
		break;
	case ST_WAIT_BEFORE_CB:
		switch (pr) {
		case EV_NET_TIMER_CB:
			/* Callback Delay */
			init_dialout(lp);
			return 1;
		}
		break;
	default:
		isdn_BUG();
		break;
	}
	printk("NOT HANDLED?\n");
	return 0;
}

/*
 * Perform hangup for a net-interface.
 */
void
isdn_net_hangup(isdn_net_local *lp)
{
	isdn_ctrl cmd;
#ifdef CONFIG_ISDN_X25
	struct concap_proto *cprot = lp -> netdev -> cprot;
	struct concap_proto_ops *pops = cprot ? cprot -> pops : 0;
#endif

	del_timer_sync(&lp->hup_timer);
	if (lp->flags & ISDN_NET_CONNECTED) {
		if (lp->slave != NULL) {
			isdn_net_local *slp = (isdn_net_local *)lp->slave->priv;
			if (slp->flags & ISDN_NET_CONNECTED) {
				printk(KERN_INFO
					"isdn_net: hang up slave %s before %s\n",
					slp->name, lp->name);
				isdn_net_hangup(slp);
			}
		}
		printk(KERN_INFO "isdn_net: local hangup %s\n", lp->name);
#ifdef CONFIG_ISDN_PPP
		if (lp->p_encap == ISDN_NET_ENCAP_SYNCPPP)
			isdn_ppp_free(lp);
#endif
		isdn_net_lp_disconnected(lp);
#ifdef CONFIG_ISDN_X25
		/* try if there are generic encap protocol
		   receiver routines and signal the closure of
		   the link */
		if( pops && pops -> disconn_ind )
		  pops -> disconn_ind(cprot);
#endif /* CONFIG_ISDN_X25 */

		isdn_slot_command(lp->isdn_slot, ISDN_CMD_HANGUP, &cmd);
		printk(KERN_INFO "%s: Chargesum is %d\n", lp->name, lp->charge);
		isdn_slot_all_eaz(lp->isdn_slot);
	}
	isdn_net_unbind_channel(lp);
}

void
isdn_net_hangup_all()
{
	struct list_head *l;

	list_for_each(l, &isdn_net_devs) {
		isdn_net_dev *p = list_entry(l, isdn_net_dev, global_list);
		isdn_net_hangup(&p->local);
	}
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
			printk(KERN_INFO
				"OPEN: %d.%d.%d.%d -> %d.%d.%d.%d%s\n",

			       p[12], p[13], p[14], p[15],
			       p[16], p[17], p[18], p[19],
			       addinfo);
			break;
		case ETH_P_ARP:
			printk(KERN_INFO
				"OPEN: ARP %d.%d.%d.%d -> *.*.*.* ?%d.%d.%d.%d\n",
			       p[14], p[15], p[16], p[17],
			       p[24], p[25], p[26], p[27]);
			break;
	}
}

/*
 * this function is used to send supervisory data, i.e. data which was
 * not received from the network layer, but e.g. frames from ipppd, CCP
 * reset frames etc.
 */
void isdn_net_write_super(isdn_net_local *lp, struct sk_buff *skb)
{
	if (in_irq()) {
		// we can't grab the lock from irq context, 
		// so we just queue the packet
		skb_queue_tail(&lp->super_tx_queue, skb); 
		queue_task(&lp->tqueue, &tq_immediate);
		mark_bh(IMMEDIATE_BH);
		return;
	}

	spin_lock_bh(&lp->xmit_lock);
	if (!isdn_net_lp_busy(lp)) {
		isdn_net_writebuf_skb(lp, skb);
	} else {
		skb_queue_tail(&lp->super_tx_queue, skb);
	}
	spin_unlock_bh(&lp->xmit_lock);
}

/*
 * called from tq_immediate
 */
static void isdn_net_softint(void *private)
{
	isdn_net_local *lp = private;
	struct sk_buff *skb;

	spin_lock_bh(&lp->xmit_lock);
	while (!isdn_net_lp_busy(lp)) {
		skb = skb_dequeue(&lp->super_tx_queue);
		if (!skb)
			break;
		isdn_net_writebuf_skb(lp, skb);                                
	}
	spin_unlock_bh(&lp->xmit_lock);
}

/* 
 * all frames sent from the (net) LL to a HL driver should go via this function
 * it's serialized by the caller holding the lp->xmit_lock spinlock
 */
void isdn_net_writebuf_skb(isdn_net_local *lp, struct sk_buff *skb)
{
	int ret;
	int len = skb->len;     /* save len */

	/* before obtaining the lock the caller should have checked that
	   the lp isn't busy */
	if (isdn_net_lp_busy(lp)) {
		isdn_BUG();
		goto error;
	}

	if (!(lp->flags & ISDN_NET_CONNECTED)) {
		isdn_BUG();
		goto error;
	}
	ret = isdn_slot_write(lp->isdn_slot, skb);
	if (ret != len) {
		/* we should never get here */
		printk(KERN_WARNING "%s: HL driver queue full\n", lp->name);
		goto error;
	}
	
	lp->transcount += len;
	isdn_net_inc_frame_cnt(lp);
	return;

 error:
	dev_kfree_skb(skb);
	lp->stats.tx_errors++;

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
isdn_net_xmit(struct net_device *ndev, struct sk_buff *skb)
{
	isdn_net_dev *nd;
	isdn_net_local *slp;
	isdn_net_local *lp = (isdn_net_local *) ndev->priv;
	int retv = 0;

	if (((isdn_net_local *) (ndev->priv))->master) {
		isdn_BUG();
		dev_kfree_skb(skb);
		return 0;
	}

	/* For the other encaps the header has already been built */
#ifdef CONFIG_ISDN_PPP
	if (lp->p_encap == ISDN_NET_ENCAP_SYNCPPP) {
		return isdn_ppp_xmit(skb, ndev);
	}
#endif
	nd = ((isdn_net_local *) ndev->priv)->netdev;
	lp = isdn_net_get_locked_lp(nd);
	if (!lp) {
		printk(KERN_WARNING "%s: all channels busy - requeuing!\n", ndev->name);
		return 1;
	}
	/* we have our lp locked from now on */

	/* Reset hangup-timeout */
	lp->huptimer = 0; // FIXME?
	isdn_net_writebuf_skb(lp, skb);
	spin_unlock_bh(&lp->xmit_lock);

	/* the following stuff is here for backwards compatibility.
	 * in future, start-up and hangup of slaves (based on current load)
	 * should move to userspace and get based on an overall cps
	 * calculation
	 */
	if (jiffies != lp->last_jiffies) {
		lp->cps = lp->transcount * HZ / (jiffies - lp->last_jiffies);
		lp->last_jiffies = jiffies;
		lp->transcount = 0;
	}
	if (dev->net_verbose > 3)
		printk(KERN_DEBUG "%s: %d bogocps\n", lp->name, lp->cps);

	if (lp->cps > lp->triggercps) {
		if (lp->slave) {
			if (!lp->sqfull) {
				/* First time overload: set timestamp only */
				lp->sqfull = 1;
				lp->sqfull_stamp = jiffies;
			} else {
				/* subsequent overload: if slavedelay exceeded, start dialing */
				if (time_after(jiffies, lp->sqfull_stamp + lp->slavedelay)) {
					slp = lp->slave->priv;
					if (!(slp->flags & ISDN_NET_CONNECTED)) {
						isdn_net_force_dial_lp((isdn_net_local *) lp->slave->priv);
					}
				}
			}
		}
	} else {
		if (lp->sqfull && time_after(jiffies, lp->sqfull_stamp + lp->slavedelay + (10 * HZ))) {
			lp->sqfull = 0;
		}
		/* this is a hack to allow auto-hangup for slaves on moderate loads */
		nd->queue = &nd->local;
	}

	return retv;

}

static void
isdn_net_adjust_hdr(struct sk_buff *skb, struct net_device *dev)
{
	isdn_net_local *lp = (isdn_net_local *) dev->priv;
	if (!skb)
		return;
	if (lp->p_encap == ISDN_NET_ENCAP_ETHER) {
		int pullsize = (ulong)skb->nh.raw - (ulong)skb->data - ETH_HLEN;
		if (pullsize > 0) {
			printk(KERN_DEBUG "isdn_net: Pull junk %d\n", pullsize);
			skb_pull(skb, pullsize);
		}
	}
}


void isdn_net_tx_timeout(struct net_device * ndev)
{
	isdn_net_local *lp = (isdn_net_local *) ndev->priv;

	printk(KERN_WARNING "isdn_tx_timeout dev %s dialstate %d\n", ndev->name, lp->dialstate);
	if (lp->dialstate == ST_ACTIVE){
		lp->stats.tx_errors++;
                /*
		 * There is a certain probability that this currently
		 * works at all because if we always wake up the interface,
		 * then upper layer will try to send the next packet
		 * immediately. And then, the old clean_up logic in the
		 * driver will hopefully continue to work as it used to do.
		 *
		 * This is rather primitive right know, we better should
		 * clean internal queues here, in particular for multilink and
		 * ppp, and reset HL driver's channel, too.   --HE
		 *
		 * actually, this may not matter at all, because ISDN hardware
		 * should not see transmitter hangs at all IMO
		 * changed KERN_DEBUG to KERN_WARNING to find out if this is 
		 * ever called   --KG
		 */
	}
	ndev->trans_start = jiffies;
	netif_wake_queue(ndev);
}

/*
 * Try sending a packet.
 * If this interface isn't connected to a ISDN-Channel, find a free channel,
 * and start dialing.
 */
static int
isdn_net_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	unsigned long flags;
	isdn_net_local *lp = (isdn_net_local *) ndev->priv;
#ifdef CONFIG_ISDN_X25
	struct concap_proto * cprot = lp -> netdev -> cprot;
#endif
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
		int ret = cprot -> pops -> encap_and_xmit ( cprot , skb);
		if(ret) netif_stop_queue(ndev);
		return ret;
	} else
#endif
	/* auto-dialing xmit function */
	{
		isdn_net_adjust_hdr(skb, ndev);
		isdn_dumppkt("S:", skb->data, skb->len, 40);

		if (!(lp->flags & ISDN_NET_CONNECTED)) {
			int chi;
			/* only do autodial if allowed by config */
			if (!(ISDN_NET_DIALMODE(*lp) == ISDN_NET_DM_AUTO)) {
				isdn_net_unreachable(ndev, skb, "dial rejected: interface not in dialmode `auto'");
				dev_kfree_skb(skb);
				return 0;
			}

			save_flags(flags);
			cli();

			if(lp->dialwait_timer <= 0)
				if(lp->dialstarted > 0 && lp->dialtimeout > 0 && time_before(jiffies, lp->dialstarted + lp->dialtimeout + lp->dialwait))
					lp->dialwait_timer = lp->dialstarted + lp->dialtimeout + lp->dialwait;
			
			if(lp->dialwait_timer > 0) {
				if(time_before(jiffies, lp->dialwait_timer)) {
					isdn_net_unreachable(ndev, skb, "dial rejected: retry-time not reached");
					dev_kfree_skb(skb);
					restore_flags(flags);
					return 0;
				} else
					lp->dialwait_timer = 0;
			}
			/* Grab a free ISDN-Channel */
			if (((chi =
			      isdn_get_free_slot(
				      ISDN_USAGE_NET,
				      lp->l2_proto,
				      lp->l3_proto,
				      lp->pre_device,
				      lp->pre_channel,
				      lp->msn)
				     ) < 0) &&
			    ((chi =
			      isdn_get_free_slot(
				      ISDN_USAGE_NET,
				      lp->l2_proto,
				      lp->l3_proto,
				      lp->pre_device,
				      lp->pre_channel^1,
				      lp->msn)
				    ) < 0)) {
				restore_flags(flags);
				isdn_net_unreachable(ndev, skb,
						     "No channel");
				dev_kfree_skb(skb);
				return 0;
			}
			/* Log packet, which triggered dialing */
			if (dev->net_verbose)
				isdn_net_log_skb(skb, lp);
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
				init_dialout(lp);
				netif_stop_queue(ndev);
				return 1;	/* let upper layer requeue skb packet */
			}
#endif
			/* Initiate dialing */
			restore_flags(flags);
			init_dialout(lp);
			isdn_net_device_stop_queue(lp);
			return 1;
		} else {
			/* Device is connected to an ISDN channel */ 
			ndev->trans_start = jiffies;
			if (lp->dialstate == ST_ACTIVE) {
				/* ISDN connection is established, try sending */
				int ret;
				ret = (isdn_net_xmit(ndev, skb));
				if(ret) netif_stop_queue(ndev);
				return ret;
			} else
				netif_stop_queue(ndev);
		}
	}
	return 1;
}

/*
 * Shutdown a net-interface.
 */
static int
isdn_net_close(struct net_device *dev)
{
	struct net_device *p;
#ifdef CONFIG_ISDN_X25
	struct concap_proto * cprot =
		( (isdn_net_local *) dev->priv ) -> netdev -> cprot;
	/* printk(KERN_DEBUG "isdn_net_close %s\n" , dev-> name ); */
#endif

#ifdef CONFIG_ISDN_X25
	if( cprot && cprot -> pops ) cprot -> pops -> close( cprot );
#endif
	netif_stop_queue(dev);
	if ((p = (((isdn_net_local *) dev->priv)->slave))) {
		/* If this interface has slaves, stop them also */
		while (p) {
#ifdef CONFIG_ISDN_X25
			cprot = ( (isdn_net_local *) p->priv )
				-> netdev -> cprot;
			if( cprot && cprot -> pops )
				cprot -> pops -> close( cprot );
#endif
			isdn_net_hangup(p->priv);
			p = (((isdn_net_local *) p->priv)->slave);
		}
	}
	isdn_net_hangup(dev->priv);
	isdn_MOD_DEC_USE_COUNT();
	return 0;
}

/*
 * Get statistics
 */
static struct net_device_stats *
isdn_net_get_stats(struct net_device *dev)
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
isdn_net_type_trans(struct sk_buff *skb, struct net_device *dev)
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


/* 
 * CISCO HDLC keepalive specific stuff
 */
static struct sk_buff*
isdn_net_ciscohdlck_alloc_skb(isdn_net_local *lp, int len)
{
	unsigned short hl = isdn_slot_hdrlen(lp->isdn_slot);
	struct sk_buff *skb;

	skb = alloc_skb(hl + len, GFP_ATOMIC);
	if (!skb) {
		printk("isdn out of mem at %s:%d!\n", __FILE__, __LINE__);
		return 0;
	}
	skb_reserve(skb, hl);
	return skb;
}

/* cisco hdlck device private ioctls */
int
isdn_ciscohdlck_dev_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	isdn_net_local *lp = (isdn_net_local *) dev->priv;
	unsigned long len = 0;
	unsigned long expires = 0;
	int tmp = 0;
	int period = lp->cisco_keepalive_period;
	char debserint = lp->cisco_debserint;
	int rc = 0;

	if (lp->p_encap != ISDN_NET_ENCAP_CISCOHDLCK)
		return -EINVAL;

	switch (cmd) {
		/* get/set keepalive period */
		case SIOCGKEEPPERIOD:
			len = (unsigned long)sizeof(lp->cisco_keepalive_period);
			if (copy_to_user((char *)ifr->ifr_ifru.ifru_data,
				(int *)&lp->cisco_keepalive_period, len))
				rc = -EFAULT;
			break;
		case SIOCSKEEPPERIOD:
			tmp = lp->cisco_keepalive_period;
			len = (unsigned long)sizeof(lp->cisco_keepalive_period);
			if (copy_from_user((int *)&period,
				(char *)ifr->ifr_ifru.ifru_data, len))
				rc = -EFAULT;
			if ((period > 0) && (period <= 32767))
				lp->cisco_keepalive_period = period;
			else
				rc = -EINVAL;
			if (!rc && (tmp != lp->cisco_keepalive_period)) {
				expires = (unsigned long)(jiffies +
					lp->cisco_keepalive_period * HZ);
				mod_timer(&lp->cisco_timer, expires);
				printk(KERN_INFO "%s: Keepalive period set "
					"to %d seconds.\n",
					lp->name, lp->cisco_keepalive_period);
			}
			break;

		/* get/set debugging */
		case SIOCGDEBSERINT:
			len = (unsigned long)sizeof(lp->cisco_debserint);
			if (copy_to_user((char *)ifr->ifr_ifru.ifru_data,
				(char *)&lp->cisco_debserint, len))
				rc = -EFAULT;
			break;
		case SIOCSDEBSERINT:
			len = (unsigned long)sizeof(lp->cisco_debserint);
			if (copy_from_user((char *)&debserint,
				(char *)ifr->ifr_ifru.ifru_data, len))
				rc = -EFAULT;
			if ((debserint >= 0) && (debserint <= 64))
				lp->cisco_debserint = debserint;
			else
				rc = -EINVAL;
			break;

		default:
			rc = -EINVAL;
			break;
	}
	return (rc);
}

/* called via cisco_timer.function */
static void
isdn_net_ciscohdlck_slarp_send_keepalive(unsigned long data)
{
	isdn_net_local *lp = (isdn_net_local *) data;
	struct sk_buff *skb;
	unsigned char *p;
	unsigned long last_cisco_myseq = lp->cisco_myseq;
	int myseq_diff = 0;

	if (!(lp->flags & ISDN_NET_CONNECTED) || lp->dialstate != ST_ACTIVE) {
		isdn_BUG();
		return;
	}
	lp->cisco_myseq++;

	myseq_diff = (lp->cisco_myseq - lp->cisco_mineseen);
	if ((lp->cisco_line_state) && ((myseq_diff >= 3)||(myseq_diff <= -3))) {
		/* line up -> down */
		lp->cisco_line_state = 0;
		printk (KERN_WARNING
				"UPDOWN: Line protocol on Interface %s,"
				" changed state to down\n", lp->name);
		/* should stop routing higher-level data accross */
	} else if ((!lp->cisco_line_state) &&
		(myseq_diff >= 0) && (myseq_diff <= 2)) {
		/* line down -> up */
		lp->cisco_line_state = 1;
		printk (KERN_WARNING
				"UPDOWN: Line protocol on Interface %s,"
				" changed state to up\n", lp->name);
		/* restart routing higher-level data accross */
	}

	if (lp->cisco_debserint)
		printk (KERN_DEBUG "%s: HDLC "
			"myseq %lu, mineseen %lu%c, yourseen %lu, %s\n",
			lp->name, last_cisco_myseq, lp->cisco_mineseen,
			((last_cisco_myseq == lp->cisco_mineseen) ? '*' : 040),
			lp->cisco_yourseq,
			((lp->cisco_line_state) ? "line up" : "line down"));

	skb = isdn_net_ciscohdlck_alloc_skb(lp, 4 + 14);
	if (!skb)
		return;

	p = skb_put(skb, 4 + 14);

	/* cisco header */
	p += put_u8 (p, CISCO_ADDR_UNICAST);
	p += put_u8 (p, CISCO_CTRL);
	p += put_u16(p, CISCO_TYPE_SLARP);

	/* slarp keepalive */
	p += put_u32(p, CISCO_SLARP_KEEPALIVE);
	p += put_u32(p, lp->cisco_myseq);
	p += put_u32(p, lp->cisco_yourseq);
	p += put_u16(p, 0xffff); // reliablity, always 0xffff

	isdn_net_write_super(lp, skb);

	lp->cisco_timer.expires = jiffies + lp->cisco_keepalive_period * HZ;
	
	add_timer(&lp->cisco_timer);
}

static void
isdn_net_ciscohdlck_slarp_send_request(isdn_net_local *lp)
{
	struct sk_buff *skb;
	unsigned char *p;

	skb = isdn_net_ciscohdlck_alloc_skb(lp, 4 + 14);
	if (!skb)
		return;

	p = skb_put(skb, 4 + 14);

	/* cisco header */
	p += put_u8 (p, CISCO_ADDR_UNICAST);
	p += put_u8 (p, CISCO_CTRL);
	p += put_u16(p, CISCO_TYPE_SLARP);

	/* slarp request */
	p += put_u32(p, CISCO_SLARP_REQUEST);
	p += put_u32(p, 0); // address
	p += put_u32(p, 0); // netmask
	p += put_u16(p, 0); // unused

	isdn_net_write_super(lp, skb);
}

static void 
isdn_net_ciscohdlck_connected(isdn_net_local *lp)
{
	lp->cisco_myseq = 0;
	lp->cisco_mineseen = 0;
	lp->cisco_yourseq = 0;
	lp->cisco_keepalive_period = ISDN_TIMER_KEEPINT;
	lp->cisco_last_slarp_in = 0;
	lp->cisco_line_state = 0;
	lp->cisco_debserint = 0;

	/* send slarp request because interface/seq.no.s reset */
	isdn_net_ciscohdlck_slarp_send_request(lp);

	init_timer(&lp->cisco_timer);
	lp->cisco_timer.data = (unsigned long) lp;
	lp->cisco_timer.function = isdn_net_ciscohdlck_slarp_send_keepalive;
	lp->cisco_timer.expires = jiffies + lp->cisco_keepalive_period * HZ;
	add_timer(&lp->cisco_timer);
}

static void 
isdn_net_ciscohdlck_disconnected(isdn_net_local *lp)
{
	del_timer(&lp->cisco_timer);
}

static void
isdn_net_ciscohdlck_slarp_send_reply(isdn_net_local *lp)
{
	struct sk_buff *skb;
	unsigned char *p;
	struct in_device *in_dev = NULL;
	u32 addr = 0;		/* local ipv4 address */
	u32 mask = 0;		/* local netmask */

	if ((in_dev = lp->netdev->dev.ip_ptr) != NULL) {
		/* take primary(first) address of interface */
		struct in_ifaddr *ifa = in_dev->ifa_list;
		if (ifa != NULL) {
			addr = ifa->ifa_local;
			mask = ifa->ifa_mask;
		}
	}

	skb = isdn_net_ciscohdlck_alloc_skb(lp, 4 + 14);
	if (!skb)
		return;

	p = skb_put(skb, 4 + 14);

	/* cisco header */
	p += put_u8 (p, CISCO_ADDR_UNICAST);
	p += put_u8 (p, CISCO_CTRL);
	p += put_u16(p, CISCO_TYPE_SLARP);

	/* slarp reply, send own ip/netmask; if values are nonsense remote
	 * should think we are unable to provide it with an address via SLARP */
	p += put_u32(p, CISCO_SLARP_REPLY);
	p += put_u32(p, addr);	// address
	p += put_u32(p, mask);	// netmask
	p += put_u16(p, 0);	// unused

	isdn_net_write_super(lp, skb);
}

static void
isdn_net_ciscohdlck_slarp_in(isdn_net_local *lp, struct sk_buff *skb)
{
	unsigned char *p;
	int period;
	u32 code;
	u32 my_seq, addr;
	u32 your_seq, mask;
	u32 local;
	u16 unused;

	if (skb->len < 14)
		return;

	p = skb->data;
	p += get_u32(p, &code);
	
	switch (code) {
	case CISCO_SLARP_REQUEST:
		lp->cisco_yourseq = 0;
		isdn_net_ciscohdlck_slarp_send_reply(lp);
		break;
	case CISCO_SLARP_REPLY:
		addr = ntohl(*(u32 *)p);
		mask = ntohl(*(u32 *)(p+4));
		if (mask != 0xfffffffc)
			goto slarp_reply_out;
		if ((addr & 3) == 0 || (addr & 3) == 3)
			goto slarp_reply_out;
		local = addr ^ 3;
		printk(KERN_INFO "%s: got slarp reply: "
			"remote ip: %d.%d.%d.%d, "
			"local ip: %d.%d.%d.%d "
			"mask: %d.%d.%d.%d\n",
		       lp->name,
		       HIPQUAD(addr),
		       HIPQUAD(local),
		       HIPQUAD(mask));
		break;
  slarp_reply_out:
		 printk(KERN_INFO "%s: got invalid slarp "
				 "reply (%d.%d.%d.%d/%d.%d.%d.%d) "
				 "- ignored\n", lp->name,
				 HIPQUAD(addr), HIPQUAD(mask));
		break;
	case CISCO_SLARP_KEEPALIVE:
		period = (int)((jiffies - lp->cisco_last_slarp_in
				+ HZ/2 - 1) / HZ);
		if (lp->cisco_debserint &&
				(period != lp->cisco_keepalive_period) &&
				lp->cisco_last_slarp_in) {
			printk(KERN_DEBUG "%s: Keepalive period mismatch - "
				"is %d but should be %d.\n",
				lp->name, period, lp->cisco_keepalive_period);
		}
		lp->cisco_last_slarp_in = jiffies;
		p += get_u32(p, &my_seq);
		p += get_u32(p, &your_seq);
		p += get_u16(p, &unused);
		lp->cisco_yourseq = my_seq;
		lp->cisco_mineseen = your_seq;
		break;
	}
}

static void
isdn_net_ciscohdlck_receive(isdn_net_local *lp, struct sk_buff *skb)
{
	unsigned char *p;
 	u8 addr;
 	u8 ctrl;
 	u16 type;
	
	if (skb->len < 4)
		goto out_free;

	p = skb->data;
	p += get_u8 (p, &addr);
	p += get_u8 (p, &ctrl);
	p += get_u16(p, &type);
	skb_pull(skb, 4);
	
	if (addr != CISCO_ADDR_UNICAST && addr != CISCO_ADDR_BROADCAST) {
		printk(KERN_WARNING "%s: Unknown Cisco addr 0x%02x\n",
		       lp->name, addr);
		goto out_free;
	}
	if (ctrl != CISCO_CTRL) {
		printk(KERN_WARNING "%s: Unknown Cisco ctrl 0x%02x\n",
		       lp->name, ctrl);
		goto out_free;
	}

	switch (type) {
	case CISCO_TYPE_SLARP:
		isdn_net_ciscohdlck_slarp_in(lp, skb);
		goto out_free;
	case CISCO_TYPE_CDP:
		if (lp->cisco_debserint)
			printk(KERN_DEBUG "%s: Received CDP packet. use "
				"\"no cdp enable\" on cisco.\n", lp->name);
		goto out_free;
	default:
		/* no special cisco protocol */
		skb->protocol = htons(type);
		netif_rx(skb);
		return;
	}

 out_free:
	kfree_skb(skb);
}

/*
 * Got a packet from ISDN-Channel.
 */
static void
isdn_net_receive(struct net_device *ndev, struct sk_buff *skb)
{
	isdn_net_local *lp = (isdn_net_local *) ndev->priv;
	isdn_net_local *olp = lp;	/* original 'lp' */
#ifdef CONFIG_ISDN_PPP
	int proto = PPP_PROTOCOL(skb->data);
#endif
#ifdef CONFIG_ISDN_X25
	struct concap_proto *cprot = lp -> netdev -> cprot;
#endif
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
	isdn_dumppkt("R:", skb->data, skb->len, 40);
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
			isdn_net_ciscohdlck_receive(lp, skb);
			return;
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
	isdn_net_dev *p = isdn_slot_rx_netdev(idx);

	if (p) {
		isdn_net_local *lp = &p->local;
		if ((lp->flags & ISDN_NET_CONNECTED) &&
		    (lp->dialstate == ST_ACTIVE)) {
			isdn_net_receive(&p->dev, skb);
			return 1;
		}
	}
	return 0;
}

static int
my_eth_header(struct sk_buff *skb, struct net_device *dev, unsigned short type,
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
isdn_net_header(struct sk_buff *skb, struct net_device *dev, unsigned short type,
		void *daddr, void *saddr, unsigned plen)
{
	isdn_net_local *lp = dev->priv;
	unsigned char *p;
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
		case ISDN_NET_ENCAP_CISCOHDLCK:
			p = skb_push(skb, 4);
			p += put_u8 (p, CISCO_ADDR_UNICAST);
			p += put_u8 (p, CISCO_CTRL);
			p += put_u16(p, type);
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
	struct net_device *dev = skb->dev;
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
 * Interface-setup. (just after registering a new interface)
 */
static int
isdn_net_init(struct net_device *ndev)
{
	ushort max_hlhdr_len = 0;
	isdn_net_local *lp = (isdn_net_local *) ndev->priv;
	int drvidx, i;

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

	/* for clients with MPPP maybe higher values better */
	ndev->tx_queue_len = 30;

	for (i = 0; i < ETH_ALEN; i++)
		ndev->broadcast[i] = 0xff;

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
	ndev->do_ioctl = NULL;
	return 0;
}

static void
isdn_net_swapbind(int drvidx)
{
	struct list_head *l;

	dbg_net_icall("n_fi: swapping ch of %d\n", drvidx);
	list_for_each(l, &isdn_net_devs) {
		isdn_net_dev *p = list_entry(l, isdn_net_dev, global_list);
		if (p->local.pre_device != drvidx)
			continue;

		switch (p->local.pre_channel) {
		case 0:
			p->local.pre_channel = 1;
			break;
		case 1:
			p->local.pre_channel = 0;
			break;
		}
	}
}

static void
isdn_net_swap_usage(int i1, int i2)
{
	int u1 = isdn_slot_usage(i1);
	int u2 = isdn_slot_usage(i2);

	dbg_net_icall("n_fi: usage of %d and %d\n", i1, i2);
	isdn_slot_set_usage(i1, (u1 & ~ISDN_USAGE_EXCLUSIVE) | (u2 & ISDN_USAGE_EXCLUSIVE));
	isdn_slot_set_usage(i2, (u2 & ~ISDN_USAGE_EXCLUSIVE) | (u1 & ISDN_USAGE_EXCLUSIVE));
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
 *               5 = No appropriate interface for this call,
 *                   would eventually match if CID was longer.
 */
int
isdn_net_find_icall(int di, int ch, int idx, setup_parm *setup)
{
	char *eaz;
	int si1;
	int si2;
	int ematch;
	int wret;
	int swapped;
	int sidx = 0;
	struct list_head *l;
	struct isdn_net_phone *n;
	ulong flags;
	char nr[32];
	char *my_eaz;
	isdn_ctrl cmd;

	int slot = isdn_dc2minor(di, ch);
	/* Search name in netdev-chain */
	save_flags(flags);
	cli();
	if (!setup->phone[0]) {
		nr[0] = '0';
		nr[1] = '\0';
		printk(KERN_INFO "isdn_net: Incoming call without OAD, assuming '0'\n");
	} else
		strcpy(nr, setup->phone);
	si1 = (int) setup->si1;
	si2 = (int) setup->si2;
	if (!setup->eazmsn[0]) {
		printk(KERN_WARNING "isdn_net: Incoming call without CPN, assuming '0'\n");
		eaz = "0";
	} else
		eaz = setup->eazmsn;
	if (dev->net_verbose > 1)
		printk(KERN_INFO "isdn_net: call from %s,%d,%d -> %s\n", nr, si1, si2, eaz);
        /* Accept DATA and VOICE calls at this stage
        local eaz is checked later for allowed call types */
        if ((si1 != 7) && (si1 != 1)) {
                restore_flags(flags);
                if (dev->net_verbose > 1)
                        printk(KERN_INFO "isdn_net: Service-Indicator not 1 or 7, ignored\n");
                return 0;
        }

	n = NULL;
	ematch = wret = swapped = 0;
	dbg_net_icall("n_fi: di=%d ch=%d idx=%d usg=%d\n", di, ch, idx,
		      isdn_slot_usage(idx));

	list_for_each(l, &isdn_net_devs) {
		int matchret;
		isdn_net_dev *p = list_entry(l, isdn_net_dev, global_list);
		isdn_net_local *lp = &p->local;

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
                /* check acceptable call types for DOV */
                my_eaz = isdn_slot_map_eaz2msn(slot, lp->msn);
                if (si1 == 1) { /* it's a DOV call, check if we allow it */
                        if (*my_eaz == 'v' || *my_eaz == 'V' ||
			    *my_eaz == 'b' || *my_eaz == 'B')
                                my_eaz++; /* skip to allow a match */
                        else
                                my_eaz = 0; /* force non match */
                } else { /* it's a DATA call, check if we allow it */
                        if (*my_eaz == 'b' || *my_eaz == 'B')
                                my_eaz++; /* skip to allow a match */
                }
                if (my_eaz)
                        matchret = isdn_msncmp(eaz, my_eaz);
                else
                        matchret = 1;
                if (!matchret)
                        ematch = 1;

		/* Remember if more numbers eventually can match */
		if (matchret > wret)
			wret = matchret;
		dbg_net_icall("n_fi: if='%s', l.msn=%s, l.flags=%d, l.dstate=%d\n",
		       lp->name, lp->msn, lp->flags, lp->dialstate);
		if ((!matchret) &&                                        /* EAZ is matching   */
		    (((!(lp->flags & ISDN_NET_CONNECTED)) &&              /* but not connected */
		      (USG_NONE(isdn_slot_usage(idx)))) ||                     /* and ch. unused or */
		     ((((lp->dialstate == ST_OUT_WAIT_DCONN) || (lp->dialstate == ST_OUT_WAIT_DCONN)) && /* if dialing        */
		       (!(lp->flags & ISDN_NET_CALLBACK)))                /* but no callback   */
		     )))
			 {
			dbg_net_icall("n_fi: match1, pdev=%d pch=%d\n",
			       lp->pre_device, lp->pre_channel);
			if (isdn_slot_usage(idx) & ISDN_USAGE_EXCLUSIVE) {
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
						dbg_net_icall("n_fi: ch is 0\n");
						if (USG_NONE(isdn_slot_usage(sidx))) {
							/* Second Channel is free, now see if it is bound
							 * exclusive too. */
							if (isdn_slot_usage(sidx) & ISDN_USAGE_EXCLUSIVE) {
								dbg_net_icall("n_fi: 2nd channel is down and bound\n");
								/* Yes, swap bindings only, if the original
								 * binding is bound to channel 1 of this driver */
								if ((lp->pre_device == di) &&
								    (lp->pre_channel == 1)) {
									isdn_net_swapbind(di);
									swapped = 1;
								} else {
									/* ... else iterate next device */
									continue;
								}
							} else {
								dbg_net_icall("n_fi: 2nd channel is down and unbound\n");
								/* No, swap always and swap excl-usage also */
								isdn_net_swap_usage(idx, sidx);
								isdn_net_swapbind(di);
								swapped = 2;
							}
							/* Now check for exclusive binding again */
							dbg_net_icall("n_fi: final check\n");
							if ((isdn_slot_usage(idx) & ISDN_USAGE_EXCLUSIVE) &&
							    ((lp->pre_channel != ch) ||
							     (lp->pre_device != di))) {
								dbg_net_icall("n_fi: final check failed\n");
								continue;
							}
						}
					} else {
						/* We are already on the second channel, so nothing to do */
						dbg_net_icall("n_fi: already on 2nd channel\n");
					}
				}
			}
			dbg_net_icall("n_fi: match2\n");
			if (lp->flags & ISDN_NET_SECURE) {
				spin_lock_irqsave(&lp->lock, flags);
				list_for_each_entry(n, &lp->phone[0], list) {
					if (!isdn_msncmp(nr, n->num)) {
						spin_unlock_irqrestore(&lp->lock, flags);
						break;
					}
				}
				spin_unlock_irqrestore(&lp->lock, flags);
				continue;
			}
			dbg_net_icall("n_fi: match3\n");
			/* matching interface found */

			/*
			 * Is the state STOPPED?
			 * If so, no dialin is allowed,
			 * so reject actively.
			 * */
			if (ISDN_NET_DIALMODE(*lp) == ISDN_NET_DM_OFF) {
				restore_flags(flags);
				printk(KERN_INFO "incoming call, interface %s `stopped' -> rejected\n",
				       lp->name);
				return 3;
			}
			/*
			 * Is the interface up?
			 * If not, reject the call actively.
			 */
			if (!isdn_net_device_started(p)) {
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
					continue;
				}
			} 
			if (lp->flags & ISDN_NET_CALLBACK) {
				int chi;
				/*
				 * Is the state MANUAL?
				 * If so, no callback can be made,
				 * so reject actively.
				 * */
				if (ISDN_NET_DIALMODE(*lp) == ISDN_NET_DM_OFF) {
					restore_flags(flags);
					printk(KERN_INFO "incoming call for callback, interface %s `off' -> rejected\n",
					       lp->name);
					return 3;
				}
				printk(KERN_DEBUG "%s: call from %s -> %s, start callback\n",
				       lp->name, nr, eaz);

				/* Grab a free ISDN-Channel */
				if ((chi = 
				     isdn_get_free_slot(
					     ISDN_USAGE_NET,
					     lp->l2_proto,
					     lp->l3_proto,
					     lp->pre_device,
					     lp->pre_channel,
					     lp->msn)
					    ) < 0) {
					
					printk(KERN_WARNING "isdn_net_find_icall: No channel for %s\n", lp->name);
					restore_flags(flags);
					return 0;
				}
				/* Setup dialstate. */
				lp->dial_timer.expires = jiffies + lp->cbdelay;
				lp->dial_event = EV_NET_TIMER_CB;
				add_timer(&lp->dial_timer);
				
				lp->dialstate = ST_WAIT_BEFORE_CB;
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
			} else {
				printk(KERN_DEBUG "%s: call from %s -> %s accepted\n", lp->name, nr,
				       eaz);
				/* if this interface is dialing, it does it probably on a different
				   device, so free this device */
				if (lp->dialstate == ST_OUT_WAIT_DCONN) {
#ifdef CONFIG_ISDN_PPP
					if (lp->p_encap == ISDN_NET_ENCAP_SYNCPPP)
						isdn_ppp_free(lp);
#endif
					isdn_net_lp_disconnected(lp);
					isdn_slot_free(lp->isdn_slot,
						       ISDN_USAGE_NET);
				}
				strcpy(isdn_slot_num(idx), nr);
				isdn_slot_set_usage(idx, (isdn_slot_usage(idx) & ISDN_USAGE_EXCLUSIVE) | ISDN_USAGE_NET);
				isdn_slot_set_st_netdev(idx, lp->netdev);
				lp->isdn_slot = slot;
				lp->ppp_slot = -1;
				lp->flags |= ISDN_NET_CONNECTED;
				
				lp->outgoing = 0;
				lp->huptimer = 0;
				lp->charge_state = ST_CHARGE_NULL;
				/* Got incoming Call, setup L2 and L3 protocols,
				 * then wait for D-Channel-connect
				 */
				cmd.arg = lp->l2_proto << 8;
				isdn_slot_command(lp->isdn_slot, ISDN_CMD_SETL2, &cmd);
				cmd.arg = lp->l3_proto << 8;
				isdn_slot_command(lp->isdn_slot, ISDN_CMD_SETL3, &cmd);
				
				lp->dial_timer.expires = jiffies + 15 * HZ;
				lp->dial_event = EV_NET_TIMER_IN_DCONN;
				add_timer(&lp->dial_timer);
				lp->dialstate = ST_IN_WAIT_DCONN;
				
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
	/* If none of configured EAZ/MSN matched and not verbose, be silent */
	if (!ematch || dev->net_verbose)
		printk(KERN_INFO "isdn_net: call from %s -> %d %s ignored\n", nr, slot, eaz);
	restore_flags(flags);
	return (wret == 2)?5:0;
}

/*
 * Search list of net-interfaces for an interface with given name.
 */
isdn_net_dev *
isdn_net_findif(char *name)
{
	struct list_head *l;

	list_for_each(l, &isdn_net_devs) {
		isdn_net_dev *p = list_entry(l, isdn_net_dev, global_list);
		if (!strcmp(p->local.name, name))
			return p;
	}
	return NULL;
}

/*
 * Force a net-interface to dial out.
 * This is called from the userlevel-routine below or
 * from isdn_net_start_xmit().
 */
int
isdn_net_force_dial_lp(isdn_net_local * lp)
{
	int chi;
	unsigned long flags;

	if (lp->flags & ISDN_NET_CONNECTED || lp->dialstate != ST_NULL)
		return -EBUSY;

	save_flags(flags);
	cli();
	
	/* Grab a free ISDN-Channel */
	chi = isdn_get_free_slot(ISDN_USAGE_NET, lp->l2_proto, lp->l3_proto,
				 lp->pre_device, lp->pre_channel, lp->msn);
	if (chi < 0) {
		printk(KERN_WARNING "isdn_net_force_dial: No channel for %s\n", lp->name);
		restore_flags(flags);
		return -EAGAIN;
	}
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
	init_dialout(lp);

	return 0;
}

/*
 * This is called from certain upper protocol layers (multilink ppp
 * and x25iface encapsulation module) that want to initiate dialing
 * themselves.
 */
int
isdn_net_dial_req(isdn_net_local * lp)
{
	/* is there a better error code? */
	if (!(ISDN_NET_DIALMODE(*lp) == ISDN_NET_DM_AUTO)) return -EBUSY;

	return isdn_net_force_dial_lp(lp);
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
	return (isdn_net_force_dial_lp(&p->local));
}

/*
 * Allocate a new network-interface and initialize its data structures.
 */
int
isdn_net_new(char *name, struct net_device *master)
{
	int retval;
	isdn_net_dev *netdev;

	/* Avoid creating an existing interface */
	if (isdn_net_findif(name)) {
		printk(KERN_WARNING "isdn_net: interface %s already exists\n", name);
		return -EEXIST;
	}
	if (!(netdev = kmalloc(sizeof(isdn_net_dev), GFP_KERNEL))) {
		printk(KERN_WARNING "isdn_net: Could not allocate net-device\n");
		return -ENOMEM;
	}
	memset(netdev, 0, sizeof(isdn_net_dev));
	strcpy(netdev->local.name, name);
	strcpy(netdev->dev.name, name);
	netdev->dev.priv = &netdev->local;
	netdev->dev.init = isdn_net_init;
	netdev->local.p_encap = ISDN_NET_ENCAP_RAWIP;
	if (master) {
		/* Device shall be a slave */
		struct net_device *p = (((isdn_net_local *) master->priv)->slave);
		struct net_device *q = master;

		netdev->local.master = master;
		/* Put device at end of slave-chain */
		while (p) {
			q = p;
			p = (((isdn_net_local *) p->priv)->slave);
		}
		((isdn_net_local *) q->priv)->slave = &(netdev->dev);
	} else {
		/* Device shall be a master */
		/*
		 * Watchdog timer (currently) for master only.
		 */
		netdev->dev.tx_timeout = isdn_net_tx_timeout;
		netdev->dev.watchdog_timeo = ISDN_NET_TX_TIMEOUT;
		retval = register_netdev(&netdev->dev);
		if (retval) {
			printk(KERN_WARNING "isdn_net: Could not register net-device\n");
			kfree(netdev);
			return retval;
		}
	}
	netdev->local.magic = ISDN_NET_MAGIC;

	netdev->queue = &netdev->local;
	spin_lock_init(&netdev->queue_lock);

	netdev->local.last = &netdev->local;
	netdev->local.netdev = netdev;
	netdev->local.next = &netdev->local;

	netdev->local.tqueue.sync = 0;
	netdev->local.tqueue.routine = isdn_net_softint;
	netdev->local.tqueue.data = &netdev->local;
	spin_lock_init(&netdev->local.xmit_lock);

	netdev->local.isdn_slot = -1;
	netdev->local.pre_device = -1;
	netdev->local.pre_channel = -1;
	netdev->local.exclusive = -1;
	netdev->local.ppp_slot = -1;
	netdev->local.pppbind = -1;
	skb_queue_head_init(&netdev->local.super_tx_queue);
	netdev->local.l2_proto = ISDN_PROTO_L2_X75I;
	netdev->local.l3_proto = ISDN_PROTO_L3_TRANS;
	netdev->local.triggercps = 6000;
	netdev->local.slavedelay = 10 * HZ;
	netdev->local.hupflags = ISDN_INHUP;	/* Do hangup even on incoming calls */
	netdev->local.onhtime = 10;	/* Default hangup-time for saving costs
	   of those who forget configuring this */
	netdev->local.dialmax = 1;
	netdev->local.flags = ISDN_NET_CBHUP | ISDN_NET_DM_MANUAL;	/* Hangup before Callback, manual dial */
	netdev->local.cbdelay = 5 * HZ;	/* Wait 5 secs before Callback */
	netdev->local.dialtimeout = -1;  /* Infinite Dial-Timeout */
	netdev->local.dialwait = 5 * HZ; /* Wait 5 sec. after failed dial */
	netdev->local.dialstarted = 0;   /* Jiffies of last dial-start */
	netdev->local.dialwait_timer = 0;  /* Jiffies of earliest next dial-start */

	init_timer(&netdev->local.dial_timer);
	netdev->local.dial_timer.data = (unsigned long) &netdev->local;
	netdev->local.dial_timer.function = isdn_net_dial_timer;
	init_timer(&netdev->local.hup_timer);
	netdev->local.hup_timer.data = (unsigned long) &netdev->local;
	netdev->local.hup_timer.function = isdn_net_hup_timer;
	spin_lock_init(&netdev->local.lock);
	INIT_LIST_HEAD(&netdev->local.phone[0]);
	INIT_LIST_HEAD(&netdev->local.phone[1]);

	/* Put into to netdev-chain */
	list_add(&netdev->global_list, &isdn_net_devs);
	return 0;
}

int
isdn_net_newslave(char *parm)
{
	char *p = strchr(parm, ',');
	isdn_net_dev *m;

	/* Slave-Name MUST not be empty */
	if (!p || !p[1])
		return -EINVAL;

	*p = 0;
	/* Master must already exist */
	if (!(m = isdn_net_findif(parm)))
		return -ESRCH;
	/* Master must be a real interface, not a slave */
	if (m->local.master)
		return -ENXIO;
	/* Master must not be started yet */
	if (isdn_net_device_started(m)) 
		return -EBUSY;

	return isdn_net_new(p+1, &m->dev);
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
		isdn_net_local *lp = &p->local;

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
			if (isdn_net_device_started(p)) {
				printk(KERN_WARNING "%s: cannot change encap when if is up\n",
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
			p->dev.do_ioctl = isdn_ppp_dev_ioctl;
#endif
			break;
		case ISDN_NET_ENCAP_X25IFACE:
#ifndef CONFIG_ISDN_X25
			printk(KERN_WARNING "%s: isdn-x25 support not configured\n",
			       p->local.name);
			return -EINVAL;
#else
			p->dev.type = ARPHRD_X25;	/* change ARP type */
			p->dev.addr_len = 0;
#endif
			break;
		case ISDN_NET_ENCAP_CISCOHDLCK:
			p->dev.do_ioctl = isdn_ciscohdlck_dev_ioctl;
			break;
		default:
			if( cfg->p_encap >= 0 &&
			    cfg->p_encap <= ISDN_NET_ENCAP_MAX_ENCAP )
				break;
			printk(KERN_WARNING
			       "%s: encapsulation protocol %d not supported\n",
			       p->local.name, cfg->p_encap);
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
			unsigned long flags;

			/* If binding is exclusive, try to grab the channel */
			save_flags(flags);
			if ((i = isdn_get_free_slot(ISDN_USAGE_NET,
				lp->l2_proto, lp->l3_proto, drvidx,
				chidx, lp->msn)) < 0) {
				/* Grab failed, because desired channel is in use */
				lp->exclusive = -1;
				restore_flags(flags);
				return -EBUSY;
			}
			/* All went ok, so update isdninfo */
			isdn_slot_set_usage(i, ISDN_USAGE_EXCLUSIVE);
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
		lp->cbdelay = cfg->cbdelay * HZ / 5;
		lp->dialmax = cfg->dialmax;
		lp->triggercps = cfg->triggercps;
		lp->slavedelay = cfg->slavedelay * HZ;
		lp->pppbind = cfg->pppbind;
		lp->dialtimeout = cfg->dialtimeout >= 0 ? cfg->dialtimeout * HZ : -1;
		lp->dialwait = cfg->dialwait * HZ;
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
		lp->flags &= ~ISDN_NET_DIALMODE_MASK;	/* first all bits off */
		if (cfg->dialmode && !(cfg->dialmode & ISDN_NET_DIALMODE_MASK)) {
			/* old isdnctrl version, where only 0 or 1 is given */
			printk(KERN_WARNING
			     "Old isdnctrl version detected! Please update.\n");
			lp->flags |= ISDN_NET_DM_OFF; /* turn on `off' bit */
		}
		else {
			lp->flags |= cfg->dialmode;  /* turn on selected bits */
		}
		if (lp->flags & ISDN_NET_DM_OFF)
			isdn_net_hangup(lp);

		if (cfg->chargehup)
			lp->hupflags |= ISDN_CHARGEHUP;
		else
			lp->hupflags &= ~ISDN_CHARGEHUP;
		if (cfg->ihup)
			lp->hupflags |= ISDN_INHUP;
		else
			lp->hupflags &= ~ISDN_INHUP;
		if (cfg->chargeint > 10) {
			lp->chargeint = cfg->chargeint * HZ;
			lp->charge_state = ST_CHARGE_HAVE_CINT;
			lp->hupflags |= ISDN_MANCHARGE;
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
		isdn_net_local *lp = &p->local;

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
		cfg->dialmode = lp->flags & ISDN_NET_DIALMODE_MASK;
		cfg->chargehup = (lp->hupflags & 4) ? 1 : 0;
		cfg->ihup = (lp->hupflags & 8) ? 1 : 0;
		cfg->cbdelay = lp->cbdelay * 5 / HZ;
		cfg->dialmax = lp->dialmax;
		cfg->triggercps = lp->triggercps;
		cfg->slavedelay = lp->slavedelay / HZ;
		cfg->chargeint = (lp->hupflags & ISDN_CHARGEHUP) ?
		    (lp->chargeint / HZ) : 0;
		cfg->pppbind = lp->pppbind;
		cfg->dialtimeout = lp->dialtimeout >= 0 ? lp->dialtimeout / HZ : -1;
		cfg->dialwait = lp->dialwait / HZ;
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
	unsigned long flags;
	struct isdn_net_phone *n;

	if (!p)
		return -ENODEV;

	n = kmalloc(sizeof(*n), GFP_KERNEL);
	if (!n)
		return -ENOMEM;

	strcpy(n->num, phone->phone);
	spin_lock_irqsave(&p->local.lock, flags);
	list_add_tail(&n->list, &p->local.phone[phone->outgoing & 1]);
	spin_unlock_irqrestore(&p->local.lock, flags);
	return 0;
}

/*
 * Copy a string of all phone-numbers of an interface to user space.
 * This might sleep and must be called with the isdn semaphore down.
 */
int
isdn_net_getphones(isdn_net_ioctl_phone * phone, char *phones)
{
	isdn_net_dev *p = isdn_net_findif(phone->name);
	unsigned long flags;
	int inout = phone->outgoing & 1;
	int count = 0;
	char *buf = (char *)__get_free_page(GFP_KERNEL);
	struct isdn_net_phone *n;

	if (!p)
		return -ENODEV;

	if (!buf)
		return -ENOMEM;

	inout &= 1;
	spin_lock_irqsave(&p->local.lock, flags);
	list_for_each_entry(n, &p->local.phone[inout], list) {
		strcpy(&buf[count], n->num);
		count += strlen(n->num);
		buf[count++] = ' ';
		if (count > PAGE_SIZE - ISDN_MSNLEN - 1)
			break;
	}
	spin_unlock_irqrestore(&p->local.lock, flags);
	if (!count)
		count++;

	buf[count-1] = 0;

	if (copy_to_user(phones, buf, count))
		count = -EFAULT;

	free_page((unsigned long)buf);
	return count;
}

/*
 * Copy a string containing the peer's phone number of a connected interface
 * to user space.
 */
int
isdn_net_getpeer(isdn_net_ioctl_phone *phone, isdn_net_ioctl_phone *peer)
{
	isdn_net_dev *p = isdn_net_findif(phone->name);
	int idx;

	if (!p) return -ENODEV;
	/*
	 * Theoretical race: while this executes, the remote number might
	 * become invalid (hang up) or change (new connection), resulting
         * in (partially) wrong number copied to user. This race
	 * currently ignored.
	 */
	idx = p->local.isdn_slot;
	if (idx<0) return -ENOTCONN;
	/* for pre-bound channels, we need this extra check */
	if (strncmp(isdn_slot_num(idx),"???",3) == 0 ) return -ENOTCONN;
	strncpy(phone->phone,isdn_slot_num(idx),ISDN_MSNLEN);
	phone->outgoing=USG_OUTGOING(isdn_slot_usage(idx));
	if ( copy_to_user(peer,phone,sizeof(*peer)) ) return -EFAULT;
	return 0;
}
/*
 * Delete a phone-number from an interface.
 */
int
isdn_net_delphone(isdn_net_ioctl_phone * phone)
{
	isdn_net_dev *p = isdn_net_findif(phone->name);
	int inout = phone->outgoing & 1;
	struct isdn_net_phone *n;
	unsigned long flags;
	int retval;

	if (!p)
		return -ENODEV;

	retval = -EINVAL;
	spin_lock_irqsave(&p->local.lock, flags);
	list_for_each_entry(n, &p->local.phone[inout], list) {
		if (!strcmp(n->num, phone->phone)) {
			list_del(&n->list);
			kfree(n);
			retval = 0;
			break;
		}
	}
	spin_unlock_irqrestore(&p->local.lock, flags);
	return retval;
}

/*
 * Delete all phone-numbers of an interface.
 */
static int
isdn_net_rmallphone(isdn_net_dev * p)
{
	struct isdn_net_phone *n;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&p->local.lock, flags);
	for (i = 0; i < 2; i++) {
		while (!list_empty(&p->local.phone[i])) {
			n = list_entry(p->local.phone[i].next, struct isdn_net_phone, list);
			list_del(&n->list);
			kfree(n);
		}
	}
	spin_lock_irqsave(&p->local.lock, flags);
	return 0;
}

/*
 * Force a hangup of a network-interface.
 */
int
isdn_net_force_hangup(char *name)
{
	isdn_net_dev *p = isdn_net_findif(name);
	struct net_device *q;

	if (p) {
		if (p->local.isdn_slot < 0)
			return 1;
		q = p->local.slave;
		/* If this interface has slaves, do a hangup for them also. */
		while (q) {
			isdn_net_hangup(&p->local);
			q = (((isdn_net_local *) q->priv)->slave);
		}
		isdn_net_hangup(&p->local);
		return 0;
	}
	return -ENODEV;
}

/*
 * Helper-function for isdn_net_rm: Do the real work.
 */
static int
isdn_net_realrm(isdn_net_dev *p)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	if (isdn_net_device_started(p)) {
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
	if (p->local.exclusive != -1)
		isdn_unexclusive_channel(p->local.pre_device, p->local.pre_channel);
	if (p->local.master) {
		/* It's a slave-device, so update master's slave-pointer if necessary */
		if (((isdn_net_local *) (p->local.master->priv))->slave == &p->dev)
			((isdn_net_local *) (p->local.master->priv))->slave = p->local.slave;
	} else {
		/* Unregister only if it's a master-device */
		p->dev.hard_header_cache = p->local.org_hhc;
		p->dev.header_cache_update = p->local.org_hcu;
		unregister_netdev(&p->dev);
	}
	/* Unlink device from chain */
	list_del(&p->global_list);
	if (p->local.slave) {
		/* If this interface has a slave, remove it also */
		char *slavename = ((isdn_net_local *) (p->local.slave->priv))->name;
		struct list_head *l;

		list_for_each(l, &isdn_net_devs) {
			isdn_net_dev *n = list_entry(l, isdn_net_dev, global_list);
			if (!strcmp(n->local.name, slavename)) {
				isdn_net_realrm(n);
				break;
			}
		}
	}
	restore_flags(flags);
	kfree(p);

	return 0;
}

/*
 * Remove a single network-interface.
 */
int
isdn_net_rm(char *name)
{
	struct list_head *l;

	/* Search name in netdev-chain */
	list_for_each(l, &isdn_net_devs) {
		isdn_net_dev *p = list_entry(l, isdn_net_dev, global_list);
		if (!strcmp(p->local.name, name))
			return isdn_net_realrm(p);
	}
	return -ENODEV;
}

/*
 * Remove all network-interfaces
 */
int
isdn_net_rmall(void)
{
	unsigned long flags;
	int ret;

	/* Walk through netdev-chain */
	save_flags(flags);
	cli();
	while (!list_empty(&isdn_net_devs)) {
		isdn_net_dev *p = list_entry(isdn_net_devs.next, isdn_net_dev, global_list);

		/* Remove master-devices only, slaves get removed with their master */
		if (!p->local.master) {
			if ((ret = isdn_net_realrm(p))) {
				restore_flags(flags);
				return ret;
			}
		}
	}
	restore_flags(flags);
	return 0;
}
