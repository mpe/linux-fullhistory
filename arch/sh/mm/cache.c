/* $Id: cache.c,v 1.10 2000/03/07 11:58:34 gniibe Exp $
 *
 *  linux/arch/sh/mm/cache.c
 *
 * Copyright (C) 1999  Niibe Yutaka
 *
 */

#include <linux/init.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/threads.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/cache.h>
#include <asm/io.h>
#include <asm/uaccess.h>

#if defined(__sh3__)
#define CCR		0xffffffec	/* Address of Cache Control Register */
#define CCR_CACHE_VAL	0x00000005	/* 8k-byte cache, P1-wb, enable */
#define CCR_CACHE_INIT	0x0000000d	/* 8k-byte cache, CF, P1-wb, enable */
#define CCR_CACHE_ENABLE	 1

#define CACHE_IC_ADDRESS_ARRAY 0xf0000000 /* SH-3 has unified cache system */
#define CACHE_OC_ADDRESS_ARRAY 0xf0000000
#define CACHE_VALID	  1
#define CACHE_UPDATED	  2

/* 7709A/7729 has 16K cache (256-entry), while 7702 has only 2K(direct)
   7702 is not supported (yet) */
struct _cache_system_info {
	int way_shift;
	int entry_mask;
	int num_entries;
};

static struct _cache_system_info cache_system_info;

#define CACHE_OC_WAY_SHIFT	(cache_system_info.way_shift)
#define CACHE_IC_WAY_SHIFT	(cache_system_info.way_shift)
#define CACHE_OC_ENTRY_SHIFT    4
#define CACHE_OC_ENTRY_MASK	(cache_system_info.entry_mask)
#define CACHE_IC_ENTRY_MASK	(cache_system_info.entry_mask)
#define CACHE_OC_NUM_ENTRIES	(cache_system_info.num_entries)
#define CACHE_OC_NUM_WAYS	4
#define CACHE_IC_NUM_WAYS	4
#elif defined(__SH4__)
#define CCR		 0xff00001c	/* Address of Cache Control Register */
#define CCR_CACHE_VAL	 0x00000105	/* 8k+16k-byte cache,P1-wb,enable */
#define CCR_CACHE_INIT	 0x0000090d	/* ICI,ICE(8k), OCI,P1-wb,OCE(16k) */
#define CCR_CACHE_ENABLE 0x00000101

#define CACHE_IC_ADDRESS_ARRAY 0xf0000000
#define CACHE_OC_ADDRESS_ARRAY 0xf4000000
#define CACHE_VALID	  1
#define CACHE_UPDATED	  2

#define CACHE_OC_WAY_SHIFT       13
#define CACHE_IC_WAY_SHIFT       13
#define CACHE_OC_ENTRY_SHIFT      5
#define CACHE_IC_ENTRY_SHIFT      5
#define CACHE_OC_ENTRY_MASK  0x3fe0
#define CACHE_OC_ENTRY_PHYS_MASK  0x0fe0
#define CACHE_IC_ENTRY_MASK  0x1fe0
#define CACHE_IC_NUM_ENTRIES	256
#define CACHE_OC_NUM_ENTRIES	512
#define CACHE_OC_NUM_WAYS	  1
#define CACHE_IC_NUM_WAYS	  1
#endif


/*
 * Write back all the cache.
 *
 * For SH-4, we only need to flush (write back) Operand Cache,
 * as Instruction Cache doesn't have "updated" data.
 *
 * Assumes that this is called in interrupt disabled context, and P2.
 * Shuld be INLINE function.
 */
static inline void cache_wback_all(void)
{
	unsigned long addr, data, i, j;

	for (i=0; i<CACHE_OC_NUM_ENTRIES; i++) {
		for (j=0; j<CACHE_OC_NUM_WAYS; j++) {
			addr = CACHE_OC_ADDRESS_ARRAY|(j<<CACHE_OC_WAY_SHIFT)|
				(i<<CACHE_OC_ENTRY_SHIFT);
			data = ctrl_inl(addr);
			if (data & CACHE_UPDATED) {
				data &= ~CACHE_UPDATED;
				ctrl_outl(data, addr);
			}
		}
	}
}

