/*
 *		IP_ALIAS (AF_INET) aliasing module.
 *
 *
 * Version:	@(#)ip_alias.c	0.43   12/20/95
 *
 * Author:	Juan Jose Ciarlante, <jjciarla@raiz.uncu.edu.ar>
 *
 * Fixes:
 *	JJC	:	ip_alias_dev_select method.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *	
 */

#include <linux/module.h>

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/if.h>
#include <linux/inet.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/route.h>
#include <net/route.h>

#ifdef ALIAS_USER_LAND_DEBUG
#include "net_alias.h"
#include "ip_alias.h"
#include "user_stubs.h"
#endif

#include <linux/net_alias.h>
#include <net/ip_alias.h>

/*
 *	AF_INET alias init
 */
 
static int ip_alias_init_1(struct net_alias_type *this, struct net_alias *alias, struct sockaddr *sa)
{
#ifdef ALIAS_USER_LAND_DEBUG
	printk("alias_init(%s) called.\n", alias->name);
#endif
	MOD_INC_USE_COUNT;
	return 0;
}

/*
 *	AF_INET alias done
 */
 
static int ip_alias_done_1(struct net_alias_type *this, struct net_alias *alias)
{
#ifdef ALIAS_USER_LAND_DEBUG
	printk("alias_done(%s) called.\n", alias->name);
#endif
	MOD_DEC_USE_COUNT;
	return 0;
}

/*
 *	Print alias address info
 */

int ip_alias_print_1(struct net_alias_type *this, struct net_alias *alias, char *buf, int len)
{
	char *p;

	p = (char *) &alias->dev.pa_addr;
	return sprintf(buf, "%d.%d.%d.%d",
		(p[0] & 255), (p[1] & 255), (p[2] & 255), (p[3] & 255));
}

struct device *ip_alias_dev_select(struct net_alias_type *this, struct device *main_dev, struct sockaddr *sa)
{
	__u32 addr;
	struct rtable *rt;
	struct device *dev=NULL;
  
	/*
	 *	Defensive...	
	 */
  
	if (main_dev == NULL) 
		return NULL;

	/*
	 *	Get u32 address. 
	 */

	addr =  (sa)? (*(struct sockaddr_in *)sa).sin_addr.s_addr : 0;
	if (addr == 0)
		return NULL;

	/*
	 *	Find 'closest' device to address given. any other suggestions? ...
	 *	net_alias module will check if returned device is main_dev's alias
	 */

	rt = ip_rt_route(addr, 0);
	if(rt)
	{
		dev=rt->rt_dev;
		ip_rt_put(rt);
	}
	return dev;
}

/*
 * net_alias AF_INET type defn.
 */

struct net_alias_type ip_alias_type =
{
	AF_INET,		/* type */
	0,			/* n_attach */
	"ip",			/* name */
	NULL,			/* get_addr32() */
	NULL,			/* dev_addr_chk() */
	ip_alias_dev_select,	/* dev_select() */
	ip_alias_init_1,	/* alias_init_1() */
	ip_alias_done_1,	/* alias_done_1() */
	ip_alias_print_1,	/* alias_print_1() */
	NULL			/* next */
};

/*
 * ip_alias module initialization
 */

int ip_alias_init(void)
{
	return register_net_alias_type(&ip_alias_type, AF_INET);
}

/*
 * ip_alias module done
 */

int ip_alias_done(void)
{
	return unregister_net_alias_type(&ip_alias_type);
}

#ifdef MODULE

int init_module(void)
{
	if (ip_alias_init() != 0)
		return -EIO;
	return 0;
}

void cleanup_module(void)
{
	if (ip_alias_done() != 0)
		printk(KERN_INFO "ip_alias: can't remove module");
}

#endif /* MODULE */
