/*
 * 	NET3	Protocol independent device support routines.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	Derived from the non IP parts of dev.c 1.0.19
 * 		Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *				Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *				Mark Evans, <evansmp@uhura.aston.ac.uk>
 *
 *	Additional Authors:
 *		Florian la Roche <rzsfl@rz.uni-sb.de>
 *		Alan Cox <gw4pts@gw4pts.ampr.org>
 *		David Hinds <dahinds@users.sourceforge.net>
 *		Alexey Kuznetsov <kuznet@ms2.inr.ac.ru>
 *		Adam Sulmicki <adam@cfar.umd.edu>
 *              Pekka Riikonen <priikone@poesidon.pspt.fi>
 *
 *	Changes:
 *              D.J. Barrow     :       Fixed bug where dev->refcnt gets set
 *              			to 2 if register_netdev gets called
 *              			before net_dev_init & also removed a
 *              			few lines of code in the process.
 *		Alan Cox	:	device private ioctl copies fields back.
 *		Alan Cox	:	Transmit queue code does relevant
 *					stunts to keep the queue safe.
 *		Alan Cox	:	Fixed double lock.
 *		Alan Cox	:	Fixed promisc NULL pointer trap
 *		????????	:	Support the full private ioctl range
 *		Alan Cox	:	Moved ioctl permission check into
 *					drivers
 *		Tim Kordas	:	SIOCADDMULTI/SIOCDELMULTI
 *		Alan Cox	:	100 backlog just doesn't cut it when
 *					you start doing multicast video 8)
 *		Alan Cox	:	Rewrote net_bh and list manager.
 *		Alan Cox	: 	Fix ETH_P_ALL echoback lengths.
 *		Alan Cox	:	Took out transmit every packet pass
 *					Saved a few bytes in the ioctl handler
 *		Alan Cox	:	Network driver sets packet type before
 *					calling netif_rx. Saves a function
 *					call a packet.
 *		Alan Cox	:	Hashed net_bh()
 *		Richard Kooijman:	Timestamp fixes.
 *		Alan Cox	:	Wrong field in SIOCGIFDSTADDR
 *		Alan Cox	:	Device lock protection.
 *		Alan Cox	: 	Fixed nasty side effect of device close
 *					changes.
 *		Rudi Cilibrasi	:	Pass the right thing to
 *					set_mac_address()
 *		Dave Miller	:	32bit quantity for the device lock to
 *					make it work out on a Sparc.
 *		Bjorn Ekwall	:	Added KERNELD hack.
 *		Alan Cox	:	Cleaned up the backlog initialise.
 *		Craig Metz	:	SIOCGIFCONF fix if space for under
 *					1 device.
 *	    Thomas Bogendoerfer :	Return ENODEV for dev_open, if there
 *					is no device open function.
 *		Andi Kleen	:	Fix error reporting for SIOCGIFCONF
 *	    Michael Chastain	:	Fix signed/unsigned for SIOCGIFCONF
 *		Cyrus Durgin	:	Cleaned for KMOD
 *		Adam Sulmicki   :	Bug Fix : Network Device Unload
 *					A network device unload needs to purge
 *					the backlog queue.
 *	Paul Rusty Russell	:	SIOCSIFNAME
 *              Pekka Riikonen  :	Netdev boot-time settings code
 *              Andrew Morton   :       Make unregister_netdevice wait
 *              			indefinitely on dev->refcnt
 * 		J Hadi Salim	:	- Backlog queue sampling
 *				        - netif_rx() feedback
 */

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/if_ether.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/notifier.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <linux/rtnetlink.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/stat.h>
#include <linux/if_bridge.h>
#include <linux/divert.h>
#include <net/dst.h>
#include <net/pkt_sched.h>
#include <net/checksum.h>
#include <linux/highmem.h>
#include <linux/init.h>
#include <linux/kmod.h>
#include <linux/module.h>
#include <linux/kallsyms.h>
#ifdef CONFIG_NET_RADIO
#include <linux/wireless.h>		/* Note : will define WIRELESS_EXT */
#include <net/iw_handler.h>
#endif	/* CONFIG_NET_RADIO */
#include <asm/current.h>

/* This define, if set, will randomly drop a packet when congestion
 * is more than moderate.  It helps fairness in the multi-interface
 * case when one of them is a hog, but it kills performance for the
 * single interface case so it is off now by default.
 */
#undef RAND_LIE

/* Setting this will sample the queue lengths and thus congestion
 * via a timer instead of as each packet is received.
 */
#undef OFFLINE_SAMPLE

/*
 *	The list of packet types we will receive (as opposed to discard)
 *	and the routines to invoke.
 *
 *	Why 16. Because with 16 the only overlap we get on a hash of the
 *	low nibble of the protocol value is RARP/SNAP/X.25.
 *
 *      NOTE:  That is no longer true with the addition of VLAN tags.  Not
 *             sure which should go first, but I bet it won't make much
 *             difference if we are running VLANs.  The good news is that
 *             this protocol won't be in the list unless compiled in, so
 *             the average user (w/out VLANs) will not be adversly affected.
 *             --BLG
 *
 *		0800	IP
 *		8100    802.1Q VLAN
 *		0001	802.3
 *		0002	AX.25
 *		0004	802.2
 *		8035	RARP
 *		0005	SNAP
 *		0805	X.25
 *		0806	ARP
 *		8137	IPX
 *		0009	Localtalk
 *		86DD	IPv6
 */

static spinlock_t ptype_lock = SPIN_LOCK_UNLOCKED;
static struct list_head ptype_base[16];	/* 16 way hashed list */
static struct list_head ptype_all;		/* Taps */

#ifdef OFFLINE_SAMPLE
static void sample_queue(unsigned long dummy);
static struct timer_list samp_timer = TIMER_INITIALIZER(sample_queue, 0, 0);
#endif

/*
 *	Our notifier list
 */

static struct notifier_block *netdev_chain;

/*
 *	Device drivers call our routines to queue packets here. We empty the
 *	queue in the local softnet handler.
 */
DEFINE_PER_CPU(struct softnet_data, softnet_data) = { 0, };

#ifdef CONFIG_NET_FASTROUTE
int netdev_fastroute;
int netdev_fastroute_obstacles;
#endif

extern int netdev_sysfs_init(void);
extern int netdev_register_sysfs(struct net_device *);
extern int netdev_unregister_sysfs(struct net_device *);


/*******************************************************************************

		Protocol management and registration routines

*******************************************************************************/

/*
 *	For efficiency
 */

int netdev_nit;

/*
 *	Add a protocol ID to the list. Now that the input handler is
 *	smarter we can dispense with all the messy stuff that used to be
 *	here.
 *
 *	BEWARE!!! Protocol handlers, mangling input packets,
 *	MUST BE last in hash buckets and checking protocol handlers
 *	MUST start from promiscuous ptype_all chain in net_bh.
 *	It is true now, do not change it.
 *	Explanation follows: if protocol handler, mangling packet, will
 *	be the first on list, it is not able to sense, that packet
 *	is cloned and should be copied-on-write, so that it will
 *	change it and subsequent readers will get broken packet.
 *							--ANK (980803)
 */

/**
 *	dev_add_pack - add packet handler
 *	@pt: packet type declaration
 *
 *	Add a protocol handler to the networking stack. The passed &packet_type
 *	is linked into kernel lists and may not be freed until it has been
 *	removed from the kernel lists.
 *
 *	This call does not sleep therefore it can not 
 *	guarantee all CPU's that are in middle of receiving packets
 *	will see the new packet type (until the next received packet).
 */

void dev_add_pack(struct packet_type *pt)
{
	int hash;

	spin_lock_bh(&ptype_lock);
#ifdef CONFIG_NET_FASTROUTE
	if (pt->af_packet_priv) {
		netdev_fastroute_obstacles++;
		dev_clear_fastroute(pt->dev);
	}
#endif
	if (pt->type == htons(ETH_P_ALL)) {
		netdev_nit++;
		list_add_rcu(&pt->list, &ptype_all);
	} else {
		hash = ntohs(pt->type) & 15;
		list_add_rcu(&pt->list, &ptype_base[hash]);
	}
	spin_unlock_bh(&ptype_lock);
}

extern void linkwatch_run_queue(void);



/**
 *	__dev_remove_pack	 - remove packet handler
 *	@pt: packet type declaration
 *
 *	Remove a protocol handler that was previously added to the kernel
 *	protocol handlers by dev_add_pack(). The passed &packet_type is removed
 *	from the kernel lists and can be freed or reused once this function
 *	returns. 
 *
 *      The packet type might still be in use by receivers
 *	and must not be freed until after all the CPU's have gone
 *	through a quiescent state.
 */
void __dev_remove_pack(struct packet_type *pt)
{
	struct list_head *head;
	struct packet_type *pt1;

	spin_lock_bh(&ptype_lock);

	if (pt->type == htons(ETH_P_ALL)) {
		netdev_nit--;
		head = &ptype_all;
	} else
		head = &ptype_base[ntohs(pt->type) & 15];

	list_for_each_entry(pt1, head, list) {
		if (pt == pt1) {
#ifdef CONFIG_NET_FASTROUTE
			if (pt->af_packet_priv)
				netdev_fastroute_obstacles--;
#endif
			list_del_rcu(&pt->list);
			goto out;
		}
	}

	printk(KERN_WARNING "dev_remove_pack: %p not found.\n", pt);
out:
	spin_unlock_bh(&ptype_lock);
}
/**
 *	dev_remove_pack	 - remove packet handler
 *	@pt: packet type declaration
 *
 *	Remove a protocol handler that was previously added to the kernel
 *	protocol handlers by dev_add_pack(). The passed &packet_type is removed
 *	from the kernel lists and can be freed or reused once this function
 *	returns.
 *
 *	This call sleeps to guarantee that no CPU is looking at the packet
 *	type after return.
 */
void dev_remove_pack(struct packet_type *pt)
{
	__dev_remove_pack(pt);
	
	synchronize_net();
}

/******************************************************************************

		      Device Boot-time Settings Routines

*******************************************************************************/

/* Boot time configuration table */
static struct netdev_boot_setup dev_boot_setup[NETDEV_BOOT_SETUP_MAX];

/**
 *	netdev_boot_setup_add	- add new setup entry
 *	@name: name of the device
 *	@map: configured settings for the device
 *
 *	Adds new setup entry to the dev_boot_setup list.  The function
 *	returns 0 on error and 1 on success.  This is a generic routine to
 *	all netdevices.
 */
int netdev_boot_setup_add(char *name, struct ifmap *map)
{
	struct netdev_boot_setup *s;
	int i;

	s = dev_boot_setup;
	for (i = 0; i < NETDEV_BOOT_SETUP_MAX; i++) {
		if (s[i].name[0] == '\0' || s[i].name[0] == ' ') {
			memset(s[i].name, 0, sizeof(s[i].name));
			strcpy(s[i].name, name);
			memcpy(&s[i].map, map, sizeof(s[i].map));
			break;
		}
	}

	return i >= NETDEV_BOOT_SETUP_MAX ? 0 : 1;
}

/**
 *	netdev_boot_setup_check	- check boot time settings
 *	@dev: the netdevice
 *
 * 	Check boot time settings for the device.
 *	The found settings are set for the device to be used
 *	later in the device probing.
 *	Returns 0 if no settings found, 1 if they are.
 */
int netdev_boot_setup_check(struct net_device *dev)
{
	struct netdev_boot_setup *s = dev_boot_setup;
	int i;

	for (i = 0; i < NETDEV_BOOT_SETUP_MAX; i++) {
		if (s[i].name[0] != '\0' && s[i].name[0] != ' ' &&
		    !strncmp(dev->name, s[i].name, strlen(s[i].name))) {
			dev->irq 	= s[i].map.irq;
			dev->base_addr 	= s[i].map.base_addr;
			dev->mem_start 	= s[i].map.mem_start;
			dev->mem_end 	= s[i].map.mem_end;
			return 1;
		}
	}
	return 0;
}

