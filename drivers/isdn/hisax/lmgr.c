/* $Id: lmgr.c,v 1.6 1999/07/01 08:12:04 keil Exp $

 * Author       Karsten Keil (keil@isdn4linux.de)
 *
 *
 *  Layermanagement module
 *
 * $Log: lmgr.c,v $
 * Revision 1.6  1999/07/01 08:12:04  keil
 * Common HiSax version for 2.0, 2.1, 2.2 and 2.3 kernel
 *
 * Revision 1.5  1998/11/15 23:55:12  keil
 * changes from 2.0
 *
 * Revision 1.4  1998/05/25 12:58:19  keil
 * HiSax golden code from certification, Don't use !!!
 * No leased lines, no X75, but many changes.
 *
 * Revision 1.3  1998/03/07 22:57:06  tsbogend
 * made HiSax working on Linux/Alpha
 *
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
			st->l2.l2tei(st, MDL_ERROR | REQUEST, NULL);
			break;
	}
}

static void
hisax_manager(struct PStack *st, int pr, void *arg)
{
	long Code;

	switch (pr) {
		case (MDL_ERROR | INDICATION):
			Code = (long) arg;
			HiSax_putstatus(st->l1.hardware, "manager: MDL_ERROR",
				" %c %s", (char)Code, 
				test_bit(FLG_LAPD, &st->l2.flag) ?
				"D-channel" : "B-channel");
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
