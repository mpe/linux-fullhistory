/* 
 * Universal Host Controller Interface driver for USB (take II).
 *
 * (c) 1999 Georg Acher, acher@in.tum.de (executive slave) (base guitar)
 *          Deti Fliegl, deti@fliegl.de (executive slave) (lead voice)
 *          Thomas Sailer, sailer@ife.ee.ethz.ch (chief consultant) (cheer leader)
 *          Roman Weissgaerber, weissg@vienna.at (virt root hub) (studio porter)
 *          
 * HW-initalization and root hub allocation code based on material of
 *
 * (C) Copyright 1999 Linus Torvalds
 * (C) Copyright 1999 Johannes Erdfelt
 * (C) Copyright 1999 Randy Dunlap
 *
 * $Id: uhci.c,v 1.124 1999/12/14 18:38:25 fliegl Exp $
 */

#define VROOTHUB

#ifndef EXPORT_SYMTAB
#define EXPORT_SYMTAB
#endif

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
#include <linux/interrupt.h>	/* for in_interrupt() */

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
//#include <asm/spinlock.h>

#include "usb.h"
#include "uhci.h"
#include "uhci-debug.h"

//#define DEBUG
#ifdef DEBUG
#define dbg printk
#else
#define dbg nix
static void nix (const char *format,...)
{
}

#endif

#define _static static

#ifdef __alpha
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0)
extern long __kernel_thread (unsigned long, int (*)(void *), void *);
static inline long kernel_thread (int (*fn) (void *), void *arg, unsigned long flags)
{
	return __kernel_thread (flags | CLONE_VM, fn, arg);
}
#undef CONFIG_APM
#endif
#endif

#ifdef CONFIG_APM
#include <linux/apm_bios.h>
static int handle_apm_event (apm_event_t event);
#endif

static puhci_t devs = NULL;

/*-------------------------------------------------------------------*/
_static void queue_urb (puhci_t s, struct list_head *p, int do_lock)
{
	unsigned long flags=0;
	if (do_lock) spin_lock_irqsave (&s->urb_list_lock, flags);
	list_add_tail (p, &s->urb_list);
	if (do_lock) spin_unlock_irqrestore (&s->urb_list_lock, flags);
}

/*-------------------------------------------------------------------*/
_static void dequeue_urb (puhci_t s, struct list_head *p, int do_lock)
{
	unsigned long flags=0;
	if (do_lock) spin_lock_irqsave (&s->urb_list_lock, flags);
	list_del (p);
	if (do_lock) spin_unlock_irqrestore (&s->urb_list_lock, flags);
}

/*-------------------------------------------------------------------*/
_static int alloc_td (puhci_desc_t * new, int flags)
{
	*new = (uhci_desc_t *) kmalloc (sizeof (uhci_desc_t), in_interrupt ()? GFP_ATOMIC : GFP_KERNEL);
	if (!*new)
		return -ENOMEM;
	memset (*new, 0, sizeof (uhci_desc_t));
	(*new)->hw.td.link = UHCI_PTR_TERM | (flags & UHCI_PTR_BITS);	// last by default

	(*new)->type = TD_TYPE;
	INIT_LIST_HEAD (&(*new)->vertical);
	INIT_LIST_HEAD (&(*new)->horizontal);
	return 0;
}
/*-------------------------------------------------------------------*/
/* insert td at last position in td-list of qh (vertical) */
_static int insert_td (puhci_t s, puhci_desc_t qh, puhci_desc_t new, int flags)
{
	uhci_desc_t *prev;
	unsigned long xxx;
	
	spin_lock_irqsave (&s->td_lock, xxx);

	list_add_tail (&new->vertical, &qh->vertical);

	if (qh->hw.qh.element & UHCI_PTR_TERM) {
		// virgin qh without any tds
		qh->hw.qh.element = virt_to_bus (new);	/* QH's cannot have the DEPTH bit set */
	}
	else {
		// already tds inserted 
		prev = list_entry (new->vertical.prev, uhci_desc_t, vertical);
		// implicitely remove TERM bit of prev
		prev->hw.td.link = virt_to_bus (new) | (flags & UHCI_PTR_DEPTH);
	}
	spin_unlock_irqrestore (&s->td_lock, xxx);
	return 0;
}
/*-------------------------------------------------------------------*/
/* insert new_td after td (horizontal) */
_static int insert_td_horizontal (puhci_t s, puhci_desc_t td, puhci_desc_t new, int flags)
{
	uhci_desc_t *next;
	unsigned long xxx;
	
	spin_lock_irqsave (&s->td_lock, xxx);

	next = list_entry (td->horizontal.next, uhci_desc_t, horizontal);
	new->hw.td.link = td->hw.td.link;
	list_add (&new->horizontal, &td->horizontal);
	td->hw.td.link = virt_to_bus (new);
	spin_unlock_irqrestore (&s->td_lock, xxx);	
	return 0;
}
/*-------------------------------------------------------------------*/
_static int unlink_td (puhci_t s, puhci_desc_t element)
{
	uhci_desc_t *next, *prev;
	int dir = 0;
	unsigned long xxx;
	
	spin_lock_irqsave (&s->td_lock, xxx);
	next = list_entry (element->vertical.next, uhci_desc_t, vertical);
	if (next == element) {
		dir = 1;
		next = list_entry (element->horizontal.next, uhci_desc_t, horizontal);
		prev = list_entry (element->horizontal.prev, uhci_desc_t, horizontal);
	}
	else {
		prev = list_entry (element->vertical.prev, uhci_desc_t, vertical);
	}
	if (prev->type == TD_TYPE)
		prev->hw.td.link = element->hw.td.link;
	else
		prev->hw.qh.element = element->hw.td.link;
	wmb ();
	if (dir == 0)
		list_del (&element->vertical);
	else
		list_del (&element->horizontal);
	spin_unlock_irqrestore (&s->td_lock, xxx);	
	return 0;
}
/*-------------------------------------------------------------------*/
_static int delete_td (puhci_desc_t element)
{
	kfree (element);
	return 0;
}
/*-------------------------------------------------------------------*/
// Allocates qh element
_static int alloc_qh (puhci_desc_t * new)
{
	*new = (uhci_desc_t *) kmalloc (sizeof (uhci_desc_t), in_interrupt ()? GFP_ATOMIC : GFP_KERNEL);
	if (!*new)
		return -ENOMEM;
	memset (*new, 0, sizeof (uhci_desc_t));
	(*new)->hw.qh.head = UHCI_PTR_TERM;
	(*new)->hw.qh.element = UHCI_PTR_TERM;
	(*new)->type = QH_TYPE;
	INIT_LIST_HEAD (&(*new)->horizontal);
	INIT_LIST_HEAD (&(*new)->vertical);
	dbg (KERN_DEBUG MODSTR "Allocated qh @ %p\n", *new);
	return 0;
}
/*-------------------------------------------------------------------*/
// inserts new qh _before_ the qh at pos
_static int insert_qh (puhci_t s, puhci_desc_t pos, puhci_desc_t new, int flags)
{
	puhci_desc_t prev;
	unsigned long xxx;

	spin_lock_irqsave (&s->qh_lock, xxx);

	prev = list_entry (pos->horizontal.prev, uhci_desc_t, horizontal);
	//  dbg("pos->horizontal.prev:%p pos->horizontal:%p\n", pos->horizontal.prev, &pos->horizontal);  
	//  dbg("insert_qh: prev->hw.qh.head:%p prev:%p new:%p pos:%p\n", prev->hw.qh.head, prev, new, pos);

	list_add_tail (&new->horizontal, &pos->horizontal);
	new->hw.qh.head = MAKE_QH_ADDR (pos) | (flags & UHCI_PTR_BITS);
	if (!(prev->hw.qh.head & UHCI_PTR_TERM))
		prev->hw.qh.head = MAKE_QH_ADDR (new) | (flags & UHCI_PTR_BITS);
	wmb ();
	//  dbg("new->hw.qh.head:%p prev->hw.qh.head:%p\n",new->hw.qh.head,prev->hw.qh.head);  
	spin_unlock_irqrestore (&s->qh_lock, xxx);
	return 0;
}
/*-------------------------------------------------------------------*/
_static int unlink_qh (puhci_t s, puhci_desc_t element)
{
	puhci_desc_t next, prev;
	unsigned long xxx;

	spin_lock_irqsave (&s->qh_lock, xxx);
	next = list_entry (element->horizontal.next, uhci_desc_t, horizontal);
	prev = list_entry (element->horizontal.prev, uhci_desc_t, horizontal);
	prev->hw.qh.head = element->hw.qh.head;
	wmb ();
	list_del (&element->horizontal);
	spin_unlock_irqrestore (&s->qh_lock, xxx);
	return 0;
}
/*-------------------------------------------------------------------*/
_static int delete_qh (puhci_t s, puhci_desc_t qh)
{
	puhci_desc_t td;
	struct list_head *p;

	list_del (&qh->horizontal);
	while ((p = qh->vertical.next) != &qh->vertical) {
		td = list_entry (p, uhci_desc_t, vertical);
		unlink_td (s, td);
		delete_td (td);
	}
	kfree (qh);
	return 0;
}
/*-------------------------------------------------------------------*/
void clean_td_chain (puhci_desc_t td)
{
	struct list_head *p;
	puhci_desc_t td1;

	if (!td)
		return;
	while ((p = td->horizontal.next) != &td->horizontal) {
		td1 = list_entry (p, uhci_desc_t, horizontal);
		delete_td (td1);
	}
	delete_td (td);
}
/*-------------------------------------------------------------------*/
// Removes ALL qhs in chain (paranoia!)
_static void cleanup_skel (puhci_t s)
{
	unsigned int n;
	puhci_desc_t td;

	printk (KERN_DEBUG MODSTR "Cleanup_skel\n");
	for (n = 0; n < 8; n++) {
		td = s->int_chain[n];
		clean_td_chain (td);
	}

	if (s->iso_td) {
		for (n = 0; n < 1024; n++) {
			td = s->iso_td[n];
			clean_td_chain (td);
		}
		kfree (s->iso_td);
	}

	if (s->framelist)
		free_page ((unsigned long) s->framelist);

	if (s->control_chain) {
		// completed init_skel?
		struct list_head *p;
		puhci_desc_t qh, qh1;

		qh = s->control_chain;
		while ((p = qh->horizontal.next) != &qh->horizontal) {
			qh1 = list_entry (p, uhci_desc_t, horizontal);
			delete_qh (s, qh1);
		}
		delete_qh (s, qh);
	}
	else {
		if (s->control_chain)
			kfree (s->control_chain);
		if (s->bulk_chain)
			kfree (s->bulk_chain);
		if (s->chain_end)
			kfree (s->chain_end);
	}
}
/*-------------------------------------------------------------------*/
// allocates framelist and qh-skeletons
// only HW-links provide continous linking, SW-links stay in their domain (ISO/INT)
_static int init_skel (puhci_t s)
{
	int n, ret;
	puhci_desc_t qh, td;
	dbg (KERN_DEBUG MODSTR "init_skel\n");
	s->framelist = (__u32 *) get_free_page (GFP_KERNEL);

	if (!s->framelist)
		return -ENOMEM;

	memset (s->framelist, 0, 4096);

	dbg (KERN_DEBUG MODSTR "allocating iso desc pointer list\n");
	s->iso_td = (puhci_desc_t *) kmalloc (1024 * sizeof (puhci_desc_t), GFP_KERNEL);
	if (!s->iso_td)
		goto init_skel_cleanup;

	s->control_chain = NULL;
	s->bulk_chain = NULL;
	s->chain_end = NULL;

	dbg (KERN_DEBUG MODSTR "allocating iso descs\n");
	for (n = 0; n < 1024; n++) {
	 	// allocate skeleton iso/irq-tds
		ret = alloc_td (&td, 0);
		if (ret)
			goto init_skel_cleanup;
		s->iso_td[n] = td;
		s->framelist[n] = ((__u32) virt_to_bus (td));
	}

	dbg (KERN_DEBUG MODSTR "allocating qh: chain_end\n");
	ret = alloc_qh (&qh);
	if (ret)
		goto init_skel_cleanup;
	s->chain_end = qh;

	dbg (KERN_DEBUG MODSTR "allocating qh: bulk_chain\n");
	ret = alloc_qh (&qh);
	if (ret)
		goto init_skel_cleanup;
	insert_qh (s, s->chain_end, qh, 0);
	s->bulk_chain = qh;
	dbg (KERN_DEBUG MODSTR "allocating qh: control_chain\n");
	ret = alloc_qh (&qh);
	if (ret)
		goto init_skel_cleanup;
	insert_qh (s, s->bulk_chain, qh, 0);
	s->control_chain = qh;
	for (n = 0; n < 8; n++)
		s->int_chain[n] = 0;

	dbg (KERN_DEBUG MODSTR "Allocating skeleton INT-TDs\n");
	for (n = 0; n < 8; n++) {
		puhci_desc_t td;

		alloc_td (&td, 0);
		if (!td)
			goto init_skel_cleanup;
		s->int_chain[n] = td;
		if (n == 0) {
			s->int_chain[0]->hw.td.link = virt_to_bus (s->control_chain);
		}
		else {
			s->int_chain[n]->hw.td.link = virt_to_bus (s->int_chain[0]);
		}
	}

	dbg (KERN_DEBUG MODSTR "Linking skeleton INT-TDs\n");
	for (n = 0; n < 1024; n++) {
		// link all iso-tds to the interrupt chains
		int m, o;
		//dbg("framelist[%i]=%x\n",n,s->framelist[n]);
		for (o = 1, m = 2; m <= 128; o++, m += m) {
			// n&(m-1) = n%m
			if ((n & (m - 1)) == ((m - 1) / 2)) {
				((puhci_desc_t) s->iso_td[n])->hw.td.link = virt_to_bus (s->int_chain[o]);
			}
		}
	}

	//uhci_show_queue(s->control_chain);   
	dbg (KERN_DEBUG MODSTR "init_skel exit\n");
	return 0;		// OK

      init_skel_cleanup:
	cleanup_skel (s);
	return -ENOMEM;
}

