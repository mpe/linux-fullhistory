/*
 *	AX.25 release 031
 *
 *	This is ALPHA test software. This code may break your machine, randomly fail to work with new 
 *	releases, misbehave and/or generally screw up. It might even work. 
 *
 *	This code REQUIRES 1.2.1 or higher/ NET3.029
 *
 *	This module:
 *		This module is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	Other kernels modules in this kit are generally BSD derived. See the copyright headers.
 *
 *
 *	History
 *	AX.25 020	Jonathan(G4KLX)	First go.
 *	AX.25 022	Jonathan(G4KLX)	Added the actual meat to this - we now have a nice heard list.
 *	AX.25 025	Alan(GW4PTS)	First cut at autobinding by route scan.
 *	AX.25 028b	Jonathan(G4KLX)	Extracted AX25 control block from the
 *					sock structure. Device removal now
 *					removes the heard structure.
 *	AX.25 029	Steven(GW7RRM)	Added /proc information for uid/callsign mapping.
 *			Jonathan(G4KLX)	Handling of IP mode in the routing list and /proc entry.
 *	AX.25 030	Jonathan(G4KLX)	Added digi-peaters to routing table, and
 *					ioctls to manipulate them. Added port
 *					configuration.
 *	AX.25 031	Jonathan(G4KLX)	Added concept of default route.
 *			Joerg(DL1BKE)	ax25_rt_build_path() find digipeater list and device by 
 *					destination call. Needed for IP routing via digipeater
 *			Jonathan(G4KLX)	Added routing for IP datagram packets.
 *			Joerg(DL1BKE)	Changed routing for IP datagram and VC to use a default
 *					route if available. Does not overwrite default routes
 *					on route-table overflow anymore.
 *			Joerg(DL1BKE)	Fixed AX.25 routing of IP datagram and VC, new ioctl()
 *					"SIOCAX25OPTRT" to set IP mode and a 'permanent' flag
 *					on routes.
 */
 
#include <linux/config.h>
#ifdef CONFIG_AX25
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <net/ax25.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>

#define	AX25_ROUTE_MAX	128

static struct ax25_route {
	struct ax25_route *next;
	ax25_address callsign;
	struct device *dev;
	ax25_digi *digipeat;
	struct timeval stamp;
	int n;
	char ip_mode;
	char perm;
} *ax25_route = NULL;

static struct ax25_dev {
	struct ax25_dev *next;
	struct device *dev;
	unsigned short values[AX25_MAX_VALUES];
} *ax25_device = NULL;

static struct ax25_route * ax25_find_route(ax25_address *addr);

/*
 * small macro to drop non-digipeated digipeaters and reverse path
 */
 
static inline void ax25_route_invert(ax25_digi *in, ax25_digi *out)
{
	int k;
	for (k = 0; k < in->ndigi; k++)
		if (!in->repeated[k])
			break;
	in->ndigi = k;
	ax25_digi_invert(in, out);

}

/*
 * dl1bke 960310: new behaviour:
 *
 * * try to find an existing route to 'src', if found:
 *   - if the route was added manually don't adjust the timestamp
 *   - if this route is 'permanent' just do some statistics and return
 *   - overwrite device and digipeater path
 * * no existing route found:
 *   - try to alloc a new entry
 *   - overwrite the oldest, not manually added entry if this fails.
 *
 * * updated on reception of frames directed to us _only_
 *
 */

