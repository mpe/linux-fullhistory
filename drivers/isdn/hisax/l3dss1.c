/* $Id: l3dss1.c,v 1.15 1997/04/17 11:50:48 keil Exp $

 * EURO/DSS1 D-channel protocol
 *
 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *              based on the teles driver from Jan den Ouden
 *
 * Thanks to    Jan den Ouden
 *              Fritz Elfert
 *
 * $Log: l3dss1.c,v $
 * Revision 1.15  1997/04/17 11:50:48  keil
 * pa->loc was undefined, if it was not send by the exchange
 *
 * Revision 1.14  1997/04/06 22:54:20  keil
 * Using SKB's
 *
 * Revision 1.13  1997/03/13 20:37:28  keil
 * CLIR and channel request added
 *
 * Revision 1.12  1997/02/17 00:34:26  keil
 * Bugfix: Wrong cause delivered
 *
 * Revision 1.11  1997/02/16 12:12:47  fritz
 * Bugfix: SI2 was nont initialized on incoming calls.
 *
 * Revision 1.10  1997/02/11 01:37:24  keil
 * Changed setup-interface (incoming and outgoing)
 *
 * Revision 1.9  1997/01/27 23:20:52  keil
 * report revision only ones
 *
 * Revision 1.8  1997/01/21 22:29:41  keil
 * new statemachine; L3 timers
 *
 * Revision 1.7  1996/12/14 21:06:59  keil
 * additional states for CC_REJECT
 *
 * Revision 1.6  1996/12/08 22:59:16  keil
 * fixed calling party number without octet 3a
 *
 * Revision 1.5  1996/12/08 19:53:31  keil
 * fixes from Pekka Sarnila
 *
 * Revision 1.4  1996/11/05 19:44:36  keil
 * some fixes from Henner Eisen
 *
 * Revision 1.3  1996/10/30 10:18:01  keil
 * bugfixes in debugging output
 *
 * Revision 1.2  1996/10/27 22:15:16  keil
 * bugfix reject handling
 *
 * Revision 1.1  1996/10/13 20:04:55  keil
 * Initial revision
 *
 *
 *
 */

#define __NO_VERSION__
#include "hisax.h"
#include "isdnl3.h"
#include <linux/ctype.h>

extern char *HiSax_getrev(const char *revision);
const char *dss1_revision = "$Revision: 1.15 $";

#define	MsgHead(ptr, cref, mty) \
	*ptr++ = 0x8; \
	*ptr++ = 0x1; \
	*ptr++ = cref; \
	*ptr++ = mty

static void
l3dss1_message(struct PStack *st, u_char mt)
{
	struct sk_buff *skb;
	u_char *p;

	if (!(skb = l3_alloc_skb(4)))
		return;
	p = skb_put(skb, 4);
	MsgHead(p, st->l3.callref, mt);
	st->l3.l3l2(st, DL_DATA, skb);
}

static void
l3dss1_release_req(struct PStack *st, u_char pr, void *arg)
{
	StopAllL3Timer(st);
	newl3state(st, 19);
	l3dss1_message(st, MT_RELEASE);
	L3AddTimer(&st->l3.timer, st->l3.t308, CC_T308_1);
}

static void
l3dss1_release_cmpl(struct PStack *st, u_char pr, void *arg)
{
	u_char *p;
	struct sk_buff *skb = arg;
	int cause = -1;

	p = skb->data;
	st->pa->loc = 0;
	if ((p = findie(p, skb->len, IE_CAUSE, 0))) {
		p++;
		if (*p++ == 2)
			st->pa->loc = *p++;
		cause = *p & 0x7f;
	}
	SET_SKB_FREE(skb);
	dev_kfree_skb(skb, FREE_READ);
	StopAllL3Timer(st);
	st->pa->cause = cause;
	newl3state(st, 0);
	st->l3.l3l4(st, CC_RELEASE_CNF, NULL);
}

