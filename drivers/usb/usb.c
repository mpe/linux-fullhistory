/*
 * drivers/usb/usb.c
 *
 * (C) Copyright Linus Torvalds 1999
 * (C) Copyright Johannes Erdfelt 1999
 * (C) Copyright Andreas Gal 1999
 * (C) Copyright Gregory P. Smith 1999
 * (C) Copyright Deti Fliegl 1999 (new USB architecture)
 *
 * NOTE! This is not actually a driver at all, rather this is
 * just a collection of helper routines that implement the
 * generic USB things that the real drivers can use..
 *
 * Think of this as a "USB library" rather than anything else.
 * It should be considered a slave, with no callbacks. Callbacks
 * are evil.
 *
 * $Id: usb.c,v 1.53 2000/01/14 16:19:09 acher Exp $
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/bitops.h>
#include <linux/malloc.h>
#include <linux/interrupt.h>  /* for in_interrupt() */

#define DEBUG

#include "usb.h"

/*
 * Prototypes for the device driver probing/loading functions
 */
static void usb_find_drivers(struct usb_device *);
static int  usb_find_interface_driver(struct usb_device *, unsigned int);
static void usb_check_support(struct usb_device *);

/*
 * We have a per-interface "registered driver" list.
 */
LIST_HEAD(usb_driver_list);
LIST_HEAD(usb_bus_list);

static struct usb_busmap busmap;

static struct usb_driver *usb_minors[16];

