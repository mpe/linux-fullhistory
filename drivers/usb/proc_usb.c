/*
 * drivers/usb/proc_usb.c
 * (C) Copyright 1999 Randy Dunlap.
 * (C) Copyright 1999 Thomas Sailer <sailer@ife.ee.ethz.ch>. (proc file per device)
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
 */

#define __KERNEL__	1

#include <linux/types.h>
#include <asm/types.h>
#include <linux/kernel.h>
/* #include <linux/module.h> */
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/bitops.h>
#include <asm/uaccess.h>
#include <linux/mm.h>

#include "usb.h"

#define DUMP_LIMIT		(PAGE_SIZE - 100)
	/* limit to only one memory page of output */

#define MAX_TOPO_LEVEL		6


static char *format_topo =
/* T:  Lev=dd Prnt=dd Port=dd Cnt=dd Dev#=ddd Spd=ddd If#=ddd MxCh=dd Driver=%s */
  "T:  Lev=%2.2d Prnt=%2.2d Port=%2.2d Cnt=%2.2d Dev#=%3d Spd=%3s If#=%3d MxCh=%2d Driver=%s\n";
  
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
/* I:  If#=dd Alt=dd #EPs=dd Cls=xx(sssss) Sub=xx Prot=xx */
  "I:  If#=%2d Alt=%2d #EPs=%2d Cls=%02x(%-5s) Sub=%02x Prot=%02x\n";

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

struct class_info {
	int class;
	char *class_name;
};

struct class_info clas_info [] =
{					/* max. 5 chars. per name string */
	{USB_CLASS_PER_INTERFACE,	">ifc"},
	{USB_CLASS_AUDIO,		"audio"},
	{USB_CLASS_COMM,		"comm."},
	{USB_CLASS_HID,			"HID"},
	{USB_CLASS_HUB,			"hub"},
	{USB_CLASS_PRINTER,		"print"},
	{USB_CLASS_MASS_STORAGE,	"stor."},
	{USB_CLASS_VENDOR_SPEC,		"vend."},
	{-1,				"unk."}		/* leave as last */
};

/*****************************************************************/

static char *class_decode (const int class)
{
	int	ix;

	for (ix = 0; clas_info [ix].class != -1; ix++)
		if (clas_info [ix].class == class)
			break;

	return (clas_info [ix].class_name);
}
static int usb_dump_endpoint_descriptor (const struct usb_endpoint_descriptor *desc,
					char *buf, int *len)
{
	char *EndpointType [4] = {"Ctrl", "Isoc", "Bulk", "Int."};

	*len += sprintf (buf + *len, format_endpt,
		desc->bEndpointAddress,
		(desc->bEndpointAddress & USB_DIR_IN) ? 'I' : 'O',
		desc->bmAttributes,
		EndpointType[desc->bmAttributes & 3],
		desc->wMaxPacketSize,
		desc->bInterval
		);

	return (*len >= DUMP_LIMIT) ? -1 : 0;
}

static int usb_dump_endpoint (const struct usb_endpoint_descriptor *endpoint,
				char *buf, int *len)
{
	if (usb_dump_endpoint_descriptor (endpoint, buf, len) < 0)
		return -1;

	return 0;
}

static int usb_dump_interface_descriptor (const struct usb_interface_descriptor *desc,
						char *buf, int *len)
{
	*len += sprintf (buf + *len, format_iface,
		desc->bInterfaceNumber,
		desc->bAlternateSetting,
		desc->bNumEndpoints,
		desc->bInterfaceClass,
		class_decode (desc->bInterfaceClass),
		desc->bInterfaceSubClass,
		desc->bInterfaceProtocol
		);

	return (*len >= DUMP_LIMIT) ? -1 : 0;
}

static int usb_dump_interface (const struct usb_interface_descriptor *interface,
				char *buf, int *len)
{
	int i;

	if (usb_dump_interface_descriptor (interface, buf, len) < 0)
		return -1;

	for (i = 0; i < interface->bNumEndpoints; i++) {
		if (usb_dump_endpoint (interface->endpoint + i, buf, len) < 0)
			return -1;
	}

	return 0;
}

