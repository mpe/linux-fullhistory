/*
 * Copyright (c) 2001-2002 by David Brownell
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* this file is part of ehci-hcd.c */

/*-------------------------------------------------------------------------*/

/*
 * EHCI scheduled transaction support:  interrupt, iso, split iso
 * These are called "periodic" transactions in the EHCI spec.
 */

/*
 * Ceiling microseconds (typical) for that many bytes at high speed
 * ISO is a bit less, no ACK ... from USB 2.0 spec, 5.11.3 (and needed
 * to preallocate bandwidth)
 */
#define EHCI_HOST_DELAY	5	/* nsec, guess */
#define HS_USECS(bytes) NS_TO_US ( ((55 * 8 * 2083)/1000) \
	+ ((2083UL * (3167 + BitTime (bytes)))/1000) \
	+ EHCI_HOST_DELAY)
#define HS_USECS_ISO(bytes) NS_TO_US ( ((long)(38 * 8 * 2.083)) \
	+ ((2083UL * (3167 + BitTime (bytes)))/1000) \
	+ EHCI_HOST_DELAY)
	
static int ehci_get_frame (struct usb_hcd *hcd);

/*-------------------------------------------------------------------------*/

/*
 * periodic_next_shadow - return "next" pointer on shadow list
 * @periodic: host pointer to qh/itd/sitd
 * @tag: hardware tag for type of this record
 */
static union ehci_shadow *
periodic_next_shadow (union ehci_shadow *periodic, int tag)
{
	switch (tag) {
	case Q_TYPE_QH:
		return &periodic->qh->qh_next;
	case Q_TYPE_FSTN:
		return &periodic->fstn->fstn_next;
	case Q_TYPE_ITD:
		return &periodic->itd->itd_next;
#ifdef have_split_iso
	case Q_TYPE_SITD:
		return &periodic->sitd->sitd_next;
#endif /* have_split_iso */
	}
	dbg ("BAD shadow %p tag %d", periodic->ptr, tag);
	// BUG ();
	return 0;
}

/* returns true after successful unlink */
/* caller must hold ehci->lock */
static int periodic_unlink (struct ehci_hcd *ehci, unsigned frame, void *ptr)
{
	union ehci_shadow	*prev_p = &ehci->pshadow [frame];
	u32			*hw_p = &ehci->periodic [frame];
	union ehci_shadow	here = *prev_p;
	union ehci_shadow	*next_p;

	/* find predecessor of "ptr"; hw and shadow lists are in sync */
	while (here.ptr && here.ptr != ptr) {
		prev_p = periodic_next_shadow (prev_p, Q_NEXT_TYPE (*hw_p));
		hw_p = &here.qh->hw_next;
		here = *prev_p;
	}
	/* an interrupt entry (at list end) could have been shared */
	if (!here.ptr) {
		dbg ("entry %p no longer on frame [%d]", ptr, frame);
		return 0;
	}
	// vdbg ("periodic unlink %p from frame %d", ptr, frame);

	/* update hardware list ... HC may still know the old structure, so
	 * don't change hw_next until it'll have purged its cache
	 */
	next_p = periodic_next_shadow (&here, Q_NEXT_TYPE (*hw_p));
	*hw_p = here.qh->hw_next;

	/* unlink from shadow list; HCD won't see old structure again */
	*prev_p = *next_p;
	next_p->ptr = 0;

	return 1;
}

/* how many of the uframe's 125 usecs are allocated? */
static unsigned short
periodic_usecs (struct ehci_hcd *ehci, unsigned frame, unsigned uframe)
{
	u32			*hw_p = &ehci->periodic [frame];
	union ehci_shadow	*q = &ehci->pshadow [frame];
	unsigned		usecs = 0;

	while (q->ptr) {
		switch (Q_NEXT_TYPE (*hw_p)) {
		case Q_TYPE_QH:
			/* is it in the S-mask? */
			if (q->qh->hw_info2 & cpu_to_le32 (1 << uframe))
				usecs += q->qh->usecs;
			q = &q->qh->qh_next;
			break;
		case Q_TYPE_FSTN:
			/* for "save place" FSTNs, count the relevant INTR
			 * bandwidth from the previous frame
			 */
			if (q->fstn->hw_prev != EHCI_LIST_END) {
				dbg ("not counting FSTN bandwidth yet ...");
			}
			q = &q->fstn->fstn_next;
			break;
		case Q_TYPE_ITD:
			/* NOTE the "one uframe per itd" policy */
			if (q->itd->hw_transaction [uframe] != 0)
				usecs += q->itd->usecs;
			q = &q->itd->itd_next;
			break;
#ifdef have_split_iso
		case Q_TYPE_SITD:
			temp = q->sitd->hw_fullspeed_ep &
				__constant_cpu_to_le32 (1 << 31);

			// FIXME:  this doesn't count data bytes right...

			/* is it in the S-mask?  (count SPLIT, DATA) */
			if (q->sitd->hw_uframe & cpu_to_le32 (1 << uframe)) {
				if (temp)
					usecs += HS_USECS (188);
				else
					usecs += HS_USECS (1);
			}

			/* ... C-mask?  (count CSPLIT, DATA) */
			if (q->sitd->hw_uframe &
					cpu_to_le32 (1 << (8 + uframe))) {
				if (temp)
					usecs += HS_USECS (0);
				else
					usecs += HS_USECS (188);
			}
			q = &q->sitd->sitd_next;
			break;
#endif /* have_split_iso */
		default:
			BUG ();
		}
	}
#ifdef	DEBUG
	if (usecs > 100)
		err ("overallocated uframe %d, periodic is %d usecs",
			frame * 8 + uframe, usecs);
#endif
	return usecs;
}