int usb_register(struct usb_driver *new_driver)
{
	struct list_head *tmp;

	if (new_driver->fops != NULL) {
		if (usb_minors[new_driver->minor/16]) {
			 err("error registering %s driver", new_driver->name);
			return -EINVAL;
		}
		usb_minors[new_driver->minor/16] = new_driver;
	}

	info("registered new driver %s", new_driver->name);
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

/*
 * This function is part of a depth-first search down the device tree,
 * removing any instances of a device driver.
 */
static void usb_drivers_purge(struct usb_driver *driver,struct usb_device *dev)
{
	int i;

	if (!dev) {
		err("null device being purged!!!");
		return;
	}

	for (i=0; i<USB_MAXCHILDREN; i++)
		if (dev->children[i])
			usb_drivers_purge(driver, dev->children[i]);

	if (!dev->actconfig)
		return;
			
	for (i = 0; i < dev->actconfig->bNumInterfaces; i++) {
		struct usb_interface *interface = &dev->actconfig->interface[i];
		
		if (interface->driver == driver) {
			driver->disconnect(dev, interface->private_data);
			usb_driver_release_interface(driver, interface);
			/*
			 * This will go through the list looking for another
			 * driver that can handle the device
			 */
			usb_find_interface_driver(dev, i);
		}
	}
}

/*
 * Unlink a driver from the driver list when it is unloaded
 */
void usb_deregister(struct usb_driver *driver)
{
	struct list_head *tmp;

	info("deregistering driver %s", driver->name);
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
		usb_drivers_purge(driver, bus->root_hub);
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

	dbg("usb-bandwidth-alloc: was: %u, new: %u, "
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
	bus->busnum = -1;
	bus->bandwidth_allocated = 0;
	bus->bandwidth_int_reqs  = 0;
	bus->bandwidth_isoc_reqs = 0;

	INIT_LIST_HEAD(&bus->bus_list);
        INIT_LIST_HEAD(&bus->inodes);

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
	int busnum;

	busnum = find_next_zero_bit(busmap.busmap, USB_MAXBUS, 1);
	if (busnum < USB_MAXBUS) {
		set_bit(busnum, busmap.busmap);
		bus->busnum = busnum;
	} else
		warn("too many buses");

	/* Add it to the list of buses */
	list_add(&bus->bus_list, &usb_bus_list);

	usbdevfs_add_bus(bus);

	info("new USB bus registered, assigned bus number %d", bus->busnum);
}

void usb_deregister_bus(struct usb_bus *bus)
{
	info("USB bus %d deregistered", bus->busnum);

	/*
	 * NOTE: make sure that all the devices are removed by the
	 * controller code, as well as having it call this when cleaning
	 * itself up
	 */
	list_del(&bus->bus_list);

        usbdevfs_remove_bus(bus);

	clear_bit(bus->busnum, busmap.busmap);
}

/*
 * This function is for doing a depth-first search for devices which
 * have support, for dynamic loading of driver modules.
 */
static void usb_check_support(struct usb_device *dev)
{
	int i;

	if (!dev) {
		err("null device being checked!!!");
		return;
	}

	for (i=0; i<USB_MAXCHILDREN; i++)
		if (dev->children[i])
			usb_check_support(dev->children[i]);

	if (!dev->actconfig)
		return;

	/* now we check this device */
	if (dev->devnum > 0)
		for (i = 0; i < dev->actconfig->bNumInterfaces; i++)
			usb_find_interface_driver(dev, i);
}


/*
 * This is intended to be used by usb device drivers that need to
 * claim more than one interface on a device at once when probing
 * (audio and acm are good examples).  No device driver should have
 * to mess with the internal usb_interface or usb_device structure
 * members.
 */
void usb_driver_claim_interface(struct usb_driver *driver, struct usb_interface *iface, void* priv)
{
	if (!iface || !driver)
		return;

	dbg("%s driver claimed interface %p", driver->name, iface);

	iface->driver = driver;
	iface->private_data = priv;
} /* usb_driver_claim_interface() */

/*
 * This should be used by drivers to check other interfaces to see if
 * they are available or not.
 */
int usb_interface_claimed(struct usb_interface *iface)
{
	if (!iface)
		return 0;

	return (iface->driver != NULL);
} /* usb_interface_claimed() */

/*
 * This should be used by drivers to release their claimed interfaces
 */
void usb_driver_release_interface(struct usb_driver *driver, struct usb_interface *iface)
{
	/* this should never happen, don't release something that's not ours */
	if (iface->driver != driver || !iface)
		return;

	iface->driver = NULL;
	iface->private_data = NULL;
}

/*
 * This entrypoint gets called for each new device.
 *
 * We now walk the list of registered USB drivers,
 * looking for one that will accept this interface.
 *
 * The probe return value is changed to be a private pointer.  This way
 * the drivers don't have to dig around in our structures to set the
 * private pointer if they only need one interface. 
 *
 * Returns: 0 if a driver accepted the interface, -1 otherwise
 */
static int usb_find_interface_driver(struct usb_device *dev, unsigned ifnum)
{
	struct list_head *tmp = usb_driver_list.next;
	struct usb_interface *interface;
	
	if ((!dev) || (ifnum >= dev->actconfig->bNumInterfaces)) {
		err("bad find_interface_driver params");
		return -1;
	}

	interface = dev->actconfig->interface + ifnum;

	if (usb_interface_claimed(interface))
		return -1;

	while (tmp != &usb_driver_list) {
		void *private;
		struct usb_driver *driver = list_entry(tmp, struct usb_driver,
		  				       driver_list);
			
		tmp = tmp->next;
		if (!(private = driver->probe(dev, ifnum)))
			continue;
		usb_driver_claim_interface(driver, interface, private);

		return 0;
	}
	
	return -1;
}

/*
 * This entrypoint gets called for each new device.
 *
 * All interfaces are scanned for matching drivers.
 */
static void usb_find_drivers(struct usb_device *dev)
{
	unsigned ifnum;
	unsigned rejected = 0;

	for (ifnum = 0; ifnum < dev->actconfig->bNumInterfaces; ifnum++) {
		/* if this interface hasn't already been claimed */
		if (!usb_interface_claimed(dev->actconfig->interface + ifnum)) {
			if (usb_find_interface_driver(dev, ifnum))
				rejected++;
		}
	}
 
	if (rejected)
		dbg("unhandled interfaces on device");

#ifdef DEBUG
	usb_show_device(dev);
#endif
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
        INIT_LIST_HEAD(&dev->inodes);
        INIT_LIST_HEAD(&dev->filelist);

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
/* ------------------------------------------------------------------------------------- 
 * New USB Core Functions
 * -------------------------------------------------------------------------------------*/

urb_t* usb_alloc_urb(int iso_packets)
{
	urb_t *urb;
	urb=(urb_t*)kmalloc(sizeof(urb_t) + iso_packets*sizeof(iso_packet_descriptor_t),
	      in_interrupt() ? GFP_ATOMIC : GFP_KERNEL);
	if (!urb)
	{
		err("alloc_urb: kmalloc failed");
		return 0;
	}
	memset(urb,0,sizeof(urb_t));
	return urb;
}

/*-------------------------------------------------------------------*/
void usb_free_urb(urb_t* urb)
{
	if(urb)
		kfree(urb);
}
/*-------------------------------------------------------------------*/
int usb_submit_urb(urb_t *urb)
{
	if(urb && urb->dev)
		return urb->dev->bus->op->submit_urb(urb);
	else
		return -1;
}

/*-------------------------------------------------------------------*/
int usb_unlink_urb(urb_t *urb)
{
	if(urb && urb->dev)
		return urb->dev->bus->op->unlink_urb(urb);
	else
		return -1;
}
/*-------------------------------------------------------------------*
 *                     COMPLETION HANDLERS                           *
 *-------------------------------------------------------------------*/

/*-------------------------------------------------------------------*
 * completion handler for compatibility wrappers (sync control/bulk) *
 *-------------------------------------------------------------------*/
static void usb_api_blocking_completion(urb_t *urb)
{
	api_wrapper_data *awd = (api_wrapper_data *)urb->context;

	if (waitqueue_active(awd->wakeup))
		wake_up(awd->wakeup);
#if 0
	else
		dbg("(blocking_completion): waitqueue empty!"); 
		// even occurs if urb was unlinked by timeout...
#endif
}

/*-------------------------------------------------------------------*
 * completion handler for compatibility wrappers (async bulk)        *
 *-------------------------------------------------------------------*/
static void usb_api_async_completion(urb_t *urb)
{
	api_wrapper_data *awd=(api_wrapper_data*)urb->context;

	if (awd->handler)
		awd->handler(urb->status,urb->transfer_buffer,urb->actual_length,awd->stuff);
 }

/*-------------------------------------------------------------------*
 *                         COMPATIBILITY STUFF                       *
 *-------------------------------------------------------------------*/

// Starts urb and waits for completion or timeout
static int usb_start_wait_urb(urb_t *urb, int timeout, int* actual_length)
{ 
	DECLARE_WAITQUEUE(wait, current);
	DECLARE_WAIT_QUEUE_HEAD(wqh);
	api_wrapper_data awd;
	int status;
  
	awd.wakeup=&wqh;
	awd.handler=0;
	init_waitqueue_head(&wqh); 	
	current->state = TASK_INTERRUPTIBLE;
	add_wait_queue(&wqh, &wait);
	urb->context=&awd;
	status=usb_submit_urb(urb);
	if (status) {
		// something went wrong
		usb_free_urb(urb);
		remove_wait_queue(&wqh, &wait);
		return status;
	}

	if (urb->status == -EINPROGRESS) {
		while (timeout && urb->status == -EINPROGRESS)
			status = timeout = schedule_timeout(timeout);
	} else
		status = 1;

	remove_wait_queue(&wqh, &wait);

	if (!status) {
		// timeout
		printk("usb_control/bulk_msg: timeout\n");
		usb_unlink_urb(urb);  // remove urb safely
		status=-ETIMEDOUT;
	}
	else
		status=urb->status;

	if (actual_length)
		*actual_length=urb->actual_length;

	usb_free_urb(urb);
  	return status;
}

/*-------------------------------------------------------------------*/
// returns status (negative) are length (positive)
int usb_internal_control_msg(struct usb_device *usb_dev, unsigned int pipe, 
			    devrequest *cmd,  void *data, int len, int timeout)
{
	urb_t *urb;
	int retv;
	int length;

	urb=usb_alloc_urb(0);
	if (!urb)
		return -ENOMEM;
  
	FILL_CONTROL_URB(urb, usb_dev, pipe, (unsigned char*)cmd, data, len,    /* build urb */  
		   (usb_complete_t)usb_api_blocking_completion,0);

	retv=usb_start_wait_urb(urb,timeout, &length);
	if (retv < 0)
		return retv;
	else
		return length;
	
}
/*-------------------------------------------------------------------*/
int usb_control_msg(struct usb_device *dev, unsigned int pipe, __u8 request, __u8 requesttype,
			 __u16 value, __u16 index, void *data, __u16 size, int timeout)
{
	devrequest *dr = kmalloc(sizeof(devrequest), GFP_KERNEL);
	int ret;
	
	if(!dr)
		return -ENOMEM;

	dr->requesttype = requesttype;
	dr->request = request;
	dr->value = cpu_to_le16p(&value);
	dr->index = cpu_to_le16p(&index);
	dr->length = cpu_to_le16p(&size);

	//dbg("usb_control_msg");	

	ret=usb_internal_control_msg(dev, pipe, dr, data, size, timeout);

	kfree(dr);

	return ret;
}

/*-------------------------------------------------------------------*/
/* compatibility wrapper, builds bulk urb, and waits for completion */
/* synchronous behavior */

int usb_bulk_msg(struct usb_device *usb_dev, unsigned int pipe, 
			void *data, int len, int *actual_length, int timeout)
{
	urb_t *urb;

	if (len < 0)
		return -EINVAL;

	urb=usb_alloc_urb(0);
	if (!urb)
		return -ENOMEM;

	FILL_BULK_URB(urb,usb_dev,pipe,(unsigned char*)data,len,   /* build urb */
			(usb_complete_t)usb_api_blocking_completion,0);

	return usb_start_wait_urb(urb,timeout,actual_length);
}
/*-------------------------------------------------------------------*/

void *usb_request_bulk(struct usb_device *dev, unsigned int pipe, usb_device_irq handler, void *data, int len, void *dev_id)
{
	urb_t *urb;
	api_wrapper_data *awd;

	if (!(urb=usb_alloc_urb(0)))
		return NULL;
	if (!(awd = kmalloc(sizeof(api_wrapper_data), in_interrupt() ? GFP_ATOMIC : GFP_KERNEL))) {
		kfree(urb);
		return NULL;
	}

	/* build urb */
	FILL_BULK_URB(urb, dev, pipe, data, len, (usb_complete_t)usb_api_async_completion, awd);

	awd->handler=handler;
	awd->stuff=dev_id;
	if (usb_submit_urb(urb) < 0) {
		kfree(awd);
		kfree(urb);
		return NULL;
	}
	return urb;
}

// compatibility wrapper. Remove urb only if it is called before the
// transaction's completion interrupt. If called from within the
// completion handler (urb->completed==1), it does nothing, since the
// qh is already removed 

int usb_terminate_bulk(struct usb_device *dev, void *first)
{
	urb_t *urb=(urb_t*)first;
	dbg("usb_terminate_bulk: urb:%p",urb);
	if (!urb) // none found? there is nothing to remove!
		return -ENODEV;
  
	usb_unlink_urb(urb);
	kfree(urb->context);
	kfree(urb);
	return USB_ST_NOERROR;
}

/*
 * usb_release_bandwidth():
 *
 * called to release an interrupt pipe's bandwidth (in microseconds)
 */
void usb_release_bandwidth(struct usb_device *dev, int bw_alloc)
{
	dev->bus->bandwidth_allocated -= bw_alloc;
	dev->bus->bandwidth_int_reqs--;
	dbg("bw_alloc reduced to %d for %d requesters",
		dev->bus->bandwidth_allocated,
		dev->bus->bandwidth_int_reqs +
		dev->bus->bandwidth_isoc_reqs);
}

static void irq_callback(urb_t *urb)
{
	struct irq_wrapper_data *wd = (struct irq_wrapper_data *)urb->context;

	if (!wd->handler)
		return;
#if 0 // verbose...
	if (!wd->handler(urb->status, urb->transfer_buffer, urb->actual_length, wd->context))
		err("legacy irq callback returned 0!!!");
#else
	wd->handler(urb->status, urb->transfer_buffer, urb->actual_length, wd->context);
#endif
}

int usb_request_irq(struct usb_device *dev, unsigned int pipe, usb_device_irq handler, int period, void *dev_id, void **handle)
{
	long    bustime;
	int     ret;
	struct irq_wrapper_data *wd;
	urb_t *urb;
	unsigned int maxsze = usb_maxpacket(dev, pipe, usb_pipeout(pipe));

	*handle = NULL;
	
	//dbg("irq: dev:%p pipe:%08X handler:%p period:%d dev_id:%p max:%d", dev, pipe, handler, period, dev_id, maxsze);
	
	/* Check host controller's bandwidth for this int. request. */
	bustime = calc_bus_time (usb_pipeslow(pipe), usb_pipein(pipe), 0,
			usb_maxpacket(dev, pipe, usb_pipeout(pipe)));
	bustime = NS_TO_US(bustime);	/* work in microseconds */
	if (check_bandwidth_alloc (dev->bus->bandwidth_allocated, bustime))
		return -EUSERS;  // no bandwidth left

	if (!maxsze || !usb_pipeint(pipe))
		return -EINVAL;

	if (!(urb = usb_alloc_urb(0)))
		return -ENOMEM;

	if (!(wd = kmalloc(sizeof(struct irq_wrapper_data), in_interrupt() ? GFP_ATOMIC : GFP_KERNEL))) {
		kfree(urb);
		return -ENOMEM;
	}
	if (!(urb->transfer_buffer = kmalloc(maxsze, in_interrupt() ? GFP_ATOMIC : GFP_KERNEL))) {
		kfree(urb);
		kfree(wd);
		return -ENOMEM;
	}
	wd->handler=handler;
	wd->context=dev_id;
	urb->dev = dev;
	urb->pipe = pipe;
	urb->transfer_buffer_length = urb->actual_length = maxsze;
	urb->interval = period;
	urb->context = wd;
	urb->complete = irq_callback;
	if ((ret = usb_submit_urb(urb)) < 0) {
		kfree(wd);
		kfree(urb->transfer_buffer);
		kfree(urb);
		return ret;
	}
	*handle = urb;

	/* Claim the USB bandwidth if no error. */
	if (!ret) {
		dev->bus->bandwidth_allocated += bustime;
		dev->bus->bandwidth_int_reqs++;
		dbg("bw_alloc bumped to %d for %d requesters",
			dev->bus->bandwidth_allocated,
			dev->bus->bandwidth_int_reqs +
			dev->bus->bandwidth_isoc_reqs);
	}

	return ret;
}

int usb_release_irq(struct usb_device *dev, void *handle, unsigned int pipe)
{
	long    bustime;
	int	err;
	urb_t *urb = (urb_t*)handle;

	if (!urb)
		return -EBADF;
	err=usb_unlink_urb(urb);
	kfree(urb->context);
	kfree(urb->transfer_buffer);
	kfree(urb);

	/* Return the USB bandwidth if no error. */
	if (!err) {
		bustime = calc_bus_time (usb_pipeslow(pipe), usb_pipein(pipe), 0,
				usb_maxpacket(dev, pipe, usb_pipeout(pipe)));
		bustime = NS_TO_US(bustime);	/* work in microseconds */
		usb_release_bandwidth(dev, bustime);
	}
	return err;
}

/*
 * usb_get_current_frame_number()
 *
 * returns the current frame number for the parent USB bus/controller
 * of the given USB device.
 */
int usb_get_current_frame_number(struct usb_device *usb_dev)
{
	return usb_dev->bus->op->get_frame_number (usb_dev);
}
/*-------------------------------------------------------------------*/
static int usb_parse_endpoint(struct usb_device *dev, struct usb_endpoint_descriptor *endpoint, unsigned char *buffer, int size)
{
	struct usb_descriptor_header *header;
	unsigned char *begin;
	int parsed = 0, len, numskipped;

	header = (struct usb_descriptor_header *)buffer;

	/* Everything should be fine being passed into here, but we sanity */
	/*  check JIC */
	if (header->bLength > size) {
		err("ran out of descriptors parsing");
		return -1;
	}
		
	if (header->bDescriptorType != USB_DT_ENDPOINT) {
		warn("unexpected descriptor 0x%X, expecting endpoint descriptor, type 0x%X",
			endpoint->bDescriptorType, USB_DT_ENDPOINT);
		return parsed;
	}

	if (header->bLength == USB_DT_ENDPOINT_AUDIO_SIZE)
		memcpy(endpoint, buffer, USB_DT_ENDPOINT_AUDIO_SIZE);
	else
		memcpy(endpoint, buffer, USB_DT_ENDPOINT_SIZE);
	
	le16_to_cpus(&endpoint->wMaxPacketSize);

	buffer += header->bLength;
	size -= header->bLength;
	parsed += header->bLength;

	/* Skip over the rest of the Class Specific or Vendor Specific */
	/*  descriptors */
	begin = buffer;
	numskipped = 0;
	while (size >= sizeof(struct usb_descriptor_header)) {
		header = (struct usb_descriptor_header *)buffer;

		if (header->bLength < 2) {
			err("invalid descriptor length of %d", header->bLength);
			return -1;
		}

		/* If we find another descriptor which is at or below us */
		/*  in the descriptor heirarchy then we're done  */
		if ((header->bDescriptorType == USB_DT_ENDPOINT) ||
		    (header->bDescriptorType == USB_DT_INTERFACE) ||
		    (header->bDescriptorType == USB_DT_CONFIG) ||
		    (header->bDescriptorType == USB_DT_DEVICE))
			break;

		dbg("skipping descriptor 0x%X",
			header->bDescriptorType);
		numskipped++;

		buffer += header->bLength;
		size -= header->bLength;
		parsed += header->bLength;
	}
	if (numskipped)
		dbg("skipped %d class/vendor specific endpoint descriptors", numskipped);

	/* Copy any unknown descriptors into a storage area for drivers */
	/*  to later parse */
	len = (int)(buffer - begin);
	if (!len) {
		endpoint->extra = NULL;
		endpoint->extralen = 0;
		return parsed;
	}

	endpoint->extra = kmalloc(len, GFP_KERNEL);

	if (!endpoint->extra) {
		err("couldn't allocate memory for endpoint extra descriptors");
		endpoint->extralen = 0;
		return parsed;
	}

	memcpy(endpoint->extra, begin, len);
	endpoint->extralen = len;

	return parsed;
}

static int usb_parse_interface(struct usb_device *dev, struct usb_interface *interface, unsigned char *buffer, int size)
{
	int i, len, numskipped, retval, parsed = 0;
	struct usb_descriptor_header *header;
	struct usb_interface_descriptor *ifp;
	unsigned char *begin;

	interface->act_altsetting = 0;
	interface->num_altsetting = 0;
	interface->max_altsetting = USB_ALTSETTINGALLOC;

	interface->altsetting = kmalloc(sizeof(struct usb_interface_descriptor) * interface->max_altsetting, GFP_KERNEL);
	
	if (!interface->altsetting) {
		err("couldn't kmalloc interface->altsetting");
		return -1;
	}

	while (size > 0) {
		if (interface->num_altsetting >= interface->max_altsetting) {
			void *ptr;
			int oldmas;

			oldmas = interface->max_altsetting;
			interface->max_altsetting += USB_ALTSETTINGALLOC;
			if (interface->max_altsetting > USB_MAXALTSETTING) {
				warn("too many alternate settings (max %d)",
					USB_MAXALTSETTING);
				return -1;
			}

			ptr = interface->altsetting;
			interface->altsetting = kmalloc(sizeof(struct usb_interface_descriptor) * interface->max_altsetting, GFP_KERNEL);
			if (!interface->altsetting) {
				err("couldn't kmalloc interface->altsetting");
				interface->altsetting = ptr;
				return -1;
			}
			memcpy(interface->altsetting, ptr, sizeof(struct usb_interface_descriptor) * oldmas);

			kfree(ptr);
		}

		ifp = interface->altsetting + interface->num_altsetting;
		interface->num_altsetting++;

		memcpy(ifp, buffer, USB_DT_INTERFACE_SIZE);

		/* Skip over the interface */
		buffer += ifp->bLength;
		parsed += ifp->bLength;
		size -= ifp->bLength;

		begin = buffer;
		numskipped = 0;

		/* Skip over at Interface class or vendor descriptors */
		while (size >= sizeof(struct usb_descriptor_header)) {
			header = (struct usb_descriptor_header *)buffer;

			if (header->bLength < 2) {
				err("invalid descriptor length of %d", header->bLength);
				return -1;
			}

			/* If we find another descriptor which is at or below */
			/*  us in the descriptor heirarchy then return */
			if ((header->bDescriptorType == USB_DT_INTERFACE) ||
			    (header->bDescriptorType == USB_DT_ENDPOINT) ||
			    (header->bDescriptorType == USB_DT_CONFIG) ||
			    (header->bDescriptorType == USB_DT_DEVICE))
				break;

			numskipped++;

			buffer += header->bLength;
			parsed += header->bLength;
			size -= header->bLength;
		}

		if (numskipped)
			dbg("skipped %d class/vendor specific interface descriptors", numskipped);

		/* Copy any unknown descriptors into a storage area for */
		/*  drivers to later parse */
		len = (int)(buffer - begin);
		if (!len) {
			ifp->extra = NULL;
			ifp->extralen = 0;
		} else {
			ifp->extra = kmalloc(len, GFP_KERNEL);

			if (!ifp->extra) {
				err("couldn't allocate memory for interface extra descriptors");
				ifp->extralen = 0;
				return -1;
			}
			memcpy(ifp->extra, begin, len);
			ifp->extralen = len;
		}

		/* Did we hit an unexpected descriptor? */
		header = (struct usb_descriptor_header *)buffer;
		if ((size >= sizeof(struct usb_descriptor_header)) &&
		    ((header->bDescriptorType == USB_DT_CONFIG) ||
		     (header->bDescriptorType == USB_DT_DEVICE)))
			return parsed;

		if (ifp->bNumEndpoints > USB_MAXENDPOINTS) {
			warn("too many endpoints");
			return -1;
		}

		ifp->endpoint = (struct usb_endpoint_descriptor *)
			kmalloc(ifp->bNumEndpoints *
			sizeof(struct usb_endpoint_descriptor), GFP_KERNEL);
		if (!ifp->endpoint) {
			err("out of memory");
			return -1;	
		}

		memset(ifp->endpoint, 0, ifp->bNumEndpoints *
			sizeof(struct usb_endpoint_descriptor));
	
		for (i = 0; i < ifp->bNumEndpoints; i++) {
			header = (struct usb_descriptor_header *)buffer;

			if (header->bLength > size) {
				err("ran out of descriptors parsing");
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

int usb_parse_configuration(struct usb_device *dev, struct usb_config_descriptor *config, char *buffer)
{
	int i;
	int retval;
	int size;
	struct usb_descriptor_header *header;

	memcpy(config, buffer, USB_DT_INTERFACE_SIZE);
	le16_to_cpus(&config->wTotalLength);
	size = config->wTotalLength;

	if (config->bNumInterfaces > USB_MAXINTERFACES) {
		warn("too many interfaces");
		return -1;
	}

	config->interface = (struct usb_interface *)
		kmalloc(config->bNumInterfaces *
		sizeof(struct usb_interface), GFP_KERNEL);
	dbg("kmalloc IF %p, numif %i",config->interface,config->bNumInterfaces);
	if (!config->interface) {
		err("out of memory");
		return -1;	
	}

	memset(config->interface, 0,
	       config->bNumInterfaces * sizeof(struct usb_interface));

	buffer += config->bLength;
	size -= config->bLength;
	
	for (i = 0; i < config->bNumInterfaces; i++) {
		header = (struct usb_descriptor_header *)buffer;
		if ((header->bLength > size) || (header->bLength <= 2)) {
			err("ran out of descriptors parsing");
			return -1;
		}
		
		if (header->bDescriptorType != USB_DT_INTERFACE) {
			warn("unexpected descriptor 0x%X",
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
	int c, i, j, k;
	
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
					
				if(as->extra) {
					kfree(as->extra);
				}

				if (!as->endpoint)
					break;
					
				for(k = 0; k < as->bNumEndpoints; k++) {
					if(as->endpoint[k].extra) {
						kfree(as->endpoint[k].extra);
					}
				}	
				kfree(as->endpoint);
			}

			kfree(ifp->altsetting);
		}
		kfree(cf->interface);
	}
	kfree(dev->config);
}
			
void usb_init_root_hub(struct usb_device *dev)
{
	dev->devnum = -1;
	dev->slow = 0;
	dev->actconfig = NULL;
}

/*
 * __usb_get_extra_descriptor() finds a descriptor of specific type in the
 * extra field of the interface and endpoint descriptor structs.
 */

int __usb_get_extra_descriptor(char *buffer, unsigned size, unsigned char type, void **ptr)
{
	struct usb_descriptor_header *header;

	while (size >= sizeof(struct usb_descriptor_header)) {
		header = (struct usb_descriptor_header *)buffer;

		if (header->bLength < 2) {
			err("invalid descriptor length of %d", header->bLength);
			return -1;
		}

		if (header->bDescriptorType == type) {
			*ptr = header;
			return 0;
		}

		buffer += header->bLength;
		size -= header->bLength;
	}
	return -1;
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

	info("USB disconnect on device %d", dev->devnum);

	if (dev->actconfig) {
		for (i = 0; i < dev->actconfig->bNumInterfaces; i++) {
			struct usb_interface *interface = &dev->actconfig->interface[i];
			struct usb_driver *driver = interface->driver;
			if (driver) {
				driver->disconnect(dev, interface->private_data);
				usb_driver_release_interface(driver, interface);
			}
		}
	}

	/* Free up all the children.. */
	for (i = 0; i < USB_MAXCHILDREN; i++) {
		struct usb_device **child = dev->children + i;
		if (*child)
			usb_disconnect(child);
	}

	/* remove /proc/bus/usb entry */
        usbdevfs_remove_device(dev);

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
	// FIXME needs locking for SMP!!
	/* why? this is called only from the hub thread, 
	 * which hopefully doesn't run on multiple CPU's simulatenously 8-)
	 */
	dev->descriptor.bMaxPacketSize0 = 8;  /* Start off at 8 bytes  */
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

#define GET_TIMEOUT 3
#define SET_TIMEOUT 3

int usb_set_address(struct usb_device *dev)
{
	return usb_control_msg(dev, usb_snddefctrl(dev), USB_REQ_SET_ADDRESS,
		0, dev->devnum, 0, NULL, 0, HZ * GET_TIMEOUT);
}

int usb_get_descriptor(struct usb_device *dev, unsigned char type, unsigned char index, void *buf, int size)
{
	int i = 5;
	int result;
	
	memset(buf,0,size);	// Make sure we parse really received data

	while (i--) {
		if ((result = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
			USB_REQ_GET_DESCRIPTOR, USB_DIR_IN,
			(type << 8) + index, 0, buf, size, HZ * GET_TIMEOUT)) >= 0 ||
		     result == -EPIPE)
			break;
	}
	return result;
}

int usb_get_class_descriptor(struct usb_device *dev, int ifnum,
		unsigned char type, unsigned char id, void *buf, int size)
{
	return usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
		USB_REQ_GET_DESCRIPTOR, USB_RECIP_INTERFACE | USB_DIR_IN,
		(type << 8) + id, ifnum, buf, size, HZ * GET_TIMEOUT);
}

int usb_get_string(struct usb_device *dev, unsigned short langid, unsigned char index, void *buf, int size)
{
	return usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
		USB_REQ_GET_DESCRIPTOR, USB_DIR_IN,
		(USB_DT_STRING << 8) + index, langid, buf, size, HZ * GET_TIMEOUT);
}

int usb_get_device_descriptor(struct usb_device *dev)
{
	int ret = usb_get_descriptor(dev, USB_DT_DEVICE, 0, &dev->descriptor,
				     sizeof(dev->descriptor));
	if (ret >= 0) {
		le16_to_cpus(&dev->descriptor.bcdUSB);
		le16_to_cpus(&dev->descriptor.idVendor);
		le16_to_cpus(&dev->descriptor.idProduct);
		le16_to_cpus(&dev->descriptor.bcdDevice);
	}
	return ret;
}

int usb_get_status(struct usb_device *dev, int type, int target, void *data)
{
	return usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
		USB_REQ_GET_STATUS, USB_DIR_IN | type, 0, target, data, 2, HZ * GET_TIMEOUT);
}

int usb_get_protocol(struct usb_device *dev, int ifnum)
{
	unsigned char type;
	int ret;

	if ((ret = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
	    USB_REQ_GET_PROTOCOL, USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
	    0, ifnum, &type, 1, HZ * GET_TIMEOUT)) < 0)
		return ret;

	return type;
}

int usb_set_protocol(struct usb_device *dev, int ifnum, int protocol)
{
	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
		USB_REQ_SET_PROTOCOL, USB_TYPE_CLASS | USB_RECIP_INTERFACE,
		protocol, ifnum, NULL, 0, HZ * SET_TIMEOUT);
}

int usb_set_idle(struct usb_device *dev, int ifnum, int duration, int report_id)
{
	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
		USB_REQ_SET_IDLE, USB_TYPE_CLASS | USB_RECIP_INTERFACE,
		(duration << 8) | report_id, ifnum, NULL, 0, HZ * SET_TIMEOUT);
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
int usb_clear_halt(struct usb_device *dev, int pipe)
{
	int result;
	__u16 status;
	int endp=usb_pipeendpoint(pipe)|(usb_pipein(pipe)<<7);

/*
	if (!usb_endpoint_halted(dev, endp & 0x0f, usb_endpoint_out(endp)))
		return 0;
*/

	result = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
		USB_REQ_CLEAR_FEATURE, USB_RECIP_ENDPOINT, 0, endp, NULL, 0, HZ * SET_TIMEOUT);

	/* don't clear if failed */
	if (result < 0)
		return result;

	result = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
		USB_REQ_GET_STATUS, USB_DIR_IN | USB_RECIP_ENDPOINT, 0, endp,
		&status, sizeof(status), HZ * SET_TIMEOUT);
	if (result < 0)
		return result;

	if (status & 1)
		return -EPIPE;		/* still halted */

	usb_endpoint_running(dev, usb_pipeendpoint(pipe), usb_pipeout(pipe));

	/* toggle is reset on clear */

	usb_settoggle(dev, usb_pipeendpoint(pipe), usb_pipeout(pipe), 0);

	return 0;
}

