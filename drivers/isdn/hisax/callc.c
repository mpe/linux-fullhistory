/* $Id: callc.c,v 2.25 1999/01/02 11:17:20 keil Exp $

 * Author       Karsten Keil (keil@isdn4linux.de)
 *              based on the teles driver from Jan den Ouden
 *
 *		This file is (c) under GNU PUBLIC LICENSE
 *		For changes and modifications please read
 *		../../../Documentation/isdn/HiSax.cert
 *
 * Thanks to    Jan den Ouden
 *              Fritz Elfert
 *
 * $Log: callc.c,v $
 * Revision 2.25  1999/01/02 11:17:20  keil
 * Changes for 2.2
 *
 * Revision 2.24  1998/11/15 23:54:24  keil
 * changes from 2.0
 *
 * Revision 2.23  1998/09/30 22:21:57  keil
 * cosmetics
 *
 * Revision 2.22  1998/08/20 13:50:29  keil
 * More support for hybrid modem (not working yet)
 *
 * Revision 2.21  1998/08/13 23:36:15  keil
 * HiSax 3.1 - don't work stable with current LinkLevel
 *
 * Revision 2.20  1998/06/26 15:13:05  fritz
 * Added handling of STAT_ICALL with incomplete CPN.
 * Added AT&L for ttyI emulator.
 * Added more locking stuff in tty_write.
 *
 * Revision 2.19  1998/05/25 14:08:06  keil
 * HiSax 3.0
 * fixed X.75 and leased line to work again
 * Point2Point and fixed TEI are runtime options now:
 *    hisaxctrl <id> 7 1  set PTP
 *    hisaxctrl <id> 8 <TEIVALUE *2 >
 *    set fixed TEI to TEIVALUE (0-63)
 *
 * Revision 2.18  1998/05/25 12:57:40  keil
 * HiSax golden code from certification, Don't use !!!
 * No leased lines, no X75, but many changes.
 *
 * Revision 2.17  1998/04/15 16:46:06  keil
 * RESUME support
 *
 * Revision 2.16  1998/04/10 10:35:17  paul
 * fixed (silly?) warnings from egcs on Alpha.
 *
 * Revision 2.15  1998/03/19 13:18:37  keil
 * Start of a CAPI like interface for supplementary Service
 * first service: SUSPEND
 *
 * Revision 2.14  1998/03/07 22:56:54  tsbogend
 * made HiSax working on Linux/Alpha
 *
 * Revision 2.13  1998/02/12 23:07:16  keil
 * change for 2.1.86 (removing FREE_READ/FREE_WRITE from [dev]_kfree_skb()
 *
 * Revision 2.12  1998/02/09 10:55:54  keil
 * New leased line mode
 *
 * Revision 2.11  1998/02/02 13:35:19  keil
 * config B-channel delay
 *
 * Revision 2.10  1997/11/06 17:09:15  keil
 * New 2.1 init code
 *
 * Revision 2.9  1997/10/29 19:01:58  keil
 * new LL interface
 *
 * Revision 2.8  1997/10/10 20:56:44  fritz
 * New HL interface.
 *
 * Revision 2.7  1997/10/01 09:21:28  fritz
 * Removed old compatibility stuff for 2.0.X kernels.
 * From now on, this code is for 2.1.X ONLY!
 * Old stuff is still in the separate branch.
 *
 * Revision 2.6  1997/09/11 17:26:58  keil
 * Open B-channel if here are incomming packets
 *
 * Revision 2.5  1997/08/07 17:46:05  keil
 * Fix Incomming Call without broadcast
 *
 * Revision 2.4  1997/08/03 14:37:58  keil
 * Activate Layer2 in PtP mode
 *
 * Revision 2.3  1997/07/31 19:23:40  keil
 * LAYER2_WATCHING for PtP
 *
 * Revision 2.2  1997/07/31 11:48:18  keil
 * experimental REJECT after ALERTING
 *
 * Revision 2.1  1997/07/30 17:12:59  keil
 * more changes for 'One TEI per card'
 *
 * Revision 2.0  1997/07/27 21:12:21  keil
 * CRef based L3; new channel handling; many other stuff
 *
 * Revision 1.31  1997/06/26 11:09:23  keil
 * New managment and minor changes
 *
 * old logs removed /KKe
 *
 */

#define __NO_VERSION__
#include "hisax.h"
#include "../avmb1/capicmd.h"  /* this should be moved in a common place */

#ifdef MODULE
#define MOD_USE_COUNT ( GET_USE_COUNT (&__this_module))
#endif				/* MODULE */

const char *lli_revision = "$Revision: 2.25 $";

extern struct IsdnCard cards[];
extern int nrcards;
extern void HiSax_mod_dec_use_count(void);
extern void HiSax_mod_inc_use_count(void);

static int init_b_st(struct Channel *chanp, int incoming);
static void release_b_st(struct Channel *chanp);

static struct Fsm callcfsm =
{NULL, 0, 0, NULL, NULL};

static int chancount = 0;

/* experimental REJECT after ALERTING for CALLBACK to beat the 4s delay */
#define ALERT_REJECT 0

/* Value to delay the sending of the first B-channel paket after CONNECT
 * here is no value given by ITU, but experience shows that 300 ms will
 * work on many networks, if you or your other side is behind local exchanges
 * a greater value may be recommented. If the delay is to short the first paket
 * will be lost and autodetect on many comercial routers goes wrong !
 * You can adjust this value on runtime with
 * hisaxctrl <id> 2 <value>
 * value is in milliseconds
 */
#define DEFAULT_B_DELAY	300

/* Flags for remembering action done in lli */

#define  FLG_START_D	0
#define  FLG_ESTAB_D	1
#define  FLG_CALL_SEND	2
#define  FLG_CALL_REC   3
#define  FLG_CALL_ALERT	4
#define  FLG_START_B	5
#define  FLG_CONNECT_B	6
#define  FLG_LL_DCONN	7
#define  FLG_LL_BCONN	8
#define  FLG_DISC_SEND	9
#define  FLG_DISC_REC	10
#define  FLG_REL_REC	11
#define  FLG_DO_ALERT	12
#define  FLG_DO_HANGUP	13
#define  FLG_DO_CONNECT	14
#define  FLG_DO_ESTAB	15
#define  FLG_RESUME	16

/*
 * Because of callback it's a good idea to delay the shutdown of the d-channel
 */
#define	DREL_TIMER_VALUE 40000

/*
 * Find card with given driverId
 */
static inline struct IsdnCardState
*
hisax_findcard(int driverid)
{
	int i;

	for (i = 0; i < nrcards; i++)
		if (cards[i].cs)
			if (cards[i].cs->myid == driverid)
				return (cards[i].cs);
	return (struct IsdnCardState *) 0;
}

int
discard_queue(struct sk_buff_head *q)
{
	struct sk_buff *skb;
	int ret=0;

	while ((skb = skb_dequeue(q))) {
		dev_kfree_skb(skb);
		ret++;
	}
	return(ret);
}

static void
link_debug(struct Channel *chanp, int direction, char *fmt, ...)
{
	va_list args;
	char tmp[16];

	va_start(args, fmt);
	sprintf(tmp, "Ch%d %s ", chanp->chan,
		direction ? "LL->HL" : "HL->LL");
	VHiSax_putstatus(chanp->cs, tmp, fmt, args);
	va_end(args);
}


enum {
	ST_NULL,		/*  0 inactive */
	ST_OUT_WAIT_D,		/*  1 outgoing, awaiting d-channel establishment */
	ST_IN_WAIT_D,		/*  2 incoming, awaiting d-channel establishment */
	ST_OUT_DIAL,		/*  3 outgoing, SETUP send; awaiting confirm */
	ST_IN_WAIT_LL,		/*  4 incoming call received; wait for LL confirm */
	ST_IN_ALERT_SEND,	/*  5 incoming call received; ALERT send */
	ST_IN_WAIT_CONN_ACK,	/*  6 incoming CONNECT send; awaiting CONN_ACK */
	ST_WAIT_BCONN,		/*  7 CONNECT/CONN_ACK received, awaiting b-channel prot. estbl. */
	ST_ACTIVE,		/*  8 active, b channel prot. established */
	ST_WAIT_BRELEASE,	/*  9 call clear. (initiator), awaiting b channel prot. rel. */
	ST_WAIT_BREL_DISC,	/* 10 call clear. (receiver), DISCONNECT req. received */
	ST_WAIT_DCOMMAND,	/* 11 call clear. (receiver), awaiting DCHANNEL message */
	ST_WAIT_DRELEASE,	/* 12 DISCONNECT sent, awaiting RELEASE */
	ST_WAIT_D_REL_CNF,	/* 13 RELEASE sent, awaiting RELEASE confirm */
	ST_WAIT_DSHUTDOWN,	/*  14 awaiting d-channel shutdown */
};

#define STATE_COUNT (ST_WAIT_DSHUTDOWN +1)

static char *strState[] =
{
	"ST_NULL",
	"ST_OUT_WAIT_D",
	"ST_IN_WAIT_D",
	"ST_OUT_DIAL",
	"ST_IN_WAIT_LL",
	"ST_IN_ALERT_SEND",
	"ST_IN_WAIT_CONN_ACK",
	"ST_WAIT_BCONN",
	"ST_ACTIVE",
	"ST_WAIT_BRELEASE",
	"ST_WAIT_BREL_DISC",
	"ST_WAIT_DCOMMAND",
	"ST_WAIT_DRELEASE",
	"ST_WAIT_D_REL_CNF",
	"ST_WAIT_DSHUTDOWN",
};

enum {
	EV_DIAL,		/*  0 */
	EV_SETUP_CNF,		/*  1 */
	EV_ACCEPTB,		/*  2 */
	EV_DISCONNECT_IND,	/*  3 */
	EV_RELEASE_CNF,		/*  4 */
	EV_DLEST,		/*  5 */
	EV_DLRL,		/*  6 */
	EV_SETUP_IND,		/*  7 */
	EV_RELEASE_IND,		/*  8 */
	EV_ACCEPTD,		/*  9 */
	EV_SETUP_CMPL_IND,	/* 10 */
	EV_BC_EST,		/* 11 */
	EV_WRITEBUF,		/* 12 */
	EV_ESTABLISH,		/* 13 */
	EV_HANGUP,		/* 14 */
	EV_BC_REL,		/* 15 */
	EV_CINF,		/* 16 */
	EV_SUSPEND,		/* 17 */
	EV_RESUME,		/* 18 */
	EV_SHUTDOWN_D,		/* 19 */
	EV_NOSETUP_RSP,		/* 20 */
	EV_SETUP_ERR,		/* 21 */
	EV_CONNECT_ERR,		/* 22 */
	EV_RELEASE_ERR,		/* 23 */
};

