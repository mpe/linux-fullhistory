/*
 * Universal Host Controller Interface driver for USB.
 *
 * (C) Copyright 1999 Linus Torvalds
 * (C) Copyright 1999-2000 Johannes Erdfelt, jerdfelt@sventech.com
 * (C) Copyright 1999 Randy Dunlap
 * (C) Copyright 1999 Georg Acher, acher@in.tum.de
 * (C) Copyright 1999 Deti Fliegl, deti@fliegl.de
 * (C) Copyright 1999 Thomas Sailer, sailer@ife.ee.ethz.ch
 * (C) Copyright 1999 Roman Weissgaerber, weissg@vienna.at
 *
 * Intel documents this fairly well, and as far as I know there
 * are no royalties or anything like that, but even so there are
 * people who decided that they want to do the same thing in a
 * completely different way.
 *
 * WARNING! The USB documentation is downright evil. Most of it
 * is just crap, written by a committee. You're better off ignoring
 * most of it, the important stuff is:
 *  - the low-level protocol (fairly simple but lots of small details)
 *  - working around the horridness of the rest
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/malloc.h>
#include <linux/smp_lock.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>

#define DEBUG
#include "usb.h"

#include "uhci.h"
#include "uhci-debug.h"

#ifdef CONFIG_APM
#include <linux/apm_bios.h>
static int handle_apm_event(apm_event_t event);
#endif

static int debug = 1;
MODULE_PARM(debug, "i");

static kmem_cache_t *uhci_td_cachep;
static kmem_cache_t *uhci_qh_cachep;

static LIST_HEAD(uhci_list);

static int rh_submit_urb(urb_t *urb);
static int rh_unlink_urb(urb_t *urb);
static int uhci_get_current_frame_number(struct usb_device *usb_dev);

#define min(a,b) (((a)<(b))?(a):(b))

/*
 * Only the USB core should call uhci_alloc_dev and uhci_free_dev
 */
static int uhci_alloc_dev(struct usb_device *usb_dev)
{
	return 0;
}

static int uhci_free_dev(struct usb_device *usb_dev)
{
	return 0;
}

/*
 * UHCI interrupt list operations..
 */
static void uhci_add_irq_list(struct uhci *uhci, struct uhci_td *td)
{
	unsigned long flags;

	nested_lock(&uhci->irqlist_lock, flags);
	list_add(&td->irq_list, &uhci->interrupt_list);
	nested_unlock(&uhci->irqlist_lock, flags);
}

static void uhci_remove_irq_list(struct uhci *uhci, struct uhci_td *td)
{
	unsigned long flags;

	nested_lock(&uhci->irqlist_lock, flags);
	if (td->irq_list.next != &td->irq_list) {
		list_del(&td->irq_list);
		INIT_LIST_HEAD(&td->irq_list);
	}
	nested_unlock(&uhci->irqlist_lock, flags);
}

static void uhci_add_urb_list(struct uhci *uhci, struct urb *urb)
{
	unsigned long flags;

	spin_lock_irqsave(&uhci->urblist_lock, flags);
	list_add(&urb->urb_list, &uhci->urb_list);
	spin_unlock_irqrestore(&uhci->urblist_lock, flags);
}

static void uhci_remove_urb_list(struct uhci *uhci, struct urb *urb)
{
	unsigned long flags;

	spin_lock_irqsave(&uhci->urblist_lock, flags);
	if (urb->urb_list.next != &urb->urb_list) {
		list_del(&urb->urb_list);
		INIT_LIST_HEAD(&urb->urb_list);
	}
	spin_unlock_irqrestore(&uhci->urblist_lock, flags);
}

/*
 * We insert Isochronous transfers directly into the frame list at the
 * beginning
 * The layout looks as follows:
 * frame list pointer -> iso td's (if any) ->
 * periodic interrupt td (if frame 0) -> irq td's -> control qh -> bulk qh
 */

static void uhci_insert_td_frame_list(struct uhci *uhci, struct uhci_td *td, unsigned framenum)
{
	unsigned long flags;
	struct uhci_td *nexttd;

	framenum %= UHCI_NUMFRAMES;

	spin_lock_irqsave(&uhci->framelist_lock, flags);
	td->frameptr = &uhci->fl->frame[framenum];
	td->link = uhci->fl->frame[framenum];
	if (!(td->link & (UHCI_PTR_TERM | UHCI_PTR_QH))) {
		nexttd = (struct uhci_td *)uhci_ptr_to_virt(td->link);
		td->nexttd = nexttd;
		nexttd->prevtd = td;
		nexttd->frameptr = NULL;
	}
	uhci->fl->frame[framenum] = virt_to_bus(td);
	spin_unlock_irqrestore(&uhci->framelist_lock, flags);
}

static void uhci_remove_td(struct uhci *uhci, struct uhci_td *td)
{
	unsigned long flags;

	spin_lock_irqsave(&uhci->framelist_lock, flags);
	if (td->frameptr) {
		*(td->frameptr) = td->link;
		if (td->nexttd) {
			td->nexttd->frameptr = td->frameptr;
			td->nexttd->prevtd = NULL;
			td->nexttd = NULL;
		}
		td->frameptr = NULL;
	} else {
		if (td->prevtd) {
			td->prevtd->nexttd = td->nexttd;
			td->prevtd->link = td->link;
		}
		if (td->nexttd)
			td->nexttd->prevtd = td->prevtd;
		td->prevtd = td->nexttd = NULL;
	}
	td->link = UHCI_PTR_TERM;
	spin_unlock_irqrestore(&uhci->framelist_lock, flags);
}

static void uhci_insert_td(struct uhci *uhci, struct uhci_td *skeltd, struct uhci_td *td)
{
	unsigned long flags;

	spin_lock_irqsave(&uhci->framelist_lock, flags);

	/* Fix the linked list pointers */
	td->nexttd = skeltd->nexttd;
	td->prevtd = skeltd;
	if (skeltd->nexttd)
		skeltd->nexttd->prevtd = td;
	skeltd->nexttd = td;

	td->link = skeltd->link;
	skeltd->link = virt_to_bus(td);

	spin_unlock_irqrestore(&uhci->framelist_lock, flags);
}

/*
 * Inserts a td into qh list at the top.
 */
static void uhci_insert_tds_in_qh(struct uhci_qh *qh, struct uhci_td *begin)
{
	struct uhci_td *td, *prevtd;

	if (!begin)		/* Nothing to do */
		return;

	/* Grab the first TD and add it to the QH */
	td = begin;
	qh->element = virt_to_bus(td) | UHCI_PTR_DEPTH;

	/* Go through the rest of the TD's, link them together */
	prevtd = td;
	td = td->next;
	while (td) {
		prevtd->link = virt_to_bus(td) | UHCI_PTR_DEPTH;

		prevtd = td;
		td = td->next;
	}

	prevtd->link = UHCI_PTR_TERM;
}

static struct uhci_td *uhci_td_alloc(struct usb_device *dev)
{
	struct uhci_td *td;

	td = kmem_cache_alloc(uhci_td_cachep, in_interrupt() ? SLAB_ATOMIC : SLAB_KERNEL);
	if (!td)
		return NULL;

	td->link = UHCI_PTR_TERM;
	td->buffer = 0;

	td->frameptr = NULL;
	td->nexttd = td->prevtd = NULL;
	td->next = NULL;
	td->dev = dev;
	INIT_LIST_HEAD(&td->irq_list);
	INIT_LIST_HEAD(&td->list);

	usb_inc_dev_use(dev);

	return td;
}

static void uhci_td_free(struct uhci_td *td)
{
	kmem_cache_free(uhci_td_cachep, td);

	if (td->dev)
		usb_dec_dev_use(td->dev);
}

static void uhci_schedule_delete_td(struct uhci *uhci, struct uhci_td *td)
{
	unsigned long flags;

	spin_lock_irqsave(&uhci->freelist_lock, flags);
	list_add(&td->list, &uhci->td_free_list);
	if (td->dev) {
		usb_dec_dev_use(td->dev);
		td->dev = NULL;
	}
	spin_unlock_irqrestore(&uhci->freelist_lock, flags);
}

static struct uhci_qh *uhci_qh_alloc(struct usb_device *dev)
{
	struct uhci_qh *qh;

