/* $Id: page.h,v 1.18 1998/05/01 09:33:50 davem Exp $ */

#ifndef _SPARC64_PAGE_H
#define _SPARC64_PAGE_H

#define PAGE_SHIFT   13
#ifndef __ASSEMBLY__
/* I have my suspicions... -DaveM */
#define PAGE_SIZE    (1UL << PAGE_SHIFT)
#else
#define PAGE_SIZE    (1 << PAGE_SHIFT)
#endif

#define PAGE_MASK    (~(PAGE_SIZE-1))


#ifdef __KERNEL__

#ifndef __ASSEMBLY__

#define PAGE_ALIAS_BITS		(PAGE_SIZE)	/* 16K Dcache, 8K pages */
#ifdef __SMP__
#define ULOCK_DECLARE extern spinlock_t user_page_lock;
#else
#define ULOCK_DECLARE
#endif
struct upcache {
	struct page *list;
	unsigned long count;
};
extern struct upcache user_page_cache[2];
#define USER_PAGE_WATER		16

extern unsigned long get_user_page_slow(int which);
#define get_user_page(__vaddr) \
({ \
	ULOCK_DECLARE \
	int which = ((__vaddr) & PAGE_ALIAS_BITS) ? 1 : 0; \
	struct upcache *up = &user_page_cache[which]; \
	struct page *p; \
	unsigned long ret; \
	spin_lock(&user_page_lock); \
	if((p = up->list) != NULL) { \
		up->list = p->next; \
		up->count--; \
	} \
	spin_unlock(&user_page_lock); \
	if(p != NULL) \
		ret = PAGE_OFFSET+PAGE_SIZE*p->map_nr; \
	else \
		ret = get_user_page_slow(which); \
	ret; \
})

#define free_user_page(__page, __addr) \
do { \
	ULOCK_DECLARE \
	int which = ((__addr) & PAGE_ALIAS_BITS) ? 1 : 0; \
	struct upcache *up = &user_page_cache[which]; \
	if(atomic_read(&(__page)->count) == 1 && \
           up->count < USER_PAGE_WATER) { \
		spin_lock(&user_page_lock); \
		(__page)->age = PAGE_INITIAL_AGE; \
		(__page)->next = up->list; \
		up->list = (__page); \
		up->count++; \
		spin_unlock(&user_page_lock); \
	} else \
		free_page(addr); \
} while(0)

#define clear_page(page) memset((void *)(page), 0, PAGE_SIZE)

extern void copy_page(unsigned long to, unsigned long from);

/* GROSS, defining this makes gcc pass these types as aggregates,
 * and thus on the stack, turn this crap off... -DaveM
 */

/* #define STRICT_MM_TYPECHECKS */

#ifdef STRICT_MM_TYPECHECKS
/* These are used to make use of C type-checking.. */
typedef struct { unsigned long pte; } pte_t;
typedef struct { unsigned long iopte; } iopte_t;
typedef struct { unsigned int pmd; } pmd_t;
typedef struct { unsigned int pgd; } pgd_t;
typedef struct { unsigned long ctxd; } ctxd_t;
typedef struct { unsigned long pgprot; } pgprot_t;
typedef struct { unsigned long iopgprot; } iopgprot_t;

#define pte_val(x)	((x).pte)
#define iopte_val(x)	((x).iopte)
#define pmd_val(x)      ((unsigned long)(x).pmd)
#define pgd_val(x)	((unsigned long)(x).pgd)
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

#else
/* .. while these make it easier on the compiler */
typedef unsigned long pte_t;
typedef unsigned long iopte_t;
typedef unsigned int pmd_t;
typedef unsigned int pgd_t;
typedef unsigned long ctxd_t;
typedef unsigned long pgprot_t;
typedef unsigned long iopgprot_t;

#define pte_val(x)	(x)
#define iopte_val(x)	(x)
#define pmd_val(x)      ((unsigned long)(x))
#define pgd_val(x)	((unsigned long)(x))
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

#endif /* (STRICT_MM_TYPECHECKS) */

#define TASK_UNMAPPED_BASE(__off)	(((current->tss.flags & SPARC_FLAG_32BIT) ? \
				 	 (0x0000000070000000UL) : \
				 	 (0xfffff80000000000UL)) + \
					 (__off & PAGE_SIZE))

/* On Ultra this aligns to the size of the L1 cache. */
#define TASK_UNMAPPED_ALIGN(__addr, __off) \
	((((__addr)+((PAGE_SIZE<<1UL)-1UL)) & ~((PAGE_SIZE << 1UL)-1UL)) + \
	 (__off&PAGE_SIZE))

#endif /* !(__ASSEMBLY__) */

/* to align the pointer to the (next) page boundary */
#define PAGE_ALIGN(addr)	(((addr)+PAGE_SIZE-1)&PAGE_MASK)

#ifndef __ASSEMBLY__
/* Do prdele, look what happens to be in %g4... */
register unsigned long page_offset asm("g4");
#define PAGE_OFFSET		page_offset
#else
#define PAGE_OFFSET		0xFFFFF80000000000
#endif

#define __pa(x)			((unsigned long)(x) - PAGE_OFFSET)
#define __va(x)			((void *)((unsigned long) (x) + PAGE_OFFSET))
#define MAP_NR(addr)		(__pa(addr) >> PAGE_SHIFT)

#ifndef __ASSEMBLY__

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

#endif /* !(__ASSEMBLY__) */

#endif /* !(__KERNEL__) */

#endif /* !(_SPARC64_PAGE_H) */
