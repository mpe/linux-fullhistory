/*
 *  linux/arch/m68k/mm/memory.c
 *
 *  Copyright (C) 1995  Hamish Macdonald
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/malloc.h>

#include <asm/setup.h>
#include <asm/segment.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/traps.h>
#include <asm/io.h>
#include <asm/machdep.h>
#ifdef CONFIG_AMIGA
#include <asm/amigahw.h>
#endif

struct pgtable_cache_struct quicklists;

void __bad_pte(pmd_t *pmd)
{
	printk("Bad pmd in pte_alloc: %08lx\n", pmd_val(*pmd));
	pmd_set(pmd, BAD_PAGETABLE);
}

void __bad_pmd(pgd_t *pgd)
{
	printk("Bad pgd in pmd_alloc: %08lx\n", pgd_val(*pgd));
	pgd_set(pgd, (pmd_t *)BAD_PAGETABLE);
}

pte_t *get_pte_slow(pmd_t *pmd, unsigned long offset)
{
	pte_t *pte;

	pte = (pte_t *) __get_free_page(GFP_KERNEL);
	if (pmd_none(*pmd)) {
		if (pte) {
			clear_page((unsigned long)pte);
			flush_page_to_ram((unsigned long)pte);
			flush_tlb_kernel_page((unsigned long)pte);
			nocache_page((unsigned long)pte);
			pmd_set(pmd, pte);
			return pte + offset;
		}
		pmd_set(pmd, BAD_PAGETABLE);
		return NULL;
	}
	free_page((unsigned long)pte);
	if (pmd_bad(*pmd)) {
		__bad_pte(pmd);
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + offset;
}

pmd_t *get_pmd_slow(pgd_t *pgd, unsigned long offset)
{
	pmd_t *pmd;

	pmd = get_pointer_table();
	if (pgd_none(*pgd)) {
		if (pmd) {
			pgd_set(pgd, pmd);
			return pmd + offset;
		}
		pgd_set(pgd, (pmd_t *)BAD_PAGETABLE);
		return NULL;
	}
	free_pointer_table(pmd);
	if (pgd_bad(*pgd)) {
		__bad_pmd(pgd);
		return NULL;
	}
	return (pmd_t *) pgd_page(*pgd) + offset;
}


/* ++andreas: {get,free}_pointer_table rewritten to use unused fields from
   struct page instead of separately kmalloced struct.  Stolen from
   arch/sparc/mm/srmmu.c ... */

typedef struct page ptable_desc;
static ptable_desc ptable_list = { &ptable_list, &ptable_list };

#define PD_MARKBITS(dp) (*(unsigned char *)&(dp)->offset)
#define PD_PAGE(dp) (PAGE_OFFSET + ((dp)->map_nr << PAGE_SHIFT))
#define PAGE_PD(page) ((ptable_desc *)&mem_map[MAP_NR(page)])

#define PTABLE_SIZE (PTRS_PER_PMD * sizeof(pmd_t))

pmd_t *get_pointer_table (void)
{
	ptable_desc *dp = ptable_list.next;
	unsigned char mask = PD_MARKBITS (dp);
	unsigned char tmp;
	unsigned int off;

	/*
	 * For a pointer table for a user process address space, a
	 * table is taken from a page allocated for the purpose.  Each
	 * page can hold 8 pointer tables.  The page is remapped in
	 * virtual address space to be noncacheable.
	 */
	if (mask == 0) {
		unsigned long page;
		ptable_desc *new;

		if (!(page = get_free_page (GFP_KERNEL)))
			return 0;

		flush_tlb_kernel_page(page);
		nocache_page (page);

		new = PAGE_PD(page);
		PD_MARKBITS(new) = 0xfe;
		(new->prev = dp->prev)->next = new;
		(new->next = dp)->prev = new;
		return (pmd_t *)page;
	}

	for (tmp = 1, off = 0; (mask & tmp) == 0; tmp <<= 1, off += PTABLE_SIZE);
	PD_MARKBITS(dp) = mask & ~tmp;
	if (!PD_MARKBITS(dp)) {
		ptable_desc *last, *next;

		/* move to end of list */
		next = dp->next;
		(next->prev = dp->prev)->next = next;

		last = ptable_list.prev;
		(dp->next = last->next)->prev = dp;
		(dp->prev = last)->next = dp;
	}
	return (pmd_t *) (PD_PAGE(dp) + off);
}

