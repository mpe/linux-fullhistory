/*****************************************************************************/

/*
 *	ezusb.c  --  Firmware download miscdevice for Anchorchips EZUSB microcontrollers.
 *
 *	Copyright (C) 1999
 *          Thomas Sailer (sailer@ife.ee.ethz.ch)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  History:
 *   0.1  26.05.99  Created
 *   0.2  23.07.99  Removed EZUSB_INTERRUPT. Interrupt pipes may be polled with
 *                  bulk reads.
 *                  Implemented EZUSB_SETINTERFACE, more sanity checks for EZUSB_BULK.
 *                  Preliminary ISO support
 *   0.3  01.09.99  Async Bulk and ISO support
 *   0.4  01.09.99  Set callback_frames to the total number of frames to make
 *                  it work with OHCI-HCD
 *
 */

/*****************************************************************************/

#include <linux/module.h>
#include <linux/socket.h>
#include <linux/list.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <asm/uaccess.h>

#include <linux/spinlock.h>

#include "usb.h"
#include "ezusb.h"

/* --------------------------------------------------------------------- */

#define NREZUSB 1

static struct ezusb {
	struct semaphore mutex;
	struct usb_device *usbdev;
	struct list_head async_pending;
	struct list_head async_completed;
	wait_queue_head_t wait;
	spinlock_t lock;
} ezusb[NREZUSB];

struct async {
	struct list_head asynclist;
	struct ezusb *ez;
	void *data;
	unsigned dataorder;
	void *userdata;
	unsigned datalen;
	union {
		struct usb_isoc_desc *iso;
		void *bulk;
	} desc;
	unsigned numframes; /* 0 means bulk, > 0 means iso */
	struct ezusb_asynccompleted completed;
};

/* --------------------------------------------------------------------- */

extern inline unsigned int ld2(unsigned int x)
{
        unsigned int r = 0;
        
        if (x >= 0x10000) {
                x >>= 16;
                r += 16;
        }
        if (x >= 0x100) {
                x >>= 8;
                r += 8;
        }
        if (x >= 0x10) {
                x >>= 4;
                r += 4;
        }
        if (x >= 4) {
                x >>= 2;
                r += 2;
        }
        if (x >= 2)
                r++;
        return r;
}

#if 0
/* why doesn't this work properly on i386? */
extern inline unsigned int ld2(unsigned int x)
{
        unsigned int r;

	__asm__("bsrl %1,%0" : "=r" (r) : "g" (x));
	return r;
}
#endif

/* --------------------------------------------------------------------- */

extern __inline__ void async_removelist(struct async *as)
{
	struct ezusb *ez = as->ez;
	unsigned long flags;

	spin_lock_irqsave(&ez->lock, flags);
	list_del(&as->asynclist);
	INIT_LIST_HEAD(&as->asynclist);
	spin_unlock_irqrestore(&ez->lock, flags);
}

extern __inline__ void async_newpending(struct async *as)
{
	struct ezusb *ez = as->ez;
	unsigned long flags;
	
	spin_lock_irqsave(&ez->lock, flags);
	list_add_tail(&as->asynclist, &ez->async_pending);
	spin_unlock_irqrestore(&ez->lock, flags);
}

extern __inline__ void async_movetocompleted(struct async *as)
{
	struct ezusb *ez = as->ez;
	unsigned long flags;

	spin_lock_irqsave(&ez->lock, flags);
	list_del(&as->asynclist);
	list_add_tail(&as->asynclist, &ez->async_completed);
	spin_unlock_irqrestore(&ez->lock, flags);
}

extern __inline__ struct async *async_getcompleted(struct ezusb *ez)
{
	unsigned long flags;
	struct async *as = NULL;

	spin_lock_irqsave(&ez->lock, flags);
	if (!list_empty(&ez->async_completed)) {
		as = list_entry(ez->async_completed.next, struct async, asynclist);
		list_del(&as->asynclist);
		INIT_LIST_HEAD(&as->asynclist);
	}
	spin_unlock_irqrestore(&ez->lock, flags);
	return as;
}

