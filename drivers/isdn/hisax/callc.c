/* $Id: callc.c,v 1.30 1997/05/29 10:40:43 keil Exp $

 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *              based on the teles driver from Jan den Ouden
 *
 * Thanks to    Jan den Ouden
 *              Fritz Elfert
 *
 * $Log: callc.c,v $
 * Revision 1.30  1997/05/29 10:40:43  keil
 * chanp->impair was uninitialised
 *
 * Revision 1.29  1997/04/23 20:09:49  fritz
 * Removed tmp, used by removed debugging code.
 *
 * Revision 1.28  1997/04/21 13:42:25  keil
 * Remove unneeded debug
 *
 * Revision 1.27  1997/04/16 14:21:01  keil
 * remove unused variable
 *
 * Revision 1.26  1997/04/13 19:55:21  keil
 * Changes in debugging code
 *
 * Revision 1.25  1997/04/06 22:54:08  keil
 * Using SKB's
 *
 * Revision 1.24  1997/03/05 11:28:03  keil
 * fixed undefined l2tei procedure
 * a layer1 release delete now the drel timer
 *
 * Revision 1.23  1997/03/04 23:07:42  keil
 * bugfix dial parameter
 *
 * Revision 1.22  1997/02/27 13:51:55  keil
 * Reset B-channel (dlc) statemachine in every release
 *
 * Revision 1.21  1997/02/19 09:24:27  keil
 * Bugfix: Hangup to LL if a ttyI rings
 *
 * Revision 1.20  1997/02/17 00:32:47  keil
 * Bugfix: No Busy reported to LL
 *
 * Revision 1.19  1997/02/14 12:23:10  fritz
 * Added support for new insmod parameter handling.
 *
 * Revision 1.18  1997/02/11 01:36:58  keil
 * Changed setup-interface (incoming and outgoing), cause reporting
 *
 * Revision 1.17  1997/02/09 00:23:10  keil
 * new interface handling, one interface per card
 * some changes in debug and leased line mode
 *
 * Revision 1.16  1997/01/27 23:17:03  keil
 * delete timers while unloading
 *
 * Revision 1.15  1997/01/27 16:00:38  keil
 * D-channel shutdown delay; improved callback
 *
 * Revision 1.14  1997/01/21 22:16:39  keil
 * new statemachine; leased line support; cleanup for 2.0
 *
 * Revision 1.13  1996/12/08 19:51:17  keil
 * bugfixes from Pekka Sarnila
 *
 * Revision 1.12  1996/11/26 20:20:03  keil
 * fixed warning while compile
 *
 * Revision 1.11  1996/11/26 18:43:17  keil
 * change ioctl 555 --> 55 (555 didn't work)
 *
 * Revision 1.10  1996/11/26 18:06:07  keil
 * fixed missing break statement,ioctl 555 reset modcount
 *
 * Revision 1.9  1996/11/18 20:23:19  keil
 * log writebuf channel not open changed
 *
 * Revision 1.8  1996/11/06 17:43:17  keil
 * more changes for 2.1.X;block fixed ST_PRO_W
 *
 * Revision 1.7  1996/11/06 15:13:51  keil
 * typo 0x64 --->64 in debug code
 *
 * Revision 1.6  1996/11/05 19:40:33  keil
 * X.75 windowsize
 *
 * Revision 1.5  1996/10/30 10:11:06  keil
 * debugging LOCK changed;ST_REL_W EV_HANGUP added
 *
 * Revision 1.4  1996/10/27 22:20:16  keil
 * alerting bugfixes
 * no static b-channel<->channel mapping
 *
 * Revision 1.2  1996/10/16 21:29:45  keil
 * compile bug as "not module"
 * Callback with euro
 *
 * Revision 1.1  1996/10/13 20:04:50  keil
 * Initial revision
 *
 */

#define __NO_VERSION__
#include "hisax.h"

#ifdef MODULE
#if (LINUX_VERSION_CODE < 0x020111)
extern long mod_use_count_;
#define MOD_USE_COUNT mod_use_count_
#else
#define MOD_USE_COUNT ((&__this_module)->usecount)
#endif
#endif				/* MODULE */

const char *l4_revision = "$Revision: 1.30 $";

extern struct IsdnCard cards[];
extern int nrcards;
extern void HiSax_mod_dec_use_count(void);
extern void HiSax_mod_inc_use_count(void);

static int init_ds(struct Channel *chanp, int incoming);
static void release_ds(struct Channel *chanp);

static struct Fsm callcfsm =
{NULL, 0, 0};
static struct Fsm lcfsm =
{NULL, 0, 0};

static int chancount = 0;

/* Flags for remembering action done in l4 */

#define  FLG_START_D	0x0001
#define  FLG_ESTAB_D	0x0002
#define  FLG_CALL_SEND	0x0004
#define  FLG_CALL_REC   0x0008
#define  FLG_CALL_ALERT	0x0010
#define  FLG_START_B	0x0020
#define  FLG_CONNECT_B	0x0040
#define  FLG_LL_DCONN	0x0080
#define  FLG_LL_BCONN	0x0100
#define  FLG_DISC_SEND	0x0200
#define  FLG_DISC_REC	0x0400
#define  FLG_REL_REC	0x0800

#define  SETBIT(flg, item)  flg |= item
#define  RESBIT(flg, item)  flg &= (~item)

/*
 * Because of callback it's a good idea to delay the shutdown of the d-channel
 */
#define	DREL_TIMER_VALUE 30000

/*
 * Find card with given driverId
 */
static inline struct IsdnCardState
*
hisax_findcard(int driverid)
{
	int i;

	for (i = 0; i < nrcards; i++)
		if (cards[i].sp)
			if (cards[i].sp->myid == driverid)
				return (cards[i].sp);
	return (struct IsdnCardState *) 0;
}