int free_pointer_table (pmd_t *ptable)
{
	ptable_desc *dp, *first;
	unsigned long page = (unsigned long)ptable & PAGE_MASK;
	unsigned char mask = 1 << (((unsigned long)ptable - page)/PTABLE_SIZE);

	dp = PAGE_PD(page);
	if (PD_MARKBITS (dp) & mask)
		panic ("table already free!");

	PD_MARKBITS (dp) |= mask;

	if (PD_MARKBITS(dp) == 0xff) {
		/* all tables in page are free, free page */
		ptable_desc *next = dp->next;
		(next->prev = dp->prev)->next = next;
		cache_page (page);
		free_page (page);
		return 1;
	} else if ((first = ptable_list.next) != dp) {
		/*
		 * move this descriptor to the front of the list, since
		 * it has one or more free tables.
		 */
		ptable_desc *next = dp->next;
		(next->prev = dp->prev)->next = next;

		(dp->prev = first->prev)->next = dp;
		(dp->next = first)->prev = dp;
	}
	return 0;
}

/* maximum pages used for kpointer tables */
#define KPTR_PAGES      4
/* # of reserved slots */
#define RESERVED_KPTR	4
extern pmd_tablepage kernel_pmd_table; /* reserved in head.S */

static struct kpointer_pages {
        pmd_tablepage *page[KPTR_PAGES];
        u_char alloced[KPTR_PAGES];
} kptr_pages;

void init_kpointer_table(void) {
	short i = KPTR_PAGES-1;

	/* first page is reserved in head.S */
	kptr_pages.page[i] = &kernel_pmd_table;
	kptr_pages.alloced[i] = ~(0xff>>RESERVED_KPTR);
	for (i--; i>=0; i--) {
		kptr_pages.page[i] = NULL;
		kptr_pages.alloced[i] = 0;
	}
}

pmd_t *get_kpointer_table (void)
{
	/* For pointer tables for the kernel virtual address space,
	 * use the page that is reserved in head.S that can hold up to
	 * 8 pointer tables. 3 of these tables are always reserved
	 * (kernel_pg_dir, swapper_pg_dir and kernel pointer table for
	 * the first 16 MB of RAM). In addition, the 4th pointer table
	 * in this page is reserved. On Amiga and Atari, it is used to
	 * map in the hardware registers. It may be used for other
	 * purposes on other 68k machines. This leaves 4 pointer tables
	 * available for use by the kernel. 1 of them are usually used
	 * for the vmalloc tables. This allows mapping of 3 * 32 = 96 MB
	 * of physical memory. But these pointer tables are also used
	 * for other purposes, like kernel_map(), so further pages can
	 * now be allocated.
	 */
	pmd_tablepage *page;
	pmd_table *table;
	long nr, offset = -8;
	short i;

	for (i=KPTR_PAGES-1; i>=0; i--) {
		asm volatile("bfffo %1{%2,#8},%0"
			: "=d" (nr)
			: "d" ((u_char)~kptr_pages.alloced[i]), "d" (offset));
		if (nr)
			break;
	}
	if (i < 0) {
		printk("No space for kernel pointer table!\n");
		return NULL;
	}
	if (!(page = kptr_pages.page[i])) {
		if (!(page = (pmd_tablepage *)get_free_page(GFP_KERNEL))) {
			printk("No space for kernel pointer table!\n");
			return NULL;
		}
		flush_tlb_kernel_page((unsigned long) page);
		nocache_page((u_long)(kptr_pages.page[i] = page));
	}
	asm volatile("bfset %0@{%1,#1}"
		: /* no output */
		: "a" (&kptr_pages.alloced[i]), "d" (nr-offset));
	table = &(*page)[nr-offset];
	memset(table, 0, sizeof(pmd_table));
	return ((pmd_t *)table);
}

void free_kpointer_table (pmd_t *pmdp)
{
	pmd_table *table = (pmd_table *)pmdp;
	pmd_tablepage *page = (pmd_tablepage *)((u_long)table & PAGE_MASK);
	long nr;
	short i;

	for (i=KPTR_PAGES-1; i>=0; i--) {
		if (kptr_pages.page[i] == page)
			break;
	}
	nr = ((u_long)table - (u_long)page) / sizeof(pmd_table);
	if (!table || i < 0 || (i == KPTR_PAGES-1 && nr < RESERVED_KPTR)) {
		printk("Attempt to free invalid kernel pointer table: %p\n", table);
		return;
	}
	asm volatile("bfclr %0@{%1,#1}"
		: /* no output */
		: "a" (&kptr_pages.alloced[i]), "d" (nr));
	if (!kptr_pages.alloced[i]) {
		kptr_pages.page[i] = 0;
		cache_page ((u_long)page);
		free_page ((u_long)page);
	}
}

