#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <asm/scatterlist.h>

#if !defined (DEBUG) && defined (CONFIG_USB_DEBUG)
#   define DEBUG
#endif
#include <linux/usb.h>


/*-------------------------------------------------------------------------*/

// FIXME make these public somewhere; usbdevfs.h?
//
struct usbtest_param {
	// inputs
	unsigned		test_num;	/* 0..(TEST_CASES-1) */
	int			iterations;
	int			length;
	int			vary;
	int			sglen;

	// outputs
	struct timeval		duration;
};
#define USBTEST_REQUEST	_IOWR('U', 100, struct usbtest_param)

/*-------------------------------------------------------------------------*/

#define	GENERIC		/* let probe() bind using module params */

/* Some devices that can be used for testing will have "real" drivers.
 * Entries for those need to be enabled here by hand, after disabling
 * that "real" driver.
 */
//#define	IBOT2		/* grab iBOT2 webcams */
//#define	KEYSPAN_19Qi	/* grab un-renumerated serial adapter */

/*-------------------------------------------------------------------------*/

struct usbtest_info {
	const char		*name;
	u8			ep_in;		/* bulk/intr source */
	u8			ep_out;		/* bulk/intr sink */
	unsigned		autoconf : 1;
	int			alt;
};

/* this is accessed only through usbfs ioctl calls.
 * one ioctl to issue a test ... one lock per device.
 * tests create other threads if they need them.
 * urbs and buffers are allocated dynamically,
 * and data generated deterministically.
 */
struct usbtest_dev {
	struct usb_interface	*intf;
	struct usbtest_info	*info;
	char			id [32];
	int			in_pipe;
	int			out_pipe;
	struct semaphore	sem;

#define TBUF_SIZE	256
	u8			*buf;
};

static struct usb_device *testdev_to_usbdev (struct usbtest_dev *test)
{
	return interface_to_usbdev (test->intf);
}

/* set up all urbs so they can be used with either bulk or interrupt */
#define	INTERRUPT_RATE		1	/* msec/transfer */

/*-------------------------------------------------------------------------*/

static int
get_endpoints (struct usbtest_dev *dev, struct usb_interface *intf)
{
	int				tmp;
	struct usb_host_interface	*alt;
	struct usb_host_endpoint	*in, *out;
	struct usb_device		*udev;

	for (tmp = 0; tmp < intf->num_altsetting; tmp++) {
		unsigned	ep;

		in = out = 0;
		alt = intf->altsetting + tmp;

		/* take the first altsetting with in-bulk + out-bulk;
		 * ignore other endpoints and altsetttings.
		 */
		for (ep = 0; ep < alt->desc.bNumEndpoints; ep++) {
			struct usb_host_endpoint	*e;

			e = alt->endpoint + ep;
			if (e->desc.bmAttributes != USB_ENDPOINT_XFER_BULK)
				continue;
			if (e->desc.bEndpointAddress & USB_DIR_IN) {
				if (!in)
					in = e;
			} else {
				if (!out)
					out = e;
			}
			if (in && out)
				goto found;
		}
	}
	return -EINVAL;

found:
	udev = testdev_to_usbdev (dev);
	if (alt->desc.bAlternateSetting != 0) {
		tmp = usb_set_interface (udev,
				alt->desc.bInterfaceNumber,
				alt->desc.bAlternateSetting);
		if (tmp < 0)
			return tmp;
	}

	dev->in_pipe = usb_rcvbulkpipe (udev,
			in->desc.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK);
	dev->out_pipe = usb_sndbulkpipe (udev,
			out->desc.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK);
	return 0;
}

/*-------------------------------------------------------------------------*/

/* Support for testing basic non-queued I/O streams.
 *
 * These just package urbs as requests that can be easily canceled.
 * Each urb's data buffer is dynamically allocated; callers can fill
 * them with non-zero test data (or test for it) when appropriate.
 */

static void simple_callback (struct urb *urb, struct pt_regs *regs)
{
	complete ((struct completion *) urb->context);
}

static struct urb *simple_alloc_urb (
	struct usb_device	*udev,
	int			pipe,
	long			bytes
)
{
	struct urb		*urb;

	if (bytes < 0)
		return 0;
	urb = usb_alloc_urb (0, SLAB_KERNEL);
	if (!urb)
		return urb;
	usb_fill_bulk_urb (urb, udev, pipe, 0, bytes, simple_callback, 0);
	urb->interval = (udev->speed == USB_SPEED_HIGH)
			? (INTERRUPT_RATE << 3)
			: INTERRUPT_RATE;
	urb->transfer_flags = URB_NO_TRANSFER_DMA_MAP;
	if (usb_pipein (pipe))
		urb->transfer_flags |= URB_SHORT_NOT_OK;
	urb->transfer_buffer = usb_buffer_alloc (udev, bytes, SLAB_KERNEL,
			&urb->transfer_dma);
	if (!urb->transfer_buffer) {
		usb_free_urb (urb);
		urb = 0;
	} else
		memset (urb->transfer_buffer, 0, bytes);
	return urb;
}

static void simple_free_urb (struct urb *urb)
{
	usb_buffer_free (urb->dev, urb->transfer_buffer_length,
			urb->transfer_buffer, urb->transfer_dma);
	usb_free_urb (urb);
}

static int simple_io (
	struct urb		*urb,
	int			iterations,
	int			vary
)
{
	struct usb_device	*udev = urb->dev;
	int			max = urb->transfer_buffer_length;
	struct completion	completion;
	int			retval = 0;

	urb->context = &completion;
	while (retval == 0 && iterations-- > 0) {
		init_completion (&completion);
		if ((retval = usb_submit_urb (urb, SLAB_KERNEL)) != 0)
			break;

		/* NOTE:  no timeouts; can't be broken out of by interrupt */
		wait_for_completion (&completion);
		retval = urb->status;
		urb->dev = udev;

		if (vary) {
			int	len = urb->transfer_buffer_length;

			len += vary;
			len %= max;
			if (len == 0)
				len = (vary < max) ? vary : max;
			urb->transfer_buffer_length = len;
		}

		/* FIXME if endpoint halted, clear halt (and log) */
	}
	urb->transfer_buffer_length = max;

	// FIXME for unlink or fault handling tests, don't report
	// failure if retval is as we expected ...
	if (retval)
		dbg ("simple_io failed, iterations left %d, status %d",
				iterations, retval);
	return retval;
}

