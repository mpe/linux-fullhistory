/*
 * drivers/usb/usb.c
 *
 * (C) Copyright Linus Torvalds 1999
 * (C) Copyright Johannes Erdfelt 1999
 *
 * NOTE! This is not actually a driver at all, rather this is
 * just a collection of helper routines that implement the
 * generic USB things that the real drivers can use..
 *
 * Think of this as a "USB library" rather than anything else.
 * It should be considered a slave, with no callbacks. Callbacks
 * are evil.
 */

#ifndef EXPORT_SYMTAB
#define EXPORT_SYMTAB
#endif

#define USB_DEBUG	1

#include <linux/config.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/bitops.h>
#include <linux/malloc.h>

#include "usb.h"

static int usb_find_driver(struct usb_device *);
static void usb_check_support(struct usb_device *);
static void usb_driver_purge(struct usb_driver *, struct usb_device *);

/*
 * We have a per-interface "registered driver" list.
 */
static LIST_HEAD(usb_driver_list);
static LIST_HEAD(usb_bus_list);

static struct usb_driver *usb_minors[16];

int usb_register(struct usb_driver *new_driver)
{
	struct list_head *tmp;

	printk("usbcore: Registering new driver %s\n", new_driver->name);
	if (new_driver->fops != NULL) {
		if (usb_minors[new_driver->minor/16])
			BUG();
		usb_minors[new_driver->minor/16] = new_driver;
	}

	/* Add it to the list of known drivers */
	list_add(&new_driver->driver_list, &usb_driver_list);

	/*
	 * We go through all existing devices, and see if any of them would
	 * be acceptable to the new driver.. This is done using a depth-first
	 * search for devices without a registered driver already, then 
	 * running 'probe' with each of the drivers registered on every one 
	 * of these.
	 */
	tmp = usb_bus_list.next;
	while (tmp != &usb_bus_list) {
		struct usb_bus *bus = list_entry(tmp,struct usb_bus, bus_list);

	        tmp = tmp->next;
		usb_check_support(bus->root_hub);
	}
	return 0;
}

void usb_deregister(struct usb_driver *driver)
{
	struct list_head *tmp;

	printk("usbcore: Deregistering driver %s\n", driver->name);
	if (driver->fops != NULL)
		usb_minors[driver->minor/16] = NULL;

	/*
	 * first we remove the driver, to be sure it doesn't get used by
	 * another thread while we are stepping through removing entries
	 */
	list_del(&driver->driver_list);

	tmp = usb_bus_list.next;
	while (tmp != &usb_bus_list) {
		struct usb_bus *bus = list_entry(tmp,struct usb_bus,bus_list);

		tmp = tmp->next;
		usb_driver_purge(driver, bus->root_hub);
	}
}

/*
 * This function is part of a depth-first search down the device tree,
 * removing any instances of a device driver.
 */
static void usb_driver_purge(struct usb_driver *driver,struct usb_device *dev)
{
	int i;

	if (!dev) {
		printk(KERN_ERR "usbcore: null device being purged!!!\n");
		return;
	}

	for (i=0; i<USB_MAXCHILDREN; i++)
		if (dev->children[i])
			usb_driver_purge(driver, dev->children[i]);

	/* now we check this device */
	if (dev->driver == driver) {
		/*
		 * Note: this is not the correct way to do this, this
		 * uninitializes and reinitializes EVERY driver
		 */
		printk(KERN_INFO "disconnect driverless device %d\n",
			dev->devnum);
		dev->driver->disconnect(dev);
		dev->driver = NULL;

		/*
		 * This will go back through the list looking for a driver
		 * that can handle the device
		 */
		usb_find_driver(dev);
	}
}

/*
 * calc_bus_time:
 *
 * returns (approximate) USB bus time in nanoseconds for a USB transaction.
 */
static long calc_bus_time (int low_speed, int input_dir, int isoc, int bytecount)
{
	unsigned long	tmp;

	if (low_speed)		/* no isoc. here */
	{
		if (input_dir)
                {
		        tmp = (67667L * (31L + 10L * BitTime (bytecount))) / 1000L;
			return (64060L + (2 * BW_HUB_LS_SETUP) + BW_HOST_DELAY + tmp);
		}
		else
                {
			tmp = (66700L * (31L + 10L * BitTime (bytecount))) / 1000L;
			return (64107L + (2 * BW_HUB_LS_SETUP) + BW_HOST_DELAY + tmp);
		}
	}

	/* for full-speed: */

	if (!isoc)		/* Input or Output */
	{
		tmp = (8354L * (31L + 10L * BitTime (bytecount))) / 1000L;
		return (9107L + BW_HOST_DELAY + tmp);
	} /* end not Isoc */

	/* for isoc: */

	tmp = (8354L * (31L + 10L * BitTime (bytecount))) / 1000L;
	return (((input_dir) ? 7268L : 6265L) + BW_HOST_DELAY + tmp);
} /* end calc_bus_time */

/*
 * check_bandwidth_alloc():
 *
 * old_alloc is from host_controller->bandwidth_allocated in microseconds;
 * bustime is from calc_bus_time(), but converted to microseconds.
 *
 * returns 0 if successful,
 * -1 if bandwidth request fails.
 *
 * FIXME:
 * This initial implementation does not use Endpoint.bInterval
 * in managing bandwidth allocation.
 * It probably needs to be expanded to use Endpoint.bInterval.
 * This can be done as a later enhancement (correction).
 * This will also probably require some kind of
 * frame allocation tracking...meaning, for example,
 * that if multiple drivers request interrupts every 10 USB frames,
 * they don't all have to be allocated at
 * frame numbers N, N+10, N+20, etc.  Some of them could be at
 * N+11, N+21, N+31, etc., and others at
 * N+12, N+22, N+32, etc.
 * However, this first cut at USB bandwidth allocation does not
 * contain any frame allocation tracking.
 */