void ax25_rt_rx_frame(ax25_address *src, struct device *dev, ax25_digi *digi)
{
	unsigned long flags;
	extern struct timeval xtime;
	struct ax25_route *ax25_rt;
	struct ax25_route *oldest;
	int count;

	count  = 0;
	oldest = NULL;

	for (ax25_rt = ax25_route; ax25_rt != NULL; ax25_rt = ax25_rt->next) {
		if (count == 0 || oldest->stamp.tv_sec == 0 || (ax25_rt->stamp.tv_sec != 0 && ax25_rt->stamp.tv_sec < oldest->stamp.tv_sec))
			oldest = ax25_rt;
		
		if (ax25cmp(&ax25_rt->callsign, src) == 0) {
			if (ax25_rt->stamp.tv_sec != 0)
				ax25_rt->stamp = xtime;

			if (ax25_rt->perm == AX25_RT_PERMANENT) {
				ax25_rt->n++;
				return;
			}
			
			ax25_rt->dev = dev;
			if (digi == NULL) {
				/* drop old digipeater list */
				if (ax25_rt->digipeat != NULL) {
					kfree_s(ax25_rt->digipeat, sizeof(ax25_digi));
					ax25_rt->digipeat = NULL;
				}
				return;
			}
			
			if (ax25_rt->digipeat == NULL && (ax25_rt->digipeat = kmalloc(sizeof(ax25_digi), GFP_ATOMIC)) == NULL)
					return;
			
			ax25_route_invert(digi, ax25_rt->digipeat);
			return;
		}

		count++;
	}

	if (count > AX25_ROUTE_MAX) {
		if (oldest->stamp.tv_sec == 0)
			return;
		if (oldest->digipeat != NULL)
			kfree_s(oldest->digipeat, sizeof(ax25_digi));
		ax25_rt = oldest;
	} else {
		if ((ax25_rt = (struct ax25_route *)kmalloc(sizeof(struct ax25_route), GFP_ATOMIC)) == NULL)
			return;		/* No space */
	}
	
	ax25_rt->callsign = *src;
	ax25_rt->dev      = dev;
	ax25_rt->digipeat = NULL;
	ax25_rt->stamp    = xtime;
	ax25_rt->n        = 1;
	ax25_rt->ip_mode  = ' ';
	ax25_rt->perm	  = AX25_RT_DYNAMIC;

	if (digi != NULL) {
		if ((ax25_rt->digipeat = kmalloc(sizeof(ax25_digi), GFP_ATOMIC)) == NULL) {
			kfree_s(ax25_rt, sizeof(struct ax25_route));
			return;
		}
		
		ax25_route_invert(digi, ax25_rt->digipeat);
		/* used to be: *ax25_rt->digipeat = *digi; */
	}

	if (ax25_rt != oldest) {
		save_flags(flags);
		cli();

		ax25_rt->next = ax25_route;
		ax25_route    = ax25_rt;

		restore_flags(flags);
	}
}

void ax25_rt_device_down(struct device *dev)
{
	struct ax25_route *s, *t, *ax25_rt = ax25_route;
	
	while (ax25_rt != NULL) {
		s       = ax25_rt;
		ax25_rt = ax25_rt->next;

		if (s->dev == dev) {
			if (ax25_route == s) {
				ax25_route = s->next;
				if (s->digipeat != NULL)
					kfree_s((void *)s->digipeat, sizeof(ax25_digi));
				kfree_s((void *)s, (sizeof *s));
			} else {
				for (t = ax25_route; t != NULL; t = t->next) {
					if (t->next == s) {
						t->next = s->next;
						if (s->digipeat != NULL)
							kfree_s((void *)s->digipeat, sizeof(ax25_digi));
						kfree_s((void *)s, sizeof(*s));
						break;
					}
				}				
			}
		}
	}
}

