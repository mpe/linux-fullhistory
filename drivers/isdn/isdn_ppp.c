/* $Id: isdn_ppp.c,v 1.4 1996/02/19 15:25:50 fritz Exp fritz $
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

#ifndef STANDALONE
#include <linux/config.h>
#endif
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
static int isdn_ppp_fill_rq(char *buf, int len, int minor);
static int isdn_ppp_hangup(int);
static void isdn_ppp_push_higher(isdn_net_dev * net_dev, isdn_net_local * lp,
			struct sk_buff *skb, int proto);
static int isdn_ppp_if_get_unit(char **namebuf);

#ifdef CONFIG_ISDN_MPP
static int isdn_ppp_bundle(int, int);
static void isdn_ppp_mask_queue(isdn_net_dev * dev, long mask);
static void isdn_ppp_cleanup_queue(isdn_net_dev * dev, long min);
static int isdn_ppp_fill_mpqueue(isdn_net_dev *, struct sk_buff **skb,
		int BEbyte, int *sqno, int min_sqno);
#endif

char *isdn_ppp_revision              = "$Revision: 1.4 $";
struct ippp_struct *ippp_table = (struct ippp_struct *) 0;

extern int isdn_net_force_dial_lp(isdn_net_local *);

int isdn_ppp_free(isdn_net_local * lp)
{
	if (lp->ppp_minor < 0)
		return 0;

#ifdef CONFIG_ISDN_MPP
	if(lp->master)
	{
		isdn_net_dev *p = dev->netdev;
		lp->last->next = lp->next;
		lp->next->last = lp->last;
		if(lp->netdev->queue == lp)
			lp->netdev->queue = lp->next;
                lp->next = lp->last = lp;
		while(p) {
			if(lp == &p->local) {
				lp->netdev = p;
				break;
			}
			p=p->next;
		}
	} else {
                lp->netdev->ib.bundled = 0;
		/* last link: free mpqueue, free sqqueue ? */
	}

#endif

	isdn_ppp_hangup(lp->ppp_minor);
#if 0
	printk(KERN_DEBUG "isdn_ppp_free %d %lx %lx\n", lp->ppp_minor, (long) lp,(long) ippp_table[lp->ppp_minor].lp);
#endif
	ippp_table[lp->ppp_minor].lp = NULL;
	return 0;
}

int isdn_ppp_bind(isdn_net_local * lp)
{
	int i;
	int unit = 0;
	char *name;
	long flags;

	if (lp->p_encap != ISDN_NET_ENCAP_SYNCPPP)
		return 0;

	save_flags(flags);
	cli();

        /* 
         * search a free device 
         */
	for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
		if (ippp_table[i].state == IPPP_OPEN) {		/* OPEN, but not connected! */
#if 0
			printk(KERN_DEBUG "find_minor, %d lp: %08lx\n", i, (long) lp);
#endif
			break;
		}
	}

	if (i >= ISDN_MAX_CHANNELS) {
		restore_flags(flags);
		printk(KERN_WARNING "isdn_ppp_bind: Can't find usable ippp device.\n");
		return -1;
	}
	lp->ppp_minor = i;
	ippp_table[lp->ppp_minor].lp = lp;

	name = lp->name;
	unit = isdn_ppp_if_get_unit(&name); /* get unit number from interface name .. ugly! */
	ippp_table[lp->ppp_minor].unit = unit;

	ippp_table[lp->ppp_minor].state = IPPP_OPEN | IPPP_CONNECT | IPPP_NOBLOCK;

	restore_flags(flags);

        /*
         * kick the ipppd on the new device 
         */
	if (ippp_table[lp->ppp_minor].wq)
		wake_up_interruptible(&ippp_table[lp->ppp_minor].wq);

	return lp->ppp_minor;
}

static int isdn_ppp_hangup(int minor)
{
	if (minor < 0 || minor >= ISDN_MAX_CHANNELS)
		return 0;

	if (ippp_table[minor].state && ippp_table[minor].wq)
		wake_up_interruptible(&ippp_table[minor].wq);

	ippp_table[minor].state = IPPP_CLOSEWAIT;
	return 1;
}

/*
 * isdn_ppp_open 
 */

