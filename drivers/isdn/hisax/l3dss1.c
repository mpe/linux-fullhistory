/* $Id: l3dss1.c,v 2.7 1998/02/12 23:08:01 keil Exp $

 * EURO/DSS1 D-channel protocol
 *
 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *              based on the teles driver from Jan den Ouden
 *
 * Thanks to    Jan den Ouden
 *              Fritz Elfert
 *
 * $Log: l3dss1.c,v $
 * Revision 2.7  1998/02/12 23:08:01  keil
 * change for 2.1.86 (removing FREE_READ/FREE_WRITE from [dev]_kfree_skb()
 *
 * Revision 2.6  1998/02/03 23:26:35  keil
 * V110 extensions from Thomas Pfeiffer
 *
 * Revision 2.5  1998/02/02 13:34:28  keil
 * Support australian Microlink net and german AOCD
 *
 * Revision 2.4  1997/11/06 17:12:25  keil
 * KERN_NOTICE --> KERN_INFO
 *
 * Revision 2.3  1997/10/29 19:03:01  keil
 * changes for 2.1
 *
 * Revision 2.2  1997/08/07 17:44:36  keil
 * Fix RESTART
 *
 * Revision 2.1  1997/08/03 14:36:33  keil
 * Implement RESTART procedure
 *
 * Revision 2.0  1997/07/27 21:15:43  keil
 * New Callref based layer3
 *
 * Revision 1.17  1997/06/26 11:11:46  keil
 * SET_SKBFREE now on creation of a SKB
 *
 * Revision 1.15  1997/04/17 11:50:48  keil
 * pa->loc was undefined, if it was not send by the exchange
 *
 * Old log removed /KKe
 *
 */

#define __NO_VERSION__
#include "hisax.h"
#include "isdnl3.h"
#include "l3dss1.h"
#include <linux/ctype.h>

extern char *HiSax_getrev(const char *revision);
const char *dss1_revision = "$Revision: 2.7 $";

#define EXT_BEARER_CAPS 1

#define	MsgHead(ptr, cref, mty) \
	*ptr++ = 0x8; \
	*ptr++ = 0x1; \
	*ptr++ = cref^0x80; \
	*ptr++ = mty


#ifdef HISAX_DE_AOC
static void
l3dss1_parse_facility(struct l3_process *pc, u_char *p)
{
	int qd_len = 0;
	char tmp[32];

	p++;
	qd_len = *p++;
	if (qd_len == 0) {
		l3_debug(pc->st, "qd_len == 0");
		return;
	}
	if((*p & 0x1F) != 0x11) {	/* Service discriminator, supplementary service */
		l3_debug(pc->st, "supplementary service != 0x11");
		return;
	}
	while(qd_len > 0 && !(*p & 0x80)) {	/* extension ? */
		p++; qd_len--;
	} 
	if(qd_len < 2) {
		l3_debug(pc->st, "qd_len < 2");
		return;
	}
	p++; qd_len--;
	if((*p & 0xE0) != 0xA0) {	/* class and form */
		l3_debug(pc->st, "class and form != 0xA0");
		return;
	}
	switch(*p & 0x1F) {		/* component tag */
	    case 1: /* invoke */
		{
		    unsigned char nlen, ilen;
		    int ident;
    
		    p++; qd_len--;
		    if(qd_len < 1) {
			    l3_debug(pc->st, "qd_len < 1");
			    break;
		    }
		    if(*p & 0x80) { /* length format */
			    l3_debug(pc->st, "*p & 0x80 length format");
			    break;
		    }
		    nlen = *p++; qd_len--;
		    if(qd_len < nlen) {
			    l3_debug(pc->st, "qd_len < nlen");
			    return;
		    }
		    qd_len -= nlen;
    
		    if(nlen < 2) {
			    l3_debug(pc->st, "nlen < 2");
			    return;
		    }
		    if(*p != 0x02) {	/* invoke identifier tag */
			    l3_debug(pc->st, "invoke identifier tag !=0x02");
			    return;
		    }
		    p++; nlen--;
		    if(*p & 0x80) { /* length format */
			    l3_debug(pc->st, "*p & 0x80 length format 2");
			    break;
		    }
		    ilen = *p++; nlen--;
		    if(ilen > nlen || ilen == 0) {
			    l3_debug(pc->st, "ilen > nlen || ilen == 0");
			    return;
		    }
		    nlen -= ilen;
		    ident = 0;
		    while(ilen > 0) {
			    ident = (ident << 8) | (*p++ & 0xFF);	/* invoke identifier */
			    ilen--;
		    }
    
		    if(nlen < 2) {
			    l3_debug(pc->st, "nlen < 2 22");
			    return;
		    }
		    if(*p != 0x02)	{	/* operation value */ 
			    l3_debug(pc->st, "operation value !=0x02");
			    return;
		    }
		    p++; nlen--;
		    ilen = *p++; nlen--;
		    if(ilen > nlen || ilen == 0) {
			    l3_debug(pc->st, "ilen > nlen || ilen == 0 22");
			    return;
		    }
		    nlen -= ilen;
		    ident = 0;
		    while(ilen > 0) {
			    ident = (ident << 8) | (*p++ & 0xFF);
			    ilen--;
		    }
    
    #define FOO1(s,a,b) \
	    while(nlen > 1) {		\
		    int ilen = p[1];	\
		    if(nlen < ilen+2) {	\
			    l3_debug(pc->st, "FOO1  nlen < ilen+2"); \
			    return;		\
		    }			\
		    nlen -= ilen+2;		\
		    if((*p & 0xFF) == (a)) {	\
			    int nlen = ilen;	\
			    p += 2;		\
			    b;		\
		    } else {		\
			    p += ilen+2;	\
		    }			\
	    }
			    
		    switch(ident) {
		    default:
			    break;
		    case 0x22: /* during */
			    FOO1("1A",0x30,FOO1("1C",0xA1,FOO1("1D",0x30,FOO1("1E",0x02,({
				    ident = 0;
				    while(ilen > 0) {
					    ident = (ident<<8) | *p++;
					    ilen--;
				    }
				    if (ident > pc->para.chargeinfo) {
					    pc->para.chargeinfo = ident;
					    pc->st->l3.l3l4(pc, CC_INFO_CHARGE, NULL);
				    }
				    if (pc->st->l3.debug & L3_DEB_CHARGE) {
					    if (*(p+2) == 0) {
						    sprintf(tmp, "charging info during %d", pc->para.chargeinfo);
						    l3_debug(pc->st, tmp);
					    }
					    else {
					    sprintf(tmp, "charging info final %d", pc->para.chargeinfo);
					    l3_debug(pc->st, tmp);
					    }
				    }
			    })))))
			    break;
		    case 0x24: /* final */
			    FOO1("2A",0x30,FOO1("2B",0x30,FOO1("2C",0xA1,FOO1("2D",0x30,FOO1("2E",0x02,({
				    ident = 0;
				    while(ilen > 0) {
					    ident = (ident<<8) | *p++;
					    ilen--;
				    }
				    if (ident > pc->para.chargeinfo) {
					    pc->para.chargeinfo = ident;
					    pc->st->l3.l3l4(pc, CC_INFO_CHARGE, NULL);
				    }
				    if (pc->st->l3.debug & L3_DEB_CHARGE) {
					    sprintf(tmp, "charging info final %d", pc->para.chargeinfo);
					    l3_debug(pc->st, tmp);
				    }
			    }))))))
		    break;
		    }
    #undef FOO1
    
		}
	    break;
	    case 2: /* return result */
		    l3_debug(pc->st, "return result break");
		    break;
	    case 3: /* return error */
		    l3_debug(pc->st, "return error break");
		    break;
	    default:
		    l3_debug(pc->st, "default break");
		    break;
	}
}
#endif	

