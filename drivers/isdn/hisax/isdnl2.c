/* $Id: isdnl2.c,v 2.7 1998/02/12 23:07:47 keil Exp $

 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *              based on the teles driver from Jan den Ouden
 *
 * Thanks to    Jan den Ouden
 *              Fritz Elfert
 *
 * $Log: isdnl2.c,v $
 * Revision 2.7  1998/02/12 23:07:47  keil
 * change for 2.1.86 (removing FREE_READ/FREE_WRITE from [dev]_kfree_skb()
 *
 * Revision 2.6  1998/02/02 13:36:15  keil
 * bugfix X.75 win calculation
 *
 * Revision 2.5  1997/11/06 17:09:22  keil
 * New 2.1 init code
 *
 * Revision 2.4  1997/10/29 19:02:01  keil
 * new LL interface
 *
 * Revision 2.3  1997/10/01 09:21:39  fritz
 * Removed old compatibility stuff for 2.0.X kernels.
 * From now on, this code is for 2.1.X ONLY!
 * Old stuff is still in the separate branch.
 *
 * Revision 2.2  1997/07/31 11:49:05  keil
 * Eroor handling for no TEI assign
 *
 * Revision 2.1  1997/07/27 21:34:38  keil
 * cosmetics
 *
 * Revision 2.0  1997/06/26 11:07:29  keil
 * New q.921 and X.75 Layer2
 *
 *
 *  Old log removed KKe
 *
 */
#define __NO_VERSION__
#include "hisax.h"
#include "isdnl2.h"

const char *l2_revision = "$Revision: 2.7 $";

static void l2m_debug(struct FsmInst *fi, char *s);

static
struct Fsm l2fsm =
{NULL, 0, 0, NULL, NULL};

enum {
	ST_L2_1,
	ST_L2_2,
	ST_L2_3,
	ST_L2_4,
	ST_L2_5,
	ST_L2_6,
	ST_L2_7,
	ST_L2_8,
};

#define L2_STATE_COUNT (ST_L2_8+1)

static char *strL2State[] =
{
	"ST_L2_1",
	"ST_L2_2",
	"ST_L2_3",
	"ST_L2_4",
	"ST_L2_5",
	"ST_L2_6",
	"ST_L2_7",
	"ST_L2_8",
};

enum {
	EV_L2_UI,
	EV_L2_SABMX,
	EV_L2_DISC,
	EV_L2_DM,
	EV_L2_UA,
	EV_L2_FRMR,
	EV_L2_SUPER,
	EV_L2_I,
	EV_L2_DL_DATA,
	EV_L2_ACK_PULL,
	EV_L2_DL_UNIT_DATA,
	EV_L2_DL_ESTABLISH,
	EV_L2_DL_RELEASE,
	EV_L2_MDL_ASSIGN,
	EV_L2_MDL_REMOVE,
	EV_L2_MDL_ERROR,
	EV_L2_MDL_NOTEIPROC,
	EV_L1_DEACTIVATE,
	EV_L2_T200,
	EV_L2_T203,
};

#define L2_EVENT_COUNT (EV_L2_T203+1)

static char *strL2Event[] =
{
	"EV_L2_UI",
	"EV_L2_SABMX",
	"EV_L2_DISC",
	"EV_L2_DM",
	"EV_L2_UA",
	"EV_L2_FRMR",
	"EV_L2_SUPER",
	"EV_L2_I",
	"EV_L2_DL_DATA",
	"EV_L2_ACK_PULL",
	"EV_L2_DL_UNIT_DATA",
	"EV_L2_DL_ESTABLISH",
	"EV_L2_DL_RELEASE",
	"EV_L2_MDL_ASSIGN",
	"EV_L2_MDL_REMOVE",
	"EV_L2_MDL_ERROR",
	"EV_L2_MDL_NOTEIPROC",
	"EV_L1_DEACTIVATE",
	"EV_L2_T200",
	"EV_L2_T203",
};

static int l2addrsize(struct Layer2 *l2);

static void
InitWin(struct Layer2 *l2)
{
	int i;

	for (i = 0; i < MAX_WINDOW; i++)
		l2->windowar[i] = NULL;
}

static void
ReleaseWin(struct Layer2 *l2)
{
	int i, cnt = 0;

	for (i = 0; i < MAX_WINDOW; i++) {
		if (l2->windowar[i]) {
			cnt++;
			dev_kfree_skb(l2->windowar[i]);
			l2->windowar[i] = NULL;
		}
	}
	if (cnt)
		printk(KERN_WARNING "isdl2 freed %d skbuffs in release\n", cnt);
}

inline int
cansend(struct PStack *st)
{
	int p1;

	p1 = st->l2.vs - st->l2.va;
	if (p1 < 0)
		p1 += (test_bit(FLG_MOD128, &st->l2.flag) ? 128 : 8);
	return ((p1 < st->l2.window) && !test_bit(FLG_PEER_BUSY, &st->l2.flag));
}

static void
discard_i_queue(struct PStack *st)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&st->l2.i_queue))) {
		dev_kfree_skb(skb);
	}
}

static void
discard_ui_queue(struct PStack *st)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&st->l2.ui_queue))) {
		dev_kfree_skb(skb);
	}
}

inline void
clear_exception(struct Layer2 *l2)
{
	test_and_clear_bit(FLG_ACK_PEND, &l2->flag);
	test_and_clear_bit(FLG_REJEXC, &l2->flag);
	test_and_clear_bit(FLG_OWN_BUSY, &l2->flag);
	test_and_clear_bit(FLG_PEER_BUSY, &l2->flag);
}

inline int
l2headersize(struct Layer2 *l2, int ui)
{
	return (((test_bit(FLG_MOD128, &l2->flag) && (!ui)) ? 2 : 1) +
		(test_bit(FLG_LAPD, &l2->flag) ? 2 : 1));
}

inline int
l2addrsize(struct Layer2 *l2)
{
	return (test_bit(FLG_LAPD, &l2->flag) ? 2 : 1);
}

static int
sethdraddr(struct Layer2 *l2, u_char * header, int rsp)
{
	u_char *ptr = header;
	int crbit = rsp;

	if (test_bit(FLG_LAPD, &l2->flag)) {
		*ptr++ = (l2->sap << 2) | (rsp ? 2 : 0);
		*ptr++ = (l2->tei << 1) | 1;
		return (2);
	} else {
		if (test_bit(FLG_ORIG, &l2->flag))
			crbit = !crbit;
		if (crbit)
			*ptr++ = 1;
		else
			*ptr++ = 3;
		return (1);
	}
}

static void
enqueue_ui(struct PStack *st,
	   struct sk_buff *skb)
{
	st->l2.l2l1(st, PH_DATA_REQ, skb);
}

static void
enqueue_super(struct PStack *st,
	      struct sk_buff *skb)
{
	st->l2.l2l1(st, PH_DATA_REQ, skb);
}

inline int
IsUI(u_char * data, int ext)
{
	return ((data[0] & 0xef) == UI);
}

inline int
IsUA(u_char * data, int ext)
{
	return ((data[0] & 0xef) == UA);
}

