/*
 * URB OHCI HCD (Host Controller Driver) for USB.
 *
 * (C) Copyright 1999 Roman Weissgaerber <weissg@vienna.at>
 * 
 * [ Initialisation is based on Linus'  ]
 * [ uhci code and gregs ohci fragments ]
 * [ (C) Copyright 1999 Linus Torvalds  ]
 * [ (C) Copyright 1999 Gregory P. Smith]
 * 
 * 
 * History:
 * 
 * v5.2 1999/12/07 URB 3rd preview, 
 * v5.1 1999/11/30 URB 2nd preview, cpia, (usb-scsi)
 * v5.0 1999/11/22 URB Technical preview, Paul Mackerras powerbook susp/resume 
 * 	i386: HUB, Keyboard, Mouse, Printer 
 *
 * v4.3 1999/10/27 multiple HCs, bulk_request
 * v4.2 1999/09/05 ISO API alpha, new dev alloc, neg Error-codes
 * v4.1 1999/08/27 Randy Dunlap's - ISO API first impl.
 * v4.0 1999/08/18 
 * v3.0 1999/06/25 
 * v2.1 1999/05/09  code clean up
 * v2.0 1999/05/04 
 * v1.0 1999/04/27 initial release
 * 
 
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
// #include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/interrupt.h>  /* for in_interrupt() */

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>

#define DEBUG

#include "usb.h"
#include "ohci-hcd.h"


#ifdef DEBUG
	#define dbg printk
#else
	#define dbg __xxx
static void __xxx (const char *format, ...) 
{
}
#endif

#ifdef CONFIG_APM
#include <linux/apm_bios.h>
static int handle_apm_event (apm_event_t event);
#endif

#ifdef CONFIG_PMAC_PBOOK
#include <linux/adb.h>
#include <linux/pmu.h>
#endif

static DECLARE_WAIT_QUEUE_HEAD (op_wakeup); 
static LIST_HEAD (ohci_hcd_list);
spinlock_t usb_ed_lock = SPIN_LOCK_UNLOCKED;

/*-------------------------------------------------------------------------*
 * URB support functions 
 *-------------------------------------------------------------------------*/ 
 
/* free the private part of an URB */
 
static void urb_rm_priv (purb_t purb) 
{
	purb_priv_t purb_priv = purb->hcpriv;
	int i;
	void * wait;
	
	if (!purb_priv) return;
	
	wait = purb_priv->wait;
	
	for (i=0; i < purb_priv->length; i++) {
		if (purb_priv->td[i]) {
			OHCI_FREE (purb_priv->td[i]);
		}
	}
	kfree (purb->hcpriv);
	purb->hcpriv = NULL;
	
	if (wait) {
		add_wait_queue (&op_wakeup, wait); 
		wake_up (&op_wakeup);
	}
}

/*-------------------------------------------------------------------------*/
 
#ifdef DEBUG
static int sohci_get_current_frame_number (struct usb_device * dev);

/* debug| print the main components of an URB     
 * small: 0) header + data packets 1) just header */
 
static void urb_print (purb_t purb, char * str, int small)
{
	unsigned int pipe= purb->pipe;
	int i, len;
	
	if (!purb->dev || !purb->dev->bus) {
		printk(KERN_DEBUG " %s URB: no dev\n", str);
		return;
	}
	
	printk (KERN_DEBUG "%s URB:[%4x] dev:%2d,ep:%2d-%c,type:%s,"
			"flags:%4x,len:%d/%d,stat:%d(%x)\n", 
			str,
		 	sohci_get_current_frame_number (purb->dev), 
		 	usb_pipedevice (pipe),
		 	usb_pipeendpoint (pipe), 
		 	usb_pipeout (pipe)? 'O': 'I',
		 	usb_pipetype (pipe) < 2? (usb_pipeint (pipe)? "INTR": "ISOC"):
		 		(usb_pipecontrol (pipe)? "CTRL": "BULK"),
		 	purb->transfer_flags, 
		 	purb->actual_length, 
		 	purb->transfer_buffer_length,
		 	purb->status, purb->status);
	if (!small) {
		if (usb_pipecontrol (pipe)) {
			printk (KERN_DEBUG " cmd(8):");
			for (i = 0; i < 8 ; i++) 
				printk (" %02x", ((__u8 *) purb->setup_packet) [i]);
			printk ("\n");
		}
		if (purb->transfer_buffer_length > 0 && purb->transfer_buffer) {
			printk (KERN_DEBUG " data(%d/%d):", 
				purb->actual_length, 
				purb->transfer_buffer_length);
			len = usb_pipeout (pipe)? 
						purb->transfer_buffer_length: purb->actual_length;
			for (i = 0; i < 16 && i < len; i++) 
				printk(" %02x", ((__u8 *) purb->transfer_buffer) [i]);
			printk("%s stat:%d\n", i < len? "...": "", purb->status);
		}
	} 
	
}
#endif

/*-------------------------------------------------------------------------*
 * Interface functions (URB)
 *-------------------------------------------------------------------------*/

/* return a request to the completion handler */
 
static int sohci_return_urb (purb_t purb)
{
	purb_priv_t purb_priv = purb->hcpriv;
	purb_t purbt;
	unsigned int flags;
	int i;
	
	/* just to be sure */
	if (!purb->complete) {
		urb_rm_priv (purb);
		usb_dec_dev_use (purb->dev);
		return -1;
	}
	
	if (!purb_priv) return -1; /* urb already unlinked */
	
#ifdef DEBUG
	urb_print (purb, "RET", usb_pipeout (purb->pipe));
#endif

	switch (usb_pipetype(purb->pipe)) {
  		case PIPE_INTERRUPT:
			purb->complete (purb); /* call complete and requeue URB */	
  			purb->actual_length = 0;
  			purb->status = USB_ST_URB_PENDING;
  			if (purb_priv->state != URB_DEL)
  				td_submit_urb (purb);
  			break;
  			
		case PIPE_ISOCHRONOUS:
			for (purbt = purb->next; purbt && (purbt != purb); purbt = purbt->next);
			if (purbt) { /* send the reply and requeue URB */	
				purb->complete (purb);
				spin_lock_irqsave (&usb_ed_lock, flags);
				purb->actual_length = 0;
  				purb->status = USB_ST_URB_PENDING;
  				purb->start_frame = purb_priv->ed->last_iso + 1;
  				if (purb_priv->state != URB_DEL) {
  					for (i = 0; i < purb->number_of_packets; i++) {
  						purb->iso_frame_desc[i].actual_length = 0;
  						purb->iso_frame_desc[i].status = TD_NOTACCESSED;
  					}
  					td_submit_urb (purb);
  				}
  				spin_unlock_irqrestore (&usb_ed_lock, flags);
  			} else { /* unlink URB, call complete */
				urb_rm_priv (purb);
				usb_dec_dev_use (purb->dev);
				purb->complete (purb); 	
			}		
			break;
  				
		case PIPE_BULK:
		case PIPE_CONTROL: /* short packet error?, unlink URB, call complete */
			urb_rm_priv (purb);
			usb_dec_dev_use (purb->dev);
			purb->complete (purb);	
			break;
	}
	return 0;
}

/*-------------------------------------------------------------------------*/

/* get a transfer request */
 