/*-------------------------------------------------------------------*/
_static void fill_td (puhci_desc_t td, int status, int info, __u32 buffer)
{
	td->hw.td.status = status;
	td->hw.td.info = info;
	td->hw.td.buffer = buffer;
}

/*-------------------------------------------------------------------*/
//                         LOW LEVEL STUFF
//          assembles QHs und TDs for control, bulk and iso
/*-------------------------------------------------------------------*/
_static int uhci_submit_control_urb (purb_t purb)
{
	puhci_desc_t qh, td;
	puhci_t s = (puhci_t) purb->dev->bus->hcpriv;
	purb_priv_t purb_priv = purb->hcpriv;
	unsigned long destination, status;
	int maxsze = usb_maxpacket (purb->dev, purb->pipe, usb_pipeout (purb->pipe));
	unsigned long len, bytesrequested;
	char *data;

	dbg (KERN_DEBUG MODSTR "uhci_submit_control start\n");
	alloc_qh (&qh);		// alloc qh for this request

	if (!qh)
		return -ENOMEM;

	alloc_td (&td, UHCI_PTR_DEPTH);		// get td for setup stage

	if (!td) {
		delete_qh (s, qh);
		return -ENOMEM;
	}

	/* The "pipe" thing contains the destination in bits 8--18 */
	destination = (purb->pipe & PIPE_DEVEP_MASK) | USB_PID_SETUP;

	/* 3 errors */
	status = (purb->pipe & TD_CTRL_LS) | TD_CTRL_ACTIVE |
		(purb->transfer_flags & USB_DISABLE_SPD ? 0 : TD_CTRL_SPD) | (3 << 27);

	/*  Build the TD for the control request, try forever, 8 bytes of data */
	fill_td (td, status, destination | (7 << 21), virt_to_bus (purb->setup_packet));

	/* If direction is "send", change the frame from SETUP (0x2D)
	   to OUT (0xE1). Else change it from SETUP to IN (0x69). */

	destination ^= (USB_PID_SETUP ^ USB_PID_IN);	/* SETUP -> IN */
	if (usb_pipeout (purb->pipe))
		destination ^= (USB_PID_IN ^ USB_PID_OUT);	/* IN -> OUT */

	insert_td (s, qh, td, 0);	// queue 'setup stage'-td in qh
#if 0
	printk ("SETUP to pipe %x: %x %x %x %x %x %x %x %x\n", purb->pipe,
		purb->setup_packet[0], purb->setup_packet[1], purb->setup_packet[2], purb->setup_packet[3],
		purb->setup_packet[4], purb->setup_packet[5], purb->setup_packet[6], purb->setup_packet[7]);
	//uhci_show_td(td);
#endif
	/*  Build the DATA TD's */
	len = purb->transfer_buffer_length;
	bytesrequested = len;
	data = purb->transfer_buffer;
	while (len > 0) {
		int pktsze = len;

		alloc_td (&td, UHCI_PTR_DEPTH);
		if (!td) {
			delete_qh (s, qh);
			return -ENOMEM;
		}

		if (pktsze > maxsze)
			pktsze = maxsze;

		destination ^= 1 << TD_TOKEN_TOGGLE;	// toggle DATA0/1

		fill_td (td, status, destination | ((pktsze - 1) << 21),
			 virt_to_bus (data));	// Status, pktsze bytes of data

		insert_td (s, qh, td, UHCI_PTR_DEPTH);	// queue 'data stage'-td in qh

		data += pktsze;
		len -= pktsze;
	}

	/*  Build the final TD for control status */
	/* It's only IN if the pipe is out AND we aren't expecting data */
	destination &= ~0xFF;
	if (usb_pipeout (purb->pipe) | (bytesrequested == 0))
		destination |= USB_PID_IN;
	else
		destination |= USB_PID_OUT;

	destination |= 1 << TD_TOKEN_TOGGLE;	/* End in Data1 */

	alloc_td (&td, UHCI_PTR_DEPTH);
	if (!td) {
		delete_qh (s, qh);
		return -ENOMEM;
	}

	/* no limit on errors on final packet , 0 bytes of data */
	fill_td (td, status | TD_CTRL_IOC, destination | (UHCI_NULL_DATA_SIZE << 21),
		 0);

	insert_td (s, qh, td, UHCI_PTR_DEPTH);	// queue status td


	list_add (&qh->desc_list, &purb_priv->desc_list);
	/* Start it up... */
	insert_qh (s, s->bulk_chain, qh, 0);	// insert before bulk chain
	//uhci_show_queue(s->control_chain);

	dbg (KERN_DEBUG MODSTR "uhci_submit_control end\n");
	return 0;
}
/*-------------------------------------------------------------------*/
_static int uhci_submit_bulk_urb (purb_t purb)
{
	puhci_t s = (puhci_t) purb->dev->bus->hcpriv;
	purb_priv_t purb_priv = purb->hcpriv;
	puhci_desc_t qh, td;
	unsigned long destination, status;
	char *data;
	unsigned int pipe = purb->pipe;
	int maxsze = usb_maxpacket (purb->dev, pipe, usb_pipeout (pipe));
	int info, len;

	/* shouldn't the clear_halt be done in the USB core or in the client driver? - Thomas */
	if (usb_endpoint_halted (purb->dev, usb_pipeendpoint (pipe), usb_pipeout (pipe)) &&
	    usb_clear_halt (purb->dev, usb_pipeendpoint (pipe) | (pipe & USB_DIR_IN)))
		return -EPIPE;

	if (!maxsze)
		return -EMSGSIZE;
	/* FIXME: should tell the client that the endpoint is invalid, i.e. not in the descriptor */

	alloc_qh (&qh);		// get qh for this request

	if (!qh)
		return -ENOMEM;

	/* The "pipe" thing contains the destination in bits 8--18. */
	destination = (pipe & PIPE_DEVEP_MASK) | usb_packetid (pipe);

	/* 3 errors */
	status = (pipe & TD_CTRL_LS) | TD_CTRL_ACTIVE |
		((purb->transfer_flags & USB_DISABLE_SPD) ? 0 : TD_CTRL_SPD) | (3 << 27);

	/* Build the TDs for the bulk request */
	len = purb->transfer_buffer_length;
	data = purb->transfer_buffer;
	dbg (KERN_DEBUG MODSTR "uhci_submit_bulk_urb: len %d\n", len);
	while (len > 0) {
		int pktsze = len;

		alloc_td (&td, UHCI_PTR_DEPTH);

		if (!td) {
			delete_qh (s, qh);
			return -ENOMEM;
		}

		if (pktsze > maxsze)
			pktsze = maxsze;

		// pktsze bytes of data 
		info = destination | ((pktsze - 1) << 21) |
			(usb_gettoggle (purb->dev, usb_pipeendpoint (pipe), usb_pipeout (pipe)) << TD_TOKEN_TOGGLE);

		fill_td (td, status, info, virt_to_bus (data));

		data += pktsze;
		len -= pktsze;

		if (!len)
			td->hw.td.status |= TD_CTRL_IOC;	// last one generates INT
		//dbg("insert td %p, len %i\n",td,pktsze);

		insert_td (s, qh, td, UHCI_PTR_DEPTH);

		/* Alternate Data0/1 (start with Data0) */
		usb_dotoggle (purb->dev, usb_pipeendpoint (pipe), usb_pipeout (pipe));
	}

	list_add (&qh->desc_list, &purb_priv->desc_list);

	insert_qh (s, s->chain_end, qh, 0);	// insert before end marker
	//uhci_show_queue(s->bulk_chain);

	dbg (KERN_DEBUG MODSTR "uhci_submit_bulk_urb: exit\n");
	return 0;
}
/*-------------------------------------------------------------------*/
// unlinks an urb by dequeuing its qh, waits some frames and forgets it
// Problem: unlinking in interrupt requires waiting for one frame (udelay)
// to allow the whole structures to be safely removed
// Maybe an unlink-queue and a KILLED-completion handler would be much
// cleaner...
_static int uhci_unlink_urb (purb_t purb)
{
	puhci_t s;
	puhci_desc_t qh;
	puhci_desc_t td;
	purb_priv_t purb_priv;
	unsigned long flags;
	struct list_head *p;

	if (!purb)		// you never know... 

		return -1;

	s = (puhci_t) purb->dev->bus->hcpriv;	// get pointer to uhci struct
#ifdef VROOTHUB
	if (usb_pipedevice (purb->pipe) == s->rh.devnum)
		return rh_unlink_urb (purb);
#endif
	// is the following really necessary? dequeue_urb has its own spinlock (GA)
	spin_lock_irqsave (&s->unlink_urb_lock, flags);		// do not allow interrupts

	//dbg("unlink_urb called %p\n",purb);
	if (purb->status == USB_ST_URB_PENDING) {
		// URB probably still in work
		purb_priv = purb->hcpriv;
		dequeue_urb (s, &purb->urb_list,1);
		spin_unlock_irqrestore (&s->unlink_urb_lock, flags);	// allow interrupts from here

		switch (usb_pipetype (purb->pipe)) {
		case PIPE_ISOCHRONOUS:
		case PIPE_INTERRUPT:
			for (p = purb_priv->desc_list.next; p != &purb_priv->desc_list; p = p->next) {
				td = list_entry (p, uhci_desc_t, desc_list);
				unlink_td (s, td);
			}
			// wait at least 1 Frame
			if (in_interrupt ())
				udelay (1000);
			else
				schedule_timeout (1 + 1 * HZ / 1000);
			while ((p = purb_priv->desc_list.next) != &purb_priv->desc_list) {
				td = list_entry (p, uhci_desc_t, desc_list);
				list_del (p);
				delete_td (td);
			}
			break;

		case PIPE_BULK:
		case PIPE_CONTROL:
			qh = list_entry (purb_priv->desc_list.next, uhci_desc_t, desc_list);

			unlink_qh (s, qh);	// remove this qh from qh-list
			// wait at least 1 Frame

			if (in_interrupt ())
				udelay (1000);
			else
				schedule_timeout (1 + 1 * HZ / 1000);
			delete_qh (s, qh);		// remove it physically

		}
		purb->status = USB_ST_URB_KILLED;	// mark urb as killed

		kfree (purb->hcpriv);
		if (purb->complete) {
			dbg (KERN_DEBUG MODSTR "unlink_urb: calling completion\n");
			purb->complete ((struct urb *) purb);
			usb_dec_dev_use (purb->dev);
		}
		return 0;
	}
	else
		spin_unlock_irqrestore (&s->unlink_urb_lock, flags);	// allow interrupts from here

	return 0;
}
/*-------------------------------------------------------------------*/
// In case of ASAP iso transfer, search the URB-list for already queued URBs
// for this EP and calculate the earliest start frame for the new
// URB (easy seamless URB continuation!)
_static int find_iso_limits (purb_t purb, unsigned int *start, unsigned int *end)
{
	purb_t u, last_urb = NULL;
	puhci_t s = (puhci_t) purb->dev->bus->hcpriv;
	struct list_head *p = s->urb_list.next;
	int ret=-1;
	unsigned long flags;
	spin_lock_irqsave (&s->urb_list_lock, flags);

	for (; p != &s->urb_list; p = p->next) {
		u = list_entry (p, urb_t, urb_list);
		// look for pending URBs with identical pipe handle
		// works only because iso doesn't toggle the data bit!
		if ((purb->pipe == u->pipe) && (purb->dev == u->dev) && (u->status == USB_ST_URB_PENDING)) {
			if (!last_urb)
				*start = u->start_frame;
			last_urb = u;
		}
	}
	if (last_urb) {
		*end = (last_urb->start_frame + last_urb->number_of_packets) & 1023;
		ret=0;
	}
	spin_unlock_irqrestore(&s->urb_list_lock, flags);
	return ret;	// no previous urb found

}
/*-------------------------------------------------------------------*/
// adjust start_frame according to scheduling constraints (ASAP etc)

