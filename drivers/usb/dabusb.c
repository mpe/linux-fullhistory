/*****************************************************************************/

/*
 *      dabusb.c  --  dab usb driver.
 *
 *      Copyright (C) 1999  Deti Fliegl (deti@fliegl.de)
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 *
 *  $Id: dabusb.c,v 1.26 1999/12/13 08:40:23 fliegl Exp $
 *
 */

/*****************************************************************************/

#include <linux/config.h>
#include <linux/module.h>
#include <linux/socket.h>
#include <linux/miscdevice.h>
#include <linux/list.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <asm/atomic.h>
//#include <linux/spinlock.h>
#include <linux/delay.h>

#include "usb.h"

#include "dabusb.h"
#include "bitstream.c"
#include "firmware.c"
/* --------------------------------------------------------------------- */

#define NRDABUSB 4

/*-------------------------------------------------------------------*/
static dabusb_t dabusb[NRDABUSB];
static int buffers = 256;
/*-------------------------------------------------------------------*/
static int dabusb_add_buf_tail (pdabusb_t s, struct list_head *dst, struct list_head *src)
{
	unsigned long flags;
	struct list_head *tmp;
	int ret = 0;

	spin_lock_irqsave (&s->lock, flags);

//      printk(KERN_DEBUG MODSTR"dabusb_add_buf_tail %p %p\n",src->next, dst->next);
	if (list_empty (src)) {
		// no elements in source buffer
		//              printk(KERN_DEBUG MODSTR"source list empty\n");
		ret = -1;
		goto err;
	}
	tmp = src->next;
	list_del (tmp);
	list_add_tail (tmp, dst);

  err:	spin_unlock_irqrestore (&s->lock, flags);
	return ret;
}
/*-------------------------------------------------------------------*/
static void dump_urb (purb_t purb)
{
	printk ("urb                   :%p\n", purb);
	printk ("next                  :%p\n", purb->next);
	printk ("dev                   :%p\n", purb->dev);
	printk ("pipe                  :%08X\n", purb->pipe);
	printk ("status                :%d\n", purb->status);
	printk ("transfer_flags        :%08X\n", purb->transfer_flags);
	printk ("transfer_buffer       :%p\n", purb->transfer_buffer);
	printk ("transfer_buffer_length:%d\n", purb->transfer_buffer_length);
	printk ("actual_length         :%d\n", purb->actual_length);
	printk ("setup_packet          :%p\n", purb->setup_packet);
	printk ("start_frame           :%d\n", purb->start_frame);
	printk ("number_of_packets     :%d\n", purb->number_of_packets);
	printk ("interval              :%d\n", purb->interval);
	printk ("error_count           :%d\n", purb->error_count);
	printk ("context               :%p\n", purb->context);
	printk ("complete              :%p\n", purb->complete);
}

