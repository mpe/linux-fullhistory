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
 *					wakes people on errors. poll
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
 *		Michael Pall	:	Handle poll() after URG properly in
 *					all cases.
 *		Michael Pall	:	Undo the last fix in tcp_read_urg()
 *					(multi URG PUSH broke rlogin).
 *		Michael Pall	:	Fix the multi URG PUSH problem in
 *					tcp_readable(), poll() after URG
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
 *		Alan Cox	:	Small whoops in polling before an
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
 *		Matt Day	:	poll()->select() match BSD precisely on error
 *		Alan Cox	:	New buffers
 *		Marc Tamsky	:	Various sk->prot->retransmits and
 *					sk->retransmits misupdating fixed.
 *					Fixed tcp_write_timeout: stuck close,
 *					and TCP syn retries gets used now.
 *		Mark Yarvis	:	In tcp_read_wakeup(), don't send an
 *					ack if stat is TCP_CLOSED.
 *		Alan Cox	:	Look up device on a retransmit - routes may
 *					change. Doesn't yet cope with MSS shrink right
 *					but its a start!
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
 *		RFC1323 - PAWS and window scaling.[Required for IPv6]
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

#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/poll.h>

#include <net/icmp.h>
#include <net/tcp.h>

#include <asm/uaccess.h>

unsigned long seq_offset;
struct tcp_mib	tcp_statistics;



/*
 *	Find someone to 'accept'. Must be called with
 *	the socket locked or with interrupts disabled
 */

static struct open_request *tcp_find_established(struct tcp_opt *tp)
{
	struct open_request *req;

	req = tp->syn_wait_queue;

	if (!req)
		return NULL;
	
	do {
		if (req->sk && 
		    (req->sk->state == TCP_ESTABLISHED ||
		     req->sk->state >= TCP_FIN_WAIT1))
		{
			return req;
		}

		req = req->dl_next;

	} while (req != tp->syn_wait_queue);
	
	return NULL;
}

/*
 *	This routine closes sockets which have been at least partially
 *	opened, but not yet accepted. Currently it is only called by
 *	tcp_close, and timeout mirrors the value there.
 */

static void tcp_close_pending (struct sock *sk)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	struct open_request *req;

	req = tp->syn_wait_queue;

	if (!req)
		return;
	
	do {
		struct open_request *iter;
		
		if (req->sk)
			tcp_close(req->sk, 0);

		iter = req;
		req = req->dl_next;
		
		(*iter->class->destructor)(iter);
		tcp_dec_slow_timer(TCP_SLT_SYNACK);
		sk->ack_backlog--;
		kfree(iter);

	} while (req != tp->syn_wait_queue);

	tp->syn_wait_queue = NULL;
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
 *	Walk down the receive queue counting readable data until we hit the 
 *	end or we find a gap in the received data queue (ie a frame missing 
 *	that needs sending to us). 
 */

