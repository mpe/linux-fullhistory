/* $Id: isdn_common.c,v 1.55 1998/02/23 23:35:32 fritz Exp $

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
 * Note: This file differs from the corresponding revision as present in the
 * isdn4linux CVS repository because some later bug fixes have been extracted
 * from the repository and merged into this file. -- Henner Eisen
 *
 * $Log: isdn_common.c,v $
 * Revision 1.55  1998/02/23 23:35:32  fritz
 * Eliminated some compiler warnings.
 *
 * Revision 1.54  1998/02/22 19:44:19  fritz
 * Bugfixes and improvements regarding V.110, V.110 now running.
 *
 * Revision 1.53  1998/02/20 17:18:05  fritz
 * Changes for recent kernels.
 * Added common stub for sending commands to lowlevel.
 * Added V.110.
 *
 * Revision 1.52  1998/01/31 22:05:57  keil
 * Lots of changes for X.25 support:
 * Added generic support for connection-controlling encapsulation protocols
 * Added support of BHUP status message
 * Added support for additional p_encap X25IFACE
 * Added support for kernels >= 2.1.72
 *
 * Revision 1.51  1998/01/31 19:17:29  calle
 * merged changes from and for 2.1.82
 *
 * Revision 1.50  1997/12/12 06:12:11  calle
 * moved EXPORT_SYMBOL(register_isdn) from isdn_syms.c to isdn_common.c
 *
 * Revision 1.49  1997/11/06 17:16:52  keil
 * Sync to 2.1.62 changes
 *
 * Revision 1.48  1997/11/02 23:55:50  keil
 * Andi Kleen's changes for 2.1.60
 * without it the isdninfo and isdnctrl devices won't work
 *
 * Revision 1.47  1997/10/09 21:28:46  fritz
 * New HL<->LL interface:
 *   New BSENT callback with nr. of bytes included.
 *   Sending without ACK.
 *   New L1 error status (not yet in use).
 *   Cleaned up obsolete structures.
 * Implemented Cisco-SLARP.
 * Changed local net-interface data to be dynamically allocated.
 * Removed old 2.0 compatibility stuff.
 *
 * Revision 1.46  1997/10/01 09:20:27  fritz
 * Removed old compatibility stuff for 2.0.X kernels.
 * From now on, this code is for 2.1.X ONLY!
 * Old stuff is still in the separate branch.
 *
 * Revision 1.45  1997/08/21 23:11:41  fritz
 * Added changes for kernels >= 2.1.45
 *
 * Revision 1.44  1997/05/27 15:17:23  fritz
 * Added changes for recent 2.1.x kernels:
 *   changed return type of isdn_close
 *   queue_task_* -> queue_task
 *   clear/set_bit -> test_and_... where apropriate.
 *   changed type of hard_header_cache parameter.
 *
 * Revision 1.43  1997/03/31 14:09:43  fritz
 * Fixed memory leak in isdn_close().
 *
 * Revision 1.42  1997/03/30 16:51:08  calle
 * changed calls to copy_from_user/copy_to_user and removed verify_area
 * were possible.
 *
 * Revision 1.41  1997/03/24 22:54:41  fritz
 * Some small fixes in debug code.
 *
 * Revision 1.40  1997/03/08 08:13:51  fritz
 * Bugfix: IIOCSETMAP (Set mapping) was broken.
 *
 * Revision 1.39  1997/03/07 01:32:54  fritz
 * Added proper ifdef's for CONFIG_ISDN_AUDIO
 *
 * Revision 1.38  1997/03/05 21:15:02  fritz
 * Fix: reduced stack usage of isdn_ioctl() and isdn_set_allcfg()
 *
 * Revision 1.37  1997/03/02 14:29:18  fritz
 * More ttyI related cleanup.
 *
 * Revision 1.36  1997/02/28 02:32:40  fritz
 * Cleanup: Moved some tty related stuff from isdn_common.c
 *          to isdn_tty.c
 * Bugfix:  Bisync protocol did not behave like documented.
 *
 * Revision 1.35  1997/02/21 13:01:19  fritz
 * Changes CAUSE message output in kernel log.
 *
 * Revision 1.34  1997/02/10 20:12:43  fritz
 * Changed interface for reporting incoming calls.
 *
 * Revision 1.33  1997/02/10 10:05:42  fritz
 * More changes for Kernel 2.1.X
 * Symbol information moved to isdn_syms.c
 *
 * Revision 1.32  1997/02/03 22:55:26  fritz
 * Reformatted according CodingStyle.
 * Changed isdn_writebuf_stub static.
 * Slow down tty-RING counter.
 * skb->free stuff replaced by macro.
 * Bugfix in audio-skb locking.
 * Bugfix in HL-driver locking.
 *
 * Revision 1.31  1997/01/17 01:19:18  fritz
 * Applied chargeint patch.
 *
 * Revision 1.30  1997/01/14 01:27:47  fritz
 * Changed audio receive not to rely on skb->users and skb->lock.
 * Added ATI2 and related variables.
 * Started adding full-duplex audio capability.
 *
 * Revision 1.29  1997/01/12 23:33:03  fritz
 * Made isdn_all_eaz foolproof.
 *
 * Revision 1.28  1996/11/13 02:33:19  fritz
 * Fixed a race condition.
 *
 * Revision 1.27  1996/10/27 22:02:41  keil
 * return codes for ISDN_STAT_ICALL
 *
 * Revision 1.26  1996/10/23 11:59:40  fritz
 * More compatibility changes.
 *
 * Revision 1.25  1996/10/22 23:13:54  fritz
 * Changes for compatibility to 2.0.X and 2.1.X kernels.
 *
 * Revision 1.24  1996/10/11 14:02:03  fritz
 * Bugfix: call to isdn_ppp_timer_timeout() never compiled, because of
 *         typo in #ifdef.
 *
 * Revision 1.23  1996/06/25 18:35:38  fritz
 * Fixed bogus memory access in isdn_set_allcfg().
 *
 * Revision 1.22  1996/06/24 17:37:37  fritz
 * Bugfix: isdn_timer_ctrl() did restart timer, even if it
 *         was already running.
 *         lowlevel driver locking did use wrong parameters.
 *
 * Revision 1.21  1996/06/15 14:58:20  fritz
 * Added version signatures for data structures used
 * by userlevel programs.
 *
 * Revision 1.20  1996/06/12 16:01:49  fritz
 * Bugfix: Remote B-channel hangup sometimes did not result
 *         in a NO CARRIER on tty.
 *
 * Revision 1.19  1996/06/11 14:52:04  hipp
 * minor bugfix in isdn_writebuf_skb_stub()
 *
 * Revision 1.18  1996/06/06 14:51:51  fritz
 * Changed to support DTMF decoding on audio playback also.
 *
 * Revision 1.17  1996/06/05 02:24:10  fritz
 * Added DTMF decoder for audio mode.
 *
 * Revision 1.16  1996/06/03 20:09:05  fritz
 * Bugfix: called wrong function pointer for locking in
 *         isdn_get_free_channel().
 *
 * Revision 1.15  1996/05/31 01:10:54  fritz
 * Bugfixes:
 *   Lowlevel modules did not get locked correctly.
 *   Did show wrong revision when initializing.
 *   Minor fixes in ioctl code.
 *   sk_buff did not get freed, if error in writebuf_stub.
 *
 * Revision 1.14  1996/05/18 01:36:55  fritz
 * Added spelling corrections and some minor changes
 * to stay in sync with kernel.
 *
 * Revision 1.13  1996/05/17 15:43:30  fritz
 * Bugfix: decrement of rcvcount in readbchan() corrected.
 *
 * Revision 1.12  1996/05/17 03:55:43  fritz
 * Changed DLE handling for audio receive.
 * Some cleanup.
 * Added display of isdn_audio_revision.
 *
 * Revision 1.11  1996/05/11 21:51:32  fritz
 * Changed queue management to use sk_buffs.
 *
 * Revision 1.10  1996/05/10 08:49:16  fritz
 * Checkin before major changes of tty-code.
 *
 * Revision 1.9  1996/05/07 09:19:41  fritz
 * Adapted to changes in isdn_tty.c
 *
 * Revision 1.8  1996/05/06 11:34:51  hipp
 * fixed a few bugs
 *
 * Revision 1.7  1996/05/02 03:55:17  fritz
 * Bugfixes:
 *  - B-channel connect message for modem devices
 *    sometimes did not result in a CONNECT-message.
 *  - register_isdn did not check for driverId-conflicts.
 *
 * Revision 1.6  1996/04/30 20:57:21  fritz
 * Commit test
 *
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

#include <linux/config.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/poll.h>
#include <linux/isdn.h>
#include "isdn_common.h"
#include "isdn_tty.h"
#include "isdn_net.h"
#include "isdn_ppp.h"
#ifdef CONFIG_ISDN_AUDIO
#include "isdn_audio.h"
#endif
#include "isdn_v110.h"
#include "isdn_cards.h"

/* Debugflags */
#undef ISDN_DEBUG_STATCALLB

