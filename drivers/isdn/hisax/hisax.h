/* $Id: hisax.h,v 1.11 1997/02/11 01:36:02 keil Exp $

 *   Basic declarations, defines and prototypes
 *
 * $Log: hisax.h,v $
 * Revision 1.11  1997/02/11 01:36:02  keil
 * New Param structure
 *
 * Revision 1.10  1997/02/09 00:23:52  keil
 * new interface handling, one interface per card
 *
 * Revision 1.9  1997/01/27 23:18:44  keil
 * prototype for releasestack_isdnl3
 *
 * Revision 1.8  1997/01/27 16:02:37  keil
 * new cards, callc timers, HZDELAY macro, HiSax_getrev prototype
 *
 * Revision 1.7  1997/01/21 22:22:14  keil
 * changes for 2.0; Elsa Quickstep support
 *
 * Revision 1.6  1997/01/04 13:48:28  keil
 * primitiv for MDL_REMOVE added
 *
 * Revision 1.5  1996/12/08 19:49:19  keil
 * Monitor channel support
 *
 * Revision 1.4  1996/11/18 15:35:39  keil
 * some changes for ELSA cards
 *
 * Revision 1.3  1996/11/05 19:37:23  keil
 * using config.h
 *
 * Revision 1.2  1996/10/27 22:21:52  keil
 * CallFlags for broadcast messages
 *
 * Revision 1.1  1996/10/13 20:03:46  keil
 * Initial revision
 *
 *
 *
 */
#include <linux/module.h>
#include <linux/config.h>
#include <linux/version.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/major.h>
#include <asm/segment.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/malloc.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/ioport.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/isdnif.h>
#include <linux/tty.h>

#define PH_ACTIVATE   1
#define PH_DATA       2
#define PH_DEACTIVATE 3

#define MDL_ASSIGN       4
#define DL_UNIT_DATA     5
#define SC_STARTUP       6
#define CC_ESTABLISH     7
#define DL_ESTABLISH     8
#define DL_DATA          9
#define CC_S_STATUS_ENQ  10

#define CC_CONNECT       15
#define CC_CONNECT_ACKNOWLEDGE 16
#define CO_EOF                 17
#define SC_DISCONNECT          18
#define CO_DTMF                19
#define DL_RELEASE             20
#define DL_FLUSH               21

#define CO_ALARM               22
#define CC_REJECT              23

#define CC_SETUP_REQ           24
#define CC_SETUP_CNF           25
#define CC_SETUP_IND           26
#define CC_SETUP_RSP           27
#define CC_SETUP_COMPLETE_IND  28

#define CC_DISCONNECT_REQ      29
#define CC_DISCONNECT_IND      30

#define CC_RELEASE_CNF         31
#define CC_RELEASE_IND         32
#define CC_RELEASE_REQ         33

#define CC_REJECT_REQ          34

#define CC_PROCEEDING_IND      35

#define CC_DLRL                36
#define CC_DLEST               37

#define CC_ALERTING_REQ        38
#define CC_ALERTING_IND        39

#define DL_STOP                40
#define DL_START               41

#define MDL_NOTEIPROC          46

#define LC_ESTABLISH           47
#define LC_RELEASE             48

#define PH_REQUEST_PULL        49
#define PH_PULL_ACK            50
#define	PH_DATA_PULLED	       51
#define CC_INFO_CHARGE         52

#define CC_MORE_INFO           53
#define CC_IGNORE              54

#define MDL_REMOVE             56
#define MDL_VERIFY             57

#define CC_T303                60
#define CC_T304                61
#define CC_T305                62
#define CC_T308_1              64
#define CC_T308_2              65
#define CC_T310                66
#define CC_T313                67
#define CC_T318                68
#define CC_T319                69

#define CC_NOSETUP_RSP_ERR     70
#define CC_SETUP_ERR           71
#define CC_CONNECT_ERR         72
#define CC_RELEASE_ERR         73

/*
 * Message-Types
 */

