/* sun4c.c:  Sun4C specific mm routines.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

/* The SUN4C has an MMU based upon a Translation Lookaside Buffer scheme
 * where only so many translations can be loaded at once.  As Linus said
 * in Boston, this is a broken way of doing things.
 *
 * NOTE:  Free page pool and tables now live in high memory, see
 *        asm-sparc/pgtsun4c.c and asm-sparc/page.h for details.
 */

#include <linux/kernel.h>  /* for printk */
#include <linux/sched.h>

#include <asm/processor.h> /* for wp_works_ok */
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/vac-ops.h>
#include <asm/vaddrs.h>
#include <asm/asi.h>
#include <asm/contregs.h>
#include <asm/kdebug.h>

unsigned int sun4c_pmd_align(unsigned int addr) { return SUN4C_PMD_ALIGN(addr); }
unsigned int sun4c_pgdir_align(unsigned int addr) { return SUN4C_PGDIR_ALIGN(addr); }

extern int num_segmaps, num_contexts;

/* Idea taken from Hamish McDonald's MC680x0 Linux code, nice job.
 * The only function that actually uses this is sun4c_mk_pte() and
 * to have a complete physical ram structure walk happen for each
 * invocation is quite costly.  However, this does do some nice
 * sanity checking and we'll see when our maps don't match.  Eventually
 * when I trust my code I will just do a direct mmu probe in mk_pte().
 */
static inline unsigned int sun4c_virt_to_phys(unsigned int vaddr)
{
	unsigned int paddr = 0;
	unsigned int voff = (vaddr - PAGE_OFFSET);
	int i;

	for(i=0; sp_banks[i].num_bytes != 0; i++) {
		if(voff < paddr + sp_banks[i].num_bytes) {
			/* This matches. */
			return sp_banks[i].base_addr + voff - paddr;
		} else
			paddr += sp_banks[i].num_bytes;
	}
	/* Shit, gotta consult the MMU, this shouldn't happen... */
	printk("sun4c_virt_to_phys: Could not make translation for vaddr %08lx\n", (unsigned long) vaddr);
	SP_ENTER_DEBUGGER;
}		

static inline unsigned long
sun4c_phys_to_virt(unsigned long paddr)
{
        int i;
        unsigned long offset = PAGE_OFFSET;

        for (i=0; sp_banks[i].num_bytes != 0; i++)
        {
                if (paddr >= sp_banks[i].base_addr &&
                    paddr < (sp_banks[i].base_addr
                             + sp_banks[i].num_bytes)) {
                        return (paddr - sp_banks[i].base_addr) + offset;
                } else
                        offset += sp_banks[i].num_bytes;
        }
	printk("sun4c_phys_to_virt: Could not make translation for paddr %08lx\n", (unsigned long) paddr);
	SP_ENTER_DEBUGGER;
}

unsigned long
sun4c_vmalloc_start(void)
{
	return ((high_memory + SUN4C_VMALLOC_OFFSET) & ~(SUN4C_VMALLOC_OFFSET-1));
}

/* Note that I have 16 page tables per page, thus four less
 * bits of shifting than normal.
 */

unsigned long
sun4c_pte_page(pte_t pte)
{
	unsigned long page;

	page = ((pte_val(pte) & _SUN4C_PFN_MASK) << (PAGE_SHIFT));
	return sun4c_phys_to_virt(page);
}

unsigned long 
sun4c_pmd_page(pmd_t pmd)
{
	return ((pmd_val(pmd) & _SUN4C_PGD_PFN_MASK) << (_SUN4C_PGD_PAGE_SHIFT));
}

unsigned long
sun4c_pgd_page(pgd_t pgd)
{
	return ((pgd_val(pgd) & _SUN4C_PGD_PFN_MASK) << (_SUN4C_PGD_PAGE_SHIFT));
}

/* Update the root mmu directory on the sun4c mmu. */
void
sun4c_update_rootmmu_dir(struct task_struct *tsk, pgd_t *pgdir)
{
	(tsk)->tss.pgd_ptr = (unsigned long) (pgdir);

	/* May have to do some flushing here. */

	return;
}

