/* $Id: l3_1tr6.c,v 2.4 1998/02/12 23:07:57 keil Exp $

 *  German 1TR6 D-channel protocol
 *
 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *
 *
 * $Log: l3_1tr6.c,v $
 * Revision 2.4  1998/02/12 23:07:57  keil
 * change for 2.1.86 (removing FREE_READ/FREE_WRITE from [dev]_kfree_skb()
 *
 * Revision 2.3  1997/11/06 17:12:24  keil
 * KERN_NOTICE --> KERN_INFO
 *
 * Revision 2.2  1997/10/29 19:03:00  keil
 * changes for 2.1
 *
 * Revision 2.1  1997/08/03 15:28:09  keil
 * release L3 empty processes
 *
 * Revision 2.0  1997/07/27 21:15:45  keil
 * New Callref based layer3
 *
 * Revision 1.12  1997/06/26 11:11:45  keil
 * SET_SKBFREE now on creation of a SKB
 *
 * Revision 1.11  1997/04/06 22:54:18  keil
 * Using SKB's
 *
 * Old Log removed /KKe
 *
 */

#define __NO_VERSION__
#include "hisax.h"
#include "l3_1tr6.h"
#include "isdnl3.h"
#include <linux/ctype.h>

extern char *HiSax_getrev(const char *revision);
const char *l3_1tr6_revision = "$Revision: 2.4 $";

#define MsgHead(ptr, cref, mty, dis) \
	*ptr++ = dis; \
	*ptr++ = 0x1; \
	*ptr++ = cref ^ 0x80; \
	*ptr++ = mty

static void
l3_1TR6_message(struct l3_process *pc, u_char mt, u_char pd)
{
	struct sk_buff *skb;
	u_char *p;

	if (!(skb = l3_alloc_skb(4)))
		return;
	p = skb_put(skb, 4);
	MsgHead(p, pc->callref, mt, pd);
	pc->st->l3.l3l2(pc->st, DL_DATA, skb);
}

static int
l31tr6_check_messagetype_validity(int mt, int pd) {
/* verify if a message type exists */

	if (pd == PROTO_DIS_N0)
		switch(mt) {
		   case MT_N0_REG_IND:
		   case MT_N0_CANC_IND:
		   case MT_N0_FAC_STA:
		   case MT_N0_STA_ACK:
		   case MT_N0_STA_REJ:
		   case MT_N0_FAC_INF:
		   case MT_N0_INF_ACK:
		   case MT_N0_INF_REJ:
		   case MT_N0_CLOSE:
		   case MT_N0_CLO_ACK:
			return(1);
		   default:
			return(0);
		}
	else if (pd == PROTO_DIS_N1)
		switch(mt) {
		   case MT_N1_ESC:
		   case MT_N1_ALERT:
		   case MT_N1_CALL_SENT:
		   case MT_N1_CONN:
		   case MT_N1_CONN_ACK:
		   case MT_N1_SETUP:
		   case MT_N1_SETUP_ACK:
		   case MT_N1_RES:
		   case MT_N1_RES_ACK:
		   case MT_N1_RES_REJ:
		   case MT_N1_SUSP:
		   case MT_N1_SUSP_ACK:
		   case MT_N1_SUSP_REJ:
		   case MT_N1_USER_INFO:
		   case MT_N1_DET:
		   case MT_N1_DISC:
		   case MT_N1_REL:
		   case MT_N1_REL_ACK:
		   case MT_N1_CANC_ACK:
		   case MT_N1_CANC_REJ:
		   case MT_N1_CON_CON:
		   case MT_N1_FAC:
		   case MT_N1_FAC_ACK:
		   case MT_N1_FAC_CAN:
		   case MT_N1_FAC_REG:
		   case MT_N1_FAC_REJ:
		   case MT_N1_INFO:
		   case MT_N1_REG_ACK:
		   case MT_N1_REG_REJ:
		   case MT_N1_STAT:
		   	return (1);
		   default:
		   	return(0);
		}
	return(0);
}

