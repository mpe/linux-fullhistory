/******************************************************************************
 *  speedtouch.c  --  Alcatel SpeedTouch USB xDSL modem driver.
 *
 *  Copyright (C) 2001, Alcatel
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  You should have received a copy of the GNU General Public License along with
 *  this program; if not, write to the Free Software Foundation, Inc., 59
 *  Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 ******************************************************************************/

/*
 *  Written by Johan Verrept (Johan.Verrept@advalvas.be)
 *
 *  1.5A:	- Version for inclusion in 2.5 series kernel
 *		- Modifications by Richard Purdie (rpurdie@rpsys.net)
 *		- made compatible with kernel 2.5.6 onwards by changing
 *		udsl_usb_send_data_context->urb to a pointer and adding code
 *		to alloc and free it
 *		- remove_wait_queue() added to udsl_atm_processqueue_thread()
 *
 *  1.5:	- fixed memory leak when atmsar_decode_aal5 returned NULL.
 *		(reported by stephen.robinson@zen.co.uk)
 *
 *  1.4:	- changed the spin_lock() under interrupt to spin_lock_irqsave()
 *		- unlink all active send urbs of a vcc that is being closed.
 *
 *  1.3.1:	- added the version number
 *
 *  1.3:	- Added multiple send urb support
 *		- fixed memory leak and vcc->tx_inuse starvation bug
 *		  when not enough memory left in vcc.
 *
 *  1.2:	- Fixed race condition in udsl_usb_send_data()
 *  1.1:	- Turned off packet debugging
 *
 */

#include <asm/semaphore.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <asm/uaccess.h>
#include <linux/usb.h>
#include <linux/smp_lock.h>
#include <linux/interrupt.h>
#include <linux/atm.h>
#include <linux/atmdev.h>
#include <linux/crc32.h>
#include "atmsar.h"

/*
#define DEBUG 1
#define DEBUG_PACKET 1
*/

#ifdef DEBUG
#define PDEBUG(arg...)  printk(KERN_DEBUG __FILE__ ": " arg)
#else
#define PDEBUG(arg...)
#endif


#ifdef DEBUG_PACKET
static int udsl_print_packet (const unsigned char *data, int len);
#define PACKETDEBUG(arg...) udsl_print_packet (arg)
#else
#define PACKETDEBUG(arg...)
#endif

#define DRIVER_AUTHOR	"Johan Verrept, Johan.Verrept@advalvas.be"
#define DRIVER_DESC	"Driver for the Alcatel SpeedTouch USB ADSL modem"
#define DRIVER_VERSION	"1.5A"

#define SPEEDTOUCH_VENDORID		0x06b9
#define SPEEDTOUCH_PRODUCTID		0x4061

#define UDSL_NUMBER_RCV_URBS		1
#define UDSL_NUMBER_SND_URBS		1
#define UDSL_NUMBER_SND_BUFS		(2*UDSL_NUMBER_SND_URBS)
#define UDSL_RCV_BUFFER_SIZE		(1*64) /* ATM cells */
#define UDSL_SND_BUFFER_SIZE		(1*64) /* ATM cells */
/* max should be (1500 IP mtu + 2 ppp bytes + 32 * 5 cellheader overhead) for
 * PPPoA and (1500 + 14 + 32*5 cellheader overhead) for PPPoE */
#define UDSL_MAX_AAL5_MRU		2048

#define UDSL_IOCTL_START		1
#define UDSL_IOCTL_STOP			2

/* endpoint declarations */

#define UDSL_ENDPOINT_DATA_OUT		0x07
#define UDSL_ENDPOINT_DATA_IN		0x87

#define hex2int(c) ( (c >= '0')&&(c <= '9') ?  (c - '0') : ((c & 0xf)+9) )

/* usb_device_id struct */

static struct usb_device_id udsl_usb_ids [] = {
	{ USB_DEVICE (SPEEDTOUCH_VENDORID, SPEEDTOUCH_PRODUCTID) },
	{ }			/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, udsl_usb_ids);

/* context declarations */

struct udsl_receiver {
	struct list_head list;
	struct sk_buff *skb;
	struct urb *urb;
	struct udsl_instance_data *instance;
};

struct udsl_send_buffer {
	struct list_head list;
	unsigned char *base;
	unsigned char *free_start;
	unsigned int free_cells;
};

struct udsl_sender {
	struct list_head list;
	struct udsl_send_buffer *buffer;
	struct urb *urb;
	struct udsl_instance_data *instance;
};

struct udsl_control {
	struct atm_skb_data atm_data;
	unsigned int num_cells;
	unsigned int num_entire;
	unsigned char cell_header [ATM_CELL_HEADER];
	unsigned int pdu_padding;
	unsigned char aal5_trailer [ATM_AAL5_TRAILER];
};

#define UDSL_SKB(x)		((struct udsl_control *)(x)->cb)

/*
 * UDSL main driver data
 */

struct udsl_instance_data {
	struct semaphore serialize;

	/* usb device part */
	struct usb_device *usb_dev;
	int firmware_loaded;

	/* atm device part */
	struct atm_dev *atm_dev;
	struct atmsar_vcc_data *atmsar_vcc_list;

	/* receiving */
	struct udsl_receiver all_receivers [UDSL_NUMBER_RCV_URBS];