/* TBD:
 * 0. TBDs
 * 1. marking active config and ifaces (code lists all, but should mark
 *    which ones are active, if any)
 * 2. Add proc_usb_init() call from usb-core.c.
 * 3. proc_usb as a MODULE ?
 * 4. use __init ?
 * 5. add <halted> status to each endpoint line
 */

static int usb_dump_config_descriptor (const struct usb_config_descriptor *desc,
					const int active, char *buf, int *len)
{
	*len += sprintf (buf + *len, format_config,
		active ? '*' : ' ',	/* mark active/actual/current cfg. */
		desc->bNumInterfaces,
		desc->bConfigurationValue,
		desc->bmAttributes,
		desc->MaxPower * 2
		);

	return (*len >= DUMP_LIMIT) ? -1 : 0;
}

static int usb_dump_config (const struct usb_config_descriptor *config,
				const int active, char *buf, int *len)
{
	int i, j;
	struct usb_interface *interface;

	if (!config) {		/* getting these some in 2.3.7; none in 2.3.6 */
		*len += sprintf (buf + *len, "(null Cfg. desc.)\n");
		return 0;
	}

	if (usb_dump_config_descriptor (config, active, buf, len) < 0)
		return -1;

	for (i = 0; i < config->bNumInterfaces; i++) {
		interface = config->interface + i;
		if (!interface)
			break;

		for (j = 0; j < interface->num_altsetting; j++)
			if (usb_dump_interface (interface->altsetting + j, buf, len) < 0)
				return -1;
	}

	return 0;
}

/*
 * Dump the different USB descriptors.
 */
static int usb_dump_device_descriptor (const struct usb_device_descriptor *desc,
				char *buf, int *len)
{
	*len += sprintf (buf + *len, format_device1,
			desc->bcdUSB >> 8, desc->bcdUSB & 0xff,
			desc->bDeviceClass,
			class_decode (desc->bDeviceClass),
			desc->bDeviceSubClass,
			desc->bDeviceProtocol,
			desc->bMaxPacketSize0,
			desc->bNumConfigurations
			);
	if (*len >= DUMP_LIMIT) return -1;

	*len += sprintf (buf + *len, format_device2,
			desc->idVendor, desc->idProduct,
			desc->bcdDevice >> 8, desc->bcdDevice & 0xff
			);

	return (*len >= DUMP_LIMIT) ? -1 : 0;
}

static int usb_dump_desc (const struct usb_device *dev, char *buf, int *len)
{
	int i;

	if (usb_dump_device_descriptor (&dev->descriptor, buf, len) < 0)
		return -1;

	for (i = 0; i < dev->descriptor.bNumConfigurations; i++) {
		if (usb_dump_config (dev->config + i,
			(dev->config + i) == dev->actconfig, /* active ? */
			buf, len) < 0)
				return -1;
	}

	return 0;
}

#ifdef PROC_EXTRA /* TBD: may want to add this code later */

static int usb_dump_hub_descriptor (const struct usb_hub_descriptor * desc,
					char *buf, int *len)
{
	int leng = USB_DT_HUB_NONVAR_SIZE;
	unsigned char *ptr = (unsigned char *) desc;

	*len += sprintf (buf + *len, "Interface:");

	while (leng) {
		*len += sprintf (buf + *len, " %02x", *ptr);
		ptr++; leng--;
	}
	*len += sprintf (buf + *len, "\n");

	return (*len >= DUMP_LIMIT) ? -1 : 0;
}

static int usb_dump_string (const struct usb_device *dev, char *id, int index,
				char *buf, int *len)
{
	if (index <= dev->maxstring && dev->stringindex && dev->stringindex[index])
		*len += sprintf (buf + *len, "%s: %s ", id, dev->stringindex[index]);

	return (*len >= DUMP_LIMIT) ? -1 : 0;
}

#endif /* PROC_EXTRA */

/*****************************************************************/

