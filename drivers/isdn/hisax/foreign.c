/* $Id: foreign.c,v 1.1 1998/11/09 07:48:48 baccala Exp $
 *
 * HiSax ISDN driver - foreign chipset interface
 *
 * Author       Brent Baccala (baccala@FreeSoft.org)
 *
 *
 *
 * $Log: foreign.c,v $
 * Revision 1.1  1998/11/09 07:48:48  baccala
 * Initial DBRI ISDN code.  Sometimes works (brings up the link and you
 * can telnet through it), sometimes doesn't (crashes the machine)
 *
 * Revision 1.2  1998/02/12 23:07:10  keil
 * change for 2.1.86 (removing FREE_READ/FREE_WRITE from [dev]_kfree_skb()
 *
 * Revision 1.1  1998/02/03 23:20:51  keil
 * New files for SPARC isdn support
 *
 * Revision 1.1  1998/01/08 04:17:12  baccala
 * ISDN comes to the Sparc.  Key points:
 *
 *    - Existing ISDN HiSax driver provides all the smarts
 *    - it compiles, runs, talks to an isolated phone switch, connects
 *      to a Cisco, pings go through
 *    - AMD 7930 support only (no DBRI yet)
 *    - no US NI-1 support (may not work on US phone system - untested)
 *    - periodic packet loss, apparently due to lost interrupts
 *    - ISDN sometimes freezes, requiring reboot before it will work again
 *
 * The code is unreliable enough to be consider alpha
 *
 *
 * 
 */

#define __NO_VERSION__
#include "hisax.h"
#include "isac.h"
#include "isdnl1.h"
#include "foreign.h"
#include "rawhdlc.h"
#include <linux/interrupt.h>

static const char *foreign_revision = "$Revision: 1.1 $";

#define RCV_BUFSIZE	1024	/* Size of raw receive buffer in bytes */
#define RCV_BUFBLKS	4	/* Number of blocks to divide buffer into
				 * (must divide RCV_BUFSIZE) */

static void Bchan_fill_fifo(struct BCState *, struct sk_buff *);

static void
Bchan_xmt_bh(struct BCState *bcs)
{
	struct sk_buff *skb;

	if (bcs->hw.foreign.tx_skb != NULL) {
		dev_kfree_skb(bcs->hw.foreign.tx_skb);
		bcs->hw.foreign.tx_skb = NULL;
	}

	if ((skb = skb_dequeue(&bcs->squeue))) {
		Bchan_fill_fifo(bcs, skb);
	} else {
		clear_bit(BC_FLG_BUSY, &bcs->Flag);
		bcs->event |= 1 << B_XMTBUFREADY;
		queue_task(&bcs->tqueue, &tq_immediate);
		mark_bh(IMMEDIATE_BH);
	}
}