static int 
l3dss1_check_messagetype_validity(int mt) {
/* verify if a message type exists */
	switch(mt) {
		case MT_ALERTING:
		case MT_CALL_PROCEEDING:
		case MT_CONNECT:
		case MT_CONNECT_ACKNOWLEDGE:
		case MT_PROGRESS:
		case MT_SETUP:
		case MT_SETUP_ACKNOWLEDGE:
		case MT_RESUME:
		case MT_RESUME_ACKNOWLEDGE:
		case MT_RESUME_REJECT:
		case MT_SUSPEND:
		case MT_SUSPEND_ACKNOWLEDGE:
		case MT_SUSPEND_REJECT:
		case MT_USER_INFORMATION:
		case MT_DISCONNECT:
		case MT_RELEASE:
		case MT_RELEASE_COMPLETE:
		case MT_RESTART:
		case MT_RESTART_ACKNOWLEDGE:
		case MT_SEGMENT:
		case MT_CONGESTION_CONTROL:
		case MT_INFORMATION:
		case MT_FACILITY:
		case MT_NOTIFY:
		case MT_STATUS:
		case MT_STATUS_ENQUIRY:
			return(1);
		default:
			return(0);
	}
	return(0);
}

static void
l3dss1_message(struct l3_process *pc, u_char mt)
{
	struct sk_buff *skb;
	u_char *p;

	if (!(skb = l3_alloc_skb(4)))
		return;
	p = skb_put(skb, 4);
	MsgHead(p, pc->callref, mt);
	pc->st->l3.l3l2(pc->st, DL_DATA, skb);
}

static void
l3dss1_release_req(struct l3_process *pc, u_char pr, void *arg)
{
	StopAllL3Timer(pc);
	newl3state(pc, 19);
	l3dss1_message(pc, MT_RELEASE);
	L3AddTimer(&pc->timer, T308, CC_T308_1);
}

static void
l3dss1_release_cmpl(struct l3_process *pc, u_char pr, void *arg)
{
	u_char *p;
	struct sk_buff *skb = arg;
	int cause = -1;

	p = skb->data;
	pc->para.loc = 0;
	if ((p = findie(p, skb->len, IE_CAUSE, 0))) {
		p++;
		if (*p++ == 2)
			pc->para.loc = *p++;
		cause = *p & 0x7f;
	}
	dev_kfree_skb(skb);
	StopAllL3Timer(pc);
	pc->para.cause = cause;
	newl3state(pc, 0);
	pc->st->l3.l3l4(pc, CC_RELEASE_CNF, NULL);
	release_l3_process(pc);
}

#ifdef EXT_BEARER_CAPS

u_char *EncodeASyncParams(u_char *p, u_char si2)
{ // 7c 06 88  90 21 42 00 bb

  p[0] = p[1] = 0; p[2] = 0x80;
  if (si2 & 32) // 7 data bits
    p[2] += 16;
  else          // 8 data bits
    p[2] +=24;

  if (si2 & 16) // 2 stop bits
    p[2] += 96;
  else          // 1 stop bit
    p[2] = 32;

  if (si2 & 8)  // even parity
    p[2] += 2;
  else          // no parity
    p[2] += 3;

  switch (si2 & 0x07)
  {
    case 0:     p[0] = 66;      // 1200 bit/s
                break;
    case 1:     p[0] = 88;      // 1200/75 bit/s
                break;
    case 2:     p[0] = 87;      // 75/1200 bit/s
                break;
    case 3:     p[0] = 67;      // 2400 bit/s
                break;
    case 4:     p[0] = 69;      // 4800 bit/s
                break;
    case 5:     p[0] = 72;      // 9600 bit/s
                break;
    case 6:     p[0] = 73;      // 14400 bit/s
                break;
    case 7:     p[0] = 75;      // 19200 bit/s
                break;
  }
  return p+3;
}

u_char EncodeSyncParams(u_char si2, u_char ai)
{

  switch (si2)
  {
    case 0:     return ai + 2;  // 1200 bit/s
    case 1:     return ai + 24; // 1200/75 bit/s
    case 2:     return ai + 23; // 75/1200 bit/s
    case 3:     return ai + 3;  // 2400 bit/s
    case 4:     return ai + 5;  // 4800 bit/s
    case 5:     return ai + 8;  // 9600 bit/s
    case 6:     return ai + 9;  // 14400 bit/s
    case 7:     return ai + 11; // 19200 bit/s
    case 8:     return ai + 14; // 48000 bit/s
    case 9:     return ai + 15; // 56000 bit/s
    case 15:    return ai + 40; // negotiate bit/s
    default:    break;
  }
  return ai;
}


static u_char DecodeASyncParams(u_char si2, u_char *p)
{ u_char info;

  switch (p[5])
  {
    case 66: // 1200 bit/s
             break; // si2 bleibt gleich
    case 88: // 1200/75 bit/s
             si2 += 1;
             break;
    case 87: // 75/1200 bit/s
             si2 += 2;
             break;
    case 67: // 2400 bit/s
             si2 += 3;
             break;
    case 69: // 4800 bit/s
             si2 += 4;
             break;
    case 72: // 9600 bit/s
             si2 += 5;
             break;
    case 73: // 14400 bit/s
             si2 += 6;
             break;
    case 75: // 19200 bit/s
             si2 += 7;
             break;
  }

  info = p[7] & 0x7f;
  if ((info & 16) && (!(info & 8)))   // 7 data bits
    si2 += 32;                        // else 8 data bits
  if ((info & 96) == 96)              // 2 stop bits
    si2 += 16;                        // else 1 stop bit
  if ((info & 2) && (!(info & 1)))    // even parity
    si2 += 8;                         // else no parity

  return si2;
}


