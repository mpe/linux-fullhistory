#ifndef _ALPHA_PAGE_H
#define _ALPHA_PAGE_H

#define CONFIG_STRICT_MM_TYPECHECKS

#define invalidate_all() \
__asm__ __volatile__( \
	"lda $16,-2($31)\n\t" \
	".long 51" \
	: : :"$1", "$16", "$17", "$22","$23","$24","$25")

#define invalidate() \
__asm__ __volatile__( \
	"lda $16,-1($31)\n\t" \
	".long 51" \
	: : :"$1", "$16", "$17", "$22","$23","$24","$25")

/* PAGE_SHIFT determines the page size */
#define PAGE_SHIFT			13
#define PGDIR_SHIFT			23
#define PAGE_SIZE			(1UL << PAGE_SHIFT)
#define PGDIR_SIZE			(1UL << PGDIR_SHIFT)

#ifdef __KERNEL__

#define PAGE_OFFSET 0xFFFFFC0000000000
#define MAP_NR(addr) (((addr) - PAGE_OFFSET) >> PAGE_SHIFT)
#define MAP_PAGE_RESERVED (1<<31)

typedef unsigned int mem_map_t;

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

#else
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

#endif

/*
 * OSF/1 PAL-code-imposed page table bits
 */
#define _PAGE_VALID	0x0001
#define _PAGE_FOR	0x0002	/* used for page protection (fault on read) */
#define _PAGE_FOW	0x0004	/* used for page protection (fault on write) */
#define _PAGE_FOE	0x0008	/* used for page protection (fault on exec) */
#define _PAGE_ASM	0x0010
#define _PAGE_KRE	0x0100	/* xxx - see below on the "accessed" bit */
#define _PAGE_URE	0x0200	/* xxx */
#define _PAGE_KWE	0x1000	/* used to do the dirty bit in software */
#define _PAGE_UWE	0x2000	/* used to do the dirty bit in software */

/* .. and these are ours ... */
#define _PAGE_COW	0x10000
#define _PAGE_DIRTY	0x20000
#define _PAGE_ACCESSED	0x40000

/*
 * NOTE! The "accessed" bit isn't necessarily exact: it can be kept exactly
 * by software (use the KRE/URE/KWE/UWE bits appropritely), but I'll fake it.
 * Under Linux/AXP, the "accessed" bit just means "read", and I'll just use
 * the KRE/URE bits to watch for it. That way we don't need to overload the
 * KWE/UWE bits with both handling dirty and accessed.
 *
 * Note that the kernel uses the accessed bit just to check whether to page
 * out a page or not, so it doesn't have to be exact anyway.
 */

#define __DIRTY_BITS	(_PAGE_DIRTY | _PAGE_KWE | _PAGE_UWE)
#define __ACCESS_BITS	(_PAGE_ACCESSED | _PAGE_KRE | _PAGE_URE)

#define _PFN_MASK	0xFFFFFFFF00000000

#define _PAGE_TABLE	(_PAGE_VALID | __DIRTY_BITS | __ACCESS_BITS)
#define _PAGE_CHG_MASK	(_PFN_MASK | __DIRTY_BITS | __ACCESS_BITS)

/*
 * All the normal masks have the "page accessed" bits on, as any time they are used,
 * the page is accessed. They are cleared only by the page-out routines
 */
#define PAGE_NONE	__pgprot(_PAGE_VALID | __ACCESS_BITS | _PAGE_FOR | _PAGE_FOW | _PAGE_FOE)
#define PAGE_SHARED	__pgprot(_PAGE_VALID | __ACCESS_BITS)
#define PAGE_COPY	__pgprot(_PAGE_VALID | __ACCESS_BITS | _PAGE_FOW | _PAGE_COW)
#define PAGE_READONLY	__pgprot(_PAGE_VALID | __ACCESS_BITS | _PAGE_FOW)
#define PAGE_KERNEL	__pgprot(_PAGE_VALID | _PAGE_ASM | __ACCESS_BITS | __DIRTY_BITS)

#define _PAGE_NORMAL(x) __pgprot(_PAGE_VALID | __ACCESS_BITS | (x))

#define __P000	_PAGE_NORMAL(_PAGE_COW | _PAGE_FOR | _PAGE_FOW | _PAGE_FOE)
#define __P001	_PAGE_NORMAL(_PAGE_COW | _PAGE_FOW | _PAGE_FOE)
#define __P010	_PAGE_NORMAL(_PAGE_COW | _PAGE_FOR | _PAGE_FOE)
#define __P011	_PAGE_NORMAL(_PAGE_COW | _PAGE_FOE)
#define __P100	_PAGE_NORMAL(_PAGE_COW | _PAGE_FOR | _PAGE_FOW)
#define __P101	_PAGE_NORMAL(_PAGE_COW | _PAGE_FOW)
#define __P110	_PAGE_NORMAL(_PAGE_COW | _PAGE_FOR)
#define __P111	_PAGE_NORMAL(_PAGE_COW)

#define __S000	_PAGE_NORMAL(_PAGE_FOR | _PAGE_FOW | _PAGE_FOE)
#define __S001	_PAGE_NORMAL(_PAGE_FOW | _PAGE_FOE)
#define __S010	_PAGE_NORMAL(_PAGE_FOR | _PAGE_FOE)
#define __S011	_PAGE_NORMAL(_PAGE_FOE)
#define __S100	_PAGE_NORMAL(_PAGE_FOR | _PAGE_FOW)
#define __S101	_PAGE_NORMAL(_PAGE_FOW)
#define __S110	_PAGE_NORMAL(_PAGE_FOR)
#define __S111	_PAGE_NORMAL(0)

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
#define SIZEOF_PTR_LOG2			3

