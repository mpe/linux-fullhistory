/*  $Id: init.c,v 1.103 1998/10/20 03:09:12 jj Exp $
 *  arch/sparc64/mm/init.c
 *
 *  Copyright (C) 1996,1997 David S. Miller (davem@caip.rutgers.edu)
 *  Copyright (C) 1997,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */
 
#include <linux/config.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <linux/blk.h>
#include <linux/swap.h>
#include <linux/swapctl.h>

#include <asm/head.h>
#include <asm/system.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/oplib.h>
#include <asm/iommu.h>
#include <asm/io.h>
#include <asm/mmu_context.h>
#include <asm/vaddrs.h>

/* Turn this off if you suspect some place in some physical memory hole
   might get into page tables (something would be broken very much). */
   
#define FREE_UNUSED_MEM_MAP

extern void show_net_buffers(void);
extern unsigned long device_scan(unsigned long);

struct sparc_phys_banks sp_banks[SPARC_PHYS_BANKS];

/* Ugly, but necessary... -DaveM */
unsigned long phys_base;

/* get_new_mmu_context() uses "cache + 1".  */
unsigned long tlb_context_cache = CTX_FIRST_VERSION - 1;

/* References to section boundaries */
extern char __init_begin, __init_end, etext, __bss_start;

int do_check_pgt_cache(int low, int high)
{
        int freed = 0;

	if(pgtable_cache_size > high) {
		do {
#ifdef __SMP__
			if(pgd_quicklist)
				free_pgd_slow(get_pgd_fast()), freed++;
#endif
			if(pte_quicklist)
				free_pte_slow(get_pte_fast()), freed++;
		} while(pgtable_cache_size > low);
	}
#ifndef __SMP__ 
        if (pgd_cache_size > high / 4) {
		struct page *page, *page2;
                for (page2 = NULL, page = (struct page *)pgd_quicklist; page;) {
                        if ((unsigned long)page->pprev_hash == 3) {
                                if (page2)
                                        page2->next_hash = page->next_hash;
                                else
                                        (struct page *)pgd_quicklist = page->next_hash;
                                page->next_hash = NULL;
                                page->pprev_hash = NULL;
                                pgd_cache_size -= 2;
                                free_page(PAGE_OFFSET + (page->map_nr << PAGE_SHIFT));
                                freed++;
                                if (page2)
                                        page = page2->next_hash;
                                else
                                        page = (struct page *)pgd_quicklist;
                                if (pgd_cache_size <= low / 4)
                                        break;
                                continue;
                        }
                        page2 = page;
                        page = page->next_hash;
                }
        }
#endif
        return freed;
}

/*
 * BAD_PAGE is the page that is used for page faults when linux
 * is out-of-memory. Older versions of linux just did a
 * do_exit(), but using this instead means there is less risk
 * for a process dying in kernel mode, possibly leaving an inode
 * unused etc..
 *
 * BAD_PAGETABLE is the accompanying page-table: it is initialized
 * to point to BAD_PAGE entries.
 *
 * ZERO_PAGE is a special page that is used for zero-initialized
 * data and COW.
 */
pte_t __bad_page(void)
{
	memset((void *) &empty_bad_page, 0, PAGE_SIZE);
	return pte_mkdirty(mk_pte((((unsigned long) &empty_bad_page) 
		- ((unsigned long)&empty_zero_page) + phys_base + PAGE_OFFSET),
				  PAGE_SHARED));
}

void show_mem(void)
{
	int free = 0,total = 0,reserved = 0;
	int shared = 0, cached = 0;
	struct page *page, *end;

	printk("\nMem-info:\n");
	show_free_areas();
	printk("Free swap:       %6dkB\n",nr_swap_pages<<(PAGE_SHIFT-10));
	for (page = mem_map, end = mem_map + max_mapnr;
	     page < end; page++) {
		if (PageSkip(page)) {
			if (page->next_hash < page)
				break;
			page = page->next_hash;
		}
		total++;
		if (PageReserved(page))
			reserved++;
		else if (PageSwapCache(page))
			cached++;
		else if (!atomic_read(&page->count))
			free++;
		else
			shared += atomic_read(&page->count) - 1;
	}
	printk("%d pages of RAM\n",total);
	printk("%d free pages\n",free);
	printk("%d reserved pages\n",reserved);
	printk("%d pages shared\n",shared);
	printk("%d pages swap cached\n",cached);
	printk("%ld pages in page table cache\n",pgtable_cache_size);
#ifndef __SMP__
	printk("%ld entries in page dir cache\n",pgd_cache_size);
#endif	
	show_buffers();
#ifdef CONFIG_NET
	show_net_buffers();
#endif
}

/* IOMMU support, the ideas are right, the code should be cleaned a bit still... */

/* This keeps track of pages used in sparc_alloc_dvma() invocations. */
/* NOTE: All of these are inited to 0 in bss, don't need to make data segment bigger */
#define DVMAIO_SIZE 0x2000000
static unsigned long dvma_map_pages[DVMAIO_SIZE >> 16];
static unsigned long dvma_pages_current_offset;
static int dvma_pages_current_index;
static unsigned long dvmaiobase = 0;
static unsigned long dvmaiosz __initdata = 0;

/* #define E3000_DEBUG */

__initfunc(void dvmaio_init(void))
{
	int i;
	
	if (!dvmaiobase) {
		for (i = 0; sp_banks[i].num_bytes != 0; i++)
			if (sp_banks[i].base_addr + sp_banks[i].num_bytes > dvmaiobase)
				dvmaiobase = sp_banks[i].base_addr + sp_banks[i].num_bytes;
		dvmaiobase = (dvmaiobase + DVMAIO_SIZE + 0x400000 - 1) & ~(0x400000 - 1);
		for (i = 0; i < 6; i++)
			if (dvmaiobase <= ((1024 * 64 * 1024) << i))
				break;
		dvmaiobase = ((1024 * 64 * 1024) << i) - DVMAIO_SIZE;
		dvmaiosz = i;
	}
}

