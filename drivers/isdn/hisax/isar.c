/* $Id: isar.c,v 1.2 1998/11/15 23:54:53 keil Exp $

 * isar.c   ISAR (Siemens PSB 7110) specific routines
 *
 * Author       Karsten Keil (keil@isdn4linux.de)
 *
 *
 * $Log: isar.c,v $
 * Revision 1.2  1998/11/15 23:54:53  keil
 * changes from 2.0
 *
 * Revision 1.1  1998/08/13 23:33:47  keil
 * First version, only init
 *
 *
 */

#define __NO_VERSION__
#include "hisax.h"
#include "isar.h"
#include "isdnl1.h"
#include <linux/interrupt.h>

#define DBG_LOADFIRM	0
#define DUMP_MBOXFRAME	2

#define MIN(a,b) ((a<b)?a:b)

void isar_setup(struct IsdnCardState *cs);

static inline int
waitforHIA(struct IsdnCardState *cs, int timeout)
{

	while ((cs->BC_Read_Reg(cs, 0, ISAR_HIA) & 1) && timeout) {
		udelay(1);
		timeout--;
	}
	if (!timeout)
		printk(KERN_WARNING "HiSax: ISAR waitforHIA timeout\n");
	return(timeout);
}


int
sendmsg(struct IsdnCardState *cs, u_char his, u_char creg, u_char len,
	u_char *msg)
{
	long flags;
	int i;
	
	if (!waitforHIA(cs, 4000))
		return(0);
#if DUMP_MBOXFRAME
	if (cs->debug & L1_DEB_HSCX)
		debugl1(cs, "sendmsg(%02x,%02x,%d)", his, creg, len);
#endif
	save_flags(flags);
	cli();
	cs->BC_Write_Reg(cs, 0, ISAR_CTRL_H, creg);
	cs->BC_Write_Reg(cs, 0, ISAR_CTRL_L, len);
	cs->BC_Write_Reg(cs, 0, ISAR_WADR, 0);
	if (msg && len) {
		cs->BC_Write_Reg(cs, 1, ISAR_MBOX, msg[0]);
		for (i=1; i<len; i++)
			cs->BC_Write_Reg(cs, 2, ISAR_MBOX, msg[i]);
#if DUMP_MBOXFRAME>1
		if (cs->debug & L1_DEB_HSCX_FIFO) {
			char tmp[256], *t;
			
			i = len;
			while (i>0) {
				t = tmp;
				t += sprintf(t, "sendmbox cnt %d", len);
				QuickHex(t, &msg[len-i], (i>64) ? 64:i);
				debugl1(cs, tmp);
				i -= 64;
			}
		}
#endif
	}
	cs->BC_Write_Reg(cs, 1, ISAR_HIS, his);
	restore_flags(flags);
	waitforHIA(cs, 10000);
	return(1);
}

/* Call only with IRQ disabled !!! */
inline void
rcv_mbox(struct IsdnCardState *cs, struct isar_reg *ireg, u_char *msg)
{
	int i;

	cs->BC_Write_Reg(cs, 1, ISAR_RADR, 0);
	if (msg && ireg->clsb) {
		msg[0] = cs->BC_Read_Reg(cs, 1, ISAR_MBOX);
		for (i=1; i < ireg->clsb; i++)
			 msg[i] = cs->BC_Read_Reg(cs, 2, ISAR_MBOX);
#if DUMP_MBOXFRAME>1
		if (cs->debug & L1_DEB_HSCX_FIFO) {
			char tmp[256], *t;
			
			i = ireg->clsb;
			while (i>0) {
				t = tmp;
				t += sprintf(t, "rcv_mbox cnt %d", ireg->clsb);
				QuickHex(t, &msg[ireg->clsb-i], (i>64) ? 64:i);
				debugl1(cs, tmp);
				i -= 64;
			}
		}
#endif
	}
	cs->BC_Write_Reg(cs, 1, ISAR_IIA, 0);
}

/* Call only with IRQ disabled !!! */
inline void
get_irq_infos(struct IsdnCardState *cs, struct isar_reg *ireg)
{
	ireg->iis = cs->BC_Read_Reg(cs, 1, ISAR_IIS);
	ireg->cmsb = cs->BC_Read_Reg(cs, 1, ISAR_CTRL_H);
	ireg->clsb = cs->BC_Read_Reg(cs, 1, ISAR_CTRL_L);
#if DUMP_MBOXFRAME
	if (cs->debug & L1_DEB_HSCX)
		debugl1(cs, "rcv_mbox(%02x,%02x,%d)", ireg->iis, ireg->cmsb,
			ireg->clsb);
#endif
}

