/*
 * drivers/usb/proc_usb.c
 * (C) Copyright 1999 Randy Dunlap.
 * (C) Copyright 1999 Thomas Sailer <sailer@ife.ee.ethz.ch>. (proc file per device)
 * (C) Copyright 1999 Deti Fliegl (new USB architecture)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *************************************************************
 *
 * This is a /proc/bus/usb filesystem output module for USB.
 * It creates /proc/bus/usb/drivers and /proc/bus/usb/devices.
 *
 * /proc/bus/usb/devices contains USB topology, device, config, class,
 * interface, & endpoint data.
 *
 * I considered using /proc/bus/usb/devices/device# for each device
 * as it is attached or detached, but I didn't like this for some
 * reason -- maybe it's just too deep of a directory structure.
 * I also don't like looking in multiple places to gather and view
 * the data.  Having only one file for ./devices also prevents race
 * conditions that could arise if a program was reading device info
 * for devices that are being removed (unplugged).  (That is, the
 * program may find a directory for devnum_12 then try to open it,
 * but it was just unplugged, so the directory is now deleted.
 * But programs would just have to be prepared for situations like
 * this in any plug-and-play environment.)
 *
 * 1999-12-16: Thomas Sailer <sailer@ife.ee.ethz.ch>
 *   Converted the whole proc stuff to real
 *   read methods. Now not the whole device list needs to fit
 *   into one page, only the device list for one bus.
 *   Added a poll method to /proc/bus/usb/devices, to wake
 *   up an eventual usbd
 *
 * $Id: proc_usb.c,v 1.14 1999/12/17 10:51:41 fliegl Exp $
 */

#define __NO_VERSION__
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/bitops.h>
#include <asm/uaccess.h>
#include <linux/mm.h>
#include <linux/wait.h>
#include <linux/poll.h>

#include "usb.h"


#define MAX_TOPO_LEVEL		6

/* Define ALLOW_SERIAL_NUMBER if you want to see the serial number of devices */
#define ALLOW_SERIAL_NUMBER

static char *format_topo =
/* T:  Bus=dd Lev=dd Prnt=dd Port=dd Cnt=dd Dev#=ddd Spd=ddd MxCh=dd */
  "T:  Bus=%2.2d Lev=%2.2d Prnt=%2.2d Port=%2.2d Cnt=%2.2d Dev#=%3d Spd=%3s MxCh=%2d\n";

static char *format_string_manufacturer =
/* S:  Manufacturer=xxxx */
  "S:  Manufacturer=%s\n";

static char *format_string_product =
/* S:  Product=xxxx */
  "S:  Product=%s\n";

#ifdef ALLOW_SERIAL_NUMBER
static char *format_string_serialnumber =
/* S:  SerialNumber=xxxx */
  "S:  SerialNumber=%s\n";
#endif

static char *format_bandwidth =
/* B:  Alloc=ddd/ddd us (xx%), #Int=ddd, #Iso=ddd */
  "B:  Alloc=%3d/%3d us (%2d%%), #Int=%3d, #Iso=%3d\n";
  
static char *format_device1 =
/* D:  Ver=xx.xx Cls=xx(sssss) Sub=xx Prot=xx MxPS=dd #Cfgs=dd */
  "D:  Ver=%2x.%02x Cls=%02x(%-5s) Sub=%02x Prot=%02x MxPS=%2d #Cfgs=%3d\n";

static char *format_device2 =
/* P:  Vendor=xxxx ProdID=xxxx Rev=xx.xx */
  "P:  Vendor=%04x ProdID=%04x Rev=%2x.%02x\n";

static char *format_config =
/* C:  #Ifs=dd Cfg#=dd Atr=xx MPwr=dddmA */
  "C:%c #Ifs=%2d Cfg#=%2d Atr=%02x MxPwr=%3dmA\n";
  
static char *format_iface =
/* I:  If#=dd Alt=dd #EPs=dd Cls=xx(sssss) Sub=xx Prot=xx Driver=xxxx*/
  "I:  If#=%2d Alt=%2d #EPs=%2d Cls=%02x(%-5s) Sub=%02x Prot=%02x Driver=%s\n";

static char *format_endpt =
/* E:  Ad=xx(s) Atr=xx(ssss) MxPS=dddd Ivl=dddms */
  "E:  Ad=%02x(%c) Atr=%02x(%-4s) MxPS=%4d Ivl=%3dms\n";


/*
 * Need access to the driver and USB bus lists.
 * extern struct list_head usb_driver_list;
 * extern struct list_head usb_bus_list;
 * However, these will come from functions that return ptrs to each of them.
 */

extern struct list_head *usb_driver_get_list (void);
extern struct list_head *usb_bus_get_list (void);

extern struct proc_dir_entry *proc_bus;

static struct proc_dir_entry *usbdir = NULL, *driversdir = NULL;
static struct proc_dir_entry *devicesdir = NULL;

static DECLARE_WAIT_QUEUE_HEAD(deviceconndiscwq);
static unsigned int conndiscevcnt = 0;

/* this struct stores the poll state for /proc/bus/usb/devices pollers */
struct usb_device_status {
	unsigned int lastev;
};

struct class_info {
	int class;
	char *class_name;
};

