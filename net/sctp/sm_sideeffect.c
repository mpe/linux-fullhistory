/* SCTP kernel reference Implementation
 * Copyright (c) 1999 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2001-2002 International Business Machines Corp.
 *
 * This file is part of the SCTP kernel reference Implementation
 *
 * These functions work with the state functions in sctp_sm_statefuns.c
 * to implement that state operations.  These functions implement the
 * steps which require modifying existing data structures.
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
 *    Jon Grimm             <jgrimm@austin.ibm.com>
 *    Hui Huang		    <hui.huang@nokia.com>
 *    Dajiang Zhang	    <dajiang.zhang@nokia.com>
 *    Daisy Chang	    <daisyc@us.ibm.com>
 *    Sridhar Samudrala	    <sri@us.ibm.com>
 *    Ardelle Fan	    <ardelle.fan@intel.com>
 *
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */

#include <linux/skbuff.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/ip.h>
#include <net/sock.h>
#include <net/sctp/sctp.h>
#include <net/sctp/sm.h>

/* Do forward declarations of static functions.  */
static void sctp_do_ecn_ce_work(sctp_association_t *,__u32 lowest_tsn);
static sctp_chunk_t *sctp_do_ecn_ecne_work(sctp_association_t *asoc,
					   __u32 lowest_tsn,
					   sctp_chunk_t *);
static void sctp_do_ecn_cwr_work(sctp_association_t *,__u32 lowest_tsn);
static void sctp_do_8_2_transport_strike(sctp_association_t *,
					 struct sctp_transport *);
static void sctp_cmd_init_failed(sctp_cmd_seq_t *, sctp_association_t *);
static void sctp_cmd_assoc_failed(sctp_cmd_seq_t *, sctp_association_t *,
				  sctp_event_t, sctp_subtype_t,
				  sctp_chunk_t *chunk);
static int sctp_cmd_process_init(sctp_cmd_seq_t *, sctp_association_t *,
				 sctp_chunk_t *chunk,
				 sctp_init_chunk_t *peer_init,
				 int priority);
static void sctp_cmd_hb_timers_start(sctp_cmd_seq_t *, sctp_association_t *);
static void sctp_cmd_hb_timers_stop(sctp_cmd_seq_t *, sctp_association_t *);
static void sctp_cmd_hb_timer_update(sctp_cmd_seq_t *, sctp_association_t *,
				     struct sctp_transport *);
static void sctp_cmd_transport_reset(sctp_cmd_seq_t *, sctp_association_t *,
				     struct sctp_transport *);
static void sctp_cmd_transport_on(sctp_cmd_seq_t *, sctp_association_t *,
				  struct sctp_transport *, sctp_chunk_t *);
static int sctp_cmd_process_sack(sctp_cmd_seq_t *, sctp_association_t *,
				 sctp_sackhdr_t *);
static void sctp_cmd_setup_t2(sctp_cmd_seq_t *, sctp_association_t *,
			      sctp_chunk_t *);
static void sctp_cmd_new_state(sctp_cmd_seq_t *, sctp_association_t *,
			       sctp_state_t);

/* These three macros allow us to pull the debugging code out of the
 * main flow of sctp_do_sm() to keep attention focused on the real
 * functionality there.
 */
#define DEBUG_PRE \
	SCTP_DEBUG_PRINTK("sctp_do_sm prefn: " \
			  "ep %p, %s, %s, asoc %p[%s], %s\n", \
			  ep, sctp_evttype_tbl[event_type], \
			  (*debug_fn)(subtype), asoc, \
			  sctp_state_tbl[state], state_fn->name)

#define DEBUG_POST \
	SCTP_DEBUG_PRINTK("sctp_do_sm postfn: " \
			  "asoc %p, status: %s\n", \
			  asoc, sctp_status_tbl[status])

#define DEBUG_POST_SFX \
	SCTP_DEBUG_PRINTK("sctp_do_sm post sfx: error %d, asoc %p[%s]\n", \
			  error, asoc, \
			  sctp_state_tbl[sctp_id2assoc(ep->base.sk, \
			  sctp_assoc2id(asoc))?asoc->state:SCTP_STATE_CLOSED])

/*
 * This is the master state machine processing function.
 *
 * If you want to understand all of lksctp, this is a
 * good place to start.
 */
int sctp_do_sm(sctp_event_t event_type, sctp_subtype_t subtype,
	       sctp_state_t state,
	       sctp_endpoint_t *ep,
	       sctp_association_t *asoc,
	       void *event_arg,
	       int priority)
{
	sctp_cmd_seq_t commands;
	sctp_sm_table_entry_t *state_fn;
	sctp_disposition_t status;
	int error = 0;
	typedef const char *(printfn_t)(sctp_subtype_t);

	static printfn_t *table[] = {
		NULL, sctp_cname, sctp_tname, sctp_oname, sctp_pname,
	};
	printfn_t *debug_fn  __attribute__ ((unused)) = table[event_type];

	/* Look up the state function, run it, and then process the
	 * side effects.  These three steps are the heart of lksctp.
	 */
	state_fn = sctp_sm_lookup_event(event_type, state, subtype);

	sctp_init_cmd_seq(&commands);

	DEBUG_PRE;
	status = (*state_fn->fn)(ep, asoc, subtype, event_arg, &commands);
	DEBUG_POST;

	error = sctp_side_effects(event_type, subtype, state,
				  ep, asoc, event_arg,
				  status, &commands,
				  priority);
	DEBUG_POST_SFX;

	return error;
}