/*
 * Saves at boot time configured settings for any netdevice.
 */
int __init netdev_boot_setup(char *str)
{
	int ints[5];
	struct ifmap map;

	str = get_options(str, ARRAY_SIZE(ints), ints);
	if (!str || !*str)
		return 0;

	/* Save settings */
	memset(&map, 0, sizeof(map));
	if (ints[0] > 0)
		map.irq = ints[1];
	if (ints[0] > 1)
		map.base_addr = ints[2];
	if (ints[0] > 2)
		map.mem_start = ints[3];
	if (ints[0] > 3)
		map.mem_end = ints[4];

	/* Add new entry to the list */
	return netdev_boot_setup_add(str, &map);
}

__setup("netdev=", netdev_boot_setup);

/*******************************************************************************

			    Device Interface Subroutines

*******************************************************************************/

/**
 *	__dev_get_by_name	- find a device by its name
 *	@name: name to find
 *
 *	Find an interface by name. Must be called under RTNL semaphore
 *	or @dev_base_lock. If the name is found a pointer to the device
 *	is returned. If the name is not found then %NULL is returned. The
 *	reference counters are not incremented so the caller must be
 *	careful with locks.
 */

struct net_device *__dev_get_by_name(const char *name)
{
	struct net_device *dev;

	for (dev = dev_base; dev; dev = dev->next)
		if (!strncmp(dev->name, name, IFNAMSIZ))
			break;
	return dev;
}

/**
 *	dev_get_by_name		- find a device by its name
 *	@name: name to find
 *
 *	Find an interface by name. This can be called from any
 *	context and does its own locking. The returned handle has
 *	the usage count incremented and the caller must use dev_put() to
 *	release it when it is no longer needed. %NULL is returned if no
 *	matching device is found.
 */

struct net_device *dev_get_by_name(const char *name)
{
	struct net_device *dev;

	read_lock(&dev_base_lock);
	dev = __dev_get_by_name(name);
	if (dev)
		dev_hold(dev);
	read_unlock(&dev_base_lock);
	return dev;
}

/*
   Return value is changed to int to prevent illegal usage in future.
   It is still legal to use to check for device existence.

   User should understand, that the result returned by this function
   is meaningless, if it was not issued under rtnl semaphore.
 */

/**
 *	dev_get	-	test if a device exists
 *	@name:	name to test for
 *
 *	Test if a name exists. Returns true if the name is found. In order
 *	to be sure the name is not allocated or removed during the test the
 *	caller must hold the rtnl semaphore.
 *
 *	This function exists only for back compatibility with older
 *	drivers.
 */
int __dev_get(const char *name)
{
	struct net_device *dev;

	read_lock(&dev_base_lock);
	dev = __dev_get_by_name(name);
	read_unlock(&dev_base_lock);
	return dev != NULL;
}

/**
 *	__dev_get_by_index - find a device by its ifindex
 *	@ifindex: index of device
 *
 *	Search for an interface by index. Returns %NULL if the device
 *	is not found or a pointer to the device. The device has not
 *	had its reference counter increased so the caller must be careful
 *	about locking. The caller must hold either the RTNL semaphore
 *	or @dev_base_lock.
 */

struct net_device *__dev_get_by_index(int ifindex)
{
	struct net_device *dev;

	for (dev = dev_base; dev; dev = dev->next)
		if (dev->ifindex == ifindex)
			break;
	return dev;
}


/**
 *	dev_get_by_index - find a device by its ifindex
 *	@ifindex: index of device
 *
 *	Search for an interface by index. Returns NULL if the device
 *	is not found or a pointer to the device. The device returned has
 *	had a reference added and the pointer is safe until the user calls
 *	dev_put to indicate they have finished with it.
 */

struct net_device *dev_get_by_index(int ifindex)
{
	struct net_device *dev;

	read_lock(&dev_base_lock);
	dev = __dev_get_by_index(ifindex);
	if (dev)
		dev_hold(dev);
	read_unlock(&dev_base_lock);
	return dev;
}

/**
 *	dev_getbyhwaddr - find a device by its hardware address
 *	@type: media type of device
 *	@ha: hardware address
 *
 *	Search for an interface by MAC address. Returns NULL if the device
 *	is not found or a pointer to the device. The caller must hold the
 *	rtnl semaphore. The returned device has not had its ref count increased
 *	and the caller must therefore be careful about locking
 *
 *	BUGS:
 *	If the API was consistent this would be __dev_get_by_hwaddr
 */

struct net_device *dev_getbyhwaddr(unsigned short type, char *ha)
{
	struct net_device *dev;

	ASSERT_RTNL();

	for (dev = dev_base; dev; dev = dev->next)
		if (dev->type == type &&
		    !memcmp(dev->dev_addr, ha, dev->addr_len))
			break;
	return dev;
}

struct net_device *__dev_getfirstbyhwtype(unsigned short type)
{
	struct net_device *dev;

	for (dev = dev_base; dev; dev = dev->next)
		if (dev->type == type)
			break;
	return dev;
}

EXPORT_SYMBOL(__dev_getfirstbyhwtype);

struct net_device *dev_getfirstbyhwtype(unsigned short type)
{
	struct net_device *dev;

	rtnl_lock();
	dev = __dev_getfirstbyhwtype(type);
	if (dev)
		dev_hold(dev);
	rtnl_unlock();
	return dev;
}

EXPORT_SYMBOL(dev_getfirstbyhwtype);

/**
 *	dev_get_by_flags - find any device with given flags
 *	@if_flags: IFF_* values
 *	@mask: bitmask of bits in if_flags to check
 *
 *	Search for any interface with the given flags. Returns NULL if a device
 *	is not found or a pointer to the device. The device returned has 
 *	had a reference added and the pointer is safe until the user calls
 *	dev_put to indicate they have finished with it.
 */

struct net_device * dev_get_by_flags(unsigned short if_flags, unsigned short mask)
{
	struct net_device *dev;

	read_lock(&dev_base_lock);
	dev = __dev_get_by_flags(if_flags, mask);
	if (dev)
		dev_hold(dev);
	read_unlock(&dev_base_lock);
	return dev;
}

/**
 *	__dev_get_by_flags - find any device with given flags
 *	@if_flags: IFF_* values
 *	@mask: bitmask of bits in if_flags to check
 *
 *	Search for any interface with the given flags. Returns NULL if a device
 *	is not found or a pointer to the device. The caller must hold either
 *	the RTNL semaphore or @dev_base_lock.
 */

struct net_device *__dev_get_by_flags(unsigned short if_flags, unsigned short mask)
{
	struct net_device *dev;

	for (dev = dev_base; dev != NULL; dev = dev->next) {
		if (((dev->flags ^ if_flags) & mask) == 0)
			return dev;
	}
	return NULL;
}

/**
 *	dev_valid_name - check if name is okay for network device
 *	@name: name string
 *
 *	Network device names need to be valid file names to
 *	to allow sysfs to work
 */
int dev_valid_name(const char *name)
{
	return !(*name == '\0' 
		 || !strcmp(name, ".")
		 || !strcmp(name, "..")
		 || strchr(name, '/'));
}

/**
 *	dev_alloc_name - allocate a name for a device
 *	@dev: device
 *	@name: name format string
 *
 *	Passed a format string - eg "lt%d" it will try and find a suitable
 *	id. Not efficient for many devices, not called a lot. The caller
 *	must hold the dev_base or rtnl lock while allocating the name and
 *	adding the device in order to avoid duplicates. Returns the number
 *	of the unit assigned or a negative errno code.
 */

int dev_alloc_name(struct net_device *dev, const char *name)
{
	int i;
	char buf[32];
	char *p;

	/*
	 * Verify the string as this thing may have come from
	 * the user.  There must be either one "%d" and no other "%"
	 * characters, or no "%" characters at all.
	 */
	p = strchr(name, '%');
	if (p && (p[1] != 'd' || strchr(p + 2, '%')))
		return -EINVAL;

	/*
	 * If you need over 100 please also fix the algorithm...
	 */
	for (i = 0; i < 100; i++) {
		snprintf(buf, sizeof(buf), name, i);
		if (!__dev_get_by_name(buf)) {
			strcpy(dev->name, buf);
			return i;
		}
	}
	return -ENFILE;	/* Over 100 of the things .. bail out! */
}


/**
 *	dev_change_name - change name of a device
 *	@dev: device
 *	@name: name (or format string) must be at least IFNAMSIZ
 *
 *	Change name of a device, can pass format strings "eth%d".
 *	for wildcarding.
 */
int dev_change_name(struct net_device *dev, char *newname)
{
	ASSERT_RTNL();

	if (dev->flags & IFF_UP)
		return -EBUSY;

	if (!dev_valid_name(newname))
		return -EINVAL;

	if (strchr(newname, '%')) {
		int err = dev_alloc_name(dev, newname);
		if (err < 0)
			return err;
		strcpy(newname, dev->name);
	}
	else if (__dev_get_by_name(newname))
		return -EEXIST;
	else
		strlcpy(dev->name, newname, IFNAMSIZ);

	class_device_rename(&dev->class_dev, dev->name);
	notifier_call_chain(&netdev_chain, NETDEV_CHANGENAME, dev);
	return 0;
}

/**
 *	dev_alloc - allocate a network device and name
 *	@name: name format string
 *	@err: error return pointer
 *
 *	Passed a format string, eg. "lt%d", it will allocate a network device
 *	and space for the name. %NULL is returned if no memory is available.
 *	If the allocation succeeds then the name is assigned and the
 *	device pointer returned. %NULL is returned if the name allocation
 *	failed. The cause of an error is returned as a negative errno code
 *	in the variable @err points to.
 *
 *	This call is deprecated in favor of alloc_netdev because
 *	the caller must hold the @dev_base or RTNL locks when doing this in
 *	order to avoid duplicate name allocations.
 */

struct net_device *__dev_alloc(const char *name, int *err)
{
	struct net_device *dev = kmalloc(sizeof(*dev), GFP_KERNEL);

	if (!dev)
		*err = -ENOBUFS;
	else {
		memset(dev, 0, sizeof(*dev));
		*err = dev_alloc_name(dev, name);
		if (*err < 0) {
			kfree(dev);
			dev = NULL;
		}
	}
	return dev;
}

/**
 *	netdev_state_change - device changes state
 *	@dev: device to cause notification
 *
 *	Called to indicate a device has changed state. This function calls
 *	the notifier chains for netdev_chain and sends a NEWLINK message
 *	to the routing socket.
 */
void netdev_state_change(struct net_device *dev)
{
	if (dev->flags & IFF_UP) {
		notifier_call_chain(&netdev_chain, NETDEV_CHANGE, dev);
		rtmsg_ifinfo(RTM_NEWLINK, dev, 0);
	}
}

/**
 *	dev_load 	- load a network module
 *	@name: name of interface
 *
 *	If a network interface is not present and the process has suitable
 *	privileges this function loads the module. If module loading is not
 *	available in this kernel then it becomes a nop.
 */

void dev_load(const char *name)
{
	struct net_device *dev;  

	read_lock(&dev_base_lock);
	dev = __dev_get_by_name(name);
	read_unlock(&dev_base_lock);

	if (!dev && capable(CAP_SYS_MODULE))
		request_module("%s", name);
}

static int default_rebuild_header(struct sk_buff *skb)
{
	printk(KERN_DEBUG "%s: default_rebuild_header called -- BUG!\n",
	       skb->dev ? skb->dev->name : "NULL!!!");
	kfree_skb(skb);
	return 1;
}


