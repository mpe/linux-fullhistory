/*
 *	Ioctl handler
 *	Linux ethernet bridge
 *
 *	Authors:
 *	Lennert Buytenhek		<buytenh@gnu.org>
 *
 *	$Id: br_ioctl.c,v 1.4 2000/11/08 05:16:40 davem Exp $
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/if_bridge.h>
#include <linux/inetdevice.h>
#include <asm/uaccess.h>
#include "br_private.h"

/* import values in USER_HZ  */
static inline unsigned long user_to_ticks(unsigned long utick)
{
	return (utick * HZ) / USER_HZ;
}

/* export values in USER_HZ */
static inline unsigned long ticks_to_user(unsigned long tick)
{
	return (tick * USER_HZ) / HZ;
}

/* Report time remaining in user HZ  */
static unsigned long timer_residue(const struct timer_list *timer)
{
	return ticks_to_user(timer_pending(timer) 
			     ? (timer->expires - jiffies) : 0);
}

static int br_ioctl_device(struct net_bridge *br,
			   unsigned int cmd,
			   unsigned long arg0,
			   unsigned long arg1,
			   unsigned long arg2)
{
	if (br == NULL)
		return -EINVAL;

	switch (cmd)
	{
	case BRCTL_ADD_IF:
	case BRCTL_DEL_IF:
	{
		struct net_device *dev;
		int ret;

		dev = dev_get_by_index(arg0);
		if (dev == NULL)
			return -EINVAL;

		spin_lock_bh(&br->lock);
		if (cmd == BRCTL_ADD_IF)
			ret = br_add_if(br, dev);
		else
			ret = br_del_if(br, dev);
		spin_unlock_bh(&br->lock);

		dev_put(dev);
		return ret;
	}

	case BRCTL_GET_BRIDGE_INFO:
	{
		struct __bridge_info b;

		memset(&b, 0, sizeof(struct __bridge_info));
		rcu_read_lock();
		memcpy(&b.designated_root, &br->designated_root, 8);
		memcpy(&b.bridge_id, &br->bridge_id, 8);
		b.root_path_cost = br->root_path_cost;
		b.max_age = ticks_to_user(br->max_age);
		b.hello_time = ticks_to_user(br->hello_time);
		b.forward_delay = br->forward_delay;
		b.bridge_max_age = br->bridge_max_age;
		b.bridge_hello_time = br->bridge_hello_time;
		b.bridge_forward_delay = ticks_to_user(br->bridge_forward_delay);
		b.topology_change = br->topology_change;
		b.topology_change_detected = br->topology_change_detected;
		b.root_port = br->root_port;
		b.stp_enabled = br->stp_enabled;
		b.ageing_time = ticks_to_user(br->ageing_time);
		b.hello_timer_value = timer_residue(&br->hello_timer);
		b.tcn_timer_value = timer_residue(&br->tcn_timer);
		b.topology_change_timer_value = timer_residue(&br->topology_change_timer);
		b.gc_timer_value = timer_residue(&br->gc_timer);
	        rcu_read_unlock();

		if (copy_to_user((void *)arg0, &b, sizeof(b)))
			return -EFAULT;

		return 0;
	}

	case BRCTL_GET_PORT_LIST:
	{
		int *indices;
		int ret = 0;

		indices = kmalloc(256*sizeof(int), GFP_KERNEL);
		if (indices == NULL)
			return -ENOMEM;

		memset(indices, 0, 256*sizeof(int));

		br_get_port_ifindices(br, indices);
		if (copy_to_user((void *)arg0, indices, 256*sizeof(int)))
			ret =  -EFAULT;
		kfree(indices);
		return ret;
	}

	case BRCTL_SET_BRIDGE_FORWARD_DELAY:
		spin_lock_bh(&br->lock);
		br->bridge_forward_delay = user_to_ticks(arg0);
		if (br_is_root_bridge(br))
			br->forward_delay = br->bridge_forward_delay;
		spin_unlock_bh(&br->lock);
		return 0;

	case BRCTL_SET_BRIDGE_HELLO_TIME:
		spin_lock_bh(&br->lock);
		br->bridge_hello_time = user_to_ticks(arg0);
		if (br_is_root_bridge(br))
			br->hello_time = br->bridge_hello_time;
		spin_unlock_bh(&br->lock);
		return 0;

	case BRCTL_SET_BRIDGE_MAX_AGE:
		spin_lock_bh(&br->lock);
		br->bridge_max_age = user_to_ticks(arg0);
		if (br_is_root_bridge(br))
			br->max_age = br->bridge_max_age;
		spin_unlock_bh(&br->lock);
		return 0;

	case BRCTL_SET_AGEING_TIME:
		br->ageing_time = user_to_ticks(arg0);
		return 0;

	case BRCTL_SET_GC_INTERVAL:	 /* no longer used */
		return 0;

	case BRCTL_GET_PORT_INFO:
	{
		struct __port_info p;
		struct net_bridge_port *pt;

		rcu_read_lock();
		if ((pt = br_get_port(br, arg1)) == NULL) {
			rcu_read_unlock();
			return -EINVAL;
		}

		memset(&p, 0, sizeof(struct __port_info));
		memcpy(&p.designated_root, &pt->designated_root, 8);
		memcpy(&p.designated_bridge, &pt->designated_bridge, 8);
		p.port_id = pt->port_id;
		p.designated_port = pt->designated_port;
		p.path_cost = pt->path_cost;
		p.designated_cost = pt->designated_cost;
		p.state = pt->state;
		p.top_change_ack = pt->topology_change_ack;
		p.config_pending = pt->config_pending;
		p.message_age_timer_value = timer_residue(&pt->message_age_timer);
		p.forward_delay_timer_value = timer_residue(&pt->forward_delay_timer);
		p.hold_timer_value = timer_residue(&pt->hold_timer);

		rcu_read_unlock();

		if (copy_to_user((void *)arg0, &p, sizeof(p)))
			return -EFAULT;

		return 0;
	}

	case BRCTL_SET_BRIDGE_STP_STATE:
		br->stp_enabled = arg0?1:0;
		return 0;

	case BRCTL_SET_BRIDGE_PRIORITY:
		spin_lock_bh(&br->lock);
		br_stp_set_bridge_priority(br, arg0);
		spin_unlock_bh(&br->lock);
		return 0;

	case BRCTL_SET_PORT_PRIORITY:
	{
		struct net_bridge_port *p;
		int ret = 0;

		spin_lock_bh(&br->lock);
		if ((p = br_get_port(br, arg0)) == NULL) 
			ret = -EINVAL;
		else
			br_stp_set_port_priority(p, arg1);
		spin_unlock_bh(&br->lock);
		return ret;
	}

	case BRCTL_SET_PATH_COST:
	{
		struct net_bridge_port *p;
		int ret = 0;

		spin_lock_bh(&br->lock);
		if ((p = br_get_port(br, arg0)) == NULL)
			ret = -EINVAL;
		else
			br_stp_set_path_cost(p, arg1);
		spin_unlock_bh(&br->lock);
		return ret;
	}

	case BRCTL_GET_FDB_ENTRIES:
		return br_fdb_get_entries(br, (void *)arg0, arg1, arg2);
	}

	return -EOPNOTSUPP;
}