int usb_set_interface(struct usb_device *dev, int interface, int alternate)
{
	struct usb_interface *iface = NULL;
	int ret, i;

	for (i=0; i<dev->actconfig->bNumInterfaces; i++) {
		if (dev->actconfig->interface[i].altsetting->bInterfaceNumber == interface) {
			iface = &dev->actconfig->interface[i];
			break;
		}
	}
	if (!iface) {
		warn("selecting invalid interface %d", interface);
		return -EINVAL;
	}

	if ((ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
	    USB_REQ_SET_INTERFACE, USB_RECIP_INTERFACE, alternate,
	    interface, NULL, 0, HZ * 5)) < 0)
		return ret;

	iface->act_altsetting = alternate;
	usb_set_maxpacket(dev);
	return 0;
}

int usb_set_configuration(struct usb_device *dev, int configuration)
{
	int i, ret;
	struct usb_config_descriptor *cp = NULL;
	
	for (i=0; i<dev->descriptor.bNumConfigurations; i++) {
		if (dev->config[i].bConfigurationValue == configuration) {
			cp = &dev->config[i];
			break;
		}
	}
	if (!cp) {
		warn("selecting invalid configuration %d", configuration);
		return -1;
	}

	if ((ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
	    USB_REQ_SET_CONFIGURATION, 0, configuration, 0, NULL, 0, HZ * SET_TIMEOUT)) < 0)
		return ret;

	dev->actconfig = cp;
	dev->toggle[0] = 0;
	dev->toggle[1] = 0;
	usb_set_maxpacket(dev);

	return 0;
}