/*-------------------------------------------------------------------*/
static int dabusb_cancel_queue (pdabusb_t s, struct list_head *q)
{
	unsigned long flags;
	struct list_head *p;
	pbuff_t b;
	printk (KERN_DEBUG MODSTR "dabusb_cancel_queue\n");
	spin_lock_irqsave (&s->lock, flags);
	for (p = q->next; p != q; p = p->next) {
//              printk("p:%p\n",p);
		b = list_entry (p, buff_t, buff_list);
//              printk("buff:%p\n",b);
		//              dump_urb(b->purb);
		usb_unlink_urb (b->purb);
	}
	spin_unlock_irqrestore (&s->lock, flags);
	return 0;
}
/*-------------------------------------------------------------------*/
static int dabusb_free_queue (struct list_head *q)
{
	struct list_head *tmp;
	struct list_head *p;
	pbuff_t b;
	printk (KERN_DEBUG MODSTR "dabusb_free_queue\n");
	for (p = q->next; p != q;) {
//              printk("%p\n",p);
		b = list_entry (p, buff_t, buff_list);
//              dump_urb(b->purb);
		if (b->purb->transfer_buffer)
			kfree (b->purb->transfer_buffer);
		if (b->purb)
			kfree (b->purb);
		tmp = p->next;
		list_del (p);
		kfree (b);
		p = tmp;
	}
	return 0;
}
/*-------------------------------------------------------------------*/
static int dabusb_free_buffers (pdabusb_t s)
{
	printk (KERN_DEBUG MODSTR "dabusb_free_buffers\n");
	dabusb_free_queue (&s->free_buff_list);
	dabusb_free_queue (&s->rec_buff_list);
	s->got_mem = 0;
	return 0;
}
/*-------------------------------------------------------------------*/
static void dabusb_iso_complete (purb_t purb)
{
	pbuff_t b = purb->context;
	pdabusb_t s = b->s;
	int i;
	int len;
	int dst = 0;
	void *buf = purb->transfer_buffer;
//      printk(KERN_DEBUG MODSTR"dabusb_iso_complete\n");
	if (purb->status != USB_ST_URB_KILLED) {
		unsigned int pipe = usb_rcvisocpipe (purb->dev, _DABUSB_ISOPIPE);
		int pipesize = usb_maxpacket (purb->dev, pipe, usb_pipeout (pipe));
		for (i = 0; i < purb->number_of_packets; i++)
			if (purb->iso_frame_desc[i].status == USB_ST_NOERROR) {
				len = purb->iso_frame_desc[i].actual_length;
				if (len <= pipesize) {
					memcpy (buf + dst, buf + purb->iso_frame_desc[i].offset, len);
					dst += len;
				}
				else
					printk (KERN_ERR MODSTR "dabusb_iso_complete: invalid len %d\n", len);
			}
		if (dst != purb->actual_length)
			printk (KERN_ERR MODSTR "dst!=purb->actual_length:%d!=%d\n", dst, purb->actual_length);
	}

	if (atomic_dec_and_test (&s->pending_io) && !s->remove_pending && s->state != _stopped) {
		s->overruns++;
		printk (KERN_ERR MODSTR "overrun (%d)\n", s->overruns);
	}
	wake_up (&s->wait);
}
/*-------------------------------------------------------------------*/
static int dabusb_alloc_buffers (pdabusb_t s)
{
	int buffers = 0;
	pbuff_t b;
	unsigned int pipe = usb_rcvisocpipe (s->usbdev, _DABUSB_ISOPIPE);
	int pipesize = usb_maxpacket (s->usbdev, pipe, usb_pipeout (pipe));
	int packets = _ISOPIPESIZE / pipesize;
	int transfer_buffer_length = packets * pipesize;
	int i;
	int len = sizeof (urb_t) + packets * sizeof (iso_packet_descriptor_t);
	printk (KERN_DEBUG MODSTR "dabusb_alloc_buffers len:%d pipesize:%d packets:%d transfer_buffer_len:%d\n",
		len, pipesize, packets, transfer_buffer_length);
	while (buffers < (s->total_buffer_size << 10)) {
		b = (pbuff_t) kmalloc (sizeof (buff_t), GFP_KERNEL);
		if (!b) {
			printk (KERN_ERR MODSTR "kmalloc(sizeof(buff_t))==NULL\n");
			goto err;
		}
		memset (b, sizeof (buff_t), 0);
		b->s = s;
		b->purb = (purb_t) kmalloc (len, GFP_KERNEL);
		if (!b->purb) {
			printk (KERN_ERR MODSTR "kmalloc(sizeof(urb_t)+packets*sizeof(iso_packet_descriptor_t))==NULL\n");
			kfree (b);
			goto err;
		}
		memset (b->purb, 0, len);
		b->purb->transfer_buffer = kmalloc (transfer_buffer_length, GFP_KERNEL);
		if (!b->purb->transfer_buffer) {
			kfree (b->purb);
			kfree (b);
			printk (KERN_ERR MODSTR "kmalloc(%d)==NULL\n", transfer_buffer_length);
			goto err;
		}

		b->purb->transfer_buffer_length = transfer_buffer_length;
		b->purb->number_of_packets = packets;
		b->purb->complete = dabusb_iso_complete;
		b->purb->context = b;
		b->purb->dev = s->usbdev;
		b->purb->pipe = pipe;
		b->purb->transfer_flags = USB_ISO_ASAP;

		for (i = 0; i < packets; i++) {
			b->purb->iso_frame_desc[i].offset = i * pipesize;
			b->purb->iso_frame_desc[i].length = pipesize;
		}
//              dump_urb(b->purb);
		buffers += transfer_buffer_length;
		list_add_tail (&b->buff_list, &s->free_buff_list);
	}
	s->got_mem = buffers;

	return 0;
err:
	dabusb_free_buffers (s);
	return -ENOMEM;
}
/*-------------------------------------------------------------------*/
static int dabusb_reset_pipe (struct usb_device *usbdev, unsigned int ep)
{
	printk (KERN_DEBUG MODSTR "dabusb_reset_pipe\n");
	if ((ep & ~0x80) >= 16)
		return -EINVAL;
	usb_settoggle (usbdev, ep & 0xf, !(ep & 0x80), 0);
	return 0;
}
/* --------------------------------------------------------------------- */
static int dabusb_submit_urb (pdabusb_t s, purb_t purb)
{
	int ret;
	bulk_completion_context_t context;
	init_waitqueue_head (&context.wait);
	purb->context = &context;
//      dump_urb(purb); 
	ret = usb_submit_urb (purb);
	if (ret < 0) {
		printk (KERN_DEBUG MODSTR "dabusb_bulk: usb_submit_urb returned %d\n", ret);
		return -EINVAL;
	}
	interruptible_sleep_on_timeout (&context.wait, HZ);
	if (purb->status == USB_ST_URB_PENDING) {
		printk (KERN_ERR MODSTR "dabusb_usb_submit_urb: %p timed out\n", purb);
		usb_unlink_urb (purb);
		return -ETIMEDOUT;
	}
	return purb->status;
}
/* --------------------------------------------------------------------- */
static void dabusb_bulk_complete (purb_t purb)
{
	pbulk_completion_context_t context = purb->context;
//      printk(KERN_DEBUG MODSTR"dabusb_bulk_complete\n");
	//      dump_urb(purb);
	wake_up (&context->wait);
}

