/*
 * drivers/usb/usb.c
 *
 * (C) Copyright Linus Torvalds 1999
 *
 * NOTE! This is not actually a driver at all, rather this is
 * just a collection of helper routines that implement the
 * generic USB things that the real drivers can use..
 *
 * Think of this as a "USB library" rather than anything else.
 * It should be considered a slave, with no callbacks. Callbacks
 * are evil.
 */

/*
 * Table 9-2
 *
 * Offset	Field		Size	Value		Desc
 * 0		bmRequestType	1	Bitmap		D7:	Direction
 *							0 = Host-to-device
 *							1 = Device-to-host
 *							D6..5:	Type
 *							0 = Standard
 *							1 = Class
 *							2 = Vendor
 *							3 = Reserved
 *							D4..0:	Recipient
 *							0 = Device
 *							1 = Interface
 *							2 = Endpoint
 *							3 = Other
 *							4..31 = Reserved
 * 1		bRequest	1	Value		Specific request (9-3)
 * 2		wValue		2	Value		Varies
 * 4		wIndex		2	Index/Offset	Varies
 * 6		wLength		2	Count		Bytes for data
 */

#include <linux/config.h>
#include <linux/string.h>
#include <linux/bitops.h>
#include <linux/malloc.h>

#include "usb.h"

/*
 * We have a per-interface "registered driver" list.
 */
static LIST_HEAD(usb_driver_list);
static LIST_HEAD(usb_bus_list);

int usb_register(struct usb_driver *new_driver)
{
	struct list_head *tmp;

	printk("usbcore: Registering new driver %s\n", new_driver->name);

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
		struct usb_bus *bus = list_entry(tmp,struct usb_bus,bus_list);

	        tmp = tmp->next;
		usb_check_support(bus->root_hub);
	}
	return 0;
}

void usb_deregister(struct usb_driver *driver)
{
	struct list_head *tmp;

	printk("usbcore: Deregistering driver %s\n", driver->name);

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

/* This function is part of a depth-first search down the device tree,
 * removing any instances of a device driver.
 */
void usb_driver_purge(struct usb_driver *driver,struct usb_device *dev)
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

	INIT_LIST_HEAD(&bus->bus_list);

	return bus;
}

void usb_free_bus(struct usb_bus *bus)
{
	if (!bus)
		return;

	if (bus->bus_list.next != &bus->bus_list)
		printk(KERN_ERR "usbcore: freeing non-empty bus\n");

	kfree(bus);
}

void usb_register_bus(struct usb_bus *new_bus)
{
	/* Add it to the list of buses */
	list_add(&new_bus->bus_list, &usb_bus_list);
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
}

/*
 * This function is for doing a depth-first search for devices which
 * have support, for dynamic loading of driver modules.
 */
void usb_check_support(struct usb_device *dev)
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
int usb_find_driver(struct usb_device *dev)
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
 * Parse the fairly incomprehensible output of
 * the USB configuration data, and build up the
 * USB device database.
 */
static int usb_expect_descriptor(unsigned char *ptr, int len, unsigned char desctype, unsigned char descindex)
{
	int parsed = 0;
	int n_len;
	unsigned short n_desc;

	for (;;) {
		int i;

		if (len < descindex)
			return -1;
		n_desc = le16_to_cpup((unsigned short *)ptr);
		n_len = ptr[0];

		if (n_desc == ((desctype << 8) + descindex))
			break;

		if (((n_desc >> 8)&0xFF) == desctype &&
			n_len > descindex)
		{
			printk("bug: oversized descriptor.\n");
			break;
		}
			
		if (n_len < 2 || n_len > len)
		{
			printk("Short descriptor\n");
			return -1;
		}
		printk(
		"Expected descriptor %02X/%02X, got %02X/%02X - skipping\n",
			desctype, descindex,
			(n_desc >> 8) & 0xFF, n_desc & 0xFF);
		for (i = 0 ; i < n_len; i++)
			printk("   %d %02x\n", i, ptr[i]);
		len -= n_len;
		ptr += n_len;
		parsed += n_len;
	}
	
	printk("Found %02X:%02X\n",
		desctype, descindex);
	return parsed;
}

/*
 * Parse the even more incomprehensible mess made of the USB spec
 * by USB audio having private magic to go with it.
 */
 
