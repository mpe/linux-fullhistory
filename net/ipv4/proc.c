/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		This file implements the various access functions for the
 *		PROC file system.  It is mainly used for debugging and
 *		statistics.
 *
 * Version:	$Id: proc.c,v 1.36 1999/07/02 11:26:34 davem Exp $
 *
 * Authors:	Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Gerald J. Heim, <heim@peanuts.informatik.uni-tuebingen.de>
 *		Fred Baumgarten, <dc6iq@insu1.etec.uni-karlsruhe.de>
 *		Erik Schoenfelder, <schoenfr@ibr.cs.tu-bs.de>
 *
 * Fixes:
 *		Alan Cox	:	UDP sockets show the rxqueue/txqueue
 *					using hint flag for the netinfo.
 *	Pauline Middelink	:	identd support
 *		Alan Cox	:	Make /proc safer.
 *	Erik Schoenfelder	:	/proc/net/snmp
 *		Alan Cox	:	Handle dead sockets properly.
 *	Gerhard Koerting	:	Show both timers
 *		Alan Cox	:	Allow inode to be NULL (kernel socket)
 *	Andi Kleen		:	Add support for open_requests and 
 *					split functions for more readibility.
 *	Andi Kleen		:	Add support for /proc/net/netstat
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#include <asm/system.h>
#include <linux/sched.h>
#include <linux/socket.h>
#include <linux/net.h>
#include <linux/un.h>
#include <linux/in.h>
#include <linux/param.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <net/ip.h>
#include <net/icmp.h>
#include <net/protocol.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/raw.h>

/*
 *	Report socket allocation statistics [mea@utu.fi]
 */
int afinet_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	/* From  net/socket.c  */
	extern int socket_get_info(char *, char **, off_t, int);

	int len  = socket_get_info(buffer,start,offset,length);

	len += sprintf(buffer+len,"TCP: inuse %d highest %d\n",
		       tcp_prot.inuse, tcp_prot.highestinuse);
	len += sprintf(buffer+len,"UDP: inuse %d highest %d\n",
		       udp_prot.inuse, udp_prot.highestinuse);
	len += sprintf(buffer+len,"RAW: inuse %d highest %d\n",
		       raw_prot.inuse, raw_prot.highestinuse);
	if (offset >= len)
	{
		*start = buffer;
		return 0;
	}
	*start = buffer + offset;
	len -= offset;
	if (len > length)
		len = length;
	if (len < 0)
		len = 0;
	return len;
}


/* 
 *	Called from the PROCfs module. This outputs /proc/net/snmp.
 */
 
int snmp_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	extern struct tcp_mib tcp_statistics;
	extern struct udp_mib udp_statistics;
	int len;
