/*
 *	NET3:	Implementation of the ICMP protocol layer. 
 *	
 *		Alan Cox, <alan@cymru.net>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	Some of the function names and the icmp unreach table for this
 *	module were derived from [icmp.c 1.0.11 06/02/93] by
 *	Ross Biro, Fred N. van Kempen, Mark Evans, Alan Cox, Gerhard Koerting.
 *	Other than that this module is a complete rewrite.
 *
 *	Fixes:
 *		Mike Shaver	:	RFC1122 checks.
 *		Alan Cox	:	Multicast ping reply as self.
 *		Alan Cox	:	Fix atomicity lockup in ip_build_xmit 
 *					call.
 *		Alan Cox	:	Added 216,128 byte paths to the MTU 
 *					code.
 *		Martin Mares	:	RFC1812 checks.
 *		Martin Mares	:	Can be configured to follow redirects 
 *					if acting as a router _without_ a
 *					routing protocol (RFC 1812).
 *		Martin Mares	:	Echo requests may be configured to 
 *					be ignored (RFC 1812).
 *		Martin Mares	:	Limitation of ICMP error message 
 *					transmit rate (RFC 1812).
 *		Martin Mares	:	TOS and Precedence set correctly 
 *					(RFC 1812).
 *		Martin Mares	:	Now copying as much data from the 
 *					original packet as we can without
 *					exceeding 576 bytes (RFC 1812).
 *	Willy Konynenberg	:	Transparent proxying support.
 *		Keith Owens	:	RFC1191 correction for 4.2BSD based 
 *					path MTU bug.
 *
 *
 * RFC1122 (Host Requirements -- Comm. Layer) Status:
 * (boy, are there a lot of rules for ICMP)
 *  3.2.2 (Generic ICMP stuff)
 *   MUST discard messages of unknown type. (OK)
 *   MUST copy at least the first 8 bytes from the offending packet
 *     when sending ICMP errors. (OBSOLETE -- see RFC1812)
 *   MUST pass received ICMP errors up to protocol level. (OK)
 *   SHOULD send ICMP errors with TOS == 0. (OBSOLETE -- see RFC1812)
 *   MUST NOT send ICMP errors in reply to:
 *     ICMP errors (OK)
 *     Broadcast/multicast datagrams (OK)
 *     MAC broadcasts (OK)
 *     Non-initial fragments (OK)
 *     Datagram with a source address that isn't a single host. (OK)
 *  3.2.2.1 (Destination Unreachable)
 *   All the rules govern the IP layer, and are dealt with in ip.c, not here.
 *  3.2.2.2 (Redirect)
 *   Host SHOULD NOT send ICMP_REDIRECTs.  (OK)
 *   MUST update routing table in response to host or network redirects.
 *     (host OK, network OBSOLETE)
 *   SHOULD drop redirects if they're not from directly connected gateway
 *     (OK -- we drop it if it's not from our old gateway, which is close
 *      enough)
 * 3.2.2.3 (Source Quench)
 *   MUST pass incoming SOURCE_QUENCHs to transport layer (OK)
 *   Other requirements are dealt with at the transport layer.
 * 3.2.2.4 (Time Exceeded)
 *   MUST pass TIME_EXCEEDED to transport layer (OK)
 *   Other requirements dealt with at IP (generating TIME_EXCEEDED).
 * 3.2.2.5 (Parameter Problem)
 *   SHOULD generate these (OK)
 *   MUST pass received PARAMPROBLEM to transport layer (NOT YET)
 *   	[Solaris 2.X seems to assert EPROTO when this occurs] -- AC
 * 3.2.2.6 (Echo Request/Reply)
 *   MUST reply to ECHO_REQUEST, and give app to do ECHO stuff (OK, OK)
 *   MAY discard broadcast ECHO_REQUESTs. (We don't, but that's OK.)
 *   MUST reply using same source address as the request was sent to.
 *     We're OK for unicast ECHOs, and it doesn't say anything about
 *     how to handle broadcast ones, since it's optional.
 *   MUST copy data from REQUEST to REPLY (OK)
 *     unless it would require illegal fragmentation (OK)
 *   MUST pass REPLYs to transport/user layer (OK)
 *   MUST use any provided source route (reversed) for REPLY. (NOT YET)
 * 3.2.2.7 (Information Request/Reply)
 *   MUST NOT implement this. (I guess that means silently discard...?) (OK)
 * 3.2.2.8 (Timestamp Request/Reply)
 *   MAY implement (OK)
 *   SHOULD be in-kernel for "minimum variability" (OK)
 *   MAY discard broadcast REQUESTs.  (OK, but see source for inconsistency)
 *   MUST reply using same source address as the request was sent to. (OK)
 *   MUST reverse source route, as per ECHO (NOT YET)
 *   MUST pass REPLYs to transport/user layer (requires RAW, just like 
 *	ECHO) (OK)
 *   MUST update clock for timestamp at least 15 times/sec (OK)
 *   MUST be "correct within a few minutes" (OK)
 * 3.2.2.9 (Address Mask Request/Reply)
 *   MAY implement (OK)
 *   MUST send a broadcast REQUEST if using this system to set netmask
 *     (OK... we don't use it)
 *   MUST discard received REPLYs if not using this system (OK)
 *   MUST NOT send replies unless specifically made agent for this sort
 *     of thing. (OK)
 *
 *
 * RFC 1812 (IPv4 Router Requirements) Status (even longer):
 *  4.3.2.1 (Unknown Message Types)
 *   MUST pass messages of unknown type to ICMP user iface or silently discard
 *     them (OK)
 *  4.3.2.2 (ICMP Message TTL)
 *   MUST initialize TTL when originating an ICMP message (OK)
 *  4.3.2.3 (Original Message Header)
 *   SHOULD copy as much data from the offending packet as possible without
 *     the length of the ICMP datagram exceeding 576 bytes (OK)
 *   MUST leave original IP header of the offending packet, but we're not
 *     required to undo modifications made (OK)
 *  4.3.2.4 (Original Message Source Address)
 *   MUST use one of addresses for the interface the orig. packet arrived as
 *     source address (OK)
 *  4.3.2.5 (TOS and Precedence)
 *   SHOULD leave TOS set to the same value unless the packet would be 
 *     discarded for that reason (OK)
 *   MUST use TOS=0 if not possible to leave original value (OK)
 *   MUST leave IP Precedence for Source Quench messages (OK -- not sent 
 *	at all)
 *   SHOULD use IP Precedence = 6 (Internetwork Control) or 7 (Network Control)
 *     for all other error messages (OK, we use 6)
 *   MAY allow configuration of IP Precedence (OK -- not done)
 *   MUST leave IP Precedence and TOS for reply messages (OK)
 *  4.3.2.6 (Source Route)
 *   SHOULD use reverse source route UNLESS sending Parameter Problem on source
 *     routing and UNLESS the packet would be immediately discarded (NOT YET)
 *  4.3.2.7 (When Not to Send ICMP Errors)
 *   MUST NOT send ICMP errors in reply to:
 *     ICMP errors (OK)
 *     Packets failing IP header validation tests unless otherwise noted (OK)
 *     Broadcast/multicast datagrams (OK)
 *     MAC broadcasts (OK)
 *     Non-initial fragments (OK)
 *     Datagram with a source address that isn't a single host. (OK)
 *  4.3.2.8 (Rate Limiting)
 *   SHOULD be able to limit error message rate (OK)
 *   SHOULD allow setting of rate limits (OK, in the source)
 *  4.3.3.1 (Destination Unreachable)
 *   All the rules govern the IP layer, and are dealt with in ip.c, not here.
 *  4.3.3.2 (Redirect)
 *   MAY ignore ICMP Redirects if running a routing protocol or if forwarding
 *     is enabled on the interface (OK -- ignores)
 *  4.3.3.3 (Source Quench)
 *   SHOULD NOT originate SQ messages (OK)
 *   MUST be able to limit SQ rate if originates them (OK as we don't 
 *	send them)
 *   MAY ignore SQ messages it receives (OK -- we don't)
 *  4.3.3.4 (Time Exceeded)
 *   Requirements dealt with at IP (generating TIME_EXCEEDED).
 *  4.3.3.5 (Parameter Problem)
 *   MUST generate these for all errors not covered by other messages (OK)
 *   MUST include original value of the value pointed by (OK)
 *  4.3.3.6 (Echo Request)
 *   MUST implement echo server function (OK)
 *   MUST process at ER of at least max(576, MTU) (OK)
 *   MAY reject broadcast/multicast ER's (We don't, but that's OK)
 *   SHOULD have a config option for silently ignoring ER's (OK)
 *   MUST have a default value for the above switch = NO (OK)
 *   MUST have application layer interface for Echo Request/Reply (OK)
 *   MUST reply using same source address as the request was sent to.
 *     We're OK for unicast ECHOs, and it doesn't say anything about
 *     how to handle broadcast ones, since it's optional.
 *   MUST copy data from Request to Reply (OK)
 *   SHOULD update Record Route / Timestamp options (??)
 *   MUST use reversed Source Route for Reply if possible (NOT YET)
 *  4.3.3.7 (Information Request/Reply)
 *   SHOULD NOT originate or respond to these (OK)
 *  4.3.3.8 (Timestamp / Timestamp Reply)
 *   MAY implement (OK)
 *   MUST reply to every Timestamp message received (OK)
 *   MAY discard broadcast REQUESTs.  (OK, but see source for inconsistency)
 *   MUST reply using same source address as the request was sent to. (OK)
 *   MUST use reversed Source Route if possible (NOT YET)
 *   SHOULD update Record Route / Timestamp options (??)
 *   MUST pass REPLYs to transport/user layer (requires RAW, just like 
 *	ECHO) (OK)
 *   MUST update clock for timestamp at least 16 times/sec (OK)
 *   MUST be "correct within a few minutes" (OK)
 * 4.3.3.9 (Address Mask Request/Reply)
 *   MUST have support for receiving AMRq and responding with AMRe (OK, 
 *	but only as a compile-time option)
 *   SHOULD have option for each interface for AMRe's, MUST default to 
 *	NO (NOT YET)
 *   MUST NOT reply to AMRq before knows the correct AM (OK)
 *   MUST NOT respond to AMRq with source address 0.0.0.0 on physical
 *    	interfaces having multiple logical i-faces with different masks
 *	(NOT YET)
 *   SHOULD examine all AMRe's it receives and check them (NOT YET)
 *   SHOULD log invalid AMRe's (AM+sender) (NOT YET)
 *   MUST NOT use contents of AMRe to determine correct AM (OK)
 *   MAY broadcast AMRe's after having configured address masks (OK -- doesn't)
 *   MUST NOT do broadcast AMRe's if not set by extra option (OK, no option)
 *   MUST use the { <NetPrefix>, -1 } form of broadcast addresses (OK)
 * 4.3.3.10 (Router Advertisement and Solicitations)
 *   MUST support router part of Router Discovery Protocol on all networks we
 *     support broadcast or multicast addressing. (OK -- done by gated)
 *   MUST have all config parameters with the respective defaults (OK)
 * 5.2.7.1 (Destination Unreachable)
 *   MUST generate DU's (OK)
 *   SHOULD choose a best-match response code (OK)
 *   SHOULD NOT generate Host Isolated codes (OK)
 *   SHOULD use Communication Administratively Prohibited when administratively
 *     filtering packets (NOT YET -- bug-to-bug compatibility)
 *   MAY include config option for not generating the above and silently
 *	discard the packets instead (OK)
 *   MAY include config option for not generating Precedence Violation and
 *     Precedence Cutoff messages (OK as we don't generate them at all)
 *   MUST use Host Unreachable or Dest. Host Unknown codes whenever other hosts
 *     on the same network might be reachable (OK -- no net unreach's at all)
 *   MUST use new form of Fragmentation Needed and DF Set messages (OK)
 * 5.2.7.2 (Redirect)
 *   MUST NOT generate network redirects (OK)
 *   MUST be able to generate host redirects (OK)
 *   SHOULD be able to generate Host+TOS redirects (NO as we don't use TOS)
 *   MUST have an option to use Host redirects instead of Host+TOS ones (OK as
 *     no Host+TOS Redirects are used)
 *   MUST NOT generate redirects unless forwarding to the same i-face and the
 *     dest. address is on the same subnet as the src. address and no source
 *     routing is in use. (OK)
 *   MUST NOT follow redirects when using a routing protocol (OK)
 *   MAY use redirects if not using a routing protocol (OK, compile-time option)
 *   MUST comply to Host Requirements when not acting as a router (OK)
 *  5.2.7.3 (Time Exceeded)
 *   MUST generate Time Exceeded Code 0 when discarding packet due to TTL=0 (OK)
 *   MAY have a per-interface option to disable origination of TE messages, but
 *     it MUST default to "originate" (OK -- we don't support it)
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/fcntl.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/string.h>
#include <net/snmp.h>
#include <net/ip.h>
#include <net/route.h>
#include <net/protocol.h>
#include <net/icmp.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <net/snmp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <asm/system.h>
#include <asm/segment.h>
#include <net/checksum.h>

#define min(a,b)	((a)<(b)?(a):(b))

/*
 *	Statistics
 */
 
