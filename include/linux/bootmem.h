#ifndef _LINUX_BOOTMEM_H
#define _LINUX_BOOTMEM_H

#include <asm/pgtable.h>
#include <asm/dma.h>
#include <asm/cache.h>
#include <linux/init.h>

/*
 *  simple boot-time physical memory area allocator.
 */

extern unsigned long max_low_pfn;

extern unsigned long __init bootmem_bootmap_pages (unsigned long);
extern unsigned long __init init_bootmem (unsigned long addr, unsigned long memend);
extern void __init reserve_bootmem (unsigned long addr, unsigned long size);
extern void __init free_bootmem (unsigned long addr, unsigned long size);
extern void * __init __alloc_bootmem (unsigned long size, unsigned long align, unsigned long goal);
#define alloc_bootmem(x) \
	__alloc_bootmem((x), SMP_CACHE_BYTES, __pa(MAX_DMA_ADDRESS))
#define alloc_bootmem_pages(x) \
	__alloc_bootmem((x), PAGE_SIZE, __pa(MAX_DMA_ADDRESS))
#define alloc_bootmem_low_pages(x) \
	__alloc_bootmem((x), PAGE_SIZE, 0)
extern unsigned long __init free_all_bootmem (void);

#endif /* _LINUX_BOOTMEM_H */



