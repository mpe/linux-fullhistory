/*
 *  linux/arch/m68k/mm/memory.c
 *
 *  Copyright (C) 1995  Hamish Macdonald
 */

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
#include <asm/amigahw.h>

extern pte_t *kernel_page_table (unsigned long *memavailp);

/* Strings for `extern inline' functions in <asm/pgtable.h>.  If put
   directly into these functions, they are output for every file that
   includes pgtable.h */

const char PgtabStr_bad_pmd[] = "Bad pmd in pte_alloc: %08lx\n";
const char PgtabStr_bad_pgd[] = "Bad pgd in pmd_alloc: %08lx\n";
const char PgtabStr_bad_pmdk[] = "Bad pmd in pte_alloc_kernel: %08lx\n";
const char PgtabStr_bad_pgdk[] = "Bad pgd in pmd_alloc_kernel: %08lx\n";

static struct ptable_desc {
	struct ptable_desc *prev;
	struct ptable_desc *next;
	unsigned long	   page;
	unsigned char	   alloced;
} ptable_list = { &ptable_list, &ptable_list, 0, 0xff };

#define PD_NONEFREE(dp) ((dp)->alloced == 0xff)
#define PD_ALLFREE(dp) ((dp)->alloced == 0)
#define PD_TABLEFREE(dp,i) (!((dp)->alloced & (1<<(i))))
#define PD_MARKUSED(dp,i) ((dp)->alloced |= (1<<(i)))
#define PD_MARKFREE(dp,i) ((dp)->alloced &= ~(1<<(i)))

#define PTABLE_SIZE (PTRS_PER_PMD * sizeof(pmd_t))

pmd_t *get_pointer_table (void)
{
	pmd_t *pmdp = NULL;
	unsigned long flags;
	struct ptable_desc *dp = ptable_list.next;
	int i;

	/*
	 * For a pointer table for a user process address space, a
	 * table is taken from a page allocated for the purpose.  Each
	 * page can hold 8 pointer tables.  The page is remapped in
	 * virtual address space to be noncacheable.
	 */
	if (PD_NONEFREE (dp)) {

		if (!(dp = kmalloc (sizeof(struct ptable_desc),GFP_KERNEL))) {
			return 0;
		}

		if (!(dp->page = __get_free_page (GFP_KERNEL))) {
			kfree (dp);
			return 0;
		}

		nocache_page (dp->page);

		dp->alloced = 0;
		/* put at head of list */
		save_flags(flags);
		cli();
		dp->next = ptable_list.next;
		dp->prev = ptable_list.next->prev;
		ptable_list.next->prev = dp;
		ptable_list.next = dp;
		restore_flags(flags);
	}

	for (i = 0; i < 8; i++)
		if (PD_TABLEFREE (dp, i)) {
			PD_MARKUSED (dp, i);
			pmdp = (pmd_t *)(dp->page + PTABLE_SIZE*i);
			break;
		}

	if (PD_NONEFREE (dp)) {
		/* move to end of list */
		save_flags(flags);
		cli();
		dp->prev->next = dp->next;
		dp->next->prev = dp->prev;

		dp->next = ptable_list.next->prev;
		dp->prev = ptable_list.prev;
		ptable_list.prev->next = dp;
		ptable_list.prev = dp;
		restore_flags(flags);
	}

	memset (pmdp, 0, PTABLE_SIZE);

	return pmdp;
}