#define EVENT_COUNT (EV_RELEASE_ERR +1)

static char *strEvent[] =
{
	"EV_DIAL",
	"EV_SETUP_CNF",
	"EV_ACCEPTB",
	"EV_DISCONNECT_IND",
	"EV_RELEASE_CNF",
	"EV_DLEST",
	"EV_DLRL",
	"EV_SETUP_IND",
	"EV_RELEASE_IND",
	"EV_ACCEPTD",
	"EV_SETUP_CMPL_IND",
	"EV_BC_EST",
	"EV_WRITEBUF",
	"EV_ESTABLISH",
	"EV_HANGUP",
	"EV_BC_REL",
	"EV_CINF",
	"EV_SUSPEND",
	"EV_RESUME",
	"EV_SHUTDOWN_D",
	"EV_NOSETUP_RSP",
	"EV_SETUP_ERR",
	"EV_CONNECT_ERR",
	"EV_RELEASE_ERR",
};

static inline void
lli_deliver_cause(struct Channel *chanp, isdn_ctrl *ic)
{
	if (chanp->proc->para.cause < 0)
		return;
	ic->driver = chanp->cs->myid;
	ic->command = ISDN_STAT_CAUSE;
	ic->arg = chanp->chan;
	if (chanp->cs->protocol == ISDN_PTYPE_EURO)
		sprintf(ic->parm.num, "E%02X%02X", chanp->proc->para.loc & 0x7f,
			chanp->proc->para.cause & 0x7f);
	else
		sprintf(ic->parm.num, "%02X%02X", chanp->proc->para.loc & 0x7f,
			chanp->proc->para.cause & 0x7f);
	chanp->cs->iif.statcallb(ic);
}

static void
lli_d_established(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	test_and_set_bit(FLG_ESTAB_D, &chanp->Flags);
	if (chanp->leased) {
		isdn_ctrl ic;
		int ret;

		chanp->cs->cardmsg(chanp->cs, MDL_INFO_SETUP, (void *) (long)chanp->chan);
		FsmChangeState(fi, ST_IN_WAIT_LL);
		test_and_set_bit(FLG_CALL_REC, &chanp->Flags);
		if (chanp->debug & 1)
			link_debug(chanp, 0, "STAT_ICALL_LEASED");
		ic.driver = chanp->cs->myid;
		ic.command = ISDN_STAT_ICALL;
		ic.arg = chanp->chan;
		ic.parm.setup.si1 = 7;
		ic.parm.setup.si2 = 0;
		ic.parm.setup.plan = 0;
		ic.parm.setup.screen = 0;
		sprintf(ic.parm.setup.eazmsn,"%d", chanp->chan + 1);
		sprintf(ic.parm.setup.phone,"LEASED%d", chanp->cs->myid);
		ret = chanp->cs->iif.statcallb(&ic);
		if (chanp->debug & 1)
			link_debug(chanp, 1, "statcallb ret=%d", ret);
		if (!ret) {
			chanp->cs->cardmsg(chanp->cs, MDL_INFO_REL, (void *) (long)chanp->chan);
			FsmChangeState(fi, ST_NULL);
		}
	} else if (fi->state == ST_WAIT_DSHUTDOWN)
		FsmChangeState(fi, ST_NULL);
}

static void
lli_d_released(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	test_and_clear_bit(FLG_START_D, &chanp->Flags);
}

/*
 * Dial out
 */
static void
lli_prep_dialout(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	FsmChangeState(fi, ST_OUT_WAIT_D);
	FsmDelTimer(&chanp->drel_timer, 60);
	FsmDelTimer(&chanp->dial_timer, 73);
	chanp->l2_active_protocol = chanp->l2_protocol;
	chanp->incoming = 0;
	if (test_bit(FLG_ESTAB_D, &chanp->Flags)) {
		FsmEvent(fi, EV_DLEST, NULL);
	} else {
		chanp->Flags = 0;
		if (EV_RESUME == event)
			test_and_set_bit(FLG_RESUME, &chanp->Flags);
		test_and_set_bit(FLG_START_D, &chanp->Flags);
		chanp->d_st->lli.l4l3(chanp->d_st, DL_ESTABLISH | REQUEST, NULL);
	}
}

static void
lli_do_dialout(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	int ev;

	FsmChangeState(fi, ST_OUT_DIAL);
	chanp->cs->cardmsg(chanp->cs, MDL_INFO_SETUP, (void *) (long)chanp->chan);
	if (test_and_clear_bit(FLG_RESUME, &chanp->Flags))
		ev = CC_RESUME | REQUEST;
	else
		ev = CC_SETUP | REQUEST;
	if (chanp->leased) {
		FsmEvent(&chanp->fi, EV_SETUP_CNF, NULL);
	} else {
		test_and_set_bit(FLG_ESTAB_D, &chanp->Flags);
		chanp->d_st->lli.l4l3(chanp->d_st, ev, chanp);
		test_and_set_bit(FLG_CALL_SEND, &chanp->Flags);
	}
}

static void
lli_init_bchan_out(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	FsmChangeState(fi, ST_WAIT_BCONN);
	test_and_set_bit(FLG_LL_DCONN, &chanp->Flags);
	if (chanp->debug & 1)
		link_debug(chanp, 0, "STAT_DCONN");
	ic.driver = chanp->cs->myid;
	ic.command = ISDN_STAT_DCONN;
	ic.arg = chanp->chan;
	chanp->cs->iif.statcallb(&ic);
	init_b_st(chanp, 0);
	test_and_set_bit(FLG_START_B, &chanp->Flags);
	chanp->b_st->lli.l4l3(chanp->b_st, DL_ESTABLISH | REQUEST, NULL);
}

static void
lli_go_active(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	FsmChangeState(fi, ST_ACTIVE);
	chanp->data_open = !0;
	test_and_set_bit(FLG_CONNECT_B, &chanp->Flags);
	if (chanp->debug & 1)
		link_debug(chanp, 0, "STAT_BCONN");
	test_and_set_bit(FLG_LL_BCONN, &chanp->Flags);
	ic.driver = chanp->cs->myid;
	ic.command = ISDN_STAT_BCONN;
	ic.arg = chanp->chan;
	chanp->cs->iif.statcallb(&ic);
	chanp->cs->cardmsg(chanp->cs, MDL_INFO_CONN, (void *) (long)chanp->chan);
}

/*
 * RESUME
 */

/* incomming call */

static void
lli_start_dchan(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	FsmChangeState(fi, ST_IN_WAIT_D);
	FsmDelTimer(&chanp->drel_timer, 61);
	if (event == EV_ACCEPTD)
		test_and_set_bit(FLG_DO_CONNECT, &chanp->Flags);
	else if (event == EV_HANGUP) {
		test_and_set_bit(FLG_DO_HANGUP, &chanp->Flags);
#ifdef ALERT_REJECT
		test_and_set_bit(FLG_DO_ALERT, &chanp->Flags);
#endif
	}
	if (test_bit(FLG_ESTAB_D, &chanp->Flags)) {
		FsmEvent(fi, EV_DLEST, NULL);
	} else if (!test_and_set_bit(FLG_START_D, &chanp->Flags))
		chanp->d_st->lli.l4l3(chanp->d_st, DL_ESTABLISH | REQUEST, NULL);
}

static void
lli_deliver_call(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;
	int ret;

	chanp->cs->cardmsg(chanp->cs, MDL_INFO_SETUP, (void *) (long)chanp->chan);
	/*
	 * Report incoming calls only once to linklevel, use CallFlags
	 * which is set to 3 with each broadcast message in isdnl1.c
	 * and resetted if a interface  answered the STAT_ICALL.
	 */
	if (1) { /* for only one TEI */
		FsmChangeState(fi, ST_IN_WAIT_LL);
		test_and_set_bit(FLG_CALL_REC, &chanp->Flags);
		if (chanp->debug & 1)
			link_debug(chanp, 0, "STAT_ICALL");
		ic.driver = chanp->cs->myid;
		ic.command = ISDN_STAT_ICALL;
		ic.arg = chanp->chan;
		/*
		 * No need to return "unknown" for calls without OAD,
		 * cause that's handled in linklevel now (replaced by '0')
		 */
		ic.parm.setup = chanp->proc->para.setup;
		ret = chanp->cs->iif.statcallb(&ic);
		if (chanp->debug & 1)
			link_debug(chanp, 1, "statcallb ret=%d", ret);
		switch (ret) {
			case 1:	/* OK, anybody likes this call */
				FsmDelTimer(&chanp->drel_timer, 61);
				if (test_bit(FLG_ESTAB_D, &chanp->Flags)) {
					FsmChangeState(fi, ST_IN_ALERT_SEND);
					test_and_set_bit(FLG_CALL_ALERT, &chanp->Flags);
					chanp->d_st->lli.l4l3(chanp->d_st, CC_ALERTING | REQUEST, chanp->proc);
				} else {
					test_and_set_bit(FLG_DO_ALERT, &chanp->Flags);
					FsmChangeState(fi, ST_IN_WAIT_D);
					test_and_set_bit(FLG_START_D, &chanp->Flags);
					chanp->d_st->lli.l4l3(chanp->d_st,
						DL_ESTABLISH | REQUEST, NULL);
				}
				break;
			case 2:	/* Rejecting Call */
				test_and_clear_bit(FLG_CALL_REC, &chanp->Flags);
				break;
			case 0:	/* OK, nobody likes this call */
			default:	/* statcallb problems */
				chanp->d_st->lli.l4l3(chanp->d_st, CC_IGNORE | REQUEST, chanp->proc);
				chanp->cs->cardmsg(chanp->cs, MDL_INFO_REL, (void *) (long)chanp->chan);
				FsmChangeState(fi, ST_NULL);
				if (test_bit(FLG_ESTAB_D, &chanp->Flags) &&
					!test_bit(FLG_PTP, &chanp->d_st->l2.flag))
					FsmRestartTimer(&chanp->drel_timer, DREL_TIMER_VALUE, EV_SHUTDOWN_D, NULL, 61);
				break;
		}
	} else {
		chanp->d_st->lli.l4l3(chanp->d_st, CC_IGNORE | REQUEST, chanp->proc);
		chanp->cs->cardmsg(chanp->cs, MDL_INFO_REL, (void *) (long)chanp->chan);
		FsmChangeState(fi, ST_NULL);
		if (test_bit(FLG_ESTAB_D, &chanp->Flags) &&
			!test_bit(FLG_PTP, &chanp->d_st->l2.flag))
			FsmRestartTimer(&chanp->drel_timer, DREL_TIMER_VALUE, EV_SHUTDOWN_D, NULL, 62);
	}
}