#undef DEBUG_PRE
#undef DEBUG_POST

/*****************************************************************
 * This the master state function side effect processing function.
 *****************************************************************/
int sctp_side_effects(sctp_event_t event_type, sctp_subtype_t subtype,
		      sctp_state_t state,
		      sctp_endpoint_t *ep,
		      sctp_association_t *asoc,
		      void *event_arg,
		      sctp_disposition_t status,
		      sctp_cmd_seq_t *commands,
		      int priority)
{
	int error;

	/* FIXME - Most of the dispositions left today would be categorized
	 * as "exceptional" dispositions.  For those dispositions, it
	 * may not be proper to run through any of the commands at all.
	 * For example, the command interpreter might be run only with
	 * disposition SCTP_DISPOSITION_CONSUME.
	 */
	if (0 != (error = sctp_cmd_interpreter(event_type, subtype, state,
					       ep, asoc,
					       event_arg, status,
					       commands, priority)))
		goto bail;

	switch (status) {
	case SCTP_DISPOSITION_DISCARD:
		SCTP_DEBUG_PRINTK("Ignored sctp protocol event - state %d, "
				  "event_type %d, event_id %d\n",
				  state, event_type, subtype.chunk);
		break;

	case SCTP_DISPOSITION_NOMEM:
		/* We ran out of memory, so we need to discard this
		 * packet.
		 */
		/* BUG--we should now recover some memory, probably by
		 * reneging...
		 */
		error = -ENOMEM;
		break;

        case SCTP_DISPOSITION_DELETE_TCB:
		/* This should now be a command. */
		break;

	case SCTP_DISPOSITION_CONSUME:
	case SCTP_DISPOSITION_ABORT:
		/*
		 * We should no longer have much work to do here as the
		 * real work has been done as explicit commands above.
		 */
		break;

	case SCTP_DISPOSITION_VIOLATION:
		printk(KERN_ERR "sctp protocol violation state %d "
		       "chunkid %d\n", state, subtype.chunk);
		break;

	case SCTP_DISPOSITION_NOT_IMPL:
		printk(KERN_WARNING "sctp unimplemented feature in state %d, "
		       "event_type %d, event_id %d\n",
		       state, event_type, subtype.chunk);
		break;

	case SCTP_DISPOSITION_BUG:
		printk(KERN_ERR "sctp bug in state %d, "
		       "event_type %d, event_id %d\n",
		       state, event_type, subtype.chunk);
		BUG();
		break;

	default:
		printk(KERN_ERR "sctp impossible disposition %d "
		       "in state %d, event_type %d, event_id %d\n",
		       status, state, event_type, subtype.chunk);
		BUG();
		break;
	};

bail:
	return error;
}

/********************************************************************
 * 2nd Level Abstractions
 ********************************************************************/

