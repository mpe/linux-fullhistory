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
 *
 */

/*****************************************************************************/

#include <linux/config.h>
#include <linux/module.h>
#include <linux/socket.h>
#include <asm/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/list.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>

#include "usb.h"
#include "ezusb.h"

/* --------------------------------------------------------------------- */

#define NREZUSB 1

static struct ezusb {
	struct semaphore mutex;
	struct usb_device *usbdev;
	struct list_head iso;
} ezusb[NREZUSB];

struct isodesc {
	struct list_head isolist;
	spinlock_t lock;
	struct usb_device *usbdev;
	unsigned int ep;
	unsigned int pipe;
	unsigned int pktsz;
	unsigned int framesperint;
	unsigned int rd, wr, buflen;
	unsigned int flags;
	unsigned int schedcnt, unschedcnt;

	void *hcbuf[2];
	void *hcisodesc[2];
	unsigned char *buf;
};	

#define ISOFLG_ACTIVE  (1<<0)

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
        devrequest dr;
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
		dr.requesttype = 0xc0;
		dr.request = 0xa0;
		dr.value = pos;
		dr.index = 0;
		dr.length = len;
		i = ez->usbdev->bus->op->control_msg(ez->usbdev, usb_rcvctrlpipe(ez->usbdev, 0), &dr, b, len);
		if (i) {
			up(&ez->mutex);
			printk(KERN_WARNING "ezusb: upload failed pos %u len %u ret %d\n", dr.value, dr.length, i);
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
        devrequest dr;
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
		dr.requesttype = 0x40;
		dr.request = 0xa0;
		dr.value = pos;
		dr.index = 0;
		dr.length = len;
		i = ez->usbdev->bus->op->control_msg(ez->usbdev, usb_sndctrlpipe(ez->usbdev, 0), &dr, b, len);
		if (i) {
			up(&ez->mutex);
			printk(KERN_WARNING "ezusb: download failed pos %u len %u ret %d\n", dr.value, dr.length, i);
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
	return 0;
}

static int ezusb_release(struct inode *inode, struct file *file)
{
	struct ezusb *ez = (struct ezusb *)file->private_data;
	return 0;
}

static void iso_schedrcv(struct isodesc *isodesc)
{
	unsigned diff;

	if (!(isodesc->flags & ISOFLG_ACTIVE))
	    return;
	diff = (isodesc->buflen - 1 + isodesc->rd - isodesc->wr) % isodesc->buflen;
	if (diff < isodesc->framesperint * isodesc->pktsz)
		return;
	for (;;) {
		diff = (isodesc->schedcnt - isodesc->unschedcnt) & 3;
		if (diff >= 2)
			return;
		usb_schedule_isochronous(isodesc->usbdev, isodesc->hcisodesc[isodesc->schedcnt & 1],
					 diff ? isodesc->hcisodesc[(isodesc->schedcnt - 1) & 1] : NULL);
		isodesc->schedcnt++;
	}
}

static void iso_schedsnd(struct isodesc *isodesc)
{
	unsigned diff, bcnt, x;
	unsigned char *p1, *p2;
	
	if (!(isodesc->flags & ISOFLG_ACTIVE))
	    return;
	for (;;) {
		diff = (isodesc->schedcnt - isodesc->unschedcnt) & 3;
		if (diff >= 2)
			return;
		bcnt = (isodesc->buflen - isodesc->rd + isodesc->wr) % isodesc->buflen;
		if (bcnt < isodesc->framesperint * isodesc->pktsz)
			return;
		p2 = isodesc->hcbuf[isodesc->schedcnt & 1];
		for (bcnt = 0; bcnt < isodesc->framesperint; bcnt++) {
			p1 = isodesc->buf + isodesc->rd;
			if (isodesc->rd + isodesc->pktsz > isodesc->buflen) {
				x = isodesc->buflen - isodesc->rd;
				memcpy(p2, p1, x);
				memcpy(p2+x, isodesc->buf, isodesc->pktsz - x);
			} else
				memcpy(p2, p1, isodesc->pktsz);
			isodesc->rd = (isodesc->rd + isodesc->pktsz) % isodesc->buflen;
			p2 += isodesc->pktsz;
			if (((unsigned long)p2 ^ ((unsigned long)p2 + isodesc->pktsz - 1)) & (~(PAGE_SIZE - 1)))
				p2 = (void *)(((unsigned long)p2 + PAGE_SIZE - 1) & (~(PAGE_SIZE - 1)));
		}
		usb_schedule_isochronous(isodesc->usbdev, isodesc->hcisodesc[isodesc->schedcnt & 1],
					 diff ? isodesc->hcisodesc[(isodesc->schedcnt - 1) & 1] : NULL);
		isodesc->schedcnt++;
	}
}

static int ezusb_isorcv_irq(int status, void *__buffer, int __len, void *dev_id)
{
	struct isodesc *isodesc = (struct isodesc *)dev_id;
	unsigned int len, len2;
	unsigned char *p1;

	spin_lock(&isodesc->lock);
	usb_unschedule_isochronous(isodesc->usbdev, isodesc->hcisodesc[isodesc->unschedcnt & 1]);
	len = usb_compress_isochronous(isodesc->usbdev, isodesc->hcisodesc[isodesc->unschedcnt & 1]);
	printk(KERN_DEBUG "ezusb_isorcv_irq: %u bytes recvd\n", len);
	p1 = isodesc->hcbuf[isodesc->unschedcnt & 1];
	while (len > 0) {
		len2 = (isodesc->buflen - 1 + isodesc->rd - isodesc->wr) % isodesc->buflen;
		if (!len2)
			break;
		if (isodesc->wr + len2 > isodesc->buflen)
			len2 = isodesc->buflen - isodesc->wr;
		if (len2 > len)
			len2 = len;
		memcpy(isodesc->buf + isodesc->wr, p1, len2);
		isodesc->wr = (isodesc->wr + len2) % isodesc->buflen;
		p1 += len2;
		len -= len2;
	}
	isodesc->unschedcnt++;
	iso_schedrcv(isodesc);
	spin_unlock(&isodesc->lock);
	return 1;
}

static int ezusb_isosnd_irq(int status, void *__buffer, int len, void *dev_id)
{
	struct isodesc *isodesc = (struct isodesc *)dev_id;

	spin_lock(&isodesc->lock);
	usb_unschedule_isochronous(isodesc->usbdev, isodesc->hcisodesc[isodesc->unschedcnt & 1]);
	isodesc->unschedcnt++;
	iso_schedsnd(isodesc);
	spin_unlock(&isodesc->lock);
	return 1;
}

static struct isodesc *findiso(struct ezusb *ez, unsigned int ep)
{
	struct list_head *head = &ez->iso;
	struct list_head *tmp = head->next;
	struct isodesc *id;

	while (tmp != head) {
		id = list_entry(tmp, struct isodesc, isolist);
		if (id->ep == ep)
			return id;
		tmp = tmp->next;
	}
	return NULL;
}

static int ezusb_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct ezusb *ez = (struct ezusb *)file->private_data;
	struct ezusb_ctrltransfer ctrl;
	struct ezusb_bulktransfer bulk;
	struct ezusb_setinterface setintf;
	unsigned int len1, ep, pipe, cnt;
	unsigned long len2;
	unsigned char tbuf[1024];
	int i;
	struct ezusb_isotransfer isot;
	struct ezusb_isodata isod;
	struct isodesc *isodesc;
	usb_device_irq isocompl;
	unsigned long flags;
	unsigned char *p1, *p2;

	switch (cmd) {
	case EZUSB_CONTROL:
		copy_from_user_ret(&ctrl, (void *)arg, sizeof(ctrl), -EFAULT);
		if (ctrl.dlen > sizeof(tbuf) || ctrl.dlen > 1024)
			return -EINVAL;
		if (ctrl.requesttype & 0x80) {
			if (ctrl.dlen && !access_ok(VERIFY_WRITE, ctrl.data, ctrl.dlen))
				return -EINVAL;
			down(&ez->mutex);
			if (!ez->usbdev) {
				up(&ez->mutex);
				return -EIO;
			}
			i = ez->usbdev->bus->op->control_msg(ez->usbdev, usb_rcvctrlpipe(ez->usbdev, 0),
							     (devrequest *)&ctrl, tbuf, ctrl.dlen);
			up(&ez->mutex);
			if (!i && ctrl.dlen) {
				copy_to_user_ret(ctrl.data, tbuf, ctrl.dlen, -EFAULT);
			}
		} else {
			if (ctrl.dlen) {
				copy_from_user_ret(tbuf, ctrl.data, ctrl.dlen, -EFAULT);
			}
			down(&ez->mutex);
			if (!ez->usbdev) {
				up(&ez->mutex);
				return -EIO;
			}
			i = ez->usbdev->bus->op->control_msg(ez->usbdev, usb_sndctrlpipe(ez->usbdev, 0),
							     (devrequest *)&ctrl, tbuf, ctrl.dlen);
			up(&ez->mutex);
		}
		if (i) {
			printk(KERN_WARNING "ezusb: EZUSB_CONTROL failed rqt %u rq %u len %u ret %d\n", 
			       ctrl.requesttype, ctrl.request, ctrl.length, i);
			return -ENXIO;
		}
		return 0;

	case EZUSB_BULK:
		copy_from_user_ret(&bulk, (void *)arg, sizeof(bulk), -EFAULT);
		if (bulk.ep & 0x80)
			pipe = usb_rcvbulkpipe(ez->usbdev, bulk.ep & 0x7f);
		else
			pipe = usb_sndbulkpipe(ez->usbdev, bulk.ep & 0x7f);
		if (!usb_maxpacket(ez->usbdev, pipe, !(bulk.ep & 0x80)))
			return -EINVAL;
		len1 = bulk.len;
		if (len1 > sizeof(tbuf))
			len1 = sizeof(tbuf);
		if (bulk.ep & 0x80) {
			if (len1 && !access_ok(VERIFY_WRITE, bulk.data, len1))
				return -EINVAL;
			down(&ez->mutex);
			if (!ez->usbdev) {
				up(&ez->mutex);
				return -EIO;
			}
			i = ez->usbdev->bus->op->bulk_msg(ez->usbdev, pipe, tbuf, len1, &len2);
			up(&ez->mutex);
			if (!i && len2) {
				copy_to_user_ret(bulk.data, tbuf, len2, -EFAULT);
			}
		} else {
			if (len1) {
				copy_from_user_ret(tbuf, bulk.data, len1, -EFAULT);
			}
			down(&ez->mutex);
			if (!ez->usbdev) {
				up(&ez->mutex);
				return -EIO;
			}
			i = ez->usbdev->bus->op->bulk_msg(ez->usbdev, pipe, tbuf, len1, &len2);
			up(&ez->mutex);
		}
		if (i) {
			printk(KERN_WARNING "ezusb: EZUSB_BULK failed ep 0x%x len %u ret %d\n", 
			       bulk.ep, bulk.len, i);
			return -ENXIO;
		}
		return len2;

	case EZUSB_RESETEP:
		get_user_ret(ep, (unsigned int *)arg, -EFAULT);
		if ((ep & ~0x80) >= 16)
			return -EINVAL;
		usb_settoggle(ez->usbdev, ep & 0xf, !(ep & 0x80), 0);
		return 0;

	case EZUSB_SETINTERFACE:
		copy_from_user_ret(&setintf, (void *)arg, sizeof(setintf), -EFAULT);
		if (usb_set_interface(ez->usbdev, setintf.interface, setintf.altsetting))
			return -EINVAL;
		return 0;

	case EZUSB_STARTISO:
		copy_from_user_ret(&isot, (void *)arg, sizeof(isot), -EFAULT);
		len1 = isot.framesperint * isot.pktsz;
		if (len1 > PAGE_SIZE) {
			len1 = PAGE_SIZE / isot.pktsz;
			len1 = PAGE_SIZE * ((isot.framesperint + len1 - 1) / len1);
		}
		len2 = (isot.pktsz * 1000 + PAGE_SIZE - 1) & (PAGE_SIZE-1);
		if (len2 > 32*PAGE_SIZE)
			len2 = PAGE_SIZE;
		if ((isot.ep & ~0x80) >= 16 || isot.pktsz < 1 || isot.pktsz > 1023 ||
		    isot.framesperint < 1 || isot.framesperint > 1000 ||
		    len1 > 4*PAGE_SIZE)
			return -EINVAL;
		down(&ez->mutex);
		if (!ez->usbdev) {
			up(&ez->mutex);
			return -EIO;
		}
		if (findiso(ez, isod.ep)) {
			up(&ez->mutex);
			return -EBUSY;
		}
		if (isot.ep & 0x80) {
			pipe = usb_rcvisocpipe(ez->usbdev, isot.ep & 15);
			isocompl = ezusb_isorcv_irq;
		} else {
			pipe = usb_sndisocpipe(ez->usbdev, isot.ep & 15);
			isocompl = ezusb_isosnd_irq;
		}
		if (!(isodesc = kmalloc(sizeof(struct isodesc), GFP_KERNEL))) {
			up(&ez->mutex);
			return -ENOMEM;
		}
		memset(isodesc, 0, sizeof(struct isodesc));
		INIT_LIST_HEAD(&isodesc->isolist);
		spin_lock_init(&isodesc->lock);
		isodesc->usbdev = ez->usbdev;
		isodesc->ep = isot.ep;
		isodesc->pktsz = isot.pktsz;
		isodesc->framesperint = isot.framesperint;
		isodesc->buflen = len2;
		if (!(isodesc->hcbuf[0] = kmalloc(len1, GFP_KERNEL)) ||
		    !(isodesc->hcbuf[1] = kmalloc(len1, GFP_KERNEL)))
			goto startisomemerr;
		if (!(isodesc->hcisodesc[0] = usb_allocate_isochronous(ez->usbdev, pipe, isodesc->hcbuf[0],
								       len1, isodesc->pktsz, isocompl, isodesc)) ||
		    !(isodesc->hcisodesc[1] = usb_allocate_isochronous(ez->usbdev, pipe, isodesc->hcbuf[1],
								       len1, isodesc->pktsz, isocompl, isodesc)))
			goto startisomemerr;
		if (!(isodesc->buf = vmalloc(isodesc->buflen)))
			goto startisomemerr;
		up(&ez->mutex);
		return 0;

	startisomemerr:
		if (isodesc->hcisodesc[0])
			usb_delete_isochronous(ez->usbdev, isodesc->hcisodesc[0]);
		if (isodesc->hcisodesc[1])
			usb_delete_isochronous(ez->usbdev, isodesc->hcisodesc[1]);
		if (isodesc->hcbuf[0])
			kfree(isodesc->hcbuf[0]);
		if (isodesc->hcbuf[1])
			kfree(isodesc->hcbuf[1]);
		if (isodesc->buf)
			vfree(isodesc->buf);
		up(&ez->mutex);
		return -ENOMEM;

	case EZUSB_STOPISO:
		get_user_ret(ep, (unsigned int *)arg, -EFAULT);
		if ((ep & ~0x80) >= 16)
			return -EINVAL;
		down(&ez->mutex);
		if (!ez->usbdev) {
			up(&ez->mutex);
			return -EIO;
		}
		if (!(isodesc = findiso(ez, ep))) {
			up(&ez->mutex);
			return -EINVAL;
		}
		list_del(&isodesc->isolist);
		usb_delete_isochronous(ez->usbdev, isodesc->hcisodesc[0]);
		usb_delete_isochronous(ez->usbdev, isodesc->hcisodesc[1]);
		kfree(isodesc->hcbuf[0]);
		kfree(isodesc->hcbuf[1]);
		vfree(isodesc->buf);
		up(&ez->mutex);
		return 0;

	case EZUSB_ISODATA:
		copy_from_user_ret(&isod, (void *)arg, sizeof(isod), -EFAULT);
		if ((isod.ep & ~0x80) >= 16)
			return -EINVAL;
		if (isod.size)
			if (!access_ok((isod.ep & 0x80) ? VERIFY_WRITE : VERIFY_READ, isod.data, isod.size))
				return -EFAULT;
		down(&ez->mutex);
		if (!ez->usbdev) {
			up(&ez->mutex);
			return -EIO;
		}
		if (!(isodesc = findiso(ez, ep))) {
			up(&ez->mutex);
			return -EINVAL;
		}
		if (isod.ep & 0x80) {
			cnt = 0;
			p1 = isod.data;
			while (cnt < isod.size) {
				spin_lock_irqsave(&isodesc->lock, flags);
				p2 = isodesc->buf + isodesc->rd;
				len2 = (isodesc->rd >= isodesc->wr) ? isodesc->buflen : isodesc->wr;
				len2 -= isodesc->rd;
				spin_unlock_irqrestore(&isodesc->lock, flags);
				if (len2 <= 0)
					break;
				if (len2 >= isod.size - cnt)
					len2 = isod.size - cnt;
				if (__copy_to_user(p1, p2, len2)) {
					up(&ez->mutex);
					return -EFAULT;
				}
				p1 += len2;
				cnt += len2;
				spin_lock_irqsave(&isodesc->lock, flags);
				isodesc->rd = (isodesc->rd + len2) % isodesc->buflen;
				spin_unlock_irqrestore(&isodesc->lock, flags);
			}
			isod.size = cnt;
			iso_schedrcv(isodesc);
		} else {
			cnt = 0;
			p1 = isod.data;
			while (cnt < isod.size) {
				spin_lock_irqsave(&isodesc->lock, flags);
				p2 = isodesc->buf + isodesc->wr;
				len2 = (isodesc->buflen - 1 + isodesc->rd - isodesc->wr) % isodesc->buflen;
				if (isodesc->wr + len2 > isodesc->buflen)
					len2 = isodesc->buflen - isodesc->wr;
				spin_unlock_irqrestore(&isodesc->lock, flags);
				if (len2 <= 0)
					break;
				if (len2 >= isod.size - cnt)
					len2 = isod.size - cnt;
				if (__copy_from_user(p2, p1, len2)) {
					up(&ez->mutex);
					return -EFAULT;
				}
				p1 += len2;
				cnt += len2;
				spin_lock_irqsave(&isodesc->lock, flags);
				isodesc->wr = (isodesc->wr + len2) % isodesc->buflen;
				spin_unlock_irqrestore(&isodesc->lock, flags);
			}
			isod.size = cnt;
			iso_schedsnd(isodesc);
		}
		spin_lock_irqsave(&isodesc->lock, flags);
		isod.bufqueued = (isodesc->buflen + isodesc->wr - isodesc->rd) % isodesc->buflen;
		isod.buffree = (isodesc->buflen - 1 + isodesc->rd - isodesc->wr) % isodesc->buflen;
		spin_unlock_irqrestore(&isodesc->lock, flags);
		up(&ez->mutex);
		copy_to_user_ret((void *)arg, &isod, sizeof(isod), -EFAULT);
		return 0;

	case EZUSB_PAUSEISO:
		get_user_ret(ep, (unsigned int *)arg, -EFAULT);
		if ((ep & ~0x80) >= 16)
			return -EINVAL;
		if (!(isodesc = findiso(ez, ep)))
			return -EINVAL;
		spin_lock_irqsave(&isodesc->lock, flags);
		isodesc->flags &= ~ISOFLG_ACTIVE;
		spin_unlock_irqrestore(&isodesc->lock, flags);
		return 0;

	case EZUSB_RESUMEISO:
		get_user_ret(ep, (unsigned int *)arg, -EFAULT);
		if ((ep & ~0x80) >= 16)
			return -EINVAL;
		down(&ez->mutex);
		if (!ez->usbdev) {
			up(&ez->mutex);
			return -EIO;
		}
		if (!(isodesc = findiso(ez, ep))) {
			up(&ez->mutex);
			return -EINVAL;
		}
		spin_lock_irqsave(&isodesc->lock, flags);
		isodesc->flags |= ISOFLG_ACTIVE;
		if (isot.ep & 0x80)
			iso_schedrcv(isodesc);
		else
			iso_schedsnd(isodesc);
		spin_unlock_irqrestore(&isodesc->lock, flags);
				up(&ez->mutex);
		return 0;
	}
	return -ENOIOCTLCMD;
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

static struct miscdevice ezusb_misc = {
	192, "ezusb", &ezusb_fops
};

/* --------------------------------------------------------------------- */

static int ezusb_probe(struct usb_device *usbdev)
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
                return -1;

        /* We don't handle multiple configurations */
        if (usbdev->descriptor.bNumConfigurations != 1)
                return -1;

        /* We don't handle multiple interfaces */
        if (usbdev->config[0].bNumInterfaces != 1)
                return -1;

	down(&ez->mutex);
	if (ez->usbdev) {
		up(&ez->mutex);
		printk(KERN_INFO "ezusb: device already used\n");
		return -1;
	}
	ez->usbdev = usbdev;
	usbdev->private = ez;
        if (usb_set_configuration(usbdev, usbdev->config[0].bConfigurationValue)) {
		printk(KERN_ERR "ezusb: set_configuration failed\n");
		goto err;
	}
        interface = &usbdev->config[0].interface[0].altsetting[1];
	if (usb_set_interface(usbdev, 0, 1)) {
		printk(KERN_ERR "ezusb: set_interface failed\n");
		goto err;
	}
	up(&ez->mutex);
	MOD_INC_USE_COUNT;
        return 0;

 err:
	up(&ez->mutex);
	ez->usbdev = NULL;
	usbdev->private = NULL;
	return -1;
}

