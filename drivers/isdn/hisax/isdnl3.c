/* $Id: isdnl3.c,v 2.8 1998/11/15 23:55:04 keil Exp $

 * Author       Karsten Keil (keil@isdn4linux.de)
 *              based on the teles driver from Jan den Ouden
 *
 *		This file is (c) under GNU PUBLIC LICENSE
 *		For changes and modifications please read
 *		../../../Documentation/isdn/HiSax.cert
 *
 * Thanks to    Jan den Ouden
 *              Fritz Elfert
 *
 * $Log: isdnl3.c,v $
 * Revision 2.8  1998/11/15 23:55:04  keil
 * changes from 2.0
 *
 * Revision 2.7  1998/05/25 14:10:15  keil
 * HiSax 3.0
 * X.75 and leased are working again.
 *
 * Revision 2.6  1998/05/25 12:58:11  keil
 * HiSax golden code from certification, Don't use !!!
 * No leased lines, no X75, but many changes.
 *
 * Revision 2.5  1998/02/12 23:07:52  keil
 * change for 2.1.86 (removing FREE_READ/FREE_WRITE from [dev]_kfree_skb()
 *
 * Revision 2.4  1997/11/06 17:09:25  keil
 * New 2.1 init code
 *
 * Revision 2.3  1997/10/29 19:07:53  keil
 * changes for 2.1
 *
 * Revision 2.2  1997/10/01 09:21:41  fritz
 * Removed old compatibility stuff for 2.0.X kernels.
 * From now on, this code is for 2.1.X ONLY!
 * Old stuff is still in the separate branch.
 *
 * Revision 2.1  1997/08/03 14:36:32  keil
 * Implement RESTART procedure
 *
 * Revision 2.0  1997/07/27 21:15:42  keil
 * New Callref based layer3
 *
 * Revision 1.11  1997/06/26 11:11:44  keil
 * SET_SKBFREE now on creation of a SKB
 *
 * Revision 1.10  1997/04/06 22:54:16  keil
 * Using SKB's
 *
 * Revision 1.9  1997/03/25 23:11:25  keil
 * US NI-1 protocol
 *
 * Revision 1.8  1997/03/21 18:53:44  keil
 * Report no protocol error to syslog too
 *
 * Remove old logs /KKe
 *
 */
#define __NO_VERSION__
#include "hisax.h"
#include "isdnl3.h"
#include <linux/config.h>

const char *l3_revision = "$Revision: 2.8 $";

static
struct Fsm l3fsm =
{NULL, 0, 0, NULL, NULL};

enum {
	ST_L3_LC_REL,
	ST_L3_LC_ESTAB_WAIT,
	ST_L3_LC_REL_WAIT,
	ST_L3_LC_ESTAB,
};

#define L3_STATE_COUNT (ST_L3_LC_ESTAB+1)

static char *strL3State[] =
{
	"ST_L3_LC_REL",
	"ST_L3_LC_ESTAB_WAIT",
	"ST_L3_LC_REL_WAIT",
	"ST_L3_LC_ESTAB",
};

enum {
	EV_ESTABLISH_REQ,
	EV_ESTABLISH_IND,
	EV_ESTABLISH_CNF,
	EV_RELEASE_REQ,
	EV_RELEASE_CNF,
	EV_RELEASE_IND,
};

#define L3_EVENT_COUNT (EV_RELEASE_IND+1)

static char *strL3Event[] =
{
	"EV_ESTABLISH_REQ",
	"EV_ESTABLISH_IND",
	"EV_ESTABLISH_CNF",
	"EV_RELEASE_REQ",
	"EV_RELEASE_CNF",
	"EV_RELEASE_IND",
};

static void
l3m_debug(struct FsmInst *fi, char *fmt, ...)
{
	va_list args;
	struct PStack *st = fi->userdata;

	va_start(args, fmt);
	VHiSax_putstatus(st->l1.hardware, st->l3.debug_id, fmt, args);
	va_end(args);
}