static u_char DecodeSyncParams(u_char si2, u_char info)
{
  info &= 0x7f;
  switch (info)
  {
    case 40: // bit/s aushandeln  --- hat nicht geklappt, ai wird 165 statt 175!
      return si2 + 15;
    case 15: // 56000 bit/s --- hat nicht geklappt, ai wird 0 statt 169 !
      return si2 + 9;
    case 14: // 48000 bit/s
      return si2 + 8;
    case 11: // 19200 bit/s
      return si2 + 7;
    case 9:  // 14400 bit/s
      return si2 + 6;
    case 8:  // 9600  bit/s
      return si2 + 5;
    case 5:  // 4800  bit/s
      return si2 + 4;
    case 3:  // 2400  bit/s
      return si2 + 3;
    case 23: // 75/1200 bit/s
      return si2 + 2;
    case 24: // 1200/75 bit/s
      return si2 + 1;
    default: // 1200 bit/s
      return si2;
  }
}

static u_char DecodeSI2(struct sk_buff *skb)
{ u_char *p; //, *pend=skb->data + skb->len;

        if ((p = findie(skb->data, skb->len, 0x7c, 0)))
        {
          switch (p[4] & 0x0f)
          {
            case 0x01:  if (p[1] == 0x04) // sync. Bitratenadaption
                          return DecodeSyncParams(160, p[5]); // V.110/X.30
                        else if (p[1] == 0x06) // async. Bitratenadaption
                          return DecodeASyncParams(192, p);   // V.110/X.30
                        break;
            case 0x08:  // if (p[5] == 0x02) // sync. Bitratenadaption
                          return DecodeSyncParams(176, p[5]); // V.120
                        break;
          }
        }
        return 0;
}

#endif


static void
l3dss1_setup_req(struct l3_process *pc, u_char pr,
		 void *arg)
{
	struct sk_buff *skb;
	u_char tmp[128];
	u_char *p = tmp;
	u_char channel = 0;
	u_char screen = 0x80;
	u_char *teln;
	u_char *msn;
	u_char *sub;
	u_char *sp;
	int l;

	MsgHead(p, pc->callref, MT_SETUP);

	/*
	 * Set Bearer Capability, Map info from 1TR6-convention to EDSS1
	 */
#ifdef HISAX_EURO_SENDCOMPLETE
	*p++ = 0xa1;		/* complete indicator */
#endif
	switch (pc->para.setup.si1) {
		case 1:	/* Telephony                               */
			*p++ = 0x4;	/* BC-IE-code                              */
			*p++ = 0x3;	/* Length                                  */
			*p++ = 0x90;	/* Coding Std. CCITT, 3.1 kHz audio     */
			*p++ = 0x90;	/* Circuit-Mode 64kbps                     */
			*p++ = 0xa3;	/* A-Law Audio                             */
			break;
		case 5:	/* Datatransmission 64k, BTX               */
		case 7:	/* Datatransmission 64k                    */
		default:
			*p++ = 0x4;	/* BC-IE-code                              */
			*p++ = 0x2;	/* Length                                  */
			*p++ = 0x88;	/* Coding Std. CCITT, unrestr. dig. Inform. */
			*p++ = 0x90;	/* Circuit-Mode 64kbps                      */
			break;
	}
	/*
	 * What about info2? Mapping to High-Layer-Compatibility?
	 */
	teln = pc->para.setup.phone;
	if (*teln) {
		/* parse number for special things */
		if (!isdigit(*teln)) {
			switch (0x5f & *teln) {
				case 'C':
					channel = 0x08;
				case 'P':
					channel |= 0x80;
					teln++;
					if (*teln == '1')
						channel |= 0x01;
					else
						channel |= 0x02;
					break;
				case 'R':
					screen = 0xA0;
					break;
				case 'D':
					screen = 0x80;
					break;
				default:
					if (pc->debug & L3_DEB_WARN)
						l3_debug(pc->st, "Wrong MSN Code");
					break;
			}
			teln++;
		}
	}
	if (channel) {
		*p++ = IE_CHANNEL_ID;
		*p++ = 1;
		*p++ = channel;
	}
	msn = pc->para.setup.eazmsn;
	sub = NULL;
	sp = msn;
	while (*sp) { 
		if ('.' == *sp) {
			sub = sp;
			*sp = 0;
		} else 
			sp++;
	}
	if (*msn) {
		*p++ = 0x6c;
		*p++ = strlen(msn) + (screen ? 2 : 1);
		/* Classify as AnyPref. */
		if (screen) {
			*p++ = 0x01;	/* Ext = '0'B, Type = '000'B, Plan = '0001'B. */
			*p++ = screen;
		} else
			*p++ = 0x81;	/* Ext = '1'B, Type = '000'B, Plan = '0001'B. */
		while (*msn)
			*p++ = *msn++ & 0x7f;
	}
	if (sub) {
		*sub++ = '.';
		*p++ = 0x6d; /* Calling party subaddress */
 		*p++ = strlen(sub) + 2;
		*p++ = 0x80;	/* NSAP coded */
		*p++ = 0x50;	/* local IDI format */
 		while (*sub)
			*p++ = *sub++ & 0x7f;
	}
	sub = NULL;
	sp = teln;
	while (*sp) { 
		if ('.' == *sp) {
			sub = sp;
			*sp = 0;
		} else 
			sp++;
	}
	*p++ = 0x70;
	*p++ = strlen(teln) + 1;
	/* Classify as AnyPref. */
	*p++ = 0x81;		/* Ext = '1'B, Type = '000'B, Plan = '0001'B. */
	while (*teln)
		*p++ = *teln++ & 0x7f;

	if (sub) {
		*sub++ = '.';
		*p++ = 0x71; /* Called party subaddress */
 		*p++ = strlen(sub) + 2;
		*p++ = 0x80;	/* NSAP coded */
		*p++ = 0x50;	/* local IDI format */
 		while (*sub)
			*p++ = *sub++ & 0x7f;
	}

#ifdef EXT_BEARER_CAPS
        if ((pc->para.setup.si2 >= 160) && (pc->para.setup.si2 <= 175))
        { // sync. Bitratenadaption, V.110/X.30
          *p++ = 0x7c; *p++ = 0x04; *p++ = 0x88; *p++ = 0x90; *p++ = 0x21;
          *p++ = EncodeSyncParams(pc->para.setup.si2 - 160, 0x80);
        }
        else if ((pc->para.setup.si2 >= 176) && (pc->para.setup.si2 <= 191))
        { // sync. Bitratenadaption, V.120
          *p++ = 0x7c; *p++ = 0x05; *p++ = 0x88; *p++ = 0x90; *p++ = 0x28;
          *p++ = EncodeSyncParams(pc->para.setup.si2 - 176, 0);
          *p++ = 0x82;
        }
        else if (pc->para.setup.si2 >= 192)
        { // async. Bitratenadaption, V.110/X.30
          *p++ = 0x7c; *p++ = 0x06; *p++ = 0x88; *p++ = 0x90; *p++ = 0x21;
          p = EncodeASyncParams(p, pc->para.setup.si2 - 192);
        }
#endif
	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	L3DelTimer(&pc->timer);
	L3AddTimer(&pc->timer, T303, CC_T303);
	newl3state(pc, 1);
	pc->st->l3.l3l2(pc->st, DL_DATA, skb);
}

