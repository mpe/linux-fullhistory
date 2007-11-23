/*********************************************************************
 *                
 * Filename:      wrapper.c
 * Version:       1.1
 * Description:   SIR wrapper layer
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Mon Aug  4 20:40:53 1997
 * Modified at:   Wed Apr 21 12:45:55 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1998 Dag Brattli <dagb@cs.uit.no>, 
 *     All Rights Reserved.
 *     
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 *
 *     Neither Dag Brattli nor University of Troms� admit liability nor
 *     provide warranty for any of this software. This material is 
 *     provided "AS-IS" and at no charge.
 *
 ********************************************************************/

#include <linux/skbuff.h>
#include <asm/byteorder.h>

#include <net/irda/irda.h>
#include <net/irda/wrapper.h>
#include <net/irda/irtty.h>
#include <net/irda/crc.h>
#include <net/irda/irlap.h>
#include <net/irda/irlap_frame.h>
#include <net/irda/irda_device.h>

inline static int stuff_byte(__u8 byte, __u8 *buf);

/*
 * Function async_wrap (skb, *tx_buff)
 *
 *    Makes a new buffer with wrapping and stuffing, should check that 
 *    we don't get tx buffer overflow.
 */
int async_wrap_skb(struct sk_buff *skb, __u8 *tx_buff, int buffsize)
{
 	int i;
	int n;
	int xbofs;
	union {
		__u16 value;
		__u8 bytes[2];
	} fcs;

	ASSERT(skb != NULL, return 0;);

	/* Initialize variables */
	fcs.value = INIT_FCS;
	n = 0;

	if (skb->len > 2048) {
		DEBUG(0, __FUNCTION__ "Warning size=%d of sk_buff to big!\n", 
		      (int) skb->len);
		return 0;
	}
	
	/*
	 *  Send  XBOF's for required min. turn time and for the negotiated
	 *  additional XBOFS
	 */
	if (((struct irlap_skb_cb *)(skb->cb))->magic != LAP_MAGIC) {
		DEBUG(1, __FUNCTION__ "(), wrong magic in skb!\n");
		xbofs = 10;
	} else
		xbofs = ((struct irlap_skb_cb *)(skb->cb))->xbofs;

#if 0
 	for (i=0; i<xbofs; i++)
  		tx_buff[n++] = XBOF;
#else
	memset(tx_buff+n, XBOF, xbofs);
	n += xbofs;
#endif
	/* Start of packet character BOF */
	tx_buff[n++] = BOF;

	/* Insert frame and calc CRC */
	for (i=0; i < skb->len; i++) {
		/*
		 *  Check for the possibility of tx buffer overflow. We use
		 *  bufsize-5 since the maximum number of bytes that can be 
		 *  transmitted after this point is 5.
		 */
		ASSERT(n < (buffsize-5), return n;);

		n += stuff_byte(skb->data[i], tx_buff+n);
		fcs.value = IR_FCS(fcs.value, skb->data[i]);
	}
	
	/* Insert CRC in little endian format (LSB first) */
	fcs.value = ~fcs.value;
#ifdef __LITTLE_ENDIAN
	n += stuff_byte(fcs.bytes[0], tx_buff+n);
	n += stuff_byte(fcs.bytes[1], tx_buff+n);
#else ifdef __BIG_ENDIAN
	n += stuff_byte(fcs.bytes[1], tx_buff+n);
	n += stuff_byte(fcs.bytes[0], tx_buff+n);
#endif
	tx_buff[n++] = EOF;

#if 0
	{
		int i;
		
		for (i=0;i<n;i++)
			printk("%02x", tx_buff[i]);
		printk("\n");
	}
#endif
	return n;
}

/*
 * Function async_bump (idev)
 *
 *    Got a frame, make a copy of it, and pass it up the stack!
 *
 */
static inline void async_bump(struct irda_device *idev, __u8 *buf, int len)
{
       	struct sk_buff *skb;

	skb = dev_alloc_skb(len+1);
	if (!skb)  {
		idev->stats.rx_dropped++;
		return;
	}

	/*  Align IP header to 20 bytes */
	skb_reserve(skb, 1);
	
        /* Copy data without CRC */
	memcpy(skb_put(skb, len-2), buf, len-2); 
	
	/* 
	 *  Feed it to IrLAP layer 
	 */
	skb->dev = &idev->netdev;
	skb->mac.raw  = skb->data;
	skb->protocol = htons(ETH_P_IRDA);

	netif_rx(skb);
	idev->stats.rx_packets++;
	idev->stats.rx_bytes += skb->len;	
}

/*
 * Function async_unwrap (skb)
 *
 *    Parse and de-stuff frame received from the IR-port
 *
 */
