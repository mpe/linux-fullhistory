/*
 * Universal Host Controller Interface driver for USB.
 *
 * (C) Copyright 1999 Linus Torvalds
 * (C) Copyright 1999 Johannes Erdfelt
 * (C) Copyright 1999 Randy Dunlap
 *
 * Intel documents this fairly well, and as far as I know there
 * are no royalties or anything like that, but even so there are
 * people who decided that they want to do the same thing in a
 * completely different way.
 *
 * Oh, well. The intel version is the more common by far. As such,
 * that's the one I care about right now.
 *
 * WARNING! The USB documentation is downright evil. Most of it
 * is just crap, written by a committee. You're better off ignoring
 * most of it, the important stuff is:
 *  - the low-level protocol (fairly simple but lots of small details)
 *  - working around the horridness of the rest
 */

/* 4/4/1999 added data toggle for interrupt pipes -keryan */
/* 5/16/1999 added global toggles for bulk and control */
/* 6/25/1999 added fix for data toggles on bidirectional bulk endpoints */ 
/*
 * 1999-09-02: Thomas Sailer <sailer@ife.ee.ethz.ch>
 *             Added explicit frame list manipulation routines
 *             for inserting/removing iso td's to/from the frame list.
 *             START_ABSOLUTE fixes
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

#include "uhci.h"

#ifdef CONFIG_APM
#include <linux/apm_bios.h>
static int handle_apm_event(apm_event_t event);
static int apm_resume = 0;
#endif

static int uhci_debug = 1;

#define compile_assert(x) do { switch (0) { case 1: case !(x): } } while (0)

static DECLARE_WAIT_QUEUE_HEAD(uhci_configure);

static kmem_cache_t *uhci_td_cachep;
static kmem_cache_t *uhci_qh_cachep;

static LIST_HEAD(uhci_list);

#define UHCI_DEBUG

/*
 * function prototypes
 */

static int uhci_get_current_frame_number(struct usb_device *usb_dev);

static int uhci_init_isoc(struct usb_device *usb_dev,
				unsigned int pipe,
				int frame_count,
				void *context,
				struct usb_isoc_desc **isocdesc);

static void uhci_free_isoc(struct usb_isoc_desc *isocdesc);

static int uhci_run_isoc(struct usb_isoc_desc *isocdesc,
				struct usb_isoc_desc *pr_isocdesc);

static int uhci_kill_isoc(struct usb_isoc_desc *isocdesc);

/*
 * Map status to standard result codes
 *
 * <status> is (td->status & 0xFE0000) [a.k.a. uhci_status_bits(td->status)]
 * <dir_out> is True for output TDs and False for input TDs.
 */
static int uhci_map_status(int status, int dir_out)
{
	if (!status)
		return USB_ST_NOERROR;
	if (status & TD_CTRL_BITSTUFF)			/* Bitstuff error */
		return USB_ST_BITSTUFF;
	if (status & TD_CTRL_CRCTIMEO) {		/* CRC/Timeout */
		if (dir_out)
			return USB_ST_NORESPONSE;
		else
			return USB_ST_CRC;
	}
	if (status & TD_CTRL_NAK)			/* NAK */
		return USB_ST_TIMEOUT;
	if (status & TD_CTRL_BABBLE)			/* Babble */
		return USB_ST_STALL;
	if (status & TD_CTRL_DBUFERR)			/* Buffer error */
		return USB_ST_BUFFERUNDERRUN;
	if (status & TD_CTRL_STALLED)			/* Stalled */
		return USB_ST_STALL;
	if (status & TD_CTRL_ACTIVE)			/* Active */
		return USB_ST_NOERROR;

	return USB_ST_INTERNALERROR;
}

/*
 * Return the result of a TD..
 */
static int uhci_td_result(struct uhci_device *dev, struct uhci_td *td, unsigned long *rval)
{
	unsigned int status;
	struct uhci_td *tmp;
	int count = 1000, actlength, explength;

	/* Start at the TD first in the chain, if possible */
	if (td->qh && td->qh->first)
		tmp = td->qh->first;
	else
		tmp = td;

	if (!tmp)
		return USB_ST_INTERNALERROR;

	if (rval)
		 *rval = 0;

	/* Locate the first failing td, if any */
	do {
		status = uhci_status_bits(tmp->status);

		if (status)
			break;

		/* The length field is only valid if the TD was completed */
		if (!(tmp->status & TD_CTRL_ACTIVE) && uhci_packetin(tmp->info)) {
			explength = uhci_expected_length(tmp->info);
			actlength = uhci_actual_length(tmp->status);
			if (rval)
				*rval += actlength;

			if (explength != actlength &&
			   ((tmp->pipetype == PIPE_BULK) || (tmp->pipetype == PIPE_CONTROL))) {
				/* If the packet is short, none of the */
				/*  packets after this were processed, so */
				/*  fix the DT accordingly */
				if (in_interrupt() || uhci_debug)
					printk(KERN_DEBUG "Set toggle from %p rval %ld%c for status=%x to %d, exp=%d, act=%d\n",
						tmp, rval ? *rval : 0,
						rval ? '*' : '/', tmp->status,
						uhci_toggle(tmp->info) ^ 1,
						explength, actlength);
				usb_settoggle(dev->usb, uhci_endpoint(tmp->info),
					uhci_packetout(tmp->info),
					uhci_toggle(tmp->info) ^ 1);
				break;	/* Short packet */
			}
		}

		if ((tmp->link & UHCI_PTR_TERM) || (tmp->link & UHCI_PTR_QH))
			break;

		tmp = uhci_ptr_to_virt(tmp->link);
	} while (--count);

	if (!count) {
		printk(KERN_ERR "runaway td's in uhci_td_result!\n");
	} else {
		/* If we got to the last TD */

		/* No error */
		if (!status)
			return USB_ST_NOERROR;

		/* APC BackUPS Pro kludge */
		/* It tries to send all of the descriptor instead of */
		/*  the amount we requested */
		if (tmp->status & TD_CTRL_IOC &&
		    tmp->status & TD_CTRL_ACTIVE &&
		    tmp->status & TD_CTRL_NAK &&
		    tmp->pipetype == PIPE_CONTROL)
			return USB_ST_NOERROR;

		/* We got to an error, but the controller hasn't finished */
		/*  with it yet */
		if (tmp->status & TD_CTRL_ACTIVE)
			return USB_ST_NOCHANGE;

		/* If this wasn't the last TD and SPD is set, ACTIVE */
		/*  is not and NAK isn't then we received a short */
		/*  packet */
		if (tmp->status & TD_CTRL_SPD && !(tmp->status & TD_CTRL_NAK))
			return USB_ST_NOERROR;
	}

	/* Some debugging code */
	if (!count || (!in_interrupt() && uhci_debug)) {
		printk(KERN_DEBUG "uhci_td_result() failed with status %x\n",
			status);

		/* Print the chain for debugging purposes */
		if (td->qh)
			uhci_show_queue(td->qh);
		else
			uhci_show_td(td);
	}

	if (status & TD_CTRL_STALLED) {
		/* endpoint has stalled - mark it halted */
		usb_endpoint_halt(dev->usb, uhci_endpoint(tmp->info),
	    			uhci_packetout(tmp->info));
		return USB_ST_STALL;
	}

	if ((status == TD_CTRL_ACTIVE) && (!rval))
		return USB_ST_DATAUNDERRUN;

	return uhci_map_status(status, uhci_packetout(tmp->info));
}

/*
 * Inserts a td into qh list at the top.
 *
 * Careful about atomicity: even on UP this
 * requires a locked access due to the concurrent
 * DMA engine.
 *
 * NOTE! This assumes that first->last is a valid
 * list of TD's with the proper backpointers set
 * up and all..
 */
static void uhci_insert_tds_in_qh(struct uhci_qh *qh, struct uhci_td *first, struct uhci_td *last)
{
	unsigned int link = qh->element;
	unsigned int new = virt_to_bus(first) | UHCI_PTR_DEPTH;

	for (;;) {
		unsigned char success;

		last->link = link;
		first->backptr = &qh->element;
		asm volatile("lock ; cmpxchg %4,%2 ; sete %0"
			:"=q" (success), "=a" (link)
			:"m" (qh->element), "1" (link), "r" (new)
			:"memory");

		if (success) {
			/* Was there a successor entry? Fix it's backpointer */
			if ((link & UHCI_PTR_TERM) == 0) {
				struct uhci_td *next = uhci_ptr_to_virt(link);
				next->backptr = &last->link;
			}
			break;
		}
	}

	qh->first = first;
	first->qh = qh;
	last->qh = qh;
}

static inline void uhci_insert_td_in_qh(struct uhci_qh *qh, struct uhci_td *td)
{
	uhci_insert_tds_in_qh(qh, td, td);
}

static void uhci_insert_qh(struct uhci_qh *qh, struct uhci_qh *newqh)
{
	newqh->link = qh->link;
	qh->link = virt_to_bus(newqh) | UHCI_PTR_QH;
}

static void uhci_remove_qh(struct uhci_qh *qh, struct uhci_qh *remqh)
{
	struct uhci_qh *lqh = qh;

	while (uhci_ptr_to_virt(lqh->link) != remqh) {
		if (lqh->link & UHCI_PTR_TERM)
			break;

		lqh = (struct uhci_qh *)uhci_ptr_to_virt(lqh->link);
	}

	if (lqh->link & UHCI_PTR_TERM) {
		printk(KERN_DEBUG "couldn't find qh in chain!\n");
		return;
	}

	lqh->link = remqh->link;
}

