#ifndef __ASM_MIPS_PARAM_H
#define __ASM_MIPS_PARAM_H

#ifndef HZ

#include <linux/config.h>

#ifdef CONFIG_DECSTATION
   /*
    * log2(HZ), change this here if you want another
    * HZ value. This is also used in dec_time_init.
    * Minimum is 1, Maximum is 15.
    */
#  define LOG_2_HZ 7
#  define HZ (1 << LOG_2_HZ)
   /*
    * Ye olde division-by-multiplication trick.
    *
    * This works only if 100 / HZ <= 1
    */
#  define QUOTIENT ((1UL << (32 - LOG_2_HZ)) * 100)
#  define HZ_TO_STD(a)                            \
   ({ int __res;                                  \
        __asm__(                                  \
           "multu\t%0,%2\n\t"			  \
           "mfhi\t%0"				  \
        : "=r" (__res): "0" (a), "r" (QUOTIENT)); \
        __res;})
#else
#  define HZ 100
#  define HZ_TO_STD(a) (a)
#endif

#endif

#define EXEC_PAGESIZE	4096

#ifndef NGROUPS
#define NGROUPS		32
#endif

#ifndef NOGROUP
#define NOGROUP		(-1)
#endif

#define MAXHOSTNAMELEN	64	/* max length of hostname */

#endif /* __ASM_MIPS_PARAM_H */