/*-------------------------------------------------------------------------*/

static void enable_periodic (struct ehci_hcd *ehci)
{
	u32	cmd;

	/* did clearing PSE did take effect yet?
	 * takes effect only at frame boundaries...
	 */
	while (readl (&ehci->regs->status) & STS_PSS)
		udelay (20);

	cmd = readl (&ehci->regs->command) | CMD_PSE;
	writel (cmd, &ehci->regs->command);
	/* posted write ... PSS happens later */
	ehci->hcd.state = USB_STATE_RUNNING;

	/* make sure tasklet scans these */
	ehci->next_uframe = readl (&ehci->regs->frame_index)
				% (ehci->periodic_size << 3);
}

static void disable_periodic (struct ehci_hcd *ehci)
{
	u32	cmd;

	/* did setting PSE not take effect yet?
	 * takes effect only at frame boundaries...
	 */
	while (!(readl (&ehci->regs->status) & STS_PSS))
		udelay (20);

	cmd = readl (&ehci->regs->command) & ~CMD_PSE;
	writel (cmd, &ehci->regs->command);
	/* posted write ... */

	ehci->next_uframe = -1;
}

/*-------------------------------------------------------------------------*/

static void intr_deschedule (
	struct ehci_hcd	*ehci,
	unsigned	frame,
	struct ehci_qh	*qh,
	unsigned	period
) {
	unsigned long	flags;

	spin_lock_irqsave (&ehci->lock, flags);

	do {
		periodic_unlink (ehci, frame, qh);
		qh_unput (ehci, qh);
		frame += period;
	} while (frame < ehci->periodic_size);

	qh->qh_state = QH_STATE_UNLINK;
	qh->qh_next.ptr = 0;
	ehci->periodic_urbs--;

	/* maybe turn off periodic schedule */
	if (!ehci->periodic_urbs)
		disable_periodic (ehci);
	else
		vdbg ("periodic schedule still enabled");

	spin_unlock_irqrestore (&ehci->lock, flags);

	/*
	 * If the hc may be looking at this qh, then delay a uframe
	 * (yeech!) to be sure it's done.
	 * No other threads may be mucking with this qh.
	 */
	if (((ehci_get_frame (&ehci->hcd) - frame) % period) == 0)
		udelay (125);

	qh->qh_state = QH_STATE_IDLE;
	qh->hw_next = EHCI_LIST_END;

	vdbg ("descheduled qh %p, per = %d frame = %d count = %d, urbs = %d",
		qh, period, frame,
		atomic_read (&qh->refcount), ehci->periodic_urbs);
}