static const struct class_info clas_info[] =
{					/* max. 5 chars. per name string */
	{USB_CLASS_PER_INTERFACE,	">ifc"},
	{USB_CLASS_AUDIO,		"audio"},
	{USB_CLASS_COMM,		"comm."},
	{USB_CLASS_HID,			"HID"},
	{USB_CLASS_HUB,			"hub"},
	{USB_CLASS_PRINTER,		"print"},
	{USB_CLASS_MASS_STORAGE,	"stor."},
	{USB_CLASS_DATA,		"data"},
	{USB_CLASS_VENDOR_SPEC,		"vend."},
	{-1,				"unk."}		/* leave as last */
};

/*****************************************************************/

extern inline void conndiscevent(void)
{
	wake_up(&deviceconndiscwq);
	conndiscevcnt++;
}

static const char *class_decode(const int class)
{
	int ix;

	for (ix = 0; clas_info[ix].class != -1; ix++)
		if (clas_info[ix].class == class)
			break;
	return (clas_info[ix].class_name);
}

static char *usb_dump_endpoint_descriptor(char *start, char *end, const struct usb_endpoint_descriptor *desc)
{
	char *EndpointType [4] = {"Ctrl", "Isoc", "Bulk", "Int."};

	if (start > end)
		return start;
	start += sprintf(start, format_endpt, desc->bEndpointAddress,
			 (desc->bEndpointAddress & USB_DIR_IN) ? 'I' : 'O',
			 desc->bmAttributes, EndpointType[desc->bmAttributes & 3],
			 desc->wMaxPacketSize, desc->bInterval);
	return start;
}

static char *usb_dump_endpoint(char *start, char *end, const struct usb_endpoint_descriptor *endpoint)
{
	return usb_dump_endpoint_descriptor(start, end, endpoint);
}

static char *usb_dump_interface_descriptor(char *start, char *end, const struct usb_interface *iface, int setno)
{
	struct usb_interface_descriptor *desc = &iface->altsetting[setno];

	if (start > end)
		return start;
	start += sprintf(start, format_iface,
			 desc->bInterfaceNumber,
			 desc->bAlternateSetting,
			 desc->bNumEndpoints,
			 desc->bInterfaceClass,
			 class_decode(desc->bInterfaceClass),
			 desc->bInterfaceSubClass,
			 desc->bInterfaceProtocol,
			 iface->driver ? iface->driver->name : "(none)");
	return start;
}

static char *usb_dump_interface(char *start, char *end, const struct usb_interface *iface, int setno)
{
	struct usb_interface_descriptor *desc = &iface->altsetting[setno];
	int i;

	start = usb_dump_interface_descriptor(start, end, iface, setno);
	for (i = 0; i < desc->bNumEndpoints; i++) {
		if (start > end)
			return start;
		start = usb_dump_endpoint(start, end, desc->endpoint + i);
	}
	return start;
}

/* TBD:
 * 0. TBDs
 * 1. marking active config and ifaces (code lists all, but should mark
 *    which ones are active, if any)
 * 2. add <halted> status to each endpoint line
 */

static char *usb_dump_config_descriptor(char *start, char *end, const struct usb_config_descriptor *desc, const int active)
{
	if (start > end)
		return start;
	start += sprintf(start, format_config,
			 active ? '*' : ' ',	/* mark active/actual/current cfg. */
			 desc->bNumInterfaces,
			 desc->bConfigurationValue,
			 desc->bmAttributes,
			 desc->MaxPower * 2);
	return start;
}

static char *usb_dump_config(char *start, char *end, const struct usb_config_descriptor *config, const int active)
{
	int i, j;
	struct usb_interface *interface;

	if (start > end)
		return start;
	if (!config)		/* getting these some in 2.3.7; none in 2.3.6 */
		return start + sprintf(start, "(null Cfg. desc.)\n");
	start = usb_dump_config_descriptor(start, end, config, active);
	for (i = 0; i < config->bNumInterfaces; i++) {
		interface = config->interface + i;
		if (!interface)
			break;
		for (j = 0; j < interface->num_altsetting; j++) {
			if (start > end)
				return start;
			start = usb_dump_interface(start, end, interface, j);
		}
	}
	return start;
}

/*
 * Dump the different USB descriptors.
 */
static char *usb_dump_device_descriptor(char *start, char *end, const struct usb_device_descriptor *desc)
{
	if (start > end)
		return start;
	start += sprintf (start, format_device1,
			  desc->bcdUSB >> 8, desc->bcdUSB & 0xff,
			  desc->bDeviceClass,
			  class_decode (desc->bDeviceClass),
			  desc->bDeviceSubClass,
			  desc->bDeviceProtocol,
			  desc->bMaxPacketSize0,
			  desc->bNumConfigurations);
	if (start > end)
		return start;
	start += sprintf(start, format_device2,
			 desc->idVendor, desc->idProduct,
			 desc->bcdDevice >> 8, desc->bcdDevice & 0xff);
	return start;
}

/*
 * Dump the different strings that this device holds.
 */
