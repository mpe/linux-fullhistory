/*
 * linux/include/asm-arm/proc-armo/page.h
 *
 * Copyright (C) 1995, 1996 Russell King
 */

#ifndef __ASM_PROC_PAGE_H
#define __ASM_PROC_PAGE_H

#include <linux/config.h>

/* PAGE_SHIFT determines the page size.  This is configurable. */
#if defined(CONFIG_PAGESIZE_8)
#define PAGE_SHIFT	13		/* 8K */
#elif defined(CONFIG_PAGESIZE_16)
#define PAGE_SHIFT	14		/* 16K */
#else		/* default */
#define PAGE_SHIFT	15		/* 32K */
#endif
#define PAGE_SIZE       (1UL << PAGE_SHIFT)
#define PAGE_MASK       (~(PAGE_SIZE-1))

#ifdef __KERNEL__

#define STRICT_MM_TYPECHECKS

#ifdef STRICT_MM_TYPECHECKS
/*
 * These are used to make use of C type-checking..
 */
typedef struct { unsigned long pte; } pte_t;
typedef struct { unsigned long pmd; } pmd_t;
typedef struct { unsigned long pgd; } pgd_t;
typedef struct { unsigned long pgprot; } pgprot_t;

#define pte_val(x)      ((x).pte)
#define pmd_val(x)      ((x).pmd)
#define pgd_val(x)      ((x).pgd)
#define pgprot_val(x)   ((x).pgprot)

#define __pte(x)        ((pte_t) { (x) } )
#define __pmd(x)        ((pmd_t) { (x) } )
#define __pgd(x)        ((pgd_t) { (x) } )
#define __pgprot(x)     ((pgprot_t) { (x) } )

#else
/*
 * .. while these make it easier on the compiler
 */
typedef unsigned long pte_t;
typedef unsigned long pmd_t;
typedef unsigned long pgd_t;
typedef unsigned long pgprot_t;

#define pte_val(x)      (x)
#define pmd_val(x)      (x)
#define pgd_val(x)      (x)
#define pgprot_val(x)   (x)

#define __pte(x)        (x)
#define __pmd(x)        (x)
#define __pgd(x)        (x)
#define __pgprot(x)     (x)

#endif

/* to align the pointer to the (next) page boundary */
#define PAGE_ALIGN(addr)	(((addr)+PAGE_SIZE-1)&PAGE_MASK)

/* This handles the memory map.. */
#define PAGE_OFFSET		0x02000000
#define MAP_NR(addr)		(((unsigned long)(addr) - PAGE_OFFSET) >> PAGE_SHIFT)

#endif /* __KERNEL__ */

#endif /* __ASM_PROC_PAGE_H */