struct icmp_mib icmp_statistics;

/* An array of errno for error messages from dest unreach. */
/* RFC 1122: 3.2.2.1 States that NET_UNREACH, HOS_UNREACH and SR_FAIELD MUST be considered 'transient errs'. */

struct icmp_err icmp_err_convert[] = {
  { ENETUNREACH,	0 },	/*	ICMP_NET_UNREACH	*/
  { EHOSTUNREACH,	0 },	/*	ICMP_HOST_UNREACH	*/
  { ENOPROTOOPT,	1 },	/*	ICMP_PROT_UNREACH	*/
  { ECONNREFUSED,	1 },	/*	ICMP_PORT_UNREACH	*/
  { EOPNOTSUPP,		0 },	/*	ICMP_FRAG_NEEDED	*/
  { EOPNOTSUPP,		0 },	/*	ICMP_SR_FAILED		*/
  { ENETUNREACH,	1 },	/* 	ICMP_NET_UNKNOWN	*/
  { EHOSTDOWN,		1 },	/*	ICMP_HOST_UNKNOWN	*/
  { ENONET,		1 },	/*	ICMP_HOST_ISOLATED	*/
  { ENETUNREACH,	1 },	/*	ICMP_NET_ANO		*/
  { EHOSTUNREACH,	1 },	/*	ICMP_HOST_ANO		*/
  { EOPNOTSUPP,		0 },	/*	ICMP_NET_UNR_TOS	*/
  { EOPNOTSUPP,		0 }	/*	ICMP_HOST_UNR_TOS	*/
};