static void
l3_1tr6_setup_req(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb;
	u_char tmp[128];
	u_char *p = tmp;
	u_char *teln;
	u_char *eaz;
	u_char channel = 0;
	int l;

	MsgHead(p, pc->callref, MT_N1_SETUP, PROTO_DIS_N1);
	teln = pc->para.setup.phone;
	pc->para.spv = 0;
	if (!isdigit(*teln)) {
		switch (0x5f & *teln) {
			case 'S':
				pc->para.spv = 1;
				break;
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
			default:
				if (pc->st->l3.debug & L3_DEB_WARN)
					l3_debug(pc->st, "Wrong MSN Code");
				break;
		}
		teln++;
	}
	if (channel) {
		*p++ = 0x18;	/* channel indicator */
		*p++ = 1;
		*p++ = channel;
	}
	if (pc->para.spv) {	/* SPV ? */
		/* NSF SPV */
		*p++ = WE0_netSpecFac;
		*p++ = 4;	/* Laenge */
		*p++ = 0;
		*p++ = FAC_SPV;	/* SPV */
		*p++ = pc->para.setup.si1;	/* 0 for all Services */
		*p++ = pc->para.setup.si2;	/* 0 for all Services */
		*p++ = WE0_netSpecFac;
		*p++ = 4;	/* Laenge */
		*p++ = 0;
		*p++ = FAC_Activate;	/* aktiviere SPV (default) */
		*p++ = pc->para.setup.si1;	/* 0 for all Services */
		*p++ = pc->para.setup.si2;	/* 0 for all Services */
	}
	eaz = pc->para.setup.eazmsn;
	if (*eaz) {
		*p++ = WE0_origAddr;
		*p++ = strlen(eaz) + 1;
		/* Classify as AnyPref. */
		*p++ = 0x81;	/* Ext = '1'B, Type = '000'B, Plan = '0001'B. */
		while (*eaz)
			*p++ = *eaz++ & 0x7f;
	}
	*p++ = WE0_destAddr;
	*p++ = strlen(teln) + 1;
	/* Classify as AnyPref. */
	*p++ = 0x81;		/* Ext = '1'B, Type = '000'B, Plan = '0001'B. */
	while (*teln)
		*p++ = *teln++ & 0x7f;

	*p++ = WE_Shift_F6;
	/* Codesatz 6 fuer Service */
	*p++ = WE6_serviceInd;
	*p++ = 2;		/* len=2 info,info2 */
	*p++ = pc->para.setup.si1;
	*p++ = pc->para.setup.si2;

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
l3_1tr6_setup(struct l3_process *pc, u_char pr, void *arg)
{
	u_char *p;
	int bcfound = 0;
	char tmp[80];
	struct sk_buff *skb = arg;

	p = skb->data;

	/* Channel Identification */
	p = skb->data;
	if ((p = findie(p, skb->len, WE0_chanID, 0))) {
		pc->para.bchannel = p[2] & 0x3;
		bcfound++;
	} else if (pc->st->l3.debug & L3_DEB_WARN)
		l3_debug(pc->st, "setup without bchannel");

	p = skb->data;
	if ((p = findie(p, skb->len, WE6_serviceInd, 6))) {
		pc->para.setup.si1 = p[2];
		pc->para.setup.si2 = p[3];
	} else if (pc->st->l3.debug & L3_DEB_WARN)
		l3_debug(pc->st, "setup without service indicator");

	p = skb->data;
	if ((p = findie(p, skb->len, WE0_destAddr, 0)))
		iecpy(pc->para.setup.eazmsn, p, 1);
	else
		pc->para.setup.eazmsn[0] = 0;

	p = skb->data;
	if ((p = findie(p, skb->len, WE0_origAddr, 0))) {
		iecpy(pc->para.setup.phone, p, 1);
	} else
		pc->para.setup.phone[0] = 0;

	p = skb->data;
	pc->para.spv = 0;
	if ((p = findie(p, skb->len, WE0_netSpecFac, 0))) {
		if ((FAC_SPV == p[3]) || (FAC_Activate == p[3]))
			pc->para.spv = 1;
	}
	dev_kfree_skb(skb);

	/* Signal all services, linklevel takes care of Service-Indicator */
	if (bcfound) {
		if ((pc->para.setup.si1 != 7) && (pc->st->l3.debug & L3_DEB_WARN)) {
			sprintf(tmp, "non-digital call: %s -> %s",
				pc->para.setup.phone,
				pc->para.setup.eazmsn);
			l3_debug(pc->st, tmp);
		}
		newl3state(pc, 6);
		pc->st->l3.l3l4(pc, CC_SETUP_IND, NULL);
	} else
		release_l3_process(pc);
}

static void
l3_1tr6_setup_ack(struct l3_process *pc, u_char pr, void *arg)
{
	u_char *p;
	struct sk_buff *skb = arg;

	L3DelTimer(&pc->timer);
	p = skb->data;
	newl3state(pc, 2);
	if ((p = findie(p, skb->len, WE0_chanID, 0))) {
		pc->para.bchannel = p[2] & 0x3;
	} else if (pc->st->l3.debug & L3_DEB_WARN)
		l3_debug(pc->st, "setup answer without bchannel");
	dev_kfree_skb(skb);
	L3AddTimer(&pc->timer, T304, CC_T304);
	pc->st->l3.l3l4(pc, CC_MORE_INFO, NULL);
}

static void
l3_1tr6_call_sent(struct l3_process *pc, u_char pr, void *arg)
{
	u_char *p;
	struct sk_buff *skb = arg;

	L3DelTimer(&pc->timer);
	p = skb->data;
	if ((p = findie(p, skb->len, WE0_chanID, 0))) {
		pc->para.bchannel = p[2] & 0x3;
	} else if (pc->st->l3.debug & L3_DEB_WARN)
		l3_debug(pc->st, "setup answer without bchannel");
	dev_kfree_skb(skb);
	L3AddTimer(&pc->timer, T310, CC_T310);
	newl3state(pc, 3);
	pc->st->l3.l3l4(pc, CC_PROCEEDING_IND, NULL);
}

static void
l3_1tr6_alert(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;

	dev_kfree_skb(skb);
	L3DelTimer(&pc->timer);	/* T304 */
	newl3state(pc, 4);
	pc->st->l3.l3l4(pc, CC_ALERTING_IND, NULL);
}

static void
l3_1tr6_info(struct l3_process *pc, u_char pr, void *arg)
{
	u_char *p;
	int i, tmpcharge = 0;
	char a_charge[8], tmp[32];
	struct sk_buff *skb = arg;

	p = skb->data;
	if ((p = findie(p, skb->len, WE6_chargingInfo, 6))) {
		iecpy(a_charge, p, 1);
		for (i = 0; i < strlen(a_charge); i++) {
			tmpcharge *= 10;
			tmpcharge += a_charge[i] & 0xf;
		}
		if (tmpcharge > pc->para.chargeinfo) {
			pc->para.chargeinfo = tmpcharge;
			pc->st->l3.l3l4(pc, CC_INFO_CHARGE, NULL);
		}
		if (pc->st->l3.debug & L3_DEB_CHARGE) {
			sprintf(tmp, "charging info %d", pc->para.chargeinfo);
			l3_debug(pc->st, tmp);
		}
	} else if (pc->st->l3.debug & L3_DEB_CHARGE)
		l3_debug(pc->st, "charging info not found");
	dev_kfree_skb(skb);

}

static void
l3_1tr6_info_s2(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;

	dev_kfree_skb(skb);
}

static void
l3_1tr6_connect(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;

	L3DelTimer(&pc->timer);	/* T310 */
	newl3state(pc, 10);
	dev_kfree_skb(skb);
	pc->para.chargeinfo = 0;
	pc->st->l3.l3l4(pc, CC_SETUP_CNF, NULL);
}

static void
l3_1tr6_rel(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	u_char *p;

	p = skb->data;
	if ((p = findie(p, skb->len, WE0_cause, 0))) {
		if (p[1] > 0) {
			pc->para.cause = p[2];
			if (p[1] > 1)
				pc->para.loc = p[3];
			else
				pc->para.loc = 0;
		} else {
			pc->para.cause = 0;
			pc->para.loc = 0;
		}
	} else
		pc->para.cause = -1;
	dev_kfree_skb(skb);
	StopAllL3Timer(pc);
	newl3state(pc, 0);
	l3_1TR6_message(pc, MT_N1_REL_ACK, PROTO_DIS_N1);
	pc->st->l3.l3l4(pc, CC_RELEASE_IND, NULL);
	release_l3_process(pc);
}

static void
l3_1tr6_rel_ack(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;

	dev_kfree_skb(skb);
	StopAllL3Timer(pc);
	newl3state(pc, 0);
	pc->para.cause = -1;
	pc->st->l3.l3l4(pc, CC_RELEASE_CNF, NULL);
	release_l3_process(pc);
}

static void
l3_1tr6_disc(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	u_char *p;
	int i, tmpcharge = 0;
	char a_charge[8], tmp[32];

	StopAllL3Timer(pc);
	p = skb->data;
	if ((p = findie(p, skb->len, WE6_chargingInfo, 6))) {
		iecpy(a_charge, p, 1);
		for (i = 0; i < strlen(a_charge); i++) {
			tmpcharge *= 10;
			tmpcharge += a_charge[i] & 0xf;
		}
		if (tmpcharge > pc->para.chargeinfo) {
			pc->para.chargeinfo = tmpcharge;
			pc->st->l3.l3l4(pc, CC_INFO_CHARGE, NULL);
		}
		if (pc->st->l3.debug & L3_DEB_CHARGE) {
			sprintf(tmp, "charging info %d", pc->para.chargeinfo);
			l3_debug(pc->st, tmp);
		}
	} else if (pc->st->l3.debug & L3_DEB_CHARGE)
		l3_debug(pc->st, "charging info not found");


	p = skb->data;
	if ((p = findie(p, skb->len, WE0_cause, 0))) {
		if (p[1] > 0) {
			pc->para.cause = p[2];
			if (p[1] > 1)
				pc->para.loc = p[3];
			else
				pc->para.loc = 0;
		} else {
			pc->para.cause = 0;
			pc->para.loc = 0;
		}
	} else {
		if (pc->st->l3.debug & L3_DEB_WARN)
			l3_debug(pc->st, "cause not found");
		pc->para.cause = -1;
	}
	dev_kfree_skb(skb);
	newl3state(pc, 12);
	pc->st->l3.l3l4(pc, CC_DISCONNECT_IND, NULL);
}


static void
l3_1tr6_connect_ack(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;

	dev_kfree_skb(skb);
	newl3state(pc, 10);
	pc->para.chargeinfo = 0;
	L3DelTimer(&pc->timer);
	pc->st->l3.l3l4(pc, CC_SETUP_COMPLETE_IND, NULL);
}

static void
l3_1tr6_alert_req(struct l3_process *pc, u_char pr, void *arg)
{
	newl3state(pc, 7);
	l3_1TR6_message(pc, MT_N1_ALERT, PROTO_DIS_N1);
}

static void
l3_1tr6_setup_rsp(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb;
	u_char tmp[24];
	u_char *p = tmp;
	int l;

	MsgHead(p, pc->callref, MT_N1_CONN, PROTO_DIS_N1);
	if (pc->para.spv) {	/* SPV ? */
		/* NSF SPV */
		*p++ = WE0_netSpecFac;
		*p++ = 4;	/* Laenge */
		*p++ = 0;
		*p++ = FAC_SPV;	/* SPV */
		*p++ = pc->para.setup.si1;
		*p++ = pc->para.setup.si2;
		*p++ = WE0_netSpecFac;
		*p++ = 4;	/* Laenge */
		*p++ = 0;
		*p++ = FAC_Activate;	/* aktiviere SPV */
		*p++ = pc->para.setup.si1;
		*p++ = pc->para.setup.si2;
	}
	newl3state(pc, 8);
	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	pc->st->l3.l3l2(pc->st, DL_DATA, skb);
	L3DelTimer(&pc->timer);
	L3AddTimer(&pc->timer, T313, CC_T313);
}

static void
l3_1tr6_reset(struct l3_process *pc, u_char pr, void *arg)
{
	release_l3_process(pc);
}

static void
l3_1tr6_disconnect_req(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb;
	u_char tmp[16];
	u_char *p = tmp;
	int l;
	u_char cause = 0x10;
	u_char clen = 1;

	if (pc->para.cause > 0)
		cause = pc->para.cause;
	/* Map DSS1 causes */
	switch (cause & 0x7f) {
		case 0x10:
			clen = 0;
			break;
		case 0x15:
			cause = CAUSE_CallRejected;
			break;
	}
	StopAllL3Timer(pc);
	MsgHead(p, pc->callref, MT_N1_DISC, PROTO_DIS_N1);
	*p++ = WE0_cause;
	*p++ = clen;		/* Laenge */
	if (clen)
		*p++ = cause | 0x80;
	newl3state(pc, 11);
	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	pc->st->l3.l3l2(pc->st, DL_DATA, skb);
	L3AddTimer(&pc->timer, T305, CC_T305);
}

static void
l3_1tr6_release_req(struct l3_process *pc, u_char pr, void *arg)
{
	StopAllL3Timer(pc);
	newl3state(pc, 19);
	l3_1TR6_message(pc, MT_N1_REL, PROTO_DIS_N1);
	L3AddTimer(&pc->timer, T308, CC_T308_1);
}

static void
l3_1tr6_t303(struct l3_process *pc, u_char pr, void *arg)
{
	if (pc->N303 > 0) {
		pc->N303--;
		L3DelTimer(&pc->timer);
		l3_1tr6_setup_req(pc, pr, arg);
	} else {
		L3DelTimer(&pc->timer);
		pc->st->l3.l3l4(pc, CC_NOSETUP_RSP_ERR, NULL);
		release_l3_process(pc);
	}
}

static void
l3_1tr6_t304(struct l3_process *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);
	pc->para.cause = 0xE6;
	l3_1tr6_disconnect_req(pc, pr, NULL);
	pc->st->l3.l3l4(pc, CC_SETUP_ERR, NULL);
}