void async_unwrap_char(struct irda_device *idev, __u8 byte) 
{
	/* State machine for receiving frames */	   
	switch (idev->rx_buff.state) {
	case OUTSIDE_FRAME:
		switch(byte) {
		case BOF:
			idev->rx_buff.state = BEGIN_FRAME;
			idev->rx_buff.in_frame = TRUE;
			break;
		case XBOF:
			/* idev->xbofs++; */
			break;
		case EOF:
			irda_device_set_media_busy( idev, TRUE);
			break;
		default:
			break;
		}
		break;
	case BEGIN_FRAME:
		switch (byte) {
		case BOF:
			/* Continue */
			break;
		case CE:
			/* Stuffed byte */
			idev->rx_buff.state = LINK_ESCAPE;
			break;
		case EOF:
			/* Abort frame */
			idev->rx_buff.state = OUTSIDE_FRAME;

			idev->stats.rx_errors++;
			idev->stats.rx_frame_errors++;
			break;
		default:
			/* Got first byte of frame */
			idev->rx_buff.data = idev->rx_buff.head;
			idev->rx_buff.len = 0;

			idev->rx_buff.data[idev->rx_buff.len++] = byte;
			
			idev->rx_buff.fcs = IR_FCS(INIT_FCS, byte);
			idev->rx_buff.state = INSIDE_FRAME;
			break;
		}
		break;
	case LINK_ESCAPE:
		switch (byte) {
		case BOF:
			/* New frame? */
			idev->rx_buff.state = BEGIN_FRAME;
			irda_device_set_media_busy(idev, TRUE);
			break;
		case CE:
			DEBUG(4, "WARNING: State not defined\n");
			break;
		case EOF:
			/* Abort frame */
			idev->rx_buff.state = OUTSIDE_FRAME;
			break;
		default:
			/* 
			 *  Stuffed char, complement bit 5 of byte 
			 *  following CE, IrLAP p.114 
			 */
			byte ^= IR_TRANS;
			if (idev->rx_buff.len < idev->rx_buff.truesize)  {
				idev->rx_buff.data[idev->rx_buff.len++] = byte;
				idev->rx_buff.fcs = IR_FCS(idev->rx_buff.fcs,
							   byte);
				idev->rx_buff.state = INSIDE_FRAME;
			} else {
				DEBUG(1, __FUNCTION__ 
				       "(), Rx buffer overflow, aborting\n");
				idev->rx_buff.state = OUTSIDE_FRAME;
			}
			break;
		}
		break;
	case INSIDE_FRAME:
		switch (byte) {
		case BOF:
			/* New frame? */
			idev->rx_buff.state = BEGIN_FRAME;
			irda_device_set_media_busy(idev, TRUE);
			break;
		case CE:
			/* Stuffed char */
			idev->rx_buff.state = LINK_ESCAPE;
			break;
		case EOF:
			/* End of frame */
			idev->rx_buff.state = OUTSIDE_FRAME;
			idev->rx_buff.in_frame = FALSE;
			
			/* 
			 *  Test FCS and deliver frame if it's good
			 */			
			if (idev->rx_buff.fcs == GOOD_FCS) {
				async_bump(idev, idev->rx_buff.data, 
					   idev->rx_buff.len);
			} else {
				/* Wrong CRC, discard frame!  */
				irda_device_set_media_busy(idev, TRUE); 

				idev->stats.rx_errors++;
				idev->stats.rx_crc_errors++;
			}			
			break;
		default:
			/* Next byte of frame */
			if (idev->rx_buff.len < idev->rx_buff.truesize)  {
				idev->rx_buff.data[idev->rx_buff.len++] = byte;
				idev->rx_buff.fcs = IR_FCS(idev->rx_buff.fcs,
							   byte);
			} else {
				DEBUG(1, __FUNCTION__ 
				      "(), Rx buffer overflow, aborting\n");
				idev->rx_buff.state = OUTSIDE_FRAME;
			}
			break;
		}
		break;
	}
}
 
/*
 * Function stuff_byte (byte, buf)
 *
 *    Byte stuff one single byte and put the result in buffer pointed to by
 *    buf. The buffer must at all times be able to have two bytes inserted.
 * 
 */
inline static int stuff_byte(__u8 byte, __u8 *buf) 
{
	switch (byte) {
	case BOF: /* FALLTHROUGH */
	case EOF: /* FALLTHROUGH */
	case CE:
		/* Insert transparently coded */
		buf[0] = CE;               /* Send link escape */
		buf[1] = byte^IR_TRANS;    /* Complement bit 5 */
		return 2;
		/* break; */
	default:
		 /* Non-special value, no transparency required */
		buf[0] = byte;
		return 1;
		/* break; */
	}
}
	 



