/* $Id: callc.c,v 2.13 1998/02/12 23:07:16 keil Exp $

 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *              based on the teles driver from Jan den Ouden
 *
 * Thanks to    Jan den Ouden
 *              Fritz Elfert
 *
 * $Log: callc.c,v $
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

#ifdef MODULE
#define MOD_USE_COUNT ( GET_USE_COUNT (&__this_module))
#endif				/* MODULE */

const char *lli_revision = "$Revision: 2.13 $";

extern struct IsdnCard cards[];
extern int nrcards;
extern void HiSax_mod_dec_use_count(void);
extern void HiSax_mod_inc_use_count(void);

static int init_b_st(struct Channel *chanp, int incoming);
static void release_b_st(struct Channel *chanp);

static struct Fsm callcfsm =
{NULL, 0, 0, NULL, NULL};
static struct Fsm lcfsm =
{NULL, 0, 0, NULL, NULL};

static int chancount = 0;

/* experimental REJECT after ALERTING for CALLBACK to beat the 4s delay */ 
#define ALERT_REJECT 1

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

/*
 * Because of callback it's a good idea to delay the shutdown of the d-channel
 */
#define	DREL_TIMER_VALUE 10000

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

static void
link_debug(struct Channel *chanp, char *s, int direction)
{
	char tmp[100], tm[32];

	jiftime(tm, jiffies);
	sprintf(tmp, "%s Channel %d %s %s\n", tm, chanp->chan,
		direction ? "LL->HL" : "HL->LL", s);
	HiSax_putstatus(chanp->cs, tmp);
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

enum {
	ST_LC_NULL,
	ST_LC_ACTIVATE_WAIT,
	ST_LC_DELAY,
	ST_LC_ESTABLISH_WAIT,
	ST_LC_CONNECTED,
	ST_LC_FLUSH_WAIT,
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
	"ST_LC_RELEASE_WAIT",
};

enum {
	EV_LC_ESTABLISH,
	EV_LC_PH_ACTIVATE,
	EV_LC_PH_DEACTIVATE,
	EV_LC_DL_ESTABLISH,
	EV_LC_TIMER,
	EV_LC_DL_RELEASE,
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
	"EV_LC_DL_RELEASE",
	"EV_LC_RELEASE",
};

#define LC_D  0
#define LC_B  1

static inline void
lli_deliver_cause(struct Channel *chanp)
{
	isdn_ctrl ic;

	if (chanp->proc->para.cause < 0)
		return;
	ic.driver = chanp->cs->myid;
	ic.command = ISDN_STAT_CAUSE;
	ic.arg = chanp->chan;
	if (chanp->cs->protocol == ISDN_PTYPE_EURO)
		sprintf(ic.parm.num, "E%02X%02X", chanp->proc->para.loc & 0x7f,
			chanp->proc->para.cause & 0x7f);
	else
		sprintf(ic.parm.num, "%02X%02X", chanp->proc->para.loc & 0x7f,
			chanp->proc->para.cause & 0x7f);
	chanp->cs->iif.statcallb(&ic);
}

static void
lli_d_established(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	test_and_set_bit(FLG_ESTAB_D, &chanp->Flags);
	if (chanp->leased) {
		isdn_ctrl ic;
		int ret;
		char txt[32];

		chanp->cs->cardmsg(chanp->cs, MDL_INFO_SETUP, (void *) chanp->chan);
		FsmChangeState(fi, ST_IN_WAIT_LL);
		test_and_set_bit(FLG_CALL_REC, &chanp->Flags);
		if (chanp->debug & 1)
			link_debug(chanp, "STAT_ICALL_LEASED", 0);
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
		if (chanp->debug & 1) {
			sprintf(txt, "statcallb ret=%d", ret);
			link_debug(chanp, txt, 1);
		}
		if (!ret) {
			chanp->cs->cardmsg(chanp->cs, MDL_INFO_REL, (void *) chanp->chan);
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
	chanp->lc_b->l2_start = !0;
	switch (chanp->l2_active_protocol) {
		case (ISDN_PROTO_L2_X75I):
			chanp->lc_b->l2_establish = !0;
			break;
		case (ISDN_PROTO_L2_HDLC):
		case (ISDN_PROTO_L2_TRANS):
			chanp->lc_b->l2_establish = 0;
			break;
		default:
			printk(KERN_WARNING "lli_prep_dialout unknown protocol\n");
			break;
	}
	if (test_bit(FLG_ESTAB_D, &chanp->Flags)) {
		FsmEvent(fi, EV_DLEST, NULL);
	} else {
		chanp->Flags = 0;
		test_and_set_bit(FLG_START_D, &chanp->Flags);
		if (chanp->leased) {
			chanp->lc_d->l2_establish = 0;
		}
		FsmEvent(&chanp->lc_d->lcfi, EV_LC_ESTABLISH, NULL);
	}
}

static void
lli_do_dialout(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	FsmChangeState(fi, ST_OUT_DIAL);
	chanp->cs->cardmsg(chanp->cs, MDL_INFO_SETUP, (void *) chanp->chan);
	if (chanp->leased) {
		FsmEvent(&chanp->fi, EV_SETUP_CNF, NULL);
	} else {
		test_and_set_bit(FLG_ESTAB_D, &chanp->Flags);
		chanp->d_st->lli.l4l3(chanp->d_st, CC_SETUP_REQ, chanp);
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
		link_debug(chanp, "STAT_DCONN", 0);
	ic.driver = chanp->cs->myid;
	ic.command = ISDN_STAT_DCONN;
	ic.arg = chanp->chan;
	chanp->cs->iif.statcallb(&ic);
	init_b_st(chanp, 0);
	test_and_set_bit(FLG_START_B, &chanp->Flags);
	FsmEvent(&chanp->lc_b->lcfi, EV_LC_ESTABLISH, NULL);
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
		link_debug(chanp, "STAT_BCONN", 0);
	test_and_set_bit(FLG_LL_BCONN, &chanp->Flags);
	ic.driver = chanp->cs->myid;
	ic.command = ISDN_STAT_BCONN;
	ic.arg = chanp->chan;
	chanp->cs->iif.statcallb(&ic);
	chanp->cs->cardmsg(chanp->cs, MDL_INFO_CONN, (void *) chanp->chan);
}

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
		FsmEvent(&chanp->lc_d->lcfi, EV_LC_ESTABLISH, NULL);
}

static void
lli_deliver_call(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;
	int ret;
	char txt[32];

	chanp->cs->cardmsg(chanp->cs, MDL_INFO_SETUP, (void *) chanp->chan);
	/*
	 * Report incoming calls only once to linklevel, use CallFlags
	 * which is set to 3 with each broadcast message in isdnl1.c
	 * and resetted if a interface  answered the STAT_ICALL.
	 */
	if (1) { /* for only one TEI */
		FsmChangeState(fi, ST_IN_WAIT_LL);
		test_and_set_bit(FLG_CALL_REC, &chanp->Flags);
		if (chanp->debug & 1)
			link_debug(chanp, "STAT_ICALL", 0);
		ic.driver = chanp->cs->myid;
		ic.command = ISDN_STAT_ICALL;
		ic.arg = chanp->chan;
		/*
		 * No need to return "unknown" for calls without OAD,
		 * cause that's handled in linklevel now (replaced by '0')
		 */
		ic.parm.setup = chanp->proc->para.setup;
		ret = chanp->cs->iif.statcallb(&ic);
		if (chanp->debug & 1) {
			sprintf(txt, "statcallb ret=%d", ret);
			link_debug(chanp, txt, 1);
		}
		switch (ret) {
			case 1:	/* OK, anybody likes this call */
				FsmDelTimer(&chanp->drel_timer, 61);
				if (test_bit(FLG_ESTAB_D, &chanp->Flags)) {
					FsmChangeState(fi, ST_IN_ALERT_SEND);
					test_and_set_bit(FLG_CALL_ALERT, &chanp->Flags);
					chanp->d_st->lli.l4l3(chanp->d_st, CC_ALERTING_REQ, chanp->proc);
				} else {
					test_and_set_bit(FLG_DO_ALERT, &chanp->Flags);
					FsmChangeState(fi, ST_IN_WAIT_D);
					test_and_set_bit(FLG_START_D, &chanp->Flags);
					FsmEvent(&chanp->lc_d->lcfi, EV_LC_ESTABLISH, NULL);
				}
				break;
			case 2:	/* Rejecting Call */
				test_and_clear_bit(FLG_CALL_REC, &chanp->Flags);
				break;
			case 0:	/* OK, nobody likes this call */
			default:	/* statcallb problems */
				chanp->d_st->lli.l4l3(chanp->d_st, CC_IGNORE, chanp->proc);
				chanp->cs->cardmsg(chanp->cs, MDL_INFO_REL, (void *) chanp->chan);
				FsmChangeState(fi, ST_NULL);
#ifndef LAYER2_WATCHING
				if (test_bit(FLG_ESTAB_D, &chanp->Flags))
					FsmRestartTimer(&chanp->drel_timer, DREL_TIMER_VALUE, EV_SHUTDOWN_D, NULL, 61);
#endif
				break;
		}
	} else {
		chanp->d_st->lli.l4l3(chanp->d_st, CC_IGNORE, chanp->proc);
		chanp->cs->cardmsg(chanp->cs, MDL_INFO_REL, (void *) chanp->chan);
		FsmChangeState(fi, ST_NULL);
#ifndef LAYER2_WATCHING
		if (test_bit(FLG_ESTAB_D, &chanp->Flags))
			FsmRestartTimer(&chanp->drel_timer, DREL_TIMER_VALUE, EV_SHUTDOWN_D, NULL, 62);
#endif
	}
}

static void
lli_establish_d(struct FsmInst *fi, int event, void *arg)
{
	/* This establish the D-channel for pending L3 messages 
	 * without blocking th channel
	 */
	struct Channel *chanp = fi->userdata;

	test_and_set_bit(FLG_DO_ESTAB, &chanp->Flags);
	FsmChangeState(fi, ST_IN_WAIT_D);
	test_and_set_bit(FLG_START_D, &chanp->Flags);
	FsmEvent(&chanp->lc_d->lcfi, EV_LC_ESTABLISH, NULL);
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
		chanp->d_st->lli.l4l3(chanp->d_st, CC_SETUP_RSP, chanp->proc);
	} else if (test_and_clear_bit(FLG_DO_ALERT, &chanp->Flags)) {
		if (test_bit(FLG_DO_HANGUP, &chanp->Flags))
			FsmRestartTimer(&chanp->drel_timer, 40, EV_HANGUP, NULL, 63);
		FsmChangeState(fi, ST_IN_ALERT_SEND);
		test_and_set_bit(FLG_CALL_ALERT, &chanp->Flags);
		chanp->d_st->lli.l4l3(chanp->d_st, CC_ALERTING_REQ, chanp->proc);
	} else if (test_and_clear_bit(FLG_DO_HANGUP, &chanp->Flags)) {
		FsmChangeState(fi, ST_WAIT_DRELEASE);
		chanp->proc->para.cause = 0x15;		/* Call Rejected */
		chanp->d_st->lli.l4l3(chanp->d_st, CC_REJECT_REQ, chanp->proc);
		test_and_set_bit(FLG_DISC_SEND, &chanp->Flags);
	} else if (test_and_clear_bit(FLG_DO_ESTAB, &chanp->Flags)) {
		FsmChangeState(fi, ST_NULL);
		chanp->Flags = 0;
		test_and_set_bit(FLG_ESTAB_D, &chanp->Flags);
		chanp->d_st->lli.l4l3(chanp->d_st, CC_ESTABLISH, chanp->proc);
		chanp->proc = NULL;
#ifndef LAYER2_WATCHING
		FsmAddTimer(&chanp->drel_timer, DREL_TIMER_VALUE, EV_SHUTDOWN_D, NULL, 60);
#endif
	}
}

static void
lli_send_dconnect(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	FsmChangeState(fi, ST_IN_WAIT_CONN_ACK);
	chanp->d_st->lli.l4l3(chanp->d_st, CC_SETUP_RSP, chanp->proc);
}

static void
lli_init_bchan_in(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	FsmChangeState(fi, ST_WAIT_BCONN);
	test_and_set_bit(FLG_LL_DCONN, &chanp->Flags);
	if (chanp->debug & 1)
		link_debug(chanp, "STAT_DCONN", 0);
	ic.driver = chanp->cs->myid;
	ic.command = ISDN_STAT_DCONN;
	ic.arg = chanp->chan;
	chanp->cs->iif.statcallb(&ic);
	chanp->l2_active_protocol = chanp->l2_protocol;
	chanp->incoming = !0;
	chanp->lc_b->l2_start = 0;
	switch (chanp->l2_active_protocol) {
		case (ISDN_PROTO_L2_X75I):
			chanp->lc_b->l2_establish = !0;
			break;
		case (ISDN_PROTO_L2_HDLC):
		case (ISDN_PROTO_L2_TRANS):
			chanp->lc_b->l2_establish = 0;
			break;
		default:
			printk(KERN_WARNING "bchannel unknown protocol\n");
			break;
	}
	init_b_st(chanp, !0);
	test_and_set_bit(FLG_START_B, &chanp->Flags);
	FsmEvent(&chanp->lc_b->lcfi, EV_LC_ESTABLISH, NULL);
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
			link_debug(chanp, "STAT_BHUP", 0);
		ic.driver = chanp->cs->myid;
		ic.command = ISDN_STAT_BHUP;
		ic.arg = chanp->chan;
		chanp->cs->iif.statcallb(&ic);
	}
	if (test_and_clear_bit(FLG_START_B, &chanp->Flags))
		release_b_st(chanp);
	chanp->proc->para.cause = 0x10;		/* Normal Call Clearing */
	chanp->d_st->lli.l4l3(chanp->d_st, CC_DISCONNECT_REQ, chanp->proc);
	test_and_set_bit(FLG_DISC_SEND, &chanp->Flags);
}

static void
lli_shutdown_d(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	FsmDelTimer(&chanp->drel_timer, 62);
#ifdef LAYER2_WATCHING
	FsmChangeState(fi, ST_NULL);
#else
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
	FsmEvent(&chanp->lc_d->lcfi, EV_LC_RELEASE, NULL);
#endif
}

static void
lli_timeout_d(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	if (test_and_clear_bit(FLG_LL_DCONN, &chanp->Flags)) {
		if (chanp->debug & 1)
			link_debug(chanp, "STAT_DHUP", 0);
		lli_deliver_cause(chanp);
		ic.driver = chanp->cs->myid;
		ic.command = ISDN_STAT_DHUP;
		ic.arg = chanp->chan;
		chanp->cs->iif.statcallb(&ic);
	}
	FsmChangeState(fi, ST_NULL);
	chanp->Flags = 0;
	test_and_set_bit(FLG_ESTAB_D, &chanp->Flags);
#ifndef LAYER2_WATCHING
	FsmAddTimer(&chanp->drel_timer, DREL_TIMER_VALUE, EV_SHUTDOWN_D, NULL, 60);
#endif
	chanp->cs->cardmsg(chanp->cs, MDL_INFO_REL, (void *) chanp->chan);
}

static void
lli_go_null(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	FsmChangeState(fi, ST_NULL);
	chanp->Flags = 0;
	FsmDelTimer(&chanp->drel_timer, 63);
	chanp->cs->cardmsg(chanp->cs, MDL_INFO_REL, (void *) chanp->chan);
}

static void
lli_disconn_bchan(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	chanp->data_open = 0;
	FsmChangeState(fi, ST_WAIT_BRELEASE);
	test_and_clear_bit(FLG_CONNECT_B, &chanp->Flags);
	FsmEvent(&chanp->lc_b->lcfi, EV_LC_RELEASE, NULL);
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
			link_debug(chanp, "STAT_BHUP", 0);
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
			link_debug(chanp, "STAT_DHUP", 0);
		ic.driver = chanp->cs->myid;
		ic.command = ISDN_STAT_DHUP;
		ic.arg = chanp->chan;
		chanp->cs->iif.statcallb(&ic);
		FsmChangeState(fi, ST_WAIT_DSHUTDOWN);
		test_and_clear_bit(FLG_ESTAB_D, &chanp->Flags);
		FsmEvent(&chanp->lc_d->lcfi, EV_LC_RELEASE, NULL);
	} else {
		if (test_and_clear_bit(FLG_DO_HANGUP, &chanp->Flags))
			chanp->proc->para.cause = 0x15;		/* Call Reject */
		else
			chanp->proc->para.cause = 0x10;		/* Normal Call Clearing */
		chanp->d_st->lli.l4l3(chanp->d_st, CC_DISCONNECT_REQ, chanp->proc);
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
			link_debug(chanp, "STAT_BHUP", 0);
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
	FsmEvent(&chanp->lc_b->lcfi, EV_LC_RELEASE, NULL);
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
		chanp->lc_b->l2_establish = 0;	/* direct reset in lc_b->lcfi */
		FsmEvent(&chanp->lc_b->lcfi, EV_LC_RELEASE, NULL);
	}
	if (test_and_clear_bit(FLG_LL_BCONN, &chanp->Flags)) {
		if (chanp->debug & 1)
			link_debug(chanp, "STAT_BHUP", 0);
		ic.driver = chanp->cs->myid;
		ic.command = ISDN_STAT_BHUP;
		ic.arg = chanp->chan;
		chanp->cs->iif.statcallb(&ic);
	}
	if (test_and_clear_bit(FLG_START_B, &chanp->Flags))
		release_b_st(chanp);
	if (test_bit(FLG_LL_DCONN, &chanp->Flags) ||
		test_bit(FLG_CALL_SEND, &chanp->Flags) ||
		test_bit(FLG_CALL_ALERT, &chanp->Flags)) {
		if (chanp->debug & 1)
			link_debug(chanp, "STAT_DHUP", 0);
		lli_deliver_cause(chanp);
		ic.driver = chanp->cs->myid;
		ic.command = ISDN_STAT_DHUP;
		ic.arg = chanp->chan;
		chanp->cs->iif.statcallb(&ic);
	}
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
		chanp->lc_b->l2_establish = 0;	/* direct reset in lc_b->lcfi */
		FsmEvent(&chanp->lc_b->lcfi, EV_LC_RELEASE, NULL);
	}
	if (test_and_clear_bit(FLG_LL_BCONN, &chanp->Flags)) {
		if (chanp->debug & 1)
			link_debug(chanp, "STAT_BHUP", 0);
		ic.driver = chanp->cs->myid;
		ic.command = ISDN_STAT_BHUP;
		ic.arg = chanp->chan;
		chanp->cs->iif.statcallb(&ic);
	}
	if (test_and_clear_bit(FLG_START_B, &chanp->Flags))
		release_b_st(chanp);
	if (test_bit(FLG_LL_DCONN, &chanp->Flags) ||
		test_bit(FLG_CALL_SEND, &chanp->Flags) ||
		test_bit(FLG_CALL_ALERT, &chanp->Flags)) {
		if (chanp->debug & 1)
			link_debug(chanp, "STAT_DHUP", 0);
		lli_deliver_cause(chanp);
		ic.driver = chanp->cs->myid;
		ic.command = ISDN_STAT_DHUP;
		ic.arg = chanp->chan;
		chanp->cs->iif.statcallb(&ic);
	}
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
			link_debug(chanp, "STAT_BHUP", 0);
		ic.driver = chanp->cs->myid;
		ic.command = ISDN_STAT_BHUP;
		ic.arg = chanp->chan;
		chanp->cs->iif.statcallb(&ic);
	}
	if (test_and_clear_bit(FLG_START_B, &chanp->Flags))
		release_b_st(chanp);
	if (test_bit(FLG_LL_DCONN, &chanp->Flags) ||
		test_bit(FLG_CALL_SEND, &chanp->Flags) ||
		test_bit(FLG_CALL_ALERT, &chanp->Flags)) {
		if (chanp->debug & 1)
			link_debug(chanp, "STAT_DHUP", 0);
		lli_deliver_cause(chanp);
		ic.driver = chanp->cs->myid;
		ic.command = ISDN_STAT_DHUP;
		ic.arg = chanp->chan;
		chanp->cs->iif.statcallb(&ic);
	}
	test_and_clear_bit(FLG_CALL_ALERT, &chanp->Flags);
	test_and_clear_bit(FLG_LL_DCONN, &chanp->Flags);
	test_and_clear_bit(FLG_CALL_SEND, &chanp->Flags);
	chanp->d_st->lli.l4l3(chanp->d_st, CC_RELEASE_REQ, chanp->proc);
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
		link_debug(chanp, "STAT_NODCH", 0);
	ic.driver = chanp->cs->myid;
	ic.command = ISDN_STAT_NODCH;
	ic.arg = chanp->chan;
	chanp->cs->iif.statcallb(&ic);
	chanp->Flags = 0;
	FsmChangeState(fi, ST_NULL);
	FsmEvent(&chanp->lc_d->lcfi, EV_LC_RELEASE, NULL);
}

