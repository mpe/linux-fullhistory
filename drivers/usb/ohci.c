/*
 * Open Host Controller Interface driver for USB.
 *
 * (C) Copyright 1999 Gregory P. Smith <greg@electricrain.com>
 *
 * This is the "other" host controller interface for USB.  You will
 * find this on many non-Intel based motherboards, and of course the
 * Mac.  As Linus hacked his UHCI driver together first, I modeled
 * this after his.. (it should be obvious)
 *
 * From the programming standpoint the OHCI interface seems a little
 * prettier and potentially less CPU intensive.  This remains to be
 * proven.  In reality, I don't believe it'll make one darn bit of
 * difference.  USB v1.1 is a slow bus by today's standards.
 *
 * OHCI hardware takes care of most of the scheduling of different
 * transfer types with the correct prioritization for us.
 *
 * To get started in USB, I used the "Universal Serial Bus System
 * Architecture" book by Mindshare, Inc.  It was a reasonable introduction
 * and overview of USB and the two dominant host controller interfaces
 * however you're better off just reading the real specs available
 * from www.usb.org as you'll need them to get enough detailt to
 * actually implement a HCD.  The book has many typos and omissions
 * Beware, the specs are the victim of a committee.
 *
 * This code was written with Guinness on the brain, xsnow on the desktop
 * and Orbital, Orb, Enya & Massive Attack on the CD player.  What a life!  ;) 
 *
 * No filesystems were harmed in the development of this code.
 *
 * $Id: ohci.c,v 1.11 1999/04/25 00:18:52 greg Exp $
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

#include <asm/spinlock.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>

#include "ohci.h"
#include "inits.h"

#ifdef CONFIG_APM
#include <linux/apm_bios.h>
static int handle_apm_event(apm_event_t event);
static int apm_resume = 0;
#endif

static struct wait_queue *ohci_configure = NULL;


static int ohci_td_result(struct ohci_device *dev, struct ohci_td *td)
{
	unsigned int status;

	status = td->info & OHCI_TD_CC;

	/* TODO Debugging code for TD failures goes here */

	return status;
}


static spinlock_t ohci_edtd_lock = SPIN_LOCK_UNLOCKED;

/*
 * Add a TD to the end of the TD list on a given ED.  td->next_td is
 * assumed to be set correctly for the situation of no TDs already
 * being on the list (ie: pointing to NULL).
 */
static void ohci_add_td_to_ed(struct ohci_td *td, struct ohci_ed *ed)
{
	struct ohci_td *tail = bus_to_virt(ed->tail_td);
	struct ohci_td *head = bus_to_virt(ed->head_td);
	unsigned long flags;

	spin_lock_irqsave(&ohci_edtd_lock, flags);

	if (tail == head) {	/* empty list, put it on the head */
		head = (struct ohci_td *) virt_to_bus(td);
		tail = 0;
	} else {
		if (!tail) {	/* no tail, single element list */
			td->next_td = head->next_td;
			head->next_td = virt_to_bus(td);
			tail = (struct ohci_td *) virt_to_bus(td);
		} else {	/* append to the list */
			td->next_td = tail->next_td;
			tail->next_td = virt_to_bus(td);
			tail = (struct ohci_td *) virt_to_bus(td);
		}
	}
	/* save the reverse link */
	td->ed_bus = virt_to_bus(ed);

	spin_unlock_irqrestore(&ohci_edtd_lock, flags);
} /* ohci_add_td_to_ed() */


/*
 *  Remove a TD from the given EDs TD list
 */
