/* $Id: isdnl3.c,v 1.9 1996/06/06 14:22:27 fritz Exp $
 *
 * $Log: isdnl3.c,v $
 * Revision 1.9  1996/06/06 14:22:27  fritz
 * Changed level of "non-digital call..." message, since
 * with audio support, this is quite normal.
 *
 * Revision 1.8  1996/06/03 20:35:04  fritz
 * Fixed typos.
 *
 * Revision 1.7  1996/06/03 20:03:39  fritz
 * Fixed typos.
 *
 * Revision 1.6  1996/05/21 11:33:50  keil
 * Adding SETUP_ACKNOWLEDGE as answer of a SETUP message.
 *
 * Revision 1.5  1996/05/18 01:37:16  fritz
 * Added spelling corrections and some minor changes
 * to stay in sync with kernel.
 *
 * Revision 1.4  1996/05/17 03:46:16  fritz
 * General cleanup.
 *
 * Revision 1.3  1996/04/30 21:57:53  isdn4dev
 * remove some debugging code, improve callback   Karsten Keil
 *
 * Revision 1.2  1996/04/20 16:45:05  fritz
 * Changed to report all incoming calls to Linklevel, not just those
 * with Service 7.
 * Misc. typos
 *
 * Revision 1.1  1996/04/13 10:24:45  fritz
 * Initial revision
 *
 *
 */
#define __NO_VERSION__
#define P_1TR6
#include "teles.h"
#include "l3_1TR6.h"
#define DEBUG_1TR6 0

static void
i_down(struct PStack *st,
       struct BufHeader *ibh)
{
	st->l3.l3l2(st, DL_DATA, ibh);
}

static void
newl3state(struct PStack *st, int state)
{
	st->l3.state = state;
	if (DEBUG_1TR6 > 4)
		printk(KERN_INFO "isdnl3: bc:%d cr:%x new state %d\n",
		       st->pa->bchannel, st->pa->callref, state);

}

static void
l3_message(struct PStack *st, int mt)
{
	struct BufHeader *dibh;
	byte           *p;
	int             size;

	BufPoolGet(&dibh, st->l1.sbufpool, GFP_ATOMIC, (void *) st, 18);
	p = DATAPTR(dibh);
	p += st->l2.ihsize;
	size = st->l2.ihsize;

	*p++ = 0x8;
	*p++ = 0x1;
	*p++ = st->l3.callref;
	*p++ = mt;
	size += 4;

	dibh->datasize = size;
	i_down(st, dibh);
}

static void
l3s3(struct PStack *st, byte pr, void *arg)
{
	l3_message(st, MT_RELEASE);
	newl3state(st, 19);
}

static void
l3s4(struct PStack *st, byte pr, void *arg)
{
	struct BufHeader *ibh = arg;

	BufPoolRelease(ibh);
	newl3state(st, 0);
	st->l3.l3l4(st, CC_RELEASE_CNF, NULL);
}

static void
l3s4_1(struct PStack *st, byte pr, void *arg)
{
	struct BufHeader *ibh = arg;

	BufPoolRelease(ibh);
	newl3state(st, 19);
	l3_message(st, MT_RELEASE);
	st->l3.l3l4(st, CC_RELEASE_CNF, NULL);
}

