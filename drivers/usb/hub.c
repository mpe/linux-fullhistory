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
#include <linux/spinlock.h>

#include <asm/uaccess.h>
#include <asm/byteorder.h>

#include "usb.h"
#include "hub.h"

/* Wakes up khubd */
static spinlock_t hub_event_lock = SPIN_LOCK_UNLOCKED;

static LIST_HEAD(hub_event_list);	/* List of hubs needing servicing */
static LIST_HEAD(hub_list);		/* List containing all of the hubs (for cleanup) */

static DECLARE_WAIT_QUEUE_HEAD(khubd_wait);
static int khubd_pid = 0;			/* PID of khubd */
static int khubd_running = 0;

static int usb_get_hub_descriptor(struct usb_device *dev, void *data, int size)
{
	return usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
		USB_REQ_GET_DESCRIPTOR, USB_DIR_IN | USB_RT_HUB,
		USB_DT_HUB << 8, 0, data, size, HZ);
}

static int usb_clear_port_feature(struct usb_device *dev, int port, int feature)
{
	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
		USB_REQ_CLEAR_FEATURE, USB_RT_PORT, feature, port, NULL, 0, HZ);
}

static int usb_set_port_feature(struct usb_device *dev, int port, int feature)
{
	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
		USB_REQ_SET_FEATURE, USB_RT_PORT, feature, port, NULL, 0, HZ);
}

static int usb_get_hub_status(struct usb_device *dev, void *data)
{
	return usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
		USB_REQ_GET_STATUS, USB_DIR_IN | USB_RT_HUB, 0, 0,
		data, sizeof(struct usb_hub_status), HZ);
}

static int usb_get_port_status(struct usb_device *dev, int port, void *data)
{
	return usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
		USB_REQ_GET_STATUS, USB_DIR_IN | USB_RT_PORT, 0, port,
		data, sizeof(struct usb_hub_status), HZ);
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

	switch (status) {
	case USB_ST_REMOVED:
		/* Just ignore it */
		break;
	case USB_ST_NOERROR:
		/* Something happened, let khubd figure it out */
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
		break;
	}

	return 1;
}