static void
lli_establish_d(struct FsmInst *fi, int event, void *arg)
{
	/* This establish the D-channel for pending L3 messages
	 * without blocking the channel
	 */
	struct Channel *chanp = fi->userdata;

	test_and_set_bit(FLG_DO_ESTAB, &chanp->Flags);
	FsmChangeState(fi, ST_IN_WAIT_D);
	test_and_set_bit(FLG_START_D, &chanp->Flags);
	chanp->d_st->lli.l4l3(chanp->d_st, DL_ESTABLISH | REQUEST, NULL);
}

static void
lli_do_action(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	test_and_set_bit(FLG_ESTAB_D, &chanp->Flags);
	if (chanp->leased) {
		FsmChangeState(fi, ST_IN_WAIT_CONN_ACK);
		test_and_clear_bit(FLG_DO_ALERT, &chanp->Flags);
		test_and_clear_bit(FLG_DO_CONNECT, &chanp->Flags);
		FsmEvent(&chanp->fi, EV_SETUP_CMPL_IND, NULL);
	} else if (test_and_clear_bit(FLG_DO_CONNECT, &chanp->Flags) &&
		!test_bit(FLG_DO_HANGUP, &chanp->Flags)) {
		FsmChangeState(fi, ST_IN_WAIT_CONN_ACK);
		test_and_clear_bit(FLG_DO_ALERT, &chanp->Flags);
		chanp->d_st->lli.l4l3(chanp->d_st, CC_SETUP | RESPONSE, chanp->proc);
	} else if (test_and_clear_bit(FLG_DO_ALERT, &chanp->Flags)) {
		if (test_bit(FLG_DO_HANGUP, &chanp->Flags))
			FsmRestartTimer(&chanp->drel_timer, 40, EV_HANGUP, NULL, 63);
		FsmChangeState(fi, ST_IN_ALERT_SEND);
		test_and_set_bit(FLG_CALL_ALERT, &chanp->Flags);
		chanp->d_st->lli.l4l3(chanp->d_st, CC_ALERTING | REQUEST, chanp->proc);
	} else if (test_and_clear_bit(FLG_DO_HANGUP, &chanp->Flags)) {
		FsmChangeState(fi, ST_WAIT_DRELEASE);
		chanp->proc->para.cause = 0x15;		/* Call Rejected */
		chanp->d_st->lli.l4l3(chanp->d_st, CC_REJECT | REQUEST, chanp->proc);
		test_and_set_bit(FLG_DISC_SEND, &chanp->Flags);
	}
}

static void
lli_send_dconnect(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	FsmChangeState(fi, ST_IN_WAIT_CONN_ACK);
	chanp->d_st->lli.l4l3(chanp->d_st, CC_SETUP | RESPONSE, chanp->proc);
}

static void
lli_init_bchan_in(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	FsmChangeState(fi, ST_WAIT_BCONN);
	test_and_set_bit(FLG_LL_DCONN, &chanp->Flags);
	if (chanp->debug & 1)
		link_debug(chanp, 0, "STAT_DCONN");
	ic.driver = chanp->cs->myid;
	ic.command = ISDN_STAT_DCONN;
	ic.arg = chanp->chan;
	chanp->cs->iif.statcallb(&ic);
	chanp->l2_active_protocol = chanp->l2_protocol;
	chanp->incoming = !0;
	init_b_st(chanp, !0);
	test_and_set_bit(FLG_START_B, &chanp->Flags);
	chanp->b_st->lli.l4l3(chanp->b_st, DL_ESTABLISH | REQUEST, NULL);
}

/* Call suspend */

static void
lli_suspend(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	chanp->d_st->lli.l4l3(chanp->d_st, CC_SUSPEND | REQUEST, chanp->proc);
}

/* Call clearing */

static void
lli_cancel_call(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	FsmChangeState(fi, ST_WAIT_DRELEASE);
	if (test_and_clear_bit(FLG_LL_BCONN, &chanp->Flags)) {
		if (chanp->debug & 1)
			link_debug(chanp, 0, "STAT_BHUP");
		ic.driver = chanp->cs->myid;
		ic.command = ISDN_STAT_BHUP;
		ic.arg = chanp->chan;
		chanp->cs->iif.statcallb(&ic);
	}
	if (test_and_clear_bit(FLG_START_B, &chanp->Flags))
		release_b_st(chanp);
	chanp->proc->para.cause = 0x10;		/* Normal Call Clearing */
	chanp->d_st->lli.l4l3(chanp->d_st, CC_DISCONNECT | REQUEST, chanp->proc);
	test_and_set_bit(FLG_DISC_SEND, &chanp->Flags);
}

static void
lli_shutdown_d(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	FsmDelTimer(&chanp->drel_timer, 62);
	if (test_bit(FLG_PTP, &chanp->d_st->l2.flag)) {
		FsmChangeState(fi, ST_NULL);
	} else {
		if (!test_bit(FLG_TWO_DCHAN, &chanp->cs->HW_Flags)) {
			if (chanp->chan) {
				if (chanp->cs->channel[0].fi.state != ST_NULL)
					return;
			} else {
				if (chanp->cs->channel[1].fi.state != ST_NULL)
					return;
			}
		}
		FsmChangeState(fi, ST_WAIT_DSHUTDOWN);
		test_and_clear_bit(FLG_ESTAB_D, &chanp->Flags);
		chanp->d_st->lli.l4l3(chanp->d_st, DL_RELEASE | REQUEST, NULL);
	}
}

static void
lli_timeout_d(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	test_and_clear_bit(FLG_LL_DCONN, &chanp->Flags);
	if (chanp->debug & 1)
		link_debug(chanp, 0, "STAT_DHUP");
	lli_deliver_cause(chanp, &ic);
	ic.driver = chanp->cs->myid;
	ic.command = ISDN_STAT_DHUP;
	ic.arg = chanp->chan;
	chanp->cs->iif.statcallb(&ic);
	FsmChangeState(fi, ST_NULL);
	chanp->Flags = 0;
	test_and_set_bit(FLG_ESTAB_D, &chanp->Flags);
	if (!test_bit(FLG_PTP, &chanp->d_st->l2.flag))
		FsmAddTimer(&chanp->drel_timer, DREL_TIMER_VALUE, EV_SHUTDOWN_D, NULL, 60);
	chanp->cs->cardmsg(chanp->cs, MDL_INFO_REL, (void *) (long)chanp->chan);
}

static void
lli_go_null(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	FsmChangeState(fi, ST_NULL);
	chanp->Flags = 0;
	FsmDelTimer(&chanp->drel_timer, 63);
	chanp->cs->cardmsg(chanp->cs, MDL_INFO_REL, (void *) (long)chanp->chan);
}

static void
lli_disconn_bchan(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	chanp->data_open = 0;
	FsmChangeState(fi, ST_WAIT_BRELEASE);
	test_and_clear_bit(FLG_CONNECT_B, &chanp->Flags);
	chanp->b_st->lli.l4l3(chanp->b_st, DL_RELEASE | REQUEST, NULL);
}

static void
lli_send_d_disc(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	if (test_bit(FLG_DISC_REC, &chanp->Flags) ||
		test_bit(FLG_REL_REC, &chanp->Flags))
		return;
	FsmChangeState(fi, ST_WAIT_DRELEASE);
	if (test_and_clear_bit(FLG_LL_BCONN, &chanp->Flags)) {
		if (chanp->debug & 1)
			link_debug(chanp, 0, "STAT_BHUP");
		ic.driver = chanp->cs->myid;
		ic.command = ISDN_STAT_BHUP;
		ic.arg = chanp->chan;
		chanp->cs->iif.statcallb(&ic);
	}
	if (test_and_clear_bit(FLG_START_B, &chanp->Flags))
		release_b_st(chanp);
	if (chanp->leased) {
		ic.command = ISDN_STAT_CAUSE;
		ic.arg = chanp->chan;
		sprintf(ic.parm.num, "L0010");
		chanp->cs->iif.statcallb(&ic);
		if (chanp->debug & 1)
			link_debug(chanp, 0, "STAT_DHUP");
		ic.driver = chanp->cs->myid;
		ic.command = ISDN_STAT_DHUP;
		ic.arg = chanp->chan;
		chanp->cs->iif.statcallb(&ic);
		FsmChangeState(fi, ST_WAIT_DSHUTDOWN);
		test_and_clear_bit(FLG_ESTAB_D, &chanp->Flags);
		chanp->d_st->lli.l4l3(chanp->d_st, DL_RELEASE | REQUEST, NULL);
	} else {
		if (test_and_clear_bit(FLG_DO_HANGUP, &chanp->Flags))
			chanp->proc->para.cause = 0x15;		/* Call Reject */
		else
			chanp->proc->para.cause = 0x10;		/* Normal Call Clearing */
		chanp->d_st->lli.l4l3(chanp->d_st, CC_DISCONNECT | REQUEST, chanp->proc);
		test_and_set_bit(FLG_DISC_SEND, &chanp->Flags);
	}
}

static void
lli_released_bchan(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	FsmChangeState(fi, ST_WAIT_DCOMMAND);
	chanp->data_open = 0;
	if (test_and_clear_bit(FLG_LL_BCONN, &chanp->Flags)) {
		if (chanp->debug & 1)
			link_debug(chanp, 0, "STAT_BHUP");
		ic.driver = chanp->cs->myid;
		ic.command = ISDN_STAT_BHUP;
		ic.arg = chanp->chan;
		chanp->cs->iif.statcallb(&ic);
	}
	release_b_st(chanp);
	test_and_clear_bit(FLG_START_B, &chanp->Flags);
}


static void
lli_release_bchan(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	chanp->data_open = 0;
	test_and_set_bit(FLG_DISC_REC, &chanp->Flags);
	FsmChangeState(fi, ST_WAIT_BREL_DISC);
	test_and_clear_bit(FLG_CONNECT_B, &chanp->Flags);
	chanp->b_st->lli.l4l3(chanp->b_st, DL_RELEASE | REQUEST, NULL);
}