int sun4c_pte_none(pte_t pte)		{ return !pte_val(pte); }
int sun4c_pte_present(pte_t pte)	{ return pte_val(pte) & _SUN4C_PAGE_VALID; }
int sun4c_pte_inuse(pte_t *ptep)        { return mem_map[MAP_NR(ptep)] != 1; }
void sun4c_pte_clear(pte_t *ptep)	{ pte_val(*ptep) = 0; }
void sun4c_pte_reuse(pte_t *ptep)
{
  if(!(mem_map[MAP_NR(ptep)] & MAP_PAGE_RESERVED))
    mem_map[MAP_NR(ptep)]++;
}

int sun4c_pmd_none(pmd_t pmd)		{ return !pmd_val(pmd); }
int sun4c_pmd_bad(pmd_t pmd)
{
	return ((pmd_val(pmd) & _SUN4C_PGD_MMU_MASK) != _SUN4C_PAGE_TABLE);
}

int sun4c_pmd_present(pmd_t pmd)	{ return pmd_val(pmd) & _SUN4C_PAGE_VALID; }
int sun4c_pmd_inuse(pmd_t *pmdp)        { return 0; }
void sun4c_pmd_clear(pmd_t *pmdp)	{ pmd_val(*pmdp) = 0; }
void sun4c_pmd_reuse(pmd_t * pmdp)      { }

int sun4c_pgd_none(pgd_t pgd)		{ return 0; }
int sun4c_pgd_bad(pgd_t pgd)		{ return 0; }
int sun4c_pgd_present(pgd_t pgd)	{ return 1; }
int sun4c_pgd_inuse(pgd_t *pgdp)        { return mem_map[MAP_NR(pgdp)] != 1; }
void sun4c_pgd_clear(pgd_t * pgdp)	{ }
void sun4c_pgd_reuse(pgd_t *pgdp)
{
  if (!(mem_map[MAP_NR(pgdp)] & MAP_PAGE_RESERVED))
    mem_map[MAP_NR(pgdp)]++;
}

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
int sun4c_pte_read(pte_t pte)		{ return pte_val(pte) & _SUN4C_PAGE_VALID; }
int sun4c_pte_write(pte_t pte)		{ return pte_val(pte) & _SUN4C_PAGE_WRITE; }
int sun4c_pte_exec(pte_t pte)		{ return pte_val(pte) & _SUN4C_PAGE_VALID; }
int sun4c_pte_dirty(pte_t pte)		{ return pte_val(pte) & _SUN4C_PAGE_DIRTY; }
int sun4c_pte_young(pte_t pte)		{ return pte_val(pte) & _SUN4C_PAGE_REF; }
int sun4c_pte_cow(pte_t pte)		{ return pte_val(pte) & _SUN4C_PAGE_COW; }

pte_t sun4c_pte_wrprotect(pte_t pte)	{ pte_val(pte) &= ~_SUN4C_PAGE_WRITE; return pte; }
pte_t sun4c_pte_rdprotect(pte_t pte)	{ pte_val(pte) |= _SUN4C_PAGE_PRIV; return pte; }
pte_t sun4c_pte_exprotect(pte_t pte)	{ pte_val(pte) |= _SUN4C_PAGE_PRIV; return pte; }
pte_t sun4c_pte_mkclean(pte_t pte)	{ pte_val(pte) &= ~_SUN4C_PAGE_DIRTY; return pte; }
pte_t sun4c_pte_mkold(pte_t pte)	{ pte_val(pte) &= ~_SUN4C_PAGE_REF; return pte; }
pte_t sun4c_pte_uncow(pte_t pte)	{ pte_val(pte) &= ~_SUN4C_PAGE_COW; return pte; }
pte_t sun4c_pte_mkwrite(pte_t pte)	{ pte_val(pte) |= _SUN4C_PAGE_WRITE; return pte; }
pte_t sun4c_pte_mkread(pte_t pte)	{ pte_val(pte) &= ~_SUN4C_PAGE_PRIV; return pte; }
pte_t sun4c_pte_mkexec(pte_t pte)	{ pte_val(pte) &= ~_SUN4C_PAGE_PRIV; return pte; }
pte_t sun4c_pte_mkdirty(pte_t pte)	{ pte_val(pte) |= _SUN4C_PAGE_DIRTY; return pte; }
pte_t sun4c_pte_mkyoung(pte_t pte)	{ pte_val(pte) |= _SUN4C_PAGE_REF; return pte; }
pte_t sun4c_pte_mkcow(pte_t pte)	{ pte_val(pte) |= _SUN4C_PAGE_COW; return pte; }

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
pte_t
sun4c_mk_pte(unsigned long page, pgprot_t pgprot)
{
	pte_t pte;

	if(page & (~PAGE_MASK)) panic("sun4c_mk_pte() called with unaligned page");
	page = sun4c_virt_to_phys(page);
	pte_val(pte) = ((page>>PAGE_SHIFT)&_SUN4C_PFN_MASK);
	pte_val(pte) |= (pgprot_val(pgprot) & _SUN4C_MMU_MASK);
	return pte;
}

