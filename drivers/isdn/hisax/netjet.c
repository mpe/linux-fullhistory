/* $Id: netjet.c,v 1.3 1998/02/12 23:08:05 keil Exp $

 * netjet.c     low level stuff for Traverse Technologie NETJet ISDN cards
 *
 * Author     Karsten Keil (keil@temic-ech.spacenet.de)
 *
 * Thanks to Traverse Technologie Australia for documents and informations
 *
 *
 * $Log: netjet.c,v $
 * Revision 1.3  1998/02/12 23:08:05  keil
 * change for 2.1.86 (removing FREE_READ/FREE_WRITE from [dev]_kfree_skb()
 *
 * Revision 1.2  1998/02/02 13:32:06  keil
 * New
 *
 *
 *
 */

#define __NO_VERSION__
#include <linux/config.h>
#include "hisax.h"
#include "isac.h"
#include "hscx.h"
#include "isdnl1.h"
#include <linux/pci.h>
#include <linux/bios32.h>
#include <linux/interrupt.h>
#define fcstab  ppp_crc16_table
#include <linux/ppp_defs.h>
extern __u16 ppp_crc16_table[256]; /* from ppp code */

extern const char *CardType[];

const char *NETjet_revision = "$Revision: 1.3 $";

#define byteout(addr,val) outb(val,addr)
#define bytein(addr) inb(addr)

/* PCI stuff */
#define PCI_VENDOR_TRAVERSE_TECH 0xe159
#define PCI_NETJET_ID	0x0001

#define NETJET_CTRL	0x00
#define NETJET_DMACTRL	0x01
#define NETJET_AUXCTRL	0x02
#define NETJET_AUXDATA	0x03
#define NETJET_IRQMASK0 0x04
#define NETJET_IRQMASK1 0x05
#define NETJET_IRQSTAT0 0x06
#define NETJET_IRQSTAT1 0x07
#define NETJET_DMA_READ_START	0x08
#define NETJET_DMA_READ_IRQ	0x0c
#define NETJET_DMA_READ_END	0x10
#define NETJET_DMA_READ_ADR	0x14
#define NETJET_DMA_WRITE_START	0x18
#define NETJET_DMA_WRITE_IRQ	0x1c
#define NETJET_DMA_WRITE_END	0x20
#define NETJET_DMA_WRITE_ADR	0x24
#define NETJET_PULSE_CNT	0x28

#define NETJET_ISAC_OFF	0xc0
#define NETJET_ISACIRQ	0x10

#define NETJET_DMA_SIZE 512

#define HDLC_ZERO_SEARCH 0
#define HDLC_FLAG_SEARCH 1
#define HDLC_FLAG_FOUND  2
#define HDLC_FRAME_FOUND 3
#define HDLC_NULL 4
#define HDLC_PART 5
#define HDLC_FULL 6

#define HDLC_FLAG_VALUE	0x7e

/* Interface functions */

static u_char
ReadISAC(struct IsdnCardState *cs, u_char offset)
{
	long flags;
	u_char ret;
	
	save_flags(flags);
	cli();
	cs->hw.njet.auxd &= 0xfc;
	cs->hw.njet.auxd |= (offset>>4) & 3;
	byteout(cs->hw.njet.auxa, cs->hw.njet.auxd);
	ret = bytein(cs->hw.njet.isac + ((offset & 0xf)<<2));
	restore_flags(flags);
	return(ret);
}

static void
WriteISAC(struct IsdnCardState *cs, u_char offset, u_char value)
{
	long flags;
	
	save_flags(flags);
	cli();
	cs->hw.njet.auxd &= 0xfc;
	cs->hw.njet.auxd |= (offset>>4) & 3;
	byteout(cs->hw.njet.auxa, cs->hw.njet.auxd);
	byteout(cs->hw.njet.isac + ((offset & 0xf)<<2), value);
	restore_flags(flags);
}

static void
ReadISACfifo(struct IsdnCardState *cs, u_char *data, int size)
{
	cs->hw.njet.auxd &= 0xfc;
	byteout(cs->hw.njet.auxa, cs->hw.njet.auxd);
	insb(cs->hw.njet.isac, data, size);
}

static void 
WriteISACfifo(struct IsdnCardState *cs, u_char *data, int size)
{
	cs->hw.njet.auxd &= 0xfc;
	byteout(cs->hw.njet.auxa, cs->hw.njet.auxd);
	outsb(cs->hw.njet.isac, data, size);
}

void fill_mem(struct BCState *bcs, u_int *pos, u_int cnt, int chan, u_char fill)
{
	u_int mask=0x000000ff, val = 0, *p=pos;
	u_int i;
	
	val |= fill;
	if (chan) {
		val  <<= 8;
		mask <<= 8;
	}
	mask ^= 0xffffffff;
	for (i=0; i<cnt; i++) {
		*p   &= mask;
		*p++ |= val;
		if (p > bcs->hw.tiger.s_end)
			p = bcs->hw.tiger.send;
	}
}

