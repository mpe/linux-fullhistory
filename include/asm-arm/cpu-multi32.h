#ifndef __ASSEMBLY__

#include <asm/page.h>

/* forward-declare task_struct */
struct task_struct;

/*
 * Don't change this structure - ASM code
 * relies on it.
 */
extern struct processor {
	/* MISC
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
	 * clean a virtual address range from the
	 * D-cache without flushing the cache.
	 */
	void (*_clean_cache_area)(unsigned long start, unsigned long size);
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
	 * Set the page table
	 */
	void (*_set_pgd)(unsigned long pgd_phys);
	/*
	 * Set a PMD (handling IMP bit 4)
	 */
	void (*_set_pmd)(pmd_t *pmdp, pmd_t pmd);
	/*
	 * Set a PTE
	 */
	void (*_set_pte)(pte_t *ptep, pte_t pte);
	/*
	 * Special stuff for a reset
	 */
	volatile void (*reset)(unsigned long addr);
	/*
	 * flush an icached page
	 */
	void (*_flush_icache_area)(unsigned long start, unsigned long size);
	/*
	 * write back dirty cached data
	 */
	void (*_cache_wback_area)(unsigned long start, unsigned long end);
	/*
	 * purge cached data without (necessarily) writing it back
	 */
	void (*_cache_purge_area)(unsigned long start, unsigned long end);
	/*
	 * flush a specific TLB
	 */
	void (*_flush_tlb_page)(unsigned long address, int flags);
	/*
	 * Idle the processor
	 */
	int (*_do_idle)(int mode);
	/*
	 * flush I cache for a page
	 */
	void (*_flush_icache_page)(unsigned long address);
} processor;

extern const struct processor arm6_processor_functions;
extern const struct processor arm7_processor_functions;
extern const struct processor sa110_processor_functions;

#define cpu_data_abort(pc)			processor._data_abort(pc)
#define cpu_check_bugs()			processor._check_bugs()
#define cpu_proc_init()				processor._proc_init()
#define cpu_proc_fin()				processor._proc_fin()
#define cpu_do_idle(mode)			processor._do_idle(mode)

#define cpu_flush_cache_all()			processor._flush_cache_all()
#define cpu_flush_cache_area(start,end,flags)	processor._flush_cache_area(start,end,flags)
#define cpu_flush_cache_entry(addr)		processor._flush_cache_entry(addr)
#define cpu_clean_cache_area(start,size)	processor._clean_cache_area(start,size)
#define cpu_flush_ram_page(page)		processor._flush_ram_page(page)
#define cpu_flush_tlb_all()			processor._flush_tlb_all()
#define cpu_flush_tlb_area(start,end,flags)	processor._flush_tlb_area(start,end,flags)
#define cpu_flush_tlb_page(addr,flags)		processor._flush_tlb_page(addr,flags)
#define cpu_set_pgd(pgd)			processor._set_pgd(pgd)
#define cpu_set_pmd(pmdp, pmd)			processor._set_pmd(pmdp, pmd)
#define cpu_set_pte(ptep, pte)			processor._set_pte(ptep, pte)
#define cpu_reset(addr)				processor.reset(addr)
#define cpu_flush_icache_area(start,end)	processor._flush_icache_area(start,end)
#define cpu_cache_wback_area(start,end)		processor._cache_wback_area(start,end)
#define cpu_cache_purge_area(start,end)		processor._cache_purge_area(start,end)
#define cpu_flush_icache_page(virt)		processor._flush_icache_page(virt)

#define cpu_switch_mm(pgd,tsk)			cpu_set_pgd(__virt_to_phys((unsigned long)(pgd)))

#endif
