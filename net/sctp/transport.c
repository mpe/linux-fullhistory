/* SCTP kernel reference Implementation
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2001 International Business Machines Corp.
 * Copyright (c) 2001 Intel Corp.
 * Copyright (c) 2001 La Monte H.P. Yarroll
 *
 * This file is part of the SCTP kernel reference Implementation
 *
 * This module provides the abstraction for an SCTP tranport representing
 * a remote transport address.  For local transport addresses, we just use
 * sockaddr_storage_t.
 *
 * The SCTP reference implementation is free software;
 * you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * The SCTP reference implementation is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *                 ************************
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU CC; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Please send any bug reports or fixes you make to the
 * email address(es):
 *    lksctp developers <lksctp-developers@lists.sourceforge.net>
 *
 * Or submit a bug report through the following website:
 *    http://www.sf.net/projects/lksctp
 *
 * Written or modified by:
 *    La Monte H.P. Yarroll <piggy@acm.org>
 *    Karl Knutson          <karl@athena.chicago.il.us>
 *    Jon Grimm             <jgrimm@us.ibm.com>
 *    Xingang Guo           <xingang.guo@intel.com>
 *    Hui Huang             <hui.huang@nokia.com>
 *    Sridhar Samudrala	    <sri@us.ibm.com>
 *
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */

#include <linux/types.h>
#include <net/sctp/sctp.h>

/* 1st Level Abstractions.  */

/* Allocate and initialize a new transport.  */
sctp_transport_t *sctp_transport_new(const sockaddr_storage_t *addr, int priority)
{
        sctp_transport_t *transport;

        transport = t_new(sctp_transport_t, priority);
	if (!transport)
		goto fail;

	if (!sctp_transport_init(transport, addr, priority))
		goto fail_init;

	transport->malloced = 1;
	SCTP_DBG_OBJCNT_INC(transport);

	return transport;

fail_init:
	kfree(transport);

fail:
	return NULL;
}

/* Intialize a new transport from provided memory.  */
sctp_transport_t *sctp_transport_init(sctp_transport_t *peer,
				      const sockaddr_storage_t *addr,
				      int priority)
{
	sctp_protocol_t *proto = sctp_get_protocol();

	/* Copy in the address.  */
	peer->ipaddr = *addr;
	peer->af_specific = sctp_get_af_specific(addr);
	peer->asoc = NULL;
	peer->pmtu = peer->af_specific->get_dst_mtu(addr);

	/* From 6.3.1 RTO Calculation:
	 *
	 * C1) Until an RTT measurement has been made for a packet sent to the
	 * given destination transport address, set RTO to the protocol
	 * parameter 'RTO.Initial'.
	 */
	peer->rtt = 0;
	peer->rto = proto->rto_initial;
	peer->rttvar = 0;
	peer->srtt = 0;
	peer->rto_pending = 0;

	peer->last_time_heard = jiffies;
	peer->last_time_used = jiffies;
	peer->last_time_ecne_reduced = jiffies;

	peer->state.active = 1;
	peer->state.hb_allowed = 0;

	/* Initialize the default path max_retrans.  */
	peer->max_retrans = proto->max_retrans_path;
	peer->error_threshold = 0;
	peer->error_count = 0;

	peer->debug_name = "unnamedtransport";

	INIT_LIST_HEAD(&peer->transmitted);
	INIT_LIST_HEAD(&peer->send_ready);
	INIT_LIST_HEAD(&peer->transports);

	/* Set up the retransmission timer.  */
	init_timer(&peer->T3_rtx_timer);
	peer->T3_rtx_timer.function = sctp_generate_t3_rtx_event;
	peer->T3_rtx_timer.data = (unsigned long)peer;

	/* Set up the heartbeat timer. */
	init_timer(&peer->hb_timer);
	peer->hb_interval = SCTP_DEFAULT_TIMEOUT_HEARTBEAT;
	peer->hb_timer.function = sctp_generate_heartbeat_event;
	peer->hb_timer.data = (unsigned long)peer;

	atomic_set(&peer->refcnt, 1);
	peer->dead = 0;

	peer->malloced = 0;
	return peer;
}

