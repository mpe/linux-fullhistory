/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Implementation of the Transmission Control Protocol(TCP).
 *
 * Version:	@(#)tcp.c	1.0.16	05/25/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Mark Evans, <evansmp@uhura.aston.ac.uk>
 *		Corey Minyard <wf-rch!minyard@relay.EU.net>
 *		Florian La Roche, <flla@stud.uni-sb.de>
 *		Charles Hedrick, <hedrick@klinzhai.rutgers.edu>
 *		Linus Torvalds, <torvalds@cs.helsinki.fi>
 *		Alan Cox, <gw4pts@gw4pts.ampr.org>
 *		Matthew Dillon, <dillon@apollo.west.oic.com>
 *		Arnt Gulbrandsen, <agulbra@nvg.unit.no>
 *		Jorge Cwik, <jorge@laser.satlink.net>
 *
 * Fixes:
 *		Alan Cox	:	Numerous verify_area() calls
 *		Alan Cox	:	Set the ACK bit on a reset
 *		Alan Cox	:	Stopped it crashing if it closed while
 *					sk->inuse=1 and was trying to connect
 *					(tcp_err()).
 *		Alan Cox	:	All icmp error handling was broken
 *					pointers passed where wrong and the
 *					socket was looked up backwards. Nobody
 *					tested any icmp error code obviously.
 *		Alan Cox	:	tcp_err() now handled properly. It
 *					wakes people on errors. select
 *					behaves and the icmp error race
 *					has gone by moving it into sock.c
 *		Alan Cox	:	tcp_send_reset() fixed to work for
 *					everything not just packets for
 *					unknown sockets.
 *		Alan Cox	:	tcp option processing.
 *		Alan Cox	:	Reset tweaked (still not 100%) [Had
 *					syn rule wrong]
 *		Herp Rosmanith  :	More reset fixes
 *		Alan Cox	:	No longer acks invalid rst frames.
 *					Acking any kind of RST is right out.
 *		Alan Cox	:	Sets an ignore me flag on an rst
 *					receive otherwise odd bits of prattle
 *					escape still
 *		Alan Cox	:	Fixed another acking RST frame bug.
 *					Should stop LAN workplace lockups.
 *		Alan Cox	: 	Some tidyups using the new skb list
 *					facilities
 *		Alan Cox	:	sk->keepopen now seems to work
 *		Alan Cox	:	Pulls options out correctly on accepts
 *		Alan Cox	:	Fixed assorted sk->rqueue->next errors
 *		Alan Cox	:	PSH doesn't end a TCP read. Switched a
 *					bit to skb ops.
 *		Alan Cox	:	Tidied tcp_data to avoid a potential
 *					nasty.
 *		Alan Cox	:	Added some better commenting, as the
 *					tcp is hard to follow
 *		Alan Cox	:	Removed incorrect check for 20 * psh
 *	Michael O'Reilly	:	ack < copied bug fix.
 *	Johannes Stille		:	Misc tcp fixes (not all in yet).
 *		Alan Cox	:	FIN with no memory -> CRASH
 *		Alan Cox	:	Added socket option proto entries.
 *					Also added awareness of them to accept.
 *		Alan Cox	:	Added TCP options (SOL_TCP)
 *		Alan Cox	:	Switched wakeup calls to callbacks,
 *					so the kernel can layer network
 *					sockets.
 *		Alan Cox	:	Use ip_tos/ip_ttl settings.
 *		Alan Cox	:	Handle FIN (more) properly (we hope).
 *		Alan Cox	:	RST frames sent on unsynchronised
 *					state ack error.
 *		Alan Cox	:	Put in missing check for SYN bit.
 *		Alan Cox	:	Added tcp_select_window() aka NET2E
 *					window non shrink trick.
 *		Alan Cox	:	Added a couple of small NET2E timer
 *					fixes
 *		Charles Hedrick :	TCP fixes
 *		Toomas Tamm	:	TCP window fixes
 *		Alan Cox	:	Small URG fix to rlogin ^C ack fight
 *		Charles Hedrick	:	Rewrote most of it to actually work
 *		Linus		:	Rewrote tcp_read() and URG handling
 *					completely
 *		Gerhard Koerting:	Fixed some missing timer handling
 *		Matthew Dillon  :	Reworked TCP machine states as per RFC
 *		Gerhard Koerting:	PC/TCP workarounds
 *		Adam Caldwell	:	Assorted timer/timing errors
 *		Matthew Dillon	:	Fixed another RST bug
 *		Alan Cox	:	Move to kernel side addressing changes.
 *		Alan Cox	:	Beginning work on TCP fastpathing
 *					(not yet usable)
 *		Arnt Gulbrandsen:	Turbocharged tcp_check() routine.
 *		Alan Cox	:	TCP fast path debugging
 *		Alan Cox	:	Window clamping
 *		Michael Riepe	:	Bug in tcp_check()
 *		Matt Dillon	:	More TCP improvements and RST bug fixes
 *		Matt Dillon	:	Yet more small nasties remove from the
 *					TCP code (Be very nice to this man if
 *					tcp finally works 100%) 8)
 *		Alan Cox	:	BSD accept semantics.
 *		Alan Cox	:	Reset on closedown bug.
 *	Peter De Schrijver	:	ENOTCONN check missing in tcp_sendto().
 *		Michael Pall	:	Handle select() after URG properly in
 *					all cases.
 *		Michael Pall	:	Undo the last fix in tcp_read_urg()
 *					(multi URG PUSH broke rlogin).
 *		Michael Pall	:	Fix the multi URG PUSH problem in
 *					tcp_readable(), select() after URG
 *					works now.
 *		Michael Pall	:	recv(...,MSG_OOB) never blocks in the
 *					BSD api.
 *		Alan Cox	:	Changed the semantics of sk->socket to
 *					fix a race and a signal problem with
 *					accept() and async I/O.
 *		Alan Cox	:	Relaxed the rules on tcp_sendto().
 *		Yury Shevchuk	:	Really fixed accept() blocking problem.
 *		Craig I. Hagan  :	Allow for BSD compatible TIME_WAIT for
 *					clients/servers which listen in on
 *					fixed ports.
 *		Alan Cox	:	Cleaned the above up and shrank it to
 *					a sensible code size.
 *		Alan Cox	:	Self connect lockup fix.
 *		Alan Cox	:	No connect to multicast.
 *		Ross Biro	:	Close unaccepted children on master
 *					socket close.
 *		Alan Cox	:	Reset tracing code.
 *		Alan Cox	:	Spurious resets on shutdown.
 *		Alan Cox	:	Giant 15 minute/60 second timer error
 *		Alan Cox	:	Small whoops in selecting before an
 *					accept.
 *		Alan Cox	:	Kept the state trace facility since
 *					it's handy for debugging.
 *		Alan Cox	:	More reset handler fixes.
 *		Alan Cox	:	Started rewriting the code based on
 *					the RFC's for other useful protocol
 *					references see: Comer, KA9Q NOS, and
 *					for a reference on the difference
 *					between specifications and how BSD
 *					works see the 4.4lite source.
 *		A.N.Kuznetsov	:	Don't time wait on completion of tidy
 *					close.
 *		Linus Torvalds	:	Fin/Shutdown & copied_seq changes.
 *		Linus Torvalds	:	Fixed BSD port reuse to work first syn
 *		Alan Cox	:	Reimplemented timers as per the RFC
 *					and using multiple timers for sanity.
 *		Alan Cox	:	Small bug fixes, and a lot of new
 *					comments.
 *		Alan Cox	:	Fixed dual reader crash by locking
 *					the buffers (much like datagram.c)
 *		Alan Cox	:	Fixed stuck sockets in probe. A probe
 *					now gets fed up of retrying without
 *					(even a no space) answer.
 *		Alan Cox	:	Extracted closing code better
 *		Alan Cox	:	Fixed the closing state machine to
 *					resemble the RFC.
 *		Alan Cox	:	More 'per spec' fixes.
 *		Jorge Cwik	:	Even faster checksumming.
 *		Alan Cox	:	tcp_data() doesn't ack illegal PSH
 *					only frames. At least one pc tcp stack
 *					generates them.
 *		Alan Cox	:	Cache last socket.
 *		Alan Cox	:	Per route irtt.
 *		Matt Day	:	Select() match BSD precisely on error
 *		Alan Cox	:	New buffers
 *		Marc Tamsky	:	Various sk->prot->retransmits and
 *					sk->retransmits misupdating fixed.
 *					Fixed tcp_write_timeout: stuck close,
 *					and TCP syn retries gets used now.
 *		Mark Yarvis	:	In tcp_read_wakeup(), don't send an
 *					ack if stat is TCP_CLOSED.
 *		Alan Cox	:	Look up device on a retransmit - routes may
 *					change. Doesn't yet cope with MSS shrink right
 *					but it's a start!
 *		Marc Tamsky	:	Closing in closing fixes.
 *		Mike Shaver	:	RFC1122 verifications.
 *		Alan Cox	:	rcv_saddr errors.
 *		Alan Cox	:	Block double connect().
 *		Alan Cox	:	Small hooks for enSKIP.
 *		Alexey Kuznetsov:	Path MTU discovery.
 *		Alan Cox	:	Support soft errors.
 *		Alan Cox	:	Fix MTU discovery pathological case
 *					when the remote claims no mtu!
 *		Marc Tamsky	:	TCP_CLOSE fix.
 *		Colin (G3TNE)	:	Send a reset on syn ack replies in
 *					window but wrong (fixes NT lpd problems)
 *		Pedro Roque	:	Better TCP window handling, delayed ack.
 *		Joerg Reuter	:	No modification of locked buffers in
 *					tcp_do_retransmit()
 *		Eric Schenk	:	Changed receiver side silly window
 *					avoidance algorithm to BSD style
 *					algorithm. This doubles throughput
 *					against machines running Solaris,
 *					and seems to result in general
 *					improvement.
 *		Eric Schenk	:	Changed receiver side silly window
 *					avoidance algorithm to BSD style
 *					algorithm. This doubles throughput
 *					against machines running Solaris,
 *					and seems to result in general
 *					improvement.
 *	Stefan Magdalinski	:	adjusted tcp_readable() to fix FIONREAD
 *	Willy Konynenberg	:	Transparent proxying support.
 *					
 * To Fix:
 *		Fast path the code. Two things here - fix the window calculation
 *		so it doesn't iterate over the queue, also spot packets with no funny
 *		options arriving in order and process directly.
 *
 *		Rewrite output state machine to use a single queue.
 *		Speed up input assembly algorithm.
 *		RFC1323 - PAWS and window scaling. PAWS is required for IPv6 so we
 *		could do with it working on IPv4
 *		User settable/learned rtt/max window/mtu
 *
 *		Change the fundamental structure to a single send queue maintained
 *		by TCP (removing the bogus ip stuff [thus fixing mtu drops on
 *		active routes too]). Cut the queue off in tcp_retransmit/
 *		tcp_transmit.
 *		Change the receive queue to assemble as it goes. This lets us
 *		dispose of most of tcp_sequence, half of tcp_ack and chunks of
 *		tcp_data/tcp_read as well as the window shrink crud.
 *		Separate out duplicated code - tcp_alloc_skb, tcp_build_ack
 *		tcp_queue_skb seem obvious routines to extract.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or(at your option) any later version.
 *
 * Description of States:
 *
 *	TCP_SYN_SENT		sent a connection request, waiting for ack
 *
 *	TCP_SYN_RECV		received a connection request, sent ack,
 *				waiting for final ack in three-way handshake.
 *
 *	TCP_ESTABLISHED		connection established
 *
 *	TCP_FIN_WAIT1		our side has shutdown, waiting to complete
 *				transmission of remaining buffered data
 *
 *	TCP_FIN_WAIT2		all buffered data sent, waiting for remote
 *				to shutdown
 *
 *	TCP_CLOSING		both sides have shutdown but we still have
 *				data we have to finish sending
 *
 *	TCP_TIME_WAIT		timeout to catch resent junk before entering
 *				closed, can only be entered from FIN_WAIT2
 *				or CLOSING.  Required because the other end
 *				may not have gotten our last ACK causing it
 *				to retransmit the data packet (which we ignore)
 *
 *	TCP_CLOSE_WAIT		remote side has shutdown and is waiting for
 *				us to finish writing our data and to shutdown
 *				(we have to close() to move on to LAST_ACK)
 *
 *	TCP_LAST_ACK		out side has shutdown after remote has
 *				shutdown.  There may still be data in our
 *				buffer that we have to finish sending
 *
 *	TCP_CLOSE		socket is finished
 */

