/* $Id: eicon_idi.c,v 1.11 1999/07/25 15:12:03 armin Exp $
 *
 * ISDN lowlevel-module for Eicon.Diehl active cards.
 *        IDI interface 
 *
 * Copyright 1998,99 by Armin Schindler (mac@melware.de)
 * Copyright 1999    Cytronics & Melware (info@melware.de)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. 
 *
 * $Log: eicon_idi.c,v $
 * Revision 1.11  1999/07/25 15:12:03  armin
 * fix of some debug logs.
 * enabled ISA-cards option.
 *
 * Revision 1.10  1999/07/11 17:16:24  armin
 * Bugfixes in queue handling.
 * Added DSP-DTMF decoder functions.
 * Reorganized ack_handler.
 *
 * Revision 1.9  1999/03/29 11:19:42  armin
 * I/O stuff now in seperate file (eicon_io.c)
 * Old ISA type cards (S,SX,SCOM,Quadro,S2M) implemented.
 *
 * Revision 1.8  1999/03/02 12:37:43  armin
 * Added some important checks.
 * Analog Modem with DSP.
 * Channels will be added to Link-Level after loading firmware.
 *
 * Revision 1.7  1999/02/03 18:34:35  armin
 * Channel selection for outgoing calls w/o CHI.
 * Added channel # in debug messages.
 * L2 Transparent should work with 800 byte/packet now.
 *
 * Revision 1.6  1999/01/26 07:18:59  armin
 * Bug with wrong added CPN fixed.
 *
 * Revision 1.5  1999/01/24 20:14:11  armin
 * Changed and added debug stuff.
 * Better data sending. (still problems with tty's flip buffer)
 *
 * Revision 1.4  1999/01/10 18:46:05  armin
 * Bug with wrong values in HLC fixed.
 * Bytes to send are counted and limited now.
 *
 * Revision 1.3  1999/01/05 14:49:34  armin
 * Added experimental usage of full BC and HLC for
 * speech, 3.1kHz audio, fax gr.2/3
 *
 * Revision 1.2  1999/01/04 13:19:29  armin
 * Channel status with listen-request wrong - fixed.
 *
 * Revision 1.1  1999/01/01 18:09:41  armin
 * First checkin of new eicon driver.
 * DIVA-Server BRI/PCI and PRI/PCI are supported.
 * Old diehl code is obsolete.
 *
 *
 */

#define __NO_VERSION__
#include "eicon.h"
#include "eicon_idi.h"
#include "eicon_dsp.h"

#undef EICON_FULL_SERVICE_OKTETT

char *eicon_idi_revision = "$Revision: 1.11 $";

eicon_manifbuf *manbuf;

static char BC_Speech[3] = 	{ 0x80, 0x90, 0xa3 };
static char BC_31khz[3] =  	{ 0x90, 0x90, 0xa3 };
static char BC_64k[2] =    	{ 0x88, 0x90 };
static char BC_video[3] =  	{ 0x91, 0x90, 0xa5 };

#ifdef EICON_FULL_SERVICE_OKTETT
/* 
static char HLC_telephony[2] =	{ 0x91, 0x81 }; 
*/
static char HLC_faxg3[2] =  	{ 0x91, 0x84 };
#endif

int eicon_idi_manage_assign(eicon_card *card);
int eicon_idi_manage_remove(eicon_card *card);
int idi_fill_in_T30(eicon_chan *chan, unsigned char *buffer);

int
idi_assign_req(eicon_REQ *reqbuf, int signet, eicon_chan *chan)
{
	int l = 0;
	int tmp;

	tmp = 0;
  if (!signet) {
	/* Signal Layer */
	reqbuf->XBuffer.P[l++] = CAI;
	reqbuf->XBuffer.P[l++] = 1;
	reqbuf->XBuffer.P[l++] = 0;
	reqbuf->XBuffer.P[l++] = KEY;
	reqbuf->XBuffer.P[l++] = 3;
	reqbuf->XBuffer.P[l++] = 'I';
	reqbuf->XBuffer.P[l++] = '4';
	reqbuf->XBuffer.P[l++] = 'L';
	reqbuf->XBuffer.P[l++] = SHIFT|6;
	reqbuf->XBuffer.P[l++] = SIN;
	reqbuf->XBuffer.P[l++] = 2;
	reqbuf->XBuffer.P[l++] = 0;
	reqbuf->XBuffer.P[l++] = 0;
	reqbuf->XBuffer.P[l++] = 0; /* end */
	reqbuf->Req = ASSIGN;
	reqbuf->ReqCh = 0;
	reqbuf->ReqId = 0;
	reqbuf->XBuffer.length = l;
	reqbuf->Reference = 0; /* Sig Entity */
  }
  else {
	/* Network Layer */
	reqbuf->XBuffer.P[l++] = CAI;
	reqbuf->XBuffer.P[l++] = 1;
	reqbuf->XBuffer.P[l++] = chan->e.D3Id;
	reqbuf->XBuffer.P[l++] = LLC;
	reqbuf->XBuffer.P[l++] = 2;
	switch(chan->l2prot) {
		case ISDN_PROTO_L2_HDLC:
			reqbuf->XBuffer.P[l++] = 2;
			break;
		case ISDN_PROTO_L2_X75I:
		case ISDN_PROTO_L2_X75UI:
		case ISDN_PROTO_L2_X75BUI:
			reqbuf->XBuffer.P[l++] = 5; 
			break;
		case ISDN_PROTO_L2_TRANS:
		case ISDN_PROTO_L2_MODEM:
			reqbuf->XBuffer.P[l++] = 2;
			break;
		case ISDN_PROTO_L2_FAX:
  			if (chan->fsm_state == EICON_STATE_IWAIT)
				reqbuf->XBuffer.P[l++] = 3; /* autoconnect on incoming */
			else
				reqbuf->XBuffer.P[l++] = 2;
			break;
		default:
			reqbuf->XBuffer.P[l++] = 1;
	}
	switch(chan->l3prot) {
		case ISDN_PROTO_L3_FAX:
#ifdef CONFIG_ISDN_TTY_FAX
			reqbuf->XBuffer.P[l++] = 6;
			reqbuf->XBuffer.P[l++] = NLC;
			tmp = idi_fill_in_T30(chan, &reqbuf->XBuffer.P[l+1]);
			reqbuf->XBuffer.P[l++] = tmp; 
			l += tmp;
			break;
#endif
		case ISDN_PROTO_L3_TRANS:
		default:
			reqbuf->XBuffer.P[l++] = 4;
	}
	reqbuf->XBuffer.P[l++] = 0; /* end */
	reqbuf->Req = ASSIGN;
	reqbuf->ReqCh = 0;
	reqbuf->ReqId = 0x20;
	reqbuf->XBuffer.length = l;
	reqbuf->Reference = 1; /* Net Entity */
  }
   return(0);
}

int
idi_put_req(eicon_REQ *reqbuf, int rq, int signet)
{
	reqbuf->Req = rq;
	reqbuf->ReqCh = 0;
	reqbuf->ReqId = 1;
	reqbuf->XBuffer.length = 1;
	reqbuf->XBuffer.P[0] = 0;
	reqbuf->Reference = signet;
   return(0);
}

int
idi_call_res_req(eicon_REQ *reqbuf, eicon_chan *chan)
{
	int l = 9;
	reqbuf->Req = CALL_RES;
	reqbuf->ReqCh = 0;
	reqbuf->ReqId = 1;
	reqbuf->XBuffer.P[0] = CAI;
	reqbuf->XBuffer.P[1] = 6;
	reqbuf->XBuffer.P[2] = 9;
	reqbuf->XBuffer.P[3] = 0;
	reqbuf->XBuffer.P[4] = 0;
	reqbuf->XBuffer.P[5] = 0;
	reqbuf->XBuffer.P[6] = 32;
	reqbuf->XBuffer.P[7] = 3;
	switch(chan->l2prot) {
		case ISDN_PROTO_L2_X75I:
		case ISDN_PROTO_L2_X75UI:
		case ISDN_PROTO_L2_X75BUI:
		case ISDN_PROTO_L2_HDLC:
			reqbuf->XBuffer.P[1] = 1;
			reqbuf->XBuffer.P[2] = 0x05;
			l = 4;
			break;
		case ISDN_PROTO_L2_V11096:
			reqbuf->XBuffer.P[2] = 0x0d;
			reqbuf->XBuffer.P[3] = 5;
			reqbuf->XBuffer.P[4] = 0;
			break;
		case ISDN_PROTO_L2_V11019:
			reqbuf->XBuffer.P[2] = 0x0d;
			reqbuf->XBuffer.P[3] = 6;
			reqbuf->XBuffer.P[4] = 0;
			break;
		case ISDN_PROTO_L2_V11038:
			reqbuf->XBuffer.P[2] = 0x0d;
			reqbuf->XBuffer.P[3] = 7;
			reqbuf->XBuffer.P[4] = 0;
			break;
		case ISDN_PROTO_L2_MODEM:
			reqbuf->XBuffer.P[2] = 0x11;
			reqbuf->XBuffer.P[3] = 7;
			reqbuf->XBuffer.P[4] = 0;
			reqbuf->XBuffer.P[5] = 0;
			reqbuf->XBuffer.P[6] = 128;
			reqbuf->XBuffer.P[7] = 0;
			break;
		case ISDN_PROTO_L2_FAX:
			reqbuf->XBuffer.P[2] = 0x10;
			reqbuf->XBuffer.P[3] = 0;
			reqbuf->XBuffer.P[4] = 0;
			reqbuf->XBuffer.P[5] = 0;
			reqbuf->XBuffer.P[6] = 128;
			reqbuf->XBuffer.P[7] = 0;
			break;
		case ISDN_PROTO_L2_TRANS:
			switch(chan->l3prot) {
				case ISDN_PROTO_L3_TRANSDSP:
					reqbuf->XBuffer.P[2] = 22; /* DTMF, audio events on */
			}
			break;
	}
	reqbuf->XBuffer.P[8] = 0;
	reqbuf->XBuffer.length = l;
	reqbuf->Reference = 0; /* Sig Entity */
	if (DebugVar & 8)
		printk(KERN_DEBUG"idi_req: Ch%d: Call_Res\n", chan->No);
   return(0);
}

