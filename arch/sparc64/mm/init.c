/*  $Id: init.c,v 1.39 1997/07/07 02:50:57 davem Exp $
 *  arch/sparc64/mm/init.c
 *
 *  Copyright (C) 1996,1997 David S. Miller (davem@caip.rutgers.edu)
 *  Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */
 
#include <linux/config.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/blk.h>
#include <linux/swap.h>

#include <asm/head.h>
#include <asm/system.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/oplib.h>
#include <asm/iommu.h>
#include <asm/io.h>
#include <asm/mmu_context.h>
#include <asm/vaddrs.h>

extern void show_net_buffers(void);
extern unsigned long device_scan(unsigned long);

struct sparc_phys_banks sp_banks[SPARC_PHYS_BANKS];

/* Ugly, but necessary... -DaveM */
unsigned long phys_base, null_pmd_table, null_pte_table;

extern unsigned long empty_null_pmd_table;
extern unsigned long empty_null_pte_table;

unsigned long tlb_context_cache = CTX_FIRST_VERSION;

/* References to section boundaries */
extern char __init_begin, __init_end, etext, __bss_start;

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
	int i,free = 0,total = 0,reserved = 0;
	int shared = 0;

	printk("\nMem-info:\n");
	show_free_areas();
	printk("Free swap:       %6dkB\n",nr_swap_pages<<(PAGE_SHIFT-10));
	i = max_mapnr;
	while (i-- > 0) {
		total++;
		if (PageReserved(mem_map + i))
			reserved++;
		else if (!atomic_read(&mem_map[i].count))
			free++;
		else
			shared += atomic_read(&mem_map[i].count) - 1;
	}
	printk("%d pages of RAM\n",total);
	printk("%d free pages\n",free);
	printk("%d reserved pages\n",reserved);
	printk("%d pages shared\n",shared);
	show_buffers();
#ifdef CONFIG_NET
	show_net_buffers();
#endif
}

/* IOMMU support, the ideas are right, the code should be cleaned a bit still... */

/* XXX Also, play with the streaming buffers at some point, both
 * XXX Fusion and Sunfire both have them aparently... -DaveM
 */

/* This keeps track of pages used in sparc_alloc_dvma() invocations. */
static unsigned long dvma_map_pages[0x10000000 >> 16] = { 0, };
static unsigned long dvma_pages_current_offset = 0;
static int dvma_pages_current_index = 0;

__initfunc(unsigned long iommu_init(int iommu_node, unsigned long memory_start,
				    unsigned long memory_end, struct linux_sbus *sbus))
{
	struct iommu_struct *iommu;
	struct sysio_regs *sregs;
	struct linux_prom_registers rprop[2];
	unsigned long impl, vers;
	unsigned long control, tsbbase;
	unsigned long *iopte;
	int err, i;

	err = prom_getproperty(iommu_node, "reg", (char *)rprop,
			       sizeof(rprop));
	if(err == -1) {
		prom_printf("iommu_init: Cannot map SYSIO control registers.\n");
		prom_halt();
	}
	sregs = (struct sysio_regs *) sparc_alloc_io(rprop[0].phys_addr,
						     (void *)0,
						     sizeof(struct sysio_regs),
						     "SYSIO Regs",
						     rprop[0].which_io, 0x0);

	memory_start = (memory_start + 7) & ~7;
	iommu = (struct iommu_struct *) memory_start;
	memory_start += sizeof(struct iommu_struct);
	iommu->sysio_regs = sregs;
	sbus->iommu = iommu;

	control = sregs->iommu_control;
	impl = (control & IOMMU_CTRL_IMPL) >> 60;
	vers = (control & IOMMU_CTRL_VERS) >> 56;
	printk("IOMMU: IMPL[%x] VERS[%x] SYSIO mapped at %016lx\n",
	       (unsigned int) impl, (unsigned int)vers, (unsigned long) sregs);
	
	control &= ~(IOMMU_CTRL_TSBSZ);
	control |= (IOMMU_TSBSZ_64K | IOMMU_CTRL_TBWSZ | IOMMU_CTRL_ENAB);

	/* Use only 64k pages, things are layed out in the 32-bit SBUS
	 * address space like this:
	 *
	 * 0x00000000	----------------------------------------
	 *		| Direct physical mappings for most    |
	 *              | DVMA to paddr's within this range    |
	 * 0xf0000000   ----------------------------------------
	 * 		| For mappings requested via           |
	 *              | sparc_alloc_dvma()		       |
	 * 0xffffffff	----------------------------------------
	 */
	tsbbase = PAGE_ALIGN(memory_start);
	memory_start = (tsbbase + ((64 * 1024) * 8));
	iommu->page_table = (iopte_t *) tsbbase;
	iopte = (unsigned long *) tsbbase;

	/* Setup aliased mappings... */
	for(i = 0; i < (65536 - 4096); i++) {
		*iopte  = (IOPTE_VALID | IOPTE_64K | IOPTE_CACHE | IOPTE_WRITE);
		*iopte |= (i << 16);
		iopte++;
	}

	/* Clear all sparc_alloc_dvma() maps. */
	for( ; i < 65536; i++)
		*iopte++ = 0;

	sregs->iommu_tsbbase = __pa(tsbbase);
	sregs->iommu_control = control;

	return memory_start;
}