static int usb_hub_configure(struct usb_hub *hub)
{
	struct usb_device *dev = hub->dev;
	unsigned char buffer[4], *bitmap;
	struct usb_hub_descriptor *descriptor;
	struct usb_descriptor_header *header;
	struct usb_hub_status *hubsts;
	int i;

	/* Get the length first */
	if (usb_get_hub_descriptor(dev, buffer, 4) < 0)
		return -1;

	header = (struct usb_descriptor_header *)buffer;
	bitmap = kmalloc(header->bLength, GFP_KERNEL);
	if (!bitmap)
		return -1;

	if (usb_get_hub_descriptor(dev, bitmap, header->bLength) < 0)
		return -1;

	descriptor = (struct usb_hub_descriptor *)bitmap;

	hub->nports = dev->maxchild = descriptor->bNbrPorts;
	printk(KERN_INFO "hub: %d port%s detected\n", hub->nports,
		(hub->nports == 1) ? "" : "s");

	switch (descriptor->wHubCharacteristics & HUB_CHAR_LPSM) {
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

	if (descriptor->wHubCharacteristics & HUB_CHAR_COMPOUND)
		printk(KERN_INFO "hub: part of a compound device\n");
	else
		printk(KERN_INFO "hub: standalone hub\n");

	switch (descriptor->wHubCharacteristics & HUB_CHAR_OCPM) {
		case 0x00:
			printk(KERN_INFO "hub: global over-current protection\n");
			break;
		case 0x08:
			printk(KERN_INFO "hub: individual port over-current protection\n");
			break;
		case 0x10:
		case 0x18:
			printk(KERN_INFO "hub: no over-current protection\n");
                        break;
	}

	printk(KERN_INFO "hub: power on to power good time: %dms\n",
		descriptor->bPwrOn2PwrGood * 2);

	printk(KERN_INFO "hub: hub controller current requirement: %dmA\n",
		descriptor->bHubContrCurrent);

	for (i = 0; i < dev->maxchild; i++)
		printk(KERN_INFO "hub:  port %d is%s removable\n", i + 1,
			bitmap[7 + ((i + 1)/8)] & (1 << ((i + 1) % 8))
			? " not" : "");

	kfree(bitmap);

	if (usb_get_hub_status(dev, buffer) < 0)
		return -1;

	hubsts = (struct usb_hub_status *)buffer;
	printk(KERN_INFO "hub: local power source is %s\n",
		(le16_to_cpu(hubsts->wHubStatus) & HUB_STATUS_LOCAL_POWER) ? "lost (inactive)" : "good");

	printk(KERN_INFO "hub: %sover-current condition exists\n",
		(le16_to_cpu(hubsts->wHubStatus) & HUB_STATUS_OVERCURRENT) ? "" : "no ");

	/* Enable power to the ports */
	printk(KERN_INFO "hub: enabling power on all ports\n");
	for (i = 0; i < hub->nports; i++)
		usb_set_port_feature(dev, i + 1, USB_PORT_FEAT_POWER);

	return 0;
}

static void * hub_probe(struct usb_device *dev, unsigned int i)
{
	struct usb_interface_descriptor *interface;
	struct usb_endpoint_descriptor *endpoint;
	struct usb_hub *hub;
	unsigned long flags;
	int ret;

	interface = &dev->actconfig->interface[i].altsetting[0];

	/* Is it a hub? */
	if (interface->bInterfaceClass != USB_CLASS_HUB)
		return NULL;

	/* Some hubs have a subclass of 1, which AFAICT according to the */
	/*  specs is not defined, but it works */
	if ((interface->bInterfaceSubClass != 0) &&
	    (interface->bInterfaceSubClass != 1))
		return NULL;

	/* Multiple endpoints? What kind of mutant ninja-hub is this? */
	if (interface->bNumEndpoints != 1)
		return NULL;

	endpoint = &interface->endpoint[0];

	/* Output endpoint? Curiousier and curiousier.. */
	if (!(endpoint->bEndpointAddress & USB_DIR_IN))
		return NULL;

	/* If it's not an interrupt endpoint, we'd better punt! */
	if ((endpoint->bmAttributes & 3) != 3)
		return NULL;

	/* We found a hub */
	printk(KERN_INFO "USB hub found\n");

	if ((hub = kmalloc(sizeof(*hub), GFP_KERNEL)) == NULL) {
		printk(KERN_ERR "couldn't kmalloc hub struct\n");
		return NULL;
	}

	memset(hub, 0, sizeof(*hub));

	INIT_LIST_HEAD(&hub->event_list);
	hub->dev = dev;

	/* Record the new hub's existence */
	spin_lock_irqsave(&hub_event_lock, flags);
	INIT_LIST_HEAD(&hub->hub_list);
	list_add(&hub->hub_list, &hub_list);
	spin_unlock_irqrestore(&hub_event_lock, flags);

	if (usb_hub_configure(hub) >= 0) {
		hub->irqpipe = usb_rcvctrlpipe(dev, endpoint->bEndpointAddress);
		ret = usb_request_irq(dev, hub->irqpipe,
			hub_irq, endpoint->bInterval,
			hub, &hub->irq_handle);
		if (ret) {
			printk (KERN_WARNING "usb-hub: usb_request_irq failed (0x%x)\n", ret);
			/* free hub, but first clean up its list. */
			spin_lock_irqsave(&hub_event_lock, flags);

			/* Delete it and then reset it */
			list_del(&hub->event_list);
			INIT_LIST_HEAD(&hub->event_list);
			list_del(&hub->hub_list);
			INIT_LIST_HEAD(&hub->hub_list);

			spin_unlock_irqrestore(&hub_event_lock, flags);

			kfree(hub);

			return NULL;
		}

		/* Wake up khubd */
		wake_up(&khubd_wait);
	}

	return hub;
}

static void hub_disconnect(struct usb_device *dev, void *ptr)
{
	struct usb_hub *hub = ptr;
	unsigned long flags;

	spin_lock_irqsave(&hub_event_lock, flags);

	/* Delete it and then reset it */
	list_del(&hub->event_list);
	INIT_LIST_HEAD(&hub->event_list);
	list_del(&hub->hub_list);
	INIT_LIST_HEAD(&hub->hub_list);

	spin_unlock_irqrestore(&hub_event_lock, flags);

	if (hub->irq_handle) {
		usb_release_irq(hub->dev, hub->irq_handle, hub->irqpipe);
	}

	/* Free the memory */
	kfree(hub);
}

static void usb_hub_port_connect_change(struct usb_device *hub, int port)
{
	struct usb_device *usb;
	struct usb_port_status portsts;
	unsigned short portstatus, portchange;

	/* Disconnect anything that may have been there */
	usb_disconnect(&hub->children[port]);

	/* Reset the port */
	usb_set_port_feature(hub, port + 1, USB_PORT_FEAT_RESET);

	wait_ms(50);	/* FIXME: This is from the *BSD stack, thanks! :) */

	/* Check status */
	if (usb_get_port_status(hub, port + 1, &portsts) < 0) {
		printk(KERN_ERR "get_port_status failed\n");
		return;
	}

	portstatus = le16_to_cpu(portsts.wPortStatus);
	portchange = le16_to_cpu(portsts.wPortChange);

	/* If it's not in CONNECT and ENABLE state, we're done */
	if ((!(portstatus & USB_PORT_STAT_CONNECTION)) &&
	    (!(portstatus & USB_PORT_STAT_ENABLE)))
		/* We're done now, we already disconnected the device */
		return;

	/* Allocate a new device struct for it */
	usb = usb_alloc_dev(hub, hub->bus);
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
		printk(KERN_DEBUG "hub: disabling port %d\n",
			port + 1);
		usb_clear_port_feature(hub, port + 1, USB_PORT_FEAT_ENABLE);
	}
}