void
sun4c_pgd_set(pgd_t * pgdp, pte_t * ptep)
{
	pgd_val(*pgdp) = (_SUN4C_PAGE_TABLE & _SUN4C_PGD_MMU_MASK);
	pgd_val(*pgdp) |= (((((unsigned long) ptep)) >>
			    (_SUN4C_PGD_PAGE_SHIFT)) & _SUN4C_PGD_PFN_MASK);
}

pte_t
sun4c_pte_modify(pte_t pte, pgprot_t newprot)
{
	pte_val(pte) = (pte_val(pte) & _SUN4C_PAGE_CHG_MASK);
	pte_val(pte) |= pgprot_val(newprot);
	return pte;
}

/* to find an entry in a page-table-directory */
pgd_t *
sun4c_pgd_offset(struct task_struct * tsk, unsigned long address)
{
	return ((pgd_t *) (tsk->tss.pgd_ptr)) +
		(address >> SUN4C_PGDIR_SHIFT);
}

/* Find an entry in the second-level page table.. */
pmd_t *
sun4c_pmd_offset(pgd_t * dir, unsigned long address)
{
	return (pmd_t *) dir;
}

/* Find an entry in the third-level page table.. */ 
pte_t *
sun4c_pte_offset(pmd_t * dir, unsigned long address)
{
	return (pte_t *) sun4c_pmd_page(*dir) +	((address >> PAGE_SHIFT) & (SUN4C_PTRS_PER_PTE - 1));
}

/*
 * Allocate and free page tables. The xxx_kernel() versions are
 * used to allocate a kernel page table - this turns on ASN bits
 * if any, and marks the page tables reserved.
 */
void
sun4c_pte_free_kernel(pte_t *pte)
{
	mem_map[MAP_NR(pte)] = 1;
	free_page((unsigned long) pte);
}

static inline void
sun4c_pmd_set(pmd_t * pmdp, pte_t * ptep)
{
	pmd_val(*pmdp) = (_SUN4C_PAGE_TABLE & _SUN4C_PGD_MMU_MASK);
	pmd_val(*pmdp) |= ((((unsigned long) ptep) >> (_SUN4C_PGD_PAGE_SHIFT)) & _SUN4C_PGD_PFN_MASK);
}


pte_t *
sun4c_pte_alloc_kernel(pmd_t *pmd, unsigned long address)
{
	pte_t *page;


	address = (address >> PAGE_SHIFT) & (SUN4C_PTRS_PER_PTE - 1);
	if (sun4c_pmd_none(*pmd)) {
		/* New scheme, use a whole page */
		page = (pte_t *) get_free_page(GFP_KERNEL);
		if (sun4c_pmd_none(*pmd)) {
			if (page) {
				sun4c_pmd_set(pmd, page);
				mem_map[MAP_NR(page)] = MAP_PAGE_RESERVED;
				return page + address;
			}
			sun4c_pmd_set(pmd, (pte_t *) BAD_PAGETABLE);
			return NULL;
		}
		free_page((unsigned long) page);
	}
	if (sun4c_pmd_bad(*pmd)) {
		printk("Bad pmd in pte_alloc_kernel: %08lx\n", pmd_val(*pmd));
		sun4c_pmd_set(pmd, (pte_t *) BAD_PAGETABLE);
		return NULL;
	}

	return (pte_t *) sun4c_pmd_page(*pmd) + address;
}

