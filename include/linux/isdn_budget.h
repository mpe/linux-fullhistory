/* isdn_budget.h
 *
 * Linux ISDN subsystem, budget-accounting for network interfaces.
 *
 * Copyright 1997       by Christian Lademann <cal@zls.de>
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
 */

/*
30.06.97:cal:angelegt
04.11.97:cal:budget.period: int --> time_t
*/

#ifndef __isdn_budget_h__
#define __isdn_budget_h__

#include	<linux/types.h>

#define	ISDN_BUDGET_DIAL		0
#define	ISDN_BUDGET_CHARGE		1
#define	ISDN_BUDGET_ONLINE		2
#define	ISDN_BUDGET_NUM_BUDGET		3

#define	ISDN_BUDGET_INIT		0
#define	ISDN_BUDGET_CHECK_DIAL		1
#define	ISDN_BUDGET_CHECK_CHARGE	2
#define	ISDN_BUDGET_CHECK_ONLINE	3
#define	ISDN_BUDGET_START_ONLINE	10

#define	ISDN_BUDGET_SET_BUDGET	0
#define	ISDN_BUDGET_GET_BUDGET	1

typedef struct {
	char	name [9];		/* Interface */
	int	command,		/* subcommand */
		budget,			/* budget-nr. */
		amount,			/* set/get budget-amount */
		used;			/* set/get used amount */
	time_t	period,			/* set/get length of period */
		period_started;		/* set/get startpoint of period */
}	isdn_ioctl_budget;

#ifdef __KERNEL__
extern int	isdn_net_budget(int, struct device *);
extern int	isdn_budget_ioctl(isdn_ioctl_budget *);
#endif /* __KERNEL__ */

#endif /* __isdn_budget_h__ */
