/* $Id: isdn_cards.c,v 1.13 2000/10/28 23:03:38 kai Exp $

 * Linux ISDN subsystem, initialization for non-modularized drivers.
 *
 * Copyright 1994,95,96 by Fritz Elfert (fritz@isdn4linux.de)
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

#include <linux/config.h>

#ifdef CONFIG_ISDN_DRV_ICN
extern void icn_init(void);
#endif

#ifdef CONFIG_ISDN_DRV_HISAX
extern void HiSax_init(void);
#endif

#ifdef CONFIG_ISDN_DRV_PCBIT
extern void pcbit_init(void);
#endif

#ifdef CONFIG_ISDN_DRV_EICON
extern void eicon_init(void);
#endif

#ifdef CONFIG_ISDN_DRV_AVMB1
extern void kcapi_init(void);
extern void capi_init(void);
extern void capidrv_init(void);
#endif
#if CONFIG_ISDN_DRV_ACT2000
extern void act2000_init(void);
#endif

void
isdn_cards_init(void)
{
#if CONFIG_ISDN_DRV_ICN
	icn_init();
#endif
#ifdef CONFIG_ISDN_DRV_HISAX
	HiSax_init();
#endif
#if CONFIG_ISDN_DRV_PCBIT
	pcbit_init();
#endif
#ifdef CONFIG_ISDN_DRV_AVMB1
	kcapi_init();
	capi_init();
	capidrv_init();
#endif
#if CONFIG_ISDN_DRV_ACT2000
	act2000_init();
#endif
#if CONFIG_ISDN_DRV_EICON
	eicon_init();
#endif
}
