/*
 * Single CPU
 */
#ifdef __STDC__
#define __cpu_fn(name,x)	cpu_##name##x
#else
#define __cpu_fn(name,x)	cpu_/**/name/**/x
#endif
#define cpu_fn(name,x)		__cpu_fn(name,x)

/*
 * If we are supporting multiple CPUs, then
 * we must use a table of function pointers
 * for this lot.  Otherwise, we can optimise
 * the table away.
 */
#define cpu_data_abort			cpu_fn(CPU_NAME,_data_abort)
#define cpu_check_bugs			cpu_fn(CPU_NAME,_check_bugs)
#define cpu_proc_init			cpu_fn(CPU_NAME,_proc_init)
#define cpu_proc_fin			cpu_fn(CPU_NAME,_proc_fin)
#define cpu_do_idle			cpu_fn(CPU_NAME,_do_idle)

#define cpu_flush_cache_all		cpu_fn(CPU_NAME,_flush_cache_all)
#define cpu_flush_cache_area		cpu_fn(CPU_NAME,_flush_cache_area)
#define cpu_flush_cache_entry		cpu_fn(CPU_NAME,_flush_cache_entry)
#define cpu_clean_cache_area		cpu_fn(CPU_NAME,_clean_cache_area)
#define cpu_flush_ram_page		cpu_fn(CPU_NAME,_flush_ram_page)
#define cpu_flush_tlb_all		cpu_fn(CPU_NAME,_flush_tlb_all)
#define cpu_flush_tlb_area		cpu_fn(CPU_NAME,_flush_tlb_area)
#define cpu_flush_tlb_page		cpu_fn(CPU_NAME,_flush_tlb_page)
#define cpu_set_pgd			cpu_fn(CPU_NAME,_set_pgd)
#define cpu_set_pmd			cpu_fn(CPU_NAME,_set_pmd)
#define cpu_set_pte			cpu_fn(CPU_NAME,_set_pte)
#define cpu_reset			cpu_fn(CPU_NAME,_reset)
#define cpu_flush_icache_area		cpu_fn(CPU_NAME,_flush_icache_area)
#define cpu_cache_wback_area		cpu_fn(CPU_NAME,_cache_wback_area)
#define cpu_cache_purge_area		cpu_fn(CPU_NAME,_cache_purge_area)
#define cpu_flush_icache_page		cpu_fn(CPU_NAME,_flush_icache_page)

#ifndef __ASSEMBLY__

#include <asm/page.h>

/* forward declare task_struct */
struct task_struct;

/* declare all the functions as extern */
extern void cpu_data_abort(unsigned long pc);
extern void cpu_check_bugs(void);
extern void cpu_proc_init(void);
extern void cpu_proc_fin(void);
extern int cpu_do_idle(void);

extern void cpu_flush_cache_all(void);
extern void cpu_flush_cache_area(unsigned long address, unsigned long end, int flags);
extern void cpu_flush_cache_entry(unsigned long address);
extern void cpu_clean_cache_area(unsigned long start, unsigned long size);
extern void cpu_flush_ram_page(unsigned long page);
extern void cpu_flush_tlb_all(void);
extern void cpu_flush_tlb_area(unsigned long address, unsigned long end, int flags);
extern void cpu_flush_tlb_page(unsigned long address, int flags);
extern void cpu_set_pgd(unsigned long pgd_phys);
extern void cpu_set_pmd(pmd_t *pmdp, pmd_t pmd);
extern void cpu_set_pte(pte_t *ptep, pte_t pte);
extern volatile void cpu_reset(unsigned long addr);
extern void cpu_flush_icache_area(unsigned long start, unsigned long size);
extern void cpu_cache_wback_area(unsigned long start, unsigned long end);
extern void cpu_cache_purge_area(unsigned long start, unsigned long end);
extern void cpu_flush_icache_page(unsigned long virt);

#define cpu_switch_mm(pgd,tsk) cpu_set_pgd(__virt_to_phys((unsigned long)(pgd)))

#endif