int
waitrecmsg(struct IsdnCardState *cs, u_char *len,
	u_char *msg, int maxdelay)
{
	int timeout = 0;
	long flags;
	struct isar_reg *ir = cs->bcs[0].hw.isar.reg;
	
	
	while((!(cs->BC_Read_Reg(cs, 0, ISAR_IRQBIT) & ISAR_IRQSTA)) &&
		(timeout++ < maxdelay))
		udelay(1);
	if (timeout >= maxdelay) {
		printk(KERN_WARNING"isar recmsg IRQSTA timeout\n");
		return(0);
	}
	save_flags(flags);
	cli();
	get_irq_infos(cs, ir);
	rcv_mbox(cs, ir, msg);
	*len = ir->clsb;
	restore_flags(flags);
	return(1);
}

int
ISARVersion(struct IsdnCardState *cs, char *s)
{
	int ver;
	u_char msg[] = ISAR_MSG_HWVER;
	u_char tmp[64];
	u_char len;
	int debug;

	cs->cardmsg(cs, CARD_RESET,  NULL);
	/* disable ISAR IRQ */
	cs->BC_Write_Reg(cs, 0, ISAR_IRQBIT, 0);
	debug = cs->debug;
	cs->debug &= ~(L1_DEB_HSCX | L1_DEB_HSCX_FIFO);
	if (!sendmsg(cs, ISAR_HIS_VNR, 0, 3, msg))
		return(-1);
	if (!waitrecmsg(cs, &len, tmp, 100000))
		 return(-2);
	cs->debug = debug;
	if (cs->bcs[0].hw.isar.reg->iis == ISAR_IIS_VNR) {
		if (len == 1) {
			ver = tmp[0] & 0xf;
			printk(KERN_INFO "%s ISAR version %d\n", s, ver);
			return(ver);
		}
		return(-3);
	}
	return(-4);
}