static void ohci_remove_td_from_ed(struct ohci_td *td, struct ohci_ed *ed)
{
	struct ohci_td *head = bus_to_virt(ed->head_td);
	struct ohci_td *tmp_td;
	unsigned long flags;

	spin_lock_irqsave(&ohci_edtd_lock, flags);

	/* set the "skip me bit" in this ED */
	writel_set(OHCI_ED_SKIP, ed->status);

	/* XXX Assuming this list will never be circular */

	if (td == head) {
		/* unlink this TD; it was at the beginning */
		ed->head_td = head->next_td;
	}

	tmp_td = head;
	head = (struct ohci_td *) ed->head_td;
	
	while (head != NULL) {

		if (td == head) {
			/* unlink this TD from the middle or end */
			tmp_td->next_td = head->next_td;
		}

		tmp_td = head;
		head = bus_to_virt(head->next_td);
	}

	td->next_td = virt_to_bus(NULL);  /* remove links to ED list */

	/* XXX mark this TD for possible cleanup? */

	/* unset the "skip me bit" in this ED */
	writel_mask(~(__u32)OHCI_ED_SKIP, ed->status);

	spin_unlock_irqrestore(&ohci_edtd_lock, flags);
} /* ohci_remove_td_from_ed() */


/*
 * Get a pointer (virtual) to an available TD from the given device's
 * pool.
 *
 * Return NULL if none are left.
 */
static struct ohci_td *ohci_get_free_td(struct ohci_device *dev)
{
	int idx;

	for (idx=0; idx < NUM_TDS; idx++) {
		if (td_done(dev->td[idx].info)) {
			/* XXX should this also zero out the structure? */
			/* mark all new TDs as unaccessed */
			dev->td[idx].info = OHCI_TD_CC_NEW;
			return &dev->td[idx];
		}
	}

	return NULL;
} /* ohci_get_free_td() */


/**********************************
 * OHCI interrupt list operations *
 **********************************/
static spinlock_t irqlist_lock = SPIN_LOCK_UNLOCKED;

static void ohci_add_irq_list(struct ohci *ohci, struct ohci_td *td, usb_device_irq completed, void *dev_id)
{
	unsigned long flags;

	/* save the irq in our private portion of the TD */
	td->completed = completed;
	td->dev_id = dev_id;

	spin_lock_irqsave(&irqlist_lock, flags);
	list_add(&td->irq_list, &ohci->interrupt_list);
	spin_unlock_irqrestore(&irqlist_lock, flags);
} /* ohci_add_irq_list() */

static void ohci_remove_irq_list(struct ohci_td *td)
{
	unsigned long flags;

	spin_lock_irqsave(&irqlist_lock, flags);
	list_del(&td->irq_list);
	spin_unlock_irqrestore(&irqlist_lock, flags);
} /* ohci_remove_irq_list() */

/*
 * Request an interrupt handler for one "pipe" of a USB device.
 * (this function is pretty minimal right now)
 *
 * At the moment this is only good for input interrupts. (ie: for a
 * mouse)
 *
 * period is desired polling interval in ms.  The closest, shorter
 * match will be used.  Powers of two from 1-32 are supported by OHCI.
 */
static int ohci_request_irq(struct usb_device *usb, unsigned int pipe,
	usb_device_irq handler, int period, void *dev_id)
{
	struct ohci_device *dev = usb_to_ohci(usb);
	struct ohci_td *td = dev->td;	/* */
	struct ohci_ed *interrupt_ed;	/* endpoint descriptor for this irq */

	/*
	 * Pick a good frequency endpoint based on the requested period
	 */
	interrupt_ed = &dev->ohci->root_hub->ed[ms_to_ed_int(period)];

	/*
	 * Set the max packet size, device speed, endpoint number, usb
	 * device number (function address), and type of TD.
	 *
	 * FIXME: Isochronous transfers need a pool of special 32 byte
	 * TDs (32 byte aligned) in order to be supported.
	 */
	interrupt_ed->status = \
		ed_set_maxpacket(usb_maxpacket(pipe)) |
		ed_set_speed(usb_pipeslow(pipe)) |
		usb_pipe_endpdev(pipe) |
		OHCI_ED_F_NORM;

	/*
	 * Set the not accessed condition code, allow odd sized data,
	 * and set the data transfer direction.
	 */
	td->info = OHCI_TD_CC_NEW | OHCI_TD_ROUND |
		td_set_dir_out(usb_pipeout(pipe));

	/* point it to our data buffer */
	td->cur_buf = virt_to_bus(dev->data);

	/* FIXME: we're only using 1 TD right now! */
	td->next_td = virt_to_bus(&td);

	/*
	 * FIXME: be aware that OHCI won't advance out of the 4kb
	 * page cur_buf started in.  It'll wrap around to the start
	 * of the page...  annoying or useful? you decide.
	 *
	 * A pointer to the last *byte* in the buffer (ergh.. we get
	 * to work around C's pointer arithmatic here with a typecast)
	 */
	td->buf_end = virt_to_bus(((u8*)(dev->data + DATA_BUF_LEN)) - 1);

	/* does this make sense for ohci?.. time to think.. */
	ohci_add_irq_list(dev->ohci, td, handler, dev_id);
	wmb();     /* found in asm/system.h; scary concept... */
	ohci_add_td_to_ed(td, interrupt_ed);

	return 0;
} /* ohci_request_irq() */

