/* $Id: hfc_pci.c,v 1.13 1999/08/11 21:01:28 keil Exp $

 * hfc_pci.c     low level driver for CCD�s hfc-pci based cards
 *
 * Author     Werner Cornelius (werner@isdn4linux.de)
 *            based on existing driver for CCD hfc ISA cards
 *
 * Copyright 1999  by Werner Cornelius (werner@isdn4linux.de)
 * Copyright 1999  by Karsten Keil (keil@isdn4linux.de)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * $Log: hfc_pci.c,v $
 * Revision 1.13  1999/08/11 21:01:28  keil
 * new PCI codefix
 *
 * Revision 1.12  1999/08/10 16:01:58  calle
 * struct pci_dev changed in 2.3.13. Made the necessary changes.
 *
 * Revision 1.11  1999/08/09 19:13:32  werner
 * moved constant pci ids to pci id table
 *
 * Revision 1.10  1999/08/08 10:17:34  werner
 * added new PCI vendor and card ids for Manufacturer 0x1043
 *
 * Revision 1.9  1999/08/07 21:09:10  werner
 * Fixed another memcpy problem in fifo handling.
 * Thanks for debugging aid by Olaf Kordwittenborg.
 *
 * Revision 1.8  1999/07/23 14:25:15  werner
 * Some smaller bug fixes and prepared support for GCI/IOM bus
 *
 * Revision 1.7  1999/07/14 21:24:20  werner
 * fixed memcpy problem when using E-channel feature
 *
 * Revision 1.6  1999/07/13 21:08:08  werner
 * added echo channel logging feature.
 *
 * Revision 1.5  1999/07/12 21:05:10  keil
 * fix race in IRQ handling
 * added watchdog for lost IRQs
 *
 * Revision 1.4  1999/07/04 21:51:39  werner
 * Changes to solve problems with irq sharing and smp machines
 * Thanks to Karsten Keil and Alex Holden for giving aid with
 * testing and debugging
 *
 * Revision 1.3  1999/07/01 09:43:19  keil
 * removed additional schedules in timeouts
 *
 * Revision 1.2  1999/07/01 08:07:51  keil
 * Initial version
 *
 *
 *
 */

#define __NO_VERSION__
#include "hisax.h"
#include "hfc_pci.h"
#include "isdnl1.h"
#include <linux/pci.h>
#ifndef COMPAT_HAS_NEW_PCI
#include <linux/bios32.h>
#endif
#include <linux/interrupt.h>

extern const char *CardType[];

static const char *hfcpci_revision = "$Revision: 1.13 $";

static const int CCD_VENDOR_IDS[] = { 
	0x1043,   /* Asuscom  */
	0x1051,   /* Motorola MC145575 */
        0x1397,   /* CCD and Billion */
	0,
};

static const int CCD_DEVICE_IDS[] = { 
	0x675,    /* Asuscom  */
	0x100,    /* Motorola MC145575 */
        0x2BD0,   /* CCD and Billion */
	0,
};


#if CONFIG_PCI
/*****************************/
/* release D- and B-channels */
/*****************************/
void
releasehfcpci(struct IsdnCardState *cs)
{
	if (cs->bcs[0].hw.hfc.send) {
		kfree(cs->bcs[0].hw.hfc.send);
		cs->bcs[0].hw.hfc.send = NULL;
	}
	if (cs->bcs[1].hw.hfc.send) {
		kfree(cs->bcs[1].hw.hfc.send);
		cs->bcs[1].hw.hfc.send = NULL;
	}
}

/******************************************/
/* free hardware resources used by driver */
/******************************************/
void
release_io_hfcpci(struct IsdnCardState *cs)
{
#if CONFIG_PCI
	pcibios_write_config_word(cs->hw.hfcpci.pci_bus, cs->hw.hfcpci.pci_device_fn, PCI_COMMAND, 0);	/* disabe memory mapped ports + busmaster */
#endif	/* CONFIG_PCI */
	releasehfcpci(cs);
	del_timer(&cs->hw.hfcpci.timer);
	kfree(cs->hw.hfcpci.share_start);
	cs->hw.hfcpci.share_start = NULL;
	vfree(cs->hw.hfcpci.pci_io);
}

/********************************************************************************/
/* function called to reset the HFC PCI chip. A complete software reset of chip */
/* and fifos is done.                                                           */
/********************************************************************************/
static void
reset_hfcpci(struct IsdnCardState *cs)
{
	long flags;

	pcibios_write_config_word(cs->hw.hfcpci.pci_bus, cs->hw.hfcpci.pci_device_fn, PCI_COMMAND, PCI_ENA_MEMIO);	/* enable memory mapped ports, disable busmaster */
	printk(KERN_INFO "HFC_PCI: resetting card\n");
	pcibios_write_config_word(cs->hw.hfcpci.pci_bus, cs->hw.hfcpci.pci_device_fn, PCI_COMMAND, PCI_ENA_MEMIO + PCI_ENA_MASTER);	/* enable memory ports + busmaster */
	Write_hfc(cs, HFCPCI_CIRM, HFCPCI_RESET);	/* Reset On */
	save_flags(flags);
	sti();
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout((30 * HZ) / 1000);	/* Timeout 30ms */
	Write_hfc(cs, HFCPCI_CIRM, 0);	/* Reset Off */
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout((20 * HZ) / 1000);	/* Timeout 20ms */
	if (Read_hfc(cs, HFCPCI_STATUS) & 2)
		printk(KERN_WARNING "HFC-PCI init bit busy\n");

	cs->hw.hfcpci.fifo_en = 0x30;	/* only D fifos enabled */
	Write_hfc(cs, HFCPCI_FIFO_EN, cs->hw.hfcpci.fifo_en);

	cs->hw.hfcpci.trm = 0; /* no echo connect */
	Write_hfc(cs, HFCPCI_TRM, cs->hw.hfcpci.trm);

	Write_hfc(cs, HFCPCI_CLKDEL, 0x0e);	/* ST-Bit delay for TE-Mode */
	cs->hw.hfcpci.sctrl_e = HFCPCI_AUTO_AWAKE;
	Write_hfc(cs, HFCPCI_SCTRL_E, cs->hw.hfcpci.sctrl_e);	/* S/T Auto awake */
	cs->hw.hfcpci.bswapped = 0; /* no exchange */
	cs->hw.hfcpci.ctmt = HFCPCI_TIM3_125 | HFCPCI_AUTO_TIMER;
	Write_hfc(cs, HFCPCI_CTMT, cs->hw.hfcpci.ctmt);

	cs->hw.hfcpci.int_m2 = HFCPCI_IRQ_ENABLE;
	cs->hw.hfcpci.int_m1 = HFCPCI_INTS_DTRANS | HFCPCI_INTS_DREC | 
	                       HFCPCI_INTS_L1STATE | HFCPCI_CLTIMER;
	Write_hfc(cs, HFCPCI_INT_M1, cs->hw.hfcpci.int_m1);
	Write_hfc(cs, HFCPCI_INT_M2, cs->hw.hfcpci.int_m2);

	/* Clear already pending ints */
	if (Read_hfc(cs, HFCPCI_INT_S1));
	if (Read_hfc(cs, HFCPCI_INT_S2));

	Write_hfc(cs, HFCPCI_STATES, HFCPCI_LOAD_STATE | 2);	/* HFC ST 2 */
	udelay(10);
	Write_hfc(cs, HFCPCI_STATES, 2);	/* HFC ST 2 */
	cs->hw.hfcpci.mst_m = HFCPCI_MASTER;	/* HFC Master Mode */

	Write_hfc(cs, HFCPCI_MST_MODE, cs->hw.hfcpci.mst_m);
	cs->hw.hfcpci.sctrl = 0;
	Write_hfc(cs, HFCPCI_SCTRL, cs->hw.hfcpci.sctrl);
	cs->hw.hfcpci.sctrl_r = 0;
	Write_hfc(cs, HFCPCI_SCTRL_R, cs->hw.hfcpci.sctrl_r);

        /* Init GCI/IOM2 in master mode */
	/* Slots 0 and 1 are set for B-chan 1 and 2 */
	/* D- and monitor/CI channel are not enabled */
	/* STIO1 is used as output for data, B1+B2 from ST->IOM+HFC */
	/* STIO2 is used as data input, B1+B2 from IOM->ST */ 
	/* ST B-channel send disabled -> continous 1s */
	/* The IOM slots are always enabled */
	cs->hw.hfcpci.conn = 0x36; /* set data flow directions */
	Write_hfc(cs, HFCPCI_CONNECT, cs->hw.hfcpci.conn);
	Write_hfc(cs, HFCPCI_B1_SSL, 0x80); /* B1-Slot 0 STIO1 out enabled */
	Write_hfc(cs, HFCPCI_B2_SSL, 0x81); /* B2-Slot 1 STIO1 out enabled */
	Write_hfc(cs, HFCPCI_B1_RSL, 0x80); /* B1-Slot 0 STIO2 in enabled */
	Write_hfc(cs, HFCPCI_B2_RSL, 0x81); /* B2-Slot 1 STIO2 in enabled */
	restore_flags(flags);
}

