/* $Id: isdn_ppp.c,v 1.14 1996/08/12 16:26:47 hipp Exp $
 *
 * Linux ISDN subsystem, functions for synchronous PPP (linklevel).
 *
 * Copyright 1995,96 by Michael Hipp (Michael.Hipp@student.uni-tuebingen.de)
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
 * $Log: isdn_ppp.c,v $
 * Revision 1.14  1996/08/12 16:26:47  hipp
 * code cleanup
 * changed connection management from minors to slots
 *
 * Revision 1.13  1996/07/01 19:47:24  hipp
 * Fixed memory leak in VJ handling and more VJ changes
 *
 * Revision 1.12  1996/06/24 17:42:03  fritz
 * Minor bugfixes.
 *
 * Revision 1.11  1996/06/16 17:46:05  tsbogend
 * changed unsigned long to u32 to make Alpha people happy
 *
 * Revision 1.10  1996/06/11 14:50:29  hipp
 * Lot of changes and bugfixes.
 * New scheme to resend packets to busy LL devices.
 *
 * Revision 1.9  1996/05/18 01:37:01  fritz
 * Added spelling corrections and some minor changes
 * to stay in sync with kernel.
 *
 * Revision 1.8  1996/05/06 11:34:55  hipp
 * fixed a few bugs
 *
 * Revision 1.7  1996/04/30 11:07:42  fritz
 * Added Michael's ippp-bind patch.
 *
 * Revision 1.6  1996/04/30 09:33:09  fritz
 * Removed compatibility-macros.
 *
 * Revision 1.5  1996/04/20 16:32:32  fritz
 * Changed ippp_table to an array of pointers, allocating each part
 * separately.
 *
 * Revision 1.4  1996/02/19 15:25:50  fritz
 * Bugfix: Sync-PPP packets got compressed twice, when resent due to
 * send-queue-full reject.
 *
 * Revision 1.3  1996/02/11 02:27:12  fritz
 * Lot of Bugfixes my Michael.
 * Moved calls to skb_push() into isdn_net_header()
 * Fixed a possible race-condition in isdn_ppp_timer_timeout().
 *
 * Revision 1.2  1996/01/22 05:08:06  fritz
 * Merged in Michael's patches for MP.
 * Minor changes in isdn_ppp_xmit.
 *
 * Revision 1.1  1996/01/09 04:11:29  fritz
 * Initial revision
 *
 */

/* TODO: right tbusy handling when using MP */

#include <linux/config.h>
#define __NO_VERSION__
#include <linux/module.h>
#include <linux/isdn.h>
#include "isdn_common.h"
#include "isdn_ppp.h"
#include "isdn_net.h"

#ifndef PPP_IPX
#define PPP_IPX 0x002b 
#endif
 
/* Prototypes */
static int isdn_ppp_fill_rq(unsigned char *buf, int len,int proto, int slot);
static int isdn_ppp_closewait(int slot);
static void isdn_ppp_push_higher(isdn_net_dev * net_dev, isdn_net_local * lp,
			struct sk_buff *skb, int proto);
static int isdn_ppp_if_get_unit(char *namebuf);

#ifdef CONFIG_ISDN_MPP
static int isdn_ppp_bundle(struct ippp_struct *, int unit);
static void isdn_ppp_mask_queue(isdn_net_dev * dev, long mask);
static void isdn_ppp_cleanup_queue(isdn_net_dev * dev, long min);
static int isdn_ppp_fill_mpqueue(isdn_net_dev *, struct sk_buff **skb,
		int BEbyte, int *sqno, int min_sqno);
#endif

char *isdn_ppp_revision              = "$Revision: 1.14 $";
struct ippp_struct *ippp_table[ISDN_MAX_CHANNELS];

extern int isdn_net_force_dial_lp(isdn_net_local *);

/*
 * unbind isdn_net_local <=> ippp-device 
 * note: it can happen, that we hangup/free the master before the slaves
 */
int isdn_ppp_free(isdn_net_local *lp)
{
	isdn_net_local *master_lp=lp;
	unsigned long flags;
	struct ippp_struct *is;

	if (lp->ppp_slot < 0 || lp->ppp_slot > ISDN_MAX_CHANNELS)
		return 0;

	is = ippp_table[lp->ppp_slot];

	save_flags(flags);
	cli();
#ifdef CONFIG_ISDN_MPP
	if(lp->master)
		master_lp = (isdn_net_local *) lp->master->priv;

	lp->last->next = lp->next;
	lp->next->last = lp->last;
	if(master_lp->netdev->queue == lp) {
		master_lp->netdev->queue = lp->next;
		if(lp->next == lp) {	/* last link in queue? */
               		master_lp->netdev->ib.bundled = 0;
			isdn_ppp_free_mpqueue(master_lp->netdev);
			isdn_ppp_free_sqqueue(master_lp->netdev);
		}
	}
	lp->next = lp->last = lp;	/* (re)set own pointers */
#endif

	isdn_ppp_closewait(lp->ppp_slot);	/* force wakeup on ippp device */

	if(is->debug & 0x1)
		printk(KERN_DEBUG "isdn_ppp_free %d %lx %lx\n", lp->ppp_slot, (long) lp,(long) is->lp);

	is->lp = NULL;	/* link is down .. set lp to NULL */
	lp->ppp_slot = -1;			/* is this OK ?? */
	restore_flags(flags);

	return 0;
}

/*
 * bind isdn_net_local <=> ippp-device
 */
int isdn_ppp_bind(isdn_net_local * lp)
{
	int i;
	int unit = 0;
	long flags;
	struct ippp_struct *is;

	if (lp->p_encap != ISDN_NET_ENCAP_SYNCPPP)
		return -1;

	save_flags(flags);
	cli();

	if(lp->pppbind < 0)	/* device bounded to ippp device ? */
	{
 		isdn_net_dev *net_dev = dev->netdev;
		char exclusive[ISDN_MAX_CHANNELS];	/* exclusive flags */
		memset(exclusive,0,ISDN_MAX_CHANNELS);
		while (net_dev) {	/* step through net devices to find exclusive minors */
			isdn_net_local *lp = &net_dev->local;
			if(lp->pppbind >= 0)
				exclusive[lp->pppbind] = 1;
			net_dev = net_dev->next;
		}
		/*
		 * search a free device / slot
		 */
		for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
			if (ippp_table[i]->state == IPPP_OPEN && !exclusive[ippp_table[i]->minor]) { /* OPEN, but not connected! */
				break;
			}
		}
	}
	else {
		for(i=0;i<ISDN_MAX_CHANNELS;i++)
			if(ippp_table[i]->minor == lp->pppbind && ippp_table[i]->state == IPPP_OPEN)
				break;
	}

	if (i >= ISDN_MAX_CHANNELS) {
		restore_flags(flags);
		printk(KERN_WARNING "isdn_ppp_bind: Can't find usable ippp device.\n");
		return -1;
	}

	unit = isdn_ppp_if_get_unit(lp->name); /* get unit number from interface name .. ugly! */
	if(unit < 0) {
		printk(KERN_ERR "isdn_ppp_bind: illegal interface name %s.\n",lp->name);
		return -1;
	}

	lp->ppp_slot = i;
	is = ippp_table[i];
	is->lp = lp;
	is->unit = unit;
	is->state = IPPP_OPEN | IPPP_CONNECT | IPPP_NOBLOCK;

	restore_flags(flags);

	return lp->ppp_slot;
}

