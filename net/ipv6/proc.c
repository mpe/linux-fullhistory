/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		This file implements the various access functions for the
 *		PROC file system.  This is very similar to the IPv4 version,
 *		except it reports the sockets in the INET6 address family.
 *
 * Version:	$Id: proc.c,v 1.9 1998/08/26 12:05:11 davem Exp $
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
#include <linux/stddef.h>
#include <net/sock.h>
#include <net/tcp.h>
#include <net/transp_v6.h>
#include <net/ipv6.h>

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
		struct tcp_tw_bucket *tw = (struct tcp_tw_bucket *)sp;
		int tw_bucket = 0;

		pos += 149;
		if(pos < offset)
			goto next;
		tp = &(sp->tp_pinfo.af_tcp);
		if((format == 0) && (sp->state == TCP_TIME_WAIT)) {
			tw_bucket = 1;
			dest  = &tw->v6_daddr;
			src   = &tw->v6_rcv_saddr;
		} else {
			dest  = &sp->net_pinfo.af_inet6.daddr;
			src   = &sp->net_pinfo.af_inet6.rcv_saddr;
		}
		destp = ntohs(sp->dport);
		srcp  = ntohs(sp->sport);
		if((format == 0) && (sp->state == TCP_TIME_WAIT)) {
			extern int tcp_tw_death_row_slot;
			int slot_dist;

			timer_active1	= timer_active2 = 0;
			timer_active	= 3;
			slot_dist	= tw->death_slot;
			if(slot_dist > tcp_tw_death_row_slot)
				slot_dist = (TCP_TWKILL_SLOTS - slot_dist) + tcp_tw_death_row_slot;
			else
				slot_dist = tcp_tw_death_row_slot - slot_dist;
			timer_expires	= jiffies + (slot_dist * TCP_TWKILL_PERIOD);
		} else {
			timer_active1 = del_timer(&tp->retransmit_timer);
			timer_active2 = del_timer(&sp->timer);
			if(!timer_active1) tp->retransmit_timer.expires = 0;
			if(!timer_active2) sp->timer.expires = 0;
			timer_active = 0;
			timer_expires = (unsigned) -1;
		}
		if(timer_active1 && tp->retransmit_timer.expires < timer_expires) {
			timer_active = timer_active1;
			timer_expires = tp->retransmit_timer.expires;
		}
		if(timer_active2 && sp->timer.expires < timer_expires) {
			timer_active = timer_active2;
			timer_expires = sp->timer.expires;
		}
		if(timer_active == 0)
			timer_expires = jiffies;
		sprintf(tmpbuf, "%4d: %08X%08X%08X%08X:%04X %08X%08X%08X%08X:%04X "
			"%02X %08X:%08X %02X:%08lX %08X %5d %8d %ld",
			i,
			src->s6_addr32[0], src->s6_addr32[1],
			src->s6_addr32[2], src->s6_addr32[3], srcp,
			dest->s6_addr32[0], dest->s6_addr32[1],
			dest->s6_addr32[2], dest->s6_addr32[3], destp,
			sp->state,
			(tw_bucket ?
			 0 :
			 (format == 0) ?
			 tp->write_seq-tp->snd_una :
			 atomic_read(&sp->wmem_alloc)),
			(tw_bucket ?
			 0 :
			 (format == 0) ?
			 tp->rcv_nxt-tp->copied_seq :
			 atomic_read(&sp->rmem_alloc)),
			timer_active, timer_expires-jiffies,
			(tw_bucket ? 0 : tp->retransmits),
			((!tw_bucket && sp->socket) ?
			 sp->socket->inode->i_uid : 0),
			(!tw_bucket && timer_active) ? sp->timeout : 0,
			((!tw_bucket && sp->socket) ?
			 sp->socket->inode->i_ino : 0));

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