static int usb_device_dump (char *buf, int *len,
			const struct usb_device *usbdev,
			int level, int index, int count)
{
	int	chix;
	int	cnt = 0;
	int	parent_devnum;

	if (level > MAX_TOPO_LEVEL) return -1;

	parent_devnum = usbdev->parent ? (usbdev->parent->devnum == -1) ? 0
			: usbdev->parent->devnum : 0;
		/*
		 * So the root hub's parent is 0 and any device that is
		 * plugged into the root hub has a parent of 0.
		 */
	*len += sprintf (buf + *len, format_topo,
		level, parent_devnum, index, count,
		usbdev->devnum,
		usbdev->slow ? "1.5" : "12 ",
		usbdev->ifnum, usbdev->maxchild,
		usbdev->driver ? usbdev->driver->name :
		(level == 0) ? "(root hub)" : "(none)"
		);
		/*
		 * level = topology-tier level;
		 * parent_devnum = parent device number;
		 * index = parent's connector number;
		 * count = device count at this level
		 */

	if (*len >= DUMP_LIMIT)
		return -1;

	if (usbdev->devnum > 0) {	/* for any except root hub */
		if (usb_dump_desc (usbdev, buf, len) < 0)
			return -1;
	}

	/* Now look at all of this device's children. */
	for (chix = 0; chix < usbdev->maxchild; chix++) {
		if (usbdev->children [chix]) {
			if (usb_device_dump (buf, len,
				usbdev->children [chix],
				level + 1, chix, ++cnt) < 0)
					return -1;
		}
	}

	return 0;
}

static int usb_bus_list_dump (char *buf, int len)
{
	struct list_head *usb_bus_list = usb_bus_get_list ();
	struct list_head *list = usb_bus_list->next;

	len = 0;

	/*
	 * Go thru each usb_bus. Within each usb_bus: each usb_device.
	 * Within each usb_device: all of its device & config. descriptors,
	 * marking the currently active ones.
	 */


        while (list != usb_bus_list) {
		struct usb_bus *bus = list_entry (list, struct usb_bus, bus_list);

		if (usb_device_dump (buf, &len, bus->root_hub, 0, 0, 0)
			< 0)
			break;

	        list = list->next;

		if (len >= DUMP_LIMIT) {
			len += sprintf (buf + len, "(truncated)\n");
			break;
		}
        }

	return (len);
}

static int usb_bus_list_dump_devices (char *buf, char **start, off_t offset,
				int len, int *eof, void *data)
{
	return usb_bus_list_dump (buf, len);
}

/*
 * Dump usb_driver_list.
 *
 * We now walk the list of registered USB drivers.
 */