/*
 * Some old buggy device drivers change get_stats after registering
 * the device.  Try and trap them here.
 * This can be elimnated when all devices are known fixed.
 */
static inline int get_stats_changed(struct net_device *dev)
{
	int changed = dev->last_stats != dev->get_stats;
	dev->last_stats = dev->get_stats;
	return changed;
}

/**
 *	dev_open	- prepare an interface for use.
 *	@dev:	device to open
 *
 *	Takes a device from down to up state. The device's private open
 *	function is invoked and then the multicast lists are loaded. Finally
 *	the device is moved into the up state and a %NETDEV_UP message is
 *	sent to the netdev notifier chain.
 *
 *	Calling this function on an active interface is a nop. On a failure
 *	a negative errno code is returned.
 */
int dev_open(struct net_device *dev)
{
	int ret = 0;

	/*
	 *	Is it already up?
	 */

	if (dev->flags & IFF_UP)
		return 0;

	/*
	 *	 Check for broken device drivers.
	 */
	if (get_stats_changed(dev) && net_ratelimit()) {
		printk(KERN_ERR "%s: driver changed get_stats after register\n",
		       dev->name);
	}

	/*
	 *	Is it even present?
	 */
	if (!netif_device_present(dev))
		return -ENODEV;

	/*
	 *	Call device private open method
	 */
	set_bit(__LINK_STATE_START, &dev->state);
	if (dev->open) {
		ret = dev->open(dev);
		if (ret)
			clear_bit(__LINK_STATE_START, &dev->state);
	}

	/*
	 * 	Check for more broken device drivers.
	 */
	if (get_stats_changed(dev) && net_ratelimit()) {
		printk(KERN_ERR "%s: driver changed get_stats in open\n",
		       dev->name);
	}

 	/*
	 *	If it went open OK then:
	 */

	if (!ret) {
		/*
		 *	Set the flags.
		 */
		dev->flags |= IFF_UP;

		/*
		 *	Initialize multicasting status
		 */
		dev_mc_upload(dev);

		/*
		 *	Wakeup transmit queue engine
		 */
		dev_activate(dev);

		/*
		 *	... and announce new interface.
		 */
		notifier_call_chain(&netdev_chain, NETDEV_UP, dev);
	}
	return ret;
}

#ifdef CONFIG_NET_FASTROUTE

static void dev_do_clear_fastroute(struct net_device *dev)
{
	if (dev->accept_fastpath) {
		int i;

		for (i = 0; i <= NETDEV_FASTROUTE_HMASK; i++) {
			struct dst_entry *dst;

			write_lock_irq(&dev->fastpath_lock);
			dst = dev->fastpath[i];
			dev->fastpath[i] = NULL;
			write_unlock_irq(&dev->fastpath_lock);

			dst_release(dst);
		}
	}
}

void dev_clear_fastroute(struct net_device *dev)
{
	if (dev) {
		dev_do_clear_fastroute(dev);
	} else {
		read_lock(&dev_base_lock);
		for (dev = dev_base; dev; dev = dev->next)
			dev_do_clear_fastroute(dev);
		read_unlock(&dev_base_lock);
	}
}
#endif

/**
 *	dev_close - shutdown an interface.
 *	@dev: device to shutdown
 *
 *	This function moves an active device into down state. A
 *	%NETDEV_GOING_DOWN is sent to the netdev notifier chain. The device
 *	is then deactivated and finally a %NETDEV_DOWN is sent to the notifier
 *	chain.
 */
int dev_close(struct net_device *dev)
{
	if (!(dev->flags & IFF_UP))
		return 0;

	/*
	 *	Tell people we are going down, so that they can
	 *	prepare to death, when device is still operating.
	 */
	notifier_call_chain(&netdev_chain, NETDEV_GOING_DOWN, dev);

	dev_deactivate(dev);

	clear_bit(__LINK_STATE_START, &dev->state);

	/* Synchronize to scheduled poll. We cannot touch poll list,
	 * it can be even on different cpu. So just clear netif_running(),
	 * and wait when poll really will happen. Actually, the best place
	 * for this is inside dev->stop() after device stopped its irq
	 * engine, but this requires more changes in devices. */

	smp_mb__after_clear_bit(); /* Commit netif_running(). */
	while (test_bit(__LINK_STATE_RX_SCHED, &dev->state)) {
		/* No hurry. */
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(1);
	}

	/*
	 *	Call the device specific close. This cannot fail.
	 *	Only if device is UP
	 *
	 *	We allow it to be called even after a DETACH hot-plug
	 *	event.
	 */
	if (dev->stop)
		dev->stop(dev);

	/*
	 *	Device is now down.
	 */

	dev->flags &= ~IFF_UP;
#ifdef CONFIG_NET_FASTROUTE
	dev_clear_fastroute(dev);
#endif

	/*
	 * Tell people we are down
	 */
	notifier_call_chain(&netdev_chain, NETDEV_DOWN, dev);

	return 0;
}


/*
 *	Device change register/unregister. These are not inline or static
 *	as we export them to the world.
 */

/**
 *	register_netdevice_notifier - register a network notifier block
 *	@nb: notifier
 *
 *	Register a notifier to be called when network device events occur.
 *	The notifier passed is linked into the kernel structures and must
 *	not be reused until it has been unregistered. A negative errno code
 *	is returned on a failure.
 *
 * 	When registered all registration and up events are replayed
 *	to the new notifier to allow device to have a race free 
 *	view of the network device list.
 */

int register_netdevice_notifier(struct notifier_block *nb)
{
	struct net_device *dev;
	int err;

	rtnl_lock();
	err = notifier_chain_register(&netdev_chain, nb);
	if (!err) {
		for (dev = dev_base; dev; dev = dev->next) {
			nb->notifier_call(nb, NETDEV_REGISTER, dev);

			if (dev->flags & IFF_UP) 
				nb->notifier_call(nb, NETDEV_UP, dev);
		}
	}
	rtnl_unlock();
	return err;
}

/**
 *	unregister_netdevice_notifier - unregister a network notifier block
 *	@nb: notifier
 *
 *	Unregister a notifier previously registered by
 *	register_netdevice_notifier(). The notifier is unlinked into the
 *	kernel structures and may then be reused. A negative errno code
 *	is returned on a failure.
 */

int unregister_netdevice_notifier(struct notifier_block *nb)
{
	return notifier_chain_unregister(&netdev_chain, nb);
}

/**
 *	call_netdevice_notifiers - call all network notifier blocks
 *      @val: value passed unmodified to notifier function
 *      @v:   pointer passed unmodified to notifier function
 *
 *	Call all network notifier blocks.  Parameters and return value
 *	are as for notifier_call_chain().
 */

int call_netdevice_notifiers(unsigned long val, void *v)
{
	return notifier_call_chain(&netdev_chain, val, v);
}

/*
 *	Support routine. Sends outgoing frames to any network
 *	taps currently in use.
 */

void dev_queue_xmit_nit(struct sk_buff *skb, struct net_device *dev)
{
	struct packet_type *ptype;
	do_gettimeofday(&skb->stamp);

	rcu_read_lock();
	list_for_each_entry_rcu(ptype, &ptype_all, list) {
		/* Never send packets back to the socket
		 * they originated from - MvS (miquels@drinkel.ow.org)
		 */
		if ((ptype->dev == dev || !ptype->dev) &&
		    (ptype->af_packet_priv == NULL ||
		     (struct sock *)ptype->af_packet_priv != skb->sk)) {
			struct sk_buff *skb2= skb_clone(skb, GFP_ATOMIC);
			if (!skb2)
				break;

			/* skb->nh should be correctly
			   set by sender, so that the second statement is
			   just protection against buggy protocols.
			 */
			skb2->mac.raw = skb2->data;

			if (skb2->nh.raw < skb2->data ||
			    skb2->nh.raw > skb2->tail) {
				if (net_ratelimit())
					printk(KERN_CRIT "protocol %04x is "
					       "buggy, dev %s\n",
					       skb2->protocol, dev->name);
				skb2->nh.raw = skb2->data;
			}

			skb2->h.raw = skb2->nh.raw;
			skb2->pkt_type = PACKET_OUTGOING;
			ptype->func(skb2, skb->dev, ptype);
		}
	}
	rcu_read_unlock();
}

/* Calculate csum in the case, when packet is misrouted.
 * If it failed by some reason, ignore and send skb with wrong
 * checksum.
 */
struct sk_buff *skb_checksum_help(struct sk_buff *skb)
{
	unsigned int csum;
	int offset = skb->h.raw - skb->data;

	if (offset > (int)skb->len)
		BUG();
	csum = skb_checksum(skb, offset, skb->len-offset, 0);

	offset = skb->tail - skb->h.raw;
	if (offset <= 0)
		BUG();
	if (skb->csum + 2 > offset)
		BUG();

	*(u16*)(skb->h.raw + skb->csum) = csum_fold(csum);
	skb->ip_summed = CHECKSUM_NONE;
	return skb;
}

#ifdef CONFIG_HIGHMEM
/* Actually, we should eliminate this check as soon as we know, that:
 * 1. IOMMU is present and allows to map all the memory.
 * 2. No high memory really exists on this machine.
 */

static inline int illegal_highdma(struct net_device *dev, struct sk_buff *skb)
{
	int i;

	if (dev->features & NETIF_F_HIGHDMA)
		return 0;

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++)
		if (skb_shinfo(skb)->frags[i].page >= highmem_start_page)
			return 1;

	return 0;
}
#else
#define illegal_highdma(dev, skb)	(0)
#endif

extern void skb_release_data(struct sk_buff *);

/* Keep head the same: replace data */
int __skb_linearize(struct sk_buff *skb, int gfp_mask)
{
	unsigned int size;
	u8 *data;
	long offset;
	struct skb_shared_info *ninfo;
	int headerlen = skb->data - skb->head;
	int expand = (skb->tail + skb->data_len) - skb->end;

	if (skb_shared(skb))
		BUG();

	if (expand <= 0)
		expand = 0;

	size = skb->end - skb->head + expand;
	size = SKB_DATA_ALIGN(size);
	data = kmalloc(size + sizeof(struct skb_shared_info), gfp_mask);
	if (!data)
		return -ENOMEM;

	/* Copy entire thing */
	if (skb_copy_bits(skb, -headerlen, data, headerlen + skb->len))
		BUG();

	/* Set up shinfo */
	ninfo = (struct skb_shared_info*)(data + size);
	atomic_set(&ninfo->dataref, 1);
	ninfo->tso_size = skb_shinfo(skb)->tso_size;
	ninfo->tso_segs = skb_shinfo(skb)->tso_segs;
	ninfo->nr_frags = 0;
	ninfo->frag_list = NULL;

	/* Offset between the two in bytes */
	offset = data - skb->head;

	/* Free old data. */
	skb_release_data(skb);

	skb->head = data;
	skb->end  = data + size;

	/* Set up new pointers */
	skb->h.raw   += offset;
	skb->nh.raw  += offset;
	skb->mac.raw += offset;
	skb->tail    += offset;
	skb->data    += offset;

	/* We are no longer a clone, even if we were. */
	skb->cloned    = 0;

	skb->tail     += skb->data_len;
	skb->data_len  = 0;
	return 0;
}

/**
 *	dev_queue_xmit - transmit a buffer
 *	@skb: buffer to transmit
 *
 *	Queue a buffer for transmission to a network device. The caller must
 *	have set the device and priority and built the buffer before calling
 *	this function. The function can be called from an interrupt.
 *
 *	A negative errno code is returned on a failure. A success does not
 *	guarantee the frame will be transmitted as it may be dropped due
 *	to congestion or traffic shaping.
 */

