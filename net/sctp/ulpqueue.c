/* SCTP kernel reference Implementation
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2001-2003 International Business Machines, Corp.
 * Copyright (c) 2001 Intel Corp.
 * Copyright (c) 2001 Nokia, Inc.
 * Copyright (c) 2001 La Monte H.P. Yarroll
 *
 * This abstraction carries sctp events to the ULP (sockets).
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
 *    Jon Grimm             <jgrimm@us.ibm.com>
 *    La Monte H.P. Yarroll <piggy@acm.org>
 *    Sridhar Samudrala     <sri@us.ibm.com>
 *
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */

#include <linux/types.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/sctp/structs.h>
#include <net/sctp/sctp.h>
#include <net/sctp/sm.h>

/* Forward declarations for internal helpers.  */
static inline struct sctp_ulpevent * sctp_ulpq_reasm(struct sctp_ulpq *ulpq,
						     struct sctp_ulpevent *);
static inline struct sctp_ulpevent *sctp_ulpq_order(struct sctp_ulpq *,
						    struct sctp_ulpevent *);

/* 1st Level Abstractions */

/* Create a new ULP queue.  */
struct sctp_ulpq *sctp_ulpq_new(sctp_association_t *asoc, int priority)
{
	struct sctp_ulpq *ulpq;

	ulpq = kmalloc(sizeof(struct sctp_ulpq), priority);
	if (!ulpq)
		goto fail;
	if (!sctp_ulpq_init(ulpq, asoc))
		goto fail_init;
	ulpq->malloced = 1;
	return ulpq;

fail_init:
	kfree(ulpq);
fail:
	return NULL;
}

/* Initialize a ULP queue from a block of memory.  */
struct sctp_ulpq *sctp_ulpq_init(struct sctp_ulpq *ulpq,
				 sctp_association_t *asoc)
{
	memset(ulpq, sizeof(struct sctp_ulpq), 0x00);

	ulpq->asoc = asoc;
	skb_queue_head_init(&ulpq->reasm);
	skb_queue_head_init(&ulpq->lobby);
	ulpq->malloced = 0;

	return ulpq;
}


/* Flush the reassembly and ordering queues.  */
void sctp_ulpq_flush(struct sctp_ulpq *ulpq)
{
	struct sk_buff *skb;
	struct sctp_ulpevent *event;

	while ((skb = skb_dequeue(&ulpq->lobby))) {
		event = (struct sctp_ulpevent *) skb->cb;
		sctp_ulpevent_free(event);
	}

	while ((skb = skb_dequeue(&ulpq->reasm))) {
		event = (struct sctp_ulpevent *) skb->cb;
		sctp_ulpevent_free(event);
	}
}

/* Dispose of a ulpqueue.  */
void sctp_ulpq_free(struct sctp_ulpq *ulpq)
{
	sctp_ulpq_flush(ulpq);
	if (ulpq->malloced)
		kfree(ulpq);
}

/* Process an incoming DATA chunk.  */
int sctp_ulpq_tail_data(struct sctp_ulpq *ulpq, sctp_chunk_t *chunk,
			    int priority)
{
	struct sk_buff_head temp;
	sctp_data_chunk_t *hdr;
	struct sctp_ulpevent *event;

	hdr = (sctp_data_chunk_t *) chunk->chunk_hdr;

	/* FIXME: Instead of event being the skb clone, we really should
	 * have a new skb based chunk structure that we can convert to
	 * an event.  Temporarily, I'm carrying a few chunk fields in
	 * the event to allow reassembly.  Its too painful to change
	 * everything at once.  --jgrimm
	 */
	event = sctp_ulpevent_make_rcvmsg(chunk->asoc, chunk, priority);
	if (!event)
		return -ENOMEM;

	/* Do reassembly if needed.  */
	event = sctp_ulpq_reasm(ulpq, event);

	/* Do ordering if needed.  */
	if (event) {
		/* Create a temporary list to collect chunks on.  */
		skb_queue_head_init(&temp);
		skb_queue_tail(&temp, event->parent);

		event = sctp_ulpq_order(ulpq, event);
	}

	/* Send event to the ULP.  */
	if (event)
		sctp_ulpq_tail_event(ulpq, event);

	return 0;
}

/* Add a new event for propagation to the ULP.  */
int sctp_ulpq_tail_event(struct sctp_ulpq *ulpq, struct sctp_ulpevent *event)
{
	struct sock *sk = ulpq->asoc->base.sk;

	/* If the socket is just going to throw this away, do not
	 * even try to deliver it.
	 */
	if (sk->dead || (sk->shutdown & RCV_SHUTDOWN))
		goto out_free;

	/* Check if the user wishes to receive this event.  */
	if (!sctp_ulpevent_is_enabled(event, &sctp_sk(sk)->subscribe))
		goto out_free;

	/* If we are harvesting multiple skbs they will be
	 * collected on a list.
	 */
	if (event->parent->list)
		sctp_skb_list_tail(event->parent->list, &sk->receive_queue);
	else
		skb_queue_tail(&sk->receive_queue, event->parent);

	wake_up_interruptible(sk->sleep);
	return 1;

out_free:
	if (event->parent->list)
		skb_queue_purge(event->parent->list);
	else
		kfree_skb(event->parent);
	return 0;
}