static void
link_debug(struct Channel *chanp, char *s, int direction)
{
	char tmp[100], tm[32];

	jiftime(tm, jiffies);
	sprintf(tmp, "%s Channel %d %s %s\n", tm, chanp->chan,
		direction ? "LL->HL" : "HL->LL", s);
	HiSax_putstatus(chanp->sp, tmp);
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
	EV_DATAIN,		/* 13 */
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
	"EV_DATAIN",
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

enum {
	ST_LC_NULL,
	ST_LC_ACTIVATE_WAIT,
	ST_LC_DELAY,
	ST_LC_ESTABLISH_WAIT,
	ST_LC_CONNECTED,
	ST_LC_FLUSH_WAIT,
	ST_LC_FLUSH_DELAY,
	ST_LC_RELEASE_WAIT,
};

#define LC_STATE_COUNT (ST_LC_RELEASE_WAIT+1)

static char *strLcState[] =
{
	"ST_LC_NULL",
	"ST_LC_ACTIVATE_WAIT",
	"ST_LC_DELAY",
	"ST_LC_ESTABLISH_WAIT",
	"ST_LC_CONNECTED",
	"ST_LC_FLUSH_WAIT",
	"ST_LC_FLUSH_DELAY",
	"ST_LC_RELEASE_WAIT",
};

enum {
	EV_LC_ESTABLISH,
	EV_LC_PH_ACTIVATE,
	EV_LC_PH_DEACTIVATE,
	EV_LC_DL_ESTABLISH,
	EV_LC_TIMER,
	EV_LC_DL_FLUSH,
	EV_LC_DL_RELEASE,
	EV_LC_FLUSH,
	EV_LC_RELEASE,
};

#define LC_EVENT_COUNT (EV_LC_RELEASE+1)

static char *strLcEvent[] =
{
	"EV_LC_ESTABLISH",
	"EV_LC_PH_ACTIVATE",
	"EV_LC_PH_DEACTIVATE",
	"EV_LC_DL_ESTABLISH",
	"EV_LC_TIMER",
	"EV_LC_DL_FLUSH",
	"EV_LC_DL_RELEASE",
	"EV_LC_FLUSH",
	"EV_LC_RELEASE",
};

#define LC_D  0
#define LC_B  1

static inline void
l4_deliver_cause(struct Channel *chanp)
{
	isdn_ctrl ic;

	if (chanp->para.cause < 0)
		return;
	ic.driver = chanp->sp->myid;
	ic.command = ISDN_STAT_CAUSE;
	ic.arg = chanp->chan;
	if (chanp->sp->protocol == ISDN_PTYPE_EURO)
		sprintf(ic.parm.num, "E%02X%02X", chanp->para.loc & 0x7f,
			chanp->para.cause & 0x7f);
	else
		sprintf(ic.parm.num, "%02X%02X", chanp->para.loc & 0x7f,
			chanp->para.cause & 0x7f);
	chanp->sp->iif.statcallb(&ic);
}

/*
 * Dial out
 */
static void
l4_prep_dialout(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	FsmChangeState(fi, ST_OUT_WAIT_D);
	FsmDelTimer(&chanp->drel_timer, 60);
	FsmDelTimer(&chanp->dial_timer, 73);

	chanp->l2_active_protocol = chanp->l2_protocol;
	chanp->incoming = 0;
	chanp->lc_b.l2_start = !0;
	switch (chanp->l2_active_protocol) {
		case (ISDN_PROTO_L2_X75I):
			chanp->lc_b.l2_establish = !0;
			break;
		case (ISDN_PROTO_L2_HDLC):
		case (ISDN_PROTO_L2_TRANS):
			chanp->lc_b.l2_establish = 0;
			break;
		default:
			printk(KERN_WARNING "l4_prep_dialout unknown protocol\n");
			break;
	}
	if (chanp->Flags & FLG_ESTAB_D) {
		FsmEvent(fi, EV_DLEST, NULL);
	} else {
		chanp->Flags = FLG_START_D;
		if (chanp->leased) {
			chanp->lc_d.l2_establish = 0;
		}
		FsmEvent(&chanp->lc_d.lcfi, EV_LC_ESTABLISH, NULL);
	}
}

static void
l4_do_dialout(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	FsmChangeState(fi, ST_OUT_DIAL);
	if (chanp->leased) {
		chanp->para.bchannel = (chanp->chan & 1) + 1;
		FsmEvent(&chanp->fi, EV_SETUP_CNF, NULL);
	} else {
		SETBIT(chanp->Flags, FLG_ESTAB_D);
		chanp->para.callref = chanp->outcallref;
		chanp->outcallref++;
		if (chanp->outcallref == 128)
			chanp->outcallref = 64;
		chanp->is.l4.l4l3(&chanp->is, CC_SETUP_REQ, NULL);
		SETBIT(chanp->Flags, FLG_CALL_SEND);
	}
}

static void
l4_init_bchan_out(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	FsmChangeState(fi, ST_WAIT_BCONN);
	SETBIT(chanp->Flags, FLG_LL_DCONN);
	if (chanp->debug & 1)
		link_debug(chanp, "STAT_DCONN", 0);
	ic.driver = chanp->sp->myid;
	ic.command = ISDN_STAT_DCONN;
	ic.arg = chanp->chan;
	chanp->sp->iif.statcallb(&ic);
	init_ds(chanp, 0);
	SETBIT(chanp->Flags, FLG_START_B);
	FsmEvent(&chanp->lc_b.lcfi, EV_LC_ESTABLISH, NULL);
}

static void
l4_go_active(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	FsmChangeState(fi, ST_ACTIVE);
	chanp->data_open = !0;
	SETBIT(chanp->Flags, FLG_CONNECT_B);
	if (chanp->debug & 1)
		link_debug(chanp, "STAT_BCONN", 0);
	SETBIT(chanp->Flags, FLG_LL_BCONN);
	ic.driver = chanp->sp->myid;
	ic.command = ISDN_STAT_BCONN;
	ic.arg = chanp->chan;
	chanp->sp->iif.statcallb(&ic);
}

/* incomming call */

static void
l4_start_dchan(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	FsmChangeState(fi, ST_IN_WAIT_D);
	FsmDelTimer(&chanp->drel_timer, 61);
	if (chanp->Flags & FLG_ESTAB_D) {
		FsmEvent(fi, EV_DLEST, NULL);
	} else {
		chanp->Flags = FLG_START_D;
		FsmEvent(&chanp->lc_d.lcfi, EV_LC_ESTABLISH, NULL);
	}
}

static void
l4_deliver_call(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;
	int ret;
	char txt[32];

	/*
	 * Report incoming calls only once to linklevel, use CallFlags
	 * which is set to 3 with each broadcast message in isdnl1.c
	 * and resetted if a interface  answered the STAT_ICALL.
	 */
	if ((chanp->sp) && (chanp->sp->CallFlags == 3)) {
		FsmChangeState(fi, ST_IN_WAIT_LL);
		SETBIT(chanp->Flags, FLG_ESTAB_D);
		SETBIT(chanp->Flags, FLG_CALL_REC);
		if (chanp->debug & 1)
			link_debug(chanp, "STAT_ICALL", 0);
		ic.driver = chanp->sp->myid;
		ic.command = ISDN_STAT_ICALL;
		ic.arg = chanp->chan;
		/*
		 * No need to return "unknown" for calls without OAD,
		 * cause that's handled in linklevel now (replaced by '0')
		 */
		ic.parm.setup = chanp->para.setup;
		ret = chanp->sp->iif.statcallb(&ic);
		if (chanp->debug & 1) {
			sprintf(txt, "statcallb ret=%d", ret);
			link_debug(chanp, txt, 1);
		}
		if (ret)	/* if a interface knows this call, reset the CallFlag
				   * to avoid a second Call report to the linklevel
				 */
			chanp->sp->CallFlags &= ~(chanp->chan + 1);
		switch (ret) {
			case 1:	/* OK, anybody likes this call */
				FsmChangeState(fi, ST_IN_ALERT_SEND);
				SETBIT(chanp->Flags, FLG_CALL_ALERT);
				chanp->is.l4.l4l3(&chanp->is, CC_ALERTING_REQ, NULL);
				break;
			case 2:	/* Rejecting Call */
				RESBIT(chanp->Flags, FLG_CALL_REC);
				break;
			case 0:	/* OK, nobody likes this call */
			default:	/* statcallb problems */
				chanp->is.l4.l4l3(&chanp->is, CC_IGNORE, NULL);
				FsmChangeState(fi, ST_NULL);
				chanp->Flags = FLG_ESTAB_D;
				FsmAddTimer(&chanp->drel_timer, DREL_TIMER_VALUE, EV_SHUTDOWN_D, NULL, 61);
				break;
		}
	} else {
		chanp->is.l4.l4l3(&chanp->is, CC_IGNORE, NULL);
		FsmChangeState(fi, ST_NULL);
		chanp->Flags = FLG_ESTAB_D;
		FsmAddTimer(&chanp->drel_timer, DREL_TIMER_VALUE, EV_SHUTDOWN_D, NULL, 62);
	}
}

static void
l4_send_dconnect(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	FsmChangeState(fi, ST_IN_WAIT_CONN_ACK);
	chanp->is.l4.l4l3(&chanp->is, CC_SETUP_RSP, NULL);
}

static void
l4_init_bchan_in(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	FsmChangeState(fi, ST_WAIT_BCONN);
	SETBIT(chanp->Flags, FLG_LL_DCONN);
	if (chanp->debug & 1)
		link_debug(chanp, "STAT_DCONN", 0);
	ic.driver = chanp->sp->myid;
	ic.command = ISDN_STAT_DCONN;
	ic.arg = chanp->chan;
	chanp->sp->iif.statcallb(&ic);
	chanp->l2_active_protocol = chanp->l2_protocol;
	chanp->incoming = !0;
	chanp->lc_b.l2_start = 0;
	switch (chanp->l2_active_protocol) {
		case (ISDN_PROTO_L2_X75I):
			chanp->lc_b.l2_establish = !0;
			break;
		case (ISDN_PROTO_L2_HDLC):
		case (ISDN_PROTO_L2_TRANS):
			chanp->lc_b.l2_establish = 0;
			break;
		default:
			printk(KERN_WARNING "r9 unknown protocol\n");
			break;
	}
	init_ds(chanp, !0);
	SETBIT(chanp->Flags, FLG_START_B);
	FsmEvent(&chanp->lc_b.lcfi, EV_LC_ESTABLISH, NULL);
}

/* Call clearing */

static void
l4_reject_call(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	FsmChangeState(fi, ST_WAIT_DRELEASE);
	chanp->para.cause = 0x15;	/* Call Rejected */
	chanp->is.l4.l4l3(&chanp->is, CC_REJECT_REQ, NULL);
	SETBIT(chanp->Flags, FLG_DISC_SEND);
}

static void
l4_cancel_call(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	FsmChangeState(fi, ST_WAIT_DRELEASE);
	if (chanp->Flags & FLG_LL_BCONN) {
		if (chanp->debug & 1)
			link_debug(chanp, "STAT_BHUP", 0);
		RESBIT(chanp->Flags, FLG_LL_BCONN);
		ic.driver = chanp->sp->myid;
		ic.command = ISDN_STAT_BHUP;
		ic.arg = chanp->chan;
		chanp->sp->iif.statcallb(&ic);
	}
	if (chanp->Flags & FLG_START_B) {
		release_ds(chanp);
		RESBIT(chanp->Flags, FLG_START_B);
	}
	chanp->para.cause = 0x10;	/* Normal Call Clearing */
	chanp->is.l4.l4l3(&chanp->is, CC_DISCONNECT_REQ, NULL);
	SETBIT(chanp->Flags, FLG_DISC_SEND);
}

static void
l4_shutdown_d(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	FsmChangeState(fi, ST_WAIT_DSHUTDOWN);
	FsmDelTimer(&chanp->drel_timer, 62);
	RESBIT(chanp->Flags, FLG_ESTAB_D);
	FsmEvent(&chanp->lc_d.lcfi, EV_LC_RELEASE, NULL);
}

static void
l4_timeout_d(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	if (chanp->Flags & FLG_LL_DCONN) {
		if (chanp->debug & 1)
			link_debug(chanp, "STAT_DHUP", 0);
		RESBIT(chanp->Flags, FLG_LL_DCONN);
		l4_deliver_cause(chanp);
		ic.driver = chanp->sp->myid;
		ic.command = ISDN_STAT_DHUP;
		ic.arg = chanp->chan;
		chanp->sp->iif.statcallb(&ic);
	}
	FsmChangeState(fi, ST_NULL);
	chanp->Flags = FLG_ESTAB_D;
	FsmAddTimer(&chanp->drel_timer, DREL_TIMER_VALUE, EV_SHUTDOWN_D, NULL, 60);
}

static void
l4_go_null(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	FsmChangeState(fi, ST_NULL);
	chanp->Flags = 0;
	FsmDelTimer(&chanp->drel_timer, 63);
}

static void
l4_disconn_bchan(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	chanp->data_open = 0;
	FsmChangeState(fi, ST_WAIT_BRELEASE);
	RESBIT(chanp->Flags, FLG_CONNECT_B);
	FsmEvent(&chanp->lc_b.lcfi, EV_LC_RELEASE, NULL);
}

static void
l4_send_d_disc(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;


	if (chanp->Flags & (FLG_DISC_REC | FLG_REL_REC))
		return;
	FsmChangeState(fi, ST_WAIT_DRELEASE);
	if (chanp->Flags & FLG_LL_BCONN) {
		if (chanp->debug & 1)
			link_debug(chanp, "STAT_BHUP", 0);
		RESBIT(chanp->Flags, FLG_LL_BCONN);
		ic.driver = chanp->sp->myid;
		ic.command = ISDN_STAT_BHUP;
		ic.arg = chanp->chan;
		chanp->sp->iif.statcallb(&ic);
	}
	if (chanp->Flags & FLG_START_B) {
		release_ds(chanp);
		RESBIT(chanp->Flags, FLG_START_B);
	}
	chanp->para.cause = 0x10;	/* Normal Call Clearing */
	chanp->is.l4.l4l3(&chanp->is, CC_DISCONNECT_REQ, NULL);
	SETBIT(chanp->Flags, FLG_DISC_SEND);
}

static void
l4_released_bchan(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	FsmChangeState(fi, ST_WAIT_DCOMMAND);
	chanp->data_open = 0;
	if (chanp->Flags & FLG_LL_BCONN) {
		if (chanp->debug & 1)
			link_debug(chanp, "STAT_BHUP", 0);
		RESBIT(chanp->Flags, FLG_LL_BCONN);
		ic.driver = chanp->sp->myid;
		ic.command = ISDN_STAT_BHUP;
		ic.arg = chanp->chan;
		chanp->sp->iif.statcallb(&ic);
	}
	release_ds(chanp);
	RESBIT(chanp->Flags, FLG_START_B);
}


static void
l4_release_bchan(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	chanp->data_open = 0;
	SETBIT(chanp->Flags, FLG_DISC_REC);
	FsmChangeState(fi, ST_WAIT_BREL_DISC);
	RESBIT(chanp->Flags, FLG_CONNECT_B);
	FsmEvent(&chanp->lc_b.lcfi, EV_LC_RELEASE, NULL);
}

static void
l4_received_d_rel(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	chanp->data_open = 0;
	FsmChangeState(fi, ST_WAIT_DSHUTDOWN);
	SETBIT(chanp->Flags, FLG_REL_REC);
	if (chanp->Flags & FLG_CONNECT_B) {
		chanp->lc_b.l2_establish = 0;	/* direct reset in lc_b.lcfi */
		FsmEvent(&chanp->lc_b.lcfi, EV_LC_RELEASE, NULL);
		RESBIT(chanp->Flags, FLG_CONNECT_B);
	}
	if (chanp->Flags & FLG_LL_BCONN) {
		if (chanp->debug & 1)
			link_debug(chanp, "STAT_BHUP", 0);
		ic.driver = chanp->sp->myid;
		ic.command = ISDN_STAT_BHUP;
		ic.arg = chanp->chan;
		chanp->sp->iif.statcallb(&ic);
		RESBIT(chanp->Flags, FLG_LL_BCONN);
	}
	if (chanp->Flags & FLG_START_B) {
		release_ds(chanp);
		RESBIT(chanp->Flags, FLG_START_B);
	}
	if (chanp->Flags & (FLG_LL_DCONN | FLG_CALL_SEND | FLG_CALL_ALERT)) {
		if (chanp->debug & 1)
			link_debug(chanp, "STAT_DHUP", 0);
		l4_deliver_cause(chanp);
		ic.driver = chanp->sp->myid;
		ic.command = ISDN_STAT_DHUP;
		ic.arg = chanp->chan;
		chanp->sp->iif.statcallb(&ic);
	}
	FsmEvent(&chanp->lc_d.lcfi, EV_LC_FLUSH, NULL);
	RESBIT(chanp->Flags, FLG_ESTAB_D);
	RESBIT(chanp->Flags, FLG_DISC_SEND);
	RESBIT(chanp->Flags, FLG_CALL_REC);
	RESBIT(chanp->Flags, FLG_CALL_ALERT);
	RESBIT(chanp->Flags, FLG_LL_DCONN);
	RESBIT(chanp->Flags, FLG_CALL_SEND);
}

static void
l4_received_d_relcnf(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	chanp->data_open = 0;
	FsmChangeState(fi, ST_WAIT_DSHUTDOWN);
	if (chanp->Flags & FLG_CONNECT_B) {
		chanp->lc_b.l2_establish = 0;	/* direct reset in lc_b.lcfi */
		FsmEvent(&chanp->lc_b.lcfi, EV_LC_RELEASE, NULL);
		RESBIT(chanp->Flags, FLG_CONNECT_B);
	}
	if (chanp->Flags & FLG_LL_BCONN) {
		if (chanp->debug & 1)
			link_debug(chanp, "STAT_BHUP", 0);
		ic.driver = chanp->sp->myid;
		ic.command = ISDN_STAT_BHUP;
		ic.arg = chanp->chan;
		chanp->sp->iif.statcallb(&ic);
		RESBIT(chanp->Flags, FLG_LL_BCONN);
	}
	if (chanp->Flags & FLG_START_B) {
		release_ds(chanp);
		RESBIT(chanp->Flags, FLG_START_B);
	}
	if (chanp->Flags & (FLG_LL_DCONN | FLG_CALL_SEND | FLG_CALL_ALERT)) {
		if (chanp->debug & 1)
			link_debug(chanp, "STAT_DHUP", 0);
		l4_deliver_cause(chanp);
		ic.driver = chanp->sp->myid;
		ic.command = ISDN_STAT_DHUP;
		ic.arg = chanp->chan;
		chanp->sp->iif.statcallb(&ic);
	}
	FsmEvent(&chanp->lc_d.lcfi, EV_LC_RELEASE, NULL);
	RESBIT(chanp->Flags, FLG_ESTAB_D);
	RESBIT(chanp->Flags, FLG_DISC_SEND);
	RESBIT(chanp->Flags, FLG_CALL_REC);
	RESBIT(chanp->Flags, FLG_CALL_ALERT);
	RESBIT(chanp->Flags, FLG_LL_DCONN);
	RESBIT(chanp->Flags, FLG_CALL_SEND);
}

static void
l4_received_d_disc(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	chanp->data_open = 0;
	FsmChangeState(fi, ST_WAIT_D_REL_CNF);
	SETBIT(chanp->Flags, FLG_DISC_REC);
	if (chanp->Flags & FLG_LL_BCONN) {
		if (chanp->debug & 1)
			link_debug(chanp, "STAT_BHUP", 0);
		ic.driver = chanp->sp->myid;
		ic.command = ISDN_STAT_BHUP;
		ic.arg = chanp->chan;
		chanp->sp->iif.statcallb(&ic);
		RESBIT(chanp->Flags, FLG_LL_BCONN);
	}
	if (chanp->Flags & FLG_START_B) {
		release_ds(chanp);
		RESBIT(chanp->Flags, FLG_START_B);
	}
	if (chanp->Flags & (FLG_LL_DCONN | FLG_CALL_SEND | FLG_CALL_ALERT)) {
		if (chanp->debug & 1)
			link_debug(chanp, "STAT_DHUP", 0);
		l4_deliver_cause(chanp);
		ic.driver = chanp->sp->myid;
		ic.command = ISDN_STAT_DHUP;
		ic.arg = chanp->chan;
		chanp->sp->iif.statcallb(&ic);
	}
	RESBIT(chanp->Flags, FLG_LL_DCONN);
	RESBIT(chanp->Flags, FLG_CALL_SEND);
	RESBIT(chanp->Flags, FLG_CALL_ALERT);
	chanp->is.l4.l4l3(&chanp->is, CC_RELEASE_REQ, NULL);
}

/* processing charge info */
static void
l4_charge_info(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	ic.driver = chanp->sp->myid;
	ic.command = ISDN_STAT_CINF;
	ic.arg = chanp->chan;
	sprintf(ic.parm.num, "%d", chanp->para.chargeinfo);
	chanp->sp->iif.statcallb(&ic);
}

/* error procedures */

static void
l4_no_dchan(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	if (chanp->debug & 1)
		link_debug(chanp, "STAT_NODCH", 0);
	ic.driver = chanp->sp->myid;
	ic.command = ISDN_STAT_NODCH;
	ic.arg = chanp->chan;
	chanp->sp->iif.statcallb(&ic);
	chanp->Flags = 0;
	FsmChangeState(fi, ST_NULL);
	FsmEvent(&chanp->lc_d.lcfi, EV_LC_RELEASE, NULL);
}

static void
l4_no_dchan_ready(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	if (chanp->debug & 1)
		link_debug(chanp, "STAT_DHUP", 0);
	ic.driver = chanp->sp->myid;
	ic.command = ISDN_STAT_DHUP;
	ic.arg = chanp->chan;
	chanp->sp->iif.statcallb(&ic);
}

static void
l4_no_dchan_in(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	chanp->is.l4.l4l3(&chanp->is, CC_DLRL, NULL);
	chanp->Flags = 0;
	FsmChangeState(fi, ST_NULL);
	FsmEvent(&chanp->lc_d.lcfi, EV_LC_RELEASE, NULL);
}

static void
l4_no_setup_rsp(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	chanp->Flags = 0;
	FsmChangeState(fi, ST_NULL);
	FsmEvent(&chanp->lc_d.lcfi, EV_LC_RELEASE, NULL);
	if (chanp->debug & 1)
		link_debug(chanp, "STAT_DHUP", 0);
	ic.driver = chanp->sp->myid;
	ic.command = ISDN_STAT_DHUP;
	ic.arg = chanp->chan;
	chanp->sp->iif.statcallb(&ic);
}

static void
l4_setup_err(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	FsmChangeState(fi, ST_WAIT_DRELEASE);
	if (chanp->Flags & FLG_LL_DCONN) {
		if (chanp->debug & 1)
			link_debug(chanp, "STAT_DHUP", 0);
		RESBIT(chanp->Flags, FLG_LL_DCONN);
		l4_deliver_cause(chanp);
		ic.driver = chanp->sp->myid;
		ic.command = ISDN_STAT_DHUP;
		ic.arg = chanp->chan;
		chanp->sp->iif.statcallb(&ic);
	}
	SETBIT(chanp->Flags, FLG_DISC_SEND);	/* DISCONN was sent from L3 */
}

static void
l4_connect_err(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	FsmChangeState(fi, ST_WAIT_DRELEASE);
	if (chanp->Flags & FLG_LL_DCONN) {
		if (chanp->debug & 1)
			link_debug(chanp, "STAT_DHUP", 0);
		RESBIT(chanp->Flags, FLG_LL_DCONN);
		l4_deliver_cause(chanp);
		ic.driver = chanp->sp->myid;
		ic.command = ISDN_STAT_DHUP;
		ic.arg = chanp->chan;
		chanp->sp->iif.statcallb(&ic);
	}
	SETBIT(chanp->Flags, FLG_DISC_SEND);	/* DISCONN was sent from L3 */
}

static void
l4_active_dlrl(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	chanp->data_open = 0;
	FsmChangeState(fi, ST_NULL);
	if (chanp->Flags & FLG_CONNECT_B) {
		chanp->lc_b.l2_establish = 0;	/* direct reset in lc_b.lcfi */
		FsmEvent(&chanp->lc_b.lcfi, EV_LC_RELEASE, NULL);
		RESBIT(chanp->Flags, FLG_CONNECT_B);
	}
	if (chanp->Flags & FLG_LL_BCONN) {
		if (chanp->debug & 1)
			link_debug(chanp, "STAT_BHUP", 0);
		ic.driver = chanp->sp->myid;
		ic.command = ISDN_STAT_BHUP;
		ic.arg = chanp->chan;
		chanp->sp->iif.statcallb(&ic);
		RESBIT(chanp->Flags, FLG_LL_BCONN);
	}
	if (chanp->Flags & FLG_START_B) {
		release_ds(chanp);
		RESBIT(chanp->Flags, FLG_START_B);
	}
	if (chanp->Flags & FLG_LL_DCONN) {
		if (chanp->debug & 1)
			link_debug(chanp, "STAT_DHUP", 0);
		RESBIT(chanp->Flags, FLG_LL_DCONN);
		if (chanp->sp->protocol == ISDN_PTYPE_EURO) {
			chanp->para.cause = 0x2f;
			chanp->para.loc = 0;
		} else {
			chanp->para.cause = 0x70;
			chanp->para.loc = 0;
		}
		l4_deliver_cause(chanp);
		ic.driver = chanp->sp->myid;
		ic.command = ISDN_STAT_DHUP;
		ic.arg = chanp->chan;
		chanp->sp->iif.statcallb(&ic);
	}
	chanp->Flags = 0;
	chanp->is.l4.l4l3(&chanp->is, CC_DLRL, NULL);
	FsmEvent(&chanp->lc_d.lcfi, EV_LC_RELEASE, NULL);
}
/* *INDENT-OFF* */
static struct FsmNode fnlist[] =
{
	{ST_NULL,		EV_DIAL,		l4_prep_dialout},
	{ST_NULL,		EV_SETUP_IND,		l4_start_dchan},
	{ST_NULL,		EV_SHUTDOWN_D,		l4_shutdown_d},
	{ST_NULL,		EV_DLRL,		l4_go_null},
	{ST_OUT_WAIT_D,		EV_DLEST,		l4_do_dialout},
	{ST_OUT_WAIT_D,		EV_DLRL,		l4_no_dchan},
	{ST_OUT_WAIT_D,		EV_HANGUP,		l4_no_dchan},
	{ST_IN_WAIT_D,		EV_DLEST,		l4_deliver_call},
	{ST_IN_WAIT_D,		EV_DLRL,		l4_no_dchan_in},
	{ST_IN_WAIT_D,		EV_HANGUP,		l4_no_dchan_in},
	{ST_OUT_DIAL,		EV_SETUP_CNF,		l4_init_bchan_out},
	{ST_OUT_DIAL,		EV_HANGUP,		l4_cancel_call},
	{ST_OUT_DIAL,		EV_DISCONNECT_IND,	l4_received_d_disc},
	{ST_OUT_DIAL,		EV_RELEASE_IND,		l4_received_d_rel},
	{ST_OUT_DIAL,		EV_RELEASE_CNF,		l4_received_d_relcnf},
	{ST_OUT_DIAL,		EV_NOSETUP_RSP,		l4_no_setup_rsp},
	{ST_OUT_DIAL,		EV_SETUP_ERR,		l4_setup_err},
	{ST_IN_WAIT_LL,		EV_SETUP_CMPL_IND,	l4_init_bchan_in},
	{ST_IN_WAIT_LL,		EV_ACCEPTD,		l4_send_dconnect},
	{ST_IN_WAIT_LL,		EV_HANGUP,		l4_reject_call},
	{ST_IN_WAIT_LL,		EV_DISCONNECT_IND,	l4_received_d_disc},
	{ST_IN_WAIT_LL,		EV_RELEASE_IND,		l4_received_d_rel},
	{ST_IN_WAIT_LL,		EV_RELEASE_CNF,		l4_received_d_relcnf},
	{ST_IN_ALERT_SEND,	EV_SETUP_CMPL_IND,	l4_init_bchan_in},
	{ST_IN_ALERT_SEND,	EV_ACCEPTD,		l4_send_dconnect},
	{ST_IN_ALERT_SEND,	EV_HANGUP,		l4_send_d_disc},
	{ST_IN_ALERT_SEND,	EV_DISCONNECT_IND,	l4_received_d_disc},
	{ST_IN_ALERT_SEND,	EV_RELEASE_IND,		l4_received_d_rel},
	{ST_IN_ALERT_SEND,	EV_RELEASE_CNF,		l4_received_d_relcnf},
	{ST_IN_WAIT_CONN_ACK,	EV_SETUP_CMPL_IND,	l4_init_bchan_in},
	{ST_IN_WAIT_CONN_ACK,	EV_HANGUP,		l4_send_d_disc},
	{ST_IN_WAIT_CONN_ACK,	EV_DISCONNECT_IND,	l4_received_d_disc},
	{ST_IN_WAIT_CONN_ACK,	EV_RELEASE_IND,		l4_received_d_rel},
	{ST_IN_WAIT_CONN_ACK,	EV_RELEASE_CNF,		l4_received_d_relcnf},
	{ST_IN_WAIT_CONN_ACK,	EV_CONNECT_ERR,		l4_connect_err},
	{ST_WAIT_BCONN,		EV_BC_EST,		l4_go_active},
	{ST_WAIT_BCONN,		EV_BC_REL,		l4_send_d_disc},
	{ST_WAIT_BCONN,		EV_HANGUP,		l4_send_d_disc},
	{ST_WAIT_BCONN,		EV_DISCONNECT_IND,	l4_received_d_disc},
	{ST_WAIT_BCONN,		EV_RELEASE_IND,		l4_received_d_rel},
	{ST_WAIT_BCONN,		EV_RELEASE_CNF,		l4_received_d_relcnf},
	{ST_ACTIVE,		EV_CINF,		l4_charge_info},
	{ST_ACTIVE,		EV_BC_REL,		l4_released_bchan},
	{ST_ACTIVE,		EV_HANGUP,		l4_disconn_bchan},
	{ST_ACTIVE,		EV_DISCONNECT_IND,	l4_release_bchan},
	{ST_ACTIVE,		EV_RELEASE_CNF,		l4_received_d_relcnf},
	{ST_ACTIVE,		EV_RELEASE_IND,		l4_received_d_rel},
	{ST_ACTIVE,		EV_DLRL,		l4_active_dlrl},
	{ST_WAIT_BRELEASE,	EV_BC_REL,		l4_send_d_disc},
	{ST_WAIT_BRELEASE,	EV_DISCONNECT_IND,	l4_received_d_disc},
	{ST_WAIT_BRELEASE,	EV_RELEASE_CNF,		l4_received_d_relcnf},
	{ST_WAIT_BRELEASE,	EV_RELEASE_IND,		l4_received_d_rel},
	{ST_WAIT_BREL_DISC,	EV_BC_REL,		l4_received_d_disc},
	{ST_WAIT_BREL_DISC,	EV_RELEASE_CNF,		l4_received_d_relcnf},
	{ST_WAIT_BREL_DISC,	EV_RELEASE_IND,		l4_received_d_rel},
	{ST_WAIT_DCOMMAND,	EV_HANGUP,		l4_send_d_disc},
	{ST_WAIT_DCOMMAND,	EV_DISCONNECT_IND,	l4_received_d_disc},
	{ST_WAIT_DCOMMAND,	EV_RELEASE_CNF,		l4_received_d_relcnf},
	{ST_WAIT_DCOMMAND,	EV_RELEASE_IND,		l4_received_d_rel},
	{ST_WAIT_DRELEASE,	EV_RELEASE_IND,		l4_timeout_d},
	{ST_WAIT_DRELEASE,	EV_RELEASE_CNF,		l4_timeout_d},
	{ST_WAIT_DRELEASE,	EV_RELEASE_ERR,		l4_timeout_d},
	{ST_WAIT_DRELEASE,	EV_DIAL,		l4_no_dchan_ready},
	{ST_WAIT_D_REL_CNF,	EV_RELEASE_CNF,		l4_timeout_d},
	{ST_WAIT_D_REL_CNF,	EV_RELEASE_ERR,		l4_timeout_d},
	{ST_WAIT_D_REL_CNF,	EV_DIAL,		l4_no_dchan_ready},
	{ST_WAIT_DSHUTDOWN,	EV_DLRL,		l4_go_null},
	{ST_WAIT_DSHUTDOWN,	EV_DIAL,		l4_prep_dialout},
	{ST_WAIT_DSHUTDOWN,	EV_SETUP_IND,		l4_start_dchan},
};
/* *INDENT-ON* */




#define FNCOUNT (sizeof(fnlist)/sizeof(struct FsmNode))

static void
lc_r1(struct FsmInst *fi, int event, void *arg)
{
	struct LcFsm *lf = fi->userdata;

	FsmChangeState(fi, ST_LC_ACTIVATE_WAIT);
	FsmAddTimer(&lf->act_timer, 1000, EV_LC_TIMER, NULL, 50);
	lf->st->ma.manl1(lf->st, PH_ACTIVATE, NULL);

}

static void
lc_r6(struct FsmInst *fi, int event, void *arg)
{
	struct LcFsm *lf = fi->userdata;

	FsmDelTimer(&lf->act_timer, 50);
	FsmChangeState(fi, ST_LC_DELAY);
	FsmAddTimer(&lf->act_timer, 40, EV_LC_TIMER, NULL, 51);
}

static void
lc_r2(struct FsmInst *fi, int event, void *arg)
{
	struct LcFsm *lf = fi->userdata;

	if (lf->l2_establish) {
		FsmChangeState(fi, ST_LC_ESTABLISH_WAIT);
		if (lf->l2_start)
			lf->st->ma.manl2(lf->st, DL_ESTABLISH, NULL);
	} else {
		FsmChangeState(fi, ST_LC_CONNECTED);
		lf->lccall(lf, LC_ESTABLISH, NULL);
	}
}

static void
lc_r3(struct FsmInst *fi, int event, void *arg)
{
	struct LcFsm *lf = fi->userdata;

	FsmChangeState(fi, ST_LC_CONNECTED);
	lf->lccall(lf, LC_ESTABLISH, NULL);
}

static void
lc_r7(struct FsmInst *fi, int event, void *arg)
{
	struct LcFsm *lf = fi->userdata;

	FsmChangeState(fi, ST_LC_FLUSH_WAIT);
	lf->st->ma.manl2(lf->st, DL_FLUSH, NULL);
}

static void
lc_r4(struct FsmInst *fi, int event, void *arg)
{
	struct LcFsm *lf = fi->userdata;

	if (lf->l2_establish) {
		FsmChangeState(fi, ST_LC_RELEASE_WAIT);
		lf->st->ma.manl2(lf->st, DL_RELEASE, NULL);
		/* This timer is for releasing the channel even
		 * there is a hang in layer 2 ; 5 sec are a try
		 */
		FsmAddTimer(&lf->act_timer, 5000, EV_LC_TIMER, NULL, 53);
	} else {
		FsmChangeState(fi, ST_LC_NULL);
		lf->st->ma.manl1(lf->st, PH_DEACTIVATE, NULL);
		lf->lccall(lf, LC_RELEASE, NULL);
	}
}

static void
lc_r4_1(struct FsmInst *fi, int event, void *arg)
{
	struct LcFsm *lf = fi->userdata;

	FsmChangeState(fi, ST_LC_FLUSH_DELAY);
	FsmAddTimer(&lf->act_timer, 50, EV_LC_TIMER, NULL, 52);
}

static void
lc_r5_1(struct FsmInst *fi, int event, void *arg)
{
	struct LcFsm *lf = fi->userdata;

	FsmChangeState(fi, ST_LC_RELEASE_WAIT);
	/* This delay is needed for send out the UA frame before
	 * PH_DEACTIVATE the interface
	 */
	FsmAddTimer(&lf->act_timer, 10, EV_LC_TIMER, NULL, 54);
}

static void
lc_r5(struct FsmInst *fi, int event, void *arg)
{
	struct LcFsm *lf = fi->userdata;

	FsmDelTimer(&lf->act_timer, 54);
	FsmChangeState(fi, ST_LC_NULL);
	lf->st->ma.manl1(lf->st, PH_DEACTIVATE, NULL);
	lf->lccall(lf, LC_RELEASE, NULL);
}
/* *INDENT-OFF* */
static struct FsmNode LcFnList[] =
{
	{ST_LC_NULL,  		EV_LC_ESTABLISH,	lc_r1},
	{ST_LC_ACTIVATE_WAIT,	EV_LC_PH_ACTIVATE,	lc_r6},
	{ST_LC_DELAY,		EV_LC_TIMER,		lc_r2},
	{ST_LC_DELAY,		EV_LC_DL_ESTABLISH,	lc_r3},
	{ST_LC_ESTABLISH_WAIT,	EV_LC_DL_ESTABLISH,	lc_r3},
	{ST_LC_ESTABLISH_WAIT,	EV_LC_RELEASE,		lc_r5},
	{ST_LC_CONNECTED,	EV_LC_FLUSH,		lc_r7},
	{ST_LC_CONNECTED,	EV_LC_RELEASE,		lc_r4},
	{ST_LC_CONNECTED,	EV_LC_DL_RELEASE,	lc_r5_1},
	{ST_LC_FLUSH_WAIT,	EV_LC_DL_FLUSH,		lc_r4_1},
	{ST_LC_FLUSH_DELAY,	EV_LC_TIMER,		lc_r4},
	{ST_LC_RELEASE_WAIT,	EV_LC_DL_RELEASE,	lc_r5},
	{ST_LC_RELEASE_WAIT,	EV_LC_TIMER,		lc_r5},
	{ST_LC_ACTIVATE_WAIT,	EV_LC_TIMER,		lc_r5},
	{ST_LC_ESTABLISH_WAIT,	EV_LC_DL_RELEASE,	lc_r5},
};
/* *INDENT-ON* */










#define LC_FN_COUNT (sizeof(LcFnList)/sizeof(struct FsmNode))

void
CallcNew(void)
{
	callcfsm.state_count = STATE_COUNT;
	callcfsm.event_count = EVENT_COUNT;
	callcfsm.strEvent = strEvent;
	callcfsm.strState = strState;
	FsmNew(&callcfsm, fnlist, FNCOUNT);

	lcfsm.state_count = LC_STATE_COUNT;
	lcfsm.event_count = LC_EVENT_COUNT;
	lcfsm.strEvent = strLcEvent;
	lcfsm.strState = strLcState;
	FsmNew(&lcfsm, LcFnList, LC_FN_COUNT);
}

void
CallcFree(void)
{
	FsmFree(&lcfsm);
	FsmFree(&callcfsm);
}

static void
release_ds(struct Channel *chanp)
{
	struct PStack *st = &chanp->ds;
	struct IsdnCardState *sp;
	struct HscxState *hsp;

	sp = st->l1.hardware;
	hsp = sp->hs + chanp->hscx;

	close_hscxstate(hsp);

	switch (chanp->l2_active_protocol) {
		case (ISDN_PROTO_L2_X75I):
			releasestack_isdnl2(st);
			break;
		case (ISDN_PROTO_L2_HDLC):
		case (ISDN_PROTO_L2_TRANS):
			releasestack_transl2(st);
			break;
	}
	/* Reset B-Channel Statemachine */
	FsmDelTimer(&chanp->lc_b.act_timer, 79);
	FsmChangeState(&chanp->lc_b.lcfi, ST_LC_NULL);
}

static void
cc_l1man(struct PStack *st, int pr, void *arg)
{
	struct Channel *chanp = (struct Channel *) st->l4.userdata;

	switch (pr) {
		case (PH_ACTIVATE):
			FsmEvent(&chanp->lc_d.lcfi, EV_LC_PH_ACTIVATE, NULL);
			break;
		case (PH_DEACTIVATE):
			FsmEvent(&chanp->lc_d.lcfi, EV_LC_PH_DEACTIVATE, NULL);
			break;
	}
}

static void
cc_l2man(struct PStack *st, int pr, void *arg)
{
	struct Channel *chanp = (struct Channel *) st->l4.userdata;

	switch (pr) {
		case (DL_ESTABLISH):
			FsmEvent(&chanp->lc_d.lcfi, EV_LC_DL_ESTABLISH, NULL);
			break;
		case (DL_RELEASE):
			FsmEvent(&chanp->lc_d.lcfi, EV_LC_DL_RELEASE, NULL);
			break;
		case (DL_FLUSH):
			FsmEvent(&chanp->lc_d.lcfi, EV_LC_DL_FLUSH, NULL);
			break;
	}
}

static void
dcc_l1man(struct PStack *st, int pr, void *arg)
{
	struct Channel *chanp = (struct Channel *) st->l4.userdata;

	switch (pr) {
		case (PH_ACTIVATE):
			FsmEvent(&chanp->lc_b.lcfi, EV_LC_PH_ACTIVATE, NULL);
			break;
		case (PH_DEACTIVATE):
			FsmEvent(&chanp->lc_b.lcfi, EV_LC_PH_DEACTIVATE, NULL);
			break;
	}
}

static void
dcc_l2man(struct PStack *st, int pr, void *arg)
{
	struct Channel *chanp = (struct Channel *) st->l4.userdata;

	switch (pr) {
		case (DL_ESTABLISH):
			FsmEvent(&chanp->lc_b.lcfi, EV_LC_DL_ESTABLISH, NULL);
			break;
		case (DL_RELEASE):
			FsmEvent(&chanp->lc_b.lcfi, EV_LC_DL_RELEASE, NULL);
			break;
	}
}

static void
l2tei_dummy(struct PStack *st, int pr, void *arg)
{
	struct Channel *chanp = (struct Channel *) st->l4.userdata;
	char tmp[64], tm[32];

	jiftime(tm, jiffies);
	sprintf(tmp, "%s Channel %d Warning! Dummy l2tei called pr=%d\n", tm, chanp->chan, pr);
	HiSax_putstatus(chanp->sp, tmp);
}

static void
ll_handler(struct PStack *st, int pr, void *arg)
{
	struct Channel *chanp = (struct Channel *) st->l4.userdata;
	char tmp[64], tm[32];

	switch (pr) {
		case (CC_DISCONNECT_IND):
			FsmEvent(&chanp->fi, EV_DISCONNECT_IND, NULL);
			break;
		case (CC_RELEASE_CNF):
			FsmEvent(&chanp->fi, EV_RELEASE_CNF, NULL);
			break;
		case (CC_SETUP_IND):
			FsmEvent(&chanp->fi, EV_SETUP_IND, NULL);
			break;
		case (CC_RELEASE_IND):
			FsmEvent(&chanp->fi, EV_RELEASE_IND, NULL);
			break;
		case (CC_SETUP_COMPLETE_IND):
			FsmEvent(&chanp->fi, EV_SETUP_CMPL_IND, NULL);
			break;
		case (CC_SETUP_CNF):
			FsmEvent(&chanp->fi, EV_SETUP_CNF, NULL);
			break;
		case (CC_INFO_CHARGE):
			FsmEvent(&chanp->fi, EV_CINF, NULL);
			break;
		case (CC_NOSETUP_RSP_ERR):
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
		default:
			if (chanp->debug & 2048) {
				jiftime(tm, jiffies);
				sprintf(tmp, "%s Channel %d L3->L4 unknown primitiv %d\n",
					tm, chanp->chan, pr);
				HiSax_putstatus(chanp->sp, tmp);
			}
	}
}

static void
init_is(struct Channel *chanp, unsigned int ces)
{
	struct PStack *st = &chanp->is;
	struct IsdnCardState *sp = chanp->sp;
	char tmp[128];

	setstack_HiSax(st, sp);
	st->l2.sap = 0;
	st->l2.tei = 255;
	st->l2.ces = ces;
	st->l2.extended = !0;
	st->l2.laptype = LAPD;
	st->l2.window = 1;
	st->l2.orig = !0;
	st->l2.t200 = 1000;	/* 1000 milliseconds  */
	if (st->protocol == ISDN_PTYPE_1TR6) {
		st->l2.n200 = 3;	/* try 3 times        */
		st->l2.t203 = 10000;	/* 10000 milliseconds */
	} else {
		st->l2.n200 = 4;	/* try 4 times        */
		st->l2.t203 = 5000;	/* 5000 milliseconds  */
	}
	sprintf(tmp, "Channel %d q.921", chanp->chan);
	setstack_isdnl2(st, tmp);
	setstack_isdnl3(st, chanp);
	st->l4.userdata = chanp;
	st->l4.l2writewakeup = NULL;
	st->l3.l3l4 = ll_handler;
	st->l1.l1man = cc_l1man;
	st->l2.l2man = cc_l2man;
	st->pa = &chanp->para;
	HiSax_addlist(sp, st);
}

static void
callc_debug(struct FsmInst *fi, char *s)
{
	char str[80], tm[32];
	struct Channel *chanp = fi->userdata;

	jiftime(tm, jiffies);
	sprintf(str, "%s Channel %d callc %s\n", tm, chanp->chan, s);
	HiSax_putstatus(chanp->sp, str);
}

static void
lc_debug(struct FsmInst *fi, char *s)
{
	char str[256], tm[32];
	struct LcFsm *lf = fi->userdata;

	jiftime(tm, jiffies);
	sprintf(str, "%s Channel %d lc %s\n", tm, lf->ch->chan, s);
	HiSax_putstatus(lf->ch->sp, str);
}

static void
dlc_debug(struct FsmInst *fi, char *s)
{
	char str[256], tm[32];
	struct LcFsm *lf = fi->userdata;

	jiftime(tm, jiffies);
	sprintf(str, "%s Channel %d dlc %s\n", tm, lf->ch->chan, s);
	HiSax_putstatus(lf->ch->sp, str);
}

static void
lccall_d(struct LcFsm *lf, int pr, void *arg)
{
	struct Channel *chanp = lf->ch;

	switch (pr) {
		case (LC_ESTABLISH):
			FsmEvent(&chanp->fi, EV_DLEST, NULL);
			break;
		case (LC_RELEASE):
			FsmEvent(&chanp->fi, EV_DLRL, NULL);
			break;
	}
}

static void
lccall_b(struct LcFsm *lf, int pr, void *arg)
{
	struct Channel *chanp = lf->ch;

	switch (pr) {
		case (LC_ESTABLISH):
			FsmEvent(&chanp->fi, EV_BC_EST, NULL);
			break;
		case (LC_RELEASE):
			FsmEvent(&chanp->fi, EV_BC_REL, NULL);
			break;
	}
}

static void
init_chan(int chan, struct IsdnCardState *csta, int hscx,
	  unsigned int ces)
{
	struct Channel *chanp = csta->channel + chan;

	chanp->sp = csta;
	chanp->hscx = hscx;
	chanp->chan = chan;
	chanp->incoming = 0;
	chanp->debug = 0;
	chanp->Flags = 0;
	chanp->leased = 0;
	chanp->impair = 0;
	init_is(chanp, ces);

	chanp->fi.fsm = &callcfsm;
	chanp->fi.state = ST_NULL;
	chanp->fi.debug = 0;
	chanp->fi.userdata = chanp;
	chanp->fi.printdebug = callc_debug;
	FsmInitTimer(&chanp->fi, &chanp->dial_timer);
	FsmInitTimer(&chanp->fi, &chanp->drel_timer);

	chanp->lc_d.lcfi.fsm = &lcfsm;
	chanp->lc_d.lcfi.state = ST_LC_NULL;
	chanp->lc_d.lcfi.debug = 0;
	chanp->lc_d.lcfi.userdata = &chanp->lc_d;
	chanp->lc_d.lcfi.printdebug = lc_debug;
	chanp->lc_d.type = LC_D;
	chanp->lc_d.ch = chanp;
	chanp->lc_d.st = &chanp->is;
	chanp->lc_d.l2_establish = !0;
	chanp->lc_d.l2_start = !0;
	chanp->lc_d.lccall = lccall_d;
	FsmInitTimer(&chanp->lc_d.lcfi, &chanp->lc_d.act_timer);

	chanp->lc_b.lcfi.fsm = &lcfsm;
	chanp->lc_b.lcfi.state = ST_LC_NULL;
	chanp->lc_b.lcfi.debug = 0;
	chanp->lc_b.lcfi.userdata = &chanp->lc_b;
	chanp->lc_b.lcfi.printdebug = dlc_debug;
	chanp->lc_b.type = LC_B;
	chanp->lc_b.ch = chanp;
	chanp->lc_b.st = &chanp->ds;
	chanp->lc_b.l2_establish = !0;
	chanp->lc_b.l2_start = !0;
	chanp->lc_b.lccall = lccall_b;
	FsmInitTimer(&chanp->lc_b.lcfi, &chanp->lc_b.act_timer);
	chanp->outcallref = 64;
	chanp->data_open = 0;
}

int
CallcNewChan(struct IsdnCardState *csta)
{
	int ces;

	chancount += 2;
	ces = randomces();
	init_chan(0, csta, 1, ces++);
	ces %= 0xffff;
	init_chan(1, csta, 0, ces++);
	printk(KERN_INFO "HiSax: 2 channels added\n");
	return (2);
}

static void
release_is(struct Channel *chanp)
{
	struct PStack *st = &chanp->is;

	releasestack_isdnl2(st);
	releasestack_isdnl3(st);
	HiSax_rmlist(st->l1.hardware, st);
}

void
CallcFreeChan(struct IsdnCardState *csta)
{
	int i;

	for (i = 0; i < 2; i++) {
		FsmDelTimer(&csta->channel[i].drel_timer, 74);
		FsmDelTimer(&csta->channel[i].dial_timer, 75);
		FsmDelTimer(&csta->channel[i].lc_b.act_timer, 76);
		FsmDelTimer(&csta->channel[i].lc_d.act_timer, 77);
		if (csta->channel[i].Flags & FLG_START_B) {
			release_ds(csta->channel + i);
		}
		release_is(csta->channel + i);
	}
}

static void
lldata_handler(struct PStack *st, int pr, void *arg)
{
	struct Channel *chanp = (struct Channel *) st->l4.userdata;
	struct sk_buff *skb = arg;

	switch (pr) {
		case (DL_DATA):
			if (chanp->data_open)
				chanp->sp->iif.rcvcallb_skb(chanp->sp->myid, chanp->chan, skb);
			else {
				SET_SKB_FREE(skb);
				dev_kfree_skb(skb, FREE_READ);
			}
			break;
		default:
			printk(KERN_WARNING "lldata_handler unknown primitive %d\n",
			       pr);
			break;
	}
}

static void
lltrans_handler(struct PStack *st, int pr, void *arg)
{
	struct Channel *chanp = (struct Channel *) st->l4.userdata;
	struct sk_buff *skb = arg;

	switch (pr) {
		case (PH_DATA):
			if (chanp->data_open)
				chanp->sp->iif.rcvcallb_skb(chanp->sp->myid, chanp->chan, skb);
			else {
				SET_SKB_FREE(skb);
				dev_kfree_skb(skb, FREE_READ);
			}
			break;
		default:
			printk(KERN_WARNING "lltrans_handler unknown primitive %d\n",
			       pr);
			break;
	}
}

static void
ll_writewakeup(struct PStack *st)
{
	struct Channel *chanp = st->l4.userdata;
	isdn_ctrl ic;

	ic.driver = chanp->sp->myid;
	ic.command = ISDN_STAT_BSENT;
	ic.arg = chanp->chan;
	chanp->sp->iif.statcallb(&ic);
}

static int
init_ds(struct Channel *chanp, int incoming)
{
	struct PStack *st = &chanp->ds;
	struct IsdnCardState *sp = chanp->sp;
	struct HscxState *hsp = sp->hs + chanp->hscx;
	char tmp[128];

	st->l1.hardware = sp;

	hsp->mode = 2;

	if (setstack_hscx(st, hsp))
		return (-1);

	st->l2.extended = 0;
	st->l2.laptype = LAPB;
	st->l2.orig = !incoming;
	st->l2.t200 = 1000;	/* 1000 milliseconds */
	st->l2.window = 7;
	st->l2.n200 = 4;	/* try 4 times       */
	st->l2.t203 = 5000;	/* 5000 milliseconds */

	st->l3.debug = 0;
	switch (chanp->l2_active_protocol) {
		case (ISDN_PROTO_L2_X75I):
			sprintf(tmp, "Channel %d x.75", chanp->chan);
			setstack_isdnl2(st, tmp);
			st->l2.l2l3 = lldata_handler;
			st->l1.l1man = dcc_l1man;
			st->l2.l2man = dcc_l2man;
			st->l2.l2tei = l2tei_dummy;
			st->l4.userdata = chanp;
			st->l4.l1writewakeup = NULL;
			st->l4.l2writewakeup = ll_writewakeup;
			st->l2.l2m.debug = chanp->debug & 16;
			st->l2.debug = chanp->debug & 64;
			st->ma.manl2(st, MDL_NOTEIPROC, NULL);
			st->l1.hscxmode = 2;	/* Packet-Mode ? */
			st->l1.hscxchannel = chanp->para.bchannel - 1;
			break;
		case (ISDN_PROTO_L2_HDLC):
			st->l1.l1l2 = lltrans_handler;
			st->l1.l1man = dcc_l1man;
			st->l4.userdata = chanp;
			st->l4.l1writewakeup = ll_writewakeup;
			st->l1.hscxmode = 2;
			st->l1.hscxchannel = chanp->para.bchannel - 1;
			break;
		case (ISDN_PROTO_L2_TRANS):
			st->l1.l1l2 = lltrans_handler;
			st->l1.l1man = dcc_l1man;
			st->l4.userdata = chanp;
			st->l4.l1writewakeup = ll_writewakeup;
			st->l1.hscxmode = 1;
			st->l1.hscxchannel = chanp->para.bchannel - 1;
			break;
	}
	return (0);
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
		chanp[i].is.l2.l2m.debug = debugflags & 8;
		chanp[i].ds.l2.l2m.debug = debugflags & 16;
		chanp[i].is.l2.debug = debugflags & 32;
		chanp[i].ds.l2.debug = debugflags & 64;
		chanp[i].lc_d.lcfi.debug = debugflags & 128;
		chanp[i].lc_b.lcfi.debug = debugflags & 256;
	}
	csta->dlogflag = debugflags & 4;
	csta->teistack->l2.l2m.debug = debugflags & 512;
}