int dev_queue_xmit(struct sk_buff *skb)
{
	struct net_device *dev = skb->dev;
	struct Qdisc *q;
	int rc = -ENOMEM;

	if (skb_shinfo(skb)->frag_list &&
	    !(dev->features & NETIF_F_FRAGLIST) &&
	    __skb_linearize(skb, GFP_ATOMIC))
		goto out_kfree_skb;

	/* Fragmented skb is linearized if device does not support SG,
	 * or if at least one of fragments is in highmem and device
	 * does not support DMA from it.
	 */
	if (skb_shinfo(skb)->nr_frags &&
	    (!(dev->features & NETIF_F_SG) || illegal_highdma(dev, skb)) &&
	    __skb_linearize(skb, GFP_ATOMIC))
		goto out_kfree_skb;

	/* If packet is not checksummed and device does not support
	 * checksumming for this protocol, complete checksumming here.
	 */
	if (skb->ip_summed == CHECKSUM_HW &&
	    (!(dev->features & (NETIF_F_HW_CSUM | NETIF_F_NO_CSUM)) &&
	     (!(dev->features & NETIF_F_IP_CSUM) ||
	      skb->protocol != htons(ETH_P_IP)))) {
		if ((skb = skb_checksum_help(skb)) == NULL)
			goto out;
	}

	/* Grab device queue */
	spin_lock_bh(&dev->queue_lock);
	q = dev->qdisc;
	if (q->enqueue) {
		rc = q->enqueue(skb, q);

		qdisc_run(dev);

		spin_unlock_bh(&dev->queue_lock);
		rc = rc == NET_XMIT_BYPASS ? NET_XMIT_SUCCESS : rc;
		goto out;
	}

	/* The device has no queue. Common case for software devices:
	   loopback, all the sorts of tunnels...

	   Really, it is unlikely that xmit_lock protection is necessary here.
	   (f.e. loopback and IP tunnels are clean ignoring statistics
	   counters.)
	   However, it is possible, that they rely on protection
	   made by us here.

	   Check this and shot the lock. It is not prone from deadlocks.
	   Either shot noqueue qdisc, it is even simpler 8)
	 */
	if (dev->flags & IFF_UP) {
		int cpu = smp_processor_id();

		if (dev->xmit_lock_owner != cpu) {
			/*
			 * The spin_lock effectivly does a preempt lock, but 
			 * we are about to drop that...
			 */
			preempt_disable();
			spin_unlock(&dev->queue_lock);
			spin_lock(&dev->xmit_lock);
			dev->xmit_lock_owner = cpu;
			preempt_enable();

			if (!netif_queue_stopped(dev)) {
				if (netdev_nit)
					dev_queue_xmit_nit(skb, dev);

				rc = 0;
				if (!dev->hard_start_xmit(skb, dev)) {
					dev->xmit_lock_owner = -1;
					spin_unlock_bh(&dev->xmit_lock);
					goto out;
				}
			}
			dev->xmit_lock_owner = -1;
			spin_unlock_bh(&dev->xmit_lock);
			if (net_ratelimit())
				printk(KERN_CRIT "Virtual device %s asks to "
				       "queue packet!\n", dev->name);
			goto out_enetdown;
		} else {
			/* Recursion is detected! It is possible,
			 * unfortunately */
			if (net_ratelimit())
				printk(KERN_CRIT "Dead loop on virtual device "
				       "%s, fix it urgently!\n", dev->name);
		}
	}
	spin_unlock_bh(&dev->queue_lock);
out_enetdown:
	rc = -ENETDOWN;
out_kfree_skb:
	kfree_skb(skb);
out:
	return rc;
}


/*=======================================================================
			Receiver routines
  =======================================================================*/

int netdev_max_backlog = 300;
int weight_p = 64;            /* old backlog weight */
/* These numbers are selected based on intuition and some
 * experimentatiom, if you have more scientific way of doing this
 * please go ahead and fix things.
 */
int no_cong_thresh = 10;
int no_cong = 20;
int lo_cong = 100;
int mod_cong = 290;

DEFINE_PER_CPU(struct netif_rx_stats, netdev_rx_stat) = { 0, };


#ifdef CONFIG_NET_HW_FLOWCONTROL
atomic_t netdev_dropping = ATOMIC_INIT(0);
static unsigned long netdev_fc_mask = 1;
unsigned long netdev_fc_xoff;
spinlock_t netdev_fc_lock = SPIN_LOCK_UNLOCKED;

static struct
{
	void (*stimul)(struct net_device *);
	struct net_device *dev;
} netdev_fc_slots[BITS_PER_LONG];

int netdev_register_fc(struct net_device *dev,
		       void (*stimul)(struct net_device *dev))
{
	int bit = 0;
	unsigned long flags;

	spin_lock_irqsave(&netdev_fc_lock, flags);
	if (netdev_fc_mask != ~0UL) {
		bit = ffz(netdev_fc_mask);
		netdev_fc_slots[bit].stimul = stimul;
		netdev_fc_slots[bit].dev = dev;
		set_bit(bit, &netdev_fc_mask);
		clear_bit(bit, &netdev_fc_xoff);
	}
	spin_unlock_irqrestore(&netdev_fc_lock, flags);
	return bit;
}

void netdev_unregister_fc(int bit)
{
	unsigned long flags;

	spin_lock_irqsave(&netdev_fc_lock, flags);
	if (bit > 0) {
		netdev_fc_slots[bit].stimul = NULL;
		netdev_fc_slots[bit].dev = NULL;
		clear_bit(bit, &netdev_fc_mask);
		clear_bit(bit, &netdev_fc_xoff);
	}
	spin_unlock_irqrestore(&netdev_fc_lock, flags);
}

static void netdev_wakeup(void)
{
	unsigned long xoff;

	spin_lock(&netdev_fc_lock);
	xoff = netdev_fc_xoff;
	netdev_fc_xoff = 0;
	while (xoff) {
		int i = ffz(~xoff);
		xoff &= ~(1 << i);
		netdev_fc_slots[i].stimul(netdev_fc_slots[i].dev);
	}
	spin_unlock(&netdev_fc_lock);
}
#endif

static void get_sample_stats(int cpu)
{
#ifdef RAND_LIE
	unsigned long rd;
	int rq;
#endif
	struct softnet_data *sd = &per_cpu(softnet_data, cpu);
	int blog = sd->input_pkt_queue.qlen;
	int avg_blog = sd->avg_blog;

	avg_blog = (avg_blog >> 1) + (blog >> 1);

	if (avg_blog > mod_cong) {
		/* Above moderate congestion levels. */
		sd->cng_level = NET_RX_CN_HIGH;
#ifdef RAND_LIE
		rd = net_random();
		rq = rd % netdev_max_backlog;
		if (rq < avg_blog) /* unlucky bastard */
			sd->cng_level = NET_RX_DROP;
#endif
	} else if (avg_blog > lo_cong) {
		sd->cng_level = NET_RX_CN_MOD;
#ifdef RAND_LIE
		rd = net_random();
		rq = rd % netdev_max_backlog;
			if (rq < avg_blog) /* unlucky bastard */
				sd->cng_level = NET_RX_CN_HIGH;
#endif
	} else if (avg_blog > no_cong)
		sd->cng_level = NET_RX_CN_LOW;
	else  /* no congestion */
		sd->cng_level = NET_RX_SUCCESS;

	sd->avg_blog = avg_blog;
}

#ifdef OFFLINE_SAMPLE
static void sample_queue(unsigned long dummy)
{
/* 10 ms 0r 1ms -- i don't care -- JHS */
	int next_tick = 1;
	int cpu = smp_processor_id();

	get_sample_stats(cpu);
	next_tick += jiffies;
	mod_timer(&samp_timer, next_tick);
}
#endif


/**
 *	netif_rx	-	post buffer to the network code
 *	@skb: buffer to post
 *
 *	This function receives a packet from a device driver and queues it for
 *	the upper (protocol) levels to process.  It always succeeds. The buffer
 *	may be dropped during processing for congestion control or by the
 *	protocol layers.
 *
 *	return values:
 *	NET_RX_SUCCESS	(no congestion)
 *	NET_RX_CN_LOW   (low congestion)
 *	NET_RX_CN_MOD   (moderate congestion)
 *	NET_RX_CN_HIGH  (high congestion)
 *	NET_RX_DROP     (packet was dropped)
 *
 */

int netif_rx(struct sk_buff *skb)
{
	int this_cpu;
	struct softnet_data *queue;
	unsigned long flags;

	if (!skb->stamp.tv_sec)
		do_gettimeofday(&skb->stamp);

	/*
	 * The code is rearranged so that the path is the most
	 * short when CPU is congested, but is still operating.
	 */
	local_irq_save(flags);
	this_cpu = smp_processor_id();
	queue = &__get_cpu_var(softnet_data);

	__get_cpu_var(netdev_rx_stat).total++;
	if (queue->input_pkt_queue.qlen <= netdev_max_backlog) {
		if (queue->input_pkt_queue.qlen) {
			if (queue->throttle)
				goto drop;

enqueue:
			dev_hold(skb->dev);
			__skb_queue_tail(&queue->input_pkt_queue, skb);
#ifndef OFFLINE_SAMPLE
			get_sample_stats(this_cpu);
#endif
			local_irq_restore(flags);
			return queue->cng_level;
		}

		if (queue->throttle) {
			queue->throttle = 0;
#ifdef CONFIG_NET_HW_FLOWCONTROL
			if (atomic_dec_and_test(&netdev_dropping))
				netdev_wakeup();
#endif
		}

		netif_rx_schedule(&queue->backlog_dev);
		goto enqueue;
	}

	if (!queue->throttle) {
		queue->throttle = 1;
		__get_cpu_var(netdev_rx_stat).throttled++;
#ifdef CONFIG_NET_HW_FLOWCONTROL
		atomic_inc(&netdev_dropping);
#endif
	}

drop:
	__get_cpu_var(netdev_rx_stat).dropped++;
	local_irq_restore(flags);

	kfree_skb(skb);
	return NET_RX_DROP;
}

static __inline__ void skb_bond(struct sk_buff *skb)
{
	struct net_device *dev = skb->dev;

	if (dev->master) {
		skb->real_dev = skb->dev;
		skb->dev = dev->master;
	}
}

static void net_tx_action(struct softirq_action *h)
{
	struct softnet_data *sd = &__get_cpu_var(softnet_data);

	if (sd->completion_queue) {
		struct sk_buff *clist;

		local_irq_disable();
		clist = sd->completion_queue;
		sd->completion_queue = NULL;
		local_irq_enable();

		while (clist) {
			struct sk_buff *skb = clist;
			clist = clist->next;

			BUG_TRAP(!atomic_read(&skb->users));
			__kfree_skb(skb);
		}
	}

	if (sd->output_queue) {
		struct net_device *head;

		local_irq_disable();
		head = sd->output_queue;
		sd->output_queue = NULL;
		local_irq_enable();

		while (head) {
			struct net_device *dev = head;
			head = head->next_sched;

			smp_mb__before_clear_bit();
			clear_bit(__LINK_STATE_SCHED, &dev->state);

			if (spin_trylock(&dev->queue_lock)) {
				qdisc_run(dev);
				spin_unlock(&dev->queue_lock);
			} else {
				netif_schedule(dev);
			}
		}
	}
}

static __inline__ int deliver_skb(struct sk_buff *skb,
				  struct packet_type *pt_prev, int last)
{
	atomic_inc(&skb->users);
	return pt_prev->func(skb, skb->dev, pt_prev);
}


#if defined(CONFIG_BRIDGE) || defined (CONFIG_BRIDGE_MODULE)
int (*br_handle_frame_hook)(struct sk_buff *skb);

static __inline__ int handle_bridge(struct sk_buff *skb,
				     struct packet_type *pt_prev)
{
	int ret = NET_RX_DROP;
	if (pt_prev)
		ret = deliver_skb(skb, pt_prev, 0);

	return ret;
}

#endif

