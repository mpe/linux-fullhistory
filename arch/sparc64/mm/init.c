/*  $Id: init.c,v 1.81 1998/05/04 05:35:43 jj Exp $
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
unsigned int null_pte_table;
unsigned long two_null_pmd_table, two_null_pte_table;

extern unsigned long empty_null_pmd_table;
extern unsigned long empty_null_pte_table;

unsigned long tlb_context_cache = CTX_FIRST_VERSION;

/* References to section boundaries */
extern char __init_begin, __init_end, etext, __bss_start;

extern void __bfill64(void *, unsigned long *);

static __inline__ void __init_pmd(pmd_t *pmdp)
{
	__bfill64((void *)pmdp, &two_null_pte_table);
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
pmd_t *__bad_pmd(void)
{
	pmd_t *pmdp = (pmd_t *) &empty_bad_pmd_table;

	__init_pmd(pmdp);
	return pmdp;
}

pte_t *__bad_pte(void)
{
	memset((void *) &empty_bad_pte_table, 0, PAGE_SIZE);
	return (pte_t *) (((unsigned long)&empty_bad_pte_table) 
		- ((unsigned long)&empty_zero_page) + phys_base + PAGE_OFFSET);
}

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

#define MAX_TRANSLATIONS 64
static inline void inherit_prom_mappings(void)
{
	struct linux_prom_translation transl[MAX_TRANSLATIONS];
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;
	int node, n, i;

	node = prom_finddevice("/virtual-memory");
	if ((n = prom_getproperty(node, "translations", (char *) transl,
				  sizeof(transl))) == -1) {
		prom_printf("Couldn't get translation property\n");
		prom_halt();
	}
	n = n / sizeof(transl[0]);

	for (i = 0; i < n; i++) {
		unsigned long vaddr;
		
		if (transl[i].virt >= 0xf0000000 && transl[i].virt < 0x100000000) {
			for (vaddr = transl[i].virt;
			     vaddr < transl[i].virt + transl[i].size;
			     vaddr += PAGE_SIZE) {
				pgdp = pgd_offset(init_task.mm, vaddr);
				if (pgd_none(*pgdp)) {
					pmdp = sparc_init_alloc(&mempool,
							 PMD_TABLE_SIZE);
					__init_pmd(pmdp);
					pgd_set(pgdp, pmdp);
				}
				pmdp = pmd_offset(pgdp, vaddr);
				if (pmd_none(*pmdp)) {
					ptep = sparc_init_alloc(&mempool,
							 PTE_TABLE_SIZE);
					pmd_set(pmdp, ptep);
				}
				ptep = pte_offset(pmdp, vaddr);
				set_pte (ptep, __pte(transl[i].data | _PAGE_MODIFIED));
				transl[i].data += PAGE_SIZE;
			}
		}
	}
}

static int prom_ditlb_set = 0;
int prom_itlb_ent, prom_dtlb_ent;
unsigned long prom_itlb_tag, prom_itlb_data;
unsigned long prom_dtlb_tag, prom_dtlb_data;

void prom_world(int enter)
{
	if (!prom_ditlb_set) return;
	if (enter) {
		/* Install PROM world. */
		__asm__ __volatile__("stxa %0, [%1] %2"
					: : "r" (prom_dtlb_tag), "r" (TLB_TAG_ACCESS),
					"i" (ASI_DMMU));
		membar("#Sync");
		spitfire_put_dtlb_data(62, prom_dtlb_data);
		membar("#Sync");
		__asm__ __volatile__("stxa %0, [%1] %2"
					: : "r" (prom_itlb_tag), "r" (TLB_TAG_ACCESS),
					"i" (ASI_IMMU));
		membar("#Sync");
		spitfire_put_itlb_data(62, prom_itlb_data);
		membar("#Sync");
	} else {
		__asm__ __volatile__("stxa %%g0, [%0] %1"
					: : "r" (TLB_TAG_ACCESS), "i" (ASI_DMMU));
		membar("#Sync");
		spitfire_put_dtlb_data(62, 0x0UL);
		membar("#Sync");
		__asm__ __volatile__("stxa %%g0, [%0] %1"
					: : "r" (TLB_TAG_ACCESS), "i" (ASI_IMMU));
		membar("#Sync");
		spitfire_put_itlb_data(62, 0x0UL);
		membar("#Sync");
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
	 * irrecovable rule on the PROM, it is allowed 1 locked
	 * entry in the ITLB and 1 in the DTLB.
	 *
	 * Supposedly the upper 16GB of the address space is
	 * reserved for OBP, BUT I WISH THIS WAS DOCUMENTED
	 * SOMEWHERE!!!!!!!!!!!!!!!!!  Furthermore the entire interface
	 * used between the client program and the firmware on sun5
	 * systems to coordinate mmu mappings is also COMPLETELY
	 * UNDOCUMENTED!!!!!! Thanks S(t)un!
	 */
	for(i = 0; i < 63; i++) {
		unsigned long data;

		data = spitfire_get_dtlb_data(i);
		if(!dtlb_seen && (data & _PAGE_L)) {
			unsigned long tag = spitfire_get_dtlb_tag(i);

			if(save_p) {
				prom_dtlb_ent = i;
				prom_dtlb_tag = tag;
				prom_dtlb_data = data;
			}
			__asm__ __volatile__("stxa %%g0, [%0] %1"
					     : : "r" (TLB_TAG_ACCESS), "i" (ASI_DMMU));
			membar("#Sync");
			spitfire_put_dtlb_data(i, 0x0UL);
			membar("#Sync");

			dtlb_seen = 1;
			if(itlb_seen)
				break;
		}
		data = spitfire_get_itlb_data(i);
		if(!itlb_seen && (data & _PAGE_L)) {
			unsigned long tag = spitfire_get_itlb_tag(i);

			if(save_p) {
				prom_itlb_ent = i;
				prom_itlb_tag = tag;
				prom_itlb_data = data;
			}
			__asm__ __volatile__("stxa %%g0, [%0] %1"
					     : : "r" (TLB_TAG_ACCESS), "i" (ASI_IMMU));
			membar("#Sync");
			spitfire_put_itlb_data(i, 0x0UL);
			membar("#Sync");

			/* Re-install it. */
			__asm__ __volatile__("stxa %0, [%1] %2"
					     : : "r" (tag), "r" (TLB_TAG_ACCESS),
					         "i" (ASI_IMMU));
			membar("#Sync");
			spitfire_put_itlb_data(62, data);
			membar("#Sync");
			itlb_seen = 1;
			if(dtlb_seen)
				break;
		}
	}
	if (save_p)
		prom_ditlb_set = 1;
}

/* Give PROM back his world, done during reboots... */
void prom_reload_locked(void)
{
	__asm__ __volatile__("stxa %0, [%1] %2"
			     : : "r" (prom_dtlb_tag), "r" (TLB_TAG_ACCESS),
			     "i" (ASI_DMMU));
	membar("#Sync");
	spitfire_put_dtlb_data(prom_dtlb_ent, prom_dtlb_data);
	membar("#Sync");

	__asm__ __volatile__("stxa %0, [%1] %2"
			     : : "r" (prom_itlb_tag), "r" (TLB_TAG_ACCESS),
			     "i" (ASI_IMMU));
	membar("#Sync");
	spitfire_put_itlb_data(prom_itlb_ent, prom_itlb_data);
	membar("#Sync");
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

	__asm__ __volatile__("rdpr	%%pstate, %0\n\t"
			     "wrpr	%0, %1, %%pstate\n\t"
			     "flushw"
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

unsigned long mmu_context_bmap[1UL << (CTX_VERSION_SHIFT - 6)];

/* We are always protected by scheduler_lock under SMP.
 * Caller does TLB context flushing on local CPU if necessary.
 */
void get_new_mmu_context(struct mm_struct *mm)
{
	unsigned long ctx = (tlb_context_cache + 1) & ~(CTX_VERSION_MASK);
	unsigned long new_ctx;
	
	if (mm->context != NO_CONTEXT && !((mm->context ^ tlb_context_cache) & CTX_VERSION_MASK))
		clear_bit(mm->context & ~(CTX_VERSION_MASK), mmu_context_bmap);
	new_ctx = find_next_zero_bit(mmu_context_bmap, 1UL << CTX_VERSION_SHIFT, ctx);
	if (new_ctx >= (1UL << CTX_VERSION_SHIFT)) {
		new_ctx = find_next_zero_bit(mmu_context_bmap, ctx, 1);
		if (new_ctx >= ctx) {
			new_ctx = (tlb_context_cache & CTX_VERSION_MASK) + CTX_FIRST_VERSION;
			mmu_context_bmap[0] = 3;
			memset(mmu_context_bmap + sizeof(long), 0, sizeof(mmu_context_bmap) - sizeof(long));
			goto out;
		}
	}
	set_bit(new_ctx, mmu_context_bmap);
	new_ctx |= (tlb_context_cache & CTX_VERSION_MASK);
out:	tlb_context_cache = new_ctx;
	mm->context = new_ctx;
	mm->cpu_vm_mask = 0;
}

#ifdef __SMP__
spinlock_t user_page_lock = SPIN_LOCK_UNLOCKED;
#endif
struct upcache user_page_cache[2] __attribute__((aligned(32)));

unsigned long get_user_page_slow(int which)
{
	unsigned long chunk;
	struct upcache *up = &user_page_cache[!which];
	struct page *p;

	do { chunk = __get_free_pages(GFP_KERNEL, 1); } while(chunk==0);
	p = mem_map + MAP_NR(chunk);
	atomic_set(&p->count, 1);
	atomic_set(&(p+1)->count, 1);
	p->age = (p+1)->age = PAGE_INITIAL_AGE;
	spin_lock(&user_page_lock);
	if(up->count < USER_PAGE_WATER) {
		struct page *new = p + !which;
		new->next = up->list;
		up->list = new;
		up->count++;
	} else
		free_pages((chunk+(PAGE_SIZE*(!which))), 0);
	spin_unlock(&user_page_lock);
	return page_address(p + which);
}

#ifndef __SMP__
struct pgtable_cache_struct pgt_quicklists;
#endif

pmd_t *get_pmd_slow(pgd_t *pgd, unsigned long offset)
{
	pmd_t *pmd;

	pmd = (pmd_t *) __get_free_page(GFP_DMA|GFP_KERNEL);
	if(pmd) {
		__init_pmd(pmd);
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
		memset((void *)pte, 0, PTE_TABLE_SIZE);
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
			pmdp = sparc_init_alloc(&mempool,
						PMD_TABLE_SIZE);
			__init_pmd(pmdp);
			pgd_set(pgdp, pmdp);
		}
		pmdp = pmd_offset(pgdp, start);
		if (pmd_none(*pmdp)) {
			ptep = sparc_init_alloc(&mempool,
						PTE_TABLE_SIZE);
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

#ifdef NOTUSED
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
	printk ("%2x:%016lx,%016lx\n", 0, spitfire_get_dtlb_tag(0), spitfire_get_dtlb_data(0));
        for (slot = 1; slot < 64; slot+=3) {
        	printk ("%2x:%016lx,%016lx %2x:%016lx,%016lx %2x:%016lx,%016lx\n", 
        		slot, spitfire_get_dtlb_tag(slot), spitfire_get_dtlb_data(slot),
        		slot+1, spitfire_get_dtlb_tag(slot+1), spitfire_get_dtlb_data(slot+1),
        		slot+2, spitfire_get_dtlb_tag(slot+2), spitfire_get_dtlb_data(slot+2));
        }
}
#endif

/* paging_init() sets up the page tables */

extern unsigned long free_area_init(unsigned long, unsigned long);

__initfunc(unsigned long 
paging_init(unsigned long start_mem, unsigned long end_mem))
{
	extern unsigned long phys_base;
	extern void setup_tba(unsigned long kpgdir);
	extern void __bfill64(void *, unsigned long *);
	pmd_t *pmdp;
	int i;
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

	/* The funny offsets are to make page table operations much quicker and
	 * requite less state, see pgtable.h for gory details.
	 * pgtable.h assumes null_pmd_table is null_pte_table - PAGE_SIZE, let's
	 * check it now.
	 */
	null_pte_table=__pa(((unsigned long)&empty_null_pte_table)+shift);
	if (null_pmd_table != __pa(((unsigned long)&empty_null_pmd_table)+shift)) {
		prom_printf("null_p{md|te}_table broken.\n");
		prom_halt();
	}
	two_null_pmd_table = (((unsigned long)null_pmd_table) << 32) | null_pmd_table;
	two_null_pte_table = (((unsigned long)null_pte_table) << 32) | null_pte_table;

	pmdp = (pmd_t *) &empty_null_pmd_table;
	for(i = 0; i < PTRS_PER_PMD; i++)
		pmd_val(pmdp[i]) = null_pte_table;

	memset((void *) &empty_null_pte_table, 0, PTE_TABLE_SIZE);

	/* Now can init the kernel/bad page tables. */
	__bfill64((void *)swapper_pg_dir, &two_null_pmd_table);
	__bfill64((void *)&empty_bad_pmd_table, &two_null_pte_table);

	/* We use mempool to create page tables, therefore adjust it up
	 * such that __pa() macros etc. work.
	 */
	mempool = PAGE_ALIGN(start_mem) + shift;

	/* FIXME: This should be done much nicer.
	 * Just now we allocate 64M for each.
	 */
	allocate_ptable_skeleton(IOBASE_VADDR, IOBASE_VADDR + 0x4000000);
	allocate_ptable_skeleton(DVMA_VADDR, DVMA_VADDR + 0x4000000);
	inherit_prom_mappings();

	/* Ok, we can use our TLB miss and window trap handlers safely. */
	setup_tba((unsigned long)init_mm.pgd);

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

	printk("Memory: %uk available (%dk kernel code, %dk data, %dk init) [%016lx,%016lx]\n",
	       nr_free_pages << (PAGE_SHIFT-10),
	       codepages << (PAGE_SHIFT-10),
	       datapages << (PAGE_SHIFT-10), 
	       initpages << (PAGE_SHIFT-10), 
	       PAGE_OFFSET, end_mem);

	freepages.low = nr_free_pages >> 7;
	if(freepages.low < 48)
		freepages.low = 48;
	freepages.low = freepages.low + (freepages.low >> 1);
	freepages.high = freepages.low + freepages.low;
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