/*
 * RFC1122 status:
 * NOTE: I'm not going to be doing comments in the code for this one except
 * for violations and the like.  tcp.c is just too big... If I say something
 * "does?" or "doesn't?", it means I'm not sure, and will have to hash it out
 * with Alan. -- MS 950903
 *
 * Use of PSH (4.2.2.2)
 *   MAY aggregate data sent without the PSH flag. (does)
 *   MAY queue data received without the PSH flag. (does)
 *   SHOULD collapse successive PSH flags when it packetizes data. (doesn't)
 *   MAY implement PSH on send calls. (doesn't, thus:)
 *     MUST NOT buffer data indefinitely (doesn't [1 second])
 *     MUST set PSH on last segment (does)
 *   MAY pass received PSH to application layer (doesn't)
 *   SHOULD send maximum-sized segment whenever possible. (almost always does)
 *
 * Window Size (4.2.2.3, 4.2.2.16)
 *   MUST treat window size as an unsigned number (does)
 *   SHOULD treat window size as a 32-bit number (does not)
 *   MUST NOT shrink window once it is offered (does not normally)
 *
 * Urgent Pointer (4.2.2.4)
 * **MUST point urgent pointer to last byte of urgent data (not right
 *     after). (doesn't, to be like BSD)
 *   MUST inform application layer asynchronously of incoming urgent
 *     data. (does)
 *   MUST provide application with means of determining the amount of
 *     urgent data pending. (does)
 * **MUST support urgent data sequence of arbitrary length. (doesn't, but
 *   it's sort of tricky to fix, as urg_ptr is a 16-bit quantity)
 *	[Follows BSD 1 byte of urgent data]
 *
 * TCP Options (4.2.2.5)
 *   MUST be able to receive TCP options in any segment. (does)
 *   MUST ignore unsupported options (does)
 *
 * Maximum Segment Size Option (4.2.2.6)
 *   MUST implement both sending and receiving MSS. (does)
 *   SHOULD send an MSS with every SYN where receive MSS != 536 (MAY send
 *     it always). (does, even when MSS == 536, which is legal)
 *   MUST assume MSS == 536 if no MSS received at connection setup (does)
 *   MUST calculate "effective send MSS" correctly:
 *     min(physical_MTU, remote_MSS+20) - sizeof(tcphdr) - sizeof(ipopts)
 *     (does - but allows operator override)
 *
 * TCP Checksum (4.2.2.7)
 *   MUST generate and check TCP checksum. (does)
 *
 * Initial Sequence Number Selection (4.2.2.8)
 *   MUST use the RFC 793 clock selection mechanism.  (doesn't, but it's
 *     OK: RFC 793 specifies a 250KHz clock, while we use 1MHz, which is
 *     necessary for 10Mbps networks - and harder than BSD to spoof!)
 *
 * Simultaneous Open Attempts (4.2.2.10)
 *   MUST support simultaneous open attempts (does)
 *
 * Recovery from Old Duplicate SYN (4.2.2.11)
 *   MUST keep track of active vs. passive open (does)
 *
 * RST segment (4.2.2.12)
 *   SHOULD allow an RST segment to contain data (does, but doesn't do
 *     anything with it, which is standard)
 *
 * Closing a Connection (4.2.2.13)
 *   MUST inform application of whether connection was closed by RST or
 *     normal close. (does)
 *   MAY allow "half-duplex" close (treat connection as closed for the
 *     local app, even before handshake is done). (does)
 *   MUST linger in TIME_WAIT for 2 * MSL (does)
 *
 * Retransmission Timeout (4.2.2.15)
 *   MUST implement Jacobson's slow start and congestion avoidance
 *     stuff. (does)
 *
 * Probing Zero Windows (4.2.2.17)
 *   MUST support probing of zero windows. (does)
 *   MAY keep offered window closed indefinitely. (does)
 *   MUST allow remote window to stay closed indefinitely. (does)
 *
 * Passive Open Calls (4.2.2.18)
 *   MUST NOT let new passive open affect other connections. (doesn't)
 *   MUST support passive opens (LISTENs) concurrently. (does)
 *
 * Time to Live (4.2.2.19)
 *   MUST make TCP TTL configurable. (does - IP_TTL option)
 *
 * Event Processing (4.2.2.20)
 *   SHOULD queue out-of-order segments. (does)
 *   MUST aggregate ACK segments whenever possible. (does but badly)
 *
 * Retransmission Timeout Calculation (4.2.3.1)
 *   MUST implement Karn's algorithm and Jacobson's algorithm for RTO
 *     calculation. (does, or at least explains them in the comments 8*b)
 *  SHOULD initialize RTO to 0 and RTT to 3. (does)
 *
 * When to Send an ACK Segment (4.2.3.2)
 *   SHOULD implement delayed ACK. (does)
 *   MUST keep ACK delay < 0.5 sec. (does)
 *
 * When to Send a Window Update (4.2.3.3)
 *   MUST implement receiver-side SWS. (does)
 *
 * When to Send Data (4.2.3.4)
 *   MUST implement sender-side SWS. (does)
 *   SHOULD implement Nagle algorithm. (does)
 *
 * TCP Connection Failures (4.2.3.5)
 *  MUST handle excessive retransmissions "properly" (see the RFC). (does)
 *   SHOULD inform application layer of soft errors. (does)
 *
 * TCP Keep-Alives (4.2.3.6)
 *   MAY provide keep-alives. (does)
 *   MUST make keep-alives configurable on a per-connection basis. (does)
 *   MUST default to no keep-alives. (does)
 * **MUST make keep-alive interval configurable. (doesn't)
 * **MUST make default keep-alive interval > 2 hours. (doesn't)
 *   MUST NOT interpret failure to ACK keep-alive packet as dead
 *     connection. (doesn't)
 *   SHOULD send keep-alive with no data. (does)
 *
 * TCP Multihoming (4.2.3.7)
 *   MUST get source address from IP layer before sending first
 *     SYN. (does)
 *   MUST use same local address for all segments of a connection. (does)
 *
 * IP Options (4.2.3.8)
 *   MUST ignore unsupported IP options. (does)
 *   MAY support Time Stamp and Record Route. (does)
 *   MUST allow application to specify a source route. (does)
 *   MUST allow received Source Route option to set route for all future
 *     segments on this connection. (does not (security issues))
 *
 * ICMP messages (4.2.3.9)
 *   MUST act on ICMP errors. (does)
 *   MUST slow transmission upon receipt of a Source Quench. (does)
 *   MUST NOT abort connection upon receipt of soft Destination
 *     Unreachables (0, 1, 5), Time Exceededs and Parameter
 *     Problems. (doesn't)
 *   SHOULD report soft Destination Unreachables etc. to the
 *     application. (does)
 *   SHOULD abort connection upon receipt of hard Destination Unreachable
 *     messages (2, 3, 4). (does)
 *
 * Remote Address Validation (4.2.3.10)
 *   MUST reject as an error OPEN for invalid remote IP address. (does)
 *   MUST ignore SYN with invalid source address. (does)
 *   MUST silently discard incoming SYN for broadcast/multicast
 *     address. (does)
 *
 * Asynchronous Reports (4.2.4.1)
 * MUST provide mechanism for reporting soft errors to application
 *     layer. (does)
 *
 * Type of Service (4.2.4.2)
 *   MUST allow application layer to set Type of Service. (does IP_TOS)
 *
 * (Whew. -- MS 950903)
 **/