inline int
IsDM(u_char * data, int ext)
{
	return ((data[0] & 0xef) == DM);
}

inline int
IsDISC(u_char * data, int ext)
{
	return ((data[0] & 0xef) == DISC);
}

inline int
IsRR(u_char * data, int ext)
{
	if (ext)
		return (data[0] == RR);
	else
		return ((data[0] & 0xf) == 1);
}

inline int
IsSABMX(u_char * data, int ext)
{
	u_char d = data[0] & ~0x10;

	return (ext ? d == SABME : d == SABM);
}

inline int
IsREJ(u_char * data, int ext)
{
	return (ext ? data[0] == REJ : (data[0] & 0xf) == REJ);
}

inline int
IsFRMR(u_char * data, int ext)
{
	return ((data[0] & 0xef) == FRMR);
}

inline int
IsRNR(u_char * data, int ext)
{
	return (ext ? data[0] == RNR : (data[0] & 0xf) == RNR);
}

static int
legalnr(struct PStack *st, int nr)
{
	struct Layer2 *l2 = &st->l2;
	int lnr, lvs;

	lvs = (l2->vs >= l2->va) ? l2->vs :
	    (l2->vs + (test_bit(FLG_MOD128, &l2->flag) ? 128 : 8));
	lnr = (nr >= l2->va) ? nr : (test_bit(FLG_MOD128, &l2->flag) ? 128 : 8);
	return (lnr <= lvs);
}

static void
setva(struct PStack *st, int nr)
{
	struct Layer2 *l2 = &st->l2;
	int len;

	while (l2->va != nr) {
		l2->va = (l2->va + 1) % (test_bit(FLG_MOD128, &l2->flag) ? 128 : 8);
		len = l2->windowar[l2->sow]->len;
		if (PACKET_NOACK == l2->windowar[l2->sow]->pkt_type)
			len = -1;
		dev_kfree_skb(l2->windowar[l2->sow]);
		l2->windowar[l2->sow] = NULL;
		l2->sow = (l2->sow + 1) % l2->window;
		if (st->lli.l2writewakeup && (len >=0))
			st->lli.l2writewakeup(st, len);
	}
}

static void
send_uframe(struct PStack *st, u_char cmd, u_char cr)
{
	struct sk_buff *skb;
	u_char tmp[MAX_HEADER_LEN];
	int i;

	i = sethdraddr(&st->l2, tmp, cr);
	tmp[i++] = cmd;
	if (!(skb = alloc_skb(i, GFP_ATOMIC))) {
		printk(KERN_WARNING "isdl2 can't alloc sbbuff for send_uframe\n");
		return;
	}
	memcpy(skb_put(skb, i), tmp, i);
	enqueue_super(st, skb);
}

inline u_char
get_PollFlag(struct PStack * st, struct sk_buff * skb)
{
	return (skb->data[l2addrsize(&(st->l2))] & 0x10);
}

inline void
FreeSkb(struct sk_buff *skb)
{
	dev_kfree_skb(skb);
}


inline u_char
get_PollFlagFree(struct PStack *st, struct sk_buff *skb)
{
	u_char PF;

	PF = get_PollFlag(st, skb);
	FreeSkb(skb);
	return (PF);
}

static void
establishlink(struct FsmInst *fi)
{
	struct PStack *st = fi->userdata;
	u_char cmd;

	clear_exception(&st->l2);
	st->l2.rc = 0;
	cmd = (test_bit(FLG_MOD128, &st->l2.flag) ? SABME : SABM) | 0x10;
	send_uframe(st, cmd, CMD);
	FsmDelTimer(&st->l2.t203, 1);
	FsmRestartTimer(&st->l2.t200, st->l2.T200, EV_L2_T200, NULL, 1);
	test_and_set_bit(FLG_T200_RUN, &st->l2.flag);
	FsmChangeState(fi, ST_L2_5);
}

static void
l2_mdl_error(struct FsmInst *fi, int event, void *arg)
{
	struct sk_buff *skb = arg;
	struct PStack *st = fi->userdata;

	switch (event) {
		case EV_L2_UA:
			if (get_PollFlagFree(st, skb))
				st->ma.layer(st, MDL_ERROR_IND, (void *) 'C');
			else
				st->ma.layer(st, MDL_ERROR_IND, (void *) 'D');
			break;
		case EV_L2_DM:
			if (get_PollFlagFree(st, skb))
				st->ma.layer(st, MDL_ERROR_IND, (void *) 'B');
			else {
				st->ma.layer(st, MDL_ERROR_IND, (void *) 'E');
				establishlink(fi);
				test_and_clear_bit(FLG_L3_INIT, &st->l2.flag);
			}
			break;
	}
}

static void
l2_dl_establish(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	int state = fi->state;

	FsmChangeState(fi, ST_L2_3); 
	if (state == ST_L2_1)
		st->l2.l2tei(st, MDL_ASSIGN_IND, NULL);
}

static void
l2_send_ui(struct PStack *st)
{
	struct sk_buff *skb;
	u_char header[MAX_HEADER_LEN];
	int i;

	i = sethdraddr(&(st->l2), header, CMD);
	header[i++] = UI;
	while ((skb = skb_dequeue(&st->l2.ui_queue))) {
		memcpy(skb_push(skb, i), header, i);
		enqueue_ui(st, skb);
	}
}

static void
l2_put_ui(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;

	skb_queue_tail(&st->l2.ui_queue, skb);
	if (fi->state == ST_L2_1) {
		FsmChangeState(fi, ST_L2_2);
		st->l2.l2tei(st, MDL_ASSIGN_IND, NULL);
	}
	if (fi->state > ST_L2_3)
		l2_send_ui(st);
}

static void
l2_got_ui(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;

	skb_pull(skb, l2headersize(&st->l2, 1));
	if (skb->len > st->l2.maxlen) { 
		st->ma.layer(st, MDL_ERROR_IND, (void *) 'O');
		FreeSkb(skb);
	} else
		st->l2.l2l3(st, DL_UNIT_DATA, skb);
}

static void
l2_establish(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	if (fi->state != ST_L2_4)
		discard_i_queue(st);
	if (fi->state != ST_L2_5)
		establishlink(fi);
	test_and_set_bit(FLG_L3_INIT, &st->l2.flag);
}

static void
l2_dl_release(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	if (fi->state == ST_L2_4) {
		st->l2.l2man(st, DL_RELEASE, NULL);
		return;
	} else if (fi->state == ST_L2_5) {
		test_and_set_bit(FLG_PEND_REL, &st->l2.flag);
		return;
	}
	discard_i_queue(st);
	FsmChangeState(fi, ST_L2_6);
	st->l2.rc = 0;
	send_uframe(st, DISC | 0x10, CMD);
	FsmDelTimer(&st->l2.t203, 1);
	FsmRestartTimer(&st->l2.t200, st->l2.T200, EV_L2_T200, NULL, 2);
	test_and_set_bit(FLG_T200_RUN, &st->l2.flag);
}

