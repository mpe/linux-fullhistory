/* SCTP kernel reference Implementation
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2001-2002 International Business Machines, Corp.
 * Copyright (c) 2002      Nokia Corp.
 *
 * This file is part of the SCTP kernel reference Implementation
 * 
 * This is part of the SCTP Linux Kernel Reference Implementation.
 *
 * These are the state functions for the state machine.
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
 *    Mathew Kotowsky       <kotowsky@sctp.org>
 *    Sridhar Samudrala     <samudrala@us.ibm.com>
 *    Jon Grimm             <jgrimm@us.ibm.com>
 *    Hui Huang 	    <hui.huang@nokia.com>
 *    Dajiang Zhang 	    <dajiang.zhang@nokia.com>
 *    Daisy Chang	    <daisyc@us.ibm.com>
 *
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/net.h>
#include <linux/inet.h>
#include <net/sock.h>
#include <net/inet_ecn.h>
#include <linux/skbuff.h>
#include <net/sctp/sctp.h>
#include <net/sctp/sm.h>
#include <net/sctp/structs.h>

/**********************************************************
 * These are the state functions for handling chunk events.
 **********************************************************/

/*
 * Process the final SHUTDOWN COMPLETE.
 *
 * Section: 4 (C) (diagram), 9.2
 * Upon reception of the SHUTDOWN COMPLETE chunk the endpoint will verify
 * that it is in SHUTDOWN-ACK-SENT state, if it is not the chunk should be
 * discarded. If the endpoint is in the SHUTDOWN-ACK-SENT state the endpoint
 * should stop the T2-shutdown timer and remove all knowledge of the
 * association (and thus the association enters the CLOSED state).
 *
 * Verification Tag: 8.5.1(C)
 * C) Rules for packet carrying SHUTDOWN COMPLETE:
 * ...
 * - The receiver of a SHUTDOWN COMPLETE shall accept the packet if the
 *   Verification Tag field of the packet matches its own tag OR it is
 *   set to its peer's tag and the T bit is set in the Chunk Flags.
 *   Otherwise, the receiver MUST silently discard the packet and take
 *   no further action. An endpoint MUST ignore the SHUTDOWN COMPLETE if
 *   it is not in the SHUTDOWN-ACK-SENT state.
 *
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_do_4_C(const sctp_endpoint_t *ep,
				  const sctp_association_t *asoc,
				  const sctp_subtype_t type,
				  void *arg,
				  sctp_cmd_seq_t *commands)
{
	sctp_chunk_t *chunk = arg;
	sctp_ulpevent_t *ev;

	/* RFC 2960 6.10 Bundling
	 *
	 * An endpoint MUST NOT bundle INIT, INIT ACK or
	 * SHUTDOWN COMPLETE with any other chunks.
	 */
	if (!chunk->singleton)
		return SCTP_DISPOSITION_VIOLATION;

	/* RFC 2960 8.5.1 Exceptions in Verification Tag Rules
	 *
	 * (C) The receiver of a SHUTDOWN COMPLETE shall accept the
	 * packet if the Verification Tag field of the packet
	 * matches its own tag OR it is set to its peer's tag and
	 * the T bit is set in the Chunk Flags.  Otherwise, the
	 * receiver MUST silently discard the packet and take no
	 * further action....
	 */
	if ((ntohl(chunk->sctp_hdr->vtag) != asoc->c.my_vtag) &&
	    !(sctp_test_T_bit(chunk) ||
	      (ntohl(chunk->sctp_hdr->vtag) != asoc->peer.i.init_tag)))
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);

	/* RFC 2960 10.2 SCTP-to-ULP
	 *
	 * H) SHUTDOWN COMPLETE notification
	 *
	 * When SCTP completes the shutdown procedures (section 9.2) this
	 * notification is passed to the upper layer.
	 */
	ev = sctp_ulpevent_make_assoc_change(asoc, 0, SCTP_SHUTDOWN_COMP,
					     0, 0, 0, GFP_ATOMIC);
	if (!ev)
		goto nomem;

	sctp_add_cmd_sf(commands, SCTP_CMD_EVENT_ULP, SCTP_ULPEVENT(ev));

	/* Upon reception of the SHUTDOWN COMPLETE chunk the endpoint
	 * will verify that it is in SHUTDOWN-ACK-SENT state, if it is
	 * not the chunk should be discarded. If the endpoint is in
	 * the SHUTDOWN-ACK-SENT state the endpoint should stop the
	 * T2-shutdown timer and remove all knowledge of the
	 * association (and thus the association enters the CLOSED
	 * state).
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_STOP,
			SCTP_TO(SCTP_EVENT_TIMEOUT_T2_SHUTDOWN));

	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_STATE,
			SCTP_STATE(SCTP_STATE_CLOSED));

	sctp_add_cmd_sf(commands, SCTP_CMD_DELETE_TCB, SCTP_NULL());

	return SCTP_DISPOSITION_DELETE_TCB;

nomem:
	return SCTP_DISPOSITION_NOMEM;
}

/*
 * Discard the whole packet.
 *
 * Section: 8.4 2)
 *
 * 2) If the OOTB packet contains an ABORT chunk, the receiver MUST
 *    silently discard the OOTB packet and take no further action.
 *    Otherwise,
 *
 * Verification Tag: No verification necessary
 *
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_pdiscard(const sctp_endpoint_t *ep,
				    const sctp_association_t *asoc,
				    const sctp_subtype_t type,
				    void *arg,
				    sctp_cmd_seq_t *commands)
{
	sctp_add_cmd_sf(commands, SCTP_CMD_DISCARD_PACKET, SCTP_NULL());
	return SCTP_DISPOSITION_CONSUME;
}

/*
 * Respond to a normal INIT chunk.
 * We are the side that is being asked for an association.
 *
 * Section: 5.1 Normal Establishment of an Association, B
 * B) "Z" shall respond immediately with an INIT ACK chunk.  The
 *    destination IP address of the INIT ACK MUST be set to the source
 *    IP address of the INIT to which this INIT ACK is responding.  In
 *    the response, besides filling in other parameters, "Z" must set the
 *    Verification Tag field to Tag_A, and also provide its own
 *    Verification Tag (Tag_Z) in the Initiate Tag field.
 *
 * Verification Tag: No checking.
 *
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_do_5_1B_init(const sctp_endpoint_t *ep,
					const sctp_association_t *asoc,
					const sctp_subtype_t type,
					void *arg,
					sctp_cmd_seq_t *commands)
{
	sctp_chunk_t *chunk = arg;
	sctp_chunk_t *repl;
	sctp_association_t *new_asoc;

	/* If the packet is an OOTB packet which is temporarily on the
	 * control endpoint, responding with an ABORT.
	 */
	if (ep == sctp_sk((sctp_get_ctl_sock()))->ep)
		return sctp_sf_ootb(ep, asoc, type, arg, commands);

	/* 6.10 Bundling
	 * An endpoint MUST NOT bundle INIT, INIT ACK or
	 * SHUTDOWN COMPLETE with any other chunks.
	 */
	if (!chunk->singleton)
		return SCTP_DISPOSITION_VIOLATION;

        /* Grab the INIT header.  */
	chunk->subh.init_hdr = (sctp_inithdr_t *)chunk->skb->data;

	/* Tag the variable length parameters.  */
	chunk->param_hdr.v =
		skb_pull(chunk->skb, sizeof(sctp_inithdr_t));

	new_asoc = sctp_make_temp_asoc(ep, chunk, GFP_ATOMIC);
	if (!new_asoc)
		goto nomem;

	/* FIXME: sctp_process_init can fail, but there is no
	 * status nor handling.
	 */
	sctp_process_init(new_asoc, chunk->chunk_hdr->type,
			  sctp_source(chunk),
			  (sctp_init_chunk_t *)chunk->chunk_hdr,
			  GFP_ATOMIC);

	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_ASOC, SCTP_ASOC(new_asoc));

	/* B) "Z" shall respond immediately with an INIT ACK chunk.  */
	repl = sctp_make_init_ack(new_asoc, chunk, GFP_ATOMIC);
	if (!repl)
		goto nomem_ack;

	sctp_add_cmd_sf(commands, SCTP_CMD_REPLY, SCTP_CHUNK(repl));

	/*
         * Note: After sending out INIT ACK with the State Cookie parameter,
	 * "Z" MUST NOT allocate any resources, nor keep any states for the
	 * new association.  Otherwise, "Z" will be vulnerable to resource
	 * attacks.
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_DELETE_TCB, SCTP_NULL());

	return SCTP_DISPOSITION_DELETE_TCB;

nomem_ack:
	sctp_association_free(new_asoc);
nomem:
	return SCTP_DISPOSITION_NOMEM;
}

/*
 * Respond to a normal INIT ACK chunk.
 * We are the side that is initiating the association.
 *
 * Section: 5.1 Normal Establishment of an Association, C
 * C) Upon reception of the INIT ACK from "Z", "A" shall stop the T1-init
 *    timer and leave COOKIE-WAIT state. "A" shall then send the State
 *    Cookie received in the INIT ACK chunk in a COOKIE ECHO chunk, start
 *    the T1-cookie timer, and enter the COOKIE-ECHOED state.
 *
 *    Note: The COOKIE ECHO chunk can be bundled with any pending outbound
 *    DATA chunks, but it MUST be the first chunk in the packet and
 *    until the COOKIE ACK is returned the sender MUST NOT send any
 *    other packets to the peer.
 *
 * Verification Tag: 3.3.3
 *   If the value of the Initiate Tag in a received INIT ACK chunk is
 *   found to be 0, the receiver MUST treat it as an error and close the
 *   association by transmitting an ABORT.
 *
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_do_5_1C_ack(const sctp_endpoint_t *ep,
				       const sctp_association_t *asoc,
				       const sctp_subtype_t type,
				       void *arg,
				       sctp_cmd_seq_t *commands)
{
	sctp_chunk_t *chunk = arg;
	sctp_init_chunk_t *initchunk;
	__u32 init_tag;

	/* 6.10 Bundling
	 * An endpoint MUST NOT bundle INIT, INIT ACK or
	 * SHUTDOWN COMPLETE with any other chunks.
	 */
	if (!chunk->singleton)
		return SCTP_DISPOSITION_VIOLATION;

	/* Grab the INIT header.  */
	chunk->subh.init_hdr = (sctp_inithdr_t *) chunk->skb->data;	

	init_tag = ntohl(chunk->subh.init_hdr->init_tag);

	/* Verification Tag: 3.3.3
	 *   If the value of the Initiate Tag in a received INIT ACK
	 *   chunk is found to be 0, the receiver MUST treat it as an
	 *   error and close the association by transmitting an ABORT.
	 */
	if (!init_tag) {
		sctp_chunk_t *reply = sctp_make_abort(asoc, chunk, 0);
		if (!reply)
			goto nomem;

		sctp_add_cmd_sf(commands, SCTP_CMD_REPLY,
				SCTP_CHUNK(reply));
		sctp_add_cmd_sf(commands, SCTP_CMD_NEW_STATE,
				SCTP_STATE(SCTP_STATE_CLOSED));
		sctp_add_cmd_sf(commands, SCTP_CMD_DELETE_TCB,
				SCTP_NULL());
		return SCTP_DISPOSITION_DELETE_TCB;
	}

	/* Tag the variable length paramters.  Note that we never
	 * convert the parameters in an INIT chunk.
	 */
	chunk->param_hdr.v =
		skb_pull(chunk->skb, sizeof(sctp_inithdr_t));

	initchunk = (sctp_init_chunk_t *) chunk->chunk_hdr;

	sctp_add_cmd_sf(commands, SCTP_CMD_PEER_INIT,
			SCTP_PEER_INIT(initchunk));

	/* 5.1 C) "A" shall stop the T1-init timer and leave
	 * COOKIE-WAIT state.  "A" shall then ... start the T1-cookie
	 * timer, and enter the COOKIE-ECHOED state.
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_STOP,
			SCTP_TO(SCTP_EVENT_TIMEOUT_T1_INIT));
	sctp_add_cmd_sf(commands, SCTP_CMD_COUNTER_RESET,
			SCTP_COUNTER(SCTP_COUNTER_INIT_ERROR));
	sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_START,
			SCTP_TO(SCTP_EVENT_TIMEOUT_T1_COOKIE));
	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_STATE,
			SCTP_STATE(SCTP_STATE_COOKIE_ECHOED));

	/* 5.1 C) "A" shall then send the State Cookie received in the
	 * INIT ACK chunk in a COOKIE ECHO chunk, ...
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_GEN_COOKIE_ECHO, SCTP_NULL());
	return SCTP_DISPOSITION_CONSUME;

nomem:
	return SCTP_DISPOSITION_NOMEM;
}

/*
 * Respond to a normal COOKIE ECHO chunk.
 * We are the side that is being asked for an association.
 *
 * Section: 5.1 Normal Establishment of an Association, D
 * D) Upon reception of the COOKIE ECHO chunk, Endpoint "Z" will reply
 *    with a COOKIE ACK chunk after building a TCB and moving to
 *    the ESTABLISHED state. A COOKIE ACK chunk may be bundled with
 *    any pending DATA chunks (and/or SACK chunks), but the COOKIE ACK
 *    chunk MUST be the first chunk in the packet.
 *
 *   IMPLEMENTATION NOTE: An implementation may choose to send the
 *   Communication Up notification to the SCTP user upon reception
 *   of a valid COOKIE ECHO chunk.
 *
 * Verification Tag: 8.5.1 Exceptions in Verification Tag Rules
 * D) Rules for packet carrying a COOKIE ECHO
 *
 * - When sending a COOKIE ECHO, the endpoint MUST use the value of the
 *   Initial Tag received in the INIT ACK.
 *
 * - The receiver of a COOKIE ECHO follows the procedures in Section 5.
 *
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_do_5_1D_ce(const sctp_endpoint_t *ep,
				      const sctp_association_t *asoc,
				      const sctp_subtype_t type, void *arg,
				      sctp_cmd_seq_t *commands)
{
	sctp_chunk_t *chunk = arg;
	sctp_association_t *new_asoc;
	sctp_init_chunk_t *peer_init;
	sctp_chunk_t *repl;
	sctp_ulpevent_t *ev;
	int error = 0;

	/* If the packet is an OOTB packet which is temporarily on the
	 * control endpoint, responding with an ABORT.
	 */
	if (ep == sctp_sk((sctp_get_ctl_sock()))->ep)
		return sctp_sf_ootb(ep, asoc, type, arg, commands); 

	/* "Decode" the chunk.  We have no optional parameters so we
	 * are in good shape.
	 */
        chunk->subh.cookie_hdr =
		(sctp_signed_cookie_t *)chunk->skb->data;
	skb_pull(chunk->skb,
		 ntohs(chunk->chunk_hdr->length) - sizeof(sctp_chunkhdr_t));

	/* 5.1 D) Upon reception of the COOKIE ECHO chunk, Endpoint
	 * "Z" will reply with a COOKIE ACK chunk after building a TCB
	 * and moving to the ESTABLISHED state.
	 */
	new_asoc = sctp_unpack_cookie(ep, asoc, chunk, GFP_ATOMIC, &error);

	/* FIXME:
	 * If the re-build failed, what is the proper error path
	 * from here?
	 *
	 * [We should abort the association. --piggy]
	 */
	if (!new_asoc) {
		/* FIXME: Several errors are possible.  A bad cookie should
		 * be silently discarded, but think about logging it too.
		 */
		switch (error) {
		case -SCTP_IERROR_NOMEM:
			goto nomem;

		case -SCTP_IERROR_BAD_SIG:
		default:
			return sctp_sf_pdiscard(ep, asoc, type,
						arg, commands);
		};
	}

	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_ASOC, SCTP_ASOC(new_asoc));
	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_STATE,
			SCTP_STATE(SCTP_STATE_ESTABLISHED));
	sctp_add_cmd_sf(commands, SCTP_CMD_HB_TIMERS_START, SCTP_NULL());

	if (new_asoc->autoclose)
		sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_START,
				SCTP_TO(SCTP_EVENT_TIMEOUT_AUTOCLOSE));

	sctp_add_cmd_sf(commands, SCTP_CMD_TRANSMIT, SCTP_NULL());

	/* Re-build the bind address for the association is done in
	 * the sctp_unpack_cookie() already.
	 */
	/* This is a brand-new association, so these are not yet side
	 * effects--it is safe to run them here.
	 */
	peer_init = &chunk->subh.cookie_hdr->c.peer_init[0];
	sctp_process_init(new_asoc, chunk->chunk_hdr->type,
			  &chunk->subh.cookie_hdr->c.peer_addr, peer_init,
			  GFP_ATOMIC);

	repl = sctp_make_cookie_ack(new_asoc, chunk);
	if (!repl)
		goto nomem_repl;

	sctp_add_cmd_sf(commands, SCTP_CMD_REPLY, SCTP_CHUNK(repl));

	/* RFC 2960 5.1 Normal Establishment of an Association
	 *
	 * D) IMPLEMENTATION NOTE: An implementation may choose to
	 * send the Communication Up notification to the SCTP user
	 * upon reception of a valid COOKIE ECHO chunk.
	 */
	ev = sctp_ulpevent_make_assoc_change(new_asoc, 0, SCTP_COMM_UP, 0,
					     new_asoc->c.sinit_num_ostreams,
					     new_asoc->c.sinit_max_instreams,
					     GFP_ATOMIC);
	if (!ev)
		goto nomem_ev;

	sctp_add_cmd_sf(commands, SCTP_CMD_EVENT_ULP, SCTP_ULPEVENT(ev));

	return SCTP_DISPOSITION_CONSUME;

