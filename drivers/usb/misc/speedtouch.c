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
#include "atmsar.h"

/*
#define DEBUG 1
#define DEBUG_PACKET 1
*/

#ifdef DEBUG
#define PDEBUG(arg...)  printk(KERN_DEBUG "SpeedTouch USB: " arg)
#else
#define PDEBUG(arg...)
#endif


#ifdef DEBUG_PACKET
#define PACKETDEBUG(arg...) udsl_print_packet ( arg )
#else
#define PACKETDEBUG(arg...)
#endif

#define DRIVER_AUTHOR	"Johan Verrept, Johan.Verrept@advalvas.be"
#define DRIVER_DESC	"Driver for the Alcatel SpeedTouch USB ADSL modem"
#define DRIVER_VERSION	"1.5A"

#define SPEEDTOUCH_VENDORID		0x06b9
#define SPEEDTOUCH_PRODUCTID		0x4061

#define UDSL_OBUF_SIZE			32768
#define UDSL_MINOR			48
#define UDSL_NUMBER_RCV_URBS		1
#define UDSL_NUMBER_SND_URBS		1
#define UDSL_RECEIVE_BUFFER_SIZE	64*53
/* max should be (1500 IP mtu + 2 ppp bytes + 32 * 5 cellheader overhead) for
 * PPPoA and (1500 + 14 + 32*5 cellheader overhead) for PPPoE */
#define UDSL_MAX_AAL5_MRU		2048
#define UDSL_SEND_CONTEXTS		8

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

struct udsl_usb_send_data_context {
	struct urb *urb;
	struct sk_buff *skb;
	struct atm_vcc *vcc;
	struct udsl_instance_data *instance;
};

/*
 * UDSL main driver data
 */

struct udsl_instance_data {
	struct semaphore serialize;

	/* usb device part */
	struct usb_device *usb_dev;
	struct sk_buff_head sndqueue;
	struct udsl_usb_send_data_context send_ctx [UDSL_NUMBER_SND_URBS];
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
};

static const char udsl_driver_name [] = "Alcatel SpeedTouch USB";

#ifdef DEBUG_PACKET
static int udsl_print_packet (const unsigned char *data, int len);
#endif

/*
 * atm driver prototypes and stuctures
 */

static int udsl_atm_open (struct atm_vcc *vcc, short vpi, int vci);
static void udsl_atm_close (struct atm_vcc *vcc);
static int udsl_atm_ioctl (struct atm_dev *dev, unsigned int cmd, void *arg);
static int udsl_atm_send (struct atm_vcc *vcc, struct sk_buff *skb);
static int udsl_atm_proc_read (struct atm_dev *atm_dev, loff_t *pos, char *page);

static struct atmdev_ops udsl_atm_devops = {
	.open =		udsl_atm_open,
	.close =	udsl_atm_close,
	.ioctl =	udsl_atm_ioctl,
	.send =		udsl_atm_send,
	.proc_read =	udsl_atm_proc_read,
};

struct udsl_atm_dev_data {
	struct atmsar_vcc_data *atmsar_vcc;
};

/*
 * usb driver prototypes and structures
 */
static int udsl_usb_probe (struct usb_interface *intf,
			   const struct usb_device_id *id);
static void udsl_usb_disconnect (struct usb_interface *intf);
static int udsl_usb_send_data (struct udsl_instance_data *instance, struct atm_vcc *vcc,
			struct sk_buff *skb);
static int udsl_usb_ioctl (struct usb_interface *intf, unsigned int code, void *user_data);
static int udsl_usb_cancelsends (struct udsl_instance_data *instance, struct atm_vcc *vcc);

static struct usb_driver udsl_usb_driver = {
	.name =		udsl_driver_name,
	.probe =	udsl_usb_probe,
	.disconnect =	udsl_usb_disconnect,
	.ioctl =	udsl_usb_ioctl,
	.id_table =	udsl_usb_ids,
};


/**************
**  receive  **
**************/