static void
l3_1tr6_t305(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb;
	u_char tmp[16];
	u_char *p = tmp;
	int l;
	u_char cause = 0x90;
	u_char clen = 1;

	L3DelTimer(&pc->timer);
	if (pc->para.cause > 0)
		cause = pc->para.cause;
	/* Map DSS1 causes */
	switch (cause & 0x7f) {
		case 0x10:
			clen = 0;
			break;
		case 0x15:
			cause = CAUSE_CallRejected;
			break;
	}
	MsgHead(p, pc->callref, MT_N1_REL, PROTO_DIS_N1);
	*p++ = WE0_cause;
	*p++ = clen;		/* Laenge */
	if (clen)
		*p++ = cause;
	newl3state(pc, 19);
	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	pc->st->l3.l3l2(pc->st, DL_DATA, skb);
	L3AddTimer(&pc->timer, T308, CC_T308_1);
}

static void
l3_1tr6_t310(struct l3_process *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);
	pc->para.cause = 0xE6;
	l3_1tr6_disconnect_req(pc, pr, NULL);
	pc->st->l3.l3l4(pc, CC_SETUP_ERR, NULL);
}

static void
l3_1tr6_t313(struct l3_process *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);
	pc->para.cause = 0xE6;
	l3_1tr6_disconnect_req(pc, pr, NULL);
	pc->st->l3.l3l4(pc, CC_CONNECT_ERR, NULL);
}

