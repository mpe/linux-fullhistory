/*
 * HCD (OHCI) Virtual Root Hub for USB.
 *
 * (C) Copyright 1999 Roman Weissgaerber (weissg@vienna.at)
 *
 * The Root Hub is build into the HC (UHCI or OHCI) hardware. 
 * This piece of code lets it look like it resides on the usb
 * like the other hubs.
 * (for anyone who wants to do a control operation on the root hub)
 *  
 * v2.1 1999/05/09 
 * v2.0 1999/05/04
 * v1.0 1999/04/27
 * ohci-root-hub.c
 *  
 */
 


#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/malloc.h>
#include <linux/string.h>

#include "usb.h"
#include "ohci-hcd.h" 

#ifdef VROOTHUB 

#include "ohci-root-hub.h"


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
        0x40,       /*  __u16 ep_wMaxPacketSize; 64 Bytes */
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


int root_hub_control_msg(struct ohci *ohci, int cmd_len, void *rh_cmd, void *rh_data, int leni, __OHCI_BAG lw0, __OHCI_BAG lw1, f_handler handler)
{ 

  __u32 stat;
  __u32 rep_handler;
  int req_reply=0;
  union ep_addr_ ep_addr;
  union ep_addr_ ep_addr_ret;
        __u8 * cmd = rh_cmd;
	__u8 * data = rh_data;
	int i;
	int len =leni;

        __u8  bmRequestType = cmd[0];
        __u8  bRequest      = cmd[1];
        __u16 wValue        = cmd[3] << 8 | cmd [2];
        __u16 wIndex        = cmd[5] << 8 | cmd [4];
        __u16 wLength       = cmd[7] << 8 | cmd [6];
printk("USB root hub: adr: %8x cmd(%8x): ", ohci->root_hub_funct_addr, 8);
for(i=0;i<8;i++)
	printk("%2x", ((char *)rh_cmd)[i]);
 
printk(" ; \n");

	ep_addr_ret.iep = 0;
	ep_addr_ret.bep.fa = ohci->root_hub_funct_addr;
	ep_addr_ret.bep.ep =  (bmRequestType & 0x80) | 0x40;

  switch (bmRequestType | bRequest << 8) {
   /* Request Destination:
      without flags: Device, 
      RH_INTERFACE: interface, 
      RH_ENDPOINT: endpoint,
      RH_CLASS means HUB here, 
      RH_OTHER | RH_CLASS  almost ever means HUB_PORT here 
   */
  
   case RH_GET_STATUS:
     len = 2; 
     data[0] = 0x01;
     data[1] = 0x00;
     req_reply = RH_ACK; 
     break;
   case RH_GET_STATUS | RH_INTERFACE:
     len = 2; 
     data[0] = 0x00;
     data[1] = 0x00;     
     req_reply = RH_ACK;
     break;
   case RH_GET_STATUS | RH_ENDPOINT:
     len = 2; 
     data[0] = 0x00;
     data[1] = 0x00;     
     req_reply = RH_ACK;
     break;
   case RH_GET_STATUS | RH_CLASS: /* HUB_STATUS */
     stat = readl(&ohci->regs->roothub.status) & 0x7fff7fff; /* bit 31 u. 15 has other meaning */ 
     data[0] = stat & 0xff;
     data[1] = (stat >> 8) & 0xff;
     data[2] = (stat >> 16) & 0xff;
     data[3] = (stat >> 24) & 0xff;
     len = 4;
     req_reply = RH_ACK; 
     break;
   case RH_GET_STATUS | RH_OTHER | RH_CLASS: /* PORT_STATUS */
     stat = readl(&ohci->regs->roothub.portstatus[wIndex-1]);
     data[0] = stat & 0xff;
     data[1] = (stat >> 8) & 0xff;
     data[2] = (stat >> 16) & 0xff;
     data[3] = (stat >> 24) & 0xff;
     len = 4;
     req_reply = RH_ACK; 
     printk("rh: stat %4x wIndex %4x;\n", stat , wIndex);
     break;

   case RH_CLEAR_FEATURE:
     switch (wValue) {
       case (RH_DEVICE_REMOTE_WAKEUP):
       default:
     }
     break;

   case RH_CLEAR_FEATURE | RH_ENDPOINT:  
     switch (wValue) {
       case (RH_ENDPOINT_STALL): 
         len=0;
         req_reply = RH_ACK;
         break;
       default:
     }
     break;

   case RH_CLEAR_FEATURE | RH_CLASS:
     switch (wValue) {
       /*     case (RH_C_HUB_LOCAL_POWER):  OHCI says: no switching of this one */
       case (RH_C_HUB_OVER_CURRENT):
         writel(RH_PS_OCIC, &ohci->regs->roothub.status);
         len=0;
         req_reply = RH_ACK;
         break;
       default:
     }
     break;
   case RH_CLEAR_FEATURE | RH_OTHER | RH_CLASS:
     switch (wValue) {
       case (RH_PORT_ENABLE):
	     writel(RH_PS_CCS, &ohci->regs->roothub.portstatus[wIndex-1]);
         len=0;
         req_reply = RH_ACK;
         break;
       case (RH_PORT_SUSPEND):
         writel(RH_PS_POCI, &ohci->regs->roothub.portstatus[wIndex-1]);
         len=0;
         req_reply = RH_ACK;
         break;
       case (RH_PORT_POWER):
         writel(RH_PS_LSDA, &ohci->regs->roothub.portstatus[wIndex-1]);
         len=0;
         req_reply = RH_ACK;
         break;
       case (RH_C_PORT_CONNECTION):
         writel(RH_PS_CSC, &ohci->regs->roothub.portstatus[wIndex-1]);
         len=0;
         req_reply = RH_ACK;
         break;
       case (RH_C_PORT_ENABLE):
         writel(RH_PS_PESC, &ohci->regs->roothub.portstatus[wIndex-1]);
         len=0;
         req_reply = RH_ACK;
         break;
       case (RH_C_PORT_SUSPEND):
         writel(RH_PS_PSSC, &ohci->regs->roothub.portstatus[wIndex-1]);
         len=0;
         req_reply = RH_ACK;
         break;
       case (RH_C_PORT_OVER_CURRENT):
         writel(RH_PS_OCIC, &ohci->regs->roothub.portstatus[wIndex-1]);
         len=0;
         req_reply = RH_ACK;
         break;
       case (RH_C_PORT_RESET):
         writel(RH_PS_PRSC, &ohci->regs->roothub.portstatus[wIndex-1]);
         len=0;
         req_reply = RH_ACK;
         break;
       /*
       case (RH_PORT_CONNECTION):
       case (RH_PORT_OVER_CURRENT):
       case (RH_PORT_RESET):
       case (RH_PORT_LOW_SPEED):
       */
       default:
     }
     break;
   case RH_SET_FEATURE:  
     switch (wValue) {
       case (RH_DEVICE_REMOTE_WAKEUP):
       default:
     }
     break;

   case RH_SET_FEATURE | RH_ENDPOINT:
     switch (wValue) {
       case (RH_ENDPOINT_STALL):
       default:
     }
     break;

   case RH_SET_FEATURE | RH_CLASS: 
     switch (wValue) {
       /* case (RH_C_HUB_LOCAL_POWER): Root Hub has no local Power 
       case (RH_C_HUB_OVER_CURRENT): */
      default:
     }
     break;
   case RH_SET_FEATURE | RH_OTHER | RH_CLASS:
     switch (wValue) {
       case (RH_PORT_SUSPEND):
         writel(RH_PS_PSS, &ohci->regs->roothub.portstatus[wIndex-1]);
         len=0;
         req_reply = RH_ACK;
         break;
       case (RH_PORT_RESET):
         if((readl(&ohci->regs->roothub.portstatus[wIndex-1]) &1) != 0)  /* BUG IN HUP CODE *********/
         writel(RH_PS_PRS, &ohci->regs->roothub.portstatus[wIndex-1]);
         len=0;
         req_reply = RH_ACK;
         break;
       case (RH_PORT_POWER):
         writel(RH_PS_PPS, &ohci->regs->roothub.portstatus[wIndex-1]);
         len=0;
         req_reply = RH_ACK;
         break;
       case (RH_PORT_ENABLE):
         writel(RH_PS_PES, &ohci->regs->roothub.portstatus[wIndex-1]);
         len=0;
         req_reply = RH_ACK;
         break;
       /*
       case (RH_PORT_CONNECTION):
       case (RH_PORT_OVER_CURRENT):
       case (RH_PORT_LOW_SPEED):
       case (RH_C_PORT_CONNECTION):
       case (RH_C_PORT_ENABLE):
       case (RH_C_PORT_SUSPEND):
       case (RH_C_PORT_OVER_CURRENT):
       case (RH_C_PORT_RESET):
       */
       default:  
     }
     break;

  case RH_SET_ADDRESS:
     ohci->root_hub_funct_addr = wValue; 
	/* ohci->ed_func_ep0[wValue] = &ohci->ed_rh_ep0;
	 ohci->ed_func_ep0[wValue]->ep_addr.bep.fa = wValue; 
	 ohci->ed_func_ep0[wValue]->ed_list = &ohci->ed_rh_epi; */
	 ohci->ed_rh_epi.ed_list = NULL;
	 ohci->ed_rh_epi.ep_addr.bep.fa = wValue;
	 ohci->ed_rh_epi.ep_addr.bep.ep = 0xa1; /* Int in port 1 */
     ohci->ed_func_ep0[0]= NULL;
     len = 0;
     req_reply = RH_ACK;
     break;

   case RH_GET_DESCRIPTOR:
     switch ((wValue & 0xff00) >> 8) {
     case (0x01): /* device descriptor */
       len = min(sizeof(root_hub_dev_des), wLength);
       memcpy(data, root_hub_dev_des, len);
       req_reply = RH_ACK;
       break;
     case (0x02): /* configuration descriptor */
       len = min(sizeof(root_hub_config_des), wLength);
       memcpy(data, root_hub_config_des, len);
       req_reply = RH_ACK;
       break;
     case (0x03): /* string descriptors */
     default:
     }
     break;
  case RH_GET_DESCRIPTOR | RH_CLASS:
     data[1] = 0x29;
     stat = readl(&ohci->regs->roothub.a);  
     data[2] = stat & 0xff;        /* number of ports */
     data[0] = (data[2] / 8) * 2 + 9; /* length of descriptor */
     if(data[0] > wLength) {
       req_reply =  RH_REQ_ERR;
       break;
     }
     data[3] = (stat >> 8) & 0xff;
     data[4] = (stat >> 16) & 0xff;
     data[5] = (stat >> 24) & 0xff;
     data[6] = 0; /* Root Hub needs no current from bus */
     stat = readl(&ohci->regs->roothub.b); 
     if(data[2] <= 8) { /* less than 8 Ports */
       data[7] = stat & 0xff;
       data[8] = (stat >> 16) & 0xff; /* 0xff for USB Rev. 1.1 ?, stat >> 16 for USB Rev. 1.0 */
     }
     else {
       data[7] = stat & 0xff;
       data[8] = (stat >> 8) & 0xff;
       data[9] = (stat >> 16) & 0xff;  /* 0xff for USB Rev. 1.1?, stat >> 16 for USB Rev. 1.0 */
       data[10] = (stat >> 24) & 0xff; /* 0xff for USB Rev. 1.1?, stat >> 24 for USB Rev. 1.0 */
     }
     len = data[0];
     req_reply = RH_ACK;
     break;

   case RH_SET_DESCRIPTOR:
     break;

   case RH_GET_CONFIGURATION:
     len = 1;
     data[0] = 0x01;
     req_reply = RH_ACK;
     break;

   case RH_SET_CONFIGURATION: /* start it up */
     writel( 0x10000, &ohci->regs->roothub.status);
     /*writel( OHCI_INTR_RHSC, &ohci->regs->intrenable);*/
     len = 0;
     req_reply = RH_ACK;
     break;
   /*  Optional or meaningless requests 
     case RH_GET_STATE | RH_OTHER | RH_CLASS:
     case RH_GET_INTERFACE | RH_INTERFACE:
     case RH_SET_INTERFACE | RH_INTERFACE:
     case RH_SYNC_FRAME | RH_ENDPOINT: 
   */

  /* Vendor Requests, we are the vendor!
	Will the USB-Consortium give us a Vendor Id
	for a virtual hub-device :-) ?
     We could use these requests for configuration purposes on the HCD Driver, not used in the altenate usb !*/ 

  case RH_SET_FEATURE | RH_VENDOR: /* remove all endpoints of device wIndex = Dev << 8  */
    switch(wValue) {
    case RH_REMOVE_EP:
      ep_addr.iep = 0;
      ep_addr.bep.ep = wIndex & 0xff;
      ep_addr.bep.fa = (wIndex << 8) & 0xff00;
      usb_ohci_rm_function(ohci, ep_addr.iep);
      len=0;
      req_reply = RH_ACK;
      break;
    }
    break;
  case RH_SET_FEATURE | RH_ENDPOINT | RH_VENDOR: /* remove endpoint wIndex = Dev << 8 | EP */
    switch(wValue) {
    case RH_REMOVE_EP: 
      ep_addr.iep = 0;
      ep_addr.bep.ep = wIndex & 0xff;
      ep_addr.bep.fa = (wIndex << 8) & 0xff00;
      usb_ohci_rm_ep(ohci, ohci_find_ep(ohci, ep_addr.iep));
      len=0;
      req_reply = RH_ACK;
      break;
    }
    break;
  case RH_SET_EP | RH_ENDPOINT | RH_VENDOR:   
    ep_addr.bep.ep = data[0];
    ep_addr.bep.fa = data[1];
    ep_addr.bep.hc = data[2];
    ep_addr.bep.host = data[3];  
    rep_handler  = data[7] << 24 |data[6] << 16 | data[5] << 8 | data[4];
    /*  struct usb_ohci_ed *usb_ohci_add_ep(union ep_addr_ ep_addr,
	struct ohci * ohci, int interval, int load, int (*handler)(int, void*), int ep_size, int speed) */
    usb_ohci_add_ep(ohci, ep_addr.iep, data[8], data[9], (f_handler) rep_handler, data[11] << 8 | data[10] , data[12]);
    len=0;
    req_reply = RH_ACK;
    break;

  default: 
  }
 printk("USB HC roothubstat1: %x \n", readl( &(ohci->regs->roothub.portstatus[0]) ));
  printk("USB HC roothubstat2: %x \n", readl( &(ohci->regs->roothub.portstatus[1]) ));

  /* if (req_reply == RH_ACK) len; */
  queue_reply(ohci, ep_addr_ret.iep, 8, rh_cmd, data, len, lw0, lw1, handler);
  return 0;
}

