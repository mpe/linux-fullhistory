/* $Id: iommu.c,v 1.9 1998/04/15 14:58:37 jj Exp $
 * iommu.c:  IOMMU specific routines for memory management.
 *
 * Copyright (C) 1995 David S. Miller  (davem@caip.rutgers.edu)
 * Copyright (C) 1995 Peter A. Zaitcev (zaitcev@ithil.mcst.ru)
 * Copyright (C) 1996 Eddie C. Dost    (ecd@skynet.be)
 * Copyright (C) 1997,1998 Jakub Jelinek    (jj@sunsite.mff.cuni.cz)
 */
 
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <asm/pgtable.h>
#include <asm/sbus.h>
#include <asm/io.h>
#include <asm/mxcc.h>

/* srmmu.c */
extern int viking_mxcc_present;
BTFIXUPDEF_CALL(void, flush_page_for_dma, unsigned long)
#define flush_page_for_dma(page) BTFIXUP_CALL(flush_page_for_dma)(page)
extern int flush_page_for_dma_global;
static int viking_flush = 0;
/* viking.S */
extern void viking_flush_page(unsigned long page);
extern void viking_mxcc_flush_page(unsigned long page);

#define IOPERM        (IOPTE_CACHE | IOPTE_WRITE | IOPTE_VALID)
#define MKIOPTE(phys) (((((phys)>>4) & IOPTE_PAGE) | IOPERM) & ~IOPTE_WAZ)

static inline void iommu_map_dvma_pages_for_iommu(struct iommu_struct *iommu)
{
	unsigned long kern_end = (unsigned long) high_memory;
	unsigned long first = page_offset;
	unsigned long last = kern_end;
	iopte_t *iopte = iommu->page_table;

	iopte += ((first - iommu->start) >> PAGE_SHIFT);
	while(first <= last) {
		*iopte++ = __iopte(MKIOPTE(mmu_v2p(first)));
		first += PAGE_SIZE;
	}
}