/* --------------------------------------------------------------------- */
static int dabusb_bulk (pdabusb_t s, pbulk_transfer_t pb)
{
	int ret;
	urb_t urb;
	unsigned int pipe;

//      printk(KERN_DEBUG MODSTR"dabusb_bulk\n");

	if (!pb->pipe)
		pipe = usb_rcvbulkpipe (s->usbdev, 2);
	else
		pipe = usb_sndbulkpipe (s->usbdev, 2);

	memset (&urb, 0, sizeof (urb_t));
	FILL_BULK_URB ((&urb), s->usbdev, pipe, pb->data, pb->size, dabusb_bulk_complete, NULL);

	ret = dabusb_submit_urb (s, &urb);
	pb->size = urb.actual_length;
	return ret;
}
/* --------------------------------------------------------------------- */
static int dabusb_writemem (pdabusb_t s, int pos, unsigned char *data, int len)
{
	int ret;
	urb_t urb;
	unsigned int pipe;
	unsigned char *setup = kmalloc (8, GFP_KERNEL);
	unsigned char *transfer_buffer;

	if (!setup) {
		printk (KERN_ERR MODSTR "dabusb_writemem: kmalloc(8) failed.\n");
		return -ENOMEM;
	}
	transfer_buffer = kmalloc (len, GFP_KERNEL);
	if (!transfer_buffer) {
		printk (KERN_ERR MODSTR "dabusb_writemem: kmalloc(%d) failed.\n", len);
		kfree (setup);
		return -ENOMEM;
	}
	setup[0] = 0x40;
	setup[1] = 0xa0;
	setup[2] = pos & 0xff;
	setup[3] = pos >> 8;
	setup[4] = 0;
	setup[5] = 0;
	setup[6] = len & 0xff;
	setup[7] = len >> 8;
//      printk(KERN_DEBUG MODSTR"dabusb_control\n");

	memcpy (transfer_buffer, data, len);

	pipe = usb_sndctrlpipe (s->usbdev, 0);

	memset (&urb, 0, sizeof (urb_t));
	FILL_CONTROL_URB ((&urb), s->usbdev, pipe, setup, transfer_buffer, len, dabusb_bulk_complete, NULL);

	ret = dabusb_submit_urb (s, &urb);
	kfree (setup);
	kfree (transfer_buffer);
	if (ret < 0)
		return ret;
	return urb.status;
}
/* --------------------------------------------------------------------- */
static int dabusb_8051_reset (pdabusb_t s, unsigned char reset_bit)
{
	//printk("dabusb_8051_reset: %d\n",reset_bit);
	return dabusb_writemem (s, CPUCS_REG, &reset_bit, 1);
}
/* --------------------------------------------------------------------- */
static int dabusb_loadmem (pdabusb_t s, const char *fname)
{
	int ret;
	PINTEL_HEX_RECORD ptr = firmware;
	printk (KERN_DEBUG MODSTR "Enter dabusb_loadmem (internal)\n");

	ret = dabusb_8051_reset (s, 1);
	while (ptr->Type == 0) {
		//printk(KERN_ERR MODSTR"dabusb_writemem: %04X %p %d)\n", ptr->Address, ptr->Data, ptr->Length);
		ret = dabusb_writemem (s, ptr->Address, ptr->Data, ptr->Length);
		if (ret < 0) {
			printk (KERN_ERR MODSTR "dabusb_writemem failed (%04X %p %d)\n", ptr->Address, ptr->Data, ptr->Length);
			break;
		}
		ptr++;
	}
	ret = dabusb_8051_reset (s, 0);
	printk (KERN_DEBUG MODSTR "dabusb_loadmem: exit\n");
	return ret;
}
/* --------------------------------------------------------------------- */
static int dabusb_fpga_clear (pdabusb_t s, pbulk_transfer_t b)
{
	b->size = 4;
	b->data[0] = 0x2a;
	b->data[1] = 0;
	b->data[2] = 0;
	b->data[3] = 0;
	printk (KERN_DEBUG MODSTR "dabusb_fpga_clear\n");
	return dabusb_bulk (s, b);
}
/* --------------------------------------------------------------------- */
static int dabusb_fpga_init (pdabusb_t s, pbulk_transfer_t b)
{
	b->size = 4;
	b->data[0] = 0x2c;
	b->data[1] = 0;
	b->data[2] = 0;
	b->data[3] = 0;
	printk (KERN_DEBUG MODSTR "dabusb_fpga_init\n");
	return dabusb_bulk (s, b);
}
/* --------------------------------------------------------------------- */
static int dabusb_fpga_download (pdabusb_t s, const char *fname)
{
	pbulk_transfer_t b = kmalloc (sizeof (bulk_transfer_t), GFP_KERNEL);
	unsigned int blen, n;
	int ret;
	unsigned char *buf = bitstream;

	printk (KERN_DEBUG MODSTR "Enter dabusb_fpga_download (internal)\n");
	if (!b) {
		printk (KERN_ERR MODSTR "kmalloc(sizeof(bulk_transfer_t))==NULL\n");
		return -ENOMEM;
	}

	b->pipe = 1;
	ret = dabusb_fpga_clear (s, b);
	mdelay (10);
	blen = buf[73] + (buf[72] << 8);
	printk (KERN_DEBUG MODSTR "Bitstream len: %i\n", blen);

	b->data[0] = 0x2b;
	b->data[1] = 0;
	b->data[2] = 0;
	b->data[3] = 60;

	for (n = 0; n <= blen + 60; n += 60) {
		// some cclks for startup
		b->size = 64;
		memcpy (b->data + 4, buf + 74 + n, 60);
		ret = dabusb_bulk (s, b);
		if (ret < 0) {
			printk (KERN_ERR MODSTR "dabusb_bulk failed.\n");
			break;
		}
		mdelay (1);
	}

	ret = dabusb_fpga_init (s, b);
	kfree (b);
	printk (KERN_DEBUG MODSTR "exit dabusb_fpga_download\n");
	return ret;
}

