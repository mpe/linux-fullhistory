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
#ifdef CONFIG_USB_DEBUG
	#define DEBUG
#else
	#undef DEBUG
#endif
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
#include <linux/usb.h>
#include <linux/usbdevice_fs.h>

#include <asm/semaphore.h>
#include <asm/uaccess.h>
#include <asm/byteorder.h>

#include "usb.h"
#include "hcd.h"
#include "hub.h"

/* Protect struct usb_device->state and ->children members
 * Note: Both are also protected by ->serialize, except that ->state can
 * change to USB_STATE_NOTATTACHED even when the semaphore isn't held. */
static DEFINE_SPINLOCK(device_state_lock);

/* khubd's worklist and its lock */
static DEFINE_SPINLOCK(hub_event_lock);
static LIST_HEAD(hub_event_list);	/* List of hubs needing servicing */

/* Wakes up khubd */
static DECLARE_WAIT_QUEUE_HEAD(khubd_wait);

static pid_t khubd_pid = 0;			/* PID of khubd */
static DECLARE_COMPLETION(khubd_exited);

/* cycle leds on hubs that aren't blinking for attention */
static int blinkenlights = 0;
module_param (blinkenlights, bool, S_IRUGO);
MODULE_PARM_DESC (blinkenlights, "true to cycle leds on hubs");

/*
 * As of 2.6.10 we introduce a new USB device initialization scheme which
 * closely resembles the way Windows works.  Hopefully it will be compatible
 * with a wider range of devices than the old scheme.  However some previously
 * working devices may start giving rise to "device not accepting address"
 * errors; if that happens the user can try the old scheme by adjusting the
 * following module parameters.
 *
 * For maximum flexibility there are two boolean parameters to control the
 * hub driver's behavior.  On the first initialization attempt, if the
 * "old_scheme_first" parameter is set then the old scheme will be used,
 * otherwise the new scheme is used.  If that fails and "use_both_schemes"
 * is set, then the driver will make another attempt, using the other scheme.
 */
