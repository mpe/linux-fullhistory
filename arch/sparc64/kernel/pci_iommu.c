/* $Id: pci_iommu.c,v 1.1 1999/08/30 10:00:47 davem Exp $
 * pci_iommu.c: UltraSparc PCI controller IOM/STC support.
 *
 * Copyright (C) 1999 David S. Miller (davem@redhat.com)
 */

#include <asm/pbm.h>
#include <asm/iommu.h>
#include <asm/scatterlist.h>

#define PCI_STC_CTXMATCH_ADDR(STC, CTX)	\
	((STC)->strbuf_ctxmatch_base + ((CTX) << 3))

/* Accessing IOMMU and Streaming Buffer registers.
 * REG parameter is a physical address.  All registers
 * are 64-bits in size.
 */
#define pci_iommu_read(__reg) \
({	u64 __ret; \
	__asm__ __volatile__("ldxa [%1] %2, %0" \
			     : "=r" (__ret) \
			     : "r" (__reg), "i" (ASI_PHYS_BYPASS_EC_E) \
			     : "memory"); \
	__ret; \
})
#define pci_iommu_write(__reg, __val) \
	__asm__ __volatile__("stxa %0, [%1] %2" \
			     : /* no outputs */ \
			     : "r" (__val), "r" (__reg), \
			       "i" (ASI_PHYS_BYPASS_EC_E))

/* Find a range of iommu mappings of size NPAGES in page
 * table PGT.  Return pointer to first iopte.
 */
static iopte_t *iommu_find_range(unsigned long npages, iopte_t *pgt, int pgt_size)
{
	int i;

	pgt_size -= npages;
	for (i = 0; i < pgt_size; i++) {
		if (!iopte_val(pgt[i]) & IOPTE_VALID) {
			int scan;

			for (scan = 1; scan < npages; scan++) {
				if (iopte_val(pgt[i + scan]) & IOPTE_VALID) {
					i += scan;
					goto do_next;
				}
			}
			return &pgt[i];
		}
	do_next:
	}
	return NULL;
}

#define IOPTE_CONSISTANT(CTX, PADDR) \
	(IOPTE_VALID | IOPTE_CACHE | IOPTE_WRITE | \
	 (((CTX) << 47) & IOPTE_CONTEXT) | \
	 ((PADDR) & IOPTE_PAGE))

#define IOPTE_STREAMING(CTX, PADDR) \
	(IOPTE_CONSISTANT(CTX, PADDR) | IOPTE_STBUF)

#define IOPTE_INVALID	0UL

/* Map kernel buffer at ADDR of size SZ using consistant mode
 * DMA for PCI device PDEV.  Return 32-bit PCI DMA address.
 */
u32 pci_map_consistant(struct pci_dev *pdev, void *addr, int sz)
{
	struct pcidev_cookie *pcp = pdev->sysdata;
	struct pci_iommu *iommu = &pcp->pbm->parent->iommu;
	iopte_t *base;
	unsigned long flags, npages, oaddr;
	u32 ret;

	spin_lock_irqsave(&iommu->lock, flags);
	oaddr = (unsigned long)addr;
	npages = PAGE_ALIGN(oaddr + sz) - (oaddr & PAGE_MASK);
	npages >>= PAGE_SHIFT;
	base = iommu_find_range(npages,
				iommu->page_table, iommu->page_table_sz);
	ret = 0;
	if (base != NULL) {
		unsigned long i, base_paddr, ctx;

		ret = (iommu->page_table_map_base +
		       ((base - iommu->page_table) << PAGE_SHIFT));
		ret |= (oaddr & ~PAGE_MASK);
		base_paddr = __pa(oaddr & PAGE_MASK);
		ctx = 0;
		if (iommu->iommu_has_ctx_flush)
			ctx = iommu->iommu_cur_ctx++;
		for (i = 0; i < npages; i++, base++, base_paddr += PAGE_SIZE)
			iopte_val(*base) = IOPTE_CONSISTANT(ctx, base_paddr);
	}
	spin_unlock_irqrestore(&iommu->lock, flags);

	return ret;
}

