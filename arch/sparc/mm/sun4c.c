/* sun4c.c: Doing in software what should be done in hardware.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/kernel.h>
#include <linux/mm.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/vaddrs.h>
#include <asm/idprom.h>
#include <asm/machines.h>
#include <asm/memreg.h>
#include <asm/processor.h>

extern int num_segmaps, num_contexts;

/* Flushing the cache. */
struct sun4c_vac_props sun4c_vacinfo;
static int ctxflushes, segflushes, pageflushes;

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
	unsigned long vaddr;

	ctxflushes++;
	if(sun4c_vacinfo.do_hwflushes) {
		for(vaddr=0; vaddr < sun4c_vacinfo.num_bytes; vaddr+=PAGE_SIZE)
			__asm__ __volatile__("sta %%g0, [%0] %1\n\t" : :
					     "r" (vaddr), "i" (ASI_HWFLUSHCONTEXT));
	} else {
		int incr = sun4c_vacinfo.linesize;
		for(vaddr=0; vaddr < sun4c_vacinfo.num_bytes; vaddr+=incr)
			__asm__ __volatile__("sta %%g0, [%0] %1\n\t" : :
					     "r" (vaddr), "i" (ASI_FLUSHCTX));
	}
}

/* Scrape the segment starting at ADDR from the virtual cache. */
static inline void sun4c_flush_segment(unsigned long addr)
{
	unsigned long end;

	segflushes++;
	addr &= SUN4C_REAL_PGDIR_MASK;
	end = (addr + sun4c_vacinfo.num_bytes);
	if(sun4c_vacinfo.do_hwflushes) {
		for( ; addr < end; addr += PAGE_SIZE)
			__asm__ __volatile__("sta %%g0, [%0] %1\n\t" : :
					     "r" (addr), "i" (ASI_HWFLUSHSEG));
	} else {
		int incr = sun4c_vacinfo.linesize;
		for( ; addr < end; addr += incr)
			__asm__ __volatile__("sta %%g0, [%0] %1\n\t" : :
					     "r" (addr), "i" (ASI_FLUSHSEG));
	}
}

/* Bolix one page from the virtual cache. */
static inline void sun4c_flush_page(unsigned long addr)
{
	addr &= PAGE_MASK;

	pageflushes++;
	if(sun4c_vacinfo.do_hwflushes) {
		__asm__ __volatile__("sta %%g0, [%0] %1\n\t" : :
				     "r" (addr), "i" (ASI_HWFLUSHPAGE));
	} else {
		unsigned long end = addr + PAGE_SIZE;
		int incr = sun4c_vacinfo.linesize;

		for( ; addr < end; addr += incr)
			__asm__ __volatile__("sta %%g0, [%0] %1\n\t" : :
					     "r" (addr), "i" (ASI_FLUSHPG));
	}
}

/* The sun4c's do have an on chip store buffer.  And the way you
 * clear them out isn't so obvious.  The only way I can think of
 * to accomplish this is to read the current context register,
 * store the same value there, then do a bunch of nops for the
 * pipeline to clear itself completely.  This is only used for
 * dealing with memory errors, so it is not that critical.
 */
void sun4c_complete_all_stores(void)
{
	volatile int _unused;

	_unused = sun4c_get_context();
	sun4c_set_context(_unused);
	nop(); nop(); nop(); nop();
	nop(); nop(); nop(); nop();
	/* Is that enough? */
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
	sun4c_set_context(ctx);
}

void sun4c_probe_vac(void)
{
	int propval;

	sun4c_disable_vac();
	sun4c_vacinfo.num_bytes = prom_getintdefault(prom_root_node,
						     "vac-size", 65536);
	sun4c_vacinfo.linesize = prom_getintdefault(prom_root_node,
						    "vac-linesize", 16);
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

	propval = prom_getintdefault(prom_root_node, "vac_hwflush", -1);
	sun4c_vacinfo.do_hwflushes = (propval == -1 ?
				      prom_getintdefault(prom_root_node,
							 "vac-hwflush", 0) :
				      propval);

	if(sun4c_vacinfo.num_bytes != 65536) {
		prom_printf("WEIRD Sun4C VAC cache size, tell davem");
		prom_halt();
	}

	sun4c_flush_all();
	sun4c_enable_vac();
}

static void sun4c_probe_mmu(void)
{
	num_segmaps = prom_getintdefault(prom_root_node, "mmu-npmg", 128);
	num_contexts = prom_getintdefault(prom_root_node, "mmu-nctx", 0x8);
}

static inline void sun4c_init_ss2_cache_bug(void)
{
	extern unsigned long start;

	if(idprom->id_machtype == (SM_SUN4C | SM_4C_SS2)) {
		/* Whee.. */
		printk("SS2 cache bug detected, uncaching trap table page\n");
		sun4c_flush_page((unsigned int) &start);
		sun4c_put_pte(((unsigned long) &start),
			(sun4c_get_pte((unsigned long) &start) | _SUN4C_PAGE_NOCACHE));
	}
}

