#ifndef _SPARC_ASI_H
#define _SPARC_ASI_H

/* asi.h:  Address Space Identifier values for the sparc.

   Copyright (C) 1994 David S. Miller (davem@caip.rutgers.edu)
*/

/* These are sun4c, beware on other architectures. Although things should
 * be similar under regular sun4's.
 */

#include <linux/config.h>
#ifdef CONFIG_SUN4M
#include "asi4m.h"
#else

#define ASI_NULL1        0x0
#define ASI_NULL2        0x1

/* sun4c and sun4 control registers and mmu/vac ops */
#define ASI_CONTROL          0x2
#define ASI_SEGMAP           0x3
#define ASI_PTE              0x4
#define ASI_HWFLUSHSEG       0x5      /* These are to initiate hw flushes of the cache */
#define ASI_HWFLUSHPAGE      0x6
#define ASI_HWFLUSHCONTEXT   0x7


#define ASI_USERTXT      0x8
#define ASI_KERNELTXT    0x9
#define ASI_USERDATA     0xa
#define ASI_KERNELDATA   0xb

/* VAC Cache flushing on sun4c and sun4 */

#define ASI_FLUSHSEG     0xc      /* These are for "software" flushes of the cache */
#define ASI_FLUSHPG      0xd
#define ASI_FLUSHCTX     0xe


#endif /* CONFIG_SUN4M */
#endif /* _SPARC_ASI_H */
