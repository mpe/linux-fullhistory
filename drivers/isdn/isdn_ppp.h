/* $Id: isdn_ppp.h,v 1.12 1998/01/31 22:07:48 keil Exp $

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
 * Revision 1.12  1998/01/31 22:07:48  keil
 * changes for newer kernels
 *
 * Revision 1.11  1997/10/01 09:20:44  fritz
 * Removed old compatibility stuff for 2.0.X kernels.
 * From now on, this code is for 2.1.X ONLY!
 * Old stuff is still in the separate branch.
 *
 * Revision 1.10  1997/06/17 13:06:00  hipp
 * Applied Eric's underflow-patches (slightly modified)
 * more compression changes (but disabled at the moment)
 * changed one copy_to_user() to run with enabled IRQs
 * a few MP changes
 * changed 'proto' handling in the isdn_ppp receive code
 *
 * Revision 1.9  1997/02/11 18:32:59  fritz
 * Bugfix in isdn_ppp_free_mpqueue().
 *
 * Revision 1.8  1997/02/10 10:11:33  fritz
 * More changes for Kernel 2.1.X compatibility.
 *
 * Revision 1.7  1997/02/03 23:18:57  fritz
 * Removed isdn_ppp_free_sqqueue prototype
 *         and ippp_table (both static in isdn_ppp.c).
 *
 * Revision 1.6  1996/09/23 01:58:11  fritz
 * Fix: With syncPPP encapsulation, discard LCP packets
 *      when calculating hangup timeout.
 *
 * Revision 1.5  1996/09/07 12:51:34  hipp
 * *** empty log message ***
 *
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

#include <linux/ppp_defs.h>     /* for PPP_PROTOCOL */
extern void isdn_ppp_timer_timeout(void);
extern int isdn_ppp_read(int, struct file *, char *, int);
extern int isdn_ppp_write(int, struct file *, const char *, int);
extern int isdn_ppp_open(int, struct file *);
extern int isdn_ppp_init(void);
extern void isdn_ppp_cleanup(void);
extern int isdn_ppp_free(isdn_net_local *);
extern int isdn_ppp_bind(isdn_net_local *);
extern int isdn_ppp_xmit(struct sk_buff *, struct device *);
extern void isdn_ppp_receive(isdn_net_dev *, isdn_net_local *, struct sk_buff *);
extern int isdn_ppp_dev_ioctl(struct device *, struct ifreq *, int);
extern unsigned int isdn_ppp_poll(struct file *, struct poll_table_struct *);
extern int isdn_ppp_ioctl(int, struct file *, unsigned int, unsigned long);
extern void isdn_ppp_release(int, struct file *);
extern int isdn_ppp_dial_slave(char *);
extern void isdn_ppp_wakeup_daemon(isdn_net_local *);

#define IPPP_OPEN	0x01
#define IPPP_CONNECT	0x02
#define IPPP_CLOSEWAIT	0x04
#define IPPP_NOBLOCK	0x08
#define IPPP_ASSIGNED	0x10

#define IPPP_MAX_HEADER 10