extern __inline__ struct async *async_getpending(struct ezusb *ez, void *context)
{
	unsigned long flags;
	struct async *as;
	struct list_head *p;

	spin_lock_irqsave(&ez->lock, flags);
	for (p = ez->async_pending.next; p != &ez->async_pending; ) {
		as = list_entry(p, struct async, asynclist);
		p = p->next;
		if (as->completed.context != context)
			continue;
		list_del(&as->asynclist);
		INIT_LIST_HEAD(&as->asynclist);
		spin_unlock_irqrestore(&ez->lock, flags);
		return as;
	}
	spin_unlock_irqrestore(&ez->lock, flags);
	return NULL;
}

/* --------------------------------------------------------------------- */

static int bulk_completed(int status, void *__buffer, int rval, void *dev_id)
{
	struct async *as = (struct async *)dev_id;
	struct ezusb *ez = as->ez;
	unsigned cnt;

printk(KERN_DEBUG "ezusb: bulk_completed: status %d rval %d\n", status, rval);
	as->completed.length = rval;
	as->completed.status = status;
	spin_lock(&ez->lock);
	list_del(&as->asynclist);
	list_add_tail(&as->asynclist, &ez->async_completed);
	spin_unlock(&ez->lock);
	wake_up(&ez->wait);
	return 0;
}

static int iso_completed(int status, void *__buffer, int rval, void *dev_id)
{
#if 1
	struct async *as = (struct async *)dev_id;
#else
	struct usb_isoc_desc *id = (struct usb_isoc_desc *)dev_id;
	struct async *as = (struct async *)id->context;
#endif
	struct ezusb *ez = as->ez;
	unsigned cnt;

printk(KERN_DEBUG "ezusb: iso_completed: status %d rval %d\n", status, rval);
	as->completed.length = rval;
	as->completed.status = USB_ST_NOERROR;
	for (cnt = 0; cnt < as->numframes; cnt++) {
		as->completed.isostat[cnt].status = as->desc.iso->frames[cnt].frame_status;
		as->completed.isostat[cnt].length = as->desc.iso->frames[cnt].frame_length;
	}
	spin_lock(&ez->lock);
	list_del(&as->asynclist);
	list_add_tail(&as->asynclist, &ez->async_completed);
	spin_unlock(&ez->lock);
	wake_up(&ez->wait);
	return 0;
}

static void remove_async(struct async *as)
{
	if (as->data && as->dataorder)
		free_pages((unsigned long)as->data, as->dataorder);
	if (as->numframes)
		usb_free_isoc(as->desc.iso);
	kfree(as);
}

static void kill_async(struct async *as)
{
	struct ezusb *ez = as->ez;

	if (as->numframes)
		/* ISO case */
		usb_kill_isoc(as->desc.iso);
	else
		usb_terminate_bulk(ez->usbdev, as->desc.bulk);
	as->completed.status = USB_ST_REMOVED;
	async_movetocompleted(as);
}

static void destroy_all_async(struct ezusb *ez)
{
	struct async *as;
	unsigned long flags;

	spin_lock_irqsave(&ez->lock, flags);
	if (!list_empty(&ez->async_pending)) {
		as = list_entry(ez->async_pending.next, struct async, asynclist);
		list_del(&as->asynclist);
		INIT_LIST_HEAD(&as->asynclist);
		spin_unlock_irqrestore(&ez->lock, flags);
		kill_async(as);
		spin_lock_irqsave(&ez->lock, flags);
	}
	spin_unlock_irqrestore(&ez->lock, flags);
	while ((as = async_getcompleted(ez)))
		remove_async(as);
}

/* --------------------------------------------------------------------- */

static loff_t ezusb_llseek(struct file *file, loff_t offset, int origin)
{
	struct ezusb *ez = (struct ezusb *)file->private_data;

	switch(origin) {
	case 1:
		offset += file->f_pos;
		break;
	case 2:
		offset += 0x10000;
		break;
	}
	if (offset < 0 || offset >= 0x10000)
		return -EINVAL;
	return (file->f_pos = offset);
}