static void
l2_got_SABMX(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;
	int est = 1, state, rsp;
	u_char PollFlag;

	state = fi->state;
	rsp = *skb->data & 0x2;
	if (test_bit(FLG_ORIG, &st->l2.flag))
		rsp = !rsp;
	if (rsp) {
		st->ma.layer(st, MDL_ERROR_IND, (void *) 'L');
		FreeSkb(skb);
		if ((state == ST_L2_7) || (state == ST_L2_8))
			establishlink(fi);
		return;
	}	
	if (skb->len != (l2addrsize(&st->l2) + 1)) {
		st->ma.layer(st, MDL_ERROR_IND, (void *) 'N');
		FreeSkb(skb);
		if ((state == ST_L2_7) || (state == ST_L2_8))
			establishlink(fi);
		return;
	}
	PollFlag = get_PollFlagFree(st, skb);
	if (ST_L2_6 == state) {
		send_uframe(st, DM | PollFlag, RSP);
		return;
	} else
		send_uframe(st, UA | PollFlag, RSP);
	if (ST_L2_5 == state)
		return;
	if (ST_L2_4 != state) {
		st->ma.layer(st, MDL_ERROR_IND, (void *) 'F');
		if (st->l2.vs != st->l2.va) {
			discard_i_queue(st);
			est = 1;
		} else
			est = 0;
	}
	clear_exception(&st->l2);
	st->l2.vs = 0;
	st->l2.va = 0;
	st->l2.vr = 0;
	st->l2.sow = 0;
	FsmChangeState(fi, ST_L2_7);
	if (test_and_clear_bit(FLG_T200_RUN, &st->l2.flag))
		FsmDelTimer(&st->l2.t200, 2);
	FsmAddTimer(&st->l2.t203, st->l2.T203, EV_L2_T203, NULL, 3);

	if (est)
		st->l2.l2man(st, DL_ESTABLISH, NULL);

	if (ST_L2_8 == state)
		if (skb_queue_len(&st->l2.i_queue) && cansend(st))
			st->l2.l2l1(st, PH_PULL_REQ, NULL);
}

static void
l2_got_disconn(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;
	u_char PollFlag, cmd = UA;
	int state, rel = 1, cst = 1, rsp;

	state = fi->state;
	rsp = *skb->data & 0x2;
	if (test_bit(FLG_ORIG, &st->l2.flag))
		rsp = !rsp;

	if (rsp) {
		st->ma.layer(st, MDL_ERROR_IND, (void *) 'L');
		FreeSkb(skb);
		if ((state == ST_L2_7) || (state == ST_L2_8))
			establishlink(fi);
		return;
	}	
	if (skb->len != (l2addrsize(&st->l2) + 1)) {
		st->ma.layer(st, MDL_ERROR_IND, (void *) 'N');
		FreeSkb(skb);
		if ((state == ST_L2_7) || (state == ST_L2_8))
			establishlink(fi);
		return;
	}
	PollFlag = get_PollFlagFree(st, skb);
	if ((state == ST_L2_4) || (state == ST_L2_5)) {
		rel = 0;
		cst = 0;
		cmd = DM;
	} else if (state == ST_L2_6) {
		rel = 0;
		cst = 0;
	}
	if (cst) {
		FsmChangeState(fi, ST_L2_4);
		FsmDelTimer(&st->l2.t203, 3);
		if (test_and_clear_bit(FLG_T200_RUN, &st->l2.flag)) 
			FsmDelTimer(&st->l2.t200, 2);
	}
	send_uframe(st, cmd | PollFlag, RSP);
	if (rel)
		st->l2.l2man(st, DL_RELEASE, NULL);
}


static void
l2_got_ua(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;
	u_char PollFlag, est = 1;
	int state,rsp;

	state = fi->state;
	rsp = *skb->data & 0x2;
	if (test_bit(FLG_ORIG, &st->l2.flag))
		rsp = !rsp;

	if (!rsp) {
		st->ma.layer(st, MDL_ERROR_IND, (void *) 'L');
		FreeSkb(skb);
		if ((state == ST_L2_7) || (state == ST_L2_8))
			establishlink(fi);
		return;
	}	
	if (skb->len != (l2addrsize(&st->l2) + 1)) {
		st->ma.layer(st, MDL_ERROR_IND, (void *) 'N');
		FreeSkb(skb);
		if ((fi->state == ST_L2_7) || (fi->state == ST_L2_8))
			establishlink(fi);
		return;
	}
	PollFlag = get_PollFlag(st, skb);
	if (!PollFlag) {
		l2_mdl_error(fi, event, arg);
		return;
	}
	FreeSkb(skb);

	if (test_and_clear_bit(FLG_T200_RUN, &st->l2.flag))
		FsmDelTimer(&st->l2.t200, 2);
	if (fi->state == ST_L2_5) {
		if (test_and_clear_bit(FLG_PEND_REL, &st->l2.flag)) {
			discard_i_queue(st);
			st->l2.rc = 0;
			send_uframe(st, DISC | 0x10, CMD);
			FsmChangeState(fi, ST_L2_6);
			FsmAddTimer(&st->l2.t200, st->l2.T200, EV_L2_T200, NULL, 4);
			test_and_set_bit(FLG_T200_RUN, &st->l2.flag);
		} else {
			if (!test_and_clear_bit(FLG_L3_INIT, &st->l2.flag)) {
				if (st->l2.vs != st->l2.va)
					discard_i_queue(st);
				else
					est = 0;
			}
			st->l2.vs = 0;
			st->l2.va = 0;
			st->l2.vr = 0;
			st->l2.sow = 0;
			FsmChangeState(fi, ST_L2_7);
			FsmAddTimer(&st->l2.t203, st->l2.T203, EV_L2_T203, NULL, 4);
			if (est)
				st->l2.l2man(st, DL_ESTABLISH, NULL);
		}
	} else {		/* ST_L2_6 */
		st->l2.l2man(st, DL_RELEASE, NULL);
		FsmChangeState(fi, ST_L2_4);
	}
}

static void
l2_got_dm(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;
	u_char PollFlag;
	int state,rsp;

	state = fi->state;
	rsp = *skb->data & 0x2;
	if (test_bit(FLG_ORIG, &st->l2.flag))
		rsp = !rsp;

	if (!rsp) {
		st->ma.layer(st, MDL_ERROR_IND, (void *) 'L');
		FreeSkb(skb);
		if ((state == ST_L2_7) || (state == ST_L2_8))
			establishlink(fi);
		return;
	}	
	if (skb->len != (l2addrsize(&st->l2) + 1)) {
		st->ma.layer(st, MDL_ERROR_IND, (void *) 'N');
		FreeSkb(skb);
		if ((fi->state == ST_L2_7) || (fi->state == ST_L2_8))
			establishlink(fi);
		return;
	}
	PollFlag = get_PollFlagFree(st, skb);
	if (!PollFlag) {
		establishlink(fi);
		test_and_clear_bit(FLG_L3_INIT, &st->l2.flag);
	} else {
		if (test_and_clear_bit(FLG_T200_RUN, &st->l2.flag))
			FsmDelTimer(&st->l2.t200, 2);
	 	if (fi->state == ST_L2_5 && !test_bit(FLG_L3_INIT, &st->l2.flag))
			discard_i_queue(st);
		st->l2.l2man(st, DL_RELEASE, NULL);
		FsmChangeState(fi, ST_L2_4);
	}
}