int ax25_rt_ioctl(unsigned int cmd, void *arg)
{
	unsigned long flags;
	struct ax25_route *s, *t, *ax25_rt;
	struct ax25_routes_struct route;
	struct ax25_route_opt_struct rt_option;
	struct device *dev;
	int i, err;

	switch (cmd) {
		case SIOCADDRT:
			if ((err = verify_area(VERIFY_READ, arg, sizeof(route))) != 0)
				return err;		
			memcpy_fromfs(&route, arg, sizeof(route));
			if ((dev = ax25rtr_get_dev(&route.port_addr)) == NULL)
				return -EINVAL;
			if (route.digi_count > AX25_MAX_DIGIS)
				return -EINVAL;
			for (ax25_rt = ax25_route; ax25_rt != NULL; ax25_rt = ax25_rt->next) {
				if (ax25cmp(&ax25_rt->callsign, &route.dest_addr) == 0 && ax25_rt->dev == dev) {
					if (ax25_rt->digipeat != NULL) {
						kfree_s(ax25_rt->digipeat, sizeof(ax25_digi));
						ax25_rt->digipeat = NULL;
					}
					if (route.digi_count != 0) {
						if ((ax25_rt->digipeat = kmalloc(sizeof(ax25_digi), GFP_ATOMIC)) == NULL)
							return -ENOMEM;
						ax25_rt->digipeat->lastrepeat = 0;
						ax25_rt->digipeat->ndigi      = route.digi_count;
						for (i = 0; i < route.digi_count; i++) {
							ax25_rt->digipeat->repeated[i] = 0;
							ax25_rt->digipeat->calls[i]    = route.digi_addr[i];
						}
					}
					ax25_rt->stamp.tv_sec = 0;
					return 0;
				}
			}
			if ((ax25_rt = (struct ax25_route *)kmalloc(sizeof(struct ax25_route), GFP_ATOMIC)) == NULL)
				return -ENOMEM;
			ax25_rt->callsign     = route.dest_addr;
			ax25_rt->dev          = dev;
			ax25_rt->digipeat     = NULL;
			ax25_rt->stamp.tv_sec = 0;
			ax25_rt->n            = 0;
			ax25_rt->ip_mode      = ' ';
			ax25_rt->perm	      = AX25_RT_DYNAMIC;
			if (route.digi_count != 0) {
				if ((ax25_rt->digipeat = kmalloc(sizeof(ax25_digi), GFP_ATOMIC)) == NULL) {
					kfree_s(ax25_rt, sizeof(struct ax25_route));
					return -ENOMEM;
				}
				ax25_rt->digipeat->lastrepeat = 0;
				ax25_rt->digipeat->ndigi      = route.digi_count;
				for (i = 0; i < route.digi_count; i++) {
					ax25_rt->digipeat->repeated[i] = 0;
					ax25_rt->digipeat->calls[i]    = route.digi_addr[i];
				}
			}
			save_flags(flags);
			cli();
			ax25_rt->next = ax25_route;
			ax25_route    = ax25_rt;
			restore_flags(flags);
			break;

		case SIOCDELRT:
			if ((err = verify_area(VERIFY_READ, arg, sizeof(route))) != 0)
				return err;
			memcpy_fromfs(&route, arg, sizeof(route));
			if ((dev = ax25rtr_get_dev(&route.port_addr)) == NULL)
				return -EINVAL;
			ax25_rt = ax25_route;
			while (ax25_rt != NULL) {
				s       = ax25_rt;
				ax25_rt = ax25_rt->next;
				if (s->dev == dev && ax25cmp(&route.dest_addr, &s->callsign) == 0) {
					if (ax25_route == s) {
						ax25_route = s->next;
						if (s->digipeat != NULL)
							kfree_s((void *)s->digipeat, sizeof(ax25_digi));
						kfree_s((void *)s, (sizeof *s));
					} else {
						for (t = ax25_route; t != NULL; t = t->next) {
							if (t->next == s) {
								t->next = s->next;
								if (s->digipeat != NULL)
									kfree_s((void *)s->digipeat, sizeof(ax25_digi));
								kfree_s((void *)s, sizeof(*s));
								break;
							}
						}				
					}
				}
			}
			break;
		case SIOCAX25OPTRT:
			if ((err = verify_area(VERIFY_READ, arg, sizeof(rt_option))) != 0)
				return err;
			memcpy_fromfs(&rt_option, arg, sizeof(rt_option));
			if ((dev = ax25rtr_get_dev(&rt_option.port_addr)) == NULL)
				return -EINVAL;
			ax25_rt = ax25_route;
			while (ax25_rt != NULL) {
				if (ax25_rt->dev == dev && ax25cmp(&rt_option.dest_addr, &ax25_rt->callsign) == 0) {
					switch(rt_option.cmd) {
						case AX25_SET_RT_PERMANENT:
							ax25_rt->perm = (char) rt_option.arg;
							ax25_rt->stamp.tv_sec = 0;
							break;
						case AX25_SET_RT_IPMODE:
							switch (rt_option.arg) {
								case AX25_RT_IPMODE_DEFAULT:
									ax25_rt->ip_mode = ' ';
									break;
								case AX25_RT_IPMODE_DATAGRAM:
									ax25_rt->ip_mode = 'D';
									break;
								case AX25_RT_IPMODE_VC:
									ax25_rt->ip_mode = 'V';
									break;
								default:
									return -EINVAL;
							}
							break;
					}
				}
				ax25_rt = ax25_rt->next;
			}
			break;
	}

	return 0;
}