static void
l3dss1_setup_req(struct PStack *st, u_char pr,
		 void *arg)
{
	struct sk_buff *skb;
	u_char tmp[128];
	u_char *p = tmp;
	u_char channel = 0;
	u_char screen = 0;
	u_char *teln;
	u_char *msn;
	int l;

	st->l3.callref = st->pa->callref;
	MsgHead(p, st->l3.callref, MT_SETUP);

	/*
	 * Set Bearer Capability, Map info from 1TR6-convention to EDSS1
	 */
	*p++ = 0xa1;		/* complete indicator */
	switch (st->pa->setup.si1) {
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
	teln = st->pa->setup.phone;
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
					if (st->l3.debug & L3_DEB_WARN)
						l3_debug(st, "Wrong MSN Code");
					break;
			}
			teln++;
		}
	}
	if (channel) {
		*p++ = 0x18;	/* channel indicator */
		*p++ = 1;
		*p++ = channel;
	}
	msn = st->pa->setup.eazmsn;
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
	*p++ = 0x70;
	*p++ = strlen(teln) + 1;
	/* Classify as AnyPref. */
	*p++ = 0x81;		/* Ext = '1'B, Type = '000'B, Plan = '0001'B. */

	while (*teln)
		*p++ = *teln++ & 0x7f;

	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	L3DelTimer(&st->l3.timer);
	L3AddTimer(&st->l3.timer, st->l3.t303, CC_T303);
	newl3state(st, 1);
	st->l3.l3l2(st, DL_DATA, skb);

}

static void
l3dss1_call_proc(struct PStack *st, u_char pr, void *arg)
{
	u_char *p;
	struct sk_buff *skb = arg;

	L3DelTimer(&st->l3.timer);
	p = skb->data;
	if ((p = findie(p, skb->len, 0x18, 0))) {
		st->pa->bchannel = p[2] & 0x3;
		if ((!st->pa->bchannel) && (st->l3.debug & L3_DEB_WARN))
			l3_debug(st, "setup answer without bchannel");
	} else if (st->l3.debug & L3_DEB_WARN)
		l3_debug(st, "setup answer without bchannel");
	SET_SKB_FREE(skb);
	dev_kfree_skb(skb, FREE_READ);
	newl3state(st, 3);
	L3AddTimer(&st->l3.timer, st->l3.t310, CC_T310);
	st->l3.l3l4(st, CC_PROCEEDING_IND, NULL);
}

static void
l3dss1_setup_ack(struct PStack *st, u_char pr, void *arg)
{
	u_char *p;
	struct sk_buff *skb = arg;

	L3DelTimer(&st->l3.timer);
	p = skb->data;
	if ((p = findie(p, skb->len, 0x18, 0))) {
		st->pa->bchannel = p[2] & 0x3;
		if ((!st->pa->bchannel) && (st->l3.debug & L3_DEB_WARN))
			l3_debug(st, "setup answer without bchannel");
	} else if (st->l3.debug & L3_DEB_WARN)
		l3_debug(st, "setup answer without bchannel");
	SET_SKB_FREE(skb);
	dev_kfree_skb(skb, FREE_READ);
	newl3state(st, 2);
	L3AddTimer(&st->l3.timer, st->l3.t304, CC_T304);
	st->l3.l3l4(st, CC_MORE_INFO, NULL);
}

static void
l3dss1_disconnect(struct PStack *st, u_char pr, void *arg)
{
	u_char *p;
	struct sk_buff *skb = arg;
	int cause = -1;

	StopAllL3Timer(st);
	p = skb->data;
	st->pa->loc = 0;
	if ((p = findie(p, skb->len, IE_CAUSE, 0))) {
		p++;
		if (*p++ == 2)
			st->pa->loc = *p++;
		cause = *p & 0x7f;
	}
	SET_SKB_FREE(skb);
	dev_kfree_skb(skb, FREE_READ);
	newl3state(st, 12);
	st->pa->cause = cause;
	st->l3.l3l4(st, CC_DISCONNECT_IND, NULL);
}

