/*
 * Universal Host Controller Interface driver for USB.
 *
 * (C) Copyright 1999 Linus Torvalds
 * (C) Copyright 1999 Johannes Erdfelt
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

#include <asm/uaccess.h>
#include <asm/spinlock.h>
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
 * Map status to standard result codes
 */
static int uhci_map_status(int status, int dir_out)
{
	if (!status)
		return USB_ST_NOERROR;
	if (status & 0x02)				/* Bitstuff error*/
		return USB_ST_BITSTUFF;
	if (status & 0x04)	{			/* CRC/Timeout */
		if (dir_out)
    			return USB_ST_NORESPONSE;
		else
			return USB_ST_CRC;
	}
	if (status & 0x08)				/* NAK */
		return USB_ST_TIMEOUT;
	if (status & 0x10)				/* Babble */
		return USB_ST_STALL;
	if (status & 0x20)				/* Buffer error */
		return USB_ST_BUFFERUNDERRUN;
	if (status & 0x40)				/* Stalled */
		return USB_ST_STALL;
	if (status & 0x80)				/* Active */
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

	if (!td->qh)
		tmp = td;
	else
		tmp = uhci_ptr_to_virt(td->qh->element);

	if (rval)
		*rval = 0;

	/* locate the first failing td, if any */

	do {
		status = (tmp->status >> 16) & 0xff;
		if (status) {
			/* must reset the toggle on first error */
    			if (uhci_debug) {
				printk(KERN_DEBUG "Set toggle from %x rval %ld\n",
					(unsigned int)tmp, rval ? *rval : 0);
			}
			usb_settoggle(dev->usb, usb_pipeendpoint(tmp->info),
				usb_pipeout(tmp->info), (tmp->info >> 19) & 1);
			break;
		} else {
		    if (rval)
			*rval += (tmp->status & 0x3ff) + 1;
		}
		if ((tmp->link & UHCI_PTR_TERM) ||
		    (tmp->link & UHCI_PTR_QH))
			break;
		tmp = uhci_ptr_to_virt(tmp->link);
	} while (1);

	if (!status)
    		return USB_ST_NOERROR;

	/* Some debugging code */
	if (uhci_debug) {
		int count = 10;

		if (!td->qh)
			tmp = td;
		else
			tmp = uhci_ptr_to_virt(td->qh->element);
		printk(KERN_DEBUG "uhci_td_result() failed with status %x\n",
			status);
		do {
			show_td(tmp);
			if ((tmp->link & UHCI_PTR_TERM) ||
			    (tmp->link & UHCI_PTR_QH))
				break;
			tmp = uhci_ptr_to_virt(tmp->link);
		} while (--count);
	}

	if (status & 0x40) {
		/* endpoint has stalled - mark it halted */
		usb_endpoint_halt(dev->usb, usb_pipeendpoint(tmp->info));
		return USB_ST_STALL;
	}

	if (status == 0x80) {
		/* still active */
		if (!rval)
			return USB_ST_DATAUNDERRUN;
	}
	return uhci_map_status(status, usb_pipeout(tmp->info));		
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

		lqh = uhci_ptr_to_virt(lqh->link);
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

	return td;
}

static void uhci_td_free(struct uhci_td *td)
{
	if (atomic_dec_and_test(&td->refcnt))
		kmem_cache_free(uhci_td_cachep, td);
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
	init_waitqueue_head(&qh->wakeup);
	atomic_set(&qh->refcnt, 1);

	return qh;
}

