/* $Id icn.c,v 1.15 1996/01/10 20:57:39 fritz Exp fritz $
 *
 * ISDN low-level module for the ICN active ISDN-Card.
 *
 * Copyright 1994,95,96 by Fritz Elfert (fritz@wuemaus.franken.de)
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
 * $Log: icn.c,v $
 * Revision 1.17  1996/02/11 02:39:04  fritz
 * Increased Buffer for status-messages.
 * Removed conditionals for HDLC-firmware.
 *
 * Revision 1.16  1996/01/22 05:01:55  fritz
 * Revert to GPL.
 *
 * Revision 1.15  1996/01/10 20:57:39  fritz
 * Bugfix: Loading firmware twice caused the device stop working.
 *
 * Revision 1.14  1995/12/18  18:23:37  fritz
 * Support for ICN-2B Cards.
 * Change for supporting user-setable service-octet.
 *
 * Revision 1.13  1995/10/29  21:41:07  fritz
 * Added support for DriverId's, added Jan's patches for Kernelversions.
 *
 * Revision 1.12  1995/04/29  13:07:35  fritz
 * Added support for new Euro-ISDN-firmware
 *
 * Revision 1.11  1995/04/23  13:40:45  fritz
 * Added support for SPV's.
 * Changed Dial-Command to support MSN's on DSS1-Lines.
 *
 * Revision 1.10  1995/03/25  23:23:24  fritz
 * Changed configurable Ports, to allow settings for DIP-Switch Cardversions.
 *
 * Revision 1.9  1995/03/25  23:17:30  fritz
 * Fixed race-condition in pollbchan_send
 *
 * Revision 1.8  1995/03/15  12:49:44  fritz
 * Added support for SPV's
 * Splitted pollbchan_work ifor calling send-routine directly
 *
 * Revision 1.7  1995/02/20  03:48:03  fritz
 * Added support of new request_region-function.
 * Minor bugfixes.
 *
 * Revision 1.6  1995/01/31  15:48:45  fritz
 * Added Cause-Messages to be signaled to upper layers.
 * Added Revision-Info on load.
 *
 * Revision 1.5  1995/01/29  23:34:59  fritz
 * Added stopdriver() and appropriate calls.
 * Changed printk-statements to support loglevels.
 *
 * Revision 1.4  1995/01/09  07:40:46  fritz
 * Added GPL-Notice
 *
 * Revision 1.3  1995/01/04  05:15:18  fritz
 * Added undocumented "bootload-finished"-command in download-code
 * to satisfy some brain-damaged icn card-versions.
 *
 * Revision 1.2  1995/01/02  02:14:45  fritz
 * Misc Bugfixes
 *
 * Revision 1.1  1994/12/14  17:56:06  fritz
 * Initial revision
 *
 */

#include "icn.h"



/*
 * Verbose bootcode- and protocol-downloading.
 */
#undef BOOT_DEBUG

/*
 * Verbose Shmem-Mapping.
 */
#undef MAP_DEBUG

/* If defined, no bootcode- and protocol-downloading is supported and
 * you must use an external loader
 */
#undef LOADEXTERN

static char
*revision = "$Revision: 1.17 $";

static void icn_pollcard(unsigned long dummy);

/* Try to allocate a new buffer, link it into queue. */
static u_char *
 icn_new_buf(pqueue ** queue, int length)
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

#ifdef MODULE
static void icn_free_queue(pqueue ** queue)
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
#endif

/* Put a value into a shift-register, highest bit first.
 * Parameters:
 *            port     = port for output (bit 0 is significant)
 *            val      = value to be output
 *            firstbit = Bit-Number of highest bit
 *            bitcount = Number of bits to output
 */
static inline void icn_shiftout(unsigned short port,
		     unsigned long val,
		     int firstbit,
		     int bitcount)
{

	register u_char s;
	register u_char c;

	for (s = firstbit, c = bitcount; c > 0; s--, c--)
		OUTB_P((u_char) ((val >> s) & 1) ? 0xff : 0, port);
}

/*
 * Map Cannel0 (Bank0/Bank8) or Channel1 (Bank4/Bank12)
 */
static inline void icn_map_channel(int channel)
{
	static u_char chan2bank[] =
	{0, 4, 8, 12};

#ifdef MAP_DEBUG
	printk(KERN_DEBUG "icn_map_channel %d %d\n", dev->channel, channel);
#endif
	if (channel == dev->channel)
		return;
	OUTB_P(0, ICN_MAPRAM);	/* Disable RAM          */
	icn_shiftout(ICN_BANK, chan2bank[channel], 3, 4);	/* Select Bank          */
	OUTB_P(0xff, ICN_MAPRAM);	/* Enable RAM           */
	dev->channel = channel;
#ifdef MAP_DEBUG
	printk(KERN_DEBUG "icn_map_channel done\n");
#endif
}

static inline int icn_lock_channel(int channel)
{
	register int retval;
	ulong flags;

#ifdef MAP_DEBUG
	printk(KERN_DEBUG "icn_lock_channel %d\n", channel);
#endif
	save_flags(flags);
	cli();
	if (dev->channel == channel) {
		dev->chanlock++;
		retval = 1;
#ifdef MAP_DEBUG
		printk(KERN_DEBUG "icn_lock_channel %d OK\n", channel);
#endif
	} else {
		retval = 0;
#ifdef MAP_DEBUG
		printk(KERN_DEBUG "icn_lock_channel %d FAILED, dc=%d\n", channel, dev->channel);
#endif
	}
	restore_flags(flags);
	return retval;
}

static inline void icn_release_channel(void)
{
	ulong flags;

#ifdef MAP_DEBUG
	printk(KERN_DEBUG "icn_release_channel l=%d\n", dev->chanlock);
#endif
	save_flags(flags);
	cli();
	if (dev->chanlock)
		dev->chanlock--;
	restore_flags(flags);
}

static inline int icn_trymaplock_channel(int channel)
{
	ulong flags;

	save_flags(flags);
	cli();
#ifdef MAP_DEBUG
	printk(KERN_DEBUG "trymaplock c=%d dc=%d l=%d\n", channel, dev->channel,
	       dev->chanlock);
#endif
	if ((!dev->chanlock) || (dev->channel == channel)) {
		dev->chanlock++;
		icn_map_channel(channel);
		restore_flags(flags);
#ifdef MAP_DEBUG
		printk(KERN_DEBUG "trymaplock %d OK\n", channel);
#endif
		return 1;
	}
	restore_flags(flags);
#ifdef MAP_DEBUG
	printk(KERN_DEBUG "trymaplock %d FAILED\n", channel);
#endif
	return 0;
}

