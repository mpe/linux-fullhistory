/*
 * include/asm-i386/cache.h
 */
#ifndef __ARCH_I386_CACHE_H
#define __ARCH_I386_CACHE_H

/* bytes per L1 cache line */
#if    CPU==586 || CPU==686
#define        L1_CACHE_BYTES  32
#else
#define        L1_CACHE_BYTES  16
#endif

#define        L1_CACHE_ALIGN(x)       (((x)+(L1_CACHE_BYTES-1))&~(L1_CACHE_BYTES-1))

#define        SMP_CACHE_BYTES L1_CACHE_BYTES

#endif
