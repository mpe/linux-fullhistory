/* $Id: isdn_common.c,v 1.5 1996/04/20 16:19:07 fritz Exp $
 *
 * Linux ISDN subsystem, common used functions (linklevel).
 *
 * Copyright 1994,95,96 by Fritz Elfert (fritz@wuemaus.franken.de)
 * Copyright 1995,96    Thinking Objects Software GmbH Wuerzburg
 * Copyright 1995,96    by Michael Hipp (Michael.Hipp@student.uni-tuebingen.de)
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
 * $Log: isdn_common.c,v $
 * Revision 1.5  1996/04/20 16:19:07  fritz
 * Changed slow timer handlers to increase accuracy.
 * Added statistic information for usage by xisdnload.
 * Fixed behaviour of isdnctrl-device on non-blocked io.
 * Fixed all io to go through generic writebuf-function without
 * bypassing. Same for incoming data.
 * Fixed bug: Last channel had been unusable.
 * Fixed kfree of tty xmit_buf on ppp initialization failure.
 *
 * Revision 1.4  1996/02/11 02:33:26  fritz
 * Fixed bug in main timer-dispatcher.
 * Bugfix: Lot of tty-callbacks got called regardless of the events already
 * been handled by network-devices.
 * Changed ioctl-names.
 *
 * Revision 1.3  1996/01/22 05:16:11  fritz
 * Changed ioctl-names.
 * Fixed bugs in isdn_open and isdn_close regarding PPP_MINOR.
 *
 * Revision 1.2  1996/01/21 16:52:40  fritz
 * Support for sk_buffs added, changed header-stuffing.
 *
 * Revision 1.1  1996/01/09 04:12:52  fritz
 * Initial revision
 *
 */

#ifndef STANDALONE
#include <linux/config.h>
#endif
#include <linux/module.h>
#include <linux/version.h>
#ifndef __GENKSYMS__      /* Don't want genksyms report unneeded structs */
#include <linux/isdn.h>
#endif
#include "isdn_common.h"
#include "isdn_tty.h"
#include "isdn_net.h"
#include "isdn_ppp.h"
#include "isdn_cards.h"



/* Debugflags */
#undef  ISDN_DEBUG_STATCALLB

isdn_dev *dev = (isdn_dev *) 0;

static int  has_exported = 0;
static char *isdn_revision      = "$Revision: 1.5 $";

extern char *isdn_net_revision;
extern char *isdn_tty_revision;
#ifdef CONFIG_ISDN_PPP
extern char *isdn_ppp_revision;
#else
static char *isdn_ppp_revision = ": none $";
#endif

void isdn_MOD_INC_USE_COUNT(void)
{
	MOD_INC_USE_COUNT;
}

void isdn_MOD_DEC_USE_COUNT(void)
{
	MOD_DEC_USE_COUNT;
}

#if defined(ISDN_DEBUG_NET_DUMP) || defined(ISDN_DEBUG_MODEM_DUMP)
void isdn_dumppkt(char *s, u_char * p, int len, int dumplen)
{
	int dumpc;

	printk(KERN_DEBUG "%s(%d) ", s, len);
	for (dumpc = 0; (dumpc < dumplen) && (len); len--, dumpc++)
		printk(" %02x", *p++);
	printk("\n");
}
#endif

/* Try to allocate a new buffer, link it into queue. */
u_char *
 isdn_new_buf(pqueue ** queue, int length)
{
	pqueue *p;
	pqueue *q;

	if ((p = *queue)) {
		while (p) {
			q = p;
			p = (pqueue *) p->next;
		}
		p = (pqueue *) kmalloc(sizeof(pqueue) + length, GFP_ATOMIC);
		q->next = (u_char *) p;
	} else
		p = *queue = (pqueue *) kmalloc(sizeof(pqueue) + length, GFP_ATOMIC);
	if (p) {
		p->size = sizeof(pqueue) + length;
		p->length = length;
		p->next = NULL;
		p->rptr = p->buffer;
		return p->buffer;
	} else {
		return (u_char *) NULL;
	}
}

static void isdn_free_queue(pqueue ** queue)
{
	pqueue *p;
	pqueue *q;

	p = *queue;
	while (p) {
		q = p;
		p = (pqueue *) p->next;
		kfree_s(q, q->size);
	}
	*queue = (pqueue *) 0;
}

int isdn_dc2minor(int di, int ch)
{
	int i;
	for (i = 0; i < ISDN_MAX_CHANNELS; i++)
		if (dev->chanmap[i] == ch && dev->drvmap[i] == di)
			return i;
	return -1;
}

static int isdn_timer_cnt1 = 0;
static int isdn_timer_cnt2 = 0;

static void isdn_timer_funct(ulong dummy)
{
	int tf = dev->tflags;

	if (tf & ISDN_TIMER_FAST) {
		if (tf & ISDN_TIMER_MODEMREAD)
			isdn_tty_readmodem();
		if (tf & ISDN_TIMER_MODEMPLUS)
			isdn_tty_modem_escape();
		if (tf & ISDN_TIMER_MODEMXMIT)
			isdn_tty_modem_xmit();
	}
	if (tf & ISDN_TIMER_SLOW) {
		if (++isdn_timer_cnt1 >= ISDN_TIMER_02SEC) {
			isdn_timer_cnt1 = 0;
			if (tf & ISDN_TIMER_NETDIAL)
				isdn_net_dial();
		}
                if (++isdn_timer_cnt2 >= ISDN_TIMER_1SEC) {
                        isdn_timer_cnt2 = 0;
                        if (tf & ISDN_TIMER_NETHANGUP)
                                isdn_net_autohup();
                        if (tf & ISDN_TIMER_MODEMRING)
                                isdn_tty_modem_ring();
#if (defined CONFIG_ISDN_PPP ) && (defined ISDN_CONFIG_MPP)
                        if (tf & ISDN_TIMER_IPPP)
                                isdn_ppp_timer_timeout();
#endif
                }
	}
	if (tf) {
                int flags;

		save_flags(flags);
		cli();
		del_timer(&dev->timer);
		dev->timer.function = isdn_timer_funct;
		dev->timer.expires = jiffies + ISDN_TIMER_RES;
		add_timer(&dev->timer);
		restore_flags(flags);
	}
}

void isdn_timer_ctrl(int tf, int onoff)
{
	int flags;

	save_flags(flags);
	cli();
	if ((tf & ISDN_TIMER_SLOW) && (!(dev->tflags & ISDN_TIMER_SLOW))) {
		/* If the slow-timer wasn't activated until now */
		isdn_timer_cnt1 = 0;
		isdn_timer_cnt2 = 0;
	}
	if (onoff)
		dev->tflags |= tf;
	else
		dev->tflags &= ~tf;
	if (dev->tflags) {
		del_timer(&dev->timer);
		dev->timer.function = isdn_timer_funct;
		dev->timer.expires = jiffies + ISDN_TIMER_RES;
		add_timer(&dev->timer);
	}
	restore_flags(flags);
}

/* Receive a packet from B-Channel. (Called from low-level-module)
 * Parameters:
 *
 * di      = Driver-Index.
 * channel = Number of B-Channel (0...)
 * buf     = pointer to packet-data
 * len     = Length of packet-data
 *
 */
static void isdn_receive_callback(int di, int channel, u_char * buf, int len)
{
	ulong flags;
	char *p;
	int i;
	int midx;

	if (dev->global_flags & ISDN_GLOBAL_STOPPED)
		return;
        if ((i = isdn_dc2minor(di,channel))==-1)
                return;
	/* Update statistics */
        dev->ibytes[i] += len;
	/* First, try to deliver data to network-device */
	if (isdn_net_receive_callback(i, buf, len))
		return;
	/* No network-device found, deliver to tty or raw-channel */
	if (len) {
		save_flags(flags);
		cli();
                midx = dev->m_idx[i];
                if (dev->mdm.atmodem[midx].mdmreg[13] & 2)
                        /* T.70 decoding: Simply throw away the T.70 header (4 bytes) */
                        if ((buf[0] == 1) && ((buf[1] == 0) || (buf[1] == 1))) {
#ifdef ISDN_DEBUG_MODEM_DUMP
                                isdn_dumppkt("T70strip1:", buf, len, len);
#endif
                                buf += 4;
                                len -= 4;
#ifdef ISDN_DEBUG_MODEM_DUMP
                                isdn_dumppkt("T70strip2:", buf, len, len);
#endif
                        }
                /* Try to deliver directly via tty-flip-buf if queue is empty */
                if (!dev->drv[di]->rpqueue[channel])
                        if (isdn_tty_try_read(midx, buf, len)) {
                                restore_flags(flags);
                                return;
                        }
                /* Direct deliver failed or queue wasn't empty.
                 * Queue up for later dequeueing via timer-irq.
                 */
                p = isdn_new_buf(&dev->drv[di]->rpqueue[channel], len);
                if (!p) {
                        printk(KERN_WARNING "isdn: malloc of rcvbuf failed, dropping.\n");
                        dev->drv[di]->rcverr[channel]++;
                        restore_flags(flags);
                        return;
                } else {
                        memcpy(p, buf, len);
                        dev->drv[di]->rcvcount[channel] += len;
                }
                /* Schedule dequeuing */
                if ((dev->modempoll) && (midx >= 0)) {
                        if (dev->mdm.rcvsched[midx])
                                isdn_timer_ctrl(ISDN_TIMER_MODEMREAD, 1);
                }
                wake_up_interruptible(&dev->drv[di]->rcv_waitq[channel]);
		restore_flags(flags);
	}
}