/*
 * Control thread operations:
 */
static struct wait_queue *control_wakeup;

static int ohci_control_completed(int stats, void *buffer, void *dev_id)
{
	wake_up(&control_wakeup);
	return 0;
} /* ohci_control_completed() */


/*
 * Run a control transaction from the root hub's control endpoint.
 * The passed in TD is the control transfer's Status TD.
 */
static int ohci_run_control(struct ohci_device *dev, struct ohci_td *status_td)
{
	struct wait_queue wait = { current, NULL };
	struct ohci_ed *control_ed = &dev->ohci->root_hub->ed[ED_CONTROL];

	current->state = TASK_UNINTERRUPTIBLE;
	add_wait_queue(&control_wakeup, &wait);

	ohci_add_irq_list(dev->ohci, status_td, ohci_control_completed, NULL);
	ohci_add_td_to_ed(status_td, control_ed);

	/* FIXME? isn't this a little gross */
	schedule_timeout(HZ/10);

	ohci_remove_irq_list(status_td);
	ohci_remove_td_from_ed(status_td, control_ed);

	return ohci_td_result(dev, status_td);
} /* ohci_run_control() */

/*
 * Send or receive a control message on a "pipe"
 *
 * A control message contains:
 *   - The command itself
 *   - An optional data phase
 *   - Status complete phase
 *
 * The data phase can be an arbitrary number of TD's.  Currently since
 * we use statically allocated TDs if too many come in we'll just
 * start tossing them and printk() some warning goo...  Most control
 * messages won't have much data anyways.
 */