static void
Bchan_xmit_callback(struct BCState *bcs)
{
	queue_task(&bcs->hw.foreign.tq_xmt, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
}

/* B channel transmission: two modes (three, if you count L1_MODE_NULL)
 *
 * L1_MODE_HDLC - We need to do HDLC encapsulation before transmiting
 * the packet (i.e. make_raw_hdlc_data).  Since this can be a
 * time-consuming operation, our completion callback just schedules
 * a bottom half to do encapsulation for the next packet.  In between,
 * the link will just idle
 *
 * L1_MODE_TRANS - Data goes through, well, transparent.  No HDLC encap,
 * and we can't just let the link idle, so the "bottom half" actually
 * gets called during the top half (it's our callback routine in this case),
 * but it's a lot faster now since we don't call make_raw_hdlc_data
 */

static void
Bchan_fill_fifo(struct BCState *bcs, struct sk_buff *skb)
{
	struct IsdnCardState *cs = bcs->cs;
	struct foreign_hw *hw = &bcs->hw.foreign;
	int len;

	if ((cs->debug & L1_DEB_HSCX) || (cs->debug & L1_DEB_HSCX_FIFO)) {
		char tmp[2048];
		char *t = tmp;

		t += sprintf(t, "Bchan_fill_fifo %c cnt %d",
			     bcs->channel ? 'B' : 'A', skb->len);
		if (cs->debug & L1_DEB_HSCX_FIFO)
			QuickHex(t, skb->data, skb->len);
		debugl1(cs, tmp);
	}

	if (hw->doHDLCprocessing) {
		len = make_raw_hdlc_data(skb->data, skb->len,
					 bcs->hw.foreign.tx_buff, RAW_BUFMAX);
		if (len > 0)
			cs->hw.foreign->bxmit(0, bcs->channel,
                                              bcs->hw.foreign.tx_buff, len,
                                              (void *) &Bchan_xmit_callback,
                                              (void *) bcs);
		dev_kfree_skb(skb);
	} else {
		cs->hw.foreign->bxmit(0, bcs->channel,
                                      skb->data, skb->len,
                                      (void *) &Bchan_xmit_callback,
                                      (void *) bcs);
		bcs->hw.foreign.tx_skb = skb;
	}
}

static void
Bchan_mode(struct BCState *bcs, int mode, int bc)
{
	struct IsdnCardState *cs = bcs->cs;

	if (cs->debug & L1_DEB_HSCX) {
		char tmp[40];
		sprintf(tmp, "foreign mode %d bchan %d/%d",
			mode, bc, bcs->channel);
		debugl1(cs, tmp);
	}
	bcs->mode = mode;
}

/* Bchan_l2l1 is the entry point for upper layer routines that want to
 * transmit on the B channel.  PH_DATA_REQ is a normal packet that
 * we either start transmitting (if idle) or queue (if busy).
 * PH_PULL_REQ can be called to request a callback message (PH_PULL_CNF)
 * once the link is idle.  After a "pull" callback, the upper layer
 * routines can use PH_PULL_IND to send data.
 */

static void
Bchan_l2l1(struct PStack *st, int pr, void *arg)
{
	struct sk_buff *skb = arg;

	switch (pr) {
		case (PH_DATA_REQ):
			if (test_bit(BC_FLG_BUSY, &st->l1.bcs->Flag)) {
				skb_queue_tail(&st->l1.bcs->squeue, skb);
			} else {
				test_and_set_bit(BC_FLG_BUSY, &st->l1.bcs->Flag);
				Bchan_fill_fifo(st->l1.bcs, skb);
			}
			break;
		case (PH_PULL_IND):
			if (test_bit(BC_FLG_BUSY, &st->l1.bcs->Flag)) {
				printk(KERN_WARNING "foreign: this shouldn't happen\n");
				break;
			}
			test_and_set_bit(BC_FLG_BUSY, &st->l1.bcs->Flag);
			Bchan_fill_fifo(st->l1.bcs, skb);
			break;
		case (PH_PULL_REQ):
			if (!test_bit(BC_FLG_BUSY, &st->l1.bcs->Flag)) {
				clear_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
				st->l1.l1l2(st, PH_PULL_CNF, NULL);
			} else
				set_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
			break;
	}
}

/*
****************************************************************************
***************** Receiver callback and bottom half ************************
****************************************************************************
*/

/* Bchan_recv_done() is called when a frame has been completely decoded
 * into hw->rv_skb and we're ready to hand it off to the HiSax upper
 * layer.  If a "large" packet is received, stick rv_skb on the
 * receive queue and alloc a new (large) skb to act as buffer for
 * future receives.  If a small packet is received, leave rv_skb
 * alone, alloc a new skb of the correct size, and copy the packet
 * into it.  In any case, flag the channel as B_RCVBUFREADY and
 * queue the upper layer's task.
 */

static void
Bchan_recv_done(struct BCState *bcs, unsigned int len)
{
	struct IsdnCardState *cs = bcs->cs;
	struct foreign_hw *hw = &bcs->hw.foreign;
	struct sk_buff *skb;

	if (cs->debug & L1_DEB_HSCX_FIFO) {
		char tmp[2048];
		char *t = tmp;

		t += sprintf(t, "Bchan_rcv %c cnt %d (%x)", bcs->channel ? 'B' : 'A', len, hw->rv_skb->tail);
		QuickHex(t, hw->rv_skb->tail, len);
		debugl1(cs, tmp);
	}

	if (len > HSCX_BUFMAX/2) {
		/* Large packet received */

		if (!(skb = dev_alloc_skb(HSCX_BUFMAX))) {
			printk(KERN_WARNING "foreign: receive out of memory\n");
		} else {
			skb_put(hw->rv_skb, len);
			skb_queue_tail(&bcs->rqueue, hw->rv_skb);
			hw->rv_skb = skb;

			bcs->event |= 1 << B_RCVBUFREADY;
			queue_task(&bcs->tqueue, &tq_immediate);
			mark_bh(IMMEDIATE_BH);
		}
	} else {
		/* Small packet received */

		if (!(skb = dev_alloc_skb(len))) {
			printk(KERN_WARNING "foreign: receive out of memory\n");
		} else {
			memcpy(skb_put(skb, len), hw->rv_skb->tail, len);
			skb_queue_tail(&bcs->rqueue, skb);

			bcs->event |= 1 << B_RCVBUFREADY;
			queue_task(&bcs->tqueue, &tq_immediate);
			mark_bh(IMMEDIATE_BH);
		}
	}
}

/* Bchan_recv_callback()'s behavior depends on whether we're doing local
 * HDLC processing.  If so, receive into hw->rv_buff and queue Bchan_rcv_bh
 * to decode the HDLC at leisure.  Otherwise, receive directly into hw->rv_skb
 * and call Bchan_recv_done().  In either case, prepare a new buffer for
 * further receives and hand it to the hardware driver.
 */

static void
Bchan_recv_callback(struct BCState *bcs, int error, unsigned int len)
{
	struct IsdnCardState *cs = bcs->cs;
	struct foreign_hw *hw = &bcs->hw.foreign;

	if (hw->doHDLCprocessing) {

		hw->rv_buff_in += RCV_BUFSIZE/RCV_BUFBLKS;
		hw->rv_buff_in %= RCV_BUFSIZE;

		if (hw->rv_buff_in != hw->rv_buff_out) {
			cs->hw.foreign->brecv(0, bcs->channel,
					      hw->rv_buff + hw->rv_buff_in,
					      RCV_BUFSIZE/RCV_BUFBLKS,
					      (void *) &Bchan_recv_callback,
					      (void *) bcs);

		}
		queue_task(&hw->tq_rcv, &tq_immediate);
		mark_bh(IMMEDIATE_BH);

	} else {
		if (error) {
			char tmp[256];
			sprintf(tmp, "B channel %c receive error %x",
				bcs->channel ? 'B' : 'A', error);
			debugl1(cs, tmp);
		} else {
			Bchan_recv_done(bcs, len);
		}
		cs->hw.foreign->brecv(0, bcs->channel,
				      hw->rv_skb->tail, HSCX_BUFMAX,
				      (void *) &Bchan_recv_callback,
				      (void *) bcs);

	}
}

/* Bchan_rcv_bh() is a "shim" bottom half handler stuck in between
 * Bchan_recv_callback() and the HiSax upper layer if we need to
 * do local HDLC processing.
 */

static void
Bchan_rcv_bh(struct BCState *bcs)
{
	struct IsdnCardState *cs = bcs->cs;
	struct foreign_hw *hw = &bcs->hw.foreign;
	struct sk_buff *skb;
	int len;

	if (cs->debug & L1_DEB_HSCX) {
		char tmp[1024];

		sprintf(tmp, "foreign_Bchan_rcv (%d/%d)",
			hw->rv_buff_in, hw->rv_buff_out);
		debugl1(cs, tmp);
	}

	do {
		if (cs->debug & L1_DEB_HSCX) {
			char tmp[1024];

			QuickHex(tmp, hw->rv_buff + hw->rv_buff_out,
				 RCV_BUFSIZE/RCV_BUFBLKS);
			debugl1(cs, tmp);
		}

		while ((len = read_raw_hdlc_data(hw->hdlc_state,
						 hw->rv_buff + hw->rv_buff_out, RCV_BUFSIZE/RCV_BUFBLKS,
						 hw->rv_skb->tail, HSCX_BUFMAX))) {
			if (len > 0) {
				Bchan_recv_done(bcs, len);
			} else {
				char tmp[256];
				sprintf(tmp, "B channel %c receive error",
					bcs->channel ? 'B' : 'A');
				debugl1(cs, tmp);
			}
		}

		if (hw->rv_buff_in == hw->rv_buff_out) {
			/* Buffer was filled up - need to restart receiver */
			cs->hw.foreign->brecv(0, bcs->channel,
                                              hw->rv_buff + hw->rv_buff_in,
                                              RCV_BUFSIZE/RCV_BUFBLKS,
                                              (void *) &Bchan_recv_callback,
                                              (void *) bcs);
		}

		hw->rv_buff_out += RCV_BUFSIZE/RCV_BUFBLKS;
		hw->rv_buff_out %= RCV_BUFSIZE;

	} while (hw->rv_buff_in != hw->rv_buff_out);
}

static void
Bchan_close(struct BCState *bcs)
{
	struct IsdnCardState *cs = bcs->cs;
	struct sk_buff *skb;

	Bchan_mode(bcs, 0, 0);
	cs->hw.foreign->bclose(0, bcs->channel);

	if (test_bit(BC_FLG_INIT, &bcs->Flag)) {
		while ((skb = skb_dequeue(&bcs->rqueue))) {
			dev_kfree_skb(skb);
		}
		while ((skb = skb_dequeue(&bcs->squeue))) {
			dev_kfree_skb(skb);
		}
	}
	test_and_clear_bit(BC_FLG_INIT, &bcs->Flag);
}

static int
Bchan_open(struct BCState *bcs)
{
	struct IsdnCardState *cs = bcs->cs;
	struct foreign_hw *hw = &bcs->hw.foreign;

	if (!test_and_set_bit(BC_FLG_INIT, &bcs->Flag)) {
		skb_queue_head_init(&bcs->rqueue);
		skb_queue_head_init(&bcs->squeue);
	}
	test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);

	hw->doHDLCprocessing = 0;
	if (bcs->mode == L1_MODE_HDLC) {
		if (cs->hw.foreign->bopen(0, bcs->channel, 1, 0xff) == -1) {
			if (cs->hw.foreign->bopen(0, bcs->channel, 0, 0xff) == -1) {
				return (-1);
			}
			hw->doHDLCprocessing = 1;
		}
	} else {
		if (cs->hw.foreign->bopen(0, bcs->channel, 0, 0xff) == -1) {
			return (-1);
		}
	}

	hw->rv_buff_in = 0;
	hw->rv_buff_out = 0;
	hw->tx_skb = NULL;
	init_hdlc_state(hw->hdlc_state, 0);
	cs->hw.foreign->brecv(0, bcs->channel,
                              hw->rv_buff + hw->rv_buff_in,
                              RCV_BUFSIZE/RCV_BUFBLKS,
                              (void *) &Bchan_recv_callback, (void *) bcs);

	bcs->event = 0;
	bcs->tx_cnt = 0;
	return (0);
}