	qh = kmem_cache_alloc(uhci_qh_cachep, in_interrupt() ? SLAB_ATOMIC : SLAB_KERNEL);
	if (!qh)
		return NULL;

	qh->element = UHCI_PTR_TERM;
	qh->link = UHCI_PTR_TERM;

	qh->dev = dev;
	qh->prevqh = qh->nextqh = NULL;

	INIT_LIST_HEAD(&qh->list);

	usb_inc_dev_use(dev);

	return qh;
}

static void uhci_qh_free(struct uhci_qh *qh)
{
	kmem_cache_free(uhci_qh_cachep, qh);

	if (qh->dev)
		usb_dec_dev_use(qh->dev);
}

static void uhci_schedule_delete_qh(struct uhci *uhci, struct uhci_qh *qh)
{
	unsigned long flags;

	spin_lock_irqsave(&uhci->freelist_lock, flags);
	list_add(&qh->list, &uhci->qh_free_list);
	if (qh->dev) {
		usb_dec_dev_use(qh->dev);
		qh->dev = NULL;
	}
	spin_unlock_irqrestore(&uhci->freelist_lock, flags);
}

static void uhci_insert_qh(struct uhci *uhci, struct uhci_qh *skelqh, struct uhci_qh *qh)
{
	unsigned long flags;

	spin_lock_irqsave(&uhci->framelist_lock, flags);

	/* Fix the linked list pointers */
	qh->nextqh = skelqh->nextqh;
	qh->prevqh = skelqh;
	if (skelqh->nextqh)
		skelqh->nextqh->prevqh = qh;
	skelqh->nextqh = qh;

	qh->link = skelqh->link;
	skelqh->link = virt_to_bus(qh) | UHCI_PTR_QH;

	spin_unlock_irqrestore(&uhci->framelist_lock, flags);
}

static void uhci_remove_qh(struct uhci *uhci, struct uhci_qh *qh)
{
	unsigned long flags;

	spin_lock_irqsave(&uhci->framelist_lock, flags);
	if (qh->prevqh) {
		qh->prevqh->nextqh = qh->nextqh;
		qh->prevqh->link = qh->link;
	}
	if (qh->nextqh)
		qh->nextqh->prevqh = qh->prevqh;
	qh->prevqh = qh->nextqh = NULL;
	spin_unlock_irqrestore(&uhci->framelist_lock, flags);
}

static void inline uhci_fill_td(struct uhci_td *td, __u32 status,
		__u32 info, __u32 buffer)
{
	td->status = status;
	td->info = info;
	td->buffer = buffer;
}

static void uhci_add_td_to_urb(urb_t *urb, struct uhci_td *td)
{
	struct urb_priv *urbp = (struct urb_priv *)urb->hcpriv;

	td->urb = urb;

	if (urbp->end)
		urbp->end->next = td;

	urbp->end = td;

	if (!urbp->begin)
		urbp->begin = td;
}

/*
 * Map status to standard result codes
 *
 * <status> is (td->status & 0xFE0000) [a.k.a. uhci_status_bits(td->status)]
 * <dir_out> is True for output TDs and False for input TDs.
 */
static int uhci_map_status(int status, int dir_out)
{
	if (!status)
		return 0;
	if (status & TD_CTRL_BITSTUFF)			/* Bitstuff error */
		return -EPROTO;
	if (status & TD_CTRL_CRCTIMEO) {		/* CRC/Timeout */
		if (dir_out)
			return -ETIMEDOUT;
		else
			return -EILSEQ;
	}
	if (status & TD_CTRL_NAK)			/* NAK */
		return -ETIMEDOUT;
	if (status & TD_CTRL_BABBLE)			/* Babble */
		return -EPIPE;
	if (status & TD_CTRL_DBUFERR)			/* Buffer error */
		return -ENOSR;
	if (status & TD_CTRL_STALLED)			/* Stalled */
		return -EPIPE;
	if (status & TD_CTRL_ACTIVE)			/* Active */
		return 0;

	return -EINVAL;
}

/*
 * Control transfers
 */
static int uhci_submit_control(urb_t *urb)
{
	struct uhci_td *td;
	struct uhci_qh *qh;
	unsigned long destination, status;
	struct uhci *uhci = urb->dev->bus->hcpriv;
	int maxsze = usb_maxpacket(urb->dev, urb->pipe, usb_pipeout(urb->pipe));
	int len = urb->transfer_buffer_length;
	unsigned char *data = urb->transfer_buffer;
	struct urb_priv *urbp;

	/* The "pipe" thing contains the destination in bits 8--18 */
	destination = (urb->pipe & PIPE_DEVEP_MASK) | USB_PID_SETUP;

	/* 3 errors */
	status = (urb->pipe & TD_CTRL_LS) | TD_CTRL_ACTIVE | (3 << 27);

	urbp = kmalloc(sizeof(*urbp), in_interrupt() ? GFP_ATOMIC : GFP_KERNEL);
	if (!urbp)
		return -ENOMEM;

	urbp->begin = urbp->end = NULL;

	urb->hcpriv = urbp;

	/*
	 * Build the TD for the control request
	 */
	td = uhci_td_alloc(urb->dev);
	if (!td)
		return -ENOMEM;

	uhci_add_td_to_urb(urb, td);
	uhci_fill_td(td, status, destination | (7 << 21),
		virt_to_bus(urb->setup_packet));

	/*
	 * If direction is "send", change the frame from SETUP (0x2D)
	 * to OUT (0xE1). Else change it from SETUP to IN (0x69).
	 */
	destination ^= (USB_PID_SETUP ^ usb_packetid(urb->pipe));

	if (!(urb->transfer_flags & USB_DISABLE_SPD))
		status |= TD_CTRL_SPD;

	/*
	 * Build the DATA TD's
	 */
	td = uhci_td_alloc(urb->dev);
	if (!td) {
		/* FIXME: Free the TD's */
		return -ENOMEM;
	}

	while (len > 0) {
		int pktsze = len;

		if (pktsze > maxsze)
			pktsze = maxsze;

		/* Alternate Data0/1 (start with Data1) */
		destination ^= 1 << TD_TOKEN_TOGGLE;
	
		uhci_add_td_to_urb(urb, td);
		uhci_fill_td(td, status, destination | ((pktsze - 1) << 21),
			virt_to_bus(data));

		data += pktsze;
		len -= pktsze;

		td = uhci_td_alloc(urb->dev);
		if (!td)
			/* FIXME: Free all of the previously allocated td's */
			return -ENOMEM;
	}

	/*
	 * Build the final TD for control status 
	 *
	 * It's IN if the pipe is an output pipe or we're not expecting
	 * data back.
	 */
	destination &= ~TD_PID;
	if (usb_pipeout(urb->pipe) || !urb->transfer_buffer_length)
		destination |= USB_PID_IN;
	else
		destination |= USB_PID_OUT;

	destination |= 1 << TD_TOKEN_TOGGLE;		/* End in Data1 */

	status &= ~TD_CTRL_SPD;

	uhci_add_td_to_urb(urb, td);
	uhci_fill_td(td, status | TD_CTRL_IOC,
		destination | (UHCI_NULL_DATA_SIZE << 21), 0);

	uhci_add_irq_list(uhci, td);

	qh = uhci_qh_alloc(urb->dev);
	if (!qh) {
		/* FIXME: Free all of the TD's */
		return -ENOMEM;
	}
	uhci_insert_tds_in_qh(qh, urbp->begin);

	uhci_insert_qh(uhci, &uhci->skel_control_qh, qh);
	urbp->qh = qh;

	uhci_add_urb_list(uhci, urb);

	usb_inc_dev_use(urb->dev);

	return -EINPROGRESS;
}

/* This is also the uhci_unlink_bulk function */
static int uhci_unlink_control(urb_t *urb)
{
	struct urb_priv *urbp = urb->hcpriv;
	struct uhci_td *td;
	struct uhci *uhci = urb->dev->bus->hcpriv;

	if (!urbp)
		return -EINVAL;

	uhci_remove_qh(uhci, urbp->qh);
	uhci_schedule_delete_qh(uhci, urbp->qh);

	/* Go through the rest of the TD's, deleting them, then scheduling */
	/*  their deletion */
	td = urbp->begin;
	while (td) {
		struct uhci_td *next = td->next;

		if (td->status & TD_CTRL_IOC)
			uhci_remove_irq_list(uhci, td);

		uhci_schedule_delete_td(uhci, td);

		td = next;
	}

	kfree(urbp);
	urb->hcpriv = NULL;

	uhci_remove_urb_list(uhci, urb);

	return 0;
}