/* Unmap a consistant DMA translation. */
void pci_unmap_consistant(struct pci_dev *pdev, u32 bus_addr, int sz)
{
	struct pcidev_cookie *pcp = pdev->sysdata;
	struct pci_iommu *iommu = &pcp->pbm->parent->iommu;
	iopte_t *base;
	unsigned long flags, npages, i, ctx;

	spin_lock_irqsave(&iommu->lock, flags);
	npages = PAGE_ALIGN(bus_addr + sz) - (bus_addr & PAGE_MASK);
	npages >>= PAGE_SHIFT;
	base = iommu->page_table +
		((bus_addr - iommu->page_table_map_base) >> PAGE_SHIFT);

	/* Data for consistant mappings cannot enter the streaming
	 * buffers, so we only need to update the TSB and flush
	 * those entries from the IOMMU's TLB.
	 */

	/* Step 1: Clear out the TSB entries.  Save away
	 *         the context if necessary.
	 */
	ctx = 0;
	if (iommu->iommu_has_ctx_flush)
		ctx = (iopte_val(*base) & IOPTE_CONTEXT) >> 47UL;
	for (i = 0; i < npages; i++, base++)
		iopte_val(*base) = IOPTE_INVALID;

	/* Step 2: Flush from IOMMU TLB. */
	if (iommu->iommu_has_ctx_flush) {
		pci_iommu_write(iommu->iommu_ctxflush, ctx);
	} else {
		bus_addr &= PAGE_MASK;
		for (i = 0; i < npages; i++, bus_addr += PAGE_SIZE)
			pci_iommu_write(iommu->iommu_flush, bus_addr);
	}

	/* Step 3: Ensure completion of previous PIO writes. */
	(void) pci_iommu_read(iommu->write_complete_reg);

	spin_unlock_irqrestore(&iommu->lock, flags);
}

/* Map a single buffer at PTR of SZ bytes for PCI DMA
 * in streaming mode.
 */
u32 pci_map_single(struct pci_dev *pdev, void *ptr, int sz)
{
	struct pcidev_cookie *pcp = pdev->sysdata;
	struct pci_iommu *iommu = &pcp->pbm->parent->iommu;
	iopte_t *base;
	unsigned long flags, npages, oaddr;
	u32 ret;

	spin_lock_irqsave(&iommu->lock, flags);
	oaddr = (unsigned long)ptr;
	npages = PAGE_ALIGN(oaddr + sz) - (oaddr & PAGE_MASK);
	npages >>= PAGE_SHIFT;
	base = iommu_find_range(npages,
				iommu->page_table, iommu->page_table_sz);
	ret = 0;
	if (base != NULL) {
		unsigned long i, base_paddr, ctx;

		ret = (iommu->page_table_map_base +
		       ((base - iommu->page_table) << PAGE_SHIFT));
		ret |= (oaddr & ~PAGE_MASK);
		base_paddr = __pa(oaddr & PAGE_MASK);
		ctx = 0;
		if (iommu->iommu_has_ctx_flush)
			ctx = iommu->iommu_cur_ctx++;
		for (i = 0; i < npages; i++, base++, base_paddr += PAGE_SIZE)
			iopte_val(*base) = IOPTE_STREAMING(ctx, base_paddr);
	}
	spin_unlock_irqrestore(&iommu->lock, flags);

	return ret;
}