static unsigned long transp_transl_matches( unsigned long regval,
					    unsigned long vaddr )
{
    unsigned long base, mask;

    /* enabled? */
    if (!(regval & 0x8000))
	return( 0 );

    if (CPU_IS_030) {
	/* function code match? */
	base = (regval >> 4) & 7;
	mask = ~(regval & 7);
	if ((SUPER_DATA & mask) != (base & mask))
	    return( 0 );
    }
    else {
	/* must not be user-only */
	if ((regval & 0x6000) == 0)
	    return( 0 );
    }

    /* address match? */
    base = regval & 0xff000000;
    mask = ~((regval << 8) & 0xff000000);
    return( (vaddr & mask) == (base & mask) );
}

#ifndef CONFIG_SINGLE_MEMORY_CHUNK
/*
 * The following two routines map from a physical address to a kernel
 * virtual address and vice versa.
 */
unsigned long mm_vtop (unsigned long vaddr)
{
#ifndef CONFIG_SINGLE_MEMORY_CHUNK
	int i=0;
	unsigned long voff = vaddr;
	unsigned long offset = 0;

	do{
		if (voff < offset + m68k_memory[i].size) {
#ifdef DEBUGPV
			printk ("VTOP(%lx)=%lx\n", vaddr,
				m68k_memory[i].addr + voff - offset);
#endif
			return m68k_memory[i].addr + voff - offset;
		} else
			offset += m68k_memory[i].size;
		i++;
	}while (i < m68k_num_memory);
#else
	if (vaddr < m68k_memory[0].size)
		return m68k_memory[0].addr + vaddr;
#endif

	return mm_vtop_fallback(vaddr);
}
#endif

/* Separate function to make the common case faster (needs to save less
   registers) */
unsigned long mm_vtop_fallback (unsigned long vaddr)
{
	/* not in one of the memory chunks; test for applying transparent
	 * translation */

	if (CPU_IS_030) {
	    unsigned long ttreg;
	    
	    asm volatile( ".chip 68030\n\t"
			  "pmove %/tt0,%0@\n\t"
			  ".chip 68k"
			  : : "a" (&ttreg) );
	    if (transp_transl_matches( ttreg, vaddr ))
		return vaddr;
	    asm volatile( ".chip 68030\n\t"
			  "pmove %/tt1,%0@\n\t"
			  ".chip 68k"
			  : : "a" (&ttreg) );
	    if (transp_transl_matches( ttreg, vaddr ))
		return vaddr;
	}
	else if (CPU_IS_040_OR_060) {
	    unsigned long ttreg;
	    
	    asm volatile( ".chip 68040\n\t"
			  "movec %%dtt0,%0\n\t"
			  ".chip 68k"
			  : "=d" (ttreg) );
	    if (transp_transl_matches( ttreg, vaddr ))
		return vaddr;
	    asm volatile( ".chip 68040\n\t"
			  "movec %%dtt1,%0\n\t"
			  ".chip 68k"
			  : "=d" (ttreg) );
	    if (transp_transl_matches( ttreg, vaddr ))
		return vaddr;
	}

	/* no match, too, so get the actual physical address from the MMU. */

	if (CPU_IS_060) {
	  mm_segment_t fs = get_fs();
	  unsigned long  paddr;

	  set_fs (MAKE_MM_SEG(SUPER_DATA));

	  /* The PLPAR instruction causes an access error if the translation
	   * is not possible. We don't catch that here, so a bad kernel trap
	   * will be reported in this case. */
	  asm volatile (".chip 68060\n\t"
			"plpar (%0)\n\t"
			".chip 68k"
			: "=a" (paddr)
			: "0" (vaddr));
	  set_fs (fs);

	  return paddr;

	} else if (CPU_IS_040) {
	  unsigned long mmusr;
	  mm_segment_t fs = get_fs();

	  set_fs (MAKE_MM_SEG(SUPER_DATA));

	  asm volatile (".chip 68040\n\t"
			"ptestr (%1)\n\t"
			"movec %%mmusr, %0\n\t"
			".chip 68k"
			: "=r" (mmusr)
			: "a" (vaddr));
	  set_fs (fs);

	  if (mmusr & MMU_T_040) {
	    return (vaddr);	/* Transparent translation */
	  }
	  if (mmusr & MMU_R_040)
	    return (mmusr & PAGE_MASK) | (vaddr & (PAGE_SIZE-1));

	  panic ("VTOP040: bad virtual address %08lx (%lx)", vaddr, mmusr);
	} else {
	  volatile unsigned short temp;
	  unsigned short mmusr;
	  unsigned long *descaddr;

	  asm volatile ("ptestr #5,%2@,#7,%0\n\t"
			"pmove %/psr,%1@"
			: "=a&" (descaddr)
			: "a" (&temp), "a" (vaddr));
	  mmusr = temp;

	  if (mmusr & (MMU_I|MMU_B|MMU_L))
	    panic ("VTOP030: bad virtual address %08lx (%x)", vaddr, mmusr);

	  descaddr = phys_to_virt((unsigned long)descaddr);

	  switch (mmusr & MMU_NUM) {
	  case 1:
	    return (*descaddr & 0xfe000000) | (vaddr & 0x01ffffff);
	  case 2:
	    return (*descaddr & 0xfffc0000) | (vaddr & 0x0003ffff);
	  case 3:
	    return (*descaddr & PAGE_MASK) | (vaddr & (PAGE_SIZE-1));
	  default:
	    panic ("VTOP: bad levels (%u) for virtual address %08lx", 
		   mmusr & MMU_NUM, vaddr);
	  }
	}

	panic ("VTOP: bad virtual address %08lx", vaddr);
}