/* This transport is no longer needed.  Free up if possible, or
 * delay until it last reference count.
 */
void sctp_transport_free(sctp_transport_t *transport)
{
	transport->dead = 1;

	/* Try to delete the heartbeat timer.  */
	if (del_timer(&transport->hb_timer))
		sctp_transport_put(transport);

	sctp_transport_put(transport);
}

/* Destroy the transport data structure.
 * Assumes there are no more users of this structure.
 */
void sctp_transport_destroy(sctp_transport_t *transport)
{
	SCTP_ASSERT(transport->dead, "Transport is not dead", return);

	if (transport->asoc)
		sctp_association_put(transport->asoc);

	kfree(transport);
	SCTP_DBG_OBJCNT_DEC(transport);
}

/* Start T3_rtx timer if it is not already running and update the heartbeat
 * timer.  This routine is called everytime a DATA chunk is sent.
 */
void sctp_transport_reset_timers(sctp_transport_t *transport)
{
	/* RFC 2960 6.3.2 Retransmission Timer Rules
	 *
	 * R1) Every time a DATA chunk is sent to any address(including a
	 * retransmission), if the T3-rtx timer of that address is not running
	 * start it running so that it will expire after the RTO of that
	 * address.
	 */
	if (!timer_pending(&transport->T3_rtx_timer)) {
		if (!mod_timer(&transport->T3_rtx_timer,
			       jiffies + transport->rto))
			sctp_transport_hold(transport);
	}

	/* When a data chunk is sent, reset the heartbeat interval.  */
	if (!mod_timer(&transport->hb_timer,
		       transport->hb_interval + transport->rto + jiffies))
		sctp_transport_hold(transport);
}

/* This transport has been assigned to an association.
 * Initialize fields from the association or from the sock itself.
 * Register the reference count in the association.
 */
void sctp_transport_set_owner(sctp_transport_t *transport,
			      sctp_association_t *asoc)
{
	transport->asoc = asoc;
	sctp_association_hold(asoc);
}

/* Hold a reference to a transport.  */
void sctp_transport_hold(sctp_transport_t *transport)
{
	atomic_inc(&transport->refcnt);
}

/* Release a reference to a transport and clean up
 * if there are no more references.
 */
void sctp_transport_put(sctp_transport_t *transport)
{
	if (atomic_dec_and_test(&transport->refcnt))
		sctp_transport_destroy(transport);
}

/* Update transport's RTO based on the newly calculated RTT. */
void sctp_transport_update_rto(sctp_transport_t *tp, __u32 rtt)
{
	sctp_protocol_t *proto = sctp_get_protocol();

	/* Check for valid transport.  */
	SCTP_ASSERT(tp, "NULL transport", return);

	/* We should not be doing any RTO updates unless rto_pending is set.  */
	SCTP_ASSERT(tp->rto_pending, "rto_pending not set", return);

	if (tp->rttvar || tp->srtt) {
		/* 6.3.1 C3) When a new RTT measurement R' is made, set
		 * RTTVAR <- (1 - RTO.Beta) * RTTVAR + RTO.Beta * |SRTT - R'|
		 * SRTT <- (1 - RTO.Alpha) * SRTT + RTO.Alpha * R'
		 */

		/* Note:  The above algorithm has been rewritten to
		 * express rto_beta and rto_alpha as inverse powers
		 * of two.
		 * For example, assuming the default value of RTO.Alpha of
		 * 1/8, rto_alpha would be expressed as 3.
		 */
		tp->rttvar = tp->rttvar - (tp->rttvar >> proto->rto_beta)
			+ ((abs(tp->srtt - rtt)) >> proto->rto_beta);
		tp->srtt = tp->srtt - (tp->srtt >> proto->rto_alpha)
			+ (rtt >> proto->rto_alpha);
	} else {
		/* 6.3.1 C2) When the first RTT measurement R is made, set
		 * SRTT <- R, RTTVAR <- R/2.
		 */
		tp->srtt = rtt;
		tp->rttvar = rtt >> 1;
	}

	/* 6.3.1 G1) Whenever RTTVAR is computed, if RTTVAR = 0, then
	 * adjust RTTVAR <- G, where G is the CLOCK GRANULARITY.
	 */
	if (tp->rttvar == 0)
		tp->rttvar = SCTP_CLOCK_GRANULARITY;

	/* 6.3.1 C3) After the computation, update RTO <- SRTT + 4 * RTTVAR.  */
	tp->rto = tp->srtt + (tp->rttvar << 2);

	/* 6.3.1 C6) Whenever RTO is computed, if it is less than RTO.Min
	 * seconds then it is rounded up to RTO.Min seconds.
	 */
	if (tp->rto < tp->asoc->rto_min)
		tp->rto = tp->asoc->rto_min;

	/* 6.3.1 C7) A maximum value may be placed on RTO provided it is
	 * at least RTO.max seconds.
	 */
	if (tp->rto > tp->asoc->rto_max)
		tp->rto = tp->asoc->rto_max;

	tp->rtt = rtt;

	/* Reset rto_pending so that a new RTT measurement is started when a
	 * new data chunk is sent.
	 */
	tp->rto_pending = 0;

	SCTP_DEBUG_PRINTK("%s: transport: %p, rtt: %d, srtt: %d "
			  "rttvar: %d, rto: %d\n", __FUNCTION__,
			  tp, rtt, tp->srtt, tp->rttvar, tp->rto);
}

