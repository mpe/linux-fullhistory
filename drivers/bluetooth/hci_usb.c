/* 
   BlueZ - Bluetooth protocol stack for Linux
   Copyright (C) 2000-2001 Qualcomm Incorporated

   Written 2000,2001 by Maxim Krasnyansky <maxk@qualcomm.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 as
   published by the Free Software Foundation;

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS.
   IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) AND AUTHOR(S) BE LIABLE FOR ANY
   CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES 
   WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN 
   ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF 
   OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

   ALL LIABILITY, INCLUDING LIABILITY FOR INFRINGEMENT OF ANY PATENTS, 
   COPYRIGHTS, TRADEMARKS OR OTHER RIGHTS, RELATING TO USE OF THIS 
   SOFTWARE IS DISCLAIMED.
*/

/*
 * BlueZ HCI USB driver.
 * Based on original USB Bluetooth driver for Linux kernel
 *    Copyright (c) 2000 Greg Kroah-Hartman        <greg@kroah.com>
 *    Copyright (c) 2000 Mark Douglas Corner       <mcorner@umich.edu>
 *
 * $Id: hci_usb.c,v 1.6 2002/04/17 17:37:20 maxk Exp $    
 */
#define VERSION "2.0"

#include <linux/config.h>
#include <linux/module.h>

#define __KERNEL_SYSCALLS__

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/unistd.h>
#include <linux/types.h>
#include <linux/interrupt.h>

#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/skbuff.h>
#include <linux/kmod.h>

#include <linux/usb.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include "hci_usb.h"

#define HCI_MAX_PENDING (HCI_MAX_BULK_RX + HCI_MAX_BULK_TX + 1)

#ifndef HCI_USB_DEBUG
#undef  BT_DBG
#define BT_DBG( A... )
#undef  BT_DMP
#define BT_DMP( A... )
#endif

#ifndef CONFIG_BLUEZ_USB_ZERO_PACKET
#undef  USB_ZERO_PACKET
#define USB_ZERO_PACKET 0
#endif

static struct usb_driver hci_usb_driver; 

static struct usb_device_id usb_bluetooth_ids [] = {
	/* Generic Bluetooth USB device */
	{ USB_DEVICE_INFO(HCI_DEV_CLASS, HCI_DEV_SUBCLASS, HCI_DEV_PROTOCOL) },

	/* Ericsson with non-standard id */
	{ USB_DEVICE(0x0bdb, 0x1002) },

	{ }	/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, usb_bluetooth_ids);

static void hci_usb_interrupt(struct urb *urb);
static void hci_usb_rx_complete(struct urb *urb);
static void hci_usb_tx_complete(struct urb *urb);

static struct urb *hci_usb_get_completed(struct hci_usb *husb)
{
	struct sk_buff *skb;
	struct urb *urb = NULL;

	skb = skb_dequeue(&husb->completed_q);
	if (skb) {
		urb = ((struct hci_usb_scb *) skb->cb)->urb;
		kfree_skb(skb);
	}

	BT_DBG("%s urb %p", husb->hdev.name, urb);
	return urb;
}

static int hci_usb_enable_intr(struct hci_usb *husb)
{
	struct urb *urb;
	int pipe, size;
	void *buf;

	BT_DBG("%s", husb->hdev.name);

 	if (!(urb = usb_alloc_urb(0, GFP_KERNEL)))
		return -ENOMEM;

	if (!(buf = kmalloc(HCI_MAX_EVENT_SIZE, GFP_KERNEL))) {
		usb_free_urb(urb);
		return -ENOMEM;
	}

	husb->intr_urb = urb;
	
        pipe = usb_rcvintpipe(husb->udev, husb->intr_ep);
        size = usb_maxpacket(husb->udev, pipe, usb_pipeout(pipe));
	FILL_INT_URB(urb, husb->udev, pipe, buf, size, 
			hci_usb_interrupt, husb, husb->intr_interval);
	
	return usb_submit_urb(urb, GFP_KERNEL);
}