int
idi_do_req(eicon_card *card, eicon_chan *chan, int cmd, int layer)
{
        struct sk_buff *skb = 0;
        struct sk_buff *skb2 = 0;
	eicon_REQ *reqbuf;
	eicon_chan_ptr *chan2;

        skb = alloc_skb(270 + sizeof(eicon_REQ), GFP_ATOMIC);
        skb2 = alloc_skb(sizeof(eicon_chan_ptr), GFP_ATOMIC);

        if ((!skb) || (!skb2)) {
		if (DebugVar & 1)
                	printk(KERN_WARNING "idi_err: Ch%d: alloc_skb failed in do_req()\n", chan->No);
		if (skb) 
			dev_kfree_skb(skb);
		if (skb2) 
			dev_kfree_skb(skb2);
                return -ENOMEM; 
	}

	chan2 = (eicon_chan_ptr *)skb_put(skb2, sizeof(eicon_chan_ptr));
	chan2->ptr = chan;

	reqbuf = (eicon_REQ *)skb_put(skb, 270 + sizeof(eicon_REQ));
	if (DebugVar & 8)
		printk(KERN_DEBUG "idi_req: Ch%d: req %x (%s)\n", chan->No, cmd, (layer)?"Net":"Sig");
	if (layer) cmd |= 0x700;
	switch(cmd) {
		case ASSIGN:
		case ASSIGN|0x700:
			idi_assign_req(reqbuf, layer, chan);
			break;
		case REMOVE:
		case REMOVE|0x700:
			idi_put_req(reqbuf, REMOVE, layer);
			break;
		case INDICATE_REQ:
			idi_put_req(reqbuf, INDICATE_REQ, 0);
			break;
		case HANGUP:
			idi_put_req(reqbuf, HANGUP, 0);
			break;
		case REJECT:
			idi_put_req(reqbuf, REJECT, 0);
			break;
		case CALL_ALERT:
			idi_put_req(reqbuf, CALL_ALERT, 0);
			break;
		case CALL_RES:
			idi_call_res_req(reqbuf, chan);
			break;
		case IDI_N_CONNECT|0x700:
			idi_put_req(reqbuf, IDI_N_CONNECT, 1);
			break;
		case IDI_N_CONNECT_ACK|0x700:
			idi_put_req(reqbuf, IDI_N_CONNECT_ACK, 1);
			break;
		case IDI_N_DISC|0x700:
			idi_put_req(reqbuf, IDI_N_DISC, 1);
			break;
		case IDI_N_DISC_ACK|0x700:
			idi_put_req(reqbuf, IDI_N_DISC_ACK, 1);
			break;
		default:
			if (DebugVar & 1)
				printk(KERN_ERR "idi_req: Ch%d: Unknown request\n", chan->No);
			dev_kfree_skb(skb);
			dev_kfree_skb(skb2);
			return(-1);
	}

	skb_queue_tail(&chan->e.X, skb);
	skb_queue_tail(&card->sndq, skb2); 
	eicon_schedule_tx(card);
	return(0);
}

int
eicon_idi_listen_req(eicon_card *card, eicon_chan *chan)
{
	if (DebugVar & 16)
		printk(KERN_DEBUG"idi_req: Ch%d: Listen_Req eazmask=0x%x\n",chan->No, chan->eazmask);
	if (!chan->e.D3Id) {
		idi_do_req(card, chan, ASSIGN, 0); 
	}
	if (chan->fsm_state == EICON_STATE_NULL) {
		idi_do_req(card, chan, INDICATE_REQ, 0);
		chan->fsm_state = EICON_STATE_LISTEN;
	}
  return(0);
}

unsigned char
idi_si2bc(int si1, int si2, char *bc, char *hlc)
{
  hlc[0] = 0;
  switch(si1) {
	case 1:
		bc[0] = 0x90;		/* 3,1 kHz audio */
		bc[1] = 0x90;		/* 64 kbit/s */
		bc[2] = 0xa3;		/* G.711 A-law */
#ifdef EICON_FULL_SERVICE_OKTETT
		if (si2 == 1) {
			bc[0] = 0x80;	/* Speech */
			hlc[0] = 0x02;	/* hlc len */
			hlc[1] = 0x91;	/* first hic */
			hlc[2] = 0x81;	/* Telephony */
		}
#endif
		return(3);
	case 2:
		bc[0] = 0x90;		/* 3,1 kHz audio */
		bc[1] = 0x90;		/* 64 kbit/s */
		bc[2] = 0xa3;		/* G.711 A-law */
#ifdef EICON_FULL_SERVICE_OKTETT
		if (si2 == 2) {
			hlc[0] = 0x02;	/* hlc len */
			hlc[1] = 0x91;	/* first hic */
			hlc[2] = 0x84;	/* Fax Gr.2/3 */
		}
#endif
		return(3);
	case 5:
	case 7:
	default:
		bc[0] = 0x88;
		bc[1] = 0x90;
		return(2);
  }
 return (0);
}

int
idi_hangup(eicon_card *card, eicon_chan *chan)
{
	if ((chan->fsm_state == EICON_STATE_ACTIVE) ||
	    (chan->fsm_state == EICON_STATE_WMCONN)) {
  		if (chan->e.B2Id) idi_do_req(card, chan, IDI_N_DISC, 1);
	}
	if (chan->e.B2Id) idi_do_req(card, chan, REMOVE, 1);
	idi_do_req(card, chan, HANGUP, 0);
	chan->fsm_state = EICON_STATE_NULL;
	if (DebugVar & 8)
		printk(KERN_DEBUG"idi_req: Ch%d: Hangup\n", chan->No);
#ifdef CONFIG_ISDN_TTY_FAX
	chan->fax = 0;
#endif
  return(0);
}

int
idi_connect_res(eicon_card *card, eicon_chan *chan)
{
  chan->fsm_state = EICON_STATE_IWAIT;
  idi_do_req(card, chan, CALL_RES, 0);
  idi_do_req(card, chan, ASSIGN, 1);
  return(0);
}