/* This is the side-effect interpreter.  */
int sctp_cmd_interpreter(sctp_event_t event_type, sctp_subtype_t subtype,
			 sctp_state_t state, sctp_endpoint_t *ep,
			 sctp_association_t *asoc, void *event_arg,
			 sctp_disposition_t status, sctp_cmd_seq_t *commands,
			 int priority)
{
	int error = 0;
	int force;
	sctp_cmd_t *cmd;
	sctp_chunk_t *new_obj;
	sctp_chunk_t *chunk = NULL;
	sctp_packet_t *packet;
	struct list_head *pos;
	struct timer_list *timer;
	unsigned long timeout;
	struct sctp_transport *t;
	sctp_sackhdr_t sackh;

	if(SCTP_EVENT_T_TIMEOUT != event_type)
		chunk = (sctp_chunk_t *) event_arg;

	/* Note:  This whole file is a huge candidate for rework.
	 * For example, each command could either have its own handler, so
	 * the loop would look like:
	 *     while (cmds)
	 *         cmd->handle(x, y, z)
	 * --jgrimm
	 */
	while (NULL != (cmd = sctp_next_cmd(commands))) {
		switch (cmd->verb) {
		case SCTP_CMD_NOP:
			/* Do nothing. */
			break;

		case SCTP_CMD_NEW_ASOC:
			/* Register a new association.  */
			asoc = cmd->obj.ptr;
			/* Register with the endpoint.  */
			sctp_endpoint_add_asoc(ep, asoc);
			sctp_hash_established(asoc);
			break;

		case SCTP_CMD_UPDATE_ASSOC:
		       sctp_assoc_update(asoc, cmd->obj.ptr);
		       break;

		case SCTP_CMD_PURGE_OUTQUEUE:
		       sctp_outq_teardown(&asoc->outqueue);
		       break;

		case SCTP_CMD_DELETE_TCB:
			/* Delete the current association.  */
			sctp_unhash_established(asoc);
			sctp_association_free(asoc);
			asoc = NULL;
			break;

		case SCTP_CMD_NEW_STATE:
			/* Enter a new state.  */
			sctp_cmd_new_state(commands, asoc, cmd->obj.state);
			break;

		case SCTP_CMD_REPORT_TSN:
			/* Record the arrival of a TSN.  */
			sctp_tsnmap_mark(&asoc->peer.tsn_map, cmd->obj.u32);
			break;

		case SCTP_CMD_GEN_SACK:
			/* Generate a Selective ACK.
			 * The argument tells us whether to just count
			 * the packet and MAYBE generate a SACK, or
			 * force a SACK out.
			 */
			force = cmd->obj.i32;
			error = sctp_gen_sack(asoc, force, commands);
			break;

		case SCTP_CMD_PROCESS_SACK:
			/* Process an inbound SACK.  */
			error = sctp_cmd_process_sack(commands, asoc,
						      cmd->obj.ptr);
			break;

		case SCTP_CMD_GEN_INIT_ACK:
			/* Generate an INIT ACK chunk.  */
			new_obj = sctp_make_init_ack(asoc, chunk, GFP_ATOMIC,
						     0);
			if (!new_obj)
				goto nomem;

			sctp_add_cmd_sf(commands, SCTP_CMD_REPLY,
					SCTP_CHUNK(new_obj));
			break;

		case SCTP_CMD_PEER_INIT:
			/* Process a unified INIT from the peer.
			 * Note: Only used during INIT-ACK processing.  If
			 * there is an error just return to the outter
			 * layer which will bail.
			 */
			error = sctp_cmd_process_init(commands, asoc, chunk,
						      cmd->obj.ptr, priority);
			break;

		case SCTP_CMD_GEN_COOKIE_ECHO:
			/* Generate a COOKIE ECHO chunk.  */
			new_obj = sctp_make_cookie_echo(asoc, chunk);
			if (!new_obj) {
				if (cmd->obj.ptr)
					sctp_free_chunk(cmd->obj.ptr);
				goto nomem;
			}
			sctp_add_cmd_sf(commands, SCTP_CMD_REPLY,
					SCTP_CHUNK(new_obj));

			/* If there is an ERROR chunk to be sent along with
			 * the COOKIE_ECHO, send it, too.
			 */
			if (cmd->obj.ptr)
				sctp_add_cmd_sf(commands, SCTP_CMD_REPLY,
						SCTP_CHUNK(cmd->obj.ptr));
			break;

		case SCTP_CMD_GEN_SHUTDOWN:
			/* Generate SHUTDOWN when in SHUTDOWN_SENT state.
			 * Reset error counts.
			 */
			asoc->overall_error_count = 0;

			/* Generate a SHUTDOWN chunk.  */
			new_obj = sctp_make_shutdown(asoc);
			if (!new_obj)
				goto nomem;
			sctp_add_cmd_sf(commands, SCTP_CMD_REPLY,
					SCTP_CHUNK(new_obj));
			break;

		case SCTP_CMD_CHUNK_ULP:
			/* Send a chunk to the sockets layer.  */
			SCTP_DEBUG_PRINTK("sm_sideff: %s %p, %s %p.\n",
					  "chunk_up:", cmd->obj.ptr,
					  "ulpq:", &asoc->ulpq);
			sctp_ulpq_tail_data(&asoc->ulpq, cmd->obj.ptr,
					    GFP_ATOMIC);
			break;

		case SCTP_CMD_EVENT_ULP:
			/* Send a notification to the sockets layer.  */
			SCTP_DEBUG_PRINTK("sm_sideff: %s %p, %s %p.\n",
					  "event_up:",cmd->obj.ptr,
					  "ulpq:",&asoc->ulpq);
			sctp_ulpq_tail_event(&asoc->ulpq, cmd->obj.ptr);
			break;

		case SCTP_CMD_REPLY:
			/* Send a chunk to our peer.  */
			error = sctp_outq_tail(&asoc->outqueue,
					       cmd->obj.ptr);
			break;

		case SCTP_CMD_SEND_PKT:
			/* Send a full packet to our peer.  */
			packet = cmd->obj.ptr;
			sctp_packet_transmit(packet);
			sctp_ootb_pkt_free(packet);
			break;

		case SCTP_CMD_RETRAN:
			/* Mark a transport for retransmission.  */
			sctp_retransmit(&asoc->outqueue, cmd->obj.transport,
					SCTP_RETRANSMIT_T3_RTX);
			break;

		case SCTP_CMD_TRANSMIT:
			/* Kick start transmission. */
			error = sctp_outq_flush(&asoc->outqueue, 0);
			break;

		case SCTP_CMD_ECN_CE:
			/* Do delayed CE processing.   */
			sctp_do_ecn_ce_work(asoc, cmd->obj.u32);
			break;

		case SCTP_CMD_ECN_ECNE:
			/* Do delayed ECNE processing. */
			new_obj = sctp_do_ecn_ecne_work(asoc, cmd->obj.u32,
							chunk);
			if (new_obj)
				sctp_add_cmd_sf(commands, SCTP_CMD_REPLY,
						SCTP_CHUNK(new_obj));
			break;

		case SCTP_CMD_ECN_CWR:
			/* Do delayed CWR processing.  */
			sctp_do_ecn_cwr_work(asoc, cmd->obj.u32);
			break;

		case SCTP_CMD_SETUP_T2:
			sctp_cmd_setup_t2(commands, asoc, cmd->obj.ptr);
			break;

		case SCTP_CMD_TIMER_START:
			timer = &asoc->timers[cmd->obj.to];
			timeout = asoc->timeouts[cmd->obj.to];
			if (!timeout)
				BUG();

			timer->expires = jiffies + timeout;
			sctp_association_hold(asoc);
			add_timer(timer);
			break;

		case SCTP_CMD_TIMER_RESTART:
			timer = &asoc->timers[cmd->obj.to];
			timeout = asoc->timeouts[cmd->obj.to];
			if (!mod_timer(timer, jiffies + timeout))
				sctp_association_hold(asoc);
			break;

		case SCTP_CMD_TIMER_STOP:
			timer = &asoc->timers[cmd->obj.to];
			if (timer_pending(timer) && del_timer(timer))
				sctp_association_put(asoc);
			break;

		case SCTP_CMD_INIT_RESTART:
			/* Do the needed accounting and updates
			 * associated with restarting an initialization
			 * timer.
			 */
			asoc->counters[SCTP_COUNTER_INIT_ERROR]++;
			asoc->timeouts[cmd->obj.to] *= 2;
			if (asoc->timeouts[cmd->obj.to] >
			    asoc->max_init_timeo) {
				asoc->timeouts[cmd->obj.to] =
					asoc->max_init_timeo;
			}

			/* If we've sent any data bundled with
			 * COOKIE-ECHO we need to resend.
			 */
			list_for_each(pos, &asoc->peer.transport_addr_list) {
				t = list_entry(pos, struct sctp_transport,
					       transports);
				sctp_retransmit_mark(&asoc->outqueue, t, 0);
			}

			sctp_add_cmd_sf(commands,
					SCTP_CMD_TIMER_RESTART,
					SCTP_TO(cmd->obj.to));
			break;

		case SCTP_CMD_INIT_FAILED:
			sctp_cmd_init_failed(commands, asoc);
			break;

		case SCTP_CMD_ASSOC_FAILED:
			sctp_cmd_assoc_failed(commands, asoc, event_type,
					      subtype, chunk);
			break;

		case SCTP_CMD_COUNTER_INC:
			asoc->counters[cmd->obj.counter]++;
			break;

		case SCTP_CMD_COUNTER_RESET:
			asoc->counters[cmd->obj.counter] = 0;
			break;

		case SCTP_CMD_REPORT_DUP:
			sctp_tsnmap_mark_dup(&asoc->peer.tsn_map, 
					     cmd->obj.u32);
			break;

		case SCTP_CMD_REPORT_BAD_TAG:
			SCTP_DEBUG_PRINTK("vtag mismatch!\n");
			break;

		case SCTP_CMD_STRIKE:
			/* Mark one strike against a transport.  */
			sctp_do_8_2_transport_strike(asoc, cmd->obj.transport);
			break;

		case SCTP_CMD_TRANSPORT_RESET:
			t = cmd->obj.transport;
			sctp_cmd_transport_reset(commands, asoc, t);
			break;

		case SCTP_CMD_TRANSPORT_ON:
			t = cmd->obj.transport;
			sctp_cmd_transport_on(commands, asoc, t, chunk);
			break;

		case SCTP_CMD_HB_TIMERS_START:
			sctp_cmd_hb_timers_start(commands, asoc);
			break;

		case SCTP_CMD_HB_TIMER_UPDATE:
			t = cmd->obj.transport;
			sctp_cmd_hb_timer_update(commands, asoc, t);
			break;

		case SCTP_CMD_HB_TIMERS_STOP:
			sctp_cmd_hb_timers_stop(commands, asoc);
			break;

		case SCTP_CMD_REPORT_ERROR:
			error = cmd->obj.error;
			break;

		case SCTP_CMD_PROCESS_CTSN:
			/* Dummy up a SACK for processing. */
			sackh.cum_tsn_ack = cmd->obj.u32;
			sackh.a_rwnd = 0;
			sackh.num_gap_ack_blocks = 0;
			sackh.num_dup_tsns = 0;
			sctp_add_cmd_sf(commands, SCTP_CMD_PROCESS_SACK,
					SCTP_SACKH(&sackh));
			break;

		case SCTP_CMD_DISCARD_PACKET:
			/* We need to discard the whole packet.  */
			chunk->pdiscard = 1;
			break;

		case SCTP_CMD_RTO_PENDING:
			t = cmd->obj.transport;
			t->rto_pending = 1;
			break;

		case SCTP_CMD_PART_DELIVER:
			sctp_ulpq_partial_delivery(&asoc->ulpq, cmd->obj.ptr,
						   GFP_ATOMIC);
			break;

		case SCTP_CMD_RENEGE:
			sctp_ulpq_renege(&asoc->ulpq, cmd->obj.ptr,
					 GFP_ATOMIC);
			break;

		default:
			printk(KERN_WARNING "Impossible command: %u, %p\n",
			       cmd->verb, cmd->obj.ptr);
			break;
		};
		if (error)
			return error;
	}

	return error;

nomem:
	error = -ENOMEM;
	return error;
}

