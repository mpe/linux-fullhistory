/* $Id: isdnl2.c,v 1.2 1996/05/17 03:46:15 fritz Exp $
 *
 * $Log: isdnl2.c,v $
 * Revision 1.2  1996/05/17 03:46:15  fritz
 * General cleanup.
 *
 * Revision 1.1  1996/04/13 10:24:16  fritz
 * Initial revision
 *
 *
 */
#define __NO_VERSION__
#include "teles.h"

#define TIMER_1 2000

static void     l2m_debug(struct FsmInst *fi, char *s);

struct Fsm      l2fsm =
{NULL, 0, 0};

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

static char    *strL2State[] =
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
	EV_L2_DL_UNIT_DATA,
	EV_L2_DL_RELEASE,
	EV_L2_MDL_NOTEIPROC,
	EV_L2_T200,
	EV_L2_ACK_PULL,
	EV_L2_T203,
	EV_L2_RNR,
};

#define L2_EVENT_COUNT (EV_L2_RNR+1)

static char    *strL2Event[] =
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
	"EV_L2_DL_UNIT_DATA",
	"EV_L2_DL_RELEASE",
	"EV_L2_MDL_NOTEIPROC",
	"EV_L2_T200",
	"EV_L2_ACK_PULL",
	"EV_L2_T203",
	"EV_L2_RNR",
};

int             errcount = 0;

static int      l2addrsize(struct Layer2 *tsp);

static int
cansend(struct PStack *st)
{
	int             p1;

	p1 = (st->l2.va + st->l2.window) % (st->l2.extended ? 128 : 8);
	return (st->l2.vs != p1);
}

static void
discard_i_queue(struct PStack *st)
{
	struct BufHeader *ibh;

	while (!BufQueueUnlink(&ibh, &st->l2.i_queue))
		BufPoolRelease(ibh);
}

int
l2headersize(struct Layer2 *tsp, int UI)
{
	return ((tsp->extended && (!UI) ? 2 : 1) + (tsp->laptype == LAPD ? 2 : 1));
}

int
l2addrsize(struct Layer2 *tsp)
{
	return (tsp->laptype == LAPD ? 2 : 1);
}

static int
sethdraddr(struct Layer2 *tsp,
	   struct BufHeader *ibh, int rsp)
{
	byte           *ptr = DATAPTR(ibh);
	int             crbit;

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
	   struct BufHeader *ibh)
{
	st->l2.l2l1(st, PH_DATA, ibh);
}

static void
enqueue_super(struct PStack *st,
	      struct BufHeader *ibh)
{
	st->l2.l2l1(st, PH_DATA, ibh);
}

static int
legalnr(struct PStack *st, int nr)
{
	struct Layer2  *l2 = &st->l2;
	int             lnr, lvs;

	lvs = (l2->vs >= l2->va) ? l2->vs : (l2->vs + l2->extended ? 128 : 8);
	lnr = (nr >= l2->va) ? nr : (nr + l2->extended ? 128 : 8);
	return (lnr <= lvs);
}

static void
setva(struct PStack *st, int nr)
{
	struct Layer2  *l2 = &st->l2;

	if (l2->va != nr) {
                while (l2->va != nr) {
                        l2->va = (l2->va + 1) % (l2->extended ? 128 : 8);
                        BufPoolRelease(l2->windowar[l2->sow]);
                        l2->sow = (l2->sow + 1) % l2->window;
                }
                if (st->l4.l2writewakeup)
                        st->l4.l2writewakeup(st);
        }
}

static void
l2s1(struct FsmInst *fi, int event, void *arg)
{
	struct PStack  *st = fi->userdata;

	st->l2.l2tei(st, MDL_ASSIGN, (void *)st->l2.ces);
	FsmChangeState(fi, ST_L2_3);
}

static void
l2s2(struct FsmInst *fi, int event, void *arg)
{
	struct PStack  *st = fi->userdata;
	struct BufHeader *ibh = arg;

	byte           *ptr;
	int             i;

	i = sethdraddr(&(st->l2), ibh, 0);
	ptr = DATAPTR(ibh);
	ptr += i;
	*ptr = 0x3;

	enqueue_ui(st, ibh);
}

static void
l2s3(struct FsmInst *fi, int event, void *arg)
{
	struct PStack  *st = fi->userdata;
	struct BufHeader *ibh = arg;

	st->l2.l2l3(st, DL_UNIT_DATA, ibh);
}