	spinlock_t spare_receivers_lock;
	struct list_head spare_receivers;

	spinlock_t completed_receivers_lock;
	struct list_head completed_receivers;

	struct tasklet_struct receive_tasklet;

	/* sending */
	struct udsl_sender all_senders [UDSL_NUMBER_SND_URBS];
	struct udsl_send_buffer all_buffers [UDSL_NUMBER_SND_BUFS];

	struct sk_buff_head sndqueue;

	spinlock_t send_lock;
	struct list_head spare_senders;
	struct list_head spare_buffers;

	struct tasklet_struct send_tasklet;
	struct sk_buff *current_skb;			/* being emptied */
	struct udsl_send_buffer *current_buffer;	/* being filled */
	struct list_head filled_buffers;
};

static const char udsl_driver_name [] = "speedtch";

/*
 * atm driver prototypes and stuctures
 */

static void udsl_atm_dev_close (struct atm_dev *dev);
static int udsl_atm_open (struct atm_vcc *vcc, short vpi, int vci);
static void udsl_atm_close (struct atm_vcc *vcc);
static int udsl_atm_ioctl (struct atm_dev *dev, unsigned int cmd, void *arg);
static int udsl_atm_send (struct atm_vcc *vcc, struct sk_buff *skb);
static int udsl_atm_proc_read (struct atm_dev *atm_dev, loff_t *pos, char *page);

static struct atmdev_ops udsl_atm_devops = {
	.dev_close =	udsl_atm_dev_close,
	.open =		udsl_atm_open,
	.close =	udsl_atm_close,
	.ioctl =	udsl_atm_ioctl,
	.send =		udsl_atm_send,
	.proc_read =	udsl_atm_proc_read,
};

/*
 * usb driver prototypes and structures
 */
static int udsl_usb_probe (struct usb_interface *intf,
			   const struct usb_device_id *id);
static void udsl_usb_disconnect (struct usb_interface *intf);
static int udsl_usb_ioctl (struct usb_interface *intf, unsigned int code, void *user_data);

static struct usb_driver udsl_usb_driver = {
	.name =		udsl_driver_name,
	.probe =	udsl_usb_probe,
	.disconnect =	udsl_usb_disconnect,
	.ioctl =	udsl_usb_ioctl,
	.id_table =	udsl_usb_ids,
};


/*************
**  encode  **
*************/

static void udsl_groom_skb (struct atm_vcc *vcc, struct sk_buff *skb) {
	struct udsl_control *ctrl = UDSL_SKB (skb);
	unsigned int i, zero_padding;
	unsigned char zero = 0;
	u32 crc;

	ctrl->atm_data.vcc = vcc;
	ctrl->cell_header [0] = vcc->vpi >> 4;
	ctrl->cell_header [1] = (vcc->vpi << 4) | (vcc->vci >> 12);
	ctrl->cell_header [2] = vcc->vci >> 4;
	ctrl->cell_header [3] = vcc->vci << 4;
	ctrl->cell_header [4] = 0xec;

	ctrl->num_cells = (skb->len + ATM_AAL5_TRAILER + ATM_CELL_PAYLOAD - 1) / ATM_CELL_PAYLOAD;
	ctrl->num_entire = skb->len / ATM_CELL_PAYLOAD;

	zero_padding = ctrl->num_cells * ATM_CELL_PAYLOAD - skb->len - ATM_AAL5_TRAILER;

	if (ctrl->num_entire + 1 < ctrl->num_cells)
		ctrl->pdu_padding = zero_padding - (ATM_CELL_PAYLOAD - ATM_AAL5_TRAILER);
	else
		ctrl->pdu_padding = zero_padding;

	ctrl->aal5_trailer [0] = 0; /* UU = 0 */
	ctrl->aal5_trailer [1] = 0; /* CPI = 0 */
	ctrl->aal5_trailer [2] = skb->len >> 8;
	ctrl->aal5_trailer [3] = skb->len;

	crc = crc32_be (~0, skb->data, skb->len);
	for (i = 0; i < zero_padding; i++)
		crc = crc32_be (crc, &zero, 1);
	crc = crc32_be (crc, ctrl->aal5_trailer, 4);
	crc = ~crc;

	ctrl->aal5_trailer [4] = crc >> 24;
	ctrl->aal5_trailer [5] = crc >> 16;
	ctrl->aal5_trailer [6] = crc >> 8;
	ctrl->aal5_trailer [7] = crc;
}

static char *udsl_write_cell (struct sk_buff *skb, char *target) {
	struct udsl_control *ctrl = UDSL_SKB (skb);

	ctrl->num_cells--;

	memcpy (target, ctrl->cell_header, ATM_CELL_HEADER);
	target += ATM_CELL_HEADER;

	if (ctrl->num_entire) {
		ctrl->num_entire--;
		memcpy (target, skb->data, ATM_CELL_PAYLOAD);
		target += ATM_CELL_PAYLOAD;
		__skb_pull (skb, ATM_CELL_PAYLOAD);
		return target;
	}

	memcpy (target, skb->data, skb->len);
	target += skb->len;
	__skb_pull (skb, skb->len);

	memset (target, 0, ctrl->pdu_padding);
	target += ctrl->pdu_padding;

	if (ctrl->num_cells) {
		ctrl->pdu_padding = ATM_CELL_PAYLOAD - ATM_AAL5_TRAILER;
	} else {
		memcpy (target, ctrl->aal5_trailer, ATM_AAL5_TRAILER);
		target += ATM_AAL5_TRAILER;
		/* set pti bit in last cell */
		*(target + 3 - ATM_CELL_SIZE) |= 0x2;
	}

	return target;
}