/*-------------------------------------------------------------------------*/

/* We use scatterlist primitives to test queued I/O.
 * Yes, this also tests the scatterlist primitives.
 */

static void free_sglist (struct scatterlist *sg, int nents)
{
	unsigned		i;
	
	if (!sg)
		return;
	for (i = 0; i < nents; i++) {
		if (!sg [i].page)
			continue;
		kfree (page_address (sg [i].page) + sg [i].offset);
	}
	kfree (sg);
}

static struct scatterlist *
alloc_sglist (int nents, int max, int vary)
{
	struct scatterlist	*sg;
	unsigned		i;
	unsigned		size = max;

	sg = kmalloc (nents * sizeof *sg, SLAB_KERNEL);
	if (!sg)
		return 0;
	memset (sg, 0, nents * sizeof *sg);

	for (i = 0; i < nents; i++) {
		char		*buf;

		buf = kmalloc (size, SLAB_KERNEL);
		if (!buf) {
			free_sglist (sg, i);
			return 0;
		}
		memset (buf, 0, size);

		/* kmalloc pages are always physically contiguous! */
		sg [i].page = virt_to_page (buf);
		sg [i].offset = offset_in_page (buf);
		sg [i].length = size;

		if (vary) {
			size += vary;
			size %= max;
			if (size == 0)
				size = (vary < max) ? vary : max;
		}
	}

	return sg;
}

static int perform_sglist (
	struct usb_device	*udev,
	unsigned		iterations,
	int			pipe,
	struct usb_sg_request	*req,
	struct scatterlist	*sg,
	int			nents
)
{
	int			retval = 0;

	while (retval == 0 && iterations-- > 0) {
		retval = usb_sg_init (req, udev, pipe,
				(udev->speed == USB_SPEED_HIGH)
					? (INTERRUPT_RATE << 3)
					: INTERRUPT_RATE,
				sg, nents, 0, SLAB_KERNEL);
		
		if (retval)
			break;
		usb_sg_wait (req);
		retval = req->status;

		/* FIXME if endpoint halted, clear halt (and log) */
	}

	// FIXME for unlink or fault handling tests, don't report
	// failure if retval is as we expected ...

	if (retval)
		dbg ("perform_sglist failed, iterations left %d, status %d",
				iterations, retval);
	return retval;
}


/*-------------------------------------------------------------------------*/

/* unqueued control message testing
 *
 * there's a nice set of device functional requirements in chapter 9 of the
 * usb 2.0 spec, which we can apply to ANY device, even ones that don't use
 * special test firmware.
 *
 * we know the device is configured (or suspended) by the time it's visible
 * through usbfs.  we can't change that, so we won't test enumeration (which
 * worked 'well enough' to get here, this time), power management (ditto),
 * or remote wakeup (which needs human interaction).
 */

static int realworld = 1;
MODULE_PARM (realworld, "i");
MODULE_PARM_DESC (realworld, "clear to demand stricter ch9 compliance");

static int get_altsetting (struct usbtest_dev *dev)
{
	struct usb_interface	*iface = dev->intf;
	struct usb_device	*udev = interface_to_usbdev (iface);
	int			retval;

	retval = usb_control_msg (udev, usb_rcvctrlpipe (udev, 0),
			USB_REQ_GET_INTERFACE, USB_DIR_IN|USB_RECIP_INTERFACE,
			0, iface->altsetting [0].desc.bInterfaceNumber,
			dev->buf, 1, HZ * USB_CTRL_GET_TIMEOUT);
	switch (retval) {
	case 1:
		return dev->buf [0];
	case 0:
		retval = -ERANGE;
		// FALLTHROUGH
	default:
		return retval;
	}
}

/* this is usb_set_interface(), with no 'only one altsetting' case */
static int set_altsetting (struct usbtest_dev *dev, int alternate)
{
	struct usb_interface		*iface = dev->intf;
	struct usb_device		*udev;
	struct usb_host_interface	*iface_as;
	int				i, ret;

	if (alternate < 0 || alternate >= iface->num_altsetting)
		return -EINVAL;

	udev = interface_to_usbdev (iface);
	if ((ret = usb_control_msg (udev, usb_sndctrlpipe (udev, 0),
			USB_REQ_SET_INTERFACE, USB_RECIP_INTERFACE,
			alternate,
			iface->altsetting->desc.bInterfaceNumber,
			NULL, 0, HZ * USB_CTRL_SET_TIMEOUT)) < 0)
		return ret;

	// FIXME usbcore should be more like this:
	// - remove that special casing in usbcore.
	// - fix usbcore signature to take interface

	/* prevent requests using previous endpoint settings */
	iface_as = iface->altsetting + iface->act_altsetting;
	for (i = 0; i < iface_as->desc.bNumEndpoints; i++) {
		u8	ep = iface_as->endpoint [i].desc.bEndpointAddress;
		int	out = !(ep & USB_DIR_IN);

		ep &= USB_ENDPOINT_NUMBER_MASK;
		(out ? udev->epmaxpacketout : udev->epmaxpacketin ) [ep] = 0;
		// FIXME want hcd hook here, "forget this endpoint"
	}
	iface->act_altsetting = alternate;

	/* reset toggles and maxpacket for all endpoints affected */
	iface_as = iface->altsetting + iface->act_altsetting;
	for (i = 0; i < iface_as->desc.bNumEndpoints; i++) {
		u8	ep = iface_as->endpoint [i].desc.bEndpointAddress;
		int	out = !(ep & USB_DIR_IN);

		ep &= USB_ENDPOINT_NUMBER_MASK;
		usb_settoggle (udev, ep, out, 0);
		(out ? udev->epmaxpacketout : udev->epmaxpacketin ) [ep]
			= iface_as->endpoint [i].desc.wMaxPacketSize;
	}

	return 0;
}

static int is_good_config (char *buf, int len)
{
	struct usb_config_descriptor	*config;
	
	if (len < sizeof *config)
		return 0;
	config = (struct usb_config_descriptor *) buf;

	switch (config->bDescriptorType) {
	case USB_DT_CONFIG:
	case USB_DT_OTHER_SPEED_CONFIG:
		if (config->bLength != 9)
			return 0;
		/* this bit 'must be 1' but often isn't */
		if (!realworld && !(config->bmAttributes & 0x80)) {
			dbg ("high bit of config attributes not set");
			return 0;
		}
		if (config->bmAttributes & 0x1f)	/* reserved == 0 */
			return 0;
		break;
	default:
		return 0;
	}

	le16_to_cpus (&config->wTotalLength);
	if (config->wTotalLength == len)		/* read it all */
		return 1;
	return config->wTotalLength >= TBUF_SIZE;	/* max partial read */
}