int
idi_connect_req(eicon_card *card, eicon_chan *chan, char *phone,
                    char *eazmsn, int si1, int si2)
{
	int l = 0;
	int i;
	unsigned char tmp;
	unsigned char bc[5];
	unsigned char hlc[5];
        struct sk_buff *skb;
        struct sk_buff *skb2;
	eicon_REQ *reqbuf;
	eicon_chan_ptr *chan2;

        skb = alloc_skb(270 + sizeof(eicon_REQ), GFP_ATOMIC);
        skb2 = alloc_skb(sizeof(eicon_chan_ptr), GFP_ATOMIC);

        if ((!skb) || (!skb2)) {
		if (DebugVar & 1)
                	printk(KERN_WARNING "idi_err: Ch%d: alloc_skb failed in connect_req()\n", chan->No);
		if (skb) 
			dev_kfree_skb(skb);
		if (skb2) 
			dev_kfree_skb(skb2);
                return -ENOMEM; 
	}

	chan2 = (eicon_chan_ptr *)skb_put(skb2, sizeof(eicon_chan_ptr));
	chan2->ptr = chan;

	reqbuf = (eicon_REQ *)skb_put(skb, 270 + sizeof(eicon_REQ));
	reqbuf->Req = CALL_REQ;
	reqbuf->ReqCh = 0;
	reqbuf->ReqId = 1;

	reqbuf->XBuffer.P[l++] = CPN;
	reqbuf->XBuffer.P[l++] = strlen(phone) + 1;
	reqbuf->XBuffer.P[l++] = 0xc1;
	for(i=0; i<strlen(phone);i++) 
		reqbuf->XBuffer.P[l++] = phone[i];

	reqbuf->XBuffer.P[l++] = OAD;
	reqbuf->XBuffer.P[l++] = strlen(eazmsn) + 2;
	reqbuf->XBuffer.P[l++] = 0x01;
	reqbuf->XBuffer.P[l++] = 0x81;
	for(i=0; i<strlen(eazmsn);i++) 
		reqbuf->XBuffer.P[l++] = eazmsn[i];

	if ((tmp = idi_si2bc(si1, si2, bc, hlc)) > 0) {
		reqbuf->XBuffer.P[l++] = BC;
		reqbuf->XBuffer.P[l++] = tmp;
		for(i=0; i<tmp;i++) 
			reqbuf->XBuffer.P[l++] = bc[i];
		if ((tmp=hlc[0])) {
			reqbuf->XBuffer.P[l++] = HLC;
			reqbuf->XBuffer.P[l++] = tmp;
			for(i=1; i<=tmp;i++) 
				reqbuf->XBuffer.P[l++] = hlc[i];
		}
	}
        reqbuf->XBuffer.P[l++] = CAI;
        reqbuf->XBuffer.P[l++] = 6;
        reqbuf->XBuffer.P[l++] = 0x09;
	reqbuf->XBuffer.P[l++] = 0;
	reqbuf->XBuffer.P[l++] = 0;
	reqbuf->XBuffer.P[l++] = 0;
	reqbuf->XBuffer.P[l++] = 32;
	reqbuf->XBuffer.P[l++] = 3;
        switch(chan->l2prot) {
		case ISDN_PROTO_L2_X75I:
		case ISDN_PROTO_L2_X75UI:
		case ISDN_PROTO_L2_X75BUI:
                case ISDN_PROTO_L2_HDLC:
                        reqbuf->XBuffer.P[l-6] = 5;
                        reqbuf->XBuffer.P[l-7] = 1;
			l -= 5; 
                        break;
                case ISDN_PROTO_L2_V11096:
                        reqbuf->XBuffer.P[l-7] = 3;
                        reqbuf->XBuffer.P[l-6] = 0x0d;
                        reqbuf->XBuffer.P[l-5] = 5;
                        reqbuf->XBuffer.P[l-4] = 0;
                        l -= 3;
                        break;
                case ISDN_PROTO_L2_V11019:
                        reqbuf->XBuffer.P[l-7] = 3;
                        reqbuf->XBuffer.P[l-6] = 0x0d;
                        reqbuf->XBuffer.P[l-5] = 6;
                        reqbuf->XBuffer.P[l-4] = 0;
                        l -= 3;
                        break;
                case ISDN_PROTO_L2_V11038:
                        reqbuf->XBuffer.P[l-7] = 3;
                        reqbuf->XBuffer.P[l-6] = 0x0d;
                        reqbuf->XBuffer.P[l-5] = 7;
                        reqbuf->XBuffer.P[l-4] = 0;
                        l -= 3;
                        break;
                case ISDN_PROTO_L2_MODEM:
			reqbuf->XBuffer.P[l-6] = 0x11;
			reqbuf->XBuffer.P[l-5] = 7;
			reqbuf->XBuffer.P[l-4] = 0;
			reqbuf->XBuffer.P[l-3] = 0;
			reqbuf->XBuffer.P[l-2] = 128;
			reqbuf->XBuffer.P[l-1] = 0;
                        break;
                case ISDN_PROTO_L2_FAX:
			reqbuf->XBuffer.P[l-6] = 0x10;
			reqbuf->XBuffer.P[l-5] = 0;
			reqbuf->XBuffer.P[l-4] = 0;
			reqbuf->XBuffer.P[l-3] = 0;
			reqbuf->XBuffer.P[l-2] = 128;
			reqbuf->XBuffer.P[l-1] = 0;
                        break;
		case ISDN_PROTO_L2_TRANS:
			switch(chan->l3prot) {
				case ISDN_PROTO_L3_TRANSDSP:
					reqbuf->XBuffer.P[l-6] = 22; /* DTMF, audio events on */
			}
			break;
        }
	
	reqbuf->XBuffer.P[l++] = 0; /* end */
	reqbuf->XBuffer.length = l;
	reqbuf->Reference = 0; /* Sig Entity */

	skb_queue_tail(&chan->e.X, skb);
	skb_queue_tail(&card->sndq, skb2); 
	eicon_schedule_tx(card);

	if (DebugVar & 8)
  		printk(KERN_DEBUG"idi_req: Ch%d: Conn_Req %s -> %s\n",chan->No, eazmsn, phone);
   return(0);
}