static void
l3s5(struct PStack *st, byte pr,
     void *arg)
{
	struct BufHeader *dibh;
	byte           *p;
	char           *teln;

	st->l3.callref = st->pa->callref;
	BufPoolGet(&dibh, st->l1.sbufpool, GFP_ATOMIC, (void *) st, 19);
	p = DATAPTR(dibh);
	p += st->l2.ihsize;

	*p++ = 0x8;
	*p++ = 0x1;
	*p++ = st->l3.callref;
	*p++ = MT_SETUP;
	*p++ = 0xa1;

	/*
         * Set Bearer Capability, Map info from 1TR6-convention to EDSS1
         */
	switch (st->pa->info) {
	  case 1:		/* Telephony                               */
		  *p++ = 0x4;	/* BC-IE-code                              */
		  *p++ = 0x3;	/* Length                                  */
		  *p++ = 0x90;	/* Coding Std. national, 3.1 kHz audio     */
		  *p++ = 0x90;	/* Circuit-Mode 64kbps                     */
		  *p++ = 0xa3;	/* A-Law Audio                             */
		  break;
	  case 5:		/* Datatransmission 64k, BTX               */
	  case 7:		/* Datatransmission 64k                    */
	  default:
		  *p++ = 0x4;	/* BC-IE-code                              */
		  *p++ = 0x2;	/* Length                                  */
		  *p++ = 0x88;	/* Coding Std. nat., unrestr. dig. Inform. */
		  *p++ = 0x90;	/* Packet-Mode 64kbps                      */
		  break;
	}
	/*
	 * What about info2? Mapping to High-Layer-Compatibility?
	 */
	if (st->pa->calling[0] != '\0') {
		*p++ = 0x6c;
		*p++ = strlen(st->pa->calling) + 1;
		/* Classify as AnyPref. */
		*p++ = 0x81;	/* Ext = '1'B, Type = '000'B, Plan = '0001'B. */
		teln = st->pa->calling;
		while (*teln)
			*p++ = *teln++ & 0x7f;
	}
	*p++ = 0x70;
	*p++ = strlen(st->pa->called) + 1;
	/* Classify as AnyPref. */
	*p++ = 0x81;		/* Ext = '1'B, Type = '000'B, Plan = '0001'B. */

	teln = st->pa->called;
	while (*teln)
		*p++ = *teln++ & 0x7f;


	dibh->datasize = p - DATAPTR(dibh);

	newl3state(st, 1);
	i_down(st, dibh);

}

static void
l3s6(struct PStack *st, byte pr, void *arg)
{
	byte           *p;
	struct BufHeader *ibh = arg;

	p = DATAPTR(ibh);
	if ((p = findie(p + st->l2.ihsize, ibh->datasize - st->l2.ihsize,
			0x18, 0))) {
		st->pa->bchannel = p[2] & 0x3;
	} else
		printk(KERN_WARNING "octect 3 not found\n");

	BufPoolRelease(ibh);
	newl3state(st, 3);
	st->l3.l3l4(st, CC_PROCEEDING_IND, NULL);
}

static void
l3s7(struct PStack *st, byte pr, void *arg)
{
	struct BufHeader *ibh = arg;

	BufPoolRelease(ibh);
	newl3state(st, 12);
	st->l3.l3l4(st, CC_DISCONNECT_IND, NULL);
}

static void
l3s8(struct PStack *st, byte pr, void *arg)
{
	struct BufHeader *ibh = arg;

	BufPoolRelease(ibh);
	st->l3.l3l4(st, CC_SETUP_CNF, NULL);
	newl3state(st, 10);
}

static void
l3s11(struct PStack *st, byte pr, void *arg)
{
	struct BufHeader *ibh = arg;

	BufPoolRelease(ibh);
	newl3state(st, 4);
	st->l3.l3l4(st, CC_ALERTING_IND, NULL);
}