void
mode_tiger(struct BCState *bcs, int mode, int bc)
{
	struct IsdnCardState *cs = bcs->cs;
	char tmp[64];

	if (cs->debug & L1_DEB_HSCX) {
		sprintf(tmp, "Tiger mode %d bchan %d/%d",
			mode, bc, bcs->channel);
		debugl1(cs, tmp);
	}
	bcs->mode = mode;
	bcs->channel = bc;
	switch (mode) {
		case (L1_MODE_NULL):
			fill_mem(bcs, bcs->hw.tiger.send,
				NETJET_DMA_SIZE, bc, 0xff);
			if (cs->debug & L1_DEB_HSCX) {
				sprintf(tmp, "Tiger stat rec %d/%d send %d",
					bcs->hw.tiger.r_tot, bcs->hw.tiger.r_err,
					bcs->hw.tiger.s_tot); 
				debugl1(cs, tmp);
			}
			if ((cs->bcs[0].mode == L1_MODE_NULL) &&
				(cs->bcs[1].mode == L1_MODE_NULL)) {
				cs->hw.njet.dmactrl = 0;
				byteout(cs->hw.njet.base + NETJET_DMACTRL,
					cs->hw.njet.dmactrl);
				byteout(cs->hw.njet.base + NETJET_IRQMASK0, 0);
			}
			break;
		case (L1_MODE_TRANS):
			break;
		case (L1_MODE_HDLC): 
			fill_mem(bcs, bcs->hw.tiger.send,
				NETJET_DMA_SIZE, bc, 0xff);
			bcs->hw.tiger.r_state = HDLC_ZERO_SEARCH;
			bcs->hw.tiger.r_tot = 0;
			bcs->hw.tiger.r_bitcnt = 0;
			bcs->hw.tiger.r_one = 0;
			bcs->hw.tiger.r_err = 0;
			bcs->hw.tiger.s_tot = 0;
			if (! cs->hw.njet.dmactrl) {
				fill_mem(bcs, bcs->hw.tiger.send,
					NETJET_DMA_SIZE, !bc, 0xff);
				cs->hw.njet.dmactrl = 1;
				byteout(cs->hw.njet.base + NETJET_DMACTRL,
					cs->hw.njet.dmactrl);
				byteout(cs->hw.njet.base + NETJET_IRQMASK0, 0x3f);
			}
			bcs->hw.tiger.sendp = bcs->hw.tiger.send;
			bcs->hw.tiger.free = NETJET_DMA_SIZE;
			test_and_set_bit(BC_FLG_EMPTY, &bcs->Flag);
			break;
	}
	if (cs->debug & L1_DEB_HSCX) {
		sprintf(tmp, "tiger: set %x %x %x  %x/%x  pulse=%d",
			bytein(cs->hw.njet.base + NETJET_DMACTRL),
			bytein(cs->hw.njet.base + NETJET_IRQMASK0),
			bytein(cs->hw.njet.base + NETJET_IRQSTAT0),
			inl(cs->hw.njet.base + NETJET_DMA_READ_ADR),
			inl(cs->hw.njet.base + NETJET_DMA_WRITE_ADR),
			bytein(cs->hw.njet.base + NETJET_PULSE_CNT));
		debugl1(cs, tmp);
	}
}

static u_char dummyrr(struct IsdnCardState *cs, int chan, u_char off)
{
	return(5);
}

static void dummywr(struct IsdnCardState *cs, int chan, u_char off, u_char value)
{
}

static void printframe(struct IsdnCardState *cs, u_char *buf, int count, char *s) {
	char tmp[128];
	char *t = tmp;
	int i=count,j;
	u_char *p = buf;

	t += sprintf(t, "tiger %s(%4d)", s, count);
	while (i>0) {
		if (i>16)
			j=16;
		else
			j=i;
		QuickHex(t, p, j);
		debugl1(cs, tmp);
		p += j;
		i -= j;
		t = tmp;
		t += sprintf(t, "tiger %s      ", s);
	}
}

#define MAKE_RAW_BYTE for (j=0; j<8; j++) { \
			bitcnt++;\
			s_val >>= 1;\
			if (val & 1) {\
				s_one++;\
				s_val |= 0x80;\
			} else {\
				s_one = 0;\
				s_val &= 0x7f;\
			}\
			if (bitcnt==8) {\
				bcs->hw.tiger.sendbuf[s_cnt++] = s_val;\
				bitcnt = 0;\
			}\
			if (s_one == 5) {\
				s_val >>= 1;\
				s_val &= 0x7f;\
				bitcnt++;\
				s_one = 0;\
			}\
			if (bitcnt==8) {\
				bcs->hw.tiger.sendbuf[s_cnt++] = s_val;\
				bitcnt = 0;\
			}\
			val >>= 1;\
		}

static void make_raw_data(struct BCState *bcs) {
	register u_int i,s_cnt=0;
	register u_char j;
	register u_char val;
	register u_char s_one = 0;
	register u_char s_val = 0;
	register u_char bitcnt = 0;
	u_int fcs;
	char tmp[64];
	
	
	bcs->hw.tiger.sendbuf[s_cnt++] = HDLC_FLAG_VALUE;
	fcs = PPP_INITFCS;
	for (i=0; i<bcs->hw.tiger.tx_skb->len; i++) {
		val = bcs->hw.tiger.tx_skb->data[i];
		fcs = PPP_FCS (fcs, val);
		MAKE_RAW_BYTE;
	}
	fcs ^= 0xffff;
	val = fcs & 0xff;
	MAKE_RAW_BYTE;
	val = (fcs>>8) & 0xff;
	MAKE_RAW_BYTE;
	val = HDLC_FLAG_VALUE;
	for (j=0; j<8; j++) { 
		bitcnt++;
		s_val >>= 1;
		if (val & 1)
			s_val |= 0x80;
		else
			s_val &= 0x7f;
		if (bitcnt==8) {
			bcs->hw.tiger.sendbuf[s_cnt++] = s_val;
			bitcnt = 0;
		}
		val >>= 1;
	}
	if (bcs->cs->debug & L1_DEB_HSCX) {
		sprintf(tmp,"tiger make_raw: in %d out %d.%d",
			bcs->hw.tiger.tx_skb->len, s_cnt, bitcnt);
		debugl1(bcs->cs,tmp);
	}
	if (bitcnt) {
		while (8>bitcnt++) {
			s_val >>= 1;
			s_val |= 0x80;
		}
		bcs->hw.tiger.sendbuf[s_cnt++] = s_val;
	}
	bcs->hw.tiger.sendcnt = s_cnt;
	bcs->tx_cnt -= bcs->hw.tiger.tx_skb->len;
	bcs->hw.tiger.sp = bcs->hw.tiger.sendbuf;
}

