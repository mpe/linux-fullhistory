/* SCTP kernel reference Implementation
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2001 International Business Machines, Corp.
 * Copyright (c) 2001 Intel Corp.
 * Copyright (c) 2001 Nokia, Inc.
 * Copyright (c) 2001 La Monte H.P. Yarroll
 * 
 * This file is part of the SCTP kernel reference Implementation
 * 
 * This abstraction represents an SCTP endpoint.   
 *
 * This file is part of the implementation of the add-IP extension,
 * based on <draft-ietf-tsvwg-addip-sctp-02.txt> June 29, 2001,
 * for the SCTP kernel reference Implementation.
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
 *    Karl Knutson <karl@athena.chicago.il.us>
 *    Jon Grimm <jgrimm@austin.ibm.com>
 *    Daisy Chang <daisyc@us.ibm.com>
 *    Dajiang Zhang <dajiang.zhang@nokia.com>
 * 
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/in.h>
#include <linux/random.h>	/* get_random_bytes() */
#include <net/sock.h>
#include <net/ipv6.h>
#include <net/sctp/sctp.h>
#include <net/sctp/sm.h>

/* Forward declarations for internal helpers. */
static void sctp_endpoint_bh_rcv(sctp_endpoint_t *ep);

/* Create a sctp_endpoint_t with all that boring stuff initialized.
 * Returns NULL if there isn't enough memory.
 */
sctp_endpoint_t *sctp_endpoint_new(sctp_protocol_t *proto,
				   struct sock *sk, int priority)
{
	sctp_endpoint_t *ep;

	/* Build a local endpoint. */
	ep = t_new(sctp_endpoint_t, priority);
	if (!ep)
		goto fail;
	if (!sctp_endpoint_init(ep, proto, sk, priority))
		goto fail_init;
	ep->base.malloced = 1;
	SCTP_DBG_OBJCNT_INC(ep);
	return ep;

fail_init:
	kfree(ep);
fail:
	return NULL;
}

/*
 * Initialize the base fields of the endpoint structure.
 */
sctp_endpoint_t *sctp_endpoint_init(sctp_endpoint_t *ep, sctp_protocol_t *proto,
				    struct sock *sk, int priority)
{
	memset(ep, 0, sizeof(sctp_endpoint_t));

	/* Initialize the base structure. */
	/* What type of endpoint are we?  */
	ep->base.type = SCTP_EP_TYPE_SOCKET;

        /* Initialize the basic object fields. */
	atomic_set(&ep->base.refcnt, 1);
	ep->base.dead     = 0;
	ep->base.malloced = 1;

	/* Create an input queue.  */
	sctp_inqueue_init(&ep->base.inqueue);

	/* Set its top-half handler */
	sctp_inqueue_set_th_handler(&ep->base.inqueue,
				    (void (*)(void *))sctp_endpoint_bh_rcv,
				    ep);

	/* Initialize the bind addr area */
	sctp_bind_addr_init(&ep->base.bind_addr, 0);
	ep->base.addr_lock = RW_LOCK_UNLOCKED;

	/* Remember who we are attached to.  */
	ep->base.sk = sk;
	sock_hold(ep->base.sk);

	/* This pointer is useful to access the default protocol parameter
	 * values.
	 */
	ep->proto = proto;

	/* Create the lists of associations.  */
	INIT_LIST_HEAD(&ep->asocs);