static int usb_check_descriptor(unsigned char *ptr, int len, unsigned char desctype)
{
	int n_len = ptr[0];

	if (len <= 0)
		return -1;

	if (n_len < 2 || n_len > len) {
		int i;
		printk("Short descriptor. (%d, %d):\n", len, n_len);
		for (i = 0; i < len; ++i)
			printk(" %d: %x\n", i, ptr[i]);
		return -1;
	}

	if (ptr[1] == desctype)
		return 0;
		
	return -1;
}


static int usb_parse_endpoint(struct usb_device *dev, struct usb_endpoint_descriptor *endpoint, unsigned char *ptr, int len)
{
	int parsed = usb_expect_descriptor(ptr, len, USB_DT_ENDPOINT, USB_DT_ENDPOINT_SIZE);
	int i;

	if (parsed < 0)
		return parsed;
	memcpy(endpoint, ptr + parsed, ptr[parsed]);
	le16_to_cpus(&endpoint->wMaxPacketSize);

	parsed += ptr[parsed];
	len -= parsed;

	while((i = usb_check_descriptor(ptr+parsed, len, 0x25)) >= 0) {
		usb_audio_endpoint(endpoint, ptr+parsed+i);
		len -= ptr[parsed+i];
		parsed += ptr[parsed+i];
	}
	
	return parsed;// + ptr[parsed];
}

static int usb_parse_interface(struct usb_device *dev, struct usb_interface_descriptor *interface, unsigned char *ptr, int len)
{
	int i;
	int parsed = usb_expect_descriptor(ptr, len, USB_DT_INTERFACE, USB_DT_INTERFACE_SIZE);
	int retval;

	if (parsed < 0)
		return parsed;

	memcpy(interface, ptr + parsed, *ptr);
	len -= ptr[parsed];
	parsed += ptr[parsed];

	while((i=usb_check_descriptor(ptr+parsed, len, 0x24)) >= 0) {
		usb_audio_interface(interface, ptr+parsed+i);
		len -= ptr[parsed+i];
		parsed += ptr[parsed+i];
	}
	
	if (interface->bNumEndpoints > USB_MAXENDPOINTS) {
		printk(KERN_WARNING "usb: too many endpoints.\n");
		return -1;
	}

	interface->endpoint = (struct usb_endpoint_descriptor *)
		kmalloc(interface->bNumEndpoints * sizeof(struct usb_endpoint_descriptor), GFP_KERNEL);
	if (!interface->endpoint) {
		printk(KERN_WARNING "usb: out of memory.\n");
		return -1;	
	}
	memset(interface->endpoint, 0, interface->bNumEndpoints*sizeof(struct usb_endpoint_descriptor));
	
	for (i = 0; i < interface->bNumEndpoints; i++) {
//		if(((USB_DT_HID << 8) | 9) == *(unsigned short*)(ptr + parsed)) {
//			parsed += 9;	/* skip over the HID descriptor for now */
//			len -= 9;
//		}
		retval = usb_parse_endpoint(dev, interface->endpoint + i, ptr + parsed, len);
		if (retval < 0)
			return retval;
		parsed += retval;
		len -= retval;
	}
	return parsed;
}

