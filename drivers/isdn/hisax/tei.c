/* $Id: tei.c,v 1.6 1997/02/09 00:25:12 keil Exp $

 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *              based on the teles driver from Jan den Ouden
 *
 * Thanks to    Jan den Ouden
 *              Fritz Elfert
 *
 * $Log: tei.c,v $
 * Revision 1.6  1997/02/09 00:25:12  keil
 * new interface handling, one interface per card
 *
 * Revision 1.5  1997/01/27 15:57:51  keil
 * cosmetics
 *
 * Revision 1.4  1997/01/21 22:32:44  keil
 * Tei verify request
 *
 * Revision 1.3  1997/01/04 13:45:02  keil
 * cleanup,adding remove tei request (thanks to Sim Yskes)
 *
 * Revision 1.2  1996/12/08 19:52:39  keil
 * minor debug fix
 *
 * Revision 1.1  1996/10/13 20:04:57  keil
 * Initial revision
 *
 *
 *
 */
#define __NO_VERSION__
#include "hisax.h"

extern struct IsdnCard cards[];
extern int nrcards;

const char *tei_revision = "$Revision: 1.6 $";

static struct PStack *
findces(struct PStack *st, int ces)
{
	struct PStack *ptr = *(st->l1.stlistp);

	while (ptr)
		if (ptr->l2.ces == ces)
			return (ptr);
		else
			ptr = ptr->next;
	return (NULL);
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
mdl_unit_data_res(struct PStack *st, unsigned int ri, byte mt, byte ai)
{
	struct BufHeader *ibh;
	byte *bp;

	if (BufPoolGet(&ibh, st->l1.smallpool, GFP_ATOMIC, (void *) st, 7))
		return;
	bp = DATAPTR(ibh);
	bp += 3;
	bp[0] = 0xf;
	bp[1] = ri >> 8;
	bp[2] = ri & 0xff;
	bp[3] = mt;
	bp[4] = (ai << 1) | 1;
	ibh->datasize = 8;
	st->l3.l3l2(st, DL_UNIT_DATA, ibh);
}

static void
mdl_unit_data_ind(struct PStack *st, unsigned int ri, byte mt, byte ai)
{
	unsigned int tces;
	struct PStack *otsp, *ptr;
	char tmp[64];

	switch (mt) {
		case (2):
			tces = ri;
			if (st->l3.debug) {
				sprintf(tmp, "identity assign ces %d ai %d", tces, ai);
				st->l2.l2m.printdebug(&st->l2.l2m, tmp);
			}
			if ((otsp = findces(st, tces))) {
				if (st->l3.debug) {
					sprintf(tmp, "ces %d --> tei %d", tces, ai);
					st->l2.l2m.printdebug(&st->l2.l2m, tmp);
				}
				otsp->ma.teil2(otsp, MDL_ASSIGN, (void *) (int) ai);
			}
			break;
		case (3):
			tces = ri;
			if (st->l3.debug) {
				sprintf(tmp, "identity denied for ces %d ai %d", tces, ai);
				st->l2.l2m.printdebug(&st->l2.l2m, tmp);
			}
			if ((otsp = findces(st, tces))) {
				if (st->l3.debug) {
					sprintf(tmp, "ces %d denied tei %d", tces, ai);
					st->l2.l2m.printdebug(&st->l2.l2m, tmp);
				}
				otsp->l2.tei = 255;
				otsp->l2.ces = randomces();
				otsp->ma.teil2(otsp, MDL_REMOVE, 0);
			}
			break;
		case (4):
			if (st->l3.debug) {
				sprintf(tmp, "checking identity for %d", ai);
				st->l2.l2m.printdebug(&st->l2.l2m, tmp);
			}
			if (ai == 0x7f) {
				ptr = *(st->l1.stlistp);
				while (ptr) {
					if ((ptr->l2.tei & 0x7f) != 0x7f) {
						if (st->l3.debug) {
							sprintf(tmp, "check response for ces %d with tei %d",
								ptr->l2.ces, ptr->l2.tei);
							st->l2.l2m.printdebug(&st->l2.l2m, tmp);
						}
						/* send identity check response (user->network) */
						mdl_unit_data_res(st, ptr->l2.ces, 5, ptr->l2.tei);
					}
					ptr = ptr->next;
				}
			} else {
				otsp = findtei(st, ai);
				if (!otsp)
					break;
				if (st->l3.debug) {
					sprintf(tmp, "check response for ces %d with tei %d",
					     otsp->l2.ces, otsp->l2.tei);
					st->l2.l2m.printdebug(&st->l2.l2m, tmp);
				}
				/* send identity check response (user->network) */
				mdl_unit_data_res(st, otsp->l2.ces, 5, otsp->l2.tei);
			}
			break;
		case (6):
			if (st->l3.debug) {
				sprintf(tmp, "removal for %d", ai);
				st->l2.l2m.printdebug(&st->l2.l2m, tmp);
			}
			if (ai == 0x7f) {
				ptr = *(st->l1.stlistp);
				while (ptr) {
					if ((ptr->l2.tei & 0x7f) != 0x7f) {
						if (st->l3.debug) {
							sprintf(tmp, "rem ces %d with tei %d",
								ptr->l2.ces, ptr->l2.tei);
							st->l2.l2m.printdebug(&st->l2.l2m, tmp);
						}
						ptr->ma.teil2(ptr, MDL_REMOVE, 0);
					}
					ptr = ptr->next;
				}
			} else {
				otsp = findtei(st, ai);
				if (!otsp)
					break;
				if (st->l3.debug) {
					sprintf(tmp, "rem ces %d with tei %d",
					     otsp->l2.ces, otsp->l2.tei);
					st->l2.l2m.printdebug(&st->l2.l2m, tmp);
				}
				otsp->ma.teil2(otsp, MDL_REMOVE, 0);
			}
			break;
		default:
			if (st->l3.debug) {
				sprintf(tmp, "message unknown %d ai %d", mt, ai);
				st->l2.l2m.printdebug(&st->l2.l2m, tmp);
			}
	}
}

void
tei_handler(struct PStack *st,
	    byte pr, struct BufHeader *ibh)
{
	byte *bp;
	unsigned int data;
	char tmp[32];

	switch (pr) {
		case (MDL_ASSIGN):
			data = (unsigned int) ibh;
			if (st->l3.debug) {
				sprintf(tmp, "ces %d assign request", data);
				st->l2.l2m.printdebug(&st->l2.l2m, tmp);
			}
			mdl_unit_data_res(st, data, 1, 127);
			break;
		case (MDL_VERIFY):
			data = (unsigned int) ibh;
			if (st->l3.debug) {
				sprintf(tmp, "%d id verify request", data);
				st->l2.l2m.printdebug(&st->l2.l2m, tmp);
			}
			mdl_unit_data_res(st, 0, 7, data);
			break;
		case (DL_UNIT_DATA):
			bp = DATAPTR(ibh);
			bp += 3;
			if (bp[0] != 0xf) {
				/* wrong management entity identifier, ignore */
				/* shouldn't ibh be released??? */
				printk(KERN_WARNING "tei handler wrong entity id %x\n", bp[0]);
				BufPoolRelease(ibh);
				break;
			}
			mdl_unit_data_ind(st, (bp[1] << 8) | bp[2], bp[3], bp[4] >> 1);
			BufPoolRelease(ibh);
			break;
		default:
			break;
	}
}

unsigned int
randomces(void)
{
	int x = jiffies & 0xffff;

	return (x);
}

static void
tei_man(struct PStack *sp, int i, void *v)
{

	printk(KERN_DEBUG "tei_man\n");
}

static void
tei_l2tei(struct PStack *st, int pr, void *arg)
{
	struct IsdnCardState *sp = st->l1.hardware;

	tei_handler(sp->teistack, pr, arg);
}

void
setstack_tei(struct PStack *st)
{
	st->l2.l2tei = tei_l2tei;
}

void
init_tei(struct IsdnCardState *sp, int protocol)
{
	struct PStack *st;
	char tmp[128];

	st = (struct PStack *) Smalloc(sizeof(struct PStack), GFP_KERNEL,
				       "struct PStack");
	setstack_HiSax(st, sp);
	st->l2.extended = !0;
	st->l2.laptype = LAPD;
	st->l2.window = 1;
	st->l2.orig = !0;
	st->protocol = protocol;
/*
 * the following is not necessary for tei mng. (broadcast only)
 */

	st->l2.t200 = 500;	/* 500 milliseconds */
	st->l2.n200 = 4;	/* try 4 times */

	st->l2.sap = 63;
	st->l2.tei = 127;

	sprintf(tmp, "Card %d tei", sp->cardnr + 1);
	setstack_isdnl2(st, tmp);
	st->l2.debug = 0;
	st->l3.debug = 0;

	st->ma.manl2(st, MDL_NOTEIPROC, NULL);

	st->l2.l2l3 = (void *) tei_handler;
	st->l1.l1man = tei_man;
	st->l2.l2man = tei_man;
	st->l4.l2writewakeup = NULL;

	HiSax_addlist(sp, st);
	sp->teistack = st;
}

void
release_tei(struct IsdnCardState *sp)
{
	struct PStack *st = sp->teistack;

	HiSax_rmlist(sp, st);
	Sfree((void *) st);
}