static int hci_usb_disable_intr(struct hci_usb *husb)
{
	struct urb *urb = husb->intr_urb;
	struct sk_buff *skb;

	BT_DBG("%s", husb->hdev.name);

	usb_unlink_urb(urb); usb_free_urb(urb);
	husb->intr_urb = NULL;

	skb = husb->intr_skb;
	if (skb) {
		husb->intr_skb = NULL;
		kfree_skb(skb);
	}

	return 0;
}

static int hci_usb_rx_submit(struct hci_usb *husb, struct urb *urb)
{
	struct hci_usb_scb *scb;
	struct sk_buff *skb;
	int    pipe, size, err;

	if (!urb && !(urb = usb_alloc_urb(0, GFP_ATOMIC)))
		return -ENOMEM;

        size = HCI_MAX_FRAME_SIZE;

	if (!(skb = bluez_skb_alloc(size, GFP_ATOMIC))) {
		usb_free_urb(urb);
		return -ENOMEM;
	}
	
	BT_DBG("%s urb %p", husb->hdev.name, urb);

	skb->dev = (void *) &husb->hdev;
	skb->pkt_type = HCI_ACLDATA_PKT;

	scb = (struct hci_usb_scb *) skb->cb;
	scb->urb = urb;

        pipe = usb_rcvbulkpipe(husb->udev, husb->bulk_in_ep);

        FILL_BULK_URB(urb, husb->udev, pipe, skb->data, size, hci_usb_rx_complete, skb);

	skb_queue_tail(&husb->pending_q, skb);
	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err) {
		BT_ERR("%s bulk rx submit failed urb %p err %d",
				husb->hdev.name, urb, err);
		skb_unlink(skb);
		usb_free_urb(urb);
	}
	return err;
}

/* Initialize device */
static int hci_usb_open(struct hci_dev *hdev)
{
	struct hci_usb *husb = (struct hci_usb *) hdev->driver_data;
	int i, err;
	long flags;

	BT_DBG("%s", hdev->name);

	if (test_and_set_bit(HCI_RUNNING, &hdev->flags))
		return 0;

	write_lock_irqsave(&husb->completion_lock, flags);

	err = hci_usb_enable_intr(husb);
	if (!err) {
		for (i = 0; i < HCI_MAX_BULK_TX; i++)
			hci_usb_rx_submit(husb, NULL);
	} else 
		clear_bit(HCI_RUNNING, &hdev->flags);
		
	write_unlock_irqrestore(&husb->completion_lock, flags);
	return err;
}

/* Reset device */
static int hci_usb_flush(struct hci_dev *hdev)
{
	struct hci_usb *husb = (struct hci_usb *) hdev->driver_data;

	BT_DBG("%s", hdev->name);

	skb_queue_purge(&husb->cmd_q);
	skb_queue_purge(&husb->acl_q);
	return 0;
}

static inline void hci_usb_unlink_urbs(struct hci_usb *husb)
{
	struct sk_buff *skb;
	struct urb *urb;

	BT_DBG("%s", husb->hdev.name);

	while ((skb = skb_dequeue(&husb->pending_q))) {
		urb = ((struct hci_usb_scb *) skb->cb)->urb;
		usb_unlink_urb(urb);
		kfree_skb(skb);
	}

	while ((urb = hci_usb_get_completed(husb)))
		usb_free_urb(urb);
}

/* Close device */
static int hci_usb_close(struct hci_dev *hdev)
{
	struct hci_usb *husb = (struct hci_usb *) hdev->driver_data;
	long flags;
	
	if (!test_and_clear_bit(HCI_RUNNING, &hdev->flags))
		return 0;

	BT_DBG("%s", hdev->name);

	write_lock_irqsave(&husb->completion_lock, flags);
	
	hci_usb_disable_intr(husb);
	hci_usb_unlink_urbs(husb);
	hci_usb_flush(hdev);

	write_unlock_irqrestore(&husb->completion_lock, flags);
	return 0;
}