static void
establishlink(struct FsmInst *fi)
{
	struct PStack  *st = fi->userdata;
	struct BufHeader *ibh;
	int             i;
	byte           *ptr;

	FsmChangeState(fi, ST_L2_5);
	st->l2.rc = 0;

	if (FsmAddTimer(&st->l2.t200_timer, st->l2.t200, EV_L2_T200, NULL, 1))
		if (st->l2.l2m.debug)
			l2m_debug(&st->l2.l2m, "FAT 1");


	if (BufPoolGet(&ibh, st->l1.smallpool, GFP_ATOMIC, (void *) st, 15))
		return;
	i = sethdraddr(&st->l2, ibh, 0);
	ptr = DATAPTR(ibh);
	ptr += i;
	if (st->l2.extended)
		*ptr = 0x7f;
	else
		*ptr = 0x3f;
	ibh->datasize = i + 1;

	enqueue_super(st, ibh);
}

static void
l2s11(struct FsmInst *fi, int event, void *arg)
{
	establishlink(fi);
}

static void
l2s13(struct FsmInst *fi, int event, void *arg)
{
	struct PStack  *st = fi->userdata;
	struct Channel *chanp = st->l4.userdata;
	byte           *ptr;
	struct BufHeader *ibh;
	int             i;

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


	if ((chanp->impair == 2) && (st->l2.laptype == LAPB))
		goto nodisc;

	if (BufPoolGet(&ibh, st->l1.smallpool, GFP_ATOMIC, (void *) st, 9))
		return;
	i = sethdraddr(&(st->l2), ibh, 0);
	ptr = DATAPTR(ibh);
	ptr += i;
	*ptr = 0x53;
	ibh->datasize = i + 1;
	enqueue_super(st, ibh);

      nodisc:
	discard_i_queue(st);
}

static void
l2s12(struct FsmInst *fi, int event, void *arg)
{
	struct PStack  *st = fi->userdata;
	struct BufHeader *ibh = arg;
	byte           *ptr;
	int             i;

	BufPoolRelease(ibh);
	st->l2.vs = 0;
	st->l2.va = 0;
	st->l2.vr = 0;
	st->l2.sow = 0;
	FsmChangeState(fi, ST_L2_7);
	if (FsmAddTimer(&st->l2.t203_timer, st->l2.t203, EV_L2_T203, NULL, 3))
		if (st->l2.l2m.debug)
			l2m_debug(&st->l2.l2m, "FAT 3");

	st->l2.l2man(st, DL_ESTABLISH, NULL);

	if (BufPoolGet(&ibh, st->l1.smallpool, GFP_ATOMIC, (void *) st, 10))
		return;
	i = sethdraddr(&(st->l2), ibh, 0);
	ptr = DATAPTR(ibh);
	ptr += i;
	*ptr = 0x73;
	ibh->datasize = i + 1;
	enqueue_super(st, ibh);

}

static void
l2s14(struct FsmInst *fi, int event, void *arg)
{
	struct PStack  *st = fi->userdata;
	struct BufHeader *ibh = arg;
	struct Channel *chanp = st->l4.userdata;
	byte           *ptr;
	int             i, p;

	ptr = DATAPTR(ibh);
	ptr += l2addrsize(&(st->l2));
	p = (*ptr) & 0x10;
	BufPoolRelease(ibh);

	FsmChangeState(fi, ST_L2_4);

	FsmDelTimer(&st->l2.t203_timer, 3);
	if (st->l2.t200_running) {
		FsmDelTimer(&st->l2.t200_timer, 4);
		st->l2.t200_running = 0;
	}
	if ((chanp->impair == 1) && (st->l2.laptype == LAPB))
		goto noresponse;

	if (BufPoolGet(&ibh, st->l1.smallpool, GFP_ATOMIC, (void *) st, 11))
		return;
	i = sethdraddr(&(st->l2), ibh, 0);
	ptr = DATAPTR(ibh);
	ptr += i;
	*ptr = 0x63 | (p ? 0x10 : 0x0);
	ibh->datasize = i + 1;
	enqueue_super(st, ibh);

      noresponse:
	st->l2.l2man(st, DL_RELEASE, NULL);

}