static int uhci_result_control(urb_t *urb)
{
	struct urb_priv *urbp = urb->hcpriv;
	struct uhci_td *td;
	unsigned int status;
	int ret;

	td = urbp->begin;
	if (!td)		/* Nothing to do */
		return -EINVAL;

	/* The first TD is the SETUP phase, check the status, but skip */
	/*  the count */
	status =  uhci_status_bits(td->status);
	if (status & TD_CTRL_ACTIVE)
		return -EINPROGRESS;

	if (status)
		goto td_error;

	urb->actual_length = 0;

	/* The rest of the TD's (but the last) are data */
	td = td->next;
	while (td && td->next) {
		status = uhci_status_bits(td->status);
		if (status & TD_CTRL_ACTIVE)
			return -EINPROGRESS;

		urb->actual_length += uhci_actual_length(td->status);

		/* If SPD is set then we received a short packet */
		/*  There will be no status phase at the end */
		if (td->status & TD_CTRL_SPD && (uhci_actual_length(td->status) < uhci_expected_length(td->info)))
			goto td_success;

		if (status)
			goto td_error;

		td = td->next;
	}

	/* Control status phase */
	status = uhci_status_bits(td->status);

	/* APC BackUPS Pro kludge */
	/* It tries to send all of the descriptor instead of */
	/*  the amount we requested */
	if (td->status & TD_CTRL_IOC &&
	    status & TD_CTRL_ACTIVE &&
	    status & TD_CTRL_NAK)
		goto td_success;

	if (status & TD_CTRL_ACTIVE)
		return -EINPROGRESS;

	if (status)
		goto td_error;

td_success:
	uhci_unlink_control(urb);

	return 0;

td_error:
	/* Some debugging code */
	if (debug) {
		dbg("uhci_result_control() failed with status %x",
			status);

		/* Print the chain for debugging purposes */
		uhci_show_queue(urbp->qh);
	}

	if (status & TD_CTRL_STALLED) {
		/* endpoint has stalled - mark it halted */
		usb_endpoint_halt(urb->dev, uhci_endpoint(td->info),
	    			uhci_packetout(td->info));
		uhci_unlink_control(urb);

		return -EPIPE;
	}

	ret = uhci_map_status(status, uhci_packetout(td->info));

	uhci_unlink_control(urb);

	return ret;
}

/*
 * Interrupt transfers
 */
static int uhci_submit_interrupt(urb_t *urb)
{
	struct uhci_td *td;
	unsigned long destination, status;
	struct uhci *uhci = urb->dev->bus->hcpriv;
	struct urb_priv *urbp;

	if (urb->transfer_buffer_length > usb_maxpacket(urb->dev, urb->pipe, usb_pipeout(urb->pipe)))
		return -EINVAL;

	/* The "pipe" thing contains the destination in bits 8--18 */
	destination = (urb->pipe & PIPE_DEVEP_MASK) | usb_packetid(urb->pipe);

	status = (urb->pipe & TD_CTRL_LS) | TD_CTRL_ACTIVE | TD_CTRL_SPD |
			TD_CTRL_IOC;

	urbp = kmalloc(sizeof(*urbp), in_interrupt() ? GFP_ATOMIC : GFP_KERNEL);
	if (!urbp)
		return -ENOMEM;

	urbp->begin = urbp->end = NULL;

	urb->hcpriv = urbp;

	td = uhci_td_alloc(urb->dev);
	if (!td)
		return -ENOMEM;

	destination |= (usb_gettoggle(urb->dev, usb_pipeendpoint(urb->pipe), usb_pipeout(urb->pipe)) << TD_TOKEN_TOGGLE);
	destination |= ((urb->transfer_buffer_length - 1) << 21);

	uhci_add_td_to_urb(urb, td);
	uhci_fill_td(td, status, destination,
		virt_to_bus(urb->transfer_buffer));

	uhci_add_irq_list(uhci, td);

	uhci_insert_td(uhci, &uhci->skeltd[__interval_to_skel(urb->interval)], td);

	uhci_add_urb_list(uhci, urb);

	usb_inc_dev_use(urb->dev);

	return -EINPROGRESS;
}

static int uhci_unlink_interrupt(urb_t *urb)
{
	struct urb_priv *urbp = urb->hcpriv;
	struct uhci_td *td;
	struct uhci *uhci = urb->dev->bus->hcpriv;

	if (!urbp)
		return -EINVAL;

	td = urbp->begin;
	uhci_remove_td(uhci, td);
	if (td->status & TD_CTRL_IOC)
		uhci_remove_irq_list(uhci, td);
	uhci_schedule_delete_td(uhci, td);

	kfree(urbp);
	urb->hcpriv = NULL;

	uhci_remove_urb_list(uhci, urb);

	return 0;
}

static int uhci_result_interrupt(urb_t *urb)
{
	struct urb_priv *urbp = (struct urb_priv *)urb->hcpriv;
	struct uhci_td *td;
	int status;

	if (!urbp)
		return -EINVAL;

	td = urbp->begin;
	if (!td)
		return -EINVAL;

	status =  uhci_status_bits(td->status);
	if (status & TD_CTRL_ACTIVE)
		return -EINPROGRESS;

	if (status)
		return uhci_map_status(status, uhci_packetout(td->info));

	urb->actual_length += uhci_actual_length(td->status);

	return 0;
}

static void uhci_reset_interrupt(urb_t *urb)
{
	struct urb_priv *urbp = (struct urb_priv *)urb->hcpriv;
	struct uhci_td *td;

	if (urb->interval) {
		td = urbp->begin;

		usb_dotoggle(urb->dev, usb_pipeendpoint(urb->pipe), usb_pipeout(urb->pipe));
		td->status = (td->status & 0x2F000000) | TD_CTRL_ACTIVE | TD_CTRL_IOC;
		td->info &= ~(1 << TD_TOKEN_TOGGLE);
		td->info |= (usb_gettoggle(urb->dev, usb_pipeendpoint(urb->pipe), usb_pipeout(urb->pipe)) << TD_TOKEN_TOGGLE);

		urb->status = -EINPROGRESS;
	} else
		uhci_unlink_interrupt(urb);
}

/*
 * Bulk transfers
 */
static int uhci_submit_bulk(urb_t *urb)
{
	struct uhci_td *td;
	struct uhci_qh *qh;
	unsigned long destination, status;
	struct uhci *uhci = urb->dev->bus->hcpriv;
	int maxsze = usb_maxpacket(urb->dev, urb->pipe, usb_pipeout(urb->pipe));
	int len = urb->transfer_buffer_length;
	unsigned char *data = urb->transfer_buffer;
	struct urb_priv *urbp;

	if (len < 0)
		return -EINVAL;

	/* The "pipe" thing contains the destination in bits 8--18 */
	destination = (urb->pipe & PIPE_DEVEP_MASK) | usb_packetid(urb->pipe);

	/* 3 errors */
	status = (urb->pipe & TD_CTRL_LS) | TD_CTRL_ACTIVE | (3 << 27);
	if (!(urb->transfer_flags & USB_DISABLE_SPD))
		status |= TD_CTRL_SPD;

	urbp = kmalloc(sizeof(*urbp), in_interrupt() ? GFP_ATOMIC : GFP_KERNEL);
	if (!urbp)
		return -ENOMEM;

	urbp->begin = urbp->end = NULL;

	urb->hcpriv = urbp;

	/*
	 * Build the DATA TD's
	 */
	while (len > 0) {
		int pktsze = len;

		if (pktsze > maxsze)
			pktsze = maxsze;

		td = uhci_td_alloc(urb->dev);
		if (!td) {
			/* FIXME: Free the TD's */
			return -ENOMEM;
		}

		uhci_add_td_to_urb(urb, td);
		uhci_fill_td(td, status, destination | ((pktsze - 1) << 21) |
			(usb_gettoggle(urb->dev, usb_pipeendpoint(urb->pipe),
			 usb_pipeout(urb->pipe)) << TD_TOKEN_TOGGLE),
			virt_to_bus(data));

		data += pktsze;
		len -= maxsze;

		if (len <= 0) {
			td->status |= TD_CTRL_IOC;
			uhci_add_irq_list(uhci, td);
		}

		usb_dotoggle(urb->dev, usb_pipeendpoint(urb->pipe),
			usb_pipeout(urb->pipe));
	}

	qh = uhci_qh_alloc(urb->dev);
	if (!qh) {
		/* FIXME: Free all of the TD's */
		return -ENOMEM;
	}
	uhci_insert_tds_in_qh(qh, urbp->begin);

	uhci_insert_qh(uhci, &uhci->skel_bulk_qh, qh);
	urbp->qh = qh;

	uhci_add_urb_list(uhci, urb);

	usb_inc_dev_use(urb->dev);

	return -EINPROGRESS;
}

