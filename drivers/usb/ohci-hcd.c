/*
 * OHCI HCD (Host Controller Driver) for USB.
 *
 * (C) Copyright 1999 Roman Weissgaerber <weissg@vienna.at>
 *
 * The OHCI HCD layer is a simple but nearly complete implementation of what 
 * the USB people would call a HCD  for the OHCI. 
 * (ISO comming soon, Bulk disabled, INT u. CTRL transfers enabled)
 * The layer on top of it, is for interfacing to the alternate-usb 
 * device-drivers.
 * 
 * [ This is based on Linus' UHCI code and gregs OHCI fragments 
 * (0.03c source tree). ]
 * [ Open Host Controller Interface driver for USB. ]
 * [ (C) Copyright 1999 Linus Torvalds (uhci.c) ]
 * [ (C) Copyright 1999 Gregory P. Smith <greg@electricrain.com> ]
 * [ $Log: ohci.c,v $ ]
 * [ Revision 1.1  1999/04/05 08:32:30  greg ]
 * 
 * 
 * v2.1 1999/05/09 ep_addr correction, code clean up
 * v2.0 1999/05/04 
 * virtual root hub is now an option, 
 * memory allocation based on kmalloc and kfree now, Bus error handling, 
 * INT and CTRL transfers enabled, Bulk included but disabled, ISO needs completion
 * 
 * from Linus Torvalds (uhci.c) (APM not tested; hub, usb_device, bus and related stuff)
 * from Greg Smith (ohci.c) (reset controller handling, hub)
 * 
 * v1.0 1999/04/27 initial release
 * ohci-hcd.c
 */

/* #define OHCI_DBG  */  /* printk some debug information */

 
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

#include <asm/spinlock.h>
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

 
static DECLARE_WAIT_QUEUE_HEAD(bulk_wakeup);
static DECLARE_WAIT_QUEUE_HEAD(control_wakeup);
static DECLARE_WAIT_QUEUE_HEAD(root_hub);
 
static __u8 cc_to_status[16] = { /* mapping of the OHCI CC to the UHCI status codes; first guess */
/* Activ, Stalled, Data Buffer Err, Babble Detected : NAK recvd, CRC/Timeout, Bitstuff, reservd */
/* No  Error  */		0x00,
/* CRC Error  */		0x04,
/* Bit Stuff  */		0x02,
/* Data Togg  */ 		0x40,
/* Stall      */		0x40,
/* DevNotResp */		0x04,
/* PIDCheck   */		0x04,
/* UnExpPID   */		0x40,
/* DataOver   */		0x20,
/* DataUnder  */ 		0x20,
/* reservd    */		0x40,
/* reservd    */		0x40,
/* BufferOver */		0x20,
/* BuffUnder  */		0x20,
/* Not Access */		0x80,
/* Not Access */		0x80
 };
 
 
/********
 **** Interface functions
 ***********************************************/
 
static int sohci_int_handler(void * ohci_in, unsigned int ep_addr, int ctrl_len, void * ctrl, void * data, int data_len, int status, __OHCI_BAG lw0, __OHCI_BAG lw1)
{

	struct ohci * ohci = ohci_in; 
	usb_device_irq handler=(void *) lw0;
	void *dev_id = (void *) lw1;
	int ret;
	
	OHCI_DEBUG({ int i; printk("USB HC IRQ <<<: %x: data(%d):", ep_addr, data_len);)
	OHCI_DEBUG( for(i=0; i < data_len; i++ ) printk(" %02x", ((__u8 *) data)[i]);)
	OHCI_DEBUG( printk(" ret_status: %x\n", status); })
	
 	ret = handler(cc_to_status[status & 0xf], data, dev_id);
	if(ret == 0) return 0; /* 0 .. do not requeue  */
	if(status > 0) return -1; /* error occured do not requeue ? */
	ohci_trans_req(ohci, ep_addr, 0, NULL, data, 8, (__OHCI_BAG) handler, (__OHCI_BAG) dev_id); /* requeue int request */
	return 0;
}

static int sohci_ctrl_handler(void * ohci_in, unsigned int ep_addr, int ctrl_len, void * ctrl, void * data, int data_len, int status, __OHCI_BAG lw0, __OHCI_BAG lw)
{  
	*(int * )lw0 = status;
	wake_up(&control_wakeup);

	OHCI_DEBUG( { int i; printk("USB HC CTRL<<<: %x: ctrl(%d):", ep_addr, ctrl_len);)
	OHCI_DEBUG( for(i=0; i < 8; i++ ) printk(" %02x", ((__u8 *) ctrl)[i]);)
	OHCI_DEBUG( printk(" data(%d):", data_len);) 
	OHCI_DEBUG( for(i=0; i < data_len; i++ ) printk(" %02x", ((__u8 *) data)[i]);)
	OHCI_DEBUG( printk(" ret_status: %x\n", status); })
    return 0;                       
} 

static int sohci_bulk_handler(void * ohci_in, unsigned int ep_addr, int ctrl_len, void * ctrl, void * data, int data_len, int status, __OHCI_BAG lw0, __OHCI_BAG lw)
{  
	*(int * )lw0 = status;
	wake_up(&bulk_wakeup);

	OHCI_DEBUG( { int i; printk("USB HC BULK<<<: %x:", ep_addr, ctrl_len);)
	OHCI_DEBUG( printk(" data(%d):", data_len);) 
	OHCI_DEBUG( for(i=0; i < data_len; i++ ) printk(" %02x", ((__u8 *) data)[i]);)
	OHCI_DEBUG( printk(" ret_status: %x\n", status); })
    return 0;                       
}
                                                           
static int sohci_request_irq(struct usb_device *usb_dev, unsigned int pipe, usb_device_irq handler, int period, void *dev_id)
{
	struct ohci * ohci = usb_dev->bus->hcpriv;
	union ep_addr_ ep_addr;
	
	ep_addr.iep = 0;
	ep_addr.bep.ep = ((pipe >> 15) & 0x0f) 		/* endpoint address */
				| (pipe  & 0x80)  	 			/* direction */
				| (1 << 5);						/* type = int*/
	ep_addr.bep.fa = ((pipe >> 8) & 0x7f);			/* device address */

	OHCI_DEBUG( printk("USB HC IRQ >>>: %x: every %d ms\n", ep_addr.iep, period);) 
	
	usb_ohci_add_ep(ohci, ep_addr.iep, period, 1, sohci_int_handler, 1 << ((pipe & 0x03) + 3) , (pipe >> 26) & 0x01);
	
   	ohci_trans_req(ohci, ep_addr.iep, 0, NULL, ((struct ohci_device *) usb_dev->hcpriv)->data, 8, (__OHCI_BAG) handler, (__OHCI_BAG) dev_id);
	return 0;
}


