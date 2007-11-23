/*
 * NET		An implementation of the IEEE 802.2 LLC protocol for the
 *		LINUX operating system.  LLC is implemented as a set of 
 *		state machines and callbacks for higher networking layers.
 *
 *		llc_sendpdu(), llc_sendipdu(), resend() + queue handling code
 *
 *		Written by Tim Alpaerts, Tim_Alpaerts@toyota-motor-europe.com
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	Changes
 *		Alan Cox	:	Chainsawed into Linux format, style
 *					Added llc_ to function names
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <net/p8022.h>
#include <linux/stat.h>
#include <asm/byteorder.h>
#include <net/llc_frame.h>
#include <net/llc.h>

static unsigned char cntl_byte_encode[] = 
{
	0x00,   /* I_CMD */
	0x01,   /* RR_CMD */
	0x05,   /* RNR_CMD */
	0x09,   /* REJ_CMD */
	0x43,   /* DISC_CMD */
	0x7F,   /* SABME_CMD */
	0x00,   /* I_RSP */
	0x01,   /* RR_RSP */
	0x05,   /* RNR_RSP */
	0x09,   /* REJ_RSP */
	0x63,   /* UA_RSP */
	0x0F,   /* DM_RSP */
	0x87,   /* FRMR_RSP */
	0xFF,   /* BAD_FRAME */
	0x03,   /* UI_CMD */
	0xBF,   /* XID_CMD */
	0xE3,   /* TEST_CMD */
	0xBF,   /* XID_RSP */
	0xE3    /* TEST_RSP */
};

static unsigned char fr_length_encode[] = 
{
	0x04,   /* I_CMD */
	0x04,   /* RR_CMD */
	0x04,   /* RNR_CMD */
	0x04,   /* REJ_CMD */
	0x03,   /* DISC_CMD */
	0x03,   /* SABME_CMD */
	0x04,   /* I_RSP */
	0x04,   /* RR_RSP */
	0x04,   /* RNR_RSP */
	0x04,   /* REJ_RSP */
	0x03,   /* UA_RSP */
	0x03,   /* DM_RSP */
	0x03,   /* FRMR_RSP */
	0x00,   /* BAD_FRAME */
	0x03,   /* UI_CMD */
	0x03,   /* XID_CMD */
	0x03,   /* TEST_CMD */
	0x03,   /* XID_RSP */
	0x03    /* TEST_RSP */
};

static unsigned char cr_bit_encode[] = {
	0x00,   /* I_CMD */
	0x00,   /* RR_CMD */
	0x00,   /* RNR_CMD */
	0x00,   /* REJ_CMD */
	0x00,   /* DISC_CMD */
	0x00,   /* SABME_CMD */
	0x01,   /* I_RSP */
	0x01,   /* RR_RSP */
	0x01,   /* RNR_RSP */
	0x01,   /* REJ_RSP */
	0x01,   /* UA_RSP */
	0x01,   /* DM_RSP */
	0x01,   /* FRMR_RSP */
	0x00,   /* BAD_FRAME */
	0x00,   /* UI_CMD */
	0x00,   /* XID_CMD */
	0x00,   /* TEST_CMD */
	0x01,   /* XID_RSP */
	0x01    /* TEST_RSP */
};

/*
 *	Sendpdu() constructs an output frame in a new skb and
 *	gives it to the MAC layer for transmision.
 *	This function is not used to send I pdus.
 *	No queues are updated here, nothing is saved for retransmission.
 *
 *	Parameter pf controls both the poll/final bit and dsap
 *	fields in the output pdu. 
 *	The dsap trick was needed to implement XID_CMD send with
 *	zero dsap field as described in doc 6.6 item 1 of enum.  
 */