inline void
enquiry_cr(struct PStack *st, u_char typ, u_char cr, u_char pf)
{
	struct sk_buff *skb;
	struct Layer2 *l2;
	u_char tmp[MAX_HEADER_LEN];
	int i;

	l2 = &st->l2;
	i = sethdraddr(l2, tmp, cr);
	if (test_bit(FLG_MOD128, &l2->flag)) {
		tmp[i++] = typ;
		tmp[i++] = (l2->vr << 1) | (pf ? 1 : 0);
	} else
		tmp[i++] = (l2->vr << 5) | typ | (pf ? 0x10 : 0);
	if (!(skb = alloc_skb(i, GFP_ATOMIC))) {
		printk(KERN_WARNING "isdl2 can't alloc sbbuff for enquiry_cr\n");
		return;
	}
	memcpy(skb_put(skb, i), tmp, i);
	enqueue_super(st, skb);
}

inline void
enquiry_response(struct PStack *st)
{
	if (test_bit(FLG_OWN_BUSY, &st->l2.flag))
		enquiry_cr(st, RNR, RSP, 1);
	else
		enquiry_cr(st, RR, RSP, 1);
	test_and_clear_bit(FLG_ACK_PEND, &st->l2.flag);
}

inline void
transmit_enquiry(struct PStack *st)
{
	if (test_bit(FLG_OWN_BUSY, &st->l2.flag))
		enquiry_cr(st, RNR, CMD, 1);
	else
		enquiry_cr(st, RR, CMD, 1);
	test_and_clear_bit(FLG_ACK_PEND, &st->l2.flag);
	FsmAddTimer(&st->l2.t200, st->l2.T200, EV_L2_T200, NULL, 12);
	test_and_set_bit(FLG_T200_RUN, &st->l2.flag);
}


static void
nrerrorrecovery(struct FsmInst *fi)
{
	struct PStack *st = fi->userdata;

	st->ma.layer(st, MDL_ERROR_IND, (void *) 'J');
	establishlink(fi);
}

static void
invoke_retransmission(struct PStack *st, int nr)
{
	struct Layer2 *l2 = &st->l2;
	int p1;
	long flags;

	if (l2->vs != nr) {
		save_flags(flags);
		cli();
		while (l2->vs != nr) {
			l2->vs = l2->vs - 1;
			if (l2->vs < 0)
				l2->vs += (test_bit(FLG_MOD128, &l2->flag) ? 128 : 8);
			p1 = l2->vs - l2->va;
			if (p1 < 0)
				p1 += (test_bit(FLG_MOD128, &l2->flag) ? 128 : 8);
			p1 = (p1 + l2->sow) % l2->window;
			skb_queue_head(&l2->i_queue, l2->windowar[p1]);
			l2->windowar[p1] = NULL;
		}
		restore_flags(flags);
		st->l2.l2l1(st, PH_PULL_REQ, NULL);
	}
}

static void
l2_got_st7_super(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;
	int PollFlag, nr, rsp, typ = RR;
	struct Layer2 *l2 = &st->l2;

	rsp = *skb->data & 0x2;
	if (test_bit(FLG_ORIG, &l2->flag))
		rsp = !rsp;

	skb_pull(skb, l2addrsize(l2));
	if (IsRNR(skb->data, test_bit(FLG_MOD128, &l2->flag))) {
		test_and_set_bit(FLG_PEER_BUSY, &l2->flag);
		typ = RNR;
	} else
		test_and_clear_bit(FLG_PEER_BUSY, &l2->flag);
	if (IsREJ(skb->data, test_bit(FLG_MOD128, &l2->flag)))
		typ = REJ;
	if (test_bit(FLG_MOD128, &l2->flag)) {
		if (skb->len == 2) {
			PollFlag = (skb->data[1] & 0x1) == 0x1;
			nr = skb->data[1] >> 1;
		} else {
			st->ma.layer(st, MDL_ERROR_IND, (void *) 'N');
			FreeSkb(skb);
			establishlink(fi);
			return;
		}
	} else {
		if (skb->len == 1) {
			PollFlag = (skb->data[0] & 0x10);
			nr = (skb->data[0] >> 5) & 0x7;
		} else {
			st->ma.layer(st, MDL_ERROR_IND, (void *) 'N');
			FreeSkb(skb);
			establishlink(fi);
			return;
		}
	}
	FreeSkb(skb);

	if ((!rsp) && PollFlag)
		enquiry_response(st);
	if (rsp && PollFlag)
		st->ma.layer(st, MDL_ERROR_IND, (void *) 'A');
	if (legalnr(st, nr)) {
		if (typ == REJ) {
			setva(st, nr);
			invoke_retransmission(st, nr);
			if (test_and_clear_bit(FLG_T200_RUN, &st->l2.flag))
				FsmDelTimer(&st->l2.t200, 9);
			if (FsmAddTimer(&st->l2.t203, st->l2.T203,
					EV_L2_T203, NULL, 6))
				l2m_debug(&st->l2.l2m, "Restart T203 ST7 REJ");
		} else if ((nr == l2->vs) && (typ == RR)) {
			setva(st, nr);
			if (test_and_clear_bit(FLG_T200_RUN, &st->l2.flag))
				FsmDelTimer(&st->l2.t200, 9);
			FsmRestartTimer(&st->l2.t203, st->l2.T203,
					EV_L2_T203, NULL, 7);
		} else if ((l2->va != nr) || (typ == RNR)) {
			setva(st, nr);
			FsmDelTimer(&st->l2.t203, 9);
			FsmRestartTimer(&st->l2.t200, st->l2.T200, EV_L2_T200, NULL, 6);
			test_and_set_bit(FLG_T200_RUN, &st->l2.flag);
		}
		if (skb_queue_len(&st->l2.i_queue) && (typ == RR))
			st->l2.l2l1(st, PH_PULL_REQ, NULL);
	} else
		nrerrorrecovery(fi);

	if ((fi->userint & LC_FLUSH_WAIT) && rsp && !(skb_queue_len(&st->l2.i_queue))) {
		fi->userint &= ~LC_FLUSH_WAIT;
		st->l2.l2man(st, DL_FLUSH, NULL);
	}
}

static void
l2_feed_iqueue(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;

	if (test_bit(FLG_LAPB, &st->l2.flag))
		st->l1.bcs->tx_cnt += skb->len + l2headersize(&st->l2, 0);
	if (!((fi->state == ST_L2_5) && test_bit(FLG_L3_INIT, &st->l2.flag)))
		skb_queue_tail(&st->l2.i_queue, skb);
	if (fi->state == ST_L2_7)
		st->l2.l2l1(st, PH_PULL_REQ, NULL);
}