static int usb_parse_config(struct usb_device *dev, struct usb_config_descriptor *config, unsigned char *ptr, int len)
{
	int i, j;
	int retval;
	struct usb_alternate_setting *as;
	int parsed = usb_expect_descriptor(ptr, len, USB_DT_CONFIG, 9);

	if (parsed < 0)
		return parsed;

	memcpy(config, ptr + parsed, *ptr);
	len -= *ptr;
	parsed += *ptr;
	le16_to_cpus(&config->wTotalLength);

	if (config->bNumInterfaces > USB_MAXINTERFACES) {
		printk(KERN_WARNING "usb: too many interfaces.\n");
		return -1;

	}

	config->altsetting = (struct usb_alternate_setting *)
	        kmalloc(USB_MAXALTSETTING * sizeof(struct usb_alternate_setting), GFP_KERNEL);
	if (!config->altsetting) {
		printk(KERN_WARNING "usb: out of memory.\n");
		return -1;
	}
	config->act_altsetting = 0;
	config->num_altsetting = 1;

	config->altsetting->interface = (struct usb_interface_descriptor *)
		kmalloc(config->bNumInterfaces * sizeof(struct usb_interface_descriptor), GFP_KERNEL);
	if (!config->altsetting->interface) {
		printk(KERN_WARNING "usb: out of memory.\n");
		return -1;	
	}
	memset(config->altsetting->interface, 
	       0, config->bNumInterfaces*sizeof(struct usb_interface_descriptor));
	
	for (i = 0; i < config->bNumInterfaces; i++) {
		retval = usb_parse_interface(dev, config->altsetting->interface + i, ptr + parsed, len);
		if (retval < 0)
			return parsed; // HACK
//			return retval;
		parsed += retval;
		len -= retval;
	}

	printk("parsed = %d len = %d\n", parsed, len);

	// now parse for additional alternate settings
	for (j = 1; j < USB_MAXALTSETTING; j++) {
		retval = usb_expect_descriptor(ptr + parsed, len, USB_DT_INTERFACE, 9);
		if (retval) 
			break;
		config->num_altsetting++;
		as = config->altsetting + j;
		as->interface = (struct usb_interface_descriptor *)
			kmalloc(config->bNumInterfaces * sizeof(struct usb_interface_descriptor), GFP_KERNEL);
		if (!as->interface) {
			printk(KERN_WARNING "usb: out of memory.\n");
			return -1;
		}
		memset(as->interface, 0, config->bNumInterfaces * sizeof(struct usb_interface_descriptor));
		for (i = 0; i < config->bNumInterfaces; i++) {
			retval = usb_parse_interface(dev, as->interface + i, 
					 ptr + parsed, len);
			if (retval < 0)
				return parsed;
			parsed += retval;
			len -= retval;
		}
	}
	return parsed;
}

int usb_parse_configuration(struct usb_device *dev, void *__buf, int bytes)
{
	int i;
	unsigned char *ptr = __buf;

	if (dev->descriptor.bNumConfigurations > USB_MAXCONFIG) {
		printk(KERN_WARNING "usb: too many configurations.\n");
		return -1;
	}

	dev->config = (struct usb_config_descriptor *)
		kmalloc(dev->descriptor.bNumConfigurations * sizeof(struct usb_config_descriptor), GFP_KERNEL);
	if (!dev->config) {
		printk(KERN_WARNING "usb: out of memory.\n");
		return -1;	
	}
	memset(dev->config, 0, dev->descriptor.bNumConfigurations*sizeof(struct usb_config_descriptor));
	for (i = 0; i < dev->descriptor.bNumConfigurations; i++) {
		int retval = usb_parse_config(dev, dev->config + i, ptr, bytes);
		if (retval < 0)
			return retval;
		ptr += retval;
		bytes -= retval;
	}
	if (bytes)
		printk(KERN_WARNING "usb: %d bytes of extra configuration data left\n", bytes);
	return 0;
}

