/* srmmu.c:  SRMMU specific routines for memory management.
 *
 * Copyright (C) 1995 David S. Miller  (davem@caip.rutgers.edu)
 * Copyright (C) 1995 Peter A. Zaitcev (zaitcev@lab.ipmce.su)
 */

#include <linux/kernel.h>  /* for printk */

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/kdebug.h>
#include <asm/vaddrs.h>
#include <asm/traps.h>
#include <asm/mp.h>
#include <asm/cache.h>
#include <asm/oplib.h>

extern unsigned long free_area_init(unsigned long, unsigned long);

unsigned int srmmu_pmd_align(unsigned int addr) { return SRMMU_PMD_ALIGN(addr); }
unsigned int srmmu_pgdir_align(unsigned int addr) { return SRMMU_PGDIR_ALIGN(addr); }

/* Idea taken from Hamish McDonald's MC680x0 Linux code, nice job.
 * Many of the page table/directory functions on the SRMMU use this
 * routine.
 *
 * Having a complete physical ram structure walk happen for each
 * invocation is quite costly.  However, this does do some nice
 * sanity checking and we'll see when our maps don't match.  Eventually
 * when I trust my code I will just do a direct mmu probe in mk_pte().
 */
static inline unsigned int
srmmu_virt_to_phys(unsigned int vaddr)
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
	printk("srmmu_virt_to_phys: SRMMU virt to phys translation failed, halting\n");
	halt();
}		

static inline unsigned long
srmmu_phys_to_virt(unsigned long paddr)
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
	printk("srmmu_phys_to_virt: Could not make translation, halting...\n");
	halt();
}

unsigned long
srmmu_vmalloc_start(void)
{
	return ((high_memory + SRMMU_VMALLOC_OFFSET) & ~(SRMMU_VMALLOC_OFFSET-1));
}

unsigned long 
srmmu_pmd_page(pmd_t pmd)
{
	unsigned long page;

	page = (pmd_val(pmd) & (SRMMU_PTD_PTP_MASK)) << SRMMU_PTD_PTP_PADDR_SHIFT;
	return srmmu_phys_to_virt(page);
}

unsigned long
srmmu_pgd_page(pgd_t pgd)
{
	unsigned long page;

	page = (pgd_val(pgd) & (SRMMU_PTD_PTP_MASK)) << SRMMU_PTD_PTP_PADDR_SHIFT;
	return srmmu_phys_to_virt(page);
}

unsigned long 
srmmu_pte_page(pte_t pte)
{
	unsigned long page;

	page = (pte_val(pte) & (SRMMU_PTE_PPN_MASK)) << SRMMU_PTE_PPN_PADDR_SHIFT;
	printk("srmmu_pte_page: page = %08lx\n", page);
	return srmmu_phys_to_virt(page);
}

int srmmu_pte_none(pte_t pte)		{ return !pte_val(pte); }
int srmmu_pte_present(pte_t pte)	{ return pte_val(pte) & SRMMU_ET_PTE; }
int srmmu_pte_inuse(pte_t *ptep)        { return mem_map[MAP_NR(ptep)] != 1; }
void srmmu_pte_clear(pte_t *ptep)	{ pte_val(*ptep) = 0; }
void srmmu_pte_reuse(pte_t *ptep)
{
  if(!(mem_map[MAP_NR(ptep)] & MAP_PAGE_RESERVED))
    mem_map[MAP_NR(ptep)]++;
}

int srmmu_pmd_none(pmd_t pmd)		{ return !pmd_val(pmd); }
int srmmu_pmd_bad(pmd_t pmd)
{
	return ((pmd_val(pmd)&SRMMU_ET_PTDBAD)==SRMMU_ET_PTDBAD) ||
		(srmmu_pmd_page(pmd) > high_memory);
}

int srmmu_pmd_present(pmd_t pmd)	{ return pmd_val(pmd) & SRMMU_ET_PTD; }
int srmmu_pmd_inuse(pmd_t *pmdp)        { return mem_map[MAP_NR(pmdp)] != 1; }
void srmmu_pmd_clear(pmd_t *pmdp)	{ pmd_val(*pmdp) = 0; }
void srmmu_pmd_reuse(pmd_t * pmdp)
{
        if (!(mem_map[MAP_NR(pmdp)] & MAP_PAGE_RESERVED))
                mem_map[MAP_NR(pmdp)]++;
}