void
idi_IndParse(eicon_card *ccard, eicon_chan *chan, idi_ind_message *message, unsigned char *buffer, int len)
{
	int i,j;
	int pos = 0;
	int codeset = 0;
	int wlen = 0;
	int lock = 0;
	__u8 w;
	__u16 code;
	isdn_ctrl cmd;

  memset(message, 0, sizeof(idi_ind_message));

  if ((!len) || (!buffer[pos])) return;
  while(pos <= len) {
	w = buffer[pos++];
	if (!w) return;
	if (w & 0x80) {
		wlen = 0;
	}
	else {
		wlen = buffer[pos++];
	}

	if (pos > len) return;

	if (lock & 0x80) lock &= 0x7f;
	else codeset = lock;

	if((w&0xf0) == SHIFT) {
		codeset = w;
		if(!(codeset & 0x08)) lock = codeset & 7;
		codeset &= 7;
		lock |= 0x80;
	}
	else {
		if (w==ESC && wlen >=2) {
			code = buffer[pos++]|0x800;
			wlen--;
		}
		else code = w;
		code |= (codeset<<8);

		switch(code) {
			case OAD:
				j = 1;
				if (wlen) {
					message->plan = buffer[pos++];
					if (message->plan &0x80) 
						message->screen = 0;
					else {
						message->screen = buffer[pos++];
						j = 2;
					}
				}
				for(i=0; i < wlen-j; i++) 
					message->oad[i] = buffer[pos++];
				if (DebugVar & 2)
					printk(KERN_DEBUG"idi_inf: Ch%d: OAD=(0x%02x,0x%02x) %s\n", chan->No, 
						message->plan, message->screen, message->oad);
				break;
			case RDN:
				j = 1;
				if (wlen) {
					if (!(buffer[pos++] & 0x80)) {
						pos++; 
						j = 2;
					}
				}
				for(i=0; i < wlen-j; i++) 
					message->rdn[i] = buffer[pos++];
				if (DebugVar & 2)
					printk(KERN_DEBUG"idi_inf: Ch%d: RDN= %s\n", chan->No, 
						message->rdn);
				break;
			case CPN:
				for(i=0; i < wlen; i++) 
					message->cpn[i] = buffer[pos++];
				if (DebugVar & 2)
					printk(KERN_DEBUG"idi_inf: Ch%d: CPN=(0x%02x) %s\n", chan->No,
						(__u8)message->cpn[0], message->cpn + 1);
				break;
			case DSA:
				pos++;
				for(i=0; i < wlen-1; i++) 
					message->dsa[i] = buffer[pos++];
				if (DebugVar & 2)
					printk(KERN_DEBUG"idi_inf: Ch%d: DSA=%s\n", chan->No, message->dsa);
				break;
			case OSA:
				pos++;
				for(i=0; i < wlen-1; i++) 
					message->osa[i] = buffer[pos++];
				if (DebugVar & 2)
					printk(KERN_DEBUG"idi_inf: Ch%d: OSA=%s\n", chan->No, message->osa);
				break;
			case BC:
				for(i=0; i < wlen; i++) 
					message->bc[i] = buffer[pos++];
				if (DebugVar & 4)
					printk(KERN_DEBUG"idi_inf: Ch%d: BC = 0x%02x 0x%02x 0x%02x\n", chan->No,
						message->bc[0],message->bc[1],message->bc[2]);
				break;
			case 0x800|BC:
				for(i=0; i < wlen; i++) 
					message->e_bc[i] = buffer[pos++];
				if (DebugVar & 4)
					printk(KERN_DEBUG"idi_inf: Ch%d: ESC/BC=%d\n", chan->No, message->bc[0]);
				break;
			case LLC:
				for(i=0; i < wlen; i++) 
					message->llc[i] = buffer[pos++];
				if (DebugVar & 4)
					printk(KERN_DEBUG"idi_inf: Ch%d: LLC=%d %d %d %d\n", chan->No, message->llc[0],
						message->llc[1],message->llc[2],message->llc[3]);
				break;
			case HLC:
				for(i=0; i < wlen; i++) 
					message->hlc[i] = buffer[pos++];
				if (DebugVar & 4)
					printk(KERN_DEBUG"idi_inf: Ch%d: HLC=%x %x %x %x %x\n", chan->No,
						message->hlc[0], message->hlc[1],
						message->hlc[2], message->hlc[3], message->hlc[4]);
				break;
			case DSP:
			case 0x600|DSP:
				for(i=0; i < wlen; i++) 
					message->display[i] = buffer[pos++];
				if (DebugVar & 4)
					printk(KERN_DEBUG"idi_inf: Ch%d: Display: %s\n", chan->No,
						message->display);
				break;
			case 0x600|KEY:
				for(i=0; i < wlen; i++) 
					message->keypad[i] = buffer[pos++];
				if (DebugVar & 4)
					printk(KERN_DEBUG"idi_inf: Ch%d: Keypad: %s\n", chan->No,
						message->keypad);
				break;
			case NI:
			case 0x600|NI:
				if (wlen) {
					if (DebugVar & 4) {
						switch(buffer[pos] & 127) {
							case 0:
								printk(KERN_DEBUG"idi_inf: Ch%d: User suspended.\n", chan->No);
								break;
							case 1:
								printk(KERN_DEBUG"idi_inf: Ch%d: User resumed.\n", chan->No);
								break;
							case 2:
								printk(KERN_DEBUG"idi_inf: Ch%d: Bearer service change.\n", chan->No);
								break;
							default:
								printk(KERN_DEBUG"idi_inf: Ch%d: Unknown Notification %x.\n", 
										chan->No, buffer[pos] & 127);
						}
					}
					pos += wlen;
				}
				break;
			case PI:
			case 0x600|PI:
				if (wlen > 1) {
					if (DebugVar & 4) {
						switch(buffer[pos+1] & 127) {
							case 1:
								printk(KERN_DEBUG"idi_inf: Ch%d: Call is not end-to-end ISDN.\n", chan->No);
								break;
							case 2:
								printk(KERN_DEBUG"idi_inf: Ch%d: Destination address is non ISDN.\n", chan->No);
								break;
							case 3:
								printk(KERN_DEBUG"idi_inf: Ch%d: Origination address is non ISDN.\n", chan->No);
								break;
							case 4:
								printk(KERN_DEBUG"idi_inf: Ch%d: Call has returned to the ISDN.\n", chan->No);
								break;
							case 5:
								printk(KERN_DEBUG"idi_inf: Ch%d: Interworking has occurred.\n", chan->No);
								break;
							case 8:
								printk(KERN_DEBUG"idi_inf: Ch%d: In-band information available.\n", chan->No);
								break;
							default:
								printk(KERN_DEBUG"idi_inf: Ch%d: Unknown Progress %x.\n", 
										chan->No, buffer[pos+1] & 127);
						}
					}
				}
				pos += wlen;
				break;
			case CAU:
				for(i=0; i < wlen; i++) 
					message->cau[i] = buffer[pos++];
				memcpy(&chan->cause, &message->cau, 2);
				if (DebugVar & 4)
					printk(KERN_DEBUG"idi_inf: Ch%d: CAU=%d %d\n", chan->No,
						message->cau[0],message->cau[1]);
				break;
			case 0x800|CAU:
				for(i=0; i < wlen; i++) 
					message->e_cau[i] = buffer[pos++];
				if (DebugVar & 4)
					printk(KERN_DEBUG"idi_inf: Ch%d: ECAU=%d %d\n", chan->No,
						message->e_cau[0],message->e_cau[1]);
				break;
			case 0x800|CHI:
				for(i=0; i < wlen; i++) 
					message->e_chi[i] = buffer[pos++];
				if (DebugVar & 4)
					printk(KERN_DEBUG"idi_inf: Ch%d: ESC/CHI=%d\n", chan->No,
						message->e_cau[0]);
				break;
			case 0x800|0x7a:
				pos ++;
				message->e_mt=buffer[pos++];
				if (DebugVar & 2)
					printk(KERN_DEBUG"idi_inf: Ch%d: EMT=0x%x\n", chan->No, message->e_mt);
				break;
			case DT:
				for(i=0; i < wlen; i++) 
					message->dt[i] = buffer[pos++];
				if (DebugVar & 4)
					printk(KERN_DEBUG"idi_inf: Ch%d: DT: %02d.%02d.%02d %02d:%02d:%02d\n", chan->No,
						message->dt[2], message->dt[1], message->dt[0],
						message->dt[3], message->dt[4], message->dt[5]);
				break;
			case 0x600|SIN:
				for(i=0; i < wlen; i++) 
					message->sin[i] = buffer[pos++];
				if (DebugVar & 2)
					printk(KERN_DEBUG"idi_inf: Ch%d: SIN=%d %d\n", chan->No,
						message->sin[0],message->sin[1]);
				break;
			case 0x600|CPS:
				if (DebugVar & 2)
					printk(KERN_DEBUG"idi_inf: Ch%d: Called Party Status in ind\n", chan->No);
				pos += wlen;
				break;
			case 0x600|CIF:
				for (i = 0; i < wlen; i++)
					if (buffer[pos + i] != '0') break;
				memcpy(&cmd.parm.num, &buffer[pos + i], wlen - i);
				cmd.parm.num[wlen - i] = 0;
				if (DebugVar & 2)
					printk(KERN_DEBUG"idi_inf: Ch%d: CIF=%s\n", chan->No, cmd.parm.num);
				pos += wlen;
				cmd.driver = ccard->myid;
				cmd.command = ISDN_STAT_CINF;
				cmd.arg = chan->No;
				ccard->interface.statcallb(&cmd);
				break;
			case 0x600|DATE:
				if (DebugVar & 2)
					printk(KERN_DEBUG"idi_inf: Ch%d: Date in ind\n", chan->No);
				pos += wlen;
				break;
			case 0xe08: 
			case 0xe7a: 
			case 0xe04: 
			case 0xe00: 
				/* *** TODO *** */
			case CHA:
				/* Charge advice */
			case FTY:
			case 0x600|FTY:
			case CHI:
			case 0x800:
				/* Not yet interested in this */
				pos += wlen;
				break;
			case 0x880:
				/* Managment Information Element */
				if (!manbuf) {
					if (DebugVar & 1)
						printk(KERN_WARNING"idi_err: manbuf not allocated\n");
				}
				else {
					memcpy(&manbuf->data[manbuf->pos], &buffer[pos], wlen);
					manbuf->length[manbuf->count] = wlen;
					manbuf->count++;
					manbuf->pos += wlen;
				}
				pos += wlen;
				break;
			default:
				pos += wlen;
				if (DebugVar & 6)
					printk(KERN_WARNING"idi_inf: Ch%d: unknown information element 0x%x in ind, len:%x\n", 
						chan->No, code, wlen);
		}
	}
  }
}

void
idi_bc2si(unsigned char *bc, unsigned char *hlc, unsigned char *si1, unsigned char *si2)
{
  si1[0] = 0;
  si2[0] = 0;
  if (memcmp(bc, BC_Speech, 3) == 0) {		/* Speech */
	si1[0] = 1;
#ifdef EICON_FULL_SERVICE_OKTETT
	si2[0] = 1;
#endif
  }
  if (memcmp(bc, BC_31khz, 3) == 0) {		/* 3.1kHz audio */
	si1[0] = 1;
#ifdef EICON_FULL_SERVICE_OKTETT
	si2[0] = 2;
  	if (memcmp(hlc, HLC_faxg3, 2) == 0) {	/* Fax Gr.2/3 */
		si1[0] = 2;
	}
#endif
  }
  if (memcmp(bc, BC_64k, 2) == 0) {		/* unrestricted 64 kbits */
	si1[0] = 7;
  }
  if (memcmp(bc, BC_video, 3) == 0) {		/* video */
	si1[0] = 4;
  }
}


int
idi_send_udata(eicon_card *card, eicon_chan *chan, int UReq, u_char *buffer, int len)
{
	struct sk_buff *skb;
	struct sk_buff *skb2;
	eicon_REQ *reqbuf;
	eicon_chan_ptr *chan2;

	if ((chan->fsm_state == EICON_STATE_NULL) || (chan->fsm_state == EICON_STATE_LISTEN)) {
		if (DebugVar & 1)
			printk(KERN_DEBUG"idi_snd: Ch%d: send udata on state %d !\n", chan->No, chan->fsm_state);
		return -ENODEV;
	}
	if (DebugVar & 8)
		printk(KERN_DEBUG"idi_snd: Ch%d: udata 0x%x: %d %d %d %d\n", chan->No,
				UReq, buffer[0], buffer[1], buffer[2], buffer[3]);

	skb = alloc_skb(sizeof(eicon_REQ) + len + 1, GFP_ATOMIC);
	skb2 = alloc_skb(sizeof(eicon_chan_ptr), GFP_ATOMIC);

	if ((!skb) || (!skb2)) {
		if (DebugVar & 1)
			printk(KERN_WARNING "idi_err: Ch%d: alloc_skb failed in send_udata()\n", chan->No);
		if (skb) 
			dev_kfree_skb(skb);
		if (skb2) 
			dev_kfree_skb(skb2);
		return -ENOMEM;
	}

	chan2 = (eicon_chan_ptr *)skb_put(skb2, sizeof(eicon_chan_ptr));
	chan2->ptr = chan;

	reqbuf = (eicon_REQ *)skb_put(skb, 1 + len + sizeof(eicon_REQ));

	reqbuf->Req = IDI_N_UDATA;
	reqbuf->ReqCh = 0;
	reqbuf->ReqId = 1;

	reqbuf->XBuffer.length = len + 1;
	reqbuf->XBuffer.P[0] = UReq;
	memcpy(&reqbuf->XBuffer.P[1], buffer, len);
	reqbuf->Reference = 1; /* Net Entity */

	skb_queue_tail(&chan->e.X, skb);
	skb_queue_tail(&card->sndq, skb2);
	eicon_schedule_tx(card);
	return (0);
}