static int ohci_control_msg(struct usb_device *usb, unsigned int pipe, void *cmd, void *data, int len)
{
	struct ohci_device *dev = usb_to_ohci(usb);
	/*
	 * ideally dev->ed should be linked into the root hub's
	 * control_ed list and used instead of just using it directly.
	 * This could present a problem as is with more than one
	 * device.  (but who wants to use a keyboard AND a mouse
	 * anyways? ;)
	 */
	struct ohci_ed *control_ed = &dev->ohci->root_hub->ed[ED_CONTROL];
	struct ohci_td *control_td;
	struct ohci_td *data_td;
	struct ohci_td *last_td;
	__u32 data_td_info;

	/*
	 * Set the max packet size, device speed, endpoint number, usb
	 * device number (function address), and type of TD.
	 *
	 */
	control_ed->status = \
		ed_set_maxpacket(usb_maxpacket(pipe)) |
		ed_set_speed(usb_pipeslow(pipe)) |
		usb_pipe_endpdev(pipe) |
		OHCI_ED_F_NORM;

	/*
	 * Build the control TD
	 */

	/* get a TD to send this control message with */
	control_td = ohci_get_free_td(dev);
	/* TODO check for NULL */

	/*
	 * Set the not accessed condition code, allow odd sized data,
	 * and set the data transfer type to SETUP.  Setup DATA always
	 * uses a DATA0 packet.
	 */
	control_td->info = OHCI_TD_CC_NEW | OHCI_TD_ROUND |
		OHCI_TD_D_SETUP | OHCI_TD_IOC_OFF | td_force_toggle(0);

	/* point it to the command */
	control_td->cur_buf = virt_to_bus(cmd);

	/* link to a free TD for the control data input */
	data_td = ohci_get_free_td(dev);  /* TODO check for NULL */
	control_td->next_td = virt_to_bus(data_td);

	/*
	 * Build the DATA TDs
	 */

	data_td_info = OHCI_TD_CC_NEW | OHCI_TD_ROUND | OHCI_TD_IOC_OFF |
		td_set_dir_out(usb_pipeout(pipe));

	while (len > 0) {
		int pktsize = len;
		struct ohci_td *tmp_td;

		if (pktsize > usb_maxpacket(pipe))
			pktsize = usb_maxpacket(pipe);

		/* set the data transaction type */
		data_td->info = data_td_info;
		/* point to the current spot in the data buffer */
		data_td->cur_buf = virt_to_bus(data);
		/* point to the end of this data */
		data_td->buf_end = virt_to_bus(data+pktsize-1);

		/* allocate the next TD */
		tmp_td = ohci_get_free_td(dev);  /* TODO check for NULL */
		data_td->next_td = virt_to_bus(tmp_td);
		data_td = tmp_td;

		/* move on.. */
		data += pktsize;
		len -= pktsize;
	}

	/* point it at the newly allocated TD from above */
	last_td = data_td;

	/* The control status packet always uses a DATA1 */
	last_td->info = OHCI_TD_CC_NEW | OHCI_TD_ROUND | td_force_toggle(1);
	last_td->next_td = 0; /* end of TDs */
	last_td->cur_buf = 0; /* no data in this packet */
	last_td->buf_end = 0;

	/*
	 * Start the control transaction.. give it the last TD so the
	 * result can be returned.
	 */
	return ohci_run_control(dev, last_td);
} /* ohci_control_msg() */


static struct usb_device *ohci_usb_allocate(struct usb_device *parent)
{
	struct usb_device *usb_dev;
	struct ohci_device *dev;

	usb_dev = kmalloc(sizeof(*usb_dev), GFP_KERNEL);
	if (!usb_dev)
		return NULL;

	memset(usb_dev, 0, sizeof(*usb_dev));

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
		dev->ohci = usb_to_ohci(parent)->ohci;
	}

	return usb_dev;
}

static int ohci_usb_deallocate(struct usb_device *usb_dev)
{
	kfree(usb_to_ohci(usb_dev));
	kfree(usb_dev);
	return 0;
}

/*
 * functions for the generic USB driver
 */
struct usb_operations ohci_device_operations = {
	ohci_usb_allocate,
	ohci_usb_deallocate,
	ohci_control_msg,
	ohci_request_irq,
};

/*
 * Reset an OHCI controller
 */
static void reset_hc(struct ohci *ohci)
{
	writel((1<<31), &ohci->regs->intrdisable); /* Disable HC interrupts */
	writel(1,  &ohci->regs->cmdstatus);	   /* HC Reset */
	writel_mask(0x3f, &ohci->regs->control);   /* move to UsbReset state */
} /* reset_hc() */


/*
 * Reset and start an OHCI controller
 */
static void start_hc(struct ohci *ohci)
{
	int timeout = 1000;	/* used to prevent an infinite loop. */

	reset_hc(ohci);

	while ((readl(&ohci->regs->control) & 0xc0) == 0) {
		if (!--timeout) {
			printk("USB HC Reset timed out!\n");
			break;
		}
	}

	/* Choose the interrupts we care about */
	writel( OHCI_INTR_MIE | OHCI_INTR_RHSC | OHCI_INTR_SF |
		OHCI_INTR_WDH | OHCI_INTR_SO | OHCI_INTR_UE |
		OHCI_INTR_FNO,
		&ohci->regs->intrenable);

	/* Enter the USB Operational state & start the frames a flowing.. */
	writel_set(OHCI_USB_OPER, &ohci->regs->control);

} /* start_hc() */