static int sohci_control_msg(struct usb_device *usb_dev, unsigned int pipe, void *cmd, void *data, int len)
{
	DECLARE_WAITQUEUE(wait, current);
	struct ohci * ohci = usb_dev->bus->hcpriv;
	int status;
	union ep_addr_ ep_addr;

    ep_addr.iep = 0;
	ep_addr.bep.ep = ((pipe >> 15) & 0x0f) 		/* endpoint address */		
				| (pipe  & 0x80)  				/* direction */
				|  (1 << 6);					/* type = ctrl*/
	ep_addr.bep.fa	= ((pipe >> 8) & 0x7f);				/* device address */
	
	status = 0xf; /* CC not Accessed */
	OHCI_DEBUG( { int i; printk("USB HC CTRL>>>: %x: ctrl(%d):", ep_addr.iep, 8);)
	OHCI_DEBUG( for(i=0; i < 8; i++ ) printk(" %02x", ((__u8 *) cmd)[i]);)
	OHCI_DEBUG( printk(" data(%d):", len);) 
	OHCI_DEBUG( for(i=0; i < len; i++ ) printk(" %02x", ((__u8 *) data)[i]);)
	OHCI_DEBUG( printk("\n"); })
	
	usb_ohci_add_ep(ohci, ep_addr.iep, 0, 1, sohci_ctrl_handler, 1 << ((pipe & 0x03) + 3) , (pipe >> 26) & 0x01);
	
	current->state = TASK_UNINTERRUPTIBLE;
    add_wait_queue(&control_wakeup, &wait);   
 
	ohci_trans_req(ohci, ep_addr.iep, 8, cmd, data, len, (__OHCI_BAG) &status, 0);

	schedule_timeout(HZ/10);

    remove_wait_queue(&control_wakeup, &wait); 
     
    OHCI_DEBUG(printk("USB HC status::: %x\n", cc_to_status[status & 0x0f]);)
     
	return cc_to_status[status & 0x0f];
}

static int sohci_bulk_msg(struct usb_device *usb_dev, unsigned int pipe, void *data, int len)
{
	DECLARE_WAITQUEUE(wait, current);
	struct ohci * ohci = usb_dev->bus->hcpriv;
	int status;
	union ep_addr_ ep_addr;

    ep_addr.iep = 0;
	ep_addr.bep.ep = ((pipe >> 15) & 0x0f) 		/* endpoint address */		
				| (pipe  & 0x80)  				/* direction */
				|  (11 << 5);					/* type = bulk*/
	ep_addr.bep.fa	= ((pipe >> 8) & 0x7f);				/* device address */
	
	status = 0xf; /* CC not Accessed */
	OHCI_DEBUG( { int i; printk("USB HC BULK>>>: %x:", ep_addr.iep);)
	OHCI_DEBUG( printk(" data(%d):", len);) 
	OHCI_DEBUG( for(i=0; i < len; i++ ) printk(" %02x", ((__u8 *) data)[i]);)
	OHCI_DEBUG( printk("\n"); })
	
	usb_ohci_add_ep(ohci, ep_addr.iep, 0, 1, sohci_bulk_handler, 1 << ((pipe & 0x03) + 3) , (pipe >> 26) & 0x01);
	
	current->state = TASK_UNINTERRUPTIBLE;
    add_wait_queue(&bulk_wakeup, &wait);   
 
	ohci_trans_req(ohci, ep_addr.iep, 0, NULL, data, len, (__OHCI_BAG) &status, 0);

	schedule_timeout(HZ/10);

    remove_wait_queue(&bulk_wakeup, &wait); 
     
    OHCI_DEBUG(printk("USB HC status::: %x\n", cc_to_status[status & 0x0f]);)
     
	return cc_to_status[status & 0x0f];
}

static int sohci_usb_deallocate(struct usb_device *usb_dev) {
    struct ohci_device *dev = usb_to_ohci(usb_dev); 
	union ep_addr_ ep_addr;

	ep_addr.iep = 0;
	
	OHCI_DEBUG(printk("USB HC dealloc %x\n", usb_dev->devnum);)

	/* wait_ms(20); */

	if(usb_dev->devnum >=0) {
		ep_addr.bep.fa = usb_dev->devnum;
    	usb_ohci_rm_function(((struct ohci_device *)usb_dev->hcpriv)->ohci, ep_addr.iep);
	}
	
    USB_FREE(dev);
	USB_FREE(usb_dev);

	return 0;
}

static struct usb_device *sohci_usb_allocate(struct usb_device *parent) {
 
	struct usb_device *usb_dev;
	struct ohci_device *dev;
	
	
	USB_ALLOC(usb_dev, sizeof(*usb_dev));
	if (!usb_dev)
		return NULL;
		
	memset(usb_dev, 0, sizeof(*usb_dev));