int isdn_ppp_open(int minor, struct file *file)
{
#if 0
	printk(KERN_DEBUG "ippp, open, minor: %d state: %04x\n", minor,ippp_table[minor].state);
#endif
	if (ippp_table[minor].state)
		return -EBUSY;

	ippp_table[minor].lp = 0;
	ippp_table[minor].mp_seqno = 0;  /* MP sequence number */
	ippp_table[minor].pppcfg = 0;    /* ppp configuration */
	ippp_table[minor].mpppcfg = 0;   /* mppp configuration */
	ippp_table[minor].range = 0x1000000;	/* MP: 24 bit range */
	ippp_table[minor].last_link_seqno = -1;	/* MP: maybe set to Bundle-MIN, when joining a bundle ?? */
	ippp_table[minor].unit = -1;	/* set, when we have our interface */
	ippp_table[minor].mru = 1524;	/* MRU, default 1524 */
	ippp_table[minor].maxcid = 16;	/* VJ: maxcid */
	ippp_table[minor].tk = current;
	ippp_table[minor].wq = NULL;    /* read() wait queue */
	ippp_table[minor].wq1 = NULL;   /* select() wait queue */
	ippp_table[minor].first = ippp_table[minor].rq + NUM_RCV_BUFFS - 1; /* receive queue */
	ippp_table[minor].last = ippp_table[minor].rq;
#ifdef CONFIG_ISDN_PPP_VJ
        /*
         * VJ header compression init
         */
	ippp_table[minor].cbuf = kmalloc(ippp_table[minor].mru + PPP_HARD_HDR_LEN + 2, GFP_KERNEL);

	if (ippp_table[minor].cbuf == NULL) {
		printk(KERN_DEBUG "ippp: Can't allocate memory buffer for VJ compression.\n");
		return -ENOMEM;
	}
	ippp_table[minor].slcomp = slhc_init(16, 16);	/* not necessary for 2. link in bundle */
#endif

	ippp_table[minor].state = IPPP_OPEN;

	return 0;
}

void isdn_ppp_release(int minor, struct file *file)
{
	int i;

	if (minor < 0 || minor >= ISDN_MAX_CHANNELS)
		return;

#if 0
	printk(KERN_DEBUG "ippp: release, minor: %d %lx\n", minor, (long) ippp_table[minor].lp);
#endif

	if (ippp_table[minor].lp) {	/* a lp address says: this link is still up */
		isdn_net_dev *p = dev->netdev;
		while(p) {	/* find interface for our lp; */
			if(&p->local == ippp_table[minor].lp)
				break;
			p = p->next;
		}
		if(!p) {
			printk(KERN_ERR "isdn_ppp_release: Can't find device for net_local\n");
			p = ippp_table[minor].lp->netdev;
		}
		ippp_table[minor].lp->ppp_minor = -1;
		isdn_net_hangup(&p->dev); /* lp->ppp_minor==-1 => no calling of isdn_ppp_hangup() */
		ippp_table[minor].lp = NULL;
	}
	for (i = 0; i < NUM_RCV_BUFFS; i++) {
		if (ippp_table[minor].rq[i].buf)
			kfree(ippp_table[minor].rq[i].buf);
	}

#ifdef CONFIG_ISDN_PPP_VJ
	slhc_free(ippp_table[minor].slcomp);
	kfree(ippp_table[minor].cbuf);
#endif

	ippp_table[minor].state = 0;
}

static int get_arg(void *b, unsigned long *val)
{
	int r;
	if ((r = verify_area(VERIFY_READ, (void *) b, sizeof(unsigned long))))
		 return r;
	memcpy_fromfs((void *) val, b, sizeof(unsigned long));
	return 0;
}

static int set_arg(void *b, unsigned long val)
{
	int r;
	if ((r = verify_area(VERIFY_WRITE, b, sizeof(unsigned long))))
		 return r;
	memcpy_tofs(b, (void *) &val, sizeof(unsigned long));
	return 0;
}