static void
l2s5(struct FsmInst *fi, int event, void *arg)
{
	struct PStack  *st = fi->userdata;
	struct BufHeader *ibh = arg;
	int             f;
	byte           *data;

	data = DATAPTR(ibh);
	data += l2addrsize(&(st->l2));

	f = *data & 0x10;
	BufPoolRelease(ibh);

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
l2s15(struct FsmInst *fi, int event, void *arg)
{
	struct PStack  *st = fi->userdata;
	struct BufHeader *ibh = arg;
	int             f;
	byte           *data;

	data = DATAPTR(ibh);
	data += l2addrsize(&st->l2);

	f = *data & 0x10;
	BufPoolRelease(ibh);

	if (f) {
		FsmDelTimer(&st->l2.t200_timer, 6);
		FsmChangeState(fi, ST_L2_4);
		st->l2.l2man(st, DL_RELEASE, NULL);
	}
}

static void
l2s6(struct FsmInst *fi, int event, void *arg)
{
	struct PStack  *st = fi->userdata;
	struct Channel *chanp = st->l4.userdata;
	struct BufHeader *ibh = arg;
	int             p, i, seq, rsp;
	byte           *ptr;
	struct Layer2  *l2;

	l2 = &st->l2;
	ptr = DATAPTR(ibh);

	if (l2->laptype == LAPD) {
		rsp = ptr[0] & 0x2;
		if (l2->orig)
			rsp = !rsp;
	} else {
		rsp = ptr[0] == 0x3;
		if (l2->orig)
			rsp = !rsp;
	}

	ptr += l2addrsize(l2);

	if (l2->extended) {
		p = (ptr[1] & 0x1) == 0x1;
		seq = ptr[1] >> 1;
	} else {
		p = (ptr[0] & 0x10);
		seq = (ptr[0] >> 5) & 0x7;
	}
	BufPoolRelease(ibh);

	if ((chanp->impair == 4) && (st->l2.laptype == LAPB))
		goto noresp;

	if ((!rsp) && p) {
		if (!BufPoolGet(&ibh, st->l1.smallpool, GFP_ATOMIC, (void *) st, 12)) {
			i = sethdraddr(l2, ibh, !0);
			ptr = DATAPTR(ibh);
			ptr += i;

			if (l2->extended) {
				*ptr++ = 0x1;
				*ptr++ = (l2->vr << 1) | (p ? 1 : 0);
				i += 2;
			} else {
				*ptr++ = (l2->vr << 5) | 0x1 | (p ? 0x10 : 0x0);
				i += 1;
			}
			ibh->datasize = i;
			enqueue_super(st, ibh);
		}
	}
      noresp:
	if (legalnr(st, seq))
		if (seq == st->l2.vs) {
			setva(st, seq);
			FsmDelTimer(&st->l2.t200_timer, 7);
			st->l2.t200_running = 0;
			FsmDelTimer(&st->l2.t203_timer, 8);
			if (FsmAddTimer(&st->l2.t203_timer, st->l2.t203, EV_L2_T203, NULL, 5))
				if (st->l2.l2m.debug)
					l2m_debug(&st->l2.l2m, "FAT 5");

			if (st->l2.i_queue.head)
				st->l2.l2l1(st, PH_REQUEST_PULL, NULL);
		} else if (st->l2.va != seq) {
			setva(st, seq);
			FsmDelTimer(&st->l2.t200_timer, 9);
			if (FsmAddTimer(&st->l2.t200_timer, st->l2.t200, EV_L2_T200, NULL, 6))
				if (st->l2.l2m.debug)
					l2m_debug(&st->l2.l2m, "FAT 6");

			if (st->l2.i_queue.head)
				st->l2.l2l1(st, PH_REQUEST_PULL, NULL);
		}
}

static void
l2s7(struct FsmInst *fi, int event, void *arg)
{
	struct PStack  *st = fi->userdata;
	struct BufHeader *ibh = arg;
	int             i;
	byte           *ptr;
	struct IsdnCardState *sp = st->l1.hardware;
	char            str[64];

	i = sethdraddr(&st->l2, ibh, 0);
	ptr = DATAPTR(ibh);

	if (st->l2.laptype == LAPD)
		if (sp->dlogflag) {
			sprintf(str, "Q.931 frame user->network tei %d", st->l2.tei);
			dlogframe(sp, ptr + st->l2.ihsize, ibh->datasize - st->l2.ihsize,
				  str);
		}
	BufQueueLink(&st->l2.i_queue, ibh);

	st->l2.l2l1(st, PH_REQUEST_PULL, NULL);
}

static void
l2s8(struct FsmInst *fi, int event, void *arg)
{
	struct PStack  *st = fi->userdata;
	struct Channel *chanp = st->l4.userdata;
	struct BufHeader *ibh = arg;
	byte           *ptr;
	struct BufHeader *ibh2;
	struct IsdnCardState *sp = st->l1.hardware;
	struct Layer2  *l2 = &(st->l2);
	int             i, p, seq, nr, wasok;
	char            str[64];

	ptr = DATAPTR(ibh);
	ptr += l2addrsize(l2);
	if (l2->extended) {
		p = (ptr[1] & 0x1) == 0x1;
		seq = ptr[0] >> 1;
		nr = (ptr[1] >> 1) & 0x7f;
	} else {
		p = (ptr[0] & 0x10);
		seq = (ptr[0] >> 1) & 0x7;
		nr = (ptr[0] >> 5) & 0x7;
	}

	if (l2->vr == seq) {
		wasok = !0;

		l2->vr = (l2->vr + 1) % (l2->extended ? 128 : 8);
		l2->rejexp = 0;

		ptr = DATAPTR(ibh);
		if (st->l2.laptype == LAPD)
			if (sp->dlogflag) {
				sprintf(str, "Q.931 frame network->user tei %d", st->l2.tei);
				dlogframe(st->l1.hardware, ptr + l2->ihsize,
					ibh->datasize - l2->ihsize, str);
			}
	      label8_1:
		if ((chanp->impair == 3) && (st->l2.laptype == LAPB))
			goto noRR;

		if (!BufPoolGet(&ibh2, st->l1.smallpool, GFP_ATOMIC, (void *) st, 13)) {
			i = sethdraddr(&(st->l2), ibh2, p);
			ptr = DATAPTR(ibh2);
			ptr += i;

			if (l2->extended) {
				*ptr++ = 0x1;
				*ptr++ = (l2->vr << 1) | (p ? 1 : 0);
				i += 2;
			} else {
				*ptr++ = (l2->vr << 5) | 0x1 | (p ? 0x10 : 0x0);
				i += 1;
			}
			ibh2->datasize = i;
			enqueue_super(st, ibh2);
		      noRR:
		}
	} else {
	        /* n(s)!=v(r) */
		wasok = 0;
		BufPoolRelease(ibh);
		if (st->l2.rejexp) {
			if (p)
				goto label8_1;
		} else {
			st->l2.rejexp = !0;
			if (!BufPoolGet(&ibh2, st->l1.smallpool, GFP_ATOMIC, (void *) st, 14)) {
				i = sethdraddr(&(st->l2), ibh2, p);
				ptr = DATAPTR(ibh2);
				ptr += i;

				if (l2->extended) {
					*ptr++ = 0x9;
					*ptr++ = (l2->vr << 1) | (p ? 1 : 0);
					i += 2;
				} else {
					*ptr++ = (l2->vr << 5) | 0x9 | (p ? 0x10 : 0x0);
					i += 1;
				}
				ibh2->datasize = i;
				enqueue_super(st, ibh2);
			}
		}
	}

	if (legalnr(st, nr))
		if (nr == st->l2.vs) {
			setva(st, nr);
			FsmDelTimer(&st->l2.t200_timer, 10);
			st->l2.t200_running = 0;
			FsmDelTimer(&st->l2.t203_timer, 11);
			if (FsmAddTimer(&st->l2.t203_timer, st->l2.t203, EV_L2_T203, NULL, 7))
				if (st->l2.l2m.debug)
					l2m_debug(&st->l2.l2m, "FAT 5");

			if (st->l2.i_queue.head)
				st->l2.l2l1(st, PH_REQUEST_PULL, NULL);
		} else if (nr != st->l2.va) {
			setva(st, nr);
			FsmDelTimer(&st->l2.t200_timer, 12);
			if (FsmAddTimer(&st->l2.t200_timer, st->l2.t200, EV_L2_T200, NULL, 8))
				if (st->l2.l2m.debug)
					l2m_debug(&st->l2.l2m, "FAT 6");

			if (st->l2.i_queue.head)
				st->l2.l2l1(st, PH_REQUEST_PULL, NULL);
		}
	if (wasok)
		st->l2.l2l3(st, DL_DATA, ibh);

}

static void
l2s17(struct FsmInst *fi, int event, void *arg)
{
	struct PStack  *st = fi->userdata;

	st->l2.tei = (int) arg;
	establishlink(fi);
}

static void
enquiry_response(struct PStack *st)
{
	struct BufHeader *ibh2;
	int             i;
	byte           *ptr;
	struct Layer2  *l2;

	l2 = &st->l2;
	if (!BufPoolGet(&ibh2, st->l1.smallpool, GFP_ATOMIC, (void *) st, 16)) {
		i = sethdraddr(&(st->l2), ibh2, !0);
		ptr = DATAPTR(ibh2);
		ptr += i;

		if (l2->extended) {
			*ptr++ = 0x1;
			*ptr++ = (l2->vr << 1) | 0x1;
			i += 2;
		} else {
			*ptr++ = (l2->vr << 5) | 0x1 | 0x10;
			i += 1;
		}
		ibh2->datasize = i;
		enqueue_super(st, ibh2);
	}
}

static void
invoke_retransmission(struct PStack *st, int nr)
{
	struct Layer2  *l2 = &st->l2;
	int             p1;

	if (l2->vs != nr) {
		while (l2->vs != nr) {

			l2->vs = l2->vs - 1;
			if (l2->vs < 0)
				l2->vs += l2->extended ? 128 : 8;

			p1 = l2->vs - l2->va;
			if (p1 < 0)
				p1 += l2->extended ? 128 : 8;
			p1 = (p1 + l2->sow) % l2->window;

			BufQueueLinkFront(&l2->i_queue, l2->windowar[p1]);
		}
		st->l2.l2l1(st, PH_REQUEST_PULL, NULL);
	}
}

static void
l2s16(struct FsmInst *fi, int event, void *arg)
{
	struct PStack  *st = fi->userdata;
	struct BufHeader *ibh = arg;
	int             p, seq, rsp;
	byte           *ptr;
	struct Layer2  *l2;

	l2 = &(st->l2);
	ptr = DATAPTR(ibh);

	if (l2->laptype == LAPD) {
		rsp = ptr[0] & 0x2;
		if (l2->orig)
			rsp = !rsp;
	} else {
		rsp = ptr[0] == 0x3;
		if (l2->orig)
			rsp = !rsp;
	}


	ptr += l2addrsize(l2);

	if (l2->extended) {
		p = (ptr[1] & 0x1) == 0x1;
		seq = ptr[1] >> 1;
	} else {
		p = (ptr[0] & 0x10);
		seq = (ptr[0] >> 5) & 0x7;
	}
	BufPoolRelease(ibh);

	if ((!rsp) && p)
		enquiry_response(st);

	if (!legalnr(st, seq))
		return;

	setva(st, seq);
	invoke_retransmission(st, seq);

}

static void
l2s19(struct FsmInst *fi, int event, void *arg)
{
	FsmChangeState(fi, ST_L2_4);
}

static void
l2s20(struct FsmInst *fi, int event, void *arg)
{
	struct PStack  *st = fi->userdata;
	int             i;
	struct BufHeader *ibh;
	byte           *ptr;

	if (st->l2.rc == st->l2.n200) {
		FsmChangeState(fi, ST_L2_4);
		st->l2.l2man(st, DL_RELEASE, NULL);
	} else {
		st->l2.rc++;

		if (FsmAddTimer(&st->l2.t200_timer, st->l2.t200, EV_L2_T200, NULL, 9))
			if (st->l2.l2m.debug)
				l2m_debug(&st->l2.l2m, "FAT 7");

		if (BufPoolGet(&ibh, st->l1.smallpool, GFP_ATOMIC, (void *) st, 15))
			return;

		i = sethdraddr(&st->l2, ibh, 0);
		ptr = DATAPTR(ibh);
		ptr += i;
		if (st->l2.extended)
			*ptr = 0x7f;
		else
			*ptr = 0x3f;
		ibh->datasize = i + 1;
		enqueue_super(st, ibh);
	}
}

static void
l2s21(struct FsmInst *fi, int event, void *arg)
{
	struct PStack  *st = fi->userdata;
	struct Channel *chanp = st->l4.userdata;
	int             i;
	struct BufHeader *ibh;
	byte           *ptr;

	if (st->l2.rc == st->l2.n200) {
		FsmChangeState(fi, ST_L2_4);
		st->l2.l2man(st, DL_RELEASE, NULL);
	} else {
		st->l2.rc++;

		if (FsmAddTimer(&st->l2.t200_timer, st->l2.t200, EV_L2_T200, NULL, 10))
			if (st->l2.l2m.debug)
				l2m_debug(&st->l2.l2m, "FAT 8");


		if ((chanp->impair == 2) && (st->l2.laptype == LAPB))
			goto nodisc;

		if (BufPoolGet(&ibh, st->l1.smallpool, GFP_ATOMIC, (void *) st, 15))
			return;

		i = sethdraddr(&st->l2, ibh, 0);
		ptr = DATAPTR(ibh);
		ptr += i;
		*ptr = 0x53;
		ibh->datasize = i + 1;
		enqueue_super(st, ibh);
	      nodisc:

	}
}

static void
l2s22(struct FsmInst *fi, int event, void *arg)
{
	struct PStack  *st = fi->userdata;
	struct BufHeader *ibh;
	struct Layer2  *l2 = &st->l2;
	byte           *ptr;
	int             p1;

	if (!cansend(st))
		return;

	if (BufQueueUnlink(&ibh, &l2->i_queue))
		return;


	p1 = l2->vs - l2->va;
	if (p1 < 0)
		p1 += l2->extended ? 128 : 8;
	p1 = (p1 + l2->sow) % l2->window;
	l2->windowar[p1] = ibh;

	ptr = DATAPTR(ibh);
	ptr += l2addrsize(l2);

	if (l2->extended) {
		*ptr++ = l2->vs << 1;
		*ptr++ = (l2->vr << 1) | 0x1;
		l2->vs = (l2->vs + 1) % 128;
	} else {
		*ptr++ = (l2->vr << 5) | (l2->vs << 1) | 0x10;
		l2->vs = (l2->vs + 1) % 8;
	}

	st->l2.l2l1(st, PH_DATA_PULLED, ibh);

	if (!st->l2.t200_running) {
		FsmDelTimer(&st->l2.t203_timer, 13);
		if (FsmAddTimer(&st->l2.t200_timer, st->l2.t200, EV_L2_T200, NULL, 11))
			if (st->l2.l2m.debug)
				l2m_debug(&st->l2.l2m, "FAT 9");

		st->l2.t200_running = !0;
	}
	if (l2->i_queue.head && cansend(st))
		st->l2.l2l1(st, PH_REQUEST_PULL, NULL);

}

static void
transmit_enquiry(struct PStack *st)
{
	struct BufHeader *ibh;
	byte           *ptr;

	if (!BufPoolGet(&ibh, st->l1.smallpool, GFP_ATOMIC, (void *) st, 12)) {
		ptr = DATAPTR(ibh);
		ptr += sethdraddr(&st->l2, ibh, 0);

		if (st->l2.extended) {
			*ptr++ = 0x1;
			*ptr++ = (st->l2.vr << 1) | 1;
		} else {
			*ptr++ = (st->l2.vr << 5) | 0x11;
		}
		ibh->datasize = ptr - DATAPTR(ibh);
		enqueue_super(st, ibh);
		if (FsmAddTimer(&st->l2.t200_timer, st->l2.t200, EV_L2_T200, NULL, 12))
			if (st->l2.l2m.debug)
				l2m_debug(&st->l2.l2m, "FAT 10");

		st->l2.t200_running = !0;
	}
}

static void
l2s23(struct FsmInst *fi, int event, void *arg)
{
	struct PStack  *st = fi->userdata;

	st->l2.t200_running = 0;

	st->l2.rc = 1;
	FsmChangeState(fi, ST_L2_8);
	transmit_enquiry(st);
}

static void
l2s24(struct FsmInst *fi, int event, void *arg)
{
	struct PStack  *st = fi->userdata;
	struct BufHeader *ibh = arg;
	int             p, seq, rsp;
	byte           *ptr;
	struct Layer2  *l2;

	l2 = &st->l2;
	ptr = DATAPTR(ibh);

	if (l2->laptype == LAPD) {
		rsp = ptr[0] & 0x2;
		if (l2->orig)
			rsp = !rsp;
	} else {
		rsp = ptr[0] == 0x3;
		if (l2->orig)
			rsp = !rsp;
	}


	ptr += l2addrsize(l2);

	if (l2->extended) {
		p = (ptr[1] & 0x1) == 0x1;
		seq = ptr[1] >> 1;
	} else {
		p = (ptr[0] & 0x10);
		seq = (ptr[0] >> 5) & 0x7;
	}
	BufPoolRelease(ibh);

	if (rsp && p) {
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
		}
	} else {
		if (!rsp && p)
			enquiry_response(st);
		if (legalnr(st, seq)) {
			setva(st, seq);
		}
	}
}