static void udsl_complete_receive (struct urb *urb, struct pt_regs *regs)
{
	struct udsl_instance_data *instance;
	struct udsl_receiver *rcv;
	unsigned long flags;

	PDEBUG ("udsl_complete_receive entered\n");

	if (!urb || !(rcv = urb->context) || !(instance = rcv->instance)) {
		PDEBUG ("udsl_complete_receive: bad urb!\n");
		return;
	}

	/* may not be in_interrupt() */
	spin_lock_irqsave (&instance->completed_receivers_lock, flags);
	list_add_tail (&rcv->list, &instance->completed_receivers);
	spin_unlock_irqrestore (&instance->completed_receivers_lock, flags);
	PDEBUG ("udsl_complete_receive: scheduling tasklet\n");
	tasklet_schedule (&instance->receive_tasklet);
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
					   UDSL_RECEIVE_BUFFER_SIZE,
					   udsl_complete_receive,
					   rcv);
			if (!usb_submit_urb (urb, GFP_ATOMIC))
				break;
			PDEBUG ("udsl_process_receive: submission failed\n");
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
				   UDSL_RECEIVE_BUFFER_SIZE,
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


/************
**   ATM   **
************/

/***************************************************************************
*
* init functions
*
****************************************************************************/

static void udsl_atm_stopdevice (struct udsl_instance_data *instance)
{
	struct atm_vcc *walk;
	struct atm_dev *atm_dev;

	if (!instance->atm_dev)
		return;

	atm_dev = instance->atm_dev;

	atm_dev->signal = ATM_PHY_SIG_LOST;
	walk = atm_dev->vccs;
	shutdown_atm_dev (atm_dev);

	for (; walk; walk = walk->next)
		wake_up (&walk->sleep);

	instance->atm_dev = NULL;
}


/***************************************************************************
*
* ATM helper functions
*
****************************************************************************/
static struct sk_buff *udsl_atm_alloc_tx (struct atm_vcc *vcc, unsigned int size)
{
	struct atmsar_vcc_data *atmsar_vcc =
	    ((struct udsl_atm_dev_data *) vcc->dev_data)->atmsar_vcc;
	if (atmsar_vcc)
		return atmsar_alloc_tx (atmsar_vcc, size);

	printk (KERN_INFO
		"SpeedTouch USB: udsl_atm_alloc_tx could not find correct alloc_tx function !\n");
	return NULL;
}

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
		return sprintf (page, "AAL0: tx %d ( %d err ), rx %d ( %d err, %d drop )\n",
				atomic_read (&atm_dev->stats.aal0.tx),
				atomic_read (&atm_dev->stats.aal0.tx_err),
				atomic_read (&atm_dev->stats.aal0.rx),
				atomic_read (&atm_dev->stats.aal0.rx_err),
				atomic_read (&atm_dev->stats.aal0.rx_drop));

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
* ATM DATA functions
*
****************************************************************************/
static int udsl_atm_send (struct atm_vcc *vcc, struct sk_buff *skb)
{
	struct udsl_atm_dev_data *dev_data = vcc->dev_data;
	struct udsl_instance_data *instance = vcc->dev->dev_data;
	struct sk_buff *new = NULL;
	int err;

	PDEBUG ("udsl_atm_send called\n");

	if (!dev_data || !instance) {
		PDEBUG ("NULL data!\n");
		return -EINVAL;
	}

	if (!instance->firmware_loaded)
		return -EAGAIN;

	switch (vcc->qos.aal) {
	case ATM_AAL5:
		new = atmsar_encode_aal5 (dev_data->atmsar_vcc, skb);
		if (!new)
			goto nomem;
		if (new != skb) {
			vcc->pop (vcc, skb);
			skb = new;
		}
		new = atmsar_encode_rawcell (dev_data->atmsar_vcc, skb);
		if (!new)
			goto nomem;
		if (new != skb) {
			vcc->pop (vcc, skb);
			skb = new;
		}
		err = udsl_usb_send_data (instance, vcc, skb);
		PDEBUG ("udsl_atm_send successfull (%d)\n", err);
		return err;
		break;
	default:
		return -EINVAL;
	}

	PDEBUG ("udsl_atm_send unsuccessfull\n");
	return 0;
      nomem:
	vcc->pop (vcc, skb);
	return -ENOMEM;
}


