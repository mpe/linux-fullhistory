/* $Id: arcofi.h,v 1.4 1999/07/01 08:11:18 keil Exp $

 * arcofi.h   Ansteuerung ARCOFI 2165
 *
 * Author     Karsten Keil (keil@temic-ech.spacenet.de)
 *
 *
 *
 * $Log: arcofi.h,v $
 * Revision 1.4  1999/07/01 08:11:18  keil
 * Common HiSax version for 2.0, 2.1, 2.2 and 2.3 kernel
 *
 * Revision 1.3  1998/05/25 12:57:39  keil
 * HiSax golden code from certification, Don't use !!!
 * No leased lines, no X75, but many changes.
 *
 * Revision 1.2  1998/04/15 16:47:17  keil
 * new interface
 *
 * Revision 1.1  1997/10/29 18:51:20  keil
 * New files
 *
 */
 
#define ARCOFI_USE	1

/* states */
#define ARCOFI_NOP	0
#define ARCOFI_TRANSMIT	1
#define ARCOFI_RECEIVE	2
/* events */
#define ARCOFI_START	1
#define ARCOFI_TX_END	2
#define ARCOFI_RX_END	3
#define ARCOFI_TIMEOUT	4

extern int arcofi_fsm(struct IsdnCardState *cs, int event, void *data);
extern void init_arcofi(struct IsdnCardState *cs);
extern void clear_arcofi(struct IsdnCardState *cs);
