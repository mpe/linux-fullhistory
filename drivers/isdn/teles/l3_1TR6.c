/* $Id: l3_1TR6.c,v 1.4 1996/06/06 14:22:28 fritz Exp $
 *
 * $Log: l3_1TR6.c,v $
 * Revision 1.4  1996/06/06 14:22:28  fritz
 * Changed level of "non-digital call..." message, since
 * with audio support, this is quite normal.
 *
 * Revision 1.3  1996/04/30 21:54:42  isdn4dev
 * SPV, callback , remove some debugging code  Karsten Keil
 *
 * Revision 1.2  1996/04/20 16:47:23  fritz
 * Changed statemachine to allow reject of an incoming call.
 * Report all incoming calls, not just those with Service = 7.
 * Misc. typos
 *
 * Revision 1.1  1996/04/13 10:25:16  fritz
 * Initial revision
 *
 *
 */

static void
l3_1TR6_message(struct PStack *st, int mt, int pd)
{
	struct BufHeader *dibh;
	byte           *p;

	BufPoolGet(&dibh, st->l1.sbufpool, GFP_ATOMIC, (void *) st, 18);
	p = DATAPTR(dibh);
	p += st->l2.ihsize;

	*p++ = pd;
	*p++ = 0x1;
	*p++ = st->l3.callref;
	*p++ = mt;

	dibh->datasize = p - DATAPTR(dibh);
	i_down(st, dibh);
}

static void
l3_1tr6_setup(struct PStack *st, byte pr, void *arg)
{
	struct BufHeader *dibh;
	byte           *p;
	char           *teln;

	st->l3.callref = st->pa->callref;
	BufPoolGet(&dibh, st->l1.sbufpool, GFP_ATOMIC, (void *) st, 19);
	p = DATAPTR(dibh);
	p += st->l2.ihsize;

	*p++ = PROTO_DIS_N1;
	*p++ = 0x1;
	*p++ = st->l3.callref;
	*p++ = MT_N1_SETUP;

	if ('S' == (st->pa->called[0] & 0x5f)) {	/* SPV ??? */
		/* NSF SPV */
		*p++ = WE0_netSpecFac;
		*p++ = 4;	/* Laenge */
		*p++ = 0;
		*p++ = FAC_SPV;	/* SPV */
		*p++ = st->pa->info; /* 0 for all Services */
		*p++ = st->pa->info2; /* 0 for all Services */
		*p++ = WE0_netSpecFac;
		*p++ = 4;	/* Laenge */
		*p++ = 0;
		*p++ = FAC_Activate;	/* aktiviere SPV (default) */
		*p++ = st->pa->info; /* 0 for all Services */
		*p++ = st->pa->info2; /* 0 for all Services */
	}
	if (st->pa->calling[0] != '\0') {
		*p++ = WE0_origAddr;
		*p++ = strlen(st->pa->calling) + 1;
		/* Classify as AnyPref. */
		*p++ = 0x81;	/* Ext = '1'B, Type = '000'B, Plan = '0001'B. */
		teln = st->pa->calling;
		while (*teln)
			*p++ = *teln++ & 0x7f;
	}
	*p++ = WE0_destAddr;
	teln = st->pa->called;
	if ('S' != (st->pa->called[0] & 0x5f)) {	/* Keine SPV ??? */
		*p++ = strlen(st->pa->called) + 1;
		st->pa->spv = 0;
	} else {		/* SPV */
		*p++ = strlen(st->pa->called);
		teln++;		/* skip S */
		st->pa->spv = 1;
	}
	/* Classify as AnyPref. */
	*p++ = 0x81;		/* Ext = '1'B, Type = '000'B, Plan = '0001'B. */
	while (*teln)
		*p++ = *teln++ & 0x7f;

	*p++ = WE_Shift_F6;
	/* Codesatz 6 fuer Service */
	*p++ = WE6_serviceInd;
	*p++ = 2;		/* len=2 info,info2 */
	*p++ = st->pa->info;
	*p++ = st->pa->info2;

	dibh->datasize = p - DATAPTR(dibh);

	newl3state(st, 1);
	i_down(st, dibh);

}