#define MT_ALERTING            0x01
#define MT_CALL_PROCEEDING     0x02
#define MT_CONNECT             0x07
#define MT_CONNECT_ACKNOWLEDGE 0x0f
#define MT_PROGRESS            0x03
#define MT_SETUP               0x05
#define MT_SETUP_ACKNOWLEDGE   0x0d
#define MT_RESUME              0x26
#define MT_RESUME_ACKNOWLEDGE  0x2e
#define MT_RESUME_REJECT       0x22
#define MT_SUSPEND             0x25
#define MT_SUSPEND_ACKNOWLEDGE 0x2d
#define MT_SUSPEND_REJECT      0x21
#define MT_USER_INFORMATION    0x20
#define MT_DISCONNECT          0x45
#define MT_RELEASE             0x4d
#define MT_RELEASE_COMPLETE    0x5a
#define MT_RESTART             0x46
#define MT_RESTART_ACKNOWLEDGE 0x4e
#define MT_SEGMENT             0x60
#define MT_CONGESTION_CONTROL  0x79
#define MT_INFORMATION         0x7b
#define MT_FACILITY            0x62
#define MT_NOTIFY              0x6e
#define MT_STATUS              0x7d
#define MT_STATUS_ENQUIRY      0x75

#define IE_CAUSE               0x08

struct HscxIoctlArg {
	int channel;
	int mode;
	int transbufsize;
};

#ifdef __KERNEL__

#undef DEBUG_MAGIC

#define HSCX_SBUF_ORDER     1
#define HSCX_SBUF_BPPS      2
#define HSCX_SBUF_MAXPAGES  3

#define HSCX_RBUF_ORDER     1
#define HSCX_RBUF_BPPS      2
#define HSCX_RBUF_MAXPAGES  3

#define HSCX_SMALLBUF_ORDER     0
#define HSCX_SMALLBUF_BPPS      40
#define HSCX_SMALLBUF_MAXPAGES  1

#define ISAC_SBUF_ORDER     0
#define ISAC_SBUF_BPPS      16
#define ISAC_SBUF_MAXPAGES  1

#define ISAC_RBUF_ORDER     0
#define ISAC_RBUF_BPPS      16
#define ISAC_RBUF_MAXPAGES  1

#define ISAC_SMALLBUF_ORDER     0
#define ISAC_SMALLBUF_BPPS      40
#define ISAC_SMALLBUF_MAXPAGES  1

#define byte unsigned char

#define MAX_WINDOW 8

byte *Smalloc(int size, int pr, char *why);
void Sfree(byte * ptr);

/*
 * Statemachine
 */
struct Fsm {
	int *jumpmatrix;
	int state_count, event_count;
	char **strEvent, **strState;
};

struct FsmInst {
	struct Fsm *fsm;
	int state;
	int debug;
	void *userdata;
	int userint;
	void (*printdebug) (struct FsmInst *, char *);
};

struct FsmNode {
	int state, event;
	void (*routine) (struct FsmInst *, int, void *);
};

struct FsmTimer {
	struct FsmInst *fi;
	struct timer_list tl;
	int event;
	void *arg;
};

struct L3Timer {
	struct PStack *st;
	struct timer_list tl;
	int event;
};

struct BufHeader {
#ifdef DEBUG_MAGIC
	int magic;
#endif
	struct BufHeader *next;
	struct BufPool *bp;
	int datasize;
	byte primitive, where;
	void *heldby;
};

struct Pages {
	struct Pages *next;
};

struct BufPool {
#ifdef DEBUG_MAGIC
	int magic;
#endif
	struct BufHeader *freelist;
	struct Pages *pageslist;
	int pageorder;
	int pagescount;
	int bpps;
	int bufsize;
	int maxpages;
};

struct BufQueue {
#ifdef DEBUG_MAGIC
	int magic;
#endif
	struct BufHeader *head, *tail;
};