/*
 * Removes td from qh if present.
 *
 * NOTE! We keep track of both forward and back-pointers,
 * so this should be trivial, right?
 *
 * Wrong. While all TD insert/remove operations are synchronous
 * on the CPU, the UHCI controller can (and does) play with the
 * very first forward pointer. So we need to validate the backptr
 * before we change it, so that we don't by mistake reset the QH
 * head to something old.
 */
static void uhci_remove_td(struct uhci_td *td)
{
	unsigned int *backptr = td->backptr;
	unsigned int link = td->link;
	unsigned int me;

	if (!backptr)
		return;

	td->backptr = NULL;

	/*
	 * This is the easy case: the UHCI will never change "td->link",
	 * so we can always just look at that and fix up the backpointer
	 * of any next element..
	 */
	if (!(link & UHCI_PTR_TERM)) {
		struct uhci_td *next = uhci_ptr_to_virt(link);
		next->backptr = backptr;
	}

	/*
	 * The nasty case is "backptr->next", which we need to
	 * update to "link" _only_ if "backptr" still points
	 * to us (it may not: maybe backptr is a QH->element
	 * pointer and the UHCI has changed the value).
	 */
	me = virt_to_bus(td) | (0xe & *backptr);
	asm volatile("lock ; cmpxchg %0,%1"
		:
		:"r" (link), "m" (*backptr), "a" (me)
		:"memory");

	/* Reset it just in case */
	td->link = UHCI_PTR_TERM;
}

/*
 * Only the USB core should call uhci_alloc_dev and uhci_free_dev
 */
static int uhci_alloc_dev(struct usb_device *usb_dev)
{
	struct uhci_device *dev;

	/* Allocate the UHCI device private data */
	dev = kmalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -1;

	/* Initialize "dev" */
	memset(dev, 0, sizeof(*dev));

	usb_dev->hcpriv = dev;
	dev->usb = usb_dev;
	atomic_set(&dev->refcnt, 1);

	if (usb_dev->parent)
		dev->uhci = usb_to_uhci(usb_dev->parent)->uhci;

	return 0;
}

static int uhci_free_dev(struct usb_device *usb_dev)
{
	struct uhci_device *dev = usb_to_uhci(usb_dev);

	if (atomic_dec_and_test(&dev->refcnt))
		kfree(dev);

	return 0;
}

static void uhci_inc_dev_use(struct uhci_device *dev)
{
	atomic_inc(&dev->refcnt);
}

static void uhci_dec_dev_use(struct uhci_device *dev)
{
	uhci_free_dev(dev->usb);
}

static struct uhci_td *uhci_td_alloc(struct uhci_device *dev)
{
	struct uhci_td *td;

	td = kmem_cache_alloc(uhci_td_cachep, SLAB_KERNEL);
	if (!td)
		return NULL;

#ifdef UHCI_DEBUG
	if ((__u32)td & UHCI_PTR_BITS)
		printk("qh not 16 byte aligned!\n");
#endif

	td->link = UHCI_PTR_TERM;
	td->buffer = 0;

	td->backptr = NULL;
	td->qh = NULL;
	td->dev_id = NULL;
	td->dev = dev;
	td->flags = 0;
	INIT_LIST_HEAD(&td->irq_list);
	atomic_set(&td->refcnt, 1);

	uhci_inc_dev_use(dev);

	return td;
}

static void uhci_td_free(struct uhci_td *td)
{
	if (atomic_dec_and_test(&td->refcnt)) {
		kmem_cache_free(uhci_td_cachep, td);

		if (td->dev)
			uhci_dec_dev_use(td->dev);
	}
}

static struct uhci_qh *uhci_qh_alloc(struct uhci_device *dev)
{
	struct uhci_qh *qh;

	qh = kmem_cache_alloc(uhci_qh_cachep, SLAB_KERNEL);
	if (!qh)
		return NULL;

#ifdef UHCI_DEBUG
	if ((__u32)qh & UHCI_PTR_BITS)
		printk("qh not 16 byte aligned!\n");
#endif

	qh->element = UHCI_PTR_TERM;
	qh->link = UHCI_PTR_TERM;

	qh->dev = dev;
	qh->skel = NULL;
	qh->first = NULL;
	init_waitqueue_head(&qh->wakeup);
	atomic_set(&qh->refcnt, 1);

	uhci_inc_dev_use(dev);

	return qh;
}

static void uhci_qh_free(struct uhci_qh *qh)
{
	if (atomic_dec_and_test(&qh->refcnt)) {
		kmem_cache_free(uhci_qh_cachep, qh);

		if (qh->dev)
			uhci_dec_dev_use(qh->dev);
	}
}

/*
 * UHCI interrupt list operations..
 */
static spinlock_t irqlist_lock = SPIN_LOCK_UNLOCKED;

static void uhci_add_irq_list(struct uhci *uhci, struct uhci_td *td, usb_device_irq completed, void *dev_id)
{
	unsigned long flags;

	td->completed = completed;
	td->dev_id = dev_id;

	spin_lock_irqsave(&irqlist_lock, flags);
	list_add(&td->irq_list, &uhci->interrupt_list);
	spin_unlock_irqrestore(&irqlist_lock, flags);
}

static void uhci_remove_irq_list(struct uhci_td *td)
{
	unsigned long flags;

	spin_lock_irqsave(&irqlist_lock, flags);
	list_del(&td->irq_list);
	spin_unlock_irqrestore(&irqlist_lock, flags);
}

/*
 * frame list manipulation. Used for Isochronous transfers.
 * the list of (iso) TD's enqueued in a frame list entry
 * is basically a doubly linked list with link being
 * the forward pointer and backptr the backward ptr.
 * the frame list entry itself doesn't have a back ptr
 * (therefore the list is not circular), and the forward pointer
 * stops at link entries having the UHCI_PTR_TERM or the UHCI_PTR_QH
 * bit set. Maybe it could be extended to handle the QH's also,
 * but it doesn't seem necessary right now.
 * The layout looks as follows:
 * frame list pointer -> iso td's (if any) ->
 * periodic interrupt td (if framelist 0) -> irq qh -> control qh -> bulk qh
 */

static spinlock_t framelist_lock = SPIN_LOCK_UNLOCKED;

static void uhci_add_frame_list(struct uhci *uhci, struct uhci_td *td, unsigned framenum)
{
	unsigned long flags;
	struct uhci_td *nexttd;

	framenum %= UHCI_NUMFRAMES;
	spin_lock_irqsave(&framelist_lock, flags);
	td->backptr = &uhci->fl->frame[framenum];
	td->link = uhci->fl->frame[framenum];
	if (!(td->link & (UHCI_PTR_TERM | UHCI_PTR_QH))) {
		nexttd = (struct uhci_td *)uhci_ptr_to_virt(td->link);
		nexttd->backptr = &td->link;
	}
	wmb();
	uhci->fl->frame[framenum] = virt_to_bus(td);
	spin_unlock_irqrestore(&framelist_lock, flags);
}

static void uhci_remove_frame_list(struct uhci *uhci, struct uhci_td *td)
{
	unsigned long flags;
	struct uhci_td *nexttd;

	if (!td->backptr)
		return;
	spin_lock_irqsave(&framelist_lock, flags);
	*(td->backptr) = td->link;
	if (!(td->link & (UHCI_PTR_TERM | UHCI_PTR_QH))) {
		nexttd = (struct uhci_td *)uhci_ptr_to_virt(td->link);
		nexttd->backptr = td->backptr;
	}
	spin_unlock_irqrestore(&framelist_lock, flags);
	td->backptr = NULL;
	/*
	 * attention: td->link might still be in use by the
	 * hardware if the td is still active and the hardware
	 * was processing it. So td->link should be preserved
	 * until the frame number changes. Don't know what to do...
	 * udelay(1000) doesn't sound nice, and schedule()
	 * can't be used as this is called from within interrupt context.
	 */
	/*
	 * we should do the same thing as we do with the QH's
	 * see uhci_insert_tds_in_qh and uhci_remove_td --jerdfelt
	 */
	/* for now warn if there's a possible problem */
	if (td->status & TD_CTRL_ACTIVE) {
		unsigned frn = inw(uhci->io_addr + USBFRNUM);
		__u32 link = uhci->fl->frame[frn % UHCI_NUMFRAMES];
		if (!(link & (UHCI_PTR_TERM | UHCI_PTR_QH))) {
			struct uhci_td *tdl = (struct uhci_td *)uhci_ptr_to_virt(link);
			for (;tdl;) {
				if (tdl == td) {
					printk(KERN_WARNING "uhci_remove_frame_list: td possibly still in use!!\n");
					break;
				}
				if (tdl->link & (UHCI_PTR_TERM | UHCI_PTR_QH))
					break;
				tdl = (struct uhci_td *)uhci_ptr_to_virt(tdl->link);
			}
		}
	}
}


/*
 * This function removes and disallocates all structures set up for a transfer.
 * It takes the qh out of the skeleton, removes the tq and the td's.
 * It only removes the associated interrupt handler if removeirq is set.
 * The *td argument is any td in the list of td's.
 */