_static void jnx_show_desc (puhci_desc_t d)
{
	switch (d->type) {
	case TD_TYPE:
		printk (KERN_DEBUG MODSTR "td @ 0x%08lx: link 0x%08x status 0x%08x info 0x%08x buffer 0x%08x\n",
			(unsigned long) d, d->hw.td.link, d->hw.td.status, d->hw.td.info, d->hw.td.buffer);
		if (!(d->hw.td.link & UHCI_PTR_TERM))
			jnx_show_desc ((puhci_desc_t) bus_to_virt (d->hw.td.link & ~UHCI_PTR_BITS));
		break;

	case QH_TYPE:
		printk (KERN_DEBUG MODSTR "qh @ 0x%08lx: head 0x%08x element 0x%08x\n",
			(unsigned long) d, d->hw.qh.head, d->hw.qh.element);
		if (!(d->hw.qh.element & UHCI_PTR_TERM))
			jnx_show_desc ((puhci_desc_t) bus_to_virt (d->hw.qh.element & ~UHCI_PTR_BITS));
		if (!(d->hw.qh.head & UHCI_PTR_TERM))
			jnx_show_desc ((puhci_desc_t) bus_to_virt (d->hw.qh.head & ~UHCI_PTR_BITS));
		break;

	default:
		printk (KERN_DEBUG MODSTR "desc @ 0x%08lx: invalid type %u\n",
			(unsigned long) d, d->type);
	}
}

/*-------------------------------------------------------------------*/
_static int iso_find_start (purb_t purb)
{
	puhci_t s = (puhci_t) purb->dev->bus->hcpriv;
	unsigned int now;
	unsigned int start_limit = 0, stop_limit = 0, queued_size;
	int limits;

	now = UHCI_GET_CURRENT_FRAME (s) & 1023;

	if ((unsigned) purb->number_of_packets > 900)
		return -EFBIG;
	limits = find_iso_limits (purb, &start_limit, &stop_limit);
	queued_size = (stop_limit - start_limit) & 1023;

	if (purb->transfer_flags & USB_ISO_ASAP) {
		// first iso
		if (limits) {
			// 10ms setup should be enough //FIXME!
			purb->start_frame = (now + 10) & 1023;
		}
		else {
			purb->start_frame = stop_limit;		//seamless linkage

			if (((now - purb->start_frame) & 1023) <= (unsigned) purb->number_of_packets) {
				printk (KERN_DEBUG MODSTR "iso_find_start: warning, ASAP gap, should not happen\n");
				printk (KERN_DEBUG MODSTR "iso_find_start: now %u start_frame %u number_of_packets %u pipe 0x%08x\n",
					now, purb->start_frame, purb->number_of_packets, purb->pipe);
				{
					puhci_t s = (puhci_t) purb->dev->bus->hcpriv;
					struct list_head *p = s->urb_list.next;
					purb_t u;
					int a = -1, b = -1;
					unsigned long flags;
					spin_lock_irqsave (&s->urb_list_lock, flags);

					for (; p != &s->urb_list; p = p->next) {
						u = list_entry (p, urb_t, urb_list);
						if (purb->dev != u->dev)
							continue;
						printk (KERN_DEBUG MODSTR "urb: pipe 0x%08x status %d start_frame %u number_of_packets %u\n",
							u->pipe, u->status, u->start_frame, u->number_of_packets);
						if (!usb_pipeisoc (u->pipe))
							continue;
						if (a == -1)
							a = u->start_frame;
						b = (u->start_frame + u->number_of_packets - 1) & 1023;
					}
					spin_unlock_irqrestore(&s->urb_list_lock, flags);
#if 0
					if (a != -1 && b != -1) {
						do {
							jnx_show_desc (s->iso_td[a]);
							a = (a + 1) & 1023;
						}
						while (a != b);
					}
#endif
				}
				purb->start_frame = (now + 5) & 1023;	// 5ms setup should be enough //FIXME!
				//return -EAGAIN; //FIXME

			}
		}
	}
	else {
		purb->start_frame &= 1023;
		if (((now - purb->start_frame) & 1023) < (unsigned) purb->number_of_packets) {
			printk (KERN_DEBUG MODSTR "iso_find_start: now between start_frame and end\n");
			return -EAGAIN;
		}
	}

	/* check if either start_frame or start_frame+number_of_packets-1 lies between start_limit and stop_limit */
	if (limits)
		return 0;
	if (((purb->start_frame - start_limit) & 1023) < queued_size ||
	    ((purb->start_frame + purb->number_of_packets - 1 - start_limit) & 1023) < queued_size) {
		printk (KERN_DEBUG MODSTR "iso_find_start: start_frame %u number_of_packets %u start_limit %u stop_limit %u\n",
			purb->start_frame, purb->number_of_packets, start_limit, stop_limit);
		return -EAGAIN;
	}
	return 0;
}
/*-------------------------------------------------------------------*/
// submits USB interrupt (ie. polling ;-) 
// ASAP-flag set implicitely
// if period==0, the the transfer is only done once (usb_scsi need this...)