/* A helper function for delayed processing of INET ECN CE bit. */
static void sctp_do_ecn_ce_work(sctp_association_t *asoc, __u32 lowest_tsn)
{
	/* Save the TSN away for comparison when we receive CWR */

	asoc->last_ecne_tsn = lowest_tsn;
	asoc->need_ecne = 1;
}

/* Helper function for delayed processing of SCTP ECNE chunk.  */
/* RFC 2960 Appendix A
 *
 * RFC 2481 details a specific bit for a sender to send in
 * the header of its next outbound TCP segment to indicate to
 * its peer that it has reduced its congestion window.  This
 * is termed the CWR bit.  For SCTP the same indication is made
 * by including the CWR chunk.  This chunk contains one data
 * element, i.e. the TSN number that was sent in the ECNE chunk.
 * This element represents the lowest TSN number in the datagram
 * that was originally marked with the CE bit.
 */
static sctp_chunk_t *sctp_do_ecn_ecne_work(sctp_association_t *asoc,
					   __u32 lowest_tsn,
					   sctp_chunk_t *chunk)
{
	sctp_chunk_t *repl;

	/* Our previously transmitted packet ran into some congestion
	 * so we should take action by reducing cwnd and ssthresh
	 * and then ACK our peer that we we've done so by
	 * sending a CWR.
	 */

	/* First, try to determine if we want to actually lower
	 * our cwnd variables.  Only lower them if the ECNE looks more
	 * recent than the last response.
	 */
	if (TSN_lt(asoc->last_cwr_tsn, lowest_tsn)) {
		struct sctp_transport *transport;

		/* Find which transport's congestion variables
		 * need to be adjusted.
		 */
		transport = sctp_assoc_lookup_tsn(asoc, lowest_tsn);

		/* Update the congestion variables. */
		if (transport)
			sctp_transport_lower_cwnd(transport,
						  SCTP_LOWER_CWND_ECNE);
		asoc->last_cwr_tsn = lowest_tsn;
	}

	/* Always try to quiet the other end.  In case of lost CWR,
	 * resend last_cwr_tsn.
	 */
	repl = sctp_make_cwr(asoc, asoc->last_cwr_tsn, chunk);

	/* If we run out of memory, it will look like a lost CWR.  We'll
	 * get back in sync eventually.
	 */
	return repl;
}

