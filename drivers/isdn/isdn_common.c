/* $Id: isdn_common.c,v 1.101 2000/04/07 14:50:34 calle Exp $

 * Linux ISDN subsystem, common used functions (linklevel).
 *
 * Copyright 1994-1999  by Fritz Elfert (fritz@isdn4linux.de)
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
 * Revision 1.101  2000/04/07 14:50:34  calle
 * Bugfix: on my system 2.3.99-pre3 Dual PII 350, unload of module isdn.o
 *         hangs if vfree is called with interrupt disabled. After moving
 * 	restore_flags in front of vfree it doesn't hang.
 *
 * Revision 1.100  2000/03/03 16:37:11  kai
 * incorporated some cosmetic changes from the official kernel tree back
 * into CVS
 *
 * Revision 1.99  2000/02/26 01:00:52  keil
 * changes from 2.3.47
 *
 * Revision 1.98  2000/02/16 14:56:27  paul
 * translated ISDN_MODEM_ANZREG to ISDN_MODEM_NUMREG for english speakers
 *
 * Revision 1.97  2000/01/23 18:45:37  keil
 * Change EAZ mapping to forbit the use of cards (insert a "-" for the MSN)
 *
 * Revision 1.96  2000/01/20 19:55:33  keil
 * Add FAX Class 1 support
 *
 * Revision 1.95  2000/01/09 20:43:13  detabc
 * exand logical bind-group's for both call's (in and out).
 * add first part of kernel-config-help for abc-extension.
 *
 * Revision 1.94  1999/11/20 22:14:13  detabc
 * added channel dial-skip in case of external use
 * (isdn phone or another isdn device) on the same NTBA.
 * usefull with two or more card's connected the different NTBA's.
 * global switchable in kernel-config and also per netinterface.
 *
 * add auto disable of netinterface's in case of:
 * 	to many connection's in short time.
 * 	config mistakes (wrong encapsulation, B2-protokoll or so on) on local
 * 	or remote side.
 * 	wrong password's or something else to a ISP (syncppp).
 *
 * possible encapsulations for this future are:
 * ISDN_NET_ENCAP_SYNCPPP, ISDN_NET_ENCAP_UIHDLC, ISDN_NET_ENCAP_RAWIP,
 * and ISDN_NET_ENCAP_CISCOHDLCK.
 *
 * Revision 1.93  1999/11/04 13:11:36  keil
 * Reinit of v110 structs
 *
 * Revision 1.92  1999/10/31 15:59:50  he
 * more skb headroom checks
 *
 * Revision 1.91  1999/10/28 22:48:45  armin
 * Bugfix: isdn_free_channel() now frees the channel,
 * even when the usage of the ttyI has changed.
 *
 * Revision 1.90  1999/10/27 21:21:17  detabc
 * Added support for building logically-bind-group's per interface.
 * usefull for outgoing call's with more then one isdn-card.
 *
 * Switchable support to dont reset the hangup-timeout for
 * receive frames. Most part's of the timru-rules for receiving frames
 * are now obsolete. If the input- or forwarding-firewall deny
 * the frame, the line will be not hold open.
 *
 * Revision 1.89  1999/10/16 14:46:47  keil
 * replace kmalloc with vmalloc for the big dev struct
 *
 * Revision 1.88  1999/10/02 00:39:26  he
 * Fixed a 2.3.x wait queue initialization (was causing panics)
 *
 * Revision 1.87  1999/09/12 16:19:39  detabc
 * added abc features
 * low cost routing for net-interfaces (only the HL side).
 * need more implementation in the isdnlog-utility
 * udp info support (first part).
 * different EAZ on outgoing call's.
 * more checks on D-Channel callbacks (double use of channels).
 * tested and running with kernel 2.3.17
 *
 * Revision 1.86  1999/07/31 12:59:42  armin
 * Added tty fax capabilities.
 *
 * Revision 1.85  1999/07/29 16:58:35  armin
 * Bugfix: DLE handling in isdn_readbchan()
 *
 * Revision 1.84  1999/07/25 16:21:10  keil
 * fix number matching
 *
 * Revision 1.83  1999/07/13 21:02:05  werner
 * Added limit possibilty of driver b_channel resources (ISDN_STAT_DISCH)
 *
 * Revision 1.82  1999/07/12 21:06:50  werner
 * Fixed problem when loading more than one driver temporary
 *
 * Revision 1.81  1999/07/11 17:14:09  armin
 * Added new layer 2 and 3 protocols for Fax and DSP functions.
 * Moved "Add CPN to RING message" to new register S23,
 * "Display message" is now correct on register S13 bit 7.
 * New audio command AT+VDD implemented (deactivate DTMF decoder and
 * activate possible existing hardware/DSP decoder).
 * Moved some tty defines to .h file.
 * Made whitespace possible in AT command line.
 * Some AT-emulator output bugfixes.
 * First Fax G3 implementations.
 *
 * Revision 1.80  1999/07/07 10:14:00  detabc
 * remove unused messages
 *
 * Revision 1.79  1999/07/05 23:51:30  werner
 * Allow limiting of available HiSax B-chans per card. Controlled by hisaxctrl
 * hisaxctrl id 10 <nr. of chans 0-2>
 *
 * Revision 1.78  1999/07/05 20:21:15  werner
 * changes to use diversion sources for all kernel versions.
 * removed static device, only proc filesystem used
 *
 * Revision 1.77  1999/07/01 08:29:50  keil
 * compatibility to 2.3 kernel
 *
 * Revision 1.76  1999/06/29 16:16:44  calle
 * Let ISDN_CMD_UNLOAD work with open isdn devices without crash again.
 * Also right unlocking (ISDN_CMD_UNLOCK) is done now.
 * isdnlog should check returncode of read(2) calls.
 *
 * Revision 1.75  1999/04/18 14:06:47  fritz
 * Removed TIMRU stuff.
 *
 * Revision 1.74  1999/04/12 13:16:45  fritz
 * Changes from 2.0 tree.
 *
 * Revision 1.73  1999/04/12 12:33:15  fritz
 * Changes from 2.0 tree.
 *
 * Revision 1.72  1999/03/02 12:04:44  armin
 * -added ISDN_STAT_ADDCH to increase supported channels after
 *  register_isdn().
 * -ttyI now goes on-hook on ATZ when B-Ch is connected.
 * -added timer-function for register S7 (Wait for Carrier).
 * -analog modem (ISDN_PROTO_L2_MODEM) implementations.
 * -on L2_MODEM a string will be appended to the CONNECT-Message,
 *  which is provided by the HL-Driver in parm.num in ISDN_STAT_BCONN.
 * -variable "dialing" used for ATA also, for interrupting call
 *  establishment and register S7.
 *
 * Revision 1.71  1999/01/28 09:10:43  armin
 * Fixed bad while-loop in isdn_readbch().
 *
 * Revision 1.70  1999/01/15 19:58:54  he
 * removed compatibiltity macro
 *
 * Revision 1.69  1998/09/07 21:59:58  he
 * flush method for 2.1.118 and above
 * updated IIOCTLNETGPN
 *
 * Revision 1.68  1998/08/31 21:09:45  he
 * new ioctl IIOCNETGPN for /dev/isdninfo (get network interface'
 *     peer phone number)
 *
 * Revision 1.67  1998/06/26 15:12:21  fritz
 * Added handling of STAT_ICALL with incomplete CPN.
 * Added AT&L for ttyI emulator.
 * Added more locking stuff in tty_write.
 *
 * Revision 1.66  1998/06/17 19:50:41  he
 * merged with 2.1.10[34] (cosmetics and udelay() -> mdelay())
 * brute force fix to avoid Ugh's in isdn_tty_write()
 * cleaned up some dead code
 *
 *
 * Revision 1.62  1998/04/14 16:28:43  he
 * Fixed user space access with interrupts off and remaining
 * copy_{to,from}_user() -> -EFAULT return codes
 *
 * Revision 1.61  1998/03/22 18:50:46  hipp
 * Added BSD Compression for syncPPP .. UNTESTED at the moment
 *
 * Revision 1.60  1998/03/19 13:18:18  keil
 * Start of a CAPI like interface for supplementary Service
 * first service: SUSPEND
 *
 * Revision 1.59  1998/03/09 17:46:23  he
 * merged in 2.1.89 changes
 *
 * Revision 1.58  1998/03/07 22:35:24  fritz
 * Starting generic module support (Nothing usable yet).
 *
 * Revision 1.57  1998/03/07 18:21:01  cal
 * Dynamic Timeout-Rule-Handling vs. 971110 included
 *
 * Revision 1.56  1998/02/25 17:49:38  he
 * Changed return codes caused be failing copy_{to,from}_user to -EFAULT
 *
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
#include <linux/vmalloc.h>
#include <linux/isdn.h>
#include "isdn_common.h"
#include "isdn_tty.h"
#include "isdn_net.h"
#include "isdn_ppp.h"
#ifdef CONFIG_ISDN_AUDIO
#include "isdn_audio.h"
#endif
#ifdef CONFIG_ISDN_DIVERSION
#include <linux/isdn_divertif.h>
#endif CONFIG_ISDN_DIVERSION
#include "isdn_v110.h"
#include "isdn_cards.h"
#include <linux/devfs_fs_kernel.h>

/* Debugflags */
#undef ISDN_DEBUG_STATCALLB

