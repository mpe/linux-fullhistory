/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Interface (streams) handling functions.
 *
 * Version:	@(#)dev.c	1.28	20/12/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Mark Evans, <evansmp@uhura.aston.ac.uk>
 * 
 * Fixes:	
 *		Alan Cox:	check_addr returns a value for a wrong subnet
 *				ie not us but don't forward this!
 *		Alan Cox:	block timer if the inet_bh handler is running
 *		Alan Cox:	generic queue code added. A lot neater now
 *		C.E.Hawkins:	SIOCGIFCONF only reports 'upped' interfaces
 *		C.E.Hawkins:	IFF_PROMISC support
 *		Alan Cox:	Supports Donald Beckers new hardware 
 *				multicast layer, but not yet multicast lists.
 *		Alan Cox:	ip_addr_match problems with class A/B nets.
 *		C.E.Hawkins	IP 0.0.0.0 and also same net route fix. [FIXME: Ought to cause ICMP_REDIRECT]
 *		Alan Cox:	Removed bogus subnet check now the subnet code
 *				a) actually works for all A/B nets
 *				b) doesn't forward off the same interface.
 *		Alan Cox:	Multiple extra protocols
 *		Alan Cox:	A Couple more escaped verify_area calls die
 *		Alan Cox:	IP_SET_DEV is gone (forever) as per Fred's comment.
 *		Alan Cox:	Grand tidy up ready for the big day.
 *		Alan Cox:	Handles dev_open errors correctly.
 *		Alan Cox:	IP and generic parts split
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <asm/segment.h>
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
#include <linux/in.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/if_ether.h>
#include "inet.h"
#include "devinet.h"
#include "eth.h"
#include "ip.h"
#include "route.h"
#include "protocol.h"
#include "tcp.h"
#include "skbuff.h"


/* 
 *	Determine a default network mask, based on the IP address.
 */

unsigned long ip_get_mask(unsigned long addr)
{
  	unsigned long dst;

  	if (addr == 0L) 
  		return(0L);	/* special case */

  	dst = ntohl(addr);
  	if (IN_CLASSA(dst)) 
  		return(htonl(IN_CLASSA_NET));
  	if (IN_CLASSB(dst)) 
  		return(htonl(IN_CLASSB_NET));
  	if (IN_CLASSC(dst)) 
  		return(htonl(IN_CLASSC_NET));
  	
  	/* Something else, probably a subnet. */
  	return(0);
}

/*
 *	See if a pair of addresses match.
 */

int ip_addr_match(unsigned long me, unsigned long him)
{
  	int i;
  	unsigned long mask=0xFFFFFFFF;
  	DPRINTF((DBG_DEV, "ip_addr_match(%s, ", in_ntoa(me)));
  	DPRINTF((DBG_DEV, "%s)\n", in_ntoa(him)));

	/* Fast path for 99.9% of cases */
  	if (me == him) 
  		return(1);
  		
  	for (i = 0; i < 4; i++, me >>= 8, him >>= 8, mask >>= 8) 
  	{
		if ((me & 0xFF) != (him & 0xFF)) 
		{
			/*
			 * The only way this could be a match is for
			 * the rest of addr1 to be 0 or 255.
			 */
			if (me != 0 && me != mask) 
				return(0);
			return(1);
		}
  	}
  	return(1);
}


/*
 *	Check the address for our address, broadcasts, etc. 
 *
 *	This routine is used a lot, and in many time critical
 *	places. It's already _TOO_ slow so be careful how you
 *	alter it.
 */
 
