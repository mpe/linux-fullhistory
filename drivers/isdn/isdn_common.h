/* $Id: isdn_common.h,v 1.17 1999/10/27 21:21:17 detabc Exp $

 * header for Linux ISDN subsystem, common used functions and debugging-switches (linklevel).
 *
 * Copyright 1994-1999  by Fritz Elfert (fritz@isdn4linux.de)
 * Copyright 1995,96    by Thinking Objects Software GmbH Wuerzburg
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
 * $Log: isdn_common.h,v $
 * Revision 1.17  1999/10/27 21:21:17  detabc
 * Added support for building logically-bind-group's per interface.
 * usefull for outgoing call's with more then one isdn-card.
 *
 * Switchable support to dont reset the hangup-timeout for
 * receive frames. Most part's of the timru-rules for receiving frames
 * are now obsolete. If the input- or forwarding-firewall deny
 * the frame, the line will be not hold open.
 *
 * Revision 1.16  1999/07/01 08:29:54  keil
 * compatibility to 2.3 kernel
 *
 * Revision 1.15  1999/04/18 14:06:50  fritz
 * Removed TIMRU stuff.
 *
 * Revision 1.14  1999/04/12 12:33:18  fritz
 * Changes from 2.0 tree.
 *
 * Revision 1.13  1999/03/02 12:04:47  armin
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
 * Revision 1.12  1998/06/26 15:12:27  fritz
 * Added handling of STAT_ICALL with incomplete CPN.
 * Added AT&L for ttyI emulator.
 * Added more locking stuff in tty_write.
 *
 * Revision 1.11  1998/04/14 16:28:47  he
 * Fixed user space access with interrupts off and remaining
 * copy_{to,from}_user() -> -EFAULT return codes
 *
 * Revision 1.10  1998/03/07 18:21:03  cal
 * Dynamic Timeout-Rule-Handling vs. 971110 included
 *
 * Revision 1.9  1998/02/20 17:19:01  fritz
 * Added common stub for sending commands to lowlevel.
 *
 * Revision 1.8  1997/10/09 21:28:49  fritz
 * New HL<->LL interface:
 *   New BSENT callback with nr. of bytes included.
 *   Sending without ACK.
 *   New L1 error status (not yet in use).
 *   Cleaned up obsolete structures.
 * Implemented Cisco-SLARP.
 * Changed local net-interface data to be dynamically allocated.
 * Removed old 2.0 compatibility stuff.
 *
 * Revision 1.7  1997/10/01 09:20:30  fritz
 * Removed old compatibility stuff for 2.0.X kernels.
 * From now on, this code is for 2.1.X ONLY!
 * Old stuff is still in the separate branch.
 *
 * Revision 1.6  1997/02/28 02:32:44  fritz
 * Cleanup: Moved some tty related stuff from isdn_common.c
 *          to isdn_tty.c
 * Bugfix:  Bisync protocol did not behave like documented.
 *
 * Revision 1.5  1997/02/10 10:05:45  fritz
 * More changes for Kernel 2.1.X
 * Symbol information moved to isdn_syms.c
 *
 * Revision 1.4  1997/02/03 22:56:50  fritz
 * Removed isdn_writebuf_stub prototype.
 *
 * Revision 1.3  1996/05/19 00:13:05  fritz
 * Removed debug flag.
 *
 * Revision 1.2  1996/04/20 16:20:40  fritz
 * Misc. typos.
 *
 * Revision 1.1  1996/01/10 21:37:19  fritz
 * Initial revision
 *
 */

#undef  ISDN_DEBUG_MODEM_OPEN
#undef  ISDN_DEBUG_MODEM_IOCTL
#undef  ISDN_DEBUG_MODEM_WAITSENT
#undef  ISDN_DEBUG_MODEM_HUP
#undef  ISDN_DEBUG_MODEM_ICALL
#undef  ISDN_DEBUG_MODEM_DUMP
#undef  ISDN_DEBUG_MODEM_VOICE
#undef  ISDN_DEBUG_AT
#undef  ISDN_DEBUG_NET_DUMP
#undef  ISDN_DEBUG_NET_DIAL
#undef  ISDN_DEBUG_NET_ICALL

/* Prototypes */
extern void isdn_MOD_INC_USE_COUNT(void);
extern void isdn_MOD_DEC_USE_COUNT(void);
extern void isdn_free_channel(int di, int ch, int usage);
extern void isdn_all_eaz(int di, int ch);
extern int isdn_command(isdn_ctrl *);
extern int isdn_dc2minor(int di, int ch);
extern void isdn_info_update(void);
extern char *isdn_map_eaz2msn(char *msn, int di);
extern void isdn_timer_ctrl(int tf, int onoff);
extern void isdn_unexclusive_channel(int di, int ch);
extern int isdn_getnum(char **);
extern int isdn_readbchan(int, int, u_char *, u_char *, int, wait_queue_head_t *);
extern int isdn_get_free_channel(int, int, int, int, int);
extern int isdn_writebuf_skb_stub(int, int, int, struct sk_buff *);
extern int register_isdn(isdn_if * i);
extern int isdn_wildmat(char *, char *);
extern int isdn_add_channels(driver *, int, int, int);
#if defined(ISDN_DEBUG_NET_DUMP) || defined(ISDN_DEBUG_MODEM_DUMP)
extern void isdn_dumppkt(char *, u_char *, int, int);
#endif