static void
l2s25(struct FsmInst *fi, int event, void *arg)
{
	struct PStack  *st = fi->userdata;

	st->l2.rc = 0;
	FsmChangeState(fi, ST_L2_8);
	transmit_enquiry(st);
}

static void
l2s26(struct FsmInst *fi, int event, void *arg)
{
	struct PStack  *st = fi->userdata;

	if (st->l2.rc == st->l2.n200) {
		l2s13(fi, event, NULL);
	} else {
		st->l2.rc++;
		transmit_enquiry(st);
	}
}

static void
l2s27(struct FsmInst *fi, int event, void *arg)
{
	struct PStack  *st = fi->userdata;
	struct BufHeader *ibh = arg;
	byte           *ptr;
	int             i, p, est;

	ptr = DATAPTR(ibh);
	ptr += l2addrsize(&st->l2);

	if (st->l2.extended)
		p = ptr[1] & 0x1;
	else
		p = ptr[0] & 0x10;

	BufPoolRelease(ibh);

	if (BufPoolGet(&ibh, st->l1.smallpool, GFP_ATOMIC, (void *) st, 10))
		return;
	i = sethdraddr(&st->l2, ibh, 0);
	ptr = DATAPTR(ibh);
	ptr += i;
	*ptr = 0x63 | p;
	ibh->datasize = i + 1;
	enqueue_super(st, ibh);

	if (st->l2.vs != st->l2.va) {
		discard_i_queue(st);
		est = !0;
	} else
		est = 0;

	FsmDelTimer(&st->l2.t200_timer, 15);
	st->l2.t200_running = 0;

	if (FsmAddTimer(&st->l2.t203_timer, st->l2.t203, EV_L2_T203, NULL, 3))
		if (st->l2.l2m.debug)
			l2m_debug(&st->l2.l2m, "FAT 12");

	st->l2.vs = 0;
	st->l2.va = 0;
	st->l2.vr = 0;
	st->l2.sow = 0;


	if (est)
		st->l2.l2man(st, DL_ESTABLISH, NULL);

}

