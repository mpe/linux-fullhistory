/* $Id: isdn_ppp.h,v 1.4 1996/05/06 11:34:56 hipp Exp $
 *
 * header for Linux ISDN subsystem, functions for synchronous PPP (linklevel).
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
 * $Log: isdn_ppp.h,v $
 * Revision 1.4  1996/05/06 11:34:56  hipp
 * fixed a few bugs
 *
 * Revision 1.3  1996/04/30 09:33:10  fritz
 * Removed compatibility-macros.
 *
 * Revision 1.2  1996/04/20 16:35:11  fritz
 * Changed isdn_ppp_receive to use sk_buff as parameter.
 * Added definition of isdn_ppp_dial_slave and ippp_table.
 *
 * Revision 1.1  1996/01/10 21:39:10  fritz
 * Initial revision
 *
 */

extern void isdn_ppp_timer_timeout(void);
extern int  isdn_ppp_read(int , struct file *, char *, int);
extern int  isdn_ppp_write(int , struct file *, const char *, int);
extern int  isdn_ppp_open(int , struct file *);
extern int  isdn_ppp_init(void);
extern void isdn_ppp_cleanup(void);
extern int  isdn_ppp_free(isdn_net_local *);
extern int  isdn_ppp_bind(isdn_net_local *);
extern int  isdn_ppp_xmit(struct sk_buff *, struct device *);
extern void isdn_ppp_receive(isdn_net_dev *, isdn_net_local *, struct sk_buff *);
extern int  isdn_ppp_dev_ioctl(struct device *, struct ifreq *, int);
extern void isdn_ppp_free_mpqueue(isdn_net_dev *);
extern void isdn_ppp_free_sqqueue(isdn_net_dev *);
extern int  isdn_ppp_select(int, struct file *, int, select_table *);
extern int  isdn_ppp_ioctl(int, struct file *, unsigned int, unsigned long);
extern void isdn_ppp_release(int, struct file *);
extern int  isdn_ppp_dial_slave(char *);

extern struct ippp_struct *ippp_table[ISDN_MAX_CHANNELS];