static void uhci_remove_transfer(struct uhci_td *td, char removeirq)
{
	int count = 1000;
	struct uhci_td *curtd;
	unsigned int nextlink;

	if (td->qh && td->qh->first)
		curtd = td->qh->first;
	else
		curtd = td;

	/* Remove the QH from the skeleton and then free it */
	uhci_remove_qh(td->qh->skel, td->qh);
	uhci_qh_free(td->qh);

	do {
		nextlink = curtd->link;

		/* IOC? => remove handler */
		/* HACK: Don't remove if already removed. Prevents deadlock */
		/*  in uhci_interrupt_notify and callbacks */
		if (removeirq && (td->status & TD_CTRL_IOC) &&
		    td->irq_list.next != &td->irq_list)
			uhci_remove_irq_list(td);

		/* Remove the TD and then free it */
		uhci_remove_td(curtd);
		uhci_td_free(curtd);

		if (nextlink & UHCI_PTR_TERM)	/* Tail? */
			break;

		curtd = (struct uhci_td *)uhci_ptr_to_virt(nextlink & ~UHCI_PTR_BITS);
	} while (count--);

	if (!count)
		printk(KERN_ERR "runaway td's!?\n");
}

/*
 * Request an interrupt handler..
 *
 * Returns 0 (success) or negative (failure).
 * Also returns/sets a "handle pointer" that release_irq can use to stop this
 * interrupt.  (It's really a pointer to the TD.)
 */
static int uhci_request_irq(struct usb_device *usb_dev, unsigned int pipe,
			usb_device_irq handler, int period,
			void *dev_id, void **handle, long bustime)
{
	struct uhci_device *dev = usb_to_uhci(usb_dev);
	struct uhci_td *td = uhci_td_alloc(dev);
	struct uhci_qh *qh = uhci_qh_alloc(dev);
	unsigned int destination, status;

	if (!td || !qh) {
		if (td)
			uhci_td_free(td);
		if (qh)
			uhci_qh_free(qh);
		return USB_ST_INTERNALERROR;
	}

	/* Destination: pipe destination with INPUT */
	destination = (pipe & PIPE_DEVEP_MASK) | usb_packetid(pipe);

	/* Infinite errors is 0, so no bits */
	status = (pipe & TD_CTRL_LS) | TD_CTRL_IOC | TD_CTRL_ACTIVE |
			TD_CTRL_SPD;

	td->link = UHCI_PTR_TERM;		/* Terminate */
	td->status = status;			/* In */
	td->info = destination | ((usb_maxpacket(usb_dev, pipe, usb_pipeout(pipe)) - 1) << 21) |
		(usb_gettoggle(usb_dev, usb_pipeendpoint(pipe), usb_pipeout(pipe)) << TD_TOKEN_TOGGLE);

	td->buffer = virt_to_bus(dev->data);
	td->qh = qh;
	td->dev = dev;
	td->pipetype = PIPE_INTERRUPT;
	td->bandwidth_alloc = bustime;

	/* if period 0, set _REMOVE flag */
	if (!period)
		td->flags |= UHCI_TD_REMOVE;

	qh->skel = &dev->uhci->skelqh[__interval_to_skel(period)];

	uhci_add_irq_list(dev->uhci, td, handler, dev_id);

	uhci_insert_td_in_qh(qh, td);

	/* Add it into the skeleton */
	uhci_insert_qh(qh->skel, qh);

	*handle = (void *)td;

	return 0;
}

/*
 * Release an interrupt handler previously allocated using
 * uhci_request_irq.  This function does no validity checking, so make
 * sure you're not releasing an already released handle as it may be
 * in use by something else..
 *
 * This function can NOT be called from an interrupt.
 */
static int uhci_release_irq(struct usb_device *usb, void *handle)
{
	struct uhci_td *td;
	struct uhci_qh *qh;

	td = (struct uhci_td *)handle;
	if (!td)
		return USB_ST_INTERNALERROR;

	qh = td->qh;

	/* Remove it from the internal irq_list */
	uhci_remove_irq_list(td);

	/* Remove the interrupt TD and QH */
	uhci_remove_td(td);
	uhci_remove_qh(qh->skel, qh);

	if (td->completed != NULL)
		td->completed(USB_ST_REMOVED, NULL, 0, td->dev_id);

	/* Free the TD and QH */
	uhci_td_free(td);
	uhci_qh_free(qh);

	return USB_ST_NOERROR;
} /* uhci_release_irq() */

/*
 * uhci_get_current_frame_number()
 *
 * returns the current frame number for a USB bus/controller.
 */
static int uhci_get_current_frame_number(struct usb_device *usb_dev)
{
       return inw (usb_to_uhci(usb_dev)->uhci->io_addr + USBFRNUM);
}


/*
 * uhci_init_isoc()
 *
 * Allocates some data structures.
 * Initializes parts of them from the function parameters.
 *
 * It does not associate any data/buffer pointers or
 * driver (caller) callback functions with the allocated
 * data structures.  Such associations are left until
 * uhci_run_isoc().
 *
 * Returns 0 for success or negative value for error.
 * Sets isocdesc before successful return.
 */
static int uhci_init_isoc(struct usb_device *usb_dev,
				unsigned int pipe,
				int frame_count,        /* bandwidth % = 100 * this / 1024 */
				void *context,
				struct usb_isoc_desc **isocdesc)
{
	struct usb_isoc_desc *id;
	int i;

	*isocdesc = NULL;

	/* Check some parameters. */
	if ((frame_count <= 0) || (frame_count > UHCI_NUMFRAMES)) {
#ifdef CONFIG_USB_DEBUG_ISOC
		printk (KERN_DEBUG "uhci_init_isoc: invalid frame_count (%d)\n",
			frame_count);
#endif
		return -EINVAL;
	}

	if (!usb_pipeisoc (pipe)) {
#ifdef CONFIG_USB_DEBUG_ISOC
		printk (KERN_DEBUG "uhci_init_isoc: NOT an Isoc. pipe\n");
#endif
		return -EINVAL;
	}

	id = kmalloc (sizeof (*id) +
		(sizeof (struct isoc_frame_desc) * frame_count), GFP_KERNEL);
	if (!id)
		return -ENOMEM;

	memset (id, 0, sizeof (*id) +
		(sizeof (struct isoc_frame_desc) * frame_count));

	id->td = kmalloc (sizeof (struct uhci_td) * frame_count, GFP_KERNEL);
	if (!id->td) {
		kfree (id);
		return -ENOMEM;
	}

	memset (id->td, 0, sizeof (struct uhci_td) * frame_count);

	for (i = 0; i < frame_count; i++)
		INIT_LIST_HEAD(&((struct uhci_td *)(id->td))[i].irq_list);

	id->frame_count = frame_count;
	id->frame_size  = usb_maxpacket (usb_dev, pipe, usb_pipeout(pipe));
		/* TBD: or make this a parameter to allow for frame_size
		that is less than maxpacketsize */
	id->start_frame = -1;
	id->end_frame   = -1;
	id->usb_dev     = usb_dev;
	id->pipe        = pipe;
	id->context     = context;

	*isocdesc = id;
	return 0;
} /* end uhci_init_isoc */

/*
 * uhci_run_isoc()
 *
 * Associates data/buffer pointers/lengths and
 * driver (caller) callback functions with the
 * allocated Isoc. data structures and TDs.
 *
 * Then inserts the TDs into the USB controller frame list
 * for its processing.
 * And inserts the callback function into its TD.
 *
 * pr_isocdesc (previous Isoc. desc.) may be NULL.
 * It is used only for chaining one list of TDs onto the
 * end of the previous list of TDs.
 *
 * Returns 0 (success) or error code (negative value).
 */
static int uhci_run_isoc (struct usb_isoc_desc *isocdesc,
				struct usb_isoc_desc *pr_isocdesc)
{
	struct uhci_device *dev = usb_to_uhci (isocdesc->usb_dev);
	struct uhci *uhci = dev->uhci;
	unsigned long   destination, status;
	struct uhci_td  *td;
	int             ix, cur_frame, pipeinput, frlen;
	int             cb_frames = 0;
	struct isoc_frame_desc *fd;
	unsigned char   *bufptr;

	if (!isocdesc->callback_fn) {
#ifdef CONFIG_USB_DEBUG_ISOC
		printk (KERN_DEBUG "uhci_run_isoc: caller must have a callback function\n");
#endif
		return -EINVAL;
	}

	/* Check buffer size large enough for maxpacketsize * frame_count. */
	if (isocdesc->buf_size < (isocdesc->frame_count * isocdesc->frame_size)) {
#ifdef CONFIG_USB_DEBUG_ISOC
		printk (KERN_DEBUG "uhci_init_isoc: buf_size too small (%d < %d)\n",
			isocdesc->buf_size, isocdesc->frame_count * isocdesc->frame_size);
#endif
		return -EINVAL;
	}

	/* Check buffer ptr for Null. */
	if (!isocdesc->data) {
#ifdef CONFIG_USB_DEBUG_ISOC
		printk (KERN_DEBUG "uhci_init_isoc: data ptr is null\n");
#endif
		return -EINVAL;
	}

#ifdef NEED_ALIGNMENT
	/* Check data page alignment. */
	if (((int)(isocdesc->data) & (PAGE_SIZE - 1)) != 0) {
#ifdef CONFIG_USB_DEBUG_ISOC
		printk (KERN_DEBUG "uhci_init_isoc: buffer must be page-aligned (%p)\n",
			isocdesc->data);
#endif
		return -EINVAL;
	}
#endif /* NEED_ALIGNMENT */