void llc_sendpdu(llcptr lp, char type, char pf, int data_len, char *pdu_data)
{
	frameptr fr;                /* ptr to output pdu buffer */
	unsigned short int fl;      /* frame length == 802.3 "length" value */
	struct sk_buff *skb;

	fl = data_len + fr_length_encode[(int)type];
	skb = alloc_skb(16 + fl, GFP_ATOMIC); 
	if (skb != NULL)
	{
		skb->dev = lp->dev;	
	        skb_reserve(skb, 16);
		fr = (frameptr) skb_put(skb, fl);
		memset(fr, 0, fl);
                /*
                 *	Construct 802.2 header 
                 */
		if (pf & 0x02) 
			fr->pdu_hdr.dsap = 0;
		else
			fr->pdu_hdr.dsap = lp->remote_sap;
		fr->pdu_hdr.ssap = lp->local_sap + cr_bit_encode[(int)type];
		fr->pdu_cntl.byte1 = cntl_byte_encode[(int)type];
		/* 
		 *	Fill in pflag and seq nbrs: 
		 */
		if (IS_SFRAME(fr)) 
		{
		  	/* case S-frames */
			if (pf & 0x01) 
				fr->i_hdr.i_pflag = 1;
			fr->i_hdr.nr = lp->vr;
		}
		else
		{
			/* case U frames */ 
			if (pf & 0x01) 
				fr->u_hdr.u_pflag = 1;
		}
                       
		if (data_len > 0) 
		{ 			/* append data if any  */ 
			if (IS_UFRAME(fr)) 
			{
				memcpy(fr->u_hdr.u_info, pdu_data, data_len);
			}
			else 
			{
				memcpy(fr->i_hdr.is_info, pdu_data, data_len);
			}
		}
		lp->dev->hard_header(skb, lp->dev, ETH_P_802_3,
			 lp->remote_mac, NULL, fl);
		skb->arp = 1;
		skb->free = 1;
		dev_queue_xmit(skb, lp->dev, SOPRI_NORMAL);
	}
	else
		printk(KERN_DEBUG "cl2llc: skb_alloc() in llc_sendpdu() failed\n");     
}

void llc_xid_request(llcptr lp, char opt, int ll, char * data)
{
	llc_sendpdu(lp, XID_CMD, opt, ll, data); 
}

void llc_test_request(llcptr lp, int ll, char * data)
{
	llc_sendpdu(lp, TEST_CMD, 0, ll, data); 
}

void llc_unit_data_request(llcptr lp, int ll, char * data)
{
	llc_sendpdu(lp, UI_CMD, 0, ll, data); 
}


/*
 *	llc_sendipdu() Completes an I pdu in an existing skb and gives it
 *	to the MAC layer for transmision.
 *	Parameter "type" must be either I_CMD or I_RSP.
 *	The skb is not freed after xmit, it is kept in case a retransmission
 *	is requested. If needed it can be picked up again from the rtq.
 */

void llc_sendipdu(llcptr lp, char type, char pf, struct sk_buff *skb)
{
	frameptr fr;                /* ptr to output pdu buffer */

	fr = (frameptr) skb->data;

	fr->pdu_hdr.dsap = lp->remote_sap;
	fr->pdu_hdr.ssap = lp->local_sap + cr_bit_encode[(int)type];
	fr->pdu_cntl.byte1 = cntl_byte_encode[(int)type];
			
	if (pf)
		fr->i_hdr.i_pflag = 1; /* p/f and seq numbers */  
	fr->i_hdr.nr = lp->vr;
	fr->i_hdr.ns = lp->vs;
	lp->vs++;
	if (lp->vs > 127) 
		lp->vs = 0;
	lp->dev->hard_header(skb, lp->dev, ETH_P_802_3,
		lp->remote_mac, NULL, skb->len);
	skb->arp = 1;
	skb->free = 0;              /* thanks, Alan */
	ADD_TO_RTQ(skb);		/* add skb to the retransmit queue */
	dev_queue_xmit(skb, lp->dev, SOPRI_NORMAL);
}


/*
 *	Resend_ipdu() will resend the pdus in the retransmit queue (rtq)
 *	the return value is the number of pdus resend.
 *	ack_nr is N(R) of 1st pdu to resent.
 *	Type is I_CMD or I_RSP for 1st pdu resent.
 *	p is p/f flag 0 or 1 for 1st pdu resent.
 *	All subsequent pdus will be sent as I_CMDs with p/f set to 0
 */ 