static inline void icn_maprelease_channel(int channel)
{
	ulong flags;

	save_flags(flags);
	cli();
#ifdef MAP_DEBUG
	printk(KERN_DEBUG "map_release c=%d l=%d\n", channel, dev->chanlock);
#endif
	if (dev->chanlock)
		dev->chanlock--;
	if (!dev->chanlock)
		icn_map_channel(channel);
	restore_flags(flags);
}

/* Get Data from the B-Channel, assemble fragmented packets and put them
 * into receive-queue. Wake up any B-Channel-reading processes.
 * This routine is called via timer-callback from pollbchan().
 * It schedules itself while any B-Channel is open.
 */

#ifdef DEBUG_RCVCALLBACK
static int max_pending[2] =
{0, 0};
#endif

static void icn_pollbchan_receive(int channel, icn_dev * dev)
{
	int mch = channel + ((dev->secondhalf) ? 2 : 0);
	int eflag;
	int cnt;
	int flags;
#ifdef DEBUG_RCVCALLBACK
	int rcv_pending1;
	int rcv_pending2;
	int akt_pending;
#endif

	if (icn_trymaplock_channel(mch)) {
		while (rbavl) {
			cnt = rbuf_l;
			if ((dev->rcvidx[channel] + cnt) > 4000) {
				printk(KERN_WARNING "icn: bogus packet on ch%d, dropping.\n",
				       channel + 1);
				dev->rcvidx[channel] = 0;
				eflag = 0;
			} else {
				memcpy(&dev->rcvbuf[channel][dev->rcvidx[channel]], rbuf_d, cnt);
				dev->rcvidx[channel] += cnt;
				eflag = rbuf_f;
			}
			rbnext;
			icn_maprelease_channel(mch & 2);
			if (!eflag) {
				save_flags(flags);
				cli();
#ifdef DEBUG_RCVCALLBACK
				rcv_pending1 =
				    (dev->shmem->data_control.ecnr > dev->shmem->data_control.ecns) ?
				    0xf - dev->shmem->data_control.ecnr + dev->shmem->data_control.ecns :
				    dev->shmem->data_control.ecns - dev->shmem->data_control.ecnr;
#endif
				dev->interface.rcvcallb(dev->myid, channel, dev->rcvbuf[channel],
						   dev->rcvidx[channel]);
				dev->rcvidx[channel] = 0;
#ifdef DEBUG_RCVCALLBACK
				rcv_pending2 =
				    (dev->shmem->data_control.ecnr > dev->shmem->data_control.ecns) ?
				    0xf - dev->shmem->data_control.ecnr + dev->shmem->data_control.ecns :
				    dev->shmem->data_control.ecns - dev->shmem->data_control.ecnr;
				akt_pending = rcv_pending2 - rcv_pending1;
				if (akt_pending > max_pending[channel]) {
					max_pending[channel] = akt_pending;
					printk(KERN_DEBUG "ICN_DEBUG: pend: %d %d\n", max_pending[0], max_pending[1]);
				}
#endif
				restore_flags(flags);
			}
			if (!icn_trymaplock_channel(mch))
				break;
		}
		icn_maprelease_channel(mch & 2);
	}
}

/* Send data-packet to B-Channel, split it up into fragments of
 * ICN_FRAGSIZE length. If last fragment is sent out, signal
 * success to upper layers via statcallb with ISDN_STAT_BSENT argument.
 * This routine is called via timer-callback from pollbchan() or
 * directly from sendbuf().
 */

static void icn_pollbchan_send(int channel, icn_dev * dev)
{
	int mch = channel + ((dev->secondhalf) ? 2 : 0);
	int eflag = 0;
	int cnt;
	int left;
	int flags;
	pqueue *p;
	isdn_ctrl cmd;

	if (!dev->sndcount[channel])
		return;
	if (icn_trymaplock_channel(mch)) {
		while (sbfree && dev->sndcount[channel]) {
			left = dev->spqueue[channel]->length;
			cnt =
			    (sbuf_l =
			     (left > ICN_FRAGSIZE) ? ((sbuf_f = 0xff), ICN_FRAGSIZE) : ((sbuf_f = 0), left));
			memcpy(sbuf_d, dev->spqueue[channel]->rptr, cnt);
			sbnext;	/* switch to next buffer        */
			icn_maprelease_channel(mch & 2);
			dev->spqueue[channel]->rptr += cnt;
			eflag = ((dev->spqueue[channel]->length -= cnt) == 0);
			save_flags(flags);
			cli();
			p = dev->spqueue[channel];
			dev->sndcount[channel] -= cnt;
			if (eflag)
				dev->spqueue[channel] = (pqueue *) dev->spqueue[channel]->next;
			restore_flags(flags);
			if (eflag) {
				kfree_s(p, p->size);
				cmd.command = ISDN_STAT_BSENT;
				cmd.driver = dev->myid;
				cmd.arg = channel;
				dev->interface.statcallb(&cmd);
			}
			if (!icn_trymaplock_channel(mch))
				break;
		}
		icn_maprelease_channel(mch & 2);
	}
}

/* Send/Receive Data to/from the B-Channel.
 * This routine is called via timer-callback.
 * It schedules itself while any B-Channel is open.
 */

static void icn_pollbchan(unsigned long dummy)
{
	unsigned long flags;

	dev->flags |= ICN_FLAGS_RBTIMER;
	if (dev->flags & ICN_FLAGS_B1ACTIVE) {
		icn_pollbchan_receive(0, dev);
		icn_pollbchan_send(0, dev);
	}
	if (dev->flags & ICN_FLAGS_B2ACTIVE) {
		icn_pollbchan_receive(1, dev);
		icn_pollbchan_send(1, dev);
	}
	if (dev->doubleS0) {
		if (dev2->flags & ICN_FLAGS_B1ACTIVE) {
			icn_pollbchan_receive(0, dev2);
			icn_pollbchan_send(0, dev2);
		}
		if (dev2->flags & ICN_FLAGS_B2ACTIVE) {
			icn_pollbchan_receive(1, dev2);
			icn_pollbchan_send(1, dev2);
		}
	}
	if (dev->flags & (ICN_FLAGS_B1ACTIVE | ICN_FLAGS_B2ACTIVE)) {
		/* schedule b-channel polling again */
		save_flags(flags);
		cli();
		del_timer(&dev->rb_timer);
		dev->rb_timer.function = icn_pollbchan;
		dev->rb_timer.expires = jiffies + ICN_TIMER_BCREAD;
		add_timer(&dev->rb_timer);
		restore_flags(flags);
	} else
		dev->flags &= ~ICN_FLAGS_RBTIMER;
	if (dev->doubleS0) {
		if (dev2->flags & (ICN_FLAGS_B1ACTIVE | ICN_FLAGS_B2ACTIVE)) {
			/* schedule b-channel polling again */
			save_flags(flags);
			cli();
			del_timer(&dev2->rb_timer);
			dev2->rb_timer.function = icn_pollbchan;
			dev2->rb_timer.expires = jiffies + ICN_TIMER_BCREAD;
			add_timer(&dev2->rb_timer);
			restore_flags(flags);
		} else
			dev2->flags &= ~ICN_FLAGS_RBTIMER;
	}
}