/***************************************************/
/* Timer function called when kernel timer expires */
/***************************************************/
static void
hfcpci_Timer(struct IsdnCardState *cs)
{
	cs->hw.hfcpci.timer.expires = jiffies + 75;
	/* WD RESET */
/*      WriteReg(cs, HFCD_DATA, HFCD_CTMT, cs->hw.hfcpci.ctmt | 0x80);
   add_timer(&cs->hw.hfcpci.timer);
 */
}


/*********************************/
/* schedule a new D-channel task */
/*********************************/
static void
sched_event_D_pci(struct IsdnCardState *cs, int event)
{
	test_and_set_bit(event, &cs->event);
	queue_task(&cs->tqueue, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
}

/*********************************/
/* schedule a new b_channel task */
/*********************************/
static void
hfcpci_sched_event(struct BCState *bcs, int event)
{
	bcs->event |= 1 << event;
	queue_task(&bcs->tqueue, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
}

/************************************************/
/* select a b-channel entry matching and active */
/************************************************/
static
struct BCState *
Sel_BCS(struct IsdnCardState *cs, int channel)
{
	if (cs->bcs[0].mode && (cs->bcs[0].channel == channel))
		return (&cs->bcs[0]);
	else if (cs->bcs[1].mode && (cs->bcs[1].channel == channel))
		return (&cs->bcs[1]);
	else
		return (NULL);
}

/*********************************************/
/* read a complete B-frame out of the buffer */
/*********************************************/
static struct sk_buff
*
hfcpci_empty_fifo(struct BCState *bcs, bzfifo_type * bz, u_char * bdata, int count)
{
	u_char *ptr, *ptr1, new_f2;
	struct sk_buff *skb;
	struct IsdnCardState *cs = bcs->cs;
	int flags, total, maxlen, new_z2;
	z_type *zp;

	save_flags(flags);
	sti();
	if ((cs->debug & L1_DEB_HSCX) && !(cs->debug & L1_DEB_HSCX_FIFO))
		debugl1(cs, "hfcpci_empty_fifo");
	zp = &bz->za[bz->f2];	/* point to Z-Regs */
	new_z2 = zp->z2 + count;	/* new position in fifo */
	if (new_z2 >= (B_FIFO_SIZE + B_SUB_VAL))
		new_z2 -= B_FIFO_SIZE;	/* buffer wrap */
	new_f2 = (bz->f2 + 1) & MAX_B_FRAMES;
	if ((count > HSCX_BUFMAX + 3) || (count < 4) ||
	    (*(bdata + (zp->z1 - B_SUB_VAL)))) {
		if (cs->debug & L1_DEB_WARN)
			debugl1(cs, "hfcpci_empty_fifo: incoming packet invalid length %d or crc", count);
		bz->za[new_f2].z2 = new_z2;
		bz->f2 = new_f2;	/* next buffer */
		skb = NULL;
	} else if (!(skb = dev_alloc_skb(count - 3)))
		printk(KERN_WARNING "HFCPCI: receive out of memory\n");
	else {
		SET_SKB_FREE(skb);
		total = count;
		count -= 3;
		ptr = skb_put(skb, count);

 		if (zp->z2 + count <= B_FIFO_SIZE + B_SUB_VAL)
			maxlen = count;		/* complete transfer */
		else
			maxlen = B_FIFO_SIZE + B_SUB_VAL - zp->z2;	/* maximum */

		ptr1 = bdata + (zp->z2 - B_SUB_VAL);	/* start of data */
		memcpy(ptr, ptr1, maxlen);	/* copy data */
		count -= maxlen;

		if (count) {	/* rest remaining */
			ptr += maxlen;
			ptr1 = bdata;	/* start of buffer */
			memcpy(ptr, ptr1, count);	/* rest */
		}
		bz->za[new_f2].z2 = new_z2;
		bz->f2 = new_f2;	/* next buffer */

	}
	restore_flags(flags);
	return (skb);
}

/*******************************/
/* D-channel receive procedure */
/*******************************/
static
int
receive_dmsg(struct IsdnCardState *cs)
{
	struct sk_buff *skb;
	int maxlen;
	int rcnt, total;
	int count = 5;
	u_char *ptr, *ptr1;
	dfifo_type *df;
	z_type *zp;

	df = &((fifo_area *) (cs->hw.hfcpci.fifos))->d_chan.d_rx;
	if (test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
		debugl1(cs, "rec_dmsg blocked");
		return (1);
	}
	while (((df->f1 & D_FREG_MASK) != (df->f2 & D_FREG_MASK)) && count--) {
		zp = &df->za[df->f2 & D_FREG_MASK];
		rcnt = zp->z1 - zp->z2;
		if (rcnt < 0)
			rcnt += D_FIFO_SIZE;
		rcnt++;
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "hfcpci recd f1(%d) f2(%d) z1(%x) z2(%x) cnt(%d)",
				df->f1, df->f2, zp->z1, zp->z2, rcnt);

		if ((rcnt > MAX_DFRAME_LEN + 3) || (rcnt < 4) ||
		    (df->data[zp->z1])) {
			if (cs->debug & L1_DEB_WARN)
				debugl1(cs, "empty_fifo hfcpci paket inv. len %d or crc %d", rcnt, df->data[zp->z1]);
			df->f2 = ((df->f2 + 1) & MAX_D_FRAMES) | (MAX_D_FRAMES + 1);	/* next buffer */
			df->za[df->f2 & D_FREG_MASK].z2 = (zp->z2 + rcnt) & (D_FIFO_SIZE - 1);
		} else if ((skb = dev_alloc_skb(rcnt - 3))) {
			SET_SKB_FREE(skb);
			total = rcnt;
			rcnt -= 3;
			ptr = skb_put(skb, rcnt);

			if (zp->z2 + rcnt <= D_FIFO_SIZE)
				maxlen = rcnt;	/* complete transfer */
			else
				maxlen = D_FIFO_SIZE - zp->z2;	/* maximum */

			ptr1 = df->data + zp->z2;	/* start of data */
			memcpy(ptr, ptr1, maxlen);	/* copy data */
			rcnt -= maxlen;

			if (rcnt) {	/* rest remaining */
				ptr += maxlen;
				ptr1 = df->data;	/* start of buffer */
				memcpy(ptr, ptr1, rcnt);	/* rest */
			}
			df->f2 = ((df->f2 + 1) & MAX_D_FRAMES) | (MAX_D_FRAMES + 1);	/* next buffer */
			df->za[df->f2 & D_FREG_MASK].z2 = (zp->z2 + total) & (D_FIFO_SIZE - 1);

			skb_queue_tail(&cs->rq, skb);
			sched_event_D_pci(cs, D_RCVBUFREADY);
		} else
			printk(KERN_WARNING "HFC-PCI: D receive out of memory\n");
	}
	test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
	return (1);
}

/**********************************/
/* B-channel main receive routine */
/**********************************/
void
main_rec_hfcpci(struct BCState *bcs)
{
	long flags;
	struct IsdnCardState *cs = bcs->cs;
	int rcnt;
	int receive, count = 5;
	struct sk_buff *skb;
	bzfifo_type *bz;
	u_char *bdata;
	z_type *zp;


	save_flags(flags);
	if ((bcs->channel) && (!cs->hw.hfcpci.bswapped)) {
		bz = &((fifo_area *) (cs->hw.hfcpci.fifos))->b_chans.rxbz_b2;
		bdata = ((fifo_area *) (cs->hw.hfcpci.fifos))->b_chans.rxdat_b2;
	} else {
		bz = &((fifo_area *) (cs->hw.hfcpci.fifos))->b_chans.rxbz_b1;
		bdata = ((fifo_area *) (cs->hw.hfcpci.fifos))->b_chans.rxdat_b1;
	}
      Begin:
	count--;
	cli();
	if (test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
		debugl1(cs, "rec_data %d blocked", bcs->channel);
		restore_flags(flags);
		return;
	}
	sti();
	if (bz->f1 != bz->f2) {
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "hfcpci rec %d f1(%d) f2(%d)",
				bcs->channel, bz->f1, bz->f2);
		zp = &bz->za[bz->f2];

		rcnt = zp->z1 - zp->z2;
		if (rcnt < 0)
			rcnt += B_FIFO_SIZE;
		rcnt++;
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "hfcpci rec %d z1(%x) z2(%x) cnt(%d)",
				bcs->channel, zp->z1, zp->z2, rcnt);
		if ((skb = hfcpci_empty_fifo(bcs, bz, bdata, rcnt))) {
			cli();
			skb_queue_tail(&bcs->rqueue, skb);
			sti();
			hfcpci_sched_event(bcs, B_RCVBUFREADY);
		}
		rcnt = bz->f1 - bz->f2;
		if (rcnt < 0)
			rcnt += MAX_B_FRAMES + 1;
		if (rcnt > 1)
			receive = 1;
		else
			receive = 0;
	} else
		receive = 0;
	test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
	if (count && receive)
		goto Begin;
	restore_flags(flags);
	return;
}

