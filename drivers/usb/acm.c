/*
 * USB Abstract Control Model based on Brad Keryan's USB busmouse driver 
 *
 * Armin Fuerst 5/8/1999
 *
 * version 0.0: Driver sets up configuration, setus up data pipes, opens misc
 * device. No actual data transfer is done, since we don't have bulk transfer,
 * yet. Purely skeleton for now.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/random.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/malloc.h>

#include <asm/spinlock.h>

#include "usb.h"

#define USB_ACM_MINOR 32

struct acm_state {
	int present; /* this acm is plugged in */
	int active; /* someone is has this acm's device open */
	struct usb_device *dev;
	unsigned int readpipe,writepipe;
};

static struct acm_state static_acm_state;

spinlock_t usb_acm_lock = SPIN_LOCK_UNLOCKED;

static int acm_irq(int state, void *__buffer, void *dev_id)
{
/*
	signed char *data = __buffer;
        struct acm_state *acm = &static_acm_state; 
	if(!acm->active)
		return 1;
*/

	/*We should so something useful here*/
	printk("ACM_USB_IRQ\n");

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
        char * buffer="ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	struct acm_state *acm = &static_acm_state;
	printk("USB_FILE_WRITE\n");
		printk("writing:>%s<\n",buffer);
		acm->dev->bus->op->bulk_msg(acm->dev,acm->writepipe,buffer, 26);
		printk("done:>%s<\n",buffer);
		printk("reading:>%s<\n",buffer);
		acm->dev->bus->op->bulk_msg(acm->dev,acm->readpipe,buffer, 26);
		printk("done:>%s<\n",buffer);
	return -EINVAL;
}

static ssize_t read_acm(struct file * file, char * buffer, size_t count, loff_t *ppos)
{
	printk("USB_FILE_READ\n");
	return -EINVAL;
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
		interface = &dev->config[cfgnum].interface[0];
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
		interface = &dev->config[cfgnum].interface[1];
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
		acm->readpipe=__create_pipe(dev,&dev->config[cfgnum].interface[1].endpoint[0]);
		acm->writepipe=__create_pipe(dev,&dev->config[cfgnum].interface[1].endpoint[1]);
		usb_request_irq(dev, usb_rcvctrlpipe(dev,&dev->config[cfgnum].interface[0].endpoint[0]), acm_irq, endpoint->bInterval, NULL);
		acm->present = 1;
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

#if 0

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