struct snmp6_item
{
	char *name;
	unsigned long *ptr;
} snmp6_list[] = {
/* ipv6 mib according to draft-ietf-ipngwg-ipv6-mib-04 */
#define SNMP6_GEN(x) { #x , &ipv6_statistics.x }
	SNMP6_GEN(Ip6InReceives),
	SNMP6_GEN(Ip6InHdrErrors),
	SNMP6_GEN(Ip6InTooBigErrors),
	SNMP6_GEN(Ip6InNoRoutes),
	SNMP6_GEN(Ip6InAddrErrors),
	SNMP6_GEN(Ip6InUnknownProtos),
	SNMP6_GEN(Ip6InTruncatedPkts),
	SNMP6_GEN(Ip6InDiscards),
	SNMP6_GEN(Ip6InDelivers),
	SNMP6_GEN(Ip6OutForwDatagrams),
	SNMP6_GEN(Ip6OutRequests),
	SNMP6_GEN(Ip6OutDiscards),
	SNMP6_GEN(Ip6OutNoRoutes),
	SNMP6_GEN(Ip6ReasmTimeout),
	SNMP6_GEN(Ip6ReasmReqds),
	SNMP6_GEN(Ip6ReasmOKs),
	SNMP6_GEN(Ip6ReasmFails),
	SNMP6_GEN(Ip6FragOKs),
	SNMP6_GEN(Ip6FragFails),
	SNMP6_GEN(Ip6FragCreates),
	SNMP6_GEN(Ip6InMcastPkts),
	SNMP6_GEN(Ip6OutMcastPkts),
#undef SNMP6_GEN
/* icmpv6 mib according to draft-ietf-ipngwg-ipv6-icmp-mib-02

   Exceptions:  {In|Out}AdminProhibs are removed, because I see
                no good reasons to account them separately
		of another dest.unreachs.
		OutErrs is zero identically.
		OutEchos too.
		OutRouterAdvertisements too.
		OutGroupMembQueries too.
 */
#define SNMP6_GEN(x) { #x , &icmpv6_statistics.x }
	SNMP6_GEN(Icmp6InMsgs),
	SNMP6_GEN(Icmp6InErrors),
	SNMP6_GEN(Icmp6InDestUnreachs),
	SNMP6_GEN(Icmp6InPktTooBigs),
	SNMP6_GEN(Icmp6InTimeExcds),
	SNMP6_GEN(Icmp6InParmProblems),
	SNMP6_GEN(Icmp6InEchos),
	SNMP6_GEN(Icmp6InEchoReplies),
	SNMP6_GEN(Icmp6InGroupMembQueries),
	SNMP6_GEN(Icmp6InGroupMembResponses),
	SNMP6_GEN(Icmp6InGroupMembReductions),
	SNMP6_GEN(Icmp6InRouterSolicits),
	SNMP6_GEN(Icmp6InRouterAdvertisements),
	SNMP6_GEN(Icmp6InNeighborSolicits),
	SNMP6_GEN(Icmp6InNeighborAdvertisements),
	SNMP6_GEN(Icmp6InRedirects),
	SNMP6_GEN(Icmp6OutMsgs),
	SNMP6_GEN(Icmp6OutDestUnreachs),
	SNMP6_GEN(Icmp6OutPktTooBigs),
	SNMP6_GEN(Icmp6OutTimeExcds),
	SNMP6_GEN(Icmp6OutParmProblems),
	SNMP6_GEN(Icmp6OutEchoReplies),
	SNMP6_GEN(Icmp6OutRouterSolicits),
	SNMP6_GEN(Icmp6OutNeighborSolicits),
	SNMP6_GEN(Icmp6OutNeighborAdvertisements),
	SNMP6_GEN(Icmp6OutRedirects),
	SNMP6_GEN(Icmp6OutGroupMembResponses),
	SNMP6_GEN(Icmp6OutGroupMembReductions),
#undef SNMP6_GEN
#define SNMP6_GEN(x) { "Udp6" #x , &udp_stats_in6.Udp##x }
	SNMP6_GEN(InDatagrams),
	SNMP6_GEN(NoPorts),
	SNMP6_GEN(InErrors),
	SNMP6_GEN(OutDatagrams)
#undef SNMP6_GEN
};


int afinet6_get_snmp(char *buffer, char **start, off_t offset, int length,
		     int dummy)
{
	int len = 0;
	int i;

	for (i=0; i<sizeof(snmp6_list)/sizeof(snmp6_list[0]); i++)
		len += sprintf(buffer+len, "%-32s\t%ld\n", snmp6_list[i].name,
			       *(snmp6_list[i].ptr));

	len -= offset;

	if (len > length)
		len = length;
	if(len < 0)
		len = 0;

	*start = buffer + offset;

	return len;
}