__initfunc(void iommu_init(int iommu_node, struct linux_sbus *sbus))
{
	struct iommu_struct *iommu;
	struct sysio_regs *sregs;
	struct linux_prom64_registers rprop;
	unsigned long impl, vers;
	unsigned long control, tsbbase;
	unsigned long tsbbases[32];
	unsigned long *iopte;
	int err, i, j;
	
	dvmaio_init();
#ifdef E3000_DEBUG
	prom_printf("\niommu_init: [%x:%p] ",
		    iommu_node, sbus);
#endif
	err = prom_getproperty(iommu_node, "reg", (char *)&rprop,
			       sizeof(rprop));
	if(err == -1) {
		prom_printf("iommu_init: Cannot map SYSIO control registers.\n");
		prom_halt();
	}
	sregs = (struct sysio_regs *) __va(rprop.phys_addr);

#ifdef E3000_DEBUG
	prom_printf("sregs[%p]\n");
#endif
	if(!sregs) {
		prom_printf("iommu_init: Fatal error, sysio regs not mapped\n");
		prom_halt();
	}

	iommu = kmalloc(sizeof(struct iommu_struct), GFP_ATOMIC);

#ifdef E3000_DEBUG
	prom_printf("iommu_init: iommu[%p] ", iommu);
#endif

	spin_lock_init(&iommu->iommu_lock);
	iommu->sysio_regs = sregs;
	sbus->iommu = iommu;

	control = sregs->iommu_control;
	impl = (control & IOMMU_CTRL_IMPL) >> 60;
	vers = (control & IOMMU_CTRL_VERS) >> 56;
#ifdef E3000_DEBUG
	prom_printf("sreg_control[%08x]\n", control);
	prom_printf("IOMMU: IMPL[%x] VERS[%x] SYSIO mapped at %016lx\n",
		    (unsigned int) impl, (unsigned int)vers, (unsigned long) sregs);
#endif
	printk("IOMMU(SBUS): IMPL[%x] VERS[%x] SYSIO mapped at %016lx\n",
	       (unsigned int) impl, (unsigned int)vers, (unsigned long) sregs);
	
	control &= ~(IOMMU_CTRL_TSBSZ);
	control |= ((IOMMU_TSBSZ_2K * dvmaiosz) | IOMMU_CTRL_TBWSZ | IOMMU_CTRL_ENAB);

	/* Use only 64k pages, things are layed out in the 32-bit SBUS
	 * address space like this:
	 *
	 * 0x00000000	  ----------------------------------------
	 *		  | Direct physical mappings for most    |
	 *                | DVMA to paddr's within this range    |
	 * dvmaiobase     ----------------------------------------
	 * 		  | For mappings requested via           |
	 *                | sparc_alloc_dvma()		         |
	 * dvmaiobase+32M ----------------------------------------
	 *
	 * NOTE: we need to order 2 contiguous order 5, that's the largest
	 *       chunk page_alloc will give us.   -JJ */
	tsbbase = 0;
	if (dvmaiosz == 6) {
		memset (tsbbases, 0, sizeof(tsbbases));
		for (i = 0; i < 32; i++) {
			tsbbases[i] = __get_free_pages(GFP_DMA, 5);
			for (j = 0; j < i; j++)
				if (tsbbases[j] == tsbbases[i] + 32768*sizeof(iopte_t)) {
					tsbbase = tsbbases[i];
					break;
				} else if (tsbbases[i] == tsbbases[j] + 32768*sizeof(iopte_t)) {
					tsbbase = tsbbases[j];
					break;
				}
			if (tsbbase) {
				tsbbases[i] = 0;
				tsbbases[j] = 0;
				break;
			}
		}
		for (i = 0; i < 32; i++)
			if (tsbbases[i])
				free_pages(tsbbases[i], 5);
	} else
		tsbbase = __get_free_pages(GFP_DMA, dvmaiosz);
	if (!tsbbase) {
		prom_printf("Strange. Could not allocate 512K of contiguous RAM.\n");
		prom_halt();
	}
	iommu->page_table = (iopte_t *) tsbbase;
	iopte = (unsigned long *) tsbbase;

	/* Setup aliased mappings... */
	for(i = 0; i < (dvmaiobase >> 16); i++) {
		*iopte  = (IOPTE_VALID | IOPTE_64K | IOPTE_STBUF |
			   IOPTE_CACHE | IOPTE_WRITE);
		*iopte |= (i << 16);
		iopte++;
	}

	/* Clear all sparc_alloc_dvma() maps. */
	for( ; i < ((dvmaiobase + DVMAIO_SIZE) >> 16); i++)
		*iopte++ = 0;

#ifdef E3000_DEBUG
	prom_printf("IOMMU: pte's mapped, enabling IOMMU... ");
#endif
	sregs->iommu_tsbbase = __pa(tsbbase);
	sregs->iommu_control = control;

#ifdef E3000_DEBUG
	prom_printf("done\n");
#endif
	/* Get the streaming buffer going. */
	control = sregs->sbuf_control;
	impl = (control & SYSIO_SBUFCTRL_IMPL) >> 60;
	vers = (control & SYSIO_SBUFCTRL_REV) >> 56;
#ifdef E3000_DEBUG
	prom_printf("IOMMU: enabling streaming buffer, control[%08x]... ",
		    control);
#endif
	printk("IOMMU: Streaming Buffer IMPL[%x] REV[%x] ",
	       (unsigned int)impl, (unsigned int)vers);
	iommu->sbuf_flushflag_va = kmalloc(sizeof(unsigned long), GFP_DMA);
	printk("FlushFLAG[%016lx] ... ", (iommu->sbuf_flushflag_pa = __pa(iommu->sbuf_flushflag_va)));
	*(iommu->sbuf_flushflag_va) = 0;

	sregs->sbuf_control = (control | SYSIO_SBUFCTRL_SB_EN);

#ifdef E3000_DEBUG
	prom_printf("done, returning\n");
#endif
	printk("ENABLED\n");

	/* Finally enable DVMA arbitration for all devices, just in case. */
	sregs->sbus_control |= SYSIO_SBCNTRL_AEN;
}