/* We can use the control unlink since they're identical */
#define uhci_unlink_bulk uhci_unlink_control

static int uhci_result_bulk(urb_t *urb)
{
	struct urb_priv *urbp = urb->hcpriv;
	struct uhci_td *td;
	unsigned int status;

	urb->actual_length = 0;

	/* The rest of the TD's (but the last) are data */
	for (td = urbp->begin; td; td = td->next) {
		status = uhci_status_bits(td->status);
		if (status & TD_CTRL_ACTIVE)
			return -EINPROGRESS;

		urb->actual_length += uhci_actual_length(td->status);

		/* If SPD is set then we received a short packet */
		if (td->status & TD_CTRL_SPD && (uhci_actual_length(td->status) < uhci_expected_length(td->info))) {
			usb_settoggle(urb->dev, uhci_endpoint(td->info),
				uhci_packetout(td->info),
				uhci_toggle(td->info) ^ 1);

			goto td_success;
		}

		if (status)
			goto td_error;
	}

td_success:
	uhci_unlink_bulk(urb);

	return 0;

td_error:
	/* Some debugging code */
	if (debug) {
		dbg("uhci_result_bulk() failed with status %x",
			status);

		/* Print the chain for debugging purposes */
		uhci_show_queue(urbp->qh);
	}

	if (status & TD_CTRL_STALLED) {
		/* endpoint has stalled - mark it halted */
		usb_endpoint_halt(urb->dev, uhci_endpoint(td->info),
	    			uhci_packetout(td->info));
		return -EPIPE;
	}

	return uhci_map_status(status, uhci_packetout(td->info));
}

/*
 * Isochronous transfers
 */
static int isochronous_find_limits(urb_t *urb, unsigned int *start, unsigned int *end)
{
	urb_t *u, *last_urb = NULL;
	struct uhci *uhci = (struct uhci *)urb->dev->bus->hcpriv;
	struct list_head *tmp, *head = &uhci->urb_list;
	unsigned long flags;

	spin_lock_irqsave(&uhci->urblist_lock, flags);
	tmp = head->next;
	while (tmp != head) {
		u = list_entry(tmp, urb_t, urb_list);

		/* look for pending URB's with identical pipe handle */
		if ((urb->pipe == u->pipe) && (urb->dev == u->dev) &&
		    (u->status == -EINPROGRESS) && (u != urb)) {
			if (!last_urb)
				*start = u->start_frame;
			last_urb = u;
		}
		tmp = tmp->next;
	}
	spin_unlock_irqrestore(&uhci->urblist_lock, flags);

	if (last_urb) {
		*end = (last_urb->start_frame + last_urb->number_of_packets) & 1023;
		return 0;
	} else
		return -1;	// no previous urb found

}

static int isochronous_find_start(urb_t *urb)
{
	int limits;
	unsigned int start = 0, end = 0;

	if (urb->number_of_packets > 900)	/* 900? Why? */
		return -EFBIG;

	limits = isochronous_find_limits(urb, &start, &end);

	if (urb->transfer_flags & USB_ISO_ASAP) {
		if (limits) {
			int curframe;

			curframe = uhci_get_current_frame_number(urb->dev) % UHCI_NUMFRAMES;
			urb->start_frame = (curframe + 10) % UHCI_NUMFRAMES;
		} else
			urb->start_frame = end;
	} else {
		urb->start_frame %= UHCI_NUMFRAMES;
		/* FIXME: Sanity check */
	}

	return 0;
}

static int uhci_submit_isochronous(urb_t *urb)
{
	struct uhci_td *td;
	struct uhci *uhci = urb->dev->bus->hcpriv;
	struct urb_priv *urbp;
	int i, ret, framenum;
	int status, destination;

	status = TD_CTRL_ACTIVE | TD_CTRL_IOS;
	destination = (urb->pipe & PIPE_DEVEP_MASK) | usb_packetid(urb->pipe);

	ret = isochronous_find_start(urb);
	if (ret)
		return ret;

	urbp = kmalloc(sizeof(*urbp), in_interrupt() ? GFP_ATOMIC : GFP_KERNEL);
	if (!urbp)
		return -ENOMEM;

	urbp->begin = urbp->end = NULL;

	urb->hcpriv = urbp;

	framenum = urb->start_frame;
	for (i = 0; i < urb->number_of_packets; i++, framenum++) {
		if (!urb->iso_frame_desc[i].length)
			continue;

		td = uhci_td_alloc(urb->dev);
		if (!td) {
			/* FIXME: Free the TD's */
			return -ENOMEM;
		}

		uhci_add_td_to_urb(urb, td);
		uhci_fill_td(td, status, destination | ((urb->iso_frame_desc[i].length - 1) << 21),
			virt_to_bus(urb->transfer_buffer + urb->iso_frame_desc[i].offset));

		if (i + 1 >= urb->number_of_packets) {
			td->status |= TD_CTRL_IOC;
			uhci_add_irq_list(uhci, td);
		}

		uhci_insert_td_frame_list(uhci, td, framenum);
	}

	uhci_add_urb_list(uhci, urb);

	usb_inc_dev_use(urb->dev);

	return -EINPROGRESS;
}

static int uhci_unlink_isochronous(urb_t *urb)
{
	struct urb_priv *urbp = urb->hcpriv;
	struct uhci_td *td;
	struct uhci *uhci = urb->dev->bus->hcpriv;

	if (!urbp)
		return -EINVAL;

	/* Go through the rest of the TD's, deleting them, then scheduling */
	/*  their deletion */
	td = urbp->begin;
	while (td) {
		struct uhci_td *next = td->next;

		uhci_remove_td(uhci, td);

		if (td->status & TD_CTRL_IOC)
			uhci_remove_irq_list(uhci, td);
		uhci_schedule_delete_td(uhci, td);

		td = next;
	}

	kfree(urbp);
	urb->hcpriv = NULL;

	uhci_remove_urb_list(uhci, urb);

	return 0;
}

static int uhci_result_isochronous(urb_t *urb)
{
	struct urb_priv *urbp = (struct urb_priv *)urb->hcpriv;
	struct uhci_td *td;
	int status;
	int i, ret = 0;

	td = urbp->end;
	if (!td)		/* Nothing to do */
		return -EINVAL;

	status =  uhci_status_bits(td->status);
	if (status & TD_CTRL_ACTIVE)
		return -EINPROGRESS;

	/* Assume no errors, we'll overwrite this if not */
	urb->status = 0;

	urb->actual_length = 0;
	for (i = 0, td = urbp->begin; td; i++, td = td->next) {
		int actlength;

		actlength = uhci_actual_length(td->status);
		urb->iso_frame_desc[i].actual_length = actlength;
		urb->actual_length += actlength;

		status = uhci_map_status(uhci_status_bits(td->status), usb_pipeout(urb->pipe));
		urb->iso_frame_desc[i].status = status;
		if (status != 0) {
			urb->error_count++;
			ret = status;
		}
	}

	uhci_unlink_isochronous(urb);

	return status;
}

static int uhci_submit_urb(urb_t *urb)
{
	int ret = -EINVAL;
	struct uhci *uhci;

	if (!urb)
		return -EINVAL;

	if (!urb->dev || !urb->dev->bus)
		return -ENODEV;

	uhci = (struct uhci *)urb->dev->bus->hcpriv;

	if (usb_pipedevice(urb->pipe) == uhci->rh.devnum)
		return rh_submit_urb(urb);	/* Virtual root hub */

	switch (usb_pipetype(urb->pipe)) {
	case PIPE_CONTROL:
		ret = uhci_submit_control(urb);
		break;
	case PIPE_INTERRUPT:
		ret = uhci_submit_interrupt(urb);
		break;
	case PIPE_BULK:
		ret = uhci_submit_bulk(urb);
		break;
	case PIPE_ISOCHRONOUS:
		ret = uhci_submit_isochronous(urb);
		break;
	}

	urb->status = ret;
	if (ret == -EINPROGRESS)
		return 0;

	return ret;
}