/*
 * kick the ipppd on the device
 * (wakes up daemon after B-channel connect)
 */

void isdn_ppp_wakeup_daemon(isdn_net_local *lp)
{
	if (ippp_table[lp->ppp_slot]->wq)
		wake_up_interruptible(&ippp_table[lp->ppp_slot]->wq);
}

/*
 * there was a hangup on the netdevice
 * force wakeup of the ippp device 
 * go into 'device waits for release' state
 */
static int isdn_ppp_closewait(int slot)
{
	struct ippp_struct *is;

	if (slot < 0 || slot >= ISDN_MAX_CHANNELS)
		return 0;
	is = ippp_table[slot];

	if (is->state && is->wq)
		wake_up_interruptible(&is->wq);

	is->state = IPPP_CLOSEWAIT;
	return 1;
}

/*
 * isdn_ppp_find_slot / isdn_ppp_free_slot
 */

static int isdn_ppp_get_slot(void)
{
	int i;
	for(i=0;i<ISDN_MAX_CHANNELS;i++) {
		if(!ippp_table[i]->state)
			return i;
	}
	return -1;
}

/*
 * isdn_ppp_open 
 */

int isdn_ppp_open(int min, struct file *file)
{
	int slot;
	struct ippp_struct *is;

	slot = isdn_ppp_get_slot();
	if(slot < 0) {
		return -EBUSY;
	}
	is = file->private_data = ippp_table[slot];

	if(is->debug & 0x1)
		printk(KERN_DEBUG "ippp, open, slot: %d, minor: %d, state: %04x\n",slot, min,is->state);

	is->lp = 0;
	is->mp_seqno = 0;  /* MP sequence number */
	is->pppcfg = 0;    /* ppp configuration */
	is->mpppcfg = 0;   /* mppp configuration */
	is->range = 0x1000000;	/* MP: 24 bit range */
	is->last_link_seqno = -1;	/* MP: maybe set to Bundle-MIN, when joining a bundle ?? */
	is->unit = -1;	/* set, when we have our interface */
	is->mru = 1524;	/* MRU, default 1524 */
	is->maxcid = 16;	/* VJ: maxcid */
	is->tk = current;
	is->wq = NULL;    /* read() wait queue */
	is->wq1 = NULL;   /* select() wait queue */
	is->first = is->rq + NUM_RCV_BUFFS - 1; /* receive queue */
	is->last = is->rq;
	is->minor = min;
#ifdef CONFIG_ISDN_PPP_VJ
        /*
         * VJ header compression init
         */
	is->slcomp = slhc_init(16, 16);	/* not necessary for 2. link in bundle */
#endif

	is->state = IPPP_OPEN;

	return 0;
}

/*
 * release ippp device
 */
void isdn_ppp_release(int min, struct file *file)
{
	int i;
	struct ippp_struct *is;

	if (min < 0 || min >= ISDN_MAX_CHANNELS)
		return;
	is = file->private_data;

	if(is->debug & 0x1)
		printk(KERN_DEBUG "ippp: release, minor: %d %lx\n", min, (long) is->lp);

	if (is->lp) {	/* a lp address says: this link is still up */
		isdn_net_dev *p = dev->netdev;
		p = is->lp->netdev;
		is->lp->ppp_slot = -1;
		isdn_net_hangup(&p->dev); /* lp->ppp_slot==-1 => no calling of isdn_ppp_closewait() */
		is->lp = NULL;
	}
	for (i = 0; i < NUM_RCV_BUFFS; i++) {
		if (is->rq[i].buf) {
			kfree(is->rq[i].buf);
			is->rq[i].buf = NULL;
		}
	}
        is->first = is->rq + NUM_RCV_BUFFS - 1; /* receive queue */
        is->last = is->rq;

#ifdef CONFIG_ISDN_PPP_VJ
	slhc_free(is->slcomp);
	is->slcomp = NULL;
#endif

	is->state = 0;
}

/*
 * get_arg .. ioctl helper
 */
static int get_arg(void *b, unsigned long *val)
{
	int r;
	if ((r = verify_area(VERIFY_READ, (void *) b, sizeof(unsigned long))))
		 return r;
	memcpy_fromfs((void *) val, b, sizeof(unsigned long));
	return 0;
}

/*
 * set arg .. ioctl helper
 */
static int set_arg(void *b, unsigned long val)
{
	int r;
	if ((r = verify_area(VERIFY_WRITE, b, sizeof(unsigned long))))
		 return r;
	memcpy_tofs(b, (void *) &val, sizeof(unsigned long));
	return 0;
}

/*
 * ippp device ioctl 
 */