void mmu_map_dma_area(unsigned long addr, int len, __u32 *dvma_addr,
		      struct linux_sbus *sbus)
{
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;

	/* Find out if we need to grab some pages. */
	if(!dvma_map_pages[dvma_pages_current_index] ||
	   ((dvma_pages_current_offset + len) > (1 << 16))) {
		struct linux_sbus *sbus;
		unsigned long *iopte;
		unsigned long newpages = __get_free_pages(GFP_KERNEL, 3);
		int i;

		if(!newpages)
			panic("AIEEE cannot get DVMA pages.");

		memset((char *)newpages, 0, (1 << 16));

		if(!dvma_map_pages[dvma_pages_current_index]) {
			dvma_map_pages[dvma_pages_current_index] = newpages;
			i = dvma_pages_current_index;
		} else {
			dvma_map_pages[dvma_pages_current_index + 1] = newpages;
			i = dvma_pages_current_index + 1;
		}

		/* Stick it in the IOMMU. */
		i = (dvmaiobase >> 16) + i;
		for_each_sbus(sbus) {
			struct iommu_struct *iommu = sbus->iommu;
			unsigned long flags;

			spin_lock_irqsave(&iommu->iommu_lock, flags);
			iopte = (unsigned long *)(iommu->page_table + i);
			*iopte  = (IOPTE_VALID | IOPTE_64K | IOPTE_CACHE | IOPTE_WRITE);
			*iopte |= __pa(newpages);
			spin_unlock_irqrestore(&iommu->iommu_lock, flags);
		}
	}

	/* Get this out of the way. */
	*dvma_addr = (__u32) ((dvmaiobase) +
			      (dvma_pages_current_index << 16) +
			      (dvma_pages_current_offset));

	while(len > 0) {
		while((len > 0) && (dvma_pages_current_offset < (1 << 16))) {
			pte_t pte;
			unsigned long the_page =
				dvma_map_pages[dvma_pages_current_index] +
				dvma_pages_current_offset;

			/* Map the CPU's view. */
			pgdp = pgd_offset(init_task.mm, addr);
			pmdp = pmd_alloc_kernel(pgdp, addr);
			ptep = pte_alloc_kernel(pmdp, addr);
			pte = mk_pte(the_page, PAGE_KERNEL);
			set_pte(ptep, pte);

			dvma_pages_current_offset += PAGE_SIZE;
			addr += PAGE_SIZE;
			len -= PAGE_SIZE;
		}
		dvma_pages_current_index++;
		dvma_pages_current_offset = 0;
	}
}

__u32 mmu_get_scsi_one(char *vaddr, unsigned long len, struct linux_sbus *sbus)
{
	__u32 sbus_addr = (__u32) __pa(vaddr);

#ifndef DEBUG_IOMMU
	return sbus_addr;
#else
	if((sbus_addr < dvmaiobase) &&
	   ((sbus_addr + len) < dvmaiobase))
		return sbus_addr;

	/* "can't happen"... GFP_DMA assures this. */
	panic("Very high scsi_one mappings should never happen.");
        return (__u32)0;
#endif        
}

void mmu_release_scsi_one(u32 vaddr, unsigned long len, struct linux_sbus *sbus)
{
	struct iommu_struct *iommu = sbus->iommu;
	struct sysio_regs *sregs = iommu->sysio_regs;
	unsigned long start = (unsigned long) vaddr;
	unsigned long end = PAGE_ALIGN(start + len);
	unsigned long flags;
	unsigned int *sync_word;

	start &= PAGE_MASK;

	spin_lock_irqsave(&iommu->iommu_lock, flags);

	while(start < end) {
		sregs->sbuf_pflush = start;
		start += PAGE_SIZE;
	}
	sync_word = iommu->sbuf_flushflag_va;
	sregs->sbuf_fsync = iommu->sbuf_flushflag_pa;
	membar("#StoreLoad | #MemIssue");
	while((*sync_word & 0x1) == 0)
		membar("#LoadLoad");
	*sync_word = 0;

	spin_unlock_irqrestore(&iommu->iommu_lock, flags);
}

void mmu_get_scsi_sgl(struct mmu_sglist *sg, int sz, struct linux_sbus *sbus)
{
	while(sz >= 0) {
		__u32 page = (__u32) __pa(((unsigned long) sg[sz].addr));
#ifndef DEBUG_IOMMU
		sg[sz].dvma_addr = page;
#else		
		if((page < dvmaiobase) &&
		   (page + sg[sz].len) < dvmaiobase) {
			sg[sz].dvma_addr = page;
		} else {
			/* "can't happen"... GFP_DMA assures this. */
			panic("scsi_sgl high mappings should never happen.");
		}
#endif
		sz--;
	}
}

void mmu_release_scsi_sgl(struct mmu_sglist *sg, int sz, struct linux_sbus *sbus)
{
	struct iommu_struct *iommu = sbus->iommu;
	struct sysio_regs *sregs = iommu->sysio_regs;
	unsigned long flags;
	unsigned int *sync_word;

	spin_lock_irqsave(&iommu->iommu_lock, flags);

	while(sz >= 0) {
		unsigned long start = sg[sz].dvma_addr;
		unsigned long end = PAGE_ALIGN(start + sg[sz].len);

		start &= PAGE_MASK;
		while(start < end) {
			sregs->sbuf_pflush = start;
			start += PAGE_SIZE;
		}
		sz--;
	}
	sync_word = iommu->sbuf_flushflag_va;
	sregs->sbuf_fsync = iommu->sbuf_flushflag_pa;
	membar("#StoreLoad | #MemIssue");
	while((*sync_word & 0x1) == 0)
		membar("#LoadLoad");
	*sync_word = 0;

	spin_unlock_irqrestore(&iommu->iommu_lock, flags);
}

