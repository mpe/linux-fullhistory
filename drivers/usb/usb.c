/*
 * driver/usb/usb.c
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

#include <linux/string.h>
#include <linux/bitops.h>
#include <linux/malloc.h>

#include "usb.h"

/*
 * We have a per-interface "registered driver" list.
 */
static LIST_HEAD(usb_driver_list);

int usb_register(struct usb_driver *new_driver)
{
	/* Add it to the list of known drivers */
	list_add(&new_driver->driver_list, &usb_driver_list);

	/*
	 * We should go through all existing devices, and see if any of
	 * them would be acceptable to the new driver.. Let's do that
	 * in version 2.0.
	 */
	return 0;
}

void usb_deregister(struct usb_driver *driver)
{
	list_del(&driver->driver_list);
}

/*
 * This entrypoint gets called for each new device.
 *
 * We now walk the list of registered USB drivers,
 * looking for one that will accept this device as
 * his..
 */
void usb_device_descriptor(struct usb_device *dev)
{
	struct list_head *tmp = usb_driver_list.next;

	while (tmp != &usb_driver_list) {
		struct usb_driver *driver = list_entry(tmp, struct usb_driver, driver_list);
		tmp = tmp->next;
		if (driver->probe(dev))
			continue;
		dev->driver = driver;
		return;
	}

	/*
	 * Ok, no driver accepted the device, so show the info
	 * for debugging..
	 */
	printk("Unknown new USB device:\n");
	usb_show_device(dev);
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
		n_desc = *(unsigned short *)ptr;
		n_len = n_desc & 0xff;

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
			printk("Short descriptor.\n");
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

	if (n_len < 2 || n_len > len)
	{
		printk("Short descriptor.\n");
		return -1;
	}

	if (ptr[1] == desctype)
		return 0;
		
	return -1;
}


static int usb_parse_endpoint(struct usb_device *dev, struct usb_endpoint_descriptor *endpoint, unsigned char *ptr, int len)
{
	int parsed = usb_expect_descriptor(ptr, len, USB_DT_ENDPOINT, 7);
	int i;

	if (parsed < 0)
		return parsed;
	memcpy(endpoint, ptr + parsed, ptr[parsed]);

	parsed += ptr[parsed];
	len -= ptr[parsed];

	while((i = usb_check_descriptor(ptr+parsed, len, 0x25))>=0)
	{
		usb_audio_endpoint(endpoint, ptr+parsed+i);
		len -= ptr[parsed+i];
		parsed += ptr[parsed+i];
	}
	
	return parsed;// + ptr[parsed];
}

static int usb_parse_interface(struct usb_device *dev, struct usb_interface_descriptor *interface, unsigned char *ptr, int len)
{
	int i;
	int parsed = usb_expect_descriptor(ptr, len, USB_DT_INTERFACE, 9);
	int retval;

	if (parsed < 0)
		return parsed;

	memcpy(interface, ptr + parsed, *ptr);
	len -= ptr[parsed];
	parsed += ptr[parsed];

	while((i=usb_check_descriptor(ptr+parsed, len, 0x24))>=0)
	{
		usb_audio_interface(interface, ptr+parsed+i);
		len -= ptr[parsed+i];
		parsed += ptr[parsed+i];
	}
	
	if (interface->bNumEndpoints > USB_MAXENDPOINTS)
	{
		printk(KERN_WARNING "usb: too many endpoints.\n");
		return -1;
	}

	interface->endpoint = (struct usb_endpoint_descriptor *)
		kmalloc(interface->bNumEndpoints * sizeof(struct usb_endpoint_descriptor), GFP_KERNEL);
	if(interface->endpoint==NULL)
	{
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
		if (retval < 0) return retval;
		parsed += retval;
		len -= retval;
	}
	return parsed;
}