/**************
**  receive  **
**************/

static void udsl_complete_receive (struct urb *urb, struct pt_regs *regs)
{
	struct udsl_instance_data *instance;
	struct udsl_receiver *rcv;
	unsigned long flags;

	if (!urb || !(rcv = urb->context) || !(instance = rcv->instance)) {
		PDEBUG ("udsl_complete_receive: bad urb!\n");
		return;
	}

	PDEBUG ("udsl_complete_receive entered (urb 0x%p, status %d)\n", urb, urb->status);

	tasklet_schedule (&instance->receive_tasklet);
	/* may not be in_interrupt() */
	spin_lock_irqsave (&instance->completed_receivers_lock, flags);
	list_add_tail (&rcv->list, &instance->completed_receivers);
	spin_unlock_irqrestore (&instance->completed_receivers_lock, flags);
}

static void udsl_process_receive (unsigned long data)
{
	struct udsl_instance_data *instance = (struct udsl_instance_data *) data;
	struct udsl_receiver *rcv;
	unsigned long flags;
	unsigned char *data_start;
	struct sk_buff *skb;
	struct urb *urb;
	struct atmsar_vcc_data *atmsar_vcc = NULL;
	struct sk_buff *new = NULL, *tmp = NULL;
	int err;

	PDEBUG ("udsl_process_receive entered\n");

	spin_lock_irqsave (&instance->completed_receivers_lock, flags);
	while (!list_empty (&instance->completed_receivers)) {
		rcv = list_entry (instance->completed_receivers.next, struct udsl_receiver, list);
		list_del (&rcv->list);
		spin_unlock_irqrestore (&instance->completed_receivers_lock, flags);

		urb = rcv->urb;
		PDEBUG ("udsl_process_receive: got packet %p with length %d and status %d\n", urb, urb->actual_length, urb->status);

		switch (urb->status) {
		case 0:
			PDEBUG ("udsl_process_receive: processing urb with rcv %p, urb %p, skb %p\n", rcv, urb, rcv->skb);

			/* update the skb structure */
			skb = rcv->skb;
			skb_trim (skb, 0);
			skb_put (skb, urb->actual_length);
			data_start = skb->data;

			PDEBUG ("skb->len = %d\n", skb->len);
			PACKETDEBUG (skb->data, skb->len);

			while ((new =
				atmsar_decode_rawcell (instance->atmsar_vcc_list, skb,
						       &atmsar_vcc)) != NULL) {
				PDEBUG ("(after cell processing)skb->len = %d\n", new->len);

				switch (atmsar_vcc->type) {
				case ATMSAR_TYPE_AAL5:
					tmp = new;
					new = atmsar_decode_aal5 (atmsar_vcc, new);

					/* we can't send NULL skbs upstream, the ATM layer would try to close the vcc... */
					if (new) {
						PDEBUG ("(after aal5 decap) skb->len = %d\n", new->len);
						if (new->len && atm_charge (atmsar_vcc->vcc, new->truesize)) {
							PACKETDEBUG (new->data, new->len);
							atmsar_vcc->vcc->push (atmsar_vcc->vcc, new);
						} else {
							PDEBUG
							    ("dropping incoming packet : rx_inuse = %d, vcc->sk->rcvbuf = %d, skb->true_size = %d\n",
							     atomic_read (&atmsar_vcc->vcc->rx_inuse),
							     atmsar_vcc->vcc->sk->rcvbuf, new->truesize);
							dev_kfree_skb (new);
						}
					} else {
						PDEBUG ("atmsar_decode_aal5 returned NULL!\n");
						dev_kfree_skb (tmp);
					}
					break;
				default:
					/* not supported. we delete the skb. */
					printk (KERN_INFO
						"SpeedTouch USB: illegal vcc type. Dropping packet.\n");
					dev_kfree_skb (new);
					break;
				}
			}

			/* restore skb */
			skb_push (skb, skb->data - data_start);

			usb_fill_bulk_urb (urb,
					   instance->usb_dev,
					   usb_rcvbulkpipe (instance->usb_dev, UDSL_ENDPOINT_DATA_IN),
					   (unsigned char *) rcv->skb->data,
					   UDSL_RCV_BUFFER_SIZE * ATM_CELL_SIZE,
					   udsl_complete_receive,
					   rcv);
			if (!(err = usb_submit_urb (urb, GFP_ATOMIC)))
				break;
			PDEBUG ("udsl_process_receive: submission failed (%d)\n", err);
			/* fall through */
		default: /* error or urb unlinked */
			PDEBUG ("udsl_process_receive: adding to spare_receivers\n");
			spin_lock_irqsave (&instance->spare_receivers_lock, flags);
			list_add (&rcv->list, &instance->spare_receivers);
			spin_unlock_irqrestore (&instance->spare_receivers_lock, flags);
			break;
		} /* switch */

		spin_lock_irqsave (&instance->completed_receivers_lock, flags);
	} /* while */
	spin_unlock_irqrestore (&instance->completed_receivers_lock, flags);
	PDEBUG ("udsl_process_receive successful\n");
}