int isdn_ppp_ioctl(int min, struct file *file, unsigned int cmd, unsigned long arg)
{
	unsigned long val;
	int r;
	struct ippp_struct *is;

	is = file->private_data;

	if(is->debug & 0x1)
		printk(KERN_DEBUG "isdn_ppp_ioctl: minor: %d cmd: %x state: %x\n",min,cmd,is->state);

	if (!(is->state & IPPP_OPEN))
		return -EINVAL;

	switch (cmd) {
	case PPPIOCBUNDLE:
#ifdef CONFIG_ISDN_MPP
		if ((r = get_arg((void *) arg, &val)))
			return r;
		printk(KERN_DEBUG "iPPP-bundle: minor: %d, slave unit: %d, master unit: %d\n",
                        (int) min, (int) is->unit, (int) val);
		return isdn_ppp_bundle(is, val);
#else
		return -1;
#endif
		break;
	case PPPIOCGUNIT:	/* get ppp/isdn unit number */
		if ((r = set_arg((void *) arg, is->unit)))
			return r;
		break;
	case PPPIOCGMPFLAGS:	/* get configuration flags */
		if ((r = set_arg((void *) arg, is->mpppcfg)))
			return r;
		break;
	case PPPIOCSMPFLAGS:	/* set configuration flags */
		if ((r = get_arg((void *) arg, &val)))
			return r;
		is->mpppcfg = val;
		break;
	case PPPIOCGFLAGS:	/* get configuration flags */
		if ((r = set_arg((void *) arg, is->pppcfg)))
			return r;
		break;
	case PPPIOCSFLAGS:	/* set configuration flags */
		if ((r = get_arg((void *) arg, &val))) {
			return r;
		}
		if (val & SC_ENABLE_IP && !(is->pppcfg & SC_ENABLE_IP)) {
			isdn_net_local *lp = is->lp;
			lp->netdev->dev.tbusy = 0;
			mark_bh(NET_BH); /* OK .. we are ready to send buffers */
		}
		is->pppcfg = val;
		break;
#if 0
	case PPPIOCGSTAT:	/* read PPP statistic information */
		break;
	case PPPIOCGTIME:	/* read time delta information */
		break;
#endif
	case PPPIOCSMRU:	/* set receive unit size for PPP */
		if ((r = get_arg((void *) arg, &val)))
			return r;
		is->mru = val;
		break;
	case PPPIOCSMPMRU:
		break;
	case PPPIOCSMPMTU:
		break;
	case PPPIOCSMAXCID:	/* set the maximum compression slot id */
		if ((r = get_arg((void *) arg, &val)))
			return r;
		val++;
		if(is->maxcid != val) {
#ifdef CONFIG_ISDN_PPP_VJ
			struct slcompress *sltmp;
#endif
			if(is->debug & 0x1)
				printk(KERN_DEBUG "ippp, ioctl: changed MAXCID to %ld\n",val);
			is->maxcid = val;
#ifdef CONFIG_ISDN_PPP_VJ
			sltmp = slhc_init(16,val);
			if(!sltmp) {
				printk(KERN_ERR "ippp, can't realloc slhc struct\n");
				return -ENOMEM;
			}	
			if(is->slcomp)
				slhc_free(is->slcomp);
			is->slcomp = sltmp;
#endif
		}
		break;
	case PPPIOCGDEBUG:
		if ((r = set_arg((void *) arg, is->debug)))
			return r;
		break;
	case PPPIOCSDEBUG:
		if ((r = get_arg((void *) arg, &val)))
			return r;
		is->debug = val;
		break;
	default:
		break;
	}
	return 0;
}

int isdn_ppp_select(int min, struct file *file, int type, select_table * st)
{
	struct ippp_buf_queue *bf, *bl;
	unsigned long flags;
	struct ippp_struct *is;

	is = file->private_data;

	if(is->debug & 0x2)
		printk(KERN_DEBUG "isdn_ppp_select: minor: %d, type: %d \n",min,type);

	if (!(is->state & IPPP_OPEN))
		return -EINVAL;

	switch (type) {
	case SEL_IN:
		save_flags(flags);
		cli();
		bl = is->last;
		bf = is->first;
		/* 
		 * if IPPP_NOBLOCK is set we return even if we have nothing to read 
		 */
		if (bf->next == bl && !(is->state & IPPP_NOBLOCK)) {
			select_wait(&is->wq, st);
			restore_flags(flags);
			return 0;
		}
		is->state &= ~IPPP_NOBLOCK;
		restore_flags(flags);
		return 1;
	case SEL_OUT:
                /* we're always ready to send .. */
		return 1;
	case SEL_EX:
		select_wait(&is->wq1, st);
		return 0;
	}
	return 1;
}

/*
 *  fill up isdn_ppp_read() queue ..
 */

static int isdn_ppp_fill_rq(unsigned char *buf, int len,int proto, int slot)
{
	struct ippp_buf_queue *bf, *bl;
	unsigned long flags;
	unsigned char *nbuf;
	struct ippp_struct *is;

	if (slot < 0 || slot >= ISDN_MAX_CHANNELS) {
		printk(KERN_WARNING "ippp: illegal slot.\n");
		return 0;
	}
	is = ippp_table[slot];

	if (!(is->state & IPPP_CONNECT)) {
		printk(KERN_DEBUG "ippp: device not activated.\n");
		return 0;
	}

	nbuf = (unsigned char *) kmalloc(len+4, GFP_ATOMIC);
	if(!nbuf) {
		printk(KERN_WARNING "ippp: Can't alloc buf\n");
		return 0;
	}
	nbuf[0] = PPP_ALLSTATIONS;
	nbuf[1] = PPP_UI;
	nbuf[2] = proto >> 8;
	nbuf[3] = proto & 0xff;
	memcpy(nbuf+4, buf, len);

	save_flags(flags);
	cli();

	bf = is->first;
	bl = is->last;

	if (bf == bl) {
		printk(KERN_WARNING "ippp: Queue is full; discarding first buffer\n");
		bf = bf->next;
		kfree(bf->buf);
		is->first = bf;
	}
	bl->buf = (char *) nbuf;
	bl->len = len+4;

	is->last = bl->next;
	restore_flags(flags);

	if (is->wq)
		wake_up_interruptible(&is->wq);

	return len;
}

/*
 * read() .. non-blocking: ipppd calls it only after select()
 *           reports, that there is data
 */

int isdn_ppp_read(int min, struct file *file, char *buf, int count)
{
	struct ippp_struct *is;
	struct ippp_buf_queue *b;
	int r;
	unsigned long flags;

	is = file->private_data;

	if (!(is->state & IPPP_OPEN))
		return 0;

	if ((r = verify_area(VERIFY_WRITE, (void *) buf, count)))
		return r;

	save_flags(flags);
	cli();

	b = is->first->next;
	if (!b->buf) {
		restore_flags(flags);
		return -EAGAIN;
	}
	if (b->len < count)
		count = b->len;
	memcpy_tofs(buf, b->buf, count);
	kfree(b->buf);
	b->buf = NULL;
	is->first = b;
	restore_flags(flags);

	return count;
}

/*
 * ipppd wanna write a packet to the card .. non-blocking
 */
 
int isdn_ppp_write(int min, struct file *file,  const char *buf, int count)
{
	isdn_net_local *lp;
	struct ippp_struct *is;

	is = file->private_data;

	if (!(is->state & IPPP_CONNECT))
		return 0;

	lp = is->lp;

	/* -> push it directly to the lowlevel interface */

	if (!lp)
		printk(KERN_DEBUG "isdn_ppp_write: lp == NULL\n");
	else {
		lp->huptimer = 0;
		if (lp->isdn_device < 0 || lp->isdn_channel < 0)
			return 0;

		if (dev->drv[lp->isdn_device]->running && lp->dialstate == 0 &&
		    (lp->flags & ISDN_NET_CONNECTED)) {
			struct sk_buff *skb;
			skb = dev_alloc_skb(count);
			if(!skb) {
				printk(KERN_WARNING "isdn_ppp_write: out of memory!\n");
				return count;
			}
			skb->free = 1;
			memcpy_fromfs(skb_put(skb, count), buf, count);
			if(isdn_writebuf_skb_stub(lp->isdn_device,lp->isdn_channel,skb) != count) {
				if(lp->sav_skb) {
					dev_kfree_skb(lp->sav_skb,FREE_WRITE);
					printk(KERN_INFO "isdn_ppp_write: freeing sav_skb!\n");
				}
				lp->sav_skb = skb;
			}
		}
	}

	return count;
}

