#ifndef __ASM_MIPS_PAGE_H
#define __ASM_MIPS_PAGE_H

#define CONFIG_STRICT_MM_TYPECHECKS

#ifndef __ASSEMBLY__

#include <linux/linkage.h>

#define invalidate()	tlbflush();
extern asmlinkage void tlbflush(void);

typedef unsigned short mem_map_t;

#ifdef CONFIG_STRICT_MM_TYPECHECKS
/*
 * These are used to make use of C type-checking..
 */
typedef struct { unsigned long pte; } pte_t;
typedef struct { unsigned long pgd; } pgd_t;
typedef struct { unsigned long pgprot; } pgprot_t;

#define pte_val(x)	((x).pte)
#define pgd_val(x)	((x).pgd)
#define pgprot_val(x)	((x).pgprot)

#define __pte(x)	((pte_t) { (x) } )
#define __pgd(x)	((pgd_t) { (x) } )
#define __pgprot(x)	((pgprot_t) { (x) } )

#else /* !defined (CONFIG_STRICT_MM_TYPECHECKS) */
/*
 * .. while these make it easier on the compiler
 */
typedef unsigned long pte_t;
typedef unsigned long pgd_t;
typedef unsigned long pgprot_t;

#define pte_val(x)	(x)
#define pgd_val(x)	(x)
#define pgprot_val(x)	(x)

#define __pte(x)	(x)
#define __pgd(x)	(x)
#define __pgprot(x)	(x)

#endif /* !defined (CONFIG_STRICT_MM_TYPECHECKS) */
#endif /* __ASSEMBLY__ */

/*
 * Note that we shift the lower 32bits of each EntryLo[01] entry
 * 6 bits to the left. That way we can convert the PFN into the
 * physical address by a single 'and' operation and gain 6 additional
 * bits for storing information which isn't present in a normal
 * MIPS page table.
 */
#define _PAGE_PRESENT               (1<<0)  /* implemented in software */
#define _PAGE_COW                   (1<<1)  /* implemented in software */
#define _PAGE_DIRTY                 (1<<2)  /* implemented in software */
#define _PAGE_USER                  (1<<3)
#define _PAGE_UNUSED1               (1<<4)
#define _PAGE_UNUSED2               (1<<5)
#define _PAGE_GLOBAL                (1<<6)
#define _PAGE_ACCESSED              (1<<7)   /* The MIPS valid bit      */
#define _PAGE_RW                    (1<<8)   /* The MIPS dirty bit      */
#define _CACHE_CACHABLE_NO_WA       (0<<9)
#define _CACHE_CACHABLE_WA          (1<<9)
#define _CACHE_UNCACHED             (2<<9)
#define _CACHE_CACHABLE_NONCOHERENT (3<<9)
#define _CACHE_CACHABLE_CE          (4<<9)
#define _CACHE_CACHABLE_COW         (5<<9)
#define _CACHE_CACHABLE_CUW         (6<<9)
#define _CACHE_MASK                 (7<<9)

#define _PAGE_TABLE     (_PAGE_PRESENT | _PAGE_RW | _PAGE_USER | _PAGE_ACCESSED | _PAGE_DIRTY | \
                         _CACHE_CACHABLE_NO_WA)

#define _PAGE_CHG_MASK  (PAGE_MASK | _PAGE_ACCESSED | _PAGE_DIRTY | _CACHE_MASK)

#define PAGE_NONE	__pgprot(_PAGE_PRESENT | _PAGE_ACCESSED)
#define PAGE_SHARED     __pgprot(_PAGE_PRESENT | _PAGE_RW | _PAGE_USER | _PAGE_ACCESSED | _CACHE_CACHABLE_NO_WA)
#define PAGE_COPY       __pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_ACCESSED | _PAGE_COW | _CACHE_CACHABLE_NO_WA)
#define PAGE_READONLY   __pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_ACCESSED | _CACHE_CACHABLE_NO_WA)
#define PAGE_KERNEL	__pgprot(_PAGE_PRESENT | _PAGE_RW | _PAGE_DIRTY | _PAGE_ACCESSED)

/*
 * MIPS can't do page protection for execute, and considers that the same like
 * read. Also, write permissions imply read permissions. This is the closest
 * we can get by reasonable means..
 */
#define __P000	PAGE_NONE
#define __P001	PAGE_READONLY
#define __P010	PAGE_COPY
#define __P011	PAGE_COPY
#define __P100	PAGE_READONLY
#define __P101	PAGE_READONLY
#define __P110	PAGE_COPY
#define __P111	PAGE_COPY

#define __S000	PAGE_NONE
#define __S001	PAGE_READONLY
#define __S010	PAGE_SHARED
#define __S011	PAGE_SHARED
#define __S100	PAGE_READONLY
#define __S101	PAGE_READONLY
#define __S110	PAGE_SHARED
#define __S111	PAGE_SHARED

/* PAGE_SHIFT determines the page size */
#define PAGE_SHIFT			12
#define PGDIR_SHIFT			22
#define PAGE_SIZE			(1UL << PAGE_SHIFT)
#define PGDIR_SIZE			(1UL << PGDIR_SHIFT)

#ifdef __KERNEL__

#define PAGE_OFFSET	KERNELBASE
#define MAP_NR(addr)	(((addr) - PAGE_OFFSET) >> PAGE_SHIFT)
#define MAP_PAGE_RESERVED (1<<15)

#if !defined (__ASSEMBLY__)

/* page table for 0-4MB for everybody */
extern unsigned long pg0[1024];