static void icn_pollit(icn_dev * dev)
{
	int mch = dev->secondhalf ? 2 : 0;
	int avail = 0;
	int dflag = 0;
	int left;
	u_char c;
	int ch;
	int flags;
	int i;
	u_char *p;
	isdn_ctrl cmd;

	if (icn_trymaplock_channel(mch)) {
		avail = msg_avail;
		for (left = avail, i = msg_o; left > 0; i++, left--) {
			c = dev->shmem->comm_buffers.iopc_buf[i & 0xff];
			save_flags(flags);
			cli();
			*dev->msg_buf_write++ = (c == 0xff) ? '\n' : c;
			/* No checks for buffer overflow for raw-status-device */
			if (dev->msg_buf_write > dev->msg_buf_end)
				dev->msg_buf_write = dev->msg_buf;
			restore_flags(flags);
			if (c == 0xff) {
				dev->imsg[dev->iptr] = 0;
				dev->iptr = 0;
				if (dev->imsg[0] == '0' && dev->imsg[1] >= '0' &&
				    dev->imsg[1] <= '2' && dev->imsg[2] == ';') {
					ch = dev->imsg[1] - '0';
					p = &dev->imsg[3];
					if (!strncmp(p, "BCON_", 5)) {
						switch (ch) {
						case 1:
							dev->flags |= ICN_FLAGS_B1ACTIVE;
							break;
						case 2:
							dev->flags |= ICN_FLAGS_B2ACTIVE;
							break;
						}
						cmd.command = ISDN_STAT_BCONN;
						cmd.driver = dev->myid;
						cmd.arg = ch - 1;
						dev->interface.statcallb(&cmd);
						continue;
					}
					if (!strncmp(p, "TEI OK", 6)) {
						cmd.command = ISDN_STAT_RUN;
						cmd.driver = dev->myid;
						cmd.arg = ch - 1;
						dev->interface.statcallb(&cmd);
						continue;
					}
					if (!strncmp(p, "BDIS_", 5)) {
						switch (ch) {
						case 1:
							dev->flags &= ~ICN_FLAGS_B1ACTIVE;
							dflag |= 1;
							break;
						case 2:
							dev->flags &= ~ICN_FLAGS_B2ACTIVE;
							dflag |= 2;
							break;
						}
						cmd.command = ISDN_STAT_BHUP;
						cmd.arg = ch - 1;
						cmd.driver = dev->myid;
						dev->interface.statcallb(&cmd);
						continue;
					}
					if (!strncmp(p, "DCON_", 5)) {
						cmd.command = ISDN_STAT_DCONN;
						cmd.arg = ch - 1;
						cmd.driver = dev->myid;
						dev->interface.statcallb(&cmd);
						continue;
					}
					if (!strncmp(p, "DDIS_", 5)) {
						cmd.command = ISDN_STAT_DHUP;
						cmd.arg = ch - 1;
						cmd.driver = dev->myid;
						dev->interface.statcallb(&cmd);
						continue;
					}
					if (!strncmp(p, "E_L1: ACT FAIL", 14)) {
						cmd.command = ISDN_STAT_BHUP;
						cmd.arg = 0;
						cmd.driver = dev->myid;
						dev->interface.statcallb(&cmd);
						cmd.command = ISDN_STAT_DHUP;
						cmd.arg = 0;
						cmd.driver = dev->myid;
						dev->interface.statcallb(&cmd);
						continue;
					}
					if (!strncmp(p, "CIF", 3)) {
						cmd.command = ISDN_STAT_CINF;
						cmd.arg = ch - 1;
						strncpy(cmd.num, p + 3, sizeof(cmd.num) - 1);
						cmd.driver = dev->myid;
						dev->interface.statcallb(&cmd);
						continue;
					}
					if (!strncmp(p, "CAU", 3)) {
						cmd.command = ISDN_STAT_CAUSE;
						cmd.arg = ch - 1;
						strncpy(cmd.num, p + 3, sizeof(cmd.num) - 1);
						cmd.driver = dev->myid;
						dev->interface.statcallb(&cmd);
						continue;
					}
					if (!strncmp(p, "DCAL_I", 6)) {
						cmd.command = ISDN_STAT_ICALL;
						cmd.driver = dev->myid;
						cmd.arg = ch - 1;
						strncpy(cmd.num, p + 6, sizeof(cmd.num) - 1);
						dev->interface.statcallb(&cmd);
						continue;
					}
					if (!strncmp(p, "FCALL", 5)) {
						cmd.command = ISDN_STAT_ICALL;
						cmd.driver = dev->myid;
						cmd.arg = ch - 1;
						strcpy(cmd.num, "LEASED,07,00,1");
						dev->interface.statcallb(&cmd);
						continue;
					}
					if (!strncmp(p, "DSCA_I", 6)) {
						cmd.command = ISDN_STAT_ICALL;
						cmd.driver = dev->myid;
						cmd.arg = ch - 1;
						strncpy(cmd.num, p + 6, sizeof(cmd.num) - 1);
						dev->interface.statcallb(&cmd);
						continue;
					}
					if (!strncmp(p, "NO D-CHAN", 9)) {
						cmd.command = ISDN_STAT_NODCH;
						cmd.driver = dev->myid;
						cmd.arg = ch - 1;
						strncpy(cmd.num, p + 6, sizeof(cmd.num) - 1);
						dev->interface.statcallb(&cmd);
						continue;
					}
				} else {
					p = dev->imsg;
					if (!strncmp(p, "DRV1.", 5)) {
                                                printk(KERN_INFO "icn: %s\n",p);
						if (!strncmp(p + 7, "TC", 2)) {
							dev->ptype = ISDN_PTYPE_1TR6;
							dev->interface.features |= ISDN_FEATURE_P_1TR6;
							printk(KERN_INFO "icn: 1TR6-Protocol loaded and running\n");
						}
						if (!strncmp(p + 7, "EC", 2)) {
							dev->ptype = ISDN_PTYPE_EURO;
							dev->interface.features |= ISDN_FEATURE_P_EURO;
							printk(KERN_INFO "icn: Euro-Protocol loaded and running\n");
						}
						continue;
					}
				}
			} else {
				dev->imsg[dev->iptr] = c;
				if (dev->iptr < 59)
					dev->iptr++;
			}
		}
		msg_o = (msg_o + avail) & 0xff;
		icn_release_channel();
	}
	if (avail) {
		cmd.command = ISDN_STAT_STAVAIL;
		cmd.driver = dev->myid;
		cmd.arg = avail;
		dev->interface.statcallb(&cmd);
	}
	if (dflag & 1)
		dev->interface.rcvcallb(dev->myid, 0, dev->rcvbuf[0], 0);
	if (dflag & 2)
		dev->interface.rcvcallb(dev->myid, 1, dev->rcvbuf[1], 0);
	if (dev->flags & (ICN_FLAGS_B1ACTIVE | ICN_FLAGS_B2ACTIVE))
		if (!(dev->flags & ICN_FLAGS_RBTIMER)) {
			/* schedule b-channel polling */
			dev->flags |= ICN_FLAGS_RBTIMER;
			save_flags(flags);
			cli();
			del_timer(&dev->rb_timer);
			dev->rb_timer.function = icn_pollbchan;
			dev->rb_timer.expires = jiffies + ICN_TIMER_BCREAD;
			add_timer(&dev->rb_timer);
			restore_flags(flags);
		}
}

