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
#include <asm/uaccess.h>

#include "usb.h"
#include "hub.h"

/* Wakes up khubd */
static spinlock_t hub_event_lock = SPIN_LOCK_UNLOCKED;
static spinlock_t hub_list_lock = SPIN_LOCK_UNLOCKED;

static LIST_HEAD(hub_event_list);	/* List of hubs needing servicing */
static LIST_HEAD(hub_list);		/* List containing all of the hubs (for cleanup) */

static DECLARE_WAIT_QUEUE_HEAD(khubd_wait);
static int khubd_pid = 0;			/* PID of khubd */
static int khubd_running = 0;

static int usb_get_hub_descriptor(struct usb_device *dev, void *data, int size)
{
	devrequest dr;

	dr.requesttype = USB_DIR_IN | USB_RT_HUB;
	dr.request = USB_REQ_GET_DESCRIPTOR;
	dr.value = (USB_DT_HUB << 8);
	dr.index = 0;
	dr.length = size;

	return dev->bus->op->control_msg(dev, usb_rcvctrlpipe(dev,0), &dr,
		data, size);
}

static int usb_clear_port_feature(struct usb_device *dev, int port, int feature)
{
	devrequest dr;

	dr.requesttype = USB_RT_PORT;
	dr.request = USB_REQ_CLEAR_FEATURE;
	dr.value = feature;
	dr.index = port;
	dr.length = 0;

	return dev->bus->op->control_msg(dev, usb_sndctrlpipe(dev,0), &dr,
		NULL, 0);
}

static int usb_set_port_feature(struct usb_device *dev, int port, int feature)
{
	devrequest dr;

	dr.requesttype = USB_RT_PORT;
	dr.request = USB_REQ_SET_FEATURE;
	dr.value = feature;
	dr.index = port;
	dr.length = 0;

	return dev->bus->op->control_msg(dev, usb_sndctrlpipe(dev,0), &dr,
		NULL, 0);
}

static int usb_get_hub_status(struct usb_device *dev, void *data)
{
	devrequest dr;

	dr.requesttype = USB_DIR_IN | USB_RT_HUB;
	dr.request = USB_REQ_GET_STATUS;
	dr.value = 0;
	dr.index = 0;
	dr.length = 4;

	return dev->bus->op->control_msg(dev, usb_rcvctrlpipe(dev,0), &dr,
		data, 4);
}

static int usb_get_port_status(struct usb_device *dev, int port, void *data)
{
	devrequest dr;

	dr.requesttype = USB_DIR_IN | USB_RT_PORT;
	dr.request = USB_REQ_GET_STATUS;
	dr.value = 0;
	dr.index = port;
	dr.length = 4;

	return dev->bus->op->control_msg(dev, usb_rcvctrlpipe(dev,0), &dr,
		data, 4);
}

/*
 * A irq handler returns non-zero to indicate to
 * the low-level driver that it wants to be re-activated,
 * or zero to say "I'm done".
 */
