/* $Id: sun4c.c,v 1.135 1996/12/23 05:27:50 davem Exp $
 * sun4c.c: Doing in software what should be done in hardware.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996 Eddie C. Dost (ecd@skynet.be)
 * Copyright (C) 1996 Andrew Tridgell (Andrew.Tridgell@anu.edu.au)
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/init.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/vaddrs.h>
#include <asm/idprom.h>
#include <asm/machines.h>
#include <asm/memreg.h>
#include <asm/processor.h>
#include <asm/auxio.h>
#include <asm/io.h>
#include <asm/oplib.h>
#include <asm/openprom.h>

extern int num_segmaps, num_contexts;

#define SUN4C_KERNEL_BUCKETS 32

#ifndef MAX
#define MAX(a,b) ((a)<(b)?(b):(a))
#endif
#ifndef MIN
#define MIN(a,b) ((a)<(b)?(a):(b))
#endif


#define KGPROF_PROFILING 0
#if KGPROF_PROFILING
#define KGPROF_DEPTH 3 /* this needs to match the code below */
#define KGPROF_SIZE 100
static struct {
	unsigned addr[KGPROF_DEPTH];
	unsigned count;
} kgprof_counters[KGPROF_SIZE];

/* just call this function from whatever function you think needs it then
   look at /proc/cpuinfo to see where the function is being called from
   and how often. This gives a type of "kernel gprof" */
#define NEXT_PROF(prev,lvl) (prev>PAGE_OFFSET?__builtin_return_address(lvl):0)
static inline void kgprof_profile(void)
{
	unsigned ret[KGPROF_DEPTH];
	int i,j;
	/* you can't use a variable argument to __builtin_return_address() */
	ret[0] = (unsigned)__builtin_return_address(0);
	ret[1] = (unsigned)NEXT_PROF(ret[0],1);
	ret[2] = (unsigned)NEXT_PROF(ret[1],2);

	for (i=0;i<KGPROF_SIZE && kgprof_counters[i].addr[0];i++) {
		for (j=0;j<KGPROF_DEPTH;j++) 
			if (ret[j] != kgprof_counters[i].addr[j]) break;
		if (j==KGPROF_DEPTH) break;
	}
	if (i<KGPROF_SIZE) {		
		for (j=0;j<KGPROF_DEPTH;j++)
			kgprof_counters[i].addr[j] = ret[j];
		kgprof_counters[i].count++;
	}
}
#endif


/* Flushing the cache. */
struct sun4c_vac_props sun4c_vacinfo;
static int ctxflushes, segflushes, pageflushes;
unsigned long sun4c_kernel_faults;

/* convert a virtual address to a physical address and vice
   versa. Easy on the 4c */
static unsigned long sun4c_v2p(unsigned long vaddr)
{
  return(vaddr - PAGE_OFFSET);
}

static unsigned long sun4c_p2v(unsigned long vaddr)
{
  return(vaddr + PAGE_OFFSET);
}


/* Invalidate every sun4c cache line tag. */
void sun4c_flush_all(void)
{
	unsigned long begin, end;

	if(sun4c_vacinfo.on)
		panic("SUN4C: AIEEE, trying to invalidate vac while"
                      " it is on.");

	/* Clear 'valid' bit in all cache line tags */
	begin = AC_CACHETAGS;
	end = (AC_CACHETAGS + sun4c_vacinfo.num_bytes);
	while(begin < end) {
		__asm__ __volatile__("sta %%g0, [%0] %1\n\t" : :
				     "r" (begin), "i" (ASI_CONTROL));
		begin += sun4c_vacinfo.linesize;
	}
}

