/* $Id: isdn_tty.h,v 1.5 1996/05/17 03:52:31 fritz Exp $
 *
 * header for Linux ISDN subsystem, tty related functions (linklevel).
 *
 * Copyright 1994,95,96 by Fritz Elfert (fritz@wuemaus.franken.de)
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

extern void  isdn_tty_modem_result(int, modem_info *);
extern void  isdn_tty_modem_escape(void);
extern void  isdn_tty_modem_ring(void);
extern void  isdn_tty_modem_xmit(void);
extern void  isdn_tty_modem_hup(modem_info *);
extern int   isdn_tty_modem_init(void);
extern void  isdn_tty_readmodem(void);
extern int   isdn_tty_try_read(modem_info *, struct sk_buff *);
extern int   isdn_tty_find_icall(int, int, char *);
extern int   isdn_tty_countDLE(unsigned char *, int);
extern void  isdn_tty_bsent(int, int);
extern void  isdn_tty_cleanup_xmit(modem_info *);