int usb_get_report(struct usb_device *dev, int ifnum, unsigned char type, unsigned char id, void *buf, int size)
{
	return usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
		USB_REQ_GET_REPORT, USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
		(type << 8) + id, ifnum, buf, size, HZ * GET_TIMEOUT);
}

int usb_set_report(struct usb_device *dev, int ifnum, unsigned char type, unsigned char id, void *buf, int size)
{
	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
		USB_REQ_SET_REPORT, USB_TYPE_CLASS | USB_RECIP_INTERFACE,
		(type << 8) + id, ifnum, buf, size, HZ);
}

int usb_get_configuration(struct usb_device *dev)
{
       	int result;
	unsigned int cfgno;
	unsigned char buffer[8];
	unsigned char *bigbuffer;
	unsigned int tmp;
	struct usb_config_descriptor *desc =
		(struct usb_config_descriptor *)buffer;

	if (dev->descriptor.bNumConfigurations > USB_MAXCONFIG) {
		warn("too many configurations");
		return -1;
	}

	if (dev->descriptor.bNumConfigurations < 1) {
		warn("not enough configurations");
		return -1;
	}

	dev->config = (struct usb_config_descriptor *)
		kmalloc(dev->descriptor.bNumConfigurations *
		sizeof(struct usb_config_descriptor), GFP_KERNEL);
	if (!dev->config) {
		err("out of memory");
		return -1;	
	}
	memset(dev->config, 0, dev->descriptor.bNumConfigurations *
		sizeof(struct usb_config_descriptor));

	for (cfgno = 0; cfgno < dev->descriptor.bNumConfigurations; cfgno++) {


		/* We grab the first 8 bytes so we know how long the whole */
		/*  configuration is */
		result = usb_get_descriptor(dev, USB_DT_CONFIG, cfgno, buffer, 8);
		if (result < 8) {
			if (result < 0)
				err("unable to get descriptor");
			else
				err("config descriptor too short (expected %i, got %i)",8,result);
			goto err;
		}

  	  	/* Get the full buffer */
		le16_to_cpus(&desc->wTotalLength);

		bigbuffer = kmalloc(desc->wTotalLength, GFP_KERNEL);
		if (!bigbuffer) {
			err("unable to allocate memory for configuration descriptors");
			result=-ENOMEM;
			goto err;
		}
		tmp=desc->wTotalLength;
		/* Now that we know the length, get the whole thing */
		result = usb_get_descriptor(dev, USB_DT_CONFIG, cfgno, bigbuffer, desc->wTotalLength);
		if (result < 0) {
			err("couldn't get all of config descriptors");
			kfree(bigbuffer);
			goto err;
		}	
	
		if (result < tmp) {
			err("config descriptor too short (expected %i, got %i)",tmp,result);
			kfree(bigbuffer);
			goto err;
		}		
		result = usb_parse_configuration(dev, &dev->config[cfgno], bigbuffer);
		kfree(bigbuffer);

		if (result > 0)
			dbg("descriptor data left");
		else if (result < 0)
		{
			result=-1;
			goto err;
		}
	}

	return 0;
	err:
	dev->descriptor.bNumConfigurations=cfgno;
	return result;
}