static ssize_t ezusb_read(struct file *file, char *buf, size_t sz, loff_t *ppos)
{
	struct ezusb *ez = (struct ezusb *)file->private_data;
	unsigned pos = *ppos;
	unsigned ret = 0;
	unsigned len;
	unsigned char b[64];
	int i;

	if (*ppos < 0 || *ppos >= 0x10000)
		return -EINVAL;
	down(&ez->mutex);
	if (!ez->usbdev) {
		up(&ez->mutex);
		return -EIO;
	}
	while (sz > 0 && pos < 0x10000) {
		len = sz;
		if (len > sizeof(b))
			len = sizeof(b);
		if (pos + len > 0x10000)
			len = 0x10000 - pos;
		i = usb_control_msg(ez->usbdev, usb_rcvctrlpipe(ez->usbdev, 0), 0xa0, 0xc0, pos, 0, b, len, HZ);
		if (i < 0) {
			up(&ez->mutex);
			printk(KERN_WARNING "ezusb: upload failed pos %u len %u ret %d\n", pos, len, i);
			*ppos = pos;
			if (ret)
				return ret;
			return -ENXIO;
		}
		if (copy_to_user(buf, b, len)) {
			up(&ez->mutex);
			*ppos = pos;
			if (ret)
				return ret;
			return -EFAULT;
		}
		pos += len;
		buf += len;
		sz -= len;
		ret += len;
	}
	up(&ez->mutex);
	*ppos = pos;
	return ret;
}

static ssize_t ezusb_write(struct file *file, const char *buf, size_t sz, loff_t *ppos)
{
	struct ezusb *ez = (struct ezusb *)file->private_data;
	unsigned pos = *ppos;
	unsigned ret = 0;
	unsigned len;
	unsigned char b[64];
	int i;

	if (*ppos < 0 || *ppos >= 0x10000)
		return -EINVAL;
	down(&ez->mutex);
	if (!ez->usbdev) {
		up(&ez->mutex);
		return -EIO;
	}
	while (sz > 0 && pos < 0x10000) {
		len = sz;
		if (len > sizeof(b))
			len = sizeof(b);
		if (pos + len > 0x10000)
			len = 0x10000 - pos;
		if (copy_from_user(b, buf, len)) {
			up(&ez->mutex);
			*ppos = pos;
			if (ret)
				return ret;
			return -EFAULT;
		}
		i = usb_control_msg(ez->usbdev, usb_sndctrlpipe(ez->usbdev, 0), 0xa0, 0x40, pos, 0, b, len, HZ);
		if (i < 0) {
			up(&ez->mutex);
			printk(KERN_WARNING "ezusb: download failed pos %u len %u ret %d\n", pos, len, i);
			*ppos = pos;
			if (ret)
				return ret;
			return -ENXIO;
		}
		pos += len;
		buf += len;
		sz -= len;
		ret += len;
	}
	up(&ez->mutex);
	*ppos = pos;
	return ret;
}

static int ezusb_open(struct inode *inode, struct file *file)
{
	struct ezusb *ez = &ezusb[0];

	down(&ez->mutex);
	while (!ez->usbdev) {
		up(&ez->mutex);
		if (!(file->f_flags & O_NONBLOCK)) {
			return -EIO;
		}
		schedule_timeout(HZ/2);
		if (signal_pending(current))
			return -EAGAIN;
		down(&ez->mutex);
	}
	up(&ez->mutex);
	file->f_pos = 0;
	file->private_data = ez;
	MOD_INC_USE_COUNT;
	return 0;
}

static int ezusb_release(struct inode *inode, struct file *file)
{
	struct ezusb *ez = (struct ezusb *)file->private_data;

	down(&ez->mutex);
	destroy_all_async(ez);
	up(&ez->mutex);
	MOD_DEC_USE_COUNT;
	return 0;
}

static int ezusb_control(struct usb_device *usbdev, unsigned char requesttype,
			 unsigned char request, unsigned short value, 
			 unsigned short index, unsigned short length,
			 unsigned int timeout, void *data)
{
	unsigned char *tbuf = NULL;
	unsigned int pipe;
	int i;

	if (length > PAGE_SIZE)
		return -EINVAL;
	/* __range_ok is broken; 
	   with unsigned short size, it gave
	   addl %si,%edx ; sbbl %ecx,%ecx; cmpl %edx,12(%eax); sbbl $0,%ecx
	*/
	if (requesttype & 0x80) {
		pipe = usb_rcvctrlpipe(usbdev, 0);
		if (length > 0 && !access_ok(VERIFY_WRITE, data, (unsigned int)length))
			return -EFAULT;
	} else
		pipe = usb_sndctrlpipe(usbdev, 0);
	if (length > 0) {
		if (!(tbuf = (unsigned char *)__get_free_page(GFP_KERNEL)))
			return -ENOMEM;
		if (!(requesttype & 0x80)) {
			if (copy_from_user(tbuf, data, length)) {
				free_page((unsigned long)tbuf);
				return -EFAULT;
			}
		}
	}
	i = usb_control_msg(usbdev, pipe, request, requesttype, value, index, tbuf, length, timeout);
	if (i < 0) {
		if (length > 0)
			free_page((unsigned long)tbuf);
		printk(KERN_WARNING "ezusb: EZUSB_CONTROL failed rqt %u rq %u len %u ret %d\n", 
		       requesttype, request, length, i);
		return -ENXIO;
	}
	if (requesttype & 0x80 && length > 0 && copy_to_user(data, tbuf, length))
		i = -EFAULT;
	if (length > 0)
		free_page((unsigned long)tbuf);
	return i;
}