/***************************************************************************
*
* SAR driver entries
*
****************************************************************************/

static int udsl_atm_open (struct atm_vcc *vcc, short vpi, int vci)
{
	struct udsl_atm_dev_data *dev_data;
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
	dev_data = kmalloc (sizeof (struct udsl_atm_dev_data), GFP_KERNEL);
	if (!dev_data)
		return -ENOMEM;

	dev_data->atmsar_vcc =
	    atmsar_open (&(instance->atmsar_vcc_list), vcc, ATMSAR_TYPE_AAL5, vpi, vci, 0, 0,
			 ATMSAR_USE_53BYTE_CELL | ATMSAR_SET_PTI);
	if (!dev_data->atmsar_vcc) {
		kfree (dev_data);
		return -ENOMEM;	/* this is the only reason atmsar_open can fail... */
	}

	vcc->vpi = vpi;
	vcc->vci = vci;
	set_bit (ATM_VF_ADDR, &vcc->flags);
	set_bit (ATM_VF_PARTIAL, &vcc->flags);
	set_bit (ATM_VF_READY, &vcc->flags);
	vcc->dev_data = dev_data;
	vcc->alloc_tx = udsl_atm_alloc_tx;

	dev_data->atmsar_vcc->mtu = UDSL_MAX_AAL5_MRU;

	if (instance->firmware_loaded)
		udsl_fire_receivers (instance);

	PDEBUG ("udsl_atm_open successfull\n");
	return 0;
}

