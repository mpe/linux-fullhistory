/* $Id: tei.c,v 2.7 1998/02/12 23:08:11 keil Exp $

 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *              based on the teles driver from Jan den Ouden
 *
 * Thanks to    Jan den Ouden
 *              Fritz Elfert
 *
 * $Log: tei.c,v $
 * Revision 2.7  1998/02/12 23:08:11  keil
 * change for 2.1.86 (removing FREE_READ/FREE_WRITE from [dev]_kfree_skb()
 *
 * Revision 2.6  1998/02/02 13:41:42  keil
 * fix MDL_ASSIGN for PtP
 *
 * Revision 2.5  1997/11/06 17:09:12  keil
 * New 2.1 init code
 *
 * Revision 2.4  1997/10/29 19:04:46  keil
 * changes for 2.1
 *
 * Revision 2.3  1997/10/01 09:21:43  fritz
 * Removed old compatibility stuff for 2.0.X kernels.
 * From now on, this code is for 2.1.X ONLY!
 * Old stuff is still in the separate branch.
 *
 * Revision 2.2  1997/07/31 19:24:39  keil
 * fixed a warning
 *
 * Revision 2.1  1997/07/31 11:50:16  keil
 * ONE TEI and FIXED TEI handling
 *
 * Revision 2.0  1997/07/27 21:13:30  keil
 * New TEI managment
 *
 * Revision 1.9  1997/06/26 11:18:02  keil
 * New managment
 *
 * Revision 1.8  1997/04/07 22:59:08  keil
 * GFP_KERNEL --> GFP_ATOMIC
 *
 * Revision 1.7  1997/04/06 22:54:03  keil
 * Using SKB's
 *
 * Old log removed/ KKe
 *
 */
#define __NO_VERSION__
#include "hisax.h"
#include "isdnl2.h"
#include <linux/random.h>

const char *tei_revision = "$Revision: 2.7 $";

#define ID_REQUEST	1
#define ID_ASSIGNED	2
#define ID_DENIED	3
#define ID_CHK_REQ	4
#define ID_CHK_RES	5
#define ID_REMOVE	6
#define ID_VERIFY	7

#define TEI_ENTITY_ID	0xf

static
struct Fsm teifsm =
{NULL, 0, 0, NULL, NULL};

void tei_handler(struct PStack *st, u_char pr, struct sk_buff *skb);

enum {
	ST_TEI_NOP,
	ST_TEI_IDREQ,
	ST_TEI_IDVERIFY,
};

#define TEI_STATE_COUNT (ST_TEI_IDVERIFY+1)

static char *strTeiState[] =
{
	"ST_TEI_NOP",
	"ST_TEI_IDREQ",
	"ST_TEI_IDVERIFY",
};

enum {
	EV_IDREQ,
	EV_ASSIGN,
	EV_DENIED,
	EV_CHKREQ,
	EV_REMOVE,
	EV_VERIFY,
	EV_T202,
};

#define TEI_EVENT_COUNT (EV_T202+1)

static char *strTeiEvent[] =
{
	"EV_IDREQ",
	"EV_ASSIGN",
	"EV_DENIED",
	"EV_CHKREQ",
	"EV_REMOVE",
	"EV_VERIFY",
	"EV_T202",
};

unsigned int
random_ri(void)
{
	unsigned int x;

	get_random_bytes(&x, sizeof(x));
	return (x & 0xffff);
}

static struct PStack *
findtei(struct PStack *st, int tei)
{
	struct PStack *ptr = *(st->l1.stlistp);

	if (tei == 127)
		return (NULL);

	while (ptr)
		if (ptr->l2.tei == tei)
			return (ptr);
		else
			ptr = ptr->next;
	return (NULL);
}

static void
put_tei_msg(struct PStack *st, u_char m_id, unsigned int ri, u_char tei)
{
	struct sk_buff *skb;
	u_char *bp;

	if (!(skb = alloc_skb(8, GFP_ATOMIC))) {
		printk(KERN_WARNING "HiSax: No skb for TEI manager\n");
		return;
	}
	bp = skb_put(skb, 3);
	bp[0] = (TEI_SAPI << 2);
	bp[1] = (GROUP_TEI << 1) | 0x1;
	bp[2] = UI;
	bp = skb_put(skb, 5);
	bp[0] = TEI_ENTITY_ID;
	bp[1] = ri >> 8;
	bp[2] = ri & 0xff;
	bp[3] = m_id;
	bp[4] = (tei << 1) | 1;
	st->l2.l2l1(st, PH_DATA_REQ, skb);
}