static void
Bchan_init(struct BCState *bcs)
{
	if (!(bcs->hw.foreign.tx_buff = kmalloc(RAW_BUFMAX, GFP_ATOMIC))) {
		printk(KERN_WARNING
		       "HiSax: No memory for foreign.tx_buff\n");
		return;
	}
	if (!(bcs->hw.foreign.rv_buff = kmalloc(RCV_BUFSIZE, GFP_ATOMIC))) {
		printk(KERN_WARNING
		       "HiSax: No memory for foreign.rv_buff\n");
		return;
	}
	if (!(bcs->hw.foreign.rv_skb = dev_alloc_skb(HSCX_BUFMAX))) {
		printk(KERN_WARNING
		       "HiSax: No memory for foreign.rv_skb\n");
		return;
	}
	if (!(bcs->hw.foreign.hdlc_state = kmalloc(sizeof(struct hdlc_state),
						   GFP_ATOMIC))) {
		printk(KERN_WARNING
		       "HiSax: No memory for foreign.hdlc_state\n");
		return;
	}

	bcs->hw.foreign.tq_rcv.sync = 0;
	bcs->hw.foreign.tq_rcv.routine = (void (*)(void *)) &Bchan_rcv_bh;
	bcs->hw.foreign.tq_rcv.data = (void *) bcs;

	bcs->hw.foreign.tq_xmt.sync = 0;
	bcs->hw.foreign.tq_xmt.routine = (void (*)(void *)) &Bchan_xmt_bh;
	bcs->hw.foreign.tq_xmt.data = (void *) bcs;
}

