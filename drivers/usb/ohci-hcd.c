/*
 * OHCI HCD (Host Controller Driver) for USB.
 *
 * (C) Copyright 1999 Roman Weissgaerber <weissg@vienna.at>
 *
 * The OHCI HCD layer is a simple but nearly complete implementation of what 
 * the USB people would call a HCD  for the OHCI. 
 * (ISO alpha , Bulk, INT u. CTRL transfers enabled)
 * The layer on top of it, is for interfacing to the alternate-usb 
 * device-drivers.
 * 
 * [ This is based on Linus' UHCI code and gregs OHCI fragments 
 * (0.03c source tree). ]
 * [ Open Host Controller Interface driver for USB. ]
 * [ (C) Copyright 1999 Linus Torvalds (uhci.c) ]
 * [ (C) Copyright 1999 Gregory P. Smith <greg@electricrain.com> ]
 * [ _Log: ohci-hcd.c,v _
 * [ Revision 1.1  1999/04/05 08:32:30  greg ]
 * 
 * v4.2 1999/09/05 ISO API alpha, new dev alloc, neg Error-codes
 * v4.1 1999/08/27 Randy Dunlap's - ISO API first impl.
 * v4.0 1999/08/18 
 * v3.0 1999/06/25 
 * v2.1 1999/05/09  code clean up
 * v2.0 1999/05/04 
 * virtual root hub is now enabled, 
 * memory allocation based on kmalloc and kfree now, Bus error handling, 
 * INT, CTRL and BULK transfers enabled, ISO needs testing (alpha)
 * 
 * from Linus Torvalds (uhci.c) (APM not tested; hub, usb_device, bus and related stuff)
 * from Greg Smith (ohci.c) (reset controller handling, hub)
 * 
 * v1.0 1999/04/27 initial release
 * ohci-hcd.c
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
#include <linux/timer.h>
#include <linux/spinlock.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>

#include "usb.h"
#include "ohci-hcd.h"

#ifdef CONFIG_APM
#include <linux/apm_bios.h>
static int handle_apm_event(apm_event_t event);
static int apm_resume = 0;
#endif

static int ohci_link_ed(struct ohci * ohci, struct usb_ohci_ed *ed);
static int sohci_kill_isoc (struct usb_isoc_desc *id);
static int sohci_get_current_frame_number (struct usb_device *usb_dev);
static int sohci_run_isoc(struct usb_isoc_desc *id, struct usb_isoc_desc *pr_id);
static DECLARE_WAIT_QUEUE_HEAD(op_wakeup);
 
void usb_pipe_to_hcd_ed(struct usb_device *usb_dev, unsigned int pipe, struct usb_hcd_ed  *hcd_ed) 
{
	hcd_ed->endpoint = usb_pipeendpoint(pipe);
	hcd_ed->out      = usb_pipeout(pipe);
	hcd_ed->function = usb_pipedevice(pipe);
	hcd_ed->type     = usb_pipetype(pipe);
	hcd_ed->slow     = usb_pipeslow(pipe);
	hcd_ed->maxpack  = usb_maxpacket(usb_dev, pipe, usb_pipeout(pipe));
	OHCI_DEBUG(printk("******* hcd_ed: pipe: %8x, endpoint: %4x, function: %4x, out: %4x, type: %4x, slow: %4x, maxpack: %4x\n", pipe, hcd_ed->endpoint, hcd_ed->function, hcd_ed->out, hcd_ed->type, hcd_ed->slow, hcd_ed->maxpack); )
OHCI_DEBUG(printk("******* dev: devnum: %4x, slow: %4x, maxpacketsize: %4x\n",usb_dev->devnum, usb_dev->slow, usb_dev->maxpacketsize); )
}


/********
 **** Interface functions
 ***********************************************/
 
static int sohci_blocking_handler(void * ohci_in, struct usb_ohci_ed *ed, void * data, int data_len, int status, __OHCI_BAG lw0, __OHCI_BAG lw1)
{  
	if(lw0 != NULL) {
		if(USB_ST_CRC < 0 && (status == USB_ST_DATAUNDERRUN || status == USB_ST_NOERROR)) 
			((struct ohci_state * )lw0)->status = data_len;
		else
			((struct ohci_state * )lw0)->status = status;
		((struct ohci_state * )lw0)->len = data_len;
	}
	if(lw1 != NULL) {
		add_wait_queue(&op_wakeup, lw1);   
		wake_up(&op_wakeup);
	}
	
	OHCI_DEBUG( { int i; printk("USB HC bh <<<: %x: ", ed->hwINFO);)
	OHCI_DEBUG( printk(" data(%d):", data_len);) 
	OHCI_DEBUG( for(i=0; i < data_len; i++ ) printk(" %02x", ((__u8 *) data)[i]);)
	OHCI_DEBUG( printk(" ret_status: %x\n", status); })
	
    return 0;                       
}
  
static int sohci_int_handler(void * ohci_in, struct usb_ohci_ed *ed, void * data, int data_len, int status, __OHCI_BAG lw0, __OHCI_BAG lw1)
{

	struct ohci * ohci = ohci_in; 
	usb_device_irq handler=(void *) lw0;
	void *dev_id = (void *) lw1;
	int ret;

	OHCI_DEBUG({ int i; printk("USB HC IRQ <<<: %x: data(%d):", ed->hwINFO, data_len);)
	OHCI_DEBUG( for(i=0; i < data_len; i++ ) printk(" %02x", ((__u8 *) data)[i]);)
	OHCI_DEBUG( printk(" ret_status: %x\n", status); })
 
	ret = handler(status, data, data_len, dev_id);
	if(ret == 0) return 0; /* 0 .. do not requeue  */
	if(status > 0) return -1; /* error occured do not requeue ? */
	ohci_trans_req(ohci, ed, 0, NULL, data, (ed->hwINFO >> 16) & 0x3f, (__OHCI_BAG) handler, (__OHCI_BAG) dev_id, INT_IN, sohci_int_handler); /* requeue int request */
 
	return 0;
}

static int sohci_iso_handler(void * ohci_in, struct usb_ohci_ed *ed, void * data, int data_len, int status, __OHCI_BAG lw0, __OHCI_BAG lw1) {

	// struct ohci * ohci = ohci_in; 
	unsigned int ix = (unsigned int) lw0;
	struct usb_isoc_desc * id = (struct usb_isoc_desc *) lw1;
	struct usb_ohci_td **tdp = id->td;
	int ret = 0;
	int fx;

	OHCI_DEBUG({ int i; printk("USB HC ISO |||: %x: data(%d):", ed->hwINFO, data_len);)
	OHCI_DEBUG( for(i=0; i < 16 ; i++ ) printk(" %02x", ((__u8 *) data)[i]);)
	OHCI_DEBUG( printk(" ... ret_status: %x\n", status); })

	tdp[ix] = NULL;
	id->frames[ix].frame_length = data_len;
	id->frames[ix].frame_status = status;
	id->total_length += data_len;
	if(status) id->error_count++;
	
	id->cur_completed_frame++;
	id->total_completed_frames++;
	
	if(id->cur_completed_frame == id->callback_frames) {
		id->prev_completed_frame = id->cur_completed_frame;
		id->cur_completed_frame = 0;
		OHCI_DEBUG(printk("USB HC ISO <<<: %x: \n", ed->hwINFO);)
		ret = id->callback_fn(id->error_count, id->data, id->total_length, id);
		switch (ret) {
			case CB_CONT_RUN:
				for (fx = 0; fx < id->frame_count; fx++)
					id->frames[fx].frame_length = id->frame_size;
				sohci_run_isoc(id, id->prev_isocdesc);
				break;
				
			case CB_CONTINUE:
				break;
		
			case CB_REUSE:
				break;
		
			case CB_RESTART:
				break;
		
			case CB_ABORT:
				sohci_kill_isoc(id);
				break;
		}
	}
	
	return 0;
}
                                                     