nomem_ev:
	sctp_free_chunk(repl);

nomem_repl:
	sctp_association_free(new_asoc);

nomem:
	return SCTP_DISPOSITION_NOMEM;
}

/*
 * Respond to a normal COOKIE ACK chunk.
 * We are the side that is being asked for an association.
 *
 * RFC 2960 5.1 Normal Establishment of an Association
 *
 * E) Upon reception of the COOKIE ACK, endpoint "A" will move from the
 *    COOKIE-ECHOED state to the ESTABLISHED state, stopping the T1-cookie
 *    timer. It may also notify its ULP about the successful
 *    establishment of the association with a Communication Up
 *    notification (see Section 10).
 *
 * Verification Tag:
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_do_5_1E_ca(const sctp_endpoint_t *ep,
				      const sctp_association_t *asoc,
				      const sctp_subtype_t type, void *arg,
				      sctp_cmd_seq_t *commands)
{
	sctp_ulpevent_t *ev;

	/* RFC 2960 5.1 Normal Establishment of an Association
	 *
	 * E) Upon reception of the COOKIE ACK, endpoint "A" will move
	 * from the COOKIE-ECHOED state to the ESTABLISHED state,
	 * stopping the T1-cookie timer.
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_STOP,
			SCTP_TO(SCTP_EVENT_TIMEOUT_T1_COOKIE));
	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_STATE,
			SCTP_STATE(SCTP_STATE_ESTABLISHED));
	sctp_add_cmd_sf(commands, SCTP_CMD_HB_TIMERS_START, SCTP_NULL());
	if (asoc->autoclose)
		sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_START, 
				SCTP_TO(SCTP_EVENT_TIMEOUT_AUTOCLOSE));
	sctp_add_cmd_sf(commands, SCTP_CMD_TRANSMIT, SCTP_NULL());

	/* It may also notify its ULP about the successful
	 * establishment of the association with a Communication Up
	 * notification (see Section 10).
	 */
	ev = sctp_ulpevent_make_assoc_change(asoc, 0, SCTP_COMM_UP,
					     0, asoc->c.sinit_num_ostreams,
					     asoc->c.sinit_max_instreams,
					     GFP_ATOMIC);

	if (!ev)
		goto nomem;

	sctp_add_cmd_sf(commands, SCTP_CMD_EVENT_ULP, SCTP_ULPEVENT(ev));

	return SCTP_DISPOSITION_CONSUME;

nomem:
	return SCTP_DISPOSITION_NOMEM;
}

/* Generate a HEARTBEAT packet on the given transport.  */
sctp_disposition_t sctp_sf_sendbeat_8_3(const sctp_endpoint_t *ep,
					const sctp_association_t *asoc,
					const sctp_subtype_t type,
					void *arg,
					sctp_cmd_seq_t *commands)
{
	sctp_transport_t *transport = (sctp_transport_t *) arg;
	sctp_chunk_t *reply;
	sctp_sender_hb_info_t hbinfo;
	size_t paylen = 0;

	if (asoc->overall_error_count >= asoc->overall_error_threshold) {
		/* CMD_ASSOC_FAILED calls CMD_DELETE_TCB. */
		sctp_add_cmd_sf(commands, SCTP_CMD_ASSOC_FAILED, SCTP_NULL());
		return SCTP_DISPOSITION_DELETE_TCB;
	}

	/* Section 3.3.5.
	 * The Sender-specific Heartbeat Info field should normally include
	 * information about the sender's current time when this HEARTBEAT
	 * chunk is sent and the destination transport address to which this
	 * HEARTBEAT is sent (see Section 8.3).
	 */

	hbinfo.param_hdr.type = SCTP_PARAM_HEATBEAT_INFO;
	hbinfo.param_hdr.length = htons(sizeof(sctp_sender_hb_info_t));
	hbinfo.daddr = transport->ipaddr;
	hbinfo.sent_at = jiffies;

	/* Set rto_pending indicating that an RTT measurement is started
	 * with this heartbeat chunk.
	 */
	transport->rto_pending = 1;

	/* Send a heartbeat to our peer.  */
	paylen = sizeof(sctp_sender_hb_info_t);
	reply = sctp_make_heartbeat(asoc, transport, &hbinfo, paylen);
	if (!reply)
		goto nomem;

	sctp_add_cmd_sf(commands, SCTP_CMD_REPLY,
			SCTP_CHUNK(reply));

	/* Set transport error counter and association error counter
	 * when sending heartbeat.
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_TRANSPORT_RESET,
			SCTP_TRANSPORT(transport));

        return SCTP_DISPOSITION_CONSUME;

nomem:
	return SCTP_DISPOSITION_NOMEM;
}

/*
 * Process an heartbeat request.
 *
 * Section: 8.3 Path Heartbeat
 * The receiver of the HEARTBEAT should immediately respond with a
 * HEARTBEAT ACK that contains the Heartbeat Information field copied
 * from the received HEARTBEAT chunk.
 *
 * Verification Tag:  8.5 Verification Tag [Normal verification]
 * When receiving an SCTP packet, the endpoint MUST ensure that the
 * value in the Verification Tag field of the received SCTP packet
 * matches its own Tag. If the received Verification Tag value does not
 * match the receiver's own tag value, the receiver shall silently
 * discard the packet and shall not process it any further except for
 * those cases listed in Section 8.5.1 below.
 *
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_beat_8_3(const sctp_endpoint_t *ep,
				    const sctp_association_t *asoc,
				    const sctp_subtype_t type,
				    void *arg,
				    sctp_cmd_seq_t *commands)
{
	sctp_chunk_t *chunk = arg;
	sctp_chunk_t *reply;
	size_t paylen = 0;

	/* 8.5 When receiving an SCTP packet, the endpoint MUST ensure
	 * that the value in the Verification Tag field of the
	 * received SCTP packet matches its own Tag. If the received
	 * Verification Tag value does not match the receiver's own
	 * tag value, the receiver shall silently discard the packet...
	 */
	if (ntohl(chunk->sctp_hdr->vtag) != asoc->c.my_vtag)
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);

	/* 8.3 The receiver of the HEARTBEAT should immediately
	 * respond with a HEARTBEAT ACK that contains the Heartbeat
	 * Information field copied from the received HEARTBEAT chunk.
	 */
	chunk->subh.hb_hdr = (sctp_heartbeathdr_t *) chunk->skb->data;
	paylen = ntohs(chunk->chunk_hdr->length) - sizeof(sctp_chunkhdr_t);
	skb_pull(chunk->skb, paylen);

	reply = sctp_make_heartbeat_ack(asoc, chunk,
					chunk->subh.hb_hdr, paylen);
	if (!reply)
		goto nomem;

	sctp_add_cmd_sf(commands, SCTP_CMD_REPLY, SCTP_CHUNK(reply));
	return SCTP_DISPOSITION_CONSUME;

nomem:
	return SCTP_DISPOSITION_NOMEM;
}

/*
 * Process the returning HEARTBEAT ACK.
 *
 * Section: 8.3 Path Heartbeat
 * Upon the receipt of the HEARTBEAT ACK, the sender of the HEARTBEAT
 * should clear the error counter of the destination transport
 * address to which the HEARTBEAT was sent, and mark the destination
 * transport address as active if it is not so marked. The endpoint may
 * optionally report to the upper layer when an inactive destination
 * address is marked as active due to the reception of the latest
 * HEARTBEAT ACK. The receiver of the HEARTBEAT ACK must also
 * clear the association overall error count as well (as defined
 * in section 8.1).
 *
 * The receiver of the HEARTBEAT ACK should also perform an RTT
 * measurement for that destination transport address using the time
 * value carried in the HEARTBEAT ACK chunk.
 *
 * Verification Tag:  8.5 Verification Tag [Normal verification]
 *
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_backbeat_8_3(const sctp_endpoint_t *ep,
					const sctp_association_t *asoc,
					const sctp_subtype_t type,
					void *arg,
					sctp_cmd_seq_t *commands)
{
	sctp_chunk_t *chunk = arg;
	sockaddr_storage_t from_addr;
	sctp_transport_t *link;
	sctp_sender_hb_info_t *hbinfo;
	unsigned long max_interval;

	/* 8.5 When receiving an SCTP packet, the endpoint MUST ensure
	 * that the value in the Verification Tag field of the
	 * received SCTP packet matches its own Tag. ...
	 */
	if (ntohl(chunk->sctp_hdr->vtag) != asoc->c.my_vtag)
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);

	hbinfo = (sctp_sender_hb_info_t *) chunk->skb->data;
	from_addr = hbinfo->daddr;		
	link = sctp_assoc_lookup_paddr(asoc, &from_addr);

	/* This should never happen, but lets log it if so.  */
	if (!link) {
		printk(KERN_WARNING
		       "%s: Could not find address %d.%d.%d.%d\n",
		       __FUNCTION__, NIPQUAD(from_addr.v4.sin_addr));
		return SCTP_DISPOSITION_DISCARD;
	}

	max_interval = link->hb_interval + link->rto;

	/* Check if the timestamp looks valid.  */
	if (time_after(hbinfo->sent_at, jiffies) ||
	    time_after(jiffies, hbinfo->sent_at + max_interval)) {
		SCTP_DEBUG_PRINTK("%s: HEARTBEAT ACK with invalid timestamp
				   received for transport: %p\n",
				   __FUNCTION__, link);
		return SCTP_DISPOSITION_DISCARD;
	}

	/* 8.3 Upon the receipt of the HEARTBEAT ACK, the sender of
	 * the HEARTBEAT should clear the error counter of the
	 * destination transport address to which the HEARTBEAT was
	 * sent and mark the destination transport address as active if
	 * it is not so marked.
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_TRANSPORT_ON,
			SCTP_TRANSPORT(link));

	return SCTP_DISPOSITION_CONSUME;
}

/* Populate the verification/tie tags based on overlapping INIT
 * scenario.
 *
 * Note: Do not use in CLOSED or SHUTDOWN-ACK-SENT state.
 */
static void sctp_tietags_populate(sctp_association_t *new_asoc,
				  const sctp_association_t *asoc)
{
	switch (asoc->state) {

	/* 5.2.1 INIT received in COOKIE-WAIT or COOKIE-ECHOED State */

	case SCTP_STATE_COOKIE_WAIT:
		new_asoc->c.my_vtag     = asoc->c.my_vtag;
		new_asoc->c.my_ttag     = asoc->c.my_vtag;
		new_asoc->c.peer_ttag   = 0;
		break;

	case SCTP_STATE_COOKIE_ECHOED:
		new_asoc->c.my_vtag     = asoc->c.my_vtag;
		new_asoc->c.my_ttag     = asoc->c.my_vtag;
		new_asoc->c.peer_ttag   = asoc->c.peer_vtag;
		break;

	/* 5.2.2 Unexpected INIT in States Other than CLOSED, COOKIE-ECHOED,
	 *   COOKIE-WAIT and SHUTDOWN-ACK-SENT
	 */
	default:
		new_asoc->c.my_ttag   = asoc->c.my_vtag;
		new_asoc->c.peer_ttag = asoc->c.peer_vtag;
		break;
	};

	/* Other parameters for the endpoint SHOULD be copied from the
	 * existing parameters of the association (e.g. number of
	 * outbound streams) into the INIT ACK and cookie.
	 */
	new_asoc->rwnd                  = asoc->rwnd;
	new_asoc->c.sinit_num_ostreams  = asoc->c.sinit_num_ostreams;
	new_asoc->c.sinit_max_instreams = asoc->c.sinit_max_instreams;
	new_asoc->c.initial_tsn         = asoc->c.initial_tsn;
}

/*
 * Compare vtag/tietag values to determine unexpected COOKIE-ECHO
 * handling action.
 *
 * RFC 2960 5.2.4 Handle a COOKIE ECHO when a TCB exists.
 *
 * Returns value representing action to be taken.   These action values
 * correspond to Action/Description values in RFC 2960, Table 2.
 */
static char sctp_tietags_compare(sctp_association_t *new_asoc,
				 const sctp_association_t *asoc)
{
	/* In this case, the peer may have restarted.  */
	if ((asoc->c.my_vtag != new_asoc->c.my_vtag) &&
	    (asoc->c.peer_vtag != new_asoc->c.peer_vtag) &&
	    (asoc->c.my_vtag   == new_asoc->c.my_ttag) &&
	    (asoc->c.peer_vtag == new_asoc->c.peer_ttag))
		return 'A';
	
	/* Collision case D.
	 * Note: Test case D first, otherwise it may be incorrectly
	 * identified as second case of B if the value of the Tie_tag is
	 * not filled into the state cookie.
	 */
	if ((asoc->c.my_vtag == new_asoc->c.my_vtag) &&
	    (asoc->c.peer_vtag == new_asoc->c.peer_vtag))
		return 'D';

	/* Collision case B. */
	if ((asoc->c.my_vtag == new_asoc->c.my_vtag) &&
	    ((asoc->c.peer_vtag != new_asoc->c.peer_vtag) ||
	     (!new_asoc->c.my_ttag && !new_asoc->c.peer_ttag)))
		return 'B';

	/* Collision case C. */
	if ((asoc->c.my_vtag != new_asoc->c.my_vtag) &&
	    (asoc->c.peer_vtag == new_asoc->c.peer_vtag) &&
	    (0 == new_asoc->c.my_ttag) &&
	    (0 == new_asoc->c.peer_ttag))
		return 'C';

	return 'E'; /* No such case available. */
}

/* Common helper routine for both duplicate and simulataneous INIT
 * chunk handling.
 */