static void
l2_got_iframe(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;
	struct IsdnCardState *sp = st->l1.hardware;
	struct Layer2 *l2 = &(st->l2);
	int PollFlag, ns, nr, i, hs, rsp;
	char str[64];

	rsp = *skb->data & 0x2;
	if (test_bit(FLG_ORIG, &l2->flag))
		rsp = !rsp;

	if (rsp) {
		st->ma.layer(st, MDL_ERROR_IND, (void *) 'L');
		FreeSkb(skb);
		establishlink(fi);
		return;
	}	
	i = l2addrsize(l2);
	if (test_bit(FLG_MOD128, &l2->flag)) {
		if (skb->len <= (i + 1)) {
			st->ma.layer(st, MDL_ERROR_IND, (void *) 'N');
			FreeSkb(skb);
			establishlink(fi);
			return;
		} else if ((skb->len - i - 1) > l2->maxlen) { 
			st->ma.layer(st, MDL_ERROR_IND, (void *) 'O');
			FreeSkb(skb);
			establishlink(fi);
			return;
		}
		PollFlag = ((skb->data[i + 1] & 0x1) == 0x1);
		ns = skb->data[i] >> 1;
		nr = (skb->data[i + 1] >> 1) & 0x7f;
	} else {
		if (skb->len <= i) {
			st->ma.layer(st, MDL_ERROR_IND, (void *) 'N');
			FreeSkb(skb);
			establishlink(fi);
			return;
		} else if ((skb->len - i) > l2->maxlen) { 
			st->ma.layer(st, MDL_ERROR_IND, (void *) 'O');
			FreeSkb(skb);
			establishlink(fi);
			return;
		}
		PollFlag = (skb->data[i] & 0x10);
		ns = (skb->data[i] >> 1) & 0x7;
		nr = (skb->data[i] >> 5) & 0x7;
	}
	if (test_bit(FLG_OWN_BUSY, &l2->flag)) {
		FreeSkb(skb);
		enquiry_response(st);
	} else if (l2->vr == ns) {
		l2->vr = (l2->vr + 1) % (test_bit(FLG_MOD128, &l2->flag) ? 128 : 8);
		test_and_clear_bit(FLG_REJEXC, &l2->flag);
		if (test_bit(FLG_LAPD, &l2->flag))
			if (sp->dlogflag) {
				hs = l2headersize(l2, 0);
				LogFrame(st->l1.hardware, skb->data, skb->len);
				sprintf(str, "Q.931 frame network->user tei %d", st->l2.tei);
				dlogframe(st->l1.hardware, skb->data + hs,
					  skb->len - hs, str);
			}
		if (PollFlag)
			enquiry_response(st);
		else
			test_and_set_bit(FLG_ACK_PEND, &l2->flag);
		skb_pull(skb, l2headersize(l2, 0));
		st->l2.l2l3(st, DL_DATA, skb);
	} else {
		/* n(s)!=v(r) */
		FreeSkb(skb);
		if (test_and_set_bit(FLG_REJEXC, &l2->flag)) {
			if (PollFlag)
				enquiry_response(st);
		} else {
			enquiry_cr(st, REJ, RSP, PollFlag);
			test_and_clear_bit(FLG_ACK_PEND, &l2->flag);
		}
	}

	if (legalnr(st, nr)) {
		setva(st, nr);
		if (!test_bit(FLG_PEER_BUSY, &st->l2.flag) && (fi->state == ST_L2_7)) {
			if (nr == st->l2.vs) {
				if (test_and_clear_bit(FLG_T200_RUN, &st->l2.flag))
					FsmDelTimer(&st->l2.t200, 10);
				FsmRestartTimer(&st->l2.t203, st->l2.T203,
						EV_L2_T203, NULL, 7);
			} else if (nr != st->l2.va) {
				FsmRestartTimer(&st->l2.t200, st->l2.T200, EV_L2_T200,
						NULL, 8);
				test_and_set_bit(FLG_T200_RUN, &st->l2.flag);
			}
		}
	} else {
		nrerrorrecovery(fi);
		return;
	}

	if (skb_queue_len(&st->l2.i_queue) && (fi->state == ST_L2_7))
		st->l2.l2l1(st, PH_PULL_REQ, NULL);
	if (test_and_clear_bit(FLG_ACK_PEND, &st->l2.flag))
		enquiry_cr(st, RR, RSP, 0);
}

static void
l2_got_tei(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	st->l2.tei = (int) arg;

	if (fi->state == ST_L2_3) {
		establishlink(fi);
		test_and_set_bit(FLG_L3_INIT, &st->l2.flag);
	} else
		FsmChangeState(fi, ST_L2_4);
	if (skb_queue_len(&st->l2.ui_queue))
		l2_send_ui(st);
}

static void
l2_no_tei(struct FsmInst *fi, int event, void *arg)
{
	FsmChangeState(fi, ST_L2_4);
}

static void
l2_st5_tout_200(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	if (test_bit(FLG_LAPD, &st->l2.flag) &&
		test_bit(FLG_DCHAN_BUSY, &st->l2.flag)) {
		FsmAddTimer(&st->l2.t200, st->l2.T200, EV_L2_T200, NULL, 9);
	} else if (st->l2.rc == st->l2.N200) {
		FsmChangeState(fi, ST_L2_4);
		test_and_clear_bit(FLG_T200_RUN, &st->l2.flag);
		discard_i_queue(st);
		st->ma.layer(st, MDL_ERROR_IND, (void *) 'G');
		st->l2.l2man(st, DL_RELEASE, NULL);
	} else {
		st->l2.rc++;
		FsmAddTimer(&st->l2.t200, st->l2.T200, EV_L2_T200, NULL, 9);
		send_uframe(st, (test_bit(FLG_MOD128, &st->l2.flag) ? SABME : SABM)
			    | 0x10, CMD);
	}
}

static void
l2_st6_tout_200(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	if (test_bit(FLG_LAPD, &st->l2.flag) &&
		test_bit(FLG_DCHAN_BUSY, &st->l2.flag)) {
		FsmAddTimer(&st->l2.t200, st->l2.T200, EV_L2_T200, NULL, 9);
	} else if (st->l2.rc == st->l2.N200) {
		FsmChangeState(fi, ST_L2_4);
		st->ma.layer(st, MDL_ERROR_IND, (void *) 'H');
		st->l2.l2man(st, DL_RELEASE, NULL);
	} else {
		st->l2.rc++;
		FsmAddTimer(&st->l2.t200, st->l2.T200, EV_L2_T200,
			    NULL, 9);
		send_uframe(st, DISC | 0x10, CMD);
	}
}

static void
l2_st78_tout_200(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	if (test_bit(FLG_LAPD, &st->l2.flag) &&
		test_bit(FLG_DCHAN_BUSY, &st->l2.flag)) {
		FsmAddTimer(&st->l2.t200, st->l2.T200, EV_L2_T200, NULL, 9);
		return;
	}
	test_and_clear_bit(FLG_T200_RUN, &st->l2.flag);
	if (fi->state == ST_L2_7) {
		st->l2.rc = 0;
		FsmChangeState(fi, ST_L2_8);
	}
	if (st->l2.rc == st->l2.N200) {
		establishlink(fi);
	} else {
		transmit_enquiry(st);
		st->l2.rc++;
	}
}