static void
lli_no_dchan_ready(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	if (chanp->debug & 1)
		link_debug(chanp, "STAT_DHUP", 0);
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
		link_debug(chanp, "STAT_DHUP", 0);
	ic.driver = chanp->cs->myid;
	ic.command = ISDN_STAT_DHUP;
	ic.arg = chanp->chan;
	chanp->cs->iif.statcallb(&ic);
	chanp->d_st->lli.l4l3(chanp->d_st, CC_DLRL, chanp->proc);
	chanp->Flags = 0;
	FsmChangeState(fi, ST_NULL);
	FsmEvent(&chanp->lc_d->lcfi, EV_LC_RELEASE, NULL);
}

static void
lli_no_setup_rsp(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	FsmChangeState(fi, ST_NULL);
	test_and_clear_bit(FLG_CALL_SEND, &chanp->Flags);
	if (chanp->debug & 1)
		link_debug(chanp, "STAT_DHUP", 0);
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
	if (test_and_clear_bit(FLG_LL_DCONN, &chanp->Flags)) {
		if (chanp->debug & 1)
			link_debug(chanp, "STAT_DHUP", 0);
		lli_deliver_cause(chanp);
		ic.driver = chanp->cs->myid;
		ic.command = ISDN_STAT_DHUP;
		ic.arg = chanp->chan;
		chanp->cs->iif.statcallb(&ic);
	}
	test_and_set_bit(FLG_DISC_SEND, &chanp->Flags);	/* DISCONN was sent from L3 */
}