void
idi_audio_cmd(eicon_card *ccard, eicon_chan *chan, int cmd, u_char *value)
{
	u_char buf[6];
	struct enable_dtmf_s *dtmf_buf = (struct enable_dtmf_s *)buf;

	memset(buf, 0, 6);
	switch(cmd) {
		case ISDN_AUDIO_SETDD:
			if (value[0]) {
				dtmf_buf->tone = (__u16) (value[1] * 5);
				dtmf_buf->gap = (__u16) (value[1] * 5);
				idi_send_udata(ccard, chan,
					DSP_UDATA_REQUEST_ENABLE_DTMF_RECEIVER,
					buf, 4);
			} else {
				idi_send_udata(ccard, chan,
					DSP_UDATA_REQUEST_DISABLE_DTMF_RECEIVER,
					buf, 0);
			}
			break;
	}
}

void
idi_parse_udata(eicon_card *ccard, eicon_chan *chan, unsigned char *buffer, int len)
{
	isdn_ctrl cmd;
	eicon_dsp_ind *p = (eicon_dsp_ind *) (&buffer[1]);
        static char *connmsg[] =
        {"", "V.21", "V.23", "V.22", "V.22bis", "V.32bis", "V.34",
         "V.8", "Bell 212A", "Bell 103", "V.29 Leased", "V.33 Leased", "V.90",
         "V.21 CH2", "V.27ter", "V.29", "V.33", "V.17"};
	static u_char dtmf_code[] = {
	'1','4','7','*','2','5','8','0','3','6','9','#','A','B','C','D'
	};

	switch (buffer[0]) {
		case DSP_UDATA_INDICATION_SYNC:
			if (DebugVar & 16)
  				printk(KERN_DEBUG"idi_ind: Ch%d: UDATA_SYNC time %d\n", chan->No, p->time);
			break;
		case DSP_UDATA_INDICATION_DCD_OFF:
			if (DebugVar & 8)
  				printk(KERN_DEBUG"idi_ind: Ch%d: UDATA_DCD_OFF time %d\n", chan->No, p->time);
			break;
		case DSP_UDATA_INDICATION_DCD_ON:
			if ((chan->l2prot == ISDN_PROTO_L2_MODEM) &&
			    (chan->fsm_state == EICON_STATE_WMCONN)) {
				chan->fsm_state = EICON_STATE_ACTIVE;
				cmd.driver = ccard->myid;
				cmd.command = ISDN_STAT_BCONN;
				cmd.arg = chan->No;
				sprintf(cmd.parm.num, "%d/%s", p->speed, connmsg[p->norm]);
				ccard->interface.statcallb(&cmd);
			}
			if (DebugVar & 8) {
  				printk(KERN_DEBUG"idi_ind: Ch%d: UDATA_DCD_ON time %d\n", chan->No, p->time);
				printk(KERN_DEBUG"idi_ind: Ch%d: %d %d %d %d\n", chan->No,
					    p->norm, p->options, p->speed, p->delay); 
			    }
			break;
		case DSP_UDATA_INDICATION_CTS_OFF:
			if (DebugVar & 8)
  				printk(KERN_DEBUG"idi_ind: Ch%d: UDATA_CTS_OFF time %d\n", chan->No, p->time);
			break;
		case DSP_UDATA_INDICATION_CTS_ON:
			if (DebugVar & 8) {
  				printk(KERN_DEBUG"idi_ind: Ch%d: UDATA_CTS_ON time %d\n", chan->No, p->time);
				printk(KERN_DEBUG"idi_ind: Ch%d: %d %d %d %d\n", chan->No,
					    p->norm, p->options, p->speed, p->delay); 
			}
			break;
		case DSP_UDATA_INDICATION_DISCONNECT:
			if (DebugVar & 8)
  				printk(KERN_DEBUG"idi_ind: Ch%d: UDATA_DISCONNECT cause %d\n", chan->No, buffer[1]);
			break;
		case DSP_UDATA_INDICATION_DTMF_DIGITS_RECEIVED:
			if (DebugVar & 8)
  				printk(KERN_DEBUG"idi_ind: Ch%d: UDATA_DTMF_REC '%c'\n", chan->No,
					dtmf_code[buffer[1]]);
			cmd.driver = ccard->myid;
			cmd.command = ISDN_STAT_AUDIO;
			cmd.parm.num[0] = ISDN_AUDIO_DTMF;
			cmd.parm.num[1] = dtmf_code[buffer[1]];
			cmd.arg = chan->No;
			ccard->interface.statcallb(&cmd);
			break;
		default:
			if (DebugVar & 8)
				printk(KERN_WARNING "idi_ind: Ch%d: UNHANDLED UDATA Indication 0x%02x\n", chan->No, buffer[0]);
	}
}