static void
lli_received_d_rel(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	chanp->data_open = 0;
	FsmChangeState(fi, ST_NULL);
	test_and_set_bit(FLG_REL_REC, &chanp->Flags);
	if (test_and_clear_bit(FLG_CONNECT_B, &chanp->Flags)) {
		chanp->b_st->lli.l4l3(chanp->b_st, DL_RELEASE | REQUEST, NULL);
	}
	if (test_and_clear_bit(FLG_LL_BCONN, &chanp->Flags)) {
		if (chanp->debug & 1)
			link_debug(chanp, 0, "STAT_BHUP");
		ic.driver = chanp->cs->myid;
		ic.command = ISDN_STAT_BHUP;
		ic.arg = chanp->chan;
		chanp->cs->iif.statcallb(&ic);
	}
	if (test_and_clear_bit(FLG_START_B, &chanp->Flags))
		release_b_st(chanp);
	if (chanp->debug & 1)
		link_debug(chanp, 0, "STAT_DHUP");
	lli_deliver_cause(chanp, &ic);
	ic.driver = chanp->cs->myid;
	ic.command = ISDN_STAT_DHUP;
	ic.arg = chanp->chan;
	chanp->cs->iif.statcallb(&ic);
	test_and_clear_bit(FLG_DISC_SEND, &chanp->Flags);
	test_and_clear_bit(FLG_CALL_REC, &chanp->Flags);
	test_and_clear_bit(FLG_CALL_ALERT, &chanp->Flags);
	test_and_clear_bit(FLG_LL_DCONN, &chanp->Flags);
	test_and_clear_bit(FLG_CALL_SEND, &chanp->Flags);
	lli_timeout_d(fi, event, arg);
}

static void
lli_received_d_relcnf(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	chanp->data_open = 0;
	FsmChangeState(fi, ST_NULL);
	if (test_and_clear_bit(FLG_CONNECT_B, &chanp->Flags)) {
		chanp->b_st->lli.l4l3(chanp->b_st, DL_RELEASE | REQUEST, NULL);
	}
	if (test_and_clear_bit(FLG_LL_BCONN, &chanp->Flags)) {
		if (chanp->debug & 1)
			link_debug(chanp, 0, "STAT_BHUP");
		ic.driver = chanp->cs->myid;
		ic.command = ISDN_STAT_BHUP;
		ic.arg = chanp->chan;
		chanp->cs->iif.statcallb(&ic);
	}
	if (test_and_clear_bit(FLG_START_B, &chanp->Flags))
		release_b_st(chanp);
	if (chanp->debug & 1)
		link_debug(chanp, 0, "STAT_DHUP");
	lli_deliver_cause(chanp, &ic);
	ic.driver = chanp->cs->myid;
	ic.command = ISDN_STAT_DHUP;
	ic.arg = chanp->chan;
	chanp->cs->iif.statcallb(&ic);
	test_and_clear_bit(FLG_DISC_SEND, &chanp->Flags);
	test_and_clear_bit(FLG_CALL_REC, &chanp->Flags);
	test_and_clear_bit(FLG_CALL_ALERT, &chanp->Flags);
	test_and_clear_bit(FLG_LL_DCONN, &chanp->Flags);
	test_and_clear_bit(FLG_CALL_SEND, &chanp->Flags);
	lli_timeout_d(fi, event, arg);
}

static void
lli_received_d_disc(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	chanp->data_open = 0;
	FsmChangeState(fi, ST_WAIT_D_REL_CNF);
	test_and_set_bit(FLG_DISC_REC, &chanp->Flags);
	if (test_and_clear_bit(FLG_LL_BCONN, &chanp->Flags)) {
		if (chanp->debug & 1)
			link_debug(chanp, 0, "STAT_BHUP");
		ic.driver = chanp->cs->myid;
		ic.command = ISDN_STAT_BHUP;
		ic.arg = chanp->chan;
		chanp->cs->iif.statcallb(&ic);
	}
	if (test_and_clear_bit(FLG_START_B, &chanp->Flags))
		release_b_st(chanp);
	if (chanp->debug & 1)
		link_debug(chanp, 0, "STAT_DHUP");
	lli_deliver_cause(chanp, &ic);
	ic.driver = chanp->cs->myid;
	ic.command = ISDN_STAT_DHUP;
	ic.arg = chanp->chan;
	chanp->cs->iif.statcallb(&ic);
	test_and_clear_bit(FLG_CALL_ALERT, &chanp->Flags);
	test_and_clear_bit(FLG_LL_DCONN, &chanp->Flags);
	test_and_clear_bit(FLG_CALL_SEND, &chanp->Flags);
	chanp->d_st->lli.l4l3(chanp->d_st, CC_RELEASE | REQUEST, chanp->proc);
}

/* processing charge info */
static void
lli_charge_info(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	ic.driver = chanp->cs->myid;
	ic.command = ISDN_STAT_CINF;
	ic.arg = chanp->chan;
	sprintf(ic.parm.num, "%d", chanp->proc->para.chargeinfo);
	chanp->cs->iif.statcallb(&ic);
}

/* error procedures */

static void
lli_no_dchan(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	if (chanp->debug & 1)
		link_debug(chanp, 0, "STAT_NODCH");
	ic.driver = chanp->cs->myid;
	ic.command = ISDN_STAT_NODCH;
	ic.arg = chanp->chan;
	chanp->cs->iif.statcallb(&ic);
	chanp->Flags = 0;
	FsmChangeState(fi, ST_NULL);
	chanp->d_st->lli.l4l3(chanp->d_st, DL_RELEASE | REQUEST, NULL);
}

static void
lli_no_dchan_ready(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	if (chanp->debug & 1)
		link_debug(chanp, 0, "STAT_DHUP");
	ic.driver = chanp->cs->myid;
	ic.command = ISDN_STAT_DHUP;
	ic.arg = chanp->chan;
	chanp->cs->iif.statcallb(&ic);
}

static void
lli_no_dchan_in(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	if (chanp->debug & 1)
		link_debug(chanp, 0, "STAT_DHUP");
	ic.driver = chanp->cs->myid;
	ic.command = ISDN_STAT_DHUP;
	ic.arg = chanp->chan;
	chanp->cs->iif.statcallb(&ic);
	chanp->d_st->lli.l4l3(chanp->d_st, CC_DLRL | REQUEST, chanp->proc);
	chanp->Flags = 0;
	FsmChangeState(fi, ST_NULL);
	chanp->d_st->lli.l4l3(chanp->d_st, DL_RELEASE | REQUEST, NULL);
}

static void
lli_no_setup_rsp(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	FsmChangeState(fi, ST_NULL);
	test_and_clear_bit(FLG_CALL_SEND, &chanp->Flags);
	if (chanp->debug & 1)
		link_debug(chanp, 0, "STAT_DHUP");
	ic.driver = chanp->cs->myid;
	ic.command = ISDN_STAT_DHUP;
	ic.arg = chanp->chan;
	chanp->cs->iif.statcallb(&ic);
	lli_shutdown_d(fi, event, arg);
}

static void
lli_setup_err(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	FsmChangeState(fi, ST_WAIT_DRELEASE);
	test_and_clear_bit(FLG_LL_DCONN, &chanp->Flags);
	if (chanp->debug & 1)
		link_debug(chanp, 0, "STAT_DHUP");
	lli_deliver_cause(chanp, &ic);
	ic.driver = chanp->cs->myid;
	ic.command = ISDN_STAT_DHUP;
	ic.arg = chanp->chan;
	chanp->cs->iif.statcallb(&ic);
	test_and_set_bit(FLG_DISC_SEND, &chanp->Flags);	/* DISCONN was sent from L3 */
}

static void
lli_connect_err(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	FsmChangeState(fi, ST_WAIT_DRELEASE);
	test_and_clear_bit(FLG_LL_DCONN, &chanp->Flags);
	if (chanp->debug & 1)
		link_debug(chanp, 0, "STAT_DHUP");
	lli_deliver_cause(chanp, &ic);
	ic.driver = chanp->cs->myid;
	ic.command = ISDN_STAT_DHUP;
	ic.arg = chanp->chan;
	chanp->cs->iif.statcallb(&ic);
	test_and_set_bit(FLG_DISC_SEND, &chanp->Flags);	/* DISCONN was sent from L3 */
}

static void
lli_got_dlrl(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	chanp->data_open = 0;
	FsmChangeState(fi, ST_NULL);
	if (test_and_clear_bit(FLG_CONNECT_B, &chanp->Flags)) {
		chanp->b_st->lli.l4l3(chanp->b_st, DL_RELEASE | REQUEST, NULL);
	}
	if (test_and_clear_bit(FLG_LL_BCONN, &chanp->Flags)) {
		if (chanp->debug & 1)
			link_debug(chanp, 0, "STAT_BHUP");
		ic.driver = chanp->cs->myid;
		ic.command = ISDN_STAT_BHUP;
		ic.arg = chanp->chan;
		chanp->cs->iif.statcallb(&ic);
	}
	if (test_and_clear_bit(FLG_START_B, &chanp->Flags))
		release_b_st(chanp);
	if (chanp->leased) {
		ic.driver = chanp->cs->myid;
		ic.command = ISDN_STAT_CAUSE;
		ic.arg = chanp->chan;
		sprintf(ic.parm.num, "L%02X%02X", 0, 0x2f);
		chanp->cs->iif.statcallb(&ic);
		ic.driver = chanp->cs->myid;
		ic.command = ISDN_STAT_DHUP;
		ic.arg = chanp->chan;
		chanp->cs->iif.statcallb(&ic);
		chanp->Flags = 0;
	} else {
		test_and_clear_bit(FLG_LL_DCONN, &chanp->Flags);
		if (chanp->debug & 1)
			link_debug(chanp, 0, "STAT_DHUP");
		if (chanp->cs->protocol == ISDN_PTYPE_EURO) {
			chanp->proc->para.cause = 0x2f;
			chanp->proc->para.loc = 0;
		} else {
			chanp->proc->para.cause = 0x70;
			chanp->proc->para.loc = 0;
		}
		lli_deliver_cause(chanp, &ic);
		ic.driver = chanp->cs->myid;
		ic.command = ISDN_STAT_DHUP;
		ic.arg = chanp->chan;
		chanp->cs->iif.statcallb(&ic);
		chanp->d_st->lli.l4l3(chanp->d_st, CC_DLRL | REQUEST, chanp->proc);
		chanp->Flags = 0;
		chanp->d_st->lli.l4l3(chanp->d_st, DL_RELEASE | REQUEST, NULL);
	}
	chanp->cs->cardmsg(chanp->cs, MDL_INFO_REL, (void *) (long)chanp->chan);
}

