/* $Id: teles0.h,v 1.2 1997/01/21 22:26:52 keil Exp $
 *
 * teles0.h   Header for Teles 16.0 8.0 & compatible
 *
 * Author	Karsten Keil (keil@temic-ech.spacenet.de)
 *
 *
 * $Log: teles0.h,v $
 * Revision 1.2  1997/01/21 22:26:52  keil
 * cleanups
 *
 * Revision 1.1  1996/10/13 20:03:48  keil
 * Initial revision
 *
 *
*/

extern	void teles0_report(struct IsdnCardState *sp);
extern  void release_io_teles0(struct IsdnCard *card);
extern	int  setup_teles0(struct IsdnCard *card);
extern  int  initteles0(struct IsdnCardState *sp);