	/*
	 * Check start_type unless pr_isocdesc is used.
	 */
	if (!pr_isocdesc && (isocdesc->start_type > START_TYPE_MAX)) {
#ifdef CONFIG_USB_DEBUG_ISOC
		printk (KERN_DEBUG "uhci_run_isoc: invalid start_type (%d)\n",
			isocdesc->start_type);
#endif
		return -EINVAL;
	}

	/*
	 * Check start_frame for inside a valid range.
	 * Only allow transfer requests to be made 1.000 second
	 * into the future.
	 */
	cur_frame = uhci_get_current_frame_number (isocdesc->usb_dev);

	/* if not START_ASAP (i.e., RELATIVE or ABSOLUTE): */
	if (!pr_isocdesc) {
		if (isocdesc->start_type == START_RELATIVE) {
			if ((isocdesc->start_frame < 0) || (isocdesc->start_frame > CAN_SCHEDULE_FRAMES)) {
#ifdef CONFIG_USB_DEBUG_ISOC
				printk (KERN_DEBUG "uhci_init_isoc: bad start_frame value (%d)\n",
					isocdesc->start_frame);
#endif
				return -EINVAL;
			}
		} /* end START_RELATIVE */
		else
		if (isocdesc->start_type == START_ABSOLUTE) { /* within the scope of cur_frame */
			ix = USB_WRAP_FRAMENR(isocdesc->start_frame - cur_frame);
			if (ix < START_FRAME_FUDGE || /* too small */
			    ix > CAN_SCHEDULE_FRAMES) { /* too large */
#ifdef CONFIG_USB_DEBUG_ISOC
					printk (KERN_DEBUG "uhci_init_isoc: bad start_frame value (%d,%d)\n",
						isocdesc->start_frame, cur_frame);
#endif
					return -EINVAL;
			}
		} /* end START_ABSOLUTE */
	} /* end not pr_isocdesc */

	/*
	 * Set the start/end frame numbers.
	 */
	if (pr_isocdesc) {
		isocdesc->start_frame = pr_isocdesc->end_frame + 1;
	} else if (isocdesc->start_type == START_RELATIVE) {
		if (isocdesc->start_frame < START_FRAME_FUDGE)
			isocdesc->start_frame = START_FRAME_FUDGE;
		isocdesc->start_frame += cur_frame;
	} else if (isocdesc->start_type == START_ASAP) {
		isocdesc->start_frame = cur_frame + START_FRAME_FUDGE;
	}

	/* and see if start_frame needs any correction */
	/* only wrap to USB frame numbers, the frame_list insertion routine
	   takes care of the wrapping to the frame_list size */
	isocdesc->start_frame = USB_WRAP_FRAMENR(isocdesc->start_frame);

	/* and fix the end_frame value */
	isocdesc->end_frame = USB_WRAP_FRAMENR(isocdesc->start_frame + isocdesc->frame_count - 1);

	isocdesc->prev_completed_frame = -1;
	isocdesc->cur_completed_frame  = -1;

	destination = (isocdesc->pipe & PIPE_DEVEP_MASK) |
		usb_packetid (isocdesc->pipe);
	status = TD_CTRL_ACTIVE | TD_CTRL_IOS;  /* mark Isoc.; can't be low speed */
	pipeinput = usb_pipein (isocdesc->pipe);
	cur_frame = isocdesc->start_frame;
	bufptr = isocdesc->data;

	/*
	 * Build the Data TDs.
	 * TBD: Not using frame_spacing (Yet).  Defaults to 1 (every frame).
	 * (frame_spacing is a way to request less bandwidth.)
	 * This can also be done by using frame_length = 0 in the
	 * frame_desc array, but this way won't take less bandwidth
	 * allocation into account.
	 */
	if (isocdesc->frame_spacing <= 0)
		isocdesc->frame_spacing = 1;

	for (ix = 0, td = isocdesc->td, fd = isocdesc->frames;
	     ix < isocdesc->frame_count; ix++, td++, fd++) {
		frlen = fd->frame_length;
		if (frlen > isocdesc->frame_size)
			frlen = isocdesc->frame_size;

#ifdef NOTDEF
		td->info = destination |  /* use Actual len on OUT; max. on IN */
			(pipeinput ? ((isocdesc->frame_size - 1) << 21)
			: ((frlen - 1) << 21));
#endif

		td->dev_id = isocdesc;  /* can get dev_id or context from isocdesc */
		td->status = status;
		td->info   = destination | ((frlen - 1) << 21);
		td->buffer = virt_to_bus (bufptr);
		td->dev    = dev;
		td->pipetype = PIPE_ISOCHRONOUS;
		td->isoc_td_number = ix;        /* 0-based; does not wrap/overflow back to 0 */

		if (isocdesc->callback_frames &&
		    (++cb_frames >= isocdesc->callback_frames)) {
			td->status |= TD_CTRL_IOC;
			td->completed = isocdesc->callback_fn;
			cb_frames = 0;
			uhci_add_irq_list (dev->uhci, td, isocdesc->callback_fn, isocdesc);
		}

		bufptr += fd->frame_length;  /* or isocdesc->frame_size; */

		/*
		 * Insert the TD in the frame list.
		 */
		uhci_add_frame_list(uhci, td, cur_frame);

		cur_frame = USB_WRAP_FRAMENR(cur_frame+1);
	} /* end for ix */

	/*
	 * Add IOC on the last TD.
	 */
	td--;
	if (!(td->status & TD_CTRL_IOC)) {
		td->status |= TD_CTRL_IOC;
		td->completed = isocdesc->callback_fn;
		uhci_add_irq_list(dev->uhci, td, isocdesc->callback_fn, isocdesc); /* TBD: D.K. ??? */
	}

	return 0;
} /* end uhci_run_isoc */

/*
 * uhci_kill_isoc()
 *
 * Marks a TD list as Inactive and removes it from the Isoc.
 * TD frame list.
 *
 * Does not free any memory resources.
 *
 * Returns 0 for success or negative value for error.
 */
static int uhci_kill_isoc (struct usb_isoc_desc *isocdesc)
{
	struct uhci_device *dev = usb_to_uhci (isocdesc->usb_dev);
	struct uhci     *uhci = dev->uhci;
	struct uhci_td  *td;
	int             ix;

	if (USB_WRAP_FRAMENR(isocdesc->start_frame) != isocdesc->start_frame) {
#ifdef CONFIG_USB_DEBUG_ISOC
		printk (KERN_DEBUG "uhci_kill_isoc: invalid start_frame (%d)\n",
			isocdesc->start_frame);
#endif
		return -EINVAL;
	}

	for (ix = 0, td = isocdesc->td; ix < isocdesc->frame_count; ix++, td++) {
		/* Deactivate and unlink */
		uhci_remove_frame_list(uhci, td);
		td->status &= ~(TD_CTRL_ACTIVE | TD_CTRL_IOC);
	} /* end for ix */

	isocdesc->start_frame = -1;
	return 0;
} /* end uhci_kill_isoc */

static void uhci_free_isoc(struct usb_isoc_desc *isocdesc)
{
	int i;

	/* If still Active, kill it. */
	if (isocdesc->start_frame >= 0)
		uhci_kill_isoc(isocdesc);

	/* Remove all TD's from the IRQ list. */
	for (i = 0; i < isocdesc->frame_count; i++)
		uhci_remove_irq_list(((struct uhci_td *)isocdesc->td) + i);

	/* Free the associate memory. */
	if (isocdesc->td)
		kfree(isocdesc->td);

	kfree(isocdesc);
} /* end uhci_free_isoc */

/*
 * Control thread operations: we just mark the last TD
 * in a control thread as an interrupt TD, and wake up
 * the front-end on completion.
 *
 * We need to remove the TD from the lists (both interrupt
 * list and TD lists) by hand if something bad happens!
 */

static int uhci_generic_completed(int status, void *buffer, int len, void *dev_id)
{
	wait_queue_head_t *wakeup = (wait_queue_head_t *)dev_id;

	if (waitqueue_active(wakeup))
		wake_up(wakeup);
	else
		printk("waitqueue empty!\n");

	return 0;			/* Don't re-instate */
}

/* td points to the last td in the list, which interrupts on completion */
static int uhci_run_control(struct uhci_device *dev, struct uhci_td *first, struct uhci_td *last, int timeout)
{
	DECLARE_WAITQUEUE(wait, current);
	struct uhci_qh *qh = uhci_qh_alloc(dev);
	unsigned long rval;
	int ret;

	if (!qh)
		return USB_ST_INTERNALERROR;

	current->state = TASK_UNINTERRUPTIBLE;
	add_wait_queue(&qh->wakeup, &wait);

	uhci_add_irq_list(dev->uhci, last, uhci_generic_completed, &qh->wakeup);
	
	uhci_insert_tds_in_qh(qh, first, last);

	/* Add it into the skeleton */
	uhci_insert_qh(&dev->uhci->skel_control_qh, qh);

	/* wait a user specified reasonable amount of time */
	schedule_timeout(timeout);

	remove_wait_queue(&qh->wakeup, &wait);

	/* Clean up in case it failed.. */
	uhci_remove_irq_list(last);

	/* Remove it from the skeleton */
	uhci_remove_qh(&dev->uhci->skel_control_qh, qh);

	/* Need to check result before free'ing the qh */
	ret = uhci_td_result(dev, last, &rval);

	uhci_qh_free(qh);

	return (ret < 0) ? ret : rval;
}