static void
Bchan_manl1(struct PStack *st, int pr,
	  void *arg)
{
	switch (pr) {
		case (PH_ACTIVATE_REQ):
			test_and_set_bit(BC_FLG_ACTIV, &st->l1.bcs->Flag);
			Bchan_mode(st->l1.bcs, st->l1.mode, st->l1.bc);
			st->l1.l1man(st, PH_ACTIVATE_CNF, NULL);
			break;
		case (PH_DEACTIVATE_REQ):
			if (!test_bit(BC_FLG_BUSY, &st->l1.bcs->Flag))
				Bchan_mode(st->l1.bcs, 0, 0);
			test_and_clear_bit(BC_FLG_ACTIV, &st->l1.bcs->Flag);
			break;
	}
}

int
setstack_foreign(struct PStack *st, struct BCState *bcs)
{
	if (Bchan_open(bcs))
		return (-1);
	st->l1.bcs = bcs;
	st->l2.l2l1 = Bchan_l2l1;
	st->ma.manl1 = Bchan_manl1;
	setstack_manager(st);
	bcs->st = st;
	return (0);
}


static void
foreign_drecv_callback(void *arg, int error, unsigned int count)
{
	struct IsdnCardState *cs = (struct IsdnCardState *) arg;
	static struct tq_struct task = {0, 0, (void *) &DChannel_proc_rcv, 0};
	struct sk_buff *skb;

        /* NOTE: This function is called directly from an interrupt handler */

	if (1) {
		if (!(skb = alloc_skb(count, GFP_ATOMIC)))
			printk(KERN_WARNING "HiSax: D receive out of memory\n");
		else {
			memcpy(skb_put(skb, count), cs->rcvbuf, count);
			skb_queue_tail(&cs->rq, skb);
		}

		task.data = (void *) cs;
		queue_task(&task, &tq_immediate);
		mark_bh(IMMEDIATE_BH);
	}

	if (cs->debug & L1_DEB_ISAC_FIFO) {
		char tmp[128];
		char *t = tmp;

		t += sprintf(t, "foreign Drecv cnt %d", count);
		if (error) t += sprintf(t, " ERR %x", error);
		QuickHex(t, cs->rcvbuf, count);
		debugl1(cs, tmp);
	}

	cs->hw.foreign->drecv(0, cs->rcvbuf, MAX_DFRAME_LEN,
		      &foreign_drecv_callback, cs);
}