static sctp_disposition_t sctp_sf_do_unexpected_init(const sctp_endpoint_t *ep,
	const sctp_association_t *asoc, const sctp_subtype_t type,
	void *arg, sctp_cmd_seq_t *commands)
{
	sctp_chunk_t *chunk = arg;
	sctp_chunk_t *repl;
	sctp_association_t *new_asoc;

	/* 6.10 Bundling
	 * An endpoint MUST NOT bundle INIT, INIT ACK or
	 * SHUTDOWN COMPLETE with any other chunks.
	 */
	if (!chunk->singleton)
		return SCTP_DISPOSITION_VIOLATION;

	/* Grab the INIT header.  */
	chunk->subh.init_hdr = (sctp_inithdr_t *) chunk->skb->data;

	/* Tag the variable length parameters.  */
	chunk->param_hdr.v =
		skb_pull(chunk->skb, sizeof(sctp_inithdr_t));

	/*
	 * Other parameters for the endpoint SHOULD be copied from the
	 * existing parameters of the association (e.g. number of
	 * outbound streams) into the INIT ACK and cookie.
	 * FIXME:  We are copying parameters from the endpoint not the
	 * association.
	 */
	new_asoc = sctp_make_temp_asoc(ep, chunk, GFP_ATOMIC);
	if (!new_asoc)
		goto nomem;

	/* In the outbound INIT ACK the endpoint MUST copy its current
	 * Verification Tag and Peers Verification tag into a reserved
	 * place (local tie-tag and per tie-tag) within the state cookie.
	 */
	sctp_process_init(new_asoc, chunk->chunk_hdr->type,
			  sctp_source(chunk),
			  (sctp_init_chunk_t *)chunk->chunk_hdr,
			  GFP_ATOMIC);
	sctp_tietags_populate(new_asoc, asoc);

	/* B) "Z" shall respond immediately with an INIT ACK chunk.  */
	repl = sctp_make_init_ack(new_asoc, chunk, GFP_ATOMIC);
	if (!repl)
		goto nomem;

	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_ASOC, SCTP_ASOC(new_asoc));
	sctp_add_cmd_sf(commands, SCTP_CMD_REPLY, SCTP_CHUNK(repl));

	/*
         * Note: After sending out INIT ACK with the State Cookie parameter,
	 * "Z" MUST NOT allocate any resources for this new association.
	 * Otherwise, "Z" will be vulnerable to resource attacks.
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_DELETE_TCB, SCTP_NULL());
	return SCTP_DISPOSITION_CONSUME;

nomem:
	return SCTP_DISPOSITION_NOMEM;
}

/*
 * Handle simultanous INIT.
 * This means we started an INIT and then we got an INIT request from
 * our peer.
 *
 * Section: 5.2.1 INIT received in COOKIE-WAIT or COOKIE-ECHOED State (Item B)
 * This usually indicates an initialization collision, i.e., each
 * endpoint is attempting, at about the same time, to establish an
 * association with the other endpoint.
 *
 * Upon receipt of an INIT in the COOKIE-WAIT or COOKIE-ECHOED state, an
 * endpoint MUST respond with an INIT ACK using the same parameters it
 * sent in its original INIT chunk (including its Verification Tag,
 * unchanged). These original parameters are combined with those from the
 * newly received INIT chunk. The endpoint shall also generate a State
 * Cookie with the INIT ACK. The endpoint uses the parameters sent in its
 * INIT to calculate the State Cookie.
 *
 * After that, the endpoint MUST NOT change its state, the T1-init
 * timer shall be left running and the corresponding TCB MUST NOT be
 * destroyed. The normal procedures for handling State Cookies when
 * a TCB exists will resolve the duplicate INITs to a single association.
 *
 * For an endpoint that is in the COOKIE-ECHOED state it MUST populate
 * its Tie-Tags with the Tag information of itself and its peer (see
 * section 5.2.2 for a description of the Tie-Tags).
 *
 * Verification Tag: Not explicit, but an INIT can not have a valid
 * verification tag, so we skip the check.
 *
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_do_5_2_1_siminit(const sctp_endpoint_t *ep,
					    const sctp_association_t *asoc,
					    const sctp_subtype_t type,
					    void *arg,
					    sctp_cmd_seq_t *commands)
{
	/* Call helper to do the real work for both simulataneous and
	 * duplicate INIT chunk handling.
	 */
	return sctp_sf_do_unexpected_init(ep, asoc, type, arg, commands);
}

/*
 * Handle duplicated INIT messages.  These are usually delayed
 * restransmissions.
 *
 * Section: 5.2.2 Unexpected INIT in States Other than CLOSED,
 * COOKIE-ECHOED and COOKIE-WAIT
 *
 * Unless otherwise stated, upon reception of an unexpected INIT for
 * this association, the endpoint shall generate an INIT ACK with a
 * State Cookie.  In the outbound INIT ACK the endpoint MUST copy its
 * current Verification Tag and peer's Verification Tag into a reserved
 * place within the state cookie.  We shall refer to these locations as
 * the Peer's-Tie-Tag and the Local-Tie-Tag.  The outbound SCTP packet
 * containing this INIT ACK MUST carry a Verification Tag value equal to
 * the Initiation Tag found in the unexpected INIT.  And the INIT ACK
 * MUST contain a new Initiation Tag (randomly generated see Section
 * 5.3.1).  Other parameters for the endpoint SHOULD be copied from the
 * existing parameters of the association (e.g. number of outbound
 * streams) into the INIT ACK and cookie.
 *
 * After sending out the INIT ACK, the endpoint shall take no further
 * actions, i.e., the existing association, including its current state,
 * and the corresponding TCB MUST NOT be changed.
 *
 * Note: Only when a TCB exists and the association is not in a COOKIE-
 * WAIT state are the Tie-Tags populated.  For a normal association INIT
 * (i.e. the endpoint is in a COOKIE-WAIT state), the Tie-Tags MUST be
 * set to 0 (indicating that no previous TCB existed).  The INIT ACK and
 * State Cookie are populated as specified in section 5.2.1.
 *
 * Verification Tag: Not specifed, but an INIT has no way of knowing
 * what the verification tag could be, so we ignore it.
 *
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_do_5_2_2_dupinit(const sctp_endpoint_t *ep,
					    const sctp_association_t *asoc,
					    const sctp_subtype_t type,
					    void *arg,
					    sctp_cmd_seq_t *commands)
{
	/* Call helper to do the real work for both simulataneous and
	 * duplicate INIT chunk handling.
	 */
	return sctp_sf_do_unexpected_init(ep, asoc, type, arg, commands);
}

/* Unexpected COOKIE-ECHO handlerfor peer restart (Table 2, action 'A')
 *
 * Section 5.2.4
 *  A)  In this case, the peer may have restarted.
 */
static sctp_disposition_t sctp_sf_do_dupcook_a(const sctp_endpoint_t *ep,
					       const sctp_association_t *asoc,
					       sctp_chunk_t *chunk,
					       sctp_cmd_seq_t *commands,
					       sctp_association_t *new_asoc)
{
	sctp_init_chunk_t *peer_init;
	sctp_ulpevent_t *ev;
	sctp_chunk_t *repl;
	sctp_transport_t *new_addr, *addr;
	struct list_head *pos, *pos2, *temp;
	int found, error;

	/* new_asoc is a brand-new association, so these are not yet
	 * side effects--it is safe to run them here.
	 */
	peer_init = &chunk->subh.cookie_hdr->c.peer_init[0];
	sctp_process_init(new_asoc, chunk->chunk_hdr->type,
			  sctp_source(chunk), peer_init, GFP_ATOMIC);

	/* Make sure peer is not adding new addresses.  */
	found = 0;
	new_addr = NULL;
	list_for_each(pos, &new_asoc->peer.transport_addr_list) {
		new_addr = list_entry(pos, sctp_transport_t, transports);
		found = 1;
		list_for_each_safe(pos2, temp,
				   &asoc->peer.transport_addr_list) {
			addr = list_entry(pos2, sctp_transport_t, transports);
			if (!sctp_cmp_addr_exact(&new_addr->ipaddr,
						 &addr->ipaddr)) {
				found = 0;
				break;
			}
		}
		if (!found)
			break;
	}

	if (!found) {
		sctp_bind_addr_t *bp;
		sctpParam_t rawaddr;
		int len;

		bp = sctp_bind_addr_new(GFP_ATOMIC);
		if (!bp)
			goto nomem;

		error = sctp_add_bind_addr(bp, &new_addr->ipaddr, GFP_ATOMIC);
		if (error)
			goto nomem_add;

		rawaddr = sctp_bind_addrs_to_raw(bp, &len, GFP_ATOMIC);
		if (!rawaddr.v)
			goto nomem_raw;

		repl = sctp_make_abort(asoc, chunk, len+sizeof(sctp_errhdr_t));
		if (!repl)
			goto nomem_abort;
		sctp_init_cause(repl, SCTP_ERROR_RESTART, rawaddr.v, len);
		sctp_add_cmd_sf(commands, SCTP_CMD_REPLY, SCTP_CHUNK(repl));
		return SCTP_DISPOSITION_CONSUME;

	nomem_abort:
		kfree(rawaddr.v);

	nomem_raw:
	nomem_add:
		sctp_bind_addr_free(bp);
		goto nomem;
	}

	/* For now, fail any unsent/unacked data.  Consider the optional
	 * choice of resending of this data.
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_PURGE_OUTQUEUE, SCTP_NULL());

	/* Update the content of current association. */
	sctp_add_cmd_sf(commands, SCTP_CMD_UPDATE_ASSOC, SCTP_ASOC(new_asoc));

	repl = sctp_make_cookie_ack(new_asoc, chunk);
	if (!repl)
		goto nomem;

	sctp_add_cmd_sf(commands, SCTP_CMD_REPLY, SCTP_CHUNK(repl));

	/* Report association restart to upper layer. */
	ev = sctp_ulpevent_make_assoc_change(asoc, 0, SCTP_RESTART, 0,
					     new_asoc->c.sinit_num_ostreams,
					     new_asoc->c.sinit_max_instreams,
					     GFP_ATOMIC);
	if (!ev)
		goto nomem_ev;

	sctp_add_cmd_sf(commands, SCTP_CMD_EVENT_ULP, SCTP_ULPEVENT(ev));
	return SCTP_DISPOSITION_CONSUME;

nomem_ev:
	sctp_free_chunk(repl);

nomem:
	return SCTP_DISPOSITION_NOMEM;
}

/* Unexpected COOKIE-ECHO handler for setup collision (Table 2, action 'B')
 *
 * Section 5.2.4
 *   B) In this case, both sides may be attempting to start an association
 *      at about the same time but the peer endpoint started its INIT
 *      after responding to the local endpoint's INIT
 */
/* This case represents an intialization collision.  */
static sctp_disposition_t sctp_sf_do_dupcook_b(const sctp_endpoint_t *ep,
					       const sctp_association_t *asoc,
					       sctp_chunk_t *chunk,
					       sctp_cmd_seq_t *commands,
					       sctp_association_t *new_asoc)
{
	sctp_init_chunk_t *peer_init;
	sctp_ulpevent_t *ev;
	sctp_chunk_t *repl;

	/* new_asoc is a brand-new association, so these are not yet
	 * side effects--it is safe to run them here.
	 */
	peer_init = &chunk->subh.cookie_hdr->c.peer_init[0];
	sctp_process_init(new_asoc, chunk->chunk_hdr->type,
			  sctp_source(chunk), peer_init, GFP_ATOMIC);

	/* Update the content of current association.  */
	sctp_add_cmd_sf(commands, SCTP_CMD_UPDATE_ASSOC, SCTP_ASOC(new_asoc));
	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_STATE,
			SCTP_STATE(SCTP_STATE_ESTABLISHED));
	sctp_add_cmd_sf(commands, SCTP_CMD_HB_TIMERS_START, SCTP_NULL());

	repl = sctp_make_cookie_ack(new_asoc, chunk);
	if (!repl)
		goto nomem;

	sctp_add_cmd_sf(commands, SCTP_CMD_REPLY, SCTP_CHUNK(repl));
	sctp_add_cmd_sf(commands, SCTP_CMD_TRANSMIT, SCTP_NULL());

	/* RFC 2960 5.1 Normal Establishment of an Association
	 *
	 * D) IMPLEMENTATION NOTE: An implementation may choose to
	 * send the Communication Up notification to the SCTP user
	 * upon reception of a valid COOKIE ECHO chunk.
	 */
	ev = sctp_ulpevent_make_assoc_change(asoc, 0, SCTP_COMM_UP, 0,
					     new_asoc->c.sinit_num_ostreams,
					     new_asoc->c.sinit_max_instreams,
					     GFP_ATOMIC);
	if (!ev)
		goto nomem_ev;

	sctp_add_cmd_sf(commands, SCTP_CMD_EVENT_ULP, SCTP_ULPEVENT(ev));
	return SCTP_DISPOSITION_CONSUME;

nomem_ev:
	sctp_free_chunk(repl);
nomem:
	return SCTP_DISPOSITION_NOMEM;
}

/* Unexpected COOKIE-ECHO handler for setup collision (Table 2, action 'C')
 *
 * Section 5.2.4
 *  C) In this case, the local endpoint's cookie has arrived late.
 *     Before it arrived, the local endpoint sent an INIT and received an
 *     INIT-ACK and finally sent a COOKIE ECHO with the peer's same tag
 *     but a new tag of its own.
 */
/* This case represents an intialization collision.  */
static sctp_disposition_t sctp_sf_do_dupcook_c(const sctp_endpoint_t *ep,
					       const sctp_association_t *asoc,
					       sctp_chunk_t *chunk,
					       sctp_cmd_seq_t *commands,
					       sctp_association_t *new_asoc)
{
	/* The cookie should be silently discarded.
	 * The endpoint SHOULD NOT change states and should leave
	 * any timers running.
	 */
	return SCTP_DISPOSITION_DISCARD;
}

/* Unexpected COOKIE-ECHO handler lost chunk (Table 2, action 'D')
 *
 * Section 5.2.4
 *
 * D) When both local and remote tags match the endpoint should always
 *    enter the ESTABLISHED state, if it has not already done so.
 */
/* This case represents an intialization collision.  */
static sctp_disposition_t sctp_sf_do_dupcook_d(const sctp_endpoint_t *ep,
					       const sctp_association_t *asoc,
					       sctp_chunk_t *chunk,
					       sctp_cmd_seq_t *commands,
					       sctp_association_t *new_asoc)
{
	sctp_ulpevent_t *ev = NULL;
	sctp_chunk_t *repl;

	/* The local endpoint cannot use any value from the received
	 * state cookie and need to immediately resend a COOKIE-ACK
	 * and move into ESTABLISHED if it hasn't done so.
	 */
	if (SCTP_STATE_ESTABLISHED != asoc->state) {
		sctp_add_cmd_sf(commands, SCTP_CMD_NEW_STATE,
				SCTP_STATE(SCTP_STATE_ESTABLISHED));
		sctp_add_cmd_sf(commands, SCTP_CMD_HB_TIMERS_START, 
				SCTP_NULL());

		/* RFC 2960 5.1 Normal Establishment of an Association
		 *
		 * D) IMPLEMENTATION NOTE: An implementation may choose
		 * to send the Communication Up notification to the
		 * SCTP user upon reception of a valid COOKIE
		 * ECHO chunk.
		 */
		ev = sctp_ulpevent_make_assoc_change(new_asoc, 0,
					     SCTP_COMM_UP, 0,
					     new_asoc->c.sinit_num_ostreams,
					     new_asoc->c.sinit_max_instreams,
                                             GFP_ATOMIC);
		if (!ev)
			goto nomem;
		sctp_add_cmd_sf(commands, SCTP_CMD_EVENT_ULP,
				SCTP_ULPEVENT(ev));
	}
	sctp_add_cmd_sf(commands, SCTP_CMD_TRANSMIT, SCTP_NULL());

	repl = sctp_make_cookie_ack(new_asoc, chunk);
	if (!repl)
		goto nomem;

	sctp_add_cmd_sf(commands, SCTP_CMD_REPLY, SCTP_CHUNK(repl));
	sctp_add_cmd_sf(commands, SCTP_CMD_TRANSMIT, SCTP_NULL());
	return SCTP_DISPOSITION_CONSUME;

nomem:
	if (ev)
		sctp_ulpevent_free(ev);
	return SCTP_DISPOSITION_NOMEM;
}

/*
 * Handle a duplicate COOKIE-ECHO.  This usually means a cookie-carrying
 * chunk was retransmitted and then delayed in the network.
 *
 * Section: 5.2.4 Handle a COOKIE ECHO when a TCB exists
 *
 * Verification Tag: None.  Do cookie validation.
 *
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_do_5_2_4_dupcook(const sctp_endpoint_t *ep,
					    const sctp_association_t *asoc,
					    const sctp_subtype_t type,
					    void *arg,
					    sctp_cmd_seq_t *commands)
{
	sctp_disposition_t retval;
	sctp_chunk_t *chunk = arg;
	sctp_association_t *new_asoc;
	int error = 0;
	char action;

	/* "Decode" the chunk.  We have no optional parameters so we
	 * are in good shape.
	 */
        chunk->subh.cookie_hdr =
		(sctp_signed_cookie_t *) chunk->skb->data;
	skb_pull(chunk->skb,
		 ntohs(chunk->chunk_hdr->length) - sizeof(sctp_chunkhdr_t));

	/* In RFC 2960 5.2.4 3, if both Verification Tags in the State Cookie
	 * of a duplicate COOKIE ECHO match the Verification Tags of the
	 * current association, consider the State Cookie valid even if
	 * the lifespan is exceeded.
	 */
	new_asoc = sctp_unpack_cookie(ep, asoc, chunk, GFP_ATOMIC, &error);

        /* FIXME:
	 * If the re-build failed, what is the proper error path
	 * from here?
	 *
	 * [We should abort the association. --piggy]
	 */
	if (!new_asoc) {
		 /* FIXME: Several errors are possible.  A bad cookie should
		  * be silently discarded, but think about logging it too.
		  */
		 switch (error) {
		 case -SCTP_IERROR_NOMEM:
			 goto nomem;

		 case -SCTP_IERROR_BAD_SIG:
		 default:
			 return sctp_sf_pdiscard(ep, asoc, type,
						 arg, commands);
		 };
	}

	/* Compare the tie_tag in cookie with the verification tag of
	 * current association.
	 */
	action = sctp_tietags_compare(new_asoc, asoc);

	switch (action) {
	case 'A': /* Association restart. */
		retval = sctp_sf_do_dupcook_a(ep, asoc, chunk, commands,
					      new_asoc);
		break;

	case 'B': /* Collision case B. */
		retval = sctp_sf_do_dupcook_b(ep, asoc, chunk, commands,
					      new_asoc);
		break;

	case 'C': /* Collisioun case C. */
		retval = sctp_sf_do_dupcook_c(ep, asoc, chunk, commands,
					      new_asoc);
		break;

	case 'D': /* Collision case D. */
		retval = sctp_sf_do_dupcook_d(ep, asoc, chunk, commands,
					      new_asoc);
		break;

	default: /* No such case, discard it. */
		printk(KERN_WARNING "%s:unknown case\n", __FUNCTION__);
		retval = SCTP_DISPOSITION_DISCARD;
		break;
        };

	/* Delete the tempory new association. */
	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_ASOC, SCTP_ASOC(new_asoc));
	sctp_add_cmd_sf(commands, SCTP_CMD_DELETE_TCB, SCTP_NULL());

	return retval;