static void
l3s12(struct PStack *st, byte pr, void *arg)
{
	byte           *p;
	int		bcfound = 0;
	struct BufHeader *ibh = arg;

	p = DATAPTR(ibh);
	p += st->l2.uihsize;
	st->pa->callref = getcallref(p);
	st->l3.callref = 0x80 + st->pa->callref;

	/*
         * Channel Identification
         */
	p = DATAPTR(ibh);
	if ((p = findie(p + st->l2.uihsize, ibh->datasize - st->l2.uihsize,
			0x18, 0))) {
		st->pa->bchannel = p[2] & 0x3;
		bcfound++ ;
	} else
		printk(KERN_WARNING "l3s12: Channel ident not found\n");

	p = DATAPTR(ibh);
	if (st->protocol == ISDN_PTYPE_1TR6) {
		if ((p = findie(p + st->l2.uihsize, ibh->datasize - st->l2.uihsize, 0x01, 6))) {
			st->pa->info = p[2];
			st->pa->info2 = p[3];
		} else
			printk(KERN_WARNING "l3s12(1TR6): ServiceIndicator not found\n");
	} else {
		/*
                 * Bearer Capabilities
                 */
		if ((p = findie(p + st->l2.uihsize, ibh->datasize - st->l2.uihsize, 0x04, 0))) {
			switch (p[2] & 0x1f) {
			  case 0x00:
                                  /* Speech */
			  case 0x10:
                                  /* 3.1 Khz audio */
				  st->pa->info = 1;
				  break;
			  case 0x08:
                                  /* Unrestricted digital information */
				  st->pa->info = 7;
				  break;
			  case 0x09:
                                  /* Restricted digital information */
				  st->pa->info = 2;
				  break;
			  case 0x11:
                                  /* Unrestr. digital information  with tones/announcements */
				  st->pa->info = 3;
				  break;
			  case 0x18:
                                  /* Video */
				  st->pa->info = 4;
				  break;
			  default:
				  st->pa->info = 0;
			}
		} else
			printk(KERN_WARNING "l3s12: Bearer capabilities not found\n");
	}

	p = DATAPTR(ibh);
	if ((p = findie(p + st->l2.uihsize, ibh->datasize - st->l2.uihsize,
			0x70, 0)))
		iecpy(st->pa->called, p, 1);
	else
		strcpy(st->pa->called, "");

	p = DATAPTR(ibh);
	if ((p = findie(p + st->l2.uihsize, ibh->datasize - st->l2.uihsize,
			0x6c, 0))) {
		if (st->protocol == ISDN_PTYPE_1TR6)
			iecpy(st->pa->calling, p, 1);
		else
			iecpy(st->pa->calling, p, 2);
	} else
		strcpy(st->pa->calling, "");
	BufPoolRelease(ibh);

        if (bcfound) {
                if (st->pa->info != 7) {
                        printk(KERN_DEBUG "non-digital call: %s -> %s\n",
                               st->pa->calling,
                               st->pa->called);
                }
                newl3state(st, 6);
                st->l3.l3l4(st, CC_SETUP_IND, NULL);
        }
}

static void
l3s13(struct PStack *st, byte pr, void *arg)
{
	newl3state(st, 0);
}

static void
l3s16(struct PStack *st, byte pr,
      void *arg)
{
	st->l3.callref = 0x80 + st->pa->callref;
	l3_message(st, MT_CONNECT);
	newl3state(st, 8);
}

static void
l3s17(struct PStack *st, byte pr, void *arg)
{
	struct BufHeader *ibh = arg;

	BufPoolRelease(ibh);
	st->l3.l3l4(st, CC_SETUP_COMPLETE_IND, NULL);
	newl3state(st, 10);
}

static void
l3s18(struct PStack *st, byte pr, void *arg)
{
	struct BufHeader *dibh;
	byte           *p;
	int             size;

	BufPoolGet(&dibh, st->l1.sbufpool, GFP_ATOMIC, (void *) st, 20);
	p = DATAPTR(dibh);
	p += st->l2.ihsize;
	size = st->l2.ihsize;

	*p++ = 0x8;
	*p++ = 0x1;
	*p++ = st->l3.callref;
	*p++ = MT_DISCONNECT;
	size += 4;

	*p++ = IE_CAUSE;
	*p++ = 0x2;
	*p++ = 0x80;
	*p++ = 0x90;
	size += 4;

	dibh->datasize = size;
	i_down(st, dibh);

	newl3state(st, 11);
}

static void
l3s19(struct PStack *st, byte pr, void *arg)
{
	struct BufHeader *ibh = arg;

	BufPoolRelease(ibh);
	newl3state(st, 0);
	l3_message(st, MT_RELEASE_COMPLETE);
	st->l3.l3l4(st, CC_RELEASE_IND, NULL);
}

static void
l3s20(struct PStack *st, byte pr,
      void *arg)
{
	l3_message(st, MT_ALERTING);
	newl3state(st, 7);
}

struct stateentry {
	int             state;
	byte            primitive;
	void            (*rout) (struct PStack *, byte, void *);
};