static inline int hci_usb_send_ctrl(struct hci_usb *husb, struct sk_buff *skb)
{
	struct hci_usb_scb *scb = (void *) skb->cb;
	struct urb *urb = hci_usb_get_completed(husb);
	struct usb_ctrlrequest *cr;
	int pipe, err;

	if (!urb && !(urb = usb_alloc_urb(0, GFP_ATOMIC)))
		return -ENOMEM;

	if (!(cr = kmalloc(sizeof(*cr), GFP_ATOMIC))) {
		usb_free_urb(urb);
		return -ENOMEM;
	}
	
	pipe = usb_sndctrlpipe(husb->udev, 0);

	cr->bRequestType = HCI_CTRL_REQ;
	cr->bRequest = 0;
	cr->wIndex   = 0;
	cr->wValue   = 0;
	cr->wLength  = __cpu_to_le16(skb->len);

	FILL_CONTROL_URB(urb, husb->udev, pipe, (void *) cr,
			skb->data, skb->len, hci_usb_tx_complete, skb);

	BT_DBG("%s urb %p len %d", husb->hdev.name, urb, skb->len);

	scb->urb = urb;

	skb_queue_tail(&husb->pending_q, skb);
	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err) {
		BT_ERR("%s ctrl tx submit failed urb %p err %d", 
				husb->hdev.name, urb, err);
		skb_unlink(skb);
		usb_free_urb(urb); kfree(cr);
	}
	return err;
}

static inline int hci_usb_send_bulk(struct hci_usb *husb, struct sk_buff *skb)
{
	struct hci_usb_scb *scb = (void *) skb->cb;
	struct urb *urb = hci_usb_get_completed(husb);
	int pipe, err;

	if (!urb && !(urb = usb_alloc_urb(0, GFP_ATOMIC)))
		return -ENOMEM;

	pipe = usb_sndbulkpipe(husb->udev, husb->bulk_out_ep);
        
	FILL_BULK_URB(urb, husb->udev, pipe, skb->data, skb->len,
	              hci_usb_tx_complete, skb);
	urb->transfer_flags = USB_ZERO_PACKET;

	BT_DBG("%s urb %p len %d", husb->hdev.name, urb, skb->len);

	scb->urb = urb;

	skb_queue_tail(&husb->pending_q, skb);
	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err) {
		BT_ERR("%s bulk tx submit failed urb %p err %d", 
				husb->hdev.name, urb, err);
		skb_unlink(skb);
		usb_free_urb(urb);
	}
	return err;
}

static void hci_usb_tx_process(struct hci_usb *husb)
{
	struct sk_buff *skb;

	BT_DBG("%s", husb->hdev.name);

	do {
		clear_bit(HCI_USB_TX_WAKEUP, &husb->state);
		
		/* Process ACL queue */
		while (skb_queue_len(&husb->pending_q) < HCI_MAX_PENDING &&
				(skb = skb_dequeue(&husb->acl_q))) {
			if (hci_usb_send_bulk(husb, skb) < 0) {
				skb_queue_head(&husb->acl_q, skb);
				break;
			}
		}

		/* Process command queue */
		if (!test_bit(HCI_USB_CTRL_TX, &husb->state) &&
			(skb = skb_dequeue(&husb->cmd_q)) != NULL) {
			set_bit(HCI_USB_CTRL_TX, &husb->state);
			if (hci_usb_send_ctrl(husb, skb) < 0) {
				skb_queue_head(&husb->cmd_q, skb);
				clear_bit(HCI_USB_CTRL_TX, &husb->state);
			}
		}
	} while(test_bit(HCI_USB_TX_WAKEUP, &husb->state));
}

static inline void hci_usb_tx_wakeup(struct hci_usb *husb)
{
	/* Serialize TX queue processing to avoid data reordering */
	if (!test_and_set_bit(HCI_USB_TX_PROCESS, &husb->state)) {
		hci_usb_tx_process(husb);
		clear_bit(HCI_USB_TX_PROCESS, &husb->state);
	} else
		set_bit(HCI_USB_TX_WAKEUP, &husb->state);
}