static inline unsigned long sun4c_init_alloc_dvma_pages(unsigned long start_mem)
{
	unsigned long addr, pte;

	for(addr = DVMA_VADDR; addr < DVMA_END; addr += PAGE_SIZE) {
		pte = (start_mem - PAGE_OFFSET) >> PAGE_SHIFT;
		pte |= (_SUN4C_PAGE_VALID | _SUN4C_PAGE_WRITE | _SUN4C_PAGE_NOCACHE);
		sun4c_put_pte(addr, pte);
		start_mem += PAGE_SIZE;
	}
	return start_mem;
}

/* TLB management. */
struct sun4c_mmu_entry {
	struct sun4c_mmu_entry *next;
	struct sun4c_mmu_entry *prev;
	unsigned long vaddr;
	unsigned char pseg;
	unsigned char locked;
};
static struct sun4c_mmu_entry mmu_entry_pool[256];

static void sun4c_init_mmu_entry_pool(void)
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

static void sun4c_init_lock_area(unsigned long start, unsigned long end)
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

struct sun4c_mmu_ring {
	struct sun4c_mmu_entry ringhd;
	int num_entries;
};
static struct sun4c_mmu_ring sun4c_context_ring[16]; /* used user entries */
static struct sun4c_mmu_ring sun4c_ufree_ring;       /* free user entries */
static struct sun4c_mmu_ring sun4c_kernel_ring;      /* used kernel entries */
static struct sun4c_mmu_ring sun4c_kfree_ring;       /* free kernel entries */

static inline void sun4c_init_rings(void)
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
	sun4c_kernel_ring.ringhd.next = sun4c_kernel_ring.ringhd.prev =
		&sun4c_kernel_ring.ringhd;
	sun4c_kfree_ring.ringhd.next = sun4c_kfree_ring.ringhd.prev =
		&sun4c_kfree_ring.ringhd;
	sun4c_ufree_ring.num_entries = sun4c_kernel_ring.num_entries =
		sun4c_kfree_ring.num_entries = 0;
}

static inline void add_ring(struct sun4c_mmu_ring *ring, struct sun4c_mmu_entry *entry)
{
	struct sun4c_mmu_entry *head = &ring->ringhd;

	entry->prev = head;
	(entry->next = head->next)->prev = entry;
	head->next = entry;
	ring->num_entries++;
}

static inline void remove_ring(struct sun4c_mmu_ring *ring, struct sun4c_mmu_entry *entry)
{
	struct sun4c_mmu_entry *next = entry->next;

	(next->prev = entry->prev)->next = next;
	ring->num_entries--;
}

static inline void recycle_ring(struct sun4c_mmu_ring *ring, struct sun4c_mmu_entry *entry)
{
	struct sun4c_mmu_entry *head = &ring->ringhd;
	struct sun4c_mmu_entry *next = entry->next;

	(next->prev = entry->prev)->next = next;
	entry->prev = head; (entry->next = head->next)->prev = entry;
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

static inline void free_kernel_entry(struct sun4c_mmu_entry *entry, struct sun4c_mmu_ring *ring)
{
        remove_ring(ring, entry);
        add_ring(&sun4c_kfree_ring, entry);
}

static inline void assign_kernel_entry(struct sun4c_mmu_entry *entry, struct sun4c_mmu_ring *ring) 
{
        remove_ring(ring, entry);
        add_ring(&sun4c_kernel_ring, entry);
}

static inline void reassign_kernel_entry(struct sun4c_mmu_entry *entry)
{
	recycle_ring(&sun4c_kernel_ring, entry);
}

static void sun4c_init_fill_kernel_ring(int howmany)
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

static void sun4c_init_fill_user_ring(void)
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
	flush_user_windows();
	sun4c_flush_segment(kentry->vaddr);
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
	flush_user_windows();
	for(ctx = 0; ctx < num_contexts; ctx++) {
		sun4c_set_context(ctx);
		sun4c_put_segmap(kentry->vaddr, kentry->pseg);
	}
	sun4c_set_context(savectx);
}

static inline void sun4c_user_unmap(struct sun4c_mmu_entry *uentry)
{
	sun4c_flush_segment(uentry->vaddr);
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
	int savectx = sun4c_get_context();

	this_entry = crp->ringhd.next;
	flush_user_windows();
	sun4c_set_context(ctx);
	while(crp->num_entries) {
		next_entry = this_entry->next;
		sun4c_user_unmap(this_entry);
		free_user_entry(ctx, this_entry);
		this_entry = next_entry;
	}
	sun4c_set_context(savectx);
}

static inline void sun4c_demap_one(struct sun4c_mmu_ring *crp, unsigned char ctx)
{
	struct sun4c_mmu_entry *entry = crp->ringhd.next;
	int savectx = sun4c_get_context();

	flush_user_windows();
	sun4c_set_context(ctx);
	sun4c_user_unmap(entry);
	free_user_entry(ctx, entry);
	sun4c_set_context(savectx);
}

