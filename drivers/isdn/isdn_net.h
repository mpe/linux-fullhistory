/* $Id: isdn_net.h,v 1.2 1996/04/20 16:29:43 fritz Exp $
 *
 * header for Linux ISDN subsystem, network related functions (linklevel).
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
 * $Log: isdn_net.h,v $
 * Revision 1.2  1996/04/20 16:29:43  fritz
 * Misc. typos
 *
 * Revision 1.1  1996/02/11 02:35:13  fritz
 * Initial revision
 *
 */

extern char*         isdn_net_new(char *, struct device *);
extern char*         isdn_net_newslave(char *);
extern int           isdn_net_rm(char *);
extern int           isdn_net_rmall(void);
extern int           isdn_net_stat_callback(int, int);
extern int           isdn_net_receive_callback(int, u_char *, int);
extern int           isdn_net_setcfg(isdn_net_ioctl_cfg *);
extern int           isdn_net_getcfg(isdn_net_ioctl_cfg *);
extern int           isdn_net_addphone(isdn_net_ioctl_phone *);
extern int           isdn_net_getphones(isdn_net_ioctl_phone *, char *);
extern int           isdn_net_delphone(isdn_net_ioctl_phone *);
extern int           isdn_net_find_icall(int, int, int, char *);
extern void          isdn_net_hangup(struct device *);
extern void          isdn_net_dial(void);
extern void          isdn_net_autohup(void);
extern int           isdn_net_force_hangup(char *);
extern int           isdn_net_force_dial(char *);
extern isdn_net_dev* isdn_net_findif(char *);
extern int           isdn_net_send_skb(struct device *, isdn_net_local *, 
				      struct sk_buff *);
extern int           isdn_net_rcv_skb(int, struct sk_buff *); 