static void
tei_id_request(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	char tmp[64];

	if (st->l2.tei != -1) {
		sprintf(tmp, "assign request for allready asigned tei %d",
			st->l2.tei);
		st->ma.tei_m.printdebug(&st->ma.tei_m, tmp);
		return;
	}
	st->ma.ri = random_ri();
	if (st->ma.debug) {
		sprintf(tmp, "assign request ri %d", st->ma.ri);
		st->ma.tei_m.printdebug(&st->ma.tei_m, tmp);
	}
	put_tei_msg(st, ID_REQUEST, st->ma.ri, 127);
	FsmChangeState(&st->ma.tei_m, ST_TEI_IDREQ);
	FsmAddTimer(&st->ma.t202, st->ma.T202, EV_T202, NULL, 1);
	st->ma.N202 = 3;
}

static void
tei_id_assign(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *ost, *st = fi->userdata;
	struct sk_buff *skb = arg;
	struct IsdnCardState *cs;
	int ri, tei;
	char tmp[64];

	ri = ((unsigned int) skb->data[1] << 8) + skb->data[2];
	tei = skb->data[4] >> 1;
	if (st->ma.debug) {
		sprintf(tmp, "identity assign ri %d tei %d", ri, tei);
		st->ma.tei_m.printdebug(&st->ma.tei_m, tmp);
	}
	if ((ost = findtei(st, tei))) {		/* same tei is in use */
		if (ri != ost->ma.ri) {
			sprintf(tmp, "possible duplicate assignment tei %d", tei);
			st->ma.tei_m.printdebug(&st->ma.tei_m, tmp);
			ost->l2.l2tei(ost, MDL_ERROR_REQ, NULL);
		}
	} else if (ri == st->ma.ri) {
		FsmDelTimer(&st->ma.t202, 1);
		FsmChangeState(&st->ma.tei_m, ST_TEI_NOP);
		st->ma.manl2(st, MDL_ASSIGN_REQ, (void *) (int) tei);
		cs = (struct IsdnCardState *) st->l1.hardware;
		cs->cardmsg(cs, MDL_ASSIGN_REQ, NULL);
	}
}

static void
tei_id_denied(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;
	int ri, tei;
	char tmp[64];

	ri = ((unsigned int) skb->data[1] << 8) + skb->data[2];
	tei = skb->data[4] >> 1;
	if (st->ma.debug) {
		sprintf(tmp, "identity denied ri %d tei %d", ri, tei);
		st->ma.tei_m.printdebug(&st->ma.tei_m, tmp);
	}
}

static void
tei_id_chk_req(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;
	int tei;
	char tmp[64];

	tei = skb->data[4] >> 1;
	if (st->ma.debug) {
		sprintf(tmp, "identity check req tei %d", tei);
		st->ma.tei_m.printdebug(&st->ma.tei_m, tmp);
	}
	if ((st->l2.tei != -1) && ((tei == GROUP_TEI) || (tei == st->l2.tei))) {
		FsmDelTimer(&st->ma.t202, 4);
		FsmChangeState(&st->ma.tei_m, ST_TEI_NOP);
		put_tei_msg(st, ID_CHK_RES, random_ri(), st->l2.tei);
	}
}

static void
tei_id_remove(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;
	struct IsdnCardState *cs;
	int tei;
	char tmp[64];

	tei = skb->data[4] >> 1;
	if (st->ma.debug) {
		sprintf(tmp, "identity remove tei %d", tei);
		st->ma.tei_m.printdebug(&st->ma.tei_m, tmp);
	}
	if ((st->l2.tei != -1) && ((tei == GROUP_TEI) || (tei == st->l2.tei))) {
		FsmDelTimer(&st->ma.t202, 5);
		FsmChangeState(&st->ma.tei_m, ST_TEI_NOP);
		st->ma.manl2(st, MDL_REMOVE_REQ, 0);
		cs = (struct IsdnCardState *) st->l1.hardware;
		cs->cardmsg(cs, MDL_REMOVE_REQ, NULL);
	}
}