int mmu_info(char *buf)
{
	/* We'll do the rest later to make it nice... -DaveM */
	return sprintf(buf, "MMU Type\t: Spitfire\n");
}

static unsigned long mempool;

struct linux_prom_translation {
	unsigned long virt;
	unsigned long size;
	unsigned long data;
};

static inline void inherit_prom_mappings(void)
{
	struct linux_prom_translation *trans;
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;
	int node, n, i;

	node = prom_finddevice("/virtual-memory");
	n = prom_getproplen(node, "translations");
	if (n == 0 || n == -1) {
		prom_printf("Couldn't get translation property\n");
		prom_halt();
	}

	for (i = 1; i < n; i <<= 1) /* empty */;
	trans = sparc_init_alloc(&mempool, i);

	if (prom_getproperty(node, "translations", (char *)trans, i) == -1) {
		prom_printf("Couldn't get translation property\n");
		prom_halt();
	}
	n = n / sizeof(*trans);

	for (i = 0; i < n; i++) {
		unsigned long vaddr;
		
		if (trans[i].virt >= 0xf0000000 && trans[i].virt < 0x100000000) {
			for (vaddr = trans[i].virt;
			     vaddr < trans[i].virt + trans[i].size;
			     vaddr += PAGE_SIZE) {
				pgdp = pgd_offset(init_task.mm, vaddr);
				if (pgd_none(*pgdp)) {
					pmdp = sparc_init_alloc(&mempool,
							 PMD_TABLE_SIZE);
					memset(pmdp, 0, PAGE_SIZE);
					pgd_set(pgdp, pmdp);
				}
				pmdp = pmd_offset(pgdp, vaddr);
				if (pmd_none(*pmdp)) {
					ptep = sparc_init_alloc(&mempool,
							 PTE_TABLE_SIZE);
					pmd_set(pmdp, ptep);
				}
				ptep = pte_offset(pmdp, vaddr);
				set_pte (ptep, __pte(trans[i].data | _PAGE_MODIFIED));
				trans[i].data += PAGE_SIZE;
			}
		}
	}
}

/* The OBP specifications for sun4u mark 0xfffffffc00000000 and
 * upwards as reserved for use by the firmware (I wonder if this
 * will be the same on Cheetah...).  We use this virtual address
 * range for the VPTE table mappings of the nucleus so we need
 * to zap them when we enter the PROM.  -DaveM
 */
static void __flush_nucleus_vptes(void)
{
	unsigned long pstate;
	unsigned long prom_reserved_base = 0xfffffffc00000000UL;
	int i;

	__asm__ __volatile__("flushw\n\t"
			     "rdpr	%%pstate, %0\n\t"
			     "wrpr	%0, %1, %%pstate"
			     : "=r" (pstate)
			     : "i" (PSTATE_IE));

	/* Only DTLB must be checked for VPTE entries. */
	for(i = 0; i < 63; i++) {
		unsigned long tag = spitfire_get_dtlb_tag(i);

		if(((tag & ~(PAGE_MASK)) == 0) &&
		   ((tag &  (PAGE_MASK)) >= prom_reserved_base)) {
			__asm__ __volatile__("stxa %%g0, [%0] %1"
					     : /* no outputs */
					     : "r" (TLB_TAG_ACCESS), "i" (ASI_DMMU));
			membar("#Sync");
			spitfire_put_dtlb_data(i, 0x0UL);
			membar("#Sync");
		}
	}
	__asm__ __volatile__("wrpr	%0, 0, %%pstate"
			     : : "r" (pstate));
}

static int prom_ditlb_set = 0;
struct prom_tlb_entry {
	int		tlb_ent;
	unsigned long	tlb_tag;
	unsigned long	tlb_data;
};
struct prom_tlb_entry prom_itlb[8], prom_dtlb[8];

void prom_world(int enter)
{
	int i;

	if (!prom_ditlb_set)
		return;
	if (enter) {
		/* Kick out nucleus VPTEs. */
		__flush_nucleus_vptes();

		/* Install PROM world. */
		for (i = 0; i < 8; i++) {
			if (prom_dtlb[i].tlb_ent != -1) {
				__asm__ __volatile__("stxa %0, [%1] %2"
					: : "r" (prom_dtlb[i].tlb_tag), "r" (TLB_TAG_ACCESS),
					"i" (ASI_DMMU));
				membar("#Sync");
				spitfire_put_dtlb_data(prom_dtlb[i].tlb_ent,
						       prom_dtlb[i].tlb_data);
				membar("#Sync");
			}

			if (prom_itlb[i].tlb_ent != -1) {
				__asm__ __volatile__("stxa %0, [%1] %2"
					: : "r" (prom_itlb[i].tlb_tag), "r" (TLB_TAG_ACCESS),
					"i" (ASI_IMMU));
				membar("#Sync");
				spitfire_put_itlb_data(prom_itlb[i].tlb_ent,
						       prom_itlb[i].tlb_data);
				membar("#Sync");
			}
		}
	} else {
		for (i = 0; i < 8; i++) {
			if (prom_dtlb[i].tlb_ent != -1) {
				__asm__ __volatile__("stxa %%g0, [%0] %1"
					: : "r" (TLB_TAG_ACCESS), "i" (ASI_DMMU));
				membar("#Sync");
				spitfire_put_dtlb_data(prom_dtlb[i].tlb_ent, 0x0UL);
				membar("#Sync");
			}
			if (prom_itlb[i].tlb_ent != -1) {
				__asm__ __volatile__("stxa %%g0, [%0] %1"
					: : "r" (TLB_TAG_ACCESS), "i" (ASI_IMMU));
				membar("#Sync");
				spitfire_put_itlb_data(prom_itlb[i].tlb_ent, 0x0UL);
				membar("#Sync");
			}
		}
	}
}