void free_pointer_table (pmd_t *ptable)
{
	struct ptable_desc *dp;
	unsigned long page = (unsigned long)ptable & PAGE_MASK;
	int index = ((unsigned long)ptable - page)/PTABLE_SIZE;
	unsigned long flags;

	for (dp = ptable_list.next; dp->page && dp->page != page; dp = dp->next)
		;

	if (!dp->page)
		panic ("unable to find desc for ptable %p on list!", ptable);

	if (PD_TABLEFREE (dp, index))
		panic ("table already free!");

	PD_MARKFREE (dp, index);

	if (PD_ALLFREE (dp)) {
		/* all tables in page are free, free page */
		save_flags(flags);
		cli();
		dp->prev->next = dp->next;
		dp->next->prev = dp->prev;
		restore_flags(flags);
		cache_page (dp->page);
		free_page (dp->page);
		kfree (dp);
		return;
	} else {
		/*
		 * move this descriptor the the front of the list, since
		 * it has one or more free tables.
		 */
		save_flags(flags);
		cli();
		dp->prev->next = dp->next;
		dp->next->prev = dp->prev;

		dp->next = ptable_list.next;
		dp->prev = ptable_list.next->prev;
		ptable_list.next->prev = dp;
		ptable_list.next = dp;
		restore_flags(flags);
	}
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
		if (!(page = (pmd_tablepage *)__get_free_page(GFP_KERNEL))) {
			printk("No space for kernel pointer table!\n");
			return NULL;
		}
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

/*
 * The following two routines map from a physical address to a kernel
 * virtual address and vice versa.
 */
unsigned long mm_vtop (unsigned long vaddr)
{
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

	/* not in one of the memory chunks; test for applying transparent
	 * translation */

	if (CPU_IS_030) {
	    unsigned long ttreg;
	    
	    asm volatile( "pmove %/tt0,%0@" : : "a" (&ttreg) );
	    if (transp_transl_matches( ttreg, vaddr ))
		return vaddr;
	    asm volatile( "pmove %/tt1,%0@" : : "a" (&ttreg) );
	    if (transp_transl_matches( ttreg, vaddr ))
		return vaddr;
	}
	else {
	    register unsigned long ttreg __asm__( "d0" );
	    
	    asm volatile( ".long 0x4e7a0006" /* movec %dtt0,%d0 */
			  : "=d" (ttreg) );
	    if (transp_transl_matches( ttreg, vaddr ))
		return vaddr;
	    asm volatile( ".long 0x4e7a0007" /* movec %dtt1,%d0 */
			  : "=d" (ttreg) );
	    if (transp_transl_matches( ttreg, vaddr ))
		return vaddr;
	}

	/* no match, too, so get the actual physical address from the MMU. */

	if (CPU_IS_060) {
	  unsigned long fs = get_fs();
	  unsigned long  paddr;

	  set_fs (SUPER_DATA);

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
	  unsigned long fs = get_fs();

	  set_fs (SUPER_DATA);

	  asm volatile (".chip 68040\n\t"
			"ptestr (%1)\n\t"
			"movec %%mmusr, %0\n\t"
			".chip 68k"
			: "=r" (mmusr)
			: "a" (vaddr));
	  set_fs (fs);

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

	  descaddr = (unsigned long *)PTOV(descaddr);

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

unsigned long mm_ptov (unsigned long paddr)
{
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

	/*
	 * if on an amiga and address is in first 16M, move it 
	 * to the ZTWO_ADDR range
	 */
	if (MACH_IS_AMIGA && paddr < 16*1024*1024)
		return ZTWO_VADDR(paddr);
	return paddr;
}

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

/* push page defined by virtual address in both caches */
#define	pushv040(vaddr)						\
	do { unsigned long _tmp1, _tmp2;			\
	__asm__ __volatile__ ("nop\n\t"				\
			      ".chip 68040\n\t"			\
			      "ptestr (%2)\n\t"			\
			      "movec %%mmusr,%0\n\t"		\
			      "andw #0xf000,%0\n\t"		\
			      "movel %0,%1\n\t"			\
			      "nop\n\t"				\
			      "cpushp %%bc,(%1)\n\t"		\
			      ".chip 68k"			\
			      : "=d" (_tmp1), "=a" (_tmp2)	\
			      : "a" (vaddr));			\
	} while (0)

/* push page defined by virtual address in both caches */
#define	pushv060(vaddr)					\
	do { unsigned long _tmp;			\
	__asm__ __volatile__ (".chip 68060\n\t"		\
			      "plpar (%0)\n\t"		\
			      "cpushp %%bc,(%0)\n\t"	\
			      ".chip 68k"		\
			      : "=a" (_tmp)		\
			      : "0" (vaddr));		\
	} while (0)


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

/*
 * cache_clear() semantics: Clear any cache entries for the area in question,
 * without writing back dirty entries first. This is useful if the data will
 * be overwritten anyway, e.g. by DMA to memory. The range is defined by a
 * _physical_ address.
 */

void cache_clear (unsigned long paddr, int len)
{
    if (CPU_IS_040_OR_060) {
	/*
	 * cwe need special treatment for the first page, in case it
	 * is not page-aligned.
	 */
	if (paddr & (PAGE_SIZE - 1)){
	    pushcl040(paddr);
	    if (len <= PAGE_SIZE){
		if (((paddr + len - 1) ^ paddr) & PAGE_MASK) {
		    pushcl040(paddr + len - 1);
		}
		return;
	    }else{
		len -=PAGE_SIZE;
		paddr += PAGE_SIZE;
	    }
	}
		
	while (len > PAGE_SIZE) {
#if 0
	    pushcl040(paddr);
#else
	    clear040(paddr);
#endif
	    len -= PAGE_SIZE;
	    paddr += PAGE_SIZE;
	}
	if (len > 0) {
	    pushcl040(paddr);
	    if (((paddr + len - 1) ^ paddr) & PAGE_MASK) {
		/* a page boundary gets crossed at the end */
		pushcl040(paddr + len - 1);
	    }
	}
    }
    else /* 68030 or 68020 */
	asm volatile ("movec %/cacr,%/d0\n\t"
		      "oriw %0,%/d0\n\t"
		      "movec %/d0,%/cacr"
		      : : "i" (FLUSH_I_AND_D)
		      : "d0");
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
	/*
         * on 68040 or 68060, push cache lines for pages in the range;
	 * on the '040 this also invalidates the pushed lines, but not on
	 * the '060!
	 */
	while (len > PAGE_SIZE) {
	    pushcli040(paddr);
	    len -= PAGE_SIZE;
	    paddr += PAGE_SIZE;
	    }
	if (len > 0) {
	    pushcli040(paddr);
	    if (((paddr + len - 1) ^ paddr) & PAGE_MASK) {
		/* a page boundary gets crossed at the end */
		pushcli040(paddr + len - 1);
		}
	    }
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
}


/*
 * cache_push_v() semantics: Write back any dirty cache data in the given
 * area, and invalidate those entries at least in the instruction cache. This
 * is intended to be used after data has been written that can be executed as
 * code later. The range is defined by a _user_mode_ _virtual_ address  (or,
 * more exactly, the space is defined by the %sfc/%dfc register.)
 */

void cache_push_v (unsigned long vaddr, int len)
{
    if (CPU_IS_040) {
	/* on 68040, push cache lines for pages in the range */
	while (len > PAGE_SIZE) {
	    pushv040(vaddr);
	    len -= PAGE_SIZE;
	    vaddr += PAGE_SIZE;
	    }
	if (len > 0) {
	    pushv040(vaddr);
	    if (((vaddr + len - 1) ^ vaddr) & PAGE_MASK) {
		/* a page boundary gets crossed at the end */
		pushv040(vaddr + len - 1);
		}
	    }
	}
    else if (CPU_IS_060) {
	/* on 68040, push cache lines for pages in the range */
	while (len > PAGE_SIZE) {
	    pushv060(vaddr);
	    len -= PAGE_SIZE;
	    vaddr += PAGE_SIZE;
	}
	if (len > 0) {
	    pushv060(vaddr);
	    if (((vaddr + len - 1) ^ vaddr) & PAGE_MASK) {
		/* a page boundary gets crossed at the end */
		pushv060(vaddr + len - 1);
	    }
	}
    }
    /* 68030/68020 have no writeback cache; still need to clear icache. */
    else /* 68030 or 68020 */
	asm volatile ("movec %/cacr,%/d0\n\t"
		      "oriw %0,%/d0\n\t"
		      "movec %/d0,%/cacr"
		      : : "i" (FLUSH_I)
		      : "d0");
}

#undef clear040
#undef cleari040
#undef push040
#undef pushcl040
#undef pushcli040
#undef pushv040
#undef pushv060

unsigned long mm_phys_to_virt (unsigned long addr)
{
    return PTOV (addr);
}

int mm_end_of_chunk (unsigned long addr, int len)
{
	int i;

	for (i = 0; i < m68k_num_memory; i++)
		if (m68k_memory[i].addr + m68k_memory[i].size == addr + len)
			return 1;
	return 0;
}

/* Map some physical address range into the kernel address space. The
 * code is copied and adapted from map_chunk().
 */

unsigned long kernel_map(unsigned long paddr, unsigned long size,
			 int nocacheflag, unsigned long *memavailp )
{
#define STEP_SIZE	(256*1024)

	static unsigned long vaddr = 0xe0000000; /* safe place */
	unsigned long physaddr, retaddr;
	pte_t *ktablep = NULL;
	pmd_t *kpointerp;
	pgd_t *page_dir;
	int pindex;   /* index into pointer table */
	int prot;
	
	/* Round down 'paddr' to 256 KB and adjust size */
	physaddr = paddr & ~(STEP_SIZE-1);
	size += paddr - physaddr;
	retaddr = vaddr + (paddr - physaddr);
	paddr = physaddr;
	/* Round up the size to 256 KB. It doesn't hurt if too much is
	 * mapped... */
	size = (size + STEP_SIZE - 1) & ~(STEP_SIZE-1);

	if (CPU_IS_040_OR_060) {
		prot = _PAGE_PRESENT | _PAGE_GLOBAL040;
		switch( nocacheflag ) {
		  case KERNELMAP_FULL_CACHING:
			prot |= _PAGE_CACHE040;
			break;
		  case KERNELMAP_NOCACHE_SER:
		  default:
			prot |= _PAGE_NOCACHE_S;
			break;
		  case KERNELMAP_NOCACHE_NONSER:
			prot |= _PAGE_NOCACHE;
			break;
		  case KERNELMAP_NO_COPYBACK:
			prot |= _PAGE_CACHE040W;
			/* prot |= 0; */
			break;
		}
	} else
		prot = _PAGE_PRESENT |
			   ((nocacheflag == KERNELMAP_FULL_CACHING ||
				 nocacheflag == KERNELMAP_NO_COPYBACK) ? 0 : _PAGE_NOCACHE030);
	
	page_dir = pgd_offset_k(vaddr);
	if (pgd_present(*page_dir)) {
		kpointerp = (pmd_t *)pgd_page(*page_dir);
		pindex = (vaddr >> 18) & 0x7f;
		if (pindex != 0 && CPU_IS_040_OR_060) {
			if (pmd_present(*kpointerp))
				ktablep = (pte_t *)pmd_page(*kpointerp);
			else {
				ktablep = kernel_page_table (memavailp);
				/* Make entries invalid */
				memset( ktablep, 0, sizeof(long)*PTRS_PER_PTE);
				pmd_set(kpointerp,ktablep);
			}
			ktablep += (pindex & 15)*64;
		}
	}
	else {
		/* we need a new pointer table */
		kpointerp = get_kpointer_table ();
		pgd_set(page_dir, (pmd_t *)kpointerp);
		memset( kpointerp, 0, PTRS_PER_PMD*sizeof(pmd_t));
		pindex = 0;
	}

	for (physaddr = paddr; physaddr < paddr + size; vaddr += STEP_SIZE) {

		if (pindex > 127) {
			/* we need a new pointer table */
			kpointerp = get_kpointer_table ();
			pgd_set(pgd_offset_k(vaddr), (pmd_t *)kpointerp);
			memset( kpointerp, 0, PTRS_PER_PMD*sizeof(pmd_t));
			pindex = 0;
		}

		if (CPU_IS_040_OR_060) {
			int i;
			unsigned long ktable;

			/*
			 * 68040, use page tables pointed to by the
			 * kernel pointer table.
			 */

			if ((pindex & 15) == 0) {
				/* Need new page table every 4M on the '040 */
				ktablep = kernel_page_table (memavailp);
				/* Make entries invalid */
				memset( ktablep, 0, sizeof(long)*PTRS_PER_PTE);
			}

			ktable = VTOP(ktablep);

			/*
			 * initialize section of the page table mapping
			 * this 1M portion.
			 */
			for (i = 0; i < 64; i++) {
				pte_val(*ktablep++) = physaddr | prot;
				physaddr += PAGE_SIZE;
			}

			/*
			 * make the kernel pointer table point to the
			 * kernel page table.
			 */

			((unsigned long *)kpointerp)[pindex++] = ktable | _PAGE_TABLE;

		} else {
			/*
			 * 68030, use early termination page descriptors.
			 * Each one points to 64 pages (256K).
			 */
			((unsigned long *)kpointerp)[pindex++] = physaddr | prot;
			physaddr += 64 * PAGE_SIZE;
		}
	}

	return( retaddr );
}


static inline void set_cmode_pte( pmd_t *pmd, unsigned long address,
				  unsigned long size, unsigned cmode )
{	pte_t *pte;
	unsigned long end;

	if (pmd_none(*pmd))
		return;

	pte = pte_offset( pmd, address );
	address &= ~PMD_MASK;
	end = address + size;
	if (end >= PMD_SIZE)
		end = PMD_SIZE;

	for( ; address < end; pte++ ) {
		pte_val(*pte) = (pte_val(*pte) & ~_PAGE_NOCACHE) | cmode;
		address += PAGE_SIZE;
	}
}


static inline void set_cmode_pmd( pgd_t *dir, unsigned long address,
				  unsigned long size, unsigned cmode )
{
	pmd_t *pmd;
	unsigned long end;

	if (pgd_none(*dir))
		return;

	pmd = pmd_offset( dir, address );
	address &= ~PGDIR_MASK;
	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;

	if ((pmd_val(*pmd) & _DESCTYPE_MASK) == _PAGE_PRESENT) {
		/* 68030 early termination descriptor */
		pmd_val(*pmd) = (pmd_val(*pmd) & ~_PAGE_NOCACHE) | cmode;
		return;
	}
	else {
		/* "normal" tables */
		for( ; address < end; pmd++ ) {
			set_cmode_pte( pmd, address, end - address, cmode );
			address = (address + PMD_SIZE) & PMD_MASK;
		}
	}
}


/*
 * Set new cache mode for some kernel address space.
 * The caller must push data for that range itself, if such data may already
 * be in the cache.
 */

void kernel_set_cachemode( unsigned long address, unsigned long size,
						   unsigned cmode )
{
	pgd_t *dir = pgd_offset_k( address );
	unsigned long end = address + size;
	
	if (CPU_IS_040_OR_060) {
		switch( cmode ) {
		  case KERNELMAP_FULL_CACHING:
			cmode = _PAGE_CACHE040;
			break;
		  case KERNELMAP_NOCACHE_SER:
		  default:
			cmode = _PAGE_NOCACHE_S;
			break;
		  case KERNELMAP_NOCACHE_NONSER:
			cmode = _PAGE_NOCACHE;
			break;
		  case KERNELMAP_NO_COPYBACK:
			cmode = _PAGE_CACHE040W;
			break;
		}
	} else
		cmode = ((cmode == KERNELMAP_FULL_CACHING ||
				  cmode == KERNELMAP_NO_COPYBACK)    ?
			 0 : _PAGE_NOCACHE030);

	for( ; address < end; dir++ ) {
		set_cmode_pmd( dir, address, end - address, cmode );
		address = (address + PGDIR_SIZE) & PGDIR_MASK;
	}
	flush_tlb_all();
}