/* sanity test for standard requests working with usb_control_mesg() and some
 * of the utility functions which use it.
 *
 * this doesn't test how endpoint halts behave or data toggles get set, since
 * we won't do I/O to bulk/interrupt endpoints here (which is how to change
 * halt or toggle).  toggle testing is impractical without support from hcds.
 *
 * this avoids failing devices linux would normally work with, by not testing
 * config/altsetting operations for devices that only support their defaults.
 * such devices rarely support those needless operations.
 *
 * NOTE that since this is a sanity test, it's not examining boundary cases
 * to see if usbcore, hcd, and device all behave right.  such testing would
 * involve varied read sizes and other operation sequences.
 */
static int ch9_postconfig (struct usbtest_dev *dev)
{
	struct usb_interface	*iface = dev->intf;
	struct usb_device	*udev = interface_to_usbdev (iface);
	int			i, retval;

	/* [9.2.3] if there's more than one altsetting, we need to be able to
	 * set and get each one.  mostly trusts the descriptors from usbcore.
	 */
	for (i = 0; i < iface->num_altsetting; i++) {

		/* 9.2.3 constrains the range here, and Linux ensures
		 * they're ordered meaningfully in this array
		 */
		if (iface->altsetting [i].desc.bAlternateSetting != i) {
			dbg ("%s, invalid alt [%d].bAltSetting = %d",
					dev->id, i, 
					iface->altsetting [i].desc
						.bAlternateSetting);
			return -EDOM;
		}

		/* [real world] get/set unimplemented if there's only one */
		if (realworld && iface->num_altsetting == 1)
			continue;

		/* [9.4.10] set_interface */
		retval = set_altsetting (dev, i);
		if (retval) {
			dbg ("%s can't set_interface = %d, %d",
					dev->id, i, retval);
			return retval;
		}

		/* [9.4.4] get_interface always works */
		retval = get_altsetting (dev);
		if (retval != i) {
			dbg ("%s get alt should be %d, was %d",
					dev->id, i, retval);
			return (retval < 0) ? retval : -EDOM;
		}

	}

	/* [real world] get_config unimplemented if there's only one */
	if (!realworld || udev->descriptor.bNumConfigurations != 1) {
		int	expected = udev->actconfig->desc.bConfigurationValue;

		/* [9.4.2] get_configuration always works
		 * ... although some cheap devices (like one TI Hub I've got)
		 * won't return config descriptors except before set_config.
		 */
		retval = usb_control_msg (udev, usb_rcvctrlpipe (udev, 0),
				USB_REQ_GET_CONFIGURATION,
				USB_DIR_IN | USB_RECIP_DEVICE,
				0, 0, dev->buf, 1, HZ * USB_CTRL_GET_TIMEOUT);
		if (retval != 1 || dev->buf [0] != expected) {
			dbg ("%s get config --> %d (%d)", dev->id, retval,
				expected);
			return (retval < 0) ? retval : -EDOM;
		}
	}

	/* there's always [9.4.3] a device descriptor [9.6.1] */
	retval = usb_get_descriptor (udev, USB_DT_DEVICE, 0,
			dev->buf, sizeof udev->descriptor);
	if (retval != sizeof udev->descriptor) {
		dbg ("%s dev descriptor --> %d", dev->id, retval);
		return (retval < 0) ? retval : -EDOM;
	}

	/* there's always [9.4.3] at least one config descriptor [9.6.3] */
	for (i = 0; i < udev->descriptor.bNumConfigurations; i++) {
		retval = usb_get_descriptor (udev, USB_DT_CONFIG, i,
				dev->buf, TBUF_SIZE);
		if (!is_good_config (dev->buf, retval)) {
			dbg ("%s config [%d] descriptor --> %d",
					dev->id, i, retval);
			return (retval < 0) ? retval : -EDOM;
		}

		// FIXME cross-checking udev->config[i] to make sure usbcore
		// parsed it right (etc) would be good testing paranoia
	}

	/* and sometimes [9.2.6.6] speed dependent descriptors */
	if (udev->descriptor.bcdUSB == 0x0200) {	/* pre-swapped */
		struct usb_qualifier_descriptor		*d = 0;

		/* device qualifier [9.6.2] */
		retval = usb_get_descriptor (udev,
				USB_DT_DEVICE_QUALIFIER, 0, dev->buf,
				sizeof (struct usb_qualifier_descriptor));
		if (retval == -EPIPE) {
			if (udev->speed == USB_SPEED_HIGH) {
				dbg ("%s hs dev qualifier --> %d",
						dev->id, retval);
				return (retval < 0) ? retval : -EDOM;
			}
			/* usb2.0 but not high-speed capable; fine */
		} else if (retval != sizeof (struct usb_qualifier_descriptor)) {
			dbg ("%s dev qualifier --> %d", dev->id, retval);
			return (retval < 0) ? retval : -EDOM;
		} else
			d = (struct usb_qualifier_descriptor *) dev->buf;

		/* might not have [9.6.2] any other-speed configs [9.6.4] */
		if (d) {
			unsigned max = d->bNumConfigurations;
			for (i = 0; i < max; i++) {
				retval = usb_get_descriptor (udev,
					USB_DT_OTHER_SPEED_CONFIG, i,
					dev->buf, TBUF_SIZE);
				if (!is_good_config (dev->buf, retval)) {
					dbg ("%s other speed config --> %d",
							dev->id, retval);
					return (retval < 0) ? retval : -EDOM;
				}
			}
		}
	}
	// FIXME fetch strings from at least the device descriptor

	/* [9.4.5] get_status always works */
	retval = usb_get_status (udev, USB_RECIP_DEVICE, 0, dev->buf);
	if (retval != 2) {
		dbg ("%s get dev status --> %d", dev->id, retval);
		return (retval < 0) ? retval : -EDOM;
	}

	// FIXME configuration.bmAttributes says if we could try to set/clear
	// the device's remote wakeup feature ... if we can, test that here

	retval = usb_get_status (udev, USB_RECIP_INTERFACE,
			iface->altsetting [0].desc.bInterfaceNumber, dev->buf);
	if (retval != 2) {
		dbg ("%s get interface status --> %d", dev->id, retval);
		return (retval < 0) ? retval : -EDOM;
	}
	// FIXME get status for each endpoint in the interface
	
	return 0;
}

