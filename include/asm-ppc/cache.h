/*
 * include/asm-ppc/cache.h
 */
#ifndef __ARCH_PPC_CACHE_H
#define __ARCH_PPC_CACHE_H

#include <linux/config.h>
#include <asm/processor.h>
/*#include <asm/system.h>*/

/* bytes per L1 cache line */
#define        L1_CACHE_BYTES  32      
#define        L1_CACHE_ALIGN(x)       (((x)+(L1_CACHE_BYTES-1))&~(L1_CACHE_BYTES-1))
#define L1_CACHE_PAGES		8

#if defined(__KERNEL__) && !defined(__ASSEMBLY__)
static inline unsigned long unlock_dcache(void)
{
#ifndef CONFIG_8xx	
	ulong hid0 = 0;
	/* 601 doesn't do this */
	if ( (ulong) _get_PVR() == 1 )
		return 0;
	asm("mfspr %0,1008 \n\t" : "=r" (hid0) );
	if ( !(hid0 & HID0_DLOCK) )
		return 0;
	asm("mtspr 1008,%0 \n\t" :: "r" (hid0 & ~(HID0_DLOCK)));
	return (hid0 & HID0_DLOCK) ? 1 : 0;
#else /* ndef CONFIG_8xx */
	return 0;
#endif
}

static inline void lock_dcache(unsigned long lockit)
{
#ifndef CONFIG_8xx
	/* 601 doesn't do this */
	if ( !lockit || ((ulong) _get_PVR() == 1) )
		return;
	asm("mfspr	%0,1008 \n\t"
	    "ori	%0,%0,%2 \n\t"
	    "mtspr	1008,%0 \n\t"
	    "sync \n\t isync \n\t"
	    : "=r" (lockit) : "0" (lockit), "i" (HID0_DLOCK));
#endif /* ndef CONFIG_8xx */
}

#endif /* __ASSEMBLY__ */

/* prep registers for L2 */
#define CACHECRBA       0x80000823      /* Cache configuration register address */
#define L2CACHE_MASK	0x03	/* Mask for 2 L2 Cache bits */
#define L2CACHE_512KB	0x00	/* 512KB */
#define L2CACHE_256KB	0x01	/* 256KB */
#define L2CACHE_1MB	0x02	/* 1MB */
#define L2CACHE_NONE	0x03	/* NONE */
#define L2CACHE_PARITY  0x08    /* Mask for L2 Cache Parity Protected bit */

#ifdef CONFIG_8xx
/* Cache control on the MPC8xx is provided through some additional
 * special purpose registers.
 */
#define IC_CST		560	/* Instruction cache control/status */
#define IC_ADR		561	/* Address needed for some commands */
#define IC_DAT		562	/* Read-only data register */
#define DC_CST		568	/* Data cache control/status */
#define DC_ADR		569	/* Address needed for some commands */
#define DC_DAT		570	/* Read-only data register */

/* Commands.  Only the first few are available to the instruction cache.
*/
#define	IDC_ENABLE	0x02000000	/* Cache enable */
#define IDC_DISABLE	0x04000000	/* Cache disable */
#define IDC_LDLCK	0x06000000	/* Load and lock */
#define IDC_UNLINE	0x08000000	/* Unlock line */
#define IDC_UNALL	0x0a000000	/* Unlock all */
#define IDC_INVALL	0x0c000000	/* Invalidate all */

#define DC_FLINE	0x0e000000	/* Flush data cache line */
#define DC_SFWT		0x01000000	/* Set forced writethrough mode */
#define DC_CFWT		0x03000000	/* Clear forced writethrough mode */
#define DC_SLES		0x05000000	/* Set little endian swap mode */
#define DC_CLES		0x07000000	/* Clear little endian swap mode */

/* Status.
*/
#define IDC_ENABLED	0x80000000	/* Cache is enabled */
#define IDC_CERR1	0x00200000	/* Cache error 1 */
#define IDC_CERR2	0x00100000	/* Cache error 2 */
#define IDC_CERR3	0x00080000	/* Cache error 3 */

#define DC_DFWT		0x40000000	/* Data cache is forced write through */
#define DC_LES		0x20000000	/* Caches are little endian mode */
#endif /* CONFIG_8xx */

#endif