static void
l2_st7_tout_203(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	if (test_bit(FLG_LAPD, &st->l2.flag) &&
		test_bit(FLG_DCHAN_BUSY, &st->l2.flag)) {
		FsmAddTimer(&st->l2.t203, st->l2.T203, EV_L2_T203, NULL, 9);
		return;
	}
	FsmChangeState(fi, ST_L2_8);
	transmit_enquiry(st);
	st->l2.rc = 0;
}

static void
l2_pull_iqueue(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb, *oskb;
	struct Layer2 *l2 = &st->l2;
	u_char header[MAX_HEADER_LEN];
	int p1, i;

	if (!cansend(st))
		return;

	skb = skb_dequeue(&l2->i_queue);
	if (!skb)
		return;

	p1 = l2->vs - l2->va;
	if (p1 < 0)
		p1 += test_bit(FLG_MOD128, &l2->flag) ? 128 : 8;
	p1 = (p1 + l2->sow) % l2->window;
	if (l2->windowar[p1]) {
		printk(KERN_WARNING "isdnl2 try overwrite ack queue entry %d\n",
		       p1);
		dev_kfree_skb(l2->windowar[p1]);
	}
	l2->windowar[p1] = skb_clone(skb, GFP_ATOMIC);

	i = sethdraddr(&st->l2, header, CMD);

	if (test_bit(FLG_MOD128, &l2->flag)) {
		header[i++] = l2->vs << 1;
		header[i++] = l2->vr << 1;
		l2->vs = (l2->vs + 1) % 128;
	} else {
		header[i++] = (l2->vr << 5) | (l2->vs << 1);
		l2->vs = (l2->vs + 1) % 8;
	}
	p1 = skb->data - skb->head;
	if (p1 >= i)
		memcpy(skb_push(skb, i), header, i);
	else {
		printk(KERN_WARNING
		"isdl2 pull_iqueue skb header(%d/%d) too short\n", i, p1);
		oskb = skb;
		skb = alloc_skb(oskb->len + i, GFP_ATOMIC);
		memcpy(skb_put(skb, i), header, i);
		memcpy(skb_put(skb, oskb->len), oskb->data, oskb->len);
		FreeSkb(oskb);
	}
	st->l2.l2l1(st, PH_PULL_IND, skb);
	test_and_clear_bit(FLG_ACK_PEND, &st->l2.flag);
	if (!test_and_set_bit(FLG_T200_RUN, &st->l2.flag)) {
		FsmDelTimer(&st->l2.t203, 13);
		FsmAddTimer(&st->l2.t200, st->l2.T200, EV_L2_T200, NULL, 11);
	}
	if (skb_queue_len(&l2->i_queue) && cansend(st))
		st->l2.l2l1(st, PH_PULL_REQ, NULL);
}

static void
l2_got_st8_super(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;
	int PollFlag, nr, rsp, rnr = 0;
	struct Layer2 *l2 = &st->l2;

	rsp = *skb->data & 0x2;
	if (test_bit(FLG_ORIG, &l2->flag))
		rsp = !rsp;
	skb_pull(skb, l2addrsize(l2));

	if (IsRNR(skb->data, test_bit(FLG_MOD128, &l2->flag))) {
		test_and_set_bit(FLG_PEER_BUSY, &l2->flag);
		rnr = 1;
	} else
		test_and_clear_bit(FLG_PEER_BUSY, &l2->flag);
	if (test_bit(FLG_MOD128, &l2->flag)) {
		if (skb->len == 2) {
			PollFlag = (skb->data[1] & 0x1) == 0x1;
			nr = skb->data[1] >> 1;
		} else {
			st->ma.layer(st, MDL_ERROR_IND, (void *) 'N');
			FreeSkb(skb);
			establishlink(fi);
			return;
		}
	} else {
		if (skb->len == 1) {
			PollFlag = (skb->data[0] & 0x10);
			nr = (skb->data[0] >> 5) & 0x7;
		} else {
			st->ma.layer(st, MDL_ERROR_IND, (void *) 'N');
			FreeSkb(skb);
			establishlink(fi);
			return;
		}
	}
	FreeSkb(skb);

	if (rsp && PollFlag) {
		if (legalnr(st, nr)) {
			setva(st, nr);
			if (test_and_clear_bit(FLG_T200_RUN, &st->l2.flag))
				FsmDelTimer(&st->l2.t200, 7);
			FsmDelTimer(&l2->t203, 8);
			if (rnr) {
				FsmRestartTimer(&l2->t200, l2->T200,
						EV_L2_T200, NULL, 14);
				test_and_set_bit(FLG_T200_RUN, &st->l2.flag);
			} else
				FsmAddTimer(&l2->t203, l2->T203,
					    EV_L2_T203, NULL, 5);
			invoke_retransmission(st, nr);
			FsmChangeState(fi, ST_L2_7);
			if (skb_queue_len(&l2->i_queue) && cansend(st))
				st->l2.l2l1(st, PH_PULL_REQ, NULL);
			else if (fi->userint & LC_FLUSH_WAIT) {
				fi->userint &= ~LC_FLUSH_WAIT;
				st->l2.l2man(st, DL_FLUSH, NULL);
			}
		}
	} else {
		if (!rsp && PollFlag)
			enquiry_response(st);
		if (legalnr(st, nr)) {
			setva(st, nr);
		}
	}
}

static void
l2_got_FRMR(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;
	char tmp[64];

	skb_pull(skb, l2addrsize(&st->l2) + 1);
	if (test_bit(FLG_MOD128, &st->l2.flag)) {
		if (skb->len < 5)
			st->ma.layer(st, MDL_ERROR_IND, (void *) 'N');
		else {
			sprintf(tmp, "FRMR information %2x %2x %2x %2x %2x",
				skb->data[0], skb->data[1], skb->data[2],
				skb->data[3], skb->data[4]);
			l2m_debug(&st->l2.l2m, tmp);
		}
	} else {
		if (skb->len < 3)
			st->ma.layer(st, MDL_ERROR_IND, (void *) 'N');
		else {
			sprintf(tmp, "FRMR information %2x %2x %2x",
				skb->data[0], skb->data[1], skb->data[2]);
			l2m_debug(&st->l2.l2m, tmp);
		}
	}
	if (!(skb->data[0] & 1) || ((skb->data[0] & 3) == 1) ||		/* I or S */
	    (IsUA(skb->data, 0) && (fi->state == ST_L2_7))) {
		st->ma.layer(st, MDL_ERROR_IND, (void *) 'K');
		establishlink(fi);
		test_and_clear_bit(FLG_L3_INIT, &st->l2.flag);
	}
	FreeSkb(skb);
}

static void
l2_tei_remove(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	discard_i_queue(st);
	discard_ui_queue(st);
	st->l2.tei = -1;
	if (test_and_clear_bit(FLG_T200_RUN, &st->l2.flag))
		FsmDelTimer(&st->l2.t200, 18);
	FsmDelTimer(&st->l2.t203, 19);
	if (fi->state != ST_L2_4)
		st->l2.l2man(st, DL_RELEASE, NULL);
	FsmChangeState(fi, ST_L2_1);
}