/*
 * Send or receive a control message on a pipe.
 *
 * Note that the "pipe" structure is set up to map
 * easily to the uhci destination fields.
 *
 * A control message is built up from three parts:
 *  - The command itself
 *  - [ optional ] data phase
 *  - Status complete phase
 *
 * The data phase can be an arbitrary number of TD's
 * although we currently had better not have more than
 * 29 TD's here (we have 31 TD's allocated for control
 * operations, and two of them are used for command and
 * status).
 *
 * 29 TD's is a minimum of 232 bytes worth of control
 * information, that's just ridiculously high. Most
 * control messages have just a few bytes of data.
 *
 * 232 is not ridiculously high with many of the
 * configurations on audio devices, etc. anyway,
 * there is no restriction on length of transfers
 * anymore
 */
static int uhci_control_msg(struct usb_device *usb_dev, unsigned int pipe, devrequest *cmd,
				void *data, int len, int timeout)
{
	struct uhci_device *dev = usb_to_uhci(usb_dev);
	struct uhci_td *first, *td, *prevtd;
	unsigned long destination, status;
	int ret, count;
	int maxsze = usb_maxpacket(usb_dev, pipe, usb_pipeout(pipe));
	__u32 nextlink;
	unsigned long bytesrequested = len;

	first = td = uhci_td_alloc(dev);
	if (!td)
		return USB_ST_INTERNALERROR;

	/* The "pipe" thing contains the destination in bits 8--18 */
	destination = (pipe & PIPE_DEVEP_MASK) | USB_PID_SETUP;

	/* 3 errors */
	status = (pipe & TD_CTRL_LS) | TD_CTRL_ACTIVE | TD_CTRL_SPD | (3 << 27);

	/*
	 * Build the TD for the control request
	 */
	td->status = status;				/* Try forever */
	td->info = destination | (7 << 21);		/* 8 bytes of data */
	td->buffer = virt_to_bus(cmd);
	td->pipetype = PIPE_CONTROL;

	/*
	 * If direction is "send", change the frame from SETUP (0x2D)
	 * to OUT (0xE1). Else change it from SETUP to IN (0x69).
	 */
	destination ^= (USB_PID_SETUP ^ USB_PID_IN);		/* SETUP -> IN */
	if (usb_pipeout(pipe))
		destination ^= (USB_PID_IN ^ USB_PID_OUT);	/* IN -> OUT */

	prevtd = td;
	td = uhci_td_alloc(dev);
	if (!td) {
		uhci_td_free(prevtd);
		return USB_ST_INTERNALERROR;
	}

	prevtd->link = virt_to_bus(td) | UHCI_PTR_DEPTH;

	/*
	 * Build the DATA TD's
	 */
	while (len > 0) {
		/* Build the TD for control status */
		int pktsze = len;

		if (pktsze > maxsze)
			pktsze = maxsze;

		/* Alternate Data0/1 (start with Data1) */
		destination ^= 1 << TD_TOKEN_TOGGLE;
	
		td->status = status;					/* Status */
		td->info = destination | ((pktsze - 1) << 21);		/* pktsze bytes of data */
		td->buffer = virt_to_bus(data);
		td->backptr = &prevtd->link;
		td->pipetype = PIPE_CONTROL;

		data += pktsze;
		len -= pktsze;

		prevtd = td;
		td = uhci_td_alloc(dev);
		if (!td)
			return USB_ST_INTERNALERROR;
		prevtd->link = virt_to_bus(td) | UHCI_PTR_DEPTH;	/* Update previous TD */
	}

	/*
	 * Build the final TD for control status 
	 *
	 * It's IN if the pipe is an output pipe or we're not expecting
	 * data back.
	 */
	destination &= ~0xFF;
	if (usb_pipeout(pipe) || !bytesrequested)
		destination |= USB_PID_IN;
	else
		destination |= USB_PID_OUT;

	destination |= 1 << TD_TOKEN_TOGGLE;		/* End in Data1 */

	td->status = status | TD_CTRL_IOC;			/* no limit on errors on final packet */
	td->info = destination | (UHCI_NULL_DATA_SIZE << 21);	/* 0 bytes of data */
	td->buffer = 0;
	td->backptr = &prevtd->link;
	td->link = UHCI_PTR_TERM;			/* Terminate */
	td->pipetype = PIPE_CONTROL;

	/* Start it up.. */
	ret = uhci_run_control(dev, first, td, timeout);

	count = 1000;
	td = first;
	do {
		nextlink = td->link;
		uhci_remove_td(td);
		uhci_td_free(td);

		if (nextlink & UHCI_PTR_TERM)	/* Tail? */
			break;

		td = uhci_ptr_to_virt(nextlink);
	} while (--count);

	if (!count)
		printk(KERN_ERR "runaway td's!?\n");

#ifdef UHCI_DEBUG
	if (ret >= 0 && ret != bytesrequested && bytesrequested)
		printk("requested %ld bytes, got %d\n", bytesrequested, ret); 
#endif

	return ret;
}

/*
 * Bulk thread operations: we just mark the last TD
 * in a bulk thread as an interrupt TD, and wake up
 * the front-end on completion.
 *
 * We need to remove the TD from the lists (both interrupt
 * list and TD lists) by hand if something bad happens!
 */

/* td points to the last td in the list, which interrupts on completion */
static int uhci_run_bulk(struct uhci_device *dev, struct uhci_td *first, struct uhci_td *last, unsigned long *rval, int timeout)
{
	DECLARE_WAITQUEUE(wait, current);
	struct uhci_qh *qh = uhci_qh_alloc(dev);
	int ret;

	if (!qh)
		return USB_ST_INTERNALERROR;

	current->state = TASK_UNINTERRUPTIBLE;
	add_wait_queue(&qh->wakeup, &wait);

	uhci_add_irq_list(dev->uhci, last, uhci_generic_completed, &qh->wakeup);
	
	uhci_insert_tds_in_qh(qh, first, last);

	/* Add it into the skeleton */
	uhci_insert_qh(&dev->uhci->skel_bulk_qh, qh);

	/* wait a user specified reasonable amount of time */
	schedule_timeout(timeout);

	remove_wait_queue(&qh->wakeup, &wait);

	/* Clean up in case it failed.. */
	uhci_remove_irq_list(last);

	uhci_remove_qh(&dev->uhci->skel_bulk_qh, qh);

	ret = uhci_td_result(dev, last, rval);

	uhci_qh_free(qh);

	return ret;
}

/*
 * Send or receive a bulk message on a pipe.
 *
 * Note that the "pipe" structure is set up to map
 * easily to the uhci destination fields.
 *
 * A bulk message is only built up from
 * the data phase
 */
static int uhci_bulk_msg(struct usb_device *usb_dev, unsigned int pipe, void *data, int len, unsigned long *rval, int timeout)
{
	struct uhci_device *dev = usb_to_uhci(usb_dev);
	struct uhci_td *first, *td, *prevtd, *curtd;
	unsigned long destination, status;
	unsigned int nextlink;
	int ret, count;
	int maxsze = usb_maxpacket(usb_dev, pipe, usb_pipeout(pipe));

	if (usb_endpoint_halted(usb_dev, usb_pipeendpoint(pipe), usb_pipeout(pipe)) &&
	    usb_clear_halt(usb_dev, usb_pipeendpoint(pipe) | (pipe & USB_DIR_IN)))
		return USB_ST_STALL;

	/* The "pipe" thing contains the destination in bits 8--18. */
	destination = (pipe & PIPE_DEVEP_MASK) | usb_packetid (pipe);

	/* 3 errors */
	status = (pipe & TD_CTRL_LS) | TD_CTRL_ACTIVE | TD_CTRL_SPD | (3 << 27);

	/*
	 * Build the TDs for the bulk request
	 */
        first = td = uhci_td_alloc(dev);
	if (!td)
		return USB_ST_INTERNALERROR;

	prevtd = first;		// This is fake, but at least it's not NULL
	while (len > 0) {
		/* Build the TD for control status */
		int pktsze = len;

		if (pktsze > maxsze)
			pktsze = maxsze;

		td->status = status;					/* Status */
		td->info = destination | ((pktsze-1) << 21) |
			(usb_gettoggle(usb_dev, usb_pipeendpoint(pipe),
				usb_pipeout(pipe)) << TD_TOKEN_TOGGLE); /* pktsze bytes of data */
		td->buffer = virt_to_bus(data);
		td->backptr = &prevtd->link;
		td->pipetype = PIPE_BULK;

		data += maxsze;
		len -= maxsze;

		if (len > 0) {
			prevtd = td;
			td = uhci_td_alloc(dev);
			if (!td)
				return USB_ST_INTERNALERROR;

			prevtd->link = virt_to_bus(td) | UHCI_PTR_DEPTH;/* Update previous TD */
		}

		/* Alternate Data0/1 (start with Data0) */
		usb_dotoggle(usb_dev, usb_pipeendpoint(pipe), usb_pipeout(pipe));
	}

	td->link = UHCI_PTR_TERM;			/* Terminate */
	td->status |= TD_CTRL_IOC;

	/* CHANGE DIRECTION HERE! SAVE IT SOMEWHERE IN THE ENDPOINT!!! */

	/* Start it up.. */
	ret = uhci_run_bulk(dev, first, td, rval, timeout);

	count = 1000;
	curtd = first;

	do {
		nextlink = curtd->link;
		uhci_remove_td(curtd);
		uhci_td_free(curtd);

		if (nextlink & UHCI_PTR_TERM)	/* Tail? */
			break;

		curtd = uhci_ptr_to_virt(nextlink);
	} while (--count);

	if (!count)
		printk(KERN_DEBUG "uhci: runaway td's!?\n");

	return ret < 0;
}