static void udsl_atm_close (struct atm_vcc *vcc)
{
	struct udsl_atm_dev_data *dev_data = vcc->dev_data;
	struct udsl_instance_data *instance = vcc->dev->dev_data;

	PDEBUG ("udsl_atm_close called\n");

	if (!dev_data || !instance) {
		PDEBUG ("NULL data!\n");
		return;
	}

	/* freeing resources */
	/* cancel all sends on this vcc */
	udsl_usb_cancelsends (instance, vcc);

	atmsar_close (&(instance->atmsar_vcc_list), dev_data->atmsar_vcc);
	kfree (dev_data);
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

/***************************************************************************
*
* usb data functions
*
****************************************************************************/

struct udsl_cb {
	struct atm_vcc *vcc;
};

static void udsl_usb_send_data_complete (struct urb *urb, struct pt_regs *regs)
{
	struct udsl_usb_send_data_context *ctx = urb->context;
	struct udsl_instance_data *instance = ctx->instance;
	int err;

	PDEBUG ("udsl_usb_send_data_completion (vcc = %p, skb = %p, status %d)\n", ctx->vcc,
		ctx->skb, urb->status);

	ctx->vcc->pop (ctx->vcc, ctx->skb);

	if (!(ctx->skb = skb_dequeue (&(instance->sndqueue))))
		return;

	/* submit next skb */
	ctx->vcc = ((struct udsl_cb *) (ctx->skb->cb))->vcc;
	usb_fill_bulk_urb (urb,
		       instance->usb_dev,
		       usb_sndbulkpipe (instance->usb_dev, UDSL_ENDPOINT_DATA_OUT),
		       (unsigned char *) ctx->skb->data,
		       ctx->skb->len, udsl_usb_send_data_complete, ctx);

	err = usb_submit_urb (urb, GFP_ATOMIC);

	PDEBUG ("udsl_usb_send_data_completion (send packet %p with length %d), retval = %d\n",
		ctx->skb, ctx->skb->len, err);
}

static int udsl_usb_cancelsends (struct udsl_instance_data *instance, struct atm_vcc *vcc)
{
	int i;

	for (i = 0; i < UDSL_NUMBER_SND_URBS; i++) {
		if (!instance->send_ctx[i].skb)
			continue;
		if (instance->send_ctx[i].vcc == vcc) {
			usb_unlink_urb (instance->send_ctx[i].urb);
			usb_free_urb (instance->send_ctx[i].urb);
			instance->send_ctx[i].vcc->pop (instance->send_ctx[i].vcc,
							instance->send_ctx[i].skb);
			instance->send_ctx[i].skb = NULL;
		}
	}

	return 0;
}

/**** send ******/
static int udsl_usb_send_data (struct udsl_instance_data *instance, struct atm_vcc *vcc,
			struct sk_buff *skb)
{
	int err, i;
	struct urb *urb;
	unsigned long flags;

	PDEBUG ("udsl_usb_send_data entered, sending packet %p with length %d\n", skb, skb->len);

	PACKETDEBUG (skb->data, skb->len);

	spin_lock_irqsave (&instance->sndqueue.lock, flags);
	((struct udsl_cb *) skb->cb)->vcc = vcc;

	/* we are already queueing */
	if (!skb_queue_empty (&instance->sndqueue)) {
		__skb_queue_tail (&instance->sndqueue, skb);
		spin_unlock_irqrestore (&instance->sndqueue.lock, flags);
		PDEBUG ("udsl_usb_send_data: already queing, skb (0x%p) queued\n", skb);
		return 0;
	}

	for (i = 0; i < UDSL_NUMBER_SND_URBS; i++)
		if (instance->send_ctx[i].skb == NULL)
			break;

	/* we must start queueing */
	if (i == UDSL_NUMBER_SND_URBS) {
		__skb_queue_tail (&instance->sndqueue, skb);
		spin_unlock_irqrestore (&instance->sndqueue.lock, flags);
		PDEBUG ("udsl_usb_send_data: skb (0x%p) queued\n", skb);
		return 0;
	}

	/* init context */
	urb = instance->send_ctx[i].urb;
	instance->send_ctx[i].skb = skb;
	instance->send_ctx[i].vcc = vcc;
	instance->send_ctx[i].instance = instance;

	spin_unlock_irqrestore (&instance->sndqueue.lock, flags);

	/* submit packet */
	usb_fill_bulk_urb (urb,
		       instance->usb_dev,
		       usb_sndbulkpipe (instance->usb_dev, UDSL_ENDPOINT_DATA_OUT),
		       (unsigned char *) skb->data,
		       skb->len,
		       udsl_usb_send_data_complete, &(instance->send_ctx[i])
	    );

	err = usb_submit_urb (urb, GFP_KERNEL);

	if (err != 0)
		skb_unlink (skb);

	PDEBUG ("udsl_usb_send_data done (retval = %d)\n", err);
	return err;
}


/***************************************************************************
*
* usb driver entries
*
****************************************************************************/

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
			usb_set_interface (instance->usb_dev, 1, 2);
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
	int i, err;

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
		err = -ENOMEM;
		goto fail_instance;
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

	/* receive urb init */
	for (i = 0; i < UDSL_NUMBER_RCV_URBS; i++) {
		struct udsl_receiver *rcv = &(instance->all_receivers[i]);

		if (!(rcv->skb = dev_alloc_skb (UDSL_RECEIVE_BUFFER_SIZE))) {
			PDEBUG ("No memory for skb %d!\n", i);
			err = -ENOMEM;
			goto fail_urbs;
		}

		if (!(rcv->urb = usb_alloc_urb (0, GFP_KERNEL))) {
			PDEBUG ("No memory for receive urb %d!\n", i);
			err = -ENOMEM;
			goto fail_urbs;
		}

		rcv->instance = instance;

		list_add (&rcv->list, &instance->spare_receivers);

		PDEBUG ("skb->truesize = %d (asked for %d)\n", rcv->skb->truesize, UDSL_RECEIVE_BUFFER_SIZE);
	}

	for (i = 0; i < UDSL_NUMBER_SND_URBS; i++) {
		struct udsl_usb_send_data_context *snd = &(instance->send_ctx[i]);

		if (!(snd->urb = usb_alloc_urb (0, GFP_KERNEL))) {
			PDEBUG ("No memory for send urb %d!\n", i);
			err = -ENOMEM;
			goto fail_urbs;
		}

		snd->instance = instance;
	}

	/* atm init */
	if (!(instance->atm_dev = atm_dev_register (udsl_driver_name, &udsl_atm_devops, -1, 0))) {
		PDEBUG ("failed to register ATM device!\n");
		err = -ENOMEM;
		goto fail_atm;
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

fail_atm:
fail_urbs:
	for (i = 0; i < UDSL_NUMBER_SND_URBS; i++) {
		struct udsl_usb_send_data_context *snd = &(instance->send_ctx[i]);

		if (snd->urb)
			usb_free_urb (snd->urb);
	}

	for (i = 0; i < UDSL_NUMBER_RCV_URBS; i++) {
		struct udsl_receiver *rcv = &(instance->all_receivers[i]);

		if (rcv->skb)
			kfree_skb (rcv->skb);

		if (rcv->urb)
			usb_free_urb (rcv->urb);
	}
	kfree (instance);
fail_instance:
	return err;
}

static void udsl_usb_disconnect (struct usb_interface *intf)
{
	struct udsl_instance_data *instance = usb_get_intfdata (intf);
	struct list_head *pos;
	unsigned long flags;
	unsigned int count = 0;
	int i;

	PDEBUG ("disconnecting\n");

	usb_set_intfdata (intf, NULL);

	if (!instance) {
		PDEBUG ("NULL instance!\n");
		return;
	}

	tasklet_disable (&instance->receive_tasklet);

	down (&instance->serialize); /* vs udsl_fire_receivers */
	/* no need to take the spinlock - receive_tasklet is not running */
	list_for_each (pos, &instance->spare_receivers)
		if (++count > UDSL_NUMBER_RCV_URBS)
			panic (__FILE__ ": memory corruption detected at line %d!\n", __LINE__);
	INIT_LIST_HEAD (&instance->spare_receivers);
	up (&instance->serialize);

	PDEBUG ("udsl_usb_disconnect: flushed %u spare receivers\n", count);

	count = UDSL_NUMBER_RCV_URBS - count;

	for (i = 0; i < UDSL_NUMBER_RCV_URBS; i++)
		usb_unlink_urb (instance->all_receivers[i].urb);

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

		/* not all urbs completed */
		yield ();
	} while (1);

	PDEBUG ("udsl_usb_disconnect: flushing %u completed receivers\n", count);
	/* no need to take the spinlock - no completion handlers running */
	INIT_LIST_HEAD (&instance->completed_receivers);

	tasklet_enable (&instance->receive_tasklet);
	tasklet_kill (&instance->receive_tasklet);

	PDEBUG ("udsl_usb_disconnect: freeing receivers\n");
	for (i = 0; i < UDSL_NUMBER_RCV_URBS; i++) {
		struct udsl_receiver *rcv = &(instance->all_receivers[i]);

		usb_free_urb (rcv->urb);
		kfree_skb (rcv->skb);
	}

	for (i = 0; i < UDSL_NUMBER_SND_URBS; i++) {
		struct udsl_usb_send_data_context *ctx = &(instance->send_ctx[i]);

		usb_unlink_urb (ctx->urb);

		if (ctx->skb)
			ctx->vcc->pop (ctx->vcc, ctx->skb);
		ctx->skb = NULL;

		usb_free_urb (ctx->urb);

	}


	/* removing atm device */
	if (instance->atm_dev)
		udsl_atm_stopdevice (instance);

	kfree (instance);
}


/***************************************************************************
*
* Driver Init
*
****************************************************************************/

static int __init udsl_usb_init (void)
{
	PDEBUG ("udsl_usb_init: driver version " DRIVER_VERSION "\n");

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