#include <linux/config.h>
#include <linux/types.h>
#include <linux/fcntl.h>

#include <net/icmp.h>
#include <net/tcp.h>

#include <asm/segment.h>

unsigned long seq_offset;
struct tcp_mib	tcp_statistics;

static void tcp_close(struct sock *sk, unsigned long timeout);

/*
 *	Find someone to 'accept'. Must be called with
 *	the socket locked or with interrupts disabled
 */

static struct sk_buff *tcp_find_established(struct sock *s)
{
	struct sk_buff *p=skb_peek(&s->receive_queue);
	if(p==NULL)
		return NULL;
	do
	{
		if(p->sk->state == TCP_ESTABLISHED || p->sk->state >= TCP_FIN_WAIT1)
			return p;
		p=p->next;
	}
	while(p!=(struct sk_buff *)&s->receive_queue);
	return NULL;
}

/*
 *	This routine closes sockets which have been at least partially
 *	opened, but not yet accepted. Currently it is only called by
 *	tcp_close, and timeout mirrors the value there.
 */

static void tcp_close_pending (struct sock *sk)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&sk->receive_queue)) != NULL)
	{
		tcp_close(skb->sk, 0);
		kfree_skb(skb, FREE_READ);
	}
	return;
}

/*
 *	Enter the time wait state.
 */

void tcp_time_wait(struct sock *sk)
{
	tcp_set_state(sk,TCP_TIME_WAIT);
	sk->shutdown = SHUTDOWN_MASK;
	if (!sk->dead)
		sk->state_change(sk);
	tcp_reset_msl_timer(sk, TIME_CLOSE, TCP_TIMEWAIT_LEN);
}


/*
 * This routine is called by the ICMP module when it gets some
 * sort of error condition.  If err < 0 then the socket should
 * be closed and the error returned to the user.  If err > 0
 * it's just the icmp type << 8 | icmp code.  After adjustment
 * header points to the first 8 bytes of the tcp header.  We need
 * to find the appropriate port.
 */

void tcp_err(int type, int code, unsigned char *header, __u32 daddr,
	__u32 saddr, struct inet_protocol *protocol)
{
	struct tcphdr *th = (struct tcphdr *)header;
	struct sock *sk;

	/*
	 *	This one is _WRONG_. FIXME urgently.
	 */
#ifndef CONFIG_NO_PATH_MTU_DISCOVERY
	struct iphdr *iph=(struct iphdr *)(header-sizeof(struct iphdr));
#endif
	th =(struct tcphdr *)header;
	sk = get_sock(&tcp_prot, th->source, daddr, th->dest, saddr, 0, 0);

	if (sk == NULL)
		return;

	if (type == ICMP_SOURCE_QUENCH)
	{
		/*
		 * FIXME:
		 * Follow BSD for now and just reduce cong_window to 1 again.
		 * It is possible that we just want to reduce the
		 * window by 1/2, or that we want to reduce ssthresh by 1/2
		 * here as well.
		 */
		sk->cong_window = 1;
		sk->high_seq = sk->sent_seq;
		return;
	}

	if (type == ICMP_PARAMETERPROB)
	{
		sk->err=EPROTO;
		sk->error_report(sk);
	}

#ifndef CONFIG_NO_PATH_MTU_DISCOVERY
	if (type == ICMP_DEST_UNREACH && code == ICMP_FRAG_NEEDED)
	{
		struct rtable * rt;
		/*
		 * Ugly trick to pass MTU to protocol layer.
		 * Really we should add argument "info" to error handler.
		 */
		unsigned short new_mtu = ntohs(iph->id);

		if ((rt = sk->ip_route_cache) != NULL)
			if (rt->rt_mtu > new_mtu)
				rt->rt_mtu = new_mtu;

		/*
		 *	FIXME::
		 *	Not the nicest of fixes: Lose a MTU update if the socket is
		 *	locked this instant. Not the right answer but will be best
		 *	for the production fix. Make 2.1 work right!
		 */
		 
		if (sk->mtu > new_mtu - sizeof(struct iphdr) - sizeof(struct tcphdr)
			&& new_mtu > sizeof(struct iphdr)+sizeof(struct tcphdr) && !sk->users)
			sk->mtu = new_mtu - sizeof(struct iphdr) - sizeof(struct tcphdr);

		return;
	}
#endif

	/*
	 * If we've already connected we will keep trying
	 * until we time out, or the user gives up.
	 */

	if (code < 13)
	{
		if(icmp_err_convert[code].fatal || sk->state == TCP_SYN_SENT || sk->state == TCP_SYN_RECV)
		{
			sk->err = icmp_err_convert[code].errno;
			if (sk->state == TCP_SYN_SENT || sk->state == TCP_SYN_RECV)
			{
				tcp_statistics.TcpAttemptFails++;
				tcp_set_state(sk,TCP_CLOSE);
				sk->error_report(sk);		/* Wake people up to see the error (see connect in sock.c) */
			}
		}
		else	/* Only an error on timeout */
			sk->err_soft = icmp_err_convert[code].errno;
	}
}


