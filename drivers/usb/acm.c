/*
 * USB Abstract Control Model based on Brad Keryan's USB busmouse driver 
 *
 * Armin Fuerst 5/8/1999
 *
 * version 0.2: Improved Bulk transfer. TX led now flashes every time data is
 * sent. Send Encapsulated Data is not needed, nor does it do anything.
 * Why's that ?!? Thanks to Thomas Sailer for his close look at the bulk code.
 * He told me about some importand bugs. (5/21/99)
 *
 * version 0.1: Bulk transfer for uhci seems to work now, no dangling tds any
 * more. TX led of the ISDN TA flashed the first time. Does this mean it works?
 * The interrupt of the ctrl endpoint crashes the kernel => no read possible
 * (5/19/99)
 *
 * version 0.0: Driver sets up configuration, sets up data pipes, opens misc
 * device. No actual data transfer is done, since we don't have bulk transfer,
 * yet. Purely skeleton for now. (5/8/99)
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/malloc.h>
#include <linux/module.h>

#include <asm/spinlock.h>

#include "usb.h"

#define USB_ACM_MINOR 32

struct acm_state {
	int present; /* this acm is plugged in */
	int active; /* someone is has this acm's device open */
	int serstate; /* Status of the serial port (rate, handshakelines,...) */
	struct usb_device *dev;
	unsigned ctrlbuffer;	/*buffer for control messages*/
	unsigned int readendp,writeendp,ctrlendp;
	unsigned int readpipe,writepipe,ctrlpipe;
	char buffer;
};

static struct acm_state static_acm_state;

spinlock_t usb_acm_lock = SPIN_LOCK_UNLOCKED;

static int acm_irq(int state, void *__buffer, int len, void *dev_id)
{
//	unsigned char *data = __buffer;
        struct acm_state *acm = &static_acm_state; 
        devrequest *dr;

        dr=__buffer;
	printk("ACM_USB_IRQ\n");
        printk("reqtype: %02X\n",dr->requesttype);
        printk("request: %02X\n",dr->request);
	printk("wValue: %02X\n",dr->value);
	printk("wIndex: %02X\n",dr->index);
	printk("wLength: %02X\n",dr->length);
	
	switch(dr->request) {
	  //Network connection 
	  case 0x00:
	    printk("Network connection: ");
	    if (dr->request==0) printk("disconnected\n");
	    if (dr->request==1) printk("connected\n");
	    break;

    	  //Response available
	  case 0x01:
	    printk("Response available\n");
	    acm->buffer=1;
	    break;
	
	  //Set serial line state
	  case 0x20:
	    if ((dr->index==1)&&(dr->length==2)) {
	      acm->serstate=acm->ctrlbuffer;
	      printk("Serstate: %02X\n",acm->ctrlbuffer);
	    }
	    break;
	}
/*
	if(!acm->active)
		return 1;
*/
	return 1;
}

static int release_acm(struct inode * inode, struct file * file)
{
	struct acm_state *acm = &static_acm_state;
	printk("ACM_FILE_RELEASE\n");

//	fasync_acm(-1, file, 0);
	if (--acm->active)
		return 0;
	return 0;
}

static int open_acm(struct inode * inode, struct file * file)
{
	struct acm_state *acm = &static_acm_state;
	printk("USB_FILE_OPEN\n");

	if (!acm->present)
		return -EINVAL;
	if (acm->active++)
		return 0;
	return 0;
}

static ssize_t write_acm(struct file * file,
       const char * buffer, size_t count, loff_t *ppos)
{
        devrequest dr;
	struct acm_state *acm = &static_acm_state;
	unsigned long retval;	        

	printk("USB_FILE_WRITE\n");
//Huh, i seem to got that wrong, we don't need this ?!?
/*
	dr.requesttype = USB_TYPE_CLASS | USB_RT_ENDPOINT;
	dr.request = 0;
	dr.value = 0;
	dr.index = acm->writeendp;
	dr.length = count;
	acm->dev->bus->op->control_msg(acm->dev, usb_sndctrlpipe(acm->dev, 0), &dr, NULL, 0);
*/	

	acm->dev->bus->op->bulk_msg(acm->dev,&acm->writepipe,buffer, count, &retval);
	return -EINVAL;
}


static ssize_t read_acm(struct file * file, const char * buffer, size_t count, loff_t *ppos)
{
	devrequest dr;
        struct acm_state *acm = &static_acm_state;
	unsigned long retval;
	printk("USB_FILE_READ\n");
//        if (!acm->buffer) return -1;
     	acm->buffer=0;
//We don't need this
/*
	printk("writing control msg\n");
	dr.requesttype = USB_TYPE_CLASS | USB_RT_ENDPOINT | 0x80;
	dr.request = 1;
	dr.value = 0;
	dr.index = acm->readendp;
	dr.length = 0;
	acm->dev->bus->op->control_msg(acm->dev, usb_sndctrlpipe(acm->dev, 0), &dr, NULL, 0);
*/
	printk("reading:>%s<\n",buffer);
	acm->dev->bus->op->bulk_msg(acm->dev,&acm->readpipe,buffer, 1,&retval);
	printk("done:>%s<\n",buffer);
	return 1;
}