int isdn_ppp_ioctl(int minor, struct file *file, unsigned int cmd, unsigned long arg)
{
	unsigned long val;
	int r;

#if 0
	printk(KERN_DEBUG "isdn_ppp_ioctl: minor: %d cmd: %x",minor,cmd);
	printk(KERN_DEBUG " state: %x\n",ippp_table[minor].state);
#endif

	if (!(ippp_table[minor].state & IPPP_OPEN))
		return -EINVAL;

	switch (cmd) {
#if 0
	case PPPIOCSINPSIG:	/* obsolete: set input ready signal */
		/* usual: sig = SIGIO *//* we always deliver a SIGIO */
		break;
#endif
	case PPPIOCBUNDLE:
#ifdef CONFIG_ISDN_MPP
		if ((r = get_arg((void *) arg, &val)))
			return r;
		printk(KERN_DEBUG "iPPP-bundle: minor: %d, slave unit: %d, master unit: %d\n",
                        (int) minor, (int) ippp_table[minor].unit, (int) val);
		return isdn_ppp_bundle(minor, val);
#else
		return -1;
#endif
		break;
	case PPPIOCGUNIT:	/* get ppp/isdn unit number */
		if ((r = set_arg((void *) arg, ippp_table[minor].unit)))
			return r;
		break;
	case PPPIOCGMPFLAGS:	/* get configuration flags */
		if ((r = set_arg((void *) arg, ippp_table[minor].mpppcfg)))
			return r;
		break;
	case PPPIOCSMPFLAGS:	/* set configuration flags */
		if ((r = get_arg((void *) arg, &val)))
			return r;
		ippp_table[minor].mpppcfg = val;
		break;
	case PPPIOCGFLAGS:	/* get configuration flags */
		if ((r = set_arg((void *) arg, ippp_table[minor].pppcfg)))
			return r;
		break;
	case PPPIOCSFLAGS:	/* set configuration flags */
		if ((r = get_arg((void *) arg, &val))) {
			return r;
		}
		if (val & SC_ENABLE_IP && !(ippp_table[minor].pppcfg & SC_ENABLE_IP)) {
			ippp_table[minor].lp->netdev->dev.tbusy = 0;
			mark_bh(NET_BH); /* OK .. we are ready to send the first buffer */
		}
		ippp_table[minor].pppcfg = val;
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
		ippp_table[minor].mru = val;
		break;
	case PPPIOCSMPMRU:
		break;
	case PPPIOCSMPMTU:
		break;
	case PPPIOCSMAXCID:	/* set the maximum compression slot id */
		if ((r = get_arg((void *) arg, &val)))
			return r;
		ippp_table[minor].maxcid = val;
		break;
	case PPPIOCGDEBUG:
		break;
	case PPPIOCSDEBUG:
		break;
	default:
		break;
	}
	return 0;
}

int isdn_ppp_select(int minor, struct file *file, int type, select_table * st)
{
	struct ippp_buf_queue *bf, *bl;
	unsigned long flags;

#if 0
	printk(KERN_DEBUG "isdn_ppp_select: minor: %d, type: %d \n",minor,type);
#endif

	if (!(ippp_table[minor].state & IPPP_OPEN))
		return -EINVAL;

	switch (type) {
	case SEL_IN:
		save_flags(flags);
		cli();
		bl = ippp_table[minor].last;
		bf = ippp_table[minor].first;
		if (bf->next == bl && !(ippp_table[minor].state & IPPP_NOBLOCK)) {
			select_wait(&ippp_table[minor].wq, st);
			restore_flags(flags);
			return 0;
		}
		ippp_table[minor].state &= ~IPPP_NOBLOCK;
		restore_flags(flags);
		return 1;
	case SEL_OUT:
                /* we're always ready to send .. */
		return 1;
	case SEL_EX:
		select_wait(&ippp_table[minor].wq1, st);
		return 0;
	}
	return 1;
}

/*
 *  fill up isdn_ppp_read() queue ..
 */

static int isdn_ppp_fill_rq(char *buf, int len, int minor)
{
	struct ippp_buf_queue *bf, *bl;
	unsigned long flags;

	if (minor < 0 || minor >= ISDN_MAX_CHANNELS) {
		printk(KERN_WARNING "ippp: illegal minor.\n");
		return 0;
	}
	if (!(ippp_table[minor].state & IPPP_CONNECT)) {
		printk(KERN_DEBUG "ippp: device not activated.\n");
		return 0;
	}
	save_flags(flags);
	cli();

	bf = ippp_table[minor].first;
	bl = ippp_table[minor].last;

	if (bf == bl) {
		printk(KERN_WARNING "ippp: Queue is full; discarding first buffer\n");
		bf = bf->next;
		kfree(bf->buf);
		ippp_table[minor].first = bf;
	}
	bl->buf = (char *) kmalloc(len, GFP_ATOMIC);
	if (!bl->buf) {
		printk(KERN_WARNING "ippp: Can't alloc buf\n");
		restore_flags(flags);
		return 0;
	}
	bl->len = len;

	memcpy(bl->buf, buf, len);

	ippp_table[minor].last = bl->next;
	restore_flags(flags);

	if (ippp_table[minor].wq)
		wake_up_interruptible(&ippp_table[minor].wq);

	return len;
}

