/* $Id: viking.h,v 1.6 1996/03/01 07:21:05 davem Exp $
 * viking.h:  Defines specific to the TI Viking MBUS module.
 *            This is SRMMU stuff.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */
#ifndef _SPARC_VIKING_H
#define _SPARC_VIKING_H

#include <asm/mxcc.h>

/* Bits in the SRMMU control register for TI Viking modules.
 *
 * -------------------------------------------------------------
 * |implvers| RSV |DP|RSV|TC|AC|SP|BM|PC|MBM|SB|IC|DC|RSV|NF|ME|
 * -------------------------------------------------------------
 *  31    24 23-20 19  18 17 16 15 14 13  12 11 10  9 8-2  1  0
 *
 * DP: Data Prefetcher Enable -- 0 = DP is off, 1 = DP is on
 * TC: Tablewalk Cacheable -- 0 = Twalks are not cacheable
 *                            1 = Twalks are cacheable
 * AC: Alternate Cacheable -- 0 = Direct physical accesses not cacheable
 *                            1 = Direct physical accesses are cacheable
 * SP: SnooP Enable -- 0 = bus snooping off, 1 = bus snooping on
 * BM: Boot Mode -- 0 = not in boot mode, 1 = in boot mode
 * MBM: MBus Mode -- 0 = not in MBus mode, 1 = in MBus mode
 * SB: StoreBuffer enable -- 0 = store buffer off, 1 = store buffer on
 * IC: Instruction Cache -- 0 = off, 1 = on
 * DC: Data Cache -- 0 = off, 1 = 0n
 * NF: No Fault -- 0 = faults generate traps, 1 = faults don't trap
 * ME: MMU enable -- 0 = mmu not translating, 1 = mmu translating
 *
 */

#define VIKING_DCENABLE     0x00000100   /* Enable data cache */
#define VIKING_ICENABLE     0x00000200   /* Enable instruction cache */
#define VIKING_SBENABLE     0x00000400   /* Enable store buffer */
#define VIKING_MMODE        0x00000800   /* MBUS mode */
#define VIKING_PCENABLE     0x00001000   /* Enable parity checking */

/* Boot mode, 0 at boot-time, 1 after prom initializes the MMU. */
#define VIKING_BMODE        0x00002000   
#define VIKING_SPENABLE     0x00004000   /* Enable bus cache snooping */

/* The deal with this AC bit is that if you are going to modify the
 * contents of physical ram using the MMU bypass, you had better set
 * this bit or things will get unsynchronized.  This is only applicable
 * if an E-cache (ie. a PAC) is around and the Viking is not in MBUS mode.
 */
#define VIKING_ACENABLE     0x00008000   /* Enable alternate caching */
#define VIKING_TCENABLE     0x00010000   /* Enable table-walks to be cached */
#define VIKING_DPENABLE     0x00040000   /* Enable the data prefetcher */

extern inline void viking_flush_icache(void)
{
	__asm__ __volatile__("sta %%g0, [%%g0] %0\n\t" : :
			     "i" (ASI_M_IC_FLCLEAR));
}

extern inline void viking_flush_dcache(void)
{
	__asm__ __volatile__("sta %%g0, [%%g0] %0\n\t" : :
			     "i" (ASI_M_DC_FLCLEAR));
}

/* MXCC stuff... */
extern inline void viking_enable_mxcc(void)
{
}

extern inline void viking_mxcc_scrape(void)
{
	/* David, what did you learn in school today? */


}

#endif /* !(_SPARC_VIKING_H) */