/* Helper function to do delayed processing of ECN CWR chunk.  */
static void sctp_do_ecn_cwr_work(sctp_association_t *asoc,
				 __u32 lowest_tsn)
{
	/* Turn off ECNE getting auto-prepended to every outgoing
	 * packet
	 */
	asoc->need_ecne = 0;
}

/* This macro is to compress the text a bit...  */
#define AP(v) asoc->peer.v

/* Generate SACK if necessary.  We call this at the end of a packet.  */
int sctp_gen_sack(sctp_association_t *asoc, int force, sctp_cmd_seq_t *commands)
{
	__u32 ctsn, max_tsn_seen;
	sctp_chunk_t *sack;
	int error = 0;

	if (force)
		asoc->peer.sack_needed = 1;

	ctsn = sctp_tsnmap_get_ctsn(&asoc->peer.tsn_map);
	max_tsn_seen = sctp_tsnmap_get_max_tsn_seen(&asoc->peer.tsn_map);

	/* From 12.2 Parameters necessary per association (i.e. the TCB):
	 *
	 * Ack State : This flag indicates if the next received packet
	 * 	     : is to be responded to with a SACK. ...
	 *	     : When DATA chunks are out of order, SACK's
	 *           : are not delayed (see Section 6).
	 *
	 * [This is actually not mentioned in Section 6, but we
	 * implement it here anyway. --piggy]
	 */
        if (max_tsn_seen != ctsn)
		asoc->peer.sack_needed = 1;

	/* From 6.2  Acknowledgement on Reception of DATA Chunks:
	 *
	 * Section 4.2 of [RFC2581] SHOULD be followed. Specifically,
	 * an acknowledgement SHOULD be generated for at least every
	 * second packet (not every second DATA chunk) received, and
	 * SHOULD be generated within 200 ms of the arrival of any
	 * unacknowledged DATA chunk. ...
	 */
	if (!asoc->peer.sack_needed) {
		/* We will need a SACK for the next packet.  */
		asoc->peer.sack_needed = 1;
		goto out;
	} else {
		sack = sctp_make_sack(asoc);
		if (!sack)
			goto nomem;

		/* Update the last advertised rwnd value. */
		asoc->a_rwnd = asoc->rwnd;

		asoc->peer.sack_needed = 0;

		error = sctp_outq_tail(&asoc->outqueue, sack);

		/* Stop the SACK timer.  */
		sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_STOP,
				SCTP_TO(SCTP_EVENT_TIMEOUT_SACK));
	}

out:
	return error;

nomem:
	error = -ENOMEM;
	return error;
}

/* Handle a duplicate TSN.  */
void sctp_do_TSNdup(sctp_association_t *asoc, sctp_chunk_t *chunk, long gap)
{
#if 0
	sctp_chunk_t *sack;

	/* Caution:  gap < 2 * SCTP_TSN_MAP_SIZE
	 * 	so gap can be negative.
	 *
	 *		--xguo
	 */

	/* Count this TSN.  */
	if (gap < SCTP_TSN_MAP_SIZE) {
		asoc->peer.tsn_map[gap]++;
	} else {
		asoc->peer.tsn_map_overflow[gap - SCTP_TSN_MAP_SIZE]++;
	}

	/* From 6.2  Acknowledgement on Reception of DATA Chunks
	 *
	 * When a packet arrives with duplicate DATA chunk(s)
	 * and with no new DATA chunk(s), the endpoint MUST
	 * immediately send a SACK with no delay. If a packet
	 * arrives with duplicate DATA chunk(s) bundled with
	 * new DATA chunks, the endpoint MAY immediately send a
	 * SACK.  Normally receipt of duplicate DATA chunks
	 * will occur when the original SACK chunk was lost and
	 * the peer's RTO has expired. The duplicate TSN
	 * number(s) SHOULD be reported in the SACK as
	 * duplicate.
	 */
	asoc->counters[SctpCounterAckState] = 2;
#endif /* 0 */
} /* sctp_do_TSNdup() */

#undef AP

/* When the T3-RTX timer expires, it calls this function to create the
 * relevant state machine event.
 */