static int sohci_submit_urb (purb_t purb)
{
	pohci_t ohci;
	ped_t ed;
	purb_priv_t purb_priv;
	unsigned int pipe = purb->pipe;
	int i, size = 0;
	unsigned int flags;
	
	if (!purb->dev || !purb->dev->bus) return -EINVAL;
	
	if (purb->hcpriv) return -EINVAL; /* urb already in use */

	usb_inc_dev_use (purb->dev);
	ohci = (pohci_t) purb->dev->bus->hcpriv;
	
#ifdef DEBUG
	urb_print(purb, "SUB", usb_pipein (pipe));
#endif
	
	if (usb_pipedevice (pipe) == ohci->rh.devnum) 
		return rh_submit_urb (purb); /* a request to the virtual root hub */

	/* every endpoint has a ed, locate and fill it */
	if (!(ed = ep_add_ed (purb->dev, pipe, purb->interval, 1))) {
		usb_dec_dev_use (purb->dev);	
		return -ENOMEM;
	}

	/* for the private part of the URB we need the number of TDs (size) */
	switch (usb_pipetype (pipe)) {
		case PIPE_BULK:	/* one TD for every 4096 Byte */
			size = (purb->transfer_buffer_length - 1) / 4096 + 1;
			break;
		case PIPE_ISOCHRONOUS: /* number of packets from URB */
			size = purb->number_of_packets;
			for (i = 0; i < purb->number_of_packets; i++) {
  				purb->iso_frame_desc[i].actual_length = 0;
  				purb->iso_frame_desc[i].status = TD_NOTACCESSED;
  			}
			break;
		case PIPE_CONTROL: /* 1 TD for setup, 1 for ACK and 1 for every 4096 B */
			size = (purb->transfer_buffer_length == 0)? 2: 
						(purb->transfer_buffer_length - 1) / 4096 + 3;
			break;
		case PIPE_INTERRUPT: /* one TD */
			size = 1;
			
			break;
	}

	/* allocate the private part or the URB */
	purb_priv = kmalloc (sizeof (urb_priv_t) + size * sizeof (ptd_t), 
							in_interrupt() ? GFP_ATOMIC : GFP_KERNEL);
	if (!purb_priv) {
		usb_dec_dev_use (purb->dev);	
		return -ENOMEM;
	}
	memset (purb_priv, 0, sizeof (urb_priv_t) + size * sizeof (ptd_t));
	
	/* fill the private part of the URB */
	purb->hcpriv = purb_priv;
	purb_priv->length = size;
	purb_priv->td_cnt = 0;
	purb_priv->state = 0;
	purb_priv->ed = ed;	
	purb_priv->wait = NULL;
	
	/* allocate the TDs */
	for (i = 0; i < size; i++) { 
		OHCI_ALLOC (purb_priv->td[i], sizeof (td_t));
		if (!purb_priv->td[i]) {
			usb_dec_dev_use (purb->dev);	
			urb_rm_priv (purb);
			return -ENOMEM;
		}
	}	
	spin_lock_irqsave (&usb_ed_lock, flags);	
	if (ed->state == ED_NEW || (ed->state & ED_DEL)) {
		urb_rm_priv(purb);
		usb_dec_dev_use (purb->dev);	
		return -EINVAL;
	}
	
	/* for ISOC transfers calculate start frame index */
	if (purb->transfer_flags & USB_ISO_ASAP) { 
		purb->start_frame = ((ed->state == ED_OPER)? (ed->last_iso + 1): 
								(ohci->hcca.frame_no + 10)) & 0xffff;
	}	
	
	td_submit_urb (purb); /* fill the TDs and link it to the ed */
						
	if (ed->state != ED_OPER)  /* link the ed into a chain if is not already */
		ep_link (ohci, ed);
	spin_unlock_irqrestore (&usb_ed_lock, flags);
	
	purb->status = USB_ST_URB_PENDING; 
	// queue_urb(s, &purb->urb_list);

	return 0;	
}

/*-------------------------------------------------------------------------*/

/* deactivate all TDs and remove the private part of the URB */
 
static int sohci_unlink_urb (purb_t purb)
{
	unsigned int flags;
	pohci_t ohci;
	DECLARE_WAITQUEUE (wait, current);
	
	if (!purb) /* just to be sure */ 
		return -EINVAL;
	  
	ohci = (pohci_t) purb->dev->bus->hcpriv; 
	
	if (usb_pipedevice (purb->pipe) == ohci->rh.devnum) 
		return rh_unlink_urb (purb); /* a request to the virtual root hub */
	
	if (purb->hcpriv) { 
		if (purb->status == USB_ST_URB_PENDING) { /* URB active? */
			purb_priv_t purb_priv = purb->hcpriv;
			purb_priv->state = URB_DEL; 
			/* we want to delete the TDs of an URB from an ed 
			 * request the deletion, it will be handled at the next USB-frame */
			purb_priv->wait = &wait;
			current->state = TASK_UNINTERRUPTIBLE;
			spin_lock_irqsave (&usb_ed_lock, flags);
			ep_rm_ed (purb->dev, purb_priv->ed);
			purb_priv->ed->state |= ED_URB_DEL;
			spin_unlock_irqrestore (&usb_ed_lock, flags);
			current->state = TASK_UNINTERRUPTIBLE;
			schedule_timeout (HZ / 10); /* wait until all TDs are deleted */
			remove_wait_queue (&op_wakeup, &wait); 
		} else 
			urb_rm_priv (purb);
		usb_dec_dev_use (purb->dev);		
	}	
	return 0;
}

/*-------------------------------------------------------------------------*/

/* allocate private data space for a usb device */

