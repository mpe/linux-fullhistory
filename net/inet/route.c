/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		ROUTE - implementation of the IP router.
 *
 * Version:	@(#)route.c	1.0.14	05/31/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#include <asm/segment.h>
#include <asm/system.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/errno.h>
#include <linux/in.h>
#include "inet.h"
#include "dev.h"
#include "ip.h"
#include "protocol.h"
#include "route.h"
#include "tcp.h"
#include "skbuff.h"
#include "sock.h"
#include "arp.h"
#include "icmp.h"


static struct rtable *rt_base = NULL;


/* Dump the contents of a routing table entry. */
static void
rt_print(struct rtable *rt)
{
  if (rt == NULL || inet_debug != DBG_RT) return;

  printk("RT: %06lx NXT=%06lx FLAGS=0x%02lx\n",
		(long) rt, (long) rt->rt_next, rt->rt_flags);
  printk("    TARGET=%s ", in_ntoa(rt->rt_dst));
  printk("GW=%s ", in_ntoa(rt->rt_gateway));
  printk("    DEV=%s USE=%ld REF=%ld\n",
	(rt->rt_dev == NULL) ? "NONE" : rt->rt_dev->name,
	rt->rt_use, rt->rt_refcnt);
}


/* Remove a routing table entry. */
static void
rt_del(unsigned long dst)
{
  struct rtable *r, *x, *p;

  DPRINTF((DBG_RT, "RT: flushing for dst %s\n", in_ntoa(dst)));
  if ((r = rt_base) == NULL) return;
  p = NULL;
  while(r != NULL) {
	if (r->rt_dst == dst) {
		if (p == NULL) rt_base = r->rt_next;
		  else p->rt_next = r->rt_next;
		x = r->rt_next;
		kfree_s(r, sizeof(struct rtable));
		r = x;
	} else {
		p = r;
		r = r->rt_next;
	}
  }
}


/* Remove all routing table entries for a device. */
void
rt_flush(struct device *dev)
{
  struct rtable *r, *x, *p;

  DPRINTF((DBG_RT, "RT: flushing for dev 0x%08lx (%s)\n", (long)dev, dev->name));
  if ((r = rt_base) == NULL) return;
  p = NULL;
  while(r != NULL) {
	if (r->rt_dev == dev) {
		if (p == NULL) rt_base = r->rt_next;
		  else p->rt_next = r->rt_next;
		x = r->rt_next;
		kfree_s(r, sizeof(struct rtable));
		r = x;
	} else {
		p = r;
		r = r->rt_next;
	}
  }
}


void
rt_add(short flags, unsigned long dst, unsigned long gw, struct device *dev)
{
  struct rtable *r, *r1;
  struct rtable *rt;
  int mask;

  /* Allocate an entry. */
  rt = (struct rtable *) kmalloc(sizeof(struct rtable), GFP_ATOMIC);
  if (rt == NULL) {
	DPRINTF((DBG_RT, "RT: no memory for new route!\n"));
	return;
  }

  /* Fill in the fields. */
  memset(rt, 0, sizeof(struct rtable));
  rt->rt_flags = (flags | RTF_UP);
  if (gw != 0) rt->rt_flags |= RTF_GATEWAY;
  rt->rt_dev = dev;
  rt->rt_gateway = gw;

  /*
   * If this is coming from an ICMP redirect message, truncate
   * the TARGET if we are creating an entry for a NETWORK. Use
   * an Internet class C network mask.  Yuck :-(
   */
  if (flags & RTF_DYNAMIC) {
	if (flags & RTF_HOST)
		rt->rt_dst = dst;
	else
		rt->rt_dst = (dst & dev->pa_mask);
  } else rt->rt_dst = dst;

  rt_print(rt);

  if (rt_base == NULL) {
	rt->rt_next = NULL;
	rt_base = rt;
	return;
  }

  /*
   * What we have to do is loop though this until we have
   * found the first address which has the same generality
   * as the one in rt.  Then we can put rt in after it.
   */
  for (mask = 0xff000000; mask != 0xffffffff; mask = (mask >> 8) | mask) {
	if (mask & dst) {
		mask = mask << 8;
		break;
	}
  }
  DPRINTF((DBG_RT, "RT: mask = %X\n", mask));
  r1 = rt_base;

  /* See if we are getting a duplicate. */
  for (r = rt_base; r != NULL; r = r->rt_next) {
	if (r->rt_dst == dst) {
		if (r == rt_base) {
			rt->rt_next = r->rt_next;
			rt_base = rt;
		} else {
			rt->rt_next = r->rt_next;
			r1->rt_next = rt;
		}
		kfree_s(r, sizeof(struct rtable));
		return;
	}

	if (! (r->rt_dst & mask)) {
		DPRINTF((DBG_RT, "RT: adding before r=%X\n", r));
		rt_print(r);
		if (r == rt_base) {
			rt->rt_next = rt_base;
			rt_base = rt;
			return;
		}
		rt->rt_next = r;
		r1->rt_next = rt;
		return;
	}
	r1 = r;
  }
  DPRINTF((DBG_RT, "RT: adding after r1=%X\n", r1));
  rt_print(r1);

  /* Goes at the end. */
  rt->rt_next = NULL;
  r1->rt_next = rt;
}


static int
rt_new(struct rtentry *r)
{
  struct device *dev;
  struct rtable *rt;

  if ((r->rt_dst.sa_family != AF_INET) ||
      (r->rt_gateway.sa_family != AF_INET)) {
	DPRINTF((DBG_RT, "RT: We only know about AF_INET !\n"));
	return(-EAFNOSUPPORT);
  }

  /*
   * I admit that the following bits of code were "inspired" by
   * the Berkeley UNIX system source code.  I could think of no
   * other way to find out how to make it compatible with it (I
   * want this to be compatible to get "routed" up and running).
   * -FvK
   */

  /* If we have a 'gateway' route here, check the correct address. */
  if (!(r->rt_flags & RTF_GATEWAY))
	dev = dev_check(((struct sockaddr_in *) &r->rt_dst)->sin_addr.s_addr);
  else
	if ((rt = rt_route(((struct sockaddr_in *) &r->rt_gateway)->sin_addr.
			   s_addr,NULL)))
	    dev = rt->rt_dev;
	else
	    dev = NULL;

  DPRINTF((DBG_RT, "RT: dev for %s gw ",
	in_ntoa((*(struct sockaddr_in *)&r->rt_dst).sin_addr.s_addr)));
  DPRINTF((DBG_RT, "%s (0x%04X) is 0x%X (%s)\n",
	in_ntoa((*(struct sockaddr_in *)&r->rt_gateway).sin_addr.s_addr),
	r->rt_flags, dev, (dev == NULL) ? "NONE" : dev->name));

  if (dev == NULL) return(-ENETUNREACH);

  rt_add(r->rt_flags, (*(struct sockaddr_in *) &r->rt_dst).sin_addr.s_addr,
	 (*(struct sockaddr_in *) &r->rt_gateway).sin_addr.s_addr, dev);

  return(0);
}


static int
rt_kill(struct rtentry *r)
{
  struct sockaddr_in *trg;

  trg = (struct sockaddr_in *) &r->rt_dst;
  rt_del(trg->sin_addr.s_addr);

  return(0);
}


/* Called from the PROCfs module. */
int
rt_get_info(char *buffer)
{
  struct rtable *r;
  char *pos;

  pos = buffer;

  pos += sprintf(pos,
		 "Iface\tDestination\tGateway \tFlags\tRefCnt\tUse\tMetric\n");
  
  /* This isn't quite right -- r->rt_dst is a struct! */
  for (r = rt_base; r != NULL; r = r->rt_next) {
        pos += sprintf(pos, "%s\t%08X\t%08X\t%02X\t%d\t%d\t%d\n",
		r->rt_dev->name, r->rt_dst, r->rt_gateway,
		r->rt_flags, r->rt_refcnt, r->rt_use, r->rt_metric);
  }
  return(pos - buffer);
}


struct rtable *
rt_route(unsigned long daddr, struct options *opt)
{
  struct rtable *rt;

  /*
   * This is a hack, I think. -FvK
   */
  if (chk_addr(daddr) == IS_MYADDR) daddr = my_addr();

  /*
   * Loop over the IP routing table to find a route suitable
   * for this packet.  Note that we really should have a look
   * at the IP options to see if we have been given a hint as
   * to what kind of path we should use... -FvK
   */
  for (rt = rt_base; rt != NULL; rt = rt->rt_next)
	if ((rt->rt_flags & RTF_HOST) && rt->rt_dst == daddr) {
		DPRINTF((DBG_RT, "%s (%s)\n",
			rt->rt_dev->name, in_ntoa(rt->rt_gateway)));
		rt->rt_use++;
		return(rt);
	}
  for (rt = rt_base; rt != NULL; rt = rt->rt_next) {
	DPRINTF((DBG_RT, "RT: %s via ", in_ntoa(daddr)));
	if (!(rt->rt_flags & RTF_HOST) && ip_addr_match(rt->rt_dst, daddr)) {
		DPRINTF((DBG_RT, "%s (%s)\n",
			rt->rt_dev->name, in_ntoa(rt->rt_gateway)));
		rt->rt_use++;
		return(rt);
	}
	if ((rt->rt_dev->flags & IFF_BROADCAST) &&
	    ip_addr_match(rt->rt_dev->pa_brdaddr, daddr)) {
		DPRINTF((DBG_RT, "%s (BCAST %s)\n",
			rt->rt_dev->name, in_ntoa(rt->rt_dev->pa_brdaddr)));
		rt->rt_use++;
		return(rt);
	}
  }

  DPRINTF((DBG_RT, "NONE\n"));
  return(NULL);
};


int
rt_ioctl(unsigned int cmd, void *arg)
{
  struct device *dev;
  struct rtentry rt;
  char namebuf[32];
  int ret;

  switch(cmd) {
	case DDIOCSDBG:
		ret = dbg_ioctl(arg, DBG_RT);
		break;
	case SIOCADDRT:
	case SIOCDELRT:
		if (!suser()) return(-EPERM);
		verify_area(VERIFY_WRITE, arg, sizeof(struct rtentry));
		memcpy_fromfs(&rt, arg, sizeof(struct rtentry));
		if (rt.rt_dev) {
		    verify_area(VERIFY_WRITE, rt.rt_dev, sizeof namebuf);
		    memcpy_fromfs(&namebuf, rt.rt_dev, sizeof namebuf);
		    dev = dev_get(namebuf);
		    rt.rt_dev = dev;
		}
		ret = (cmd == SIOCDELRT) ? rt_kill(&rt) : rt_new(&rt);
		break;
	default:
		ret = -EINVAL;
  }

  return(ret);
}
