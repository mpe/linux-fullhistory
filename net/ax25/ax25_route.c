/*
 *	AX.25 release 030
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
 *	AX.25 022	Jonathan(G4KLX)	Added the actual meat to this - we now have a nice mheard list.
 *	AX.25 025	Alan(GW4PTS)	First cut at autobinding by route scan.
 *	AX.25 028b	Jonathan(G4KLX)	Extracted AX25 control block from the
 *					sock structure. Device removal now
 *					removes the heard structure.
 *	AX.25 029	Steven(GW7RRM)	Added /proc information for uid/callsign mapping.
 *			Jonathan(G4KLX)	Handling of IP mode in the routing list and /proc entry.
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
#include <linux/skbuff.h>
#include <net/sock.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>

#define	AX25_ROUTE_MAX	40

static struct ax25_route {
	struct ax25_route *next;
	ax25_address callsign;
	struct device *dev;
	struct timeval stamp;
	int n;
	char ip_mode;
} *ax25_route = NULL;

void ax25_rt_rx_frame(ax25_address *src, struct device *dev)
{
	unsigned long flags;
	extern struct timeval xtime;
	struct ax25_route *ax25_rt;
	struct ax25_route *oldest;
	int count;

	count  = 0;
	oldest = NULL;

	for (ax25_rt = ax25_route; ax25_rt != NULL; ax25_rt = ax25_rt->next) {
		if (count == 0 || ax25_rt->stamp.tv_sec < oldest->stamp.tv_sec)
			oldest = ax25_rt;
		
		if (ax25cmp(&ax25_rt->callsign, src) == 0 && ax25_rt->dev == dev) {
			ax25_rt->stamp = xtime;
			ax25_rt->n++;
			return;			
		}

		count++;
	}

	if (count > AX25_ROUTE_MAX) {
		oldest->callsign = *src;
		oldest->dev      = dev;
		oldest->stamp    = xtime;
		oldest->n        = 1;
		oldest->ip_mode  = ' ';
		return;
	}

	if ((ax25_rt = (struct ax25_route *)kmalloc(sizeof(struct ax25_route), GFP_ATOMIC)) == NULL)
		return;		/* No space */

	ax25_rt->callsign = *src;
	ax25_rt->dev      = dev;
	ax25_rt->stamp    = xtime;
	ax25_rt->n        = 1;
	ax25_rt->ip_mode  = ' ';

	save_flags(flags);
	cli();

	ax25_rt->next = ax25_route;
	ax25_route    = ax25_rt;

	restore_flags(flags);
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
				kfree_s((void *)s, (sizeof *s));
			} else {
				for (t = ax25_route; t != NULL; t = t->next) {
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

int ax25_rt_get_info(char *buffer, char **start, off_t offset, int length)
{
	struct ax25_route *ax25_rt;
	int len     = 0;
	off_t pos   = 0;
	off_t begin = 0;
  
	cli();

	len += sprintf(buffer, "callsign  dev  count time      mode\n");

	for (ax25_rt = ax25_route; ax25_rt != NULL; ax25_rt = ax25_rt->next) {
		len += sprintf(buffer + len, "%-9s %-4s %5d %9d",
			ax2asc(&ax25_rt->callsign),
			ax25_rt->dev ? ax25_rt->dev->name : "???",
			ax25_rt->n,
			ax25_rt->stamp.tv_sec);

		switch (ax25_rt->ip_mode) {
			case 'V':
			case 'v':
				len += sprintf(buffer + len, "   vc\n");
				break;
			case 'D':
			case 'd':
				len += sprintf(buffer + len, "   dg\n");
				break;
			default:
				len += sprintf(buffer + len, "\n");
				break;
		}
				
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

int ax25_cs_get_info(char *buffer, char **start, off_t offset, int length)
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
 *	Find what interface to use.
 */
int ax25_rt_autobind(ax25_cb *ax25, ax25_address *addr)
{
	struct ax25_route *ax25_rt;
	ax25_address *call;
	
	for (ax25_rt = ax25_route; ax25_rt != NULL; ax25_rt = ax25_rt->next) {
		if (ax25cmp(&ax25_rt->callsign, addr) == 0) {
			/*
			 *	Bind to the physical interface we heard them on.
			 */
			if ((ax25->device = ax25_rt->dev) == NULL)
				continue;
			if ((call = ax25_findbyuid(current->euid)) == NULL) {
				if (ax25_uid_policy && !suser())
					return -EPERM;
				call = (ax25_address *)ax25->device->dev_addr;
			}
			memcpy(&ax25->source_addr, call, sizeof(ax25_address));
			if (ax25->sk != NULL)
				ax25->sk->zapped = 0;

			return 0;			
		}
	}

	return -EINVAL;
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

#endif