isdn_dev *dev = (isdn_dev *) 0;

static char *isdn_revision = "$Revision: 1.55 $";

extern char *isdn_net_revision;
extern char *isdn_tty_revision;
#ifdef CONFIG_ISDN_PPP
extern char *isdn_ppp_revision;
#else
static char *isdn_ppp_revision = ": none $";
#endif
#ifdef CONFIG_ISDN_AUDIO
extern char *isdn_audio_revision;
#else
static char *isdn_audio_revision = ": none $";
#endif
extern char *isdn_v110_revision;

static int isdn_writebuf_stub(int, int, const u_char *, int, int);

void
isdn_MOD_INC_USE_COUNT(void)
{
	MOD_INC_USE_COUNT;
}

void
isdn_MOD_DEC_USE_COUNT(void)
{
	MOD_DEC_USE_COUNT;
}

#if defined(ISDN_DEBUG_NET_DUMP) || defined(ISDN_DEBUG_MODEM_DUMP)
void
isdn_dumppkt(char *s, u_char * p, int len, int dumplen)
{
	int dumpc;

	printk(KERN_DEBUG "%s(%d) ", s, len);
	for (dumpc = 0; (dumpc < dumplen) && (len); len--, dumpc++)
		printk(" %02x", *p++);
	printk("\n");
}
#endif

static void
isdn_free_queue(struct sk_buff_head *queue)
{
	struct sk_buff *skb;
	unsigned long flags;

	save_flags(flags);
	cli();
	if (skb_queue_len(queue))
		while ((skb = skb_dequeue(queue)))
			dev_kfree_skb(skb);
	restore_flags(flags);
}

int
isdn_dc2minor(int di, int ch)
{
	int i;
	for (i = 0; i < ISDN_MAX_CHANNELS; i++)
		if (dev->chanmap[i] == ch && dev->drvmap[i] == di)
			return i;
	return -1;
}

static int isdn_timer_cnt1 = 0;
static int isdn_timer_cnt2 = 0;
static int isdn_timer_cnt3 = 0;
static int isdn_timer_cnt4 = 0;

static void
isdn_timer_funct(ulong dummy)
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
			if (++isdn_timer_cnt3 > ISDN_TIMER_RINGING) {
				isdn_timer_cnt3 = 0;
				if (tf & ISDN_TIMER_MODEMRING)
					isdn_tty_modem_ring();
			}
			if (++isdn_timer_cnt4 > ISDN_TIMER_KEEPINT) {
				isdn_timer_cnt4 = 0;
				if (tf & ISDN_TIMER_KEEPALIVE)
					isdn_net_slarp_out();
			}
#if (defined CONFIG_ISDN_PPP) && (defined CONFIG_ISDN_MPP)
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
		dev->timer.expires = jiffies + ISDN_TIMER_RES;
		add_timer(&dev->timer);
		restore_flags(flags);
	}
}

void
isdn_timer_ctrl(int tf, int onoff)
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
		if (!del_timer(&dev->timer))	/* del_timer is 1, when active */
			dev->timer.expires = jiffies + ISDN_TIMER_RES;
		add_timer(&dev->timer);
	}
	restore_flags(flags);
}

/*
 * Receive a packet from B-Channel. (Called from low-level-module)
 */
static void
isdn_receive_skb_callback(int di, int channel, struct sk_buff *skb)
{
	int i;

	if ((i = isdn_dc2minor(di, channel)) == -1) {
		dev_kfree_skb(skb);
		return;
	}
	/* Update statistics */
	dev->ibytes[i] += skb->len;
	
	/* First, try to deliver data to network-device */
	if (isdn_net_rcv_skb(i, skb))
		return;

	/* V.110 handling
	 * makes sense for async streams only, so it is
	 * called after possible net-device delivery.
	 */
	if (dev->v110[i]) {
		atomic_inc(&dev->v110use[i]);
		skb = isdn_v110_decode(dev->v110[i], skb);
		atomic_dec(&dev->v110use[i]);
		if (!skb)
			return;
	}

	/* No network-device found, deliver to tty or raw-channel */
	if (skb->len) {
		if (isdn_tty_rcv_skb(i, di, channel, skb))
			return;
		wake_up_interruptible(&dev->drv[di]->rcv_waitq[channel]);
	} else
		dev_kfree_skb(skb);
}

/*
 * Intercept command from Linklevel to Lowlevel.
 * If layer 2 protocol is V.110 and this is not supported by current
 * lowlevel-driver, use driver's transparent mode and handle V.110 in
 * linklevel instead.
 */
