/* $Id: io-unit.c,v 1.13 1998/11/08 11:13:57 davem Exp $
 * io-unit.c:  IO-UNIT specific routines for memory management.
 *
 * Copyright (C) 1997,1998 Jakub Jelinek    (jj@sunsite.mff.cuni.cz)
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
#include <asm/spinlock.h>
#include <asm/bitops.h>

/* #define IOUNIT_DEBUG */
#ifdef IOUNIT_DEBUG
#define IOD(x) printk(x)
#else
#define IOD(x) do { } while (0)
#endif

#define IOPERM        (IOUPTE_CACHE | IOUPTE_WRITE | IOUPTE_VALID)
#define MKIOPTE(phys) __iopte((((phys)>>4) & IOUPTE_PAGE) | IOPERM)

__initfunc(void
iounit_init(int sbi_node, int io_node, struct linux_sbus *sbus))
{
	iopte_t *xpt, *xptend;
	struct iounit_struct *iounit;
	struct linux_prom_registers iommu_promregs[PROMREG_MAX];
	
	iounit = kmalloc(sizeof(struct iounit_struct), GFP_ATOMIC);
	
	memset(iounit, 0, sizeof(*iounit));
	iounit->limit[0] = IOUNIT_BMAP1_START;
	iounit->limit[1] = IOUNIT_BMAP2_START;
	iounit->limit[2] = IOUNIT_BMAPM_START;
	iounit->limit[3] = IOUNIT_BMAPM_END;
	iounit->rotor[1] = IOUNIT_BMAP2_START;
	iounit->rotor[2] = IOUNIT_BMAPM_START;
	
	prom_getproperty(sbi_node, "reg", (void *) iommu_promregs,
			 sizeof(iommu_promregs));
	prom_apply_generic_ranges(io_node, 0, iommu_promregs, 3);
	xpt = (iopte_t *)
		sparc_alloc_io(iommu_promregs[2].phys_addr, 0, (PAGE_SIZE * 16),
			       "XPT", iommu_promregs[2].which_io, 0x0);
	if(!xpt) panic("Cannot map External Page Table.");
	
	sbus->iommu = (struct iommu_struct *)iounit;
	iounit->page_table = xpt;
	
	for (xptend = iounit->page_table + (16 * PAGE_SIZE) / sizeof(iopte_t);
	     xpt < xptend;)
	     	*xpt++ = 0;
}

/* One has to hold iounit->lock to call this */
static unsigned long iounit_get_area(struct iounit_struct *iounit, unsigned long vaddr, int size)
{
	int i, j, k, npages;
	unsigned long rotor, scan, limit;
	iopte_t iopte;

        npages = ((vaddr & ~PAGE_MASK) + size + (PAGE_SIZE-1)) >> PAGE_SHIFT;

	/* A tiny bit of magic ingredience :) */
	switch (npages) {
	case 1: i = 0x0231; break;
	case 2: i = 0x0132; break;
	default: i = 0x0213; break;
	}
	
	IOD(("iounit_get_area(%08lx,%d[%d])=", vaddr, size, npages));
	
next:	j = (i & 15);
	rotor = iounit->rotor[j - 1];
	limit = iounit->limit[j];
	scan = rotor;
nexti:	scan = find_next_zero_bit(iounit->bmap, limit, scan);
	if (scan + npages > limit) {
		if (limit != rotor) {
			limit = rotor;
			scan = iounit->limit[j - 1];
			goto nexti;
		}
		i >>= 4;
		if (!(i & 15))
			panic("iounit_get_area: Couldn't find free iopte slots for (%08lx,%d)\n", vaddr, size);
		goto next;
	}
	for (k = 1, scan++; k < npages; k++)
		if (test_bit(scan++, iounit->bmap))
			goto nexti;
	iounit->rotor[j - 1] = (scan < limit) ? scan : iounit->limit[j - 1];
	scan -= npages;
	iopte = MKIOPTE(mmu_v2p(vaddr & PAGE_MASK));
	vaddr = IOUNIT_DMA_BASE + (scan << PAGE_SHIFT) + (vaddr & ~PAGE_MASK);
	for (k = 0; k < npages; k++, iopte = __iopte(iopte_val(iopte) + 0x100), scan++) {
		set_bit(scan, iounit->bmap);
		iounit->page_table[scan] = iopte;
	}
	IOD(("%08lx\n", vaddr));
	return vaddr;
}

static __u32 iounit_get_scsi_one(char *vaddr, unsigned long len, struct linux_sbus *sbus)
{
	unsigned long ret, flags;
	struct iounit_struct *iounit = (struct iounit_struct *)sbus->iommu;
	
	spin_lock_irqsave(&iounit->lock, flags);
	ret = iounit_get_area(iounit, (unsigned long)vaddr, len);
	spin_unlock_irqrestore(&iounit->lock, flags);
	return ret;
}