/*
 * Reset a root hub port
 */
static void ohci_reset_port(struct ohci *ohci, unsigned int port)
{
	short ms;
	int status;

	/* Don't allow overflows. */
	if (port >= MAX_ROOT_PORTS) {
		printk("Bad port # passed to ohci_reset_port\n");
		port = MAX_ROOT_PORTS-1;
	}

	writel(PORT_PRS, &ohci->regs->roothub.portstatus[port]);  /* Reset */

	/*
	 * Get the time required for a root hub port to reset and wait
	 * it out (adding 1ms for good measure).
	 */
	ms = (readl(&ohci->regs->roothub.a) >> 24) * 2 + 1;
	wait_ms(ms);

	/* check port status to see that the reset completed */
	status = readl(&ohci->regs->roothub.portstatus[port]);
	if (status & PORT_PRS) {
		/* reset failed, try harder? */
		printk("usb-ohci: port %d reset failed, retrying\n", port);
		writel(PORT_PRS, &ohci->regs->roothub.portstatus[port]);
		wait_ms(50);
	}

	/* TODO we might need to re-enable the port here or is that
	 * done elsewhere? */

} /* ohci_reset_port */


/*
 * This gets called if the connect status on the root hub changes.
 */
static void ohci_connect_change(struct ohci * ohci, int port)
{
	struct usb_device *usb_dev;
	struct ohci_device *dev;
	/* memory I/O address of the port status register */
	void *portaddr = &ohci->regs->roothub.portstatus[port];
	int portstatus;	

	/*
	 * Because of the status change we have to forget
	 * everything we think we know about the device
	 * on this root hub port.  It may have changed.
	 */
	usb_disconnect(ohci->root_hub->usb->children + port);

	portstatus = readl(portaddr);

	/* disable the port if nothing is connected */
	if (!(portstatus & PORT_CCS)) {
		writel(PORT_CCS, portaddr);
		return;
	}

	/*
	 * Allocate a device for the new thingy that's been attached
	 */
	usb_dev = ohci_usb_allocate(ohci->root_hub->usb);
	dev = usb_dev->hcpriv;

	dev->ohci = ohci;

	usb_connect(dev->usb);

	/* link it into the bus's device tree */
	ohci->root_hub->usb->children[port] = usb_dev;

	wait_ms(200); /* wait for powerup; XXX is this needed? */
	ohci_reset_port(ohci, port);

	/* Get information on speed by using LSD */
	usb_dev->slow = readl(portaddr) & PORT_LSDA ? 1 : 0;

	/*
	 * Do generic USB device tree processing on the new device.
	 */
	usb_new_device(usb_dev);
} /* ohci_connect_change() */


/*
 * This gets called when the root hub configuration
 * has changed.  Just go through each port, seeing if
 * there is something interesting happening.
 */
static void ohci_check_configuration(struct ohci *ohci)
{
	int num = 0;
	int maxport = readl(&ohci->regs->roothub) & 0xff;

	do {
		if (readl(ohci->regs->roothub.portstatus[num]) & PORT_CSC)
			ohci_connect_change(ohci, num);
	} while (++num < maxport);
} /* ohci_check_configuration() */


/*
 * Get annoyed at the controller for bothering us.
 */