static char *usb_dump_device_strings (char *start, char *end, const struct usb_device *dev)
{
	if (start > end)
		return start;

	if (dev->descriptor.iManufacturer) {
		char * string = usb_string ((struct usb_device *)dev, 
					dev->descriptor.iManufacturer);
		if (string) {
			start += sprintf (start, format_string_manufacturer,
					string
					);
		if (start > end)
			return start;
								
		}
	}

	if (dev->descriptor.iProduct) {
		char * string = usb_string ((struct usb_device *)dev, 
					dev->descriptor.iProduct);
		if (string) {
			start += sprintf (start, format_string_product,
					string
					);
		if (start > end)
			return start;

		}
	}

#ifdef ALLOW_SERIAL_NUMBER
	if (dev->descriptor.iSerialNumber) {
		char * string = usb_string ((struct usb_device *)dev, 
					dev->descriptor.iSerialNumber);
		if (string) {
			start += sprintf (start, format_string_serialnumber,
					string
					);
		}
	}
#endif

	return start;
}

static char *usb_dump_desc(char *start, char *end, const struct usb_device *dev)
{
	int i;

	if (start > end)
		return start;
		
	start = usb_dump_device_descriptor(start, end, &dev->descriptor);

	if (start > end)
		return start;
	
	start = usb_dump_device_strings (start, end, dev);
	
	for (i = 0; i < dev->descriptor.bNumConfigurations; i++) {
		if (start > end)
			return start;
		start = usb_dump_config(start, end, dev->config + i,
					(dev->config + i) == dev->actconfig); /* active ? */
	}
	return start;
}


#ifdef PROC_EXTRA /* TBD: may want to add this code later */

static char *usb_dump_hub_descriptor(char *start, char *end, const struct usb_hub_descriptor * desc)
{
	int leng = USB_DT_HUB_NONVAR_SIZE;
	unsigned char *ptr = (unsigned char *)desc;

	if (start > end)
		return start;
	start += sprintf(start, "Interface:");
	while (leng) {
		start += sprintf(start, " %02x", *ptr);
		ptr++; leng--;
	}
	start += sprintf(start, "\n");
	return start;
}

#endif /* PROC_EXTRA */

/*****************************************************************/

static char *usb_device_dump(char *start, char *end, const struct usb_device *usbdev,
			     const struct usb_bus *bus, int level, int index, int count)
{
	int chix;
	int cnt = 0;
	int parent_devnum = 0;

	if (level > MAX_TOPO_LEVEL)
		return start;
	if (usbdev->parent && usbdev->parent->devnum != -1)
		parent_devnum = usbdev->parent->devnum;
	/*
	 * So the root hub's parent is 0 and any device that is
	 * plugged into the root hub has a parent of 0.
	 */
	start += sprintf(start, format_topo, bus->busnum-1, level, parent_devnum, index, count,
			 usbdev->devnum, usbdev->slow ? "1.5" : "12 ", usbdev->maxchild);
	/*
	 * level = topology-tier level;
	 * parent_devnum = parent device number;
	 * index = parent's connector number;
	 * count = device count at this level
	 */
	/* If this is the root hub, display the bandwidth information */
	if (level == 0)
		start += sprintf(start, format_bandwidth, bus->bandwidth_allocated, 
				FRAME_TIME_MAX_USECS_ALLOC,
				(100 * bus->bandwidth_allocated + FRAME_TIME_MAX_USECS_ALLOC / 2) / FRAME_TIME_MAX_USECS_ALLOC,
			         bus->bandwidth_int_reqs, bus->bandwidth_isoc_reqs);

	/* show the descriptor information for this device */
	start = usb_dump_desc(start, end, usbdev);
	if (start > end)
		return start + sprintf(start, "(truncated)\n");

	/* Now look at all of this device's children. */
	for (chix = 0; chix < usbdev->maxchild; chix++) {
		if (start > end)
			return start;
		if (usbdev->children[chix])
			start = usb_device_dump(start, end, usbdev->children[chix], bus, level + 1, chix, ++cnt);
	}
	return start;
}

static ssize_t usb_device_read(struct file *file, char *buf, size_t nbytes, loff_t *ppos)
{
	struct list_head *usb_bus_list, *buslist;
	struct usb_bus *bus;
	char *page, *end;
	ssize_t ret = 0;
	unsigned int pos, len;

	if (*ppos < 0)
		return -EINVAL;
	if (nbytes <= 0)
		return 0;
	if (!access_ok(VERIFY_WRITE, buf, nbytes))
		return -EFAULT;
	if (!(page = (char*) __get_free_page(GFP_KERNEL)))
		return -ENOMEM;
	pos = *ppos;
	usb_bus_list = usb_bus_get_list();
	/* enumerate busses */
	for (buslist = usb_bus_list->next; buslist != usb_bus_list; buslist = buslist->next) {
		bus = list_entry(buslist, struct usb_bus, bus_list);
		end = usb_device_dump(page, page + (PAGE_SIZE - 100), bus->root_hub, bus, 0, 0, 0);
		len = end - page;
		if (len > pos) {
			len -= pos;
			if (len > nbytes)
				len = nbytes;
			if (copy_to_user(buf, page + pos, len)) {
				if (!ret)
					ret = -EFAULT;
				break;
			}
			nbytes -= len;
			buf += len;
			ret += len;
			pos = 0;
			*ppos += len;
		} else
			pos -= len;
	}
	free_page((unsigned long)page);
	return ret;
}