void isdn_all_eaz(int di, int ch)
{
	isdn_ctrl cmd;

	cmd.driver = di;
	cmd.arg = ch;
	cmd.command = ISDN_CMD_SETEAZ;
	cmd.num[0] = '\0';
	(void) dev->drv[di]->interface->command(&cmd);
}

static int isdn_status_callback(isdn_ctrl * c)
{
	int di;
	int mi;
	ulong flags;
	int i;
	int r;
	isdn_ctrl cmd;

	di = c->driver;
        i = isdn_dc2minor(di, c->arg);
	switch (c->command) {
                case ISDN_STAT_BSENT:
			if (i<0)
				return -1;
                        if (dev->global_flags & ISDN_GLOBAL_STOPPED)
                                return 0;
                        if (isdn_net_stat_callback(i, c->command))
                                return 0;
#if FUTURE
                        isdn_tty_bsent(di, c->arg);
#endif
                        wake_up_interruptible(&dev->drv[di]->snd_waitq[c->arg]);
                        break;
                case ISDN_STAT_STAVAIL:
                        save_flags(flags);
                        cli();
                        dev->drv[di]->stavail += c->arg;
                        restore_flags(flags);
                        wake_up_interruptible(&dev->drv[di]->st_waitq);
                        break;
                case ISDN_STAT_RUN:
                        dev->drv[di]->running = 1;
                        for (i = 0; i < ISDN_MAX_CHANNELS; i++)
                                if (dev->drvmap[i] == di)
                                        isdn_all_eaz(di, dev->chanmap[i]);
                        break;
                case ISDN_STAT_STOP:
                        dev->drv[di]->running = 0;
                        break;
                case ISDN_STAT_ICALL:
			if (i<0)
				return -1;
#ifdef ISDN_DEBUG_STATCALLB
                        printk(KERN_DEBUG "ICALL (net): %d %ld %s\n", di, c->arg, c->num);
#endif
                        if (dev->global_flags & ISDN_GLOBAL_STOPPED) {
                                cmd.driver = di;
                                cmd.arg = c->arg;
                                cmd.command = ISDN_CMD_HANGUP;
                                dev->drv[di]->interface->command(&cmd);
                                return 0;
                        }

			/* Try to find a network-interface which will accept incoming call */
			r = isdn_net_find_icall(di, c->arg, i, c->num);
			switch (r) {
                                case 0:
                                        /* No network-device replies. Schedule RING-message to
                                         * tty and set RI-bit of modem-status.
                                         */
                                        if ((mi = isdn_tty_find_icall(di, c->arg, c->num)) >= 0) {
                                                dev->mdm.msr[mi] |= UART_MSR_RI;
                                                isdn_tty_modem_result(2, &dev->mdm.info[mi]);
                                                isdn_timer_ctrl(ISDN_TIMER_MODEMRING, 1);
                                        } else if (dev->drv[di]->reject_bus) {
                                                cmd.driver = di;
                                                cmd.arg = c->arg;
                                                cmd.command = ISDN_CMD_HANGUP;
                                                dev->drv[di]->interface->command(&cmd);
                                        }
                                        break;
                                case 1:
                                        /* Schedule connection-setup */
                                        isdn_net_dial();
                                        cmd.driver = di;
                                        cmd.arg = c->arg;
                                        cmd.command = ISDN_CMD_ACCEPTD;
                                        dev->drv[di]->interface->command(&cmd);
                                        break;
                                case 2:	/* For calling back, first reject incoming call ... */
                                case 3:	/* Interface found, but down, reject call actively  */
                                        printk(KERN_INFO "isdn: Rejecting Call\n");
                                        cmd.driver = di;
                                        cmd.arg = c->arg;
                                        cmd.command = ISDN_CMD_HANGUP;
                                        dev->drv[di]->interface->command(&cmd);
                                        if (r == 3)
                                                break;
                                        /* Fall through */
                                case 4:
                                        /* ... then start callback. */
                                        isdn_net_dial();
			}
                        return 0;
                        break;
                case ISDN_STAT_CINF:
			if (i<0)
				return -1;
#ifdef ISDN_DEBUG_STATCALLB
                        printk(KERN_DEBUG "CINF: %ld %s\n", c->arg, c->num);
#endif
                        if (dev->global_flags & ISDN_GLOBAL_STOPPED)
                                return 0;
                        if (strcmp(c->num, "0"))
                                isdn_net_stat_callback(i, c->command);
                        break;
                case ISDN_STAT_CAUSE:
#ifdef ISDN_DEBUG_STATCALLB
                        printk(KERN_DEBUG "CAUSE: %ld %s\n", c->arg, c->num);
#endif
                        printk(KERN_INFO "isdn: cause: %s\n", c->num);
                        break;
                case ISDN_STAT_DCONN:
			if (i<0)
				return -1;
#ifdef ISDN_DEBUG_STATCALLB
                        printk(KERN_DEBUG "DCONN: %ld\n", c->arg);
#endif
                        if (dev->global_flags & ISDN_GLOBAL_STOPPED)
                                return 0;
                        /* Find any network-device, waiting for D-channel setup */
                        if (isdn_net_stat_callback(i, c->command))
                                break;
			if ((mi = dev->m_idx[i]) >= 0)
				/* If any tty has just dialed-out, setup B-Channel */
				if (dev->mdm.info[mi].flags &
				    (ISDN_ASYNC_NORMAL_ACTIVE | ISDN_ASYNC_CALLOUT_ACTIVE)) {
					if (dev->mdm.dialing[mi] == 1) {
						dev->mdm.dialing[mi] = 2;
						cmd.driver = di;
						cmd.arg = c->arg;
						cmd.command = ISDN_CMD_ACCEPTB;
						dev->drv[di]->interface->command(&cmd);
						return 0;
					}
				}
                        break;
                case ISDN_STAT_DHUP:
			if (i<0)
				return -1;
#ifdef ISDN_DEBUG_STATCALLB
                        printk(KERN_DEBUG "DHUP: %ld\n", c->arg);
#endif
                        if (dev->global_flags & ISDN_GLOBAL_STOPPED)
                                return 0;
                        dev->drv[di]->flags &= ~(1 << (c->arg));
                        isdn_info_update();
                        /* Signal hangup to network-devices */
                        if (isdn_net_stat_callback(i, c->command))
                                break;
			if ((mi = dev->m_idx[i]) >= 0) {
				/* Signal hangup to tty-device */
				if (dev->mdm.info[mi].flags &
				    (ISDN_ASYNC_NORMAL_ACTIVE | ISDN_ASYNC_CALLOUT_ACTIVE)) {
					if (dev->mdm.dialing[mi] == 1) {
						dev->mdm.dialing[mi] = 0;
						isdn_tty_modem_result(7, &dev->mdm.info[mi]);
					}
					if (dev->mdm.online[mi])
						isdn_tty_modem_result(3, &dev->mdm.info[mi]);
#ifdef ISDN_DEBUG_MODEM_HUP
					printk(KERN_DEBUG "Mhup in ISDN_STAT_DHUP\n");
#endif
					isdn_tty_modem_hup(&dev->mdm.info[mi]);
					dev->mdm.msr[mi] &= ~(UART_MSR_DCD | UART_MSR_RI);
					return 0;
				}
			}
                        break;
                case ISDN_STAT_BCONN:
			if (i<0)
				return -1;
#ifdef ISDN_DEBUG_STATCALLB
                        printk(KERN_DEBUG "BCONN: %ld\n", c->arg);
#endif
                        /* Signal B-channel-connect to network-devices */
                        if (dev->global_flags & ISDN_GLOBAL_STOPPED)
                                return 0;
                        dev->drv[di]->flags |= (1 << (c->arg));
                        isdn_info_update();
                        if (isdn_net_stat_callback(i, c->command))
                                break;
			if ((mi = dev->m_idx[i]) >= 0) {
				/* Schedule CONNECT-Message to any tty, waiting for it and
				 * set DCD-bit of its modem-status.
				 */
				if (dev->mdm.info[mi].flags &
				    (ISDN_ASYNC_NORMAL_ACTIVE | ISDN_ASYNC_CALLOUT_ACTIVE)) {
					dev->mdm.msr[mi] |= UART_MSR_DCD;
					if (dev->mdm.dialing[mi])
						dev->mdm.dialing[mi] = 0;
					dev->mdm.rcvsched[mi] = 1;
					isdn_tty_modem_result(5, &dev->mdm.info[mi]);
				}
			}
                        break;
                case ISDN_STAT_BHUP:
			if (i<0)
				return -1;
#ifdef ISDN_DEBUG_STATCALLB
                        printk(KERN_DEBUG "BHUP: %ld\n", c->arg);
#endif
                        if (dev->global_flags & ISDN_GLOBAL_STOPPED)
                                return 0;
                        dev->drv[di]->flags &= ~(1 << (c->arg));
                        isdn_info_update();
			if ((mi = dev->m_idx[i]) >= 0) {
				/* Signal hangup to tty-device, schedule NO CARRIER-message */
				if (dev->mdm.info[mi].flags &
				    (ISDN_ASYNC_NORMAL_ACTIVE | ISDN_ASYNC_CALLOUT_ACTIVE)) {
					dev->mdm.msr[mi] &= ~(UART_MSR_DCD | UART_MSR_RI);
					if (dev->mdm.online[mi])
						isdn_tty_modem_result(3, &dev->mdm.info[mi]);
#ifdef ISDN_DEBUG_MODEM_HUP
					printk(KERN_DEBUG "Mhup in ISDN_STAT_BHUP\n");
#endif
					isdn_tty_modem_hup(&dev->mdm.info[mi]);
				}
			}
                        break;
                case ISDN_STAT_NODCH:
			if (i<0)
				return -1;
#ifdef ISDN_DEBUG_STATCALLB
                        printk(KERN_DEBUG "NODCH: %ld\n", c->arg);
#endif
                        if (dev->global_flags & ISDN_GLOBAL_STOPPED)
                                return 0;
                        if (isdn_net_stat_callback(i, c->command))
                                break;
			if ((mi = dev->m_idx[i]) >= 0) {
				if (dev->mdm.info[mi].flags &
				    (ISDN_ASYNC_NORMAL_ACTIVE | ISDN_ASYNC_CALLOUT_ACTIVE)) {
					if (dev->mdm.dialing[mi]) {
						dev->mdm.dialing[mi] = 0;
						isdn_tty_modem_result(6, &dev->mdm.info[mi]);
					}
					dev->mdm.msr[mi] &= ~UART_MSR_DCD;
					if (dev->mdm.online[mi]) {
						isdn_tty_modem_result(3, &dev->mdm.info[mi]);
						dev->mdm.online[mi] = 0;
					}
				}
			}
                        break;
                case ISDN_STAT_ADDCH:
                        break;
                case ISDN_STAT_UNLOAD:
                        save_flags(flags);
                        cli();
                        for (i = 0; i < ISDN_MAX_CHANNELS; i++)
                                if (dev->drvmap[i] == di) {
                                        dev->drvmap[i] = -1;
                                        dev->chanmap[i] = -1;
                                        dev->mdm.info[i].isdn_driver = -1;
                                        dev->mdm.info[i].isdn_channel = -1;
                                        isdn_info_update();
                                }
                        dev->drivers--;
                        dev->channels -= dev->drv[di]->channels;
                        kfree(dev->drv[di]->rcverr);
                        kfree(dev->drv[di]->rcvcount);
                        for (i = 0; i < dev->drv[di]->channels; i++)
                                isdn_free_queue(&dev->drv[di]->rpqueue[i]);
                        kfree(dev->drv[di]->rcv_waitq);
                        kfree(dev->drv[di]->snd_waitq);
                        kfree(dev->drv[di]);
                        dev->drv[di] = NULL;
                        dev->drvid[di][0] = '\0';
                        isdn_info_update();
                        restore_flags(flags);
                        return 0;
                default:
                        return -1;
	}
	return 0;
}

