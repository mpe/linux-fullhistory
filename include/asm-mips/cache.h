/*
 * include/asm-mips/cache.h
 */
#ifndef __ASM_MIPS_CACHE_H
#define __ASM_MIPS_CACHE_H

/* bytes per L1 cache line */
#define        L1_CACHE_BYTES  32      /* a guess */

#define        L1_CACHE_ALIGN(x)       (((x)+(L1_CACHE_BYTES-1))&~(L1_CACHE_BYTES-1))

#endif /* __ASM_MIPS_CACHE_H */
