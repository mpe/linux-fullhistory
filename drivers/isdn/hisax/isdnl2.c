/* $Id: isdnl2.c,v 1.10 1997/05/06 09:38:13 keil Exp $

 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *              based on the teles driver from Jan den Ouden
 *
 * Thanks to    Jan den Ouden
 *              Fritz Elfert
 *
 * $Log: isdnl2.c,v $
 * Revision 1.10  1997/05/06 09:38:13  keil
 * Bugfixes: - clear ack queue entries after resend
 *           - acknowlege each frame to linklevel
 *           - UA for SABM is Response, not command
 *           - only RR was send as supervisor frame (X.75 hangs after a
 *             sequence error)
 *
 * Revision 1.9  1997/04/07 23:02:11  keil
 * missing braces
 *
 * Revision 1.8  1997/04/06 22:59:59  keil
 * Using SKB's; changing function names; some minor changes
 *
 * Revision 1.7  1997/02/09 00:25:44  keil
 * new interface handling, one interface per card
 *
 * Revision 1.6  1997/01/21 22:23:42  keil
 * D-channel log changed
 *
 * Revision 1.5  1997/01/04 13:47:06  keil
 * handling of MDL_REMOVE added (Thanks to Sim Yskes)
 *
 * Revision 1.4  1996/12/08 19:51:51  keil
 * many fixes from Pekka Sarnila
 *
 * Revision 1.3  1996/11/05 19:39:12  keil
 * X.75 bugfixes Thank to Martin Maurer
 *
 * Revision 1.2  1996/10/30 10:20:58  keil
 * X.75 answer of SABMX fixed to response address (AVM X.75 problem)
 *
 * Revision 1.1  1996/10/13 20:04:54  keil
 * Initial revision
 *
 *
 *
 */
#define __NO_VERSION__
#include "hisax.h"
#include "isdnl2.h"

const char *l2_revision = "$Revision: 1.10 $";

static void l2m_debug(struct FsmInst *fi, char *s);

struct Fsm l2fsm =
{NULL, 0, 0, NULL, NULL};

enum {
	ST_L2_1,
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
	EV_L2_UA,
	EV_L2_DISC,
	EV_L2_I,
	EV_L2_RR,
	EV_L2_REJ,
	EV_L2_FRMR,
	EV_L2_DL_DATA,
	EV_L2_DL_ESTABLISH,
	EV_L2_MDL_ASSIGN,
	EV_L2_MDL_REMOVE,
	EV_L2_DL_UNIT_DATA,
	EV_L2_DL_RELEASE,
	EV_L2_MDL_NOTEIPROC,
	EV_L2_T200,
	EV_L2_ACK_PULL,
	EV_L2_T203,
	EV_L2_RNR,
};

#define L2_EVENT_COUNT (EV_L2_RNR+1)

static char *strL2Event[] =
{
	"EV_L2_UI",
	"EV_L2_SABMX",
	"EV_L2_UA",
	"EV_L2_DISC",
	"EV_L2_I",
	"EV_L2_RR",
	"EV_L2_REJ",
	"EV_L2_FRMR",
	"EV_L2_DL_DATA",
	"EV_L2_DL_ESTABLISH",
	"EV_L2_MDL_ASSIGN",
	"EV_L2_MDL_REMOVE",
	"EV_L2_DL_UNIT_DATA",
	"EV_L2_DL_RELEASE",
	"EV_L2_MDL_NOTEIPROC",
	"EV_L2_T200",
	"EV_L2_ACK_PULL",
	"EV_L2_T203",
	"EV_L2_RNR",
};

int errcount = 0;

static int l2addrsize(struct Layer2 *tsp);

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
			SET_SKB_FREE(l2->windowar[i]);
			dev_kfree_skb(l2->windowar[i], FREE_WRITE);
			l2->windowar[i] = NULL;
		}
	}
	if (cnt)
		printk(KERN_WARNING "isdl2 freed %d skbuffs in release\n", cnt);
}

static int
cansend(struct PStack *st)
{
	int p1;

	p1 = (st->l2.va + st->l2.window) % (st->l2.extended ? 128 : 8);
	return (st->l2.vs != p1);
}

static void
discard_i_queue(struct PStack *st)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&st->l2.i_queue))) {
		SET_SKB_FREE(skb);
		dev_kfree_skb(skb, FREE_READ);
	}
}

int
l2headersize(struct Layer2 *tsp, int ui)
{
	return ((tsp->extended && (!ui) ? 2 : 1) + (tsp->laptype == LAPD ? 2 : 1));
}