static void
lli_connect_err(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	FsmChangeState(fi, ST_WAIT_DRELEASE);
	if (test_and_clear_bit(FLG_LL_DCONN, &chanp->Flags)) {
		if (chanp->debug & 1)
			link_debug(chanp, "STAT_DHUP", 0);
		lli_deliver_cause(chanp);
		ic.driver = chanp->cs->myid;
		ic.command = ISDN_STAT_DHUP;
		ic.arg = chanp->chan;
		chanp->cs->iif.statcallb(&ic);
	}
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
		chanp->lc_b->l2_establish = 0;	/* direct reset in lc_b->lcfi */
		FsmEvent(&chanp->lc_b->lcfi, EV_LC_RELEASE, NULL);
	}
	if (test_and_clear_bit(FLG_LL_BCONN, &chanp->Flags)) {
		if (chanp->debug & 1)
			link_debug(chanp, "STAT_BHUP", 0);
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
		if (test_and_clear_bit(FLG_LL_DCONN, &chanp->Flags)) {
			if (chanp->debug & 1)
				link_debug(chanp, "STAT_DHUP", 0);
			if (chanp->cs->protocol == ISDN_PTYPE_EURO) {
				chanp->proc->para.cause = 0x2f;
				chanp->proc->para.loc = 0;
			} else {
				chanp->proc->para.cause = 0x70;
				chanp->proc->para.loc = 0;
			}
			lli_deliver_cause(chanp);
			ic.driver = chanp->cs->myid;
			ic.command = ISDN_STAT_DHUP;
			ic.arg = chanp->chan;
			chanp->cs->iif.statcallb(&ic);
		}
		chanp->d_st->lli.l4l3(chanp->d_st, CC_DLRL, chanp->proc);
		chanp->Flags = 0;
		FsmEvent(&chanp->lc_d->lcfi, EV_LC_RELEASE, NULL);
	}
	chanp->cs->cardmsg(chanp->cs, MDL_INFO_REL, (void *) chanp->chan);
}