/*
 * Return the result of a transfer
 */
static void uhci_transfer_result(urb_t *urb)
{
	urb_t *turb;
	int proceed = 0, is_ring = 0;
	int ret = -EINVAL;

	switch (usb_pipetype(urb->pipe)) {
	case PIPE_CONTROL:
		ret = uhci_result_control(urb);
		break;
	case PIPE_INTERRUPT:
		/* Interrupts are an exception */
		urb->status = uhci_result_interrupt(urb);
		if (urb->status != -EINPROGRESS) {
			urb->complete(urb);
			uhci_reset_interrupt(urb);
		}
		return;
	case PIPE_BULK:
		ret = uhci_result_bulk(urb);
		break;
	case PIPE_ISOCHRONOUS:
		ret = uhci_result_isochronous(urb);
		break;
	}

	urb->status = ret;
	if (urb->status == -EINPROGRESS)
		return;

	if (urb->next) {
		turb = urb->next;
		do {
			if (turb->status != -EINPROGRESS) {
				proceed = 1;
				break;
			}

			turb = turb->next;
		} while (turb && turb != urb && turb != urb->next);

		if (turb == urb || turb == urb->next)
			is_ring = 1;
	}

	if (urb->complete && (!proceed || (urb->transfer_flags & USB_URB_EARLY_COMPLETE))) {
		urb->complete(urb);
		if (!proceed && is_ring)
			uhci_submit_urb(urb);
	}

	if (proceed && urb->next) {
		turb = urb->next;
		do {
			if (turb->status != -EINPROGRESS &&
			    uhci_submit_urb(turb) != 0)

			turb = turb->next;
		} while (turb && turb != urb->next);

		if (urb->complete && !(urb->transfer_flags & USB_URB_EARLY_COMPLETE))
			urb->complete(urb);
	}
}

static int uhci_unlink_urb(urb_t *urb)
{
	struct uhci *uhci;
	int ret = 0;

	if (!urb)
		return -EINVAL;

	uhci = (struct uhci *)urb->dev->bus->hcpriv;

	if (usb_pipedevice(urb->pipe) == uhci->rh.devnum)
		return rh_unlink_urb(urb);

	if (urb->status == -EINPROGRESS) {
		switch (usb_pipetype(urb->pipe)) {
		case PIPE_CONTROL:
			ret = uhci_unlink_control(urb);
			break;
		case PIPE_INTERRUPT:
			ret = uhci_unlink_interrupt(urb);
			break;
		case PIPE_BULK:
			ret = uhci_unlink_bulk(urb);
			break;
		case PIPE_ISOCHRONOUS:
			ret = uhci_unlink_isochronous(urb);
			break;
		}

		if (urb->complete)
			urb->complete(urb);

		if (in_interrupt()) {		/* wait at least 1 frame */
			int errorcount = 10;

			if (errorcount--)
				dbg("uhci_unlink_urb called from interrupt for urb %p", urb);
			udelay(1000);
		} else
			schedule_timeout(1+1*HZ/1000); 

		usb_dec_dev_use(urb->dev);
	}

	urb->status = -ENOENT;

	return ret;
}

/*
 * uhci_get_current_frame_number()
 *
 * returns the current frame number for a USB bus/controller.
 */
static int uhci_get_current_frame_number(struct usb_device *dev)
{
	struct uhci *uhci = (struct uhci *)dev->bus->hcpriv;

	return inw(uhci->io_addr + USBFRNUM);
}

struct usb_operations uhci_device_operations = {
	uhci_alloc_dev,
	uhci_free_dev,
	uhci_get_current_frame_number,
	uhci_submit_urb,
	uhci_unlink_urb
};

/* -------------------------------------------------------------------
   Virtual Root Hub
   ------------------------------------------------------------------- */

static __u8 root_hub_dev_des[] =
{
 	0x12,			/*  __u8  bLength; */
	0x01,			/*  __u8  bDescriptorType; Device */
	0x00,			/*  __u16 bcdUSB; v1.0 */
	0x01,
	0x09,			/*  __u8  bDeviceClass; HUB_CLASSCODE */
	0x00,			/*  __u8  bDeviceSubClass; */
	0x00,			/*  __u8  bDeviceProtocol; */
	0x08,			/*  __u8  bMaxPacketSize0; 8 Bytes */
	0x00,			/*  __u16 idVendor; */
	0x00,
	0x00,			/*  __u16 idProduct; */
	0x00,
	0x00,			/*  __u16 bcdDevice; */
	0x00,
	0x00,			/*  __u8  iManufacturer; */
	0x00,			/*  __u8  iProduct; */
	0x00,			/*  __u8  iSerialNumber; */
	0x01			/*  __u8  bNumConfigurations; */
};


/* Configuration descriptor */
static __u8 root_hub_config_des[] =
{
	0x09,			/*  __u8  bLength; */
	0x02,			/*  __u8  bDescriptorType; Configuration */
	0x19,			/*  __u16 wTotalLength; */
	0x00,
	0x01,			/*  __u8  bNumInterfaces; */
	0x01,			/*  __u8  bConfigurationValue; */
	0x00,			/*  __u8  iConfiguration; */
	0x40,			/*  __u8  bmAttributes;
					Bit 7: Bus-powered, 6: Self-powered,
					Bit 5 Remote-wakeup, 4..0: resvd */
	0x00,			/*  __u8  MaxPower; */

	/* interface */
	0x09,			/*  __u8  if_bLength; */
	0x04,			/*  __u8  if_bDescriptorType; Interface */
	0x00,			/*  __u8  if_bInterfaceNumber; */
	0x00,			/*  __u8  if_bAlternateSetting; */
	0x01,			/*  __u8  if_bNumEndpoints; */
	0x09,			/*  __u8  if_bInterfaceClass; HUB_CLASSCODE */
	0x00,			/*  __u8  if_bInterfaceSubClass; */
	0x00,			/*  __u8  if_bInterfaceProtocol; */
	0x00,			/*  __u8  if_iInterface; */

	/* endpoint */
	0x07,			/*  __u8  ep_bLength; */
	0x05,			/*  __u8  ep_bDescriptorType; Endpoint */
	0x81,			/*  __u8  ep_bEndpointAddress; IN Endpoint 1 */
	0x03,			/*  __u8  ep_bmAttributes; Interrupt */
	0x08,			/*  __u16 ep_wMaxPacketSize; 8 Bytes */
	0x00,
	0xff			/*  __u8  ep_bInterval; 255 ms */
};

static __u8 root_hub_hub_des[] =
{
	0x09,			/*  __u8  bLength; */
	0x29,			/*  __u8  bDescriptorType; Hub-descriptor */
	0x02,			/*  __u8  bNbrPorts; */
	0x00,			/* __u16  wHubCharacteristics; */
	0x00,
	0x01,			/*  __u8  bPwrOn2pwrGood; 2ms */
	0x00,			/*  __u8  bHubContrCurrent; 0 mA */
	0x00,			/*  __u8  DeviceRemovable; *** 7 Ports max *** */
	0xff			/*  __u8  PortPwrCtrlMask; *** 7 ports max *** */
};

/*-------------------------------------------------------------------------*/
/* prepare Interrupt pipe transaction data; HUB INTERRUPT ENDPOINT */
static int rh_send_irq(urb_t *urb)
{
	int i, len = 1;
	struct uhci *uhci = (struct uhci *)urb->dev->bus->hcpriv;
	unsigned int io_addr = uhci->io_addr;
	__u16 data = 0;

	for (i = 0; i < uhci->rh.numports; i++) {
		data |= ((inw(io_addr + USBPORTSC1 + i * 2) & 0xa) > 0 ? (1 << (i + 1)) : 0);
		len = (i + 1) / 8 + 1;
	}

	*(__u16 *) urb->transfer_buffer = cpu_to_le16(data);
	urb->actual_length = len;
	urb->status = USB_ST_NOERROR;

	if ((data > 0) && (uhci->rh.send != 0)) {
#ifdef DEBUG /* JE */
static int foo=5;
if (foo--)
#endif
		dbg("root-hub INT complete: port1: %x port2: %x data: %x",
			inw(io_addr + USBPORTSC1), inw(io_addr + USBPORTSC2), data);
		urb->complete(urb);
	}

	return USB_ST_NOERROR;
}