/*
 * Check Statusqueue-Pointer from isdn-card.
 * If there are new status-replies from the interface, check
 * them against B-Channel-connects/disconnects and set flags arrcordingly.
 * Wake-Up any processes, who are reading the status-device.
 * If there are B-Channels open, initiate a timer-callback to
 * icn_pollbchan().
 * This routine is called periodically via timer.
 */

static void icn_pollcard(unsigned long dummy)
{
	ulong flags;

	icn_pollit(dev);
	if (dev->doubleS0)
		icn_pollit(dev2);
	/* schedule again */
	save_flags(flags);
	cli();
	del_timer(&dev->st_timer);
	dev->st_timer.function = icn_pollcard;
	dev->st_timer.expires = jiffies + ICN_TIMER_DCREAD;
	add_timer(&dev->st_timer);
	restore_flags(flags);
}

/* Send a packet to the transmit-buffers, handle fragmentation if necessary.
 * Parameters:
 *            channel = Number of B-channel
 *            buffer  = pointer to packet
 *            len     = size of packet (max 4000)
 *            dev     = pointer to device-struct
 *            user    = 1 = call from userproc, 0 = call from kernel
 * Return:
 *        Number of bytes transferred, -E??? on error
 */

static int icn_sendbuf(int channel, const u_char * buffer, int len, int user, icn_dev * dev)
{
	register u_char *p;
	int flags;

	if (len > 4000)
		return -EINVAL;
	if (len) {
		if (dev->sndcount[channel] > ICN_MAX_SQUEUE)
			return 0;
		save_flags(flags);
		cli();
		p = icn_new_buf(&dev->spqueue[channel], len);
		if (!p) {
		        restore_flags(flags);
			return 0;
		}
		if (user) {
			memcpy_fromfs(p, buffer, len);
		} else {
			memcpy(p, buffer, len);
		}
		dev->sndcount[channel] += len;
		icn_pollbchan_send(channel, dev);
		restore_flags(flags);
	}
	return len;
}

#ifndef LOADEXTERN
static int icn_check_loader(int cardnumber)
{
	int timer = 0;

	while (1) {
#ifdef BOOT_DEBUG
		printk(KERN_DEBUG "Loader %d ?\n", cardnumber);
#endif
		if (dev->shmem->data_control.scns ||
		    dev->shmem->data_control.scnr) {
			if (timer++ > 5) {
				printk(KERN_WARNING "icn: Boot-Loader %d timed out.\n", cardnumber);
				icn_release_channel();
				return -EIO;
			}
#ifdef BOOT_DEBUG
			printk(KERN_DEBUG "Loader %d TO?\n", cardnumber);
#endif
			current->state = TASK_INTERRUPTIBLE;
			current->timeout = jiffies + ICN_BOOT_TIMEOUT1;
			schedule();
		} else {
#ifdef BOOT_DEBUG
			printk(KERN_DEBUG "Loader %d OK\n", cardnumber);
#endif
			icn_release_channel();
			return 0;
		}
	}
}

/* Load the boot-code into the interface-card's memory and start it.
 * Always called from user-process.
 * 
 * Parameters:
 *            buffer = pointer to packet
 * Return:
 *        0 if successfully loaded
 */

#ifdef BOOT_DEBUG
#define SLEEP(sec) { \
int slsec = sec; \
  printk(KERN_DEBUG "SLEEP(%d)\n",slsec); \
  while (slsec) { \
    current->state = TASK_INTERRUPTIBLE; \
    current->timeout = jiffies + HZ; \
    schedule(); \
    slsec--; \
  } \
}
#else
#define SLEEP(sec)
#endif

