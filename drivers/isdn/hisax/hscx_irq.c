/* $Id: hscx_irq.c,v 1.7 1998/02/12 23:07:37 keil Exp $

 * hscx_irq.c     low level b-channel stuff for Siemens HSCX
 *
 * Author     Karsten Keil (keil@temic-ech.spacenet.de)
 *
 * This is an include file for fast inline IRQ stuff
 *
 * $Log: hscx_irq.c,v $
 * Revision 1.7  1998/02/12 23:07:37  keil
 * change for 2.1.86 (removing FREE_READ/FREE_WRITE from [dev]_kfree_skb()
 *
 * Revision 1.6  1997/10/29 19:01:07  keil
 * changes for 2.1
 *
 * Revision 1.5  1997/10/01 09:21:35  fritz
 * Removed old compatibility stuff for 2.0.X kernels.
 * From now on, this code is for 2.1.X ONLY!
 * Old stuff is still in the separate branch.
 *
 * Revision 1.4  1997/08/15 17:48:02  keil
 * cosmetic
 *
 * Revision 1.3  1997/07/27 21:38:36  keil
 * new B-channel interface
 *
 * Revision 1.2  1997/06/26 11:16:19  keil
 * first version
 *
 *
 */


static inline void
waitforCEC(struct IsdnCardState *cs, int hscx)
{
	int to = 50;

	while ((READHSCX(cs, hscx, HSCX_STAR) & 0x04) && to) {
		udelay(1);
		to--;
	}
	if (!to)
		printk(KERN_WARNING "HiSax: waitforCEC timeout\n");
}


static inline void
waitforXFW(struct IsdnCardState *cs, int hscx)
{
	int to = 50;

	while ((!(READHSCX(cs, hscx, HSCX_STAR) & 0x44) == 0x40) && to) {
		udelay(1);
		to--;
	}
	if (!to)
		printk(KERN_WARNING "HiSax: waitforXFW timeout\n");
}

static inline void
WriteHSCXCMDR(struct IsdnCardState *cs, int hscx, u_char data)
{
	long flags;

	save_flags(flags);
	cli();
	waitforCEC(cs, hscx);
	WRITEHSCX(cs, hscx, HSCX_CMDR, data);
	restore_flags(flags);
}



static void
hscx_empty_fifo(struct BCState *bcs, int count)
{
	u_char *ptr;
	struct IsdnCardState *cs = bcs->cs;
	long flags;

	if ((cs->debug & L1_DEB_HSCX) && !(cs->debug & L1_DEB_HSCX_FIFO))
		debugl1(cs, "hscx_empty_fifo");

	if (bcs->hw.hscx.rcvidx + count > HSCX_BUFMAX) {
		if (cs->debug & L1_DEB_WARN)
			debugl1(cs, "hscx_empty_fifo: incoming packet too large");
		WriteHSCXCMDR(cs, bcs->channel, 0x80);
		bcs->hw.hscx.rcvidx = 0;
		return;
	}
	ptr = bcs->hw.hscx.rcvbuf + bcs->hw.hscx.rcvidx;
	bcs->hw.hscx.rcvidx += count;
	save_flags(flags);
	cli();
	READHSCXFIFO(cs, bcs->channel, ptr, count);
	WriteHSCXCMDR(cs, bcs->channel, 0x80);
	restore_flags(flags);
	if (cs->debug & L1_DEB_HSCX_FIFO) {
		char tmp[256];
		char *t = tmp;

		t += sprintf(t, "hscx_empty_fifo %c cnt %d",
			     bcs->channel ? 'B' : 'A', count);
		QuickHex(t, ptr, count);
		debugl1(cs, tmp);
	}
}