/* Send frames from HCI layer */
int hci_usb_send_frame(struct sk_buff *skb)
{
	struct hci_dev *hdev = (struct hci_dev *) skb->dev;
	struct hci_usb *husb;

	if (!hdev) {
		BT_ERR("frame for uknown device (hdev=NULL)");
		return -ENODEV;
	}

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		return -EBUSY;

	husb = (struct hci_usb *) hdev->driver_data;

	BT_DBG("%s type %d len %d", hdev->name, skb->pkt_type, skb->len);

	read_lock(&husb->completion_lock);

	switch (skb->pkt_type) {
	case HCI_COMMAND_PKT:
		skb_queue_tail(&husb->cmd_q, skb);
		hdev->stat.cmd_tx++;
		break;

	case HCI_ACLDATA_PKT:
		skb_queue_tail(&husb->acl_q, skb);
		hdev->stat.acl_tx++;
		break;

	case HCI_SCODATA_PKT:
	default:
		kfree_skb(skb);
		break;
	}
	hci_usb_tx_wakeup(husb);

	read_unlock(&husb->completion_lock);
	return 0;
}

static void hci_usb_interrupt(struct urb *urb)
{
	struct hci_usb *husb = (void *) urb->context;
	struct hci_usb_scb *scb;
	struct sk_buff *skb;
	hci_event_hdr *eh;
	__u8 *data = urb->transfer_buffer;
	int count = urb->actual_length;
	int len = HCI_EVENT_HDR_SIZE;

	BT_DBG("%s urb %p count %d", husb->hdev.name, urb, count);

	if (!test_bit(HCI_RUNNING, &husb->hdev.flags))
		return;

	if (urb->status || !count) {
		BT_DBG("%s intr status %d, count %d", 
				husb->hdev.name, urb->status, count);
		return;
	}

	read_lock(&husb->completion_lock);
	
	husb->hdev.stat.byte_rx += count;

	if (!(skb = husb->intr_skb)) {
		/* Start of the frame */
		if (count < HCI_EVENT_HDR_SIZE)
			goto bad_len;

		eh  = (hci_event_hdr *) data;
		len = eh->plen + HCI_EVENT_HDR_SIZE;

		if (count > len)
			goto bad_len;

		skb = bluez_skb_alloc(len, GFP_ATOMIC);
		if (!skb) {
			BT_ERR("%s no memory for event packet", husb->hdev.name);
			goto done;
		}
		scb = (void *) skb->cb;

		skb->dev = (void *) &husb->hdev;
		skb->pkt_type = HCI_EVENT_PKT;

		husb->intr_skb = skb;
		scb->intr_len  = len;
	} else {
		/* Continuation */
		scb = (void *) skb->cb;
		len = scb->intr_len;
		if (count > len) {
			husb->intr_skb = NULL;
			kfree_skb(skb);
			goto bad_len;
		}
	}

	memcpy(skb_put(skb, count), data, count);
	scb->intr_len -= count;

	if (!scb->intr_len) {
		/* Complete frame */
		husb->intr_skb = NULL;
		hci_recv_frame(skb);
	}

done:
	read_unlock(&husb->completion_lock);
	return;

bad_len:
	BT_ERR("%s bad frame len %d expected %d", husb->hdev.name, count, len);
	husb->hdev.stat.err_rx++;
	read_unlock(&husb->completion_lock);
}

