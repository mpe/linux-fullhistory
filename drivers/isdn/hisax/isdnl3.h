/* $Id: isdnl3.h,v 2.5 1999/07/25 16:18:32 keil Exp $

 * $Log: isdnl3.h,v $
 * Revision 2.5  1999/07/25 16:18:32  keil
 * Fix Suspend/Resume
 *
 * Revision 2.4  1999/07/01 08:11:54  keil
 * Common HiSax version for 2.0, 2.1, 2.2 and 2.3 kernel
 *
 * Revision 2.3  1998/11/15 23:55:06  keil
 * changes from 2.0
 *
 * Revision 2.2  1998/05/25 14:10:17  keil
 * HiSax 3.0
 * X.75 and leased are working again.
 *
 * Revision 2.1  1998/05/25 12:58:13  keil
 * HiSax golden code from certification, Don't use !!!
 * No leased lines, no X75, but many changes.
 *
 * Revision 2.0  1997/07/27 21:15:42  keil
 * New Callref based layer3
 *
 * Revision 1.4  1997/06/26 11:20:57  keil
 * ?
 *
 * Revision 1.3  1997/04/06 22:54:17  keil
 * Using SKB's
 *
 * Revision 1.2  1997/01/21 22:31:28  keil
 * new statemachine; L3 timers
 *
 * Revision 1.1  1996/10/13 20:03:47  keil
 * Initial revision
 *
 *
 */

#define SBIT(state) (1<<state)
#define ALL_STATES  0x03ffffff

#define PROTO_DIS_EURO	0x08

#define L3_DEB_WARN	0x01
#define L3_DEB_PROTERR	0x02
#define L3_DEB_STATE	0x04
#define L3_DEB_CHARGE	0x08
#define L3_DEB_CHECK	0x10
#define L3_DEB_SI	0x20

struct stateentry {
	int state;
	int primitive;
	void (*rout) (struct l3_process *, u_char, void *);
};

#define l3_debug(st, fmt, args...) HiSax_putstatus(st->l1.hardware, "l3 ", fmt, ## args)

extern void newl3state(struct l3_process *pc, int state);
extern void L3InitTimer(struct l3_process *pc, struct L3Timer *t);
extern void L3DelTimer(struct L3Timer *t);
extern int L3AddTimer(struct L3Timer *t, int millisec, int event);
extern void StopAllL3Timer(struct l3_process *pc);
extern struct sk_buff *l3_alloc_skb(int len);
extern struct l3_process *new_l3_process(struct PStack *st, int cr);
extern void release_l3_process(struct l3_process *p);
extern struct l3_process *getl3proc(struct PStack *st, int cr);
extern void l3_msg(struct PStack *st, int pr, void *arg);