	USB_ALLOC(dev, sizeof(*dev));
	if (!dev) {
		USB_FREE(usb_dev);
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


struct usb_operations sohci_device_operations = {
	sohci_usb_allocate,
	sohci_usb_deallocate,
	sohci_control_msg,
	sohci_bulk_msg,
	sohci_request_irq,
};

 
/******
 *** ED handling functions
 ************************************/



/* 
 * search for the right place to insert an interrupt ed into the int tree 
 * do some load ballancing
 * */

static int usb_ohci_int_ballance(struct ohci * ohci, int interval, int load) {

  int i,j;
  
  j = 0;   /* search for the least loaded interrupt endpoint branch of all 32 branches */
  for(i=0; i< 32; i++) if(ohci->ohci_int_load[j] > ohci->ohci_int_load[i]) j=i; 

  if(interval < 1) interval = 1;
  if(interval > 32) interval = 32;
  for(i= 0; ((interval >> i) > 1 ); interval &= (0xfffe << i++ )); /* interval = 2^int(ld(interval)) */
  

  for(i=j%interval; i< 32; i+=interval) ohci->ohci_int_load[i] += load;
  j = interval + (j % interval); 
  
  OHCI_DEBUG(printk("USB HC new int ed on pos : %x \n",j);)
  
  return j;
}

/* get the ed from the endpoint / device adress */

struct usb_ohci_ed * ohci_find_ep(struct ohci *ohci, unsigned int ep_addr_in) {

union ep_addr_ ep_addr;
struct usb_ohci_ed *tmp;
unsigned int mask;

mask = 0;
ep_addr.iep = ep_addr_in;

#ifdef VROOTHUB
 if(ep_addr.bep.fa == ohci->root_hub_funct_addr) {
 	if((ep_addr.bep.ep & 0x0f) == 0)
 		return &ohci->ed_rh_ep0; /* root hub ep0 */
 	else
 		return &ohci->ed_rh_epi; /* root hub int ep */
 }
#endif

 tmp = ohci->ed_func_ep0[ep_addr.bep.fa];
 mask = ((((ep_addr.bep.ep >> 5) & 0x03)==2)?0x7f:0xff); 
 ep_addr.bep.ep &= mask; /* mask out direction of ctrl ep */
 
  while (tmp != NULL) {
    if (tmp->ep_addr.iep == ep_addr.iep) 
      return tmp;
    tmp = tmp->ed_list;
  }
  return NULL;
}
 
spinlock_t usb_ed_lock = SPIN_LOCK_UNLOCKED;
/* add a new endpoint ep_addr */
struct usb_ohci_ed *usb_ohci_add_ep(struct ohci * ohci, unsigned int ep_addr_in, int interval, int load, f_handler handler, int ep_size, int speed) {
   
  struct usb_ohci_ed * ed; 
  struct usb_ohci_td * td;
  union ep_addr_ ep_addr;
 
   
  int int_junk;
 
  struct usb_ohci_ed *tmp;

  ep_addr.iep = ep_addr_in ;
  ep_addr.bep.ep &= ((((ep_addr.bep.ep >> 5) & 0x03)==2)?0x7f:0xff); /* mask out direction of ctrl ep */

  spin_lock(&usb_ed_lock);
  
  tmp =  ohci_find_ep(ohci, ep_addr.iep);
  if (tmp != NULL) { 
  
#ifdef VROOTHUB
  	if(ep_addr.bep.fa == ohci->root_hub_funct_addr) {
 		if((ep_addr.bep.ep & 0x0f) != 0) { /* root hub int ep */
			ohci->ed_rh_epi.handler = handler;
 			ohci_init_rh_int_timer(ohci, interval);
 		}
 		else { /* root hub ep0 */
 			 ohci->ed_rh_ep0.handler = handler;
 		}			 	
 	}

 	else 
#endif 

 		{
 		tmp->hw.info = ep_addr.bep.fa | ((ep_addr.bep.ep & 0xf) <<7)
  		
    	| (((ep_addr.bep.ep & 0x60) == 0)? 0x8000 : 0)
    	| (speed << 13)
    	| ep_size <<16;
 
  		tmp->handler = handler; 
  	}
 	spin_unlock(&usb_ed_lock);
    return tmp;    /* ed  already in use */
  }
  
  
  OHCI_ALLOC(td, sizeof(td)); /* dummy td; end of td list for ed */
  OHCI_ALLOC(ed, sizeof(ed));
  td->prev_td = NULL;
  
  ed->hw.tail_td = virt_to_bus(&td->hw);
  ed->hw.head_td = ed->hw.tail_td;
  ed->hw.info = ep_addr.bep.fa | ((ep_addr.bep.ep & 0xf) <<7)
  /*  | ((ep_addr.bep.port & 0x80)? 0x1000 : 0x0800 ) */
    | (((ep_addr.bep.ep & 0x60) == 0)? 0x8000 : 0)
    | (speed << 13)
    | ep_size <<16;
  
  ed->handler = handler;

  switch((ep_addr.bep.ep >> 5) & 0x03) {
  case CTRL:
    ed->hw.next_ed = 0;
    if(ohci->ed_controltail == NULL) {
      writel(virt_to_bus(&ed->hw), &ohci->regs->ed_controlhead);
    }
    else {
      ohci->ed_controltail->hw.next_ed = virt_to_bus(&ed->hw);
    }
    ed->ed_prev = ohci->ed_controltail;
    ohci->ed_controltail = ed;	  
    break;
  case BULK:  
    ed->hw.next_ed = 0;
    if(ohci->ed_bulktail == NULL) {
      writel(virt_to_bus(&ed->hw), &ohci->regs->ed_bulkhead);
    }
    else {
      ohci->ed_bulktail->hw.next_ed = virt_to_bus(&ed->hw);
    }
    ed->ed_prev = ohci->ed_bulktail;
    ohci->ed_bulktail = ed;	  
    break;
  case INT:
    int_junk = usb_ohci_int_ballance(ohci, interval, load);
    ed->hw.next_ed = ohci->hc_area->ed[int_junk].next_ed; 
    ohci->hc_area->ed[int_junk].next_ed = virt_to_bus(&ed->hw);
    ed->ed_prev = (struct usb_ohci_ed *) &ohci->hc_area->ed[int_junk];
    break;
  case ISO:
    ed->hw.next_ed = 0;
    ohci->ed_isotail->hw.next_ed = virt_to_bus(&ed->hw);
    ed->ed_prev = ohci->ed_isotail;
    ohci->ed_isotail = ed;	  
    break;
  }
  ed->ep_addr  = ep_addr;
  
  /* Add it to the "hash"-table of known endpoint descriptors */      
  
  ed->ed_list = ohci->ed_func_ep0[ed->ep_addr.bep.fa];
  ohci->ed_func_ep0[ed->ep_addr.bep.fa] = ed; 

  spin_unlock(&usb_ed_lock);
  
  OHCI_DEBUG(printk("USB HC new ed %x: %x :", ep_addr.iep, (unsigned int ) ed); )
  OHCI_DEBUG({ int i; for( i= 0; i<8 ;i++) printk(" %4x", ((unsigned int *) ed)[i]) ; printk("\n"); }; )
  return 0;
}

/*****
 * Request the removal of an endpoint
 * 
 * put the ep on the rm_list and request a stop of the bulk or ctrl list 
 * real removal is done at the next start of frame hardware interrupt 
 */
int usb_ohci_rm_ep(struct ohci * ohci, struct  usb_ohci_ed *ed)
{    
  unsigned int flags;
  struct usb_ohci_ed *tmp;
  
  OHCI_DEBUG(printk("USB HC remove ed %x: %x :\n", ed->ep_addr.iep, (unsigned int ) ed); )

  spin_lock_irqsave(&usb_ed_lock, flags);

  tmp = ohci->ed_func_ep0[ed->ep_addr.bep.fa];
  if (tmp == NULL) {
  	spin_unlock_irqrestore(&usb_ed_lock, flags);
    return 0;
  }

  if(tmp == ed) {
     ohci->ed_func_ep0[ed->ep_addr.bep.fa] = ed->ed_list;
  }
  else {
    while (tmp->ed_list != ed) {
      if (tmp->ed_list == NULL) {
        spin_unlock_irqrestore(&usb_ed_lock, flags);
		return 0;
	  }
      tmp = tmp->ed_list;
    }
    tmp->ed_list = ed->ed_list;
  }
  ed->ed_list = ohci->ed_rm_list;
  ohci->ed_rm_list = ed;
  ed->hw.info  |=  OHCI_ED_SKIP;

  switch((ed->ep_addr.bep.ep >> 5) & 0x03) {
  	case CTRL:
    	writel_mask(~(0x01<<4), &ohci->regs->control); /* stop CTRL list */
    	break;
  	case BULK:
    	writel_mask(~(0x01<<5), &ohci->regs->control); /* stop BULK list */
    	break;
  }


  writel( OHCI_INTR_SF, &ohci->regs->intrenable); /* enable sof interrupt */

  spin_unlock_irqrestore(&usb_ed_lock, flags);

  return 1;
}

/* we have requested to stop the bulk or CTRL list,
 * now we can remove the eds on the rm_list */
 
static int ohci_rm_eds(struct ohci * ohci) {

  unsigned int flags;
  struct usb_ohci_ed *ed;
  struct usb_ohci_ed *ed_tmp;
  struct usb_ohci_td *td;
  __u32 td_hw_tmp;
  __u32 td_hw;
  
  spin_lock_irqsave(&usb_ed_lock, flags);

  ed = ohci->ed_rm_list;

  while (ed != NULL) {
    
    switch((ed->ep_addr.bep.ep >> 5) & 0x03) {
    case CTRL: 
      if(ed->ed_prev == NULL) {
		writel(ed->hw.next_ed, &ohci->regs->ed_controlhead);
      }
      else {
		ed->ed_prev->hw.next_ed = ed->hw.next_ed;
      }
      if(ohci->ed_controltail == ed) {
		ohci->ed_controltail = ed->ed_prev;
      }
      break;
    case BULK: 
      if(ed->ed_prev == NULL) {
		writel(ed->hw.next_ed, &ohci->regs->ed_bulkhead);
      }
      else {
		ed->ed_prev->hw.next_ed = ed->hw.next_ed;
      }
      if(ohci->ed_bulktail == ed) {
		ohci->ed_bulktail = ed->ed_prev;
      }
      break;
    case INT: 
      	ed->ed_prev->hw.next_ed = ed->hw.next_ed;
      break;
    case ISO:
      ed->ed_prev->hw.next_ed = ed->hw.next_ed;
      if(ohci->ed_isotail == ed) {
		ohci->ed_isotail = ed->ed_prev;
      }
      break;
    }
    
    if(ed->hw.next_ed != 0) ((struct usb_ohci_ed *) bus_to_virt(ed->hw.next_ed))->ed_prev = ed->ed_prev;
    

/* tds directly connected to ed */
 
	td_hw = ed->hw.head_td & 0xfffffff0;
    while(td_hw != 0) {
    	td = bus_to_virt(td_hw);
    	td_hw_tmp = td_hw;
    	td_hw = td->hw.next_td;
    	OHCI_FREE(td); /* free pending tds */ 
    	if(td_hw_tmp == ed->hw.tail_td) break;
    	
    }
     
    /* mark TDs on the hc done list (if there are any) */      
	td_hw = readl(&ohci->regs->donehead) & 0xfffffff0;
    while(td_hw != 0) {
    	td = bus_to_virt(td_hw);
    	td_hw = td->hw.next_td;
    	if(td->ep == ed) td->ep = 0;
   	}
 
    /* mark TDs on the hcca done list (if there are any) */      
	td_hw = ohci->hc_area->hcca.done_head & 0xfffffff0 ;
	
    while(td_hw != 0) { 
    	td = bus_to_virt(td_hw); 
    	td_hw = td->hw.next_td;
    	if(td->ep == ed) td->ep = 0;
   	}

    ed_tmp = ed;
    ed = ed->ed_list;
    OHCI_FREE(ed_tmp);  /* free ed */ 
  }
  writel(0, &ohci->regs->ed_controlcurrent); /* reset CTRL list */
  writel(0, &ohci->regs->ed_bulkcurrent);    /* reset BULK list */
  writel_set((0x03<<4), &ohci->regs->control);   /* start CTRL u. (BULK list) */ 

  spin_unlock_irqrestore(&usb_ed_lock, flags);
  
  ohci->ed_rm_list = NULL;
  OHCI_DEBUG(printk("USB HC after rm ed control: %4x  intrstat: %4x intrenable: %4x\n", readl(&ohci->regs->control),readl(&ohci->regs->intrstatus),readl(&ohci->regs->intrenable));)
 

  return 0;
}

/* remove all endpoints of a function (device) */
int usb_ohci_rm_function( struct ohci * ohci, unsigned int ep_addr_in)
{    
  struct usb_ohci_ed *ed;
  struct usb_ohci_ed *tmp;
  union ep_addr_ ep_addr;

 
  
  ep_addr.iep = ep_addr_in;

  for(ed = ohci->ed_func_ep0[ep_addr.bep.fa]; ed != NULL;) {
	tmp = ed;
    ed = ed->ed_list;
    usb_ohci_rm_ep(ohci, tmp);
  }
 

  
  return 1;
}





/******
 *** TD handling functions
 ************************************/


#define FILL_TD(TD_PT, HANDLER, INFO, DATA, LEN, LW0, LW1)  \
    td_pt = (TD_PT); \
    td_pt1 = (struct usb_ohci_td *) bus_to_virt(usb_ep->hw.tail_td); \
    td_pt1->ep = usb_ep; \
    td_pt1->handler = (HANDLER); \
    td_pt1->buffer_start = (DATA); \
    td_pt1->lw0 = (LW0); \
    td_pt1->lw1 = (LW1); \
    td_pt1->hw.info = (INFO); \
    td_pt1->hw.cur_buf = virt_to_bus(DATA); \
    td_pt1->hw.buf_end = td_pt1->hw.cur_buf + (LEN) - 1; \
    td_pt1->hw.next_td = virt_to_bus(td_pt); \
    usb_ep->hw.tail_td = virt_to_bus(td_pt); \
    td_pt->prev_td = td_pt1; \
    td_pt->hw.next_td = 0

spinlock_t usb_req_lock = SPIN_LOCK_UNLOCKED;

int ohci_trans_req(struct ohci * ohci, unsigned int ep_addr, int ctrl_len, void  *ctrl, void * data, int data_len, __OHCI_BAG lw0, __OHCI_BAG lw1) {

  int ed_type;
  unsigned int flags;
  struct usb_ohci_td *td_pt;
  struct usb_ohci_td *td_pt1;
  struct usb_ohci_td *td_pt_a1, *td_pt_a2, *td_pt_a3;
  struct usb_ohci_ed *usb_ep;
  f_handler handler;

  
 td_pt_a1 =NULL;
 td_pt_a2 =NULL;
 td_pt_a3 =NULL;
  
  usb_ep = ohci_find_ep(ohci, ep_addr);
  if(usb_ep == NULL ) return -1; /* not known ep */
  
  handler = usb_ep->handler;
 
#ifdef VROOTHUB 
  if(usb_ep == &ohci->ed_rh_ep0) { /* root hub ep 0 control endpoint */  
    root_hub_control_msg(ohci, 8, ctrl, data, data_len, lw0, lw1, handler);
    return 0;
  }

  if(usb_ep == &ohci->ed_rh_epi) { /* root hub interrupt endpoint */
  
    root_hub_int_req(ohci, 8, ctrl, data, data_len, lw0, lw1, handler);  
	return 0;
  }
#endif
  /*  struct usb_ohci_ed * usb_ep = usb_ohci_add_ep(pipe, ohci, interval, 1); */
 
   ed_type = ((((union ep_addr_)ep_addr).bep.ep >> 5) & 0x07);
  
  switch(ed_type) {
  case BULK_IN:
  case BULK_OUT:   
  case INT_IN:
  case INT_OUT:    
    OHCI_ALLOC(td_pt_a1, sizeof(td_pt_a1));
    break;

  case CTRL_IN:
  case CTRL_OUT:
    OHCI_ALLOC(td_pt_a1, sizeof(td_pt_a1));
    OHCI_ALLOC(td_pt_a3, sizeof(td_pt_a3));  
    if(data_len > 0) {
      OHCI_ALLOC(td_pt_a2, sizeof(td_pt_a2));
    }
    break;

  case ISO_IN:
  case ISO_OUT:

  }
    
  spin_lock_irqsave(&usb_req_lock, flags);
  
  switch(ed_type) {
  case BULK_IN:
    FILL_TD( td_pt_a1, handler, TD_CC | TD_R | TD_DP_IN | TD_T_TOGGLE, data, data_len, lw0, lw1 );
    writel( OHCI_BLF, &ohci->regs->cmdstatus); /* start bulk list */
    break;

  case BULK_OUT: 
    FILL_TD( td_pt_a1, handler, TD_CC | TD_DP_OUT | TD_T_TOGGLE, data, data_len, lw0, lw1 );
    writel( OHCI_BLF, &ohci->regs->cmdstatus); /* start bulk list */
    break;

  case INT_IN: 
    FILL_TD( td_pt_a1, handler, TD_CC | TD_R | TD_DP_IN | TD_T_TOGGLE, data, data_len, lw0, lw1 );  
    break;

  case INT_OUT: 
    FILL_TD( td_pt_a1, handler, TD_CC | TD_DP_OUT | TD_T_TOGGLE, data, data_len, lw0, lw1 );
    break;

  case CTRL_IN: 
    FILL_TD( td_pt_a1, NULL, TD_CC | TD_DP_SETUP | TD_T_DATA0, ctrl, ctrl_len, 0, 0 );  
    if(data_len > 0) { 
      FILL_TD( td_pt_a2, NULL, TD_CC | TD_R |  TD_DP_IN | TD_T_DATA1, data, data_len, 0, 0 );  
    } 
    FILL_TD( td_pt_a3, handler, TD_CC | TD_DP_OUT | TD_T_DATA1, NULL, 0, lw0, lw1 );
    writel( OHCI_CLF, &ohci->regs->cmdstatus); /* start Control list */
    break;

  case CTRL_OUT:     
    FILL_TD( td_pt_a1, NULL, TD_CC | TD_DP_SETUP | TD_T_DATA0, ctrl, ctrl_len, 0, 0 );  
    if(data_len > 0) { 
      FILL_TD( td_pt_a2, NULL, TD_CC | TD_R | TD_DP_OUT | TD_T_DATA1, data, data_len, 0, 0 );  
    } 
    FILL_TD( td_pt_a3, handler, TD_CC | TD_DP_IN | TD_T_DATA1, NULL, 0, lw0, lw1 );
    writel( OHCI_CLF, &ohci->regs->cmdstatus); /* start Control list */
    break;

  case ISO_IN:
  case ISO_OUT:
    break;
  }


  

 td_pt1 = (struct usb_ohci_td *) bus_to_virt(usb_ep->hw.tail_td); 

 
  if(td_pt_a3 != NULL) td_pt_a3->prev_td = NULL;
  	else if (td_pt_a2 != NULL) td_pt_a2->prev_td = NULL;
  	  	else if (td_pt_a1 != NULL) td_pt_a1->prev_td = NULL;
  	  
  spin_unlock_irqrestore(&usb_req_lock, flags);
  return 0;
}


/******
 *** Done List handling functions
 ************************************/

/* replies to the request have to be on a FIFO basis so
 * we reverse the reversed done-list */
 
static struct usb_ohci_td * ohci_reverse_done_list(struct ohci * ohci) {

  __u32 td_list_hc;
  struct usb_ohci_td      * td_list = NULL;
  struct usb_ohci_td      * td_rev = NULL;
  	
	td_list_hc = ohci->hc_area->hcca.done_head & 0xfffffff0;
    ohci->hc_area->hcca.done_head = 0;
    
 	while(td_list_hc) {
		
		td_list = (struct  usb_ohci_td *) bus_to_virt(td_list_hc);
		td_list->next_dl_td = td_rev;
			
		td_rev = td_list;
		td_list_hc = td_list->hw.next_td & 0xfffffff0;
	}
 return td_list;
}

/* all done requests are replied here */
static int usb_ohci_done_list(struct ohci *  ohci) {

  struct usb_ohci_td      * td = NULL;
  struct usb_ohci_td      * td_list; 
  struct usb_ohci_td      * td_list_next = NULL;
  struct usb_ohci_td      * td_err = NULL;
	__u32 td_hw;
 
	
  td_list = ohci_reverse_done_list(ohci);

  while(td_list) {
   	td_list_next = td_list->next_dl_td;
    td = td_list;

    if(td->ep == NULL) { /* removed ep */
   		OHCI_FREE(td_list);
   		break;
   	}
   	
   	/* the HC halts an ED if an error occurs; put all pendings TDs of an halted ED on the
   	 * done list; they are marked with an 0xf CC_error code 
   	 */
   	 
 	if(TD_CC_GET(td_list->hw.info) != TD_CC_NOERROR) { /* on error move all pending tds of an ed into the done list */
 		printk("******* USB BUS error %x @ep %x\n", TD_CC_GET(td_list->hw.info), td_list->ep->ep_addr.iep);
    td_err= td_list;
	td_hw = td_list->ep->hw.head_td & 0xfffffff0;
    while(td_hw != 0) { 
    	if(td_hw == td_list->ep->hw.tail_td) break;
    	td = bus_to_virt(td_hw);
    	td_err->next_dl_td = td;
    	td_err= td;
    	td_hw = td->hw.next_td;
    }
    td_list->ep->hw.head_td = td_list->ep->hw.tail_td;
    td->next_dl_td = td_list_next;
    td_list_next = td_list->next_dl_td;
 	
    }
  /*  send the reply  */
 	if(td_list->handler != NULL)  {
    	if(td_list->prev_td == NULL) {
			td_list->handler((void *) ohci,
						td_list->ep->ep_addr.iep,
						0, 
						NULL, 
						td_list->buffer_start, 
						td_list->hw.buf_end-virt_to_bus(td_list->buffer_start)+1, 
						TD_CC_GET(td_list->hw.info),
						td_list->lw0,
						td_list->lw1);
			OHCI_FREE(td_list);			
   	  	}
      	else {
      		if(td_list->prev_td->prev_td == NULL) { /* cntrl 2 Transactions dataless */
	  		td_list->handler((void *) ohci,
	  		            td_list->ep->ep_addr.iep,
	  					td_list->prev_td->hw.buf_end-virt_to_bus(td_list->prev_td->buffer_start)+1,
	  					td_list->prev_td->buffer_start, 
	  					NULL,
	  					0, 
	  					(TD_CC_GET(td_list->prev_td->hw.info) > 0) ? TD_CC_GET(td_list->prev_td->hw.info) : TD_CC_GET(td_list->hw.info),
						td_list->lw0,
						td_list->lw1);
			OHCI_FREE(td_list->prev_td);
			OHCI_FREE(td_list);
			}
			else { /* cntrl 3 Transactions */
	  			td_list->handler((void *) ohci,
	  					td_list->ep->ep_addr.iep, 
	  					td_list->prev_td->prev_td->hw.buf_end-virt_to_bus(td_list->prev_td->prev_td->buffer_start)+1, 
	  					td_list->prev_td->prev_td->buffer_start, 
	  					td_list->prev_td->buffer_start, 
	  					td_list->prev_td->hw.buf_end-virt_to_bus(td_list->prev_td->buffer_start)+1, 
	  					(TD_CC_GET(td_list->prev_td->prev_td->hw.info) > 0) ? TD_CC_GET(td_list->prev_td->prev_td->hw.info)
	  						: (TD_CC_GET(td_list->prev_td->hw.info) > 0) ? TD_CC_GET(td_list->prev_td->hw.info) : TD_CC_GET(td_list->hw.info),
						td_list->lw0,
						td_list->lw1); 
				OHCI_FREE(td_list->prev_td->prev_td);			
				OHCI_FREE(td_list->prev_td);
				OHCI_FREE(td_list);
					
			}
      	} 
    	
    }
    td_list = td_list_next;
  }  
  return 0; 
}



/******
 *** HC functions
 ************************************/
 

 
void reset_hc(struct ohci *ohci) {
	int retries = 5;
	int timeout = 30;
	int fminterval;
	
	if(readl(&ohci->regs->control) & 0x100) { /* SMM owns the HC */
		writel(0x08,  &ohci->regs->cmdstatus); /* request ownership */
		printk("USB HC TakeOver from SMM\n");
		do {
			wait_ms(100);
			if(--retries) { 
				printk("USB HC TakeOver timed out!\n");
				break;
			}
		}
		while(readl(&ohci->regs->control) & 0x100); 
	}	
		
	writel((1<<31), &ohci->regs->intrdisable); /* Disable HC interrupts */
	OHCI_DEBUG(printk("USB HC reset_hc: %x ; retries: %d\n", readl(&ohci->regs->control), 5-retries);)
	fminterval = readl(&ohci->regs->fminterval) & 0x3fff;
	writel(1,  &ohci->regs->cmdstatus);	   /* HC Reset */
	while ((readl(&ohci->regs->cmdstatus) & 0x01) != 0) { /* 10us Reset */
		if (--timeout == 0) {
			printk("USB HC reset timed out!\n");
			return;
		}	
		udelay(1);
	}
	/* set the timing */
	fminterval |= (((fminterval -210) * 6)/7)<<16; 
	writel(fminterval, &ohci->regs->fminterval);
	writel(((fminterval&0x3fff)*9)/10, &ohci->regs->periodicstart);
}


/*
 * Reset and start an OHCI controller
 */
void start_hc(struct ohci *ohci)
{
   /*  int fminterval; */
    unsigned int mask;
    int port_nr;
  /*  fminterval = readl(&ohci->regs->fminterval) & 0x3fff;
	reset_hc(ohci); */
 

	writel(virt_to_bus(&ohci->hc_area->hcca), &ohci->regs->hcca); /* a reset clears this */
 
	/* Choose the interrupts we care about now, others later on demand */
	mask = OHCI_INTR_MIE | OHCI_INTR_WDH;
	/* | OHCI_INTR_SO | OHCI_INTR_UE |OHCI_INTR_RHSC |OHCI_INTR_SF|  
		OHCI_INTR_FNO */
		
	if(readl(&ohci->regs->roothub.a) & 0x100) /* global power on */
 		writel( 0x10000, &ohci->regs->roothub.status); /* root hub power on */
	else { /* port power on */
 		for(port_nr=0; port_nr < (ohci->regs->roothub.a & 0xff); port_nr++)
	     		 writel(0x100, &ohci->regs->roothub.portstatus[port_nr]);
	}
	wait_ms(50); 		
	 
	writel((0x00), &ohci->regs->control); /* USB Reset BUS */
	wait_ms(10);
	  
	writel((0xB7), &ohci->regs->control); /* USB Operational  */
 		
	OHCI_DEBUG(printk("USB HC rstart_hc_operational: %x\n", readl(&ohci->regs->control)); )
 	OHCI_DEBUG(printk("USB HC roothubstata: %x \n", readl( &(ohci->regs->roothub.a) )); )
 	OHCI_DEBUG(printk("USB HC roothubstatb: %x \n", readl( &(ohci->regs->roothub.b) )); )
 	OHCI_DEBUG(printk("USB HC roothubstatu: %x \n", readl( &(ohci->regs->roothub.status) )); )
 	OHCI_DEBUG(printk("USB HC roothubstat1: %x \n", readl( &(ohci->regs->roothub.portstatus[0]) )); )
   	OHCI_DEBUG(printk("USB HC roothubstat2: %x \n", readl( &(ohci->regs->roothub.portstatus[1]) )); )
  
	/* control_wakeup = NULL; */
	writel(mask, &ohci->regs->intrenable);
	writel(mask, &ohci->regs->intrstatus);
 
#ifdef VROOTHUB
	{

	struct usb_device * usb_dev;
	struct ohci_device *dev;
	struct ohci_device *tmp_root_hub= usb_to_ohci(ohci->bus->root_hub);
	usb_dev = sohci_usb_allocate(tmp_root_hub->usb);
	dev = usb_dev->hcpriv; 

	dev->ohci = ohci; 

	usb_connect(usb_dev);

	tmp_root_hub->usb->children[0] = usb_dev;

	usb_new_device(usb_dev);
	}
#endif

 
	
}




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
   
  ohci->intrstatus |= ints;
  OHCI_DEBUG(printk("USB HC interrupt: %x (%x) \n", ints, readl(&ohci->regs->intrstatus));)

  /* ints &= ~(OHCI_INTR_WDH);  WH Bit will be set by done list subroutine */
 /*  if(ints & OHCI_INTR_FNO) {
   	writel(OHCI_INTR_FNO,  &regs->intrstatus);
   	if (waitqueue_active(&ohci_tasks)) wake_up(&ohci_tasks);
  } */
  
  if(ints & OHCI_INTR_WDH) {
   	writel(OHCI_INTR_WDH,  &regs->intrdisable);	
   	ohci->intrstatus &= (~OHCI_INTR_WDH);
	usb_ohci_done_list(ohci); /* prepare out channel list */
	writel(OHCI_INTR_WDH, &ohci->regs->intrstatus);
	writel(OHCI_INTR_WDH, &ohci->regs->intrenable);
   	 
  }
  
  if(ints & OHCI_INTR_SF) {
  	writel(OHCI_INTR_SF,  &regs->intrdisable);	
  	writel(OHCI_INTR_SF, &ohci->regs->intrstatus);
	ohci->intrstatus &= (~OHCI_INTR_SF);
  	if(ohci->ed_rm_list != NULL) {
			ohci_rm_eds(ohci);
 	 }
  }
#ifndef VROOTHUB 
  if(ints & OHCI_INTR_RHSC) {  
  	writel(OHCI_INTR_RHSC, &regs->intrdisable);
  	writel(OHCI_INTR_RHSC, &ohci->regs->intrstatus);
	wake_up(&root_hub);

  }
 #endif

  writel(OHCI_INTR_MIE, &regs->intrenable);
	
}
 
#ifndef VROOTHUB
/*
 * This gets called if the connect status on the root
 * hub (and the root hub only) changes.
 */
static void ohci_connect_change(struct ohci *ohci, unsigned int port_nr)
{
	struct usb_device *usb_dev;
    struct ohci_device *dev;
	struct ohci_device *tmp_root_hub=usb_to_ohci(ohci->bus->root_hub);
	OHCI_DEBUG(printk("uhci_connect_change: called for %d stat %x\n", port_nr,readl(&ohci->regs->roothub.portstatus[port_nr]) );)

	/*
	 * Even if the status says we're connected,
	 * the fact that the status bits changed may
	 * that we got disconnected and then reconnected.
	 *
	 * So start off by getting rid of any old devices..
	 */
	usb_disconnect(&tmp_root_hub->usb->children[port_nr]);

	 if(!(readl(&ohci->regs->roothub.portstatus[port_nr]) & RH_PS_CCS)) {
	 	writel(RH_PS_CCS, &ohci->regs->roothub.portstatus[port_nr]);
		return; /* nothing connected */
	}
	/*
	 * Ok, we got a new connection. Allocate a device to it,
	 * and find out what it wants to do..
	 */
	usb_dev = sohci_usb_allocate(tmp_root_hub->usb);
	dev = usb_dev->hcpriv; 
	dev->ohci = ohci; 
	usb_connect(dev->usb);
	tmp_root_hub->usb->children[port_nr] = usb_dev;
	wait_ms(200); /* wait for powerup */
    /* reset port/device */
 	writel(RH_PS_PRS, &ohci->regs->roothub.portstatus[port_nr]); /* reset port */
 	while(!(readl( &ohci->regs->roothub.portstatus[port_nr]) & RH_PS_PRSC)) wait_ms(10); /* reset active ? */
 	writel(RH_PS_PES, &ohci->regs->roothub.portstatus[port_nr]); /* enable port */
 	wait_ms(10);
	/* Get speed information */
	usb_dev->slow = (readl( &ohci->regs->roothub.portstatus[port_nr]) & RH_PS_LSDA) ? 1 : 0;

	/*
	 * Ok, all the stuff specific to the root hub has been done.
	 * The rest is generic for any new USB attach, regardless of
	 * hub type.
	 */
	usb_new_device(usb_dev);
}
#endif
 


/*
 * Allocate the resources required for running an OHCI controller.
 * Host controller interrupts must not be running while calling this
 * function.
 *
 * The mem_base parameter must be the usable -virtual- address of the
 * host controller's memory mapped I/O registers.
 *
 * This is where OHCI triumphs over UHCI, because good is dumb.
 * Note how much simpler this function is than in uhci.c.
 *
 * OHCI hardware takes care of most of the scheduling of different
 * transfer types with the correct prioritization for us.
 */


static struct ohci *alloc_ohci(void* mem_base)
{
	int i,j;
	struct ohci *ohci;
	struct ohci_hc_area *hc_area;
	struct usb_bus *bus;
	struct ohci_device *dev;
	struct usb_device *usb;