/**************************/
/* D-channel send routine */
/**************************/
static void
hfcpci_fill_dfifo(struct IsdnCardState *cs)
{
	long flags;
	int fcnt;
	int count, new_z1, maxlen;
	dfifo_type *df;
	u_char *src, *dst, new_f1;

	if (!cs->tx_skb)
		return;
	if (cs->tx_skb->len <= 0)
		return;

	df = &((fifo_area *) (cs->hw.hfcpci.fifos))->d_chan.d_tx;

	if (cs->debug & L1_DEB_ISAC)
		debugl1(cs, "hfcpci_fill_Dfifo f1(%d) f2(%d) z1(f1)(%x)",
			df->f1, df->f2,
			df->za[df->f1 & D_FREG_MASK].z1);
	fcnt = df->f1 - df->f2;	/* frame count actually buffered */
	if (fcnt < 0)
		fcnt += (MAX_D_FRAMES + 1);	/* if wrap around */
	if (fcnt > (MAX_D_FRAMES - 1)) {
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "hfcpci_fill_Dfifo more as 14 frames");
		return;
	}
	/* now determine free bytes in FIFO buffer */
	count = df->za[df->f1 & D_FREG_MASK].z2 - df->za[df->f1 & D_FREG_MASK].z1;
	if (count <= 0)
		count += D_FIFO_SIZE;	/* count now contains available bytes */

	if (cs->debug & L1_DEB_ISAC)
		debugl1(cs, "hfcpci_fill_Dfifo count(%ld/%d)",
			cs->tx_skb->len, count);
	if (count < cs->tx_skb->len) {
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "hfcpci_fill_Dfifo no fifo mem");
		return;
	}
	count = cs->tx_skb->len;	/* get frame len */
	new_z1 = (df->za[df->f1 & D_FREG_MASK].z1 + count) & (D_FIFO_SIZE - 1);
	new_f1 = ((df->f1 + 1) & D_FREG_MASK) | (D_FREG_MASK + 1);
	src = cs->tx_skb->data;	/* source pointer */
	dst = df->data + df->za[df->f1 & D_FREG_MASK].z1;
	maxlen = D_FIFO_SIZE - df->za[df->f1 & D_FREG_MASK].z1;		/* end fifo */
	if (maxlen > count)
		maxlen = count;	/* limit size */
	memcpy(dst, src, maxlen);	/* first copy */

	count -= maxlen;	/* remaining bytes */
	if (count) {
		dst = df->data;	/* start of buffer */
		src += maxlen;	/* new position */
		memcpy(dst, src, count);
	}
	save_flags(flags);
	cli();
	df->za[new_f1 & D_FREG_MASK].z1 = new_z1;	/* for next buffer */
	df->za[df->f1 & D_FREG_MASK].z1 = new_z1;	/* new pos actual buffer */
	df->f1 = new_f1;	/* next frame */
	restore_flags(flags);

	idev_kfree_skb(cs->tx_skb, FREE_WRITE);
	cs->tx_skb = NULL;
	return;
}

/**************************/
/* B-channel send routine */
/**************************/
static void
hfcpci_fill_fifo(struct BCState *bcs)
{
	struct IsdnCardState *cs = bcs->cs;
	int flags, maxlen, fcnt;
	int count, new_z1;
	bzfifo_type *bz;
	u_char *bdata;
	u_char new_f1, *src, *dst;

	if (!bcs->tx_skb)
		return;
	if (bcs->tx_skb->len <= 0)
		return;

	save_flags(flags);
	sti();

	if ((bcs->channel) && (!cs->hw.hfcpci.bswapped)) {
		bz = &((fifo_area *) (cs->hw.hfcpci.fifos))->b_chans.txbz_b2;
		bdata = ((fifo_area *) (cs->hw.hfcpci.fifos))->b_chans.txdat_b2;
	} else {
		bz = &((fifo_area *) (cs->hw.hfcpci.fifos))->b_chans.txbz_b1;
		bdata = ((fifo_area *) (cs->hw.hfcpci.fifos))->b_chans.txdat_b1;
	}

	if (cs->debug & L1_DEB_HSCX)
		debugl1(cs, "hfcpci_fill_fifo %d f1(%d) f2(%d) z1(f1)(%x)",
			bcs->channel, bz->f1, bz->f2,
			bz->za[bz->f1].z1);

	fcnt = bz->f1 - bz->f2;	/* frame count actually buffered */
	if (fcnt < 0)
		fcnt += (MAX_B_FRAMES + 1);	/* if wrap around */
	if (fcnt > (MAX_B_FRAMES - 1)) {
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "hfcpci_fill_Bfifo more as 14 frames");
		restore_flags(flags);
		return;
	}
	/* now determine free bytes in FIFO buffer */
	count = bz->za[bz->f1].z2 - bz->za[bz->f1].z1;
	if (count <= 0)
		count += B_FIFO_SIZE;	/* count now contains available bytes */

	if (cs->debug & L1_DEB_HSCX)
		debugl1(cs, "hfcpci_fill_fifo %d count(%ld/%d),%lx",
			bcs->channel, bcs->tx_skb->len,
			count, current->state);

	if (count < bcs->tx_skb->len) {
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "hfcpci_fill_fifo no fifo mem");
		restore_flags(flags);
		return;
	}
	count = bcs->tx_skb->len;	/* get frame len */
	new_z1 = bz->za[bz->f1].z1 + count;	/* new buffer Position */
	if (new_z1 >= (B_FIFO_SIZE + B_SUB_VAL))
		new_z1 -= B_FIFO_SIZE;	/* buffer wrap */

	new_f1 = ((bz->f1 + 1) & MAX_B_FRAMES);
	src = bcs->tx_skb->data;	/* source pointer */
	dst = bdata + (bz->za[bz->f1].z1 - B_SUB_VAL);
	maxlen = (B_FIFO_SIZE + B_SUB_VAL) - bz->za[bz->f1].z1;		/* end fifo */
	if (maxlen > count)
		maxlen = count;	/* limit size */
	memcpy(dst, src, maxlen);	/* first copy */

	count -= maxlen;	/* remaining bytes */
	if (count) {
		dst = bdata;	/* start of buffer */
		src += maxlen;	/* new position */
		memcpy(dst, src, count);
	}
	bcs->tx_cnt -= bcs->tx_skb->len;
	if (bcs->st->lli.l1writewakeup &&
	    (PACKET_NOACK != bcs->tx_skb->pkt_type))
		bcs->st->lli.l1writewakeup(bcs->st, bcs->tx_skb->len);

	cli();
	bz->za[new_f1].z1 = new_z1;	/* for next buffer */
	bz->f1 = new_f1;	/* next frame */
	restore_flags(flags);

	idev_kfree_skb(bcs->tx_skb, FREE_WRITE);
	bcs->tx_skb = NULL;
	test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
	return;
}

