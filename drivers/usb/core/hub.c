/*
 * USB hub driver.
 *
 * (C) Copyright 1999 Linus Torvalds
 * (C) Copyright 1999 Johannes Erdfelt
 * (C) Copyright 1999 Gregory P. Smith
 * (C) Copyright 2001 Brad Hards (bhards@bigpond.net.au)
 *
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/completion.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/ioctl.h>
#ifdef CONFIG_USB_DEBUG
	#define DEBUG
#else
	#undef DEBUG
#endif
#include <linux/usb.h>
#include <linux/usbdevice_fs.h>
#include <linux/suspend.h>

#include <asm/semaphore.h>
#include <asm/uaccess.h>
#include <asm/byteorder.h>

#include "usb.h"
#include "hcd.h"
#include "hub.h"

/* Wakes up khubd */
static spinlock_t hub_event_lock = SPIN_LOCK_UNLOCKED;

static LIST_HEAD(hub_event_list);	/* List of hubs needing servicing */
static LIST_HEAD(hub_list);		/* List of all hubs (for cleanup) */

static DECLARE_WAIT_QUEUE_HEAD(khubd_wait);
static pid_t khubd_pid = 0;			/* PID of khubd */
static DECLARE_COMPLETION(khubd_exited);

/* cycle leds on hubs that aren't blinking for attention */
static int blinkenlights = 0;
module_param (blinkenlights, bool, S_IRUGO);
MODULE_PARM_DESC (blinkenlights, "true to cycle leds on hubs");


#ifdef	DEBUG
static inline char *portspeed (int portstatus)
{
	if (portstatus & (1 << USB_PORT_FEAT_HIGHSPEED))
    		return "480 Mb/s";
	else if (portstatus & (1 << USB_PORT_FEAT_LOWSPEED))
		return "1.5 Mb/s";
	else
		return "12 Mb/s";
}
#endif

/* for dev_info, dev_dbg, etc */
static inline struct device *hubdev (struct usb_device *dev)
{
	return &dev->actconfig->interface[0]->dev;
}

/* USB 2.0 spec Section 11.24.4.5 */
static int get_hub_descriptor(struct usb_device *dev, void *data, int size)
{
	return usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
		USB_REQ_GET_DESCRIPTOR, USB_DIR_IN | USB_RT_HUB,
		USB_DT_HUB << 8, 0, data, size, HZ * USB_CTRL_GET_TIMEOUT);
}

/*
 * USB 2.0 spec Section 11.24.2.1
 */
static int clear_hub_feature(struct usb_device *dev, int feature)
{
	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
		USB_REQ_CLEAR_FEATURE, USB_RT_HUB, feature, 0, NULL, 0, HZ);
}

/*
 * USB 2.0 spec Section 11.24.2.2
 */
static int clear_port_feature(struct usb_device *dev, int port, int feature)
{
	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
		USB_REQ_CLEAR_FEATURE, USB_RT_PORT, feature, port, NULL, 0, HZ);
}

/*
 * USB 2.0 spec Section 11.24.2.13
 */
static int set_port_feature(struct usb_device *dev, int port, int feature)
{
	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
		USB_REQ_SET_FEATURE, USB_RT_PORT, feature, port, NULL, 0, HZ);
}

/*
 * USB 2.0 spec Section 11.24.2.7.1.10 and table 11-7
 * for info about using port indicators
 */
static void set_port_led(
	struct usb_device *dev,
	struct usb_hub *hub,
	int port,
	int selector
)
{
	int status = set_port_feature(dev, (selector << 8) | port,
			USB_PORT_FEAT_INDICATOR);
	if (status < 0)
		dev_dbg (&hub->intf->dev,
			"port %d indicator %s status %d\n",
			port,
			({ char *s; switch (selector) {
			case HUB_LED_AMBER: s = "amber"; break;
			case HUB_LED_GREEN: s = "green"; break;
			case HUB_LED_OFF: s = "off"; break;
			case HUB_LED_AUTO: s = "auto"; break;
			default: s = "??"; break;
			}; s; }),
			status);
}

#define	LED_CYCLE_PERIOD	((2*HZ)/3)

static void led_work (void *__hub)
{
	struct usb_hub		*hub = __hub;
	struct usb_device	*dev = interface_to_usbdev (hub->intf);
	unsigned		i;
	unsigned		changed = 0;
	int			cursor = -1;

	if (dev->state != USB_STATE_CONFIGURED)
		return;

	for (i = 0; i < hub->descriptor->bNbrPorts; i++) {
		unsigned	selector, mode;

		/* 30%-50% duty cycle */

		switch (hub->indicator[i]) {
		/* cycle marker */
		case INDICATOR_CYCLE:
			cursor = i;
			selector = HUB_LED_AUTO;
			mode = INDICATOR_AUTO;
			break;
		/* blinking green = sw attention */
		case INDICATOR_GREEN_BLINK:
			selector = HUB_LED_GREEN;
			mode = INDICATOR_GREEN_BLINK_OFF;
			break;
		case INDICATOR_GREEN_BLINK_OFF:
			selector = HUB_LED_OFF;
			mode = INDICATOR_GREEN_BLINK;
			break;
		/* blinking amber = hw attention */
		case INDICATOR_AMBER_BLINK:
			selector = HUB_LED_AMBER;
			mode = INDICATOR_AMBER_BLINK_OFF;
			break;
		case INDICATOR_AMBER_BLINK_OFF:
			selector = HUB_LED_OFF;
			mode = INDICATOR_AMBER_BLINK;
			break;
		/* blink green/amber = reserved */
		case INDICATOR_ALT_BLINK:
			selector = HUB_LED_GREEN;
			mode = INDICATOR_ALT_BLINK_OFF;
			break;
		case INDICATOR_ALT_BLINK_OFF:
			selector = HUB_LED_AMBER;
			mode = INDICATOR_ALT_BLINK;
			break;
		default:
			continue;
		}
		if (selector != HUB_LED_AUTO)
			changed = 1;
		set_port_led(dev, hub, i + 1, selector);
		hub->indicator[i] = mode;
	}
	if (!changed && blinkenlights) {
		cursor++;
		cursor %= hub->descriptor->bNbrPorts;
		set_port_led(dev, hub, cursor + 1, HUB_LED_GREEN);
		hub->indicator[cursor] = INDICATOR_CYCLE;
		changed++;
	}
	if (changed)
		schedule_delayed_work(&hub->leds, LED_CYCLE_PERIOD);
}

/*
 * USB 2.0 spec Section 11.24.2.6
 */
static int get_hub_status(struct usb_device *dev,
		struct usb_hub_status *data)
{
	return usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
		USB_REQ_GET_STATUS, USB_DIR_IN | USB_RT_HUB, 0, 0,
		data, sizeof(*data), HZ * USB_CTRL_GET_TIMEOUT);
}

/*
 * USB 2.0 spec Section 11.24.2.7
 */
static int get_port_status(struct usb_device *dev, int port,
		struct usb_port_status *data)
{
	return usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
		USB_REQ_GET_STATUS, USB_DIR_IN | USB_RT_PORT, 0, port,
		data, sizeof(*data), HZ * USB_CTRL_GET_TIMEOUT);
}

