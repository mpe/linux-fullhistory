/* ross.h: Ross module specific definitions and defines.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC_ROSS_H
#define _SPARC_ROSS_H

/* Ross made Hypersparcs have a %psr 'impl' field of '0001'.  The 'vers'
 * field has '1111'.
 */

/* The MMU control register fields on the HyperSparc.
 *
 * -----------------------------------------------------------------
 * |implvers| RSV |CWR|SE|WBE| MID |BM| C|CS|MR|CM|RSV|CE|RSV|NF|ME|
 * -----------------------------------------------------------------
 *  31    24 23-22 21  20  19 18-15 14 13 12 11 10  9   8 7-2  1  0
 *
 * Phew, lots of fields there ;-)
 *
 * CWR: Cache Wrapping Enabled, if one cache wrapping is on.
 * SE: Snoop Enable, turns on bus snooping for cache activity if one.
 * WBE: Write Buffer Enable, one turns it on.
 * MID: The ModuleID of the chip for MBus transactions.
 * BM: Boot-Mode. One indicates the MMU is in boot mode.
 * C: Indicates whether accesses are cachable while the MMU is
 *    disabled.
 * CS: Cache Size -- 0 = 128k, 1 = 256k
 * MR: Memory Reflection, one indicates that the memory bus connected
 *     to the MBus supports memory reflection.
 * CM: Cache Mode -- 0 = write-through, 1 = copy-back
 * CE: Cache Enable -- 0 = no caching, 1 = cache is on
 * NF: No Fault -- 0 = faults trap the CPU from supervisor mode
 *                 1 = faults from supervisor mode do not generate traps
 * ME: MMU Enable -- 0 = MMU is off, 1 = MMU is on
 */

#define HYPERSPARC_CWENABLE   0x00200000
#define HYPERSPARC_SBENABLE   0x00100000
#define HYPERSPARC_WBENABLE   0x00080000
#define HYPERSPARC_MIDMASK    0x00078000
#define HYPERSPARC_BMODE      0x00004000
#define HYPERSPARC_ACENABLE   0x00002000
#define HYPERSPARC_CSIZE      0x00001000
#define HYPERSPARC_MRFLCT     0x00000800
#define HYPERSPARC_CMODE      0x00000400
#define HYPERSPARC_CENABLE    0x00000100
#define HYPERSPARC_NFAULT     0x00000002
#define HYPERSPARC_MENABLE    0x00000001

/* Flushes which clear out only the on-chip Ross HyperSparc ICACHE. */
extern inline void flush_i_page(unsigned int addr)
{
	__asm__ __volatile__("sta %%g0, [%0] %1\n\t" : :
			     "r" (addr), "i" (ASI_M_IFLUSH_PAGE) :
			     "memory");
	return;
}

extern inline void flush_i_seg(unsigned int addr)
{
	__asm__ __volatile__("sta %%g0, [%0] %1\n\t" : :
			     "r" (addr), "i" (ASI_M_IFLUSH_SEG) :
			     "memory");
	return;
}

extern inline void flush_i_region(unsigned int addr)
{
	__asm__ __volatile__("sta %%g0, [%0] %1\n\t" : :
			     "r" (addr), "i" (ASI_M_IFLUSH_REGION) :
			     "memory");
	return;
}

extern inline void flush_i_ctx(unsigned int addr)
{
	__asm__ __volatile__("sta %%g0, [%0] %1\n\t" : :
			     "r" (addr), "i" (ASI_M_IFLUSH_CTX) :
			     "memory");
	return;
}

extern inline void flush_i_user(unsigned int addr)
{
	__asm__ __volatile__("sta %%g0, [%0] %1\n\t" : :
			     "r" (addr), "i" (ASI_M_IFLUSH_USER) :
			     "memory");
	return;
}

/* Finally, flush the entire ICACHE. */
extern inline void flush_whole_icache(void)
{
	__asm__ __volatile__("sta %%g0, [%%g0] %0\n\t" : :
			     "i" (ASI_M_FLUSH_IWHOLE));
	return;
}


/* The ICCR instruction cache register on the HyperSparc.
 *
 * -----------------------------------------------
 * |                                 | FTD | IDC |
 * -----------------------------------------------
 *  31                                  1     0
 *
 * This register is accessed using the V8 'wrasr' and 'rdasr'
 * opcodes, since not all assemblers understand them and those
 * that do use different semantics I will just hard code the
 * instruction with a '.word' statement.
 *
 * FTD:  If set to one flush instructions executed during an
 *       instruction cache hit occurs, the corresponding line
 *       for said cache-hit is invalidated.  If FTD is zero,
 *       an unimplemented 'flush' trap will occur when any
 *       flush is executed by the processor.
 *
 * ICE:  If set to one, the instruction cache is enabled.  If
 *       zero, the cache will not be used for instruction fetches.
 *
 * All other bits are read as zeros, and writes to them have no
 * effect.
 */

extern inline unsigned int get_ross_icr(void)
{
	unsigned int icreg;

	__asm__ __volatile__(".word 0xbf402000\n\t" : /* rd %iccr, %g1 */
			     "=r" (icreg) : :
			     "g1", "memory");

	return icreg;
}

extern inline void put_ross_icr(unsigned int icreg)
{
	__asm__ __volatile__("or %%g0, %0, %%g1\n\t"
			     ".word 0xbf802000\n\t" /* wr %g1, 0x0, %iccr */
			     "nop\n\t"
			     "nop\n\t"
			     "nop\n\t" : : 
			     "r" (icreg) :
			     "g1", "memory");

	return;
}

#endif /* !(_SPARC_ROSS_H) */
