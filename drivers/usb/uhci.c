/*
 * Universal Host Controller Interface driver for USB.
 *
 * (C) Copyright 1999 Linus Torvalds
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

#include <linux/sched.h>
#include <linux/unistd.h>
#include <linux/smp_lock.h>

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
	struct uhci_td *tmp = td->first;

	if(rval)
		*rval = 0;

	/* locate the first failing td, if any */

	do {
		status = (tmp->status >> 16) & 0xff;
		if (status) {
			/* must reset the toggle on first error */
    			if (uhci_debug) {
			    printk("Set toggle from %x rval %d\n", (unsigned int)tmp, rval ? *rval : 0);
			}
			usb_settoggle(dev->usb, usb_pipeendpoint(tmp->info), usb_pipeout(tmp->info), (tmp->info >> 19) & 1);
			break;
		} else {
		    if(rval)
			*rval += (tmp->status & 0x3ff) + 1;
		}
		if ((tmp->link & 1) || (tmp->link & 2))
			break;
		tmp = bus_to_virt(tmp->link & ~0xF);
	} while (1);


	if (!status)
    		return USB_ST_NOERROR;

	/* Some debugging code */
	if (uhci_debug /* && (!usb_pipeendpoint(tmp->info) || !(status & 0x08))*/ ) {
		int i = 10;

		tmp = td->first;
		printk("uhci_td_result() failed with status %x\n", status);
		//show_status(dev->uhci);
		do {
			show_td(tmp);
			if ((tmp->link & 1) || (tmp->link & 2))
				break;
			tmp = bus_to_virt(tmp->link & ~0xF);
			if (!--i)
				break;
		} while (1);
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
	unsigned int new = 4 | virt_to_bus(first);

	for (;;) {
		unsigned char success;

		last->link = link;
		first->backptr = &qh->element;
		asm volatile("lock ; cmpxchg %4,%2 ; sete %0"
			:"=q" (success), "=a" (link)
			:"m" (qh->element), "1" (link), "r" (new)
			:"memory");
		if (success) {
			/* Was there a successor entry? Fix it's backpointer.. */
			if ((link & 1) == 0) {
				struct uhci_td *next = bus_to_virt(link & ~15);
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
	qh->link = virt_to_bus(newqh) | 2;
}

static void uhci_remove_qh(struct uhci_qh *qh, struct uhci_qh *remqh)
{
	unsigned int remphys = virt_to_bus(remqh);
	struct uhci_qh *lqh = qh;

	while ((lqh->link & ~0xF) != remphys) {
		if (lqh->link & 1)
			break;

		lqh = bus_to_virt(lqh->link & ~0xF);
	}

	if (lqh->link & 1) {
		printk("couldn't find qh in chain!\n");
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
	if (!(link & 1)) {
		struct uhci_td *next = bus_to_virt(link & ~15);
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

static struct uhci_qh *uhci_qh_allocate(struct uhci_device *dev)
{
	struct uhci_qh *qh;
	int inuse;

	qh = dev->qh;
	for (; (inuse = test_and_set_bit(0, &qh->inuse)) != 0 && qh < &dev->qh[UHCI_MAXQH]; qh++)
		;

	if (!inuse)
		return(qh);

	printk("ran out of qh's for dev %p\n", dev);
	return(NULL);
}

static void uhci_qh_deallocate(struct uhci_qh *qh)
{
//	if (qh->element != 1)
//		printk("qh %p leaving dangling entries? (%X)\n", qh, qh->element);

	qh->element = 1;
	qh->link = 1;

	clear_bit(0, &qh->inuse);
}

static struct uhci_td *uhci_td_allocate(struct uhci_device *dev)
{
	struct uhci_td *td;
	int inuse;

	td = dev->td;
	for (; (inuse = test_and_set_bit(0, &td->inuse)) != 0 && td < &dev->td[UHCI_MAXTD]; td++)
		;

	if (!inuse) {
    		td->inuse = 1;
		return(td);
	}

	printk("ran out of td's for dev %p\n", dev);
	return(NULL);
}

/*
 * This MUST only be called when it has been removed from a QH already (or
 * the QH has been removed from the skeleton
 */
static void uhci_td_deallocate(struct uhci_td *td)
{
	td->link = 1;

	clear_bit(0, &td->inuse);
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
 * Request a interrupt handler..
 */
static int uhci_request_irq(struct usb_device *usb_dev, unsigned int pipe, usb_device_irq handler, int period, void *dev_id)
{
	struct uhci_device *dev = usb_to_uhci(usb_dev);
	struct uhci_device *root_hub=usb_to_uhci(dev->uhci->bus->root_hub);
	struct uhci_td *td = uhci_td_allocate(dev);
	struct uhci_qh *interrupt_qh = uhci_qh_allocate(dev);

	unsigned int destination, status;

	/* Destination: pipe destination with INPUT */
	destination = (pipe & 0x0007ff00) | 0x69;

	/* Status:    slow/fast,      Interrupt,   Active,    Short Packet Detect     Infinite Errors */
	status = (pipe & (1 << 26)) | (1 << 24) | (1 << 23)   |   (1 << 29)       |    (0 << 27);

	if(interrupt_qh->element != 1)
		printk("interrupt_qh->element = 0x%x\n", 
		       interrupt_qh->element);

	td->link = 1;
	td->status = status;			/* In */
	td->info = destination | (7 << 21) | (usb_gettoggle(usb_dev, usb_pipeendpoint(pipe), usb_pipeout(pipe)) << 19);	/* 8 bytes of data */
	td->buffer = virt_to_bus(dev->data);
	td->first = td;
	td->qh = interrupt_qh;
	td->dev = usb_dev;

	/* if period 0, insert into fast q */

	if (period == 0) {
		td->inuse |= 2;
		interrupt_qh->skel = &root_hub->skel_int2_qh;
	} else
		interrupt_qh->skel = &root_hub->skel_int8_qh;

	uhci_add_irq_list(dev->uhci, td, handler, dev_id);

	uhci_insert_td_in_qh(interrupt_qh, td);

	/* Add it into the skeleton */
	uhci_insert_qh(interrupt_qh->skel, interrupt_qh);
	return 0;
}

/*
 * Remove running irq td from queues
 */

static int uhci_remove_irq(struct usb_device *usb_dev, unsigned int pipe, usb_device_irq handler, int period, void *dev_id)
{
    struct uhci_device *dev = usb_to_uhci(usb_dev);
    struct uhci_device *root_hub=usb_to_uhci(dev->uhci->bus->root_hub);
    struct uhci_td *td;
    struct uhci_qh *interrupt_qh;
    unsigned long flags;
    struct list_head *head = &dev->uhci->interrupt_list;
    struct list_head *tmp;

    spin_lock_irqsave(&irqlist_lock, flags);

    /* find the TD in the interrupt list */

    tmp = head->next;
    while (tmp != head) {
	td = list_entry(tmp, struct uhci_td, irq_list);
	if (td->dev_id == dev_id && td->completed == handler) {

	    /* found the right one - let's remove it */

	    /* notify removal */

	    td->completed(USB_ST_REMOVED, NULL, 0, td->dev_id);

	    /* this is DANGEROUS - not sure whether this is right */

	    list_del(&td->irq_list);
	    uhci_remove_td(td);
	    interrupt_qh = td->qh;
	    uhci_remove_qh(interrupt_qh->skel, interrupt_qh);
	    uhci_td_deallocate(td);
	    uhci_qh_deallocate(interrupt_qh);
	    spin_unlock_irqrestore(&irqlist_lock, flags);
	    return USB_ST_NOERROR;
	}
    }
    spin_unlock_irqrestore(&irqlist_lock, flags);
    return USB_ST_INTERNALERROR;
}
/*
 * Isochronous thread operations
 */

int uhci_compress_isochronous(struct usb_device *usb_dev, void *_isodesc)
{
	struct uhci_iso_td *isodesc = (struct uhci_iso_td *)_isodesc;
	char *data = isodesc->data;
	int i, totlen = 0;

	for (i = 0; i < isodesc->num; i++) {
		char *cdata = bus_to_virt(isodesc->td[i].buffer & ~0xF);
		int n = (isodesc->td[i].status + 1) & 0x7FF;

		if ((cdata != data) && (n))
			memmove(data, cdata, n);

#if 0
if (n && n != 960)
	printk("underrun: %d %d\n", i, n);
#endif
if ((isodesc->td[i].status >> 16) & 0xFF)
	printk("error: %d %X\n", i, (isodesc->td[i].status >> 16));

		data += n;
		totlen += n;
	}

	return totlen;
}

int uhci_unsched_isochronous(struct usb_device *usb_dev, void *_isodesc)
{
	struct uhci_device *dev = usb_to_uhci(usb_dev);
	struct uhci *uhci = dev->uhci;
	struct uhci_iso_td *isodesc = (struct uhci_iso_td *)_isodesc;
	int i;

	if ((isodesc->frame < 0) || (isodesc->frame > 1023))
		return 1;

	/* Remove from previous frames */
	for (i = 0; i < isodesc->num; i++) {
		/* Turn off Active and IOC bits */
		isodesc->td[i].status &= ~(3 << 23);
		uhci->fl->frame[(isodesc->frame + i) % 1024] = isodesc->td[i].link;
	}

	isodesc->frame = -1;

	return 0;
}
	
/* td points to the one td we allocated for isochronous transfers */
int uhci_sched_isochronous(struct usb_device *usb_dev, void *_isodesc, void *_pisodesc)
{
	struct uhci_device *dev = usb_to_uhci(usb_dev);
	struct uhci *uhci = dev->uhci;
	struct uhci_iso_td *isodesc = (struct uhci_iso_td *)_isodesc;
	struct uhci_iso_td *pisodesc = (struct uhci_iso_td *)_pisodesc;
	int frame, i;

	if (isodesc->frame != -1) {
		printk("isoc queue not removed\n");
		uhci_unsched_isochronous(usb_dev, isodesc);
	}

	/* Insert TD into list */
	if (!pisodesc) {
		frame = inw(uhci->io_addr + USBFRNUM) % 1024;
		/* HACK: Start 2 frames from now */
		frame = (frame + 2) % 1024;
	} else
		frame = (pisodesc->endframe + 1) % 1024;

#if 0
printk("scheduling first at frame %d\n", frame);
#endif

	for (i = 0; i < isodesc->num; i++) {
		/* Active */
		isodesc->td[i].status |= (1 << 23);
		isodesc->td[i].backptr = &uhci->fl->frame[(frame + i) % 1024];
		isodesc->td[i].link = uhci->fl->frame[(frame + i) % 1024];
		uhci->fl->frame[(frame + i) % 1024] = virt_to_bus(&isodesc->td[i]);
	}

#if 0
printk("last at frame %d\n", (frame + i - 1) % 1024);
#endif

	/* Interrupt */
	isodesc->td[i - 1].status |= (1 << 24);

	isodesc->frame = frame;
	isodesc->endframe = (frame + isodesc->num - 1) % 1024;

#if 0
	return uhci_td_result(dev, td[num - 1]);
#endif
	return 0;
}

/*
 * Initialize isochronous queue
 */
void *uhci_alloc_isochronous(struct usb_device *usb_dev, unsigned int pipe, void *data, int len, int maxsze, usb_device_irq completed, void *dev_id)
{
	struct uhci_device *dev = usb_to_uhci(usb_dev);
	unsigned long destination, status;
	struct uhci_td *td;
	struct uhci_iso_td *isodesc;
	int i;

	isodesc = kmalloc(sizeof(*isodesc), GFP_KERNEL);
	if (!isodesc) {
		printk("Couldn't allocate isodesc!\n");
		return NULL;
	}
	memset(isodesc, 0, sizeof(*isodesc));

	/* Carefully work around the non contiguous pages */
	isodesc->num = (len / PAGE_SIZE) * (PAGE_SIZE / maxsze);
	isodesc->td = kmalloc(sizeof(struct uhci_td) * isodesc->num, GFP_KERNEL);
	isodesc->frame = isodesc->endframe = -1;
	isodesc->data = data;
	isodesc->maxsze = maxsze;
	
	if (!isodesc->td) {
		printk("Couldn't allocate td's\n");
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
		destination = (pipe & 0x0007ff00);

		if (usb_pipeout(pipe))
			destination |= 0xE1;	/* OUT */
		else
			destination |= 0x69;	/* IN */

		/* Status:    slow/fast,       Active,       Isochronous */
		status = (pipe & (1 << 26)) | (1 << 23)   |   (1 << 25);

		/*
		 * Build the TD for the control request
		 */
		td->status = status;
		td->info = destination | ((maxsze - 1) << 21);
		td->buffer = virt_to_bus(data);
		td->first = td;
		td->backptr = NULL;

		i++;

		data += maxsze;

		if (((int)data % PAGE_SIZE) + maxsze >= PAGE_SIZE)
			data = (char *)(((int)data + maxsze) & ~(PAGE_SIZE - 1));
		
		len -= maxsze;
	} while (i < isodesc->num);

	/* IOC on the last TD */
	td->status |= (1 << 24);
	uhci_add_irq_list(dev->uhci, td, completed, dev_id);

	return isodesc;
}

void uhci_delete_isochronous(struct usb_device *usb_dev, void *_isodesc)
{
	struct uhci_iso_td *isodesc = (struct uhci_iso_td *)_isodesc;

	/* If it's still scheduled, unschedule them */
	if (isodesc->frame)
		uhci_unsched_isochronous(usb_dev, isodesc);

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
static DECLARE_WAIT_QUEUE_HEAD(control_wakeup);

static int uhci_control_completed(int status, void *buffer, int len, void *dev_id)
{
	wake_up(&control_wakeup);
	return 0;			/* Don't re-instate */
}

/* td points to the last td in the list, which interrupts on completion */
static int uhci_run_control(struct uhci_device *dev, struct uhci_td *first, struct uhci_td *last)
{
	DECLARE_WAITQUEUE(wait, current);
	struct uhci_qh *ctrl_qh = uhci_qh_allocate(dev);
	struct uhci_td *curtd;
	struct uhci_device *root_hub=usb_to_uhci(dev->uhci->bus->root_hub);
	current->state = TASK_UNINTERRUPTIBLE;
	add_wait_queue(&control_wakeup, &wait);

	uhci_add_irq_list(dev->uhci, last, uhci_control_completed, NULL);
	
	/* FIXME: This is kinda kludged */
	/* Walk the TD list and update the QH pointer */
	{
	int maxcount = 100;

	curtd = first;
	do {
		curtd->qh = ctrl_qh;
		if (curtd->link & 1)
			break;

		curtd = bus_to_virt(curtd->link & ~0xF);
		if (!--maxcount) {
			printk("runaway tds!\n");
			break;
		}
	} while (1);
	}

	uhci_insert_tds_in_qh(ctrl_qh, first, last);

	/* Add it into the skeleton */
	uhci_insert_qh(&root_hub->skel_control_qh, ctrl_qh);

//	control should be full here...	
//	printk("control\n");
//	show_status(dev->uhci);
//	show_queues(dev->uhci);

	schedule_timeout(HZ*5);

//	control should be empty here...	
//	show_status(dev->uhci);
//	show_queues(dev->uhci);

	remove_wait_queue(&control_wakeup, &wait);

	/* Clean up in case it failed.. */
	uhci_remove_irq_list(last);

#if 0
	printk("Looking for tds [%p, %p]\n", dev->control_td, td);
#endif

	/* Remove it from the skeleton */
	uhci_remove_qh(&root_hub->skel_control_qh, ctrl_qh);

	uhci_qh_deallocate(ctrl_qh);

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
static int uhci_control_msg(struct usb_device *usb_dev, unsigned int pipe, void *cmd, void *data, int len)
{
	struct uhci_device *dev = usb_to_uhci(usb_dev);
	struct uhci_td *first, *td, *prevtd;
	unsigned long destination, status;
	int ret;
	int maxsze = usb_maxpacket(usb_dev, pipe);

	if (len > maxsze * 29)
		printk("Warning, too much data for a control packet, crashing\n");

	first = td = uhci_td_allocate(dev);

	/* The "pipe" thing contains the destination in bits 8--18, 0x2D is SETUP */
	destination = (pipe & 0x0007ff00) | 0x2D;

	/* Status:    slow/fast,       Active,    Short Packet Detect     Three Errors */
	status = (pipe & (1 << 26)) | (1 << 23)   |   (1 << 29)       |    (3 << 27);

	/*
	 * Build the TD for the control request
	 */
	td->status = status;				/* Try forever */
	td->info = destination | (7 << 21);		/* 8 bytes of data */
	td->buffer = virt_to_bus(cmd);
	td->first = td;

	/*
	 * If direction is "send", change the frame from SETUP (0x2D)
	 * to OUT (0xE1). Else change it from SETUP to IN (0x69)
	 */
	destination ^= (0x2D ^ 0x69);			/* SETUP -> IN */
	if (usb_pipeout(pipe))
		destination ^= (0xE1 ^ 0x69);		/* IN -> OUT */

	prevtd = td;
	td = uhci_td_allocate(dev);
	prevtd->link = 4 | virt_to_bus(td);

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
		td->info = destination | ((pktsze-1) << 21);		/* pktsze bytes of data */
		td->buffer = virt_to_bus(data);
		td->first = first;
		td->backptr = &prevtd->link;

		data += pktsze;
		len -= pktsze;

		prevtd = td;
		td = uhci_td_allocate(dev);
		prevtd->link = 4 | virt_to_bus(td);			/* Update previous TD */

	}

	/*
	 * Build the final TD for control status 
	 */
	destination ^= (0xE1 ^ 0x69);			/* OUT -> IN */
	destination |= 1 << 19;				/* End in Data1 */

	td->backptr = &prevtd->link;
	td->status = (status /* & ~(3 << 27) */) | (1 << 24);	/* no limit on final packet */
	td->info = destination | (0x7ff << 21);		/* 0 bytes of data */
	td->buffer = 0;
	td->first = first;
	td->link = 1;					/* Terminate */


	/* Start it up.. */
	ret = uhci_run_control(dev, first, td);

	{
		int maxcount = 100;
		struct uhci_td *curtd = first;
		unsigned int nextlink;

		do {
			nextlink = curtd->link;
			uhci_remove_td(curtd);
			uhci_td_deallocate(curtd);
			if (nextlink & 1)	/* Tail? */
				break;

			curtd = bus_to_virt(nextlink & ~0xF);
			if (!--maxcount) {
				printk("runaway td's!?\n");
				break;
			}
		} while (1);
	}

	if (uhci_debug && ret) {
		__u8 *p = cmd;

		printk("Failed cmd - %02X %02X %02X %02X %02X %02X %02X %02X\n",
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
static DECLARE_WAIT_QUEUE_HEAD(bulk_wakeup);

static int uhci_bulk_completed(int status, void *buffer, int len, void *dev_id)
{
	wake_up(&bulk_wakeup);
	return 0;			/* Don't re-instate */
}

/* td points to the last td in the list, which interrupts on completion */
static int uhci_run_bulk(struct uhci_device *dev, struct uhci_td *first, struct uhci_td *last, unsigned long *rval)
{
	DECLARE_WAITQUEUE(wait, current);
	struct uhci_qh *bulk_qh = uhci_qh_allocate(dev);
	struct uhci_td *curtd;
	struct uhci_device *root_hub=usb_to_uhci(dev->uhci->bus->root_hub);

	current->state = TASK_UNINTERRUPTIBLE;
	add_wait_queue(&bulk_wakeup, &wait);

	uhci_add_irq_list(dev->uhci, last, uhci_bulk_completed, NULL);
	
	/* FIXME: This is kinda kludged */
	/* Walk the TD list and update the QH pointer */
	{
	int maxcount = 100;

	curtd = first;
	do {
		curtd->qh = bulk_qh;
		if (curtd->link & 1)
			break;

		curtd = bus_to_virt(curtd->link & ~0xF);
		if (!--maxcount) {
			printk("runaway tds!\n");
			break;
		}
	} while (1);
	}

	uhci_insert_tds_in_qh(bulk_qh, first, last);

	/* Add it into the skeleton */
	uhci_insert_qh(&root_hub->skel_bulk0_qh, bulk_qh);

//	now we're in the queue... but don't ask WHAT is in there ;-(
//	printk("bulk\n");
//	show_status(dev->uhci);
//	show_queues(dev->uhci);

	schedule_timeout(HZ*5);
//	show_status(dev->uhci);
//	show_queues(dev->uhci);

	//show_queue(first->qh);
	remove_wait_queue(&bulk_wakeup, &wait);

	/* Clean up in case it failed.. */
	uhci_remove_irq_list(last);

#if 0
	printk("Looking for tds [%p, %p]\n", dev->control_td, td);
#endif

	/* Remove it from the skeleton */
	uhci_remove_qh(&root_hub->skel_bulk0_qh, bulk_qh);

	uhci_qh_deallocate(bulk_qh);

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
 *
 * The data phase can be an arbitrary number of TD's
 * although we currently had better not have more than
 * 31 TD's here.
 *
 * 31 TD's is a minimum of 248 bytes worth of bulk
 * information.
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

	if (len > maxsze * 31)
		printk("Warning, too much data for a bulk packet, crashing (%d/%d)\n", len, maxsze);

	/* The "pipe" thing contains the destination in bits 8--18, 0x69 is IN */
	/*
	  IS THIS NECCESARY? PERHAPS WE CAN JUST USE THE PIPE
	  LOOK AT: usb_pipeout and the pipe bits
	  I FORGOT WHAT IT EXACTLY DOES
	*/
	if (usb_pipeout(pipe)) {
		destination = (pipe & 0x0007ff00) | 0xE1;
	}
	else {
		destination = (pipe & 0x0007ff00) | 0x69;
	}

	/* Status:    slow/fast,       Active,    Short Packet Detect     Three Errors */
	status = (pipe & (1 << 26)) | (1 << 23)   |   (1 << 29)       |    (3 << 27);

	/*
	 * Build the TDs for the bulk request
	 */
        first = td = uhci_td_allocate(dev);
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
		td->first = first;

		data += maxsze;
		len -= maxsze;

		if (len > 0) {
			prevtd = td;
			td = uhci_td_allocate(dev);
			prevtd->link = 4 | virt_to_bus(td);			/* Update previous TD */
		}

		/* Alternate Data0/1 (start with Data0) */
		usb_dotoggle(usb_dev, usb_pipeendpoint(pipe), usb_pipeout(pipe));
	}
	td->link = 1;				/* Terminate */
	td->status |= (1 << 24);		/* IOC */

	/* CHANGE DIRECTION HERE! SAVE IT SOMEWHERE IN THE ENDPOINT!!! */

	/* Start it up.. */
	ret = uhci_run_bulk(dev, first, td, rval);

	{
		int maxcount = 100;
		struct uhci_td *curtd = first;
		unsigned int nextlink;

		do {
			nextlink = curtd->link;
			uhci_remove_td(curtd);
			uhci_td_deallocate(curtd);
			if (nextlink & 1)	/* Tail? */
				break;

			curtd = bus_to_virt(nextlink & ~0xF);
			if (!--maxcount) {
				printk("runaway td's!?\n");
				break;
			}
		} while (1);
	}

	return ret;
}

static struct usb_device *uhci_usb_allocate(struct usb_device *parent)
{
	struct usb_device *usb_dev;
	struct uhci_device *dev;
	int i;

	usb_dev = kmalloc(sizeof(*usb_dev), GFP_KERNEL);
	if (!usb_dev)
		return NULL;

	memset(usb_dev, 0, sizeof(*usb_dev));

	dev = kmalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		usb_destroy_configuration(usb_dev);
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

	/* Reset the QH's and TD's */
	for (i = 0; i < UHCI_MAXQH; i++) {
		dev->qh[i].link = 1;
		dev->qh[i].element = 1;
		dev->qh[i].inuse = 0;
	}

	for (i = 0; i < UHCI_MAXTD; i++) {
		dev->td[i].link = 1;
		dev->td[i].inuse = 0;
	}

	return usb_dev;
}

static int uhci_usb_deallocate(struct usb_device *usb_dev)
{
	struct uhci_device *dev = usb_to_uhci(usb_dev);
	int i;

	/* There are UHCI_MAXTD preallocated tds */
	for (i = 0; i < UHCI_MAXTD; ++i) {
		struct uhci_td *td = dev->td + i;

		if (td->inuse & 1) {
			uhci_remove_td(td);

			/* And remove it from the irq list, if it's active */
			if (td->status & (1 << 23))
				td->status &= ~(1 << 23);
#if 0
				uhci_remove_irq_list(td);
#endif
		}
	}

	/* Remove the td from any queues */
	for (i = 0; i < UHCI_MAXQH; ++i) {
		struct uhci_qh *qh = dev->qh + i;

		if (qh->inuse & 1)
			uhci_remove_qh(qh->skel, qh);
	}

	kfree(dev);
	usb_destroy_configuration(usb_dev);
	kfree(usb_dev);

	return 0;
}

struct usb_operations uhci_device_operations = {
	uhci_usb_allocate,
	uhci_usb_deallocate,
	uhci_control_msg,
	uhci_bulk_msg,
	uhci_request_irq,
	uhci_remove_irq,
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
	struct uhci_device *root_hub=usb_to_uhci(uhci->bus->root_hub);
	printk("uhci_connect_change: called for %d\n", nr);

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
	usb_dev = uhci_usb_allocate(root_hub->usb);
	dev = usb_dev->hcpriv;

	dev->uhci = uhci;

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
	usb_new_device(usb_dev);
}

/*
 * This gets called when the root hub configuration
 * has changed. Just go through each port, seeing if
 * there is something interesting happening.
 */
static void uhci_check_configuration(struct uhci *uhci)
{
	struct uhci_device * root_hub=usb_to_uhci(uhci->bus->root_hub);
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
	struct list_head *head = &uhci->interrupt_list;
	struct list_head *tmp;
	int status;

	spin_lock(&irqlist_lock);
	tmp = head->next;
	while (tmp != head) {
		struct uhci_td *td = list_entry(tmp, struct uhci_td, irq_list);
		struct list_head *next;

		next = tmp->next;

		if (!((status = td->status) & (1 << 23)) ||  /* No longer active? */
		    ((td->qh->element & ~15) && 
		      !((status = uhci_link_to_td(td->qh->element)->status) & (1 <<23)) &&
		      (status & 0x760000) /* is in error state (Stall, db, babble, timeout, bitstuff) */)) {	
			/* remove from IRQ list */
			__list_del(tmp->prev, next);
			INIT_LIST_HEAD(tmp);
			if (td->completed(uhci_map_status(status, 0), bus_to_virt(td->buffer), -1, td->dev_id)) {
				list_add(&td->irq_list, &uhci->interrupt_list);

				if (!(td->status & (1 << 25))) {
					struct uhci_qh *interrupt_qh = td->qh;

					usb_dotoggle(td->dev, usb_pipeendpoint(td->info), usb_pipeout(td->info));
					td->info &= ~(1 << 19); /* clear data toggle */
					td->info |= usb_gettoggle(td->dev, usb_pipeendpoint(td->info), usb_pipeout(td->info)) << 19; /* toggle between data0 and data1 */
					td->status = (td->status & 0x2f000000) | (1 << 23) | (1 << 24);	/* active */

					/* Remove then readd? Is that necessary */
					uhci_remove_td(td);
					uhci_insert_td_in_qh(interrupt_qh, td);
				}
			} else if (td->inuse & 2) {
				struct uhci_qh *interrupt_qh = td->qh;
				/* marked for removal */
				td->inuse &= ~2;
				usb_dotoggle(td->dev, usb_pipeendpoint(td->info), usb_pipeout(td->info));
				uhci_remove_qh(interrupt_qh->skel, interrupt_qh);
				uhci_qh_deallocate(interrupt_qh);
				uhci_td_deallocate(td);
			}
			/* If completed wants to not reactivate, then it's */
			/* responsible for free'ing the TD's and QH's */
			/* or another function (such as run_control) */
		} 
		tmp = next;
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
		struct uhci_device * root_hub=usb_to_uhci(uhci->bus->root_hub);
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

//	if ((status & ~0x21) != 0)
//		printk("interrupt: %X\n", status);

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
	struct uhci_td *td = uhci_td_allocate(dev);

	td->link = 1;
	td->status = (1 << 24);					/* interrupt on completion */
	td->info = (15 << 21) | 0x7f69;				/* (ignored) input packet, 16 bytes, device 127 */
	td->buffer = 0;
	td->first = td;
	td->qh = NULL;

	uhci->fl->frame[0] = virt_to_bus(td);
}

static void reset_hc(struct uhci *uhci)
{
	unsigned int io_addr = uhci->io_addr;

	/* Global reset for 50ms */
	outw(USBCMD_GRESET, io_addr+USBCMD);
	wait_ms(50);
	outw(0, io_addr+USBCMD);
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
			printk("USBCMD_HCRESET timed out!\n");
			break;
		}
	}


	outw(USBINTR_TIMEOUT | USBINTR_RESUME | USBINTR_IOC | USBINTR_SP, io_addr + USBINTR);
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

	bus = kmalloc(sizeof(*bus), GFP_KERNEL);
	if (!bus)
		return NULL;

	memset(bus, 0, sizeof(*bus));

	uhci->bus = bus;
	bus->hcpriv = uhci;
	bus->op = &uhci_device_operations;

	/*
	 * We allocate a 8kB area for the UHCI hub. The area
	 * is described by the uhci_device structure, and basically
	 * contains everything needed for normal operation.
	 *
	 * The first page is the actual device descriptor for the
	 * hub.
	 *
	 * The second page is used for the frame list.
	 */
	usb = uhci_usb_allocate(NULL);
	if (!usb)
		return NULL;

	usb->bus = bus;
	dev = usb_to_uhci(usb);
	uhci->bus->root_hub=uhci_to_usb(dev);
	/* Initialize the root hub */
	/* UHCI specs says devices must have 2 ports, but goes on to say */
	/* they may have more but give no way to determine how many they */
	/* have, so default to 2 */
	usb->maxchild = 2;
	usb_init_root_hub(usb);

	/*
	 * Initialize the queues. They all start out empty,
	 * linked to each other in the proper order.
	 */
	for (i = 1 ; i < 9; i++) {
		dev->qh[i].link = 2 | virt_to_bus(&dev->skel_control_qh);
		dev->qh[i].element = 1;
	}
	
	dev->skel_control_qh.link = 2 | virt_to_bus(&dev->skel_bulk0_qh);
	dev->skel_control_qh.element = 1;

	dev->skel_bulk0_qh.link = 2 | virt_to_bus(&dev->skel_bulk1_qh);
	dev->skel_bulk0_qh.element = 1;

	dev->skel_bulk1_qh.link = 2 | virt_to_bus(&dev->skel_bulk2_qh);
	dev->skel_bulk1_qh.element = 1;

	dev->skel_bulk2_qh.link = 2 | virt_to_bus(&dev->skel_bulk3_qh);
	dev->skel_bulk2_qh.element = 1;

	dev->skel_bulk3_qh.link = 1;
	dev->skel_bulk3_qh.element = 1;

	/*
	 * Fill the frame list: make all entries point to
	 * the proper interrupt queue.
	 *
	 * This is probably silly, but it's a simple way to
	 * scatter the interrupt queues in a way that gives
	 * us a reasonable dynamic range for irq latencies.
	 */
	for (i = 0; i < 1024; i++) {
		struct uhci_qh * irq = &dev->skel_int2_qh;
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
		uhci->fl->frame[i] =  2 | virt_to_bus(irq);
	}

	return uhci;
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

#if 0
	if (uhci->bus->root_hub) {
		uhci_usb_deallocate(uhci_to_usb(uhci->bus->root_hub));
		uhci->bus->root_hub = NULL;
	}
#endif

	if (uhci->fl) {
		free_page((unsigned long)uhci->fl);
		uhci->fl = NULL;
	}

	kfree(uhci->bus);
	kfree(uhci);
}

static int uhci_control_thread(void * __uhci)
{
	struct uhci *uhci = (struct uhci *)__uhci;
	struct uhci_device * root_hub =usb_to_uhci(uhci->bus->root_hub);

	lock_kernel();
	request_region(uhci->io_addr, 32, "usb-uhci");

	/*
	 * This thread doesn't need any user-level access,
	 * so get rid of all our resources..
	 */
	printk("uhci_control_thread at %p\n", &uhci_control_thread);
	exit_mm(current);
	exit_files(current);
	//exit_fs(current);

	strcpy(current->comm, "uhci-control");

	/*
	 * Ok, all systems are go..
	 */
	start_hc(uhci);
	usb_register_bus(uhci->bus);
	for(;;) {
		siginfo_t info;
		int unsigned long signr;

		interruptible_sleep_on(&uhci_configure);
#ifdef CONFIG_APM
		if (apm_resume) {
			apm_resume = 0;
			start_hc(uhci);
			continue;
		}
#endif
		uhci_check_configuration(uhci);

		if(signal_pending(current)) {
			/* sending SIGUSR1 makes us print out some info */
			spin_lock_irq(&current->sigmask_lock);
			signr = dequeue_signal(&current->blocked, &info);
			spin_unlock_irq(&current->sigmask_lock);

			if(signr == SIGUSR1) {
				printk("UHCI queue dump:\n");
				show_queues(uhci);
			} else if (signr == SIGUSR2) {
				printk("UHCI debug toggle\n");
				uhci_debug = !uhci_debug;
			} else {
				break;
			}
		}
	}

	{
	int i;
	if(root_hub)
		for(i = 0; i < root_hub->usb->maxchild; i++)
			usb_disconnect(root_hub->usb->children + i);
	}

	usb_deregister_bus(uhci->bus);

	reset_hc(uhci);
	release_region(uhci->io_addr, 32);

	release_uhci(uhci);
	MOD_DEC_USE_COUNT;

	printk("uhci_control_thread exiting\n");

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

	reset_hc(uhci);

	retval = -EBUSY;
	if (request_irq(irq, uhci_interrupt, SA_SHIRQ, "usb", uhci) == 0) {
		int pid;
		MOD_INC_USE_COUNT;
		uhci->irq = irq;
		pid = kernel_thread(uhci_control_thread, uhci,
			CLONE_FS | CLONE_FILES | CLONE_SIGHAND);
		if (pid >= 0)
			return 0;

		MOD_DEC_USE_COUNT;
		retval = pid;
	}
	release_uhci(uhci);
	return retval;
}

static int start_uhci(struct pci_dev *dev)
{
	int i;

	/* Search for the IO base address.. */
	for (i = 0; i < 6; i++) {
		unsigned int io_addr = dev->base_address[i];

		/* IO address? */
		if (!(io_addr & 1))
			continue;

		io_addr &= PCI_BASE_ADDRESS_IO_MASK;

		/* Is it already in use? */
		if (check_region(io_addr, 32))
			break;

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

	retval = -ENODEV;
	for (;;) {
		dev = pci_find_class(PCI_CLASS_SERIAL_USB<<8, dev);
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
}
#endif //MODULE