static int ezusb_bulk(struct usb_device *usbdev, unsigned int ep, unsigned int length, unsigned int timeout, void *data)
{
	unsigned char *tbuf = NULL;
	unsigned int pipe;
	unsigned long len2 = 0;
	int ret = 0;

	if (length > PAGE_SIZE)
		return -EINVAL;
	if ((ep & ~0x80) >= 16)
		return -EINVAL;
	if (ep & 0x80) {
		pipe = usb_rcvbulkpipe(usbdev, ep & 0x7f);
		if (length > 0 && !access_ok(VERIFY_WRITE, data, length))
			return -EFAULT;
	} else
		pipe = usb_sndbulkpipe(usbdev, ep & 0x7f);
	if (!usb_maxpacket(usbdev, pipe, !(ep & 0x80)))
		return -EINVAL;
	if (length > 0) {
		if (!(tbuf = (unsigned char *)__get_free_page(GFP_KERNEL)))
			return -ENOMEM;
		if (!(ep & 0x80)) {
			if (copy_from_user(tbuf, data, length)) {
				free_page((unsigned long)tbuf);
				return -EFAULT;
			}
		}
	}
	ret = usb_bulk_msg(usbdev, pipe, tbuf, length, &len2, timeout);
	if (ret < 0) {
		if (length > 0)
			free_page((unsigned long)tbuf);
		printk(KERN_WARNING "ezusb: EZUSB_BULK failed ep 0x%x len %u ret %d\n", 
		       ep, length, ret);
		return -ENXIO;
	}
	if (len2 > length)
		len2 = length;
	ret = len2;
	if (ep & 0x80 && len2 > 0 && copy_to_user(data, tbuf, len2))
		ret = -EFAULT;
	if (length > 0)
		free_page((unsigned long)tbuf);
	return ret;
}

static int ezusb_resetep(struct usb_device *usbdev, unsigned int ep)
{
	if ((ep & ~0x80) >= 16)
		return -EINVAL;
	usb_settoggle(usbdev, ep & 0xf, !(ep & 0x80), 0);
	return 0;
}

static int ezusb_setinterface(struct usb_device *usbdev, unsigned int interface, unsigned int altsetting)
{
	if (usb_set_interface(usbdev, interface, altsetting) < 0)
		return -EINVAL;
	return 0;
}

static int ezusb_setconfiguration(struct usb_device *usbdev, unsigned int config)
{
	if (usb_set_configuration(usbdev, config) < 0)
		return -EINVAL;
	return 0;
}