static void got_frame(struct BCState *bcs, int count) {
	struct sk_buff *skb;
		
	if (!(skb = dev_alloc_skb(count)))
		printk(KERN_WARNING "TIGER: receive out of memory\n");
	else {
		memcpy(skb_put(skb, count), bcs->hw.tiger.rcvbuf, count);
		skb_queue_tail(&bcs->rqueue, skb);
	}
	bcs->event |= 1 << B_RCVBUFREADY;
	queue_task(&bcs->tqueue, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
	
	if (bcs->cs->debug & L1_DEB_RECEIVE_FRAME)
		printframe(bcs->cs, bcs->hw.tiger.rcvbuf, count, "rec");
}



static void read_raw(struct BCState *bcs, u_int *buf, int cnt){
	int i;
	register u_char j;
	register u_char val;
	u_int  *pend = bcs->hw.tiger.rec +NETJET_DMA_SIZE -1;
	register u_char state = bcs->hw.tiger.r_state;
	register u_char r_one = bcs->hw.tiger.r_one;
	register u_char r_val = bcs->hw.tiger.r_val;
	register u_int bitcnt = bcs->hw.tiger.r_bitcnt;
	u_int *p = buf;
	char tmp[64];
        
	for (i=0;i<cnt;i++) {
		val = bcs->channel ? ((*p>>8) & 0xff) : (*p & 0xff);
		p++;
		if (p > pend)
			p = bcs->hw.tiger.rec;
		if (val == 0xff) {
			state = HDLC_ZERO_SEARCH;
			bcs->hw.tiger.r_tot++;
			bitcnt = 0;
			r_one = 0;
			continue;
		}
		for (j=0;j<8;j++) {
			if (state == HDLC_ZERO_SEARCH) {
				if (val & 1) {
					r_one++;
				} else {
					r_one=0;
					state= HDLC_FLAG_SEARCH;
					if (bcs->cs->debug & L1_DEB_HSCX) {
						sprintf(tmp,"tiger read_raw: zBit(%d,%d,%d) %x",
							bcs->hw.tiger.r_tot,i,j,val);
						debugl1(bcs->cs,tmp);
					}
				}
			} else if (state == HDLC_FLAG_SEARCH) { 
				if (val & 1) {
					r_one++;
					if (r_one>6) {
						state=HDLC_ZERO_SEARCH;
					}
				} else {
					if (r_one==6) {
						bitcnt=0;
						r_val=0;
						state=HDLC_FLAG_FOUND;
						if (bcs->cs->debug & L1_DEB_HSCX) {
							sprintf(tmp,"tiger read_raw: flag(%d,%d,%d) %x",
								bcs->hw.tiger.r_tot,i,j,val);
							debugl1(bcs->cs,tmp);
						}
					}
					r_one=0;
				}
			} else if (state ==  HDLC_FLAG_FOUND) {
				if (val & 1) {
					r_one++;
					if (r_one>6) {
						state=HDLC_ZERO_SEARCH;
					} else {
						r_val >>= 1;
						r_val |= 0x80;
						bitcnt++;
					}
				} else {
					if (r_one==6) {
						bitcnt=0;
						r_val=0;
						r_one=0;
						val >>= 1;
						continue;
					} else if (r_one!=5) {
						r_val >>= 1;
						r_val &= 0x7f;
						bitcnt++;
					}
					r_one=0;	
				}
				if ((state != HDLC_ZERO_SEARCH) &&
					!(bitcnt & 7)) {
					state=HDLC_FRAME_FOUND;
					bcs->hw.tiger.r_fcs = PPP_INITFCS;
					bcs->hw.tiger.rcvbuf[0] = r_val;
					bcs->hw.tiger.r_fcs = PPP_FCS (bcs->hw.tiger.r_fcs, r_val);
					if (bcs->cs->debug & L1_DEB_HSCX) {
						sprintf(tmp,"tiger read_raw: byte1(%d,%d,%d) rval %x val %x i %x",
							bcs->hw.tiger.r_tot,i,j,r_val,val,
							bcs->cs->hw.njet.irqstat0);
						debugl1(bcs->cs,tmp);
					}
				}
			} else if (state ==  HDLC_FRAME_FOUND) {
				if (val & 1) {
					r_one++;
					if (r_one>6) {
						state=HDLC_ZERO_SEARCH;
						bitcnt=0;
					} else {
						r_val >>= 1;
						r_val |= 0x80;
						bitcnt++;
					}
				} else {
					if (r_one==6) {
						r_val=0; 
						r_one=0;
						bitcnt++;
						if (bitcnt & 7) {
							debugl1(bcs->cs, "tiger: frame not byte aligned");
							state=HDLC_FLAG_SEARCH;
							bcs->hw.tiger.r_err++;
						} else {
							if (bcs->cs->debug & L1_DEB_HSCX) {
								sprintf(tmp,"tiger frame end(%d,%d): fcs(%x) i %x",
									i,j,bcs->hw.tiger.r_fcs, bcs->cs->hw.njet.irqstat0);
								debugl1(bcs->cs, tmp);
							}
							if (bcs->hw.tiger.r_fcs == PPP_GOODFCS) {
								got_frame(bcs, (bitcnt>>3)-3);
							} else
								if (bcs->cs->debug) {
									debugl1(bcs->cs, "tiger FCS error");
									printframe(bcs->cs, bcs->hw.tiger.rcvbuf,
										(bitcnt>>3)-1, "rec");
									bcs->hw.tiger.r_err++;
								}
							state=HDLC_FLAG_FOUND;
						}
						bitcnt=0;
					} else if (r_one==5) {
						val >>= 1;
						r_one=0;
						continue;
					} else {
						r_val >>= 1;
						r_val &= 0x7f;
						bitcnt++;
					}
					r_one=0;	
				}
				if ((state == HDLC_FRAME_FOUND) &&
					!(bitcnt & 7)) {
					if ((bitcnt>>3)>=HSCX_BUFMAX) {
						debugl1(bcs->cs, "tiger: frame to big");
						r_val=0; 
						state=HDLC_FLAG_SEARCH;
						bcs->hw.tiger.r_err++;
					} else {
						bcs->hw.tiger.rcvbuf[(bitcnt>>3)-1] = r_val;
						bcs->hw.tiger.r_fcs = 
							PPP_FCS (bcs->hw.tiger.r_fcs, r_val);
					}
				}
			}
			val >>= 1;
		}
		bcs->hw.tiger.r_tot++;
	}
	bcs->hw.tiger.r_state = state;
	bcs->hw.tiger.r_one = r_one;
	bcs->hw.tiger.r_val = r_val;
	bcs->hw.tiger.r_bitcnt = bitcnt;
}

static void read_tiger(struct IsdnCardState *cs) {
	u_int *p;
	int cnt = NETJET_DMA_SIZE/2;
	
	if (cs->hw.njet.irqstat0 & 4)
		p = cs->bcs[0].hw.tiger.rec + NETJET_DMA_SIZE - 1;
	else
		p = cs->bcs[0].hw.tiger.rec + cnt - 1;
	if (cs->bcs[0].mode == L1_MODE_HDLC)
		read_raw(cs->bcs, p, cnt);
	if (cs->bcs[1].mode == L1_MODE_HDLC)
		read_raw(cs->bcs + 1, p, cnt);
	cs->hw.njet.irqstat0 &= 0xf3;
}

static void write_raw(struct BCState *bcs, u_int *buf, int cnt);

static void fill_dma(struct BCState *bcs)
{
	char tmp[64];
	register u_int *p, *sp;
	register int cnt;

	if (!bcs->hw.tiger.tx_skb)
		return;
	if (bcs->cs->debug & L1_DEB_HSCX) {
		sprintf(tmp,"tiger fill_dma1: c%d %4x", bcs->channel,
			bcs->Flag);
			debugl1(bcs->cs,tmp);
	}
	if (test_and_set_bit(BC_FLG_BUSY, &bcs->Flag))
		return;
	make_raw_data(bcs);
	if (bcs->cs->debug & L1_DEB_HSCX) {
		sprintf(tmp,"tiger fill_dma2: c%d %4x", bcs->channel,
			bcs->Flag);
			debugl1(bcs->cs,tmp);
	}
	if (test_and_clear_bit(BC_FLG_NOFRAME, &bcs->Flag)) {
		write_raw(bcs, bcs->hw.tiger.sendp, bcs->hw.tiger.free);
	} else if (test_and_clear_bit(BC_FLG_HALF, &bcs->Flag)) {
		p = bus_to_virt(inl(bcs->cs->hw.njet.base + NETJET_DMA_READ_ADR));
		sp = bcs->hw.tiger.sendp;
		if (p == bcs->hw.tiger.s_end)
			p = bcs->hw.tiger.send -1;
		if (sp == bcs->hw.tiger.s_end)
			sp = bcs->hw.tiger.send -1;
		cnt = p - sp;
		if (cnt <0) {
			write_raw(bcs, bcs->hw.tiger.sendp, bcs->hw.tiger.free);
		} else {
			p++;
			cnt++;
			if (p > bcs->hw.tiger.s_end)
				p = bcs->hw.tiger.send;
			p++;
			cnt++;
			if (p > bcs->hw.tiger.s_end)
				p = bcs->hw.tiger.send;
			write_raw(bcs, p, bcs->hw.tiger.free - cnt);
		}
	} else if (test_and_clear_bit(BC_FLG_EMPTY, &bcs->Flag)) {
		p = bus_to_virt(inl(bcs->cs->hw.njet.base + NETJET_DMA_READ_ADR));
		cnt = bcs->hw.tiger.s_end - p;
		if (cnt < 2) {
			p = bcs->hw.tiger.send + 1;
			cnt = NETJET_DMA_SIZE/2 - 2;
		} else {
			p++;
			p++;
			if (cnt <= (NETJET_DMA_SIZE/2))
				cnt += NETJET_DMA_SIZE/2;
			cnt--;
			cnt--;
		}
		write_raw(bcs, p, cnt);
	}
	if (bcs->cs->debug & L1_DEB_HSCX) {
		sprintf(tmp,"tiger fill_dma3: c%d %4x", bcs->channel,
			bcs->Flag);
		debugl1(bcs->cs,tmp);
	}
}

static void write_raw(struct BCState *bcs, u_int *buf, int cnt) {
	u_int mask, val, *p=buf;
	u_int i, s_cnt;
	char tmp[64];
        
        if (cnt <= 0)
        	return;
	if (test_bit(BC_FLG_BUSY, &bcs->Flag)) {
		if (bcs->hw.tiger.sendcnt> cnt) {
			s_cnt = cnt;
			bcs->hw.tiger.sendcnt -= cnt;
		} else {
			s_cnt = bcs->hw.tiger.sendcnt;
			bcs->hw.tiger.sendcnt = 0;
		}
		if (bcs->channel)
			mask = 0xffff00ff;
		else
			mask = 0xffffff00;
		for (i=0; i<s_cnt; i++) {
			val = bcs->channel ? ((bcs->hw.tiger.sp[i] <<8) & 0xff00) :
				(bcs->hw.tiger.sp[i]);
			*p   &= mask;
			*p++ |= val;
			if (p>bcs->hw.tiger.s_end)
				p = bcs->hw.tiger.send;
		}
		bcs->hw.tiger.s_tot += s_cnt;
		if (bcs->cs->debug & L1_DEB_HSCX) {
			sprintf(tmp,"tiger write_raw: c%d %x-%x %d/%d %d %x", bcs->channel,
			(u_int)buf, (u_int)p, s_cnt, cnt, bcs->hw.tiger.sendcnt,
			bcs->cs->hw.njet.irqstat0);
			debugl1(bcs->cs,tmp);
		}
		if (bcs->cs->debug & L1_DEB_HSCX_FIFO)
			printframe(bcs->cs, bcs->hw.tiger.sp, s_cnt, "snd");
		bcs->hw.tiger.sp += s_cnt;
		bcs->hw.tiger.sendp = p;
		if (!bcs->hw.tiger.sendcnt) {
			if (!bcs->hw.tiger.tx_skb) {
				sprintf(tmp,"tiger write_raw: NULL skb s_cnt %d", s_cnt);
				debugl1(bcs->cs, tmp);
			} else {
				if (bcs->st->lli.l1writewakeup &&
					(PACKET_NOACK != bcs->hw.tiger.tx_skb->pkt_type))
					bcs->st->lli.l1writewakeup(bcs->st, bcs->hw.tiger.tx_skb->len);
				dev_kfree_skb(bcs->hw.tiger.tx_skb);
				bcs->hw.tiger.tx_skb = NULL;
			}
			test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
			bcs->hw.tiger.free = cnt - s_cnt;
			if (bcs->hw.tiger.free > (NETJET_DMA_SIZE/2))
				test_and_set_bit(BC_FLG_HALF, &bcs->Flag);
			else {
				test_and_clear_bit(BC_FLG_HALF, &bcs->Flag);
				test_and_set_bit(BC_FLG_NOFRAME, &bcs->Flag);
			}
			if ((bcs->hw.tiger.tx_skb = skb_dequeue(&bcs->squeue))) {
				fill_dma(bcs);
			} else {
				mask ^= 0xffffffff;
				if (s_cnt < cnt) {
					for (i=s_cnt; i<cnt;i++) {
						*p++ |= mask;
						if (p>bcs->hw.tiger.s_end)
							p = bcs->hw.tiger.send;
					}
					if (bcs->cs->debug & L1_DEB_HSCX) {
						sprintf(tmp, "tiger write_raw: fill rest %d",
							cnt - s_cnt);
						debugl1(bcs->cs,tmp);
					}
				}
				bcs->event |= 1 << B_XMTBUFREADY;
				queue_task(&bcs->tqueue, &tq_immediate);
				mark_bh(IMMEDIATE_BH);
			}
		}
	} else if (test_and_clear_bit(BC_FLG_NOFRAME, &bcs->Flag)) {
		test_and_set_bit(BC_FLG_HALF, &bcs->Flag);
		fill_mem(bcs, buf, cnt, bcs->channel, 0xff);
		bcs->hw.tiger.free += cnt;
		if (bcs->cs->debug & L1_DEB_HSCX) {
			sprintf(tmp,"tiger write_raw: fill half");
			debugl1(bcs->cs,tmp);
		}
	} else if (test_and_clear_bit(BC_FLG_HALF, &bcs->Flag)) {
		test_and_set_bit(BC_FLG_EMPTY, &bcs->Flag);
		fill_mem(bcs, buf, cnt, bcs->channel, 0xff);
		if (bcs->cs->debug & L1_DEB_HSCX) {
			sprintf(tmp,"tiger write_raw: fill full");
			debugl1(bcs->cs,tmp);
		}
	}
}

static void write_tiger(struct IsdnCardState *cs) {
	u_int *p, cnt = NETJET_DMA_SIZE/2;
	
	if (cs->hw.njet.irqstat0  & 1)
		p = cs->bcs[0].hw.tiger.send + NETJET_DMA_SIZE - 1;
	else
		p = cs->bcs[0].hw.tiger.send + cnt - 1;
	if (cs->bcs[0].mode == L1_MODE_HDLC)
		write_raw(cs->bcs, p, cnt);
	if (cs->bcs[1].mode == L1_MODE_HDLC)
		write_raw(cs->bcs + 1, p, cnt);
	cs->hw.njet.irqstat0 &= 0xfc;
}

static void
tiger_l2l1(struct PStack *st, int pr, void *arg)
{
	struct sk_buff *skb = arg;
	long flags;

	switch (pr) {
		case (PH_DATA_REQ):
			save_flags(flags);
			cli();
			if (st->l1.bcs->hw.tiger.tx_skb) {
				skb_queue_tail(&st->l1.bcs->squeue, skb);
				restore_flags(flags);
			} else {
				st->l1.bcs->hw.tiger.tx_skb = skb;
				st->l1.bcs->cs->BC_Send_Data(st->l1.bcs);
				restore_flags(flags);
			}
			break;
		case (PH_PULL_IND):
			if (st->l1.bcs->hw.tiger.tx_skb) {
				printk(KERN_WARNING "tiger_l2l1: this shouldn't happen\n");
				break;
			}
			save_flags(flags);
			cli();
			st->l1.bcs->hw.tiger.tx_skb = skb;
			st->l1.bcs->cs->BC_Send_Data(st->l1.bcs);
			restore_flags(flags);
			break;
		case (PH_PULL_REQ):
			if (!st->l1.bcs->hw.tiger.tx_skb) {
				test_and_clear_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
				st->l1.l1l2(st, PH_PULL_CNF, NULL);
			} else
				test_and_set_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
			break;
	}
}

void
close_tigerstate(struct BCState *bcs)
{
	struct sk_buff *skb;

	mode_tiger(bcs, 0, 0);
	if (test_and_clear_bit(BC_FLG_INIT, &bcs->Flag)) {
		if (bcs->hw.tiger.rcvbuf) {
			kfree(bcs->hw.tiger.rcvbuf);
			bcs->hw.tiger.rcvbuf = NULL;
		}
		if (bcs->hw.tiger.sendbuf) {
			kfree(bcs->hw.tiger.sendbuf);
			bcs->hw.tiger.sendbuf = NULL;
		}
		while ((skb = skb_dequeue(&bcs->rqueue))) {
			dev_kfree_skb(skb);
		}
		while ((skb = skb_dequeue(&bcs->squeue))) {
			dev_kfree_skb(skb);
		}
		if (bcs->hw.tiger.tx_skb) {
			dev_kfree_skb(bcs->hw.tiger.tx_skb);
			bcs->hw.tiger.tx_skb = NULL;
			test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
		}
	}
}

static int
open_tigerstate(struct IsdnCardState *cs, int bc)
{
	struct BCState *bcs = cs->bcs + bc;

	if (!test_and_set_bit(BC_FLG_INIT, &bcs->Flag)) {
		if (!(bcs->hw.tiger.rcvbuf = kmalloc(HSCX_BUFMAX, GFP_KERNEL))) {
			printk(KERN_WARNING
			       "HiSax: No memory for tiger.rcvbuf\n");
			return (1);
		}
		if (!(bcs->hw.tiger.sendbuf = kmalloc(RAW_BUFMAX, GFP_KERNEL))) {
			printk(KERN_WARNING
			       "HiSax: No memory for tiger.sendbuf\n");
			return (1);
		}
		skb_queue_head_init(&bcs->rqueue);
		skb_queue_head_init(&bcs->squeue);
	}
	bcs->hw.tiger.tx_skb = NULL;
	bcs->hw.tiger.sendcnt = 0;
	test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
	bcs->event = 0;
	bcs->tx_cnt = 0;
	return (0);
}

static void
tiger_manl1(struct PStack *st, int pr,
	  void *arg)
{
	switch (pr) {
		case (PH_ACTIVATE_REQ):
			test_and_set_bit(BC_FLG_ACTIV, &st->l1.bcs->Flag);
			mode_tiger(st->l1.bcs, st->l1.mode, st->l1.bc);
			st->l1.l1man(st, PH_ACTIVATE_CNF, NULL);
			break;
		case (PH_DEACTIVATE_REQ):
			if (!test_bit(BC_FLG_BUSY, &st->l1.bcs->Flag))
				mode_tiger(st->l1.bcs, 0, 0);
			test_and_clear_bit(BC_FLG_ACTIV, &st->l1.bcs->Flag);
			break;
	}
}

int
setstack_tiger(struct PStack *st, struct BCState *bcs)
{
	if (open_tigerstate(st->l1.hardware, bcs->channel))
		return (-1);
	st->l1.bcs = bcs;
	st->l2.l2l1 = tiger_l2l1;
	st->ma.manl1 = tiger_manl1;
	setstack_manager(st);
	bcs->st = st;
	return (0);
}

 
__initfunc(void
inittiger(struct IsdnCardState *cs))
{
	char tmp[128];

	if (!(cs->bcs[0].hw.tiger.send = kmalloc(NETJET_DMA_SIZE * sizeof(unsigned int),
		GFP_KERNEL | GFP_DMA))) {
		printk(KERN_WARNING
		       "HiSax: No memory for tiger.send\n");
		return;
	}
	cs->bcs[0].hw.tiger.s_irq = cs->bcs[0].hw.tiger.send + NETJET_DMA_SIZE/2 - 1;
	cs->bcs[0].hw.tiger.s_end = cs->bcs[0].hw.tiger.send + NETJET_DMA_SIZE - 1;
	cs->bcs[1].hw.tiger.send = cs->bcs[0].hw.tiger.send;
	cs->bcs[1].hw.tiger.s_irq = cs->bcs[0].hw.tiger.s_irq;
	cs->bcs[1].hw.tiger.s_end = cs->bcs[0].hw.tiger.s_end;
	
	memset(cs->bcs[0].hw.tiger.send, 0xff, NETJET_DMA_SIZE * sizeof(unsigned int));
	sprintf(tmp, "tiger: send buf %x - %x", (u_int)cs->bcs[0].hw.tiger.send,
		(u_int)(cs->bcs[0].hw.tiger.send + NETJET_DMA_SIZE - 1));
	debugl1(cs, tmp);
	outl(virt_to_bus(cs->bcs[0].hw.tiger.send),
		cs->hw.njet.base + NETJET_DMA_READ_START);
	outl(virt_to_bus(cs->bcs[0].hw.tiger.s_irq),
		cs->hw.njet.base + NETJET_DMA_READ_IRQ);
	outl(virt_to_bus(cs->bcs[0].hw.tiger.s_end),
		cs->hw.njet.base + NETJET_DMA_READ_END);
	if (!(cs->bcs[0].hw.tiger.rec = kmalloc(NETJET_DMA_SIZE * sizeof(unsigned int),
		GFP_KERNEL | GFP_DMA))) {
		printk(KERN_WARNING
		       "HiSax: No memory for tiger.rec\n");
		return;
	}
	sprintf(tmp, "tiger: rec buf %x - %x", (u_int)cs->bcs[0].hw.tiger.rec,
		(u_int)(cs->bcs[0].hw.tiger.rec + NETJET_DMA_SIZE - 1));
	debugl1(cs, tmp);
	cs->bcs[1].hw.tiger.rec = cs->bcs[0].hw.tiger.rec;
	memset(cs->bcs[0].hw.tiger.rec, 0xff, NETJET_DMA_SIZE * sizeof(unsigned int));
	outl(virt_to_bus(cs->bcs[0].hw.tiger.rec),
		cs->hw.njet.base + NETJET_DMA_WRITE_START);
	outl(virt_to_bus(cs->bcs[0].hw.tiger.rec + NETJET_DMA_SIZE/2 - 1),
		cs->hw.njet.base + NETJET_DMA_WRITE_IRQ);
	outl(virt_to_bus(cs->bcs[0].hw.tiger.rec + NETJET_DMA_SIZE - 1),
		cs->hw.njet.base + NETJET_DMA_WRITE_END);
	sprintf(tmp, "tiger: dmacfg  %x/%x  pulse=%d",
		inl(cs->hw.njet.base + NETJET_DMA_WRITE_ADR),
		inl(cs->hw.njet.base + NETJET_DMA_READ_ADR),
		bytein(cs->hw.njet.base + NETJET_PULSE_CNT));
	debugl1(cs, tmp);
	cs->hw.njet.last_is0 = 0;
	cs->bcs[0].BC_SetStack = setstack_tiger;
	cs->bcs[1].BC_SetStack = setstack_tiger;
	cs->bcs[0].BC_Close = close_tigerstate;
	cs->bcs[1].BC_Close = close_tigerstate;
}

void
releasetiger(struct IsdnCardState *cs)
{
	if (cs->bcs[0].hw.tiger.send) {
		kfree(cs->bcs[0].hw.tiger.send);
		cs->bcs[0].hw.tiger.send = NULL;
	}
	if (cs->bcs[1].hw.tiger.send) {
		cs->bcs[1].hw.tiger.send = NULL;
	}
	if (cs->bcs[0].hw.tiger.rec) {
		kfree(cs->bcs[0].hw.tiger.rec);
		cs->bcs[0].hw.tiger.rec = NULL;
	}
	if (cs->bcs[1].hw.tiger.rec) {
		cs->bcs[1].hw.tiger.rec = NULL;
	}
}

static void
netjet_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs = dev_id;
	u_char val, sval, stat = 1;
	char tmp[128];

	if (!cs) {
		printk(KERN_WARNING "NETjet: Spurious interrupt!\n");
		return;
	}
	if (!((sval = bytein(cs->hw.njet.base + NETJET_IRQSTAT1)) &
		NETJET_ISACIRQ)) {
		val = ReadISAC(cs, ISAC_ISTA);
		if (cs->debug & L1_DEB_ISAC) {
			sprintf(tmp, "tiger: i1 %x %x", sval, val);
			debugl1(cs, tmp);
		}
		if (val) {
			isac_interrupt(cs, val);
			stat |= 2;
		}
	}
	if ((cs->hw.njet.irqstat0 = bytein(cs->hw.njet.base + NETJET_IRQSTAT0))) {
/*		sprintf(tmp, "tiger: ist0 %x  %x %x  %x/%x  pulse=%d",
			sval, 
			bytein(cs->hw.njet.base + NETJET_DMACTRL),
			bytein(cs->hw.njet.base + NETJET_IRQMASK0),
			inl(cs->hw.njet.base + NETJET_DMA_READ_ADR),
			inl(cs->hw.njet.base + NETJET_DMA_WRITE_ADR),
			bytein(cs->hw.njet.base + NETJET_PULSE_CNT));
		debugl1(cs, tmp);
*/
		if (cs->hw.njet.last_is0 & cs->hw.njet.irqstat0 & 0xf) {
			sprintf(tmp, "tiger: ist0 %x->%x irq lost",
				cs->hw.njet.last_is0, cs->hw.njet.irqstat0);
			debugl1(cs, tmp);
		}
		cs->hw.njet.last_is0 = cs->hw.njet.irqstat0;
/*		cs->hw.njet.irqmask0 = ((0x0f & cs->hw.njet.irqstat0) ^ 0x0f) | 0x30;
*/		byteout(cs->hw.njet.base + NETJET_IRQSTAT0, cs->hw.njet.irqstat0);
/*		byteout(cs->hw.njet.base + NETJET_IRQMASK0, cs->hw.njet.irqmask0);
*/		if (cs->hw.njet.irqstat0 & 0x0c)
			read_tiger(cs);
		if (cs->hw.njet.irqstat0 & 0x03)
			write_tiger(cs);
	}
/*	if (!testcnt--) {
		cs->hw.njet.dmactrl = 0;
		byteout(cs->hw.njet.base + NETJET_DMACTRL,
			cs->hw.njet.dmactrl);
		byteout(cs->hw.njet.base + NETJET_IRQMASK0, 0);
	}
*/	if (stat & 2) {
		WriteISAC(cs, ISAC_MASK, 0xFF);
		WriteISAC(cs, ISAC_MASK, 0x0);
	}
}