/* to find an entry in a page-table-directory */
/*
 * XXXXX This isn't right: we shouldn't use the ptbr, but the L2 pointer.
 * This is just for getting it through the compiler right now
 */
#define PAGE_DIR_OFFSET(tsk,address) \
((pgd_t *) ((tsk)->tss.ptbr + ((((unsigned long)(address)) >> 21) & PTR_MASK & ~PAGE_MASK)))

/* to find an entry in a page-table */
#define PAGE_PTR(address)		\
  ((unsigned long)(address)>>(PAGE_SHIFT-SIZEOF_PTR_LOG2)&PTR_MASK&~PAGE_MASK)

/* the no. of pointers that fit on a page */
#define PTRS_PER_PAGE			(PAGE_SIZE/sizeof(void*))

/* to set the page-dir */
/*
 * XXXXX This isn't right: we shouldn't use the ptbr, but the L2 pointer.
 * This is just for getting it through the compiler right now
 */
#define SET_PAGE_DIR(tsk,pgdir) \
do { \
	(tsk)->tss.ptbr = (unsigned long) (pgdir); \
	if ((tsk) == current) \
		invalidate(); \
} while (0)

extern unsigned long high_memory;

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
extern inline pte_t mk_pte(unsigned long page, pgprot_t pgprot)
{ pte_t pte; pte_val(pte) = (page << (32-PAGE_SHIFT)) | pgprot_val(pgprot); return pte; }

extern inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{ pte_val(pte) = (pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot); return pte; }

extern inline void pgd_set(pgd_t * pgdp, pte_t * ptep)
{ pgd_val(*pgdp) = _PAGE_TABLE | (((unsigned long) ptep) << (32-PAGE_SHIFT)); }

extern inline unsigned long pte_page(pte_t pte)	{ return (pte_val(pte) & _PFN_MASK) >> (32-PAGE_SHIFT); }
extern inline unsigned long pgd_page(pgd_t pgd)	{ return (pgd_val(pgd) & _PFN_MASK) >> (32-PAGE_SHIFT); }

extern inline int pte_none(pte_t pte)		{ return !pte_val(pte); }
extern inline int pte_present(pte_t pte)	{ return pte_val(pte) & _PAGE_VALID; }
extern inline void pte_clear(pte_t *ptep)	{ pte_val(*ptep) = 0; }

extern inline int pgd_none(pgd_t pgd)		{ return !pgd_val(pgd); }
extern inline int pgd_bad(pgd_t pgd)		{ return (pgd_val(pgd) & ~_PFN_MASK) != _PAGE_TABLE || pgd_page(pgd) > high_memory; }
extern inline int pgd_present(pgd_t pgd)	{ return pgd_val(pgd) & _PAGE_VALID; }
extern inline void pgd_clear(pgd_t * pgdp)	{ pgd_val(*pgdp) = 0; }

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
extern inline int pte_read(pte_t pte)		{ return !(pte_val(pte) & _PAGE_FOR); }
extern inline int pte_write(pte_t pte)		{ return !(pte_val(pte) & _PAGE_FOW); }
extern inline int pte_exec(pte_t pte)		{ return !(pte_val(pte) & _PAGE_FOE); }
extern inline int pte_dirty(pte_t pte)		{ return pte_val(pte) & _PAGE_DIRTY; }
extern inline int pte_young(pte_t pte)		{ return pte_val(pte) & _PAGE_ACCESSED; }
extern inline int pte_cow(pte_t pte)		{ return pte_val(pte) & _PAGE_COW; }

extern inline pte_t pte_wrprotect(pte_t pte)	{ pte_val(pte) |= _PAGE_FOW; return pte; }
extern inline pte_t pte_rdprotect(pte_t pte)	{ pte_val(pte) |= _PAGE_FOR; return pte; }
extern inline pte_t pte_exprotect(pte_t pte)	{ pte_val(pte) |= _PAGE_FOE; return pte; }
extern inline pte_t pte_mkclean(pte_t pte)	{ pte_val(pte) &= ~(__DIRTY_BITS); return pte; }
extern inline pte_t pte_mkold(pte_t pte)	{ pte_val(pte) &= ~(__ACCESS_BITS); return pte; }
extern inline pte_t pte_uncow(pte_t pte)	{ pte_val(pte) &= ~_PAGE_COW; return pte; }
extern inline pte_t pte_mkwrite(pte_t pte)	{ pte_val(pte) &= _PAGE_FOW; return pte; }
extern inline pte_t pte_mkread(pte_t pte)	{ pte_val(pte) &= _PAGE_FOR; return pte; }
extern inline pte_t pte_mkexec(pte_t pte)	{ pte_val(pte) &= _PAGE_FOE; return pte; }
extern inline pte_t pte_mkdirty(pte_t pte)	{ pte_val(pte) |= __DIRTY_BITS; return pte; }
extern inline pte_t pte_mkyoung(pte_t pte)	{ pte_val(pte) |= __ACCESS_BITS; return pte; }
extern inline pte_t pte_mkcow(pte_t pte)	{ pte_val(pte) |= _PAGE_COW; return pte; }

#endif /* __KERNEL__ */

#endif /* _ALPHA_PAGE_H */
