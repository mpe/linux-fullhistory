/* $Id: isdn_budget.c,v 1.3 1998/10/23 10:18:39 paul Exp $
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
 * $Log: isdn_budget.c,v $
 * Revision 1.3  1998/10/23 10:18:39  paul
 * Implementation of "dialmode" (successor of "status")
 * You also need current isdnctrl for this!
 *
 * Revision 1.2  1998/03/07 23:17:30  fritz
 * Added RCS keywords
 * Bugfix: Did not compile without isdn_dumppkt beeing enabled.
 *
 */

/*
30.06.97:cal:angelegt
04.11.97:cal:budget.period: int --> time_t
*/

#include <linux/config.h>
#define __NO_VERSION__
#include <linux/module.h>
#include <linux/isdn.h>
#include "isdn_common.h"
#include "isdn_net.h"

#ifdef CONFIG_ISDN_BUDGET

#define	VERBOSE_PRINTK(v, l, p...)	{ \
	if(dev->net_verbose >= (v)) { \
		printk(l ## p); \
	} else { ; } \
}


int
isdn_net_budget(int type, struct device *ndev) {
	isdn_net_local		*lp = (isdn_net_local *)ndev->priv;
	int			i, ret = 0;

	switch(type) {
	case ISDN_BUDGET_INIT:
		for(i = 0; i < ISDN_BUDGET_NUM_BUDGET; i++) {
			lp->budget [i] .amount = -1;
			lp->budget [i] .used = 0;
			lp->budget [i] .period = (time_t)0;
			lp->budget [i] .period_started = (time_t)0;
			lp->budget [i] .last_check = CURRENT_TIME;
			lp->budget [i] .notified = 0;
		}

		return(0);
		break;

	case ISDN_BUDGET_CHECK_DIAL:
	case ISDN_BUDGET_CHECK_CHARGE:
	case ISDN_BUDGET_CHECK_ONLINE:
		ret = 0;

		for(i = 0; i < ISDN_BUDGET_NUM_BUDGET; i++) {
			if(lp->budget [i] .amount < 0)
				continue;

			if(lp->budget [i] .period_started + lp->budget [i] .period < CURRENT_TIME) {
				lp->budget [i] .used = 0;
				lp->budget [i] .period_started = CURRENT_TIME;
				lp->budget [i] .notified = 0;
			}

			if(lp->budget [i] .used >= lp->budget [i] .amount)
				ret |= (1 << i);
		}

		switch(type) {
		case ISDN_BUDGET_CHECK_DIAL:
			if(! ret) {
				lp->budget [ISDN_BUDGET_DIAL] .used++;
				lp->budget [ISDN_BUDGET_DIAL] .last_check = CURRENT_TIME;
			}
			break;

		case ISDN_BUDGET_CHECK_CHARGE:
			lp->budget [ISDN_BUDGET_CHARGE] .used++;
			lp->budget [ISDN_BUDGET_CHARGE] .last_check = CURRENT_TIME;
			break;

		case ISDN_BUDGET_CHECK_ONLINE:
			if(lp->budget [ISDN_BUDGET_ONLINE] .last_check) {
				lp->budget [ISDN_BUDGET_ONLINE] .used += (CURRENT_TIME - lp->budget [ISDN_BUDGET_ONLINE] .last_check);
			}

			lp->budget [ISDN_BUDGET_ONLINE] .last_check = CURRENT_TIME;
			break;
		}

/*
		if(ret)
			lp->flags |= ISDN_NET_DM_OFF;
*/
		for(i = 0; i < ISDN_BUDGET_NUM_BUDGET; i++) {
			if(ret & (1 << i) && ! lp->budget [i] .notified) {
				switch(i) {
				case ISDN_BUDGET_DIAL:
					printk(KERN_WARNING "isdn_budget: dial budget used up.\n");
					break;

				case ISDN_BUDGET_CHARGE:
					printk(KERN_WARNING "isdn_budget: charge budget used up.\n");
					break;

				case ISDN_BUDGET_ONLINE:
					printk(KERN_WARNING "isdn_budget: online budget used up.\n");
					break;

				default:
					printk(KERN_WARNING "isdn_budget: budget #%d used up.\n", i);
					break;
				}

				lp->budget [i] .notified = 1;
			}
		}

		return(ret);
		break;

	case ISDN_BUDGET_START_ONLINE:
		lp->budget [ISDN_BUDGET_ONLINE] .last_check = CURRENT_TIME;
		return(0);

		break;
	}

	return(-1);
}


int
isdn_budget_ioctl(isdn_ioctl_budget *iocmd) {
	isdn_net_dev		*p = isdn_net_findif(iocmd->name);

	if(p) {
		switch(iocmd->command) {
		case ISDN_BUDGET_SET_BUDGET:
			if(! suser())
				return(-EPERM);

			if(iocmd->budget < 0 || iocmd->budget > ISDN_BUDGET_NUM_BUDGET)
				return(-EINVAL);

			if(iocmd->amount < 0)
				iocmd->amount = -1;

			p->local->budget [iocmd->budget] .amount = iocmd->amount;
			p->local->budget [iocmd->budget] .period = iocmd->period;

			if(iocmd->used <= 0)
				p->local->budget [iocmd->budget] .used = 0;
			else
				p->local->budget [iocmd->budget] .used = iocmd->used;

			if(iocmd->period_started == (time_t)0)
				p->local->budget [iocmd->budget] .period_started = CURRENT_TIME;
			else
				p->local->budget [iocmd->budget] .period_started = iocmd->period_started;

			return(0);
			break;

		case ISDN_BUDGET_GET_BUDGET:
			if(iocmd->budget < 0 || iocmd->budget > ISDN_BUDGET_NUM_BUDGET)
				return(-EINVAL);

			iocmd->amount = p->local->budget [iocmd->budget] .amount;
			iocmd->used = p->local->budget [iocmd->budget] .used;
			iocmd->period = p->local->budget [iocmd->budget] .period;
			iocmd->period_started = p->local->budget [iocmd->budget] .period_started;

			return(0);
			break;

		default:
			return(-EINVAL);
			break;
		}
	}
	return(-ENODEV);
}
#endif