static int sohci_request_irq(struct usb_device *usb_dev, unsigned int pipe, usb_device_irq handler, int period, void *dev_id, void **handle, long bustime)
{
	struct ohci * ohci = usb_dev->bus->hcpriv;
	struct ohci_device * dev = usb_to_ohci(usb_dev);
	struct usb_hcd_ed hcd_ed;
	struct usb_ohci_ed * ed;

#ifdef  VROOTHUB
	if(usb_pipedevice(pipe) == ohci->rh.devnum) 
		return root_hub_request_irq(usb_dev, pipe, handler, period, dev_id, handle);
#endif		

	usb_pipe_to_hcd_ed(usb_dev, pipe, &hcd_ed);
	hcd_ed.type = INT;
	ed = usb_ohci_add_ep(usb_dev, &hcd_ed, period, 1);
	
	OHCI_DEBUG( printk("USB HC IRQ>>>: %x: every %d ms\n", ed->hwINFO, period);) 
	
	ohci_trans_req(ohci, ed, 0, NULL, dev->data, hcd_ed.maxpack, (__OHCI_BAG) handler, (__OHCI_BAG) dev_id, INT_IN, sohci_int_handler);
	if (ED_STATE(ed) != ED_OPER)  ohci_link_ed(ohci, ed);
	*handle = ed;
	return 0;
    
}

static int sohci_release_irq(struct usb_device *usb_dev, void * ed)
{  
	// struct usb_device *usb_dev = ((struct ohci_device *) ((unsigned int)ed & 0xfffff000))->usb;
	struct ohci * ohci = usb_dev->bus->hcpriv; 
	
	OHCI_DEBUG( printk("USB HC ***** RM_IRQ>>>:%4x\n", (unsigned int) ed);)

	if(ed == NULL) return 0;
			 
#ifdef  VROOTHUB
	if(ed == ohci->rh.int_addr) 
		return root_hub_release_irq(usb_dev, ed);
#endif		
	ED_setSTATE((struct usb_ohci_ed *)ed, ED_STOP);
	 
    usb_ohci_rm_ep(usb_dev, (struct usb_ohci_ed *) ed, NULL, NULL, NULL, 0);
 
	return 0;
}

static int sohci_control_msg(struct usb_device *usb_dev, unsigned int pipe, devrequest *cmd, void *data, int len, int timeout)
{
	DECLARE_WAITQUEUE(wait, current);
	struct ohci_state state = {0, TD_NOTACCESSED};
	struct ohci * ohci = usb_dev->bus->hcpriv;
	struct usb_hcd_ed hcd_ed;
	struct usb_ohci_ed *ed;

#ifdef  VROOTHUB
	if(usb_pipedevice(pipe) == ohci->rh.devnum) 
		return root_hub_control_msg(usb_dev, pipe, cmd, data, len);
#endif		

	usb_pipe_to_hcd_ed(usb_dev, pipe, &hcd_ed);
	hcd_ed.type = CTRL; 	
 	
	ed = usb_ohci_add_ep(usb_dev, &hcd_ed, 0, 1);
	
 	OHCI_DEBUG( { int i; printk("USB HC CTRL>>>: ed:%x-%x: ctrl(%d):", (unsigned int) ed, ed->hwINFO, 8);)
	OHCI_DEBUG( for(i=0; i < 8; i++ ) printk(" %02x", ((__u8 *) cmd)[i]);)
	OHCI_DEBUG( printk(" data(%d):", len);) 
	OHCI_DEBUG( for(i=0; i < len; i++ ) printk(" %02x", ((__u8 *) data)[i]);)
	OHCI_DEBUG( printk("\n"); })
	current->state = TASK_UNINTERRUPTIBLE;
    
	ohci_trans_req(ohci, ed, 8, cmd, data, len, (__OHCI_BAG) &state, (__OHCI_BAG) &wait, (usb_pipeout(pipe))?CTRL_OUT:CTRL_IN, sohci_blocking_handler);

    OHCI_DEBUG(printk("USB HC trans req ed %x: %x :", ed->hwINFO, (unsigned int ) ed); )
 	OHCI_DEBUG({ int i; for( i= 0; i<8 ;i++) printk(" %4x", ((unsigned int *) ed)[i]) ; printk("\n"); }; )
 	if (ED_STATE(ed) != ED_OPER)  ohci_link_ed(ohci, ed);
 	schedule_timeout(timeout);
   
    if(state.status == TD_NOTACCESSED) {	
    	current->state = TASK_UNINTERRUPTIBLE;
  		usb_ohci_rm_ep(usb_dev, ed, sohci_blocking_handler, NULL, NULL, 0);
		schedule();
  		state.status = USB_ST_TIMEOUT;
  	}
  	remove_wait_queue(&op_wakeup, &wait); 
	return state.status;
}

static int sohci_bulk_msg(struct usb_device *usb_dev, unsigned int pipe, void *data, int len, unsigned long *rval, int timeout)
{
	DECLARE_WAITQUEUE(wait, current);
	struct ohci_state state = {0, TD_NOTACCESSED};
	struct ohci * ohci = usb_dev->bus->hcpriv;
	struct usb_hcd_ed hcd_ed;
	struct usb_ohci_ed *ed;
	
	usb_pipe_to_hcd_ed(usb_dev, pipe, &hcd_ed);
	hcd_ed.type = BULK; 
	ed = usb_ohci_add_ep(usb_dev, &hcd_ed, 0, 1);
	OHCI_DEBUG( { int i; printk("USB HC BULK>>>: %x: ", ed->hwINFO);)
	OHCI_DEBUG( printk(" data(%d):", len);) 
	OHCI_DEBUG( for(i=0; i < len; i++ ) printk(" %02x", ((__u8 *) data)[i]);)
	OHCI_DEBUG( printk("\n"); })
	current->state = TASK_UNINTERRUPTIBLE;  
 
	ohci_trans_req(ohci, ed, 0, NULL, data, len, (__OHCI_BAG) &state, (__OHCI_BAG) &wait,(usb_pipeout(pipe))?BULK_OUT:BULK_IN, sohci_blocking_handler);
	if (ED_STATE(ed) != ED_OPER)  ohci_link_ed(ohci, ed);
	
	schedule_timeout(timeout);

	if(state.status == TD_NOTACCESSED) {		
    	current->state = TASK_UNINTERRUPTIBLE;
  		usb_ohci_rm_ep(usb_dev, ed, sohci_blocking_handler, NULL, NULL, 0);
  		schedule();
  		state.status = USB_ST_TIMEOUT;
	}
	remove_wait_queue(&op_wakeup, &wait); 
    *rval = state.len;
	return state.status;
}

static void * sohci_request_bulk(struct usb_device *usb_dev, unsigned int pipe, usb_device_irq handler, void *data, int len, void *dev_id)
{
	struct ohci * ohci = usb_dev->bus->hcpriv;
	struct usb_hcd_ed hcd_ed;
	struct usb_ohci_ed * ed;
 
	usb_pipe_to_hcd_ed(usb_dev, pipe, &hcd_ed);
	hcd_ed.type = BULK;
	ed = usb_ohci_add_ep(usb_dev, &hcd_ed, 0, 1);
	
	OHCI_DEBUG( printk("USB HC BULK_RQ>>>: %x \n", ed->hwINFO);) 
	
	ohci_trans_req(ohci, ed, 0, NULL, data, len, (__OHCI_BAG) handler, (__OHCI_BAG) dev_id, (usb_pipeout(pipe))?BULK_OUT:BULK_IN, sohci_int_handler);
	if (ED_STATE(ed) != ED_OPER)  ohci_link_ed(ohci, ed);

	return ed;   
}
 
static int sohci_terminate_bulk(struct usb_device *usb_dev, void * ed) 
{
	DECLARE_WAITQUEUE(wait, current);
	
	OHCI_DEBUG( printk("USB HC TERM_BULK>>>:%4x\n", (unsigned int) ed);)
 	
	current->state = TASK_UNINTERRUPTIBLE;
    usb_ohci_rm_ep(usb_dev, (struct usb_ohci_ed *) ed, sohci_blocking_handler, NULL, &wait, SEND);
    schedule();
    remove_wait_queue(&op_wakeup, &wait); 
	return 1;
}