	/*
	 * Here we allocate some dummy  EDs as well as the
	 * OHCI host controller communications area.  The HCCA is just
	 * a nice pool of memory with pointers to endpoint descriptors
	 * for the different interrupts.
	 *
	 * The first page of memory  contains the HCCA and ohci structure
	 */
	hc_area = (struct  ohci_hc_area *) __get_free_pages(GFP_KERNEL, 1);
	if (!hc_area)
		return NULL;
	memset(hc_area, 0, sizeof(*hc_area));
    ohci = &hc_area->ohci;
	ohci->irq = -1;
	ohci->regs = mem_base;
    
	ohci->hc_area = hc_area;
	/* Tell the controller where the HCCA is */
	writel(virt_to_bus(&hc_area->hcca), &ohci->regs->hcca);
	
 
	/*
	 * Initialize the ED polling "tree",  full tree;
         * dummy eds ed[i] (hc should skip them) 
         * i == 0 is the end of the iso list;
		 * 1 is the   1ms node, 
         * 2,3        2ms nodes,
         * 4,5,6,7    4ms nodes,
         * 8  ... 15  8ms nodes,
         * 16 ... 31  16ms nodes,
         * 32 ... 63  32ms nodes
         * Sequenzes:
		 * 32-16- 8-4-2-1-0
         * 33-17- 9-5-3-1-0
         * 34-18-10-6-2-1-0
         * 35-19-11-7-3-1-0
         * 36-20-12-4-2-1-0
         * 37-21-13-5-3-1-0
         * 38-22-14-6-2-1-0
         * 39-23-15-7-3-1-0
         * 40-24- 8-4-2-1-0
         * 41-25- 9-5-3-1-0
         * 42-26-10-6-2-1-0
         *     :      :
         * 63-31-15-7-3-1-0
	 */
        hc_area->ed[ED_ISO].info |=  OHCI_ED_SKIP; /* place holder, so skip it */
        hc_area->ed[ED_ISO].next_ed =  0x0000;       /* end of iso list */