/* *INDENT-OFF* */
static struct FsmNode fnlist[] HISAX_INITDATA =
{
	{ST_NULL,		EV_DIAL,		lli_prep_dialout},
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
	{ST_WAIT_DSHUTDOWN,	EV_SETUP_IND,		lli_deliver_call},
};
/* *INDENT-ON* */


#define FNCOUNT (sizeof(fnlist)/sizeof(struct FsmNode))

static void
lc_activate_l1(struct FsmInst *fi, int event, void *arg)
{
	struct LcFsm *lf = fi->userdata;

	FsmDelTimer(&lf->act_timer, 50);
	FsmChangeState(fi, ST_LC_ACTIVATE_WAIT);
	/* This timeout is to avoid a hang if no L1 activation is possible */
	FsmAddTimer(&lf->act_timer, 30000, EV_LC_TIMER, NULL, 50);
	lf->st->ma.manl1(lf->st, PH_ACTIVATE_REQ, NULL);
}

static void
lc_activated_from_l1(struct FsmInst *fi, int event, void *arg)
{
	struct LcFsm *lf = fi->userdata;

	if (lf->l2_establish)
		FsmChangeState(fi, ST_LC_DELAY);
	else {
		FsmChangeState(fi, ST_LC_CONNECTED);
		lf->lccall(lf, LC_ESTABLISH, NULL);
	}
}