int srmmu_pgd_none(pgd_t pgd)		{ return !pgd_val(pgd); }
int srmmu_pgd_bad(pgd_t pgd)
{
	return ((pgd_val(pgd)&SRMMU_ET_PTDBAD)==SRMMU_ET_PTDBAD) ||
		(srmmu_pgd_page(pgd) > high_memory);
}
int srmmu_pgd_present(pgd_t pgd)	{ return pgd_val(pgd) & SRMMU_ET_PTD; }
int srmmu_pgd_inuse(pgd_t *pgdp)        { return mem_map[MAP_NR(pgdp)] != 1; }
void srmmu_pgd_clear(pgd_t * pgdp)	{ pgd_val(*pgdp) = 0; }
void srmmu_pgd_reuse(pgd_t *pgdp)
{
  if (!(mem_map[MAP_NR(pgdp)] & MAP_PAGE_RESERVED))
    mem_map[MAP_NR(pgdp)]++;
}

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
int srmmu_pte_read(pte_t pte)		{ return (pte_val(pte) & _SRMMU_PAGE_RDONLY) || (pte_val(pte) & _SRMMU_PAGE_WRITE_USR); }
int srmmu_pte_write(pte_t pte)		{ return pte_val(pte) & _SRMMU_PAGE_WRITE_USR; }
int srmmu_pte_exec(pte_t pte)		{ return pte_val(pte) & _SRMMU_PAGE_EXEC; }
int srmmu_pte_dirty(pte_t pte)		{ return pte_val(pte) & _SRMMU_PAGE_DIRTY; }
int srmmu_pte_young(pte_t pte)		{ return pte_val(pte) & _SRMMU_PAGE_REF; }
int srmmu_pte_cow(pte_t pte)		{ return pte_val(pte) & _SRMMU_PAGE_COW; }

/* When we change permissions, we first clear all bits in the ACCESS field
 * then apply the wanted bits.
 */
pte_t srmmu_pte_wrprotect(pte_t pte)	{ pte_val(pte) &= ~SRMMU_PTE_ACC_MASK; pte_val(pte) |= _SRMMU_PAGE_EXEC; return pte; }
pte_t srmmu_pte_rdprotect(pte_t pte)	{ pte_val(pte) &= ~SRMMU_PTE_ACC_MASK; pte_val(pte) |= _SRMMU_PAGE_NOREAD; return pte; }
pte_t srmmu_pte_exprotect(pte_t pte)	{ pte_val(pte) &= ~SRMMU_PTE_ACC_MASK; pte_val(pte) |= _SRMMU_PAGE_WRITE_USR; return pte; }
pte_t srmmu_pte_mkclean(pte_t pte)	{ pte_val(pte) &= ~_SRMMU_PAGE_DIRTY; return pte; }
pte_t srmmu_pte_mkold(pte_t pte)	{ pte_val(pte) &= ~_SRMMU_PAGE_REF; return pte; }
pte_t srmmu_pte_uncow(pte_t pte)	{ pte_val(pte) &= ~SRMMU_PTE_ACC_MASK; pte_val(pte) |= _SRMMU_PAGE_UNCOW; return pte; }
pte_t srmmu_pte_mkwrite(pte_t pte)	{ pte_val(pte) &= ~SRMMU_PTE_ACC_MASK; pte_val(pte) |= _SRMMU_PAGE_WRITE_USR; return pte; }
pte_t srmmu_pte_mkread(pte_t pte)	{ pte_val(pte) &= ~SRMMU_PTE_ACC_MASK; pte_val(pte) |= _SRMMU_PAGE_RDONLY; return pte; }
pte_t srmmu_pte_mkexec(pte_t pte)	{ pte_val(pte) &= ~SRMMU_PTE_ACC_MASK; pte_val(pte) |= _SRMMU_PAGE_EXEC; return pte; }
pte_t srmmu_pte_mkdirty(pte_t pte)	{ pte_val(pte) |= _SRMMU_PAGE_DIRTY; return pte; }
pte_t srmmu_pte_mkyoung(pte_t pte)	{ pte_val(pte) |= _SRMMU_PAGE_REF; return pte; }
pte_t srmmu_pte_mkcow(pte_t pte)	{ pte_val(pte) &= ~SRMMU_PTE_ACC_MASK; pte_val(pte) |= _SRMMU_PAGE_COW; return pte; }

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
pte_t
srmmu_mk_pte(unsigned long page, pgprot_t pgprot)
{
	pte_t pte;

	if(page & (~PAGE_MASK)) panic("srmmu_mk_pte() called with unaligned page");
	page = (srmmu_virt_to_phys(page) >> SRMMU_PTE_PPN_PADDR_SHIFT);
	pte_val(pte) = (page & SRMMU_PTE_PPN_MASK);
	pte_val(pte) |= pgprot_val(pgprot);
	return pte;
}

void
srmmu_pgd_set(pgd_t * pgdp, pmd_t * pmdp)
{
	unsigned long page = (unsigned long) pmdp;

	page = (srmmu_virt_to_phys(page) >> SRMMU_PTD_PTP_PADDR_SHIFT);

	pgd_val(*pgdp) = ((page & SRMMU_PTD_PTP_MASK) | SRMMU_ET_PTD);
}