/* Using this method to free up mmu entries eliminates a lot of
 * potential races since we have a kernel that incurs tlb
 * replacement faults.  There may be performance penalties.
 */
static inline struct sun4c_mmu_entry *sun4c_user_strategy(void)
{
	struct sun4c_mmu_ring *rp = 0;
	unsigned char mmuhog, i, ctx = 0;

	/* If some are free, return first one. */
	if(sun4c_ufree_ring.num_entries)
		return sun4c_ufree_ring.ringhd.next;

	/* Else free one up. */
	mmuhog = 0;
	for(i=0; i < num_contexts; i++) {
		if(sun4c_context_ring[i].num_entries > mmuhog) {
			rp = &sun4c_context_ring[i];
			mmuhog = rp->num_entries;
			ctx = i;
		}
	}
	sun4c_demap_one(rp, ctx);
	return sun4c_ufree_ring.ringhd.next;
}

static inline struct sun4c_mmu_entry *sun4c_kernel_strategy(void)
{
	struct sun4c_mmu_entry *this_entry;

	/* If some are free, return first one. */
	if(sun4c_kfree_ring.num_entries)
		return sun4c_kfree_ring.ringhd.next;

	/* Else free one up. */
	this_entry = sun4c_kernel_ring.ringhd.prev;
	sun4c_kernel_unmap(this_entry);
	free_kernel_entry(this_entry, &sun4c_kernel_ring);
	return sun4c_kfree_ring.ringhd.next;
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

static inline void alloc_kernel_segment(unsigned long address)
{
	struct sun4c_mmu_entry *entry;

	address &= SUN4C_REAL_PGDIR_MASK;
	entry = sun4c_kernel_strategy();

	assign_kernel_entry(entry, &sun4c_kfree_ring);
	entry->vaddr = address;
	sun4c_kernel_map(entry);
}

/* XXX Just like kernel tlb replacement we'd like to have a low level
 * XXX equivalent for user faults which need not go through the mm
 * XXX subsystem just to load a mmu entry.  But this might not be as
 * XXX feasible since we need to go through the kernel page tables
 * XXX for this process, which we currently don't lock into the mmu
 * XXX so we would fault with traps off... must think about this...
 */
static void sun4c_update_mmu_cache(struct vm_area_struct *vma, unsigned long address, pte_t pte)
{
	unsigned long flags;

	save_flags(flags); cli();
	address &= PAGE_MASK;
	if(sun4c_get_segmap(address) == invalid_segment)
		alloc_user_segment(address, sun4c_get_context());
	sun4c_put_pte(address, pte_val(pte));
	restore_flags(flags);
}

/* READ THIS:  If you put any diagnostic printing code in any of the kernel
 *             fault handling code you will lose badly.  This is the most
 *             delicate piece of code in the entire kernel, atomicity of
 *             kernel tlb replacement must be guaranteed.  This is why we
 *             have separate user and kernel allocation rings to alleviate
 *             as many bad interactions as possible.
 *
 * XXX Someday make this into a fast in-window trap handler to avoid
 * XXX any and all races.  *High* priority, also for performance.
 */
static void sun4c_quick_kernel_fault(unsigned long address)
{
	unsigned long end, flags;

	save_flags(flags); cli();
	address &= SUN4C_REAL_PGDIR_MASK;
	end = address + SUN4C_REAL_PGDIR_SIZE;
	if(sun4c_get_segmap(address) == invalid_segment)
		alloc_kernel_segment(address);

	if(address < SUN4C_VMALLOC_START) {
		unsigned long pte;
		pte = (address - PAGE_OFFSET) >> PAGE_SHIFT;
		pte |= pgprot_val(SUN4C_PAGE_KERNEL);
		/* Stupid pte tricks... */
		while(address < end) {
			sun4c_put_pte(address, pte++);
			address += PAGE_SIZE;
		}
	} else {
		pte_t *ptep;

		ptep = (pte_t *) (PAGE_MASK & pgd_val(swapper_pg_dir[address>>SUN4C_PGDIR_SHIFT]));
		ptep = (ptep + ((address >> PAGE_SHIFT) & (SUN4C_PTRS_PER_PTE - 1)));
		while(address < end) {
			sun4c_put_pte(address, pte_val(*ptep++));
			address += PAGE_SIZE;
		}
	}
	restore_flags(flags);
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

static inline void get_task_segment(unsigned long addr)
{
	struct sun4c_mmu_entry *stolen;
	unsigned long flags;

	save_flags(flags); cli();
	addr &= SUN4C_REAL_PGDIR_MASK;
	stolen = sun4c_user_strategy();
	remove_ring(&sun4c_ufree_ring, stolen);
	stolen->vaddr = addr;
	sun4c_kernel_map(stolen);
	restore_flags(flags);
}

static inline void free_task_segment(unsigned long addr)
{
	struct sun4c_mmu_entry *entry;
	unsigned long flags;
	unsigned char pseg;

	save_flags(flags); cli();
	addr &= SUN4C_REAL_PGDIR_MASK;
	pseg = sun4c_get_segmap(addr);
	entry = &mmu_entry_pool[pseg];
	sun4c_flush_segment(addr);
	sun4c_kernel_unmap(entry);
	add_ring(&sun4c_ufree_ring, entry);
	restore_flags(flags);
}

static inline void garbage_collect(int entry)
{
	int start, end;

	/* 16 buckets per segment... */
	entry &= ~15;
	start = entry;
	for(end = (start + 16); start < end; start++)
		if(sun4c_bucket[start] != BUCKET_EMPTY)
			return;
	/* Entire segment empty, release it. */
	free_task_segment(BUCKET_ADDR(entry));
}

static struct task_struct *sun4c_alloc_task_struct(void)
{
	unsigned long addr, page;
	int entry;

	page = get_free_page(GFP_KERNEL);
	if(!page)
		return (struct task_struct *) 0;
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
		return (struct task_struct *) 0;
	}
	addr = BUCKET_ADDR(entry);
	sun4c_bucket[entry] = (struct task_bucket *) addr;
	if(sun4c_get_segmap(addr) == invalid_segment)
		get_task_segment(addr);
	sun4c_put_pte(addr, BUCKET_PTE(page));
	return (struct task_struct *) addr;
}

static unsigned long sun4c_alloc_kernel_stack(struct task_struct *tsk)
{
	unsigned long saddr = (unsigned long) tsk;
	unsigned long page[3];

	if(!saddr)
		return 0;
	page[0] = get_free_page(GFP_KERNEL);
	if(!page[0])
		return 0;
	page[1] = get_free_page(GFP_KERNEL);
	if(!page[1]) {
		free_page(page[0]);
		return 0;
	}
	page[2] = get_free_page(GFP_KERNEL);
	if(!page[2]) {
		free_page(page[0]);
		free_page(page[1]);
		return 0;
	}
	saddr += PAGE_SIZE;
	sun4c_put_pte(saddr, BUCKET_PTE(page[0]));
	sun4c_put_pte(saddr + PAGE_SIZE, BUCKET_PTE(page[1]));
	sun4c_put_pte(saddr + (PAGE_SIZE<<1), BUCKET_PTE(page[2]));
	return saddr;
}

static void sun4c_free_kernel_stack(unsigned long stack)
{
	unsigned long page[3];

	page[0] = BUCKET_PTE_PAGE(sun4c_get_pte(stack));
	page[1] = BUCKET_PTE_PAGE(sun4c_get_pte(stack+PAGE_SIZE));
	page[2] = BUCKET_PTE_PAGE(sun4c_get_pte(stack+(PAGE_SIZE<<1)));
	sun4c_flush_segment(stack & SUN4C_REAL_PGDIR_MASK);
	sun4c_put_pte(stack, 0);
	sun4c_put_pte(stack + PAGE_SIZE, 0);
	sun4c_put_pte(stack + (PAGE_SIZE<<1), 0);
	free_page(page[0]);
	free_page(page[1]);
	free_page(page[2]);
}

static void sun4c_free_task_struct(struct task_struct *tsk)
{
	unsigned long tsaddr = (unsigned long) tsk;
	unsigned long page = BUCKET_PTE_PAGE(sun4c_get_pte(tsaddr));
	int entry = BUCKET_NUM(tsaddr);

	sun4c_flush_segment(tsaddr & SUN4C_REAL_PGDIR_MASK);
	sun4c_put_pte(tsaddr, 0);
	sun4c_bucket[entry] = BUCKET_EMPTY;
	free_page(page);
	garbage_collect(entry);
}

static void sun4c_init_buckets(void)
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

	npages = (((unsigned long)vaddr & ~PAGE_MASK) +
		  size + (PAGE_SIZE-1)) >> PAGE_SHIFT;

	scan = 0;
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
	vpage = ((unsigned long) vaddr) & PAGE_MASK;
	for (scan = base; scan < base+npages; scan++) {
		pte = ((vpage-PAGE_OFFSET) >> PAGE_SHIFT);
 		pte |= pgprot_val(SUN4C_PAGE_KERNEL);
		pte |= _SUN4C_PAGE_NOCACHE;
		set_bit(scan, sun4c_iobuffer_map);
		apage = (scan << PAGE_SHIFT) + sun4c_iobuffer_start;
		sun4c_flush_page(vpage);
		sun4c_put_pte(apage, pte);
		vpage += PAGE_SIZE;
	}
	return (char *) ((base << PAGE_SHIFT) + sun4c_iobuffer_start +
			 (((unsigned long) vaddr) & ~PAGE_MASK));

abend:
	printk("DMA vaddr=0x%p size=%08lx\n", vaddr, size);
	panic("Out of iobuffer table");
	return 0;
}