static void
l3dss1_connect(struct PStack *st, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;

	SET_SKB_FREE(skb);
	dev_kfree_skb(skb, FREE_READ);
	L3DelTimer(&st->l3.timer);	/* T310 */
	newl3state(st, 10);
	st->l3.l3l4(st, CC_SETUP_CNF, NULL);
}

static void
l3dss1_alerting(struct PStack *st, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;

	SET_SKB_FREE(skb);
	dev_kfree_skb(skb, FREE_READ);
	L3DelTimer(&st->l3.timer);	/* T304 */
	newl3state(st, 4);
	st->l3.l3l4(st, CC_ALERTING_IND, NULL);
}

static void
l3dss1_setup(struct PStack *st, u_char pr, void *arg)
{
	u_char *p;
	int bcfound = 0;
	char tmp[80];
	struct sk_buff *skb = arg;

	p = skb->data;
	st->pa->callref = getcallref(p);
	st->l3.callref = 0x80 + st->pa->callref;

	/*
	 * Channel Identification
	 */
	p = skb->data;
	if ((p = findie(p, skb->len, 0x18, 0))) {
		st->pa->bchannel = p[2] & 0x3;
		if (st->pa->bchannel)
			bcfound++;
		else if (st->l3.debug & L3_DEB_WARN)
			l3_debug(st, "setup without bchannel");
	} else if (st->l3.debug & L3_DEB_WARN)
		l3_debug(st, "setup without bchannel");

	/*
	   * Bearer Capabilities
	 */
	p = skb->data;
	if ((p = findie(p, skb->len, 0x04, 0))) {
		st->pa->setup.si2 = 0;
		switch (p[2] & 0x1f) {
			case 0x00:
				/* Speech */
			case 0x10:
				/* 3.1 Khz audio */
				st->pa->setup.si1 = 1;
				break;
			case 0x08:
				/* Unrestricted digital information */
				st->pa->setup.si1 = 7;
				break;
			case 0x09:
				/* Restricted digital information */
				st->pa->setup.si1 = 2;
				break;
			case 0x11:
				/* Unrestr. digital information  with tones/announcements */
				st->pa->setup.si1 = 3;
				break;
			case 0x18:
				/* Video */
				st->pa->setup.si1 = 4;
				break;
			default:
				st->pa->setup.si1 = 0;
		}
	} else if (st->l3.debug & L3_DEB_WARN)
		l3_debug(st, "setup without bearer capabilities");

	p = skb->data;
	if ((p = findie(p, skb->len, 0x70, 0)))
		iecpy(st->pa->setup.eazmsn, p, 1);
	else
		st->pa->setup.eazmsn[0] = 0;

	p = skb->data;
	if ((p = findie(p, skb->len, 0x6c, 0))) {
		st->pa->setup.plan = p[2];
		if (p[2] & 0x80) {
			iecpy(st->pa->setup.phone, p, 1);
			st->pa->setup.screen = 0;
		} else {
			iecpy(st->pa->setup.phone, p, 2);
			st->pa->setup.screen = p[3];
		}
	} else {
		st->pa->setup.phone[0] = 0;
		st->pa->setup.plan = 0;
		st->pa->setup.screen = 0;
	}
	SET_SKB_FREE(skb);
	dev_kfree_skb(skb, FREE_READ);

	if (bcfound) {
		if ((st->pa->setup.si1 != 7) && (st->l3.debug & L3_DEB_WARN)) {
			sprintf(tmp, "non-digital call: %s -> %s",
				st->pa->setup.phone,
				st->pa->setup.eazmsn);
			l3_debug(st, tmp);
		}
		newl3state(st, 6);
		st->l3.l3l4(st, CC_SETUP_IND, NULL);
	}
}

static void
l3dss1_reset(struct PStack *st, u_char pr, void *arg)
{
	StopAllL3Timer(st);
	newl3state(st, 0);
}