static int usb_parse_config(struct usb_device *dev, struct usb_config_descriptor *config, unsigned char *ptr, int len)
{
	int i;
	int parsed = usb_expect_descriptor(ptr, len, USB_DT_CONFIG, 9);

	if (parsed < 0)
		return parsed;

	memcpy(config, ptr + parsed, *ptr);
	len -= *ptr;
	parsed += *ptr;

	if (config->bNumInterfaces > USB_MAXINTERFACES)
	{
		printk(KERN_WARNING "usb: too many interfaces.\n");
		return -1;

	}

	config->interface = (struct usb_interface_descriptor *)
		kmalloc(config->bNumInterfaces * sizeof(struct usb_interface_descriptor), GFP_KERNEL);
	if(config->interface==NULL)
	{
		printk(KERN_WARNING "usb: out of memory.\n");
		return -1;	
	}
	memset(config->interface, 0, config->bNumInterfaces*sizeof(struct usb_interface_descriptor));
	
	for (i = 0; i < config->bNumInterfaces; i++) {
		int retval = usb_parse_interface(dev, config->interface + i, ptr + parsed, len);
		if (retval < 0)
			return parsed; // HACK
//			return retval;
		parsed += retval;
		len -= retval;
	}
	return parsed;
}

int usb_parse_configuration(struct usb_device *dev, void *__buf, int bytes)
{
	int i;
	unsigned char *ptr = __buf;

	if (dev->descriptor.bNumConfigurations > USB_MAXCONFIG)
	{
		printk(KERN_WARNING "usb: too many configurations.\n");
		return -1;
	}

	dev->config = (struct usb_config_descriptor *)
		kmalloc(dev->descriptor.bNumConfigurations * sizeof(struct usb_config_descriptor), GFP_KERNEL);
	if(dev->config==NULL)
	{
		printk(KERN_WARNING "usb: out of memory.\n");
		return -1;	
	}
	memset(dev->config, 0, dev->descriptor.bNumConfigurations*sizeof(struct usb_config_descriptor));
	for (i = 0; i < dev->descriptor.bNumConfigurations; i++) {
		int retval = usb_parse_config(dev, dev->config + i, ptr, bytes);
		if (retval < 0)
			return retval;
		ptr += retval;
		bytes += retval;
	}
	return 0;
}

void usb_destroy_configuration(struct usb_device *dev)
{
	int c, i;
	struct usb_config_descriptor *cf;
	struct usb_interface_descriptor *ifp;
	
	if(dev->config==NULL)
		return;
	for(c=0;c<dev->descriptor.bNumConfigurations;c++)
	{
		cf=&dev->config[c];
		if(cf->interface==NULL)
			break;
		for(i=0;i<cf->bNumInterfaces;i++)
		{
			ifp=&cf->interface[i];
			if(ifp->endpoint==NULL)
				break;
			kfree(ifp->endpoint);
		}
		kfree(cf->interface);
	}
	kfree(dev->config);
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

	dr.requesttype = 0x80;
	dr.request = USB_REQ_GET_DESCRIPTOR;
	dr.value = (type << 8) + index;
	dr.index = 0;
	dr.length = size;

	return dev->bus->op->control_msg(dev, usb_rcvctrlpipe(dev,0), &dr, buf, size);
}

int usb_get_device_descriptor(struct usb_device *dev)
{
	return usb_get_descriptor(dev, USB_DT_DEVICE, 0, &dev->descriptor, sizeof(dev->descriptor));
}

int usb_get_hub_descriptor(struct usb_device *dev, void *data, int size)
{
	devrequest dr;

	dr.requesttype = USB_RT_HUB | 0x80;
	dr.request = USB_REQ_GET_DESCRIPTOR;
	dr.value = (USB_DT_HUB << 8);
	dr.index = 0;
	dr.length = size;

	return dev->bus->op->control_msg(dev, usb_rcvctrlpipe(dev,0), &dr, data, size);
}

int usb_clear_port_feature(struct usb_device *dev, int port, int feature)
{
	devrequest dr;

	dr.requesttype = USB_RT_PORT;
	dr.request = USB_REQ_CLEAR_FEATURE;
	dr.value = feature;
	dr.index = port;
	dr.length = 0;

	return dev->bus->op->control_msg(dev, usb_sndctrlpipe(dev,0), &dr, NULL, 0);
}

int usb_set_port_feature(struct usb_device *dev, int port, int feature)
{
	devrequest dr;

	dr.requesttype = USB_RT_PORT;
	dr.request = USB_REQ_SET_FEATURE;
	dr.value = feature;
	dr.index = port;
	dr.length = 0;

	return dev->bus->op->control_msg(dev, usb_sndctrlpipe(dev,0), &dr, NULL, 0);
}