int ax25_rt_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	struct ax25_route *ax25_rt;
	int len     = 0;
	off_t pos   = 0;
	off_t begin = 0;
	char *callsign;
	int i;
  
	cli();

	len += sprintf(buffer, "callsign  dev  count time      mode F digipeaters\n");

	for (ax25_rt = ax25_route; ax25_rt != NULL; ax25_rt = ax25_rt->next) {
		if (ax25cmp(&ax25_rt->callsign, &null_ax25_address) == 0)
			callsign = "default";
		else
			callsign = ax2asc(&ax25_rt->callsign);
		len += sprintf(buffer + len, "%-9s %-4s %5d %9d",
			callsign,
			ax25_rt->dev ? ax25_rt->dev->name : "???",
			ax25_rt->n,
			ax25_rt->stamp.tv_sec);

		switch (ax25_rt->ip_mode) {
			case 'V':
			case 'v':
				len += sprintf(buffer + len, "   vc");
				break;
			case 'D':
			case 'd':
				len += sprintf(buffer + len, "   dg");
				break;
			default:
				len += sprintf(buffer + len, "    *");
				break;
		}
		
		switch (ax25_rt->perm) {
			case AX25_RT_DYNAMIC:
				if (ax25_rt->stamp.tv_sec == 0)
					len += sprintf(buffer + len, " M");
				else
					len += sprintf(buffer + len, "  ");
				break;
			case AX25_RT_PERMANENT:
				len += sprintf(buffer + len, " P");
				break;
			default:
				len += sprintf(buffer + len, " ?");
		}

		if (ax25_rt->digipeat != NULL)
			for (i = 0; i < ax25_rt->digipeat->ndigi; i++)
				len += sprintf(buffer + len, " %s", ax2asc(&ax25_rt->digipeat->calls[i]));
		
		len += sprintf(buffer + len, "\n");
				
		pos = begin + len;

		if (pos < offset) {
			len   = 0;
			begin = pos;
		}
		
		if (pos > offset + length)
			break;
	}

	sti();

	*start = buffer + (offset - begin);
	len   -= (offset - begin);

	if (len > length) len = length;

	return len;
} 

int ax25_cs_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	ax25_uid_assoc *pt;
	int len     = 0;
	off_t pos   = 0;
	off_t begin = 0;

	cli();

	len += sprintf(buffer, "Policy: %d\n", ax25_uid_policy);

	for (pt = ax25_uid_list; pt != NULL; pt = pt->next) {
		len += sprintf(buffer + len, "%6d %s\n", pt->uid, ax2asc(&pt->call));

		pos = begin + len;

		if (pos < offset) {
			len = 0;
			begin = pos;
		}

		if (pos > offset + length)
			break;
	}

	sti();

	*start = buffer + (offset - begin);
	len   -= offset - begin;

	if (len > length) len = length;

	return len;
}

/*
 *	Find AX.25 route
 */
 
static struct ax25_route * ax25_find_route(ax25_address *addr)
{
	struct ax25_route *ax25_spe_rt = NULL;
	struct ax25_route *ax25_def_rt = NULL;
	struct ax25_route *ax25_rt;
	
	/*
	 *	Bind to the physical interface we heard them on, or the default
	 *	route if none is found;
	 */
	for (ax25_rt = ax25_route; ax25_rt != NULL; ax25_rt = ax25_rt->next) {
		if (ax25cmp(&ax25_rt->callsign, addr) == 0 && ax25_rt->dev != NULL)
			ax25_spe_rt = ax25_rt;
		if (ax25cmp(&ax25_rt->callsign, &null_ax25_address) == 0 && ax25_rt->dev != NULL)
			ax25_def_rt = ax25_rt;
	}