static int ezusb_requestbulk(struct ezusb *ez, struct ezusb_asyncbulk *ab)
{
	struct async *as = NULL;
	unsigned int pipe;

	if (ab->len > PAGE_SIZE)
		return -EINVAL;
	if ((ab->ep & ~0x80) >= 16)
		return -EINVAL;
	if (ab->ep & 0x80) {
		pipe = usb_rcvbulkpipe(ez->usbdev, ab->ep & 0x7f);
		if (ab->len > 0 && !access_ok(VERIFY_WRITE, ab->data, ab->len))
			return -EFAULT;
	} else
		pipe = usb_sndbulkpipe(ez->usbdev, ab->ep & 0x7f);
	if (!usb_maxpacket(ez->usbdev, pipe, !(ab->ep & 0x80)))
		return -EINVAL;
	if (!(as = kmalloc(sizeof(struct async), GFP_KERNEL)))
		return -ENOMEM;
	INIT_LIST_HEAD(&as->asynclist);
	as->ez = ez;
	as->userdata = ab->data;
	as->numframes = 0;
	as->data = 0;
	as->dataorder = 0;
	as->datalen = ab->len;
	as->completed.context = ab->context;
	if (ab->len > 0) {
		as->dataorder = 1;
		if (!(as->data = (unsigned char *)__get_free_page(GFP_KERNEL))) {
			kfree(as);
			return -ENOMEM;
		}
		if (!(ab->ep & 0x80)) {
			if (copy_from_user(as->data, ab->data, ab->len))
				goto err_fault;
			as->datalen = 0; /* no need to copy back at completion */
		}
	}
	async_newpending(as);
	if (!(as->desc.bulk = usb_request_bulk(ez->usbdev, pipe, bulk_completed, as->data, ab->len, as))) {
		async_removelist(as);
		goto err_inval;
	}
	return 0;

 err_fault:
	if (as) {
		if (as->data)
			free_page((unsigned long)as->data);
		kfree(as);
	}
	return -EFAULT;

 err_inval:
	if (as) {
		if (as->data)
			free_page((unsigned long)as->data);
		kfree(as);
	}
	return -EINVAL;
}

static int ezusb_requestiso(struct ezusb *ez, struct ezusb_asynciso *ai, unsigned char *cmd)
{
	struct async *as;
	unsigned int maxpkt, pipe;
	unsigned int dsize, order, assize, j;
	int i;

	if ((ai->ep & ~0x80) >= 16 || ai->framecnt < 1 || ai->framecnt > 128)
		return -EINVAL;
	if (ai->ep & 0x80)
		pipe = usb_rcvisocpipe(ez->usbdev, ai->ep & 0x7f);
	else
		pipe = usb_sndisocpipe(ez->usbdev, ai->ep & 0x7f);
	if (!(maxpkt = usb_maxpacket(ez->usbdev, pipe, !(ai->ep & 0x80))))
		return -EINVAL;
	dsize = maxpkt * ai->framecnt;
printk(KERN_DEBUG "ezusb: iso: dsize %d\n", dsize);
	if (dsize > 65536) 
		return -EINVAL;
	order = ld2(dsize >> PAGE_SHIFT);
	if (dsize > (PAGE_SIZE << order))
		order++;
	if (ai->ep & 0x80)
		if (dsize > 0 && !access_ok(VERIFY_WRITE, ai->data, dsize))
			return -EFAULT;
	assize = sizeof(struct async) + ai->framecnt * sizeof(struct ezusb_isoframestat);
	if (!(as = kmalloc(assize, GFP_KERNEL)))
		return -ENOMEM;
printk(KERN_DEBUG "ezusb: iso: assize %d\n", assize);
	memset(as, 0, assize);
	INIT_LIST_HEAD(&as->asynclist);
	as->ez = ez;
	as->userdata = ai->data;
	as->numframes = ai->framecnt;
	as->data = 0;
	as->dataorder = order;
	as->datalen = dsize;
	as->completed.context = ai->context;
	as->desc.iso = NULL;
	if (dsize > 0) {
		if (!(as->data = (unsigned char *)__get_free_pages(GFP_KERNEL, order))) {
			kfree(as);
			return -ENOMEM;
		}
		if (!(ai->ep & 0x80)) {
			if (copy_from_user(as->data, ai->data, dsize))
				goto err_fault;
			as->datalen = 0; /* no need to copy back at completion */
		}
	}
	if ((i = usb_init_isoc(ez->usbdev, pipe, ai->framecnt, as, &as->desc.iso))) {
		printk(KERN_DEBUG "ezusb: usb_init_isoc error %d\n", i);
		goto err_inval;
	}
	as->desc.iso->start_type = START_ABSOLUTE;
	as->desc.iso->start_frame = ai->startframe;
	as->desc.iso->callback_frames = /*0*/ai->framecnt;
	as->desc.iso->callback_fn = iso_completed;
	as->desc.iso->data = as->data;
	as->desc.iso->buf_size = dsize;
	for (j = 0; j < ai->framecnt; j++) {
		if (get_user(i, (int *)(cmd + j * sizeof(struct ezusb_isoframestat)))) {
			usb_free_isoc(as->desc.iso);
			kfree(as);
			return -EFAULT;
		}
		if (i < 0)
			i = 0;
		as->desc.iso->frames[j].frame_length = i;
	}
       	async_newpending(as);
	if ((i = usb_run_isoc(as->desc.iso, NULL))) {
		printk(KERN_DEBUG "ezusb: usb_run_isoc error %d\n", i);
		async_removelist(as);
		goto err_inval;
	}
	return 0;

 err_fault:
	if (as) {
		if (as->desc.iso)
			usb_free_isoc(as->desc.iso);
		if (as->data)
			free_page((unsigned long)as->data);
		kfree(as);
	}
	return -EFAULT;

 err_inval:
	if (as) {
		if (as->desc.iso)
			usb_free_isoc(as->desc.iso);
		if (as->data)
			free_page((unsigned long)as->data);
		kfree(as);
	}
	return -EINVAL;
}

