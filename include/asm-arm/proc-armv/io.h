/*
 * linux/include/asm-arm/proc-armv/io.h
 */

/*
 * The caches on some architectures aren't dma-coherent and have need to
 * handle this in software.  There are two types of operations that
 * can be applied to dma buffers.
 *
 *  - dma_cache_wback_inv(start, size) makes caches and RAM coherent by
 *    writing the content of the caches back to memory, if necessary.
 *    The function also invalidates the affected part of the caches as
 *    necessary before DMA transfers from outside to memory.
 *  - dma_cache_inv(start, size) invalidates the affected parts of the
 *    caches.  Dirty lines of the caches may be written back or simply
 *    be discarded.  This operation is necessary before dma operations
 *    to the memory.
 *  - dma_cache_wback(start, size) writes back any dirty lines but does
 *    not invalidate the cache.  This can be used before DMA reads from
 *    memory,
 */

#include <asm/proc-fns.h>

extern inline void dma_cache_inv(unsigned long start, unsigned long size)
{
	processor.u.armv3v4._cache_purge_area(start, start + size);
}

extern inline void dma_cache_wback(unsigned long start, unsigned long size)
{
	processor.u.armv3v4._cache_wback_area(start, start + size);
}

extern inline void dma_cache_wback_inv(unsigned long start, unsigned long size)
{
	processor.u.armv3v4._flush_cache_area(start, start + size, 0);
}