static void
l2_persistant_da(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	
	discard_i_queue(st);
	discard_ui_queue(st);
	if (test_and_clear_bit(FLG_T200_RUN, &st->l2.flag))
		FsmDelTimer(&st->l2.t200, 18);
	FsmDelTimer(&st->l2.t203, 19);
	test_and_clear_bit(FLG_PEND_REL, &st->l2.flag);
	clear_exception(&st->l2);
	switch (fi->state) {
		case ST_L2_3:
			st->l2.l2man(st, DL_RELEASE, NULL);
		case ST_L2_2:
			FsmChangeState(fi, ST_L2_1);
			break;
		case ST_L2_5:
		case ST_L2_6:
		case ST_L2_7:
		case ST_L2_8:
			st->l2.l2man(st, DL_RELEASE, NULL);
			FsmChangeState(fi, ST_L2_4);
			break;
	}
}

static struct FsmNode L2FnList[] HISAX_INITDATA =
{
	{ST_L2_1, EV_L2_MDL_NOTEIPROC, l2_no_tei},
	{ST_L2_1, EV_L2_DL_ESTABLISH, l2_dl_establish},
	{ST_L2_2, EV_L2_DL_ESTABLISH, l2_dl_establish},
	{ST_L2_4, EV_L2_DL_ESTABLISH, l2_establish},
	{ST_L2_5, EV_L2_DL_ESTABLISH, l2_establish},
	{ST_L2_7, EV_L2_DL_ESTABLISH, l2_establish},
	{ST_L2_8, EV_L2_DL_ESTABLISH, l2_establish},
	{ST_L2_4, EV_L2_DL_RELEASE, l2_dl_release},
	{ST_L2_5, EV_L2_DL_RELEASE, l2_dl_release},
	{ST_L2_7, EV_L2_DL_RELEASE, l2_dl_release},
	{ST_L2_8, EV_L2_DL_RELEASE, l2_dl_release},
	{ST_L2_5, EV_L2_DL_DATA, l2_feed_iqueue},
	{ST_L2_7, EV_L2_DL_DATA, l2_feed_iqueue},
	{ST_L2_8, EV_L2_DL_DATA, l2_feed_iqueue},
	{ST_L2_1, EV_L2_DL_UNIT_DATA, l2_put_ui},
	{ST_L2_2, EV_L2_DL_UNIT_DATA, l2_put_ui},
	{ST_L2_3, EV_L2_DL_UNIT_DATA, l2_put_ui},
	{ST_L2_4, EV_L2_DL_UNIT_DATA, l2_put_ui},
	{ST_L2_5, EV_L2_DL_UNIT_DATA, l2_put_ui},
	{ST_L2_6, EV_L2_DL_UNIT_DATA, l2_put_ui},
	{ST_L2_7, EV_L2_DL_UNIT_DATA, l2_put_ui},
	{ST_L2_8, EV_L2_DL_UNIT_DATA, l2_put_ui},
	{ST_L2_1, EV_L2_MDL_ASSIGN, l2_got_tei},
	{ST_L2_2, EV_L2_MDL_ASSIGN, l2_got_tei},
	{ST_L2_3, EV_L2_MDL_ASSIGN, l2_got_tei},
	{ST_L2_2, EV_L2_MDL_ERROR, l2_tei_remove},
	{ST_L2_3, EV_L2_MDL_ERROR, l2_tei_remove},
	{ST_L2_4, EV_L2_MDL_REMOVE, l2_tei_remove},
	{ST_L2_5, EV_L2_MDL_REMOVE, l2_tei_remove},
	{ST_L2_6, EV_L2_MDL_REMOVE, l2_tei_remove},
	{ST_L2_7, EV_L2_MDL_REMOVE, l2_tei_remove},
	{ST_L2_8, EV_L2_MDL_REMOVE, l2_tei_remove},
	{ST_L2_4, EV_L2_SABMX, l2_got_SABMX},
	{ST_L2_5, EV_L2_SABMX, l2_got_SABMX},
	{ST_L2_6, EV_L2_SABMX, l2_got_SABMX},
	{ST_L2_7, EV_L2_SABMX, l2_got_SABMX},
	{ST_L2_8, EV_L2_SABMX, l2_got_SABMX},
	{ST_L2_4, EV_L2_DISC, l2_got_disconn},
	{ST_L2_5, EV_L2_DISC, l2_got_disconn},
	{ST_L2_6, EV_L2_DISC, l2_got_disconn},
	{ST_L2_7, EV_L2_DISC, l2_got_disconn},
	{ST_L2_8, EV_L2_DISC, l2_got_disconn},
	{ST_L2_4, EV_L2_UA, l2_mdl_error},
	{ST_L2_5, EV_L2_UA, l2_got_ua},
	{ST_L2_6, EV_L2_UA, l2_got_ua},
	{ST_L2_7, EV_L2_UA, l2_mdl_error},
	{ST_L2_8, EV_L2_UA, l2_mdl_error},
	{ST_L2_4, EV_L2_DM, l2_got_dm},
	{ST_L2_5, EV_L2_DM, l2_got_dm},
	{ST_L2_6, EV_L2_DM, l2_got_dm},
	{ST_L2_7, EV_L2_DM, l2_mdl_error},
	{ST_L2_8, EV_L2_DM, l2_mdl_error},
	{ST_L2_1, EV_L2_UI, l2_got_ui},
	{ST_L2_2, EV_L2_UI, l2_got_ui},
	{ST_L2_3, EV_L2_UI, l2_got_ui},
	{ST_L2_4, EV_L2_UI, l2_got_ui},
	{ST_L2_5, EV_L2_UI, l2_got_ui},
	{ST_L2_6, EV_L2_UI, l2_got_ui},
	{ST_L2_7, EV_L2_UI, l2_got_ui},
	{ST_L2_8, EV_L2_UI, l2_got_ui},
	{ST_L2_7, EV_L2_FRMR, l2_got_FRMR},
	{ST_L2_8, EV_L2_FRMR, l2_got_FRMR},
	{ST_L2_7, EV_L2_SUPER, l2_got_st7_super},
	{ST_L2_8, EV_L2_SUPER, l2_got_st8_super},
	{ST_L2_7, EV_L2_I, l2_got_iframe},
	{ST_L2_8, EV_L2_I, l2_got_iframe},
	{ST_L2_5, EV_L2_T200, l2_st5_tout_200},
	{ST_L2_6, EV_L2_T200, l2_st6_tout_200},
	{ST_L2_7, EV_L2_T200, l2_st78_tout_200},
	{ST_L2_8, EV_L2_T200, l2_st78_tout_200},
	{ST_L2_7, EV_L2_T203, l2_st7_tout_203},
	{ST_L2_7, EV_L2_ACK_PULL, l2_pull_iqueue},
	{ST_L2_2, EV_L1_DEACTIVATE, l2_persistant_da},
	{ST_L2_3, EV_L1_DEACTIVATE, l2_persistant_da},
	{ST_L2_4, EV_L1_DEACTIVATE, l2_persistant_da},
	{ST_L2_5, EV_L1_DEACTIVATE, l2_persistant_da},
	{ST_L2_6, EV_L1_DEACTIVATE, l2_persistant_da},
	{ST_L2_7, EV_L1_DEACTIVATE, l2_persistant_da},
	{ST_L2_8, EV_L1_DEACTIVATE, l2_persistant_da},
};