static void hci_usb_tx_complete(struct urb *urb)
{
	struct sk_buff *skb  = (struct sk_buff *) urb->context;
	struct hci_dev *hdev = (struct hci_dev *) skb->dev;
	struct hci_usb *husb = (struct hci_usb *) hdev->driver_data;

	BT_DBG("%s urb %p status %d flags %x", husb->hdev.name, urb,
			urb->status, urb->transfer_flags);

	if (urb->pipe == usb_sndctrlpipe(husb->udev, 0)) {
		kfree(urb->setup_packet);
		clear_bit(HCI_USB_CTRL_TX, &husb->state);
	}

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		return;

	read_lock(&husb->completion_lock);
	
	if (!urb->status)
		husb->hdev.stat.byte_tx += skb->len;
	else
		husb->hdev.stat.err_tx++;

	skb_unlink(skb);
	skb_queue_tail(&husb->completed_q, skb);
	hci_usb_tx_wakeup(husb);
	
	read_unlock(&husb->completion_lock);
	return;
}

static void hci_usb_rx_complete(struct urb *urb)
{
	struct sk_buff *skb  = (struct sk_buff *) urb->context;
	struct hci_dev *hdev = (struct hci_dev *) skb->dev;
	struct hci_usb *husb = (struct hci_usb *) hdev->driver_data;
	int status, count = urb->actual_length;
	hci_acl_hdr *ah;
	int dlen, size;

	BT_DBG("%s urb %p status %d count %d flags %x", husb->hdev.name, urb,
			urb->status, count, urb->transfer_flags);

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		return;

	read_lock(&husb->completion_lock);

	if (urb->status || !count)
		goto resubmit;

	husb->hdev.stat.byte_rx += count;

	ah   = (hci_acl_hdr *) skb->data;
	dlen = __le16_to_cpu(ah->dlen);
	size = HCI_ACL_HDR_SIZE + dlen;

	/* Verify frame len and completeness */
	if (count != size) {
		BT_ERR("%s corrupted ACL packet: count %d, dlen %d",
				husb->hdev.name, count, dlen);
		bluez_dump("hci_usb", skb->data, count);
		husb->hdev.stat.err_rx++;
		goto resubmit;
	}

	skb_unlink(skb);
	skb_put(skb, count);
	hci_recv_frame(skb);

	hci_usb_rx_submit(husb, urb);

	read_unlock(&husb->completion_lock);
	return;
		
resubmit:
	urb->dev = husb->udev;
	status   = usb_submit_urb(urb, GFP_ATOMIC);
	BT_DBG("%s URB resubmit status %d", husb->hdev.name, status);
	read_unlock(&husb->completion_lock);
}

static void hci_usb_destruct(struct hci_dev *hdev)
{
	struct hci_usb *husb;

	if (!hdev) return;

	BT_DBG("%s", hdev->name);

	husb = (struct hci_usb *) hdev->driver_data;
	kfree(husb);

	MOD_DEC_USE_COUNT;
}

#ifdef CONFIG_BLUEZ_USB_FW_LOAD

/* Support for user mode Bluetooth USB firmware loader */

#define FW_LOADER "/sbin/bluefw"
static int errno;

static int hci_usb_fw_exec(void *dev)
{
	char *envp[] = { "HOME=/", "TERM=linux", 
			 "PATH=/sbin:/usr/sbin:/bin:/usr/bin", NULL };
	char *argv[] = { FW_LOADER, dev, NULL };
	int err;

	err = exec_usermodehelper(FW_LOADER, argv, envp);
	if (err)
		BT_ERR("failed to exec %s %s", FW_LOADER, (char *)dev);
	return err;
}

static int hci_usb_fw_load(struct usb_device *udev)
{
	sigset_t tmpsig;
	char dev[16];
	pid_t pid;
	int result;

	/* Check if root fs is mounted */
	if (!current->fs->root) {
		BT_ERR("root fs not mounted");
		return -EPERM;
	}

	sprintf(dev, "%3.3d/%3.3d", udev->bus->busnum, udev->devnum);

	pid = kernel_thread(hci_usb_fw_exec, (void *)dev, 0);
	if (pid < 0) {
		BT_ERR("fork failed, errno %d\n", -pid);
		return pid;
	}

	/* Block signals, everything but SIGKILL/SIGSTOP */
	spin_lock_irq(&current->sig->siglock);
	tmpsig = current->blocked;
	siginitsetinv(&current->blocked, sigmask(SIGKILL) | sigmask(SIGSTOP));
	recalc_sigpending();
	spin_unlock_irq(&current->sig->siglock);

	result = waitpid(pid, NULL, __WCLONE);

	/* Allow signals again */
	spin_lock_irq(&current->sig->siglock);
	current->blocked = tmpsig;
	recalc_sigpending();
	spin_unlock_irq(&current->sig->siglock);

	if (result != pid) {
		BT_ERR("waitpid failed pid %d errno %d\n", pid, -result);
		return -result;
	}
	return 0;
}