/* Blow the entire current context out of the virtual cache. */
static inline void sun4c_flush_context(void)
{
	extern unsigned long bcopy;
	unsigned long begin, end, flags;

	ctxflushes++;

	begin = ((unsigned long) &bcopy);
	end = (begin + sun4c_vacinfo.num_bytes);

	save_and_cli(flags);
	if(sun4c_vacinfo.linesize == 32) {
		while(begin < end) {
			__asm__ __volatile__("
			ld	[%0 + 0x00], %%g0
			ld	[%0 + 0x20], %%g0
			ld	[%0 + 0x40], %%g0
			ld	[%0 + 0x60], %%g0
			ld	[%0 + 0x80], %%g0
			ld	[%0 + 0xa0], %%g0
			ld	[%0 + 0xc0], %%g0
			ld	[%0 + 0xe0], %%g0
			ld	[%0 + 0x100], %%g0
			ld	[%0 + 0x120], %%g0
			ld	[%0 + 0x140], %%g0
			ld	[%0 + 0x160], %%g0
			ld	[%0 + 0x180], %%g0
			ld	[%0 + 0x1a0], %%g0
			ld	[%0 + 0x1c0], %%g0
			ld	[%0 + 0x1e0], %%g0
			" : : "r" (begin));
			begin += 512;
		}
	} else {
		while(begin < end) {
			__asm__ __volatile__("
			ld	[%0 + 0x00], %%g0
			ld	[%0 + 0x10], %%g0
			ld	[%0 + 0x20], %%g0
			ld	[%0 + 0x30], %%g0
			ld	[%0 + 0x40], %%g0
			ld	[%0 + 0x50], %%g0
			ld	[%0 + 0x60], %%g0
			ld	[%0 + 0x70], %%g0
			ld	[%0 + 0x80], %%g0
			ld	[%0 + 0x90], %%g0
			ld	[%0 + 0xa0], %%g0
			ld	[%0 + 0xb0], %%g0
			ld	[%0 + 0xc0], %%g0
			ld	[%0 + 0xd0], %%g0
			ld	[%0 + 0xe0], %%g0
			ld	[%0 + 0xf0], %%g0
			" : : "r" (begin));
			begin += 256;
		}
	}
	restore_flags(flags);
}

/* Scrape the segment starting at ADDR from the virtual cache. */
static inline void sun4c_flush_segment(unsigned long addr)
{
	segflushes++;
	addr &= SUN4C_REAL_PGDIR_MASK;

	if(sun4c_get_segmap(addr) == invalid_segment)
		return;

	if(sun4c_vacinfo.do_hwflushes) {
		unsigned long end = (addr + sun4c_vacinfo.num_bytes);

		for( ; addr < end; addr += PAGE_SIZE)
			__asm__ __volatile__("sta %%g0, [%0] %1\n\t" : :
					     "r" (addr), "i" (ASI_HWFLUSHSEG));
	} else {
		unsigned long nbytes = sun4c_vacinfo.num_bytes;
		unsigned long lsize = sun4c_vacinfo.linesize;

		__asm__ __volatile__("add	%2, %2, %%g1\n\t"
				     "add	%2, %%g1, %%g2\n\t"
				     "add	%2, %%g2, %%g3\n\t"
				     "add	%2, %%g3, %%g4\n\t"
				     "add	%2, %%g4, %%g5\n\t"
				     "add	%2, %%g5, %%o4\n\t"
				     "add	%2, %%o4, %%o5\n"
				     "1:\n\t"
				     "subcc	%1, %%o5, %1\n\t"
				     "sta	%%g0, [%0] %6\n\t"
				     "sta	%%g0, [%0 + %2] %6\n\t"
				     "sta	%%g0, [%0 + %%g1] %6\n\t"
				     "sta	%%g0, [%0 + %%g2] %6\n\t"
				     "sta	%%g0, [%0 + %%g3] %6\n\t"
				     "sta	%%g0, [%0 + %%g4] %6\n\t"
				     "sta	%%g0, [%0 + %%g5] %6\n\t"
				     "sta	%%g0, [%0 + %%o4] %6\n\t"
				     "bg	1b\n\t"
				     " add	%0, %%o5, %0\n\t"
				     : "=&r" (addr), "=&r" (nbytes), "=&r" (lsize)
				     : "0" (addr), "1" (nbytes), "2" (lsize),
				       "i" (ASI_FLUSHSEG)
				     : "g1", "g2", "g3", "g4", "g5", "o4", "o5");
	}
}

/* Bolix one page from the virtual cache. */
static inline void sun4c_flush_page(unsigned long addr)
{
	addr &= PAGE_MASK;

	if((sun4c_get_pte(addr) & (_SUN4C_PAGE_NOCACHE | _SUN4C_PAGE_VALID)) !=
	   _SUN4C_PAGE_VALID)
		return;

	pageflushes++;
	if(sun4c_vacinfo.do_hwflushes) {
		__asm__ __volatile__("sta %%g0, [%0] %1\n\t" : :
				     "r" (addr), "i" (ASI_HWFLUSHPAGE));
	} else {
		unsigned long left = PAGE_SIZE;
		unsigned long lsize = sun4c_vacinfo.linesize;

		__asm__ __volatile__("add	%2, %2, %%g1\n\t"
				     "add	%2, %%g1, %%g2\n\t"
				     "add	%2, %%g2, %%g3\n\t"
				     "add	%2, %%g3, %%g4\n\t"
				     "add	%2, %%g4, %%g5\n\t"
				     "add	%2, %%g5, %%o4\n\t"
				     "add	%2, %%o4, %%o5\n"
				     "1:\n\t"
				     "subcc	%1, %%o5, %1\n\t"
				     "sta	%%g0, [%0] %6\n\t"
				     "sta	%%g0, [%0 + %2] %6\n\t"
				     "sta	%%g0, [%0 + %%g1] %6\n\t"
				     "sta	%%g0, [%0 + %%g2] %6\n\t"
				     "sta	%%g0, [%0 + %%g3] %6\n\t"
				     "sta	%%g0, [%0 + %%g4] %6\n\t"
				     "sta	%%g0, [%0 + %%g5] %6\n\t"
				     "sta	%%g0, [%0 + %%o4] %6\n\t"
				     "bg	1b\n\t"
				     " add	%0, %%o5, %0\n\t"
				     : "=&r" (addr), "=&r" (left), "=&r" (lsize)
				     : "0" (addr), "1" (left), "2" (lsize),
				       "i" (ASI_FLUSHPG)
				     : "g1", "g2", "g3", "g4", "g5", "o4", "o5");
	}
}

/* The sun4c's do have an on chip store buffer.  And the way you
 * clear them out isn't so obvious.  The only way I can think of
 * to accomplish this is to read the current context register,
 * store the same value there, then read an external hardware
 * register.
 */
void sun4c_complete_all_stores(void)
{
	volatile int _unused;

	_unused = sun4c_get_context();
	sun4c_set_context(_unused);
	_unused = *AUXREG;
}

/* Bootup utility functions. */
static inline void sun4c_init_clean_segmap(unsigned char pseg)
{
	unsigned long vaddr;

	sun4c_put_segmap(0, pseg);
	for(vaddr = 0; vaddr < SUN4C_REAL_PGDIR_SIZE; vaddr+=PAGE_SIZE)
		sun4c_put_pte(vaddr, 0);
	sun4c_put_segmap(0, invalid_segment);
}

static inline void sun4c_init_clean_mmu(unsigned long kernel_end)
{
	unsigned long vaddr;
	unsigned char savectx, ctx;

	savectx = sun4c_get_context();
	kernel_end = SUN4C_REAL_PGDIR_ALIGN(kernel_end);
	for(ctx = 0; ctx < num_contexts; ctx++) {
		sun4c_set_context(ctx);
		for(vaddr = 0; vaddr < 0x20000000; vaddr += SUN4C_REAL_PGDIR_SIZE)
			sun4c_put_segmap(vaddr, invalid_segment);
		for(vaddr = 0xe0000000; vaddr < KERNBASE; vaddr += SUN4C_REAL_PGDIR_SIZE)
			sun4c_put_segmap(vaddr, invalid_segment);
		for(vaddr = kernel_end; vaddr < KADB_DEBUGGER_BEGVM; vaddr += SUN4C_REAL_PGDIR_SIZE)
			sun4c_put_segmap(vaddr, invalid_segment);
		for(vaddr = LINUX_OPPROM_ENDVM; vaddr; vaddr += SUN4C_REAL_PGDIR_SIZE)
			sun4c_put_segmap(vaddr, invalid_segment);
	}
	sun4c_set_context(savectx);
}

void sun4c_probe_vac(void)
{
	sun4c_disable_vac();
	if((idprom->id_machtype == (SM_SUN4C | SM_4C_SS1)) ||
	   (idprom->id_machtype == (SM_SUN4C | SM_4C_SS1PLUS))) {
		/* PROM on SS1 lacks this info, to be super safe we
		 * hard code it here since this arch is cast in stone.
		 */
		sun4c_vacinfo.num_bytes = 65536;
		sun4c_vacinfo.linesize = 16;
	} else {
		sun4c_vacinfo.num_bytes = prom_getintdefault(prom_root_node,
							     "vac-size", 65536);
		sun4c_vacinfo.linesize = prom_getintdefault(prom_root_node,
							    "vac-linesize", 16);
	}
	sun4c_vacinfo.num_lines =
		(sun4c_vacinfo.num_bytes / sun4c_vacinfo.linesize);
	switch(sun4c_vacinfo.linesize) {
	case 16:
		sun4c_vacinfo.log2lsize = 4;
		break;
	case 32:
		sun4c_vacinfo.log2lsize = 5;
		break;
	default:
		prom_printf("probe_vac: Didn't expect vac-linesize of %d, halting\n",
			    sun4c_vacinfo.linesize);
		prom_halt();
	};

	sun4c_vacinfo.do_hwflushes = prom_getintdefault(prom_root_node,
							"vac-hwflush", 0);
	if(sun4c_vacinfo.do_hwflushes == 0)
		sun4c_vacinfo.do_hwflushes = prom_getintdefault(prom_root_node,
								"vac_hwflush", 0);

	if(sun4c_vacinfo.num_bytes != 65536) {
		prom_printf("WEIRD Sun4C VAC cache size, tell davem");
		prom_halt();
	}

	sun4c_flush_all();
	sun4c_enable_vac();
}

/* Patch instructions for the low level kernel fault handler. */
extern unsigned long invalid_segment_patch1, invalid_segment_patch1_ff;
extern unsigned long invalid_segment_patch2, invalid_segment_patch2_ff;
extern unsigned long num_context_patch1, num_context_patch1_16;
extern unsigned long num_context_patch2, num_context_patch2_16;
extern unsigned long vac_linesize_patch, vac_linesize_patch_32;
extern unsigned long vac_hwflush_patch1, vac_hwflush_patch1_on;
extern unsigned long vac_hwflush_patch2, vac_hwflush_patch2_on;

#define PATCH_INSN(src, dst) do {	\
		daddr = &(dst);		\
		iaddr = &(src);		\
		*daddr = *iaddr;	\
	} while (0);

static void patch_kernel_fault_handler(void)
{
	unsigned long *iaddr, *daddr;

	switch (num_segmaps) {
		case 128:
			/* Default, nothing to do. */
			break;
		case 256:
			PATCH_INSN(invalid_segment_patch1_ff,
				   invalid_segment_patch1);
			PATCH_INSN(invalid_segment_patch2_ff,
				   invalid_segment_patch2);
			break;
		default:
			prom_printf("Unhandled number of segmaps: %d\n",
				    num_segmaps);
			prom_halt();
	}
	switch (num_contexts) {
		case 8:
			/* Default, nothing to do. */
			break;
		case 16:
			PATCH_INSN(num_context_patch1_16,
				   num_context_patch1);
			PATCH_INSN(num_context_patch2_16,
				   num_context_patch2);
			break;
		default:
			prom_printf("Unhandled number of contexts: %d\n",
				    num_contexts);
			prom_halt();
	}
	if(sun4c_vacinfo.do_hwflushes != 0) {
		PATCH_INSN(vac_hwflush_patch1_on, vac_hwflush_patch1);
		PATCH_INSN(vac_hwflush_patch2_on, vac_hwflush_patch2);
	} else {
		switch(sun4c_vacinfo.linesize) {
		case 16:
			/* Default, nothing to do. */
			break;
		case 32:
			PATCH_INSN(vac_linesize_patch_32, vac_linesize_patch);
			break;
		default:
			prom_printf("Impossible VAC linesize %d, halting...\n",
				    sun4c_vacinfo.linesize);
			prom_halt();
		};
	}
}

static void sun4c_probe_mmu(void)
{
	if((idprom->id_machtype == (SM_SUN4C | SM_4C_SS1)) ||
	   (idprom->id_machtype == (SM_SUN4C | SM_4C_SS1PLUS))) {
		/* Hardcode these just to be safe, PROM on SS1 does
		 * not have this info available in the root node.
		 */
		num_segmaps = 128;
		num_contexts = 8;
	} else {
		num_segmaps = prom_getintdefault(prom_root_node, "mmu-npmg", 128);
		num_contexts = prom_getintdefault(prom_root_node, "mmu-nctx", 0x8);
	}
	patch_kernel_fault_handler();
}

volatile unsigned long *sun4c_memerr_reg = 0;

void sun4c_probe_memerr_reg(void)
{
	int node;
	struct linux_prom_registers regs[1];

	node = prom_getchild(prom_root_node);
	node = prom_searchsiblings(prom_root_node, "memory-error");
	if (!node)
		return;
	prom_getproperty(node, "reg", (char *)regs, sizeof(regs));
	sun4c_memerr_reg = sparc_alloc_io(regs[0].phys_addr, 0,
					  regs[0].reg_size,
					  "memory parity error",
					  regs[0].which_io, 0);
}

static inline void sun4c_init_ss2_cache_bug(void)
{
	extern unsigned long start;

	if((idprom->id_machtype == (SM_SUN4C | SM_4C_SS2)) ||
	   (idprom->id_machtype == (SM_SUN4C | SM_4C_IPX)) ||
	   (idprom->id_machtype == (SM_SUN4C | SM_4C_ELC))) {
		/* Whee.. */
		printk("SS2 cache bug detected, uncaching trap table page\n");
		sun4c_flush_page((unsigned int) &start);
		sun4c_put_pte(((unsigned long) &start),
			(sun4c_get_pte((unsigned long) &start) | _SUN4C_PAGE_NOCACHE));
	}
}

/* Addr is always aligned on a page boundry for us already. */
static void sun4c_map_dma_area(unsigned long addr, int len)
{
	unsigned long page, end;

	end = PAGE_ALIGN((addr + len));
	while(addr < end) {
		page = get_free_page(GFP_KERNEL);
		if(!page) {
			prom_printf("alloc_dvma: Cannot get a dvma page\n");
			prom_halt();
		}
		sun4c_flush_page(page);
		page -= PAGE_OFFSET;
		page >>= PAGE_SHIFT;
		page |= (_SUN4C_PAGE_VALID | _SUN4C_PAGE_DIRTY |
			 _SUN4C_PAGE_NOCACHE | _SUN4C_PAGE_PRIV);
		sun4c_put_pte(addr, page);
		addr += PAGE_SIZE;
	}
}


/* TLB management. */

/* Don't change this struct without changing entry.S. This is used
 * in the in-window kernel fault handler, and you don't want to mess
 * with that. (See sun4c_fault in entry.S).
 */
struct sun4c_mmu_entry {
	struct sun4c_mmu_entry *next;
	struct sun4c_mmu_entry *prev;
	unsigned long vaddr;
	unsigned char pseg;
	unsigned char locked;
};
static struct sun4c_mmu_entry mmu_entry_pool[256];

__initfunc(static void sun4c_init_mmu_entry_pool(void))
{
	int i;

	for(i=0; i < 256; i++) {
		mmu_entry_pool[i].pseg = i;
		mmu_entry_pool[i].next = 0;
		mmu_entry_pool[i].prev = 0;
		mmu_entry_pool[i].vaddr = 0;
		mmu_entry_pool[i].locked = 0;
	}
	mmu_entry_pool[invalid_segment].locked = 1;
}

static inline void fix_permissions(unsigned long vaddr, unsigned long bits_on,
				   unsigned long bits_off)
{
	unsigned long start, end;

	end = vaddr + SUN4C_REAL_PGDIR_SIZE;
	for(start = vaddr; start < end; start += PAGE_SIZE)
		if(sun4c_get_pte(start) & _SUN4C_PAGE_VALID)
			sun4c_put_pte(start, (sun4c_get_pte(start) | bits_on) &
				      ~bits_off);
}

static inline void sun4c_init_map_kernelprom(unsigned long kernel_end)
{
	unsigned long vaddr;
	unsigned char pseg, ctx;

	for(vaddr = KADB_DEBUGGER_BEGVM;
	    vaddr < LINUX_OPPROM_ENDVM;
	    vaddr += SUN4C_REAL_PGDIR_SIZE) {
		pseg = sun4c_get_segmap(vaddr);
		if(pseg != invalid_segment) {
			mmu_entry_pool[pseg].locked = 1;
			for(ctx = 0; ctx < num_contexts; ctx++)
				prom_putsegment(ctx, vaddr, pseg);
			fix_permissions(vaddr, _SUN4C_PAGE_PRIV, 0);
		}
	}
	for(vaddr = KERNBASE; vaddr < kernel_end; vaddr += SUN4C_REAL_PGDIR_SIZE) {
		pseg = sun4c_get_segmap(vaddr);
		mmu_entry_pool[pseg].locked = 1;
		for(ctx = 0; ctx < num_contexts; ctx++)
			prom_putsegment(ctx, vaddr, pseg);
		fix_permissions(vaddr, _SUN4C_PAGE_PRIV, _SUN4C_PAGE_NOCACHE);
	}
}

__initfunc(static void sun4c_init_lock_area(unsigned long start, unsigned long end))
{
	int i, ctx;

	while(start < end) {
		for(i=0; i < invalid_segment; i++)
			if(!mmu_entry_pool[i].locked)
				break;
		mmu_entry_pool[i].locked = 1;
		sun4c_init_clean_segmap(i);
		for(ctx = 0; ctx < num_contexts; ctx++)
			prom_putsegment(ctx, start, mmu_entry_pool[i].pseg);
		start += SUN4C_REAL_PGDIR_SIZE;
	}
}

/* Don't change this struct without changing entry.S. This is used
 * in the in-window kernel fault handler, and you don't want to mess
 * with that. (See sun4c_fault in entry.S).
 */
struct sun4c_mmu_ring {
	struct sun4c_mmu_entry ringhd;
	int num_entries;
};
static struct sun4c_mmu_ring sun4c_context_ring[16]; /* used user entries */
static struct sun4c_mmu_ring sun4c_ufree_ring;       /* free user entries */
struct sun4c_mmu_ring sun4c_kernel_ring;      /* used kernel entries */
struct sun4c_mmu_ring sun4c_kfree_ring;       /* free kernel entries */

static inline void sun4c_init_rings(unsigned long *mempool)
{
	int i;
	for(i=0; i<16; i++) {
		sun4c_context_ring[i].ringhd.next =
			sun4c_context_ring[i].ringhd.prev =
			&sun4c_context_ring[i].ringhd;
		sun4c_context_ring[i].num_entries = 0;
	}
	sun4c_ufree_ring.ringhd.next = sun4c_ufree_ring.ringhd.prev =
		&sun4c_ufree_ring.ringhd;
	sun4c_ufree_ring.num_entries = 0;
	sun4c_kernel_ring.ringhd.next = sun4c_kernel_ring.ringhd.prev =
		&sun4c_kernel_ring.ringhd;
	sun4c_kernel_ring.num_entries = 0;
	sun4c_kfree_ring.ringhd.next = sun4c_kfree_ring.ringhd.prev =
		&sun4c_kfree_ring.ringhd;
	sun4c_kfree_ring.num_entries = 0;
}

static inline void add_ring(struct sun4c_mmu_ring *ring,
			    struct sun4c_mmu_entry *entry)
{
	struct sun4c_mmu_entry *head = &ring->ringhd;

	entry->prev = head;
	(entry->next = head->next)->prev = entry;
	head->next = entry;
	ring->num_entries++;
}

static inline void remove_ring(struct sun4c_mmu_ring *ring,
			       struct sun4c_mmu_entry *entry)
{
	struct sun4c_mmu_entry *next = entry->next;

	(next->prev = entry->prev)->next = next;
	ring->num_entries--;
}

static inline void recycle_ring(struct sun4c_mmu_ring *ring,
			        struct sun4c_mmu_entry *entry)
{
	struct sun4c_mmu_entry *head = &ring->ringhd;
	struct sun4c_mmu_entry *next = entry->next;

	(next->prev = entry->prev)->next = next;
	entry->prev = head;
	(entry->next = head->next)->prev = entry;
	head->next = entry;
	/* num_entries stays the same */
}

static inline void free_user_entry(int ctx, struct sun4c_mmu_entry *entry)
{
        remove_ring(sun4c_context_ring+ctx, entry);
        add_ring(&sun4c_ufree_ring, entry);
}

static inline void assign_user_entry(int ctx, struct sun4c_mmu_entry *entry) 
{
        remove_ring(&sun4c_ufree_ring, entry);
        add_ring(sun4c_context_ring+ctx, entry);
}

static inline void free_kernel_entry(struct sun4c_mmu_entry *entry,
				     struct sun4c_mmu_ring *ring)
{
        remove_ring(ring, entry);
        add_ring(&sun4c_kfree_ring, entry);
}

__initfunc(static void sun4c_init_fill_kernel_ring(int howmany))
{
	int i;

	while(howmany) {
		for(i=0; i < invalid_segment; i++)
			if(!mmu_entry_pool[i].locked)
				break;
		mmu_entry_pool[i].locked = 1;
		sun4c_init_clean_segmap(i);
		add_ring(&sun4c_kfree_ring, &mmu_entry_pool[i]);
		howmany--;
	}
}

__initfunc(static void sun4c_init_fill_user_ring(void))
{
	int i;

	for(i=0; i < invalid_segment; i++) {
		if(mmu_entry_pool[i].locked)
			continue;
		sun4c_init_clean_segmap(i);
		add_ring(&sun4c_ufree_ring, &mmu_entry_pool[i]);
	}
}

static inline void sun4c_kernel_unmap(struct sun4c_mmu_entry *kentry)
{
	int savectx, ctx;

	savectx = sun4c_get_context();
	for(ctx = 0; ctx < num_contexts; ctx++) {
		sun4c_set_context(ctx);
		sun4c_put_segmap(kentry->vaddr, invalid_segment);
	}
	sun4c_set_context(savectx);
}

static inline void sun4c_kernel_map(struct sun4c_mmu_entry *kentry)
{
	int savectx, ctx;

	savectx = sun4c_get_context();
	for(ctx = 0; ctx < num_contexts; ctx++) {
		sun4c_set_context(ctx);
		sun4c_put_segmap(kentry->vaddr, kentry->pseg);
	}
	sun4c_set_context(savectx);
}

static inline void sun4c_user_unmap(struct sun4c_mmu_entry *uentry)
{
	sun4c_put_segmap(uentry->vaddr, invalid_segment);
}

static inline void sun4c_user_map(struct sun4c_mmu_entry *uentry)
{
	unsigned long start = uentry->vaddr;
	unsigned long end = start + SUN4C_REAL_PGDIR_SIZE;

	sun4c_put_segmap(uentry->vaddr, uentry->pseg);
	while(start < end) {
		sun4c_put_pte(start, 0);
		start += PAGE_SIZE;
	}
}

static inline void sun4c_demap_context(struct sun4c_mmu_ring *crp, unsigned char ctx)
{
	struct sun4c_mmu_entry *this_entry, *next_entry;
	unsigned long flags;
	int savectx = sun4c_get_context();

	save_and_cli(flags);
	this_entry = crp->ringhd.next;
	flush_user_windows();
	sun4c_set_context(ctx);
	while(crp->num_entries) {
		next_entry = this_entry->next;
		sun4c_flush_segment(this_entry->vaddr);
		sun4c_user_unmap(this_entry);
		free_user_entry(ctx, this_entry);
		this_entry = next_entry;
	}
	sun4c_set_context(savectx);
	restore_flags(flags);
}

static inline void sun4c_demap_one(struct sun4c_mmu_ring *crp, unsigned char ctx)
{
	/* by using .prev we get a kind of "lru" algorithm */
	struct sun4c_mmu_entry *entry = crp->ringhd.prev;
	int savectx = sun4c_get_context();

	flush_user_windows();
	sun4c_set_context(ctx);
	sun4c_flush_segment(entry->vaddr);
	sun4c_user_unmap(entry);
	free_user_entry(ctx, entry);
	sun4c_set_context(savectx);
}

static int sun4c_user_taken_entries = 0;

static inline struct sun4c_mmu_entry *sun4c_kernel_strategy(void)
{
	struct sun4c_mmu_entry *this_entry;

	/* If some are free, return first one. */
	if(sun4c_kfree_ring.num_entries) {
		this_entry = sun4c_kfree_ring.ringhd.next;
		return this_entry;
	}

	/* Else free one up. */
	this_entry = sun4c_kernel_ring.ringhd.prev;
	sun4c_flush_segment(this_entry->vaddr);
	sun4c_kernel_unmap(this_entry);
	free_kernel_entry(this_entry, &sun4c_kernel_ring);
	this_entry = sun4c_kfree_ring.ringhd.next;

	return this_entry;
}

void sun4c_shrink_kernel_ring(void)
{
	struct sun4c_mmu_entry *entry;
	unsigned long flags;

	/* If an interrupt comes in here, we die... */
	save_and_cli(flags);

	if (sun4c_user_taken_entries) {
		entry = sun4c_kernel_strategy();
        	remove_ring(&sun4c_kfree_ring, entry);
		add_ring(&sun4c_ufree_ring, entry);
		sun4c_user_taken_entries--;
#if 0
		printk("shrink: ufree= %d, kfree= %d, kernel= %d\n",
			sun4c_ufree_ring.num_entries,
			sun4c_kfree_ring.num_entries,
			sun4c_kernel_ring.num_entries);
#endif
	}
	restore_flags(flags);
}

/* Using this method to free up mmu entries eliminates a lot of
 * potential races since we have a kernel that incurs tlb
 * replacement faults.  There may be performance penalties.
 */
static inline struct sun4c_mmu_entry *sun4c_user_strategy(void)
{
	struct ctx_list *next_one;
	struct sun4c_mmu_ring *rp = 0;
	unsigned char ctx;

	/* If some are free, return first one. */
	if(sun4c_ufree_ring.num_entries)
		return sun4c_ufree_ring.ringhd.next;

	if (sun4c_user_taken_entries) {
		sun4c_shrink_kernel_ring();
		return sun4c_ufree_ring.ringhd.next;
	}

	/* Grab one from the LRU context. */
	next_one = ctx_used.next;
	while (sun4c_context_ring[next_one->ctx_number].num_entries == 0)
		next_one = next_one->next;

	ctx = next_one->ctx_number;
	rp = &sun4c_context_ring[ctx];

	sun4c_demap_one(rp, ctx);
	return sun4c_ufree_ring.ringhd.next;
}

void sun4c_grow_kernel_ring(void)
{
	struct sun4c_mmu_entry *entry;

	if (sun4c_ufree_ring.num_entries) {
		entry = sun4c_ufree_ring.ringhd.next;
        	remove_ring(&sun4c_ufree_ring, entry);
		add_ring(&sun4c_kfree_ring, entry);
		sun4c_user_taken_entries++;
#if 0
		printk("grow: ufree= %d, kfree= %d, kernel= %d\n",
			sun4c_ufree_ring.num_entries,
			sun4c_kfree_ring.num_entries,
			sun4c_kernel_ring.num_entries);
#endif
	}
}

static inline void alloc_user_segment(unsigned long address, unsigned char ctx)
{
	struct sun4c_mmu_entry *entry;

	address &= SUN4C_REAL_PGDIR_MASK;
	entry = sun4c_user_strategy();
	assign_user_entry(ctx, entry);
	entry->vaddr = address;
	sun4c_user_map(entry);
}

/* This is now a fast in-window trap handler to avoid any and all races. */
static void sun4c_quick_kernel_fault(unsigned long address)
{
        printk("Kernel faults at addr 0x%08lx\n", address);
        panic("sun4c kernel fault handler bolixed...");
}

/*
 * 4 page buckets for task struct and kernel stack allocation.
 *
 * TASK_STACK_BEGIN
 * bucket[0]
 * bucket[1]
 *   [ ... ]
 * bucket[NR_TASKS-1]
 * TASK_STACK_BEGIN + (sizeof(struct task_bucket) * NR_TASKS)
 *
 * Each slot looks like:
 *
 *  page 1   --  task struct
 *  page 2   --  unmapped, for stack redzone (maybe use for pgd)
 *  page 3/4 --  kernel stack
 */

struct task_bucket {
	struct task_struct task;
	char _unused1[PAGE_SIZE - sizeof(struct task_struct)];
	char kstack[(PAGE_SIZE*3)];
};

struct task_bucket *sun4c_bucket[NR_TASKS];

#define BUCKET_EMPTY     ((struct task_bucket *) 0)
#define BUCKET_SIZE      (PAGE_SIZE << 2)
#define BUCKET_SHIFT     14        /* log2(sizeof(struct task_bucket)) */
#define BUCKET_NUM(addr) ((((addr) - SUN4C_LOCK_VADDR) >> BUCKET_SHIFT))
#define BUCKET_ADDR(num) (((num) << BUCKET_SHIFT) + SUN4C_LOCK_VADDR)
#define BUCKET_PTE(page)       \
        ((((page) - PAGE_OFFSET) >> PAGE_SHIFT) | pgprot_val(SUN4C_PAGE_KERNEL))
#define BUCKET_PTE_PAGE(pte)   \
        (PAGE_OFFSET + (((pte) & 0xffff) << PAGE_SHIFT))

static inline void get_locked_segment(unsigned long addr)
{
	struct sun4c_mmu_entry *stolen;
	unsigned long flags;

	save_and_cli(flags);
	addr &= SUN4C_REAL_PGDIR_MASK;
	stolen = sun4c_user_strategy();
	remove_ring(&sun4c_ufree_ring, stolen);
	stolen->vaddr = addr;
	flush_user_windows();
	sun4c_kernel_map(stolen);
	restore_flags(flags);
}

static inline void free_locked_segment(unsigned long addr)
{
	struct sun4c_mmu_entry *entry;
	unsigned long flags;
	unsigned char pseg;

	save_and_cli(flags);
	addr &= SUN4C_REAL_PGDIR_MASK;
	pseg = sun4c_get_segmap(addr);
	entry = &mmu_entry_pool[pseg];
	flush_user_windows();
	sun4c_flush_segment(addr);
	sun4c_kernel_unmap(entry);
	add_ring(&sun4c_ufree_ring, entry);
	restore_flags(flags);
}

static inline void garbage_collect(int entry)
{
	unsigned long flags;
	int start, end;

	save_and_cli(flags);

	/* 16 buckets per segment... */
	entry &= ~15;
	start = entry;
	for(end = (start + 16); start < end; start++)
		if(sun4c_bucket[start] != BUCKET_EMPTY)
			goto done;
	/* Entire segment empty, release it. */
	free_locked_segment(BUCKET_ADDR(entry));
done:
	restore_flags(flags);
}

static struct task_struct *sun4c_alloc_task_struct(void)
{
	unsigned long addr, page, flags;
	int entry;

	save_and_cli(flags);

	page = get_free_page(GFP_KERNEL);
	if(!page) {
		restore_flags(flags);
		return (struct task_struct *) 0;
	}
	/* XXX Bahh, linear search too slow, use hash
	 * XXX table in final implementation.  Or
	 * XXX keep track of first free when we free
	 * XXX a bucket... anything but this.
	 */
	for(entry = 0; entry < NR_TASKS; entry++)
		if(sun4c_bucket[entry] == BUCKET_EMPTY)
			break;
	if(entry == NR_TASKS) {
		free_page(page);
		restore_flags(flags);
		return (struct task_struct *) 0;
	}

	/* Prevent an alias from occurring while the page is ours. */
	sun4c_flush_page(page);

	addr = BUCKET_ADDR(entry);
	sun4c_bucket[entry] = (struct task_bucket *) addr;
	if(sun4c_get_segmap(addr) == invalid_segment)
		get_locked_segment(addr);
	sun4c_put_pte(addr, BUCKET_PTE(page));

	restore_flags(flags);

	return (struct task_struct *) addr;
}

static unsigned long sun4c_alloc_kernel_stack(struct task_struct *tsk)
{
	unsigned long saddr = (unsigned long) tsk;
	unsigned long page[2];

	if(!saddr)
		return 0;
	page[0] = __get_free_page(GFP_KERNEL);
	if(!page[0])
		return 0;
	page[1] = __get_free_page(GFP_KERNEL);
	if(!page[1]) {
		free_page(page[0]);
		return 0;
	}

	/* Prevent aliases from occurring while the pages are ours. */
	sun4c_flush_page(page[0]);
	sun4c_flush_page(page[1]);

	saddr += PAGE_SIZE << 1;
	sun4c_put_pte(saddr, BUCKET_PTE(page[0]));
	sun4c_put_pte(saddr + PAGE_SIZE, BUCKET_PTE(page[1]));
	return saddr;
}

static void sun4c_free_kernel_stack(unsigned long stack)
{
	unsigned long page[2];
	unsigned long flags;

	save_and_cli(flags);
	page[0] = BUCKET_PTE_PAGE(sun4c_get_pte(stack));
	page[1] = BUCKET_PTE_PAGE(sun4c_get_pte(stack+PAGE_SIZE));
	sun4c_flush_page(stack);
	sun4c_flush_page(stack + PAGE_SIZE);
	sun4c_put_pte(stack, 0);
	sun4c_put_pte(stack + PAGE_SIZE, 0);
	free_page(page[0]);
	free_page(page[1]);
	restore_flags(flags);
}

static void sun4c_free_task_struct(struct task_struct *tsk)
{
	unsigned long tsaddr = (unsigned long) tsk;
	unsigned long page = BUCKET_PTE_PAGE(sun4c_get_pte(tsaddr));
	unsigned long flags;
	int entry = BUCKET_NUM(tsaddr);

	save_and_cli(flags);
	sun4c_flush_page(tsaddr);
	sun4c_put_pte(tsaddr, 0);
	sun4c_bucket[entry] = BUCKET_EMPTY;
	free_page(page);
	garbage_collect(entry);
	restore_flags(flags);
}

__initfunc(static void sun4c_init_buckets(void))
{
	int entry;

	if(sizeof(struct task_bucket) != (PAGE_SIZE << 2)) {
		prom_printf("task bucket not 4 pages!\n");
		prom_halt();
	}
	for(entry = 0; entry < NR_TASKS; entry++)
		sun4c_bucket[entry] = BUCKET_EMPTY;
}

static unsigned long sun4c_iobuffer_start;
static unsigned long sun4c_iobuffer_end;
static unsigned long sun4c_iobuffer_high;
static unsigned long *sun4c_iobuffer_map;
static int iobuffer_map_size;

/*
 * Alias our pages so they do not cause a trap.
 * Also one page may be aliased into several I/O areas and we may
 * finish these I/O separately.
 */
static char *sun4c_lockarea(char *vaddr, unsigned long size)
{
	unsigned long base, scan;
	unsigned long npages;
	unsigned long vpage;
	unsigned long pte;
	unsigned long apage;
	unsigned long high;
	unsigned long flags;

	npages = (((unsigned long)vaddr & ~PAGE_MASK) +
		  size + (PAGE_SIZE-1)) >> PAGE_SHIFT;

	scan = 0;
	save_and_cli(flags);
	for (;;) {
		scan = find_next_zero_bit(sun4c_iobuffer_map,
					  iobuffer_map_size, scan);
		if ((base = scan) + npages > iobuffer_map_size) goto abend;
		for (;;) {
			if (scan >= base + npages) goto found;
			if (test_bit(scan, sun4c_iobuffer_map)) break;
			scan++;
		}
	}

found:
	high = ((base + npages) << PAGE_SHIFT) + sun4c_iobuffer_start;
	high = SUN4C_REAL_PGDIR_ALIGN(high);
	while (high > sun4c_iobuffer_high) {
		get_locked_segment(sun4c_iobuffer_high);
		sun4c_iobuffer_high += SUN4C_REAL_PGDIR_SIZE;
	}

	vpage = ((unsigned long) vaddr) & PAGE_MASK;
	for (scan = base; scan < base+npages; scan++) {
		pte = ((vpage-PAGE_OFFSET) >> PAGE_SHIFT);
 		pte |= pgprot_val(SUN4C_PAGE_KERNEL);
		pte |= _SUN4C_PAGE_NOCACHE;
		set_bit(scan, sun4c_iobuffer_map);
		apage = (scan << PAGE_SHIFT) + sun4c_iobuffer_start;

		/* Flush original mapping so we see the right things later. */
		sun4c_flush_page(vpage);

		sun4c_put_pte(apage, pte);
		vpage += PAGE_SIZE;
	}
	restore_flags(flags);
	return (char *) ((base << PAGE_SHIFT) + sun4c_iobuffer_start +
			 (((unsigned long) vaddr) & ~PAGE_MASK));

abend:
	restore_flags(flags);
	printk("DMA vaddr=0x%p size=%08lx\n", vaddr, size);
	panic("Out of iobuffer table");
	return 0;
}

static void sun4c_unlockarea(char *vaddr, unsigned long size)
{
	unsigned long vpage, npages;
	unsigned long flags;
	int scan, high;

	vpage = (unsigned long)vaddr & PAGE_MASK;
	npages = (((unsigned long)vaddr & ~PAGE_MASK) +
		  size + (PAGE_SIZE-1)) >> PAGE_SHIFT;

	save_and_cli(flags);
	while (npages != 0) {
		--npages;
		sun4c_put_pte(vpage, 0);
		clear_bit((vpage - sun4c_iobuffer_start) >> PAGE_SHIFT,
			  sun4c_iobuffer_map);
		vpage += PAGE_SIZE;
	}

	/* garbage collect */
	scan = (sun4c_iobuffer_high - sun4c_iobuffer_start) >> PAGE_SHIFT;
	while (scan >= 0 && !sun4c_iobuffer_map[scan >> 5])
		scan -= 32;
	scan += 32;
	high = sun4c_iobuffer_start + (scan << PAGE_SHIFT);
	high = SUN4C_REAL_PGDIR_ALIGN(high) + SUN4C_REAL_PGDIR_SIZE;
	while (high < sun4c_iobuffer_high) {
		sun4c_iobuffer_high -= SUN4C_REAL_PGDIR_SIZE;
		free_locked_segment(sun4c_iobuffer_high);
	}
	restore_flags(flags);
}

/* Note the scsi code at init time passes to here buffers
 * which sit on the kernel stack, those are already locked
 * by implication and fool the page locking code above
 * if passed to by mistake.
 */
static char *sun4c_get_scsi_one(char *bufptr, unsigned long len, struct linux_sbus *sbus)
{
	unsigned long page;

	page = ((unsigned long) bufptr) & PAGE_MASK;
	if(MAP_NR(page) > max_mapnr) {
		sun4c_flush_page(page);
		return bufptr; /* already locked */
	}
	return sun4c_lockarea(bufptr, len);
}

static void sun4c_get_scsi_sgl(struct mmu_sglist *sg, int sz, struct linux_sbus *sbus)
{
	while(sz >= 0) {
		sg[sz].dvma_addr = sun4c_lockarea(sg[sz].addr, sg[sz].len);
		sz--;
	}
}

static void sun4c_release_scsi_one(char *bufptr, unsigned long len, struct linux_sbus *sbus)
{
	unsigned long page = (unsigned long) bufptr;

	if(page < sun4c_iobuffer_start)
		return; /* On kernel stack or similar, see above */
	sun4c_unlockarea(bufptr, len);
}

static void sun4c_release_scsi_sgl(struct mmu_sglist *sg, int sz, struct linux_sbus *sbus)
{
	while(sz >= 0) {
		sun4c_unlockarea(sg[sz].dvma_addr, sg[sz].len);
		sz--;
	}
}

#define TASK_ENTRY_SIZE    BUCKET_SIZE /* see above */
#define LONG_ALIGN(x) (((x)+(sizeof(long))-1)&~((sizeof(long))-1))

struct vm_area_struct sun4c_kstack_vma;

__initfunc(static unsigned long sun4c_init_lock_areas(unsigned long start_mem))
{
	unsigned long sun4c_taskstack_start;
	unsigned long sun4c_taskstack_end;
	int bitmap_size;

	sun4c_init_buckets();
	sun4c_taskstack_start = SUN4C_LOCK_VADDR;
	sun4c_taskstack_end = (sun4c_taskstack_start +
			       (TASK_ENTRY_SIZE * NR_TASKS));
	if(sun4c_taskstack_end >= SUN4C_LOCK_END) {
		prom_printf("Too many tasks, decrease NR_TASKS please.\n");
		prom_halt();
	}

	sun4c_iobuffer_start = sun4c_iobuffer_high =
				SUN4C_REAL_PGDIR_ALIGN(sun4c_taskstack_end);
	sun4c_iobuffer_end = SUN4C_LOCK_END;
	bitmap_size = (sun4c_iobuffer_end - sun4c_iobuffer_start) >> PAGE_SHIFT;
	bitmap_size = (bitmap_size + 7) >> 3;
	bitmap_size = LONG_ALIGN(bitmap_size);
	iobuffer_map_size = bitmap_size << 3;
	sun4c_iobuffer_map = (unsigned long *) start_mem;
	memset((void *) start_mem, 0, bitmap_size);
	start_mem += bitmap_size;

	sun4c_kstack_vma.vm_mm = init_task.mm;
	sun4c_kstack_vma.vm_start = sun4c_taskstack_start;
	sun4c_kstack_vma.vm_end = sun4c_taskstack_end;
	sun4c_kstack_vma.vm_page_prot = PAGE_SHARED;
	sun4c_kstack_vma.vm_flags = VM_READ | VM_WRITE | VM_EXEC;
	insert_vm_struct(&init_mm, &sun4c_kstack_vma);
	return start_mem;
}

/* Cache flushing on the sun4c. */
static void sun4c_flush_cache_all(void)
{
	extern unsigned long bcopy;
	unsigned long begin, end, flags;

	begin = ((unsigned long) &bcopy);
	end = (begin + sun4c_vacinfo.num_bytes);

	save_and_cli(flags);
	flush_user_windows();
	if(sun4c_vacinfo.linesize == 32) {
		while(begin < end) {
			__asm__ __volatile__("
			ld	[%0 + 0x00], %%g0
			ld	[%0 + 0x20], %%g0
			ld	[%0 + 0x40], %%g0
			ld	[%0 + 0x60], %%g0
			ld	[%0 + 0x80], %%g0
			ld	[%0 + 0xa0], %%g0
			ld	[%0 + 0xc0], %%g0
			ld	[%0 + 0xe0], %%g0
			ld	[%0 + 0x100], %%g0
			ld	[%0 + 0x120], %%g0
			ld	[%0 + 0x140], %%g0
			ld	[%0 + 0x160], %%g0
			ld	[%0 + 0x180], %%g0
			ld	[%0 + 0x1a0], %%g0
			ld	[%0 + 0x1c0], %%g0
			ld	[%0 + 0x1e0], %%g0
			" : : "r" (begin));
			begin += 512;
		}
	} else {
		while(begin < end) {
			__asm__ __volatile__("
			ld	[%0 + 0x00], %%g0
			ld	[%0 + 0x10], %%g0
			ld	[%0 + 0x20], %%g0
			ld	[%0 + 0x30], %%g0
			ld	[%0 + 0x40], %%g0
			ld	[%0 + 0x50], %%g0
			ld	[%0 + 0x60], %%g0
			ld	[%0 + 0x70], %%g0
			ld	[%0 + 0x80], %%g0
			ld	[%0 + 0x90], %%g0
			ld	[%0 + 0xa0], %%g0
			ld	[%0 + 0xb0], %%g0
			ld	[%0 + 0xc0], %%g0
			ld	[%0 + 0xd0], %%g0
			ld	[%0 + 0xe0], %%g0
			ld	[%0 + 0xf0], %%g0
			" : : "r" (begin));
			begin += 256;
		}
	}
	restore_flags(flags);
}

static void sun4c_flush_cache_mm(struct mm_struct *mm)
{
	unsigned long flags;
	int octx, new_ctx = mm->context;

	if(new_ctx != NO_CONTEXT) {
		save_and_cli(flags);
		octx = sun4c_get_context();
		flush_user_windows();
		sun4c_set_context(new_ctx);
		sun4c_flush_context();
		sun4c_set_context(octx);
		restore_flags(flags);
	}
}

static void sun4c_flush_cache_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
	int size, size2, octx, i, new_ctx = mm->context;
	unsigned long start2, end2, flags;
	struct sun4c_mmu_entry *entry, *entry2;
	
#if KGPROF_PROFILING
	kgprof_profile();
#endif

	if(new_ctx != NO_CONTEXT) {
		save_and_cli(flags);

		size = end - start;

		octx = sun4c_get_context();
		flush_user_windows();
		sun4c_set_context(new_ctx);

		entry = sun4c_context_ring[new_ctx].ringhd.next;
		i = sun4c_context_ring[new_ctx].num_entries;
		while (i--) {
			entry2 = entry->next;
			if (entry->vaddr < start || entry->vaddr >= end) 
				goto next_entry;

			start2 = MAX(start,entry->vaddr);
			end2 = MIN(end,entry->vaddr+SUN4C_REAL_PGDIR_SIZE);
			size2 = end2 - start2;

			if (size2 <= (PAGE_SIZE << 3)) {
				start2 &= PAGE_MASK;
				while(start2 < end2) {
					sun4c_flush_page(start2);
					start2 += PAGE_SIZE;
				}
			} else {
				start2 &= SUN4C_REAL_PGDIR_MASK;
				sun4c_flush_segment(start2);

				/* We are betting that the entry will not be 
				 * needed for a while.
				 */
				sun4c_user_unmap(entry);
				free_user_entry(new_ctx, entry);
			}

		next_entry:
			entry = entry2;
		}
		sun4c_set_context(octx);
		restore_flags(flags);
	}
}

static void sun4c_flush_cache_page(struct vm_area_struct *vma, unsigned long page)
{
	unsigned long flags;
	struct mm_struct *mm = vma->vm_mm;
	int octx, new_ctx = mm->context;

	/* Sun4c has no separate I/D caches so cannot optimize for non
	 * text page flushes.
	 */
	if(new_ctx != NO_CONTEXT) {
		save_and_cli(flags);
		octx = sun4c_get_context();
		flush_user_windows();
		sun4c_set_context(new_ctx);
		sun4c_flush_page(page);
		sun4c_set_context(octx);
		restore_flags(flags);
	}
}

/* Even though sun4c is write through, a virtual cache alias inconsistancy
 * can still occur with COW page handling so we must flush anyways.
 */
static void sun4c_flush_page_to_ram(unsigned long page)
{
	sun4c_flush_page(page);
}

/* Sun4c cache is unified, both instructions and data live there, so
 * no need to flush the on-stack instructions for new signal handlers.
 */
static void sun4c_flush_sig_insns(struct mm_struct *mm, unsigned long insn_addr)
{
}

/* TLB flushing on the sun4c.  These routines count on the cache
 * flushing code to flush the user register windows so that we need
 * not do so when we get here.
 */

static void sun4c_flush_tlb_all(void)
{
	struct sun4c_mmu_entry *this_entry, *next_entry;
	unsigned long flags;
	int savectx, ctx;

	save_and_cli(flags);
	this_entry = sun4c_kernel_ring.ringhd.next;
	savectx = sun4c_get_context();
	flush_user_windows();
	while (sun4c_kernel_ring.num_entries) {
		next_entry = this_entry->next;
		sun4c_flush_segment(this_entry->vaddr);
		for(ctx = 0; ctx < num_contexts; ctx++) {
			sun4c_set_context(ctx);
			sun4c_put_segmap(this_entry->vaddr, invalid_segment);
		}
		free_kernel_entry(this_entry, &sun4c_kernel_ring);
		this_entry = next_entry;
	}
	sun4c_set_context(savectx);
	restore_flags(flags);
}

static void sun4c_flush_tlb_mm(struct mm_struct *mm)
{
	struct sun4c_mmu_entry *this_entry, *next_entry;
	struct sun4c_mmu_ring *crp;
	unsigned long flags;
	int savectx, new_ctx = mm->context;

	if(new_ctx != NO_CONTEXT) {
		save_and_cli(flags);
		crp = &sun4c_context_ring[new_ctx];
		savectx = sun4c_get_context();
		this_entry = crp->ringhd.next;
		flush_user_windows();
		sun4c_set_context(new_ctx);
		while(crp->num_entries) {
			next_entry = this_entry->next;
			sun4c_flush_segment(this_entry->vaddr);
			sun4c_user_unmap(this_entry);
			free_user_entry(new_ctx, this_entry);
			this_entry = next_entry;
		}
		sun4c_set_context(savectx);
		restore_flags(flags);
	}
}

static void sun4c_flush_tlb_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
	struct sun4c_mmu_entry *entry, *entry2;
	unsigned long flags;
	int i, savectx, new_ctx = mm->context;

	if(new_ctx == NO_CONTEXT)
		return;

#if KGPROF_PROFILING
	kgprof_profile();
#endif

	save_and_cli(flags);
	savectx = sun4c_get_context();
	flush_user_windows();
	sun4c_set_context(new_ctx);
	start &= SUN4C_REAL_PGDIR_MASK;

	entry = sun4c_context_ring[new_ctx].ringhd.next;
	i = sun4c_context_ring[new_ctx].num_entries;
	while (i--) {
		entry2 = entry->next;
		if (entry->vaddr >= start && entry->vaddr < end) {
			sun4c_flush_segment(entry->vaddr);
			sun4c_user_unmap(entry);
			free_user_entry(new_ctx, entry);
		}
		entry = entry2;
	}
	sun4c_set_context(savectx);
	restore_flags(flags);
}

static void sun4c_flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long flags;
	int savectx, new_ctx = mm->context;

	if(new_ctx != NO_CONTEXT) {
		save_and_cli(flags);
		savectx = sun4c_get_context();
		flush_user_windows();
		sun4c_set_context(new_ctx);
		page &= PAGE_MASK;
		sun4c_flush_page(page);
		sun4c_put_pte(page, 0);
		sun4c_set_context(savectx);
		restore_flags(flags);
	}
}