nomem:
	return SCTP_DISPOSITION_NOMEM;
}

#if 0
/*
 * Handle a Stale COOKIE Error
 *
 * Section: 5.2.6 Handle Stale COOKIE Error
 * If the association is in the COOKIE-ECHOED state, the endpoint may elect
 * one of the following three alternatives.
 * ...
 * 3) Send a new INIT chunk to the endpoint, adding a Cookie
 *    Preservative parameter requesting an extension to the lifetime of
 *    the State Cookie. When calculating the time extension, an
 *    implementation SHOULD use the RTT information measured based on the
 *    previous COOKIE ECHO / ERROR exchange, and should add no more
 *    than 1 second beyond the measured RTT, due to long State Cookie
 *    lifetimes making the endpoint more subject to a replay attack.
 *
 * Verification Tag:  Not explicit, but safe to ignore.
 *
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t do_5_2_6_stale(const sctp_endpoint_t *ep,
				  const sctp_association_t *asoc,
				  const sctp_subtype_t type,
				  void *arg,
				  sctp_cmd_seq_t *commands)
{
	sctp_chunk_t *chunk = arg;

	/* This is not a real chunk type.  It is a subtype of the
	 * ERROR chunk type.  The ERROR chunk processing will bring us
	 * here.
	 */
	sctp_chunk_t *in_packet;
	stp_chunk_t *reply;
	sctp_inithdr_t initack;
	__u8 *addrs;
	int addrs_len;
	time_t rtt;
	struct sctpCookiePreserve bht; 

	/* If we have gotten too many failures, give up.  */
	if (1 + asoc->counters[SctpCounterInits] > asoc->max_init_attempts) {
		/* FIXME: Move to new ulpevent.  */
		retval->event_up = sctp_make_ulp_init_timeout(asoc);
		if (!retval->event_up)
			goto nomem;
		sctp_add_cmd_sf(retval->commands, SCTP_CMD_DELETE_TCB,
				SCTP_NULL());
		return SCTP_DISPOSITION_DELETE_TCB;
	}

	retval->counters[0] = SCTP_COUNTER_INCR;
	retval->counters[0] = SctpCounterInits;
	retval->counters[1] = 0;
	retval->counters[1] = 0;

	/* Calculate the RTT in ms.  */
	/* BUG--we should get the send time of the HEARTBEAT REQUEST.  */
	in_packet = chunk;
	rtt = 1000 * timeval_sub(in_packet->skb->stamp,
				 asoc->c.state_timestamp);

	/* When calculating the time extension, an implementation
	 * SHOULD use the RTT information measured based on the
	 * previous COOKIE ECHO / ERROR exchange, and should add no
	 * more than 1 second beyond the measured RTT, due to long
	 * State Cookie lifetimes making the endpoint more subject to
	 * a replay attack.
	 */
	bht.p = {SCTP_COOKIE_PRESERVE, 8};
	bht.extraTime = htonl(rtt + 1000);

	initack.init_tag		= htonl(asoc->c.my_vtag);
	initack.a_rwnd 		        = htonl(atomic_read(&asoc->rnwd));
	initack.num_outbound_streams    = htons(asoc->streamoutcnt);
	initack.num_inbound_streams     = htons(asoc->streamincnt);
	initack.initial_tsn             = htonl(asoc->c.initSeqNumber);

	sctp_get_my_addrs(asoc, &addrs, &addrs_len);

	/* Build that new INIT chunk.  */
	reply = sctp_make_chunk(SCTP_INITIATION, 0,
				sizeof(initack)
				+ sizeof(bht)
				+ addrs_len);
	if (!reply)
		goto nomem;
	sctp_addto_chunk(reply, sizeof(initack), &initack);
	sctp_addto_chunk(reply, sizeof(bht), &bht);
	sctp_addto_chunk(reply, addrs_len, addrs);
	sctp_add_cmd_sf(commands, SCTP_CMD_REPLY, SCTP_CHUNK(reply));

	return SCTP_DISPOSITION_CONSUME;

nomem:
	return SCTP_DISPOSITION_NOMEM;
}
#endif /* 0 */

/*
 * Process an ABORT.
 *
 * Section: 9.1
 * After checking the Verification Tag, the receiving endpoint shall
 * remove the association from its record, and shall report the
 * termination to its upper layer.
 *
 * Verification Tag: 8.5.1 Exceptions in Verification Tag Rules
 * B) Rules for packet carrying ABORT:
 *
 *  - The endpoint shall always fill in the Verification Tag field of the
 *    outbound packet with the destination endpoint's tag value if it
 *    is known.
 *
 *  - If the ABORT is sent in response to an OOTB packet, the endpoint
 *    MUST follow the procedure described in Section 8.4.
 *
 *  - The receiver MUST accept the packet if the Verification Tag
 *    matches either its own tag, OR the tag of its peer. Otherwise, the
 *    receiver MUST silently discard the packet and take no further
 *    action.
 *
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_do_9_1_abort(const sctp_endpoint_t *ep,
					const sctp_association_t *asoc,
					const sctp_subtype_t type,
					void *arg,
					sctp_cmd_seq_t *commands)
{
	/* Check the verification tag.  */
	/* BUG: WRITE ME. */
	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_STATE,
			SCTP_STATE(SCTP_STATE_CLOSED));
	sctp_add_cmd_sf(commands, SCTP_CMD_DELETE_TCB, SCTP_NULL());

	/* BUG?  This does not look complete... */
	return SCTP_DISPOSITION_ABORT;
}

/*
 * Process an ABORT.  (COOKIE-WAIT state)
 *
 * See sctp_sf_do_9_1_abort() above.
 */
sctp_disposition_t sctp_sf_cookie_wait_abort(const sctp_endpoint_t *ep,
					     const sctp_association_t *asoc,
					     const sctp_subtype_t type,
					     void *arg,
					     sctp_cmd_seq_t *commands)
{
	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_STATE,
			SCTP_STATE(SCTP_STATE_CLOSED));
	sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_STOP,
			SCTP_TO(SCTP_EVENT_TIMEOUT_T1_INIT));

	/* CMD_INIT_FAILED will DELETE_TCB. */
	sctp_add_cmd_sf(commands,SCTP_CMD_INIT_FAILED, SCTP_NULL());

	return SCTP_DISPOSITION_DELETE_TCB;
}

/*
 * Process an ABORT.  (COOKIE-ECHOED state)
 *
 * See sctp_sf_do_9_1_abort() above.
 */
sctp_disposition_t sctp_sf_cookie_echoed_abort(const sctp_endpoint_t *ep,
					       const sctp_association_t *asoc,
					       const sctp_subtype_t type,
					       void *arg,
					       sctp_cmd_seq_t *commands)
{
	/* There is a single T1 timer, so we should be able to use
	 * common function with the COOKIE-WAIT state.
	 */        
	return sctp_sf_cookie_wait_abort(ep, asoc, type, arg, commands);
}

#if 0
/*
 * Handle a shutdown timeout or INIT during a shutdown phase.
 *
 * Section: 9.2
 * If an endpoint is in SHUTDOWN-ACK-SENT state and receives an INIT chunk
 * (e.g., if the SHUTDOWN COMPLETE was lost) with source and destination
 * transport addresses (either in the IP addresses or in the INIT chunk)
 * that belong to this association, it should discard the INIT chunk and
 * retransmit the SHUTDOWN ACK chunk.
 *...
 * While in SHUTDOWN-SENT state ... If the timer expires, the endpoint
 * must re-send the SHUTDOWN ACK.
 *
 * Verification Tag:  Neither the INIT nor the timeout will have a
 * valid verification tag, so it is safe to ignore.
 *
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_do_9_2_reshutack(const sctp_endpoint_t *ep,
					 const sctp_association_t *asoc,
					 const sctp_subtype_t type,
					 void *arg,
					 sctp_cmd_seq_t *commands)
{
	sctp_chunk_t *chunk = arg;

	/* If this was a timeout (not an INIT), then do the counter
	 * work.  We might need to just dump the association.
	 */
	if (!chunk) {
		if (1 + asoc->counters[SctpCounterRetran] >
		    asoc->maxRetrans) {
			sctp_add_cmd(commands, SCTP_CMD_DELETE_TCB,
				     SCTP_NULL());
			return SCTP_DISPOSITION_DELETE_TCB;
		}
		retval->counters[0] = SCTP_COUNTER_INCR;
		retval->counters[0] = SctpCounterRetran;
		retval->counters[1] = 0;
		retval->counters[1] = 0;
	}

	reply = sctp_make_shutdown_ack(asoc, chunk);
	if (!reply)
		goto nomem;

	sctp_add_cmd_sf(commands, SCTP_CMD_REPLY, SCTP_CHUNK(reply));
	return SCTP_DISPOSITION_CONSUME;

nomem:
	return SCTP_DISPOSITION_NOMEM;
}
#endif /* 0 */

/*
 * sctp_sf_do_9_2_shut
 *
 * Section: 9.2
 * Upon the reception of the SHUTDOWN, the peer endpoint shall
 *  - enter the SHUTDOWN-RECEIVED state,
 *
 *  - stop accepting new data from its SCTP user
 *
 *  - verify, by checking the Cumulative TSN Ack field of the chunk,
 *    that all its outstanding DATA chunks have been received by the
 *    SHUTDOWN sender.
 *
 * Once an endpoint as reached the SHUTDOWN-RECEIVED state it MUST NOT
 * send a SHUTDOWN in response to a ULP request. And should discard
 * subsequent SHUTDOWN chunks.
 *
 * If there are still outstanding DATA chunks left, the SHUTDOWN
 * receiver shall continue to follow normal data transmission
 * procedures defined in Section 6 until all outstanding DATA chunks
 * are acknowledged; however, the SHUTDOWN receiver MUST NOT accept
 * new data from its SCTP user.
 *
 * Verification Tag:  8.5 Verification Tag [Normal verification]
 *
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_do_9_2_shutdown(const sctp_endpoint_t *ep,
					   const sctp_association_t *asoc,
					   const sctp_subtype_t type,
					   void *arg,
					   sctp_cmd_seq_t *commands)
{
	sctp_chunk_t *chunk = arg;
	sctp_shutdownhdr_t *sdh;
	sctp_disposition_t disposition;

	/* Convert the elaborate header.  */
        sdh = (sctp_shutdownhdr_t *) chunk->skb->data;
	skb_pull(chunk->skb, sizeof(sctp_shutdownhdr_t));
	chunk->subh.shutdown_hdr = sdh;

	/* 8.5 When receiving an SCTP packet, the endpoint MUST ensure
	 * that the value in the Verification Tag field of the
	 * received SCTP packet matches its own Tag. If the received
	 * Verification Tag value does not match the receiver's own
	 * tag value, the receiver shall silently discard the packet...
	 */
	if (ntohl(chunk->sctp_hdr->vtag) != asoc->c.my_vtag)
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);

	/* Upon the reception of the SHUTDOWN, the peer endpoint shall
	 *  - enter the SHUTDOWN-RECEIVED state,
	 *  - stop accepting new data from its SCTP user
	 *
	 * [This is implicit in the new state.]
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_STATE,
			SCTP_STATE(SCTP_STATE_SHUTDOWN_RECEIVED));
	disposition = SCTP_DISPOSITION_CONSUME;

	if (sctp_outqueue_is_empty(&asoc->outqueue)) {
		disposition =
			sctp_sf_do_9_2_shutdown_ack(ep, asoc, type,
						    arg, commands);
	}

	/*  - verify, by checking the Cumulative TSN Ack field of the
	 *    chunk, that all its outstanding DATA chunks have been
	 *    received by the SHUTDOWN sender.
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_PROCESS_CTSN,
			SCTP_U32(chunk->subh.shutdown_hdr->cum_tsn_ack));
	return disposition;
}

/*
 * sctp_sf_do_ecn_cwr
 *
 * Section:  Appendix A: Explicit Congestion Notification
 *
 * CWR:
 *
 * RFC 2481 details a specific bit for a sender to send in the header of
 * its next outbound TCP segment to indicate to its peer that it has
 * reduced its congestion window.  This is termed the CWR bit.  For
 * SCTP the same indication is made by including the CWR chunk.
 * This chunk contains one data element, i.e. the TSN number that
 * was sent in the ECNE chunk.  This element represents the lowest
 * TSN number in the datagram that was originally marked with the
 * CE bit.
 *
 * Verification Tag: 8.5 Verification Tag [Normal verification]
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_do_ecn_cwr(const sctp_endpoint_t *ep,
				      const sctp_association_t *asoc,
				      const sctp_subtype_t type,
				      void *arg,
				      sctp_cmd_seq_t *commands)
{
	sctp_cwrhdr_t *cwr;
	sctp_chunk_t *chunk = arg;

	/* 8.5 When receiving an SCTP packet, the endpoint MUST ensure
	 * that the value in the Verification Tag field of the
	 * received SCTP packet matches its own Tag. If the received
	 * Verification Tag value does not match the receiver's own
	 * tag value, the receiver shall silently discard the packet...
	 */
	if (ntohl(chunk->sctp_hdr->vtag) != asoc->c.my_vtag)
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);

	cwr = (sctp_cwrhdr_t *) chunk->skb->data;
	skb_pull(chunk->skb, sizeof(sctp_cwrhdr_t));

	cwr->lowest_tsn = ntohl(cwr->lowest_tsn);

	/* Does this CWR ack the last sent congestion notification? */ 
	if (TSN_lte(asoc->last_ecne_tsn, cwr->lowest_tsn)) {
		/* Stop sending ECNE. */
		sctp_add_cmd_sf(commands,
				SCTP_CMD_ECN_CWR,
				SCTP_U32(cwr->lowest_tsn));
	}
	return SCTP_DISPOSITION_CONSUME;
}

/*
 * sctp_sf_do_ecne
 *
 * Section:  Appendix A: Explicit Congestion Notification
 *
 * ECN-Echo
 *
 * RFC 2481 details a specific bit for a receiver to send back in its
 * TCP acknowledgements to notify the sender of the Congestion
 * Experienced (CE) bit having arrived from the network.  For SCTP this
 * same indication is made by including the ECNE chunk.  This chunk
 * contains one data element, i.e. the lowest TSN associated with the IP
 * datagram marked with the CE bit.....
 *
 * Verification Tag: 8.5 Verification Tag [Normal verification]
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_do_ecne(const sctp_endpoint_t *ep,
				   const sctp_association_t *asoc,
				   const sctp_subtype_t type,
				   void *arg,
				   sctp_cmd_seq_t *commands)
{
	sctp_ecnehdr_t *ecne;
	sctp_chunk_t *chunk = arg;

	/* 8.5 When receiving an SCTP packet, the endpoint MUST ensure
	 * that the value in the Verification Tag field of the
	 * received SCTP packet matches its own Tag. If the received
	 * Verification Tag value does not match the receiver's own
	 * tag value, the receiver shall silently discard the packet...
	 */
	if (ntohl(chunk->sctp_hdr->vtag) != asoc->c.my_vtag)
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);

	ecne = (sctp_ecnehdr_t *) chunk->skb->data;
	skb_pull(chunk->skb, sizeof(sctp_ecnehdr_t));
	ecne->lowest_tsn = ntohl(ecne->lowest_tsn);

	/* Casting away the const, as we are just modifying the spinlock,
	 * not the association itself.   This should go away in the near
	 * future when we move to an endpoint based lock.
	 */

	/* If this is a newer ECNE than the last CWR packet we sent out */
	if (TSN_lt(asoc->last_cwr_tsn, ecne->lowest_tsn)) {
		sctp_add_cmd_sf(commands, SCTP_CMD_ECN_ECNE,
				SCTP_U32(ecne->lowest_tsn));
	}	
	return SCTP_DISPOSITION_CONSUME;
}