void mmu_map_dma_area(unsigned long addr, int len, __u32 *dvma_addr)
{
	struct iommu_struct *iommu = SBus_chain->iommu; /* GROSS ME OUT! */
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;

	/* Find out if we need to grab some pages. */
	if(!dvma_map_pages[dvma_pages_current_index] ||
	   ((dvma_pages_current_offset + len) > (1 << 16))) {
		unsigned long *iopte;
		unsigned long newpages = __get_free_pages(GFP_KERNEL, 3, 0);
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
		i = (65536 - 4096) + i;
		iopte = (unsigned long *)(iommu->page_table + i);
		*iopte  = (IOPTE_VALID | IOPTE_64K | IOPTE_CACHE | IOPTE_WRITE);
		*iopte |= __pa(newpages);
	}

	/* Get this out of the way. */
	*dvma_addr = (__u32) ((0xf0000000) +
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

	if((sbus_addr < 0xf0000000) &&
	   ((sbus_addr + len) < 0xf0000000))
		return sbus_addr;

	/* "can't happen"... GFP_DMA assures this. */
	panic("Very high scsi_one mappings should never happen.");
        return (__u32)0;
}

void mmu_get_scsi_sgl(struct mmu_sglist *sg, int sz, struct linux_sbus *sbus)
{
	while(sz >= 0) {
		__u32 page = (__u32) __pa(((unsigned long) sg[sz].addr));
		if((page < 0xf0000000) &&
		   (page + sg[sz].len) < 0xf0000000) {
			sg[sz].dvma_addr = page;
		} else {
			/* "can't happen"... GFP_DMA assures this. */
			panic("scsi_sgl high mappings should never happen.");
		}
		sz--;
	}
}

static char sfmmuinfo[512];

char *mmu_info(void)
{
	/* We'll do the rest later to make it nice... -DaveM */
	sprintf(sfmmuinfo, "MMU Type\t: Spitfire\n");

	return sfmmuinfo;
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

int prom_itlb_ent, prom_dtlb_ent;
unsigned long prom_itlb_tag, prom_itlb_data;
unsigned long prom_dtlb_tag, prom_dtlb_data;

static inline void inherit_locked_prom_mappings(void)
{
	int i;
	int dtlb_seen = 0;
	int itlb_seen = 0;

	/* Fucking losing PROM has more mappings in the TLB, but
	 * it (conveniently) fails to mention any of these in the
	 * translations property.  The only ones that matter are
	 * the locked PROM tlb entries, so we impose the following
	 * irrecovable rule on the PROM, it is allowed 1 locked
	 * entry in the ITLB and 1 in the DTLB.  We move those
	 * (if necessary) up into tlb entry 62.
	 *
	 * Supposedly the upper 16GB of the address space is
	 * reserved for OBP, BUT I WISH THIS WAS DOCUMENTED
	 * SOMEWHERE!!!!!!!!!!!!!!!!!  Furthermore the entire interface
	 * used between the client program and the firmware on sun5
	 * systems to coordinate mmu mappings is also COMPLETELY
	 * UNDOCUMENTED!!!!!! Thanks S(t)un!
	 */
	for(i = 0; i < 62; i++) {
		unsigned long data;

		data = spitfire_get_dtlb_data(i);
		if(!dtlb_seen && (data & _PAGE_L)) {
			unsigned long tag = spitfire_get_dtlb_tag(i);
			prom_dtlb_ent = i;
			prom_dtlb_tag = tag;
			prom_dtlb_data = data;
			__asm__ __volatile__("stxa %%g0, [%0] %1"
					     : : "r" (TLB_TAG_ACCESS), "i" (ASI_DMMU));
			membar("#Sync");
			spitfire_put_dtlb_data(i, 0x0UL);
			membar("#Sync");

			/* Re-install it. */
			__asm__ __volatile__("stxa %0, [%1] %2"
					     : : "r" (tag), "r" (TLB_TAG_ACCESS),
					         "i" (ASI_DMMU));
			membar("#Sync");
			spitfire_put_dtlb_data(62, data);
			membar("#Sync");
			dtlb_seen = 1;
			if(itlb_seen)
				break;
		}
		data = spitfire_get_itlb_data(i);
		if(!itlb_seen && (data & _PAGE_L)) {
			unsigned long tag = spitfire_get_itlb_tag(i);
			prom_itlb_ent = i;
			prom_itlb_tag = tag;
			prom_itlb_data = data;
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

/* If not locked, zap it. */
void flush_tlb_all(void)
{
	unsigned long flags;
	int i;

	save_flags(flags); cli();
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
	restore_flags(flags);
}

void get_new_mmu_context(struct mm_struct *mm, unsigned long ctx)
{
	if((ctx & ~(CTX_VERSION_MASK)) == 0) {
		flush_tlb_all();
		ctx = (ctx & CTX_VERSION_MASK) + CTX_FIRST_VERSION;
		if(ctx == 1)
			ctx = CTX_FIRST_VERSION;
	}
	tlb_context_cache = ctx + 1;
	mm->context = ctx;
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
	
	/* We assume physical memory starts at some 4mb multiple,
	 * if this were not true we wouldn't boot up to this point
	 * anyways.
	 */
	pt  = phys_base | _PAGE_VALID | _PAGE_SZ4MB;
	pt |= _PAGE_CP | _PAGE_CV | _PAGE_P | _PAGE_L | _PAGE_W;
	save_flags(flags); cli();
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
	restore_flags(flags);
	
	/* Now set kernel pgd to upper alias so physical page computations
	 * work.
	 */
	init_mm.pgd += ((shift) / (sizeof(pgd_t *)));

	null_pmd_table = __pa(((unsigned long)&empty_null_pmd_table) + shift);
	null_pte_table = __pa(((unsigned long)&empty_null_pte_table) + shift);

	pmdp = (pmd_t *) &empty_null_pmd_table;
	for(i = 0; i < 1024; i++)
		pmd_val(pmdp[i]) = null_pte_table;

	memset((void *) &empty_null_pte_table, 0, PAGE_SIZE);

	/* Now can init the kernel/bad page tables. */
	__bfill64((void *)swapper_pg_dir, &null_pmd_table);
	__bfill64((void *)&empty_bad_pmd_table, &null_pte_table);

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

	inherit_locked_prom_mappings();
	
	flush_tlb_all();
	
	start_mem = free_area_init(PAGE_ALIGN(mempool), end_mem);

	return device_scan (PAGE_ALIGN (start_mem));
}

extern int min_free_pages;
extern int free_pages_low;
extern int free_pages_high;

__initfunc(static void taint_real_pages(unsigned long start_mem, unsigned long end_mem))
{
	unsigned long addr, tmp2 = 0;

	for(addr = PAGE_OFFSET; addr < end_mem; addr += PAGE_SIZE) {
		if(addr >= PAGE_OFFSET && addr < start_mem)
			addr = start_mem;
		for(tmp2=0; sp_banks[tmp2].num_bytes != 0; tmp2++) {
			unsigned long phys_addr = __pa(addr);
			unsigned long base = sp_banks[tmp2].base_addr;
			unsigned long limit = base + sp_banks[tmp2].num_bytes;

			if((phys_addr >= base) && (phys_addr < limit) &&
			   ((phys_addr + PAGE_SIZE) < limit))
				mem_map[MAP_NR(addr)].flags &= ~(1<<PG_reserved);
		}
	}
}

__initfunc(void mem_init(unsigned long start_mem, unsigned long end_mem))
{
	int codepages = 0;
	int datapages = 0;
	int initpages = 0;
	unsigned long tmp2, addr;
	unsigned long alias_base = phys_base + PAGE_OFFSET - (long)(&empty_zero_page);

	end_mem &= PAGE_MASK;
	max_mapnr = MAP_NR(end_mem);
	high_memory = (void *) end_mem;

	start_mem = PAGE_ALIGN(start_mem);
	num_physpages = (start_mem - PAGE_OFFSET) >> PAGE_SHIFT;

	addr = PAGE_OFFSET;
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
	for (addr = PAGE_OFFSET; addr < end_mem; addr += PAGE_SIZE) {
		if(PageReserved(mem_map + MAP_NR(addr))) {
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
		num_physpages++;
#ifdef CONFIG_BLK_DEV_INITRD
		if (!initrd_start ||
		    (addr < initrd_start || addr >= initrd_end))
#endif
			free_page(addr);
	}

	tmp2 = nr_free_pages << PAGE_SHIFT;

	printk("Memory: %luk available (%dk kernel code, %dk data, %dk init) [%016lx,%016lx]\n",
	       tmp2 >> 10,
	       codepages << (PAGE_SHIFT-10),
	       datapages << (PAGE_SHIFT-10), 
	       initpages << (PAGE_SHIFT-10), 
	       PAGE_OFFSET, end_mem);

	min_free_pages = nr_free_pages >> 7;
	if(min_free_pages < 16)
		min_free_pages = 16;
	free_pages_low = min_free_pages + (min_free_pages >> 1);
	free_pages_high = min_free_pages + min_free_pages;
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
	int i;

	i = MAP_NR(high_memory);
	val->totalram = 0;
	val->sharedram = 0;
	val->freeram = nr_free_pages << PAGE_SHIFT;
	val->bufferram = buffermem;
	while (i-- > 0)  {
		if (PageReserved(mem_map + i))
			continue;
		val->totalram++;
		if (!atomic_read(&mem_map[i].count))
			continue;
		val->sharedram += atomic_read(&mem_map[i].count) - 1;
	}
	val->totalram <<= PAGE_SHIFT;
	val->sharedram <<= PAGE_SHIFT;
}
