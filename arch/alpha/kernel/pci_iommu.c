/*
 *	linux/arch/alpha/kernel/pci_iommu.c
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/bootmem.h>

#include <asm/io.h>
#include <asm/hwrpb.h>

#include "proto.h"
#include "pci_impl.h"


#define DEBUG_ALLOC 0

#if DEBUG_ALLOC > 0
# define DBGA(args...)		printk(KERN_DEBUG ##args)
#else
# define DBGA(args...)
#endif
#if DEBUG_ALLOC > 1
# define DBGA2(args...)		printk(KERN_DEBUG ##args)
#else
# define DBGA2(args...)
#endif


static inline unsigned long
mk_iommu_pte(unsigned long paddr)
{
	return (paddr >> (PAGE_SHIFT-1)) | 1;
}

static inline long
calc_npages(long bytes)
{
	return (bytes + PAGE_SIZE - 1) >> PAGE_SHIFT;
}

struct pci_iommu_arena *
iommu_arena_new(dma_addr_t base, unsigned long window_size,
		unsigned long align)
{
	unsigned long entries, mem_size, mem_pages;
	struct pci_iommu_arena *arena;

	entries = window_size >> PAGE_SHIFT;
	mem_size = entries * sizeof(unsigned long);
	mem_pages = calc_npages(mem_size);

	arena = alloc_bootmem(sizeof(*arena));
	arena->ptes = __alloc_bootmem(mem_pages * PAGE_SIZE, align, 0);

	spin_lock_init(&arena->lock);
	arena->dma_base = base;
	arena->size = window_size;
	arena->alloc_hint = 0;

	return arena;
}

long
iommu_arena_alloc(struct pci_iommu_arena *arena, long n)
{
	unsigned long flags;
	unsigned long *beg, *p, *end;
	long i;

	spin_lock_irqsave(&arena->lock, flags);

	/* Search forward for the first sequence of N empty ptes.  */
	beg = arena->ptes;
	end = beg + (arena->size >> PAGE_SHIFT);
	p = beg + arena->alloc_hint;
	i = 0;
	while (i < n && p < end)
		i = (*p++ == 0 ? i + 1 : 0);

	if (p >= end) {
		/* Failure.  Assume the hint was wrong and go back to
		   search from the beginning.  */
		p = beg;
		i = 0;
		while (i < n && p < end)
			i = (*p++ == 0 ? i + 1 : 0);

		if (p >= end) {
			spin_unlock_irqrestore(&arena->lock, flags);
			return -1;
		}
	}

	/* Success.  Mark them all in use, ie not zero.  Typically
	   bit zero is the valid bit, so write ~1 into everything.
	   The chip specific bits will fill this in with something
	   kosher when we return.  */
	for (p = p - n, i = 0; i < n; ++i)
		p[i] = ~1UL;

	arena->alloc_hint = p - beg + n;
	spin_unlock_irqrestore(&arena->lock, flags);

	return p - beg;
}

static void
iommu_arena_free(struct pci_iommu_arena *arena, long ofs, long n)
{
	unsigned long *p;
	long i;

	p = arena->ptes + ofs;
	for (i = 0; i < n; ++i)
		p[i] = 0;
	arena->alloc_hint = ofs;
}

/* Map a single buffer of the indicate size for PCI DMA in streaming
   mode.  The 32-bit PCI bus mastering address to use is returned.
   Once the device is given the dma address, the device owns this memory
   until either pci_unmap_single or pci_sync_single is performed.  */

