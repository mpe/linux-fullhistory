/*
 * USB hub driver.
 *
 * (C) Copyright 1999 Linus Torvalds
 * (C) Copyright 1999 Johannes Erdfelt
 * (C) Copyright 1999 Gregory P. Smith
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <linux/malloc.h>
#include <linux/smp_lock.h>
#ifdef CONFIG_USB_DEBUG
	#define DEBUG
#else
	#undef DEBUG
#endif
#include <linux/usb.h>
#include <linux/usbdevice_fs.h>

#include <asm/semaphore.h>
#include <asm/uaccess.h>
#include <asm/byteorder.h>

#include "hub.h"

/* Wakes up khubd */
static spinlock_t hub_event_lock = SPIN_LOCK_UNLOCKED;
static DECLARE_MUTEX(usb_address0_sem);

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

static int usb_clear_hub_feature(struct usb_device *dev, int feature)
{
	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
		USB_REQ_CLEAR_FEATURE, USB_RT_HUB, feature, 0, NULL, 0, HZ);
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
static void hub_irq(struct urb *urb)
{
	struct usb_hub *hub = (struct usb_hub *)urb->context;
	unsigned long flags;

	if (urb->status) {
		if (urb->status != -ENOENT)
			dbg("nonzero status in irq %d", urb->status);

		return;
	}

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
}

static void usb_hub_power_on(struct usb_hub *hub)
{
	int i;

	/* Enable power to the ports */
	dbg("enabling power on all ports");
	for (i = 0; i < hub->nports; i++)
		usb_set_port_feature(hub->dev, i + 1, USB_PORT_FEAT_POWER);
}

static int usb_hub_configure(struct usb_hub *hub)
{
	struct usb_device *dev = hub->dev;
	unsigned char buffer[HUB_DESCRIPTOR_MAX_SIZE], *bitmap;
	struct usb_hub_descriptor *descriptor;
	struct usb_descriptor_header *header;
	struct usb_hub_status *hubsts;
	int i, ret;

	/* Request the entire hub descriptor. */
	header = (struct usb_descriptor_header *)buffer;
	ret = usb_get_hub_descriptor(dev, buffer, sizeof(buffer));
		/* <buffer> is large enough for a hub with 127 ports;
		 * the hub can/will return fewer bytes here. */
	if (ret < 0) {
		err("Unable to get hub descriptor (err = %d)", ret);
		return -1;
	}

	bitmap = kmalloc(header->bLength, GFP_KERNEL);
	if (!bitmap) {
		err("Unable to kmalloc %d bytes for bitmap", header->bLength);
		return -1;
	}

	memcpy (bitmap, buffer, header->bLength);
	descriptor = (struct usb_hub_descriptor *)bitmap;

	hub->nports = dev->maxchild = descriptor->bNbrPorts;
	info("%d port%s detected", hub->nports, (hub->nports == 1) ? "" : "s");

	switch (descriptor->wHubCharacteristics & HUB_CHAR_LPSM) {
		case 0x00:
			dbg("ganged power switching");
			break;
		case 0x01:
			dbg("individual port power switching");
			break;
		case 0x02:
		case 0x03:
			dbg("unknown reserved power switching mode");
			break;
	}

	if (descriptor->wHubCharacteristics & HUB_CHAR_COMPOUND)
		dbg("part of a compound device");
	else
		dbg("standalone hub");

	switch (descriptor->wHubCharacteristics & HUB_CHAR_OCPM) {
		case 0x00:
			dbg("global over-current protection");
			break;
		case 0x08:
			dbg("individual port over-current protection");
			break;
		case 0x10:
		case 0x18:
			dbg("no over-current protection");
                        break;
	}

	dbg("power on to power good time: %dms", descriptor->bPwrOn2PwrGood * 2);
	dbg("hub controller current requirement: %dmA", descriptor->bHubContrCurrent);

	for (i = 0; i < dev->maxchild; i++)
		dbg("port %d is%s removable", i + 1,
			bitmap[7 + ((i + 1)/8)] & (1 << ((i + 1) % 8))
			? " not" : "");

	kfree(bitmap);

	ret = usb_get_hub_status(dev, buffer);
	if (ret < 0) {
		err("Unable to get hub status (err = %d)", ret);
		return -1;
	}

	hubsts = (struct usb_hub_status *)buffer;
	dbg("local power source is %s",
		(le16_to_cpu(hubsts->wHubStatus) & HUB_STATUS_LOCAL_POWER) ? "lost (inactive)" : "good");

	dbg("%sover-current condition exists",
		(le16_to_cpu(hubsts->wHubStatus) & HUB_STATUS_OVERCURRENT) ? "" : "no ");

	usb_hub_power_on(hub);

	return 0;
}

