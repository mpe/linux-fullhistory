/* $Id: config.c,v 1.1 1996/04/13 10:23:11 fritz Exp $
 *
 * $Log: config.c,v $
 * Revision 1.1  1996/04/13 10:23:11  fritz
 * Initial revision
 *
 *
 */
#define __NO_VERSION__
#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/timer.h>
#include "teles.h"

/*
 * This structure array contains one entry per card. An entry looks
 * like this:
 * 
 * { membase,irq,portbase,protocol,NULL }
 *
 * protocol can be either ISDN_PTYPE_EURO or ISDN_PTYPE_1TR6
 *
 * Cards which don't have an io port (Teles 8 bit cards for
 * example) can be entered with io port 0x0
 *
 * For the Teles 16.3, membase has to be set to 0.
 *
 */

struct IsdnCard cards[] =
{
	{(byte *) 0xd0000, 15, 0xd80, ISDN_PTYPE_EURO, NULL},	/* example */
	{NULL, 0, 0, 0, NULL},
	{NULL, 0, 0, 0, NULL},
	{NULL, 0, 0, 0, NULL},
	{NULL, 0, 0, 0, NULL},
	{NULL, 0, 0, 0, NULL},
	{NULL, 0, 0, 0, NULL},
	{NULL, 0, 0, 0, NULL},
	{NULL, 0, 0, 0, NULL},
	{NULL, 0, 0, 0, NULL},
	{NULL, 0, 0, 0, NULL},
	{NULL, 0, 0, 0, NULL},
	{NULL, 0, 0, 0, NULL},
	{NULL, 0, 0, 0, NULL},
	{NULL, 0, 0, 0, NULL},
	{NULL, 0, 0, 0, NULL},
};