static inline int __handle_bridge(struct sk_buff *skb,
			struct packet_type **pt_prev, int *ret)
{
#if defined(CONFIG_BRIDGE) || defined(CONFIG_BRIDGE_MODULE)
	if (skb->dev->br_port && skb->pkt_type != PACKET_LOOPBACK) {
		*ret = handle_bridge(skb, *pt_prev);
		if (br_handle_frame_hook(skb) == 0)
			return 1;

		*pt_prev = NULL;
	}
#endif
	return 0;
}

int netif_receive_skb(struct sk_buff *skb)
{
	struct packet_type *ptype, *pt_prev;
	int ret = NET_RX_DROP;
	unsigned short type = skb->protocol;

	if (!skb->stamp.tv_sec)
		do_gettimeofday(&skb->stamp);

	skb_bond(skb);

	__get_cpu_var(netdev_rx_stat).total++;

#ifdef CONFIG_NET_FASTROUTE
	if (skb->pkt_type == PACKET_FASTROUTE) {
		__get_cpu_var(netdev_rx_stat).fastroute_deferred_out++;
		return dev_queue_xmit(skb);
	}
#endif

	skb->h.raw = skb->nh.raw = skb->data;

	pt_prev = NULL;
	rcu_read_lock();
	list_for_each_entry_rcu(ptype, &ptype_all, list) {
		if (!ptype->dev || ptype->dev == skb->dev) {
			if (pt_prev) 
				ret = deliver_skb(skb, pt_prev, 0);
			pt_prev = ptype;
		}
	}

	handle_diverter(skb);

	if (__handle_bridge(skb, &pt_prev, &ret))
		goto out;

	list_for_each_entry_rcu(ptype, &ptype_base[ntohs(type)&15], list) {
		if (ptype->type == type &&
		    (!ptype->dev || ptype->dev == skb->dev)) {
			if (pt_prev) 
				ret = deliver_skb(skb, pt_prev, 0);
			pt_prev = ptype;
		}
	}

	if (pt_prev) {
		ret = pt_prev->func(skb, skb->dev, pt_prev);
	} else {
		kfree_skb(skb);
		/* Jamal, now you will not able to escape explaining
		 * me how you were going to use this. :-)
		 */
		ret = NET_RX_DROP;
	}

out:
	rcu_read_unlock();
	return ret;
}

static int process_backlog(struct net_device *backlog_dev, int *budget)
{
	int work = 0;
	int quota = min(backlog_dev->quota, *budget);
	struct softnet_data *queue = &__get_cpu_var(softnet_data);
	unsigned long start_time = jiffies;

	for (;;) {
		struct sk_buff *skb;
		struct net_device *dev;

		local_irq_disable();
		skb = __skb_dequeue(&queue->input_pkt_queue);
		if (!skb)
			goto job_done;
		local_irq_enable();

		dev = skb->dev;

		netif_receive_skb(skb);

		dev_put(dev);

		work++;

		if (work >= quota || jiffies - start_time > 1)
			break;

#ifdef CONFIG_NET_HW_FLOWCONTROL
		if (queue->throttle &&
		    queue->input_pkt_queue.qlen < no_cong_thresh ) {
			queue->throttle = 0;
			if (atomic_dec_and_test(&netdev_dropping)) {
				netdev_wakeup();
				break;
			}
		}
#endif
	}

	backlog_dev->quota -= work;
	*budget -= work;
	return -1;

job_done:
	backlog_dev->quota -= work;
	*budget -= work;

	list_del(&backlog_dev->poll_list);
	smp_mb__before_clear_bit();
	netif_poll_enable(backlog_dev);

	if (queue->throttle) {
		queue->throttle = 0;
#ifdef CONFIG_NET_HW_FLOWCONTROL
		if (atomic_dec_and_test(&netdev_dropping))
			netdev_wakeup();
#endif
	}
	local_irq_enable();
	return 0;
}

static void net_rx_action(struct softirq_action *h)
{
	struct softnet_data *queue = &__get_cpu_var(softnet_data);
	unsigned long start_time = jiffies;
	int budget = netdev_max_backlog;

	
	preempt_disable();
	local_irq_disable();

	while (!list_empty(&queue->poll_list)) {
		struct net_device *dev;

		if (budget <= 0 || jiffies - start_time > 1)
			goto softnet_break;

		local_irq_enable();

		dev = list_entry(queue->poll_list.next,
				 struct net_device, poll_list);

		if (dev->quota <= 0 || dev->poll(dev, &budget)) {
			local_irq_disable();
			list_del(&dev->poll_list);
			list_add_tail(&dev->poll_list, &queue->poll_list);
			if (dev->quota < 0)
				dev->quota += dev->weight;
			else
				dev->quota = dev->weight;
		} else {
			dev_put(dev);
			local_irq_disable();
		}
	}
out:
	local_irq_enable();
	preempt_enable();
	return;

softnet_break:
	__get_cpu_var(netdev_rx_stat).time_squeeze++;
	__raise_softirq_irqoff(NET_RX_SOFTIRQ);
	goto out;
}

static gifconf_func_t * gifconf_list [NPROTO];

/**
 *	register_gifconf	-	register a SIOCGIF handler
 *	@family: Address family
 *	@gifconf: Function handler
 *
 *	Register protocol dependent address dumping routines. The handler
 *	that is passed must not be freed or reused until it has been replaced
 *	by another handler.
 */
int register_gifconf(unsigned int family, gifconf_func_t * gifconf)
{
	if (family >= NPROTO)
		return -EINVAL;
	gifconf_list[family] = gifconf;
	return 0;
}


/*
 *	Map an interface index to its name (SIOCGIFNAME)
 */

/*
 *	We need this ioctl for efficient implementation of the
 *	if_indextoname() function required by the IPv6 API.  Without
 *	it, we would have to search all the interfaces to find a
 *	match.  --pb
 */

static int dev_ifname(struct ifreq *arg)
{
	struct net_device *dev;
	struct ifreq ifr;

	/*
	 *	Fetch the caller's info block.
	 */

	if (copy_from_user(&ifr, arg, sizeof(struct ifreq)))
		return -EFAULT;

	read_lock(&dev_base_lock);
	dev = __dev_get_by_index(ifr.ifr_ifindex);
	if (!dev) {
		read_unlock(&dev_base_lock);
		return -ENODEV;
	}

	strcpy(ifr.ifr_name, dev->name);
	read_unlock(&dev_base_lock);

	if (copy_to_user(arg, &ifr, sizeof(struct ifreq)))
		return -EFAULT;
	return 0;
}

/*
 *	Perform a SIOCGIFCONF call. This structure will change
 *	size eventually, and there is nothing I can do about it.
 *	Thus we will need a 'compatibility mode'.
 */

static int dev_ifconf(char *arg)
{
	struct ifconf ifc;
	struct net_device *dev;
	char *pos;
	int len;
	int total;
	int i;

	/*
	 *	Fetch the caller's info block.
	 */

	if (copy_from_user(&ifc, arg, sizeof(struct ifconf)))
		return -EFAULT;

	pos = ifc.ifc_buf;
	len = ifc.ifc_len;

	/*
	 *	Loop over the interfaces, and write an info block for each.
	 */

	total = 0;
	for (dev = dev_base; dev; dev = dev->next) {
		for (i = 0; i < NPROTO; i++) {
			if (gifconf_list[i]) {
				int done;
				if (!pos)
					done = gifconf_list[i](dev, NULL, 0);
				else
					done = gifconf_list[i](dev, pos + total,
							       len - total);
				if (done < 0)
					return -EFAULT;
				total += done;
			}
		}
  	}

	/*
	 *	All done.  Write the updated control block back to the caller.
	 */
	ifc.ifc_len = total;

	/*
	 * 	Both BSD and Solaris return 0 here, so we do too.
	 */
	return copy_to_user(arg, &ifc, sizeof(struct ifconf)) ? -EFAULT : 0;
}

#ifdef CONFIG_PROC_FS
/*
 *	This is invoked by the /proc filesystem handler to display a device
 *	in detail.
 */
static __inline__ struct net_device *dev_get_idx(loff_t pos)
{
	struct net_device *dev;
	loff_t i;

	for (i = 0, dev = dev_base; dev && i < pos; ++i, dev = dev->next);

	return i == pos ? dev : NULL;
}

void *dev_seq_start(struct seq_file *seq, loff_t *pos)
{
	read_lock(&dev_base_lock);
	return *pos ? dev_get_idx(*pos - 1) : SEQ_START_TOKEN;
}

void *dev_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	++*pos;
	return v == SEQ_START_TOKEN ? dev_base : ((struct net_device *)v)->next;
}

void dev_seq_stop(struct seq_file *seq, void *v)
{
	read_unlock(&dev_base_lock);
}

static void dev_seq_printf_stats(struct seq_file *seq, struct net_device *dev)
{
	struct net_device_stats *stats = dev->get_stats ? dev->get_stats(dev) :
							  NULL;
	if (stats)
		seq_printf(seq, "%6s:%8lu %7lu %4lu %4lu %4lu %5lu %10lu %9lu "
				"%8lu %7lu %4lu %4lu %4lu %5lu %7lu %10lu\n",
			   dev->name, stats->rx_bytes, stats->rx_packets,
			   stats->rx_errors,
			   stats->rx_dropped + stats->rx_missed_errors,
			   stats->rx_fifo_errors,
			   stats->rx_length_errors + stats->rx_over_errors +
			     stats->rx_crc_errors + stats->rx_frame_errors,
			   stats->rx_compressed, stats->multicast,
			   stats->tx_bytes, stats->tx_packets,
			   stats->tx_errors, stats->tx_dropped,
			   stats->tx_fifo_errors, stats->collisions,
			   stats->tx_carrier_errors +
			     stats->tx_aborted_errors +
			     stats->tx_window_errors +
			     stats->tx_heartbeat_errors,
			   stats->tx_compressed);
	else
		seq_printf(seq, "%6s: No statistics available.\n", dev->name);
}

/*
 *	Called from the PROCfs module. This now uses the new arbitrary sized
 *	/proc/net interface to create /proc/net/dev
 */
static int dev_seq_show(struct seq_file *seq, void *v)
{
	if (v == SEQ_START_TOKEN)
		seq_puts(seq, "Inter-|   Receive                            "
			      "                    |  Transmit\n"
			      " face |bytes    packets errs drop fifo frame "
			      "compressed multicast|bytes    packets errs "
			      "drop fifo colls carrier compressed\n");
	else
		dev_seq_printf_stats(seq, v);
	return 0;
}

static struct netif_rx_stats *softnet_get_online(loff_t *pos)
{
	struct netif_rx_stats *rc = NULL;

	while (*pos < NR_CPUS)
	       	if (cpu_online(*pos)) {
			rc = &per_cpu(netdev_rx_stat, *pos);
			break;
		} else
			++*pos;
	return rc;
}

static void *softnet_seq_start(struct seq_file *seq, loff_t *pos)
{
	return softnet_get_online(pos);
}

static void *softnet_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	++*pos;
	return softnet_get_online(pos);
}

static void softnet_seq_stop(struct seq_file *seq, void *v)
{
}

static int softnet_seq_show(struct seq_file *seq, void *v)
{
	struct netif_rx_stats *s = v;

	seq_printf(seq, "%08x %08x %08x %08x %08x %08x %08x %08x %08x\n",
		   s->total, s->dropped, s->time_squeeze, s->throttled,
		   s->fastroute_hit, s->fastroute_success, s->fastroute_defer,
		   s->fastroute_deferred_out,
#if 0
		   s->fastroute_latency_reduction
#else
		   s->cpu_collision
#endif
		  );
	return 0;
}

static struct seq_operations dev_seq_ops = {
	.start = dev_seq_start,
	.next  = dev_seq_next,
	.stop  = dev_seq_stop,
	.show  = dev_seq_show,
};