static int icn_loadboot(u_char * buffer, icn_dev * dev)
{
	int ret;
	ulong flags;

#ifdef BOOT_DEBUG
	printk(KERN_DEBUG "icn_loadboot called, buffaddr=%08lx\n", (ulong) buffer);
#endif
	if ((ret = verify_area(VERIFY_READ, (void *) buffer, ICN_CODE_STAGE1)))
		return ret;
	save_flags(flags);
	cli();
	if (!dev->rvalid) {
		if (check_region(dev->port, ICN_PORTLEN)) {
			printk(KERN_WARNING "icn: ports 0x%03x-0x%03x in use.\n", dev->port,
			       dev->port + ICN_PORTLEN);
			restore_flags(flags);
			return -EBUSY;
		}
		request_region(dev->port, ICN_PORTLEN, regname);
		dev->rvalid = 1;
	}
	if (!dev->mvalid) {
		if (check_shmem((ulong) dev->shmem, 0x4000)) {
			printk(KERN_WARNING "icn: memory at 0x%08lx in use.\n", (ulong) dev->shmem);
			restore_flags(flags);
			return -EBUSY;
		}
		request_shmem((ulong) dev->shmem, 0x4000, regname);
		dev->mvalid = 1;
	}
	restore_flags(flags);
	OUTB_P(0, ICN_RUN);	/* Reset Controler */
	OUTB_P(0, ICN_MAPRAM);	/* Disable RAM     */
	icn_shiftout(ICN_CFG, 0x0f, 3, 4);	/* Windowsize= 16k */
	icn_shiftout(ICN_CFG, (unsigned long) dev->shmem, 23, 10);	/* Set RAM-Addr.   */
#ifdef BOOT_DEBUG
	printk(KERN_DEBUG "shmem=%08lx\n", (ulong) dev->shmem);
#endif
	SLEEP(1);
	save_flags(flags);
	cli();
	dev->channel = 1;	/* Force Mapping   */
#ifdef BOOT_DEBUG
	printk(KERN_DEBUG "Map Bank 0\n");
#endif
	icn_map_channel(0);		/* Select Bank 0   */
	icn_lock_channel(0);	/* Lock Bank 0     */
	restore_flags(flags);
	SLEEP(1);
	memcpy_fromfs(dev->shmem, buffer, ICN_CODE_STAGE1);	/* Copy code       */
#ifdef BOOT_DEBUG
	printk(KERN_DEBUG "Bootloader transfered\n");
#endif
	if (dev->doubleS0) {
		SLEEP(1);
		save_flags(flags);
		cli();
		icn_release_channel();
#ifdef BOOT_DEBUG
		printk(KERN_DEBUG "Map Bank 8\n");
#endif
		icn_map_channel(2);	/* Select Bank 8   */
		icn_lock_channel(2);	/* Lock Bank 8     */
		restore_flags(flags);
		SLEEP(1);
		memcpy_fromfs(dev->shmem, buffer, ICN_CODE_STAGE1);	/* Copy code       */
#ifdef BOOT_DEBUG
		printk(KERN_DEBUG "Bootloader transfered\n");
#endif
	}
	SLEEP(1);
	OUTB_P(0xff, ICN_RUN);	/* Start Boot-Code */
	if ((ret = icn_check_loader(dev->doubleS0 ? 2 : 1)))
		return ret;
	if (!dev->doubleS0)
		return 0;
	/* reached only, if we have a Double-S0-Card */
	save_flags(flags);
	cli();
#ifdef BOOT_DEBUG
	printk(KERN_DEBUG "Map Bank 0\n");
#endif
	icn_map_channel(0);		/* Select Bank 0   */
	icn_lock_channel(0);	/* Lock Bank 0     */
	restore_flags(flags);
	SLEEP(1);
	return (icn_check_loader(1));
}

static int icn_loadproto(u_char * buffer, icn_dev * ldev)
{
	register u_char *p = buffer;
	uint left = ICN_CODE_STAGE2;
	uint cnt;
	int timer;
	int ret;
	unsigned long flags;

#ifdef BOOT_DEBUG
	printk(KERN_DEBUG "icn_loadproto called\n");
#endif
	if ((ret = verify_area(VERIFY_READ, (void *) buffer, ICN_CODE_STAGE2)))
		return ret;
	timer = 0;
	save_flags(flags);
	cli();
	if (ldev->secondhalf) {
		icn_map_channel(2);
		icn_lock_channel(2);
	} else {
		icn_map_channel(0);
		icn_lock_channel(0);
	}
	restore_flags(flags);
	while (left) {
		if (sbfree) {	/* If there is a free buffer...  */
			cnt = MIN(256, left);
			memcpy_fromfs(&sbuf_l, p, cnt);		/* copy data                     */
			sbnext;	/* switch to next buffer         */
			p += cnt;
			left -= cnt;
			timer = 0;
		} else {
#ifdef BOOT_DEBUG
			printk(KERN_DEBUG "boot 2 !sbfree\n");
#endif
			if (timer++ > 5) {
				icn_maprelease_channel(0);
				return -EIO;
			}
			current->state = TASK_INTERRUPTIBLE;
			current->timeout = jiffies + 10;
			schedule();
		}
	}
	sbuf_n = 0x20;
	timer = 0;
	while (1) {
		if (cmd_o || cmd_i) {
#ifdef BOOT_DEBUG
			printk(KERN_DEBUG "Proto?\n");
#endif
			if (timer++ > 5) {
				printk(KERN_WARNING "icn: Protocol timed out.\n");
#ifdef BOOT_DEBUG
				printk(KERN_DEBUG "Proto TO!\n");
#endif
				icn_maprelease_channel(0);
				return -EIO;
			}
#ifdef BOOT_DEBUG
			printk(KERN_DEBUG "Proto TO?\n");
#endif
			current->state = TASK_INTERRUPTIBLE;
			current->timeout = jiffies + ICN_BOOT_TIMEOUT1;
			schedule();
		} else {
			if ((ldev->secondhalf) || (!dev->doubleS0)) {
				save_flags(flags);
				cli();
#ifdef BOOT_DEBUG
				printk(KERN_DEBUG "Proto loaded, install poll-timer %d\n",
				       ldev->secondhalf);
#endif
				init_timer(&dev->st_timer);
				dev->st_timer.expires = jiffies + ICN_TIMER_DCREAD;
				dev->st_timer.function = icn_pollcard;
				add_timer(&dev->st_timer);
				restore_flags(flags);
			}
			icn_maprelease_channel(0);
			return 0;
		}
	}
}
#endif				/* !LOADEXTERN */

/* Read the Status-replies from the Interface */
static int icn_readstatus(u_char * buf, int len, int user, icn_dev * dev)
{
	int count;
	u_char *p;

	for (p = buf, count = 0; count < len; p++, count++) {
		if (user)
			put_fs_byte(*dev->msg_buf_read++, p);
		else
			*p = *dev->msg_buf_read++;
		if (dev->msg_buf_read > dev->msg_buf_end)
			dev->msg_buf_read = dev->msg_buf;
	}
	return count;
}