/*
 *	Walk down the receive queue counting readable data until we hit the end or we find a gap
 *	in the received data queue (ie a frame missing that needs sending to us). Not
 *	sorting using two queues as data arrives makes life so much harder.
 */

static int tcp_readable(struct sock *sk)
{
	unsigned long counted;
	unsigned long amount;
	struct sk_buff *skb;
	int sum;
	unsigned long flags;

	if(sk && sk->debug)
	  	printk("tcp_readable: %p - ",sk);

	save_flags(flags);
	cli();
	if (sk == NULL || (skb = skb_peek(&sk->receive_queue)) == NULL)
	{
		restore_flags(flags);
	  	if(sk && sk->debug)
	  		printk("empty\n");
	  	return(0);
	}

	counted = sk->copied_seq;	/* Where we are at the moment */
	amount = 0;

	/*
	 *	Do until a push or until we are out of data.
	 */

	do
	{
		if (before(counted, skb->seq))	 	/* Found a hole so stops here */
			break;
		sum = skb->len - (counted - skb->seq);	/* Length - header but start from where we are up to (avoid overlaps) */
		if (skb->h.th->syn)
			sum++;
		if (sum > 0)
		{					/* Add it up, move on */
			amount += sum;
			if (skb->h.th->syn)
				amount--;
			counted += sum;
		}
		/*
		 * Don't count urg data ... but do it in the right place!
		 * Consider: "old_data (ptr is here) URG PUSH data"
		 * The old code would stop at the first push because
		 * it counted the urg (amount==1) and then does amount--
		 * *after* the loop.  This means tcp_readable() always
		 * returned zero if any URG PUSH was in the queue, even
		 * though there was normal data available. If we subtract
		 * the urg data right here, we even get it to work for more
		 * than one URG PUSH skb without normal data.
		 * This means that select() finally works now with urg data
		 * in the queue.  Note that rlogin was never affected
		 * because it doesn't use select(); it uses two processes
		 * and a blocking read().  And the queue scan in tcp_read()
		 * was correct.  Mike <pall@rz.uni-karlsruhe.de>
		 */
		if (skb->h.th->urg)
			amount--;	/* don't count urg data */
/*		if (amount && skb->h.th->psh) break;*/
		skb = skb->next;
	}
	while(skb != (struct sk_buff *)&sk->receive_queue);

	restore_flags(flags);
	if(sk->debug)
	  	printk("got %lu bytes.\n",amount);
	return(amount);
}

/*
 * LISTEN is a special case for select..
 */
static int tcp_listen_select(struct sock *sk, int sel_type, select_table *wait)
{
	if (sel_type == SEL_IN) {
		struct sk_buff * skb;

		lock_sock(sk);
		skb = tcp_find_established(sk);
		release_sock(sk);
		if (skb)
			return 1;
		select_wait(sk->sleep,wait);
		return 0;
	}
	return 0;
}


/*
 *	Wait for a TCP event.
 *
 *	Note that we don't need to lock the socket, as the upper select layers
 *	take care of normal races (between the test and the event) and we don't
 *	go look at any of the socket buffers directly.
 */
static int tcp_select(struct sock *sk, int sel_type, select_table *wait)
{
	if (sk->state == TCP_LISTEN)
		return tcp_listen_select(sk, sel_type, wait);

	switch(sel_type) {
	case SEL_IN:
		if (sk->err)
			return 1;
		if (sk->state == TCP_SYN_SENT || sk->state == TCP_SYN_RECV)
			break;

		if (sk->shutdown & RCV_SHUTDOWN)
			return 1;

		if (sk->acked_seq == sk->copied_seq)
			break;

		if (sk->urg_seq != sk->copied_seq ||
		    sk->acked_seq != sk->copied_seq+1 ||
		    sk->urginline || !sk->urg_data)
			return 1;
		break;

	case SEL_OUT:
		if (sk->err)
			return 1;
		if (sk->shutdown & SEND_SHUTDOWN)
			return 0;
		if (sk->state == TCP_SYN_SENT || sk->state == TCP_SYN_RECV)
			break;
		/*
		 * This is now right thanks to a small fix
		 * by Matt Dillon.
		 */

		if (sock_wspace(sk) < sk->mtu+128+sk->prot->max_header)
			break;
		return 1;

	case SEL_EX:
		if (sk->urg_data)
			return 1;
		break;
	}
	select_wait(sk->sleep, wait);
	return 0;
}

int tcp_ioctl(struct sock *sk, int cmd, unsigned long arg)
{
	int err;
	switch(cmd)
	{

		case TIOCINQ:
#ifdef FIXME	/* FIXME: */
		case FIONREAD:
#endif
		{
			unsigned long amount;

			if (sk->state == TCP_LISTEN)
				return(-EINVAL);

			lock_sock(sk);
			amount = tcp_readable(sk);
			release_sock(sk);
			err=verify_area(VERIFY_WRITE,(void *)arg, sizeof(int));
			if(err)
				return err;
			put_user(amount, (int *)arg);
			return(0);
		}
		case SIOCATMARK:
		{
			int answ = sk->urg_data && sk->urg_seq == sk->copied_seq;

			err = verify_area(VERIFY_WRITE,(void *) arg, sizeof(int));
			if (err)
				return err;
			put_user(answ,(int *) arg);
			return(0);
		}
		case TIOCOUTQ:
		{
			unsigned long amount;

			if (sk->state == TCP_LISTEN) return(-EINVAL);
			amount = sock_wspace(sk);
			err=verify_area(VERIFY_WRITE,(void *)arg, sizeof(int));
			if(err)
				return err;
			put_user(amount, (int *)arg);
			return(0);
		}
		default:
			return(-EINVAL);
	}
}


/*
 *	This routine computes a TCP checksum.
 *
 *	Modified January 1995 from a go-faster DOS routine by
 *	Jorge Cwik <jorge@laser.satlink.net>
 */
#undef DEBUG_TCP_CHECK
void tcp_send_check(struct tcphdr *th, unsigned long saddr,
		unsigned long daddr, int len, struct sk_buff *skb)
{
#ifdef DEBUG_TCP_CHECK
	u16 check;
#endif
	th->check = 0;
	th->check = tcp_check(th, len, saddr, daddr,
		csum_partial((char *)th,sizeof(*th),skb->csum));

#ifdef DEBUG_TCP_CHECK
	check = th->check;
	th->check = 0;
	th->check = tcp_check(th, len, saddr, daddr,
		csum_partial((char *)th,len,0));
	if (check != th->check) {
		static int count = 0;
		if (++count < 10) {
			printk("Checksum %x (%x) from %p\n", th->check, check,
				(&th)[-1]);
			printk("TCP=<off:%d a:%d s:%d f:%d>\n", th->doff*4, th->ack, th->syn, th->fin);
		}
	}
#endif
}


/*
 *	This routine builds a generic TCP header.
 */

static inline int tcp_build_header(struct tcphdr *th, struct sock *sk, int push)
{
	memcpy(th,(void *) &(sk->dummy_th), sizeof(*th));
	th->psh = (push == 0) ? 1 : 0;
	th->seq = htonl(sk->write_seq);
	th->ack_seq = htonl(sk->acked_seq);
	th->window = htons(tcp_select_window(sk));

	return(sizeof(*th));
}

/*
 *	Wait for a socket to get into the connected state
 */
static void wait_for_tcp_connect(struct sock * sk)
{
	release_sock(sk);
	cli();
	if (sk->state != TCP_ESTABLISHED && sk->state != TCP_CLOSE_WAIT && sk->err == 0)
    	{
		interruptible_sleep_on(sk->sleep);
	}
	sti();
	lock_sock(sk);
}

/*
 *	Wait for more memory for a socket
 */
static void wait_for_tcp_memory(struct sock * sk)
{
	release_sock(sk);
	cli();
	if (sk->wmem_alloc*2 > sk->sndbuf &&
	    (sk->state == TCP_ESTABLISHED||sk->state == TCP_CLOSE_WAIT)
		&& sk->err == 0)
	{
		sk->socket->flags &= ~SO_NOSPACE;
		interruptible_sleep_on(sk->sleep);
	}
	sti();
	lock_sock(sk);
}


/*
 *	This routine copies from a user buffer into a socket,
 *	and starts the transmit system.
 */