static int sohci_alloc_dev(struct usb_device *usb_dev)
{
	struct ohci_device *dev;

	/* Allocate the OHCI_HCD device private data */
	dev = kmalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -1;

	/* Initialize "dev" */
	memset(dev, 0, sizeof(*dev));

	usb_dev->hcpriv = dev;
	dev->usb = usb_dev;
	atomic_set(&dev->refcnt, 1);

	if (usb_dev->parent)
		dev->ohci = usb_to_ohci(usb_dev->parent)->ohci;

	return 0;
}

static int sohci_free_dev(struct usb_device *usb_dev)
{
	int cnt;
    DECLARE_WAITQUEUE(wait, current);
	struct ohci_device *dev = usb_to_ohci(usb_dev);

	OHCI_DEBUG(printk("USB HC ***** free %x\n", usb_dev->devnum);)

	if(usb_dev->devnum >= 0) {
		current->state = TASK_UNINTERRUPTIBLE;
    	cnt = usb_ohci_rm_function(usb_dev, sohci_blocking_handler, NULL, &wait);
    	if(cnt > 0) {
    		schedule();
    		remove_wait_queue(&op_wakeup, &wait);
    	}
    	current->state = TASK_INTERRUPTIBLE;
	}

if (atomic_dec_and_test(&dev->refcnt))
		kfree(dev);

	return 0;
}

 

/*
 * ISO Interface designed by Randy Dunlap
 */

static int sohci_get_current_frame_number(struct usb_device *usb_dev) {

	struct ohci * ohci = usb_dev->bus->hcpriv;
	
	return readl(&ohci->regs->fmnumber) & 0xffff;
}


static int sohci_init_isoc(struct usb_device *usb_dev, unsigned int pipe, int frame_count, void *context, struct usb_isoc_desc **idp) {

	struct usb_isoc_desc *id;
	
	*idp = NULL;

	id = kmalloc (sizeof (struct usb_isoc_desc) + (sizeof (struct isoc_frame_desc) * frame_count), GFP_KERNEL);
	if(!id) return -ENOMEM;
	memset (id, 0, sizeof (struct usb_isoc_desc) + (sizeof (struct isoc_frame_desc) * frame_count));
OHCI_DEBUG(printk("ISO alloc id: %p, size: %d\n", id, sizeof (struct usb_isoc_desc) + (sizeof (struct isoc_frame_desc) * frame_count));)
	id->td = kmalloc (sizeof (void *) * frame_count, GFP_KERNEL);
	if(!id->td) {
		kfree (id);
		return -ENOMEM;
	}		
	memset (id->td, 0, sizeof (void *) * frame_count);
OHCI_DEBUG(printk("ISO alloc id->td: %p, size: %d\n", id->td, sizeof (void *) * frame_count);)
	
	id->frame_count = frame_count;
	id->frame_size  = usb_maxpacket (usb_dev, pipe, usb_pipeout(pipe));
	id->start_frame = -1;
	id->end_frame   = -1;
	id->usb_dev     = usb_dev;
	id->pipe        = pipe;
	id->context     = context;

	*idp = id;
	return 0;
} 

void print_int_eds(struct ohci * ohci);

static int sohci_run_isoc(struct usb_isoc_desc *id, struct usb_isoc_desc *pr_id) {

	struct ohci * ohci = id->usb_dev->bus->hcpriv;
	struct usb_ohci_td  ** tdp = (struct usb_ohci_td  **) id->td;
	struct usb_hcd_ed   hcd_ed;
	struct usb_ohci_ed  * ed;
	int	ix, frlen;
	unsigned char	*bufptr;

	if(pr_id)
		id->start_frame = pr_id->end_frame + 1;		
	else {
		switch(id->start_type) {
			case START_ABSOLUTE:
				break;
				
			case START_RELATIVE:
				if(id->start_frame < START_FRAME_FUDGE) id->start_frame = START_FRAME_FUDGE;
				id->start_frame += sohci_get_current_frame_number(id->usb_dev);
				break;
				
			case START_ASAP:
				id->start_frame = START_FRAME_FUDGE + sohci_get_current_frame_number(id->usb_dev);
				break;
		}
	}

	id->start_frame &= 0xffff;	
	id->end_frame = (id->start_frame + id->frame_count - 1) & 0xffff;

	id->prev_completed_frame = 0;
	id->cur_completed_frame  = 0;
	if (id->frame_spacing <= 0) id->frame_spacing = 1;
	
	bufptr = id->data;
	
	usb_pipe_to_hcd_ed(id->usb_dev, id->pipe, &hcd_ed);
	hcd_ed.type = ISO;
	ed = usb_ohci_add_ep(id->usb_dev, &hcd_ed, 1, 1);
	OHCI_DEBUG( printk("USB HC ISO>>>: ed: %x-%x: (%d tds)\n", (unsigned int) ed, ed->hwINFO, id->frame_count);) 
	 
	for (ix = 0; ix < id->frame_count; ix++) {
		frlen = (id->frames[ix].frame_length > id->frame_size)? id->frame_size : id->frames[ix].frame_length;	
		printk("ISO run id->td: %p \n", &tdp[ix]);
		tdp[ix] = ohci_trans_req(ohci, ed, id->start_frame + ix , NULL, bufptr, frlen, (__OHCI_BAG) ix, (__OHCI_BAG) id, 
						(usb_pipeout(id->pipe))?ISO_OUT:ISO_IN, 
						sohci_iso_handler);
		bufptr += frlen;
	}
	
	if (ED_STATE(ed) != ED_OPER)  ohci_link_ed(ohci, ed);
	print_int_eds(ohci);
	return 0;
}

static int sohci_kill_isoc(struct usb_isoc_desc *id) {

	struct usb_ohci_ed	*ed = NULL;
	struct usb_ohci_td	**td = id->td;
	int i;
printk("KILL_ISOC***:\n");	
	for (i = 0; i < id->frame_count; i++) {
		if(td[i]) {
			td[i]->type |= DEL;
			ed = td[i]->ed; printk(" %d", i);	
		}
	} 
	if(ed) usb_ohci_rm_ep(id->usb_dev, ed, NULL, NULL, NULL, TD_RM);
printk(": end KILL_ISOC***: %p\n", ed);		
	id->start_frame = -1;
	return 0;
}


static void sohci_free_isoc(struct usb_isoc_desc *id) {
printk("FREE_ISOC***\n");
wait_ms(2000);
	if(id->start_frame >= 0) sohci_kill_isoc(id);
printk("FREE_ISOC2***\n");
wait_ms(2000);
	kfree(id->td);
	kfree(id);
printk("FREE_ISOC3***\n");
wait_ms(2000);
}

struct usb_operations sohci_device_operations = {
	sohci_alloc_dev,
	sohci_free_dev,
	sohci_control_msg,
	sohci_bulk_msg,
	sohci_request_irq,
	sohci_release_irq,
	sohci_request_bulk,
	sohci_terminate_bulk,
	sohci_get_current_frame_number,
	sohci_init_isoc,
	sohci_free_isoc,
	sohci_run_isoc,
	sohci_kill_isoc
};

 
/******
 *** ED handling functions
 ************************************/

/* just for debugging; prints all 32 branches of the int ed tree inclusive iso eds*/
void print_int_eds(struct ohci * ohci) {int i; __u32 * ed_p;
	for(i= 0; i < 32; i++) {
			OHCI_DEBUG(printk("branch int %2d(%2x): ", i,i); )
			ed_p = &(ohci->hc_area->hcca.int_table[i]);
			while (*ed_p != 0) {
				OHCI_DEBUG(printk("ed: %4x; ", (((struct usb_ohci_ed *)bus_to_virt(*ed_p))->hwINFO));)
				ed_p = &(((struct usb_ohci_ed *)bus_to_virt(*ed_p))->hwNextED);
			}
			OHCI_DEBUG(printk("\n");)
		}
}
		