static loff_t dabusb_llseek (struct file *file, loff_t offset, int origin)
{
	return -ESPIPE;
}

static int dabusb_stop (pdabusb_t s)
{
	printk (KERN_DEBUG MODSTR "dabusb_stop\n");
	s->state = _stopped;
	dabusb_cancel_queue (s, &s->rec_buff_list);
	printk (KERN_DEBUG MODSTR "pending_io: %d\n", s->pending_io.counter);
	s->pending_io.counter = 0;
	return 0;
}

static int dabusb_startrek (pdabusb_t s)
{
	if (!s->got_mem && s->state != _started) {
		printk (KERN_DEBUG MODSTR "dabusb_startrek\n");
		if (dabusb_alloc_buffers (s) < 0)
			return -ENOMEM;
		dabusb_stop (s);
		dabusb_reset_pipe (s->usbdev, _DABUSB_ISOPIPE);
		s->state = _started;
		s->readptr = 0;
	}

	if (!list_empty (&s->free_buff_list)) {
		pbuff_t end;
		int ret;

		while (!dabusb_add_buf_tail (s, &s->rec_buff_list, &s->free_buff_list)) {
			//printk("submitting: end:%p s->rec_buff_list:%p\n", s->rec_buff_list.prev, &s->rec_buff_list);

			end = list_entry (s->rec_buff_list.prev, buff_t, buff_list);
			//printk("pipesize:%d number_of_packets:%d\n",pipesize, end->purb->number_of_packets);                  

			ret = usb_submit_urb (end->purb);
			if (ret) {
				printk (KERN_ERR MODSTR "usb_submit_urb returned:%d\n", ret);
				if (dabusb_add_buf_tail (s, &s->free_buff_list, &s->rec_buff_list))
					printk (KERN_ERR MODSTR "startrek: dabusb_add_buf_tail failed");
			}
			else
				atomic_inc (&s->pending_io);
		}
		//printk(KERN_DEBUG MODSTR"pending_io: %d\n",s->pending_io.counter);
	}
	return 0;
}