static struct stateentry downstatelist[] =
{
        {0,CC_SETUP_REQ,l3s5},
        {1,CC_DISCONNECT_REQ,l3s18},
        {1,CC_RELEASE_REQ,l3s3},
        {1,CC_DLRL,l3s13},
        {3,CC_DISCONNECT_REQ,l3s18},
        {3,CC_RELEASE_REQ,l3s3},
        {3,CC_DLRL,l3s13},
        {4,CC_RELEASE_REQ,l3s3},
        {4,CC_DISCONNECT_REQ,l3s18},
        {4,CC_DLRL,l3s13},
        {6,CC_RELEASE_REQ,l3s3},
        {6,CC_DISCONNECT_REQ,l3s18},
        {6,CC_ALERTING_REQ,l3s20},
        {6,CC_DLRL,l3s13},
        {7,CC_RELEASE_REQ,l3s3},
        {7,CC_SETUP_RSP,l3s16},
        {7,CC_DLRL,l3s13},
        {8,CC_RELEASE_REQ,l3s3},
        {8,CC_DISCONNECT_REQ,l3s18},
        {8,CC_DLRL,l3s13},
        {10,CC_DISCONNECT_REQ,l3s18},
        {10,CC_RELEASE_REQ,l3s3},
        {10,CC_DLRL,l3s13},
        {11,CC_RELEASE_REQ,l3s3},
        {12,CC_RELEASE_REQ,l3s3},
        {19,CC_DLRL,l3s13},
};

static int      downsllen = sizeof(downstatelist) /
sizeof(struct stateentry);

static struct stateentry datastatelist[] =
{
        {0,MT_SETUP,l3s12},
        {1,MT_CALL_PROCEEDING,l3s6},
        {1,MT_SETUP_ACKNOWLEDGE,l3s6},
        {1,MT_RELEASE_COMPLETE,l3s4},
        {1,MT_RELEASE,l3s19},
        {1,MT_DISCONNECT,l3s7},
        {3,MT_DISCONNECT,l3s7},
        {3,MT_CONNECT,l3s8},
        {3,MT_ALERTING,l3s11},
        {3,MT_RELEASE,l3s19},
        {3,MT_RELEASE_COMPLETE,l3s4},
        {4,MT_CONNECT,l3s8},
        {4,MT_DISCONNECT,l3s7},
        {4,MT_RELEASE,l3s19},
        {4,MT_RELEASE_COMPLETE,l3s4},
        {6,MT_SETUP,l3s12},
        {7,MT_RELEASE,l3s19},
        {7,MT_RELEASE_COMPLETE,l3s4_1},
        {7,MT_DISCONNECT,l3s7},
        {8,MT_RELEASE,l3s19},
        {8,MT_CONNECT_ACKNOWLEDGE,l3s17},
        {8,MT_DISCONNECT,l3s7},
        {8,MT_RELEASE_COMPLETE,l3s4_1},
        {10,MT_DISCONNECT,l3s7},
        {10,MT_RELEASE,l3s19},
        {10,MT_RELEASE_COMPLETE,l3s4_1},
        {11,MT_RELEASE,l3s19},
        {11,MT_RELEASE_COMPLETE,l3s4},
        {19,MT_RELEASE_COMPLETE,l3s4},
};

static int      datasllen = sizeof(datastatelist) /
sizeof(struct stateentry);

#ifdef P_1TR6
#include "l3_1TR6.c"
#endif