/*-------------------------------------------------------------------------*/

/* use ch9 requests to test whether:
 *   (a) queues work for control, keeping N subtests queued and
 *       active (auto-resubmit) for M loops through the queue.
 *   (b) protocol stalls (control-only) will autorecover.
 *       it's quite not like bulk/intr; no halt clearing.
 *   (c) short control reads are reported and handled.
 *   (d) queues are always processed in-order
 */

struct ctrl_ctx {
	spinlock_t		lock;
	struct usbtest_dev	*dev;
	struct completion	complete;
	unsigned		count;
	unsigned		pending;
	int			status;
	struct urb		**urb;
	struct usbtest_param	*param;
	int			last;
};

#define NUM_SUBCASES	13		/* how many test subcases here? */

struct subcase {
	struct usb_ctrlrequest	setup;
	int			number;
	int			expected;
};

static void ctrl_complete (struct urb *urb, struct pt_regs *regs)
{
	struct ctrl_ctx		*ctx = urb->context;
	struct usb_ctrlrequest	*reqp;
	struct subcase		*subcase;
	int			status = urb->status;

	reqp = (struct usb_ctrlrequest *)urb->setup_packet;
	subcase = container_of (reqp, struct subcase, setup);

	spin_lock (&ctx->lock);
	ctx->count--;
	ctx->pending--;

	/* queue must transfer and complete in fifo order, unless
	 * usb_unlink_urb() is used to unlink something not at the
	 * physical queue head (not tested).
	 */
	if (subcase->number > 0) {
		if ((subcase->number - ctx->last) != 1) {
			dbg ("subcase %d completed out of order, last %d",
					subcase->number, ctx->last);
			status = -EDOM;
			goto error;
		}
	}
	ctx->last = subcase->number;

	/* succeed or fault in only one way? */
	if (status == subcase->expected)
		status = 0;

	/* async unlink for cleanup? */
	else if (status != -ECONNRESET) {

		/* some faults are allowed, not required */
		if (subcase->expected > 0 && (
			  ((urb->status == -subcase->expected	/* happened */
			   || urb->status == 0))))		/* didn't */
			status = 0;
		/* sometimes more than one fault is allowed */
		else if (subcase->number == 12 && status == -EPIPE)
			status = 0;
		else
			dbg ("subtest %d error, status %d",
					subcase->number, status);
	}

	/* unexpected status codes mean errors; ideally, in hardware */
	if (status) {
error:
		if (ctx->status == 0) {
			int		i;

			ctx->status = status;
			info ("control queue %02x.%02x, err %d, %d left",
					reqp->bRequestType, reqp->bRequest,
					status, ctx->count);

			/* FIXME this "unlink everything" exit route should
			 * be a separate test case.
			 */

			/* unlink whatever's still pending */
			for (i = 1; i < ctx->param->sglen; i++) {
				struct urb	*u = ctx->urb [
	(i + subcase->number) % ctx->param->sglen];

				if (u == urb || !u->dev)
					continue;
				status = usb_unlink_urb (u);
				switch (status) {
				case -EINPROGRESS:
				case -EBUSY:
					continue;
				default:
					dbg ("urb unlink --> %d", status);
				}
			}
			status = ctx->status;
		}
	}

	/* resubmit if we need to, else mark this as done */
	if ((status == 0) && (ctx->pending < ctx->count)) {
		if ((status = usb_submit_urb (urb, SLAB_ATOMIC)) != 0) {
			dbg ("can't resubmit ctrl %02x.%02x, err %d",
				reqp->bRequestType, reqp->bRequest, status);
			urb->dev = 0;
		} else
			ctx->pending++;
	} else
		urb->dev = 0;
	
	/* signal completion when nothing's queued */
	if (ctx->pending == 0)
		complete (&ctx->complete);
	spin_unlock (&ctx->lock);
}