static void
l3_1tr6_t308_1(struct l3_process *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);
	l3_1TR6_message(pc, MT_N1_REL, PROTO_DIS_N1);
	L3AddTimer(&pc->timer, T308, CC_T308_2);
	newl3state(pc, 19);
}

static void
l3_1tr6_t308_2(struct l3_process *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);
	pc->st->l3.l3l4(pc, CC_RELEASE_ERR, NULL);
	release_l3_process(pc);
}
/* *INDENT-OFF* */
static struct stateentry downstl[] =
{
	{SBIT(0),
	 CC_SETUP_REQ, l3_1tr6_setup_req},
   	{SBIT(1) | SBIT(2) | SBIT(3) | SBIT(4) | SBIT(6) | SBIT(7) | SBIT(8) |
    	 SBIT(10),
    	 CC_DISCONNECT_REQ, l3_1tr6_disconnect_req},
	{SBIT(12),
	 CC_RELEASE_REQ, l3_1tr6_release_req},
	{ALL_STATES,
	 CC_DLRL, l3_1tr6_reset},
	{SBIT(6),
	 CC_IGNORE, l3_1tr6_reset},
	{SBIT(6),
	 CC_REJECT_REQ, l3_1tr6_disconnect_req},
	{SBIT(6),
	 CC_ALERTING_REQ, l3_1tr6_alert_req},
	{SBIT(6) | SBIT(7),
	 CC_SETUP_RSP, l3_1tr6_setup_rsp},
	{SBIT(1),
	 CC_T303, l3_1tr6_t303},
	{SBIT(2),
	 CC_T304, l3_1tr6_t304},
	{SBIT(3),
	 CC_T310, l3_1tr6_t310},
	{SBIT(8),
	 CC_T313, l3_1tr6_t313},
	{SBIT(11),
	 CC_T305, l3_1tr6_t305},
	{SBIT(19),
	 CC_T308_1, l3_1tr6_t308_1},
	{SBIT(19),
	 CC_T308_2, l3_1tr6_t308_2},
};