static int sohci_alloc_dev (struct usb_device *usb_dev)
{
	struct ohci_device *dev;

	dev = kmalloc (sizeof (*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;
		
	memset (dev, 0, sizeof (*dev));

	usb_dev->hcpriv = dev;

	return 0;
}

/*-------------------------------------------------------------------------*/

/* free private data space of usb device */
  
static int sohci_free_dev (struct usb_device *usb_dev)
{
	unsigned int flags;
	int i, cnt = 0;
	ped_t ed;
	DECLARE_WAITQUEUE (wait, current);
	struct ohci_device *dev = usb_to_ohci (usb_dev);
	struct ohci *ohci = usb_dev->bus->hcpriv;
	
	if (!dev) return 0;
	
	if (usb_dev->devnum >= 0) {
	
		/* delete all TDs of all EDs */
		spin_lock_irqsave (&usb_ed_lock, flags);	
		for(i = 0; i < NUM_EDS; i++) {
  			ed = &(dev->ed[i]);
  			if (ed->state != ED_NEW) {
  				if (ed->state == ED_OPER) ep_unlink (ohci, ed);
  				ep_rm_ed (usb_dev, ed);
  				ed->state = ED_DEL;
  				cnt++;
  			}
  		}
  		spin_unlock_irqrestore (&usb_ed_lock, flags);
  		
    	if (cnt > 0) { 
    		dev->wait = &wait;
			current->state = TASK_UNINTERRUPTIBLE;
			schedule_timeout (HZ / 10);
			remove_wait_queue (&op_wakeup, &wait);
		}
	}
	kfree (dev);

	return 0;
}

/*-------------------------------------------------------------------------*/

/* tell us the current USB frame number */

static int sohci_get_current_frame_number (struct usb_device *usb_dev) 
{
	pohci_t ohci = usb_dev->bus->hcpriv;
	
	return ohci->hcca.frame_no;
}

/*-------------------------------------------------------------------------*/

struct usb_operations sohci_device_operations = {
	sohci_alloc_dev,
	sohci_free_dev,
	sohci_get_current_frame_number,
	sohci_submit_urb,
	sohci_unlink_urb
};

/*-------------------------------------------------------------------------*
 * ED handling functions
 *-------------------------------------------------------------------------*/  
		
/* search for the right branch to insert an interrupt ed into the int tree 
 * do some load ballancing;
 * returns the branch and 
 * sets the interval to interval = 2^integer (ld (interval)) */

static int ep_int_ballance (pohci_t ohci, int interval, int load)
{
	int i, branch = 0;
   
	/* search for the least loaded interrupt endpoint branch of all 32 branches */
	for (i = 0; i < 32; i++) 
		if (ohci->ohci_int_load [branch] > ohci->ohci_int_load [i]) branch = i; 
  
	branch = branch % interval;
	for (i = branch; i < 32; i += interval) ohci->ohci_int_load [i] += load;

	return branch;
}

/*-------------------------------------------------------------------------*/

/*  2^int( ld (inter)) */

static int ep_2_n_interval (int inter)
{	
	int i;
	for (i = 0; ((inter >> i) > 1 ) && (i < 5); i++); 
	return 1 << i;
}

/*-------------------------------------------------------------------------*/

/* the int tree is a binary tree 
 * in order to process it sequentially the indexes of the branches have to be mapped 
 * the mapping reverses the bits of a word of num_bits length */
 
static int ep_rev (int num_bits, int word)
{
	int i, wout = 0;

	for (i = 0; i < num_bits; i++) wout |= (((word >> i) & 1) << (num_bits - i - 1));
	return wout;
}

/*-------------------------------------------------------------------------*/

/* link an ed into one of the HC chains */

static int ep_link (pohci_t ohci, ped_t ed)
{	 
	int int_branch;
	int i;
	int inter;
	int interval;
	int load;
	__u32 * ed_p;
	
	ed->state = ED_OPER;
	
	switch (ed->type) {
	case CTRL:
		ed->hwNextED = 0;
		if (ohci->ed_controltail == NULL) {
			writel (virt_to_bus (ed), &ohci->regs->ed_controlhead);
		} else {
			ohci->ed_controltail->hwNextED = cpu_to_le32 (virt_to_bus (ed));
		}
		ed->ed_prev = ohci->ed_controltail;
		ohci->ed_controltail = ed;	  
		break;
		
	case BULK:  
		ed->hwNextED = 0;
		if (ohci->ed_bulktail == NULL) {
			writel (virt_to_bus (ed), &ohci->regs->ed_bulkhead);
		} else {
			ohci->ed_bulktail->hwNextED = cpu_to_le32 (virt_to_bus (ed));
		}
		ed->ed_prev = ohci->ed_bulktail;
		ohci->ed_bulktail = ed;	  
		break;
		
	case INT:
		load = ed->int_load;
		interval = ep_2_n_interval (ed->int_period);
		ed->int_interval = interval;
		int_branch = ep_int_ballance (ohci, interval, load);
		ed->int_branch = int_branch;
		
		for (i = 0; i < ep_rev (6, interval); i += inter) {
			inter = 1;
			for (ed_p = &(ohci->hcca.int_table[ep_rev (5, i) + int_branch]); 
				(*ed_p != 0) && (((ped_t) bus_to_virt (le32_to_cpu (*ed_p)))->int_interval >= interval); 
				ed_p = &(((ped_t) bus_to_virt (le32_to_cpu (*ed_p)))->hwNextED)) 
					inter = ep_rev (6, ((ped_t) bus_to_virt (le32_to_cpu (*ed_p)))->int_interval);
			ed->hwNextED = *ed_p; 
			*ed_p = cpu_to_le32 (virt_to_bus (ed));
		}
		break;
		
	case ISO:
		if (ohci->ed_isotail != NULL) {
			ohci->ed_isotail->hwNextED = cpu_to_le32 (virt_to_bus (ed));
			ed->ed_prev = ohci->ed_isotail;
		} else {
			for ( i = 0; i < 32; i += inter) {
				inter = 1;
				for (ed_p = &(ohci->hcca.int_table[ep_rev (5, i)]); 
					*ed_p != 0; 
					ed_p = &(((ped_t) bus_to_virt (le32_to_cpu (*ed_p)))->hwNextED)) 
						inter = ep_rev (6, ((ped_t) bus_to_virt (le32_to_cpu (*ed_p)))->int_interval);
				*ed_p = cpu_to_le32 (virt_to_bus (ed));	
			}	
			ed->ed_prev = NULL;
		}	
		ed->hwNextED = 0;
		ohci->ed_isotail = ed;  
		break;
	}
	 	
	return 0;
}

/*-------------------------------------------------------------------------*/

/* unlink an ed from one of the HC chains. 
 * just the link to the ed is unlinked.
 * the link from the ed still points to another operational ed or 0
 * so the HC can eventually finish the processing of the unlinked ed */

static int ep_unlink (pohci_t ohci, ped_t ed) 
{
	int int_branch;
	int i;
	int inter;
	int interval;
	__u32 * ed_p;
	 
    
	switch (ed->type) {
	case CTRL: 
		if (ed->ed_prev == NULL) {
			writel (le32_to_cpu (ed->hwNextED), &ohci->regs->ed_controlhead);
		} else {
			ed->ed_prev->hwNextED = ed->hwNextED;
		}
		if(ohci->ed_controltail == ed) {
			ohci->ed_controltail = ed->ed_prev;
		} else {
			((ped_t) bus_to_virt (le32_to_cpu (ed->hwNextED)))->ed_prev = ed->ed_prev;
		}
		break;
      
	case BULK: 
		if (ed->ed_prev == NULL) {
			writel (le32_to_cpu (ed->hwNextED), &ohci->regs->ed_bulkhead);
		} else {
			ed->ed_prev->hwNextED = ed->hwNextED;
		}
		if (ohci->ed_bulktail == ed) {
			ohci->ed_bulktail = ed->ed_prev;
		} else {
			((ped_t) bus_to_virt (le32_to_cpu (ed->hwNextED)))->ed_prev = ed->ed_prev;
		}
		break;
      
    case INT: 
      	int_branch = ed->int_branch;
		interval = ed->int_interval;

		for (i = 0; i < ep_rev (6, interval); i += inter) {
			for (ed_p = &(ohci->hcca.int_table[ep_rev (5, i) + int_branch]), inter = 1; 
				(*ed_p != 0) && (*ed_p != ed->hwNextED); 
				ed_p = &(((ped_t) bus_to_virt (le32_to_cpu (*ed_p)))->hwNextED), 
				inter = ep_rev (6, ((ped_t) bus_to_virt (le32_to_cpu (*ed_p)))->int_interval)) {				
					if(((ped_t) bus_to_virt (le32_to_cpu (*ed_p))) == ed) {
			  			*ed_p = ed->hwNextED;		
			  			break;
			  		}
			  }
		}
		for (i = int_branch; i < 32; i += interval) ohci->ohci_int_load[i] -= ed->int_load;
		break;
		
    case ISO:
    	if (ohci->ed_isotail == ed)
				ohci->ed_isotail = ed->ed_prev;
		if (ed->hwNextED != 0) 
				((ped_t) bus_to_virt (le32_to_cpu (ed->hwNextED)))->ed_prev = ed->ed_prev;
				
		if (ed->ed_prev != NULL) {
			ed->ed_prev->hwNextED = ed->hwNextED;
		} else {
			for (i = 0; i < 32; i += inter) {
				inter = 1;
				for (ed_p = &(ohci->hcca.int_table[ep_rev (5, i)]); 
					*ed_p != 0; 
					ed_p = &(((ped_t) bus_to_virt (le32_to_cpu (*ed_p)))->hwNextED)) {
						inter = ep_rev (6, ((ped_t) bus_to_virt (le32_to_cpu (*ed_p)))->int_interval);
						if(((ped_t) bus_to_virt (le32_to_cpu (*ed_p))) == ed) {
			  				*ed_p = ed->hwNextED;		
			  				break;
			  			}
			  	}
			}	
		}	
		break;
    }
    ed->state = ED_UNLINK;
    return 0;
}


/*-------------------------------------------------------------------------*/

/* add/reinit an endpoint; this should be done once at the usb_set_configuration command,
 * but the USB stack is a little bit stateless  so we do it at every transaction
 * if the state of the ed is ED_NEW then a dummy td is added and the state is changed to ED_UNLINK
 * in all other cases the state is left unchanged
 * the ed info fields are setted anyway even though most of them should not change */
 
static ped_t ep_add_ed (struct usb_device * usb_dev, unsigned int pipe, int interval, int load)
{
   	struct ohci * ohci = usb_dev->bus->hcpriv;
	ptd_t td;  
	ped_t ed; 
 	
 	
	spin_lock (&usb_ed_lock);

	ed = &(usb_to_ohci (usb_dev)->ed[(usb_pipeendpoint (pipe) << 1) | 
			(usb_pipecontrol (pipe)? 0: usb_pipeout (pipe))]);

	if((ed->state & ED_DEL) || (ed->state & ED_URB_DEL)) 
		return NULL; /* pending delete request */
	
	if (ed->state == ED_NEW) {
  		OHCI_ALLOC (td, sizeof (*td)); /* dummy td; end of td list for ed */
  		if(!td) return NULL; /* out of memory */
		ed->hwTailP = cpu_to_le32 (virt_to_bus (td));
		ed->hwHeadP = ed->hwTailP;	
		ed->state = ED_UNLINK;
		ed->type = usb_pipetype (pipe);
		usb_to_ohci (usb_dev)->ed_cnt++;
	}

	ohci->dev[usb_pipedevice (pipe)] = usb_dev;
	
	ed->hwINFO = cpu_to_le32 (usb_pipedevice (pipe)
			| usb_pipeendpoint (pipe) << 7
			| (usb_pipeisoc (pipe)? 0x8000: 0)
			| (usb_pipecontrol (pipe)? 0: (usb_pipeout (pipe)? 0x800: 0x1000)) 
			| usb_pipeslow (pipe) << 13
			| usb_maxpacket (usb_dev, pipe, usb_pipeout (pipe)) << 16);
  
  	if (ed->type == INT && ed->state == ED_UNLINK) {
  		ed->int_period = interval;
  		ed->int_load = load;
  	}
  	
	spin_unlock(&usb_ed_lock);
	return ed; 
}

/*-------------------------------------------------------------------------*/
 
/* request the removal of an endpoint
 * put the ep on the rm_list and request a stop of the bulk or ctrl list 
 * real removal is done at the next start of frame (SOF) hardware interrupt */
 
static void ep_rm_ed (struct usb_device * usb_dev, ped_t ed)
{    
	unsigned int frame;
	pohci_t ohci = usb_dev->bus->hcpriv;

	if ((ed->state & ED_DEL) || (ed->state & ED_URB_DEL)) return;
	
	ed->hwINFO  |=  cpu_to_le32 (OHCI_ED_SKIP);
	
	writel (OHCI_INTR_SF, &ohci->regs->intrstatus);
	writel (OHCI_INTR_SF, &ohci->regs->intrenable); /* enable sof interrupt */

	frame = ohci->hcca.frame_no & 0x1;
	ed->ed_rm_list = ohci->ed_rm_list[frame];
	ohci->ed_rm_list[frame] = ed;

	switch (ed->type) {
		case CTRL: /* stop CTRL list */
			writel (ohci->hc_control &= ~(0x01 << 4), &ohci->regs->control); 
  			break;
		case BULK: /* stop BULK list */
			writel (ohci->hc_control &= ~(0x01 << 5), &ohci->regs->control); 
			break;
	}
}

/*-------------------------------------------------------------------------*
 * TD handling functions
 *-------------------------------------------------------------------------*/

/* prepare a TD */

static void td_fill (unsigned int info, void * data, int len, purb_t purb, int type, int index)
{
	volatile ptd_t td, td_pt;
	purb_priv_t purb_priv = purb->hcpriv;
	
	td_pt = purb_priv->td [index];
	/* fill the old dummy TD */
	td = (ptd_t) bus_to_virt (le32_to_cpu (purb_priv->ed->hwTailP) & 0xfffffff0);
	td->ed = purb_priv->ed;
	td->index = index;
	td->urb = purb; 
	td->hwINFO = cpu_to_le32 (info);
	td->type = type;
	if ((td->ed->type & 3) == PIPE_ISOCHRONOUS) {
		td->hwCBP = cpu_to_le32 (((!data || !len)? 
								0 : virt_to_bus (data)) & 0xFFFFF000);
		td->ed->last_iso = info & 0xffff;
	} else {
		td->hwCBP = cpu_to_le32 (((!data || !len)? 0 : virt_to_bus (data))); 
	}			
	td->hwBE = cpu_to_le32 ((!data || !len )? 0: virt_to_bus (data + len - 1));
	td->hwNextTD = cpu_to_le32 (virt_to_bus (td_pt));
	td->hwPSW [0] = cpu_to_le16 ((virt_to_bus (data) & 0x0FFF) | 0xE000);
	td_pt->hwNextTD = 0;
	td->ed->hwTailP = td->hwNextTD;
	purb_priv->td [index] = td;
   
	td->next_dl_td = td_pt;
}

/*-------------------------------------------------------------------------*/
 
/* prepare all TDs of a transfer */

static void td_submit_urb (purb_t purb)
{ 
	purb_priv_t purb_priv = purb->hcpriv;
	pohci_t ohci = (pohci_t) purb->dev->bus->hcpriv;
	void * ctrl = purb->setup_packet;
	void * data = purb->transfer_buffer;
	int data_len = purb->transfer_buffer_length;
	int cnt = 0; 
	__u32 info = 0;
  

	purb_priv->td_cnt = 0;
	
	switch (usb_pipetype (purb->pipe)) {
		case PIPE_BULK:
			info = usb_pipeout (purb->pipe)? 
				TD_CC | TD_DP_OUT | TD_T_TOGGLE: TD_CC | TD_DP_IN | TD_T_TOGGLE;
			while(data_len > 4096) {		
				td_fill (info, data, 4096, purb, (cnt? 0: ST_ADDR) | ADD_LEN, cnt);
				data += 4096; data_len -= 4096; cnt++;
			}
			info = usb_pipeout (purb->pipe)?
				TD_CC | TD_DP_OUT | TD_T_TOGGLE: TD_CC | TD_R | TD_DP_IN | TD_T_TOGGLE;
			td_fill (info, data, data_len, purb, (cnt? 0: ST_ADDR) | ADD_LEN, cnt);
			writel (OHCI_BLF, &ohci->regs->cmdstatus); /* start bulk list */
			break;

		case PIPE_INTERRUPT:
			info = usb_pipeout (purb->pipe)? 
				TD_CC | TD_DP_OUT | TD_T_TOGGLE: TD_CC | TD_R | TD_DP_IN | TD_T_TOGGLE;
			td_fill (info, data, data_len, purb, ST_ADDR | ADD_LEN, 0);
			break;

		case PIPE_CONTROL:
			info = TD_CC | TD_DP_SETUP | TD_T_DATA0;
			td_fill (info, ctrl, 8, purb, ST_ADDR, cnt++); 
			if (data_len > 0) {  
				info = usb_pipeout (purb->pipe)? 
					TD_CC | TD_R | TD_DP_OUT | TD_T_DATA1 : TD_CC | TD_R | TD_DP_IN | TD_T_DATA1;
				td_fill (info, data, data_len, purb, ADD_LEN, cnt++);  
			} 
			info = usb_pipeout (purb->pipe)? 
 				TD_CC | TD_DP_IN | TD_T_DATA1: TD_CC | TD_DP_OUT | TD_T_DATA1;
			td_fill (info, NULL, 0, purb, 0, cnt++);
			writel (OHCI_CLF, &ohci->regs->cmdstatus); /* start Control list */
			break;

		case PIPE_ISOCHRONOUS:
			for (cnt = 0; cnt < purb->number_of_packets; cnt++) {
				td_fill (TD_CC|TD_ISO | ((purb->start_frame + cnt) & 0xffff), 
 					(__u8 *) data + purb->iso_frame_desc[cnt].offset, 
					purb->iso_frame_desc[cnt].length, purb, (cnt? 0: ST_ADDR) | ADD_LEN, cnt); 
			}
			break;
	} 
}

/*-------------------------------------------------------------------------*
 * Done List handling functions
 *-------------------------------------------------------------------------*/
 
/* replies to the request have to be on a FIFO basis so
 * we reverse the reversed done-list */
 
static ptd_t dl_reverse_done_list (pohci_t ohci)
{
	__u32 td_list_hc;
	ptd_t td_rev = NULL;
	ptd_t td_list = NULL;
  	purb_priv_t purb_priv = NULL;
  	unsigned int flags;
  	
  	spin_lock_irqsave (&usb_ed_lock, flags);
  	
	td_list_hc = le32_to_cpu (ohci->hcca.done_head) & 0xfffffff0;
	ohci->hcca.done_head = 0;
	
	while (td_list_hc) {		
		td_list = (ptd_t) bus_to_virt (td_list_hc);

		if (TD_CC_GET (le32_to_cpu (td_list->hwINFO))) {
			purb_priv = (purb_priv_t) td_list->urb->hcpriv;
			dbg (KERN_DEBUG MODSTR "**** USB-error/status: %x : %p \n", 
					TD_CC_GET (le32_to_cpu (td_list->hwINFO)), td_list);
			if (td_list->ed->hwHeadP & cpu_to_le32 (0x1)) {
				if (purb_priv && ((td_list->index + 1) < purb_priv->length)) {
					td_list->ed->hwHeadP = 
						(purb_priv->td[purb_priv->length - 1]->hwNextTD & cpu_to_le32 (0xfffffff0)) |
									(td_list->ed->hwHeadP & cpu_to_le32 (0x2));
					purb_priv->td_cnt = purb_priv->length - 1;
				} else 
					td_list->ed->hwHeadP &= cpu_to_le32 (0xfffffff2);
			}
		}

		td_list->next_dl_td = td_rev;	
		td_rev = td_list;
		td_list_hc = le32_to_cpu (td_list->hwNextTD) & 0xfffffff0;	
	}	
	spin_unlock_irqrestore (&usb_ed_lock, flags);
	return td_list;
}

/*-------------------------------------------------------------------------*/

/* there are some pending requests to remove 
 * - some of the eds (if ed->state & ED_DEL (set by sohci_free_dev)
 * - some URBs/TDs if purb_priv->state == URB_DEL */
 
static void dl_del_list (pohci_t ohci, unsigned int frame)
{
	unsigned int flags;
	ped_t ed;
	__u32 edINFO;
	ptd_t td = NULL, td_next = NULL, tdHeadP = NULL, tdTailP;
	__u32 * td_p;
	int ctrl=0, bulk=0;

	spin_lock_irqsave (&usb_ed_lock, flags);
	for (ed = ohci->ed_rm_list[frame]; ed != NULL; ed = ed->ed_rm_list) {

   	 	tdTailP = bus_to_virt (le32_to_cpu (ed->hwTailP) & 0xfffffff0);
		tdHeadP = bus_to_virt (le32_to_cpu (ed->hwHeadP) & 0xfffffff0);
		edINFO = le32_to_cpu (ed->hwINFO);
		td_p = &ed->hwHeadP;
			
		for (td = tdHeadP; td != tdTailP; td = td_next) { 
			purb_t purb = td->urb;
			purb_priv_t purb_priv = td->urb->hcpriv;
			
			td_next = td->next_dl_td; /* td may be freed by urb_rm_priv */
			if ((purb_priv->state == URB_DEL) || (ed->state & ED_DEL)) {
				*td_p = td->hwNextTD | (*td_p & cpu_to_le32 (0x3));
				if(++ (purb_priv->td_cnt) == purb_priv->length) 
					urb_rm_priv (purb);
			} else {
				td_p = &td->hwNextTD;
			}
			
		}
		if (ed->state & ED_DEL) { /* set by sohci_free_dev */
			struct ohci_device * dev = usb_to_ohci (ohci->dev[edINFO & 0x7F]);
   	 		OHCI_FREE (tdTailP); /* free dummy td */
   	 		ed->state = ED_NEW; 
   	 		/* if all eds are removed wake up sohci_free_dev */
   	 		if ((! --dev->ed_cnt) && dev->wait) {
   	 			add_wait_queue (&op_wakeup, dev->wait); 
				wake_up (&op_wakeup);
			}
   	 	}
   	 	else {
   	 		ed->state &= ~ED_URB_DEL;
   	 		ed->hwINFO  &=  ~cpu_to_le32 (OHCI_ED_SKIP);
   	 	}
   	 	
   	 	if ((ed->type & 3) == CTRL) ctrl |= 1;
  		if ((ed->type & 3) == BULK) bulk |= 1;  
   	}
   	
   	if (ctrl) writel (0, &ohci->regs->ed_controlcurrent); /* reset CTRL list */
   	if (bulk) writel (0, &ohci->regs->ed_bulkcurrent);    /* reset BULK list */
	if (!ohci->ed_rm_list[!frame]) 		/* start CTRL u. BULK list */ 
  		writel (ohci->hc_control |= (0x03<<4), &ohci->regs->control);   
   	ohci->ed_rm_list[frame] = NULL;

   	spin_unlock_irqrestore (&usb_ed_lock, flags);
}

/*-------------------------------------------------------------------------*/

/* td done list */

static void dl_done_list (pohci_t ohci, ptd_t td_list)
{
  	ptd_t td_list_next = NULL;
	ped_t ed;
	int dlen = 0;
	int cc = 0;
	purb_t purb;
	purb_priv_t purb_priv;
 	__u32 tdINFO, tdBE, tdCBP, edHeadP, edTailP;
 	__u16 tdPSW;
 	unsigned int flags;
 	
  	while (td_list) {
   		td_list_next = td_list->next_dl_td;
   		
  		purb = td_list->urb;
  		purb_priv = purb->hcpriv;
  		tdINFO = le32_to_cpu (td_list->hwINFO);
  		tdBE   = le32_to_cpu (td_list->hwBE);
  		tdCBP  = le32_to_cpu (td_list->hwCBP);
  		
   		ed = td_list->ed;
   		
  		if (td_list->type & ST_ADDR) 
  			purb->actual_length = 0;
  			
 		if (td_list->type & ADD_LEN) { /* accumulate length of multi td transfers */
 			if (tdINFO & TD_ISO) {
 				tdPSW = le16_to_cpu (td_list->hwPSW[0]);
 				cc = (tdPSW >> 12) & 0xF;
				if (cc < 0xE)  {
					if (usb_pipeout(purb->pipe)) {
						dlen = purb->iso_frame_desc[td_list->index].length;
					} else {
						dlen = tdPSW & 0x3ff;
					}
					purb->actual_length += dlen;
					purb->iso_frame_desc[td_list->index].actual_length = dlen;
					if (!(purb->transfer_flags & USB_DISABLE_SPD) && (cc == TD_DATAUNDERRUN))
						cc = TD_CC_NOERROR;
					 
					purb->iso_frame_desc[td_list->index].status = cc_to_error[cc];
				}
 			} else {
 				if (tdBE != 0) {
 					dlen = (bus_to_virt (tdBE) - purb->transfer_buffer + 1);
 					if (td_list->hwCBP == 0)
  			    		purb->actual_length += dlen;
  					else
  			    		purb->actual_length += (bus_to_virt(tdCBP) - purb->transfer_buffer);
  			    }
  			}
  		}
  		/* error code of transfer */
  		cc = TD_CC_GET (tdINFO);
  		if (!(purb->transfer_flags & USB_DISABLE_SPD) && (cc == TD_DATAUNDERRUN))
						cc = TD_CC_NOERROR;
  		if (++(purb_priv->td_cnt) == purb_priv->length) {
  			if (purb_priv->state != URB_DEL) { 
  				purb->status = cc_to_error[cc];
  				sohci_return_urb (purb);
  			} else {
  				urb_rm_priv (purb);
  			}
  		}
  		
  		spin_lock_irqsave (&usb_ed_lock, flags);
  		if (ed->state != ED_NEW) { 
  			edHeadP = le32_to_cpu (ed->hwHeadP) & 0xfffffff0;
  			edTailP = le32_to_cpu (ed->hwTailP);

     		if((edHeadP == edTailP) && (ed->state == ED_OPER)) 
     			ep_unlink (ohci, ed); /* unlink eds if they are not busy */
     		
     	}	
     	spin_unlock_irqrestore (&usb_ed_lock, flags);
     	
    	td_list = td_list_next;
  	}  
}




/*-------------------------------------------------------------------------*
 * Virtual Root Hub 
 *-------------------------------------------------------------------------*/
 
static __u8 root_hub_dev_des[] =
{
	0x12,       /*  __u8  bLength; */
	0x01,       /*  __u8  bDescriptorType; Device */
	0x00,	    /*  __u16 bcdUSB; v1.0 */
	0x01,
	0x09,	    /*  __u8  bDeviceClass; HUB_CLASSCODE */
	0x00,	    /*  __u8  bDeviceSubClass; */
	0x00,       /*  __u8  bDeviceProtocol; */
	0x08,       /*  __u8  bMaxPacketSize0; 8 Bytes */
	0x00,       /*  __u16 idVendor; */
	0x00,
	0x00,       /*  __u16 idProduct; */
 	0x00,
	0x00,       /*  __u16 bcdDevice; */
 	0x00,
	0x00,       /*  __u8  iManufacturer; */
	0x00,       /*  __u8  iProduct; */
	0x00,       /*  __u8  iSerialNumber; */
	0x01        /*  __u8  bNumConfigurations; */
};


/* Configuration descriptor */
static __u8 root_hub_config_des[] =
{
	0x09,       /*  __u8  bLength; */
	0x02,       /*  __u8  bDescriptorType; Configuration */
	0x19,       /*  __u16 wTotalLength; */
	0x00,
	0x01,       /*  __u8  bNumInterfaces; */
	0x01,       /*  __u8  bConfigurationValue; */
	0x00,       /*  __u8  iConfiguration; */
	0x40,       /*  __u8  bmAttributes; 
		 Bit 7: Bus-powered, 6: Self-powered, 5 Remote-wakwup, 4..0: resvd */
	0x00,       /*  __u8  MaxPower; */
      
	/* interface */	  
	0x09,       /*  __u8  if_bLength; */
	0x04,       /*  __u8  if_bDescriptorType; Interface */
	0x00,       /*  __u8  if_bInterfaceNumber; */
	0x00,       /*  __u8  if_bAlternateSetting; */
	0x01,       /*  __u8  if_bNumEndpoints; */
	0x09,       /*  __u8  if_bInterfaceClass; HUB_CLASSCODE */
	0x00,       /*  __u8  if_bInterfaceSubClass; */
	0x00,       /*  __u8  if_bInterfaceProtocol; */
	0x00,       /*  __u8  if_iInterface; */
     
	/* endpoint */
	0x07,       /*  __u8  ep_bLength; */
	0x05,       /*  __u8  ep_bDescriptorType; Endpoint */
	0x81,       /*  __u8  ep_bEndpointAddress; IN Endpoint 1 */
 	0x03,       /*  __u8  ep_bmAttributes; Interrupt */
 	0x08,       /*  __u16 ep_wMaxPacketSize; 8 Bytes */
 	0x00,
	0xff        /*  __u8  ep_bInterval; 255 ms */
};

/* 
For OHCI we need just the  2nd Byte, so we 
don't need this constant byte-array 

static __u8 root_hub_hub_des[] =
{ 
	0x00,       *  __u8  bLength; *
	0x29,       *  __u8  bDescriptorType; Hub-descriptor *
	0x02,       *  __u8  bNbrPorts; *
	0x00,       * __u16  wHubCharacteristics; *
	0x00,
	0x01,       *  __u8  bPwrOn2pwrGood; 2ms * 
 	0x00,       *  __u8  bHubContrCurrent; 0 mA *
	0x00,       *  __u8  DeviceRemovable; *** 8 Ports max *** *
	0xff        *  __u8  PortPwrCtrlMask; *** 8 ports max *** *
};
*/

/*-------------------------------------------------------------------------*/

/* prepare Interrupt pipe data; HUB INTERRUPT ENDPOINT */ 
 
static int rh_send_irq (pohci_t ohci, void * rh_data, int rh_len)
{
	int num_ports;
	int i;
	int ret;
	int len;

	__u8 * data = rh_data;

	num_ports = readl (&ohci->regs->roothub.a) & 0xff; 
	*(__u8 *) data = (readl (&ohci->regs->roothub.status) & 0x00030000) > 0? 1: 0;
	ret = *(__u8 *) data;

	for ( i = 0; i < num_ports; i++) {
		*(__u8 *) (data + i / 8) |= 
			((readl (&ohci->regs->roothub.portstatus[i]) & 0x001f0000) > 0? 1: 0) << ((i + 1) % 8);
		ret += *(__u8 *) (data + i / 8);
	}
	len = i/8 + 1;
  
	if (ret > 0) return len;

	return 0;
}

/*-------------------------------------------------------------------------*/

/* Virtual Root Hub INTs are polled by this timer every "intervall" ms */
 
static void rh_int_timer_do (unsigned long ptr)
{
	int len; 

	purb_t purb = (purb_t) ptr;
	pohci_t ohci = purb->dev->bus->hcpriv;
	
	if(ohci->rh.send) { 
		len = rh_send_irq (ohci, purb->transfer_buffer, purb->transfer_buffer_length);
		if (len > 0) {
			purb->actual_length = len;
#ifdef DEBUG
			urb_print (purb, "RET(rh)", usb_pipeout (purb->pipe));
#endif
			if (purb->complete) purb->complete (purb);
		}
	}	
	rh_init_int_timer (purb);
}

/*-------------------------------------------------------------------------*/

/* Root Hub INTs are polled by this timer */

static int rh_init_int_timer (purb_t purb) 
{
	pohci_t ohci = purb->dev->bus->hcpriv;

	ohci->rh.interval = purb->interval;
	init_timer (&ohci->rh.rh_int_timer);
	ohci->rh.rh_int_timer.function = rh_int_timer_do;
	ohci->rh.rh_int_timer.data = (unsigned long) purb;
	ohci->rh.rh_int_timer.expires = 
			jiffies + (HZ * (purb->interval < 30? 30: purb->interval)) / 1000;
	add_timer (&ohci->rh.rh_int_timer);
	
	return 0;
}

/*-------------------------------------------------------------------------*/

#define OK(x) 				len = (x); req_reply = 0; break
#define WR_RH_STAT(x) 		writel((x), &ohci->regs->roothub.status)
#define WR_RH_PORTSTAT(x) 	writel((x), &ohci->regs->roothub.portstatus[wIndex-1])
#define RD_RH_STAT			readl(&ohci->regs->roothub.status)
#define RD_RH_PORTSTAT		readl(&ohci->regs->roothub.portstatus[wIndex-1])

/* request to virtual root hub */

static int rh_submit_urb (purb_t purb)
{
	struct usb_device *usb_dev = purb->dev;
	pohci_t ohci = usb_dev->bus->hcpriv;
	unsigned int pipe = purb->pipe;
	devrequest * cmd = (devrequest *) purb->setup_packet;
	void *data = purb->transfer_buffer;
	int leni = purb->transfer_buffer_length;
	int len = 0;
	int status = TD_CC_NOERROR;
	
	__u8 data_buf[16];
	int req_reply=-4;
	
 	__u16 bmRType_bReq;
	__u16 wValue; 
	__u16 wIndex;
	__u16 wLength;

	if (usb_pipeint(pipe)) {
	
		ohci->rh.urb =  purb;
		ohci->rh.send = 1;
		ohci->rh.interval = purb->interval;
		rh_init_int_timer(purb);
		purb->status = cc_to_error [TD_CC_NOERROR];
		
		return 0;
	}

	bmRType_bReq  = cmd->requesttype | (cmd->request << 8);
	wValue        = le16_to_cpu (cmd->value);
	wIndex        = le16_to_cpu (cmd->index);
	wLength       = le16_to_cpu (cmd->length);
	switch (bmRType_bReq) {
	/* Request Destination:
	   without flags: Device, 
	   RH_INTERFACE: interface, 
	   RH_ENDPOINT: endpoint,
	   RH_CLASS means HUB here, 
	   RH_OTHER | RH_CLASS  almost ever means HUB_PORT here 
	*/
  
		case RH_GET_STATUS: 				 		
				*(__u16 *) data = cpu_to_le16 (1); OK (2);
		case RH_GET_STATUS | RH_INTERFACE: 	 		
				*(__u16 *) data = cpu_to_le16 (0); OK (2);
		case RH_GET_STATUS | RH_ENDPOINT:	 		
				*(__u16 *) data = cpu_to_le16 (0); OK (2);   
		case RH_GET_STATUS | RH_CLASS: 				
				*(__u32 *) data = cpu_to_le32 (RD_RH_STAT & 0x7fff7fff); OK (4);
		case RH_GET_STATUS | RH_OTHER | RH_CLASS: 	
				*(__u32 *) data = cpu_to_le32 (RD_RH_PORTSTAT); OK (4);

		case RH_CLEAR_FEATURE | RH_ENDPOINT:  
			switch (wValue) {
				case (RH_ENDPOINT_STALL): OK (0);
			}
			break;

		case RH_CLEAR_FEATURE | RH_CLASS:
			switch (wValue) {
				case (RH_C_HUB_OVER_CURRENT): 
						WR_RH_STAT(RH_PS_OCIC); OK (0);
			}
			break;
		
		case RH_CLEAR_FEATURE | RH_OTHER | RH_CLASS:
			switch (wValue) {
				case (RH_PORT_ENABLE): 			
						WR_RH_PORTSTAT (RH_PS_CCS ); OK (0);
				case (RH_PORT_SUSPEND):			
						WR_RH_PORTSTAT (RH_PS_POCI); OK (0);
				case (RH_PORT_POWER):			
						WR_RH_PORTSTAT (RH_PS_LSDA); OK (0);
				case (RH_C_PORT_CONNECTION):	
						WR_RH_PORTSTAT (RH_PS_CSC ); OK (0);
				case (RH_C_PORT_ENABLE):		
						WR_RH_PORTSTAT (RH_PS_PESC); OK (0);
				case (RH_C_PORT_SUSPEND):		
						WR_RH_PORTSTAT (RH_PS_PSSC); OK (0);
				case (RH_C_PORT_OVER_CURRENT):	
						WR_RH_PORTSTAT (RH_PS_OCIC); OK (0);
				case (RH_C_PORT_RESET):			
						WR_RH_PORTSTAT (RH_PS_PRSC); OK (0); 
			}
			break;
 
		case RH_SET_FEATURE | RH_OTHER | RH_CLASS:
			switch (wValue) {
				case (RH_PORT_SUSPEND):			
						WR_RH_PORTSTAT (RH_PS_PSS ); OK (0); 
				case (RH_PORT_RESET): /* BUG IN HUP CODE *********/
						if((RD_RH_PORTSTAT &1) != 0)  WR_RH_PORTSTAT (RH_PS_PRS ); OK (0);
				case (RH_PORT_POWER):			
						WR_RH_PORTSTAT (RH_PS_PPS ); OK (0); 
				case (RH_PORT_ENABLE): /* BUG IN HUP CODE *********/
						if((RD_RH_PORTSTAT &1) != 0)  WR_RH_PORTSTAT (RH_PS_PES ); OK (0);
			}
			break;

		case RH_SET_ADDRESS: ohci->rh.devnum = wValue; OK(0);

		case RH_GET_DESCRIPTOR:
			switch ((wValue & 0xff00) >> 8) {
				case (0x01): /* device descriptor */
					len = min (leni, min (sizeof (root_hub_dev_des), wLength));
					memcpy (data, root_hub_dev_des, len); OK(len);
				case (0x02): /* configuration descriptor */
					len = min (leni, min (sizeof (root_hub_config_des), wLength));
					memcpy (data, root_hub_config_des, len); OK(len);
				case (0x03): /* string descriptors */
				default: 
					status = TD_CC_STALL;
			}
			break;
		
		case RH_GET_DESCRIPTOR | RH_CLASS:
			*(__u8 *)  (data_buf+1) = 0x29;
			*(__u32 *) (data_buf+2) = cpu_to_le32 (readl (&ohci->regs->roothub.a));  
	 		*(__u8 *)  data_buf = (*(__u8 *) (data_buf + 2) / 8) * 2 + 9; /* length of descriptor */
				 
			len = min (leni, min(*(__u8 *) data_buf, wLength));
			*(__u8 *) (data_buf+6) = 0; /* Root Hub needs no current from bus */
			if (*(__u8 *) (data_buf+2) < 8) { /* less than 8 Ports */
				*(__u8 *) (data_buf+7) = readl (&ohci->regs->roothub.b) & 0xff; 
				*(__u8 *) (data_buf+8) = (readl (&ohci->regs->roothub.b) & 0xff0000) >> 16; 
			} else {
				*(__u32 *) (data_buf+7) = cpu_to_le32 (readl(&ohci->regs->roothub.b)); 
			}
			memcpy (data, data_buf, len);
			OK (len); 
 
		case RH_GET_CONFIGURATION: 	*(__u8 *) data = 0x01; OK (1);

		case RH_SET_CONFIGURATION: 	WR_RH_STAT (0x10000); OK (0);

		default: 
			status = TD_CC_STALL;
	}
	
	dbg (KERN_DEBUG MODSTR "USB HC roothubstat1: %x \n",
			readl ( &(ohci->regs->roothub.portstatus[0]) ));
	dbg (KERN_DEBUG MODSTR "USB HC roothubstat2: %x \n",
			readl ( &(ohci->regs->roothub.portstatus[1]) ));

  	purb->actual_length = len;
	purb->status = cc_to_error [status];
	
#ifdef DEBUG
	urb_print (purb, "RET(rh)", usb_pipeout (purb->pipe));
#endif

	if (purb->complete) purb->complete (purb);
	return 0;
}

/*-------------------------------------------------------------------------*/

static int rh_unlink_urb (purb_t purb)
{
	pohci_t ohci = purb->dev->bus->hcpriv;
 
	ohci->rh.send = 0;
	del_timer (&ohci->rh.rh_int_timer);
	return 0;
}
 
/*-------------------------------------------------------------------------*
 * HC functions
 *-------------------------------------------------------------------------*/

/* reset the HC not the BUS */

static void hc_reset (pohci_t ohci)
{
	int timeout = 30;
	int smm_timeout = 50; /* 0,5 sec */
	 	
	if (readl (&ohci->regs->control) & 0x100) { /* SMM owns the HC */
		writel (0x08, &ohci->regs->cmdstatus); /* request ownership */
		printk (KERN_DEBUG MODSTR "USB HC TakeOver from SMM\n");
		while (readl (&ohci->regs->control) & 0x100) {
			wait_ms (10);
			if (--smm_timeout == 0) {
				printk (KERN_ERR MODSTR "USB HC TakeOver failed!\n");
				break;
			}
		}
	}	
		
	writel ((1 << 31), &ohci->regs->intrdisable); /* Disable HC interrupts */
	dbg (KERN_DEBUG MODSTR "USB HC reset_hc: %x ; \n", readl (&ohci->regs->control));
  	/* this seems to be needed for the lucent controller on powerbooks.. */
	writel (0, &ohci->regs->control);           /* Move USB to reset state */
      	
	writel (1,  &ohci->regs->cmdstatus);	   /* HC Reset */
	while ((readl (&ohci->regs->cmdstatus) & 0x01) != 0) { /* 10us Reset */
		if (--timeout == 0) {
			printk (KERN_ERR MODSTR "USB HC reset timed out!\n");
			return;
		}	
		udelay (1);
	}	 
}

/*-------------------------------------------------------------------------*/

/* Start an OHCI controller, set the BUS operational
 * enable interrupts 
 * connect the virtual root hub */

static int hc_start (pohci_t ohci)
{
	unsigned int mask;
  	unsigned int fminterval;
  	struct usb_device  * usb_dev;
	struct ohci_device * dev;
	
	/* Tell the controller where the control and bulk lists are
	 * The lists are empty now. */
	 
	writel (0, &ohci->regs->ed_controlhead);
	writel (0, &ohci->regs->ed_bulkhead);
	
	writel (virt_to_bus(&ohci->hcca), &ohci->regs->hcca); /* a reset clears this */
   
  	fminterval = 0x2edf;
	writel ((fminterval * 9) / 10, &ohci->regs->periodicstart);
	fminterval |= ((((fminterval - 210) * 6) / 7) << 16); 
	writel (fminterval, &ohci->regs->fminterval);	
	writel (0x628, &ohci->regs->lsthresh);

	/* Choose the interrupts we care about now, others later on demand */
	mask = OHCI_INTR_MIE | OHCI_INTR_WDH | OHCI_INTR_SO;
	
	writel (ohci->hc_control = 0xBF, &ohci->regs->control); /* USB Operational */
	writel (mask, &ohci->regs->intrenable);
	writel (mask, &ohci->regs->intrstatus);
 
	/* connect the virtual root hub */
	
	usb_dev = usb_alloc_dev (NULL, ohci->bus);
	if (!usb_dev) return -1;

	dev = usb_to_ohci (usb_dev);
	ohci->bus->root_hub = usb_dev;
	usb_connect (usb_dev);
	if (usb_new_device (usb_dev) != 0) {
		usb_free_dev (usb_dev); 
		return -1;
	}
	
	return 0;
}

/*-------------------------------------------------------------------------*/

/* an interrupt happens */

static void hc_interrupt (int irq, void *__ohci, struct pt_regs *r)
{
	pohci_t ohci = __ohci;
	struct ohci_regs *regs = ohci->regs;
 	int ints; 

	if ((ohci->hcca.done_head != 0) && !(ohci->hcca.done_head & 0x01)) {
		ints =  OHCI_INTR_WDH;
	} else { 
 		if ((ints = (readl (&regs->intrstatus) & readl (&regs->intrenable))) == 0)
			return;
	} 
 
	if (ints & OHCI_INTR_WDH) {
		writel (OHCI_INTR_WDH, &regs->intrdisable);	
		dl_done_list (ohci, dl_reverse_done_list (ohci));
		writel (OHCI_INTR_WDH, &regs->intrenable); 
	}
  
	if (ints & OHCI_INTR_SO) {
		dbg (KERN_ERR MODSTR " USB Schedule overrun \n");
		writel (OHCI_INTR_SO, &regs->intrenable); 	 
	}

	if (ints & OHCI_INTR_SF) { 
		unsigned int frame = (ohci->hcca.frame_no) & 1;
		writel (OHCI_INTR_SF, &regs->intrdisable);	
		if (ohci->ed_rm_list[!frame] != NULL) {
			dl_del_list (ohci, !frame);
		}
		if (ohci->ed_rm_list[frame] != NULL) writel (OHCI_INTR_SF, &regs->intrenable);	
	}
	writel (ints, &regs->intrstatus);
	writel (OHCI_INTR_MIE, &regs->intrenable);	
}

/*-------------------------------------------------------------------------*/

/* allocate OHCI */

static pohci_t hc_alloc_ohci (void* mem_base)
{
	int i;
	pohci_t ohci;
	struct usb_bus *bus;

	ohci = (pohci_t) __get_free_pages (GFP_KERNEL, 1);
	if (!ohci)
		return NULL;
		
	memset (ohci, 0, sizeof (ohci_t));
	
	ohci->irq = -1;
	ohci->regs = mem_base;   

	/* for load ballancing of the interrupt branches */
	for (i = 0; i < NUM_INTS; i++) ohci->ohci_int_load[i] = 0;
	for (i = 0; i < NUM_INTS; i++) ohci->hcca.int_table[i] = 0;
	
	/* end of control and bulk lists */	 
	ohci->ed_isotail     = NULL;
	ohci->ed_controltail = NULL;
	ohci->ed_bulktail    = NULL;

	bus = usb_alloc_bus (&sohci_device_operations);
	if (!bus) {
		free_pages ((unsigned int) ohci, 1);
		return NULL;
	}

	ohci->bus = bus;
	bus->hcpriv = (void *) ohci;
 
	return ohci;
} 

/*-------------------------------------------------------------------------*/

/* De-allocate all resources.. */

static void hc_release_ohci (pohci_t ohci)
{	
	dbg (KERN_DEBUG MODSTR "USB HC release ohci\n");

	/* disconnect all devices */    
	if (ohci->bus->root_hub) usb_disconnect (&ohci->bus->root_hub);
	
	hc_reset (ohci);
	writel (OHCI_USB_RESET, &ohci->regs->control);
	wait_ms (10);
	
	if (ohci->irq >= 0) {
		free_irq (ohci->irq, ohci);
		ohci->irq = -1;
	}

	usb_deregister_bus (ohci->bus);
	usb_free_bus (ohci->bus);
    
	/* unmap the IO address space */
	iounmap (ohci->regs);
       
	free_pages ((unsigned int) ohci, 1);	
}

/*-------------------------------------------------------------------------*/

/* Increment the module usage count, start the control thread and
 * return success. */
 
static int hc_found_ohci (int irq, void* mem_base)
{
	pohci_t ohci;
	dbg (KERN_DEBUG MODSTR "USB HC found: irq= %d membase= %x \n", irq, (int) mem_base);
    
	ohci = hc_alloc_ohci (mem_base);
	if (!ohci) {
		return -ENOMEM;
	}
	
	INIT_LIST_HEAD (&ohci->ohci_hcd_list);
	list_add (&ohci->ohci_hcd_list, &ohci_hcd_list);
	
	hc_reset (ohci);
	writel (ohci->hc_control = OHCI_USB_RESET, &ohci->regs->control);
	wait_ms (10);
	usb_register_bus (ohci->bus);
	
	if (request_irq (irq, hc_interrupt, SA_SHIRQ, "ohci-usb", ohci) == 0) {
		ohci->irq = irq;     
		hc_start (ohci);
		return 0;
 	}	
 	printk (KERN_ERR MODSTR "request interrupt %d failed\n", irq);
	hc_release_ohci (ohci);
	return -EBUSY;
}

/*-------------------------------------------------------------------------*/
 
static int hc_start_ohci (struct pci_dev *dev)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0)
	unsigned int mem_base = dev->resource[0].start;
#else
	unsigned int mem_base = dev->base_address[0];
	if (mem_base & PCI_BASE_ADDRESS_SPACE_IO) return -ENODEV;
	mem_base &= PCI_BASE_ADDRESS_MEM_MASK;
#endif
	
	pci_set_master (dev);
	mem_base = (unsigned int) ioremap_nocache (mem_base, 4096);

	if (!mem_base) {
		printk (KERN_ERR MODSTR "Error mapping OHCI memory\n");
		return -EFAULT;
	}
	return hc_found_ohci (dev->irq, (void *) mem_base);
} 