static void
l3dss1_setup_rsp(struct PStack *st, u_char pr,
		 void *arg)
{
	newl3state(st, 8);
	l3dss1_message(st, MT_CONNECT);
	L3DelTimer(&st->l3.timer);
	L3AddTimer(&st->l3.timer, st->l3.t313, CC_T313);
}

static void
l3dss1_connect_ack(struct PStack *st, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;

	SET_SKB_FREE(skb);
	dev_kfree_skb(skb, FREE_READ);
	newl3state(st, 10);
	L3DelTimer(&st->l3.timer);
	st->l3.l3l4(st, CC_SETUP_COMPLETE_IND, NULL);
}

static void
l3dss1_disconnect_req(struct PStack *st, u_char pr, void *arg)
{
	struct sk_buff *skb;
	u_char tmp[16];
	u_char *p = tmp;
	int l;
	u_char cause = 0x10;

	if (st->pa->cause > 0)
		cause = st->pa->cause;

	StopAllL3Timer(st);

	MsgHead(p, st->l3.callref, MT_DISCONNECT);

	*p++ = IE_CAUSE;
	*p++ = 0x2;
	*p++ = 0x80;
	*p++ = cause | 0x80;

	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	newl3state(st, 11);
	st->l3.l3l2(st, DL_DATA, skb);
	L3AddTimer(&st->l3.timer, st->l3.t305, CC_T305);
}

static void
l3dss1_reject_req(struct PStack *st, u_char pr, void *arg)
{
	struct sk_buff *skb;
	u_char tmp[16];
	u_char *p = tmp;
	int l;
	u_char cause = 0x95;

	if (st->pa->cause > 0)
		cause = st->pa->cause;

	MsgHead(p, st->l3.callref, MT_RELEASE_COMPLETE);

	*p++ = IE_CAUSE;
	*p++ = 0x2;
	*p++ = 0x80;
	*p++ = cause;

	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	newl3state(st, 0);
	st->l3.l3l2(st, DL_DATA, skb);
	st->l3.l3l4(st, CC_RELEASE_IND, NULL);
}

static void
l3dss1_release(struct PStack *st, u_char pr, void *arg)
{
	u_char *p;
	struct sk_buff *skb = arg;
	int cause = -1;

	p = skb->data;
	if ((p = findie(p, skb->len, IE_CAUSE, 0))) {
		p++;
		if (*p++ == 2)
			st->pa->loc = *p++;
		cause = *p & 0x7f;
	}
	SET_SKB_FREE(skb);
	dev_kfree_skb(skb, FREE_READ);
	StopAllL3Timer(st);
	st->pa->cause = cause;
	newl3state(st, 0);
	l3dss1_message(st, MT_RELEASE_COMPLETE);
	st->l3.l3l4(st, CC_RELEASE_IND, NULL);
}

static void
l3dss1_alert_req(struct PStack *st, u_char pr,
		 void *arg)
{
	newl3state(st, 7);
	l3dss1_message(st, MT_ALERTING);
}

static void
l3dss1_status_enq(struct PStack *st, u_char pr, void *arg)
{
	u_char tmp[16];
	u_char *p = tmp;
	int l;
	struct sk_buff *skb = arg;

	SET_SKB_FREE(skb);
	dev_kfree_skb(skb, FREE_READ);

	MsgHead(p, st->l3.callref, MT_STATUS);

	*p++ = IE_CAUSE;
	*p++ = 0x2;
	*p++ = 0x80;
	*p++ = 0x9E;		/* answer status enquire */

	*p++ = 0x14;		/* CallState */
	*p++ = 0x1;
	*p++ = st->l3.state & 0x3f;

	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	st->l3.l3l2(st, DL_DATA, skb);
}

static void
l3dss1_t303(struct PStack *st, u_char pr, void *arg)
{
	if (st->l3.n_t303 > 0) {
		st->l3.n_t303--;
		L3DelTimer(&st->l3.timer);
		l3dss1_setup_req(st, pr, arg);
	} else {
		newl3state(st, 0);
		L3DelTimer(&st->l3.timer);
		st->l3.l3l4(st, CC_NOSETUP_RSP_ERR, NULL);
		st->l3.n_t303 = 1;
	}
}