	if (ax25_spe_rt != NULL)
		return ax25_spe_rt;
		
	return ax25_def_rt;
}

/*
 *	Adjust path: If you specify a default route and want to connect
 *      a target on the digipeater path but w/o having a special route
 *	set before, the path has to be truncated from your target on.
 */
 
static inline void ax25_adjust_path(ax25_address *addr, ax25_digi *digipeat)
{
	int k;
	
	for (k = 0; k < digipeat->ndigi; k++) {
		if (ax25cmp(addr, &digipeat->calls[k]) == 0)
			break;
	}
	
	digipeat->ndigi = k;
}
 

/*
 *	Find which interface to use.
 */
int ax25_rt_autobind(ax25_cb *ax25, ax25_address *addr)
{
	struct ax25_route *ax25_rt;
	ax25_address *call;

	if ((ax25_rt = ax25_find_route(addr)) == NULL)
		return -EHOSTUNREACH;
		
	if ((call = ax25_findbyuid(current->euid)) == NULL) {
		if (ax25_uid_policy && !suser())
			return -EPERM;
		if (ax25->device == NULL)
			return -ENODEV;
		call = (ax25_address *)ax25->device->dev_addr;
	}

	ax25->source_addr = *call;

	if (ax25_rt->digipeat != NULL) {
		if ((ax25->digipeat = kmalloc(sizeof(ax25_digi), GFP_ATOMIC)) == NULL)
			return -ENOMEM;
		*ax25->digipeat = *ax25_rt->digipeat;
		ax25_adjust_path(addr, ax25->digipeat);
	}

	if (ax25->sk != NULL)
		ax25->sk->zapped = 0;

	return 0;
}

/*
 *	dl1bke 960117: build digipeater path
 *	dl1bke 960301: use the default route if it exists
 */
void ax25_rt_build_path(ax25_cb *ax25, ax25_address *addr)
{
	struct ax25_route *ax25_rt;
	
	ax25_rt = ax25_find_route(addr);
	
	if (ax25_rt == NULL || ax25_rt->digipeat == NULL)
		return;

	if ((ax25->digipeat = kmalloc(sizeof(ax25_digi), GFP_ATOMIC)) == NULL)
		return;

	ax25->device = ax25_rt->dev;
	*ax25->digipeat = *ax25_rt->digipeat;
	ax25_adjust_path(addr, ax25->digipeat);
}

void ax25_dg_build_path(struct sk_buff *skb, ax25_address *addr, struct device *dev)
{
	struct ax25_route *ax25_rt;
	ax25_digi digipeat;
	ax25_address src, dest;
	unsigned char *bp;
	int len;

	skb_pull(skb, 1);	/* skip KISS command */

	ax25_rt = ax25_find_route(addr);
	if (ax25_rt == NULL || ax25_rt->digipeat == NULL)
		return;
		
	digipeat = *ax25_rt->digipeat;
	
	ax25_adjust_path(addr, &digipeat);

	len = ax25_rt->digipeat->ndigi * AX25_ADDR_LEN;
		
	if (skb_headroom(skb) < len) {
		printk(KERN_CRIT "ax25_dg_build_path: not enough headroom for digis in skb\n");
		return;
	}
	
	memcpy(&dest, skb->data    , AX25_ADDR_LEN);
	memcpy(&src,  skb->data + 7, AX25_ADDR_LEN);

	bp = skb_push(skb, len);

	build_ax25_addr(bp, &src, &dest, ax25_rt->digipeat, C_COMMAND, MODULUS);
}

/*
 *	Register the mode of an incoming IP frame. It is assumed that an entry
 *	already exists in the routing table.
 */
void ax25_ip_mode_set(ax25_address *callsign, struct device *dev, char ip_mode)
{
	struct ax25_route *ax25_rt;

	for (ax25_rt = ax25_route; ax25_rt != NULL; ax25_rt = ax25_rt->next) {
		if (ax25cmp(&ax25_rt->callsign, callsign) == 0 && ax25_rt->dev == dev) {
			ax25_rt->ip_mode = ip_mode;
			return;
		}
	}
}