/*
 *	A spare long used to speed up statistics updating
 */
 
unsigned long dummy;

/*
 *	ICMP transmit rate limit control structures. We use a relatively simple
 *	approach to the problem: For each type of ICMP message with rate limit
 *	we count the number of messages sent during some time quantum. If this
 *	count exceeds given maximal value, we ignore all messages not separated
 *	from the last message sent at least by specified time.
 */

#define XRLIM_CACHE_SIZE 16		/* How many destination hosts do we cache */

struct icmp_xrl_cache			/* One entry of the ICMP rate cache */
{
	__u32 daddr;			/* Destination address */
	unsigned long counter;		/* Message counter */
	unsigned long next_reset;	/* Time of next reset of the counter */
	unsigned long last_access;	/* Time of last access to this entry (LRU) */
	unsigned int restricted;	/* Set if we're in restricted mode */
	unsigned long next_packet;	/* When we'll allow a next packet if restricted */
};

struct icmp_xrlim
{
	unsigned long timeout;		/* Time quantum for rate measuring */
	unsigned long limit;		/* Maximal number of messages per time quantum allowed */
	unsigned long delay;		/* How long we wait between packets when restricting */
	struct icmp_xrl_cache cache[XRLIM_CACHE_SIZE];	/* Rate cache */
};

/*
 *	ICMP control array. This specifies what to do with each ICMP.
 */
 
struct icmp_control
{
	unsigned long *output;		/* Address to increment on output */
	unsigned long *input;		/* Address to increment on input */
	void (*handler)(struct icmphdr *icmph, struct sk_buff *skb, struct device *dev, __u32 saddr, __u32 daddr, int len);
	unsigned long error;		/* This ICMP is classed as an error message */
	struct icmp_xrlim *xrlim;	/* Transmit rate limit control structure or NULL for no limits */
};

static struct icmp_control icmp_pointers[19];

/*
 *	Build xmit assembly blocks
 */

struct icmp_bxm
{
	void *data_ptr;
	int data_len;
	struct icmphdr icmph;
	unsigned long csum;
	struct options replyopts;
	unsigned char  optbuf[40];
};

/*
 *	The ICMP socket. This is the most convenient way to flow control
 *	our ICMP output as well as maintain a clean interface throughout
 *	all layers. All Socketless IP sends will soon be gone.
 */
	
struct socket icmp_socket;

/*
 *	Send an ICMP frame.
 */


/*
 *	Initialize the transmit rate limitation mechanism.
 */

#ifndef CONFIG_NO_ICMP_LIMIT