static int
test_ctrl_queue (struct usbtest_dev *dev, struct usbtest_param *param)
{
	struct usb_device	*udev = testdev_to_usbdev (dev);
	struct urb		**urb;
	struct ctrl_ctx		context;
	int			i;

	spin_lock_init (&context.lock);
	context.dev = dev;
	init_completion (&context.complete);
	context.count = param->sglen * param->iterations;
	context.pending = 0;
	context.status = -ENOMEM;
	context.param = param;
	context.last = -1;

	/* allocate and init the urbs we'll queue.
	 * as with bulk/intr sglists, sglen is the queue depth; it also
	 * controls which subtests run (more tests than sglen) or rerun.
	 */
	urb = kmalloc (param->sglen * sizeof (struct urb *), SLAB_KERNEL);
	if (!urb)
		goto cleanup;
	memset (urb, 0, param->sglen * sizeof (struct urb *));
	for (i = 0; i < param->sglen; i++) {
		int			pipe = usb_rcvctrlpipe (udev, 0);
		unsigned		len;
		struct urb		*u;
		struct usb_ctrlrequest	req;
		struct subcase		*reqp;
		int			expected = 0;

		/* requests here are mostly expected to succeed on any
		 * device, but some are chosen to trigger protocol stalls
		 * or short reads.
		 */
		memset (&req, 0, sizeof req);
		req.bRequest = USB_REQ_GET_DESCRIPTOR;
		req.bRequestType = USB_DIR_IN|USB_RECIP_DEVICE;

		switch (i % NUM_SUBCASES) {
		case 0:		// get device descriptor
			req.wValue = cpu_to_le16 (USB_DT_DEVICE << 8);
			len = sizeof (struct usb_device_descriptor);
			break;
		case 1:		// get first config descriptor (only)
			req.wValue = cpu_to_le16 ((USB_DT_CONFIG << 8) | 0);
			len = sizeof (struct usb_config_descriptor);
			break;
		case 2:		// get altsetting (OFTEN STALLS)
			req.bRequest = USB_REQ_GET_INTERFACE;
			req.bRequestType = USB_DIR_IN|USB_RECIP_INTERFACE;
			// index = 0 means first interface
			len = 1;
			expected = EPIPE;
			break;
		case 3:		// get interface status
			req.bRequest = USB_REQ_GET_STATUS;
			req.bRequestType = USB_DIR_IN|USB_RECIP_INTERFACE;
			// interface 0
			len = 2;
			break;
		case 4:		// get device status
			req.bRequest = USB_REQ_GET_STATUS;
			req.bRequestType = USB_DIR_IN|USB_RECIP_DEVICE;
			len = 2;
			break;
		case 5:		// get device qualifier (MAY STALL)
			req.wValue = cpu_to_le16 (USB_DT_DEVICE_QUALIFIER << 8);
			len = sizeof (struct usb_qualifier_descriptor);
			if (udev->speed != USB_SPEED_HIGH)
				expected = EPIPE;
			break;
		case 6:		// get first config descriptor, plus interface
			req.wValue = cpu_to_le16 ((USB_DT_CONFIG << 8) | 0);
			len = sizeof (struct usb_config_descriptor);
			len += sizeof (struct usb_interface_descriptor);
			break;
		case 7:		// get interface descriptor (ALWAYS STALLS)
			req.wValue = cpu_to_le16 (USB_DT_INTERFACE << 8);
			// interface == 0
			len = sizeof (struct usb_interface_descriptor);
			expected = -EPIPE;
			break;
		// NOTE: two consecutive stalls in the queue here.
		// that tests fault recovery a bit more aggressively.
		case 8:		// clear endpoint halt (USUALLY STALLS)
			req.bRequest = USB_REQ_CLEAR_FEATURE;
			req.bRequestType = USB_RECIP_ENDPOINT;
			// wValue 0 == ep halt
			// wIndex 0 == ep0 (shouldn't halt!)
			len = 0;
			pipe = usb_sndctrlpipe (udev, 0);
			expected = EPIPE;
			break;
		case 9:		// get endpoint status
			req.bRequest = USB_REQ_GET_STATUS;
			req.bRequestType = USB_DIR_IN|USB_RECIP_ENDPOINT;
			// endpoint 0
			len = 2;
			break;
		case 10:	// trigger short read (EREMOTEIO)
			req.wValue = cpu_to_le16 ((USB_DT_CONFIG << 8) | 0);
			len = 1024;
			expected = -EREMOTEIO;
			break;
		// NOTE: two consecutive _different_ faults in the queue.
		case 11:	// get endpoint descriptor (ALWAYS STALLS)
			req.wValue = cpu_to_le16 (USB_DT_ENDPOINT << 8);
			// endpoint == 0
			len = sizeof (struct usb_interface_descriptor);
			expected = -EPIPE;
			break;
		// NOTE: sometimes even a third fault in the queue!
		case 12:	// get string 0 descriptor (MAY STALL)
			req.wValue = cpu_to_le16 (USB_DT_STRING << 8);
			// string == 0, for language IDs
			len = sizeof (struct usb_interface_descriptor);
			expected = EREMOTEIO;	// or EPIPE, if no strings
			break;
		default:
			err ("bogus number of ctrl queue testcases!");
			context.status = -EINVAL;
			goto cleanup;
		}
		req.wLength = cpu_to_le16 (len);
		urb [i] = u = simple_alloc_urb (udev, pipe, len);
		if (!u)
			goto cleanup;

		reqp = usb_buffer_alloc (udev, sizeof *reqp, SLAB_KERNEL,
				&u->setup_dma);
		if (!reqp)
			goto cleanup;
		reqp->setup = req;
		reqp->number = i % NUM_SUBCASES;
		reqp->expected = expected;
		u->setup_packet = (char *) &reqp->setup;

		u->context = &context;
		u->complete = ctrl_complete;
		u->transfer_flags |= URB_ASYNC_UNLINK;
	}

	/* queue the urbs */
	context.urb = urb;
	spin_lock_irq (&context.lock);
	for (i = 0; i < param->sglen; i++) {
		context.status = usb_submit_urb (urb [i], SLAB_ATOMIC);
		if (context.status != 0) {
			dbg ("can't submit urb[%d], status %d",
					i, context.status);
			context.count = context.pending;
			break;
		}
		context.pending++;
	}
	spin_unlock_irq (&context.lock);

	/* FIXME  set timer and time out; provide a disconnect hook */

	/* wait for the last one to complete */
	if (context.pending > 0)
		wait_for_completion (&context.complete);

cleanup:
	for (i = 0; i < param->sglen; i++) {
		if (!urb [i])
			continue;
		urb [i]->dev = udev;
		if (urb [i]->setup_packet)
			usb_buffer_free (udev, sizeof (struct usb_ctrlrequest),
					urb [i]->setup_packet,
					urb [i]->setup_dma);
		simple_free_urb (urb [i]);
	}
	kfree (urb);
	return context.status;
}
#undef NUM_SUBCASES


/*-------------------------------------------------------------------------*/

static void unlink1_callback (struct urb *urb, struct pt_regs *regs)
{
	int	status = urb->status;

	// we "know" -EPIPE (stall) never happens
	if (!status)
		status = usb_submit_urb (urb, SLAB_ATOMIC);
	if (status) {
		if (status == -ECONNRESET || status == -ENOENT)
			status = 0;
		urb->status = status;
		complete ((struct completion *) urb->context);
	}
}

static int unlink1 (struct usbtest_dev *dev, int pipe, int size, int async)
{
	struct urb		*urb;
	struct completion	completion;
	int			retval = 0;

	init_completion (&completion);
	urb = simple_alloc_urb (testdev_to_usbdev (dev), pipe, size);
	if (!urb)
		return -ENOMEM;
	if (async)
		urb->transfer_flags |= URB_ASYNC_UNLINK;
	urb->context = &completion;
	urb->complete = unlink1_callback;

	/* keep the endpoint busy.  there are lots of hc/hcd-internal
	 * states, and testing should get to all of them over time.
	 *
	 * FIXME want additional tests for when endpoint is STALLing
	 * due to errors, or is just NAKing requests.
	 */
	if ((retval = usb_submit_urb (urb, SLAB_KERNEL)) != 0) {
		dbg ("submit/unlink fail %d", retval);
		return retval;
	}

	/* unlinking that should always work.  variable delay tests more
	 * hcd states and code paths, even with little other system load.
	 */
	wait_ms (jiffies % (2 * INTERRUPT_RATE));
retry:
	retval = usb_unlink_urb (urb);
	if (retval == -EBUSY) {
		/* we can't unlink urbs while they're completing.
		 * "normal" drivers would prevent resubmission, but
		 * since we're testing unlink paths, we can't.
		 */
		dbg ("unlink retry");
		goto retry;
	}
	if (!(retval == 0 || retval == -EINPROGRESS)) {
		dbg ("submit/unlink fail %d", retval);
		return retval;
	}

	wait_for_completion (&completion);
	retval = urb->status;
	simple_free_urb (urb);
	return retval;
}