/*
 *	Return the IP mode of a given callsign/device pair.
 */
char ax25_ip_mode_get(ax25_address *callsign, struct device *dev)
{
	struct ax25_route *ax25_rt;

	for (ax25_rt = ax25_route; ax25_rt != NULL; ax25_rt = ax25_rt->next)
		if (ax25cmp(&ax25_rt->callsign, callsign) == 0 && ax25_rt->dev == dev)
			return ax25_rt->ip_mode;

	return ' ';
}

static struct ax25_dev *ax25_dev_get_dev(struct device *dev)
{
	struct ax25_dev *s;

	for (s = ax25_device; s != NULL; s = s->next)
		if (s->dev == dev)
			return s;
	
	return NULL;
}

/*
 *	Wow, a bit of data hiding. Is this C++ or what ?
 */
unsigned short ax25_dev_get_value(struct device *dev, int valueno)
{
	struct ax25_dev *ax25_dev;

	if ((ax25_dev = ax25_dev_get_dev(dev)) == NULL) {
		printk(KERN_WARNING "ax25_dev_get_flag called with invalid device\n");
		return 1;
	}

	return ax25_dev->values[valueno];
}

/*
 *	This is called when an interface is brought up. These are
 *	reasonable defaults.
 */
void ax25_dev_device_up(struct device *dev)
{
	unsigned long flags;
	struct ax25_dev *ax25_dev;
	
	if ((ax25_dev = (struct ax25_dev *)kmalloc(sizeof(struct ax25_dev), GFP_ATOMIC)) == NULL)
		return;		/* No space */

	ax25_dev->dev        = dev;

	ax25_dev->values[AX25_VALUES_IPDEFMODE] = AX25_DEF_IPDEFMODE;
	ax25_dev->values[AX25_VALUES_AXDEFMODE] = AX25_DEF_AXDEFMODE;
	ax25_dev->values[AX25_VALUES_NETROM]    = AX25_DEF_NETROM;
	ax25_dev->values[AX25_VALUES_TEXT]      = AX25_DEF_TEXT;
	ax25_dev->values[AX25_VALUES_BACKOFF]   = AX25_DEF_BACKOFF;
	ax25_dev->values[AX25_VALUES_CONMODE]   = AX25_DEF_CONMODE;
	ax25_dev->values[AX25_VALUES_WINDOW]    = AX25_DEF_WINDOW;
	ax25_dev->values[AX25_VALUES_EWINDOW]   = AX25_DEF_EWINDOW;
	ax25_dev->values[AX25_VALUES_T1]        = AX25_DEF_T1 * PR_SLOWHZ;
	ax25_dev->values[AX25_VALUES_T2]        = AX25_DEF_T2 * PR_SLOWHZ;
	ax25_dev->values[AX25_VALUES_T3]        = AX25_DEF_T3 * PR_SLOWHZ;
	ax25_dev->values[AX25_VALUES_IDLE]	= AX25_DEF_IDLE * PR_SLOWHZ * 60;
	ax25_dev->values[AX25_VALUES_N2]        = AX25_DEF_N2;
	ax25_dev->values[AX25_VALUES_DIGI]      = AX25_DEF_DIGI;
	ax25_dev->values[AX25_VALUES_PACLEN]	= AX25_DEF_PACLEN;
	ax25_dev->values[AX25_VALUES_IPMAXQUEUE]= AX25_DEF_IPMAXQUEUE;

	save_flags(flags);
	cli();

	ax25_dev->next = ax25_device;
	ax25_device    = ax25_dev;

	restore_flags(flags);
}

void ax25_dev_device_down(struct device *dev)
{
	struct ax25_dev *s, *t, *ax25_dev = ax25_device;
	
	while (ax25_dev != NULL) {
		s        = ax25_dev;
		ax25_dev = ax25_dev->next;

		if (s->dev == dev) {
			if (ax25_device == s) {
				ax25_device = s->next;
				kfree_s((void *)s, (sizeof *s));
			} else {
				for (t = ax25_device; t != NULL; t = t->next) {
					if (t->next == s) {
						t->next = s->next;
						kfree_s((void *)s, sizeof(*s));
						break;
					}
				}				
			}
		}
	}
}