int
isdn_command(isdn_ctrl *cmd)
{
	if (cmd->command == ISDN_CMD_SETL2) {
			int idx = isdn_dc2minor(cmd->driver, cmd->arg & 255);
			unsigned long l2prot = (cmd->arg >> 8) & 255;
			unsigned long features = (dev->drv[cmd->driver]->interface->features
						 >> ISDN_FEATURE_L2_SHIFT) &
				ISDN_FEATURE_L2_MASK;
			unsigned long l2_feature = (1 << l2prot);

			switch (l2prot) {
				case ISDN_PROTO_L2_V11096:
				case ISDN_PROTO_L2_V11019:
				case ISDN_PROTO_L2_V11038:
						/* If V.110 requested, but not supported by
						 * HL-driver, set emulator-flag and change
						 * Layer-2 to transparent
						 */
					if (!(features & l2_feature)) {
						dev->v110emu[idx] = l2prot;
						cmd->arg = (cmd->arg & 255) |
								   (ISDN_PROTO_L2_TRANS << 8);
					} else
						dev->v110emu[idx] = 0;
			}
	}
	return dev->drv[cmd->driver]->interface->command(cmd);
}

void
isdn_all_eaz(int di, int ch)
{
	isdn_ctrl cmd;

	if (di < 0)
		return;
	cmd.driver = di;
	cmd.arg = ch;
	cmd.command = ISDN_CMD_SETEAZ;
	cmd.parm.num[0] = '\0';
	isdn_command(&cmd);
}

static int
isdn_status_callback(isdn_ctrl * c)
{
	int di;
	ulong flags;
	int i;
	int r;
	int retval = 0;
	isdn_ctrl cmd;

	di = c->driver;
	i = isdn_dc2minor(di, c->arg);
	switch (c->command) {
		case ISDN_STAT_BSENT:
			if (i < 0)
				return -1;
			if (dev->global_flags & ISDN_GLOBAL_STOPPED)
				return 0;
			if (isdn_net_stat_callback(i, c))
				return 0;
			if (isdn_v110_stat_callback(i, c))
				return 0;
			if (isdn_tty_stat_callback(i, c))
				return 0;
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
			if (i < 0)
				return -1;
#ifdef ISDN_DEBUG_STATCALLB
			printk(KERN_DEBUG "ICALL (net): %d %ld %s\n", di, c->arg, c->parm.num);
#endif
			if (dev->global_flags & ISDN_GLOBAL_STOPPED) {
				cmd.driver = di;
				cmd.arg = c->arg;
				cmd.command = ISDN_CMD_HANGUP;
				isdn_command(&cmd);
				return 0;
			}
			/* Try to find a network-interface which will accept incoming call */
			cmd.driver = di;
			cmd.arg = c->arg;
			cmd.command = ISDN_CMD_LOCK;
			isdn_command(&cmd);
			r = isdn_net_find_icall(di, c->arg, i, c->parm.setup);
			switch (r) {
				case 0:
					/* No network-device replies.
					 * Try ttyI's
					 */
					if (isdn_tty_find_icall(di, c->arg, c->parm.setup) >= 0)
						retval = 1;
					else if (dev->drv[di]->reject_bus) {
						cmd.driver = di;
						cmd.arg = c->arg;
						cmd.command = ISDN_CMD_HANGUP;
						isdn_command(&cmd);
						retval = 2;
					}
					break;
				case 1:
					/* Schedule connection-setup */
					isdn_net_dial();
					cmd.driver = di;
					cmd.arg = c->arg;
					cmd.command = ISDN_CMD_ACCEPTD;
					isdn_command(&cmd);
					retval = 1;
					break;
				case 2:	/* For calling back, first reject incoming call ... */
				case 3:	/* Interface found, but down, reject call actively  */
					retval = 2;
					printk(KERN_INFO "isdn: Rejecting Call\n");
					cmd.driver = di;
					cmd.arg = c->arg;
					cmd.command = ISDN_CMD_HANGUP;
					isdn_command(&cmd);
					if (r == 3)
						break;
					/* Fall through */
				case 4:
					/* ... then start callback. */
					isdn_net_dial();
					break;
			}
			if (retval != 1) {
				cmd.driver = di;
				cmd.arg = c->arg;
				cmd.command = ISDN_CMD_UNLOCK;
				isdn_command(&cmd);
			}
			return retval;
			break;
		case ISDN_STAT_CINF:
			if (i < 0)
				return -1;
#ifdef ISDN_DEBUG_STATCALLB
			printk(KERN_DEBUG "CINF: %ld %s\n", c->arg, c->parm.num);
#endif
			if (dev->global_flags & ISDN_GLOBAL_STOPPED)
				return 0;
			if (strcmp(c->parm.num, "0"))
				isdn_net_stat_callback(i, c);
			isdn_tty_stat_callback(i, c);
			break;
		case ISDN_STAT_CAUSE:
#ifdef ISDN_DEBUG_STATCALLB
			printk(KERN_DEBUG "CAUSE: %ld %s\n", c->arg, c->parm.num);
#endif
			printk(KERN_INFO "isdn: %s,ch%ld cause: %s\n",
			       dev->drvid[di], c->arg, c->parm.num);
			isdn_tty_stat_callback(i, c);
			break;
		case ISDN_STAT_DCONN:
			if (i < 0)
				return -1;
#ifdef ISDN_DEBUG_STATCALLB
			printk(KERN_DEBUG "DCONN: %ld\n", c->arg);
#endif
			if (dev->global_flags & ISDN_GLOBAL_STOPPED)
				return 0;
			/* Find any net-device, waiting for D-channel setup */
			if (isdn_net_stat_callback(i, c))
				break;
			isdn_v110_stat_callback(i, c);
			/* Find any ttyI, waiting for D-channel setup */
			if (isdn_tty_stat_callback(i, c)) {
				cmd.driver = di;
				cmd.arg = c->arg;
				cmd.command = ISDN_CMD_ACCEPTB;
				isdn_command(&cmd);
				break;
			}
			break;
		case ISDN_STAT_DHUP:
			if (i < 0)
				return -1;
#ifdef ISDN_DEBUG_STATCALLB
			printk(KERN_DEBUG "DHUP: %ld\n", c->arg);
#endif
			if (dev->global_flags & ISDN_GLOBAL_STOPPED)
				return 0;
			dev->drv[di]->flags &= ~(1 << (c->arg));
			isdn_info_update();
			/* Signal hangup to network-devices */
			if (isdn_net_stat_callback(i, c))
				break;
			isdn_v110_stat_callback(i, c);
			if (isdn_tty_stat_callback(i, c))
				break;
			break;
		case ISDN_STAT_BCONN:
			if (i < 0)
				return -1;
#ifdef ISDN_DEBUG_STATCALLB
			printk(KERN_DEBUG "BCONN: %ld\n", c->arg);
#endif
			/* Signal B-channel-connect to network-devices */
			if (dev->global_flags & ISDN_GLOBAL_STOPPED)
				return 0;
			dev->drv[di]->flags |= (1 << (c->arg));
			isdn_info_update();
			if (isdn_net_stat_callback(i, c))
				break;
			isdn_v110_stat_callback(i, c);
			if (isdn_tty_stat_callback(i, c))
				break;
			break;
		case ISDN_STAT_BHUP:
			if (i < 0)
				return -1;
#ifdef ISDN_DEBUG_STATCALLB
			printk(KERN_DEBUG "BHUP: %ld\n", c->arg);
#endif
			if (dev->global_flags & ISDN_GLOBAL_STOPPED)
				return 0;
			dev->drv[di]->flags &= ~(1 << (c->arg));
			isdn_info_update();
#ifdef CONFIG_ISDN_X25
			/* Signal hangup to network-devices */
			if (isdn_net_stat_callback(i, c))
				break;
#endif
			isdn_v110_stat_callback(i, c);
			if (isdn_tty_stat_callback(i, c))
				break;
			break;
		case ISDN_STAT_NODCH:
			if (i < 0)
				return -1;
#ifdef ISDN_DEBUG_STATCALLB
			printk(KERN_DEBUG "NODCH: %ld\n", c->arg);
#endif
			if (dev->global_flags & ISDN_GLOBAL_STOPPED)
				return 0;
			if (isdn_net_stat_callback(i, c))
				break;
			if (isdn_tty_stat_callback(i, c))
				break;
			break;
		case ISDN_STAT_ADDCH:
			break;
		case ISDN_STAT_UNLOAD:
			save_flags(flags);
			cli();
			isdn_tty_stat_callback(i, c);
			for (i = 0; i < ISDN_MAX_CHANNELS; i++)
				if (dev->drvmap[i] == di) {
					dev->drvmap[i] = -1;
					dev->chanmap[i] = -1;
				}
			dev->drivers--;
			dev->channels -= dev->drv[di]->channels;
			kfree(dev->drv[di]->rcverr);
			kfree(dev->drv[di]->rcvcount);
			for (i = 0; i < dev->drv[di]->channels; i++)
				isdn_free_queue(&dev->drv[di]->rpqueue[i]);
			kfree(dev->drv[di]->rpqueue);
			kfree(dev->drv[di]->rcv_waitq);
			kfree(dev->drv[di]->snd_waitq);
			kfree(dev->drv[di]);
			dev->drv[di] = NULL;
			dev->drvid[di][0] = '\0';
			isdn_info_update();
			restore_flags(flags);
			return 0;
		case ISDN_STAT_L1ERR:
			break;
		default:
			return -1;
	}
	return 0;
}