/*
 * allocating and freeing a pmd is trivial: the 1-entry pmd is
 * inside the pgd, so has no extra memory associated with it.
 */
void
sun4c_pmd_free_kernel(pmd_t *pmd)
{
	return;
}

pmd_t *
sun4c_pmd_alloc_kernel(pgd_t *pgd, unsigned long address)
{
	return (pmd_t *) pgd;
}

void
sun4c_pte_free(pte_t *pte)
{
	free_page((unsigned long) pte);
}

pte_t *
sun4c_pte_alloc(pmd_t * pmd, unsigned long address)
{
	pte_t *page;

	address = (address >> PAGE_SHIFT) & (SUN4C_PTRS_PER_PTE - 1);
	if (sun4c_pmd_none(*pmd)) {
		page = (pte_t *) get_free_page(GFP_KERNEL);
		if (sun4c_pmd_none(*pmd)) {
			if (page) {
				sun4c_pmd_set(pmd, page);
				return page + address;
			}
			sun4c_pmd_set(pmd, (pte_t *) BAD_PAGETABLE);
			return NULL;
		}
		free_page((unsigned long) page);
	}
	if (sun4c_pmd_bad(*pmd)) {
		printk("Bad pmd in pte_alloc: %08lx\n", pmd_val(*pmd));
		sun4c_pmd_set(pmd, (pte_t *) BAD_PAGETABLE);
		halt();
		return NULL;
	}

	return (pte_t *) sun4c_pmd_page(*pmd) + address;
}

/*
 * allocating and freeing a pmd is trivial: the 1-entry pmd is
 * inside the pgd, so has no extra memory associated with it.
 */
void 
sun4c_pmd_free(pmd_t * pmd)
{
	return;
}

pmd_t *
sun4c_pmd_alloc(pgd_t * pgd, unsigned long address)
{
	return (pmd_t *) pgd;
}

/* This now works, as both our pgd's and pte's have 1024 entries. */
void
sun4c_pgd_free(pgd_t *pgd)
{
	free_page((unsigned long) pgd);
}

/* A page directory on the sun4c needs 64k, thus we request an order of
 * four.  We must also clear it by hand, very inefficient.
 */

pgd_t *
sun4c_pgd_alloc(void)
{
	return (pgd_t *) get_free_page(GFP_KERNEL);
}

void
sun4c_invalidate(void)
{
	flush_vac_context();
}

void
sun4c_switch_to_context(int context)
{
	__asm__ __volatile__("stba %0, [%1] %2" : :
			     "r" (context),
			     "r" (AC_CONTEXT), "i" (ASI_CONTROL));

	return;
}

int 
sun4c_get_context(void)
{
	register int ctx;

	__asm__ __volatile__("lduba [%1] %2, %0" :
			     "=r" (ctx) :
			     "r" (AC_CONTEXT), "i" (ASI_CONTROL));

	return ctx;
}

/* Low level IO area allocation on the Sun4c MMU.  This function is called
 * for each page of IO area you need.  Kernel code should not call this
 * routine directly, use sparc_alloc_io() instead.
 */
void
sun4c_mapioaddr(unsigned long physaddr, unsigned long virt_addr,
		int bus_type, int rdonly)
{
  unsigned long page_entry;

  page_entry = ((physaddr >> PAGE_SHIFT) & _SUN4C_PFN_MASK);

  if(!rdonly)
	  page_entry |= (PTE_V | PTE_ACC | PTE_NC | PTE_IO);  /* kernel io addr */
  else
	  page_entry |= (PTE_V | PTE_P | PTE_NC | PTE_IO);  /* readonly io addr */

  page_entry &= (~PTE_RESV);

  /* Maybe have to do something with the bus_type on sun4c's? */


  put_pte(virt_addr, page_entry);
  return;
}

/* Paging initialization on the Sun4c. */
extern unsigned long free_area_init(unsigned long, unsigned long);
extern unsigned long eintstack, intstack;

/* This code was soooo krufty, I have to rewrite this now! XXX
 * Ok, things are cleaning up.  I have now decided that it makes
 * a lot of sense to put the free page pool in upper ram right
 * after the kernel.  We map these free pages to be virtually
 * contiguous, that way we don't get so many reserved pages
 * during mem_init().  I think this will work out nicely.
 */
