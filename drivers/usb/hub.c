/*
 * USB hub driver.
 *
 * (C) Copyright 1999 Linus Torvalds
 * (C) Copyright 1999 Johannes Erdfelt
 * (C) Copyright 1999 Gregory P. Smith
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <linux/malloc.h>
#include <linux/smp_lock.h>
#include <linux/module.h>

#include <asm/spinlock.h>

#include "usb.h"
#include "hub.h"

/* Wakes up khubd */
static DECLARE_WAIT_QUEUE_HEAD(usb_hub_wait);
static spinlock_t hub_event_lock = SPIN_LOCK_UNLOCKED;
static spinlock_t hub_list_lock = SPIN_LOCK_UNLOCKED;

/* List of hubs needing servicing */
static struct list_head hub_event_list;

#ifdef MODULE
/* List containing all of the hubs (for cleanup) */
static struct list_head all_hubs_list;
#endif

/* PID of khubd */
static int khubd_pid = 0;

/*
 * A irq handler returns non-zero to indicate to
 * the low-level driver that it wants to be re-activated,
 * or zero to say "I'm done".
 */
static int hub_irq(int status, void *__buffer, int len, void *dev_id)
{
	struct usb_hub *hub = dev_id;
	unsigned long flags;

	if (waitqueue_active(&usb_hub_wait)) {
		/* Add the hub to the event queue */
		spin_lock_irqsave(&hub_event_lock, flags);
		if (hub->event_list.next == &hub->event_list) {
			list_add(&hub->event_list, &hub_event_list);
			/* Wake up khubd */
			wake_up(&usb_hub_wait);
		}
		spin_unlock_irqrestore(&hub_event_lock, flags);
	}

	return 1;
}

static void usb_hub_configure(struct usb_hub *hub)
{
	struct usb_device *dev = hub->dev;
	unsigned char hubdescriptor[8], buf[4];
	int charac, i;

	usb_set_configuration(dev, dev->config[0].bConfigurationValue);

	if (usb_get_hub_descriptor(dev, hubdescriptor, 8))
		return;

	hub->nports = dev->maxchild = hubdescriptor[2];
	printk("hub: %d-port%s detected\n", hub->nports,
		(hub->nports == 1) ? "" : "s");

	charac = (hubdescriptor[4] << 8) + hubdescriptor[3];
	switch (charac & HUB_CHAR_LPSM) {
		case 0x00:
			printk("hub: ganged power switching\n");
			break;
		case 0x01:
			printk("hub: individual port power switching\n");
			break;
		case 0x02:
		case 0x03:
			printk("hub: unknown reserved power switching mode\n");
			break;
	}

	if (charac & HUB_CHAR_COMPOUND)
		printk("hub: part of a compound device\n");
	else
		printk("hub: standalone hub\n");

	switch (charac & HUB_CHAR_OCPM) {
		case 0x00:
			printk("hub: global over current protection\n");
			break;
		case 0x08:
			printk("hub: individual port over current protection\n");
			break;
		case 0x10:
		case 0x18:
			printk("hub: no over current protection\n");
                        break;
	}

	printk("hub: power on to power good time: %dms\n",
		hubdescriptor[5] * 2);

	printk("hub: hub controller current requirement: %dmA\n",
		hubdescriptor[6]);

	for (i = 0; i < dev->maxchild; i++)
		printk("hub:  port %d is%s removable\n", i + 1,
			hubdescriptor[7 + ((i + 1)/8)] & (1 << ((i + 1) % 8))
			? " not" : "");

	if (usb_get_hub_status(dev, buf))
		return;

	printk("hub: local power source is %s\n",
		(buf[0] & 1) ? "lost (inactive)" : "good");

	printk("hub: %sover current condition exists\n",
		(buf[0] & 2) ? "" : "no ");

#if 0
	for (i = 0; i < hub->nports; i++) {
		int portstat, portchange;
		unsigned char portstatus[4];

		if (usb_get_port_status(dev, i + 1, portstatus))
			return;
		portstat = (portstatus[1] << 8) + portstatus[0];
		portchange = (portstatus[3] << 8) + portstatus[2];

		printk("hub: port %d status\n", i + 1);
		printk("hub:  %sdevice present\n", (portstat & 1) ? "" : "no ");
		printk("hub:  %s\n", (portstat & 2) ? "enabled" : "disabled");
		printk("hub:  %ssuspended\n", (portstat & 4) ? "" : "not ");
		printk("hub:  %sover current\n", (portstat & 8) ? "" : "not ");
		printk("hub:  has %spower\n", (portstat & 0x100) ? "" : "no ");
		printk("hub:  %s speed\n", (portstat & 0x200) ? "low" : "full");
	}
#endif

	/* Enable power to the ports */
	printk("enabling power on all ports\n");
	for (i = 0; i < hub->nports; i++)
		usb_set_port_feature(dev, i + 1, USB_PORT_FEAT_POWER);
}