static void
l3_1tr6_tu_setup(struct PStack *st, byte pr, void *arg)
{
	byte           *p;
	struct BufHeader *ibh = arg;

	p = DATAPTR(ibh);
	p += st->l2.uihsize;
	st->pa->callref = getcallref(p);
	st->l3.callref = 0x80 + st->pa->callref;

	/* Channel Identification */
	p = DATAPTR(ibh);
	if ((p = findie(p + st->l2.uihsize, ibh->datasize - st->l2.uihsize,
			WE0_chanID, 0))) {
		st->pa->bchannel = p[2] & 0x3;
	} else
		printk(KERN_INFO "l3tu_setup: Channel ident not found\n");

	p = DATAPTR(ibh);

	if ((p = findie(p + st->l2.uihsize, ibh->datasize - st->l2.uihsize, WE6_serviceInd, 6))) {
		st->pa->info = p[2];
		st->pa->info2 = p[3];
	} else
		printk(KERN_INFO "l3s12(1TR6): ServiceIndicator not found\n");

	p = DATAPTR(ibh);
	if ((p = findie(p + st->l2.uihsize, ibh->datasize - st->l2.uihsize,
			WE0_destAddr, 0)))
		iecpy(st->pa->called, p, 1);
	else
		strcpy(st->pa->called, "");

	p = DATAPTR(ibh);
	if ((p = findie(p + st->l2.uihsize, ibh->datasize - st->l2.uihsize,
			WE0_origAddr, 0))) {
		iecpy(st->pa->calling, p, 1);
	} else
		strcpy(st->pa->calling, "");

	p = DATAPTR(ibh);
	st->pa->spv = 0;
	if ((p = findie(p + st->l2.uihsize, ibh->datasize - st->l2.uihsize,
			WE0_netSpecFac, 0))) {
		if ((FAC_SPV == p[3]) || (FAC_Activate == p[3]))
			st->pa->spv = 1;
	}
	BufPoolRelease(ibh);

        /* Signal all services, linklevel takes care of Service-Indicator */
	if (st->pa->info != 7) {
                printk(KERN_DEBUG "non-digital call: %s -> %s\n",
                       st->pa->calling,
                       st->pa->called);
	}
        newl3state(st, 6);
        st->l3.l3l4(st, CC_SETUP_IND, NULL);
}

static void
l3_1tr6_tu_setup_ack(struct PStack *st, byte pr, void *arg)
{
	byte           *p;
	struct BufHeader *ibh = arg;

	p = DATAPTR(ibh);
	if ((p = findie(p + st->l2.ihsize, ibh->datasize - st->l2.ihsize,
			WE0_chanID, 0))) {
		st->pa->bchannel = p[2] & 0x3;
	} else
		printk(KERN_INFO "octect 3 not found\n");


	BufPoolRelease(ibh);
	newl3state(st, 2);
}

static void
l3_1tr6_tu_call_sent(struct PStack *st, byte pr, void *arg)
{
	byte           *p;
	struct BufHeader *ibh = arg;

	p = DATAPTR(ibh);
	if ((p = findie(p + st->l2.ihsize, ibh->datasize - st->l2.ihsize,
			WE0_chanID, 0))) {
		st->pa->bchannel = p[2] & 0x3;
	} else
		printk(KERN_INFO "octect 3 not found\n");

	BufPoolRelease(ibh);
	newl3state(st, 3);
	st->l3.l3l4(st, CC_PROCEEDING_IND, NULL);
}