static int do_tcp_sendmsg(struct sock *sk,
	int iovlen, struct iovec *iov,
	int len, int nonblock, int flags)
{
	int copied = 0;
	struct device *dev = NULL;

	/*
	 *	Wait for a connection to finish.
	 */
	while (sk->state != TCP_ESTABLISHED && sk->state != TCP_CLOSE_WAIT)
	{
		if (sk->err)
			return sock_error(sk);

		if (sk->state != TCP_SYN_SENT && sk->state != TCP_SYN_RECV)
		{
			if (sk->keepopen)
				send_sig(SIGPIPE, current, 0);
			return -EPIPE;
		}

		if (nonblock)
			return -EAGAIN;

		if (current->signal & ~current->blocked)
			return -ERESTARTSYS;

		wait_for_tcp_connect(sk);
	}

	/*
	 *	Ok commence sending
	 */

	while (--iovlen >= 0)
	{
		int seglen=iov->iov_len;
		unsigned char * from=iov->iov_base;
		iov++;

		while(seglen > 0)
		{
			int copy, delay;
			int tmp;
			struct sk_buff *skb;

			/*
			 * Stop on errors
			 */
			if (sk->err)
			{
				if (copied)
					return copied;
				return sock_error(sk);
			}

			/*
			 *	Make sure that we are established.
			 */
			if (sk->shutdown & SEND_SHUTDOWN)
			{
				if (copied)
					return copied;
				return -EPIPE;
			}

			/*
			 * The following code can result in copy <= if sk->mss is ever
			 * decreased.  It shouldn't be.  sk->mss is min(sk->mtu, sk->max_window).
			 * sk->mtu is constant once SYN processing is finished.  I.e. we
			 * had better not get here until we've seen his SYN and at least one
			 * valid ack.  (The SYN sets sk->mtu and the ack sets sk->max_window.)
			 * But ESTABLISHED should guarantee that.  sk->max_window is by definition
			 * non-decreasing.  Note that any ioctl to set user_mss must be done
			 * before the exchange of SYN's.  If the initial ack from the other
			 * end has a window of 0, max_window and thus mss will both be 0.
			 */

			/*
			 *	Now we need to check if we have a half built packet.
			 */
#ifndef CONFIG_NO_PATH_MTU_DISCOVERY
			/*
			 *	FIXME:  I'm almost sure that this fragment is BUG,
			 *		but it works... I do not know why 8) --ANK
			 *
			 *	Really, we should rebuild all the queues...
			 *	It's difficult. Temporary hack is to send all
			 *	queued segments with allowed fragmentation.
			 */
			{
				int new_mss = min(sk->mtu, sk->max_window);
				if (new_mss < sk->mss)
				{
					tcp_send_partial(sk);
					sk->mss = new_mss;
				}
			}
#endif

			/*
			 *	If there is a partly filled frame we can fill
			 *	out.
			 */
			if ((skb = tcp_dequeue_partial(sk)) != NULL)
			{
				int tcp_size;

				tcp_size = skb->tail - (unsigned char *)(skb->h.th + 1);

				/* Add more stuff to the end of skb->len */
				if (!(flags & MSG_OOB))
				{
					copy = min(sk->mss - tcp_size, seglen);
					
					/*
					 *	Now we may find the frame is as big, or too
					 *	big for our MSS. Thats all fine. It means the
					 *	MSS shrank (from an ICMP) after we allocated 
					 *	this frame.
					 */
					 
					if (copy <= 0)
					{
						/*
						 *	Send the now forced complete frame out. 
						 *
						 *	Note for 2.1: The MSS reduce code ought to
						 *	flush any frames in partial that are now
						 *	full sized. Not serious, potential tiny
						 *	performance hit.
						 */
						tcp_send_skb(sk,skb);
						/*
						 *	Get a new buffer and try again.
						 */
						continue;
					}
					/*
					 *	Otherwise continue to fill the buffer.
					 */
					tcp_size += copy;
					memcpy_fromfs(skb_put(skb,copy), from, copy);
					skb->csum = csum_partial(skb->tail - tcp_size, tcp_size, 0);
					from += copy;
					copied += copy;
					len -= copy;
					sk->write_seq += copy;
					seglen -= copy;
				}
				if (tcp_size >= sk->mss || (flags & MSG_OOB) || !sk->packets_out)
					tcp_send_skb(sk, skb);
				else
					tcp_enqueue_partial(skb, sk);
				continue;
			}

		/*
		 * We also need to worry about the window.
	 	 * If window < 1/2 the maximum window we've seen from this
	 	 *   host, don't use it.  This is sender side
	 	 *   silly window prevention, as specified in RFC1122.
	 	 *   (Note that this is different than earlier versions of
		 *   SWS prevention, e.g. RFC813.).  What we actually do is
		 *   use the whole MSS.  Since the results in the right
		 *   edge of the packet being outside the window, it will
		 *   be queued for later rather than sent.
		 */

			copy = sk->window_seq - sk->write_seq;
			if (copy <= 0 || copy < (sk->max_window >> 1) || copy > sk->mss)
				copy = sk->mss;
			if (copy > seglen)
				copy = seglen;
			if (copy <= 0)
			{
				printk(KERN_CRIT "TCP: **bug**: copy=%d, sk->mss=%d\n", copy, sk->mss);
		  		return -EFAULT;
			}

			/*
			 *	We should really check the window here also.
			 */

			delay = 0;
			tmp = copy + sk->prot->max_header + 15;
			if (copy < sk->mss && !(flags & MSG_OOB) && sk->packets_out)
			{
				tmp = tmp - copy + sk->mtu + 128;
				delay = 1;
			}
			skb = sock_wmalloc(sk, tmp, 0, GFP_KERNEL);

			/*
			 *	If we didn't get any memory, we need to sleep.
			 */

			if (skb == NULL)
			{
				sk->socket->flags |= SO_NOSPACE;
				if (nonblock)
				{
					if (copied)
						return copied;
					return -EAGAIN;
				}

				if (current->signal & ~current->blocked)
				{
					if (copied)
						return copied;
					return -ERESTARTSYS;
				}

				wait_for_tcp_memory(sk);
				continue;
			}

			skb->sk = sk;
			skb->free = 0;
			skb->localroute = sk->localroute|(flags&MSG_DONTROUTE);

			/*
			 * FIXME: we need to optimize this.
			 * Perhaps some hints here would be good.
			 */

			tmp = sk->prot->build_header(skb, sk->saddr, sk->daddr, &dev,
				 IPPROTO_TCP, sk->opt, skb->truesize,sk->ip_tos,sk->ip_ttl,&sk->ip_route_cache);
			if (tmp < 0 )
			{
				sock_wfree(sk, skb);
				if (copied)
					return(copied);
				return(tmp);
			}
#ifndef CONFIG_NO_PATH_MTU_DISCOVERY
			skb->ip_hdr->frag_off |= htons(IP_DF);
#endif
			skb->dev = dev;
			skb->h.th =(struct tcphdr *)skb_put(skb,sizeof(struct tcphdr));
			tmp = tcp_build_header(skb->h.th, sk, seglen-copy);
			if (tmp < 0)
			{
				sock_wfree(sk, skb);
				if (copied)
					return(copied);
				return(tmp);
			}

			if (flags & MSG_OOB)
			{
				skb->h.th->urg = 1;
				skb->h.th->urg_ptr = ntohs(copy);
			}

			skb->csum = csum_partial_copy_fromuser(from,
				skb_put(skb,copy), copy, 0);

			from += copy;
			copied += copy;
			len -= copy;
			seglen -= copy;
			skb->free = 0;
			sk->write_seq += copy;

			if (delay)
			{
				tcp_enqueue_partial(skb, sk);
				continue;
			}
			tcp_send_skb(sk, skb);
		}
	}
	sk->err = 0;

	return copied;
}


static int tcp_sendmsg(struct sock *sk, struct msghdr *msg,
	  int len, int nonblock, int flags)
{
	int retval = -EINVAL;

	/*
	 *	Do sanity checking for sendmsg/sendto/send
	 */

	if (flags & ~(MSG_OOB|MSG_DONTROUTE))
		goto out;
	if (msg->msg_name) {
		struct sockaddr_in *addr=(struct sockaddr_in *)msg->msg_name;

		if (msg->msg_namelen < sizeof(*addr))
			goto out;
		if (addr->sin_family && addr->sin_family != AF_INET)
			goto out;
		retval = -ENOTCONN;
		if(sk->state == TCP_CLOSE)
			goto out;
		retval = -EISCONN;
		if (addr->sin_port != sk->dummy_th.dest)
			goto out;
		if (addr->sin_addr.s_addr != sk->daddr)
			goto out;
	}

	lock_sock(sk);
	retval = do_tcp_sendmsg(sk, msg->msg_iovlen, msg->msg_iov, len, nonblock, flags);

/*
 *	Nagle's rule. Turn Nagle off with TCP_NODELAY for highly
 *	interactive fast network servers. It's meant to be on and
 *	it really improves the throughput though not the echo time
 *	on my slow slip link - Alan
 *
 *	If not nagling we can send on the before case too..
 */

	if (sk->partial) {
		if (!sk->packets_out ||
		    (sk->nonagle && before(sk->write_seq , sk->window_seq))) {
	  		tcp_send_partial(sk);
	  	}
	}

	release_sock(sk);

out:
	return retval;
}