/* This routine updates the transport's cwnd and partial_bytes_acked
 * parameters based on the bytes acked in the received SACK.
 */
void sctp_transport_raise_cwnd(sctp_transport_t *transport, __u32 sack_ctsn,
			       __u32 bytes_acked)
{
	__u32 cwnd, ssthresh, flight_size, pba, pmtu;

	cwnd = transport->cwnd;
	flight_size = transport->flight_size;

	/* The appropriate cwnd increase algorithm is performed if, and only
	 * if the cumulative TSN has advanced and the congestion window is
	 * being fully utilized.
	 */
	if ((transport->asoc->ctsn_ack_point >= sack_ctsn) ||
	    (flight_size < cwnd))
		return;

	ssthresh = transport->ssthresh;
	pba = transport->partial_bytes_acked;
	pmtu = transport->asoc->pmtu;

	if (cwnd <= ssthresh) {
		/* RFC 2960 7.2.1, sctpimpguide-05 2.14.2 When cwnd is less
		 * than or equal to ssthresh an SCTP endpoint MUST use the
		 * slow start algorithm to increase cwnd only if the current
		 * congestion window is being fully utilized and an incoming
		 * SACK advances the Cumulative TSN Ack Point. Only when these
		 * two conditions are met can the cwnd be increased otherwise
		 * the cwnd MUST not be increased. If these conditions are met
		 * then cwnd MUST be increased by at most the lesser of
		 * 1) the total size of the previously outstanding DATA chunk(s)
		 * acknowledged, and 2) the destination's path MTU.
		 */
		if (bytes_acked > pmtu)
			cwnd += pmtu;
		else
			cwnd += bytes_acked;
		SCTP_DEBUG_PRINTK("%s: SLOW START: transport: %p, "
				  "bytes_acked: %d, cwnd: %d, ssthresh: %d, "
				  "flight_size: %d, pba: %d\n",
				  __FUNCTION__,
				  transport, bytes_acked, cwnd,
				  ssthresh, flight_size, pba);
	} else {
		/* RFC 2960 7.2.2 Whenever cwnd is greater than ssthresh, upon
		 * each SACK arrival that advances the Cumulative TSN Ack Point,
		 * increase partial_bytes_acked by the total number of bytes of
		 * all new chunks acknowledged in that SACK including chunks
		 * acknowledged by the new Cumulative TSN Ack and by Gap Ack
		 * Blocks.
		 *
		 * When partial_bytes_acked is equal to or greater than cwnd and
		 * before the arrival of the SACK the sender had cwnd or more
		 * bytes of data outstanding (i.e., before arrival of the SACK,
		 * flightsize was greater than or equal to cwnd), increase cwnd
		 * by MTU, and reset partial_bytes_acked to
		 * (partial_bytes_acked - cwnd).
		 */
		pba += bytes_acked;
		if (pba >= cwnd) {
			cwnd += pmtu;
			pba = ((cwnd < pba) ? (pba - cwnd) : 0);
		}
		SCTP_DEBUG_PRINTK("%s: CONGESTION AVOIDANCE: "
				  "transport: %p, bytes_acked: %d, cwnd: %d, "
				  "ssthresh: %d, flight_size: %d, pba: %d\n",
				  __FUNCTION__,
				  transport, bytes_acked, cwnd,
				  ssthresh, flight_size, pba);
	}

	transport->cwnd = cwnd;
	transport->partial_bytes_acked = pba;
}