void
srmmu_pmd_set(pmd_t * pmdp, pte_t * ptep)
{
	unsigned long page = (unsigned long) ptep;

	page = (srmmu_virt_to_phys(page) >> SRMMU_PTD_PTP_PADDR_SHIFT);

	pmd_val(*pmdp) = ((page & SRMMU_PTD_PTP_MASK) | SRMMU_ET_PTD);
}

pte_t
srmmu_pte_modify(pte_t pte, pgprot_t newprot)
{
	pte_val(pte) = (pte_val(pte) & (~SRMMU_PTE_ACC_MASK)) | pgprot_val(newprot);
	return pte;
}

/* to find an entry in a top-level page table... */
pgd_t *
srmmu_pgd_offset(struct task_struct * tsk, unsigned long address)
{
	return ((pgd_t *) tsk->tss.pgd_ptr) +
		((address >> SRMMU_PGDIR_SHIFT) & (SRMMU_PTRS_PER_PGD - 1));
}

/* Find an entry in the second-level page table.. */
pmd_t *
srmmu_pmd_offset(pgd_t * dir, unsigned long address)
{
	return ((pmd_t *) pgd_page(*dir)) +
		((address >> SRMMU_PMD_SHIFT) & (SRMMU_PTRS_PER_PMD - 1));
}

/* Find an entry in the third-level page table.. */ 
pte_t *
srmmu_pte_offset(pmd_t * dir, unsigned long address)
{
	return ((pte_t *) pmd_page(*dir)) +
		((address >> PAGE_SHIFT) & (SRMMU_PTRS_PER_PTE - 1));
}

/* This must update the context register for this process. */
void
srmmu_update_rootmmu_dir(struct task_struct *tsk, pgd_t *pgdir) 
{
	/* See if this process has a context entry already, like after execve() */
	if(tsk->tss.context != -1) {
		pgd_t *ctable_ptr = 0;
		ctable_ptr = (pgd_t *) srmmu_phys_to_virt(srmmu_get_ctable_ptr());
		ctable_ptr += tsk->tss.context;
		srmmu_pgd_set(ctable_ptr, (pmd_t *) pgdir);
		/* Should flush caches here too... */
		srmmu_flush_whole_tlb();
	}

	tsk->tss.pgd_ptr = (unsigned long) pgdir;

	return;
}

/*
 * Allocate and free page tables. The xxx_kernel() versions are
 * used to allocate a kernel page table - this turns on ASN bits
 * if any, and marks the page tables reserved.
 */
void
srmmu_pte_free_kernel(pte_t *pte)
{
	mem_map[MAP_NR(pte)] = 1;
	free_page((unsigned long) pte);
}

pte_t *
srmmu_pte_alloc_kernel(pmd_t *pmd, unsigned long address)
{
	pte_t *page;

	address = (address >> PAGE_SHIFT) & (SRMMU_PTRS_PER_PTE - 1);
	if (srmmu_pmd_none(*pmd)) {
		page = (pte_t *) get_free_page(GFP_KERNEL);
		if (srmmu_pmd_none(*pmd)) {
			if (page) {
				srmmu_pmd_set(pmd, page);
				mem_map[MAP_NR(page)] = MAP_PAGE_RESERVED;
				return page + address;
			}
			srmmu_pmd_set(pmd, (pte_t *) SRMMU_ET_PTDBAD);
			return NULL;
		}
		free_page((unsigned long) page);
	}
	if (srmmu_pmd_bad(*pmd)) {
		printk("Bad pmd in pte_alloc_kernel: %08lx\n", pmd_val(*pmd));
		srmmu_pmd_set(pmd, (pte_t *) SRMMU_ET_PTDBAD);
		return NULL;
	}
	return (pte_t *) srmmu_pmd_page(*pmd) + address;
}

/* Full three level on SRMMU */
void
srmmu_pmd_free_kernel(pmd_t *pmd)
{
	mem_map[MAP_NR(pmd)] = 1;
	free_page((unsigned long) pmd);
}

pmd_t *
srmmu_pmd_alloc_kernel(pgd_t *pgd, unsigned long address)
{
	pmd_t *page;

	address = (address >> SRMMU_PMD_SHIFT) & (SRMMU_PTRS_PER_PMD - 1);
	if (srmmu_pgd_none(*pgd)) {
		page = (pmd_t *) get_free_page(GFP_KERNEL);
		if (srmmu_pgd_none(*pgd)) {
			if (page) {
				srmmu_pgd_set(pgd, page);
				mem_map[MAP_NR(page)] = MAP_PAGE_RESERVED;
				return page + address;
			}
			srmmu_pgd_set(pgd, (pmd_t *) SRMMU_ET_PTDBAD);
			return NULL;
		}
		free_page((unsigned long) page);
	}
	if (srmmu_pgd_bad(*pgd)) {
		printk("Bad pgd in pmd_alloc_kernel: %08lx\n", pgd_val(*pgd));
		srmmu_pgd_set(pgd, (pmd_t *) SRMMU_ET_PTDBAD);
		return NULL;
	}
	return (pmd_t *) srmmu_pgd_page(*pgd) + address;
}