u_char *
findie(u_char * p, int size, u_char ie, int wanted_set)
{
	int l, codeset, maincodeset;
	u_char *pend = p + size;

	/* skip protocol discriminator, callref and message type */
	p++;
	l = (*p++) & 0xf;
	p += l;
	p++;
	codeset = 0;
	maincodeset = 0;
	/* while there are bytes left... */
	while (p < pend) {
		if ((*p & 0xf0) == 0x90) {
			codeset = *p & 0x07;
			if (!(*p & 0x08))
				maincodeset = codeset;
		}
		if (*p & 0x80)
			p++;
		else {
			if (codeset == wanted_set) {
				if (*p == ie)
					return (p);
				if (*p > ie)
					return (NULL);
			}
			p++;
			l = *p++;
			p += l;
			codeset = maincodeset;
		}
	}
	return (NULL);
}

int
getcallref(u_char * p)
{
	int l, m = 1, cr = 0;
	p++;			/* prot discr */
	l = 0xf & *p++;		/* callref length */
	if (!l)			/* dummy CallRef */
		return(-1);
	while (l--) {
		cr += m * (*p++);
		m *= 8;
	}
	return (cr);
}

static int OrigCallRef = 0;

int
newcallref(void)
{
	if (OrigCallRef == 127)
		OrigCallRef = 1;
	else
		OrigCallRef++;
	return (OrigCallRef);
}

void
newl3state(struct l3_process *pc, int state)
{
	if (pc->debug & L3_DEB_STATE)
		l3_debug(pc->st, "newstate cr %d %d --> %d", pc->callref,
			pc->state, state);
	pc->state = state;
}

static void
L3ExpireTimer(struct L3Timer *t)
{
	t->pc->st->lli.l4l3(t->pc->st, t->event, t->pc);
}

void
L3InitTimer(struct l3_process *pc, struct L3Timer *t)
{
	t->pc = pc;
	t->tl.function = (void *) L3ExpireTimer;
	t->tl.data = (long) t;
	init_timer(&t->tl);
}

void
L3DelTimer(struct L3Timer *t)
{
	del_timer(&t->tl);
}

int
L3AddTimer(struct L3Timer *t,
	   int millisec, int event)
{
	if (t->tl.next || t->tl.prev) {
		printk(KERN_WARNING "L3AddTimer: timer already active!\n");
		return -1;
	}
	init_timer(&t->tl);
	t->event = event;
	t->tl.expires = jiffies + (millisec * HZ) / 1000;
	add_timer(&t->tl);
	return 0;
}

void
StopAllL3Timer(struct l3_process *pc)
{
	L3DelTimer(&pc->timer);
}

struct sk_buff *
l3_alloc_skb(int len)
{
	struct sk_buff *skb;

	if (!(skb = alloc_skb(len + MAX_HEADER_LEN, GFP_ATOMIC))) {
		printk(KERN_WARNING "HiSax: No skb for D-channel\n");
		return (NULL);
	}
	skb_reserve(skb, MAX_HEADER_LEN);
	return (skb);
}

static void
no_l3_proto(struct PStack *st, int pr, void *arg)
{
	struct sk_buff *skb = arg;

	HiSax_putstatus(st->l1.hardware, "L3", "no D protocol");
	if (skb) {
		dev_kfree_skb(skb);
	}
}

#ifdef	CONFIG_HISAX_EURO
extern void setstack_dss1(struct PStack *st);
#endif

#ifdef        CONFIG_HISAX_NI1
extern void setstack_ni1(struct PStack *st);
#endif

#ifdef	CONFIG_HISAX_1TR6
extern void setstack_1tr6(struct PStack *st);
#endif

struct l3_process
*getl3proc(struct PStack *st, int cr)
{
	struct l3_process *p = st->l3.proc;

	while (p)
		if (p->callref == cr)
			return (p);
		else
			p = p->next;
	return (NULL);
}

struct l3_process
*new_l3_process(struct PStack *st, int cr)
{
	struct l3_process *p, *np;

	if (!(p = kmalloc(sizeof(struct l3_process), GFP_ATOMIC))) {
		printk(KERN_ERR "HiSax can't get memory for cr %d\n", cr);
		return (NULL);
	}
	if (!st->l3.proc)
		st->l3.proc = p;
	else {
		np = st->l3.proc;
		while (np->next)
			np = np->next;
		np->next = p;
	}
	p->next = NULL;
	p->debug = L3_DEB_WARN;
	p->callref = cr;
	p->state = 0;
	p->chan = NULL;
	p->st = st;
	p->N303 = st->l3.N303;
	L3InitTimer(p, &p->timer);
	return (p);
};