struct file_operations usb_acm_fops = {
	NULL,		/* acm_seek */
	read_acm,
	write_acm,
	NULL, 		/* acm_readdir */
	NULL,	 	/* acm_poll */
	NULL, 		/* acm_ioctl */
	NULL,		/* acm_mmap */
	open_acm,
	NULL,		/* flush */
	release_acm,
	NULL,
	NULL,		/*fasync*/
};

static struct miscdevice usb_acm = {
	USB_ACM_MINOR, "USB ACM", &usb_acm_fops
};

static int acm_probe(struct usb_device *dev)
{
	struct usb_interface_descriptor *interface;
	struct usb_endpoint_descriptor *endpoint;
	struct acm_state *acm = &static_acm_state;
	int cfgnum;

	/* Only use CDC */
	if (dev->descriptor.bDeviceClass != 2 ||
	    dev->descriptor.bDeviceSubClass != 0 ||
            dev->descriptor.bDeviceProtocol != 0)
		return -1;

	/*Now scan all configs for a ACM configuration*/
	for (cfgnum=0;cfgnum<dev->descriptor.bNumConfigurations;cfgnum++) {
		/* The first one should be Communications interface? */
		interface = &dev->config[cfgnum].altsetting[0].interface[0];
		if (interface->bInterfaceClass != 2 ||
		    interface->bInterfaceSubClass != 2 ||
		    interface->bInterfaceProtocol != 1 ||
	    	    interface->bNumEndpoints != 1)
			continue;

		/*Which uses an interrupt input */
		endpoint = &interface->endpoint[0];
		if ((endpoint->bEndpointAddress & 0x80) != 0x80 ||
		    (endpoint->bmAttributes & 3) != 3)
			continue;
			
		/* The second one should be a Data interface? */
		interface = &dev->config[cfgnum].altsetting[0].interface[1];
		if (interface->bInterfaceClass != 10 ||
		    interface->bInterfaceSubClass != 0 ||
		    interface->bInterfaceProtocol != 0 ||
	    	    interface->bNumEndpoints != 2)
			continue;

		/*With a bulk input */
		endpoint = &interface->endpoint[0];
		if ((endpoint->bEndpointAddress & 0x80) != 0x80 ||
		    (endpoint->bmAttributes & 3) != 2)
			continue;
			
		/*And a bulk output */
		endpoint = &interface->endpoint[1];
		if ((endpoint->bEndpointAddress & 0x80) == 0x80 ||
		    (endpoint->bmAttributes & 3) != 2)
			continue;

		printk("USB ACM found\n");
		usb_set_configuration(dev, dev->config[cfgnum].bConfigurationValue);
		acm->dev=dev;
		acm->readendp=dev->config[cfgnum].altsetting[0].interface[1].endpoint[0].bEndpointAddress;
		acm->writeendp=dev->config[cfgnum].altsetting[0].interface[1].endpoint[1].bEndpointAddress;
		acm->ctrlendp=dev->config[cfgnum].altsetting[0].interface[0].endpoint[0].bEndpointAddress;
		acm->readpipe=usb_rcvbulkpipe(dev,acm->readendp);
		acm->writepipe=usb_sndbulkpipe(dev,acm->writeendp);
		usb_request_irq(dev,acm->ctrlpipe=usb_rcvctrlpipe(dev,acm->ctrlendp), acm_irq, dev->config[cfgnum].altsetting[0].interface[0].endpoint[0].bInterval, &acm->ctrlbuffer);
		acm->present = 1;
		acm->buffer=0;
		return 0;
	}

	return -1;
}

static void acm_disconnect(struct usb_device *dev)
{
	struct acm_state *acm = &static_acm_state;

	/* this might need work */
	acm->present = 0;
}

static struct usb_driver acm_driver = {
	"acm",
	acm_probe,
	acm_disconnect,
	{ NULL, NULL }
};

int usb_acm_init(void)
{
	struct acm_state *acm = &static_acm_state;

	misc_register(&usb_acm);

	acm->present = acm->active = 0;

	usb_register(&acm_driver);
	printk(KERN_INFO "USB ACM registered.\n");
	return 0;
}

#ifdef MODULE

int init_module(void)
{
	return usb_acm_init();
}

void cleanup_module(void)
{
	/* this, too, probably needs work */
	usb_deregister(&acm_driver);
	misc_deregister(&usb_acm);
}

#endif