static void
l3dss1_call_proc(struct l3_process *pc, u_char pr, void *arg)
{
	u_char *p;
	struct sk_buff *skb = arg;

	L3DelTimer(&pc->timer);
	p = skb->data;
	if ((p = findie(p, skb->len, IE_CHANNEL_ID, 0))) {
		pc->para.bchannel = p[2] & 0x3;
		if ((!pc->para.bchannel) && (pc->debug & L3_DEB_WARN))
			l3_debug(pc->st, "setup answer without bchannel");
	} else if (pc->debug & L3_DEB_WARN)
		l3_debug(pc->st, "setup answer without bchannel");
	dev_kfree_skb(skb);
	newl3state(pc, 3);
	L3AddTimer(&pc->timer, T310, CC_T310);
	pc->st->l3.l3l4(pc, CC_PROCEEDING_IND, NULL);
}

static void
l3dss1_setup_ack(struct l3_process *pc, u_char pr, void *arg)
{
	u_char *p;
	struct sk_buff *skb = arg;

	L3DelTimer(&pc->timer);
	p = skb->data;
	if ((p = findie(p, skb->len, IE_CHANNEL_ID, 0))) {
		pc->para.bchannel = p[2] & 0x3;
		if ((!pc->para.bchannel) && (pc->debug & L3_DEB_WARN))
			l3_debug(pc->st, "setup answer without bchannel");
	} else if (pc->debug & L3_DEB_WARN)
		l3_debug(pc->st, "setup answer without bchannel");
	dev_kfree_skb(skb);
	newl3state(pc, 2);
	L3AddTimer(&pc->timer, T304, CC_T304);
	pc->st->l3.l3l4(pc, CC_MORE_INFO, NULL);
}

static void
l3dss1_disconnect(struct l3_process *pc, u_char pr, void *arg)
{
	u_char *p;
	struct sk_buff *skb = arg;
	int cause = -1;

	StopAllL3Timer(pc);
	p = skb->data;
	pc->para.loc = 0;
	if ((p = findie(p, skb->len, IE_CAUSE, 0))) {
		p++;
		if (*p++ == 2)
			pc->para.loc = *p++;
		cause = *p & 0x7f;
	}
	dev_kfree_skb(skb);
	newl3state(pc, 12);
	pc->para.cause = cause;
	pc->st->l3.l3l4(pc, CC_DISCONNECT_IND, NULL);
}

static void
l3dss1_connect(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;

	dev_kfree_skb(skb);
	L3DelTimer(&pc->timer);	/* T310 */
	newl3state(pc, 10);
	pc->para.chargeinfo = 0;
	pc->st->l3.l3l4(pc, CC_SETUP_CNF, NULL);
}

static void
l3dss1_alerting(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;

	dev_kfree_skb(skb);
	L3DelTimer(&pc->timer);	/* T304 */
	newl3state(pc, 4);
	pc->st->l3.l3l4(pc, CC_ALERTING_IND, NULL);
}

static void
l3dss1_msg_without_setup(struct l3_process *pc, u_char pr, void *arg)
{
  /* This routine is called if here was no SETUP made (checks in dss1up and in
   * l3dss1_setup) and a RELEASE_COMPLETE have to be sent with an error code
   * It is called after it is veryfied that Layer2 is up.
   * The cause value is allready in pc->para.cause
   * MT_STATUS_ENQUIRE in the NULL state is handled too
   */
	u_char tmp[16];
	u_char *p=tmp;
	int l;
	struct sk_buff *skb;

	switch (pc->para.cause) {
	  case  81: /* 0x51 invalid callreference */
	  case  96: /* 0x60 mandory IE missing */
	  case 101: /* 0x65 incompatible Callstate */
	  	MsgHead(p, pc->callref, MT_RELEASE_COMPLETE);
		*p++ = IE_CAUSE;
		*p++ = 0x2;
		*p++ = 0x80;
		*p++ = pc->para.cause | 0x80;
		break;
	  default:
	  	printk(KERN_ERR "HiSax internal error l3dss1_msg_without_setup\n");
	  	return;
	}	
	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	pc->st->l3.l3l2(pc->st, DL_DATA, skb);
	release_l3_process(pc);
}

static void
l3dss1_setup(struct l3_process *pc, u_char pr, void *arg)
{
        u_char *p, *ptmp[8];
	int i;
	int bcfound = 0;
	char tmp[80];
	struct sk_buff *skb = arg;

	/* ETS 300-104 1.3.4 and 1.3.5
	 * we need to detect unknown inform. element from 0 to 7
	 */	
	p = skb->data;
	for(i = 0; i < 8; i++)
		ptmp[i] = skb->data;
	if (findie(ptmp[1], skb->len, 0x01, 0)
	    || findie(ptmp[2], skb->len, 0x02, 0)
	    || findie(ptmp[3], skb->len, 0x03, 0)
	    || findie(ptmp[5], skb->len, 0x05, 0)
	    || findie(ptmp[6], skb->len, 0x06, 0)
	    || findie(ptmp[7], skb->len, 0x07, 0)) {
	  	/* if ie is < 8 and not 0 nor 4, send RELEASE_COMPLETE 
	  	 * cause 0x60
	  	 */
	  	pc->para.cause = 0x60;
		dev_kfree_skb(skb);
		if (pc->state == 0)
			pc->st->l3.l3l4(pc, CC_ESTABLISH, NULL);
		else
			l3dss1_msg_without_setup(pc, pr, NULL);
		return;
	}

	/*
	 * Channel Identification
	 */
	p = skb->data;
	if ((p = findie(p, skb->len, IE_CHANNEL_ID, 0))) {
		pc->para.bchannel = p[2] & 0x3;
		if (pc->para.bchannel)
			bcfound++;
		else if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->st, "setup without bchannel");
	} else if (pc->debug & L3_DEB_WARN)
		l3_debug(pc->st, "setup without bchannel");

	/*
	   * Bearer Capabilities
	 */
	p = skb->data;
	if ((p = findie(p, skb->len, 0x04, 0))) {
		pc->para.setup.si2 = 0;
		switch (p[2] & 0x1f) {
			case 0x00:
				/* Speech */
			case 0x10:
				/* 3.1 Khz audio */
				pc->para.setup.si1 = 1;
				break;
			case 0x08:
				/* Unrestricted digital information */
				pc->para.setup.si1 = 7;
/* JIM, 05.11.97 I wanna set service indicator 2 */
#ifdef EXT_BEARER_CAPS
                                pc->para.setup.si2 = DecodeSI2(skb);
                                printk(KERN_DEBUG "HiSax: SI=%d, AI=%d\n",
                                       pc->para.setup.si1, pc->para.setup.si2);
#endif
				break;
			case 0x09:
				/* Restricted digital information */
				pc->para.setup.si1 = 2;
				break;
			case 0x11:
				/* Unrestr. digital information  with tones/announcements */
				pc->para.setup.si1 = 3;
				break;
			case 0x18:
				/* Video */
				pc->para.setup.si1 = 4;
				break;
			default:
				pc->para.setup.si1 = 0;
		}
	} else {
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->st, "setup without bearer capabilities");
		/* ETS 300-104 1.3.3 */
	  	pc->para.cause = 0x60;
		dev_kfree_skb(skb);
		if (pc->state == 0)
			pc->st->l3.l3l4(pc, CC_ESTABLISH, NULL);
		else
			l3dss1_msg_without_setup(pc, pr, NULL);
		return;
	}

	p = skb->data;
	if ((p = findie(p, skb->len, 0x70, 0)))
		iecpy(pc->para.setup.eazmsn, p, 1);
	else
		pc->para.setup.eazmsn[0] = 0;

	p = skb->data;
	if ((p = findie(p, skb->len, 0x71, 0))) {
		/* Called party subaddress */
		if ((p[1]>=2) && (p[2]==0x80) && (p[3]==0x50)) {
			tmp[0]='.';
			iecpy(&tmp[1], p, 2);
			strcat(pc->para.setup.eazmsn, tmp);
		} else if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->st, "wrong called subaddress");
	}
	p = skb->data;
	if ((p = findie(p, skb->len, 0x6c, 0))) {
		pc->para.setup.plan = p[2];
		if (p[2] & 0x80) {
			iecpy(pc->para.setup.phone, p, 1);
			pc->para.setup.screen = 0;
		} else {
			iecpy(pc->para.setup.phone, p, 2);
			pc->para.setup.screen = p[3];
		}
	} else {
		pc->para.setup.phone[0] = 0;
		pc->para.setup.plan = 0;
		pc->para.setup.screen = 0;
	}
	p = skb->data;
	if ((p = findie(p, skb->len, 0x6d, 0))) {
		/* Calling party subaddress */
		if ((p[1]>=2) && (p[2]==0x80) && (p[3]==0x50)) {
			tmp[0]='.';
			iecpy(&tmp[1], p, 2);
			strcat(pc->para.setup.phone, tmp);
		} else if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->st, "wrong calling subaddress");
	}

	dev_kfree_skb(skb);

	if (bcfound) {
		if ((pc->para.setup.si1 != 7) && (pc->debug & L3_DEB_WARN)) {
			sprintf(tmp, "non-digital call: %s -> %s",
				pc->para.setup.phone, pc->para.setup.eazmsn);
			l3_debug(pc->st, tmp);
		}
		newl3state(pc, 6);
		pc->st->l3.l3l4(pc, CC_SETUP_IND, NULL);
	} else
		release_l3_process(pc);
}