/*-------------------------------------------------------------------------*/
/* Virtual Root Hub INTs are polled by this timer every "interval" ms */
static int rh_init_int_timer(urb_t *urb);

static void rh_int_timer_do(unsigned long ptr)
{
	int len;
	urb_t *urb = (urb_t *)ptr;
	struct uhci *uhci = (struct uhci *)urb->dev->bus->hcpriv;

	if (uhci->rh.send) {
		len = rh_send_irq(urb);
		if (len > 0) {
			urb->actual_length = len;
			if (urb->complete)
				urb->complete(urb);
		}
	}

	rh_init_int_timer(urb);
}

/*-------------------------------------------------------------------------*/
/* Root Hub INTs are polled by this timer */
static int rh_init_int_timer(urb_t *urb)
{
	struct uhci *uhci = (struct uhci *)urb->dev->bus->hcpriv;

	uhci->rh.interval = urb->interval;
	init_timer(&uhci->rh.rh_int_timer);
	uhci->rh.rh_int_timer.function = rh_int_timer_do;
	uhci->rh.rh_int_timer.data = (unsigned long)urb;
	uhci->rh.rh_int_timer.expires = jiffies + (HZ * (urb->interval < 30 ? 30 : urb->interval)) / 1000;
	add_timer(&uhci->rh.rh_int_timer);

	return 0;
}

/*-------------------------------------------------------------------------*/
#define OK(x)			len = (x); break

#define CLR_RH_PORTSTAT(x) \
	status = inw(io_addr + USBPORTSC1 + 2 * (wIndex-1)); \
	status = (status & 0xfff5) & ~(x); \
	outw(status, io_addr + USBPORTSC1 + 2 * (wIndex-1))

#define SET_RH_PORTSTAT(x) \
	status = inw(io_addr + USBPORTSC1 + 2 * (wIndex-1)); \
	status = (status & 0xfff5) | (x); \
	outw(status, io_addr + USBPORTSC1 + 2 * (wIndex-1))


/*-------------------------------------------------------------------------*/
/*************************
 ** Root Hub Control Pipe
 *************************/

static int rh_submit_urb(urb_t *urb)
{
	struct uhci *uhci = (struct uhci *)urb->dev->bus->hcpriv;
	unsigned int pipe = urb->pipe;
	devrequest *cmd = (devrequest *)urb->setup_packet;
	void *data = urb->transfer_buffer;
	int leni = urb->transfer_buffer_length;
	int len = 0;
	int status = 0;
	int stat = USB_ST_NOERROR;
	int i;
	unsigned int io_addr = uhci->io_addr;
	__u16 cstatus;
	__u16 bmRType_bReq;
	__u16 wValue;
	__u16 wIndex;
	__u16 wLength;

	if (usb_pipetype(pipe) == PIPE_INTERRUPT) {
		uhci->rh.urb = urb;
		uhci->rh.send = 1;
		uhci->rh.interval = urb->interval;
		rh_init_int_timer(urb);

		return USB_ST_NOERROR;
	}

	bmRType_bReq = cmd->requesttype | cmd->request << 8;
	wValue = le16_to_cpu(cmd->value);
	wIndex = le16_to_cpu(cmd->index);
	wLength = le16_to_cpu(cmd->length);

	for (i = 0; i < 8; i++)
		uhci->rh.c_p_r[i] = 0;

	switch (bmRType_bReq) {
		/* Request Destination:
		   without flags: Device,
		   RH_INTERFACE: interface,
		   RH_ENDPOINT: endpoint,
		   RH_CLASS means HUB here,
		   RH_OTHER | RH_CLASS  almost ever means HUB_PORT here
		*/

	case RH_GET_STATUS:
		*(__u16 *)data = cpu_to_le16(1);
		OK(2);
	case RH_GET_STATUS | RH_INTERFACE:
		*(__u16 *)data = cpu_to_le16(0);
		OK(2);
	case RH_GET_STATUS | RH_ENDPOINT:
		*(__u16 *)data = cpu_to_le16(0);
		OK(2);
	case RH_GET_STATUS | RH_CLASS:
		*(__u32 *)data = cpu_to_le32(0);
		OK(4);		/* hub power */
	case RH_GET_STATUS | RH_OTHER | RH_CLASS:
		status = inw(io_addr + USBPORTSC1 + 2 * (wIndex - 1));
		cstatus = ((status & USBPORTSC_CSC) >> (1 - 0)) |
			((status & USBPORTSC_PEC) >> (3 - 1)) |
			(uhci->rh.c_p_r[wIndex - 1] << (0 + 4));
			status = (status & USBPORTSC_CCS) |
			((status & USBPORTSC_PE) >> (2 - 1)) |
			((status & USBPORTSC_SUSP) >> (12 - 2)) |
			((status & USBPORTSC_PR) >> (9 - 4)) |
			(1 << 8) |      /* power on */
			((status & USBPORTSC_LSDA) << (-8 + 9));

		*(__u16 *)data = cpu_to_le16(status);
		*(__u16 *)(data + 2) = cpu_to_le16(cstatus);
		OK(4);
	case RH_CLEAR_FEATURE | RH_ENDPOINT:
		switch (wValue) {
		case RH_ENDPOINT_STALL:
			OK(0);
		}
		break;
	case RH_CLEAR_FEATURE | RH_CLASS:
		switch (wValue) {
		case RH_C_HUB_OVER_CURRENT:
			OK(0);	/* hub power over current */
		}
		break;
	case RH_CLEAR_FEATURE | RH_OTHER | RH_CLASS:
		switch (wValue) {
		case RH_PORT_ENABLE:
			CLR_RH_PORTSTAT(USBPORTSC_PE);
			OK(0);
		case RH_PORT_SUSPEND:
			CLR_RH_PORTSTAT(USBPORTSC_SUSP);
			OK(0);
		case RH_PORT_POWER:
			OK(0);	/* port power */
		case RH_C_PORT_CONNECTION:
			SET_RH_PORTSTAT(USBPORTSC_CSC);
			OK(0);
		case RH_C_PORT_ENABLE:
			SET_RH_PORTSTAT(USBPORTSC_PEC);
			OK(0);
		case RH_C_PORT_SUSPEND:
			/*** WR_RH_PORTSTAT(RH_PS_PSSC); */
			OK(0);
		case RH_C_PORT_OVER_CURRENT:
			OK(0);	/* port power over current */
		case RH_C_PORT_RESET:
			uhci->rh.c_p_r[wIndex - 1] = 0;
			OK(0);
		}
		break;
	case RH_SET_FEATURE | RH_OTHER | RH_CLASS:
		switch (wValue) {
		case RH_PORT_SUSPEND:
			SET_RH_PORTSTAT(USBPORTSC_SUSP);
			OK(0);
		case RH_PORT_RESET:
			SET_RH_PORTSTAT(USBPORTSC_PR);
			wait_ms(10);
			uhci->rh.c_p_r[wIndex - 1] = 1;
			CLR_RH_PORTSTAT(USBPORTSC_PR);
			udelay(10);
			SET_RH_PORTSTAT(USBPORTSC_PE);
			wait_ms(10);
			SET_RH_PORTSTAT(0xa);
			OK(0);
		case RH_PORT_POWER:
			OK(0); /* port power ** */
		case RH_PORT_ENABLE:
			SET_RH_PORTSTAT (USBPORTSC_PE);
			OK(0);
		}
		break;
	case RH_SET_ADDRESS:
		uhci->rh.devnum = wValue;
		OK(0);
	case RH_GET_DESCRIPTOR:
		switch ((wValue & 0xff00) >> 8) {
		case 0x01:	/* device descriptor */
			len = min(leni, min(sizeof(root_hub_dev_des), wLength));
			memcpy(data, root_hub_dev_des, len);
			OK(len);
		case 0x02:	/* configuration descriptor */
			len = min(leni, min(sizeof(root_hub_config_des), wLength));
			memcpy (data, root_hub_config_des, len);
			OK(len);
		case 0x03:	/* string descriptors */
			stat = -EPIPE;
		}
		break;
	case RH_GET_DESCRIPTOR | RH_CLASS:
		root_hub_hub_des[2] = uhci->rh.numports;
		len = min(leni, min(sizeof(root_hub_hub_des), wLength));
		memcpy(data, root_hub_hub_des, len);
		OK(len);
	case RH_GET_CONFIGURATION:
		*(__u8 *)data = 0x01;
		OK(1);
	case RH_SET_CONFIGURATION:
		OK(0);
	default:
		stat = -EPIPE;
	}

	urb->actual_length = len;
	urb->status = stat;
	if (urb->complete)
		urb->complete(urb);

	return USB_ST_NOERROR;
}
/*-------------------------------------------------------------------------*/