static int intr_submit (
	struct ehci_hcd		*ehci,
	struct urb		*urb,
	struct list_head	*qtd_list,
	int			mem_flags
) {
	unsigned		epnum, period;
	unsigned		temp;
	unsigned short		usecs;
	unsigned long		flags;
	struct ehci_qh		*qh;
	struct hcd_dev		*dev;
	int			status = 0;

	/* get endpoint and transfer data */
	epnum = usb_pipeendpoint (urb->pipe);
	if (usb_pipein (urb->pipe)) {
		temp = urb->dev->epmaxpacketin [epnum];
		epnum |= 0x10;
	} else
		temp = urb->dev->epmaxpacketout [epnum];
	if (urb->dev->speed != USB_SPEED_HIGH) {
		dbg ("no intr/tt scheduling yet"); 
		status = -ENOSYS;
		goto done;
	}

	/*
	 * NOTE: current completion/restart logic doesn't handle more than
	 * one qtd in a periodic qh ... 16-20 KB/urb is pretty big for this.
	 * such big requests need many periods to transfer.
	 */
	if (unlikely (qtd_list->next != qtd_list->prev)) {
		dbg ("only one intr qtd per urb allowed"); 
		status = -EINVAL;
		goto done;
	}

	usecs = HS_USECS (urb->transfer_buffer_length);

	/* FIXME handle HS periods of less than 1 frame. */
	if (urb->interval < 8)
		period = 1;
	else
		period = urb->interval >> 8;
		
	spin_lock_irqsave (&ehci->lock, flags);

	/* get the qh (must be empty and idle) */
	dev = (struct hcd_dev *)urb->dev->hcpriv;
	qh = (struct ehci_qh *) dev->ep [epnum];
	if (qh) {
		/* only allow one queued interrupt urb per EP */
		if (unlikely (qh->qh_state != QH_STATE_IDLE
				|| !list_empty (&qh->qtd_list))) {
			dbg ("interrupt urb already queued");
			status = -EBUSY;
		} else {
			/* maybe reset hardware's data toggle in the qh */
			if (unlikely (!usb_gettoggle (urb->dev, epnum & 0x0f,
					!(epnum & 0x10)))) {
				qh->hw_token |=
					__constant_cpu_to_le32 (QTD_TOGGLE);
				usb_settoggle (urb->dev, epnum & 0x0f,
					!(epnum & 0x10), 1);
			}
			/* trust the QH was set up as interrupt ... */
			list_splice (qtd_list, &qh->qtd_list);
			qh_update (qh, list_entry (qtd_list->next,
						struct ehci_qtd, qtd_list));
		}
	} else {
		/* can't sleep here, we have ehci->lock... */
		qh = ehci_qh_make (ehci, urb, qtd_list, SLAB_ATOMIC);
		qtd_list = &qh->qtd_list;
		if (likely (qh != 0)) {
			// dbg ("new INTR qh %p", qh);
			dev->ep [epnum] = qh;
		} else
			status = -ENOMEM;
	}

	/* Schedule this periodic QH. */
	if (likely (status == 0)) {
		unsigned	frame = urb->interval;

		qh->hw_next = EHCI_LIST_END;
		qh->usecs = usecs;

		urb->hcpriv = qh_put (qh);
		status = -ENOSPC;

		/* pick a set of schedule slots, link the QH into them */
		do {
			int	uframe;

			/* Select some frame 0..(urb->interval - 1) with a
			 * microframe that can hold this transaction.
			 *
			 * FIXME for TT splits, need uframes for start and end.
			 * FSTNs can put end into next frame (uframes 0 or 1).
			 */
			frame--;
			for (uframe = 0; uframe < 8; uframe++) {
				int	claimed;
				claimed = periodic_usecs (ehci, frame, uframe);
				/* 80% periodic == 100 usec max committed */
				if ((claimed + usecs) <= 100) {
					vdbg ("frame %d.%d: %d usecs, plus %d",
						frame, uframe, claimed, usecs);
					break;
				}
			}
			if (uframe == 8)
				continue;
// FIXME delete when code below handles non-empty queues
			if (ehci->pshadow [frame].ptr)
				continue;

			/* QH will run once each period, starting there  */
			urb->start_frame = frame;
			status = 0;

			/* set S-frame mask */
			qh->hw_info2 |= cpu_to_le32 (1 << uframe);
			// dbg_qh ("Schedule INTR qh", ehci, qh);

			/* stuff into the periodic schedule */
			qh->qh_state = QH_STATE_LINKED;
			vdbg ("qh %p usecs %d period %d starting %d.%d",
				qh, qh->usecs, period, frame, uframe);
			do {
				if (unlikely (ehci->pshadow [frame].ptr != 0)) {
// FIXME -- just link to the end, before any qh with a shorter period,
// AND handle it already being (implicitly) linked into this frame
					BUG ();
				} else {
					ehci->pshadow [frame].qh = qh_put (qh);
					ehci->periodic [frame] =
						QH_NEXT (qh->qh_dma);
				}
				frame += period;
			} while (frame < ehci->periodic_size);

			/* update bandwidth utilization records (for usbfs) */
			usb_claim_bandwidth (urb->dev, urb, usecs, 0);

			/* maybe enable periodic schedule processing */
			if (!ehci->periodic_urbs++)
				enable_periodic (ehci);
			break;

		} while (frame);
	}
	spin_unlock_irqrestore (&ehci->lock, flags);
done:
	if (status) {
		usb_complete_t	complete = urb->complete;

		urb->complete = 0;
		urb->status = status;
		qh_completions (ehci, qtd_list, 1);
		urb->complete = complete;
	}
	return status;
}