static void
lc_l1_activated(struct FsmInst *fi, int event, void *arg)
{
	struct LcFsm *lf = fi->userdata;

	FsmDelTimer(&lf->act_timer, 50);
	FsmChangeState(fi, ST_LC_DELAY);
	/* This timer is needed for delay the first paket on a channel
	   to be shure that the other side is ready too */
	if (lf->delay)
		FsmAddTimer(&lf->act_timer, lf->delay, EV_LC_TIMER, NULL, 51);
	else
		FsmEvent(fi, EV_LC_TIMER, NULL);
}

static void
lc_start_l2(struct FsmInst *fi, int event, void *arg)
{
	struct LcFsm *lf = fi->userdata;

/*	if (!lf->st->l1.act_state)
		lf->st->l1.act_state = 2;
*/	if (lf->l2_establish) {
		FsmChangeState(fi, ST_LC_ESTABLISH_WAIT);
		if (lf->l2_start)
			lf->st->ma.manl2(lf->st, DL_ESTABLISH, NULL);
	} else {
		FsmChangeState(fi, ST_LC_CONNECTED);
		lf->lccall(lf, LC_ESTABLISH, NULL);
	}
}

static void
lc_connected(struct FsmInst *fi, int event, void *arg)
{
	struct LcFsm *lf = fi->userdata;

	FsmDelTimer(&lf->act_timer, 50);
	FsmChangeState(fi, ST_LC_CONNECTED);
	lf->lccall(lf, LC_ESTABLISH, NULL);
}

static void
lc_release_l2(struct FsmInst *fi, int event, void *arg)
{
	struct LcFsm *lf = fi->userdata;

	if (lf->l2_establish) {
		FsmChangeState(fi, ST_LC_RELEASE_WAIT);
		lf->st->ma.manl2(lf->st, DL_RELEASE, NULL);
	} else {
		FsmChangeState(fi, ST_LC_NULL);
		lf->st->ma.manl1(lf->st, PH_DEACTIVATE_REQ, NULL);
		lf->lccall(lf, LC_RELEASE, NULL);
	}
}

static void
lc_l2_released(struct FsmInst *fi, int event, void *arg)
{
	struct LcFsm *lf = fi->userdata;

	FsmChangeState(fi, ST_LC_RELEASE_WAIT);
	FsmDelTimer(&lf->act_timer, 51);
	/* This delay is needed for send out the UA frame before
	 * PH_DEACTIVATE the interface
	 */
	FsmAddTimer(&lf->act_timer, 20, EV_LC_TIMER, NULL, 54);
}

static void
lc_release_l1(struct FsmInst *fi, int event, void *arg)
{
	struct LcFsm *lf = fi->userdata;

	FsmDelTimer(&lf->act_timer, 54);
	FsmChangeState(fi, ST_LC_NULL);
	lf->st->ma.manl1(lf->st, PH_DEACTIVATE_REQ, NULL);
	lf->lccall(lf, LC_RELEASE, NULL);
}

static void
lc_l1_deactivated(struct FsmInst *fi, int event, void *arg)
{
	struct LcFsm *lf = fi->userdata;

	FsmDelTimer(&lf->act_timer, 54);
	FsmChangeState(fi, ST_LC_NULL);
	lf->lccall(lf, LC_RELEASE, NULL);
}
/* *INDENT-OFF* */
static struct FsmNode LcFnList[] HISAX_INITDATA =
{
	{ST_LC_NULL,  		EV_LC_ESTABLISH,	lc_activate_l1},
	{ST_LC_NULL,  		EV_LC_PH_ACTIVATE,	lc_activated_from_l1},
	{ST_LC_NULL,  		EV_LC_DL_ESTABLISH,	lc_connected},
	{ST_LC_ACTIVATE_WAIT,	EV_LC_PH_ACTIVATE,	lc_l1_activated},
	{ST_LC_ACTIVATE_WAIT,	EV_LC_TIMER,		lc_release_l1},
	{ST_LC_ACTIVATE_WAIT,  	EV_LC_PH_DEACTIVATE,	lc_l1_deactivated},
	{ST_LC_DELAY,  		EV_LC_ESTABLISH,	lc_start_l2},
	{ST_LC_DELAY,		EV_LC_TIMER,		lc_start_l2},
	{ST_LC_DELAY,		EV_LC_DL_ESTABLISH,	lc_connected},
	{ST_LC_DELAY,  		EV_LC_PH_DEACTIVATE,	lc_l1_deactivated},
	{ST_LC_ESTABLISH_WAIT,	EV_LC_DL_ESTABLISH,	lc_connected},
	{ST_LC_ESTABLISH_WAIT,	EV_LC_RELEASE,		lc_release_l1},
	{ST_LC_ESTABLISH_WAIT,	EV_LC_DL_RELEASE,	lc_release_l1},
	{ST_LC_ESTABLISH_WAIT,  EV_LC_PH_DEACTIVATE,	lc_l1_deactivated},
	{ST_LC_CONNECTED,	EV_LC_ESTABLISH,	lc_connected},
	{ST_LC_CONNECTED,	EV_LC_RELEASE,		lc_release_l2},
	{ST_LC_CONNECTED,	EV_LC_DL_RELEASE,	lc_l2_released},
	{ST_LC_CONNECTED,  	EV_LC_PH_DEACTIVATE,	lc_l1_deactivated},
	{ST_LC_FLUSH_WAIT,	EV_LC_TIMER,		lc_release_l2},
	{ST_LC_FLUSH_WAIT,  	EV_LC_PH_DEACTIVATE,	lc_l1_deactivated},
	{ST_LC_RELEASE_WAIT,	EV_LC_DL_RELEASE,	lc_release_l1},
	{ST_LC_RELEASE_WAIT,	EV_LC_TIMER,		lc_release_l1},
	{ST_LC_FLUSH_WAIT,  	EV_LC_PH_DEACTIVATE,	lc_l1_deactivated},
};
/* *INDENT-ON* */