struct Layer1 {
	void *hardware;
	int hscx;
	struct BufPool *sbufpool, *rbufpool, *smallpool;
	struct PStack **stlistp;
	int act_state;
	void (*l1l2) (struct PStack *, int, struct BufHeader *);
	void (*l1man) (struct PStack *, int, void *);
	int hscxmode, hscxchannel, requestpull;
};

struct Layer2 {
	int sap, tei, ces;
	int extended, laptype;
	int uihsize, ihsize;
	int vs, va, vr;
	struct BufQueue i_queue;
	int window, orig;
	int rejexp;
	int debug;
	struct BufHeader *windowar[MAX_WINDOW];
	int sow;
	struct FsmInst l2m;
	void (*l2l1) (struct PStack *, int, struct BufHeader *);
	void (*l2l1discardq) (struct PStack *, int, void *, int);
	void (*l2man) (struct PStack *, int, void *);
	void (*l2l3) (struct PStack *, int, void *);
	void (*l2tei) (struct PStack *, int, void *);
	struct FsmTimer t200_timer, t203_timer;
	int t200, n200, t203;
	int rc, t200_running;
	char debug_id[32];
};

struct Layer3 {
	void (*l3l4) (struct PStack *, int, struct BufHeader *);
	void (*l3l2) (struct PStack *, int, void *);
	int state, callref;
	struct L3Timer timer;
	int t303, t304, t305, t308, t310, t313, t318, t319;
	int n_t303;
	int debug;
	int channr;
};

struct Layer4 {
	void (*l4l3) (struct PStack *, int, void *);
	void *userdata;
	void (*l1writewakeup) (struct PStack *);
	void (*l2writewakeup) (struct PStack *);
};

struct Management {
	void (*manl1) (struct PStack *, int, void *);
	void (*manl2) (struct PStack *, int, void *);
	void (*teil2) (struct PStack *, int, void *);
};

struct Param {
	int cause;
	int loc;
	int bchannel;
	int callref;		/* Callreferenz Number */
	setup_parm setup;       /* from isdnif.h numbers and Serviceindicator */
	int chargeinfo;		/* Charge Info - only for 1tr6 in
				 * the moment
				 */
	int spv;		/* SPV Flag */
};

struct PStack {
	struct PStack *next;
	struct Layer1 l1;
	struct Layer2 l2;
	struct Layer3 l3;
	struct Layer4 l4;
	struct Management ma;
	struct Param *pa;
	int protocol;		/* EDSS1 or 1TR6 */
};

struct HscxState {
	int inuse, init, active;
	struct BufPool sbufpool, rbufpool, smallpool;
	struct IsdnCardState *sp;
	int hscx, mode;
	int transbufsize, receive;
	struct BufHeader *rcvibh, *xmtibh;
	int rcvptr, sendptr;
	struct PStack *st;
	struct tq_struct tqueue;
	int event;
	struct BufQueue rq, sq;
	int releasebuf;
#ifdef DEBUG_MAGIC
	int magic;		/* 301270 */
#endif
};

struct LcFsm {
	struct FsmInst lcfi;
	int type;
	struct Channel *ch;
	void (*lccall) (struct LcFsm *, int, void *);
	struct PStack *st;
	int l2_establish;
	int l2_start;
	struct FsmTimer act_timer;
	char debug_id[32];
};

struct Channel {
	struct PStack ds, is;
	struct IsdnCardState *sp;
	int hscx;
	int chan;
	int incoming;
	struct FsmInst fi;
	struct LcFsm lc_d, lc_b;
	struct Param para;
	struct FsmTimer drel_timer, dial_timer;
	int debug;
#ifdef DEBUG_MAGIC
	int magic;		/* 301272 */
#endif
	int l2_protocol, l2_active_protocol;
	int l2_primitive, l2_headersize;
	int data_open;
	int outcallref;
	int impair;
	int Flags;		/* for remembering action done in l4 */
	int leased;
};

