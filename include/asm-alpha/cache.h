/*
 * include/asm-alpha/cache.h
 */
#ifndef __ARCH_ALPHA_CACHE_H
#define __ARCH_ALPHA_CACHE_H

/* bytes per L1 cache line */
#define        L1_CACHE_BYTES  32      /* a guess */

#define        L1_CACHE_ALIGN(x)       (((x)+(L1_CACHE_BYTES-1))&~(L1_CACHE_BYTES-1))

#endif
