/*
 * include/asm-alpha/cache.h
 */
#ifndef __ARCH_ALPHA_CACHE_H
#define __ARCH_ALPHA_CACHE_H

/* Bytes per L1 (data) cache line.  Both EV4 and EV5 are write-through,
   read-allocate, direct-mapped, physical. */
#define L1_CACHE_BYTES     32
#define L1_CACHE_ALIGN(x)  (((x)+(L1_CACHE_BYTES-1))&~(L1_CACHE_BYTES-1))

#endif