/*
 * Section: 6.2  Acknowledgement on Reception of DATA Chunks
 *
 * The SCTP endpoint MUST always acknowledge the reception of each valid
 * DATA chunk.
 *
 * The guidelines on delayed acknowledgement algorithm specified in
 * Section 4.2 of [RFC2581] SHOULD be followed. Specifically, an
 * acknowledgement SHOULD be generated for at least every second packet
 * (not every second DATA chunk) received, and SHOULD be generated within
 * 200 ms of the arrival of any unacknowledged DATA chunk. In some
 * situations it may be beneficial for an SCTP transmitter to be more
 * conservative than the algorithms detailed in this document allow.
 * However, an SCTP transmitter MUST NOT be more aggressive than the
 * following algorithms allow.
 *
 * A SCTP receiver MUST NOT generate more than one SACK for every
 * incoming packet, other than to update the offered window as the
 * receiving application consumes new data.
 *
 * Verification Tag:  8.5 Verification Tag [Normal verification]
 *
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_eat_data_6_2(const sctp_endpoint_t *ep,
					const sctp_association_t *asoc,
					const sctp_subtype_t type,
					void *arg,
					sctp_cmd_seq_t *commands)
{
	sctp_chunk_t *chunk = arg;
	sctp_datahdr_t *data_hdr;
	sctp_chunk_t *err;
	size_t datalen;
	int tmp;
	__u32 tsn;

	/* RFC 2960 8.5 Verification Tag
	 *
	 * When receiving an SCTP packet, the endpoint MUST ensure
	 * that the value in the Verification Tag field of the
	 * received SCTP packet matches its own Tag.
	 */

	if (ntohl(chunk->sctp_hdr->vtag) != asoc->c.my_vtag) {
		sctp_add_cmd_sf(commands, SCTP_CMD_REPORT_BAD_TAG,
				SCTP_NULL());
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);
        }

	data_hdr = chunk->subh.data_hdr = (sctp_datahdr_t *)chunk->skb->data;
	skb_pull(chunk->skb, sizeof(sctp_datahdr_t));

	tsn = ntohl(data_hdr->tsn);

	SCTP_DEBUG_PRINTK("eat_data: TSN 0x%x.\n", tsn);
	SCTP_DEBUG_PRINTK("eat_data: skb->head %p.\n", chunk->skb->head);

	/* ASSERT:  Now skb->data is really the user data.  */

	/* Process ECN based congestion.
	 *
	 * Since the chunk structure is reused for all chunks within
	 * a packet, we use ecn_ce_done to track if we've already
	 * done CE processing for this packet.
	 *
	 * We need to do ECN processing even if we plan to discard the
	 * chunk later.
	 */

	if (!chunk->ecn_ce_done) {
		chunk->ecn_ce_done = 1;
		if (INET_ECN_is_ce(chunk->skb->nh.iph->tos) &&
		    asoc->peer.ecn_capable) {
			/* Do real work as sideffect. */
			sctp_add_cmd_sf(commands, SCTP_CMD_ECN_CE,
					SCTP_U32(tsn));
		}
	}

	tmp = sctp_tsnmap_check(&asoc->peer.tsn_map, tsn);
	if (tmp < 0) {
		/* The TSN is too high--silently discard the chunk and
		 * count on it getting retransmitted later.
		 */
		goto discard_noforce;
	} else if (tmp > 0) {
		/* This is a duplicate.  Record it.  */
		sctp_add_cmd_sf(commands, SCTP_CMD_REPORT_DUP, SCTP_U32(tsn));
		goto discard_force;
	}

	/* This is a new TSN.  */

	/* If we don't have any room in our receive window, discard.
	 * Actually, allow a little bit of overflow (up to a MTU of
	 * of overflow).
	 */
	datalen = ntohs(chunk->chunk_hdr->length);
	datalen -= sizeof(sctp_data_chunk_t);

	if (!asoc->rwnd || (datalen > asoc->frag_point)) {
		SCTP_DEBUG_PRINTK("Discarding tsn: %u datalen: %Zd, "
				  "rwnd: %d\n", tsn, datalen, asoc->rwnd);
		goto discard_noforce;
	}

	/*
	 * Section 3.3.10.9 No User Data (9)
	 *
	 * Cause of error
	 * ---------------
	 * No User Data:  This error cause is returned to the originator of a
	 * DATA chunk if a received DATA chunk has no user data.
	 */
	if (unlikely(0 == datalen)) {
		err = sctp_make_abort_no_data(asoc, chunk, tsn);
		if (err) {
			sctp_add_cmd_sf(commands, SCTP_CMD_REPLY,
					SCTP_CHUNK(err));
		}
		/* We are going to ABORT, so we might as well stop
		 * processing the rest of the chunks in the packet.
		 */
		sctp_add_cmd_sf(commands, SCTP_CMD_DISCARD_PACKET,SCTP_NULL());
		sctp_add_cmd_sf(commands, SCTP_CMD_NEW_STATE,
				SCTP_STATE(SCTP_STATE_CLOSED));
		sctp_add_cmd_sf(commands, SCTP_CMD_DELETE_TCB, SCTP_NULL());
		return SCTP_DISPOSITION_CONSUME;
	}

	/* We are accepting this DATA chunk. */

	/* Record the fact that we have received this TSN.  */
	sctp_add_cmd_sf(commands, SCTP_CMD_REPORT_TSN, SCTP_U32(tsn));

	/* RFC 2960 6.5 Stream Identifier and Stream Sequence Number
	 *
	 * If an endpoint receive a DATA chunk with an invalid stream
	 * identifier, it shall acknowledge the reception of the DATA chunk
	 * following the normal procedure, immediately send an ERROR chunk
	 * with cause set to "Invalid Stream Identifier" (See Section 3.3.10)
	 * and discard the DATA chunk.
	 */
	if (ntohs(data_hdr->stream) >= asoc->c.sinit_max_instreams) {
		err = sctp_make_op_error(asoc, chunk, SCTP_ERROR_INV_STRM,
					 &data_hdr->stream,
					 sizeof(data_hdr->stream));
		if (err) {
			sctp_add_cmd_sf(commands, SCTP_CMD_REPLY,
					SCTP_CHUNK(err));
		}
		goto discard_noforce;
	}

	/* Send the data up to the user.  Note:  Schedule  the
	 * SCTP_CMD_CHUNK_ULP cmd before the SCTP_CMD_GEN_SACK, as the SACK
	 * chunk needs the updated rwnd.
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_CHUNK_ULP, SCTP_CHUNK(chunk));
	if (asoc->autoclose) {
		sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_RESTART,
				SCTP_TO(SCTP_EVENT_TIMEOUT_AUTOCLOSE));
	}

	/* If this is the last chunk in a packet, we need to count it
	 * toward sack generation.  Note that we need to SACK every
	 * OTHER packet containing data chunks, EVEN IF WE DISCARD
	 * THEM.  We elect to NOT generate SACK's if the chunk fails
	 * the verification tag test.
	 *
	 * RFC 2960 6.2 Acknowledgement on Reception of DATA Chunks
	 *
	 * The SCTP endpoint MUST always acknowledge the reception of
	 * each valid DATA chunk.
	 * 
	 * The guidelines on delayed acknowledgement algorithm
	 * specified in  Section 4.2 of [RFC2581] SHOULD be followed.
	 * Specifically, an acknowledgement SHOULD be generated for at
	 * least every second packet (not every second DATA chunk)
	 * received, and SHOULD be generated within 200 ms of the
	 * arrival of any unacknowledged DATA chunk.  In some
	 * situations it may be beneficial for an SCTP transmitter to
	 * be more conservative than the algorithms detailed in this
	 * document allow. However, an SCTP transmitter MUST NOT be
	 * more aggressive than the following algorithms allow.
	 */
	if (chunk->end_of_packet) {
		sctp_add_cmd_sf(commands, SCTP_CMD_GEN_SACK, SCTP_NOFORCE());

		/* Start the SACK timer.  */
		sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_RESTART,
				SCTP_TO(SCTP_EVENT_TIMEOUT_SACK));
	}

	return SCTP_DISPOSITION_CONSUME;

discard_force:
	/* RFC 2960 6.2 Acknowledgement on Reception of DATA Chunks
	 *
	 * When a packet arrives with duplicate DATA chunk(s) and with
	 * no new DATA chunk(s), the endpoint MUST immediately send a
	 * SACK with no delay.  If a packet arrives with duplicate
	 * DATA chunk(s) bundled with new DATA chunks, the endpoint
	 * MAY immediately send a SACK.  Normally receipt of duplicate
	 * DATA chunks will occur when the original SACK chunk was lost
	 * and the peer's RTO has expired.  The duplicate TSN number(s)
	 * SHOULD be reported in the SACK as duplicate.
	 */
	/* In our case, we split the MAY SACK advice up whether or not
	 * the last chunk is a duplicate.'
	 */
	if (chunk->end_of_packet)
		sctp_add_cmd_sf(commands, SCTP_CMD_GEN_SACK, SCTP_FORCE());
	return SCTP_DISPOSITION_DISCARD;

discard_noforce:
	if (chunk->end_of_packet) {
		sctp_add_cmd_sf(commands, SCTP_CMD_GEN_SACK, SCTP_NOFORCE());

		/* Start the SACK timer.  */
		sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_RESTART,
				SCTP_TO(SCTP_EVENT_TIMEOUT_SACK));
	}
	return SCTP_DISPOSITION_DISCARD;
}

/*
 * sctp_sf_eat_data_fast_4_4
 *
 * Section: 4 (4)
 * (4) In SHUTDOWN-SENT state the endpoint MUST acknowledge any received
 *    DATA chunks without delay.
 *
 * Verification Tag:  8.5 Verification Tag [Normal verification]
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_eat_data_fast_4_4(const sctp_endpoint_t *ep,
					     const sctp_association_t *asoc,
					     const sctp_subtype_t type,
					     void *arg,
					     sctp_cmd_seq_t *commands)
{
	sctp_chunk_t *chunk = arg;
	sctp_datahdr_t *data_hdr;
	sctp_chunk_t *err;
	size_t datalen;
	int tmp;
	__u32 tsn;

	/* RFC 2960 8.5 Verification Tag
	 *
	 * When receiving an SCTP packet, the endpoint MUST ensure
	 * that the value in the Verification Tag field of the
	 * received SCTP packet matches its own Tag.
	 */
	if (ntohl(chunk->sctp_hdr->vtag) != asoc->c.my_vtag) {
		sctp_add_cmd_sf(commands, SCTP_CMD_REPORT_BAD_TAG,
				SCTP_NULL());
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);
	}

	data_hdr = chunk->subh.data_hdr = (sctp_datahdr_t *) chunk->skb->data;
	skb_pull(chunk->skb, sizeof(sctp_datahdr_t));

	tsn = ntohl(data_hdr->tsn);

	SCTP_DEBUG_PRINTK("eat_data: TSN 0x%x.\n", tsn);

	/* ASSERT:  Now skb->data is really the user data.  */

	/* Process ECN based congestion.
	 *
	 * Since the chunk structure is reused for all chunks within
	 * a packet, we use ecn_ce_done to track if we've already
	 * done CE processing for this packet.
	 *
	 * We need to do ECN processing even if we plan to discard the
	 * chunk later.
	 */
	if (!chunk->ecn_ce_done) {
		chunk->ecn_ce_done = 1;
		if (INET_ECN_is_ce(chunk->skb->nh.iph->tos)  &&
		    asoc->peer.ecn_capable) {
			/* Do real work as sideffect. */
			sctp_add_cmd_sf(commands, SCTP_CMD_ECN_CE,
					SCTP_U32(tsn));
		}
	}

	tmp = sctp_tsnmap_check(&asoc->peer.tsn_map, tsn);
	if (tmp < 0) {
		/* The TSN is too high--silently discard the chunk and
		 * count on it getting retransmitted later.
		 */
		goto gen_shutdown;
	} else if (tmp > 0) {
		/* This is a duplicate.  Record it.  */
		sctp_add_cmd_sf(commands, SCTP_CMD_REPORT_DUP, SCTP_U32(tsn));
		goto gen_shutdown;
	}

	/* This is a new TSN.  */

	datalen = ntohs(chunk->chunk_hdr->length);
	datalen -= sizeof(sctp_data_chunk_t);

	/*
	 * Section 3.3.10.9 No User Data (9)
	 *
	 * Cause of error
	 * ---------------
	 * No User Data:  This error cause is returned to the originator of a
	 * DATA chunk if a received DATA chunk has no user data.
	 */
	if (unlikely(0 == datalen)) {
		err = sctp_make_abort_no_data(asoc, chunk, tsn);
		if (err) {
			sctp_add_cmd_sf(commands, SCTP_CMD_REPLY,
					SCTP_CHUNK(err));
		}
		/* We are going to ABORT, so we might as well stop
		 * processing the rest of the chunks in the packet.
		 */
		sctp_add_cmd_sf(commands, SCTP_CMD_DISCARD_PACKET,SCTP_NULL());
		sctp_add_cmd_sf(commands, SCTP_CMD_NEW_STATE,
				SCTP_STATE(SCTP_STATE_CLOSED));
		sctp_add_cmd_sf(commands, SCTP_CMD_DELETE_TCB, SCTP_NULL());
		return SCTP_DISPOSITION_CONSUME;
	}

	/* We are accepting this DATA chunk. */

	/* Record the fact that we have received this TSN.  */
	sctp_add_cmd_sf(commands, SCTP_CMD_REPORT_TSN, SCTP_U32(tsn));

	/* RFC 2960 6.5 Stream Identifier and Stream Sequence Number
	 *
	 * If an endpoint receive a DATA chunk with an invalid stream
	 * identifier, it shall acknowledge the reception of the DATA chunk
	 * following the normal procedure, immediately send an ERROR chunk
	 * with cause set to "Invalid Stream Identifier" (See Section 3.3.10)
	 * and discard the DATA chunk.
	 */
	if (ntohs(data_hdr->stream) >= asoc->c.sinit_max_instreams) {
		err = sctp_make_op_error(asoc, chunk, SCTP_ERROR_INV_STRM,
					 &data_hdr->stream,
					 sizeof(data_hdr->stream));
		if (err) {
			sctp_add_cmd_sf(commands, SCTP_CMD_REPLY,
					SCTP_CHUNK(err));
		}
	}

	/* Go a head and force a SACK, since we are shutting down. */
gen_shutdown:
	/* Implementor's Guide.
	 *
	 * While in SHUTDOWN-SENT state, the SHUTDOWN sender MUST immediately
	 * respond to each received packet containing one or more DATA chunk(s)
	 * with a SACK, a SHUTDOWN chunk, and restart the T2-shutdown timer
	 */
	if (chunk->end_of_packet) {
		/* We must delay the chunk creation since the cumulative
		 * TSN has not been updated yet.
		 */
		sctp_add_cmd_sf(commands, SCTP_CMD_GEN_SHUTDOWN, SCTP_NULL());
		sctp_add_cmd_sf(commands, SCTP_CMD_GEN_SACK, SCTP_FORCE());
		sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_RESTART,
				SCTP_TO(SCTP_EVENT_TIMEOUT_T2_SHUTDOWN));
	}
	return SCTP_DISPOSITION_CONSUME;
}

