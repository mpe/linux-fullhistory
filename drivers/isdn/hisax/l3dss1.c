/* $Id: l3dss1.c,v 1.12 1997/02/17 00:34:26 keil Exp $

 * EURO/DSS1 D-channel protocol
 *
 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *              based on the teles driver from Jan den Ouden
 *
 * Thanks to    Jan den Ouden
 *              Fritz Elfert
 *
 * $Log: l3dss1.c,v $
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

extern char *HiSax_getrev(const char *revision);
const char *dss1_revision = "$Revision: 1.12 $";

#define	MsgHead(ptr, cref, mty) \
	*ptr++ = 0x8; \
	*ptr++ = 0x1; \
	*ptr++ = cref; \
	*ptr++ = mty

static void
l3dss1_message(struct PStack *st, byte mt)
{
	struct BufHeader *dibh;
	byte *p;

	BufPoolGet(&dibh, st->l1.sbufpool, GFP_ATOMIC, (void *) st, 18);
	p = DATAPTR(dibh);
	p += st->l2.ihsize;

	MsgHead(p, st->l3.callref, mt);

	dibh->datasize = p - DATAPTR(dibh);
	st->l3.l3l2(st, DL_DATA, dibh);
}

static void
l3dss1_release_req(struct PStack *st, byte pr, void *arg)
{
	StopAllL3Timer(st);
	newl3state(st, 19);
	l3dss1_message(st, MT_RELEASE);
	L3AddTimer(&st->l3.timer, st->l3.t308, CC_T308_1);
}

static void
l3dss1_release_cmpl(struct PStack *st, byte pr, void *arg)
{
	byte *p;
	struct BufHeader *ibh = arg;
	int cause = -1;

	p = DATAPTR(ibh);
	if ((p = findie(p + st->l2.ihsize, ibh->datasize - st->l2.ihsize,
			IE_CAUSE, 0))) {
		p++;
		if (*p++ == 2) 
			st->pa->loc = *p++;
		cause = *p & 0x7f;
	}
	BufPoolRelease(ibh);
	StopAllL3Timer(st);
	st->pa->cause = cause;
	newl3state(st, 0);
	st->l3.l3l4(st, CC_RELEASE_CNF, NULL);
}

static void
l3dss1_setup_req(struct PStack *st, byte pr,
		 void *arg)
{
	struct BufHeader *dibh;
	byte *p;
	char *teln;

	st->l3.callref = st->pa->callref;
	BufPoolGet(&dibh, st->l1.sbufpool, GFP_ATOMIC, (void *) st, 19);
	p = DATAPTR(dibh);
	p += st->l2.ihsize;

	MsgHead(p, st->l3.callref, MT_SETUP);

	/*
	 * Set Bearer Capability, Map info from 1TR6-convention to EDSS1
	 */
	*p++ = 0xa1;
	switch (st->pa->setup.si1) {
	case 1:		/* Telephony                               */
		*p++ = 0x4;	/* BC-IE-code                              */
		*p++ = 0x3;	/* Length                                  */
		*p++ = 0x90;	/* Coding Std. CCITT, 3.1 kHz audio     */
		*p++ = 0x90;	/* Circuit-Mode 64kbps                     */
		*p++ = 0xa3;	/* A-Law Audio                             */
		break;
	case 5:		/* Datatransmission 64k, BTX               */
	case 7:		/* Datatransmission 64k                    */
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
	if (st->pa->setup.eazmsn[0]) {
		*p++ = 0x6c;
		*p++ = strlen(st->pa->setup.eazmsn) + 1;
		/* Classify as AnyPref. */
		*p++ = 0x81;	/* Ext = '1'B, Type = '000'B, Plan = '0001'B. */
		teln = st->pa->setup.eazmsn;
		while (*teln)
			*p++ = *teln++ & 0x7f;
	}
	*p++ = 0x70;
	*p++ = strlen(st->pa->setup.phone) + 1;
	/* Classify as AnyPref. */
	*p++ = 0x81;		/* Ext = '1'B, Type = '000'B, Plan = '0001'B. */

	teln = st->pa->setup.phone;
	while (*teln)
		*p++ = *teln++ & 0x7f;


	dibh->datasize = p - DATAPTR(dibh);
	L3DelTimer(&st->l3.timer);
	L3AddTimer(&st->l3.timer, st->l3.t303, CC_T303);
	newl3state(st, 1);
	st->l3.l3l2(st, DL_DATA, dibh);

}