/* completion function, fires on port status changes and various faults */
static void hub_irq(struct urb *urb, struct pt_regs *regs)
{
	struct usb_hub *hub = (struct usb_hub *)urb->context;
	int status;

	spin_lock(&hub_event_lock);
	hub->urb_active = 0;
	if (hub->urb_complete) {	/* disconnect or rmmod */
		complete(hub->urb_complete);
		goto done;
	}

	switch (urb->status) {
	case -ENOENT:		/* synchronous unlink */
	case -ECONNRESET:	/* async unlink */
	case -ESHUTDOWN:	/* hardware going away */
		goto done;

	default:		/* presumably an error */
		/* Cause a hub reset after 10 consecutive errors */
		dev_dbg (&hub->intf->dev, "transfer --> %d\n", urb->status);
		if ((++hub->nerrors < 10) || hub->error)
			goto resubmit;
		hub->error = urb->status;
		/* FALL THROUGH */
	
	/* let khubd handle things */
	case 0:			/* we got data:  port status changed */
		break;
	}

	hub->nerrors = 0;

	/* Something happened, let khubd figure it out */
	if (list_empty(&hub->event_list)) {
		list_add(&hub->event_list, &hub_event_list);
		wake_up(&khubd_wait);
	}

resubmit:
	if ((status = usb_submit_urb (hub->urb, GFP_ATOMIC)) != 0
			/* ENODEV means we raced disconnect() */
			&& status != -ENODEV)
		dev_err (&hub->intf->dev, "resubmit --> %d\n", urb->status);
	if (status == 0)
		hub->urb_active = 1;
done:
	spin_unlock(&hub_event_lock);
}

/* USB 2.0 spec Section 11.24.2.3 */
static inline int
hub_clear_tt_buffer (struct usb_device *hub, u16 devinfo, u16 tt)
{
	return usb_control_msg (hub, usb_rcvctrlpipe (hub, 0),
		HUB_CLEAR_TT_BUFFER, USB_RT_PORT,
		devinfo, tt, 0, 0, HZ);
}

/*
 * enumeration blocks khubd for a long time. we use keventd instead, since
 * long blocking there is the exception, not the rule.  accordingly, HCDs
 * talking to TTs must queue control transfers (not just bulk and iso), so
 * both can talk to the same hub concurrently.
 */
static void hub_tt_kevent (void *arg)
{
	struct usb_hub		*hub = arg;
	unsigned long		flags;

	spin_lock_irqsave (&hub->tt.lock, flags);
	while (!list_empty (&hub->tt.clear_list)) {
		struct list_head	*temp;
		struct usb_tt_clear	*clear;
		struct usb_device	*dev;
		int			status;

		temp = hub->tt.clear_list.next;
		clear = list_entry (temp, struct usb_tt_clear, clear_list);
		list_del (&clear->clear_list);

		/* drop lock so HCD can concurrently report other TT errors */
		spin_unlock_irqrestore (&hub->tt.lock, flags);
		dev = interface_to_usbdev (hub->intf);
		status = hub_clear_tt_buffer (dev, clear->devinfo, clear->tt);
		spin_lock_irqsave (&hub->tt.lock, flags);

		if (status)
			dev_err (&dev->dev, "clear tt %d (%04x) error %d\n",
				clear->tt, clear->devinfo, status);
		kfree (clear);
	}
	spin_unlock_irqrestore (&hub->tt.lock, flags);
}

/**
 * usb_hub_tt_clear_buffer - clear control/bulk TT state in high speed hub
 * @dev: the device whose split transaction failed
 * @pipe: identifies the endpoint of the failed transaction
 *
 * High speed HCDs use this to tell the hub driver that some split control or
 * bulk transaction failed in a way that requires clearing internal state of
 * a transaction translator.  This is normally detected (and reported) from
 * interrupt context.
 *
 * It may not be possible for that hub to handle additional full (or low)
 * speed transactions until that state is fully cleared out.
 */
void usb_hub_tt_clear_buffer (struct usb_device *dev, int pipe)
{
	struct usb_tt		*tt = dev->tt;
	unsigned long		flags;
	struct usb_tt_clear	*clear;

	/* we've got to cope with an arbitrary number of pending TT clears,
	 * since each TT has "at least two" buffers that can need it (and
	 * there can be many TTs per hub).  even if they're uncommon.
	 */
	if ((clear = kmalloc (sizeof *clear, SLAB_ATOMIC)) == 0) {
		dev_err (&dev->dev, "can't save CLEAR_TT_BUFFER state\n");
		/* FIXME recover somehow ... RESET_TT? */
		return;
	}

	/* info that CLEAR_TT_BUFFER needs */
	clear->tt = tt->multi ? dev->ttport : 1;
	clear->devinfo = usb_pipeendpoint (pipe);
	clear->devinfo |= dev->devnum << 4;
	clear->devinfo |= usb_pipecontrol (pipe)
			? (USB_ENDPOINT_XFER_CONTROL << 11)
			: (USB_ENDPOINT_XFER_BULK << 11);
	if (usb_pipein (pipe))
		clear->devinfo |= 1 << 15;
	
	/* tell keventd to clear state for this TT */
	spin_lock_irqsave (&tt->lock, flags);
	list_add_tail (&clear->clear_list, &tt->clear_list);
	schedule_work (&tt->kevent);
	spin_unlock_irqrestore (&tt->lock, flags);
}

static void hub_power_on(struct usb_hub *hub)
{
	struct usb_device *dev;
	int i;

	/* if hub supports power switching, enable power on each port */
	if ((hub->descriptor->wHubCharacteristics & HUB_CHAR_LPSM) < 2) {
		dev_dbg(&hub->intf->dev, "enabling power on all ports\n");
		dev = interface_to_usbdev(hub->intf);
		for (i = 0; i < hub->descriptor->bNbrPorts; i++)
			set_port_feature(dev, i + 1, USB_PORT_FEAT_POWER);
	}

	/* Wait for power to be enabled */
	usb_uninterruptible_sleep_ms(hub->descriptor->bPwrOn2PwrGood * 2);
}

static int hub_hub_status(struct usb_hub *hub,
		u16 *status, u16 *change)
{
	struct usb_device *dev = interface_to_usbdev (hub->intf);
	int ret;

	ret = get_hub_status(dev, &hub->status->hub);
	if (ret < 0)
		dev_err (hubdev (dev),
			"%s failed (err = %d)\n", __FUNCTION__, ret);
	else {
		*status = le16_to_cpu(hub->status->hub.wHubStatus);
		*change = le16_to_cpu(hub->status->hub.wHubChange); 
		ret = 0;
	}
	return ret;
}

static int hub_configure(struct usb_hub *hub,
	struct usb_endpoint_descriptor *endpoint)
{
	struct usb_device *dev = interface_to_usbdev (hub->intf);
	struct device *hub_dev;
	u16 hubstatus, hubchange;
	unsigned int pipe;
	int maxp, ret;
	char *message;

	hub->buffer = usb_buffer_alloc(dev, sizeof(*hub->buffer), GFP_KERNEL,
			&hub->buffer_dma);
	if (!hub->buffer) {
		message = "can't allocate hub irq buffer";
		ret = -ENOMEM;
		goto fail;
	}

	hub->status = kmalloc(sizeof(*hub->status), GFP_KERNEL);
	if (!hub->status) {
		message = "can't kmalloc hub status buffer";
		ret = -ENOMEM;
		goto fail;
	}