/* Unmap a single streaming mode DMA translation. */
void pci_unmap_single(struct pci_dev *pdev, u32 bus_addr, int sz)
{
	struct pcidev_cookie *pcp = pdev->sysdata;
	struct pci_iommu *iommu = &pcp->pbm->parent->iommu;
	struct pci_strbuf *strbuf = &pcp->pbm->stc;
	iopte_t *base;
	unsigned long flags, npages, i, ctx;

	spin_lock_irqsave(&iommu->lock, flags);
	npages = PAGE_ALIGN(bus_addr + sz) - (bus_addr & PAGE_MASK);
	npages >>= PAGE_SHIFT;
	base = iommu->page_table +
		((bus_addr - iommu->page_table_map_base) >> PAGE_SHIFT);
	bus_addr &= PAGE_MASK;

	/* Step 1: Record the context, if any. */
	ctx = 0;
	if (iommu->iommu_has_ctx_flush)
		ctx = (iopte_val(*base) & IOPTE_CONTEXT) >> 47UL;

	/* Step 2: Kick data out of streaming buffers if necessary. */
	if (strbuf->strbuf_enabled) {
		u32 vaddr = bus_addr;

		PCI_STC_FLUSHFLAG_INIT(strbuf);
		if (strbuf->strbuf_has_ctx_flush &&
		    iommu->iommu_has_ctx_flush) {
			unsigned long matchreg, flushreg;

			flushreg = strbuf->strbuf_ctxflush;
			matchreg = PCI_STC_CTXMATCH_ADDR(strbuf, ctx);
			do {
				pci_iommu_write(flushreg, ctx);
			} while(((long)pci_iommu_read(matchreg)) < 0L);
		} else {
			for (i = 0; i < npages; i++, vaddr += PAGE_SIZE)
				pci_iommu_write(strbuf->strbuf_pflush, vaddr);
		}

		pci_iommu_write(strbuf->strbuf_fsync, strbuf->strbuf_flushflag_pa);
		(void) pci_iommu_read(iommu->write_complete_reg);
		while (!PCI_STC_FLUSHFLAG_SET(strbuf))
			membar("#LoadLoad");
	}

	/* Step 3: Clear out TSB entries. */
	for (i = 0; i < npages; i++, base++)
		iopte_val(*base) = IOPTE_INVALID;

	/* Step 4: Flush the IOMMU TLB. */
	if (iommu->iommu_has_ctx_flush) {
		pci_iommu_write(iommu->iommu_ctxflush, ctx);
	} else {
		for (i = 0; i < npages; i++, bus_addr += PAGE_SIZE)
			pci_iommu_write(iommu->iommu_flush, bus_addr);
	}

	/* Step 5: Ensure completion of previous PIO writes. */
	(void) pci_iommu_read(iommu->write_complete_reg);

	spin_unlock_irqrestore(&iommu->lock, flags);
}

/* Map a set of buffers described by SGLIST with NELEMS array
 * elements in streaming mode for PCI DMA.
 */
void pci_map_sg(struct pci_dev *pdev, struct scatterlist *sglist, int nelems)
{
	struct pcidev_cookie *pcp = pdev->sysdata;
	struct pci_iommu *iommu = &pcp->pbm->parent->iommu;
	unsigned long flags, ctx, i;

	spin_lock_irqsave(&iommu->lock, flags);

	/* Step 1: Choose a context if necessary. */
	ctx = 0;
	if (iommu->iommu_has_ctx_flush)
		ctx = iommu->iommu_cur_ctx++;

	/* Step 2: Create the mappings. */
	for (i = 0; i < nelems; i++) {
		unsigned long oaddr, npages;
		iopte_t *base;

		oaddr = (unsigned long)sglist[i].address;
		npages = PAGE_ALIGN(oaddr + sglist[i].length) - (oaddr & PAGE_MASK);
		npages >>= PAGE_SHIFT;
		base = iommu_find_range(npages,
					iommu->page_table, iommu->page_table_sz);
		if (base != NULL) {
			unsigned long j, base_paddr;
			u32 dvma_addr;

			dvma_addr = (iommu->page_table_map_base +
				     ((base - iommu->page_table) << PAGE_SHIFT));
			dvma_addr |= (oaddr & ~PAGE_MASK);
			sglist[i].dvma_address = dvma_addr;
			sglist[i].dvma_length = sglist[i].length;
			base_paddr = __pa(oaddr & PAGE_MASK);
			for (j = 0; j < npages; j++, base++, base_paddr += PAGE_SIZE)
				iopte_val(*base) = IOPTE_STREAMING(ctx, base_paddr);
		} else {
			sglist[i].dvma_address = 0;
			sglist[i].dvma_length = 0;
		}
	}

	spin_unlock_irqrestore(&iommu->lock, flags);
}