/*
 * init memory, structures etc. 
 */

int isdn_ppp_init(void)
{
	int i, j;

	for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
	        if (!(ippp_table[i] = (struct ippp_struct *)
	                kmalloc(sizeof(struct ippp_struct), GFP_KERNEL))) {
		        printk(KERN_WARNING "isdn_ppp_init: Could not alloc ippp_table\n");
			for (j = 0; j < i; j++)
				kfree(ippp_table[i]);
		        return -1;
	        }
		memset((char *) ippp_table[i], 0, sizeof(struct ippp_struct));
		ippp_table[i]->state = 0;
		ippp_table[i]->first = ippp_table[i]->rq + NUM_RCV_BUFFS - 1;
		ippp_table[i]->last = ippp_table[i]->rq;

		for (j = 0; j < NUM_RCV_BUFFS; j++) {
			ippp_table[i]->rq[j].buf = NULL;
			ippp_table[i]->rq[j].last = ippp_table[i]->rq +
			    (NUM_RCV_BUFFS + j - 1) % NUM_RCV_BUFFS;
			ippp_table[i]->rq[j].next = ippp_table[i]->rq + (j + 1) % NUM_RCV_BUFFS;
		}
	}
	return 0;
}

void isdn_ppp_cleanup(void)
{
        int i;

        for (i = 0; i < ISDN_MAX_CHANNELS; i++)
                kfree(ippp_table[i]);
}

/*
 * handler for incoming packets on a syncPPP interface
 */

void isdn_ppp_receive(isdn_net_dev * net_dev, isdn_net_local * lp, struct sk_buff *skb)
{
	struct ippp_struct *is;
	is = ippp_table[lp->ppp_slot];

	if(is->debug & 0x4)
		printk(KERN_DEBUG "recv skb, len: %ld\n",skb->len);

	if(net_dev->local.master) {
		printk(KERN_WARNING "isdn_ppp_receice: net_dev != master\n");
		net_dev = ((isdn_net_local*) net_dev->local.master->priv)->netdev;
	}

	if(skb->data[0] == 0xff && skb->data[1] == 0x03)
		skb_pull(skb,2);
	else if (is->pppcfg & SC_REJ_COMP_AC) {
		skb->free = 1;
		dev_kfree_skb(skb,0 /* FREE_READ */ );
		return;		/* discard it silently */
	}

#ifdef CONFIG_ISDN_MPP
	if (!(is->mpppcfg & SC_REJ_MP_PROT)) {
		int proto;
		int sqno_end;
		if (skb->data[0] & 0x1) {
			proto = skb->data[0];
			skb_pull(skb,1);	/* protocol ID is only 8 bit */
		} else {
			proto = ((int) skb->data[0] << 8) + skb->data[1];
			skb_pull(skb,2);
		}
		if (proto == PPP_MP) {
			isdn_net_local *lpq;
			int sqno, min_sqno, tseq;
			u_char BEbyte = skb->data[0];
			if(is->debug & 0x8)
	 			printk(KERN_DEBUG "recv: %d/%04x/%d -> %02x %02x %02x %02x %02x %02x\n", lp->ppp_slot, proto ,
					(int) skb->len, (int) skb->data[0], (int) skb->data[1], (int) skb->data[2], 
					(int) skb->data[3], (int) skb->data[4], (int) skb->data[5]);
			if (!(is->mpppcfg & SC_IN_SHORT_SEQ)) {
				sqno = ((int) skb->data[1] << 16) + ((int) skb->data[2] << 8) + (int) skb->data[3];
				skb_pull(skb,4);
			} else {
				sqno = (((int) skb->data[0] & 0xf) << 8) + (int) skb->data[1];
				skb_pull(skb,2);
			}

			if ((tseq = is->last_link_seqno) >= sqno) {
				int range = is->range;
				if (tseq + 1024 < range + sqno) /* redundancy check .. not MP conform */
					printk(KERN_WARNING "isdn_ppp_receive, MP, detected overflow with sqno: %d, last: %d !!!\n", sqno, tseq);
				else {
					sqno += range;
					is->last_link_seqno = sqno;
				}
			} else
				is->last_link_seqno = sqno;

			for (min_sqno = 0, lpq = net_dev->queue;;) {
				if (ippp_table[lpq->ppp_slot]->last_link_seqno > min_sqno)
					min_sqno = ippp_table[lpq->ppp_slot]->last_link_seqno;
				lpq = lpq->next;
				if (lpq == net_dev->queue)
					break;
			}
			if (min_sqno >= ippp_table[lpq->ppp_slot]->range) {	/* OK, every link overflowed */
				int mask = ippp_table[lpq->ppp_slot]->range - 1;	/* range is a power of 2 */
				isdn_ppp_cleanup_queue(net_dev, min_sqno);
				isdn_ppp_mask_queue(net_dev, mask);
				net_dev->ib.next_num &= mask;
				{
					struct sqqueue *q = net_dev->ib.sq;
					while (q) {
						q->sqno_start &= mask;
						q->sqno_end &= mask;
					}
				}
				min_sqno &= mask;
				for (lpq = net_dev->queue;;) {
					ippp_table[lpq->ppp_slot]->last_link_seqno &= mask;
					lpq = lpq->next;
					if (lpq == net_dev->queue)
						break;
				}
			}
			if ((BEbyte & (MP_BEGIN_FRAG | MP_END_FRAG)) != (MP_BEGIN_FRAG | MP_END_FRAG)) {
				printk(KERN_DEBUG "ippp: trying ;) to fill mp_queue %d .. UNTESTED!!\n", lp->ppp_slot);
				if ((sqno_end = isdn_ppp_fill_mpqueue(net_dev, &skb , BEbyte, &sqno, min_sqno)) < 0)
					return;		/* no packet complete */
			} else
				sqno_end = sqno;

			/*
			 * MP buffer management .. reorders incoming packets ..
			 * lotsa mem-copies and not heavily tested.
			 *
			 * first check whether there is more than one link in the bundle
			 * then check whether the number is in order
			 */
			net_dev->ib.modify = 1;		/* block timeout-timer */
			if (net_dev->ib.bundled && net_dev->ib.next_num != sqno) {
				/*
				 * packet is not 'in order'
				 */
				struct sqqueue *q;

				q = (struct sqqueue *) kmalloc(sizeof(struct sqqueue), GFP_ATOMIC);
				if (!q) {
					net_dev->ib.modify = 0;
					printk(KERN_WARNING "ippp/MPPP: Bad! Can't alloc sq node!\n");
					skb->free = 1;
					dev_kfree_skb(skb, 0 /* FREE_READ */ );
					return;		/* discard */
				}
				q->skb = skb;
				q->sqno_end = sqno_end;
				q->sqno_start = sqno;
				q->timer = jiffies + (ISDN_TIMER_1SEC) * 5;	/* timeout after 5 seconds */

				if (!net_dev->ib.sq) {
					net_dev->ib.sq = q;
					q->next = NULL;
				} else {
					struct sqqueue *ql = net_dev->ib.sq;
					if (ql->sqno_start > q->sqno_start) {
						q->next = ql;
						net_dev->ib.sq = q;
					} else {
						while (ql->next && ql->next->sqno_start < q->sqno_start)
							ql = ql->next;
						q->next = ql->next;
						ql->next = q;
					}
				}
				net_dev->ib.modify = 0;
				return;
			} else {
				/* 
			 	 * packet was 'in order' .. push it higher
				 */
				struct sqqueue *q;

				net_dev->ib.next_num = sqno_end + 1;
				isdn_ppp_push_higher(net_dev, lp, skb, -1);

                                /*
                                 * check queue, whether we have still buffered the next packet(s)
                                 */
				while ((q = net_dev->ib.sq) && q->sqno_start == net_dev->ib.next_num) {
					isdn_ppp_push_higher(net_dev, lp, q->skb, -1);
					net_dev->ib.sq = q->next;
					net_dev->ib.next_num = q->sqno_end + 1;
					kfree(q);
				}
			}
			net_dev->ib.modify = 0;

		} else
			isdn_ppp_push_higher(net_dev, lp, skb , proto);
	} else
#endif
		isdn_ppp_push_higher(net_dev, lp, skb , -1);
}