static void
tei_id_verify(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	char tmp[64];

	if (st->ma.debug) {
		sprintf(tmp, "id verify request for tei %d", st->l2.tei);
		st->ma.tei_m.printdebug(&st->ma.tei_m, tmp);
	}
	put_tei_msg(st, ID_VERIFY, 0, st->l2.tei);
	FsmChangeState(&st->ma.tei_m, ST_TEI_IDVERIFY);
	FsmAddTimer(&st->ma.t202, st->ma.T202, EV_T202, NULL, 2);
	st->ma.N202 = 2;
}

static void
tei_id_req_tout(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	char tmp[64];
	struct IsdnCardState *cs;

	if (--st->ma.N202) {
		st->ma.ri = random_ri();
		if (st->ma.debug) {
			sprintf(tmp, "assign req(%d) ri %d",
				4 - st->ma.N202, st->ma.ri);
			st->ma.tei_m.printdebug(&st->ma.tei_m, tmp);
		}
		put_tei_msg(st, ID_REQUEST, st->ma.ri, 127);
		FsmAddTimer(&st->ma.t202, st->ma.T202, EV_T202, NULL, 3);
	} else {
		sprintf(tmp, "assign req failed");
		st->ma.tei_m.printdebug(&st->ma.tei_m, tmp);
		st->ma.manl2(st, MDL_ERROR_IND, 0);
		cs = (struct IsdnCardState *) st->l1.hardware;
		cs->cardmsg(cs, MDL_REMOVE_REQ, NULL);
		FsmChangeState(fi, ST_TEI_NOP);
	}
}

static void
tei_id_ver_tout(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	char tmp[64];
	struct IsdnCardState *cs;

	if (--st->ma.N202) {
		if (st->ma.debug) {
			sprintf(tmp, "id verify req(%d) for tei %d",
				3 - st->ma.N202, st->l2.tei);
			st->ma.tei_m.printdebug(&st->ma.tei_m, tmp);
		}
		put_tei_msg(st, ID_VERIFY, 0, st->l2.tei);
		FsmAddTimer(&st->ma.t202, st->ma.T202, EV_T202, NULL, 4);
	} else {
		sprintf(tmp, "verify req for tei %d failed", st->l2.tei);
		st->ma.tei_m.printdebug(&st->ma.tei_m, tmp);
		st->ma.manl2(st, MDL_REMOVE_REQ, 0);
		cs = (struct IsdnCardState *) st->l1.hardware;
		cs->cardmsg(cs, MDL_REMOVE_REQ, NULL);
		FsmChangeState(fi, ST_TEI_NOP);
	}
}

static void
tei_l1l2(struct PStack *st, int pr, void *arg)
{
	struct sk_buff *skb = arg;
	int mt;
	char tmp[64];

	if (pr == PH_DATA_IND) {
		if (skb->len < 3) {
			sprintf(tmp, "short mgr frame %d/3", skb->len);
			st->ma.tei_m.printdebug(&st->ma.tei_m, tmp);
		} else if (((skb->data[0] >> 2) != TEI_SAPI) ||
			   ((skb->data[1] >> 1) != GROUP_TEI)) {
			sprintf(tmp, "wrong mgr sapi/tei %x/%x",
				skb->data[0], skb->data[1]);
			st->ma.tei_m.printdebug(&st->ma.tei_m, tmp);
		} else if ((skb->data[2] & 0xef) != UI) {
			sprintf(tmp, "mgr frame is not ui %x",
				skb->data[2]);
			st->ma.tei_m.printdebug(&st->ma.tei_m, tmp);
		} else {
			skb_pull(skb, 3);
			if (skb->len < 5) {
				sprintf(tmp, "short mgr frame %d/5", skb->len);
				st->ma.tei_m.printdebug(&st->ma.tei_m, tmp);
			} else if (skb->data[0] != TEI_ENTITY_ID) {
				/* wrong management entity identifier, ignore */
				sprintf(tmp, "tei handler wrong entity id %x\n",
					skb->data[0]);
				st->ma.tei_m.printdebug(&st->ma.tei_m, tmp);
			} else {
				mt = skb->data[3];
				if (mt == ID_ASSIGNED)
					FsmEvent(&st->ma.tei_m, EV_ASSIGN, skb);
				else if (mt == ID_DENIED)
					FsmEvent(&st->ma.tei_m, EV_DENIED, skb);
				else if (mt == ID_CHK_REQ)
					FsmEvent(&st->ma.tei_m, EV_CHKREQ, skb);
				else if (mt == ID_REMOVE)
					FsmEvent(&st->ma.tei_m, EV_REMOVE, skb);
				else {
					sprintf(tmp, "tei handler wrong mt %x\n",
						mt);
					st->ma.tei_m.printdebug(&st->ma.tei_m, tmp);
				}
			}
		}
	} else {
		sprintf(tmp, "tei handler wrong pr %x\n", pr);
		st->ma.tei_m.printdebug(&st->ma.tei_m, tmp);
	}
	dev_kfree_skb(skb);
}