/*
 * Get integer from char-pointer, set pointer to end of number
 */
int
isdn_getnum(char **p)
{
	int v = -1;

	while (*p[0] >= '0' && *p[0] <= '9')
		v = ((v < 0) ? 0 : (v * 10)) + (int) ((*p[0]++) - '0');
	return v;
}

#define DLE 0x10

/*
 * isdn_readbchan() tries to get data from the read-queue.
 * It MUST be called with interrupts off.
 *
 * Be aware that this is not an atomic operation when sleep != 0, even though 
 * interrupts are turned off! Well, like that we are currently only called
 * on behalf of a read system call on raw device files (which are documented
 * to be dangerous and for for debugging purpose only). The inode semaphore
 * takes care that this is not called for the same minor device number while
 * we are sleeping, but access is not serialized against simultaneous read()
 * from the corresponding ttyI device. Can other ugly events, like changes
 * of the mapping (di,ch)<->minor, happen during the sleep? --he 
 */
int
isdn_readbchan(int di, int channel, u_char * buf, u_char * fp, int len, struct wait_queue **sleep)
{
	int left;
	int count;
	int count_pull;
	int count_put;
	int dflag;
	struct sk_buff *skb;
	u_char *cp;

	if (!dev->drv[di])
		return 0;
	if (skb_queue_empty(&dev->drv[di]->rpqueue[channel])) {
		if (sleep)
			interruptible_sleep_on(sleep);
		else
			return 0;
	}
	left = MIN(len, dev->drv[di]->rcvcount[channel]);
	cp = buf;
	count = 0;
	while (left) {
		if (!(skb = skb_peek(&dev->drv[di]->rpqueue[channel])))
			break;
#ifdef CONFIG_ISDN_AUDIO
		if (ISDN_AUDIO_SKB_LOCK(skb))
			break;
		ISDN_AUDIO_SKB_LOCK(skb) = 1;
		if (ISDN_AUDIO_SKB_DLECOUNT(skb)) {
			char *p = skb->data;
			unsigned long DLEmask = (1 << channel);

			dflag = 0;
			count_pull = count_put = 0;
			while ((count_pull < skb->len) && (left-- > 0)) {
				if (dev->drv[di]->DLEflag & DLEmask) {
					*cp++ = DLE;
					dev->drv[di]->DLEflag &= ~DLEmask;
				} else {
					*cp++ = *p;
					if (*p == DLE) {
						dev->drv[di]->DLEflag |= DLEmask;
						(ISDN_AUDIO_SKB_DLECOUNT(skb))--;
					}
					p++;
					count_pull++;
				}
				count_put++;
			}
			if (count_pull >= skb->len)
				dflag = 1;
		} else {
#endif
			/* No DLE's in buff, so simply copy it */
			dflag = 1;
			if ((count_pull = skb->len) > left) {
				count_pull = left;
				dflag = 0;
			}
			count_put = count_pull;
			memcpy(cp, skb->data, count_put);
			cp += count_put;
			left -= count_put;
#ifdef CONFIG_ISDN_AUDIO
		}
#endif
		count += count_put;
		if (fp) {
			memset(fp, 0, count_put);
			fp += count_put;
		}
		if (dflag) {
			/* We got all the data in this buff.
			 * Now we can dequeue it.
			 */
			if (fp)
				*(fp - 1) = 0xff;
#ifdef CONFIG_ISDN_AUDIO
			ISDN_AUDIO_SKB_LOCK(skb) = 0;
#endif
			skb = skb_dequeue(&dev->drv[di]->rpqueue[channel]);
			dev_kfree_skb(skb);
		} else {
			/* Not yet emptied this buff, so it
			 * must stay in the queue, for further calls
			 * but we pull off the data we got until now.
			 */
			skb_pull(skb, count_pull);
#ifdef CONFIG_ISDN_AUDIO
			ISDN_AUDIO_SKB_LOCK(skb) = 0;
#endif
		}
		dev->drv[di]->rcvcount[channel] -= count_put;
	}
	return count;
}

static __inline int
isdn_minor2drv(int minor)
{
	return (dev->drvmap[minor]);
}

static __inline int
isdn_minor2chan(int minor)
{
	return (dev->chanmap[minor]);
}

#define INF_DV 0x01             /* Data version for /dev/isdninfo */

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

void
isdn_info_update(void)
{
	infostruct *p = dev->infochain;

	while (p) {
		*(p->private) = 1;
		p = (infostruct *) p->next;
	}
	wake_up_interruptible(&(dev->info_waitq));
}

