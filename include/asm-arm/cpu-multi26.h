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
} processor;

extern const struct processor arm2_processor_functions;
extern const struct processor arm250_processor_functions;
extern const struct processor arm3_processor_functions;

#define cpu_data_abort(pc)			processor._data_abort(pc)
#define cpu_check_bugs()			processor._check_bugs()
#define cpu_proc_init()				processor._proc_init()
#define cpu_proc_fin()				processor._proc_fin()

#define cpu_remap_memc(tsk)			processor._remap_memc(tsk)
#define cpu_update_map(tsk)			processor._update_map(tsk)
#define cpu_update_mmu_cache(vma,addr,pte)	processor._update_mmu_cache(vma,addr,pte)
#define cpu_xchg_1(x,ptr)			processor._xchg_1(x,ptr)
#define cpu_xchg_2(x,ptr)			processor._xchg_2(x,ptr)
#define cpu_xchg_4(x,ptr)			processor._xchg_4(x,ptr)

#endif