/*
 * Section: 6.2  Processing a Received SACK
 * D) Any time a SACK arrives, the endpoint performs the following:
 *
 *     i) If Cumulative TSN Ack is less than the Cumulative TSN Ack Point,
 *     then drop the SACK.   Since Cumulative TSN Ack is monotonically
 *     increasing, a SACK whose Cumulative TSN Ack is less than the
 *     Cumulative TSN Ack Point indicates an out-of-order SACK.
 *
 *     ii) Set rwnd equal to the newly received a_rwnd minus the number
 *     of bytes still outstanding after processing the Cumulative TSN Ack
 *     and the Gap Ack Blocks.
 *
 *     iii) If the SACK is missing a TSN that was previously
 *     acknowledged via a Gap Ack Block (e.g., the data receiver
 *     reneged on the data), then mark the corresponding DATA chunk
 *     as available for retransmit:  Mark it as missing for fast
 *     retransmit as described in Section 7.2.4 and if no retransmit
 *     timer is running for the destination address to which the DATA
 *     chunk was originally transmitted, then T3-rtx is started for
 *     that destination address.
 *
 * Verification Tag:  8.5 Verification Tag [Normal verification]
 *
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_eat_sack_6_2(const sctp_endpoint_t *ep,
					const sctp_association_t *asoc,
					const sctp_subtype_t type,
					void *arg,
					sctp_cmd_seq_t *commands)
{
	sctp_chunk_t *chunk = arg;
	sctp_sackhdr_t *sackh;
	__u32 ctsn;

	/* 8.5 When receiving an SCTP packet, the endpoint MUST ensure
	 * that the value in the Verification Tag field of the
	 * received SCTP packet matches its own Tag. ...
	 */
	if (ntohl(chunk->sctp_hdr->vtag) != asoc->c.my_vtag)
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);

	/* Pull the SACK chunk from the data buffer */
	sackh = sctp_sm_pull_sack(chunk);
	chunk->subh.sack_hdr = sackh;
	ctsn = ntohl(sackh->cum_tsn_ack);

	/* i) If Cumulative TSN Ack is less than the Cumulative TSN
	 *     Ack Point, then drop the SACK.  Since Cumulative TSN
	 *     Ack is monotonically increasing, a SACK whose
	 *     Cumulative TSN Ack is less than the Cumulative TSN Ack
	 *     Point indicates an out-of-order SACK.
	 */
	if (TSN_lt(ctsn, asoc->ctsn_ack_point)) {
		SCTP_DEBUG_PRINTK("ctsn %x\n", ctsn);
		SCTP_DEBUG_PRINTK("ctsn_ack_point %x\n",
				  asoc->ctsn_ack_point);
		return SCTP_DISPOSITION_DISCARD;
	}

	/* Return this SACK for further processing.  */
	sctp_add_cmd_sf(commands, SCTP_CMD_PROCESS_SACK,
			SCTP_SACKH(sackh));

	/* Note: We do the rest of the work on the PROCESS_SACK
	 * sideeffect.
	 */
	return SCTP_DISPOSITION_CONSUME;
}

/*
 * Generate an ABORT in response to a packet.
 *
 * Section: 8.4 Handle "Out of the blue" Packets
 *
 * 8) The receiver should respond to the sender of the OOTB packet
 *    with an ABORT.  When sending the ABORT, the receiver of the
 *    OOTB packet MUST fill in the Verification Tag field of the
 *    outbound packet with the value found in the Verification Tag
 *    field of the OOTB packet and set the T-bit in the Chunk Flags
 *    to indicate that no TCB was found.  After sending this ABORT,
 *    the receiver of the OOTB packet shall discard the OOTB packet
 *    and take no further action.
 *
 * Verification Tag:
 *
 * The return value is the disposition of the chunk.
*/
sctp_disposition_t sctp_sf_tabort_8_4_8(const sctp_endpoint_t *ep,
					const sctp_association_t *asoc,
					const sctp_subtype_t type,
					void *arg,
					sctp_cmd_seq_t *commands)
{
	sctp_packet_t *packet = NULL;
	sctp_transport_t *transport = NULL;
	sctp_chunk_t *chunk = arg;
	sctp_chunk_t *abort;
	__u16 sport;
	__u16 dport;
	__u32 vtag;

	/* Grub in chunk and endpoint for kewl bitz. */
	sport = ntohs(chunk->sctp_hdr->dest);
	dport = ntohs(chunk->sctp_hdr->source);
	/* -- Make sure the ABORT packet's V-tag is the same as the
	 *    inbound packet if no association exists, otherwise use
	 *    the peer's vtag.
	 */
	if (asoc)
		vtag = asoc->peer.i.init_tag;
	else
		vtag = ntohl(chunk->sctp_hdr->vtag);

	/* Make a transport for the bucket, Eliza... */
	transport = sctp_transport_new(sctp_source(chunk), GFP_ATOMIC);
	if (!transport)
		goto nomem;

	/* Make a packet for the ABORT to go into. */
	packet = t_new(sctp_packet_t, GFP_ATOMIC);
	if (!packet)
		goto nomem_packet;

	packet = sctp_packet_init(packet, transport, sport, dport);
	packet = sctp_packet_config(packet, vtag, 0, NULL);

	/* Make an ABORT.
	 * This will set the T bit since we have no association.
	 */
	abort = sctp_make_abort(NULL, chunk, 0);
	if (!abort)
		goto nomem_chunk;

	/* Set the skb to the belonging sock for accounting.  */
	abort->skb->sk = ep->base.sk;

	sctp_packet_append_chunk(packet, abort);
	sctp_add_cmd_sf(commands, SCTP_CMD_SEND_PKT, SCTP_PACKET(packet));
	return SCTP_DISPOSITION_DISCARD;

nomem_chunk:
	sctp_packet_free(packet);

nomem_packet:
	sctp_transport_free(transport);

nomem:
	return SCTP_DISPOSITION_NOMEM;
}

/*
 * Received an ERROR chunk from peer.  Generate SCTP_REMOTE_ERROR
 * event as ULP notification for each cause included in the chunk.
 *
 * API 5.3.1.3 - SCTP_REMOTE_ERROR
 *
 * The return value is the disposition of the chunk.
*/
sctp_disposition_t sctp_sf_operr_notify(const sctp_endpoint_t *ep,
					const sctp_association_t *asoc,
					const sctp_subtype_t type,
					void *arg,
					sctp_cmd_seq_t *commands)
{
	sctp_chunk_t *chunk = arg;
	sctp_ulpevent_t *ev;

	while (chunk->chunk_end > chunk->skb->data) {
		ev = sctp_ulpevent_make_remote_error(asoc,chunk,0, GFP_ATOMIC);
		if (!ev)
			goto nomem;

		if (!sctp_add_cmd(commands, SCTP_CMD_EVENT_ULP,
				  SCTP_ULPEVENT(ev))) {
			sctp_ulpevent_free(ev);
			goto nomem;
		}
	}
	return SCTP_DISPOSITION_CONSUME;

nomem:
	return SCTP_DISPOSITION_NOMEM;
}

/*
 * Process an inbound SHUTDOWN ACK.
 *
 * From Section 9.2:
 * Upon the receipt of the SHUTDOWN ACK, the SHUTDOWN sender shall
 * stop the T2-shutdown timer, send a SHUTDOWN COMPLETE chunk to its
 * peer, and remove all record of the association.
 *
 * The return value is the disposition.
 */
sctp_disposition_t sctp_sf_do_9_2_final(const sctp_endpoint_t *ep,
					const sctp_association_t *asoc,
					const sctp_subtype_t type,
					void *arg,
					sctp_cmd_seq_t *commands)
{
	sctp_chunk_t *chunk = arg;
	sctp_chunk_t *reply;
	sctp_ulpevent_t *ev;

	/* 10.2 H) SHUTDOWN COMPLETE notification
	 *
	 * When SCTP completes the shutdown procedures (section 9.2) this
	 * notification is passed to the upper layer.
	 */
	ev = sctp_ulpevent_make_assoc_change(asoc, 0, SCTP_SHUTDOWN_COMP,
					     0, 0, 0, GFP_ATOMIC);
	if (!ev)
		goto nomem;

	sctp_add_cmd_sf(commands, SCTP_CMD_EVENT_ULP, SCTP_ULPEVENT(ev));

	/* Upon the receipt of the SHUTDOWN ACK, the SHUTDOWN sender shall
	 * stop the T2-shutdown timer,
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_STOP,
			SCTP_TO(SCTP_EVENT_TIMEOUT_T2_SHUTDOWN));

	/* ...send a SHUTDOWN COMPLETE chunk to its peer, */
	reply = sctp_make_shutdown_complete(asoc, chunk);
	if (!reply)
		goto nomem;

	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_STATE,
			SCTP_STATE(SCTP_STATE_CLOSED));
	sctp_add_cmd_sf(commands, SCTP_CMD_REPLY, SCTP_CHUNK(reply));

	/* ...and remove all record of the association. */
	sctp_add_cmd_sf(commands, SCTP_CMD_DELETE_TCB, SCTP_NULL());
	return SCTP_DISPOSITION_DELETE_TCB;

nomem:
	return SCTP_DISPOSITION_NOMEM;
}

/*
 * RFC 2960, 8.4 - Handle "Out of the blue" Packets
 * 5) If the packet contains a SHUTDOWN ACK chunk, the receiver should
 *    respond to the sender of the OOTB packet with a SHUTDOWN COMPLETE.
 *    When sending the SHUTDOWN COMPLETE, the receiver of the OOTB
 *    packet must fill in the Verification Tag field of the outbound
 *    packet with the Verification Tag received in the SHUTDOWN ACK and
 *    set the T-bit in the Chunk Flags to indicate that no TCB was
 *    found. Otherwise,
 *
 * 8) The receiver should respond to the sender of the OOTB packet with
 *    an ABORT.  When sending the ABORT, the receiver of the OOTB packet
 *    MUST fill in the Verification Tag field of the outbound packet
 *    with the value found in the Verification Tag field of the OOTB
 *    packet and set the T-bit in the Chunk Flags to indicate that no
 *    TCB was found.  After sending this ABORT, the receiver of the OOTB
 *    packet shall discard the OOTB packet and take no further action.
 */
sctp_disposition_t sctp_sf_ootb(const sctp_endpoint_t *ep,
				const sctp_association_t *asoc,
				const sctp_subtype_t type,
				void *arg,
				sctp_cmd_seq_t *commands)
{
	sctp_chunk_t *chunk = arg;
	struct sk_buff *skb = chunk->skb;
	sctp_chunkhdr_t *ch;
	__u8 *ch_end;
	int ootb_shut_ack = 0;

	ch = (sctp_chunkhdr_t *) chunk->chunk_hdr;
	do {
		ch_end = ((__u8 *)ch) + WORD_ROUND(ntohs(ch->length));

		if (SCTP_CID_SHUTDOWN_ACK == ch->type)
			ootb_shut_ack = 1;

		ch = (sctp_chunkhdr_t *) ch_end;
	} while (ch_end < skb->tail);

	if (ootb_shut_ack)
		sctp_sf_shut_8_4_5(ep, asoc, type, arg, commands);
	else
	  	sctp_sf_tabort_8_4_8(ep, asoc, type, arg, commands);

	return sctp_sf_pdiscard(ep, asoc, type, arg, commands);
}

/*
 * Handle an "Out of the blue" SHUTDOWN ACK.
 *
 * Section: 8.4 5)
 * 5) If the packet contains a SHUTDOWN ACK chunk, the receiver should
 *   respond to the sender of the OOTB packet with a SHUTDOWN COMPLETE.
 *   When sending the SHUTDOWN COMPLETE, the receiver of the OOTB packet
 *   must fill in the Verification Tag field of the outbound packet with
 *   the Verification Tag received in the SHUTDOWN ACK and set the
 *   T-bit in the Chunk Flags to indicate that no TCB was found.
 *
 * Verification Tag:  8.5.1 E) Rules for packet carrying a SHUTDOWN ACK
 *   If the receiver is in COOKIE-ECHOED or COOKIE-WAIT state the
 *   procedures in section 8.4 SHOULD be followed, in other words it
 *   should be treated as an Out Of The Blue packet.
 *   [This means that we do NOT check the Verification Tag on these
 *   chunks. --piggy ]
 *
 * Inputs
 * (endpoint, asoc, type, arg, commands)
 *
 * Outputs
 * (sctp_disposition_t)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_shut_8_4_5(const sctp_endpoint_t *ep,
				      const sctp_association_t *asoc,
				      const sctp_subtype_t type,
				      void *arg,
				      sctp_cmd_seq_t *commands)
{
	sctp_packet_t *packet = NULL;
	sctp_transport_t *transport = NULL;
	sctp_chunk_t *chunk = arg;
	sctp_chunk_t *shut;
	__u16 sport;
	__u16 dport;
	__u32 vtag;

	/* Grub in chunk and endpoint for kewl bitz. */
	sport = ntohs(chunk->sctp_hdr->dest);
	dport = ntohs(chunk->sctp_hdr->source);

	/* Make sure the ABORT packet's V-tag is the same as the
	 * inbound packet if no association exists, otherwise use
	 * the peer's vtag.
	 */
	vtag = ntohl(chunk->sctp_hdr->vtag);

	/* Make a transport for the bucket, Eliza... */
	transport = sctp_transport_new(sctp_source(chunk), GFP_ATOMIC);
	if (!transport)
		goto nomem;

	/* Make a packet for the ABORT to go into. */
	packet = t_new(sctp_packet_t, GFP_ATOMIC);
	if (!packet)
		goto nomem_packet;

	packet = sctp_packet_init(packet, transport, sport, dport);
	packet = sctp_packet_config(packet, vtag, 0, NULL);

	/* Make an ABORT.
	 * This will set the T bit since we have no association.
	 */
	shut = sctp_make_shutdown_complete(NULL, chunk);
	if (!shut)
		goto nomem_chunk;

	/* Set the skb to the belonging sock for accounting.  */
	shut->skb->sk = ep->base.sk;

	sctp_packet_append_chunk(packet, shut);
	sctp_add_cmd_sf(commands, SCTP_CMD_SEND_PKT, SCTP_PACKET(packet));

	return SCTP_DISPOSITION_CONSUME;

nomem_chunk:
	sctp_packet_free(packet);

nomem_packet:
	sctp_transport_free(transport);

nomem:
	return SCTP_DISPOSITION_NOMEM;
}

#if 0
/*
 * We did something stupid but got lucky.  Namely, we sent a HEARTBEAT
 * before the association was all the way up and we did NOT get an
 * ABORT.
 *
 * Log the fact and then process normally.
 *
 * Section: Not specified
 * Verification Tag:  8.5 Verification Tag [Normal verification]
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t lucky(const sctp_endpoint_t *ep,
			 const sctp_association_t *asoc,
			 const sctp_subtype_t type,
			 void *arg,
			 sctp_cmd_seq_t *commands)
{
	sctp_chunk_t *chunk = arg;

	/* 8.5 When receiving an SCTP packet, the endpoint MUST ensure
	 * that the value in the Verification Tag field of the
	 * received SCTP packet matches its own Tag. ...
	 */
	if (chunk->sctp_hdr->vtag != asoc->c.my_vtag)
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);

	return SCTP_DISPOSITION_CONSUME;

nomem:
	return SCTP_DISPOSITION_NOMEM;
}
#endif /* 0 */

#if 0
/*
 * The other end is doing something very stupid.  We'll ignore them
 * after logging their idiocy. :-)
 *
 * Section: Not specified
 * Verification Tag:  8.5 Verification Tag [Normal verification]
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t other_stupid(const sctp_endpoint_t *ep,
				const sctp_association_t *asoc,
				const sctp_subtype_t type,
				void *arg,
				sctp_cmd_seq_t *commands)
{
	sctp_chunk_t *chunk = arg;

	/* 8.5 When receiving an SCTP packet, the endpoint MUST ensure
	 * that the value in the Verification Tag field of the
	 * received SCTP packet matches its own Tag. ...
	 */
	if (chunk->sctp_hdr->vtag != asoc->c.my_vtag)
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);

	return SCTP_DISPOSITION_CONSUME;

nomem:
	return SCTP_DISPOSITION_NOMEM;
}
#endif /* 0 */

/*
 * The other end is violating protocol.
 *
 * Section: Not specified
 * Verification Tag: Not specified
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * We simply tag the chunk as a violation.  The state machine will log
 * the violation and continue.
 */
sctp_disposition_t sctp_sf_violation(const sctp_endpoint_t *ep,
				     const sctp_association_t *asoc,
				     const sctp_subtype_t type,
				     void *arg,
				     sctp_cmd_seq_t *commands)
{
	return SCTP_DISPOSITION_VIOLATION;
}

/***************************************************************************
 * These are the state functions for handling primitive (Section 10) events.
 ***************************************************************************/