/* 
 * search for the right branch to insert an interrupt ed into the int tree 
 * do some load ballancing 
 * returns the branch and 
 * sets the interval to interval = 2^integer(ld(interval))
 * */

static int usb_ohci_int_ballance(struct ohci * ohci, int * interval, int load) {

  int i,j;
  
  j = 0;   /* search for the least loaded interrupt endpoint branch of all 32 branches */
  for(i=0; i< 32; i++) if(ohci->ohci_int_load[j] > ohci->ohci_int_load[i]) j=i; 
  
  for(i= 0; ((*interval >> i) > 1 ) && (i < 5); i++ ); /* interval = 2^int(ld(interval)) */
  *interval = 1 << i;
  
  j = j % (*interval);
  for(i=j; i< 32; i+=(*interval)) ohci->ohci_int_load[i] += load;
   
  
  OHCI_DEBUG(printk("USB HC new int ed on pos %d of interval %d \n",j, *interval);)
  
  return j;
}

/* the int tree is a binary tree 
 * in order to process it sequentially the indexes of the branches have to be mapped 
 * the mapping reverses the bits of a word of num_bits length
 * */
static int rev(int num_bits, int word) {
	int i;
	int wout = 0;

	for(i = 0; i < num_bits; i++) wout |= (((word >> i) & 1) << (num_bits - i - 1));
	return wout;
}

/* get the ed from the endpoint / usb_device address */

struct usb_ohci_ed * ohci_find_ep(struct usb_device *usb_dev, struct usb_hcd_ed *hcd_ed) {
 	return &(usb_to_ohci(usb_dev)->ed[(hcd_ed->endpoint << 1) | ((hcd_ed->type == CTRL)? 0:hcd_ed->out)]);
}

/* link an ed into one of the HC chains */
static int ohci_link_ed(struct ohci * ohci, struct usb_ohci_ed *ed) {	 

	int int_branch;
	int i;
	int inter;
	int interval;
	int load;
	__u32 * ed_p;
	
	ED_setSTATE(ed, ED_OPER);
	
	switch(ED_TYPE(ed)) {
	case CTRL:
		ed->hwNextED = 0;
		if(ohci->ed_controltail == NULL) {
			writel(virt_to_bus(ed), &ohci->regs->ed_controlhead);
		}
		else {
			ohci->ed_controltail->hwNextED = virt_to_bus(ed);
		}
		ed->ed_prev = ohci->ed_controltail;
		ohci->ed_controltail = ed;	  
		break;
		
	case BULK:  
		ed->hwNextED = 0;
		if(ohci->ed_bulktail == NULL) {
			writel(virt_to_bus(ed), &ohci->regs->ed_bulkhead);
		}
		else {
			ohci->ed_bulktail->hwNextED = virt_to_bus(ed);
		}
		ed->ed_prev = ohci->ed_bulktail;
		ohci->ed_bulktail = ed;	  
		break;
		
	case INT:
		interval = ed->int_period;
		load = ed->int_load;
		int_branch = usb_ohci_int_ballance(ohci, &interval, load);
		ed->int_interval = interval;
		ed->int_branch = int_branch;
		
		for( i= 0; i < rev(6, interval); i += inter) {
			inter = 1;
			for(ed_p = &(ohci->hc_area->hcca.int_table[rev(5,i)+int_branch]); 
				(*ed_p != 0) && (((struct usb_ohci_ed *)bus_to_virt(*ed_p))->int_interval >= interval); 
				ed_p = &(((struct usb_ohci_ed *)bus_to_virt(*ed_p))->hwNextED)) 
					inter = rev(6,((struct usb_ohci_ed *)bus_to_virt(*ed_p))->int_interval);
			ed->hwNextED = *ed_p; 
			*ed_p = virt_to_bus(ed);
			OHCI_DEBUG(printk("int_link i: %2x, inter: %2x, ed: %4x\n", i, inter, ed->hwINFO);)
		}
		break;
		
	case ISO:
		if(ohci->ed_isotail != NULL) {
			ohci->ed_isotail->hwNextED = virt_to_bus(ed);
			ed->ed_prev = ohci->ed_isotail;
		}
		else {
			for( i= 0; i < 32; i += inter) {
				inter = 1;
				for(ed_p = &(ohci->hc_area->hcca.int_table[rev(5,i)]); 
					*ed_p != 0; 
					ed_p = &(((struct usb_ohci_ed *)bus_to_virt(*ed_p))->hwNextED)) 
						inter = rev(6,((struct usb_ohci_ed *)bus_to_virt(*ed_p))->int_interval);
				*ed_p = virt_to_bus(ed);	
			}	
			ed->ed_prev = NULL;
		}	
		ed->hwNextED = 0;
		ohci->ed_isotail = ed;  
		break;
	}
	
   	
	return 1;
}

/* unlink an ed from one of the HC chains. 
 * just the link to the ed is unlinked.
 * the link from the ed still points to another operational ed or 0
 * so the HC can eventually finish the processing of the unlinked ed
 * */
static int ohci_unlink_ed(struct ohci * ohci, struct usb_ohci_ed *ed) {

	int int_branch;
	int i;
	int inter;
	int interval;
	__u32 * ed_p;
	 
    
    switch(ED_TYPE(ed)) {
    case CTRL: 
      if(ed->ed_prev == NULL) {
		writel(ed->hwNextED, &ohci->regs->ed_controlhead);
      }
      else {
		ed->ed_prev->hwNextED = ed->hwNextED;
      }
      if(ohci->ed_controltail == ed) {
		ohci->ed_controltail = ed->ed_prev;
      }
      else {
      	((struct usb_ohci_ed *)bus_to_virt(ed->hwNextED))->ed_prev = ed->ed_prev;
      }
      break;
      
    case BULK: 
      if(ed->ed_prev == NULL) {
		writel(ed->hwNextED, &ohci->regs->ed_bulkhead);
      }
      else {
		ed->ed_prev->hwNextED = ed->hwNextED;
      }
      if(ohci->ed_bulktail == ed) {
		ohci->ed_bulktail = ed->ed_prev;
      }
      else {
      	((struct usb_ohci_ed *)bus_to_virt(ed->hwNextED))->ed_prev = ed->ed_prev;
      }
      break;
      
    case INT: 
      	int_branch = ed->int_branch;
		interval = ed->int_interval;

		for( i= 0; i < rev(6,interval); i += inter) {
			for(ed_p = &(ohci->hc_area->hcca.int_table[rev(5,i)+int_branch]), inter = 1; 
				(*ed_p != 0) && (*ed_p != ed->hwNextED); 
				ed_p = &(((struct usb_ohci_ed *)bus_to_virt(*ed_p))->hwNextED), 
				inter = rev(6, ((struct usb_ohci_ed *)bus_to_virt(*ed_p))->int_interval)) {				
					if(((struct usb_ohci_ed *)bus_to_virt(*ed_p)) == ed) {
			  			*ed_p = ed->hwNextED;		
			  			break;
			  		}
			  }
			  OHCI_DEBUG(printk("int_link i: %2x, inter: %2x, ed: %4x\n", i, inter, ed->hwINFO);)
		}
		for(i=int_branch; i< 32; i+=interval) ohci->ohci_int_load[i] -= ed->int_load;
		break;
		
    case ISO:
    	if(ohci->ed_isotail == ed)
				ohci->ed_isotail = ed->ed_prev;
		if(ed->hwNextED != 0) 
				((struct usb_ohci_ed *)bus_to_virt(ed->hwNextED))->ed_prev = ed->ed_prev;
				
		if(ed->ed_prev != NULL) {
			ed->ed_prev->hwNextED = ed->hwNextED;
		}	
		else {
			for( i= 0; i < 32; i += inter) {
				inter = 1;
				for(ed_p = &(ohci->hc_area->hcca.int_table[rev(5,i)]); 
					*ed_p != 0; 
					ed_p = &(((struct usb_ohci_ed *)bus_to_virt(*ed_p))->hwNextED)) {
						inter = rev(6,((struct usb_ohci_ed *)bus_to_virt(*ed_p))->int_interval);
						if(((struct usb_ohci_ed *)bus_to_virt(*ed_p)) == ed) {
			  				*ed_p = ed->hwNextED;		
			  				break;
			  			}
			  	}
			}	
		}	
		break;
    }
    ED_setSTATE(ed, ED_UNLINK);
    return 1;
}

