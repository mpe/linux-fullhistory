/* $Id: lmgr.c,v 1.2 1997/10/29 19:09:34 keil Exp $

 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *
 *
 *  Layermanagement module
 *
 * $Log: lmgr.c,v $
 * Revision 1.2  1997/10/29 19:09:34  keil
 * new L1
 *
 * Revision 1.1  1997/06/26 11:17:25  keil
 * first version
 *
 *
 */

#define __NO_VERSION__
#include "hisax.h"

static void
error_handling_dchan(struct PStack *st, int Error)
{
	switch (Error) {
		case 'C':
		case 'D':
		case 'G':
		case 'H':
			st->l2.l2tei(st, MDL_ERROR_REQ, NULL);
			break;
	}
}

static void
hisax_manager(struct PStack *st, int pr, void *arg)
{
	char tm[32], str[256];
	int Code;

	switch (pr) {
		case MDL_ERROR_IND:
			Code = (int) arg;
			jiftime(tm, jiffies);
			sprintf(str, "%s manager: MDL_ERROR %c %s\n", tm,
				Code, test_bit(FLG_LAPD, &st->l2.flag) ?
				"D-channel" : "B-channel");
			HiSax_putstatus(st->l1.hardware, str);
			if (test_bit(FLG_LAPD, &st->l2.flag))
				error_handling_dchan(st, Code);
			break;
	}
}

void
setstack_manager(struct PStack *st)
{
	st->ma.layer = hisax_manager;
}
