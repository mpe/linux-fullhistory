/*
 * include/asm-i386/cache.h
 */
#ifndef __ASMARM_CACHE_H
#define __ASMARM_CACHE_H

#define        L1_CACHE_BYTES  32
#define        L1_CACHE_ALIGN(x)       (((x)+(L1_CACHE_BYTES-1))&~(L1_CACHE_BYTES-1))

#endif