/*
 * push frame to higher layers
 * note: net_dev has to be master net_dev
 */
static void isdn_ppp_push_higher(isdn_net_dev *net_dev, isdn_net_local *lp, struct sk_buff *skb,int proto)
{
	struct device *dev = &net_dev->dev;
	struct ippp_struct *is = ippp_table[lp->ppp_slot];

	if (proto < 0) {	/* MP, oder normales Paket bei REJ_MP, MP Pakete gehen bei REJ zum pppd */
		if (skb->data[0] & 0x01) {	/* is it odd? */
			proto = (unsigned char) skb->data[0];
			skb_pull(skb,1);	/* protocol ID is only 8 bit */
		} else {
			proto = ((int) (unsigned char) skb->data[0] << 8) + (unsigned char) skb->data[1];
			skb_pull(skb,2);
		}
	}

	if(is->debug & 0x10)
		printk(KERN_DEBUG "push, skb %ld %04x\n",skb->len,proto);

	switch (proto) {
	case PPP_IPX: /* untested */
		if(is->debug & 0x20)
			printk(KERN_DEBUG "isdn_ppp: _IPX\n");
		skb->dev = dev;
		skb->mac.raw = skb->data;
		skb->protocol = htons(ETH_P_IPX);
		break;
#ifdef CONFIG_ISDN_PPP_VJ
	case PPP_VJC_UNCOMP:
		if(is->debug & 0x20)
			printk(KERN_DEBUG "isdn_ppp: VJC_UNCOMP\n");
		if(slhc_remember(ippp_table[net_dev->local.ppp_slot]->slcomp, skb->data, skb->len) <= 0) {
			printk(KERN_WARNING "isdn_ppp: received illegal VJC_UNCOMP frame!\n");
			net_dev->local.stats.rx_dropped++;
			skb->free = 1;
			dev_kfree_skb(skb,0 /* FREE_READ */ );
			return;
		}
#endif
	case PPP_IP:
		if(is->debug & 0x20)
			printk(KERN_DEBUG "isdn_ppp: IP\n");
		skb->dev = dev;
		skb->mac.raw = skb->data;
		skb->protocol = htons(ETH_P_IP);
		break;
	case PPP_VJC_COMP:
                if(is->debug & 0x20)
                        printk(KERN_DEBUG "isdn_ppp: VJC_COMP\n");
#ifdef CONFIG_ISDN_PPP_VJ
		{
			struct sk_buff *skb_old = skb;
			int pkt_len;
			skb = dev_alloc_skb(skb_old->len + 40);

			skb_old->free = 1;

			if (!skb) {
				printk(KERN_WARNING "%s: Memory squeeze, dropping packet.\n", dev->name);
				net_dev->local.stats.rx_dropped++;
				dev_kfree_skb(skb_old,0 /* FREE_READ */ );
				return;
			}
			skb->dev = dev;
			skb_put(skb,skb_old->len + 40);
			memcpy(skb->data, skb_old->data, skb_old->len);
			skb->mac.raw = skb->data;
			pkt_len = slhc_uncompress(ippp_table[net_dev->local.ppp_slot]->slcomp,
						  skb->data, skb_old->len);
			dev_kfree_skb(skb_old,0 /* FREE_READ */ );
			if(pkt_len < 0) {
				skb->free = 1;
				dev_kfree_skb(skb, 0 /* FREE_READ */ );
				lp->stats.rx_dropped++;
				return;
			}
			skb_trim(skb, pkt_len);
			skb->protocol = htons(ETH_P_IP);
		}
#else
		printk(KERN_INFO "isdn: Ooopsa .. VJ-Compression support not compiled into isdn driver.\n");
		lp->stats.rx_dropped++;
		skb->free = 1;
		dev_kfree_skb(skb,0 /* FREE_READ */ );
		return;
#endif
		break;
	default:
		isdn_ppp_fill_rq(skb->data, skb->len,proto, lp->ppp_slot);	/* push data to pppd device */
		skb->free = 1;
		dev_kfree_skb(skb,0 /* FREE_READ */ );
		return;
	}

	netif_rx(skb);
	net_dev->local.stats.rx_packets++;
	/* Reset hangup-timer */
	lp->huptimer = 0;

	return;
}

/*
 * send ppp frame .. we expect a PIDCOMPressable proto -- 
 *  (here: currently always PPP_IP,PPP_VJC_COMP,PPP_VJC_UNCOMP)
 *
 * VJ compression may change skb pointer!!! .. requeue with old
 * skb isn't allowed!! 
 */

static void isdn_ppp_skb_destructor(struct sk_buff *skb)
{
	char outstr[80],*outpnt=outstr;
	int i;

	*outpnt = 0;
	for(i=0;i<24 && i<skb->len;i++) {
		sprintf(outpnt,"%02x ",skb->data[i]);
		outpnt += 3;
	}
	printk(KERN_DEBUG "ippp_dstrct: %s\n",outstr);
}