extern unsigned long start;

static unsigned long mempool;  /* This allows us to work with elf bootloaders */

unsigned long
sun4c_paging_init(unsigned long start_mem, unsigned long end_mem)
{
	unsigned long addr, vaddr, kern_begin, kern_end;
	unsigned long prom_begin, prom_end;
	int phys_seg, i, min_prom_segmap;
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;

	mempool = start_mem;

	/* 127 on most sun4c's, 255 on SS2 and IPX. */
	invalid_segment = (num_segmaps - 1);

	memset(swapper_pg_dir, 0, PAGE_SIZE);
	memset(pg0, 0, PAGE_SIZE);
	/* Delete low mapping of the kernel and sanitize invalid segmap. */
	for(vaddr=0; vaddr<(4*1024*1024); vaddr+=SUN4C_REAL_PGDIR_SIZE) 
		put_segmap(vaddr, invalid_segment);
	for(vaddr=0; vaddr<(256*1024); vaddr+=PAGE_SIZE) put_pte(vaddr, 0);

	/* Initialize phys_seg_map[] */
	for(i=0; i<num_segmaps; i++) phys_seg_map[i] = PSEG_AVL;
	for(i=num_segmaps; i<PSEG_ENTRIES; i++) phys_seg_map[i] = PSEG_RSV;

	kern_begin = KERNBASE;
	kern_end = ((unsigned long) &end);
	prom_begin = LINUX_OPPROM_BEGVM;
	prom_end = LINUX_OPPROM_ENDVM;

	/* Set up swapper_pg_dir based upon three things:
	 * 1) Where the kernel lives (KERNBASE)
	 * 2) Where the PROM lives (PROM_BEGVM -> PROM_ENDVM)
	 *    This is cheese, should do it dynamically XXX
	 * 3) Where the valid physical pages are (sp_banks[])
	 *    This is done first.
	 *
	 * I'm trying to concentrate this into one big loop and localize
	 * the logic because it is so messy to do it in seperate loop
	 * stages.  If anyone else has better ideas, let me know.
	 */

	if(sp_banks[0].base_addr != 0)
		panic("sun4c_paging_init: First physical address in first bank is not zero!\n");
	/* First, linearly map all physical RAM to the equivalent virtual pages.
	 * Then, we invalidate everything the kernel uses by either invalidating
	 * the entire segmep (if the whole segment is used by the kernel) or
	 * just invalidating the relevant pte's.
	 */

	for(vaddr = KERNBASE; vaddr < end_mem; vaddr+=PAGE_SIZE) {
		pgdp = sun4c_pgd_offset(current, vaddr);
		pmdp = sun4c_pmd_offset(pgdp, vaddr);
		if(sun4c_pmd_none(*pmdp)) {
			pgd_set(pgdp, (pte_t *) mempool);
			mempool += PAGE_SIZE;
		}
		ptep = sun4c_pte_offset(pmdp, vaddr);
		*ptep = sun4c_mk_pte(vaddr, SUN4C_PAGE_KERNEL);
	}

	/* Now map the kernel, and mark the segmaps as PSEG_KERN.
	 *
	 * NOTE: The first address of the upper kernel mapping must be
	 *       segment aligned.
	 */
	if(kern_begin & (~SUN4C_REAL_PGDIR_MASK)) {
		panic("paging_init() Kernel not segmap aligned, halting...");
	}

	/* Mark the segmaps so that our phys_seg allocator doesn't try to
	 * use them for TLB misses.
	 */
	for(addr=kern_begin; addr < kern_end; addr += SUN4C_REAL_PGDIR_SIZE) {
		if(get_segmap(addr) == invalid_segment) {
			panic("paging_init() AIEEE, Kernel has invalid mapping, halting...");
		}
		phys_seg = get_segmap(addr);
		phys_seg_map[phys_seg] = PSEG_KERNEL;
		/* Map this segment in every context */
		for(i=0; i<num_contexts; i++)
			(*romvec->pv_setctxt)(i, (char *) addr, phys_seg);
	}

	for(addr=((unsigned long) (&empty_zero_page)) + PAGE_SIZE; 
	    addr < ((unsigned long) (&etext)); addr += PAGE_SIZE)
		put_pte(addr, (get_pte(addr) & (~(PTE_W | PTE_NC))));

	/* Finally map the prom's address space.  Any segments that
	 * are not the invalid segment are marked as PSEG_RESV so
	 * they are never re-allocated.  This guarentees the PROM
	 * a sane state if we have to return execution over to it.
	 * Our kernel static tables make it look like nothing is
	 * mapped in these segments, if we get a page fault for
	 * a prom address either the user is gonna die or the kernel
	 * is doing something *really* bad.
	 */
	if(prom_begin & (~SUN4C_REAL_PGDIR_MASK)) {
		panic("paging_init() Boot PROM not segmap aligned, halting...");
		halt();
	}

	min_prom_segmap = 254;
	for(addr=KADB_DEBUGGER_BEGVM; addr < prom_end; addr += SUN4C_REAL_PGDIR_SIZE) {
		if(get_segmap(addr) == invalid_segment)
			continue;
		phys_seg = get_segmap(addr);
		if(phys_seg < min_prom_segmap) min_prom_segmap = phys_seg;
		phys_seg_map[phys_seg] = PSEG_RSV;
		/* Make the prom pages unaccessible from userland.  However, we
		 * don't touch debugger segmaps/ptes.
		 */
		if((addr>=LINUX_OPPROM_BEGVM) && (addr<LINUX_OPPROM_ENDVM))
			for(vaddr=addr; vaddr < (addr+SUN4C_REAL_PGDIR_SIZE); vaddr+=PAGE_SIZE)
				put_pte(vaddr, (get_pte(vaddr) | PTE_P));

		/* Map this segment in every context */
		for(i=0; i<num_contexts; i++)
			(*romvec->pv_setctxt)(i, (char *) addr, phys_seg);
	}

	/* Finally, unmap kernel page zero. */
	put_pte(0x0, 0x0);

	/* Hard pin down the IO area segmaps */
	phys_seg = (min_prom_segmap - 1);
	for(addr = (IOBASE_VADDR + SUN4C_REAL_PGDIR_SIZE); addr < (IOBASE_VADDR + IOBASE_LEN);
	    addr += SUN4C_REAL_PGDIR_SIZE) {
		if(addr & (~SUN4C_REAL_PGDIR_MASK)) {
			panic("paging_init() IO segment not aligned, halting...");
		}
		phys_seg_map[phys_seg] = PSEG_RSV; /* Don't touch */
		put_segmap(addr, phys_seg--);
	}
	phys_seg_map[IOBASE_SUN4C_SEGMAP] = PSEG_RSV;

	start_mem = PAGE_ALIGN(mempool);
	start_mem = free_area_init(start_mem, end_mem);
	start_mem = PAGE_ALIGN(start_mem);

	/* That should be it. */
	invalidate();

	return start_mem;
}