static int old_scheme_first = 0;
module_param(old_scheme_first, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(old_scheme_first,
		 "start with the old device initialization scheme");

static int use_both_schemes = 0;
module_param(use_both_schemes, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(use_both_schemes,
		"try the other device initialization scheme if the "
		"first one fails");


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

/* Note that hdev or one of its children must be locked! */
static inline struct usb_hub *hdev_to_hub(struct usb_device *hdev)
{
	return usb_get_intfdata(hdev->actconfig->interface[0]);
}

/* USB 2.0 spec Section 11.24.4.5 */
static int get_hub_descriptor(struct usb_device *hdev, void *data, int size)
{
	int i, ret;

	for (i = 0; i < 3; i++) {
		ret = usb_control_msg(hdev, usb_rcvctrlpipe(hdev, 0),
			USB_REQ_GET_DESCRIPTOR, USB_DIR_IN | USB_RT_HUB,
			USB_DT_HUB << 8, 0, data, size,
			HZ * USB_CTRL_GET_TIMEOUT);
		if (ret >= (USB_DT_HUB_NONVAR_SIZE + 2))
			return ret;
	}
	return -EINVAL;
}

/*
 * USB 2.0 spec Section 11.24.2.1
 */
static int clear_hub_feature(struct usb_device *hdev, int feature)
{
	return usb_control_msg(hdev, usb_sndctrlpipe(hdev, 0),
		USB_REQ_CLEAR_FEATURE, USB_RT_HUB, feature, 0, NULL, 0, HZ);
}

/*
 * USB 2.0 spec Section 11.24.2.2
 */
static int clear_port_feature(struct usb_device *hdev, int port1, int feature)
{
	return usb_control_msg(hdev, usb_sndctrlpipe(hdev, 0),
		USB_REQ_CLEAR_FEATURE, USB_RT_PORT, feature, port1,
		NULL, 0, HZ);
}

/*
 * USB 2.0 spec Section 11.24.2.13
 */
static int set_port_feature(struct usb_device *hdev, int port1, int feature)
{
	return usb_control_msg(hdev, usb_sndctrlpipe(hdev, 0),
		USB_REQ_SET_FEATURE, USB_RT_PORT, feature, port1,
		NULL, 0, HZ);
}

/*
 * USB 2.0 spec Section 11.24.2.7.1.10 and table 11-7
 * for info about using port indicators
 */
static void set_port_led(
	struct usb_hub *hub,
	int port1,
	int selector
)
{
	int status = set_port_feature(hub->hdev, (selector << 8) | port1,
			USB_PORT_FEAT_INDICATOR);
	if (status < 0)
		dev_dbg (hub->intfdev,
			"port %d indicator %s status %d\n",
			port1,
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
	struct usb_device	*hdev = hub->hdev;
	unsigned		i;
	unsigned		changed = 0;
	int			cursor = -1;

	if (hdev->state != USB_STATE_CONFIGURED || hub->quiescing)
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
		set_port_led(hub, i + 1, selector);
		hub->indicator[i] = mode;
	}
	if (!changed && blinkenlights) {
		cursor++;
		cursor %= hub->descriptor->bNbrPorts;
		set_port_led(hub, cursor + 1, HUB_LED_GREEN);
		hub->indicator[cursor] = INDICATOR_CYCLE;
		changed++;
	}
	if (changed)
		schedule_delayed_work(&hub->leds, LED_CYCLE_PERIOD);
}

/* use a short timeout for hub/port status fetches */
#define	USB_STS_TIMEOUT		1
#define	USB_STS_RETRIES		5

/*
 * USB 2.0 spec Section 11.24.2.6
 */
static int get_hub_status(struct usb_device *hdev,
		struct usb_hub_status *data)
{
	int i, status = -ETIMEDOUT;

	for (i = 0; i < USB_STS_RETRIES && status == -ETIMEDOUT; i++) {
		status = usb_control_msg(hdev, usb_rcvctrlpipe(hdev, 0),
			USB_REQ_GET_STATUS, USB_DIR_IN | USB_RT_HUB, 0, 0,
			data, sizeof(*data), HZ * USB_STS_TIMEOUT);
	}
	return status;
}

/*
 * USB 2.0 spec Section 11.24.2.7
 */
static int get_port_status(struct usb_device *hdev, int port1,
		struct usb_port_status *data)
{
	int i, status = -ETIMEDOUT;

	for (i = 0; i < USB_STS_RETRIES && status == -ETIMEDOUT; i++) {
		status = usb_control_msg(hdev, usb_rcvctrlpipe(hdev, 0),
			USB_REQ_GET_STATUS, USB_DIR_IN | USB_RT_PORT, 0, port1,
			data, sizeof(*data), HZ * USB_STS_TIMEOUT);
	}
	return status;
}

static void kick_khubd(struct usb_hub *hub)
{
	unsigned long	flags;

	spin_lock_irqsave(&hub_event_lock, flags);
	if (list_empty(&hub->event_list)) {
		list_add_tail(&hub->event_list, &hub_event_list);
		wake_up(&khubd_wait);
	}
	spin_unlock_irqrestore(&hub_event_lock, flags);
}


/* completion function, fires on port status changes and various faults */
static void hub_irq(struct urb *urb, struct pt_regs *regs)
{
	struct usb_hub *hub = (struct usb_hub *)urb->context;
	int status;
	int i;
	unsigned long bits;

	switch (urb->status) {
	case -ENOENT:		/* synchronous unlink */
	case -ECONNRESET:	/* async unlink */
	case -ESHUTDOWN:	/* hardware going away */
		return;

	default:		/* presumably an error */
		/* Cause a hub reset after 10 consecutive errors */
		dev_dbg (hub->intfdev, "transfer --> %d\n", urb->status);
		if ((++hub->nerrors < 10) || hub->error)
			goto resubmit;
		hub->error = urb->status;
		/* FALL THROUGH */
	
	/* let khubd handle things */
	case 0:			/* we got data:  port status changed */
		bits = 0;
		for (i = 0; i < urb->actual_length; ++i)
			bits |= ((unsigned long) ((*hub->buffer)[i]))
					<< (i*8);
		hub->event_bits[0] = bits;
		break;
	}

	hub->nerrors = 0;

	/* Something happened, let khubd figure it out */
	kick_khubd(hub);

resubmit:
	if (hub->quiescing)
		return;

	if ((status = usb_submit_urb (hub->urb, GFP_ATOMIC)) != 0
			&& status != -ENODEV && status != -EPERM)
		dev_err (hub->intfdev, "resubmit --> %d\n", status);
}

/* USB 2.0 spec Section 11.24.2.3 */
static inline int
hub_clear_tt_buffer (struct usb_device *hdev, u16 devinfo, u16 tt)
{
	return usb_control_msg(hdev, usb_rcvctrlpipe(hdev, 0),
			       HUB_CLEAR_TT_BUFFER, USB_RT_PORT, devinfo,
			       tt, NULL, 0, HZ);
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
		struct usb_device	*hdev = hub->hdev;
		int			status;

		temp = hub->tt.clear_list.next;
		clear = list_entry (temp, struct usb_tt_clear, clear_list);
		list_del (&clear->clear_list);

		/* drop lock so HCD can concurrently report other TT errors */
		spin_unlock_irqrestore (&hub->tt.lock, flags);
		status = hub_clear_tt_buffer (hdev, clear->devinfo, clear->tt);
		spin_lock_irqsave (&hub->tt.lock, flags);

		if (status)
			dev_err (&hdev->dev,
				"clear tt %d (%04x) error %d\n",
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
void usb_hub_tt_clear_buffer (struct usb_device *udev, int pipe)
{
	struct usb_tt		*tt = udev->tt;
	unsigned long		flags;
	struct usb_tt_clear	*clear;

	/* we've got to cope with an arbitrary number of pending TT clears,
	 * since each TT has "at least two" buffers that can need it (and
	 * there can be many TTs per hub).  even if they're uncommon.
	 */
	if ((clear = kmalloc (sizeof *clear, SLAB_ATOMIC)) == NULL) {
		dev_err (&udev->dev, "can't save CLEAR_TT_BUFFER state\n");
		/* FIXME recover somehow ... RESET_TT? */
		return;
	}

	/* info that CLEAR_TT_BUFFER needs */
	clear->tt = tt->multi ? udev->ttport : 1;
	clear->devinfo = usb_pipeendpoint (pipe);
	clear->devinfo |= udev->devnum << 4;
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
	int port1;

	/* if hub supports power switching, enable power on each port */
	if ((hub->descriptor->wHubCharacteristics & HUB_CHAR_LPSM) < 2) {
		dev_dbg(hub->intfdev, "enabling power on all ports\n");
		for (port1 = 1; port1 <= hub->descriptor->bNbrPorts; port1++)
			set_port_feature(hub->hdev, port1,
					USB_PORT_FEAT_POWER);
	}

	/* Wait for power to be enabled */
	msleep(hub->descriptor->bPwrOn2PwrGood * 2);
}

static void hub_quiesce(struct usb_hub *hub)
{
	/* stop khubd and related activity */
	hub->quiescing = 1;
	usb_kill_urb(hub->urb);
	if (hub->has_indicators)
		cancel_delayed_work(&hub->leds);
	if (hub->has_indicators || hub->tt.hub)
		flush_scheduled_work();
}

static void hub_activate(struct usb_hub *hub)
{
	int	status;

	hub->quiescing = 0;
	status = usb_submit_urb(hub->urb, GFP_NOIO);
	if (status < 0)
		dev_err(hub->intfdev, "activate --> %d\n", status);
	if (hub->has_indicators && blinkenlights)
		schedule_delayed_work(&hub->leds, LED_CYCLE_PERIOD);

	/* scan all ports ASAP */
	hub->event_bits[0] = (1UL << (hub->descriptor->bNbrPorts + 1)) - 1;
	kick_khubd(hub);
}

static int hub_hub_status(struct usb_hub *hub,
		u16 *status, u16 *change)
{
	int ret;

	ret = get_hub_status(hub->hdev, &hub->status->hub);
	if (ret < 0)
		dev_err (hub->intfdev,
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
	struct usb_device *hdev = hub->hdev;
	struct device *hub_dev = hub->intfdev;
	u16 hubstatus, hubchange;
	unsigned int pipe;
	int maxp, ret;
	char *message;

	hub->buffer = usb_buffer_alloc(hdev, sizeof(*hub->buffer), GFP_KERNEL,
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
	ret = get_hub_descriptor(hdev, hub->descriptor,
			sizeof(*hub->descriptor));
	if (ret < 0) {
		message = "can't read hub descriptor";
		goto fail;
	} else if (hub->descriptor->bNbrPorts > USB_MAXCHILDREN) {
		message = "hub has too many ports!";
		ret = -ENODEV;
		goto fail;
	}

	hdev->maxchild = hub->descriptor->bNbrPorts;
	dev_info (hub_dev, "%d port%s detected\n", hdev->maxchild,
		(hdev->maxchild == 1) ? "" : "s");

	le16_to_cpus(&hub->descriptor->wHubCharacteristics);

	if (hub->descriptor->wHubCharacteristics & HUB_CHAR_COMPOUND) {
		int	i;
		char	portstr [USB_MAXCHILDREN + 1];

		for (i = 0; i < hdev->maxchild; i++)
			portstr[i] = hub->descriptor->DeviceRemovable
				    [((i + 1) / 8)] & (1 << ((i + 1) % 8))
				? 'F' : 'R';
		portstr[hdev->maxchild] = 0;
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
	switch (hdev->descriptor.bDeviceProtocol) {
		case 0:
			break;
		case 1:
			dev_dbg(hub_dev, "Single TT\n");
			hub->tt.hub = hdev;
			break;
		case 2:
			ret = usb_set_interface(hdev, 0, 1);
			if (ret == 0) {
				dev_dbg(hub_dev, "TT per port\n");
				hub->tt.multi = 1;
			} else
				dev_err(hub_dev, "Using single TT (err %d)\n",
					ret);
			hub->tt.hub = hdev;
			break;
		default:
			dev_dbg(hub_dev, "Unrecognized hub protocol %d\n",
				hdev->descriptor.bDeviceProtocol);
			break;
	}

	switch (hub->descriptor->wHubCharacteristics & HUB_CHAR_TTTT) {
		case 0x00:
			if (hdev->descriptor.bDeviceProtocol != 0)
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
	ret = usb_get_status(hdev, USB_RECIP_DEVICE, 0, &hubstatus);
	if (ret < 0) {
		message = "can't get hub status";
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
	if (hdev->actconfig->desc.bmAttributes & USB_CONFIG_ATT_SELFPOWER)
		dev_dbg(hub_dev, "local power source is %s\n",
			(hubstatus & HUB_STATUS_LOCAL_POWER)
			? "lost (inactive)" : "good");

	if ((hub->descriptor->wHubCharacteristics & HUB_CHAR_OCPM) == 0)
		dev_dbg(hub_dev, "%sover-current condition exists\n",
			(hubstatus & HUB_STATUS_OVERCURRENT) ? "" : "no ");

	/* set up the interrupt endpoint */
	pipe = usb_rcvintpipe(hdev, endpoint->bEndpointAddress);
	maxp = usb_maxpacket(hdev, pipe, usb_pipeout(pipe));

	if (maxp > sizeof(*hub->buffer))
		maxp = sizeof(*hub->buffer);

	hub->urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!hub->urb) {
		message = "couldn't allocate interrupt urb";
		ret = -ENOMEM;
		goto fail;
	}

	usb_fill_int_urb(hub->urb, hdev, pipe, *hub->buffer, maxp, hub_irq,
		hub, endpoint->bInterval);
	hub->urb->transfer_dma = hub->buffer_dma;
	hub->urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	/* maybe cycle the hub leds */
	if (hub->has_indicators && blinkenlights)
		hub->indicator [0] = INDICATOR_CYCLE;

	hub_power_on(hub);
	hub->change_bits[0] = (1UL << (hub->descriptor->bNbrPorts + 1)) - 2;
	hub_activate(hub);
	return 0;

fail:
	dev_err (hub_dev, "config failed, %s (err %d)\n",
			message, ret);
	/* hub_disconnect() frees urb and descriptor */
	return ret;
}

static unsigned highspeed_hubs;

static void hub_disconnect(struct usb_interface *intf)
{
	struct usb_hub *hub = usb_get_intfdata (intf);
	struct usb_device *hdev;

	if (!hub)
		return;
	hdev = hub->hdev;

	if (hdev->speed == USB_SPEED_HIGH)
		highspeed_hubs--;

	usb_set_intfdata (intf, NULL);

	hub_quiesce(hub);
	usb_free_urb(hub->urb);
	hub->urb = NULL;

	spin_lock_irq(&hub_event_lock);
	list_del_init(&hub->event_list);
	spin_unlock_irq(&hub_event_lock);

	if (hub->descriptor) {
		kfree(hub->descriptor);
		hub->descriptor = NULL;
	}

	if (hub->status) {
		kfree(hub->status);
		hub->status = NULL;
	}

	if (hub->buffer) {
		usb_buffer_free(hdev, sizeof(*hub->buffer), hub->buffer,
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
	struct usb_device *hdev;
	struct usb_hub *hub;

	desc = intf->cur_altsetting;
	hdev = interface_to_usbdev(intf);

	/* Some hubs have a subclass of 1, which AFAICT according to the */
	/*  specs is not defined, but it works */
	if ((desc->desc.bInterfaceSubClass != 0) &&
	    (desc->desc.bInterfaceSubClass != 1)) {
descriptor_error:
		dev_err (&intf->dev, "bad descriptor, ignoring hub\n");
		return -EIO;
	}

	/* Multiple endpoints? What kind of mutant ninja-hub is this? */
	if (desc->desc.bNumEndpoints != 1)
		goto descriptor_error;

	endpoint = &desc->endpoint[0].desc;

	/* Output endpoint? Curiouser and curiouser.. */
	if (!(endpoint->bEndpointAddress & USB_DIR_IN))
		goto descriptor_error;

	/* If it's not an interrupt endpoint, we'd better punt! */
	if ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
			!= USB_ENDPOINT_XFER_INT)
		goto descriptor_error;

	/* We found a hub */
	dev_info (&intf->dev, "USB hub found\n");

	hub = kmalloc(sizeof(*hub), GFP_KERNEL);
	if (!hub) {
		dev_dbg (&intf->dev, "couldn't kmalloc hub struct\n");
		return -ENOMEM;
	}

	memset(hub, 0, sizeof(*hub));

	INIT_LIST_HEAD(&hub->event_list);
	hub->intfdev = &intf->dev;
	hub->hdev = hdev;
	INIT_WORK(&hub->leds, led_work, hub);

	usb_set_intfdata (intf, hub);

	if (hdev->speed == USB_SPEED_HIGH)
		highspeed_hubs++;

	if (hub_configure(hub, endpoint) >= 0)
		return 0;

	hub_disconnect (intf);
	return -ENODEV;
}

static int
hub_ioctl(struct usb_interface *intf, unsigned int code, void *user_data)
{
	struct usb_device *hdev = interface_to_usbdev (intf);

	/* assert ifno == 0 (part of hub spec) */
	switch (code) {
	case USBDEVFS_HUB_PORTINFO: {
		struct usbdevfs_hub_portinfo *info = user_data;
		int i;

		spin_lock_irq(&device_state_lock);
		if (hdev->devnum <= 0)
			info->nports = 0;
		else {
			info->nports = hdev->maxchild;
			for (i = 0; i < info->nports; i++) {
				if (hdev->children[i] == NULL)
					info->port[i] = 0;
				else
					info->port[i] =
						hdev->children[i]->devnum;
			}
		}
		spin_unlock_irq(&device_state_lock);

		return info->nports + 1;
		}

	default:
		return -ENOSYS;
	}
}

/* caller has locked the hub device */
static void hub_pre_reset(struct usb_hub *hub)
{
	struct usb_device *hdev = hub->hdev;
	int i;

	for (i = 0; i < hdev->maxchild; ++i) {
		if (hdev->children[i])
			usb_disconnect(&hdev->children[i]);
	}
	hub_quiesce(hub);
}

/* caller has locked the hub device */
static void hub_post_reset(struct usb_hub *hub)
{
	hub_activate(hub);
	hub_power_on(hub);
}


/* grab device/port lock, returning index of that port (zero based).
 * protects the upstream link used by this device from concurrent
 * tree operations like suspend, resume, reset, and disconnect, which
 * apply to everything downstream of a given port.
 */
static int locktree(struct usb_device *udev)
{
	int			t;
	struct usb_device	*hdev;

	if (!udev)
		return -ENODEV;

	/* root hub is always the first lock in the series */
	hdev = udev->parent;
	if (!hdev) {
		usb_lock_device(udev);
		return 0;
	}

	/* on the path from root to us, lock everything from
	 * top down, dropping parent locks when not needed
	 */
	t = locktree(hdev);
	if (t < 0)
		return t;
	for (t = 0; t < hdev->maxchild; t++) {
		if (hdev->children[t] == udev) {
			/* everything is fail-fast once disconnect
			 * processing starts
			 */
			if (udev->state == USB_STATE_NOTATTACHED)
				break;

			/* when everyone grabs locks top->bottom,
			 * non-overlapping work may be concurrent
			 */
			down(&udev->serialize);
			up(&hdev->serialize);
			return t + 1;
		}
	}
	usb_unlock_device(hdev);
	return -ENODEV;
}

static void recursively_mark_NOTATTACHED(struct usb_device *udev)
{
	int i;

	for (i = 0; i < udev->maxchild; ++i) {
		if (udev->children[i])
			recursively_mark_NOTATTACHED(udev->children[i]);
	}
	udev->state = USB_STATE_NOTATTACHED;
}

/**
 * usb_set_device_state - change a device's current state (usbcore, hcds)
 * @udev: pointer to device whose state should be changed
 * @new_state: new state value to be stored
 *
 * udev->state is _not_ fully protected by the device lock.  Although
 * most transitions are made only while holding the lock, the state can
 * can change to USB_STATE_NOTATTACHED at almost any time.  This
 * is so that devices can be marked as disconnected as soon as possible,
 * without having to wait for any semaphores to be released.  As a result,
 * all changes to any device's state must be protected by the
 * device_state_lock spinlock.
 *
 * Once a device has been added to the device tree, all changes to its state
 * should be made using this routine.  The state should _not_ be set directly.
 *
 * If udev->state is already USB_STATE_NOTATTACHED then no change is made.
 * Otherwise udev->state is set to new_state, and if new_state is
 * USB_STATE_NOTATTACHED then all of udev's descendants' states are also set
 * to USB_STATE_NOTATTACHED.
 */
void usb_set_device_state(struct usb_device *udev,
		enum usb_device_state new_state)
{
	unsigned long flags;

	spin_lock_irqsave(&device_state_lock, flags);
	if (udev->state == USB_STATE_NOTATTACHED)
		;	/* do nothing */
	else if (new_state != USB_STATE_NOTATTACHED)
		udev->state = new_state;
	else
		recursively_mark_NOTATTACHED(udev);
	spin_unlock_irqrestore(&device_state_lock, flags);
}
EXPORT_SYMBOL(usb_set_device_state);


static void choose_address(struct usb_device *udev)
{
	int		devnum;
	struct usb_bus	*bus = udev->bus;

	/* If khubd ever becomes multithreaded, this will need a lock */

	/* Try to allocate the next devnum beginning at bus->devnum_next. */
	devnum = find_next_zero_bit(bus->devmap.devicemap, 128,
			bus->devnum_next);
	if (devnum >= 128)
		devnum = find_next_zero_bit(bus->devmap.devicemap, 128, 1);

	bus->devnum_next = ( devnum >= 127 ? 1 : devnum + 1);

	if (devnum < 128) {
		set_bit(devnum, bus->devmap.devicemap);
		udev->devnum = devnum;
	}
}

static void release_address(struct usb_device *udev)
{
	if (udev->devnum > 0) {
		clear_bit(udev->devnum, udev->bus->devmap.devicemap);
		udev->devnum = -1;
	}
}

/**
 * usb_disconnect - disconnect a device (usbcore-internal)
 * @pdev: pointer to device being disconnected
 * Context: !in_interrupt ()
 *
 * Something got disconnected. Get rid of it and all of its children.
 *
 * If *pdev is a normal device then the parent hub must already be locked.
 * If *pdev is a root hub then this routine will acquire the
 * usb_bus_list_lock on behalf of the caller.
 *
 * Only hub drivers (including virtual root hub drivers for host
 * controllers) should ever call this.
 *
 * This call is synchronous, and may not be used in an interrupt context.
 */
void usb_disconnect(struct usb_device **pdev)
{
	struct usb_device	*udev = *pdev;
	int			i;

	if (!udev) {
		pr_debug ("%s nodev\n", __FUNCTION__);
		return;
	}

	/* mark the device as inactive, so any further urb submissions for
	 * this device (and any of its children) will fail immediately.
	 * this quiesces everyting except pending urbs.
	 */
	usb_set_device_state(udev, USB_STATE_NOTATTACHED);

	/* lock the bus list on behalf of HCDs unregistering their root hubs */
	if (!udev->parent) {
		down(&usb_bus_list_lock);
		usb_lock_device(udev);
	} else
		down(&udev->serialize);

	dev_info (&udev->dev, "USB disconnect, address %d\n", udev->devnum);

	/* Free up all the children before we remove this device */
	for (i = 0; i < USB_MAXCHILDREN; i++) {
		if (udev->children[i])
			usb_disconnect(&udev->children[i]);
	}

	/* deallocate hcd/hardware state ... nuking all pending urbs and
	 * cleaning up all state associated with the current configuration
	 * so that the hardware is now fully quiesced.
	 */
	usb_disable_device(udev, 0);

	/* Free the device number, remove the /proc/bus/usb entry and
	 * the sysfs attributes, and delete the parent's children[]
	 * (or root_hub) pointer.
	 */
	dev_dbg (&udev->dev, "unregistering device\n");
	release_address(udev);
	usbfs_remove_device(udev);
	usb_remove_sysfs_dev_files(udev);

	/* Avoid races with recursively_mark_NOTATTACHED() */
	spin_lock_irq(&device_state_lock);
	*pdev = NULL;
	spin_unlock_irq(&device_state_lock);

	if (!udev->parent) {
		usb_unlock_device(udev);
		up(&usb_bus_list_lock);
	} else
		up(&udev->serialize);

	device_unregister(&udev->dev);
}

static int choose_configuration(struct usb_device *udev)
{
	int c, i;

	/* NOTE: this should interact with hub power budgeting */

	c = udev->config[0].desc.bConfigurationValue;
	if (udev->descriptor.bNumConfigurations != 1) {
		for (i = 0; i < udev->descriptor.bNumConfigurations; i++) {
			struct usb_interface_descriptor	*desc;

			/* heuristic:  Linux is more likely to have class
			 * drivers, so avoid vendor-specific interfaces.
			 */
			desc = &udev->config[i].intf_cache[0]
					->altsetting->desc;
			if (desc->bInterfaceClass == USB_CLASS_VENDOR_SPEC)
				continue;
			/* COMM/2/all is CDC ACM, except 0xff is MSFT RNDIS.
			 * MSFT needs this to be the first config; never use
			 * it as the default unless Linux has host-side RNDIS.
			 * A second config would ideally be CDC-Ethernet, but
			 * may instead be the "vendor specific" CDC subset
			 * long used by ARM Linux for sa1100 or pxa255.
			 */
			if (desc->bInterfaceClass == USB_CLASS_COMM
					&& desc->bInterfaceSubClass == 2
					&& desc->bInterfaceProtocol == 0xff) {
				c = udev->config[1].desc.bConfigurationValue;
				continue;
			}
			c = udev->config[i].desc.bConfigurationValue;
			break;
		}
		dev_info(&udev->dev,
			"configuration #%d chosen from %d choices\n",
			c, udev->descriptor.bNumConfigurations);
	}
	return c;
}

#ifdef DEBUG
static void show_string(struct usb_device *udev, char *id, int index)
{
	char *buf;

	if (!index)
		return;
	if (!(buf = kmalloc(256, GFP_KERNEL)))
		return;
	if (usb_string(udev, index, buf, 256) > 0)
		dev_printk(KERN_INFO, &udev->dev, "%s: %s\n", id, buf);
	kfree(buf);
}

#else
static inline void show_string(struct usb_device *udev, char *id, int index)
{}
#endif

#ifdef	CONFIG_USB_OTG
#include "otg_whitelist.h"
#endif

/**
 * usb_new_device - perform initial device setup (usbcore-internal)
 * @udev: newly addressed device (in ADDRESS state)
 *
 * This is called with devices which have been enumerated, but not yet
 * configured.  The device descriptor is available, but not descriptors
 * for any device configuration.  The caller must have locked udev and
 * either the parent hub (if udev is a normal device) or else the
 * usb_bus_list_lock (if udev is a root hub).  The parent's pointer to
 * udev has already been installed, but udev is not yet visible through
 * sysfs or other filesystem code.
 *
 * Returns 0 for success (device is configured and listed, with its
 * interfaces, in sysfs); else a negative errno value.
 *
 * This call is synchronous, and may not be used in an interrupt context.
 *
 * Only the hub driver should ever call this; root hub registration
 * uses it indirectly.
 */
int usb_new_device(struct usb_device *udev)
{
	int err;
	int c;

	err = usb_get_configuration(udev);
	if (err < 0) {
		dev_err(&udev->dev, "can't read configurations, error %d\n",
			err);
		goto fail;
	}

	/* Tell the world! */
	dev_dbg(&udev->dev, "new device strings: Mfr=%d, Product=%d, "
			"SerialNumber=%d\n",
			udev->descriptor.iManufacturer,
			udev->descriptor.iProduct,
			udev->descriptor.iSerialNumber);

	if (udev->descriptor.iProduct)
		show_string(udev, "Product",
				udev->descriptor.iProduct);
	if (udev->descriptor.iManufacturer)
		show_string(udev, "Manufacturer",
				udev->descriptor.iManufacturer);
	if (udev->descriptor.iSerialNumber)
		show_string(udev, "SerialNumber",
				udev->descriptor.iSerialNumber);

#ifdef	CONFIG_USB_OTG
	/*
	 * OTG-aware devices on OTG-capable root hubs may be able to use SRP,
	 * to wake us after we've powered off VBUS; and HNP, switching roles
	 * "host" to "peripheral".  The OTG descriptor helps figure this out.
	 */
	if (!udev->bus->is_b_host
			&& udev->config
			&& udev->parent == udev->bus->root_hub) {
		struct usb_otg_descriptor	*desc = 0;
		struct usb_bus			*bus = udev->bus;

		/* descriptor may appear anywhere in config */
		if (__usb_get_extra_descriptor (udev->rawdescriptors[0],
					le16_to_cpu(udev->config[0].desc.wTotalLength),
					USB_DT_OTG, (void **) &desc) == 0) {
			if (desc->bmAttributes & USB_OTG_HNP) {
				unsigned		port1;
				struct usb_device	*root = udev->parent;
				
				for (port1 = 1; port1 <= root->maxchild;
						port1++) {
					if (root->children[port1-1] == udev)
						break;
				}

				dev_info(&udev->dev,
					"Dual-Role OTG device on %sHNP port\n",
					(port1 == bus->otg_port)
						? "" : "non-");

				/* enable HNP before suspend, it's simpler */
				if (port1 == bus->otg_port)
					bus->b_hnp_enable = 1;
				err = usb_control_msg(udev,
					usb_sndctrlpipe(udev, 0),
					USB_REQ_SET_FEATURE, 0,
					bus->b_hnp_enable
						? USB_DEVICE_B_HNP_ENABLE
						: USB_DEVICE_A_ALT_HNP_SUPPORT,
					0, NULL, 0, HZ * USB_CTRL_SET_TIMEOUT);
				if (err < 0) {
					/* OTG MESSAGE: report errors here,
					 * customize to match your product.
					 */
					dev_info(&udev->dev,
						"can't set HNP mode; %d\n",
						err);
					bus->b_hnp_enable = 0;
				}
			}
		}
	}

	if (!is_targeted(udev)) {

		/* Maybe it can talk to us, though we can't talk to it.
		 * (Includes HNP test device.)
		 */
		if (udev->bus->b_hnp_enable || udev->bus->is_b_host) {
			static int __usb_suspend_device (struct usb_device *,
						int port1, u32 state);
			err = __usb_suspend_device(udev,
					udev->bus->otg_port,
					PM_SUSPEND_MEM);
			if (err < 0)
				dev_dbg(&udev->dev, "HNP fail, %d\n", err);
		}
		err = -ENODEV;
		goto fail;
	}
#endif

	/* put device-specific files into sysfs */
	err = device_add (&udev->dev);
	if (err) {
		dev_err(&udev->dev, "can't device_add, error %d\n", err);
		goto fail;
	}
	usb_create_sysfs_dev_files (udev);

	/* choose and set the configuration. that registers the interfaces
	 * with the driver core, and lets usb device drivers bind to them.
	 */
	c = choose_configuration(udev);
	if (c < 0)
		dev_warn(&udev->dev,
				"can't choose an initial configuration\n");
	else {
		err = usb_set_configuration(udev, c);
		if (err) {
			dev_err(&udev->dev, "can't set config #%d, error %d\n",
					c, err);
			usb_remove_sysfs_dev_files(udev);
			device_del(&udev->dev);
			goto fail;
		}
	}

	/* USB device state == configured ... usable */

	/* add a /proc/bus/usb entry */
	usbfs_add_device(udev);
	return 0;

fail:
	usb_set_device_state(udev, USB_STATE_NOTATTACHED);
	return err;
}


static int hub_port_status(struct usb_hub *hub, int port1,
			       u16 *status, u16 *change)
{
	int ret;

	ret = get_port_status(hub->hdev, port1, &hub->status->port);
	if (ret < 0)
		dev_err (hub->intfdev,
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
#define SET_CONFIG_TRIES	(2 * (use_both_schemes + 1))
#define USE_NEW_SCHEME(i)	((i) / 2 == old_scheme_first)

#define HUB_ROOT_RESET_TIME	50	/* times are in msec */
#define HUB_SHORT_RESET_TIME	10
#define HUB_LONG_RESET_TIME	200
#define HUB_RESET_TIMEOUT	500

static int hub_port_wait_reset(struct usb_hub *hub, int port1,
				struct usb_device *udev, unsigned int delay)
{
	int delay_time, ret;
	u16 portstatus;
	u16 portchange;

	for (delay_time = 0;
			delay_time < HUB_RESET_TIMEOUT;
			delay_time += delay) {
		/* wait to give the device a chance to reset */
		msleep(delay);

		/* read and decode port status */
		ret = hub_port_status(hub, port1, &portstatus, &portchange);
		if (ret < 0)
			return ret;

		/* Device went away? */
		if (!(portstatus & USB_PORT_STAT_CONNECTION))
			return -ENOTCONN;

		/* bomb out completely if something weird happened */
		if ((portchange & USB_PORT_STAT_C_CONNECTION))
			return -EINVAL;

		/* if we`ve finished resetting, then break out of the loop */
		if (!(portstatus & USB_PORT_STAT_RESET) &&
		    (portstatus & USB_PORT_STAT_ENABLE)) {
			if (portstatus & USB_PORT_STAT_HIGH_SPEED)
				udev->speed = USB_SPEED_HIGH;
			else if (portstatus & USB_PORT_STAT_LOW_SPEED)
				udev->speed = USB_SPEED_LOW;
			else
				udev->speed = USB_SPEED_FULL;
			return 0;
		}

		/* switch to the long delay after two short delay failures */
		if (delay_time >= 2 * HUB_SHORT_RESET_TIME)
			delay = HUB_LONG_RESET_TIME;

		dev_dbg (hub->intfdev,
			"port %d not reset yet, waiting %dms\n",
			port1, delay);
	}

	return -EBUSY;
}

static int hub_port_reset(struct usb_hub *hub, int port1,
				struct usb_device *udev, unsigned int delay)
{
	int i, status;

	/* Reset the port */
	for (i = 0; i < PORT_RESET_TRIES; i++) {
		status = set_port_feature(hub->hdev,
				port1, USB_PORT_FEAT_RESET);
		if (status)
			dev_err(hub->intfdev,
					"cannot reset port %d (err = %d)\n",
					port1, status);
		else
			status = hub_port_wait_reset(hub, port1, udev, delay);

		/* return on disconnect or reset */
		switch (status) {
		case 0:
			/* TRSTRCY = 10 ms */
			msleep(10);
			/* FALL THROUGH */
		case -ENOTCONN:
		case -ENODEV:
			clear_port_feature(hub->hdev,
				port1, USB_PORT_FEAT_C_RESET);
			/* FIXME need disconnect() for NOTATTACHED device */
			usb_set_device_state(udev, status
					? USB_STATE_NOTATTACHED
					: USB_STATE_DEFAULT);
			return status;
		}

		dev_dbg (hub->intfdev,
			"port %d not enabled, trying reset again...\n",
			port1);
		delay = HUB_LONG_RESET_TIME;
	}

	dev_err (hub->intfdev,
		"Cannot enable port %i.  Maybe the USB cable is bad?\n",
		port1);

	return status;
}

static int hub_port_disable(struct usb_hub *hub, int port1, int set_state)
{
	struct usb_device *hdev = hub->hdev;
	int ret;

	if (hdev->children[port1-1] && set_state) {
		usb_set_device_state(hdev->children[port1-1],
				USB_STATE_NOTATTACHED);
	}
	ret = clear_port_feature(hdev, port1, USB_PORT_FEAT_ENABLE);
	if (ret)
		dev_err(hub->intfdev, "cannot disable port %d (err = %d)\n",
			port1, ret);

	return ret;
}

/*
 * Disable a port and mark a logical connnect-change event, so that some
 * time later khubd will disconnect() any existing usb_device on the port
 * and will re-enumerate if there actually is a device attached.
 */
static void hub_port_logical_disconnect(struct usb_hub *hub, int port1)
{
	dev_dbg(hub->intfdev, "logical disconnect on port %d\n", port1);
	hub_port_disable(hub, port1, 1);

	/* FIXME let caller ask to power down the port:
	 *  - some devices won't enumerate without a VBUS power cycle
	 *  - SRP saves power that way
	 *  - usb_suspend_device(dev,PM_SUSPEND_DISK)
	 * That's easy if this hub can switch power per-port, and
	 * khubd reactivates the port later (timer, SRP, etc).
	 * Powerdown must be optional, because of reset/DFU.
	 */

	set_bit(port1, hub->change_bits);
 	kick_khubd(hub);
}


#ifdef	CONFIG_USB_SUSPEND

/*
 * Selective port suspend reduces power; most suspended devices draw
 * less than 500 uA.  It's also used in OTG, along with remote wakeup.
 * All devices below the suspended port are also suspended.
 *
 * Devices leave suspend state when the host wakes them up.  Some devices
 * also support "remote wakeup", where the device can activate the USB
 * tree above them to deliver data, such as a keypress or packet.  In
 * some cases, this wakes the USB host.
 */
static int hub_port_suspend(struct usb_hub *hub, int port1,
		struct usb_device *udev)
{
	int	status;

	// dev_dbg(hub->intfdev, "suspend port %d\n", port1);

	/* enable remote wakeup when appropriate; this lets the device
	 * wake up the upstream hub (including maybe the root hub).
	 *
	 * NOTE:  OTG devices may issue remote wakeup (or SRP) even when
	 * we don't explicitly enable it here.
	 */
	if (udev->actconfig
			// && FIXME (remote wakeup enabled on this bus)
			// ... currently assuming it's always appropriate
			&& (udev->actconfig->desc.bmAttributes
				& USB_CONFIG_ATT_WAKEUP) != 0) {
		status = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
				USB_REQ_SET_FEATURE, USB_RECIP_DEVICE,
				USB_DEVICE_REMOTE_WAKEUP, 0,
				NULL, 0,
				USB_CTRL_SET_TIMEOUT);
		if (status)
			dev_dbg(&udev->dev,
				"won't remote wakeup, status %d\n",
				status);
	}

	/* see 7.1.7.6 */
	status = set_port_feature(hub->hdev, port1, USB_PORT_FEAT_SUSPEND);
	if (status) {
		dev_dbg(hub->intfdev,
			"can't suspend port %d, status %d\n",
			port1, status);
		/* paranoia:  "should not happen" */
		(void) usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
				USB_REQ_CLEAR_FEATURE, USB_RECIP_DEVICE,
				USB_DEVICE_REMOTE_WAKEUP, 0,
				NULL, 0,
				USB_CTRL_SET_TIMEOUT);
	} else {
		/* device has up to 10 msec to fully suspend */
		dev_dbg(&udev->dev, "usb suspend\n");
		usb_set_device_state(udev, USB_STATE_SUSPENDED);
		msleep(10);
	}
	return status;
}

/*
 * Devices on USB hub ports have only one "suspend" state, corresponding
 * to ACPI D2 (PM_SUSPEND_MEM), "may cause the device to lose some context".
 * State transitions include:
 *
 *   - suspend, resume ... when the VBUS power link stays live
 *   - suspend, disconnect ... VBUS lost
 *
 * Once VBUS drop breaks the circuit, the port it's using has to go through
 * normal re-enumeration procedures, starting with enabling VBUS power.
 * Other than re-initializing the hub (plug/unplug, except for root hubs),
 * Linux (2.6) currently has NO mechanisms to initiate that:  no khubd
 * timer, no SRP, no requests through sysfs.
 */
int __usb_suspend_device (struct usb_device *udev, int port1, u32 state)
{
	int	status;

	/* caller owns the udev device lock */
	if (port1 < 0)
		return port1;

	if (udev->state == USB_STATE_SUSPENDED
			|| udev->state == USB_STATE_NOTATTACHED) {
		return 0;
	}

	/* suspend interface drivers; if this is a hub, it
	 * suspends the child devices
	 */
	if (udev->actconfig) {
		int	i;

		for (i = 0; i < udev->actconfig->desc.bNumInterfaces; i++) {
			struct usb_interface	*intf;
			struct usb_driver	*driver;

			intf = udev->actconfig->interface[i];
			if (state <= intf->dev.power.power_state)
				continue;
			if (!intf->dev.driver)
				continue;
			driver = to_usb_driver(intf->dev.driver);

			if (driver->suspend) {
				status = driver->suspend(intf, state);
				if (intf->dev.power.power_state != state
						|| status)
					dev_err(&intf->dev,
						"suspend %d fail, code %d\n",
						state, status);
			}

			/* only drivers with suspend() can ever resume();
			 * and after power loss, even they won't.
			 * bus_rescan_devices() can rebind drivers later.
			 *
			 * FIXME the PM core self-deadlocks when unbinding
			 * drivers during suspend/resume ... everything grabs
			 * dpm_sem (not a spinlock, ugh).  we want to unbind,
			 * since we know every driver's probe/disconnect works
			 * even for drivers that can't suspend.
			 */
			if (!driver->suspend || state > PM_SUSPEND_MEM) {
#if 1
				dev_warn(&intf->dev, "resume is unsafe!\n");
#else
				down_write(&usb_bus_type.rwsem);
				device_release_driver(&intf->dev);
				up_write(&usb_bus_type.rwsem);
#endif
			}
		}
	}

	/*
	 * FIXME this needs port power off call paths too, to help force
	 * USB into the "generic" PM model.  At least for devices on
	 * ports that aren't using ganged switching (usually root hubs).
	 *
	 * NOTE: SRP-capable links should adopt more aggressive poweroff
	 * policies (when HNP doesn't apply) once we have mechanisms to
	 * turn power back on!  (Likely not before 2.7...)
	 */
	if (state > PM_SUSPEND_MEM) {
		dev_warn(&udev->dev, "no poweroff yet, suspending instead\n");
	}

	/* "global suspend" of the HC-to-USB interface (root hub), or
	 * "selective suspend" of just one hub-device link.
	 */
	if (!udev->parent) {
		struct usb_bus	*bus = udev->bus;
		if (bus && bus->op->hub_suspend) {
			status = bus->op->hub_suspend (bus);
			if (status == 0)
				usb_set_device_state(udev,
						USB_STATE_SUSPENDED);
		} else
			status = -EOPNOTSUPP;
	} else
		status = hub_port_suspend(hdev_to_hub(udev->parent), port1,
				udev);

	if (status == 0)
		udev->dev.power.power_state = state;
	return status;
}

/**
 * usb_suspend_device - suspend a usb device
 * @udev: device that's no longer in active use
 * @state: PM_SUSPEND_MEM to suspend
 * Context: must be able to sleep; device not locked
 *
 * Suspends a USB device that isn't in active use, conserving power.
 * Devices may wake out of a suspend, if anything important happens,
 * using the remote wakeup mechanism.  They may also be taken out of
 * suspend by the host, using usb_resume_device().  It's also routine
 * to disconnect devices while they are suspended.
 *
 * Suspending OTG devices may trigger HNP, if that's been enabled
 * between a pair of dual-role devices.  That will change roles, such
 * as from A-Host to A-Peripheral or from B-Host back to B-Peripheral.
 *
 * Returns 0 on success, else negative errno.
 */
int usb_suspend_device(struct usb_device *udev, u32 state)
{
	int	port1, status;

	port1 = locktree(udev);
	if (port1 < 0)
		return port1;

	status = __usb_suspend_device(udev, port1, state);
	usb_unlock_device(udev);
	return status;
}

/*
 * hardware resume signaling is finished, either because of selective
 * resume (by host) or remote wakeup (by device) ... now see what changed
 * in the tree that's rooted at this device.
 */
static int finish_port_resume(struct usb_device *udev)
{
	int	status;
	u16	devstatus;

	/* caller owns the udev device lock */
	dev_dbg(&udev->dev, "usb resume\n");

	/* usb ch9 identifies four variants of SUSPENDED, based on what
	 * state the device resumes to.  Linux currently won't see the
	 * first two on the host side; they'd be inside hub_port_init()
	 * during many timeouts, but khubd can't suspend until later.
	 */
	usb_set_device_state(udev, udev->actconfig
			? USB_STATE_CONFIGURED
			: USB_STATE_ADDRESS);
	udev->dev.power.power_state = PM_SUSPEND_ON;

 	/* 10.5.4.5 says be sure devices in the tree are still there.
 	 * For now let's assume the device didn't go crazy on resume,
	 * and device drivers will know about any resume quirks.
	 */
	status = usb_get_status(udev, USB_RECIP_DEVICE, 0, &devstatus);
	if (status < 0)
		dev_dbg(&udev->dev,
			"gone after usb resume? status %d\n",
			status);
	else if (udev->actconfig) {
		unsigned	i;

		le16_to_cpus(&devstatus);
		if (devstatus & (1 << USB_DEVICE_REMOTE_WAKEUP)) {
			status = usb_control_msg(udev,
					usb_sndctrlpipe(udev, 0),
					USB_REQ_CLEAR_FEATURE,
						USB_RECIP_DEVICE,
					USB_DEVICE_REMOTE_WAKEUP, 0,
					NULL, 0,
					USB_CTRL_SET_TIMEOUT);
			if (status) {
				dev_dbg(&udev->dev, "disable remote "
					"wakeup, status %d\n", status);
				status = 0;
			}
		}

		/* resume interface drivers; if this is a hub, it
		 * resumes the child devices
		 */
		for (i = 0; i < udev->actconfig->desc.bNumInterfaces; i++) {
			struct usb_interface	*intf;
			struct usb_driver	*driver;

			intf = udev->actconfig->interface[i];
			if (intf->dev.power.power_state == PM_SUSPEND_ON)
				continue;
			if (!intf->dev.driver) {
				/* FIXME maybe force to alt 0 */
				continue;
			}
			driver = to_usb_driver(intf->dev.driver);

			/* bus_rescan_devices() may rebind drivers */
			if (!driver->resume)
				continue;

			/* can we do better than just logging errors? */
			status = driver->resume(intf);
			if (intf->dev.power.power_state != PM_SUSPEND_ON
					|| status)
				dev_dbg(&intf->dev,
					"resume fail, state %d code %d\n",
					intf->dev.power.power_state, status);
		}
		status = 0;

	} else if (udev->devnum <= 0) {
		dev_dbg(&udev->dev, "bogus resume!\n");
		status = -EINVAL;
	}
	return status;
}

static int
hub_port_resume(struct usb_hub *hub, int port1, struct usb_device *udev)
{
	int	status;

	// dev_dbg(hub->intfdev, "resume port %d\n", port1);

	/* see 7.1.7.7; affects power usage, but not budgeting */
	status = clear_port_feature(hub->hdev,
			port1, USB_PORT_FEAT_SUSPEND);
	if (status) {
		dev_dbg(hub->intfdev,
			"can't resume port %d, status %d\n",
			port1, status);
	} else {
		u16		devstatus;
		u16		portchange;

		/* drive resume for at least 20 msec */
		if (udev)
			dev_dbg(&udev->dev, "RESUME\n");
		msleep(25);

#define LIVE_FLAGS	( USB_PORT_STAT_POWER \
			| USB_PORT_STAT_ENABLE \
			| USB_PORT_STAT_CONNECTION)

		/* Virtual root hubs can trigger on GET_PORT_STATUS to
		 * stop resume signaling.  Then finish the resume
		 * sequence.
		 */
		devstatus = portchange = 0;
		status = hub_port_status(hub, port1,
				&devstatus, &portchange);
		if (status < 0
				|| (devstatus & LIVE_FLAGS) != LIVE_FLAGS
				|| (devstatus & USB_PORT_STAT_SUSPEND) != 0
				) {
			dev_dbg(hub->intfdev,
				"port %d status %04x.%04x after resume, %d\n",
				port1, portchange, devstatus, status);
		} else {
			/* TRSMRCY = 10 msec */
			msleep(10);
			if (udev)
				status = finish_port_resume(udev);
		}
	}
	if (status < 0)
		hub_port_logical_disconnect(hub, port1);

	return status;
}

static int hub_resume (struct usb_interface *intf);

/**
 * usb_resume_device - re-activate a suspended usb device
 * @udev: device to re-activate
 * Context: must be able to sleep; device not locked
 *
 * This will re-activate the suspended device, increasing power usage
 * while letting drivers communicate again with its endpoints.
 * USB resume explicitly guarantees that the power session between
 * the host and the device is the same as it was when the device
 * suspended.
 *
 * Returns 0 on success, else negative errno.
 */
int usb_resume_device(struct usb_device *udev)
{
	int	port1, status;

	port1 = locktree(udev);
	if (port1 < 0)
		return port1;

	/* "global resume" of the HC-to-USB interface (root hub), or
	 * selective resume of one hub-to-device port
	 */
	if (!udev->parent) {
		struct usb_bus	*bus = udev->bus;
		if (bus && bus->op->hub_resume) {
			status = bus->op->hub_resume (bus);
		} else
			status = -EOPNOTSUPP;
		if (status == 0) {
			/* TRSMRCY = 10 msec */
			msleep(10);
			usb_set_device_state (udev, USB_STATE_CONFIGURED);
			status = hub_resume (udev
					->actconfig->interface[0]);
		}
	} else if (udev->state == USB_STATE_SUSPENDED) {
		// NOTE this fails if parent is also suspended...
		status = hub_port_resume(hdev_to_hub(udev->parent),
				port1, udev);
	} else {
		status = 0;
	}
	if (status < 0) {
		dev_dbg(&udev->dev, "can't resume, status %d\n",
			status);
	}

	usb_unlock_device(udev);

	/* rebind drivers that had no suspend() */
	if (status == 0) {
		usb_lock_all_devices();
		bus_rescan_devices(&usb_bus_type);
		usb_unlock_all_devices();
	}
	return status;
}

static int remote_wakeup(struct usb_device *udev)
{
	int	status = 0;

	/* don't repeat RESUME sequence if this device
	 * was already woken up by some other task
	 */
	down(&udev->serialize);
	if (udev->state == USB_STATE_SUSPENDED) {
		dev_dbg(&udev->dev, "RESUME (wakeup)\n");
		/* TRSMRCY = 10 msec */
		msleep(10);
		status = finish_port_resume(udev);
	}
	up(&udev->serialize);
	return status;
}

static int hub_suspend(struct usb_interface *intf, u32 state)
{
	struct usb_hub		*hub = usb_get_intfdata (intf);
	struct usb_device	*hdev = hub->hdev;
	unsigned		port1;
	int			status;

	/* stop khubd and related activity */
	hub_quiesce(hub);

	/* then suspend every port */
	for (port1 = 1; port1 <= hdev->maxchild; port1++) {
		struct usb_device	*udev;

		udev = hdev->children [port1-1];
		if (!udev)
			continue;
		down(&udev->serialize);
		status = __usb_suspend_device(udev, port1, state);
		up(&udev->serialize);
		if (status < 0)
			dev_dbg(&intf->dev, "suspend port %d --> %d\n",
				port1, status);
	}

	intf->dev.power.power_state = state;
	return 0;
}

static int hub_resume(struct usb_interface *intf)
{
	struct usb_device	*hdev = interface_to_usbdev(intf);
	struct usb_hub		*hub = usb_get_intfdata (intf);
	unsigned		port1;
	int			status;

	if (intf->dev.power.power_state == PM_SUSPEND_ON)
		return 0;

	for (port1 = 1; port1 <= hdev->maxchild; port1++) {
		struct usb_device	*udev;
		u16			portstat, portchange;

		udev = hdev->children [port1-1];
		status = hub_port_status(hub, port1, &portstat, &portchange);
		if (status == 0) {
			if (portchange & USB_PORT_STAT_C_SUSPEND) {
				clear_port_feature(hdev, port1,
					USB_PORT_FEAT_C_SUSPEND);
				portchange &= ~USB_PORT_STAT_C_SUSPEND;
			}

			/* let khubd handle disconnects etc */
			if (portchange)
				continue;
		}

		if (!udev || status < 0)
			continue;
		down (&udev->serialize);
		if (portstat & USB_PORT_STAT_SUSPEND)
			status = hub_port_resume(hub, port1, udev);
		else {
			status = finish_port_resume(udev);
			if (status < 0) {
				dev_dbg(&intf->dev, "resume port %d --> %d\n",
					port1, status);
				hub_port_logical_disconnect(hub, port1);
			}
		}
		up(&udev->serialize);
	}
	intf->dev.power.power_state = PM_SUSPEND_ON;

	hub_activate(hub);
	return 0;
}

#else	/* !CONFIG_USB_SUSPEND */

int usb_suspend_device(struct usb_device *udev, u32 state)
{
	return 0;
}

int usb_resume_device(struct usb_device *udev)
{
	return 0;
}

#define	hub_suspend		NULL
#define	hub_resume		NULL
#define	remote_wakeup(x)	0

#endif	/* CONFIG_USB_SUSPEND */

EXPORT_SYMBOL(usb_suspend_device);
EXPORT_SYMBOL(usb_resume_device);



/* USB 2.0 spec, 7.1.7.3 / fig 7-29:
 *
 * Between connect detection and reset signaling there must be a delay
 * of 100ms at least for debounce and power-settling.  The corresponding
 * timer shall restart whenever the downstream port detects a disconnect.
 * 
 * Apparently there are some bluetooth and irda-dongles and a number of
 * low-speed devices for which this debounce period may last over a second.
 * Not covered by the spec - but easy to deal with.
 *
 * This implementation uses a 1500ms total debounce timeout; if the
 * connection isn't stable by then it returns -ETIMEDOUT.  It checks
 * every 25ms for transient disconnects.  When the port status has been
 * unchanged for 100ms it returns the port status.
 */

#define HUB_DEBOUNCE_TIMEOUT	1500
#define HUB_DEBOUNCE_STEP	  25
#define HUB_DEBOUNCE_STABLE	 100

static int hub_port_debounce(struct usb_hub *hub, int port1)
{
	int ret;
	int total_time, stable_time = 0;
	u16 portchange, portstatus;
	unsigned connection = 0xffff;

	for (total_time = 0; ; total_time += HUB_DEBOUNCE_STEP) {
		ret = hub_port_status(hub, port1, &portstatus, &portchange);
		if (ret < 0)
			return ret;

		if (!(portchange & USB_PORT_STAT_C_CONNECTION) &&
		     (portstatus & USB_PORT_STAT_CONNECTION) == connection) {
			stable_time += HUB_DEBOUNCE_STEP;
			if (stable_time >= HUB_DEBOUNCE_STABLE)
				break;
		} else {
			stable_time = 0;
			connection = portstatus & USB_PORT_STAT_CONNECTION;
		}

		if (portchange & USB_PORT_STAT_C_CONNECTION) {
			clear_port_feature(hub->hdev, port1,
					USB_PORT_FEAT_C_CONNECTION);
		}

		if (total_time >= HUB_DEBOUNCE_TIMEOUT)
			break;
		msleep(HUB_DEBOUNCE_STEP);
	}

	dev_dbg (hub->intfdev,
		"debounce: port %d: total %dms stable %dms status 0x%x\n",
		port1, total_time, stable_time, portstatus);

	if (stable_time < HUB_DEBOUNCE_STABLE)
		return -ETIMEDOUT;
	return portstatus;
}

static void ep0_reinit(struct usb_device *udev)
{
	usb_disable_endpoint(udev, 0 + USB_DIR_IN);
	usb_disable_endpoint(udev, 0 + USB_DIR_OUT);
	udev->ep_in[0] = udev->ep_out[0] = &udev->ep0;
}

#define usb_sndaddr0pipe()	(PIPE_CONTROL << 30)
#define usb_rcvaddr0pipe()	((PIPE_CONTROL << 30) | USB_DIR_IN)

static int hub_set_address(struct usb_device *udev)
{
	int retval;

	if (udev->devnum == 0)
		return -EINVAL;
	if (udev->state == USB_STATE_ADDRESS)
		return 0;
	if (udev->state != USB_STATE_DEFAULT)
		return -EINVAL;
	retval = usb_control_msg(udev, usb_sndaddr0pipe(),
		USB_REQ_SET_ADDRESS, 0, udev->devnum, 0,
		NULL, 0, HZ * USB_CTRL_SET_TIMEOUT);
	if (retval == 0) {
		usb_set_device_state(udev, USB_STATE_ADDRESS);
		ep0_reinit(udev);
	}
	return retval;
}

/* Reset device, (re)assign address, get device descriptor.
 * Device connection must be stable, no more debouncing needed.
 * Returns device in USB_STATE_ADDRESS, except on error.
 *
 * If this is called for an already-existing device (as part of
 * usb_reset_device), the caller must own the device lock.  For a
 * newly detected device that is not accessible through any global
 * pointers, it's not necessary to lock the device.
 */
static int
hub_port_init (struct usb_hub *hub, struct usb_device *udev, int port1,
		int retry_counter)
{
	static DECLARE_MUTEX(usb_address0_sem);

	struct usb_device	*hdev = hub->hdev;
	int			i, j, retval;
	unsigned		delay = HUB_SHORT_RESET_TIME;
	enum usb_device_speed	oldspeed = udev->speed;

	/* root hub ports have a slightly longer reset period
	 * (from USB 2.0 spec, section 7.1.7.5)
	 */
	if (!hdev->parent) {
		delay = HUB_ROOT_RESET_TIME;
		if (port1 == hdev->bus->otg_port)
			hdev->bus->b_hnp_enable = 0;
	}

	/* Some low speed devices have problems with the quick delay, so */
	/*  be a bit pessimistic with those devices. RHbug #23670 */
	if (oldspeed == USB_SPEED_LOW)
		delay = HUB_LONG_RESET_TIME;

	down(&usb_address0_sem);

	/* Reset the device; full speed may morph to high speed */
	retval = hub_port_reset(hub, port1, udev, delay);
	if (retval < 0)		/* error or disconnect */
		goto fail;
				/* success, speed is known */
	retval = -ENODEV;

	if (oldspeed != USB_SPEED_UNKNOWN && oldspeed != udev->speed) {
		dev_dbg(&udev->dev, "device reset changed speed!\n");
		goto fail;
	}
	oldspeed = udev->speed;
  
	/* USB 2.0 section 5.5.3 talks about ep0 maxpacket ...
	 * it's fixed size except for full speed devices.
	 */
	switch (udev->speed) {
	case USB_SPEED_HIGH:		/* fixed at 64 */
		udev->ep0.desc.wMaxPacketSize = __constant_cpu_to_le16(64);
		break;
	case USB_SPEED_FULL:		/* 8, 16, 32, or 64 */
		/* to determine the ep0 maxpacket size, try to read
		 * the device descriptor to get bMaxPacketSize0 and
		 * then correct our initial guess.
		 */
		udev->ep0.desc.wMaxPacketSize = __constant_cpu_to_le16(64);
		break;
	case USB_SPEED_LOW:		/* fixed at 8 */
		udev->ep0.desc.wMaxPacketSize = __constant_cpu_to_le16(8);
		break;
	default:
		goto fail;
	}
 
	dev_info (&udev->dev,
			"%s %s speed USB device using %s and address %d\n",
			(udev->config) ? "reset" : "new",
			({ char *speed; switch (udev->speed) {
			case USB_SPEED_LOW:	speed = "low";	break;
			case USB_SPEED_FULL:	speed = "full";	break;
			case USB_SPEED_HIGH:	speed = "high";	break;
			default: 		speed = "?";	break;
			}; speed;}),
			udev->bus->controller->driver->name,
			udev->devnum);

	/* Set up TT records, if needed  */
	if (hdev->tt) {
		udev->tt = hdev->tt;
		udev->ttport = hdev->ttport;
	} else if (udev->speed != USB_SPEED_HIGH
			&& hdev->speed == USB_SPEED_HIGH) {
		udev->tt = &hub->tt;
		udev->ttport = port1;
	}
 
	/* Why interleave GET_DESCRIPTOR and SET_ADDRESS this way?
	 * Because device hardware and firmware is sometimes buggy in
	 * this area, and this is how Linux has done it for ages.
	 * Change it cautiously.
	 *
	 * NOTE:  If USE_NEW_SCHEME() is true we will start by issuing
	 * a 64-byte GET_DESCRIPTOR request.  This is what Windows does,
	 * so it may help with some non-standards-compliant devices.
	 * Otherwise we start with SET_ADDRESS and then try to read the
	 * first 8 bytes of the device descriptor to get the ep0 maxpacket
	 * value.
	 */
	for (i = 0; i < GET_DESCRIPTOR_TRIES; (++i, msleep(100))) {
		if (USE_NEW_SCHEME(retry_counter)) {
			struct usb_device_descriptor *buf;
			int r = 0;

#define GET_DESCRIPTOR_BUFSIZE	64
			buf = kmalloc(GET_DESCRIPTOR_BUFSIZE, GFP_NOIO);
			if (!buf) {
				retval = -ENOMEM;
				continue;
			}
			buf->bMaxPacketSize0 = 0;

			/* Use a short timeout the first time through,
			 * so that recalcitrant full-speed devices with
			 * 8- or 16-byte ep0-maxpackets won't slow things
			 * down tremendously by NAKing the unexpectedly
			 * early status stage.  Also, retry on length 0
			 * or stall; some devices are flakey.
			 */
			for (j = 0; j < 3; ++j) {
				r = usb_control_msg(udev, usb_rcvaddr0pipe(),
					USB_REQ_GET_DESCRIPTOR, USB_DIR_IN,
					USB_DT_DEVICE << 8, 0,
					buf, GET_DESCRIPTOR_BUFSIZE,
					(i ? HZ * USB_CTRL_GET_TIMEOUT : HZ));
				if (r == 0 || r == -EPIPE)
					continue;
				if (r < 0)
					break;
			}
			udev->descriptor.bMaxPacketSize0 =
					buf->bMaxPacketSize0;
			kfree(buf);

			retval = hub_port_reset(hub, port1, udev, delay);
			if (retval < 0)		/* error or disconnect */
				goto fail;
			if (oldspeed != udev->speed) {
				dev_dbg(&udev->dev,
					"device reset changed speed!\n");
				retval = -ENODEV;
				goto fail;
			}
			switch (udev->descriptor.bMaxPacketSize0) {
			case 64: case 32: case 16: case 8:
				break;
			default:
				dev_err(&udev->dev, "device descriptor "
						"read/%s, error %d\n",
						"64", r);
				retval = -EMSGSIZE;
				continue;
			}
#undef GET_DESCRIPTOR_BUFSIZE
		}

		for (j = 0; j < SET_ADDRESS_TRIES; ++j) {
			retval = hub_set_address(udev);
			if (retval >= 0)
				break;
			msleep(200);
		}
		if (retval < 0) {
			dev_err(&udev->dev,
				"device not accepting address %d, error %d\n",
				udev->devnum, retval);
			goto fail;
		}
 
		/* cope with hardware quirkiness:
		 *  - let SET_ADDRESS settle, some device hardware wants it
		 *  - read ep0 maxpacket even for high and low speed,
  		 */
		msleep(10);
		if (USE_NEW_SCHEME(retry_counter))
			break;

		retval = usb_get_device_descriptor(udev, 8);
		if (retval < 8) {
			dev_err(&udev->dev, "device descriptor "
					"read/%s, error %d\n",
					"8", retval);
			if (retval >= 0)
				retval = -EMSGSIZE;
		} else {
			retval = 0;
			break;
		}
	}
	if (retval)
		goto fail;

	i = udev->descriptor.bMaxPacketSize0;
	if (le16_to_cpu(udev->ep0.desc.wMaxPacketSize) != i) {
		if (udev->speed != USB_SPEED_FULL ||
				!(i == 8 || i == 16 || i == 32 || i == 64)) {
			dev_err(&udev->dev, "ep0 maxpacket = %d\n", i);
			retval = -EMSGSIZE;
			goto fail;
		}
		dev_dbg(&udev->dev, "ep0 maxpacket = %d\n", i);
		udev->ep0.desc.wMaxPacketSize = cpu_to_le16(i);
		ep0_reinit(udev);
	}
  
	retval = usb_get_device_descriptor(udev, USB_DT_DEVICE_SIZE);
	if (retval < (signed)sizeof(udev->descriptor)) {
		dev_err(&udev->dev, "device descriptor read/%s, error %d\n",
			"all", retval);
		if (retval >= 0)
			retval = -ENOMSG;
		goto fail;
	}

	retval = 0;

fail:
	if (retval)
		hub_port_disable(hub, port1, 0);
	up(&usb_address0_sem);
	return retval;
}

static void
check_highspeed (struct usb_hub *hub, struct usb_device *udev, int port1)
{
	struct usb_qualifier_descriptor	*qual;
	int				status;

	qual = kmalloc (sizeof *qual, SLAB_KERNEL);
	if (qual == NULL)
		return;

	status = usb_get_descriptor (udev, USB_DT_DEVICE_QUALIFIER, 0,
			qual, sizeof *qual);
	if (status == sizeof *qual) {
		dev_info(&udev->dev, "not running at top speed; "
			"connect to a high speed hub\n");
		/* hub LEDs are probably harder to miss than syslog */
		if (hub->has_indicators) {
			hub->indicator[port1-1] = INDICATOR_GREEN_BLINK;
			schedule_work (&hub->leds);
		}
	}
	kfree (qual);
}

static unsigned
hub_power_remaining (struct usb_hub *hub)
{
	struct usb_device *hdev = hub->hdev;
	int remaining;
	unsigned i;

	remaining = hub->power_budget;
	if (!remaining)		/* self-powered */
		return 0;

	for (i = 0; i < hdev->maxchild; i++) {
		struct usb_device	*udev = hdev->children[i];
		int			delta, ceiling;

		if (!udev)
			continue;

		/* 100mA per-port ceiling, or 8mA for OTG ports */
		if (i != (udev->bus->otg_port - 1) || hdev->parent)
			ceiling = 50;
		else
			ceiling = 4;

		if (udev->actconfig)
			delta = udev->actconfig->desc.bMaxPower;
		else
			delta = ceiling;
		// dev_dbg(&udev->dev, "budgeted %dmA\n", 2 * delta);
		if (delta > ceiling)
			dev_warn(&udev->dev, "%dmA over %dmA budget!\n",
				2 * (delta - ceiling), 2 * ceiling);
		remaining -= delta;
	}
	if (remaining < 0) {
		dev_warn(hub->intfdev,
			"%dmA over power budget!\n",
			-2 * remaining);
		remaining = 0;
	}
	return remaining;
}

/* Handle physical or logical connection change events.
 * This routine is called when:
 * 	a port connection-change occurs;
 *	a port enable-change occurs (often caused by EMI);
 *	usb_reset_device() encounters changed descriptors (as from
 *		a firmware download)
 * caller already locked the hub
 */
static void hub_port_connect_change(struct usb_hub *hub, int port1,
					u16 portstatus, u16 portchange)
{
	struct usb_device *hdev = hub->hdev;
	struct device *hub_dev = hub->intfdev;
	int status, i;
 
	dev_dbg (hub_dev,
		"port %d, status %04x, change %04x, %s\n",
		port1, portstatus, portchange, portspeed (portstatus));

	if (hub->has_indicators) {
		set_port_led(hub, port1, HUB_LED_AUTO);
		hub->indicator[port1-1] = INDICATOR_AUTO;
	}
 
	/* Disconnect any existing devices under this port */
	if (hdev->children[port1-1])
		usb_disconnect(&hdev->children[port1-1]);
	clear_bit(port1, hub->change_bits);

#ifdef	CONFIG_USB_OTG
	/* during HNP, don't repeat the debounce */
	if (hdev->bus->is_b_host)
		portchange &= ~USB_PORT_STAT_C_CONNECTION;
#endif

	if (portchange & USB_PORT_STAT_C_CONNECTION) {
		status = hub_port_debounce(hub, port1);
		if (status < 0) {
			dev_err (hub_dev,
				"connect-debounce failed, port %d disabled\n",
				port1);
			goto done;
		}
		portstatus = status;
	}

	/* Return now if nothing is connected */
	if (!(portstatus & USB_PORT_STAT_CONNECTION)) {

		/* maybe switch power back on (e.g. root hub was reset) */
		if ((hub->descriptor->wHubCharacteristics
					& HUB_CHAR_LPSM) < 2
				&& !(portstatus & (1 << USB_PORT_FEAT_POWER)))
			set_port_feature(hdev, port1, USB_PORT_FEAT_POWER);
 
		if (portstatus & USB_PORT_STAT_ENABLE)
  			goto done;
		return;
	}

#ifdef  CONFIG_USB_SUSPEND
	/* If something is connected, but the port is suspended, wake it up. */
	if (portstatus & USB_PORT_STAT_SUSPEND) {
		status = hub_port_resume(hub, port1, NULL);
		if (status < 0) {
			dev_dbg(hub_dev,
				"can't clear suspend on port %d; %d\n",
				port1, status);
			goto done;
		}
	}
#endif

	for (i = 0; i < SET_CONFIG_TRIES; i++) {
		struct usb_device *udev;

		/* reallocate for each attempt, since references
		 * to the previous one can escape in various ways
		 */
		udev = usb_alloc_dev(hdev, hdev->bus, port1);
		if (!udev) {
			dev_err (hub_dev,
				"couldn't allocate port %d usb_device\n",
				port1);
			goto done;
		}

		usb_set_device_state(udev, USB_STATE_POWERED);
		udev->speed = USB_SPEED_UNKNOWN;
 
		/* set the address */
		choose_address(udev);
		if (udev->devnum <= 0) {
			status = -ENOTCONN;	/* Don't retry */
			goto loop;
		}

		/* reset and get descriptor */
		status = hub_port_init(hub, udev, port1, i);
		if (status < 0)
			goto loop;

		/* consecutive bus-powered hubs aren't reliable; they can
		 * violate the voltage drop budget.  if the new child has
		 * a "powered" LED, users should notice we didn't enable it
		 * (without reading syslog), even without per-port LEDs
		 * on the parent.
		 */
		if (udev->descriptor.bDeviceClass == USB_CLASS_HUB
				&& hub->power_budget) {
			u16	devstat;

			status = usb_get_status(udev, USB_RECIP_DEVICE, 0,
					&devstat);
			if (status < 0) {
				dev_dbg(&udev->dev, "get status %d ?\n", status);
				goto loop_disable;
			}
			cpu_to_le16s(&devstat);
			if ((devstat & (1 << USB_DEVICE_SELF_POWERED)) == 0) {
				dev_err(&udev->dev,
					"can't connect bus-powered hub "
					"to this port\n");
				if (hub->has_indicators) {
					hub->indicator[port1-1] =
						INDICATOR_AMBER_BLINK;
					schedule_work (&hub->leds);
				}
				status = -ENOTCONN;	/* Don't retry */
				goto loop_disable;
			}
		}
 
		/* check for devices running slower than they could */
		if (le16_to_cpu(udev->descriptor.bcdUSB) >= 0x0200
				&& udev->speed == USB_SPEED_FULL
				&& highspeed_hubs != 0)
			check_highspeed (hub, udev, port1);

		/* Store the parent's children[] pointer.  At this point
		 * udev becomes globally accessible, although presumably
		 * no one will look at it until hdev is unlocked.
		 */
		down (&udev->serialize);
		status = 0;

		/* We mustn't add new devices if the parent hub has
		 * been disconnected; we would race with the
		 * recursively_mark_NOTATTACHED() routine.
		 */
		spin_lock_irq(&device_state_lock);
		if (hdev->state == USB_STATE_NOTATTACHED)
			status = -ENOTCONN;
		else
			hdev->children[port1-1] = udev;
		spin_unlock_irq(&device_state_lock);

		/* Run it through the hoops (find a driver, etc) */
		if (!status) {
			status = usb_new_device(udev);
			if (status) {
				spin_lock_irq(&device_state_lock);
				hdev->children[port1-1] = NULL;
				spin_unlock_irq(&device_state_lock);
			}
		}

		up (&udev->serialize);
		if (status)
			goto loop_disable;

		status = hub_power_remaining(hub);
		if (status)
			dev_dbg(hub_dev,
				"%dmA power budget left\n",
				2 * status);

		return;

loop_disable:
		hub_port_disable(hub, port1, 1);
loop:
		ep0_reinit(udev);
		release_address(udev);
		usb_put_dev(udev);
		if (status == -ENOTCONN)
			break;
	}
 
done:
	hub_port_disable(hub, port1, 1);
}

static void hub_events(void)
{
	struct list_head *tmp;
	struct usb_device *hdev;
	struct usb_interface *intf;
	struct usb_hub *hub;
	struct device *hub_dev;
	u16 hubstatus;
	u16 hubchange;
	u16 portstatus;
	u16 portchange;
	int i, ret;
	int connect_change;

	/*
	 *  We restart the list every time to avoid a deadlock with
	 * deleting hubs downstream from this one. This should be
	 * safe since we delete the hub from the event list.
	 * Not the most efficient, but avoids deadlocks.
	 */
	while (1) {

		/* Grab the first entry at the beginning of the list */
		spin_lock_irq(&hub_event_lock);
		if (list_empty(&hub_event_list)) {
			spin_unlock_irq(&hub_event_lock);
			break;
		}

		tmp = hub_event_list.next;
		list_del_init(tmp);

		hub = list_entry(tmp, struct usb_hub, event_list);
		hdev = hub->hdev;
		intf = to_usb_interface(hub->intfdev);
		hub_dev = &intf->dev;

		dev_dbg(hub_dev, "state %d ports %d chg %04x evt %04x\n",
				hdev->state, hub->descriptor
					? hub->descriptor->bNbrPorts
					: 0,
				/* NOTE: expects max 15 ports... */
				(u16) hub->change_bits[0],
				(u16) hub->event_bits[0]);

		usb_get_intf(intf);
		spin_unlock_irq(&hub_event_lock);

		/* Lock the device, then check to see if we were
		 * disconnected while waiting for the lock to succeed. */
		if (locktree(hdev) < 0) {
			usb_put_intf(intf);
			continue;
		}
		if (hub != usb_get_intfdata(intf) || hub->quiescing)
			goto loop;

		if (hub->error) {
			dev_dbg (hub_dev, "resetting for error %d\n",
				hub->error);

			ret = usb_reset_device(hdev);
			if (ret) {
				dev_dbg (hub_dev,
					"error resetting hub: %d\n", ret);
				goto loop;
			}

			hub->nerrors = 0;
			hub->error = 0;
		}

		/* deal with port status changes */
		for (i = 1; i <= hub->descriptor->bNbrPorts; i++) {
			connect_change = test_bit(i, hub->change_bits);
			if (!test_and_clear_bit(i, hub->event_bits) &&
					!connect_change)
				continue;

			ret = hub_port_status(hub, i,
					&portstatus, &portchange);
			if (ret < 0)
				continue;

			if (portchange & USB_PORT_STAT_C_CONNECTION) {
				clear_port_feature(hdev, i,
					USB_PORT_FEAT_C_CONNECTION);
				connect_change = 1;
			}

			if (portchange & USB_PORT_STAT_C_ENABLE) {
				if (!connect_change)
					dev_dbg (hub_dev,
						"port %d enable change, "
						"status %08x\n",
						i, portstatus);
				clear_port_feature(hdev, i,
					USB_PORT_FEAT_C_ENABLE);

				/*
				 * EM interference sometimes causes badly
				 * shielded USB devices to be shutdown by
				 * the hub, this hack enables them again.
				 * Works at least with mouse driver. 
				 */
				if (!(portstatus & USB_PORT_STAT_ENABLE)
				    && !connect_change
				    && hdev->children[i-1]) {
					dev_err (hub_dev,
					    "port %i "
					    "disabled by hub (EMI?), "
					    "re-enabling...\n",
						i);
					connect_change = 1;
				}
			}

			if (portchange & USB_PORT_STAT_C_SUSPEND) {
				clear_port_feature(hdev, i,
					USB_PORT_FEAT_C_SUSPEND);
				if (hdev->children[i-1]) {
					ret = remote_wakeup(hdev->
							children[i-1]);
					if (ret < 0)
						connect_change = 1;
				} else {
					ret = -ENODEV;
					hub_port_disable(hub, i, 1);
				}
				dev_dbg (hub_dev,
					"resume on port %d, status %d\n",
					i, ret);
			}
			
			if (portchange & USB_PORT_STAT_C_OVERCURRENT) {
				dev_err (hub_dev,
					"over-current change on port %d\n",
					i);
				clear_port_feature(hdev, i,
					USB_PORT_FEAT_C_OVER_CURRENT);
				hub_power_on(hub);
			}

			if (portchange & USB_PORT_STAT_C_RESET) {
				dev_dbg (hub_dev,
					"reset change on port %d\n",
					i);
				clear_port_feature(hdev, i,
					USB_PORT_FEAT_C_RESET);
			}

			if (connect_change)
				hub_port_connect_change(hub, i,
						portstatus, portchange);
		} /* end for i */

		/* deal with hub status changes */
		if (test_and_clear_bit(0, hub->event_bits) == 0)
			;	/* do nothing */
		else if (hub_hub_status(hub, &hubstatus, &hubchange) < 0)
			dev_err (hub_dev, "get_hub_status failed\n");
		else {
			if (hubchange & HUB_CHANGE_LOCAL_POWER) {
				dev_dbg (hub_dev, "power change\n");
				clear_hub_feature(hdev, C_HUB_LOCAL_POWER);
			}
			if (hubchange & HUB_CHANGE_OVERCURRENT) {
				dev_dbg (hub_dev, "overcurrent change\n");
				msleep(500);	/* Cool down */
				clear_hub_feature(hdev, C_HUB_OVER_CURRENT);
                        	hub_power_on(hub);
			}
		}

loop:
		usb_unlock_device(hdev);
		usb_put_intf(intf);

        } /* end while (1) */
}

static int hub_thread(void *__unused)
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
		try_to_freeze(PF_FREEZE);
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
	.suspend =	hub_suspend,
	.resume =	hub_resume,
	.ioctl =	hub_ioctl,
	.id_table =	hub_id_table,
};

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


static int config_descriptors_changed(struct usb_device *udev)
{
	unsigned			index;
	unsigned			len = 0;
	struct usb_config_descriptor	*buf;

	for (index = 0; index < udev->descriptor.bNumConfigurations; index++) {
		if (len < le16_to_cpu(udev->config[index].desc.wTotalLength))
			len = le16_to_cpu(udev->config[index].desc.wTotalLength);
	}
	buf = kmalloc (len, SLAB_KERNEL);
	if (buf == NULL) {
		dev_err(&udev->dev, "no mem to re-read configs after reset\n");
		/* assume the worst */
		return 1;
	}
	for (index = 0; index < udev->descriptor.bNumConfigurations; index++) {
		int length;
		int old_length = le16_to_cpu(udev->config[index].desc.wTotalLength);

		length = usb_get_descriptor(udev, USB_DT_CONFIG, index, buf,
				old_length);
		if (length < old_length) {
			dev_dbg(&udev->dev, "config index %d, error %d\n",
					index, length);
			break;
		}
		if (memcmp (buf, udev->rawdescriptors[index], old_length)
				!= 0) {
			dev_dbg(&udev->dev, "config index %d changed (#%d)\n",
				index, buf->bConfigurationValue);
			break;
		}
	}
	kfree(buf);
	return index != udev->descriptor.bNumConfigurations;
}

/**
 * usb_reset_device - perform a USB port reset to reinitialize a device
 * @udev: device to reset (not in SUSPENDED or NOTATTACHED state)
 *
 * WARNING - don't reset any device unless drivers for all of its
 * interfaces are expecting that reset!  Maybe some driver->reset()
 * method should eventually help ensure sufficient cooperation.
 *
 * Do a port reset, reassign the device's address, and establish its
 * former operating configuration.  If the reset fails, or the device's
 * descriptors change from their values before the reset, or the original
 * configuration and altsettings cannot be restored, a flag will be set
 * telling khubd to pretend the device has been disconnected and then
 * re-connected.  All drivers will be unbound, and the device will be
 * re-enumerated and probed all over again.
 *
 * Returns 0 if the reset succeeded, -ENODEV if the device has been
 * flagged for logical disconnection, or some other negative error code
 * if the reset wasn't even attempted.
 *
 * The caller must own the device lock.  For example, it's safe to use
 * this from a driver probe() routine after downloading new firmware.
 * For calls that might not occur during probe(), drivers should lock
 * the device using usb_lock_device_for_reset().
 */
int usb_reset_device(struct usb_device *udev)
{
	struct usb_device		*parent_hdev = udev->parent;
	struct usb_hub			*parent_hub;
	struct usb_device_descriptor	descriptor = udev->descriptor;
	struct usb_hub			*hub = NULL;
	int 				i, ret = 0, port1 = -1;

	if (udev->state == USB_STATE_NOTATTACHED ||
			udev->state == USB_STATE_SUSPENDED) {
		dev_dbg(&udev->dev, "device reset not allowed in state %d\n",
				udev->state);
		return -EINVAL;
	}

	if (!parent_hdev) {
		/* this requires hcd-specific logic; see OHCI hc_restart() */
		dev_dbg(&udev->dev, "%s for root hub!\n", __FUNCTION__);
		return -EISDIR;
	}

	for (i = 0; i < parent_hdev->maxchild; i++)
		if (parent_hdev->children[i] == udev) {
			port1 = i + 1;
			break;
		}

	if (port1 < 0) {
		/* If this ever happens, it's very bad */
		dev_err(&udev->dev, "Can't locate device's port!\n");
		return -ENOENT;
	}
	parent_hub = hdev_to_hub(parent_hdev);

	/* If we're resetting an active hub, take some special actions */
	if (udev->actconfig &&
			udev->actconfig->interface[0]->dev.driver ==
				&hub_driver.driver &&
			(hub = hdev_to_hub(udev)) != NULL) {
		hub_pre_reset(hub);
	}

	for (i = 0; i < SET_CONFIG_TRIES; ++i) {

		/* ep0 maxpacket size may change; let the HCD know about it.
		 * Other endpoints will be handled by re-enumeration. */
		ep0_reinit(udev);
		ret = hub_port_init(parent_hub, udev, port1, i);
		if (ret >= 0)
			break;
	}
	if (ret < 0)
		goto re_enumerate;
 
	/* Device might have changed firmware (DFU or similar) */
	if (memcmp(&udev->descriptor, &descriptor, sizeof descriptor)
			|| config_descriptors_changed (udev)) {
		dev_info(&udev->dev, "device firmware changed\n");
		udev->descriptor = descriptor;	/* for disconnect() calls */
		goto re_enumerate;
  	}
  
	if (!udev->actconfig)
		goto done;

	ret = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
			USB_REQ_SET_CONFIGURATION, 0,
			udev->actconfig->desc.bConfigurationValue, 0,
			NULL, 0, HZ * USB_CTRL_SET_TIMEOUT);
	if (ret < 0) {
		dev_err(&udev->dev,
			"can't restore configuration #%d (error=%d)\n",
			udev->actconfig->desc.bConfigurationValue, ret);
		goto re_enumerate;
  	}
	usb_set_device_state(udev, USB_STATE_CONFIGURED);

	for (i = 0; i < udev->actconfig->desc.bNumInterfaces; i++) {
		struct usb_interface *intf = udev->actconfig->interface[i];
		struct usb_interface_descriptor *desc;

		/* set_interface resets host side toggle even
		 * for altsetting zero.  the interface may have no driver.
		 */
		desc = &intf->cur_altsetting->desc;
		ret = usb_set_interface(udev, desc->bInterfaceNumber,
			desc->bAlternateSetting);
		if (ret < 0) {
			dev_err(&udev->dev, "failed to restore interface %d "
				"altsetting %d (error=%d)\n",
				desc->bInterfaceNumber,
				desc->bAlternateSetting,
				ret);
			goto re_enumerate;
		}
	}

done:
	if (hub)
		hub_post_reset(hub);
	return 0;
 
re_enumerate:
	hub_port_logical_disconnect(parent_hub, port1);
	return -ENODEV;
}