static void
tei_l2tei(struct PStack *st, int pr, void *arg)
{
	switch (pr) {
		case (MDL_ASSIGN_IND):
#ifdef TEI_FIXED
			if (st->ma.debug) {
				char tmp[64];
				sprintf(tmp, "fixed assign tei %d", TEI_FIXED);
				st->ma.tei_m.printdebug(&st->ma.tei_m, tmp);
			}
			st->ma.manl2(st, MDL_ASSIGN_REQ, (void *) (int) TEI_FIXED);
#else
			FsmEvent(&st->ma.tei_m, EV_IDREQ, arg);
#endif
			break;
		case (MDL_ERROR_REQ):
			FsmEvent(&st->ma.tei_m, EV_VERIFY, arg);
			break;
		default:
			break;
	}
}

static void
tei_debug(struct FsmInst *fi, char *s)
{
	struct PStack *st = fi->userdata;
	char tm[32], str[256];

	jiftime(tm, jiffies);
	sprintf(str, "%s Tei %s\n", tm, s);
	HiSax_putstatus(st->l1.hardware, str);
}

void
setstack_tei(struct PStack *st)
{
	st->l2.l2tei = tei_l2tei;
	st->ma.T202 = 2000;	/* T202  2000 milliseconds */
	st->l1.l1tei = tei_l1l2;
	st->ma.debug = 1;
	st->ma.tei_m.fsm = &teifsm;
	st->ma.tei_m.state = ST_TEI_NOP;
	st->ma.tei_m.debug = 1;
	st->ma.tei_m.userdata = st;
	st->ma.tei_m.userint = 0;
	st->ma.tei_m.printdebug = tei_debug;
	FsmInitTimer(&st->ma.tei_m, &st->ma.t202);
}

void
init_tei(struct IsdnCardState *sp, int protocol)
{

}

void
release_tei(struct IsdnCardState *cs)
{
	struct PStack *st = cs->stlist;

	while (st) {
		FsmDelTimer(&st->ma.t202, 1);
		st = st->next;
	}
}

static struct FsmNode TeiFnList[] HISAX_INITDATA =
{
	{ST_TEI_NOP, EV_IDREQ, tei_id_request},
	{ST_TEI_NOP, EV_VERIFY, tei_id_verify},
	{ST_TEI_NOP, EV_REMOVE, tei_id_remove},
	{ST_TEI_NOP, EV_CHKREQ, tei_id_chk_req},
	{ST_TEI_IDREQ, EV_T202, tei_id_req_tout},
	{ST_TEI_IDREQ, EV_ASSIGN, tei_id_assign},
	{ST_TEI_IDREQ, EV_DENIED, tei_id_denied},
	{ST_TEI_IDVERIFY, EV_T202, tei_id_ver_tout},
	{ST_TEI_IDVERIFY, EV_REMOVE, tei_id_remove},
	{ST_TEI_IDVERIFY, EV_CHKREQ, tei_id_chk_req},
};

#define TEI_FN_COUNT (sizeof(TeiFnList)/sizeof(struct FsmNode))

HISAX_INITFUNC(void
TeiNew(void))
{
	teifsm.state_count = TEI_STATE_COUNT;
	teifsm.event_count = TEI_EVENT_COUNT;
	teifsm.strEvent = strTeiEvent;
	teifsm.strState = strTeiState;
	FsmNew(&teifsm, TeiFnList, TEI_FN_COUNT);
}

void
TeiFree(void)
{
	FsmFree(&teifsm);
}