_static int uhci_submit_int_urb (purb_t purb)
{
	puhci_t s = (puhci_t) purb->dev->bus->hcpriv;
	purb_priv_t purb_priv = purb->hcpriv;
	int nint, n, ret;
	puhci_desc_t td;
	int status, destination;
	int now;
	int info;
	unsigned int pipe = purb->pipe;

	//printk("SUBMIT INT\n");

	if (purb->interval < 0 || purb->interval >= 256)
		return -EINVAL;

	if (purb->interval == 0)
		nint = 0;
	else {
		for (nint = 0, n = 1; nint <= 8; nint++, n += n)	// round interval down to 2^n
		 {
			if (purb->interval < n) {
				purb->interval = n / 2;
				break;
			}
		}
		nint--;
	}
	dbg(KERN_INFO "Rounded interval to %i, chain  %i\n", purb->interval, nint);

	now = UHCI_GET_CURRENT_FRAME (s) & 1023;
	purb->start_frame = now;	// remember start frame, just in case...

	purb->number_of_packets = 1;

	// INT allows only one packet
	if (purb->transfer_buffer_length > usb_maxpacket (purb->dev, pipe, usb_pipeout (pipe)))
		return -EINVAL;

	ret = alloc_td (&td, UHCI_PTR_DEPTH);
	if (ret)
		return -ENOMEM;

	status = (pipe & TD_CTRL_LS) | TD_CTRL_ACTIVE | TD_CTRL_IOC |
		(purb->transfer_flags & USB_DISABLE_SPD ? 0 : TD_CTRL_SPD) | (1 << 27);

	destination = (purb->pipe & PIPE_DEVEP_MASK) | usb_packetid (purb->pipe) |
		(((purb->transfer_buffer_length - 1) & 0x7ff) << 21);


	info = destination | (usb_gettoggle (purb->dev, usb_pipeendpoint (pipe), usb_pipeout (pipe)) << TD_TOKEN_TOGGLE);

	fill_td (td, status, info, virt_to_bus (purb->transfer_buffer));
	list_add_tail (&td->desc_list, &purb_priv->desc_list);
	insert_td_horizontal (s, s->int_chain[nint], td, UHCI_PTR_DEPTH);	// store in INT-TDs

	usb_dotoggle (purb->dev, usb_pipeendpoint (pipe), usb_pipeout (pipe));

#if 0
	td = tdm[purb->number_of_packets];
	fill_td (td, TD_CTRL_IOC, 0, 0);
	insert_td_horizontal (s, s->iso_td[(purb->start_frame + (purb->number_of_packets) * purb->interval + 1) & 1023], td, UHCI_PTR_DEPTH);
	list_add_tail (&td->desc_list, &purb_priv->desc_list);
#endif

	return 0;
}
/*-------------------------------------------------------------------*/
_static int uhci_submit_iso_urb (purb_t purb)
{
	puhci_t s = (puhci_t) purb->dev->bus->hcpriv;
	purb_priv_t purb_priv = purb->hcpriv;
	int n, ret;
	puhci_desc_t td, *tdm;
	int status, destination;
	unsigned long flags;
	spinlock_t lock;

	spin_lock_init (&lock);
	spin_lock_irqsave (&lock, flags);	// Disable IRQs to schedule all ISO-TDs in time

	ret = iso_find_start (purb);	// adjusts purb->start_frame for later use

	if (ret)
		goto err;

	tdm = (puhci_desc_t *) kmalloc (purb->number_of_packets * sizeof (puhci_desc_t), in_interrupt ()? GFP_ATOMIC : GFP_KERNEL);
	if (!tdm) {
		ret = -ENOMEM;
		goto err;
	}
	// First try to get all TDs
	for (n = 0; n < purb->number_of_packets; n++) {
		dbg (KERN_DEBUG MODSTR "n:%d purb->iso_frame_desc[n].length:%d\n", n, purb->iso_frame_desc[n].length);
		if (!purb->iso_frame_desc[n].length) {
			// allows ISO striping by setting length to zero in iso_descriptor
			tdm[n] = 0;
			continue;
		}
		ret = alloc_td (&td, UHCI_PTR_DEPTH);
		if (ret) {
			int i;	// Cleanup allocated TDs

			for (i = 0; i < n; n++)
				if (tdm[i])
					kfree (tdm[i]);
			kfree (tdm);
			ret = -ENOMEM;
			goto err;
		}
		tdm[n] = td;
	}

	status = TD_CTRL_ACTIVE | TD_CTRL_IOS;	//| (purb->transfer_flags&USB_DISABLE_SPD?0:TD_CTRL_SPD);

	destination = (purb->pipe & PIPE_DEVEP_MASK) | usb_packetid (purb->pipe);

	// Queue all allocated TDs
	for (n = 0; n < purb->number_of_packets; n++) {
		td = tdm[n];
		if (!td)
			continue;
		if (n + 1 >= purb->number_of_packets)
			status |= TD_CTRL_IOC;

		fill_td (td, status, destination | (((purb->iso_frame_desc[n].length - 1) & 0x7ff) << 21),
			 virt_to_bus (purb->transfer_buffer + purb->iso_frame_desc[n].offset));
		list_add_tail (&td->desc_list, &purb_priv->desc_list);
		insert_td_horizontal (s, s->iso_td[(purb->start_frame + n) & 1023], td, UHCI_PTR_DEPTH);	// store in iso-tds
		//uhci_show_td(td);

	}
	kfree (tdm);
	dbg ("ISO-INT# %i, start %i, now %i\n", purb->number_of_packets, purb->start_frame, UHCI_GET_CURRENT_FRAME (s) & 1023);
	ret = 0;

      err:
	spin_unlock_irqrestore (&lock, flags);
	return ret;

}
/*-------------------------------------------------------------------*/
_static int search_dev_ep (puhci_t s, purb_t purb)
{
	unsigned long flags;
	struct list_head *p = s->urb_list.next;
	purb_t tmp;
	dbg (KERN_DEBUG MODSTR "search_dev_ep:\n");
	spin_lock_irqsave (&s->urb_list_lock, flags);
	for (; p != &s->urb_list; p = p->next) {
		tmp = list_entry (p, urb_t, urb_list);
		dbg (KERN_DEBUG MODSTR "urb: %p\n", tmp);
		// we can accept this urb if it is not queued at this time 
		// or if non-iso transfer requests should be scheduled for the same device and pipe
		if ((usb_pipetype (purb->pipe) != PIPE_ISOCHRONOUS &&
		     tmp->dev == purb->dev && tmp->pipe == purb->pipe) || (purb == tmp))
			return 1;	// found another urb already queued for processing

	}
	spin_unlock_irqrestore (&s->urb_list_lock, flags);
	return 0;
}
/*-------------------------------------------------------------------*/
_static int uhci_submit_urb (purb_t purb)
{
	puhci_t s;
	purb_priv_t purb_priv;
	int ret = 0;

	if (!purb->dev || !purb->dev->bus)
		return -ENODEV;

	s = (puhci_t) purb->dev->bus->hcpriv;
	//printk( MODSTR"submit_urb: %p type %d\n",purb,usb_pipetype(purb->pipe));
#ifdef VROOTHUB
	if (usb_pipedevice (purb->pipe) == s->rh.devnum)
		return rh_submit_urb (purb);	/* virtual root hub */
#endif
	usb_inc_dev_use (purb->dev);

	if (search_dev_ep (s, purb)) {
		usb_dec_dev_use (purb->dev);
		return -ENXIO;	// no such address

	}
	purb_priv = kmalloc (sizeof (urb_priv_t), in_interrupt ()? GFP_ATOMIC : GFP_KERNEL);
	if (!purb_priv) {
		usb_dec_dev_use (purb->dev);
		return -ENOMEM;
	}
	purb->hcpriv = purb_priv;
	INIT_LIST_HEAD (&purb_priv->desc_list);
	purb_priv->short_control_packet=0;
	dbg (KERN_DEBUG MODSTR "submit_urb: scheduling %p\n", purb);
	switch (usb_pipetype (purb->pipe)) {
	case PIPE_ISOCHRONOUS:
		ret = uhci_submit_iso_urb (purb);
		break;
	case PIPE_INTERRUPT:
		ret = uhci_submit_int_urb (purb);
		break;
	case PIPE_CONTROL:
		//dump_urb (purb);
		ret = uhci_submit_control_urb (purb);
		break;
	case PIPE_BULK:
		ret = uhci_submit_bulk_urb (purb);
		break;
	default:
		ret = -EINVAL;
	}
	dbg (KERN_DEBUG MODSTR "submit_urb: scheduled with ret: %d\n", ret);
	if (ret != USB_ST_NOERROR) {
		usb_dec_dev_use (purb->dev);
		kfree (purb_priv);
		return ret;
	}
	purb->status = USB_ST_URB_PENDING;
	queue_urb (s, &purb->urb_list,1);
	dbg (KERN_DEBUG MODSTR "submit_urb: exit\n");
	return 0;
}
/*-------------------------------------------------------------------
 Virtual Root Hub
 -------------------------------------------------------------------*/