static void iounit_get_scsi_sgl(struct mmu_sglist *sg, int sz, struct linux_sbus *sbus)
{
	unsigned long flags;
	struct iounit_struct *iounit = (struct iounit_struct *)sbus->iommu;

	/* FIXME: Cache some resolved pages - often several sg entries are to the same page */
	spin_lock_irqsave(&iounit->lock, flags);
	for (; sz >= 0; sz--) {
		sg[sz].dvma_addr = iounit_get_area(iounit, (unsigned long)sg[sz].addr, sg[sz].len);
	}
	spin_unlock_irqrestore(&iounit->lock, flags);
}

static void iounit_release_scsi_one(__u32 vaddr, unsigned long len, struct linux_sbus *sbus)
{
	unsigned long flags;
	struct iounit_struct *iounit = (struct iounit_struct *)sbus->iommu;
	
	spin_lock_irqsave(&iounit->lock, flags);
	len = ((vaddr & ~PAGE_MASK) + len + (PAGE_SIZE-1)) >> PAGE_SHIFT;
	vaddr = (vaddr - IOUNIT_DMA_BASE) >> PAGE_SHIFT;
	IOD(("iounit_release %08lx-%08lx\n", (long)vaddr, (long)len+vaddr));
	for (len += vaddr; vaddr < len; vaddr++)
		clear_bit(vaddr, iounit->bmap);
	spin_unlock_irqrestore(&iounit->lock, flags);
}

static void iounit_release_scsi_sgl(struct mmu_sglist *sg, int sz, struct linux_sbus *sbus)
{
	unsigned long flags;
	unsigned long vaddr, len;
	struct iounit_struct *iounit = (struct iounit_struct *)sbus->iommu;
	
	spin_lock_irqsave(&iounit->lock, flags);
	for (; sz >= 0; sz--) {
		len = ((sg[sz].dvma_addr & ~PAGE_MASK) + sg[sz].len + (PAGE_SIZE-1)) >> PAGE_SHIFT;
		vaddr = (sg[sz].dvma_addr - IOUNIT_DMA_BASE) >> PAGE_SHIFT;
		IOD(("iounit_release %08lx-%08lx\n", (long)vaddr, (long)len+vaddr));
		for (len += vaddr; vaddr < len; vaddr++)
			clear_bit(vaddr, iounit->bmap);
	}
	spin_unlock_irqrestore(&iounit->lock, flags);
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
/* FIXME: Write this */
	return vaddr;
}

static void iounit_unlockarea(char *vaddr, unsigned long len)
{
/* FIXME: Write this */
}

__initfunc(void ld_mmu_iounit(void))
{
	BTFIXUPSET_CALL(mmu_lockarea, iounit_lockarea, BTFIXUPCALL_RETO0);
	BTFIXUPSET_CALL(mmu_unlockarea, iounit_unlockarea, BTFIXUPCALL_NOP);

	BTFIXUPSET_CALL(mmu_get_scsi_one, iounit_get_scsi_one, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(mmu_get_scsi_sgl, iounit_get_scsi_sgl, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(mmu_release_scsi_one, iounit_release_scsi_one, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(mmu_release_scsi_sgl, iounit_release_scsi_sgl, BTFIXUPCALL_NORM);

#ifdef CONFIG_SBUS
	BTFIXUPSET_CALL(mmu_map_dma_area, iounit_map_dma_area, BTFIXUPCALL_NORM);
#endif
}

__u32 iounit_map_dma_init(struct linux_sbus *sbus, int size)
{
	int i, j, k, npages;
	unsigned long rotor, scan, limit;
	unsigned long flags;
	__u32 ret;
	struct iounit_struct *iounit = (struct iounit_struct *)sbus->iommu;

        npages = (size + (PAGE_SIZE-1)) >> PAGE_SHIFT;
	i = 0x0213;
	spin_lock_irqsave(&iounit->lock, flags);
next:	j = (i & 15);
	rotor = iounit->rotor[j - 1];
	limit = iounit->limit[j];
	scan = rotor;
nexti:	scan = find_next_zero_bit(iounit->bmap, limit, scan);
	if (scan + npages > limit) {
		if (limit != rotor) {
			limit = rotor;
			scan = iounit->limit[j - 1];
			goto nexti;
		}
		i >>= 4;
		if (!(i & 15))
			panic("iounit_map_dma_init: Couldn't find free iopte slots for %d bytes\n", size);
		goto next;
	}
	for (k = 1, scan++; k < npages; k++)
		if (test_bit(scan++, iounit->bmap))
			goto nexti;
	iounit->rotor[j - 1] = (scan < limit) ? scan : iounit->limit[j - 1];
	scan -= npages;
	ret = IOUNIT_DMA_BASE + (scan << PAGE_SHIFT);
	for (k = 0; k < npages; k++, scan++)
		set_bit(scan, iounit->bmap);
	spin_unlock_irqrestore(&iounit->lock, flags);
	return ret;
}

__u32 iounit_map_dma_page(__u32 vaddr, void *addr, struct linux_sbus *sbus)
{
	int scan = (vaddr - IOUNIT_DMA_BASE) >> PAGE_SHIFT;
	struct iounit_struct *iounit = (struct iounit_struct *)sbus->iommu;
	
	iounit->page_table[scan] = MKIOPTE(mmu_v2p(((unsigned long)addr) & PAGE_MASK));
	return vaddr + (((unsigned long)addr) & ~PAGE_MASK);
}
