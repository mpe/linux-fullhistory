/* $Id: page.h,v 1.43 1998/05/11 08:40:11 davem Exp $
 * page.h:  Various defines and such for MMU operations on the Sparc for
 *          the Linux kernel.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC_PAGE_H
#define _SPARC_PAGE_H

#include <linux/config.h>
#ifdef CONFIG_SUN4
#define PAGE_SHIFT   13
#else
#define PAGE_SHIFT   12
#endif
#define PAGE_SIZE    (1 << PAGE_SHIFT)
#define PAGE_MASK    (~(PAGE_SIZE-1))

#ifdef __KERNEL__

#include <asm/head.h>       /* for KERNBASE */
#include <asm/btfixup.h>

/* This is always 2048*sizeof(long), doesn't change with PAGE_SIZE */
#define TASK_UNION_SIZE		8192

#ifndef __ASSEMBLY__

#define clear_page(page)	memset((void *)(page), 0, PAGE_SIZE)
#define copy_page(to,from)	memcpy((void *)(to), (void *)(from), PAGE_SIZE)

extern unsigned long page_offset;

BTFIXUPDEF_SETHI_INIT(page_offset,0xf0000000)

#ifdef MODULE
#define 	PAGE_OFFSET  (page_offset)
#else
#define		PAGE_OFFSET  BTFIXUP_SETHI(page_offset)
#endif

/* translate between physical and virtual addresses */
BTFIXUPDEF_CALL_CONST(unsigned long, mmu_v2p, unsigned long)
BTFIXUPDEF_CALL_CONST(unsigned long, mmu_p2v, unsigned long)

#define mmu_v2p(vaddr) BTFIXUP_CALL(mmu_v2p)(vaddr)
#define mmu_p2v(paddr) BTFIXUP_CALL(mmu_p2v)(paddr)

#define __pa(x)    (mmu_v2p((unsigned long)(x)))
#define __va(x)    ((void *)(mmu_p2v((unsigned long)(x))))

/* The following structure is used to hold the physical
 * memory configuration of the machine.  This is filled in
 * probe_memory() and is later used by mem_init() to set up
 * mem_map[].  We statically allocate SPARC_PHYS_BANKS of
 * these structs, this is arbitrary.  The entry after the
 * last valid one has num_bytes==0.
 */

struct sparc_phys_banks {
  unsigned long base_addr;
  unsigned long num_bytes;
};

#define SPARC_PHYS_BANKS 32

extern struct sparc_phys_banks sp_banks[SPARC_PHYS_BANKS];

/* Cache alias structure.  Entry is valid if context != -1. */
struct cache_palias {
	unsigned long vaddr;
	int context;
};

extern struct cache_palias *sparc_aliases;

/* passing structs on the Sparc slow us down tremendously... */

/* #define STRICT_MM_TYPECHECKS */

#ifdef STRICT_MM_TYPECHECKS
/*
 * These are used to make use of C type-checking..
 */
typedef struct { unsigned long pte; } pte_t;
typedef struct { unsigned long iopte; } iopte_t;
typedef struct { unsigned long pmd; } pmd_t;
typedef struct { unsigned long pgd; } pgd_t;
typedef struct { unsigned long ctxd; } ctxd_t;
typedef struct { unsigned long pgprot; } pgprot_t;
typedef struct { unsigned long iopgprot; } iopgprot_t;

#define pte_val(x)	((x).pte)
#define iopte_val(x)	((x).iopte)
#define pmd_val(x)      ((x).pmd)
#define pgd_val(x)	((x).pgd)
#define ctxd_val(x)	((x).ctxd)
#define pgprot_val(x)	((x).pgprot)
#define iopgprot_val(x)	((x).iopgprot)

#define __pte(x)	((pte_t) { (x) } )
#define __iopte(x)	((iopte_t) { (x) } )
#define __pmd(x)        ((pmd_t) { (x) } )
#define __pgd(x)	((pgd_t) { (x) } )
#define __ctxd(x)	((ctxd_t) { (x) } )
#define __pgprot(x)	((pgprot_t) { (x) } )
#define __iopgprot(x)	((iopgprot_t) { (x) } )

#elif CONFIG_AP1000_DEBUG

typedef struct { unsigned long pte; } pte_t;
typedef struct { unsigned long iopte; } iopte_t;
typedef struct { unsigned long pmd; } pmd_t;
typedef struct { unsigned long pgd; } pgd_t;
typedef struct { unsigned long ctxd; } ctxd_t;
typedef struct { unsigned long pgprot; } pgprot_t;
typedef struct { unsigned long iopgprot; } iopgprot_t;

static inline unsigned long __get_val(unsigned long x)
{
	if ((x & 0xF0000000) == (8<<28))
		return x & 0x0FFFFFFF;
	return x;
}

static inline unsigned long __set_val(unsigned long x)
{
	if ((x & 0xF0000000) == (0<<28))
		return x | 0x80000000;
	return x;
}

#define __pte_val(x)	((x).pte)
#define __iopte_val(x)	((x).iopte)
#define __pmd_val(x)      ((x).pmd)
#define __pgd_val(x)	((x).pgd)
#define __ctxd_val(x)	((x).ctxd)
#define __pgprot_val(x)	((x).pgprot)
#define __iopgprot_val(x)	((x).iopgprot)

