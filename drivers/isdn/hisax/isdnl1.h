/* $Id: isdnl1.h,v 2.5 1998/02/02 13:36:58 keil Exp $

 * $Log: isdnl1.h,v $
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


#define L2FRAME_DEBUG

/* DEBUG Level */

#define	L1_DEB_WARN		0x01
#define	L1_DEB_INTSTAT		0x02
#define	L1_DEB_ISAC		0x04
#define	L1_DEB_ISAC_FIFO	0x08
#define	L1_DEB_HSCX		0x10
#define	L1_DEB_HSCX_FIFO	0x20
#define	L1_DEB_LAPD	        0x40
#define	L1_DEB_IPAC	        0x80
#define	L1_DEB_RECEIVE_FRAME    0x100

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

extern void debugl1(struct IsdnCardState *sp, char *msg);
extern void DChannel_proc_xmt(struct IsdnCardState *cs);
extern void DChannel_proc_rcv(struct IsdnCardState *cs);


#ifdef L2FRAME_DEBUG
extern void Logl2Frame(struct IsdnCardState *sp, struct sk_buff *skb, char *buf, int dir);
#endif