void inherit_locked_prom_mappings(int save_p)
{
	int i;
	int dtlb_seen = 0;
	int itlb_seen = 0;

	/* Fucking losing PROM has more mappings in the TLB, but
	 * it (conveniently) fails to mention any of these in the
	 * translations property.  The only ones that matter are
	 * the locked PROM tlb entries, so we impose the following
	 * irrecovable rule on the PROM, it is allowed 8 locked
	 * entries in the ITLB and 8 in the DTLB.
	 *
	 * Supposedly the upper 16GB of the address space is
	 * reserved for OBP, BUT I WISH THIS WAS DOCUMENTED
	 * SOMEWHERE!!!!!!!!!!!!!!!!!  Furthermore the entire interface
	 * used between the client program and the firmware on sun5
	 * systems to coordinate mmu mappings is also COMPLETELY
	 * UNDOCUMENTED!!!!!! Thanks S(t)un!
	 */
	if (save_p) {
		for(i = 0; i < 8; i++) {
			prom_dtlb[i].tlb_ent = -1;
			prom_itlb[i].tlb_ent = -1;
		}
	}
	for(i = 0; i < 63; i++) {
		unsigned long data;

		data = spitfire_get_dtlb_data(i);
		if(data & _PAGE_L) {
			unsigned long tag = spitfire_get_dtlb_tag(i);

			if(save_p) {
				prom_dtlb[dtlb_seen].tlb_ent = i;
				prom_dtlb[dtlb_seen].tlb_tag = tag;
				prom_dtlb[dtlb_seen].tlb_data = data;
			}
			__asm__ __volatile__("stxa %%g0, [%0] %1"
					     : : "r" (TLB_TAG_ACCESS), "i" (ASI_DMMU));
			membar("#Sync");
			spitfire_put_dtlb_data(i, 0x0UL);
			membar("#Sync");

			dtlb_seen++;
			if(dtlb_seen > 7)
				break;
		}
	}
	for(i = 0; i < 63; i++) {
		unsigned long data;

		data = spitfire_get_itlb_data(i);
		if(data & _PAGE_L) {
			unsigned long tag = spitfire_get_itlb_tag(i);

			if(save_p) {
				prom_itlb[itlb_seen].tlb_ent = i;
				prom_itlb[itlb_seen].tlb_tag = tag;
				prom_itlb[itlb_seen].tlb_data = data;
			}
			__asm__ __volatile__("stxa %%g0, [%0] %1"
					     : : "r" (TLB_TAG_ACCESS), "i" (ASI_IMMU));
			membar("#Sync");
			spitfire_put_itlb_data(i, 0x0UL);
			membar("#Sync");

			itlb_seen++;
			if(itlb_seen > 7)
				break;
		}
	}
	if (save_p)
		prom_ditlb_set = 1;
}

/* Give PROM back his world, done during reboots... */
void prom_reload_locked(void)
{
	int i;

	for (i = 0; i < 8; i++) {
		if (prom_dtlb[i].tlb_ent != -1) {
			__asm__ __volatile__("stxa %0, [%1] %2"
				: : "r" (prom_dtlb[i].tlb_tag), "r" (TLB_TAG_ACCESS),
				"i" (ASI_DMMU));
			membar("#Sync");
			spitfire_put_dtlb_data(prom_dtlb[i].tlb_ent,
					       prom_dtlb[i].tlb_data);
			membar("#Sync");
		}

		if (prom_itlb[i].tlb_ent != -1) {
			__asm__ __volatile__("stxa %0, [%1] %2"
				: : "r" (prom_itlb[i].tlb_tag), "r" (TLB_TAG_ACCESS),
				"i" (ASI_IMMU));
			membar("#Sync");
			spitfire_put_itlb_data(prom_itlb[i].tlb_ent,
					       prom_itlb[i].tlb_data);
			membar("#Sync");
		}
	}
}

void __flush_dcache_range(unsigned long start, unsigned long end)
{
	unsigned long va;
	int n = 0;

	for (va = start; va < end; va += 32) {
		spitfire_put_dcache_tag(va & 0x3fe0, 0x0);
		if (++n >= 512)
			break;
	}
}

void __flush_cache_all(void)
{
	unsigned long va;

	flushw_all();
	for(va =  0; va < (PAGE_SIZE << 1); va += 32)
		spitfire_put_icache_tag(va, 0x0);
}

/* If not locked, zap it. */
void __flush_tlb_all(void)
{
	unsigned long pstate;
	int i;

	__asm__ __volatile__("flushw\n\t"
			     "rdpr	%%pstate, %0\n\t"
			     "wrpr	%0, %1, %%pstate"
			     : "=r" (pstate)
			     : "i" (PSTATE_IE));
	for(i = 0; i < 64; i++) {
		if(!(spitfire_get_dtlb_data(i) & _PAGE_L)) {
			__asm__ __volatile__("stxa %%g0, [%0] %1"
					     : /* no outputs */
					     : "r" (TLB_TAG_ACCESS), "i" (ASI_DMMU));
			membar("#Sync");
			spitfire_put_dtlb_data(i, 0x0UL);
			membar("#Sync");
		}
		if(!(spitfire_get_itlb_data(i) & _PAGE_L)) {
			__asm__ __volatile__("stxa %%g0, [%0] %1"
					     : /* no outputs */
					     : "r" (TLB_TAG_ACCESS), "i" (ASI_IMMU));
			membar("#Sync");
			spitfire_put_itlb_data(i, 0x0UL);
			membar("#Sync");
		}
	}
	__asm__ __volatile__("wrpr	%0, 0, %%pstate"
			     : : "r" (pstate));
}

#define CTX_BMAP_SLOTS (1UL << (CTX_VERSION_SHIFT - 6))
unsigned long mmu_context_bmap[CTX_BMAP_SLOTS];