static void udsl_fire_receivers (struct udsl_instance_data *instance)
{
	struct list_head receivers, *pos, *n;
	unsigned long flags;

	INIT_LIST_HEAD (&receivers);

	down (&instance->serialize);

	spin_lock_irqsave (&instance->spare_receivers_lock, flags);
	list_splice_init (&instance->spare_receivers, &receivers);
	spin_unlock_irqrestore (&instance->spare_receivers_lock, flags);

	list_for_each_safe (pos, n, &receivers) {
		struct udsl_receiver *rcv = list_entry (pos, struct udsl_receiver, list);

		PDEBUG ("udsl_fire_receivers: firing urb %p\n", rcv->urb);

		usb_fill_bulk_urb (rcv->urb,
				   instance->usb_dev,
				   usb_rcvbulkpipe (instance->usb_dev, UDSL_ENDPOINT_DATA_IN),
				   (unsigned char *) rcv->skb->data,
				   UDSL_RCV_BUFFER_SIZE * ATM_CELL_SIZE,
				   udsl_complete_receive,
				   rcv);

		if (usb_submit_urb (rcv->urb, GFP_KERNEL) < 0) {
			PDEBUG ("udsl_fire_receivers: submit failed!\n");
			spin_lock_irqsave (&instance->spare_receivers_lock, flags);
			list_move (pos, &instance->spare_receivers);
			spin_unlock_irqrestore (&instance->spare_receivers_lock, flags);
		}
	}

	up (&instance->serialize);
}


/***********
**  send  **
***********/

static void udsl_complete_send (struct urb *urb, struct pt_regs *regs)
{
	struct udsl_instance_data *instance;
	struct udsl_sender *snd;
	unsigned long flags;

	if (!urb || !(snd = urb->context) || !(instance = snd->instance)) {
		PDEBUG ("udsl_complete_send: bad urb!\n");
		return;
	}

	PDEBUG ("udsl_complete_send entered (urb 0x%p, status %d)\n", urb, urb->status);

	tasklet_schedule (&instance->send_tasklet);
	/* may not be in_interrupt() */
	spin_lock_irqsave (&instance->send_lock, flags);
	list_add (&snd->list, &instance->spare_senders);
	list_add (&snd->buffer->list, &instance->spare_buffers);
	spin_unlock_irqrestore (&instance->send_lock, flags);
}

static void udsl_process_send (unsigned long data)
{
	struct udsl_send_buffer *buf;
	unsigned int cells_to_write;
	int err;
	unsigned long flags;
	unsigned int i;
	struct udsl_instance_data *instance = (struct udsl_instance_data *) data;
	struct sk_buff *skb;
	struct udsl_sender *snd;
	unsigned char *target;

	PDEBUG ("udsl_process_send entered\n");

made_progress:
	spin_lock_irqsave (&instance->send_lock, flags);
	while (!list_empty (&instance->spare_senders)) {
		if (!list_empty (&instance->filled_buffers)) {
			buf = list_entry (instance->filled_buffers.next, struct udsl_send_buffer, list);
			list_del (&buf->list);
			PDEBUG ("sending filled buffer (0x%p)\n", buf);
		} else if ((buf = instance->current_buffer)) {
			instance->current_buffer = NULL;
			PDEBUG ("sending current buffer (0x%p)\n", buf);
		} else /* all buffers empty */
			break;

		snd = list_entry (instance->spare_senders.next, struct udsl_sender, list);
		list_del (&snd->list);
		spin_unlock_irqrestore (&instance->send_lock, flags);

		snd->buffer = buf;
	        usb_fill_bulk_urb (snd->urb,
				   instance->usb_dev,
				   usb_sndbulkpipe (instance->usb_dev, UDSL_ENDPOINT_DATA_OUT),
				   buf->base,
				   (UDSL_SND_BUFFER_SIZE - buf->free_cells) * ATM_CELL_SIZE,
				   udsl_complete_send,
				   snd);

		PDEBUG ("submitting urb 0x%p, contains %d cells\n", snd->urb, UDSL_SND_BUFFER_SIZE - buf->free_cells);

		if ((err = usb_submit_urb(snd->urb, GFP_ATOMIC)) < 0) {
			PDEBUG ("submission failed (%d)!\n", err);
			spin_lock_irqsave (&instance->send_lock, flags);
			list_add (&snd->list, &instance->spare_senders);
			spin_unlock_irqrestore (&instance->send_lock, flags);
			list_add (&buf->list, &instance->filled_buffers);
			return;
		}

		spin_lock_irqsave (&instance->send_lock, flags);
	} /* while */
	spin_unlock_irqrestore (&instance->send_lock, flags);

	if (!instance->current_skb && !(instance->current_skb = skb_dequeue (&instance->sndqueue))) {
		PDEBUG ("done - no more skbs\n");
		return;
	}

	skb = instance->current_skb;

	if (!(buf = instance->current_buffer)) {
		spin_lock_irqsave (&instance->send_lock, flags);
		if (list_empty (&instance->spare_buffers)) {
			instance->current_buffer = NULL;
			spin_unlock_irqrestore (&instance->send_lock, flags);
			PDEBUG ("done - no more buffers\n");
			return;
		}
		buf = list_entry (instance->spare_buffers.next, struct udsl_send_buffer, list);
		list_del (&buf->list);
		spin_unlock_irqrestore (&instance->send_lock, flags);

		buf->free_start = buf->base;
		buf->free_cells = UDSL_SND_BUFFER_SIZE;

		instance->current_buffer = buf;
	}

	cells_to_write = min (buf->free_cells, UDSL_SKB (skb)->num_cells);
	target = buf->free_start;

	PDEBUG ("writing %u cells from skb 0x%p to buffer 0x%p\n", cells_to_write, skb, buf);

	for (i = 0; i < cells_to_write; i++)
		target = udsl_write_cell (skb, target);

	buf->free_start = target;
	if (!(buf->free_cells -= cells_to_write)) {
		list_add_tail (&buf->list, &instance->filled_buffers);
		instance->current_buffer = NULL;
		PDEBUG ("queued filled buffer\n");
	}

	PDEBUG ("buffer contains %d cells, %d left\n", UDSL_SND_BUFFER_SIZE - buf->free_cells, buf->free_cells);

	if (!UDSL_SKB (skb)->num_cells) {
		struct atm_vcc *vcc = UDSL_SKB (skb)->atm_data.vcc;

		PDEBUG ("discarding empty skb\n");
		if (vcc->pop)
			vcc->pop (vcc, skb);
		else
			kfree_skb (skb);
		instance->current_skb = NULL;

		if (vcc->stats)
			atomic_inc (&vcc->stats->tx);
	}

	goto made_progress;
}

