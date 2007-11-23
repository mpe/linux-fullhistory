/* $Id: ipac.h,v 1.4 1999/12/23 15:09:32 keil Exp $

 * ipac.h   IPAC specific defines
 *
 * Author       Karsten Keil (keil@isdn4linux.de)
 *
 *
 * $Log: ipac.h,v $
 * Revision 1.4  1999/12/23 15:09:32  keil
 * change email
 *
 * Revision 1.3  1998/04/15 16:48:09  keil
 * IPAC_ATX added
 *
 * Revision 1.2  1997/10/29 18:51:21  keil
 * New files
 *
 * Revision 1.1.2.1  1997/10/17 22:10:48  keil
 * new files on 2.0
 *
 *
 *
 */


/* All Registers original Siemens Spec  */

#define IPAC_CONF	0xC0
#define IPAC_MASK	0xC1
#define IPAC_ISTA	0xC1
#define IPAC_ID		0xC2
#define IPAC_ACFG	0xC3
#define IPAC_AOE	0xC4
#define IPAC_ARX	0xC5
#define IPAC_ATX	0xC5
#define IPAC_PITA1	0xC6
#define IPAC_PITA2	0xC7
#define IPAC_POTA1	0xC8
#define IPAC_POTA2	0xC9
#define IPAC_PCFG	0xCA
#define IPAC_SCFG	0xCB
#define IPAC_TIMR2	0xCC