static void xrlim_init(void)
{
	int type, entry;
	struct icmp_xrlim *xr;

	for (type=0; type<=18; type++) {
		xr = icmp_pointers[type].xrlim;
		if (xr) {
			for (entry=0; entry<XRLIM_CACHE_SIZE; entry++)
				xr->cache[entry].daddr = INADDR_NONE;
		}
	}
}

/*
 *	Check transmit rate limitation for given message.
 *
 *	RFC 1812: 4.3.2.8 SHOULD be able to limit error message rate
 *			  SHOULD allow setting of rate limits (we allow 
 *			  in the source)
 */

static int xrlim_allow(int type, __u32 addr)
{
	struct icmp_xrlim *r;
	struct icmp_xrl_cache *c;
	unsigned long now;

	if (type > 18)			/* No time limit present */
		return 1;
	r = icmp_pointers[type].xrlim;
	if (!r)
		return 1;

	for (c = r->cache; c < &r->cache[XRLIM_CACHE_SIZE]; c++)	
	  /* Cache lookup */
		if (c->daddr == addr)
			break;

	now = jiffies;		/* Cache current time (saves accesses to volatile variable) */

	if (c == &r->cache[XRLIM_CACHE_SIZE]) {		/* Cache miss */
		unsigned long oldest = now;		/* Find the oldest entry to replace */
		struct icmp_xrl_cache *d;
		c = r->cache;
		for (d = r->cache; d < &r->cache[XRLIM_CACHE_SIZE]; d++)
			if (!d->daddr) {		/* Unused entry */
				c = d;
				break;
			} else if (d->last_access < oldest) {
				oldest = d->last_access;
				c = d;
			}
		c->last_access = now;			/* Fill the entry with new data */
		c->daddr = addr;
		c->counter = 1;
		c->next_reset = now + r->timeout;
		c->restricted = 0;
		return 1;
	}

	c->last_access = now;
	if (c->next_reset > now) {			/* Let's increment the counter */
		c->counter++;
		if (c->counter == r->limit) {		/* Limit exceeded, start restrictions */
			c->restricted = 1;
			c->next_packet = now + r->delay;
			return 0;
		}
		if (c->restricted) {			/* Any restrictions pending? */
			if (c->next_packet > now)
				return 0;
			c->next_packet = now + r->delay;
			return 1;
		}
	} else {					/* Reset the counter */
		if (c->counter < r->limit)		/* Switch off all restrictions */
			c->restricted = 0;
		c->next_reset = now + r->timeout;
		c->counter = 0;
	}

	return 1;					/* Send the packet */
}

#endif /* CONFIG_NO_ICMP_LIMIT */

/*
 *	Maintain the counters used in the SNMP statistics for outgoing ICMP
 */
 
static void icmp_out_count(int type)
{
	if(type>18)
		return;
	(*icmp_pointers[type].output)++;
	icmp_statistics.IcmpOutMsgs++;
}
 
/*
 *	Checksum each fragment, and on the first include the headers and final checksum.
 */
 
static void icmp_glue_bits(const void *p, __u32 saddr, char *to, unsigned int offset, unsigned int fraglen)
{
	struct icmp_bxm *icmp_param = (struct icmp_bxm *)p;
	struct icmphdr *icmph;
	unsigned long csum;

	if (offset) {
		icmp_param->csum=csum_partial_copy(icmp_param->data_ptr+offset-sizeof(struct icmphdr), 
				to, fraglen,icmp_param->csum);
		return;
	}

	/*
	 *	First fragment includes header. Note that we've done
	 *	the other fragments first, so that we get the checksum
	 *	for the whole packet here.
	 */
	csum = csum_partial_copy((void *)&icmp_param->icmph,
		to, sizeof(struct icmphdr), 
		icmp_param->csum);
	csum = csum_partial_copy(icmp_param->data_ptr,
		to+sizeof(struct icmphdr),
		fraglen-sizeof(struct icmphdr), csum);
	icmph=(struct icmphdr *)to;
	icmph->checksum = csum_fold(csum);
}
 
/*
 *	Driving logic for building and sending ICMP messages.
 */

static void icmp_build_xmit(struct icmp_bxm *icmp_param, __u32 saddr, __u32 daddr, __u8 tos)
{
	struct sock *sk=icmp_socket.data;
	icmp_param->icmph.checksum=0;
	icmp_param->csum=0;
	icmp_out_count(icmp_param->icmph.type);
	sk->ip_tos = tos;
	ip_build_xmit(sk, icmp_glue_bits, icmp_param, 
		icmp_param->data_len+sizeof(struct icmphdr),
		daddr, saddr, &icmp_param->replyopts, 0, IPPROTO_ICMP, 1);
}


/*
 *	Send an ICMP message in response to a situation
 *
 *	RFC 1122: 3.2.2	MUST send at least the IP header and 8 bytes of header. MAY send more (we do).
 *			MUST NOT change this header information.
 *			MUST NOT reply to a multicast/broadcast IP address.
 *			MUST NOT reply to a multicast/broadcast MAC address.
 *			MUST reply to only the first fragment.
 */

