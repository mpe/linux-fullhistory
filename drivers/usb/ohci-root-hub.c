/*
 * HCD (OHCI) Virtual Root Hub for USB.
 *
 * (C) Copyright 1999 Roman Weissgaerber (weissg@vienna.at)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * 
 * The Root Hub is build into the HC (UHCI or OHCI) hardware. 
 * This piece of code lets it look like it resides on the usb
 * like the other hubs.
 * (for anyone who wants to do a control operation on the root hub)
 * 
 * v4.0 1999/08/18 
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

#define OK(x) 			len = (x); req_reply = 0; break
#define WR_RH_STAT(x) 		writel((x), &ohci->regs->roothub.status)
#define WR_RH_PORTSTAT(x) 	writel((x), &ohci->regs->roothub.portstatus[wIndex-1])
#define RD_RH_STAT			readl(&ohci->regs->roothub.status)
#define RD_RH_PORTSTAT		readl(&ohci->regs->roothub.portstatus[wIndex-1])

int root_hub_control_msg(struct usb_device *usb_dev, unsigned int pipe, devrequest * cmd, void *data, int leni)
{ 
	struct ohci * ohci = usb_dev->bus->hcpriv;
	__u8 data_buf[16];
	int req_reply=4;
 
	int len =leni;

	__u16 bmRType_bReq  = cmd->requesttype | (cmd->request << 8);
	__u16 wValue        = cpu_to_le16(cmd->value);
	__u16 wIndex        = cpu_to_le16(cmd->index);
	__u16 wLength       = cpu_to_le16(cmd->length);

	OHCI_DEBUG(printk("USB root hub: adr: %2x cmd(%1x): ", ohci->rh.devnum, 8);)
	OHCI_DEBUG({ int i; for(i=0;i<8;i++) printk(" %02x", ((unsigned char *)cmd)[i]);})
	OHCI_DEBUG(printk(" ; \n");)
 
	switch (bmRType_bReq) {
	/* Request Destination:
	   without flags: Device, 
	   RH_INTERFACE: interface, 
	   RH_ENDPOINT: endpoint,
	   RH_CLASS means HUB here, 
	   RH_OTHER | RH_CLASS  almost ever means HUB_PORT here 
	*/
  
		case RH_GET_STATUS: 				 		*(__u16 *)data = cpu_to_le16(1); OK(2);
		case RH_GET_STATUS | RH_INTERFACE: 	 		*(__u16 *)data = cpu_to_le16(0); OK(2);
		case RH_GET_STATUS | RH_ENDPOINT:	 		*(__u16 *)data = cpu_to_le16(0); OK(2);   
		case RH_GET_STATUS | RH_CLASS: 				*(__u32 *)data = cpu_to_le32(RD_RH_STAT & 0x7fff7fff); OK(4);
		case RH_GET_STATUS | RH_OTHER | RH_CLASS: 	*(__u32 *)data = cpu_to_le32(RD_RH_PORTSTAT); OK(4);

		case RH_CLEAR_FEATURE | RH_ENDPOINT:  
			switch (wValue) {
				case (RH_ENDPOINT_STALL): OK(0);
			}
			break;

		case RH_CLEAR_FEATURE | RH_CLASS:
			switch (wValue) {
				case (RH_C_HUB_OVER_CURRENT): WR_RH_STAT(RH_PS_OCIC); OK(0);
			}
			break;
		
		case RH_CLEAR_FEATURE | RH_OTHER | RH_CLASS:
			switch (wValue) {
				case (RH_PORT_ENABLE): 			WR_RH_PORTSTAT(RH_PS_CCS ); OK(0);
				case (RH_PORT_SUSPEND):			WR_RH_PORTSTAT(RH_PS_POCI); OK(0);
				case (RH_PORT_POWER):			WR_RH_PORTSTAT(RH_PS_LSDA); OK(0);
				case (RH_C_PORT_CONNECTION):	WR_RH_PORTSTAT(RH_PS_CSC ); OK(0);
				case (RH_C_PORT_ENABLE):		WR_RH_PORTSTAT(RH_PS_PESC); OK(0);
				case (RH_C_PORT_SUSPEND):		WR_RH_PORTSTAT(RH_PS_PSSC); OK(0);
				case (RH_C_PORT_OVER_CURRENT):	WR_RH_PORTSTAT(RH_PS_OCIC); OK(0);
				case (RH_C_PORT_RESET):			WR_RH_PORTSTAT(RH_PS_PRSC); OK(0); 
			}
			break;
 
		case RH_SET_FEATURE | RH_OTHER | RH_CLASS:
			switch (wValue) {
				case (RH_PORT_SUSPEND):			WR_RH_PORTSTAT(RH_PS_PSS ); OK(0); 
				case (RH_PORT_RESET): if((RD_RH_PORTSTAT &1) != 0)  WR_RH_PORTSTAT(RH_PS_PRS ); /* BUG IN HUP CODE *********/ OK(0);
				case (RH_PORT_POWER):			WR_RH_PORTSTAT(RH_PS_PPS ); OK(0); 
				case (RH_PORT_ENABLE):			WR_RH_PORTSTAT(RH_PS_PES ); OK(0);
			}
			break;

		case RH_SET_ADDRESS: ohci->rh.devnum = wValue; OK(0);

		case RH_GET_DESCRIPTOR:
			switch ((wValue & 0xff00) >> 8) {
				case (0x01): /* device descriptor */
					len = min(leni, min(sizeof(root_hub_dev_des), wLength));
					memcpy(data, root_hub_dev_des, len); OK(len);
				case (0x02): /* configuration descriptor */
					len = min(leni, min(sizeof(root_hub_config_des), wLength));
					memcpy(data, root_hub_config_des, len); OK(len);
				case (0x03): /* string descriptors */
				default:
			}
			break;
		
		case RH_GET_DESCRIPTOR | RH_CLASS:
			*(__u8 *)(data_buf+1) = 0x29;
			*(__u32 *)(data_buf+2) = cpu_to_le32(readl(&ohci->regs->roothub.a));  
	 		*(__u8 *)data_buf = (*(__u8 *)(data_buf+2) / 8) * 2 + 9; /* length of descriptor */
				 
			len = min(leni, min(*(__u8 *)data_buf, wLength));
			*(__u8 *)(data_buf+6) = 0; /* Root Hub needs no current from bus */
			if(*(__u8 *)(data_buf+2) < 8) { /* less than 8 Ports */
				*(__u8 *) (data_buf+7) = readl(&ohci->regs->roothub.b) & 0xff; 
				*(__u8 *) (data_buf+8) = (readl(&ohci->regs->roothub.b) & 0xff0000) >> 16; 
			}
			else {
				*(__u32 *) (data_buf+7) = cpu_to_le32(readl(&ohci->regs->roothub.b)); 
			}
			memcpy(data, data_buf, len);
			OK(len); 
 
		case RH_GET_CONFIGURATION: 	*(__u8 *)data = 0x01; OK(1);

		case RH_SET_CONFIGURATION: 	WR_RH_STAT( 0x10000); OK(0);
	}
	
	OHCI_DEBUG(printk("USB HC roothubstat1: %x \n", readl( &(ohci->regs->roothub.portstatus[0]) ));)
	OHCI_DEBUG(printk("USB HC roothubstat2: %x \n", readl( &(ohci->regs->roothub.portstatus[1]) ));)
  		
	
	return req_reply;
}