#ifdef VROOTHUB
_static __u8 root_hub_dev_des[] =
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
_static __u8 root_hub_config_des[] =
{
	0x09,			/*  __u8  bLength; */
	0x02,			/*  __u8  bDescriptorType; Configuration */
	0x19,			/*  __u16 wTotalLength; */
	0x00,
	0x01,			/*  __u8  bNumInterfaces; */
	0x01,			/*  __u8  bConfigurationValue; */
	0x00,			/*  __u8  iConfiguration; */
	0x40,			/*  __u8  bmAttributes; 
				   Bit 7: Bus-powered, 6: Self-powered, 5 Remote-wakwup, 4..0: resvd */
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


_static __u8 root_hub_hub_des[] =
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
_static int rh_send_irq (purb_t purb)
{

	int len = 1;
	int i;
	puhci_t uhci = purb->dev->bus->hcpriv;
	unsigned int io_addr = uhci->io_addr;
	__u16 data = 0;

	for (i = 0; i < uhci->rh.numports; i++) {
		data |= ((inw (io_addr + USBPORTSC1 + i * 2) & 0xa) > 0 ? (1 << (i + 1)) : 0);
		len = (i + 1) / 8 + 1;
	}

	*(__u16 *) purb->transfer_buffer = cpu_to_le16 (data);
	purb->actual_length = len;
	purb->status = USB_ST_NOERROR;

	if ((data > 0) && (uhci->rh.send != 0)) {
		dbg (KERN_DEBUG MODSTR "Root-Hub INT complete: port1: %x port2: %x data: %x\n",
		     inw (io_addr + USBPORTSC1), inw (io_addr + USBPORTSC2), data);
		purb->complete (purb);

	}
	return USB_ST_NOERROR;
}

/*-------------------------------------------------------------------------*/
/* Virtual Root Hub INTs are polled by this timer every "intervall" ms */
_static int rh_init_int_timer (purb_t purb);

_static void rh_int_timer_do (unsigned long ptr)
{
	int len;

	purb_t purb = (purb_t) ptr;
	puhci_t uhci = purb->dev->bus->hcpriv;

	if (uhci->rh.send) {
		len = rh_send_irq (purb);
		if (len > 0) {
			purb->actual_length = len;
			if (purb->complete)
				purb->complete (purb);
		}
	}
	rh_init_int_timer (purb);
}

/*-------------------------------------------------------------------------*/
/* Root Hub INTs are polled by this timer */
_static int rh_init_int_timer (purb_t purb)
{
	puhci_t uhci = purb->dev->bus->hcpriv;

	uhci->rh.interval = purb->interval;
	init_timer (&uhci->rh.rh_int_timer);
	uhci->rh.rh_int_timer.function = rh_int_timer_do;
	uhci->rh.rh_int_timer.data = (unsigned long) purb;
	uhci->rh.rh_int_timer.expires = jiffies + (HZ * (purb->interval < 30 ? 30 : purb->interval)) / 1000;
	add_timer (&uhci->rh.rh_int_timer);

	return 0;
}

/*-------------------------------------------------------------------------*/
#define OK(x) 			len = (x); break

#define CLR_RH_PORTSTAT(x) \
		status = inw(io_addr+USBPORTSC1+2*(wIndex-1)); \
		status = (status & 0xfff5) & ~(x); \
		outw(status, io_addr+USBPORTSC1+2*(wIndex-1))

#define SET_RH_PORTSTAT(x) \
		status = inw(io_addr+USBPORTSC1+2*(wIndex-1)); \
		status = (status & 0xfff5) | (x); \
		outw(status, io_addr+USBPORTSC1+2*(wIndex-1))


/*-------------------------------------------------------------------------*/
/****
 ** Root Hub Control Pipe
 *************************/


_static int rh_submit_urb (purb_t purb)
{
	struct usb_device *usb_dev = purb->dev;
	puhci_t uhci = usb_dev->bus->hcpriv;
	unsigned int pipe = purb->pipe;
	devrequest *cmd = (devrequest *) purb->setup_packet;
	void *data = purb->transfer_buffer;
	int leni = purb->transfer_buffer_length;
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

	if (usb_pipetype (pipe) == PIPE_INTERRUPT) {
		dbg (KERN_DEBUG MODSTR "Root-Hub submit IRQ: every %d ms\n", purb->interval);
		uhci->rh.urb = purb;
		uhci->rh.send = 1;
		uhci->rh.interval = purb->interval;
		rh_init_int_timer (purb);

		return USB_ST_NOERROR;
	}


	bmRType_bReq = cmd->requesttype | cmd->request << 8;
	wValue = le16_to_cpu (cmd->value);
	wIndex = le16_to_cpu (cmd->index);
	wLength = le16_to_cpu (cmd->length);

	for (i = 0; i < 8; i++)
		uhci->rh.c_p_r[i] = 0;

	dbg (KERN_DEBUG MODSTR "Root-Hub: adr: %2x cmd(%1x): %04 %04 %04 %04\n",
	     uhci->rh.devnum, 8, bmRType_bReq, wValue, wIndex, wLength);

	switch (bmRType_bReq) {
		/* Request Destination:
		   without flags: Device, 
		   RH_INTERFACE: interface, 
		   RH_ENDPOINT: endpoint,
		   RH_CLASS means HUB here, 
		   RH_OTHER | RH_CLASS  almost ever means HUB_PORT here 
		 */

	case RH_GET_STATUS:
		*(__u16 *) data = cpu_to_le16 (1);
		OK (2);
	case RH_GET_STATUS | RH_INTERFACE:
		*(__u16 *) data = cpu_to_le16 (0);
		OK (2);
	case RH_GET_STATUS | RH_ENDPOINT:
		*(__u16 *) data = cpu_to_le16 (0);
		OK (2);
	case RH_GET_STATUS | RH_CLASS:
		*(__u32 *) data = cpu_to_le32 (0);
		OK (4);		/* hub power ** */
	case RH_GET_STATUS | RH_OTHER | RH_CLASS:
		status = inw (io_addr + USBPORTSC1 + 2 * (wIndex - 1));
		cstatus = ((status & USBPORTSC_CSC) >> (1 - 0)) |
			((status & USBPORTSC_PEC) >> (3 - 1)) |
			(uhci->rh.c_p_r[wIndex - 1] << (0 + 4));
		status = (status & USBPORTSC_CCS) |
			((status & USBPORTSC_PE) >> (2 - 1)) |
			((status & USBPORTSC_SUSP) >> (12 - 2)) |
			((status & USBPORTSC_PR) >> (9 - 4)) |
			(1 << 8) |	/* power on ** */
			((status & USBPORTSC_LSDA) << (-8 + 9));

		*(__u16 *) data = cpu_to_le16 (status);
		*(__u16 *) (data + 2) = cpu_to_le16 (cstatus);
		OK (4);

	case RH_CLEAR_FEATURE | RH_ENDPOINT:
		switch (wValue) {
		case (RH_ENDPOINT_STALL):
			OK (0);
		}
		break;

	case RH_CLEAR_FEATURE | RH_CLASS:
		switch (wValue) {
		case (RH_C_HUB_OVER_CURRENT):
			OK (0);	/* hub power over current ** */
		}
		break;

	case RH_CLEAR_FEATURE | RH_OTHER | RH_CLASS:
		switch (wValue) {
		case (RH_PORT_ENABLE):
			CLR_RH_PORTSTAT (USBPORTSC_PE);
			OK (0);
		case (RH_PORT_SUSPEND):
			CLR_RH_PORTSTAT (USBPORTSC_SUSP);
			OK (0);
		case (RH_PORT_POWER):
			OK (0);	/* port power ** */
		case (RH_C_PORT_CONNECTION):
			SET_RH_PORTSTAT (USBPORTSC_CSC);
			OK (0);
		case (RH_C_PORT_ENABLE):
			SET_RH_PORTSTAT (USBPORTSC_PEC);
			OK (0);
		case (RH_C_PORT_SUSPEND):
/*** WR_RH_PORTSTAT(RH_PS_PSSC); */
			OK (0);
		case (RH_C_PORT_OVER_CURRENT):
			OK (0);	/* port power over current ** */
		case (RH_C_PORT_RESET):
			uhci->rh.c_p_r[wIndex - 1] = 0;
			OK (0);
		}
		break;

	case RH_SET_FEATURE | RH_OTHER | RH_CLASS:
		switch (wValue) {
		case (RH_PORT_SUSPEND):
			SET_RH_PORTSTAT (USBPORTSC_SUSP);
			OK (0);
		case (RH_PORT_RESET):
			SET_RH_PORTSTAT (USBPORTSC_PR);
			wait_ms (10);
			uhci->rh.c_p_r[wIndex - 1] = 1;
			CLR_RH_PORTSTAT (USBPORTSC_PR);
			udelay (10);
			SET_RH_PORTSTAT (USBPORTSC_PE);
			wait_ms (10);
			SET_RH_PORTSTAT (0xa);
			OK (0);
		case (RH_PORT_POWER):
			OK (0);	/* port power ** */
		case (RH_PORT_ENABLE):
			SET_RH_PORTSTAT (USBPORTSC_PE);
			OK (0);
		}
		break;

	case RH_SET_ADDRESS:
		uhci->rh.devnum = wValue;
		OK (0);

	case RH_GET_DESCRIPTOR:
		switch ((wValue & 0xff00) >> 8) {
		case (0x01):	/* device descriptor */
			len = min (leni, min (sizeof (root_hub_dev_des), wLength));
			memcpy (data, root_hub_dev_des, len);
			OK (len);
		case (0x02):	/* configuration descriptor */
			len = min (leni, min (sizeof (root_hub_config_des), wLength));
			memcpy (data, root_hub_config_des, len);
			OK (len);
		case (0x03):	/*string descriptors */
			stat = -EPIPE;
		}
		break;

	case RH_GET_DESCRIPTOR | RH_CLASS:
		root_hub_hub_des[2] = uhci->rh.numports;
		len = min (leni, min (sizeof (root_hub_hub_des), wLength));
		memcpy (data, root_hub_hub_des, len);
		OK (len);

	case RH_GET_CONFIGURATION:
		*(__u8 *) data = 0x01;
		OK (1);

	case RH_SET_CONFIGURATION:
		OK (0);
	default:
		stat = -EPIPE;
	}


	dbg (KERN_DEBUG MODSTR "Root-Hub stat port1: %x port2: %x \n",
	     inw (io_addr + USBPORTSC1), inw (io_addr + USBPORTSC2));

	purb->actual_length = len;
	purb->status = stat;
	if (purb->complete)
		purb->complete (purb);
	return USB_ST_NOERROR;
}
/*-------------------------------------------------------------------------*/

_static int rh_unlink_urb (purb_t purb)
{
	puhci_t uhci = purb->dev->bus->hcpriv;

	dbg (KERN_DEBUG MODSTR "Root-Hub unlink IRQ\n");
	uhci->rh.send = 0;
	del_timer (&uhci->rh.rh_int_timer);
	return 0;
}
/*-------------------------------------------------------------------*/
#endif

#define UHCI_DEBUG

/*
 * Map status to standard result codes
 *
 * <status> is (td->status & 0xFE0000) [a.k.a. uhci_status_bits(td->status)
 * <dir_out> is True for output TDs and False for input TDs.
 */
_static int uhci_map_status (int status, int dir_out)
{
	if (!status)
		return USB_ST_NOERROR;
	if (status & TD_CTRL_BITSTUFF)	/* Bitstuff error */
		return USB_ST_BITSTUFF;
	if (status & TD_CTRL_CRCTIMEO) {	/* CRC/Timeout */
		if (dir_out)
			return USB_ST_NORESPONSE;
		else
			return USB_ST_CRC;
	}
	if (status & TD_CTRL_NAK)	/* NAK */
		return USB_ST_TIMEOUT;
	if (status & TD_CTRL_BABBLE)	/* Babble */
		return -EPIPE;
	if (status & TD_CTRL_DBUFERR)	/* Buffer error */
		return USB_ST_BUFFERUNDERRUN;
	if (status & TD_CTRL_STALLED)	/* Stalled */
		return -EPIPE;
	if (status & TD_CTRL_ACTIVE)	/* Active */
		return USB_ST_NOERROR;

	return USB_ST_INTERNALERROR;
}

/*
 * Only the USB core should call uhci_alloc_dev and uhci_free_dev
 */
_static int uhci_alloc_dev (struct usb_device *usb_dev)
{
	// hint: inc module usage count
	return 0;
}

_static int uhci_free_dev (struct usb_device *usb_dev)
{
	// hint: dec module usage count
	return 0;
}

/*
 * uhci_get_current_frame_number()
 *
 * returns the current frame number for a USB bus/controller.
 */
_static int uhci_get_current_frame_number (struct usb_device *usb_dev)
{
	return UHCI_GET_CURRENT_FRAME ((puhci_t) usb_dev->bus->hcpriv);
}

struct usb_operations uhci_device_operations =
{
	uhci_alloc_dev,
	uhci_free_dev,
	uhci_get_current_frame_number,
	uhci_submit_urb,
	uhci_unlink_urb
};

/* 
 * For IN-control transfers, process_transfer gets a bit more complicated,
 * since there are devices that return less data (eg. strings) than they
 * have announced. This leads to a queue abort due to the short packet,
 * the status stage is not executed. If this happens, the status stage
 * is manually re-executed.
 * FIXME: Stall-condition may override 'nearly' successful CTRL-IN-transfer
 * when the transfered length fits exactly in maxsze-packets. A bit
 * more intelligence is needed to detect this and finish without error.
 */
_static int process_transfer (puhci_t s, purb_t purb)
{
	int ret = USB_ST_NOERROR;
	purb_priv_t purb_priv = purb->hcpriv;
	struct list_head *qhl = purb_priv->desc_list.next;
	puhci_desc_t qh = list_entry (qhl, uhci_desc_t, desc_list);
	struct list_head *p = qh->vertical.next;
	puhci_desc_t desc;
	puhci_desc_t last_desc = list_entry (purb_priv->desc_list.prev, uhci_desc_t, desc_list);
	int data_toggle = usb_gettoggle (purb->dev, usb_pipeendpoint (purb->pipe), usb_pipeout (purb->pipe));	// save initial data_toggle
	int skip_active = 0;

	// extracted and remapped info from TD
	int maxlength;
	int actual_length;
	int status = USB_ST_NOERROR;

	dbg (KERN_DEBUG MODSTR "process_transfer: urb contains bulk/control request\n");


	/* if the urb is a control-transfer, data has been recieved and the
	   queue is empty or the last status-TD is inactive, the retriggered
	   status stage is completed
	 */
#if 1
	if (purb_priv->short_control_packet && 
		((qh->hw.qh.element == UHCI_PTR_TERM)||(!(last_desc->hw.td.status & TD_CTRL_ACTIVE)))) 
		goto transfer_finished;
#endif
	purb->actual_length=0;
	for (; p != &qh->vertical; p = p->next) {
		desc = list_entry (p, uhci_desc_t, vertical);

		if ((desc->hw.td.status & TD_CTRL_ACTIVE) && (skip_active == 0))	// do not process active TDs
			return ret;
#if 1
		if (skip_active) {
			if ((desc->hw.td.info & 0xff) == USB_PID_OUT) {
				uhci_show_td (desc);
				qh->hw.qh.element = virt_to_bus (desc);		// trigger status stage
				//printk("retrigger\n");
				purb_priv->short_control_packet=1;
				return 0;
			}
			continue;
		}
#endif
		// extract transfer parameters from TD
		actual_length = (desc->hw.td.status + 1) & 0x7ff;
		maxlength = (((desc->hw.td.info >> 21) & 0x7ff) + 1) & 0x7ff;
		status = uhci_map_status (uhci_status_bits (desc->hw.td.status), usb_pipeout (purb->pipe));

		// see if EP is stalled
		if (status == -EPIPE) {
			// set up stalled condition
			usb_endpoint_halt (purb->dev, usb_pipeendpoint (purb->pipe), usb_pipeout (purb->pipe));
		}
		// if any error occured stop processing of further TDs
		if (status != USB_ST_NOERROR) {
			// only set ret if status returned an error
			uhci_show_td (desc);
			ret = status;
			purb->error_count++;
			break;
		}
		else if ((desc->hw.td.info & 0xff) != USB_PID_SETUP)
			purb->actual_length += actual_length;

#if 0
		//if (i++==0)
		if (skip_active)
			uhci_show_td (desc);	// show first TD of each transfer
#endif

		// go into error condition to keep data_toggle unchanged if short packet occurs
		if ((purb->transfer_flags & USB_DISABLE_SPD) && (actual_length < maxlength)) {
			ret = USB_ST_SHORT_PACKET;
			printk (KERN_DEBUG MODSTR "process_transfer: SPD!!\n");
			break;	// exit after this TD because SP was detected

		}

		data_toggle = uhci_toggle (desc->hw.td.info);
		//printk(KERN_DEBUG MODSTR"process_transfer: len:%d status:%x mapped:%x toggle:%d\n", actual_length, desc->hw.td.status,status, data_toggle);      
#if 1
		if ((usb_pipetype (purb->pipe) == PIPE_CONTROL) && (actual_length < maxlength)) {
			skip_active = 1;
			//printk("skip_active...\n");
		}
#endif
	}
	usb_settoggle (purb->dev, usb_pipeendpoint (purb->pipe), usb_pipeout (purb->pipe), !data_toggle);
      transfer_finished:

	unlink_qh (s, qh);
	delete_qh (s, qh);

	purb->status = status;
	dbg(KERN_DEBUG MODSTR"process_transfer: urb %p, wanted len %d, len %d status %x err %d\n",
		purb,purb->transfer_buffer_length,purb->actual_length, purb->status, purb->error_count);
	//dbg(KERN_DEBUG MODSTR"process_transfer: exit\n");
	return ret;
}

_static int process_interrupt (puhci_t s, purb_t purb)
{
	int i, ret = USB_ST_URB_PENDING;
	purb_priv_t purb_priv = purb->hcpriv;
	struct list_head *p = purb_priv->desc_list.next;
	puhci_desc_t desc = list_entry (purb_priv->desc_list.prev, uhci_desc_t, desc_list);
	int data_toggle = usb_gettoggle (purb->dev, usb_pipeendpoint (purb->pipe),
					 usb_pipeout (purb->pipe));	// save initial data_toggle
	// extracted and remapped info from TD

	int actual_length;
	int status = USB_ST_NOERROR;

	//printk(KERN_DEBUG MODSTR"urb contains interrupt request\n");

	for (i = 0; p != &purb_priv->desc_list; p = p->next, i++)	// Maybe we allow more than one TD later ;-)

	{
		desc = list_entry (p, uhci_desc_t, desc_list);

		if (desc->hw.td.status & TD_CTRL_NAK) {
			// NAKed transfer
			//printk("TD NAK Status @%p %08x\n",desc,desc->hw.td.status);
			goto err;
		}

		if (desc->hw.td.status & TD_CTRL_ACTIVE) {
			// do not process active TDs
			//printk("TD ACT Status @%p %08x\n",desc,desc->hw.td.status);
			goto err;
		}

		if (!desc->hw.td.status & TD_CTRL_IOC) {
			// do not process one-shot TDs
			goto err;
		}
		// extract transfer parameters from TD

		actual_length = (desc->hw.td.status + 1) & 0x7ff;
		status = uhci_map_status (uhci_status_bits (desc->hw.td.status), usb_pipeout (purb->pipe));

		// see if EP is stalled
		if (status == -EPIPE) {
			// set up stalled condition
			usb_endpoint_halt (purb->dev, usb_pipeendpoint (purb->pipe), usb_pipeout (purb->pipe));
		}
		// if any error occured: ignore this td, and continue
		if (status != USB_ST_NOERROR) {
			purb->error_count++;
			goto err;
		}
		else
			purb->actual_length = actual_length;

		// FIXME: SPD?

		data_toggle = uhci_toggle (desc->hw.td.info);

		if (purb->complete && status != USB_ST_TIMEOUT) {
			// for last td, no user completion is needed
			dbg (KERN_DEBUG MODSTR "process_interrupt: calling completion\n");
			purb->status = status;
			purb->complete ((struct urb *) purb);
			purb->status = USB_ST_URB_PENDING;
		}

		// Recycle INT-TD if interval!=0, else mark TD as one-shot
		if (purb->interval) {
			desc->hw.td.status |= TD_CTRL_ACTIVE;
			desc->hw.td.info &= ~(1 << TD_TOKEN_TOGGLE);
			desc->hw.td.info |= (usb_gettoggle (purb->dev, usb_pipeendpoint (purb->pipe),
			      usb_pipeout (purb->pipe)) << TD_TOKEN_TOGGLE);
			usb_dotoggle (purb->dev, usb_pipeendpoint (purb->pipe), usb_pipeout (purb->pipe));
		}
		else {
			desc->hw.td.status &= ~TD_CTRL_IOC;
		}

	      err:
	}

	return ret;
}


_static int process_iso (puhci_t s, purb_t purb)
{
	int i;
	int ret = USB_ST_NOERROR;
	purb_priv_t purb_priv = purb->hcpriv;
	struct list_head *p = purb_priv->desc_list.next;
	puhci_desc_t desc = list_entry (purb_priv->desc_list.prev, uhci_desc_t, desc_list);

	dbg ( /*KERN_DEBUG */ MODSTR "urb contains iso request\n");
	if (desc->hw.td.status & TD_CTRL_ACTIVE)
		return USB_ST_PARTIAL_ERROR;	// last TD not finished

	purb->error_count = 0;
	purb->actual_length = 0;
	purb->status = USB_ST_NOERROR;

	for (i = 0; p != &purb_priv->desc_list; p = p->next, i++) {
		desc = list_entry (p, uhci_desc_t, desc_list);
		//uhci_show_td(desc);
		if (desc->hw.td.status & TD_CTRL_ACTIVE) {
			// means we have completed the last TD, but not the TDs before
			printk (KERN_DEBUG MODSTR "TD still active (%x)- grrr. paranoia!\n", desc->hw.td.status);
			ret = USB_ST_PARTIAL_ERROR;
			purb->iso_frame_desc[i].status = ret;
			unlink_td (s, desc);
			goto err;
		}
		unlink_td (s, desc);
		if (purb->number_of_packets <= i) {
			dbg (KERN_DEBUG MODSTR "purb->number_of_packets (%d)<=(%d)\n", purb->number_of_packets, i);
			ret = USB_ST_URB_INVALID_ERROR;
			goto err;
		}

		if (purb->iso_frame_desc[i].offset + purb->transfer_buffer != bus_to_virt (desc->hw.td.buffer)) {
			// Hm, something really weird is going on
			dbg (KERN_DEBUG MODSTR "Pointer Paranoia: %p!=%p\n", purb->iso_frame_desc[i].offset + purb->transfer_buffer, bus_to_virt (desc->hw.td.buffer));
			ret = USB_ST_URB_INVALID_ERROR;
			purb->iso_frame_desc[i].status = ret;
			goto err;
		}
		purb->iso_frame_desc[i].actual_length = (desc->hw.td.status + 1) & 0x7ff;
		purb->iso_frame_desc[i].status = uhci_map_status (uhci_status_bits (desc->hw.td.status), usb_pipeout (purb->pipe));
		purb->actual_length += purb->iso_frame_desc[i].actual_length;

	      err:
		if (purb->iso_frame_desc[i].status != USB_ST_NOERROR) {
			purb->error_count++;
			purb->status = purb->iso_frame_desc[i].status;
		}
		dbg (KERN_DEBUG MODSTR "process_iso: len:%d status:%x\n",
		     purb->iso_frame_desc[i].length, purb->iso_frame_desc[i].status);

		delete_td (desc);
		list_del (p);
	}
	dbg ( /*KERN_DEBUG */ MODSTR "process_iso: exit %i (%d)\n", i, ret);
	return ret;
}


_static int process_urb (puhci_t s, struct list_head *p)
{
	int ret = USB_ST_NOERROR;
	unsigned long flags;
	purb_t purb = list_entry (p, urb_t, urb_list);
	spin_lock_irqsave (&s->urb_list_lock, flags);

	dbg ( /*KERN_DEBUG */ MODSTR "found queued urb: %p\n", purb);
	switch (usb_pipetype (purb->pipe)) {
	case PIPE_CONTROL:
	case PIPE_BULK:
		ret = process_transfer (s, purb);
		break;
	case PIPE_ISOCHRONOUS:
		ret = process_iso (s, purb);
		break;
	case PIPE_INTERRUPT:
		ret = process_interrupt (s, purb);
		break;
	}
	spin_unlock_irqrestore(&s->urb_list_lock, flags);

	if (purb->status != USB_ST_URB_PENDING) {
		int proceed = 0;
		dbg ( /*KERN_DEBUG */ MODSTR "dequeued urb: %p\n", purb);
		dequeue_urb (s, p, 1);
		kfree (purb->hcpriv);

		if ((usb_pipetype (purb->pipe) != PIPE_INTERRUPT)) {
			purb_t tmp = purb->next;	// pointer to first urb

			int is_ring = 0;
			if (purb->next) {
				do {
					if (tmp->status != USB_ST_URB_PENDING) {
						proceed = 1;
						break;
					}
					tmp = tmp->next;
				}
				while (tmp != NULL && tmp != purb->next);
				if (tmp == purb->next)
					is_ring = 1;
			}

			// In case you need the current URB status for your completion handler
			if (purb->complete && (!proceed || (purb->transfer_flags & USB_URB_EARLY_COMPLETE))) {
				dbg (KERN_DEBUG MODSTR "process_transfer: calling early completion\n");
				purb->complete ((struct urb *) purb);
				if (!proceed && is_ring)
					uhci_submit_urb (purb);
			}

			if (proceed && purb->next) {
				// if there are linked urbs - handle submitting of them right now.
				tmp = purb->next;	// pointer to first urb

				do {
					if ((tmp->status != USB_ST_URB_PENDING) && uhci_submit_urb (tmp) != USB_ST_NOERROR)
						break;
					tmp = tmp->next;
				}
				while (tmp != NULL && tmp != purb->next);	// submit until we reach NULL or our own pointer or submit fails


				if (purb->complete && !(purb->transfer_flags & USB_URB_EARLY_COMPLETE)) {
					dbg ( /*KERN_DEBUG */ MODSTR "process_transfer: calling completion\n");
					purb->complete ((struct urb *) purb);
				}
			}
			usb_dec_dev_use (purb->dev);
		}
	}

	return ret;
}

_static void uhci_interrupt (int irq, void *__uhci, struct pt_regs *regs)
{
	puhci_t s = __uhci;
	unsigned int io_addr = s->io_addr;
	unsigned short status;
	struct list_head *p, *p2;

	/*
	 * Read the interrupt status, and write it back to clear the
	 * interrupt cause
	 */
	dbg ("interrupt\n");
	status = inw (io_addr + USBSTS);
	if (!status)		/* shared interrupt, not mine */
		return;

	if (status != 1) {
		printk (KERN_DEBUG MODSTR "interrupt, status %x\n", status);
		uhci_show_status (s);
	}
	//beep(1000);           
	/*
	 * the following is very subtle and was blatantly wrong before
	 * traverse the list in *reverse* direction, because new entries
	 * may be added at the end.
	 * also, because process_urb may unlink the current urb,
	 * we need to advance the list before
	 * - Thomas Sailer
	 */
	spin_lock (&s->urb_list_lock);
	p = s->urb_list.prev;
	spin_unlock (&s->urb_list_lock);

	while (p != &s->urb_list) {
		p2 = p;
		p = p->prev;
		process_urb (s, p2);
	}
	outw (status, io_addr + USBSTS);
#ifdef __alpha
	mb ();			// ?
#endif
	dbg ("done\n");
}

_static void reset_hc (puhci_t s)
{
	unsigned int io_addr = s->io_addr;

	s->apm_state = 0;
	/* Global reset for 50ms */
	outw (USBCMD_GRESET, io_addr + USBCMD);
	wait_ms (50);
	outw (0, io_addr + USBCMD);
	wait_ms (10);
}

_static void start_hc (puhci_t s)
{
	unsigned int io_addr = s->io_addr;
	int timeout = 1000;

	/*
	 * Reset the HC - this will force us to get a
	 * new notification of any already connected
	 * ports due to the virtual disconnect that it
	 * implies.
	 */
	outw (USBCMD_HCRESET, io_addr + USBCMD);
	while (inw (io_addr + USBCMD) & USBCMD_HCRESET) {
		if (!--timeout) {
			printk (KERN_ERR MODSTR "USBCMD_HCRESET timed out!\n");
			break;
		}
	}

	/* Turn on all interrupts */
	outw (USBINTR_TIMEOUT | USBINTR_RESUME | USBINTR_IOC | USBINTR_SP, io_addr + USBINTR);

	/* Start at frame 0 */
	outw (0, io_addr + USBFRNUM);
	outl (virt_to_bus (s->framelist), io_addr + USBFLBASEADD);

	/* Run and mark it configured with a 64-byte max packet */
	outw (USBCMD_RS | USBCMD_CF | USBCMD_MAXP, io_addr + USBCMD);
	s->apm_state = 1;
}

#ifdef VROOTHUB
_static int uhci_start_usb (puhci_t s)
{				/* start it up */
	/* connect the virtual root hub */
	struct usb_device *usb_dev;

	usb_dev = usb_alloc_dev (NULL, s->bus);
	if (!usb_dev)
		return -1;

	s->bus->root_hub = usb_dev;
	usb_connect (usb_dev);

	if (usb_new_device (usb_dev) != 0) {
		usb_free_dev (usb_dev);
		return -1;
	}
	return 0;
}
#endif


_static puhci_t alloc_uhci (unsigned int io_addr, unsigned int io_size)
{
	puhci_t s;
	struct usb_bus *bus;
#ifndef VROOTHUB	
	struct usb_device *usb;
#endif
	s = kmalloc (sizeof (uhci_t), GFP_KERNEL);
	if (!s)
		return NULL;

	memset (s, 0, sizeof (uhci_t));
	INIT_LIST_HEAD (&s->urb_list);
	spin_lock_init (&s->urb_list_lock);
	spin_lock_init (&s->qh_lock);
	spin_lock_init (&s->td_lock);
	spin_lock_init (&s->unlink_urb_lock);
	s->irq = -1;
	s->io_addr = io_addr;
	s->io_size = io_size;
	s->next = devs;		//chain new uhci device into global list

	devs = s;

	bus = usb_alloc_bus (&uhci_device_operations);
	if (!bus)
		goto err_bus;

	s->bus = bus;
	bus->hcpriv = s;
#if 1
#ifndef VROOTHUB
	/*
	 * Allocate the root_hub
	 */
	usb = usb_alloc_dev (NULL, bus);
	if (!usb)
		goto err_dev;

	s->bus->root_hub = usb;
#endif
	/* Initialize the root hub */

	/* UHCI specs says devices must have 2 ports, but goes on to say */
	/* they may have more but give no way to determine how many they */
	/* have, so default to 2 */
	/* According to the UHCI spec, Bit 7 is always set to 1. So we try */
	/* to use this to our advantage */
	for (s->maxports = 0; s->maxports < (io_size - 0x10) / 2; s->maxports++) {
		unsigned int portstatus;

		portstatus = inw (io_addr + 0x10 + (s->maxports * 2));
		printk ("port %i, adr %x status %x\n", s->maxports,
			io_addr + 0x10 + (s->maxports * 2), portstatus);
		if (!(portstatus & 0x0080))
			break;
	}
	dbg (KERN_DEBUG MODSTR "Detected %d ports\n", s->maxports);

	/* This is experimental so anything less than 2 or greater than 8 is */
	/*  something weird and we'll ignore it */
	if (s->maxports < 2 || s->maxports > 8) {
		dbg (KERN_DEBUG "Port count misdetected, forcing to 2 ports\n");
		s->maxports = 2;
	}
#ifndef VROOTHUB
	usb->maxchild = s->maxports;
	usb_init_root_hub (usb);
#endif
#endif
#ifdef VROOTHUB
	s->rh.numports = s->maxports;
#endif
	return s;

/*
 * error exits:
 */
#ifndef VROOTHUB
      err_dev:
#endif      
	usb_free_bus (bus);
      err_bus:
	kfree (s);
	return NULL;
}

#ifndef VROOTHUB
/*
 * This is just incredibly fragile. The timings must be just
 * right, and they aren't really documented very well.
 *
 * Note the short delay between disabling reset and enabling
 * the port..
 */
_static void uhci_reset_port (unsigned int port)
{
	unsigned short status;
	status = inw (port);
	outw (status | USBPORTSC_PR, port);	/* reset port */
	wait_ms (10);
	outw (status & ~USBPORTSC_PR, port);
	udelay (5);

	status = inw (port);
	outw (status | USBPORTSC_PE, port);	/* enable port */
	wait_ms (10);

	status = inw (port);
	if (!(status & USBPORTSC_PE)) {
		outw (status | USBPORTSC_PE, port);	/* one more try at enabling port */
		wait_ms (50);
	}
}

/*
 * Check port status - Connect Status Change - for
 * each of the attached ports (defaults to two ports,
 * but at least in theory there can be more of them).
 */
_static int uhci_root_hub_events (puhci_t s)
{
	int io_addr = s->io_addr;
	int ports = s->maxports;

	io_addr += USBPORTSC1;
	do {
		if (inw (io_addr) & USBPORTSC_CSC)
			return 1;
		io_addr += 2;
	}
	while (--ports > 0);
	return 0;
}
/*
 * This gets called if the connect status on the root
 * hub (and the root hub only) changes.
 */
_static void uhci_connect_change (puhci_t s, unsigned int port, unsigned int nr)
{
	struct usb_device *usb_dev;
	unsigned short status;
	struct usb_device *root_hub = s->bus->root_hub;

#if 1				// UHCI_DEBUG
	printk (KERN_INFO MODSTR "uhci_connect_change: called for %d\n", nr);
#endif

	/*
	 * Even if the status says we're connected,
	 * the fact that the status bits changed may
	 * that we got disconnected and then reconnected.
	 *
	 * So start off by getting rid of any old devices..
	 */
	// dbg("before disconnect\n");
	usb_disconnect (&root_hub->children[nr]);
	status = inw (port);
	//dbg("after disconnect\n");
	/* If we have nothing connected, then clear change status and disable the port */
	status = (status & ~USBPORTSC_PE) | USBPORTSC_PEC;
	if (!(status & USBPORTSC_CCS)) {
		outw (status, port);
		return;
	}

	/*
	 * Ok, we got a new connection. Allocate a device to it,
	 * and find out what it wants to do..
	 */
	usb_dev = usb_alloc_dev (root_hub, root_hub->bus);

	if (!usb_dev)
		return;

	usb_connect (usb_dev);

	root_hub->children[nr] = usb_dev;
	wait_ms (200);		/* wait for powerup */
	uhci_reset_port (port);

	/* Get speed information */
	usb_dev->slow = (inw (port) & USBPORTSC_LSDA) ? 1 : 0;

	/*
	 * Ok, all the stuff specific to the root hub has been done.
	 * The rest is generic for any new USB attach, regardless of
	 * hub type.
	 */
	if (usb_new_device (usb_dev)) {
		unsigned short status = inw (port);
		dbg (KERN_INFO MODSTR "disabling malfunctioning port %d\n", nr + 1);
		outw (status | USBPORTSC_PE, port);
	}

}

/*
 * This gets called when the root hub configuration
 * has changed. Just go through each port, seeing if
 * there is something interesting happening.
 */
_static void uhci_check_configuration (puhci_t s)
{
	unsigned int io_addr = s->io_addr + USBPORTSC1;
	int nr = 0;

	do {
		unsigned short status = inw (io_addr);

		if (status & USBPORTSC_CSC)
			uhci_connect_change (s, io_addr, nr);
		dbg ("after connchange\n");
		nr++;
		io_addr += 2;
	}
	while (nr < s->maxports);
	//dbg("after check conf\n");
}

_static int uhci_control_thread (void *__uhci)
{
	puhci_t s = (puhci_t) __uhci;

	s->control_running = 1;

	lock_kernel ();

	exit_mm (current);
	exit_files (current);

	strcpy (current->comm, "uhci-control");
	unlock_kernel ();
	/*
	 * Ok, all systems are go..
	 */
	do {
		if (s->apm_state) {
			if (uhci_root_hub_events (s))
				uhci_check_configuration (s);
		}

		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout (HZ);

		if (signal_pending (current)) {
			siginfo_t info;
			int signr;
			/* sending SIGUSR1 makes us print out some info */
			spin_lock_irq (&current->sigmask_lock);
			signr = dequeue_signal (&current->blocked, &info);
			spin_unlock_irq (&current->sigmask_lock);
			dbg (KERN_DEBUG MODSTR "signal %d\n", signr);
			if (signr == SIGUSR1) {
				dbg (KERN_DEBUG "UHCI queue dump:\n");
				uhci_show_queue (s->control_chain);
			}
			else if (signr == SIGUSR2) {
				purb_t purb;
				struct list_head *p = s->urb_list.next;
				for (; p != &s->urb_list; p = p->next) {
					purb = list_entry (p, urb_t, urb_list);
					printk (KERN_DEBUG MODSTR "found queued urb:%p type:%d\n", purb, usb_pipetype (purb->pipe));
				}
			}
		}
	}
	while (s->control_continue);
	s->control_running = 0;
	return 0;
}
#endif

/*
 * If we've successfully found a UHCI, now is the time to increment the
 * module usage count, start the control thread, and return success..
 */
_static int found_uhci (int irq, unsigned int io_addr, unsigned int io_size)
{
	puhci_t s;

	s = alloc_uhci (io_addr, io_size);
	if (!s)
		return -ENOMEM;

	if (init_skel (s))
		return -ENOMEM;

	request_region (s->io_addr, io_size, "usb-uhci");
	reset_hc (s);
	usb_register_bus (s->bus);

	start_hc (s);
	s->control_continue = 1;

	if (!request_irq (irq, uhci_interrupt, SA_SHIRQ, "uhci", s)) {

		s->irq = irq;
#ifdef VROOTHUB
		return uhci_start_usb (s);
#endif
#ifndef VROOTHUB
		uhci_check_configuration (s);
// #if 1
		s->control_pid = kernel_thread (uhci_control_thread, s, 0);	//CLONE_FS | CLONE_FILES | CLONE_SIGHAND);

		if (s->control_pid >= 0)
			return 0;
#endif
	}
	reset_hc (s);
	release_region (s->io_addr, s->io_size);
	free_irq (irq, s);
	return -1;
}

_static int start_uhci (struct pci_dev *dev)
{
	int i;

	/* Search for the IO base address.. */
	for (i = 0; i < 6; i++) {
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,3,8)
		unsigned int io_addr = dev->resource[i].start;
		unsigned int io_size =
		dev->resource[i].end - dev->resource[i].start + 1;
		if (!(dev->resource[i].flags & 1))
			continue;
#else
		unsigned int io_addr = dev->base_address[i];
		unsigned int io_size = 0x14;
		if (!(io_addr & 1))
			continue;
		io_addr &= ~1;
#endif

		/* Is it already in use? */
		if (check_region (io_addr, io_size))
			break;
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,3,8)
		pci_enable_device (dev);
#endif
		/* disable legacy emulation */
		pci_write_config_word (dev, USBLEGSUP, USBLEGSUP_DEFAULT);

		return found_uhci (dev->irq, io_addr, io_size);
	}
	return -1;
}