/*
 * BAD_PAGETABLE is used when we need a bogus page-table, while
 * BAD_PAGE is used for a bogus page.
 *
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
extern pte_t __bad_page(void);
extern pte_t * __bad_pagetable(void);

extern unsigned long __zero_page(void);

#define BAD_PAGETABLE __bad_pagetable()
#define BAD_PAGE __bad_page()
#define ZERO_PAGE __zero_page()

/* number of bits that fit into a memory pointer */
#define BITS_PER_PTR			(8*sizeof(unsigned long))

/* to mask away the intra-page address bits */
#define PAGE_MASK			(~(PAGE_SIZE-1))

/* to mask away the intra-page address bits */
#define PGDIR_MASK			(~(PGDIR_SIZE-1))

/* to align the pointer to the (next) page boundary */
#define PAGE_ALIGN(addr)		(((addr)+PAGE_SIZE-1)&PAGE_MASK)

/* to align the pointer to a pointer address */
#define PTR_MASK			(~(sizeof(void*)-1))

/* sizeof(void*)==1<<SIZEOF_PTR_LOG2 */
/* 64-bit machines, beware!  SRB. */
#define SIZEOF_PTR_LOG2			2

/* to find an entry in a page-table-directory */
#define PAGE_DIR_OFFSET(tsk,address) \
((((unsigned long)(address)) >> PGDIR_SHIFT) + (pgd_t *) (tsk)->tss.pg_dir)

/* to find an entry in a page-table */
#define PAGE_PTR(address) \
((unsigned long)(address)>>(PAGE_SHIFT-SIZEOF_PTR_LOG2)&PTR_MASK&~PAGE_MASK)

/* the no. of pointers that fit on a page */
#define PTRS_PER_PAGE			(PAGE_SIZE/sizeof(void*))

/* to set the page-dir */
#define SET_PAGE_DIR(tsk,pgdir) \
do { \
	(tsk)->tss.pg_dir = (unsigned long) (pgdir); \
	if ((tsk) == current) \
		invalidate(); \
} while (0)

extern unsigned long high_memory;

extern inline int pte_none(pte_t pte)		{ return !pte_val(pte); }
extern inline int pte_present(pte_t pte)	{ return pte_val(pte) & _PAGE_PRESENT; }
extern inline void pte_clear(pte_t *ptep)	{ pte_val(*ptep) = 0; }

extern inline int pgd_none(pgd_t pgd)		{ return !pgd_val(pgd); }
extern inline int pgd_bad(pgd_t pgd)		{ return (pgd_val(pgd) & ~PAGE_MASK) != _PAGE_TABLE || pgd_val(pgd) > high_memory; }
extern inline int pgd_present(pgd_t pgd)	{ return pgd_val(pgd) & _PAGE_PRESENT; }
extern inline void pgd_clear(pgd_t * pgdp)	{ pgd_val(*pgdp) = 0; }

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
extern inline int pte_read(pte_t pte)		{ return pte_val(pte) & _PAGE_USER; }
extern inline int pte_write(pte_t pte)		{ return pte_val(pte) & _PAGE_RW; }
extern inline int pte_exec(pte_t pte)		{ return pte_val(pte) & _PAGE_USER; }
extern inline int pte_dirty(pte_t pte)		{ return pte_val(pte) & _PAGE_DIRTY; }
extern inline int pte_young(pte_t pte)		{ return pte_val(pte) & _PAGE_ACCESSED; }
extern inline int pte_cow(pte_t pte)		{ return pte_val(pte) & _PAGE_COW; }

extern inline pte_t pte_wrprotect(pte_t pte)	{ pte_val(pte) &= ~_PAGE_RW; return pte; }
extern inline pte_t pte_rdprotect(pte_t pte)	{ pte_val(pte) &= ~_PAGE_USER; return pte; }
extern inline pte_t pte_exprotect(pte_t pte)	{ pte_val(pte) &= ~_PAGE_USER; return pte; }
extern inline pte_t pte_mkclean(pte_t pte)	{ pte_val(pte) &= ~_PAGE_DIRTY; return pte; }
extern inline pte_t pte_mkold(pte_t pte)	{ pte_val(pte) &= ~_PAGE_ACCESSED; return pte; }
extern inline pte_t pte_uncow(pte_t pte)	{ pte_val(pte) &= ~_PAGE_COW; return pte; }
extern inline pte_t pte_mkwrite(pte_t pte)	{ pte_val(pte) |= _PAGE_RW; return pte; }
extern inline pte_t pte_mkread(pte_t pte)	{ pte_val(pte) |= _PAGE_USER; return pte; }
extern inline pte_t pte_mkexec(pte_t pte)	{ pte_val(pte) |= _PAGE_USER; return pte; }
extern inline pte_t pte_mkdirty(pte_t pte)	{ pte_val(pte) |= _PAGE_DIRTY; return pte; }
extern inline pte_t pte_mkyoung(pte_t pte)	{ pte_val(pte) |= _PAGE_ACCESSED; return pte; }
extern inline pte_t pte_mkcow(pte_t pte)	{ pte_val(pte) |= _PAGE_COW; return pte; }

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
extern inline pte_t mk_pte(unsigned long page, pgprot_t pgprot)
{ pte_t pte; pte_val(pte) = page | pgprot_val(pgprot); return pte; }

extern inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{ pte_val(pte) = (pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot); return pte; }

extern inline unsigned long pte_page(pte_t pte)	{ return pte_val(pte) & PAGE_MASK; }
extern inline unsigned long pgd_page(pgd_t pgd)	{ return pgd_val(pgd) & PAGE_MASK; }

extern inline void pgd_set(pgd_t * pgdp, pte_t * ptep)
{ pgd_val(*pgdp) = _PAGE_TABLE | (unsigned long) ptep; }

#endif /* !defined (__ASSEMBLY__) */
#endif /* defined (__KERNEL__) */

#endif /* __ASM_MIPS_PAGE_H */