static void
detect_cpu_and_cache_system(void)
{
#if defined(__sh3__)
	unsigned long addr0, addr1, data0, data1, data2, data3;

	jump_to_P2();
	/*
	 * Check if the entry shadows or not.
	 * When shadowed, it's 128-entry system.
	 * Otherwise, it's 256-entry system.
	 */
	addr0 = CACHE_OC_ADDRESS_ARRAY + (3 << 12);
	addr1 = CACHE_OC_ADDRESS_ARRAY + (1 << 12);
	data0  = ctrl_inl(addr0);
	data0  ^= 0x00000001;
	ctrl_outl(data0,addr0);
	data1 = ctrl_inl(addr1);
	data2 = data1 ^ 0x00000001;
	ctrl_outl(data2,addr1);
	data3 = ctrl_inl(addr0);

	/* Invaliate them, in case the cache has been enabled already. */
	ctrl_outl(data0&~0x00000001, addr0);
	ctrl_outl(data2&~0x00000001, addr1);
	back_to_P1();

	if (data0 == data1 && data2 == data3) {	/* Shadow */
		cache_system_info.way_shift = 11;
		cache_system_info.entry_mask = 0x7f0;
		cache_system_info.num_entries = 128;
		cpu_data->type = CPU_SH7708;
	} else {				/* 7709A or 7729  */
		cache_system_info.way_shift = 12;
		cache_system_info.entry_mask = 0xff0;
		cache_system_info.num_entries = 256;
		cpu_data->type = CPU_SH7729;
	}
#elif defined(__SH4__)
	cpu_data->type = CPU_SH7750;
#endif
}

void __init cache_init(void)
{
	unsigned long ccr;

	detect_cpu_and_cache_system();

	ccr = ctrl_inl(CCR);
	if (ccr == CCR_CACHE_VAL)
		return;
	jump_to_P2();
	if (ccr & CCR_CACHE_ENABLE)
		/*
		 * XXX: Should check RA here. 
		 * If RA was 1, we only need to flush the half of the caches.
		 */
		cache_wback_all();

	ctrl_outl(CCR_CACHE_INIT, CCR);
	back_to_P1();
}

#if defined(__SH4__)
/*
 * SH-4 has virtually indexed and physically tagged cache.
 */

/*
 * Write back the dirty D-caches, but not invalidate them.
 *
 * START, END: Virtual Address
 */
static void dcache_wback_range(unsigned long start, unsigned long end)
{
	unsigned long v;

	start &= ~(L1_CACHE_BYTES-1);
	for (v = start; v < end; v+=L1_CACHE_BYTES) {
		asm volatile("ocbwb	%0"
			     : /* no output */
			     : "m" (__m(v)));
	}
}

/*
 * Invalidate I-caches.
 *
 * START, END: Virtual Address
 *
 */
static void icache_purge_range(unsigned long start, unsigned long end)
{
	unsigned long addr, data, v;

	start &= ~(L1_CACHE_BYTES-1);

	jump_to_P2();
	/*
	 * To handle the cache-line, we calculate the entry with virtual
	 * address: entry = vaddr & CACHE_IC_ENTRY_MASK.
	 *
	 * With A-bit "on", data written to is translated by MMU and
	 * compared the tag of cache and if it's not matched, nothing
	 * will be occurred.  (We can avoid flushing other caches.)
	 * 
	 */
	for (v = start; v < end; v+=L1_CACHE_BYTES) {
		addr = CACHE_IC_ADDRESS_ARRAY |	(v&CACHE_IC_ENTRY_MASK)
			| 0x8 /* A-bit */;
		data = (v&0xfffffc00); /* Valid=0 */
		ctrl_outl(data, addr);
	}
	back_to_P1();
}

/*
 * Write back the range of D-cache, and purge the I-cache.
 *
 * Called from sh/kernel/signal.c, after accessing the memory
 * through U0 area.  START and END is the address of U0.
 */
void flush_icache_range(unsigned long start, unsigned long end)
{
	dcache_wback_range(start, end);
	icache_purge_range(start, end);
}

/*
 * Invalidate the I-cache of the page (don't need to write back D-cache).
 *
 * Called from kernel/ptrace.c, mm/memory.c after flush_page_to_ram is called.
 */