static void
l3up(struct PStack *st,
     int pr, void *arg)
{
	int             i, mt, size;
	byte           *ptr;
	struct BufHeader *ibh = arg;

	if (pr == DL_DATA) {
		ptr = DATAPTR(ibh);
		ptr += st->l2.ihsize;
		size = ibh->datasize - st->l2.ihsize;
		mt = ptr[3];
		switch (ptr[0]) {
#ifdef P_1TR6
		  case PROTO_DIS_N0:
			  BufPoolRelease(ibh);
			  break;
		  case PROTO_DIS_N1:
			  for (i = 0; i < datasl_1tr6t_len; i++)
				  if ((st->l3.state == datastatelist_1tr6t[i].state) &&
				      (mt == datastatelist_1tr6t[i].primitive))
					  break;
			  if (i == datasl_1tr6t_len) {
				  BufPoolRelease(ibh);
				  if (DEBUG_1TR6 > 0)
					  printk(KERN_INFO "isdnl3up unhandled 1tr6 state %d MT %x\n",
						 st->l3.state, mt);
			  } else
				  datastatelist_1tr6t[i].rout(st, pr, ibh);
			  break;
#endif
		  default:	/* E-DSS1 */
			  for (i = 0; i < datasllen; i++)
				  if ((st->l3.state == datastatelist[i].state) &&
				      (mt == datastatelist[i].primitive))
					  break;
			  if (i == datasllen) {
				  BufPoolRelease(ibh);
				  if (DEBUG_1TR6 > 0)
			  	  	printk(KERN_INFO "isdnl3up unhandled E-DSS1 state %d MT %x\n",
				 		st->l3.state, mt);
			  } else
				  datastatelist[i].rout(st, pr, ibh);
		}
	} else if (pr == DL_UNIT_DATA) {
		ptr = DATAPTR(ibh);
		ptr += st->l2.uihsize;
		size = ibh->datasize - st->l2.uihsize;
		mt = ptr[3];
		switch (ptr[0]) {
#ifdef P_1TR6
		  case PROTO_DIS_N0:
			  BufPoolRelease(ibh);
			  break;
		  case PROTO_DIS_N1:
			  for (i = 0; i < datasl_1tr6t_len; i++)
				  if ((st->l3.state == datastatelist_1tr6t[i].state) &&
				      (mt == datastatelist_1tr6t[i].primitive))
					  break;
			  if (i == datasl_1tr6t_len) {
				  if (DEBUG_1TR6 > 0) {
					  printk(KERN_INFO "isdnl3up unhandled 1tr6 state %d MT %x\n"
						 ,st->l3.state, mt);
				  }
				  BufPoolRelease(ibh);
			  } else
				  datastatelist_1tr6t[i].rout(st, pr, ibh);
			  break;
#endif
		  default:	/* E-DSS1 */
			  for (i = 0; i < datasllen; i++)
				  if ((st->l3.state == datastatelist[i].state) &&
				      (mt == datastatelist[i].primitive))
					  break;
			  if (i == datasllen) {
				  BufPoolRelease(ibh);
				  if (DEBUG_1TR6 > 0)
			  	  	printk(KERN_INFO "isdnl3up unhandled E-DSS1 state %d MT %x\n",
				 		st->l3.state, mt);
			  } else
				  datastatelist[i].rout(st, pr, ibh);
		}
	}
}

static void
l3down(struct PStack *st,
       int pr, void *arg)
{
	int             i;
	struct BufHeader *ibh = arg;

	switch (st->protocol) {
#ifdef P_1TR6
	  case ISDN_PTYPE_1TR6:
		  for (i = 0; i < downsl_1tr6t_len; i++)
			  if ((st->l3.state == downstatelist_1tr6t[i].state) &&
			      (pr == downstatelist_1tr6t[i].primitive))
				  break;
		  if (i == downsl_1tr6t_len) {
			  if (DEBUG_1TR6 > 0) {
				  printk(KERN_INFO "isdnl3down unhandled 1tr6 state %d primitive %x\n", st->l3.state, pr);
			  }
		  } else
			  downstatelist_1tr6t[i].rout(st, pr, ibh);
		  break;
#endif
	  default:
		  for (i = 0; i < downsllen; i++)
			  if ((st->l3.state == downstatelist[i].state) &&
			      (pr == downstatelist[i].primitive))
				  break;
		  if (i == downsllen) {
			  if (DEBUG_1TR6 > 0) {
				  printk(KERN_INFO "isdnl3down unhandled E-DSS1 state %d primitive %x\n", st->l3.state, pr);
			  }
		  } else
			  downstatelist[i].rout(st, pr, ibh);
	}
}

void
setstack_isdnl3(struct PStack *st)
{
	st->l4.l4l3 = l3down;
	st->l2.l2l3 = l3up;
	st->l3.state = 0;
	st->l3.callref = 0;
	st->l3.debug = 0;
}