static void
foreign_dxmit_callback(void *arg, int error)
{
	struct IsdnCardState *cs = (struct IsdnCardState *) arg;
	static struct tq_struct task = {0, 0, (void *) &DChannel_proc_xmt, 0};

        /* NOTE: This function is called directly from an interrupt handler */

	/* may wish to do retransmission here, if error indicates collision */

	if (cs->debug & L1_DEB_ISAC_FIFO) {
		char tmp[128];
		char *t = tmp;

		t += sprintf(t, "foreign Dxmit cnt %d", cs->tx_skb->len);
		if (error) t += sprintf(t, " ERR %x", error);
		QuickHex(t, cs->tx_skb->data, cs->tx_skb->len);
		debugl1(cs, tmp);
	}

	cs->tx_skb = NULL;

	task.data = (void *) cs;
	queue_task(&task, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
}

static void
foreign_Dchan_l2l1(struct PStack *st, int pr, void *arg)
{
	struct IsdnCardState *cs = (struct IsdnCardState *) st->l1.hardware;
	struct sk_buff *skb = arg;
	char str[64];

	switch (pr) {
		case (PH_DATA_REQ):
			if (cs->tx_skb) {
				skb_queue_tail(&cs->sq, skb);
#ifdef L2FRAME_DEBUG		/* psa */
				if (cs->debug & L1_DEB_LAPD)
					Logl2Frame(cs, skb, "PH_DATA Queued", 0);
#endif
			} else {
				if ((cs->dlogflag) && (!(skb->data[2] & 1))) {
					/* I-FRAME */
					LogFrame(cs, skb->data, skb->len);
					sprintf(str, "Q.931 frame user->network tei %d", st->l2.tei);
					dlogframe(cs, skb->data+4, skb->len-4,
						  str);
				}
				cs->tx_skb = skb;
				cs->tx_cnt = 0;
#ifdef L2FRAME_DEBUG		/* psa */
				if (cs->debug & L1_DEB_LAPD)
					Logl2Frame(cs, skb, "PH_DATA", 0);
#endif
				cs->hw.foreign->dxmit(0, skb->data, skb->len,
                                                      &foreign_dxmit_callback, cs);
			}
			break;
		case (PH_PULL_IND):
			if (cs->tx_skb) {
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, " l2l1 tx_skb exist this shouldn't happen");
				skb_queue_tail(&cs->sq, skb);
				break;
			}
			if ((cs->dlogflag) && (!(skb->data[2] & 1))) {	/* I-FRAME */
				LogFrame(cs, skb->data, skb->len);
				sprintf(str, "Q.931 frame user->network tei %d", st->l2.tei);
				dlogframe(cs, skb->data + 4, skb->len - 4,
					  str);
			}
			cs->tx_skb = skb;
			cs->tx_cnt = 0;
#ifdef L2FRAME_DEBUG		/* psa */
			if (cs->debug & L1_DEB_LAPD)
				Logl2Frame(cs, skb, "PH_DATA_PULLED", 0);
#endif
			cs->hw.foreign->dxmit(0, cs->tx_skb->data, cs->tx_skb->len,
                                              &foreign_dxmit_callback, cs);
			break;
		case (PH_PULL_REQ):
#ifdef L2FRAME_DEBUG		/* psa */
			if (cs->debug & L1_DEB_LAPD)
				debugl1(cs, "-> PH_REQUEST_PULL");
#endif
			if (!cs->tx_skb) {
				test_and_clear_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
				st->l1.l1l2(st, PH_PULL_CNF, NULL);
			} else
				test_and_set_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
			break;
	}
}

