/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		This file implements the various access functions for the
 *		PROC file system.  This is very similar to the IPv4 version,
 *		except it reports the sockets in the INET6 address family.
 *
 * Version:	$Id: proc.c,v 1.4 1997/04/20 22:50:44 schenk Exp $
 *
 * Authors:	David S. Miller (davem@caip.rutgers.edu)
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#include <linux/sched.h>
#include <linux/socket.h>
#include <linux/net.h>
#include <linux/in6.h>
#include <net/sock.h>
#include <net/transp_v6.h>

/* This is the main implementation workhorse of all these routines. */
static int get__netinfo6(struct proto *pro, char *buffer, int format, char **start,
			 off_t offset, int length)
{
	struct sock *sp;
	struct tcp_opt *tp;
	int timer_active, timer_active1, timer_active2;
	unsigned long timer_expires;
	struct in6_addr *dest, *src;
	unsigned short destp, srcp;
	int len = 0, i = 0;
	off_t pos = 0;
	off_t begin;
	char tmpbuf[150];

	if(offset < 149)
		len += sprintf(buffer, "%-148s\n",
			       "  sl  "						/* 6 */
			       "local_address                         "		/* 38 */
			       "remote_address                        "		/* 38 */
			       "st tx_queue rx_queue tr tm->when retrnsmt"	/* 41 */
			       "   uid  timeout inode");			/* 21 */
										/*----*/
										/*144 */

	pos = 149;
	SOCKHASH_LOCK();
	sp = pro->sklist_next;
	while(sp != (struct sock *)pro) {
		pos += 149;
		if(pos < offset)
			goto next;
		tp = &(sp->tp_pinfo.af_tcp);
		dest  = &sp->net_pinfo.af_inet6.daddr;
		src   = &sp->net_pinfo.af_inet6.rcv_saddr;
		destp = ntohs(sp->dummy_th.dest);
		srcp  = ntohs(sp->dummy_th.source);

		timer_active1 = del_timer(&tp->retransmit_timer);
		timer_active2 = del_timer(&sp->timer);
		if(!timer_active1) tp->retransmit_timer.expires = 0;
		if(!timer_active2) sp->timer.expires = 0;
		timer_active = 0;
		timer_expires = (unsigned) -1;
		if(timer_active1 && tp->retransmit_timer.expires < timer_expires) {
			timer_active = timer_active1;
			timer_expires = tp->retransmit_timer.expires;
		}
		if(timer_active2 && sp->timer.expires < timer_expires) {
			timer_active = timer_active2;
			timer_expires = sp->timer.expires;
		}
		sprintf(tmpbuf, "%4d: %08X%08X%08X%08X:%04X %08X%08X%08X%08X:%04X "
			"%02X %08X:%08X %02X:%08lX %08X %5d %8d %ld",
			i,
			src->s6_addr32[0], src->s6_addr32[1],
			src->s6_addr32[2], src->s6_addr32[3], srcp,
			dest->s6_addr32[0], dest->s6_addr32[1],
			dest->s6_addr32[2], dest->s6_addr32[3], destp,
			sp->state,
			format==0?sp->write_seq-tp->snd_una:atomic_read(&sp->wmem_alloc),
			format==0?tp->rcv_nxt-sp->copied_seq:atomic_read(&sp->rmem_alloc),
			timer_active, timer_expires-jiffies,
			tp->retransmits,
			sp->socket ? sp->socket->inode->i_uid:0,
			timer_active?sp->timeout:0,
			sp->socket ? sp->socket->inode->i_ino:0);

		if(timer_active1) add_timer(&tp->retransmit_timer);
		if(timer_active2) add_timer(&sp->timer);
		len += sprintf(buffer+len, "%-148s\n", tmpbuf);
		if(len >= length)
			break;
	next:
		sp = sp->sklist_next;
		i++;
	}
	SOCKHASH_UNLOCK();

	begin = len - (pos - offset);
	*start = buffer + begin;
	len -= begin;
	if(len > length)
		len = length;
	return len;
}

/* These get exported and registered with procfs in af_inet6.c at init time. */
int tcp6_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	return get__netinfo6(&tcpv6_prot, buffer, 0, start, offset, length);
}

int udp6_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	return get__netinfo6(&udpv6_prot, buffer, 1, start, offset, length);
}

int raw6_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	return get__netinfo6(&rawv6_prot, buffer, 1, start, offset, length);
}

int afinet6_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	int len = 0;
	len += sprintf(buffer+len, "TCP6: inuse %d highest %d\n",
		       tcpv6_prot.inuse, tcpv6_prot.highestinuse);
	len += sprintf(buffer+len, "UDP6: inuse %d highest %d\n",
		       udpv6_prot.inuse, udpv6_prot.highestinuse);
	len += sprintf(buffer+len, "RAW6: inuse %d highest %d\n",
		       rawv6_prot.inuse, rawv6_prot.highestinuse);
	*start = buffer + offset;
	len -= offset;
	if(len > length)
		len = length;
	return len;
}
