/* $Id: isdn_common.h,v 1.3 1996/05/19 00:13:05 fritz Exp $
 *
 * header for Linux ISDN subsystem, common used functions and debugging-switches (linklevel).
 *
 * Copyright 1994,95,96 by Fritz Elfert (fritz@wuemaus.franken.de)
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
#undef  ISDN_DEBUG_AT
#undef  ISDN_DEBUG_NET_DUMP
#undef  ISDN_DEBUG_NET_DIAL
#undef  ISDN_DEBUG_NET_ICALL

/* Prototypes */
extern void  isdn_MOD_INC_USE_COUNT(void);
extern void  isdn_MOD_DEC_USE_COUNT(void);
extern void  isdn_free_channel(int di, int ch, int usage);
extern void  isdn_all_eaz(int di, int ch);
extern int   isdn_dc2minor(int di, int ch);
extern void  isdn_info_update(void);
extern char* isdn_map_eaz2msn(char *msn, int di);
extern void  isdn_timer_ctrl(int tf, int onoff);
extern void  isdn_unexclusive_channel(int di, int ch);
extern int   isdn_getnum(char **);
extern int   isdn_readbchan (int, int, u_char *, u_char *, int, int);
extern int   isdn_get_free_channel(int, int, int, int, int);
extern int   isdn_writebuf_stub(int, int, const u_char *, int, int);
extern int   isdn_writebuf_skb_stub(int, int, struct sk_buff *);
#if defined(ISDN_DEBUG_NET_DUMP) || defined(ISDN_DEBUG_MODEM_DUMP)
extern void  isdn_dumppkt(char *, u_char *, int, int);
#endif