static ssize_t dabusb_read (struct file *file, char *buf, size_t count, loff_t * ppos)
{
	pdabusb_t s = (pdabusb_t) file->private_data;
	unsigned ret = 0;
	int rem;
	int cnt;
	pbuff_t b;
	purb_t purb = NULL;

	//printk(KERN_DEBUG MODSTR"dabusb_read\n");

	if (*ppos)
		return -ESPIPE;

	if (s->remove_pending)
		return -EIO;

//      down(&s->mutex);
	if (!s->usbdev) {
//              up(&s->mutex);
		return -EIO;
	}

	while (count > 0) {
		dabusb_startrek (s);
		if (list_empty (&s->rec_buff_list)) {
			printk (KERN_ERR MODSTR "shit... rec_buf_list is empty\n");
			goto err;
		}
		b = list_entry (s->rec_buff_list.next, buff_t, buff_list);
		purb = b->purb;

		if (purb->status == USB_ST_URB_PENDING) {
			//printk("after comp\n");
			if (file->f_flags & O_NONBLOCK)		// return nonblocking
			 {
				if (!ret)
					ret = -EAGAIN;
				goto err;
			}
			//printk("before sleep\n");
			interruptible_sleep_on (&s->wait);
			//printk("after sleep\n");
			if (signal_pending (current)) {
				if (!ret)
					ret = -ERESTARTSYS;
				goto err;
			}
			if (list_empty (&s->rec_buff_list)) {
				printk (KERN_ERR MODSTR "shit... still no buffer available.\n");
				goto err;
			}
			s->readptr = 0;
		}
		if (s->remove_pending) {
			ret = -EIO;
			goto err;
		}
		//printk("get pointers %d %d %d\n", purb->actual_length, s->readptr, count);            

		rem = purb->actual_length - s->readptr;		// set remaining bytes to copy

		if (count >= rem)
			cnt = rem;
		else
			cnt = count;

		//printk("copy_to_user:%p %p %d\n",buf, purb->transfer_buffer + s->readptr, cnt);

		if (copy_to_user (buf, purb->transfer_buffer + s->readptr, cnt)) {
			printk (KERN_ERR MODSTR "read: copy_to_user failed\n");
			if (!ret)
				ret = -EFAULT;
			goto err;
		}

		s->readptr += cnt;
		count -= cnt;
		buf += cnt;
		ret += cnt;

		if (s->readptr == purb->actual_length) {
			// finished, take next buffer
			//printk("next buffer...\n");
			if (dabusb_add_buf_tail (s, &s->free_buff_list, &s->rec_buff_list))
				printk (KERN_ERR MODSTR "read: dabusb_add_buf_tail failed");
			s->readptr = 0;
		}
	}
err:			//up(&s->mutex);
	return ret;
}