int isdn_ppp_xmit(struct sk_buff *skb, struct device *dev)
{
	struct device *mdev = ((isdn_net_local *) (dev->priv) )->master;	/* get master (for redundancy) */
	isdn_net_local *lp,*mlp;
	isdn_net_dev *nd;
	int proto = PPP_IP;	/* 0x21 */
	struct ippp_struct *ipt,*ipts;

	if(mdev)
		mlp = (isdn_net_local *) (mdev->priv); 
	else
		mlp = (isdn_net_local *) (dev->priv);
	nd = mlp->netdev;	/* get master lp */
	ipts = ippp_table[mlp->ppp_slot];

	if (!(ipts->pppcfg & SC_ENABLE_IP)) {    /* PPP connected ? */
		if(ipts->debug & 0x1) {
			printk(KERN_INFO "%s: IP frame delayed.\n",dev->name);
			skb->destructor = isdn_ppp_skb_destructor;
		}
 		return 1;
        }

	skb->destructor = NULL;

	lp = nd->queue;		/* get lp on top of queue */
	if(lp->sav_skb) {	/* find a non-busy device */
		isdn_net_local *nlp = lp->next;
		while(lp->sav_skb) {
			if(lp == nlp)
				return 1;
			nlp = nd->queue = nd->queue->next;
		}
		lp = nlp;
	}
	ipt = ippp_table[lp->ppp_slot];

        lp->huptimer = 0;

	/*
	 * after this line .. requeueing in the device queue is no longer allowed!!! 
	 */

	if(ipt->debug & 0x4)
		printk(KERN_DEBUG  "xmit skb, len %ld\n",skb->len);

#ifdef CONFIG_ISDN_PPP_VJ
	if (ipts->pppcfg & SC_COMP_TCP) {	/* ipts here? probably yes .. but this check again */
		struct sk_buff *new_skb;

		new_skb = dev_alloc_skb(skb->len);
		if(new_skb) {
			u_char *buf;
			int pktlen;

			new_skb->dev = skb->dev;
			new_skb->free = 1;
			skb_put(new_skb,skb->len);
			buf = skb->data;

			pktlen = slhc_compress(ipts->slcomp, skb->data, skb->len, new_skb->data,
					&buf, !(ipts->pppcfg & SC_NO_TCP_CCID));

			if(buf != skb->data) { /* copied to new buffer ??? (btw: WHY must slhc copy it?? *sigh*)  */
				if(new_skb->data != buf)
					printk(KERN_ERR "isdn_ppp: FATAL error after slhc_compress!!\n");
				dev_kfree_skb(skb,FREE_WRITE);
				skb = new_skb;
			}
			else {
				dev_kfree_skb(new_skb,0 /* FREE_WRITE */ );
			}

			skb_trim(skb,pktlen);
			if (skb->data[0] & SL_TYPE_COMPRESSED_TCP) {	/* cslip? style -> PPP */
				proto = PPP_VJC_COMP;
				skb->data[0] ^= SL_TYPE_COMPRESSED_TCP;
			} else {
				if (skb->data[0] >= SL_TYPE_UNCOMPRESSED_TCP)
					proto = PPP_VJC_UNCOMP;
				skb->data[0] = (skb->data[0] & 0x0f) | 0x40;
			}
		}
	}
#endif

        if(ipt->debug & 0x24)
 		printk(KERN_DEBUG  "xmit2 skb, len %ld, proto %04x\n",skb->len,proto);

#ifdef CONFIG_ISDN_MPP
	if (ipt->mpppcfg & SC_MP_PROT) {
		/* we get mp_seqno from static isdn_net_local */
		long mp_seqno = ipts->mp_seqno;
		ipts->mp_seqno++;
		nd->queue = nd->queue->next;
		if (ipt->mpppcfg & SC_OUT_SHORT_SEQ) {
			skb_push(skb, 3);
			mp_seqno &= 0xfff;
			skb->data[0] = MP_BEGIN_FRAG | MP_END_FRAG | (mp_seqno >> 8);	/* (B)egin & (E)ndbit .. */
			skb->data[1] = mp_seqno & 0xff;
			skb->data[2] = proto;	/* PID compression */
		} else {
			skb_push(skb, 5);
			skb->data[0] = MP_BEGIN_FRAG | MP_END_FRAG;	/* (B)egin & (E)ndbit .. */
			skb->data[1] = (mp_seqno >> 16) & 0xff;	/* sequence number: 24bit */
			skb->data[2] = (mp_seqno >> 8) & 0xff;
			skb->data[3] = (mp_seqno >> 0) & 0xff;
			skb->data[4] = proto;	/* PID compression */
		}
		proto = PPP_MP; /* MP Protocol, 0x003d */
	}
#endif
	skb_push(skb,4);
	skb->data[0] = 0xff;        /* All Stations */
	skb->data[1] = 0x03;        /* Unnumbered information */
	skb->data[2] = proto >> 8;
	skb->data[3] = proto & 0xff;

        /* tx-stats are now updated via BSENT-callback */
	if(isdn_net_send_skb(dev , lp , skb)) {	
		if(lp->sav_skb) {	/* whole sav_skb processing with disabled IRQs ?? */
			printk(KERN_ERR "%s: whoops .. there is another stored skb!\n",dev->name);
			dev_kfree_skb(skb,FREE_WRITE);
		}
		else
			lp->sav_skb = skb;
	}
	return 0;
}

void isdn_ppp_free_sqqueue(isdn_net_dev * p) 
{
	struct sqqueue *q = p->ib.sq;

	p->ib.sq = NULL;
	while(q) {
		struct sqqueue *qn = q->next;
		if(q->skb) {
			q->skb->free = 1;
			dev_kfree_skb(q->skb,0 /* FREE_READ */ );
		}
		kfree(q);
		q = qn;
	}

}

void isdn_ppp_free_mpqueue(isdn_net_dev * p)
{
	struct mpqueue *ql, *q = p->mp_last;
	while (q) {
		ql = q->next;
		q->skb->free = 1;
 		dev_kfree_skb(q->skb,0 /* FREE_READ */ );
		kfree(q);
		q = ql;
	}
}

#ifdef CONFIG_ISDN_MPP