int
setDstack_foreign(struct PStack *st, struct IsdnCardState *cs)
{
	st->l2.l2l1 = foreign_Dchan_l2l1;
	if (! cs->rcvbuf) {
		printk("setDstack_foreign: No cs->rcvbuf!\n");
	} else {
		cs->hw.foreign->drecv(0, cs->rcvbuf, MAX_DFRAME_LEN,
                                      &foreign_drecv_callback, cs);
	}
	return (0);
}

static void
manl1_msg(struct IsdnCardState *cs, int msg, void *arg) {
	struct PStack *st;

	st = cs->stlist;
	while (st) {
		st->ma.manl1(st, msg, arg);
		st = st->next;
	}
}

void
foreign_l1cmd(struct IsdnCardState *cs, int msg, void *arg)
{
	u_char val;
	char tmp[32];
	
	if (cs->debug & L1_DEB_ISAC) {
		sprintf(tmp, "foreign_l1cmd msg %x", msg);
		debugl1(cs, tmp);
	}

	switch(msg) {
		case PH_RESET_REQ:
			cs->hw.foreign->liu_deactivate(0);
			manl1_msg(cs, PH_POWERUP_CNF, NULL);
			break;
		case PH_ENABLE_REQ:
			break;
		case PH_INFO3_REQ:
			cs->hw.foreign->liu_activate(0,0);
			break;
		case PH_TESTLOOP_REQ:
			break;
		default:
			if (cs->debug & L1_DEB_WARN) {
				sprintf(tmp, "foreign_l1cmd unknown %4x", msg);
				debugl1(cs, tmp);
			}
			break;
	}
}