static int usb_driver_list_dump (char *buf, char **start, off_t offset,
				int len, int *eof, void *data)
{
	struct list_head *usb_driver_list = usb_driver_get_list ();
	struct list_head *tmp = usb_driver_list->next;
	int cnt = 0;

	len = 0;

	while (tmp != usb_driver_list) {
		struct usb_driver *driver = list_entry (tmp, struct usb_driver,
						       driver_list);
		len += sprintf (buf + len, "%s\n", driver->name);
		cnt++;
		tmp = tmp->next;

		if (len >= DUMP_LIMIT)
		{
			len += sprintf (buf + len, "(truncated)\n");
			return (len);
		}
	}

	if (!cnt)
		len += sprintf (buf + len, "(none)\n");
	return (len);
}

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
	struct ezusb_setinterface setintf;
	unsigned int len1, ep, pipe;
	unsigned long len2;
	unsigned char *tbuf;
	int i;

	switch (cmd) {
	case EZUSB_CONTROL:
		if (obsolete_warn < 20) {
			printk(KERN_WARNING "/proc/bus/usb: process %d (%s) used obsolete EZUSB_CONTROL ioctl\n",
			       current->pid, current->comm);
			obsolete_warn++;
		}
		if (!capable(CAP_SYS_RAWIO))
			return -EPERM;
		copy_from_user_ret(&ctrl, (void *)arg, sizeof(ctrl), -EFAULT);
		if (ctrl.dlen > PAGE_SIZE)
			return -EINVAL;
		if (!(tbuf = (unsigned char *)__get_free_page(GFP_KERNEL)))
			return -ENOMEM;
		if (ctrl.requesttype & 0x80) {
			if (ctrl.dlen && !access_ok(VERIFY_WRITE, ctrl.data, ctrl.dlen)) {
				free_page((unsigned long)tbuf);
				return -EINVAL;
			}
			i = dev->bus->op->control_msg(dev, usb_rcvctrlpipe(dev, 0), (devrequest *)&ctrl, tbuf, ctrl.dlen);
			if (!i && ctrl.dlen) {
				copy_to_user_ret(ctrl.data, tbuf, ctrl.dlen, -EFAULT);
			}
		} else {
			if (ctrl.dlen) {
				copy_from_user_ret(tbuf, ctrl.data, ctrl.dlen, -EFAULT);
			}
			i = dev->bus->op->control_msg(dev, usb_sndctrlpipe(dev, 0), (devrequest *)&ctrl, tbuf, ctrl.dlen);
		}
		free_page((unsigned long)tbuf);
		if (i) {
			printk(KERN_WARNING "procusb: EZUSB_CONTROL failed rqt %u rq %u len %u ret %d\n", 
			       ctrl.requesttype, ctrl.request, ctrl.length, i);
			return -ENXIO;
		}
		return 0;

	case EZUSB_BULK:
		if (obsolete_warn < 20) {
			printk(KERN_WARNING "/proc/bus/usb: process %d (%s) used obsolete EZUSB_BULK ioctl\n",
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
			i = dev->bus->op->bulk_msg(dev, pipe, tbuf, len1, &len2);
			if (!i && len2) {
				copy_to_user_ret(bulk.data, tbuf, len2, -EFAULT);
			}
		} else {
			if (len1) {
				copy_from_user_ret(tbuf, bulk.data, len1, -EFAULT);
			}
			i = dev->bus->op->bulk_msg(dev, pipe, tbuf, len1, &len2);
		}
		free_page((unsigned long)tbuf);
		if (i) {
			printk(KERN_WARNING "procusb: EZUSB_BULK failed ep 0x%x len %u ret %d\n", 
			       bulk.ep, bulk.len, i);
			return -ENXIO;
		}
		return len2;

	case EZUSB_RESETEP:
		if (obsolete_warn < 20) {
			printk(KERN_WARNING "/proc/bus/usb: process %d (%s) used obsolete EZUSB_RESETEP ioctl\n",
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
			printk(KERN_WARNING "/proc/bus/usb: process %d (%s) used obsolete EZUSB_SETINTERFACE ioctl\n",
			       current->pid, current->comm);
			obsolete_warn++;
		}
		if (!capable(CAP_SYS_RAWIO))
			return -EPERM;
		copy_from_user_ret(&setintf, (void *)arg, sizeof(setintf), -EFAULT);
		if (usb_set_interface(dev, setintf.interface, setintf.altsetting))
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
	struct usb_proc_setinterface setintf;
	unsigned int len1, ep, pipe;
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
					    ctrl.length);
			if (!i && ctrl.length) {
				copy_to_user_ret(ctrl.data, tbuf, ctrl.length, -EFAULT);
			}
		} else {
			if (ctrl.length) {
				copy_from_user_ret(tbuf, ctrl.data, ctrl.length, -EFAULT);
			}
			i = usb_control_msg(dev, usb_sndctrlpipe(dev, 0), ctrl.request, 
					    ctrl.requesttype, ctrl.value, ctrl.index, tbuf, 
					    ctrl.length);
		}
		free_page((unsigned long)tbuf);
		if (i) {
			printk(KERN_WARNING "/proc/bus/usb: USB_PROC_CONTROL failed rqt %u rq %u len %u ret %d\n", 
			       ctrl.requesttype, ctrl.request, ctrl.length, i);
			return -ENXIO;
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
			i = dev->bus->op->bulk_msg(dev, pipe, tbuf, len1, &len2);
			if (!i && len2) {
				copy_to_user_ret(bulk.data, tbuf, len2, -EFAULT);
			}
		} else {
			if (len1) {
				copy_from_user_ret(tbuf, bulk.data, len1, -EFAULT);
			}
			i = dev->bus->op->bulk_msg(dev, pipe, tbuf, len1, &len2);
		}
		free_page((unsigned long)tbuf);
		if (i) {
			printk(KERN_WARNING "/proc/bus/usb: USB_PROC_BULK failed ep 0x%x len %u ret %d\n", 
			       bulk.ep, bulk.len, i);
			return -ENXIO;
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

	case EZUSB_CONTROL:
	case EZUSB_BULK:
	case EZUSB_RESETEP:
	case EZUSB_SETINTERFACE:
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
        NULL,                              /* create       */
        NULL,                              /* lookup       */
        NULL,                              /* link         */
        NULL,                              /* unlink       */
        NULL,                              /* symlink      */
        NULL,                              /* mkdir        */
        NULL,                              /* rmdir        */
        NULL,                              /* mknod        */
        NULL,                              /* rename       */
        NULL,                              /* readlink     */
        NULL,                              /* follow_link  */
        NULL,                              /* get_block    */
        NULL,                              /* readpage     */
        NULL,                              /* writepage    */
        NULL,                              /* flushpage    */
        NULL,                              /* truncate     */
        NULL,                              /* permission   */
        NULL,                              /* smap         */
        NULL                               /* revalidate   */
};

#define PROCUSB_MAXBUSSES 64

static unsigned long busnumbermap[(PROCUSB_MAXBUSSES+8 * sizeof(unsigned long)-1) / (8 * sizeof(unsigned long))] = { 0, };

void proc_usb_add_bus(struct usb_bus *bus)
{
	int bnum;
	char buf[16];

	bus->proc_busnum = -1;
	bus->proc_entry = NULL;
	if (!usbdir)
		return;
	bnum = find_first_zero_bit(busnumbermap, PROCUSB_MAXBUSSES);
	if (bnum >= PROCUSB_MAXBUSSES)
		return;
	sprintf(buf, "%03d", bnum);
	if (!(bus->proc_entry = create_proc_entry(buf, S_IFDIR, usbdir)))
		return;
	set_bit(bnum, busnumbermap);
	bus->proc_busnum = bnum;
	bus->proc_entry->data = bus;
}

/* devices need already be removed! */
void proc_usb_remove_bus(struct usb_bus *bus)
{
	if (!bus->proc_entry)
		return;
	remove_proc_entry(bus->proc_entry->name, usbdir);
	clear_bit(bus->proc_busnum, busnumbermap);
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
}

void proc_usb_remove_device(struct usb_device *dev)
{
	if (dev->proc_entry)
		remove_proc_entry(dev->proc_entry->name, dev->bus->proc_entry);
}


void proc_usb_cleanup (void)
{
	if (driversdir)
		remove_proc_entry ("drivers", usbdir);
	if (devicesdir)
		remove_proc_entry ("devices", usbdir);
	if (usbdir)
		remove_proc_entry ("usb", proc_bus);
}

int proc_usb_init (void)
{
	usbdir = create_proc_entry ("usb", S_IFDIR, proc_bus);
	if (!usbdir) {
		printk ("proc_usb: cannot create /proc/bus/usb entry\n");
		return -1;
	}

	driversdir = create_proc_entry ("drivers", 0, usbdir);
	if (!driversdir) {
		printk ("proc_usb: cannot create /proc/bus/usb/drivers entry\n");
		proc_usb_cleanup ();
		return -1;
	}
	driversdir->read_proc = usb_driver_list_dump;

	devicesdir = create_proc_entry ("devices", 0, usbdir);
	if (!devicesdir) {
		printk ("proc_usb: cannot create /proc/bus/usb/devices entry\n");
		proc_usb_cleanup ();
		return -1;
	}
	devicesdir->read_proc = usb_bus_list_dump_devices;

	return 0;
}

#ifdef PROCFS_MODULE /* TBD: support proc_fs MODULE ??? */

int init_module (void)
{
	return proc_usb_init ();
}

void cleanup_module (void)
{
	proc_usb_cleanup ();
}

#endif /* PROCFS_MODULE */

/* end proc_usb.c */