struct IsdnCardState {
#ifdef DEBUG_MAGIC
	int magic;
#endif
	unsigned char typ;
	unsigned char subtyp;
	int protocol;
	unsigned int irq;
	unsigned int cfg_reg;
	unsigned int membase;
	unsigned int isac;
	unsigned int hscx[2];
	unsigned int counter;
	int	     myid;
	isdn_if	     iif;
	byte	*status_buf;
	byte    *status_read;
	byte    *status_write;
	byte    *status_end;
	struct BufHeader *mon_rx, *mon_tx;
	int mon_rxp, mon_txp, mon_flg;
	void (*ph_command) (struct IsdnCardState *, unsigned int);
	void (*modehscx) (struct HscxState *, int, int);
	void (*hscx_fill_fifo) (struct HscxState *);
	void (*isac_fill_fifo) (struct IsdnCardState *);
	struct BufPool sbufpool, rbufpool, smallpool;
	struct Channel channel[2];
	struct PStack *stlist;
	struct BufHeader *xmtibh, *rcvibh;
	int rcvptr, sendptr;
	int event;
	struct tq_struct tqueue;
	int ph_active;
	struct BufQueue rq, sq;
	int cardnr;
	int ph_state;
	struct PStack *teistack;
	struct HscxState hs[2];
	int dlogflag;
	char *dlogspace;
	int debug;
	int releasebuf;
	unsigned int CallFlags;
};

#define  MON0_RX	1
#define  MON1_RX	2
#define  MON0_TX	4
#define  MON1_TX	8

#define  ISDN_CTYPE_16_0	1
#define  ISDN_CTYPE_8_0		2
#define  ISDN_CTYPE_16_3	3
#define  ISDN_CTYPE_PNP		4
#define  ISDN_CTYPE_A1		5
#define  ISDN_CTYPE_ELSA	6
#define  ISDN_CTYPE_ELSA_QS1000	7
#define  ISDN_CTYPE_TELESPCMCIA	8
#define  ISDN_CTYPE_IX1MICROR2	9

#define  ISDN_CTYPE_COUNT	9

#ifdef	CONFIG_HISAX_16_0
#define  CARD_TELES0 (1<< ISDN_CTYPE_16_0) | (1<< ISDN_CTYPE_8_0)
#else
#define  CARD_TELES0  0
#endif

#ifdef	CONFIG_HISAX_16_3
#define  CARD_TELES3 (1<< ISDN_CTYPE_16_3) | (1<< ISDN_CTYPE_PNP) | \
		     (1<< ISDN_CTYPE_TELESPCMCIA)
#else
#define  CARD_TELES3  0
#endif

#ifdef	CONFIG_HISAX_AVM_A1
#define  CARD_AVM_A1 (1<< ISDN_CTYPE_A1)
#else
#define  CARD_AVM_A1  0
#endif

#ifdef	CONFIG_HISAX_ELSA_PCC
#define  CARD_ELSA (1<< ISDN_CTYPE_ELSA) | (1<< ISDN_CTYPE_ELSA_QS1000)
#else
#define  CARD_ELSA  0
#endif

#ifdef	CONFIG_HISAX_IX1MICROR2
#define	CARD_IX1MICROR2 (1 << ISDN_CTYPE_IX1MICROR2)
#else
#define CARD_IX1MICROR2 0
#endif

#define  SUPORTED_CARDS  (CARD_TELES0 | CARD_TELES3 | CARD_AVM_A1 | CARD_ELSA \
			 | CARD_IX1MICROR2)

struct IsdnCard {
	int typ;
	int protocol;		/* EDSS1 or 1TR6 */
	unsigned int para[3];
	struct IsdnCardState *sp;
};

#define DATAPTR(x) ((byte *)x+sizeof(struct BufHeader))

#define LAPD 0
#define LAPB 1

void BufPoolInit(struct BufPool *bp, int order, int bpps,
		 int maxpages);
int BufPoolAdd(struct BufPool *bp, int priority);
void BufPoolFree(struct BufPool *bp);
int BufPoolGet(struct BufHeader **bh, struct BufPool *bp,
	       int priority, void *heldby, int where);