static unsigned long
intr_complete (
	struct ehci_hcd	*ehci,
	unsigned	frame,
	struct ehci_qh	*qh,
	unsigned long	flags		/* caller owns ehci->lock ... */
) {
	struct ehci_qtd	*qtd;
	struct urb	*urb;
	int		unlinking;

	/* nothing to report? */
	if (likely ((qh->hw_token & __constant_cpu_to_le32 (QTD_STS_ACTIVE))
			!= 0))
		return flags;
	
	qtd = list_entry (qh->qtd_list.next, struct ehci_qtd, qtd_list);
	urb = qtd->urb;
	unlinking = (urb->status == -ENOENT) || (urb->status == -ECONNRESET);

	/* call any completions, after patching for reactivation */
	spin_unlock_irqrestore (&ehci->lock, flags);
	/* NOTE:  currently restricted to one qtd per qh! */
	if (qh_completions (ehci, &qh->qtd_list, 0) == 0)
		urb = 0;
	spin_lock_irqsave (&ehci->lock, flags);

	/* never reactivate requests that were unlinked ... */
	if (likely (urb != 0)) {
		if (unlinking
				|| urb->status == -ECONNRESET
				|| urb->status == -ENOENT
				// || (urb->dev == null)
				|| ehci->hcd.state == USB_STATE_HALT)
			urb = 0;
		// FIXME look at all those unlink cases ... we always
		// need exactly one completion that reports unlink.
		// the one above might not have been it!
	}

	/* normally reactivate */
	if (likely (urb != 0)) {
		if (usb_pipeout (urb->pipe))
			pci_dma_sync_single (ehci->hcd.pdev,
				qtd->buf_dma,
				urb->transfer_buffer_length,
				PCI_DMA_TODEVICE);
		urb->status = -EINPROGRESS;
		urb->actual_length = 0;

		/* patch qh and restart */
		qh_update (qh, qtd);
	}
	return flags;
}

/*-------------------------------------------------------------------------*/

static void
itd_free_list (struct ehci_hcd *ehci, struct urb *urb)
{
	struct ehci_itd *first_itd = urb->hcpriv;

	pci_unmap_single (ehci->hcd.pdev,
		first_itd->buf_dma, urb->transfer_buffer_length,
		usb_pipein (urb->pipe)
		    ? PCI_DMA_FROMDEVICE
		    : PCI_DMA_TODEVICE);
	while (!list_empty (&first_itd->itd_list)) {
		struct ehci_itd	*itd;

		itd = list_entry (
			first_itd->itd_list.next,
			struct ehci_itd, itd_list);
		list_del (&itd->itd_list);
		pci_pool_free (ehci->itd_pool, itd, itd->itd_dma);
	}
	pci_pool_free (ehci->itd_pool, first_itd, first_itd->itd_dma);
	urb->hcpriv = 0;
}

static int
itd_fill (
	struct ehci_hcd	*ehci,
	struct ehci_itd	*itd,
	struct urb	*urb,
	unsigned	index,		// urb->iso_frame_desc [index]
	dma_addr_t	dma		// mapped transfer buffer
) {
	u64		temp;
	u32		buf1;
	unsigned	i, epnum, maxp, multi;
	unsigned	length;

	itd->hw_next = EHCI_LIST_END;
	itd->urb = urb;
	itd->index = index;

	/* tell itd about its transfer buffer, max 2 pages */
	length = urb->iso_frame_desc [index].length;
	dma += urb->iso_frame_desc [index].offset;
	temp = dma & ~0x0fff;
	for (i = 0; i < 2; i++) {
		itd->hw_bufp [i] = cpu_to_le32 ((u32) temp);
		itd->hw_bufp_hi [i] = cpu_to_le32 ((u32)(temp >> 32));
		temp += 0x1000;
	}
	itd->buf_dma = dma;

	/*
	 * this might be a "high bandwidth" highspeed endpoint,
	 * as encoded in the ep descriptor's maxpacket field
	 */
	epnum = usb_pipeendpoint (urb->pipe);
	if (usb_pipein (urb->pipe)) {
		maxp = urb->dev->epmaxpacketin [epnum];
		buf1 = (1 << 11);
	} else {
		maxp = urb->dev->epmaxpacketout [epnum];
		buf1 = 0;
	}
	buf1 |= (maxp & 0x03ff);
	multi = 1;
	multi += (maxp >> 11) & 0x03;
	maxp &= 0x03ff;
	maxp *= multi;

	/* transfer can't fit in any uframe? */ 
	if (length < 0 || maxp < length) {
		dbg ("BAD iso packet: %d bytes, max %d, urb %p [%d] (of %d)",
			length, maxp, urb, index,
			urb->iso_frame_desc [index].length);
		return -ENOSPC;
	}
	itd->usecs = HS_USECS_ISO (length);

	/* "plus" info in low order bits of buffer pointers */
	itd->hw_bufp [0] |= cpu_to_le32 ((epnum << 8) | urb->dev->devnum);
	itd->hw_bufp [1] |= cpu_to_le32 (buf1);
	itd->hw_bufp [2] |= cpu_to_le32 (multi);

	/* figure hw_transaction[] value (it's scheduled later) */
	itd->transaction = EHCI_ISOC_ACTIVE;
	itd->transaction |= dma & 0x0fff;		/* offset; buffer=0 */
	if ((index + 1) == urb->number_of_packets)
		itd->transaction |= EHCI_ITD_IOC; 	/* end-of-urb irq */
	itd->transaction |= length << 16;
	cpu_to_le32s (&itd->transaction);

	return 0;
}