/* *INDENT-OFF* */
static struct FsmNode fnlist[] HISAX_INITDATA =
{
	{ST_NULL,		EV_DIAL,		lli_prep_dialout},
	{ST_NULL,		EV_RESUME,		lli_prep_dialout},
	{ST_NULL,		EV_SETUP_IND,		lli_deliver_call},
	{ST_NULL,		EV_SHUTDOWN_D,		lli_shutdown_d},
	{ST_NULL,		EV_DLRL,		lli_go_null},
	{ST_NULL,		EV_DLEST,		lli_d_established},
	{ST_NULL,		EV_ESTABLISH,		lli_establish_d},
	{ST_OUT_WAIT_D,		EV_DLEST,		lli_do_dialout},
	{ST_OUT_WAIT_D,		EV_DLRL,		lli_no_dchan},
	{ST_OUT_WAIT_D,		EV_HANGUP,		lli_no_dchan},
	{ST_IN_WAIT_D,		EV_DLEST,		lli_do_action},
	{ST_IN_WAIT_D,		EV_DLRL,		lli_no_dchan_in},
	{ST_IN_WAIT_D,		EV_ACCEPTD,		lli_start_dchan},
	{ST_IN_WAIT_D,		EV_HANGUP,		lli_start_dchan},
	{ST_OUT_DIAL,		EV_SETUP_CNF,		lli_init_bchan_out},
	{ST_OUT_DIAL,		EV_HANGUP,		lli_cancel_call},
	{ST_OUT_DIAL,		EV_DISCONNECT_IND,	lli_received_d_disc},
	{ST_OUT_DIAL,		EV_RELEASE_IND,		lli_received_d_rel},
	{ST_OUT_DIAL,		EV_RELEASE_CNF,		lli_received_d_relcnf},
	{ST_OUT_DIAL,		EV_NOSETUP_RSP,		lli_no_setup_rsp},
	{ST_OUT_DIAL,		EV_SETUP_ERR,		lli_setup_err},
	{ST_OUT_DIAL,		EV_DLRL,		lli_got_dlrl},
	{ST_IN_WAIT_LL,		EV_DLEST,		lli_d_established},
	{ST_IN_WAIT_LL,		EV_DLRL,		lli_d_released},
	{ST_IN_WAIT_LL,		EV_ACCEPTD,		lli_start_dchan},
	{ST_IN_WAIT_LL,		EV_HANGUP,		lli_start_dchan},
	{ST_IN_WAIT_LL,		EV_DISCONNECT_IND,	lli_received_d_disc},
	{ST_IN_WAIT_LL,		EV_RELEASE_IND,		lli_received_d_rel},
	{ST_IN_WAIT_LL,		EV_RELEASE_CNF,		lli_received_d_relcnf},
	{ST_IN_ALERT_SEND,	EV_SETUP_CMPL_IND,	lli_init_bchan_in},
	{ST_IN_ALERT_SEND,	EV_ACCEPTD,		lli_send_dconnect},
	{ST_IN_ALERT_SEND,	EV_HANGUP,		lli_send_d_disc},
	{ST_IN_ALERT_SEND,	EV_DISCONNECT_IND,	lli_received_d_disc},
	{ST_IN_ALERT_SEND,	EV_RELEASE_IND,		lli_received_d_rel},
	{ST_IN_ALERT_SEND,	EV_RELEASE_CNF,		lli_received_d_relcnf},
	{ST_IN_ALERT_SEND,	EV_DLRL,		lli_got_dlrl},
	{ST_IN_WAIT_CONN_ACK,	EV_SETUP_CMPL_IND,	lli_init_bchan_in},
	{ST_IN_WAIT_CONN_ACK,	EV_HANGUP,		lli_send_d_disc},
	{ST_IN_WAIT_CONN_ACK,	EV_DISCONNECT_IND,	lli_received_d_disc},
	{ST_IN_WAIT_CONN_ACK,	EV_RELEASE_IND,		lli_received_d_rel},
	{ST_IN_WAIT_CONN_ACK,	EV_RELEASE_CNF,		lli_received_d_relcnf},
	{ST_IN_WAIT_CONN_ACK,	EV_CONNECT_ERR,		lli_connect_err},
	{ST_IN_WAIT_CONN_ACK,	EV_DLRL,		lli_got_dlrl},
	{ST_WAIT_BCONN,		EV_BC_EST,		lli_go_active},
	{ST_WAIT_BCONN,		EV_BC_REL,		lli_send_d_disc},
	{ST_WAIT_BCONN,		EV_HANGUP,		lli_send_d_disc},
	{ST_WAIT_BCONN,		EV_DISCONNECT_IND,	lli_received_d_disc},
	{ST_WAIT_BCONN,		EV_RELEASE_IND,		lli_received_d_rel},
	{ST_WAIT_BCONN,		EV_RELEASE_CNF,		lli_received_d_relcnf},
	{ST_WAIT_BCONN,		EV_DLRL,		lli_got_dlrl},
	{ST_WAIT_BCONN,		EV_CINF,		lli_charge_info},
	{ST_ACTIVE,		EV_CINF,		lli_charge_info},
	{ST_ACTIVE,		EV_BC_REL,		lli_released_bchan},
	{ST_ACTIVE,		EV_SUSPEND,		lli_suspend},
	{ST_ACTIVE,		EV_HANGUP,		lli_disconn_bchan},
	{ST_ACTIVE,		EV_DISCONNECT_IND,	lli_release_bchan},
	{ST_ACTIVE,		EV_RELEASE_CNF,		lli_received_d_relcnf},
	{ST_ACTIVE,		EV_RELEASE_IND,		lli_received_d_rel},
	{ST_ACTIVE,		EV_DLRL,		lli_got_dlrl},
	{ST_WAIT_BRELEASE,	EV_BC_REL,		lli_send_d_disc},
	{ST_WAIT_BRELEASE,	EV_DISCONNECT_IND,	lli_received_d_disc},
	{ST_WAIT_BRELEASE,	EV_RELEASE_CNF,		lli_received_d_relcnf},
	{ST_WAIT_BRELEASE,	EV_RELEASE_IND,		lli_received_d_rel},
	{ST_WAIT_BRELEASE,	EV_DLRL,		lli_got_dlrl},
	{ST_WAIT_BREL_DISC,	EV_BC_REL,		lli_received_d_disc},
	{ST_WAIT_BREL_DISC,	EV_RELEASE_CNF,		lli_received_d_relcnf},
	{ST_WAIT_BREL_DISC,	EV_RELEASE_IND,		lli_received_d_rel},
	{ST_WAIT_BREL_DISC,	EV_DLRL,		lli_got_dlrl},
	{ST_WAIT_DCOMMAND,	EV_HANGUP,		lli_send_d_disc},
	{ST_WAIT_DCOMMAND,	EV_DISCONNECT_IND,	lli_received_d_disc},
	{ST_WAIT_DCOMMAND,	EV_RELEASE_CNF,		lli_received_d_relcnf},
	{ST_WAIT_DCOMMAND,	EV_RELEASE_IND,		lli_received_d_rel},
	{ST_WAIT_DCOMMAND,	EV_DLRL,		lli_got_dlrl},
	{ST_WAIT_DRELEASE,	EV_RELEASE_IND,		lli_timeout_d},
	{ST_WAIT_DRELEASE,	EV_RELEASE_CNF,		lli_timeout_d},
	{ST_WAIT_DRELEASE,	EV_RELEASE_ERR,		lli_timeout_d},
	{ST_WAIT_DRELEASE,	EV_DIAL,		lli_no_dchan_ready},
	{ST_WAIT_DRELEASE,	EV_DLRL,		lli_got_dlrl},
	{ST_WAIT_D_REL_CNF,	EV_RELEASE_CNF,		lli_timeout_d},
	{ST_WAIT_D_REL_CNF,	EV_RELEASE_ERR,		lli_timeout_d},
/* ETS 300-104 16.1 */
	{ST_WAIT_D_REL_CNF,     EV_RELEASE_IND,         lli_timeout_d},
	{ST_WAIT_D_REL_CNF,	EV_DIAL,		lli_no_dchan_ready},
	{ST_WAIT_D_REL_CNF,	EV_DLRL,		lli_got_dlrl},
	{ST_WAIT_DSHUTDOWN,	EV_DLRL,		lli_go_null},
	{ST_WAIT_DSHUTDOWN,	EV_DLEST,		lli_d_established},
	{ST_WAIT_DSHUTDOWN,	EV_DIAL,		lli_prep_dialout},
	{ST_WAIT_DSHUTDOWN,	EV_RESUME,		lli_prep_dialout},
	{ST_WAIT_DSHUTDOWN,	EV_SETUP_IND,		lli_deliver_call},
};
/* *INDENT-ON* */


#define FNCOUNT (sizeof(fnlist)/sizeof(struct FsmNode))

HISAX_INITFUNC(void
CallcNew(void))
{
	callcfsm.state_count = STATE_COUNT;
	callcfsm.event_count = EVENT_COUNT;
	callcfsm.strEvent = strEvent;
	callcfsm.strState = strState;
	FsmNew(&callcfsm, fnlist, FNCOUNT);
}

void
CallcFree(void)
{
	FsmFree(&callcfsm);
}

static void
release_b_st(struct Channel *chanp)
{
	struct PStack *st = chanp->b_st;

	chanp->bcs->BC_Close(chanp->bcs);
	switch (chanp->l2_active_protocol) {
		case (ISDN_PROTO_L2_X75I):
			releasestack_isdnl2(st);
			break;
		case (ISDN_PROTO_L2_HDLC):
		case (ISDN_PROTO_L2_TRANS):
//		case (ISDN_PROTO_L2_MODEM):
			releasestack_transl2(st);
			break;
	}
}

struct Channel
*selectfreechannel(struct PStack *st)
{
	struct IsdnCardState *cs = st->l1.hardware;
	struct Channel *chanp = st->lli.userdata;
	int i;

	if (test_bit(FLG_TWO_DCHAN, &cs->HW_Flags))
		i=1;
	else
		i=0;
	while (i<2) {
		if (chanp->fi.state == ST_NULL)
			return (chanp);
		chanp++;
		i++;
	}
	return (NULL);
}