void
release_l3_process(struct l3_process *p)
{
	struct l3_process *np, *pp = NULL;

	if (!p)
		return;
	np = p->st->l3.proc;
	while (np) {
		if (np == p) {
			StopAllL3Timer(p);
			if (pp)
				pp->next = np->next;
			else
				p->st->l3.proc = np->next;
			kfree(p);
			return;
		}
		pp = np;
		np = np->next;
	}
	printk(KERN_ERR "HiSax internal L3 error CR(%d) not in list\n", p->callref);
	l3_debug(p->st, "HiSax internal L3 error CR(%d) not in list", p->callref);
};

void
setstack_l3dc(struct PStack *st, struct Channel *chanp)
{
	char tmp[64];

	st->l3.proc   = NULL;
	st->l3.global = NULL;
	skb_queue_head_init(&st->l3.squeue);
	st->l3.l3m.fsm = &l3fsm;
	st->l3.l3m.state = ST_L3_LC_REL;
	st->l3.l3m.debug = 1;
	st->l3.l3m.userdata = st;
	st->l3.l3m.userint = 0;
	st->l3.l3m.printdebug = l3m_debug;
	strcpy(st->l3.debug_id, "L3DC ");

#ifdef	CONFIG_HISAX_EURO
	if (st->protocol == ISDN_PTYPE_EURO) {
		setstack_dss1(st);
	} else
#endif
#ifdef        CONFIG_HISAX_NI1
	if (st->protocol == ISDN_PTYPE_NI1) {
		setstack_ni1(st);
	} else
#endif
#ifdef	CONFIG_HISAX_1TR6
	if (st->protocol == ISDN_PTYPE_1TR6) {
		setstack_1tr6(st);
	} else
#endif
	if (st->protocol == ISDN_PTYPE_LEASED) {
		st->lli.l4l3 = no_l3_proto;
		st->l2.l2l3 = no_l3_proto;
		printk(KERN_INFO "HiSax: Leased line mode\n");
	} else {
		st->lli.l4l3 = no_l3_proto;
		st->l2.l2l3 = no_l3_proto;
		sprintf(tmp, "protocol %s not supported",
			(st->protocol == ISDN_PTYPE_1TR6) ? "1tr6" :
			(st->protocol == ISDN_PTYPE_EURO) ? "euro" :
			(st->protocol == ISDN_PTYPE_NI1) ? "ni1" :
			"unknown");
		printk(KERN_WARNING "HiSax: %s\n", tmp);
		st->protocol = -1;
	}
}

void
isdnl3_trans(struct PStack *st, int pr, void *arg) {
	st->l3.l3l2(st, pr, arg);
}

void
releasestack_isdnl3(struct PStack *st)
{
	while (st->l3.proc)
		release_l3_process(st->l3.proc);
	if (st->l3.global) {
		StopAllL3Timer(st->l3.global);
		kfree(st->l3.global);
		st->l3.global = NULL;
	}
	discard_queue(&st->l3.squeue);
}

void
setstack_l3bc(struct PStack *st, struct Channel *chanp)
{

	st->l3.proc   = NULL;
	st->l3.global = NULL;
	skb_queue_head_init(&st->l3.squeue);
	st->l3.l3m.fsm = &l3fsm;
	st->l3.l3m.state = ST_L3_LC_REL;
	st->l3.l3m.debug = 1;
	st->l3.l3m.userdata = st;
	st->l3.l3m.userint = 0;
	st->l3.l3m.printdebug = l3m_debug;
	strcpy(st->l3.debug_id, "L3BC ");
	st->lli.l4l3 = isdnl3_trans;
}

static void
lc_activate(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	FsmChangeState(fi, ST_L3_LC_ESTAB_WAIT);
	st->l3.l3l2(st, DL_ESTABLISH | REQUEST, NULL);
}