static int
itd_urb_transaction (
	struct ehci_hcd		*ehci,
	struct urb		*urb,
	int			mem_flags
) {
	int			frame_index;
	struct ehci_itd		*first_itd, *itd;
	int			status;
	dma_addr_t		buf_dma, itd_dma;

	/* set up one dma mapping for this urb */
	buf_dma = pci_map_single (ehci->hcd.pdev,
		urb->transfer_buffer, urb->transfer_buffer_length,
		usb_pipein (urb->pipe)
		    ? PCI_DMA_FROMDEVICE
		    : PCI_DMA_TODEVICE);
	if (buf_dma == 0)
		return -ENOMEM;

	/* allocate/init ITDs */
	for (frame_index = 0, first_itd = 0;
			frame_index < urb->number_of_packets;
			frame_index++) {
		itd = pci_pool_alloc (ehci->itd_pool, mem_flags, &itd_dma);
		if (!itd) {
			status = -ENOMEM;
			goto fail;
		}
		memset (itd, 0, sizeof *itd);
		itd->itd_dma = itd_dma;

		status = itd_fill (ehci, itd, urb, frame_index, buf_dma);
		if (status != 0)
			goto fail;

		if (first_itd)
			list_add_tail (&itd->itd_list,
					&first_itd->itd_list);
		else {
			INIT_LIST_HEAD (&itd->itd_list);
			urb->hcpriv = first_itd = itd;
		}
	}
	urb->error_count = 0;
	return 0;

fail:
	if (urb->hcpriv)
		itd_free_list (ehci, urb);
	return status;
}

/*-------------------------------------------------------------------------*/

static inline void
itd_link (struct ehci_hcd *ehci, unsigned frame, struct ehci_itd *itd)
{
	/* always prepend ITD/SITD ... only QH tree is order-sensitive */
	itd->itd_next = ehci->pshadow [frame];
	itd->hw_next = ehci->periodic [frame];
	ehci->pshadow [frame].itd = itd;
	ehci->periodic [frame] = cpu_to_le32 (itd->itd_dma) | Q_TYPE_ITD;
}

/*
 * return zero on success, else -errno
 * - start holds first uframe to start scheduling into
 * - max is the first uframe it's NOT (!) OK to start scheduling into
 * math to be done modulo "mod" (ehci->periodic_size << 3)
 */
static int get_iso_range (
	struct ehci_hcd		*ehci,
	struct urb		*urb,
	unsigned		*start,
	unsigned		*max,
	unsigned		mod
) {
	struct list_head	*lh;
	struct hcd_dev		*dev = urb->dev->hcpriv;
	int			last = -1;
	unsigned		now, span, end;

	span = urb->interval * urb->number_of_packets;

	/* first see if we know when the next transfer SHOULD happen */
	list_for_each (lh, &dev->urb_list) {
		struct urb	*u;
		struct ehci_itd	*itd;
		unsigned	s;

		u = list_entry (lh, struct urb, urb_list);
		if (u == urb || u->pipe != urb->pipe)
			continue;
		if (u->interval != urb->interval) {	/* must not change! */ 
			dbg ("urb %p interval %d ... != %p interval %d",
				u, u->interval, urb, urb->interval);
			return -EINVAL;
		}
		
		/* URB for this endpoint... covers through when?  */
		itd = urb->hcpriv;
		s = itd->uframe + u->interval * u->number_of_packets;
		if (last < 0)
			last = s;
		else {
			/*
			 * So far we can only queue two ISO URBs...
			 *
			 * FIXME do interval math, figure out whether
			 * this URB is "before" or not ... also, handle
			 * the case where the URB might have completed,
			 * but hasn't yet been processed.
			 */
			dbg ("NYET: queue >2 URBs per ISO endpoint");
			return -EDOM;
		}
	}

	/* calculate the legal range [start,max) */
	now = readl (&ehci->regs->frame_index) + 1;	/* next uframe */
	if (!ehci->periodic_urbs)
		now += 8;				/* startup delay */
	now %= mod;
	end = now + mod;
	if (last < 0) {
		*start = now + ehci->i_thresh + /* paranoia */ 1;
		*max = end - span;
		if (*max < *start + 1)
			*max = *start + 1;
	} else {
		*start = last % mod;
		*max = (last + 1) % mod;
	}

	/* explicit start frame? */
	if (!(urb->transfer_flags & USB_ISO_ASAP)) {
		unsigned	temp;

		/* sanity check: must be in range */
		urb->start_frame %= ehci->periodic_size;
		temp = urb->start_frame << 3;
		if (temp < *start)
			temp += mod;
		if (temp > *max)
			return -EDOM;

		/* use that explicit start frame */
		*start = urb->start_frame << 3;
		temp += 8;
		if (temp < *max)
			*max = temp;
	}

	// FIXME minimize wraparound to "now" ... insist max+span
	// (and start+span) remains a few frames short of "end"

	*max %= ehci->periodic_size;
	if ((*start + span) < end)
		return 0;
	return -EFBIG;
}