static void *uhci_request_bulk(struct usb_device *usb_dev, unsigned int pipe,
		usb_device_irq handler, void *data, int len, void *dev_id)
{
	struct uhci_device *dev = usb_to_uhci(usb_dev);
	struct uhci *uhci = dev->uhci;
	struct uhci_td *first, *td, *prevtd;
	struct uhci_qh *bulk_qh = uhci_qh_alloc(dev);
	unsigned long destination, status;
	int maxsze = usb_maxpacket(usb_dev, pipe, usb_pipeout(pipe));

	/* The "pipe" thing contains the destination in bits 8--18. */
	destination = (pipe & PIPE_DEVEP_MASK) | usb_packetid(pipe);

	/* Infinite errors is 0 */
	status = (pipe & TD_CTRL_LS) | TD_CTRL_ACTIVE | TD_CTRL_SPD;

	/* Build the TDs for the bulk request */
	first = td = uhci_td_alloc(dev);
	prevtd = td;
	while (len > 0) {
		/* Build the TD for control status */
		int pktsze = len;

		if (pktsze > maxsze)
			pktsze = maxsze;

		td->status = status;                                    /* Status */
		td->info = destination | ((pktsze-1) << 21) |
			(usb_gettoggle(usb_dev, usb_pipeendpoint(pipe), usb_pipeout(pipe)) << TD_TOKEN_TOGGLE); /* pktsze bytes of data */
		td->buffer = virt_to_bus(data);
		td->backptr = &prevtd->link;
		td->qh = bulk_qh;
		td->dev = dev;
		td->pipetype = PIPE_BULK;

		data += pktsze;
		len -= pktsze;

		if (len > 0) {
			prevtd = td;
			td = uhci_td_alloc(dev);
			prevtd->link = virt_to_bus(td) | UHCI_PTR_DEPTH;
		}

		/* Alternate Data0/1 */
		usb_dotoggle(usb_dev, usb_pipeendpoint(pipe), usb_pipeout(pipe));
	}

	first->backptr = NULL;
	td->link = 1;                           /* Terminate */
	td->status = status | TD_CTRL_IOC;	/* IOC */

	uhci_add_irq_list(dev->uhci, td, handler, dev_id);

	uhci_insert_tds_in_qh(bulk_qh, first, td);

	bulk_qh->skel = &uhci->skel_bulk_qh;
	uhci_insert_qh(&uhci->skel_bulk_qh, bulk_qh);

	/* Return last td for removal */
	return td;
}

/*
 * Remove a handler from a pipe. This terminates the transfer.
 * We have some assumptions here:
 * There is only one queue using this pipe. (the one we remove)
 * Any data that is in the queue is useless for us, we throw it away.
 */
static int uhci_terminate_bulk(struct usb_device *dev, void *first)
{
	/* none found? there is nothing to remove! */
	if (!first)
		return USB_ST_REMOVED;

	uhci_remove_transfer(first, 1);

	return USB_ST_NOERROR;
}

struct usb_operations uhci_device_operations = {
	uhci_alloc_dev,
	uhci_free_dev,
	uhci_control_msg,
	uhci_bulk_msg,
	uhci_request_irq,
	uhci_release_irq,
	uhci_request_bulk,
	uhci_terminate_bulk,
	uhci_get_current_frame_number,
	uhci_init_isoc,
	uhci_free_isoc,
	uhci_run_isoc,
	uhci_kill_isoc
};

/*
 * This is just incredibly fragile. The timings must be just
 * right, and they aren't really documented very well.
 *
 * Note the short delay between disabling reset and enabling
 * the port..
 */
static void uhci_reset_port(unsigned int port)
{
	unsigned short status;

	status = inw(port);
	outw(status | USBPORTSC_PR, port);		/* reset port */
	wait_ms(10);
	outw(status & ~USBPORTSC_PR, port);
	udelay(5);

	status = inw(port);
	outw(status | USBPORTSC_PE, port);		/* enable port */
	wait_ms(10);

	status = inw(port);
	if (!(status & USBPORTSC_PE)) {
		outw(status | USBPORTSC_PE, port);	/* one more try at enabling port */
		wait_ms(50);
	}

}

/*
 * This gets called if the connect status on the root
 * hub (and the root hub only) changes.
 */
static void uhci_connect_change(struct uhci *uhci, unsigned int port, unsigned int nr)
{
	struct usb_device *usb_dev;
	struct uhci_device *dev;
	unsigned short status;
	struct uhci_device *root_hub = usb_to_uhci(uhci->bus->root_hub);

#ifdef UHCI_DEBUG
	printk(KERN_INFO "uhci_connect_change: called for %d\n", nr);
#endif

	/*
	 * Even if the status says we're connected,
	 * the fact that the status bits changed may
	 * that we got disconnected and then reconnected.
	 *
	 * So start off by getting rid of any old devices..
	 */
	usb_disconnect(&root_hub->usb->children[nr]);

	status = inw(port);

	/* If we have nothing connected, then clear change status and disable the port */
	status = (status & ~USBPORTSC_PE) | USBPORTSC_PEC;
	if (!(status & USBPORTSC_CCS)) {
		outw(status, port);
		return;
	}

	/*
	 * Ok, we got a new connection. Allocate a device to it,
	 * and find out what it wants to do..
	 */
	usb_dev = usb_alloc_dev(root_hub->usb, root_hub->usb->bus);
	if (!usb_dev)
		return;
	
	dev = usb_dev->hcpriv;

	usb_connect(usb_dev);

	root_hub->usb->children[nr] = usb_dev;

	wait_ms(200); /* wait for powerup */
	uhci_reset_port(port);

	/* Get speed information */
	usb_dev->slow = (inw(port) & USBPORTSC_LSDA) ? 1 : 0;

	/*
	 * Ok, all the stuff specific to the root hub has been done.
	 * The rest is generic for any new USB attach, regardless of
	 * hub type.
	 */
	if (usb_new_device(usb_dev)) {
		unsigned short status = inw(port);

		printk(KERN_INFO "uhci: disabling port %d\n",
			nr + 1);
		outw(status & ~USBPORTSC_PE, port);
	}
}

/*
 * This gets called when the root hub configuration
 * has changed. Just go through each port, seeing if
 * there is something interesting happening.
 */
static void uhci_check_configuration(struct uhci *uhci)
{
	struct uhci_device *root_hub = usb_to_uhci(uhci->bus->root_hub);
	unsigned int io_addr = uhci->io_addr + USBPORTSC1;
	int maxchild = root_hub->usb->maxchild;
	int nr = 0;

	do {
		unsigned short status = inw(io_addr);

		if (status & USBPORTSC_CSC)
			uhci_connect_change(uhci, io_addr, nr);

		nr++; io_addr += 2;
	} while (nr < maxchild);
}

static int fixup_isoc_desc (struct uhci_td *td)
{
	struct usb_isoc_desc    *isocdesc = td->dev_id;
	struct uhci_td          *prtd;
	struct isoc_frame_desc  *frm;
	int                     first_comp = isocdesc->cur_completed_frame + 1; /* 0-based */
	int                     cur_comp = td->isoc_td_number;  /* 0-based */
	int                     ix, fx;
	int                     num_comp;

	if (first_comp >= isocdesc->frame_count)
		first_comp = 0;
	num_comp = cur_comp - first_comp + 1;

#ifdef CONFIG_USB_DEBUG_ISOC
	printk ("fixup_isoc_desc.1: td = %p, id = %p, first_comp = %d, cur_comp = %d, num_comp = %d\n",
			td, isocdesc, first_comp, cur_comp, num_comp);
#endif

	for (ix = 0, fx = first_comp, prtd = ((struct uhci_td *)(isocdesc->td))+first_comp, frm = &isocdesc->frames [first_comp];
	    ix < num_comp; ix++) {
		frm->frame_length = uhci_actual_length (prtd->status);
		isocdesc->total_length += frm->frame_length;

		if ((frm->frame_status = uhci_map_status (uhci_status_bits (prtd->status),
		    uhci_packetout (prtd->info))))
			isocdesc->error_count++;

		prtd++;
		frm++;
		if (++fx >= isocdesc->frame_count) {    /* wrap fx, prtd, and frm */
			fx = 0;
			prtd = isocdesc->td;
			frm  = isocdesc->frames;
		} /* end wrap */
	} /* end for */

	/*
 	 * Update some other fields for drivers.
	 */
	isocdesc->prev_completed_frame = isocdesc->cur_completed_frame;
	isocdesc->cur_completed_frame  = cur_comp;
	isocdesc->total_completed_frames += num_comp;   /* 1-based */

#ifdef CONFIG_USB_DEBUG_ISOC
	printk ("fixup_isoc_desc.2: total_comp_frames = %d, total_length = %d, error_count = %d\n",
		isocdesc->total_completed_frames, isocdesc->total_length, isocdesc->error_count);
#endif /* CONFIG_USB_DEBUG_ISOC */

	return 0;
}

