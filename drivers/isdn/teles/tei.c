/* $Id: tei.c,v 1.1 1996/04/13 10:28:25 fritz Exp $
 *
 * $Log: tei.c,v $
 * Revision 1.1  1996/04/13 10:28:25  fritz
 * Initial revision
 *
 *
 */
#define __NO_VERSION__
#include "teles.h"

extern struct IsdnCard cards[];
extern int      nrcards;

static struct PStack *
findces(struct PStack *st, int ces)
{
	struct PStack  *ptr = *(st->l1.stlistp);

	while (ptr)
		if (ptr->l2.ces == ces)
			return (ptr);
		else
			ptr = ptr->next;
	return (NULL);
}

static struct PStack *
findtei(struct PStack *st, int tei)
{
	struct PStack  *ptr = *(st->l1.stlistp);

	if (tei == 127)
		return (NULL);

	while (ptr)
		if (ptr->l2.tei == tei)
			return (ptr);
		else
			ptr = ptr->next;
	return (NULL);
}

void 
tei_handler(struct PStack *st,
	    byte pr, struct BufHeader *ibh)
{
	byte           *bp;
	unsigned int    tces;
	struct PStack  *otsp, *ptr;
	unsigned int    data;

	if (st->l2.debug)
		printk(KERN_DEBUG "teihandler %d\n", pr);

	switch (pr) {
	  case (MDL_ASSIGN):
		  data = (unsigned int) ibh;
		  BufPoolGet(&ibh, st->l1.smallpool, GFP_ATOMIC, (void *) st, 6);
		  if (!ibh)
			  return;
		  bp = DATAPTR(ibh);
		  bp += st->l2.uihsize;
		  bp[0] = 0xf;
		  bp[1] = data >> 8;
		  bp[2] = data & 0xff;
		  bp[3] = 0x1;
		  bp[4] = 0xff;
		  ibh->datasize = 8;
		  st->l3.l3l2(st, DL_UNIT_DATA, ibh);
		  break;
	  case (DL_UNIT_DATA):
		  bp = DATAPTR(ibh);
		  bp += 3;
		  if (bp[0] != 0xf)
			  break;
		  switch (bp[3]) {
		    case (2):
			    tces = (bp[1] << 8) | bp[2];
			    BufPoolRelease(ibh);
			    if (st->l3.debug)
				    printk(KERN_DEBUG "tei identity assigned for %d=%d\n", tces,
					   bp[4] >> 1);
			    if ((otsp = findces(st, tces)))
				    otsp->ma.teil2(otsp, MDL_ASSIGN,
						   (void *)(bp[4] >> 1));
			    break;
		    case (4):
			    if (st->l3.debug)
				    printk(KERN_DEBUG "checking identity for %d\n", bp[4] >> 1);
			    if (bp[4] >> 1 == 0x7f) {
				    BufPoolRelease(ibh);
				    ptr = *(st->l1.stlistp);
				    while (ptr) {
					    if ((ptr->l2.tei & 0x7f) != 0x7f) {
						    if (BufPoolGet(&ibh, st->l1.smallpool, GFP_ATOMIC, (void *) st, 7))
							    break;
						    bp = DATAPTR(ibh);
						    bp += 3;
						    bp[0] = 0xf;
						    bp[1] = ptr->l2.ces >> 8;
						    bp[2] = ptr->l2.ces & 0xff;
						    bp[3] = 0x5;
						    bp[4] = (ptr->l2.tei << 1) | 1;
						    ibh->datasize = 8;
						    st->l3.l3l2(st, DL_UNIT_DATA, ibh);
					    }
					    ptr = ptr->next;
				    }
			    } else {
				    otsp = findtei(st, bp[4] >> 1);
				    BufPoolRelease(ibh);
				    if (!otsp)
					    break;
				    if (st->l3.debug)
					    printk(KERN_DEBUG "ces is %d\n", otsp->l2.ces);
				    if (BufPoolGet(&ibh, st->l1.smallpool, GFP_ATOMIC, (void *) st, 7))
					    break;
				    bp = DATAPTR(ibh);
				    bp += 3;
				    bp[0] = 0xf;
				    bp[1] = otsp->l2.ces >> 8;
				    bp[2] = otsp->l2.ces & 0xff;
				    bp[3] = 0x5;
				    bp[4] = (otsp->l2.tei << 1) | 1;
				    ibh->datasize = 8;
				    st->l3.l3l2(st, DL_UNIT_DATA, ibh);
			    }
			    break;
		    default:
			    BufPoolRelease(ibh);
			    if (st->l3.debug)
				    printk(KERN_DEBUG "tei message unknown %d ai %d\n", bp[3], bp[4] >> 1);
		  }
		  break;
	  default:
		  printk(KERN_WARNING "tei handler unknown primitive %d\n", pr);
		  break;
	}
}

unsigned int 
randomces(void)
{
	int             x = jiffies & 0xffff;

	return (x);
}

static void 
tei_man(struct PStack *sp, int i, void *v)
{
	printk(KERN_DEBUG "tei_man\n");
}

static void 
tei_l2tei(struct PStack *st, int pr, void *arg)
{
	struct IsdnCardState *sp = st->l1.hardware;

	tei_handler(sp->teistack, pr, arg);
}

void 
setstack_tei(struct PStack *st)
{
	st->l2.l2tei = tei_l2tei;
}

static void 
init_tei(struct IsdnCardState *sp, int protocol)
{
	struct PStack  *st;
	char            tmp[128];

#define DIRTY_HACK_AGAINST_SIGSEGV

	st = (struct PStack *) Smalloc(sizeof(struct PStack), GFP_KERNEL,
				       "struct PStack");

#ifdef DIRTY_HACK_AGAINST_SIGSEGV
	sp->teistack = st;	                /* struct is not initialized yet */
	sp->teistack->protocol = protocol;	/* struct is not initialized yet */
#endif	                                        /* DIRTY_HACK_AGAINST_SIGSEGV    */


	setstack_teles(st, sp);

	st->l2.extended = !0;
	st->l2.laptype = LAPD;
	st->l2.window = 1;
	st->l2.orig = !0;
	st->protocol = protocol;

/*
 * the following is not necessary for tei mng. (broadcast only)
 */

	st->l2.t200 = 500;	/* 500 milliseconds */
	st->l2.n200 = 4;	/* try 4 times */

	st->l2.sap = 63;
	st->l2.tei = 127;

	sprintf(tmp, "Card %d tei ", sp->cardnr);
	setstack_isdnl2(st, tmp);
	st->l2.debug = 0;
	st->l3.debug = 0;

	st->ma.manl2(st, MDL_NOTEIPROC, NULL);

	st->l2.l2l3 = (void *) tei_handler;
	st->l1.l1man = tei_man;
	st->l2.l2man = tei_man;
	st->l4.l2writewakeup = NULL;
	
	teles_addlist(sp, st);
	sp->teistack = st;
}

static void 
release_tei(struct IsdnCardState *sp)
{
	struct PStack  *st = sp->teistack;

	teles_rmlist(sp, st);
	Sfree((void *) st);
}

void 
TeiNew(void)
{
	int             i;

	for (i = 0; i < nrcards; i++)
		if (cards[i].sp)
			init_tei(cards[i].sp, cards[i].protocol);
}

void 
TeiFree(void)
{
	int             i;

	for (i = 0; i < nrcards; i++)
		if (cards[i].sp)
			release_tei(cards[i].sp);
}