static void udsl_cancel_send (struct udsl_instance_data *instance, struct atm_vcc *vcc)
{
	unsigned long flags;
	struct sk_buff *skb, *n;

	PDEBUG ("udsl_cancel_send entered\n");
	spin_lock_irqsave (&instance->sndqueue.lock, flags);
	for (skb = instance->sndqueue.next, n = skb->next; skb != (struct sk_buff *)&instance->sndqueue; skb = n, n = skb->next)
		if (UDSL_SKB (skb)->atm_data.vcc == vcc) {
			PDEBUG ("popping skb 0x%p\n", skb);
			__skb_unlink (skb, &instance->sndqueue);
			if (vcc->pop)
				vcc->pop (vcc, skb);
			else
				kfree_skb (skb);
		}
	spin_unlock_irqrestore (&instance->sndqueue.lock, flags);

	tasklet_disable (&instance->send_tasklet);
	if ((skb = instance->current_skb) && (UDSL_SKB (skb)->atm_data.vcc == vcc)) {
		PDEBUG ("popping current skb (0x%p)\n", skb);
		instance->current_skb = NULL;
		if (vcc->pop)
			vcc->pop (vcc, skb);
		else
			kfree_skb (skb);
	}
	tasklet_enable (&instance->send_tasklet);
	PDEBUG ("udsl_cancel_send done\n");
}

static int udsl_atm_send (struct atm_vcc *vcc, struct sk_buff *skb)
{
	struct udsl_instance_data *instance = vcc->dev->dev_data;

	PDEBUG ("udsl_atm_send called (skb 0x%p, len %u)\n", skb, skb->len);

	if (!instance) {
		PDEBUG ("NULL instance!\n");
		return -EINVAL;
	}

	if (!instance->firmware_loaded)
		return -EAGAIN;

	if (vcc->qos.aal != ATM_AAL5) {
		PDEBUG ("unsupported ATM type %d!\n", vcc->qos.aal);
		return -EINVAL;
	}

	if (skb->len > ATM_MAX_AAL5_PDU) {
		PDEBUG ("packet too long (%d vs %d)!\n", skb->len, ATM_MAX_AAL5_PDU);
		return -EINVAL;
	}

	PACKETDEBUG (skb->data, skb->len);

	udsl_groom_skb (vcc, skb);
	skb_queue_tail (&instance->sndqueue, skb);
	tasklet_schedule (&instance->send_tasklet);

	return 0;
}


/************
**   ATM   **
************/

/***************************************************************************
*
* init functions
*
****************************************************************************/

static void udsl_atm_dev_close (struct atm_dev *dev)
{
	struct udsl_instance_data *instance = dev->dev_data;

	if (!instance) {
		PDEBUG ("udsl_atm_dev_close: NULL instance!\n");
		return;
	}

	PDEBUG ("udsl_atm_dev_close: queue has %u elements\n", instance->sndqueue.qlen);

	PDEBUG ("udsl_atm_dev_close: killing tasklet\n");
	tasklet_kill (&instance->send_tasklet);
	PDEBUG ("udsl_atm_dev_close: freeing instance\n");
	kfree (instance);
}


/***************************************************************************
*
* ATM helper functions
*
****************************************************************************/