/* 2nd Level Abstractions */

/* Helper function to store chunks that need to be reassembled.  */
static inline void sctp_ulpq_store_reasm(struct sctp_ulpq *ulpq, 
					 struct sctp_ulpevent *event)
{
	struct sk_buff *pos, *tmp;
	struct sctp_ulpevent *cevent;
	__u32 tsn, ctsn;

	tsn = event->sndrcvinfo.sinfo_tsn;

	/* Find the right place in this list. We store them by TSN.  */
	sctp_skb_for_each(pos, &ulpq->reasm, tmp) {
		cevent = (struct sctp_ulpevent *)pos->cb;
		ctsn = cevent->sndrcvinfo.sinfo_tsn;

		if (TSN_lt(tsn, ctsn))
			break;
	}

	/* If the queue is empty, we have a different function to call.  */
	if (skb_peek(&ulpq->reasm))
		__skb_insert(event->parent, pos->prev, pos, &ulpq->reasm);
	else
		__skb_queue_tail(&ulpq->reasm, event->parent);
}

/* Helper function to return an event corresponding to the reassembled
 * datagram.
 * This routine creates a re-assembled skb given the first and last skb's
 * as stored in the reassembly queue. The skb's may be non-linear if the sctp
 * payload was fragmented on the way and ip had to reassemble them.
 * We add the rest of skb's to the first skb's fraglist.
 */
static inline struct sctp_ulpevent *sctp_make_reassembled_event(struct sk_buff *f_frag, struct sk_buff *l_frag)
{
	struct sk_buff *pos;
	struct sctp_ulpevent *event;
	struct sk_buff *pnext, *last;
	struct sk_buff *list = skb_shinfo(f_frag)->frag_list;

	/* Store the pointer to the 2nd skb */
	pos = f_frag->next;

	/* Get the last skb in the f_frag's frag_list if present. */
	for (last = list; list; last = list, list = list->next);

	/* Add the list of remaining fragments to the first fragments
	 * frag_list.
	 */
	if (last)
		last->next = pos;
	else
		skb_shinfo(f_frag)->frag_list = pos;

	/* Remove the first fragment from the reassembly queue.  */
	__skb_unlink(f_frag, f_frag->list);
	do {
		pnext = pos->next;

		/* Update the len and data_len fields of the first fragment. */
		f_frag->len += pos->len;
		f_frag->data_len += pos->len;

		/* Remove the fragment from the reassembly queue.  */
		__skb_unlink(pos, pos->list);

		/* Break if we have reached the last fragment.  */
		if (pos == l_frag)
			break;

		pos->next = pnext;
		pos = pnext;
	} while (1);

	event = (sctp_ulpevent_t *) f_frag->cb;

	return event;
}

/* Helper function to check if an incoming chunk has filled up the last
 * missing fragment in a SCTP datagram and return the corresponding event.
 */
static inline sctp_ulpevent_t *sctp_ulpq_retrieve_reassembled(struct sctp_ulpq *ulpq)
{
	struct sk_buff *pos, *tmp;
	sctp_ulpevent_t *cevent;
	struct sk_buff *first_frag = NULL;
	__u32 ctsn, next_tsn;
	sctp_ulpevent_t *retval = NULL;

	/* Initialized to 0 just to avoid compiler warning message. Will
	 * never be used with this value. It is referenced only after it
	 * is set when we find the first fragment of a message.
	 */
	next_tsn = 0;

	/* The chunks are held in the reasm queue sorted by TSN.
	 * Walk through the queue sequentially and look for a sequence of
	 * fragmented chunks that complete a datagram.
	 * 'first_frag' and next_tsn are reset when we find a chunk which
	 * is the first fragment of a datagram. Once these 2 fields are set
	 * we expect to find the remaining middle fragments and the last
	 * fragment in order. If not, first_frag is reset to NULL and we
	 * start the next pass when we find another first fragment.
	 */
	sctp_skb_for_each(pos, &ulpq->reasm, tmp) {
		cevent = (sctp_ulpevent_t *) pos->cb;
		ctsn = cevent->sndrcvinfo.sinfo_tsn;

		switch (cevent->chunk_flags & SCTP_DATA_FRAG_MASK) {
		case SCTP_DATA_FIRST_FRAG:
			first_frag = pos;
			next_tsn = ctsn + 1;
			break;

		case SCTP_DATA_MIDDLE_FRAG:
			if ((first_frag) && (ctsn == next_tsn))
				next_tsn++;
			else
				first_frag = NULL;
			break;

		case SCTP_DATA_LAST_FRAG:
			if ((first_frag) && (ctsn == next_tsn))
				retval = sctp_make_reassembled_event(
						first_frag, pos);
			else
				first_frag = NULL;
			break;
		};

		/* We have the reassembled event. There is no need to look
		 * further.
		 */
		if (retval)
			break;
	}

	return retval;
}