static int check_bandwidth_alloc (unsigned int old_alloc, long bustime)
{
	unsigned int	new_alloc;

	new_alloc = old_alloc + bustime;
		/* what new total allocated bus time would be */

	PRINTD ("usb-bandwidth-alloc: was: %u, new: %u, "
		"bustime = %ld us, Pipe allowed: %s",
		old_alloc, new_alloc, bustime,
		(new_alloc <= FRAME_TIME_MAX_USECS_ALLOC) ?
			"yes" : "no");

	return (new_alloc <= FRAME_TIME_MAX_USECS_ALLOC) ? 0 : -1;
} /* end check_bandwidth_alloc */

/*
 * New functions for (de)registering a controller
 */
struct usb_bus *usb_alloc_bus(struct usb_operations *op)
{
	struct usb_bus *bus;

	bus = kmalloc(sizeof(*bus), GFP_KERNEL);
	if (!bus)
		return NULL;

	memset(&bus->devmap, 0, sizeof(struct usb_devmap));

	bus->op = op;
	bus->root_hub = NULL;
	bus->hcpriv = NULL;
	bus->bandwidth_allocated = 0;
	bus->bandwidth_int_reqs  = 0;
	bus->bandwidth_isoc_reqs = 0;

	INIT_LIST_HEAD(&bus->bus_list);

	return bus;
}

void usb_free_bus(struct usb_bus *bus)
{
	if (!bus)
		return;

	kfree(bus);
}

void usb_register_bus(struct usb_bus *bus)
{
	proc_usb_add_bus(bus);

	/* Add it to the list of buses */
	list_add(&bus->bus_list, &usb_bus_list);

	printk("New USB bus registered\n");
}

void usb_deregister_bus(struct usb_bus *bus)
{
	/*
	 * NOTE: make sure that all the devices are removed by the
	 * controller code, as well as having it call this when cleaning
	 * itself up
	 */
	list_del(&bus->bus_list);

	proc_usb_remove_bus(bus);
}

/*
 * This function is for doing a depth-first search for devices which
 * have support, for dynamic loading of driver modules.
 */
static void usb_check_support(struct usb_device *dev)
{
	int i;

	if (!dev) {
		printk(KERN_ERR "usbcore: null device being checked!!!\n");
		return;
	}

	for (i=0; i<USB_MAXCHILDREN; i++)
		if (dev->children[i])
			usb_check_support(dev->children[i]);

	/* now we check this device */
	if (!dev->driver && dev->devnum > 0)
		usb_find_driver(dev);
}

/*
 * This entrypoint gets called for each new device.
 *
 * We now walk the list of registered USB drivers,
 * looking for one that will accept this device as
 * his..
 */
static int usb_find_driver(struct usb_device *dev)
{
	struct list_head *tmp = usb_driver_list.next;

	while (tmp != &usb_driver_list) {
		struct usb_driver *driver = list_entry(tmp, struct usb_driver,
						       driver_list);
		tmp = tmp->next;
		if (driver->probe(dev))
			continue;
		dev->driver = driver;
		return 1;
	}

	/*
	 * Ok, no driver accepted the device, so show the info
	 * for debugging..
	 */
	return 0;
}

/*
 * Only HC's should call usb_alloc_dev and usb_free_dev directly
 * Anybody may use usb_inc_dev_use or usb_dec_dev_use
 */
struct usb_device *usb_alloc_dev(struct usb_device *parent, struct usb_bus *bus)
{
	struct usb_device *dev;

	dev = kmalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return NULL;

	memset(dev, 0, sizeof(*dev));

	dev->bus = bus;
	dev->parent = parent;
	atomic_set(&dev->refcnt, 1);

	dev->bus->op->allocate(dev);

	return dev;
}

void usb_free_dev(struct usb_device *dev)
{
	if (atomic_dec_and_test(&dev->refcnt)) {
		usb_destroy_configuration(dev);
		dev->bus->op->deallocate(dev);
		kfree(dev);
	}
}

void usb_inc_dev_use(struct usb_device *dev)
{
	atomic_inc(&dev->refcnt);
}

static int usb_parse_endpoint(struct usb_device *dev, struct usb_endpoint_descriptor *endpoint, unsigned char *buffer, int size)
{
	struct usb_descriptor_header *header;
	int parsed = 0;

	header = (struct usb_descriptor_header *)buffer;

	/* Everything should be fine being passed into here, but we sanity */
	/*  check JIC */
	if (header->bLength > size) {
		printk(KERN_ERR "usb: ran out of descriptors parsing\n");
		return -1;
	}
		
	if (header->bDescriptorType != USB_DT_ENDPOINT) {
		printk(KERN_INFO "usb: unexpected descriptor 0x%X\n",
			endpoint->bDescriptorType);
		return parsed;
	}

	memcpy(endpoint, buffer, USB_DT_ENDPOINT_SIZE);
        le16_to_cpus(&endpoint->wMaxPacketSize);

	buffer += header->bLength;
	size -= header->bLength;
	parsed += header->bLength;

	/* Skip over the rest of the Class Specific or Vendor Specific */
	/*  descriptors */
	while (size >= sizeof(struct usb_descriptor_header)) {
		header = (struct usb_descriptor_header *)buffer;

		if (header->bLength < 2) {
			printk(KERN_ERR "usb: invalid descriptor length of %d\n", header->bLength);
			return -1;
		}

		/* If we find another descriptor which is at or below us */
		/*  in the descriptor heirarchy then return */
		if ((header->bDescriptorType == USB_DT_ENDPOINT) ||
		    (header->bDescriptorType == USB_DT_INTERFACE) ||
		    (header->bDescriptorType == USB_DT_CONFIG) ||
		    (header->bDescriptorType == USB_DT_DEVICE))
			return parsed;

		printk(KERN_INFO "usb: skipping descriptor 0x%X\n",
			header->bDescriptorType);

		buffer += header->bLength;
		size -= header->bLength;
		parsed += header->bLength;
	}

	return parsed;
}