static void sun4c_unlockarea(char *vaddr, unsigned long size)
{
	unsigned long vpage, npages;

	vpage = (unsigned long)vaddr & PAGE_MASK;
	npages = (((unsigned long)vaddr & ~PAGE_MASK) +
		  size + (PAGE_SIZE-1)) >> PAGE_SHIFT;
	while (npages != 0) {
		--npages;
		sun4c_put_pte(vpage, 0);
		clear_bit((vpage - sun4c_iobuffer_start) >> PAGE_SHIFT,
			  sun4c_iobuffer_map);
		vpage += PAGE_SIZE;
	}
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
	if(page > high_memory)
		return bufptr; /* already locked */
	return sun4c_lockarea(bufptr, len);
}

static void sun4c_get_scsi_sgl(struct mmu_sglist *sg, int sz, struct linux_sbus *sbus)
{
	while(sz >= 0) {
		sg[sz].alt_addr = sun4c_lockarea(sg[sz].addr, sg[sz].len);
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
		sun4c_unlockarea(sg[sz].alt_addr, sg[sz].len);
		sg[sz].alt_addr = 0;
		sz--;
	}
}

#define TASK_ENTRY_SIZE    BUCKET_SIZE /* see above */
#define LONG_ALIGN(x) (((x)+(sizeof(long))-1)&~((sizeof(long))-1))