void icmp_send(struct sk_buff *skb_in, int type, int code, unsigned long info, struct device *dev)
{
	struct iphdr *iph;
	struct icmphdr *icmph;
	int atype, room;
	struct icmp_bxm icmp_param;
	__u32 saddr;
	
	/*
	 *	Find the original header
	 */
	 
	iph = skb_in->ip_hdr;
	
	/*
	 *	No replies to physical multicast/broadcast
	 */
	 
	if(skb_in->pkt_type!=PACKET_HOST)
		return;
		
	/*
	 *	Now check at the protocol level
	 */
	 
	atype=ip_chk_addr(iph->daddr);
	if(atype==IS_BROADCAST||atype==IS_MULTICAST)
		return;
		
	/*
	 *	Only reply to fragment 0. We byte re-order the constant
	 *	mask for efficiency.
	 */
	 
	if(iph->frag_off&htons(IP_OFFSET))
		return;
		
	/* 
	 *	If we send an ICMP error to an ICMP error a mess would result..
	 */
	 
	if(icmp_pointers[type].error)
	{
		/*
		 *	We are an error, check if we are replying to an ICMP error
		 */
		 
		if(iph->protocol==IPPROTO_ICMP)
		{
			icmph = (struct icmphdr *)((char *)iph + (iph->ihl<<2));
			/*
			 *	Assume any unknown ICMP type is an error. This isn't
			 *	specified by the RFC, but think about it..
			 */
			if(icmph->type>18 || icmp_pointers[icmph->type].error)
				return;
		}
	}

	/*
	 *	Check the rate limit
	 */

#ifndef CONFIG_NO_ICMP_LIMIT
	if (!xrlim_allow(type, iph->saddr))
		return;
#endif	

	/*
	 *	Construct source address and options.
	 */
	 
	saddr=iph->daddr;
	if(saddr!=dev->pa_addr && ip_chk_addr(saddr)!=IS_MYADDR)
		saddr=dev->pa_addr;
	if(ip_options_echo(&icmp_param.replyopts, NULL, saddr, iph->saddr, skb_in))
		return;

	/*
	 *	Prepare data for ICMP header.
	 */

	icmp_param.icmph.type=type;
	icmp_param.icmph.code=code;
	icmp_param.icmph.un.gateway = info;
	icmp_param.data_ptr=iph;
	room = 576 - sizeof(struct iphdr) - icmp_param.replyopts.optlen;
	icmp_param.data_len=(iph->ihl<<2)+skb_in->len;	/* RFC says return as much as we can without exceeding 576 bytes */
	if (icmp_param.data_len > room)
		icmp_param.data_len = room;
	
	/*
	 *	Build and send the packet.
	 */

	icmp_build_xmit(&icmp_param, saddr, iph->saddr, ((iph->tos & 0x38) | 6));
}


/* 
 *	Handle ICMP_DEST_UNREACH, ICMP_TIME_EXCEED, and ICMP_QUENCH. 
 */
 
static void icmp_unreach(struct icmphdr *icmph, struct sk_buff *skb, struct device *dev, __u32 saddr, __u32 daddr, int len)
{
	struct iphdr *iph;
	int hash;
	struct inet_protocol *ipprot;
	unsigned char *dp;	
	
	iph = (struct iphdr *) (icmph + 1);
	
	dp= ((unsigned char *)iph)+(iph->ihl<<2);
	
	if(icmph->type==ICMP_DEST_UNREACH)
	{
		switch(icmph->code & 15)
		{
			case ICMP_NET_UNREACH:
				break;
			case ICMP_HOST_UNREACH:
				break;
			case ICMP_PROT_UNREACH:
/*				printk(KERN_INFO "ICMP: %s:%d: protocol unreachable.\n",
					in_ntoa(iph->daddr), (int)iph->protocol);*/
				break;
			case ICMP_PORT_UNREACH:
				break;
			case ICMP_FRAG_NEEDED:
#ifdef CONFIG_NO_PATH_MTU_DISCOVERY
				printk(KERN_INFO "ICMP: %s: fragmentation needed and DF set.\n",
								in_ntoa(iph->daddr));
				break;
#else
			{
				unsigned short old_mtu = ntohs(iph->tot_len);
				unsigned short new_mtu = ntohs(icmph->un.echo.sequence);

				/*
				 * RFC1191 5.  4.2BSD based router can return incorrect
				 * Total Length.  If current mtu is unknown or old_mtu
				 * is not less than current mtu, reduce old_mtu by 4 times
				 * the header length.
				 */

				if (skb->sk == NULL /* can this happen? */
					|| skb->sk->ip_route_cache == NULL
					|| skb->sk->ip_route_cache->rt_mtu <= old_mtu)
				{
					NETDEBUG(printk(KERN_INFO "4.2BSD based fragmenting router between here and %s, mtu corrected from %d", in_ntoa(iph->daddr), old_mtu));
					old_mtu -= 4 * iph->ihl;
					NETDEBUG(printk(" to %d\n", old_mtu));
				}

				if (new_mtu < 68 || new_mtu >= old_mtu)
				{
					/*
					 * 	It is either dumb router, which does not
					 *	understand Path MTU Disc. protocol
					 *	or broken (f.e. Linux<=1.3.37 8) router.
					 *	Try to guess...
					 *	The table is taken from RFC-1191.
					 */
					if (old_mtu > 32000)
						new_mtu = 32000;
					else if (old_mtu > 17914)
						new_mtu = 17914;
					else if (old_mtu > 8166)
						new_mtu = 8166;
					else if (old_mtu > 4352)
						new_mtu = 4352;
					else if (old_mtu > 2002)
						new_mtu = 2002;
					else if (old_mtu > 1492)
						new_mtu = 1492;
					else if (old_mtu > 576)
						new_mtu = 576;
					else if (old_mtu > 296)
						new_mtu = 296;
					/*
					 *	These two are not from the RFC but
					 *	are needed for AMPRnet AX.25 paths.
					 */
					else if (old_mtu > 216)
						new_mtu = 216;
					else if (old_mtu > 128)
						new_mtu = 128;
					else
					/*
					 *	Despair..
					 */
						new_mtu = 68;
				}
				/*
				 * Ugly trick to pass MTU to protocol layer.
				 * Really we should add argument "info" to error handler.
				 */
				iph->id = htons(new_mtu);
				break;
			}
#endif
			case ICMP_SR_FAILED:
				printk(KERN_INFO "ICMP: %s: Source Route Failed.\n", in_ntoa(iph->daddr));
				break;
			default:
				break;
		}
		if(icmph->code>12)	/* Invalid type */
			return;
	}
	
	/*
	 *	Throw it at our lower layers
	 *
	 *	RFC 1122: 3.2.2 MUST extract the protocol ID from the passed header.
	 *	RFC 1122: 3.2.2.1 MUST pass ICMP unreach messages to the transport layer.
	 *	RFC 1122: 3.2.2.2 MUST pass ICMP time expired messages to transport layer.
	 */

	/*
	 *	Get the protocol(s). 
	 */
	 
	hash = iph->protocol & (MAX_INET_PROTOS -1);

	/*
	 *	This can't change while we are doing it. 
	 *
	 *	FIXME: Deliver to appropriate raw sockets too.
	 */
	 
	ipprot = (struct inet_protocol *) inet_protos[hash];
	while(ipprot != NULL) 
	{
		struct inet_protocol *nextip;

		nextip = (struct inet_protocol *) ipprot->next;
	
		/* 
		 *	Pass it off to everyone who wants it. 
		 */

		/* RFC1122: OK. Passes appropriate ICMP errors to the */
		/* appropriate protocol layer (MUST), as per 3.2.2. */

		if (iph->protocol == ipprot->protocol && ipprot->err_handler) 
		{
			ipprot->err_handler(icmph->type, icmph->code, dp,
					    iph->daddr, iph->saddr, ipprot);
		}

		ipprot = nextip;
  	}
	kfree_skb(skb, FREE_READ);
}