static int hub_irq(int status, void *__buffer, int len, void *dev_id)
{
	struct usb_hub *hub = dev_id;
	unsigned long flags;

	if (waitqueue_active(&khubd_wait)) {
		/* Add the hub to the event queue */
		spin_lock_irqsave(&hub_event_lock, flags);
		if (hub->event_list.next == &hub->event_list) {
			list_add(&hub->event_list, &hub_event_list);
			/* Wake up khubd */
			wake_up(&khubd_wait);
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
	printk(KERN_INFO "hub: %d-port%s detected\n", hub->nports,
		(hub->nports == 1) ? "" : "s");

	charac = (hubdescriptor[4] << 8) + hubdescriptor[3];
	switch (charac & HUB_CHAR_LPSM) {
		case 0x00:
			printk(KERN_INFO "hub: ganged power switching\n");
			break;
		case 0x01:
			printk(KERN_INFO "hub: individual port power switching\n");
			break;
		case 0x02:
		case 0x03:
			printk(KERN_INFO "hub: unknown reserved power switching mode\n");
			break;
	}

	if (charac & HUB_CHAR_COMPOUND)
		printk(KERN_INFO "hub: part of a compound device\n");
	else
		printk(KERN_INFO "hub: standalone hub\n");

	switch (charac & HUB_CHAR_OCPM) {
		case 0x00:
			printk(KERN_INFO "hub: global over current protection\n");
			break;
		case 0x08:
			printk(KERN_INFO "hub: individual port over current protection\n");
			break;
		case 0x10:
		case 0x18:
			printk(KERN_INFO "hub: no over current protection\n");
                        break;
	}

	printk(KERN_INFO "hub: power on to power good time: %dms\n",
		hubdescriptor[5] * 2);

	printk(KERN_INFO "hub: hub controller current requirement: %dmA\n",
		hubdescriptor[6]);

	for (i = 0; i < dev->maxchild; i++)
		printk(KERN_INFO "hub:  port %d is%s removable\n", i + 1,
			hubdescriptor[7 + ((i + 1)/8)] & (1 << ((i + 1) % 8))
			? " not" : "");

	if (usb_get_hub_status(dev, buf))
		return;

	printk(KERN_INFO "hub: local power source is %s\n",
		(buf[0] & 1) ? "lost (inactive)" : "good");

	printk(KERN_INFO "hub: %sover current condition exists\n",
		(buf[0] & 2) ? "" : "no ");

	/* Enable power to the ports */
	printk(KERN_INFO "hub: enabling power on all ports\n");
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
	if (!(endpoint->bEndpointAddress & USB_DIR_IN))
		return -1;

	/* If it's not an interrupt endpoint, we'd better punt! */
	if ((endpoint->bmAttributes & 3) != 3)
		return -1;

	/* We found a hub */
	printk(KERN_INFO "USB hub found\n");

	if ((hub = kmalloc(sizeof(*hub), GFP_KERNEL)) == NULL) {
		printk(KERN_ERR "couldn't kmalloc hub struct\n");
		return -1;
	}

	memset(hub, 0, sizeof(*hub));

	dev->private = hub;

	INIT_LIST_HEAD(&hub->event_list);
	hub->dev = dev;

	/* Record the new hub's existence */
	spin_lock_irqsave(&hub_list_lock, flags);
	INIT_LIST_HEAD(&hub->hub_list);
	list_add(&hub->hub_list, &hub_list);
	spin_unlock_irqrestore(&hub_list_lock, flags);

	usb_hub_configure(hub);

	hub->irq_handle = usb_request_irq(dev, usb_rcvctrlpipe(dev,
		endpoint->bEndpointAddress), hub_irq, endpoint->bInterval, hub);

	/* Wake up khubd */
	wake_up(&khubd_wait);

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

	/* Disconnect anything that may have been there */
	usb_disconnect(&hub->children[port]);

	/* Reset the port */
	usb_set_port_feature(hub, port + 1, USB_PORT_FEAT_RESET);

	wait_ms(50);	/* FIXME: This is from the *BSD stack, thanks! :) */

	/* Check status */
	if (usb_get_port_status(hub, port + 1, buf)) {
		printk(KERN_ERR "get_port_status failed\n");
		return;
	}

	portstatus = le16_to_cpup((unsigned short *)buf + 0);
	portchange = le16_to_cpup((unsigned short *)buf + 1);

	/* If it's not in CONNECT and ENABLE state, we're done */
	if ((!(portstatus & USB_PORT_STAT_CONNECTION)) &&
	    (!(portstatus & USB_PORT_STAT_ENABLE)))
		/* We're done now, we already disconnected the device */
		return;

	/* Allocate a new device struct for it */
	usb = hub->bus->op->allocate(hub);
	if (!usb) {
		printk(KERN_ERR "couldn't allocate usb_device\n");
		return;
	}

	usb->slow = (portstatus & USB_PORT_STAT_LOW_SPEED) ? 1 : 0;

	hub->children[port] = usb;

	/* Find a new device ID for it */
	usb_connect(usb);

	/* Run it through the hoops (find a driver, etc) */
	if (usb_new_device(usb)) {
		/* Woops, disable the port */
		printk(KERN_DEBUG "hub: disabling malfunctioning port %d\n",
			port + 1);
		usb_clear_port_feature(hub, port + 1, USB_PORT_FEAT_ENABLE);
		usb_clear_port_feature(hub, port + 1, USB_PORT_FEAT_POWER);
	}
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
				printk(KERN_ERR "get_port_status failed\n");
				continue;
			}

			portstatus = le16_to_cpup((unsigned short *)buf + 0);
			portchange = le16_to_cpup((unsigned short *)buf + 1);

			if (portchange & USB_PORT_STAT_C_CONNECTION) {
				printk(KERN_INFO "hub: port %d connection change\n",
					i + 1);

				usb_clear_port_feature(dev, i + 1,
					USB_PORT_FEAT_C_CONNECTION);

				usb_hub_port_connect_change(dev, i);
			}

			if (portchange & USB_PORT_STAT_C_ENABLE) {
				printk(KERN_INFO "hub: port %d enable change\n",
					i + 1);
				usb_clear_port_feature(dev, i + 1,
					USB_PORT_FEAT_C_ENABLE);
			}

			if (portchange & USB_PORT_STAT_C_SUSPEND)
				printk(KERN_INFO "hub: port %d suspend change\n",
					i + 1);

			if (portchange & USB_PORT_STAT_C_OVERCURRENT)
				printk(KERN_INFO "hub: port %d over-current change\n",
					i + 1);

			if (portchange & USB_PORT_STAT_C_RESET) {
				printk(KERN_INFO "hub: port %d reset change\n",
					i + 1);
				usb_clear_port_feature(dev, i + 1,
					USB_PORT_FEAT_C_RESET);
			}

		}
		tmp = next;
        }

	spin_unlock_irqrestore(&hub_event_lock, flags);
}