static void
l3dss1_t304(struct PStack *st, u_char pr, void *arg)
{
	L3DelTimer(&st->l3.timer);
	st->pa->cause = 0xE6;
	l3dss1_disconnect_req(st, pr, NULL);
	st->l3.l3l4(st, CC_SETUP_ERR, NULL);

}

static void
l3dss1_t305(struct PStack *st, u_char pr, void *arg)
{
	u_char tmp[16];
	u_char *p = tmp;
	int l;
	struct sk_buff *skb;
	u_char cause = 0x90;

	L3DelTimer(&st->l3.timer);
	if (st->pa->cause > 0)
		cause = st->pa->cause;

	MsgHead(p, st->l3.callref, MT_RELEASE);

	*p++ = IE_CAUSE;
	*p++ = 0x2;
	*p++ = 0x80;
	*p++ = cause;

	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	newl3state(st, 19);
	st->l3.l3l2(st, DL_DATA, skb);
	L3AddTimer(&st->l3.timer, st->l3.t308, CC_T308_1);
}

static void
l3dss1_t310(struct PStack *st, u_char pr, void *arg)
{
	L3DelTimer(&st->l3.timer);
	st->pa->cause = 0xE6;
	l3dss1_disconnect_req(st, pr, NULL);
	st->l3.l3l4(st, CC_SETUP_ERR, NULL);
}

static void
l3dss1_t313(struct PStack *st, u_char pr, void *arg)
{
	L3DelTimer(&st->l3.timer);
	st->pa->cause = 0xE6;
	l3dss1_disconnect_req(st, pr, NULL);
	st->l3.l3l4(st, CC_CONNECT_ERR, NULL);
}

static void
l3dss1_t308_1(struct PStack *st, u_char pr, void *arg)
{
	newl3state(st, 19);
	L3DelTimer(&st->l3.timer);
	l3dss1_message(st, MT_RELEASE);
	L3AddTimer(&st->l3.timer, st->l3.t308, CC_T308_2);
}

static void
l3dss1_t308_2(struct PStack *st, u_char pr, void *arg)
{
	newl3state(st, 0);
	L3DelTimer(&st->l3.timer);
	st->l3.l3l4(st, CC_RELEASE_ERR, NULL);
}
/* *INDENT-OFF* */
static struct stateentry downstatelist[] =
{
	{SBIT(0),
	 CC_SETUP_REQ, l3dss1_setup_req},
	{SBIT(1) | SBIT(2) | SBIT(3) | SBIT(4) | SBIT(6) | SBIT(7) | SBIT(8) | SBIT(10),
	 CC_DISCONNECT_REQ, l3dss1_disconnect_req},
	{SBIT(12),
	 CC_RELEASE_REQ, l3dss1_release_req},
	{ALL_STATES,
	 CC_DLRL, l3dss1_reset},
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
	{SBIT(0) | SBIT(6),
	 MT_SETUP, l3dss1_setup},
	{SBIT(1) | SBIT(2),
	 MT_CALL_PROCEEDING, l3dss1_call_proc},
	{SBIT(1),
	 MT_SETUP_ACKNOWLEDGE, l3dss1_setup_ack},
	{SBIT(1) | SBIT(2) | SBIT(3),
	 MT_ALERTING, l3dss1_alerting},
	{SBIT(0) | SBIT(1) | SBIT(2) | SBIT(3) | SBIT(4) | SBIT(7) | SBIT(8) | SBIT(10) |
	 SBIT(11) | SBIT(12) | SBIT(15) | SBIT(17) | SBIT(19),
	 MT_RELEASE_COMPLETE, l3dss1_release_cmpl},
	{SBIT(0) | SBIT(1) | SBIT(2) | SBIT(3) | SBIT(4) | SBIT(7) | SBIT(8) | SBIT(10) |
	 SBIT(11) | SBIT(12) | SBIT(15) | SBIT(17) | SBIT(19),
	 MT_RELEASE, l3dss1_release},
	{SBIT(1) | SBIT(2) | SBIT(3) | SBIT(4) | SBIT(7) | SBIT(8) | SBIT(10),
	 MT_DISCONNECT, l3dss1_disconnect},
	{SBIT(1) | SBIT(2) | SBIT(3) | SBIT(4),
	 MT_CONNECT, l3dss1_connect},
	{SBIT(8),
	 MT_CONNECT_ACKNOWLEDGE, l3dss1_connect_ack},
};
/* *INDENT-ON* */


