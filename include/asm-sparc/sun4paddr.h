/* $Id: sun4paddr.h,v 1.2 1998/02/09 13:26:41 jj Exp $
 * sun4paddr.h:  Various physical addresses on sun4 machines
 *
 * Copyright (C) 1997 Anton Blanchard (anton@progsoc.uts.edu.au)
 */

#ifndef _SPARC_SUN4PADDR_H
#define _SPARC_SUN4PADDR_H

#define SUN4_MEMREG_PHYSADDR		0xf4000000
#define SUN4_IE_PHYSADDR		0xf5000000
#define SUN4_300_MOSTEK_PHYSADDR	0xf2000000
#define SUN4_300_TIMER_PHYSADDR		0xef000000
#define SUN4_300_ETH_PHYSADDR		0xf9000000
#define SUN4_300_BWTWO_PHYSADDR		0xfb400000
#define SUN4_300_DMA_PHYSADDR		0xfa001000
#define SUN4_300_ESP_PHYSADDR		0xfa000000

#endif /* !(_SPARC_SUN4PADDR_H) */