#ifndef CONFIG_SINGLE_MEMORY_CHUNK
unsigned long mm_ptov (unsigned long paddr)
{
#ifndef CONFIG_SINGLE_MEMORY_CHUNK
	int i = 0;
	unsigned long offset = 0;

	do{
		if (paddr >= m68k_memory[i].addr &&
		    paddr < (m68k_memory[i].addr
			     + m68k_memory[i].size)) {
#ifdef DEBUGPV
			printk ("PTOV(%lx)=%lx\n", paddr,
				(paddr - m68k_memory[i].addr) + offset);
#endif
			return (paddr - m68k_memory[i].addr) + offset;
		} else
			offset += m68k_memory[i].size;
		i++;
	}while (i < m68k_num_memory);
#else
	unsigned long base = m68k_memory[0].addr;
	if (paddr >= base && paddr < (base + m68k_memory[0].size))
		return (paddr - base);
#endif

	/*
	 * assume that the kernel virtual address is the same as the
	 * physical address.
	 *
	 * This should be reasonable in most situations:
	 *  1) They shouldn't be dereferencing the virtual address
	 *     unless they are sure that it is valid from kernel space.
	 *  2) The only usage I see so far is converting a page table
	 *     reference to some non-FASTMEM address space when freeing
         *     mmaped "/dev/mem" pages.  These addresses are just passed
	 *     to "free_page", which ignores addresses that aren't in
	 *     the memory list anyway.
	 *
	 */

#ifdef CONFIG_AMIGA
	/*
	 * if on an amiga and address is in first 16M, move it 
	 * to the ZTWO_VADDR range
	 */
	if (MACH_IS_AMIGA && paddr < 16*1024*1024)
		return ZTWO_VADDR(paddr);
#endif
	return paddr;
}
#endif

/* invalidate page in both caches */
#define	clear040(paddr)					\
	__asm__ __volatile__ ("nop\n\t"			\
			      ".chip 68040\n\t"		\
			      "cinvp %%bc,(%0)\n\t"	\
			      ".chip 68k"		\
			      : : "a" (paddr))

/* invalidate page in i-cache */
#define	cleari040(paddr)				\
	__asm__ __volatile__ ("nop\n\t"			\
			      ".chip 68040\n\t"		\
			      "cinvp %%ic,(%0)\n\t"	\
			      ".chip 68k"		\
			      : : "a" (paddr))

/* push page in both caches */
#define	push040(paddr)					\
	__asm__ __volatile__ ("nop\n\t"			\
			      ".chip 68040\n\t"		\
			      "cpushp %%bc,(%0)\n\t"	\
			      ".chip 68k"		\
			      : : "a" (paddr))

/* push and invalidate page in both caches */
#define	pushcl040(paddr)			\
	do { push040(paddr);			\
	     if (CPU_IS_060) clear040(paddr);	\
	} while(0)

/* push page in both caches, invalidate in i-cache */
#define	pushcli040(paddr)			\
	do { push040(paddr);			\
	     if (CPU_IS_060) cleari040(paddr);	\
	} while(0)


/*
 * 040: Hit every page containing an address in the range paddr..paddr+len-1.
 * (Low order bits of the ea of a CINVP/CPUSHP are "don't care"s).
 * Hit every page until there is a page or less to go. Hit the next page,
 * and the one after that if the range hits it.
 */