/*
 * sctp_sf_do_prm_asoc
 *
 * Section: 10.1 ULP-to-SCTP
 * B) Associate
 *
 * Format: ASSOCIATE(local SCTP instance name, destination transport addr,
 * outbound stream count)
 * -> association id [,destination transport addr list] [,outbound stream
 * count]
 *
 * This primitive allows the upper layer to initiate an association to a
 * specific peer endpoint.
 *
 * The peer endpoint shall be specified by one of the transport addresses
 * which defines the endpoint (see Section 1.4).  If the local SCTP
 * instance has not been initialized, the ASSOCIATE is considered an
 * error.
 * [This is not relevant for the kernel implementation since we do all
 * initialization at boot time.  It we hadn't initialized we wouldn't
 * get anywhere near this code.]
 *
 * An association id, which is a local handle to the SCTP association,
 * will be returned on successful establishment of the association. If
 * SCTP is not able to open an SCTP association with the peer endpoint,
 * an error is returned.
 * [In the kernel implementation, the sctp_association_t needs to
 * be created BEFORE causing this primitive to run.]
 *
 * Other association parameters may be returned, including the
 * complete destination transport addresses of the peer as well as the
 * outbound stream count of the local endpoint. One of the transport
 * address from the returned destination addresses will be selected by
 * the local endpoint as default primary path for sending SCTP packets
 * to this peer.  The returned "destination transport addr list" can
 * be used by the ULP to change the default primary path or to force
 * sending a packet to a specific transport address.  [All of this
 * stuff happens when the INIT ACK arrives.  This is a NON-BLOCKING
 * function.]
 *
 * Mandatory attributes:
 *
 * o local SCTP instance name - obtained from the INITIALIZE operation.
 *   [This is the argument asoc.]
 * o destination transport addr - specified as one of the transport
 * addresses of the peer endpoint with which the association is to be
 * established.
 *  [This is asoc->peer.active_path.]
 * o outbound stream count - the number of outbound streams the ULP
 * would like to open towards this peer endpoint.
 * [BUG: This is not currently implemented.]
 * Optional attributes:
 *
 * None.
 *
 * The return value is a disposition.
 */
sctp_disposition_t sctp_sf_do_prm_asoc(const sctp_endpoint_t *ep,
				       const sctp_association_t *asoc,
				       const sctp_subtype_t type,
				       void *arg,
				       sctp_cmd_seq_t *commands)
{
	sctp_chunk_t *repl;
	sctp_bind_addr_t *bp;
	sctp_scope_t scope;
	int error;
	int flags;

	/* The comment below says that we enter COOKIE-WAIT AFTER
	 * sending the INIT, but that doesn't actually work in our
	 * implementation...
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_STATE,
			SCTP_STATE(SCTP_STATE_COOKIE_WAIT));

	/* Build up the bind address list for the association based on
	 * info from the local endpoint and the remote peer.
	 */
	bp = sctp_bind_addr_new(GFP_ATOMIC);
	if (!bp)
		goto nomem;

	/* Use scoping rules to determine the subset of addresses from
	 * the endpoint.
	 */
	scope = sctp_scope(&asoc->peer.active_path->ipaddr);
	flags = (PF_INET6 == asoc->base.sk->family) ? SCTP_ADDR6_ALLOWED : 0;
	if (asoc->peer.ipv4_address)
		flags |= SCTP_ADDR4_PEERSUPP;
	if (asoc->peer.ipv6_address)
		flags |= SCTP_ADDR6_PEERSUPP;
	error = sctp_bind_addr_copy(bp, &ep->base.bind_addr, scope,
				    GFP_ATOMIC, flags);
	if (error)
		goto nomem;

	/* FIXME: Either move address assignment out of this function
	 * or else move the association allocation/init into this function.
	 * The association structure is brand new before calling this
	 * function, so would not be a sideeffect if the allocation
	 * moved into this function.  --jgrimm 
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_SET_BIND_ADDR, (sctp_arg_t) bp);

	/* RFC 2960 5.1 Normal Establishment of an Association
	 *
	 * A) "A" first sends an INIT chunk to "Z".  In the INIT, "A"
	 * must provide its Verification Tag (Tag_A) in the Initiate
	 * Tag field.  Tag_A SHOULD be a random number in the range of
	 * 1 to 4294967295 (see 5.3.1 for Tag value selection). ...
	 */

	repl = sctp_make_init(asoc, bp, GFP_ATOMIC);
	if (!repl)
		goto nomem;

	/* Cast away the const modifier, as we want to just
	 * rerun it through as a sideffect.
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_ASOC,
			SCTP_ASOC((sctp_association_t *) asoc));

	/* After sending the INIT, "A" starts the T1-init timer and
	 * enters the COOKIE-WAIT state.
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_START,
			SCTP_TO(SCTP_EVENT_TIMEOUT_T1_INIT));
	sctp_add_cmd_sf(commands, SCTP_CMD_REPLY, SCTP_CHUNK(repl));
	return SCTP_DISPOSITION_CONSUME;

nomem:
	if (bp)
		sctp_bind_addr_free(bp);

	return SCTP_DISPOSITION_NOMEM;
}

/*
 * Process the SEND primitive.
 *
 * Section: 10.1 ULP-to-SCTP
 * E) Send
 *
 * Format: SEND(association id, buffer address, byte count [,context]
 *         [,stream id] [,life time] [,destination transport address]
 *         [,unorder flag] [,no-bundle flag] [,payload protocol-id] )
 * -> result
 *
 * This is the main method to send user data via SCTP.
 *
 * Mandatory attributes:
 *
 *  o association id - local handle to the SCTP association
 *
 *  o buffer address - the location where the user message to be
 *    transmitted is stored;
 *
 *  o byte count - The size of the user data in number of bytes;
 *
 * Optional attributes:
 *
 *  o context - an optional 32 bit integer that will be carried in the
 *    sending failure notification to the ULP if the transportation of
 *    this User Message fails.
 *
 *  o stream id - to indicate which stream to send the data on. If not
 *    specified, stream 0 will be used.
 *
 *  o life time - specifies the life time of the user data. The user data
 *    will not be sent by SCTP after the life time expires. This
 *    parameter can be used to avoid efforts to transmit stale
 *    user messages. SCTP notifies the ULP if the data cannot be
 *    initiated to transport (i.e. sent to the destination via SCTP's
 *    send primitive) within the life time variable. However, the
 *    user data will be transmitted if SCTP has attempted to transmit a
 *    chunk before the life time expired.
 *
 *  o destination transport address - specified as one of the destination
 *    transport addresses of the peer endpoint to which this packet
 *    should be sent. Whenever possible, SCTP should use this destination
 *    transport address for sending the packets, instead of the current
 *    primary path.
 *
 *  o unorder flag - this flag, if present, indicates that the user
 *    would like the data delivered in an unordered fashion to the peer
 *    (i.e., the U flag is set to 1 on all DATA chunks carrying this
 *    message).
 *
 *  o no-bundle flag - instructs SCTP not to bundle this user data with
 *    other outbound DATA chunks. SCTP MAY still bundle even when
 *    this flag is present, when faced with network congestion.
 *
 *  o payload protocol-id - A 32 bit unsigned integer that is to be
 *    passed to the peer indicating the type of payload protocol data
 *    being transmitted. This value is passed as opaque data by SCTP.
 *
 * The return value is the disposition.
 */
sctp_disposition_t sctp_sf_do_prm_send(const sctp_endpoint_t *ep,
				       const sctp_association_t *asoc,
				       const sctp_subtype_t type,
				       void *arg,
				       sctp_cmd_seq_t *commands)
{
	sctp_chunk_t *chunk = arg;

	sctp_add_cmd_sf(commands, SCTP_CMD_REPLY, SCTP_CHUNK(chunk));
	return SCTP_DISPOSITION_CONSUME;
}

/*
 * Process the SHUTDOWN primitive.
 *
 * Section: 10.1:
 * C) Shutdown
 *
 * Format: SHUTDOWN(association id)
 * -> result
 *
 * Gracefully closes an association. Any locally queued user data
 * will be delivered to the peer. The association will be terminated only
 * after the peer acknowledges all the SCTP packets sent.  A success code
 * will be returned on successful termination of the association. If
 * attempting to terminate the association results in a failure, an error
 * code shall be returned.
 *
 * Mandatory attributes:
 *
 *  o association id - local handle to the SCTP association
 *
 * Optional attributes:
 *
 * None.
 *
 * The return value is the disposition.
 */
sctp_disposition_t sctp_sf_do_9_2_prm_shutdown(const sctp_endpoint_t *ep,
					       const sctp_association_t *asoc,
					       const sctp_subtype_t type,
					       void *arg,
					       sctp_cmd_seq_t *commands)
{
	int disposition;

	/* From 9.2 Shutdown of an Association
	 * Upon receipt of the SHUTDOWN primitive from its upper
	 * layer, the endpoint enters SHUTDOWN-PENDING state and
	 * remains there until all outstanding data has been
	 * acknowledged by its peer. The endpoint accepts no new data
	 * from its upper layer, but retransmits data to the far end
	 * if necessary to fill gaps.
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_STATE,
			SCTP_STATE(SCTP_STATE_SHUTDOWN_PENDING));        

	disposition = SCTP_DISPOSITION_CONSUME;
	if (sctp_outqueue_is_empty(&asoc->outqueue)) {
		disposition =
			sctp_sf_do_9_2_start_shutdown(ep, asoc, type,
						      arg, commands);
	}
	return disposition;
}

/*
 * Process the ABORT primitive.
 *
 * Section: 10.1:
 * C) Abort
 *
 * Format: Abort(association id [, cause code])
 * -> result
 *
 * Ungracefully closes an association. Any locally queued user data
 * will be discarded and an ABORT chunk is sent to the peer.  A success code
 * will be returned on successful abortion of the association. If
 * attempting to abort the association results in a failure, an error
 * code shall be returned.
 *
 * Mandatory attributes:
 *
 *  o association id - local handle to the SCTP association
 *
 * Optional attributes:
 *
 *  o cause code - reason of the abort to be passed to the peer
 *
 * None.
 *
 * The return value is the disposition.
 */
sctp_disposition_t sctp_sf_do_9_1_prm_abort(const sctp_endpoint_t *ep,
					    const sctp_association_t *asoc,
					    const sctp_subtype_t type,
					    void *arg,
					    sctp_cmd_seq_t *commands)
{
	/* From 9.1 Abort of an Association
	 * Upon receipt of the ABORT primitive from its upper
	 * layer, the endpoint enters CLOSED state and
	 * discard all outstanding data has been
	 * acknowledged by its peer. The endpoint accepts no new data
	 * from its upper layer, but retransmits data to the far end
	 * if necessary to fill gaps.
	 */
	sctp_chunk_t *abort;
	sctp_disposition_t retval;

	retval = SCTP_DISPOSITION_CONSUME;

	/* Generate ABORT chunk to send the peer.  */
	abort = sctp_make_abort(asoc, NULL, 0);
	if (!abort)
        	retval = SCTP_DISPOSITION_NOMEM;
	else
		sctp_add_cmd_sf(commands, SCTP_CMD_REPLY, SCTP_CHUNK(abort));

	/* Even if we can't send the ABORT due to low memory delete the
	 * TCB.  This is a departure from our typical NOMEM handling.
	 */

	/* Change to CLOSED state.  */
	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_STATE,
			SCTP_STATE(SCTP_STATE_CLOSED));

	/* Delete the established association.  */
	sctp_add_cmd_sf(commands, SCTP_CMD_DELETE_TCB, SCTP_NULL());
	return retval;
}

/* We tried an illegal operation on an association which is closed.  */
sctp_disposition_t sctp_sf_error_closed(const sctp_endpoint_t *ep,
					const sctp_association_t *asoc,
					const sctp_subtype_t type,
					void *arg,
					sctp_cmd_seq_t *commands)
{
	sctp_add_cmd_sf(commands, SCTP_CMD_REPORT_ERROR, SCTP_ERROR(-EINVAL));
	return SCTP_DISPOSITION_CONSUME;
}

/* We tried an illegal operation on an association which is shutting
 * down.
 */
sctp_disposition_t sctp_sf_error_shutdown(const sctp_endpoint_t *ep,
					  const sctp_association_t *asoc,
					  const sctp_subtype_t type,
					  void *arg,
					  sctp_cmd_seq_t *commands)
{
	sctp_add_cmd_sf(commands, SCTP_CMD_REPORT_ERROR,
			SCTP_ERROR(-ESHUTDOWN));
	return SCTP_DISPOSITION_CONSUME;
}

/*
 * sctp_cookie_wait_prm_shutdown
 *
 * Section: 4 Note: 2
 * Verification Tag:
 * Inputs
 * (endpoint, asoc)
 *
 * The RFC does not explicitly address this issue, but is the route through the
 * state table when someone issues a shutdown while in COOKIE_WAIT state.
 *
 * Outputs
 * (timers)
 */
sctp_disposition_t sctp_sf_cookie_wait_prm_shutdown(const sctp_endpoint_t *ep,
						    const sctp_association_t *asoc,
						    const sctp_subtype_t type,
						    void *arg,
						    sctp_cmd_seq_t *commands)
{
	sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_STOP,
			SCTP_TO(SCTP_EVENT_TIMEOUT_T1_INIT));

	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_STATE,
			SCTP_STATE(SCTP_STATE_CLOSED));

	sctp_add_cmd_sf(commands, SCTP_CMD_DELETE_TCB, SCTP_NULL());

	return SCTP_DISPOSITION_DELETE_TCB;
}

/*
 * sctp_cookie_echoed_prm_shutdown
 *
 * Section: 4 Note: 2
 * Verification Tag:
 * Inputs
 * (endpoint, asoc)
 *
 * The RFC does not explcitly address this issue, but is the route through the
 * state table when someone issues a shutdown while in COOKIE_ECHOED state.
 *
 * Outputs
 * (timers)
 */
sctp_disposition_t sctp_sf_cookie_echoed_prm_shutdown(
	const sctp_endpoint_t *ep,
	const sctp_association_t *asoc,
	const sctp_subtype_t type,
	void *arg, sctp_cmd_seq_t *commands)
{
	/* There is a single T1 timer, so we should be able to use
	 * common function with the COOKIE-WAIT state.
	 */
	return sctp_sf_cookie_wait_prm_shutdown(ep, asoc, type, arg, commands);
}

/*
 * sctp_cookie_wait_prm_abort
 *
 * Section: 4 Note: 2
 * Verification Tag:
 * Inputs
 * (endpoint, asoc)
 *
 * The RFC does not explicitly address this issue, but is the route through the
 * state table when someone issues an abort while in COOKIE_WAIT state.
 *
 * Outputs
 * (timers)
 */
sctp_disposition_t sctp_sf_cookie_wait_prm_abort(const sctp_endpoint_t *ep,
						 const sctp_association_t *asoc,
						 const sctp_subtype_t type,
						 void *arg,
						 sctp_cmd_seq_t *commands)
{
	/* Stop T1-init timer */
	sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_STOP,
			SCTP_TO(SCTP_EVENT_TIMEOUT_T1_INIT));
	return sctp_sf_do_9_1_prm_abort(ep, asoc, type, arg, commands);
}

/*
 * sctp_cookie_echoed_prm_abort
 *
 * Section: 4 Note: 3
 * Verification Tag:
 * Inputs
 * (endpoint, asoc)
 *
 * The RFC does not explcitly address this issue, but is the route through the
 * state table when someone issues an abort while in COOKIE_ECHOED state.
 *
 * Outputs
 * (timers)
 */
sctp_disposition_t sctp_sf_cookie_echoed_prm_abort(const sctp_endpoint_t *ep,
						   const sctp_association_t *asoc,
						   const sctp_subtype_t type,
						   void *arg,
						   sctp_cmd_seq_t *commands)
{
	/* There is a single T1 timer, so we should be able to use
	 * common function with the COOKIE-WAIT state.
	 */
	return sctp_sf_cookie_wait_prm_abort(ep, asoc, type, arg, commands);
}

/*
 * Ignore the primitive event
 *
 * The return value is the disposition of the primitive.
 */
sctp_disposition_t sctp_sf_ignore_primitive(const sctp_endpoint_t *ep,
					    const sctp_association_t *asoc,
					    const sctp_subtype_t type,
					    void *arg,
					    sctp_cmd_seq_t *commands)
{
	SCTP_DEBUG_PRINTK("Primitive type %d is ignored.\n", type.primitive);
	return SCTP_DISPOSITION_DISCARD;
}

/***************************************************************************
 * These are the state functions for the OTHER events.
 ***************************************************************************/

/*
 * Start the shutdown negotiation.
 *
 * From Section 9.2:
 * Once all its outstanding data has been acknowledged, the endpoint
 * shall send a SHUTDOWN chunk to its peer including in the Cumulative
 * TSN Ack field the last sequential TSN it has received from the peer.
 * It shall then start the T2-shutdown timer and enter the SHUTDOWN-SENT
 * state. If the timer expires, the endpoint must re-send the SHUTDOWN
 * with the updated last sequential TSN received from its peer.
 *
 * The return value is the disposition.
 */