static int dabusb_open (struct inode *inode, struct file *file)
{
	int devnum = MINOR (inode->i_rdev);
	pdabusb_t s;

	if (devnum < DABUSB_MINOR || devnum > (DABUSB_MINOR + NRDABUSB))
		return -EIO;

	s = &dabusb[devnum - DABUSB_MINOR];
	printk (KERN_DEBUG MODSTR "dabusb_open\n");
	down (&s->mutex);
	while (!s->usbdev || s->opened) {
		up (&s->mutex);
		if (file->f_flags & O_NONBLOCK) {
			return -EBUSY;
		}
		schedule_timeout (HZ / 2);
		if (signal_pending (current))
			return -EAGAIN;
		down (&s->mutex);
	}
	s->opened = 1;
	up (&s->mutex);
	if (usb_set_interface (s->usbdev, _DABUSB_IF, 1) < 0) {
		printk (KERN_ERR "dabusb: set_interface failed\n");
		return -EINVAL;
	}
	file->f_pos = 0;
	file->private_data = s;
	MOD_INC_USE_COUNT;
	return 0;
}

static int dabusb_release (struct inode *inode, struct file *file)
{
	pdabusb_t s = (pdabusb_t) file->private_data;

	printk (KERN_DEBUG MODSTR "dabusb_release\n");

	down (&s->mutex);
	dabusb_stop (s);
	dabusb_free_buffers (s);
	up (&s->mutex);
	if (!s->remove_pending) {
		if (usb_set_interface (s->usbdev, _DABUSB_IF, 0) < 0)
			printk (KERN_ERR "dabusb: set_interface failed\n");
	}
	else
		wake_up (&s->remove_ok);
	MOD_DEC_USE_COUNT;
	s->opened = 0;
	return 0;
}

static int dabusb_ioctl (struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	pdabusb_t s = (pdabusb_t) file->private_data;
	pbulk_transfer_t pbulk;
	int ret = 0;
	int version = DABUSB_VERSION;
	DECLARE_WAITQUEUE (wait, current);

//        printk(KERN_DEBUG MODSTR"dabusb_ioctl\n");

	if (s->remove_pending)
		return -EIO;

	down (&s->mutex);
	if (!s->usbdev) {
		up (&s->mutex);
		return -EIO;
	}

	switch (cmd) {

	case IOCTL_DAB_BULK:
		pbulk = (pbulk_transfer_t) kmalloc (sizeof (bulk_transfer_t), GFP_KERNEL);
		if (!pbulk) {
			ret = -ENOMEM;
			break;
		}
		if (copy_from_user (pbulk, (void *) arg, sizeof (bulk_transfer_t))) {
			ret = -EFAULT;
			kfree (pbulk);
			break;
		}
		dabusb_bulk (s, pbulk);
		ret = copy_to_user ((void *) arg, pbulk, sizeof (bulk_transfer_t));
		kfree (pbulk);
		break;

	case IOCTL_DAB_OVERRUNS:
		ret = put_user (s->overruns, (unsigned int *) arg);
		break;

	case IOCTL_DAB_VERSION:
		ret = put_user (version, (unsigned int *) arg);
		break;

	default:
		ret = -ENOIOCTLCMD;
		break;
	}
	up (&s->mutex);
	return ret;
}

static struct file_operations dabusb_fops =
{
	dabusb_llseek,
	dabusb_read,
	NULL,			/* write */
	NULL,			/* readdir */
	NULL,			/* poll */
	dabusb_ioctl,
	NULL,			/* mmap */
	dabusb_open,
	NULL,			/* flush */
	dabusb_release,
	NULL,			/* fsync */
	NULL,			/* fasync */
	NULL,			/* check_media_change */
	NULL,			/* revalidate */
	NULL			/* lock */
};

static int dabusb_find_struct (void)
{
	int u;
	for (u = 0; u < NRDABUSB; u++) {
		pdabusb_t s = &dabusb[u];
		if (!s->usbdev)
			return u;
	}
	return -1;
}