void sctp_generate_t3_rtx_event(unsigned long peer)
{
	int error;
	struct sctp_transport *transport = (struct sctp_transport *) peer;
	sctp_association_t *asoc = transport->asoc;

	/* Check whether a task is in the sock.  */

	sctp_bh_lock_sock(asoc->base.sk);
	if (sock_owned_by_user(asoc->base.sk)) {
		SCTP_DEBUG_PRINTK("%s:Sock is busy.\n", __FUNCTION__);

		/* Try again later.  */
		if (!mod_timer(&transport->T3_rtx_timer, jiffies + (HZ/20)))
			sctp_transport_hold(transport);
		goto out_unlock;
	}

	/* Is this transport really dead and just waiting around for
	 * the timer to let go of the reference?
	 */
	if (transport->dead)
		goto out_unlock;

	/* Run through the state machine.  */
	error = sctp_do_sm(SCTP_EVENT_T_TIMEOUT,
			   SCTP_ST_TIMEOUT(SCTP_EVENT_TIMEOUT_T3_RTX),
			   asoc->state,
			   asoc->ep, asoc,
			   transport, GFP_ATOMIC);

	if (error)
		asoc->base.sk->err = -error;

out_unlock:
	sctp_bh_unlock_sock(asoc->base.sk);
	sctp_transport_put(transport);
}

/* This is a sa interface for producing timeout events.  It works
 * for timeouts which use the association as their parameter.
 */
static void sctp_generate_timeout_event(sctp_association_t *asoc,
					sctp_event_timeout_t timeout_type)
{
	int error = 0;

	sctp_bh_lock_sock(asoc->base.sk);
	if (sock_owned_by_user(asoc->base.sk)) {
		SCTP_DEBUG_PRINTK("%s:Sock is busy: timer %d\n",
				  __FUNCTION__,
				  timeout_type);

		/* Try again later.  */
		if (!mod_timer(&asoc->timers[timeout_type], jiffies + (HZ/20)))
			sctp_association_hold(asoc);
		goto out_unlock;
	}

	/* Is this association really dead and just waiting around for
	 * the timer to let go of the reference?
	 */
	if (asoc->base.dead)
		goto out_unlock;

	/* Run through the state machine.  */
	error = sctp_do_sm(SCTP_EVENT_T_TIMEOUT,
			   SCTP_ST_TIMEOUT(timeout_type),
			   asoc->state, asoc->ep, asoc,
			   (void *)timeout_type,
			   GFP_ATOMIC);

	if (error)
		asoc->base.sk->err = -error;

out_unlock:
	sctp_bh_unlock_sock(asoc->base.sk);
	sctp_association_put(asoc);
}

void sctp_generate_t1_cookie_event(unsigned long data)
{
	sctp_association_t *asoc = (sctp_association_t *) data;
	sctp_generate_timeout_event(asoc, SCTP_EVENT_TIMEOUT_T1_COOKIE);
}

void sctp_generate_t1_init_event(unsigned long data)
{
	sctp_association_t *asoc = (sctp_association_t *) data;
	sctp_generate_timeout_event(asoc, SCTP_EVENT_TIMEOUT_T1_INIT);
}

void sctp_generate_t2_shutdown_event(unsigned long data)
{
	sctp_association_t *asoc = (sctp_association_t *) data;
	sctp_generate_timeout_event(asoc, SCTP_EVENT_TIMEOUT_T2_SHUTDOWN);
}

void sctp_generate_t5_shutdown_guard_event(unsigned long data)
{
        sctp_association_t *asoc = (sctp_association_t *)data;
        sctp_generate_timeout_event(asoc,
				    SCTP_EVENT_TIMEOUT_T5_SHUTDOWN_GUARD);

} /* sctp_generate_t5_shutdown_guard_event() */

void sctp_generate_autoclose_event(unsigned long data)
{
	sctp_association_t *asoc = (sctp_association_t *) data;
	sctp_generate_timeout_event(asoc, SCTP_EVENT_TIMEOUT_AUTOCLOSE);
}

/* Generate a heart beat event.  If the sock is busy, reschedule.   Make
 * sure that the transport is still valid.
 */
void sctp_generate_heartbeat_event(unsigned long data)
{
	int error = 0;
	struct sctp_transport *transport = (struct sctp_transport *) data;
	sctp_association_t *asoc = transport->asoc;

	sctp_bh_lock_sock(asoc->base.sk);
	if (sock_owned_by_user(asoc->base.sk)) {
		SCTP_DEBUG_PRINTK("%s:Sock is busy.\n", __FUNCTION__);

		/* Try again later.  */
		if (!mod_timer(&transport->hb_timer, jiffies + (HZ/20)))
			sctp_transport_hold(transport);
		goto out_unlock;
	}

	/* Is this structure just waiting around for us to actually
	 * get destroyed?
	 */
	if (transport->dead)
		goto out_unlock;

	error = sctp_do_sm(SCTP_EVENT_T_TIMEOUT,
			   SCTP_ST_TIMEOUT(SCTP_EVENT_TIMEOUT_HEARTBEAT),
			   asoc->state,
			   asoc->ep, asoc,
			   transport, GFP_ATOMIC);

         if (error)
		 asoc->base.sk->err = -error;

out_unlock:
	sctp_bh_unlock_sock(asoc->base.sk);
	sctp_transport_put(transport);
}

/* Inject a SACK Timeout event into the state machine.  */
void sctp_generate_sack_event(unsigned long data)
{
	sctp_association_t *asoc = (sctp_association_t *) data;
	sctp_generate_timeout_event(asoc, SCTP_EVENT_TIMEOUT_SACK);
}

