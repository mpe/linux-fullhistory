/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET  is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the Forwarding Information Base.
 *
 * Authors:	A.N.Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#ifndef _NET_IP_FIB_H
#define _NET_IP_FIB_H


struct fib_node
{
	struct fib_node		*fib_next;
	u32			fib_key;
	struct fib_info		*fib_info;
	short			fib_metric;
	u8			fib_tos;
	u8			fib_flag;
};

#define FIBFLG_DOWN		1	/* Ignore this node	*/
#define FIBFLG_THROW		2	/* Class lookup failed	*/
#define FIBFLG_REJECT		4	/* Route lookup failed	*/

#define MAGIC_METRIC		0x7FFF

/*
 * This structure contains data shared by many of routes.
 */	

struct fib_info
{
	struct fib_info		*fib_next;
	struct fib_info		*fib_prev;
	u32			fib_gateway;
	struct device		*fib_dev;
	int			fib_refcnt;
	unsigned long		fib_window;
	unsigned		fib_flags;
	unsigned short		fib_mtu;
	unsigned short		fib_irtt;
};

struct fib_zone
{
	struct fib_zone	*fz_next;
	struct fib_node	**fz_hash;
	int		fz_nent;
	int		fz_divisor;
	u32		fz_hashmask;
	int		fz_logmask;
	u32		fz_mask;
};

struct fib_class
{
	unsigned char	cl_id;
	unsigned char	cl_auto;
	struct fib_zone	*fib_zones[33];
	struct fib_zone	*fib_zone_list;
	int		cl_users;
};

struct fib_rule
{
	struct fib_rule *cl_next;
	struct fib_class *cl_class;
	u32		cl_src;
	u32		cl_srcmask;
	u32		cl_dst;
	u32		cl_dstmask;
	u32		cl_srcmap;
	u8		cl_action;
	u8		cl_flags;
	u8		cl_tos;
	u8		cl_preference;
	struct device	*cl_dev;
};

struct fib_result
{
	struct fib_node *f;
	struct fib_rule *fr;
	int		 fm;
};

void ip_fib_init(void);
unsigned ip_fib_chk_addr(u32 addr);
int ip_fib_chk_default_gw(u32 addr, struct device*);

int fib_lookup(struct fib_result *, u32 daddr, u32 src, u8 tos, struct device *devin,
	       struct device *devout);

static __inline__ struct fib_info *
fib_lookup_info(u32 dst, u32 src, u8 tos, struct device *devin,
		struct device *devout)
{
	struct fib_result res;
	if (fib_lookup(&res, dst, src, tos, devin, devout) < 0)
		return NULL;
	return res.f->fib_info;
}

static __inline__ struct device * get_gw_dev(u32 gw, struct device *dev)
{
	struct fib_info * fi;

	fi = fib_lookup_info(gw, 0, 1, &loopback_dev, dev);
	if (fi)
		return fi->fib_dev;
	return NULL;
}

extern int		ip_rt_event(int event, struct device *dev);
extern int		ip_rt_ioctl(unsigned int cmd, void *arg);
extern void		ip_rt_change_broadcast(struct device *, u32);
extern void		ip_rt_change_dstaddr(struct device *, u32);
extern void		ip_rt_change_netmask(struct device *, u32);
extern void		ip_rt_multicast_event(struct device *dev);

extern struct device *	ip_dev_find_tunnel(u32 daddr, u32 saddr);


#endif  _NET_FIB_H