int
HiSax_command(isdn_ctrl * ic)
{
	struct IsdnCardState *csta = hisax_findcard(ic->driver);
	struct Channel *chanp;
	char tmp[128];
	int i;
	unsigned int num;

	if (!csta) {
		printk(KERN_ERR
		"HiSax: if_command %d called with invalid driverId %d!\n",
		       ic->command, ic->driver);
		return -ENODEV;
	}
	switch (ic->command) {
		case (ISDN_CMD_SETEAZ):
			chanp = csta->channel + ic->arg;
			if (chanp->debug & 1)
				link_debug(chanp, "SETEAZ", 1);
			break;
		case (ISDN_CMD_SETL2):
			chanp = csta->channel + (ic->arg & 0xff);
			if (chanp->debug & 1) {
				sprintf(tmp, "SETL2 card %d %ld", csta->cardnr + 1,
					ic->arg >> 8);
				link_debug(chanp, tmp, 1);
			}
			chanp->l2_protocol = ic->arg >> 8;
			break;
		case (ISDN_CMD_DIAL):
			chanp = csta->channel + (ic->arg & 0xff);
			if (chanp->debug & 1) {
				sprintf(tmp, "DIAL %s -> %s (%d,%d)",
					ic->parm.setup.eazmsn, ic->parm.setup.phone,
				 ic->parm.setup.si1, ic->parm.setup.si2);
				link_debug(chanp, tmp, 1);
			}
			chanp->para.setup = ic->parm.setup;
			if (!strcmp(chanp->para.setup.eazmsn, "0"))
				chanp->para.setup.eazmsn[0] = '\0';
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
				link_debug(chanp, "ACCEPTB", 1);
			FsmEvent(&chanp->fi, EV_ACCEPTB, NULL);
			break;
		case (ISDN_CMD_ACCEPTD):
			chanp = csta->channel + ic->arg;
			if (chanp->debug & 1)
				link_debug(chanp, "ACCEPTD", 1);
			FsmEvent(&chanp->fi, EV_ACCEPTD, NULL);
			break;
		case (ISDN_CMD_HANGUP):
			chanp = csta->channel + ic->arg;
			if (chanp->debug & 1)
				link_debug(chanp, "HANGUP", 1);
			FsmEvent(&chanp->fi, EV_HANGUP, NULL);
			break;
		case (ISDN_CMD_SUSPEND):
			chanp = csta->channel + ic->arg;
			if (chanp->debug & 1) {
				sprintf(tmp, "SUSPEND %s", ic->parm.num);
				link_debug(chanp, tmp, 1);
			}
			FsmEvent(&chanp->fi, EV_SUSPEND, ic);
			break;
		case (ISDN_CMD_RESUME):
			chanp = csta->channel + ic->arg;
			if (chanp->debug & 1) {
				sprintf(tmp, "RESUME %s", ic->parm.num);
				link_debug(chanp, tmp, 1);
			}
			FsmEvent(&chanp->fi, EV_RESUME, ic);
			break;
		case (ISDN_CMD_LOCK):
			HiSax_mod_inc_use_count();
#ifdef MODULE
			if (csta->channel[0].debug & 1024) {
				jiftime(tmp, jiffies);
				i = strlen(tmp);
				sprintf(tmp + i, "   LOCK modcnt %lx\n", MOD_USE_COUNT);
				HiSax_putstatus(csta, tmp);
			}
#endif				/* MODULE */
			break;
		case (ISDN_CMD_UNLOCK):
			HiSax_mod_dec_use_count();
#ifdef MODULE
			if (csta->channel[0].debug & 1024) {
				jiftime(tmp, jiffies);
				i = strlen(tmp);
				sprintf(tmp + i, " UNLOCK modcnt %lx\n", MOD_USE_COUNT);
				HiSax_putstatus(csta, tmp);
			}
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
					sprintf(tmp, "debugging flags card %d set to %x\n",
						csta->cardnr + 1, num);
					HiSax_putstatus(csta, tmp);
					printk(KERN_DEBUG "HiSax: %s", tmp);
					break;
				case (2):
					num = *(unsigned int *) ic->parm.num;
					i = num >> 8;
					if (i >= 2)
						break;
					chanp = csta->channel + i;
					chanp->impair = num & 0xff;
					if (chanp->debug & 1) {
						sprintf(tmp, "IMPAIR %x", chanp->impair);
						link_debug(chanp, tmp, 1);
					}
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
					csta->channel[0].leased = 1;
					csta->channel[1].leased = 1;
					sprintf(tmp, "card %d set into leased mode\n",
						csta->cardnr + 1);
					HiSax_putstatus(csta, tmp);
					break;
#ifdef MODULE
				case (55):
#if (LINUX_VERSION_CODE < 0x020111)
					MOD_USE_COUNT = MOD_VISITED;
#else
					MOD_USE_COUNT = 0;
#endif
					HiSax_mod_inc_use_count();
					break;
#endif				/* MODULE */
				case (11):
					csta->debug = *(unsigned int *) ic->parm.num;
					sprintf(tmp, "l1 debugging flags card %d set to %x\n",
					  csta->cardnr + 1, csta->debug);
					HiSax_putstatus(cards[0].sp, tmp);
					printk(KERN_DEBUG "HiSax: %s", tmp);
					break;
				case (13):
					csta->channel[0].is.l3.debug = *(unsigned int *) ic->parm.num;
					csta->channel[1].is.l3.debug = *(unsigned int *) ic->parm.num;
					sprintf(tmp, "l3 debugging flags card %d set to %x\n",
						csta->cardnr + 1, *(unsigned int *) ic->parm.num);
					HiSax_putstatus(cards[0].sp, tmp);
					printk(KERN_DEBUG "HiSax: %s", tmp);
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
HiSax_writebuf_skb(int id, int chan, struct sk_buff *skb)
{
	struct IsdnCardState *csta = hisax_findcard(id);
	struct Channel *chanp;
	struct PStack *st;
	int len = skb->len;
	unsigned long flags;
	struct sk_buff *nskb;
	char tmp[64];

	if (!csta) {
		printk(KERN_ERR
		    "HiSax: if_sendbuf called with invalid driverId!\n");
		return -ENODEV;
	}
	chanp = csta->channel + chan;
	st = &chanp->ds;
	if (!chanp->data_open) {
		link_debug(chanp, "writebuf: channel not open", 1);
		return -EIO;
	}
	if (len > MAX_DATA_SIZE) {
		sprintf(tmp, "writebuf: packet too large (%d bytes)", len);
		printk(KERN_WARNING "HiSax_%s !\n", tmp);
		link_debug(chanp, tmp, 1);
		return -EINVAL;
	}
	if (len) {
		if ((len + csta->hs[chanp->hscx].tx_cnt) > MAX_DATA_MEM) {
			/* Must return 0 here, since this is not an error
			 * but a temporary lack of resources.
			 */
			if (chanp->debug & 2048) {
				sprintf(tmp, "writebuf: no buffers for %d bytes", len);
				link_debug(chanp, tmp, 1);
			}
			return 0;
		}
		save_flags(flags);
		cli();
		nskb = skb_clone(skb, GFP_ATOMIC);
		if (nskb) {
			if (chanp->lc_b.l2_establish) {
				csta->hs[chanp->hscx].tx_cnt += len + st->l2.ihsize;
				chanp->ds.l3.l3l2(&chanp->ds, DL_DATA, nskb);
			} else {
				csta->hs[chanp->hscx].tx_cnt += len;
				chanp->ds.l2.l2l1(&chanp->ds, PH_DATA, nskb);
			}
			dev_kfree_skb(skb, FREE_WRITE);
		} else
			len = 0;
		restore_flags(flags);
	}
	return (len);
}