#if 0
static int usb_parse_hid(struct usb_device *dev, struct usb_hid_descriptor *hid, unsigned char *ptr, int len)
{
	int parsed = usb_expect_descriptor(ptr, len, USB_DT_HID, ptr[0]);
	int i;

	if (parsed < 0)
		return parsed;

	memcpy(hid, ptr + parsed, ptr[parsed]);
	le16_to_cpus(&hid->bcdHID);

	for (i=0; i<hid->bNumDescriptors; i++)
		le16_to_cpus(&(hid->desc[i].wDescriptorLength));

	return parsed + ptr[parsed];
}
#endif

static int usb_parse_interface(struct usb_device *dev, struct usb_interface *interface, unsigned char *buffer, int size)
{
	int i;
	int retval, parsed = 0;
	struct usb_descriptor_header *header;
	struct usb_interface_descriptor *ifp;

	interface->act_altsetting = 0;
	interface->num_altsetting = 0;

	interface->altsetting = kmalloc(sizeof(struct usb_interface_descriptor) * USB_MAXALTSETTING, GFP_KERNEL);
	if (!interface->altsetting) {
		printk("couldn't kmalloc interface->altsetting\n");
		return -1;
	}

	while (size > 0) {
		ifp = interface->altsetting + interface->num_altsetting;
		interface->num_altsetting++;

		if (interface->num_altsetting >= USB_MAXALTSETTING) {
			printk(KERN_WARNING "usb: too many alternate settings\n");
			return -1;
		}
		memcpy(ifp, buffer, USB_DT_INTERFACE_SIZE);

		/* Skip over the interface */
		buffer += ifp->bLength;
		parsed += ifp->bLength;
		size -= ifp->bLength;

		/* Skip over at Interface class or vendor descriptors */
		while (size >= sizeof(struct usb_descriptor_header)) {
			header = (struct usb_descriptor_header *)buffer;

			if (header->bLength < 2) {
				printk(KERN_ERR "usb: invalid descriptor length of %d\n", header->bLength);
				return -1;
			}

			/* If we find another descriptor which is at or below us */
			/*  in the descriptor heirarchy then return */
			if ((header->bDescriptorType == USB_DT_INTERFACE) ||
			    (header->bDescriptorType == USB_DT_ENDPOINT))
				break;

			if ((header->bDescriptorType == USB_DT_CONFIG) ||
			    (header->bDescriptorType == USB_DT_DEVICE))
				return parsed;

			if (header->bDescriptorType == USB_DT_HID)
				printk(KERN_INFO "usb: skipping HID descriptor\n");
			else
				printk(KERN_INFO "usb: unexpected descriptor 0x%X\n",
					header->bDescriptorType);

			buffer += header->bLength;
			parsed += header->bLength;
			size -= header->bLength;
		}

		if (ifp->bNumEndpoints > USB_MAXENDPOINTS) {
			printk(KERN_WARNING "usb: too many endpoints\n");
			return -1;
		}

		ifp->endpoint = (struct usb_endpoint_descriptor *)
			kmalloc(ifp->bNumEndpoints *
			sizeof(struct usb_endpoint_descriptor), GFP_KERNEL);
		if (!ifp->endpoint) {
			printk(KERN_WARNING "usb: out of memory\n");
			return -1;	
		}

		memset(ifp->endpoint, 0, ifp->bNumEndpoints *
			sizeof(struct usb_endpoint_descriptor));
	
		for (i = 0; i < ifp->bNumEndpoints; i++) {
			header = (struct usb_descriptor_header *)buffer;

			if (header->bLength > size) {
				printk(KERN_ERR "usb: ran out of descriptors parsing\n");
				return -1;
			}
		
			retval = usb_parse_endpoint(dev, ifp->endpoint + i, buffer, size);
			if (retval < 0)
				return retval;

			buffer += retval;
			parsed += retval;
			size -= retval;
		}

		/* We check to see if it's an alternate to this one */
		ifp = (struct usb_interface_descriptor *)buffer;
		if (size < USB_DT_INTERFACE_SIZE ||
		    ifp->bDescriptorType != USB_DT_INTERFACE ||
		    !ifp->bAlternateSetting)
			return parsed;
	}

	return parsed;
}

