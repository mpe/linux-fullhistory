/* $Id: isdnl3.c,v 1.10 1997/04/06 22:54:16 keil Exp $

 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *              based on the teles driver from Jan den Ouden
 *
 * Thanks to    Jan den Ouden
 *              Fritz Elfert
 *
 * $Log: isdnl3.c,v $
 * Revision 1.10  1997/04/06 22:54:16  keil
 * Using SKB's
 *
 * Revision 1.9  1997/03/25 23:11:25  keil
 * US NI-1 protocol
 *
 * Revision 1.8  1997/03/21 18:53:44  keil
 * Report no protocol error to syslog too
 *
 * Revision 1.7  1997/03/17 18:34:38  keil
 * fixed oops if no protocol selected during config
 *
 * Revision 1.6  1997/02/16 01:04:08  fritz
 * Bugfix: Changed timer handling caused hang with 2.1.X
 *
 * Revision 1.5  1997/02/09 00:26:27  keil
 * new interface handling, one interface per card
 * leased line changes
 *
 * Revision 1.4  1997/01/27 23:17:44  keil
 * delete timers while unloading
 *
 * Revision 1.3  1997/01/21 22:31:12  keil
 * new statemachine; L3 timers
 *
 * Revision 1.2  1996/11/05 19:42:04  keil
 * using config.h
 *
 * Revision 1.1  1996/10/13 20:04:54  keil
 * Initial revision
 *
 *
 *
 */
#define __NO_VERSION__
#include "hisax.h"
#include "isdnl3.h"
#include <linux/config.h>

const char *l3_revision = "$Revision: 1.10 $";

void
l3_debug(struct PStack *st, char *s)
{
	char str[256], tm[32];

	jiftime(tm, jiffies);
	sprintf(str, "%s Channel %d l3 %s\n", tm, st->l3.channr, s);
	HiSax_putstatus(st->l1.hardware, str);
}



void
newl3state(struct PStack *st, int state)
{
	char tmp[80];

	if (st->l3.debug & L3_DEB_STATE) {
		sprintf(tmp, "newstate  %d --> %d", st->l3.state, state);
		l3_debug(st, tmp);
	}
	st->l3.state = state;
}

static void
L3ExpireTimer(struct L3Timer *t)
{
	t->st->l4.l4l3(t->st, t->event, NULL);
}

void
L3InitTimer(struct PStack *st, struct L3Timer *t)
{
	t->st = st;
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
StopAllL3Timer(struct PStack *st)
{
	L3DelTimer(&st->l3.timer);
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

	l3_debug(st, "no protocol");
	if (skb) {
		SET_SKB_FREE(skb);
		dev_kfree_skb(skb, FREE_READ);
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

void
setstack_isdnl3(struct PStack *st, struct Channel *chanp)
{
	char tmp[64];

	st->l3.debug = L3_DEB_WARN;
	st->l3.channr = chanp->chan;
	L3InitTimer(st, &st->l3.timer);

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
		st->l4.l4l3 = no_l3_proto;
		st->l2.l2l3 = no_l3_proto;
		printk(KERN_NOTICE "HiSax: Leased line mode\n");
	} else {
		st->l4.l4l3 = no_l3_proto;
		st->l2.l2l3 = no_l3_proto;
		sprintf(tmp, "protocol %s not supported",
			(st->protocol == ISDN_PTYPE_1TR6) ? "1tr6" :
			(st->protocol == ISDN_PTYPE_EURO) ? "euro" :
			(st->protocol == ISDN_PTYPE_NI1) ? "ni1" :
			"unknown");
		printk(KERN_WARNING "HiSax: %s\n", tmp);
		l3_debug(st, tmp);
		st->protocol = -1;
	}
	st->l3.state = 0;
	st->l3.callref = 0;
}

void
releasestack_isdnl3(struct PStack *st)
{
	StopAllL3Timer(st);
}