void
srmmu_pte_free(pte_t *pte)
{
	free_page((unsigned long) pte);
}

pte_t *
srmmu_pte_alloc(pmd_t * pmd, unsigned long address)
{
	pte_t *page;

	address = (address >> PAGE_SHIFT) & (SRMMU_PTRS_PER_PTE - 1);
	if (srmmu_pmd_none(*pmd)) {
		page = (pte_t *) get_free_page(GFP_KERNEL);
		if (srmmu_pmd_none(*pmd)) {
			if (page) {
				srmmu_pmd_set(pmd, page);
				mem_map[MAP_NR(page)] = MAP_PAGE_RESERVED;
				return page + address;
			}
			srmmu_pmd_set(pmd, (pte_t *) SRMMU_ET_PTDBAD);
			return NULL;
		}
		free_page((unsigned long) page);
	}
	if (srmmu_pmd_bad(*pmd)) {
		printk("Bad pmd in pte_alloc_kernel: %08lx\n", pmd_val(*pmd));
		srmmu_pmd_set(pmd, (pte_t *) SRMMU_ET_PTDBAD);
		return NULL;
	}
	return (pte_t *) srmmu_pmd_page(*pmd) + address;
}

/*
 * allocating and freeing a pmd is trivial: the 1-entry pmd is
 * inside the pgd, so has no extra memory associated with it.
 */
void 
srmmu_pmd_free(pmd_t * pmd)
{
	free_page((unsigned long) pmd);
}

pmd_t *
srmmu_pmd_alloc(pgd_t * pgd, unsigned long address)
{
	pmd_t *page;

	address = (address >> SRMMU_PMD_SHIFT) & (SRMMU_PTRS_PER_PMD - 1);
	if (srmmu_pgd_none(*pgd)) {
		page = (pmd_t *) get_free_page(GFP_KERNEL);
		if (srmmu_pgd_none(*pgd)) {
			if (page) {
				srmmu_pgd_set(pgd, page);
				mem_map[MAP_NR(page)] = MAP_PAGE_RESERVED;
				return page + address;
			}
			srmmu_pgd_set(pgd, (pmd_t *) SRMMU_ET_PTDBAD);
			return NULL;
		}
		free_page((unsigned long) page);
	}
	if (srmmu_pgd_bad(*pgd)) {
		printk("Bad pgd in pmd_alloc_kernel: %08lx\n", pgd_val(*pgd));
		srmmu_pgd_set(pgd, (pmd_t *) SRMMU_ET_PTDBAD);
		return NULL;
	}
	return (pmd_t *) srmmu_pgd_page(*pgd) + address;
}

void
srmmu_pgd_free(pgd_t *pgd)
{
	free_page((unsigned long) pgd);
}

/* A page directory on the srmmu needs 1k, but for now to simplify the
 * alignment constraints and allocation we just grab a whole page.
 */

pgd_t *
srmmu_pgd_alloc(void)
{
	return (pgd_t *) get_free_page(GFP_KERNEL);
}

/* Just flush the whole thing for now. We will need module
 * specific invalidate routines in certain circumstances,
 * because of different flushing facilities and hardware
 * bugs.
 */
void
srmmu_invalidate(void)
{
	srmmu_flush_whole_tlb();
	return;
}

/* XXX Needs to be written */
void
srmmu_switch_to_context(int context)
{
	printk("switching to context %d\n", context);

	return;
}

/* Low level IO area allocation on the SRMMU.
 *
 * I think we can get away with just using a regular page translation,
 * just making sure the cacheable bit is off.  I would like to avoid
 * having to mess with the IOMMU if at all possible at first.
 *
 * Aparently IOMMU is only necessary for SBus devices, maybe VME too.
 * We'll see...
 */
void
srmmu_mapioaddr(unsigned long physaddr, unsigned long virt_addr,
		int bus_type, int rdonly)
{
  pgd_t *pgdp;
  pmd_t *pmdp;
  pte_t *ptep;

  pgdp = srmmu_pgd_offset(&init_task, virt_addr);
  pmdp = srmmu_pmd_offset(pgdp, virt_addr);
  ptep = srmmu_pte_offset(pmdp, virt_addr);
  pte_val(*ptep) = (physaddr >> SRMMU_PTE_PPN_PADDR_SHIFT) & SRMMU_PTE_PPN_MASK;

  if(!rdonly)
	  pte_val(*ptep) |= (SRMMU_ACC_S_RDWREXEC | SRMMU_ET_PTE);
  else
	  pte_val(*ptep) |= (SRMMU_ACC_S_RDEXEC | SRMMU_ET_PTE);

  pte_val(*ptep) |= (bus_type << 28);
  pte_val(*ptep) &= ~(SRMMU_PTE_C_MASK); /* Make sure cacheable bit is off. */
  srmmu_flush_whole_tlb();
  flush_ei_ctx(0x0);

  return;
}