	hub->descriptor = kmalloc(sizeof(*hub->descriptor), GFP_KERNEL);
	if (!hub->descriptor) {
		message = "can't kmalloc hub descriptor";
		ret = -ENOMEM;
		goto fail;
	}

	/* Request the entire hub descriptor.
	 * hub->descriptor can handle USB_MAXCHILDREN ports,
	 * but the hub can/will return fewer bytes here.
	 */
	ret = get_hub_descriptor(dev, hub->descriptor,
			sizeof(*hub->descriptor));
	if (ret < 0) {
		message = "can't read hub descriptor";
		goto fail;
	} else if (hub->descriptor->bNbrPorts > USB_MAXCHILDREN) {
		message = "hub has too many ports!";
		ret = -ENODEV;
		goto fail;
	}

	hub_dev = hubdev(dev);
	dev->maxchild = hub->descriptor->bNbrPorts;
	dev_info (hub_dev, "%d port%s detected\n", dev->maxchild,
		(dev->maxchild == 1) ? "" : "s");

	le16_to_cpus(&hub->descriptor->wHubCharacteristics);

	if (hub->descriptor->wHubCharacteristics & HUB_CHAR_COMPOUND) {
		int	i;
		char	portstr [USB_MAXCHILDREN + 1];

		for (i = 0; i < dev->maxchild; i++)
			portstr[i] = hub->descriptor->DeviceRemovable
				    [((i + 1) / 8)] & (1 << ((i + 1) % 8))
				? 'F' : 'R';
		portstr[dev->maxchild] = 0;
		dev_dbg(hub_dev, "compound device; port removable status: %s\n", portstr);
	} else
		dev_dbg(hub_dev, "standalone hub\n");

	switch (hub->descriptor->wHubCharacteristics & HUB_CHAR_LPSM) {
		case 0x00:
			dev_dbg(hub_dev, "ganged power switching\n");
			break;
		case 0x01:
			dev_dbg(hub_dev, "individual port power switching\n");
			break;
		case 0x02:
		case 0x03:
			dev_dbg(hub_dev, "no power switching (usb 1.0)\n");
			break;
	}

	switch (hub->descriptor->wHubCharacteristics & HUB_CHAR_OCPM) {
		case 0x00:
			dev_dbg(hub_dev, "global over-current protection\n");
			break;
		case 0x08:
			dev_dbg(hub_dev, "individual port over-current protection\n");
			break;
		case 0x10:
		case 0x18:
			dev_dbg(hub_dev, "no over-current protection\n");
                        break;
	}

	spin_lock_init (&hub->tt.lock);
	INIT_LIST_HEAD (&hub->tt.clear_list);
	INIT_WORK (&hub->tt.kevent, hub_tt_kevent, hub);
	switch (dev->descriptor.bDeviceProtocol) {
		case 0:
			break;
		case 1:
			dev_dbg(hub_dev, "Single TT\n");
			hub->tt.hub = dev;
			break;
		case 2:
			ret = usb_set_interface(dev, 0, 1);
			if (ret == 0) {
				dev_dbg(hub_dev, "TT per port\n");
				hub->tt.multi = 1;
			} else
				dev_err(hub_dev, "Using single TT (err %d)\n",
					ret);
			hub->tt.hub = dev;
			break;
		default:
			dev_dbg(hub_dev, "Unrecognized hub protocol %d\n",
				dev->descriptor.bDeviceProtocol);
			break;
	}

	switch (hub->descriptor->wHubCharacteristics & HUB_CHAR_TTTT) {
		case 0x00:
			if (dev->descriptor.bDeviceProtocol != 0)
				dev_dbg(hub_dev, "TT requires at most 8 FS bit times\n");
			break;
		case 0x20:
			dev_dbg(hub_dev, "TT requires at most 16 FS bit times\n");
			break;
		case 0x40:
			dev_dbg(hub_dev, "TT requires at most 24 FS bit times\n");
			break;
		case 0x60:
			dev_dbg(hub_dev, "TT requires at most 32 FS bit times\n");
			break;
	}

	/* probe() zeroes hub->indicator[] */
	if (hub->descriptor->wHubCharacteristics & HUB_CHAR_PORTIND) {
		hub->has_indicators = 1;
		dev_dbg(hub_dev, "Port indicators are supported\n");
	}

	dev_dbg(hub_dev, "power on to power good time: %dms\n",
		hub->descriptor->bPwrOn2PwrGood * 2);

	/* power budgeting mostly matters with bus-powered hubs,
	 * and battery-powered root hubs (may provide just 8 mA).
	 */
	ret = usb_get_status(dev, USB_RECIP_DEVICE, 0, &hubstatus);
	if (ret < 0) {
		message = "can't get hubdev status";
		goto fail;
	}
	cpu_to_le16s(&hubstatus);
	if ((hubstatus & (1 << USB_DEVICE_SELF_POWERED)) == 0) {
		dev_dbg(hub_dev, "hub controller current requirement: %dmA\n",
			hub->descriptor->bHubContrCurrent);
		hub->power_budget = (501 - hub->descriptor->bHubContrCurrent)
					/ 2;
		dev_dbg(hub_dev, "%dmA bus power budget for children\n",
			hub->power_budget * 2);
	}


	ret = hub_hub_status(hub, &hubstatus, &hubchange);
	if (ret < 0) {
		message = "can't get hub status";
		goto fail;
	}

	/* local power status reports aren't always correct */
	if (dev->actconfig->desc.bmAttributes & USB_CONFIG_ATT_SELFPOWER)
		dev_dbg(hub_dev, "local power source is %s\n",
			(hubstatus & HUB_STATUS_LOCAL_POWER)
			? "lost (inactive)" : "good");

	if ((hub->descriptor->wHubCharacteristics & HUB_CHAR_OCPM) == 0)
		dev_dbg(hub_dev, "%sover-current condition exists\n",
			(hubstatus & HUB_STATUS_OVERCURRENT) ? "" : "no ");

	/* Start the interrupt endpoint */
	pipe = usb_rcvintpipe(dev, endpoint->bEndpointAddress);
	maxp = usb_maxpacket(dev, pipe, usb_pipeout(pipe));

	if (maxp > sizeof(*hub->buffer))
		maxp = sizeof(*hub->buffer);

	hub->urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!hub->urb) {
		message = "couldn't allocate interrupt urb";
		ret = -ENOMEM;
		goto fail;
	}

	usb_fill_int_urb(hub->urb, dev, pipe, *hub->buffer, maxp, hub_irq,
		hub, endpoint->bInterval);
	hub->urb->transfer_dma = hub->buffer_dma;
	hub->urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	ret = usb_submit_urb(hub->urb, GFP_KERNEL);
	if (ret) {
		message = "couldn't submit status urb";
		goto fail;
	}
	hub->urb_active = 1;

	/* Wake up khubd */
	wake_up(&khubd_wait);

	/* maybe start cycling the hub leds */
	if (hub->has_indicators && blinkenlights) {
		set_port_led(dev, hub, 1, HUB_LED_GREEN);
		hub->indicator [0] = INDICATOR_CYCLE;
		schedule_delayed_work(&hub->leds, LED_CYCLE_PERIOD);
	}

	hub_power_on(hub);

	return 0;