void
idi_handle_ind(eicon_card *ccard, struct sk_buff *skb)
{
	int tmp;
	int free_buff;
	struct sk_buff *skb2;
        eicon_IND *ind = (eicon_IND *)skb->data;
	eicon_chan *chan;
	idi_ind_message message;
	isdn_ctrl cmd;

	if ((chan = ccard->IdTable[ind->IndId]) == NULL) {
  		dev_kfree_skb(skb);
		return;
	}
	
	if ((DebugVar & 128) || 
	   ((DebugVar & 16) && (ind->Ind != 8))) {
        	printk(KERN_DEBUG "idi_hdl: Ch%d: Ind=%d Id=%x Ch=%d MInd=%d MLen=%d Len=%d\n", chan->No,
		        ind->Ind,ind->IndId,ind->IndCh,ind->MInd,ind->MLength,ind->RBuffer.length);
	}

	free_buff = 1;
	/* Signal Layer */
	if (chan->e.D3Id == ind->IndId) {
		idi_IndParse(ccard, chan, &message, ind->RBuffer.P, ind->RBuffer.length);
		switch(ind->Ind) {
			case HANGUP:
				if (DebugVar & 8)
  					printk(KERN_DEBUG"idi_ind: Ch%d: Hangup\n", chan->No);
		                while((skb2 = skb_dequeue(&chan->e.X))) {
					dev_kfree_skb(skb2);
				}
				chan->e.busy = 0;
				chan->queued = 0;
				chan->waitq = 0;
				chan->waitpq = 0;
				chan->fsm_state = EICON_STATE_NULL;
				if (message.e_cau[0] & 0x7f) {
					cmd.driver = ccard->myid;
					cmd.arg = chan->No;
					sprintf(cmd.parm.num,"E%02x%02x", 
						chan->cause[0]&0x7f, message.e_cau[0]&0x7f); 
					cmd.command = ISDN_STAT_CAUSE;
					ccard->interface.statcallb(&cmd);
				}
				chan->cause[0] = 0; 
				cmd.driver = ccard->myid;
				cmd.arg = chan->No;
				cmd.command = ISDN_STAT_DHUP;
				ccard->interface.statcallb(&cmd);
				eicon_idi_listen_req(ccard, chan);
#ifdef CONFIG_ISDN_TTY_FAX
				chan->fax = 0;
#endif
				break;
			case INDICATE_IND:
				if (DebugVar & 8)
  					printk(KERN_DEBUG"idi_ind: Ch%d: Indicate_Ind\n", chan->No);
				chan->fsm_state = EICON_STATE_ICALL;
				idi_bc2si(message.bc, message.hlc, &chan->si1, &chan->si2);
				strcpy(chan->cpn, message.cpn + 1);
				if (strlen(message.dsa)) {
					strcat(chan->cpn, ".");
					strcat(chan->cpn, message.dsa);
				}
				strcpy(chan->oad, message.oad);
				try_stat_icall_again: 
				cmd.driver = ccard->myid;
				cmd.command = ISDN_STAT_ICALL;
				cmd.arg = chan->No;
				cmd.parm.setup.si1 = chan->si1;
				cmd.parm.setup.si2 = chan->si2;
				strcpy(cmd.parm.setup.eazmsn, chan->cpn);
				strcpy(cmd.parm.setup.phone, chan->oad);
				cmd.parm.setup.plan = message.plan;
				cmd.parm.setup.screen = message.screen;
				tmp = ccard->interface.statcallb(&cmd);
				switch(tmp) {
					case 0: /* no user responding */
						idi_do_req(ccard, chan, HANGUP, 0);
						break;
					case 1: /* alert */
						if (DebugVar & 8)
  							printk(KERN_DEBUG"idi_req: Ch%d: Call Alert\n", chan->No);
						if ((chan->fsm_state == EICON_STATE_ICALL) || (chan->fsm_state == EICON_STATE_ICALLW)) {
							chan->fsm_state = EICON_STATE_ICALL;
							idi_do_req(ccard, chan, CALL_ALERT, 0);
						}
						break;
					case 2: /* reject */
						if (DebugVar & 8)
  							printk(KERN_DEBUG"idi_req: Ch%d: Call Reject\n", chan->No);
						idi_do_req(ccard, chan, REJECT, 0);
						break;
					case 3: /* incomplete number */
						if (DebugVar & 8)
  							printk(KERN_DEBUG"idi_req: Ch%d: Incomplete Number\n", chan->No);
					        switch(ccard->type) {
					                case EICON_CTYPE_MAESTRAP:
					                case EICON_CTYPE_S2M:
								/* TODO (other protocols) */
								chan->fsm_state = EICON_STATE_ICALLW;
								break;
							default:
								idi_do_req(ccard, chan, HANGUP, 0);
						}
						break;
				}
				break;
			case INFO_IND:
				if (DebugVar & 8)
  					printk(KERN_DEBUG"idi_ind: Ch%d: Info_Ind\n", chan->No);
				if ((chan->fsm_state == EICON_STATE_ICALLW) &&
				    (message.cpn[0])) {
					strcat(chan->cpn, message.cpn + 1);
					goto try_stat_icall_again;
				}
				break;
			case CALL_IND:
				if (DebugVar & 8)
  					printk(KERN_DEBUG"idi_ind: Ch%d: Call_Ind\n", chan->No);
				if ((chan->fsm_state == EICON_STATE_ICALL) || (chan->fsm_state == EICON_STATE_IWAIT)) {
					chan->fsm_state = EICON_STATE_IBWAIT;
					cmd.driver = ccard->myid;
					cmd.command = ISDN_STAT_DCONN;
					cmd.arg = chan->No;
					ccard->interface.statcallb(&cmd);
					if (chan->l2prot != ISDN_PROTO_L2_FAX) {
						idi_do_req(ccard, chan, IDI_N_CONNECT, 1);
					}
#ifdef CONFIG_ISDN_TTY_FAX
					else {
						if (chan->fax)
							chan->fax->phase = ISDN_FAX_PHASE_A;
					}
#endif
				} else
					idi_hangup(ccard, chan);
				break;
			case CALL_CON:
				if (DebugVar & 8)
  					printk(KERN_DEBUG"idi_ind: Ch%d: Call_Con\n", chan->No);
				if (chan->fsm_state == EICON_STATE_OCALL) {
					chan->fsm_state = EICON_STATE_OBWAIT;
					cmd.driver = ccard->myid;
					cmd.command = ISDN_STAT_DCONN;
					cmd.arg = chan->No;
					ccard->interface.statcallb(&cmd);
					idi_do_req(ccard, chan, ASSIGN, 1); 
					idi_do_req(ccard, chan, IDI_N_CONNECT, 1);
#ifdef CONFIG_ISDN_TTY_FAX
					if (chan->l2prot == ISDN_PROTO_L2_FAX) {
						if (chan->fax)
							chan->fax->phase = ISDN_FAX_PHASE_A;
					}
#endif
				} else
				idi_hangup(ccard, chan);
				break;
			case AOC_IND:
				if (DebugVar & 8)
  					printk(KERN_DEBUG"idi_ind: Ch%d: Advice of Charge\n", chan->No);
				break;
			default:
				if (DebugVar & 8)
					printk(KERN_WARNING "idi_ind: Ch%d: UNHANDLED SigIndication 0x%02x\n", chan->No, ind->Ind);
		}
	}
	/* Network Layer */
	else if (chan->e.B2Id == ind->IndId) {

		if (chan->No == ccard->nchannels) {
			/* Management Indication */
			idi_IndParse(ccard, chan, &message, ind->RBuffer.P, ind->RBuffer.length);
			chan->fsm_state = 1;
		} 
		else
		switch(ind->Ind) {
			case IDI_N_CONNECT_ACK:
				if (DebugVar & 16)
  					printk(KERN_DEBUG"idi_ind: Ch%d: N_Connect_Ack\n", chan->No);
				if (chan->l2prot == ISDN_PROTO_L2_MODEM) {
					chan->fsm_state = EICON_STATE_WMCONN;
					break;
				}
				if (chan->l2prot == ISDN_PROTO_L2_FAX) {
#ifdef CONFIG_ISDN_TTY_FAX
					chan->fsm_state = EICON_STATE_ACTIVE;
					idi_parse_edata(ccard, chan, ind->RBuffer.P, ind->RBuffer.length);
					if (chan->fax) {
						if (chan->fax->phase == ISDN_FAX_PHASE_B) {
							idi_fax_send_header(ccard, chan, 2);
							cmd.driver = ccard->myid;
							cmd.command = ISDN_STAT_FAXIND;
							cmd.arg = chan->No;
							chan->fax->r_code = ISDN_TTY_FAX_DCS;
							ccard->interface.statcallb(&cmd);
						}
					}
					else {
						if (DebugVar & 1)
							printk(KERN_DEBUG "idi_ind: N_CONNECT_ACK with NULL fax struct, ERROR\n");
					}
#endif
					break;
				}
				chan->fsm_state = EICON_STATE_ACTIVE;
				cmd.driver = ccard->myid;
				cmd.command = ISDN_STAT_BCONN;
				cmd.arg = chan->No;
				ccard->interface.statcallb(&cmd);
				break; 
			case IDI_N_CONNECT:
				if (DebugVar & 16)
  					printk(KERN_DEBUG"idi_ind: Ch%d: N_Connect\n", chan->No);
				if (chan->e.B2Id) idi_do_req(ccard, chan, IDI_N_CONNECT_ACK, 1);
				if (chan->l2prot == ISDN_PROTO_L2_FAX) {
					break;
				}
				if (chan->l2prot == ISDN_PROTO_L2_MODEM) {
					chan->fsm_state = EICON_STATE_WMCONN;
					break;
				}
				chan->fsm_state = EICON_STATE_ACTIVE;
				cmd.driver = ccard->myid;
				cmd.command = ISDN_STAT_BCONN;
				cmd.arg = chan->No;
				ccard->interface.statcallb(&cmd);
				break; 
			case IDI_N_DISC:
				if (DebugVar & 16)
  					printk(KERN_DEBUG"idi_ind: Ch%d: N_DISC\n", chan->No);
				if (chan->e.B2Id) {
					idi_do_req(ccard, chan, IDI_N_DISC_ACK, 1);
					idi_do_req(ccard, chan, REMOVE, 1);
				}
#ifdef CONFIG_ISDN_TTY_FAX
				if (chan->l2prot == ISDN_PROTO_L2_FAX) {
					idi_parse_edata(ccard, chan, ind->RBuffer.P, ind->RBuffer.length);
					idi_fax_hangup(ccard, chan);
				}
#endif
				chan->queued = 0;
				chan->waitq = 0;
				chan->waitpq = 0;
				if (chan->fsm_state == EICON_STATE_ACTIVE) {
					cmd.driver = ccard->myid;
					cmd.command = ISDN_STAT_BHUP;
					cmd.arg = chan->No;
					ccard->interface.statcallb(&cmd);
				}
				break; 
			case IDI_N_DISC_ACK:
				if (DebugVar & 16)
  					printk(KERN_DEBUG"idi_ind: Ch%d: N_DISC_ACK\n", chan->No);
#ifdef CONFIG_ISDN_TTY_FAX
				if (chan->l2prot == ISDN_PROTO_L2_FAX) {
					idi_parse_edata(ccard, chan, ind->RBuffer.P, ind->RBuffer.length);
					idi_fax_hangup(ccard, chan);
				}
#endif
				break; 
			case IDI_N_DATA_ACK:
				if (DebugVar & 16)
  					printk(KERN_DEBUG"idi_ind: Ch%d: N_DATA_ACK\n", chan->No);
				break;
			case IDI_N_DATA:
				skb_pull(skb, sizeof(eicon_IND) - 1);
				if (DebugVar & 128)
					printk(KERN_DEBUG"idi_rcv: Ch%d: %d bytes\n", chan->No, skb->len);
				if (chan->l2prot == ISDN_PROTO_L2_FAX) {
#ifdef CONFIG_ISDN_TTY_FAX
					idi_faxdata_rcv(ccard, chan, skb);
#endif
				} else {
					ccard->interface.rcvcallb_skb(ccard->myid, chan->No, skb);
					free_buff = 0; 
				}
				break; 
			case IDI_N_UDATA:
				idi_parse_udata(ccard, chan, ind->RBuffer.P, ind->RBuffer.length);
				break; 
#ifdef CONFIG_ISDN_TTY_FAX
			case IDI_N_EDATA:
				idi_edata_action(ccard, chan, ind->RBuffer.P, ind->RBuffer.length);
				break; 
#endif
			default:
				if (DebugVar & 8)
					printk(KERN_WARNING "idi_ind: Ch%d: UNHANDLED NetIndication 0x%02x\n", chan->No, ind->Ind);
		}
	}
	else {
		if (DebugVar & 1)
			printk(KERN_ERR "idi_ind: Ch%d: Ind is neither SIG nor NET !\n", chan->No);
	}
   if (free_buff) dev_kfree_skb(skb);
}