static void ohci_interrupt(int irq, void *__ohci, struct pt_regs *r)
{
	struct ohci *ohci = __ohci;
	struct ohci_regs *regs = ohci->regs;
	struct ohci_hcca *hcca = ohci->root_hub->hcca;
	__u32 donehead = hcca->donehead;

	/*
	 * Check the interrupt status register if needed
	 */
	if (!donehead || (donehead & 1)) {
		__u32 intrstatus = readl(&regs->intrstatus);

		/*
		 * XXX eek! printk's in an interrupt handler.  shoot me!
		 */
		if (intrstatus & OHCI_INTR_SO) {
			printk(KERN_DEBUG "usb-ohci: scheduling overrun\n");
		}
		if (intrstatus & OHCI_INTR_RD) {
			printk(KERN_DEBUG "usb-ohci: resume detected\n");
		}
		if (intrstatus & OHCI_INTR_UE) {
			printk(KERN_DEBUG "usb-ohci: unrecoverable error\n");
		}
		if (intrstatus & OHCI_INTR_OC) {
			printk(KERN_DEBUG "usb-ohci: ownership change?\n");
		}

		if (intrstatus & OHCI_INTR_RHSC) {
			/* TODO Process events on the root hub */
		}
	}

	/*
	 * Process the done list
	 */
	if (donehead &= ~0x1) {
		/*
		 * TODO See which TD's completed..
		 */
	}

	/* Re-enable done queue interrupts and reset the donehead */
	hcca->donehead = 0;
	writel(OHCI_INTR_WDH, &regs->intrenable);
	
} /* ohci_interrupt() */


/*
 * Allocate the resources required for running an OHCI controller.
 * Host controller interrupts must not be running while calling this
 * function or the penguins will get angry.
 *
 * The mem_base parameter must be the usable -virtual- address of the
 * host controller's memory mapped I/O registers.
 */
static struct ohci *alloc_ohci(void* mem_base)
{
	int i;
	struct ohci *ohci;
	struct usb_bus *bus;
	struct ohci_device *dev;
	struct usb_device *usb;

	ohci = kmalloc(sizeof(*ohci), GFP_KERNEL);
	if (!ohci)
		return NULL;

	memset(ohci, 0, sizeof(*ohci));

	ohci->irq = -1;
	ohci->regs = mem_base;
	INIT_LIST_HEAD(&ohci->interrupt_list);

	bus = kmalloc(sizeof(*bus), GFP_KERNEL);
	if (!bus)
		return NULL;

	memset(bus, 0, sizeof(*bus));

	ohci->bus = bus;
	bus->hcpriv = ohci;
	bus->op = &ohci_device_operations;

	/*
	 * Here we allocate our own root hub and TDs as well as the
	 * OHCI host controller communications area.  The HCCA is just
	 * a nice pool of memory with pointers to endpoint descriptors
	 * for the different interrupts.
	 */
	usb = ohci_usb_allocate(NULL);
	if (!usb)
		return NULL;

	dev = ohci->root_hub = usb_to_ohci(usb);

	usb->bus = bus;

	/* Initialize the root hub */
	memset(dev, 0, sizeof(*dev));
	dev->ohci = ohci;    /* link back to the controller */

	/*
	 * Allocate the Host Controller Communications Area
	 */
	dev->hcca = (struct ohci_hcca *) kmalloc(sizeof(*dev->hcca), GFP_KERNEL);

	/* Tell the controller where the HCCA is */
	writel(virt_to_bus(dev->hcca), &ohci->regs->hcca);

	/* Get the number of ports on the root hub */
	usb->maxchild = readl(&ohci->regs->roothub.a) & 0xff;
	if (usb->maxchild > MAX_ROOT_PORTS) {
		printk("usb-ohci: Limited to %d ports\n", MAX_ROOT_PORTS);
		usb->maxchild = MAX_ROOT_PORTS;
	}
	if (usb->maxchild < 1) {
		printk("usb-ohci: Less than one root hub port? Impossible!\n");
		usb->maxchild = 1;
	}
	printk("usb-ohci: %d root hub ports found\n", usb->maxchild);

	printk("alloc_ohci() controller\n");
	show_ohci_status(ohci);
	printk("alloc_ohci() root_hub device\n");
	show_ohci_device(dev);

	/*
	 * Initialize the ED polling "tree" (for simplicity's sake in
	 * this driver many nodes in the tree will be identical)
	 */
	dev->ed[ED_INT_32].next_ed = virt_to_bus(&dev->ed[ED_INT_16]);
	dev->ed[ED_INT_16].next_ed = virt_to_bus(&dev->ed[ED_INT_8]);
	dev->ed[ED_INT_8].next_ed = virt_to_bus(&dev->ed[ED_INT_4]);
	dev->ed[ED_INT_4].next_ed = virt_to_bus(&dev->ed[ED_INT_2]);
	dev->ed[ED_INT_2].next_ed = virt_to_bus(&dev->ed[ED_INT_1]);

