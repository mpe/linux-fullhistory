/* $Id: cache.c,v 1.7 1999/09/23 11:43:07 gniibe Exp $
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
#define CCR_CACHE_INIT	 0x0000090d	/* 8k+16k-byte cache,CF,P1-wb,enable */
#define CCR_CACHE_ENABLE 0x00000101

#define CACHE_IC_ADDRESS_ARRAY 0xf0000000
#define CACHE_OC_ADDRESS_ARRAY 0xf4000000
#define CACHE_VALID	  1
#define CACHE_UPDATED	  2

#define CACHE_OC_WAY_SHIFT       13
#define CACHE_IC_WAY_SHIFT       13
#define CACHE_OC_ENTRY_SHIFT      5
#define CACHE_OC_ENTRY_MASK  0x3fe0
#define CACHE_IC_ENTRY_MASK  0x1fe0
#define CACHE_OC_NUM_ENTRIES	512
#define CACHE_OC_NUM_WAYS	  1
#define CACHE_IC_NUM_WAYS	  1
#endif

#define jump_to_p2(__dummy) \
	asm volatile("mova	1f,%0\n\t" \
		     "add	%1,%0\n\t" \
		     "jmp	@r0		! Jump to P2 area\n\t" \
		     " nop\n\t" \
		     ".balign 4\n" \
		     "1:" \
		     : "=&z" (__dummy) \
		     : "r" (0x20000000))

#define back_to_p1(__dummy) \
	asm volatile("nop;nop;nop;nop;nop;nop\n\t" \
		     "mova	9f,%0\n\t" \
		     "sub	%1,%0\n\t" \
		     "jmp	@r0		! Back to P1 area\n\t" \
		     " nop\n\t" \
		     ".balign 4\n" \
		     "9:" \
		     : "=&z" (__dummy) \
		     : "r" (0x20000000), "0" (__dummy))

/* Write back caches to memory (if needed) and invalidates the caches */
void cache_flush_area(unsigned long start, unsigned long end)
{
	unsigned long flags, __dummy;
	unsigned long addr, data, v, p;

	start &= ~(L1_CACHE_BYTES-1);
	save_and_cli(flags);
	jump_to_p2(__dummy);

	for (v = start; v < end; v+=L1_CACHE_BYTES) {
		p = __pa(v);
		addr = CACHE_IC_ADDRESS_ARRAY |
			(v&CACHE_IC_ENTRY_MASK) | 0x8 /* A-bit */;
		data = (v&0xfffffc00); /* U=0, V=0 */
		ctrl_outl(data,addr);
#if CACHE_IC_ADDRESS_ARRAY != CACHE_OC_ADDRESS_ARRAY
		asm volatile("ocbp	%0"
			     : /* no output */
			     : "m" (__m(v)));
#endif
	}
	back_to_p1(__dummy);
	restore_flags(flags);
}

/* Purge (just invalidate, no write back) the caches */
/* This is expected to work well.. but..

   On SH7708S, the write-back cache is written back on "purge".
   (it's not expected, though).

   It seems that we have no way to just purge (with no write back action)
   the cache line. */
void cache_purge_area(unsigned long start, unsigned long end)
{
	unsigned long flags, __dummy;
	unsigned long addr, data, v, p, j;

	start &= ~(L1_CACHE_BYTES-1);
	save_and_cli(flags);
	jump_to_p2(__dummy);

	for (v = start; v < end; v+=L1_CACHE_BYTES) {
		p = __pa(v);
		for (j=0; j<CACHE_IC_NUM_WAYS; j++) {
			addr = CACHE_IC_ADDRESS_ARRAY|(j<<CACHE_IC_WAY_SHIFT)|
				(v&CACHE_IC_ENTRY_MASK);
			data = ctrl_inl(addr);
			if ((data & 0xfffffc00) == (p&0xfffffc00)
			    && (data & CACHE_VALID)) {
				data &= ~CACHE_VALID;
				ctrl_outl(data,addr);
				break;
			}
		}
#if CACHE_IC_ADDRESS_ARRAY != CACHE_OC_ADDRESS_ARRAY
		asm volatile("ocbi	%0"
			     : /* no output */
			     : "m" (__m(v)));
#endif
	}
	back_to_p1(__dummy);
	restore_flags(flags);
}

/* write back the dirty cache, but not invalidate the cache */
void cache_wback_area(unsigned long start, unsigned long end)
{
	unsigned long flags, __dummy;
	unsigned long v;

	start &= ~(L1_CACHE_BYTES-1);
	save_and_cli(flags);
	jump_to_p2(__dummy);

	for (v = start; v < end; v+=L1_CACHE_BYTES) {
#if CACHE_IC_ADDRESS_ARRAY == CACHE_OC_ADDRESS_ARRAY
		unsigned long addr, data, j;
		unsigned long p = __pa(v);

		for (j=0; j<CACHE_OC_NUM_WAYS; j++) {
			addr = CACHE_OC_ADDRESS_ARRAY|(j<<CACHE_OC_WAY_SHIFT)|
				(v&CACHE_OC_ENTRY_MASK);
			data = ctrl_inl(addr);
			if ((data & 0xfffffc00) == (p&0xfffffc00)
			    && (data & CACHE_VALID)
			    && (data & CACHE_UPDATED)) {
				data &= ~CACHE_UPDATED;
				ctrl_outl(data,addr);
				break;
			}
		}
#else
		asm volatile("ocbwb	%0"
			     : /* no output */
			     : "m" (__m(v)));
#endif
	}
	back_to_p1(__dummy);
	restore_flags(flags);
}