static int udsl_atm_proc_read (struct atm_dev *atm_dev, loff_t *pos, char *page)
{
	struct udsl_instance_data *instance = atm_dev->dev_data;
	int left = *pos;

	if (!instance) {
		PDEBUG ("NULL instance!\n");
		return -ENODEV;
	}

	if (!left--)
		return sprintf (page, "SpeedTouch USB %s-%s (%02x:%02x:%02x:%02x:%02x:%02x)\n",
				instance->usb_dev->bus->bus_name, instance->usb_dev->devpath,
				atm_dev->esi[0], atm_dev->esi[1], atm_dev->esi[2],
				atm_dev->esi[3], atm_dev->esi[4], atm_dev->esi[5]);

	if (!left--)
		return sprintf (page, "AAL5: tx %d ( %d err ), rx %d ( %d err, %d drop )\n",
				atomic_read (&atm_dev->stats.aal5.tx),
				atomic_read (&atm_dev->stats.aal5.tx_err),
				atomic_read (&atm_dev->stats.aal5.rx),
				atomic_read (&atm_dev->stats.aal5.rx_err),
				atomic_read (&atm_dev->stats.aal5.rx_drop));

	return 0;
}


/***************************************************************************
*
* SAR driver entries
*
****************************************************************************/

static int udsl_atm_open (struct atm_vcc *vcc, short vpi, int vci)
{
	struct udsl_instance_data *instance = vcc->dev->dev_data;

	PDEBUG ("udsl_atm_open called\n");

	if (!instance) {
		PDEBUG ("NULL instance!\n");
		return -ENODEV;
	}

	/* at the moment only AAL5 support */
	if (vcc->qos.aal != ATM_AAL5)
		return -EINVAL;

	MOD_INC_USE_COUNT;

	vcc->dev_data =
	    atmsar_open (&(instance->atmsar_vcc_list), vcc, ATMSAR_TYPE_AAL5, vpi, vci, 0, 0,
			 ATMSAR_USE_53BYTE_CELL | ATMSAR_SET_PTI);
	if (!vcc->dev_data) {
		MOD_DEC_USE_COUNT;
		return -ENOMEM;	/* this is the only reason atmsar_open can fail... */
	}

	vcc->vpi = vpi;
	vcc->vci = vci;
	set_bit (ATM_VF_ADDR, &vcc->flags);
	set_bit (ATM_VF_PARTIAL, &vcc->flags);
	set_bit (ATM_VF_READY, &vcc->flags);

	((struct atmsar_vcc_data *)vcc->dev_data)->mtu = UDSL_MAX_AAL5_MRU;

	if (instance->firmware_loaded)
		udsl_fire_receivers (instance);

	PDEBUG ("udsl_atm_open successfull\n");
	return 0;
}

static void udsl_atm_close (struct atm_vcc *vcc)
{
	struct udsl_instance_data *instance = vcc->dev->dev_data;

	PDEBUG ("udsl_atm_close called\n");

	if (!instance) {
		PDEBUG ("NULL instance!\n");
		return;
	}

	/* freeing resources */
	/* cancel all sends on this vcc */
	udsl_cancel_send (instance, vcc);

	atmsar_close (&(instance->atmsar_vcc_list), vcc->dev_data);
	vcc->dev_data = NULL;
	clear_bit (ATM_VF_PARTIAL, &vcc->flags);

	/* freeing address */
	vcc->vpi = ATM_VPI_UNSPEC;
	vcc->vci = ATM_VCI_UNSPEC;
	clear_bit (ATM_VF_ADDR, &vcc->flags);

	MOD_DEC_USE_COUNT;

	PDEBUG ("udsl_atm_close successfull\n");
	return;
}

static int udsl_atm_ioctl (struct atm_dev *dev, unsigned int cmd, void *arg)
{
	switch (cmd) {
	case ATM_QUERYLOOP:
		return put_user (ATM_LM_NONE, (int *) arg) ? -EFAULT : 0;
	default:
		return -ENOIOCTLCMD;
	}
}


/************
**   USB   **
************/

static int udsl_usb_ioctl (struct usb_interface *intf, unsigned int code, void *user_data)
{
	struct udsl_instance_data *instance = usb_get_intfdata (intf);

	PDEBUG ("udsl_usb_ioctl entered\n");

	if (!instance) {
		PDEBUG ("NULL instance!\n");
		return -ENODEV;
	}

	switch (code) {
	case UDSL_IOCTL_START:
		instance->atm_dev->signal = ATM_PHY_SIG_FOUND;
		down (&instance->serialize); /* vs self */
		if (!instance->firmware_loaded) {
			usb_set_interface (instance->usb_dev, 1, 1);
			instance->firmware_loaded = 1;
		}
		up (&instance->serialize);
		udsl_fire_receivers (instance);
		return 0;
	case UDSL_IOCTL_STOP:
		instance->atm_dev->signal = ATM_PHY_SIG_LOST;
		return 0;
	default:
		return -ENOTTY;
	}
}