static void
l3dss1_call_proc(struct PStack *st, byte pr, void *arg)
{
	byte *p;
	struct BufHeader *ibh = arg;

	L3DelTimer(&st->l3.timer);
	p = DATAPTR(ibh);
	if ((p = findie(p + st->l2.ihsize, ibh->datasize - st->l2.ihsize,
			0x18, 0))) {
		st->pa->bchannel = p[2] & 0x3;
		if ((!st->pa->bchannel) && (st->l3.debug & L3_DEB_WARN))
			l3_debug(st, "setup answer without bchannel");
	} else if (st->l3.debug & L3_DEB_WARN)
		l3_debug(st, "setup answer without bchannel");
	BufPoolRelease(ibh);
	newl3state(st, 3);
	L3AddTimer(&st->l3.timer, st->l3.t310, CC_T310);
	st->l3.l3l4(st, CC_PROCEEDING_IND, NULL);
}

static void
l3dss1_setup_ack(struct PStack *st, byte pr, void *arg)
{
	byte *p;
	struct BufHeader *ibh = arg;

	L3DelTimer(&st->l3.timer);
	p = DATAPTR(ibh);
	if ((p = findie(p + st->l2.ihsize, ibh->datasize - st->l2.ihsize,
			0x18, 0))) {
		st->pa->bchannel = p[2] & 0x3;
		if ((!st->pa->bchannel) && (st->l3.debug & L3_DEB_WARN))
			l3_debug(st, "setup answer without bchannel");
	} else if (st->l3.debug & L3_DEB_WARN)
		l3_debug(st, "setup answer without bchannel");

	BufPoolRelease(ibh);
	newl3state(st, 2);
	L3AddTimer(&st->l3.timer, st->l3.t304, CC_T304);
	st->l3.l3l4(st, CC_MORE_INFO, NULL);
}

static void
l3dss1_disconnect(struct PStack *st, byte pr, void *arg)
{
	byte *p;
	struct BufHeader *ibh = arg;
	int cause = -1;

	StopAllL3Timer(st);
	p = DATAPTR(ibh);
	if ((p = findie(p + st->l2.ihsize, ibh->datasize - st->l2.ihsize,
			IE_CAUSE, 0))) {
		p++;
		if (*p++ == 2)
			st->pa->loc = *p++;
		cause = *p & 0x7f;
	}
	BufPoolRelease(ibh);
	newl3state(st, 12);
	st->pa->cause = cause;
	st->l3.l3l4(st, CC_DISCONNECT_IND, NULL);
}


static void
l3dss1_connect(struct PStack *st, byte pr, void *arg)
{
	struct BufHeader *ibh = arg;


	BufPoolRelease(ibh);
	L3DelTimer(&st->l3.timer);	/* T310 */
	newl3state(st, 10);
	st->l3.l3l4(st, CC_SETUP_CNF, NULL);
}

static void
l3dss1_alerting(struct PStack *st, byte pr, void *arg)
{
	struct BufHeader *ibh = arg;

	BufPoolRelease(ibh);
	L3DelTimer(&st->l3.timer);	/* T304 */
	newl3state(st, 4);
	st->l3.l3l4(st, CC_ALERTING_IND, NULL);
}