/*
 *	Send an ack if one is backlogged at this point.
 */

void tcp_read_wakeup(struct sock *sk)
{
	if (!sk->ack_backlog)
		return;

	/*
	 * If we're closed, don't send an ack, or we'll get a RST
	 * from the closed destination.
	 */
	if ((sk->state == TCP_CLOSE) || (sk->state == TCP_TIME_WAIT))
		return;

	tcp_send_ack(sk);
}


/*
 *	Handle reading urgent data. BSD has very simple semantics for
 *	this, no blocking and very strange errors 8)
 */

static int tcp_recv_urg(struct sock * sk, int nonblock,
	     struct msghdr *msg, int len, int flags, int *addr_len)
{
	/*
	 *	No URG data to read
	 */
	if (sk->urginline || !sk->urg_data || sk->urg_data == URG_READ)
		return -EINVAL;	/* Yes this is right ! */

	if (sk->err)
		return sock_error(sk);

	if (sk->state == TCP_CLOSE || sk->done)
	{
		if (!sk->done)
		{
			sk->done = 1;
			return 0;
		}
		return -ENOTCONN;
	}

	if (sk->shutdown & RCV_SHUTDOWN)
	{
		sk->done = 1;
		return 0;
	}
	lock_sock(sk);
	if (sk->urg_data & URG_VALID)
	{
		char c = sk->urg_data;
		if (!(flags & MSG_PEEK))
			sk->urg_data = URG_READ;
		memcpy_toiovec(msg->msg_iov, &c, 1);
		if(msg->msg_name)
		{
			struct sockaddr_in *sin=(struct sockaddr_in *)msg->msg_name;
			sin->sin_family=AF_INET;
			sin->sin_addr.s_addr=sk->daddr;
			sin->sin_port=sk->dummy_th.dest;
		}
		if(addr_len)
			*addr_len=sizeof(struct sockaddr_in);
		release_sock(sk);
		return 1;
	}
	release_sock(sk);

	/*
	 * Fixed the recv(..., MSG_OOB) behaviour.  BSD docs and
	 * the available implementations agree in this case:
	 * this call should never block, independent of the
	 * blocking state of the socket.
	 * Mike <pall@rz.uni-karlsruhe.de>
	 */
	return -EAGAIN;
}

/*
 *	Release a skb if it is no longer needed. This routine
 *	must be called with interrupts disabled or with the
 *	socket locked so that the sk_buff queue operation is ok.
 */

static inline void tcp_eat_skb(struct sock *sk, struct sk_buff * skb)
{
	skb->sk = sk;
	__skb_unlink(skb, &sk->receive_queue);
	kfree_skb(skb, FREE_READ);
}

/*
 *	FIXME:
 *	This routine frees used buffers.
 *	It should consider sending an ACK to let the
 *	other end know we now have a bigger window.
 */

static void cleanup_rbuf(struct sock *sk)
{
	/*
	 * NOTE! The socket must be locked, so that we don't get
	 * a messed-up receive queue.
	 */
	while (!skb_queue_empty(&sk->receive_queue)) {
		struct sk_buff *skb = sk->receive_queue.next;
		if (!skb->used || skb->users)
			break;
		tcp_eat_skb(sk, skb);
	}

	/*
	 * Tell the world if we raised the window.
	 */
	if (tcp_raise_window(sk))
		tcp_send_ack(sk);
}


/*
 *	This routine copies from a sock struct into the user buffer.
 */

static int tcp_recvmsg(struct sock *sk, struct msghdr *msg,
	int len, int nonblock, int flags, int *addr_len)
{
	struct wait_queue wait = { current, NULL };
	int copied = 0;
	u32 peek_seq;
	volatile u32 *seq;	/* So gcc doesn't overoptimise */
	unsigned long used;

	/*
	 *	This error should be checked.
	 */

	if (sk->state == TCP_LISTEN)
		return -ENOTCONN;

	/*
	 *	Urgent data needs to be handled specially.
	 */

	if (flags & MSG_OOB)
		return tcp_recv_urg(sk, nonblock, msg, len, flags, addr_len);

	/*
	 *	Copying sequence to update. This is volatile to handle
	 *	the multi-reader case neatly (memcpy_to/fromfs might be
	 *	inline and thus not flush cached variables otherwise).
	 */

	peek_seq = sk->copied_seq;
	seq = &sk->copied_seq;
	if (flags & MSG_PEEK)
		seq = &peek_seq;

	add_wait_queue(sk->sleep, &wait);
	lock_sock(sk);
	while (len > 0)
	{
		struct sk_buff * skb;
		u32 offset;

		/*
		 * Are we at urgent data? Stop if we have read anything.
		 */

		if (copied && sk->urg_data && sk->urg_seq == *seq)
			break;

		/*
		 * We need to check signals first, to get correct SIGURG
		 * handling.
		 */
		if (current->signal & ~current->blocked) {
			if (copied)
				break;
			copied = -ERESTARTSYS;
			break;
		}

		/*
		 *	Next get a buffer.
		 */

		current->state = TASK_INTERRUPTIBLE;

		skb = sk->receive_queue.next;
		while (skb != (struct sk_buff *)&sk->receive_queue)
		{
			if (before(*seq, skb->seq))
				break;
			offset = *seq - skb->seq;
			if (skb->h.th->syn)
				offset--;
			if (offset < skb->len)
				goto found_ok_skb;
			if (skb->h.th->fin)
				goto found_fin_ok;
			if (!(flags & MSG_PEEK))
				skb->used = 1;
			skb = skb->next;
		}

		if (copied)
			break;

		if (sk->err)
		{
			copied = sock_error(sk);
			break;
		}

		if (sk->state == TCP_CLOSE)
		{
			if (!sk->done)
			{
				sk->done = 1;
				break;
			}
			copied = -ENOTCONN;
			break;
		}

		if (sk->shutdown & RCV_SHUTDOWN)
		{
			sk->done = 1;
			break;
		}

		if (nonblock)
		{
			copied = -EAGAIN;
			break;
		}

		cleanup_rbuf(sk);
		release_sock(sk);
		sk->socket->flags |= SO_WAITDATA;
		schedule();
		sk->socket->flags &= ~SO_WAITDATA;
		lock_sock(sk);
		continue;

	found_ok_skb:
		/*
		 *	Lock the buffer. We can be fairly relaxed as
		 *	an interrupt will never steal a buffer we are
		 *	using unless I've missed something serious in
		 *	tcp_data.
		 */

		skb->users++;

		/*
		 *	Ok so how much can we use ?
		 */

		used = skb->len - offset;
		if (len < used)
			used = len;
		/*
		 *	Do we have urgent data here?
		 */

		if (sk->urg_data)
		{
			u32 urg_offset = sk->urg_seq - *seq;
			if (urg_offset < used)
			{
				if (!urg_offset)
				{
					if (!sk->urginline)
					{
						++*seq;
						offset++;
						used--;
					}
				}
				else
					used = urg_offset;
			}
		}

		/*
		 *	Copy it - We _MUST_ update *seq first so that we
		 *	don't ever double read when we have dual readers
		 */

		*seq += used;

		/*
		 *	This memcpy_tofs can sleep. If it sleeps and we
		 *	do a second read it relies on the skb->users to avoid
		 *	a crash when cleanup_rbuf() gets called.
		 */

		memcpy_toiovec(msg->msg_iov,((unsigned char *)skb->h.th) +
			skb->h.th->doff*4 + offset, used);
		copied += used;
		len -= used;

		/*
		 *	We now will not sleep again until we are finished
		 *	with skb. Sorry if you are doing the SMP port
		 *	but you'll just have to fix it neatly ;)
		 */

		skb->users --;

		if (after(sk->copied_seq,sk->urg_seq))
			sk->urg_data = 0;
		if (used + offset < skb->len)
			continue;

		/*
		 *	Process the FIN.
		 */

		if (skb->h.th->fin)
			goto found_fin_ok;
		if (flags & MSG_PEEK)
			continue;
		skb->used = 1;
		if (!skb->users)
			tcp_eat_skb(sk, skb);
		continue;

	found_fin_ok:
		++*seq;
		if (flags & MSG_PEEK)
			break;

		/*
		 *	All is done
		 */

		skb->used = 1;
		sk->shutdown |= RCV_SHUTDOWN;
		break;

	}

	if(copied>0 && msg->msg_name)
	{
		struct sockaddr_in *sin=(struct sockaddr_in *)msg->msg_name;
		sin->sin_family=AF_INET;
		sin->sin_addr.s_addr=sk->daddr;
		sin->sin_port=sk->dummy_th.dest;
	}
	if(addr_len)
		*addr_len=sizeof(struct sockaddr_in);

	remove_wait_queue(sk->sleep, &wait);
	current->state = TASK_RUNNING;

	/* Clean up data we have read: This will do ACK frames */
	cleanup_rbuf(sk);
	release_sock(sk);
	return copied;
}