static int hub_probe(struct usb_device *dev)
{
	struct usb_interface_descriptor *interface;
	struct usb_endpoint_descriptor *endpoint;
	struct usb_hub *hub;
	unsigned long flags;

	/* We don't handle multi-config hubs */
	if (dev->descriptor.bNumConfigurations != 1)
		return -1;

	/* We don't handle multi-interface hubs */
	if (dev->config[0].bNumInterfaces != 1)
		return -1;

	interface = &dev->config[0].altsetting[0].interface[0];

	/* Is it a hub? */
	if (interface->bInterfaceClass != 9)
		return -1;
	if ((interface->bInterfaceSubClass != 0) &&
	    (interface->bInterfaceSubClass != 1))
		return -1;

	/* Multiple endpoints? What kind of mutant ninja-hub is this? */
	if (interface->bNumEndpoints != 1)
		return -1;

	endpoint = &interface->endpoint[0];

	/* Output endpoint? Curiousier and curiousier.. */
	if (!(endpoint->bEndpointAddress & 0x80))
		return -1;

	/* If it's not an interrupt endpoint, we'd better punt! */
	if ((endpoint->bmAttributes & 3) != 3)
		return -1;

	/* We found a hub */
	printk("USB hub found\n");

	if ((hub = kmalloc(sizeof(*hub), GFP_KERNEL)) == NULL) {
		printk("couldn't kmalloc hub struct\n");
		return -1;
	}

	memset(hub, 0, sizeof(*hub));

	dev->private = hub;

	INIT_LIST_HEAD(&hub->event_list);
	hub->dev = dev;

	/* Record the new hub's existence */
	spin_lock_irqsave(&hub_list_lock, flags);
	INIT_LIST_HEAD(&hub->hub_list);
	list_add(&hub->hub_list, &all_hubs_list);
	spin_unlock_irqrestore(&hub_list_lock, flags);

	usb_hub_configure(hub);

	hub->irq_handle = usb_request_irq(dev, usb_rcvctrlpipe(dev, endpoint->bEndpointAddress), hub_irq, endpoint->bInterval, hub);

	/* Wake up khubd */
	wake_up(&usb_hub_wait);

	return 0;
}

static void hub_disconnect(struct usb_device *dev)
{
	struct usb_hub *hub = dev->private;
	unsigned long flags;

	spin_lock_irqsave(&hub_event_lock, flags);

	/* Delete it and then reset it */
	list_del(&hub->event_list);
	INIT_LIST_HEAD(&hub->event_list);
	list_del(&hub->hub_list);
	INIT_LIST_HEAD(&hub->hub_list);

	spin_unlock_irqrestore(&hub_event_lock, flags);

	usb_release_irq(hub->dev, hub->irq_handle);
	hub->irq_handle = NULL;

	/* Free the memory */
	kfree(hub);
}

static void usb_hub_port_connect_change(struct usb_device *hub, int port)
{
	struct usb_device *usb;
	unsigned char buf[4];
	unsigned short portstatus, portchange;

	usb_disconnect(&hub->children[port]);

	usb_set_port_feature(hub, port + 1, USB_PORT_FEAT_RESET);

	wait_ms(50);	/* FIXME: This is from the *BSD stack, thanks! :) */

	if (usb_get_port_status(hub, port + 1, buf)) {
		printk("get_port_status failed\n");
		return;
	}

	portstatus = le16_to_cpup((unsigned short *)buf + 0);
	portchange = le16_to_cpup((unsigned short *)buf + 1);

	if ((!(portstatus & USB_PORT_STAT_CONNECTION)) &&
		(!(portstatus & USB_PORT_STAT_ENABLE))) {
		/* We're done now, we already disconnected the device */
		/* printk("not connected/enabled\n"); */
		return;
	}

	usb = hub->bus->op->allocate(hub);
	if (!usb) {
		printk("couldn't allocate usb_device\n");
		return;
	}

	usb_connect(usb);

	usb->slow = (portstatus & USB_PORT_STAT_LOW_SPEED) ? 1 : 0;

	hub->children[port] = usb;

	usb_new_device(usb);
}