fail:
	dev_err (&hub->intf->dev, "config failed, %s (err %d)\n",
			message, ret);
	/* hub_disconnect() frees urb and descriptor */
	return ret;
}

static unsigned highspeed_hubs;

static void hub_disconnect(struct usb_interface *intf)
{
	struct usb_hub *hub = usb_get_intfdata (intf);
	DECLARE_COMPLETION(urb_complete);
	unsigned long flags;

	if (!hub)
		return;

	if (interface_to_usbdev(intf)->speed == USB_SPEED_HIGH)
		highspeed_hubs--;

	usb_set_intfdata (intf, NULL);
	spin_lock_irqsave(&hub_event_lock, flags);
	hub->urb_complete = &urb_complete;

	/* Delete it and then reset it */
	list_del_init(&hub->event_list);
	list_del_init(&hub->hub_list);

	spin_unlock_irqrestore(&hub_event_lock, flags);

	down(&hub->khubd_sem); /* Wait for khubd to leave this hub alone. */
	up(&hub->khubd_sem);

	/* assuming we used keventd, it must quiesce too */
	if (hub->has_indicators)
		cancel_delayed_work (&hub->leds);
	if (hub->has_indicators || hub->tt.hub)
		flush_scheduled_work ();

	if (hub->urb) {
		usb_unlink_urb(hub->urb);
		if (hub->urb_active)
			wait_for_completion(&urb_complete);
		usb_free_urb(hub->urb);
		hub->urb = NULL;
	}

	if (hub->descriptor) {
		kfree(hub->descriptor);
		hub->descriptor = NULL;
	}

	if (hub->status) {
		kfree(hub->status);
		hub->status = NULL;
	}

	if (hub->buffer) {
		usb_buffer_free(interface_to_usbdev(intf),
				sizeof(*hub->buffer), hub->buffer,
				hub->buffer_dma);
		hub->buffer = NULL;
	}

	/* Free the memory */
	kfree(hub);
}

static int hub_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_host_interface *desc;
	struct usb_endpoint_descriptor *endpoint;
	struct usb_device *dev;
	struct usb_hub *hub;
	unsigned long flags;

	desc = intf->cur_altsetting;
	dev = interface_to_usbdev(intf);

	/* Some hubs have a subclass of 1, which AFAICT according to the */
	/*  specs is not defined, but it works */
	if ((desc->desc.bInterfaceSubClass != 0) &&
	    (desc->desc.bInterfaceSubClass != 1)) {
descriptor_error:
		dev_err (&intf->dev, "bad descriptor, ignoring hub\n");
		return -EIO;
	}

	/* Multiple endpoints? What kind of mutant ninja-hub is this? */
	if (desc->desc.bNumEndpoints != 1) {
		goto descriptor_error;
	}

	endpoint = &desc->endpoint[0].desc;

	/* Output endpoint? Curiouser and curiouser.. */
	if (!(endpoint->bEndpointAddress & USB_DIR_IN)) {
		goto descriptor_error;
	}

	/* If it's not an interrupt endpoint, we'd better punt! */
	if ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
			!= USB_ENDPOINT_XFER_INT) {
		goto descriptor_error;
		return -EIO;
	}

	/* We found a hub */
	dev_info (hubdev (dev), "USB hub found\n");

	hub = kmalloc(sizeof(*hub), GFP_KERNEL);
	if (!hub) {
		dev_dbg (hubdev(dev), "couldn't kmalloc hub struct\n");
		return -ENOMEM;
	}

	memset(hub, 0, sizeof(*hub));

	INIT_LIST_HEAD(&hub->event_list);
	hub->intf = intf;
	init_MUTEX(&hub->khubd_sem);
	INIT_WORK(&hub->leds, led_work, hub);

	/* Record the new hub's existence */
	spin_lock_irqsave(&hub_event_lock, flags);
	INIT_LIST_HEAD(&hub->hub_list);
	list_add(&hub->hub_list, &hub_list);
	spin_unlock_irqrestore(&hub_event_lock, flags);

	usb_set_intfdata (intf, hub);

	if (dev->speed == USB_SPEED_HIGH)
		highspeed_hubs++;

	if (hub_configure(hub, endpoint) >= 0)
		return 0;

	hub_disconnect (intf);
	return -ENODEV;
}

static int
hub_ioctl(struct usb_interface *intf, unsigned int code, void *user_data)
{
	struct usb_device *hub = interface_to_usbdev (intf);

	/* assert ifno == 0 (part of hub spec) */
	switch (code) {
	case USBDEVFS_HUB_PORTINFO: {
		struct usbdevfs_hub_portinfo *info = user_data;
		unsigned long flags;
		int i;

		spin_lock_irqsave(&hub_event_lock, flags);
		if (hub->devnum <= 0)
			info->nports = 0;
		else {
			info->nports = hub->maxchild;
			for (i = 0; i < info->nports; i++) {
				if (hub->children[i] == NULL)
					info->port[i] = 0;
				else
					info->port[i] =
						hub->children[i]->devnum;
			}
		}
		spin_unlock_irqrestore(&hub_event_lock, flags);

		return info->nports + 1;
		}

	default:
		return -ENOSYS;
	}
}

static int hub_reset(struct usb_hub *hub)
{
	struct usb_device *dev = interface_to_usbdev(hub->intf);
	int i;

	/* Disconnect any attached devices */
	for (i = 0; i < hub->descriptor->bNbrPorts; i++) {
		if (dev->children[i])
			usb_disconnect(&dev->children[i]);
	}

	/* Attempt to reset the hub */
	if (hub->urb)
		usb_unlink_urb(hub->urb);
	else
		return -1;

	if (usb_reset_device(dev))
		return -1;

	hub->urb->dev = dev;                                                    
	if (usb_submit_urb(hub->urb, GFP_KERNEL))
		return -1;

	hub_power_on(hub);

	return 0;
}

static void hub_start_disconnect(struct usb_device *dev)
{
	struct usb_device *parent = dev->parent;
	int i;

	/* Find the device pointer to disconnect */
	if (parent) {
		for (i = 0; i < parent->maxchild; i++) {
			if (parent->children[i] == dev) {
				usb_disconnect(&parent->children[i]);
				return;
			}
		}
	}

	dev_err(&dev->dev, "cannot disconnect hub!\n");
}

static int hub_port_status(struct usb_device *dev, int port,
			       u16 *status, u16 *change)
{
	struct usb_hub *hub = usb_get_intfdata(dev->actconfig->interface[0]);
	int ret;

	if (!hub)
		return -ENODEV;

	ret = get_port_status(dev, port + 1, &hub->status->port);
	if (ret < 0)
		dev_err (hubdev (dev),
			"%s failed (err = %d)\n", __FUNCTION__, ret);
	else {
		*status = le16_to_cpu(hub->status->port.wPortStatus);
		*change = le16_to_cpu(hub->status->port.wPortChange); 
		ret = 0;
	}
	return ret;
}

#define PORT_RESET_TRIES	5
#define SET_ADDRESS_TRIES	2
#define GET_DESCRIPTOR_TRIES	2
#define SET_CONFIG_TRIES	2