static void sun4c_set_pte(pte_t *ptep, pte_t pte)
{
	*ptep = pte;
}

void sun4c_mapioaddr(unsigned long physaddr, unsigned long virt_addr,
		     int bus_type, int rdonly)
{
	unsigned long page_entry;

	page_entry = ((physaddr >> PAGE_SHIFT) & 0xffff);
	page_entry |= ((pg_iobits | _SUN4C_PAGE_PRIV) & ~(_SUN4C_PAGE_PRESENT));
	if(rdonly)
		page_entry &= ~_SUN4C_WRITEABLE;
	sun4c_put_pte(virt_addr, page_entry);
}

void sun4c_unmapioaddr(unsigned long virt_addr)
{
	sun4c_put_pte(virt_addr, 0);
}

static inline void sun4c_alloc_context(struct mm_struct *mm)
{
	struct ctx_list *ctxp;

	ctxp = ctx_free.next;
	if(ctxp != &ctx_free) {
		remove_from_ctx_list(ctxp);
		add_to_used_ctxlist(ctxp);
		mm->context = ctxp->ctx_number;
		ctxp->ctx_mm = mm;
		return;
	}
	ctxp = ctx_used.next;
	if(ctxp->ctx_mm == current->mm)
		ctxp = ctxp->next;
	if(ctxp == &ctx_used)
		panic("out of mmu contexts");
	remove_from_ctx_list(ctxp);
	add_to_used_ctxlist(ctxp);
	ctxp->ctx_mm->context = NO_CONTEXT;
	ctxp->ctx_mm = mm;
	mm->context = ctxp->ctx_number;
	sun4c_demap_context(&sun4c_context_ring[ctxp->ctx_number],
			    ctxp->ctx_number);
}