	/* Set up the base timeout information.  */
	ep->timeouts[SCTP_EVENT_TIMEOUT_NONE] = 0;
	ep->timeouts[SCTP_EVENT_TIMEOUT_T1_COOKIE]
		= SCTP_DEFAULT_TIMEOUT_T1_COOKIE;
	ep->timeouts[SCTP_EVENT_TIMEOUT_T1_INIT]
		= SCTP_DEFAULT_TIMEOUT_T1_INIT;
	ep->timeouts[SCTP_EVENT_TIMEOUT_T2_SHUTDOWN]
		= sctp_sk(sk)->rtoinfo.srto_initial;
	ep->timeouts[SCTP_EVENT_TIMEOUT_T3_RTX] = 0;
	ep->timeouts[SCTP_EVENT_TIMEOUT_T4_RTO] = 0;
	ep->timeouts[SCTP_EVENT_TIMEOUT_HEARTBEAT]
		= SCTP_DEFAULT_TIMEOUT_HEARTBEAT;
	ep->timeouts[SCTP_EVENT_TIMEOUT_SACK]
		= SCTP_DEFAULT_TIMEOUT_SACK;
	ep->timeouts[SCTP_EVENT_TIMEOUT_AUTOCLOSE]
		= sctp_sk(sk)->autoclose * HZ;
	ep->timeouts[SCTP_EVENT_TIMEOUT_PMTU_RAISE]
		= SCTP_DEFAULT_TIMEOUT_PMTU_RAISE;

	/* Set up the default send/receive buffer space.  */

	/* FIXME - Should the min and max window size be configurable
	 * sysctl parameters as opposed to be constants?
	 */
	sk->rcvbuf = SCTP_DEFAULT_MAXWINDOW;
	sk->sndbuf = SCTP_DEFAULT_MAXWINDOW * 2;

	/* Use SCTP specific send buffer space queues.  */
	sk->write_space = sctp_write_space;
	sk->use_write_queue = 1;

	/* Initialize the secret key used with cookie. */
	get_random_bytes(&ep->secret_key[0], SCTP_SECRET_SIZE);
	ep->last_key = ep->current_key = 0;
	ep->key_changed_at = jiffies;

	ep->debug_name = "unnamedEndpoint";
	return ep;
}

/* Add an association to an endpoint.  */
void sctp_endpoint_add_asoc(sctp_endpoint_t *ep, sctp_association_t *asoc)
{
	/* Now just add it to our list of asocs */
	list_add_tail(&asoc->asocs, &ep->asocs);
}

/* Free the endpoint structure.  Delay cleanup until
 * all users have released their reference count on this structure.
 */
void sctp_endpoint_free(sctp_endpoint_t *ep)
{
	ep->base.dead = 1;
	sctp_endpoint_put(ep);
}

/* Final destructor for endpoint.  */
void sctp_endpoint_destroy(sctp_endpoint_t *ep)
{
	SCTP_ASSERT(ep->base.dead, "Endpoint is not dead", return);

	/* Unlink this endpoint, so we can't find it again! */
	sctp_unhash_endpoint(ep);

	/* Cleanup the inqueue. */
	sctp_inqueue_free(&ep->base.inqueue);

	sctp_bind_addr_free(&ep->base.bind_addr);

	/* Remove and free the port */
	if (ep->base.sk->prev != NULL)
		sctp_put_port(ep->base.sk);

	/* Give up our hold on the sock. */
	if (ep->base.sk)
		sock_put(ep->base.sk);

	/* Finally, free up our memory. */
	if (ep->base.malloced) {
		kfree(ep);
		SCTP_DBG_OBJCNT_DEC(ep);
	}
}

/* Hold a reference to an endpoint. */
void sctp_endpoint_hold(sctp_endpoint_t *ep)
{
	atomic_inc(&ep->base.refcnt);
}

/* Release a reference to an endpoint and clean up if there are
 * no more references.
 */
void sctp_endpoint_put(sctp_endpoint_t *ep)
{
	if (atomic_dec_and_test(&ep->base.refcnt))
		sctp_endpoint_destroy(ep);
}

