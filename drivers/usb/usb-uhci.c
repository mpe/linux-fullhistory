/* 
 * Universal Host Controller Interface driver for USB (take II).
 *
 * (c) 1999 Georg Acher, acher@in.tum.de (executive slave) (base guitar)
 *          Deti Fliegl, deti@fliegl.de (executive slave) (lead voice)
 *          Thomas Sailer, sailer@ife.ee.ethz.ch (chief consultant) (cheer leader)
 *          Roman Weissgaerber, weissg@vienna.at (virt root hub) (studio porter)
 *          
 * HW-initalization based on material of
 *
 * (C) Copyright 1999 Linus Torvalds
 * (C) Copyright 1999 Johannes Erdfelt
 * (C) Copyright 1999 Randy Dunlap
 *
 * $Id: usb-uhci.c,v 1.197 2000/02/15 17:44:22 acher Exp $
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
#include <linux/interrupt.h>	/* for in_interrupt() */
#include <linux/init.h>

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>

/* This enables more detailed sanity checks in submit_iso */
//#define ISO_SANITY_CHECK

/* This enables debug printks */
#define DEBUG

/* This enables all symbols to be exported, to ease debugging oopses */
//#define DEBUG_SYMBOLS

/* This enables an extra UHCI slab for memory debugging */
#define DEBUG_SLAB

#include "usb.h"
#include "usb-uhci.h"
#include "usb-uhci-debug.h"

#undef DEBUG
#undef dbg
#define dbg(format, arg...) do {} while (0)

#include <linux/pm.h>

#ifdef DEBUG_SYMBOLS
	#define _static
	#ifndef EXPORT_SYMTAB
	#define EXPORT_SYMTAB
	#endif
#else
	#define _static static
#endif

#ifdef DEBUG_SLAB
	static kmem_cache_t *uhci_desc_kmem;
	static kmem_cache_t *urb_priv_kmem;
#endif

#define USE_CTRL_DEPTH_FIRST 0  // 0: Breadth first, 1: Depth first
#define USE_BULK_DEPTH_FIRST 0  // 0: Breadth first, 1: Depth first

#ifdef CONFIG_USB_UHCI_HIGH_BANDWIDTH
#define USE_RECLAMATION_LOOP
#else
//#define USE_RECLAMATION_LOOP
#endif

// stop bandwidth reclamation after (roughly) 50ms (depends also on
// hub polling interval)
#define IDLE_TIMEOUT  (HZ/20)

_static int rh_submit_urb (urb_t *urb);
_static int rh_unlink_urb (urb_t *urb);
_static int delete_qh (uhci_t *s, uhci_desc_t *qh);

static uhci_t *devs = NULL;

/* used by userspace UHCI data structure dumper */
uhci_t **uhci_devices = &devs;

/*-------------------------------------------------------------------*/
// Cleans up collected QHs
void clean_descs(uhci_t *s, int force)
{
	struct list_head *q;
	uhci_desc_t *qh;
	int now=UHCI_GET_CURRENT_FRAME(s);

	q=s->free_desc.prev;

	while (q != &s->free_desc) {
		qh = list_entry (q, uhci_desc_t, horizontal);
		if ((qh->last_used!=now) || force)
			delete_qh(s,qh);
		q=qh->horizontal.prev;
	}
}
/*-------------------------------------------------------------------*/
#ifdef USE_RECLAMATION_LOOP
_static void enable_desc_loop(uhci_t *s, urb_t *urb)
{
	int flags;
	
	dbg("enable_desc_loop: enter");
	
	spin_lock_irqsave (&s->qh_lock, flags);
	s->chain_end->hw.qh.head=virt_to_bus(s->control_chain)|UHCI_PTR_QH;
	s->loop_usage++;
	((urb_priv_t*)urb->hcpriv)->use_loop=1;
	spin_unlock_irqrestore (&s->qh_lock, flags);
	
	dbg("enable_desc_loop: finished");
}
/*-------------------------------------------------------------------*/
_static void disable_desc_loop(uhci_t *s, urb_t *urb)
{
	int flags;
	
	dbg("disable_desc_loop: enter\n");
	
	spin_lock_irqsave (&s->qh_lock, flags);

	if (((urb_priv_t*)urb->hcpriv)->use_loop) {
		s->loop_usage--;

		if (!s->loop_usage)
			s->chain_end->hw.qh.head=UHCI_PTR_TERM;

		((urb_priv_t*)urb->hcpriv)->use_loop=0;
	}
	spin_unlock_irqrestore (&s->qh_lock, flags);
	
	dbg("disable_desc_loop: finished");

}
#endif
/*-------------------------------------------------------------------*/
_static void queue_urb (uhci_t *s, urb_t *urb)
{
	unsigned long flags=0;
	struct list_head *p=&urb->urb_list;
	

	spin_lock_irqsave (&s->urb_list_lock, flags);

#ifdef USE_RECLAMATION_LOOP
	{
		int type;
		type=usb_pipetype (urb->pipe);

		if ((type == PIPE_BULK) || (type == PIPE_CONTROL))
			enable_desc_loop(s, urb);
	}
#endif
	((urb_priv_t*)urb->hcpriv)->started=jiffies;
	list_add_tail (p, &s->urb_list);
	
	spin_unlock_irqrestore (&s->urb_list_lock, flags);
}

/*-------------------------------------------------------------------*/
_static void dequeue_urb (uhci_t *s, urb_t *urb)
{
#ifdef USE_RECLAMATION_LOOP
	int type;

	type=usb_pipetype (urb->pipe);

	if ((type == PIPE_BULK) || (type == PIPE_CONTROL))
		disable_desc_loop(s, urb);
#endif

	list_del (&urb->urb_list);
}
/*-------------------------------------------------------------------*/
_static int alloc_td (uhci_desc_t ** new, int flags)
{
#ifdef DEBUG_SLAB
	*new= kmem_cache_alloc(uhci_desc_kmem, in_interrupt ()? SLAB_ATOMIC : SLAB_KERNEL);
#else
	*new = (uhci_desc_t *) kmalloc (sizeof (uhci_desc_t), in_interrupt ()? GFP_ATOMIC : GFP_KERNEL);
#endif
	if (!*new)
		return -ENOMEM;
	 memset (*new, 0, sizeof (uhci_desc_t));
	(*new)->hw.td.link = UHCI_PTR_TERM | (flags & UHCI_PTR_BITS);	// last by default
	(*new)->type = TD_TYPE;
	mb();
	INIT_LIST_HEAD (&(*new)->vertical);
	INIT_LIST_HEAD (&(*new)->horizontal);
	
	return 0;
}
/*-------------------------------------------------------------------*/
/* insert td at last position in td-list of qh (vertical) */
_static int insert_td (uhci_t *s, uhci_desc_t *qh, uhci_desc_t* new, int flags)
{
	uhci_desc_t *prev;
	unsigned long xxx;
	
	spin_lock_irqsave (&s->td_lock, xxx);

	list_add_tail (&new->vertical, &qh->vertical);

	prev = list_entry (new->vertical.prev, uhci_desc_t, vertical);

	if (qh == prev ) {
		// virgin qh without any tds
		qh->hw.qh.element = virt_to_bus (new);
	}
	else {
		// already tds inserted, implicitely remove TERM bit of prev
		prev->hw.td.link = virt_to_bus (new) | (flags & UHCI_PTR_DEPTH);
	}
	mb();
	spin_unlock_irqrestore (&s->td_lock, xxx);
	
	return 0;
}
/*-------------------------------------------------------------------*/
/* insert new_td after td (horizontal) */
_static int insert_td_horizontal (uhci_t *s, uhci_desc_t *td, uhci_desc_t* new)
{
	uhci_desc_t *next;
	unsigned long flags;
	
	spin_lock_irqsave (&s->td_lock, flags);

	next = list_entry (td->horizontal.next, uhci_desc_t, horizontal);
	list_add (&new->horizontal, &td->horizontal);
	new->hw.td.link = td->hw.td.link;
	td->hw.td.link = virt_to_bus (new);
	mb();
	spin_unlock_irqrestore (&s->td_lock, flags);	
	
	return 0;
}
/*-------------------------------------------------------------------*/
_static int unlink_td (uhci_t *s, uhci_desc_t *element, int phys_unlink)
{
	uhci_desc_t *next, *prev;
	int dir = 0;
	unsigned long flags;
	
	spin_lock_irqsave (&s->td_lock, flags);
	
	next = list_entry (element->vertical.next, uhci_desc_t, vertical);
	
	if (next == element) {
		dir = 1;
		prev = list_entry (element->horizontal.prev, uhci_desc_t, horizontal);
	}
	else 
		prev = list_entry (element->vertical.prev, uhci_desc_t, vertical);
	
	if (phys_unlink) {
		// really remove HW linking
		if (prev->type == TD_TYPE)
			prev->hw.td.link = element->hw.td.link;
		else
			prev->hw.qh.element = element->hw.td.link;	
	}

	element->hw.td.link=UHCI_PTR_TERM;
	mb ();

	if (dir == 0)
		list_del (&element->vertical);
	else
		list_del (&element->horizontal);
	
	spin_unlock_irqrestore (&s->td_lock, flags);	
	
	return 0;
}