static void
foreign_new_ph(struct IsdnCardState *cs)
{
	switch (cs->hw.foreign->get_liu_state(0)) {
	        case 3:
			manl1_msg(cs, PH_POWERUP_CNF, NULL);
                        break;

	        case 7:
			manl1_msg(cs, PH_I4_P8_IND, NULL);
			break;

	        case 8:
			manl1_msg(cs, PH_RSYNC_IND, NULL);
			break;
	}
}

/* LIU state change callback */

static void
foreign_liu_callback(struct IsdnCardState *cs)
{
	static struct tq_struct task = {0, 0, (void *) &foreign_new_ph, 0};

	if (!cs)
		return;

	if (cs->debug & L1_DEB_ISAC) {
		char tmp[32];
		sprintf(tmp, "foreign LIU state %d",
                        cs->hw.foreign->get_liu_state(0));
		debugl1(cs, tmp);
	}

	task.data = (void *) cs;
	queue_task(&task, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
}

static void init_foreign(struct IsdnCardState *cs)
{
	Bchan_init(&cs->bcs[0]);
	Bchan_init(&cs->bcs[1]);
	cs->bcs[0].BC_SetStack = setstack_foreign;
	cs->bcs[1].BC_SetStack = setstack_foreign;
	cs->bcs[0].BC_Close = Bchan_close;
	cs->bcs[1].BC_Close = Bchan_close;
	Bchan_mode(cs->bcs, 0, 0);
	Bchan_mode(cs->bcs + 1, 0, 0);
}

static int
foreign_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
	switch (mt) {
		case CARD_RESET:
			return(0);
		case CARD_RELEASE:
			return(0);
		case CARD_SETIRQ:
			return(0);
		case CARD_INIT:
			cs->l1cmd = foreign_l1cmd;
                        cs->setstack_d = setDstack_foreign;
			cs->hw.foreign->liu_init(0, &foreign_liu_callback,
                                                (void *)cs);
			init_foreign(cs);
			return(0);
		case CARD_TEST:
			return(0);
	}
	return(0);
}

#if CARD_AMD7930
extern struct foreign_interface amd7930_foreign_interface;
#endif

#if CARD_DBRI
extern struct foreign_interface dbri_foreign_interface;
#endif

int __init 
setup_foreign(struct IsdnCard *card)
{
	struct IsdnCardState *cs = card->cs;
	char tmp[64];

	strcpy(tmp, foreign_revision);
	printk(KERN_INFO "HiSax: Foreign chip driver Rev. %s\n",
               HiSax_getrev(tmp));

#if CARD_AMD7930
	if (cs->typ == ISDN_CTYPE_AMD7930) {
                cs->hw.foreign = &amd7930_foreign_interface;
                cs->irq = cs->hw.foreign->get_irqnum(0);
                if (cs->irq == 0)
                        return (0);
                cs->cardmsg = &foreign_card_msg;
                return (1);
        }
#endif

#if CARD_DBRI
	if (cs->typ == ISDN_CTYPE_DBRI) {
                cs->hw.foreign = &dbri_foreign_interface;
                cs->irq = cs->hw.foreign->get_irqnum(0);
                if (cs->irq == 0)
                        return (0);
                cs->cardmsg = &foreign_card_msg;
                return (1);
        }
#endif

        return(0);
}