	/*
	 * Initialize the polling table to call interrupts at the
	 * intended intervals.
	 */
	for (i = 0; i < NUM_INTS; i++) {
		if (i == 0)
			dev->hcca->int_table[i] =
				virt_to_bus(&dev->ed[ED_INT_32]);
		else if (i & 1)
			dev->hcca->int_table[i] =
				virt_to_bus(&dev->ed[ED_INT_16]);
		else if (i & 2)
			dev->hcca->int_table[i] =
				virt_to_bus(&dev->ed[ED_INT_8]);
		else if (i & 4)
			dev->hcca->int_table[i] =
				virt_to_bus(&dev->ed[ED_INT_4]);
		else if (i & 8)
			dev->hcca->int_table[i] =
				virt_to_bus(&dev->ed[ED_INT_2]);
		else if (i & 16)
			dev->hcca->int_table[i] =
				virt_to_bus(&dev->ed[ED_INT_1]);
	}

	/*
	 * Tell the controller where the control and bulk lists are
	 */
	writel(virt_to_bus(&dev->ed[ED_CONTROL]), &ohci->regs->ed_controlhead);
	writel(virt_to_bus(&dev->ed[ED_BULK]), &ohci->regs->ed_bulkhead);

	return ohci;
} /* alloc_ohci() */


/*
 * De-allocate all resoueces..
 */
static void release_ohci(struct ohci *ohci)
{
	if (ohci->irq >= 0) {
		free_irq(ohci->irq, ohci);
		ohci->irq = -1;
	}

	if (ohci->root_hub) {
		/* ensure that HC is stopped before releasing the HCCA */
		writel(OHCI_USB_SUSPEND, &ohci->regs->control);
		free_pages((unsigned int) ohci->root_hub->hcca, 1);
		free_pages((unsigned int) ohci->root_hub, 1);
		ohci->root_hub->hcca = NULL;
		ohci->root_hub = NULL;
	}

	/* unmap the IO address space */
	iounmap(ohci->regs);

	/* If the ohci itself were dynamic we'd free it here */

} /* release_ohci() */

/*
 * USB OHCI control thread
 */
static int ohci_control_thread(void * __ohci)
{
	struct ohci *ohci = (struct ohci *)__ohci;
	
	/*
	 * I'm unfamiliar with the SMP kernel locking.. where should
	 * this be released?  -greg
	 */
	lock_kernel();

	/*
	 * This thread doesn't need any user-level access,
	 * so get rid of all of our resources..
	 */
	printk("ohci_control_thread at %p\n", &ohci_control_thread);
	exit_mm(current);
	exit_files(current);
	exit_fs(current);

	strcpy(current->comm, "ohci-control");

	/*
	 * Damn the torpedoes, full speed ahead
	 */
	start_hc(ohci);
	do {
		interruptible_sleep_on(&ohci_configure);
#ifdef CONFIG_APM
		if (apm_resume) {
			apm_resume = 0;
			start_hc(ohci);
			continue;
		}
#endif
		ohci_check_configuration(ohci);
	} while (!signal_pending(current));

	reset_hc(ohci);

	release_ohci(ohci);
	MOD_DEC_USE_COUNT;
	return 0;
} /* ohci_control_thread() */


#ifdef CONFIG_APM
static int handle_apm_event(apm_event_t event)
{
	static int down = 0;

	switch (event) {
	case APM_SYS_SUSPEND:
	case APM_USER_SUSPEND:
		if (down) {
			printk(KERN_DEBUG "usb-ohci: received extra suspend event\n");
			break;
		}
		down = 1;
		break;
	case APM_NORMAL_RESUME:
	case APM_CRITICAL_RESUME:
		if (!down) {
			printk(KERN_DEBUG "usb-ohci: received bogus resume event\n");
			break;
		}
		down = 0;
		if (waitqueue_active(&ohci_configure)) {
			apm_resume = 1;
			wake_up(&ohci_configure);
		}
		break;
	}
	return 0;
}
#endif