/*-------------------------------------------------------------------*/
_static int delete_desc (uhci_desc_t *element)
{
#ifdef DEBUG_SLAB
	kmem_cache_free(uhci_desc_kmem, element);
#else
	kfree (element);
#endif
	return 0;
}
/*-------------------------------------------------------------------*/
// Allocates qh element
_static int alloc_qh (uhci_desc_t ** new)
{
#ifdef DEBUG_SLAB
	*new= kmem_cache_alloc(uhci_desc_kmem, in_interrupt ()? SLAB_ATOMIC : SLAB_KERNEL);
#else
	*new = (uhci_desc_t *) kmalloc (sizeof (uhci_desc_t), in_interrupt ()? GFP_ATOMIC : GFP_KERNEL);
#endif	
	if (!*new)
		return -ENOMEM;
	memset (*new, 0, sizeof (uhci_desc_t));
	(*new)->hw.qh.head = UHCI_PTR_TERM;
	(*new)->hw.qh.element = UHCI_PTR_TERM;
	(*new)->type = QH_TYPE;
	
	mb();
	INIT_LIST_HEAD (&(*new)->horizontal);
	INIT_LIST_HEAD (&(*new)->vertical);
	
	dbg("Allocated qh @ %p", *new);
	
	return 0;
}
/*-------------------------------------------------------------------*/
// inserts new qh before/after the qh at pos
// flags: 0: insert before pos, 1: insert after pos (for low speed transfers)
_static int insert_qh (uhci_t *s, uhci_desc_t *pos, uhci_desc_t *new, int order)
{
	uhci_desc_t *old;
	unsigned long flags;

	spin_lock_irqsave (&s->qh_lock, flags);

	if (!order) {
		// (OLD) (POS) -> (OLD) (NEW) (POS)
		old = list_entry (pos->horizontal.prev, uhci_desc_t, horizontal);
		list_add_tail (&new->horizontal, &pos->horizontal);
		new->hw.qh.head = MAKE_QH_ADDR (pos) ;
		if (!(old->hw.qh.head & UHCI_PTR_TERM))
			old->hw.qh.head = MAKE_QH_ADDR (new) ;
	}
	else {
		// (POS) (OLD) -> (POS) (NEW) (OLD)
		old = list_entry (pos->horizontal.next, uhci_desc_t, horizontal);
		list_add (&new->horizontal, &pos->horizontal);
		new->hw.qh.head = MAKE_QH_ADDR (old);
		pos->hw.qh.head = MAKE_QH_ADDR (new) ;
	}

	mb ();
	
	spin_unlock_irqrestore (&s->qh_lock, flags);
	
	return 0;
}
/*-------------------------------------------------------------------*/
_static int unlink_qh (uhci_t *s, uhci_desc_t *element)
{
	uhci_desc_t  *prev;
	unsigned long flags;

	spin_lock_irqsave (&s->qh_lock, flags);
	
	prev = list_entry (element->horizontal.prev, uhci_desc_t, horizontal);
	prev->hw.qh.head = element->hw.qh.head;

	list_del(&element->horizontal);

	mb ();
	spin_unlock_irqrestore (&s->qh_lock, flags);
	
	return 0;
}
/*-------------------------------------------------------------------*/
_static int delete_qh (uhci_t *s, uhci_desc_t *qh)
{
	uhci_desc_t *td;
	struct list_head *p;
	
	list_del (&qh->horizontal);

	while ((p = qh->vertical.next) != &qh->vertical) {
		td = list_entry (p, uhci_desc_t, vertical);
		dbg("unlink td @ %p",td);
		unlink_td (s, td, 0); // no physical unlink
		delete_desc (td);
	}

	delete_desc (qh);
	
	return 0;
}
/*-------------------------------------------------------------------*/
_static void clean_td_chain (uhci_desc_t *td)
{
	struct list_head *p;
	uhci_desc_t *td1;

	if (!td)
		return;
	
	while ((p = td->horizontal.next) != &td->horizontal) {
		td1 = list_entry (p, uhci_desc_t, horizontal);
		delete_desc (td1);
	}
	
	delete_desc (td);
}
/*-------------------------------------------------------------------*/
// Removes ALL qhs in chain (paranoia!)
_static void cleanup_skel (uhci_t *s)
{
	unsigned int n;
	uhci_desc_t *td;

	dbg("cleanup_skel");

	clean_descs(s,1);

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
		uhci_desc_t *qh, *qh1;

		qh = s->control_chain;
		while ((p = qh->horizontal.next) != &qh->horizontal) {
			qh1 = list_entry (p, uhci_desc_t, horizontal);
			dbg("delete_qh @ %p",qh1);
			delete_qh (s, qh1);
		}
			dbg("delete_qh last @ %p",qh);
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
	dbg("cleanup_skel finished");	
}
/*-------------------------------------------------------------------*/
// allocates framelist and qh-skeletons
// only HW-links provide continous linking, SW-links stay in their domain (ISO/INT)
_static int init_skel (uhci_t *s)
{
	int n, ret;
	uhci_desc_t *qh, *td;
	
	dbg("init_skel");
	
	s->framelist = (__u32 *) get_free_page (GFP_KERNEL);

	if (!s->framelist)
		return -ENOMEM;

	memset (s->framelist, 0, 4096);

	dbg("allocating iso desc pointer list");
	s->iso_td = (uhci_desc_t **) kmalloc (1024 * sizeof (uhci_desc_t*), GFP_KERNEL);
	
	if (!s->iso_td)
		goto init_skel_cleanup;

	s->control_chain = NULL;
	s->bulk_chain = NULL;
	s->chain_end = NULL;

	dbg("allocating iso descs");
	for (n = 0; n < 1024; n++) {
	 	// allocate skeleton iso/irq-tds
		ret = alloc_td (&td, 0);
		if (ret)
			goto init_skel_cleanup;
		s->iso_td[n] = td;
		s->framelist[n] = ((__u32) virt_to_bus (td));
	}

	dbg("allocating qh: chain_end");
	ret = alloc_qh (&qh);
	
	if (ret)
		goto init_skel_cleanup;
	
	s->chain_end = qh;

	dbg("allocating qh: bulk_chain");
	ret = alloc_qh (&qh);
	
	if (ret)
		goto init_skel_cleanup;
	
	insert_qh (s, s->chain_end, qh, 0);
	s->bulk_chain = qh;
	dbg("allocating qh: control_chain");
	ret = alloc_qh (&qh);
	
	if (ret)
		goto init_skel_cleanup;
	
	insert_qh (s, s->bulk_chain, qh, 0);
	s->control_chain = qh;

	for (n = 0; n < 8; n++)
		s->int_chain[n] = 0;

	dbg("allocating skeleton INT-TDs");
	
	for (n = 0; n < 8; n++) {
		uhci_desc_t *td;

		alloc_td (&td, 0);
		if (!td)
			goto init_skel_cleanup;
		s->int_chain[n] = td;
		if (n == 0) {
			s->int_chain[0]->hw.td.link = virt_to_bus (s->control_chain) | UHCI_PTR_QH;
		}
		else {
			s->int_chain[n]->hw.td.link = virt_to_bus (s->int_chain[0]);
		}
	}

	dbg("Linking skeleton INT-TDs");
	
	for (n = 0; n < 1024; n++) {
		// link all iso-tds to the interrupt chains
		int m, o;
		dbg("framelist[%i]=%x",n,s->framelist[n]);
		if ((n&127)==127) 
			((uhci_desc_t*) s->iso_td[n])->hw.td.link = virt_to_bus(s->int_chain[0]);
		else {
			for (o = 1, m = 2; m <= 128; o++, m += m) {
				// n&(m-1) = n%m
				if ((n & (m - 1)) == ((m - 1) / 2)) {
					((uhci_desc_t*) s->iso_td[n])->hw.td.link = virt_to_bus (s->int_chain[o]);
				}
			}
		}
	}

	mb();
	//uhci_show_queue(s->control_chain);   
	dbg("init_skel exit");
	return 0;		// OK

      init_skel_cleanup:
	cleanup_skel (s);
	return -ENOMEM;
}

/*-------------------------------------------------------------------*/
_static void fill_td (uhci_desc_t *td, int status, int info, __u32 buffer)
{
	td->hw.td.status = status;
	td->hw.td.info = info;
	td->hw.td.buffer = buffer;
}