/*
 * Get integer from char-pointer, set pointer to end of number
 */
int isdn_getnum(char **p)
{
	int v = -1;

	while (*p[0] >= '0' && *p[0] <= '9')
		v = ((v < 0) ? 0 : (v * 10)) + (int) ((*p[0]++) - '0');
	return v;
}

int isdn_readbchan(int di, int channel, u_char * buf, u_char * fp, int len, int user)
{
	int avail;
	int left;
	int count;
	int copy_l;
	int dflag;
	int flags;
	pqueue *p;
	u_char *cp;

	if (!dev->drv[di]->rpqueue[channel]) {
		if (user)
			interruptible_sleep_on(&dev->drv[di]->rcv_waitq[channel]);
		else
			return 0;
	}
	if (!dev->drv[di])
		return 0;
	save_flags(flags);
	cli();
	avail = dev->drv[di]->rcvcount[channel];
	restore_flags(flags);
	left = MIN(len, avail);
	cp = buf;
	count = 0;
	while (left) {
		if ((copy_l = dev->drv[di]->rpqueue[channel]->length) > left) {
			copy_l = left;
			dflag = 0;
		} else
			dflag = 1;
		p = dev->drv[di]->rpqueue[channel];
		if (user)
			memcpy_tofs(cp, p->rptr, copy_l);
		else
			memcpy(cp, p->rptr, copy_l);
		if (fp) {
			memset(fp, 0, copy_l);
			fp += copy_l;
		}
		left -= copy_l;
		count += copy_l;
		cp += copy_l;
		if (dflag) {
			if (fp)
				*(fp - 1) = 0xff;
			save_flags(flags);
			cli();
			dev->drv[di]->rpqueue[channel] = (pqueue *) p->next;
			kfree_s(p, p->size);
			restore_flags(flags);
		} else {
			p->rptr += copy_l;
			p->length -= copy_l;
		}
		save_flags(flags);
		cli();
		dev->drv[di]->rcvcount[channel] -= copy_l;
		restore_flags(flags);
	}
	return count;
}

static __inline int isdn_minor2drv(int minor)
{
	return (dev->drvmap[minor]);
}

static __inline int isdn_minor2chan(int minor)
{
	return (dev->chanmap[minor]);
}

static char *
 isdn_statstr(void)
{
	static char istatbuf[2048];
	char *p;
	int i;

	sprintf(istatbuf, "idmap:\t");
	p = istatbuf + strlen(istatbuf);
	for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
		sprintf(p, "%s ", (dev->drvmap[i] < 0) ? "-" : dev->drvid[dev->drvmap[i]]);
		p = istatbuf + strlen(istatbuf);
	}
	sprintf(p, "\nchmap:\t");
	p = istatbuf + strlen(istatbuf);
	for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
		sprintf(p, "%d ", dev->chanmap[i]);
		p = istatbuf + strlen(istatbuf);
	}
	sprintf(p, "\ndrmap:\t");
	p = istatbuf + strlen(istatbuf);
	for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
		sprintf(p, "%d ", dev->drvmap[i]);
		p = istatbuf + strlen(istatbuf);
	}
	sprintf(p, "\nusage:\t");
	p = istatbuf + strlen(istatbuf);
	for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
		sprintf(p, "%d ", dev->usage[i]);
		p = istatbuf + strlen(istatbuf);
	}
	sprintf(p, "\nflags:\t");
	p = istatbuf + strlen(istatbuf);
	for (i = 0; i < ISDN_MAX_DRIVERS; i++) {
		if (dev->drv[i]) {
			sprintf(p, "%ld ", dev->drv[i]->flags);
			p = istatbuf + strlen(istatbuf);
		} else {
			sprintf(p, "? ");
			p = istatbuf + strlen(istatbuf);
		}
	}
	sprintf(p, "\nphone:\t");
	p = istatbuf + strlen(istatbuf);
	for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
		sprintf(p, "%s ", dev->num[i]);
		p = istatbuf + strlen(istatbuf);
	}
	sprintf(p, "\n");
	return istatbuf;
}

/* Module interface-code */

void isdn_info_update(void)
{
	infostruct *p = dev->infochain;

	while (p) {
		*(p->private) = 1;
		p = (infostruct *) p->next;
	}
	wake_up_interruptible(&(dev->info_waitq));
}