static unsigned int usb_device_poll(struct file *file, struct poll_table_struct *wait)
{
	struct usb_device_status *st = (struct usb_device_status *)file->private_data;
	unsigned int mask = 0;
	
	if (!st) {
		st = kmalloc(sizeof(struct usb_device_status), GFP_KERNEL);
		if (!st)
			return POLLIN;
		/*
		 * need to prevent the module from being unloaded, since
		 * proc_unregister does not call the release method and
		 * we would have a memory leak
		 */
		st->lastev = conndiscevcnt;
		file->private_data = st;
		MOD_INC_USE_COUNT;
		mask = POLLIN;
	}
	if (file->f_mode & FMODE_READ)
		poll_wait(file, &deviceconndiscwq, wait);
	if (st->lastev != conndiscevcnt)
		mask |= POLLIN;
	st->lastev = conndiscevcnt;
	return mask;
}

static int usb_device_open(struct inode *inode, struct file *file)
{
	file->private_data = NULL;
	MOD_INC_USE_COUNT;
	return 0;
}

static int usb_device_release(struct inode *inode, struct file *file)
{
	if (file->private_data) {
		kfree(file->private_data);
		file->private_data = NULL;
	}
	MOD_DEC_USE_COUNT;	
	return 0;
}

/*
 * Dump usb_driver_list.
 *
 * We now walk the list of registered USB drivers.
 */
static ssize_t usb_driver_read(struct file *file, char *buf, size_t nbytes, loff_t *ppos)
{
	struct list_head *usb_driver_list = usb_driver_get_list();
	struct list_head *tmp = usb_driver_list->next;
	char *page, *start, *end;
	ssize_t ret = 0;
	unsigned int pos, len;

	if (*ppos < 0)
		return -EINVAL;
	if (nbytes <= 0)
		return 0;
	if (!access_ok(VERIFY_WRITE, buf, nbytes))
		return -EFAULT;
	if (!(page = (char*) __get_free_page(GFP_KERNEL)))
		return -ENOMEM;
	start = page;
	end = page + (PAGE_SIZE - 100);
	pos = *ppos;
	for (; tmp != usb_driver_list; tmp = tmp->next) {
		struct usb_driver *driver = list_entry(tmp, struct usb_driver, driver_list);
		start += sprintf (start, "%s\n", driver->name);
		if (start > end) {
			start += sprintf(start, "(truncated)\n");
			break;
		}
	}
	if (start == page)
		start += sprintf(start, "(none)\n");
	len = start - page;
	if (len > pos) {
		len -= pos;
		if (len > nbytes)
			len = nbytes;
		ret = len;
		if (copy_to_user(buf, page + pos, len))
			ret = -EFAULT;
		else
			*ppos += len;
	}
	free_page((unsigned long)page);
	return ret;
}

static long long usbdev_lseek(struct file * file, long long offset, int orig);

static struct file_operations proc_usb_devlist_file_operations = {
	usbdev_lseek,       /* lseek   */
	usb_device_read,    /* read    */
	NULL,               /* write   */
	NULL,               /* readdir */
	usb_device_poll,    /* poll    */
	NULL,               /* ioctl   */
	NULL,               /* mmap    */
	usb_device_open,    /* open    */
	NULL,               /* flush   */
	usb_device_release, /* release */
	NULL                /* fsync   */
};

static struct inode_operations proc_usb_devlist_inode_operations = {
	&proc_usb_devlist_file_operations,  /* file-ops */
};

static struct file_operations proc_usb_drvlist_file_operations = {
	usbdev_lseek,    /* lseek   */
	usb_driver_read, /* read    */
	NULL,            /* write   */
	NULL,            /* readdir */
	NULL,            /* poll    */
	NULL,            /* ioctl   */
	NULL,            /* mmap    */
	NULL,            /* no special open code    */
	NULL,            /* flush */
	NULL,            /* no special release code */
	NULL             /* can't fsync */
};

static struct inode_operations proc_usb_drvlist_inode_operations = {
	&proc_usb_drvlist_file_operations,  /* file-ops */
};


/*
 * proc entry for every device
 */

static long long usbdev_lseek(struct file * file, long long offset, int orig)
{
	switch (orig) {
	case 0:
		file->f_pos = offset;
		return file->f_pos;

	case 1:
		file->f_pos += offset;
		return file->f_pos;

	case 2:
		return -EINVAL;

	default:
		return -EINVAL;
	}
}

static ssize_t usbdev_read(struct file * file, char * buf, size_t nbytes, loff_t *ppos)
{
	struct inode *inode = file->f_dentry->d_inode;
	struct proc_dir_entry *dp = (struct proc_dir_entry *)inode->u.generic_ip;
	struct usb_device *dev = (struct usb_device *)dp->data;
	ssize_t ret = 0;
	unsigned len;

	if (*ppos < 0)
		return -EINVAL;
	if (*ppos < sizeof(struct usb_device_descriptor)) {
		len = sizeof(struct usb_device_descriptor);
		if (len > nbytes)
			len = nbytes;
		copy_to_user_ret(buf, ((char *)&dev->descriptor) + *ppos, len, -EFAULT);
		*ppos += len;
		buf += len;
		nbytes -= len;
		ret += len;
	}
	return ret;
}

/* note: this is a compatibility kludge that will vanish soon. */
#include "ezusb.h"