static void *hub_probe(struct usb_device *dev, unsigned int i)
{
	struct usb_interface_descriptor *interface;
	struct usb_endpoint_descriptor *endpoint;
	struct usb_hub *hub;
	unsigned long flags;
	unsigned int pipe;
	int maxp, ret;

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
	if (!(endpoint->bEndpointAddress & USB_DIR_IN)) {
		err("Device is hub class, but has output endpoint?");
		return NULL;
	}

	/* If it's not an interrupt endpoint, we'd better punt! */
	if ((endpoint->bmAttributes & 3) != 3) {
		err("Device is hub class, but has endpoint other than interrupt?");
		return NULL;
	}

	/* We found a hub */
	info("USB hub found");

	hub = kmalloc(sizeof(*hub), GFP_KERNEL);
	if (!hub) {
		err("couldn't kmalloc hub struct");
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
		pipe = usb_rcvintpipe(dev, endpoint->bEndpointAddress);
		maxp = usb_maxpacket(dev, pipe, usb_pipeout(pipe));

		if (maxp > sizeof(hub->buffer))
			maxp = sizeof(hub->buffer);

		hub->urb = usb_alloc_urb(0);
		if (!hub->urb) {
			err("couldn't allocate interrupt urb");
			goto fail;
		}

		FILL_INT_URB(hub->urb, dev, pipe, hub->buffer, maxp, hub_irq,
			hub, endpoint->bInterval);
		ret = usb_submit_urb(hub->urb);
		if (ret) {
			err("usb_submit_urb failed (%d)", ret);
			goto fail;
		}

		/* Wake up khubd */
		wake_up(&khubd_wait);
	}

	return hub;

fail:
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

static void hub_disconnect(struct usb_device *dev, void *ptr)
{
	struct usb_hub *hub = (struct usb_hub *)ptr;
	unsigned long flags;

	spin_lock_irqsave(&hub_event_lock, flags);

	/* Delete it and then reset it */
	list_del(&hub->event_list);
	INIT_LIST_HEAD(&hub->event_list);
	list_del(&hub->hub_list);
	INIT_LIST_HEAD(&hub->hub_list);

	spin_unlock_irqrestore(&hub_event_lock, flags);

	if (hub->urb) {
		usb_unlink_urb(hub->urb);
		usb_free_urb(hub->urb);
		hub->urb = NULL;
	}

	/* Free the memory */
	kfree(hub);
}

static int hub_ioctl (struct usb_device *hub, unsigned int code, void *user_data)
{
	/* assert ifno == 0 (part of hub spec) */
	switch (code) {
	case USBDEVFS_HUB_PORTINFO: {
		struct usbdevfs_hub_portinfo *info = user_data;
		unsigned long flags;
		int i;

		spin_lock_irqsave (&hub_event_lock, flags);
		if (hub->devnum <= 0)
			info->nports = 0;
		else {
			info->nports = hub->maxchild;
			for (i = 0; i < info->nports; i++) {
				if (hub->children [i] == NULL)
					info->port [i] = 0;
				else
					info->port [i] = hub->children [i]->devnum;
			}
		}
		spin_unlock_irqrestore (&hub_event_lock, flags);

		return info->nports + 1;
		}

	default:
		return -ENOSYS;
	}
}

static void usb_hub_port_connect_change(struct usb_device *hub, int port)
{
	struct usb_device *usb;
	struct usb_port_status portsts;
	unsigned short portstatus, portchange;
	int ret, tries;

	wait_ms(100);

	ret = usb_get_port_status(hub, port + 1, &portsts);
	if (ret < 0) {
		err("get_port_status(%d) failed (err = %d)", port + 1, ret);
		return;
	}

	portstatus = le16_to_cpu(portsts.wPortStatus);
	portchange = le16_to_cpu(portsts.wPortChange);
	dbg("portstatus %x, change %x, %s", portstatus, portchange,
		portstatus&(1<<USB_PORT_FEAT_LOWSPEED) ? "1.5 Mb/s" : "12 Mb/s");

	/* Clear the connection change status */
	usb_clear_port_feature(hub, port + 1, USB_PORT_FEAT_C_CONNECTION);

	/* Disconnect any existing devices under this port */
	if (((!(portstatus & USB_PORT_STAT_CONNECTION)) &&
	     (!(portstatus & USB_PORT_STAT_ENABLE)))|| (hub->children[port])) {
		usb_disconnect(&hub->children[port]);
		/* Return now if nothing is connected */
		if (!(portstatus & USB_PORT_STAT_CONNECTION))
			return;
	}
	wait_ms(400);

	down(&usb_address0_sem);

#define MAX_TRIES 5
	/* Reset the port */
	for (tries = 0; tries < MAX_TRIES ; tries++) {
		usb_set_port_feature(hub, port + 1, USB_PORT_FEAT_RESET);
		wait_ms(200);

		ret = usb_get_port_status(hub, port + 1, &portsts);
		if (ret < 0) {
			err("get_port_status(%d) failed (err = %d)", port + 1, ret);
			goto out;
		}

		portstatus = le16_to_cpu(portsts.wPortStatus);
		portchange = le16_to_cpu(portsts.wPortChange);
		dbg("portstatus %x, change %x, %s", portstatus ,portchange,
			portstatus&(1<<USB_PORT_FEAT_LOWSPEED) ? "1.5 Mb/s" : "12 Mb/s");

		if ((portchange & USB_PORT_STAT_C_CONNECTION) ||
		    !(portstatus & USB_PORT_STAT_CONNECTION))
			goto out;

		if (portstatus & USB_PORT_STAT_ENABLE)
			break;

		wait_ms(200);
	}

	if (tries >= MAX_TRIES) {
		err("Cannot enable port %i after %i retries, disabling port.", port+1, MAX_TRIES);
		err("Maybe the USB cable is bad?");
		goto out;
	}

	usb_clear_port_feature(hub, port + 1, USB_PORT_FEAT_C_RESET);

	/* Allocate a new device struct for it */
	usb = usb_alloc_dev(hub, hub->bus);
	if (!usb) {
		err("couldn't allocate usb_device");
		goto out;
	}

	usb->slow = (portstatus & USB_PORT_STAT_LOW_SPEED) ? 1 : 0;

	hub->children[port] = usb;

	/* Find a new device ID for it */
	usb_connect(usb);

	/* Run it through the hoops (find a driver, etc) */
	ret = usb_new_device(usb);
	if (ret) {
		/* Try resetting the device. Windows does this and it */
		/*  gets some devices working correctly */
		usb_set_port_feature(hub, port + 1, USB_PORT_FEAT_RESET);

		ret = usb_new_device(usb);
		if (ret) {
			usb_disconnect(&hub->children[port]);

			/* Woops, disable the port */
			dbg("hub: disabling port %d", port + 1);
			usb_clear_port_feature(hub, port + 1,
				USB_PORT_FEAT_ENABLE);
		}
	}

out:
	up(&usb_address0_sem);
}

static void usb_hub_events(void)
{
	unsigned long flags;
	int i;
	struct list_head *tmp;
	struct usb_device *dev;
	struct usb_hub *hub;
	struct usb_hub_status hubsts;
	unsigned short hubstatus, hubchange;

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
				err("get_port_status failed");
				continue;
			}

			portstatus = le16_to_cpu(portsts.wPortStatus);
			portchange = le16_to_cpu(portsts.wPortChange);

			if (portchange & USB_PORT_STAT_C_CONNECTION) {
				dbg("port %d connection change", i + 1);

				usb_hub_port_connect_change(dev, i);
			}

			if (portchange & USB_PORT_STAT_C_ENABLE) {
				dbg("port %d enable change, status %x", i + 1, portstatus);
				usb_clear_port_feature(dev, i + 1, USB_PORT_FEAT_C_ENABLE);

				// EM interference sometimes causes bad shielded USB devices to 
				// be shutdown by the hub, this hack enables them again.
				// Works at least with mouse driver. 
				if (!(portstatus & USB_PORT_STAT_ENABLE) && 
				    (portstatus & USB_PORT_STAT_CONNECTION) && (dev->children[i])) {
					err("already running port %i disabled by hub (EMI?), re-enabling...",
						i + 1);
					usb_hub_port_connect_change(dev, i);
				}
			}

			if (portstatus & USB_PORT_STAT_SUSPEND) {
				dbg("port %d suspend change", i + 1);
				usb_clear_port_feature(dev, i + 1,  USB_PORT_FEAT_SUSPEND);
			}
			
			if (portchange & USB_PORT_STAT_C_OVERCURRENT) {
				err("port %d over-current change", i + 1);
				usb_clear_port_feature(dev, i + 1, USB_PORT_FEAT_C_OVER_CURRENT);
				usb_hub_power_on(hub);
			}

			if (portchange & USB_PORT_STAT_C_RESET) {
				dbg("port %d reset change", i + 1);
				usb_clear_port_feature(dev, i + 1, USB_PORT_FEAT_C_RESET);
			}
		} /* end for i */

		/* deal with hub status changes */
		if (usb_get_hub_status(dev, &hubsts) < 0) {
			err("get_hub_status failed");
		} else {
			hubstatus = le16_to_cpup(&hubsts.wHubStatus);
			hubchange = le16_to_cpup(&hubsts.wHubChange);
			if (hubchange & HUB_CHANGE_LOCAL_POWER) {
				dbg("hub power change");
				usb_clear_hub_feature(dev, C_HUB_LOCAL_POWER);
			}
			if (hubchange & HUB_CHANGE_OVERCURRENT) {
				dbg("hub overcurrent change");
				wait_ms(500); //Cool down
				usb_clear_hub_feature(dev, C_HUB_OVER_CURRENT);
                        	usb_hub_power_on(hub);
			}
		}
        } /* end while (1) */