int
is_activ(struct PStack *st)
{
	struct IsdnCardState *cs = st->l1.hardware;
	struct Channel *chanp = st->lli.userdata;
	int i;

	if (test_bit(FLG_TWO_DCHAN, &cs->HW_Flags))
		i=1;
	else
		i=0;
	while (i<2) {
		if (test_bit(FLG_ESTAB_D, &chanp->Flags))
			return (1);
		chanp++;
		i++;
	}
	return (0);
}

static void
dchan_l3l4(struct PStack *st, int pr, void *arg)
{
	struct l3_process *pc = arg;
	struct IsdnCardState *cs = st->l1.hardware;
	struct Channel *chanp;
	int event;

	switch (pr) {
		case (DL_ESTABLISH | INDICATION):
			event = EV_DLEST;
			break;
		case (DL_RELEASE | INDICATION):
			event = EV_DLRL;
			break;
		default:
			event = -1;
			break;
	}
	if (event >= 0) {
		int i;

		chanp = st->lli.userdata;
		if (test_bit(FLG_TWO_DCHAN, &cs->HW_Flags))
			i = 1;
		else
			i = 0;
		while (i < 2) {
			FsmEvent(&chanp->fi, event, NULL);
			chanp++;
			i++;
		}
		return;
	} else if (pr == (CC_SETUP | INDICATION)) {
		if (!(chanp = selectfreechannel(pc->st))) {
			pc->st->lli.l4l3(pc->st, CC_DLRL | REQUEST, pc);
		} else {
			chanp->proc = pc;
			pc->chan = chanp;
			FsmEvent(&chanp->fi, EV_SETUP_IND, NULL);
		}
		return;
	}
	if (!(chanp = pc->chan))
		return;

	switch (pr) {
		case (CC_DISCONNECT | INDICATION):
			FsmEvent(&chanp->fi, EV_DISCONNECT_IND, NULL);
			break;
		case (CC_RELEASE | CONFIRM):
			FsmEvent(&chanp->fi, EV_RELEASE_CNF, NULL);
			break;
		case (CC_SUSPEND | CONFIRM):
			FsmEvent(&chanp->fi, EV_RELEASE_CNF, NULL);
			break;
		case (CC_RESUME | CONFIRM):
			FsmEvent(&chanp->fi, EV_SETUP_CNF, NULL);
			break;
		case (CC_RESUME_ERR):
			FsmEvent(&chanp->fi, EV_RELEASE_CNF, NULL);
			break;
		case (CC_RELEASE | INDICATION):
			FsmEvent(&chanp->fi, EV_RELEASE_IND, NULL);
			break;
		case (CC_SETUP_COMPL | INDICATION):
			FsmEvent(&chanp->fi, EV_SETUP_CMPL_IND, NULL);
			break;
		case (CC_SETUP | CONFIRM):
			FsmEvent(&chanp->fi, EV_SETUP_CNF, NULL);
			break;
		case (CC_CHARGE | INDICATION):
			FsmEvent(&chanp->fi, EV_CINF, NULL);
			break;
		case (CC_NOSETUP_RSP):
			FsmEvent(&chanp->fi, EV_NOSETUP_RSP, NULL);
			break;
		case (CC_SETUP_ERR):
			FsmEvent(&chanp->fi, EV_SETUP_ERR, NULL);
			break;
		case (CC_CONNECT_ERR):
			FsmEvent(&chanp->fi, EV_CONNECT_ERR, NULL);
			break;
		case (CC_RELEASE_ERR):
			FsmEvent(&chanp->fi, EV_RELEASE_ERR, NULL);
			break;
		case (CC_PROCEEDING | INDICATION):
		case (CC_ALERTING | INDICATION):
			break;
		default:
			if (chanp->debug & 0x800) {
				HiSax_putstatus(chanp->cs, "Ch",
					"%d L3->L4 unknown primitiv %x",
					chanp->chan, pr);
			}
	}
}

static void
init_d_st(struct Channel *chanp)
{
	struct PStack *st = chanp->d_st;
	struct IsdnCardState *cs = chanp->cs;
	char tmp[16];

	HiSax_addlist(cs, st);
	setstack_HiSax(st, cs);
	st->l2.sap = 0;
	st->l2.tei = -1;
	st->l2.flag = 0;
	test_and_set_bit(FLG_MOD128, &st->l2.flag);
	test_and_set_bit(FLG_LAPD, &st->l2.flag);
	test_and_set_bit(FLG_ORIG, &st->l2.flag);
	st->l2.maxlen = MAX_DFRAME_LEN;
	st->l2.window = 1;
	st->l2.T200 = 1000;	/* 1000 milliseconds  */
	st->l2.N200 = 3;	/* try 3 times        */
	st->l2.T203 = 10000;	/* 10000 milliseconds */
	if (test_bit(FLG_TWO_DCHAN, &cs->HW_Flags))
		sprintf(tmp, "DCh%d Q.921 ", chanp->chan);
	else
		sprintf(tmp, "DCh Q.921 ");
	setstack_isdnl2(st, tmp);
	setstack_l3dc(st, chanp);
	st->lli.userdata = chanp;
	st->lli.l2writewakeup = NULL;
	st->l3.l3l4 = dchan_l3l4;
}

static void
callc_debug(struct FsmInst *fi, char *fmt, ...)
{
	va_list args;
	struct Channel *chanp = fi->userdata;
	char tmp[16];

	va_start(args, fmt);
	sprintf(tmp, "Ch%d callc ", chanp->chan);
	VHiSax_putstatus(chanp->cs, tmp, fmt, args);
	va_end(args);
}

static void
dummy_pstack(struct PStack *st, int pr, void *arg) {
	printk(KERN_WARNING"call to dummy_pstack pr=%04x arg %lx\n", pr, (long)arg);
}

static void
init_PStack(struct PStack **stp) {
	*stp = kmalloc(sizeof(struct PStack), GFP_ATOMIC);
	(*stp)->next = NULL;
	(*stp)->l1.l1l2 = dummy_pstack;
	(*stp)->l1.l1hw = dummy_pstack;
	(*stp)->l1.l1tei = dummy_pstack;
	(*stp)->l2.l2tei = dummy_pstack;
	(*stp)->l2.l2l1 = dummy_pstack;
	(*stp)->l2.l2l3 = dummy_pstack;
	(*stp)->l3.l3l2 = dummy_pstack;
	(*stp)->l3.l3l4 = dummy_pstack;
	(*stp)->lli.l4l3 = dummy_pstack;
	(*stp)->ma.layer = dummy_pstack;
}

static void
init_chan(int chan, struct IsdnCardState *csta)
{
	struct Channel *chanp = csta->channel + chan;

	chanp->cs = csta;
	chanp->bcs = csta->bcs + chan;
	chanp->chan = chan;
	chanp->incoming = 0;
	chanp->debug = 0;
	chanp->Flags = 0;
	chanp->leased = 0;
	init_PStack(&chanp->b_st);
	chanp->b_st->l1.delay = DEFAULT_B_DELAY;
	chanp->fi.fsm = &callcfsm;
	chanp->fi.state = ST_NULL;
	chanp->fi.debug = 0;
	chanp->fi.userdata = chanp;
	chanp->fi.printdebug = callc_debug;
	FsmInitTimer(&chanp->fi, &chanp->dial_timer);
	FsmInitTimer(&chanp->fi, &chanp->drel_timer);
	if (!chan || test_bit(FLG_TWO_DCHAN, &csta->HW_Flags)) {
		init_PStack(&chanp->d_st);
		if (chan)
			csta->channel->d_st->next = chanp->d_st;
		chanp->d_st->next = NULL;
		init_d_st(chanp);
	} else {
		chanp->d_st = csta->channel->d_st;
	}
	chanp->data_open = 0;
}

int
CallcNewChan(struct IsdnCardState *csta)
{
	chancount += 2;
	init_chan(0, csta);
	init_chan(1, csta);
	printk(KERN_INFO "HiSax: 2 channels added\n");
	if (test_bit(FLG_PTP, &csta->channel->d_st->l2.flag)) {
		printk(KERN_INFO "LAYER2 WATCHING ESTABLISH\n");
		test_and_set_bit(FLG_START_D, &csta->channel->Flags);
		csta->channel->d_st->lli.l4l3(csta->channel->d_st,
			DL_ESTABLISH | REQUEST, NULL);
	}
	return (2);
}

static void
release_d_st(struct Channel *chanp)
{
	struct PStack *st = chanp->d_st;

	if (!st)
		return;
	releasestack_isdnl2(st);
	releasestack_isdnl3(st);
	HiSax_rmlist(st->l1.hardware, st);
	kfree(st);
	chanp->d_st = NULL;
}

void
CallcFreeChan(struct IsdnCardState *csta)
{
	int i;

	for (i = 0; i < 2; i++) {
		FsmDelTimer(&csta->channel[i].drel_timer, 74);
		FsmDelTimer(&csta->channel[i].dial_timer, 75);
		if (i || test_bit(FLG_TWO_DCHAN, &csta->HW_Flags))
			release_d_st(csta->channel + i);
		if (csta->channel[i].b_st) {
			if (test_and_clear_bit(FLG_START_B, &csta->channel[i].Flags))
				release_b_st(csta->channel + i);
			kfree(csta->channel[i].b_st);
			csta->channel[i].b_st = NULL;
		} else
			printk(KERN_WARNING "CallcFreeChan b_st ch%d allready freed\n", i);
		if (i || test_bit(FLG_TWO_DCHAN, &csta->HW_Flags)) {
			release_d_st(csta->channel + i);
		} else
			csta->channel[i].d_st = NULL;
	}
}

static void
lldata_handler(struct PStack *st, int pr, void *arg)
{
	struct Channel *chanp = (struct Channel *) st->lli.userdata;
	struct sk_buff *skb = arg;

	switch (pr) {
		case (DL_DATA  | INDICATION):
			if (chanp->data_open)
				chanp->cs->iif.rcvcallb_skb(chanp->cs->myid, chanp->chan, skb);
			else {
				dev_kfree_skb(skb);
			}
			break;
		case (DL_ESTABLISH | INDICATION):
		case (DL_ESTABLISH | CONFIRM):
			FsmEvent(&chanp->fi, EV_BC_EST, NULL);
			break;
		case (DL_RELEASE | INDICATION):
		case (DL_RELEASE | CONFIRM):
			FsmEvent(&chanp->fi, EV_BC_REL, NULL);
			break;
		default:
			printk(KERN_WARNING "lldata_handler unknown primitive %x\n",
			       pr);
			break;
	}
}