static void usb_hub_events(void)
{
	unsigned long flags;
	unsigned char buf[4];
	unsigned short portstatus, portchange;
	int i;
	struct list_head *next, *tmp, *head = &hub_event_list;
	struct usb_device *dev;
	struct usb_hub *hub;

	spin_lock_irqsave(&hub_event_lock, flags);

	tmp = head->next;
	while (tmp != head) {
		hub = list_entry(tmp, struct usb_hub, event_list);
		dev = hub->dev;

		next = tmp->next;

		list_del(tmp);
		INIT_LIST_HEAD(tmp);

		for (i = 0; i < hub->nports; i++) {
			if (usb_get_port_status(dev, i + 1, buf)) {
				printk("get_port_status failed\n");
				continue;
			}

			portstatus = le16_to_cpup((unsigned short *)buf + 0);
			portchange = le16_to_cpup((unsigned short *)buf + 1);

			if (portchange & USB_PORT_STAT_C_CONNECTION) {
				printk("hub: port %d connection change\n", i + 1);

				usb_clear_port_feature(dev, i + 1,
					USB_PORT_FEAT_C_CONNECTION);

				usb_hub_port_connect_change(dev, i);
			}

			if (portchange & USB_PORT_STAT_C_ENABLE) {
				printk("hub: port %d enable change\n", i + 1);
				usb_clear_port_feature(dev, i + 1,
					USB_PORT_FEAT_C_ENABLE);
			}

			if (portchange & USB_PORT_STAT_C_SUSPEND)
				printk("hub: port %d suspend change\n", i + 1);

			if (portchange & USB_PORT_STAT_C_OVERCURRENT)
				printk("hub: port %d over-current change\n", i + 1);

			if (portchange & USB_PORT_STAT_C_RESET) {
				printk("hub: port %d reset change\n", i + 1);
				usb_clear_port_feature(dev, i + 1,
					USB_PORT_FEAT_C_RESET);
			}

#if 0
			if (!portchange)
				continue;

			if (usb_get_port_status(dev, i + 1, buf))
				return;

			portstatus = (buf[1] << 8) + buf[0];
			portchange = (buf[3] << 8) + buf[2];

			printk("hub: port %d status\n", i + 1);
			printk("hub:  %sdevice present\n", (portstatus & 1) ? "" : "no ");
			printk("hub:  %s\n", (portstatus & 2) ? "enabled" : "disabled");
			printk("hub:  %ssuspended\n", (portstatus & 4) ? "" : "not ");
			printk("hub:  %sover current\n", (portstatus & 8) ? "" : "not ");
			printk("hub:  has %spower\n", (portstatus & 0x100) ? "" : "no ");
			printk("hub:  %s speed\n", (portstatus & 0x200) ? "low" : "full");
#endif
		}
		tmp = next;
#if 0
		wait_ms(1000);
#endif
        }

	spin_unlock_irqrestore(&hub_event_lock, flags);
}

static int usb_hub_thread(void *__hub)
{
	MOD_INC_USE_COUNT;
	
	printk(KERN_INFO "USB hub driver registered\n");

	lock_kernel();

	/*
	 * This thread doesn't need any user-level access,
	 * so get rid of all our resources
	 */
	exit_mm(current);
	exit_files(current);
	exit_fs(current);

	/* Setup a nice name */
	strcpy(current->comm, "khubd");

	/* Send me a signal to get me die (for debugging) */
	do {
		interruptible_sleep_on(&usb_hub_wait);
		usb_hub_events();
	} while (!signal_pending(current));

	MOD_DEC_USE_COUNT;

	printk("usb_hub_thread exiting\n");

	return 0;
}

static struct usb_driver hub_driver = {
	"hub",
	hub_probe,
	hub_disconnect,
	{ NULL, NULL }
};

/*
 * This should be a separate module.
 */
int usb_hub_init(void)
{
	int pid;

	INIT_LIST_HEAD(&hub_event_list);
	INIT_LIST_HEAD(&all_hubs_list);

	usb_register(&hub_driver);
	pid = kernel_thread(usb_hub_thread, NULL, CLONE_FS | CLONE_FILES | CLONE_SIGHAND);
	if (pid >= 0) {
		khubd_pid = pid;
		return 0;
	}

	/* Fall through if kernel_thread failed */
	usb_deregister(&hub_driver);

	return 0;
}

void usb_hub_cleanup(void)
{
	struct list_head *next, *tmp, *head = &all_hubs_list;
	struct usb_hub *hub;
	unsigned long flags, flags2;

	/* Free the resources allocated by each hub */
	spin_lock_irqsave(&hub_list_lock, flags);
	spin_lock_irqsave(&hub_event_lock, flags2);

	tmp = head->next;
	while (tmp != head) {
		hub = list_entry(tmp, struct usb_hub, hub_list);

		next = tmp->next;

		list_del(&hub->event_list);
		INIT_LIST_HEAD(&hub->event_list);
		list_del(tmp);         /* &hub->hub_list */
		INIT_LIST_HEAD(tmp);   /* &hub->hub_list */

		/* XXX we should disconnect each connected port here */

		usb_release_irq(hub->dev, hub->irq_handle);
		hub->irq_handle = NULL;
		kfree(hub);

		tmp = next;
	}

	usb_deregister(&hub_driver);

	spin_unlock_irqrestore(&hub_event_lock, flags2);
	spin_unlock_irqrestore(&hub_list_lock, flags);
} /* usb_hub_cleanup() */

#ifdef MODULE
int init_module(void){
	return usb_hub_init();
}

void cleanup_module(void){
	usb_hub_cleanup();
}
#endif