static void
l3dss1_reset(struct l3_process *pc, u_char pr, void *arg)
{
	release_l3_process(pc);
}

static void
l3dss1_setup_rsp(struct l3_process *pc, u_char pr,
		 void *arg)
{
	newl3state(pc, 8);
	l3dss1_message(pc, MT_CONNECT);
	L3DelTimer(&pc->timer);
	L3AddTimer(&pc->timer, T313, CC_T313);
}

static void
l3dss1_connect_ack(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;

	dev_kfree_skb(skb);
	newl3state(pc, 10);
	L3DelTimer(&pc->timer);
	pc->st->l3.l3l4(pc, CC_SETUP_COMPLETE_IND, NULL);
}

static void
l3dss1_disconnect_req(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb;
	u_char tmp[16];
	u_char *p = tmp;
	int l;
	u_char cause = 0x10;

	if (pc->para.cause > 0)
		cause = pc->para.cause;

	StopAllL3Timer(pc);

	MsgHead(p, pc->callref, MT_DISCONNECT);

	*p++ = IE_CAUSE;
	*p++ = 0x2;
	*p++ = 0x80;
	*p++ = cause | 0x80;

	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	newl3state(pc, 11);
	pc->st->l3.l3l2(pc->st, DL_DATA, skb);
	L3AddTimer(&pc->timer, T305, CC_T305);
}

static void
l3dss1_reject_req(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb;
	u_char tmp[16];
	u_char *p = tmp;
	int l;
	u_char cause = 0x95;

	if (pc->para.cause > 0)
		cause = pc->para.cause;

	MsgHead(p, pc->callref, MT_RELEASE_COMPLETE);

	*p++ = IE_CAUSE;
	*p++ = 0x2;
	*p++ = 0x80;
	*p++ = cause;

	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	pc->st->l3.l3l2(pc->st, DL_DATA, skb);
	pc->st->l3.l3l4(pc, CC_RELEASE_IND, NULL);
	newl3state(pc, 0);
	release_l3_process(pc);
}

static void
l3dss1_release(struct l3_process *pc, u_char pr, void *arg)
{
	u_char *p;
	struct sk_buff *skb = arg;
	int cause = -1;

	p = skb->data;
	if ((p = findie(p, skb->len, IE_CAUSE, 0))) {
		p++;
		if (*p++ == 2)
			pc->para.loc = *p++;
		cause = *p & 0x7f;
	}
	p = skb->data;
	if ((p = findie(p, skb->len, IE_FACILITY, 0))) {
#ifdef HISAX_DE_AOC
	    l3dss1_parse_facility(pc,p);
#else
		p = NULL;
#endif
	}
	dev_kfree_skb(skb);
	StopAllL3Timer(pc);
	pc->para.cause = cause;
	l3dss1_message(pc, MT_RELEASE_COMPLETE);
	pc->st->l3.l3l4(pc, CC_RELEASE_IND, NULL);
	newl3state(pc, 0);
	release_l3_process(pc);
}

static void
l3dss1_alert_req(struct l3_process *pc, u_char pr,
		 void *arg)
{
	newl3state(pc, 7);
	l3dss1_message(pc, MT_ALERTING);
}

static void
l3dss1_status_enq(struct l3_process *pc, u_char pr, void *arg)
{
	u_char tmp[16];
	u_char *p = tmp;
	int l;
	struct sk_buff *skb = arg;

	dev_kfree_skb(skb);

	MsgHead(p, pc->callref, MT_STATUS);

	*p++ = IE_CAUSE;
	*p++ = 0x2;
	*p++ = 0x80;
	*p++ = 0x9E;		/* answer status enquire */

	*p++ = 0x14;		/* CallState */
	*p++ = 0x1;
	*p++ = pc->state & 0x3f;

	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	pc->st->l3.l3l2(pc->st, DL_DATA, skb);
}

static void
l3dss1_status_req(struct l3_process *pc, u_char pr, void *arg)
{
  /* ETS 300-104 7.4.1, 8.4.1, 10.3.1, 11.4.1, 12.4.1, 13.4.1, 14.4.1...
     if setup has been made and a non expected message type is received, we must send MT_STATUS cause 0x62  */
        u_char tmp[16];
	u_char *p = tmp;
	int l;
	struct sk_buff *skb = arg;

	dev_kfree_skb(skb);

	MsgHead(p, pc->callref, MT_STATUS);

	*p++ = IE_CAUSE;
	*p++ = 0x2;
	*p++ = 0x80;
	*p++ = 0x62 | 0x80;		/* status sending */

	*p++ = 0x14;		/* CallState */
	*p++ = 0x1;
	*p++ = pc->state & 0x3f;

	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	pc->st->l3.l3l2(pc->st, DL_DATA, skb);
}

