/* $Id: io-unit.c,v 1.5 1997/12/22 16:09:26 jj Exp $
 * io-unit.c:  IO-UNIT specific routines for memory management.
 *
 * Copyright (C) 1997 Jakub Jelinek    (jj@sunsite.mff.cuni.cz)
 */
 
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/malloc.h>
#include <asm/pgtable.h>
#include <asm/sbus.h>
#include <asm/io.h>
#include <asm/io-unit.h>
#include <asm/mxcc.h>

#define LONG_ALIGN(x) (((x)+(sizeof(long))-1)&~((sizeof(long))-1))

#define IOPERM        (IOUPTE_CACHE | IOUPTE_WRITE | IOUPTE_VALID)
#define MKIOPTE(phys) ((((phys)>>4) & IOUPTE_PAGE) | IOPERM)

unsigned long sun4d_dma_base;
unsigned long sun4d_dma_vbase;
unsigned long sun4d_dma_size;
__initfunc(unsigned long
iounit_init(int sbi_node, int io_node, unsigned long memory_start,
	    unsigned long memory_end, struct linux_sbus *sbus))
{
	iopte_t *xpt, *xptend;
	unsigned long paddr;
	struct iounit_struct *iounit;
	struct linux_prom_registers iommu_promregs[PROMREG_MAX];
	
	memory_start = LONG_ALIGN(memory_start);
	iounit = (struct iounit_struct *)memory_start;
	memory_start += sizeof(struct iounit_struct);

	prom_getproperty(sbi_node, "reg", (void *) iommu_promregs,
			 sizeof(iommu_promregs));
	prom_apply_generic_ranges(io_node, 0, iommu_promregs, 3);
	xpt = (iopte_t *)
		sparc_alloc_io(iommu_promregs[2].phys_addr, 0, (PAGE_SIZE * 16),
			       "XPT", iommu_promregs[2].which_io, 0x0);
	if(!xpt) panic("Cannot map External Page Table.");
	
	sbus->iommu = (struct iommu_struct *)iounit;
	iounit->page_table = xpt;
	
	/* Initialize new table. */
	paddr = IOUNIT_DMA_BASE - sun4d_dma_base;
	for (xptend = xpt + (sun4d_dma_size >> PAGE_SHIFT);
	     xpt < xptend; paddr++)
		*xpt++ = MKIOPTE(paddr);
	for (xptend = iounit->page_table + (16 * PAGE_SIZE) / sizeof(iopte_t);
	     xpt < xptend;)
	     	*xpt++ = 0;
	     	
	return memory_start;
}

static __u32 iounit_get_scsi_one(char *vaddr, unsigned long len, struct linux_sbus *sbus)
{
	/* Viking MXCC is IO coherent, just need to translate the address to DMA handle */
#ifdef IOUNIT_DEBUG
	if ((((unsigned long) vaddr) & PAGE_MASK) < sun4d_dma_vaddr || 
	    (((unsigned long) vaddr) & PAGE_MASK) + len > sun4d_dma_vbase + sun4d_dma_size)
			panic("Using non-DMA memory for iounit_get_scsi_one");
#endif	
	return (__u32)(sun4d_dma_base + mmu_v2p((long)vaddr));
}

static void iounit_get_scsi_sgl(struct mmu_sglist *sg, int sz, struct linux_sbus *sbus)
{
	/* Viking MXCC is IO coherent, just need to translate the address to DMA handle */
	for (; sz >= 0; sz--) {
#ifdef IOUNIT_DEBUG
		unsigned long page = ((unsigned long) sg[sz].addr) & PAGE_MASK;
		if (page < sun4d_dma_vbase || page + sg[sz].len > sun4d_dma_vbase + sun4d_dma_size)
			panic("Using non-DMA memory for iounit_get_scsi_sgl");
#endif	
		sg[sz].dvma_addr = (__u32) (sun4d_dma_base + mmu_v2p((long)sg[sz].addr));;
	}
}

static void iounit_release_scsi_one(__u32 vaddr, unsigned long len, struct linux_sbus *sbus)
{
}

static void iounit_release_scsi_sgl(struct mmu_sglist *sg, int sz, struct linux_sbus *sbus)
{
}

#ifdef CONFIG_SBUS
static void iounit_map_dma_area(unsigned long addr, int len)
{
	unsigned long page, end;
	pgprot_t dvma_prot;
	iopte_t *iopte;
	struct linux_sbus *sbus;

	dvma_prot = __pgprot(SRMMU_CACHE | SRMMU_ET_PTE | SRMMU_PRIV);
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
			long i;

			pgdp = pgd_offset(init_task.mm, addr);
			pmdp = pmd_offset(pgdp, addr);
			ptep = pte_offset(pmdp, addr);

			set_pte(ptep, pte_val(mk_pte(page, dvma_prot)));
			
			i = ((addr - IOUNIT_DMA_BASE) >> PAGE_SHIFT);

			for_each_sbus(sbus) {
				struct iounit_struct *iounit = (struct iounit_struct *)sbus->iommu;

				iopte = (iopte_t *)(iounit->page_table + i);
				*iopte = __iopte(MKIOPTE(mmu_v2p(page)));
			}
		}
		addr += PAGE_SIZE;
	}
	flush_cache_all();
	flush_tlb_all();
}
#endif

static char *iounit_lockarea(char *vaddr, unsigned long len)
{
	return vaddr;
}

static void iounit_unlockarea(char *vaddr, unsigned long len)
{
}

__initfunc(void ld_mmu_iounit(void))
{
	mmu_lockarea = iounit_lockarea;
	mmu_unlockarea = iounit_unlockarea;

	mmu_get_scsi_one = iounit_get_scsi_one;
	mmu_get_scsi_sgl = iounit_get_scsi_sgl;
	mmu_release_scsi_one = iounit_release_scsi_one;
	mmu_release_scsi_sgl = iounit_release_scsi_sgl;

#ifdef CONFIG_SBUS
	mmu_map_dma_area = iounit_map_dma_area;
#endif
}