#define LC_FN_COUNT (sizeof(LcFnList)/sizeof(struct FsmNode))

HISAX_INITFUNC(void
CallcNew(void))
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
			releasestack_transl2(st);
			break;
	}
	/* Reset B-Channel Statemachine */
	FsmDelTimer(&chanp->lc_b->act_timer, 79);
	FsmChangeState(&chanp->lc_b->lcfi, ST_LC_NULL);
}

static void
dc_l1man(struct PStack *st, int pr, void *arg)
{
	struct Channel *chanp;

	chanp = (struct Channel *) st->lli.userdata;
	switch (pr) {
		case (PH_ACTIVATE_CNF):
		case (PH_ACTIVATE_IND):
			FsmEvent(&chanp->lc_d->lcfi, EV_LC_PH_ACTIVATE, NULL);
			break;
		case (PH_DEACTIVATE_IND):
			FsmEvent(&chanp->lc_d->lcfi, EV_LC_PH_DEACTIVATE, NULL);
			break;
	}
}

static void
dc_l2man(struct PStack *st, int pr, void *arg)
{
	struct Channel *chanp = (struct Channel *) st->lli.userdata;

	switch (pr) {
		case (DL_ESTABLISH):
			FsmEvent(&chanp->lc_d->lcfi, EV_LC_DL_ESTABLISH, NULL);
			break;
		case (DL_RELEASE):
			FsmEvent(&chanp->lc_d->lcfi, EV_LC_DL_RELEASE, NULL);
			break;
	}
}

static void
bc_l1man(struct PStack *st, int pr, void *arg)
{
	struct Channel *chanp = (struct Channel *) st->lli.userdata;

	switch (pr) {
		case (PH_ACTIVATE_IND):
		case (PH_ACTIVATE_CNF):
			FsmEvent(&chanp->lc_b->lcfi, EV_LC_PH_ACTIVATE, NULL);
			break;
		case (PH_DEACTIVATE_IND):
			FsmEvent(&chanp->lc_b->lcfi, EV_LC_PH_DEACTIVATE, NULL);
			break;
	}
}