static int unlink_simple (struct usbtest_dev *dev, int pipe, int len)
{
	int			retval = 0;

	/* test sync and async paths */
	retval = unlink1 (dev, pipe, len, 1);
	if (!retval)
		retval = unlink1 (dev, pipe, len, 0);
	return retval;
}

/*-------------------------------------------------------------------------*/

/* We only have this one interface to user space, through usbfs.
 * User mode code can scan usbfs to find N different devices (maybe on
 * different busses) to use when testing, and allocate one thread per
 * test.  So discovery is simplified, and we have no device naming issues.
 *
 * Don't use these only as stress/load tests.  Use them along with with
 * other USB bus activity:  plugging, unplugging, mousing, mp3 playback,
 * video capture, and so on.  Run different tests at different times, in
 * different sequences.  Nothing here should interact with other devices,
 * except indirectly by consuming USB bandwidth and CPU resources for test
 * threads and request completion.  But the only way to know that for sure
 * is to test when HC queues are in use by many devices.
 */

static int
usbtest_ioctl (struct usb_interface *intf, unsigned int code, void *buf)
{
	struct usbtest_dev	*dev = usb_get_intfdata (intf);
	struct usb_device	*udev = testdev_to_usbdev (dev);
	struct usbtest_param	*param = buf;
	int			retval = -EOPNOTSUPP;
	struct urb		*urb;
	struct scatterlist	*sg;
	struct usb_sg_request	req;
	struct timeval		start;
	unsigned		i;

	// FIXME USBDEVFS_CONNECTINFO doesn't say how fast the device is.

	if (code != USBTEST_REQUEST)
		return -EOPNOTSUPP;

	if (param->iterations <= 0 || param->length < 0
			|| param->sglen < 0 || param->vary < 0)
		return -EINVAL;

	if (down_interruptible (&dev->sem))
		return -ERESTARTSYS;

	/* some devices, like ez-usb default devices, need a non-default
	 * altsetting to have any active endpoints.  some tests change
	 * altsettings; force a default so most tests don't need to check.
	 */
	if (dev->info->alt >= 0) {
	    	int	res;

		if (intf->altsetting->desc.bInterfaceNumber) {
			up (&dev->sem);
			return -ENODEV;
		}
		res = set_altsetting (dev, dev->info->alt);
		if (res) {
			err ("%s: set altsetting to %d failed, %d",
					dev->id, dev->info->alt, res);
			up (&dev->sem);
			return res;
		}
	}

	/*
	 * Just a bunch of test cases that every HCD is expected to handle.
	 *
	 * Some may need specific firmware, though it'd be good to have
	 * one firmware image to handle all the test cases.
	 *
	 * FIXME add more tests!  cancel requests, verify the data, control
	 * queueing, concurrent read+write threads, and so on.
	 */
	do_gettimeofday (&start);
	switch (param->test_num) {

	case 0:
		dbg ("%s TEST 0:  NOP", dev->id);
		retval = 0;
		break;

	/* Simple non-queued bulk I/O tests */
	case 1:
		if (dev->out_pipe == 0)
			break;
		dbg ("%s TEST 1:  write %d bytes %u times", dev->id,
				param->length, param->iterations);
		urb = simple_alloc_urb (udev, dev->out_pipe, param->length);
		if (!urb) {
			retval = -ENOMEM;
			break;
		}
		// FIRMWARE:  bulk sink (maybe accepts short writes)
		retval = simple_io (urb, param->iterations, 0);
		simple_free_urb (urb);
		break;
	case 2:
		if (dev->in_pipe == 0)
			break;
		dbg ("%s TEST 2:  read %d bytes %u times", dev->id,
				param->length, param->iterations);
		urb = simple_alloc_urb (udev, dev->in_pipe, param->length);
		if (!urb) {
			retval = -ENOMEM;
			break;
		}
		// FIRMWARE:  bulk source (maybe generates short writes)
		retval = simple_io (urb, param->iterations, 0);
		simple_free_urb (urb);
		break;
	case 3:
		if (dev->out_pipe == 0 || param->vary == 0)
			break;
		dbg ("%s TEST 3:  write/%d 0..%d bytes %u times", dev->id,
				param->vary, param->length, param->iterations);
		urb = simple_alloc_urb (udev, dev->out_pipe, param->length);
		if (!urb) {
			retval = -ENOMEM;
			break;
		}
		// FIRMWARE:  bulk sink (maybe accepts short writes)
		retval = simple_io (urb, param->iterations, param->vary);
		simple_free_urb (urb);
		break;
	case 4:
		if (dev->in_pipe == 0 || param->vary == 0)
			break;
		dbg ("%s TEST 4:  read/%d 0..%d bytes %u times", dev->id,
				param->vary, param->length, param->iterations);
		urb = simple_alloc_urb (udev, dev->in_pipe, param->length);
		if (!urb) {
			retval = -ENOMEM;
			break;
		}
		// FIRMWARE:  bulk source (maybe generates short writes)
		retval = simple_io (urb, param->iterations, param->vary);
		simple_free_urb (urb);
		break;

	/* Queued bulk I/O tests */
	case 5:
		if (dev->out_pipe == 0 || param->sglen == 0)
			break;
		dbg ("%s TEST 5:  write %d sglists, %d entries of %d bytes",
				dev->id, param->iterations,
				param->sglen, param->length);
		sg = alloc_sglist (param->sglen, param->length, 0);
		if (!sg) {
			retval = -ENOMEM;
			break;
		}
		// FIRMWARE:  bulk sink (maybe accepts short writes)
		retval = perform_sglist (udev, param->iterations, dev->out_pipe,
				&req, sg, param->sglen);
		free_sglist (sg, param->sglen);
		break;

	case 6:
		if (dev->in_pipe == 0 || param->sglen == 0)
			break;
		dbg ("%s TEST 6:  read %d sglists, %d entries of %d bytes",
				dev->id, param->iterations,
				param->sglen, param->length);
		sg = alloc_sglist (param->sglen, param->length, 0);
		if (!sg) {
			retval = -ENOMEM;
			break;
		}
		// FIRMWARE:  bulk source (maybe generates short writes)
		retval = perform_sglist (udev, param->iterations, dev->in_pipe,
				&req, sg, param->sglen);
		free_sglist (sg, param->sglen);
		break;
	case 7:
		if (dev->out_pipe == 0 || param->sglen == 0 || param->vary == 0)
			break;
		dbg ("%s TEST 7:  write/%d %d sglists, %d entries 0..%d bytes",
				dev->id, param->vary, param->iterations,
				param->sglen, param->length);
		sg = alloc_sglist (param->sglen, param->length, param->vary);
		if (!sg) {
			retval = -ENOMEM;
			break;
		}
		// FIRMWARE:  bulk sink (maybe accepts short writes)
		retval = perform_sglist (udev, param->iterations, dev->out_pipe,
				&req, sg, param->sglen);
		free_sglist (sg, param->sglen);
		break;
	case 8:
		if (dev->in_pipe == 0 || param->sglen == 0 || param->vary == 0)
			break;
		dbg ("%s TEST 8:  read/%d %d sglists, %d entries 0..%d bytes",
				dev->id, param->vary, param->iterations,
				param->sglen, param->length);
		sg = alloc_sglist (param->sglen, param->length, param->vary);
		if (!sg) {
			retval = -ENOMEM;
			break;
		}
		// FIRMWARE:  bulk source (maybe generates short writes)
		retval = perform_sglist (udev, param->iterations, dev->in_pipe,
				&req, sg, param->sglen);
		free_sglist (sg, param->sglen);
		break;

	/* non-queued sanity tests for control (chapter 9 subset) */
	case 9:
		retval = 0;
		dbg ("%s TEST 9:  ch9 (subset) control tests, %d times",
				dev->id, param->iterations);
		for (i = param->iterations; retval == 0 && i--; /* NOP */)
			retval = ch9_postconfig (dev);
		if (retval)
			dbg ("ch9 subset failed, iterations left %d", i);
		break;

	/* queued control messaging */
	case 10:
		if (param->sglen == 0)
			break;
		retval = 0;
		dbg ("%s TEST 10:  queue %d control calls, %d times",
				dev->id, param->sglen,
				param->iterations);
		retval = test_ctrl_queue (dev, param);
		break;

	/* simple non-queued unlinks (ring with one urb) */
	case 11:
		if (dev->in_pipe == 0 || !param->length)
			break;
		retval = 0;
		dbg ("%s TEST 11:  unlink %d reads of %d",
				dev->id, param->iterations, param->length);
		for (i = param->iterations; retval == 0 && i--; /* NOP */)
			retval = unlink_simple (dev, dev->in_pipe, param->length);
		if (retval)
			dbg ("unlink reads failed, iterations left %d", i);
		break;
	case 12:
		if (dev->out_pipe == 0 || !param->length)
			break;
		retval = 0;
		dbg ("%s TEST 12:  unlink %d writes of %d",
				dev->id, param->iterations, param->length);
		for (i = param->iterations; retval == 0 && i--; /* NOP */)
			retval = unlink_simple (dev, dev->out_pipe, param->length);
		if (retval)
			dbg ("unlink writes failed, iterations left %d", i);
		break;

	// FIXME unlink from queue (ring with N urbs)

	// FIXME scatterlist cancel (needs helper thread)

	}
	do_gettimeofday (&param->duration);
	param->duration.tv_sec -= start.tv_sec;
	param->duration.tv_usec -= start.tv_usec;
	if (param->duration.tv_usec < 0) {
		param->duration.tv_usec += 1000 * 1000;
		param->duration.tv_sec -= 1;
	}
	up (&dev->sem);
	return retval;
}