int ax25_dev_ioctl(unsigned int cmd, void *arg)
{
	struct ax25_parms_struct ax25_parms;
	struct device *dev;
	struct ax25_dev *ax25_dev;
	int err;

	switch (cmd) {
		case SIOCAX25SETPARMS:
			if (!suser())
				return -EPERM;
			if ((err = verify_area(VERIFY_READ, arg, sizeof(ax25_parms))) != 0)
				return err;
			memcpy_fromfs(&ax25_parms, arg, sizeof(ax25_parms));
			if ((dev = ax25rtr_get_dev(&ax25_parms.port_addr)) == NULL)
				return -EINVAL;
			if ((ax25_dev = ax25_dev_get_dev(dev)) == NULL)
				return -EINVAL;
			if (ax25_parms.values[AX25_VALUES_IPDEFMODE] != 'D' &&
			    ax25_parms.values[AX25_VALUES_IPDEFMODE] != 'V')
				return -EINVAL;
			if (ax25_parms.values[AX25_VALUES_AXDEFMODE] != MODULUS &&
			    ax25_parms.values[AX25_VALUES_AXDEFMODE] != EMODULUS)
				return -EINVAL;
			if (ax25_parms.values[AX25_VALUES_NETROM] != 0 &&
			    ax25_parms.values[AX25_VALUES_NETROM] != 1)
				return -EINVAL;
			if (ax25_parms.values[AX25_VALUES_TEXT] != 0 &&
			    ax25_parms.values[AX25_VALUES_TEXT] != 1)
				return -EINVAL;
			if (ax25_parms.values[AX25_VALUES_BACKOFF] != 'E' &&
			    ax25_parms.values[AX25_VALUES_BACKOFF] != 'L')
				return -EINVAL;
			if (ax25_parms.values[AX25_VALUES_CONMODE] != 0 &&
			    ax25_parms.values[AX25_VALUES_CONMODE] != 1)
				return -EINVAL;
			if (ax25_parms.values[AX25_VALUES_WINDOW] < 1 ||
			    ax25_parms.values[AX25_VALUES_WINDOW] > 7)
				return -EINVAL;
			if (ax25_parms.values[AX25_VALUES_EWINDOW] < 1 ||
			    ax25_parms.values[AX25_VALUES_EWINDOW] > 63)
				return -EINVAL;
			if (ax25_parms.values[AX25_VALUES_T1] < 1)
				return -EINVAL;
			if (ax25_parms.values[AX25_VALUES_T2] < 1)
				return -EINVAL;
			if (ax25_parms.values[AX25_VALUES_T3] < 1)
				return -EINVAL;
			if (ax25_parms.values[AX25_VALUES_IDLE] > 100)
				return -EINVAL;
			if (ax25_parms.values[AX25_VALUES_N2] < 1 ||
			    ax25_parms.values[AX25_VALUES_N2] > 31)
				return -EINVAL;
			if (ax25_parms.values[AX25_VALUES_PACLEN] < 22)
				return -EINVAL;
			if ((ax25_parms.values[AX25_VALUES_DIGI] &
			    ~(AX25_DIGI_INBAND | AX25_DIGI_XBAND)) != 0)
				return -EINVAL;
			if (ax25_parms.values[AX25_VALUES_IPMAXQUEUE] < 1)
				return -EINVAL;
			memcpy(ax25_dev->values, ax25_parms.values, AX25_MAX_VALUES * sizeof(short));
			ax25_dev->values[AX25_VALUES_T1] *= PR_SLOWHZ;
			ax25_dev->values[AX25_VALUES_T1] /= 2;
			ax25_dev->values[AX25_VALUES_T2] *= PR_SLOWHZ;
			ax25_dev->values[AX25_VALUES_T3] *= PR_SLOWHZ;
			ax25_dev->values[AX25_VALUES_IDLE] *= PR_SLOWHZ * 60;
			break;

		case SIOCAX25GETPARMS:
			if ((err = verify_area(VERIFY_WRITE, arg, sizeof(struct ax25_parms_struct))) != 0)
				return err;
			memcpy_fromfs(&ax25_parms, arg, sizeof(ax25_parms));
			if ((dev = ax25rtr_get_dev(&ax25_parms.port_addr)) == NULL)
				return -EINVAL;
			if ((ax25_dev = ax25_dev_get_dev(dev)) == NULL)
				return -EINVAL;
			memcpy(ax25_parms.values, ax25_dev->values, AX25_MAX_VALUES * sizeof(short));
			ax25_parms.values[AX25_VALUES_T1] *= 2;
			ax25_parms.values[AX25_VALUES_T1] /= PR_SLOWHZ;
			ax25_parms.values[AX25_VALUES_T2] /= PR_SLOWHZ;
			ax25_parms.values[AX25_VALUES_T3] /= PR_SLOWHZ;
			ax25_parms.values[AX25_VALUES_IDLE] /= PR_SLOWHZ * 60;
			memcpy_tofs(arg, &ax25_parms, sizeof(ax25_parms));
			break;
	}

	return 0;
}