/*
 *	Handle ICMP_REDIRECT. 
 */

static void icmp_redirect(struct icmphdr *icmph, struct sk_buff *skb, struct device *dev, __u32 source, __u32 daddr, int len)
{
	struct iphdr *iph;
	unsigned long ip;

	/*
	 *	Get the copied header of the packet that caused the redirect
	 */
	 
	iph = (struct iphdr *) (icmph + 1);
	ip = iph->daddr;

	/*
	 *	If we are a router and we run a routing protocol, we MUST NOT follow redirects.
	 *	When using no routing protocol, we MAY follow redirects. (RFC 1812, 5.2.7.2)
	 */

#if defined(CONFIG_IP_FORWARD) && !defined(CONFIG_IP_DUMB_ROUTER)
	NETDEBUG(printk(KERN_INFO "icmp: ICMP redirect ignored. dest = %lX, "
	       "orig gw = %lX, \"new\" gw = %lX, device = %s.\n", ntohl(ip),
		ntohl(source), ntohl(icmph->un.gateway), dev->name));
#else	
	switch(icmph->code & 7) 
	{
		case ICMP_REDIR_NET:
			/*
			 *	This causes a problem with subnetted networks. What we should do
			 *	is use ICMP_ADDRESS to get the subnet mask of the problem route
			 *	and set both. But we don't.. [RFC1812 says routers MUST NOT
			 *	generate Network Redirects]
			 */
#ifdef not_a_good_idea
			ip_rt_add((RTF_DYNAMIC | RTF_MODIFIED | RTF_GATEWAY),
				ip, 0, icmph->un.gateway, dev,0, 0, 0);
#endif
			/*
			 *	As per RFC recommendations now handle it as
			 *	a host redirect.
			 */
			 
		case ICMP_REDIR_HOST:
			/*
			 *	Add better route to host.
			 *	But first check that the redirect
			 *	comes from the old gateway..
			 *	And make sure it's an ok host address
			 *	(not some confused thing sending our
			 *	address)
			 */
			printk(KERN_INFO "ICMP redirect from %s\n", in_ntoa(source));
			ip_rt_redirect(source, ip, icmph->un.gateway, dev);
			break;
		case ICMP_REDIR_NETTOS:
		case ICMP_REDIR_HOSTTOS:
			printk(KERN_INFO "ICMP: cannot handle TOS redirects yet!\n");
			break;
		default:
			break;
  	}
#endif  	
  	/*
  	 *	Discard the original packet
  	 */
  	 
  	kfree_skb(skb, FREE_READ);
}

/*
 *	Handle ICMP_ECHO ("ping") requests. 
 *
 *	RFC 1122: 3.2.2.6 MUST have an echo server that answers ICMP echo requests.
 *	RFC 1122: 3.2.2.6 Data received in the ICMP_ECHO request MUST be included in the reply.
 *	RFC 1812: 4.3.3.6 SHOULD have a config option for silently ignoring echo requests, MUST have default=NOT.
 *	See also WRT handling of options once they are done and working.
 */
 
static void icmp_echo(struct icmphdr *icmph, struct sk_buff *skb, struct device *dev, __u32 saddr, __u32 daddr, int len)
{
#ifndef CONFIG_IP_IGNORE_ECHO_REQUESTS
	struct icmp_bxm icmp_param;
	icmp_param.icmph=*icmph;
	icmp_param.icmph.type=ICMP_ECHOREPLY;
	icmp_param.data_ptr=(icmph+1);
	icmp_param.data_len=len;
	if (ip_options_echo(&icmp_param.replyopts, NULL, daddr, saddr, skb)==0)
		icmp_build_xmit(&icmp_param, daddr, saddr, skb->ip_hdr->tos);
#endif
	kfree_skb(skb, FREE_READ);
}

/*
 *	Handle ICMP Timestamp requests. 
 *	RFC 1122: 3.2.2.8 MAY implement ICMP timestamp requests.
 *		  SHOULD be in the kernel for minimum random latency.
 *		  MUST be accurate to a few minutes.
 *		  MUST be updated at least at 15Hz.
 */
 