/* Helper function to reassemble chunks. Hold chunks on the reasm queue that
 * need reassembling.
 */
static inline sctp_ulpevent_t *sctp_ulpq_reasm(struct sctp_ulpq *ulpq,
						   sctp_ulpevent_t *event)
{
	sctp_ulpevent_t *retval = NULL;

	/* FIXME: We should be using some new chunk structure here
	 * instead of carrying chunk fields in the event structure.
	 * This is temporary as it is too painful to change everything
	 * at once.
	 */

	/* Check if this is part of a fragmented message.  */
	if (SCTP_DATA_NOT_FRAG == (event->chunk_flags & SCTP_DATA_FRAG_MASK))
		return event;

	sctp_ulpq_store_reasm(ulpq, event);
	retval = sctp_ulpq_retrieve_reassembled(ulpq);

	return retval;
}

/* Helper function to gather skbs that have possibly become
 * ordered by an an incoming chunk.
 */
static inline void sctp_ulpq_retrieve_ordered(struct sctp_ulpq *ulpq,
					      sctp_ulpevent_t *event)
{
	struct sk_buff *pos, *tmp;
	struct sctp_ulpevent *cevent;
	struct sctp_stream *in;
	__u16 sid, csid;
	__u16 ssn, cssn;

	sid = event->sndrcvinfo.sinfo_stream;
	ssn = event->sndrcvinfo.sinfo_ssn;
	in  = &ulpq->asoc->ssnmap->in;

	/* We are holding the chunks by stream, by SSN.  */
	sctp_skb_for_each(pos, &ulpq->lobby, tmp) {
		cevent = (sctp_ulpevent_t *) pos->cb;
		csid = cevent->sndrcvinfo.sinfo_stream;
		cssn = cevent->sndrcvinfo.sinfo_ssn;

		/* Have we gone too far?  */
		if (csid > sid)
			break;

		/* Have we not gone far enough?  */
		if (csid < sid)
			continue;

		if (cssn != sctp_ssn_peek(in, sid))
			break;

		/* Found it, so mark in the ssnmap. */
		sctp_ssn_next(in, sid);
	       
		__skb_unlink(pos, pos->list);

		/* Attach all gathered skbs to the event.  */
		__skb_queue_tail(event->parent->list, pos);
	}
}

/* Helper function to store chunks needing ordering.  */
static inline void sctp_ulpq_store_ordered(struct sctp_ulpq *ulpq,
					   sctp_ulpevent_t *event)
{
	struct sk_buff *pos, *tmp;
	sctp_ulpevent_t *cevent;
	__u16 sid, csid;
	__u16 ssn, cssn;

	sid = event->sndrcvinfo.sinfo_stream;
	ssn = event->sndrcvinfo.sinfo_ssn;


	/* Find the right place in this list.  We store them by
	 * stream ID and then by SSN.
	 */
	sctp_skb_for_each(pos, &ulpq->lobby, tmp) {
		cevent = (sctp_ulpevent_t *) pos->cb;
		csid = cevent->sndrcvinfo.sinfo_stream;
		cssn = cevent->sndrcvinfo.sinfo_ssn;

		if (csid > sid)
			break;
		if (csid == sid && SSN_lt(ssn, cssn))
			break;
	}

	/* If the queue is empty, we have a different function to call.  */
	if (skb_peek(&ulpq->lobby))
		__skb_insert(event->parent, pos->prev, pos, &ulpq->lobby);
	else
		__skb_queue_tail(&ulpq->lobby, event->parent);
}

static inline sctp_ulpevent_t *sctp_ulpq_order(struct sctp_ulpq *ulpq,
					       sctp_ulpevent_t *event)
{
	__u16 sid, ssn;
	struct sctp_stream *in;

	/* FIXME: We should be using some new chunk structure here
	 * instead of carrying chunk fields in the event structure.
	 * This is temporary as it is too painful to change everything
	 * at once.
	 */

	/* Check if this message needs ordering.  */
	if (SCTP_DATA_UNORDERED & event->chunk_flags)
		return event;

	/* Note: The stream ID must be verified before this routine.  */
	sid = event->sndrcvinfo.sinfo_stream;
	ssn = event->sndrcvinfo.sinfo_ssn;
	in  = &ulpq->asoc->ssnmap->in;

	/* Is this the expected SSN for this stream ID?  */
	if (ssn != sctp_ssn_peek(in, sid)) {
		/* We've received something out of order, so find where it
		 * needs to be placed.  We order by stream and then by SSN.
		 */
		sctp_ulpq_store_ordered(ulpq, event);
		return NULL;
	}

	/* Mark that the next chunk has been found.  */
	sctp_ssn_next(in, sid);

	/* Go find any other chunks that were waiting for
	 * ordering.
	 */
	sctp_ulpq_retrieve_ordered(ulpq, event);

	return event;
}