#endif /* CONFIG_BLUEZ_USB_FW_LOAD */

static int hci_usb_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct usb_endpoint_descriptor *bulk_out_ep[HCI_MAX_IFACE_NUM];
	struct usb_endpoint_descriptor *isoc_out_ep[HCI_MAX_IFACE_NUM];
	struct usb_endpoint_descriptor *bulk_in_ep[HCI_MAX_IFACE_NUM];
	struct usb_endpoint_descriptor *isoc_in_ep[HCI_MAX_IFACE_NUM];
	struct usb_endpoint_descriptor *intr_in_ep[HCI_MAX_IFACE_NUM];
	struct usb_interface_descriptor *uif;
	struct usb_endpoint_descriptor *ep;
	struct usb_interface *iface, *isoc_iface;
	struct hci_usb *husb;
	struct hci_dev *hdev;
	int i, a, e, size, ifn, isoc_ifnum, isoc_alts;

	BT_DBG("intf %p", intf);

	/* Check number of endpoints */
	if (intf->altsetting[0].bNumEndpoints < 3)
		return -ENODEV;

	MOD_INC_USE_COUNT;

#ifdef CONFIG_BLUEZ_USB_FW_LOAD
	hci_usb_fw_load(udev);
#endif

	memset(bulk_out_ep, 0, sizeof(bulk_out_ep));
	memset(isoc_out_ep, 0, sizeof(isoc_out_ep));
	memset(bulk_in_ep,  0, sizeof(bulk_in_ep));
	memset(isoc_in_ep,  0, sizeof(isoc_in_ep));
	memset(intr_in_ep,  0, sizeof(intr_in_ep));

	size = 0; 
	isoc_iface = NULL;
	isoc_alts  = isoc_ifnum = 0;
	
	/* Find endpoints that we need */

	ifn = MIN(udev->actconfig->bNumInterfaces, HCI_MAX_IFACE_NUM);
	for (i = 0; i < ifn; i++) {
		iface = &udev->actconfig->interface[i];
		for (a = 0; a < iface->num_altsetting; a++) {
			uif = &iface->altsetting[a];
			for (e = 0; e < uif->bNumEndpoints; e++) {
				ep = &uif->endpoint[e];

				switch (ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) {
				case USB_ENDPOINT_XFER_INT:
					if (ep->bEndpointAddress & USB_DIR_IN)
						intr_in_ep[i] = ep;
					break;

				case USB_ENDPOINT_XFER_BULK:
					if (ep->bEndpointAddress & USB_DIR_IN)
						bulk_in_ep[i]  = ep;
					else
						bulk_out_ep[i] = ep;
					break;

				case USB_ENDPOINT_XFER_ISOC:
					if (ep->wMaxPacketSize < size)
						break;
					size = ep->wMaxPacketSize;

					isoc_iface = iface;
					isoc_alts  = a;
					isoc_ifnum = i;

					if (ep->bEndpointAddress & USB_DIR_IN)
						isoc_in_ep[i]  = ep;
					else
						isoc_out_ep[i] = ep;
					break;
				}
			}
		}
	}

	if (!bulk_in_ep[0] || !bulk_out_ep[0] || !intr_in_ep[0]) {
		BT_DBG("Bulk endpoints not found");
		goto done;
	}

	if (!isoc_in_ep[1] || !isoc_out_ep[1]) {
		BT_DBG("Isoc endpoints not found");
		isoc_iface = NULL;
	}

	if (!(husb = kmalloc(sizeof(struct hci_usb), GFP_KERNEL))) {
		BT_ERR("Can't allocate: control structure");
		goto done;
	}

	memset(husb, 0, sizeof(struct hci_usb));

	husb->udev = udev;
	husb->bulk_out_ep = bulk_out_ep[0]->bEndpointAddress;
	husb->bulk_in_ep  = bulk_in_ep[0]->bEndpointAddress;

	husb->intr_ep = intr_in_ep[0]->bEndpointAddress;
	husb->intr_interval = intr_in_ep[0]->bInterval;

	if (isoc_iface) {
		if (usb_set_interface(udev, isoc_ifnum, isoc_alts)) {
			BT_ERR("Can't set isoc interface settings");
			isoc_iface = NULL;
		}
		usb_driver_claim_interface(&hci_usb_driver, isoc_iface, husb);
		husb->isoc_iface  = isoc_iface;

		husb->isoc_in_ep  = isoc_in_ep[1]->bEndpointAddress;
		husb->isoc_out_ep = isoc_in_ep[1]->bEndpointAddress;
	}

	husb->completion_lock = RW_LOCK_UNLOCKED;
	
	skb_queue_head_init(&husb->acl_q);
	skb_queue_head_init(&husb->cmd_q);
	skb_queue_head_init(&husb->pending_q);
	skb_queue_head_init(&husb->completed_q);

	/* Initialize and register HCI device */
	hdev = &husb->hdev;

	hdev->type = HCI_USB;
	hdev->driver_data = husb;

	hdev->open  = hci_usb_open;
	hdev->close = hci_usb_close;
	hdev->flush = hci_usb_flush;
	hdev->send  = hci_usb_send_frame;
	hdev->destruct = hci_usb_destruct;

	if (hci_register_dev(hdev) < 0) {
		BT_ERR("Can't register HCI device");
		goto probe_error;
	}

	dev_set_drvdata(&intf->dev, husb);
	return 0;