static int usbdev_ioctl_ezusbcompat(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	static unsigned obsolete_warn = 0;
	struct proc_dir_entry *dp = (struct proc_dir_entry *)inode->u.generic_ip;
	struct usb_device *dev = (struct usb_device *)dp->data;
	struct ezusb_ctrltransfer ctrl;
	struct ezusb_bulktransfer bulk;
	struct ezusb_old_ctrltransfer octrl;
	struct ezusb_old_bulktransfer obulk;
	struct ezusb_setinterface setintf;
	unsigned int len1, ep, pipe, cfg;
	unsigned long len2;
	unsigned char *tbuf;
	int i;

	switch (cmd) {
	case EZUSB_CONTROL:
		if (obsolete_warn < 20) {
			warn("process %d (%s) used obsolete EZUSB_CONTROL ioctl",
			       current->pid, current->comm);
			obsolete_warn++;
		}
		if (!capable(CAP_SYS_RAWIO))
			return -EPERM;
		copy_from_user_ret(&ctrl, (void *)arg, sizeof(ctrl), -EFAULT);
		if (ctrl.length > PAGE_SIZE)
			return -EINVAL;
		if (!(tbuf = (unsigned char *)__get_free_page(GFP_KERNEL)))
			return -ENOMEM;
		if (ctrl.requesttype & 0x80) {
			if (ctrl.length && !access_ok(VERIFY_WRITE, ctrl.data, ctrl.length)) {
				free_page((unsigned long)tbuf);
				return -EINVAL;
			}
			i = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), ctrl.request, ctrl.requesttype,
					    ctrl.value, ctrl.index, tbuf, ctrl.length, 
					    (ctrl.timeout * HZ + 500) / 1000);
			if ((i > 0) && ctrl.length) {
				copy_to_user_ret(ctrl.data, tbuf, ctrl.length, -EFAULT);
			}
		} else {
			if (ctrl.length) {
				copy_from_user_ret(tbuf, ctrl.data, ctrl.length, -EFAULT);
			}
			i = usb_control_msg(dev, usb_sndctrlpipe(dev, 0), ctrl.request, ctrl.requesttype,
					    ctrl.value, ctrl.index, tbuf, ctrl.length, 
					    (ctrl.timeout * HZ + 500) / 1000);
		}
		free_page((unsigned long)tbuf);
		if (i < 0) {
			warn("EZUSB_CONTROL failed rqt %u rq %u len %u ret %d", 
			       ctrl.requesttype, ctrl.request, ctrl.length, i);
			return i;
		}
		return i;

	case EZUSB_BULK:
		if (obsolete_warn < 20) {
			warn("process %d (%s) used obsolete EZUSB_BULK ioctl",
			       current->pid, current->comm);
			obsolete_warn++;
		}
		if (!capable(CAP_SYS_RAWIO))
			return -EPERM;
		copy_from_user_ret(&bulk, (void *)arg, sizeof(bulk), -EFAULT);
		if (bulk.ep & 0x80)
			pipe = usb_rcvbulkpipe(dev, bulk.ep & 0x7f);
		else
			pipe = usb_sndbulkpipe(dev, bulk.ep & 0x7f);
		if (!usb_maxpacket(dev, pipe, !(bulk.ep & 0x80)))
			return -EINVAL;
		len1 = bulk.len;
		if (len1 > PAGE_SIZE)
			len1 = PAGE_SIZE;
		if (!(tbuf = (unsigned char *)__get_free_page(GFP_KERNEL)))
			return -ENOMEM;
		if (bulk.ep & 0x80) {
			if (len1 && !access_ok(VERIFY_WRITE, bulk.data, len1)) {
				free_page((unsigned long)tbuf);
				return -EINVAL;
			}
			i = usb_bulk_msg(dev, pipe, tbuf, len1, &len2, (ctrl.timeout * HZ + 500) / 1000);
			if ((i > 0) && len2) {
				copy_to_user_ret(bulk.data, tbuf, len2, -EFAULT);
			}
		} else {
			if (len1) {
				copy_from_user_ret(tbuf, bulk.data, len1, -EFAULT);
			}
			i = usb_bulk_msg(dev, pipe, tbuf, len1, &len2, (ctrl.timeout * HZ + 500) / 1000);
		}
		free_page((unsigned long)tbuf);
		if (i < 0) {
			warn("EZUSB_BULK failed ep 0x%x len %u ret %d", 
			       bulk.ep, bulk.len, i);
			return i;
		}
		return len2;

	case EZUSB_OLD_CONTROL:
		if (obsolete_warn < 20) {
			warn("process %d (%s) used obsolete EZUSB_OLD_CONTROL ioctl",
			       current->pid, current->comm);
			obsolete_warn++;
		}
		if (!capable(CAP_SYS_RAWIO))
			return -EPERM;
		copy_from_user_ret(&octrl, (void *)arg, sizeof(octrl), -EFAULT);
		if (octrl.dlen > PAGE_SIZE)
			return -EINVAL;
		if (!(tbuf = (unsigned char *)__get_free_page(GFP_KERNEL)))
			return -ENOMEM;
		if (octrl.requesttype & 0x80) {
			if (octrl.dlen && !access_ok(VERIFY_WRITE, octrl.data, octrl.dlen)) {
				free_page((unsigned long)tbuf);
				return -EINVAL;
			}
			i = usb_internal_control_msg(dev, usb_rcvctrlpipe(dev, 0), (devrequest *)&octrl, tbuf, octrl.dlen, HZ);
			if ((i > 0) && octrl.dlen) {
				copy_to_user_ret(octrl.data, tbuf, octrl.dlen, -EFAULT);
			}
		} else {
			if (octrl.dlen) {
				copy_from_user_ret(tbuf, octrl.data, octrl.dlen, -EFAULT);
			}
			i = usb_internal_control_msg(dev, usb_sndctrlpipe(dev, 0), (devrequest *)&octrl, tbuf, octrl.dlen, HZ);
					}
		free_page((unsigned long)tbuf);
		if (i < 0) {
			warn("EZUSB_OLD_CONTROL failed rqt %u rq %u len %u ret %d", 
			       octrl.requesttype, octrl.request, octrl.length, i);
			return i;
		}
		return i;

	case EZUSB_OLD_BULK:
		if (obsolete_warn < 20) {
			warn("process %d (%s) used obsolete EZUSB_OLD_BULK ioctl",
			       current->pid, current->comm);
			obsolete_warn++;
		}
		if (!capable(CAP_SYS_RAWIO))
			return -EPERM;
		copy_from_user_ret(&obulk, (void *)arg, sizeof(obulk), -EFAULT);
		if (obulk.ep & 0x80)
			pipe = usb_rcvbulkpipe(dev, obulk.ep & 0x7f);
		else
			pipe = usb_sndbulkpipe(dev, obulk.ep & 0x7f);
		if (!usb_maxpacket(dev, pipe, !(obulk.ep & 0x80)))
			return -EINVAL;
		len1 = obulk.len;
		if (len1 > PAGE_SIZE)
			len1 = PAGE_SIZE;
		if (!(tbuf = (unsigned char *)__get_free_page(GFP_KERNEL)))
			return -ENOMEM;
		if (obulk.ep & 0x80) {
			if (len1 && !access_ok(VERIFY_WRITE, obulk.data, len1)) {
				free_page((unsigned long)tbuf);
				return -EINVAL;
			}
			i = usb_bulk_msg(dev, pipe, tbuf, len1, &len2, HZ*5);
			if ((i > 0) && len2) {
				copy_to_user_ret(obulk.data, tbuf, len2, -EFAULT);
			}
		} else {
			if (len1) {
				copy_from_user_ret(tbuf, obulk.data, len1, -EFAULT);
			}
			i = usb_bulk_msg(dev, pipe, tbuf, len1, &len2, HZ*5);
		}
		free_page((unsigned long)tbuf);
		if (i < 0) {
			warn("EZUSB_OLD_BULK failed ep 0x%x len %u ret %d", 
			       obulk.ep, obulk.len, i);
			return i;
		}
		return len2;

	case EZUSB_RESETEP:
		if (obsolete_warn < 20) {
			warn("process %d (%s) used obsolete EZUSB_RESETEP ioctl",
			       current->pid, current->comm);
			obsolete_warn++;
		}
		if (!capable(CAP_SYS_RAWIO))
			return -EPERM;
		get_user_ret(ep, (unsigned int *)arg, -EFAULT);
		if ((ep & ~0x80) >= 16)
			return -EINVAL;
		usb_settoggle(dev, ep & 0xf, !(ep & 0x80), 0);
		return 0;

	case EZUSB_SETINTERFACE:
		if (obsolete_warn < 20) {
			warn("process %d (%s) used obsolete EZUSB_SETINTERFACE ioctl",
			       current->pid, current->comm);
			obsolete_warn++;
		}
		if (!capable(CAP_SYS_RAWIO))
			return -EPERM;
		copy_from_user_ret(&setintf, (void *)arg, sizeof(setintf), -EFAULT);
		if (usb_set_interface(dev, setintf.interface, setintf.altsetting))
			return -EINVAL;
		return 0;

	case EZUSB_SETCONFIGURATION:
		if (obsolete_warn < 20) {
			warn("process %d (%s) used obsolete EZUSB_SETCONFIGURATION ioctl",
			       current->pid, current->comm);
			obsolete_warn++;
		}
		if (!capable(CAP_SYS_RAWIO))
			return -EPERM;
		get_user_ret(cfg, (unsigned int *)arg, -EFAULT);
		if (usb_set_configuration(dev, cfg) < 0)
			return -EINVAL;
		return 0;

	}
	return -ENOIOCTLCMD;
}