static int usb_parse_configuration(struct usb_device *dev, struct usb_config_descriptor *config, char *buffer)
{
	int i;
	int retval;
	int size;
	struct usb_descriptor_header *header;

	memcpy(config, buffer, USB_DT_INTERFACE_SIZE);

	le16_to_cpus(&config->wTotalLength);
	size = config->wTotalLength;

	if (config->bNumInterfaces > USB_MAXINTERFACES) {
		printk(KERN_WARNING "usb: too many interfaces\n");
		return -1;
	}

	config->interface = (struct usb_interface *)
		kmalloc(config->bNumInterfaces *
		sizeof(struct usb_interface), GFP_KERNEL);
	if (!config->interface) {
		printk(KERN_WARNING "usb: out of memory\n");
		return -1;	
	}

	memset(config->interface, 0,
	       config->bNumInterfaces*sizeof(struct usb_interface_descriptor));

	buffer += config->bLength;
	size -= config->bLength;
	
	for (i = 0; i < config->bNumInterfaces; i++) {
		header = (struct usb_descriptor_header *)buffer;
		if (header->bLength > size) {
			printk(KERN_ERR "usb: ran out of descriptors parsing\n");
			return -1;
		}
		
		if (header->bDescriptorType != USB_DT_INTERFACE) {
			printk(KERN_INFO "usb: unexpected descriptor 0x%X\n",
				header->bDescriptorType);

			buffer += header->bLength;
			size -= header->bLength;
			continue;
		}

		retval = usb_parse_interface(dev, config->interface + i, buffer, size);
		if (retval < 0)
			return retval;

		buffer += retval;
		size -= retval;
	}

	return size;
}

void usb_destroy_configuration(struct usb_device *dev)
{
	int c, i, j;
	
	if (!dev->config)
		return;

	for (c = 0; c < dev->descriptor.bNumConfigurations; c++) {
		struct usb_config_descriptor *cf = &dev->config[c];

		if (!cf->interface)
		        break;

		for (i = 0; i < cf->bNumInterfaces; i++) {
			struct usb_interface *ifp =
				&cf->interface[i];

			if (!ifp->altsetting)
			        break;

			for (j = 0; j < ifp->num_altsetting; j++) {
				struct usb_interface_descriptor *as =
					&ifp->altsetting[j];

				if (!as->endpoint)
					break;

				kfree(as->endpoint);
			}
			kfree(ifp->altsetting);
		}
		kfree(cf->interface);
	}
	kfree(dev->config);

	if (dev->string) {
		kfree(dev->string);
		dev->string = 0;
 	}
}
			
void usb_init_root_hub(struct usb_device *dev)
{
	dev->devnum = -1;
	dev->slow = 0;
}

/*
 * Something got disconnected. Get rid of it, and all of its children.
 */
void usb_disconnect(struct usb_device **pdev)
{
	struct usb_device * dev = *pdev;
	int i;

	if (!dev)
		return;

	*pdev = NULL;

	printk("USB disconnect on device %d\n", dev->devnum);

	if (dev->driver)
		dev->driver->disconnect(dev);

	/* Free up all the children.. */
	for (i = 0; i < USB_MAXCHILDREN; i++) {
		struct usb_device **child = dev->children + i;
		usb_disconnect(child);
	}

	/* remove /proc/bus/usb entry */
	proc_usb_remove_device(dev);

	/* Free up the device itself, including its device number */
	if (dev->devnum > 0)
		clear_bit(dev->devnum, &dev->bus->devmap.devicemap);

	usb_free_dev(dev);
}

/*
 * Connect a new USB device. This basically just initializes
 * the USB device information and sets up the topology - it's
 * up to the low-level driver to reset the port and actually
 * do the setup (the upper levels don't know how to do that).
 */
void usb_connect(struct usb_device *dev)
{
	int devnum;

	dev->descriptor.bMaxPacketSize0 = 8;  /* XXX fixed 8 bytes for now */

	devnum = find_next_zero_bit(dev->bus->devmap.devicemap, 128, 1);
	if (devnum < 128) {
		set_bit(devnum, dev->bus->devmap.devicemap);
		dev->devnum = devnum;
	}
}

/*
 * These are the actual routines to send
 * and receive control messages.
 */
int usb_set_address(struct usb_device *dev)
{
	devrequest dr;

	dr.requesttype = 0;
	dr.request = USB_REQ_SET_ADDRESS;
	dr.value = dev->devnum;
	dr.index = 0;
	dr.length = 0;

	return dev->bus->op->control_msg(dev, usb_snddefctrl(dev), &dr, NULL, 0, HZ);
}

int usb_get_descriptor(struct usb_device *dev, unsigned char type, unsigned char index, void *buf, int size)
{
	devrequest dr;
	int i = 5;
	int result;

	dr.requesttype = USB_DIR_IN;
	dr.request = USB_REQ_GET_DESCRIPTOR;
	dr.value = (type << 8) + index;
	dr.index = 0;
	dr.length = size;

	while (i--) {
		if (!(result = dev->bus->op->control_msg(dev, usb_rcvctrlpipe(dev,0), &dr, buf, size, HZ))
		    || result == USB_ST_STALL)
			break;
	}
	return result;
}

int usb_get_string(struct usb_device *dev, unsigned short langid, unsigned char index, void *buf, int size)
{
	devrequest dr;

	dr.requesttype = USB_DIR_IN;
	dr.request = USB_REQ_GET_DESCRIPTOR;
	dr.value = (USB_DT_STRING << 8) + index;
	dr.index = langid;
	dr.length = size;

	return dev->bus->op->control_msg(dev, usb_rcvctrlpipe(dev,0), &dr, buf, size, HZ);
}