static void
l2s28(struct FsmInst *fi, int event, void *arg)
{
	struct PStack  *st = fi->userdata;
	struct BufHeader *ibh = arg;
	byte           *ptr;
	char            tmp[64];

	ptr = DATAPTR(ibh);
	ptr += l2addrsize(&st->l2);
	ptr++;

	if (st->l2.l2m.debug) {
		if (st->l2.extended)
			sprintf(tmp, "FRMR information %2x %2x %2x %2x %2x",
				ptr[0], ptr[1], ptr[2], ptr[3], ptr[4]);
		else
			sprintf(tmp, "FRMR information %2x %2x %2x",
				ptr[0], ptr[1], ptr[2]);

		l2m_debug(&st->l2.l2m, tmp);
	}
	BufPoolRelease(ibh);
}

static int
IsUI(byte * data, int ext)
{
	return ((data[0] & 0xef) == 0x3);
}

static int
IsUA(byte * data, int ext)
{
	return ((data[0] & 0xef) == 0x63);
}

static int
IsDISC(byte * data, int ext)
{
	return ((data[0] & 0xef) == 0x43);
}

static int
IsRR(byte * data, int ext)
{
	if (ext)
		return (data[0] == 0x1);
	else
		return ((data[0] & 0xf) == 1);
}