static void
l3dss1_release_ind(struct l3_process *pc, u_char pr, void *arg)
{
	u_char *p;
	struct sk_buff *skb = arg;
	int callState = 0;
	p = skb->data;

	if ((p = findie(p, skb->len, IE_CALL_STATE, 0))) {
		p++;
		if (1== *p++)
			callState = *p;
	}
	if(callState == 0) {
		/* ETS 300-104 7.6.1, 8.6.1, 10.6.1... and 16.1
		 * set down layer 3 without sending any message
		 */
		pc->st->l3.l3l4(pc, CC_RELEASE_IND, NULL);
		newl3state(pc, 0);
		release_l3_process(pc);
	} else {
		pc->st->l3.l3l4(pc, CC_IGNORE, NULL);
	}
}

static void
l3dss1_t303(struct l3_process *pc, u_char pr, void *arg)
{
	if (pc->N303 > 0) {
		pc->N303--;
		L3DelTimer(&pc->timer);
		l3dss1_setup_req(pc, pr, arg);
	} else {
		L3DelTimer(&pc->timer);
		pc->st->l3.l3l4(pc, CC_NOSETUP_RSP_ERR, NULL);
		release_l3_process(pc);
	}
}

static void
l3dss1_t304(struct l3_process *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);
	pc->para.cause = 0xE6;
	l3dss1_disconnect_req(pc, pr, NULL);
	pc->st->l3.l3l4(pc, CC_SETUP_ERR, NULL);

}

static void
l3dss1_t305(struct l3_process *pc, u_char pr, void *arg)
{
	u_char tmp[16];
	u_char *p = tmp;
	int l;
	struct sk_buff *skb;
	u_char cause = 0x90;

	L3DelTimer(&pc->timer);
	if (pc->para.cause > 0)
		cause = pc->para.cause | 0x80;

	MsgHead(p, pc->callref, MT_RELEASE);

	*p++ = IE_CAUSE;
	*p++ = 0x2;
	*p++ = 0x80;
	*p++ = cause;

	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	newl3state(pc, 19);
	pc->st->l3.l3l2(pc->st, DL_DATA, skb);
	L3AddTimer(&pc->timer, T308, CC_T308_1);
}

static void
l3dss1_t310(struct l3_process *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);
	pc->para.cause = 0xE6;
	l3dss1_disconnect_req(pc, pr, NULL);
	pc->st->l3.l3l4(pc, CC_SETUP_ERR, NULL);
}

static void
l3dss1_t313(struct l3_process *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);
	pc->para.cause = 0xE6;
	l3dss1_disconnect_req(pc, pr, NULL);
	pc->st->l3.l3l4(pc, CC_CONNECT_ERR, NULL);
}

static void
l3dss1_t308_1(struct l3_process *pc, u_char pr, void *arg)
{
	newl3state(pc, 19);
	L3DelTimer(&pc->timer);
	l3dss1_message(pc, MT_RELEASE);
	L3AddTimer(&pc->timer, T308, CC_T308_2);
}

static void
l3dss1_t308_2(struct l3_process *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);
	pc->st->l3.l3l4(pc, CC_RELEASE_ERR, NULL);
	release_l3_process(pc);
}

static void
l3dss1_restart(struct l3_process *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);
	pc->st->l3.l3l4(pc, CC_DLRL, NULL);
	release_l3_process(pc);
}

static void
l3dss1_status(struct l3_process *pc, u_char pr, void *arg)
{
	u_char *p;
	char tmp[64], *t;
	int l;
	struct sk_buff *skb = arg;
	int cause, callState;

	cause = callState = -1;
	p = skb->data;
	t = tmp;
	if ((p = findie(p, skb->len, IE_CAUSE, 0))) {
		p++;
		l = *p++;
		t += sprintf(t,"Status CR %x Cause:", pc->callref);
		while (l--) {
		        cause = *p;
			t += sprintf(t," %2x",*p++);
		}
	} else
		sprintf(t,"Status CR %x no Cause", pc->callref);
	l3_debug(pc->st, tmp);
	p = skb->data;
	t = tmp;
	t += sprintf(t,"Status state %x ", pc->state);
	if ((p = findie(p, skb->len, IE_CALL_STATE, 0))) {
		p++;
		if (1== *p++) {
		        callState = *p;
			t += sprintf(t,"peer state %x" , *p);
		}
		else
			t += sprintf(t,"peer state len error");
	} else
		sprintf(t,"no peer state");
	l3_debug(pc->st, tmp);
	if(((cause & 0x7f) == 0x6f) && (callState == 0)) {
		/* ETS 300-104 7.6.1, 8.6.1, 10.6.1... 
		 * if received MT_STATUS with cause == 0x6f and call 
		 * state == 0, then we must set down layer 3
		 */
		l3dss1_release_ind(pc, pr, arg);
	} else
		dev_kfree_skb(skb);
}

static void
l3dss1_facility(struct l3_process *pc, u_char pr, void *arg)
{
        u_char *p;
	struct sk_buff *skb = arg;

	p = skb->data;
	if ((p = findie(p, skb->len, IE_FACILITY, 0))) {
#ifdef HISAX_DE_AOC
	    l3dss1_parse_facility(pc,p);
#else
		p = NULL;
#endif
	}
}


	
static void
l3dss1_global_restart(struct l3_process *pc, u_char pr, void *arg)
{
	u_char tmp[32];
	u_char *p;
	u_char ri, chan=0;
	int l;
	struct sk_buff *skb = arg;
	struct l3_process *up;
	
	newl3state(pc, 2);
	L3DelTimer(&pc->timer);
	p = skb->data;
	if ((p = findie(p, skb->len, IE_RESTART_IND, 0))) {
	        ri = p[2];
	        sprintf(tmp, "Restart %x", ri);
	} else {
		sprintf(tmp, "Restart without restart IE");
		ri = 0x86;
	}
	l3_debug(pc->st, tmp);
	p = skb->data;
	if ((p = findie(p, skb->len, IE_CHANNEL_ID, 0))) {
		chan = p[2] & 3;
		sprintf(tmp, "Restart for channel %d", chan);
		l3_debug(pc->st, tmp);
	}
	dev_kfree_skb(skb);
	newl3state(pc, 2);
	up = pc->st->l3.proc;
	while (up) {
		if ((ri & 7)==7)
			up->st->lli.l4l3(up->st, CC_RESTART, up);
		else if (up->para.bchannel == chan)
				up->st->lli.l4l3(up->st, CC_RESTART, up);
		up = up->next;
	}
	p = tmp;
	MsgHead(p, pc->callref, MT_RESTART_ACKNOWLEDGE);
	if (chan) {
		*p++ = IE_CHANNEL_ID;
		*p++ = 1;
		*p++ = chan | 0x80;
	}
	*p++ = 0x79; /* RESTART Ind */
	*p++ = 1;
	*p++ = ri;
	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	newl3state(pc, 0);
	pc->st->l3.l3l2(pc->st, DL_DATA, skb);
}