static void
lltrans_handler(struct PStack *st, int pr, void *arg)
{
	struct Channel *chanp = (struct Channel *) st->lli.userdata;
	struct sk_buff *skb = arg;

	switch (pr) {
		case (PH_DATA | INDICATION):
			if (chanp->data_open)
				chanp->cs->iif.rcvcallb_skb(chanp->cs->myid, chanp->chan, skb);
			else {
				link_debug(chanp, 0, "channel not open");
				dev_kfree_skb(skb);
			}
			break;
		case (PH_ACTIVATE | INDICATION):
		case (PH_ACTIVATE | CONFIRM):
			FsmEvent(&chanp->fi, EV_BC_EST, NULL);
			break;
		case (PH_DEACTIVATE | INDICATION):
		case (PH_DEACTIVATE | CONFIRM):
			FsmEvent(&chanp->fi, EV_BC_REL, NULL);
			break;
		default:
			printk(KERN_WARNING "lltrans_handler unknown primitive %x\n",
			       pr);
			break;
	}
}

static void
ll_writewakeup(struct PStack *st, int len)
{
	struct Channel *chanp = st->lli.userdata;
	isdn_ctrl ic;

	ic.driver = chanp->cs->myid;
	ic.command = ISDN_STAT_BSENT;
	ic.arg = chanp->chan;
//	ic.parm.length = len;
	chanp->cs->iif.statcallb(&ic);
}

static int
init_b_st(struct Channel *chanp, int incoming)
{
	struct PStack *st = chanp->b_st;
	struct IsdnCardState *cs = chanp->cs;
	char tmp[16];

	st->l1.hardware = cs;
	if (chanp->leased)
		st->l1.bc = chanp->chan & 1;
	else
		st->l1.bc = chanp->proc->para.bchannel - 1;
	switch (chanp->l2_active_protocol) {
		case (ISDN_PROTO_L2_X75I):
		case (ISDN_PROTO_L2_HDLC):
			st->l1.mode = L1_MODE_HDLC;
			break;
		case (ISDN_PROTO_L2_TRANS):
			st->l1.mode = L1_MODE_TRANS;
			break;
#if 0
		case (ISDN_PROTO_L2_MODEM):
			st->l1.mode = L1_MODE_MODEM;
			break;
#endif
	}
	if (chanp->bcs->BC_SetStack(st, chanp->bcs))
		return (-1);
	st->l2.flag = 0;
	test_and_set_bit(FLG_LAPB, &st->l2.flag);
	st->l2.maxlen = MAX_DATA_SIZE;
	if (!incoming)
		test_and_set_bit(FLG_ORIG, &st->l2.flag);
	st->l2.T200 = 1000;	/* 1000 milliseconds */
	st->l2.window = 7;
	st->l2.N200 = 4;	/* try 4 times       */
	st->l2.T203 = 5000;	/* 5000 milliseconds */
	st->l3.debug = 0;
	switch (chanp->l2_active_protocol) {
		case (ISDN_PROTO_L2_X75I):
			sprintf(tmp, "Ch%d X.75", chanp->chan);
			setstack_isdnl2(st, tmp);
			setstack_l3bc(st, chanp);
			st->l2.l2l3 = lldata_handler;
			st->lli.userdata = chanp;
			st->lli.l1writewakeup = NULL;
			st->lli.l2writewakeup = ll_writewakeup;
			st->l2.l2m.debug = chanp->debug & 16;
			st->l2.debug = chanp->debug & 64;
			break;
		case (ISDN_PROTO_L2_HDLC):
		case (ISDN_PROTO_L2_TRANS):
//		case (ISDN_PROTO_L2_MODEM):
			st->l1.l1l2 = lltrans_handler;
			st->lli.userdata = chanp;
			st->lli.l1writewakeup = ll_writewakeup;
			setstack_transl2(st);
			setstack_l3bc(st, chanp);
			break;
	}
	return (0);
}

static void
leased_l4l3(struct PStack *st, int pr, void *arg)
{
	struct Channel *chanp = (struct Channel *) st->lli.userdata;
	struct sk_buff *skb = arg;

	switch (pr) {
		case (DL_DATA | REQUEST):
			link_debug(chanp, 0, "leased line d-channel DATA");
			dev_kfree_skb(skb);
			break;
		case (DL_ESTABLISH | REQUEST):
			st->l2.l2l1(st, PH_ACTIVATE | REQUEST, NULL);
			break;
		case (DL_RELEASE | REQUEST):
			break;
		default:
			printk(KERN_WARNING "transd_l4l3 unknown primitive %x\n",
			       pr);
			break;
	}
}

static void
leased_l1l2(struct PStack *st, int pr, void *arg)
{
	struct Channel *chanp = (struct Channel *) st->lli.userdata;
	struct sk_buff *skb = arg;
	int i,event = EV_DLRL;

	switch (pr) {
		case (PH_DATA | INDICATION):
			link_debug(chanp, 0, "leased line d-channel DATA");
			dev_kfree_skb(skb);
			break;
		case (PH_ACTIVATE | INDICATION):
		case (PH_ACTIVATE | CONFIRM):
			event = EV_DLEST;
		case (PH_DEACTIVATE | INDICATION):
		case (PH_DEACTIVATE | CONFIRM):
			if (test_bit(FLG_TWO_DCHAN, &chanp->cs->HW_Flags))
				i = 1;
			else
				i = 0;
			while (i < 2) {
				FsmEvent(&chanp->fi, event, NULL);
				chanp++;
				i++;
			}
			break;
		default:
			printk(KERN_WARNING
				"transd_l1l2 unknown primitive %x\n", pr);
			break;
	}
}

static void
channel_report(struct Channel *chanp)
{
}

static void
distr_debug(struct IsdnCardState *csta, int debugflags)
{
	int i;
	struct Channel *chanp = csta->channel;

	for (i = 0; i < 2; i++) {
		chanp[i].debug = debugflags;
		chanp[i].fi.debug = debugflags & 2;
		chanp[i].d_st->l2.l2m.debug = debugflags & 8;
		chanp[i].b_st->l2.l2m.debug = debugflags & 0x10;
		chanp[i].d_st->l2.debug = debugflags & 0x20;
		chanp[i].b_st->l2.debug = debugflags & 0x40;
		chanp[i].d_st->l3.l3m.debug = debugflags & 0x80;
		chanp[i].b_st->l3.l3m.debug = debugflags & 0x100;
		chanp[i].b_st->ma.tei_m.debug = debugflags & 0x200;
		chanp[i].b_st->ma.debug = debugflags & 0x200;
		chanp[i].d_st->l1.l1m.debug = debugflags & 0x1000;
		chanp[i].b_st->l1.l1m.debug = debugflags & 0x2000;
	}
	if (debugflags & 4)
		csta->debug |= DEB_DLOG_HEX;
	else
		csta->debug &= ~DEB_DLOG_HEX;
}

static char tmpbuf[256];

static void
capi_debug(struct Channel *chanp, capi_msg *cm)
{
	char *t = tmpbuf;

	t += sprintf(tmpbuf, "%d CAPIMSG", chanp->chan);
	t += QuickHex(t, (u_char *)cm, (cm->Length>50)? 50: cm->Length);
	t--;
	*t= 0;
	HiSax_putstatus(chanp->cs, "Ch", "%d CAPIMSG %s", chanp->chan, tmpbuf);
}

void
lli_got_fac_req(struct Channel *chanp, capi_msg *cm) {
	if ((cm->para[0] != 3) || (cm->para[1] != 0))
		return;
	if (cm->para[2]<3)
		return;
	if (cm->para[4] != 0)
		return;
	switch(cm->para[3]) {
		case 4: /* Suspend */
			if (cm->para[5]) {
				strncpy(chanp->setup.phone, &cm->para[5], cm->para[5] +1);
				FsmEvent(&chanp->fi, EV_SUSPEND, cm);
			}
			break;
		case 5: /* Resume */
			if (cm->para[5]) {
				strncpy(chanp->setup.phone, &cm->para[5], cm->para[5] +1);
				if (chanp->fi.state == ST_NULL) {
					FsmEvent(&chanp->fi, EV_RESUME, cm);
				} else {
					FsmDelTimer(&chanp->dial_timer, 72);
					FsmAddTimer(&chanp->dial_timer, 80, EV_RESUME, cm, 73);
				}
			}
			break;
	}
}

void
lli_got_manufacturer(struct Channel *chanp, struct IsdnCardState *cs, capi_msg *cm) {
	if ((cs->typ == ISDN_CTYPE_ELSA) || (cs->typ == ISDN_CTYPE_ELSA_PNP) ||
		(cs->typ == ISDN_CTYPE_ELSA_PCI)) {
		if (cs->hw.elsa.MFlag) {
			cs->cardmsg(cs, CARD_AUX_IND, cm->para);
		}
	}
}

