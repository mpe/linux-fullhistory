/* $Id: avm_a1.h,v 1.2 1997/01/21 22:14:36 keil Exp $
 *
 * avm_a1.h   Header for AVM A1 (Fritz) ISDN card
 *
 * Author	Karsten Keil (keil@temic-ech.spacenet.de)
 *
 *
 * $Log: avm_a1.h,v $
 * Revision 1.2  1997/01/21 22:14:36  keil
 * cleanups
 *
 * Revision 1.1  1996/10/12 21:42:40  keil
 * Initial revision
 *
 *
*/

#define	 AVM_A1_STAT_ISAC	0x01
#define	 AVM_A1_STAT_HSCX	0x02
#define	 AVM_A1_STAT_TIMER	0x04

extern	void avm_a1_report(struct IsdnCardState *sp);
extern  void release_io_avm_a1(struct IsdnCard *card);
extern	int  setup_avm_a1(struct IsdnCard *card);
extern  int  initavm_a1(struct IsdnCardState *sp);