static int udsl_usb_probe (struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	int ifnum = intf->altsetting->desc.bInterfaceNumber;
	struct udsl_instance_data *instance;
	unsigned char mac_str [13];
	unsigned char mac [6];
	int i;

	PDEBUG ("Trying device with Vendor=0x%x, Product=0x%x, ifnum %d\n",
		dev->descriptor.idVendor, dev->descriptor.idProduct, ifnum);

	if ((dev->descriptor.bDeviceClass != USB_CLASS_VENDOR_SPEC) ||
	    (dev->descriptor.idVendor != SPEEDTOUCH_VENDORID) ||
	    (dev->descriptor.idProduct != SPEEDTOUCH_PRODUCTID) || (ifnum != 1))
		return -ENODEV;

	PDEBUG ("Device Accepted\n");

	/* instance init */
	if (!(instance = kmalloc (sizeof (struct udsl_instance_data), GFP_KERNEL))) {
		PDEBUG ("No memory for Instance data!\n");
		return -ENOMEM;
	}

	memset (instance, 0, sizeof (struct udsl_instance_data));

	init_MUTEX (&instance->serialize);

	instance->usb_dev = dev;

	spin_lock_init (&instance->spare_receivers_lock);
	INIT_LIST_HEAD (&instance->spare_receivers);

	spin_lock_init (&instance->completed_receivers_lock);
	INIT_LIST_HEAD (&instance->completed_receivers);

	tasklet_init (&instance->receive_tasklet, udsl_process_receive, (unsigned long) instance);

	skb_queue_head_init (&instance->sndqueue);

	spin_lock_init (&instance->send_lock);
	INIT_LIST_HEAD (&instance->spare_senders);
	INIT_LIST_HEAD (&instance->spare_buffers);

	tasklet_init (&instance->send_tasklet, udsl_process_send, (unsigned long) instance);
	INIT_LIST_HEAD (&instance->filled_buffers);

	/* receive init */
	for (i = 0; i < UDSL_NUMBER_RCV_URBS; i++) {
		struct udsl_receiver *rcv = &(instance->all_receivers[i]);

		if (!(rcv->skb = dev_alloc_skb (UDSL_RCV_BUFFER_SIZE * ATM_CELL_SIZE))) {
			PDEBUG ("No memory for skb %d!\n", i);
			goto fail;
		}

		if (!(rcv->urb = usb_alloc_urb (0, GFP_KERNEL))) {
			PDEBUG ("No memory for receive urb %d!\n", i);
			goto fail;
		}

		rcv->instance = instance;

		list_add (&rcv->list, &instance->spare_receivers);

		PDEBUG ("skb->truesize = %d (asked for %d)\n", rcv->skb->truesize, UDSL_RCV_BUFFER_SIZE * ATM_CELL_SIZE);
	}

	/* send init */
	for (i = 0; i < UDSL_NUMBER_SND_URBS; i++) {
		struct udsl_sender *snd = &(instance->all_senders[i]);

		if (!(snd->urb = usb_alloc_urb (0, GFP_KERNEL))) {
			PDEBUG ("No memory for send urb %d!\n", i);
			goto fail;
		}

		snd->instance = instance;

		list_add (&snd->list, &instance->spare_senders);
	}

	for (i = 0; i < UDSL_NUMBER_SND_BUFS; i++) {
		struct udsl_send_buffer *buf = &(instance->all_buffers[i]);

		if (!(buf->base = kmalloc (UDSL_SND_BUFFER_SIZE * ATM_CELL_SIZE, GFP_KERNEL))) {
			PDEBUG ("No memory for send buffer %d!\n", i);
			goto fail;
		}

		list_add (&buf->list, &instance->spare_buffers);
	}

	/* atm init */
	if (!(instance->atm_dev = atm_dev_register (udsl_driver_name, &udsl_atm_devops, -1, 0))) {
		PDEBUG ("failed to register ATM device!\n");
		goto fail;
	}

	instance->atm_dev->ci_range.vpi_bits = ATM_CI_MAX;
	instance->atm_dev->ci_range.vci_bits = ATM_CI_MAX;
	instance->atm_dev->signal = ATM_PHY_SIG_LOST;

	/* tmp init atm device, set to 128kbit */
	instance->atm_dev->link_rate = 128 * 1000 / 424;

	/* set MAC address, it is stored in the serial number */
	usb_string (instance->usb_dev, instance->usb_dev->descriptor.iSerialNumber, mac_str, 13);
	for (i = 0; i < 6; i++)
		mac[i] = (hex2int (mac_str[i * 2]) * 16) + (hex2int (mac_str[i * 2 + 1]));

	PDEBUG ("MAC is %02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

	memcpy (instance->atm_dev->esi, mac, 6);

	wmb ();

	instance->atm_dev->dev_data = instance;

	usb_set_intfdata (intf, instance);

	return 0;

fail:
	for (i = 0; i < UDSL_NUMBER_SND_BUFS; i++)
		kfree (instance->all_buffers[i].base);

	for (i = 0; i < UDSL_NUMBER_SND_URBS; i++)
		usb_free_urb (instance->all_senders[i].urb);

	for (i = 0; i < UDSL_NUMBER_RCV_URBS; i++) {
		struct udsl_receiver *rcv = &(instance->all_receivers[i]);

		usb_free_urb (rcv->urb);

		if (rcv->skb)
			kfree_skb (rcv->skb);
	}

	kfree (instance);

	return -ENOMEM;
}

static void udsl_usb_disconnect (struct usb_interface *intf)
{
	struct udsl_instance_data *instance = usb_get_intfdata (intf);
	struct list_head *pos;
	unsigned long flags;
	unsigned int count = 0;
	int result, i;

	PDEBUG ("disconnecting\n");

	usb_set_intfdata (intf, NULL);

	if (!instance) {
		PDEBUG ("NULL instance!\n");
		return;
	}

	tasklet_disable (&instance->receive_tasklet);

	/* receive finalize */
	down (&instance->serialize); /* vs udsl_fire_receivers */
	/* no need to take the spinlock */
	list_for_each (pos, &instance->spare_receivers)
		if (++count > UDSL_NUMBER_RCV_URBS)
			panic (__FILE__ ": memory corruption detected at line %d!\n", __LINE__);
	INIT_LIST_HEAD (&instance->spare_receivers);
	up (&instance->serialize);

	PDEBUG ("udsl_usb_disconnect: flushed %u spare receivers\n", count);

	count = UDSL_NUMBER_RCV_URBS - count;

	for (i = 0; i < UDSL_NUMBER_RCV_URBS; i++)
		if ((result = usb_unlink_urb (instance->all_receivers[i].urb)) < 0)
			PDEBUG ("udsl_usb_disconnect: usb_unlink_urb on receive urb %d returned %d\n", i, result);

	/* wait for completion handlers to finish */
	do {
		unsigned int completed = 0;

		spin_lock_irqsave (&instance->completed_receivers_lock, flags);
		list_for_each (pos, &instance->completed_receivers)
			if (++completed > count)
				panic (__FILE__ ": memory corruption detected at line %d!\n", __LINE__);
		spin_unlock_irqrestore (&instance->completed_receivers_lock, flags);

		PDEBUG ("udsl_usb_disconnect: found %u completed receivers\n", completed);

		if (completed == count)
			break;

		yield ();
	} while (1);

	PDEBUG ("udsl_usb_disconnect: flushing\n");
	/* no need to take the spinlock */
	INIT_LIST_HEAD (&instance->completed_receivers);

	tasklet_enable (&instance->receive_tasklet);
	tasklet_kill (&instance->receive_tasklet);

	PDEBUG ("udsl_usb_disconnect: freeing receivers\n");
	for (i = 0; i < UDSL_NUMBER_RCV_URBS; i++) {
		struct udsl_receiver *rcv = &(instance->all_receivers[i]);

		usb_free_urb (rcv->urb);
		kfree_skb (rcv->skb);
	}

	/* send finalize */
	tasklet_disable (&instance->send_tasklet);

	for (i = 0; i < UDSL_NUMBER_SND_URBS; i++)
		if ((result = usb_unlink_urb (instance->all_senders[i].urb)) < 0)
			PDEBUG ("udsl_usb_disconnect: usb_unlink_urb on send urb %d returned %d\n", i, result);

	/* wait for completion handlers to finish */
	do {
		count = 0;
		spin_lock_irqsave (&instance->send_lock, flags);
		list_for_each (pos, &instance->spare_senders)
			if (++count > UDSL_NUMBER_SND_URBS)
				panic (__FILE__ ": memory corruption detected at line %d!\n", __LINE__);
		spin_unlock_irqrestore (&instance->send_lock, flags);

		PDEBUG ("udsl_usb_disconnect: found %u spare senders\n", count);

		if (count == UDSL_NUMBER_SND_URBS)
			break;

		yield ();
	} while (1);

	PDEBUG ("udsl_usb_disconnect: flushing\n");
	/* no need to take the spinlock */
	INIT_LIST_HEAD (&instance->spare_senders);
	INIT_LIST_HEAD (&instance->spare_buffers);
	instance->current_buffer = NULL;

	tasklet_enable (&instance->send_tasklet);

	PDEBUG ("udsl_usb_disconnect: freeing senders\n");
	for (i = 0; i < UDSL_NUMBER_SND_URBS; i++)
		usb_free_urb (instance->all_senders[i].urb);

	PDEBUG ("udsl_usb_disconnect: freeing buffers\n");
	for (i = 0; i < UDSL_NUMBER_SND_BUFS; i++)
		kfree (instance->all_buffers[i].base);

	/* atm finalize */
	shutdown_atm_dev (instance->atm_dev);
}


/***************************************************************************
*
* Driver Init
*
****************************************************************************/

static int __init udsl_usb_init (void)
{
	struct sk_buff *skb; /* dummy for sizeof */

	PDEBUG ("udsl_usb_init: driver version " DRIVER_VERSION "\n");

	if (sizeof (struct udsl_control) > sizeof (skb->cb)) {
		printk (KERN_ERR __FILE__ ": unusable with this kernel!\n");
		return -EIO;
	}

	return usb_register (&udsl_usb_driver);
}

static void __exit udsl_usb_cleanup (void)
{
	PDEBUG ("udsl_usb_cleanup\n");

	usb_deregister (&udsl_usb_driver);
}

module_init (udsl_usb_init);
module_exit (udsl_usb_cleanup);

MODULE_AUTHOR (DRIVER_AUTHOR);
MODULE_DESCRIPTION (DRIVER_DESC);
MODULE_LICENSE ("GPL");


#ifdef DEBUG_PACKET
/*******************************************************************************
*
* Debug
*
*******************************************************************************/

static int udsl_print_packet (const unsigned char *data, int len)
{
	unsigned char buffer [256];
	int i = 0, j = 0;

	for (i = 0; i < len;) {
		buffer[0] = '\0';
		sprintf (buffer, "%.3d :", i);
		for (j = 0; (j < 16) && (i < len); j++, i++) {
			sprintf (buffer, "%s %2.2x", buffer, data[i]);
		}
		PDEBUG ("%s\n", buffer);
	}
	return i;
}

#endif				/* PACKETDEBUG */