/* Unmap a set of streaming mode DMA translations. */
void pci_unmap_sg(struct pci_dev *pdev, struct scatterlist *sglist, int nelems)
{
	struct pcidev_cookie *pcp = pdev->sysdata;
	struct pci_iommu *iommu = &pcp->pbm->parent->iommu;
	struct pci_strbuf *strbuf = &pcp->pbm->stc;
	unsigned long flags, ctx, i;

	spin_lock_irqsave(&iommu->lock, flags);

	/* Step 1: Record the context, if any. */
	ctx = 0;
	if (iommu->iommu_has_ctx_flush) {
		iopte_t *iopte;

		iopte = iommu->page_table +
			((sglist[0].dvma_address - iommu->page_table_map_base) >> PAGE_SHIFT);
		ctx = (iopte_val(*iopte) & IOPTE_CONTEXT) >> 47UL;
	}

	/* Step 2: Kick data out of streaming buffers if necessary. */
	if (strbuf->strbuf_enabled) {
		PCI_STC_FLUSHFLAG_INIT(strbuf);
		if (strbuf->strbuf_has_ctx_flush &&
		    iommu->iommu_has_ctx_flush) {
			unsigned long matchreg, flushreg;

			flushreg = strbuf->strbuf_ctxflush;
			matchreg = PCI_STC_CTXMATCH_ADDR(strbuf, ctx);
			do {
				pci_iommu_write(flushreg, ctx);
			} while(((long)pci_iommu_read(matchreg)) < 0L);
		} else {
			for (i = 0; i < nelems; i++) {
				unsigned long j, npages;
				u32 vaddr;

				j = sglist[i].dvma_length;
				if (!j)
					break;
				vaddr = sglist[i].dvma_address;
				npages = PAGE_ALIGN(vaddr + j) - (vaddr & PAGE_MASK);
				npages >>= PAGE_SHIFT;
				vaddr &= PAGE_MASK;
				for (j = 0; j < npages; j++, vaddr += PAGE_SIZE)
					pci_iommu_write(strbuf->strbuf_pflush, vaddr);
			}

			pci_iommu_write(strbuf->strbuf_fsync, strbuf->strbuf_flushflag_pa);
			(void) pci_iommu_read(iommu->write_complete_reg);
			while (!PCI_STC_FLUSHFLAG_SET(strbuf))
				membar("#LoadLoad");
		}
	}

	/* Step 3: Clear out TSB entries. */
	for (i = 0; i < nelems; i++) {
		unsigned long j, npages;
		iopte_t *base;
		u32 vaddr;

		j = sglist[i].dvma_length;
		if (!j)
			break;
		vaddr = sglist[i].dvma_address;
		npages = PAGE_ALIGN(vaddr + j) - (vaddr & PAGE_MASK);
		npages >>= PAGE_SHIFT;
		base = iommu->page_table +
			((vaddr - iommu->page_table_map_base) >> PAGE_SHIFT);
		for (j = 0; j < npages; j++, base++)
			iopte_val(*base) = IOPTE_INVALID;
	}

	/* Step 4: Flush the IOMMU TLB. */
	if (iommu->iommu_has_ctx_flush) {
		pci_iommu_write(iommu->iommu_ctxflush, ctx);
	} else {
		for (i = 0; i < nelems; i++) {
			unsigned long j, npages;
			u32 vaddr;

			j = sglist[i].dvma_length;
			if (!j)
				break;
			vaddr = sglist[i].dvma_address;
			npages = PAGE_ALIGN(vaddr + j) - (vaddr & PAGE_MASK);
			npages >>= PAGE_SHIFT;
			for (j = 0; j < npages; j++, vaddr += PAGE_SIZE)
				pci_iommu_write(iommu->iommu_flush, vaddr);
		}
	}

	/* Step 5: Ensure completion of previous PIO writes. */
	(void) pci_iommu_read(iommu->write_complete_reg);

	spin_unlock_irqrestore(&iommu->lock, flags);
}

/* Make physical memory consistant for a single
 * streaming mode DMA translation after a transfer.
 */