#if some_day_soon /* We need some tweaking to start using this */
extern void force_user_fault(unsigned long, int);

void sun4c_switch_heuristic(struct pt_regs *regs)
{
	unsigned long sp = regs->u_regs[UREG_FP];
	unsigned long sp2 = sp + REGWIN_SZ - 0x8;

	force_user_fault(regs->pc, 0);
	force_user_fault(sp, 0);
	if((sp&PAGE_MASK) != (sp2&PAGE_MASK))
		force_user_fault(sp2, 0);
}
#endif

static void sun4c_switch_to_context(struct task_struct *tsk)
{
	struct ctx_list *ctx;
	unsigned long flags;

	save_and_cli(flags);
	if(tsk->mm->context == NO_CONTEXT) {
		sun4c_alloc_context(tsk->mm);
		goto set_context;
	}

	/* Update the LRU ring of contexts. */
	ctx = ctx_list_pool + tsk->mm->context;
	remove_from_ctx_list(ctx);
	add_to_used_ctxlist(ctx);

set_context:
	sun4c_set_context(tsk->mm->context);
	restore_flags(flags);
}

static void sun4c_flush_hook(void)
{
	if(current->tss.flags & SPARC_FLAG_KTHREAD) {
		unsigned long flags;

		save_flags(flags);
		sun4c_alloc_context(current->mm);
		sun4c_set_context(current->mm->context);
		restore_flags(flags);
	}
}