static int downstl_len = sizeof(downstl) /
sizeof(struct stateentry);

static struct stateentry datastln1[] =
{
	{SBIT(0),
	 MT_N1_SETUP, l3_1tr6_setup},
	{SBIT(1),
	 MT_N1_SETUP_ACK, l3_1tr6_setup_ack},
	{SBIT(1) | SBIT(2),
	 MT_N1_CALL_SENT, l3_1tr6_call_sent},
	{SBIT(1) | SBIT(2) | SBIT(3) | SBIT(4) | SBIT(7) | SBIT(8) | SBIT(10),
	 MT_N1_DISC, l3_1tr6_disc},
	{SBIT(2) | SBIT(3) | SBIT(4),
	 MT_N1_ALERT, l3_1tr6_alert},
	{SBIT(2) | SBIT(3) | SBIT(4),
	 MT_N1_CONN, l3_1tr6_connect},
	{SBIT(2),
	 MT_N1_INFO, l3_1tr6_info_s2},
	{SBIT(8),
	 MT_N1_CONN_ACK, l3_1tr6_connect_ack},
	{SBIT(10),
	 MT_N1_INFO, l3_1tr6_info},
	{SBIT(0) | SBIT(1) | SBIT(2) | SBIT(3) | SBIT(4) | SBIT(7) | SBIT(8) |
	 SBIT(10) | SBIT(11) | SBIT(12) | SBIT(15) | SBIT(17) | SBIT(19),
	 MT_N1_REL, l3_1tr6_rel},
	{SBIT(19),
	 MT_N1_REL_ACK, l3_1tr6_rel_ack}
};
/* *INDENT-ON* */




