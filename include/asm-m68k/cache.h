/*
 * include/asm-m68k/cache.h
 */
#ifndef __ARCH_M68K_CACHE_H
#define __ARCH_M68K_CACHE_H

/* bytes per L1 cache line */
#define        L1_CACHE_BYTES  16

#define        L1_CACHE_ALIGN(x)       (((x)+(L1_CACHE_BYTES-1))&~(L1_CACHE_BYTES-1))

#define        SMP_CACHE_BYTES L1_CACHE_BYTES

#ifdef MODULE
#define __cacheline_aligned __attribute__((__aligned__(L1_CACHE_BYTES)))
#else
#define __cacheline_aligned					\
  __attribute__((__aligned__(L1_CACHE_BYTES),			\
		 __section__(".data.cacheline_aligned")))
#endif

#endif