static int tcp_readable(struct sock *sk)
{
	unsigned long counted;
	unsigned long amount;
	struct sk_buff *skb;
	int sum;
	unsigned long flags;

	SOCK_DEBUG(sk, "tcp_readable: %p - ",sk);
	save_flags(flags);
	cli();
	if (sk == NULL || (skb = skb_peek(&sk->receive_queue)) == NULL)
	{
		restore_flags(flags);
		SOCK_DEBUG(sk, "empty\n");
	  	return(0);
	}

	counted = sk->copied_seq;	/* Where we are at the moment */
	amount = 0;

	/*
	 *	Do until a push or until we are out of data.
	 */

	do
	{
		/* Found a hole so stops here */
		if (before(counted, skb->seq))	 	
			break;
		/* 
		 * Length - header but start from where we are up to 
		 * avoid overlaps 
		 */
		sum = skb->len - (counted - skb->seq);	
		if (skb->h.th->syn)
			sum++;
		if (sum > 0)
		{	
			/* Add it up, move on */
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
		 * This means that poll() finally works now with urg data
		 * in the queue.  Note that rlogin was never affected
		 * because it doesn't use poll(); it uses two processes
		 * and a blocking read().  And the queue scan in tcp_read()
		 * was correct.  Mike <pall@rz.uni-karlsruhe.de>
		 */

		/* don't count urg data */
		if (skb->h.th->urg)
			amount--;
#if 0
		if (amount && skb->h.th->psh) break;
#endif
		skb = skb->next;
	}
	while(skb != (struct sk_buff *)&sk->receive_queue);

	restore_flags(flags);
	SOCK_DEBUG(sk, "got %lu bytes.\n",amount);
	return(amount);
}

/*
 * LISTEN is a special case for poll..
 */
static unsigned int tcp_listen_poll(struct sock *sk, poll_table *wait)
{
	struct open_request *req;

	lock_sock(sk);
	req = tcp_find_established(&sk->tp_pinfo.af_tcp);
	release_sock(sk);
	if (req)
		return POLLIN | POLLRDNORM;
	return 0;
}

/*
 *	Wait for a TCP event.
 *
 *	Note that we don't need to lock the socket, as the upper poll layers
 *	take care of normal races (between the test and the event) and we don't
 *	go look at any of the socket buffers directly.
 */
unsigned int tcp_poll(struct socket *sock, poll_table *wait)
{
	unsigned int mask;
	struct sock *sk = sock->sk;
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

	poll_wait(sk->sleep, wait);
	if (sk->state == TCP_LISTEN)
		return tcp_listen_poll(sk, wait);

	mask = 0;
	if (sk->err)
		mask = POLLERR;
	/* connected ? */
	if (sk->state != TCP_SYN_SENT && sk->state != TCP_SYN_RECV) {

		if (sk->shutdown & RCV_SHUTDOWN)
			mask |= POLLHUP;
			
		if ((tp->rcv_nxt != sk->copied_seq) &&
		    (sk->urg_seq != sk->copied_seq ||
		     tp->rcv_nxt != sk->copied_seq+1 ||
		     sk->urginline || !sk->urg_data))
			mask |= POLLIN | POLLRDNORM;

		if (!(sk->shutdown & SEND_SHUTDOWN) &&
		     (sock_wspace(sk) >= sk->mtu+128+sk->prot->max_header))
			mask |= POLLOUT | POLLWRNORM;

		if (sk->urg_data)
			mask |= POLLPRI;
	}
	return mask;
}

int tcp_ioctl(struct sock *sk, int cmd, unsigned long arg)
{
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
			return put_user(amount, (int *)arg);
		}
		case SIOCATMARK:
		{
			int answ = sk->urg_data && sk->urg_seq == sk->copied_seq;
			return put_user(answ,(int *) arg);
		}
		case TIOCOUTQ:
		{
			unsigned long amount;

			if (sk->state == TCP_LISTEN) return(-EINVAL);
			amount = sock_wspace(sk);
			return put_user(amount, (int *)arg);
		}
		default:
			return(-EINVAL);
	}
}


/* 
 *	This routine builds a generic TCP header. 
 */
 