#define HUB_ROOT_RESET_TIME	50	/* times are in msec */
#define HUB_SHORT_RESET_TIME	10
#define HUB_LONG_RESET_TIME	200
#define HUB_RESET_TIMEOUT	500

/* return: -1 on error, 0 on success, 1 on disconnect.  */
static int hub_port_wait_reset(struct usb_device *hub, int port,
				struct usb_device *dev, unsigned int delay)
{
	int delay_time, ret;
	u16 portstatus;
	u16 portchange;

	for (delay_time = 0;
			delay_time < HUB_RESET_TIMEOUT;
			delay_time += delay) {
		/* wait to give the device a chance to reset */
		usb_uninterruptible_sleep_ms(delay);

		/* read and decode port status */
		ret = hub_port_status(hub, port, &portstatus, &portchange);
		if (ret < 0) {
			return -1;
		}

		/* Device went away? */
		if (!(portstatus & USB_PORT_STAT_CONNECTION))
			return 1;

		/* bomb out completely if something weird happened */
		if ((portchange & USB_PORT_STAT_C_CONNECTION))
			return -1;

		/* if we`ve finished resetting, then break out of the loop */
		if (!(portstatus & USB_PORT_STAT_RESET) &&
		    (portstatus & USB_PORT_STAT_ENABLE)) {
			if (portstatus & USB_PORT_STAT_HIGH_SPEED)
				dev->speed = USB_SPEED_HIGH;
			else if (portstatus & USB_PORT_STAT_LOW_SPEED)
				dev->speed = USB_SPEED_LOW;
			else
				dev->speed = USB_SPEED_FULL;
			return 0;
		}

		/* switch to the long delay after two short delay failures */
		if (delay_time >= 2 * HUB_SHORT_RESET_TIME)
			delay = HUB_LONG_RESET_TIME;

		dev_dbg (hubdev (hub),
			"port %d not reset yet, waiting %dms\n",
			port + 1, delay);
	}

	return -1;
}

/* return: -1 on error, 0 on success, 1 on disconnect.  */
static int hub_port_reset(struct usb_device *hub, int port,
				struct usb_device *dev, unsigned int delay)
{
	int i, status;

	/* Reset the port */
	for (i = 0; i < PORT_RESET_TRIES; i++) {
		set_port_feature(hub, port + 1, USB_PORT_FEAT_RESET);

		/* return on disconnect or reset */
		status = hub_port_wait_reset(hub, port, dev, delay);
		if (status != -1) {
			clear_port_feature(hub,
				port + 1, USB_PORT_FEAT_C_RESET);
			dev->state = status
					? USB_STATE_NOTATTACHED
					: USB_STATE_DEFAULT;
			return status;
		}

		dev_dbg (hubdev (hub),
			"port %d not enabled, trying reset again...\n",
			port + 1);
		delay = HUB_LONG_RESET_TIME;
	}

	dev_err (hubdev (hub),
		"Cannot enable port %i.  Maybe the USB cable is bad?\n",
		port + 1);

	return -1;
}

static int hub_port_disable(struct usb_device *hub, int port)
{
	int ret;

	ret = clear_port_feature(hub, port + 1, USB_PORT_FEAT_ENABLE);
	if (ret)
		dev_err(hubdev(hub), "cannot disable port %d (err = %d)\n",
			port + 1, ret);

	return ret;
}

/* USB 2.0 spec, 7.1.7.3 / fig 7-29:
 *
 * Between connect detection and reset signaling there must be a delay
 * of 100ms at least for debounce and power-settling. The corresponding
 * timer shall restart whenever the downstream port detects a disconnect.
 * 
 * Apparently there are some bluetooth and irda-dongles and a number
 * of low-speed devices which require longer delays of about 200-400ms.
 * Not covered by the spec - but easy to deal with.
 *
 * This implementation uses 400ms minimum debounce timeout and checks
 * every 25ms for transient disconnects to restart the delay.
 */

#define HUB_DEBOUNCE_TIMEOUT	400
#define HUB_DEBOUNCE_STEP	 25
#define HUB_DEBOUNCE_STABLE	  4

/* return: -1 on error, 0 on success, 1 on disconnect.  */
static int hub_port_debounce(struct usb_device *hub, int port)
{
	int ret;
	int delay_time, stable_count;
	u16 portchange, portstatus;
	unsigned connection;

	connection = 0;
	stable_count = 0;
	for (delay_time = 0; delay_time < HUB_DEBOUNCE_TIMEOUT; delay_time += HUB_DEBOUNCE_STEP) {
		usb_uninterruptible_sleep_ms(HUB_DEBOUNCE_STEP);

		ret = hub_port_status(hub, port, &portstatus, &portchange);
		if (ret < 0)
			return -1;

		if ((portstatus & USB_PORT_STAT_CONNECTION) == connection) {
			if (connection) {
				if (++stable_count == HUB_DEBOUNCE_STABLE)
					break;
			}
		} else {
			stable_count = 0;
		}
		connection = portstatus & USB_PORT_STAT_CONNECTION;

		if ((portchange & USB_PORT_STAT_C_CONNECTION)) {
			clear_port_feature(hub, port+1, USB_PORT_FEAT_C_CONNECTION);
		}
	}

	dev_dbg (hubdev (hub),
		"debounce: port %d: delay %dms stable %d status 0x%x\n",
		port + 1, delay_time, stable_count, portstatus);

	return ((portstatus&USB_PORT_STAT_CONNECTION)) ? 0 : 1;
}

/* reset device, (re)assign address, get device descriptor.
 * device connection is stable, no more debouncing needed.
 * returns device in USB_STATE_ADDRESS, except on error.
 * on error return, device is no longer usable (ref dropped).
 *
 * caller owns dev->serialize for the device, guarding against
 * config changes and disconnect processing.
 */