static int ezusb_terminateasync(struct ezusb *ez, void *context)
{
	struct async *as;
	int ret = 0;

	while ((as = async_getpending(ez, context))) {
		kill_async(as);
		ret++;
	}
	return ret;
}

static int ezusb_asynccompl(struct async *as, void *arg)
{
	if (as->datalen > 0) {
		if (copy_to_user(as->userdata, as->data, as->datalen)) {
			remove_async(as);
			return -EFAULT;
		}
	}
	if (copy_to_user(arg, &as->completed, 
			 sizeof(struct ezusb_asynccompleted) + 
			 as->numframes * sizeof(struct ezusb_isoframestat))) {
		remove_async(as);
		return -EFAULT;
	}
	remove_async(as);
	return 0;
}

static int ezusb_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct ezusb *ez = (struct ezusb *)file->private_data;
        DECLARE_WAITQUEUE(wait, current);
	struct usb_proc_ctrltransfer pctrl;
	struct usb_proc_bulktransfer pbulk;
	struct usb_proc_old_ctrltransfer opctrl;
	struct usb_proc_old_bulktransfer opbulk;
	struct usb_proc_setinterface psetintf;
	struct ezusb_ctrltransfer ctrl;
	struct ezusb_bulktransfer bulk;
	struct ezusb_old_ctrltransfer octrl;
	struct ezusb_old_bulktransfer obulk;
	struct ezusb_setinterface setintf;
	struct ezusb_asyncbulk abulk;
	struct ezusb_asynciso aiso;
	struct async *as;
	void *context;
	unsigned int ep, cfg;
	int i, ret = 0;

	down(&ez->mutex);
	if (!ez->usbdev) {
		up(&ez->mutex);
		return -EIO;
	}
	switch (cmd) {
	case USB_PROC_CONTROL:
		if (copy_from_user(&pctrl, (void *)arg, sizeof(pctrl))) {
			ret = -EFAULT;
			break;
		}
		ret = ezusb_control(ez->usbdev, pctrl.requesttype, pctrl.request, 
				    pctrl.value, pctrl.index, pctrl.length, 
				    (pctrl.timeout * HZ + 500) / 1000, pctrl.data);
		break;

	case USB_PROC_BULK:
		if (copy_from_user(&pbulk, (void *)arg, sizeof(pbulk))) {
			ret = -EFAULT;
			break;
		}
		ret = ezusb_bulk(ez->usbdev, pbulk.ep, pbulk.len, 
				 (pbulk.timeout * HZ + 500) / 1000, pbulk.data);
		break;

	case USB_PROC_OLD_CONTROL:
		if (copy_from_user(&opctrl, (void *)arg, sizeof(opctrl))) {
			ret = -EFAULT;
			break;
		}
		ret = ezusb_control(ez->usbdev, opctrl.requesttype, opctrl.request, 
				    opctrl.value, opctrl.index, opctrl.length, HZ, opctrl.data);
		break;

	case USB_PROC_OLD_BULK:
		if (copy_from_user(&opbulk, (void *)arg, sizeof(opbulk))) {
			ret = -EFAULT;
			break;
		}
		ret = ezusb_bulk(ez->usbdev, opbulk.ep, opbulk.len, 5*HZ, opbulk.data);
		break;

	case USB_PROC_RESETEP:
		if (get_user(ep, (unsigned int *)arg)) {
			ret = -EFAULT;
			break;
		}
		ret = ezusb_resetep(ez->usbdev, ep);
		break;
	
	case USB_PROC_SETINTERFACE:
		if (copy_from_user(&psetintf, (void *)arg, sizeof(psetintf))) {
			ret = -EFAULT;
			break;
		}
		ret = ezusb_setinterface(ez->usbdev, psetintf.interface, psetintf.altsetting);
		break;

	case USB_PROC_SETCONFIGURATION:
		if (get_user(cfg, (unsigned int *)arg)) {
			ret = -EFAULT;
			break;
		}
		ret = ezusb_setconfiguration(ez->usbdev, cfg);
		break;

	case EZUSB_CONTROL:
		if (copy_from_user(&ctrl, (void *)arg, sizeof(ctrl))) {
			ret = -EFAULT;
			break;
		}
		ret = ezusb_control(ez->usbdev, ctrl.requesttype, ctrl.request, 
				    ctrl.value, ctrl.index, ctrl.length, 
				    (ctrl.timeout * HZ + 500) / 1000, ctrl.data);
		break;

	case EZUSB_BULK:
		if (copy_from_user(&bulk, (void *)arg, sizeof(bulk))) {
			ret = -EFAULT;
			break;
		}
		ret = ezusb_bulk(ez->usbdev, bulk.ep, bulk.len, 
				 (bulk.timeout * HZ + 500) / 1000, bulk.data);
		break;

	case EZUSB_OLD_CONTROL:
		if (copy_from_user(&octrl, (void *)arg, sizeof(octrl))) {
			ret = -EFAULT;
			break;
		}
		if (octrl.dlen != octrl.length) {
			ret = -EINVAL;
			break;
		}
		ret = ezusb_control(ez->usbdev, octrl.requesttype, octrl.request, 
				    octrl.value, octrl.index, octrl.length, HZ, octrl.data);
		break;

	case EZUSB_OLD_BULK:
		if (copy_from_user(&obulk, (void *)arg, sizeof(obulk))) {
			ret = -EFAULT;
			break;
		}
		ret = ezusb_bulk(ez->usbdev, obulk.ep, obulk.len, 5*HZ, obulk.data);
		break;

	case EZUSB_RESETEP:
		if (get_user(ep, (unsigned int *)arg)) {
			ret = -EFAULT;
			break;
		}
		ret = ezusb_resetep(ez->usbdev, ep);
		break;
	
	case EZUSB_SETINTERFACE:
		if (copy_from_user(&setintf, (void *)arg, sizeof(setintf))) {
			ret = -EFAULT;
			break;
		}
		ret = ezusb_setinterface(ez->usbdev, setintf.interface, setintf.altsetting);
		break;

	case EZUSB_SETCONFIGURATION:
		if (get_user(cfg, (unsigned int *)arg)) {
			ret = -EFAULT;
			break;
		}
		ret = ezusb_setconfiguration(ez->usbdev, cfg);
		break;

	case EZUSB_ASYNCCOMPLETED:
		as = NULL;
		current->state = TASK_INTERRUPTIBLE;
		add_wait_queue(&ez->wait, &wait);
		for (;;) {
			if (!ez->usbdev)
				break;
			if ((as = async_getcompleted(ez)))
				break;
			if (signal_pending(current))
				break;
			up(&ez->mutex);
			schedule();
			down(&ez->mutex);
		}
		remove_wait_queue(&ez->wait, &wait);
		current->state = TASK_RUNNING;
		if (as) {
			ret = ezusb_asynccompl(as, (void *)arg);
			break;
		}
		if (signal_pending(current)) {
			ret = -EINTR;
			break;
		}
		ret = -EIO;
		break;

	case EZUSB_ASYNCCOMPLETEDNB:
		if ((as = async_getcompleted(ez))) {
			ret = ezusb_asynccompl(as, (void *)arg);
			break;
		}
		ret = -EAGAIN;
		break;

	case EZUSB_REQUESTBULK:
		if (copy_from_user(&abulk, (void *)arg, sizeof(abulk))) {
			ret = -EFAULT;
			break;
		}
		ret = ezusb_requestbulk(ez, &abulk);
		break;	

	case EZUSB_REQUESTISO:
		if (copy_from_user(&aiso, (void *)arg, sizeof(aiso))) {
			ret = -EFAULT;
			break;
		}
		ret = ezusb_requestiso(ez, &aiso, ((unsigned char *)arg)+sizeof(aiso));
		break;

	case EZUSB_TERMINATEASYNC:
		if (get_user(context, (void **)arg)) {
			ret = -EFAULT;
			break;
		}
		ret = ezusb_terminateasync(ez, context);
		break;

	case EZUSB_GETFRAMENUMBER:
		i = usb_get_current_frame_number(ez->usbdev);
		ret = put_user(i, (int *)arg);
		break;

	default:
		ret = -ENOIOCTLCMD;
		break;
	}
	up(&ez->mutex);
	return ret;
}

