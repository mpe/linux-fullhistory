/* $Id: isdnl3.h,v 1.2 1997/01/21 22:31:28 keil Exp $
 *
 * $Log: isdnl3.h,v $
 * Revision 1.2  1997/01/21 22:31:28  keil
 * new statemachine; L3 timers
 *
 * Revision 1.1  1996/10/13 20:03:47  keil
 * Initial revision
 *
 *
 */

#define SBIT(state) (1<<state)
#define ALL_STATES  0x00ffffff

#define	PROTO_DIS_EURO	0x08

#define L3_DEB_WARN	0x01
#define	L3_DEB_PROTERR	0x02
#define	L3_DEB_STATE	0x04
#define	L3_DEB_CHARGE	0x08

struct stateentry {
	int             state;
	byte            primitive;
	void            (*rout) (struct PStack *, byte, void *);
};

extern void l3_debug(struct PStack *st, char *s);
extern void newl3state(struct PStack *st, int state);
extern void L3InitTimer(struct PStack *st, struct L3Timer *t);
extern void L3DelTimer(struct L3Timer *t);
extern int  L3AddTimer(struct L3Timer *t, int millisec, int event);
extern void StopAllL3Timer(struct PStack *st);