/* We are always protected by scheduler_lock under SMP.
 * Caller does TLB context flushing on local CPU if necessary.
 *
 * We must be careful about boundary cases so that we never
 * let the user have CTX 0 (nucleus) or we ever use a CTX
 * version of zero (and thus NO_CONTEXT would not be caught
 * by version mis-match tests in mmu_context.h).
 */
void get_new_mmu_context(struct mm_struct *mm)
{
	unsigned long ctx = (tlb_context_cache + 1) & ~(CTX_VERSION_MASK);
	unsigned long new_ctx;
	
	if (ctx == 0)
		ctx = 1;
	if ((mm->context != NO_CONTEXT) &&
	    !((mm->context ^ tlb_context_cache) & CTX_VERSION_MASK))
		clear_bit(mm->context & ~(CTX_VERSION_MASK), mmu_context_bmap);
	new_ctx = find_next_zero_bit(mmu_context_bmap, 1UL << CTX_VERSION_SHIFT, ctx);
	if (new_ctx >= (1UL << CTX_VERSION_SHIFT)) {
		new_ctx = find_next_zero_bit(mmu_context_bmap, ctx, 1);
		if (new_ctx >= ctx) {
			int i;
			new_ctx = (tlb_context_cache & CTX_VERSION_MASK) +
				CTX_FIRST_VERSION;
			if (new_ctx == 1)
				new_ctx = CTX_FIRST_VERSION;

			/* Don't call memset, for 16 entries that's just
			 * plain silly...
			 */
			mmu_context_bmap[0] = 3;
			mmu_context_bmap[1] = 0;
			mmu_context_bmap[2] = 0;
			mmu_context_bmap[3] = 0;
			for(i = 4; i < CTX_BMAP_SLOTS; i += 4) {
				mmu_context_bmap[i + 0] = 0;
				mmu_context_bmap[i + 1] = 0;
				mmu_context_bmap[i + 2] = 0;
				mmu_context_bmap[i + 3] = 0;
			}
			goto out;
		}
	}
	set_bit(new_ctx, mmu_context_bmap);
	new_ctx |= (tlb_context_cache & CTX_VERSION_MASK);
out:
	tlb_context_cache = new_ctx;
	mm->context = new_ctx;
	mm->cpu_vm_mask = 0;
}

#ifndef __SMP__
struct pgtable_cache_struct pgt_quicklists;
#endif

/* XXX Add __GFP_HIGH to these calls to "fool" page allocator
 * XXX so we don't go to swap so quickly... then do the same
 * XXX for get_user_page as well -DaveM
 */
pmd_t *get_pmd_slow(pgd_t *pgd, unsigned long offset)
{
	pmd_t *pmd;

	pmd = (pmd_t *) __get_free_page(GFP_DMA|GFP_KERNEL);
	if(pmd) {
		memset(pmd, 0, PAGE_SIZE);
		pgd_set(pgd, pmd);
		return pmd + offset;
	}
	return NULL;
}

pte_t *get_pte_slow(pmd_t *pmd, unsigned long offset)
{
	pte_t *pte;

	pte = (pte_t *) __get_free_page(GFP_DMA|GFP_KERNEL);
	if(pte) {
		memset(pte, 0, PAGE_SIZE);
		pmd_set(pmd, pte);
		return pte + offset;
	}
	return NULL;
}

__initfunc(static void
allocate_ptable_skeleton(unsigned long start, unsigned long end))
{
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;

	while (start < end) {
		pgdp = pgd_offset(init_task.mm, start);
		if (pgd_none(*pgdp)) {
			pmdp = sparc_init_alloc(&mempool, PAGE_SIZE);
			memset(pmdp, 0, PAGE_SIZE);
			pgd_set(pgdp, pmdp);
		}
		pmdp = pmd_offset(pgdp, start);
		if (pmd_none(*pmdp)) {
			ptep = sparc_init_alloc(&mempool, PAGE_SIZE);
			memset(ptep, 0, PAGE_SIZE);
			pmd_set(pmdp, ptep);
		}
		start = (start + PMD_SIZE) & PMD_MASK;
	}
}

/*
 * Create a mapping for an I/O register.  Have to make sure the side-effect
 * bit is set.
 */
 
void sparc_ultra_mapioaddr(unsigned long physaddr, unsigned long virt_addr,
			   int bus, int rdonly)
{
	pgd_t *pgdp = pgd_offset(init_task.mm, virt_addr);
	pmd_t *pmdp = pmd_offset(pgdp, virt_addr);
	pte_t *ptep = pte_offset(pmdp, virt_addr);
	pte_t pte;

	physaddr &= PAGE_MASK;

	if(rdonly)
		pte = mk_pte_phys(physaddr, __pgprot(pg_iobits | __PRIV_BITS));
	else
		pte = mk_pte_phys(physaddr, __pgprot(pg_iobits | __DIRTY_BITS | __PRIV_BITS));

	set_pte(ptep, pte);
}

/* XXX no longer used, remove me... -DaveM */
void sparc_ultra_unmapioaddr(unsigned long virt_addr)
{
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;

	pgdp = pgd_offset(init_task.mm, virt_addr);
	pmdp = pmd_offset(pgdp, virt_addr);
	ptep = pte_offset(pmdp, virt_addr);

	/* No need to flush uncacheable page. */
	pte_clear(ptep);
}

void sparc_ultra_dump_itlb(void)
{
        int slot;

        printk ("Contents of itlb: ");
	for (slot = 0; slot < 14; slot++) printk ("    ");
	printk ("%2x:%016lx,%016lx\n", 0, spitfire_get_itlb_tag(0), spitfire_get_itlb_data(0));
        for (slot = 1; slot < 64; slot+=3) {
        	printk ("%2x:%016lx,%016lx %2x:%016lx,%016lx %2x:%016lx,%016lx\n", 
        		slot, spitfire_get_itlb_tag(slot), spitfire_get_itlb_data(slot),
        		slot+1, spitfire_get_itlb_tag(slot+1), spitfire_get_itlb_data(slot+1),
        		slot+2, spitfire_get_itlb_tag(slot+2), spitfire_get_itlb_data(slot+2));
        }
}