sctp_timer_event_t *sctp_timer_events[SCTP_NUM_TIMEOUT_TYPES] = {
	NULL,
	sctp_generate_t1_cookie_event,
	sctp_generate_t1_init_event,
	sctp_generate_t2_shutdown_event,
	NULL,
	sctp_generate_t5_shutdown_guard_event,
	sctp_generate_heartbeat_event,
	sctp_generate_sack_event,
	sctp_generate_autoclose_event,
};

/********************************************************************
 * 3rd Level Abstractions
 ********************************************************************/

/* RFC 2960 8.2 Path Failure Detection
 *
 * When its peer endpoint is multi-homed, an endpoint should keep a
 * error counter for each of the destination transport addresses of the
 * peer endpoint.
 *
 * Each time the T3-rtx timer expires on any address, or when a
 * HEARTBEAT sent to an idle address is not acknowledged within a RTO,
 * the error counter of that destination address will be incremented.
 * When the value in the error counter exceeds the protocol parameter
 * 'Path.Max.Retrans' of that destination address, the endpoint should
 * mark the destination transport address as inactive, and a
 * notification SHOULD be sent to the upper layer.
 *
 */
static void sctp_do_8_2_transport_strike(sctp_association_t *asoc,
					 struct sctp_transport *transport)
{
	/* The check for association's overall error counter exceeding the
	 * threshold is done in the state function.
	 */
	asoc->overall_error_count++;

	if (transport->active &&
	    (transport->error_count++ >= transport->error_threshold)) {
		SCTP_DEBUG_PRINTK("transport_strike: transport "
				  "IP:%d.%d.%d.%d failed.\n",
				  NIPQUAD(transport->ipaddr.v4.sin_addr));
		sctp_assoc_control_transport(asoc, transport,
					     SCTP_TRANSPORT_DOWN,
					     SCTP_FAILED_THRESHOLD);
	}

	/* E2) For the destination address for which the timer
	 * expires, set RTO <- RTO * 2 ("back off the timer").  The
	 * maximum value discussed in rule C7 above (RTO.max) may be
	 * used to provide an upper bound to this doubling operation.
	 */
	transport->rto = min((transport->rto * 2), transport->asoc->rto_max);
}