void pci_dma_sync_single(struct pci_dev *pdev, u32 bus_addr, int sz)
{
	struct pcidev_cookie *pcp = pdev->sysdata;
	struct pci_iommu *iommu = &pcp->pbm->parent->iommu;
	struct pci_strbuf *strbuf = &pcp->pbm->stc;
	unsigned long flags, ctx, npages;

	if (!strbuf->strbuf_enabled)
		return;

	spin_lock_irqsave(&iommu->lock, flags);

	npages = PAGE_ALIGN(bus_addr + sz) - (bus_addr & PAGE_MASK);
	npages >>= PAGE_SHIFT;
	bus_addr &= PAGE_MASK;

	/* Step 1: Record the context, if any. */
	ctx = 0;
	if (iommu->iommu_has_ctx_flush &&
	    strbuf->strbuf_has_ctx_flush) {
		iopte_t *iopte;

		iopte = iommu->page_table +
			((bus_addr - iommu->page_table_map_base)>>PAGE_SHIFT);
		ctx = (iopte_val(*iopte) & IOPTE_CONTEXT) >> 47UL;
	}

	/* Step 2: Kick data out of streaming buffers. */
	PCI_STC_FLUSHFLAG_INIT(strbuf);
	if (iommu->iommu_has_ctx_flush &&
	    strbuf->strbuf_has_ctx_flush) {
		unsigned long matchreg, flushreg;

		flushreg = strbuf->strbuf_ctxflush;
		matchreg = PCI_STC_CTXMATCH_ADDR(strbuf, ctx);
		do {
			pci_iommu_write(flushreg, ctx);
		} while(((long)pci_iommu_read(matchreg)) < 0L);
	} else {
		unsigned long i;

		for (i = 0; i < npages; i++, bus_addr += PAGE_SIZE)
			pci_iommu_write(strbuf->strbuf_pflush, bus_addr);
	}

	/* Step 3: Perform flush synchronization sequence. */
	pci_iommu_write(strbuf->strbuf_fsync, strbuf->strbuf_flushflag_pa);
	(void) pci_iommu_read(iommu->write_complete_reg);
	while (!PCI_STC_FLUSHFLAG_SET(strbuf))
		membar("#LoadLoad");

	spin_unlock_irqrestore(&iommu->lock, flags);
}

/* Make physical memory consistant for a set of streaming
 * mode DMA translations after a transfer.
 */
void pci_dma_sync_sg(struct pci_dev *pdev, struct scatterlist *sglist, int nelems)
{
	struct pcidev_cookie *pcp = pdev->sysdata;
	struct pci_iommu *iommu = &pcp->pbm->parent->iommu;
	struct pci_strbuf *strbuf = &pcp->pbm->stc;
	unsigned long flags, ctx;

	if (!strbuf->strbuf_enabled)
		return;

	spin_lock_irqsave(&iommu->lock, flags);

	/* Step 1: Record the context, if any. */
	ctx = 0;
	if (iommu->iommu_has_ctx_flush &&
	    strbuf->strbuf_has_ctx_flush) {
		iopte_t *iopte;

		iopte = iommu->page_table +
			((sglist[0].dvma_address - iommu->page_table_map_base) >> PAGE_SHIFT);
		ctx = (iopte_val(*iopte) & IOPTE_CONTEXT) >> 47UL;
	}

	/* Step 2: Kick data out of streaming buffers. */
	PCI_STC_FLUSHFLAG_INIT(strbuf);
	if (iommu->iommu_has_ctx_flush &&
	    strbuf->strbuf_has_ctx_flush) {
		unsigned long matchreg, flushreg;

		flushreg = strbuf->strbuf_ctxflush;
		matchreg = PCI_STC_CTXMATCH_ADDR(strbuf, ctx);
		do {
			pci_iommu_write(flushreg, ctx);
		} while (((long)pci_iommu_read(matchreg)) < 0L);
	} else {
		unsigned long i;

		for(i = 0; i < nelems; i++) {
			unsigned long bus_addr, npages, j;

			j = sglist[i].dvma_length;
			if (!j)
				break;
			bus_addr = sglist[i].dvma_address;
			npages = PAGE_ALIGN(bus_addr + j) - (bus_addr & PAGE_MASK);
			npages >>= PAGE_SHIFT;
			bus_addr &= PAGE_MASK;
			for(j = 0; i < npages; i++, bus_addr += PAGE_SIZE)
				pci_iommu_write(strbuf->strbuf_pflush, bus_addr);
		}
	}

	/* Step 3: Perform flush synchronization sequence. */
	pci_iommu_write(strbuf->strbuf_fsync, strbuf->strbuf_flushflag_pa);
	(void) pci_iommu_read(iommu->write_complete_reg);
	while (!PCI_STC_FLUSHFLAG_SET(strbuf))
		membar("#LoadLoad");

	spin_unlock_irqrestore(&iommu->lock, flags);
}