static int uhci_isoc_callback(struct uhci *uhci, struct uhci_td *td, int status, unsigned long rval)
{
	struct usb_isoc_desc *isocdesc = td->dev_id;
	int ret;

	/*
	 * Fixup the isocdesc for the driver:  total_completed_frames,
	 * error_count, total_length, frames array.
	 *
	 * ret = callback_fn (int error_count, void *buffer,
	 * 		      int len, void *isocdesc);
	 */

	fixup_isoc_desc (td);

	ret = td->completed (isocdesc->error_count, bus_to_virt (td->buffer),
				isocdesc->total_length, isocdesc);

	/*
	 * Isoc. handling of return value from td->completed (callback function)
	 */

	switch (ret) {
	case CB_CONTINUE:       /* similar to the REMOVE condition below */
		/* TBD */
		uhci_td_free (td);
		break;

	case CB_REUSE:          /* similar to the re-add condition below,
				 * but Not ACTIVE */
		/* TBD */
		/* usb_dev = td->dev->usb; */

		/* Safe since uhci_interrupt_notify holds the lock */
		list_add(&td->irq_list, &uhci->interrupt_list);

		td->status = (td->status & (TD_CTRL_SPD | TD_CTRL_C_ERR_MASK |
				TD_CTRL_LS | TD_CTRL_IOS | TD_CTRL_IOC)) |
				TD_CTRL_IOC;

		/* The HC removes it, so re-add it */
		/* Insert into a QH? */
		uhci_insert_td_in_qh(td->qh, td);
		break;

	case CB_RESTART:        /* similar to re-add, but mark ACTIVE */
		/* TBD */
		/* usb_dev = td->dev->usb; */

		list_add(&td->irq_list, &uhci->interrupt_list);

		td->status = (td->status & (TD_CTRL_SPD | TD_CTRL_C_ERR_MASK |
				TD_CTRL_LS | TD_CTRL_IOS | TD_CTRL_IOC)) |
				TD_CTRL_ACTIVE | TD_CTRL_IOC;

		/* The HC removes it, so re-add it */
		uhci_insert_td_in_qh(td->qh, td);
		break;

	case CB_ABORT:          /* kill/abort */
		/* TBD */
		uhci_kill_isoc (isocdesc);
		break;
	} /* end isoc. TD switch */

	return 0;
}

static int uhci_bulk_callback(struct uhci *uhci, struct uhci_td *td, int status, unsigned long rval)
{
	if (td->completed(status, bus_to_virt(td->buffer), rval, td->dev_id)) {
		struct usb_device *usb_dev = td->dev->usb;

		/* This is safe since uhci_interrupt_notify holds the lock */
		list_add(&td->irq_list, &uhci->interrupt_list);

		/* Reset the status */
		td->status = (td->status & TD_CTRL_LS) | TD_CTRL_ACTIVE | TD_CTRL_IOC | TD_CTRL_SPD;

		/* Reset the info */
		td->info = (td->info & (PIPE_DEVEP_MASK | 0xFF | (TD_CTRL_ACTLEN_MASK << 21))) |
			(usb_gettoggle(usb_dev, uhci_endpoint(td->info), uhci_packetout(td->info)) << TD_TOKEN_TOGGLE); /* pktsze bytes of data */

		usb_dotoggle(usb_dev, uhci_endpoint(td->info), uhci_packetout(td->info));
		/* The HC only removes it when it completed */
		/* successfully, so force remove and re-add it */
		uhci_remove_td(td);
		uhci_insert_td_in_qh(td->qh, td);
	}

	return 0;
}

static int uhci_callback(struct uhci *uhci, struct uhci_td *td, int status, unsigned long rval)
{
	if (td->completed(status, bus_to_virt(td->buffer), rval, td->dev_id)) {
		struct usb_device *usb_dev = td->dev->usb;

		if (td->pipetype != PIPE_INTERRUPT)
			return 0;

		/* This is safe since uhci_interrupt_notify holds the lock */
		list_add(&td->irq_list, &uhci->interrupt_list);

		usb_dotoggle(usb_dev, uhci_endpoint(td->info), uhci_packetout(td->info));
		td->info &= ~(1 << TD_TOKEN_TOGGLE);	/* clear data toggle */
		td->info |= usb_gettoggle(usb_dev, uhci_endpoint(td->info),
			uhci_packetout(td->info)) << TD_TOKEN_TOGGLE;	/* toggle between data0 and data1 */
		td->status = (td->status & 0x2F000000) | TD_CTRL_ACTIVE | TD_CTRL_IOC;
		/* The HC only removes it when it completed */
		/* successfully, so force remove and re-add it */
		uhci_remove_td(td);
		uhci_insert_td_in_qh(td->qh, td);
	} else if (td->flags & UHCI_TD_REMOVE) {
		struct usb_device *usb_dev = td->dev->usb;

		/* marked for removal */
		td->flags &= ~UHCI_TD_REMOVE;
		usb_dotoggle(usb_dev, uhci_endpoint(td->info), uhci_packetout(td->info));
		uhci_remove_qh(td->qh->skel, td->qh);
		uhci_qh_free(td->qh);
		if (td->pipetype == PIPE_INTERRUPT)
			usb_release_bandwidth(usb_dev, td->bandwidth_alloc);
		uhci_td_free(td);
	}

	return 0;
}

static void uhci_interrupt_notify(struct uhci *uhci)
{
	struct list_head *tmp, *head = &uhci->interrupt_list;
	int status;

	spin_lock(&irqlist_lock);
	tmp = head->next;
	while (tmp != head) {
		struct uhci_td *td = list_entry(tmp, struct uhci_td, irq_list);
		unsigned long rval;

		tmp = tmp->next;

		/* We're interested if there was an error or if the chain of */
		/*  TD's completed successfully */
		status = uhci_td_result(td->dev, td, &rval);
		if (status == USB_ST_NOCHANGE)
			continue;

		/* remove from IRQ list */
		list_del(&td->irq_list);
		INIT_LIST_HEAD(&td->irq_list);

		switch (td->pipetype) {
		case PIPE_ISOCHRONOUS:
			uhci_isoc_callback(uhci, td, status, rval);
			break;
		case PIPE_BULK:
			uhci_bulk_callback(uhci, td, status, rval);
			break;
		default:
			uhci_callback(uhci, td, status, rval);
		}

		/* If completed does not wants to reactivate, then */
		/* it's responsible for free'ing the TD's and QH's */
		/* or another function (such as run_control) */
	}
	spin_unlock(&irqlist_lock);
}

/*
 * Check port status - Connect Status Change - for
 * each of the attached ports (defaults to two ports,
 * but at least in theory there can be more of them).
 *
 * Wake up the configurator if something happened, we
 * can't really do much at interrupt time.
 */
static void uhci_root_hub_events(struct uhci *uhci, unsigned int io_addr)
{
	if (waitqueue_active(&uhci_configure)) {
		struct uhci_device *root_hub = usb_to_uhci(uhci->bus->root_hub);
		int ports = root_hub->usb->maxchild;

		io_addr += USBPORTSC1;
		do {
			if (inw(io_addr) & USBPORTSC_CSC) {
				wake_up(&uhci_configure);
				return;
			}
			io_addr += 2;
		} while (--ports > 0);
	}
}

static void uhci_interrupt(int irq, void *__uhci, struct pt_regs *regs)
{
	struct uhci *uhci = __uhci;
	unsigned int io_addr = uhci->io_addr;
	unsigned short status;

	/*
	 * Read the interrupt status, and write it back to clear the
	 * interrupt cause
	 */
	status = inw(io_addr + USBSTS);
	if (!status)	/* shared interrupt, not mine */
		return;
	outw(status, io_addr + USBSTS);

	/* Walk the list of pending TD's to see which ones completed.. */
	uhci_interrupt_notify(uhci);

	/* Check if there are any events on the root hub.. */
	uhci_root_hub_events(uhci, io_addr);
}

/*
 * We init one packet, and mark it just IOC and _not_
 * active. Which will result in no actual USB traffic,
 * but _will_ result in an interrupt every second.
 *
 * Which is exactly what we want.
 */