/* Perfom a some soft of MMU tablewalk.
 * Long contiguous mappings are not supported (yet ?).
 *
 * Origionally written by Peter Zaitcev, modified by David S.
 * Miller.  This is only used to copy over the PROM/KADB mappings
 * in srmmu_paging_init().
 *
 * The return value encodes at what level the entry was found,
 * basically this is found in the lower 2 bits of the return
 * value.  If the return value is zero, there was no valid mapping
 * found at all, the low bits for a non-zero return value
 * are:
 *         0 -- Level 1 PTE
 *         1 -- Level 2 PTE
 *         2 -- Normal level 3 PTE
 *         3 -- Context Table PTE (unlikely, but still)
 * 
 * Also note that this is called before the context table pointer
 * register is changed, so the PROMs entry is still in there.  Also,
 * it is safe to assume that the context 0 contains the mappings.
 */
/* TODO chop out 'trace' when stable */
unsigned int
srmmu_init_twalk(unsigned virt, int trace)
{
	unsigned int wh, root;

	root = (unsigned int) srmmu_get_ctable_ptr();
	if(trace) printk(":0x%x >> ", virt);

	if(trace) printk(" 0x%x :", root);
	wh = ldw_sun4m_bypass(root);
	if ((wh & SRMMU_PTE_ET_MASK) == SRMMU_ET_INVALID) {
		if(trace) printk("\n");
		return 0;
	}
	if((wh & SRMMU_PTE_ET_MASK) == SRMMU_ET_PTE) {
		wh &= ~SRMMU_PTE_ET_MASK;
		wh |= 0x3;
		if(trace) printk("\n");
		printk("AIEEE context table level pte prom mapping!\n");
		prom_halt();
		return 0;
	}
		
	if(trace) printk(" 0x%x .", wh);
	wh = ldw_sun4m_bypass(
			      ((wh & SRMMU_PTD_PTP_MASK) << 4)
			      + ((virt & SRMMU_IDX1_MASK) >> SRMMU_IDX1_SHIFT)*sizeof(pte_t));

	if ((wh & SRMMU_PTE_ET_MASK) == SRMMU_ET_INVALID) {
		if(trace) printk("\n");
		return 0;
	}
	if((wh & SRMMU_PTE_ET_MASK) == SRMMU_ET_PTE) {
		wh &= ~SRMMU_PTE_ET_MASK;
		if(trace) printk("\n");
		return wh;
	}

	if(trace) printk(" 0x%x .", wh);
	wh = ldw_sun4m_bypass(
			      ((wh & SRMMU_PTD_PTP_MASK) << 4)
			      + ((virt & SRMMU_IDX2_MASK) >> SRMMU_IDX2_SHIFT)*sizeof(pte_t));
	if ((wh & SRMMU_PTE_ET_MASK) == SRMMU_ET_INVALID) {
		if(trace) printk("\n");
		return 0;
	}
	if((wh & SRMMU_PTE_ET_MASK) == SRMMU_ET_PTE) {
		wh &= ~SRMMU_PTE_ET_MASK;
		wh |= 0x1;
		if(trace) printk("\n");
		return wh;
	}

	if(trace) printk(" 0x%x .", wh);
	wh = ldw_sun4m_bypass(
			      ((wh & SRMMU_PTD_PTP_MASK) << 4)
			      + ((virt & SRMMU_IDX3_MASK) >> SRMMU_IDX3_SHIFT)*sizeof(pte_t));
	if ((wh & SRMMU_PTE_ET_MASK) == SRMMU_ET_INVALID) {
		if(trace) printk("\n");
		return 0;
	}
	if(trace) printk(" 0x%x\n", wh);
	return wh;
}


/* Allocate a block of RAM which is aligned to its size.
 * This procedure can be used until the call to mem_init().
 *
 * To get around the elf bootloader nastyness we have a
 * early-on page table pool allocation area starting at
 * C_LABEL(pg0) which is 256k, this should be enough for now.
 */
static void *
srmmu_init_alloc(unsigned long *kbrk, unsigned size)
{
	register unsigned mask = size - 1;
	register unsigned long ret;

	if(size==0) return 0x0;
	if(size & mask) {
		printk("panic: srmmu_init_alloc botch\n");
		prom_halt();
	}
	ret = (*kbrk + mask) & ~mask;
	*kbrk = ret + size;
	memset((void*) ret, 0, size);
	return (void*) ret;
}