void BufPoolRelease(struct BufHeader *bh);
void BufQueueLink(struct BufQueue *bq, struct BufHeader *bh);
int BufQueueUnlink(struct BufHeader **bh, struct BufQueue *bq);
void BufQueueInit(struct BufQueue *bq);
void BufQueueRelease(struct BufQueue *bq);
void BufQueueDiscard(struct BufQueue *q, int pr, void *heldby,
		     int releasetoo);
int BufQueueLength(struct BufQueue *bq);
void BufQueueLinkFront(struct BufQueue *bq, struct BufHeader *bh);

void l2down(struct PStack *st, byte pr, struct BufHeader *ibh);
void l2up(struct PStack *st, byte pr, struct BufHeader *ibh);
void acceptph(struct PStack *st, struct BufHeader *ibh);
void setstack_isdnl2(struct PStack *st, char *debug_id);
int HiSax_inithardware(void);
void HiSax_closehardware(void);

void setstack_HiSax(struct PStack *st, struct IsdnCardState *sp);
unsigned int randomces(void);
void setstack_isdnl3(struct PStack *st, struct Channel *chanp );
void HiSax_addlist(struct IsdnCardState *sp, struct PStack *st);
void releasestack_isdnl2(struct PStack *st);
void releasestack_isdnl3(struct PStack *st);
void HiSax_rmlist(struct IsdnCardState *sp, struct PStack *st);
void newcallref(struct PStack *st);

int setstack_hscx(struct PStack *st, struct HscxState *hs);
byte *findie(byte * p, int size, byte ie, int wanted_set);
int getcallref(byte * p);

void FsmNew(struct Fsm *fsm, struct FsmNode *fnlist, int fncount);
void FsmFree(struct Fsm *fsm);
int FsmEvent(struct FsmInst *fi, int event, void *arg);
void FsmChangeState(struct FsmInst *fi, int newstate);
void FsmInitTimer(struct FsmInst *fi, struct FsmTimer *ft);
int FsmAddTimer(struct FsmTimer *ft, int millisec,
		int event, void *arg, int where);
void FsmDelTimer(struct FsmTimer *ft, int where);
int FsmTimerRunning(struct FsmTimer *ft);
void jiftime(char *s, long mark);

int HiSax_command(isdn_ctrl * ic);
int HiSax_writebuf(int id, int chan, const u_char * buf, int count, int user);
void HiSax_putstatus(struct IsdnCardState *csta, char *buf);
void HiSax_reportcard(int cardnr);
int ListLength(struct BufHeader *ibh);
int QuickHex(char *txt, byte * p, int cnt);
void LogFrame(struct IsdnCardState *sp, byte * p, int size);
void dlogframe(struct IsdnCardState *sp, byte * p, int size, char *comment);
void iecpy(byte * dest, byte * iestart, int ieoffset);
void setstack_transl2(struct PStack *st);
void releasestack_transl2(struct PStack *st);
void close_hscxstate(struct HscxState *);
void setstack_tei(struct PStack *st);




#define PART_SIZE(order,bpps) (( (PAGE_SIZE<<order) -\
  sizeof(void *))/bpps)
#define BUFFER_SIZE(order,bpps) (PART_SIZE(order,bpps)-\
  sizeof(struct BufHeader))

#endif				/* __KERNEL__ */

#define HZDELAY(jiffs) {int tout = jiffs; while (tout--) udelay(1000000/HZ);}

int ll_run(struct IsdnCardState *csta);
void ll_stop(struct IsdnCardState *csta);
void CallcNew(void);
void CallcFree(void);
int CallcNewChan(struct IsdnCardState *csta);
void CallcFreeChan(struct IsdnCardState *csta);
void Isdnl2New(void);
void Isdnl2Free(void);
void init_tei(struct IsdnCardState *sp, int protocol);
void release_tei(struct IsdnCardState *sp);
char *HiSax_getrev(const char *revision);