static ssize_t
isdn_read(struct file *file, char *buf, size_t count, loff_t * off)
{
	uint minor = MINOR(file->f_dentry->d_inode->i_rdev);
	int len = 0;
	ulong flags;
	int drvidx;
	int chidx;
	char *p;

	if (off != &file->f_pos)
		return -ESPIPE;

	if (minor == ISDN_MINOR_STATUS) {
		if (!file->private_data) {
			if (file->f_flags & O_NONBLOCK)
				return -EAGAIN;
			interruptible_sleep_on(&(dev->info_waitq));
		}
		p = isdn_statstr();
		file->private_data = 0;
		if ((len = strlen(p)) <= count) {
			if (copy_to_user(buf, p, len))
				return -EFAULT;
			*off += len;
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
		if(  ! (p = kmalloc(count,GFP_KERNEL))  ) return -ENOMEM;
		save_flags(flags);
		cli();
		len = isdn_readbchan(drvidx, chidx, p, 0, count,
				     &dev->drv[drvidx]->rcv_waitq[chidx]);
		*off += len;
		restore_flags(flags);
		if( copy_to_user(buf,p,len) ) len = -EFAULT;
		kfree(p);
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
			    readstat(buf, MIN(count, dev->drv[drvidx]->stavail),
				     1, drvidx, isdn_minor2chan(minor));
		else
			len = 0;
		save_flags(flags);
		cli();
		if (len)
			dev->drv[drvidx]->stavail -= len;
		else
			dev->drv[drvidx]->stavail = 0;
		restore_flags(flags);
		*off += len;
		return len;
	}
#ifdef CONFIG_ISDN_PPP
	if (minor <= ISDN_MINOR_PPPMAX)
		return (isdn_ppp_read(minor - ISDN_MINOR_PPP, file, buf, count));
#endif
	return -ENODEV;
}

static loff_t
isdn_lseek(struct file *file, loff_t offset, int orig)
{
	return -ESPIPE;
}

static ssize_t
isdn_write(struct file *file, const char *buf, size_t count, loff_t * off)
{
	uint minor = MINOR(file->f_dentry->d_inode->i_rdev);
	int drvidx;
	int chidx;

	if (off != &file->f_pos)
		return -ESPIPE;

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
			return (dev->drv[drvidx]->interface->
				writecmd(buf, count, 1, drvidx, isdn_minor2chan(minor)));
		else
			return count;
	}
#ifdef CONFIG_ISDN_PPP
	if (minor <= ISDN_MINOR_PPPMAX)
		return (isdn_ppp_write(minor - ISDN_MINOR_PPP, file, buf, count));
#endif
	return -ENODEV;
}

static unsigned int
isdn_poll(struct file *file, poll_table * wait)
{
	unsigned int mask = 0;
	unsigned int minor = MINOR(file->f_dentry->d_inode->i_rdev);
	int drvidx = isdn_minor2drv(minor - ISDN_MINOR_CTRL);

	if (minor == ISDN_MINOR_STATUS) {
		poll_wait(file, &(dev->info_waitq), wait);
		/* mask = POLLOUT | POLLWRNORM; */
		if (file->private_data) {
			mask |= POLLIN | POLLRDNORM;
		}
		return mask;
	}
	if (minor >= ISDN_MINOR_CTRL && minor <= ISDN_MINOR_CTRLMAX) {
		poll_wait(file, &(dev->drv[drvidx]->st_waitq), wait);
		if (drvidx < 0) {
			printk(KERN_ERR "isdn_common: isdn_poll 1 -> what the hell\n");
			return POLLERR;
		}
		mask = POLLOUT | POLLWRNORM;
		if (dev->drv[drvidx]->stavail) {
			mask |= POLLIN | POLLRDNORM;
		}
		return mask;
	}
#ifdef CONFIG_ISDN_PPP
	if (minor <= ISDN_MINOR_PPPMAX)
		return (isdn_ppp_poll(file, wait));
#endif
	printk(KERN_ERR "isdn_common: isdn_poll 2 -> what the hell\n");
	return POLLERR;
}

/* 
 * This accesses user space with interrupts off, but is not needed by
 * any of the isdn4k-util programs anyway. Thus, in contrast to your
 * first impression after looking at the code, fixing is trival!*/
#if 0 
static int
isdn_set_allcfg(char *src)
{
	int ret;
	int i;
	ulong flags;
	isdn_net_ioctl_cfg cfg;
	isdn_net_ioctl_phone phone;

	if ((ret = isdn_net_rmall()))
		return ret;
	if (copy_from_user((char *) &i, src, sizeof(int))) return -EFAULT;
	save_flags(flags);
	cli();
	src += sizeof(int);
	while (i) {
		int phone_len;
		int out_flag;

		if (copy_from_user((char *) &cfg, src, sizeof(cfg))) {
			restore_flags(flags);
			return -EFAULT;
		}
		src += sizeof(cfg);
		if (!isdn_net_new(cfg.name, NULL)) {
			restore_flags(flags);
			return -EIO;
		}
		if ((ret = isdn_net_setcfg(&cfg))) {
			restore_flags(flags);
			return ret;
		}
		phone_len = out_flag = 0;
		while (out_flag < 2) {
			if ((ret = verify_area(VERIFY_READ, src, 1))) {
				restore_flags(flags);
				return ret;
			}
			get_user(phone.phone[phone_len], src++);
			if ((phone.phone[phone_len] == ' ') ||
			    (phone.phone[phone_len] == '\0')) {
				if (phone_len) {
					phone.phone[phone_len] = '\0';
					strcpy(phone.name, cfg.name);
					phone.outgoing = out_flag;
					if ((ret = isdn_net_addphone(&phone))) {
						restore_flags(flags);
						return ret;
					}
				} else
					out_flag++;
				phone_len = 0;
			}
			if (++phone_len >= sizeof(phone.phone))
				printk(KERN_WARNING
				       "%s: IIOCSETSET phone number too long, ignored\n",
				       cfg.name);
		}
		i--;
	}
	restore_flags(flags);
	return 0;
}

static int
isdn_get_allcfg(char *dest)
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
		isdn_net_local *lp = p->local;

		if ((ret = verify_area(VERIFY_WRITE, (void *) dest, sizeof(cfg) + 200))) {
			restore_flags(flags);
			return ret;
		}
		strcpy(cfg.eaz, lp->msn);
		cfg.exclusive = lp->exclusive;
		if (lp->pre_device >= 0) {
			sprintf(cfg.drvid, "%s,%d", dev->drvid[lp->pre_device],
				lp->pre_channel);
		} else
			cfg.drvid[0] = '\0';
		cfg.onhtime = lp->onhtime;
		cfg.charge = lp->charge;
		cfg.l2_proto = lp->l2_proto;
		cfg.l3_proto = lp->l3_proto;
		cfg.p_encap = lp->p_encap;
		cfg.secure = (lp->flags & ISDN_NET_SECURE) ? 1 : 0;
		cfg.callback = (lp->flags & ISDN_NET_CALLBACK) ? 1 : 0;
		cfg.chargehup = (lp->hupflags & ISDN_CHARGEHUP) ? 1 : 0;
		cfg.ihup = (lp->hupflags & ISDN_INHUP) ? 1 : 0;
		cfg.chargeint = lp->chargeint;
		if (copy_to_user(dest, lp->name, 10)) {
			restore_flags(flags);
			return -EFAULT;
		}
		dest += 10;
		if (copy_to_user(dest, (char *) &cfg, sizeof(cfg))) {
			restore_flags(flags);
			return -EFAULT;
		}
		dest += sizeof(cfg);
		strcpy(phone.name, lp->name);
		phone.outgoing = 0;
		if ((ret = isdn_net_getphones(&phone, dest)) < 0) {
			restore_flags(flags);
			return ret;
		} else
			dest += ret;
		strcpy(phone.name, lp->name);
		phone.outgoing = 1;
		if ((ret = isdn_net_getphones(&phone, dest)) < 0) {
			restore_flags(flags);
			return ret;
		} else
			dest += ret;
		put_user(0, dest);
		p = p->next;
	}
	restore_flags(flags);
	return 0;
}
#endif