int usb_get_device_descriptor(struct usb_device *dev)
{
	int ret = usb_get_descriptor(dev, USB_DT_DEVICE, 0, &dev->descriptor,
				     sizeof(dev->descriptor));
	if (ret == 0) {
		le16_to_cpus(&dev->descriptor.bcdUSB);
		le16_to_cpus(&dev->descriptor.idVendor);
		le16_to_cpus(&dev->descriptor.idProduct);
		le16_to_cpus(&dev->descriptor.bcdDevice);
	}
	return ret;
}

int usb_get_status (struct usb_device *dev, int type, int target, void *data)
{
	devrequest dr;

	dr.requesttype = USB_DIR_IN | type;	/* USB_RECIP_DEVICE, _INTERFACE, or _ENDPOINT */
	dr.request = USB_REQ_GET_STATUS;
	dr.value = 0;
	dr.index = target;
	dr.length = 2;

	return dev->bus->op->control_msg (dev, usb_rcvctrlpipe (dev,0), &dr, data, 2, HZ);
}

int usb_get_protocol(struct usb_device *dev)
{
	unsigned char buf[8];
	devrequest dr;

	dr.requesttype = USB_RT_HIDD | USB_DIR_IN;
	dr.request = USB_REQ_GET_PROTOCOL;
	dr.value = 0;
	dr.index = 1;
	dr.length = 1;

	if (dev->bus->op->control_msg(dev, usb_rcvctrlpipe(dev, 0), &dr, buf, 1, HZ))
		return -1;

	return buf[0];
}

int usb_set_protocol(struct usb_device *dev, int protocol)
{
	devrequest dr;

	dr.requesttype = USB_RT_HIDD;
	dr.request = USB_REQ_SET_PROTOCOL;
	dr.value = protocol;
	dr.index = 1;
	dr.length = 0;

	if (dev->bus->op->control_msg(dev, usb_sndctrlpipe(dev, 0), &dr, NULL, 0, HZ))
		return -1;

	return 0;
}

/* keyboards want a nonzero duration according to HID spec, but
   mice should use infinity (0) -keryan */
int usb_set_idle(struct usb_device *dev,  int duration, int report_id)
{
	devrequest dr;

	dr.requesttype = USB_RT_HIDD;
	dr.request = USB_REQ_SET_IDLE;
	dr.value = (duration << 8) | report_id;
	dr.index = 1;
	dr.length = 0;

	if (dev->bus->op->control_msg(dev, usb_sndctrlpipe(dev, 0), &dr, NULL, 0, HZ))
		return -1;

	return 0;
}

static void usb_set_maxpacket(struct usb_device *dev)
{
	int i, j;
	struct usb_interface *ifp;

	for (i=0; i<dev->actconfig->bNumInterfaces; i++) {
		ifp = dev->actconfig->interface + i;

		for (j = 0; j < ifp->num_altsetting; j++) {
			struct usb_interface_descriptor *as = ifp->altsetting + j;
			struct usb_endpoint_descriptor *ep = as->endpoint;
			int e;

			for (e=0; e<as->bNumEndpoints; e++) {
				if (usb_endpoint_out(ep[e].bEndpointAddress))
					dev->epmaxpacketout[ep[e].bEndpointAddress & 0x0f] =
						ep[e].wMaxPacketSize;
				else
					dev->epmaxpacketin [ep[e].bEndpointAddress & 0x0f] =
						ep[e].wMaxPacketSize;
			}
		}
	}
}

/*
 * endp: endpoint number in bits 0-3;
 *	direction flag in bit 7 (1 = IN, 0 = OUT)
 */
int usb_clear_halt(struct usb_device *dev, int endp)
{
	devrequest dr;
	int result;
	__u16 status;

    	//if (!usb_endpoint_halted(dev, endp & 0x0f, usb_endpoint_out(endp)))
	//	return 0;

	dr.requesttype = USB_RT_ENDPOINT;
	dr.request = USB_REQ_CLEAR_FEATURE;
	dr.value = 0;
	dr.index = endp;
	dr.length = 0;

	result = dev->bus->op->control_msg(dev, usb_sndctrlpipe(dev,0), &dr, NULL, 0, HZ);

	/* don't clear if failed */
	if (result)
	    return result;

#if 1	/* let's be really tough */
	dr.requesttype = USB_DIR_IN | USB_RT_ENDPOINT;
	dr.request = USB_REQ_GET_STATUS;
	dr.length = 2;
	status = 0xffff;

	result = dev->bus->op->control_msg(dev, usb_rcvctrlpipe(dev,0), &dr, &status, 2, HZ);

	if (result)
	    return result;
	if (status & 1)
	    return 1;		/* still halted */
#endif
	usb_endpoint_running(dev, endp & 0x0f, usb_endpoint_out(endp));

	/* toggle is reset on clear */

	usb_settoggle(dev, endp & 0x0f, usb_endpoint_out(endp), 0);

	return 0;
}

int usb_set_interface(struct usb_device *dev, int interface, int alternate)
{
	devrequest dr;

	dr.requesttype = 1;
	dr.request = USB_REQ_SET_INTERFACE;
	dr.value = alternate;
	dr.index = interface;
	dr.length = 0;

	if (dev->bus->op->control_msg(dev, usb_sndctrlpipe(dev, 0), &dr, NULL, 0, HZ))
		return -1;

	dev->ifnum = interface;
	dev->actconfig->interface[interface].act_altsetting = alternate;
	usb_set_maxpacket(dev);
	return 0;
}