static void
l3dss1_setup(struct PStack *st, byte pr, void *arg)
{
	byte *p;
	int bcfound = 0;
	char tmp[80];
	struct BufHeader *ibh = arg;

	p = DATAPTR(ibh);
	p += st->l2.uihsize;
	st->pa->callref = getcallref(p);
	st->l3.callref = 0x80 + st->pa->callref;

	/*
	 * Channel Identification
	 */
	p = DATAPTR(ibh);
	if ((p = findie(p + st->l2.uihsize, ibh->datasize - st->l2.uihsize,
			0x18, 0))) {
		st->pa->bchannel = p[2] & 0x3;
		if (st->pa->bchannel)
			bcfound++;
		else if (st->l3.debug & L3_DEB_WARN)
			l3_debug(st, "setup without bchannel");
	} else if (st->l3.debug & L3_DEB_WARN)
		l3_debug(st, "setup without bchannel");

	p = DATAPTR(ibh);
	/*
	   * Bearer Capabilities
	 */
	if ((p = findie(p + st->l2.uihsize, ibh->datasize - st->l2.uihsize, 0x04, 0))) {
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

	p = DATAPTR(ibh);
	if ((p = findie(p + st->l2.uihsize, ibh->datasize - st->l2.uihsize,
			0x70, 0)))
		iecpy(st->pa->setup.eazmsn, p, 1);
	else
		st->pa->setup.eazmsn[0] = 0;

	p = DATAPTR(ibh);
	if ((p = findie(p + st->l2.uihsize, ibh->datasize - st->l2.uihsize,
			0x6c, 0))) {
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
	BufPoolRelease(ibh);

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
l3dss1_reset(struct PStack *st, byte pr, void *arg)
{
	StopAllL3Timer(st);
	newl3state(st, 0);
}

static void
l3dss1_setup_rsp(struct PStack *st, byte pr,
		 void *arg)
{
	newl3state(st, 8);
	l3dss1_message(st, MT_CONNECT);
	L3DelTimer(&st->l3.timer);
	L3AddTimer(&st->l3.timer, st->l3.t313, CC_T313);
}

static void
l3dss1_connect_ack(struct PStack *st, byte pr, void *arg)
{
	struct BufHeader *ibh = arg;

	BufPoolRelease(ibh);
	newl3state(st, 10);
	L3DelTimer(&st->l3.timer);
	st->l3.l3l4(st, CC_SETUP_COMPLETE_IND, NULL);
}

static void
l3dss1_disconnect_req(struct PStack *st, byte pr, void *arg)
{
	struct BufHeader *dibh;
	byte *p;
	byte cause = 0x10;

	if (st->pa->cause > 0)
		cause = st->pa->cause;

	StopAllL3Timer(st);
	BufPoolGet(&dibh, st->l1.sbufpool, GFP_ATOMIC, (void *) st, 20);
	p = DATAPTR(dibh);
	p += st->l2.ihsize;

	MsgHead(p, st->l3.callref, MT_DISCONNECT);

	*p++ = IE_CAUSE;
	*p++ = 0x2;
	*p++ = 0x80;
	*p++ = cause | 0x80;

	dibh->datasize = p - DATAPTR(dibh);
	newl3state(st, 11);
	st->l3.l3l2(st, DL_DATA, dibh);
	L3AddTimer(&st->l3.timer, st->l3.t305, CC_T305);
}

static void
l3dss1_reject_req(struct PStack *st, byte pr, void *arg)
{
	struct BufHeader *dibh;
	byte *p;
	byte cause = 0x95;

	if (st->pa->cause > 0)
		cause = st->pa->cause;

	BufPoolGet(&dibh, st->l1.sbufpool, GFP_ATOMIC, (void *) st, 20);
	p = DATAPTR(dibh);
	p += st->l2.ihsize;

	MsgHead(p, st->l3.callref, MT_RELEASE_COMPLETE);

	*p++ = IE_CAUSE;
	*p++ = 0x2;
	*p++ = 0x80;
	*p++ = cause;

	dibh->datasize = p - DATAPTR(dibh);
	newl3state(st, 0);
	st->l3.l3l2(st, DL_DATA, dibh);
	st->l3.l3l4(st, CC_RELEASE_IND, NULL);
}

static void
l3dss1_release(struct PStack *st, byte pr, void *arg)
{
	byte *p;
	struct BufHeader *ibh = arg;
	int cause = -1;

	p = DATAPTR(ibh);
	if ((p = findie(p + st->l2.ihsize, ibh->datasize - st->l2.ihsize,
			IE_CAUSE, 0))) {
		p++;
		if (*p++ == 2)
			st->pa->loc = *p++;
		cause = *p & 0x7f;
	}
	BufPoolRelease(ibh);
	StopAllL3Timer(st);
	st->pa->cause = cause;
	newl3state(st, 0);
	l3dss1_message(st, MT_RELEASE_COMPLETE);
	st->l3.l3l4(st, CC_RELEASE_IND, NULL);
}

static void
l3dss1_alert_req(struct PStack *st, byte pr,
		 void *arg)
{
	newl3state(st, 7);
	l3dss1_message(st, MT_ALERTING);
}

static void
l3dss1_status_enq(struct PStack *st, byte pr, void *arg)
{
	struct BufHeader *dibh = arg;
	byte *p;

	BufPoolRelease(dibh);

	BufPoolGet(&dibh, st->l1.sbufpool, GFP_ATOMIC, (void *) st, 22);
	p = DATAPTR(dibh);
	p += st->l2.ihsize;

	MsgHead(p, st->l3.callref, MT_STATUS);

	*p++ = IE_CAUSE;
	*p++ = 0x2;
	*p++ = 0x80;
	*p++ = 0x9E;		/* answer status enquire */

	*p++ = 0x14;		/* CallState */
	*p++ = 0x1;
	*p++ = st->l3.state & 0x3f;

	dibh->datasize = p - DATAPTR(dibh);
	st->l3.l3l2(st, DL_DATA, dibh);
}

static void
l3dss1_t303(struct PStack *st, byte pr, void *arg)
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
l3dss1_t304(struct PStack *st, byte pr, void *arg)
{
	L3DelTimer(&st->l3.timer);
	st->pa->cause = 0xE6;
	l3dss1_disconnect_req(st, pr, NULL);
	st->l3.l3l4(st, CC_SETUP_ERR, NULL);

}

static void
l3dss1_t305(struct PStack *st, byte pr, void *arg)
{
	struct BufHeader *dibh;
	byte *p;
	byte cause = 0x90;

	L3DelTimer(&st->l3.timer);
	if (st->pa->cause > 0)
		cause = st->pa->cause;

	BufPoolGet(&dibh, st->l1.sbufpool, GFP_ATOMIC, (void *) st, 20);
	p = DATAPTR(dibh);
	p += st->l2.ihsize;

	MsgHead(p, st->l3.callref, MT_RELEASE);

	*p++ = IE_CAUSE;
	*p++ = 0x2;
	*p++ = 0x80;
	*p++ = cause;

	dibh->datasize = p - DATAPTR(dibh);
	newl3state(st, 19);
	st->l3.l3l2(st, DL_DATA, dibh);
	L3AddTimer(&st->l3.timer, st->l3.t308, CC_T308_1);
}

static void
l3dss1_t310(struct PStack *st, byte pr, void *arg)
{
	L3DelTimer(&st->l3.timer);
	st->pa->cause = 0xE6;
	l3dss1_disconnect_req(st, pr, NULL);
	st->l3.l3l4(st, CC_SETUP_ERR, NULL);
}

static void
l3dss1_t313(struct PStack *st, byte pr, void *arg)
{
	L3DelTimer(&st->l3.timer);
	st->pa->cause = 0xE6;
	l3dss1_disconnect_req(st, pr, NULL);
	st->l3.l3l4(st, CC_CONNECT_ERR, NULL);
}

static void
l3dss1_t308_1(struct PStack *st, byte pr, void *arg)
{
	newl3state(st, 19);
	L3DelTimer(&st->l3.timer);
	l3dss1_message(st, MT_RELEASE);
	L3AddTimer(&st->l3.timer, st->l3.t308, CC_T308_2);
}

static void
l3dss1_t308_2(struct PStack *st, byte pr, void *arg)
{
	newl3state(st, 0);
	L3DelTimer(&st->l3.timer);
	st->l3.l3l4(st, CC_RELEASE_ERR, NULL);
}

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

static int datasllen = sizeof(datastatelist) /
sizeof(struct stateentry);

static void
dss1up(struct PStack *st,
       int pr, void *arg)
{
	int i, mt, size;
	byte *ptr;
	struct BufHeader *ibh = arg;
	char tmp[80];

	if (pr == DL_DATA) {
		ptr = DATAPTR(ibh);
		ptr += st->l2.ihsize;
		size = ibh->datasize - st->l2.ihsize;
	} else if (pr == DL_UNIT_DATA) {
		ptr = DATAPTR(ibh);
		ptr += st->l2.uihsize;
		size = ibh->datasize - st->l2.uihsize;
	} else {
		if (st->l3.debug & L3_DEB_WARN) {
			sprintf(tmp, "dss1up unknown data typ %d state %d",
				pr, st->l3.state);
			l3_debug(st, tmp);
		}
		BufPoolRelease(ibh);
		return;
	}
	if (ptr[0] != PROTO_DIS_EURO) {
		if (st->l3.debug & L3_DEB_PROTERR) {
			sprintf(tmp, "dss1up%sunexpected discriminator %x message len %d state %d",
				(pr == DL_DATA) ? " " : "(broadcast) ",
				ptr[0], size, st->l3.state);
			l3_debug(st, tmp);
		}
		BufPoolRelease(ibh);
		return;
	}
	mt = ptr[3];
	for (i = 0; i < datasllen; i++)
		if ((mt == datastatelist[i].primitive) &&
		    ((1 << st->l3.state) & datastatelist[i].state))
			break;
	if (i == datasllen) {
		BufPoolRelease(ibh);
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
		datastatelist[i].rout(st, pr, ibh);
	}
}

static void
dss1down(struct PStack *st,
	 int pr, void *arg)
{
	int i;
	struct BufHeader *ibh = arg;
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
		downstatelist[i].rout(st, pr, ibh);
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