int usb_string(struct usb_device *dev, int index, char *buf, size_t size)
{
	unsigned char *tbuf;
	int err;
	unsigned int u, idx;

	if (size <= 0 || !buf)
		return -EINVAL;
	buf[0] = 0;
	tbuf = kmalloc(256, GFP_KERNEL);
	if (!tbuf)
		return -ENOMEM;
	/*
	 * is this two step process necessary? can't we just
	 * ask for a maximum length string and then take the length
	 * that was returned?
	 */
	err = usb_get_string(dev, dev->string_langid, index, tbuf, 4);
	if (err < 0)
		goto errout;
	err = usb_get_string(dev, dev->string_langid, index, tbuf, tbuf[0]);
	if (err < 0)
		goto errout;
	size--;
	for (idx = 0, u = 2; u < tbuf[0]; u += 2) {
		if (idx >= size)
			break;
		if (tbuf[u+1]) {
			buf[idx++] = '?';  /* non ASCII character */
			continue;
		}
		buf[idx++] = tbuf[u];
	}
	buf[idx] = 0;
	err = idx;

 errout:
	kfree(tbuf);
	return err;
}

/*
 * By the time we get here, the device has gotten a new device ID
 * and is in the default state. We need to identify the thing and
 * get the ball rolling..
 *
 * Returns 0 for success, != 0 for error.
 */
