/* $Id: psr.h,v 1.5 1995/11/25 02:32:31 davem Exp $
 * psr.h: This file holds the macros for masking off various parts of
 *        the processor status register on the Sparc. This is valid
 *        for Version 8. On the V9 this is renamed to the PSTATE
 *        register and its members are accessed as fields like
 *        PSTATE.PRIV for the current CPU privilege level.
 *
 * Copyright (C) 1994 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __LINUX_SPARC_PSR_H
#define __LINUX_SPARC_PSR_H

#define __LINUX_SPARC_V8  /* duh */

#ifdef __LINUX_SPARC_V8

/* The Sparc PSR fields are laid out as the following:
 *
 *  ------------------------------------------------------------------------
 *  | impl  | vers  | icc   | resv  | EC | EF | PIL  | S | PS | ET |  CWP  |
 *  | 31-28 | 27-24 | 23-20 | 19-14 | 13 | 12 | 11-8 | 7 | 6  | 5  |  4-0  |
 *  ------------------------------------------------------------------------
 */

#define PSR_CWP     0x0000001f         /* current window pointer     */
#define PSR_ET      0x00000020         /* enable traps field         */
#define PSR_PS      0x00000040         /* previous privilege level   */
#define PSR_S       0x00000080         /* current privilege level    */
#define PSR_PIL     0x00000f00         /* processor interrupt level  */
#define PSR_EF      0x00001000         /* enable floating point      */
#define PSR_EC      0x00002000         /* enable co-processor        */
#define PSR_ICC     0x00f00000         /* integer condition codes    */
#define PSR_C       0x00100000         /* carry bit                  */
#define PSR_V       0x00200000         /* overflow bit               */
#define PSR_Z       0x00400000         /* zero bit                   */
#define PSR_N       0x00800000         /* negative bit               */
#define PSR_VERS    0x0f000000         /* cpu-version field          */
#define PSR_IMPL    0xf0000000         /* cpu-implementation field   */

#ifndef __ASSEMBLY__
/* Get the %psr register. */
extern inline unsigned int get_psr(void)
{
	unsigned int psr;
	__asm__ __volatile__("rd %%psr, %0\n\t" :
			     "=r" (psr));
	return psr;
}

extern inline void put_psr(unsigned int new_psr)
{
  __asm__("wr %0, 0x0, %%psr\n\t" : :
	  "r" (new_psr));
}

/* Get the %fsr register.  Be careful, make sure the floating point
 * enable bit is set in the %psr when you execute this or you will
 * incur a trap.
 */

extern unsigned int fsr_storage;

extern inline unsigned int get_fsr(void)
{
	unsigned int fsr = 0;

	__asm__ __volatile__("st %%fsr, %1\n\t"
			     "ld %1, %0\n\t" :
			     "=r" (fsr) :
			     "m" (fsr_storage));
	return fsr;
}

#endif /* !(__ASSEMBLY__) */

#endif /* !(__LINUX_SPARC_V8) */

#ifdef __LINUX_SPARC_V9

/* The information available in the %psr on the V8 is spread amongst
 * a whole bunch of registers on the V9. The main one being PSTATE.
 *
 *   --------------------------------------------------------
 *   |  CLE  | TLE |  MM  | RED | PEF | AM | PRIV | IE | AG |
 *   |   9   |  8  |  7-6 |  5  |  4  |  3 |   2  |  1 |  0 |
 *   --------------------------------------------------------
 *
 * Writes and reads to PSTATE are done via 'wrpr' and 'rdpr' instructions.
 *
 * For example:  wrpr %o2, or'd_bit_pattern, %pstate
 *               rdpr %pstate, %o3
 */

#define PSTATE_AG    0x001   /* Alternate Globals             */
#define PSTATE_IE    0x002   /* Interrupt Enable              */
#define PSTATE_PRIV  0x004   /* Current privilege level       */
#define PSTATE_AM    0x008   /* Address mask (data reads can  */
			     /* be chosen to be either big or */
			     /* little endian on V9).         */
#define PSTATE_PEF   0x010   /* enable floating point         */
#define PSTATE_RED   0x020   /* RED trap state (set if trap   */
                             /* trap_level == max_tl).        */
#define PSTATE_MM    0x0c0   /* Memory model (Total Store     */
                             /* Order=0, Partial Store Order  */
                             /* =1 or Relaxed Memory Order=2) */
#define PSTATE_TLE   0x100   /* Trap Little Endian            */
#define PSTATE_CLE   0x200   /* Current Little Endian         */


extern inline unsigned int get_v9_pstate(void)
{
	unsigned int pstate;
	__asm__ __volatile__("rdpr %pstate, %0\n\t" :
			     "=r" (pstate));
	return pstate;
}

extern inline void put_v9_pstate(unsigned int pstate)
{
	__asm__ __volatile__("wrpr %0, 0x0, %pstate\n\t" : :
			     "r" (pstate));
	return;
}

/* The Version Register holds vendor information for the chip:
 *
 *  ---------------------------------------------------------------------------
 *  | manufacturer | implementation | mask | reserved | maxtl | resv | maxwin |
 *  |  63-48       |   47-32        | 31-24|   23-16  | 15-8  | 7-5  |  4-0   |
 *  ---------------------------------------------------------------------------
 *
 */

#define VERS_MAXWIN  0x000000000000001f     /* 'nwindows' on this chip       */
#define VERS_MAXTL   0x00000000000ff000     /* Maximum Trap-level supported  */
#define VERS_MASK    0x0000000ff0000000     /* impl. dep. chip mask revision */
#define VERS_MANUF   0xffff000000000000     /* Manufacturer ID code          */

extern inline unsigned int get_v9_version(void)
{
	unsigned int vers;
	__asm__ __volatile__("rdpr %ver, %0\n\t" :
			     "=r" (vers));
	return vers;
}

extern inline unsigned int get_v9_tstate(void)
{
	unsigned int tstate;
	__asm__ __volatile__("rdpr %tstate, %0\n\t" :
			     "=r" (pstate));
	return tstate;
}

extern inline unsigned int get_v9_pil(void)
{
	unsigned int pil;
	__asm__ __volatile__("rdpr %pil, %0\n\t" :
			     "=r" (pstate));
	return pil;
}

extern inline void put_v9_pil(unsigned int pil)
{
	__asm__ __volatile__("wrpr %0, 0x0, %pil\n\t" : :
			     "r" (pil));
	return;
}


#endif /* !(__LINUX_SPARC_V9) */

#endif /* !(__LINUX_SPARC_PSR_H) */