sctp_disposition_t sctp_sf_do_9_2_start_shutdown(const sctp_endpoint_t *ep,
						 const sctp_association_t *asoc,
						 const sctp_subtype_t type,
						 void *arg,
						 sctp_cmd_seq_t *commands)
{
	sctp_chunk_t *reply;

	/* Once all its outstanding data has been acknowledged, the
	 * endpoint shall send a SHUTDOWN chunk to its peer including
	 * in the Cumulative TSN Ack field the last sequential TSN it
	 * has received from the peer.
	 */
	reply = sctp_make_shutdown(asoc);
	if (!reply)
		goto nomem;

	/* Set the transport for the SHUTDOWN chunk and the timeout for the
	 * T2-shutdown timer.
	 */ 
	sctp_add_cmd_sf(commands, SCTP_CMD_SETUP_T2, SCTP_CHUNK(reply));

	/* It shall then start the T2-shutdown timer */
	sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_START,
			SCTP_TO(SCTP_EVENT_TIMEOUT_T2_SHUTDOWN));

	if (asoc->autoclose)
		sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_STOP,
				SCTP_TO(SCTP_EVENT_TIMEOUT_AUTOCLOSE));

	/* and enter the SHUTDOWN-SENT state.  */
	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_STATE,
			SCTP_STATE(SCTP_STATE_SHUTDOWN_SENT));

	sctp_add_cmd_sf(commands, SCTP_CMD_REPLY, SCTP_CHUNK(reply));

	return SCTP_DISPOSITION_CONSUME;

nomem:
	return SCTP_DISPOSITION_NOMEM;
}

/*
 * Generate a SHUTDOWN ACK now that everything is SACK'd.
 *
 * From Section 9.2:
 *
 * If it has no more outstanding DATA chunks, the SHUTDOWN receiver
 * shall send a SHUTDOWN ACK and start a T2-shutdown timer of its own,
 * entering the SHUTDOWN-ACK-SENT state. If the timer expires, the
 * endpoint must re-send the SHUTDOWN ACK.
 *
 * The return value is the disposition.
 */
sctp_disposition_t sctp_sf_do_9_2_shutdown_ack(const sctp_endpoint_t *ep,
					       const sctp_association_t *asoc,
					       const sctp_subtype_t type,
					       void *arg,
					       sctp_cmd_seq_t *commands)
{
	sctp_chunk_t *chunk = (sctp_chunk_t *) arg;
	sctp_chunk_t *reply;

	/* If it has no more outstanding DATA chunks, the SHUTDOWN receiver
	 * shall send a SHUTDOWN ACK ...
	 */
	reply = sctp_make_shutdown_ack(asoc, chunk);
	if (!reply)
		goto nomem;

	/* Set the transport for the SHUTDOWN ACK chunk and the timeout for
	 * the T2-shutdown timer.
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_SETUP_T2, SCTP_CHUNK(reply));

	/* and start/restart a T2-shutdown timer of its own, */
	sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_RESTART,
			SCTP_TO(SCTP_EVENT_TIMEOUT_T2_SHUTDOWN));

	if (asoc->autoclose)
		sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_STOP,
				SCTP_TO(SCTP_EVENT_TIMEOUT_AUTOCLOSE));

	/* Enter the SHUTDOWN-ACK-SENT state.  */
	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_STATE,
			SCTP_STATE(SCTP_STATE_SHUTDOWN_ACK_SENT));
	sctp_add_cmd_sf(commands, SCTP_CMD_REPLY, SCTP_CHUNK(reply));

	return SCTP_DISPOSITION_CONSUME;

nomem:
	return SCTP_DISPOSITION_NOMEM;
}

/*
 * Ignore the event defined as other
 *
 * The return value is the disposition of the event.
 */
sctp_disposition_t sctp_sf_ignore_other(const sctp_endpoint_t *ep,
					const sctp_association_t *asoc,
					const sctp_subtype_t type,
					void *arg,
					sctp_cmd_seq_t *commands)
{
	SCTP_DEBUG_PRINTK("The event other type %d is ignored\n",
			  type.other);
	return SCTP_DISPOSITION_DISCARD;
}

/************************************************************
 * These are the state functions for handling timeout events.
 ************************************************************/

/*
 * RTX Timeout
 *
 * Section: 6.3.3 Handle T3-rtx Expiration
 *
 * Whenever the retransmission timer T3-rtx expires for a destination
 * address, do the following:
 * [See below]
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_do_6_3_3_rtx(const sctp_endpoint_t *ep,
					const sctp_association_t *asoc,
					const sctp_subtype_t type,
					void *arg,
					sctp_cmd_seq_t *commands)
{
	sctp_transport_t *transport = arg;

	if (asoc->overall_error_count >= asoc->overall_error_threshold) {
		/* CMD_ASSOC_FAILED calls CMD_DELETE_TCB. */
		sctp_add_cmd_sf(commands, SCTP_CMD_ASSOC_FAILED, SCTP_NULL());
		return SCTP_DISPOSITION_DELETE_TCB;
	}

	/* E1) For the destination address for which the timer
	 * expires, adjust its ssthresh with rules defined in Section
	 * 7.2.3 and set the cwnd <- MTU.
	 */

	/* E2) For the destination address for which the timer
	 * expires, set RTO <- RTO * 2 ("back off the timer").  The
	 * maximum value discussed in rule C7 above (RTO.max) may be
	 * used to provide an upper bound to this doubling operation.
	 */

	/* E3) Determine how many of the earliest (i.e., lowest TSN)
	 * outstanding DATA chunks for the address for which the
	 * T3-rtx has expired will fit into a single packet, subject
	 * to the MTU constraint for the path corresponding to the
	 * destination transport address to which the retransmission
	 * is being sent (this may be different from the address for
	 * which the timer expires [see Section 6.4]).  Call this
	 * value K. Bundle and retransmit those K DATA chunks in a
	 * single packet to the destination endpoint.
	 *
	 * Note: Any DATA chunks that were sent to the address for
	 * which the T3-rtx timer expired but did not fit in one MTU
	 * (rule E3 above), should be marked for retransmission and
	 * sent as soon as cwnd allows (normally when a SACK arrives).
	 */

	/* NB: Rules E4 and F1 are implicit in R1.  */
	sctp_add_cmd_sf(commands, SCTP_CMD_RETRAN, SCTP_TRANSPORT(transport));

	/* Do some failure management (Section 8.2). */
	sctp_add_cmd_sf(commands, SCTP_CMD_STRIKE, SCTP_TRANSPORT(transport));

	return SCTP_DISPOSITION_CONSUME;
}

/*
 * Generate delayed SACK on timeout
 *
 * Section: 6.2  Acknowledgement on Reception of DATA Chunks
 *
 * The guidelines on delayed acknowledgement algorithm specified in
 * Section 4.2 of [RFC2581] SHOULD be followed.  Specifically, an
 * acknowledgement SHOULD be generated for at least every second packet
 * (not every second DATA chunk) received, and SHOULD be generated
 * within 200 ms of the arrival of any unacknowledged DATA chunk.  In
 * some situations it may be beneficial for an SCTP transmitter to be
 * more conservative than the algorithms detailed in this document
 * allow. However, an SCTP transmitter MUST NOT be more aggressive than
 * the following algorithms allow.
 */
sctp_disposition_t sctp_sf_do_6_2_sack(const sctp_endpoint_t *ep,
				       const sctp_association_t *asoc,
				       const sctp_subtype_t type,
				       void *arg,
				       sctp_cmd_seq_t *commands)
{
        sctp_add_cmd_sf(commands, SCTP_CMD_GEN_SACK, SCTP_FORCE());
	return SCTP_DISPOSITION_CONSUME;
}

/*
 * sctp_sf_t1_timer_expire
 *
 * Section: 4 Note: 2
 * Verification Tag:
 * Inputs
 * (endpoint, asoc)
 *
 *  RFC 2960 Section 4 Notes
 *  2) If the T1-init timer expires, the endpoint MUST retransmit INIT
 *     and re-start the T1-init timer without changing state.  This MUST
 *     be repeated up to 'Max.Init.Retransmits' times.  After that, the
 *     endpoint MUST abort the initialization process and report the
 *     error to SCTP user.
 *
 *   3) If the T1-cookie timer expires, the endpoint MUST retransmit
 *     COOKIE ECHO and re-start the T1-cookie timer without changing
 *     state.  This MUST be repeated up to 'Max.Init.Retransmits' times.
 *     After that, the endpoint MUST abort the initialization process and
 *     report the error to SCTP user.
 *
 * Outputs
 * (timers, events)
 *
 */
sctp_disposition_t sctp_sf_t1_timer_expire(const sctp_endpoint_t *ep,
					   const sctp_association_t *asoc,
					   const sctp_subtype_t type,
					   void *arg,
					   sctp_cmd_seq_t *commands)
{
	sctp_chunk_t *repl;
	sctp_bind_addr_t *bp;
	sctp_event_timeout_t timer = (sctp_event_timeout_t) arg;
	int timeout;
	int attempts;

	timeout = asoc->timeouts[timer];
	attempts = asoc->counters[SCTP_COUNTER_INIT_ERROR] + 1;
	repl = NULL;

	SCTP_DEBUG_PRINTK("Timer T1 expired.\n");

	if ((timeout < asoc->max_init_timeo) &&
	    (attempts < asoc->max_init_attempts)) {
		switch (timer) {
		case SCTP_EVENT_TIMEOUT_T1_INIT:
			bp = (sctp_bind_addr_t *) &asoc->base.bind_addr;
			repl = sctp_make_init(asoc, bp, GFP_ATOMIC);
			break;

		case SCTP_EVENT_TIMEOUT_T1_COOKIE:
			repl = sctp_make_cookie_echo(asoc, NULL);
			break;

		default:
			BUG();
			break;
		};

		if (!repl)
			goto nomem;
		sctp_add_cmd_sf(commands, SCTP_CMD_REPLY, SCTP_CHUNK(repl));

		/* Issue a sideeffect to do the needed accounting. */
		sctp_add_cmd_sf(commands, SCTP_CMD_INIT_RESTART,
				SCTP_TO(timer));
	} else {
		sctp_add_cmd_sf(commands, SCTP_CMD_INIT_FAILED, SCTP_NULL());
		return SCTP_DISPOSITION_DELETE_TCB;
	}

	return SCTP_DISPOSITION_CONSUME;

nomem:
	return SCTP_DISPOSITION_NOMEM;
}

/* RFC2960 9.2 If the timer expires, the endpoint must re-send the SHUTDOWN
 * with the updated last sequential TSN received from its peer.
 *
 * An endpoint should limit the number of retransmissions of the
 * SHUTDOWN chunk to the protocol parameter 'Association.Max.Retrans'.
 * If this threshold is exceeded the endpoint should destroy the TCB and
 * MUST report the peer endpoint unreachable to the upper layer (and
 * thus the association enters the CLOSED state).  The reception of any
 * packet from its peer (i.e. as the peer sends all of its queued DATA
 * chunks) should clear the endpoint's retransmission count and restart
 * the T2-Shutdown timer,  giving its peer ample opportunity to transmit
 * all of its queued DATA chunks that have not yet been sent.
 */
sctp_disposition_t sctp_sf_t2_timer_expire(const sctp_endpoint_t *ep,
					   const sctp_association_t *asoc,
					   const sctp_subtype_t type,
					   void *arg,
					   sctp_cmd_seq_t *commands)
{
	sctp_chunk_t *reply = NULL;

	SCTP_DEBUG_PRINTK("Timer T2 expired.\n");
	if (asoc->overall_error_count >= asoc->overall_error_threshold) {
		/* Note:  CMD_ASSOC_FAILED calls CMD_DELETE_TCB. */
		sctp_add_cmd_sf(commands, SCTP_CMD_ASSOC_FAILED, SCTP_NULL());
		return SCTP_DISPOSITION_DELETE_TCB;
	}

	switch (asoc->state) {
	case SCTP_STATE_SHUTDOWN_SENT:
		reply = sctp_make_shutdown(asoc);
		break;

	case SCTP_STATE_SHUTDOWN_ACK_SENT:
		reply = sctp_make_shutdown_ack(asoc, NULL);
		break;

	default:
		BUG();
		break;
	};

	if (!reply)
		goto nomem;

	/* Do some failure management (Section 8.2). */
	sctp_add_cmd_sf(commands, SCTP_CMD_STRIKE,
			SCTP_TRANSPORT(asoc->shutdown_last_sent_to));

	/* Set the transport for the SHUTDOWN/ACK chunk and the timeout for
	 * the T2-shutdown timer.
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_SETUP_T2, SCTP_CHUNK(reply));

	/* Restart the T2-shutdown timer.  */
	sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_RESTART,
			SCTP_TO(SCTP_EVENT_TIMEOUT_T2_SHUTDOWN));
	sctp_add_cmd_sf(commands, SCTP_CMD_REPLY, SCTP_CHUNK(reply));
	return SCTP_DISPOSITION_CONSUME;

nomem:
	return SCTP_DISPOSITION_NOMEM;
}

/* Handle expiration of AUTOCLOSE timer.  When the autoclose timer expires,
 * the association is automatically closed by starting the shutdown process.
 * The work that needs to be done is same as when SHUTDOWN is initiated by
 * the user.  So this routine looks same as sctp_sf_do_9_2_prm_shutdown().
 */
sctp_disposition_t sctp_sf_autoclose_timer_expire(const sctp_endpoint_t *ep,
						  const sctp_association_t *asoc,
						  const sctp_subtype_t type,
						  void *arg,
						  sctp_cmd_seq_t *commands)
{
	int disposition;

	/* From 9.2 Shutdown of an Association
	 * Upon receipt of the SHUTDOWN primitive from its upper
	 * layer, the endpoint enters SHUTDOWN-PENDING state and
	 * remains there until all outstanding data has been
	 * acknowledged by its peer. The endpoint accepts no new data
	 * from its upper layer, but retransmits data to the far end
	 * if necessary to fill gaps.
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_STATE,
			SCTP_STATE(SCTP_STATE_SHUTDOWN_PENDING));

	disposition = SCTP_DISPOSITION_CONSUME;
	if (sctp_outqueue_is_empty(&asoc->outqueue)) {
		disposition =
			sctp_sf_do_9_2_start_shutdown(ep, asoc, type,
						      arg, commands);
	}
	return disposition;
}

/*****************************************************************************
 * These are sa state functions which could apply to all types of events.
 ****************************************************************************/

/*
 * This table entry is not implemented.
 *
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_not_impl(const sctp_endpoint_t *ep,
				    const sctp_association_t *asoc,
				    const sctp_subtype_t type,
				    void *arg,
				    sctp_cmd_seq_t *commands)
{
	return SCTP_DISPOSITION_NOT_IMPL;
}

/*
 * This table entry represents a bug.
 *
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_bug(const sctp_endpoint_t *ep,
			       const sctp_association_t *asoc,
			       const sctp_subtype_t type,
			       void *arg,
			       sctp_cmd_seq_t *commands)
{
	return SCTP_DISPOSITION_BUG;
}

/*
 * This table entry represents the firing of a timer in the wrong state.
 * Since timer deletion cannot be guaranteed a timer 'may' end up firing
 * when the association is in the wrong state.   This event should
 * be ignored, so as to prevent any rearming of the timer.
 *
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_timer_ignore(const sctp_endpoint_t *ep,
					const sctp_association_t *asoc,
					const sctp_subtype_t type,
					void *arg,
					sctp_cmd_seq_t *commands)
{
	SCTP_DEBUG_PRINTK("Timer %d ignored.\n", type.chunk);
	return SCTP_DISPOSITION_CONSUME;
}

/*
 * Discard the chunk.
 *
 * Section: 0.2, 5.2.3, 5.2.5, 5.2.6, 6.0, 8.4.6, 8.5.1c, 9.2
 * [Too numerous to mention...]
 * Verification Tag: No verification needed.
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_discard_chunk(const sctp_endpoint_t *ep,
					 const sctp_association_t *asoc,
					 const sctp_subtype_t type,
					 void *arg,
					 sctp_cmd_seq_t *commands)
{
	SCTP_DEBUG_PRINTK("Chunk %d is discarded\n", type.chunk);
	return SCTP_DISPOSITION_DISCARD;
}

/********************************************************************
 * 2nd Level Abstractions
 ********************************************************************/

/* Pull the SACK chunk based on the SACK header. */
sctp_sackhdr_t *sctp_sm_pull_sack(sctp_chunk_t *chunk)
{
	sctp_sackhdr_t *sack;
	__u16 num_blocks;
	__u16 num_dup_tsns;

	sack = (sctp_sackhdr_t *) chunk->skb->data;
	skb_pull(chunk->skb, sizeof(sctp_sackhdr_t));

	num_blocks = ntohs(sack->num_gap_ack_blocks);
	num_dup_tsns = ntohs(sack->num_dup_tsns);

	skb_pull(chunk->skb, (num_blocks + num_dup_tsns) * sizeof(__u32));
	return sack;
}