static void
lc_connect(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;

	FsmChangeState(fi, ST_L3_LC_ESTAB);
	while ((skb = skb_dequeue(&st->l3.squeue))) {
		st->l3.l3l2(st, DL_DATA | REQUEST, skb);
	}
	st->l3.l3l4(st, DL_ESTABLISH | INDICATION, NULL);
}

static void
lc_release_req(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	if (fi->state == ST_L3_LC_ESTAB_WAIT)
		FsmChangeState(fi, ST_L3_LC_REL);
	else
		FsmChangeState(fi, ST_L3_LC_REL_WAIT);
	st->l3.l3l2(st, DL_RELEASE | REQUEST, NULL);
}

static void
lc_release_ind(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	FsmChangeState(fi, ST_L3_LC_REL);
	discard_queue(&st->l3.squeue);
	st->l3.l3l4(st, DL_RELEASE | INDICATION, NULL);
}

/* *INDENT-OFF* */
static struct FsmNode L3FnList[] HISAX_INITDATA =
{
	{ST_L3_LC_REL,		EV_ESTABLISH_REQ,	lc_activate},
	{ST_L3_LC_REL,		EV_ESTABLISH_IND,	lc_connect},
	{ST_L3_LC_REL,		EV_ESTABLISH_CNF,	lc_connect},
	{ST_L3_LC_ESTAB_WAIT,	EV_ESTABLISH_CNF,	lc_connect},
	{ST_L3_LC_ESTAB_WAIT,	EV_RELEASE_REQ,		lc_release_req},
	{ST_L3_LC_ESTAB_WAIT,	EV_RELEASE_IND,		lc_release_ind},
	{ST_L3_LC_ESTAB,	EV_RELEASE_IND,		lc_release_ind},
	{ST_L3_LC_ESTAB,	EV_RELEASE_REQ,		lc_release_req},
	{ST_L3_LC_REL_WAIT,	EV_RELEASE_CNF,		lc_release_ind},
	{ST_L3_LC_REL_WAIT,	EV_ESTABLISH_REQ,	lc_activate},
};
/* *INDENT-ON* */

#define L3_FN_COUNT (sizeof(L3FnList)/sizeof(struct FsmNode))

void
l3_msg(struct PStack *st, int pr, void *arg)
{
	
	switch (pr) {
		case (DL_DATA | REQUEST):
			if (st->l3.l3m.state == ST_L3_LC_ESTAB) {
				st->l3.l3l2(st, pr, arg);
			} else {
				struct sk_buff *skb = arg;

				skb_queue_head(&st->l3.squeue, skb);
				FsmEvent(&st->l3.l3m, EV_ESTABLISH_REQ, NULL); 
			}
			break;
		case (DL_ESTABLISH | REQUEST):
			FsmEvent(&st->l3.l3m, EV_ESTABLISH_REQ, NULL);
			break;
		case (DL_ESTABLISH | CONFIRM):
			FsmEvent(&st->l3.l3m, EV_ESTABLISH_CNF, NULL);
			break;
		case (DL_ESTABLISH | INDICATION):
			FsmEvent(&st->l3.l3m, EV_ESTABLISH_IND, NULL);
			break;
		case (DL_RELEASE | INDICATION):
			FsmEvent(&st->l3.l3m, EV_RELEASE_IND, NULL);
			break;
		case (DL_RELEASE | CONFIRM):
			FsmEvent(&st->l3.l3m, EV_RELEASE_CNF, NULL);
			break;
		case (DL_RELEASE | REQUEST):
			FsmEvent(&st->l3.l3m, EV_RELEASE_REQ, NULL);
			break;
	}
}

HISAX_INITFUNC(void
Isdnl3New(void))
{
	l3fsm.state_count = L3_STATE_COUNT;
	l3fsm.event_count = L3_EVENT_COUNT;
	l3fsm.strEvent = strL3Event;
	l3fsm.strState = strL3State;
	FsmNew(&l3fsm, L3FnList, L3_FN_COUNT);
}

void
Isdnl3Free(void)
{
	FsmFree(&l3fsm);
}