/* *INDENT-OFF* */
static struct stateentry downstatelist[] =
{
	{SBIT(0),
	 CC_ESTABLISH, l3dss1_msg_without_setup},
	{SBIT(0),
	 CC_SETUP_REQ, l3dss1_setup_req},
	{SBIT(1) | SBIT(2) | SBIT(3) | SBIT(4) | SBIT(6) | SBIT(7) | SBIT(8) | SBIT(10),
	 CC_DISCONNECT_REQ, l3dss1_disconnect_req},
	{SBIT(12),
	 CC_RELEASE_REQ, l3dss1_release_req},
	{ALL_STATES,
	 CC_DLRL, l3dss1_reset},
	{ALL_STATES,
	 CC_RESTART, l3dss1_restart},
	{SBIT(6),
	 CC_IGNORE, l3dss1_reset},
	{SBIT(6),
	 CC_REJECT_REQ, l3dss1_reject_req},
	{SBIT(6),
	 CC_ALERTING_REQ, l3dss1_alert_req},
	{SBIT(6) | SBIT(7),
	 CC_SETUP_RSP, l3dss1_setup_rsp},
	{SBIT(1),
	 CC_T303, l3dss1_t303},
	{SBIT(2),
	 CC_T304, l3dss1_t304},
	{SBIT(3),
	 CC_T310, l3dss1_t310},
	{SBIT(8),
	 CC_T313, l3dss1_t313},
	{SBIT(11),
	 CC_T305, l3dss1_t305},
	{SBIT(19),
	 CC_T308_1, l3dss1_t308_1},
	{SBIT(19),
	 CC_T308_2, l3dss1_t308_2},
};

static int downsllen = sizeof(downstatelist) /
sizeof(struct stateentry);

static struct stateentry datastatelist[] =
{
	{ALL_STATES,
	 MT_STATUS_ENQUIRY, l3dss1_status_enq},
	{ALL_STATES,
	 MT_FACILITY, l3dss1_facility},
	{SBIT(19),
	 MT_STATUS, l3dss1_release_ind},
	{ALL_STATES,
	 MT_STATUS, l3dss1_status},
	{SBIT(0) | SBIT(6),
	 MT_SETUP, l3dss1_setup},
	{SBIT(1) | SBIT(2),
	 MT_CALL_PROCEEDING, l3dss1_call_proc},
	{SBIT(3) | SBIT(4) | SBIT(8) | SBIT(10) | SBIT(11) | SBIT(19),
	 MT_CALL_PROCEEDING, l3dss1_status_req},
	{SBIT(1),
	 MT_SETUP_ACKNOWLEDGE, l3dss1_setup_ack},
	{SBIT(2) | SBIT(3) | SBIT(4) | SBIT(8) | SBIT(10) | SBIT(11) | SBIT(19),
	 MT_SETUP_ACKNOWLEDGE, l3dss1_status_req},
	{SBIT(1) | SBIT(2) | SBIT(3),
	 MT_ALERTING, l3dss1_alerting},
	{SBIT(4) | SBIT(8) | SBIT(10) | SBIT(11) | SBIT(19),
	 MT_ALERTING, l3dss1_status_req},
	{SBIT(0) | SBIT(1) | SBIT(2) | SBIT(3) | SBIT(4) | SBIT(7) | SBIT(8) | SBIT(10) |
	 SBIT(11) | SBIT(12) | SBIT(15) | SBIT(17) | SBIT(19),
	 MT_RELEASE_COMPLETE, l3dss1_release_cmpl},
	{SBIT(1) | SBIT(2) | SBIT(3) | SBIT(4) | SBIT(7) | SBIT(8) | SBIT(10) |
	 SBIT(11) | SBIT(12) | SBIT(15) | SBIT(17) /*| SBIT(19)*/,
	 MT_RELEASE, l3dss1_release},
	{SBIT(19),  MT_RELEASE, l3dss1_release_ind},
	{SBIT(1) | SBIT(2) | SBIT(3) | SBIT(4) | SBIT(7) | SBIT(8) | SBIT(10),
	 MT_DISCONNECT, l3dss1_disconnect},
	{SBIT(11),
	 MT_DISCONNECT, l3dss1_release_req},
	{SBIT(1) | SBIT(2) | SBIT(3) | SBIT(4),
	 MT_CONNECT, l3dss1_connect},
	{SBIT(8) | SBIT(10) | SBIT(11) | SBIT(19),
	 MT_CONNECT, l3dss1_status_req},
	{SBIT(1) | SBIT(2) | SBIT(3) | SBIT(4) | SBIT(11) | SBIT(19),
	 MT_CONNECT_ACKNOWLEDGE, l3dss1_status_req},
	{SBIT(8),
	 MT_CONNECT_ACKNOWLEDGE, l3dss1_connect_ack},
	{SBIT(1) | SBIT(2) | SBIT(3) | SBIT(4) | SBIT(8) | SBIT(10) | SBIT(11) | SBIT(19),
	 MT_INVALID, l3dss1_status_req},
};

static int datasllen = sizeof(datastatelist) / sizeof(struct stateentry);

static struct stateentry globalmes_list[] =
{
	{ALL_STATES,
         MT_STATUS, l3dss1_status},
	{SBIT(0),
	 MT_RESTART, l3dss1_global_restart},
/*	{SBIT(1),
	 MT_RESTART_ACKNOWLEDGE, l3dss1_restart_ack},                                  
*/
};
static int globalm_len = sizeof(globalmes_list) / sizeof(struct stateentry);

#if 0
static struct stateentry globalcmd_list[] =
{
	{ALL_STATES,
         CC_STATUS, l3dss1_status_req},
	{SBIT(0),
	 CC_RESTART, l3dss1_restart_req},
};

static int globalc_len = sizeof(globalcmd_list) / sizeof(struct stateentry);
#endif
/* *INDENT-ON* */

static void
global_handler(struct PStack *st, int mt, struct sk_buff *skb)
{
	int i;
	char tmp[64];
	struct l3_process *proc = st->l3.global;
	
	for (i = 0; i < globalm_len; i++)
		if ((mt == globalmes_list[i].primitive) &&
		    ((1 << proc->state) & globalmes_list[i].state))
			break;
	if (i == globalm_len) {
		dev_kfree_skb(skb);
		if (st->l3.debug & L3_DEB_STATE) {
			sprintf(tmp, "dss1 global state %d mt %x unhandled",
				proc->state, mt);
			l3_debug(st, tmp);
		}
		return;
	} else {
		if (st->l3.debug & L3_DEB_STATE) {
			sprintf(tmp, "dss1 global %d mt %x",
				proc->state, mt);
			l3_debug(st, tmp);
		}
		globalmes_list[i].rout(proc, mt, skb);
	}
}