int llc_resend_ipdu(llcptr lp, unsigned char ack_nr, unsigned char type, char p)
{
	struct sk_buff *skb;
	int resend_count;
	frameptr fr;

	resend_count = 0;
	skb = lp->rtq_front;

	while(skb != NULL)
	{
		/* 
		 *	Should not occur: 
		 */
		 
		if (skb_device_locked(skb)) 
			return resend_count;
		
		fr = (frameptr) (skb->data + lp->dev->hard_header_len);
		if (resend_count == 0) 
		{
			/* 
			 *	Resending 1st pdu: 
			 */

			if (p) 
				fr->i_hdr.i_pflag = 1;
			else
				fr->i_hdr.i_pflag = 0;
            
			if (type == I_CMD)           
				fr->pdu_hdr.ssap = fr->pdu_hdr.ssap & 0xfe;
			else
				fr->pdu_hdr.ssap = fr->pdu_hdr.ssap | 0x01;
		}
	        else
	        {
	        	/*
	        	 *	Resending pdu 2...n 
	        	 */

			fr->pdu_hdr.ssap = fr->pdu_hdr.ssap & 0xfe;
			fr->i_hdr.i_pflag = 0;
		}
		fr->i_hdr.nr = lp->vr;
		fr->i_hdr.ns = lp->vs;
		lp->vs++;
		if (lp->vs > 127) 
			lp->vs = 0;
		skb->arp = 1;
		skb->free = 0;
		dev_queue_xmit(skb, lp->dev, SOPRI_NORMAL);
		resend_count++;
		skb = skb->link3;
	}
	return resend_count;
}

/* ************** internal queue management code ****************** */


/*
 *	Add_to_queue() adds an skb at back of an I-frame queue.
 *	this function is used for both atq and rtq.
 *	the front and back pointers identify the queue being edited.
 *	this function is called with macros ADD_TO_RTQ() and ADD_TO_ATQ() .
 */

void llc_add_to_queue(struct sk_buff *skb,
	struct sk_buff **front, struct sk_buff **back) 
{
	struct sk_buff *t;

	skb->link3 = NULL;		/* there is no more recent skb */ 
	t = *back;			/* save current back ptr */
	*back = skb;
	if (t != NULL) 
		t->link3 = skb;
	if (*front == NULL) 
		*front = *back;
}


/*
 *	Remove one skb from the front of the awaiting transmit queue
 *	(this is the skb longest on the queue) and return a pointer to 
 *	that skb. 
 */ 

struct sk_buff *llc_pull_from_atq(llcptr lp) 
{
	struct sk_buff *t;

	if (lp->atq_front == NULL) 
		return NULL;     /* empty queue */

	t = lp->atq_front;
	lp->atq_front = t->link3;
	if (lp->atq_front == NULL) 
	{
		lp->atq_back = lp->atq_front;
	}
	return t;
}
 
/*
 *	Free_acknowledged_skbs(), remove from retransmit queue (rtq)
 *	and free all skbs with an N(S) chronologicaly before 'pdu_ack'.
 *	The return value is the number of pdus acknowledged.
 */
 
int llc_free_acknowledged_skbs(llcptr lp, unsigned char pdu_ack)
{
	struct sk_buff *pp;
	frameptr fr; 
	int ack_count;
	unsigned char ack; 	/* N(S) of most recently ack'ed pdu */
	unsigned char ns_save; 

	if (pdu_ack > 0) 
		ack = pdu_ack -1;
	else 
		ack = 127;

	ack_count = 0;
	pp = lp->rtq_front; 
	while (pp != NULL)
	{
		/* 
		 *	Locate skb with N(S) == ack 
		 */
		lp->rtq_front = pp->link3;
		fr = (frameptr) (pp->data + lp->dev->hard_header_len);
		ns_save = fr->i_hdr.ns;
		if (skb_device_locked(pp)) 
			return ack_count;

		kfree_skb(pp, FREE_WRITE);
		ack_count++;

		if (ns_save == ack) 
			break;  
		pp = lp->rtq_front;   
	}
	if (pp == NULL)			/* if rtq empty now */ 
		lp->rtq_back = NULL;		/* correct back pointer */

	return ack_count; 
}