int
isar_load_firmware(struct IsdnCardState *cs, u_char *buf)
{
	int ret, size, cnt, debug;
	u_char len, nom, noc;
	u_short sadr, left, *sp;
	u_char *p = buf;
	u_char *msg, *tmpmsg, *mp, tmp[64];
	long flags;
	struct isar_reg *ireg = cs->bcs[0].hw.isar.reg;
	
	struct {u_short sadr;
		u_short len;
		u_short d_key;
	} blk_head;
		
#define	BLK_HEAD_SIZE 6
	if (1 != (ret = ISARVersion(cs, "Testing"))) {
		printk(KERN_ERR"isar_load_firmware wrong isar version %d\n", ret);
		return(1);
	}
	debug = cs->debug;
#if DBG_LOADFIRM<2
	cs->debug &= ~(L1_DEB_HSCX | L1_DEB_HSCX_FIFO);
#endif
	printk(KERN_DEBUG"isar_load_firmware buf %#lx\n", (u_long)buf);
	if ((ret = verify_area(VERIFY_READ, (void *) p, sizeof(int)))) {
		printk(KERN_ERR"isar_load_firmware verify_area ret %d\n", ret);
		return ret;
	}
	if ((ret = copy_from_user(&size, p, sizeof(int)))) {
		printk(KERN_ERR"isar_load_firmware copy_from_user ret %d\n", ret);
		return ret;
	}
	p += sizeof(int);
	printk(KERN_DEBUG"isar_load_firmware size: %d\n", size);
	if ((ret = verify_area(VERIFY_READ, (void *) p, size))) {
		printk(KERN_ERR"isar_load_firmware verify_area ret %d\n", ret);
		return ret;
	}
	cnt = 0;
	/* disable ISAR IRQ */
	cs->BC_Write_Reg(cs, 0, ISAR_IRQBIT, 0);
	if (!(msg = kmalloc(256, GFP_KERNEL))) {
		printk(KERN_ERR"isar_load_firmware no buffer\n");
		return (1);
	}
	if (!(tmpmsg = kmalloc(256, GFP_KERNEL))) {
		printk(KERN_ERR"isar_load_firmware no tmp buffer\n");
		kfree(msg);
		return (1);
	}
	while (cnt < size) {
		if ((ret = copy_from_user(&blk_head, p, BLK_HEAD_SIZE))) {
			printk(KERN_ERR"isar_load_firmware copy_from_user ret %d\n", ret);
			goto reterror;
		}
		cnt += BLK_HEAD_SIZE;
		p += BLK_HEAD_SIZE;
		printk(KERN_DEBUG"isar firmware block (%#x,%5d,%#x)\n",
			blk_head.sadr, blk_head.len, blk_head.d_key & 0xff);
		sadr = blk_head.sadr;
		left = blk_head.len;
		if (!sendmsg(cs, ISAR_HIS_DKEY, blk_head.d_key & 0xff, 0, NULL)) {
			printk(KERN_ERR"isar sendmsg dkey failed\n");
			ret = 1;goto reterror;
		}
		if (!waitrecmsg(cs, &len, tmp, 100000)) {
			printk(KERN_ERR"isar waitrecmsg dkey failed\n");
			ret = 1;goto reterror;
		}
		if ((ireg->iis != ISAR_IIS_DKEY) || ireg->cmsb || len) {
			printk(KERN_ERR"isar wrong dkey response (%x,%x,%x)\n",
				ireg->iis, ireg->cmsb, len);
			ret = 1;goto reterror;
		}
		while (left>0) {
			noc = MIN(126, left);
			nom = 2*noc;
			mp  = msg;
			*mp++ = sadr / 256;
			*mp++ = sadr % 256;
			left -= noc;
			*mp++ = noc;
			if ((ret = copy_from_user(tmpmsg, p, nom))) {
				printk(KERN_ERR"isar_load_firmware copy_from_user ret %d\n", ret);
				goto reterror;
			}
			p += nom;
			cnt += nom;
			nom += 3;
			sp = (u_short *)tmpmsg;
#if DBG_LOADFIRM
			printk(KERN_DEBUG"isar: load %3d words at %04x\n",
				 noc, sadr);
#endif
			sadr += noc;
			while(noc) {
				*mp++ = *sp / 256;
				*mp++ = *sp % 256;
				sp++;
				noc--;
			}
			if (!sendmsg(cs, ISAR_HIS_FIRM, 0, nom, msg)) {
				printk(KERN_ERR"isar sendmsg prog failed\n");
				ret = 1;goto reterror;
			}
			if (!waitrecmsg(cs, &len, tmp, 100000)) {
				printk(KERN_ERR"isar waitrecmsg prog failed\n");
				ret = 1;goto reterror;
			}
			if ((ireg->iis != ISAR_IIS_FIRM) || ireg->cmsb || len) {
				printk(KERN_ERR"isar wrong prog response (%x,%x,%x)\n",
					ireg->iis, ireg->cmsb, len);
				ret = 1;goto reterror;
			}
		}
		printk(KERN_DEBUG"isar firmware block %5d words loaded\n",
			blk_head.len);
	}
	msg[0] = 0xff;
	msg[1] = 0xfe;
	ireg->bstat = 0;
	if (!sendmsg(cs, ISAR_HIS_STDSP, 0, 2, msg)) {
		printk(KERN_ERR"isar sendmsg start dsp failed\n");
		ret = 1;goto reterror;
	}
	if (!waitrecmsg(cs, &len, tmp, 100000)) {
		printk(KERN_ERR"isar waitrecmsg start dsp failed\n");
		ret = 1;goto reterror;
	}
	if ((ireg->iis != ISAR_IIS_STDSP) || ireg->cmsb || len) {
		printk(KERN_ERR"isar wrong start dsp response (%x,%x,%x)\n",
			ireg->iis, ireg->cmsb, len);
		ret = 1;goto reterror;
	} else
		printk(KERN_DEBUG"isar start dsp success\n");
	/* NORMAL mode entered */
	/* Enable IRQs of ISAR */
	cs->BC_Write_Reg(cs, 0, ISAR_IRQBIT, ISAR_IRQSTA);
	save_flags(flags);
	sti();
	cnt = 1000; /* max 1s */
	while ((!ireg->bstat) && cnt) {
		udelay(1000);
		cnt--;
	}
	if (!cnt) {
		printk(KERN_ERR"isar no general status event received\n");
		ret = 1;goto reterrflg;
	} else {
		printk(KERN_DEBUG"isar general status event %x\n",
			ireg->bstat);
	}
	ireg->iis = 0;
	if (!sendmsg(cs, ISAR_HIS_DIAG, ISAR_CTRL_STST, 0, NULL)) {
		printk(KERN_ERR"isar sendmsg self tst failed\n");
		ret = 1;goto reterrflg;
	}
	cnt = 1000; /* max 10 ms */
	while ((ireg->iis != ISAR_IIS_DIAG) && cnt) {
		udelay(10);
		cnt--;
	}
	if (!cnt) {
		printk(KERN_ERR"isar no self tst response\n");
		ret = 1;goto reterrflg;
	} else if ((ireg->cmsb == ISAR_CTRL_STST) && (ireg->clsb == 1)
		&& (ireg->par[0] == 0)) {
		printk(KERN_DEBUG"isar selftest OK\n");
	} else {
		printk(KERN_DEBUG"isar selftest not OK %x/%x/%x\n",
			ireg->cmsb, ireg->clsb, ireg->par[0]);
		ret = 1;goto reterror;
	}
	ireg->iis = 0;
	if (!sendmsg(cs, ISAR_HIS_DIAG, ISAR_CTRL_SWVER, 0, NULL)) {
		printk(KERN_ERR"isar RQST SVN failed\n");
		ret = 1;goto reterror;
	}
	cnt = 10000; /* max 100 ms */
	while ((ireg->iis != ISAR_IIS_DIAG) && cnt) {
		udelay(10);
		cnt--;
	}
	if (!cnt) {
		printk(KERN_ERR"isar no SVN response\n");
		ret = 1;goto reterrflg;
	} else {
		if ((ireg->cmsb == ISAR_CTRL_SWVER) && (ireg->clsb == 1))
			printk(KERN_DEBUG"isar software version %#x\n",
				ireg->par[0]);
		else {
			printk(KERN_ERR"isar wrong swver response (%x,%x) cnt(%d)\n",
				ireg->cmsb, ireg->clsb, cnt);
			ret = 1;goto reterrflg;
		}
	}
	cs->debug = debug;
	isar_setup(cs);
	ret = 0;
reterrflg:
	restore_flags(flags);
reterror:
	cs->debug = debug;
	if (ret)
		/* disable ISAR IRQ */
		cs->BC_Write_Reg(cs, 0, ISAR_IRQBIT, 0);
	kfree(msg);
	kfree(tmpmsg);
	return(ret);
}