/***********************/
/* set/reset echo mode */
/***********************/ 
int hfcpci_set_echo(struct IsdnCardState *cs, int i)
{ int flags;

  if (cs->chanlimit > 1)
    return(-EINVAL);

  save_flags(flags);
  cli();
  if (i) {
    cs->logecho = 1;
    cs->hw.hfcpci.trm |= 0x20; /* enable echo chan */
    cs->hw.hfcpci.int_m1 |= HFCPCI_INTS_B2REC;
    cs->hw.hfcpci.fifo_en |= HFCPCI_FIFOEN_B2RX;
  }
  else {
    cs->logecho = 0;
    cs->hw.hfcpci.trm &= ~0x20; /* enable echo chan */
    cs->hw.hfcpci.int_m1 &= ~HFCPCI_INTS_B2REC;
    cs->hw.hfcpci.fifo_en &= ~HFCPCI_FIFOEN_B2RX;
  }
    cs->hw.hfcpci.sctrl_r &= ~SCTRL_B2_ENA;
    cs->hw.hfcpci.sctrl &= ~SCTRL_B2_ENA;
    cs->hw.hfcpci.conn |= 0x10; /* B2-IOM -> B2-ST */ 
    cs->hw.hfcpci.ctmt &= ~2;
  Write_hfc(cs, HFCPCI_CTMT, cs->hw.hfcpci.ctmt);
  Write_hfc(cs, HFCPCI_SCTRL_R, cs->hw.hfcpci.sctrl_r);
  Write_hfc(cs, HFCPCI_SCTRL, cs->hw.hfcpci.sctrl);
  Write_hfc(cs, HFCPCI_CONNECT, cs->hw.hfcpci.conn);
  Write_hfc(cs, HFCPCI_TRM, cs->hw.hfcpci.trm);
  Write_hfc(cs, HFCPCI_FIFO_EN, cs->hw.hfcpci.fifo_en);
  Write_hfc(cs, HFCPCI_INT_M1, cs->hw.hfcpci.int_m1);
  restore_flags(flags);
  return(0);
} /* hfcpci_set_echo */ 

/*****************************/
/* E-channel receive routine */
/*****************************/
static void receive_emsg(struct IsdnCardState *cs)
{
	long flags;
	int rcnt;
	int receive, count = 5;
	bzfifo_type *bz;
	u_char *bdata;
	z_type *zp;
	u_char *ptr, *ptr1, new_f2;
	int total, maxlen, new_z2;
	u_char e_buffer[256];

	save_flags(flags);
	bz = &((fifo_area *) (cs->hw.hfcpci.fifos))->b_chans.rxbz_b2;
	bdata = ((fifo_area *) (cs->hw.hfcpci.fifos))->b_chans.rxdat_b2;
      Begin:
	count--;
	cli();
	if (test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
		debugl1(cs, "echo_rec_data blocked");
		restore_flags(flags);
		return;
	}
	sti();
	if (bz->f1 != bz->f2) {
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "hfcpci e_rec f1(%d) f2(%d)",
				bz->f1, bz->f2);
		zp = &bz->za[bz->f2];

		rcnt = zp->z1 - zp->z2;
		if (rcnt < 0)
			rcnt += B_FIFO_SIZE;
		rcnt++;
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "hfcpci e_rec z1(%x) z2(%x) cnt(%d)",
				zp->z1, zp->z2, rcnt);
                new_z2 = zp->z2 + rcnt; /* new position in fifo */
		if (new_z2 >= (B_FIFO_SIZE + B_SUB_VAL))
		  new_z2 -= B_FIFO_SIZE; /* buffer wrap */
		new_f2 = (bz->f2 + 1) & MAX_B_FRAMES;
	        if ((rcnt > 256 + 3) || (count < 4) ||
		    (*(bdata + (zp->z1 - B_SUB_VAL)))) {
		  if (cs->debug & L1_DEB_WARN)
			debugl1(cs, "hfcpci_empty_echan: incoming packet invalid length %d or crc", rcnt);
		  bz->za[new_f2].z2 = new_z2;
		  bz->f2 = new_f2;	/* next buffer */
		} else {
		    total = rcnt;
		    rcnt -= 3;
		    ptr = e_buffer;

		    if (zp->z2 <= B_FIFO_SIZE + B_SUB_VAL)
			maxlen = rcnt;		/* complete transfer */
		    else
			maxlen = B_FIFO_SIZE + B_SUB_VAL - zp->z2;	/* maximum */

		    ptr1 = bdata + (zp->z2 - B_SUB_VAL);	/* start of data */
		    memcpy(ptr, ptr1, maxlen);	/* copy data */
		    rcnt -= maxlen;

		    if (rcnt) {	/* rest remaining */
			ptr += maxlen;
			ptr1 = bdata;	/* start of buffer */
			memcpy(ptr, ptr1, rcnt);	/* rest */
		    }
		    bz->za[new_f2].z2 = new_z2;
		    bz->f2 = new_f2;	/* next buffer */
		    if (cs->debug & DEB_DLOG_HEX) {
		      	ptr = cs->dlog;
			if ((total - 3) < MAX_DLOG_SPACE / 3 - 10) {
			  *ptr++ = 'E';
			  *ptr++ = 'C';
			  *ptr++ = 'H';
			  *ptr++ = 'O';
			  *ptr++ = ':';
			  ptr += QuickHex(ptr, e_buffer, total - 3);
			  ptr--;
			  *ptr++ = '\n';
			  *ptr = 0;
			  HiSax_putstatus(cs, NULL, cs->dlog);
			} else
			  HiSax_putstatus(cs, "LogEcho: ", "warning Frame too big (%d)", total - 3);
		    }

	}

		rcnt = bz->f1 - bz->f2;
		if (rcnt < 0)
			rcnt += MAX_B_FRAMES + 1;
		if (rcnt > 1)
			receive = 1;
		else
			receive = 0;
	} else
		receive = 0;
	test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
	if (count && receive)
		goto Begin;
	restore_flags(flags);
	return;
} /* receive_emsg */

