/* $Id: isdnl1.h,v 1.4 1997/04/06 22:55:52 keil Exp $
 *
 * $Log: isdnl1.h,v $
 * Revision 1.4  1997/04/06 22:55:52  keil
 * Using SKB's
 *
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
extern char *HscxVersion(u_char v);
extern char *ISACVersion(u_char v);
extern void hscx_sched_event(struct HscxState *hsp, int event);
extern void isac_sched_event(struct IsdnCardState *sp, int event);
extern void isac_new_ph(struct IsdnCardState *sp);
extern int get_irq(int cardnr, void *routine);

#ifdef L2FRAME_DEBUG
extern void Logl2Frame(struct IsdnCardState *sp, struct sk_buff *skb, char *buf, int dir);
#endif
