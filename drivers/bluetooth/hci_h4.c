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
 * BlueZ HCI UART(H4) protocol.
 *
 * $Id: hci_h4.c,v 1.2 2002/04/17 17:37:20 maxk Exp $    
 */
#define VERSION "1.1"

#include <linux/config.h>
#include <linux/module.h>

#include <linux/version.h>
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/poll.h>

#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/signal.h>
#include <linux/ioctl.h>
#include <linux/skbuff.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include "hci_uart.h"
#include "hci_h4.h"

#ifndef HCI_UART_DEBUG
#undef  BT_DBG
#define BT_DBG( A... )
#undef  BT_DMP
#define BT_DMP( A... )
#endif

/* Initialize protocol */
static int h4_open(struct n_hci *n_hci)
{
	struct h4_struct *h4;
	
	BT_DBG("n_hci %p", n_hci);
	
	h4 = kmalloc(sizeof(*h4), GFP_ATOMIC);
	if (!h4)
		return -ENOMEM;
	memset(h4, 0, sizeof(*h4));

	n_hci->priv = h4;
	return 0;
}

/* Flush protocol data */
static int h4_flush(struct n_hci *n_hci)
{
	BT_DBG("n_hci %p", n_hci);
	return 0;
}

/* Close protocol */
static int h4_close(struct n_hci *n_hci)
{
	struct h4_struct *h4 = n_hci->priv;
	n_hci->priv = NULL;

	BT_DBG("n_hci %p", n_hci);

	if (h4->rx_skb)
		kfree_skb(h4->rx_skb);

	kfree(h4);
	return 0;
}

/* Send data */
static int h4_send(struct n_hci *n_hci, void *data, int len)
{
	struct tty_struct *tty = n_hci->tty;
	
	BT_DBG("n_hci %p len %d", n_hci, len);

	/* Send frame to TTY driver */
	tty->flags |= (1 << TTY_DO_WRITE_WAKEUP);
	return tty->driver.write(tty, 0, data, len);
}

/* Init frame before queueing (padding, crc, etc) */
static struct sk_buff* h4_preq(struct n_hci *n_hci, struct sk_buff *skb)
{
	BT_DBG("n_hci %p skb %p", n_hci, skb);

	/* Prepend skb with frame type */
	memcpy(skb_push(skb, 1), &skb->pkt_type, 1);
	return skb;
}

static inline int h4_check_data_len(struct h4_struct *h4, int len)
{
	register int room = skb_tailroom(h4->rx_skb);

	BT_DBG("len %d room %d", len, room);
	if (!len) {
		BT_DMP(h4->rx_skb->data, h4->rx_skb->len);
		hci_recv_frame(h4->rx_skb);
	} else if (len > room) {
		BT_ERR("Data length is to large");
		kfree_skb(h4->rx_skb);
	} else {
		h4->rx_state = H4_W4_DATA;
		h4->rx_count = len;
		return len;
	}

	h4->rx_state = H4_W4_PACKET_TYPE;
	h4->rx_skb   = NULL;
	h4->rx_count = 0;
	return 0;
}

/* Recv data */
static int h4_recv(struct n_hci *n_hci, void *data, int count)
{
	struct h4_struct *h4 = n_hci->priv;
	register char *ptr;
	hci_event_hdr *eh;
	hci_acl_hdr   *ah;
	hci_sco_hdr   *sh;
	register int len, type, dlen;

	BT_DBG("n_hci %p count %d rx_state %ld rx_count %ld", n_hci, count, h4->rx_state, h4->rx_count);

	ptr = data;
	while (count) {
		if (h4->rx_count) {
			len = MIN(h4->rx_count, count);
			memcpy(skb_put(h4->rx_skb, len), ptr, len);
			h4->rx_count -= len; count -= len; ptr += len;

			if (h4->rx_count)
				continue;

			switch (h4->rx_state) {
			case H4_W4_DATA:
				BT_DBG("Complete data");

				BT_DMP(h4->rx_skb->data, h4->rx_skb->len);

				hci_recv_frame(h4->rx_skb);

				h4->rx_state = H4_W4_PACKET_TYPE;
				h4->rx_skb = NULL;
				continue;

			case H4_W4_EVENT_HDR:
				eh = (hci_event_hdr *) h4->rx_skb->data;

				BT_DBG("Event header: evt 0x%2.2x plen %d", eh->evt, eh->plen);

				h4_check_data_len(h4, eh->plen);
				continue;

			case H4_W4_ACL_HDR:
				ah = (hci_acl_hdr *) h4->rx_skb->data;
				dlen = __le16_to_cpu(ah->dlen);

				BT_DBG("ACL header: dlen %d", dlen);

				h4_check_data_len(h4, dlen);
				continue;

			case H4_W4_SCO_HDR:
				sh = (hci_sco_hdr *) h4->rx_skb->data;

				BT_DBG("SCO header: dlen %d", sh->dlen);

				h4_check_data_len(h4, sh->dlen);
				continue;
			};
		}

		/* H4_W4_PACKET_TYPE */
		switch (*ptr) {
		case HCI_EVENT_PKT:
			BT_DBG("Event packet");
			h4->rx_state = H4_W4_EVENT_HDR;
			h4->rx_count = HCI_EVENT_HDR_SIZE;
			type = HCI_EVENT_PKT;
			break;

		case HCI_ACLDATA_PKT:
			BT_DBG("ACL packet");
			h4->rx_state = H4_W4_ACL_HDR;
			h4->rx_count = HCI_ACL_HDR_SIZE;
			type = HCI_ACLDATA_PKT;
			break;

		case HCI_SCODATA_PKT:
			BT_DBG("SCO packet");
			h4->rx_state = H4_W4_SCO_HDR;
			h4->rx_count = HCI_SCO_HDR_SIZE;
			type = HCI_SCODATA_PKT;
			break;

		default:
			BT_ERR("Unknown HCI packet type %2.2x", (__u8)*ptr);
			n_hci->hdev.stat.err_rx++;
			ptr++; count--;
			continue;
		};
		ptr++; count--;

		/* Allocate packet */
		h4->rx_skb = bluez_skb_alloc(HCI_MAX_FRAME_SIZE, GFP_ATOMIC);
		if (!h4->rx_skb) {
			BT_ERR("Can't allocate mem for new packet");
			h4->rx_state = H4_W4_PACKET_TYPE;
			h4->rx_count = 0;
			return 0;
		}
		h4->rx_skb->dev = (void *) &n_hci->hdev;
		h4->rx_skb->pkt_type = type;
	}
	return count;
}

static struct hci_uart_proto h4p = {
	id:    HCI_UART_H4,
	open:  h4_open,
	close: h4_close,
	send:  h4_send,
	recv:  h4_recv,
	preq:  h4_preq,
	flush: h4_flush,
};
	      
int h4_init(void)
{
	return hci_uart_register_proto(&h4p);
}

int h4_deinit(void)
{
	return hci_uart_unregister_proto(&h4p);
}