void flush_icache_page(struct vm_area_struct *vma, struct page *pg)
{
	unsigned long phys, addr, data, i;

	/*
	 * Alas, we don't know where the virtual address is,
	 * So, we can't use icache_purge_range().
	 */

	/* Physical address of this page */
	phys = (pg - mem_map)*PAGE_SIZE + __MEMORY_START;

	jump_to_P2();
	/* Loop all the I-cache */
	for (i=0; i<CACHE_IC_NUM_ENTRIES; i++) {
		addr = CACHE_IC_ADDRESS_ARRAY| (i<<CACHE_IC_ENTRY_SHIFT);
		data = ctrl_inl(addr);
		if ((data & CACHE_VALID) && (data&PAGE_MASK) == phys) {
			data &= ~CACHE_VALID;
			ctrl_outl(data, addr);
		}
	}
	back_to_P1();
}

void flush_cache_all(void)
{
	unsigned long addr, data, i;
	unsigned long flags;

	save_and_cli(flags);
	jump_to_P2();

	/* Loop all the D-cache */
	for (i=0; i<CACHE_OC_NUM_ENTRIES; i++) {
		addr = CACHE_OC_ADDRESS_ARRAY|(i<<CACHE_OC_ENTRY_SHIFT);
		data = ctrl_inl(addr);
		if (data & CACHE_VALID) {
			data &= ~(CACHE_UPDATED|CACHE_VALID);
			ctrl_outl(data, addr);
		}
	}

	/* Loop all the I-cache */
	for (i=0; i<CACHE_IC_NUM_ENTRIES; i++) {
		addr = CACHE_IC_ADDRESS_ARRAY| (i<<CACHE_IC_ENTRY_SHIFT);
		data = ctrl_inl(addr);
		if (data & CACHE_VALID) {
			data &= ~CACHE_VALID;
			ctrl_outl(data, addr);
		}
	}

	back_to_P1();
	restore_flags(flags);
}

void flush_cache_mm(struct mm_struct *mm)
{
	/* Is there any good way? */
	/* XXX: possibly call flush_cache_range for each vm area */
	flush_cache_all();
}

void flush_cache_range(struct mm_struct *mm, unsigned long start,
		       unsigned long end)
{
	/*
	 * Calling
	 *	dcache_flush_range(start, end);
	 * is not good for the purpose of this function.  That is,
	 * flushing cache lines indexed by the virtual address is not
	 * sufficient.
	 *
	 * Instead, we need to flush the relevant cache lines which
	 * hold the data of the corresponding physical memory, as we
	 * have "alias" issues.
	 *
	 * This is needed because, kernel accesses the memory through
	 * P1-area (and/or U0-area) and user-space accesses through U0-area.
	 * And P1-area and U0-area may use different cache lines for
	 * same physical memory.
	 *
	 * If we would call dcache_flush_range(), the line of P1-area
	 * could remain in the cache, unflushed.
	 */
	unsigned long addr, data, v;

	start &= ~(L1_CACHE_BYTES-1);
	jump_to_P2();

	for (v = start; v < end; v+=L1_CACHE_BYTES) {
		addr = CACHE_OC_ADDRESS_ARRAY |
			(v&CACHE_OC_ENTRY_PHYS_MASK) | 0x8 /* A-bit */;
		data = (v&0xfffffc00); /* Update=0, Valid=0 */

		/* Try all the cases for aliases */
		ctrl_outl(data, addr);
		ctrl_outl(data, addr | 0x1000);
		ctrl_outl(data, addr | 0x2000);
		ctrl_outl(data, addr | 0x3000);
	}
	back_to_P1();
}

void flush_cache_page(struct vm_area_struct *vma, unsigned long addr)
{
	flush_cache_range(vma->vm_mm, addr, addr+PAGE_SIZE);
}

/*
 * After accessing the memory from kernel space (P1-area), we need to 
 * write back the cache line, to avoid "alias" issues.
 *
 * We search the D-cache to see if we have the entries corresponding to
 * the page, and if found, write back them.
 */
void flush_page_to_ram(struct page *pg)
{
	unsigned long phys, addr, data, i;

	/* Physical address of this page */
	phys = (pg - mem_map)*PAGE_SIZE + __MEMORY_START;

	jump_to_P2();
	/* Loop all the D-cache */
	for (i=0; i<CACHE_OC_NUM_ENTRIES; i++) {
		addr = CACHE_OC_ADDRESS_ARRAY| (i<<CACHE_OC_ENTRY_SHIFT);
		data = ctrl_inl(addr);
		if ((data & CACHE_UPDATED) && (data&PAGE_MASK) == phys) {
			data &= ~CACHE_UPDATED;
			ctrl_outl(data, addr);
		}
	}
	back_to_P1();
}
#endif
