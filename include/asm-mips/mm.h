#ifndef __ASM_MIPS_MM_H
#define __ASM_MIPS_MM_H

#if defined (__KERNEL__)

/*
 * Note that we shift the lower 32bits of each EntryLo[01] entry
 * 6 bits to the left. That way we can convert the PFN into the
 * physical address by a single 'and' operation and gain 6 additional
 * bits for storing information which isn't present in a normal
 * MIPS page table.
 * I've also changed the naming of some bits so that they conform
 * the i386 naming as much as possible.
 * PAGE_USER isn't implemented in software yet.
 */
#define PAGE_PRESENT               (1<<0)   /* implemented in software */
#define PAGE_COW                   (1<<1)   /* implemented in software */
#define PAGE_DIRTY                 (1<<2)   /* implemented in software */
#define PAGE_USER                  (1<<3)   /* implemented in software */
#define PAGE_UNUSED1               (1<<4)   /* implemented in software */
#define PAGE_UNUSED2               (1<<5)   /* implemented in software */
#define PAGE_GLOBAL                (1<<6)
#define PAGE_ACCESSED              (1<<7)   /* The Mips valid bit      */
#define PAGE_RW                    (1<<8)   /* The Mips dirty bit      */
#define CACHE_CACHABLE_NO_WA       (0<<9)
#define CACHE_CACHABLE_WA          (1<<9)
#define CACHE_UNCACHED             (2<<9)
#define CACHE_CACHABLE_NONCOHERENT (3<<9)
#define CACHE_CACHABLE_CE          (4<<9)
#define CACHE_CACHABLE_COW         (5<<9)
#define CACHE_CACHABLE_CUW         (6<<9)
#define CACHE_MASK                 (7<<9)

#define PAGE_PRIVATE    (PAGE_PRESENT | PAGE_ACCESSED | PAGE_DIRTY | PAGE_RW | \
                         PAGE_COW | CACHE_CACHABLE_NO_WA)
#define PAGE_SHARED     (PAGE_PRESENT | PAGE_ACCESSED | PAGE_DIRTY | PAGE_RW | \
                         CACHE_CACHABLE_NO_WA)
#define PAGE_COPY       (PAGE_PRESENT | PAGE_ACCESSED | PAGE_COW | \
                         CACHE_CACHABLE_NO_WA)
#define PAGE_READONLY   (PAGE_PRESENT | PAGE_ACCESSED | CACHE_CACHABLE_NO_WA)
#define PAGE_TABLE      (PAGE_PRESENT | PAGE_ACCESSED | PAGE_DIRTY | PAGE_RW | \
                         CACHE_CACHABLE_NO_WA)

#ifndef __ASSEMBLY__

#include <asm/system.h>

extern unsigned long invalid_pg_table[1024];

/*
 * memory.c & swap.c
 */
extern void mem_init(unsigned long start_mem, unsigned long end_mem);
#endif /* !defined (__ASSEMBLY__) */

#endif /* defined (__KERNEL__) */

#endif /* __ASM_MIPS_MM_H */