static void icmp_timestamp(struct icmphdr *icmph, struct sk_buff *skb, struct device *dev, __u32 saddr, __u32 daddr, int len)
{
	__u32 times[3];		/* So the new timestamp works on ALPHA's.. */
	struct icmp_bxm icmp_param;
	
	/*
	 *	Too short.
	 */
	 
	if(len<12)
	{
		icmp_statistics.IcmpInErrors++;
		kfree_skb(skb, FREE_READ);
		return;
	}
	
	/*
	 *	Fill in the current time as ms since midnight UT: 
	 */
	 
	{
		struct timeval tv;
		do_gettimeofday(&tv);
		times[1] = htonl((tv.tv_sec % 86400) * 1000 + tv.tv_usec / 1000);
	}
	times[2] = times[1];
	memcpy((void *)&times[0], icmph+1, 4);		/* Incoming stamp */
	icmp_param.icmph=*icmph;
	icmp_param.icmph.type=ICMP_TIMESTAMPREPLY;
	icmp_param.icmph.code=0;
	icmp_param.data_ptr=&times;
	icmp_param.data_len=12;
	if (ip_options_echo(&icmp_param.replyopts, NULL, daddr, saddr, skb)==0)
		icmp_build_xmit(&icmp_param, daddr, saddr, skb->ip_hdr->tos);
	kfree_skb(skb,FREE_READ);
}


/* 
 *	Handle ICMP_ADDRESS_MASK requests.  (RFC950)
 *
 * RFC1122 (3.2.2.9).  A host MUST only send replies to 
 * ADDRESS_MASK requests if it's been configured as an address mask 
 * agent.  Receiving a request doesn't constitute implicit permission to 
 * act as one. Of course, implementing this correctly requires (SHOULD) 
 * a way to turn the functionality on and off.  Another one for sysctl(), 
 * I guess. -- MS 
 * Botched with a CONFIG option for now - Linus add scts sysctl please.. 
 */
 
static void icmp_address(struct icmphdr *icmph, struct sk_buff *skb, struct device *dev, __u32 saddr, __u32 daddr, int len)
{
#ifdef CONFIG_IP_ADDR_AGENT	/* Don't use, broken */
	struct icmp_bxm icmp_param;
	icmp_param.icmph.type=ICMP_ADDRESSREPLY;
	icmp_param.icmph.code=0;
	icmp_param.icmph.un.echo.id = icmph->un.echo.id;
	icmp_param.icmph.un.echo.sequence = icmph->un.echo.sequence;
	icmp_param.data_ptr=&dev->pa_mask;
	icmp_param.data_len=4;
	if (ip_options_echo(&icmp_param.replyopts, NULL, daddr, saddr, skb)==0)
		icmp_build_xmit(&icmp_param, daddr, saddr, skb->iph->tos);
#endif	
	kfree_skb(skb, FREE_READ);	
}

static void icmp_discard(struct icmphdr *icmph, struct sk_buff *skb, struct device *dev, __u32 saddr, __u32 daddr, int len)
{
	kfree_skb(skb, FREE_READ);
}

#ifdef CONFIG_IP_TRANSPARENT_PROXY
/*
 *	Check incoming icmp packets not addressed locally, to check whether
 *	they relate to a (proxying) socket on our system.
 *	Needed for transparent proxying.
 *
 *	This code is presently ugly and needs cleanup.
 *	Probably should add a chkaddr entry to ipprot to call a chk routine
 *	in udp.c or tcp.c...
 */

int icmp_chkaddr(struct sk_buff *skb)
{
	struct icmphdr *icmph=(struct icmphdr *)(skb->h.raw + skb->h.iph->ihl*4);
	struct iphdr *iph = (struct iphdr *) (icmph + 1);
	void (*handler)(struct icmphdr *icmph, struct sk_buff *skb, struct device *dev, __u32 saddr, __u32 daddr, int len) = icmp_pointers[icmph->type].handler;

	if (handler == icmp_unreach || handler == icmp_redirect) {
		struct sock *sk;

		switch (iph->protocol) {
		case IPPROTO_TCP:
			{
			struct tcphdr *th = (struct tcphdr *)(((unsigned char *)iph)+(iph->ihl<<2));

			sk = get_sock(&tcp_prot, th->source, iph->daddr,
						th->dest, iph->saddr, 0, 0);
			if (!sk) return 0;
			if (sk->saddr != iph->saddr) return 0;
			if (sk->daddr != iph->daddr) return 0;
			if (sk->dummy_th.dest != th->dest) return 0;
			/*
			 * This packet came from us.
			 */
			return 1;
			}
		case IPPROTO_UDP:
			{
			struct udphdr *uh = (struct udphdr *)(((unsigned char *)iph)+(iph->ihl<<2));

			sk = get_sock(&udp_prot, uh->source, iph->daddr,
						uh->dest, iph->saddr, 0, 0);
			if (!sk) return 0;
			if (sk->saddr != iph->saddr && ip_chk_addr(iph->saddr) != IS_MYADDR)
				return 0;
			/*
			 * This packet may have come from us.
			 * Assume it did.
			 */
			return 1;
			}
		}
	}
	return 0;
}

#endif
/* 
 *	Deal with incoming ICMP packets. 
 */
 