#ifdef CONFIG_APM
_static int handle_apm_event (apm_event_t event)
{
	static int down = 0;
	puhci_t s = devs;
	printk ("handle_apm_event(%d)\n", event);
	switch (event) {
	case APM_SYS_SUSPEND:
	case APM_USER_SUSPEND:
		if (down) {
			dbg (KERN_DEBUG MODSTR "received extra suspend event\n");
			break;
		}
		while (s) {
			reset_hc (s);
			s = s->next;
		}
		down = 1;
		break;
	case APM_NORMAL_RESUME:
	case APM_CRITICAL_RESUME:
		if (!down) {
			dbg (KERN_DEBUG MODSTR "received bogus resume event\n");
			break;
		}
		down = 0;
		while (s) {
			start_hc (s);
			s = s->next;
		}
		break;
	}
	return 0;
}
#endif

int uhci_init (void)
{
	int retval = -ENODEV;
	struct pci_dev *dev = NULL;
	u8 type;

	printk (KERN_INFO MODSTR VERSTR "\n");
	for (;;) {
		dev = pci_find_class (PCI_CLASS_SERIAL_USB << 8, dev);
		if (!dev)
			break;

		/* Is it UHCI */
		pci_read_config_byte (dev, PCI_CLASS_PROG, &type);
		if (type != 0)
			continue;

		/* Ok set it up */
		retval = start_uhci (dev);
		if (retval)
			break;
	}
#ifdef CONFIG_APM
	apm_register_callback (&handle_apm_event);
#endif
	return retval;
}

