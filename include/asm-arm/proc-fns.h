/*
 * linux/include/asm-arm/proc-fns.h
 *
 * Copyright (C) 1997 Russell King
 */
#ifndef __ASM_PROCFNS_H
#define __ASM_PROCFNS_H

#include <asm/page.h>

#ifdef __KERNEL__
/*
 * Don't change this structure
 */
extern struct processor {
	const char *name;
	/* MISC
	 *
	 * flush caches for task switch
	 */
	void (*_switch_to)(void *prev, void *next);
	/*
	 * get data abort address/flags
	 */
	void (*_data_abort)(unsigned long pc);
	/*
	 * check for any bugs
	 */
	void (*_check_bugs)(void);
	/*
	 * Set up any processor specifics
	 */
	void (*_proc_init)(void);
	/*
	 * Disable any processor specifics
	 */
	void (*_proc_fin)(void);
	/*
	 * Processor architecture specific
	 */
	union {
		struct {
			/* CACHE
			 *
			 * flush all caches
			 */
			void (*_flush_cache_all)(void);
			/*
			 * flush a specific page or pages
			 */
			void (*_flush_cache_area)(unsigned long address, unsigned long end, int flags);
			/*
			 * flush cache entry for an address
			 */
			void (*_flush_cache_entry)(unsigned long address);
			/*
			 * flush a virtual address used for a page table
			 * note D-cache only!
			 */
			void (*_flush_cache_pte)(unsigned long address);
			/*
			 * flush a page to RAM
			 */
			void (*_flush_ram_page)(unsigned long page);
			/* TLB
			 *
			 * flush all TLBs
			 */
			void (*_flush_tlb_all)(void);
			/*
			 * flush a specific TLB
			 */
			void (*_flush_tlb_area)(unsigned long address, unsigned long end, int flags);
			/*
			 * Set a PMD (handling IMP bit 4)
			 */
			void (*_set_pmd)(pmd_t *pmdp, pmd_t pmd);
			/*
			 * Special stuff for a reset
			 */
			unsigned long (*reset)(void);
			/*
			 * flush an icached page
			 */
			void (*_flush_icache_area)(unsigned long start, unsigned long end);
			/*
			 * write back dirty cached data
			 */
			void (*_cache_wback_area)(unsigned long start, unsigned long end);
			/*
			 * purge cached data without (necessarily) writing it back
			 */
			void (*_cache_purge_area)(unsigned long start, unsigned long end);
		} armv3v4;
		struct {
			/* MEMC
			 *
			 * remap memc tables
			 */
			void (*_remap_memc)(void *tsk);
			/*
			 * update task's idea of mmap
			 */
			void (*_update_map)(void *tsk);
			/*
			 * update task's idea after abort
			 */
			void (*_update_mmu_cache)(void *vma, unsigned long addr, pte_t pte);
			/* XCHG
			 */
			unsigned long (*_xchg_1)(unsigned long x, volatile void *ptr);
			unsigned long (*_xchg_2)(unsigned long x, volatile void *ptr);
			unsigned long (*_xchg_4)(unsigned long x, volatile void *ptr);
		} armv2;
	} u;
} processor;
#endif	
#endif