static int br_ioctl_deviceless(unsigned int cmd,
			       unsigned long arg0,
			       unsigned long arg1)
{
	switch (cmd)
	{
	case BRCTL_GET_VERSION:
		return BRCTL_VERSION;

	case BRCTL_GET_BRIDGES:
	{
		int *indices;
		int ret = 0;

		if (arg1 > 64)
			arg1 = 64;

		indices = kmalloc(arg1*sizeof(int), GFP_KERNEL);
		if (indices == NULL)
			return -ENOMEM;

		memset(indices, 0, arg1*sizeof(int));
		arg1 = br_get_bridge_ifindices(indices, arg1);

		ret = copy_to_user((void *)arg0, indices, arg1*sizeof(int))
			? -EFAULT : arg1;

		kfree(indices);
		return ret;
	}

	case BRCTL_ADD_BRIDGE:
	case BRCTL_DEL_BRIDGE:
	{
		char buf[IFNAMSIZ];

		if (copy_from_user(buf, (void *)arg0, IFNAMSIZ))
			return -EFAULT;

		buf[IFNAMSIZ-1] = 0;

		if (cmd == BRCTL_ADD_BRIDGE)
			return br_add_bridge(buf);

		return br_del_bridge(buf);
	}
	}

	return -EOPNOTSUPP;
}


int br_ioctl_deviceless_stub(unsigned long arg)
{
	unsigned long i[3];

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (copy_from_user(i, (void *)arg, 3*sizeof(unsigned long)))
		return -EFAULT;

	return br_ioctl_deviceless(i[0], i[1], i[2]);
}

int br_ioctl(struct net_bridge *br, unsigned int cmd, unsigned long arg0, unsigned long arg1, unsigned long arg2)
{
	int err;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	err = br_ioctl_deviceless(cmd, arg0, arg1);
	if (err == -EOPNOTSUPP)
		err = br_ioctl_device(br, cmd, arg0, arg1, arg2);

	return err;
}