#define ___pte(x)	((pte_t) { (x) } )
#define ___iopte(x)	((iopte_t) { (x) } )
#define ___pmd(x)        ((pmd_t) { (x) } )
#define ___pgd(x)	((pgd_t) { (x) } )
#define ___ctxd(x)	((ctxd_t) { (x) } )
#define ___pgprot(x)	((pgprot_t) { (x) } )
#define ___iopgprot(x)	((iopgprot_t) { (x) } )


#define pte_val(x) __get_val(__pte_val(x))
#define iopte_val(x) __get_val(__iopte_val(x))
#define pmd_val(x) __get_val(__pmd_val(x))
#define pgd_val(x) __get_val(__pgd_val(x))
#define ctxd_val(x) __get_val(__ctxd_val(x))
#define pgprot_val(x) __get_val(__pgprot_val(x))
#define iopgprot_val(x) __get_val(__iopgprot_val(x))

#define __pte(x) ___pte(__set_val(x))
#define __iopte(x) ___iopte(__set_val(x))
#define __pmd(x) ___pmd(__set_val(x))
#define __pgd(x) ___pgd(__set_val(x))
#define __ctxd(x) ___ctxd(__set_val(x))
#define __pgprot(x) ___pgprot(x)
#define __iopgprot(x) ___iopgprot(__set_val(x))

#elif CONFIG_AP1000

typedef unsigned long pte_t;
typedef unsigned long iopte_t;
typedef unsigned long pmd_t;
typedef unsigned long pgd_t;
typedef unsigned long ctxd_t;
typedef unsigned long pgprot_t;
typedef unsigned long iopgprot_t;

static inline unsigned long __get_val(unsigned long x)
{
#if 0
	extern void ap_panic(char *fmt,...);
	if (x && (x & 0xF0000000) == 0) {
		ap_panic("get_val got 0x%x\n",x);
	}
#endif
	if ((x & 0xF0000000) == (8<<28))
		return x & 0x0FFFFFFF;
	return x;
}

static inline unsigned long __set_val(unsigned long x)
{
#if 0
	extern void ap_panic(char *fmt,...);
	if ((x & 0xF0000000) == (8<<28)) {
		ap_panic("set_val got 0x%x\n",x);
	}
#endif
	if ((x & 0xF0000000) == (0<<28))
		return x | 0x80000000;
	return x;
}

#define __pte_val(x)	(x)
#define __iopte_val(x)	(x)
#define __pmd_val(x)      (x)
#define __pgd_val(x)	(x)
#define __ctxd_val(x)	(x)
#define __pgprot_val(x)	(x)
#define __iopgprot_val(x)	(x)

#define ___pte(x)	((pte_t) { (x) } )
#define ___iopte(x)	((iopte_t) { (x) } )
#define ___pmd(x)        ((pmd_t) { (x) } )
#define ___pgd(x)	((pgd_t) { (x) } )
#define ___ctxd(x)	((ctxd_t) { (x) } )
#define ___pgprot(x)	((pgprot_t) { (x) } )
#define ___iopgprot(x)	((iopgprot_t) { (x) } )


#define pte_val(x) __get_val(__pte_val(x))
#define iopte_val(x) __get_val(__iopte_val(x))
#define pmd_val(x) __get_val(__pmd_val(x))
#define pgd_val(x) __get_val(__pgd_val(x))
#define ctxd_val(x) __get_val(__ctxd_val(x))
#define pgprot_val(x) __get_val(__pgprot_val(x))
#define iopgprot_val(x) __get_val(__iopgprot_val(x))

#define __pte(x) ___pte(__set_val(x))
#define __iopte(x) ___iopte(__set_val(x))
#define __pmd(x) ___pmd(__set_val(x))
#define __pgd(x) ___pgd(__set_val(x))
#define __ctxd(x) ___ctxd(__set_val(x))
#define __pgprot(x) ___pgprot(x)
#define __iopgprot(x) ___iopgprot(__set_val(x))

#else
/*
 * .. while these make it easier on the compiler
 */
typedef unsigned long pte_t;
typedef unsigned long iopte_t;
typedef unsigned long pmd_t;
typedef unsigned long pgd_t;
typedef unsigned long ctxd_t;
typedef unsigned long pgprot_t;
typedef unsigned long iopgprot_t;

#define pte_val(x)	(x)
#define iopte_val(x)	(x)
#define pmd_val(x)      (x)
#define pgd_val(x)	(x)
#define ctxd_val(x)	(x)
#define pgprot_val(x)	(x)
#define iopgprot_val(x)	(x)

#define __pte(x)	(x)
#define __iopte(x)	(x)
#define __pmd(x)        (x)
#define __pgd(x)	(x)
#define __ctxd(x)	(x)
#define __pgprot(x)	(x)
#define __iopgprot(x)	(x)

#endif

extern unsigned long sparc_unmapped_base;

BTFIXUPDEF_SETHI(sparc_unmapped_base)

#define TASK_UNMAPPED_BASE	BTFIXUP_SETHI(sparc_unmapped_base)

/* to align the pointer to the (next) page boundary */
#define PAGE_ALIGN(addr)  (((addr)+PAGE_SIZE-1)&PAGE_MASK)

/* Now, to allow for very large physical memory configurations we
 * place the page pool both above the kernel and below the kernel.
 */
#define MAP_NR(addr) ((((unsigned long) (addr)) - PAGE_OFFSET) >> PAGE_SHIFT)

#else /* !(__ASSEMBLY__) */

#define __pgprot(x)	(x)

#endif /* !(__ASSEMBLY__) */

#endif /* __KERNEL__ */

#endif /* _SPARC_PAGE_H */