static void
l3_1tr6_tu_alert(struct PStack *st, byte pr, void *arg)
{
	byte           *p;
	struct BufHeader *ibh = arg;


	p = DATAPTR(ibh);
	if ((p = findie(p + st->l2.ihsize, ibh->datasize - st->l2.ihsize,
			WE6_statusCalled, 6))) {
		if (DEBUG_1TR6 > 2)
			printk(KERN_INFO "status called %x\n", p[2]);
	} else if (DEBUG_1TR6 > 0)
		printk(KERN_INFO "statusCalled not found\n");

	BufPoolRelease(ibh);
	newl3state(st, 4);
	st->l3.l3l4(st, CC_ALERTING_IND, NULL);
}

static void
l3_1tr6_tu_info(struct PStack *st, byte pr, void *arg)
{
	byte           *p;
	int             i,tmpcharge=0;
	char            a_charge[8];
	struct BufHeader *ibh = arg;

	p = DATAPTR(ibh);
	if ((p = findie(p + st->l2.ihsize, ibh->datasize - st->l2.ihsize,
			WE6_chargingInfo, 6))) {
		iecpy(a_charge, p, 1);
                for (i = 0; i < strlen (a_charge); i++) {
     	                tmpcharge *= 10;
     	                tmpcharge += a_charge[i] & 0xf;
     	        }
                if (tmpcharge > st->pa->chargeinfo) {
     	                st->pa->chargeinfo = tmpcharge;
     	                st->l3.l3l4 (st, CC_INFO_CHARGE, NULL);
     	        }
		if (DEBUG_1TR6 > 2)
			printk(KERN_INFO "chargingInfo %d\n", st->pa->chargeinfo);
	} else if (DEBUG_1TR6 > 2)
		printk(KERN_INFO "chargingInfo not found\n");

	BufPoolRelease(ibh);
}

static void
l3_1tr6_tu_info_s2(struct PStack *st, byte pr, void *arg)
{
	byte           *p;
	int             i;
	struct BufHeader *ibh = arg;

	if (DEBUG_1TR6 > 4) {
		p = DATAPTR(ibh);
		for (i = 0; i < ibh->datasize; i++) {
			printk(KERN_INFO "Info DATA %x\n", p[i]);
		}
	}
	BufPoolRelease(ibh);
}

static void
l3_1tr6_tu_connect(struct PStack *st, byte pr, void *arg)
{
	struct BufHeader *ibh = arg;

        st->pa->chargeinfo=0;
	BufPoolRelease(ibh);
	st->l3.l3l4(st, CC_SETUP_CNF, NULL);
	newl3state(st, 10);
}

static void
l3_1tr6_tu_rel(struct PStack *st, byte pr, void *arg)
{
	struct BufHeader *ibh = arg;

	BufPoolRelease(ibh);
	l3_1TR6_message(st, MT_N1_REL_ACK, PROTO_DIS_N1);
	st->l3.l3l4(st, CC_RELEASE_IND, NULL);
	newl3state(st, 0);
}

static void
l3_1tr6_tu_rel_ack(struct PStack *st, byte pr, void *arg)
{
	struct BufHeader *ibh = arg;

	BufPoolRelease(ibh);
	newl3state(st, 0);
	st->l3.l3l4(st, CC_RELEASE_CNF, NULL);
}