isdn_dev *dev = (isdn_dev *) 0;

static char *isdn_revision = "$Revision: 1.101 $";

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

#ifdef CONFIG_ISDN_DIVERSION
isdn_divert_if *divert_if = NULL; /* interface to diversion module */
#endif CONFIG_ISDN_DIVERSION


static int isdn_writebuf_stub(int, int, const u_char *, int, int);
static void set_global_features(void);
static void isdn_register_devfs(int);
static void isdn_unregister_devfs(int);

void
isdn_MOD_INC_USE_COUNT(void)
{
	int i;

	MOD_INC_USE_COUNT;
	for (i = 0; i < dev->drivers; i++) {
		isdn_ctrl cmd;

		cmd.driver = i;
		cmd.arg = 0;
		cmd.command = ISDN_CMD_LOCK;
		isdn_command(&cmd);
		dev->drv[i]->locks++;
	}
}

void
isdn_MOD_DEC_USE_COUNT(void)
{
	int i;

	MOD_DEC_USE_COUNT;
	for (i = 0; i < dev->drivers; i++)
		if (dev->drv[i]->locks > 0) {
			isdn_ctrl cmd;

			cmd.driver = i;
			cmd.arg = 0;
			cmd.command = ISDN_CMD_UNLOCK;
			isdn_command(&cmd);
			dev->drv[i]->locks--;
		}
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

/*
 * I picked the pattern-matching-functions from an old GNU-tar version (1.10)
 * It was originally written and put to PD by rs@mirror.TMC.COM (Rich Salz)
 */
static int
isdn_star(char *s, char *p)
{
	while (isdn_wildmat(s, p)) {
		if (*++s == '\0')
			return (2);
	}
	return (0);
}

/*
 * Shell-type Pattern-matching for incoming caller-Ids
 * This function gets a string in s and checks, if it matches the pattern
 * given in p.
 *
 * Return:
 *   0 = match.
 *   1 = no match.
 *   2 = no match. Would eventually match, if s would be longer.
 *
 * Possible Patterns:
 *
 * '?'     matches one character
 * '*'     matches zero or more characters
 * [xyz]   matches the set of characters in brackets.
 * [^xyz]  matches any single character not in the set of characters
 */

int
isdn_wildmat(char *s, char *p)
{
	register int last;
	register int matched;
	register int reverse;
	register int nostar = 1;

	if (!(*s) && !(*p))
		return(1);
	for (; *p; s++, p++)
		switch (*p) {
			case '\\':
				/*
				 * Literal match with following character,
				 * fall through.
				 */
				p++;
			default:
				if (*s != *p)
					return (*s == '\0')?2:1;
				continue;
			case '?':
				/* Match anything. */
				if (*s == '\0')
					return (2);
				continue;
			case '*':
				nostar = 0;	
				/* Trailing star matches everything. */
				return (*++p ? isdn_star(s, p) : 0);
			case '[':
				/* [^....] means inverse character class. */
				if ((reverse = (p[1] == '^')))
					p++;
				for (last = 0, matched = 0; *++p && (*p != ']'); last = *p)
					/* This next line requires a good C compiler. */
					if (*p == '-' ? *s <= *++p && *s >= last : *s == *p)
						matched = 1;
				if (matched == reverse)
					return (1);
				continue;
		}
	return (*s == '\0')?0:nostar;
}

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
			if (tf & ISDN_TIMER_CARRIER)
				isdn_tty_carrier_timeout();
#if (defined CONFIG_ISDN_PPP) && (defined CONFIG_ISDN_MPP)
			if (tf & ISDN_TIMER_IPPP)
				isdn_ppp_timer_timeout();
#endif
		}
	}
	if (tf) 
	{
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
	if (cmd->driver == -1) {
		printk(KERN_WARNING "isdn_command command(%x) driver -1\n", cmd->command);
		return(1);
	}
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

/*
 * Begin of a CAPI like LL<->HL interface, currently used only for 
 * supplementary service (CAPI 2.0 part III)
 */
#include "avmb1/capicmd.h"  /* this should be moved in a common place */

int
isdn_capi_rec_hl_msg(capi_msg *cm) {
	
	int di;
	int ch;
	
	di = (cm->adr.Controller & 0x7f) -1;
	ch = isdn_dc2minor(di, (cm->adr.Controller>>8)& 0x7f);
	switch(cm->Command) {
		case CAPI_FACILITY:
			/* in the moment only handled in tty */
			return(isdn_tty_capi_facility(cm));
		default:
			return(-1);
	}
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
			dev->drv[di]->flags |= DRV_FLAG_RUNNING;
			for (i = 0; i < ISDN_MAX_CHANNELS; i++)
				if (dev->drvmap[i] == di)
					isdn_all_eaz(di, dev->chanmap[i]);
			set_global_features();
			break;
		case ISDN_STAT_STOP:
			dev->drv[di]->flags &= ~DRV_FLAG_RUNNING;
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
			r = ((c->command == ISDN_STAT_ICALLW) ? 0 : isdn_net_find_icall(di, c->arg, i, c->parm.setup));
			switch (r) {
				case 0:
					/* No network-device replies.
					 * Try ttyI's.
					 * These return 0 on no match, 1 on match and
					 * 3 on eventually match, if CID is longer.
					 */
                                        if (c->command == ISDN_STAT_ICALL)
					  if ((retval = isdn_tty_find_icall(di, c->arg, c->parm.setup))) return(retval);
#ifdef CONFIG_ISDN_DIVERSION 
                                         if (divert_if)
                 	                  if ((retval = divert_if->stat_callback(c))) 
					    return(retval); /* processed */
#endif CONFIG_ISDN_DIVERSION                        
					if ((!retval) && (dev->drv[di]->flags & DRV_FLAG_REJBUS)) {
						/* No tty responding */
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
				case 5:
					/* Number would eventually match, if longer */
					retval = 3;
					break;
			}
#ifdef ISDN_DEBUG_STATCALLB
			printk(KERN_DEBUG "ICALL: ret=%d\n", retval);
#endif
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
#ifdef CONFIG_ISDN_DIVERSION
                        if (divert_if)
                         divert_if->stat_callback(c); 
#endif CONFIG_ISDN_DIVERSION
			break;
		case ISDN_STAT_DISPLAY:
#ifdef ISDN_DEBUG_STATCALLB
			printk(KERN_DEBUG "DISPLAY: %ld %s\n", c->arg, c->parm.display);
#endif
			isdn_tty_stat_callback(i, c);
#ifdef CONFIG_ISDN_DIVERSION
                        if (divert_if)
                         divert_if->stat_callback(c); 
#endif CONFIG_ISDN_DIVERSION
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
			dev->drv[di]->online &= ~(1 << (c->arg));
			isdn_info_update();
			/* Signal hangup to network-devices */
			if (isdn_net_stat_callback(i, c))
				break;
			isdn_v110_stat_callback(i, c);
			if (isdn_tty_stat_callback(i, c))
				break;
#ifdef CONFIG_ISDN_DIVERSION
                        if (divert_if)
                         divert_if->stat_callback(c); 
#endif CONFIG_ISDN_DIVERSION
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
			dev->drv[di]->online |= (1 << (c->arg));
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
			dev->drv[di]->online &= ~(1 << (c->arg));
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
			if (isdn_add_channels(dev->drv[di], di, c->arg, 1))
				return -1;
			isdn_info_update();
			break;
		case ISDN_STAT_DISCH:
			save_flags(flags);
			cli();
			for (i = 0; i < ISDN_MAX_CHANNELS; i++)
				if ((dev->drvmap[i] == di) &&
				    (dev->chanmap[i] == c->arg)) {
				    if (c->parm.num[0])
				      dev->usage[i] &= ~ISDN_USAGE_DISABLED;
				    else
				      if (USG_NONE(dev->usage[i])) {
					dev->usage[i] |= ISDN_USAGE_DISABLED;
				      }
				      else 
					retval = -1;
				    break;
				}
			restore_flags(flags);
			isdn_info_update();
			break;
		case ISDN_STAT_UNLOAD:
			while (dev->drv[di]->locks > 0) {
				isdn_ctrl cmd;
				cmd.driver = di;
				cmd.arg = 0;
				cmd.command = ISDN_CMD_UNLOCK;
				isdn_command(&cmd);
				dev->drv[di]->locks--;
			}
			save_flags(flags);
			cli();
			isdn_tty_stat_callback(i, c);
			for (i = 0; i < ISDN_MAX_CHANNELS; i++)
				if (dev->drvmap[i] == di) {
					dev->drvmap[i] = -1;
					dev->chanmap[i] = -1;
					dev->usage[i] &= ~ISDN_USAGE_DISABLED;
					isdn_unregister_devfs(i);
				}
			dev->drivers--;
			dev->channels -= dev->drv[di]->channels;
			kfree(dev->drv[di]->rcverr);
			kfree(dev->drv[di]->rcvcount);
			for (i = 0; i < dev->drv[di]->channels; i++)
				isdn_free_queue(&dev->drv[di]->rpqueue[i]);
			kfree(dev->drv[di]->rpqueue);
			kfree(dev->drv[di]->rcv_waitq);
			kfree(dev->drv[di]);
			dev->drv[di] = NULL;
			dev->drvid[di][0] = '\0';
			isdn_info_update();
			set_global_features();
			restore_flags(flags);
			return 0;
		case ISDN_STAT_L1ERR:
			break;
		case CAPI_PUT_MESSAGE:
			return(isdn_capi_rec_hl_msg(&c->parm.cmsg));
#ifdef CONFIG_ISDN_TTY_FAX
		case ISDN_STAT_FAXIND:
			isdn_tty_stat_callback(i, c);
			break;
#endif
#ifdef CONFIG_ISDN_AUDIO
		case ISDN_STAT_AUDIO:
			isdn_tty_stat_callback(i, c);
			break;
#endif
#ifdef CONFIG_ISDN_DIVERSION
	        case ISDN_STAT_PROT:
	        case ISDN_STAT_REDIR:
                        if (divert_if)
                          return(divert_if->stat_callback(c));
#endif CONFIG_ISDN_DIVERSION
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
isdn_readbchan(int di, int channel, u_char * buf, u_char * fp, int len, wait_queue_head_t *sleep)
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
		if ((ISDN_AUDIO_SKB_DLECOUNT(skb)) || (dev->drv[di]->DLEflag & (1 << channel))) {
			char *p = skb->data;
			unsigned long DLEmask = (1 << channel);

			dflag = 0;
			count_pull = count_put = 0;
			while ((count_pull < skb->len) && (left > 0)) {
				left--;
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
			sprintf(p, "%ld ", dev->drv[i]->online);
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
		if (!(dev->drv[drvidx]->flags & DRV_FLAG_RUNNING))
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
		if (!(dev->drv[drvidx]->flags & DRV_FLAG_RUNNING))
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
		 if (!(dev->drv[drvidx]->flags & DRV_FLAG_RUNNING))
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
		if (drvidx < 0) {
			/* driver deregistered while file open */
		        return POLLHUP;
		}
		poll_wait(file, &(dev->drv[drvidx]->st_waitq), wait);
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
#ifdef CONFIG_NETDEVICES
			case IIOCNETGPN:
				/* Get peer phone number of a connected 
				 * isdn network interface */
				if (arg) {
					if (copy_from_user((char *) &phone, (char *) arg, sizeof(phone)))
						return -EFAULT;
					return isdn_net_getpeer(&phone, (isdn_net_ioctl_phone *) arg);
				} else
					return -EINVAL;
#endif
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
		if (!(dev->drv[drvidx]->flags & DRV_FLAG_RUNNING))
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
			case IIOCNETDWRSET:
				printk(KERN_INFO "INFO: ISDN_DW_ABC_EXTENSION not enabled\n");
				return(-EINVAL);
			case IIOCNETLCR:
				printk(KERN_INFO "INFO: ISDN_ABC_LCR_SUPPORT not enabled\n");
				return -ENODEV;
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
				if (iocts.arg)
					dev->drv[drvidx]->flags |= DRV_FLAG_REJBUS;
				else
					dev->drv[drvidx]->flags &= ~DRV_FLAG_REJBUS;
				return 0;
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
					(ISDN_MODEM_NUMREG + ISDN_MSNLEN + ISDN_LMSNLEN)
						   * ISDN_MAX_CHANNELS)))
						return ret;

					for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
						if (copy_to_user(p, dev->mdm.info[i].emu.profile,
						      ISDN_MODEM_NUMREG))
							return -EFAULT;
						p += ISDN_MODEM_NUMREG;
						if (copy_to_user(p, dev->mdm.info[i].emu.pmsn, ISDN_MSNLEN))
							return -EFAULT;
						p += ISDN_MSNLEN;
						if (copy_to_user(p, dev->mdm.info[i].emu.plmsn, ISDN_LMSNLEN))
							return -EFAULT;
						p += ISDN_LMSNLEN;
					}
					return (ISDN_MODEM_NUMREG + ISDN_MSNLEN + ISDN_LMSNLEN) * ISDN_MAX_CHANNELS;
				} else
					return -EINVAL;
				break;
			case IIOCSETPRF:
				/* Set all Modem-Profiles */
				if (arg) {
					char *p = (char *) arg;
					int i;

					if ((ret = verify_area(VERIFY_READ, (void *) arg,
					(ISDN_MODEM_NUMREG + ISDN_MSNLEN)
						   * ISDN_MAX_CHANNELS)))
						return ret;

					for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
						if (copy_from_user(dev->mdm.info[i].emu.profile, p,
						     ISDN_MODEM_NUMREG))
							return -EFAULT;
						p += ISDN_MODEM_NUMREG;
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
								dev->drv[drvidx]->msn2eaz[i] : "_",
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
		if (!(dev->drv[drvidx]->flags & DRV_FLAG_RUNNING))
			return -ENODEV;
		if (!(dev->drv[drvidx]->online & (1 << chidx)))
			return -ENODEV;
		isdn_MOD_INC_USE_COUNT();
		return 0;
	}
	if (minor <= ISDN_MINOR_CTRLMAX) {
		drvidx = isdn_minor2drv(minor - ISDN_MINOR_CTRL);
		if (drvidx < 0)
			return -ENODEV;
		isdn_MOD_INC_USE_COUNT();
		return 0;
	}