/*
  extern unsigned long tcp_rx_miss, tcp_rx_hit1,tcp_rx_hit2;
*/

	len = sprintf (buffer,
		"Ip: Forwarding DefaultTTL InReceives InHdrErrors InAddrErrors ForwDatagrams InUnknownProtos InDiscards InDelivers OutRequests OutDiscards OutNoRoutes ReasmTimeout ReasmReqds ReasmOKs ReasmFails FragOKs FragFails FragCreates\n"
		"Ip: %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu\n",
		    ip_statistics.IpForwarding, ip_statistics.IpDefaultTTL, 
		    ip_statistics.IpInReceives, ip_statistics.IpInHdrErrors, 
		    ip_statistics.IpInAddrErrors, ip_statistics.IpForwDatagrams, 
		    ip_statistics.IpInUnknownProtos, ip_statistics.IpInDiscards, 
		    ip_statistics.IpInDelivers, ip_statistics.IpOutRequests, 
		    ip_statistics.IpOutDiscards, ip_statistics.IpOutNoRoutes, 
		    ip_statistics.IpReasmTimeout, ip_statistics.IpReasmReqds, 
		    ip_statistics.IpReasmOKs, ip_statistics.IpReasmFails, 
		    ip_statistics.IpFragOKs, ip_statistics.IpFragFails, 
		    ip_statistics.IpFragCreates);
		    		
	len += sprintf (buffer + len,
		"Icmp: InMsgs InErrors InDestUnreachs InTimeExcds InParmProbs InSrcQuenchs InRedirects InEchos InEchoReps InTimestamps InTimestampReps InAddrMasks InAddrMaskReps OutMsgs OutErrors OutDestUnreachs OutTimeExcds OutParmProbs OutSrcQuenchs OutRedirects OutEchos OutEchoReps OutTimestamps OutTimestampReps OutAddrMasks OutAddrMaskReps\n"
		"Icmp: %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu\n",
		    icmp_statistics.IcmpInMsgs, icmp_statistics.IcmpInErrors,
		    icmp_statistics.IcmpInDestUnreachs, icmp_statistics.IcmpInTimeExcds,
		    icmp_statistics.IcmpInParmProbs, icmp_statistics.IcmpInSrcQuenchs,
		    icmp_statistics.IcmpInRedirects, icmp_statistics.IcmpInEchos,
		    icmp_statistics.IcmpInEchoReps, icmp_statistics.IcmpInTimestamps,
		    icmp_statistics.IcmpInTimestampReps, icmp_statistics.IcmpInAddrMasks,
		    icmp_statistics.IcmpInAddrMaskReps, icmp_statistics.IcmpOutMsgs,
		    icmp_statistics.IcmpOutErrors, icmp_statistics.IcmpOutDestUnreachs,
		    icmp_statistics.IcmpOutTimeExcds, icmp_statistics.IcmpOutParmProbs,
		    icmp_statistics.IcmpOutSrcQuenchs, icmp_statistics.IcmpOutRedirects,
		    icmp_statistics.IcmpOutEchos, icmp_statistics.IcmpOutEchoReps,
		    icmp_statistics.IcmpOutTimestamps, icmp_statistics.IcmpOutTimestampReps,
		    icmp_statistics.IcmpOutAddrMasks, icmp_statistics.IcmpOutAddrMaskReps);
	
	len += sprintf (buffer + len,
		"Tcp: RtoAlgorithm RtoMin RtoMax MaxConn ActiveOpens PassiveOpens AttemptFails EstabResets CurrEstab InSegs OutSegs RetransSegs InErrs OutRsts\n"
		"Tcp: %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu\n",
		    tcp_statistics.TcpRtoAlgorithm, tcp_statistics.TcpRtoMin,
		    tcp_statistics.TcpRtoMax, tcp_statistics.TcpMaxConn,
		    tcp_statistics.TcpActiveOpens, tcp_statistics.TcpPassiveOpens,
		    tcp_statistics.TcpAttemptFails, tcp_statistics.TcpEstabResets,
		    tcp_statistics.TcpCurrEstab, tcp_statistics.TcpInSegs,
		    tcp_statistics.TcpOutSegs, tcp_statistics.TcpRetransSegs,
		    tcp_statistics.TcpInErrs, tcp_statistics.TcpOutRsts);
		
	len += sprintf (buffer + len,
		"Udp: InDatagrams NoPorts InErrors OutDatagrams\nUdp: %lu %lu %lu %lu\n",
		    udp_statistics.UdpInDatagrams, udp_statistics.UdpNoPorts,
		    udp_statistics.UdpInErrors, udp_statistics.UdpOutDatagrams);	    
/*	
	  len += sprintf( buffer + len,
	  	"TCP fast path RX:  H2: %ul H1: %ul L: %ul\n",
	  		tcp_rx_hit2,tcp_rx_hit1,tcp_rx_miss);
*/
	
	if (offset >= len)
	{
		*start = buffer;
		return 0;
	}
	*start = buffer + offset;
	len -= offset;
	if (len > length)
		len = length;
	if (len < 0)
		len = 0; 
	return len;
}

/* 
 *	Output /proc/net/netstat
 */
 
int netstat_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	extern struct linux_mib net_statistics;
	int len;

	len = sprintf(buffer,
		      "TcpExt: SyncookiesSent SyncookiesRecv SyncookiesFailed"
		      " EmbryonicRsts PruneCalled RcvPruned OfoPruned"
		      " OutOfWindowIcmps LockDroppedIcmps\n" 	
		      "TcpExt: %lu %lu %lu %lu %lu %lu %lu %lu %lu\n",
		      net_statistics.SyncookiesSent,
		      net_statistics.SyncookiesRecv,
		      net_statistics.SyncookiesFailed,
		      net_statistics.EmbryonicRsts,
		      net_statistics.PruneCalled,
		      net_statistics.RcvPruned,
		      net_statistics.OfoPruned,
		      net_statistics.OutOfWindowIcmps,
		      net_statistics.LockDroppedIcmps);

	if (offset >= len)
	{
		*start = buffer;
		return 0;
	}
	*start = buffer + offset;
	len -= offset;
	if (len > length)
		len = length;
	if (len < 0)
		len = 0; 
	return len;
}