static void
hscx_fill_fifo(struct BCState *bcs)
{
	struct IsdnCardState *cs = bcs->cs;
	int more, count;
	int fifo_size = test_bit(HW_IPAC, &cs->HW_Flags)? 64: 32;
	u_char *ptr;
	long flags;


	if ((cs->debug & L1_DEB_HSCX) && !(cs->debug & L1_DEB_HSCX_FIFO))
		debugl1(cs, "hscx_fill_fifo");

	if (!bcs->hw.hscx.tx_skb)
		return;
	if (bcs->hw.hscx.tx_skb->len <= 0)
		return;

	more = (bcs->mode == L1_MODE_TRANS) ? 1 : 0;
	if (bcs->hw.hscx.tx_skb->len > fifo_size) {
		more = !0;
		count = fifo_size;
	} else
		count = bcs->hw.hscx.tx_skb->len;

	waitforXFW(cs, bcs->channel);
	save_flags(flags);
	cli();
	ptr = bcs->hw.hscx.tx_skb->data;
	skb_pull(bcs->hw.hscx.tx_skb, count);
	bcs->tx_cnt -= count;
	bcs->hw.hscx.count += count;
	WRITEHSCXFIFO(cs, bcs->channel, ptr, count);
	WriteHSCXCMDR(cs, bcs->channel, more ? 0x8 : 0xa);
	restore_flags(flags);
	if (cs->debug & L1_DEB_HSCX_FIFO) {
		char tmp[256];
		char *t = tmp;

		t += sprintf(t, "hscx_fill_fifo %c cnt %d",
			     bcs->channel ? 'B' : 'A', count);
		QuickHex(t, ptr, count);
		debugl1(cs, tmp);
	}
}

static inline void
hscx_interrupt(struct IsdnCardState *cs, u_char val, u_char hscx)
{
	u_char r;
	struct BCState *bcs = cs->bcs + hscx;
	struct sk_buff *skb;
	int fifo_size = test_bit(HW_IPAC, &cs->HW_Flags)? 64: 32;
	int count;
	char tmp[32];

	if (!test_bit(BC_FLG_INIT, &bcs->Flag))
		return;

	if (val & 0x80) {	/* RME */
		r = READHSCX(cs, hscx, HSCX_RSTA);
		if ((r & 0xf0) != 0xa0) {
			if (!(r & 0x80))
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, "HSCX invalid frame");
			if ((r & 0x40) && bcs->mode)
				if (cs->debug & L1_DEB_WARN) {
					sprintf(tmp, "HSCX RDO mode=%d",
						bcs->mode);
					debugl1(cs, tmp);
				}
			if (!(r & 0x20))
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, "HSCX CRC error");
			WriteHSCXCMDR(cs, hscx, 0x80);
		} else {
			count = READHSCX(cs, hscx, HSCX_RBCL) & (
				test_bit(HW_IPAC, &cs->HW_Flags)? 0x3f: 0x1f);
			if (count == 0)
				count = fifo_size;
			hscx_empty_fifo(bcs, count);
			if ((count = bcs->hw.hscx.rcvidx - 1) > 0) {
				if (cs->debug & L1_DEB_HSCX_FIFO) {
					sprintf(tmp, "HX Frame %d", count);
					debugl1(cs, tmp);
				}
				if (!(skb = dev_alloc_skb(count)))
					printk(KERN_WARNING "HSCX: receive out of memory\n");
				else {
					memcpy(skb_put(skb, count), bcs->hw.hscx.rcvbuf, count);
					skb_queue_tail(&bcs->rqueue, skb);
				}
			}
		}
		bcs->hw.hscx.rcvidx = 0;
		hscx_sched_event(bcs, B_RCVBUFREADY);
	}
	if (val & 0x40) {	/* RPF */
		hscx_empty_fifo(bcs, fifo_size);
		if (bcs->mode == L1_MODE_TRANS) {
			/* receive audio data */
			if (!(skb = dev_alloc_skb(fifo_size)))
				printk(KERN_WARNING "HiSax: receive out of memory\n");
			else {
				memcpy(skb_put(skb, fifo_size), bcs->hw.hscx.rcvbuf, fifo_size);
				skb_queue_tail(&bcs->rqueue, skb);
			}
			bcs->hw.hscx.rcvidx = 0;
			hscx_sched_event(bcs, B_RCVBUFREADY);
		}
	}
	if (val & 0x10) {	/* XPR */
		if (bcs->hw.hscx.tx_skb) {
			if (bcs->hw.hscx.tx_skb->len) {
				hscx_fill_fifo(bcs);
				return;
			} else {
				if (bcs->st->lli.l1writewakeup &&
					(PACKET_NOACK != bcs->hw.hscx.tx_skb->pkt_type))
					bcs->st->lli.l1writewakeup(bcs->st, bcs->hw.hscx.count);
				dev_kfree_skb(bcs->hw.hscx.tx_skb);
				bcs->hw.hscx.count = 0; 
				bcs->hw.hscx.tx_skb = NULL;
			}
		}
		if ((bcs->hw.hscx.tx_skb = skb_dequeue(&bcs->squeue))) {
			bcs->hw.hscx.count = 0;
			test_and_set_bit(BC_FLG_BUSY, &bcs->Flag);
			hscx_fill_fifo(bcs);
		} else {
			test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
			hscx_sched_event(bcs, B_XMTBUFREADY);
		}
	}
}