#ifdef CONFIG_BPQETHER
static struct ax25_bpqdev {
	struct ax25_bpqdev *next;
	struct device *dev;
	ax25_address callsign;
} *ax25_bpqdev = NULL;

int ax25_bpq_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	struct ax25_bpqdev *bpqdev;
	int len     = 0;
	off_t pos   = 0;
	off_t begin = 0;
  
	cli();

	len += sprintf(buffer, "dev  callsign\n");

	for (bpqdev = ax25_bpqdev; bpqdev != NULL; bpqdev = bpqdev->next) {
		len += sprintf(buffer + len, "%-4s %-9s\n",
			bpqdev->dev ? bpqdev->dev->name : "???",
			ax2asc(&bpqdev->callsign));

		pos = begin + len;

		if (pos < offset) {
			len   = 0;
			begin = pos;
		}
		
		if (pos > offset + length)
			break;
	}

	sti();

	*start = buffer + (offset - begin);
	len   -= (offset - begin);

	if (len > length) len = length;

	return len;
} 

ax25_address *ax25_bpq_get_addr(struct device *dev)
{
	struct ax25_bpqdev *bpqdev;
	
	for (bpqdev = ax25_bpqdev; bpqdev != NULL; bpqdev = bpqdev->next)
		if (bpqdev->dev == dev)
			return &bpqdev->callsign;

	return NULL;
}

int ax25_bpq_ioctl(unsigned int cmd, void *arg)
{
	unsigned long flags;
	struct ax25_bpqdev *bpqdev;
	struct ax25_bpqaddr_struct bpqaddr;
	struct device *dev;
	int err;

	switch (cmd) {
		case SIOCAX25BPQADDR:
			if ((err = verify_area(VERIFY_READ, arg, sizeof(bpqaddr))) != 0)
				return err;
			memcpy_fromfs(&bpqaddr, arg, sizeof(bpqaddr));
			if ((dev = dev_get(bpqaddr.dev)) == NULL)
				return -EINVAL;
			if (dev->type != ARPHRD_ETHER)
				return -EINVAL;
			for (bpqdev = ax25_bpqdev; bpqdev != NULL; bpqdev = bpqdev->next) {
				if (bpqdev->dev == dev) {
					bpqdev->callsign = bpqaddr.addr;
					return 0;
				}
			}
			if ((bpqdev = (struct ax25_bpqdev *)kmalloc(sizeof(struct ax25_bpqdev), GFP_ATOMIC)) == NULL)
				return -ENOMEM;
			bpqdev->dev      = dev;
			bpqdev->callsign = bpqaddr.addr;
			save_flags(flags);
			cli();
			bpqdev->next = ax25_bpqdev;
			ax25_bpqdev  = bpqdev;
			restore_flags(flags);
			break;
	}

	return 0;
}

#endif

#endif