int usb_set_configuration(struct usb_device *dev, int configuration)
{
	devrequest dr;
	int i;
	struct usb_config_descriptor *cp = NULL;
	
	dr.requesttype = 0;
	dr.request = USB_REQ_SET_CONFIGURATION;
	dr.value = configuration;
	dr.index = 0;
	dr.length = 0;

	for (i=0; i<dev->descriptor.bNumConfigurations; i++) {
		if (dev->config[i].bConfigurationValue == configuration) {
			cp = &dev->config[i];
			break;
		}
	}
	if (!cp) {
		printk(KERN_INFO "usb: selecting invalid configuration %d\n", configuration);
		return -1;
	}
	if (dev->bus->op->control_msg(dev, usb_sndctrlpipe(dev, 0), &dr, NULL, 0, HZ))
		return -1;

	dev->actconfig = cp;
	dev->toggle[0] = 0;
	dev->toggle[1] = 0;
	usb_set_maxpacket(dev);
	return 0;
}

int usb_get_report(struct usb_device *dev, unsigned char type, unsigned char id, unsigned char index, void *buf, int size)
{
	devrequest dr;

	dr.requesttype = USB_RT_HIDD | USB_DIR_IN;
	dr.request = USB_REQ_GET_REPORT;
	dr.value = (type << 8) + id;
	dr.index = index;
	dr.length = size;

	if (dev->bus->op->control_msg(dev, usb_rcvctrlpipe(dev, 0), &dr, buf, size, HZ))
		return -1;

	return 0;
}

int usb_get_configuration(struct usb_device *dev)
{
	unsigned int cfgno;
	unsigned char buffer[8];
	unsigned char *bigbuffer;
	struct usb_config_descriptor *desc =
		(struct usb_config_descriptor *)buffer;

	if (dev->descriptor.bNumConfigurations > USB_MAXCONFIG) {
		printk(KERN_WARNING "usb: too many configurations\n");
		return -1;
	}

	dev->config = (struct usb_config_descriptor *)
		kmalloc(dev->descriptor.bNumConfigurations *
		sizeof(struct usb_config_descriptor), GFP_KERNEL);
	if (!dev->config) {
		printk(KERN_WARNING "usb: out of memory.\n");
		return -1;	
	}
	memset(dev->config, 0, dev->descriptor.bNumConfigurations *
		sizeof(struct usb_config_descriptor));

	for (cfgno = 0; cfgno < dev->descriptor.bNumConfigurations; cfgno++) {
        	int result;

		/* We grab the first 8 bytes so we know how long the whole */
		/*  configuration is */
		result = usb_get_descriptor(dev, USB_DT_CONFIG, cfgno, buffer, 8);
		if (result < 0)
	    		return -1;
		
  	  	/* Get the full buffer */
		le16_to_cpus(&desc->wTotalLength);

		bigbuffer = kmalloc(desc->wTotalLength, GFP_KERNEL);
		if (!bigbuffer)
			return -1;

		/* Now that we know the length, get the whole thing */
		result = usb_get_descriptor(dev, USB_DT_CONFIG, cfgno, bigbuffer, desc->wTotalLength);
		if (result) {
			kfree(bigbuffer);
			return -1;
		}
			
		result = usb_parse_configuration(dev, &dev->config[cfgno], bigbuffer);
		kfree(bigbuffer);

		if (result > 0)
			printk(KERN_INFO "usb: descriptor data left\n");
		else if (result < 0)
			return -1;
	}

	return 0;
}

char *usb_string(struct usb_device *dev, int index)
{
	int len, i;
	char *ptr;
	union {
		unsigned char buffer[256];
		struct usb_string_descriptor desc;
	} u;

	if (index <= 0)
		return 0;
	if (dev->string)
		kfree (dev->string);

	if (dev->string_langid == 0) {
		/* read string descriptor 0 */
		if (usb_get_string(dev, 0, 0, u.buffer, 2) == 0
		    && u.desc.bLength >= 4
		    && usb_get_string(dev, 0, 0, u.buffer, 4) == 0)
			dev->string_langid = le16_to_cpup(&u.desc.wData[0]);
		dev->string_langid |= 0x10000;	/* so it's non-zero */
	}

	if (usb_get_string(dev, dev->string_langid, index, u.buffer, 2) ||
	    usb_get_string(dev, dev->string_langid, index, u.buffer,
			      u.desc.bLength))
		return 0;

	len = u.desc.bLength / 2;	/* includes terminating null */

	ptr = kmalloc(len, GFP_KERNEL);
	if (!ptr)
		return 0;

	for (i = 0; i < len - 1; ++i)
		ptr[i] = le16_to_cpup(&u.desc.wData[i]);
	ptr[i] = 0;

	dev->string = ptr;
	return ptr;
}

/*
 * By the time we get here, the device has gotten a new device ID
 * and is in the default state. We need to identify the thing and
 * get the ball rolling..
 */