he_unlock:
	spin_unlock_irqrestore(&hub_event_lock, flags);
}

static int usb_hub_thread(void *__hub)
{
	khubd_running = 1;

	lock_kernel();

	/*
	 * This thread doesn't need any user-level access,
	 * so get rid of all our resources
	 */
	exit_files(current);  /* daemonize doesn't do exit_files */
	current->files = init_task.files;
	atomic_inc(&current->files->count);
	daemonize();

	/* Setup a nice name */
	strcpy(current->comm, "khubd");

	/* Send me a signal to get me die (for debugging) */
	do {
		usb_hub_events();
		interruptible_sleep_on(&khubd_wait);
	} while (!signal_pending(current));

	dbg("usb_hub_thread exiting");
	khubd_running = 0;

	return 0;
}

static struct usb_driver hub_driver = {
	name:		"hub",
	probe:		hub_probe,
	ioctl:		hub_ioctl,
	disconnect:	hub_disconnect
};

/*
 * This should be a separate module.
 */
int usb_hub_init(void)
{
	int pid;

	if (usb_register(&hub_driver) < 0) {
		err("Unable to register USB hub driver");
		return -1;
	}

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
			err("giving up on killing khubd");
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

/*
 * WARNING - If a driver calls usb_reset_device, you should simulate a
 * disconnect() and probe() for other interfaces you doesn't claim. This
 * is left up to the driver writer right now. This insures other drivers
 * have a chance to re-setup their interface.
 *
 * Take a look at proc_resetdevice in devio.c for some sample code to
 * do this.
 */
int usb_reset_device(struct usb_device *dev)
{
	struct usb_device *parent = dev->parent;
	struct usb_device_descriptor descriptor;
	int i, ret, port = -1;

	if (!parent) {
		err("attempting to reset root hub!");
		return -EINVAL;
	}

	for (i = 0; i < parent->maxchild; i++)
		if (parent->children[i] == dev) {
			port = i;
			break;
		}

	if (port < 0)
		return -ENOENT;

	down(&usb_address0_sem);

	/* Send a reset to the device */
	usb_set_port_feature(parent, port + 1, USB_PORT_FEAT_RESET);

	wait_ms(200);

	usb_clear_port_feature(parent, port + 1, USB_PORT_FEAT_C_RESET);

	/* Reprogram the Address */
	ret = usb_set_address(dev);
	if (ret < 0) {
		err("USB device not accepting new address (error=%d)", ret);
		clear_bit(dev->devnum, &dev->bus->devmap.devicemap);
		dev->devnum = -1;
		up(&usb_address0_sem);
		return ret;
	}

	wait_ms(10);	/* Let the SET_ADDRESS settle */

	up(&usb_address0_sem);

	/*
	 * Now we fetch the configuration descriptors for the device and
	 * see if anything has changed. If it has, we dump the current
	 * parsed descriptors and reparse from scratch. Then we leave
	 * the device alone for the caller to finish setting up.
	 *
	 * If nothing changed, we reprogram the configuration and then
	 * the alternate settings.
	 */
	ret = usb_get_descriptor(dev, USB_DT_DEVICE, 0, &descriptor,
			sizeof(descriptor));
	if (ret < 0)
		return ret;

	le16_to_cpus(&descriptor.bcdUSB);
	le16_to_cpus(&descriptor.idVendor);
	le16_to_cpus(&descriptor.idProduct);
	le16_to_cpus(&descriptor.bcdDevice);

	if (memcmp(&dev->descriptor, &descriptor, sizeof(descriptor))) {
		usb_destroy_configuration(dev);

		ret = usb_get_device_descriptor(dev);
		if (ret < sizeof(dev->descriptor)) {
			if (ret < 0)
				err("unable to get device descriptor (error=%d)", ret);
			else
				err("USB device descriptor short read (expected %i, got %i)", sizeof(dev->descriptor), ret);
        
			clear_bit(dev->devnum, &dev->bus->devmap.devicemap);
			dev->devnum = -1;
			return -EIO;
		}

		ret = usb_get_configuration(dev);
		if (ret < 0) {
			err("unable to get configuration (error=%d)", ret);
			usb_destroy_configuration(dev);
			clear_bit(dev->devnum, &dev->bus->devmap.devicemap);
			dev->devnum = -1;
			return 1;
		}

		dev->actconfig = dev->config;
		usb_set_maxpacket(dev);

		return 1;
	} else {
		ret = usb_set_configuration(dev,
			dev->actconfig->bConfigurationValue);
		if (ret < 0) {
			err("failed to set active configuration (error=%d)",
				ret);
			return ret;
		}

		for (i = 0; i < dev->actconfig->bNumInterfaces; i++) {
			struct usb_interface *intf =
				&dev->actconfig->interface[i];
			struct usb_interface_descriptor *as =
				&intf->altsetting[intf->act_altsetting];

			ret = usb_set_interface(dev, as->bInterfaceNumber,
				as->bAlternateSetting);
			if (ret < 0) {
				err("failed to set active alternate setting for interface %d (error=%d)", i, ret);
				return ret;
			}
		}

		return 0;
	}

	return 0;
}