static int isdn_read(struct inode *inode, struct file *file, char *buf, int count)
{
	uint minor = MINOR(inode->i_rdev);
	int len = 0;
	ulong flags;
	int drvidx;
	int chidx;

	if (minor == ISDN_MINOR_STATUS) {
		char *p;
		if (!file->private_data) {
                        if (file->f_flags & O_NONBLOCK)
                                return -EAGAIN;
			interruptible_sleep_on(&(dev->info_waitq));
                }
		save_flags(flags);
		p = isdn_statstr();
		restore_flags(flags);
		file->private_data = 0;
		if ((len = strlen(p)) <= count) {
			memcpy_tofs(buf, p, len);
			file->f_pos += len;
			return len;
		}
		return 0;
	}
	if (!dev->drivers)
		return -ENODEV;
	if (minor < ISDN_MINOR_CTRL) {
		drvidx = isdn_minor2drv(minor);
		if (drvidx < 0)
			return -ENODEV;
		if (!dev->drv[drvidx]->running)
			return -ENODEV;
		chidx = isdn_minor2chan(minor);
		len = isdn_readbchan(drvidx, chidx, buf, 0, count, 1);
		file->f_pos += len;
		return len;
	}
	if (minor <= ISDN_MINOR_CTRLMAX) {
		drvidx = isdn_minor2drv(minor - ISDN_MINOR_CTRL);
		if (drvidx < 0)
			return -ENODEV;
		if (!dev->drv[drvidx]->stavail) {
                        if (file->f_flags & O_NONBLOCK)
                                return -EAGAIN;
			interruptible_sleep_on(&(dev->drv[drvidx]->st_waitq));
                }
		if (dev->drv[drvidx]->interface->readstat)
			len = dev->drv[drvidx]->interface->
			    readstat(buf, MIN(count, dev->drv[drvidx]->stavail), 1);
		else
			len = 0;
		save_flags(flags);
		cli();
                if (len)
                        dev->drv[drvidx]->stavail -= len;
                else
                        dev->drv[drvidx]->stavail = 0;
		restore_flags(flags);
		file->f_pos += len;
		return len;
	}
#ifdef CONFIG_ISDN_PPP
	if (minor <= ISDN_MINOR_PPPMAX)
		return (isdn_ppp_read(minor - ISDN_MINOR_PPP, file, buf, count));
#endif
	return -ENODEV;
}

static int isdn_lseek(struct inode *inode, struct file *file, off_t offset, int orig)
{
	return -ESPIPE;
}

static int isdn_write(struct inode *inode, struct file *file, const char *buf, int count)
{
	uint minor = MINOR(inode->i_rdev);
	int drvidx;
	int chidx;

	if (minor == ISDN_MINOR_STATUS)
		return -EPERM;
	if (!dev->drivers)
		return -ENODEV;
	if (minor < ISDN_MINOR_CTRL) {
		drvidx = isdn_minor2drv(minor);
		if (drvidx < 0)
			return -ENODEV;
		if (!dev->drv[drvidx]->running)
			return -ENODEV;
		chidx = isdn_minor2chan(minor);
                dev->obytes[minor] += count;
		while (isdn_writebuf_stub(drvidx, chidx, buf, count, 1) != count)
			interruptible_sleep_on(&dev->drv[drvidx]->snd_waitq[chidx]);
		return count;
	}
	if (minor <= ISDN_MINOR_CTRLMAX) {
		drvidx = isdn_minor2drv(minor - ISDN_MINOR_CTRL);
		if (drvidx < 0)
			return -ENODEV;
		/*
		 * We want to use the isdnctrl device to load the firmware
		 *
		 if (!dev->drv[drvidx]->running)
		 return -ENODEV;
		 */
		if (dev->drv[drvidx]->interface->writecmd)
			return (dev->drv[drvidx]->interface->writecmd(buf, count, 1));
		else
			return count;
	}
#ifdef CONFIG_ISDN_PPP
	if (minor <= ISDN_MINOR_PPPMAX)
		return (isdn_ppp_write(minor - ISDN_MINOR_PPP, file, buf, count));
#endif
	return -ENODEV;
}

static int isdn_select(struct inode *inode, struct file *file, int type, select_table * st)
{
	uint minor = MINOR(inode->i_rdev);

	if (minor == ISDN_MINOR_STATUS) {
		if (file->private_data)
			return 1;
		else {
			if (st)
				select_wait(&(dev->info_waitq), st);
			return 0;
		}
	}
	if (minor <= ISDN_MINOR_CTRLMAX)
		return 1;
#ifdef CONFIG_ISDN_PPP
	if (minor <= ISDN_MINOR_PPPMAX)
		return (isdn_ppp_select(minor - ISDN_MINOR_PPP, file, type, st));
#endif
	return -ENODEV;
}

static int isdn_set_allcfg(char *src)
{
	int ret;
	int i;
	ulong flags;
	char buf[1024];
	isdn_net_ioctl_cfg cfg;
	isdn_net_ioctl_phone phone;

	if ((ret = isdn_net_rmall()))
		return ret;
	save_flags(flags);
	cli();
	if ((ret = verify_area(VERIFY_READ, (void *) src, sizeof(int)))) {
		restore_flags(flags);
		return ret;
	}
	memcpy_tofs((char *) &i, src, sizeof(int));
	while (i) {
		char *c;
		char *c2;

		if ((ret = verify_area(VERIFY_READ, (void *) src, sizeof(cfg)))) {
			restore_flags(flags);
			return ret;
		}
		memcpy_tofs((char *) &cfg, src, sizeof(cfg));
		src += sizeof(cfg);
		if (!isdn_net_new(cfg.name, NULL)) {
			restore_flags(flags);
			return -EIO;
		}
		if ((ret = isdn_net_setcfg(&cfg))) {
			restore_flags(flags);
			return ret;
		}
		if ((ret = verify_area(VERIFY_READ, (void *) src, sizeof(buf)))) {
			restore_flags(flags);
			return ret;
		}
		memcpy_fromfs(buf, src, sizeof(buf));
		src += sizeof(buf);
		c = buf;
		while (*c) {
			if ((c2 = strchr(c, ' ')))
				*c2++ = '\0';
			strcpy(phone.phone, c);
			strcpy(phone.name, cfg.name);
			phone.outgoing = 0;
			if ((ret = isdn_net_addphone(&phone))) {
				restore_flags(flags);
				return ret;
			}
			if (c2)
				c = c2;
			else
				c += strlen(c);
		}
		if ((ret = verify_area(VERIFY_READ, (void *) src, sizeof(buf)))) {
			restore_flags(flags);
			return ret;
		}
		memcpy_fromfs(buf, src, sizeof(buf));
		src += sizeof(buf);
		c = buf;
		while (*c) {
			if ((c2 = strchr(c, ' ')))
				*c2++ = '\0';
			strcpy(phone.phone, c);
			strcpy(phone.name, cfg.name);
			phone.outgoing = 1;
			if ((ret = isdn_net_addphone(&phone))) {
				restore_flags(flags);
				return ret;
			}
			if (c2)
				c = c2;
			else
				c += strlen(c);
		}
		i--;
	}
	restore_flags(flags);
	return 0;
}

static int isdn_get_allcfg(char *dest)
{
	isdn_net_ioctl_cfg cfg;
	isdn_net_ioctl_phone phone;
	isdn_net_dev *p;
	ulong flags;
	int ret;

	/* Walk through netdev-chain */
	save_flags(flags);
	cli();
	p = dev->netdev;
	while (p) {
		if ((ret = verify_area(VERIFY_WRITE, (void *) dest, sizeof(cfg) + 10))) {
			restore_flags(flags);
			return ret;
		}
		strcpy(cfg.eaz, p->local.msn);
		cfg.exclusive = p->local.exclusive;
		if (p->local.pre_device >= 0) {
			sprintf(cfg.drvid, "%s,%d", dev->drvid[p->local.pre_device],
				p->local.pre_channel);
		} else
			cfg.drvid[0] = '\0';
		cfg.onhtime = p->local.onhtime;
		cfg.charge = p->local.charge;
		cfg.l2_proto = p->local.l2_proto;
		cfg.l3_proto = p->local.l3_proto;
		cfg.p_encap = p->local.p_encap;
		cfg.secure = (p->local.flags & ISDN_NET_SECURE) ? 1 : 0;
		cfg.callback = (p->local.flags & ISDN_NET_CALLBACK) ? 1 : 0;
		cfg.chargehup = (p->local.hupflags & 4) ? 1 : 0;
		cfg.ihup = (p->local.hupflags & 8) ? 1 : 0;
		memcpy_tofs(dest, p->local.name, 10);
		dest += 10;
		memcpy_tofs(dest, (char *) &cfg, sizeof(cfg));
		dest += sizeof(cfg);
		strcpy(phone.name, p->local.name);
		phone.outgoing = 0;
		if ((ret = isdn_net_getphones(&phone, dest)) < 0) {
			restore_flags(flags);
			return ret;
		} else
			dest += ret;
		strcpy(phone.name, p->local.name);
		phone.outgoing = 1;
		if ((ret = isdn_net_getphones(&phone, dest)) < 0) {
			restore_flags(flags);
			return ret;
		} else
			dest += ret;
		p = p->next;
	}
	restore_flags(flags);
	return 0;
}