/* --------------------------------------------------------------------- */
static void *dabusb_probe (struct usb_device *usbdev, unsigned int ifnum)
{
	int devnum;
	pdabusb_t s;

	printk (KERN_DEBUG MODSTR "dabusb: probe: vendor id 0x%x, device id 0x%x ifnum:%d\n",
	  usbdev->descriptor.idVendor, usbdev->descriptor.idProduct, ifnum);

	/* the 1234:5678 is just a self assigned test ID */
	if ((usbdev->descriptor.idVendor != 0x0547 || usbdev->descriptor.idProduct != 0x2131) &&
	    (usbdev->descriptor.idVendor != 0x0547 || usbdev->descriptor.idProduct != 0x9999))
		return NULL;

	/* We don't handle multiple configurations */
	if (usbdev->descriptor.bNumConfigurations != 1)
		return NULL;

	if (ifnum != _DABUSB_IF && usbdev->descriptor.idProduct == 0x9999)
		return NULL;

	devnum = dabusb_find_struct ();
	if (devnum == -1)
		return NULL;

	s = &dabusb[devnum];

	down (&s->mutex);
	s->remove_pending = 0;
	s->usbdev = usbdev;

	if (usb_set_configuration (usbdev, usbdev->config[0].bConfigurationValue) < 0) {
		printk (KERN_ERR MODSTR "set_configuration failed\n");
		goto reject;
	}
	if (usbdev->descriptor.idProduct == 0x2131)
		dabusb_loadmem (s, NULL);
	else {
		dabusb_fpga_download (s, NULL);

		if (usb_set_interface (s->usbdev, _DABUSB_IF, 0) < 0) {
			printk (KERN_ERR MODSTR "set_interface failed\n");
			goto reject;
		}
	}
	printk (KERN_DEBUG MODSTR "bound to interface: %d\n", ifnum);
	up (&s->mutex);
	MOD_INC_USE_COUNT;
	return s;

reject:
	up (&s->mutex);
	s->usbdev = NULL;
	return NULL;
}

static void dabusb_disconnect (struct usb_device *usbdev, void *ptr)
{
	pdabusb_t s = (pdabusb_t) ptr;
	printk (KERN_DEBUG MODSTR "dabusb_disconnect\n");
	s->remove_pending = 1;
	wake_up (&s->wait);
	if (s->state == _started)
		sleep_on (&s->remove_ok);
	s->usbdev = NULL;
	s->overruns = 0;
	MOD_DEC_USE_COUNT;
}

static struct usb_driver dabusb_driver =
{
	"dabusb",
	dabusb_probe,
	dabusb_disconnect,
	{NULL, NULL},
	&dabusb_fops,
	DABUSB_MINOR
};

/* --------------------------------------------------------------------- */

int dabusb_init (void)
{
	unsigned u;

	/* initialize struct */
	for (u = 0; u < NRDABUSB; u++) {
		pdabusb_t s = &dabusb[u];
		memset (s, 0, sizeof (dabusb_t));
		init_MUTEX (&s->mutex);
		s->usbdev = NULL;
		s->total_buffer_size = buffers;
		init_waitqueue_head (&s->wait);
		init_waitqueue_head (&s->remove_ok);
		spin_lock_init (&s->lock);
		INIT_LIST_HEAD (&s->free_buff_list);
		INIT_LIST_HEAD (&s->rec_buff_list);
//              printk("s->free_buff_list:%p\n",&s->free_buff_list);
		//              printk("s->rec_buff_list:%p\n",&s->rec_buff_list);
	}
	/* register misc device */
	usb_register (&dabusb_driver);
	printk (KERN_INFO "dabusb_init: driver registered\n");
	return 0;
}

void dabusb_cleanup (void)
{
	printk (KERN_DEBUG MODSTR "dabusb_cleanup\n");
	usb_deregister (&dabusb_driver);
}

/* --------------------------------------------------------------------- */

#ifdef MODULE
MODULE_AUTHOR ("Deti Fliegl, deti@fliegl.de");
MODULE_DESCRIPTION ("DAB-USB Interface Driver for Linux (c)1999");
MODULE_PARM (buffers, "i");
int init_module (void)
{
	return dabusb_init ();
}

void cleanup_module (void)
{
	dabusb_cleanup ();
}

#endif

/* --------------------------------------------------------------------- */