static int usb_hub_thread(void *__hub)
{
/*
	MOD_INC_USE_COUNT;
*/
	
	khubd_running = 1;

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
		usb_hub_events();
		interruptible_sleep_on(&khubd_wait);
	} while (!signal_pending(current));

/*
	MOD_DEC_USE_COUNT;
*/

	printk("usb_hub_thread exiting\n");
	khubd_running = 0;

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

	usb_register(&hub_driver);
	printk(KERN_INFO "USB hub driver registered\n");

	pid = kernel_thread(usb_hub_thread, NULL,
		CLONE_FS | CLONE_FILES | CLONE_SIGHAND);
	if (pid >= 0) {
		khubd_pid = pid;

		return 0;
	}

	/* Fall through if kernel_thread failed */
	usb_deregister(&hub_driver);

	return 1;
}

void usb_hub_cleanup(void)
{
	struct list_head *next, *tmp, *head = &hub_list;
	struct usb_hub *hub;
	unsigned long flags, flags2;
	int ret;

	/* Kill the thread */
	ret = kill_proc(khubd_pid, SIGTERM, 1);
	if (!ret) {
		int count = 10;

		while (khubd_running && --count) {
			current->state = TASK_INTERRUPTIBLE;
			schedule_timeout(1);
		}

		if (!count)
			printk(KERN_ERR "hub: giving up on killing khubd\n");
	}

	/*
	 * Hub resources are freed for us by usb_deregister.  It
	 * usb_driver_purge on every device which in turn calls that
	 * devices disconnect function if it is using this driver.
	 * The hub_disconnect function takes care of releasing the
	 * individual hub resources. -greg
	 */
	usb_deregister(&hub_driver);
} /* usb_hub_cleanup() */

#ifdef MODULE
int init_module(void)
{
	return usb_hub_init();
}

void cleanup_module(void)
{
	usb_hub_cleanup();
}
#endif