static int dev_seq_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &dev_seq_ops);
}

static struct file_operations dev_seq_fops = {
	.owner	 = THIS_MODULE,
	.open    = dev_seq_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release,
};

static struct seq_operations softnet_seq_ops = {
	.start = softnet_seq_start,
	.next  = softnet_seq_next,
	.stop  = softnet_seq_stop,
	.show  = softnet_seq_show,
};

static int softnet_seq_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &softnet_seq_ops);
}

static struct file_operations softnet_seq_fops = {
	.owner	 = THIS_MODULE,
	.open    = softnet_seq_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release,
};

#ifdef WIRELESS_EXT
extern int wireless_proc_init(void);
#else
#define wireless_proc_init() 0
#endif

static int __init dev_proc_init(void)
{
	int rc = -ENOMEM;

	if (!proc_net_fops_create("dev", S_IRUGO, &dev_seq_fops))
		goto out;
	if (!proc_net_fops_create("softnet_stat", S_IRUGO, &softnet_seq_fops))
		goto out_dev;
	if (wireless_proc_init())
		goto out_softnet;
	rc = 0;
out:
	return rc;
out_softnet:
	proc_net_remove("softnet_stat");
out_dev:
	proc_net_remove("dev");
	goto out;
}
#else
#define dev_proc_init() 0
#endif	/* CONFIG_PROC_FS */


/**
 *	netdev_set_master	-	set up master/slave pair
 *	@slave: slave device
 *	@master: new master device
 *
 *	Changes the master device of the slave. Pass %NULL to break the
 *	bonding. The caller must hold the RTNL semaphore. On a failure
 *	a negative errno code is returned. On success the reference counts
 *	are adjusted, %RTM_NEWLINK is sent to the routing socket and the
 *	function returns zero.
 */
int netdev_set_master(struct net_device *slave, struct net_device *master)
{
	struct net_device *old = slave->master;

	ASSERT_RTNL();

	if (master) {
		if (old)
			return -EBUSY;
		dev_hold(master);
	}

	slave->master = master;
	
	synchronize_net();

	if (old)
		dev_put(old);

	if (master)
		slave->flags |= IFF_SLAVE;
	else
		slave->flags &= ~IFF_SLAVE;

	rtmsg_ifinfo(RTM_NEWLINK, slave, IFF_SLAVE);
	return 0;
}

/**
 *	dev_set_promiscuity	- update promiscuity count on a device
 *	@dev: device
 *	@inc: modifier
 *
 *	Add or remove promsicuity from a device. While the count in the device
 *	remains above zero the interface remains promiscuous. Once it hits zero
 *	the device reverts back to normal filtering operation. A negative inc
 *	value is used to drop promiscuity on the device.
 */
void dev_set_promiscuity(struct net_device *dev, int inc)
{
	unsigned short old_flags = dev->flags;

	dev->flags |= IFF_PROMISC;
	if ((dev->promiscuity += inc) == 0)
		dev->flags &= ~IFF_PROMISC;
	if (dev->flags ^ old_flags) {
#ifdef CONFIG_NET_FASTROUTE
		if (dev->flags & IFF_PROMISC) {
			netdev_fastroute_obstacles++;
			dev_clear_fastroute(dev);
		} else
			netdev_fastroute_obstacles--;
#endif
		dev_mc_upload(dev);
		printk(KERN_INFO "device %s %s promiscuous mode\n",
		       dev->name, (dev->flags & IFF_PROMISC) ? "entered" :
		       					       "left");
	}
}

/**
 *	dev_set_allmulti	- update allmulti count on a device
 *	@dev: device
 *	@inc: modifier
 *
 *	Add or remove reception of all multicast frames to a device. While the
 *	count in the device remains above zero the interface remains listening
 *	to all interfaces. Once it hits zero the device reverts back to normal
 *	filtering operation. A negative @inc value is used to drop the counter
 *	when releasing a resource needing all multicasts.
 */

void dev_set_allmulti(struct net_device *dev, int inc)
{
	unsigned short old_flags = dev->flags;

	dev->flags |= IFF_ALLMULTI;
	if ((dev->allmulti += inc) == 0)
		dev->flags &= ~IFF_ALLMULTI;
	if (dev->flags ^ old_flags)
		dev_mc_upload(dev);
}

unsigned dev_get_flags(const struct net_device *dev)
{
	unsigned flags;

	flags = (dev->flags & ~(IFF_PROMISC |
				IFF_ALLMULTI |
				IFF_RUNNING)) | 
		(dev->gflags & (IFF_PROMISC |
				IFF_ALLMULTI));

	if (netif_running(dev) && netif_carrier_ok(dev))
		flags |= IFF_RUNNING;

	return flags;
}

int dev_change_flags(struct net_device *dev, unsigned flags)
{
	int ret;
	int old_flags = dev->flags;

	/*
	 *	Set the flags on our device.
	 */

	dev->flags = (flags & (IFF_DEBUG | IFF_NOTRAILERS | IFF_NOARP |
			       IFF_DYNAMIC | IFF_MULTICAST | IFF_PORTSEL |
			       IFF_AUTOMEDIA)) |
		     (dev->flags & (IFF_UP | IFF_VOLATILE | IFF_PROMISC |
				    IFF_ALLMULTI));

	/*
	 *	Load in the correct multicast list now the flags have changed.
	 */

	dev_mc_upload(dev);

	/*
	 *	Have we downed the interface. We handle IFF_UP ourselves
	 *	according to user attempts to set it, rather than blindly
	 *	setting it.
	 */

	ret = 0;
	if ((old_flags ^ flags) & IFF_UP) {	/* Bit is different  ? */
		ret = ((old_flags & IFF_UP) ? dev_close : dev_open)(dev);

		if (!ret)
			dev_mc_upload(dev);
	}

	if (dev->flags & IFF_UP &&
	    ((old_flags ^ dev->flags) &~ (IFF_UP | IFF_PROMISC | IFF_ALLMULTI |
					  IFF_VOLATILE)))
		notifier_call_chain(&netdev_chain, NETDEV_CHANGE, dev);

	if ((flags ^ dev->gflags) & IFF_PROMISC) {
		int inc = (flags & IFF_PROMISC) ? +1 : -1;
		dev->gflags ^= IFF_PROMISC;
		dev_set_promiscuity(dev, inc);
	}

	/* NOTE: order of synchronization of IFF_PROMISC and IFF_ALLMULTI
	   is important. Some (broken) drivers set IFF_PROMISC, when
	   IFF_ALLMULTI is requested not asking us and not reporting.
	 */
	if ((flags ^ dev->gflags) & IFF_ALLMULTI) {
		int inc = (flags & IFF_ALLMULTI) ? +1 : -1;
		dev->gflags ^= IFF_ALLMULTI;
		dev_set_allmulti(dev, inc);
	}

	if (old_flags ^ dev->flags)
		rtmsg_ifinfo(RTM_NEWLINK, dev, old_flags ^ dev->flags);

	return ret;
}

int dev_set_mtu(struct net_device *dev, int new_mtu)
{
	int err;

	if (new_mtu == dev->mtu)
		return 0;

	/*	MTU must be positive.	 */
	if (new_mtu < 0)
		return -EINVAL;

	if (!netif_device_present(dev))
		return -ENODEV;

	err = 0;
	if (dev->change_mtu)
		err = dev->change_mtu(dev, new_mtu);
	else
		dev->mtu = new_mtu;
	if (!err && dev->flags & IFF_UP)
		notifier_call_chain(&netdev_chain,
				    NETDEV_CHANGEMTU, dev);
	return err;
}


/*
 *	Perform the SIOCxIFxxx calls.
 */
static int dev_ifsioc(struct ifreq *ifr, unsigned int cmd)
{
	int err;
	struct net_device *dev = __dev_get_by_name(ifr->ifr_name);

	if (!dev)
		return -ENODEV;

	switch (cmd) {
		case SIOCGIFFLAGS:	/* Get interface flags */
			ifr->ifr_flags = dev_get_flags(dev);
			return 0;

		case SIOCSIFFLAGS:	/* Set interface flags */
			return dev_change_flags(dev, ifr->ifr_flags);

		case SIOCGIFMETRIC:	/* Get the metric on the interface
					   (currently unused) */
			ifr->ifr_metric = 0;
			return 0;

		case SIOCSIFMETRIC:	/* Set the metric on the interface
					   (currently unused) */
			return -EOPNOTSUPP;

		case SIOCGIFMTU:	/* Get the MTU of a device */
			ifr->ifr_mtu = dev->mtu;
			return 0;

		case SIOCSIFMTU:	/* Set the MTU of a device */
			return dev_set_mtu(dev, ifr->ifr_mtu);

		case SIOCGIFHWADDR:
			memcpy(ifr->ifr_hwaddr.sa_data, dev->dev_addr,
			       min(sizeof ifr->ifr_hwaddr.sa_data, (size_t) dev->addr_len));
			ifr->ifr_hwaddr.sa_family = dev->type;
			return 0;

		case SIOCSIFHWADDR:
			if (!dev->set_mac_address)
				return -EOPNOTSUPP;
			if (ifr->ifr_hwaddr.sa_family != dev->type)
				return -EINVAL;
			if (!netif_device_present(dev))
				return -ENODEV;
			err = dev->set_mac_address(dev, &ifr->ifr_hwaddr);
			if (!err)
				notifier_call_chain(&netdev_chain,
						    NETDEV_CHANGEADDR, dev);
			return err;

		case SIOCSIFHWBROADCAST:
			if (ifr->ifr_hwaddr.sa_family != dev->type)
				return -EINVAL;
			memcpy(dev->broadcast, ifr->ifr_hwaddr.sa_data,
			       min(sizeof ifr->ifr_hwaddr.sa_data, (size_t) dev->addr_len));
			notifier_call_chain(&netdev_chain,
					    NETDEV_CHANGEADDR, dev);
			return 0;

		case SIOCGIFMAP:
			ifr->ifr_map.mem_start = dev->mem_start;
			ifr->ifr_map.mem_end   = dev->mem_end;
			ifr->ifr_map.base_addr = dev->base_addr;
			ifr->ifr_map.irq       = dev->irq;
			ifr->ifr_map.dma       = dev->dma;
			ifr->ifr_map.port      = dev->if_port;
			return 0;

		case SIOCSIFMAP:
			if (dev->set_config) {
				if (!netif_device_present(dev))
					return -ENODEV;
				return dev->set_config(dev, &ifr->ifr_map);
			}
			return -EOPNOTSUPP;

		case SIOCADDMULTI:
			if (!dev->set_multicast_list ||
			    ifr->ifr_hwaddr.sa_family != AF_UNSPEC)
				return -EINVAL;
			if (!netif_device_present(dev))
				return -ENODEV;
			dev_mc_add(dev, ifr->ifr_hwaddr.sa_data,
				   dev->addr_len, 1);
			return 0;

		case SIOCDELMULTI:
			if (!dev->set_multicast_list ||
			    ifr->ifr_hwaddr.sa_family != AF_UNSPEC)
				return -EINVAL;
			if (!netif_device_present(dev))
				return -ENODEV;
			dev_mc_delete(dev, ifr->ifr_hwaddr.sa_data,
				      dev->addr_len, 1);
			return 0;

		case SIOCGIFINDEX:
			ifr->ifr_ifindex = dev->ifindex;
			return 0;

		case SIOCGIFTXQLEN:
			ifr->ifr_qlen = dev->tx_queue_len;
			return 0;

		case SIOCSIFTXQLEN:
			if (ifr->ifr_qlen < 0)
				return -EINVAL;
			dev->tx_queue_len = ifr->ifr_qlen;
			return 0;

		case SIOCSIFNAME:
			ifr->ifr_newname[IFNAMSIZ-1] = '\0';
			return dev_change_name(dev, ifr->ifr_newname);

		/*
		 *	Unknown or private ioctl
		 */

		default:
			if ((cmd >= SIOCDEVPRIVATE &&
			    cmd <= SIOCDEVPRIVATE + 15) ||
			    cmd == SIOCBONDENSLAVE ||
			    cmd == SIOCBONDRELEASE ||
			    cmd == SIOCBONDSETHWADDR ||
			    cmd == SIOCBONDSLAVEINFOQUERY ||
			    cmd == SIOCBONDINFOQUERY ||
			    cmd == SIOCBONDCHANGEACTIVE ||
			    cmd == SIOCGMIIPHY ||
			    cmd == SIOCGMIIREG ||
			    cmd == SIOCSMIIREG ||
			    cmd == SIOCWANDEV) {
				err = -EOPNOTSUPP;
				if (dev->do_ioctl) {
					if (netif_device_present(dev))
						err = dev->do_ioctl(dev, ifr,
								    cmd);
					else
						err = -ENODEV;
				}
			} else
				err = -EINVAL;

	}
	return err;
}