static int usbdev_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct proc_dir_entry *dp = (struct proc_dir_entry *)inode->u.generic_ip;
	struct usb_device *dev = (struct usb_device *)dp->data;
	struct usb_proc_ctrltransfer ctrl;
	struct usb_proc_bulktransfer bulk;
	struct usb_proc_old_ctrltransfer octrl;
	struct usb_proc_old_bulktransfer obulk;
	struct usb_proc_setinterface setintf;
	unsigned int len1, ep, pipe, cfg;
	unsigned long len2;
	unsigned char *tbuf;
	int i;

	switch (cmd) {
	case USB_PROC_CONTROL:
		if (!capable(CAP_SYS_RAWIO))
			return -EPERM;
		copy_from_user_ret(&ctrl, (void *)arg, sizeof(ctrl), -EFAULT);
		if (ctrl.length > PAGE_SIZE)
			return -EINVAL;
		if (!(tbuf = (unsigned char *)__get_free_page(GFP_KERNEL)))
			return -ENOMEM;
		if (ctrl.requesttype & 0x80) {
			if (ctrl.length && !access_ok(VERIFY_WRITE, ctrl.data, ctrl.length)) {
				free_page((unsigned long)tbuf);
				return -EINVAL;
			}
			i = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), ctrl.request, 
					    ctrl.requesttype, ctrl.value, ctrl.index, tbuf, 
					    ctrl.length, (ctrl.timeout * HZ + 500) / 1000);
			if ((i > 0) && ctrl.length) {
				copy_to_user_ret(ctrl.data, tbuf, ctrl.length, -EFAULT);
			}
		} else {
			if (ctrl.length) {
				copy_from_user_ret(tbuf, ctrl.data, ctrl.length, -EFAULT);
			}
			i = usb_control_msg(dev, usb_sndctrlpipe(dev, 0), ctrl.request, 
					    ctrl.requesttype, ctrl.value, ctrl.index, tbuf, 
					    ctrl.length, (ctrl.timeout * HZ + 500) / 1000);
		}
		free_page((unsigned long)tbuf);
		if (i<0) {
			warn("USB_PROC_CONTROL failed rqt %u rq %u len %u ret %d", 
			       ctrl.requesttype, ctrl.request, ctrl.length, i);
			return i;
		}
		return 0;

	case USB_PROC_BULK:
		if (!capable(CAP_SYS_RAWIO))
			return -EPERM;
		copy_from_user_ret(&bulk, (void *)arg, sizeof(bulk), -EFAULT);
		if (bulk.ep & 0x80)
			pipe = usb_rcvbulkpipe(dev, bulk.ep & 0x7f);
		else
			pipe = usb_sndbulkpipe(dev, bulk.ep & 0x7f);
		if (!usb_maxpacket(dev, pipe, !(bulk.ep & 0x80)))
			return -EINVAL;
		len1 = bulk.len;
		if (len1 > PAGE_SIZE)
			len1 = PAGE_SIZE;
		if (!(tbuf = (unsigned char *)__get_free_page(GFP_KERNEL)))
			return -ENOMEM;
		if (bulk.ep & 0x80) {
			if (len1 && !access_ok(VERIFY_WRITE, bulk.data, len1)) {
				free_page((unsigned long)tbuf);
				return -EINVAL;
			}
			i = usb_bulk_msg(dev, pipe, tbuf, len1, &len2, (bulk.timeout * HZ + 500) / 1000);
			if (!i && len2) {
				copy_to_user_ret(bulk.data, tbuf, len2, -EFAULT);
			}
		} else {
			if (len1) {
				copy_from_user_ret(tbuf, bulk.data, len1, -EFAULT);
			}
			i = usb_bulk_msg(dev, pipe, tbuf, len1, &len2, (bulk.timeout * HZ + 500) / 1000);
		}
		free_page((unsigned long)tbuf);
		if (i) {
			warn("USB_PROC_BULK failed ep 0x%x len %u ret %d", 
			       bulk.ep, bulk.len, i);
			return -ENXIO;
		}
		return len2;

	case USB_PROC_OLD_CONTROL:
		if (!capable(CAP_SYS_RAWIO))
			return -EPERM;
		copy_from_user_ret(&octrl, (void *)arg, sizeof(octrl), -EFAULT);
		if (octrl.length > PAGE_SIZE)
			return -EINVAL;
		if (!(tbuf = (unsigned char *)__get_free_page(GFP_KERNEL)))
			return -ENOMEM;
		if (octrl.requesttype & 0x80) {
			if (octrl.length && !access_ok(VERIFY_WRITE, octrl.data, octrl.length)) {
				free_page((unsigned long)tbuf);
				return -EINVAL;
			}
			i = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), octrl.request, 
					    octrl.requesttype, octrl.value, octrl.index, tbuf, 
					    octrl.length, HZ);
			if ((i > 0) && octrl.length) {
				copy_to_user_ret(octrl.data, tbuf, octrl.length, -EFAULT);
			}
		} else {
			if (octrl.length) {
				copy_from_user_ret(tbuf, octrl.data, octrl.length, -EFAULT);
			}
			i = usb_control_msg(dev, usb_sndctrlpipe(dev, 0), octrl.request, 
					    octrl.requesttype, octrl.value, octrl.index, tbuf, 
					    octrl.length, HZ);
		}
		free_page((unsigned long)tbuf);
		if (i < 0) {
			warn("USB_PROC_OLD_CONTROL failed rqt %u rq %u len %u ret %d", 
			       octrl.requesttype, octrl.request, octrl.length, i);
			return i;
		}
		return 0;

	case USB_PROC_OLD_BULK:
		if (!capable(CAP_SYS_RAWIO))
			return -EPERM;
		copy_from_user_ret(&obulk, (void *)arg, sizeof(obulk), -EFAULT);
		if (obulk.ep & 0x80)
			pipe = usb_rcvbulkpipe(dev, obulk.ep & 0x7f);
		else
			pipe = usb_sndbulkpipe(dev, obulk.ep & 0x7f);
		if (!usb_maxpacket(dev, pipe, !(obulk.ep & 0x80)))
			return -EINVAL;
		len1 = obulk.len;
		if (len1 > PAGE_SIZE)
			len1 = PAGE_SIZE;
		if (!(tbuf = (unsigned char *)__get_free_page(GFP_KERNEL)))
			return -ENOMEM;
		if (obulk.ep & 0x80) {
			if (len1 && !access_ok(VERIFY_WRITE, obulk.data, len1)) {
				free_page((unsigned long)tbuf);
				return -EINVAL;
			}
			i = usb_bulk_msg(dev, pipe, tbuf, len1, &len2, HZ*5);
			if ((i > 0) && len2) {
				copy_to_user_ret(obulk.data, tbuf, len2, -EFAULT);
			}
		} else {
			if (len1) {
				copy_from_user_ret(tbuf, obulk.data, len1, -EFAULT);
			}
			i = usb_bulk_msg(dev, pipe, tbuf, len1, &len2, HZ*5);
		}
		free_page((unsigned long)tbuf);
		if (i < 0) {
			warn("USB_PROC_OLD_BULK failed ep 0x%x len %u ret %d", 
			       obulk.ep, obulk.len, i);
			return i;
		}
		return len2;

	case USB_PROC_RESETEP:
		if (!capable(CAP_SYS_RAWIO))
			return -EPERM;
		get_user_ret(ep, (unsigned int *)arg, -EFAULT);
		if ((ep & ~0x80) >= 16)
			return -EINVAL;
		usb_settoggle(dev, ep & 0xf, !(ep & 0x80), 0);
		return 0;

	case USB_PROC_SETINTERFACE:
		if (!capable(CAP_SYS_RAWIO))
			return -EPERM;
		copy_from_user_ret(&setintf, (void *)arg, sizeof(setintf), -EFAULT);
		if (usb_set_interface(dev, setintf.interface, setintf.altsetting))
			return -EINVAL;
		return 0;

	case USB_PROC_SETCONFIGURATION:
		if (!capable(CAP_SYS_RAWIO))
			return -EPERM;
		get_user_ret(cfg, (unsigned int *)arg, -EFAULT);
		if (usb_set_configuration(dev, cfg) < 0)
			return -EINVAL;
		return 0;

	case EZUSB_CONTROL:
	case EZUSB_BULK:
	case EZUSB_OLD_CONTROL:
	case EZUSB_OLD_BULK:
	case EZUSB_RESETEP:
	case EZUSB_SETINTERFACE:
	case EZUSB_SETCONFIGURATION:
		return usbdev_ioctl_ezusbcompat(inode, file, cmd, arg);
	}
	return -ENOIOCTLCMD;
}