static int
itd_schedule (struct ehci_hcd *ehci, struct urb *urb)
{
	unsigned	start, max, i;
	int		status;
	unsigned	mod = ehci->periodic_size << 3;

	for (i = 0; i < urb->number_of_packets; i++) {
		urb->iso_frame_desc [i].status = -EINPROGRESS;
		urb->iso_frame_desc [i].actual_length = 0;
	}

	if ((status = get_iso_range (ehci, urb, &start, &max, mod)) != 0)
		return status;

	do {
		unsigned	uframe;
		unsigned	usecs;
		struct ehci_itd	*itd;

		/* check schedule: enough space? */
		itd = urb->hcpriv;
		uframe = start;
		for (i = 0, uframe = start;
				i < urb->number_of_packets;
				i++, uframe += urb->interval) {
			uframe %= mod;

			/* can't commit more than 80% periodic == 100 usec */
			if (periodic_usecs (ehci, uframe >> 3, uframe & 0x7)
					> (100 - itd->usecs)) {
				itd = 0;
				break;
			}
			itd = list_entry (itd->itd_list.next,
				struct ehci_itd, itd_list);
		}
		if (!itd)
			continue;
		
		/* that's where we'll schedule this! */
		itd = urb->hcpriv;
		urb->start_frame = start >> 3;
		vdbg ("ISO urb %p (%d packets period %d) starting %d.%d",
			urb, urb->number_of_packets, urb->interval,
			urb->start_frame, start & 0x7);
		for (i = 0, uframe = start, usecs = 0;
				i < urb->number_of_packets;
				i++, uframe += urb->interval) {
			uframe %= mod;

			itd->uframe = uframe;
			itd->hw_transaction [uframe & 0x07] = itd->transaction;
			itd_link (ehci, (uframe >> 3) % ehci->periodic_size,
				itd);
			usecs += itd->usecs;

			itd = list_entry (itd->itd_list.next,
				struct ehci_itd, itd_list);
		}

		/* update bandwidth utilization records (for usbfs) */
		/* FIXME usbcore expects per-frame average, which isn't
		 * most accurate model... this provides the total claim,
		 * and expects the average to be computed only display.
		 */
		usb_claim_bandwidth (urb->dev, urb, usecs, 1);

		/* maybe enable periodic schedule processing */
		if (!ehci->periodic_urbs++)
			enable_periodic (ehci);

		return 0;

	} while ((start = ++start % mod) != max);

	/* no room in the schedule */
	dbg ("urb %p, CAN'T SCHEDULE", urb);
	return -ENOSPC;
}

/*-------------------------------------------------------------------------*/

#define	ISO_ERRS (EHCI_ISOC_BUF_ERR | EHCI_ISOC_BABBLE | EHCI_ISOC_XACTERR)

static unsigned long
itd_complete (
	struct ehci_hcd	*ehci,
	struct ehci_itd	*itd,
	unsigned	uframe,
	unsigned long	flags
) {
	struct urb				*urb = itd->urb;
	struct usb_iso_packet_descriptor	*desc;
	u32					t;

	/* update status for this uframe's transfers */
	desc = &urb->iso_frame_desc [itd->index];

	t = itd->hw_transaction [uframe];
	itd->hw_transaction [uframe] = 0;
	if (t & EHCI_ISOC_ACTIVE)
		desc->status = -EXDEV;
	else if (t & ISO_ERRS) {
		urb->error_count++;
		if (t & EHCI_ISOC_BUF_ERR)
			desc->status = usb_pipein (urb->pipe)
				? -ENOSR  /* couldn't read */
				: -ECOMM; /* couldn't write */
		else if (t & EHCI_ISOC_BABBLE)
			desc->status = -EOVERFLOW;
		else /* (t & EHCI_ISOC_XACTERR) */
			desc->status = -EPROTO;

		/* HC need not update length with this error */
		if (!(t & EHCI_ISOC_BABBLE))
			desc->actual_length += EHCI_ITD_LENGTH (t);
	} else {
		desc->status = 0;
		desc->actual_length += EHCI_ITD_LENGTH (t);
	}

	vdbg ("itd %p urb %p packet %d/%d trans %x status %d len %d",
		itd, urb, itd->index + 1, urb->number_of_packets,
		t, desc->status, desc->actual_length);

	/* handle completion now? */
	if ((itd->index + 1) != urb->number_of_packets)
		return flags;

	/*
	 * For now, always give the urb back to the driver ... expect it
	 * to submit a new urb (or resubmit this), and to have another
	 * already queued when un-interrupted transfers are needed.
	 * No, that's not what OHCI or UHCI are now doing.
	 *
	 * FIXME Revisit the ISO URB model.  It's cleaner not to have all
	 * the special case magic, but it'd be faster to reuse existing
	 * ITD/DMA setup and schedule state.  Easy to dma_sync/complete(),
	 * then either reschedule or, if unlinking, free and giveback().
	 * But we can't overcommit like the full and low speed HCs do, and
	 * there's no clean way to report an error when rescheduling...
	 *
	 * NOTE that for now we don't accelerate ISO unlinks; they just
	 * happen according to the current schedule.  Means a delay of
	 * up to about a second (max).
	 */
	itd_free_list (ehci, urb);
	if (urb->status == -EINPROGRESS)
		urb->status = 0;

	spin_unlock_irqrestore (&ehci->lock, flags);
	usb_hcd_giveback_urb (&ehci->hcd, urb);
	spin_lock_irqsave (&ehci->lock, flags);

	/* defer stopping schedule; completion can submit */
	ehci->periodic_urbs--;
	if (!ehci->periodic_urbs)
		disable_periodic (ehci);

	return flags;
}