/*
 *	State processing on a close. This implements the state shift for
 *	sending our FIN frame. Note that we only send a FIN for some
 *	states. A shutdown() may have already sent the FIN, or we may be
 *	closed.
 */

static int tcp_close_state(struct sock *sk, int dead)
{
	int ns=TCP_CLOSE;
	int send_fin=0;
	switch(sk->state)
	{
		case TCP_SYN_SENT:	/* No SYN back, no FIN needed */
			break;
		case TCP_SYN_RECV:
		case TCP_ESTABLISHED:	/* Closedown begin */
			ns=TCP_FIN_WAIT1;
			send_fin=1;
			break;
		case TCP_FIN_WAIT1:	/* Already closing, or FIN sent: no change */
		case TCP_FIN_WAIT2:
		case TCP_CLOSING:
			ns=sk->state;
			break;
		case TCP_CLOSE:
		case TCP_LISTEN:
			break;
		case TCP_CLOSE_WAIT:	/* They have FIN'd us. We send our FIN and
					   wait only for the ACK */
			ns=TCP_LAST_ACK;
			send_fin=1;
	}

	tcp_set_state(sk,ns);

	/*
	 *	This is a (useful) BSD violating of the RFC. There is a
	 *	problem with TCP as specified in that the other end could
	 *	keep a socket open forever with no application left this end.
	 *	We use a 3 minute timeout (about the same as BSD) then kill
	 *	our end. If they send after that then tough - BUT: long enough
	 *	that we won't make the old 4*rto = almost no time - whoops
	 *	reset mistake.
	 */
	if(dead && ns==TCP_FIN_WAIT2)
	{
		int timer_active=del_timer(&sk->timer);
		if(timer_active)
			add_timer(&sk->timer);
		else
			tcp_reset_msl_timer(sk, TIME_CLOSE, TCP_FIN_TIMEOUT);
	}

	return send_fin;
}

/*
 *	Shutdown the sending side of a connection. Much like close except
 *	that we don't receive shut down or set sk->dead.
 */

void tcp_shutdown(struct sock *sk, int how)
{
	/*
	 *	We need to grab some memory, and put together a FIN,
	 *	and then put it into the queue to be sent.
	 *		Tim MacKenzie(tym@dibbler.cs.monash.edu.au) 4 Dec '92.
	 */

	if (!(how & SEND_SHUTDOWN))
		return;

	/*
	 *	If we've already sent a FIN, or it's a closed state
	 */

	if (sk->state == TCP_FIN_WAIT1 ||
	    sk->state == TCP_FIN_WAIT2 ||
	    sk->state == TCP_CLOSING ||
	    sk->state == TCP_LAST_ACK ||
	    sk->state == TCP_TIME_WAIT ||
	    sk->state == TCP_CLOSE ||
	    sk->state == TCP_LISTEN
	  )
	{
		return;
	}
	lock_sock(sk);

	/*
	 * flag that the sender has shutdown
	 */

	sk->shutdown |= SEND_SHUTDOWN;

	/*
	 *  Clear out any half completed packets.
	 */

	if (sk->partial)
		tcp_send_partial(sk);

	/*
	 *	FIN if needed
	 */

	if (tcp_close_state(sk,0))
		tcp_send_fin(sk);

	release_sock(sk);
}


/*
 *	Return 1 if we still have things to send in our buffers.
 */

static inline int closing(struct sock * sk)
{
	switch (sk->state) {
		case TCP_FIN_WAIT1:
		case TCP_CLOSING:
		case TCP_LAST_ACK:
			return 1;
	}
	return 0;
}


static void tcp_close(struct sock *sk, unsigned long timeout)
{
	struct sk_buff *skb;

	/*
	 * We need to grab some memory, and put together a FIN,
	 * and then put it into the queue to be sent.
	 */

	lock_sock(sk);

	tcp_cache_zap();
	if(sk->state == TCP_LISTEN)
	{
		/* Special case */
		tcp_set_state(sk, TCP_CLOSE);
		tcp_close_pending(sk);
		release_sock(sk);
		sk->dead = 1;
		return;
	}

	sk->keepopen = 1;
	sk->shutdown = SHUTDOWN_MASK;

	if (!sk->dead)
	  	sk->state_change(sk);

	/*
	 *  We need to flush the recv. buffs.  We do this only on the
	 *  descriptor close, not protocol-sourced closes, because the
	 *  reader process may not have drained the data yet!
	 */

	while((skb=skb_dequeue(&sk->receive_queue))!=NULL)
		kfree_skb(skb, FREE_READ);

	/*
	 *	Get rid off any half-completed packets.
	 */

	if (sk->partial)
		tcp_send_partial(sk);

	/*
	 *	Timeout is not the same thing - however the code likes
	 *	to send both the same way (sigh).
	 */

	if (tcp_close_state(sk,1)==1)
	{
		tcp_send_fin(sk);
	}

	if (timeout) {
		cli();
		release_sock(sk);
		current->timeout = timeout;
		while(closing(sk) && current->timeout)
		{
			interruptible_sleep_on(sk->sleep);
			if (current->signal & ~current->blocked)
			{
				break;
			}
		}
		current->timeout=0;
		lock_sock(sk);
		sti();
	}

	/*
	 * This will destroy it. The timers will take care of actually
	 * free'ing up the memory.
	 */
	tcp_cache_zap();	/* Kill the cache again. */

	/* Now that the socket is dead, if we are in the FIN_WAIT2 state
	 * we may need to set up a timer.
         */
	if (sk->state==TCP_FIN_WAIT2)
	{
		int timer_active=del_timer(&sk->timer);
		if(timer_active)
			add_timer(&sk->timer);
		else
			tcp_reset_msl_timer(sk, TIME_CLOSE, TCP_FIN_TIMEOUT);
	}

	release_sock(sk);
	sk->dead = 1;
}


/*
 * Wait for a incoming connection, avoid race
 * conditions. This must be called with the socket
 * locked.
 */
static struct sk_buff * wait_for_connect(struct sock * sk)
{
	struct wait_queue wait = { current, NULL };
	struct sk_buff * skb = NULL;

	add_wait_queue(sk->sleep, &wait);
	for (;;) {
		current->state = TASK_INTERRUPTIBLE;
		release_sock(sk);
		schedule();
		lock_sock(sk);
		skb = tcp_find_established(sk);
		if (skb)
			break;
		if (current->signal & ~current->blocked)
			break;
	}
	remove_wait_queue(sk->sleep, &wait);
	return skb;
}

/*
 *	This will accept the next outstanding connection.
 *
 *	Be careful about race conditions here - this is subtle.
 */

static struct sock *tcp_accept(struct sock *sk, int flags)
{
	int error;
	struct sk_buff *skb;
	struct sock *newsk = NULL;

  /*
   * We need to make sure that this socket is listening,
   * and that it has something pending.
   */

	error = EINVAL;
	if (sk->state != TCP_LISTEN)
		goto no_listen;

	lock_sock(sk);

	skb = tcp_find_established(sk);
	if (skb) {
got_new_connect:
		__skb_unlink(skb, &sk->receive_queue);
		newsk = skb->sk;
		kfree_skb(skb, FREE_READ);
		sk->ack_backlog--;
		error = 0;
out:
		release_sock(sk);
no_listen:
		sk->err = error;
		return newsk;
	}