/*-------------------------------------------------------------------------*/

static int force_interrupt = 0;
MODULE_PARM (force_interrupt, "i");
MODULE_PARM_DESC (force_interrupt, "0 = test bulk (default), else interrupt");

#ifdef	GENERIC
static int vendor;
MODULE_PARM (vendor, "h");
MODULE_PARM_DESC (vendor, "vendor code (from usb-if)");

static int product;
MODULE_PARM (product, "h");
MODULE_PARM_DESC (product, "product code (from vendor)");
#endif

static int
usbtest_probe (struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device	*udev;
	struct usbtest_dev	*dev;
	struct usbtest_info	*info;
	char			*rtest, *wtest;

	udev = interface_to_usbdev (intf);

#ifdef	GENERIC
	/* specify devices by module parameters? */
	if (id->match_flags == 0) {
		/* vendor match required, product match optional */
		if (!vendor || udev->descriptor.idVendor != (u16)vendor)
			return -ENODEV;
		if (product && udev->descriptor.idProduct != (u16)product)
			return -ENODEV;
		dbg ("matched module params, vend=0x%04x prod=0x%04x",
				udev->descriptor.idVendor,
				udev->descriptor.idProduct);
	}
#endif

	dev = kmalloc (sizeof *dev, SLAB_KERNEL);
	if (!dev)
		return -ENOMEM;
	memset (dev, 0, sizeof *dev);
	info = (struct usbtest_info *) id->driver_info;
	dev->info = info;
	init_MUTEX (&dev->sem);

	/* use the same kind of id the hid driver shows */
	snprintf (dev->id, sizeof dev->id, "%s-%s:%d",
			udev->bus->bus_name, udev->devpath,
			intf->altsetting [0].desc.bInterfaceNumber);
	dev->intf = intf;

	/* cacheline-aligned scratch for i/o */
	if ((dev->buf = kmalloc (TBUF_SIZE, SLAB_KERNEL)) == 0) {
		kfree (dev);
		return -ENOMEM;
	}

	/* NOTE this doesn't yet test the handful of difference that are
	 * visible with high speed interrupts:  bigger maxpacket (1K) and
	 * "high bandwidth" modes (up to 3 packets/uframe).
	 */
	rtest = wtest = "";
	if (force_interrupt || udev->speed == USB_SPEED_LOW) {
		if (info->ep_in) {
			dev->in_pipe = usb_rcvintpipe (udev, info->ep_in);
			rtest = " intr-in";
		}
		if (info->ep_out) {
			dev->out_pipe = usb_sndintpipe (udev, info->ep_out);
			wtest = " intr-out";
		}
	} else {
		if (info->autoconf) {
			int status;

			status = get_endpoints (dev, intf);
			if (status < 0) {
				dbg ("couldn't get endpoints, %d\n", status);
				return status;
			}
		} else {
			if (info->ep_in)
				dev->in_pipe = usb_rcvbulkpipe (udev,
							info->ep_in);
			if (info->ep_out)
				dev->out_pipe = usb_sndbulkpipe (udev,
							info->ep_out);
		}
		if (dev->in_pipe)
			rtest = " bulk-in";
		if (dev->out_pipe)
			wtest = " bulk-out";
	}

	usb_set_intfdata (intf, dev);
	info ("%s at %s ... %s speed {control%s%s} tests",
			info->name, dev->id,
			({ char *tmp;
			switch (udev->speed) {
			case USB_SPEED_LOW: tmp = "low"; break;
			case USB_SPEED_FULL: tmp = "full"; break;
			case USB_SPEED_HIGH: tmp = "high"; break;
			default: tmp = "unknown"; break;
			}; tmp; }), rtest, wtest);
	return 0;
}