dma_addr_t
pci_map_single(struct pci_dev *pdev, void *cpu_addr, long size)
{
	struct pci_controler *hose = pdev ? pdev->sysdata : pci_isa_hose;
	dma_addr_t max_dma = pdev ? pdev->dma_mask : 0x00ffffff;
	struct pci_iommu_arena *arena;
	long npages, dma_ofs, i;
	unsigned long paddr;
	dma_addr_t ret;

	paddr = virt_to_phys(cpu_addr);

	/* First check to see if we can use the direct map window.  */
	if (paddr + size + __direct_map_base - 1 <= max_dma
	    && paddr + size <= __direct_map_size) {
		ret = paddr + __direct_map_base;

		DBGA2("pci_map_single: [%p,%lx] -> direct %x from %p\n",
		      cpu_addr, size, ret, __builtin_return_address(0));

		return ret;
	}

	/* If the machine doesn't define a pci_tbi routine, we have to
	   assume it doesn't support sg mapping.  */
	if (! alpha_mv.mv_pci_tbi) {
		printk(KERN_INFO "pci_map_single failed: no hw sg\n");
		return 0;
	}
		
	arena = hose->sg_pci;
	if (!arena || arena->dma_base + arena->size > max_dma)
		arena = hose->sg_isa;

	npages = calc_npages((paddr & ~PAGE_MASK) + size);
	dma_ofs = iommu_arena_alloc(arena, npages);
	if (dma_ofs < 0) {
		printk(KERN_INFO "pci_map_single failed: "
		       "could not allocate dma page tables\n");
		return 0;
	}

	paddr &= PAGE_MASK;
	for (i = 0; i < npages; ++i, paddr += PAGE_SIZE)
		arena->ptes[i + dma_ofs] = mk_iommu_pte(paddr);

	ret = arena->dma_base + dma_ofs * PAGE_SIZE;
	ret += (unsigned long)cpu_addr & ~PAGE_MASK;

	/* ??? This shouldn't have been needed, since the entries
	   we've just modified were not in the iommu tlb.  */
	alpha_mv.mv_pci_tbi(hose, ret, ret + size - 1);

	DBGA("pci_map_single: [%p,%lx] np %ld -> sg %x from %p\n",
	     cpu_addr, size, npages, ret, __builtin_return_address(0));

	return ret;
}


/* Unmap a single streaming mode DMA translation.  The DMA_ADDR and
   SIZE must match what was provided for in a previous pci_map_single
   call.  All other usages are undefined.  After this call, reads by
   the cpu to the buffer are guarenteed to see whatever the device
   wrote there.  */

void
pci_unmap_single(struct pci_dev *pdev, dma_addr_t dma_addr, long size)
{
	struct pci_controler *hose = pdev ? pdev->sysdata : pci_isa_hose;
	struct pci_iommu_arena *arena;
	long dma_ofs, npages;


	if (dma_addr >= __direct_map_base
	    && dma_addr < __direct_map_base + __direct_map_size) {
		/* Nothing to do.  */

		DBGA2("pci_unmap_single: direct [%x,%lx] from %p\n",
		      dma_addr, size, __builtin_return_address(0));

		return;
	}

	arena = hose->sg_pci;
	if (!arena || dma_addr < arena->dma_base)
		arena = hose->sg_isa;

	dma_ofs = (dma_addr - arena->dma_base) >> PAGE_SHIFT;
	if (dma_ofs * PAGE_SIZE >= arena->size) {
		printk(KERN_ERR "Bogus pci_unmap_single: dma_addr %x "
		       " base %x size %x\n", dma_addr, arena->dma_base,
		       arena->size);
		return;
		BUG();
	}

	npages = calc_npages((dma_addr & ~PAGE_MASK) + size);
	iommu_arena_free(arena, dma_ofs, npages);
	alpha_mv.mv_pci_tbi(hose, dma_addr, dma_addr + size - 1);

	DBGA2("pci_unmap_single: sg [%x,%lx] np %ld from %p\n",
	      dma_addr, size, npages, __builtin_return_address(0));
}


/* Allocate and map kernel buffer using consistent mode DMA for PCI
   device.  Returns non-NULL cpu-view pointer to the buffer if
   successful and sets *DMA_ADDRP to the pci side dma address as well,
   else DMA_ADDRP is undefined.  */

void *
pci_alloc_consistent(struct pci_dev *pdev, long size, dma_addr_t *dma_addrp)
{
	void *cpu_addr;

	cpu_addr = kmalloc(size, GFP_ATOMIC);
	if (! cpu_addr) {
		printk(KERN_INFO "dma_alloc_consistent: "
		       "kmalloc failed from %p\n",
			__builtin_return_address(0));
		/* ??? Really atomic allocation?  Otherwise we could play
		   with vmalloc and sg if we can't find contiguous memory.  */
		return NULL;
	}
	memset(cpu_addr, 0, size);

	*dma_addrp = pci_map_single(pdev, cpu_addr, size);
	if (*dma_addrp == 0) {
		kfree_s(cpu_addr, size);
		return NULL;
	}
		
	DBGA2("dma_alloc_consistent: %lx -> [%p,%x] from %p\n",
	      size, cpu_addr, *dma_addrp, __builtin_return_address(0));

	return cpu_addr;
}