struct vm_area_struct sun4c_kstack_vma;

static unsigned long sun4c_init_lock_areas(unsigned long start_mem)
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

	sun4c_iobuffer_start = SUN4C_REAL_PGDIR_ALIGN(sun4c_taskstack_end);
	sun4c_iobuffer_end = SUN4C_LOCK_END;
	bitmap_size = (sun4c_iobuffer_end - sun4c_iobuffer_start) >> PAGE_SHIFT;
	bitmap_size = (bitmap_size + 7) >> 3;
	bitmap_size = LONG_ALIGN(bitmap_size);
	iobuffer_map_size = bitmap_size << 3;
	sun4c_iobuffer_map = (unsigned long *) start_mem;
	memset((void *) start_mem, 0, bitmap_size);
	start_mem += bitmap_size;

	/* Now get us some mmu entries for I/O maps. */
	sun4c_init_lock_area(sun4c_iobuffer_start, sun4c_iobuffer_end);
	sun4c_kstack_vma.vm_mm = init_task.mm;
	sun4c_kstack_vma.vm_start = sun4c_taskstack_start;
	sun4c_kstack_vma.vm_end = sun4c_taskstack_end;
	sun4c_kstack_vma.vm_page_prot = PAGE_SHARED;
	sun4c_kstack_vma.vm_flags = VM_READ | VM_WRITE | VM_EXEC;
	insert_vm_struct(&init_task, &sun4c_kstack_vma);
	return start_mem;
}

/* Cache flushing on the sun4c. */
static void sun4c_flush_cache_all(void)
{
	unsigned long start, end;

	/* Clear all tags in the sun4c cache.
	 * The cache is write through so this is safe.
	 */
	start = AC_CACHETAGS;
	end = start + sun4c_vacinfo.num_bytes;
	flush_user_windows();
	while(start < end) {
		__asm__ __volatile__("sta %%g0, [%0] %1\n\t" : :
				     "r" (start), "i" (ASI_CONTROL));
		start += sun4c_vacinfo.linesize;
	}
}

static void sun4c_flush_cache_mm(struct mm_struct *mm)
{
	unsigned long flags;
	int octx;

#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif
		octx = sun4c_get_context();
		save_flags(flags); cli();
		flush_user_windows();
		sun4c_set_context(mm->context);
		sun4c_flush_context();
		sun4c_set_context(octx);
		restore_flags(flags);
#ifndef __SMP__
	}
#endif
}

static void sun4c_flush_cache_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
	unsigned long flags;
	int size, octx;

#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif
		size = start - end;

		flush_user_windows();

		if(size >= sun4c_vacinfo.num_bytes)
			goto flush_it_all;

		save_flags(flags); cli();
		octx = sun4c_get_context();
		sun4c_set_context(mm->context);

		if(size <= (PAGE_SIZE << 1)) {
			start &= PAGE_MASK;
			while(start < end) {
				sun4c_flush_page(start);
				start += PAGE_SIZE;
			};
		} else {
			start &= SUN4C_REAL_PGDIR_MASK;
			while(start < end) {
				sun4c_flush_segment(start);
				start += SUN4C_REAL_PGDIR_SIZE;
			}
		}
		sun4c_set_context(octx);
		restore_flags(flags);
#ifndef __SMP__
	}
#endif
	return;

flush_it_all:
	/* Cache size bounded flushing, thank you. */
	sun4c_flush_cache_all();
}