static void sun4c_exit_hook(void)
{
	struct ctx_list *ctx_old;
	struct mm_struct *mm = current->mm;

	if(mm->context != NO_CONTEXT && mm->count == 1) {
		unsigned long flags;

		save_and_cli(flags);
		sun4c_demap_context(&sun4c_context_ring[mm->context], mm->context);
		ctx_old = ctx_list_pool + mm->context;
		remove_from_ctx_list(ctx_old);
		add_to_free_ctxlist(ctx_old);
		mm->context = NO_CONTEXT;
		restore_flags(flags);
	}
}

#if KGPROF_PROFILING
static char s4cinfo[10240];
#else
static char s4cinfo[512];
#endif

static char *sun4c_mmu_info(void)
{
	int used_user_entries, i;

	used_user_entries = 0;
	for(i=0; i < num_contexts; i++)
		used_user_entries += sun4c_context_ring[i].num_entries;

	sprintf(s4cinfo, "vacsize\t\t: %d bytes\n"
		"vachwflush\t: %s\n"
		"vaclinesize\t: %d bytes\n"
		"mmuctxs\t\t: %d\n"
		"mmupsegs\t: %d\n"
		"kernelpsegs\t: %d\n"
		"kfreepsegs\t: %d\n"
		"usedpsegs\t: %d\n"
		"ufreepsegs\t: %d\n"
		"context\t\t: %d flushes\n"
		"segment\t\t: %d flushes\n"
		"page\t\t: %d flushes\n",
		sun4c_vacinfo.num_bytes,
		(sun4c_vacinfo.do_hwflushes ? "yes" : "no"),
		sun4c_vacinfo.linesize,
		num_contexts,
		(invalid_segment + 1),
		sun4c_kernel_ring.num_entries,
		sun4c_kfree_ring.num_entries,
		used_user_entries,
		sun4c_ufree_ring.num_entries,
		ctxflushes, segflushes, pageflushes);

#if KGPROF_PROFILING
	{
		char *p = s4cinfo + strlen(s4cinfo);
		int i,j;
		sprintf(p,"kgprof profiling:\n"); p += strlen(p);
		for (i=0;i<KGPROF_SIZE && kgprof_counters[i].addr[0];i++) {
			sprintf(p,"%5d  ",kgprof_counters[i].count); p += strlen(p);
			for (j=0;j<KGPROF_DEPTH;j++) {
				sprintf(p,"%08x ",kgprof_counters[i].addr[j]);
				p += strlen(p);
			}
			sprintf(p,"\n"); p += strlen(p);
		}
	}
#endif

	return s4cinfo;
}