/*********************/
/* Interrupt handler */
/*********************/
static void
hfcpci_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs = dev_id;
	u_char exval;
	struct BCState *bcs;
	int count = 15;
	long flags;
	u_char val, stat;

	if (!cs) {
		printk(KERN_WARNING "HFC-PCI: Spurious interrupt!\n");
		return;
	}
	if (HFCPCI_ANYINT & (stat = Read_hfc(cs, HFCPCI_STATUS))) {
		val = Read_hfc(cs, HFCPCI_INT_S1);
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "HFC-PCI: stat(%02x) s1(%02x)", stat, val);
	} else
		return;

	if (cs->debug & L1_DEB_ISAC)
		debugl1(cs, "HFC-PCI irq %x %s", val,
			test_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags) ?
			"locked" : "unlocked");
	val &= cs->hw.hfcpci.int_m1;
	if (val & 0x40) {	/* TE state machine irq */
		exval = Read_hfc(cs, HFCPCI_STATES) & 0xf;
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "ph_state chg %d->%d", cs->dc.hfcpci.ph_state,
				exval);
		cs->dc.hfcpci.ph_state = exval;
		sched_event_D_pci(cs, D_L1STATECHANGE);
		val &= ~0x40;
	}
	while (val) {
		save_flags(flags);
		cli();
		if (test_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
			cs->hw.hfcpci.int_s1 |= val;
			restore_flags(flags);
			return;
		}
		if (cs->hw.hfcpci.int_s1 & 0x18) {
			exval = val;
			val = cs->hw.hfcpci.int_s1;
			cs->hw.hfcpci.int_s1 = exval;
		}
		if (val & 0x08) {
			if (!(bcs = Sel_BCS(cs, cs->hw.hfcpci.bswapped ? 1:0))) {
				if (cs->debug)
					debugl1(cs, "hfcpci spurious 0x08 IRQ");
			} else
				main_rec_hfcpci(bcs);
		}
		if (val & 0x10) { 
		        if (cs->logecho)
			  receive_emsg(cs);
			else    
			if (!(bcs = Sel_BCS(cs, 1))) {
				if (cs->debug)
					debugl1(cs, "hfcpci spurious 0x10 IRQ");
			} else
				main_rec_hfcpci(bcs);
		}
		if (val & 0x01) {
			if (!(bcs = Sel_BCS(cs, cs->hw.hfcpci.bswapped ? 1:0))) {
				if (cs->debug)
					debugl1(cs, "hfcpci spurious 0x01 IRQ");
			} else {
				if (bcs->tx_skb) {
					if (!test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
						hfcpci_fill_fifo(bcs);
						test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
					} else
						debugl1(cs, "fill_data %d blocked", bcs->channel);
				} else {
					if ((bcs->tx_skb = skb_dequeue(&bcs->squeue))) {
						if (!test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
							hfcpci_fill_fifo(bcs);
							test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
						} else
							debugl1(cs, "fill_data %d blocked", bcs->channel);
					} else {
						hfcpci_sched_event(bcs, B_XMTBUFREADY);
					}
				}
			}
		}
		if (val & 0x02) {
			if (!(bcs = Sel_BCS(cs, 1))) {
				if (cs->debug)
					debugl1(cs, "hfcpci spurious 0x02 IRQ");
			} else {
				if (bcs->tx_skb) {
					if (!test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
						hfcpci_fill_fifo(bcs);
						test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
					} else
						debugl1(cs, "fill_data %d blocked", bcs->channel);
				} else {
					if ((bcs->tx_skb = skb_dequeue(&bcs->squeue))) {
						if (!test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
							hfcpci_fill_fifo(bcs);
							test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
						} else
							debugl1(cs, "fill_data %d blocked", bcs->channel);
					} else {
						hfcpci_sched_event(bcs, B_XMTBUFREADY);
					}
				}
			}
		}
		if (val & 0x20) {	/* receive dframe */
			receive_dmsg(cs);
		}
		if (val & 0x04) {	/* dframe transmitted */
			if (test_and_clear_bit(FLG_DBUSY_TIMER, &cs->HW_Flags))
				del_timer(&cs->dbusytimer);
			if (test_and_clear_bit(FLG_L1_DBUSY, &cs->HW_Flags))
				sched_event_D_pci(cs, D_CLEARBUSY);
			if (cs->tx_skb) {
				if (cs->tx_skb->len) {
					if (!test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
						hfcpci_fill_dfifo(cs);
						test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
					} else {
						debugl1(cs, "hfcpci_fill_dfifo irq blocked");
					}
					goto afterXPR;
				} else {
					idev_kfree_skb(cs->tx_skb, FREE_WRITE);
					cs->tx_cnt = 0;
					cs->tx_skb = NULL;
				}
			}
			if ((cs->tx_skb = skb_dequeue(&cs->sq))) {
				cs->tx_cnt = 0;
				if (!test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
					hfcpci_fill_dfifo(cs);
					test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
				} else {
					debugl1(cs, "hfcpci_fill_dfifo irq blocked");
				}
			} else
				sched_event_D_pci(cs, D_XMTBUFREADY);
		}
	      afterXPR:
		if (cs->hw.hfcpci.int_s1 && count--) {
			val = cs->hw.hfcpci.int_s1;
			cs->hw.hfcpci.int_s1 = 0;
			if (cs->debug & L1_DEB_ISAC)
				debugl1(cs, "HFC-PCI irq %x loop %d", val, 15 - count);
		} else
			val = 0;
		restore_flags(flags);
	}
}

/********************************************************************/
/* timer callback for D-chan busy resolution. Currently no function */
/********************************************************************/
static void
hfcpci_dbusy_timer(struct IsdnCardState *cs)
{
#if 0
	struct PStack *stptr;
	if (test_bit(FLG_DBUSY_TIMER, &cs->HW_Flags)) {
		if (cs->debug)
			debugl1(cs, "D-Channel Busy");
		test_and_set_bit(FLG_L1_DBUSY, &cs->HW_Flags);
		stptr = cs->stlist;

		while (stptr != NULL) {
			stptr->l1.l1l2(stptr, PH_PAUSE | INDICATION, NULL);
			stptr = stptr->next;
		}
	}
#endif
}