static int
hub_port_init (struct usb_device *hub, struct usb_device *dev, int port)
{
	static DECLARE_MUTEX(usb_address0_sem);

	int			i, j, retval = -ENODEV;
	unsigned		delay = HUB_SHORT_RESET_TIME;
	enum usb_device_speed	oldspeed = dev->speed;

	/* root hub ports have a slightly longer reset period
	 * (from USB 2.0 spec, section 7.1.7.5)
	 */
	if (!hub->parent)
		delay = HUB_ROOT_RESET_TIME;

	/* Some low speed devices have problems with the quick delay, so */
	/*  be a bit pessimistic with those devices. RHbug #23670 */
	if (oldspeed == USB_SPEED_LOW)
		delay = HUB_LONG_RESET_TIME;

	down(&usb_address0_sem);

	/* Reset the device; full speed may morph to high speed */
	switch (hub_port_reset(hub, port, dev, delay)) {
	case 0:			/* success, speed is known */
		break;
	case 1:			/* disconnect, give to companion */
		retval = -EBUSY;
		/* FALL THROUGH */
	default:		/* error */
		goto fail;
	}
	if (oldspeed != USB_SPEED_UNKNOWN && oldspeed != dev->speed) {
		dev_dbg(&dev->dev, "device reset changed speed!\n");
		goto fail;
	}
  
	/* USB 2.0 section 5.5.3 talks about ep0 maxpacket ...
	 * it's fixed size except for full speed devices.
	 */
	switch (dev->speed) {
	case USB_SPEED_HIGH:		/* fixed at 64 */
		i = 64;
		break;
	case USB_SPEED_FULL:		/* 8, 16, 32, or 64 */
		/* to determine the ep0 maxpacket size, read the first 8
		 * bytes from the device descriptor to get bMaxPacketSize0;
		 * then correct our initial (small) guess.
		 */
		// FALLTHROUGH
	case USB_SPEED_LOW:		/* fixed at 8 */
		i = 8;
		break;
	default:
		goto fail;
	}
	dev->epmaxpacketin [0] = i;
	dev->epmaxpacketout[0] = i;
 
	/* set the address */
	if (dev->devnum <= 0) {
		usb_choose_address(dev);
		if (dev->devnum <= 0)
			goto fail;

		/* Set up TT records, if needed  */
		if (hub->tt) {
			dev->tt = hub->tt;
			dev->ttport = hub->ttport;
		} else if (dev->speed != USB_SPEED_HIGH
				&& hub->speed == USB_SPEED_HIGH) {
			struct usb_hub	*hubstate;
 
			hubstate = usb_get_intfdata (hub->actconfig
							->interface[0]);
			dev->tt = &hubstate->tt;
			dev->ttport = port + 1;
		}

		/* force the right log message (below) at low speed */
		oldspeed = USB_SPEED_UNKNOWN;
	}
 
	dev_info (&dev->dev,
			"%s %s speed USB device using address %d\n",
			(oldspeed == USB_SPEED_UNKNOWN) ? "new" : "reset",
			({ char *speed; switch (dev->speed) {
			case USB_SPEED_LOW:	speed = "low";	break;
			case USB_SPEED_FULL:	speed = "full";	break;
			case USB_SPEED_HIGH:	speed = "high";	break;
			default: 		speed = "?";	break;
			}; speed;}),
			dev->devnum);
 
	/* Why interleave GET_DESCRIPTOR and SET_ADDRESS this way?
	 * Because device hardware and firmware is sometimes buggy in
	 * this area, and this is how Linux has done it for ages.
	 * Change it cautiously.
	 *
	 * NOTE:  Windows gets the descriptor first, seemingly to help
	 * work around device bugs like "can't use addresses with bit 3
	 * set in certain configurations".  Yes, really.
	 */
	for (i = 0; i < GET_DESCRIPTOR_TRIES; ++i) {
		for (j = 0; j < SET_ADDRESS_TRIES; ++j) {
			retval = usb_set_address(dev);
			if (retval >= 0)
				break;
			usb_uninterruptible_sleep_ms(200);
		}
		if (retval < 0) {
			dev_err(&dev->dev,
				"device not accepting address %d, error %d\n",
				dev->devnum, retval);
 fail:
			hub_port_disable(hub, port);
			clear_bit(dev->devnum, dev->bus->devmap.devicemap);
			dev->devnum = -1;
			usb_put_dev(dev);
			up(&usb_address0_sem);
			return retval;
		}
 
		/* cope with hardware quirkiness:
		 *  - let SET_ADDRESS settle, some device hardware wants it
		 *  - read ep0 maxpacket even for high and low speed,
  		 */
		usb_uninterruptible_sleep_ms(10);
		retval = usb_get_device_descriptor(dev, 8);
		if (retval >= 8)
			break;
		usb_uninterruptible_sleep_ms(100);
	}
	if (retval != 8) {
		dev_err(&dev->dev, "device descriptor read/%s, error %d\n",
				"8", retval);
		if (retval >= 0)
			retval = -EMSGSIZE;
		goto fail;
	}
	if (dev->speed == USB_SPEED_FULL
			&& (dev->epmaxpacketin [0]
				!= dev->descriptor.bMaxPacketSize0)) {
		usb_disable_endpoint(dev, 0);
		usb_endpoint_running(dev, 0, 1);
		usb_endpoint_running(dev, 0, 0);
		dev->epmaxpacketin [0] = dev->descriptor.bMaxPacketSize0;
		dev->epmaxpacketout[0] = dev->descriptor.bMaxPacketSize0;
	}
  
	retval = usb_get_device_descriptor(dev, USB_DT_DEVICE_SIZE);
	if (retval < (signed)sizeof(dev->descriptor)) {
		dev_err(&dev->dev, "device descriptor read/%s, error %d\n",
			"all", retval);
		if (retval >= 0)
			retval = -ENOMSG;
		goto fail;
	}

	/* now dev is visible to other tasks */
	hub->children[port] = dev;

	up(&usb_address0_sem);
	return 0;
}

static void
check_highspeed (struct usb_hub *hub, struct usb_device *dev, int port)
{
	struct usb_qualifier_descriptor	*qual;
	int				status;

	qual = kmalloc (sizeof *qual, SLAB_KERNEL);
	if (qual == 0)
		return;

	status = usb_get_descriptor (dev, USB_DT_DEVICE_QUALIFIER, 0,
			qual, sizeof *qual);
	if (status == sizeof *qual) {
		dev_info(&dev->dev, "not running at top speed; "
			"connect to a high speed hub\n");
		/* hub LEDs are probably harder to miss than syslog */
		if (hub->has_indicators) {
			hub->indicator[port] = INDICATOR_GREEN_BLINK;
			schedule_work (&hub->leds);
		}
	}
	kfree (qual);
}

static unsigned
hub_power_remaining (struct usb_hub *hubstate, struct usb_device *hub)
{
	int remaining;
	unsigned i;

	remaining = hubstate->power_budget;
	if (!remaining)		/* self-powered */
		return 0;

	for (i = 0; i < hub->maxchild; i++) {
		struct usb_device	*dev = hub->children[i];
		int			delta;

		if (!dev)
			continue;

		if (dev->actconfig)
			delta = dev->actconfig->desc.bMaxPower;
		else
			delta = 50;
		// dev_dbg(&dev->dev, "budgeted %dmA\n", 2 * delta);
		remaining -= delta;
	}
	if (remaining < 0) {
		dev_warn(&hubstate->intf->dev,
			"%dmA over power budget!\n",
			-2 * remaining);
		remaining = 0;
	}
	return remaining;
}
 