/* ++roman: A little bit more care is required here: The CINVP instruction
 * invalidates cache entries WITHOUT WRITING DIRTY DATA BACK! So the beginning
 * and the end of the region must be treated differently if they are not
 * exactly at the beginning or end of a page boundary. Else, maybe too much
 * data becomes invalidated and thus lost forever. CPUSHP does what we need:
 * it invalidates the page after pushing dirty data to memory. (Thanks to Jes
 * for discovering the problem!)
 */
/* ... but on the '060, CPUSH doesn't invalidate (for us, since we have set
 * the DPI bit in the CACR; would it cause problems with temporarily changing
 * this?). So we have to push first and then additionally to invalidate.
 */

#ifdef CONFIG_M68K_L2_CACHE
/*
 *	Jes was worried about performance (urhh ???) so its optional
 */
 
extern void (*mach_l2_flush)(int) = NULL;
#endif
 
/*
 * cache_clear() semantics: Clear any cache entries for the area in question,
 * without writing back dirty entries first. This is useful if the data will
 * be overwritten anyway, e.g. by DMA to memory. The range is defined by a
 * _physical_ address.
 */

void cache_clear (unsigned long paddr, int len)
{
    if (CPU_IS_040_OR_060) {
	int tmp;

	/*
	 * We need special treatment for the first page, in case it
	 * is not page-aligned. Page align the addresses to work
	 * around bug I17 in the 68060.
	 */
	if ((tmp = -paddr & (PAGE_SIZE - 1))) {
	    pushcl040(paddr & PAGE_MASK);
	    if ((len -= tmp) <= 0)
		return;
	    paddr += tmp;
	}
	tmp = PAGE_SIZE;
	paddr &= PAGE_MASK;
	while ((len -= tmp) >= 0) {
	    clear040(paddr);
	    paddr += tmp;
	}
	if ((len += tmp))
	    /* a page boundary gets crossed at the end */
	    pushcl040(paddr);
    }
    else /* 68030 or 68020 */
	asm volatile ("movec %/cacr,%/d0\n\t"
		      "oriw %0,%/d0\n\t"
		      "movec %/d0,%/cacr"
		      : : "i" (FLUSH_I_AND_D)
		      : "d0");
#ifdef CONFIG_M68K_L2_CACHE
    if(mach_l2_flush)
    	mach_l2_flush(0);
#endif
}


/*
 * cache_push() semantics: Write back any dirty cache data in the given area,
 * and invalidate the range in the instruction cache. It needs not (but may)
 * invalidate those entries also in the data cache. The range is defined by a
 * _physical_ address.
 */

void cache_push (unsigned long paddr, int len)
{
    if (CPU_IS_040_OR_060) {
	int tmp = PAGE_SIZE;

	/*
         * on 68040 or 68060, push cache lines for pages in the range;
	 * on the '040 this also invalidates the pushed lines, but not on
	 * the '060!
	 */
	len += paddr & (PAGE_SIZE - 1);

	/*
	 * Work around bug I17 in the 68060 affecting some instruction
	 * lines not being invalidated properly.
	 */
	paddr &= PAGE_MASK;

	do {
	    pushcli040(paddr);
	    paddr += tmp;
	} while ((len -= tmp) > 0);
    }
    /*
     * 68030/68020 have no writeback cache. On the other hand,
     * cache_push is actually a superset of cache_clear (the lines
     * get written back and invalidated), so we should make sure
     * to perform the corresponding actions. After all, this is getting
     * called in places where we've just loaded code, or whatever, so
     * flushing the icache is appropriate; flushing the dcache shouldn't
     * be required.
     */
    else /* 68030 or 68020 */
	asm volatile ("movec %/cacr,%/d0\n\t"
		      "oriw %0,%/d0\n\t"
		      "movec %/d0,%/cacr"
		      : : "i" (FLUSH_I)
		      : "d0");
#ifdef CONFIG_M68K_L2_CACHE
    if(mach_l2_flush)
    	mach_l2_flush(1);
#endif
}


#undef clear040
#undef cleari040
#undef push040
#undef pushcl040
#undef pushcli040

#ifndef CONFIG_SINGLE_MEMORY_CHUNK
int mm_end_of_chunk (unsigned long addr, int len)
{
	int i;

	for (i = 0; i < m68k_num_memory; i++)
		if (m68k_memory[i].addr + m68k_memory[i].size == addr + len)
			return 1;
	return 0;
}
#endif