__initfunc(void
iommu_init(int iommund, struct linux_sbus *sbus))
{
	unsigned int impl, vers, ptsize;
	unsigned long tmp;
	struct iommu_struct *iommu;
	struct linux_prom_registers iommu_promregs[PROMREG_MAX];
	int i, j, k, l, m;
	struct iommu_alloc { unsigned long addr; int next; } *ia;

	iommu = kmalloc(sizeof(struct iommu_struct), GFP_ATOMIC);
	prom_getproperty(iommund, "reg", (void *) iommu_promregs,
			 sizeof(iommu_promregs));
	iommu->regs = (struct iommu_regs *)
		sparc_alloc_io(iommu_promregs[0].phys_addr, 0, (PAGE_SIZE * 3),
			       "IOMMU registers", iommu_promregs[0].which_io, 0x0);
	if(!iommu->regs)
		panic("Cannot map IOMMU registers.");
	impl = (iommu->regs->control & IOMMU_CTRL_IMPL) >> 28;
	vers = (iommu->regs->control & IOMMU_CTRL_VERS) >> 24;
	tmp = iommu->regs->control;
	tmp &= ~(IOMMU_CTRL_RNGE);
	switch(page_offset & 0xf0000000) {
	case 0xf0000000:
		tmp |= (IOMMU_RNGE_256MB | IOMMU_CTRL_ENAB);
		iommu->plow = iommu->start = 0xf0000000;
		break;
	case 0xe0000000:
		tmp |= (IOMMU_RNGE_512MB | IOMMU_CTRL_ENAB);
		iommu->plow = iommu->start = 0xe0000000;
		break;
	case 0xd0000000:
	case 0xc0000000:
		tmp |= (IOMMU_RNGE_1GB | IOMMU_CTRL_ENAB);
		iommu->plow = iommu->start = 0xc0000000;
		break;
	case 0xb0000000:
	case 0xa0000000:
	case 0x90000000:
	case 0x80000000:
		tmp |= (IOMMU_RNGE_2GB | IOMMU_CTRL_ENAB);
		iommu->plow = iommu->start = 0x80000000;
		break;
	}
	iommu->regs->control = tmp;
	iommu_invalidate(iommu->regs);
	iommu->end = 0xffffffff;

	/* Allocate IOMMU page table */
	ptsize = iommu->end - iommu->start + 1;
	ptsize = (ptsize >> PAGE_SHIFT) * sizeof(iopte_t);

	/* Stupid alignment constraints give me a headache. 
	   We want to get very large aligned memory area, larger than
	   maximum what get_free_pages gives us (128K): we need
	   256K or 512K or 1M or 2M aligned to its size. */
	ia = (struct iommu_alloc *) kmalloc (sizeof(struct iommu_alloc) * 128, GFP_ATOMIC);
	for (i = 0; i < 128; i++) {
		ia[i].addr = 0;
		ia[i].next = -1;
	}
	k = 0;
	for (i = 0; i < 128; i++) {
		ia[i].addr = __get_free_pages(GFP_DMA, 5);
		if (ia[i].addr <= ia[k].addr) {
			if (i) {
				ia[i].next = k;
				k = i;
			}			
		} else {
			for (m = k, l = ia[k].next; l != -1; m = l, l = ia[l].next)
				if (ia[i].addr <= ia[l].addr) {
					ia[i].next = l;
					ia[m].next = i;
				}
			if (l == -1)
				ia[m].next = i;
		}
		for (m = -1, j = 0, l = k; l != -1; l = ia[l].next) {
			if (!(ia[l].addr & (ptsize - 1))) {
				tmp = ia[l].addr;
				m = l;
				j = 128 * 1024;
			} else if (m != -1) {
				if (ia[l].addr != tmp + j)
					m = -1;
				else {
					j += 128 * 1024;
					if (j == ptsize) {
						break;
					}
				}
			}
		}
		if (l != -1)
			break;
	}
	if (i == 128) {
		prom_printf("Could not allocate iopte of size 0x%08x\n", ptsize);
		prom_halt();
	}
	for (l = m, j = 0; j < ptsize; j += 128 * 1024, l = ia[l].next)
		ia[l].addr = 0;
	for (l = k; l != -1; l = ia[l].next)
		if (ia[l].addr)
			free_pages(ia[l].addr, 5);
	kfree (ia);
	iommu->lowest = iommu->page_table = (iopte_t *)tmp;
	

	/* Initialize new table. */
	flush_cache_all();
	memset(iommu->page_table, 0, ptsize);
	iommu_map_dvma_pages_for_iommu(iommu);
	if(viking_mxcc_present) {
		unsigned long start = (unsigned long) iommu->page_table;
		unsigned long end = (start + ptsize);
		while(start < end) {
			viking_mxcc_flush_page(start);
			start += PAGE_SIZE;
		}
	} else if (viking_flush) {
		unsigned long start = (unsigned long) iommu->page_table;
		unsigned long end = (start + ptsize);
		while(start < end) {
			viking_flush_page(start);
			start += PAGE_SIZE;
		}
	}
	flush_tlb_all();
	iommu->regs->base = mmu_v2p((unsigned long) iommu->page_table) >> 4;
	iommu_invalidate(iommu->regs);

	sbus->iommu = iommu;
	printk("IOMMU: impl %d vers %d page table at %p of size %d bytes\n",
	       impl, vers, iommu->page_table, ptsize);
}

static __u32 iommu_get_scsi_one_noflush(char *vaddr, unsigned long len, struct linux_sbus *sbus)
{
	return (__u32)vaddr;
}

static __u32 iommu_get_scsi_one_gflush(char *vaddr, unsigned long len, struct linux_sbus *sbus)
{
	flush_page_for_dma(0);
	return (__u32)vaddr;
}

static __u32 iommu_get_scsi_one_pflush(char *vaddr, unsigned long len, struct linux_sbus *sbus)
{
	unsigned long page = ((unsigned long) vaddr) & PAGE_MASK;

	while(page < ((unsigned long)(vaddr + len))) {
		flush_page_for_dma(page);
		page += PAGE_SIZE;
	}
	return (__u32)vaddr;
}

static void iommu_get_scsi_sgl_noflush(struct mmu_sglist *sg, int sz, struct linux_sbus *sbus)
{
	for (; sz >= 0; sz--)
		sg[sz].dvma_addr = (__u32) (sg[sz].addr);
}

static void iommu_get_scsi_sgl_gflush(struct mmu_sglist *sg, int sz, struct linux_sbus *sbus)
{
	flush_page_for_dma(0);
	for (; sz >= 0; sz--)
		sg[sz].dvma_addr = (__u32) (sg[sz].addr);
}

static void iommu_get_scsi_sgl_pflush(struct mmu_sglist *sg, int sz, struct linux_sbus *sbus)
{
	unsigned long page, oldpage = 0;

	while(sz >= 0) {
		page = ((unsigned long) sg[sz].addr) & PAGE_MASK;
		if (oldpage == page)
			page += PAGE_SIZE; /* We flushed that page already */
		while(page < (unsigned long)(sg[sz].addr + sg[sz].len)) {
			flush_page_for_dma(page);
			page += PAGE_SIZE;
		}
		sg[sz].dvma_addr = (__u32) (sg[sz].addr);
		sz--;
		oldpage = page - PAGE_SIZE;
	}
}

