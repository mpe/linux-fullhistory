#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/sched.h>
#include "usb.h"

static int usb_audio_probe(struct usb_device *dev);
static void usb_audio_disconnect(struct usb_device *dev);
static LIST_HEAD(usb_audio_list);

struct usb_audio
{
	struct usb_device *dev;
	struct list_head list;
};

static struct usb_driver usb_audio_driver =
{
	"audio",
	usb_audio_probe,
	usb_audio_disconnect,
	{NULL, NULL}
};


static int usb_audio_irq(int state, void *buffer, void *dev_id)
{
	struct usb_audio *aud = (struct usb_audio*) dev_id;
	return 1;
}

static int usb_audio_probe(struct usb_device *dev)
{
	struct usb_interface_descriptor *interface;
	struct usb_endpoint_descriptor *endpoint;
	struct usb_audio *aud;

	int i;
	int na=0;
	
	interface = &dev->config[0].interface[0];

	for(i=0;i<dev->config[0].bNumInterfaces;i++)
	{
		int x;
		
		endpoint = &interface->endpoint[i];

		if(interface->bInterfaceClass != 1) 
	        	continue;

		printk(KERN_INFO "USB audio device detected.\n");
	
		switch(interface->bInterfaceSubClass)
		{
			case 0x01:
				printk(KERN_INFO "audio: Control device.\n");
				break;
			case 0x02:
				printk(KERN_INFO "audio: streaming.\n");
				break;
			case 0x03:
				printk(KERN_INFO "audio: nonstreaming.\n");
				break;
		}
		na++;
	}
	
	if(na==0)
		return -1;

	aud = kmalloc(sizeof(struct usb_audio), GFP_KERNEL);
	if(aud)
	{
        	memset(aud, 0, sizeof(*aud));
        	aud->dev = dev;
        	dev->private = aud;

		endpoint = &interface->endpoint[0];

//        	usb_set_configuration(dev, dev->config[0].bConfigurationValue);
//        	usb_set_protocol(dev, 0);
//        	usb_set_idle(dev, 0, 0);
        
        	usb_request_irq(dev,
                        usb_rcvctrlpipe(dev, endpoint->bEndpointAddress),
                        usb_audio_irq,
                        endpoint->bInterval,
                        aud);

		list_add(&aud->list, &usb_audio_list);
	}
	return 0;
}

static void usb_audio_disconnect(struct usb_device *dev)
{
	struct usb_audio *aud = (struct usb_audio*) dev->private;
	if(aud)
	{
        	dev->private = NULL;
        	list_del(&aud->list);
        	kfree(aud);
	}
	printk(KERN_INFO "USB audio driver removed.\n");
}

int usb_audio_init(void)
{
	usb_register(&usb_audio_driver);
	return 0;
}

/*
 *	Support functions for parsing
 */
 
void usb_audio_interface(struct usb_interface_descriptor *interface, u8 *data)
{
}

void usb_audio_endpoint(struct usb_endpoint_descriptor *interface, u8 *data)
{
}