void uhci_cleanup (void)
{
#ifndef VROOTHUB
	int ret, i;
#endif
	puhci_t s;

	while ((s = devs)) {
		struct usb_device *root_hub = s->bus->root_hub;
		devs = devs->next;
#ifndef VROOTHUB
		s->control_continue = 0;
		/* Check if the process is still running */
		ret = kill_proc (s->control_pid, 0, 1);
		if (!ret) {
			/* Try a maximum of 10 seconds */
			int count = 10 * 100;

			while (s->control_running && --count) {
				current->state = TASK_INTERRUPTIBLE;
				schedule_timeout (1);
			}

			if (!count)
				printk (KERN_ERR MODSTR "giving up on killing uhci-control\n");
		}

		if (root_hub)
			for (i = 0; i < root_hub->maxchild; i++)
				usb_disconnect (root_hub->children + i);
#endif
#ifdef VROOTHUB
		if (root_hub)
			usb_disconnect (&root_hub);
#endif
		usb_deregister_bus (s->bus);

		reset_hc (s);
		release_region (s->io_addr, s->io_size);
		free_irq (s->irq, s);
		usb_free_bus (s->bus);
		cleanup_skel (s);
		kfree (s);
	}
}

#ifdef MODULE
int init_module (void)
{
	return uhci_init ();
}

void cleanup_module (void)
{
#ifdef CONFIG_APM
	apm_unregister_callback (&handle_apm_event);
#endif
	uhci_cleanup ();
}

#endif //MODULE