static void
reset_netjet(struct IsdnCardState *cs)
{
	long flags;

	save_flags(flags);
	sti();
	cs->hw.njet.ctrl_reg = 0xff;  /* Reset On */
	byteout(cs->hw.njet.base + NETJET_CTRL, cs->hw.njet.ctrl_reg);
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout((10*HZ)/1000);	/* Timeout 10ms */
	cs->hw.njet.ctrl_reg = 0x00;  /* Reset Off and status read clear */
	byteout(cs->hw.njet.base + NETJET_CTRL, cs->hw.njet.ctrl_reg);
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout((10*HZ)/1000);	/* Timeout 10ms */
	restore_flags(flags);
	cs->hw.njet.auxd = 0;
	cs->hw.njet.dmactrl = 0;
	byteout(cs->hw.njet.base + NETJET_AUXCTRL, ~NETJET_ISACIRQ);
	byteout(cs->hw.njet.base + NETJET_IRQMASK1, NETJET_ISACIRQ);
	byteout(cs->hw.njet.auxa, cs->hw.njet.auxd);
}

void
release_io_netjet(struct IsdnCardState *cs)
{
	byteout(cs->hw.njet.base + NETJET_IRQMASK0, 0);
	byteout(cs->hw.njet.base + NETJET_IRQMASK1, 0);
	releasetiger(cs);
	release_region(cs->hw.njet.base, 256);
}