static struct file_operations proc_usb_device_file_operations = {
	usbdev_lseek,    /* lseek   */
	usbdev_read,     /* read    */
	NULL,            /* write   */
	NULL,            /* readdir */
	NULL,            /* poll    */
	usbdev_ioctl,    /* ioctl   */
	NULL,            /* mmap    */
	NULL,            /* no special open code    */
	NULL,            /* flush */
	NULL,            /* no special release code */
	NULL             /* can't fsync */
};

static struct inode_operations proc_usb_device_inode_operations = {
	&proc_usb_device_file_operations,  /* file-ops */
};

void proc_usb_add_bus(struct usb_bus *bus)
{
	char buf[16];

	bus->proc_entry = NULL;
	if (!usbdir)
		return;
	sprintf(buf, "%03d", bus->busnum);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,31)
	if (!(bus->proc_entry = create_proc_entry(buf, S_IFDIR, usbdir)))
#else
	if (!(bus->proc_entry = proc_mkdir(buf, usbdir)))
#endif
		return;
	bus->proc_entry->data = bus;
	conndiscevent();
}

/* devices need already be removed! */
void proc_usb_remove_bus(struct usb_bus *bus)
{
	if (!bus->proc_entry)
		return;
	remove_proc_entry(bus->proc_entry->name, usbdir);
	conndiscevent();
}