/* This routine is used to lower the transport's cwnd when congestion is
 * detected.
 */
void sctp_transport_lower_cwnd(sctp_transport_t *transport,
			       sctp_lower_cwnd_t reason)
{
	switch (reason) {
	case SCTP_LOWER_CWND_T3_RTX:
		/* RFC 2960 Section 7.2.3, sctpimpguide-05 Section 2.9.2
		 * When the T3-rtx timer expires on an address, SCTP should
		 * perform slow start by:
		 *      ssthresh = max(cwnd/2, 2*MTU)
		 *      cwnd = 1*MTU
		 *      partial_bytes_acked = 0
		 */
		transport->ssthresh = max(transport->cwnd/2,
					  2*transport->asoc->pmtu);
		transport->cwnd = transport->asoc->pmtu;
		break;

	case SCTP_LOWER_CWND_FAST_RTX:
		/* RFC 2960 7.2.4 Adjust the ssthresh and cwnd of the
		 * destination address(es) to which the missing DATA chunks
		 * were last sent, according to the formula described in
		 * Section 7.2.3.
	 	 *
	 	 * RFC 2960 7.2.3, sctpimpguide-05 2.9.2 Upon detection of
		 * packet losses from SACK (see Section 7.2.4), An endpoint
		 * should do the following:
		 *      ssthresh = max(cwnd/2, 2*MTU)
		 *      cwnd = ssthresh
		 *      partial_bytes_acked = 0
		 */
		transport->ssthresh = max(transport->cwnd/2,
					  2*transport->asoc->pmtu);
		transport->cwnd = transport->ssthresh;
		break;

	case SCTP_LOWER_CWND_ECNE:
		/* RFC 2481 Section 6.1.2.
		 * If the sender receives an ECN-Echo ACK packet
		 * then the sender knows that congestion was encountered in the
		 * network on the path from the sender to the receiver. The
		 * indication of congestion should be treated just as a
		 * congestion loss in non-ECN Capable TCP. That is, the TCP
		 * source halves the congestion window "cwnd" and reduces the
		 * slow start threshold "ssthresh".
		 * A critical condition is that TCP does not react to
		 * congestion indications more than once every window of
		 * data (or more loosely more than once every round-trip time).
		 */
		if ((jiffies - transport->last_time_ecne_reduced) >
		    transport->rtt) {
			transport->ssthresh = max(transport->cwnd/2,
					  	  2*transport->asoc->pmtu);
			transport->cwnd = transport->ssthresh;
			transport->last_time_ecne_reduced = jiffies;
		}
		break;

	case SCTP_LOWER_CWND_INACTIVE:
		/* RFC 2960 Section 7.2.1, sctpimpguide-05 Section 2.14.2
		 * When the association does not transmit data on a given
		 * transport address within an RTO, the cwnd of the transport
		 * address should be adjusted to 2*MTU.
		 * NOTE: Although the draft recommends that this check needs
		 * to be done every RTO interval, we do it every hearbeat
		 * interval.
		 */
		if ((jiffies - transport->last_time_used) > transport->rto)
			transport->cwnd = 2*transport->asoc->pmtu;
		break;
	};

	transport->partial_bytes_acked = 0;
	SCTP_DEBUG_PRINTK("%s: transport: %p reason: %d cwnd: "
			  "%d ssthresh: %d\n", __FUNCTION__,
			  transport, reason,
			  transport->cwnd, transport->ssthresh);
}