/* Nothing below here should touch the mmu hardware nor the mmu_entry
 * data structures.
 */

static unsigned int sun4c_pmd_align(unsigned int addr) { return SUN4C_PMD_ALIGN(addr); }
static unsigned int sun4c_pgdir_align(unsigned int addr) { return SUN4C_PGDIR_ALIGN(addr); }

/* First the functions which the mid-level code uses to directly
 * manipulate the software page tables.  Some defines since we are
 * emulating the i386 page directory layout.
 */
#define PGD_PRESENT  0x001
#define PGD_RW       0x002
#define PGD_USER     0x004
#define PGD_ACCESSED 0x020
#define PGD_DIRTY    0x040
#define PGD_TABLE    (PGD_PRESENT | PGD_RW | PGD_USER | PGD_ACCESSED | PGD_DIRTY)

static unsigned long sun4c_vmalloc_start(void)
{
	return SUN4C_VMALLOC_START;
}

static int sun4c_pte_none(pte_t pte)		{ return !pte_val(pte); }
static int sun4c_pte_present(pte_t pte)
{
	return ((pte_val(pte) & (_SUN4C_PAGE_PRESENT | _SUN4C_PAGE_PRIV)) != 0);
}
static void sun4c_pte_clear(pte_t *ptep)	{ *ptep = __pte(0); }