static int isdn_ppp_bundle(struct ippp_struct *is, int unit)
{
	char ifn[IFNAMSIZ + 1];
	long flags;
	isdn_net_dev *p;
	isdn_net_local *lp,*nlp;

	sprintf(ifn, "ippp%d", unit);
	p = isdn_net_findif(ifn);
	if (!p)
		return -1;

	isdn_timer_ctrl(ISDN_TIMER_IPPP, 1);	/* enable timer for ippp/MP */

	save_flags(flags);
	cli();

	nlp = is->lp;

	lp = p->queue;
	p->ib.bundled = 1;
	nlp->last = lp->last;
	lp->last->next = nlp;
	lp->last = nlp;
	nlp->next = lp;
	p->queue = nlp;

	ippp_table[nlp->ppp_slot]->unit = ippp_table[lp->ppp_slot]->unit;
/* maybe also SC_CCP stuff */
	ippp_table[nlp->ppp_slot]->pppcfg |= ippp_table[lp->ppp_slot]->pppcfg &
	    (SC_ENABLE_IP | SC_NO_TCP_CCID | SC_REJ_COMP_TCP);

	ippp_table[nlp->ppp_slot]->mpppcfg |= ippp_table[lp->ppp_slot]->mpppcfg &
	    (SC_MP_PROT | SC_REJ_MP_PROT | SC_OUT_SHORT_SEQ | SC_IN_SHORT_SEQ);
#if 0
	if (ippp_table[nlp->ppp_slot]->mpppcfg != ippp_table[lp->ppp_slot]->mpppcfg) {
		printk(KERN_WARNING "isdn_ppp_bundle: different MP options %04x and %04x\n",
		       ippp_table[nlp->ppp_slot]->mpppcfg, ippp_table[lp->ppp_slot]->mpppcfg);
	}
#endif

	restore_flags(flags);
	return 0;
}


static void isdn_ppp_mask_queue(isdn_net_dev * dev, long mask)
{
	struct mpqueue *q = dev->mp_last;
	while (q) {
		q->sqno &= mask;
		q = q->next;
	}
}


static int isdn_ppp_fill_mpqueue(isdn_net_dev * dev, struct sk_buff ** skb, int BEbyte, int *sqnop, int min_sqno)
{
	struct mpqueue *qe, *q1, *q;
	long cnt, flags;
	int pktlen, sqno_end;
	int sqno = *sqnop;

	q1 = (struct mpqueue *) kmalloc(sizeof(struct mpqueue), GFP_KERNEL);
	if (!q1) {
		printk(KERN_WARNING "isdn_ppp_fill_mpqueue: Can't alloc struct memory.\n");
		save_flags(flags);
		cli();
		isdn_ppp_cleanup_queue(dev, min_sqno);
		restore_flags(flags);
		return -1;
	}
	q1->skb = *skb;
	q1->sqno = sqno;
	q1->BEbyte = BEbyte;
	q1->time = jiffies;

	save_flags(flags);
	cli();

	if (!(q = dev->mp_last)) {
		dev->mp_last = q1;
		q1->next = NULL;
		q1->last = NULL;
		isdn_ppp_cleanup_queue(dev, min_sqno);	/* not necessary */
		restore_flags(flags);
		return -1;
	}
	for (;;) {		/* the faster way would be to step from the queue-end to the start */
		if (sqno > q->sqno) {
			if (q->next) {
				q = q->next;
				continue;
			}
			q->next = q1;
			q1->next = NULL;
			q1->last = q;
			break;
		}
		if (sqno == q->sqno)
			printk(KERN_WARNING "isdn_fill_mpqueue: illegal sqno received!!\n");
		q1->last = q->last;
		q1->next = q;
		if (q->last) {
			q->last->next = q1;
		} else
			dev->mp_last = q1;
		q->last = q1;
		break;
	}

/* now we check whether we completed a packet with this fragment */
	pktlen = -q1->skb->len;
	q = q1;
	cnt = q1->sqno;
	while (!(q->BEbyte & MP_END_FRAG)) {
		cnt++;
		if (!(q->next) || q->next->sqno != cnt) {
			isdn_ppp_cleanup_queue(dev, min_sqno);
			restore_flags(flags);
			return -1;
		}
		pktlen += q->skb->len;
		q = q->next;
	}
	pktlen += q->skb->len;
	qe = q;

	q = q1;
	cnt = q1->sqno;
	while (!(q->BEbyte & MP_BEGIN_FRAG)) {
		cnt--;
		if (!(q->last) || q->last->sqno != cnt) {
			isdn_ppp_cleanup_queue(dev, min_sqno);
			restore_flags(flags);
			return -1;
		}
		pktlen += q->skb->len;
		q = q->last;
	}
	pktlen += q->skb->len;

	if (q->last)
		q->last->next = qe->next;
	else
		dev->mp_last = qe->next;

	if (qe->next)
		qe->next->last = q->last;
	qe->next = NULL;
	sqno_end = qe->sqno;
	*sqnop = q->sqno;

	isdn_ppp_cleanup_queue(dev, min_sqno);
	restore_flags(flags);

	*skb = dev_alloc_skb(pktlen + 40); /* not needed: +40 for VJ compression .. */

	if (!(*skb)) {
		while (q) {
			struct mpqueue *ql = q->next;
			q->skb->free = 1;
			dev_kfree_skb(q->skb,0 /* FREE_READ */ );
			kfree(q);
			q = ql;
		}
		return -2;
	}
	cnt = 0;
	skb_put(*skb,pktlen);
	while (q) {
		struct mpqueue *ql = q->next;
		memcpy((*skb)->data + cnt, q->skb->data, q->skb->len);
		cnt += q->skb->len;
		q->skb->free = 1;
		dev_kfree_skb(q->skb,0 /* FREE_READ */ );
		kfree(q);
		q = ql;
	}

	return sqno_end;
}

/*
 * remove stale packets from list
 */

static void isdn_ppp_cleanup_queue(isdn_net_dev * dev, long min_sqno)
{
#ifdef CONFIG_ISDN_PPP_VJ
	int toss = 0;
#endif
/* z.z einfaches aussortieren gammeliger pakete. Fuer die Zukunft:
   eventuell, solange vorne kein B-paket ist und sqno<=min_sqno: auch rauswerfen
   wenn sqno<min_sqno und Luecken vorhanden sind: auch weg (die koennen nicht mehr gefuellt werden)
   bei paketen groesser min_sqno: ueber mp_mrru: wenn summe ueber pktlen der rumhaengenden Pakete 
   groesser als mrru ist: raus damit , Pakete muessen allerdings zusammenhaengen sonst koennte
   ja ein Paket mit B und eins mit E dazwischenpassen */

	struct mpqueue *ql, *q = dev->mp_last;
	while (q) {
		if (q->sqno < min_sqno) {
			if (q->BEbyte & MP_END_FRAG) {
				printk(KERN_DEBUG "ippp: freeing stale packet!\n");
				if ((dev->mp_last = q->next))
					q->next->last = NULL;
				while (q) {
					ql = q->last;
					q->skb->free = 1;
					dev_kfree_skb(q->skb,0 /* FREE_READ */ );
					kfree(q);
#ifdef CONFIG_ISDN_PPP_VJ
					toss = 1;
#endif
					q = ql;
				}
				q = dev->mp_last;
			} else
				q = q->next;
		} else
			break;
	}
#ifdef CONFIG_ISDN_PPP_VJ
	/* did we free a stale frame ? */
	if(toss)
		slhc_toss(ippp_table[dev->local.ppp_slot]->slcomp);
#endif
}