#define L2_FN_COUNT (sizeof(L2FnList)/sizeof(struct FsmNode))

static void
isdnl2_l1l2(struct PStack *st, int pr, void *arg)
{
	struct sk_buff *skb = arg;
	u_char *datap;
	int ret = 1, len;

	switch (pr) {
		case (PH_DATA_IND):
			datap = skb->data;
			len = l2addrsize(&st->l2);
			if (skb->len > len)
				datap += len;
			else {
				st->ma.layer(st, MDL_ERROR_IND, (void *) 'N');
				FreeSkb(skb);
				return;
			}
			if (!(*datap & 1))	/* I-Frame */
				ret = FsmEvent(&st->l2.l2m, EV_L2_I, skb);
			else if ((*datap & 3) == 1)	/* S-Frame */
				ret = FsmEvent(&st->l2.l2m, EV_L2_SUPER, skb);
			else if (IsUI(datap, test_bit(FLG_MOD128, &st->l2.flag)))
				ret = FsmEvent(&st->l2.l2m, EV_L2_UI, skb);
			else if (IsSABMX(datap, test_bit(FLG_MOD128, &st->l2.flag)))
				ret = FsmEvent(&st->l2.l2m, EV_L2_SABMX, skb);
			else if (IsUA(datap, test_bit(FLG_MOD128, &st->l2.flag)))
				ret = FsmEvent(&st->l2.l2m, EV_L2_UA, skb);
			else if (IsDISC(datap, test_bit(FLG_MOD128, &st->l2.flag)))
				ret = FsmEvent(&st->l2.l2m, EV_L2_DISC, skb);
			else if (IsDM(datap, test_bit(FLG_MOD128, &st->l2.flag)))
				ret = FsmEvent(&st->l2.l2m, EV_L2_DM, skb);
			else if (IsFRMR(datap, test_bit(FLG_MOD128, &st->l2.flag)))
				ret = FsmEvent(&st->l2.l2m, EV_L2_FRMR, skb);
			else {
				ret = 0;
				st->ma.layer(st, MDL_ERROR_IND, (void *) 'L');
				FreeSkb(skb);
			}
			if (ret) {
				FreeSkb(skb);
			}
			break;
		case (PH_PULL_CNF):
			FsmEvent(&st->l2.l2m, EV_L2_ACK_PULL, arg);
			break;
		case (PH_PAUSE_IND):
			test_and_set_bit(FLG_DCHAN_BUSY, &st->l2.flag);
			break;
		case (PH_PAUSE_CNF):
			test_and_clear_bit(FLG_DCHAN_BUSY, &st->l2.flag);
			break;
	}
}

static void
isdnl2_l3l2(struct PStack *st, int pr, void *arg)
{
	switch (pr) {
		case (DL_DATA):
			if (FsmEvent(&st->l2.l2m, EV_L2_DL_DATA, arg)) {
				dev_kfree_skb((struct sk_buff *) arg);
			}
			break;
		case (DL_UNIT_DATA):
			if (FsmEvent(&st->l2.l2m, EV_L2_DL_UNIT_DATA, arg)) {
				dev_kfree_skb((struct sk_buff *) arg);
			}
			break;
	}
}

static void
isdnl2_manl2(struct PStack *st, int pr, void *arg)
{
	switch (pr) {
		case (DL_ESTABLISH):
			FsmEvent(&st->l2.l2m, EV_L2_DL_ESTABLISH, arg);
			break;
		case (DL_RELEASE):
			FsmEvent(&st->l2.l2m, EV_L2_DL_RELEASE, arg);
			break;
		case (MDL_NOTEIPROC):
			FsmEvent(&st->l2.l2m, EV_L2_MDL_NOTEIPROC, NULL);
			break;
		case (DL_FLUSH):
			(&st->l2.l2m)->userint |= LC_FLUSH_WAIT;
			break;
		case (PH_DEACTIVATE_IND):
			FsmEvent(&st->l2.l2m, EV_L1_DEACTIVATE, arg);
			break;
		case (MDL_ASSIGN_REQ):
			FsmEvent(&st->l2.l2m, EV_L2_MDL_ASSIGN, arg);
			break;
		case (MDL_REMOVE_REQ):
			FsmEvent(&st->l2.l2m, EV_L2_MDL_REMOVE, arg);
			break;
		case (MDL_ERROR_REQ):
			FsmEvent(&st->l2.l2m, EV_L2_MDL_ERROR, arg);
			break;
	}
}

void
releasestack_isdnl2(struct PStack *st)
{
	FsmDelTimer(&st->l2.t200, 15);
	FsmDelTimer(&st->l2.t203, 16);
	discard_i_queue(st);
	discard_ui_queue(st);
	ReleaseWin(&st->l2);
}

static void
l2m_debug(struct FsmInst *fi, char *s)
{
	struct PStack *st = fi->userdata;
	char tm[32], str[256];

	jiftime(tm, jiffies);
	sprintf(str, "%s %s %s\n", tm, st->l2.debug_id, s);
	HiSax_putstatus(st->l1.hardware, str);
}

void
setstack_isdnl2(struct PStack *st, char *debug_id)
{
	st->l1.l1l2 = isdnl2_l1l2;
	st->l3.l3l2 = isdnl2_l3l2;
	st->ma.manl2 = isdnl2_manl2;

	skb_queue_head_init(&st->l2.i_queue);
	skb_queue_head_init(&st->l2.ui_queue);
	InitWin(&st->l2);
	st->l2.debug = 0;

	st->l2.l2m.fsm = &l2fsm;
	st->l2.l2m.state = ST_L2_1;
	st->l2.l2m.debug = 0;
	st->l2.l2m.userdata = st;
	st->l2.l2m.userint = 0;
	st->l2.l2m.printdebug = l2m_debug;
	strcpy(st->l2.debug_id, debug_id);

	FsmInitTimer(&st->l2.l2m, &st->l2.t200);
	FsmInitTimer(&st->l2.l2m, &st->l2.t203);
}

void
setstack_transl2(struct PStack *st)
{
}

void
releasestack_transl2(struct PStack *st)
{
}

HISAX_INITFUNC(void
Isdnl2New(void))
{
	l2fsm.state_count = L2_STATE_COUNT;
	l2fsm.event_count = L2_EVENT_COUNT;
	l2fsm.strEvent = strL2Event;
	l2fsm.strState = strL2State;
	FsmNew(&l2fsm, L2FnList, L2_FN_COUNT);
}

void
Isdnl2Free(void)
{
	FsmFree(&l2fsm);
}