static inline void
hscx_int_main(struct IsdnCardState *cs, u_char val)
{

	u_char exval;
	struct BCState *bcs;
	char tmp[32];

	if (val & 0x01) {
		bcs = cs->bcs + 1;
		exval = READHSCX(cs, 1, HSCX_EXIR);
		if (exval == 0x40) {
			if (bcs->mode == 1)
				hscx_fill_fifo(bcs);
			else {
				/* Here we lost an TX interrupt, so
				   * restart transmitting the whole frame.
				 */
				if (bcs->hw.hscx.tx_skb) {
					skb_push(bcs->hw.hscx.tx_skb, bcs->hw.hscx.count);
					bcs->tx_cnt += bcs->hw.hscx.count;
					bcs->hw.hscx.count = 0;
				}
				WriteHSCXCMDR(cs, bcs->channel, 0x01);
				if (cs->debug & L1_DEB_WARN) {
					sprintf(tmp, "HSCX B EXIR %x Lost TX", exval);
					debugl1(cs, tmp);
				}
			}
		} else if (cs->debug & L1_DEB_HSCX) {
			sprintf(tmp, "HSCX B EXIR %x", exval);
			debugl1(cs, tmp);
		}
	}
	if (val & 0xf8) {
		if (cs->debug & L1_DEB_HSCX) {
			sprintf(tmp, "HSCX B interrupt %x", val);
			debugl1(cs, tmp);
		}
		hscx_interrupt(cs, val, 1);
	}
	if (val & 0x02) {
		bcs = cs->bcs;
		exval = READHSCX(cs, 0, HSCX_EXIR);
		if (exval == 0x40) {
			if (bcs->mode == L1_MODE_TRANS)
				hscx_fill_fifo(bcs);
			else {
				/* Here we lost an TX interrupt, so
				   * restart transmitting the whole frame.
				 */
				if (bcs->hw.hscx.tx_skb) {
					skb_push(bcs->hw.hscx.tx_skb, bcs->hw.hscx.count);
					bcs->tx_cnt += bcs->hw.hscx.count;
					bcs->hw.hscx.count = 0;
				}
				WriteHSCXCMDR(cs, bcs->channel, 0x01);
				if (cs->debug & L1_DEB_WARN) {
					sprintf(tmp, "HSCX A EXIR %x Lost TX", exval);
					debugl1(cs, tmp);
				}
			}
		} else if (cs->debug & L1_DEB_HSCX) {
			sprintf(tmp, "HSCX A EXIR %x", exval);
			debugl1(cs, tmp);
		}
	}
	if (val & 0x04) {
		exval = READHSCX(cs, 0, HSCX_ISTA);
		if (cs->debug & L1_DEB_HSCX) {
			sprintf(tmp, "HSCX A interrupt %x", exval);
			debugl1(cs, tmp);
		}
		hscx_interrupt(cs, exval, 0);
	}
}
