/* $Id: isdnl3.c,v 1.6 1997/02/16 01:04:08 fritz Exp $

 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *              based on the teles driver from Jan den Ouden
 *
 * Thanks to    Jan den Ouden
 *              Fritz Elfert
 *
 * $Log: isdnl3.c,v $
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

const char *l3_revision = "$Revision: 1.6 $";

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

static void
no_l3_proto(struct PStack *st, int pr, void *arg)
{
	struct BufHeader *ibh = arg;

	l3_debug(st, "no protocol");
	if (ibh)
		BufPoolRelease(ibh);
}

#ifdef	CONFIG_HISAX_EURO
extern void setstack_dss1(struct PStack *st);
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
		sprintf(tmp, "protocol %s not supported",
			(st->protocol == ISDN_PTYPE_1TR6) ? "1tr6" :
			(st->protocol == ISDN_PTYPE_EURO) ? "euro" :
			"unknown");
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