static int sun4c_pmd_none(pmd_t pmd)		{ return !pmd_val(pmd); }
static int sun4c_pmd_bad(pmd_t pmd)
{
	return (((pmd_val(pmd) & ~PAGE_MASK) != PGD_TABLE) ||
		(MAP_NR(pmd_val(pmd)) > max_mapnr));
}

static int sun4c_pmd_present(pmd_t pmd)
{
	return ((pmd_val(pmd) & PGD_PRESENT) != 0);
}
static void sun4c_pmd_clear(pmd_t *pmdp)	{ *pmdp = __pmd(0); }

static int sun4c_pgd_none(pgd_t pgd)		{ return 0; }
static int sun4c_pgd_bad(pgd_t pgd)		{ return 0; }
static int sun4c_pgd_present(pgd_t pgd)	        { return 1; }
static void sun4c_pgd_clear(pgd_t * pgdp)	{ }

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
static int sun4c_pte_write(pte_t pte)
{
	return pte_val(pte) & _SUN4C_PAGE_WRITE;
}

static int sun4c_pte_dirty(pte_t pte)
{
	return pte_val(pte) & _SUN4C_PAGE_MODIFIED;
}

static int sun4c_pte_young(pte_t pte)
{
	return pte_val(pte) & _SUN4C_PAGE_ACCESSED;
}

static pte_t sun4c_pte_wrprotect(pte_t pte)
{
	return __pte(pte_val(pte) & ~(_SUN4C_PAGE_WRITE | _SUN4C_PAGE_SILENT_WRITE));
}

static pte_t sun4c_pte_mkclean(pte_t pte)
{
	return __pte(pte_val(pte) & ~(_SUN4C_PAGE_MODIFIED | _SUN4C_PAGE_SILENT_WRITE));
}

static pte_t sun4c_pte_mkold(pte_t pte)
{
	return __pte(pte_val(pte) & ~(_SUN4C_PAGE_ACCESSED | _SUN4C_PAGE_SILENT_READ));
}

static pte_t sun4c_pte_mkwrite(pte_t pte)
{
	pte = __pte(pte_val(pte) | _SUN4C_PAGE_WRITE);
	if (pte_val(pte) & _SUN4C_PAGE_MODIFIED)
		pte = __pte(pte_val(pte) | _SUN4C_PAGE_SILENT_WRITE);
	return pte;
}

static pte_t sun4c_pte_mkdirty(pte_t pte)
{
	pte = __pte(pte_val(pte) | _SUN4C_PAGE_MODIFIED);
	if (pte_val(pte) & _SUN4C_PAGE_WRITE)
		pte = __pte(pte_val(pte) | _SUN4C_PAGE_SILENT_WRITE);
	return pte;
}

static pte_t sun4c_pte_mkyoung(pte_t pte)
{
	pte = __pte(pte_val(pte) | _SUN4C_PAGE_ACCESSED);
	if (pte_val(pte) & _SUN4C_PAGE_READ)
		pte = __pte(pte_val(pte) | _SUN4C_PAGE_SILENT_READ);
	return pte;
}

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
static pte_t sun4c_mk_pte(unsigned long page, pgprot_t pgprot)
{
	return __pte(((page - PAGE_OFFSET) >> PAGE_SHIFT) | pgprot_val(pgprot));
}

static pte_t sun4c_mk_pte_phys(unsigned long phys_page, pgprot_t pgprot)
{
	return __pte((phys_page >> PAGE_SHIFT) | pgprot_val(pgprot));
}

static pte_t sun4c_mk_pte_io(unsigned long page, pgprot_t pgprot, int space)
{
	return __pte(((page - PAGE_OFFSET) >> PAGE_SHIFT) | pgprot_val(pgprot));
}

static pte_t sun4c_pte_modify(pte_t pte, pgprot_t newprot)
{
	return __pte((pte_val(pte) & _SUN4C_PAGE_CHG_MASK) |
		     pgprot_val(newprot));
}

static unsigned long sun4c_pte_page(pte_t pte)
{
	return (PAGE_OFFSET + ((pte_val(pte) & 0xffff) << (PAGE_SHIFT)));
}

static unsigned long sun4c_pmd_page(pmd_t pmd)
{
	return (pmd_val(pmd) & PAGE_MASK);
}

/* to find an entry in a page-table-directory */
pgd_t *sun4c_pgd_offset(struct mm_struct * mm, unsigned long address)
{
	return mm->pgd + (address >> SUN4C_PGDIR_SHIFT);
}

/* Find an entry in the second-level page table.. */
static pmd_t *sun4c_pmd_offset(pgd_t * dir, unsigned long address)
{
	return (pmd_t *) dir;
}

/* Find an entry in the third-level page table.. */ 
pte_t *sun4c_pte_offset(pmd_t * dir, unsigned long address)
{
	return (pte_t *) sun4c_pmd_page(*dir) +	((address >> PAGE_SHIFT) & (SUN4C_PTRS_PER_PTE - 1));
}

/* Update the root mmu directory. */
static void sun4c_update_rootmmu_dir(struct task_struct *tsk, pgd_t *pgdir)
{
}

/* Please take special note on the foo_kernel() routines below, our
 * fast in window fault handler wants to get at the pte's for vmalloc
 * area with traps off, therefore they _MUST_ be locked down to prevent
 * a watchdog from happening.  It only takes 4 pages of pte's to lock
 * down the maximum vmalloc space possible on sun4c so we statically
 * allocate these page table pieces in the kernel image.  Therefore
 * we should never have to really allocate or free any kernel page
 * table information.
 */

/* Allocate and free page tables. The xxx_kernel() versions are
 * used to allocate a kernel page table - this turns on ASN bits
 * if any, and marks the page tables reserved.
 */
static void sun4c_pte_free_kernel(pte_t *pte)
{
	/* This should never get called. */
	panic("sun4c_pte_free_kernel called, can't happen...");
}

static pte_t *sun4c_pte_alloc_kernel(pmd_t *pmd, unsigned long address)
{
	if(address >= SUN4C_LOCK_VADDR)
		return NULL;
	if (sun4c_pmd_none(*pmd))
		panic("sun4c_pmd_none for kernel pmd, can't happen...");
	if (sun4c_pmd_bad(*pmd)) {
		printk("Bad pmd in pte_alloc_kernel: %08lx\n", pmd_val(*pmd));
		*pmd = __pmd(PGD_TABLE | (unsigned long) BAD_PAGETABLE);
		return NULL;
	}
	return (pte_t *) sun4c_pmd_page(*pmd) + address;
}

/*
 * allocating and freeing a pmd is trivial: the 1-entry pmd is
 * inside the pgd, so has no extra memory associated with it.
 */
static void sun4c_pmd_free_kernel(pmd_t *pmd)
{
}

static pmd_t *sun4c_pmd_alloc_kernel(pgd_t *pgd, unsigned long address)
{
	return (pmd_t *) pgd;
}

static void sun4c_pte_free(pte_t *pte)
{
	free_page((unsigned long) pte);
}

static pte_t *sun4c_pte_alloc(pmd_t * pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (SUN4C_PTRS_PER_PTE - 1);
	if (sun4c_pmd_none(*pmd)) {
		pte_t *page = (pte_t *) get_free_page(GFP_KERNEL);
		if (sun4c_pmd_none(*pmd)) {
			if (page) {
				*pmd = __pmd(PGD_TABLE | (unsigned long) page);
				return page + address;
			}
			*pmd = __pmd(PGD_TABLE | (unsigned long) BAD_PAGETABLE);
			return NULL;
		}
		free_page((unsigned long) page);
	}
	if (sun4c_pmd_bad(*pmd)) {
		printk("Bad pmd in pte_alloc: %08lx\n", pmd_val(*pmd));
		*pmd = __pmd(PGD_TABLE | (unsigned long) BAD_PAGETABLE);
		return NULL;
	}
	return (pte_t *) sun4c_pmd_page(*pmd) + address;
}

/*
 * allocating and freeing a pmd is trivial: the 1-entry pmd is
 * inside the pgd, so has no extra memory associated with it.
 */
static void sun4c_pmd_free(pmd_t * pmd)
{
	*pmd = __pmd(0);
}

static pmd_t *sun4c_pmd_alloc(pgd_t * pgd, unsigned long address)
{
	return (pmd_t *) pgd;
}

static void sun4c_pgd_free(pgd_t *pgd)
{
	free_page((unsigned long) pgd);
}

static pgd_t *sun4c_pgd_alloc(void)
{
	return (pgd_t *) get_free_page(GFP_KERNEL);
}

/* There are really two cases of aliases to watch out for, and these
 * are:
 *
 *     1) A user's page which can be aliased with the kernels virtual
 *        mapping of the physical page.
 *
 *     2) Multiple user mappings of the same inode/anonymous object
 *        such that two copies of the same data for the same phys page
 *        can live (writable) in the cache at the same time.
 *
 * We handle number 1 by flushing the kernel copy of the page always
 * after COW page operations.
 *
 * NOTE: We are a bit slowed down now because the VMA arg is indeed used
 *       now, so our ref/mod bit tracking quick userfaults eat a few more
 *       cycles than they used to.
 */