int usb_new_device(struct usb_device *dev)
{
	unsigned char *buf;
	int addr, err;
	int tmp;

	info("USB new device connect, assigned device number %d", dev->devnum);
 
	dev->maxpacketsize = 0;		/* Default to 8 byte max packet size */
	dev->epmaxpacketin [0] = 8;
	dev->epmaxpacketout[0] = 8;

	/* We still haven't set the Address yet */
	addr = dev->devnum;
	dev->devnum = 0;

	err = usb_get_descriptor(dev, USB_DT_DEVICE, 0, &dev->descriptor, 8);
	if (err < 8) {
		if (err < 0)
			err("USB device not responding, giving up (error=%d)", err);
		else
			err("USB device descriptor short read (expected %i, got %i)",8,err);
		clear_bit(addr, &dev->bus->devmap.devicemap);
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

	err = usb_set_address(dev);
	
	if (err < 0) {
		err("USB device not accepting new address (error=%d)", err);
		clear_bit(dev->devnum, &dev->bus->devmap.devicemap);
		dev->devnum = -1;
		return 1;
	}

	wait_ms(10);	/* Let the SET_ADDRESS settle */

	tmp = sizeof(dev->descriptor);

	err = usb_get_device_descriptor(dev);
	if (err < tmp) {
		if (err < 0)
			err("unable to get device descriptor (error=%d)",err);
		else
			err("USB device descriptor short read (expected %i, got %i)",tmp,err);
	
		clear_bit(dev->devnum, &dev->bus->devmap.devicemap);
		dev->devnum = -1;
		return 1;
	}

	err = usb_get_configuration(dev);
	if (err < 0) {
		err("unable to get configuration (error=%d)", err);
		clear_bit(dev->devnum, &dev->bus->devmap.devicemap);
		dev->devnum = -1;
		return 1;
	}

	dev->actconfig = dev->config;
	usb_set_maxpacket(dev);

	/* we set the default configuration here */
	if (usb_set_configuration(dev, dev->config[0].bConfigurationValue)) {
		err("failed to set default configuration");
		return -1;
	}
	/* get langid for strings */
	buf = kmalloc(256, GFP_KERNEL);
	if (!buf) {
		err("out of memory\n");
	} else {
		err = usb_get_string(dev, 0, 0, buf, 4);
		if (err < 0) {
			err("error getting string descriptor 0 (error=%d)\n", err);
		} else if (buf[0] < 4) {
			err("string descriptpr 0 too short\n");
		} else
			dev->string_langid = buf[2] | (buf[3]<< 8);
		kfree(buf);
		info("USB device number %d default language ID 0x%x", dev->devnum, dev->string_langid);
	}

	usb_show_string(dev, "Manufacturer", dev->descriptor.iManufacturer);
	usb_show_string(dev, "Product", dev->descriptor.iProduct);
	usb_show_string(dev, "SerialNumber", dev->descriptor.iSerialNumber);

	/* now that the basic setup is over, add a /proc/bus/usb entry */
        usbdevfs_add_device(dev);

	/* find drivers willing to handle this device */
	usb_find_drivers(dev);

	return 0;
}

static int usb_open(struct inode * inode, struct file * file)
{
	int minor = MINOR(inode->i_rdev);
	struct usb_driver *c = usb_minors[minor/16];

	file->f_op = NULL;

	if (c && (file->f_op = c->fops) && file->f_op->open)
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

int usb_major_init(void)
{
	if (register_chrdev(USB_MAJOR,"usb",&usb_fops)) {
		err("unable to get major %d for usb devices", USB_MAJOR);
		return -EBUSY;
	}
	return 0;
}

void usb_major_cleanup(void)
{
	unregister_chrdev(USB_MAJOR, "usb");
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

EXPORT_SYMBOL(usb_driver_claim_interface);
EXPORT_SYMBOL(usb_interface_claimed);
EXPORT_SYMBOL(usb_driver_release_interface);

EXPORT_SYMBOL(usb_init_root_hub);
EXPORT_SYMBOL(usb_new_device);
EXPORT_SYMBOL(usb_connect);
EXPORT_SYMBOL(usb_disconnect);
EXPORT_SYMBOL(usb_release_bandwidth);

EXPORT_SYMBOL(usb_set_address);
EXPORT_SYMBOL(usb_get_descriptor);
EXPORT_SYMBOL(usb_get_class_descriptor);
EXPORT_SYMBOL(__usb_get_extra_descriptor);
EXPORT_SYMBOL(usb_get_device_descriptor);
EXPORT_SYMBOL(usb_get_string);
EXPORT_SYMBOL(usb_string);
EXPORT_SYMBOL(usb_get_protocol);
EXPORT_SYMBOL(usb_set_protocol);
EXPORT_SYMBOL(usb_get_report);
EXPORT_SYMBOL(usb_set_report);
EXPORT_SYMBOL(usb_set_idle);
EXPORT_SYMBOL(usb_clear_halt);
EXPORT_SYMBOL(usb_set_interface);
EXPORT_SYMBOL(usb_get_configuration);
EXPORT_SYMBOL(usb_set_configuration);

EXPORT_SYMBOL(usb_get_current_frame_number);

EXPORT_SYMBOL(usb_alloc_urb);
EXPORT_SYMBOL(usb_free_urb);
EXPORT_SYMBOL(usb_submit_urb);
EXPORT_SYMBOL(usb_unlink_urb);

EXPORT_SYMBOL(usb_control_msg);
EXPORT_SYMBOL(usb_request_irq);
EXPORT_SYMBOL(usb_release_irq);
EXPORT_SYMBOL(usb_bulk_msg);
EXPORT_SYMBOL(usb_request_bulk);
EXPORT_SYMBOL(usb_terminate_bulk);