/*
 *	This function handles all "interface"-type I/O control requests. The actual
 *	'doing' part of this is dev_ifsioc above.
 */

/**
 *	dev_ioctl	-	network device ioctl
 *	@cmd: command to issue
 *	@arg: pointer to a struct ifreq in user space
 *
 *	Issue ioctl functions to devices. This is normally called by the
 *	user space syscall interfaces but can sometimes be useful for
 *	other purposes. The return value is the return from the syscall if
 *	positive or a negative errno code on error.
 */

int dev_ioctl(unsigned int cmd, void *arg)
{
	struct ifreq ifr;
	int ret;
	char *colon;

	/* One special case: SIOCGIFCONF takes ifconf argument
	   and requires shared lock, because it sleeps writing
	   to user space.
	 */

	if (cmd == SIOCGIFCONF) {
		rtnl_shlock();
		ret = dev_ifconf((char *) arg);
		rtnl_shunlock();
		return ret;
	}
	if (cmd == SIOCGIFNAME)
		return dev_ifname((struct ifreq *)arg);

	if (copy_from_user(&ifr, arg, sizeof(struct ifreq)))
		return -EFAULT;

	ifr.ifr_name[IFNAMSIZ-1] = 0;

	colon = strchr(ifr.ifr_name, ':');
	if (colon)
		*colon = 0;

	/*
	 *	See which interface the caller is talking about.
	 */

	switch (cmd) {
		/*
		 *	These ioctl calls:
		 *	- can be done by all.
		 *	- atomic and do not require locking.
		 *	- return a value
		 */
		case SIOCGIFFLAGS:
		case SIOCGIFMETRIC:
		case SIOCGIFMTU:
		case SIOCGIFHWADDR:
		case SIOCGIFSLAVE:
		case SIOCGIFMAP:
		case SIOCGIFINDEX:
		case SIOCGIFTXQLEN:
			dev_load(ifr.ifr_name);
			read_lock(&dev_base_lock);
			ret = dev_ifsioc(&ifr, cmd);
			read_unlock(&dev_base_lock);
			if (!ret) {
				if (colon)
					*colon = ':';
				if (copy_to_user(arg, &ifr,
						 sizeof(struct ifreq)))
					ret = -EFAULT;
			}
			return ret;

		case SIOCETHTOOL:
			dev_load(ifr.ifr_name);
			rtnl_lock();
			ret = dev_ethtool(&ifr);
			rtnl_unlock();
			if (!ret) {
				if (colon)
					*colon = ':';
				if (copy_to_user(arg, &ifr,
						 sizeof(struct ifreq)))
					ret = -EFAULT;
			}
			return ret;

		/*
		 *	These ioctl calls:
		 *	- require superuser power.
		 *	- require strict serialization.
		 *	- return a value
		 */
		case SIOCGMIIPHY:
		case SIOCGMIIREG:
		case SIOCSIFNAME:
			if (!capable(CAP_NET_ADMIN))
				return -EPERM;
			dev_load(ifr.ifr_name);
			rtnl_lock();
			ret = dev_ifsioc(&ifr, cmd);
			rtnl_unlock();
			if (!ret) {
				if (colon)
					*colon = ':';
				if (copy_to_user(arg, &ifr,
						 sizeof(struct ifreq)))
					ret = -EFAULT;
			}
			return ret;

		/*
		 *	These ioctl calls:
		 *	- require superuser power.
		 *	- require strict serialization.
		 *	- do not return a value
		 */
		case SIOCSIFFLAGS:
		case SIOCSIFMETRIC:
		case SIOCSIFMTU:
		case SIOCSIFMAP:
		case SIOCSIFHWADDR:
		case SIOCSIFSLAVE:
		case SIOCADDMULTI:
		case SIOCDELMULTI:
		case SIOCSIFHWBROADCAST:
		case SIOCSIFTXQLEN:
		case SIOCSMIIREG:
		case SIOCBONDENSLAVE:
		case SIOCBONDRELEASE:
		case SIOCBONDSETHWADDR:
		case SIOCBONDSLAVEINFOQUERY:
		case SIOCBONDINFOQUERY:
		case SIOCBONDCHANGEACTIVE:
			if (!capable(CAP_NET_ADMIN))
				return -EPERM;
			dev_load(ifr.ifr_name);
			rtnl_lock();
			ret = dev_ifsioc(&ifr, cmd);
			rtnl_unlock();
			return ret;

		case SIOCGIFMEM:
			/* Get the per device memory space. We can add this but
			 * currently do not support it */
		case SIOCSIFMEM:
			/* Set the per device memory buffer space.
			 * Not applicable in our case */
		case SIOCSIFLINK:
			return -EINVAL;

		/*
		 *	Unknown or private ioctl.
		 */
		default:
			if (cmd == SIOCWANDEV ||
			    (cmd >= SIOCDEVPRIVATE &&
			     cmd <= SIOCDEVPRIVATE + 15)) {
				dev_load(ifr.ifr_name);
				rtnl_lock();
				ret = dev_ifsioc(&ifr, cmd);
				rtnl_unlock();
				if (!ret && copy_to_user(arg, &ifr,
							 sizeof(struct ifreq)))
					ret = -EFAULT;
				return ret;
			}
#ifdef WIRELESS_EXT
			/* Take care of Wireless Extensions */
			if (cmd >= SIOCIWFIRST && cmd <= SIOCIWLAST) {
				/* If command is `set a parameter', or
				 * `get the encoding parameters', check if
				 * the user has the right to do it */
				if (IW_IS_SET(cmd) || cmd == SIOCGIWENCODE) {
					if (!capable(CAP_NET_ADMIN))
						return -EPERM;
				}
				dev_load(ifr.ifr_name);
				rtnl_lock();
				/* Follow me in net/core/wireless.c */
				ret = wireless_process_ioctl(&ifr, cmd);
				rtnl_unlock();
				if (!ret && IW_IS_GET(cmd) &&
				    copy_to_user(arg, &ifr,
					    	 sizeof(struct ifreq)))
					ret = -EFAULT;
				return ret;
			}
#endif	/* WIRELESS_EXT */
			return -EINVAL;
	}
}


/**
 *	dev_new_index	-	allocate an ifindex
 *
 *	Returns a suitable unique value for a new device interface
 *	number.  The caller must hold the rtnl semaphore or the
 *	dev_base_lock to be sure it remains unique.
 */
int dev_new_index(void)
{
	static int ifindex;
	for (;;) {
		if (++ifindex <= 0)
			ifindex = 1;
		if (!__dev_get_by_index(ifindex))
			return ifindex;
	}
}

static int dev_boot_phase = 1;

/* Delayed registration/unregisteration */
static spinlock_t net_todo_list_lock = SPIN_LOCK_UNLOCKED;
static struct list_head net_todo_list = LIST_HEAD_INIT(net_todo_list);

static inline void net_set_todo(struct net_device *dev)
{
	spin_lock(&net_todo_list_lock);
	list_add_tail(&dev->todo_list, &net_todo_list);
	spin_unlock(&net_todo_list_lock);
}

/**
 *	register_netdevice	- register a network device
 *	@dev: device to register
 *
 *	Take a completed network device structure and add it to the kernel
 *	interfaces. A %NETDEV_REGISTER message is sent to the netdev notifier
 *	chain. 0 is returned on success. A negative errno code is returned
 *	on a failure to set up the device, or if the name is a duplicate.
 *
 *	Callers must hold the rtnl semaphore.  See the comment at the
 *	end of Space.c for details about the locking.  You may want
 *	register_netdev() instead of this.
 *
 *	BUGS:
 *	The locking appears insufficient to guarantee two parallel registers
 *	will not get the same name.
 */

int register_netdevice(struct net_device *dev)
{
	struct net_device *d, **dp;
	int ret;

	BUG_ON(dev_boot_phase);
	ASSERT_RTNL();

	/* When net_device's are persistent, this will be fatal. */
	BUG_ON(dev->reg_state != NETREG_UNINITIALIZED);

	spin_lock_init(&dev->queue_lock);
	spin_lock_init(&dev->xmit_lock);
	dev->xmit_lock_owner = -1;
#ifdef CONFIG_NET_FASTROUTE
	dev->fastpath_lock = RW_LOCK_UNLOCKED;
#endif

	ret = alloc_divert_blk(dev);
	if (ret)
		goto out;

	dev->iflink = -1;

	/* Init, if this function is available */
	if (dev->init) {
		ret = dev->init(dev);
		if (ret) {
			if (ret > 0)
				ret = -EIO;
			goto out_err;
		}
	}
 
	if (!dev_valid_name(dev->name)) {
		ret = -EINVAL;
		goto out_err;
	}

	dev->ifindex = dev_new_index();
	if (dev->iflink == -1)
		dev->iflink = dev->ifindex;

	/* Check for existence, and append to tail of chain */
	ret = -EEXIST;
	for (dp = &dev_base; (d = *dp) != NULL; dp = &d->next) {
		if (d == dev || !strcmp(d->name, dev->name))
			goto out_err;
	}
	
	/* Fix illegal SG+CSUM combinations. */
	if ((dev->features & NETIF_F_SG) &&
	    !(dev->features & (NETIF_F_IP_CSUM |
			       NETIF_F_NO_CSUM |
			       NETIF_F_HW_CSUM))) {
		printk("%s: Dropping NETIF_F_SG since no checksum feature.\n",
		       dev->name);
		dev->features &= ~NETIF_F_SG;
	}

	/*
	 *	nil rebuild_header routine,
	 *	that should be never called and used as just bug trap.
	 */

	if (!dev->rebuild_header)
		dev->rebuild_header = default_rebuild_header;

	/*
	 *	Default initial state at registry is that the
	 *	device is present.
	 */

	set_bit(__LINK_STATE_PRESENT, &dev->state);

	dev->next = NULL;
	dev_init_scheduler(dev);
	write_lock_bh(&dev_base_lock);
	*dp = dev;
	dev_hold(dev);
	dev->reg_state = NETREG_REGISTERING;
	write_unlock_bh(&dev_base_lock);

	/* Notify protocols, that a new device appeared. */
	notifier_call_chain(&netdev_chain, NETDEV_REGISTER, dev);

	/* Finish registration after unlock */
	net_set_todo(dev);
	ret = 0;

out:
	return ret;
out_err:
	free_divert_blk(dev);
	goto out;
}

/*
 * netdev_wait_allrefs - wait until all references are gone.
 *
 * This is called when unregistering network devices.
 *
 * Any protocol or device that holds a reference should register
 * for netdevice notification, and cleanup and put back the
 * reference if they receive an UNREGISTER event.
 * We can get stuck here if buggy protocols don't correctly
 * call dev_put. 
 */
