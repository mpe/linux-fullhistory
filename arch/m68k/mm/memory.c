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

#include <asm/segment.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/traps.h>
#include <asm/amigahw.h>
#include <asm/bootinfo.h>

extern pte_t *kernel_page_table (unsigned long *memavailp);

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

static unsigned char alloced = 0;
extern pmd_t (*kernel_pmd_table)[PTRS_PER_PMD]; /* initialized in head.S */

pmd_t *get_kpointer_table (void)
{
	/* For pointer tables for the kernel virtual address space,
	 * use a page that is allocated in head.S that can hold up to
	 * 8 pointer tables.  This allows mapping of 8 * 32M = 256M of
	 * physical memory.  This should be sufficient for now.
	 */
	pmd_t *ptable;
	int i;

	for (i = 0; i < PAGE_SIZE/(PTRS_PER_PMD*sizeof(pmd_t)); i++)
		if ((alloced & (1 << i)) == 0) {
			ptable = kernel_pmd_table[i];
			memset (ptable, 0, PTRS_PER_PMD*sizeof(pmd_t));
			alloced |= (1 << i);
			return ptable;
		}
	printk ("no space for kernel pointer table\n");
	return NULL;
}

void free_kpointer_table (pmd_t *pmdp)
{
	int index = (pmd_t (*)[PTRS_PER_PMD])pmdp - kernel_pmd_table;

	if (index < 0 || index > 7 ||
	    /* This works because kernel_pmd_table is page aligned. */
	    ((unsigned long)pmdp & (sizeof(pmd_t) * PTRS_PER_PMD - 1)))
		panic("attempt to free invalid kernel pointer table");
	else
		alloced &= ~(1 << index);
}

/*
 * The following two routines map from a physical address to a kernel
 * virtual address and vice versa.
 */
unsigned long mm_vtop (unsigned long vaddr)
{
	int i;
	unsigned long voff = vaddr;
	unsigned long offset = 0;

	for (i = 0; i < boot_info.num_memory; i++)
	{
		if (voff < offset + boot_info.memory[i].size) {
#ifdef DEBUGPV
			printk ("VTOP(%lx)=%lx\n", vaddr,
				boot_info.memory[i].addr + voff - offset);
#endif
			return boot_info.memory[i].addr + voff - offset;
		} else
			offset += boot_info.memory[i].size;
	}

	/* not in one of the memory chunks; get the actual
	 * physical address from the MMU.
	 */
	if (m68k_is040or060 == 6) {
	  unsigned long fs = get_fs();
	  unsigned long  paddr;

	  set_fs (SUPER_DATA);

	  /* The PLPAR instruction causes an access error if the translation
	   * is not possible. We don't catch that here, so a bad kernel trap
	   * will be reported in this case. */
	  asm volatile ("movel %1,%/a0\n\t"
			".word 0xf5c8\n\t"	/* plpar (a0) */
			"movel %/a0,%0"
			: "=g" (paddr)
			: "g" (vaddr)
			: "a0" );
	  set_fs (fs);

	  return paddr;

	} else if (m68k_is040or060 == 4) {
	  unsigned long mmusr;
	  unsigned long fs = get_fs();

	  set_fs (SUPER_DATA);

	  asm volatile ("movel %1,%/a0\n\t"
			".word 0xf568\n\t"	/* ptestr (a0) */
			".long 0x4e7a8805\n\t"	/* movec mmusr, a0 */
			"movel %/a0,%0"
			: "=g" (mmusr)
			: "g" (vaddr)
			: "a0", "d0");
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
	int i;
	unsigned long offset = 0;

	for (i = 0; i < boot_info.num_memory; i++)
	{
		if (paddr >= boot_info.memory[i].addr &&
		    paddr < (boot_info.memory[i].addr
			     + boot_info.memory[i].size)) {
#ifdef DEBUGPV
			printk ("PTOV(%lx)=%lx\n", paddr,
				(paddr - boot_info.memory[i].addr) + offset);
#endif
			return (paddr - boot_info.memory[i].addr) + offset;
		} else
			offset += boot_info.memory[i].size;
	}

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

#define	clear040(paddr) __asm__ __volatile__ ("movel %0,%/a0\n\t"\
					      ".word 0xf4d0"\
					      /* CINVP I/D (a0) */\
					      : : "g" ((paddr))\
					      : "a0")

#define	push040(paddr) __asm__ __volatile__ ("movel %0,%/a0\n\t"\
					     ".word 0xf4f0"\
					     /* CPUSHP I/D (a0) */\
					     : : "g" ((paddr))\
					     : "a0")

#define	pushcl040(paddr) do { push040((paddr));\
			      if (m68k_is040or060 == 6) clear040((paddr));\
			 } while(0)

#define	pushv040(vaddr) __asm__ __volatile__ ("movel %0,%/a0\n\t"\
					      /* ptestr (a0) */\
					      ".word 0xf568\n\t"\
					      /* movec mmusr,d0 */\
					      ".long 0x4e7a0805\n\t"\
					      "andw #0xf000,%/d0\n\t"\
					      "movel %/d0,%/a0\n\t"\
					      /* CPUSHP I/D (a0) */\
					      ".word 0xf4f0"\
					      : : "g" ((vaddr))\
					      : "a0", "d0")

