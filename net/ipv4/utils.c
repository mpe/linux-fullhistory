/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Various kernel-resident INET utility functions; mainly
 *		for format conversion and debugging output.
 *
 * Version:	@(#)utils.c	1.0.7	05/18/93
 *
 * Author:	Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 * Fixes:
 *		Alan Cox	:	verify_area check.
 *		Alan Cox	:	removed old debugging.
 *		Andi Kleen	:	add net_ratelimit()  
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <stdarg.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <net/tcp.h>
#include <linux/skbuff.h>


/*
 *	Display an IP address in readable format. 
 */
 
char *in_ntoa(__u32 in)
{
	static char buff[18];
	char *p;

	p = (char *) &in;
	sprintf(buff, "%d.%d.%d.%d",
		(p[0] & 255), (p[1] & 255), (p[2] & 255), (p[3] & 255));
	return(buff);
}


/*
 *	Convert an ASCII string to binary IP. 
 */
 
__u32 in_aton(const char *str)
{
	unsigned long l;
	unsigned int val;
	int i;

	l = 0;
	for (i = 0; i < 4; i++) 
	{
		l <<= 8;
		if (*str != '\0') 
		{
			val = 0;
			while (*str != '\0' && *str != '.') 
			{
				val *= 10;
				val += *str - '0';
				str++;
			}
			l |= val;
			if (*str != '\0') 
				str++;
		}
	}
	return(htonl(l));
}

/* 
 * This enforces a rate limit: not more than one kernel message
 * every 5secs to make a denial-of-service attack impossible.
 *
 * All warning printk()s should be guarded by this function. 
 */ 
int net_ratelimit(void)
{
	static unsigned long last_msg; 
	static int missed; 
	
	if ((jiffies - last_msg) >= 5*HZ) {
		if (missed)	
			printk(KERN_WARNING "ipv4: (%d messages suppressed. Flood?)\n", missed);
		missed = 0; 
		last_msg = jiffies;
		return 1;
	}
	missed++; 
	return 0; 
}
