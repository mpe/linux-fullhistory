/* $Id: isdn_cards.c,v 1.1 1996/04/20 16:04:36 fritz Exp $
 *
 * Linux ISDN subsystem, initialization for non-modularized drivers.
 *
 * Copyright 1994,95,96 by Fritz Elfert (fritz@wuemaus.franken.de)
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
 * $Log: isdn_cards.c,v $
 * Revision 1.1  1996/04/20 16:04:36  fritz
 * Initial revision
 *
 */

#include <linux/config.h>

#ifdef CONFIG_ISDN_DRV_ICN
extern void icn_init(void);
#endif

#ifdef CONFIG_ISDN_DRV_TELES
extern void teles_init(void);
#endif

#ifdef CONFIG_ISDN_DRV_PCBIT
extern void pcbit_init(void);
#endif

void isdn_cards_init(void)
{
#if CONFIG_ISDN_DRV_ICN
        icn_init();
#endif
#if CONFIG_ISDN_DRV_TELES
        teles_init();
#endif
#if CONFIG_ISDN_DRV_PCBIT
        pcbit_init();
#endif
}