static int
IsI(byte * data, int ext)
{
	return ((data[0] & 0x1) == 0x0);
}

static int
IsSABMX(byte * data, int ext)
{
	return (ext ? data[0] == 0x7f : data[0] == 0x3f);
}

static int
IsREJ(byte * data, int ext)
{
	return (ext ? data[0] == 0x9 : (data[0] & 0xf) == 0x9);
}

static int
IsFRMR(byte * data, int ext)
{
	return ((data[0] & 0xef) == 0x87);
}

static int
IsRNR(byte * data, int ext)
{
	if (ext)
		return (data[0] == 0x5);
	else
		return ((data[0] & 0xf) == 5);
}

static struct FsmNode L2FnList[] =
{
	{ST_L2_1, EV_L2_DL_ESTABLISH, l2s1},
	{ST_L2_1, EV_L2_MDL_NOTEIPROC, l2s19},
	{ST_L2_3, EV_L2_MDL_ASSIGN, l2s17},
	{ST_L2_4, EV_L2_DL_UNIT_DATA, l2s2},
	{ST_L2_4, EV_L2_DL_ESTABLISH, l2s11},
	{ST_L2_7, EV_L2_DL_UNIT_DATA, l2s2},
	{ST_L2_7, EV_L2_DL_DATA, l2s7},
	{ST_L2_7, EV_L2_DL_RELEASE, l2s13},
	{ST_L2_7, EV_L2_ACK_PULL, l2s22},
	{ST_L2_8, EV_L2_DL_RELEASE, l2s13},