int root_hub_int_req(struct ohci * ohci, int cmd_len, void * ctrl, void *  data, int data_len, __OHCI_BAG lw0, __OHCI_BAG lw1, f_handler handler){

	struct ohci_rep_td *td;
 	struct ohci_rep_td *tmp;
 	union ep_addr_ ep_addr;
 	
 	td = kmalloc(sizeof(td),GFP_KERNEL);
 	tmp = ohci->td_rh_epi;
 	td->next_td = NULL;
 	if(tmp == NULL) { /* queue td */
 		ohci->td_rh_epi = td;	
 	}
 	else {
 		while(tmp->next_td != NULL) tmp = tmp->next_td;
 		tmp->next_td = td;
 	}
 	ep_addr.iep = 0;
 	ep_addr.bep.fa = ohci->root_hub_funct_addr;
 	ep_addr.bep.ep = 0xA1; /* INT IN EP endpoint 1 */
  	td->cmd_len = 0;
  	td->cmd = NULL;
  	td->data = data;
  	td->data_len = data_len;
  	td->handler = handler; 
  	td->next_td = NULL;
  	td->ep_addr = ep_addr.iep;
	td->lw0 = lw0;
	td->lw1 = lw1;
	ohci_init_rh_int_timer(ohci, 255);
	return 0;
}