static void hub_port_connect_change(struct usb_hub *hubstate, int port,
					u16 portstatus, u16 portchange)
{
	struct usb_device *hub = interface_to_usbdev(hubstate->intf);
	int status, i;
 
	dev_dbg (&hubstate->intf->dev,
		"port %d, status %04x, change %04x, %s\n",
		port + 1, portstatus, portchange, portspeed (portstatus));
 
	/* Clear the connection change status */
	clear_port_feature(hub, port + 1, USB_PORT_FEAT_C_CONNECTION);

	if (hubstate->has_indicators) {
		set_port_led(hub, hubstate, port + 1, HUB_LED_AUTO);
		hubstate->indicator[port] = INDICATOR_AUTO;
	}
 
	/* Disconnect any existing devices under this port */
	if (hub->children[port])
		usb_disconnect(&hub->children[port]);

	/* Return now if nothing is connected */
	if (!(portstatus & USB_PORT_STAT_CONNECTION)) {

		/* maybe switch power back on (e.g. root hub was reset) */
		if ((hubstate->descriptor->wHubCharacteristics
					& HUB_CHAR_LPSM) < 2
				&& !(portstatus & (1 << USB_PORT_FEAT_POWER)))
			set_port_feature(hub, port + 1, USB_PORT_FEAT_POWER);
 
		if (portstatus & USB_PORT_STAT_ENABLE)
  			goto done;
		return;
	}
  
	if (hub_port_debounce(hub, port)) {
		dev_err (&hubstate->intf->dev,
			"connect-debounce failed, port %d disabled\n",
			port+1);
		goto done;
	}

	for (i = 0; i < SET_CONFIG_TRIES; i++) {
		struct usb_device *dev;

		/* reallocate for each attempt, since references
		 * to the previous one can escape in various ways
		 */
		dev = usb_alloc_dev(hub, hub->bus, port);
		if (!dev) {
			dev_err (&hubstate->intf->dev,
				"couldn't allocate port %d usb_device\n", port+1);
			goto done;
		}
		dev->state = USB_STATE_POWERED;
	  
		/* hub can tell if it's lowspeed already:  D- pullup (not D+) */
		if (portstatus & USB_PORT_STAT_LOW_SPEED)
			dev->speed = USB_SPEED_LOW;
		else
			dev->speed = USB_SPEED_UNKNOWN;

		/* reset, set address, get descriptor, add to hub's children */
		down (&dev->serialize);
		status = hub_port_init(hub, dev, port);
		if (status == -EBUSY)
			break;
		if (status < 0)
			continue;

		/* consecutive bus-powered hubs aren't reliable; they can
		 * violate the voltage drop budget.  if the new child has
		 * a "powered" LED, users should notice we didn't enable it
		 * (without reading syslog), even without per-port LEDs
		 * on the parent.
		 */
		if (dev->descriptor.bDeviceClass == USB_CLASS_HUB
				&& hubstate->power_budget) {
			u16	devstat;

			status = usb_get_status(dev, USB_RECIP_DEVICE, 0,
					&devstat);
			if (status < 0) {
				dev_dbg(&dev->dev, "get status %d ?\n", status);
				continue;
			}
			cpu_to_le16s(&devstat);
			if ((devstat & (1 << USB_DEVICE_SELF_POWERED)) == 0) {
				dev_err(&dev->dev,
					"can't connect bus-powered hub "
					"to this port\n");
				if (hubstate->has_indicators) {
					hubstate->indicator[port] =
						INDICATOR_AMBER_BLINK;
					schedule_work (&hubstate->leds);
				}
				hub->children[port] = NULL;
				usb_put_dev(dev);
				hub_port_disable(hub, port);
				return;
			}
		}
 
		/* check for devices running slower than they could */
		if (dev->descriptor.bcdUSB >= 0x0200
				&& dev->speed == USB_SPEED_FULL
				&& highspeed_hubs != 0)
			check_highspeed (hubstate, dev, port);

		/* Run it through the hoops (find a driver, etc) */
		status = usb_new_device(dev);
		if (status != 0) {
			hub->children[port] = NULL;
			continue;
		}
		up (&dev->serialize);

		status = hub_power_remaining(hubstate, hub);
		if (status)
			dev_dbg(&hubstate->intf->dev,
				"%dmA power budget left\n",
				2 * status);

		return;
	}
 
done:
	hub_port_disable(hub, port);
}

static void hub_events(void)
{
	unsigned long flags;
	struct list_head *tmp;
	struct usb_device *dev;
	struct usb_hub *hub;
	u16 hubstatus;
	u16 hubchange;
	u16 portstatus;
	u16 portchange;
	int i, ret;

	/*
	 *  We restart the list every time to avoid a deadlock with
	 * deleting hubs downstream from this one. This should be
	 * safe since we delete the hub from the event list.
	 * Not the most efficient, but avoids deadlocks.
	 */
	while (1) {
		spin_lock_irqsave(&hub_event_lock, flags);

		if (list_empty(&hub_event_list))
			break;

		/* Grab the next entry from the beginning of the list */
		tmp = hub_event_list.next;

		hub = list_entry(tmp, struct usb_hub, event_list);
		dev = interface_to_usbdev(hub->intf);

		list_del_init(tmp);

		if (unlikely(down_trylock(&hub->khubd_sem)))
			BUG();	/* never blocks, we were on list */

		spin_unlock_irqrestore(&hub_event_lock, flags);

		if (hub->error) {
			dev_dbg (&hub->intf->dev, "resetting for error %d\n",
				hub->error);

			if (hub_reset(hub)) {
				dev_dbg (&hub->intf->dev,
					"can't reset; disconnecting\n");
				up(&hub->khubd_sem);
				hub_start_disconnect(dev);
				continue;
			}

			hub->nerrors = 0;
			hub->error = 0;
		}

		for (i = 0; i < hub->descriptor->bNbrPorts; i++) {
			ret = hub_port_status(dev, i, &portstatus, &portchange);
			if (ret < 0) {
				continue;
			}

			if (portchange & USB_PORT_STAT_C_CONNECTION) {
				hub_port_connect_change(hub, i, portstatus, portchange);
			} else if (portchange & USB_PORT_STAT_C_ENABLE) {
				dev_dbg (hubdev (dev),
					"port %d enable change, status %08x\n",
					i + 1, portstatus);
				clear_port_feature(dev,
					i + 1, USB_PORT_FEAT_C_ENABLE);

				/*
				 * EM interference sometimes causes badly
				 * shielded USB devices to be shutdown by
				 * the hub, this hack enables them again.
				 * Works at least with mouse driver. 
				 */
				if (!(portstatus & USB_PORT_STAT_ENABLE)
				    && (portstatus & USB_PORT_STAT_CONNECTION)
				    && (dev->children[i])) {
					dev_err (&hub->intf->dev,
					    "port %i "
					    "disabled by hub (EMI?), "
					    "re-enabling...",
						i + 1);
					hub_port_connect_change(hub,
						i, portstatus, portchange);
				}
			}

			if (portchange & USB_PORT_STAT_C_SUSPEND) {
				dev_dbg (&hub->intf->dev,
					"suspend change on port %d\n",
					i + 1);
				clear_port_feature(dev,
					i + 1,  USB_PORT_FEAT_C_SUSPEND);
			}
			
			if (portchange & USB_PORT_STAT_C_OVERCURRENT) {
				dev_err (&hub->intf->dev,
					"over-current change on port %d\n",
					i + 1);
				clear_port_feature(dev,
					i + 1, USB_PORT_FEAT_C_OVER_CURRENT);
				hub_power_on(hub);
			}

			if (portchange & USB_PORT_STAT_C_RESET) {
				dev_dbg (&hub->intf->dev,
					"reset change on port %d\n",
					i + 1);
				clear_port_feature(dev,
					i + 1, USB_PORT_FEAT_C_RESET);
			}
		} /* end for i */

		/* deal with hub status changes */
		if (hub_hub_status(hub, &hubstatus, &hubchange) < 0)
			dev_err (&hub->intf->dev, "get_hub_status failed\n");
		else {
			if (hubchange & HUB_CHANGE_LOCAL_POWER) {
				dev_dbg (&hub->intf->dev, "power change\n");
				clear_hub_feature(dev, C_HUB_LOCAL_POWER);
			}
			if (hubchange & HUB_CHANGE_OVERCURRENT) {
				dev_dbg (&hub->intf->dev, "overcurrent change\n");
				usb_uninterruptible_sleep_ms(500);	/* Cool down */
				clear_hub_feature(dev, C_HUB_OVER_CURRENT);
                        	hub_power_on(hub);
			}
		}
		up(&hub->khubd_sem);
        } /* end while (1) */

	spin_unlock_irqrestore(&hub_event_lock, flags);
}

