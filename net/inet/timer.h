/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the TIMER module.
 *
 * Version:	@(#)timer.h	1.0.2	05/23/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _TIMER_H
#define _TIMER_H


#define SEQ_TICK	3
#define timer_seq	jiffies


struct timer {
  unsigned long	len;
  struct sock *sk;
  unsigned long	when;
  int running;
  struct timer *next;
};


extern unsigned long seq_offset;


extern void	delete_timer(struct timer *);
extern void	reset_timer(struct timer *);
extern void	net_timer(void);


#endif	/* _TIMER_H */