int chk_addr(unsigned long addr)
{
  	struct device *dev;
  	unsigned long dst;

  	DPRINTF((DBG_DEV, "chk_addr(%s) --> ", in_ntoa(addr)));
  	dst = ntohl(addr);

  	/*
  	 * Accept both `all ones' and `all zeros' as BROADCAST. 
  	 * All 0's is the old BSD broadcast.
  	 */
  	 
  	if (dst == INADDR_ANY || dst == INADDR_BROADCAST) 
  	{
		DPRINTF((DBG_DEV, "BROADCAST\n"));
		return(IS_BROADCAST);
	}

  	/* Accept all of the `loopback' class A net. */
  	if ((dst & IN_CLASSA_NET) == 0x7F000000L) 
  	{
		DPRINTF((DBG_DEV, "LOOPBACK\n"));

		/*
		 * We force `loopback' to be equal to MY_ADDR.
		 */
		return(IS_MYADDR);
		/* return(IS_LOOPBACK); */
  	}

  	/* OK, now check the interface addresses. */
  	for (dev = dev_base; dev != NULL; dev = dev->next) 
  	{
        	if (!(dev->flags&IFF_UP))
		        continue;
		if ((dev->pa_addr == 0)/* || (dev->flags&IFF_PROMISC)*/)
	        	return(IS_MYADDR);
		/* Is it the exact IP address? */
		if (addr == dev->pa_addr) 
		{
			DPRINTF((DBG_DEV, "MYADDR\n"));
			return(IS_MYADDR);
		}

		/* Nope. Check for a subnetwork broadcast. */
		if ((addr & dev->pa_mask) == (dev->pa_addr & dev->pa_mask)) 
		{
			if ((addr & ~dev->pa_mask) == 0) 
			{
				DPRINTF((DBG_DEV, "SUBBROADCAST-0\n"));
				return(IS_BROADCAST);
			}
			if (((addr & ~dev->pa_mask) | dev->pa_mask)
						== INADDR_BROADCAST) 
			{
				DPRINTF((DBG_DEV, "SUBBROADCAST-1\n"));
				return(IS_BROADCAST);
			}
		}

		/* Nope. Check for Network broadcast. */
		if(IN_CLASSA(dst)) 
		{
	  		if( addr == (dev->pa_addr | 0xffffff00)) 
	  		{
	    			DPRINTF((DBG_DEV, "CLASS A BROADCAST-1\n"));
	   			return(IS_BROADCAST);
	  		}
		}
		else if(IN_CLASSB(dst)) 
		{
	  		if( addr == (dev->pa_addr | 0xffff0000)) 
	  		{
	    			DPRINTF((DBG_DEV, "CLASS B BROADCAST-1\n"));
	    			return(IS_BROADCAST);
	  		}
		}
		else 
		{   /* IN_CLASSC */
	  		if( addr == (dev->pa_addr | 0xff000000)) 
	  		{
	    			DPRINTF((DBG_DEV, "CLASS C BROADCAST-1\n"));
	    			return(IS_BROADCAST);
	 		}
		}
  	}

  	DPRINTF((DBG_DEV, "NONE\n"));
  
  	return(0);		/* no match at all */
}


/*
 * Retrieve our own address.
 * Because the loopback address (127.0.0.1) is already recognized
 * automatically, we can use the loopback interface's address as
 * our "primary" interface.  This is the addressed used by IP et
 * al when it doesn't know which address to use (i.e. it does not
 * yet know from or to which interface to go...).
 */

unsigned long my_addr(void)
{
  	struct device *dev;

  	for (dev = dev_base; dev != NULL; dev = dev->next) 
  	{
		if (dev->flags & IFF_LOOPBACK) 
			return(dev->pa_addr);
  	}
  	return(0);
}



/*
 *	Find an interface that can handle addresses for a certain address. 
 */
 
struct device *dev_check(unsigned long addr)
{
  	struct device *dev;

  	for (dev = dev_base; dev; dev = dev->next)
		if ((dev->flags & IFF_UP) && (dev->flags & IFF_POINTOPOINT) &&
	    		(addr == dev->pa_dstaddr))
			return dev;
	for (dev = dev_base; dev; dev = dev->next)
		if ((dev->flags & IFF_UP) && !(dev->flags & IFF_POINTOPOINT) &&
	    		(dev->flags & IFF_LOOPBACK ? (addr == dev->pa_addr) :
	    		(dev->pa_mask & addr) == (dev->pa_addr & dev->pa_mask)))
			break;
  /* no need to check broadcast addresses */
	return dev;
}