static int datastln1_len = sizeof(datastln1) /
sizeof(struct stateentry);

static void
up1tr6(struct PStack *st, int pr, void *arg)
{
	int i, mt, cr;
	struct l3_process *proc;
	struct sk_buff *skb = arg;
	char tmp[80];

	if (skb->len < 4) {
		if (st->l3.debug & L3_DEB_PROTERR) {
			sprintf(tmp, "up1tr6 len only %d", skb->len);
			l3_debug(st, tmp);
		}
		dev_kfree_skb(skb);
		return;
	}
	if ((skb->data[0] & 0xfe) != PROTO_DIS_N0) {
		if (st->l3.debug & L3_DEB_PROTERR) {
			sprintf(tmp, "up1tr6%sunexpected discriminator %x message len %d",
				(pr == DL_DATA) ? " " : "(broadcast) ",
				skb->data[0], skb->len);
			l3_debug(st, tmp);
		}
		dev_kfree_skb(skb);
		return;
	}
	if (skb->data[1] != 1) {
		if (st->l3.debug & L3_DEB_PROTERR) {
			sprintf(tmp, "up1tr6 CR len not 1");
			l3_debug(st, tmp);
		}
		dev_kfree_skb(skb);
		return;
	}
	cr = skb->data[2];
	mt = skb->data[3];
	if (skb->data[0] == PROTO_DIS_N0) {
		dev_kfree_skb(skb);
		if (st->l3.debug & L3_DEB_STATE) {
			sprintf(tmp, "up1tr6%s N0 mt %x unhandled",
			     (pr == DL_DATA) ? " " : "(broadcast) ", mt);
			l3_debug(st, tmp);
		}
	} else if (skb->data[0] == PROTO_DIS_N1) {
		if (!(proc = getl3proc(st, cr))) {
			if ((mt == MT_N1_SETUP) && (cr < 128)) {
				if (!(proc = new_l3_process(st, cr))) {
					if (st->l3.debug & L3_DEB_PROTERR) {
						sprintf(tmp, "up1tr6 no roc mem");
						l3_debug(st, tmp);
					}
					dev_kfree_skb(skb);
					return;
				}
			} else {
				dev_kfree_skb(skb);
				return;
			}
		}
		for (i = 0; i < datastln1_len; i++)
			if ((mt == datastln1[i].primitive) &&
			    ((1 << proc->state) & datastln1[i].state))
				break;
		if (i == datastln1_len) {
			dev_kfree_skb(skb);
			if (st->l3.debug & L3_DEB_STATE) {
				sprintf(tmp, "up1tr6%sstate %d mt %x unhandled",
				  (pr == DL_DATA) ? " " : "(broadcast) ",
					proc->state, mt);
				l3_debug(st, tmp);
			}
			return;
		} else {
			if (st->l3.debug & L3_DEB_STATE) {
				sprintf(tmp, "up1tr6%sstate %d mt %x",
				  (pr == DL_DATA) ? " " : "(broadcast) ",
					proc->state, mt);
				l3_debug(st, tmp);
			}
			datastln1[i].rout(proc, pr, skb);
		}
	}
}