/* prepare Interrupt pipe transaction data; HUB INTERRUPT ENDPOINT */ 
static int root_hub_send_irq(struct ohci * ohci, void * rh_data, int rh_len ) {

  int num_ports;
  int i;
  int ret;
  int len;

  __u8 * data = rh_data;

  num_ports = readl(&ohci->regs->roothub.a) & 0xff; 
  *(__u8 *)data = (readl(&ohci->regs->roothub.status) & 0x00030000)>0?1:0;
  ret = *(__u8 *)data;

  for(i=0; i < num_ports; i++) {
    *(__u8 *)(data+i/8) |= ((readl(&ohci->regs->roothub.portstatus[i]) & 0x001f0000)>0?1:0) << ((i+1) % 8);
    ret += *(__u8 *)(data+i/8);
  }
  len = i/8 + 1;
  
  if (ret > 0) return len;

  return  RH_NACK;
}

 
static int ohci_init_rh_int_timer(struct usb_device * usb_dev, int interval);

/* Virtual Root Hub INTs are polled by this timer every "intervall" ms */
static void rh_int_timer_do(unsigned long ptr) {
	int len; 
	int interval;
	int ret;
	
	struct usb_device * usb_dev = (struct usb_device *) ptr;
    struct ohci * ohci = usb_dev->bus->hcpriv;
	struct ohci_device * dev = usb_to_ohci(usb_dev);


	if(ohci->rh.send) { 
		len = root_hub_send_irq(ohci, dev->data, 1 );

		if(len > 0) { 
			
			ret = ohci->rh.handler(0, dev->data, len, ohci->rh.dev_id);
			if(ret <= 0)ohci->rh.send = 0; /* 0 .. do not requeue  */
		}
	}	
	interval = ohci->rh.interval;
	ohci_init_rh_int_timer(usb_dev, interval);
}

/* Root Hub INTs are polled by this timer */
static int ohci_init_rh_int_timer(struct usb_device * usb_dev, int interval) {
	struct ohci * ohci = usb_dev->bus->hcpriv;

	ohci->rh.interval = interval;
	init_timer(& ohci->rh.rh_int_timer);
	ohci->rh.rh_int_timer.function = rh_int_timer_do;
	ohci->rh.rh_int_timer.data = (unsigned long) usb_dev;
	ohci->rh.rh_int_timer.expires = jiffies + (HZ * (interval<30?30: interval)) /1000;
	add_timer(&ohci->rh.rh_int_timer);
	
	return 0;
}

static int ohci_del_rh_int_timer(struct ohci * ohci) {
	del_timer(&ohci->rh.rh_int_timer);
	return 0;
}
 
void * root_hub_request_irq(struct usb_device *usb_dev, unsigned int pipe, usb_device_irq handler, int period, void *dev_id) 
{
	struct ohci * ohci = usb_dev->bus->hcpriv;
	
	OHCI_DEBUG( printk("USB HC-RH IRQ>>>: RH every %d ms\n", period);) 
	ohci->rh.handler = handler;
	ohci->rh.dev_id =  dev_id;
	ohci->rh.send = 1;
	ohci->rh.interval = period;
	ohci_init_rh_int_timer(usb_dev, period);
	return ohci->rh.int_addr = usb_to_ohci(usb_dev);
}

int root_hub_release_irq(struct usb_device *usb_dev, void * ed) 
{
	// struct usb_device *usb_dev = ((struct ohci_device *) ((unsigned int)ed & 0xfffff000))->usb;
	struct ohci * ohci = usb_dev->bus->hcpriv;

	OHCI_DEBUG( printk("USB HC-RH RM_IRQ>>>:\n");) 
	ohci->rh.send = 0;
	ohci_del_rh_int_timer(ohci);
	return 0;
}


 
#endif