/* Put command-strings into the command-queue of the Interface */
static int icn_writecmd(const u_char * buf, int len, int user, icn_dev * dev, int waitflg)
{
	int mch = dev->secondhalf ? 2 : 0;
	int avail;
	int pp;
	int i;
	int count;
	int ocount;
	unsigned long flags;
	u_char *p;
	isdn_ctrl cmd;
	u_char msg[0x100];

	while (1) {
		if (icn_trymaplock_channel(mch)) {
			avail = cmd_free;
			count = MIN(avail, len);
			if (user)
				memcpy_fromfs(msg, buf, count);
			else
				memcpy(msg, buf, count);
			save_flags(flags);
			cli();
			ocount = 1;
			*dev->msg_buf_write++ = '>';
			if (dev->msg_buf_write > dev->msg_buf_end)
				dev->msg_buf_write = dev->msg_buf;
			for (p = msg, pp = cmd_i, i = count; i > 0; i--, p++, pp++) {
				dev->shmem->comm_buffers.pcio_buf[pp & 0xff] = (*p == '\n') ? 0xff : *p;
				*dev->msg_buf_write++ = *p;
				if ((*p == '\n') && (i > 1)) {
					*dev->msg_buf_write++ = '>';
					if (dev->msg_buf_write > dev->msg_buf_end)
						dev->msg_buf_write = dev->msg_buf;
					ocount++;
				}
				/* No checks for buffer overflow of raw-status-device */
				if (dev->msg_buf_write > dev->msg_buf_end)
					dev->msg_buf_write = dev->msg_buf;
				ocount++;
			}
			restore_flags(flags);
			cmd.command = ISDN_STAT_STAVAIL;
			cmd.driver = dev->myid;
			cmd.arg = ocount;
			dev->interface.statcallb(&cmd);
			cmd_i = (cmd_i + count) & 0xff;
			icn_release_channel();
			waitflg = 0;
		} else
			count = 0;
		if (!waitflg)
			break;
		current->timeout = jiffies + 10;
		schedule();
	}
	return count;
}

static void icn_stopdriver(icn_dev * ldev)
{
	unsigned long flags;
	isdn_ctrl cmd;

	save_flags(flags);
	cli();
	del_timer(&dev->st_timer);
	del_timer(&ldev->rb_timer);
	cmd.command = ISDN_STAT_STOP;
	cmd.driver = ldev->myid;
	ldev->interface.statcallb(&cmd);
	restore_flags(flags);
}

static int my_atoi(char *s)
{
	int i, n;

	n = 0;
	if (!s)
		return -1;
	for (i = 0; *s >= '0' && *s <= '9'; i++, s++)
		n = 10 * n + (*s - '0');
	return n;
}

