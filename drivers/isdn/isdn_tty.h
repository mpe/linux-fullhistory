/* $Id: isdn_tty.h,v 1.1 1996/01/10 21:39:22 fritz Exp fritz $
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
extern int   isdn_tty_try_read(int, u_char *, int);
extern int   isdn_tty_find_icall(int, int, char *);
#if FUTURE
extern void  isdn_tty_bsent(int, int);
#endif