int
HiSax_command(isdn_ctrl * ic)
{
	struct IsdnCardState *csta = hisax_findcard(ic->driver);
	struct Channel *chanp;
	int i;
	u_int num;
	u_long adr;

	if (!csta) {
		printk(KERN_ERR
		"HiSax: if_command %d called with invalid driverId %d!\n",
		       ic->command, ic->driver);
		return -ENODEV;
	}

	switch (ic->command) {
		case (ISDN_CMD_SETEAZ):
			chanp = csta->channel + ic->arg;
			break;

		case (ISDN_CMD_SETL2):
			chanp = csta->channel + (ic->arg & 0xff);
			if (chanp->debug & 1)
				link_debug(chanp, 1, "SETL2 card %d %ld",
					csta->cardnr + 1, ic->arg >> 8);
			chanp->l2_protocol = ic->arg >> 8;
			break;
		case (ISDN_CMD_SETL3):
			chanp = csta->channel + (ic->arg & 0xff);
			if (chanp->debug & 1)
				link_debug(chanp, 1, "SETL3 card %d %ld",
					csta->cardnr + 1, ic->arg >> 8);
			chanp->l3_protocol = ic->arg >> 8;
			break;
		case (ISDN_CMD_DIAL):
			chanp = csta->channel + (ic->arg & 0xff);
			if (chanp->debug & 1)
				link_debug(chanp, 1, "DIAL %s -> %s (%d,%d)",
					ic->parm.setup.eazmsn, ic->parm.setup.phone,
					ic->parm.setup.si1, ic->parm.setup.si2);
			chanp->setup = ic->parm.setup;
			if (!strcmp(chanp->setup.eazmsn, "0"))
				chanp->setup.eazmsn[0] = '\0';
			/* this solution is dirty and may be change, if
			 * we make a callreference based callmanager */
			if (chanp->fi.state == ST_NULL) {
				FsmEvent(&chanp->fi, EV_DIAL, NULL);
			} else {
				FsmDelTimer(&chanp->dial_timer, 70);
				FsmAddTimer(&chanp->dial_timer, 50, EV_DIAL, NULL, 71);
			}
			break;
		case (ISDN_CMD_ACCEPTB):
			chanp = csta->channel + ic->arg;
			if (chanp->debug & 1)
				link_debug(chanp, 1, "ACCEPTB");
			FsmEvent(&chanp->fi, EV_ACCEPTB, NULL);
			break;
		case (ISDN_CMD_ACCEPTD):
			chanp = csta->channel + ic->arg;
			if (chanp->debug & 1)
				link_debug(chanp, 1, "ACCEPTD");
			FsmEvent(&chanp->fi, EV_ACCEPTD, NULL);
			break;
		case (ISDN_CMD_HANGUP):
			chanp = csta->channel + ic->arg;
			if (chanp->debug & 1)
				link_debug(chanp, 1, "HANGUP");
			FsmEvent(&chanp->fi, EV_HANGUP, NULL);
			break;
		case (CAPI_PUT_MESSAGE):
			chanp = csta->channel + ic->arg;
			if (chanp->debug & 1)
				capi_debug(chanp, &ic->parm.cmsg);
			if (ic->parm.cmsg.Length < 8)
				break;
			switch(ic->parm.cmsg.Command) {
				case CAPI_FACILITY:
					if (ic->parm.cmsg.Subcommand == CAPI_REQ)
						lli_got_fac_req(chanp, &ic->parm.cmsg);
					break;
				case CAPI_MANUFACTURER:
					if (ic->parm.cmsg.Subcommand == CAPI_REQ)
						lli_got_manufacturer(chanp, csta, &ic->parm.cmsg);
					break;
				default:
					break;
			}
			break;
		case (ISDN_CMD_LOCK):
			HiSax_mod_inc_use_count();
#ifdef MODULE
			if (csta->channel[0].debug & 0x400)
				HiSax_putstatus(csta, "   LOCK ", "modcnt %x",
					MOD_USE_COUNT);
#endif				/* MODULE */
			break;
		case (ISDN_CMD_UNLOCK):
			HiSax_mod_dec_use_count();
#ifdef MODULE
			if (csta->channel[0].debug & 0x400)
				HiSax_putstatus(csta, " UNLOCK ", "modcnt %x",
					MOD_USE_COUNT);
#endif				/* MODULE */
			break;
		case (ISDN_CMD_IOCTL):
			switch (ic->arg) {
				case (0):
					HiSax_reportcard(csta->cardnr);
					for (i = 0; i < 2; i++)
						channel_report(&csta->channel[i]);
					break;
				case (1):
					num = *(unsigned int *) ic->parm.num;
					distr_debug(csta, num);
					printk(KERN_DEBUG "HiSax: debugging flags card %d set to %x\n",
						csta->cardnr + 1, num);
					HiSax_putstatus(csta, "debugging flags ",
						"card %d set to %x", csta->cardnr + 1, num);
					break;
				case (2):
					num = *(unsigned int *) ic->parm.num;
					csta->channel[0].b_st->l1.delay = num;
					csta->channel[1].b_st->l1.delay = num;
					HiSax_putstatus(csta, "delay ", "card %d set to %d ms",
						csta->cardnr + 1, num);
					printk(KERN_DEBUG "HiSax: delay card %d set to %d ms\n",
						csta->cardnr + 1, num);
					break;
				case (3):
					for (i = 0; i < *(unsigned int *) ic->parm.num; i++)
						HiSax_mod_dec_use_count();
					break;
				case (4):
					for (i = 0; i < *(unsigned int *) ic->parm.num; i++)
						HiSax_mod_inc_use_count();
					break;
				case (5):	/* set card in leased mode */
					num = *(unsigned int *) ic->parm.num;
					if ((num <1) || (num > 2)) {
						HiSax_putstatus(csta, "Set LEASED ",
							"wrong channel %d", num);
						printk(KERN_WARNING "HiSax: Set LEASED wrong channel %d\n",
							num);
					} else {
						num--;
						chanp = csta->channel +num;
						chanp->leased = 1;
						HiSax_putstatus(csta, "Card",
							"%d channel %d set leased mode\n",
							csta->cardnr + 1, num + 1);
						chanp->d_st->l1.l1l2 = leased_l1l2;
						chanp->d_st->lli.l4l3 = leased_l4l3;
						chanp->d_st->lli.l4l3(chanp->d_st,
							DL_ESTABLISH | REQUEST, NULL);
					}
					break;
				case (6):	/* set B-channel test loop */
					num = *(unsigned int *) ic->parm.num;
					if (csta->stlist)
						csta->stlist->l2.l2l1(csta->stlist,
							PH_TESTLOOP | REQUEST, (void *) (long)num);
					break;
				case (7):	/* set card in PTP mode */
					num = *(unsigned int *) ic->parm.num;
					if (test_bit(FLG_TWO_DCHAN, &csta->HW_Flags)) {
						printk(KERN_ERR "HiSax PTP mode only with one TEI possible\n");
					} else if (num) {
						test_and_set_bit(FLG_PTP, &csta->channel[0].d_st->l2.flag);
						test_and_set_bit(FLG_FIXED_TEI, &csta->channel[0].d_st->l2.flag);
						csta->channel[0].d_st->l2.tei = 0;
						HiSax_putstatus(csta, "set card ", "in PTP mode");
						printk(KERN_DEBUG "HiSax: set card in PTP mode\n");
						printk(KERN_INFO "LAYER2 WATCHING ESTABLISH\n");
						test_and_set_bit(FLG_START_D, &csta->channel[0].Flags);
						test_and_set_bit(FLG_START_D, &csta->channel[1].Flags);
						csta->channel[0].d_st->lli.l4l3(csta->channel[0].d_st,
							DL_ESTABLISH | REQUEST, NULL);
					} else {
						test_and_clear_bit(FLG_PTP, &csta->channel[0].d_st->l2.flag);
						test_and_clear_bit(FLG_FIXED_TEI, &csta->channel[0].d_st->l2.flag);
						HiSax_putstatus(csta, "set card ", "in PTMP mode");
						printk(KERN_DEBUG "HiSax: set card in PTMP mode\n");
					}
					break;
				case (8):	/* set card in FIXED TEI mode */
					num = *(unsigned int *) ic->parm.num;
					chanp = csta->channel + (num & 1);
					num = num >>1;
					test_and_set_bit(FLG_FIXED_TEI, &chanp->d_st->l2.flag);
					chanp->d_st->l2.tei = num;
					HiSax_putstatus(csta, "set card ", "in FIXED TEI (%d) mode", num);
					printk(KERN_DEBUG "HiSax: set card in FIXED TEI (%d) mode\n",
						num);
					break;
				case (9): /* load firmware */
					memcpy(&adr, ic->parm.num, sizeof(ulong));
					csta->cardmsg(csta, CARD_LOAD_FIRM,
						(void *) adr);
					break;
#ifdef MODULE
				case (55):
					while ( MOD_USE_COUNT > 0)
						MOD_DEC_USE_COUNT;
					HiSax_mod_inc_use_count();
					break;
#endif				/* MODULE */
				case (11):
					num = csta->debug & DEB_DLOG_HEX;
					csta->debug = *(unsigned int *) ic->parm.num;
					csta->debug |= num;
					HiSax_putstatus(cards[0].cs, "l1 debugging ",
						"flags card %d set to %x",
						csta->cardnr + 1, csta->debug);
					printk(KERN_DEBUG "HiSax: l1 debugging flags card %d set to %x\n",
						csta->cardnr + 1, csta->debug);
					break;
				case (13):
					csta->channel[0].d_st->l3.debug = *(unsigned int *) ic->parm.num;
					csta->channel[1].d_st->l3.debug = *(unsigned int *) ic->parm.num;
					HiSax_putstatus(cards[0].cs, "l3 debugging ",
						"flags card %d set to %x\n", csta->cardnr + 1,
						*(unsigned int *) ic->parm.num);
					printk(KERN_DEBUG "HiSax: l3 debugging flags card %d set to %x\n",
						csta->cardnr + 1, *(unsigned int *) ic->parm.num);
					break;
				default:
					printk(KERN_DEBUG "HiSax: invalid ioclt %d\n",
					       (int) ic->arg);
					return (-EINVAL);
			}
			break;
		default:
			break;
	}

	return (0);
}

int
HiSax_writebuf_skb(int id, int chan, int ack, struct sk_buff *skb)
{
	struct IsdnCardState *csta = hisax_findcard(id);
	struct Channel *chanp;
	struct PStack *st;
	int len = skb->len;
	unsigned long flags;
	struct sk_buff *nskb;

	if (!csta) {
		printk(KERN_ERR
		    "HiSax: if_sendbuf called with invalid driverId!\n");
		return -ENODEV;
	}
	chanp = csta->channel + chan;
	st = chanp->b_st;
	if (!chanp->data_open) {
		link_debug(chanp, 1, "writebuf: channel not open");
		return -EIO;
	}
	if (len > MAX_DATA_SIZE) {
		link_debug(chanp, 1, "writebuf: packet too large (%d bytes)", len);
		printk(KERN_WARNING "HiSax_writebuf: packet too large (%d bytes) !\n",
			len);
		return -EINVAL;
	}
	if (len) {
		if ((len + chanp->bcs->tx_cnt) > MAX_DATA_MEM) {
			/* Must return 0 here, since this is not an error
			 * but a temporary lack of resources.
			 */
			if (chanp->debug & 0x800)
				link_debug(chanp, 1, "writebuf: no buffers for %d bytes", len);
			return 0;
		}
		save_flags(flags);
		cli();
		nskb = skb_clone(skb, GFP_ATOMIC);
		if (nskb) {
			if (!ack)
				nskb->pkt_type = PACKET_NOACK;
			if (chanp->l2_active_protocol == ISDN_PROTO_L2_X75I)
				st->l3.l3l2(st, DL_DATA | REQUEST, nskb);
			else {
				chanp->bcs->tx_cnt += len;
				st->l2.l2l1(st, PH_DATA | REQUEST, nskb);
			}
			dev_kfree_skb(skb);
		} else
			len = 0;
		restore_flags(flags);
	}
	return (len);
}