probe_error:
	kfree(husb);

done:
	MOD_DEC_USE_COUNT;
	return -EIO;
}

static void hci_usb_disconnect(struct usb_interface *intf)
{
	struct hci_usb *husb = dev_get_drvdata(&intf->dev);
	struct hci_dev *hdev;

	dev_set_drvdata(&intf->dev, NULL);
	if (!husb)
		return;

	hdev = &husb->hdev;
	BT_DBG("%s", hdev->name);

	hci_usb_close(hdev);

	if (husb->isoc_iface)
		usb_driver_release_interface(&hci_usb_driver, husb->isoc_iface);

	if (hci_unregister_dev(hdev) < 0)
		BT_ERR("Can't unregister HCI device %s", hdev->name);
}

static struct usb_driver hci_usb_driver = {
	.name           = "hci_usb",
	.probe          = hci_usb_probe,
	.disconnect     = hci_usb_disconnect,
	.id_table       = usb_bluetooth_ids,
};

int hci_usb_init(void)
{
	int err;

	BT_INFO("BlueZ HCI USB driver ver %s Copyright (C) 2000,2001 Qualcomm Inc",  
		VERSION);
	BT_INFO("Written 2000,2001 by Maxim Krasnyansky <maxk@qualcomm.com>");

	if ((err = usb_register(&hci_usb_driver)) < 0)
		BT_ERR("Failed to register HCI USB driver");

	return err;
}

void hci_usb_cleanup(void)
{
	usb_deregister(&hci_usb_driver);
}

module_init(hci_usb_init);
module_exit(hci_usb_cleanup);

MODULE_AUTHOR("Maxim Krasnyansky <maxk@qualcomm.com>");
MODULE_DESCRIPTION("BlueZ HCI USB driver ver " VERSION);
MODULE_LICENSE("GPL");