static int
isdn_ioctl(struct inode *inode, struct file *file, uint cmd, ulong arg)
{
	uint minor = MINOR(inode->i_rdev);
	isdn_ctrl c;
	int drvidx;
	int chidx;
	int ret;
	int i;
	char *p;
	char *s;
	union iocpar {
		char name[10];
		char bname[22];
		isdn_ioctl_struct iocts;
		isdn_net_ioctl_phone phone;
		isdn_net_ioctl_cfg cfg;
	} iocpar;

#define name  iocpar.name
#define bname iocpar.bname
#define iocts iocpar.iocts
#define phone iocpar.phone
#define cfg   iocpar.cfg

	if (minor == ISDN_MINOR_STATUS) {
		switch (cmd) {
			case IIOCGETDVR:
				return (TTY_DV +
					(NET_DV << 8) +
					(INF_DV << 16));
			case IIOCGETCPS:
				if (arg) {
					ulong *p = (ulong *) arg;
					int i;
					if ((ret = verify_area(VERIFY_WRITE, (void *) arg,
							       sizeof(ulong) * ISDN_MAX_CHANNELS * 2)))
						return ret;
					for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
						put_user(dev->ibytes[i], p++);
						put_user(dev->obytes[i], p++);
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
/*
 * isdn net devices manage lots of configuration variables as linked lists.
 * Those lists must only be manipulated from user space. Some of the ioctl's
 * service routines access user space and are not atomic. Therefor, ioctl's
 * manipulating the lists and ioctl's sleeping while accessing the lists
 * are serialized by means of a semaphore.
 */
		switch (cmd) {
#ifdef CONFIG_NETDEVICES
			case IIOCNETAIF:
				/* Add a network-interface */
				if (arg) {
					if (copy_from_user(name, (char *) arg, sizeof(name)))
						return -EFAULT;
					s = name;
				} else {
					s = NULL;
				}
				ret = down_interruptible(&dev->sem);
				if( ret ) return ret;
				if ((s = isdn_net_new(s, NULL))) {
					if (copy_to_user((char *) arg, s, strlen(s) + 1)){
						ret = -EFAULT;
					} else {
						ret = 0;
					}
				} else
					ret = -ENODEV;
				up(&dev->sem);
				return ret;
			case IIOCNETASL:
				/* Add a slave to a network-interface */
				if (arg) {
					if (copy_from_user(bname, (char *) arg, sizeof(bname) - 1))
						return -EFAULT;
				} else
					return -EINVAL;
				ret = down_interruptible(&dev->sem);
				if( ret ) return ret;
				if ((s = isdn_net_newslave(bname))) {
					if (copy_to_user((char *) arg, s, strlen(s) + 1)){
						ret = -EFAULT;
					} else {
						ret = 0;
					}
				} else
					ret = -ENODEV;
				up(&dev->sem);
				return ret;
			case IIOCNETDIF:
				/* Delete a network-interface */
				if (arg) {
					if (copy_from_user(name, (char *) arg, sizeof(name)))
						return -EFAULT;
					ret = down_interruptible(&dev->sem);
					if( ret ) return ret;
					ret = isdn_net_rm(name);
					up(&dev->sem);
					return ret;
				} else
					return -EINVAL;
			case IIOCNETSCF:
				/* Set configurable parameters of a network-interface */
				if (arg) {
					if (copy_from_user((char *) &cfg, (char *) arg, sizeof(cfg)))
						return -EFAULT;
					return isdn_net_setcfg(&cfg);
				} else
					return -EINVAL;
			case IIOCNETGCF:
				/* Get configurable parameters of a network-interface */
				if (arg) {
					if (copy_from_user((char *) &cfg, (char *) arg, sizeof(cfg)))
						return -EFAULT;
					if (!(ret = isdn_net_getcfg(&cfg))) {
						if (copy_to_user((char *) arg, (char *) &cfg, sizeof(cfg)))
							return -EFAULT;
					}
					return ret;
				} else
					return -EINVAL;
			case IIOCNETANM:
				/* Add a phone-number to a network-interface */
				if (arg) {
					if (copy_from_user((char *) &phone, (char *) arg, sizeof(phone)))
						return -EFAULT;
					ret = down_interruptible(&dev->sem);
					if( ret ) return ret;
					ret = isdn_net_addphone(&phone);
					up(&dev->sem);
					return ret;
				} else
					return -EINVAL;
			case IIOCNETGNM:
				/* Get list of phone-numbers of a network-interface */
				if (arg) {
					if (copy_from_user((char *) &phone, (char *) arg, sizeof(phone)))
						return -EFAULT;
					ret = down_interruptible(&dev->sem);
					if( ret ) return ret;
					ret = isdn_net_getphones(&phone, (char *) arg);
					up(&dev->sem);
					return ret;
				} else
					return -EINVAL;
			case IIOCNETDNM:
				/* Delete a phone-number of a network-interface */
				if (arg) {
					if (copy_from_user((char *) &phone, (char *) arg, sizeof(phone)))
						return -EFAULT;
					ret = down_interruptible(&dev->sem);
					if( ret ) return ret;
					ret = isdn_net_delphone(&phone);
					up(&dev->sem);
					return ret;
				} else
					return -EINVAL;
			case IIOCNETDIL:
				/* Force dialing of a network-interface */
				if (arg) {
					if (copy_from_user(name, (char *) arg, sizeof(name)))
						return -EFAULT;
					return isdn_net_force_dial(name);
				} else
					return -EINVAL;
#ifdef CONFIG_ISDN_PPP
			case IIOCNETALN:
				if (!arg)
					return -EINVAL;
				if (copy_from_user(name, (char *) arg, sizeof(name)))
					return -EFAULT;
				return isdn_ppp_dial_slave(name);
			case IIOCNETDLN:
				if (!arg)
					return -EINVAL;
				if (copy_from_user(name, (char *) arg, sizeof(name)))
					return -EFAULT;
				return isdn_ppp_hangup_slave(name);
#endif
			case IIOCNETHUP:
				/* Force hangup of a network-interface */
				if (!arg)
					return -EINVAL;
				if (copy_from_user(name, (char *) arg, sizeof(name)))
					return -EFAULT;
				return isdn_net_force_hangup(name);
				break;
#endif                          /* CONFIG_NETDEVICES */
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
					if (copy_from_user((char *) &iocts, (char *) arg,
					     sizeof(isdn_ioctl_struct)))
						return -EFAULT;
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
#if 0
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
#endif
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
						if (copy_to_user(p, dev->mdm.info[i].emu.profile,
						      ISDN_MODEM_ANZREG))
							return -EFAULT;
						p += ISDN_MODEM_ANZREG;
						if (copy_to_user(p, dev->mdm.info[i].emu.pmsn, ISDN_MSNLEN))
							return -EFAULT;
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
						if (copy_from_user(dev->mdm.info[i].emu.profile, p,
						     ISDN_MODEM_ANZREG))
							return -EFAULT;
						p += ISDN_MODEM_ANZREG;
						if (copy_from_user(dev->mdm.info[i].emu.pmsn, p, ISDN_MSNLEN))
							return -EFAULT;
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

					if (copy_from_user((char *) &iocts,
							    (char *) arg,
					     sizeof(isdn_ioctl_struct)))
						return -EFAULT;
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
						int loop = 1;

						p = (char *) iocts.arg;
						i = 0;
						while (loop) {
							int j = 0;

							while (1) {
								if ((ret = verify_area(VERIFY_READ, p, 1)))
									return ret;
								get_user(bname[j], p++);
								switch (bname[j]) {
									case '\0':
										loop = 0;
										/* Fall through */
									case ',':
										bname[j] = '\0';
										strcpy(dev->drv[drvidx]->msn2eaz[i], bname);
										j = ISDN_MSNLEN;
										break;
									default:
										j++;
								}
								if (j >= ISDN_MSNLEN)
									break;
							}
							if (++i > 9)
								break;
						}
					} else {
						p = (char *) iocts.arg;
						for (i = 0; i < 10; i++) {
							sprintf(bname, "%s%s",
								strlen(dev->drv[drvidx]->msn2eaz[i]) ?
								dev->drv[drvidx]->msn2eaz[i] : "-",
								(i < 9) ? "," : "\0");
							if (copy_to_user(p, bname, strlen(bname) + 1))
								return -EFAULT;
							p += strlen(bname);
						}
					}
					return 0;
				} else
					return -EINVAL;
			case IIOCDBGVAR:
				if (arg) {
					if (copy_to_user((char *) arg, (char *) &dev, sizeof(ulong)))
						return -EFAULT;
					return 0;
				} else
					return -EINVAL;
				break;
			default:
				if ((cmd & IIOCDRVCTL) == IIOCDRVCTL)
					cmd = ((cmd >> _IOC_NRSHIFT) & _IOC_NRMASK) & ISDN_DRVIOCTL_MASK;
				else
					return -EINVAL;
				if (arg) {
					int i;
					char *p;
					if (copy_from_user((char *) &iocts, (char *) arg, sizeof(isdn_ioctl_struct)))
						return -EFAULT;
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
					memcpy(c.parm.num, (char *) &iocts.arg, sizeof(ulong));
					ret = isdn_command(&c);
					memcpy((char *) &iocts.arg, c.parm.num, sizeof(ulong));
					if (copy_to_user((char *) arg, &iocts, sizeof(isdn_ioctl_struct)))
						return -EFAULT;
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

#undef name
#undef bname
#undef iocts
#undef phone
#undef cfg
}

/*
 * Open the device code.
 * MOD_INC_USE_COUNT make sure that the driver memory is not freed
 * while the device is in use.
 */
static int
isdn_open(struct inode *ino, struct file *filep)
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
		isdn_command(&c);
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
		isdn_command(&c);
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

static int
isdn_close(struct inode *ino, struct file *filep)
{
	uint minor = MINOR(ino->i_rdev);
	int drvidx;
	isdn_ctrl c;

	MOD_DEC_USE_COUNT;
	if (minor == ISDN_MINOR_STATUS) {
		infostruct *p = dev->infochain;
		infostruct *q = NULL;
		while (p) {
			if (p->private == (char *) &(filep->private_data)) {
				if (q)
					q->next = p->next;
				else
					dev->infochain = (infostruct *) (p->next);
				kfree(p);
				return 0;
			}
			q = p;
			p = (infostruct *) (p->next);
		}
		printk(KERN_WARNING "isdn: No private data while closing isdnctrl\n");
		return 0;
	}
	if (minor < ISDN_MINOR_CTRL) {
		drvidx = isdn_minor2drv(minor);
		if (drvidx < 0)
			return 0;
		c.command = ISDN_CMD_UNLOCK;
		c.driver = drvidx;
		isdn_command(&c);
		return 0;
	}
	if (minor <= ISDN_MINOR_CTRLMAX) {
		drvidx = isdn_minor2drv(minor - ISDN_MINOR_CTRL);
		if (drvidx < 0)
			return 0;
		if (dev->profd == current)
			dev->profd = NULL;
		c.command = ISDN_CMD_UNLOCK;
		c.driver = drvidx;
		isdn_command(&c);
		return 0;
	}
#ifdef CONFIG_ISDN_PPP
	if (minor <= ISDN_MINOR_PPPMAX)
		isdn_ppp_release(minor - ISDN_MINOR_PPP, filep);
#endif
	return 0;
}

static struct file_operations isdn_fops =
{
	isdn_lseek,
	isdn_read,
	isdn_write,
	NULL,                   /* isdn_readdir */
	isdn_poll,              /* isdn_poll */
	isdn_ioctl,             /* isdn_ioctl */
	NULL,                   /* isdn_mmap */
	isdn_open,
	NULL,			/* flush */
	isdn_close,
	NULL                    /* fsync */
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
#define L2V (~(ISDN_FEATURE_L2_V11096|ISDN_FEATURE_L2_V11019|ISDN_FEATURE_L2_V11038))

int
isdn_get_free_channel(int usage, int l2_proto, int l3_proto, int pre_dev
		      ,int pre_chan)
{
	int i;
	ulong flags;
	ulong features;
	ulong vfeatures;
	isdn_ctrl cmd;

	save_flags(flags);
	cli();
	features = ((1 << l2_proto) | (0x10000 << l3_proto));
	vfeatures = (((1 << l2_proto) | (0x10000 << l3_proto)) &
		     ~(ISDN_FEATURE_L2_V11096|ISDN_FEATURE_L2_V11019|ISDN_FEATURE_L2_V11038));
	/* If Layer-2 protocol is V.110, accept drivers with
	 * transparent feature even if these don't support V.110
	 * because we can emulate this in linklevel.
	 */
	for (i = 0; i < ISDN_MAX_CHANNELS; i++)
		if (USG_NONE(dev->usage[i]) &&
		    (dev->drvmap[i] != -1)) {
			int d = dev->drvmap[i];
			if ((dev->usage[i] & ISDN_USAGE_EXCLUSIVE) &&
			((pre_dev != d) || (pre_chan != dev->chanmap[i])))
				continue;
			if ((dev->drv[d]->running)) {
				if (((dev->drv[d]->interface->features & features) == features) ||
				    (((dev->drv[d]->interface->features & vfeatures) == vfeatures) &&
				     (dev->drv[d]->interface->features & ISDN_FEATURE_L2_TRANS))) {
					if ((pre_dev < 0) || (pre_chan < 0)) {
						dev->usage[i] &= ISDN_USAGE_EXCLUSIVE;
						dev->usage[i] |= usage;
						isdn_info_update();
						cmd.driver = d;
						cmd.arg = 0;
						cmd.command = ISDN_CMD_LOCK;
						isdn_command(&cmd);
						restore_flags(flags);
						return i;
					} else {
						if ((pre_dev == d) && (pre_chan == dev->chanmap[i])) {
							dev->usage[i] &= ISDN_USAGE_EXCLUSIVE;
							dev->usage[i] |= usage;
							isdn_info_update();
							cmd.driver = d;
							cmd.arg = 0;
							cmd.command = ISDN_CMD_LOCK;
							isdn_command(&cmd);
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
void
isdn_free_channel(int di, int ch, int usage)
{
	int i;
	ulong flags;
	isdn_ctrl cmd;

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
			isdn_free_queue(&dev->drv[di]->rpqueue[ch]);
			cmd.driver = di;
			cmd.arg = ch;
			cmd.command = ISDN_CMD_UNLOCK;
			restore_flags(flags);
			isdn_command(&cmd);
			return;
		}
	restore_flags(flags);
}

/*
 * Cancel Exclusive-Flag for ISDN-channel
 */
void
isdn_unexclusive_channel(int di, int ch)
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
 *  writebuf replacement for SKB_ABLE drivers
 */
static int
isdn_writebuf_stub(int drvidx, int chan, const u_char * buf, int len,
		   int user)
{
	int ret;
	int hl = dev->drv[drvidx]->interface->hl_hdrlen;
	struct sk_buff *skb = alloc_skb(hl + len, GFP_ATOMIC);

	if (!skb)
		return 0;
	skb_reserve(skb, hl);
	if (user)
		copy_from_user(skb_put(skb, len), buf, len);
	else
		memcpy(skb_put(skb, len), buf, len);

	ret = dev->drv[drvidx]->interface->writebuf_skb(drvidx, chan, 1, skb);
	if (ret <= 0)
		dev_kfree_skb(skb);
	if (ret > 0)
		dev->obytes[isdn_dc2minor(drvidx, chan)] += ret;
	return ret;
}

/*
 * Return: length of data on success, -ERRcode on failure.
 */
int
isdn_writebuf_skb_stub(int drvidx, int chan, int ack, struct sk_buff *skb)
{
	int ret;
	struct sk_buff *nskb = NULL;
	int v110_ret = skb->len;
	int idx = isdn_dc2minor(drvidx, chan);

	if (dev->v110[idx]) {
		atomic_inc(&dev->v110use[idx]);
		nskb = isdn_v110_encode(dev->v110[idx], skb);
		atomic_dec(&dev->v110use[idx]);
		if (!nskb)
			return 0;
		v110_ret = *((int *)nskb->data);
		skb_pull(nskb, sizeof(int));
		if (!nskb->len) {
			dev_kfree_skb(nskb);
			dev_kfree_skb(skb);
			return v110_ret;
		}
		/* V.110 must always be acknowledged */
		ack = 1;
		ret = dev->drv[drvidx]->interface->writebuf_skb(drvidx, chan, ack, nskb);
	} else
		ret = dev->drv[drvidx]->interface->writebuf_skb(drvidx, chan, ack, skb);
	if (ret > 0) {
		dev->obytes[idx] += ret;
		if (dev->v110[idx]) {
			atomic_inc(&dev->v110use[idx]);
			dev->v110[idx]->skbuser++;
			atomic_dec(&dev->v110use[idx]);
			dev_kfree_skb(skb);
			/* For V.110 return unencoded data length */
			ret = v110_ret;
			if (ret == skb->len)
				dev_kfree_skb(skb);
		}
	} else
		if (dev->v110[idx])
			dev_kfree_skb(nskb);
	return ret;
}

/*
 * Low-level-driver registration
 */

EXPORT_SYMBOL(register_isdn);

int
register_isdn(isdn_if * i)
{
	driver *d;
	int n,
	 j,
	 k;
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
	if (!i->writebuf_skb) {
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
	if (!(d->rpqueue =
	      (struct sk_buff_head *) kmalloc(sizeof(struct sk_buff_head) * n, GFP_KERNEL))) {
		printk(KERN_WARNING "register_isdn: Could not alloc rpqueue\n");
		kfree(d->rcvcount);
		kfree(d->rcverr);
		kfree(d);
		return 0;
	}
	for (j = 0; j < n; j++) {
		skb_queue_head_init(&d->rpqueue[j]);
	}
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
	i->statcallb = isdn_status_callback;
	if (!strlen(i->id))
		sprintf(i->id, "line%d", drvidx);
	save_flags(flags);
	cli();
	for (j = 0; j < drvidx; j++)
		if (!strcmp(i->id, dev->drvid[j]))
			sprintf(i->id, "line%d", drvidx);
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

static char *
isdn_getrev(const char *revision)
{
	char *rev;
	char *p;

	if ((p = strchr(revision, ':'))) {
		rev = p + 2;
		p = strchr(rev, '$');
		*--p = 0;
	} else
		rev = "???";
	return rev;
}

/*
 * Allocate and initialize all data, register modem-devices
 */
int
isdn_init(void)
{
	int i;
	char tmprev[50];

	sti();
	if (!(dev = (isdn_dev *) kmalloc(sizeof(isdn_dev), GFP_KERNEL))) {
		printk(KERN_WARNING "isdn: Could not allocate device-struct.\n");
		return -EIO;
	}
	memset((char *) dev, 0, sizeof(isdn_dev));
	init_timer(&dev->timer);
	dev->timer.function = isdn_timer_funct;
	dev->sem = MUTEX;
	for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
		dev->drvmap[i] = -1;
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
#endif                          /* CONFIG_ISDN_PPP */

	strcpy(tmprev, isdn_revision);
	printk(KERN_NOTICE "ISDN subsystem Rev: %s/", isdn_getrev(tmprev));
	strcpy(tmprev, isdn_tty_revision);
	printk("%s/", isdn_getrev(tmprev));
	strcpy(tmprev, isdn_net_revision);
	printk("%s/", isdn_getrev(tmprev));
	strcpy(tmprev, isdn_ppp_revision);
	printk("%s/", isdn_getrev(tmprev));
	strcpy(tmprev, isdn_audio_revision);
	printk("%s/", isdn_getrev(tmprev));
	strcpy(tmprev, isdn_v110_revision);
	printk("%s", isdn_getrev(tmprev));

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
void
cleanup_module(void)
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
	for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
		isdn_tty_cleanup_xmit(&dev->mdm.info[i]);
		kfree(dev->mdm.info[i].xmit_buf - 4);
	}
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
