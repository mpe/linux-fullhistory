/* SCTP kernel reference Implementation
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2002 International Business Machines, Corp.
 * 
 * This file is part of the SCTP kernel reference Implementation
 * 
 * These functions are the methods for accessing the SCTP inqueue.
 *
 * An SCTP inqueue is a queue into which you push SCTP packets
 * (which might be bundles or fragments of chunks) and out of which you
 * pop SCTP whole chunks.
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
 * 
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */

#include <net/sctp/sctp.h>
#include <net/sctp/sm.h>
#include <linux/interrupt.h>

/* Initialize an SCTP_inqueue.  */
void sctp_inqueue_init(sctp_inqueue_t *queue)
{
	skb_queue_head_init(&queue->in);
	queue->in_progress = NULL;

	/* Create a task for delivering data.  */
	INIT_LIST_HEAD(&queue->immediate.list);
	queue->immediate.sync = 0;
	queue->immediate.routine = NULL;
	queue->immediate.data = NULL;

	queue->malloced = 0;
}

/* Create an initialized SCTP_inqueue.  */
sctp_inqueue_t *sctp_inqueue_new(void)
{
	sctp_inqueue_t *retval;

	retval = t_new(sctp_inqueue_t, GFP_ATOMIC);
	if (retval) {
		sctp_inqueue_init(retval);
		retval->malloced = 1;
	}
        return retval;
}

/* Release the memory associated with an SCTP inqueue.  */
void sctp_inqueue_free(sctp_inqueue_t *queue)
{
	sctp_chunk_t *chunk;

	/* Empty the queue.  */
	while ((chunk = (sctp_chunk_t *) skb_dequeue(&queue->in)) != NULL)
		sctp_free_chunk(chunk);

	/* If there is a packet which is currently being worked on,
	 * free it as well.
	 */
	if (queue->in_progress)
		sctp_free_chunk(queue->in_progress);

	if (queue->malloced) {
		/* Dump the master memory segment.  */
		kfree(queue);
	}
}

/* Put a new packet in an SCTP inqueue.
 * We assume that packet->sctp_hdr is set and in host byte order.
 */
void sctp_push_inqueue(sctp_inqueue_t *q, sctp_chunk_t *packet)
{
	/* Directly call the packet handling routine. */

	/* We are now calling this either from the soft interrupt
	 * or from the backlog processing.
	 * Eventually, we should clean up inqueue to not rely
	 * on the BH related data structures.
	 */
	skb_queue_tail(&(q->in), (struct sk_buff *) packet);
	q->immediate.routine(q->immediate.data);
}

/* Extract a chunk from an SCTP inqueue.
 *
 * WARNING:  If you need to put the chunk on another queue, you need to
 * make a shallow copy (clone) of it.
 */
sctp_chunk_t *sctp_pop_inqueue(sctp_inqueue_t *queue)
{
	sctp_chunk_t *chunk;
	sctp_chunkhdr_t *ch = NULL;

	/* The assumption is that we are safe to process the chunks
	 * at this time.
	 */

	if ((chunk = queue->in_progress) != NULL) {
		/* There is a packet that we have been working on.
		 * Any post processing work to do before we move on?
		 */
		if (chunk->singleton ||
		    chunk->end_of_packet ||
		    chunk->pdiscard) {
			sctp_free_chunk(chunk);
			chunk = queue->in_progress = NULL;
		} else {
			/* Nothing to do. Next chunk in the packet, please. */
			ch = (sctp_chunkhdr_t *) chunk->chunk_end;

			/* Force chunk->skb->data to chunk->chunk_end.  */
			skb_pull(chunk->skb,
				 chunk->chunk_end - chunk->skb->data);
		}
	}

	/* Do we need to take the next packet out of the queue to process? */
	if (!chunk) {
		/* Is the queue empty?  */
        	if (skb_queue_empty(&queue->in))
			return NULL;

		chunk = queue->in_progress =
			(sctp_chunk_t *) skb_dequeue(&queue->in);

		/* This is the first chunk in the packet.  */
		chunk->singleton = 1;
		ch = (sctp_chunkhdr_t *) chunk->skb->data;
	}

        chunk->chunk_hdr = ch;
        chunk->chunk_end = ((__u8 *) ch)
		+ WORD_ROUND(ntohs(ch->length));
	skb_pull(chunk->skb, sizeof(sctp_chunkhdr_t));
	chunk->subh.v = NULL; /* Subheader is no longer valid.  */

	if (chunk->chunk_end < chunk->skb->tail) {
		/* This is not a singleton */
		chunk->singleton = 0;
	} else {
		/* We are at the end of the packet, so mark the chunk
		 * in case we need to send a SACK.
		 */
		chunk->end_of_packet = 1;
	}

	SCTP_DEBUG_PRINTK("+++sctp_pop_inqueue+++ chunk %p[%s],"
			  " length %d, skb->len %d\n",chunk,
			  sctp_cname(SCTP_ST_CHUNK(chunk->chunk_hdr->type)),
			  ntohs(chunk->chunk_hdr->length), chunk->skb->len);
	return chunk;
}

/* Set a top-half handler.
 *
 * Originally, we the top-half handler was scheduled as a BH.  We now
 * call the handler directly in sctp_push_inqueue() at a time that
 * we know we are lock safe.
 * The intent is that this routine will pull stuff out of the
 * inqueue and process it.
 */
void sctp_inqueue_set_th_handler(sctp_inqueue_t *q,
				 void (*callback)(void *), void *arg)
{
	q->immediate.routine = callback;
	q->immediate.data = arg;
}