int usb_get_hub_status(struct usb_device *dev, void *data)
{
	devrequest dr;

	dr.requesttype = USB_RT_HUB | 0x80;
	dr.request = USB_REQ_GET_STATUS;
	dr.value = 0;
	dr.index = 0;
	dr.length = 4;

	return dev->bus->op->control_msg(dev, usb_rcvctrlpipe(dev,0), &dr, data, 4);
}

int usb_get_port_status(struct usb_device *dev, int port, void *data)
{
	devrequest dr;

	dr.requesttype = USB_RT_PORT | 0x80;
	dr.request = USB_REQ_GET_STATUS;
	dr.value = 0;
	dr.index = port;
	dr.length = 4;

	return dev->bus->op->control_msg(dev, usb_rcvctrlpipe(dev,0), &dr, data, 4);
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

int usb_set_configuration(struct usb_device *dev, int configuration)
{
	devrequest dr;

	dr.requesttype = 0;
	dr.request = USB_REQ_SET_CONFIGURATION;
	dr.value = configuration;
	dr.index = 0;
	dr.length = 0;

	if (dev->bus->op->control_msg(dev, usb_sndctrlpipe(dev, 0), &dr, NULL, 0))
		return -1;

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
	unsigned int cfgno,size;
	unsigned char buffer[400];
	unsigned char * bufptr;
	
	bufptr=buffer;
        for (cfgno=0;cfgno<dev->descriptor.bNumConfigurations;cfgno++) {
  		/* Get the first 8 bytes - guaranteed */
	  	if (usb_get_descriptor(dev, USB_DT_CONFIG, cfgno, bufptr, 8))
	    		return -1;

  	  	/* Get the full buffer */
	  	size = *(unsigned short *)(bufptr+2);
	  	if (bufptr+size > buffer+sizeof(buffer))
	  	{
			printk(KERN_INFO "usb: truncated DT_CONFIG (want %d).\n", size);
			size = buffer+sizeof(buffer)-bufptr;
		}
		if (usb_get_descriptor(dev, USB_DT_CONFIG, cfgno, bufptr, size))
			return -1;
			
		/* Prepare for next configuration */
		bufptr+=size;
	}
	return usb_parse_configuration(dev, buffer, size);
}

/*
 * By the time we get here, the device has gotten a new device ID
 * and is in the default state. We need to identify the thing and
 * get the ball rolling..
 */
void usb_new_device(struct usb_device *dev)
{
	int addr, i;

	printk("USB new device connect, assigned device number %d\n",
		dev->devnum);

	dev->maxpacketsize = 0;		/* Default to 8 byte max packet size */

	addr = dev->devnum;
	dev->devnum = 0;

	/* Slow devices */
	for (i = 0; i < 5; i++) {
		if (!usb_get_descriptor(dev, USB_DT_DEVICE, 0, &dev->descriptor, 8))
			break;

		printk("get_descriptor failed, waiting\n");
		wait_ms(200);
	}
	if (i == 5) {
		printk("giving up\n");
		return;
	}

#if 0
	printk("maxpacketsize: %d\n", dev->descriptor.bMaxPacketSize0);
#endif
	switch (dev->descriptor.bMaxPacketSize0) {
		case 8: dev->maxpacketsize = 0; break;
		case 16: dev->maxpacketsize = 1; break;
		case 32: dev->maxpacketsize = 2; break;
		case 64: dev->maxpacketsize = 3; break;
	}
#if 0
	printk("dev->mps: %d\n", dev->maxpacketsize);
#endif

	dev->devnum = addr;

	if (usb_set_address(dev)) {
		printk("Unable to set address\n");
		/* FIXME: We should disable the port */
		return;
	}

	wait_ms(10);	/* Let the SET_ADDRESS settle */

	if (usb_get_device_descriptor(dev)) {
		printk("Unable to get device descriptor\n");
		return;
	}

	if (usb_get_configuration(dev)) {
		printk("Unable to get configuration\n");
		return;
	}

#if 0
	printk("Vendor: %X\n", dev->descriptor.idVendor);
	printk("Product: %X\n", dev->descriptor.idProduct);
#endif

	usb_device_descriptor(dev);
}

int usb_request_irq(struct usb_device *dev, unsigned int pipe, usb_device_irq handler, int period, void *dev_id)
{
	return dev->bus->op->request_irq(dev, pipe, handler, period, dev_id);
}