int
l2addrsize(struct Layer2 *tsp)
{
	return (tsp->laptype == LAPD ? 2 : 1);
}

static int
sethdraddr(struct Layer2 *tsp,
	   u_char * header, int rsp)
{
	u_char *ptr = header;
	int crbit;

	if (tsp->laptype == LAPD) {
		crbit = rsp;
		if (!tsp->orig)
			crbit = !crbit;
		*ptr++ = (tsp->sap << 2) | (crbit ? 2 : 0);
		*ptr++ = (tsp->tei << 1) | 1;
		return (2);
	} else {
		crbit = rsp;
		if (tsp->orig)
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
	st->l2.l2l1(st, PH_DATA, skb);
}

static void
enqueue_super(struct PStack *st,
	      struct sk_buff *skb)
{
	st->l2.l2l1(st, PH_DATA, skb);
}

static int
legalnr(struct PStack *st, int nr)
{
	struct Layer2 *l2 = &st->l2;
	int lnr, lvs;

	lvs = (l2->vs >= l2->va) ? l2->vs : (l2->vs + l2->extended ? 128 : 8);
	lnr = (nr >= l2->va) ? nr : (nr + l2->extended ? 128 : 8);
	return (lnr <= lvs);
}

static void
setva(struct PStack *st, int nr)
{
	struct Layer2 *l2 = &st->l2;

	if (l2->va != nr) {
		while (l2->va != nr) {
			l2->va = (l2->va + 1) % (l2->extended ? 128 : 8);
			SET_SKB_FREE(l2->windowar[l2->sow]);
			dev_kfree_skb(l2->windowar[l2->sow], FREE_WRITE);
			l2->windowar[l2->sow] = NULL;
			l2->sow = (l2->sow + 1) % l2->window;
			if (st->l4.l2writewakeup)
				st->l4.l2writewakeup(st);
		}
	}
}

static void
l2s1(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	st->l2.l2tei(st, MDL_ASSIGN, (void *) st->l2.ces);
	FsmChangeState(fi, ST_L2_3);
}

static void
l2_send_ui(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;
	u_char header[MAX_HEADER_LEN];
	int i;

	i = sethdraddr(&(st->l2), header, CMD);
	header[i++] = UI;
	memcpy(skb_push(skb, i), header, i);
	enqueue_ui(st, skb);
}

static void
l2_receive_ui(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;

	skb_pull(skb, l2headersize(&st->l2, 1));
	st->l2.l2l3(st, DL_UNIT_DATA, skb);
}

inline void
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

static void
establishlink(struct FsmInst *fi)
{
	struct PStack *st = fi->userdata;
	u_char cmd;

	FsmChangeState(fi, ST_L2_5);
	st->l2.rc = 0;

	if (FsmAddTimer(&st->l2.t200_timer, st->l2.t200, EV_L2_T200, NULL, 1))
		if (st->l2.l2m.debug)
			l2m_debug(&st->l2.l2m, "FAT 1");


	cmd = (st->l2.extended ? SABME : SABM) | 0x10; 
	send_uframe(st, cmd, CMD);
}

static void
l2_establish(struct FsmInst *fi, int event, void *arg)
{
	establishlink(fi);
}

static void
l2_send_disconn(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct Channel *chanp = st->l4.userdata;

	FsmChangeState(fi, ST_L2_6);

	FsmDelTimer(&st->l2.t203_timer, 1);
	if (st->l2.t200_running) {
		FsmDelTimer(&st->l2.t200_timer, 2);
		st->l2.t200_running = 0;
	}
	st->l2.rc = 0;
	if (FsmAddTimer(&st->l2.t200_timer, st->l2.t200, EV_L2_T200, NULL, 2))
		if (st->l2.l2m.debug)
			l2m_debug(&st->l2.l2m, "FAT 2");


	if (!((chanp->impair == 2) && (st->l2.laptype == LAPB)))
		send_uframe(st, DISC | 0x10, CMD);

	discard_i_queue(st);
}

static void
l2_got_SABMX(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;
	int est = 1, state;
	u_char PollFlag;

	state = fi->state;

	skb_pull(skb, l2addrsize(&(st->l2)));
	PollFlag = *skb->data & 0x10;
	SET_SKB_FREE(skb);
	dev_kfree_skb(skb, FREE_READ);

	if (ST_L2_4 != state)
		if (st->l2.vs != st->l2.va) {
			discard_i_queue(st);
			est = 1;
		} else
			est = 0;

	st->l2.vs = 0;
	st->l2.va = 0;
	st->l2.vr = 0;
	st->l2.sow = 0;
	if (ST_L2_7 != state)
		FsmChangeState(fi, ST_L2_7);

	send_uframe(st, UA | PollFlag, RSP);

	if (st->l2.t200_running) {
		FsmDelTimer(&st->l2.t200_timer, 15);
		st->l2.t200_running = 0;
	}
	if (FsmAddTimer(&st->l2.t203_timer, st->l2.t203, EV_L2_T203, NULL, 3))
		if (st->l2.l2m.debug)
			l2m_debug(&st->l2.l2m, "FAT 3");

	if (est)
		st->l2.l2man(st, DL_ESTABLISH, NULL);

	if (ST_L2_8 == state)
		if (skb_queue_len(&st->l2.i_queue) && cansend(st))
			st->l2.l2l1(st, PH_REQUEST_PULL, NULL);
}

static void
l2_got_disconn(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;
	struct Channel *chanp = st->l4.userdata;
	u_char PollFlag;

	skb_pull(skb, l2addrsize(&(st->l2)));
	PollFlag = *skb->data & 0x10;
	SET_SKB_FREE(skb);
	dev_kfree_skb(skb, FREE_READ);

	FsmChangeState(fi, ST_L2_4);

	FsmDelTimer(&st->l2.t203_timer, 3);
	if (st->l2.t200_running) {
		FsmDelTimer(&st->l2.t200_timer, 4);
		st->l2.t200_running = 0;
	}
	if (!((chanp->impair == 1) && (st->l2.laptype == LAPB)))
		send_uframe(st, UA | PollFlag, RSP);

	st->l2.l2man(st, DL_RELEASE, NULL);
}

static void
l2_got_st4_disc(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;
	struct Channel *chanp = st->l4.userdata;
	u_char PollFlag;

	skb_pull(skb, l2addrsize(&(st->l2)));
	PollFlag = *skb->data & 0x10;
	SET_SKB_FREE(skb);
	dev_kfree_skb(skb, FREE_READ);

	if (!((chanp->impair == 1) && (st->l2.laptype == LAPB)))
		send_uframe(st, DM | (PollFlag ? 0x10 : 0x0), RSP);

}

static void
l2_got_ua_establish(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;
	u_char f;

	skb_pull(skb, l2addrsize(&(st->l2)));
	f = *skb->data & 0x10;
	SET_SKB_FREE(skb);
	dev_kfree_skb(skb, FREE_READ);
	if (f) {
		st->l2.vs = 0;
		st->l2.va = 0;
		st->l2.vr = 0;
		st->l2.sow = 0;
		FsmChangeState(fi, ST_L2_7);

		FsmDelTimer(&st->l2.t200_timer, 5);
		if (FsmAddTimer(&st->l2.t203_timer, st->l2.t203, EV_L2_T203, NULL, 4))
			if (st->l2.l2m.debug)
				l2m_debug(&st->l2.l2m, "FAT 4");

		st->l2.l2man(st, DL_ESTABLISH, NULL);
	}
}

static void
l2_got_ua_disconn(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;
	u_char f;

	skb_pull(skb, l2addrsize(&(st->l2)));
	f = *skb->data & 0x10;
	SET_SKB_FREE(skb);
	dev_kfree_skb(skb, FREE_READ);
	if (f) {
		FsmDelTimer(&st->l2.t200_timer, 6);
		FsmChangeState(fi, ST_L2_4);
		st->l2.l2man(st, DL_RELEASE, NULL);
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
	if (l2->extended) {
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
enquiry_response(struct PStack *st, u_char typ, u_char final)
{
	enquiry_cr(st, typ, RSP, final);
}

inline void
enquiry_command(struct PStack *st, u_char typ, u_char poll)
{
	enquiry_cr(st, typ, CMD, poll);
}

static void
nrerrorrecovery(struct FsmInst *fi)
{
	/* should log error here */
	establishlink(fi);
}

static void
l2_got_st7_RR(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct Channel *chanp = st->l4.userdata;
	struct sk_buff *skb = arg;
	int PollFlag, seq, rsp;
	struct Layer2 *l2;

	l2 = &st->l2;
	if (l2->laptype == LAPD)
		rsp = *skb->data & 0x2;
	else
		rsp = *skb->data == 0x3;
	if (l2->orig)
		rsp = !rsp;

	skb_pull(skb, l2addrsize(l2));
	if (l2->extended) {
		PollFlag = (skb->data[1] & 0x1) == 0x1;
		seq = skb->data[1] >> 1;
	} else {
		PollFlag = (skb->data[0] & 0x10);
		seq = (skb->data[0] >> 5) & 0x7;
	}
	SET_SKB_FREE(skb);
	dev_kfree_skb(skb, FREE_READ);

	if (!((chanp->impair == 4) && (st->l2.laptype == LAPB)))
		if ((!rsp) && PollFlag)
			enquiry_response(st, RR, PollFlag);

	if (legalnr(st, seq)) {
		if (seq == l2->vs) {
			setva(st, seq);
			FsmDelTimer(&l2->t200_timer, 7);
			l2->t200_running = 0;
			FsmDelTimer(&l2->t203_timer, 8);
			if (FsmAddTimer(&l2->t203_timer, l2->t203, EV_L2_T203, NULL, 5))
				if (l2->l2m.debug)
					l2m_debug(&st->l2.l2m, "FAT 5");

			if (skb_queue_len(&st->l2.i_queue))
				st->l2.l2l1(st, PH_REQUEST_PULL, NULL);
		} else if (l2->va != seq) {
			setva(st, seq);
			FsmDelTimer(&st->l2.t200_timer, 9);
			if (FsmAddTimer(&st->l2.t200_timer, st->l2.t200, EV_L2_T200, NULL, 6))
				if (st->l2.l2m.debug)
					l2m_debug(&st->l2.l2m, "FAT 6");
			if (skb_queue_len(&st->l2.i_queue))
				st->l2.l2l1(st, PH_REQUEST_PULL, NULL);
		}
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

	skb_queue_tail(&st->l2.i_queue, skb);
	st->l2.l2l1(st, PH_REQUEST_PULL, NULL);
}

static int
icommandreceived(struct FsmInst *fi, int event, void *arg, int *nr)
{
	struct PStack *st = fi->userdata;
	struct Channel *chanp = st->l4.userdata;
	struct sk_buff *skb = arg;
	struct IsdnCardState *sp = st->l1.hardware;
	struct Layer2 *l2 = &(st->l2);
	int i, p, seq, wasok;
	char str[64];

	i = l2addrsize(l2);
	if (l2->extended) {
		p = (skb->data[i + 1] & 0x1) == 0x1;
		seq = skb->data[i] >> 1;
		*nr = (skb->data[i + 1] >> 1) & 0x7f;
	} else {
		p = (skb->data[i] & 0x10);
		seq = (skb->data[i] >> 1) & 0x7;
		*nr = (skb->data[i] >> 5) & 0x7;
	}

	if (l2->vr == seq) {
		wasok = !0;

		l2->vr = (l2->vr + 1) % (l2->extended ? 128 : 8);
		l2->rejexp = 0;

		if (st->l2.laptype == LAPD)
			if (sp->dlogflag) {
				LogFrame(st->l1.hardware, skb->data, skb->len);
				sprintf(str, "Q.931 frame network->user tei %d", st->l2.tei);
				dlogframe(st->l1.hardware, skb->data + l2->ihsize,
					  skb->len - l2->ihsize, str);
			}
		if (!((chanp->impair == 3) && (st->l2.laptype == LAPB)))
			if (p || (!skb_queue_len(&st->l2.i_queue)))
				enquiry_response(st, RR, p);
		skb_pull(skb, l2headersize(l2, 0));
	} else {
		/* n(s)!=v(r) */
		wasok = 0;
		SET_SKB_FREE(skb);
		dev_kfree_skb(skb, FREE_READ);
		if (st->l2.rejexp) {
			if (p)
				if (!((chanp->impair == 3) && (st->l2.laptype == LAPB)))
					enquiry_response(st, RR, p);
		} else {
			st->l2.rejexp = !0;
			enquiry_command(st, REJ, 1);
		}
	}
	return wasok;
}

static void
l2_got_st7_data(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;
	int nr, wasok;

	wasok = icommandreceived(fi, event, arg, &nr);

	if (legalnr(st, nr)) {
		if (nr == st->l2.vs) {
			setva(st, nr);
			FsmDelTimer(&st->l2.t200_timer, 10);
			st->l2.t200_running = 0;
			FsmDelTimer(&st->l2.t203_timer, 11);
			if (FsmAddTimer(&st->l2.t203_timer, st->l2.t203, EV_L2_T203, NULL, 7))
				if (st->l2.l2m.debug)
					l2m_debug(&st->l2.l2m, "FAT 5");

			if (skb_queue_len(&st->l2.i_queue))
				st->l2.l2l1(st, PH_REQUEST_PULL, NULL);
		} else if (nr != st->l2.va) {
			setva(st, nr);
			FsmDelTimer(&st->l2.t200_timer, 12);
			if (FsmAddTimer(&st->l2.t200_timer, st->l2.t200, EV_L2_T200, NULL, 8))
				if (st->l2.l2m.debug)
					l2m_debug(&st->l2.l2m, "FAT 6");

			if (skb_queue_len(&st->l2.i_queue))
				st->l2.l2l1(st, PH_REQUEST_PULL, NULL);
		}
	} else
		nrerrorrecovery(fi);

	if (wasok)
		st->l2.l2l3(st, DL_DATA, skb);
}

static void
l2_got_st8_data(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;
	int nr, wasok;

	wasok = icommandreceived(fi, event, arg, &nr);

	if (legalnr(st, nr)) {
		setva(st, nr);
		if (skb_queue_len(&st->l2.i_queue))
			st->l2.l2l1(st, PH_REQUEST_PULL, NULL);
	} else
		nrerrorrecovery(fi);

	if (wasok)
		st->l2.l2l3(st, DL_DATA, skb);
}

static void
l2_got_tei(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	st->l2.tei = (int) arg;
	establishlink(fi);
}

static void
invoke_retransmission(struct PStack *st, int nr)
{
	struct Layer2 *l2 = &st->l2;
	int p1;

	if (l2->vs != nr) {
		while (l2->vs != nr) {

			l2->vs = l2->vs - 1;
			if (l2->vs < 0)
				l2->vs += l2->extended ? 128 : 8;

			p1 = l2->vs - l2->va;
			if (p1 < 0)
				p1 += l2->extended ? 128 : 8;
			p1 = (p1 + l2->sow) % l2->window;

			skb_queue_head(&l2->i_queue, l2->windowar[p1]);
			l2->windowar[p1] = NULL;
		}
		st->l2.l2l1(st, PH_REQUEST_PULL, NULL);
	}
}

static void
l2_got_st7_rej(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;
	int PollFlag, seq, rsp;
	struct Layer2 *l2;

	l2 = &st->l2;
	if (l2->laptype == LAPD)
		rsp = *skb->data & 0x2;
	else
		rsp = *skb->data == 0x3;
	if (l2->orig)
		rsp = !rsp;

	skb_pull(skb, l2addrsize(l2));
	if (l2->extended) {
		PollFlag = (skb->data[1] & 0x1) == 0x1;
		seq = skb->data[1] >> 1;
	} else {
		PollFlag = (skb->data[0] & 0x10);
		seq = (skb->data[0] >> 5) & 0x7;
	}
	SET_SKB_FREE(skb);
	dev_kfree_skb(skb, FREE_READ);

	if ((!rsp) && PollFlag)
		enquiry_response(st, RR, PollFlag);

	if (!legalnr(st, seq))
		return;

	setva(st, seq);
	invoke_retransmission(st, seq);
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
	u_char cmd;

	if (st->l2.rc == st->l2.n200) {
		FsmChangeState(fi, ST_L2_4);
		st->l2.l2tei(st, MDL_VERIFY, (void *) st->l2.tei);
		st->l2.l2man(st, DL_RELEASE, NULL);
	} else {
		st->l2.rc++;

		if (FsmAddTimer(&st->l2.t200_timer, st->l2.t200, EV_L2_T200, NULL, 9))
			if (st->l2.l2m.debug)
				l2m_debug(&st->l2.l2m, "FAT 7");

		cmd = (st->l2.extended ? SABME : SABM) | 0x10; 
		send_uframe(st, cmd, CMD);
	}
}

static void
l2_st6_tout_200(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct Channel *chanp = st->l4.userdata;

	if (st->l2.rc == st->l2.n200) {
		FsmChangeState(fi, ST_L2_4);
		st->l2.l2man(st, DL_RELEASE, NULL);
	} else {
		st->l2.rc++;

		if (FsmAddTimer(&st->l2.t200_timer, st->l2.t200, EV_L2_T200, NULL, 10))
			if (st->l2.l2m.debug)
				l2m_debug(&st->l2.l2m, "FAT 8");


		if (!((chanp->impair == 2) && (st->l2.laptype == LAPB)))
			send_uframe(st, DISC | 0x10, CMD);

	}
}

static void
l2_pull_iqueue(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb;
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
		p1 += l2->extended ? 128 : 8;
	p1 = (p1 + l2->sow) % l2->window;
	if (l2->windowar[p1]) {
		printk(KERN_WARNING "isdnl2 try overwrite ack queue entry %d\n",
		       p1);
		SET_SKB_FREE(l2->windowar[p1]);
		dev_kfree_skb(l2->windowar[p1], FREE_WRITE);
	}
	l2->windowar[p1] = skb_clone(skb, GFP_ATOMIC);

	i = sethdraddr(&st->l2, header, CMD);

	if (l2->extended) {
		header[i++] = l2->vs << 1;
		header[i++] = l2->vr << 1;
		l2->vs = (l2->vs + 1) % 128;
	} else {
		header[i++] = (l2->vr << 5) | (l2->vs << 1);
		l2->vs = (l2->vs + 1) % 8;
	}

	memcpy(skb_push(skb, i), header, i);
	st->l2.l2l1(st, PH_DATA_PULLED, skb);
	if (!st->l2.t200_running) {
		FsmDelTimer(&st->l2.t203_timer, 13);
		if (FsmAddTimer(&st->l2.t200_timer, st->l2.t200, EV_L2_T200, NULL, 11))
			if (st->l2.l2m.debug)
				l2m_debug(&st->l2.l2m, "FAT 9");

		st->l2.t200_running = !0;
	}
	if (skb_queue_len(&l2->i_queue) && cansend(st))
		st->l2.l2l1(st, PH_REQUEST_PULL, NULL);
}

static void
transmit_enquiry(struct PStack *st)
{

	enquiry_command(st, RR, 1);
	if (FsmAddTimer(&st->l2.t200_timer, st->l2.t200, EV_L2_T200, NULL, 12))
		if (st->l2.l2m.debug)
			l2m_debug(&st->l2.l2m, "FAT 10");

	st->l2.t200_running = !0;
}

static void
l2_st7_tout_200(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	st->l2.t200_running = 0;

	st->l2.rc = 1;
	FsmChangeState(fi, ST_L2_8);
	transmit_enquiry(st);
}

static void
l2_got_st8_rr_rej(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;
	int PollFlag, seq, rsp;
	struct Layer2 *l2;

	l2 = &st->l2;
	if (l2->laptype == LAPD)
		rsp = *skb->data & 0x2;
	else
		rsp = *skb->data == 0x3;
	if (l2->orig)
		rsp = !rsp;

	skb_pull(skb, l2addrsize(l2));
	if (l2->extended) {
		PollFlag = (skb->data[1] & 0x1) == 0x1;
		seq = skb->data[1] >> 1;
	} else {
		PollFlag = (skb->data[0] & 0x10);
		seq = (skb->data[0] >> 5) & 0x7;
	}
	SET_SKB_FREE(skb);
	dev_kfree_skb(skb, FREE_READ);

	if (rsp && PollFlag) {
		if (legalnr(st, seq)) {
			FsmChangeState(fi, ST_L2_7);
			setva(st, seq);
			if (st->l2.t200_running) {
				FsmDelTimer(&st->l2.t200_timer, 14);
				st->l2.t200_running = 0;
			}
			if (FsmAddTimer(&st->l2.t203_timer, st->l2.t203, EV_L2_T203, NULL, 13))
				if (st->l2.l2m.debug)
					l2m_debug(&st->l2.l2m, "FAT 11");

			invoke_retransmission(st, seq);

			if (skb_queue_len(&l2->i_queue) && cansend(st))
				st->l2.l2l1(st, PH_REQUEST_PULL, NULL);
			else if (fi->userint & LC_FLUSH_WAIT) {
				fi->userint &= ~LC_FLUSH_WAIT;
				st->l2.l2man(st, DL_FLUSH, NULL);
			}
		}
	} else {
		if (!rsp && PollFlag)
			enquiry_response(st, RR, PollFlag);
		if (legalnr(st, seq)) {
			setva(st, seq);
		}
	}
}

static void
l2_st7_tout_203(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	st->l2.rc = 0;
	FsmChangeState(fi, ST_L2_8);
	transmit_enquiry(st);
}

static void
l2_st8_tout_200(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	if (st->l2.rc == st->l2.n200) {
		establishlink(fi);
	} else {
		st->l2.rc++;
		transmit_enquiry(st);
	}
}

static void
l2_got_FRMR(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;
	char tmp[64];

	skb_pull(skb, l2addrsize(&st->l2));
	if (st->l2.l2m.debug) {
		if (st->l2.extended)
			sprintf(tmp, "FRMR information %2x %2x %2x %2x %2x",
				skb->data[0], skb->data[1], skb->data[2],
				skb->data[3], skb->data[4]);
		else
			sprintf(tmp, "FRMR information %2x %2x %2x",
				skb->data[0], skb->data[1], skb->data[2]);

		l2m_debug(&st->l2.l2m, tmp);
	}
	SET_SKB_FREE(skb);
	dev_kfree_skb(skb, FREE_READ);
}

static void
l2_tei_remove(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

/*TODO
   if( DL_RELEASE.req outstanding ) {
   ... issue DL_RELEASE.confirm
   } else {
   if( fi->state != ST_L2_4 ) {
   ... issue DL_RELEASE.indication
   }
   }
   TODO */
	discard_i_queue(st);	/* There is no UI queue in layer 2 */
	st->l2.tei = 255;
	if (st->l2.t200_running) {
		FsmDelTimer(&st->l2.t200_timer, 18);
		st->l2.t200_running = 0;
	}
	FsmDelTimer(&st->l2.t203_timer, 19);
	st->l2.l2man(st, DL_RELEASE, NULL);	/* TEMP */
	FsmChangeState(fi, ST_L2_1);
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
IsI(u_char * data, int ext)
{
	return ((data[0] & 0x1) == 0x0);
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
	return (ext ? data[0] == REJ : (data[0] & 0xf) == 0x9);
}

inline int
IsFRMR(u_char * data, int ext)
{
	return ((data[0] & 0xef) == FRMR);
}

inline int
IsRNR(u_char * data, int ext)
{
	if (ext)
		return (data[0] == RNR);
	else
		return ((data[0] & 0xf) == 5);
}

static struct FsmNode L2FnList[] =
{
	{ST_L2_1, EV_L2_DL_ESTABLISH, l2s1},
	{ST_L2_1, EV_L2_MDL_NOTEIPROC, l2_no_tei},
	{ST_L2_3, EV_L2_MDL_ASSIGN, l2_got_tei},
	{ST_L2_4, EV_L2_DL_UNIT_DATA, l2_send_ui},
	{ST_L2_4, EV_L2_DL_ESTABLISH, l2_establish},
	{ST_L2_7, EV_L2_DL_UNIT_DATA, l2_send_ui},
	{ST_L2_7, EV_L2_DL_DATA, l2_feed_iqueue},
	{ST_L2_7, EV_L2_DL_RELEASE, l2_send_disconn},
	{ST_L2_7, EV_L2_ACK_PULL, l2_pull_iqueue},
	{ST_L2_8, EV_L2_DL_DATA, l2_feed_iqueue},
	{ST_L2_8, EV_L2_DL_RELEASE, l2_send_disconn},

	{ST_L2_1, EV_L2_UI, l2_receive_ui},
	{ST_L2_4, EV_L2_UI, l2_receive_ui},
	{ST_L2_4, EV_L2_SABMX, l2_got_SABMX},
	{ST_L2_4, EV_L2_DISC, l2_got_st4_disc},
	{ST_L2_5, EV_L2_UA, l2_got_ua_establish},
	{ST_L2_6, EV_L2_UA, l2_got_ua_disconn},
	{ST_L2_7, EV_L2_UI, l2_receive_ui},
	{ST_L2_7, EV_L2_DISC, l2_got_disconn},
	{ST_L2_7, EV_L2_I, l2_got_st7_data},
	{ST_L2_7, EV_L2_RR, l2_got_st7_RR},
	{ST_L2_7, EV_L2_REJ, l2_got_st7_rej},
	{ST_L2_7, EV_L2_SABMX, l2_got_SABMX},
	{ST_L2_7, EV_L2_FRMR, l2_got_FRMR},
	{ST_L2_8, EV_L2_RR, l2_got_st8_rr_rej},
	{ST_L2_8, EV_L2_REJ, l2_got_st8_rr_rej},
	{ST_L2_8, EV_L2_SABMX, l2_got_SABMX},
	{ST_L2_8, EV_L2_DISC, l2_got_disconn},
	{ST_L2_8, EV_L2_FRMR, l2_got_FRMR},
	{ST_L2_8, EV_L2_I, l2_got_st8_data},

	{ST_L2_5, EV_L2_T200, l2_st5_tout_200},
	{ST_L2_6, EV_L2_T200, l2_st6_tout_200},
	{ST_L2_7, EV_L2_T200, l2_st7_tout_200},
	{ST_L2_7, EV_L2_T203, l2_st7_tout_203},
	{ST_L2_8, EV_L2_T200, l2_st8_tout_200},

	{ST_L2_1, EV_L2_MDL_REMOVE, l2_tei_remove},
	{ST_L2_3, EV_L2_MDL_REMOVE, l2_tei_remove},
	{ST_L2_4, EV_L2_MDL_REMOVE, l2_tei_remove},
	{ST_L2_5, EV_L2_MDL_REMOVE, l2_tei_remove},
	{ST_L2_6, EV_L2_MDL_REMOVE, l2_tei_remove},
	{ST_L2_7, EV_L2_MDL_REMOVE, l2_tei_remove},
	{ST_L2_8, EV_L2_MDL_REMOVE, l2_tei_remove},
};

#define L2_FN_COUNT (sizeof(L2FnList)/sizeof(struct FsmNode))

static void
isdnl2_l1l2(struct PStack *st, int pr, void *arg)
{
	struct sk_buff *skb = arg;
	u_char *datap;
	int ret = !0;

	switch (pr) {
		case (PH_DATA):
			datap = skb->data;
			datap += l2addrsize(&st->l2);

			if (IsI(datap, st->l2.extended))
				ret = FsmEvent(&st->l2.l2m, EV_L2_I, skb);
			else if (IsRR(datap, st->l2.extended))
				ret = FsmEvent(&st->l2.l2m, EV_L2_RR, skb);
			else if (IsUI(datap, st->l2.extended))
				ret = FsmEvent(&st->l2.l2m, EV_L2_UI, skb);
			else if (IsSABMX(datap, st->l2.extended))
				ret = FsmEvent(&st->l2.l2m, EV_L2_SABMX, skb);
			else if (IsUA(datap, st->l2.extended))
				ret = FsmEvent(&st->l2.l2m, EV_L2_UA, skb);
			else if (IsDISC(datap, st->l2.extended))
				ret = FsmEvent(&st->l2.l2m, EV_L2_DISC, skb);
			else if (IsREJ(datap, st->l2.extended))
				ret = FsmEvent(&st->l2.l2m, EV_L2_REJ, skb);
			else if (IsFRMR(datap, st->l2.extended))
				ret = FsmEvent(&st->l2.l2m, EV_L2_FRMR, skb);
			else if (IsRNR(datap, st->l2.extended))
				ret = FsmEvent(&st->l2.l2m, EV_L2_RNR, skb);

			if (ret) {
				SET_SKB_FREE(skb);
				dev_kfree_skb(skb, FREE_READ);
			}
			break;
		case (PH_PULL_ACK):
			FsmEvent(&st->l2.l2m, EV_L2_ACK_PULL, arg);
			break;
	}
}

static void
isdnl2_l3l2(struct PStack *st, int pr, void *arg)
{
	switch (pr) {
		case (DL_DATA):
			if (FsmEvent(&st->l2.l2m, EV_L2_DL_DATA, arg)) {
				SET_SKB_FREE(((struct sk_buff *) arg));
				dev_kfree_skb((struct sk_buff *) arg, FREE_READ);
			}
			break;
		case (DL_UNIT_DATA):
			if (FsmEvent(&st->l2.l2m, EV_L2_DL_UNIT_DATA, arg)) {
				SET_SKB_FREE(((struct sk_buff *) arg));
				dev_kfree_skb((struct sk_buff *) arg, FREE_READ);
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
	}
}

static void
isdnl2_teil2(struct PStack *st, int pr, void *arg)
{
	switch (pr) {
		case (MDL_ASSIGN):
			FsmEvent(&st->l2.l2m, EV_L2_MDL_ASSIGN, arg);
			break;
		case (MDL_REMOVE):
			FsmEvent(&st->l2.l2m, EV_L2_MDL_REMOVE, arg);
			break;
	}
}

void
releasestack_isdnl2(struct PStack *st)
{
	FsmDelTimer(&st->l2.t200_timer, 15);
	FsmDelTimer(&st->l2.t203_timer, 16);
	discard_i_queue(st);
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
	st->ma.teil2 = isdnl2_teil2;

	st->l2.uihsize = l2headersize(&st->l2, !0);
	st->l2.ihsize = l2headersize(&st->l2, 0);
	skb_queue_head_init(&st->l2.i_queue);
	InitWin(&st->l2);
	st->l2.rejexp = 0;
	st->l2.debug = 0;

	st->l2.l2m.fsm = &l2fsm;
	st->l2.l2m.state = ST_L2_1;
	st->l2.l2m.debug = 0;
	st->l2.l2m.userdata = st;
	st->l2.l2m.userint = 0;
	st->l2.l2m.printdebug = l2m_debug;
	strcpy(st->l2.debug_id, debug_id);

	FsmInitTimer(&st->l2.l2m, &st->l2.t200_timer);
	FsmInitTimer(&st->l2.l2m, &st->l2.t203_timer);
	st->l2.t200_running = 0;
}

void
setstack_transl2(struct PStack *st)
{
}

void
releasestack_transl2(struct PStack *st)
{
}

void
Isdnl2New(void)
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