/*
 * read() .. non-blocking: ipppd calls it only after select()
 *           reports, that there is data
 */

int isdn_ppp_read(int minor, struct file *file, char *buf, int count)
{
	struct ippp_struct *c = &ippp_table[minor];
	struct ippp_buf_queue *b;
	int r;
	unsigned long flags;

	if (!(ippp_table[minor].state & IPPP_OPEN))
		return 0;

	if ((r = verify_area(VERIFY_WRITE, (void *) buf, count)))
		return r;

	save_flags(flags);
	cli();

	b = c->first->next;
	if (!b->buf) {
		restore_flags(flags);
		return -EAGAIN;
	}
	if (b->len < count)
		count = b->len;
	memcpy_tofs(buf, b->buf, count);
	kfree(b->buf);
	b->buf = NULL;
	c->first = b;
	restore_flags(flags);

	return count;
}

/*
 * ipppd wanna write a packet to the card .. non-blocking
 */
 
int isdn_ppp_write(int minor, struct file *file,  const char *buf, int count)
{
	isdn_net_local *lp;

	if (!(ippp_table[minor].state & IPPP_CONNECT))
		return 0;

	lp = ippp_table[minor].lp;

	/* -> push it directly to the lowlevel interface */

	if (!lp)
		printk(KERN_DEBUG "isdn_ppp_write: lp == NULL\n");
	else {
		if (lp->isdn_device < 0 || lp->isdn_channel < 0)
			return 0;

		if (dev->drv[lp->isdn_device]->running && lp->dialstate == 0 &&
		    (lp->flags & ISDN_NET_CONNECTED))
			dev->drv[lp->isdn_device]->interface->writebuf(
                                lp->isdn_device,lp->isdn_channel, buf, count, 1);
	}

	return count;
}

/*
 * init memory, structures etc. 
 */

int isdn_ppp_init(void)
{
	int i, j;

	if (!(ippp_table = (struct ippp_struct *)
	      kmalloc(sizeof(struct ippp_struct) * ISDN_MAX_CHANNELS, GFP_KERNEL))) {
		printk(KERN_WARNING "isdn_ppp_init: Could not alloc ippp_table\n");
		return -1;
	}
	memset((char *) ippp_table, 0, sizeof(struct ippp_struct) * ISDN_MAX_CHANNELS);
	for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
		ippp_table[i].state = 0;
		ippp_table[i].first = ippp_table[i].rq + NUM_RCV_BUFFS - 1;
		ippp_table[i].last = ippp_table[i].rq;

		for (j = 0; j < NUM_RCV_BUFFS; j++) {
			ippp_table[i].rq[j].buf = NULL;
			ippp_table[i].rq[j].last = ippp_table[i].rq +
			    (NUM_RCV_BUFFS + j - 1) % NUM_RCV_BUFFS;
			ippp_table[i].rq[j].next = ippp_table[i].rq + (j + 1) % NUM_RCV_BUFFS;
		}
	}
	return 0;
}

void isdn_ppp_cleanup(void)
{
	kfree(ippp_table);
}

/*
 * handler for incoming packets on a syncPPP interface
 */