/* Free and unmap a consistent DMA buffer.  CPU_ADDR and DMA_ADDR must
   be values that were returned from pci_alloc_consistent.  SIZE must
   be the same as what as passed into pci_alloc_consistent.
   References to the memory and mappings assosciated with CPU_ADDR or
   DMA_ADDR past this call are illegal.  */

void
pci_free_consistent(struct pci_dev *pdev, long size, void *cpu_addr,
		    dma_addr_t dma_addr)
{
	pci_unmap_single(pdev, dma_addr, size);
	kfree_s(cpu_addr, size);

	DBGA2("dma_free_consistent: [%x,%lx] from %p\n",
	      dma_addr, size, __builtin_return_address(0));
}


/* Classify the elements of the scatterlist.  Write dma_address
   of each element with:
	0   : Not mergable.
	1   : Followers all physically adjacent.
	[23]: Followers all virtually adjacent.
	-1  : Not leader.
   Write dma_length of each leader with the combined lengths of
   the mergable followers.  */

static inline void
sg_classify(struct scatterlist *sg, struct scatterlist *end)
{
	unsigned long next_vaddr;
	struct scatterlist *leader;

	leader = sg;
	leader->dma_address = 0;
	leader->dma_length = leader->length;
	next_vaddr = (unsigned long)leader->address + leader->length;

	for (++sg; sg < end; ++sg) {
		unsigned long addr, len;
		addr = (unsigned long) sg->address;
		len = sg->length;

		if (next_vaddr == addr) {
			sg->dma_address = -1;
			leader->dma_address |= 1;
			leader->dma_length += len;
		} else if (((next_vaddr | addr) & ~PAGE_MASK) == 0) {
			sg->dma_address = -1;
			leader->dma_address |= 2;
			leader->dma_length += len;
		} else {
			leader = sg;
			leader->dma_address = 0;
			leader->dma_length = len;
		}

		next_vaddr = addr + len;
	}
}

/* Given a scatterlist leader, choose an allocation method and fill
   in the blanks.  */

static inline int
sg_fill(struct scatterlist *leader, struct scatterlist *end,
	struct scatterlist *out, struct pci_iommu_arena *arena,
	dma_addr_t max_dma)
{
	unsigned long paddr = virt_to_phys(leader->address);
	unsigned long size = leader->dma_length;
	struct scatterlist *sg;
	unsigned long *ptes;
	long npages, dma_ofs, i;

	/* If everything is physically contiguous, and the addresses
	   fall into the direct-map window, use it.  */
	if (leader->dma_address < 2
	    && paddr + size + __direct_map_base - 1 <= max_dma
	    && paddr + size <= __direct_map_size) {
		out->dma_address = paddr + __direct_map_base;
		out->dma_length = size;

		DBGA2("sg_fill: [%p,%lx] -> direct %x\n",
		      leader->address, size, out->dma_address);

		return 0;
	}

	/* Otherwise, we'll use the iommu to make the pages virtually
	   contiguous.  */

	paddr &= ~PAGE_MASK;
	npages = calc_npages(paddr + size);
	dma_ofs = iommu_arena_alloc(arena, npages);
	if (dma_ofs < 0)
		return -1;

	out->dma_address = arena->dma_base + dma_ofs*PAGE_SIZE + paddr;
	out->dma_length = size;

	DBGA("sg_fill: [%p,%lx] -> sg %x\n",
	     leader->address, size, out->dma_address);

	ptes = &arena->ptes[dma_ofs];
	sg = leader;
	do {
		paddr = virt_to_phys(sg->address);
		npages = calc_npages((paddr & ~PAGE_MASK) + sg->length);

		DBGA("        (%ld) [%p,%x]\n",
		      sg - leader, sg->address, sg->length);

		paddr &= PAGE_MASK;
		for (i = 0; i < npages; ++i, paddr += PAGE_SIZE)
			*ptes++ = mk_iommu_pte(paddr);

		++sg;
	} while (sg < end && sg->dma_address == -1);

	return 1;
}

/* TODO: Only use the iommu when it helps.  Non-mergable scatterlist
   entries might as well use direct mappings.  */

