/* $Id: isdn_tty.h,v 1.15 1999/07/31 12:59:48 armin Exp $

 * header for Linux ISDN subsystem, tty related functions (linklevel).
 *
 * Copyright 1994-1999  by Fritz Elfert (fritz@isdn4linux.de)
 * Copyright 1995,96    by Thinking Objects Software GmbH Wuerzburg
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
 * $Log: isdn_tty.h,v $
 * Revision 1.15  1999/07/31 12:59:48  armin
 * Added tty fax capabilities.
 *
 * Revision 1.14  1999/07/11 17:14:15  armin
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
 * Revision 1.13  1999/04/12 12:33:46  fritz
 * Changes from 2.0 tree.
 *
 * Revision 1.12  1999/03/02 12:04:51  armin
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
 * Revision 1.11  1998/03/19 13:18:27  keil
 * Start of a CAPI like interface for supplementary Service
 * first service: SUSPEND
 *
 * Revision 1.10  1997/03/02 14:29:26  fritz
 * More ttyI related cleanup.
 *
 * Revision 1.9  1997/02/28 02:32:49  fritz
 * Cleanup: Moved some tty related stuff from isdn_common.c
 *          to isdn_tty.c
 * Bugfix:  Bisync protocol did not behave like documented.
 *
 * Revision 1.8  1997/02/10 20:12:50  fritz
 * Changed interface for reporting incoming calls.
 *
 * Revision 1.7  1997/02/03 23:06:10  fritz
 * Reformatted according CodingStyle
 *
 * Revision 1.6  1997/01/14 01:35:19  fritz
 * Changed prototype of isdn_tty_modem_hup.
 *
 * Revision 1.5  1996/05/17 03:52:31  fritz
 * Changed DLE handling for audio receive.
 *
 * Revision 1.4  1996/05/11 21:52:34  fritz
 * Changed queue management to use sk_buffs.
 *
 * Revision 1.3  1996/05/07 09:16:34  fritz
 * Changed isdn_try_read parameter.
 *
 * Revision 1.2  1996/04/30 21:05:27  fritz
 * Test commit
 *
 * Revision 1.1  1996/01/10 21:39:22  fritz
 * Initial revision
 *
 */


#define DLE 0x10
#define ETX 0x03
#define DC4 0x14


/*
 * Definition of some special Registers of AT-Emulator
 */
#define REG_RINGATA   0
#define REG_RINGCNT   1
#define REG_ESC       2
#define REG_CR        3
#define REG_LF        4
#define REG_BS        5

#define REG_WAITC     7

#define REG_RESP     12
#define BIT_RESP      1
#define REG_RESPNUM  12
#define BIT_RESPNUM   2
#define REG_ECHO     12
#define BIT_ECHO      4
#define REG_DCD      12
#define BIT_DCD       8
#define REG_CTS      12
#define BIT_CTS      16
#define REG_DTRR     12
#define BIT_DTRR     32
#define REG_DSR      12
#define BIT_DSR      64
#define REG_CPPP     12
#define BIT_CPPP    128

#define REG_T70      13
#define BIT_T70       2
#define BIT_T70_EXT  32
#define REG_DTRHUP   13
#define BIT_DTRHUP    4
#define REG_RESPXT   13
#define BIT_RESPXT    8
#define REG_CIDONCE  13
#define BIT_CIDONCE  16
#define REG_RUNG     13
#define BIT_RUNG     64
#define REG_DISPLAY  13
#define BIT_DISPLAY 128

#define REG_L2PROT   14
#define REG_L3PROT   15
#define REG_PSIZE    16
#define REG_WSIZE    17
#define REG_SI1      18
#define REG_SI2      19
#define REG_SI1I     20
#define REG_PLAN     21
#define REG_SCREEN   22

#define REG_CPN      23
#define BIT_CPN       1

extern void isdn_tty_modem_escape(void);
extern void isdn_tty_modem_ring(void);
extern void isdn_tty_carrier_timeout(void);
extern void isdn_tty_modem_xmit(void);
extern int isdn_tty_modem_init(void);
extern void isdn_tty_readmodem(void);
extern int isdn_tty_find_icall(int, int, setup_parm);
extern void isdn_tty_cleanup_xmit(modem_info *);
extern int isdn_tty_stat_callback(int, isdn_ctrl *);
extern int isdn_tty_rcv_skb(int, int, int, struct sk_buff *);
extern int isdn_tty_capi_facility(capi_msg *cm); 
extern void isdn_tty_at_cout(char *, modem_info *);
extern void isdn_tty_modem_hup(modem_info *, int);
#ifdef CONFIG_ISDN_TTY_FAX
extern int isdn_tty_cmd_PLUSF_FAX(char **, modem_info *);
extern int isdn_tty_fax_command(modem_info *);
extern void isdn_tty_fax_bitorder(modem_info *, struct sk_buff *);
#endif