static void usbtest_disconnect (struct usb_interface *intf)
{
	struct usbtest_dev	*dev = usb_get_intfdata (intf);

	down (&dev->sem);

	usb_set_intfdata (intf, NULL);
	info ("unbound %s", dev->id);
	kfree (dev);
}

/* Basic testing only needs a device that can source or sink bulk traffic.
 * Any device can test control transfers (default with GENERIC binding).
 *
 * Several entries work with the default EP0 implementation that's built
 * into EZ-USB chips.  There's a default vendor ID which can be overridden
 * by (very) small config EEPROMS, but otherwise all these devices act
 * identically until firmware is loaded:  only EP0 works.  It turns out
 * to be easy to make other endpoints work, without modifying that EP0
 * behavior.  For now, we expect that kind of firmware.
 */

/* an21xx or fx versions of ez-usb */
static struct usbtest_info ez1_info = {
	.name		= "EZ-USB device",
	.ep_in		= 2,
	.ep_out		= 2,
	.alt		= 1,
};

/* fx2 version of ez-usb */
static struct usbtest_info ez2_info = {
	.name		= "FX2 device",
	.ep_in		= 6,
	.ep_out		= 2,
	.alt		= 1,
};

/* ezusb family device with dedicated usb test firmware,
 */
static struct usbtest_info fw_info = {
	.name		= "usb test device",
	.ep_in		= 2,
	.ep_out		= 2,
	.alt		= 0,
};

/* peripheral running Linux and 'zero.c' test firmware, or
 * its user-mode cousin. different versions of this use
 * different hardware with the same vendor/product codes.
 * host side MUST rely on the endpoint descriptors.
 */
static struct usbtest_info gz_info = {
	.name		= "Linux gadget zero",
	.autoconf	= 1,
	.alt		= 0,
};

static struct usbtest_info um_info = {
	.name		= "Linux user mode test driver",
	.autoconf	= 1,
	.alt		= -1,
};

#ifdef IBOT2
/* this is a nice source of high speed bulk data;
 * uses an FX2, with firmware provided in the device
 */
static struct usbtest_info ibot2_info = {
	.name		= "iBOT2 webcam",
	.ep_in		= 2,
	.alt		= -1,
};
#endif

#ifdef GENERIC
/* we can use any device to test control traffic */
static struct usbtest_info generic_info = {
	.name		= "Generic USB device",
	.alt		= -1,
};
#endif

// FIXME remove this 
static struct usbtest_info hact_info = {
	.name		= "FX2/hact",
	//.ep_in		= 6,
	.ep_out		= 2,
	.alt		= -1,
};


static struct usb_device_id id_table [] = {

	{ USB_DEVICE (0x0547, 0x1002),
		.driver_info = (unsigned long) &hact_info,
		},

	/*-------------------------------------------------------------*/

	/* EZ-USB devices which download firmware to replace (or in our
	 * case augment) the default device implementation.
	 */

	/* generic EZ-USB FX controller */
	{ USB_DEVICE (0x0547, 0x2235),
		.driver_info = (unsigned long) &ez1_info,
		},

	/* CY3671 development board with EZ-USB FX */
	{ USB_DEVICE (0x0547, 0x0080),
		.driver_info = (unsigned long) &ez1_info,
		},

	/* generic EZ-USB FX2 controller (or development board) */
	{ USB_DEVICE (0x04b4, 0x8613),
		.driver_info = (unsigned long) &ez2_info,
		},

	/* re-enumerated usb test device firmware */
	{ USB_DEVICE (0xfff0, 0xfff0),
		.driver_info = (unsigned long) &fw_info,
		},

	/* "Gadget Zero" firmware runs under Linux */
	{ USB_DEVICE (0x0525, 0xa4a0),
		.driver_info = (unsigned long) &gz_info,
		},

	/* so does a user-mode variant */
	{ USB_DEVICE (0x0525, 0xa4a4),
		.driver_info = (unsigned long) &um_info,
		},

#ifdef KEYSPAN_19Qi
	/* Keyspan 19qi uses an21xx (original EZ-USB) */
	// this does not coexist with the real Keyspan 19qi driver!
	{ USB_DEVICE (0x06cd, 0x010b),
		.driver_info = (unsigned long) &ez1_info,
		},
#endif

	/*-------------------------------------------------------------*/

#ifdef IBOT2
	/* iBOT2 makes a nice source of high speed bulk-in data */
	// this does not coexist with a real iBOT2 driver!
	{ USB_DEVICE (0x0b62, 0x0059),
		.driver_info = (unsigned long) &ibot2_info,
		},
#endif

	/*-------------------------------------------------------------*/

#ifdef GENERIC
	/* module params can specify devices to use for control tests */
	{ .driver_info = (unsigned long) &generic_info, },
#endif

	/*-------------------------------------------------------------*/

	{ }
};
MODULE_DEVICE_TABLE (usb, id_table);

static struct usb_driver usbtest_driver = {
	.owner =	THIS_MODULE,
	.name =		"usbtest",
	.id_table =	id_table,
	.probe =	usbtest_probe,
	.ioctl =	usbtest_ioctl,
	.disconnect =	usbtest_disconnect,
};

/*-------------------------------------------------------------------------*/

static int __init usbtest_init (void)
{
#ifdef GENERIC
	if (vendor)
		dbg ("params: vend=0x%04x prod=0x%04x", vendor, product);
#endif
	return usb_register (&usbtest_driver);
}
module_init (usbtest_init);

static void __exit usbtest_exit (void)
{
	usb_deregister (&usbtest_driver);
}
module_exit (usbtest_exit);

MODULE_DESCRIPTION ("USB Core/HCD Testing Driver");
MODULE_LICENSE ("GPL");