/* Worker routine to handle INIT command failure.  */
static void sctp_cmd_init_failed(sctp_cmd_seq_t *commands,
				 sctp_association_t *asoc)
{
	struct sctp_ulpevent *event;

	event = sctp_ulpevent_make_assoc_change(asoc,
						0,
						SCTP_CANT_STR_ASSOC,
						0, 0, 0,
						GFP_ATOMIC);

	if (event)
		sctp_add_cmd_sf(commands, SCTP_CMD_EVENT_ULP,
				SCTP_ULPEVENT(event));

	/* FIXME:  We need to handle data possibly either
	 * sent via COOKIE-ECHO bundling or just waiting in
	 * the transmit queue, if the user has enabled
	 * SEND_FAILED notifications.
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_DELETE_TCB, SCTP_NULL());
}

/* Worker routine to handle SCTP_CMD_ASSOC_FAILED.  */
static void sctp_cmd_assoc_failed(sctp_cmd_seq_t *commands,
				  sctp_association_t *asoc,
				  sctp_event_t event_type,
				  sctp_subtype_t subtype,
				  sctp_chunk_t *chunk)
{
	struct sctp_ulpevent *event;
	__u16 error = 0;

	switch(event_type) {
	case SCTP_EVENT_T_PRIMITIVE:
		if (SCTP_PRIMITIVE_ABORT == subtype.primitive)
			error = SCTP_ERROR_USER_ABORT;
		break;
	case SCTP_EVENT_T_CHUNK:
		if (chunk && (SCTP_CID_ABORT == chunk->chunk_hdr->type) &&
	    	    (ntohs(chunk->chunk_hdr->length) >=
			(sizeof(struct sctp_chunkhdr) +
				sizeof(struct sctp_errhdr)))) {
			error = ((sctp_errhdr_t *)chunk->skb->data)->cause;
		}
		break;
	default:
		break;
	}

	/* Cancel any partial delivery in progress. */
	sctp_ulpq_abort_pd(&asoc->ulpq, GFP_ATOMIC);

	event = sctp_ulpevent_make_assoc_change(asoc, 0, SCTP_COMM_LOST,
						error, 0, 0, GFP_ATOMIC);
	if (event)
		sctp_add_cmd_sf(commands, SCTP_CMD_EVENT_ULP,
				SCTP_ULPEVENT(event));

	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_STATE,
			SCTP_STATE(SCTP_STATE_CLOSED));

	/* FIXME:  We need to handle data that could not be sent or was not
	 * acked, if the user has enabled SEND_FAILED notifications.
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_DELETE_TCB, SCTP_NULL());
}

/* Process an init chunk (may be real INIT/INIT-ACK or an embedded INIT
 * inside the cookie.  In reality, this is only used for INIT-ACK processing
 * since all other cases use "temporary" associations and can do all
 * their work in statefuns directly.
 */
static int sctp_cmd_process_init(sctp_cmd_seq_t *commands,
				 sctp_association_t *asoc,
				 sctp_chunk_t *chunk,
				 sctp_init_chunk_t *peer_init,
				 int priority)
{
	int error;

	/* We only process the init as a sideeffect in a single
	 * case.   This is when we process the INIT-ACK.   If we
	 * fail during INIT processing (due to malloc problems),
	 * just return the error and stop processing the stack.
	 */

	if (!sctp_process_init(asoc, chunk->chunk_hdr->type,
			       sctp_source(chunk), peer_init,
			       priority))
		error = -ENOMEM;
	else
		error = 0;

	return error;
}

/* Helper function to break out starting up of heartbeat timers.  */
static void sctp_cmd_hb_timers_start(sctp_cmd_seq_t *cmds,
				     sctp_association_t *asoc)
{
	struct sctp_transport *t;
	struct list_head *pos;

	/* Start a heartbeat timer for each transport on the association.
	 * hold a reference on the transport to make sure none of
	 * the needed data structures go away.
	 */
	list_for_each(pos, &asoc->peer.transport_addr_list) {
		t = list_entry(pos, struct sctp_transport, transports);

		if (!mod_timer(&t->hb_timer, sctp_transport_timeout(t)))
			sctp_transport_hold(t);
	}
}

static void sctp_cmd_hb_timers_stop(sctp_cmd_seq_t *cmds,
				    sctp_association_t *asoc)
{
	struct sctp_transport *t;
	struct list_head *pos;

	/* Stop all heartbeat timers. */

	list_for_each(pos, &asoc->peer.transport_addr_list) {
		t = list_entry(pos, struct sctp_transport, transports);
		if (del_timer(&t->hb_timer))
			sctp_transport_put(t);
	}
}

/* Helper function to update the heartbeat timer. */
static void sctp_cmd_hb_timer_update(sctp_cmd_seq_t *cmds,
				     sctp_association_t *asoc,
				     struct sctp_transport *t)
{
	/* Update the heartbeat timer.  */
	if (!mod_timer(&t->hb_timer, sctp_transport_timeout(t)))
		sctp_transport_hold(t);
}

/* Helper function to handle the reception of an HEARTBEAT ACK.  */
static void sctp_cmd_transport_on(sctp_cmd_seq_t *cmds,
				  sctp_association_t *asoc,
				  struct sctp_transport *t,
				  sctp_chunk_t *chunk)
{
	sctp_sender_hb_info_t *hbinfo;

	/* 8.3 Upon the receipt of the HEARTBEAT ACK, the sender of the
	 * HEARTBEAT should clear the error counter of the destination
	 * transport address to which the HEARTBEAT was sent.
	 * The association's overall error count is also cleared.
	 */
	t->error_count = 0;
	t->asoc->overall_error_count = 0;

	/* Mark the destination transport address as active if it is not so
	 * marked.
	 */
	if (!t->active)
		sctp_assoc_control_transport(asoc, t, SCTP_TRANSPORT_UP,
					     SCTP_HEARTBEAT_SUCCESS);

	/* The receiver of the HEARTBEAT ACK should also perform an
	 * RTT measurement for that destination transport address
	 * using the time value carried in the HEARTBEAT ACK chunk.
	 */
	hbinfo = (sctp_sender_hb_info_t *) chunk->skb->data;
	sctp_transport_update_rto(t, (jiffies - hbinfo->sent_at));
}

/* Helper function to do a transport reset at the expiry of the hearbeat
 * timer.
 */
static void sctp_cmd_transport_reset(sctp_cmd_seq_t *cmds,
				     sctp_association_t *asoc,
				     struct sctp_transport *t)
{
	sctp_transport_lower_cwnd(t, SCTP_LOWER_CWND_INACTIVE);

	/* Mark one strike against a transport.  */
	sctp_do_8_2_transport_strike(asoc, t);
}

/* Helper function to process the process SACK command.  */
static int sctp_cmd_process_sack(sctp_cmd_seq_t *cmds,
				 sctp_association_t *asoc,
				 sctp_sackhdr_t *sackh)
{
	int err;

	if (sctp_outq_sack(&asoc->outqueue, sackh)) {
		/* There are no more TSNs awaiting SACK.  */
		err = sctp_do_sm(SCTP_EVENT_T_OTHER,
				 SCTP_ST_OTHER(SCTP_EVENT_NO_PENDING_TSN),
				 asoc->state, asoc->ep, asoc, NULL,
				 GFP_ATOMIC);
	} else {
		/* Windows may have opened, so we need
		 * to check if we have DATA to transmit
		 */
		err = sctp_outq_flush(&asoc->outqueue, 0);
	}

	return err;
}

/* Helper function to set the timeout value for T2-SHUTDOWN timer and to set
 * the transport for a shutdown chunk.
 */
static void sctp_cmd_setup_t2(sctp_cmd_seq_t *cmds, sctp_association_t *asoc,
			      sctp_chunk_t *chunk)
{
	struct sctp_transport *t;

	t = sctp_assoc_choose_shutdown_transport(asoc);
	asoc->shutdown_last_sent_to = t;
	asoc->timeouts[SCTP_EVENT_TIMEOUT_T2_SHUTDOWN] = t->rto;
	chunk->transport = t;
}

/* Helper function to change the state of an association. */
static void sctp_cmd_new_state(sctp_cmd_seq_t *cmds, sctp_association_t *asoc,
			       sctp_state_t state)
{
	asoc->state = state;
	asoc->state_timestamp = jiffies;

	/* Wake up any process waiting for the association to
	 * get established.
	 */
	if ((SCTP_STATE_ESTABLISHED == asoc->state) &&
	    (waitqueue_active(&asoc->wait)))
		wake_up_interruptible(&asoc->wait);
}