static void
dss1up(struct PStack *st, int pr, void *arg)
{
	int i, mt, cr, cause, callState;
	char *ptr;
	struct sk_buff *skb = arg;
	struct l3_process *proc;
	char tmp[80];

	if (skb->data[0] != PROTO_DIS_EURO) {
		if (st->l3.debug & L3_DEB_PROTERR) {
			sprintf(tmp, "dss1up%sunexpected discriminator %x message len %d",
				(pr == DL_DATA) ? " " : "(broadcast) ",
				skb->data[0], skb->len);
			l3_debug(st, tmp);
		}
		dev_kfree_skb(skb);
		return;
	}
	cr = getcallref(skb->data);
	mt = skb->data[skb->data[1] + 2];
	if (!cr) {				/* Global CallRef */
		global_handler(st, mt, skb);
		return;
	} else if (cr == -1) {			/* Dummy Callref */
		dev_kfree_skb(skb);
		return;
	} else if (!(proc = getl3proc(st, cr))) {
		/* No transaction process exist, that means no call with
		 * this callreference is active
		 */
		if (mt == MT_SETUP) {
		/* Setup creates a new transaction process */
			if (!(proc = new_l3_process(st, cr))) {
				/* May be to answer with RELEASE_COMPLETE and
				 * CAUSE 0x2f "Resource unavailable", but this
				 * need a new_l3_process too ... arghh
				 */
				dev_kfree_skb(skb);
				return;
			}
		} else if (mt == MT_STATUS) {
			cause = 0;
			if((ptr = findie(skb->data, skb->len, IE_CAUSE, 0)) != NULL) {
				  ptr++;
				  if (*ptr++ == 2)
				  	ptr++;
				  cause = *ptr & 0x7f;
			}
			callState = 0;
			if((ptr = findie(skb->data, skb->len, IE_CALL_STATE, 0)) != NULL) {
				ptr++;
				if (*ptr++ == 2)
					ptr++;
				callState = *ptr;
			}
			if (callState == 0) {
				/* ETS 300-104 part 2.4.1
				 * if setup has not been made and a message type
				 * MT_STATUS is received with call state == 0,
				 * we must send nothing
				 */
				dev_kfree_skb(skb);
				return;
			} else {
				/* ETS 300-104 part 2.4.2
				 * if setup has not been made and a message type 
				 * MT_STATUS is received with call state != 0,
				 * we must send MT_RELEASE_COMPLETE cause 101
				 */
				dev_kfree_skb(skb);
				if ((proc = new_l3_process(st, cr))) {
					proc->para.cause = 0x65; /* 101 */
					proc->st->l3.l3l4(proc, CC_ESTABLISH, NULL);
				}
				return;
			}
		} else if (mt == MT_RELEASE_COMPLETE){
			dev_kfree_skb(skb);
			return;
		} else {
			/* ETS 300-104 part 2
			 * if setup has not been made and a message type 
			 * (except MT_SETUP and RELEASE_COMPLETE) is received,
			 * we must send MT_RELEASE_COMPLETE cause 81 */
			dev_kfree_skb(skb);
			if ((proc = new_l3_process(st, cr))) {
				proc->para.cause = 0x51; /* 81 */
				proc->st->l3.l3l4(proc, CC_ESTABLISH, NULL);
			}
			return;
		}
	} else if (!l3dss1_check_messagetype_validity(mt)) {
		/* ETS 300-104 7.4.2, 8.4.2, 10.3.2, 11.4.2, 12.4.2, 13.4.2,
		 * 14.4.2...
		 * if setup has been made and invalid message type is received,
		 * we must send MT_STATUS cause 0x62
		 */
		mt = MT_INVALID;  /* sorry, not clean, but do the right thing ;-) */
	}

	for (i = 0; i < datasllen; i++)
		if ((mt == datastatelist[i].primitive) &&
		    ((1 << proc->state) & datastatelist[i].state))
			break;
	if (i == datasllen) {
		dev_kfree_skb(skb);
		if (st->l3.debug & L3_DEB_STATE) {
			sprintf(tmp, "dss1up%sstate %d mt %x unhandled",
				(pr == DL_DATA) ? " " : "(broadcast) ",
				proc->state, mt);
			l3_debug(st, tmp);
		}
		return;
	} else {
		if (st->l3.debug & L3_DEB_STATE) {
			sprintf(tmp, "dss1up%sstate %d mt %x",
				(pr == DL_DATA) ? " " : "(broadcast) ",
				proc->state, mt);
			l3_debug(st, tmp);
		}
		datastatelist[i].rout(proc, pr, skb);
	}
}

static void
dss1down(struct PStack *st, int pr, void *arg)
{
	int i, cr;
	struct l3_process *proc;
	struct Channel *chan;
	char tmp[80];

	if (CC_SETUP_REQ == pr) {
		chan = arg;
		cr = newcallref();
		cr |= 0x80;
		if ((proc = new_l3_process(st, cr))) {
			proc->chan = chan;
			chan->proc = proc;
			proc->para.setup = chan->setup;
			proc->callref = cr;
		}
	} else {
		proc = arg;
	}
	if (!proc) {
		printk(KERN_ERR "HiSax internal error dss1down without proc\n");
		return;
	}
	for (i = 0; i < downsllen; i++)
		if ((pr == downstatelist[i].primitive) &&
		    ((1 << proc->state) & downstatelist[i].state))
			break;
	if (i == downsllen) {
		if (st->l3.debug & L3_DEB_STATE) {
			sprintf(tmp, "dss1down state %d prim %d unhandled",
				proc->state, pr);
			l3_debug(st, tmp);
		}
	} else {
		if (st->l3.debug & L3_DEB_STATE) {
			sprintf(tmp, "dss1down state %d prim %d",
				proc->state, pr);
			l3_debug(st, tmp);
		}
		downstatelist[i].rout(proc, pr, arg);
	}
}

void
setstack_dss1(struct PStack *st)
{
	char tmp[64];

	st->lli.l4l3 = dss1down;
	st->l2.l2l3 = dss1up;
	st->l3.N303 = 1;
	if (!(st->l3.global = kmalloc(sizeof(struct l3_process), GFP_ATOMIC))) {
		printk(KERN_ERR "HiSax can't get memory for dss1 global CR\n");
	} else {
		st->l3.global->state = 0;
		st->l3.global->callref = 0;
		st->l3.global->next = NULL;
		st->l3.global->debug = L3_DEB_WARN;
		st->l3.global->st = st;
		st->l3.global->N303 = 1;
		L3InitTimer(st->l3.global, &st->l3.global->timer);
	}
	strcpy(tmp, dss1_revision);
	printk(KERN_INFO "HiSax: DSS1 Rev. %s\n", HiSax_getrev(tmp));
}
