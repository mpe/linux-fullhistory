/* $Id: psrcompat.h,v 1.3 1997/06/05 06:22:54 davem Exp $ */
#ifndef _SPARC64_PSRCOMPAT_H
#define _SPARC64_PSRCOMPAT_H

#include <asm/pstate.h>

/* Old 32-bit PSR fields for the compatability conversion code. */
#define PSR_CWP     0x0000001f         /* current window pointer     */
#define PSR_ET      0x00000020         /* enable traps field         */
#define PSR_PS      0x00000040         /* previous privilege level   */
#define PSR_S       0x00000080         /* current privilege level    */
#define PSR_PIL     0x00000f00         /* processor interrupt level  */
#define PSR_EF      0x00001000         /* enable floating point      */
#define PSR_EC      0x00002000         /* enable co-processor        */
#define PSR_LE      0x00008000         /* SuperSparcII little-endian */
#define PSR_ICC     0x00f00000         /* integer condition codes    */
#define PSR_C       0x00100000         /* carry bit                  */
#define PSR_V       0x00200000         /* overflow bit               */
#define PSR_Z       0x00400000         /* zero bit                   */
#define PSR_N       0x00800000         /* negative bit               */
#define PSR_VERS    0x0f000000         /* cpu-version field          */
#define PSR_IMPL    0xf0000000         /* cpu-implementation field   */

extern inline unsigned int tstate_to_psr(unsigned long tstate)
{
	unsigned int psr;
	unsigned long vers;

	/* These fields are in the same place. */
	psr  = (tstate & (TSTATE_CWP | TSTATE_PEF));

	/* This is what the user would have always seen. */
	psr |= PSR_S;

	/* Slam in the 32-bit condition codes. */
	psr |= ((tstate & TSTATE_ICC) >> 12);

	/* This is completely arbitrary. */
	__asm__ __volatile__("rdpr	%%ver, %0" : "=r" (vers));
	psr |= ((vers << 8) >> 32) & PSR_IMPL;
	psr |= ((vers << 24) >> 36) & PSR_VERS;

	return psr;
}

extern inline unsigned long psr_to_tstate_icc(unsigned int psr)
{
	unsigned long tstate;

	tstate = ((unsigned long)(psr & PSR_ICC)) << 12;

	return tstate;
}

#endif /* !(_SPARC64_PSRCOMPAT_H) */