void proc_usb_add_device(struct usb_device *dev)
{
	char buf[16];

	dev->proc_entry = NULL;
	if (!dev->bus->proc_entry)
		return;
	sprintf(buf, "%03d", dev->devnum);
	if (!(dev->proc_entry = create_proc_entry(buf, 0, dev->bus->proc_entry)))
		return;
	dev->proc_entry->ops = &proc_usb_device_inode_operations;
	dev->proc_entry->data = dev;
	conndiscevent();
}

void proc_usb_remove_device(struct usb_device *dev)
{
	if (dev->proc_entry)
		remove_proc_entry(dev->proc_entry->name, dev->bus->proc_entry);
	conndiscevent();
}


void proc_usb_cleanup (void)
{
	if (driversdir)
		remove_proc_entry("drivers", usbdir);
	if (devicesdir)
		remove_proc_entry("devices", usbdir);
	if (usbdir)
		remove_proc_entry("usb", proc_bus);
}

int proc_usb_init (void)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,31)
	usbdir = create_proc_entry("usb", S_IFDIR, proc_bus);
#else
	usbdir = proc_mkdir("usb", proc_bus);
#endif	
	if (!usbdir) {
		err("cannot create /proc/bus/usb entry");
		return -1;
	}

	driversdir = create_proc_entry("drivers", 0, usbdir);
	if (!driversdir) {
		err("cannot create /proc/bus/usb/drivers entry");
		proc_usb_cleanup();
		return -1;
	}
	driversdir->ops = &proc_usb_drvlist_inode_operations;

	devicesdir = create_proc_entry("devices", 0, usbdir);
	if (!devicesdir) {
		err("cannot create /proc/bus/usb/devices entry");
		proc_usb_cleanup ();
		return -1;
	}
	devicesdir->ops = &proc_usb_devlist_inode_operations;

	return 0;
}

/* end proc_usb.c */