spinlock_t usb_ed_lock = SPIN_LOCK_UNLOCKED;

/* add/reinit an endpoint; this should be done once at the usb_set_configuration command,
 * but the USB stack is a little bit stateless  so we do it at every transaction
 * if the state of the ed is ED_NEW then a dummy td is added and the state is changed to ED_UNLINK
 * in all other cases the state is left unchanged
 * the ed info fields are setted anyway even though they should not change
 * */
struct usb_ohci_ed *usb_ohci_add_ep(struct usb_device * usb_dev, struct usb_hcd_ed * hcd_ed, int interval, int load) {

   	// struct ohci * ohci = usb_dev->bus->hcpriv;
	struct usb_ohci_td * td;  
	struct usb_ohci_ed * ed, *ed1; 
	 
 	int ed_state, ed_state1;
 
	spin_lock(&usb_ed_lock);

	ed = ohci_find_ep(usb_dev, hcd_ed); 

	 
	ed1 = ((void *) ed) + 0x40; ed_state1 = ED_STATE(ed1);
OHCI_DEBUG(printk("++++ USB HC add 60 ed1 %x: %x :state: %x\n", ed1->hwINFO, (unsigned int ) ed1, ed_state1); )
	ed_state = ED_STATE(ed); /* store state of ed */
	OHCI_DEBUG(printk("USB HC add ed %x: %x :state: %x\n", ed->hwINFO, (unsigned int ) ed, ed_state); )
	if (ed_state == ED_NEW) {
  		OHCI_ALLOC(td, sizeof(*td)); /* dummy td; end of td list for ed */
		ed->hwTailP = virt_to_bus(td);
		ed->hwHeadP = ed->hwTailP;	
		ed_state = 	ED_UNLINK;
	}
	
    ed->hwINFO = hcd_ed->function 
 			| hcd_ed->endpoint << 7
    		| (hcd_ed->type == ISO? 0x8000 : 0)
    		| (hcd_ed->type == CTRL? 0:(hcd_ed->out == 1? 0x800 : 0x1000 )) 
    		| hcd_ed->slow << 13
    		| hcd_ed->maxpack << 16
    		| hcd_ed->type << 27
    //		| 1 << 14
    		| ed_state << 29 ;
 
  	if (ED_TYPE(ed) == INT && ed_state == ED_UNLINK) {
  		ed->int_period = interval;
  		ed->int_load = load;
  	}
  	
	spin_unlock(&usb_ed_lock);
	return ed; 
}


 
/*****
 * Request the removal of an endpoint
 * 
 * put the ep on the rm_list and request a stop of the bulk or ctrl list 
 * real removal is done at the next start of frame (SOF) hardware interrupt 
 * the dummy td carries the essential information (handler, proc queue, ...)
 * if(send & TD_RM) then just the TD witch have (TD->type & DEL) set will be removed
 * otherwise all TDs including the dummy TD of the ED will be removed
 */
int usb_ohci_rm_ep(struct usb_device * usb_dev, struct usb_ohci_ed *ed, f_handler handler, __OHCI_BAG lw0, __OHCI_BAG lw1, int send)
{    
	unsigned int flags;
   
	struct ohci * ohci = usb_dev->bus->hcpriv;
	struct  usb_ohci_td *td; 
  
	OHCI_DEBUG(printk("USB HC remove ed %x: %x :\n", ed->hwINFO, (unsigned int ) ed); )

	spin_lock_irqsave(&usb_ed_lock, flags);
	ed->hwINFO  |=  OHCI_ED_SKIP;
	writel( OHCI_INTR_SF, &ohci->regs->intrenable); /* enable sof interrupt */


	if(send & TD_RM) { /* delete selected TDs */
		ED_setSTATE(ed, ED_TD_DEL);
	}
	else { /* delete all TDS */
		if(ED_STATE(ed) == ED_OPER) ohci_unlink_ed(ohci, ed);
		td = (struct usb_ohci_td *) bus_to_virt(ed->hwTailP);
		td->lw0 = lw0;
		td->lw1 = lw1;
		td->ed = ed;
		td->hwINFO = TD_CC;
		td->handler = handler;
		td->type = send; 
		ED_setSTATE(ed, ED_DEL);
	}
	ed->ed_prev = ohci->ed_rm_list;
	ohci->ed_rm_list = ed;
   
	switch(ED_TYPE(ed)) {
		case CTRL:
			writel_mask(~(0x01<<4), &ohci->regs->control); /* stop CTRL list */
  			break;
		case BULK:
			writel_mask(~(0x01<<5), &ohci->regs->control); /* stop BULK list */
			break;
	}

	spin_unlock_irqrestore(&usb_ed_lock, flags);

	return 1;
}

 
/* remove all endpoints of a function (device) 
 * just the last ed sends a reply
 * the last ed is ed0 as there always should be an ep0
 * */
int usb_ohci_rm_function(struct usb_device * usb_dev, f_handler handler,__OHCI_BAG tw0, __OHCI_BAG tw1)
{   
	struct usb_ohci_ed *ed;
	int i;
	int cnt = 0;
	
	
  	for(i = NUM_EDS - 1 ; i >= 0; i--) {
  		ed = &(usb_to_ohci(usb_dev)->ed[i]);
  		if(ED_STATE(ed) != ED_NEW) {
  		OHCI_DEBUG(printk("USB RM FUNCTION ed: %4x;\n", ed->hwINFO);)
  			usb_ohci_rm_ep(usb_dev, ed, handler, tw0, tw1, i==0?SEND:0);
  			cnt++;
  		}
  	}
  	OHCI_DEBUG(printk("USB RM FUNCTION %d eds removed;\n", cnt);)
  	return cnt;
}



/******
 *** TD handling functions
 ************************************/


#define FILL_TD(INFO, DATA, LEN, LW0, LW1, TYPE, HANDLER)  \
	OHCI_ALLOC(td_pt, sizeof(*td_pt)); \
    td_ret = (struct usb_ohci_td *) bus_to_virt(usb_ed->hwTailP & 0xfffffff0); \
    td_pt1 = td_ret; \
    td_pt1->ed = ed; \
    td_pt1->buffer_start = (DATA); \
    td_pt1->type = (TYPE); \
    td_pt1->handler = (HANDLER); \
    td_pt1->lw0 = (LW0); \
    td_pt1->lw1 = (LW1); \
    td_pt1->hwINFO = (INFO); \
    td_pt1->hwCBP = (((DATA)==NULL)||((LEN)==0))?0:virt_to_bus(DATA); \
    td_pt1->hwBE = (((DATA)==NULL)||((LEN)==0))?0:virt_to_bus((DATA) + (LEN) - 1); \
    td_pt1->hwNextTD = virt_to_bus(td_pt); \
    td_pt->hwNextTD = 0; \
    usb_ed->hwTailP =  td_pt1->hwNextTD

#define FILL_ISO_TD(INFO, DATA, LEN, LW0, LW1, TYPE, HANDLER)  \
	OHCI_ALLOC(td_pt, sizeof(*td_pt)); \
    td_ret = (struct usb_ohci_td *) bus_to_virt(usb_ed->hwTailP & 0xfffffff0); \
    td_pt1 = td_ret; \
    td_pt1->ed = ed; \
    td_pt1->buffer_start = (DATA); \
    td_pt1->type = (TYPE); \
    td_pt1->handler = (HANDLER); \
    td_pt1->lw0 = (LW0); \
    td_pt1->lw1 = (LW1); \
    td_pt1->hwINFO = (INFO); \
    td_pt1->hwCBP = (((DATA)==NULL)||((LEN)==0))?0:(virt_to_bus(DATA) & 0xfffff000); \
    td_pt1->hwBE = (((DATA)==NULL)||((LEN)==0))?0:virt_to_bus((DATA) + (LEN) - 1); \
    td_pt1->hwNextTD = virt_to_bus(td_pt); \
    td_pt1->hwPSW[0] = (virt_to_bus(DATA) & 0xfff) | 0xe000; \
    td_pt->hwNextTD = 0; \
    usb_ed->hwTailP =  td_pt1->hwNextTD
    
    