/*-------------------------------------------------------------------------*/

#ifdef CONFIG_PMAC_PBOOK

/* On Powerbooks, put the controller into suspend mode when going
 * to sleep, and do a resume when waking up. */

static int ohci_sleep_notify (struct pmu_sleep_notifier *self, int when)
{
	struct list_head *ohci_l;
	pohci_t ohci;
       
	for (ohci_l = ohci_hcd_list.next; ohci_l != &ohci_hcd_list; ohci_l = ohci_l->next) {
	ohci = list_entry (ohci_l, struct ohci, ohci_hcd_list);

		switch (when) {
		case PBOOK_SLEEP_NOW:
			disable_irq (ohci->irq);
			writel (ohci->hc_control = OHCI_USB_SUSPEND, &ohci->regs->control);
			wait_ms (10);
			break;
		case PBOOK_WAKE:
 			writel (ohci->hc_control = OHCI_USB_RESUME, &ohci->regs->control);
			wait_ms (20);
			writel (ohci->hc_control = 0xBF, &ohci->regs->control);
			enable_irq (ohci->irq);
			break;
		}
	}
	return PBOOK_SLEEP_OK;
}

static struct pmu_sleep_notifier ohci_sleep_notifier = {
       ohci_sleep_notify, SLEEP_LEVEL_MISC,
};
#endif /* CONFIG_PMAC_PBOOK */

