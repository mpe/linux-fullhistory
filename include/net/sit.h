/*
 *	SIT tunneling device - definitions
 *	Linux INET6 implementation
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#ifndef _NET_SIT_H
#define _NET_SIT_H

struct sit_mtu_info {
	__u32			addr;		/* IPv4 destination	*/
	unsigned long		tstamp;		/* last use tstamp	*/
	__u32			mtu;		/* Path MTU		*/
	struct sit_mtu_info	*next;
};

struct sit_vif {
	char			name[8];
	struct device		*dev;
	struct sit_vif		*next;
};

extern int				sit_init(void);
extern void				sit_cleanup(void);

extern struct device *			sit_add_tunnel(__u32 dstaddr);

#define SIT_GC_TIMEOUT		(3*60*HZ)
#define SIT_GC_FREQUENCY	(2*60*HZ)

#endif