/*************************************/
/* Layer 1 D-channel hardware access */
/*************************************/
static void
HFCPCI_l1hw(struct PStack *st, int pr, void *arg)
{
	struct IsdnCardState *cs = (struct IsdnCardState *) st->l1.hardware;
	struct sk_buff *skb = arg;

	switch (pr) {
		case (PH_DATA | REQUEST):
			if (cs->debug & DEB_DLOG_HEX)
				LogFrame(cs, skb->data, skb->len);
			if (cs->debug & DEB_DLOG_VERBOSE)
				dlogframe(cs, skb, 0);
			if (cs->tx_skb) {
				skb_queue_tail(&cs->sq, skb);
#ifdef L2FRAME_DEBUG		/* psa */
				if (cs->debug & L1_DEB_LAPD)
					Logl2Frame(cs, skb, "PH_DATA Queued", 0);
#endif
			} else {
				cs->tx_skb = skb;
				cs->tx_cnt = 0;
#ifdef L2FRAME_DEBUG		/* psa */
				if (cs->debug & L1_DEB_LAPD)
					Logl2Frame(cs, skb, "PH_DATA", 0);
#endif
				if (!test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
					hfcpci_fill_dfifo(cs);
					test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
				} else
					debugl1(cs, "hfcpci_fill_dfifo blocked");

			}
			break;
		case (PH_PULL | INDICATION):
			if (cs->tx_skb) {
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, " l2l1 tx_skb exist this shouldn't happen");
				skb_queue_tail(&cs->sq, skb);
				break;
			}
			if (cs->debug & DEB_DLOG_HEX)
				LogFrame(cs, skb->data, skb->len);
			if (cs->debug & DEB_DLOG_VERBOSE)
				dlogframe(cs, skb, 0);
			cs->tx_skb = skb;
			cs->tx_cnt = 0;
#ifdef L2FRAME_DEBUG		/* psa */
			if (cs->debug & L1_DEB_LAPD)
				Logl2Frame(cs, skb, "PH_DATA_PULLED", 0);
#endif
			if (!test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
				hfcpci_fill_dfifo(cs);
				test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
			} else
				debugl1(cs, "hfcpci_fill_dfifo blocked");
			break;
		case (PH_PULL | REQUEST):
#ifdef L2FRAME_DEBUG		/* psa */
			if (cs->debug & L1_DEB_LAPD)
				debugl1(cs, "-> PH_REQUEST_PULL");
#endif
			if (!cs->tx_skb) {
				test_and_clear_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
				st->l1.l1l2(st, PH_PULL | CONFIRM, NULL);
			} else
				test_and_set_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
			break;
		case (HW_RESET | REQUEST):
			Write_hfc(cs, HFCPCI_STATES, HFCPCI_LOAD_STATE | 3);	/* HFC ST 3 */
			udelay(6);
			Write_hfc(cs, HFCPCI_STATES, 3);	/* HFC ST 2 */
			cs->hw.hfcpci.mst_m |= HFCPCI_MASTER;
			Write_hfc(cs, HFCPCI_MST_MODE, cs->hw.hfcpci.mst_m);
			Write_hfc(cs, HFCPCI_STATES, HFCPCI_ACTIVATE | HFCPCI_DO_ACTION);
			l1_msg(cs, HW_POWERUP | CONFIRM, NULL);
			break;
		case (HW_ENABLE | REQUEST):
			Write_hfc(cs, HFCPCI_STATES, HFCPCI_ACTIVATE | HFCPCI_DO_ACTION);
			break;
		case (HW_DEACTIVATE | REQUEST):
      			cs->hw.hfcpci.mst_m &= ~HFCPCI_MASTER;
			Write_hfc(cs, HFCPCI_MST_MODE, cs->hw.hfcpci.mst_m);
			break;
		case (HW_INFO3 | REQUEST):
			cs->hw.hfcpci.mst_m |= HFCPCI_MASTER;
			Write_hfc(cs, HFCPCI_MST_MODE, cs->hw.hfcpci.mst_m);
			break;
#if 0
		case (HW_TESTLOOP | REQUEST):
			u_char val = 0;
			if (1 & (int) arg)
				val |= 0x0c;
			if (2 & (int) arg)
				val |= 0x3;
			if (test_bit(HW_IOM1, &cs->HW_Flags)) {
				/* IOM 1 Mode */
				if (!val) {
					cs->writeisac(cs, ISAC_SPCR, 0xa);
					cs->writeisac(cs, ISAC_ADF1, 0x2);
				} else {
					cs->writeisac(cs, ISAC_SPCR, val);
					cs->writeisac(cs, ISAC_ADF1, 0xa);
				}
			} else {
				/* IOM 2 Mode */
				cs->writeisac(cs, ISAC_SPCR, val);
				if (val)
					cs->writeisac(cs, ISAC_ADF1, 0x8);
				else
					cs->writeisac(cs, ISAC_ADF1, 0x0);
			}
			break;
#endif
		default:
			if (cs->debug & L1_DEB_WARN)
				debugl1(cs, "hfcpci_l1hw unknown pr %4x", pr);
			break;
	}
}

/***********************************************/
/* called during init setting l1 stack pointer */
/***********************************************/
void
setstack_hfcpci(struct PStack *st, struct IsdnCardState *cs)
{
	st->l1.l1hw = HFCPCI_l1hw;
}

/**************************************/
/* send B-channel data if not blocked */
/**************************************/
static void
hfcpci_send_data(struct BCState *bcs)
{
	struct IsdnCardState *cs = bcs->cs;

	if (!test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
		hfcpci_fill_fifo(bcs);
		test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
	} else
		debugl1(cs, "send_data %d blocked", bcs->channel);
}

/***************************************************************/
/* activate/deactivate hardware for selected channels and mode */
/***************************************************************/
void
mode_hfcpci(struct BCState *bcs, int mode, int bc)
{
	struct IsdnCardState *cs = bcs->cs;
	int flags;

	if (cs->debug & L1_DEB_HSCX)
		debugl1(cs, "HFCPCI bchannel mode %d bchan %d/%d",
			mode, bc, bcs->channel);
	bcs->mode = mode;
	bcs->channel = bc;
	if (cs->chanlimit > 1) { 
	  cs->hw.hfcpci.bswapped = 0; /* B1 and B2 normal mode */
	  cs->hw.hfcpci.sctrl_e &= ~0x80; 
	}
	else {
	  if (bc) {
	    cs->hw.hfcpci.bswapped = 1; /* B1 and B2 exchanged */
	    cs->hw.hfcpci.sctrl_e |= 0x80;
	    bc = 0; /* B1 controller used */
	  }
	  else {
	    cs->hw.hfcpci.bswapped = 0; /* B1 and B2 normal mode */
	    cs->hw.hfcpci.sctrl_e &= ~0x80; 
	  }  
	}  
	save_flags(flags);
	cli();
	switch (mode) {
		case (L1_MODE_NULL):
			if (bc) {
				cs->hw.hfcpci.sctrl &= ~SCTRL_B2_ENA;
				cs->hw.hfcpci.sctrl_r &= ~SCTRL_B2_ENA;
				cs->hw.hfcpci.fifo_en &= ~HFCPCI_FIFOEN_B2;
				cs->hw.hfcpci.int_m1 &= ~(HFCPCI_INTS_B2TRANS+HFCPCI_INTS_B2REC);
			} else {
				cs->hw.hfcpci.sctrl &= ~SCTRL_B1_ENA;
				cs->hw.hfcpci.sctrl_r &= ~SCTRL_B1_ENA;
				cs->hw.hfcpci.fifo_en &= ~HFCPCI_FIFOEN_B1;
				cs->hw.hfcpci.int_m1 &= ~(HFCPCI_INTS_B1TRANS+HFCPCI_INTS_B1REC);
			}
			break;
		case (L1_MODE_TRANS):
			if (bc) {
				cs->hw.hfcpci.ctmt |= 2;
				cs->hw.hfcpci.conn &= ~0x18;
				cs->hw.hfcpci.sctrl |= SCTRL_B2_ENA;
				cs->hw.hfcpci.sctrl_r |= SCTRL_B2_ENA;
				cs->hw.hfcpci.fifo_en |= HFCPCI_FIFOEN_B2;
				cs->hw.hfcpci.int_m1 |= (HFCPCI_INTS_B2TRANS+HFCPCI_INTS_B2REC);
			} else {
				cs->hw.hfcpci.ctmt |= 1;
				cs->hw.hfcpci.conn &= ~0x03;
				cs->hw.hfcpci.sctrl |= SCTRL_B1_ENA;
				cs->hw.hfcpci.sctrl_r |= SCTRL_B1_ENA;
				cs->hw.hfcpci.fifo_en |= HFCPCI_FIFOEN_B1;
				cs->hw.hfcpci.int_m1 |= (HFCPCI_INTS_B1TRANS+HFCPCI_INTS_B1REC);
			}
			break;
		case (L1_MODE_HDLC):
			if (bc) {
				cs->hw.hfcpci.ctmt &= ~2;
				cs->hw.hfcpci.conn &= ~0x18;
				cs->hw.hfcpci.sctrl |= SCTRL_B2_ENA;
				cs->hw.hfcpci.sctrl_r |= SCTRL_B2_ENA;
				cs->hw.hfcpci.fifo_en |= HFCPCI_FIFOEN_B2;
				cs->hw.hfcpci.int_m1 |= (HFCPCI_INTS_B2TRANS+HFCPCI_INTS_B2REC);
			} else {
				cs->hw.hfcpci.ctmt &= ~1;
				cs->hw.hfcpci.conn &= ~0x3;
				cs->hw.hfcpci.sctrl |= SCTRL_B1_ENA;
				cs->hw.hfcpci.sctrl_r |= SCTRL_B1_ENA;
				cs->hw.hfcpci.fifo_en |= HFCPCI_FIFOEN_B1;
				cs->hw.hfcpci.int_m1 |= (HFCPCI_INTS_B1TRANS+HFCPCI_INTS_B1REC);
			}
			break;
		case (L1_MODE_EXTRN):
			if (bc) {
			        cs->hw.hfcpci.conn |= 0x10;
				cs->hw.hfcpci.sctrl |= SCTRL_B2_ENA;
				cs->hw.hfcpci.sctrl_r |= SCTRL_B2_ENA;
				cs->hw.hfcpci.fifo_en &= ~HFCPCI_FIFOEN_B2;
				cs->hw.hfcpci.int_m1 &= ~(HFCPCI_INTS_B2TRANS+HFCPCI_INTS_B2REC);
			} else {
			        cs->hw.hfcpci.conn |= 0x02;
				cs->hw.hfcpci.sctrl |= SCTRL_B1_ENA;
				cs->hw.hfcpci.sctrl_r |= SCTRL_B1_ENA;
				cs->hw.hfcpci.fifo_en &= ~HFCPCI_FIFOEN_B1;
				cs->hw.hfcpci.int_m1 &= ~(HFCPCI_INTS_B1TRANS+HFCPCI_INTS_B1REC);
			}
			break;
	}
	Write_hfc(cs, HFCPCI_INT_M1, cs->hw.hfcpci.int_m1);
	restore_flags(flags);
	Write_hfc(cs, HFCPCI_FIFO_EN, cs->hw.hfcpci.fifo_en);
	Write_hfc(cs, HFCPCI_SCTRL, cs->hw.hfcpci.sctrl);
	Write_hfc(cs, HFCPCI_SCTRL_R, cs->hw.hfcpci.sctrl_r);
	Write_hfc(cs, HFCPCI_CTMT, cs->hw.hfcpci.ctmt);
	Write_hfc(cs, HFCPCI_CONNECT, cs->hw.hfcpci.conn);
}