void sparc_ultra_dump_dtlb(void)
{
        int slot;

        printk ("Contents of dtlb: ");
	for (slot = 0; slot < 14; slot++) printk ("    ");
	printk ("%2x:%016lx,%016lx\n", 0, spitfire_get_dtlb_tag(0),
		spitfire_get_dtlb_data(0));
        for (slot = 1; slot < 64; slot+=3) {
        	printk ("%2x:%016lx,%016lx %2x:%016lx,%016lx %2x:%016lx,%016lx\n", 
        		slot, spitfire_get_dtlb_tag(slot), spitfire_get_dtlb_data(slot),
        		slot+1, spitfire_get_dtlb_tag(slot+1), spitfire_get_dtlb_data(slot+1),
        		slot+2, spitfire_get_dtlb_tag(slot+2), spitfire_get_dtlb_data(slot+2));
        }
}

/* paging_init() sets up the page tables */

extern unsigned long free_area_init(unsigned long, unsigned long);

__initfunc(unsigned long 
paging_init(unsigned long start_mem, unsigned long end_mem))
{
	extern void setup_tba(void);
	extern pmd_t swapper_pmd_dir[1024];
	extern unsigned long irq_init(unsigned long start_mem, unsigned long end_mem);
	extern unsigned int sparc64_vpte_patchme[1];
	unsigned long alias_base = phys_base + PAGE_OFFSET;
	unsigned long pt;
	unsigned long flags;
	unsigned long shift = alias_base - ((unsigned long)&empty_zero_page);

	set_bit(0, mmu_context_bmap);
	/* We assume physical memory starts at some 4mb multiple,
	 * if this were not true we wouldn't boot up to this point
	 * anyways.
	 */
	pt  = phys_base | _PAGE_VALID | _PAGE_SZ4MB;
	pt |= _PAGE_CP | _PAGE_CV | _PAGE_P | _PAGE_L | _PAGE_W;
	__save_and_cli(flags);
	__asm__ __volatile__("
	stxa	%1, [%0] %3
	stxa	%2, [%5] %4
	membar	#Sync
	flush	%%g6
	nop
	nop
	nop"
	: /* No outputs */
	: "r" (TLB_TAG_ACCESS), "r" (alias_base), "r" (pt),
	  "i" (ASI_DMMU), "i" (ASI_DTLB_DATA_ACCESS), "r" (61 << 3)
	: "memory");
	__restore_flags(flags);
	
	/* Now set kernel pgd to upper alias so physical page computations
	 * work.
	 */
	init_mm.pgd += ((shift) / (sizeof(pgd_t)));
	
	memset(swapper_pmd_dir, 0, sizeof(swapper_pmd_dir));

	/* Now can init the kernel/bad page tables. */
	pgd_set(&swapper_pg_dir[0], swapper_pmd_dir + (shift / sizeof(pgd_t)));
	
	sparc64_vpte_patchme[0] |= (init_mm.pgd[0] >> 10);
	
	start_mem = irq_init(start_mem, end_mem);

	/* We use mempool to create page tables, therefore adjust it up
	 * such that __pa() macros etc. work.
	 */
	mempool = PAGE_ALIGN(start_mem) + shift;

	/* Allocate 64M for dynamic DVMA mapping area. */
	allocate_ptable_skeleton(DVMA_VADDR, DVMA_VADDR + 0x4000000);
	inherit_prom_mappings();
	

	/* Ok, we can use our TLB miss and window trap handlers safely. */
	setup_tba();

	/* Really paranoid. */
	flushi((long)&empty_zero_page);
	membar("#Sync");

	/* Cleanup the extra locked TLB entry we created since we have the
	 * nice TLB miss handlers of ours installed now.
	 */
	/* We only created DTLB mapping of this stuff. */
	spitfire_flush_dtlb_nucleus_page(alias_base);
	membar("#Sync");

	/* Paranoid */
	flushi((long)&empty_zero_page);
	membar("#Sync");

	inherit_locked_prom_mappings(1);

	flush_tlb_all();

	start_mem = free_area_init(PAGE_ALIGN(mempool), end_mem);

	return device_scan (PAGE_ALIGN (start_mem));
}

/* XXX Add also PG_Hole flag, set it in the page structs here,
 * XXX remove FREE_UNUSED_MEM_MAP code, and the nfsd file handle
 * problems will all be gone.  -DaveM
 */
__initfunc(static void taint_real_pages(unsigned long start_mem, unsigned long end_mem))
{
	unsigned long tmp = 0, paddr, endaddr;
	unsigned long end = __pa(end_mem);

	dvmaio_init();
	for (paddr = __pa(start_mem); paddr < end; ) {
		for (; sp_banks[tmp].num_bytes != 0; tmp++)
			if (sp_banks[tmp].base_addr + sp_banks[tmp].num_bytes > paddr)
				break;
		if (!sp_banks[tmp].num_bytes) {
			mem_map[paddr>>PAGE_SHIFT].flags |= (1<<PG_skip);
			mem_map[paddr>>PAGE_SHIFT].next_hash = mem_map + (phys_base >> PAGE_SHIFT);
			return;
		}
		
		if (sp_banks[tmp].base_addr > paddr) {
			/* Making a one or two pages PG_skip holes is not necessary */
			if (sp_banks[tmp].base_addr - paddr > 2 * PAGE_SIZE) {
				mem_map[paddr>>PAGE_SHIFT].flags |= (1<<PG_skip);
				mem_map[paddr>>PAGE_SHIFT].next_hash = mem_map + (sp_banks[tmp].base_addr >> PAGE_SHIFT);
			}
			paddr = sp_banks[tmp].base_addr;
		}
		
		endaddr = sp_banks[tmp].base_addr + sp_banks[tmp].num_bytes;
		while (paddr < endaddr) {
			mem_map[paddr>>PAGE_SHIFT].flags &= ~(1<<PG_reserved);
			if (paddr >= dvmaiobase)
				mem_map[paddr>>PAGE_SHIFT].flags &= ~(1<<PG_DMA);
			paddr += PAGE_SIZE;
		}
	}
}

