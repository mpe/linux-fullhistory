/*
 * Cache flushing...
 */
#define flush_cache_all()						\
	cpu_flush_cache_all()

#define flush_cache_mm(_mm)						\
	do {								\
		if ((_mm) == current->mm)				\
			cpu_flush_cache_all();				\
	} while (0)

#define flush_cache_range(_mm,_start,_end)				\
	do {								\
		if ((_mm) == current->mm)				\
			cpu_flush_cache_area((_start), (_end), 1);	\
	} while (0)

#define flush_cache_page(_vma,_vmaddr)					\
	do {								\
		if ((_vma)->vm_mm == current->mm)			\
			cpu_flush_cache_area((_vmaddr),			\
				(_vmaddr) + PAGE_SIZE,			\
				((_vma)->vm_flags & VM_EXEC));		\
	} while (0)

#define clean_cache_range(_start,_end)					\
	do {								\
		unsigned long _s, _sz;					\
		_s = (unsigned long)_start;				\
		_sz = (unsigned long)_end - _s;				\
		cpu_clean_cache_area(_s, _sz);				\
	} while (0)

#define clean_cache_area(_start,_size)					\
	do {								\
		unsigned long _s;					\
		_s = (unsigned long)_start;				\
		cpu_clean_cache_area(_s, _size);			\
	} while (0)

#define flush_icache_range(_start,_end)					\
	cpu_flush_icache_area((_start), (_end) - (_start))

/*
 * We don't have a MEMC chip...
 */
#define memc_update_all()		do { } while (0)
#define memc_update_mm(mm)		do { } while (0)
#define memc_update_addr(mm,pte,log)	do { } while (0)
#define memc_clear(mm,physaddr)		do { } while (0)

/*
 * This flushes back any buffered write data.  We have to clean the entries
 * in the cache for this page.  This does not invalidate either I or D caches.
 */
#define flush_page_to_ram(_page)					\
	cpu_flush_ram_page((_page) & PAGE_MASK);

/*
 * TLB flushing:
 *
 *  - flush_tlb_all() flushes all processes TLBs
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB's
 *  - flush_tlb_page(vma, vmaddr) flushes one page
 *  - flush_tlb_range(mm, start, end) flushes a range of pages
 *
 * We drain the write buffer in here to ensure that the page tables in ram
 * are really up to date.  It is more efficient to do this here...
 */
#define flush_tlb_all()								\
	cpu_flush_tlb_all()

#define flush_tlb_mm(_mm)							\
	do {									\
		if ((_mm) == current->mm)					\
			cpu_flush_tlb_all();					\
	} while (0)

#define flush_tlb_range(_mm,_start,_end)					\
	do {									\
		if ((_mm) == current->mm)					\
			cpu_flush_tlb_area((_start), (_end), 1);		\
	} while (0)

#define flush_tlb_page(_vma,_vmaddr)						\
	do {									\
		if ((_vma)->vm_mm == current->mm)				\
			cpu_flush_tlb_page((_vmaddr),				\
				 ((_vma)->vm_flags & VM_EXEC));			\
	} while (0)