extern unsigned long srmmu_data_fault, srmmu_text_fault;

/* Patch in the SRMMU fault handlers for the trap table. */
void
srmmu_patch_fhandlers(void)
{
	/* Say the following ten times fast... */
	sparc_ttable[SP_TRAP_TFLT].inst_one = SPARC_MOV_CONST_L3(0x1);
	sparc_ttable[SP_TRAP_TFLT].inst_two =
		SPARC_BRANCH((unsigned long) &srmmu_text_fault, 
			     (unsigned long) &sparc_ttable[SP_TRAP_TFLT].inst_two);
	sparc_ttable[SP_TRAP_TFLT].inst_three = SPARC_RD_PSR_L0;
	sparc_ttable[SP_TRAP_TFLT].inst_four = SPARC_NOP;

	sparc_ttable[SP_TRAP_DFLT].inst_one = SPARC_MOV_CONST_L3(0x9);
	sparc_ttable[SP_TRAP_DFLT].inst_two =
		SPARC_BRANCH((unsigned long) &srmmu_data_fault,
			     (unsigned long) &sparc_ttable[SP_TRAP_DFLT].inst_two);
	sparc_ttable[SP_TRAP_DFLT].inst_three = SPARC_RD_PSR_L0;
	sparc_ttable[SP_TRAP_DFLT].inst_four = SPARC_NOP;

	return;
}

/* Paging initialization on the Sparc Reference MMU. */

/* This is all poorly designed, we cannot assume any pages are valid
 * past _end until *after* this routine runs, thus we can't use the
 * start_mem mechanism during initialization...
 */
static unsigned long mempool;

/* The following is global because trap_init needs it to fire up
 * the other cpu's on multiprocessors.
 */
pgd_t *lnx_root;      /* Pointer to the new root table */

extern char start[];