#ifdef CONFIG_ISDN_PPP
	if (minor <= ISDN_MINOR_PPPMAX) {
		int ret;
		if (!(ret = isdn_ppp_open(minor - ISDN_MINOR_PPP, filep)))
			isdn_MOD_INC_USE_COUNT();
		return ret;
	}
#endif
	return -ENODEV;
}

static int
isdn_close(struct inode *ino, struct file *filep)
{
	uint minor = MINOR(ino->i_rdev);

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
				kfree(p);
				return 0;
			}
			q = p;
			p = (infostruct *) (p->next);
		}
		printk(KERN_WARNING "isdn: No private data while closing isdnctrl\n");
		return 0;
	}
	isdn_MOD_DEC_USE_COUNT();
	if (minor < ISDN_MINOR_CTRL)
		return 0;
	if (minor <= ISDN_MINOR_CTRLMAX) {
		if (dev->profd == current)
			dev->profd = NULL;
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
	llseek:		isdn_lseek,
	read:		isdn_read,
	write:		isdn_write,
	poll:		isdn_poll,
	ioctl:		isdn_ioctl,
	open:		isdn_open,
	release:	isdn_close,
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
		      ,int pre_chan, char *msn)
{
	int i;
	ulong flags;
	ulong features;
	ulong vfeatures;

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
			if (!strcmp(isdn_map_eaz2msn(msn, d), "-"))
				continue;
			if (dev->usage[i] & ISDN_USAGE_DISABLED)
			        continue; /* usage not allowed */
			if (dev->drv[d]->flags & DRV_FLAG_RUNNING) {
				if (((dev->drv[d]->interface->features & features) == features) ||
				    (((dev->drv[d]->interface->features & vfeatures) == vfeatures) &&
				     (dev->drv[d]->interface->features & ISDN_FEATURE_L2_TRANS))) {
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
void
isdn_free_channel(int di, int ch, int usage)
{
	int i;
	ulong flags;

	save_flags(flags);
	cli();
	for (i = 0; i < ISDN_MAX_CHANNELS; i++)
		if (((!usage) || ((dev->usage[i] & ISDN_USAGE_MASK) == usage)) &&
		    (dev->drvmap[i] == di) &&
		    (dev->chanmap[i] == ch)) {
			dev->usage[i] &= (ISDN_USAGE_NONE | ISDN_USAGE_EXCLUSIVE);
			strcpy(dev->num[i], "???");
			dev->ibytes[i] = 0;
			dev->obytes[i] = 0;
// 20.10.99 JIM, try to reinitialize v110 !
			dev->v110emu[i] = 0;
			atomic_set(&(dev->v110use[i]), 0);
			isdn_v110_close(dev->v110[i]);
			dev->v110[i] = NULL;
// 20.10.99 JIM, try to reinitialize v110 !
			isdn_info_update();
			isdn_free_queue(&dev->drv[di]->rpqueue[ch]);
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
	} else {
		int hl = dev->drv[drvidx]->interface->hl_hdrlen;

		if( skb_headroom(skb) < hl ){
			/* 
			 * This should only occur when new HL driver with
			 * increased hl_hdrlen was loaded after netdevice
			 * was created and connected to the new driver.
			 *
			 * The V.110 branch (re-allocates on its own) does
			 * not need this
			 */
			struct sk_buff * skb_tmp;

			skb_tmp = skb_realloc_headroom(skb, hl);
			printk(KERN_DEBUG "isdn_writebuf_skb_stub: reallocating headroom%s\n", skb_tmp ? "" : " failed");
			if (!skb_tmp) return -ENOMEM; /* 0 better? */
			ret = dev->drv[drvidx]->interface->writebuf_skb(drvidx, chan, ack, skb_tmp);
			if( ret > 0 ){
				dev_kfree_skb(skb);
			} else {
				dev_kfree_skb(skb_tmp);
			}
		} else {
			ret = dev->drv[drvidx]->interface->writebuf_skb(drvidx, chan, ack, skb);
		}
	}
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

int
register_isdn_module(isdn_module *m) {
	return 0;
}

int
unregister_isdn_module(isdn_module *m) {
	return 0;
}

int
isdn_add_channels(driver *d, int drvidx, int n, int adding)
{
	int j, k, m;
	ulong flags;

	init_waitqueue_head(&d->st_waitq);
	if (d->flags & DRV_FLAG_RUNNING)
		return -1;
       	if (n < 1) return 0;

	m = (adding) ? d->channels + n : n;

	if (dev->channels + n > ISDN_MAX_CHANNELS) {
		printk(KERN_WARNING "register_isdn: Max. %d channels supported\n",
		       ISDN_MAX_CHANNELS);
		return -1;
	}

	if ((adding) && (d->rcverr))
		kfree(d->rcverr);
	if (!(d->rcverr = (int *) kmalloc(sizeof(int) * m, GFP_KERNEL))) {
		printk(KERN_WARNING "register_isdn: Could not alloc rcverr\n");
		return -1;
	}
	memset((char *) d->rcverr, 0, sizeof(int) * m);

	if ((adding) && (d->rcvcount))
		kfree(d->rcvcount);
	if (!(d->rcvcount = (int *) kmalloc(sizeof(int) * m, GFP_KERNEL))) {
		printk(KERN_WARNING "register_isdn: Could not alloc rcvcount\n");
		if (!adding) kfree(d->rcverr);
		return -1;
	}
	memset((char *) d->rcvcount, 0, sizeof(int) * m);

	if ((adding) && (d->rpqueue)) {
		for (j = 0; j < d->channels; j++)
			isdn_free_queue(&d->rpqueue[j]);
		kfree(d->rpqueue);
	}
	if (!(d->rpqueue =
	      (struct sk_buff_head *) kmalloc(sizeof(struct sk_buff_head) * m, GFP_KERNEL))) {
		printk(KERN_WARNING "register_isdn: Could not alloc rpqueue\n");
		if (!adding) {
			kfree(d->rcvcount);
			kfree(d->rcverr);
		}
		return -1; 
	}
	for (j = 0; j < m; j++) {
		skb_queue_head_init(&d->rpqueue[j]);
	}

	if ((adding) && (d->rcv_waitq))
		kfree(d->rcv_waitq);
	d->rcv_waitq = (wait_queue_head_t *)
		kmalloc(sizeof(wait_queue_head_t) * 2 * m, GFP_KERNEL);
	if (!d->rcv_waitq) {
		printk(KERN_WARNING "register_isdn: Could not alloc rcv_waitq\n");
		if (!adding) {
			kfree(d->rpqueue);
			kfree(d->rcvcount);
			kfree(d->rcverr);
		}
		return -1;
	}
	d->snd_waitq = d->rcv_waitq + m;
	for (j = 0; j < m; j++) {
		init_waitqueue_head(&d->rcv_waitq[j]);
		init_waitqueue_head(&d->snd_waitq[j]);
	}

	dev->channels += n;
	save_flags(flags);
	cli();
	for (j = d->channels; j < m; j++)
		for (k = 0; k < ISDN_MAX_CHANNELS; k++)
			if (dev->chanmap[k] < 0) {
				dev->chanmap[k] = j;
				dev->drvmap[k] = drvidx;
				isdn_register_devfs(k);
				break;
			}
	restore_flags(flags);
	d->channels = m;
	return 0;
}

/*
 * Low-level-driver registration
 */

static void
set_global_features(void)
{
	int drvidx;

	dev->global_features = 0;
	for (drvidx = 0; drvidx < ISDN_MAX_DRIVERS; drvidx++) {
		if (!dev->drv[drvidx])
			continue;
		if (dev->drv[drvidx]->interface)
			dev->global_features |= dev->drv[drvidx]->interface->features;
	}
}

#ifdef CONFIG_ISDN_DIVERSION
extern isdn_divert_if *divert_if;

static char *map_drvname(int di)
{
  if ((di < 0) || (di >= ISDN_MAX_DRIVERS)) 
    return(NULL);
  return(dev->drvid[di]); /* driver name */
} /* map_drvname */

static int map_namedrv(char *id)
{  int i;

   for (i = 0; i < ISDN_MAX_DRIVERS; i++)
    { if (!strcmp(dev->drvid[i],id)) 
        return(i);
    }
   return(-1);
} /* map_namedrv */

int DIVERT_REG_NAME(isdn_divert_if *i_div)
{
  if (i_div->if_magic != DIVERT_IF_MAGIC) 
    return(DIVERT_VER_ERR);
  switch (i_div->cmd)
    {
      case DIVERT_CMD_REL:
        if (divert_if != i_div) 
          return(DIVERT_REL_ERR);
        divert_if = NULL; /* free interface */
        MOD_DEC_USE_COUNT;
        return(DIVERT_NO_ERR);

      case DIVERT_CMD_REG:
        if (divert_if) 
          return(DIVERT_REG_ERR);
        i_div->ll_cmd = isdn_command; /* set command function */
        i_div->drv_to_name = map_drvname; 
        i_div->name_to_drv = map_namedrv; 
        MOD_INC_USE_COUNT;
        divert_if = i_div; /* remember interface */
        return(DIVERT_NO_ERR);

      default:
        return(DIVERT_CMD_ERR);   
    }
} /* DIVERT_REG_NAME */

EXPORT_SYMBOL(DIVERT_REG_NAME);

#endif CONFIG_ISDN_DIVERSION


EXPORT_SYMBOL(register_isdn);
EXPORT_SYMBOL(register_isdn_module);
EXPORT_SYMBOL(unregister_isdn_module);
#ifdef CONFIG_ISDN_PPP
EXPORT_SYMBOL(isdn_ppp_register_compressor);
EXPORT_SYMBOL(isdn_ppp_unregister_compressor);
#endif

int
register_isdn(isdn_if * i)
{
	driver *d;
	int j;
	ulong flags;
	int drvidx;

	if (dev->drivers >= ISDN_MAX_DRIVERS) {
		printk(KERN_WARNING "register_isdn: Max. %d drivers supported\n",
		       ISDN_MAX_DRIVERS);
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

	d->maxbufsize = i->maxbufsize;
	d->pktcount = 0;
	d->stavail = 0;
	d->flags = DRV_FLAG_LOADED;
	d->online = 0;
	d->interface = i;
	d->channels = 0;
	for (drvidx = 0; drvidx < ISDN_MAX_DRIVERS; drvidx++)
		if (!dev->drv[drvidx])
			break;
	if (isdn_add_channels(d, drvidx, i->channels, 0)) {
		kfree(d);
		return 0;
	}
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
	dev->drv[drvidx] = d;
	strcpy(dev->drvid[drvidx], i->id);
	isdn_info_update();
	dev->drivers++;
	set_global_features();
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

#ifdef CONFIG_DEVFS_FS

static devfs_handle_t devfs_handle = NULL;

static void isdn_register_devfs(int k)
{
	char buf[11];

	sprintf (buf, "isdn%d", k);
	dev->devfs_handle_isdnX[k] =
	    devfs_register (devfs_handle, buf, 0, DEVFS_FL_DEFAULT,
			    ISDN_MAJOR, ISDN_MINOR_B + k,0600 | S_IFCHR, 0, 0,
			    &isdn_fops, NULL);
	sprintf (buf, "isdnctrl%d", k);
	dev->devfs_handle_isdnctrlX[k] =
	    devfs_register (devfs_handle, buf, 0, DEVFS_FL_DEFAULT,
			    ISDN_MAJOR, ISDN_MINOR_CTRL + k, 0600 | S_IFCHR,
			    0, 0, &isdn_fops, NULL);
}

static void isdn_unregister_devfs(int k)
{
	devfs_unregister (dev->devfs_handle_isdnX[k]);
	devfs_unregister (dev->devfs_handle_isdnctrlX[k]);
}

static void isdn_init_devfs(void)
{
#  ifdef CONFIG_ISDN_PPP
	int i;
#  endif

	devfs_handle = devfs_mk_dir (NULL, "isdn", 4, NULL);
#  ifdef CONFIG_ISDN_PPP
	for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
		char buf[8];

		sprintf (buf, "ippp%d", i);
		dev->devfs_handle_ipppX[i] =
		    devfs_register (devfs_handle, buf, 0, DEVFS_FL_DEFAULT,
				    ISDN_MAJOR, ISDN_MINOR_PPP + i,
				    0600 | S_IFCHR, 0, 0, &isdn_fops, NULL);
	}
#  endif

	dev->devfs_handle_isdninfo =
	    devfs_register (devfs_handle, "isdninfo", 0, DEVFS_FL_DEFAULT,
			    ISDN_MAJOR, ISDN_MINOR_STATUS, 0600 | S_IFCHR,
			    0, 0, &isdn_fops, NULL);
	dev->devfs_handle_isdnctrl =
	    devfs_register (devfs_handle, "isdnctrl", 0, DEVFS_FL_DEFAULT,
			    ISDN_MAJOR, ISDN_MINOR_CTRL, 0600 | S_IFCHR, 0, 0, 
			    &isdn_fops, NULL);
}

static void isdn_cleanup_devfs(void)
{
#  ifdef CONFIG_ISDN_PPP
	int i;
	for (i = 0; i < ISDN_MAX_CHANNELS; i++) 
		devfs_unregister (dev->devfs_handle_ipppX[i]);
#  endif
	devfs_unregister (dev->devfs_handle_isdninfo);
	devfs_unregister (dev->devfs_handle_isdnctrl);
	devfs_unregister (devfs_handle);
}

#else   /* CONFIG_DEVFS_FS */
static void isdn_register_devfs(int dummy)
{
	return;
}

static void isdn_unregister_devfs(int dummy)
{
	return;
}

static void isdn_init_devfs(void)
{
    return;
}

static void isdn_cleanup_devfs(void)
{
    return;
}

#endif  /* CONFIG_DEVFS_FS */

/*
 * Allocate and initialize all data, register modem-devices
 */
int
isdn_init(void)
{
	int i;
	char tmprev[50];

	if (!(dev = (isdn_dev *) vmalloc(sizeof(isdn_dev)))) {
		printk(KERN_WARNING "isdn: Could not allocate device-struct.\n");
		return -EIO;
	}
	memset((char *) dev, 0, sizeof(isdn_dev));
	init_timer(&dev->timer);
	dev->timer.function = isdn_timer_funct;
	init_MUTEX(&dev->sem);
	init_waitqueue_head(&dev->info_waitq);
	for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
		dev->drvmap[i] = -1;
		dev->chanmap[i] = -1;
		dev->m_idx[i] = -1;
		strcpy(dev->num[i], "???");
		init_waitqueue_head(&dev->mdm.info[i].open_wait);
		init_waitqueue_head(&dev->mdm.info[i].close_wait);
	}
	if (devfs_register_chrdev(ISDN_MAJOR, "isdn", &isdn_fops)) {
		printk(KERN_WARNING "isdn: Could not register control devices\n");
		vfree(dev);
		return -EIO;
	}
	isdn_init_devfs();
	if ((i = isdn_tty_modem_init()) < 0) {
		printk(KERN_WARNING "isdn: Could not register tty devices\n");
		if (i == -3)
			tty_unregister_driver(&dev->mdm.cua_modem);
		if (i <= -2)
			tty_unregister_driver(&dev->mdm.tty_modem);
		vfree(dev);
		isdn_cleanup_devfs();
		devfs_unregister_chrdev(ISDN_MAJOR, "isdn");
		return -EIO;
	}
#ifdef CONFIG_ISDN_PPP
	if (isdn_ppp_init() < 0) {
		printk(KERN_WARNING "isdn: Could not create PPP-device-structs\n");
		tty_unregister_driver(&dev->mdm.tty_modem);
		tty_unregister_driver(&dev->mdm.cua_modem);
		for (i = 0; i < ISDN_MAX_CHANNELS; i++)
			kfree(dev->mdm.info[i].xmit_buf - 4);
		isdn_cleanup_devfs();
		devfs_unregister_chrdev(ISDN_MAJOR, "isdn");
		vfree(dev);
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
#ifdef CONFIG_ISDN_TTY_FAX
		kfree(dev->mdm.info[i].fax);
#endif
	}
	if (devfs_unregister_chrdev(ISDN_MAJOR, "isdn") != 0) {
		printk(KERN_WARNING "isdn: controldevice busy, remove cancelled\n");
		restore_flags(flags);
	} else {
		isdn_cleanup_devfs();
		del_timer(&dev->timer);
		restore_flags(flags);
		/* call vfree with interrupts enabled, else it will hang */
		vfree(dev);
		printk(KERN_NOTICE "ISDN-subsystem unloaded\n");
	}
}
#endif