extern __inline int tcp_build_header(struct tcphdr *th, struct sock *sk, int push)
{
	struct tcp_opt *tp=&(sk->tp_pinfo.af_tcp);
	memcpy(th,(void *) &(sk->dummy_th), sizeof(*th));
	th->seq = htonl(sk->write_seq);

	th->psh =(push == 0) ? 1 : 0;

	sk->bytes_rcv = 0;
	sk->ack_timed = 0;
	th->ack_seq = htonl(tp->rcv_nxt);
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

static inline int tcp_memory_free(struct sock *sk)
{
	return sk->wmem_alloc < sk->sndbuf;
}

/*
 *	Wait for more memory for a socket
 */
static void wait_for_tcp_memory(struct sock * sk)
{
	release_sock(sk);
	if (!tcp_memory_free(sk)) {
		struct wait_queue wait = { current, NULL };

		sk->socket->flags &= ~SO_NOSPACE;
		add_wait_queue(sk->sleep, &wait);
		for (;;) {
			if (current->signal & ~current->blocked)
				break;
			current->state = TASK_INTERRUPTIBLE;
			if (tcp_memory_free(sk))
				break;
			if (sk->shutdown & SEND_SHUTDOWN)
				break;
			if (sk->err)
				break;
			schedule();
		}
		current->state = TASK_RUNNING;
		remove_wait_queue(sk->sleep, &wait);
	}
	lock_sock(sk);
}


static int tcp_append_tail(struct sock *sk, struct sk_buff *skb, u8 *from,
			   int tcp_size, int seglen)
{
	int fault;
	int copy;

	/* 
	 * Add more stuff to the end 
	 * of the skb
	 */

	copy = min(sk->mss - tcp_size, skb_tailroom(skb));
	copy = min(copy, seglen);
	
	tcp_size += copy;
	
	fault = copy_from_user(skb->tail, from, copy);
	
	if (fault)
	{
		return -1;
	}

	skb_put(skb, copy);
	skb->csum = csum_partial(skb->tail - tcp_size, tcp_size, 0);

	sk->write_seq += copy;
	skb->end_seq += copy;

	return copy;
}

/*
 *	This routine copies from a user buffer into a socket,
 *	and starts the transmit system.
 */

int tcp_do_sendmsg(struct sock *sk, int iovlen, struct iovec *iov,
		   int len, int flags)
{
	int err = 0;
	int copied  = 0;
	struct tcp_opt *tp=&(sk->tp_pinfo.af_tcp);

	/* 
	 *	Wait for a connection to finish.
	 */
	while (sk->state != TCP_ESTABLISHED && sk->state != TCP_CLOSE_WAIT)
	{
		
		if (copied)
			return copied;
		
		if (sk->err) 
			return sock_error(sk);
		
		if (sk->state != TCP_SYN_SENT && sk->state != TCP_SYN_RECV)
		{
			if (sk->keepopen)
				send_sig(SIGPIPE, current, 0);
			return -EPIPE;
		}
		
		if (flags&MSG_DONTWAIT)
			return -EAGAIN;
		
		if (current->signal & ~current->blocked)
			return -ERESTARTSYS;
		
		wait_for_tcp_connect(sk);
	}
	
	
	/*
	 *	Ok commence sending
	 */
	
	while(--iovlen >= 0)
	{
		int seglen=iov->iov_len;
		unsigned char * from=iov->iov_base;
		u32 actual_win;

		iov++;

		while(seglen > 0) 
		{
			int copy;
			int tmp;
			struct sk_buff *skb;

			if (err)
				return (err);
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
				send_sig(SIGPIPE,current,0);
				return -EPIPE;
			}
	
			/* 
			 *	Now we need to check if we have a half built packet. 
			 */

			/* if we have queued packets */
			if (tp->send_head && !(flags & MSG_OOB) ) 
			{
				int tcp_size;

				/* Tail */
				
				skb = sk->write_queue.prev;
				tcp_size = skb->tail - 
					(unsigned char *)(skb->h.th + 1);
					
				/*
				 * This window_seq test is somewhat dangerous
				 * If the remote does SWS avoidance we should
				 * queue the best we can if not we should in 
				 * fact send multiple packets...
				 * a method for detecting this would be most
				 * welcome
				 */

				if (skb->end > skb->tail &&
				    sk->mss - tcp_size > 0 &&
				    skb->end_seq < tp->snd_una + tp->snd_wnd) 
				{
					int tcopy;
					
					tcopy = tcp_append_tail(sk, skb, from,
							       tcp_size,
							       seglen);
					if (tcopy == -1)
					{
						return -EFAULT;
					}
					
					from += tcopy;
					copied += tcopy;
					len -= tcopy;
					seglen -= tcopy;
					
					/*
					 *	FIXME: if we're nagling we
					 *	should send here.
					 */
					continue;
				}
			}


		/*
		 *   We also need to worry about the window.
	 	 *   If window < 1/2 the maximum window we've seen from this
	 	 *   host, don't use it.  This is sender side
	 	 *   silly window prevention, as specified in RFC1122.
	 	 *   (Note that this is different than earlier versions of
		 *   SWS prevention, e.g. RFC813.).  What we actually do is
		 *   use the whole MSS.  Since the results in the right
		 *   edge of the packet being outside the window, it will
		 *   be queued for later rather than sent.
		 */

			copy = min(seglen, sk->mss);

			actual_win = tp->snd_wnd - (tp->snd_nxt - tp->snd_una);

			if (copy > actual_win &&
			    (((long) actual_win) >= (sk->max_window >> 1))
			    && actual_win)
			{
				copy = actual_win;
			}

			if (copy <= 0)
			{
				printk(KERN_DEBUG "sendmsg: copy < 0\n");
				return -EIO;
			}

			/*
			 *  If sk->packets_out > 0 segment will be nagled
			 *  else we kick it right away
			 */

			tmp = MAX_HEADER + sk->prot->max_header + 
				sizeof(struct sk_buff) + 15;
			if (copy < min(sk->mss, sk->max_window >> 1) && 
			    !(flags & MSG_OOB) && sk->packets_out)
			{
				tmp += min(sk->mss, sk->max_window);
			}
			else
			{
				tmp += copy;
			}

			skb = sock_wmalloc(sk, tmp, 0, GFP_KERNEL);
	
			/*
			 *	If we didn't get any memory, we need to sleep. 
			 */
	
			if (skb == NULL) 
			{
				sk->socket->flags |= SO_NOSPACE;
				if (flags&MSG_DONTWAIT)
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

			/*
			 * FIXME: we need to optimize this.
			 * Perhaps some hints here would be good.
			 */

			tmp = tp->af_specific->build_net_header(sk, skb);

			if (tmp < 0)
			{
				kfree_skb(skb, FREE_WRITE);
				if (copied)
					return(copied);
				return(tmp);
			}

			skb->h.th =(struct tcphdr *) 
			  skb_put(skb,sizeof(struct tcphdr));

			seglen -= copy;
			tmp = tcp_build_header(skb->h.th, sk, seglen || iovlen);

                        if (tmp < 0) 
                        {
                                kfree_skb(skb, FREE_WRITE);
                                if (copied) 
                                        return(copied);
                                return(tmp);
                        }

			if (flags & MSG_OOB)
			{
				skb->h.th->urg = 1;
				skb->h.th->urg_ptr = ntohs(copy);
			}

			skb->csum = csum_partial_copy_from_user(from,
					skb_put(skb, copy), copy, 0, &err);
		
			from += copy;
			copied += copy;
			len -= copy;
			sk->write_seq += copy;
		
			tcp_send_skb(sk, skb);
		}
	}

	sk->err = 0;

	if (err)
		return (err);

	return copied;
}


	

/*
 *	Send an ack if one is backlogged at this point. Ought to merge
 *	this with tcp_send_ack().
 *      This is called for delayed acks also.
 */
 
void tcp_read_wakeup(struct sock *sk)
{
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
			struct msghdr *msg, int len, int flags, 
			int *addr_len)
{
	int err=0; 
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

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
			
		if(len>0)
			err = memcpy_toiovec(msg->msg_iov, &c, 1);
		else
			msg->msg_flags|=MSG_TRUNC;
			
		if(msg->msg_name)
		{
			tp->af_specific->addr2sockaddr(sk, (struct sockaddr *)
						       msg->msg_name);       
		}
		if(addr_len)
			*addr_len = tp->af_specific->sockaddr_len;
		/* 
		 *	Read urgent data
		 */
		msg->msg_flags|=MSG_OOB;
		release_sock(sk);
		return err ? -EFAULT : 1;
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
	sk->delayed_acks++;

	__skb_unlink(skb, &sk->receive_queue);
	kfree_skb(skb, FREE_READ);
}


static void cleanup_rbuf(struct sock *sk)
{
	struct sk_buff *skb;

	/*
	 * NOTE! The socket must be locked, so that we don't get
	 * a messed-up receive queue.
	 */

	while ((skb=skb_peek(&sk->receive_queue)) != NULL) {
		if (!skb->used || skb->users>1)
			break;
		tcp_eat_skb(sk, skb);
	}
       
	SOCK_DEBUG(sk, "sk->rspace = %lu\n", sock_rspace(sk));
	
  	/*
  	 *  We send a ACK if the sender is blocked
  	 *  else let tcp_data deal with the acking policy.
  	 */
  
	if (sk->delayed_acks)
	{
		struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
		__u32 rcv_wnd;

		rcv_wnd = tp->rcv_wnd - (tp->rcv_nxt - tp->rcv_wup);

		if ((rcv_wnd < sk->mss) && (sock_rspace(sk) > rcv_wnd))
			tcp_read_wakeup(sk);
	}
}


/*
 *	This routine copies from a sock struct into the user buffer. 
 */
 
int tcp_recvmsg(struct sock *sk, struct msghdr *msg,
		int len, int nonblock, int flags, int *addr_len)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	struct wait_queue wait = { current, NULL };
	int copied = 0;
	u32 peek_seq;
	volatile u32 *seq;	/* So gcc doesn't overoptimise */
	unsigned long used;
	int err = 0; 
	int target = 1;		/* Read at least this may bytes */

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
		
	/*
	 *	Handle the POSIX bogosity MSG_WAITALL
	 */
	 
	if (flags & MSG_WAITALL)
		target=len;

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
		 * handling. FIXME: Need to check this doesnt impact 1003.1g
		 * and move it down to the bottom of the loop
		 */
		if (current->signal & ~current->blocked) {
			if (copied)
				break;
			copied = -ERESTARTSYS;
			if (nonblock)
				copied = -EAGAIN;
			break;
		}

		/*
		 *	Next get a buffer.
		 */

		current->state = TASK_INTERRUPTIBLE;

		skb = skb_peek(&sk->receive_queue);
		do
		{
			if (!skb)
				break;
			/* 
			 * now that we have two receive queues this 
			 * shouldn't happen
			 */
			if (before(*seq, skb->seq)) {
				printk("recvmsg bug: copied %X seq %X\n",
				       *seq, skb->seq);
				break;
			}
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
		while (skb != (struct sk_buff *)&sk->receive_queue);

		if (copied >= target)
			break;

		if (sk->err && !(flags&MSG_PEEK))
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

		atomic_inc(&skb->users);

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
		 *	This memcpy_toiovec can sleep. If it sleeps and we
		 *	do a second read it relies on the skb->users to avoid
		 *	a crash when cleanup_rbuf() gets called.
		 */

		err = memcpy_toiovec(msg->msg_iov, ((unsigned char *)skb->h.th) + skb->h.th->doff*4 + offset, used);
		
		if (err)
		{
			/*
			 *	exception. bailout!
			 */
			*seq -= err;
			atomic_dec(&skb->users);
			copied = -EFAULT;
			break;
		}

		copied += used;
		len -= used;

		/*
		 *	We now will not sleep again until we are finished
		 *	with skb. Sorry if you are doing the SMP port
		 *	but you'll just have to fix it neatly ;)
		 */

		atomic_dec(&skb->users);

		if (after(sk->copied_seq,sk->urg_seq))
			sk->urg_data = 0;
		if (used + offset < skb->len)
			continue;

		/*
		 *	Process the FIN. We may also need to handle PSH
		 *	here and make it break out of MSG_WAITALL
		 */

		if (skb->h.th->fin)
			goto found_fin_ok;
		if (flags & MSG_PEEK)
			continue;
		skb->used = 1;
		if (skb->users == 1)
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

	if(copied > 0 && msg->msg_name)
	{
		tp->af_specific->addr2sockaddr(sk, (struct sockaddr *)
					       msg->msg_name);       
	}
	if(addr_len)
		*addr_len= tp->af_specific->sockaddr_len;

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


void tcp_close(struct sock *sk, unsigned long timeout)
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
 *	Wait for an incoming connection, avoid race
 *	conditions. This must be called with the socket locked.
 */
static struct open_request * wait_for_connect(struct sock * sk)
{
	struct wait_queue wait = { current, NULL };
	struct open_request *req = NULL;

	add_wait_queue(sk->sleep, &wait);
	for (;;) {
		current->state = TASK_INTERRUPTIBLE;
		release_sock(sk);
		schedule();
		lock_sock(sk);
		req = tcp_find_established(&(sk->tp_pinfo.af_tcp));
		if (req)
			break;
		if (current->signal & ~current->blocked)
			break;
	}
	remove_wait_queue(sk->sleep, &wait);
	return req;
}


/*
 *	This will accept the next outstanding connection.
 *
 *	Be careful about race conditions here - this is subtle.
 */

struct sock *tcp_accept(struct sock *sk, int flags)
{
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;
	struct open_request *req;
	struct sock *newsk = NULL;
	int error;

  /*
   * We need to make sure that this socket is listening,
   * and that it has something pending.
   */

	error = EINVAL;
	if (sk->state != TCP_LISTEN)
		goto no_listen;

	lock_sock(sk);

	req = tcp_find_established(tp);
	if (req) {
got_new_connect:
		tcp_synq_unlink(tp, req);
		newsk = req->sk;
		kfree(req);
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
	req = wait_for_connect(sk);
	if (req)
		goto got_new_connect;
	error = ERESTARTSYS;
	goto out;
}


/*
 *	Socket option code for TCP. 
 */
  
int tcp_setsockopt(struct sock *sk, int level, int optname, char *optval, 
		   int optlen)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	int val;

	if (level != SOL_TCP)
	{
		return tp->af_specific->setsockopt(sk, level, optname, 
						   optval, optlen);
	}

  	if (optval == NULL)
  		return(-EINVAL);

  	if (get_user(val, (int *)optval))
		return -EFAULT;

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

int tcp_getsockopt(struct sock *sk, int level, int optname, char *optval, 
		   int *optlen)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	int val,err;

	if(level != SOL_TCP)
	{
		return tp->af_specific->getsockopt(sk, level, optname,
						   optval, optlen);
	}

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

  	err = put_user(sizeof(int),(int *) optlen);
	if (!err)
		err = put_user(val,(int *)optval);

  	return err;
}

void tcp_set_keepalive(struct sock *sk, int val)
{
	if (!sk->keepopen && val)
	{
		tcp_inc_slow_timer(TCP_SLT_KEEPALIVE);
	}
	else if (sk->keepopen && !val)
	{
		tcp_dec_slow_timer(TCP_SLT_KEEPALIVE);
	}
}