/*-------------------------------------------------------------------------*/

static int itd_submit (struct ehci_hcd *ehci, struct urb *urb, int mem_flags)
{
	int		status;
	unsigned long	flags;

	dbg ("itd_submit urb %p", urb);

	/* NOTE DMA mapping assumes this ... */
	if (urb->iso_frame_desc [0].offset != 0)
		return -EINVAL;
	
	/*
	 * NOTE doing this for now, anticipating periodic URB models
	 * get updated to be "explicit resubmit".
	 */
	if (urb->next) {
		dbg ("use explicit resubmit for ISO");
		return -EINVAL;
	}

	/* allocate ITDs w/o locking anything */
	status = itd_urb_transaction (ehci, urb, mem_flags);
	if (status < 0)
		return status;

	/* schedule ... need to lock */
	spin_lock_irqsave (&ehci->lock, flags);
	status = itd_schedule (ehci, urb);
	spin_unlock_irqrestore (&ehci->lock, flags);
	if (status < 0)
		itd_free_list (ehci, urb);

	return status;
}

#ifdef have_split_iso

/*-------------------------------------------------------------------------*/

/*
 * "Split ISO TDs" ... used for USB 1.1 devices going through
 * the TTs in USB 2.0 hubs.
 */

static void
sitd_free (struct ehci_hcd *ehci, struct ehci_sitd *sitd)
{
	pci_pool_free (ehci->sitd_pool, sitd, sitd->sitd_dma);
}

static struct ehci_sitd *
sitd_make (
	struct ehci_hcd	*ehci,
	struct urb	*urb,
	unsigned	index,		// urb->iso_frame_desc [index]
	unsigned	uframe,		// scheduled start
	dma_addr_t	dma,		// mapped transfer buffer
	int		mem_flags
) {
	struct ehci_sitd	*sitd;
	unsigned		length;

	sitd = pci_pool_alloc (ehci->sitd_pool, mem_flags, &dma);
	if (!sitd)
		return sitd;
	sitd->urb = urb;
	length = urb->iso_frame_desc [index].length;
	dma += urb->iso_frame_desc [index].offset;

#if 0
	// FIXME:  do the rest!
#else
	sitd_free (ehci, sitd);
	return 0;
#endif

}

static void
sitd_link (struct ehci_hcd *ehci, unsigned frame, struct ehci_sitd *sitd)
{
	u32		ptr;

	ptr = cpu_to_le32 (sitd->sitd_dma | 2);	// type 2 == sitd
	if (ehci->pshadow [frame].ptr) {
		if (!sitd->sitd_next.ptr) {
			sitd->sitd_next = ehci->pshadow [frame];
			sitd->hw_next = ehci->periodic [frame];
		} else if (sitd->sitd_next.ptr != ehci->pshadow [frame].ptr) {
			dbg ("frame %d sitd link goof", frame);
			BUG ();
		}
	}
	ehci->pshadow [frame].sitd = sitd;
	ehci->periodic [frame] = ptr;
}

static unsigned long
sitd_complete (
	struct ehci_hcd *ehci,
	struct ehci_sitd	*sitd,
	unsigned long		flags
) {
	// FIXME -- implement!

	dbg ("NYI -- sitd_complete");
	return flags;
}

/*-------------------------------------------------------------------------*/

static int sitd_submit (struct ehci_hcd *ehci, struct urb *urb, int mem_flags)
{
	// struct ehci_sitd	*first_sitd = 0;
	unsigned		frame_index;
	dma_addr_t		dma;

	dbg ("NYI -- sitd_submit");

	// FIXME -- implement!

	// FIXME:  setup one big dma mapping
	dma = 0;

	for (frame_index = 0;
			frame_index < urb->number_of_packets;
			frame_index++) {
		struct ehci_sitd	*sitd;
		unsigned		uframe;

		// FIXME:  use real arguments, schedule this!
		uframe = -1;

		sitd = sitd_make (ehci, urb, frame_index,
				uframe, dma, mem_flags);

		if (sitd) {
    /*
			if (first_sitd)
				list_add_tail (&sitd->sitd_list,
						&first_sitd->sitd_list);
			else
				first_sitd = sitd;
    */
		} else {
			// FIXME:  clean everything up
		}
	}

	// if we have a first sitd, then
		// store them all into the periodic schedule!
		// urb->hcpriv = first sitd in sitd_list

	return -ENOSYS;
}
#endif /* have_split_iso */