/*-------------------------------------------------------------------------*/
 
#ifdef CONFIG_APM
static int handle_apm_event (apm_event_t event) 
{
	static int down = 0;
	pohci_t ohci;
	struct list_head *ohci_l;
	
	switch (event) {
	case APM_SYS_SUSPEND:
	case APM_USER_SUSPEND:
		if (down) {
			printk(KERN_DEBUG MODSTR "received extra suspend event\n");
			break;
		}
		for (ohci_l = ohci_hcd_list.next; ohci_l != &ohci_hcd_list; ohci_l = ohci_l->next) {
			ohci = list_entry (ohci_l, struct ohci, ohci_hcd_list);
			dbg (KERN_DEBUG MODSTR "USB-Bus suspend: %p\n", ohci);
			writel (ohci->hc_control = 0xFF, &ohci->regs->control);
		}
		wait_ms (10);
		down = 1;
		break;
	case APM_NORMAL_RESUME:
	case APM_CRITICAL_RESUME:
		if (!down) {
			printk (KERN_DEBUG MODSTR "received bogus resume event\n");
			break;
		}
		for (ohci_l = ohci_hcd_list.next; ohci_l != &ohci_hcd_list; ohci_l = ohci_l->next) {
			ohci = list_entry(ohci_l, struct ohci, ohci_hcd_list);
			dbg (KERN_DEBUG MODSTR "USB-Bus resume: %p\n", ohci);
			writel (ohci->hc_control = 0x7F, &ohci->regs->control);
		}		
		wait_ms (20);
		for (ohci_l = ohci_hcd_list.next; ohci_l != &ohci_hcd_list; ohci_l = ohci_l->next) {
			ohci = list_entry (ohci_l, struct ohci, ohci_hcd_list);
			writel (ohci->hc_control = 0xBF, &ohci->regs->control);
		}
		down = 0;
		break;
	}
	return 0;
}
#endif

