/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		This file implements the various access functions for the
 *		PROC file system.  It is mainly used for debugging and
 *		statistics.
 *
 * Version:	@(#)proc.c	1.0.5	05/27/93
 *
 * Authors:	Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Gerald J. Heim, <heim@peanuts.informatik.uni-tuebingen.de>
 *		Fred Baumgarten, <dc6iq@insu1.etec.uni-karlsruhe.de>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#include <asm/system.h>
#include <linux/autoconf.h>
#include <linux/sched.h>
#include <linux/socket.h>
#include <linux/net.h>
#include <linux/un.h>
#include <linux/in.h>
#include <linux/param.h>
#include "inet.h"
#include "timer.h"
#include "dev.h"
#include "ip.h"
#include "protocol.h"
#include "tcp.h"
#include "udp.h"
#include "skbuff.h"
#include "sock.h"
#include "raw.h"

extern struct timer *timer_base;

/*
 * Get__netinfo returns the length of that string.
 *
 * KNOWN BUGS
 *  As in get_unix_netinfo, the buffer might be too small. If this
 *  happens, get__netinfo returns only part of the available infos.
 */
static int
get__netinfo(struct proto *pro, char *buffer)
{
  struct sock **s_array;
  struct sock *sp;
  char *pos=buffer;
  int i;
  unsigned long  dest, src;
  unsigned short destp, srcp;
  struct timer *tp;

  s_array = pro->sock_array;
  pos+=sprintf(pos, "sl  local_address rem_address   st tx_queue rx_queue tr tm->when\n");
  for(i = 0; i < SOCK_ARRAY_SIZE; i++) {
	sp = s_array[i];
	while(sp != NULL) {
		dest  = sp->daddr;
		src   = sp->saddr;
		destp = sp->dummy_th.dest;
		srcp  = sp->dummy_th.source;

		/* Since we are Little Endian we need to swap the bytes :-( */
		destp = ntohs(destp);
		srcp  = ntohs(srcp);

		pos+=sprintf(pos, "%2d: %08X:%04X %08X:%04X %02X %08X:%08X %02X:%08X %02X",
			i, src, srcp, dest, destp, sp->state, 
			sp->send_seq-sp->rcv_ack_seq, sp->acked_seq-sp->copied_seq,
			sp->time_wait.running, sp->time_wait.when-jiffies, sp->retransmits);

		cli();
		tp = timer_base;
		while (tp) {
			if (tp == &(sp->time_wait)) {
				pos+=sprintf(pos, " *");
			}
			tp = tp->next;
		}
		sti();
		pos+=sprintf(pos, "\n");

		/* Is place in buffer too rare? then abort. */
		if (pos > buffer+PAGE_SIZE-80) {
			printk("oops, too many %s sockets for netinfo.\n",
					pro->name);
			return(strlen(buffer));
		}

		/*
		 * All sockets with (port mod SOCK_ARRAY_SIZE) = i
		 * are kept in sock_array[i], so we must follow the
		 * 'next' link to get them all.
		 */
		sp = sp->next;
	}
  }
  return(strlen(buffer));
} 


int tcp_get_info(char *buffer)
{
  return get__netinfo(&tcp_prot, buffer);
}


int udp_get_info(char *buffer)
{
  return get__netinfo(&udp_prot, buffer);
}


int raw_get_info(char *buffer)
{
  return get__netinfo(&raw_prot, buffer);
}