static void uhci_qh_free(struct uhci_qh *qh)
{
	if (atomic_dec_and_test(&qh->refcnt))
		kmem_cache_free(uhci_qh_cachep, qh);
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
 * This function removes and disallcoates all structures set up for an transfer.
 * It takes the qh out of the skeleton, removes the tq and the td's.
 * It only removes the associated interrupt handler if removeirq ist set.
 * The *td argument is any td in the list of td's.
 */
static void uhci_remove_transfer(struct uhci_td *td, char removeirq)
{
	int maxcount = 1000;
	struct uhci_td *curtd;
	unsigned int nextlink;

	if (!td->qh)
		curtd = td;
	else
		curtd = uhci_ptr_to_virt(td->qh->element);

	/* Remove it from the skeleton */
	uhci_remove_qh(td->qh->skel, td->qh);
	uhci_qh_free(td->qh);
	do {
		nextlink = curtd->link;

		/* IOC? => remove handler */
		if (removeirq && (td->status & TD_CTRL_IOC))
			uhci_remove_irq_list(td);

		uhci_remove_td(curtd);
		uhci_td_free(curtd);
		if (nextlink & UHCI_PTR_TERM)	/* Tail? */
			break;

		curtd = bus_to_virt(nextlink & ~UHCI_PTR_BITS);
		if (!--maxcount) {
			printk(KERN_ERR "runaway td's!?\n");
			break;
		}
	} while (1);
}

/*
 * Request a interrupt handler..
 *
 * Returns: a "handle pointer" that release_irq can use to stop this
 * interrupt.  (It's really a pointer to the TD).
 */
static void *uhci_request_irq(struct usb_device *usb_dev, unsigned int pipe, usb_device_irq handler, int period, void *dev_id)
{
	struct uhci_device *dev = usb_to_uhci(usb_dev);
	struct uhci_td *td = uhci_td_alloc(dev);
	struct uhci_qh *qh = uhci_qh_alloc(dev);
	unsigned int destination, status;

	if (!td || !qh)
		return NULL;

	/* Destination: pipe destination with INPUT */
	destination = (pipe & PIPE_DEVEP_MASK) | usb_packetid(pipe);

	/* Infinite errors is 0, so no bits */
	status = (pipe & TD_CTRL_LS) | TD_CTRL_IOC | TD_CTRL_ACTIVE |
			TD_CTRL_SPD;

	td->link = UHCI_PTR_TERM;		/* Terminate */
	td->status = status;			/* In */
	td->info = destination | ((usb_maxpacket(usb_dev, pipe) - 1) << 21) |
		(usb_gettoggle(usb_dev, usb_pipeendpoint(pipe),
		usb_pipeout(pipe)) << 19);
	td->buffer = virt_to_bus(dev->data);
	td->qh = qh;
	td->dev = dev;

	/* if period 0, insert into fast q */
	if (period == 0) {
		td->flags |= UHCI_TD_REMOVE;
		qh->skel = &dev->uhci->skel_int2_qh;
	} else
		qh->skel = &dev->uhci->skel_int8_qh;

	uhci_add_irq_list(dev->uhci, td, handler, dev_id);

	uhci_insert_td_in_qh(qh, td);

	/* Add it into the skeleton */
	uhci_insert_qh(qh->skel, qh);

	return (void *)td;
}

/*
 * Release an interrupt handler previously allocated using
 * uhci_request_irq.  This function does no validity checking, so make
 * sure you're not releasing an already released handle as it may be
 * in use by something else..
 *
 * This function can NOT be called from an interrupt.
 */
int uhci_release_irq(void *handle)
{
	struct uhci_td *td;
	struct uhci_qh *qh;

#ifdef UHCI_DEBUG
	printk(KERN_DEBUG "usb-uhci: releasing irq handle %p\n", handle);
#endif

	td = (struct uhci_td *)handle;
	if (!td)
		return USB_ST_INTERNALERROR;

	/* Remove it from the internal irq_list */
	uhci_remove_irq_list(td);

	/* Remove the interrupt TD and QH */
	uhci_remove_td(td);
	qh = td->qh;
	uhci_remove_qh(qh->skel, qh);

	if (td->completed != NULL)
		td->completed(USB_ST_REMOVED, NULL, 0, td->dev_id);

	/* Free the TD and QH */
	uhci_td_free(td);
	uhci_qh_free(qh);

	return USB_ST_NOERROR;
} /* uhci_release_irq() */

/*
 * Isochronous operations
 */
static int uhci_compress_isochronous(struct usb_device *usb_dev, void *_isodesc)
{
	struct uhci_iso_td *isodesc = (struct uhci_iso_td *)_isodesc;
	char *data = isodesc->data;
	int i, totlen = 0;

	for (i = 0; i < isodesc->num; i++) {
		struct uhci_td *td = &isodesc->td[i];
		char *cdata = uhci_ptr_to_virt(td->buffer);
		int n = (td->status + 1) & 0x7FF;

		if ((cdata != data) && (n))
			memmove(data, cdata, n);

#ifdef UHCI_DEBUG
		/* Debugging */
		if ((td->status >> 16) & 0xFF)
			printk(KERN_DEBUG "error: %d %X\n", i,
				(td->status >> 16));
#endif

		data += n;
		totlen += n;
	}

	return totlen;
}

static int uhci_unschedule_isochronous(struct usb_device *usb_dev, void *_isodesc)
{
	struct uhci_device *dev = usb_to_uhci(usb_dev);
	struct uhci *uhci = dev->uhci;
	struct uhci_iso_td *isodesc = (struct uhci_iso_td *)_isodesc;
	int i;

	if ((isodesc->frame < 0) || (isodesc->frame > 1023)) {
		printk(KERN_ERR "illegal frame number %d\n", isodesc->frame);
		return 1;
	}

	/* FIXME: Use uhci_remove_td */

	/* Remove from previous frames */
	for (i = 0; i < isodesc->num; i++) {
		struct uhci_td *td = &isodesc->td[i];

		/* Turn off Active and IOC bits */
		td->status &= ~(3 << 23);
		td->status &= ~(TD_CTRL_ACTIVE | TD_CTRL_IOC);
		
		uhci->fl->frame[(isodesc->frame + i) % 1024] = td->link;
	}

	isodesc->frame = -1;

	return 0;
}
	
/* td points to the one td we allocated for isochronous transfers */
static int uhci_schedule_isochronous(struct usb_device *usb_dev, void *_isodesc, void *_pisodesc)
{
	struct uhci_device *dev = usb_to_uhci(usb_dev);
	struct uhci *uhci = dev->uhci;
	struct uhci_iso_td *isodesc = (struct uhci_iso_td *)_isodesc;
	struct uhci_iso_td *pisodesc = (struct uhci_iso_td *)_pisodesc;
	int frame, i;

	if (isodesc->frame != -1) {
		printk(KERN_ERR "isoc queue not removed\n");
		uhci_unschedule_isochronous(usb_dev, isodesc);
	}

	/* Insert TD into list */
	if (!pisodesc) {
		/* It's not guaranteed to be 1-1024 */
		frame = inw(uhci->io_addr + USBFRNUM) % 1024;

		/* HACK: Start 2 frames from now */
		frame = (frame + 2) % 1024;
	} else
		frame = (pisodesc->endframe + 1) % 1024;

	for (i = 0; i < isodesc->num; i++) {
		struct uhci_td *td = &isodesc->td[i];

		/* Active */
		td->status |= TD_CTRL_ACTIVE;
		td->backptr = &uhci->fl->frame[(frame + i) % 1024];
		td->link = uhci->fl->frame[(frame + i) % 1024];
		uhci->fl->frame[(frame + i) % 1024] = virt_to_bus(td);
	}

	/* IOC on the last TD */
	isodesc->td[i - 1].status |= TD_CTRL_IOC;

	isodesc->frame = frame;
	isodesc->endframe = (frame + isodesc->num - 1) % 1024;

	return 0;
}

/*
 * Initialize isochronous queue
 */
static void *uhci_allocate_isochronous(struct usb_device *usb_dev, unsigned int pipe, void *data, int len, int maxsze, usb_device_irq completed, void *dev_id)
{
	struct uhci_device *dev = usb_to_uhci(usb_dev);
	unsigned long destination, status;
	struct uhci_td *td;
	struct uhci_iso_td *isodesc;
	int i;

	isodesc = kmalloc(sizeof(*isodesc), GFP_KERNEL);
	if (!isodesc) {
		printk(KERN_ERR "Couldn't allocate isodesc!\n");
		return NULL;
	}

	memset(isodesc, 0, sizeof(*isodesc));

	/* Carefully work around the non contiguous pages */
	isodesc->num = len / maxsze;
	isodesc->td = kmalloc(sizeof(struct uhci_td) * isodesc->num, GFP_KERNEL);
	isodesc->frame = isodesc->endframe = -1;
	isodesc->data = data;
	isodesc->maxsze = maxsze;
	
	if (!isodesc->td) {
		printk(KERN_ERR "couldn't allocate td's\n");
		kfree(isodesc);
		return NULL;
	}

	isodesc->frame = isodesc->endframe = -1;

	/*
	 * Build the DATA TD's
	 */
	i = 0;
	do {
		/* Build the TD for control status */
		td = &isodesc->td[i];

		/* The "pipe" thing contains the destination in bits 8--18 */
		destination = (pipe & PIPE_DEVEP_MASK)
			| usb_packetid (pipe);  /* add IN or OUT */

		status = (pipe & TD_CTRL_LS) | TD_CTRL_ACTIVE | TD_CTRL_IOS;

		/*
		 * Build the TD for the control request
		 */
		td->status = status;
		td->info = destination | ((maxsze - 1) << 21);
		td->buffer = virt_to_bus(data);
		td->backptr = NULL;

		i++;

		data += maxsze;
		len -= maxsze;
	} while (i < isodesc->num);

	uhci_add_irq_list(dev->uhci, td, completed, dev_id);

	return isodesc;
}

static void uhci_delete_isochronous(struct usb_device *usb_dev, void *_isodesc)
{
	struct uhci_iso_td *isodesc = (struct uhci_iso_td *)_isodesc;

	/* If it's still scheduled, unschedule them */
	if (isodesc->frame)
		uhci_unschedule_isochronous(usb_dev, isodesc);

	/* Remove it from the IRQ list */
	uhci_remove_irq_list(&isodesc->td[isodesc->num - 1]);

	kfree(isodesc->td);
	kfree(isodesc);
}

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
static int uhci_run_control(struct uhci_device *dev, struct uhci_td *first, struct uhci_td *last)
{
	DECLARE_WAITQUEUE(wait, current);
	struct uhci_qh *qh = uhci_qh_alloc(dev);

	if (!qh)
		return -1;

	current->state = TASK_UNINTERRUPTIBLE;
	add_wait_queue(&qh->wakeup, &wait);

	uhci_add_irq_list(dev->uhci, last, uhci_generic_completed, &qh->wakeup);
	
#if 0
	/* FIXME: This is kinda kludged */
	/* Walk the TD list and update the QH pointer */
	{
	struct uhci_td *curtd;
	int count = 100;

	curtd = first;
	do {
		curtd->qh = ctrl_qh;
		if (curtd->link & TD_CTRL_TERM)
			break;

		curtd = uhci_ptr_to_virt(curtd->link);
	} while (--count);
	if (!count)
		printk(KERN_DEBUG "runaway tds!\n");
	}
#endif

	uhci_insert_tds_in_qh(qh, first, last);

	/* Add it into the skeleton */
	uhci_insert_qh(&dev->uhci->skel_control_qh, qh);

	schedule_timeout(HZ * 5);	/* 5 seconds */

	remove_wait_queue(&qh->wakeup, &wait);

	/* Clean up in case it failed.. */
	uhci_remove_irq_list(last);

	/* Remove it from the skeleton */
	uhci_remove_qh(&dev->uhci->skel_control_qh, qh);

	uhci_qh_free(qh);

	return uhci_td_result(dev, last, NULL);
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
 */
static int uhci_control_msg(struct usb_device *usb_dev, unsigned int pipe, devrequest *cmd, void *data, int len)
{
	struct uhci_device *dev = usb_to_uhci(usb_dev);
	struct uhci_td *first, *td, *prevtd;
	unsigned long destination, status;
	int ret, count;
	int maxsze = usb_maxpacket(usb_dev, pipe);
	__u32 nextlink;

	first = td = uhci_td_alloc(dev);
	if (!td)
		return -ENOMEM;

	/* The "pipe" thing contains the destination in bits 8--18, 0x2D is SETUP */
	destination = (pipe & PIPE_DEVEP_MASK) | 0x2D;

	/* 3 errors */
	status = (pipe & TD_CTRL_LS) | TD_CTRL_ACTIVE | TD_CTRL_SPD | (3 << 27);

	/*
	 * Build the TD for the control request
	 */
	td->status = status;				/* Try forever */
	td->info = destination | (7 << 21);		/* 8 bytes of data */
	td->buffer = virt_to_bus(cmd);

	/*
	 * If direction is "send", change the frame from SETUP (0x2D)
	 * to OUT (0xE1). Else change it from SETUP to IN (0x69)
	 */
	destination ^= (0x2D ^ 0x69);			/* SETUP -> IN */
	if (usb_pipeout(pipe))
		destination ^= (0xE1 ^ 0x69);		/* IN -> OUT */

	prevtd = td;
	td = uhci_td_alloc(dev);
	if (!td)
		return -ENOMEM;

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
		destination ^= 1 << 19;
	
		td->status = status;					/* Status */
		td->info = destination | ((pktsze - 1) << 21);		/* pktsze bytes of data */
		td->buffer = virt_to_bus(data);
		td->backptr = &prevtd->link;

		data += pktsze;
		len -= pktsze;

		prevtd = td;
		td = uhci_td_alloc(dev);
		if (!td)
			return -ENOMEM;
		prevtd->link = virt_to_bus(td) | UHCI_PTR_DEPTH;	/* Update previous TD */
	}

	/*
	 * Build the final TD for control status 
	 */
	destination ^= (0xE1 ^ 0x69);			/* OUT -> IN */
	destination |= 1 << 19;				/* End in Data1 */

	td->status = status | TD_CTRL_IOC;			/* no limit on errors on final packet */
	td->info = destination | (UHCI_NULL_DATA_SIZE << 21);	/* 0 bytes of data */
	td->buffer = 0;
	td->backptr = &prevtd->link;
	td->link = UHCI_PTR_TERM;			/* Terminate */

	/* Start it up.. */
	ret = uhci_run_control(dev, first, td);

	count = 100;
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

	if (uhci_debug && ret) {
		__u8 *p = (__u8 *)cmd;

		printk(KERN_DEBUG "Failed cmd - %02X %02X %02X %02X %02X %02X %02X %02X\n",
		       p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
	}
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
static int uhci_run_bulk(struct uhci_device *dev, struct uhci_td *first, struct uhci_td *last, unsigned long *rval)
{
	DECLARE_WAITQUEUE(wait, current);
	struct uhci_qh *qh = uhci_qh_alloc(dev);

	if (!qh)
		return -ENOMEM;

	current->state = TASK_UNINTERRUPTIBLE;
	add_wait_queue(&qh->wakeup, &wait);

	uhci_add_irq_list(dev->uhci, last, uhci_generic_completed, &qh->wakeup);
	
#if 0
	/* FIXME: This is kinda kludged */
	/* Walk the TD list and update the QH pointer */
	{
	struct uhci_td *curtd;
	int count = 100;

	curtd = first;
	do {
		curtd->qh = bulk_qh;
		if (curtd->link & UHCI_PTR_TERM)
			break;

		curtd = uhci_ptr_to_virt(curtd->link);
	} while (--count);
	if (!count)
		printk(KERN_ERR "runaway tds!\n");
	}
#endif

	uhci_insert_tds_in_qh(qh, first, last);

	/* Add it into the skeleton */
	uhci_insert_qh(&dev->uhci->skel_bulk_qh, qh);

	schedule_timeout(HZ*5);		/* 5 seconds */

	remove_wait_queue(&qh->wakeup, &wait);

	/* Clean up in case it failed.. */
	uhci_remove_irq_list(last);

	uhci_remove_qh(&dev->uhci->skel_bulk_qh, qh);

	uhci_qh_free(qh);

	return uhci_td_result(dev, last, rval);
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
static int uhci_bulk_msg(struct usb_device *usb_dev, unsigned int pipe, void *data, int len, unsigned long *rval)
{
	struct uhci_device *dev = usb_to_uhci(usb_dev);
	struct uhci_td *first, *td, *prevtd;
	unsigned long destination, status;
	int ret;
	int maxsze = usb_maxpacket(usb_dev, pipe);

	if (usb_endpoint_halted(usb_dev, usb_pipeendpoint(pipe)) &&
	    usb_clear_halt(usb_dev, usb_pipeendpoint(pipe) | (pipe & 0x80)))
		return USB_ST_STALL;

	/* The "pipe" thing contains the destination in bits 8--18 */
	destination = (pipe & PIPE_DEVEP_MASK) | usb_packetid (pipe);

	/* 3 errors */
	status = (pipe & TD_CTRL_LS) | TD_CTRL_ACTIVE | TD_CTRL_SPD | (3 << 27);

	/*
	 * Build the TDs for the bulk request
	 */
        first = td = uhci_td_alloc(dev);
	if (!td)
		return -ENOMEM;

        prevtd = first; //This is fake, but at least it's not NULL
	while (len > 0) {
		/* Build the TD for control status */
		int pktsze = len;

		if (pktsze > maxsze)
			pktsze = maxsze;

		td->status = status;					/* Status */
		td->info = destination | ((pktsze-1) << 21) |
			 (usb_gettoggle(usb_dev, usb_pipeendpoint(pipe), usb_pipeout(pipe)) << 19); /* pktsze bytes of data */
		td->buffer = virt_to_bus(data);
		td->backptr = &prevtd->link;

		data += maxsze;
		len -= maxsze;

		if (len > 0) {
			prevtd = td;
			td = uhci_td_alloc(dev);
			if (!td)
				return -ENOMEM;

			prevtd->link = virt_to_bus(td) | UHCI_PTR_DEPTH;/* Update previous TD */
		}

		/* Alternate Data0/1 (start with Data0) */
		usb_dotoggle(usb_dev, usb_pipeendpoint(pipe), usb_pipeout(pipe));
	}

	td->link = 1;				/* Terminate */
	td->status |= TD_CTRL_IOC;

	/* CHANGE DIRECTION HERE! SAVE IT SOMEWHERE IN THE ENDPOINT!!! */

	/* Start it up.. */
	ret = uhci_run_bulk(dev, first, td, rval);

	{
		int count = 100;
		struct uhci_td *curtd = first;
		unsigned int nextlink;

		do {
			nextlink = curtd->link;
			uhci_remove_td(curtd);
			uhci_td_free(curtd);

			if (nextlink & UHCI_PTR_TERM)	/* Tail? */
				break;

			curtd = uhci_ptr_to_virt(nextlink);
		} while (--count);

		if (!count)
			printk(KERN_DEBUG "runaway td's!?\n");
	}

	return ret;
}

static void * uhci_request_bulk(struct usb_device *usb_dev, unsigned int pipe, usb_device_irq handler, void *data, int len, void *dev_id)
{
	struct uhci_device *dev = usb_to_uhci(usb_dev);
	struct uhci *uhci = dev->uhci;
	struct uhci_td *first, *td, *prevtd;
	struct uhci_qh *bulk_qh = uhci_qh_alloc(dev);
	unsigned long destination, status;
	int maxsze = usb_maxpacket(usb_dev, pipe);

	/* The "pipe" thing contains the destination in bits 8--18, 0x69 is IN */
	destination = (pipe & 0x0007ff00) | usb_packetid(pipe);

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
			(usb_gettoggle(usb_dev, usb_pipeendpoint(pipe), usb_pipeout(pipe)) << 19); /* pktsze bytes of data */
		td->buffer = virt_to_bus(data);
		td->backptr = &prevtd->link;
		td->qh = bulk_qh;
		td->dev = dev;
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
 *Remove a handler from a pipe. This terminates the transfer.
 *We have some assumptions here:
 * There is only one queue using this pipe. (the one we remove)
 * Any data that is in the queue is useless for us, we throw it away.
 */
static int uhci_terminate_bulk(struct usb_device *dev, void * first)
{
	/* none found? there is nothing to remove! */
	if (!first)
		return 0;

	uhci_remove_transfer(first,1);

	return 1;
}

static struct usb_device *uhci_usb_alloc(struct usb_device *parent)
{
	struct usb_device *usb_dev;
	struct uhci_device *dev;

	/* Allocate the USB device */
	usb_dev = kmalloc(sizeof(*usb_dev), GFP_KERNEL);
	if (!usb_dev)
		return NULL;

	memset(usb_dev, 0, sizeof(*usb_dev));

	/* Allocate the UHCI device private data */
	dev = kmalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		kfree(usb_dev);
		return NULL;
	}

	/* Initialize "dev" */
	memset(dev, 0, sizeof(*dev));

	usb_dev->hcpriv = dev;
	dev->usb = usb_dev;

	usb_dev->parent = parent;

	if (parent) {
		usb_dev->bus = parent->bus;
		dev->uhci = usb_to_uhci(parent)->uhci;
	}

	return usb_dev;
}

static int uhci_usb_free(struct usb_device *usb_dev)
{
	struct uhci_device *dev = usb_to_uhci(usb_dev);

	kfree(dev);
	usb_destroy_configuration(usb_dev);
	kfree(usb_dev);

	return 0;
}

struct usb_operations uhci_device_operations = {
	uhci_usb_alloc,
	uhci_usb_free,
	uhci_control_msg,
	uhci_bulk_msg,
	uhci_request_irq,
	uhci_release_irq,
	uhci_request_bulk,
	uhci_terminate_bulk,
	uhci_allocate_isochronous,
	uhci_delete_isochronous,
	uhci_schedule_isochronous,
	uhci_unschedule_isochronous,
	uhci_compress_isochronous
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
	if(!(status & USBPORTSC_PE)) {
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
	usb_dev = uhci_usb_alloc(root_hub->usb);
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

		printk(KERN_INFO "uhci: disabling malfunctioning port %d\n",
			nr + 1);
		outw(status | USBPORTSC_PE, port);
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

static void uhci_interrupt_notify(struct uhci *uhci)
{
	struct list_head *tmp, *head = &uhci->interrupt_list;
	int status;

	spin_lock(&irqlist_lock);
	tmp = head->next;
	while (tmp != head) {
		struct uhci_td *first, *td = list_entry(tmp,
					struct uhci_td, irq_list);

		tmp = tmp->next;

		/* We check the TD which had the IOC bit as well as the */
		/*  first TD */
		/* XXX: Shouldn't we check all of the TD's in the chain? */
		if ((td->qh) && (td->qh->element & ~UHCI_PTR_BITS))
			first = uhci_link_to_td(td->qh->element);
		else
			first = NULL;

		/* If any of the error bits are set OR the active is NOT set */
		/*  then we're interested in this TD */
		status = td->status & 0xF60000;

		if ((!(status ^ TD_CTRL_ACTIVE)) && (first) &&
		    (!(first->status & TD_CTRL_ACTIVE)))
			status = first->status & 0xF60000;

		if (!(status ^ TD_CTRL_ACTIVE))
			continue;


		/* remove from IRQ list */
		list_del(&td->irq_list);
		INIT_LIST_HEAD(&td->irq_list);

		if (td->completed(uhci_map_status(status, 0),
		    bus_to_virt(td->buffer), -1, td->dev_id)) {
			list_add(&td->irq_list, &uhci->interrupt_list);

			/* Isochronous TD's don't need this */
			if (!(td->status & TD_CTRL_IOS)) {
				struct usb_device *usb_dev = td->dev->usb;

				usb_dotoggle(usb_dev, usb_pipeendpoint(td->info), usb_pipeout(td->info));
				td->info &= ~(1 << 19); /* clear data toggle */
				td->info |= usb_gettoggle(usb_dev, usb_pipeendpoint(td->info), usb_pipeout(td->info)) << 19; /* toggle between data0 and data1 */
				td->status = (td->status & 0x2F000000) | TD_CTRL_ACTIVE | TD_CTRL_IOC;
				/* The HC removes it, so readd it */
				uhci_insert_td_in_qh(td->qh, td);
			}
		} else if (td->flags & UHCI_TD_REMOVE) {
			struct usb_device *usb_dev = td->dev->usb;

			/* marked for removal */
			td->flags &= ~UHCI_TD_REMOVE;
			usb_dotoggle(usb_dev, usb_pipeendpoint(td->info), usb_pipeout(td->info));
			uhci_remove_qh(td->qh->skel, td->qh);
			uhci_qh_free(td->qh);
			uhci_td_free(td);
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
	 * Read the interrupt status, and write it back to clear the interrupt cause
	 */
	status = inw(io_addr + USBSTS);
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
	td->status = TD_CTRL_IOC;
	td->info = (15 << 21) | 0x7f69;				/* (ignored) input packet, 16 bytes, device 127 */
	td->buffer = 0;
	td->qh = NULL;

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
	outw(USBINTR_TIMEOUT | USBINTR_RESUME | USBINTR_IOC | USBINTR_SP, io_addr + USBINTR);

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
static struct uhci *alloc_uhci(unsigned int io_addr)
{
	int i;
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
	usb = uhci_usb_alloc(NULL);
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
	usb->maxchild = 2;
	usb_init_root_hub(usb);

	/* 8 Interrupt queues */
	for (i = 0; i < 8; i++) {
		struct uhci_qh *qh = &uhci->skelqh[i];

		qh->link = virt_to_bus(&uhci->skel_control_qh) | UHCI_PTR_QH;
		qh->element = UHCI_PTR_TERM;
	}

	uhci->skel_control_qh.link = virt_to_bus(&uhci->skel_bulk_qh) |
		UHCI_PTR_QH;
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

static int uhci_control_thread(void * __uhci)
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
				show_queues(uhci);
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
static int found_uhci(int irq, unsigned int io_addr)
{
	int retval;
	struct uhci *uhci;

	uhci = alloc_uhci(io_addr);
	if (!uhci)
		return -ENOMEM;

	INIT_LIST_HEAD(&uhci->uhci_list);
	list_add(&uhci->uhci_list, &uhci_list);

	request_region(uhci->io_addr, 32, "usb-uhci");

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
	release_region(uhci->io_addr, 32);

	release_uhci(uhci);
	return retval;
}

static int start_uhci(struct pci_dev *dev)
{
	int i;

	/* Search for the IO base address.. */
	for (i = 0; i < 6; i++) {
		unsigned int io_addr = dev->resource[i].start;

		/* IO address? */
		if (!(dev->resource[i].flags & 1))
			continue;

#if 0
		/* Is it already in use? */
		if (check_region(io_addr, 32))
			break;
#endif

		return found_uhci(dev->irq, io_addr);
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
	char *name;

	/* FIXME: This is lame, but I guess it's better to leak memory than */
	/* crash */
	name = kmalloc(10, GFP_KERNEL);
	if (!name)
		return -ENOMEM;

	strcpy(name, "uhci_td");

	uhci_td_cachep = kmem_cache_create(name,
		sizeof(struct uhci_td), 0,
		SLAB_HWCACHE_ALIGN, NULL, NULL);

	if (!uhci_td_cachep)
		return -ENOMEM;

	name = kmalloc(10, GFP_KERNEL);
	if (!name)
		return -ENOMEM;

	strcpy(name, "uhci_qh");

	uhci_qh_cachep = kmem_cache_create(name,
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
		if(type != 0)
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
			int count = 10;

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
		release_region(uhci->io_addr, 32);

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