spinlock_t usb_req_lock = SPIN_LOCK_UNLOCKED;

struct usb_ohci_td * ohci_trans_req(struct ohci * ohci, struct usb_ohci_ed * ed, int ctrl_len, 
			void  *ctrl, void * data, int data_len, __OHCI_BAG lw0, __OHCI_BAG lw1, unsigned int ed_type, f_handler handler) {

   
  unsigned int flags;
  volatile struct usb_ohci_ed *usb_ed = ed;
  volatile struct usb_ohci_td *td_pt;
  volatile struct usb_ohci_td *td_pt1 = NULL;

	struct usb_ohci_td *td_ret = NULL;
	
  int cnt = 0;
 
   
  if(usb_ed == NULL ) return NULL; /* not known ep */
  
  
  spin_lock_irqsave(&usb_req_lock, flags);
  
  switch(ed_type) {
  case BULK_IN: 
  	while(data_len > 4096)
	{	
		FILL_TD( TD_CC | TD_DP_IN | TD_T_TOGGLE, data, 4096, NULL, NULL, BULK_IN | ADD_LEN|(cnt++?0:ST_ADDR), NULL);
		data += 4096; data_len -= 4096;
	}
    FILL_TD( TD_CC | TD_R | TD_DP_IN | TD_T_TOGGLE, data, data_len, lw0, lw1, BULK_IN |ADD_LEN|SEND|(cnt++?0:ST_ADDR), handler);
    writel( OHCI_BLF, &ohci->regs->cmdstatus); /* start bulk list */
    break;

  case BULK_OUT: 
  	while(data_len > 4096)
	{
		FILL_TD( TD_CC | TD_DP_OUT | TD_T_TOGGLE, data, 4096, NULL, NULL, BULK_OUT | ADD_LEN|(cnt++?0:ST_ADDR), NULL);
		data += 4096; data_len -= 4096;
	}
    FILL_TD( TD_CC | TD_DP_OUT | TD_T_TOGGLE, data, data_len, lw0, lw1, BULK_OUT |ADD_LEN|SEND|(cnt++?0:ST_ADDR), handler);
    writel( OHCI_BLF, &ohci->regs->cmdstatus); /* start bulk list */
    break;

  case INT_IN: 
    FILL_TD( TD_CC | TD_R | TD_DP_IN | TD_T_TOGGLE, data, data_len, lw0, lw1, INT_IN | ST_ADDR|ADD_LEN|SEND, handler);  
    break;

  case INT_OUT: 
    FILL_TD( TD_CC | TD_DP_OUT | TD_T_TOGGLE, data, data_len, lw0, lw1, INT_OUT | ST_ADDR|ADD_LEN|SEND, handler);
    break;

  case CTRL_IN:
    FILL_TD( TD_CC | TD_DP_SETUP | TD_T_DATA0, ctrl, ctrl_len, 0, 0, CTRL_SETUP |ST_ADDR, NULL);  
    if(data_len > 0) {  
      	FILL_TD( TD_CC | TD_R |  TD_DP_IN | TD_T_DATA1, data, data_len, 0, 0, CTRL_DATA_IN | ST_ADDR|ADD_LEN, NULL);  
    } 
    FILL_TD( TD_CC | TD_DP_OUT | TD_T_DATA1, NULL, 0, lw0, lw1, CTRL_STATUS_OUT |SEND, handler);
    writel( OHCI_CLF, &ohci->regs->cmdstatus); /* start Control list */
    break;

  case CTRL_OUT:
    FILL_TD( TD_CC | TD_DP_SETUP | TD_T_DATA0, ctrl, ctrl_len, 0, 0, CTRL_SETUP |ST_ADDR, NULL);  
    if(data_len > 0) {
      	FILL_TD( TD_CC | TD_R | TD_DP_OUT | TD_T_DATA1, data, data_len, 0, 0, CTRL_DATA_OUT | ST_ADDR|ADD_LEN, NULL);  
    }  
    FILL_TD( TD_CC | TD_DP_IN | TD_T_DATA1, NULL, 0, lw0, lw1, CTRL_STATUS_IN |SEND, handler);
    writel( OHCI_CLF, &ohci->regs->cmdstatus); /* start Control list */
    break;

  case ISO_IN:
   	FILL_ISO_TD( TD_CC|TD_ISO|(ctrl_len & 0xffff), data, data_len, lw0, lw1, ISO_IN | ST_ADDR|ADD_LEN|SEND, handler); 
   	break;
   	
  case ISO_OUT:
  	FILL_ISO_TD( TD_CC|TD_ISO|(ctrl_len & 0xffff), data, data_len, lw0, lw1, ISO_OUT | ST_ADDR|ADD_LEN|SEND, handler); 
    break;
  }
  
  spin_unlock_irqrestore(&usb_req_lock, flags); 
  return td_ret;
}


/******
 *** Done List handling functions
 ************************************/
/* the HC halts an ED if an error occurs;  
 * on error move all pending tds of a transaction (from ed) onto the done list 
 * * */
static struct usb_ohci_td * ohci_append_error_tds(struct usb_ohci_td * td_list, struct usb_ohci_td * td_rev) {

	struct usb_ohci_td * tdl;
	struct usb_ohci_td * tdx;
	struct usb_ohci_td * tdt;
	int cc;

	tdl = (struct  usb_ohci_td *) bus_to_virt( td_list->ed->hwHeadP & 0xfffffff0);
	tdt = (struct  usb_ohci_td *) bus_to_virt( td_list->ed->hwTailP);
	cc = TD_CC_GET(td_list->hwINFO);
	
	for	( tdx = tdl; tdx != tdt; tdx = tdx->next_dl_td) {
		if(tdx->type & SEND) break;
		tdx->next_dl_td = bus_to_virt(tdx->hwNextTD & 0xfffffff0);	
	}
	tdx->next_dl_td = td_rev;
	td_list->ed->hwHeadP = (tdx->hwNextTD & 0xfffffff0) | (td_list->ed->hwHeadP & 0x2);
	TD_CC_SET(tdx->hwINFO, cc);
	return tdl;
}

/* replies to the request have to be on a FIFO basis so
 * we reverse the reversed done-list */
 
static struct usb_ohci_td * ohci_reverse_done_list(struct ohci * ohci) {

	__u32 td_list_hc;
	struct usb_ohci_td * td_rev = NULL;
	struct usb_ohci_td * td_list = NULL;
  	
	td_list_hc = ohci->hc_area->hcca.done_head & 0xfffffff0;
	ohci->hc_area->hcca.done_head = 0;

 
	while(td_list_hc) {
		
		td_list = (struct  usb_ohci_td *) bus_to_virt(td_list_hc);
		if(TD_CC_GET(td_list->hwINFO) && !(td_list->type & SEND)) 
			td_list->next_dl_td = ohci_append_error_tds(td_list, td_rev);
		else
			td_list->next_dl_td = td_rev;
			
		td_rev = td_list;
		td_list_hc = td_list->hwNextTD & 0xfffffff0;
		
		
	}	
 return td_list;
}

/* there are some pending requests to remove some of the eds 
 * we either process every td including the dummy td of these eds 
 * or just those marked with TD->type&DEL
 * and link them to a list
 * */
static struct usb_ohci_td * usb_ohci_del_list(struct ohci *  ohci) {
	struct usb_ohci_ed * ed;
	struct usb_ohci_td * td;
	struct usb_ohci_td * td_tmp = NULL;
	struct usb_ohci_td * td_list = NULL;
	__u32 * td_hw; 
	
