/* $Id: isdnl1.h,v 2.8 1998/11/15 23:54:59 keil Exp $

 * $Log: isdnl1.h,v $
 * Revision 2.8  1998/11/15 23:54:59  keil
 * changes from 2.0
 *
 * Revision 2.7  1998/09/30 22:21:55  keil
 * cosmetics
 *
 * Revision 2.6  1998/05/25 12:58:06  keil
 * HiSax golden code from certification, Don't use !!!
 * No leased lines, no X75, but many changes.
 *
 * Revision 2.5  1998/02/02 13:36:58  keil
 * more debug
 *
 * Revision 2.4  1997/11/08 21:35:49  keil
 * new l1 init
 *
 * Revision 2.3  1997/10/29 19:07:53  keil
 * changes for 2.1
 *
 * Revision 2.2  1997/07/30 17:11:09  keil
 * L1deactivated exported
 *
 * Revision 2.1  1997/07/27 21:43:58  keil
 * new l1 interface
 *
 * Revision 2.0  1997/06/26 11:02:55  keil
 * New Layer and card interface
 *
 *
 */

#define D_RCVBUFREADY	0
#define D_XMTBUFREADY	1
#define D_L1STATECHANGE	2
#define D_CLEARBUSY	3
#define D_RX_MON0	4
#define D_RX_MON1	5
#define D_TX_MON0	6
#define D_TX_MON1	7

#define B_RCVBUFREADY 0
#define B_XMTBUFREADY 1

extern void debugl1(struct IsdnCardState *cs, char *fmt, ...);
extern void DChannel_proc_xmt(struct IsdnCardState *cs);
extern void DChannel_proc_rcv(struct IsdnCardState *cs);
extern void l1_msg(struct IsdnCardState *cs, int pr, void *arg);
extern void l1_msg_b(struct PStack *st, int pr, void *arg);

#ifdef L2FRAME_DEBUG
extern void Logl2Frame(struct IsdnCardState *cs, struct sk_buff *skb, char *buf, int dir);
#endif