int usb_new_device(struct usb_device *dev)
{
	int addr;

	printk(KERN_INFO "USB new device connect, assigned device number %d\n",
		dev->devnum);

	dev->maxpacketsize = 0;		/* Default to 8 byte max packet size */
	dev->epmaxpacketin [0] = 8;
	dev->epmaxpacketout[0] = 8;

	/* We still haven't set the Address yet */
	addr = dev->devnum;
	dev->devnum = 0;

	/* Slow devices */
	if (usb_get_descriptor(dev, USB_DT_DEVICE, 0, &dev->descriptor, 8)) {
		printk(KERN_ERR "usbcore: USB device not responding, giving up\n");
		dev->devnum = -1;
		return 1;
	}

	dev->epmaxpacketin [0] = dev->descriptor.bMaxPacketSize0;
	dev->epmaxpacketout[0] = dev->descriptor.bMaxPacketSize0;
	switch (dev->descriptor.bMaxPacketSize0) {
		case 8: dev->maxpacketsize = 0; break;
		case 16: dev->maxpacketsize = 1; break;
		case 32: dev->maxpacketsize = 2; break;
		case 64: dev->maxpacketsize = 3; break;
	}

	dev->devnum = addr;

	if (usb_set_address(dev)) {
		printk(KERN_ERR "usbcore: USB device not accepting new address\n");
		dev->devnum = -1;
		return 1;
	}

	wait_ms(10);	/* Let the SET_ADDRESS settle */

	if (usb_get_device_descriptor(dev)) {
		printk(KERN_ERR "usbcore: unable to get device descriptor\n");
		dev->devnum = -1;
		return 1;
	}

	if (usb_get_configuration(dev)) {
		printk(KERN_ERR "usbcore: unable to get configuration\n");
		dev->devnum = -1;
		return 1;
	}

	dev->actconfig = dev->config;
	dev->ifnum = 0;
	usb_set_maxpacket(dev);

	usb_show_string(dev, "Manufacturer", dev->descriptor.iManufacturer);
	usb_show_string(dev, "Product", dev->descriptor.iProduct);
	usb_show_string(dev, "SerialNumber", dev->descriptor.iSerialNumber);

	/* now that the basic setup is over, add a /proc/bus/usb entry */
	proc_usb_add_device(dev);

	if (!usb_find_driver(dev)) {
		/*
		 * Ok, no driver accepted the device, so show the info for
		 * debugging
		 */
		printk(KERN_DEBUG "Unknown new USB device:\n");
		usb_show_device(dev);
	}

	return 0;
}

int usb_control_msg(struct usb_device *dev, unsigned int pipe, __u8 request, __u8 requesttype, __u16 value, __u16 index, void *data, __u16 size, int timeout)
{
        devrequest dr;

        dr.requesttype = requesttype;
        dr.request = request;
        dr.value = value;
        dr.index = index;
        dr.length = size;

        return dev->bus->op->control_msg(dev, pipe, &dr, data, size, timeout);
}

int usb_request_irq(struct usb_device *dev, unsigned int pipe, usb_device_irq handler, int period, void *dev_id, void **handle)
{
	long    bustime;
	int	ret;

	*handle = NULL;

	/* Check host controller's bandwidth for this int. request. */
	bustime = calc_bus_time (usb_pipeslow(pipe), usb_pipein(pipe), 0,
			usb_maxpacket(dev, pipe, usb_pipeout(pipe)));
	bustime = NS_TO_US(bustime);	/* work in microseconds */
	if (check_bandwidth_alloc (dev->bus->bandwidth_allocated, bustime))
		return (USB_ST_BANDWIDTH_ERROR);

	ret = dev->bus->op->request_irq(dev, pipe, handler, period, dev_id, handle);

	/* Claim the USB bandwidth if no error. */
	if (!ret) {
		dev->bus->bandwidth_allocated += bustime;
		dev->bus->bandwidth_int_reqs++;
		PRINTD ("bw_alloc bumped to %d for %d requesters",
			dev->bus->bandwidth_allocated,
			dev->bus->bandwidth_int_reqs +
			dev->bus->bandwidth_isoc_reqs);
	}

	return ret;
}

void *usb_request_bulk(struct usb_device *dev, unsigned int pipe, usb_device_irq handler, void *data, int len, void *dev_id)
{
	return dev->bus->op->request_bulk(dev, pipe, handler, data, len, dev_id);
}

int usb_terminate_bulk(struct usb_device *dev, void *first)
{
	return dev->bus->op->terminate_bulk(dev, first);
}

int usb_release_irq(struct usb_device *dev, void *handle, unsigned int pipe)
{
	long    bustime;
	int	err;

	err = dev->bus->op->release_irq(dev, handle);

	/* Return the USB bandwidth if no error. */
	if (!err) {
		bustime = calc_bus_time (usb_pipeslow(pipe), usb_pipein(pipe), 0,
				usb_maxpacket(dev, pipe, usb_pipeout(pipe)));
		bustime = NS_TO_US(bustime);
		dev->bus->bandwidth_allocated -= bustime;
		dev->bus->bandwidth_int_reqs--;
		PRINTD ("bw_alloc reduced to %d for %d requesters",
			dev->bus->bandwidth_allocated,
			dev->bus->bandwidth_int_reqs +
			dev->bus->bandwidth_isoc_reqs);
	}

	return err;
}

/*
 * usb_get_current_frame_number()
 *
 * returns the current frame number for the parent USB bus/controller
 * of the given USB device.
 */
int usb_get_current_frame_number (struct usb_device *usb_dev)
{
	return usb_dev->bus->op->get_frame_number (usb_dev);
}

int usb_init_isoc (struct usb_device *usb_dev,
			unsigned int pipe,
			int frame_count,
			void *context,
			struct usb_isoc_desc **isocdesc)
{
	long    bustime;
	int	err;

	if (frame_count <= 0)
		return -EINVAL;

	/* Check host controller's bandwidth for this Isoc. request. */
	/* TBD: some way to factor in frame_spacing ??? */
	bustime = calc_bus_time (0, usb_pipein(pipe), 1,
			usb_maxpacket(usb_dev, pipe, usb_pipeout(pipe)));
	bustime = NS_TO_US(bustime) / frame_count;	/* work in microseconds */
	if (check_bandwidth_alloc (usb_dev->bus->bandwidth_allocated, bustime))
		return USB_ST_BANDWIDTH_ERROR;

