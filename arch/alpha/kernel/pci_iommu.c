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

static inline long
calc_order(long size)
{
	int order;

	size = (size-1) >> (PAGE_SHIFT-1);
	order = -1;
	do {
		size >>= 1;
		order++;
	} while (size);
	return order;
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
	long order = calc_order(size);

	cpu_addr = (void *)__get_free_pages(GFP_ATOMIC, order);
	if (! cpu_addr) {
		printk(KERN_INFO "pci_alloc_consistent: "
		       "get_free_pages failed from %p\n",
			__builtin_return_address(0));
		/* ??? Really atomic allocation?  Otherwise we could play
		   with vmalloc and sg if we can't find contiguous memory.  */
		return NULL;
	}
	memset(cpu_addr, 0, size);

	*dma_addrp = pci_map_single(pdev, cpu_addr, size);
	if (*dma_addrp == 0) {
		free_pages((unsigned long)cpu_addr, order);
		return NULL;
	}
		
	DBGA2("pci_alloc_consistent: %lx -> [%p,%x] from %p\n",
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
	free_pages((unsigned long)cpu_addr, calc_order(size));

	DBGA2("pci_free_consistent: [%x,%lx] from %p\n",
	      dma_addr, size, __builtin_return_address(0));
}


/* Classify the elements of the scatterlist.  Write dma_address
   of each element with:
	0   : Followers all physically adjacent.
	1   : Followers all virtually adjacent.
	-1  : Not leader, physically adjacent to previous.
	-2  : Not leader, virtually adjacent to previous.
   Write dma_length of each leader with the combined lengths of
   the mergable followers.  */

static inline void
sg_classify(struct scatterlist *sg, struct scatterlist *end, int virt_ok)
{
	unsigned long next_vaddr;
	struct scatterlist *leader;
	long leader_flag, leader_length;

	leader = sg;
	leader_flag = 0;
	leader_length = leader->length;
	next_vaddr = (unsigned long)leader->address + leader_length;

	for (++sg; sg < end; ++sg) {
		unsigned long addr, len;
		addr = (unsigned long) sg->address;
		len = sg->length;

		if (next_vaddr == addr) {
			sg->dma_address = -1;
			leader_length += len;
		} else if (((next_vaddr | addr) & ~PAGE_MASK) == 0 && virt_ok) {
			sg->dma_address = -2;
			leader_flag = 1;
			leader_length += len;
		} else {
			leader->dma_address = leader_flag;
			leader->dma_length = leader_length;
			leader = sg;
			leader_flag = 0;
			leader_length = len;
		}

		next_vaddr = addr + len;
	}

	leader->dma_address = leader_flag;
	leader->dma_length = leader_length;
}

/* Given a scatterlist leader, choose an allocation method and fill
   in the blanks.  */

static inline int
sg_fill(struct scatterlist *leader, struct scatterlist *end,
	struct scatterlist *out, struct pci_iommu_arena *arena,
	dma_addr_t max_dma)
{
	unsigned long paddr = virt_to_phys(leader->address);
	long size = leader->dma_length;
	struct scatterlist *sg;
	unsigned long *ptes;
	long npages, dma_ofs, i;

	/* If everything is physically contiguous, and the addresses
	   fall into the direct-map window, use it.  */
	if (leader->dma_address == 0
	    && paddr + size + __direct_map_base - 1 <= max_dma
	    && paddr + size <= __direct_map_size) {
		out->dma_address = paddr + __direct_map_base;
		out->dma_length = size;

		DBGA("    sg_fill: [%p,%lx] -> direct %x\n",
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

	DBGA("    sg_fill: [%p,%lx] -> sg %x np %ld\n",
	     leader->address, size, out->dma_address, npages);

	ptes = &arena->ptes[dma_ofs];
	sg = leader;
	if (0 && leader->dma_address == 0) {
		/* All physically contiguous.  We already have the
		   length, all we need is to fill in the ptes.  */

		paddr = virt_to_phys(sg->address) & PAGE_MASK;
		for (i = 0; i < npages; ++i, paddr += PAGE_SIZE)
			*ptes++ = mk_iommu_pte(paddr);

#if DEBUG_ALLOC > 0
		DBGA("    (0) [%p,%x] np %ld\n",
		     sg->address, sg->length, npages);
		for (++sg; sg < end && (int) sg->dma_address < 0; ++sg)
			DBGA("        (%ld) [%p,%x] cont\n",
			     sg - leader, sg->address, sg->length);
#endif
	} else {
		/* All virtually contiguous.  We need to find the
		   length of each physically contiguous subsegment
		   to fill in the ptes.  */
		do {
			struct scatterlist *last_sg = sg;

			size = sg->length;
			paddr = virt_to_phys(sg->address);

			while (sg+1 < end && (int) sg[1].dma_address == -1) {
				size += sg[1].length;
				sg++;
			}

			npages = calc_npages((paddr & ~PAGE_MASK) + size);

			paddr &= PAGE_MASK;
			for (i = 0; i < npages; ++i, paddr += PAGE_SIZE)
				*ptes++ = mk_iommu_pte(paddr);

#if DEBUG_ALLOC > 0
			DBGA("    (%ld) [%p,%x] np %ld\n",
			     last_sg - leader, last_sg->address,
			     last_sg->length, npages);
			while (++last_sg <= sg) {
				DBGA("        (%ld) [%p,%x] cont\n",
				     last_sg - leader, last_sg->address,
				     last_sg->length);
			}
#endif
		} while (++sg < end && (int) sg->dma_address < 0);
	}

	return 1;
}

int
pci_map_sg(struct pci_dev *pdev, struct scatterlist *sg, int nents)
{
	struct scatterlist *start, *end, *out;
	struct pci_controler *hose;
	struct pci_iommu_arena *arena;
	dma_addr_t max_dma;

	/* Fast path single entry scatterlists.  */
	if (nents == 1) {
		sg->dma_length = sg->length;
		sg->dma_address
		  = pci_map_single(pdev, sg->address, sg->length);
		return sg->dma_address != 0;
	}

	start = sg;
	end = sg + nents;

	/* First, prepare information about the entries.  */
	sg_classify(sg, end, alpha_mv.mv_pci_tbi != 0);

	/* Second, figure out where we're going to map things.  */
	if (alpha_mv.mv_pci_tbi) {
		hose = pdev ? pdev->sysdata : pci_isa_hose;
		max_dma = pdev ? pdev->dma_mask : 0x00ffffff;
		arena = hose->sg_pci;
		if (!arena || arena->dma_base + arena->size > max_dma)
			arena = hose->sg_isa;
	} else {
		max_dma = -1;
		arena = NULL;
		hose = NULL;
	}

	/* Third, iterate over the scatterlist leaders and allocate
	   dma space as needed.  */
	for (out = sg; sg < end; ++sg) {
		int ret;

		if ((int) sg->dma_address < 0)
			continue;

		ret = sg_fill(sg, end, out, arena, max_dma);
		if (ret < 0)
			goto error;
		out++;
	}

	if (out - start == 0)
		printk(KERN_INFO "pci_map_sg failed: no entries?\n");
	DBGA("pci_map_sg: %ld entries\n", out - start);

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

	DBGA("pci_unmap_sg: %d entries\n", nents);

	fstart = -1;
	fend = 0;
	for (end = sg + nents; sg < end; ++sg) {
		unsigned long addr, size;

		addr = sg->dma_address;
		size = sg->dma_length;

		if (addr >= __direct_map_base
		    && addr < __direct_map_base + __direct_map_size) {
			/* Nothing to do.  */
			DBGA("    (%ld) direct [%lx,%lx]\n",
			      sg - end + nents, addr, size);
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

			DBGA("    (%ld) sg [%lx,%lx]\n",
			      sg - end + nents, addr, size);
		}
	}
	if (fend)
		alpha_mv.mv_pci_tbi(hose, fstart, fend);
}