void sun4c_update_mmu_cache(struct vm_area_struct *vma, unsigned long address, pte_t pte)
{
	unsigned long offset, vaddr, start, flags;
	pgd_t *pgdp;
	pte_t *ptep;

	save_and_cli(flags);
	address &= PAGE_MASK;
	if(sun4c_get_segmap(address) == invalid_segment)
		alloc_user_segment(address, sun4c_get_context());

	if((vma->vm_flags & (VM_WRITE|VM_SHARED)) == (VM_WRITE|VM_SHARED)) {
		struct vm_area_struct *vmaring;
		struct inode *inode;
		int alias_found = 0;

		/* While on the other hand, this is very uncommon... */
		inode = vma->vm_inode;
		if(!inode)
			goto done;

		offset = (address & PAGE_MASK) - vma->vm_start;
		vmaring = inode->i_mmap; 
		do {
			vaddr = vmaring->vm_start + offset;

			if (S4CVAC_BADALIAS(vaddr, address)) {
				alias_found++;
				start = vmaring->vm_start;
				while(start < vmaring->vm_end) {
					pgdp = sun4c_pgd_offset(vmaring->vm_mm, start);
					if(!pgdp) goto next;
					ptep = sun4c_pte_offset((pmd_t *) pgdp, start);
					if(!ptep) goto next;

					if(pte_val(*ptep) & _SUN4C_PAGE_PRESENT) {
#if 1
						printk("Fixing USER/USER alias [%d:%08lx]\n",
						       vmaring->vm_mm->context, start);
#endif
						sun4c_flush_cache_page(vmaring, start);
						*ptep = __pte(pte_val(*ptep) |
							      _SUN4C_PAGE_NOCACHE);
						sun4c_flush_tlb_page(vmaring, start);
					}
				next:
					start += PAGE_SIZE;
				}
			}
		} while ((vmaring = vmaring->vm_next_share) != inode->i_mmap);

		if(alias_found && !(pte_val(pte) & _SUN4C_PAGE_NOCACHE)) {
			pgdp = sun4c_pgd_offset(vma->vm_mm, address);
			ptep = sun4c_pte_offset((pmd_t *) pgdp, address);
			*ptep = __pte(pte_val(*ptep) | _SUN4C_PAGE_NOCACHE);
			pte = pte_val(*ptep);
		}
	}
done:
	sun4c_put_pte(address, pte_val(pte));
	restore_flags(flags);
}

static void sun4c_pgd_flush(pgd_t *pgdp)
{
}

extern unsigned long free_area_init(unsigned long, unsigned long);
extern unsigned long sparc_context_init(unsigned long, int);
extern unsigned long end;

__initfunc(unsigned long sun4c_paging_init(unsigned long start_mem, unsigned long end_mem))
{
	int i, cnt;
	unsigned long kernel_end, vaddr;
	extern unsigned long sparc_iobase_vaddr;

	kernel_end = (unsigned long) &end;
	kernel_end += (SUN4C_REAL_PGDIR_SIZE * 3);
	kernel_end = SUN4C_REAL_PGDIR_ALIGN(kernel_end);
	sun4c_probe_mmu();
	invalid_segment = (num_segmaps - 1);
	sun4c_init_mmu_entry_pool();
	sun4c_init_rings(&start_mem);
	sun4c_init_map_kernelprom(kernel_end);
	sun4c_init_clean_mmu(kernel_end);
	sun4c_init_fill_kernel_ring(SUN4C_KERNEL_BUCKETS);
	sun4c_init_lock_area(sparc_iobase_vaddr, IOBASE_END);
	sun4c_init_lock_area(DVMA_VADDR, DVMA_END);
	start_mem = sun4c_init_lock_areas(start_mem);
	sun4c_init_fill_user_ring();

	sun4c_set_context(0);
	memset(swapper_pg_dir, 0, PAGE_SIZE);
	memset(pg0, 0, PAGE_SIZE);
	memset(pg1, 0, PAGE_SIZE);
	memset(pg2, 0, PAGE_SIZE);
	memset(pg3, 0, PAGE_SIZE);

	/* Save work later. */
	vaddr = SUN4C_VMALLOC_START;
	swapper_pg_dir[vaddr>>SUN4C_PGDIR_SHIFT] = __pgd(PGD_TABLE | (unsigned long) pg0);
	vaddr += SUN4C_PGDIR_SIZE;
	swapper_pg_dir[vaddr>>SUN4C_PGDIR_SHIFT] = __pgd(PGD_TABLE | (unsigned long) pg1);
	vaddr += SUN4C_PGDIR_SIZE;
	swapper_pg_dir[vaddr>>SUN4C_PGDIR_SHIFT] = __pgd(PGD_TABLE | (unsigned long) pg2);
	vaddr += SUN4C_PGDIR_SIZE;
	swapper_pg_dir[vaddr>>SUN4C_PGDIR_SHIFT] = __pgd(PGD_TABLE | (unsigned long) pg3);
	sun4c_init_ss2_cache_bug();
	start_mem = PAGE_ALIGN(start_mem);
	start_mem = sparc_context_init(start_mem, num_contexts);
	start_mem = free_area_init(start_mem, end_mem);
	cnt = 0;
	for(i = 0; i < num_segmaps; i++)
		if(mmu_entry_pool[i].locked)
			cnt++;
	printk("SUN4C: %d mmu entries for the kernel\n", cnt);
	return start_mem;
}

/* Load up routines and constants for sun4c mmu */
__initfunc(void ld_mmu_sun4c(void))
{
	printk("Loading sun4c MMU routines\n");

	/* First the constants */
	pmd_shift = SUN4C_PMD_SHIFT;
	pmd_size = SUN4C_PMD_SIZE;
	pmd_mask = SUN4C_PMD_MASK;
	pgdir_shift = SUN4C_PGDIR_SHIFT;
	pgdir_size = SUN4C_PGDIR_SIZE;
	pgdir_mask = SUN4C_PGDIR_MASK;

	ptrs_per_pte = SUN4C_PTRS_PER_PTE;
	ptrs_per_pmd = SUN4C_PTRS_PER_PMD;
	ptrs_per_pgd = SUN4C_PTRS_PER_PGD;

	page_none = SUN4C_PAGE_NONE;
	page_shared = SUN4C_PAGE_SHARED;
	page_copy = SUN4C_PAGE_COPY;
	page_readonly = SUN4C_PAGE_READONLY;
	page_kernel = SUN4C_PAGE_KERNEL;
	pg_iobits = _SUN4C_PAGE_PRESENT | _SUN4C_READABLE | _SUN4C_WRITEABLE |
		    _SUN4C_PAGE_IO | _SUN4C_PAGE_NOCACHE;
	
	/* Functions */
	flush_cache_all = sun4c_flush_cache_all;
	flush_cache_mm = sun4c_flush_cache_mm;
	flush_cache_range = sun4c_flush_cache_range;
	flush_cache_page = sun4c_flush_cache_page;

	flush_tlb_all = sun4c_flush_tlb_all;
	flush_tlb_mm = sun4c_flush_tlb_mm;
	flush_tlb_range = sun4c_flush_tlb_range;
	flush_tlb_page = sun4c_flush_tlb_page;

	flush_page_to_ram = sun4c_flush_page_to_ram;

	flush_sig_insns = sun4c_flush_sig_insns;

	set_pte = sun4c_set_pte;
	switch_to_context = sun4c_switch_to_context;
	pmd_align = sun4c_pmd_align;
	pgdir_align = sun4c_pgdir_align;
	vmalloc_start = sun4c_vmalloc_start;

	pte_page = sun4c_pte_page;
	pmd_page = sun4c_pmd_page;

	sparc_update_rootmmu_dir = sun4c_update_rootmmu_dir;

	pte_none = sun4c_pte_none;
	pte_present = sun4c_pte_present;
	pte_clear = sun4c_pte_clear;

	pmd_none = sun4c_pmd_none;
	pmd_bad = sun4c_pmd_bad;
	pmd_present = sun4c_pmd_present;
	pmd_clear = sun4c_pmd_clear;

	pgd_none = sun4c_pgd_none;
	pgd_bad = sun4c_pgd_bad;
	pgd_present = sun4c_pgd_present;
	pgd_clear = sun4c_pgd_clear;

	mk_pte = sun4c_mk_pte;
	mk_pte_phys = sun4c_mk_pte_phys;
	mk_pte_io = sun4c_mk_pte_io;
	pte_modify = sun4c_pte_modify;
	pgd_offset = sun4c_pgd_offset;
	pmd_offset = sun4c_pmd_offset;
	pte_offset = sun4c_pte_offset;
	pte_free_kernel = sun4c_pte_free_kernel;
	pmd_free_kernel = sun4c_pmd_free_kernel;
	pte_alloc_kernel = sun4c_pte_alloc_kernel;
	pmd_alloc_kernel = sun4c_pmd_alloc_kernel;
	pte_free = sun4c_pte_free;
	pte_alloc = sun4c_pte_alloc;
	pmd_free = sun4c_pmd_free;
	pmd_alloc = sun4c_pmd_alloc;
	pgd_free = sun4c_pgd_free;
	pgd_alloc = sun4c_pgd_alloc;
	pgd_flush = sun4c_pgd_flush;

	pte_write = sun4c_pte_write;
	pte_dirty = sun4c_pte_dirty;
	pte_young = sun4c_pte_young;
	pte_wrprotect = sun4c_pte_wrprotect;
	pte_mkclean = sun4c_pte_mkclean;
	pte_mkold = sun4c_pte_mkold;
	pte_mkwrite = sun4c_pte_mkwrite;
	pte_mkdirty = sun4c_pte_mkdirty;
	pte_mkyoung = sun4c_pte_mkyoung;
	update_mmu_cache = sun4c_update_mmu_cache;
	mmu_exit_hook = sun4c_exit_hook;
	mmu_flush_hook = sun4c_flush_hook;
	mmu_lockarea = sun4c_lockarea;
	mmu_unlockarea = sun4c_unlockarea;

	mmu_get_scsi_one = sun4c_get_scsi_one;
	mmu_get_scsi_sgl = sun4c_get_scsi_sgl;
	mmu_release_scsi_one = sun4c_release_scsi_one;
	mmu_release_scsi_sgl = sun4c_release_scsi_sgl;

	mmu_map_dma_area = sun4c_map_dma_area;

        mmu_v2p = sun4c_v2p;
        mmu_p2v = sun4c_p2v;
	
	/* Task struct and kernel stack allocating/freeing. */
	alloc_kernel_stack = sun4c_alloc_kernel_stack;
	alloc_task_struct = sun4c_alloc_task_struct;
	free_kernel_stack = sun4c_free_kernel_stack;
	free_task_struct = sun4c_free_task_struct;

	quick_kernel_fault = sun4c_quick_kernel_fault;
	mmu_info = sun4c_mmu_info;

	/* These should _never_ get called with two level tables. */
	pgd_set = 0;
	pgd_page = 0;
}