	for(ed = ohci->ed_rm_list; ed != NULL; ed = ed->ed_prev) {
		 OHCI_DEBUG(printk("USB HC ed_rm_list: %4x :\n", ed->hwINFO);)
		  
  			
		for( td_hw = &(ed->hwHeadP); (*td_hw & 0xfffffff0) != ed->hwTailP; td_hw = &(td->hwNextTD)) {
				td = (struct usb_ohci_td *)bus_to_virt(*td_hw & 0xfffffff0);
   	 	 		if((ED_STATE(ed) == ED_DEL)	|| (td->type & DEL)) {
   	 	 			*td_hw = td->hwNextTD;
   	 	 			if(td_list == NULL)
   	 	 				td_list = td; 
   	 	 			else
   	 	 				td_tmp->next_dl_td = td;
   	 	 			td_tmp = td;
   	 	 			OHCI_DEBUG(printk("USB HC ed_rm_list: td: %4x\n", (unsigned int) td);)
   	 	 			td->next_dl_td = NULL; 
   	 	 		}
   	 	}
   	 	if(ED_STATE(ed) == ED_DEL) { /* send dummy td */
   	 		td = (struct usb_ohci_td *)bus_to_virt(*td_hw & 0xfffffff0);
   	 		td->ed = ed;
   	 		if(td_list == NULL)
   	 	 		td_list = td; 
   	 	 	else
   	 	 		td_tmp->next_dl_td = td; 
   	 	 	td_tmp = td;
   	 	 	td->type |= DEL_ED;
   	 	 	OHCI_DEBUG(printk("USB HC ed_rm_list: dummy (ED_DEL) td: %4x\n", (unsigned int) td);)
   	 	 	ED_setSTATE(td->ed, ED_DEL| ED_NEW);
   	 	 	td->next_dl_td = NULL;
   	 	}	  	
   	 	if(ED_TYPE(ed) == CTRL) writel(0, &ohci->regs->ed_controlcurrent); /* reset CTRL list */
  		if(ED_TYPE(ed) == BULK) writel(0, &ohci->regs->ed_bulkcurrent);    /* reset BULK list */

   	}  
   	  
  	writel_set((0x03<<4), &ohci->regs->control);   /* start CTRL u. BULK list */ 

   	ohci->ed_rm_list = NULL;
   	
 
   	return td_list;
}


/* all tds ever alive go through this loop
 *  requests are replied here 
 *  the handler is the 
 *  interface handler (blocking/non blocking) the real reply-handler 
 *  is called in the non blocking interface routine
 * */
static int usb_ohci_done_list(struct ohci * ohci, struct usb_ohci_td * td_list) {

  	struct usb_ohci_td      * td_list_next = NULL;

	int cc;
	int i;
 
  	while(td_list) {
   		td_list_next = td_list->next_dl_td;
  		 
  		if(td_list->type & ST_ADDR) { /* remember start address of data buffer */
 			td_list->ed->buffer_start = td_list->buffer_start;
 			td_list->ed->len = 0;
 		}
 				
 		if(td_list->type & ADD_LEN) { /* accumulate length of multi td transfers */
 			if(td_list->hwINFO & TD_ISO) {
				for(i= 0; i <= ((td_list->hwINFO >> 24) & 0x7); i++) 
					if((td_list->hwPSW[i] >> 12) < 0xE)  td_list->ed->len += (td_list->hwPSW[i] & 0x3ff);
 			}
 			else {
 				if(td_list->hwBE != 0) {
 					if(td_list->hwCBP == 0)
  			    		td_list->ed->len += (bus_to_virt(td_list->hwBE) - td_list->buffer_start + 1);
  					else
  			    		td_list->ed->len += (bus_to_virt(td_list->hwCBP) - td_list->buffer_start);
  			    }
  			}
  		}
  		/* error code of transfer */
  		cc = (ED_STATE(td_list->ed) == ED_DEL || (td_list->type & DEL))? USB_ST_REMOVED : (USB_ST_CRC*TD_CC_GET(td_list->hwINFO));

  	 	if(td_list->type & DEL_ED) ED_setSTATE(td_list->ed, ED_NEW); /* remove ed */
 		
  		if((td_list->type & SEND) && (ED_STATE(td_list->ed) != ED_STOP) && (td_list->handler)) {  /*  send the reply  */	
  			td_list->handler((void *) ohci,
						td_list->ed,				  
						td_list->ed->buffer_start, 
						td_list->ed->len,
						cc,
						td_list->lw0,
						td_list->lw1); 
			OHCI_DEBUG(if(cc != TD_CC_NOERROR) printk("******* USB BUS error %x @ep %x\n", TD_CC_GET(td_list->hwINFO), td_list->ed->hwINFO);)		
			
		}
		if(ED_STATE(td_list->ed) == ED_TD_DEL) td_list->ed->hwINFO &= ~OHCI_ED_SKIP;
		
     	if(((td_list->ed->hwHeadP & 0xfffffff0) == td_list->ed->hwTailP) && (ED_STATE(td_list->ed) > ED_UNLINK)) 
     		ohci_unlink_ed(ohci, td_list->ed); 		/* unlink eds if they are not busy */
      	
    	OHCI_FREE(td_list);
    	td_list = td_list_next;
  	}  
 	return 0; 
}



/******
 *** HC functions
 ************************************/
 

/* reset the HC not the BUS */
void reset_hc(struct ohci *ohci) {

	int timeout = 30;
	int smm_timeout = 50; /* 0,5 sec */
	 	
	if(readl(&ohci->regs->control) & 0x100) { /* SMM owns the HC */
		writel(0x08,  &ohci->regs->cmdstatus); /* request ownership */
		printk("USB HC TakeOver from SMM\n");
		while(readl(&ohci->regs->control) & 0x100) {
			wait_ms(10);
			if (--smm_timeout == 0) {
				printk("USB HC TakeOver failed!\n");
				break;
			}
		}
	}	
		
	writel((1<<31), &ohci->regs->intrdisable); /* Disable HC interrupts */
	OHCI_DEBUG(printk("USB HC reset_hc: %x ; \n", readl(&ohci->regs->control));)
	
	writel(1,  &ohci->regs->cmdstatus);	   /* HC Reset */
	while ((readl(&ohci->regs->cmdstatus) & 0x01) != 0) { /* 10us Reset */
		if (--timeout == 0) {
			printk("USB HC reset timed out!\n");
			return;
		}	
		udelay(1);
	}	 
}
static struct ohci *__ohci;

/*
 * Start an OHCI controller, set the BUS operational
 * set and enable interrupts 
 * connect the virtual root hub
 * * */

