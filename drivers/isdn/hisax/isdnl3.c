/* $Id: isdnl3.c,v 2.5 1998/02/12 23:07:52 keil Exp $

 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *              based on the teles driver from Jan den Ouden
 *
 * Thanks to    Jan den Ouden
 *              Fritz Elfert
 *
 * $Log: isdnl3.c,v $
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

const char *l3_revision = "$Revision: 2.5 $";

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
l3_debug(struct PStack *st, char *s)
{
	char str[256], tm[32];

	jiftime(tm, jiffies);
	sprintf(str, "%s l3 %s\n", tm, s);
	HiSax_putstatus(st->l1.hardware, str);
}

void
newl3state(struct l3_process *pc, int state)
{
	char tmp[80];

	if (pc->debug & L3_DEB_STATE) {
		sprintf(tmp, "newstate cr %d %d --> %d", pc->callref,
			pc->state, state);
		l3_debug(pc->st, tmp);
	}
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

	HiSax_putstatus(st->l1.hardware, "L3 no D protocol\n");
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
	printk(KERN_ERR "HiSax internal L3 error CR not in list\n");
};

void
setstack_isdnl3(struct PStack *st, struct Channel *chanp)
{
	char tmp[64];

	st->l3.proc   = NULL;
	st->l3.global = NULL;

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
releasestack_isdnl3(struct PStack *st)
{
	while (st->l3.proc)
		release_l3_process(st->l3.proc);
	if (st->l3.global) {
		StopAllL3Timer(st->l3.global);
		kfree(st->l3.global);
		st->l3.global = NULL;
	}
}