static int
NETjet_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
	switch (mt) {
		case CARD_RESET:
			reset_netjet(cs);
			return(0);
		case CARD_RELEASE:
			release_io_netjet(cs);
			return(0);
		case CARD_SETIRQ:
			return(request_irq(cs->irq, &netjet_interrupt,
					I4L_IRQ_FLAG, "HiSax", cs));
		case CARD_INIT:
			inittiger(cs);
			clear_pending_isac_ints(cs);
			initisac(cs);
			return(0);
		case CARD_TEST:
			return(0);
	}
	return(0);
}



static 	int pci_index __initdata = 0;

__initfunc(int
setup_netjet(struct IsdnCard *card))
{
	int bytecnt;
	struct IsdnCardState *cs = card->cs;
	char tmp[64];
#if CONFIG_PCI
	u_char pci_bus, pci_device_fn, pci_irq;
	u_int pci_ioaddr, found;
#endif

	strcpy(tmp, NETjet_revision);
	printk(KERN_INFO "HiSax: Traverse Tech. NETjet driver Rev. %s\n", HiSax_getrev(tmp));
	if (cs->typ != ISDN_CTYPE_NETJET)
		return(0);
#if CONFIG_PCI
	found = 0;
	for (; pci_index < 0xff; pci_index++) {
		if (pcibios_find_device(PCI_VENDOR_TRAVERSE_TECH,
			PCI_NETJET_ID, pci_index, &pci_bus, &pci_device_fn)
			== PCIBIOS_SUCCESSFUL)
			found = 1;
		else
			break;
		/* get IRQ */
		pcibios_read_config_byte(pci_bus, pci_device_fn,
			PCI_INTERRUPT_LINE, &pci_irq);

		/* get IO address */
		pcibios_read_config_dword(pci_bus, pci_device_fn,
			PCI_BASE_ADDRESS_0, &pci_ioaddr);
		if (found)
			break;
	}
	if (!found) {
		printk(KERN_WARNING "NETjet: No PCI card found\n");
		return(0);
	}
	if (!pci_irq) {
		printk(KERN_WARNING "NETjet: No IRQ for PCI card found\n");
		return(0);
	}
	if (!pci_ioaddr) {
		printk(KERN_WARNING "NETjet: No IO-Adr for PCI card found\n");
		return(0);
	}
	pci_ioaddr &= ~3; /* remove io/mem flag */
	cs->hw.njet.base = pci_ioaddr; 
	cs->hw.njet.auxa = pci_ioaddr + NETJET_AUXDATA;
	cs->hw.njet.isac = pci_ioaddr | NETJET_ISAC_OFF;
	cs->irq = pci_irq;
	bytecnt = 256;
#else
	printk(KERN_WARNING "NETjet: NO_PCI_BIOS\n");
	printk(KERN_WARNING "NETjet: unable to config NETJET PCI\n");
	return (0);
#endif /* CONFIG_PCI */
	printk(KERN_INFO
		"NETjet: PCI card configured at 0x%x IRQ %d\n",
		cs->hw.njet.base, cs->irq);
	if (check_region(cs->hw.njet.base, bytecnt)) {
		printk(KERN_WARNING
		       "HiSax: %s config port %x-%x already in use\n",
		       CardType[card->typ],
		       cs->hw.njet.base,
		       cs->hw.njet.base + bytecnt);
		return (0);
	} else {
		request_region(cs->hw.njet.base, bytecnt, "netjet isdn");
	}
	reset_netjet(cs);
	cs->readisac  = &ReadISAC;
	cs->writeisac = &WriteISAC;
	cs->readisacfifo  = &ReadISACfifo;
	cs->writeisacfifo = &WriteISACfifo;
	cs->BC_Read_Reg  = &dummyrr;
	cs->BC_Write_Reg = &dummywr;
	cs->BC_Send_Data = &fill_dma;
	cs->cardmsg = &NETjet_card_msg;
	ISACVersion(cs, "NETjet:");
	return (1);
}
