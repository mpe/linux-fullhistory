/* $Id: isdnl1.h,v 1.3 1996/12/08 19:41:55 keil Exp $
 *
 * $Log: isdnl1.h,v $
 * Revision 1.3  1996/12/08 19:41:55  keil
 * L2FRAME_DEBUG
 *
 * Revision 1.2  1996/10/27 22:26:27  keil
 * ISAC/HSCX version functions
 *
 * Revision 1.1  1996/10/13 20:03:47  keil
 * Initial revision
 *
 *
 *
 */


#define L2FRAME_DEBUG

/* DEBUG Level */

#define	L1_DEB_WARN		0x01
#define	L1_DEB_INTSTAT		0x02
#define	L1_DEB_ISAC		0x04
#define	L1_DEB_ISAC_FIFO	0x08
#define	L1_DEB_HSCX		0x10
#define	L1_DEB_HSCX_FIFO	0x20
#define	L1_DEB_LAPD	        0x40


#define ISAC_RCVBUFREADY 0
#define ISAC_XMTBUFREADY 1
#define ISAC_PHCHANGE    2

#define HSCX_RCVBUFREADY 0
#define HSCX_XMTBUFREADY 1

extern void debugl1(struct IsdnCardState *sp, char *msg);
extern char *HscxVersion(byte v);
extern char *ISACVersion(byte v);
extern void hscx_sched_event(struct HscxState *hsp, int event);
extern void isac_sched_event(struct IsdnCardState *sp, int event);
extern void isac_new_ph(struct IsdnCardState *sp);
extern get_irq(int cardnr, void *routine);
extern void Logl2Frame(struct IsdnCardState *sp, struct BufHeader *ibh, char *buf, int dir);