static struct file_operations ezusb_fops = {
	ezusb_llseek,
	ezusb_read,
	ezusb_write,
	NULL,  /* readdir */
	NULL,  /* poll */
	ezusb_ioctl,
	NULL,  /* mmap */
	ezusb_open,
	NULL,  /* flush */
	ezusb_release,
	NULL,  /* fsync */
	NULL,  /* fasync */
	NULL,  /* check_media_change */
	NULL,  /* revalidate */
	NULL   /* lock */
};

/* --------------------------------------------------------------------- */

static void * ezusb_probe(struct usb_device *usbdev, unsigned int ifnum)
{
	struct ezusb *ez = &ezusb[0];
        struct usb_interface_descriptor *interface;
        struct usb_endpoint_descriptor *endpoint;

#undef KERN_DEBUG
#define KERN_DEBUG ""
        printk(KERN_DEBUG "ezusb: probe: vendor id 0x%x, device id 0x%x\n",
               usbdev->descriptor.idVendor, usbdev->descriptor.idProduct);

	/* the 1234:5678 is just a self assigned test ID */
        if ((usbdev->descriptor.idVendor != 0x0547 || usbdev->descriptor.idProduct != 0x2131) &&
	    (usbdev->descriptor.idVendor != 0x1234 || usbdev->descriptor.idProduct != 0x5678))
                return NULL;

        /* We don't handle multiple configurations */
        if (usbdev->descriptor.bNumConfigurations != 1)
                return NULL;

#if 0
        /* We don't handle multiple interfaces */
        if (usbdev->actconfig.bNumInterfaces != 1)
                return NULL;
#endif

	down(&ez->mutex);
	if (ez->usbdev) {
		up(&ez->mutex);
		printk(KERN_INFO "ezusb: device already used\n");
		return NULL;
	}
	ez->usbdev = usbdev;
        interface = &usbdev->actconfig->interface[ifnum].altsetting[1];
	if (usb_set_interface(usbdev, ifnum, 1) < 0) {
		printk(KERN_ERR "ezusb: set_interface failed\n");
		goto err;
	}
	up(&ez->mutex);
	MOD_INC_USE_COUNT;
        return ez;

 err:
	up(&ez->mutex);
	ez->usbdev = NULL;
	return NULL;
}