static void sun4c_flush_cache_page(struct vm_area_struct *vma, unsigned long page)
{
	unsigned long flags;
	int octx;
	struct mm_struct *mm = vma->vm_mm;

	/* Sun4c has no separate I/D caches so cannot optimize for non
	 * text page flushes.
	 */
#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif
		octx = sun4c_get_context();
		save_flags(flags); cli();
		flush_user_windows();
		sun4c_set_context(mm->context);
		sun4c_flush_page(page);
		sun4c_set_context(octx);
		restore_flags(flags);
#ifndef __SMP__
	}
#endif
}

/* Sun4c cache is write-through, so no need to validate main memory
 * during a page copy in kernel space.
 */
static void sun4c_flush_page_to_ram(unsigned long page)
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

	save_flags(flags); cli();
	this_entry = sun4c_kernel_ring.ringhd.next;
	savectx = sun4c_get_context();
	while(sun4c_kernel_ring.num_entries) {
		next_entry = this_entry->next;
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
	int savectx, ctx;

#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif
		crp = &sun4c_context_ring[mm->context];
		savectx = sun4c_get_context();
		ctx = mm->context;
		this_entry = crp->ringhd.next;
		sun4c_set_context(mm->context);
		while(crp->num_entries) {
			next_entry = this_entry->next;
			sun4c_user_unmap(this_entry);
			free_user_entry(ctx, this_entry);
			this_entry = next_entry;
		}
		sun4c_set_context(savectx);
#ifndef __SMP__
	}
#endif
}

static void sun4c_flush_tlb_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
	struct sun4c_mmu_entry *this_entry;
	unsigned char pseg, savectx;

#ifndef __SMP__
	if(mm->context == NO_CONTEXT)
		return;
#endif
	flush_user_windows();
	savectx = sun4c_get_context();
	sun4c_set_context(mm->context);
	start &= SUN4C_REAL_PGDIR_MASK;
	while(start < end) {
		pseg = sun4c_get_segmap(start);
		if(pseg == invalid_segment)
			goto next_one;
		this_entry = &mmu_entry_pool[pseg];
		sun4c_put_segmap(this_entry->vaddr, invalid_segment);
		free_user_entry(mm->context, this_entry);
	next_one:
		start += SUN4C_REAL_PGDIR_SIZE;
	}
	sun4c_set_context(savectx);
}

static void sun4c_flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;
	int savectx;

#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif
		savectx = sun4c_get_context();
		sun4c_set_context(mm->context);
		page &= PAGE_MASK;
		if(sun4c_get_pte(page) & _SUN4C_PAGE_VALID)
			sun4c_put_pte(page, 0);
		sun4c_set_context(savectx);
#ifndef __SMP__
	}
#endif
}

/* Sun4c mmu hardware doesn't update the dirty bit in the pte's
 * for us, so we do it in software.
 */
static void sun4c_set_pte(pte_t *ptep, pte_t pte)
{

	if((pte_val(pte) & (_SUN4C_PAGE_WRITE|_SUN4C_PAGE_DIRTY)) ==
	   _SUN4C_PAGE_WRITE)
		pte_val(pte) |= _SUN4C_PAGE_DIRTY;

	*ptep = pte;
}

/* static */ void sun4c_mapioaddr(unsigned long physaddr, unsigned long virt_addr,
		     int bus_type, int rdonly)
{
	unsigned long page_entry;

	page_entry = ((physaddr >> PAGE_SHIFT) & 0xffff);
	page_entry |= (_SUN4C_PAGE_VALID | _SUN4C_PAGE_WRITE |
		       _SUN4C_PAGE_NOCACHE | _SUN4C_PAGE_IO);
	if(rdonly)
		page_entry &= (~_SUN4C_PAGE_WRITE);
	sun4c_flush_page(virt_addr);
	sun4c_put_pte(virt_addr, page_entry);
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
	sun4c_demap_context(&sun4c_context_ring[ctxp->ctx_number], ctxp->ctx_number);
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
	/* Kernel threads can execute in any context and so can tasks
	 * sleeping in the middle of exiting. If this task has already
	 * been allocated a piece of the mmu realestate, just jump to
	 * it.
	 */
	if((tsk->tss.flags & SPARC_FLAG_KTHREAD) ||
	   (tsk->flags & PF_EXITING))
		return;
	if(tsk->mm->context == NO_CONTEXT)
		sun4c_alloc_context(tsk->mm);

	sun4c_set_context(tsk->mm->context);
}

static void sun4c_flush_hook(void)
{
	if(current->tss.flags & SPARC_FLAG_KTHREAD) {
		sun4c_alloc_context(current->mm);
		sun4c_set_context(current->mm->context);
	}
}