        hc_area->ed[1].next_ed = virt_to_bus(&(hc_area->ed[ED_ISO]));
        hc_area->ed[1].info |=  OHCI_ED_SKIP; /* place holder, skip it */
	     
        j=1;
        for (i = 2; i < (NUM_INTS * 2); i++) {
          	if (i >= NUM_INTS) 
	   		 	hc_area->hcca.int_table[i - NUM_INTS] = virt_to_bus(&(hc_area->ed[i]));
           
          	if( i == j*4) j *= 2;
	    	hc_area->ed[i].next_ed = virt_to_bus(&(hc_area->ed[j+ i%j]));
            hc_area->ed[i].info |=  OHCI_ED_SKIP; /* place holder, skip it */
        }


	/*
	 * for load ballancing of the interrupt branches 
	 */
	for (i = 0; i < NUM_INTS; i++) ohci->ohci_int_load[i] = 0;

	/*
	 * Store the end of control and bulk list eds. So, we know where we can add
	 * elements to these lists.
	 */
	ohci->ed_isotail     = (struct usb_ohci_ed *) &(hc_area->ed[ED_ISO]);
        ohci->ed_controltail = NULL;
        ohci->ed_bulktail    = NULL;
	
	/*
	 * Tell the controller where the control and bulk lists are
	 * The lists are empty now.
	 */	
	writel(0, &ohci->regs->ed_controlhead);
	writel(0, &ohci->regs->ed_bulkhead);

	
	USB_ALLOC(bus, sizeof(*bus));
	if (!bus)
		return NULL;