void
idi_handle_ack_ok(eicon_card *ccard, eicon_chan *chan, eicon_RC *ack)
{
	isdn_ctrl cmd;

	if (ack->RcId != ((chan->e.ReqCh) ? chan->e.B2Id : chan->e.D3Id)) {
		/* I dont know why this happens, just ignoring this RC */
		if (DebugVar & 16)
			printk(KERN_DEBUG "idi_ack: Ch%d: RcId %d not equal to last %d\n", chan->No, 
				ack->RcId, (chan->e.ReqCh) ? chan->e.B2Id : chan->e.D3Id);
		return;
	}

	/* Management Interface */	
	if (chan->No == ccard->nchannels) {
		/* Managementinterface: changing state */
		if (chan->e.Req == 0x04)
			chan->fsm_state = 1;
	}

	/* Remove an Id */
	if (chan->e.Req == REMOVE) {
		if (ack->Reference != chan->e.ref) {
			if (DebugVar & 1)
				printk(KERN_DEBUG "idi_ack: Ch%d: Rc-Ref %d not equal to stored %d\n", chan->No,
					ack->Reference, chan->e.ref);
		}
		ccard->IdTable[ack->RcId] = NULL;
		if (DebugVar & 16)
			printk(KERN_DEBUG "idi_ack: Ch%d: Removed : Id=%d Ch=%d (%s)\n", chan->No,
				ack->RcId, ack->RcCh, (chan->e.ReqCh)? "Net":"Sig");
		if (!chan->e.ReqCh) 
			chan->e.D3Id = 0;
		else
			chan->e.B2Id = 0;
		return;
	}

	/* Signal layer */
	if (!chan->e.ReqCh) {
		if (DebugVar & 16)
			printk(KERN_DEBUG "idi_ack: Ch%d: RC OK Id=%d Ch=%d (ref:%d)\n", chan->No,
				ack->RcId, ack->RcCh, ack->Reference);
	} else {
	/* Network layer */
		switch(chan->e.Req & 0x0f) {
			case IDI_N_MDATA:
			case IDI_N_DATA:
				if ((chan->e.Req & 0x0f) == IDI_N_DATA) {
					if (chan->queued) {
						cmd.driver = ccard->myid;
						cmd.command = ISDN_STAT_BSENT;
						cmd.arg = chan->No;
						cmd.parm.length = chan->waitpq;
						ccard->interface.statcallb(&cmd);
					}
					chan->waitpq = 0;
#ifdef CONFIG_ISDN_TTY_FAX
					if (chan->l2prot == ISDN_PROTO_L2_FAX) {
						if (((chan->queued - chan->waitq) < 1) &&
						    (chan->fax2.Eop)) {
							chan->fax2.Eop = 0;
							if (chan->fax) {
								cmd.driver = ccard->myid;
								cmd.command = ISDN_STAT_FAXIND;
								cmd.arg = chan->No;
								chan->fax->r_code = ISDN_TTY_FAX_SENT;
								ccard->interface.statcallb(&cmd);
							}
							else {
								if (DebugVar & 1)
									printk(KERN_DEBUG "idi_ack: Sent with NULL fax struct, ERROR\n");
							}
						}
					}
#endif
				}
				chan->queued -= chan->waitq;
				if (chan->queued < 0) chan->queued = 0;
				break;
			default:
				if (DebugVar & 16)
					printk(KERN_DEBUG "idi_ack: Ch%d: RC OK Id=%d Ch=%d (ref:%d)\n", chan->No,
						ack->RcId, ack->RcCh, ack->Reference);
		}
	}
}

void
idi_handle_ack(eicon_card *ccard, struct sk_buff *skb)
{
	int j;
        eicon_RC *ack = (eicon_RC *)skb->data;
	eicon_chan *chan;
	isdn_ctrl cmd;
	int dCh = -1;

	if ((chan = ccard->IdTable[ack->RcId]) != NULL)
		dCh = chan->No;
	

	switch (ack->Rc) {
		case OK_FC:
		case N_FLOW_CONTROL:
		case ASSIGN_RC:
			if (DebugVar & 1)
				printk(KERN_ERR "idi_ack: Ch%d: unhandled RC 0x%x\n",
					dCh, ack->Rc);
			break;
		case READY_INT:
		case TIMER_INT:
			/* we do nothing here */
			break;

		case OK:
			if (!chan) {
				if (DebugVar & 1)
					printk(KERN_ERR "idi_ack: Ch%d: OK on chan without Id\n", dCh);
				break;
			}
			idi_handle_ack_ok(ccard, chan, ack);
			break;

		case ASSIGN_OK:
			if (chan) {
				if (DebugVar & 1)
					printk(KERN_ERR "idi_ack: Ch%d: ASSIGN-OK on chan already assigned (%x,%x)\n",
						chan->No, chan->e.D3Id, chan->e.B2Id);
			}
			for(j = 0; j < ccard->nchannels + 1; j++) {
				if (ccard->bch[j].e.ref == ack->Reference) {
					if (!ccard->bch[j].e.ReqCh) 
						ccard->bch[j].e.D3Id  = ack->RcId;
					else
						ccard->bch[j].e.B2Id  = ack->RcId;
					ccard->IdTable[ack->RcId] = &ccard->bch[j];
					ccard->bch[j].e.busy = 0;
					ccard->bch[j].e.ref = 0;
					if (DebugVar & 16)
						printk(KERN_DEBUG"idi_ack: Ch%d: Id %x assigned (%s)\n", j, 
							ack->RcId, (ccard->bch[j].e.ReqCh)? "Net":"Sig");
					break;
				}
			}		
			if (j > ccard->nchannels) {
				if (DebugVar & 24)
					printk(KERN_DEBUG"idi_ack: Ch??: ref %d not found for Id %d\n", 
						ack->Reference, ack->RcId);
			}
			break;

		case OUT_OF_RESOURCES:
		case UNKNOWN_COMMAND:
		case WRONG_COMMAND:
		case WRONG_ID:
		case WRONG_CH:
		case UNKNOWN_IE:
		case WRONG_IE:
		default:
			chan->e.busy = 0;
			if (DebugVar & 24)
				printk(KERN_ERR "eicon_ack: Ch%d: Not OK: Rc=%d Id=%d Ch=%d\n", dCh, 
					ack->Rc, ack->RcId, ack->RcCh);
			if (dCh == ccard->nchannels) { /* Management */
				chan->fsm_state = 2;
			} else if (dCh >= 0) {
					/* any other channel */
					/* card reports error: we hangup */
				idi_hangup(ccard, chan);
				cmd.driver = ccard->myid;
				cmd.command = ISDN_STAT_DHUP;
				cmd.arg = chan->No;
				ccard->interface.statcallb(&cmd);
			}
	}
	if (chan)
		chan->e.busy = 0;
	dev_kfree_skb(skb);
	eicon_schedule_tx(ccard);
}