static void ezusb_disconnect(struct usb_device *usbdev, void *ptr)
{
	struct ezusb *ez = (struct ezusb *) ptr;

	down(&ez->mutex);
	destroy_all_async(ez);
	ez->usbdev = NULL;
	up(&ez->mutex);
	wake_up(&ez->wait);
	MOD_DEC_USE_COUNT;
}

static struct usb_driver ezusb_driver = {
        "ezusb",
        ezusb_probe,
        ezusb_disconnect,
        { NULL, NULL },
	&ezusb_fops,
	32
};

/* --------------------------------------------------------------------- */

int ezusb_init(void)
{
	unsigned u;

	/* initialize struct */
	for (u = 0; u < NREZUSB; u++) {
		init_MUTEX(&ezusb[u].mutex);
		ezusb[u].usbdev = NULL;
		INIT_LIST_HEAD(&ezusb[u].async_pending);
		INIT_LIST_HEAD(&ezusb[u].async_completed);
		init_waitqueue_head(&ezusb[u].wait);
		spin_lock_init(&ezusb[u].lock);
	}
	if (usb_register(&ezusb_driver) < 0)
		return -1;

        printk(KERN_INFO "ezusb: Anchorchip firmware download driver registered\n");
	return 0;
}

void ezusb_cleanup(void)
{
	usb_deregister(&ezusb_driver);
}

/* --------------------------------------------------------------------- */

#ifdef MODULE

int init_module(void)
{
	return ezusb_init();
}

void cleanup_module(void)
{
	ezusb_cleanup();
}

#endif

/* --------------------------------------------------------------------- */