/* ... */

/*
 * Increment the module usage count, start the control thread and
 * return success if the controller is good.
 */
static int found_ohci(int irq, void* mem_base)
{
	int retval;
	struct ohci *ohci;

	/* Allocate the running OHCI structures */
	ohci = alloc_ohci(mem_base);
	if (!ohci)
		return -ENOMEM;

	printk("usb-ohci: alloc_ohci() = %p\n", ohci);

	reset_hc(ohci);

	retval = -EBUSY;
	if (request_irq(irq, ohci_interrupt, SA_SHIRQ, "usb-ohci", ohci) == 0) {
		int pid;

		MOD_INC_USE_COUNT;
		ohci->irq = irq;

		/* fork off the handler */
		pid = kernel_thread(ohci_control_thread, ohci,
				CLONE_FS | CLONE_FILES | CLONE_SIGHAND);
		if (pid >= 0)
			return 0;

		MOD_DEC_USE_COUNT;
		retval = pid;
	}
	release_ohci(ohci);
	return retval;
} /* found_ohci() */


/*
 * If this controller is for real, map the IO memory and proceed
 */
static int init_ohci(struct pci_dev *dev)
{
	unsigned int mem_base = dev->base_address[0];
	
	printk("usb-ohci: mem_base is %p\n", (void*)mem_base);

	/* If its OHCI, its memory */
	if (mem_base & PCI_BASE_ADDRESS_SPACE_IO)
		return -ENODEV;

	/* Get the memory address and map it for IO */
	mem_base &= PCI_BASE_ADDRESS_MEM_MASK;
	/* 
	 * FIXME ioremap_nocache isn't implemented on all CPUs (such
	 * as the Alpha) [?]  What should I use instead...
	 *
	 * The iounmap() is done on in release_ohci.
	 */
	mem_base = (unsigned int) ioremap_nocache(mem_base, 4096);

	if (!mem_base) {
		printk("Error mapping OHCI memory\n");
		return -EFAULT;
	}

	return found_ohci(dev->irq, (void *) mem_base);
} /* init_ohci() */


#ifdef MODULE

/*
 *  Clean up when unloading the module
 */
void cleanup_module(void)
{
#ifdef CONFIG_APM
	apm_unregister_callback(&handle_apm_event);
#endif
}

#define ohci_init init_module

#endif


/* TODO this should be named following Linux convention and go in pci.h */
#define PCI_CLASS_SERIAL_USB_OHCI ((PCI_CLASS_SERIAL_USB << 8) | 0x0010)

/*
 * Search the PCI bus for an OHCI USB controller and set it up
 *
 * If anyone wants multiple controllers this will need to be
 * updated..  Right now, it just picks the first one it finds.
 */
int ohci_init(void)
{
	int retval;
	struct pci_dev *dev = NULL;
	/*u8 type;*/

	if (sizeof(struct ohci_device) > 4096) {
		printk("usb-ohci: struct ohci_device to large\n");
		return -ENODEV;
	}

	retval = -ENODEV;
	for (;;) {
		/* Find an OHCI USB controller */
		dev = pci_find_class(PCI_CLASS_SERIAL_USB_OHCI, dev);
		if (!dev)
			break;

		/* Verify that its OpenHCI by checking for MMIO */
		/* pci_read_config_byte(dev, PCI_CLASS_PROG, &type);
		if (!type)
			continue; */

		/* Ok, set it up */
		retval = init_ohci(dev);
		if (retval < 0)
			continue;

		/* TODO check module params here to determine what to load */

/*		usb_mouse_init(); */
/*		usb_kbd_init();
		hub_init();	*/
#ifdef CONFIG_APM
		apm_register_callback(&handle_apm_event);
#endif

		return 0; /* no error */
	}
	return retval;
} /* init_module() */

/* vim:sw=8
 */