static int datasllen = sizeof(datastatelist) /
sizeof(struct stateentry);

static void
dss1up(struct PStack *st, int pr, void *arg)
{
	int i, mt;
	struct sk_buff *skb = arg;
	char tmp[80];

	if (skb->data[0] != PROTO_DIS_EURO) {
		if (st->l3.debug & L3_DEB_PROTERR) {
			sprintf(tmp, "dss1up%sunexpected discriminator %x message len %ld state %d",
				(pr == DL_DATA) ? " " : "(broadcast) ",
				skb->data[0], skb->len, st->l3.state);
			l3_debug(st, tmp);
		}
		SET_SKB_FREE(skb);
		dev_kfree_skb(skb, FREE_READ);
		return;
	}
	mt = skb->data[skb->data[1] + 2];
	for (i = 0; i < datasllen; i++)
		if ((mt == datastatelist[i].primitive) &&
		    ((1 << st->l3.state) & datastatelist[i].state))
			break;
	if (i == datasllen) {
		SET_SKB_FREE(skb);
		dev_kfree_skb(skb, FREE_READ);
		if (st->l3.debug & L3_DEB_STATE) {
			sprintf(tmp, "dss1up%sstate %d mt %x unhandled",
				(pr == DL_DATA) ? " " : "(broadcast) ",
				st->l3.state, mt);
			l3_debug(st, tmp);
		}
		return;
	} else {
		if (st->l3.debug & L3_DEB_STATE) {
			sprintf(tmp, "dss1up%sstate %d mt %x",
				(pr == DL_DATA) ? " " : "(broadcast) ",
				st->l3.state, mt);
			l3_debug(st, tmp);
		}
		datastatelist[i].rout(st, pr, skb);
	}
}

static void
dss1down(struct PStack *st, int pr, void *arg)
{
	int i;
	char tmp[80];

	for (i = 0; i < downsllen; i++)
		if ((pr == downstatelist[i].primitive) &&
		    ((1 << st->l3.state) & downstatelist[i].state))
			break;
	if (i == downsllen) {
		if (st->l3.debug & L3_DEB_STATE) {
			sprintf(tmp, "dss1down state %d prim %d unhandled",
				st->l3.state, pr);
			l3_debug(st, tmp);
		}
	} else {
		if (st->l3.debug & L3_DEB_STATE) {
			sprintf(tmp, "dss1down state %d prim %d",
				st->l3.state, pr);
			l3_debug(st, tmp);
		}
		downstatelist[i].rout(st, pr, arg);
	}
}

void
setstack_dss1(struct PStack *st)
{
	char tmp[64];

	st->l4.l4l3 = dss1down;
	st->l2.l2l3 = dss1up;
	st->l3.t303 = 4000;
	st->l3.t304 = 30000;
	st->l3.t305 = 30000;
	st->l3.t308 = 4000;
	st->l3.t310 = 30000;
	st->l3.t313 = 4000;
	st->l3.t318 = 4000;
	st->l3.t319 = 4000;
	st->l3.n_t303 = 1;

	if (st->l3.channr & 1) {
		strcpy(tmp, dss1_revision);
		printk(KERN_NOTICE "HiSax: DSS1 Rev. %s\n", HiSax_getrev(tmp));
	}
}