/*
 * a buffered packet timed-out?
 */

#endif

void isdn_ppp_timer_timeout(void)
{
#ifdef CONFIG_ISDN_MPP
	isdn_net_dev *net_dev = dev->netdev;
	struct sqqueue *q, *ql = NULL, *qn;

	while (net_dev) {
		isdn_net_local *lp = &net_dev->local;
		if (net_dev->ib.modify || lp->master)	{	/* interface locked or slave?*/
                        net_dev = net_dev->next;
			continue;
                }

		q = net_dev->ib.sq;
		while (q) {
			if (q->sqno_start == net_dev->ib.next_num || q->timer < jiffies) {

#ifdef CONFIG_ISDN_PPP_VJ
				/* did we step over a missing frame ? */
				if(q->sqno_start != net_dev->ib.next_num)
					slhc_toss(ippp_table[lp->ppp_slot]->slcomp);
#endif

				ql = net_dev->ib.sq;
				net_dev->ib.sq = q->next;
				net_dev->ib.next_num = q->sqno_end + 1;
				q->next = NULL;
				for (; ql;) {
					isdn_ppp_push_higher(net_dev, lp, ql->skb, -1);
					qn = ql->next;
					kfree(ql);
					ql = qn;
				}
				q = net_dev->ib.sq;
			} else
				q = q->next;
		}
		net_dev = net_dev->next;
	}
#endif
}

/*
 * network device ioctl handlers
 */

static int isdn_ppp_dev_ioctl_stats(int slot,struct ifreq *ifr,struct device *dev)
{
	struct ppp_stats *res, t;
	isdn_net_local *lp = (isdn_net_local *) dev->priv;
	int err;

        res = (struct ppp_stats *) ifr->ifr_ifru.ifru_data;
        err = verify_area (VERIFY_WRITE, res,sizeof(struct ppp_stats));

	if(err)
		return err;

	/* build a temporary stat struct and copy it to user space */

	memset (&t, 0, sizeof(struct ppp_stats));
	if(dev->flags & IFF_UP) {
		t.p.ppp_ipackets = lp->stats.rx_packets;
		t.p.ppp_ierrors = lp->stats.rx_errors;
		t.p.ppp_opackets = lp->stats.tx_packets;
		t.p.ppp_oerrors = lp->stats.tx_errors;
#ifdef CONFIG_ISDN_PPP_VJ
		if(slot >= 0 && ippp_table[slot]->slcomp) {
			struct slcompress *slcomp = ippp_table[slot]->slcomp;
			t.vj.vjs_packets  = slcomp->sls_o_compressed+slcomp->sls_o_uncompressed;
			t.vj.vjs_compressed = slcomp->sls_o_compressed;
			t.vj.vjs_searches = slcomp->sls_o_searches;
			t.vj.vjs_misses   = slcomp->sls_o_misses;
			t.vj.vjs_errorin  = slcomp->sls_i_error;
			t.vj.vjs_tossed   = slcomp->sls_i_tossed;
			t.vj.vjs_uncompressedin = slcomp->sls_i_uncompressed;
			t.vj.vjs_compressedin = slcomp->sls_i_compressed;
		}
#endif
	}
	memcpy_tofs (res, &t, sizeof (struct ppp_stats));
	return 0;

}

int isdn_ppp_dev_ioctl(struct device *dev, struct ifreq *ifr, int cmd)
{
	int error;
	char *r;
	int len;
	isdn_net_local *lp = (isdn_net_local *) dev->priv;

#if 0
	printk(KERN_DEBUG "ippp, dev_ioctl: cmd %#08x , %d \n",cmd,lp->ppp_slot);
#endif

	if (lp->p_encap != ISDN_NET_ENCAP_SYNCPPP)
		return -EINVAL;

	switch (cmd) {
		case SIOCGPPPVER:
			r = (char *) ifr->ifr_ifru.ifru_data;
			len = strlen(PPP_VERSION) + 1;
			error = verify_area(VERIFY_WRITE, r, len);
			if (!error)
				memcpy_tofs(r, PPP_VERSION, len);
			break;
		case SIOCGPPPSTATS:
			error = isdn_ppp_dev_ioctl_stats (lp->ppp_slot, ifr, dev);
			break;
		default:
			error = -EINVAL;
			break;
	}
	return error;
}

static int isdn_ppp_if_get_unit(char *name)
{
	int len, i, unit = 0, deci;

	len = strlen(name);

	if(strncmp("ippp",name,4) || len > 8)
		return -1;

	for (i = 0, deci = 1; i < len; i++, deci *= 10) {
		char a = name[len-i-1];
		if (a >= '0' && a <= '9')
			unit += (a - '0') * deci;
		else
			break;
	}
	if (!i || len-i != 4)
		unit = -1;

	return unit;
}


int isdn_ppp_dial_slave(char *name)
{
#ifdef CONFIG_ISDN_MPP
	isdn_net_dev *ndev;
	isdn_net_local *lp;
	struct device *sdev;

	if(!(ndev = isdn_net_findif(name)))
		return 1;
	lp = &ndev->local;
	if(!(lp->flags & ISDN_NET_CONNECTED))
		return 5;

	sdev = lp->slave;
	while(sdev)
	{
		isdn_net_local *mlp = (isdn_net_local *) sdev->priv;
		if(!(mlp->flags & ISDN_NET_CONNECTED))
			break;
		sdev = mlp->slave;
	}
	if(!sdev)
		return 2;

	isdn_net_force_dial_lp((isdn_net_local *) sdev->priv);
	return 0;
#else
	return -1;
#endif
}

int isdn_ppp_hangup_slave(char *name)
{
#ifdef CONFIG_ISDN_MPP
        isdn_net_dev *ndev;
        isdn_net_local *lp;
        struct device *sdev;

	if(!(ndev = isdn_net_findif(name)))
		return 1;
	lp = &ndev->local;
	if(!(lp->flags & ISDN_NET_CONNECTED))
		return 5;

	sdev = lp->slave;
	while(sdev)
	{
		isdn_net_local *mlp = (isdn_net_local *) sdev->priv;
		if((mlp->flags & ISDN_NET_CONNECTED))
			break;
		sdev = mlp->slave;
	}
	if(!sdev)
		return 2;

	isdn_net_hangup(sdev);
	return 0;
#else
	return -1;
#endif
}