unsigned long
srmmu_paging_init(unsigned long start_mem, unsigned long end_mem)
{
	unsigned long vaddr;  /* Virtual counter */
	int i;

	pte_t *ptep = 0;
	pmd_t *pmdp = 0;
	pgd_t *pgdp = 0;

	mempool = start_mem;
	lnx_root = srmmu_init_alloc(&mempool, num_contexts*sizeof(pgd_t));

	memset(swapper_pg_dir, 0, PAGE_SIZE);

	/* For every entry in the new Linux context table, put in
	 * an entry which points to swapper_pg_dir .
	 */
	pmdp = (pmd_t *) swapper_pg_dir;
	for(i = 0; i < num_contexts; i++)
		srmmu_pgd_set(&lnx_root[i], pmdp);

	/* Make Linux physical page tables. */
	for(vaddr = KERNBASE; vaddr < end_mem; vaddr+=PAGE_SIZE) {
		pgdp = srmmu_pgd_offset(&init_task, vaddr);
		if(srmmu_pgd_none(*pgdp)) {
			pmdp = srmmu_init_alloc(&mempool,
						SRMMU_PTRS_PER_PMD*sizeof(pmd_t));
			srmmu_pgd_set(pgdp, pmdp);
		}

		pmdp = srmmu_pmd_offset(pgdp, vaddr);
		if(srmmu_pmd_none(*pmdp)) {
			ptep = srmmu_init_alloc(&mempool,
						SRMMU_PTRS_PER_PTE*sizeof(pte_t));
			srmmu_pmd_set(pmdp, ptep);
		}

		ptep = srmmu_pte_offset(pmdp, vaddr);
		*ptep = srmmu_mk_pte(vaddr, SRMMU_PAGE_KERNEL);
	}

	/* Map IO areas. */
	for(vaddr = IOBASE_VADDR; vaddr < (IOBASE_VADDR+IOBASE_LEN);
	    vaddr += SRMMU_PMD_SIZE) {
		pgdp = srmmu_pgd_offset(&init_task, vaddr);
		if(srmmu_pgd_none(*pgdp)) {
			pmdp = srmmu_init_alloc(&mempool,
						SRMMU_PTRS_PER_PMD*sizeof(pmd_t));
			srmmu_pgd_set(pgdp, pmdp);
		}
		pmdp = srmmu_pmd_offset(pgdp, vaddr);
		if(srmmu_pmd_none(*pmdp)) {
			ptep = srmmu_init_alloc(&mempool,
						SRMMU_PTRS_PER_PTE*sizeof(pte_t));
			srmmu_pmd_set(pmdp, ptep);
		}
	}

	/* Map in the PERCPU areas in virtual address space. */
	printk("PERCPU_VADDR + PERCPU_LEN = %08lx\n",
	       (PERCPU_VADDR + PERCPU_LEN));
	for(vaddr = PERCPU_VADDR; vaddr < (PERCPU_VADDR + PERCPU_LEN);
	    vaddr += PERCPU_ENTSIZE) {
		pgdp = srmmu_pgd_offset(&init_task, vaddr);
		if(srmmu_pgd_none(*pgdp)) {
			pmdp = srmmu_init_alloc(&mempool,
						SRMMU_PTRS_PER_PMD*sizeof(pmd_t));
			srmmu_pgd_set(pgdp, pmdp);
		}
		pmdp = srmmu_pmd_offset(pgdp, vaddr);
		if(srmmu_pmd_none(*pmdp)) {
			ptep = srmmu_init_alloc(&mempool,
						SRMMU_PTRS_PER_PTE*sizeof(pte_t));
			srmmu_pmd_set(pmdp, ptep);
		}
		ptep = srmmu_pte_offset(pmdp, vaddr);
		/* Per-cpu trap table page. */
		*ptep++ = srmmu_mk_pte((unsigned int) start, SRMMU_PAGE_KERNEL);
		/* Per-cpu kernel stack page. */
		*ptep++ = srmmu_mk_pte((unsigned int) srmmu_init_alloc(&mempool, PAGE_SIZE),
				       SRMMU_PAGE_KERNEL);
		/* Per-cpu Prom MBox. */
		*ptep++ = srmmu_mk_pte((unsigned int) srmmu_init_alloc(&mempool, PAGE_SIZE),
				       SRMMU_PAGE_KERNEL);
		/* Per-cpu state variables. */
		*ptep = srmmu_mk_pte((unsigned int) srmmu_init_alloc(&mempool, PAGE_SIZE),
				     SRMMU_PAGE_KERNEL);
	}
	percpu_table = (struct sparc_percpu *) PERCPU_VADDR;

	/* Ugh, have to map DVMA that the prom has mapped too or else
	 * you will lose with video cards when we take over the ctx table.
	 * Also, must take into consideration that prom might be using level
	 * two or one PTE's. TODO
	 */
	for(vaddr = KADB_DEBUGGER_BEGVM; vaddr != 0x0;) {
		unsigned int prom_pte;

		prom_pte = srmmu_init_twalk(vaddr, 0);

		if(prom_pte) {
			pgdp = srmmu_pgd_offset(&init_task, vaddr);
			if((prom_pte&0x3) == 0x0) {
				prom_pte &= ~0x3;
				prom_pte |= SRMMU_ET_PTE;
				pgd_val(*pgdp) = prom_pte;
				vaddr = SRMMU_PGDIR_ALIGN(vaddr+1);
				continue;
			}
			if(srmmu_pgd_none(*pgdp)) {
				pmdp = srmmu_init_alloc(&mempool,
							SRMMU_PTRS_PER_PMD*sizeof(pmd_t));
				srmmu_pgd_set(pgdp, pmdp);
			}

			pmdp = srmmu_pmd_offset(pgdp, vaddr);
			if((prom_pte&0x3) == 0x1) {
				prom_pte &= ~0x3;
				prom_pte |= SRMMU_ET_PTE;
				pgd_val(*pgdp) = prom_pte;
				vaddr = SRMMU_PMD_ALIGN(vaddr+1);
				continue;
			}
			if(srmmu_pmd_none(*pmdp)) {
				ptep = srmmu_init_alloc(&mempool,
							SRMMU_PTRS_PER_PTE*sizeof(pte_t));
				srmmu_pmd_set(pmdp, ptep);
			}
			/* A normal 3rd level PTE, no need to change ET bits. */
			ptep = srmmu_pte_offset(pmdp, vaddr);
			pte_val(*ptep) = prom_pte;

		}
		vaddr += PAGE_SIZE;
	}

	/* I believe I do not need to flush VAC here since my stores  */
        /* probably already reached the physical RAM.             --P3 */

	/* We probably do, and should do it just to be safe... -Davem */

	/* Take the MMU over from the PROM */
	printk("Taking over MMU from PROM.\n");

	srmmu_set_ctable_ptr(srmmu_virt_to_phys((unsigned)lnx_root));

	srmmu_flush_whole_tlb();

	/* Now it is ok to use memory at start_mem. */
	start_mem = PAGE_ALIGN(mempool);
	start_mem = free_area_init(start_mem, end_mem);
	start_mem = PAGE_ALIGN(start_mem);

#if 0
	printk("Testing context switches...\n");
	for(i=0; i<num_contexts; i++)
		srmmu_set_context(i);
	printk("done...\n");
	srmmu_set_context(0);
#endif

	printk("survived...\n");
	return start_mem;
}