	err = usb_dev->bus->op->init_isoc (usb_dev, pipe, frame_count, context, isocdesc);

	/* Claim the USB bandwidth if no error. */
	if (!err) {
		usb_dev->bus->bandwidth_allocated += bustime;
		usb_dev->bus->bandwidth_isoc_reqs++;
		PRINTD ("bw_alloc bumped to %d for %d requesters",
			usb_dev->bus->bandwidth_allocated,
			usb_dev->bus->bandwidth_int_reqs +
			usb_dev->bus->bandwidth_isoc_reqs);
	}

	return err;
}

void usb_free_isoc (struct usb_isoc_desc *isocdesc)
{
	long    bustime;

	/* Return the USB bandwidth. */
	bustime = calc_bus_time (0, usb_pipein(isocdesc->pipe), 1,
			usb_maxpacket(isocdesc->usb_dev, isocdesc->pipe,
			usb_pipeout(isocdesc->pipe)));
	bustime = NS_TO_US(bustime) / isocdesc->frame_count;
	isocdesc->usb_dev->bus->bandwidth_allocated -= bustime;
	isocdesc->usb_dev->bus->bandwidth_isoc_reqs--;
	PRINTD ("bw_alloc reduced to %d for %d requesters",
		isocdesc->usb_dev->bus->bandwidth_allocated,
		isocdesc->usb_dev->bus->bandwidth_int_reqs +
		isocdesc->usb_dev->bus->bandwidth_isoc_reqs);

	isocdesc->usb_dev->bus->op->free_isoc (isocdesc);
}

int usb_run_isoc (struct usb_isoc_desc *isocdesc,
			struct usb_isoc_desc *pr_isocdesc)
{
	return isocdesc->usb_dev->bus->op->run_isoc (isocdesc, pr_isocdesc);
}

int usb_kill_isoc (struct usb_isoc_desc *isocdesc)
{
	return isocdesc->usb_dev->bus->op->kill_isoc (isocdesc);
}

static int usb_open(struct inode * inode, struct file * file)
{
	int minor = MINOR(inode->i_rdev);
	struct usb_driver *c = usb_minors[minor/16];
	file->f_op = NULL;

	if ((file->f_op = c->fops) && file->f_op->open)
		return file->f_op->open(inode,file);
	else
		return -ENODEV;
}

static struct file_operations usb_fops = {
        NULL,		/* seek */
	NULL,		/* read */
	NULL,		/* write */
	NULL,		/* readdir */
	NULL,		/* poll */
	NULL,		/* ioctl */
	NULL,		/* mmap */
        usb_open,
	NULL,		/* flush */
        NULL		/* release */
};

void usb_major_init(void)
{
	if (register_chrdev(180,"usb",&usb_fops)) {
		printk("unable to get major %d for usb devices\n",
		       MISC_MAJOR);
		return -EIO;
	}
}


#ifdef CONFIG_PROC_FS
struct list_head *usb_driver_get_list(void)
{
	return &usb_driver_list;
}

struct list_head *usb_bus_get_list(void)
{
	return &usb_bus_list;
}
#endif

/*
 * USB may be built into the kernel or be built as modules.
 * If the USB core [and maybe a host controller driver] is built
 * into the kernel, and other device drivers are built as modules,
 * then these symbols need to be exported for the modules to use.
 */
EXPORT_SYMBOL(usb_register);
EXPORT_SYMBOL(usb_deregister);
EXPORT_SYMBOL(usb_alloc_bus);
EXPORT_SYMBOL(usb_free_bus);
EXPORT_SYMBOL(usb_register_bus);
EXPORT_SYMBOL(usb_deregister_bus);
EXPORT_SYMBOL(usb_alloc_dev);
EXPORT_SYMBOL(usb_free_dev);
EXPORT_SYMBOL(usb_inc_dev_use);

EXPORT_SYMBOL(usb_init_root_hub);
EXPORT_SYMBOL(usb_new_device);
EXPORT_SYMBOL(usb_connect);
EXPORT_SYMBOL(usb_disconnect);

EXPORT_SYMBOL(usb_set_address);
EXPORT_SYMBOL(usb_get_descriptor);
EXPORT_SYMBOL(usb_get_string);
EXPORT_SYMBOL(usb_string);
EXPORT_SYMBOL(usb_get_protocol);
EXPORT_SYMBOL(usb_set_protocol);
EXPORT_SYMBOL(usb_get_report);
EXPORT_SYMBOL(usb_set_idle);
EXPORT_SYMBOL(usb_clear_halt);
EXPORT_SYMBOL(usb_set_interface);
EXPORT_SYMBOL(usb_get_configuration);
EXPORT_SYMBOL(usb_set_configuration);

EXPORT_SYMBOL(usb_control_msg);
EXPORT_SYMBOL(usb_request_irq);
EXPORT_SYMBOL(usb_release_irq);
/* EXPORT_SYMBOL(usb_bulk_msg); */
EXPORT_SYMBOL(usb_request_bulk);
EXPORT_SYMBOL(usb_terminate_bulk);

EXPORT_SYMBOL(usb_get_current_frame_number);
EXPORT_SYMBOL(usb_init_isoc);
EXPORT_SYMBOL(usb_free_isoc);
EXPORT_SYMBOL(usb_run_isoc);
EXPORT_SYMBOL(usb_kill_isoc);