static void sun4c_exit_hook(void)
{
	struct ctx_list *ctx_old;
	struct mm_struct *mm = current->mm;

	if(mm->context != NO_CONTEXT) {
		sun4c_demap_context(&sun4c_context_ring[mm->context], mm->context);
		ctx_old = ctx_list_pool + mm->context;
		remove_from_ctx_list(ctx_old);
		add_to_free_ctxlist(ctx_old);
		mm->context = NO_CONTEXT;
	}
}

static char s4cinfo[512];

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
		used_user_entries,
		sun4c_ufree_ring.num_entries,
		ctxflushes, segflushes, pageflushes);

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
static int sun4c_pte_present(pte_t pte)	        { return pte_val(pte) & _SUN4C_PAGE_VALID; }
static void sun4c_pte_clear(pte_t *ptep)	{ pte_val(*ptep) = 0; }

static int sun4c_pmd_none(pmd_t pmd)		{ return !pmd_val(pmd); }
static int sun4c_pmd_bad(pmd_t pmd)
{
	return (pmd_val(pmd) & ~PAGE_MASK) != PGD_TABLE || pmd_val(pmd) > high_memory;
}

static int sun4c_pmd_present(pmd_t pmd)	        { return pmd_val(pmd) & PGD_PRESENT; }
static void sun4c_pmd_clear(pmd_t *pmdp)	{ pmd_val(*pmdp) = 0; }

static int sun4c_pgd_none(pgd_t pgd)		{ return 0; }
static int sun4c_pgd_bad(pgd_t pgd)		{ return 0; }
static int sun4c_pgd_present(pgd_t pgd)	        { return 1; }
static void sun4c_pgd_clear(pgd_t * pgdp)	{ }

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
static int sun4c_pte_write(pte_t pte)		{ return pte_val(pte) & _SUN4C_PAGE_WRITE; }
static int sun4c_pte_dirty(pte_t pte)		{ return pte_val(pte) & _SUN4C_PAGE_DIRTY; }
static int sun4c_pte_young(pte_t pte)		{ return pte_val(pte) & _SUN4C_PAGE_REF; }

static pte_t sun4c_pte_wrprotect(pte_t pte)	{ pte_val(pte) &= ~_SUN4C_PAGE_WRITE; return pte; }
static pte_t sun4c_pte_mkclean(pte_t pte)	{ pte_val(pte) &= ~_SUN4C_PAGE_DIRTY; return pte; }
static pte_t sun4c_pte_mkold(pte_t pte)	        { pte_val(pte) &= ~_SUN4C_PAGE_REF; return pte; }
static pte_t sun4c_pte_mkwrite(pte_t pte)	{ pte_val(pte) |= _SUN4C_PAGE_WRITE; return pte; }
static pte_t sun4c_pte_mkdirty(pte_t pte)	{ pte_val(pte) |= _SUN4C_PAGE_DIRTY; return pte; }
static pte_t sun4c_pte_mkyoung(pte_t pte)	{ pte_val(pte) |= _SUN4C_PAGE_REF; return pte; }

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
static pte_t sun4c_mk_pte(unsigned long page, pgprot_t pgprot)
{
	return __pte(((page - PAGE_OFFSET) >> PAGE_SHIFT) | pgprot_val(pgprot));
}

static pte_t sun4c_mk_pte_io(unsigned long page, pgprot_t pgprot, int space)
{
	return __pte(((page - PAGE_OFFSET) >> PAGE_SHIFT) | pgprot_val(pgprot));
}