void usb_destroy_configuration(struct usb_device *dev)
{
	int c, a, i;
	struct usb_config_descriptor *cf;
	struct usb_alternate_setting *as;
	struct usb_interface_descriptor *ifp;
	
	if (!dev->config)
		return;

	for (c = 0; c < dev->descriptor.bNumConfigurations; c++) {
		cf = &dev->config[c];
		if (!cf->altsetting)
		        break;
		for (a = 0; a < cf->num_altsetting; a++) {
		        as = &cf->altsetting[a];
			if (as->interface == NULL)
			        break;
			for(i=0;i<cf->bNumInterfaces;i++) {
			        ifp = &as->interface[i];
				if(ifp->endpoint==NULL)
				       break;
				kfree(ifp->endpoint);
			}
			kfree(as->interface);
		}
		kfree(cf->altsetting);
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

	if (dev) {
		int i;

		*pdev = NULL;

		printk("USB disconnect on device %d\n", dev->devnum);

		if(dev->driver) dev->driver->disconnect(dev);

		/* Free up all the children.. */
		for (i = 0; i < USB_MAXCHILDREN; i++) {
			struct usb_device **child = dev->children + i;
			usb_disconnect(child);
		}

		/* Free up the device itself, including its device number */
		if (dev->devnum > 0)
			clear_bit(dev->devnum, &dev->bus->devmap.devicemap);
		dev->bus->op->deallocate(dev);
	}
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

	return dev->bus->op->control_msg(dev, usb_snddefctrl(dev), &dr, NULL, 0);
}

int usb_get_descriptor(struct usb_device *dev, unsigned char type, unsigned char index, void *buf, int size)
{
	devrequest dr;
	int i = 5;
	int result;

	dr.requesttype = 0x80;
	dr.request = USB_REQ_GET_DESCRIPTOR;
	dr.value = (type << 8) + index;
	dr.index = 0;
	dr.length = size;

	while (i--) {
		if (!(result = dev->bus->op->control_msg(dev, usb_rcvctrlpipe(dev,0), &dr, buf, size))
		    || result == USB_ST_STALL)
			break;
	}
	return result;
}

int usb_get_string(struct usb_device *dev, unsigned short langid, unsigned char index, void *buf, int size)
{
	devrequest dr;

	dr.requesttype = 0x80;
	dr.request = USB_REQ_GET_DESCRIPTOR;
	dr.value = (USB_DT_STRING << 8) + index;
	dr.index = langid;
	dr.length = size;

	return dev->bus->op->control_msg(dev, usb_rcvctrlpipe(dev,0), &dr, buf, size);
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

	return dev->bus->op->control_msg (dev, usb_rcvctrlpipe (dev,0), &dr, data, 2);
}

int usb_get_protocol(struct usb_device *dev)
{
	unsigned char buf[8];
	devrequest dr;

	dr.requesttype = USB_RT_HIDD | 0x80;
	dr.request = USB_REQ_GET_PROTOCOL;
	dr.value = 0;
	dr.index = 1;
	dr.length = 1;

	if (dev->bus->op->control_msg(dev, usb_rcvctrlpipe(dev, 0), &dr, buf, 1))
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

	if (dev->bus->op->control_msg(dev, usb_sndctrlpipe(dev, 0), &dr, NULL, 0))
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

	if (dev->bus->op->control_msg(dev, usb_sndctrlpipe(dev, 0), &dr, NULL, 0))
		return -1;

	return 0;
}

static void usb_set_maxpacket(struct usb_device *dev)
{
	int i;
	int act_as = dev->actconfig->act_altsetting;
	struct usb_alternate_setting *as = dev->actconfig->altsetting + act_as;

	for (i=0; i<dev->actconfig->bNumInterfaces; i++) {
		struct usb_interface_descriptor *ip = &as->interface[i];
		struct usb_endpoint_descriptor *ep = ip->endpoint;
		int e;

		for (e=0; e<ip->bNumEndpoints; e++) {
			if (usb_endpoint_out(ep[e].bEndpointAddress))
				dev->epmaxpacketout[ep[e].bEndpointAddress & 0x0f] =
					ep[e].wMaxPacketSize;
			else
				dev->epmaxpacketin [ep[e].bEndpointAddress & 0x0f] =
					ep[e].wMaxPacketSize;
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

	result = dev->bus->op->control_msg(dev, usb_sndctrlpipe(dev,0), &dr, NULL, 0);

	/* don't clear if failed */
	if (result)
	    return result;

#if 1	/* let's be really tough */
	dr.requesttype = 0x80 | USB_RT_ENDPOINT;
	dr.request = USB_REQ_GET_STATUS;
	dr.length = 2;
	status = 0xffff;

	result = dev->bus->op->control_msg(dev, usb_rcvctrlpipe(dev,0), &dr, &status, 2);

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

	if (dev->bus->op->control_msg(dev, usb_sndctrlpipe(dev, 0), &dr, NULL, 0))
		return -1;

	dev->ifnum = interface;
	dev->actconfig->act_altsetting = alternate;
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
	if (dev->bus->op->control_msg(dev, usb_sndctrlpipe(dev, 0), &dr, NULL, 0))
		return -1;

	dev->actconfig = cp;
	dev->toggle[0] = 0;
	dev->toggle[1] = 0;
	usb_set_maxpacket(dev);
	return 0;
}

int usb_get_report(struct usb_device *dev)
{
	unsigned char buf[8];
	devrequest dr;

	dr.requesttype = USB_RT_HIDD | 0x80;
	dr.request = USB_REQ_GET_REPORT;
	dr.value = 0x100;
	dr.index = 1;
	dr.length = 3;

	if (dev->bus->op->control_msg(dev, usb_rcvctrlpipe(dev, 0), &dr, buf, 3))
		return -1;

	return buf[0];
}

int usb_get_configuration(struct usb_device *dev)
{
	unsigned int cfgno;
	unsigned char * bufptr;
	unsigned char * buffer;
	int parse;

	buffer = (unsigned char *) __get_free_page (GFP_KERNEL);
	if (!buffer)
		return -1;

	bufptr = buffer;
	for (cfgno = 0 ; cfgno < dev->descriptor.bNumConfigurations ; cfgno++) {
		unsigned int size;
  		/* Get the first 8 bytes - guaranteed */
	  	if (usb_get_descriptor(dev, USB_DT_CONFIG, cfgno, bufptr, 8)) {
			__free_page ((struct page *) buffer);
	    		return -1;
		}

  	  	/* Get the full buffer */
	  	size = le16_to_cpup((unsigned short *)(bufptr+2));
	  	if (bufptr+size > buffer+PAGE_SIZE) {
			printk(KERN_INFO "usb: truncated DT_CONFIG (want %d).\n", size);
			size = buffer+PAGE_SIZE-bufptr;
		}
		if (usb_get_descriptor(dev, USB_DT_CONFIG, cfgno, bufptr, size)) {
			__free_page ((struct page *) buffer);
			return -1;
		}
			
		/* Prepare for next configuration */
		bufptr += size;
	}
	parse = usb_parse_configuration(dev, buffer, bufptr - buffer);
	__free_page ((struct page *) buffer);
	return parse;
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

	if (usb_get_string(dev, dev->string_langid, index, u.buffer, 2)
	    || usb_get_string(dev, dev->string_langid, index, u.buffer,
			      u.desc.bLength))
		return 0;

	len = u.desc.bLength / 2;	/* includes terminating null */
	ptr = kmalloc(len, GFP_KERNEL);
	if (ptr == 0)
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
		printk(KERN_ERR "USB device not responding, giving up\n");
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
		printk(KERN_ERR "USB device not accepting new address\n");
		dev->devnum = -1;
		return 1;
	}

	wait_ms(10);	/* Let the SET_ADDRESS settle */

	if (usb_get_device_descriptor(dev)) {
		printk(KERN_ERR "Unable to get device descriptor\n");
		dev->devnum = -1;
		return 1;
	}

	if (usb_get_configuration(dev)) {
		printk(KERN_ERR "Unable to get configuration\n");
		dev->devnum = -1;
		return 1;
	}

	dev->actconfig = dev->config;
	dev->ifnum = 0;
	usb_set_maxpacket(dev);

	usb_show_string(dev, "Manufacturer", dev->descriptor.iManufacturer);
	usb_show_string(dev, "Product", dev->descriptor.iProduct);
	usb_show_string(dev, "SerialNumber", dev->descriptor.iSerialNumber);

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

void* usb_request_irq(struct usb_device *dev, unsigned int pipe, usb_device_irq handler, int period, void *dev_id)
{
	return dev->bus->op->request_irq(dev, pipe, handler, period, dev_id);
}

void *usb_request_bulk(struct usb_device *dev, unsigned int pipe, usb_device_irq handler, void *data, int len, void *dev_id)
{
	return dev->bus->op->request_bulk(dev, pipe, handler, data, len, dev_id);
}

int usb_terminate_bulk(struct usb_device *dev, void *first)
{
	return dev->bus->op->terminate_bulk(dev, first);
}

void *usb_allocate_isochronous (struct usb_device *usb_dev, unsigned int pipe, void *data, int len, int maxsze, usb_device_irq completed, void *dev_id)
{
	return usb_dev->bus->op->alloc_isoc (usb_dev, pipe, data, len, maxsze, completed, dev_id);
}

void usb_delete_isochronous (struct usb_device *dev, void *_isodesc)
{
	return dev->bus->op->delete_isoc (dev, _isodesc);
}

int usb_schedule_isochronous (struct usb_device *usb_dev, void *_isodesc, void *_pisodesc)
{
	return usb_dev->bus->op->sched_isoc (usb_dev, _isodesc, _pisodesc);
}

int usb_unschedule_isochronous (struct usb_device *usb_dev, void *_isodesc)
{
	return usb_dev->bus->op->unsched_isoc (usb_dev, _isodesc);
}

int usb_compress_isochronous (struct usb_device *usb_dev, void *_isodesc)
{
	return usb_dev->bus->op->compress_isoc (usb_dev, _isodesc);
}

int usb_release_irq(struct usb_device *dev, void* handle)
{
	return dev->bus->op->release_irq(handle);
}

#ifdef CONFIG_PROC_FS
struct list_head * usb_driver_get_list (void)
{
	return &usb_driver_list;
}

struct list_head * usb_bus_get_list (void)
{
	return &usb_bus_list;
}
#endif