/*
 * Write back the cache.
 *
 * For SH-4, flush (write back) Operand Cache, as Instruction Cache
 * doesn't have "updated" data.
 */
static void cache_wback_all(void)
{
	unsigned long flags, __dummy;
	unsigned long addr, data, i, j;

	save_and_cli(flags);
	jump_to_p2(__dummy);

	for (i=0; i<CACHE_OC_NUM_ENTRIES; i++) {
		for (j=0; j<CACHE_OC_NUM_WAYS; j++) {
			addr = CACHE_OC_ADDRESS_ARRAY|(j<<CACHE_OC_WAY_SHIFT)|
				(i<<CACHE_OC_ENTRY_SHIFT);
			data = ctrl_inl(addr);
			if (data & CACHE_VALID) {
				data &= ~(CACHE_VALID|CACHE_UPDATED);
				ctrl_outl(data,addr);
			}
		}
	}

	back_to_p1(__dummy);
	restore_flags(flags);
}

static void
detect_cpu_and_cache_system(void)
{
#if defined(__sh3__)
	unsigned long __dummy, addr0, addr1, data0, data1, data2, data3;

	jump_to_p2(__dummy);
	/* Check if the entry shadows or not.
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
	ctrl_outl(data0&~0x00000001,addr0);
	ctrl_outl(data2&~0x00000001,addr1);
	back_to_p1(__dummy);

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
	unsigned long __dummy, ccr;

	detect_cpu_and_cache_system();

	ccr = ctrl_inl(CCR);
	if (ccr == CCR_CACHE_VAL)
		return;
	if (ccr & CCR_CACHE_ENABLE)
		/* Should check RA here. If RA was 1,
		   we only need to flush the half of the caches. */
		cache_wback_all();

	jump_to_p2(__dummy);
	ctrl_outl(CCR_CACHE_INIT, CCR);
	back_to_p1(__dummy);
}

#if defined(__SH4__)
/* Write back data caches, and invalidates instructiin caches */
void flush_icache_range(unsigned long start, unsigned long end)
{
	unsigned long flags, __dummy;
	unsigned long addr, data, v;

	start &= ~(L1_CACHE_BYTES-1);
	save_and_cli(flags);
	jump_to_p2(__dummy);

	for (v = start; v < end; v+=L1_CACHE_BYTES) {
		/* Write back O Cache */
		asm volatile("ocbwb	%0"
			     : /* no output */
			     : "m" (__m(v)));
		/* Invalidate I Cache */
		addr = CACHE_IC_ADDRESS_ARRAY |
			(v&CACHE_IC_ENTRY_MASK) | 0x8 /* A-bit */;
		data = (v&0xfffffc00); /* Valid=0 */
		ctrl_outl(data,addr);
	}
	back_to_p1(__dummy);
	restore_flags(flags);
}

void flush_cache_all(void)
{
	unsigned long flags,__dummy;

	/* Write back Operand Cache */
	cache_wback_all ();

	/* Then, invalidate Instruction Cache and Operand Cache */
	save_and_cli(flags);
	jump_to_p2(__dummy);
	ctrl_outl(CCR_CACHE_INIT, CCR);
	back_to_p1(__dummy);
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
	unsigned long flags, __dummy;
	unsigned long addr, data, v;

	start &= ~(L1_CACHE_BYTES-1);
	save_and_cli(flags);
	jump_to_p2(__dummy);

	for (v = start; v < end; v+=L1_CACHE_BYTES) {
		addr = CACHE_IC_ADDRESS_ARRAY |
			(v&CACHE_IC_ENTRY_MASK) | 0x8 /* A-bit */;
		data = (v&0xfffffc00); /* Update=0, Valid=0 */
		ctrl_outl(data,addr);
		addr = CACHE_OC_ADDRESS_ARRAY |
			(v&CACHE_OC_ENTRY_MASK) | 0x8 /* A-bit */;
		ctrl_outl(data,addr);
	}
	back_to_p1(__dummy);
	restore_flags(flags);
}

void flush_cache_page(struct vm_area_struct *vma, unsigned long addr)
{
	flush_cache_range(vma->vm_mm, addr, addr+PAGE_SIZE);
}

void flush_page_to_ram(unsigned long page)
{				/* Page is in physical address */
	/* XXX: for the time being... */
	flush_cache_all();
}
#endif