static void
bc_l2man(struct PStack *st, int pr, void *arg)
{
	struct Channel *chanp = (struct Channel *) st->lli.userdata;

	switch (pr) {
		case (DL_ESTABLISH):
			FsmEvent(&chanp->lc_b->lcfi, EV_LC_DL_ESTABLISH, NULL);
			break;
		case (DL_RELEASE):
			FsmEvent(&chanp->lc_b->lcfi, EV_LC_DL_RELEASE, NULL);
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
ll_handler(struct l3_process *pc, int pr, void *arg)
{
	struct Channel *chanp;
	char tmp[64], tm[32];

	if (pr == CC_SETUP_IND) {
		if (!(chanp = selectfreechannel(pc->st))) {
			pc->st->lli.l4l3(pc->st, CC_DLRL, pc);
			return;
		} else {
			chanp->proc = pc;
			pc->chan = chanp;
			FsmEvent(&chanp->fi, EV_SETUP_IND, NULL);
			return;
		}
	} else if (pr == CC_ESTABLISH) {
		if (is_activ(pc->st)) {
			pc->st->lli.l4l3(pc->st, CC_ESTABLISH, pc);
			return;
		} else if (!(chanp = selectfreechannel(pc->st))) {
			pc->st->lli.l4l3(pc->st, CC_DLRL, pc);
			return;
		} else {
			chanp->proc = pc;
			FsmEvent(&chanp->fi, EV_ESTABLISH, NULL);
			return;
		}

			
	}
	chanp = pc->chan;
	switch (pr) {
		case (CC_DISCONNECT_IND):
			FsmEvent(&chanp->fi, EV_DISCONNECT_IND, NULL);
			break;
		case (CC_RELEASE_CNF):
			FsmEvent(&chanp->fi, EV_RELEASE_CNF, NULL);
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
		case (CC_PROCEEDING_IND):
		case (CC_ALERTING_IND):
			break;
		default:
			if (chanp->debug & 0x800) {
				jiftime(tm, jiffies);
				sprintf(tmp, "%s Channel %d L3->L4 unknown primitiv %d\n",
					tm, chanp->chan, pr);
				HiSax_putstatus(chanp->cs, tmp);
			}
	}
}

static void
init_d_st(struct Channel *chanp)
{
	struct PStack *st = chanp->d_st;
	struct IsdnCardState *cs = chanp->cs;
	char tmp[128];

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
	if (st->protocol == ISDN_PTYPE_1TR6)
		st->l2.T203 = 10000;	/* 10000 milliseconds */
	else
		st->l2.T203 = 10000;	/* 5000 milliseconds  */
	
	sprintf(tmp, "Channel %d q.921", chanp->chan);
	setstack_isdnl2(st, tmp);
	setstack_isdnl3(st, chanp);
	st->lli.userdata = chanp;
	st->lli.l2writewakeup = NULL;
	st->l3.l3l4 = ll_handler;
	st->l1.l1man = dc_l1man;
	st->l2.l2man = dc_l2man;
}

static void
callc_debug(struct FsmInst *fi, char *s)
{
	char str[80], tm[32];
	struct Channel *chanp = fi->userdata;

	jiftime(tm, jiffies);
	sprintf(str, "%s Channel %d callc %s\n", tm, chanp->chan, s);
	HiSax_putstatus(chanp->cs, str);
}

static void
lc_debug(struct FsmInst *fi, char *s)
{
	char str[256], tm[32];
	struct LcFsm *lf = fi->userdata;

	jiftime(tm, jiffies);
	sprintf(str, "%s Channel %d dc %s\n", tm, lf->ch->chan, s);
	HiSax_putstatus(lf->ch->cs, str);
}

static void
dlc_debug(struct FsmInst *fi, char *s)
{
	char str[256], tm[32];
	struct LcFsm *lf = fi->userdata;

	jiftime(tm, jiffies);
	sprintf(str, "%s Channel %d bc %s\n", tm, lf->ch->chan, s);
	HiSax_putstatus(lf->ch->cs, str);
}

static void
lccall_d(struct LcFsm *lf, int pr, void *arg)
{
	struct IsdnCardState *cs = lf->st->l1.hardware;
	struct Channel *chanp;
	int i;

	if (test_bit(FLG_TWO_DCHAN, &cs->HW_Flags)) {
		chanp = lf->ch;
		i = 1;
	} else {
		chanp = cs->channel;
		i = 0;
	}
	while (i < 2) {
		switch (pr) {
			case (LC_ESTABLISH):
				FsmEvent(&chanp->fi, EV_DLEST, NULL);
				break;
			case (LC_RELEASE):
				FsmEvent(&chanp->fi, EV_DLRL, NULL);
				break;
		}
		chanp++;
		i++;
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
	chanp->b_st = kmalloc(sizeof(struct PStack), GFP_ATOMIC);
	chanp->b_st->next = NULL;

	chanp->fi.fsm = &callcfsm;
	chanp->fi.state = ST_NULL;
	chanp->fi.debug = 0;
	chanp->fi.userdata = chanp;
	chanp->fi.printdebug = callc_debug;
	FsmInitTimer(&chanp->fi, &chanp->dial_timer);
	FsmInitTimer(&chanp->fi, &chanp->drel_timer);
	if (!chan || test_bit(FLG_TWO_DCHAN, &csta->HW_Flags)) {
		chanp->d_st = kmalloc(sizeof(struct PStack), GFP_ATOMIC);
		chanp->d_st->next = NULL;
		init_d_st(chanp);
		chanp->lc_d = kmalloc(sizeof(struct LcFsm), GFP_ATOMIC);
		chanp->lc_d->lcfi.fsm = &lcfsm;
		chanp->lc_d->lcfi.state = ST_LC_NULL;
		chanp->lc_d->lcfi.debug = 0;
		chanp->lc_d->lcfi.userdata = chanp->lc_d;
		chanp->lc_d->lcfi.printdebug = lc_debug;
		chanp->lc_d->type = LC_D;
		chanp->lc_d->delay = 0;
		chanp->lc_d->ch = chanp;
		chanp->lc_d->st = chanp->d_st;
		chanp->lc_d->l2_establish = !0;
		chanp->lc_d->l2_start = !0;
		chanp->lc_d->lccall = lccall_d;
		FsmInitTimer(&chanp->lc_d->lcfi, &chanp->lc_d->act_timer);
	} else {
		chanp->d_st = csta->channel->d_st;
		chanp->lc_d = csta->channel->lc_d;
	}
	chanp->lc_b = kmalloc(sizeof(struct LcFsm), GFP_ATOMIC);
	chanp->lc_b->lcfi.fsm = &lcfsm;
	chanp->lc_b->lcfi.state = ST_LC_NULL;
	chanp->lc_b->lcfi.debug = 0;
	chanp->lc_b->lcfi.userdata = chanp->lc_b;
	chanp->lc_b->lcfi.printdebug = dlc_debug;
	chanp->lc_b->type = LC_B;
	chanp->lc_b->delay = DEFAULT_B_DELAY;
	chanp->lc_b->ch = chanp;
	chanp->lc_b->st = chanp->b_st;
	chanp->lc_b->l2_establish = !0;
	chanp->lc_b->l2_start = !0;
	chanp->lc_b->lccall = lccall_b;
	FsmInitTimer(&chanp->lc_b->lcfi, &chanp->lc_b->act_timer);
	chanp->data_open = 0;
}

int
CallcNewChan(struct IsdnCardState *csta)
{
	chancount += 2;
	init_chan(0, csta);
	init_chan(1, csta);
	printk(KERN_INFO "HiSax: 2 channels added\n");
#ifdef LAYER2_WATCHING
	printk(KERN_INFO "LAYER2 ESTABLISH\n");
	test_and_set_bit(FLG_START_D, &csta->channel->Flags);
	FsmEvent(&csta->channel->lc_d->lcfi, EV_LC_ESTABLISH, NULL);
#endif
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
		FsmDelTimer(&csta->channel[i].lc_d->act_timer, 77);
		FsmDelTimer(&csta->channel[i].lc_b->act_timer, 76);
		if (i || test_bit(FLG_TWO_DCHAN, &csta->HW_Flags))
			release_d_st(csta->channel + i);
		if (csta->channel[i].b_st) {
			if (test_and_clear_bit(FLG_START_B, &csta->channel[i].Flags))
				release_b_st(csta->channel + i);
			kfree(csta->channel[i].b_st);
			csta->channel[i].b_st = NULL;
		} else
			printk(KERN_WARNING "CallcFreeChan b_st ch%d allready freed\n", i);
		if (csta->channel[i].lc_b) {
			kfree(csta->channel[i].lc_b);
			csta->channel[i].b_st = NULL;
		}
		if (i || test_bit(FLG_TWO_DCHAN, &csta->HW_Flags)) {
			release_d_st(csta->channel + i);
			FsmDelTimer(&csta->channel[i].lc_d->act_timer, 77);
			if (csta->channel[i].lc_d) {
				kfree(csta->channel[i].lc_d);
				csta->channel[i].d_st = NULL;
			} else
				printk(KERN_WARNING "CallcFreeChan lc_d ch%d allready freed\n", i);
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
		case (DL_DATA):
			if (chanp->data_open)
				chanp->cs->iif.rcvcallb_skb(chanp->cs->myid, chanp->chan, skb);
			else {
				dev_kfree_skb(skb);
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
	struct Channel *chanp = (struct Channel *) st->lli.userdata;
	struct sk_buff *skb = arg;

	switch (pr) {
		case (PH_DATA_IND):
			if (chanp->data_open)
				chanp->cs->iif.rcvcallb_skb(chanp->cs->myid, chanp->chan, skb);
			else {
				if (chanp->lc_b->lcfi.state == ST_LC_DELAY)
					FsmEvent(&chanp->lc_b->lcfi, EV_LC_DL_ESTABLISH, NULL);
				if (chanp->data_open) {
					link_debug(chanp, "channel now open", 0);
					chanp->cs->iif.rcvcallb_skb(chanp->cs->myid,
						chanp->chan, skb);
				} else
					dev_kfree_skb(skb);
			}
			break;
		default:
			printk(KERN_WARNING "lltrans_handler unknown primitive %d\n",
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
	ic.parm.length = len;
	chanp->cs->iif.statcallb(&ic);
}

static int
init_b_st(struct Channel *chanp, int incoming)
{
	struct PStack *st = chanp->b_st;
	struct IsdnCardState *cs = chanp->cs;
	char tmp[128];

	st->l1.hardware = cs;
	chanp->bcs->mode = 2;
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
			sprintf(tmp, "Channel %d x.75", chanp->chan);
			setstack_isdnl2(st, tmp);
			st->l2.l2l3 = lldata_handler;
			st->l1.l1man = bc_l1man;
			st->l2.l2man = bc_l2man;
			st->lli.userdata = chanp;
			st->lli.l1writewakeup = NULL;
			st->lli.l2writewakeup = ll_writewakeup;
			st->l2.l2m.debug = chanp->debug & 16;
			st->l2.debug = chanp->debug & 64;
			st->ma.manl2(st, MDL_NOTEIPROC, NULL);
			st->l1.mode = L1_MODE_HDLC;
			if (chanp->leased)
				st->l1.bc = chanp->chan & 1;
			else
				st->l1.bc = chanp->proc->para.bchannel - 1;
			break;
		case (ISDN_PROTO_L2_HDLC):
			st->l1.l1l2 = lltrans_handler;
			st->l1.l1man = bc_l1man;
			st->lli.userdata = chanp;
			st->lli.l1writewakeup = ll_writewakeup;
			st->l1.mode = L1_MODE_HDLC;
			if (chanp->leased)
				st->l1.bc = chanp->chan & 1;
			else
				st->l1.bc = chanp->proc->para.bchannel - 1;
			break;
		case (ISDN_PROTO_L2_TRANS):
			st->l1.l1l2 = lltrans_handler;
			st->l1.l1man = bc_l1man;
			st->lli.userdata = chanp;
			st->lli.l1writewakeup = ll_writewakeup;
			st->l1.mode = L1_MODE_TRANS;
			if (chanp->leased)
				st->l1.bc = chanp->chan & 1;
			else
				st->l1.bc = chanp->proc->para.bchannel - 1;
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
		chanp[i].d_st->l2.l2m.debug = debugflags & 8;
		chanp[i].b_st->l2.l2m.debug = debugflags & 0x10;
		chanp[i].d_st->l2.debug = debugflags & 0x20;
		chanp[i].b_st->l2.debug = debugflags & 0x40;
		chanp[i].lc_d->lcfi.debug = debugflags & 0x80;
		chanp[i].lc_b->lcfi.debug = debugflags & 0x100;
		chanp[i].b_st->ma.tei_m.debug = debugflags & 0x200;
		chanp[i].b_st->ma.debug = debugflags & 0x200;
		chanp[i].d_st->l1.l1m.debug = debugflags & 0x1000;
	}
	csta->dlogflag = debugflags & 4;
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
			if (chanp->debug & 1) {
				sprintf(tmp, "SETEAZ card %d %s", csta->cardnr + 1,
					ic->parm.num);
				link_debug(chanp, tmp, 1);
			}
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
			if (csta->channel[0].debug & 0x400) {
				jiftime(tmp, jiffies);
				i = strlen(tmp);
				sprintf(tmp + i, "   LOCK modcnt %d\n", MOD_USE_COUNT);
				HiSax_putstatus(csta, tmp);
			}
#endif				/* MODULE */
			break;
		case (ISDN_CMD_UNLOCK):
			HiSax_mod_dec_use_count();
#ifdef MODULE
			if (csta->channel[0].debug & 0x400) {
				jiftime(tmp, jiffies);
				i = strlen(tmp);
				sprintf(tmp + i, " UNLOCK modcnt %d\n", MOD_USE_COUNT);
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
					csta->channel[0].lc_b->delay = num;
					csta->channel[1].lc_b->delay = num;
					sprintf(tmp, "delay card %d set to %d ms\n",
						csta->cardnr + 1, num);
					HiSax_putstatus(csta, tmp);
					printk(KERN_DEBUG "HiSax: %s", tmp);
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
						sprintf(tmp, "Set LEASED wrong channel %d\n",
							num);
						HiSax_putstatus(csta, tmp);
						printk(KERN_WARNING "HiSax: %s", tmp);
					} else {
						num--;
						csta->channel[num].leased = 1;
						csta->channel[num].lc_d->l2_establish = 0;
						sprintf(tmp, "card %d channel %d set leased mode\n",
							csta->cardnr + 1, num + 1);
						HiSax_putstatus(csta, tmp);
						FsmEvent(&csta->channel[num].lc_d->lcfi, EV_LC_ESTABLISH, NULL);
					}
					break;
				case (6):	/* set B-channel test loop */
					num = *(unsigned int *) ic->parm.num;
					if (csta->stlist)
						csta->stlist->ma.manl1(csta->stlist,
							PH_TESTLOOP_REQ, (void *) num);
					break;
#ifdef MODULE
				case (55):
					while ( MOD_USE_COUNT > 0)
                                           MOD_DEC_USE_COUNT;
					HiSax_mod_inc_use_count();
					break;
#endif				/* MODULE */
				case (11):
					csta->debug = *(unsigned int *) ic->parm.num;
					sprintf(tmp, "l1 debugging flags card %d set to %x\n",
						csta->cardnr + 1, csta->debug);
					HiSax_putstatus(cards[0].cs, tmp);
					printk(KERN_DEBUG "HiSax: %s", tmp);
					break;
				case (13):
					csta->channel[0].d_st->l3.debug = *(unsigned int *) ic->parm.num;
					csta->channel[1].d_st->l3.debug = *(unsigned int *) ic->parm.num;
					sprintf(tmp, "l3 debugging flags card %d set to %x\n",
						csta->cardnr + 1, *(unsigned int *) ic->parm.num);
					HiSax_putstatus(cards[0].cs, tmp);
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
HiSax_writebuf_skb(int id, int chan, int ack, struct sk_buff *skb)
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
	st = chanp->b_st;
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
		if ((len + chanp->bcs->tx_cnt) > MAX_DATA_MEM) {
			/* Must return 0 here, since this is not an error
			 * but a temporary lack of resources.
			 */
			if (chanp->debug & 0x800) {
				sprintf(tmp, "writebuf: no buffers for %d bytes", len);
				link_debug(chanp, tmp, 1);
			}
			return 0;
		}
		save_flags(flags);
		cli();
		nskb = skb_clone(skb, GFP_ATOMIC);
		if (nskb) {
			if (!ack)
				nskb->pkt_type = PACKET_NOACK;
			if (chanp->lc_b->l2_establish)
				st->l3.l3l2(st, DL_DATA, nskb);
			else {
				chanp->bcs->tx_cnt += len;
				st->l2.l2l1(st, PH_DATA_REQ, nskb);
			}
			dev_kfree_skb(skb);
		} else
			len = 0;
		restore_flags(flags);
	}
	return (len);
}