/* prepare Interrupt pipe transaction data; HUP INTERRUPT ENDPOINT */ 
int root_hub_send_irq(struct ohci * ohci, void * rh_data, int rh_len ) {

  int num_ports;
  int i;
  int ret;
  int len;

  __u8 * data = rh_data;

  num_ports = readl(&ohci->regs->roothub.a) & 0xff; 
  data[0] = (readl(&ohci->regs->roothub.status) & 0x00030000)>0?1:0;
  ret = data[0];

  for(i=0; i < num_ports; i++) {
    data[i/8] |= ((readl(&ohci->regs->roothub.portstatus[i]) & 0x001f0000)>0?1:0) << ((i+1) % 8);
    ret += data[i/8];
  }
  len = i/8 + 1;
  
  if (ret > 0) return len;

  return  RH_NACK;
}




 
static struct timer_list rh_int_timer;

/* Virtual Root Hub INTs are polled by this timer every "intervall" ms */
static void rh_int_timer_do(unsigned long ptr) {
	int len; 
	int interval;
	struct ohci * ohci = (struct ohci *) ptr;
    struct ohci_rep_td *td = ohci->td_rh_epi;

	if(td != NULL) { /* if ther is a TD handle the INT request */
		
		len = root_hub_send_irq(ohci, td->data, td->data_len );
		if(len > 0) {
			ohci->td_rh_epi = td->next_td;
			td->next_td = ohci->repl_queue; 
			ohci->repl_queue = td;
			send_replies(ohci);
		}
	}	
	interval = ohci->rh_int_interval;
	init_timer(& rh_int_timer);
	rh_int_timer.function = rh_int_timer_do;
	rh_int_timer.data = (unsigned long) ohci;
	rh_int_timer.expires = jiffies + (HZ * (interval<30?30: interval)) /1000;
	add_timer(&rh_int_timer);
}