	{ST_L2_1, EV_L2_UI, l2s3},
	{ST_L2_4, EV_L2_UI, l2s3},
	{ST_L2_4, EV_L2_SABMX, l2s12},
	{ST_L2_5, EV_L2_UA, l2s5},
	{ST_L2_6, EV_L2_UA, l2s15},
	{ST_L2_7, EV_L2_UI, l2s3},
	{ST_L2_7, EV_L2_DISC, l2s14},
	{ST_L2_7, EV_L2_I, l2s8},
	{ST_L2_7, EV_L2_RR, l2s6},
	{ST_L2_7, EV_L2_REJ, l2s16},
	{ST_L2_7, EV_L2_SABMX, l2s27},
	{ST_L2_7, EV_L2_FRMR, l2s28},
	{ST_L2_8, EV_L2_RR, l2s24},
	{ST_L2_8, EV_L2_DISC, l2s14},
	{ST_L2_8, EV_L2_FRMR, l2s28},

	{ST_L2_5, EV_L2_T200, l2s20},
	{ST_L2_6, EV_L2_T200, l2s21},
	{ST_L2_7, EV_L2_T200, l2s23},
	{ST_L2_7, EV_L2_T203, l2s25},
	{ST_L2_8, EV_L2_T200, l2s26},
};