__initfunc(void mem_init(unsigned long start_mem, unsigned long end_mem))
{
	int codepages = 0;
	int datapages = 0;
	int initpages = 0;
	unsigned long addr;
	unsigned long alias_base = phys_base + PAGE_OFFSET - (long)(&empty_zero_page);
	struct page *page, *end;
	int i;

	end_mem &= PAGE_MASK;
	max_mapnr = MAP_NR(end_mem);
	high_memory = (void *) end_mem;

	start_mem = PAGE_ALIGN(start_mem);
	num_physpages = 0;
	
	if (phys_base) {
		mem_map[0].flags |= (1<<PG_skip) | (1<<PG_reserved);
		mem_map[0].next_hash = mem_map + (phys_base >> PAGE_SHIFT);
	}

	addr = PAGE_OFFSET + phys_base;
	while(addr < start_mem) {
#ifdef CONFIG_BLK_DEV_INITRD
		if (initrd_below_start_ok && addr >= initrd_start && addr < initrd_end)
			mem_map[MAP_NR(addr)].flags &= ~(1<<PG_reserved);
		else
#endif	
			mem_map[MAP_NR(addr)].flags |= (1<<PG_reserved);
		addr += PAGE_SIZE;
	}

	taint_real_pages(start_mem, end_mem);
	
#ifdef FREE_UNUSED_MEM_MAP	
	end = mem_map + max_mapnr;
	for (page = mem_map; page < end; page++) {
		if (PageSkip(page)) {
			unsigned long low, high;
			
			low = PAGE_ALIGN((unsigned long)(page+1));
			if (page->next_hash < page)
				high = ((unsigned long)end) & PAGE_MASK;
			else
				high = ((unsigned long)page->next_hash) & PAGE_MASK;
			while (low < high) {
				mem_map[MAP_NR(low)].flags &= ~(1<<PG_reserved);
				low += PAGE_SIZE;
			}
		}
	}
#endif
	
	for (addr = PAGE_OFFSET; addr < end_mem; addr += PAGE_SIZE) {
		if (PageSkip(mem_map + MAP_NR(addr))) {
			unsigned long next = mem_map[MAP_NR(addr)].next_hash - mem_map;
			
			next = (next << PAGE_SHIFT) + PAGE_OFFSET;
			if (next < addr || next >= end_mem)
				break;
			addr = next;
		}
		num_physpages++;
		if (PageReserved(mem_map + MAP_NR(addr))) {
			if ((addr < ((unsigned long) &etext) + alias_base) && (addr >= alias_base))
				codepages++;
			else if((addr >= ((unsigned long)&__init_begin) + alias_base)
				&& (addr < ((unsigned long)&__init_end) + alias_base))
				initpages++;
			else if((addr < start_mem) && (addr >= alias_base))
				datapages++;
			continue;
		}
		atomic_set(&mem_map[MAP_NR(addr)].count, 1);
#ifdef CONFIG_BLK_DEV_INITRD
		if (!initrd_start ||
		    (addr < initrd_start || addr >= initrd_end))
#endif
			free_page(addr);
	}
	
#ifndef __SMP__
	{
		/* Put empty_pg_dir on pgd_quicklist */
		extern pgd_t empty_pg_dir[1024];
		unsigned long addr = (unsigned long)empty_pg_dir;
		
		memset(empty_pg_dir, 0, sizeof(empty_pg_dir));
		addr += alias_base;
		mem_map[MAP_NR(addr)].pprev_hash = 0;
		free_pgd_fast((pgd_t *)addr);
	}
#endif

	printk("Memory: %uk available (%dk kernel code, %dk data, %dk init) [%016lx,%016lx]\n",
	       nr_free_pages << (PAGE_SHIFT-10),
	       codepages << (PAGE_SHIFT-10),
	       datapages << (PAGE_SHIFT-10), 
	       initpages << (PAGE_SHIFT-10), 
	       PAGE_OFFSET, end_mem);

	/* NOTE NOTE NOTE NOTE
	 * Please keep track of things and make sure this
	 * always matches the code in mm/page_alloc.c -DaveM
	 */
	i = nr_free_pages >> 7;
	if (i < 48)
		i = 48;
	if (i > 256)
		i = 256;
	freepages.min = i;
	freepages.low = i << 1;
	freepages.high = freepages.low + i;
}

void free_initmem (void)
{
	unsigned long addr;
	
	addr = (unsigned long)(&__init_begin);
	for (; addr < (unsigned long)(&__init_end); addr += PAGE_SIZE) {
		unsigned long page = addr + (long)__va(phys_base)
					- (long)(&empty_zero_page);

		mem_map[MAP_NR(page)].flags &= ~(1 << PG_reserved);
		atomic_set(&mem_map[MAP_NR(page)].count, 1);
		free_page(page);
	}
}

void si_meminfo(struct sysinfo *val)
{
	struct page *page, *end;

	val->totalram = 0;
	val->sharedram = 0;
	val->freeram = nr_free_pages << PAGE_SHIFT;
	val->bufferram = buffermem;
	for (page = mem_map, end = mem_map + max_mapnr;
	     page < end; page++) {
		if (PageSkip(page)) {
			if (page->next_hash < page)
				break;
			page = page->next_hash;
		}
		if (PageReserved(page))
			continue;
		val->totalram++;
		if (!atomic_read(&page->count))
			continue;
		val->sharedram += atomic_read(&page->count) - 1;
	}
	val->totalram <<= PAGE_SHIFT;
	val->sharedram <<= PAGE_SHIFT;
}