/*-------------------------------------------------------------------------*/

#define PCI_CLASS_SERIAL_USB_OHCI 0x0C0310
 
int ohci_hcd_init (void) 
{
	int ret = -ENODEV;
	struct pci_dev *dev = NULL;
 
	while ((dev = pci_find_class (PCI_CLASS_SERIAL_USB_OHCI, dev))) { 
		if (hc_start_ohci(dev) >= 0) ret = 0;
	}
    
#ifdef CONFIG_APM
	apm_register_callback (&handle_apm_event);
#endif

#ifdef CONFIG_PMAC_PBOOK
	pmu_register_sleep_notifier (&ohci_sleep_notifier);
#endif  
    return ret;
}

/*-------------------------------------------------------------------------*/

#ifdef MODULE
int init_module (void) 
{
	return ohci_hcd_init ();
}

/*-------------------------------------------------------------------------*/

void cleanup_module (void) 
{	
	struct ohci *ohci;
	
#ifdef CONFIG_APM
	apm_unregister_callback (&handle_apm_event);
#endif

#ifdef CONFIG_PMAC_PBOOK
	pmu_unregister_sleep_notifier (&ohci_sleep_notifier);
#endif  

	while (!list_empty (&ohci_hcd_list)) {
		ohci = list_entry (ohci_hcd_list.next, struct ohci, ohci_hcd_list);
		list_del (&ohci->ohci_hcd_list);
		INIT_LIST_HEAD (&ohci->ohci_hcd_list);
		hc_release_ohci (ohci);
	}		
}
#endif //MODULE