static void
l3_1tr6_tu_disc(struct PStack *st, byte pr, void *arg)
{
	struct BufHeader *ibh = arg;
	byte           *p;
	int             i,tmpcharge=0;
	char            a_charge[8];

	p = DATAPTR(ibh);
	if ((p = findie(p + st->l2.ihsize, ibh->datasize - st->l2.ihsize,
			WE6_chargingInfo, 6))) {
		iecpy(a_charge, p, 1);
                for (i = 0; i < strlen (a_charge); i++) {
     	                tmpcharge *= 10;
     	                tmpcharge += a_charge[i] & 0xf;
     	        }
                if (tmpcharge > st->pa->chargeinfo) {
     	                st->pa->chargeinfo = tmpcharge;
     	                st->l3.l3l4 (st, CC_INFO_CHARGE, NULL);
     	        }
		if (DEBUG_1TR6 > 2)
			printk(KERN_INFO "chargingInfo %d\n", st->pa->chargeinfo);
	} else if (DEBUG_1TR6 > 2)
		printk(KERN_INFO "chargingInfo not found\n");

	p = DATAPTR(ibh);
	if ((p = findie(p + st->l2.ihsize, ibh->datasize - st->l2.ihsize,
			WE0_cause, 0))) {
		if (p[1] > 0) {
			st->pa->cause = p[2];
		} else {
			st->pa->cause = 0;
		}
		if (DEBUG_1TR6 > 1)
			printk(KERN_INFO "Cause %x\n", st->pa->cause);
	} else if (DEBUG_1TR6 > 0)
		printk(KERN_INFO "Cause not found\n");

	BufPoolRelease(ibh);
	newl3state(st, 12);
	st->l3.l3l4(st, CC_DISCONNECT_IND, NULL);
}


static void
l3_1tr6_tu_connect_ack(struct PStack *st, byte pr, void *arg)
{
	struct BufHeader *ibh = arg;

	BufPoolRelease(ibh);
	st->pa->chargeinfo = 0;
	st->l3.l3l4(st, CC_SETUP_COMPLETE_IND, NULL);
	newl3state(st, 10);
}

static void
l3_1tr6_alert(struct PStack *st, byte pr,
	      void *arg)
{
	l3_1TR6_message(st, MT_N1_ALERT, PROTO_DIS_N1);
	newl3state(st, 7);
}

static void
l3_1tr6_conn(struct PStack *st, byte pr,
	     void *arg)
{
	struct BufHeader *dibh;
	byte *p;

	st->l3.callref = 0x80 + st->pa->callref;

	BufPoolGet(&dibh, st->l1.sbufpool, GFP_ATOMIC, (void *) st, 20);
	p = DATAPTR(dibh);
	p += st->l2.ihsize;

	*p++ = PROTO_DIS_N1;
	*p++ = 0x1;
	*p++ = st->l3.callref;
	*p++ = MT_N1_CONN;

	if (st->pa->spv) {	/* SPV ??? */
		/* NSF SPV */
		*p++ = WE0_netSpecFac;
		*p++ = 4;	/* Laenge */
		*p++ = 0;
		*p++ = FAC_SPV;	/* SPV */
		*p++ = st->pa->info;
		*p++ = st->pa->info2;
		*p++ = WE0_netSpecFac;
		*p++ = 4;	/* Laenge */
		*p++ = 0;
		*p++ = FAC_Activate;	/* aktiviere SPV */
		*p++ = st->pa->info;
		*p++ = st->pa->info2;
	}
	dibh->datasize = p - DATAPTR(dibh);

	i_down(st, dibh);

	newl3state(st, 8);
}

static void
l3_1tr6_ignore(struct PStack *st, byte pr, void *arg)
{
	newl3state(st, 0);
}

static void
l3_1tr6_disconn_req(struct PStack *st, byte pr, void *arg)
{
	struct BufHeader *dibh;
	byte             *p;
        byte             rejflg;

	BufPoolGet(&dibh, st->l1.sbufpool, GFP_ATOMIC, (void *) st, 21);
	p = DATAPTR(dibh);
	p += st->l2.ihsize;

	*p++ = PROTO_DIS_N1;
	*p++ = 0x1;
	*p++ = st->l3.callref;
	*p++ = MT_N1_DISC;

        if (st->l3.state == 7) {
                rejflg = 1;
                *p++ = WE0_cause;       /* Anruf abweisen                */
                *p++ = 0x01;            /* Laenge = 1                    */
                *p++ = CAUSE_CallRejected;
        } else {
                rejflg = 0;
                *p++ = WE0_cause;
                *p++ = 0x0;             /* Laenge = 0 normales Ausloesen */
        }

	dibh->datasize = p - DATAPTR(dibh);

	i_down(st, dibh);

        newl3state(st, 11);
}