/******************************/
/* Layer2 -> Layer 1 Transfer */
/******************************/
static void
hfcpci_l2l1(struct PStack *st, int pr, void *arg)
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
/*                              test_and_set_bit(BC_FLG_BUSY, &st->l1.bcs->Flag);
 */ st->l1.bcs->cs->BC_Send_Data(st->l1.bcs);
				restore_flags(flags);
			}
			break;
		case (PH_PULL | INDICATION):
			if (st->l1.bcs->tx_skb) {
				printk(KERN_WARNING "hfc_l2l1: this shouldn't happen\n");
				break;
			}
			save_flags(flags);
			cli();
/*                      test_and_set_bit(BC_FLG_BUSY, &st->l1.bcs->Flag);
 */ st->l1.bcs->tx_skb = skb;
			st->l1.bcs->cs->BC_Send_Data(st->l1.bcs);
			restore_flags(flags);
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
			mode_hfcpci(st->l1.bcs, st->l1.mode, st->l1.bc);
			l1_msg_b(st, pr, arg);
			break;
		case (PH_DEACTIVATE | REQUEST):
			l1_msg_b(st, pr, arg);
			break;
		case (PH_DEACTIVATE | CONFIRM):
			test_and_clear_bit(BC_FLG_ACTIV, &st->l1.bcs->Flag);
			test_and_clear_bit(BC_FLG_BUSY, &st->l1.bcs->Flag);
			mode_hfcpci(st->l1.bcs, 0, st->l1.bc);
			st->l1.l1l2(st, PH_DEACTIVATE | CONFIRM, NULL);
			break;
	}
}

/******************************************/
/* deactivate B-channel access and queues */
/******************************************/
static void
close_hfcpci(struct BCState *bcs)
{
	mode_hfcpci(bcs, 0, bcs->channel);
	if (test_and_clear_bit(BC_FLG_INIT, &bcs->Flag)) {
		discard_queue(&bcs->rqueue);
		discard_queue(&bcs->squeue);
		if (bcs->tx_skb) {
			idev_kfree_skb(bcs->tx_skb, FREE_WRITE);
			bcs->tx_skb = NULL;
			test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
		}
	}
}

/*************************************/
/* init B-channel queues and control */
/*************************************/
static int
open_hfcpcistate(struct IsdnCardState *cs, struct BCState *bcs)
{
	if (!test_and_set_bit(BC_FLG_INIT, &bcs->Flag)) {
		skb_queue_head_init(&bcs->rqueue);
		skb_queue_head_init(&bcs->squeue);
	}
	bcs->tx_skb = NULL;
	test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
	bcs->event = 0;
	bcs->tx_cnt = 0;
	return (0);
}

/*********************************/
/* inits the stack for B-channel */
/*********************************/
static int
setstack_2b(struct PStack *st, struct BCState *bcs)
{
	bcs->channel = st->l1.bc;
	if (open_hfcpcistate(st->l1.hardware, bcs))
		return (-1);
	st->l1.bcs = bcs;
	st->l2.l2l1 = hfcpci_l2l1;
	setstack_manager(st);
	bcs->st = st;
	setstack_l1_B(st);
	return (0);
}

/***************************/
/* handle L1 state changes */
/***************************/
static void
hfcpci_bh(struct IsdnCardState *cs)
{
/*      struct PStack *stptr;
 */
	if (!cs)
		return;
#if 0
	if (test_and_clear_bit(D_CLEARBUSY, &cs->event)) {
		if (cs->debug)
			debugl1(cs, "D-Channel Busy cleared");
		stptr = cs->stlist;
		while (stptr != NULL) {
			stptr->l1.l1l2(stptr, PH_PAUSE | CONFIRM, NULL);
			stptr = stptr->next;
		}
	}
#endif
	if (test_and_clear_bit(D_L1STATECHANGE, &cs->event)) {
		switch (cs->dc.hfcpci.ph_state) {
			case (0):
				l1_msg(cs, HW_RESET | INDICATION, NULL);
				break;
			case (3):
				l1_msg(cs, HW_DEACTIVATE | INDICATION, NULL);
				break;
			case (8):
				l1_msg(cs, HW_RSYNC | INDICATION, NULL);
				break;
			case (6):
				l1_msg(cs, HW_INFO2 | INDICATION, NULL);
				break;
			case (7):
				l1_msg(cs, HW_INFO4_P8 | INDICATION, NULL);
				break;
			default:
				break;
		}
	}
	if (test_and_clear_bit(D_RCVBUFREADY, &cs->event))
		DChannel_proc_rcv(cs);
	if (test_and_clear_bit(D_XMTBUFREADY, &cs->event))
		DChannel_proc_xmt(cs);
}


/*************************************/
/* Alloc memory send data for queues */
/*************************************/
__initfunc(unsigned int
	   *init_send_hfcpci(int cnt))
{
	int i, *send;

	if (!(send = kmalloc(cnt * sizeof(unsigned int), GFP_ATOMIC))) {
		printk(KERN_WARNING
		       "HiSax: No memory for hfcpci.send\n");
		return (NULL);
	}
	for (i = 0; i < cnt; i++)
		send[i] = 0x1fff;
	return (send);
}

/********************************/
/* called for card init message */
/********************************/
__initfunc(void
	   inithfcpci(struct IsdnCardState *cs))
{
	cs->setstack_d = setstack_hfcpci;
	cs->dbusytimer.function = (void *) hfcpci_dbusy_timer;
	cs->dbusytimer.data = (long) cs;
	init_timer(&cs->dbusytimer);
	cs->tqueue.routine = (void *) (void *) hfcpci_bh;
#if 0
	if (!cs->hw.hfcpci.send)
		cs->hw.hfcpci.send = init_send_hfcpci(16);
#endif
	if (!cs->bcs[0].hw.hfc.send)
		cs->bcs[0].hw.hfc.send = init_send_hfcpci(32);
	if (!cs->bcs[1].hw.hfc.send)
		cs->bcs[1].hw.hfc.send = init_send_hfcpci(32);
	cs->BC_Send_Data = &hfcpci_send_data;
	cs->bcs[0].BC_SetStack = setstack_2b;
	cs->bcs[1].BC_SetStack = setstack_2b;
	cs->bcs[0].BC_Close = close_hfcpci;
	cs->bcs[1].BC_Close = close_hfcpci;
	mode_hfcpci(cs->bcs, 0, 0);
	mode_hfcpci(cs->bcs + 1, 0, 1);
}