#define	pushv060(vaddr) __asm__ __volatile__ ("movel %0,%/a0\n\t"\
					      /* plpar (a0) */\
					      ".word 0xf5c8\n\t"\
					      /* CPUSHP I/D (a0) */\
					      ".word 0xf4f0"\
					      : : "g" ((vaddr))\
					      : "a0")


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
void cache_clear (unsigned long paddr, int len)
{
    if (m68k_is040or060) {
	/* ++roman: There have been too many problems with the CINV, it seems
	 * to break the cache maintenance of DMAing drivers. I don't expect
	 * too much overhead by using CPUSH instead.
	 */
	while (len > PAGE_SIZE) {
	    pushcl040(paddr);
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
#if 0
	/* on 68040, invalidate cache lines for pages in the range */
	while (len > PAGE_SIZE) {
	    clear040(paddr);
	    len -= PAGE_SIZE;
	    paddr += PAGE_SIZE;
	    }
	if (len > 0) {
	    /* 0 < len <= PAGE_SIZE */
	    clear040(paddr);
	    if (((paddr + len - 1) / PAGE_SIZE) != (paddr / PAGE_SIZE)) {
		/* a page boundary gets crossed at the end */
		clear040(paddr + len - 1);
		}
	    }
#endif
    else /* 68030 or 68020 */
	asm volatile ("movec %/cacr,%/d0\n\t"
		      "oriw %0,%/d0\n\t"
		      "movec %/d0,%/cacr"
		      : : "i" (FLUSH_I_AND_D)
		      : "d0");
}



void cache_push (unsigned long paddr, int len)
{
    if (m68k_is040or060) {
	/*
         * on 68040 or 68060, push cache lines for pages in the range;
	 * on the '040 this also invalidates the pushed lines, but not on
	 * the '060!
	 */
	while (len > PAGE_SIZE) {
	    push040(paddr);
	    len -= PAGE_SIZE;
	    paddr += PAGE_SIZE;
	    }
	if (len > 0) {
	    push040(paddr);
#if 0
	    if (((paddr + len - 1) / PAGE_SIZE) != (paddr / PAGE_SIZE)) {
#endif
	    if (((paddr + len - 1) ^ paddr) & PAGE_MASK) {
		/* a page boundary gets crossed at the end */
		push040(paddr + len - 1);
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

void cache_push_v (unsigned long vaddr, int len)
{
    if (m68k_is040or060 == 4) {
	/* on 68040, push cache lines for pages in the range */
	while (len > PAGE_SIZE) {
	    pushv040(vaddr);
	    len -= PAGE_SIZE;
	    vaddr += PAGE_SIZE;
	    }
	if (len > 0) {
	    pushv040(vaddr);
#if 0
	    if (((vaddr + len - 1) / PAGE_SIZE) != (vaddr / PAGE_SIZE)) {
#endif
	    if (((vaddr + len - 1) ^ vaddr) & PAGE_MASK) {
		/* a page boundary gets crossed at the end */
		pushv040(vaddr + len - 1);
		}
	    }
	}
    else if (m68k_is040or060 == 6) {
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
#if 1
void flush_cache_all(void)
{
    if (m68k_is040or060 >= 4)
        __asm__ __volatile__ (".word 0xf478\n" ::);
    else /* 68030 or 68020 */
	asm volatile ("movec %/cacr,%/d0\n\t"
		      "oriw %0,%/d0\n\t"
		      "movec %/d0,%/cacr"
		      : : "i" (FLUSH_I_AND_D)
		      : "d0");
}

void flush_cache_mm(struct mm_struct *mm){

    if (mm == current->mm)
        flush_cache_all();
}

void flush_cache_range(struct mm_struct *mm, unsigned long start,
		       unsigned long end){
    if (mm == current->mm)
        cache_push_v(start, end-start);
}

void flush_cache_page (struct vm_area_struct *vma, unsigned long vaddr)
{
    if (m68k_is040or060 >= 4)
        pushv040(vaddr); /*
			  * the 040 always invalidates the I-cache when
			  * pushing its contents to ram.
			  */

    /* 68030/68020 have no writeback cache; still need to clear icache. */
    else /* 68030 or 68020 */
        asm volatile ("movec %/cacr,%/d0\n\t"
		      "oriw %0,%/d0\n\t"
		      "movec %/d0,%/cacr"
		      : : "i" (FLUSH_I_AND_D)
		      : "d0");
}

void flush_page_to_ram (unsigned long vaddr)
{
    if (m68k_is040or060 >= 4)
        pushcl040(VTOP(vaddr));

    /* 68030/68020 have no writeback cache; still need to clear icache. */
    else /* 68030 or 68020 */
	asm volatile ("movec %/cacr,%/d0\n\t"
		      "oriw %0,%/d0\n\t"
		      "movec %/d0,%/cacr"
		      : : "i" (FLUSH_I_AND_D)
		      : "d0");
}
#endif

#undef clear040
#undef push040
#undef pushv040
#undef pushv060

unsigned long mm_phys_to_virt (unsigned long addr)
{
    return PTOV (addr);
}

int mm_end_of_chunk (unsigned long addr, int len)
{
	int i;

	for (i = 0; i < boot_info.num_memory; i++)
		if (boot_info.memory[i].addr + boot_info.memory[i].size
		    == addr + len)
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

	if (m68k_is040or060) {
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
		if (pindex != 0 && m68k_is040or060) {
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

		if (m68k_is040or060) {
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
	
	if (m68k_is040or060) {
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