int start_hc(struct ohci *ohci)
{
    unsigned int mask;
  	int fminterval;
  	
	/*
	 * Tell the controller where the control and bulk lists are
	 * The lists are empty now.
	 */	
	writel(0, &ohci->regs->ed_controlhead);
	writel(0, &ohci->regs->ed_bulkhead);
	
	writel(virt_to_bus(&ohci->hc_area->hcca), &ohci->regs->hcca); /* a reset clears this */
   
	writel((0xBF), &ohci->regs->control); /* USB Operational */
 		
  	fminterval = 0x2edf;
	writel(((fminterval)*9)/10, &ohci->regs->periodicstart);
	fminterval |= ((((fminterval -210) * 6)/7)<<16); 
	writel(fminterval, &ohci->regs->fminterval);	
	writel(0x628, &ohci->regs->lsthresh);
	OHCI_DEBUG(printk("USB HC fminterval: %x \n", readl( &(ohci->regs->fminterval) )); )
	OHCI_DEBUG(printk("USB HC periodicstart: %x \n", readl( &(ohci->regs->periodicstart) )); )
	OHCI_DEBUG(printk("USB HC lsthresh: %x \n", readl( &(ohci->regs->lsthresh) )); )
	OHCI_DEBUG(printk("USB HC control: %x\n", readl(&ohci->regs->control)); )
 	OHCI_DEBUG(printk("USB HC roothubstata: %x \n", readl( &(ohci->regs->roothub.a) )); )
 	OHCI_DEBUG(printk("USB HC roothubstatb: %x \n", readl( &(ohci->regs->roothub.b) )); )
 	OHCI_DEBUG(printk("USB HC roothubstatu: %x \n", readl( &(ohci->regs->roothub.status) )); )
 	OHCI_DEBUG(printk("USB HC roothubstat1: %x \n", readl( &(ohci->regs->roothub.portstatus[0]) )); )
   	OHCI_DEBUG(printk("USB HC roothubstat2: %x \n", readl( &(ohci->regs->roothub.portstatus[1]) )); )
	/* Choose the interrupts we care about now, others later on demand */
	mask = OHCI_INTR_MIE | OHCI_INTR_WDH | OHCI_INTR_SO;

	writel(mask, &ohci->regs->intrenable);
	writel(mask, &ohci->regs->intrstatus);
 
#ifdef VROOTHUB
  {
	/* connect the virtual root hub */
	struct usb_device * usb_dev;
	struct ohci_device *dev;
 

	usb_dev = usb_alloc_dev(NULL, ohci->bus);
	if (!usb_dev) return -1;

	dev = usb_to_ohci(usb_dev);
	// usb_dev->bus = ohci->bus;
	ohci->bus->root_hub = usb_dev;
	dev->ohci = ohci;
	usb_connect(usb_dev);
	if(usb_new_device(usb_dev) != 0) {
		usb_free_dev(usb_dev); 
		return -1;
	}

  }
#endif	
	return 0;
}

/* an interrupt happens */
static void ohci_interrupt(int irq, void *__ohci, struct pt_regs *r)
{
	struct ohci *ohci = __ohci;
	struct ohci_regs *regs = ohci->regs;
 	int ints; 

	if((ohci->hc_area->hcca.done_head != 0) && !(ohci->hc_area->hcca.done_head & 0x01)) {
		ints =  OHCI_INTR_WDH;
	}
	else { 
 		if((ints = (readl(&regs->intrstatus) & readl(&regs->intrenable))) == 0)
		return;
	} 
   
	OHCI_DEBUG(printk("USB HC interrupt: %x (%x) \n", ints, readl(&regs->intrstatus));)

  
	if(ints & OHCI_INTR_WDH) {
		writel(OHCI_INTR_WDH, &regs->intrdisable);	
		usb_ohci_done_list(ohci, ohci_reverse_done_list(ohci));
		writel(OHCI_INTR_WDH, &regs->intrenable); 
	}
  
	if(ints & OHCI_INTR_SO) {
		printk("****  USB Schedule overrun ");
		writel(OHCI_INTR_SO, &regs->intrenable); 	 
	}

	if(ints & OHCI_INTR_SF) {
		writel(OHCI_INTR_SF,  &regs->intrdisable);	
		if(ohci->ed_rm_list != NULL) {
			usb_ohci_done_list(ohci, usb_ohci_del_list(ohci));
		}
	}
	writel(ints, &regs->intrstatus);
	writel(OHCI_INTR_MIE, &regs->intrenable);	
}

/* allocate OHCI */

static struct ohci *alloc_ohci(void* mem_base)
{
	int i;
	struct ohci *ohci;
	struct ohci_hc_area *hc_area;
	struct usb_bus *bus;

	hc_area = (struct  ohci_hc_area *) __get_free_pages(GFP_KERNEL, 1);
	if (!hc_area)
		return NULL;
		
	memset(hc_area, 0, sizeof(*hc_area));
    ohci = &hc_area->ohci;
	ohci->irq = -1;
	ohci->regs = mem_base;   
	ohci->hc_area = hc_area;
	__ohci = ohci;
	/*
	 * for load ballancing of the interrupt branches 
	 */
	for (i = 0; i < NUM_INTS; i++) ohci->ohci_int_load[i] = 0;
	for (i = 0; i < NUM_INTS; i++) hc_area->hcca.int_table[i] = 0;
	
	/*
	 * Store the end of control and bulk list eds. So, we know where we can add
	 * elements to these lists.
	 */
	ohci->ed_isotail     = NULL;
	ohci->ed_controltail = NULL;
	ohci->ed_bulktail    = NULL;

	bus = usb_alloc_bus(&sohci_device_operations);
	if (!bus) {
		free_pages((unsigned int) ohci->hc_area, 1);
		return NULL;
	}

	ohci->bus = bus;
	bus->hcpriv = (void *) ohci;
 
	return ohci;
} 


/*
 * De-allocate all resources..
 * */
static void release_ohci(struct ohci *ohci)
{
	
	OHCI_DEBUG(printk("USB HC release ohci \n"););
     /* stop hc */
  
   
		
    /* disconnect all devices */    
	if(ohci->bus->root_hub) usb_disconnect(&ohci->bus->root_hub);
	
	reset_hc(__ohci);
	writel(OHCI_USB_RESET, &ohci->regs->control);
	wait_ms(10);
	
	if (ohci->irq >= 0) {
		free_irq(ohci->irq, ohci);
		ohci->irq = -1;
	}

    usb_deregister_bus(ohci->bus);
    usb_free_bus(ohci->bus);
    
	/* unmap the IO address space */
	iounmap(ohci->regs);
       
	free_pages((unsigned int) ohci->hc_area, 1);	
}


/*
 * Increment the module usage count, start the control thread and
 * return success.
 * */
static int found_ohci(int irq, void* mem_base)
{
	struct ohci *ohci;
    OHCI_DEBUG(printk("USB HC found  ohci: irq= %d membase= %x \n", irq, (int)mem_base);)
    
	ohci = alloc_ohci(mem_base);
	if (!ohci) {
		return -ENOMEM;
	}
	
	reset_hc(ohci);
	writel(OHCI_USB_RESET, &ohci->regs->control);
	wait_ms(10);
	usb_register_bus(ohci->bus);
	
	if (request_irq(irq, ohci_interrupt, SA_SHIRQ, "ohci-usb", ohci) == 0) {
		ohci->irq = irq;     
		start_hc(ohci);
		return 0;
 	}	
	release_ohci(ohci);
	return -EBUSY;
}
 
static int start_ohci(struct pci_dev *dev)
{
	unsigned int mem_base = dev->resource[0].start;
	 
	mem_base = (unsigned int) ioremap_nocache(mem_base, 4096);

	if (!mem_base) {
		printk("Error mapping OHCI memory\n");
		return -EFAULT;
	}
	return found_ohci(dev->irq, (void *) mem_base);
} 


#ifdef CONFIG_APM
static int handle_apm_event(apm_event_t event)
{
	static int down = 0;

	switch (event) {
	case APM_SYS_SUSPEND:
	case APM_USER_SUSPEND:
		if (down) {
			printk(KERN_DEBUG "ohci: received extra suspend event\n");
			break;
		}
		down = 1;
		break;
	case APM_NORMAL_RESUME:
	case APM_CRITICAL_RESUME:
		if (!down) {
			printk(KERN_DEBUG "ohci: received bogus resume event\n");
			break;
		}
		down = 0;
//		start_hc(ohci);
		break;
	}
	return 0;
}
#endif

#define PCI_CLASS_SERIAL_USB_OHCI 0x0C0310
 
int ohci_hcd_init(void)
{
	struct pci_dev *dev = NULL;
 
	while((dev = pci_find_class(PCI_CLASS_SERIAL_USB_OHCI, dev))) { 
		if (start_ohci(dev) < 0) return -ENODEV;
	}
    
#ifdef CONFIG_APM
	apm_register_callback(&handle_apm_event);
#endif
  
    return 0;
}

#ifdef MODULE
int init_module(void)
{
	return ohci_hcd_init();
}

void cleanup_module(void)
{	
#	ifdef CONFIG_APM
	apm_unregister_callback(&handle_apm_event);
#	endif
	release_ohci(__ohci);
}
#endif //MODULE