/*******************************************/
/* handle card messages from control layer */
/*******************************************/
static int
hfcpci_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
	long flags;

	if (cs->debug & L1_DEB_ISAC)
		debugl1(cs, "HFCPCI: card_msg %x", mt);
	switch (mt) {
		case CARD_RESET:
			reset_hfcpci(cs);
			return (0);
		case CARD_RELEASE:
			release_io_hfcpci(cs);
			return (0);
		case CARD_INIT:
			inithfcpci(cs);
			save_flags(flags);
			sti();
			current->state = TASK_INTERRUPTIBLE;
			schedule_timeout((80 * HZ) / 1000);	/* Timeout 80ms */
			/* now switch timer interrupt off */
			cs->hw.hfcpci.int_m1 &= ~HFCPCI_INTS_TIMER;
			Write_hfc(cs, HFCPCI_INT_M1, cs->hw.hfcpci.int_m1);
			/* reinit mode reg */
			Write_hfc(cs, HFCPCI_MST_MODE, cs->hw.hfcpci.mst_m);
			restore_flags(flags);
			return (0);
		case CARD_TEST:
			return (0);
	}
	return (0);
}


/* this variable is used as card index when more than one cards are present */
#ifdef COMPAT_HAS_NEW_PCI
static struct pci_dev *dev_hfcpci __initdata = NULL;
#else
static int pci_index __initdata = 0;
#endif

#endif				/* CONFIG_PCI */

__initfunc(int
	   setup_hfcpci(struct IsdnCard *card))
{
	struct IsdnCardState *cs = card->cs;
	char tmp[64];
	int i;
#ifdef COMPAT_HAS_NEW_PCI
        struct pci_dev *tmp_hfcpci = NULL;
#endif

	strcpy(tmp, hfcpci_revision);
	printk(KERN_INFO "HiSax: HFC-PCI driver Rev. %s\n", HiSax_getrev(tmp));
#if CONFIG_PCI
	cs->hw.hfcpci.int_s1 = 0;
#if 0
	cs->hw.hfcpci.send = NULL;
#endif
	cs->bcs[0].hw.hfc.send = NULL;
	cs->bcs[1].hw.hfc.send = NULL;
	cs->dc.hfcpci.ph_state = 0;
	cs->hw.hfcpci.fifo = 255;
	if (cs->typ == ISDN_CTYPE_HFC_PCI) {
#ifdef COMPAT_HAS_NEW_PCI
		if (!pci_present()) {
			printk(KERN_ERR "HFC-PCI: no PCI bus present\n");
			return (0);
		}
		i = 0;
                while (CCD_VENDOR_IDS[i]) {
		  tmp_hfcpci = pci_find_device(CCD_VENDOR_IDS[i],
					       CCD_DEVICE_IDS[i],
					       dev_hfcpci);
		  if (tmp_hfcpci) break;
		  i++;
		}  
					      
		if (tmp_hfcpci) {
		        dev_hfcpci = tmp_hfcpci; /* old device */
			cs->hw.hfcpci.pci_bus = dev_hfcpci->bus->number;
			cs->hw.hfcpci.pci_device_fn = dev_hfcpci->devfn;
			cs->irq = dev_hfcpci->irq;
			if (!cs->irq) {
				printk(KERN_WARNING "HFC-PCI: No IRQ for PCI card found\n");
				return (0);
			}
			cs->hw.hfcpci.pci_io = (char *) get_pcibase(dev_hfcpci, 1);
		} else {
			printk(KERN_WARNING "HFC-PCI: No PCI card found\n");
			return (0);
		}
#else
		for (; pci_index < 255; pci_index++) {
			unsigned char irq;

			i = 0;
                        while (CCD_VENDOR_IDS[i]) {
			  if (pcibios_find_device(CCD_VENDOR_IDS[i],
						  CCD_DEVICE_IDS[i], pci_index,
						  &cs->hw.hfcpci.pci_bus, &cs->hw.hfcpci.pci_device_fn) == 0) 
			    break;
			  i++;
			}
			if (!CCD_VENDOR_IDS[i]) 
			  continue;

			pcibios_read_config_byte(cs->hw.hfcpci.pci_bus, cs->hw.hfcpci.pci_device_fn,
					       PCI_INTERRUPT_LINE, &irq);
			cs->irq = irq;

			pcibios_read_config_dword(cs->hw.hfcpci.pci_bus,
				cs->hw.hfcpci.pci_device_fn, PCI_BASE_ADDRESS_1,
				(void *) &cs->hw.hfcpci.pci_io);
			break;
		}
		if (pci_index == 255) {
			printk(KERN_WARNING "HFC-PCI: No card found\n");
			return (0);
		}
		pci_index++;
#endif				/* COMPAT_HAS_NEW_PCI */
		if (!cs->hw.hfcpci.pci_io) {
			printk(KERN_WARNING "HFC-PCI: No IO-Mem for PCI card found\n");
			return (0);
		}
		/* Allocate memory for FIFOS */
		/* Because the HFC-PCI needs a 32K physical alignment, we */
		/* need to allocate the double mem and align the address */
		if (!((void *) cs->hw.hfcpci.share_start = kmalloc(65536, GFP_KERNEL))) {
			printk(KERN_WARNING "HFC-PCI: Error allocating memory for FIFO!\n");
			return 0;
		}
		(ulong) cs->hw.hfcpci.fifos =
		    (((ulong) cs->hw.hfcpci.share_start) & ~0x7FFF) + 0x8000;
		pcibios_write_config_dword(cs->hw.hfcpci.pci_bus,
				       cs->hw.hfcpci.pci_device_fn, 0x80,
					   (u_int) virt_to_bus(cs->hw.hfcpci.fifos));
		cs->hw.hfcpci.pci_io = ioremap((ulong) cs->hw.hfcpci.pci_io, 256);
		printk(KERN_INFO
		 "HFC-PCI: defined at mem %#x fifo %#x(%#x) IRQ %d HZ %d\n",
		       (u_int) cs->hw.hfcpci.pci_io,
		       (u_int) cs->hw.hfcpci.fifos,
		       (u_int) virt_to_bus(cs->hw.hfcpci.fifos),
		       cs->irq, HZ);
		pcibios_write_config_word(cs->hw.hfcpci.pci_bus, cs->hw.hfcpci.pci_device_fn, PCI_COMMAND, PCI_ENA_MEMIO);	/* enable memory mapped ports, disable busmaster */
		cs->hw.hfcpci.int_m2 = 0; /* disable alle interrupts */
		cs->hw.hfcpci.int_m1 = 0;
		Write_hfc(cs, HFCPCI_INT_M1, cs->hw.hfcpci.int_m1);
		Write_hfc(cs, HFCPCI_INT_M2, cs->hw.hfcpci.int_m2);
		/* At this point the needed PCI config is done */
		/* fifos are still not enabled */
	} else
		return (0);	/* no valid card type */


	cs->readisac = NULL;
	cs->writeisac = NULL;
	cs->readisacfifo = NULL;
	cs->writeisacfifo = NULL;
	cs->BC_Read_Reg = NULL;
	cs->BC_Write_Reg = NULL;
	cs->irq_func = &hfcpci_interrupt;
	cs->irq_flags |= SA_SHIRQ;

	cs->hw.hfcpci.timer.function = (void *) hfcpci_Timer;
	cs->hw.hfcpci.timer.data = (long) cs;
	init_timer(&cs->hw.hfcpci.timer);

	reset_hfcpci(cs);
	cs->cardmsg = &hfcpci_card_msg;
	return (1);
#else
	printk(KERN_WARNING "HFC-PCI: NO_PCI_BIOS\n");
	return (0);
#endif				/* CONFIG_PCI */
}