/*-------------------------------------------------------------------*/
//                         LOW LEVEL STUFF
//          assembles QHs und TDs for control, bulk and iso
/*-------------------------------------------------------------------*/
_static int uhci_submit_control_urb (urb_t *urb)
{
	uhci_desc_t *qh, *td;
	uhci_t *s = (uhci_t*) urb->dev->bus->hcpriv;
	urb_priv_t *urb_priv = urb->hcpriv;
	unsigned long destination, status;
	int maxsze = usb_maxpacket (urb->dev, urb->pipe, usb_pipeout (urb->pipe));
	unsigned long len, bytesrequested;
	char *data;
	int depth_first=USE_CTRL_DEPTH_FIRST;  // UHCI descriptor chasing method

	dbg("uhci_submit_control start");
	alloc_qh (&qh);		// alloc qh for this request

	if (!qh)
		return -ENOMEM;

	alloc_td (&td, UHCI_PTR_DEPTH * depth_first);		// get td for setup stage

	if (!td) {
		delete_qh (s, qh);
		return -ENOMEM;
	}

	/* The "pipe" thing contains the destination in bits 8--18 */
	destination = (urb->pipe & PIPE_DEVEP_MASK) | USB_PID_SETUP;

	/* 3 errors */
	status = (urb->pipe & TD_CTRL_LS) | TD_CTRL_ACTIVE |
		(urb->transfer_flags & USB_DISABLE_SPD ? 0 : TD_CTRL_SPD) | (3 << 27);

	/*  Build the TD for the control request, try forever, 8 bytes of data */
	fill_td (td, status, destination | (7 << 21), virt_to_bus (urb->setup_packet));

	insert_td (s, qh, td, 0);	// queue 'setup stage'-td in qh
#if 0
	dbg("SETUP to pipe %x: %x %x %x %x %x %x %x %x", urb->pipe,
		urb->setup_packet[0], urb->setup_packet[1], urb->setup_packet[2], urb->setup_packet[3],
		urb->setup_packet[4], urb->setup_packet[5], urb->setup_packet[6], urb->setup_packet[7]);
	//uhci_show_td(td);
#endif

	/*  Build the DATA TD's */
	len = urb->transfer_buffer_length;
	bytesrequested = len;
	data = urb->transfer_buffer;

	/* If direction is "send", change the frame from SETUP (0x2D)
	   to OUT (0xE1). Else change it from SETUP to IN (0x69). */

	destination &= ~UHCI_PID;
	
	if (usb_pipeout (urb->pipe))
		destination |= USB_PID_OUT;
	else
		destination |= USB_PID_IN;	

	while (len > 0) {
		int pktsze = len;

		alloc_td (&td, UHCI_PTR_DEPTH * depth_first);
		if (!td) {
			delete_qh (s, qh);
			return -ENOMEM;
		}

		if (pktsze > maxsze)
			pktsze = maxsze;

		destination ^= 1 << TD_TOKEN_TOGGLE;	// toggle DATA0/1

		fill_td (td, status, destination | ((pktsze - 1) << 21),
			 virt_to_bus (data));	// Status, pktsze bytes of data

		insert_td (s, qh, td, UHCI_PTR_DEPTH * depth_first);	// queue 'data stage'-td in qh

		data += pktsze;
		len -= pktsze;
	}

	/*  Build the final TD for control status */
	/* It's only IN if the pipe is out AND we aren't expecting data */

	destination &= ~UHCI_PID;

	if (usb_pipeout (urb->pipe) || (bytesrequested == 0))
		destination |= USB_PID_IN;
	else
		destination |= USB_PID_OUT;

	destination |= 1 << TD_TOKEN_TOGGLE;	/* End in Data1 */

	alloc_td (&td, UHCI_PTR_DEPTH);
	
	if (!td) {
		delete_qh (s, qh);
		return -ENOMEM;
	}
	status &=~TD_CTRL_SPD;

	/* no limit on errors on final packet , 0 bytes of data */
	fill_td (td, status | TD_CTRL_IOC, destination | (UHCI_NULL_DATA_SIZE << 21),
		 0);

	insert_td (s, qh, td, UHCI_PTR_DEPTH * depth_first);	// queue status td

	list_add (&qh->desc_list, &urb_priv->desc_list);

	urb->status = -EINPROGRESS;
	queue_urb (s, urb);	// queue before inserting in desc chain

	qh->hw.qh.element&=~UHCI_PTR_TERM;

	//uhci_show_queue(qh);
	/* Start it up... put low speed first */
	if (urb->pipe & TD_CTRL_LS)
		insert_qh (s, s->control_chain, qh, 1);	// insert after control chain
	else
		insert_qh (s, s->bulk_chain, qh, 0);	// insert before bulk chain
	//uhci_show_queue(qh);

	dbg("uhci_submit_control end");
	return 0;
}
/*-------------------------------------------------------------------*/
_static int uhci_submit_bulk_urb (urb_t *urb)
{
	uhci_t *s = (uhci_t*) urb->dev->bus->hcpriv;
	urb_priv_t *urb_priv = urb->hcpriv;
	uhci_desc_t *qh, *td;
	unsigned long destination, status;
	char *data;
	unsigned int pipe = urb->pipe;
	int maxsze = usb_maxpacket (urb->dev, pipe, usb_pipeout (pipe));
	int info, len;
	int depth_first=USE_BULK_DEPTH_FIRST;  // UHCI descriptor chasing method


	if (usb_endpoint_halted (urb->dev, usb_pipeendpoint (pipe), usb_pipeout (pipe)))
		return -EPIPE;

	if (urb->transfer_buffer_length < 0) {
		err("Negative transfer length in submit_bulk");
		return -EINVAL;
	}
	
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
		((urb->transfer_flags & USB_DISABLE_SPD) ? 0 : TD_CTRL_SPD) | (3 << 27);

	/* Build the TDs for the bulk request */
	len = urb->transfer_buffer_length;
	data = urb->transfer_buffer;
	dbg("uhci_submit_bulk_urb: pipe %x, len %d", pipe, len);
	
	do {					// TBD: Really allow zero-length packets?
		int pktsze = len;

		alloc_td (&td, UHCI_PTR_DEPTH * depth_first);

		if (!td) {
			delete_qh (s, qh);
			return -ENOMEM;
		}

		if (pktsze > maxsze)
			pktsze = maxsze;

		// pktsze bytes of data 
		info = destination | (((pktsze - 1)&UHCI_NULL_DATA_SIZE) << 21) |
			(usb_gettoggle (urb->dev, usb_pipeendpoint (pipe), usb_pipeout (pipe)) << TD_TOKEN_TOGGLE);

		fill_td (td, status, info, virt_to_bus (data));

		data += pktsze;
		len -= pktsze;

		if (!len)
			td->hw.td.status |= TD_CTRL_IOC;	// last one generates INT
		//dbg("insert td %p, len %i",td,pktsze);

		insert_td (s, qh, td, UHCI_PTR_DEPTH * depth_first);
		
		/* Alternate Data0/1 (start with Data0) */
		usb_dotoggle (urb->dev, usb_pipeendpoint (pipe), usb_pipeout (pipe));
	} while (len > 0);

	list_add (&qh->desc_list, &urb_priv->desc_list);

	urb->status = -EINPROGRESS;
	queue_urb (s, urb);
	
	qh->hw.qh.element&=~UHCI_PTR_TERM;

	insert_qh (s, s->chain_end, qh, 0);	// insert before end marker
	//uhci_show_queue(s->bulk_chain);

	dbg("uhci_submit_bulk_urb: exit");
	return 0;
}