int
idi_send_data(eicon_card *card, eicon_chan *chan, int ack, struct sk_buff *skb, int que)
{
        struct sk_buff *xmit_skb;
        struct sk_buff *skb2;
        eicon_REQ *reqbuf;
        eicon_chan_ptr *chan2;
        int len, plen = 0, offset = 0;
	unsigned long flags;

        if (chan->fsm_state != EICON_STATE_ACTIVE) {
		if (DebugVar & 1)
			printk(KERN_DEBUG"idi_snd: Ch%d: send bytes on state %d !\n", chan->No, chan->fsm_state);
                return -ENODEV;
	}

        len = skb->len;
	if (len > 2138)	/* too much for the shared memory */
		return -1;
        if (!len)
                return 0;
	if (chan->queued + len > ((chan->l2prot == ISDN_PROTO_L2_TRANS) ? 4000 : EICON_MAX_QUEUED))
		return 0;
	if (DebugVar & 128)
		printk(KERN_DEBUG"idi_snd: Ch%d: %d bytes\n", chan->No, len);

	save_flags(flags);
	cli();
	while(offset < len) {

		plen = ((len - offset) > 270) ? 270 : len - offset;

	        xmit_skb = alloc_skb(plen + sizeof(eicon_REQ), GFP_ATOMIC);
        	skb2 = alloc_skb(sizeof(eicon_chan_ptr), GFP_ATOMIC);

	        if ((!xmit_skb) || (!skb2)) {
			restore_flags(flags);
			if (DebugVar & 1)
	        	        printk(KERN_WARNING "idi_err: Ch%d: alloc_skb failed in send_data()\n", chan->No);
			if (xmit_skb) 
				dev_kfree_skb(skb);
			if (skb2) 
				dev_kfree_skb(skb2);
                	return -ENOMEM;
	        }

	        chan2 = (eicon_chan_ptr *)skb_put(skb2, sizeof(eicon_chan_ptr));
        	chan2->ptr = chan;

	        reqbuf = (eicon_REQ *)skb_put(xmit_skb, plen + sizeof(eicon_REQ));
		if (((len - offset) > 270) &&
			(chan->l2prot != ISDN_PROTO_L2_TRANS)) {
		        reqbuf->Req = IDI_N_MDATA;
		} else {
		        reqbuf->Req = IDI_N_DATA;
			if (ack) reqbuf->Req |= N_D_BIT;
		}	
        	reqbuf->ReqCh = 0;
	        reqbuf->ReqId = 1;
		memcpy(&reqbuf->XBuffer.P, skb->data + offset, plen);
		reqbuf->XBuffer.length = plen;
		reqbuf->Reference = 1; /* Net Entity */

		skb_queue_tail(&chan->e.X, xmit_skb);
		skb_queue_tail(&card->sndq, skb2); 

		offset += plen;
	}
	if (que)
		chan->queued += len;
	restore_flags(flags);
	eicon_schedule_tx(card);
        dev_kfree_skb(skb);
        return len;
}



int
eicon_idi_manage_assign(eicon_card *card)
{
        struct sk_buff *skb;
        struct sk_buff *skb2;
        eicon_REQ  *reqbuf;
        eicon_chan     *chan;
        eicon_chan_ptr *chan2;

        chan = &(card->bch[card->nchannels]);

        skb = alloc_skb(270 + sizeof(eicon_REQ), GFP_ATOMIC);
        skb2 = alloc_skb(sizeof(eicon_chan_ptr), GFP_ATOMIC);

        if ((!skb) || (!skb2)) {
		if (DebugVar & 1)
			printk(KERN_WARNING "idi_err: alloc_skb failed in manage_assign()\n");
		if (skb) 
			dev_kfree_skb(skb);
		if (skb2) 
			dev_kfree_skb(skb2);
                return -ENOMEM;
        }

        chan2 = (eicon_chan_ptr *)skb_put(skb2, sizeof(eicon_chan_ptr));
        chan2->ptr = chan;

        reqbuf = (eicon_REQ *)skb_put(skb, 270 + sizeof(eicon_REQ));

        reqbuf->XBuffer.P[0] = 0;
        reqbuf->Req = ASSIGN;
        reqbuf->ReqCh = 0;
        reqbuf->ReqId = 0xe0;
        reqbuf->XBuffer.length = 1;
        reqbuf->Reference = 2; /* Man Entity */

        skb_queue_tail(&chan->e.X, skb);
        skb_queue_tail(&card->sndq, skb2);
        eicon_schedule_tx(card);
        return(0);
}


int
eicon_idi_manage_remove(eicon_card *card)
{
        struct sk_buff *skb;
        struct sk_buff *skb2;
        eicon_REQ  *reqbuf;
        eicon_chan     *chan;
        eicon_chan_ptr *chan2;

        chan = &(card->bch[card->nchannels]);

        skb = alloc_skb(270 + sizeof(eicon_REQ), GFP_ATOMIC);
        skb2 = alloc_skb(sizeof(eicon_chan_ptr), GFP_ATOMIC);

        if ((!skb) || (!skb2)) {
		if (DebugVar & 1)
                	printk(KERN_WARNING "idi_err: alloc_skb failed in manage_remove()\n");
		if (skb) 
			dev_kfree_skb(skb);
		if (skb2) 
			dev_kfree_skb(skb2);
                return -ENOMEM;
        }

        chan2 = (eicon_chan_ptr *)skb_put(skb2, sizeof(eicon_chan_ptr));
        chan2->ptr = chan;

        reqbuf = (eicon_REQ *)skb_put(skb, 270 + sizeof(eicon_REQ));

        reqbuf->Req = REMOVE;
        reqbuf->ReqCh = 0;
        reqbuf->ReqId = 1;
        reqbuf->XBuffer.length = 0;
        reqbuf->Reference = 2; /* Man Entity */

        skb_queue_tail(&chan->e.X, skb);
        skb_queue_tail(&card->sndq, skb2);
        eicon_schedule_tx(card);
        return(0);
}


int
eicon_idi_manage(eicon_card *card, eicon_manifbuf *mb)
{
	int l = 0;
	int ret = 0;
	int timeout;
	int i;
        struct sk_buff *skb;
        struct sk_buff *skb2;
        eicon_REQ  *reqbuf;
        eicon_chan     *chan;
        eicon_chan_ptr *chan2;

        chan = &(card->bch[card->nchannels]);

	if (chan->e.D3Id)
		return -EBUSY;
	chan->e.D3Id = 1;
	while((skb2 = skb_dequeue(&chan->e.X)))
		dev_kfree_skb(skb2);
	chan->e.busy = 0;
 
	if ((ret = eicon_idi_manage_assign(card))) {
		chan->e.D3Id = 0;
		return(ret); 
	}

        timeout = jiffies + 50;
        while (timeout > jiffies) {
                if (chan->e.B2Id) break;
                SLEEP(10);
        }
        if (!chan->e.B2Id) {
		chan->e.D3Id = 0;
		return -EIO;
	}

	chan->fsm_state = 0;

	if (!(manbuf = kmalloc(sizeof(eicon_manifbuf), GFP_KERNEL))) {
		if (DebugVar & 1)
                	printk(KERN_WARNING "idi_err: alloc_manifbuf failed\n");
		chan->e.D3Id = 0;
		return -ENOMEM;
	}
	if (copy_from_user(manbuf, mb, sizeof(eicon_manifbuf))) {
		kfree(manbuf);
		chan->e.D3Id = 0;
		return -EFAULT;
	}

        skb = alloc_skb(270 + sizeof(eicon_REQ), GFP_ATOMIC);
        skb2 = alloc_skb(sizeof(eicon_chan_ptr), GFP_ATOMIC);

        if ((!skb) || (!skb2)) {
		if (DebugVar & 1)
                	printk(KERN_WARNING "idi_err_manif: alloc_skb failed in manage()\n");
		if (skb) 
			dev_kfree_skb(skb);
		if (skb2) 
			dev_kfree_skb(skb2);
		kfree(manbuf);
		chan->e.D3Id = 0;
                return -ENOMEM;
        }

        chan2 = (eicon_chan_ptr *)skb_put(skb2, sizeof(eicon_chan_ptr));
        chan2->ptr = chan;

        reqbuf = (eicon_REQ *)skb_put(skb, 270 + sizeof(eicon_REQ));

        reqbuf->XBuffer.P[l++] = ESC;
        reqbuf->XBuffer.P[l++] = 6;
        reqbuf->XBuffer.P[l++] = 0x80;
	for (i = 0; i < manbuf->length[0]; i++)
	        reqbuf->XBuffer.P[l++] = manbuf->data[i];
        reqbuf->XBuffer.P[1] = manbuf->length[0] + 1;

        reqbuf->XBuffer.P[l++] = 0;
        reqbuf->Req = (manbuf->count) ? manbuf->count : 0x02; /* Request */
        reqbuf->ReqCh = 0;
        reqbuf->ReqId = 1;
        reqbuf->XBuffer.length = l;
        reqbuf->Reference = 2; /* Man Entity */

        skb_queue_tail(&chan->e.X, skb);
        skb_queue_tail(&card->sndq, skb2);

	manbuf->count = 0;
	manbuf->pos = 0;

        eicon_schedule_tx(card);

        timeout = jiffies + 50;
        while (timeout > jiffies) {
                if (chan->fsm_state) break;
                SLEEP(10);
        }
        if ((!chan->fsm_state) || (chan->fsm_state == 2)) {
		eicon_idi_manage_remove(card);
		kfree(manbuf);
		chan->e.D3Id = 0;
		return -EIO;
	}

	if ((ret = eicon_idi_manage_remove(card))) {
		kfree(manbuf);
		chan->e.D3Id = 0;
		return(ret);
	}

	if (copy_to_user(mb, manbuf, sizeof(eicon_manifbuf))) {
		kfree(manbuf);
		chan->e.D3Id = 0;
		return -EFAULT;
	}

	kfree(manbuf);
	chan->e.D3Id = 0;
  return(0);
}
