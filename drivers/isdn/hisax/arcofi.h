/* $Id: arcofi.h,v 1.1 1997/10/29 18:51:20 keil Exp $

 * arcofi.h   Ansteuerung ARCOFI 2165
 *
 * Author     Karsten Keil (keil@temic-ech.spacenet.de)
 *
 *
 *
 * $Log: arcofi.h,v $
 * Revision 1.1  1997/10/29 18:51:20  keil
 * New files
 *
 */
 
#define ARCOFI_USE	1

extern int send_arcofi(struct IsdnCardState *cs, const u_char *msg);