static void netdev_wait_allrefs(struct net_device *dev)
{
	unsigned long rebroadcast_time, warning_time;

	rebroadcast_time = warning_time = jiffies;
	while (atomic_read(&dev->refcnt) != 0) {
		if (time_after(jiffies, rebroadcast_time + 1 * HZ)) {
			rtnl_shlock();
			rtnl_exlock();

			/* Rebroadcast unregister notification */
			notifier_call_chain(&netdev_chain,
					    NETDEV_UNREGISTER, dev);

			if (test_bit(__LINK_STATE_LINKWATCH_PENDING,
				     &dev->state)) {
				/* We must not have linkwatch events
				 * pending on unregister. If this
				 * happens, we simply run the queue
				 * unscheduled, resulting in a noop
				 * for this device.
				 */
				linkwatch_run_queue();
			}

			rtnl_exunlock();
			rtnl_shunlock();

			rebroadcast_time = jiffies;
		}

		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(HZ / 4);

		if (time_after(jiffies, warning_time + 10 * HZ)) {
			printk(KERN_EMERG "unregister_netdevice: "
			       "waiting for %s to become free. Usage "
			       "count = %d\n",
			       dev->name, atomic_read(&dev->refcnt));
			warning_time = jiffies;
		}
	}
}

/* The sequence is:
 *
 *	rtnl_lock();
 *	...
 *	register_netdevice(x1);
 *	register_netdevice(x2);
 *	...
 *	unregister_netdevice(y1);
 *	unregister_netdevice(y2);
 *      ...
 *	rtnl_unlock();
 *	free_netdev(y1);
 *	free_netdev(y2);
 *
 * We are invoked by rtnl_unlock() after it drops the semaphore.
 * This allows us to deal with problems:
 * 1) We can create/delete sysfs objects which invoke hotplug
 *    without deadlocking with linkwatch via keventd.
 * 2) Since we run with the RTNL semaphore not held, we can sleep
 *    safely in order to wait for the netdev refcnt to drop to zero.
 */
static DECLARE_MUTEX(net_todo_run_mutex);
void netdev_run_todo(void)
{
	struct list_head list = LIST_HEAD_INIT(list);

	/* Safe outside mutex since we only care about entries that
	 * this cpu put into queue while under RTNL.
	 */
	if (list_empty(&net_todo_list))
		return;

	/* Need to guard against multiple cpu's getting out of order. */
	down(&net_todo_run_mutex);

	/* Snapshot list, allow later requests */
	spin_lock(&net_todo_list_lock);
	list_splice_init(&net_todo_list, &list);
	spin_unlock(&net_todo_list_lock);
		
	while (!list_empty(&list)) {
		struct net_device *dev
			= list_entry(list.next, struct net_device, todo_list);
		list_del(&dev->todo_list);

		switch(dev->reg_state) {
		case NETREG_REGISTERING:
			netdev_register_sysfs(dev);
			dev->reg_state = NETREG_REGISTERED;
			break;

		case NETREG_UNREGISTERING:
			netdev_unregister_sysfs(dev);
			dev->reg_state = NETREG_UNREGISTERED;

			netdev_wait_allrefs(dev);

			/* paranoia */
			BUG_ON(atomic_read(&dev->refcnt));
			BUG_TRAP(!dev->ip_ptr);
			BUG_TRAP(!dev->ip6_ptr);
			BUG_TRAP(!dev->dn_ptr);


			/* It must be the very last action, 
			 * after this 'dev' may point to freed up memory.
			 */
			if (dev->destructor)
				dev->destructor(dev);
			break;

		default:
			printk(KERN_ERR "network todo '%s' but state %d\n",
			       dev->name, dev->reg_state);
			break;
		}
	}

	up(&net_todo_run_mutex);
}

/**
 *	free_netdev - free network device
 *	@dev: device
 *
 *	This function does the last stage of destroying an allocated device 
 * 	interface. The reference to the device object is released.  
 *	If this is the last reference then it will be freed.
 */
void free_netdev(struct net_device *dev)
{
	/*  Compatiablity with error handling in drivers */
	if (dev->reg_state == NETREG_UNINITIALIZED) {
		kfree(dev);
		return;
	}

	BUG_ON(dev->reg_state != NETREG_UNREGISTERED);
	dev->reg_state = NETREG_RELEASED;

	/* will free via class release */
	class_device_put(&dev->class_dev);
}
 
/* Synchronize with packet receive processing. */
void synchronize_net(void) 
{
	might_sleep();
	synchronize_kernel();
}

/**
 *	unregister_netdevice - remove device from the kernel
 *	@dev: device
 *
 *	This function shuts down a device interface and removes it
 *	from the kernel tables. On success 0 is returned, on a failure
 *	a negative errno code is returned.
 *
 *	Callers must hold the rtnl semaphore.  See the comment at the
 *	end of Space.c for details about the locking.  You may want
 *	unregister_netdev() instead of this.
 */

int unregister_netdevice(struct net_device *dev)
{
	struct net_device *d, **dp;

	BUG_ON(dev_boot_phase);
	ASSERT_RTNL();

	/* Some devices call without registering for initialization unwind. */
	if (dev->reg_state == NETREG_UNINITIALIZED) {
		printk(KERN_DEBUG "unregister_netdevice: device %s/%p never "
				  "was registered\n", dev->name, dev);
		return -ENODEV;
	}

	BUG_ON(dev->reg_state != NETREG_REGISTERED);

	/* If device is running, close it first. */
	if (dev->flags & IFF_UP)
		dev_close(dev);

	/* And unlink it from device chain. */
	for (dp = &dev_base; (d = *dp) != NULL; dp = &d->next) {
		if (d == dev) {
			write_lock_bh(&dev_base_lock);
			*dp = d->next;
			write_unlock_bh(&dev_base_lock);
			break;
		}
	}
	if (!d) {
		printk(KERN_ERR "unregister net_device: '%s' not found\n",
		       dev->name);
		return -ENODEV;
	}

	dev->reg_state = NETREG_UNREGISTERING;

	synchronize_net();

#ifdef CONFIG_NET_FASTROUTE
	dev_clear_fastroute(dev);
#endif

	/* Shutdown queueing discipline. */
	dev_shutdown(dev);

	
	/* Notify protocols, that we are about to destroy
	   this device. They should clean all the things.
	*/
	notifier_call_chain(&netdev_chain, NETDEV_UNREGISTER, dev);
	
	/*
	 *	Flush the multicast chain
	 */
	dev_mc_discard(dev);

	if (dev->uninit)
		dev->uninit(dev);

	/* Notifier chain MUST detach us from master device. */
	BUG_TRAP(!dev->master);

	free_divert_blk(dev);

	/* Finish processing unregister after unlock */
	net_set_todo(dev);

	dev_put(dev);
	return 0;
}


/*
 *	Initialize the DEV module. At boot time this walks the device list and
 *	unhooks any devices that fail to initialise (normally hardware not
 *	present) and leaves us with a valid list of present and active devices.
 *
 */

/*
 *       This is called single threaded during boot, so no need
 *       to take the rtnl semaphore.
 */
static int __init net_dev_init(void)
{
	int i, rc = -ENOMEM;

	BUG_ON(!dev_boot_phase);

	if (dev_proc_init())
		goto out;

	if (netdev_sysfs_init())
		goto out;

	INIT_LIST_HEAD(&ptype_all);
	for (i = 0; i < 16; i++) 
		INIT_LIST_HEAD(&ptype_base[i]);

	/*
	 *	Initialise the packet receive queues.
	 */

	for (i = 0; i < NR_CPUS; i++) {
		struct softnet_data *queue;

		queue = &per_cpu(softnet_data, i);
		skb_queue_head_init(&queue->input_pkt_queue);
		queue->throttle = 0;
		queue->cng_level = 0;
		queue->avg_blog = 10; /* arbitrary non-zero */
		queue->completion_queue = NULL;
		INIT_LIST_HEAD(&queue->poll_list);
		set_bit(__LINK_STATE_START, &queue->backlog_dev.state);
		queue->backlog_dev.weight = weight_p;
		queue->backlog_dev.poll = process_backlog;
		atomic_set(&queue->backlog_dev.refcnt, 1);
	}

#ifdef OFFLINE_SAMPLE
	samp_timer.expires = jiffies + (10 * HZ);
	add_timer(&samp_timer);
#endif

	dev_boot_phase = 0;

	open_softirq(NET_TX_SOFTIRQ, net_tx_action, NULL);
	open_softirq(NET_RX_SOFTIRQ, net_rx_action, NULL);

	dst_init();
	dev_mcast_init();

#ifdef CONFIG_NET_SCHED
	pktsched_init();
#endif
	rc = 0;
out:
	return rc;
}

subsys_initcall(net_dev_init);

EXPORT_SYMBOL(__dev_get);
EXPORT_SYMBOL(__dev_get_by_flags);
EXPORT_SYMBOL(__dev_get_by_index);
EXPORT_SYMBOL(__dev_get_by_name);
EXPORT_SYMBOL(__dev_remove_pack);
EXPORT_SYMBOL(__skb_linearize);
EXPORT_SYMBOL(call_netdevice_notifiers);
EXPORT_SYMBOL(dev_add_pack);
EXPORT_SYMBOL(__dev_alloc);
EXPORT_SYMBOL(dev_alloc_name);
EXPORT_SYMBOL(dev_close);
EXPORT_SYMBOL(dev_get_by_flags);
EXPORT_SYMBOL(dev_get_by_index);
EXPORT_SYMBOL(dev_get_by_name);
EXPORT_SYMBOL(dev_getbyhwaddr);
EXPORT_SYMBOL(dev_ioctl);
EXPORT_SYMBOL(dev_new_index);
EXPORT_SYMBOL(dev_open);
EXPORT_SYMBOL(dev_queue_xmit);
EXPORT_SYMBOL(dev_queue_xmit_nit);
EXPORT_SYMBOL(dev_remove_pack);
EXPORT_SYMBOL(dev_set_allmulti);
EXPORT_SYMBOL(dev_set_promiscuity);
EXPORT_SYMBOL(free_netdev);
EXPORT_SYMBOL(netdev_boot_setup_check);
EXPORT_SYMBOL(netdev_set_master);
EXPORT_SYMBOL(netdev_state_change);
EXPORT_SYMBOL(netif_receive_skb);
EXPORT_SYMBOL(netif_rx);
EXPORT_SYMBOL(register_gifconf);
EXPORT_SYMBOL(register_netdevice);
EXPORT_SYMBOL(register_netdevice_notifier);
EXPORT_SYMBOL(skb_checksum_help);
EXPORT_SYMBOL(synchronize_net);
EXPORT_SYMBOL(unregister_netdevice);
EXPORT_SYMBOL(unregister_netdevice_notifier);

#if defined(CONFIG_BRIDGE) || defined(CONFIG_BRIDGE_MODULE)
EXPORT_SYMBOL(br_handle_frame_hook);
#endif
/* for 801q VLAN support */
#if defined(CONFIG_VLAN_8021Q) || defined(CONFIG_VLAN_8021Q_MODULE)
EXPORT_SYMBOL(dev_change_flags);
#endif
#ifdef CONFIG_KMOD
EXPORT_SYMBOL(dev_load);
#endif
#ifdef CONFIG_NET_HW_FLOWCONTROL
EXPORT_SYMBOL(netdev_dropping);
EXPORT_SYMBOL(netdev_fc_xoff);
EXPORT_SYMBOL(netdev_register_fc);
EXPORT_SYMBOL(netdev_unregister_fc);
#endif
#ifdef CONFIG_NET_FASTROUTE
EXPORT_SYMBOL(netdev_fastroute);
EXPORT_SYMBOL(netdev_fastroute_obstacles);
#endif

EXPORT_PER_CPU_SYMBOL(softnet_data);
