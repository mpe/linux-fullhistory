/*
 *              Device round robin policy for multipath.
 *
 *
 * Version:	$Id: multipath_drr.c,v 1.1.2.1 2004/09/16 07:42:34 elueck Exp $
 *
 * Authors:	Einar Lueck <elueck@de.ibm.com><lkml@einar-lueck.de>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/igmp.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/mroute.h>
#include <linux/init.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/icmp.h>
#include <net/udp.h>
#include <net/raw.h>
#include <linux/notifier.h>
#include <linux/if_arp.h>
#include <linux/netfilter_ipv4.h>
#include <net/ipip.h>
#include <net/checksum.h>
#include <net/ip_mp_alg.h>

struct multipath_device {
	int		ifi; /* interface index of device */
	atomic_t	usecount;
	int 		allocated;
};

#define MULTIPATH_MAX_DEVICECANDIDATES 10

static struct multipath_device state[MULTIPATH_MAX_DEVICECANDIDATES];
static spinlock_t state_lock = SPIN_LOCK_UNLOCKED;
static int registered_dev_notifier = 0;
static struct rtable *last_selection = NULL;

#define RTprint(a...)	// printk(KERN_DEBUG a)

static int inline __multipath_findslot(void)
{
	int i;

	for (i = 0; i < MULTIPATH_MAX_DEVICECANDIDATES; i++) {
		if (state[i].allocated == 0)
			return i;
	}
	return -1;
}

static int inline __multipath_finddev(int ifindex)
{
	int i;

	for (i = 0; i < MULTIPATH_MAX_DEVICECANDIDATES; i++) {
		if (state[i].allocated != 0 &&
		    state[i].ifi == ifindex)
			return i;
	}
	return -1;
}

static int multipath_dev_event(struct notifier_block *this,
			       unsigned long event, void *ptr)
{
	struct net_device *dev = ptr;
	int devidx;

	switch (event) {
	case NETDEV_UNREGISTER:
	case NETDEV_DOWN:
		spin_lock_bh(&state_lock);

		devidx = __multipath_finddev(dev->ifindex);
		if (devidx != -1) {
			state[devidx].allocated = 0;
			state[devidx].ifi = 0;
			atomic_set(&state[devidx].usecount, 0);
			RTprint(KERN_DEBUG"%s: successfully removed device " \
				"with index %d\n",__FUNCTION__, devidx);
		} else {
			RTprint(KERN_DEBUG"%s: Device not relevant for " \
				" multipath: %d\n",
				__FUNCTION__, devidx);
		}

		spin_unlock_bh(&state_lock);
		break;
	};

	return NOTIFY_DONE;
}

struct notifier_block multipath_dev_notifier = {
	.notifier_call	= multipath_dev_event,
};

void __multipath_remove(struct rtable *rt)
{
	if (last_selection == rt)
		last_selection = NULL;
}

void __multipath_safe_inc(atomic_t *usecount)
{
	int n;

	atomic_inc(usecount);

	n = atomic_read(usecount);
	if (n <= 0) {
		int i;

		RTprint("%s: detected overflow, now ill will reset all "\
			"usecounts\n", __FUNCTION__);

		spin_lock_bh(&state_lock);

		for (i = 0; i < MULTIPATH_MAX_DEVICECANDIDATES; i++)
			atomic_set(&state[i].usecount, 0);

		spin_unlock_bh(&state_lock);
	}
}

void __multipath_selectroute(const struct flowi *flp,
			     struct rtable *first, struct rtable **rp)
{
	struct rtable *nh, *result, *cur_min;
	int min_usecount = -1; 
	int devidx = -1;
	int cur_min_devidx = -1;

	/* register a notifier to stay informed about dying devices */
	if (!registered_dev_notifier) {
		registered_dev_notifier = 1;
		register_netdevice_notifier(&multipath_dev_notifier);
	}

       	/* if necessary and possible utilize the old alternative */
	if ((flp->flags & FLOWI_FLAG_MULTIPATHOLDROUTE) != 0 &&
	    last_selection != NULL) {
		RTprint( KERN_CRIT"%s: holding route \n", __FUNCTION__ );
		result = last_selection;
		*rp = result;
		return;
	}

	/* 1. make sure all alt. nexthops have the same GC related data */
	/* 2. determine the new candidate to be returned */
	result = NULL;
	cur_min = NULL;
	for (nh = rcu_dereference(first); nh;
	     nh = rcu_dereference(nh->u.rt_next)) {
		if ((nh->u.dst.flags & DST_BALANCED) != 0 &&
		    multipath_comparekeys(&nh->fl, flp)) {
			int nh_ifidx = nh->u.dst.dev->ifindex;

			nh->u.dst.lastuse = jiffies;
			nh->u.dst.__use++;
			if (result != NULL)
				continue;

			/* search for the output interface */

			/* this is not SMP safe, only add/remove are
			 * SMP safe as wrong usecount updates have no big
			 * impact
			 */
			devidx = __multipath_finddev(nh_ifidx);
			if (devidx == -1) {
				/* add the interface to the array 
				 * SMP safe
				 */
				spin_lock_bh(&state_lock);

				/* due to SMP: search again */
				devidx = __multipath_finddev(nh_ifidx);
				if (devidx == -1) {
					/* add entry for device */
					devidx = __multipath_findslot();
					if (devidx == -1) {
						/* unlikely but possible */
						RTprint(KERN_DEBUG"%s: " \
							"out of space\n",
							__FUNCTION__);
						continue;
					}

					state[devidx].allocated = 1;
					state[devidx].ifi = nh_ifidx;
					atomic_set(&state[devidx].usecount, 0);
					min_usecount = 0;
					RTprint(KERN_DEBUG"%s: created " \
						" for " \
						"device %d and " \
						"min_usecount " \
						" == -1\n",
						__FUNCTION__,
						nh_ifidx);
				}

				spin_unlock_bh(&state_lock);
			}

			if (min_usecount == 0) {
				/* if the device has not been used it is
				 * the primary target
				 */
				RTprint(KERN_DEBUG"%s: now setting " \
					"result to device %d\n",
					__FUNCTION__, nh_ifidx );

				__multipath_safe_inc(&state[devidx].usecount);
				result = nh;
			} else {
				int count =
					atomic_read(&state[devidx].usecount);

				if (min_usecount == -1 ||
				    count < min_usecount) {
					cur_min = nh;
					cur_min_devidx = devidx;
					min_usecount = count;

					RTprint(KERN_DEBUG"%s: found " \
						"device " \
						"%d with usecount == %d\n",
						__FUNCTION__, 
						nh_ifidx,
						min_usecount);
				}
			}
		}
	}

	if (!result) {
		if (cur_min) {
			RTprint( KERN_DEBUG"%s: index of device in state "\
				 "array: %d\n",
				 __FUNCTION__, cur_min_devidx );
			__multipath_safe_inc(&state[cur_min_devidx].usecount);
			result = cur_min;
		} else {
			RTprint( KERN_DEBUG"%s: utilized first\n",
				 __FUNCTION__);
			result = first;
		}
	} else {
		RTprint(KERN_DEBUG"%s: utilize result: found device " \
			"%d with usecount == %d\n",
			__FUNCTION__, result->u.dst.dev->ifindex,
			min_usecount);

	}

	*rp = result;
	last_selection = result;
}