/* Is this the endpoint we are looking for?  */
sctp_endpoint_t *sctp_endpoint_is_match(sctp_endpoint_t *ep,
					const sockaddr_storage_t *laddr)
{
	sctp_endpoint_t *retval;

	sctp_read_lock(&ep->base.addr_lock);
	if (ep->base.bind_addr.port == laddr->v4.sin_port) {
		if (sctp_bind_addr_has_addr(&ep->base.bind_addr, laddr)) {
			retval = ep;
			goto out;
		}
	}

	retval = NULL;

out:
	sctp_read_unlock(&ep->base.addr_lock);
	return retval;
}

/* Find the association that goes with this chunk.
 * We do a linear search of the associations for this endpoint.
 * We return the matching transport address too.
 */
sctp_association_t *__sctp_endpoint_lookup_assoc(const sctp_endpoint_t *endpoint,
						 const sockaddr_storage_t *paddr,
						 sctp_transport_t **transport)
{
	int rport;
	sctp_association_t *asoc;
	list_t *pos;

	rport = paddr->v4.sin_port;

	list_for_each(pos, &endpoint->asocs) {
		asoc = list_entry(pos, sctp_association_t, asocs);
		if (rport == asoc->peer.port) {
			sctp_read_lock(&asoc->base.addr_lock);
			*transport = sctp_assoc_lookup_paddr(asoc, paddr);
			sctp_read_unlock(&asoc->base.addr_lock);

			if (*transport)
				return asoc;
		}
	}

	*transport = NULL;
	return NULL;
}

/* Lookup association on an endpoint based on a peer address.  BH-safe.  */
sctp_association_t *sctp_endpoint_lookup_assoc(const sctp_endpoint_t *ep,
					       const sockaddr_storage_t *paddr,
					       sctp_transport_t **transport)
{
	sctp_association_t *asoc;

	sctp_local_bh_disable();
	asoc = __sctp_endpoint_lookup_assoc(ep, paddr, transport);
	sctp_local_bh_enable();

	return asoc;
}

/* Do delayed input processing.  This is scheduled by sctp_rcv().
 * This may be called on BH or task time.
 */
static void sctp_endpoint_bh_rcv(sctp_endpoint_t *ep)
{
	sctp_association_t *asoc;
	struct sock *sk;
	sctp_transport_t *transport;
	sctp_chunk_t *chunk;
	sctp_inqueue_t *inqueue;
	sctp_subtype_t subtype;
	sctp_state_t state;
	int error = 0;

	if (ep->base.dead)
		goto out;

	asoc = NULL;
	inqueue = &ep->base.inqueue;
	sk      = ep->base.sk;

	while (NULL != (chunk = sctp_pop_inqueue(inqueue))) {
		subtype.chunk = chunk->chunk_hdr->type;

		/* We might have grown an association since last we
		 * looked, so try again.
		 *
		 * This happens when we've just processed our
		 * COOKIE-ECHO chunk.
		 */
		if (NULL == chunk->asoc) {
			asoc = sctp_endpoint_lookup_assoc(ep,
							  sctp_source(chunk),
							  &transport);
			chunk->asoc = asoc;
			chunk->transport = transport;
		}

		state = asoc ? asoc->state : SCTP_STATE_CLOSED;

		/* Remember where the last DATA chunk came from so we
		 * know where to send the SACK.
		 */
		if (asoc && sctp_chunk_is_data(chunk))
			asoc->peer.last_data_from = chunk->transport;

		if (chunk->transport)
			chunk->transport->last_time_heard = jiffies;

		/* FIX ME We really would rather NOT have to use
		 * GFP_ATOMIC.
		 */
                error = sctp_do_sm(SCTP_EVENT_T_CHUNK, subtype, state,
                                   ep, asoc, chunk, GFP_ATOMIC);

		if (error != 0)
			goto err_out;

		/* Check to see if the endpoint is freed in response to 
		 * the incoming chunk. If so, get out of the while loop.
		 */ 
		if (!sctp_sk(sk)->ep)
			goto out;
	}

err_out:
	/* Is this the right way to pass errors up to the ULP?  */
	if (error)
		ep->base.sk->err = -error;

out:
}