#define L2_FN_COUNT (sizeof(L2FnList)/sizeof(struct FsmNode))

static void
isdnl2_l1l2(struct PStack *st, int pr, struct BufHeader *arg)
{
	struct BufHeader *ibh;
	byte           *datap;
	int             ret = !0;

	switch (pr) {
	  case (PH_DATA):

		  ibh = arg;
		  datap = DATAPTR(ibh);
		  datap += l2addrsize(&st->l2);

		  if (IsI(datap, st->l2.extended))
			  ret = FsmEvent(&st->l2.l2m, EV_L2_I, ibh);
		  else if (IsRR(datap, st->l2.extended))
			  ret = FsmEvent(&st->l2.l2m, EV_L2_RR, ibh);
		  else if (IsUI(datap, st->l2.extended))
			  ret = FsmEvent(&st->l2.l2m, EV_L2_UI, ibh);
		  else if (IsSABMX(datap, st->l2.extended))
			  ret = FsmEvent(&st->l2.l2m, EV_L2_SABMX, ibh);
		  else if (IsUA(datap, st->l2.extended))
			  ret = FsmEvent(&st->l2.l2m, EV_L2_UA, ibh);
		  else if (IsDISC(datap, st->l2.extended))
			  ret = FsmEvent(&st->l2.l2m, EV_L2_DISC, ibh);
		  else if (IsREJ(datap, st->l2.extended))
			  ret = FsmEvent(&st->l2.l2m, EV_L2_REJ, ibh);
		  else if (IsFRMR(datap, st->l2.extended))
			  ret = FsmEvent(&st->l2.l2m, EV_L2_FRMR, ibh);
		  else if (IsRNR(datap, st->l2.extended))
			  ret = FsmEvent(&st->l2.l2m, EV_L2_RNR, ibh);

		  if (ret)
			  BufPoolRelease(ibh);

		  break;
	  case (PH_PULL_ACK):
		  FsmEvent(&st->l2.l2m, EV_L2_ACK_PULL, arg);
		  break;
	}
}

static void
isdnl2_l3l2(struct PStack *st, int pr,
	    void *arg)
{
	switch (pr) {
	  case (DL_DATA):
		  if (FsmEvent(&st->l2.l2m, EV_L2_DL_DATA, arg))
			  BufPoolRelease((struct BufHeader *) arg);
		  break;
	  case (DL_UNIT_DATA):
		  if (FsmEvent(&st->l2.l2m, EV_L2_DL_UNIT_DATA, arg))
			  BufPoolRelease((struct BufHeader *) arg);
		  break;
	}
}

static void
isdnl2_manl2(struct PStack *st, int pr,
	     void *arg)
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
	}
}

static void
isdnl2_teil2(struct PStack *st, int pr,
	     void *arg)
{
	switch (pr) {
	  case (MDL_ASSIGN):
		  FsmEvent(&st->l2.l2m, EV_L2_MDL_ASSIGN, arg);
		  break;
	}
}

void
releasestack_isdnl2(struct PStack *st)
{
	FsmDelTimer(&st->l2.t200_timer, 15);
	FsmDelTimer(&st->l2.t203_timer, 16);
}

static void
l2m_debug(struct FsmInst *fi, char *s)
{
	struct PStack  *st = fi->userdata;
	char            tm[32], str[256];

	jiftime(tm, jiffies);
	sprintf(str, "%s %s %s\n", tm, st->l2.debug_id, s);
	teles_putstatus(str);
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
	BufQueueInit(&(st->l2.i_queue));
	st->l2.rejexp = 0;
	st->l2.debug = 1;

	st->l2.l2m.fsm = &l2fsm;
	st->l2.l2m.state = ST_L2_1;
	st->l2.l2m.debug = 0;
	st->l2.l2m.userdata = st;
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