static int rh_unlink_urb(urb_t *urb)
{
	struct uhci *uhci = (struct uhci *)urb->dev->bus->hcpriv;

	uhci->rh.send = 0;
	del_timer(&uhci->rh.rh_int_timer);

	return 0;
}
/*-------------------------------------------------------------------*/

void uhci_free_pending(struct uhci *uhci)
{
	struct list_head *tmp, *head;

	/* Free all of the pending QH's and TD's */
	head = &uhci->td_free_list;
	tmp = head->next;
	while (tmp != head) {
		struct uhci_td *td = list_entry(tmp, struct uhci_td, list);

		tmp = tmp->next;

                list_del(&td->list);
                INIT_LIST_HEAD(&td->list);

		uhci_td_free(td);
	}

	head = &uhci->qh_free_list;
	tmp = head->next;
	while (tmp != head) {
		struct uhci_qh *qh = list_entry(tmp, struct uhci_qh, list);

		tmp = tmp->next;

                list_del(&qh->list);
                INIT_LIST_HEAD(&qh->list);

		uhci_qh_free(qh);
	}
}

static void uhci_interrupt(int irq, void *__uhci, struct pt_regs *regs)
{
	struct uhci *uhci = __uhci;
	unsigned int io_addr = uhci->io_addr;
	unsigned short status;
	unsigned long flags;
	struct list_head *tmp, *head;
	urb_t *urb;

	/*
	 * Read the interrupt status, and write it back to clear the
	 * interrupt cause
	 */
	status = inw(io_addr + USBSTS);
	if (!status)	/* shared interrupt, not mine */
		return;
	outw(status, io_addr + USBSTS);

	if (status & ~(USBSTS_USBINT | USBSTS_ERROR)) {
		if (status & USBSTS_RD)
			printk(KERN_INFO "uhci: resume detected, not implemented\n");
		if (status & USBSTS_HSE)
			printk(KERN_ERR "uhci: host system error, PCI problems?\n");
		if (status & USBSTS_HCPE)
			printk(KERN_ERR "uhci: host controller process error. something bad happened\n");
		if (status & USBSTS_HCH) {
			printk(KERN_ERR "uhci: host controller halted. very bad\n");
			/* FIXME: Reset the controller, fix the offending TD */
		}
	}

	/* Free all of the pending QH's and TD's */
	spin_lock(&uhci->freelist_lock);
	uhci_free_pending(uhci);
	spin_unlock(&uhci->freelist_lock);

	/* Walk the list of pending TD's to see which ones completed.. */
	nested_lock(&uhci->irqlist_lock, flags);
	head = &uhci->interrupt_list;
	tmp = head->next;
	while (tmp != head) {
		struct uhci_td *td = list_entry(tmp, struct uhci_td, irq_list);

		urb = td->urb;

		tmp = tmp->next;

		/* Checks the status and does all of the magic necessary */
		uhci_transfer_result(urb);
	}
	nested_unlock(&uhci->irqlist_lock, flags);
}

static void reset_hc(struct uhci *uhci)
{
	unsigned int io_addr = uhci->io_addr;

	/* Global reset for 50ms */
	outw(USBCMD_GRESET, io_addr + USBCMD);
	wait_ms(50);
	outw(0, io_addr + USBCMD);
	wait_ms(10);
}

static void start_hc(struct uhci *uhci)
{
	unsigned int io_addr = uhci->io_addr;
	int timeout = 1000;

	/*
	 * Reset the HC - this will force us to get a
	 * new notification of any already connected
	 * ports due to the virtual disconnect that it
	 * implies.
	 */
	outw(USBCMD_HCRESET, io_addr + USBCMD);
	while (inw(io_addr + USBCMD) & USBCMD_HCRESET) {
		if (!--timeout) {
			printk(KERN_ERR "uhci: USBCMD_HCRESET timed out!\n");
			break;
		}
	}

	/* Turn on all interrupts */
	outw(USBINTR_TIMEOUT | USBINTR_RESUME | USBINTR_IOC | USBINTR_SP,
		io_addr + USBINTR);

	/* Start at frame 0 */
	outw(0, io_addr + USBFRNUM);
	outl(virt_to_bus(uhci->fl), io_addr + USBFLBASEADD);

	/* Run and mark it configured with a 64-byte max packet */
	outw(USBCMD_RS | USBCMD_CF | USBCMD_MAXP, io_addr + USBCMD);
}

/*
 * Allocate a frame list, and then setup the skeleton
 *
 * The hardware doesn't really know any difference
 * in the queues, but the order does matter for the
 * protocols higher up. The order is:
 *
 *  - any isochronous events handled before any
 *    of the queues. We don't do that here, because
 *    we'll create the actual TD entries on demand.
 *  - The first queue is the "interrupt queue".
 *  - The second queue is the "control queue".
 *  - The third queue is "bulk data".
 */
static struct uhci *alloc_uhci(unsigned int io_addr, unsigned int io_size)
{
	int i, port;
	struct uhci *uhci;
	struct usb_bus *bus;

	uhci = kmalloc(sizeof(*uhci), GFP_KERNEL);
	if (!uhci)
		return NULL;

	memset(uhci, 0, sizeof(*uhci));

	uhci->irq = -1;
	uhci->io_addr = io_addr;
	uhci->io_size = io_size;

	INIT_LIST_HEAD(&uhci->interrupt_list);
	INIT_LIST_HEAD(&uhci->urb_list);
	INIT_LIST_HEAD(&uhci->td_free_list);
	INIT_LIST_HEAD(&uhci->qh_free_list);

	spin_lock_init(&uhci->urblist_lock);
	spin_lock_init(&uhci->framelist_lock);
	spin_lock_init(&uhci->freelist_lock);
	nested_init(&uhci->irqlist_lock);

	/* We need exactly one page (per UHCI specs), how convenient */
	/* We assume that one page is atleast 4k (1024 frames * 4 bytes) */
	uhci->fl = (void *)__get_free_page(GFP_KERNEL);
	if (!uhci->fl)
		goto au_free_uhci;

	bus = usb_alloc_bus(&uhci_device_operations);
	if (!bus)
		goto au_free_fl;

	uhci->bus = bus;
	bus->hcpriv = uhci;

	/* Initialize the root hub */

	/* UHCI specs says devices must have 2 ports, but goes on to say */
	/*  they may have more but give no way to determine how many they */
	/*  have. However, according to the UHCI spec, Bit 7 is always set */
	/*  to 1. So we try to use this to our advantage */
	for (port = 0; port < (io_size - 0x10) / 2; port++) {
		unsigned int portstatus;

		portstatus = inw(io_addr + 0x10 + (port * 2));
		if (!(portstatus & 0x0080))
			break;
	}
	if (debug)
		info("detected %d ports", port);

	/* This is experimental so anything less than 2 or greater than 8 is */
	/*  something weird and we'll ignore it */
	if (port < 2 || port > 8) {
		info("port count misdetected? forcing to 2 ports");
		port = 2;
	}

	uhci->rh.numports = port;

	/*
	 * 9 Interrupt queues; link int2 to int1, int4 to int2, etc
	 * then link int1 to control and control to bulk
	 */
	for (i = 1; i < 9; i++) {
		struct uhci_td *td = &uhci->skeltd[i];

		uhci_fill_td(td, 0, (UHCI_NULL_DATA_SIZE << 21) | (0x7f << 8) | USB_PID_IN, 0);
		td->link = virt_to_bus(&uhci->skeltd[i - 1]);
	}