static int isdn_ioctl(struct inode *inode, struct file *file, uint cmd, ulong arg)
{
	uint minor = MINOR(inode->i_rdev);
	isdn_ctrl c;
	int drvidx;
	int chidx;
	int ret;
	char *s;
	char name[10];
	char bname[21];
	isdn_ioctl_struct iocts;
	isdn_net_ioctl_phone phone;
	isdn_net_ioctl_cfg cfg;

	if (minor == ISDN_MINOR_STATUS) {
                switch (cmd) {
                        case IIOCGETCPS:
                                if (arg) {
                                        ulong *p = (ulong *)arg;
                                        int i;
                                        if ((ret = verify_area(VERIFY_WRITE, (void *) arg,
                                                               sizeof(ulong)*ISDN_MAX_CHANNELS*2)))
                                                return ret;
                                        for (i = 0;i<ISDN_MAX_CHANNELS;i++) {
                                                put_fs_long(dev->ibytes[i],p++);
                                                put_fs_long(dev->obytes[i],p++);
                                        }
                                        return 0;
                                } else
                                        return -EINVAL;
                                break;
                        default:
		                return -EINVAL;
                }
        }
	if (!dev->drivers)
		return -ENODEV;
	if (minor < ISDN_MINOR_CTRL) {
		drvidx = isdn_minor2drv(minor);
		if (drvidx < 0)
			return -ENODEV;
		chidx = isdn_minor2chan(minor);
		if (!dev->drv[drvidx]->running)
			return -ENODEV;
		return 0;
	}
	if (minor <= ISDN_MINOR_CTRLMAX) {
		switch (cmd) {
#ifdef CONFIG_NETDEVICES
                        case IIOCNETAIF:
                                /* Add a network-interface */
                                if (arg) {
                                        if ((ret = verify_area(VERIFY_READ, (void *) arg, sizeof(name))))
                                                return ret;
                                        memcpy_fromfs(name, (char *) arg, sizeof(name));
                                        s = name;
                                } else
                                        s = NULL;
                                if ((s = isdn_net_new(s, NULL))) {
                                        if ((ret = verify_area(VERIFY_WRITE, (void *) arg, strlen(s) + 1)))
                                                return ret;
                                        memcpy_tofs((char *) arg, s, strlen(s) + 1);
                                        return 0;
                                } else
                                        return -ENODEV;
                        case IIOCNETASL:
                                /* Add a slave to a network-interface */
                                if (arg) {
                                        if ((ret = verify_area(VERIFY_READ, (void *) arg, sizeof(bname))))
                                                return ret;
                                        memcpy_fromfs(bname, (char *) arg, sizeof(bname));
                                } else
                                        return -EINVAL;
                                if ((s = isdn_net_newslave(bname))) {
                                        if ((ret = verify_area(VERIFY_WRITE, (void *) arg, strlen(s) + 1)))
                                                return ret;
                                        memcpy_tofs((char *) arg, s, strlen(s) + 1);
                                        return 0;
                                } else
                                        return -ENODEV;
                        case IIOCNETDIF:
                                /* Delete a network-interface */
                                if (arg) {
                                        if ((ret = verify_area(VERIFY_READ, (void *) arg, sizeof(name))))
                                                return ret;
                                        memcpy_fromfs(name, (char *) arg, sizeof(name));
                                        return isdn_net_rm(name);
                                } else
                                        return -EINVAL;
                        case IIOCNETSCF:
                                /* Set configurable parameters of a network-interface */
                                if (arg) {
                                        if ((ret = verify_area(VERIFY_READ, (void *) arg, sizeof(cfg))))
                                                return ret;
                                        memcpy_fromfs((char *) &cfg, (char *) arg, sizeof(cfg));
                                        return isdn_net_setcfg(&cfg);
                                } else
                                        return -EINVAL;
                        case IIOCNETGCF:
                                /* Get configurable parameters of a network-interface */
                                if (arg) {
                                        if ((ret = verify_area(VERIFY_READ, (void *) arg, sizeof(cfg))))
                                                return ret;
                                        memcpy_fromfs((char *) &cfg, (char *) arg, sizeof(cfg));
                                        if (!(ret = isdn_net_getcfg(&cfg))) {
                                                if ((ret = verify_area(VERIFY_WRITE, (void *) arg, sizeof(cfg))))
                                                        return ret;
                                                memcpy_tofs((char *) arg, (char *) &cfg, sizeof(cfg));
                                        }
                                        return ret;
                                } else
                                        return -EINVAL;
                        case IIOCNETANM:
                                /* Add a phone-number to a network-interface */
                                if (arg) {
                                        if ((ret = verify_area(VERIFY_READ, (void *) arg, sizeof(phone))))
                                                return ret;
                                        memcpy_fromfs((char *) &phone, (char *) arg, sizeof(phone));
                                        return isdn_net_addphone(&phone);
                                } else
                                        return -EINVAL;
                        case IIOCNETGNM:
                                /* Get list of phone-numbers of a network-interface */
                                if (arg) {
                                        if ((ret = verify_area(VERIFY_READ, (void *) arg, sizeof(phone))))
                                                return ret;
                                        memcpy_fromfs((char *) &phone, (char *) arg, sizeof(phone));
                                        return isdn_net_getphones(&phone, (char *) arg);
                                } else
                                        return -EINVAL;
                        case IIOCNETDNM:
                                /* Delete a phone-number of a network-interface */
                                if (arg) {
                                        memcpy_fromfs((char *) &phone, (char *) arg, sizeof(phone));
                                        if ((ret = verify_area(VERIFY_READ, (void *) arg, sizeof(phone))))
                                                return ret;
                                        return isdn_net_delphone(&phone);
                                } else
                                        return -EINVAL;
                        case IIOCNETDIL:
                                /* Force dialing of a network-interface */
                                if (arg) {
                                        if ((ret = verify_area(VERIFY_READ, (void *) arg, sizeof(name))))
                                                return ret;
                                        memcpy_fromfs(name, (char *) arg, sizeof(name));
                                        return isdn_net_force_dial(name);
                                } else
                                        return -EINVAL;
#ifdef CONFIG_ISDN_PPP
                        case IIOCNETALN:
                                if(arg) {
                                        if ((ret = verify_area(VERIFY_READ,
                                                               (void*)arg,
                                                               sizeof(name))))
                                                return ret;
                                } else
                                        return -EINVAL;
                                memcpy_fromfs(name,(char*)arg,sizeof(name));
                                return isdn_ppp_dial_slave(name);
                        case IIOCNETDLN:
                                /* remove one link from bundle; removed for i4l 0.7.1  */
                                return 2;
#endif
                        case IIOCNETHUP:
                                /* Force hangup of a network-interface */
                                if (arg) {
                                        if ((ret = verify_area(VERIFY_READ, (void *) arg, sizeof(name))))
                                                return ret;
                                        memcpy_fromfs(name, (char *) arg, sizeof(name));
                                        return isdn_net_force_hangup(name);
                                } else
                                        return -EINVAL;
                                break;
#endif				/* CONFIG_NETDEVICES */
                        case IIOCSETVER:
                                dev->net_verbose = arg;
                                printk(KERN_INFO "isdn: Verbose-Level is %d\n", dev->net_verbose);
                                return 0;
                        case IIOCSETGST:
                                if (arg)
                                        dev->global_flags |= ISDN_GLOBAL_STOPPED;
                                else
                                        dev->global_flags &= ~ISDN_GLOBAL_STOPPED;
                                printk(KERN_INFO "isdn: Global Mode %s\n",
                                       (dev->global_flags & ISDN_GLOBAL_STOPPED) ? "stopped" : "running");
                                return 0;
                        case IIOCSETBRJ:
                                drvidx = -1;
                                if (arg) {
                                        int i;
                                        char *p;
                                        if ((ret = verify_area(VERIFY_READ, (void *) arg,
                                                               sizeof(isdn_ioctl_struct))))
                                                return ret;
                                        memcpy_fromfs((char *) &iocts, (char *) arg, sizeof(isdn_ioctl_struct));
                                        if (strlen(iocts.drvid)) {
                                                if ((p = strchr(iocts.drvid, ',')))
                                                        *p = 0;
                                                drvidx = -1;
                                                for (i = 0; i < ISDN_MAX_DRIVERS; i++)
                                                        if (!(strcmp(dev->drvid[i], iocts.drvid))) {
                                                                drvidx = i;
                                                                break;
                                                        }
                                        }
                                }
                                if (drvidx == -1)
                                        return -ENODEV;
                                dev->drv[drvidx]->reject_bus = iocts.arg;
                                return 0;
                        case IIOCGETSET:
                                /* Get complete setup (all network-interfaces and profile-
                                   settings of all tty-devices */
                                if (arg)
                                        return (isdn_get_allcfg((char *) arg));
                                else
                                        return -EINVAL;
                                break;
                        case IIOCSETSET:
                                /* Set complete setup (all network-interfaces and profile-
                                   settings of all tty-devices */
                                if (arg)
                                        return (isdn_set_allcfg((char *) arg));
                                else
                                        return -EINVAL;
                                break;
                        case IIOCSIGPRF:
                                dev->profd = current;
                                return 0;
                                break;
                        case IIOCGETPRF:
                                /* Get all Modem-Profiles */
                                if (arg) {
                                        char *p = (char *) arg;
                                        int i;
                                        
                                        if ((ret = verify_area(VERIFY_WRITE, (void *) arg,
                                                               (ISDN_MODEM_ANZREG + ISDN_MSNLEN)
                                                               * ISDN_MAX_CHANNELS)))
                                                return ret;
                                        
                                        for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
                                                memcpy_tofs(p, dev->mdm.atmodem[i].profile, ISDN_MODEM_ANZREG);
                                                p += ISDN_MODEM_ANZREG;
                                                memcpy_tofs(p, dev->mdm.atmodem[i].pmsn, ISDN_MSNLEN);
                                                p += ISDN_MSNLEN;
                                        }
                                        return (ISDN_MODEM_ANZREG + ISDN_MSNLEN) * ISDN_MAX_CHANNELS;
                                } else
                                        return -EINVAL;
                                break;
                        case IIOCSETPRF:
                                /* Set all Modem-Profiles */
                                if (arg) {
                                        char *p = (char *) arg;
                                        int i;
                                        
                                        if ((ret = verify_area(VERIFY_READ, (void *) arg,
                                                               (ISDN_MODEM_ANZREG + ISDN_MSNLEN)
                                                               * ISDN_MAX_CHANNELS)))
                                                return ret;
                                        
                                        for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
                                                memcpy_fromfs(dev->mdm.atmodem[i].profile, p, ISDN_MODEM_ANZREG);
                                                p += ISDN_MODEM_ANZREG;
                                                memcpy_fromfs(dev->mdm.atmodem[i].pmsn, p, ISDN_MSNLEN);
                                                p += ISDN_MSNLEN;
                                        }
                                        return 0;
                                } else
                                        return -EINVAL;
                                break;
                        case IIOCSETMAP:
                        case IIOCGETMAP:
                                /* Set/Get MSN->EAZ-Mapping for a driver */
                                if (arg) {
                                        int i;
                                        char *p;
                                        char nstring[255];
                                        
                                        if ((ret = verify_area(VERIFY_READ, (void *) arg,
                                                               sizeof(isdn_ioctl_struct))))
                                                return ret;
                                        memcpy_fromfs((char *) &iocts, (char *) arg, sizeof(isdn_ioctl_struct));
                                        if (strlen(iocts.drvid)) {
                                                drvidx = -1;
                                                for (i = 0; i < ISDN_MAX_DRIVERS; i++)
                                                        if (!(strcmp(dev->drvid[i], iocts.drvid))) {
                                                                drvidx = i;
                                                                break;
                                                        }
                                        } else
                                                drvidx = 0;
                                        if (drvidx == -1)
                                                return -ENODEV;
                                        if (cmd == IIOCSETMAP) {
                                                if ((ret = verify_area(VERIFY_READ, (void *) iocts.arg, 255)))
                                                        return ret;
                                                memcpy_fromfs(nstring, (char *) iocts.arg, 255);
                                                memset(dev->drv[drvidx]->msn2eaz, 0,
                                                       sizeof(dev->drv[drvidx]->msn2eaz));
                                                p = strtok(nstring, ",");
                                                i = 0;
                                                while ((p) && (i < 10)) {
                                                        strcpy(dev->drv[drvidx]->msn2eaz[i++], p);
                                                        p = strtok(NULL, ",");
                                                }
                                        } else {
                                                p = nstring;
                                                for (i = 0; i < 10; i++)
                                                        p += sprintf(p, "%s%s",
                                                                     strlen(dev->drv[drvidx]->msn2eaz[i]) ?
                                                                     dev->drv[drvidx]->msn2eaz[i] : "-",
                                                                     (i < 9) ? "," : "\0");
                                                if ((ret = verify_area(VERIFY_WRITE, (void *) iocts.arg,
                                                                       strlen(nstring) + 1)))
                                                        return ret;
                                                memcpy_tofs((char *) iocts.arg, nstring, strlen(nstring) + 1);
                                        }
                                        return 0;
                                } else
                                        return -EINVAL;
                        case IIOCDBGVAR:
                                if (arg) {
                                        if ((ret = verify_area(VERIFY_WRITE, (void *) arg, sizeof(ulong))))
                                                return ret;
                                        memcpy_tofs((char *) arg, (char *) &dev, sizeof(ulong));
                                        return 0;
                                } else
                                        return -EINVAL;
                                break;
                        default:
                                if ((cmd&IIOCDRVCTL)==IIOCDRVCTL)
                                        cmd = ((cmd>>_IOC_NRSHIFT)&_IOC_NRMASK)& ISDN_DRVIOCTL_MASK;
				else
					return -EINVAL;
                                if (arg) {
                                        int i;
                                        char *p;
                                        if ((ret = verify_area(VERIFY_READ, (void *) arg,
                                                               sizeof(isdn_ioctl_struct))))
                                                return ret;
                                        memcpy_fromfs((char *) &iocts, (char *) arg, sizeof(isdn_ioctl_struct));
                                        if (strlen(iocts.drvid)) {
                                                if ((p = strchr(iocts.drvid, ',')))
                                                        *p = 0;
                                                drvidx = -1;
                                                for (i = 0; i < ISDN_MAX_DRIVERS; i++)
                                                        if (!(strcmp(dev->drvid[i], iocts.drvid))) {
                                                                drvidx = i;
                                                                break;
                                                        }
                                        } else
                                                drvidx = 0;
                                        if (drvidx == -1)
                                                return -ENODEV;
                                        if ((ret = verify_area(VERIFY_WRITE, (void *) arg,
                                                               sizeof(isdn_ioctl_struct))))
                                                return ret;
                                        c.driver = drvidx;
                                        c.command = ISDN_CMD_IOCTL;
                                        c.arg = cmd;
                                        memcpy(c.num, (char *) &iocts.arg, sizeof(ulong));
                                        ret = dev->drv[drvidx]->interface->command(&c);
                                        memcpy((char *) &iocts.arg, c.num, sizeof(ulong));
                                        memcpy_tofs((char *) arg, &iocts, sizeof(isdn_ioctl_struct));
                                        return ret;
                                } else
                                        return -EINVAL;
		}
	}
