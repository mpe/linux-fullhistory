#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/sched.h>
#include <linux/module.h>

#include "usb.h"
#define AUDIO_DEBUG 1

static int usb_audio_probe(struct usb_device *dev);
static void usb_audio_disconnect(struct usb_device *dev);
static LIST_HEAD(usb_audio_list);

struct usb_audio {
	struct usb_device *dev;
	struct list_head list;

	void *irq_handle;
};

static struct usb_driver usb_audio_driver = {
	"audio",
	usb_audio_probe,
	usb_audio_disconnect,
	{ NULL, NULL }
};


#if 0
static int usb_audio_irq(int state, void *buffer, int len, void *dev_id)
{
#if 0
	struct usb_audio *aud = (struct usb_audio *)dev_id;

	printk("irq on %p\n", aud);
#endif

	return 1;
}
#endif

static int usb_audio_probe(struct usb_device *dev)
{
	struct usb_interface_descriptor *interface;
	struct usb_audio *aud;
	int i;
	int na=0;
	
	for (i=0; i<dev->config[0].bNumInterfaces; i++) {
		interface = &dev->config->interface[i].altsetting[0];

		if (interface->bInterfaceClass != 1) 
	        	continue;

		printk(KERN_INFO "USB audio device detected.\n");
	
		switch(interface->bInterfaceSubClass) {
			case 0x01:
				printk(KERN_INFO "audio: control device\n");
				break;
			case 0x02:
				printk(KERN_INFO "audio: streaming\n");
				break;
			case 0x03:
				printk(KERN_INFO "audio: nonstreaming\n");
				break;
		}
		na++;
	}
	
	if (!na)
		return -1;

	aud = kmalloc(sizeof(struct usb_audio), GFP_KERNEL);
	if (!aud)
		return -1;

       	memset(aud, 0, sizeof(*aud));
       	aud->dev = dev;
       	dev->private = aud;

/*
	if (usb_set_configuration(dev, dev->config[0].bConfigurationValue)) {
		printk (KERN_INFO "Failed usb_set_configuration: Audio\n");
		break;
	}
	usb_set_protocol(dev, 0);
	usb_set_idle(dev, 0, 0);
*/
        
/*
	aud->irq_handle = usb_request_irq(dev,
		usb_rcvctrlpipe(dev, endpoint->bEndpointAddress),
		usb_audio_irq,
		endpoint->bInterval,
		aud);
*/

	list_add(&aud->list, &usb_audio_list);
		
	return 0;
}

static void usb_audio_disconnect(struct usb_device *dev)
{
	struct usb_audio *aud = (struct usb_audio*) dev->private;

	if (!aud)
		return;

       	list_del(&aud->list);

	usb_release_irq(aud->dev, aud->irq_handle);
	aud->irq_handle = NULL;

       	kfree(aud);
       	dev->private = NULL;
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
#ifdef AUDIO_DEBUG
	printk(KERN_DEBUG "usb_audio_interface.\n");
#endif
}

void usb_audio_endpoint(struct usb_endpoint_descriptor *interface, u8 *data)
{
#ifdef AUDIO_DEBUG
	printk(KERN_DEBUG "usb_audio_interface.\n");
#endif
}

#ifdef MODULE
int init_module(void)
{
	return usb_audio_init();
}

void cleanup_module(void)
{
	usb_deregister(&usb_audio_driver);
}

#endif