static void iommu_release_scsi_one(__u32 vaddr, unsigned long len, struct linux_sbus *sbus)
{
}

static void iommu_release_scsi_sgl(struct mmu_sglist *sg, int sz, struct linux_sbus *sbus)
{
}

#ifdef CONFIG_SBUS
static void iommu_map_dma_area(unsigned long addr, int len)
{
	unsigned long page, end;
	pgprot_t dvma_prot;
	struct iommu_struct *iommu = SBus_chain->iommu;
	iopte_t *iopte = iommu->page_table;
	iopte_t *first;

	if(viking_mxcc_present)
		dvma_prot = __pgprot(SRMMU_CACHE | SRMMU_ET_PTE | SRMMU_PRIV);
	else
		dvma_prot = __pgprot(SRMMU_ET_PTE | SRMMU_PRIV);

	iopte += ((addr - iommu->start) >> PAGE_SHIFT);
	first = iopte;
	end = PAGE_ALIGN((addr + len));
	while(addr < end) {
		page = get_free_page(GFP_KERNEL);
		if(!page) {
			prom_printf("alloc_dvma: Cannot get a dvma page\n");
			prom_halt();
		} else {
			pgd_t *pgdp;
			pmd_t *pmdp;
			pte_t *ptep;

			pgdp = pgd_offset(init_task.mm, addr);
			pmdp = pmd_offset(pgdp, addr);
			ptep = pte_offset(pmdp, addr);

			set_pte(ptep, pte_val(mk_pte(page, dvma_prot)));
			iopte_val(*iopte++) = MKIOPTE(mmu_v2p(page));
		}
		addr += PAGE_SIZE;
	}
	flush_cache_all();
	if(viking_mxcc_present) {
		unsigned long start = ((unsigned long) first) & PAGE_MASK;
		unsigned long end = PAGE_ALIGN(((unsigned long) iopte));
		while(start < end) {
			viking_mxcc_flush_page(start);
			start += PAGE_SIZE;
		}
	} else if(viking_flush) {
		unsigned long start = ((unsigned long) first) & PAGE_MASK;
		unsigned long end = PAGE_ALIGN(((unsigned long) iopte));
		while(start < end) {
			viking_flush_page(start);
			start += PAGE_SIZE;
		}
	}
	flush_tlb_all();
	iommu_invalidate(iommu->regs);
}
#endif

static char *iommu_lockarea(char *vaddr, unsigned long len)
{
	return vaddr;
}

static void iommu_unlockarea(char *vaddr, unsigned long len)
{
}

__initfunc(void ld_mmu_iommu(void))
{
	viking_flush = (BTFIXUPVAL_CALL(flush_page_for_dma) == (unsigned long)viking_flush_page);
	BTFIXUPSET_CALL(mmu_lockarea, iommu_lockarea, BTFIXUPCALL_RETO0);
	BTFIXUPSET_CALL(mmu_unlockarea, iommu_unlockarea, BTFIXUPCALL_NOP);

	if (!BTFIXUPVAL_CALL(flush_page_for_dma)) {
		/* IO coherent chip */
		BTFIXUPSET_CALL(mmu_get_scsi_one, iommu_get_scsi_one_noflush, BTFIXUPCALL_RETO0);
		BTFIXUPSET_CALL(mmu_get_scsi_sgl, iommu_get_scsi_sgl_noflush, BTFIXUPCALL_NORM);
	} else if (flush_page_for_dma_global) {
		/* flush_page_for_dma flushes everything, no matter of what page is it */
		BTFIXUPSET_CALL(mmu_get_scsi_one, iommu_get_scsi_one_gflush, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(mmu_get_scsi_sgl, iommu_get_scsi_sgl_gflush, BTFIXUPCALL_NORM);
	} else {
		BTFIXUPSET_CALL(mmu_get_scsi_one, iommu_get_scsi_one_pflush, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(mmu_get_scsi_sgl, iommu_get_scsi_sgl_pflush, BTFIXUPCALL_NORM);
	}
	BTFIXUPSET_CALL(mmu_release_scsi_one, iommu_release_scsi_one, BTFIXUPCALL_NOP);
	BTFIXUPSET_CALL(mmu_release_scsi_sgl, iommu_release_scsi_sgl, BTFIXUPCALL_NOP);

#ifdef CONFIG_SBUS
	BTFIXUPSET_CALL(mmu_map_dma_area, iommu_map_dma_area, BTFIXUPCALL_NORM);
#endif
}