static void usb_hub_events(void)
{
	unsigned long flags;
	int i;
	struct list_head *tmp;
	struct usb_device *dev;
	struct usb_hub *hub;

	/*
	 *  We restart the list everytime to avoid a deadlock with
	 * deleting hubs downstream from this one. This should be
	 * safe since we delete the hub from the event list.
	 * Not the most efficient, but avoids deadlocks.
	 */
	while (1) {
		spin_lock_irqsave(&hub_event_lock, flags);

		if (list_empty(&hub_event_list))
			goto he_unlock;

		/* Grab the next entry from the beginning of the list */
		tmp = hub_event_list.next;

		hub = list_entry(tmp, struct usb_hub, event_list);
		dev = hub->dev;

		list_del(tmp);
		INIT_LIST_HEAD(tmp);

		spin_unlock_irqrestore(&hub_event_lock, flags);

		for (i = 0; i < hub->nports; i++) {
			struct usb_port_status portsts;
			unsigned short portstatus, portchange;

			if (usb_get_port_status(dev, i + 1, &portsts) < 0) {
				printk(KERN_ERR "get_port_status failed\n");
				continue;
			}

			portstatus = le16_to_cpu(portsts.wPortStatus);
			portchange = le16_to_cpu(portsts.wPortChange);

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
		} /* end for i */
        } /* end while (1) */

he_unlock:
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

	if (usb_register(&hub_driver) < 0)
		return -1;

	pid = kernel_thread(usb_hub_thread, NULL,
		CLONE_FS | CLONE_FILES | CLONE_SIGHAND);
	if (pid >= 0) {
		khubd_pid = pid;

		return 0;
	}

	/* Fall through if kernel_thread failed */
	usb_deregister(&hub_driver);

	return -1;
}

void usb_hub_cleanup(void)
{
	int ret;

	/* Kill the thread */
	ret = kill_proc(khubd_pid, SIGTERM, 1);
	if (!ret) {
		/* Wait 10 seconds */
		int count = 10 * 100;

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