static int icn_command(isdn_ctrl * c, icn_dev * ldev)
{
	ulong a;
	ulong flags;
	int i;
	char cbuf[60];
	isdn_ctrl cmd;

	switch (c->command) {
	case ISDN_CMD_IOCTL:
		memcpy(&a, c->num, sizeof(ulong));
		switch (c->arg) {
		case ICN_IOCTL_SETMMIO:
			if ((unsigned long) dev->shmem != (a & 0x0ffc000)) {
				if (check_shmem((ulong) (a & 0x0ffc000), 0x4000)) {
					printk(KERN_WARNING "icn: memory at 0x%08lx in use.\n",
					       (ulong) (a & 0x0ffc000));
					return -EINVAL;
				}
				icn_stopdriver(dev);
				if (dev->doubleS0)
					icn_stopdriver(dev2);
				save_flags(flags);
				cli();
				if (dev->mvalid)
					release_shmem((ulong) dev->shmem, 0x4000);
				dev->mvalid = 0;
				dev->shmem = (icn_shmem *) (a & 0x0ffc000);
				if (dev->doubleS0)
					dev2->shmem = (icn_shmem *) (a & 0x0ffc000);
				restore_flags(flags);
				printk(KERN_INFO "icn: mmio set to 0x%08lx\n",
				       (unsigned long) dev->shmem);
			}
			break;
		case ICN_IOCTL_GETMMIO:
			return (int) dev->shmem;
		case ICN_IOCTL_SETPORT:
			if (a == 0x300 || a == 0x310 || a == 0x320 || a == 0x330
			    || a == 0x340 || a == 0x350 || a == 0x360 ||
			    a == 0x308 || a == 0x318 || a == 0x328 || a == 0x338
			    || a == 0x348 || a == 0x358 || a == 0x368) {
				if (dev->port != (unsigned short) a) {
					if (check_region((unsigned short) a, ICN_PORTLEN)) {
						printk(KERN_WARNING "icn: ports 0x%03x-0x%03x in use.\n",
						       (int) a, (int) a + ICN_PORTLEN);
						return -EINVAL;
					}
					icn_stopdriver(dev);
					if (dev->doubleS0)
						icn_stopdriver(dev2);
					save_flags(flags);
					cli();
					if (dev->rvalid)
						release_region(dev->port, ICN_PORTLEN);
					dev->port = (unsigned short) a;
					dev->rvalid = 0;
					if (dev->doubleS0) {
						dev2->port = (unsigned short) a;
						dev2->rvalid = 0;
					}
					restore_flags(flags);
					printk(KERN_INFO "icn: port set to 0x%03x\n", dev->port);
				}
			} else
				return -EINVAL;
			break;
		case ICN_IOCTL_GETPORT:
			return (int) dev->port;
		case ICN_IOCTL_GETDOUBLE:
			return (int) dev->doubleS0;
		case ICN_IOCTL_DEBUGVAR:
			return (ulong) ldev;
#ifndef LOADEXTERN
		case ICN_IOCTL_LOADBOOT:
			icn_stopdriver(dev);
			if (dev->doubleS0)
				icn_stopdriver(dev2);
			return (icn_loadboot((u_char *) a, dev));
		case ICN_IOCTL_LOADPROTO:
			icn_stopdriver(dev);
			if (dev->doubleS0)
				icn_stopdriver(dev2);
			if ((i = (icn_loadproto((u_char *) a, dev))))
				return i;
			if (dev->doubleS0)
				i = icn_loadproto((u_char *) (a + ICN_CODE_STAGE2), dev2);
			return i;
#endif
		case ICN_IOCTL_LEASEDCFG:
			if (a) {
				if (!ldev->leased) {
					ldev->leased = 1;
					while (ldev->ptype == ISDN_PTYPE_UNKNOWN) {
						current->timeout = jiffies + ICN_BOOT_TIMEOUT1;
						schedule();
					}
					current->timeout = jiffies + ICN_BOOT_TIMEOUT1;
					schedule();
					sprintf(cbuf, "00;FV2ON\n01;EAZ1\n");
					i = icn_writecmd(cbuf, strlen(cbuf), 0, ldev, 1);
					printk(KERN_INFO "icn: Leased-line mode enabled\n");
					cmd.command = ISDN_STAT_RUN;
					cmd.driver = ldev->myid;
					cmd.arg = 0;
					ldev->interface.statcallb(&cmd);
				}
			} else {
				if (ldev->leased) {
					ldev->leased = 0;
					sprintf(cbuf, "00;FV2OFF\n");
					i = icn_writecmd(cbuf, strlen(cbuf), 0, ldev, 1);
					printk(KERN_INFO "icn: Leased-line mode disabled\n");
					cmd.command = ISDN_STAT_RUN;
					cmd.driver = ldev->myid;
					cmd.arg = 0;
					ldev->interface.statcallb(&cmd);
				}
			}
			return 0;
		default:
			return -EINVAL;
		}
		break;
	case ISDN_CMD_DIAL:
		if (ldev->leased)
			break;
		if ((c->arg & 255) < ICN_BCH) {
			char *p;
			char *p2;
			char dial[50];
			char sis[50];
			char dcode[4];
			int si1, si2;

			a = c->arg;
			strcpy(sis, c->num);
			p = strrchr(sis, ',');
			*p++ = '\0';
			si2 = my_atoi(p);
			p = strrchr(sis, ',') + 1;
			si1 = my_atoi(p);
			p = c->num;
			if (*p == 's' || *p == 'S') {
				/* Dial for SPV */
				p++;
				strcpy(dcode, "SCA");
			} else
				/* Normal Dial */
				strcpy(dcode, "CAL");
			strcpy(dial, p);
			p = strchr(dial, ',');
			*p++ = '\0';
			p2 = strchr(p, ',');
			*p2 = '\0';
			sprintf(cbuf, "%02d;D%s_R%s,%02d,%02d,%s\n", (int) (a + 1), dcode, dial, si1,
				si2, p);
			i = icn_writecmd(cbuf, strlen(cbuf), 0, ldev, 1);
		}
		break;
	case ISDN_CMD_ACCEPTD:
		if (c->arg < ICN_BCH) {
			a = c->arg + 1;
			sprintf(cbuf, "%02d;DCON_R\n", (int) a);
			i = icn_writecmd(cbuf, strlen(cbuf), 0, ldev, 1);
		}
		break;
	case ISDN_CMD_ACCEPTB:
		if (c->arg < ICN_BCH) {
			a = c->arg + 1;
			sprintf(cbuf, "%02d;BCON_R\n", (int) a);
			i = icn_writecmd(cbuf, strlen(cbuf), 0, ldev, 1);
		}
		break;
	case ISDN_CMD_HANGUP:
		if (c->arg < ICN_BCH) {
			a = c->arg + 1;
			sprintf(cbuf, "%02d;BDIS_R\n%02d;DDIS_R\n", (int) a, (int) a);
			i = icn_writecmd(cbuf, strlen(cbuf), 0, ldev, 1);
		}
		break;
	case ISDN_CMD_SETEAZ:
		if (ldev->leased)
			break;
		if (c->arg < ICN_BCH) {
			a = c->arg + 1;
			if (ldev->ptype == ISDN_PTYPE_EURO) {
				sprintf(cbuf, "%02d;MS%s%s\n", (int) a, c->num[0] ? "N" : "ALL", c->num);
			} else
				sprintf(cbuf, "%02d;EAZ%s\n", (int) a, c->num[0] ? c->num : "0123456789");
			i = icn_writecmd(cbuf, strlen(cbuf), 0, ldev, 1);
		}
		break;
	case ISDN_CMD_CLREAZ:
		if (ldev->leased)
			break;
		if (c->arg < ICN_BCH) {
			a = c->arg + 1;
			if (ldev->ptype == ISDN_PTYPE_EURO)
				sprintf(cbuf, "%02d;MSNC\n", (int) a);
			else
				sprintf(cbuf, "%02d;EAZC\n", (int) a);
			i = icn_writecmd(cbuf, strlen(cbuf), 0, ldev, 1);
		}
		break;
	case ISDN_CMD_SETL2:
		if ((c->arg & 255) < ICN_BCH) {
			a = c->arg;
			switch (a >> 8) {
			case ISDN_PROTO_L2_X75I:
				sprintf(cbuf, "%02d;BX75\n", (int) (a & 255) + 1);
				break;
			case ISDN_PROTO_L2_HDLC:
				sprintf(cbuf, "%02d;BTRA\n", (int) (a & 255) + 1);
				break;
			default:
				return -EINVAL;
			}
			i = icn_writecmd(cbuf, strlen(cbuf), 0, ldev, 1);
			ldev->l2_proto[a & 255] = (a >> 8);
		}
		break;
	case ISDN_CMD_GETL2:
		if ((c->arg & 255) < ICN_BCH)
			return ldev->l2_proto[c->arg & 255];
		else
			return -ENODEV;
	case ISDN_CMD_SETL3:
		return 0;
	case ISDN_CMD_GETL3:
		if ((c->arg & 255) < ICN_BCH)
			return ISDN_PROTO_L3_TRANS;
		else
			return -ENODEV;
	case ISDN_CMD_GETEAZ:
		break;
	case ISDN_CMD_SETSIL:
		break;
	case ISDN_CMD_GETSIL:
		break;
	case ISDN_CMD_LOCK:
		MOD_INC_USE_COUNT;
		break;
	case ISDN_CMD_UNLOCK:
		MOD_DEC_USE_COUNT;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/*
 * For second half of doubleS0-Card add channel-offset.
 */
static int if_command1(isdn_ctrl * c)
{
	return (icn_command(c, dev));
}

static int if_command2(isdn_ctrl * c)
{
	return (icn_command(c, dev2));
}

static int if_writecmd1(const u_char * buf, int len, int user)
{
	return (icn_writecmd(buf, len, user, dev, 0));
}

static int if_writecmd2(const u_char * buf, int len, int user)
{
	return (icn_writecmd(buf, len, user, dev2, 0));
}

static int if_readstatus1(u_char * buf, int len, int user)
{
	return (icn_readstatus(buf, len, user, dev));
}

static int if_readstatus2(u_char * buf, int len, int user)
{
	return (icn_readstatus(buf, len, user, dev2));
}

static int if_sendbuf1(int id, int channel, const u_char * buffer, int len,
		       int user)
{
	return (icn_sendbuf(channel, buffer, len, user, dev));
}

static int if_sendbuf2(int id, int channel, const u_char * buffer, int len,
		       int user)
{
	return (icn_sendbuf(channel, buffer, len, user, dev2));
}

#ifdef MODULE
#define icn_init init_module
#else
void icn_setup(char *str, int *ints)
{
	char *p;
	static char sid[20];
	static char sid2[20];

	if (ints[0])
		portbase = ints[1];
	if (ints[0]>1)
		membase  = ints[2];
	if (strlen(str)) {
		strcpy(sid,str);
		icn_id = sid;
		if ((p = strchr(sid,','))) {
			*p++ = 0;
			strcpy(sid2,p);
			icn_id2 = sid2;
		}
	}
}
#endif

int icn_init(void)
{
#ifdef LOADEXTERN
	unsigned long flags;
#endif
	char *p;
	isdn_ctrl cmd;
	char rev[10];

	if (!(dev = (icn_devptr) kmalloc(sizeof(icn_dev), GFP_KERNEL))) {
		printk(KERN_WARNING "icn: Could not allocate device-struct.\n");
		return -EIO;
	}
	memset((char *) dev, 0, sizeof(icn_dev));
	dev->port = portbase;
	dev->shmem = (icn_shmem *) (membase & 0x0ffc000);
	if (strlen(icn_id2))
		dev->doubleS0 = 1;
	dev->interface.channels = ICN_BCH;
	dev->interface.maxbufsize = 4000;
	dev->interface.command = if_command1;
	dev->interface.writebuf = if_sendbuf1;
	dev->interface.writecmd = if_writecmd1;
	dev->interface.readstat = if_readstatus1;
	dev->interface.features = ISDN_FEATURE_L2_X75I |
                ISDN_FEATURE_L2_HDLC |
                ISDN_FEATURE_L3_TRANS |
                ISDN_FEATURE_P_UNKNOWN;
	dev->ptype = ISDN_PTYPE_UNKNOWN;
	strncpy(dev->interface.id, icn_id, sizeof(dev->interface.id) - 1);
	dev->msg_buf_write = dev->msg_buf;
	dev->msg_buf_read = dev->msg_buf;
	dev->msg_buf_end = &dev->msg_buf[sizeof(dev->msg_buf) - 1];
	memset((char *) dev->l2_proto, ISDN_PROTO_L2_X75I, sizeof(dev->l2_proto));
	if (strlen(icn_id2)) {
		if (!(dev2 = (icn_devptr) kmalloc(sizeof(icn_dev), GFP_KERNEL))) {
			printk(KERN_WARNING "icn: Could not allocate device-struct.\n");
			kfree(dev);
			kfree(dev2);
			return -EIO;
		}
		memcpy((char *) dev2, (char *) dev, sizeof(icn_dev));
		dev2->interface.command = if_command2;
		dev2->interface.writebuf = if_sendbuf2;
		dev2->interface.writecmd = if_writecmd2;
		dev2->interface.readstat = if_readstatus2;
		strncpy(dev2->interface.id, icn_id2,
                        sizeof(dev->interface.id) - 1);
		dev2->msg_buf_write = dev2->msg_buf;
		dev2->msg_buf_read = dev2->msg_buf;
		dev2->msg_buf_end = &dev2->msg_buf[sizeof(dev2->msg_buf) - 1];
		dev2->secondhalf = 1;
	}
	if (!register_isdn(&dev->interface)) {
		printk(KERN_WARNING "icn: Unable to register\n");
		kfree(dev);
		if (dev->doubleS0)
			kfree(dev2);
		return -EIO;
	}
	dev->myid = dev->interface.channels;
	sprintf(regname, "icn-isdn (%s)", dev->interface.id);
	if (dev->doubleS0) {
		if (!register_isdn(&dev2->interface)) {
			printk(KERN_WARNING "icn: Unable to register\n");
			kfree(dev2);
			if (dev->doubleS0) {
				icn_stopdriver(dev);
				cmd.command = ISDN_STAT_UNLOAD;
				cmd.driver = dev->myid;
				dev->interface.statcallb(&cmd);
				kfree(dev);
			}
			return -EIO;
		}
		dev2->myid = dev2->interface.channels;
	}
	
	/* No symbols to export, hide all symbols */
	register_symtab(NULL);

	if ((p = strchr(revision, ':'))) {
		strcpy(rev, p + 1);
		p = strchr(rev, '$');
		*p = 0;
	} else
		strcpy(rev, " ??? ");
	printk(KERN_NOTICE "ICN-ISDN-driver Rev%sport=0x%03x mmio=0x%08x id='%s'\n",
	       rev, dev->port, (uint) dev->shmem, dev->interface.id);
#ifdef LOADEXTERN
	save_flags(flags);
	cli();
	init_timer(&dev->st_timer);
	dev->st_timer.expires = jiffies + ICN_TIMER_DCREAD;
	dev->st_timer.function = icn_pollcard;
	add_timer(&dev->st_timer);
	restore_flags(flags);
#endif
	return 0;
}

#ifdef MODULE
void cleanup_module(void)
{
	isdn_ctrl cmd;
	int i;

	icn_stopdriver(dev);
	cmd.command = ISDN_STAT_UNLOAD;
	cmd.driver = dev->myid;
	dev->interface.statcallb(&cmd);
	if (dev->doubleS0) {
		icn_stopdriver(dev2);
		cmd.command = ISDN_STAT_UNLOAD;
		cmd.driver = dev2->myid;
		dev2->interface.statcallb(&cmd);
	}
	if (dev->rvalid) {
		OUTB_P(0, ICN_RUN);	/* Reset Controler      */
		OUTB_P(0, ICN_MAPRAM);	/* Disable RAM          */
		release_region(dev->port, ICN_PORTLEN);
	}
	if (dev->mvalid)
		release_shmem((ulong) dev->shmem, 0x4000);
	if (dev->doubleS0) {
		for (i = 0; i < ICN_BCH; i++)
			icn_free_queue(&dev2->spqueue[1]);
		kfree(dev2);
	}
	for (i = 0; i < ICN_BCH; i++)
		icn_free_queue(&dev->spqueue[1]);
	kfree(dev);
	printk(KERN_NOTICE "ICN-ISDN-driver unloaded\n");
}
#endif