#ifdef CONFIG_ISDN_PPP
	if (minor <= ISDN_MINOR_PPPMAX)
		return (isdn_ppp_ioctl(minor - ISDN_MINOR_PPP, file, cmd, arg));
#endif
	return -ENODEV;
}

/*
 * Open the device code.
 * MOD_INC_USE_COUNT make sure that the driver memory is not freed
 * while the device is in use.
 */
static int isdn_open(struct inode *ino, struct file *filep)
{
	uint minor = MINOR(ino->i_rdev);
	int drvidx;
	int chidx;
	isdn_ctrl c;

	if (minor == ISDN_MINOR_STATUS) {
		infostruct *p;
		if ((p = (infostruct *) kmalloc(sizeof(infostruct), GFP_KERNEL))) {
			MOD_INC_USE_COUNT;
			p->next = (char *) dev->infochain;
			p->private = (char *) &(filep->private_data);
			dev->infochain = p;
			/* At opening we allow a single update */
			filep->private_data = (char *) 1;
			return 0;
		} else
			return -ENOMEM;
	}
	if (!dev->channels)
		return -ENODEV;
	if (minor < ISDN_MINOR_CTRL) {
		drvidx = isdn_minor2drv(minor);
		if (drvidx < 0)
			return -ENODEV;
		chidx = isdn_minor2chan(minor);
		if (!dev->drv[drvidx]->running)
			return -ENODEV;
		if (!(dev->drv[drvidx]->flags & (1 << chidx)))
			return -ENODEV;
		c.command = ISDN_CMD_LOCK;
		c.driver = drvidx;
		(void) dev->drv[drvidx]->interface->command(&c);
		MOD_INC_USE_COUNT;
		return 0;
	}
	if (minor <= ISDN_MINOR_CTRLMAX) {
		drvidx = isdn_minor2drv(minor - ISDN_MINOR_CTRL);
		if (drvidx < 0)
			return -ENODEV;
		c.command = ISDN_CMD_LOCK;
		c.driver = drvidx;
		MOD_INC_USE_COUNT;
		(void) dev->drv[drvidx]->interface->command(&c);
		return 0;
	}
#ifdef CONFIG_ISDN_PPP
	if (minor <= ISDN_MINOR_PPPMAX) {
		int ret;
		if (!(ret = isdn_ppp_open(minor - ISDN_MINOR_PPP, filep)))
		        MOD_INC_USE_COUNT;
		return ret;
	}
#endif
	return -ENODEV;
}