static void
down1tr6(struct PStack *st, int pr, void *arg)
{
	int i, cr;
	struct l3_process *proc;
	struct Channel *chan;
	char tmp[80];

	if (CC_SETUP_REQ == pr) {
		chan = arg;
		cr = newcallref();
		cr |= 0x80;
		if (!(proc = new_l3_process(st, cr))) {
			return;
		} else {
			proc->chan = chan;
			chan->proc = proc;
			proc->para.setup = chan->setup;
			proc->callref = cr;
		}
	} else {
		proc = arg;
	}

	for (i = 0; i < downstl_len; i++)
		if ((pr == downstl[i].primitive) &&
		    ((1 << proc->state) & downstl[i].state))
			break;
	if (i == downstl_len) {
		if (st->l3.debug & L3_DEB_STATE) {
			sprintf(tmp, "down1tr6 state %d prim %d unhandled",
				proc->state, pr);
			l3_debug(st, tmp);
		}
	} else {
		if (st->l3.debug & L3_DEB_STATE) {
			sprintf(tmp, "down1tr6 state %d prim %d",
				proc->state, pr);
			l3_debug(st, tmp);
		}
		downstl[i].rout(proc, pr, arg);
	}
}

void
setstack_1tr6(struct PStack *st)
{
	char tmp[64];

	st->lli.l4l3 = down1tr6;
	st->l2.l2l3 = up1tr6;
	st->l3.N303 = 0;

	strcpy(tmp, l3_1tr6_revision);
	printk(KERN_INFO "HiSax: 1TR6 Rev. %s\n", HiSax_getrev(tmp));
}