	memset(bus, 0, sizeof(*bus));

	ohci->bus = bus;
	bus->hcpriv = (void *) ohci;
	bus->op = &sohci_device_operations;

	 
	usb = sohci_usb_allocate(NULL);
	if (!usb)
		return NULL;

	dev = usb_to_ohci(usb);
	usb->bus = bus;
	bus->root_hub = usb;
	dev->ohci = ohci;
	
	/* Initialize the root hub */
	 
	usb->maxchild = readl(&ohci->regs->roothub.a) & 0xff;
	usb_init_root_hub(usb);

	return ohci;
} 


/*
 * De-allocate all resources..
 */

static void release_ohci(struct ohci *ohci)
{
	int i;
	struct ohci_device *tmp_root_hub=usb_to_ohci(ohci->bus->root_hub);
	union ep_addr_ ep_addr;
	ep_addr.iep = 0;
	
	OHCI_DEBUG(printk("USB HC release ohci \n"););
    
	if (ohci->irq >= 0) {
		free_irq(ohci->irq, ohci);
		ohci->irq = -1;
	}

    /* stop hc */
	writel(OHCI_USB_SUSPEND, &ohci->regs->control);
		
	/* deallocate all EDs and TDs */
	for(i=0; i < 128; i ++) {
		ep_addr.bep.fa = i;
		usb_ohci_rm_function(ohci, ep_addr.iep);
	}
	ohci_rm_eds(ohci); /* remove eds */
	
    /* disconnect all devices */    
	if(ohci->bus->root_hub)
		for(i = 0; i < tmp_root_hub->usb->maxchild; i++)
			usb_disconnect(tmp_root_hub->usb->children + i);
	    
    usb_deregister_bus(ohci->bus);
    USB_FREE(tmp_root_hub->usb);
    USB_FREE(tmp_root_hub);
    USB_FREE(ohci->bus);
    
	/* unmap the IO address space */
	iounmap(ohci->regs);
       
	free_pages((unsigned int) ohci->hc_area, 1);
	
}

static int ohci_roothub_thread(void * __ohci)
{
        struct ohci *ohci = (struct ohci *)__ohci;
        lock_kernel(); 

        /*
         * This thread doesn't need any user-level access,
         * so get rid of all our resources..
         */
        printk("ohci_roothub_thread at %p\n", &ohci_roothub_thread);
        exit_mm(current);
        exit_files(current);
        exit_fs(current);


        strcpy(current->comm, "root-hub");

         
		start_hc(ohci);
		writel( 0x10000, &ohci->regs->roothub.status);
		wait_ms(50); /* root hub power on */
	usb_register_bus(ohci->bus);
         do {
#ifdef CONFIG_APM
		if (apm_resume) {
			apm_resume = 0;
			start_hc(ohci);
			continue;
		}
#endif
 	 
		OHCI_DEBUG(printk("USB RH tasks: int: %x\n",ohci->intrstatus););
#ifndef VROOTHUB
		/*	if (ohci->intrstatus & OHCI_INTR_RHSC)  */
			{
				int port_nr;
	     		for(port_nr=0; port_nr< ohci->root_hub->usb->maxchild; port_nr++)
	     			if(readl(&ohci->regs->roothub.portstatus[port_nr]) & (RH_PS_CSC | RH_PS_PRSC)) {		
	     		 		ohci_connect_change(ohci, port_nr);
	     		 		writel(0xffff0000, &ohci->regs->roothub.portstatus[port_nr]);
	     		 	}
	     		 ohci->intrstatus &= ~(OHCI_INTR_RHSC);
	     		 writel(OHCI_INTR_RHSC, &ohci->regs->intrenable);
	    	}
#endif
 
         interruptible_sleep_on(&root_hub);
 
      	} while (!signal_pending(current)); 
             	
#ifdef VROOTHUB
		ohci_del_rh_int_timer(ohci);
#endif
 

		cleanup_drivers();
      /*  reset_hc(ohci); */
	 
        release_ohci(ohci);
        MOD_DEC_USE_COUNT;

	    printk("ohci_control_thread exiting\n");

        return 0;
}

/*
 * Increment the module usage count, start the control thread and
 * return success.
 */
static int found_ohci(int irq, void* mem_base)
{
	int retval;
	struct ohci *ohci;
    OHCI_DEBUG(printk("USB HC found  ohci: irq= %d membase= %x \n", irq, (int)mem_base);)
	/* Allocate the running OHCI structures */
	ohci = alloc_ohci(mem_base);
	if (!ohci) {
	  return -ENOMEM;
	}

	reset_hc(ohci);

	retval = -EBUSY;
	if (request_irq(irq, ohci_interrupt, SA_SHIRQ, "ohci-usb", ohci) == 0) {
		int pid;

		MOD_INC_USE_COUNT; 
		ohci->irq = irq;
		 
		pid = kernel_thread(ohci_roothub_thread, ohci, CLONE_FS | CLONE_FILES | CLONE_SIGHAND);
		if (pid >= 0) 
		 	return 0;
		

		MOD_DEC_USE_COUNT;
		retval = pid;
	}
	
	release_ohci(ohci);
	return retval;
}
 
static int start_ohci(struct pci_dev *dev)
{
	unsigned int mem_base = dev->base_address[0];

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
		if (waitqueue_active(&root_hub)) {
			apm_resume = 1;
			wake_up(&root_hub);
		}
		break;
	}
	return 0;
}
#endif

#define PCI_CLASS_SERIAL_USB_OHCI 0x0C0310
#define PCI_CLASS_SERIAL_USB_OHCI_PG 0x10
 

int ohci_hcd_init(void)
{
  int retval;
  struct pci_dev *dev = NULL;
 
  retval = -ENODEV;
   
  dev = NULL;
  while((dev = pci_find_class(PCI_CLASS_SERIAL_USB_OHCI, dev))) { /* OHCI */
    retval = start_ohci(dev);
    if (retval < 0) break;

    
#ifdef CONFIG_APM
		apm_register_callback(&handle_apm_event);
#endif
  
    return 0;
  }
  return retval;
}

#ifdef MODULE
int init_module(void){
	return ohci_hcd_init();
}

void cleanup_module(void)
{
#	ifdef CONFIG_APM
	apm_unregister_callback(&handle_apm_event);
#	endif
}
#endif //MODULE