static void isdn_close(struct inode *ino, struct file *filep)
{
	uint minor = MINOR(ino->i_rdev);
	int drvidx;
	isdn_ctrl c;

	if (minor == ISDN_MINOR_STATUS) {
		infostruct *p = dev->infochain;
		infostruct *q = NULL;
                MOD_DEC_USE_COUNT;
		while (p) {
			if (p->private == (char *) &(filep->private_data)) {
				if (q)
					q->next = p->next;
				else
					dev->infochain = (infostruct *) (p->next);
				return;
			}
			q = p;
			p = (infostruct *) (p->next);
		}
		printk(KERN_WARNING "isdn: No private data while closing isdnctrl\n");
                return;
	}
	if (!dev->channels)
		return;
	MOD_DEC_USE_COUNT;
	if (minor < ISDN_MINOR_CTRL) {
		drvidx = isdn_minor2drv(minor);
		if (drvidx < 0)
			return;
		c.command = ISDN_CMD_UNLOCK;
		c.driver = drvidx;
		(void) dev->drv[drvidx]->interface->command(&c);
		return;
	}
	if (minor <= ISDN_MINOR_CTRLMAX) {
		drvidx = isdn_minor2drv(minor - ISDN_MINOR_CTRL);
		if (drvidx < 0)
			return;
		if (dev->profd == current)
			dev->profd = NULL;
		c.command = ISDN_CMD_UNLOCK;
		c.driver = drvidx;
		(void) dev->drv[drvidx]->interface->command(&c);
		return;
	}
#ifdef CONFIG_ISDN_PPP
	if (minor <= ISDN_MINOR_PPPMAX)
		isdn_ppp_release(minor - ISDN_MINOR_PPP, filep);
#endif
}

static struct file_operations isdn_fops =
{
	isdn_lseek,
	isdn_read,
	isdn_write,
	NULL,			/* isdn_readdir */
	isdn_select,		/* isdn_select */
	isdn_ioctl,		/* isdn_ioctl */
	NULL,			/* isdn_mmap */
	isdn_open,
	isdn_close,
	NULL			/* fsync */
};

char *
 isdn_map_eaz2msn(char *msn, int di)
{
	driver *this = dev->drv[di];
	int i;

	if (strlen(msn) == 1) {
		i = msn[0] - '0';
		if ((i >= 0) && (i <= 9))
			if (strlen(this->msn2eaz[i]))
				return (this->msn2eaz[i]);
	}
	return (msn);
}

/*
 * Find an unused ISDN-channel, whose feature-flags match the
 * given L2- and L3-protocols.
 */
int isdn_get_free_channel(int usage, int l2_proto, int l3_proto, int pre_dev
		     ,int pre_chan)
{
	int i;
	ulong flags;
	ulong features;

	save_flags(flags);
	cli();
	features = (1 << l2_proto) | (0x100 << l3_proto);
	for (i = 0; i < ISDN_MAX_CHANNELS; i++)
		if (USG_NONE(dev->usage[i]) &&
		    (dev->drvmap[i] != -1)) {
			int d = dev->drvmap[i];
			if ((dev->usage[i] & ISDN_USAGE_EXCLUSIVE) &&
			((pre_dev != d) || (pre_chan != dev->chanmap[i])))
				continue;
			if ((dev->drv[d]->running)) {
				if ((dev->drv[d]->interface->features & features) == features) {
					if ((pre_dev < 0) || (pre_chan < 0)) {
						dev->usage[i] &= ISDN_USAGE_EXCLUSIVE;
						dev->usage[i] |= usage;
						isdn_info_update();
						restore_flags(flags);
						return i;
					} else {
						if ((pre_dev == d) && (pre_chan == dev->chanmap[i])) {
							dev->usage[i] &= ISDN_USAGE_EXCLUSIVE;
							dev->usage[i] |= usage;
							isdn_info_update();
							restore_flags(flags);
							return i;
						}
					}
				}
			}
		}
	restore_flags(flags);
	return -1;
}

/*
 * Set state of ISDN-channel to 'unused'
 */
void isdn_free_channel(int di, int ch, int usage)
{
	int i;
	ulong flags;

	save_flags(flags);
	cli();
	for (i = 0; i < ISDN_MAX_CHANNELS; i++)
		if (((dev->usage[i] & ISDN_USAGE_MASK) == usage) &&
		    (dev->drvmap[i] == di) &&
		    (dev->chanmap[i] == ch)) {
			dev->usage[i] &= (ISDN_USAGE_NONE | ISDN_USAGE_EXCLUSIVE);
			strcpy(dev->num[i], "???");
                        dev->ibytes[i] = 0;
                        dev->obytes[i] = 0;
			isdn_info_update();
			restore_flags(flags);
			return;
		}
	restore_flags(flags);
}

/*
 * Cancel Exclusive-Flag for ISDN-channel
 */
void isdn_unexclusive_channel(int di, int ch)
{
	int i;
	ulong flags;

	save_flags(flags);
	cli();
	for (i = 0; i < ISDN_MAX_CHANNELS; i++)
		if ((dev->drvmap[i] == di) &&
		    (dev->chanmap[i] == ch)) {
			dev->usage[i] &= ~ISDN_USAGE_EXCLUSIVE;
			isdn_info_update();
			restore_flags(flags);
			return;
		}
	restore_flags(flags);
}

/*
 *  receive callback handler for drivers supporting sk_buff's.
 */

void isdn_receive_skb_callback(int drvidx, int chan, struct sk_buff *skb) 
{
        int i, len;

	if (dev->global_flags & ISDN_GLOBAL_STOPPED)
		return;
        if ((i = isdn_dc2minor(drvidx,chan))==-1)
                return;
        len = skb->len;
	if (isdn_net_rcv_skb(i, skb) == 0) {
		isdn_receive_callback(drvidx, chan, skb->data, skb->len);
		skb->free = 1;
		kfree_skb(skb, FREE_READ);
	} else
                /* Update statistics */
                dev->ibytes[i] += len;
}

/*
 *  writebuf replacement for SKB_ABLE drivers
 */

int isdn_writebuf_stub(int drvidx, int chan, const u_char *buf, int len, 
		       int user)
{
        if (dev->drv[drvidx]->interface->writebuf)
          return dev->drv[drvidx]->interface->writebuf(drvidx, chan, buf,
                                                       len, user);
        else {
          struct sk_buff * skb;

          skb = alloc_skb(dev->drv[drvidx]->interface->hl_hdrlen + len, GFP_ATOMIC);
          if (skb == NULL)
                  return 0;

          skb_reserve(skb, dev->drv[drvidx]->interface->hl_hdrlen);
          skb->free = 1;

          if (user)
                  memcpy_fromfs(skb_put(skb, len), buf, len);
          else
                  memcpy(skb_put(skb, len), buf, len);

          return dev->drv[drvidx]->interface->writebuf_skb(drvidx, chan, skb);
        }
}

/*
 * writebuf_skb replacement for NON SKB_ABLE drivers
 * If lowlevel-device does not support supports skbufs, use
 * standard send-routine, else sind directly.
 *
 * Return: length of data on success, -ERRcode on failure.
 */

int isdn_writebuf_skb_stub(int drvidx, int chan, struct sk_buff * skb)
{
        int ret;

        if (dev->drv[drvidx]->interface->writebuf_skb) 
		ret = dev->drv[drvidx]->interface->
			writebuf_skb(drvidx, chan, skb);
	else {	      
		if ((ret = dev->drv[drvidx]->interface->
                     writebuf(drvidx,chan,skb->data,skb->len,0))==skb->len)
		        dev_kfree_skb(skb, FREE_WRITE);
        }
        return ret;
}

/*
 * Low-level-driver registration
 */