/*-------------------------------------------------------------------------*/

static void scan_periodic (struct ehci_hcd *ehci)
{
	unsigned	frame, clock, now_uframe, mod;
	unsigned long	flags;

	mod = ehci->periodic_size << 3;
	spin_lock_irqsave (&ehci->lock, flags);

	/*
	 * When running, scan from last scan point up to "now"
	 * else clean up by scanning everything that's left.
	 * Touches as few pages as possible:  cache-friendly.
	 * Don't scan ISO entries more than once, though.
	 */
	frame = ehci->next_uframe >> 3;
	if (HCD_IS_RUNNING (ehci->hcd.state))
		now_uframe = readl (&ehci->regs->frame_index);
	else
		now_uframe = (frame << 3) - 1;
	now_uframe %= mod;
	clock = now_uframe >> 3;

	for (;;) {
		union ehci_shadow	q, *q_p;
		u32			type, *hw_p;
		unsigned		uframes;

restart:
		/* scan schedule to _before_ current frame index */
		if (frame == clock)
			uframes = now_uframe & 0x07;
		else
			uframes = 8;

		q_p = &ehci->pshadow [frame];
		hw_p = &ehci->periodic [frame];
		q.ptr = q_p->ptr;
		type = Q_NEXT_TYPE (*hw_p);

		/* scan each element in frame's queue for completions */
		while (q.ptr != 0) {
			int			last;
			unsigned		uf;
			union ehci_shadow	temp;

			switch (type) {
			case Q_TYPE_QH:
				last = (q.qh->hw_next == EHCI_LIST_END);
				temp = q.qh->qh_next;
				type = Q_NEXT_TYPE (q.qh->hw_next);
				flags = intr_complete (ehci, frame,
						qh_put (q.qh), flags);
				qh_unput (ehci, q.qh);
				q = temp;
				break;
			case Q_TYPE_FSTN:
				last = (q.fstn->hw_next == EHCI_LIST_END);
				/* for "save place" FSTNs, look at QH entries
				 * in the previous frame for completions.
				 */
				if (q.fstn->hw_prev != EHCI_LIST_END) {
					dbg ("ignoring completions from FSTNs");
				}
				type = Q_NEXT_TYPE (q.fstn->hw_next);
				q = q.fstn->fstn_next;
				break;
			case Q_TYPE_ITD:
				last = (q.itd->hw_next == EHCI_LIST_END);

				/* Unlink each (S)ITD we see, since the ISO
				 * URB model forces constant rescheduling.
				 * That complicates sharing uframes in ITDs,
				 * and means we need to skip uframes the HC
				 * hasn't yet processed.
				 */
				for (uf = 0; uf < uframes; uf++) {
					if (q.itd->hw_transaction [uf] != 0) {
						temp = q;
						*q_p = q.itd->itd_next;
						*hw_p = q.itd->hw_next;
						type = Q_NEXT_TYPE (*hw_p);

						/* might free q.itd ... */
						flags = itd_complete (ehci,
							temp.itd, uf, flags);
						break;
					}
				}
				/* we might skip this ITD's uframe ... */
				if (uf == uframes) {
					q_p = &q.itd->itd_next;
					hw_p = &q.itd->hw_next;
					type = Q_NEXT_TYPE (q.itd->hw_next);
				}

				q = *q_p;
				break;
#ifdef have_split_iso
			case Q_TYPE_SITD:
				last = (q.sitd->hw_next == EHCI_LIST_END);
				flags = sitd_complete (ehci, q.sitd, flags);
				type = Q_NEXT_TYPE (q.sitd->hw_next);

				// FIXME unlink SITD after split completes
				q = q.sitd->sitd_next;
				break;
#endif /* have_split_iso */
			default:
				dbg ("corrupt type %d frame %d shadow %p",
					type, frame, q.ptr);
				// BUG ();
				last = 1;
				q.ptr = 0;
			}

			/* did completion remove an interior q entry? */
			if (unlikely (q.ptr == 0 && !last))
				goto restart;
		}

		/* stop when we catch up to the HC */

		// FIXME:  this assumes we won't get lapped when
		// latencies climb; that should be rare, but...
		// detect it, and just go all the way around.
		// FLR might help detect this case, so long as latencies
		// don't exceed periodic_size msec (default 1.024 sec).

		// FIXME:  likewise assumes HC doesn't halt mid-scan

		if (frame == clock) {
			unsigned	now;

			if (!HCD_IS_RUNNING (ehci->hcd.state))
				break;
			ehci->next_uframe = now_uframe;
			now = readl (&ehci->regs->frame_index) % mod;
			if (now_uframe == now)
				break;

			/* rescan the rest of this frame, then ... */
			now_uframe = now;
			clock = now_uframe >> 3;
		} else
			frame = (frame + 1) % ehci->periodic_size;
	} 
	spin_unlock_irqrestore (&ehci->lock, flags);
}