static void uhci_init_ticktd(struct uhci *uhci)
{
	struct uhci_device *dev = usb_to_uhci(uhci->bus->root_hub);
	struct uhci_td *td = uhci_td_alloc(dev);

	if (!td) {
		printk(KERN_ERR "unable to allocate ticktd\n");
		return;
	}

	/* Don't clobber the frame */
	td->link = uhci->fl->frame[0];
	td->backptr = &uhci->fl->frame[0];
	td->status = TD_CTRL_IOC;
	/* (ignored) input packet, 0 bytes, device 127 */
	td->info = (UHCI_NULL_DATA_SIZE << 21) | (0x7f << 8) | USB_PID_IN;
	td->buffer = 0;
	td->qh = NULL;
	td->pipetype = -1;

	uhci->fl->frame[0] = virt_to_bus(td);

	uhci->ticktd = td;
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

	uhci_init_ticktd(uhci);

	/*
	 * Reset the HC - this will force us to get a
	 * new notification of any already connected
	 * ports due to the virtual disconnect that it
	 * implies.
	 */
	outw(USBCMD_HCRESET, io_addr + USBCMD);
	while (inw(io_addr + USBCMD) & USBCMD_HCRESET) {
		if (!--timeout) {
			printk(KERN_ERR "USBCMD_HCRESET timed out!\n");
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
 * Allocate a frame list, and four regular queues.
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
 *
 * We could certainly have multiple queues of the same
 * type, and maybe we should. We could have per-device
 * queues, for example. We begin small.
 *
 * Queues are dynamically allocated for devices now,
 * this code only sets up the skeleton queue
 */
static struct uhci *alloc_uhci(unsigned int io_addr, unsigned int io_size)
{
	int i, port;
	struct uhci *uhci;
	struct usb_bus *bus;
	struct uhci_device *dev;
	struct usb_device *usb;

	uhci = kmalloc(sizeof(*uhci), GFP_KERNEL);
	if (!uhci)
		return NULL;

	memset(uhci, 0, sizeof(*uhci));

	uhci->irq = -1;
	uhci->io_addr = io_addr;
	uhci->io_size = io_size;
	INIT_LIST_HEAD(&uhci->interrupt_list);

	/* We need exactly one page (per UHCI specs), how convenient */
	uhci->fl = (void *)__get_free_page(GFP_KERNEL);
	if (!uhci->fl)
		goto au_free_uhci;

	bus = usb_alloc_bus(&uhci_device_operations);
	if (!bus)
		goto au_free_fl;

	uhci->bus = bus;
	bus->hcpriv = uhci;

	/*
 	 * Allocate the root_hub
	 */
	usb = usb_alloc_dev(NULL, bus);
	if (!usb)
		goto au_free_bus;

	usb->bus = bus;

	dev = usb_to_uhci(usb);
	dev->uhci = uhci;

	uhci->bus->root_hub = uhci_to_usb(dev);

	/* Initialize the root hub */

	/* UHCI specs says devices must have 2 ports, but goes on to say */
	/* they may have more but give no way to determine how many they */
	/* have, so default to 2 */
	/* According to the UHCI spec, Bit 7 is always set to 1. So we try */
	/* to use this to our advantage */
	for (port = 0; port < (io_size - 0x10) / 2; port++) {
		unsigned int portstatus;

		portstatus = inw(io_addr + 0x10 + (port * 2));
		if (!(portstatus & 0x0080))
			break;
	}
	printk(KERN_DEBUG "Detected %d ports\n", port);

	/* This is experimental so anything less than 2 or greater than 8 is */
	/*  something weird and we'll ignore it */
	if (port < 2 || port > 8) {
		printk(KERN_DEBUG "Port count misdetected, forcing to 2 ports\n");
		port = 2;
	}

	usb->maxchild = port;
	usb_init_root_hub(usb);

	/*
	 * 9 Interrupt queues; link int2 thru int256 to int1 first,
	 * then link int1 to control and control to bulk
	 */
	for (i = 1; i < 9; i++) {
		struct uhci_qh *qh = &uhci->skelqh[i];

		qh->link = virt_to_bus(&uhci->skel_int1_qh) | UHCI_PTR_QH;
		qh->element = UHCI_PTR_TERM;
	}

	uhci->skel_int1_qh.link = virt_to_bus(&uhci->skel_control_qh) | UHCI_PTR_QH;
	uhci->skel_int1_qh.element = UHCI_PTR_TERM;

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
		struct uhci_qh *irq = &uhci->skel_int2_qh;
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
								if (i & 64) {
									irq++;
								}
							}
						}
					}
				}
			}
		}
		uhci->fl->frame[i] =  virt_to_bus(irq) | UHCI_PTR_QH;
	}

	return uhci;

/*
 * error exits:
 */

au_free_bus:
	usb_free_bus(bus);
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

	if (uhci->ticktd) {
		uhci_td_free(uhci->ticktd);
		uhci->ticktd = NULL;
	}

	if (uhci->fl) {
		free_page((unsigned long)uhci->fl);
		uhci->fl = NULL;
	}

	usb_free_bus(uhci->bus);
	kfree(uhci);
}

static int uhci_control_thread(void *__uhci)
{
	struct uhci *uhci = (struct uhci *)__uhci;

	uhci->control_running = 1;

	lock_kernel();

	/*
	 * This thread doesn't need any user-level access,
	 * so get rid of all our resources..
	 */
	exit_mm(current);
	exit_files(current);

	strcpy(current->comm, "uhci-control");

	/*
	 * Ok, all systems are go..
	 */
	do {
		siginfo_t info;
		int unsigned long signr;

#ifdef CONFIG_APM
		if (apm_resume) {
			apm_resume = 0;
			start_hc(uhci);
			continue;
		}
#endif
		uhci_check_configuration(uhci);

		interruptible_sleep_on(&uhci_configure);

		if (signal_pending(current)) {
			/* sending SIGUSR1 makes us print out some info */
			spin_lock_irq(&current->sigmask_lock);
			signr = dequeue_signal(&current->blocked, &info);
			spin_unlock_irq(&current->sigmask_lock);

			if (signr == SIGUSR1) {
				printk(KERN_DEBUG "UHCI queue dump:\n");
				uhci_show_queues(uhci);
			} else if (signr == SIGUSR2) {
				uhci_debug = !uhci_debug;
				printk(KERN_DEBUG "UHCI debug toggle = %x\n",
					uhci_debug);
			} else
				break;
		}
	} while (uhci->control_continue);

/*
	MOD_DEC_USE_COUNT;
*/

	uhci->control_running = 0;

	return 0;
}	

/*
 * If we've successfully found a UHCI, now is the time to increment the
 * module usage count, start the control thread, and return success..
 */
static int found_uhci(int irq, unsigned int io_addr, unsigned int io_size)
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

	uhci->control_continue = 1;

	retval = -EBUSY;
	if (request_irq(irq, uhci_interrupt, SA_SHIRQ, "uhci", uhci) == 0) {
		int pid;

		uhci->irq = irq;
		pid = kernel_thread(uhci_control_thread, uhci,
			CLONE_FS | CLONE_FILES | CLONE_SIGHAND);
		if (pid >= 0) {
			uhci->control_pid = pid;

			return(pid);
		}

		retval = pid;
	}

	reset_hc(uhci);
	release_region(uhci->io_addr, uhci->io_size);

	release_uhci(uhci);
	return retval;
}

static int start_uhci(struct pci_dev *dev)
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

		return found_uhci(dev->irq, io_addr, io_size);
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
			printk(KERN_DEBUG "uhci: received extra suspend event\n");
			break;
		}
		down = 1;
		break;
	case APM_NORMAL_RESUME:
	case APM_CRITICAL_RESUME:
		if (!down) {
			printk(KERN_DEBUG "uhci: received bogus resume event\n");
			break;
		}
		down = 0;
		if (waitqueue_active(&uhci_configure)) {
			apm_resume = 1;
			wake_up(&uhci_configure);
		}
		break;
	}
	return 0;
}
#endif

int uhci_init(void)
{
	int retval;
	struct pci_dev *dev = NULL;
	u8 type;

	uhci_td_cachep = kmem_cache_create("uhci_td",
		sizeof(struct uhci_td), 0,
		SLAB_HWCACHE_ALIGN, NULL, NULL);

	if (!uhci_td_cachep)
		return -ENOMEM;

	uhci_qh_cachep = kmem_cache_create("uhci_qh",
		sizeof(struct uhci_qh), 0,
		SLAB_HWCACHE_ALIGN, NULL, NULL);

	if (!uhci_qh_cachep)
		return -ENOMEM;

	retval = -ENODEV;
	for (;;) {
		dev = pci_find_class(PCI_CLASS_SERIAL_USB << 8, dev);
		if (!dev)
			break;

		/* Is it UHCI */
		pci_read_config_byte(dev, PCI_CLASS_PROG, &type);
		if (type != 0)
			continue;

		/* Ok set it up */
		retval = start_uhci(dev);
		if (retval < 0)
			continue;

#ifdef CONFIG_APM
		apm_register_callback(&handle_apm_event);
#endif
		return 0;
	}
	return retval;
}

void uhci_cleanup(void)
{
	struct list_head *next, *tmp, *head = &uhci_list;
	int ret, i;

	tmp = head->next;
	while (tmp != head) {
		struct uhci *uhci = list_entry(tmp, struct uhci, uhci_list);
		struct uhci_device *root_hub = usb_to_uhci(uhci->bus->root_hub);

		next = tmp->next;

		list_del(&uhci->uhci_list);
		INIT_LIST_HEAD(&uhci->uhci_list);

		/* Check if the process is still running */
		ret = kill_proc(uhci->control_pid, 0, 1);
		if (!ret) {
			/* Try a maximum of 10 seconds */
			int count = 10 * 100;

			uhci->control_continue = 0;
			wake_up(&uhci_configure);

			while (uhci->control_running && --count) {
				current->state = TASK_INTERRUPTIBLE;
				schedule_timeout(1);
			}

			if (!count)
				printk(KERN_ERR "uhci: giving up on killing uhci-control\n");
		}

		if (root_hub)
			for (i = 0; i < root_hub->usb->maxchild; i++)
				usb_disconnect(root_hub->usb->children + i);

		usb_deregister_bus(uhci->bus);

		reset_hc(uhci);
		release_region(uhci->io_addr, uhci->io_size);

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