int register_isdn(isdn_if * i)
{
	driver *d;
	int n, j, k;
	ulong flags;
	int drvidx;

	if (dev->drivers >= ISDN_MAX_DRIVERS) {
		printk(KERN_WARNING "register_isdn: Max. %d drivers supported\n",
		       ISDN_MAX_DRIVERS);
		return 0;
	}
	n = i->channels;
	if (dev->channels + n > ISDN_MAX_CHANNELS) {
		printk(KERN_WARNING "register_isdn: Max. %d channels supported\n",
		       ISDN_MAX_CHANNELS);
		return 0;
	}
	if ((!i->writebuf_skb) && (!i->writebuf)) {
                printk(KERN_WARNING "register_isdn: No write routine given.\n");
                return 0;
        }
	if (!(d = (driver *) kmalloc(sizeof(driver), GFP_KERNEL))) {
		printk(KERN_WARNING "register_isdn: Could not alloc driver-struct\n");
		return 0;
	}
	memset((char *) d, 0, sizeof(driver));
	if (!(d->rcverr = (int *) kmalloc(sizeof(int) * n, GFP_KERNEL))) {
		printk(KERN_WARNING "register_isdn: Could not alloc rcverr\n");
		kfree(d);
		return 0;
	}
	memset((char *) d->rcverr, 0, sizeof(int) * n);
	if (!(d->rcvcount = (int *) kmalloc(sizeof(int) * n, GFP_KERNEL))) {
		printk(KERN_WARNING "register_isdn: Could not alloc rcvcount\n");
		kfree(d->rcverr);
		kfree(d);
		return 0;
	}
	memset((char *) d->rcvcount, 0, sizeof(int) * n);
	if (!(d->rpqueue = (pqueue **) kmalloc(sizeof(pqueue *) * n, GFP_KERNEL))) {
		printk(KERN_WARNING "register_isdn: Could not alloc rpqueue\n");
		kfree(d->rcvcount);
		kfree(d->rcverr);
		kfree(d);
		return 0;
	}
	memset((char *) d->rpqueue, 0, sizeof(pqueue *) * n);
	if (!(d->rcv_waitq = (struct wait_queue **)
	      kmalloc(sizeof(struct wait_queue *) * n, GFP_KERNEL))) {
		printk(KERN_WARNING "register_isdn: Could not alloc rcv_waitq\n");
		kfree(d->rpqueue);
		kfree(d->rcvcount);
		kfree(d->rcverr);
		kfree(d);
		return 0;
	}
	memset((char *) d->rcv_waitq, 0, sizeof(struct wait_queue *) * n);
	if (!(d->snd_waitq = (struct wait_queue **)
	      kmalloc(sizeof(struct wait_queue *) * n, GFP_KERNEL))) {
		printk(KERN_WARNING "register_isdn: Could not alloc snd_waitq\n");
		kfree(d->rcv_waitq);
		kfree(d->rpqueue);
		kfree(d->rcvcount);
		kfree(d->rcverr);
		kfree(d);
		return 0;
	}
	memset((char *) d->snd_waitq, 0, sizeof(struct wait_queue *) * n);
	d->channels = n;
	d->loaded = 1;
	d->maxbufsize = i->maxbufsize;
	d->pktcount = 0;
	d->stavail = 0;
	d->running = 0;
	d->flags = 0;
	d->interface = i;
	for (drvidx = 0; drvidx < ISDN_MAX_DRIVERS; drvidx++)
		if (!dev->drv[drvidx])
			break;
	i->channels = drvidx;
 
	i->rcvcallb_skb = isdn_receive_skb_callback;
	i->rcvcallb     = isdn_receive_callback;
	i->statcallb    = isdn_status_callback;
	if (!strlen(i->id))
		sprintf(i->id, "line%d", drvidx);
	save_flags(flags);
	cli();
	for (j = 0; j < n; j++)
		for (k = 0; k < ISDN_MAX_CHANNELS; k++)
			if (dev->chanmap[k] < 0) {
				dev->chanmap[k] = j;
				dev->drvmap[k] = drvidx;
				break;
			}
	dev->drv[drvidx] = d;
	dev->channels += n;
	strcpy(dev->drvid[drvidx], i->id);
	isdn_info_update();
	dev->drivers++;
	restore_flags(flags);
	return 1;
}

/*
 *****************************************************************************
 * And now the modules code.
 *****************************************************************************
 */

extern int printk(const char *fmt,...);

#ifdef MODULE
#define isdn_init init_module
#endif

static char *isdn_getrev(const char *revision)
{
	static char rev[20];
	char *p;

	if ((p = strchr(revision, ':'))) {
		strcpy(rev, p + 2);
		p = strchr(rev, '$');
		*--p = 0;
	} else
		strcpy(rev, "???");
	return rev;
}

static struct symbol_table isdn_syms = {
#include <linux/symtab_begin.h>
        X(register_isdn),
#include <linux/symtab_end.h>
};

static void isdn_export_syms(void)
{
        register_symtab(&isdn_syms);
        has_exported = 1;
}

/*
 * Allocate and initialize all data, register modem-devices
 */
int isdn_init(void)
{
	int i;

	sti();
	if (!(dev = (isdn_dev *) kmalloc(sizeof(isdn_dev), GFP_KERNEL))) {
		printk(KERN_WARNING "isdn: Could not allocate device-struct.\n");
		return -EIO;
	}
	memset((char *) dev, 0, sizeof(isdn_dev));
	for (i = 0; i < ISDN_MAX_CHANNELS; i++)
		dev->drvmap[i] = -1;
	for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
		dev->chanmap[i] = -1;
		dev->m_idx[i] = -1;
		strcpy(dev->num[i], "???");
	}
	if (register_chrdev(ISDN_MAJOR, "isdn", &isdn_fops)) {
		printk(KERN_WARNING "isdn: Could not register control devices\n");
		kfree(dev);
		return -EIO;
	}
	if ((i = isdn_tty_modem_init()) < 0) {
		printk(KERN_WARNING "isdn: Could not register tty devices\n");
		if (i == -3)
			tty_unregister_driver(&dev->mdm.cua_modem);
		if (i <= -2)
			tty_unregister_driver(&dev->mdm.tty_modem);
		kfree(dev);
		unregister_chrdev(ISDN_MAJOR, "isdn");
		return -EIO;
	}
#ifdef CONFIG_ISDN_PPP
	if (isdn_ppp_init() < 0) {
		printk(KERN_WARNING "isdn: Could not create PPP-device-structs\n");
		tty_unregister_driver(&dev->mdm.tty_modem);
		tty_unregister_driver(&dev->mdm.cua_modem);
		for (i = 0; i < ISDN_MAX_CHANNELS; i++)
			kfree(dev->mdm.info[i].xmit_buf - 4);
		unregister_chrdev(ISDN_MAJOR, "isdn");
		kfree(dev);
		return -EIO;
	}
#endif				/* CONFIG_ISDN_PPP */

        if (!has_exported)
                isdn_export_syms();

	printk(KERN_NOTICE "ISDN subsystem Rev: %s/%s/%s/%s",
	       isdn_getrev(isdn_revision),
	       isdn_getrev(isdn_tty_revision),
	       isdn_getrev(isdn_net_revision),
	       isdn_getrev(isdn_ppp_revision));

#ifdef MODULE
	printk(" loaded\n");
#else
	printk("\n");
	isdn_cards_init();
#endif
	isdn_info_update();
	return 0;
}

#ifdef MODULE
/*
 * Unload module
 */
void cleanup_module(void)
{
	int flags;
	int i;

#ifdef CONFIG_ISDN_PPP
	isdn_ppp_cleanup();
#endif
	save_flags(flags);
	cli();
	if (isdn_net_rmall() < 0) {
		printk(KERN_WARNING "isdn: net-device busy, remove cancelled\n");
		restore_flags(flags);
		return;
	}
	if (tty_unregister_driver(&dev->mdm.tty_modem)) {
		printk(KERN_WARNING "isdn: ttyI-device busy, remove cancelled\n");
		restore_flags(flags);
		return;
	}
	if (tty_unregister_driver(&dev->mdm.cua_modem)) {
		printk(KERN_WARNING "isdn: cui-device busy, remove cancelled\n");
		restore_flags(flags);
		return;
	}
	for (i = 0; i < ISDN_MAX_CHANNELS; i++)
		kfree(dev->mdm.info[i].xmit_buf - 4);
	if (unregister_chrdev(ISDN_MAJOR, "isdn") != 0) {
		printk(KERN_WARNING "isdn: controldevice busy, remove cancelled\n");
	} else {
		del_timer(&dev->timer);
		kfree(dev);
		printk(KERN_NOTICE "ISDN-subsystem unloaded\n");
	}
	restore_flags(flags);
}
#endif