void isdn_ppp_receive(isdn_net_dev * net_dev, isdn_net_local * lp, struct sk_buff *skb)
{
#if 0
	printk(KERN_DEBUG "recv, skb %d\n",skb->len);
#endif

	if(skb->data[0] == 0xff && skb->data[1] == 0x03)
		skb_pull(skb,2);
	else if (ippp_table[lp->ppp_minor].pppcfg & SC_REJ_COMP_AC)
		return;		/* discard it silently */

#ifdef CONFIG_ISDN_MPP
	if (!(ippp_table[lp->ppp_minor].mpppcfg & SC_REJ_MP_PROT)) {
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
#if 0
			printk(KERN_DEBUG "recv: %d/%04x/%d -> %02x %02x %02x %02x %02x %02x\n", lp->ppp_minor, proto ,
				(int) skb->len, (int) skb->data[0], (int) skb->data[1], (int) skb->data[2], 
				(int) skb->data[3], (int) skb->data[4], (int) skb->data[5]);
#endif
			if (!(ippp_table[lp->ppp_minor].mpppcfg & SC_IN_SHORT_SEQ)) {
				sqno = ((int) skb->data[1] << 16) + ((int) skb->data[2] << 8) + (int) skb->data[3];
				skb_pull(skb,4);
			} else {
				sqno = (((int) skb->data[0] & 0xf) << 8) + (int) skb->data[1];
				skb_pull(skb,2);
			}

			if ((tseq = ippp_table[lp->ppp_minor].last_link_seqno) >= sqno) {
				int range = ippp_table[lp->ppp_minor].range;
				if (tseq + 1024 < range + sqno) /* redundancy check .. not MP conform */
					printk(KERN_WARNING "isdn_ppp_receive, MP, detected overflow with sqno: %d, last: %d !!!\n", sqno, tseq);
				else {
					sqno += range;
					ippp_table[lp->ppp_minor].last_link_seqno = sqno;
				}
			} else
				ippp_table[lp->ppp_minor].last_link_seqno = sqno;

			for (min_sqno = 0, lpq = net_dev->queue;;) {
				if (ippp_table[lpq->ppp_minor].last_link_seqno > min_sqno)
					min_sqno = ippp_table[lpq->ppp_minor].last_link_seqno;
				lpq = lpq->next;
				if (lpq == net_dev->queue)
					break;
			}
			if (min_sqno >= ippp_table[lpq->ppp_minor].range) {	/* OK, every link overflowed */
				int mask = ippp_table[lpq->ppp_minor].range - 1;	/* range is a power of 2 */
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
					ippp_table[lpq->ppp_minor].last_link_seqno &= mask;
					lpq = lpq->next;
					if (lpq == net_dev->queue)
						break;
				}
			}
			if ((BEbyte & (MP_BEGIN_FRAG | MP_END_FRAG)) != (MP_BEGIN_FRAG | MP_END_FRAG)) {
				printk(KERN_DEBUG "ippp: trying ;) to fill mp_queue %d .. UNTESTED!!\n", lp->ppp_minor);
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
					printk(KERN_WARNING "ippp: err, no memory !!\n");
					net_dev->ib.modify = 0;
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


static void isdn_ppp_push_higher(isdn_net_dev *net_dev, isdn_net_local *lp, struct sk_buff *skb,int proto)
{
	struct device *dev = &net_dev->dev;

	if (proto < 0) {	/* MP, oder normales Paket bei REJ_MP, MP Pakete gehen bei REJ zum pppd */
		if (skb->data[0] & 0x01) {	/* is it odd? */
			proto = (unsigned char) skb->data[0];
			skb_pull(skb,1);	/* protocol ID is only 8 bit */
		} else {
			proto = ((int) (unsigned char) skb->data[0] << 8) + (unsigned char) skb->data[1];
			skb_pull(skb,2);
		}
	}

#if 0
	printk(KERN_DEBUG "push, skb %d %04x\n",skb->len,proto);
#endif

	switch (proto) {
	case PPP_IPX: /* untested */
		skb->dev = dev;
		skb->mac.raw = skb->data;
		skb->protocol = htons(ETH_P_IPX);
		break;
#ifdef CONFIG_ISDN_PPP_VJ
	case PPP_VJC_UNCOMP:
		slhc_remember(ippp_table[net_dev->local.ppp_minor].slcomp, skb->data, skb->len);
#endif
	case PPP_IP:
		skb->dev = dev;
		skb->mac.raw = skb->data;
		skb->protocol = htons(ETH_P_IP);
		break;
	case PPP_VJC_COMP:
#ifdef CONFIG_ISDN_PPP_VJ
		{
			struct sk_buff *skb_old = skb;
			int pkt_len;
			skb = dev_alloc_skb(skb_old->len + 40);

			if (!skb) {
				printk(KERN_WARNING "%s: Memory squeeze, dropping packet.\n", dev->name);
				net_dev->local.stats.rx_dropped++;
				return;
			}
			skb->dev = dev;
			skb_put(skb,skb_old->len + 40);
			memcpy(skb->data, skb_old->data, skb_old->len);
			skb->mac.raw = skb->data;
			pkt_len = slhc_uncompress(ippp_table[net_dev->local.ppp_minor].slcomp,
						  skb->data, skb_old->len);
			skb_trim(skb, pkt_len);
			dev_kfree_skb(skb_old,FREE_WRITE);
			skb->protocol = htons(ETH_P_IP);
		}
#else
		printk(KERN_INFO "isdn: Ooopsa .. VJ-Compression support not compiled into isdn driver.\n");
		lp->stats.rx_dropped++;
		return;
#endif
		break;
	default:
		skb_push(skb,4);
		skb->data[0] = 0xff;
		skb->data[1] = 0x03;
		skb->data[2] = (proto>>8);
		skb->data[3] = proto & 0xff;
		isdn_ppp_fill_rq(skb->data, skb->len, lp->ppp_minor);	/* push data to pppd device */
		dev_kfree_skb(skb,FREE_WRITE);
		return;
	}

	netif_rx(skb);
	net_dev->local.stats.rx_packets++;
	/* Reset hangup-timer */
	lp->huptimer = 0;

	return;
}

/*
 * send ppp frame .. we expect a PIDCOMPable proto -- 
 *  (here: currently always PPP_IP,PPP_VJC_COMP,PPP_VJC_UNCOMP)
 */
int isdn_ppp_xmit(struct sk_buff *skb, struct device *dev)
{
	isdn_net_dev *nd = ((isdn_net_local *) dev->priv)->netdev;
	isdn_net_local *lp = nd->queue;
	int proto = PPP_IP;	/* 0x21 */
	struct ippp_struct *ipt = ippp_table + lp->ppp_minor;
	struct ippp_struct *ipts = ippp_table + lp->netdev->local.ppp_minor;

        /* If packet is to be resent, it has already been processed and
         * therefore it's first bytes are already initialized. In this case
         * send it immediately ...
         */
        if (*((unsigned long *)skb->data) != 0)
          return (isdn_net_send_skb(dev , lp , skb));

        /* ... else packet needs processing. */

/* future: step to next 'lp' when this lp is 'tbusy' */

#if 0
	printk(KERN_DEBUG  "xmit, skb %d\n",skb->len);
#endif

#ifdef CONFIG_ISDN_PPP_VJ
	if (ipt->pppcfg & SC_COMP_TCP) {
		u_char *buf = skb->data;
		int pktlen;
		int len = 4;
#ifdef CONFIG_ISDN_MPP
		if (ipt->mpppcfg & SC_MP_PROT) /* sigh */ 
			if (ipt->mpppcfg & SC_OUT_SHORT_SEQ)
				len += 3;
			else
				len += 5;
#endif
		buf += len;
		pktlen = slhc_compress(ipts->slcomp, buf, skb->len-len, ipts->cbuf,
				&buf, !(ipts->pppcfg & SC_NO_TCP_CCID));
		skb_trim(skb,pktlen+len);
		if(buf != skb->data+len) { /* copied to new buffer ??? (btw: WHY must slhc copy it?? *sigh*)  */
			memcpy(skb->data+len,buf,pktlen);
		}
		if (skb->data[len] & SL_TYPE_COMPRESSED_TCP) {	/* cslip? style -> PPP */
			proto = PPP_VJC_COMP;
			skb->data[len] ^= SL_TYPE_COMPRESSED_TCP;
		} else {
			if (skb->data[len] >= SL_TYPE_UNCOMPRESSED_TCP)
				proto = PPP_VJC_UNCOMP;
			skb->data[len] = (skb->data[len] & 0x0f) | 0x40;
		}
	}
#endif

#if 0
	printk(KERN_DEBUG  "xmit, skb %d %04x\n",skb->len,proto);
#endif

#ifdef CONFIG_ISDN_MPP
	if (ipt->mpppcfg & SC_MP_PROT) {
		/* we get mp_seqno from static isdn_net_local */
		long mp_seqno = ipts->mp_seqno;
		ipts->mp_seqno++;
		nd->queue = nd->queue->next;
		if (ipt->mpppcfg & SC_OUT_SHORT_SEQ) {
			/* skb_push(skb, 3); Done in isdn_net_header() */
			mp_seqno &= 0xfff;
			skb->data[4] = MP_BEGIN_FRAG | MP_END_FRAG | (mp_seqno >> 8);	/* (B)egin & (E)ndbit .. */
			skb->data[5] = mp_seqno & 0xff;
			skb->data[6] = proto;	/* PID compression */
		} else {
			/* skb_push(skb, 5); Done in isdn_net_header () */
			skb->data[4] = MP_BEGIN_FRAG | MP_END_FRAG;	/* (B)egin & (E)ndbit .. */
			skb->data[5] = (mp_seqno >> 16) & 0xff;	/* sequence nubmer: 24bit */
			skb->data[6] = (mp_seqno >> 8) & 0xff;
			skb->data[7] = (mp_seqno >> 0) & 0xff;
			skb->data[8] = proto;	/* PID compression */
		}
		proto = PPP_MP; /* MP Protocol, 0x003d */
	}
#endif
	skb->data[0] = 0xff;        /* All Stations */
	skb->data[1] = 0x03;        /* Unumbered information */
	skb->data[2] = proto >> 8;
	skb->data[3] = proto & 0xff;

	lp->huptimer = 0;
	if (!(ipt->pppcfg & SC_ENABLE_IP)) {	/* PPP connected ? */
		printk(KERN_INFO "isdn, xmit: Packet blocked: %d %d\n", lp->isdn_device, lp->isdn_channel);
		return 1;
	}
        /* tx-stats are now updated via BSENT-callback */
	return (isdn_net_send_skb(dev , lp , skb));
}

void isdn_ppp_free_mpqueue(isdn_net_dev * p)
{
	struct mpqueue *ql, *q = p->mp_last;
	while (q) {
		ql = q->next;
 		dev_kfree_skb(q->skb,FREE_WRITE);
		kfree(q);
		q = ql;
	}
}

#ifdef CONFIG_ISDN_MPP

static int isdn_ppp_bundle(int minor, int unit)
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

	nlp = ippp_table[minor].lp;

	lp = p->queue;
	p->ib.bundled = 1;
	nlp->last = lp->last;
	lp->last->next = nlp;
	lp->last = nlp;
	nlp->next = lp;
	p->queue = nlp;

	nlp->netdev = lp->netdev;

	ippp_table[nlp->ppp_minor].unit = ippp_table[lp->ppp_minor].unit;
/* maybe also SC_CCP stuff */
	ippp_table[nlp->ppp_minor].pppcfg |= ippp_table[lp->ppp_minor].pppcfg &
	    (SC_ENABLE_IP | SC_NO_TCP_CCID | SC_REJ_COMP_TCP);

	ippp_table[nlp->ppp_minor].mpppcfg |= ippp_table[lp->ppp_minor].mpppcfg &
	    (SC_MP_PROT | SC_REJ_MP_PROT | SC_OUT_SHORT_SEQ | SC_IN_SHORT_SEQ);
#if 0
	if (ippp_table[nlp->ppp_minor].mpppcfg != ippp_table[lp->ppp_minor].mpppcfg) {
		printk(KERN_WARNING "isdn_ppp_bundle: different MP options %04x and %04x\n",
		       ippp_table[nlp->ppp_minor].mpppcfg, ippp_table[lp->ppp_minor].mpppcfg);
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
			dev_kfree_skb(q->skb,FREE_WRITE);
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
		dev_kfree_skb(q->skb,FREE_WRITE);
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
					dev_kfree_skb(q->skb,FREE_WRITE);
					kfree(q);
					q = ql;
				}
				q = dev->mp_last;
			} else
				q = q->next;
		} else
			break;
	}
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
		if (net_dev->ib.modify)	{	/* interface locked? */
                        net_dev = net_dev->next;
			continue;
                }

		q = net_dev->ib.sq;
		while (q) {
			if (q->sqno_start == net_dev->ib.next_num || q->timer < jiffies) {
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

int isdn_ppp_dev_ioctl(struct device *dev, struct ifreq *ifr, int cmd)
{
	int error;
	char *r;
	int len;
	isdn_net_local *lp = (isdn_net_local *) dev->priv;

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
	default:
		error = -EINVAL;
	}
	return error;
}

static int isdn_ppp_if_get_unit(char **namebuf)
{
	char *name = *namebuf;
	int len, i, unit = 0, deci;

	len = strlen(name);
	for (i = 0, deci = 1; i < len; i++, deci *= 10) {
		if (name[len - 1 - i] >= '0' && name[len - 1 - i] <= '9')
			unit += (name[len - 1 - i] - '0') * deci;
		else
			break;
	}
	if (!i)
		unit = -1;

	*namebuf = name + len - 1 - i;
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


