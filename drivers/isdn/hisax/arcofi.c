/* $Id: arcofi.c,v 1.6 1998/09/30 22:21:56 keil Exp $

 * arcofi.c   Ansteuerung ARCOFI 2165
 *
 * Author     Karsten Keil (keil@temic-ech.spacenet.de)
 *
 *
 *
 * $Log: arcofi.c,v $
 * Revision 1.6  1998/09/30 22:21:56  keil
 * cosmetics
 *
 * Revision 1.5  1998/09/27 12:52:57  keil
 * cosmetics
 *
 * Revision 1.4  1998/08/20 13:50:24  keil
 * More support for hybrid modem (not working yet)
 *
 * Revision 1.3  1998/05/25 12:57:38  keil
 * HiSax golden code from certification, Don't use !!!
 * No leased lines, no X75, but many changes.
 *
 * Revision 1.2  1998/04/15 16:47:16  keil
 * new interface
 *
 * Revision 1.1  1997/10/29 18:51:20  keil
 * New files
 *
 */
 
#define __NO_VERSION__
#include "hisax.h"
#include "isdnl1.h"
#include "isac.h"

int
send_arcofi(struct IsdnCardState *cs, const u_char *msg, int bc, int receive) {
	u_char val;
	long flags;
	int cnt=30;
	
	cs->mon_txp = 0;
	cs->mon_txc = msg[0];
	memcpy(cs->mon_tx, &msg[1], cs->mon_txc);
	switch(bc) {
		case 0: break;
		case 1: cs->mon_tx[1] |= 0x40;
			break;
		default: break;
	}
	cs->mocr &= 0x0f;
	cs->mocr |= 0xa0;
	test_and_clear_bit(HW_MON1_TX_END, &cs->HW_Flags);
	if (receive)
		test_and_clear_bit(HW_MON1_RX_END, &cs->HW_Flags);
	cs->writeisac(cs, ISAC_MOCR, cs->mocr);
	val = cs->readisac(cs, ISAC_MOSR);
	cs->writeisac(cs, ISAC_MOX1, cs->mon_tx[cs->mon_txp++]);
	cs->mocr |= 0x10;
	cs->writeisac(cs, ISAC_MOCR, cs->mocr);
	save_flags(flags);
	sti();
	while (cnt && !test_bit(HW_MON1_TX_END, &cs->HW_Flags)) {
		cnt--;
		udelay(500);
	}
	if (receive) {
		while (cnt && !test_bit(HW_MON1_RX_END, &cs->HW_Flags)) {
			cnt--;
			udelay(500);
		}
	}
	restore_flags(flags);
	if (cnt <= 0) {
		printk(KERN_WARNING"HiSax arcofi monitor timed out\n");
		debugl1(cs, "HiSax arcofi monitor timed out");
	}
	return(cnt);	
}