/* Test the WP bit on the sun4c. */
unsigned long
sun4c_test_wp(unsigned long start_mem)
{
	unsigned long addr, segmap;
	unsigned long page_entry;

	wp_works_ok = -1;
	page_entry = pte_val(sun4c_mk_pte(PAGE_OFFSET, SUN4C_PAGE_READONLY));
	put_pte((unsigned long) 0x0, page_entry);

	/* Let it rip... */
	__asm__ __volatile__("st %%g0, [0x0]\n\t": : :"memory");
	put_pte((unsigned long) 0x0, 0x0);
	if (wp_works_ok < 0)
		wp_works_ok = 0;

	/* Make all kernet static segmaps PSEG_KERNEL. */
	for(addr=PAGE_OFFSET; addr<start_mem; addr+=SUN4C_REAL_PGDIR_SIZE)
		phys_seg_map[get_segmap(addr)]=PSEG_KERNEL;

	/* Map all the segmaps not valid on this machine as reserved. */
	for(segmap=invalid_segment; segmap<PSEG_ENTRIES; segmap++)
		phys_seg_map[segmap]=PSEG_RSV;

	return start_mem;
}

/* Real work gets done here. */

/* Load up routines and constants for sun4c mmu */
void
ld_mmu_sun4c(void)
{
	printk("Loading sun4c MMU routines\n");

	/* First the constants */
	pmd_shift = SUN4C_PMD_SHIFT;
	pmd_size = SUN4C_PMD_SIZE;
	pmd_mask = SUN4C_PMD_MASK;
	pgdir_shift = SUN4C_PGDIR_SHIFT;
	pgdir_size = SUN4C_PGDIR_SIZE;
	pgdir_mask = SUN4C_PGDIR_MASK;

	ptrs_per_pte = SUN4C_PTRS_PER_PTE;
	ptrs_per_pmd = SUN4C_PTRS_PER_PMD;
	ptrs_per_pgd = SUN4C_PTRS_PER_PGD;

	page_none = SUN4C_PAGE_NONE;
	page_shared = SUN4C_PAGE_SHARED;
	page_copy = SUN4C_PAGE_COPY;
	page_readonly = SUN4C_PAGE_READONLY;
	page_kernel = SUN4C_PAGE_KERNEL;
	page_invalid = SUN4C_PAGE_INVALID;
	
	/* Functions */
	invalidate = sun4c_invalidate;
	switch_to_context = sun4c_switch_to_context;
	pmd_align = sun4c_pmd_align;
	pgdir_align = sun4c_pgdir_align;
	vmalloc_start = sun4c_vmalloc_start;

	pte_page = sun4c_pte_page;
	pmd_page = sun4c_pmd_page;
	pgd_page = sun4c_pgd_page;

	sparc_update_rootmmu_dir = sun4c_update_rootmmu_dir;

	pte_none = sun4c_pte_none;
	pte_present = sun4c_pte_present;
	pte_inuse = sun4c_pte_inuse;
	pte_clear = sun4c_pte_clear;
	pte_reuse = sun4c_pte_reuse;

	pmd_none = sun4c_pmd_none;
	pmd_bad = sun4c_pmd_bad;
	pmd_present = sun4c_pmd_present;
	pmd_inuse = sun4c_pmd_inuse;
	pmd_clear = sun4c_pmd_clear;
	pmd_reuse = sun4c_pmd_reuse;

	pgd_none = sun4c_pgd_none;
	pgd_bad = sun4c_pgd_bad;
	pgd_present = sun4c_pgd_present;
	pgd_inuse = sun4c_pgd_inuse;
	pgd_clear = sun4c_pgd_clear;
	pgd_reuse = sun4c_pgd_reuse;

	mk_pte = sun4c_mk_pte;
	pgd_set = sun4c_pgd_set;
	pte_modify = sun4c_pte_modify;
	pgd_offset = sun4c_pgd_offset;
	pmd_offset = sun4c_pmd_offset;
	pte_offset = sun4c_pte_offset;
	pte_free_kernel = sun4c_pte_free_kernel;
	pmd_free_kernel = sun4c_pmd_free_kernel;
	pte_alloc_kernel = sun4c_pte_alloc_kernel;
	pmd_alloc_kernel = sun4c_pmd_alloc_kernel;
	pte_free = sun4c_pte_free;
	pte_alloc = sun4c_pte_alloc;
	pmd_free = sun4c_pmd_free;
	pmd_alloc = sun4c_pmd_alloc;
	pgd_free = sun4c_pgd_free;
	pgd_alloc = sun4c_pgd_alloc;

	pte_read = sun4c_pte_read;
	pte_write = sun4c_pte_write;
	pte_exec = sun4c_pte_exec;
	pte_dirty = sun4c_pte_dirty;
	pte_young = sun4c_pte_young;
	pte_cow = sun4c_pte_cow;
	pte_wrprotect = sun4c_pte_wrprotect;
	pte_rdprotect = sun4c_pte_rdprotect;
	pte_exprotect = sun4c_pte_exprotect;
	pte_mkclean = sun4c_pte_mkclean;
	pte_mkold = sun4c_pte_mkold;
	pte_uncow = sun4c_pte_uncow;
	pte_mkwrite = sun4c_pte_mkwrite;
	pte_mkread = sun4c_pte_mkread;
	pte_mkexec = sun4c_pte_mkexec;
	pte_mkdirty = sun4c_pte_mkdirty;
	pte_mkyoung = sun4c_pte_mkyoung;
	pte_mkcow = sun4c_pte_mkcow;

	return;
}