int
pci_map_sg(struct pci_dev *pdev, struct scatterlist *sg, int nents)
{
	struct scatterlist *start, *end, *out;
	struct pci_controler *hose;
	struct pci_iommu_arena *arena;
	dma_addr_t max_dma, fstart, fend;

	/* If pci_tbi is not available, we must not be able to control
	   an iommu.  Direct map everything, no merging.  */
	if (! alpha_mv.mv_pci_tbi) {
		for (end = sg + nents; sg < end; ++sg) {
			sg->dma_address = virt_to_bus(sg->address);
			sg->dma_length = sg->length;
		}
		return nents;
	}

	/* Fast path single entry scatterlists.  */
	if (nents == 1) {
		sg->dma_length = sg->length;
		sg->dma_address
		  = pci_map_single(pdev, sg->address, sg->length);
		return sg->dma_address != 0;
	}

	hose = pdev ? pdev->sysdata : pci_isa_hose;
	max_dma = pdev ? pdev->dma_mask : 0x00ffffff;
	arena = hose->sg_pci;
	if (!arena || arena->dma_base + arena->size > max_dma)
		arena = hose->sg_isa;
	start = sg;
	end = sg + nents;
	fstart = -1;
	fend = 0;
	
	/* First, prepare information about the entries.  */
	sg_classify(sg, end);

	/* Second, iterate over the scatterlist leaders and allocate
	   dma space as needed.  */
	for (out = sg; sg < end; ++sg) {
		int ret;

		if (sg->dma_address == -1)
			continue;

		ret = sg_fill(sg, end, out, arena, max_dma);
		if (ret < 0)
			goto error;
		else if (ret > 0) {
			dma_addr_t ts, te;

			ts = out->dma_address;
			te = ts + out->dma_length - 1;
			if (fstart > ts)
				fstart = ts;
			if (fend < te)
				fend = te;
		}
		out++;
	}

	/* ??? This shouldn't have been needed, since the entries
	   we've just modified were not in the iommu tlb.  */
	if (fend)
		alpha_mv.mv_pci_tbi(hose, fstart, fend);

	if (out - start == 0)
		printk(KERN_INFO "pci_map_sg failed: no entries?\n");

	return out - start;

error:
	printk(KERN_INFO "pci_map_sg failed: "
	       "could not allocate dma page tables\n");

	/* Some allocation failed while mapping the scatterlist
	   entries.  Unmap them now.  */
	if (out > start)
		pci_unmap_sg(pdev, start, out - start);
	return 0;
}


/* Unmap a set of streaming mode DMA translations.  Again, cpu read
   rules concerning calls here are the same as for pci_unmap_single()
   above.  */

void
pci_unmap_sg(struct pci_dev *pdev, struct scatterlist *sg, int nents)
{
	struct pci_controler *hose;
	struct pci_iommu_arena *arena;
	struct scatterlist *end;
	dma_addr_t max_dma;
	dma_addr_t fstart, fend;

	if (! alpha_mv.mv_pci_tbi)
		return;

	hose = pdev ? pdev->sysdata : pci_isa_hose;
	max_dma = pdev ? pdev->dma_mask : 0x00ffffff;
	arena = hose->sg_pci;
	if (!arena || arena->dma_base + arena->size > max_dma)
		arena = hose->sg_isa;
	fstart = -1;
	fend = 0;

	for (end = sg + nents; sg < end; ++sg) {
		unsigned long addr, size;

		addr = sg->dma_address;
		size = sg->dma_length;

		if (addr >= __direct_map_base
		    && addr < __direct_map_base + __direct_map_size) {
			/* Nothing to do.  */
			DBGA2("pci_unmap_sg: direct [%lx,%lx]\n", addr, size);
		} else {
			long npages, ofs;
			dma_addr_t tend;

			npages = calc_npages((addr & ~PAGE_MASK) + size);
			ofs = (addr - arena->dma_base) >> PAGE_SHIFT;
			iommu_arena_free(arena, ofs, npages);

			tend = addr + size - 1;
			if (fstart > addr)
				fstart = addr;
			if (fend < tend)
				fend = tend;

			DBGA2("pci_unmap_sg: sg [%lx,%lx]\n", addr, size);
		}
	}
	if (fend)
		alpha_mv.mv_pci_tbi(hose, fstart, fend);
}
