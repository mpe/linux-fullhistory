/* $Id: isdn_cards.c,v 1.7 1998/02/20 17:24:28 fritz Exp $

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
 * Revision 1.7  1998/02/20 17:24:28  fritz
 * Added ACT2000 init.
 *
 * Revision 1.6  1997/04/23 18:56:03  fritz
 * Old Teles driver removed, Changed doc and scripts accordingly.
 *
 * Revision 1.5  1997/03/30 17:10:36  calle
 * added support for AVM-B1-PCI card.
 *
 * Revision 1.4  1997/03/04 21:59:44  calle
 * Added AVM-B1-CAPI2.0 driver
 *
 * Revision 1.3  1997/02/03 23:31:14  fritz
 * Reformatted according CodingStyle
 *
 * Revision 1.2  1996/10/13 19:52:17  keil
 * HiSax support
 *
 * Revision 1.1  1996/04/20 16:04:36  fritz
 * Initial revision
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

#ifdef CONFIG_ISDN_DRV_AVMB1
extern void avmb1_init(void);
extern void capi_init(void);
extern void capidrv_init(void);
#ifdef CONFIG_PCI
extern int b1pci_init(void);
#endif
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
	avmb1_init();
#ifdef CONFIG_PCI
	b1pci_init();
#endif
	capi_init();
	capidrv_init();
#endif
#if CONFIG_ISDN_DRV_ACT2000
	act2000_init();
#endif
}