/* Root Hub INTs are polled by this timer */
int ohci_init_rh_int_timer(struct ohci * ohci, int interval) {

	if(!(ohci->rh_int_timer)) { 
		ohci->rh_int_timer = 1;
		ohci->rh_int_interval = interval;
		init_timer(& rh_int_timer);
		rh_int_timer.function = rh_int_timer_do;
		rh_int_timer.data = (unsigned long) ohci;
		rh_int_timer.expires = jiffies + (HZ * (interval<30?30: interval)) /1000;
		add_timer(&rh_int_timer);
	}
	return 0;
}

int ohci_del_rh_int_timer(struct ohci * ohci) {
	del_timer(&rh_int_timer);
	return 0;
}
/* for root hub replies, queue the reply, (it will be sent immediately now) */

int queue_reply(struct ohci * ohci, unsigned int ep_addr, int cmd_len,void * cmd, void * data,int  len, __OHCI_BAG lw0, __OHCI_BAG lw1, f_handler handler) {

 struct ohci_rep_td *td;
 int status = 0;
 
printk("queue_reply ep: %x len: %x\n", ep_addr, len);
td = kmalloc(sizeof(td), GFP_KERNEL);

  if (len < 0) { status = len; len = 0;}
  td->cmd_len = cmd_len;
  td->cmd = cmd;
  td->data = data;
  td->data_len = len;
  td->handler = handler; 
  td->next_td = ohci->repl_queue; ohci->repl_queue = td;
  td->ep_addr = ep_addr;
  td->lw0 = lw0;
  td->lw1 = lw1;
  td->status = status;
  send_replies(ohci);
return 0;
}

/* for root hub replies; send the reply */
int send_replies(struct ohci * ohci) { 
	struct ohci_rep_td *td;
	struct ohci_rep_td *tmp;

	td = ohci->repl_queue; ohci->repl_queue = NULL;
	while ( td != NULL) {
		td->handler((void *) ohci, td->ep_addr,td->cmd_len,td->cmd, td->data, td->data_len, td->status, td->lw0, td->lw1); 
		tmp = td;
		td = td->next_td;
	
		kfree(tmp);
	}
	return 0;
}
 
#endif