static pte_t sun4c_pte_modify(pte_t pte, pgprot_t newprot)
{
	return __pte((pte_val(pte) & _SUN4C_PAGE_CHG_MASK) | pgprot_val(newprot));
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
static pgd_t *sun4c_pgd_offset(struct mm_struct * mm, unsigned long address)
{
	return mm->pgd + (address >> SUN4C_PGDIR_SHIFT);
}

/* Find an entry in the second-level page table.. */
static pmd_t *sun4c_pmd_offset(pgd_t * dir, unsigned long address)
{
	return (pmd_t *) dir;
}

/* Find an entry in the third-level page table.. */ 
static pte_t *sun4c_pte_offset(pmd_t * dir, unsigned long address)
{
	return (pte_t *) sun4c_pmd_page(*dir) +	((address >> PAGE_SHIFT) & (SUN4C_PTRS_PER_PTE - 1));
}

/* Update the root mmu directory. */
static void sun4c_update_rootmmu_dir(struct task_struct *tsk, pgd_t *pgdir)
{
}

/* Allocate and free page tables. The xxx_kernel() versions are
 * used to allocate a kernel page table - this turns on ASN bits
 * if any, and marks the page tables reserved.
 */
static void sun4c_pte_free_kernel(pte_t *pte)
{
	free_page((unsigned long) pte);
}

static pte_t *sun4c_pte_alloc_kernel(pmd_t *pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (SUN4C_PTRS_PER_PTE - 1);
	if (sun4c_pmd_none(*pmd)) {
		pte_t *page = (pte_t *) get_free_page(GFP_KERNEL);
		if (sun4c_pmd_none(*pmd)) {
			if (page) {
				pmd_val(*pmd) = PGD_TABLE | (unsigned long) page;
				return page + address;
			}
			pmd_val(*pmd) = PGD_TABLE | (unsigned long) BAD_PAGETABLE;
			return NULL;
		}
		free_page((unsigned long) page);
	}
	if (sun4c_pmd_bad(*pmd)) {
		printk("Bad pmd in pte_alloc_kernel: %08lx\n", pmd_val(*pmd));
		pmd_val(*pmd) = PGD_TABLE | (unsigned long) BAD_PAGETABLE;
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
	pmd_val(*pmd) = 0;
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
				pmd_val(*pmd) = PGD_TABLE | (unsigned long) page;
				return page + address;
			}
			pmd_val(*pmd) = PGD_TABLE | (unsigned long) BAD_PAGETABLE;
			return NULL;
		}
		free_page((unsigned long) page);
	}
	if (sun4c_pmd_bad(*pmd)) {
		printk("Bad pmd in pte_alloc: %08lx\n", pmd_val(*pmd));
		pmd_val(*pmd) = PGD_TABLE | (unsigned long) BAD_PAGETABLE;
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
	pmd_val(*pmd) = 0;
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

#define SUN4C_KERNEL_BUCKETS   16
extern unsigned long free_area_init(unsigned long, unsigned long);
extern unsigned long sparc_context_init(unsigned long, int);
extern unsigned long end;

unsigned long sun4c_paging_init(unsigned long start_mem, unsigned long end_mem)
{
	int i, cnt;
	unsigned long kernel_end;

	kernel_end = (unsigned long) &end;
	kernel_end += (SUN4C_REAL_PGDIR_SIZE * 3);
	kernel_end = SUN4C_REAL_PGDIR_ALIGN(kernel_end);
	sun4c_probe_mmu();
	invalid_segment = (num_segmaps - 1);
	sun4c_init_mmu_entry_pool();
	sun4c_init_rings();
	sun4c_init_map_kernelprom(kernel_end);
	sun4c_init_clean_mmu(kernel_end);
	sun4c_init_fill_kernel_ring(SUN4C_KERNEL_BUCKETS);
	sun4c_init_lock_area(IOBASE_VADDR, IOBASE_END);
	sun4c_init_lock_area(DVMA_VADDR, DVMA_END);
	start_mem = sun4c_init_lock_areas(start_mem);
	sun4c_init_fill_user_ring();

	sun4c_set_context(0);
	memset(swapper_pg_dir, 0, PAGE_SIZE);
	memset(pg0, 0, PAGE_SIZE);
	/* Save work later. */
	pgd_val(swapper_pg_dir[SUN4C_VMALLOC_START>>SUN4C_PGDIR_SHIFT]) =
		PGD_TABLE | (unsigned long) pg0;
	sun4c_init_ss2_cache_bug();
	start_mem = PAGE_ALIGN(start_mem);
	start_mem = sun4c_init_alloc_dvma_pages(start_mem);
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
void ld_mmu_sun4c(void)
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
	pg_iobits = _SUN4C_PAGE_NOCACHE | _SUN4C_PAGE_IO | _SUN4C_PAGE_VALID
	    | _SUN4C_PAGE_WRITE | _SUN4C_PAGE_DIRTY;
	
	/* Functions */
#ifndef __SMP__
	flush_cache_all = sun4c_flush_cache_all;
	flush_cache_mm = sun4c_flush_cache_mm;
	flush_cache_range = sun4c_flush_cache_range;
	flush_cache_page = sun4c_flush_cache_page;

	flush_tlb_all = sun4c_flush_tlb_all;
	flush_tlb_mm = sun4c_flush_tlb_mm;
	flush_tlb_range = sun4c_flush_tlb_range;
	flush_tlb_page = sun4c_flush_tlb_page;
#else
	local_flush_cache_all = sun4c_flush_cache_all;
	local_flush_cache_mm = sun4c_flush_cache_mm;
	local_flush_cache_range = sun4c_flush_cache_range;
	local_flush_cache_page = sun4c_flush_cache_page;

	local_flush_tlb_all = sun4c_flush_tlb_all;
	local_flush_tlb_mm = sun4c_flush_tlb_mm;
	local_flush_tlb_range = sun4c_flush_tlb_range;
	local_flush_tlb_page = sun4c_flush_tlb_page;

	flush_cache_all = smp_flush_cache_all;
	flush_cache_mm = smp_flush_cache_mm;
	flush_cache_range = smp_flush_cache_range;
	flush_cache_page = smp_flush_cache_page;

	flush_tlb_all = smp_flush_tlb_all;
	flush_tlb_mm = smp_flush_tlb_mm;
	flush_tlb_range = smp_flush_tlb_range;
	flush_tlb_page = smp_flush_tlb_page;
#endif

	flush_page_to_ram = sun4c_flush_page_to_ram;

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