	uhci_fill_td(&uhci->skel_int1_td, 0, (UHCI_NULL_DATA_SIZE << 21) | (0x7f << 8) | USB_PID_IN, 0);
	uhci->skel_int1_td.link = virt_to_bus(&uhci->skel_control_qh) | UHCI_PTR_QH;

	uhci->skel_control_qh.link = virt_to_bus(&uhci->skel_bulk_qh) | UHCI_PTR_QH;
	uhci->skel_control_qh.element = UHCI_PTR_TERM;

	uhci->skel_bulk_qh.link = UHCI_PTR_TERM;
	uhci->skel_bulk_qh.element = UHCI_PTR_TERM;

	/*
	 * Fill the frame list: make all entries point to
	 * the proper interrupt queue.
	 *
	 * This is probably silly, but it's a simple way to
	 * scatter the interrupt queues in a way that gives
	 * us a reasonable dynamic range for irq latencies.
	 */
	for (i = 0; i < 1024; i++) {
		struct uhci_td *irq = &uhci->skel_int2_td;

		if (i & 1) {
			irq++;
			if (i & 2) {
				irq++;
				if (i & 4) { 
					irq++;
					if (i & 8) { 
						irq++;
						if (i & 16) {
							irq++;
							if (i & 32) {
								irq++;
								if (i & 64)
									irq++;
							}
						}
					}
				}
			}
		}

		/* Only place we don't use the frame list routines */
		uhci->fl->frame[i] =  virt_to_bus(irq);
	}

	return uhci;

/*
 * error exits:
 */
au_free_fl:
	free_page((unsigned long)uhci->fl);
au_free_uhci:
	kfree(uhci);

	return NULL;
}

/*
 * De-allocate all resources..
 */
static void release_uhci(struct uhci *uhci)
{
	if (uhci->irq >= 0) {
		free_irq(uhci->irq, uhci);
		uhci->irq = -1;
	}

	if (uhci->fl) {
		free_page((unsigned long)uhci->fl);
		uhci->fl = NULL;
	}

	usb_free_bus(uhci->bus);
	kfree(uhci);
}

int uhci_start_root_hub(struct uhci *uhci)
{
	struct usb_device *dev;

	dev = usb_alloc_dev(NULL, uhci->bus);
	if (!dev)
		return -1;

	uhci->bus->root_hub = dev;
	usb_connect(dev);

	if (usb_new_device(dev) != 0) {
		usb_free_dev(dev);

		return -1;
	}

	return 0;
}

/*
 * If we've successfully found a UHCI, now is the time to increment the
 * module usage count, and return success..
 */
static int setup_uhci(int irq, unsigned int io_addr, unsigned int io_size)
{
	int retval;
	struct uhci *uhci;

	uhci = alloc_uhci(io_addr, io_size);
	if (!uhci)
		return -ENOMEM;

	INIT_LIST_HEAD(&uhci->uhci_list);
	list_add(&uhci->uhci_list, &uhci_list);

	request_region(uhci->io_addr, io_size, "usb-uhci");

	reset_hc(uhci);

	usb_register_bus(uhci->bus);
	start_hc(uhci);

	retval = -EBUSY;
	if (request_irq(irq, uhci_interrupt, SA_SHIRQ, "usb-uhci", uhci) == 0) {
		uhci->irq = irq;

		if (!uhci_start_root_hub(uhci))
			return 0;
	}

	/* Couldn't allocate IRQ if we got here */
	list_del(&uhci->uhci_list);
	INIT_LIST_HEAD(&uhci->uhci_list);

	reset_hc(uhci);
	release_region(uhci->io_addr, uhci->io_size);
	release_uhci(uhci);

	return retval;
}

static int found_uhci(struct pci_dev *dev)
{
	int i;

	/* Search for the IO base address.. */
	for (i = 0; i < 6; i++) {
		unsigned int io_addr = dev->resource[i].start;
		unsigned int io_size =
			dev->resource[i].end - dev->resource[i].start + 1;

		/* IO address? */
		if (!(dev->resource[i].flags & 1))
			continue;

		/* Is it already in use? */
		if (check_region(io_addr, io_size))
			break;

		/* disable legacy emulation */
		pci_write_config_word(dev, USBLEGSUP, USBLEGSUP_DEFAULT);

		pci_enable_device(dev);

		if (!dev->irq) {
			err("found UHCI device with no IRQ assigned. check BIOS settings!");
			continue;
		}

		return setup_uhci(dev->irq, io_addr, io_size);
	}

	return -1;
}

#ifdef CONFIG_APM
static int handle_apm_event(apm_event_t event)
{
	static int down = 0;

	switch (event) {
	case APM_SYS_SUSPEND:
	case APM_USER_SUSPEND:
		if (down) {
			dbg("received extra suspend event");
			break;
		}
		down = 1;
		break;
	case APM_NORMAL_RESUME:
	case APM_CRITICAL_RESUME:
		if (!down) {
			dbg("received bogus resume event");
			break;
		}
		down = 0;
		break;
	}
	return 0;
}
#endif

int uhci_init(void)
{
	int retval;
	struct pci_dev *dev;
	u8 type;

	retval = -ENOMEM;

	/* We throw all of the TD's and QH's into a kmem cache */
	/* TD's and QH's need to be 16 byte aligned and SLAB_HWCACHE_ALIGN */
	/*  does this for us */
	uhci_td_cachep = kmem_cache_create("uhci_td",
		sizeof(struct uhci_td), 0,
		SLAB_HWCACHE_ALIGN, NULL, NULL);

	if (!uhci_td_cachep)
		goto td_failed;

	uhci_qh_cachep = kmem_cache_create("uhci_qh",
		sizeof(struct uhci_qh), 0,
		SLAB_HWCACHE_ALIGN, NULL, NULL);

	if (!uhci_qh_cachep)
		goto qh_failed;

	retval = -ENODEV;
	dev = NULL;
	for (;;) {
		dev = pci_find_class(PCI_CLASS_SERIAL_USB << 8, dev);
		if (!dev)
			break;

		/* Is it the UHCI programming interface? */
		pci_read_config_byte(dev, PCI_CLASS_PROG, &type);
		if (type != 0)
			continue;

		/* Ok set it up */
		retval = found_uhci(dev);
	}

	/* We only want to return an error code if ther was an error */
	/*  and we didn't find a UHCI controller */
	if (retval && uhci_list.next == &uhci_list)
		goto init_failed;

#ifdef CONFIG_APM
	apm_register_callback(&handle_apm_event);
#endif

	return 0;

init_failed:
	if (kmem_cache_destroy(uhci_qh_cachep))
		printk(KERN_INFO "uhci: not all QH's were freed\n");

qh_failed:
	if (kmem_cache_destroy(uhci_td_cachep))
		printk(KERN_INFO "uhci: not all TD's were freed\n");

td_failed:
	return retval;
}

void uhci_cleanup(void)
{
	struct list_head *next, *tmp, *head = &uhci_list;
	unsigned long flags;

	tmp = head->next;
	while (tmp != head) {
		struct uhci *uhci = list_entry(tmp, struct uhci, uhci_list);

		next = tmp->next;

		list_del(&uhci->uhci_list);
		INIT_LIST_HEAD(&uhci->uhci_list);

		if (uhci->bus->root_hub)
			usb_disconnect(&uhci->bus->root_hub);

		usb_deregister_bus(uhci->bus);

		reset_hc(uhci);
		release_region(uhci->io_addr, uhci->io_size);

		/* Free any outstanding TD's and QH's */
		spin_lock_irqsave(&uhci->freelist_lock, flags);
		uhci_free_pending(uhci);
		spin_unlock_irqrestore(&uhci->freelist_lock, flags);

		release_uhci(uhci);

		tmp = next;
	}

	if (kmem_cache_destroy(uhci_qh_cachep))
		printk(KERN_INFO "uhci: not all QH's were freed\n");

	if (kmem_cache_destroy(uhci_td_cachep))
		printk(KERN_INFO "uhci: not all TD's were freed\n");
}

#ifdef MODULE
int init_module(void)
{
	return uhci_init();
}

void cleanup_module(void)
{
#ifdef CONFIG_APM
	apm_unregister_callback(&handle_apm_event);
#endif
	uhci_cleanup();
}
#endif //MODULE