	error = EAGAIN;
	if (flags & O_NONBLOCK)
		goto out;
	skb = wait_for_connect(sk);
	if (skb)
		goto got_new_connect;
	error = ERESTARTSYS;
	goto out;
}


/*
 *	This will initiate an outgoing connection.
 */

static int tcp_connect(struct sock *sk, struct sockaddr_in *usin, int addr_len)
{
	struct sk_buff *buff;
	struct device *dev=NULL;
	unsigned char *ptr;
	int tmp;
	int atype;
	struct tcphdr *t1;
	struct rtable *rt;

	if (sk->state != TCP_CLOSE)
		return(-EISCONN);

	/*
	 *	Don't allow a double connect.
	 */

	if(sk->daddr)
		return -EINVAL;

	if (addr_len < 8)
		return(-EINVAL);

	if (usin->sin_family && usin->sin_family != AF_INET)
		return(-EAFNOSUPPORT);

  	/*
  	 *	connect() to INADDR_ANY means loopback (BSD'ism).
  	 */

  	if(usin->sin_addr.s_addr==INADDR_ANY)
		usin->sin_addr.s_addr=ip_my_addr();

	/*
	 *	Don't want a TCP connection going to a broadcast address
	 */

	if ((atype=ip_chk_addr(usin->sin_addr.s_addr)) == IS_BROADCAST || atype==IS_MULTICAST)
		return -ENETUNREACH;

	lock_sock(sk);
	sk->daddr = usin->sin_addr.s_addr;
	sk->write_seq = tcp_init_seq();
	sk->window_seq = sk->write_seq;
	sk->rcv_ack_seq = sk->write_seq -1;
	sk->rcv_ack_cnt = 1;
	sk->err = 0;
	sk->dummy_th.dest = usin->sin_port;
	release_sock(sk);

	buff = sock_wmalloc(sk,MAX_SYN_SIZE,0, GFP_KERNEL);
	if (buff == NULL)
	{
		return(-ENOMEM);
	}
	lock_sock(sk);
	buff->sk = sk;
	buff->free = 0;
	buff->localroute = sk->localroute;


	/*
	 *	Put in the IP header and routing stuff.
	 */

	tmp = sk->prot->build_header(buff, sk->saddr, sk->daddr, &dev,
		IPPROTO_TCP, sk->opt, MAX_SYN_SIZE,sk->ip_tos,sk->ip_ttl,&sk->ip_route_cache);
	if (tmp < 0)
	{
		sock_wfree(sk, buff);
		release_sock(sk);
		return(-ENETUNREACH);
	}
	if ((rt = sk->ip_route_cache) != NULL && !sk->saddr)
		sk->saddr = rt->rt_src;
	sk->rcv_saddr = sk->saddr;

	t1 = (struct tcphdr *) skb_put(buff,sizeof(struct tcphdr));

	memcpy(t1,(void *)&(sk->dummy_th), sizeof(*t1));
	buff->seq = sk->write_seq++;
	t1->seq = htonl(buff->seq);
	sk->sent_seq = sk->write_seq;
	buff->end_seq = sk->write_seq;
	t1->ack = 0;
	t1->window = 2;
	t1->syn = 1;
	t1->doff = 6;
	/* use 512 or whatever user asked for */

	if(rt!=NULL && (rt->rt_flags&RTF_WINDOW))
		sk->window_clamp=rt->rt_window;
	else
		sk->window_clamp=0;

	if (sk->user_mss)
		sk->mtu = sk->user_mss;
	else if (rt)
		sk->mtu = rt->rt_mtu - sizeof(struct iphdr) - sizeof(struct tcphdr);
	else
		sk->mtu = 576 - sizeof(struct iphdr) - sizeof(struct tcphdr);

	/*
	 *	but not bigger than device MTU
	 */

	if(sk->mtu <32)
		sk->mtu = 32;	/* Sanity limit */

	sk->mtu = min(sk->mtu, dev->mtu - sizeof(struct iphdr) - sizeof(struct tcphdr));

#ifdef CONFIG_SKIP

	/*
	 *	SKIP devices set their MTU to 65535. This is so they can take packets
	 *	unfragmented to security process then fragment. They could lie to the
	 *	TCP layer about a suitable MTU, but it's easier to let skip sort it out
	 *	simply because the final package we want unfragmented is going to be
	 *
	 *	[IPHDR][IPSP][Security data][Modified TCP data][Security data]
	 */

	if(skip_pick_mtu!=NULL)		/* If SKIP is loaded.. */
		sk->mtu=skip_pick_mtu(sk->mtu,dev);
#endif

	/*
	 *	Put in the TCP options to say MTU.
	 */

	ptr = skb_put(buff,4);
	ptr[0] = 2;
	ptr[1] = 4;
	ptr[2] = (sk->mtu) >> 8;
	ptr[3] = (sk->mtu) & 0xff;
	buff->csum = csum_partial(ptr, 4, 0);
	tcp_send_check(t1, sk->saddr, sk->daddr,
		  sizeof(struct tcphdr) + 4, buff);

	/*
	 *	This must go first otherwise a really quick response will get reset.
	 */

	tcp_cache_zap();
	tcp_set_state(sk,TCP_SYN_SENT);
	if(rt&&rt->rt_flags&RTF_IRTT)
		sk->rto = rt->rt_irtt;
	else
		sk->rto = TCP_TIMEOUT_INIT;
	sk->delack_timer.function = tcp_delack_timer;
	sk->delack_timer.data = (unsigned long) sk;
	sk->retransmit_timer.function = tcp_retransmit_timer;
	sk->retransmit_timer.data = (unsigned long)sk;
	sk->retransmits = 0;
	sk->prot->queue_xmit(sk, dev, buff, 0);
	tcp_reset_xmit_timer(sk, TIME_WRITE, sk->rto);
	tcp_statistics.TcpActiveOpens++;
	tcp_statistics.TcpOutSegs++;

	release_sock(sk);
	return(0);
}

/*
 *	Socket option code for TCP.
 */

int tcp_setsockopt(struct sock *sk, int level, int optname, char *optval, int optlen)
{
	int val,err;

	if(level!=SOL_TCP)
		return ip_setsockopt(sk,level,optname,optval,optlen);

  	if (optval == NULL)
  		return(-EINVAL);

  	err=verify_area(VERIFY_READ, optval, sizeof(int));
  	if(err)
  		return err;

  	val = get_user((int *)optval);

	switch(optname)
	{
		case TCP_MAXSEG:
/*
 * values greater than interface MTU won't take effect.  however at
 * the point when this call is done we typically don't yet know
 * which interface is going to be used
 */
	  		if(val<1||val>MAX_WINDOW)
				return -EINVAL;
			sk->user_mss=val;
			return 0;
		case TCP_NODELAY:
			sk->nonagle=(val==0)?0:1;
			return 0;
		default:
			return(-ENOPROTOOPT);
	}
}

int tcp_getsockopt(struct sock *sk, int level, int optname, char *optval, int *optlen)
{
	int val,err;

	if(level!=SOL_TCP)
		return ip_getsockopt(sk,level,optname,optval,optlen);

	switch(optname)
	{
		case TCP_MAXSEG:
			val=sk->user_mss;
			break;
		case TCP_NODELAY:
			val=sk->nonagle;
			break;
		default:
			return(-ENOPROTOOPT);
	}
	err=verify_area(VERIFY_WRITE, optlen, sizeof(int));
	if(err)
  		return err;
  	put_user(sizeof(int),(int *) optlen);

  	err=verify_area(VERIFY_WRITE, optval, sizeof(int));
  	if(err)
  		return err;
  	put_user(val,(int *)optval);

  	return(0);
}


struct proto tcp_prot = {
	tcp_close,
	ip_build_header,
	tcp_connect,
	tcp_accept,
	ip_queue_xmit,
	tcp_retransmit,
	tcp_write_wakeup,
	tcp_read_wakeup,
	tcp_rcv,
	tcp_select,
	tcp_ioctl,
	NULL,
	tcp_shutdown,
	tcp_setsockopt,
	tcp_getsockopt,
	tcp_sendmsg,
	tcp_recvmsg,
	NULL,		/* No special bind() */
	128,
	0,
	"TCP",
	0, 0,
	{NULL,}
};