static void
l3_1tr6_rel_req(struct PStack *st, byte pr, void *arg)
{
	l3_1TR6_message(st, MT_N1_REL, PROTO_DIS_N1);
	newl3state(st, 19);
}

static struct stateentry downstatelist_1tr6t[] =
{
	{0, CC_SETUP_REQ, l3_1tr6_setup},
	{4, CC_DISCONNECT_REQ, l3_1tr6_disconn_req},
	{6, CC_REJECT_REQ, l3_1tr6_ignore},
	{6, CC_SETUP_RSP, l3_1tr6_conn},
	{6, CC_ALERTING_REQ, l3_1tr6_alert},
	{7, CC_SETUP_RSP, l3_1tr6_conn},
	{7, CC_DISCONNECT_REQ, l3_1tr6_disconn_req},
        {7, CC_DLRL, l3_1tr6_disconn_req},
	{8, CC_DISCONNECT_REQ, l3_1tr6_disconn_req},
	{10, CC_DISCONNECT_REQ, l3_1tr6_disconn_req},
	{12, CC_RELEASE_REQ, l3_1tr6_rel_req}
};

static int      downsl_1tr6t_len = sizeof(downstatelist_1tr6t) /
sizeof(struct stateentry);

static struct stateentry datastatelist_1tr6t[] =
{
	{0, MT_N1_SETUP, l3_1tr6_tu_setup},
	{0, MT_N1_REL, l3_1tr6_tu_rel},
	{1, MT_N1_SETUP_ACK, l3_1tr6_tu_setup_ack},
	{1, MT_N1_CALL_SENT, l3_1tr6_tu_call_sent},
	{1, MT_N1_REL, l3_1tr6_tu_rel},
	{1, MT_N1_DISC, l3_1tr6_tu_disc},
	{2, MT_N1_CALL_SENT, l3_1tr6_tu_call_sent},
	{2, MT_N1_ALERT, l3_1tr6_tu_alert},
	{2, MT_N1_CONN, l3_1tr6_tu_connect},
	{2, MT_N1_REL, l3_1tr6_tu_rel},
	{2, MT_N1_DISC, l3_1tr6_tu_disc},
	{2, MT_N1_INFO, l3_1tr6_tu_info_s2},
	{3, MT_N1_ALERT, l3_1tr6_tu_alert},
	{3, MT_N1_CONN, l3_1tr6_tu_connect},
	{3, MT_N1_REL, l3_1tr6_tu_rel},
	{3, MT_N1_DISC, l3_1tr6_tu_disc},
	{4, MT_N1_ALERT, l3_1tr6_tu_alert},
	{4, MT_N1_CONN, l3_1tr6_tu_connect},
	{4, MT_N1_REL, l3_1tr6_tu_rel},
	{4, MT_N1_DISC, l3_1tr6_tu_disc},
	{7, MT_N1_REL, l3_1tr6_tu_rel},
	{7, MT_N1_DISC, l3_1tr6_tu_disc},
	{8, MT_N1_REL, l3_1tr6_tu_rel},
	{8, MT_N1_DISC, l3_1tr6_tu_disc},
	{8, MT_N1_CONN_ACK, l3_1tr6_tu_connect_ack},
	{10, MT_N1_REL, l3_1tr6_tu_rel},
	{10, MT_N1_DISC, l3_1tr6_tu_disc},
	{10, MT_N1_INFO, l3_1tr6_tu_info},
	{11, MT_N1_REL, l3_1tr6_tu_rel},
	{12, MT_N1_REL, l3_1tr6_tu_rel},
	{19, MT_N1_REL_ACK, l3_1tr6_tu_rel_ack}
};

static int      datasl_1tr6t_len = sizeof(datastatelist_1tr6t) /
sizeof(struct stateentry);