/*-------------------------------------------------------------------*/
// unlinks an urb by dequeuing its qh, waits some frames and forgets it
// Problem: unlinking in interrupt requires waiting for one frame (udelay)
// to allow the whole structures to be safely removed
_static int uhci_unlink_urb (urb_t *urb)
{
	uhci_t *s;
	uhci_desc_t *qh;
	uhci_desc_t *td;
	urb_priv_t *urb_priv;
	unsigned long flags=0;

	struct list_head *p;

	if (!urb || !urb->dev)		// you never know...
		return -EINVAL;

	s = (uhci_t*) urb->dev->bus->hcpriv;	// get pointer to uhci struct

	if (usb_pipedevice (urb->pipe) == s->rh.devnum)
		return rh_unlink_urb (urb);

	if (!urb->hcpriv)		// you never know...
		return -EINVAL;

	//dbg("unlink_urb called %p",urb);

	spin_lock_irqsave (&s->urb_list_lock, flags);

	if (urb->status == -EINPROGRESS) {
		// URB probably still in work
		dequeue_urb (s, urb);
		s->unlink_urb_done=1;
		spin_unlock_irqrestore (&s->urb_list_lock, flags);		
		
		urb->status = -ENOENT;	// mark urb as killed		
		urb_priv = urb->hcpriv;

		switch (usb_pipetype (urb->pipe)) {
		case PIPE_ISOCHRONOUS:
		case PIPE_INTERRUPT:
			for (p = urb_priv->desc_list.next; p != &urb_priv->desc_list; p = p->next) {
				td = list_entry (p, uhci_desc_t, desc_list);
				unlink_td (s, td, 1);
			}
			// wait at least 1 Frame
			uhci_wait_ms(1);	
			while ((p = urb_priv->desc_list.next) != &urb_priv->desc_list) {
				td = list_entry (p, uhci_desc_t, desc_list);
				list_del (p);
				delete_desc (td);
			}
			break;

		case PIPE_BULK:
		case PIPE_CONTROL:
			qh = list_entry (urb_priv->desc_list.next, uhci_desc_t, desc_list);

			unlink_qh (s, qh);	// remove this qh from qh-list
			qh->last_used=UHCI_GET_CURRENT_FRAME(s);
			list_add_tail (&qh->horizontal, &s->free_desc); // mark for later deletion
			// wait at least 1 Frame
			uhci_wait_ms(1);
		}
		
#ifdef DEBUG_SLAB
		kmem_cache_free(urb_priv_kmem, urb->hcpriv);
#else
		kfree (urb->hcpriv);
#endif
		if (urb->complete) {
			dbg("unlink_urb: calling completion");
			urb->complete ((struct urb *) urb);
		}
		usb_dec_dev_use (urb->dev);
		return 0;
	}
	else
		spin_unlock_irqrestore (&s->urb_list_lock, flags);
	return 0;
}
/*-------------------------------------------------------------------*/
// In case of ASAP iso transfer, search the URB-list for already queued URBs
// for this EP and calculate the earliest start frame for the new
// URB (easy seamless URB continuation!)
_static int find_iso_limits (urb_t *urb, unsigned int *start, unsigned int *end)
{
	urb_t *u, *last_urb = NULL;
	uhci_t *s = (uhci_t*) urb->dev->bus->hcpriv;
	struct list_head *p;
	int ret=-1;
	unsigned long flags;
	
	spin_lock_irqsave (&s->urb_list_lock, flags);
	p=s->urb_list.next;

	for (; p != &s->urb_list; p = p->next) {
		u = list_entry (p, urb_t, urb_list);
		// look for pending URBs with identical pipe handle
		// works only because iso doesn't toggle the data bit!
		if ((urb->pipe == u->pipe) && (urb->dev == u->dev) && (u->status == -EINPROGRESS)) {
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

_static int iso_find_start (urb_t *urb)
{
	uhci_t *s = (uhci_t*) urb->dev->bus->hcpriv;
	unsigned int now;
	unsigned int start_limit = 0, stop_limit = 0, queued_size;
	int limits;

	now = UHCI_GET_CURRENT_FRAME (s) & 1023;

	if ((unsigned) urb->number_of_packets > 900)
		return -EFBIG;
	
	limits = find_iso_limits (urb, &start_limit, &stop_limit);
	queued_size = (stop_limit - start_limit) & 1023;

	if (urb->transfer_flags & USB_ISO_ASAP) {
		// first iso
		if (limits) {
			// 10ms setup should be enough //FIXME!
			urb->start_frame = (now + 10) & 1023;
		}
		else {
			urb->start_frame = stop_limit;		//seamless linkage

			if (((now - urb->start_frame) & 1023) <= (unsigned) urb->number_of_packets) {
				info("iso_find_start: gap in seamless isochronous scheduling");
				dbg("iso_find_start: now %u start_frame %u number_of_packets %u pipe 0x%08x",
					now, urb->start_frame, urb->number_of_packets, urb->pipe);
// The following code is only for debugging purposes...
#if 0
				{
					uhci_t *s = (uhci_t*) urb->dev->bus->hcpriv;
					struct list_head *p;
					urb_t *u;
					int a = -1, b = -1;
					unsigned long flags;

					spin_lock_irqsave (&s->urb_list_lock, flags);
					p=s->urb_list.next;

					for (; p != &s->urb_list; p = p->next) {
						u = list_entry (p, urb_t, urb_list);
						if (urb->dev != u->dev)
							continue;
						dbg("urb: pipe 0x%08x status %d start_frame %u number_of_packets %u",
							u->pipe, u->status, u->start_frame, u->number_of_packets);
						if (!usb_pipeisoc (u->pipe))
							continue;
						if (a == -1)
							a = u->start_frame;
						b = (u->start_frame + u->number_of_packets - 1) & 1023;
					}
					spin_unlock_irqrestore(&s->urb_list_lock, flags);
				}
#endif
				urb->start_frame = (now + 5) & 1023;	// 5ms setup should be enough //FIXME!
				//return -EAGAIN; //FIXME
			}
		}
	}
	else {
		urb->start_frame &= 1023;
		if (((now - urb->start_frame) & 1023) < (unsigned) urb->number_of_packets) {
			dbg("iso_find_start: now between start_frame and end");
			return -EAGAIN;
		}
	}

	/* check if either start_frame or start_frame+number_of_packets-1 lies between start_limit and stop_limit */
	if (limits)
		return 0;

	if (((urb->start_frame - start_limit) & 1023) < queued_size ||
	    ((urb->start_frame + urb->number_of_packets - 1 - start_limit) & 1023) < queued_size) {
		dbg("iso_find_start: start_frame %u number_of_packets %u start_limit %u stop_limit %u",
			urb->start_frame, urb->number_of_packets, start_limit, stop_limit);
		return -EAGAIN;
	}

	return 0;
}
/*-------------------------------------------------------------------*/
// submits USB interrupt (ie. polling ;-) 
// ASAP-flag set implicitely
// if period==0, the the transfer is only done once (usb_scsi need this...)

_static int uhci_submit_int_urb (urb_t *urb)
{
	uhci_t *s = (uhci_t*) urb->dev->bus->hcpriv;
	urb_priv_t *urb_priv = urb->hcpriv;
	int nint, n, ret;
	uhci_desc_t *td;
	int status, destination;
	int now;
	int info;
	unsigned int pipe = urb->pipe;

	//dbg("SUBMIT INT");

	if (urb->interval < 0 || urb->interval >= 256)
		return -EINVAL;

	if (urb->interval == 0)
		nint = 0;
	else {
		for (nint = 0, n = 1; nint <= 8; nint++, n += n)	// round interval down to 2^n
		 {
			if (urb->interval < n) {
				urb->interval = n / 2;
				break;
			}
		}
		nint--;
	}

	dbg("Rounded interval to %i, chain  %i", urb->interval, nint);

	now = UHCI_GET_CURRENT_FRAME (s) & 1023;
	urb->start_frame = now;	// remember start frame, just in case...

	urb->number_of_packets = 1;

	// INT allows only one packet
	if (urb->transfer_buffer_length > usb_maxpacket (urb->dev, pipe, usb_pipeout (pipe)))
		return -EINVAL;

	ret = alloc_td (&td, UHCI_PTR_DEPTH);

	if (ret)
		return -ENOMEM;

	status = (pipe & TD_CTRL_LS) | TD_CTRL_ACTIVE | TD_CTRL_IOC |
		(urb->transfer_flags & USB_DISABLE_SPD ? 0 : TD_CTRL_SPD) | (3 << 27);

	destination = (urb->pipe & PIPE_DEVEP_MASK) | usb_packetid (urb->pipe) |
		(((urb->transfer_buffer_length - 1) & 0x7ff) << 21);


	info = destination | (usb_gettoggle (urb->dev, usb_pipeendpoint (pipe), usb_pipeout (pipe)) << TD_TOKEN_TOGGLE);

	fill_td (td, status, info, virt_to_bus (urb->transfer_buffer));
	list_add_tail (&td->desc_list, &urb_priv->desc_list);

	urb->status = -EINPROGRESS;
	queue_urb (s, urb);

	insert_td_horizontal (s, s->int_chain[nint], td);	// store in INT-TDs

	usb_dotoggle (urb->dev, usb_pipeendpoint (pipe), usb_pipeout (pipe));

#if 0
	td = tdm[urb->number_of_packets];
	fill_td (td, TD_CTRL_IOC, 0, 0);
	insert_td_horizontal (s, s->iso_td[(urb->start_frame + (urb->number_of_packets) * urb->interval + 1) & 1023], td);
	list_add_tail (&td->desc_list, &urb_priv->desc_list);
#endif

	return 0;
}
/*-------------------------------------------------------------------*/
_static int uhci_submit_iso_urb (urb_t *urb)
{
	uhci_t *s = (uhci_t*) urb->dev->bus->hcpriv;
	urb_priv_t *urb_priv = urb->hcpriv;
	int pipe=urb->pipe;
	int maxsze = usb_maxpacket (urb->dev, pipe, usb_pipeout (pipe));
	int n, ret, last=0;
	uhci_desc_t *td, **tdm;
	int status, destination;
	unsigned long flags;

	__save_flags(flags);
	__cli();		      // Disable IRQs to schedule all ISO-TDs in time
	ret = iso_find_start (urb);	// adjusts urb->start_frame for later use

	if (ret)
		goto err;

	tdm = (uhci_desc_t **) kmalloc (urb->number_of_packets * sizeof (uhci_desc_t*), in_interrupt ()? GFP_ATOMIC : GFP_KERNEL);

	if (!tdm) {
		ret = -ENOMEM;
		goto err;
	}

	// First try to get all TDs
	for (n = 0; n < urb->number_of_packets; n++) {
		dbg("n:%d urb->iso_frame_desc[n].length:%d", n, urb->iso_frame_desc[n].length);
		if (!urb->iso_frame_desc[n].length) {
			// allows ISO striping by setting length to zero in iso_descriptor
			tdm[n] = 0;
			continue;
		}
		#ifdef ISO_SANITY_CHECK
		if(urb->iso_frame_desc[n].length > maxsze) {
			err("submit_iso: urb->iso_frame_desc[%d].length(%d)>%d",n , urb->iso_frame_desc[n].length, maxsze);
			tdm[n] = 0;
			ret=-EINVAL;
			goto inval;
		}
		#endif
		ret = alloc_td (&td, UHCI_PTR_DEPTH);
	inval:
		if (ret) {
			int i;	// Cleanup allocated TDs

			for (i = 0; i < n; n++)
				if (tdm[i])
					kfree (tdm[i]);
			kfree (tdm);
			goto err;
		}
		last=n;
		tdm[n] = td;
	}

	status = TD_CTRL_ACTIVE | TD_CTRL_IOS;	//| (urb->transfer_flags&USB_DISABLE_SPD?0:TD_CTRL_SPD);

	destination = (urb->pipe & PIPE_DEVEP_MASK) | usb_packetid (urb->pipe);

	
	// Queue all allocated TDs
	for (n = 0; n < urb->number_of_packets; n++) {
		td = tdm[n];
		if (!td)
			continue;
			
		if (n  == last)
			status |= TD_CTRL_IOC;

		fill_td (td, status, destination | (((urb->iso_frame_desc[n].length - 1) & 0x7ff) << 21),
			 virt_to_bus (urb->transfer_buffer + urb->iso_frame_desc[n].offset));
		list_add_tail (&td->desc_list, &urb_priv->desc_list);
	
		if (n == last) {
			urb->status = -EINPROGRESS;
			queue_urb (s, urb);
		}
		insert_td_horizontal (s, s->iso_td[(urb->start_frame + n) & 1023], td);	// store in iso-tds
		//uhci_show_td(td);

	}

	kfree (tdm);
	dbg("ISO-INT# %i, start %i, now %i", urb->number_of_packets, urb->start_frame, UHCI_GET_CURRENT_FRAME (s) & 1023);
	ret = 0;

      err:
	__restore_flags(flags);
	return ret;

}
/*-------------------------------------------------------------------*/
_static int search_dev_ep (uhci_t *s, urb_t *urb)
{
	unsigned long flags;
	struct list_head *p;
	urb_t *tmp;
	unsigned int mask = usb_pipecontrol(urb->pipe) ? (~USB_DIR_IN) : (~0);

	dbg("search_dev_ep:");
	spin_lock_irqsave (&s->urb_list_lock, flags);
	p=s->urb_list.next;

	for (; p != &s->urb_list; p = p->next) {
		tmp = list_entry (p, urb_t, urb_list);
		dbg("urb: %p", tmp);
		// we can accept this urb if it is not queued at this time 
		// or if non-iso transfer requests should be scheduled for the same device and pipe
		if ((!usb_pipeisoc(urb->pipe) && tmp->dev == urb->dev && !((tmp->pipe ^ urb->pipe) & mask)) ||
		    (urb == tmp)) {
			spin_unlock_irqrestore (&s->urb_list_lock, flags);
			return 1;	// found another urb already queued for processing
		}
	}
	spin_unlock_irqrestore (&s->urb_list_lock, flags);
	return 0;
}
/*-------------------------------------------------------------------*/
_static int uhci_submit_urb (urb_t *urb)
{
	uhci_t *s;
	urb_priv_t *urb_priv;
	int ret = 0;

	if (!urb->dev || !urb->dev->bus)
		return -ENODEV;

	s = (uhci_t*) urb->dev->bus->hcpriv;
	//dbg("submit_urb: %p type %d",urb,usb_pipetype(urb->pipe));

	if (usb_pipedevice (urb->pipe) == s->rh.devnum)
		return rh_submit_urb (urb);	/* virtual root hub */

	usb_inc_dev_use (urb->dev);

	if (search_dev_ep (s, urb)) {
		usb_dec_dev_use (urb->dev);
		return -ENXIO;	// urb already queued

	}

#ifdef DEBUG_SLAB
	urb_priv = kmem_cache_alloc(urb_priv_kmem, in_interrupt ()? SLAB_ATOMIC : SLAB_KERNEL);
#else
	urb_priv = kmalloc (sizeof (urb_priv_t), in_interrupt ()? GFP_ATOMIC : GFP_KERNEL);
#endif
	if (!urb_priv) {
		usb_dec_dev_use (urb->dev);
		return -ENOMEM;
	}

	urb->hcpriv = urb_priv;
	INIT_LIST_HEAD (&urb_priv->desc_list);
	urb_priv->short_control_packet=0;
	dbg("submit_urb: scheduling %p", urb);

	switch (usb_pipetype (urb->pipe)) {
	case PIPE_ISOCHRONOUS:
		ret = uhci_submit_iso_urb (urb);
		break;
	case PIPE_INTERRUPT:
		ret = uhci_submit_int_urb (urb);
		break;
	case PIPE_CONTROL:
		//dump_urb (urb);
		ret = uhci_submit_control_urb (urb);
		break;
	case PIPE_BULK:
		ret = uhci_submit_bulk_urb (urb);
		break;
	default:
		ret = -EINVAL;
	}

	dbg("submit_urb: scheduled with ret: %d", ret);
	
	
	if (ret != 0) {
		usb_dec_dev_use (urb->dev);
#ifdef DEBUG_SLAB
		kmem_cache_free(urb_priv_kmem, urb_priv);
#else
		kfree (urb_priv);
#endif
		return ret;
	}

	return 0;
}
#ifdef USE_RECLAMATION_LOOP
// Removes bandwidth reclamation if URB idles too long
void check_idling_urbs(uhci_t *s)
{
	struct list_head *p,*p2;
	urb_t *urb;
	int type;	

	//dbg("check_idling_urbs: enter i:%d",in_interrupt());
	
	spin_lock (&s->urb_list_lock);
	p = s->urb_list.prev;	

	while (p != &s->urb_list) {
		p2 = p;
		p = p->prev;
		urb=list_entry (p2, urb_t, urb_list);
		type=usb_pipetype (urb->pipe);

#if 0
		err("URB timers: %li now: %li %i\n",
			((urb_priv_t*)urb->hcpriv)->started +IDLE_TIMEOUT, jiffies,
			type);
#endif			 
		if (((type == PIPE_BULK) || (type == PIPE_CONTROL)) &&  
		    (((urb_priv_t*)urb->hcpriv)->use_loop) &&
		    ((((urb_priv_t*)urb->hcpriv)->started +IDLE_TIMEOUT) < jiffies))
			disable_desc_loop(s,urb);

	}
	spin_unlock (&s->urb_list_lock);
	
	//dbg("check_idling_urbs: finished");
}
#endif
/*-------------------------------------------------------------------
 Virtual Root Hub
 -------------------------------------------------------------------*/

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
_static int rh_send_irq (urb_t *urb)
{
	int len = 1;
	int i;
	uhci_t *uhci = urb->dev->bus->hcpriv;
	unsigned int io_addr = uhci->io_addr;
	__u16 data = 0;

	for (i = 0; i < uhci->rh.numports; i++) {
		data |= ((inw (io_addr + USBPORTSC1 + i * 2) & 0xa) > 0 ? (1 << (i + 1)) : 0);
		len = (i + 1) / 8 + 1;
	}

	*(__u16 *) urb->transfer_buffer = cpu_to_le16 (data);
	urb->actual_length = len;
	urb->status = 0;
	
	if ((data > 0) && (uhci->rh.send != 0)) {
		dbg("Root-Hub INT complete: port1: %x port2: %x data: %x",
		     inw (io_addr + USBPORTSC1), inw (io_addr + USBPORTSC2), data);
		urb->complete (urb);

	}
	return 0;
}

/*-------------------------------------------------------------------------*/
/* Virtual Root Hub INTs are polled by this timer every "intervall" ms */
_static int rh_init_int_timer (urb_t *urb);

_static void rh_int_timer_do (unsigned long ptr)
{
	int len;
	urb_t *urb = (urb_t*) ptr;
	uhci_t *uhci = urb->dev->bus->hcpriv;

#ifdef USE_RECLAMATION_LOOP
	check_idling_urbs(uhci);
#endif

	if (uhci->rh.send) {
		len = rh_send_irq (urb);
		if (len > 0) {
			urb->actual_length = len;
			if (urb->complete)
				urb->complete (urb);
		}
	}
	rh_init_int_timer (urb);
}

/*-------------------------------------------------------------------------*/
/* Root Hub INTs are polled by this timer */
_static int rh_init_int_timer (urb_t *urb)
{
	uhci_t *uhci = urb->dev->bus->hcpriv;

	uhci->rh.interval = urb->interval;
	init_timer (&uhci->rh.rh_int_timer);
	uhci->rh.rh_int_timer.function = rh_int_timer_do;
	uhci->rh.rh_int_timer.data = (unsigned long) urb;
	uhci->rh.rh_int_timer.expires = jiffies + (HZ * (urb->interval < 30 ? 30 : urb->interval)) / 1000;
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


_static int rh_submit_urb (urb_t *urb)
{
	struct usb_device *usb_dev = urb->dev;
	uhci_t *uhci = usb_dev->bus->hcpriv;
	unsigned int pipe = urb->pipe;
	devrequest *cmd = (devrequest *) urb->setup_packet;
	void *data = urb->transfer_buffer;
	int leni = urb->transfer_buffer_length;
	int len = 0;
	int status = 0;
	int stat = 0;
	int i;
	unsigned int io_addr = uhci->io_addr;
	__u16 cstatus;

	__u16 bmRType_bReq;
	__u16 wValue;
	__u16 wIndex;
	__u16 wLength;

	if (usb_pipetype (pipe) == PIPE_INTERRUPT) {
		dbg("Root-Hub submit IRQ: every %d ms", urb->interval);
		uhci->rh.urb = urb;
		uhci->rh.send = 1;
		uhci->rh.interval = urb->interval;
		rh_init_int_timer (urb);

		return 0;
	}


	bmRType_bReq = cmd->requesttype | cmd->request << 8;
	wValue = le16_to_cpu (cmd->value);
	wIndex = le16_to_cpu (cmd->index);
	wLength = le16_to_cpu (cmd->length);

	for (i = 0; i < 8; i++)
		uhci->rh.c_p_r[i] = 0;

	dbg("Root-Hub: adr: %2x cmd(%1x): %04x %04x %04x %04x",
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
			uhci_wait_ms (10);
			uhci->rh.c_p_r[wIndex - 1] = 1;
			CLR_RH_PORTSTAT (USBPORTSC_PR);
			udelay (10);
			SET_RH_PORTSTAT (USBPORTSC_PE);
			uhci_wait_ms (10);
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


	dbg("Root-Hub stat port1: %x port2: %x",
	     inw (io_addr + USBPORTSC1), inw (io_addr + USBPORTSC2));

	urb->actual_length = len;
	urb->status = stat;
	if (urb->complete)
		urb->complete (urb);
	return 0;
}
/*-------------------------------------------------------------------------*/

_static int rh_unlink_urb (urb_t *urb)
{
	uhci_t *uhci = urb->dev->bus->hcpriv;

	if (uhci->rh.urb==urb) {
		dbg("Root-Hub unlink IRQ");
		uhci->rh.send = 0;
		del_timer (&uhci->rh.rh_int_timer);
	}
	return 0;
}
/*-------------------------------------------------------------------*/

/*
 * Map status to standard result codes
 *
 * <status> is (td->status & 0xFE0000) [a.k.a. uhci_status_bits(td->status)
 * <dir_out> is True for output TDs and False for input TDs.
 */
_static int uhci_map_status (int status, int dir_out)
{
	if (!status)
		return 0;
	if (status & TD_CTRL_BITSTUFF)	/* Bitstuff error */
		return -EPROTO;
	if (status & TD_CTRL_CRCTIMEO) {	/* CRC/Timeout */
		if (dir_out)
			return -ETIMEDOUT;
		else
			return -EILSEQ;
	}
	if (status & TD_CTRL_NAK)	/* NAK */
		return -ETIMEDOUT;
	if (status & TD_CTRL_BABBLE)	/* Babble */
		return -EPIPE;
	if (status & TD_CTRL_DBUFERR)	/* Buffer error */
		return -ENOSR;
	if (status & TD_CTRL_STALLED)	/* Stalled */
		return -EPIPE;
	if (status & TD_CTRL_ACTIVE)	/* Active */
		return 0;

	return -EPROTO;
}

/*
 * Only the USB core should call uhci_alloc_dev and uhci_free_dev
 */
_static int uhci_alloc_dev (struct usb_device *usb_dev)
{
	return 0;
}

_static void uhci_unlink_urbs(uhci_t *s, struct usb_device *usb_dev, int remove_all)
{
	unsigned long flags;
	struct list_head *p;
	struct list_head *p2;
	urb_t *urb;

	spin_lock_irqsave (&s->urb_list_lock, flags);
	p = s->urb_list.prev;	
	while (p != &s->urb_list) {
		p2 = p;
		p = p->prev;
		urb = list_entry (p2, urb_t, urb_list);
		dbg("urb: %p", urb);
		if (remove_all || (usb_dev == urb->dev)) {
			warn("forced removing of queued URB %p due to disconnect",urb);
			uhci_unlink_urb(urb);
			urb->dev = NULL; // avoid further processing of this URB
		}
	}
	spin_unlock_irqrestore (&s->urb_list_lock, flags);
}

_static int uhci_free_dev (struct usb_device *usb_dev)
{
	uhci_t *s;
	
	dbg("uhci_free_dev");	

	if(!usb_dev || !usb_dev->bus || !usb_dev->bus->hcpriv)
		return -EINVAL;
	
	s=(uhci_t*) usb_dev->bus->hcpriv;
	
	uhci_unlink_urbs(s, usb_dev, 0);

	return 0;
}

/*
 * uhci_get_current_frame_number()
 *
 * returns the current frame number for a USB bus/controller.
 */
_static int uhci_get_current_frame_number (struct usb_device *usb_dev)
{
	return UHCI_GET_CURRENT_FRAME ((uhci_t*) usb_dev->bus->hcpriv);
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

_static int process_transfer (uhci_t *s, urb_t *urb)
{
	int ret = 0;
	urb_priv_t *urb_priv = urb->hcpriv;
	struct list_head *qhl = urb_priv->desc_list.next;
	uhci_desc_t *qh = list_entry (qhl, uhci_desc_t, desc_list);
	struct list_head *p = qh->vertical.next;
	uhci_desc_t *desc= list_entry (urb_priv->desc_list.prev, uhci_desc_t, desc_list);
	uhci_desc_t *last_desc = list_entry (desc->vertical.prev, uhci_desc_t, vertical);
	int data_toggle = usb_gettoggle (urb->dev, usb_pipeendpoint (urb->pipe), usb_pipeout (urb->pipe));	// save initial data_toggle


	// extracted and remapped info from TD
	int maxlength;
	int actual_length;
	int status = 0;

	dbg("process_transfer: urb contains bulk/control request");


	/* if the status phase has been retriggered and the
	   queue is empty or the last status-TD is inactive, the retriggered
	   status stage is completed
	 */
#if 1
	if (urb_priv->short_control_packet && 
		((qh->hw.qh.element == UHCI_PTR_TERM) ||(!(last_desc->hw.td.status & TD_CTRL_ACTIVE)))) 
		goto transfer_finished;
#endif
	urb->actual_length=0;

	for (; p != &qh->vertical; p = p->next) {
		desc = list_entry (p, uhci_desc_t, vertical);

		if (desc->hw.td.status & TD_CTRL_ACTIVE)	// do not process active TDs
			return ret;

		// extract transfer parameters from TD
		actual_length = (desc->hw.td.status + 1) & 0x7ff;
		maxlength = (((desc->hw.td.info >> 21) & 0x7ff) + 1) & 0x7ff;
		status = uhci_map_status (uhci_status_bits (desc->hw.td.status), usb_pipeout (urb->pipe));

		// see if EP is stalled
		if (status == -EPIPE) {
			// set up stalled condition
			usb_endpoint_halt (urb->dev, usb_pipeendpoint (urb->pipe), usb_pipeout (urb->pipe));
		}

		// if any error occured stop processing of further TDs
		if (status != 0) {
			// only set ret if status returned an error
			uhci_show_td (desc);
			ret = status;
			urb->error_count++;
			break;
		}
		else if ((desc->hw.td.info & 0xff) != USB_PID_SETUP)
			urb->actual_length += actual_length;

#if 0
		//	if (i++==0)
		       	uhci_show_td (desc);	// show first TD of each transfer
#endif

		// got less data than requested
		if ( (actual_length < maxlength)) {
			if (urb->transfer_flags & USB_DISABLE_SPD) {
				status = -EREMOTEIO;	// treat as real error
				dbg("process_transfer: SPD!!");
				break;	// exit after this TD because SP was detected
			}

			// short read during control-IN: re-start status stage
			if ((usb_pipetype (urb->pipe) == PIPE_CONTROL)) {
				if (uhci_packetid(last_desc->hw.td.info) == USB_PID_OUT) {
			
					qh->hw.qh.element = virt_to_bus (last_desc);  // re-trigger status stage
					dbg("short packet during control transfer, retrigger status stage @ %p",last_desc);
					uhci_show_td (desc);
					uhci_show_td (last_desc);
					urb_priv->short_control_packet=1;
					return 0;
				}
			}
			// all other cases: short read is OK
			data_toggle = uhci_toggle (desc->hw.td.info);
			break;
		}

		data_toggle = uhci_toggle (desc->hw.td.info);
		//dbg("process_transfer: len:%d status:%x mapped:%x toggle:%d", actual_length, desc->hw.td.status,status, data_toggle);      

	}
	usb_settoggle (urb->dev, usb_pipeendpoint (urb->pipe), usb_pipeout (urb->pipe), !data_toggle);
	transfer_finished:

	unlink_qh (s, qh);
	//delete_qh (s, qh);
	qh->last_used=UHCI_GET_CURRENT_FRAME(s);
	list_add_tail (&qh->horizontal, &s->free_desc); // mark for later deletion

	urb->status = status;

#ifdef USE_RECLAMATION_LOOP	
	disable_desc_loop(s,urb);
#endif	

	dbg("process_transfer: urb %p, wanted len %d, len %d status %x err %d",
		urb,urb->transfer_buffer_length,urb->actual_length, urb->status, urb->error_count);
	//dbg("process_transfer: exit");
#if 0
	if (urb->actual_length){
		char *uu;
		uu=urb->transfer_buffer;
		dbg("%x %x %x %x %x %x %x %x",
			*uu,*(uu+1),*(uu+2),*(uu+3),*(uu+4),*(uu+5),*(uu+6),*(uu+7));
	}
#endif
	return ret;
}

_static int process_interrupt (uhci_t *s, urb_t *urb)
{
	int i, ret = -EINPROGRESS;
	urb_priv_t *urb_priv = urb->hcpriv;
	struct list_head *p = urb_priv->desc_list.next;
	uhci_desc_t *desc = list_entry (urb_priv->desc_list.prev, uhci_desc_t, desc_list);

	int actual_length;
	int status = 0;

	//dbg("urb contains interrupt request");

	for (i = 0; p != &urb_priv->desc_list; p = p->next, i++)	// Maybe we allow more than one TD later ;-)
	{
		desc = list_entry (p, uhci_desc_t, desc_list);

		if (desc->hw.td.status & TD_CTRL_ACTIVE) {
			// do not process active TDs
			//dbg("TD ACT Status @%p %08x",desc,desc->hw.td.status);
			break;
		}

		if (!desc->hw.td.status & TD_CTRL_IOC) {
			// do not process one-shot TDs, no recycling
			break;
		}
		// extract transfer parameters from TD

		actual_length = (desc->hw.td.status + 1) & 0x7ff;
		status = uhci_map_status (uhci_status_bits (desc->hw.td.status), usb_pipeout (urb->pipe));

		// see if EP is stalled
		if (status == -EPIPE) {
			// set up stalled condition
			usb_endpoint_halt (urb->dev, usb_pipeendpoint (urb->pipe), usb_pipeout (urb->pipe));
		}

		// if any error occured: ignore this td, and continue
		if (status != 0) {
			uhci_show_td (desc);
			urb->error_count++;
			goto recycle;
		}
		else
			urb->actual_length = actual_length;

		// FIXME: SPD?

	recycle:
		if (urb->complete) {
			//dbg("process_interrupt: calling completion, status %i",status);
			urb->status = status;
			
			spin_unlock(&s->urb_list_lock);
			
			urb->complete ((struct urb *) urb);
			
			spin_lock(&s->urb_list_lock);
			
			urb->status = -EINPROGRESS;
		}

		// Recycle INT-TD if interval!=0, else mark TD as one-shot
		if (urb->interval) {

			desc->hw.td.info &= ~(1 << TD_TOKEN_TOGGLE);
			if (status==0) {
				desc->hw.td.info |= (usb_gettoggle (urb->dev, usb_pipeendpoint (urb->pipe),
				      usb_pipeout (urb->pipe)) << TD_TOKEN_TOGGLE);
				usb_dotoggle (urb->dev, usb_pipeendpoint (urb->pipe), usb_pipeout (urb->pipe));
			} else {
				desc->hw.td.info |= (!usb_gettoggle (urb->dev, usb_pipeendpoint (urb->pipe),
				      usb_pipeout (urb->pipe)) << TD_TOKEN_TOGGLE);
			}
			desc->hw.td.status= (urb->pipe & TD_CTRL_LS) | TD_CTRL_ACTIVE | TD_CTRL_IOC |
				(urb->transfer_flags & USB_DISABLE_SPD ? 0 : TD_CTRL_SPD) | (3 << 27);
			mb();
		}
		else {
			desc->hw.td.status &= ~TD_CTRL_IOC; // inactivate TD
		}
	}

	return ret;
}


_static int process_iso (uhci_t *s, urb_t *urb)
{
	int i;
	int ret = 0;
	urb_priv_t *urb_priv = urb->hcpriv;
	struct list_head *p = urb_priv->desc_list.next;
	uhci_desc_t *desc = list_entry (urb_priv->desc_list.prev, uhci_desc_t, desc_list);

	dbg("urb contains iso request");
	if (desc->hw.td.status & TD_CTRL_ACTIVE)
		return -EXDEV;	// last TD not finished

	urb->error_count = 0;
	urb->actual_length = 0;
	urb->status = 0;

	for (i = 0; p != &urb_priv->desc_list; p = p->next, i++) {
		desc = list_entry (p, uhci_desc_t, desc_list);

		//uhci_show_td(desc);
		if (desc->hw.td.status & TD_CTRL_ACTIVE) {
			// means we have completed the last TD, but not the TDs before
			desc->hw.td.status &= ~TD_CTRL_ACTIVE;
			dbg("TD still active (%x)- grrr. paranoia!", desc->hw.td.status);
			ret = -EXDEV;
			urb->iso_frame_desc[i].status = ret;
			unlink_td (s, desc, 1);
			// FIXME: immediate deletion may be dangerous
			goto err;
		}

		unlink_td (s, desc, 1);

		if (urb->number_of_packets <= i) {
			dbg("urb->number_of_packets (%d)<=(%d)", urb->number_of_packets, i);
			ret = -EINVAL;
			goto err;
		}

		if (urb->iso_frame_desc[i].offset + urb->transfer_buffer != bus_to_virt (desc->hw.td.buffer)) {
			// Hm, something really weird is going on
			dbg("Pointer Paranoia: %p!=%p", urb->iso_frame_desc[i].offset + urb->transfer_buffer, bus_to_virt (desc->hw.td.buffer));
			ret = -EINVAL;
			urb->iso_frame_desc[i].status = ret;
			goto err;
		}
		urb->iso_frame_desc[i].actual_length = (desc->hw.td.status + 1) & 0x7ff;
		urb->iso_frame_desc[i].status = uhci_map_status (uhci_status_bits (desc->hw.td.status), usb_pipeout (urb->pipe));
		urb->actual_length += urb->iso_frame_desc[i].actual_length;

	      err:

		if (urb->iso_frame_desc[i].status != 0) {
			urb->error_count++;
			urb->status = urb->iso_frame_desc[i].status;
		}
		dbg("process_iso: len:%d status:%x",
		     urb->iso_frame_desc[i].length, urb->iso_frame_desc[i].status);

		delete_desc (desc);
		list_del (p);
	}
	dbg("process_iso: exit %i (%d)", i, ret);
	return ret;
}


_static int process_urb (uhci_t *s, struct list_head *p)
{
	int ret = 0;
	urb_t *urb;


	urb=list_entry (p, urb_t, urb_list);
	dbg("found queued urb: %p", urb);

	switch (usb_pipetype (urb->pipe)) {
	case PIPE_CONTROL:
	case PIPE_BULK:
		ret = process_transfer (s, urb);
		break;
	case PIPE_ISOCHRONOUS:
		ret = process_iso (s, urb);
		break;
	case PIPE_INTERRUPT:
		ret = process_interrupt (s, urb);
		break;
	}

	if (urb->status != -EINPROGRESS) {
		int proceed = 0;

		dbg("dequeued urb: %p", urb);
		dequeue_urb (s, urb);

#ifdef DEBUG_SLAB
		kmem_cache_free(urb_priv_kmem, urb->hcpriv);
#else
		kfree (urb->hcpriv);
#endif

		if ((usb_pipetype (urb->pipe) != PIPE_INTERRUPT)) {
			urb_t *tmp = urb->next;	// pointer to first urb
			int is_ring = 0;
			
			if (urb->next) {
				do {
					if (tmp->status != -EINPROGRESS) {
						proceed = 1;
						break;
					}
					tmp = tmp->next;
				}
				while (tmp != NULL && tmp != urb->next);
				if (tmp == urb->next)
					is_ring = 1;
			}

			spin_unlock(&s->urb_list_lock);

			// In case you need the current URB status for your completion handler
			if (urb->complete && (!proceed || (urb->transfer_flags & USB_URB_EARLY_COMPLETE))) {
				dbg("process_transfer: calling early completion");
				urb->complete ((struct urb *) urb);
				if (!proceed && is_ring && (urb->status != -ENOENT))
					uhci_submit_urb (urb);
			}

			if (proceed && urb->next) {
				// if there are linked urbs - handle submitting of them right now.
				tmp = urb->next;	// pointer to first urb

				do {
					if ((tmp->status != -EINPROGRESS) && (tmp->status != -ENOENT) && uhci_submit_urb (tmp) != 0)
						break;
					tmp = tmp->next;
				}
				while (tmp != NULL && tmp != urb->next);	// submit until we reach NULL or our own pointer or submit fails

				if (urb->complete && !(urb->transfer_flags & USB_URB_EARLY_COMPLETE)) {
					dbg("process_transfer: calling completion");
					urb->complete ((struct urb *) urb);
				}
			}

			spin_lock(&s->urb_list_lock);

			usb_dec_dev_use (urb->dev);
		}
	}

	return ret;
}

_static void uhci_interrupt (int irq, void *__uhci, struct pt_regs *regs)
{
	uhci_t *s = __uhci;
	unsigned int io_addr = s->io_addr;
	unsigned short status;
	struct list_head *p, *p2;

	/*
	 * Read the interrupt status, and write it back to clear the
	 * interrupt cause
	 */

	status = inw (io_addr + USBSTS);

	if (!status)		/* shared interrupt, not mine */
		return;

	dbg("interrupt");

	if (status != 1) {
		warn("interrupt, status %x, frame# %i", status, 
		     UHCI_GET_CURRENT_FRAME(s));
		//uhci_show_queue(s->control_chain);
		// remove host controller halted state
		if ((status&0x20) && (s->running)) {
			// more to be done - check TDs for invalid entries
			// but TDs are only invalid if somewhere else is a (memory ?) problem
			outw (USBCMD_RS | inw(io_addr + USBCMD), io_addr + USBCMD);
		}
		//uhci_show_status (s);
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
restart:
	s->unlink_urb_done=0;
	p = s->urb_list.prev;	

	while (p != &s->urb_list) {
		p2 = p;
		p = p->prev;
		process_urb (s, p2);
		if(s->unlink_urb_done)
		{
			s->unlink_urb_done=0;
			goto restart;
		}
	}
	spin_unlock (&s->urb_list_lock);
	clean_descs(s,0);	

	outw (status, io_addr + USBSTS);

	dbg("done");
}

_static void reset_hc (uhci_t *s)
{
	unsigned int io_addr = s->io_addr;

	s->apm_state = 0;
	/* Global reset for 50ms */
	outw (USBCMD_GRESET, io_addr + USBCMD);
	uhci_wait_ms (50);
	outw (0, io_addr + USBCMD);
	uhci_wait_ms (10);
}

_static void start_hc (uhci_t *s)
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
			err("USBCMD_HCRESET timed out!");
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
	s->running = 1;
}

_static void __exit uhci_cleanup_dev(uhci_t *s)
{
	struct usb_device *root_hub = s->bus->root_hub;

	if (root_hub)
		usb_disconnect (&root_hub);

	uhci_unlink_urbs(s, 0, 1);  // Forced unlink of remaining URBs

	usb_deregister_bus (s->bus);

	s->running = 0;
	reset_hc (s);
	release_region (s->io_addr, s->io_size);
	free_irq (s->irq, s);
	usb_free_bus (s->bus);
	cleanup_skel (s);
	kfree (s);
}

_static int __init uhci_start_usb (uhci_t *s)
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

_static int handle_pm_event (struct pm_dev *dev, pm_request_t rqst, void *data)
{
	uhci_t *s = (uhci_t*) dev->data;
	dbg("handle_apm_event(%d)", rqst);
	if (s) {
		switch (rqst) {
		case PM_SUSPEND:
			reset_hc (s);
			break;
		case PM_RESUME:
			start_hc (s);
			break;
		}
	}
	return 0;
}

_static int __init alloc_uhci (struct pci_dev *dev, int irq, unsigned int io_addr, unsigned int io_size)
{
	uhci_t *s;
	struct usb_bus *bus;
	struct pm_dev *pmdev;

	s = kmalloc (sizeof (uhci_t), GFP_KERNEL);
	if (!s)
		return -1;

	memset (s, 0, sizeof (uhci_t));
	INIT_LIST_HEAD (&s->free_desc);
	INIT_LIST_HEAD (&s->urb_list);
	spin_lock_init (&s->urb_list_lock);
	spin_lock_init (&s->qh_lock);
	spin_lock_init (&s->td_lock);
	s->irq = -1;
	s->io_addr = io_addr;
	s->io_size = io_size;
	s->next = devs;	//chain new uhci device into global list	

	bus = usb_alloc_bus (&uhci_device_operations);
	if (!bus) {
		kfree (s);
		return -1;
	}

	s->bus = bus;
	bus->hcpriv = s;

	/* UHCI specs says devices must have 2 ports, but goes on to say */
	/* they may have more but give no way to determine how many they */
	/* have, so default to 2 */
	/* According to the UHCI spec, Bit 7 is always set to 1. So we try */
	/* to use this to our advantage */

	for (s->maxports = 0; s->maxports < (io_size - 0x10) / 2; s->maxports++) {
		unsigned int portstatus;

		portstatus = inw (io_addr + 0x10 + (s->maxports * 2));
		dbg("port %i, adr %x status %x", s->maxports,
			io_addr + 0x10 + (s->maxports * 2), portstatus);
		if (!(portstatus & 0x0080))
			break;
	}
	warn("Detected %d ports", s->maxports);

	/* This is experimental so anything less than 2 or greater than 8 is */
	/*  something weird and we'll ignore it */
	if (s->maxports < 2 || s->maxports > 8) {
		dbg("Port count misdetected, forcing to 2 ports");
		s->maxports = 2;
	}

	s->rh.numports = s->maxports;
	s->loop_usage=0;
	if (init_skel (s)) {
		usb_free_bus (bus);
		kfree(s);
		return -1;
	}

	request_region (s->io_addr, io_size, MODNAME);
	reset_hc (s);
	usb_register_bus (s->bus);

	start_hc (s);

	if (request_irq (irq, uhci_interrupt, SA_SHIRQ, MODNAME, s)) {
		err("request_irq %d failed!",irq);
		usb_free_bus (bus);
		reset_hc (s);
		release_region (s->io_addr, s->io_size);
		cleanup_skel(s);
		kfree(s);
		return -1;
	}

	s->irq = irq;

	if(uhci_start_usb (s) < 0) {
		uhci_cleanup_dev(s);
		return -1;
	}

	//chain new uhci device into global list
	devs = s;

	pmdev = pm_register(PM_PCI_DEV, PM_PCI_ID(dev), handle_pm_event);
	if (pmdev)
		pmdev->data = s;

	return 0;
}

_static int __init start_uhci (struct pci_dev *dev)
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
		/* disable legacy emulation */
		pci_write_config_word (dev, USBLEGSUP, USBLEGSUP_DEFAULT);
		return alloc_uhci(dev, dev->irq, io_addr, io_size);
	}
	return -1;
}

int __init uhci_init (void)
{
	int retval = -ENODEV;
	struct pci_dev *dev = NULL;
	u8 type;
	int i=0;

#ifdef DEBUG_SLAB

	uhci_desc_kmem = kmem_cache_create("uhci_desc", sizeof(uhci_desc_t), 0, SLAB_HWCACHE_ALIGN, NULL, NULL);
	
	if(!uhci_desc_kmem) {
		err("kmem_cache_create for uhci_desc failed (out of memory)");
		return -ENOMEM;
	}

	urb_priv_kmem = kmem_cache_create("urb_priv", sizeof(urb_priv_t), 0, SLAB_HWCACHE_ALIGN, NULL, NULL);
	
	if(!urb_priv_kmem) {
		err("kmem_cache_create for urb_priv_t failed (out of memory)");
		return -ENOMEM;
	}
#endif	
	info(VERSTR);

	for (;;) {
		dev = pci_find_class (PCI_CLASS_SERIAL_USB << 8, dev);
		if (!dev)
			break;

		/* Is it UHCI */
		pci_read_config_byte (dev, PCI_CLASS_PROG, &type);
		if (type != 0)
			continue;

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,3,8)
		pci_enable_device (dev);
#endif
		if(!dev->irq)
		{
			err("Found UHCI device with no IRQ assigned. Check BIOS settings!");
			continue;
		}

		/* Ok set it up */
		retval = start_uhci (dev);
	
		if (!retval)
			i++;
	}

	return retval;
}

void __exit uhci_cleanup (void)
{
	uhci_t *s;
	while ((s = devs)) {
		devs = devs->next;
		uhci_cleanup_dev(s);
	}
#ifdef DEBUG_SLAB
	if(kmem_cache_destroy(uhci_desc_kmem))
		err("uhci_desc_kmem remained");

	if(kmem_cache_destroy(urb_priv_kmem))
		err("urb_priv_kmem remained");
#endif
}

#ifdef MODULE
int init_module (void)
{
	return uhci_init ();
}

void cleanup_module (void)
{
	pm_unregister_all (handle_pm_event);
	uhci_cleanup ();
}

#endif //MODULE