static void ezusb_disconnect(struct usb_device *usbdev)
{
	struct ezusb *ez = (struct ezusb *)usbdev->private;
	struct isodesc *isodesc;

	down(&ez->mutex);
	while (!list_empty(&ez->iso)) {
		isodesc = list_entry(ez->iso.next, struct isodesc, isolist);
		list_del(ez->iso.next);
		usb_delete_isochronous(ez->usbdev, isodesc->hcisodesc[0]);
		usb_delete_isochronous(ez->usbdev, isodesc->hcisodesc[1]);
		kfree(isodesc->hcbuf[0]);
		kfree(isodesc->hcbuf[1]);
		vfree(isodesc->buf);
	}
	ez->usbdev = NULL;
	up(&ez->mutex);
        usbdev->private = NULL;
	MOD_DEC_USE_COUNT;
}

static struct usb_driver ezusb_driver = {
        "ezusb",
        ezusb_probe,
        ezusb_disconnect,
        { NULL, NULL }
};

/* --------------------------------------------------------------------- */

int ezusb_init(void)
{
	unsigned u;

	/* initialize struct */
	for (u = 0; u < NREZUSB; u++) {
		init_MUTEX(&ezusb[u].mutex);
		ezusb[u].usbdev = NULL;
		INIT_LIST_HEAD(&ezusb[u].iso);
	}
	/* register misc device */
	if (misc_register(&ezusb_misc)) {
		printk(KERN_WARNING "ezusb: cannot register minor %d\n", ezusb_misc.minor);
		return -1;
	}
	usb_register(&ezusb_driver);
        printk(KERN_INFO "ezusb: Anchorchip firmware download driver registered\n");
	return 0;
}

void ezusb_cleanup(void)
{
	usb_deregister(&ezusb_driver);
	misc_deregister(&ezusb_misc);
}

/* --------------------------------------------------------------------- */

#ifdef MODULE

int minor = 192;

int init_module(void)
{
	ezusb_misc.minor = minor;
	return ezusb_init();
}

void cleanup_module(void)
{
	ezusb_cleanup();
}

#endif

/* --------------------------------------------------------------------- */