void
isar_sched_event(struct BCState *bcs, int event)
{
	bcs->event |= 1 << event;
	queue_task(&bcs->tqueue, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
}

static inline void
isar_rcv_frame(struct IsdnCardState *cs, struct BCState *bcs)
{
	u_char *ptr;
	struct sk_buff *skb;
	struct isar_reg *ireg = bcs->hw.isar.reg;
	
	if (!ireg->clsb) {
		debugl1(cs, "isar zero len frame");
		cs->BC_Write_Reg(cs, 1, ISAR_IIA, 0);
		return;
	}
	switch (bcs->mode) {
	case L1_MODE_NULL:
		debugl1(cs, "isar mode 0 spurious IIS_RDATA %x/%x/%x",
			ireg->iis, ireg->cmsb, ireg->clsb);
		printk(KERN_WARNING"isar mode 0 spurious IIS_RDATA %x/%x/%x\n",
			ireg->iis, ireg->cmsb, ireg->clsb);
		cs->BC_Write_Reg(cs, 1, ISAR_IIA, 0);
		break;
	case L1_MODE_TRANS:
		if ((skb = dev_alloc_skb(ireg->clsb))) {
			rcv_mbox(cs, ireg, (u_char *)skb_put(skb, ireg->clsb));
			skb_queue_tail(&bcs->rqueue, skb);
			isar_sched_event(bcs, B_RCVBUFREADY);
		} else {
			printk(KERN_WARNING "HiSax: skb out of memory\n");
			cs->BC_Write_Reg(cs, 1, ISAR_IIA, 0);
		}
		break;
	case L1_MODE_HDLC:
		if ((bcs->hw.isar.rcvidx + ireg->clsb) > HSCX_BUFMAX) {
			if (cs->debug & L1_DEB_WARN)
				debugl1(cs, "isar_rcv_frame: incoming packet too large");
			cs->BC_Write_Reg(cs, 1, ISAR_IIA, 0);
			bcs->hw.isar.rcvidx = 0;
		} else if (ireg->cmsb & HDLC_ERROR) {
			if (cs->debug & L1_DEB_WARN)
				debugl1(cs, "isar frame error %x len %d",
					ireg->cmsb, ireg->clsb);
			bcs->hw.isar.rcvidx = 0;
			cs->BC_Write_Reg(cs, 1, ISAR_IIA, 0);
		} else {
			if (ireg->cmsb & HDLC_FSD)
				bcs->hw.isar.rcvidx = 0;
			ptr = bcs->hw.isar.rcvbuf + bcs->hw.isar.rcvidx;
			bcs->hw.isar.rcvidx += ireg->clsb;
			rcv_mbox(cs, ireg, ptr);
			if (ireg->cmsb & HDLC_FED) {
				if (bcs->hw.isar.rcvidx < 3) { /* last 2 bytes are the FCS */
					printk(KERN_WARNING "ISAR: HDLC frame too short(%d)\n",
						bcs->hw.isar.rcvidx);
				} else if (!(skb = dev_alloc_skb(bcs->hw.isar.rcvidx-2)))
					printk(KERN_WARNING "ISAR: receive out of memory\n");
				else {
					memcpy(skb_put(skb, bcs->hw.isar.rcvidx-2),
						bcs->hw.isar.rcvbuf, bcs->hw.isar.rcvidx-2);
					skb_queue_tail(&bcs->rqueue, skb);
					isar_sched_event(bcs, B_RCVBUFREADY);
				}
			}
		}
		break;
	default:
		printk(KERN_ERR"isar_rcv_frame mode (%x)error\n", bcs->mode);
		cs->BC_Write_Reg(cs, 1, ISAR_IIA, 0);
		break;
	}
}

void
isar_fill_fifo(struct BCState *bcs)
{
	struct IsdnCardState *cs = bcs->cs;
	int count;
	u_char msb;
	u_char *ptr;
	long flags;

	if ((cs->debug & L1_DEB_HSCX) && !(cs->debug & L1_DEB_HSCX_FIFO))
		debugl1(cs, "isar_fill_fifo");
	if (!bcs->tx_skb)
		return;
	if (bcs->tx_skb->len <= 0)
		return;
	if (!(bcs->hw.isar.reg->bstat & 
		(bcs->hw.isar.dpath == 1 ? BSTAT_RDM1 : BSTAT_RDM2)))
		return;
	if (bcs->tx_skb->len > bcs->hw.isar.mml) {
		msb = 0;
		count = bcs->hw.isar.mml;
	} else {
		count = bcs->tx_skb->len;
		msb = HDLC_FED;
	}
	if (!bcs->hw.isar.txcnt)
		msb |= HDLC_FST;
	save_flags(flags);
	cli();
	ptr = bcs->tx_skb->data;
	skb_pull(bcs->tx_skb, count);
	bcs->tx_cnt -= count;
	bcs->hw.isar.txcnt += count;
	switch (bcs->mode) {
	case L1_MODE_NULL:
		printk(KERN_ERR"isar_fill_fifo wrong mode 0\n");
		break;
	case L1_MODE_TRANS:
		if (!sendmsg(cs, SET_DPS(bcs->hw.isar.dpath) | ISAR_HIS_SDATA,
			0, count, ptr)) {
			if (cs->debug)
				debugl1(cs, "isar bin data send dp%d failed",
					bcs->hw.isar.dpath);
		}
		break;
	case L1_MODE_HDLC:
		if (!sendmsg(cs, SET_DPS(bcs->hw.isar.dpath) | ISAR_HIS_SDATA,
			msb, count, ptr)) {
			if (cs->debug)
				debugl1(cs, "isar hdlc data send dp%d failed",
					bcs->hw.isar.dpath);
		}
		break;
	default:
		printk(KERN_ERR"isar_fill_fifo mode (%x)error\n", bcs->mode);
		break;
	}
	restore_flags(flags);
}

inline
struct BCState *sel_bcs_isar(struct IsdnCardState *cs, u_char dpath)
{
	if ((!dpath) || (dpath == 3))
		return(NULL);
	if (cs->bcs[0].hw.isar.dpath == dpath)
		return(&cs->bcs[0]);
	if (cs->bcs[1].hw.isar.dpath == dpath)
		return(&cs->bcs[1]);
	return(NULL);
}

inline void
send_frames(struct BCState *bcs)
{
	if (bcs->tx_skb) {
		if (bcs->tx_skb->len) {
			isar_fill_fifo(bcs);
			return;
		} else {
			if (bcs->st->lli.l1writewakeup &&
				(PACKET_NOACK != bcs->tx_skb->pkt_type))
					bcs->st->lli.l1writewakeup(bcs->st, bcs->hw.isar.txcnt);
			dev_kfree_skb(bcs->tx_skb);
			bcs->hw.isar.txcnt = 0; 
			bcs->tx_skb = NULL;
		}
	}
	if ((bcs->tx_skb = skb_dequeue(&bcs->squeue))) {
		bcs->hw.isar.txcnt = 0;
		test_and_set_bit(BC_FLG_BUSY, &bcs->Flag);
		isar_fill_fifo(bcs);
	} else {
		test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
		isar_sched_event(bcs, B_XMTBUFREADY);
	}
}

inline void
check_send(struct IsdnCardState *cs, u_char rdm)
{
	struct BCState *bcs;
	
	if (rdm & BSTAT_RDM1) {
		if ((bcs = sel_bcs_isar(cs, 1))) {
			if (bcs->mode) {
				send_frames(bcs);
			}
		}
	}
	if (rdm & BSTAT_RDM2) {
		if ((bcs = sel_bcs_isar(cs, 2))) {
			if (bcs->mode) {
				send_frames(bcs);
			}
		}
	}
	
}

static char debbuf[64];

void
isar_int_main(struct IsdnCardState *cs)
{
	long flags;
	struct isar_reg *ireg = cs->bcs[0].hw.isar.reg;
	struct BCState *bcs;

	save_flags(flags);
	cli();
	get_irq_infos(cs, ireg);
	switch (ireg->iis & ISAR_IIS_MSCMSD) {
		case ISAR_IIS_RDATA:
			if ((bcs = sel_bcs_isar(cs, ireg->iis >> 6))) {
				isar_rcv_frame(cs, bcs);
			} else {
				debugl1(cs, "isar spurious IIS_RDATA %x/%x/%x",
					ireg->iis, ireg->cmsb, ireg->clsb);
				printk(KERN_WARNING"isar spurious IIS_RDATA %x/%x/%x\n",
					ireg->iis, ireg->cmsb, ireg->clsb);
				cs->BC_Write_Reg(cs, 1, ISAR_IIA, 0);
			}
			break;
		case ISAR_IIS_GSTEV:
			cs->BC_Write_Reg(cs, 1, ISAR_IIA, 0);
			ireg->bstat |= ireg->cmsb;
			check_send(cs, ireg->cmsb);
			break;
		case ISAR_IIS_BSTEV:
			cs->BC_Write_Reg(cs, 1, ISAR_IIA, 0);
			if (cs->debug & L1_DEB_WARN)
				debugl1(cs, "Buffer STEV dpath%d msb(%x)",
					ireg->iis>>6, ireg->cmsb);
			break;
		case ISAR_IIS_DIAG:
		case ISAR_IIS_PSTRSP:
		case ISAR_IIS_PSTEV:
		case ISAR_IIS_BSTRSP:
		case ISAR_IIS_IOM2RSP:
			rcv_mbox(cs, ireg, (u_char *)ireg->par);
			if ((cs->debug & (L1_DEB_HSCX | L1_DEB_HSCX_FIFO))
				== L1_DEB_HSCX) {
				u_char *tp=debbuf;

				tp += sprintf(debbuf, "msg iis(%x) msb(%x)",
					ireg->iis, ireg->cmsb);
				QuickHex(tp, (u_char *)ireg->par, ireg->clsb);
				debugl1(cs, debbuf);
			}
			break;
		default:
			rcv_mbox(cs, ireg, debbuf);
			if (cs->debug & L1_DEB_WARN)
				debugl1(cs, "unhandled msg iis(%x) ctrl(%x/%x)",
					ireg->iis, ireg->cmsb, ireg->clsb);
			break;
	}
	restore_flags(flags);
}

void
setup_pump(struct BCState *bcs) {
	struct IsdnCardState *cs = bcs->cs;
	u_char dps = SET_DPS(bcs->hw.isar.dpath);
	
	switch (bcs->mode) {
		case L1_MODE_NULL:
		case L1_MODE_TRANS:
		case L1_MODE_HDLC:
			if (!sendmsg(cs, dps | ISAR_HIS_PUMPCFG, PMOD_BYPASS, 0, NULL)) {
				if (cs->debug)
					debugl1(cs, "isar pump bypass cfg dp%d failed",
						bcs->hw.isar.dpath);
			}
			break;
	}
	if (!sendmsg(cs, dps | ISAR_HIS_PSTREQ, 0, 0, NULL)) {
		if (cs->debug)
			debugl1(cs, "isar pump status req dp%d failed",
				bcs->hw.isar.dpath);
	}
}

void
setup_sart(struct BCState *bcs) {
	struct IsdnCardState *cs = bcs->cs;
	u_char dps = SET_DPS(bcs->hw.isar.dpath);
	
	switch (bcs->mode) {
		case L1_MODE_NULL:
			if (!sendmsg(cs, dps | ISAR_HIS_SARTCFG, SMODE_DISABLE, 0, NULL)) {
				if (cs->debug)
					debugl1(cs, "isar sart disable dp%d failed",
						bcs->hw.isar.dpath);
			}
			break;
		case L1_MODE_TRANS:
			if (!sendmsg(cs, dps | ISAR_HIS_SARTCFG, SMODE_BINARY, 2, "\0\0")) {
				if (cs->debug)
					debugl1(cs, "isar sart binary dp%d failed",
						bcs->hw.isar.dpath);
			}
			break;
		case L1_MODE_HDLC:
			if (!sendmsg(cs, dps | ISAR_HIS_SARTCFG, SMODE_HDLC, 1, "\0")) {
				if (cs->debug)
					debugl1(cs, "isar sart binary dp%d failed",
						bcs->hw.isar.dpath);
			}
			break;
	}
	if (!sendmsg(cs, dps | ISAR_HIS_BSTREQ, 0, 0, NULL)) {
		if (cs->debug)
			debugl1(cs, "isar buf stat req dp%d failed",
				bcs->hw.isar.dpath);
	}
}

void
setup_iom2(struct BCState *bcs) {
	struct IsdnCardState *cs = bcs->cs;
	u_char dps = SET_DPS(bcs->hw.isar.dpath);
	u_char cmsb = 0, msg[5] = {0x10,0,0,0,0};
	
	switch (bcs->mode) {
		case L1_MODE_NULL:
			/* dummy slot */
			msg[1] = msg[3] = bcs->hw.isar.dpath + 2;
			break;
		case L1_MODE_TRANS:
		case L1_MODE_HDLC:
			cmsb = 0x80;
			if (bcs->channel)
				msg[1] = msg[3] = 1;
			break;
	}
	if (!sendmsg(cs, dps | ISAR_HIS_IOM2CFG, cmsb, 5, msg)) {
		if (cs->debug)
			debugl1(cs, "isar iom2 dp%d failed", bcs->hw.isar.dpath);
	}
	if (!sendmsg(cs, dps | ISAR_HIS_IOM2REQ, 0, 0, NULL)) {
		if (cs->debug)
			debugl1(cs, "isar IOM2 cfg req dp%d failed",
				bcs->hw.isar.dpath);
	}
}

int
modeisar(struct BCState *bcs, int mode, int bc)
{
	struct IsdnCardState *cs = bcs->cs;

	/* Here we are selecting the best datapath for requested mode */
	if(bcs->mode == L1_MODE_NULL) { /* New Setup */
		bcs->channel = bc;
		switch (mode) {
			case L1_MODE_NULL: /* init */
				break;
			case L1_MODE_TRANS:
			case L1_MODE_HDLC:
				/* best is datapath 2 */
				if (!test_and_set_bit(ISAR_DP2_USE, 
					&bcs->hw.isar.reg->Flags))
					bcs->hw.isar.dpath = 2;
				else if (!test_and_set_bit(ISAR_DP1_USE,
					&bcs->hw.isar.reg->Flags))
					bcs->hw.isar.dpath = 1;
				else {
					printk(KERN_ERR"isar modeisar both pathes in use\n");
					return(1);
				}
				break;
		}
	}
	if (cs->debug & L1_DEB_HSCX)
		debugl1(cs, "isar dp%d mode %d->%d ichan %d",
			bcs->hw.isar.dpath, bcs->mode, mode, bc);
	bcs->mode = mode;
	setup_pump(bcs);
	setup_sart(bcs);
	setup_iom2(bcs);
	if (bcs->mode == L1_MODE_NULL) {
		/* Clear resources */
		if (bcs->hw.isar.dpath == 1)
			test_and_clear_bit(ISAR_DP1_USE, &bcs->hw.isar.reg->Flags);
		else if (bcs->hw.isar.dpath == 2)
			test_and_clear_bit(ISAR_DP2_USE, &bcs->hw.isar.reg->Flags);
		bcs->hw.isar.dpath = 0;
	}
	return(0);
}

void
isar_setup(struct IsdnCardState *cs)
{
	u_char msg;
	int i;
	
	/* Dpath 1, 2 */
	msg = 61;
	for (i=0; i<2; i++) {
		/* Buffer Config */
		if (!sendmsg(cs, (i ? ISAR_HIS_DPS2 : ISAR_HIS_DPS1) |
			ISAR_HIS_P12CFG, 4, 1, &msg)) {
			if (cs->debug)
				debugl1(cs, "isar P%dCFG failed", i+1);
		}
		cs->bcs[i].hw.isar.mml = msg;
		cs->bcs[i].mode = 0;
		cs->bcs[i].hw.isar.dpath = i + 1;
		modeisar(&cs->bcs[i], 0, 0);
	}
}

void
isar_l2l1(struct PStack *st, int pr, void *arg)
{
	struct sk_buff *skb = arg;
	long flags;

	switch (pr) {
		case (PH_DATA | REQUEST):
			save_flags(flags);
			cli();
			if (st->l1.bcs->tx_skb) {
				skb_queue_tail(&st->l1.bcs->squeue, skb);
				restore_flags(flags);
			} else {
				st->l1.bcs->tx_skb = skb;
				test_and_set_bit(BC_FLG_BUSY, &st->l1.bcs->Flag);
				if (st->l1.bcs->cs->debug & L1_DEB_HSCX)
					debugl1(st->l1.bcs->cs, "DRQ set BC_FLG_BUSY");
				st->l1.bcs->hw.isar.txcnt = 0;
				restore_flags(flags);
				st->l1.bcs->cs->BC_Send_Data(st->l1.bcs);
			}
			break;
		case (PH_PULL | INDICATION):
			if (st->l1.bcs->tx_skb) {
				printk(KERN_WARNING "isar_l2l1: this shouldn't happen\n");
				break;
			}
			test_and_set_bit(BC_FLG_BUSY, &st->l1.bcs->Flag);
			if (st->l1.bcs->cs->debug & L1_DEB_HSCX)
				debugl1(st->l1.bcs->cs, "PUI set BC_FLG_BUSY");
			st->l1.bcs->tx_skb = skb;
			st->l1.bcs->hw.isar.txcnt = 0;
			st->l1.bcs->cs->BC_Send_Data(st->l1.bcs);
			break;
		case (PH_PULL | REQUEST):
			if (!st->l1.bcs->tx_skb) {
				test_and_clear_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
				st->l1.l1l2(st, PH_PULL | CONFIRM, NULL);
			} else
				test_and_set_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
			break;
		case (PH_ACTIVATE | REQUEST):
			test_and_set_bit(BC_FLG_ACTIV, &st->l1.bcs->Flag);
			modeisar(st->l1.bcs, st->l1.mode, st->l1.bc);
			l1_msg_b(st, pr, arg);
			break;
		case (PH_DEACTIVATE | REQUEST):
			l1_msg_b(st, pr, arg);
			break;
		case (PH_DEACTIVATE | CONFIRM):
			test_and_clear_bit(BC_FLG_ACTIV, &st->l1.bcs->Flag);
			test_and_clear_bit(BC_FLG_BUSY, &st->l1.bcs->Flag);
			if (st->l1.bcs->cs->debug & L1_DEB_HSCX)
				debugl1(st->l1.bcs->cs, "PDAC clear BC_FLG_BUSY");
			modeisar(st->l1.bcs, 0, st->l1.bc);
			st->l1.l1l2(st, PH_DEACTIVATE | CONFIRM, NULL);
			break;
	}
}

void
close_isarstate(struct BCState *bcs)
{
	modeisar(bcs, 0, bcs->channel);
	if (test_and_clear_bit(BC_FLG_INIT, &bcs->Flag)) {
		if (bcs->hw.isar.rcvbuf) {
			kfree(bcs->hw.isar.rcvbuf);
			bcs->hw.isar.rcvbuf = NULL;
		}
		discard_queue(&bcs->rqueue);
		discard_queue(&bcs->squeue);
		if (bcs->tx_skb) {
			dev_kfree_skb(bcs->tx_skb);
			bcs->tx_skb = NULL;
			test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
			if (bcs->cs->debug & L1_DEB_HSCX)
				debugl1(bcs->cs, "closeisar clear BC_FLG_BUSY");
		}
	}
}

int
open_isarstate(struct IsdnCardState *cs, struct BCState *bcs)
{
	if (!test_and_set_bit(BC_FLG_INIT, &bcs->Flag)) {
		if (!(bcs->hw.isar.rcvbuf = kmalloc(HSCX_BUFMAX, GFP_ATOMIC))) {
			printk(KERN_WARNING
			       "HiSax: No memory for isar.rcvbuf\n");
			return (1);
		}
		skb_queue_head_init(&bcs->rqueue);
		skb_queue_head_init(&bcs->squeue);
	}
	bcs->tx_skb = NULL;
	test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
	if (cs->debug & L1_DEB_HSCX)
		debugl1(cs, "openisar clear BC_FLG_BUSY");
	bcs->event = 0;
	bcs->hw.isar.rcvidx = 0;
	bcs->tx_cnt = 0;
	return (0);
}

int
setstack_isar(struct PStack *st, struct BCState *bcs)
{
	bcs->channel = st->l1.bc;
	if (open_isarstate(st->l1.hardware, bcs))
		return (-1);
	st->l1.bcs = bcs;
	st->l2.l2l1 = isar_l2l1;
	setstack_manager(st);
	bcs->st = st;
	setstack_l1_B(st);
	return (0);
}

HISAX_INITFUNC(void 
initisar(struct IsdnCardState *cs))
{
	cs->bcs[0].BC_SetStack = setstack_isar;
	cs->bcs[1].BC_SetStack = setstack_isar;
	cs->bcs[0].BC_Close = close_isarstate;
	cs->bcs[1].BC_Close = close_isarstate;
}