int icmp_rcv(struct sk_buff *skb, struct device *dev, struct options *opt,
	 __u32 daddr, unsigned short len,
	 __u32 saddr, int redo, struct inet_protocol *protocol)
{
	struct icmphdr *icmph=(void *)skb->h.raw;
#ifdef CONFIG_IP_TRANSPARENT_PROXY
	int r;
#endif
	icmp_statistics.IcmpInMsgs++;
	
  	/*
	 *	Validate the packet
  	 */
	
	if (ip_compute_csum((unsigned char *) icmph, len)) 
	{
		/* Failed checksum! */
		icmp_statistics.IcmpInErrors++;
		printk(KERN_INFO "ICMP: failed checksum from %s!\n", in_ntoa(saddr));
		kfree_skb(skb, FREE_READ);
		return(0);
	}
	
	/*
	 *	18 is the highest 'known' ICMP type. Anything else is a mystery
	 *
	 *	RFC 1122: 3.2.2  Unknown ICMP messages types MUST be silently discarded.
	 */
	 
	if(icmph->type > 18)
	{
		icmp_statistics.IcmpInErrors++;		/* Is this right - or do we ignore ? */
		kfree_skb(skb,FREE_READ);
		return(0);
	}
	
	/*
	 *	Parse the ICMP message 
	 */

#ifdef CONFIG_IP_TRANSPARENT_PROXY
	/*
	 *	We may get non-local addresses and still want to handle them
	 *	locally, due to transparent proxying.
	 *	Thus, narrow down the test to what is really meant.
	 */
	if (daddr!=dev->pa_addr && ((r = ip_chk_addr(daddr)) == IS_BROADCAST || r == IS_MULTICAST))
#else
	if (daddr!=dev->pa_addr && ip_chk_addr(daddr) != IS_MYADDR)
#endif
	{
		/*
		 *	RFC 1122: 3.2.2.6 An ICMP_ECHO to broadcast MAY be silently ignored (we don't as it is used
		 *	by some network mapping tools).
		 *	RFC 1122: 3.2.2.8 An ICMP_TIMESTAMP MAY be silently discarded if to broadcast/multicast.
		 */
		if (icmph->type != ICMP_ECHO) 
		{
			icmp_statistics.IcmpInErrors++;
			kfree_skb(skb, FREE_READ);
			return(0);
  		}
  		/*
  		 *	Reply the multicast/broadcast using a legal
  		 *	interface - in this case the device we got
  		 *	it from.
  		 */
		daddr=dev->pa_addr;
	}
	
	len-=sizeof(struct icmphdr);
	(*icmp_pointers[icmph->type].input)++;
	(icmp_pointers[icmph->type].handler)(icmph,skb,skb->dev,saddr,daddr,len);
	return 0;
}

/*
 *	This table defined limits of ICMP sending rate for various ICMP messages.
 */

static struct icmp_xrlim
	xrl_unreach = { 4*HZ, 80, HZ/4 },		/* Host Unreachable */
	xrl_redirect = { 2*HZ, 10, HZ/2 },		/* Redirect */
	xrl_generic = { 3*HZ, 30, HZ/4 };		/* All other errors */

/*
 *	This table is the definition of how we handle ICMP.
 */
 
static struct icmp_control icmp_pointers[19] = {
/* ECHO REPLY (0) */
 { &icmp_statistics.IcmpOutEchoReps, &icmp_statistics.IcmpInEchoReps, icmp_discard, 0, NULL },
 { &dummy, &icmp_statistics.IcmpInErrors, icmp_discard, 1, NULL },
 { &dummy, &icmp_statistics.IcmpInErrors, icmp_discard, 1, NULL },
/* DEST UNREACH (3) */
 { &icmp_statistics.IcmpOutDestUnreachs, &icmp_statistics.IcmpInDestUnreachs, icmp_unreach, 1, &xrl_unreach },
/* SOURCE QUENCH (4) */
 { &icmp_statistics.IcmpOutSrcQuenchs, &icmp_statistics.IcmpInSrcQuenchs, icmp_unreach, 1, NULL },
/* REDIRECT (5) */
 { &icmp_statistics.IcmpOutRedirects, &icmp_statistics.IcmpInRedirects, icmp_redirect, 1, &xrl_redirect },
 { &dummy, &icmp_statistics.IcmpInErrors, icmp_discard, 1, NULL },
 { &dummy, &icmp_statistics.IcmpInErrors, icmp_discard, 1, NULL },
/* ECHO (8) */
 { &icmp_statistics.IcmpOutEchos, &icmp_statistics.IcmpInEchos, icmp_echo, 0, NULL },
 { &dummy, &icmp_statistics.IcmpInErrors, icmp_discard, 1, NULL },
 { &dummy, &icmp_statistics.IcmpInErrors, icmp_discard, 1, NULL },
/* TIME EXCEEDED (11) */
 { &icmp_statistics.IcmpOutTimeExcds, &icmp_statistics.IcmpInTimeExcds, icmp_unreach, 1, &xrl_generic },
/* PARAMETER PROBLEM (12) */
/* FIXME: RFC1122 3.2.2.5 - MUST pass PARAM_PROB messages to transport layer */
 { &icmp_statistics.IcmpOutParmProbs, &icmp_statistics.IcmpInParmProbs, icmp_discard, 1, &xrl_generic },
/* TIMESTAMP (13) */
 { &icmp_statistics.IcmpOutTimestamps, &icmp_statistics.IcmpInTimestamps, icmp_timestamp, 0, NULL },
/* TIMESTAMP REPLY (14) */
 { &icmp_statistics.IcmpOutTimestampReps, &icmp_statistics.IcmpInTimestampReps, icmp_discard, 0, NULL },
/* INFO (15) */
 { &dummy, &dummy, icmp_discard, 0, NULL },
/* INFO REPLY (16) */
 { &dummy, &dummy, icmp_discard, 0, NULL },
/* ADDR MASK (17) */
 { &icmp_statistics.IcmpOutAddrMasks, &icmp_statistics.IcmpInAddrMasks, icmp_address, 0, NULL },
/* ADDR MASK REPLY (18) */
 { &icmp_statistics.IcmpOutAddrMaskReps, &icmp_statistics.IcmpInAddrMaskReps, icmp_discard, 0, NULL }
};

void icmp_init(struct proto_ops *ops)
{
	struct sock *sk;
	int err;
	icmp_socket.type=SOCK_RAW;
	icmp_socket.ops=ops;
	if((err=ops->create(&icmp_socket, IPPROTO_ICMP))<0)
		panic("Failed to create the ICMP control socket.\n");
	sk=icmp_socket.data;
	sk->allocation=GFP_ATOMIC;
	sk->num = 256;			/* Don't receive any data */
#ifndef CONFIG_NO_ICMP_LIMIT
	xrlim_init();
#endif
}

