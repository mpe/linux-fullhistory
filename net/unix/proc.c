/*
 * UNIX		An implementation of the AF_UNIX network domain for the
 *		LINUX operating system.  UNIX is implemented using the
 *		BSD Socket interface as the means of communication with
 *		the user level.
 *
 *		The functions in this file provide an interface between
 *		the PROC file system and the "unix" family of networking
 *		protocols. It is mainly used for debugging and statistics.
 *
 * Version:	@(#)proc.c	1.0.4	05/23/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Gerald J. Heim, <heim@peanuts.informatik.uni-tuebingen.de>
 *		Fred Baumgarten, <dc6iq@insu1.etec.uni-kalrsruhe.de>
 *
 * Fixes:
 *		Dmitry Gorodchanin	:	/proc locking fix
 *		Mathijs Maassen		:	unbound /proc fix.
 *		Alan Cox		:	Fix sock=NULL race
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#include <linux/autoconf.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/net.h>
#include <linux/un.h>
#include <linux/param.h>
#include "unix.h"


/* Called from PROCfs. */
int unix_get_info(char *buffer, char **start, off_t offset, int length)
{
  	off_t pos=0;
  	off_t begin=0;
  	int len=0;
  	int i;
  	unsigned long flags;
	socket_state s_state;
	short s_type;
	long s_flags;
	
  	len += sprintf(buffer, "Num RefCount Protocol Flags    Type St Path\n");

  	for(i = 0; i < NSOCKETS_UNIX; i++) 
  	{
  		save_flags(flags);
  		cli();
		if (unix_datas[i].refcnt>0 && unix_datas[i].socket!=NULL)
		{
			/* sprintf is slow... lock only for the variable reads */
			s_type=unix_datas[i].socket->type;
			s_flags=unix_datas[i].socket->flags;
			s_state=unix_datas[i].socket->state;
			restore_flags(flags);
			len += sprintf(buffer+len, "%2d: %08X %08X %08lX %04X %02X", i,
				unix_datas[i].refcnt,
				unix_datas[i].protocol,
				s_flags,
				s_type,
				s_state
			);

			/* If socket is bound to a filename, we'll print it. */
			if(unix_datas[i].sockaddr_len>0) 
			{
				len += sprintf(buffer+len, " %s\n",
				unix_datas[i].sockaddr_un.sun_path);
			} 
			else 
			{ /* just add a newline */
				buffer[len++]='\n';
			}
			
			pos=begin+len;
			if(pos<offset)
			{
				len=0;
				begin=pos;
			}
			if(pos>offset+length)
				break;
		}
		else
			restore_flags(flags);
	}
	
	*start=buffer+(offset-begin);
	len-=(offset-begin);
	if(len>length)
		len=length;
	return len;
}