static int hub_thread(void *__hub)
{
	/*
	 * This thread doesn't need any user-level access,
	 * so get rid of all our resources
	 */

	daemonize("khubd");
	allow_signal(SIGKILL);

	/* Send me a signal to get me die (for debugging) */
	do {
		hub_events();
		wait_event_interruptible(khubd_wait, !list_empty(&hub_event_list)); 
		if (current->flags & PF_FREEZE)
			refrigerator(PF_FREEZE);
	} while (!signal_pending(current));

	pr_debug ("%s: khubd exiting\n", usbcore_name);
	complete_and_exit(&khubd_exited, 0);
}

static struct usb_device_id hub_id_table [] = {
    { .match_flags = USB_DEVICE_ID_MATCH_DEV_CLASS,
      .bDeviceClass = USB_CLASS_HUB},
    { .match_flags = USB_DEVICE_ID_MATCH_INT_CLASS,
      .bInterfaceClass = USB_CLASS_HUB},
    { }						/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, hub_id_table);

static struct usb_driver hub_driver = {
	.owner =	THIS_MODULE,
	.name =		"hub",
	.probe =	hub_probe,
	.disconnect =	hub_disconnect,
	.ioctl =	hub_ioctl,
	.id_table =	hub_id_table,
};

/*
 * This should be a separate module.
 */
int usb_hub_init(void)
{
	pid_t pid;

	if (usb_register(&hub_driver) < 0) {
		printk(KERN_ERR "%s: can't register hub driver\n",
			usbcore_name);
		return -1;
	}

	pid = kernel_thread(hub_thread, NULL, CLONE_KERNEL);
	if (pid >= 0) {
		khubd_pid = pid;

		return 0;
	}

	/* Fall through if kernel_thread failed */
	usb_deregister(&hub_driver);
	printk(KERN_ERR "%s: can't start khubd\n", usbcore_name);

	return -1;
}

void usb_hub_cleanup(void)
{
	int ret;

	/* Kill the thread */
	ret = kill_proc(khubd_pid, SIGKILL, 1);

	wait_for_completion(&khubd_exited);

	/*
	 * Hub resources are freed for us by usb_deregister. It calls
	 * usb_driver_purge on every device which in turn calls that
	 * devices disconnect function if it is using this driver.
	 * The hub_disconnect function takes care of releasing the
	 * individual hub resources. -greg
	 */
	usb_deregister(&hub_driver);
} /* usb_hub_cleanup() */


static int config_descriptors_changed(struct usb_device *dev)
{
	unsigned			index;
	unsigned			len = 0;
	struct usb_config_descriptor	*buf;

	for (index = 0; index < dev->descriptor.bNumConfigurations; index++) {
		if (len < dev->config[index].desc.wTotalLength)
			len = dev->config[index].desc.wTotalLength;
	}
	buf = kmalloc (len, SLAB_KERNEL);
	if (buf == 0) {
		dev_err(&dev->dev, "no mem to re-read configs after reset\n");
		/* assume the worst */
		return 1;
	}
	for (index = 0; index < dev->descriptor.bNumConfigurations; index++) {
		int length;
		int old_length = dev->config[index].desc.wTotalLength;

		length = usb_get_descriptor(dev, USB_DT_CONFIG, index, buf,
				old_length);
		if (length < old_length) {
			dev_dbg(&dev->dev, "config index %d, error %d\n",
					index, length);
			break;
		}
		if (memcmp (buf, dev->rawdescriptors[index], old_length)
				!= 0) {
			dev_dbg(&dev->dev, "config index %d changed (#%d)\n",
				index, buf->bConfigurationValue);
/* FIXME enable this when we can re-enumerate after reset;
 * until then DFU-ish drivers need this and other workarounds
 */
//			break;
		}
	}
	kfree(buf);
	return index != dev->descriptor.bNumConfigurations;
}

/*
 * WARNING - don't reset any device unless drivers for all of its
 * interfaces are expecting that reset!  Maybe some driver->reset()
 * method should eventually help ensure sufficient cooperation.
 *
 * This is the same as usb_reset_device() except that the caller
 * already holds dev->serialize.  For example, it's safe to use
 * this from a driver probe() routine after downloading new firmware.
 */
int __usb_reset_device(struct usb_device *dev)
{
	struct usb_device *parent = dev->parent;
	struct usb_device_descriptor descriptor = dev->descriptor;
	int i, ret, port = -1;

	if (dev->maxchild) {
		/* this requires hub- or hcd-specific logic;
		 * see hub_reset() and OHCI hc_restart()
		 */
		dev_dbg(&dev->dev, "%s for hub!\n", __FUNCTION__);
		return -EINVAL;
	}

	for (i = 0; i < parent->maxchild; i++)
		if (parent->children[i] == dev) {
			port = i;
			break;
		}

	if (port < 0)
		return -ENOENT;

	ret = hub_port_init(parent, dev, port);
	if (ret < 0)
		goto re_enumerate;
 
	/* Device might have changed firmware (DFU or similar) */
	if (memcmp(&dev->descriptor, &descriptor, sizeof descriptor)
			|| config_descriptors_changed (dev)) {
		dev_info(&dev->dev, "device firmware changed\n");
		dev->descriptor = descriptor;	/* for disconnect() calls */
		goto re_enumerate;
  	}
  
	if (!dev->actconfig)
		return 0;

	ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
			USB_REQ_SET_CONFIGURATION, 0,
			dev->actconfig->desc.bConfigurationValue, 0,
			NULL, 0, HZ * USB_CTRL_SET_TIMEOUT);
	if (ret < 0) {
		dev_err(&dev->dev,
			"can't restore configuration #%d (error=%d)\n",
			dev->actconfig->desc.bConfigurationValue, ret);
		goto re_enumerate;
  	}
	dev->state = USB_STATE_CONFIGURED;

	for (i = 0; i < dev->actconfig->desc.bNumInterfaces; i++) {
		struct usb_interface *intf = dev->actconfig->interface[i];
		struct usb_interface_descriptor *desc;

		/* set_interface resets host side toggle and halt status even
		 * for altsetting zero.  the interface may have no driver.
		 */
		desc = &intf->cur_altsetting->desc;
		ret = usb_set_interface(dev, desc->bInterfaceNumber,
			desc->bAlternateSetting);
		if (ret < 0) {
			dev_err(&dev->dev, "failed to restore interface %d "
				"altsetting %d (error=%d)\n",
				desc->bInterfaceNumber,
				desc->bAlternateSetting,
				ret);
			goto re_enumerate;
		}
	}

	return 0;
 
re_enumerate:
	/* FIXME make some task re-enumerate; don't just mark unusable */
	dev->state = USB_STATE_NOTATTACHED;
	return -ENODEV;
}
EXPORT_SYMBOL(__usb_reset_device);

int usb_reset_device(struct usb_device *udev)
{
	int r;
	
	down(&udev->serialize);
	r = __usb_reset_device(udev);
	up(&udev->serialize);

	return r;
}