/* Test the WP bit on the Sparc Reference MMU. */
void
srmmu_test_wp(void)
{
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;
	
	wp_works_ok = -1;
	/* We mapped page zero as a read-only page in paging_init()
	 * So fire up the test, then invalidate the pgd for page zero.
	 * It is no longer needed.
	 */

	/* Let it rip... */
	__asm__ __volatile__("st %%g0, [0x0]\n\t": : :"memory");
	if (wp_works_ok < 0)
		wp_works_ok = 0;

	pgdp = srmmu_pgd_offset(&init_task, 0x0);
	pgd_val(*pgdp) = 0x0;

	return;
}

/* Load up routines and constants for sun4m mmu */
void
ld_mmu_srmmu(void)
{
	printk("Loading srmmu MMU routines\n");

	/* First the constants */
	pmd_shift = SRMMU_PMD_SHIFT;
	pmd_size = SRMMU_PMD_SIZE;
	pmd_mask = SRMMU_PMD_MASK;
	pgdir_shift = SRMMU_PGDIR_SHIFT;
	pgdir_size = SRMMU_PGDIR_SIZE;
	pgdir_mask = SRMMU_PGDIR_MASK;

	ptrs_per_pte = SRMMU_PTRS_PER_PTE;
	ptrs_per_pmd = SRMMU_PTRS_PER_PMD;
	ptrs_per_pgd = SRMMU_PTRS_PER_PGD;

	page_none = SRMMU_PAGE_NONE;
	page_shared = SRMMU_PAGE_SHARED;
	page_copy = SRMMU_PAGE_COPY;
	page_readonly = SRMMU_PAGE_READONLY;
	page_kernel = SRMMU_PAGE_KERNEL;
	page_invalid = SRMMU_PAGE_INVALID;
	
	/* Functions */
	invalidate = srmmu_invalidate;
	switch_to_context = srmmu_switch_to_context;
	pmd_align = srmmu_pmd_align;
	pgdir_align = srmmu_pgdir_align;
	vmalloc_start = srmmu_vmalloc_start;

	pte_page = srmmu_pte_page;
	pmd_page = srmmu_pmd_page;
	pgd_page = srmmu_pgd_page;

	sparc_update_rootmmu_dir = srmmu_update_rootmmu_dir;

	pte_none = srmmu_pte_none;
	pte_present = srmmu_pte_present;
	pte_inuse = srmmu_pte_inuse;
	pte_clear = srmmu_pte_clear;
	pte_reuse = srmmu_pte_reuse;

	pmd_none = srmmu_pmd_none;
	pmd_bad = srmmu_pmd_bad;
	pmd_present = srmmu_pmd_present;
	pmd_inuse = srmmu_pmd_inuse;
	pmd_clear = srmmu_pmd_clear;
	pmd_reuse = srmmu_pmd_reuse;

	pgd_none = srmmu_pgd_none;
	pgd_bad = srmmu_pgd_bad;
	pgd_present = srmmu_pgd_present;
	pgd_inuse = srmmu_pgd_inuse;
	pgd_clear = srmmu_pgd_clear;
	pgd_reuse = srmmu_pgd_reuse;

	mk_pte = srmmu_mk_pte;
	pgd_set = srmmu_pgd_set;  /* XXX needs a cast */
	pte_modify = srmmu_pte_modify;
	pgd_offset = srmmu_pgd_offset;
	pmd_offset = srmmu_pmd_offset;
	pte_offset = srmmu_pte_offset;
	pte_free_kernel = srmmu_pte_free_kernel;
	pmd_free_kernel = srmmu_pmd_free_kernel;
	pte_alloc_kernel = srmmu_pte_alloc_kernel;
	pmd_alloc_kernel = srmmu_pmd_alloc_kernel;
	pte_free = srmmu_pte_free;
	pte_alloc = srmmu_pte_alloc;
	pmd_free = srmmu_pmd_free;
	pmd_alloc = srmmu_pmd_alloc;
	pgd_free = srmmu_pgd_free;
	pgd_alloc = srmmu_pgd_alloc;

	pte_read = srmmu_pte_read;
	pte_write = srmmu_pte_write;
	pte_exec = srmmu_pte_exec;
	pte_dirty = srmmu_pte_dirty;
	pte_young = srmmu_pte_young;
	pte_cow = srmmu_pte_cow;
	pte_wrprotect = srmmu_pte_wrprotect;
	pte_rdprotect = srmmu_pte_rdprotect;
	pte_exprotect = srmmu_pte_exprotect;
	pte_mkclean = srmmu_pte_mkclean;
	pte_mkold = srmmu_pte_mkold;
	pte_uncow = srmmu_pte_uncow;
	pte_mkwrite = srmmu_pte_mkwrite;
	pte_mkread = srmmu_pte_mkread;
	pte_mkexec = srmmu_pte_mkexec;
	pte_mkdirty = srmmu_pte_mkdirty;
	pte_mkyoung = srmmu_pte_mkyoung;
	pte_mkcow = srmmu_pte_mkcow;

	return;
}

